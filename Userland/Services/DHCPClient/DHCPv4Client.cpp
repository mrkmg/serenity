/*
 * Copyright (c) 2020, The SerenityOS developers.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "DHCPv4Client.h"
#include <AK/ByteBuffer.h>
#include <AK/Debug.h>
#include <AK/Endian.h>
#include <AK/Function.h>
#include <AK/Random.h>
#include <LibCore/SocketAddress.h>
#include <LibCore/Timer.h>
#include <stdio.h>

static void send(const InterfaceDescriptor& iface, const DHCPv4Packet& packet, Core::Object*)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        dbgln("ERROR: socket :: {}", strerror(errno));
        return;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface.m_ifname.characters(), IFNAMSIZ) < 0) {
        dbgln("ERROR: setsockopt(SO_BINDTODEVICE) :: {}", strerror(errno));
        return;
    }

    sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(67);
    dst.sin_addr.s_addr = IPv4Address { 255, 255, 255, 255 }.to_u32();
    memset(&dst.sin_zero, 0, sizeof(dst.sin_zero));

    dbgln_if(DHCPV4CLIENT_DEBUG, "sendto({} bound to {}, ..., {} at {}) = ...?", fd, iface.m_ifname, dst.sin_addr.s_addr, dst.sin_port);
    auto rc = sendto(fd, &packet, sizeof(packet), 0, (sockaddr*)&dst, sizeof(dst));
    dbgln_if(DHCPV4CLIENT_DEBUG, "sendto({}) = {}", fd, rc);
    if (rc < 0) {
        dbgln("sendto failed with {}", strerror(errno));
        // FIXME: what do we do here?
    }
}

static void set_params(const InterfaceDescriptor& iface, const IPv4Address& ipv4_addr, const IPv4Address& netmask, const IPv4Address& gateway)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
        dbgln("ERROR: socket :: {}", strerror(errno));
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    bool fits = iface.m_ifname.copy_characters_to_buffer(ifr.ifr_name, IFNAMSIZ);
    if (!fits) {
        dbgln("Interface name doesn't fit into IFNAMSIZ!");
        return;
    }

    // set the IP address
    ifr.ifr_addr.sa_family = AF_INET;
    ((sockaddr_in&)ifr.ifr_addr).sin_addr.s_addr = ipv4_addr.to_in_addr_t();

    if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) {
        dbgln("ERROR: ioctl(SIOCSIFADDR) :: {}", strerror(errno));
    }

    // set the network mask
    ((sockaddr_in&)ifr.ifr_netmask).sin_addr.s_addr = netmask.to_in_addr_t();

    if (ioctl(fd, SIOCSIFNETMASK, &ifr) < 0) {
        dbgln("ERROR: ioctl(SIOCSIFNETMASK) :: {}", strerror(errno));
    }

    // set the default gateway
    struct rtentry rt;
    memset(&rt, 0, sizeof(rt));

    rt.rt_dev = const_cast<char*>(iface.m_ifname.characters());
    rt.rt_gateway.sa_family = AF_INET;
    ((sockaddr_in&)rt.rt_gateway).sin_addr.s_addr = gateway.to_in_addr_t();
    rt.rt_flags = RTF_UP | RTF_GATEWAY;

    if (ioctl(fd, SIOCADDRT, &rt) < 0) {
        dbgln("Error: ioctl(SIOCADDRT) :: {}", strerror(errno));
    }
}

DHCPv4Client::DHCPv4Client(Vector<InterfaceDescriptor> ifnames)
    : m_ifnames(ifnames)
{
    m_server = Core::UDPServer::construct(this);
    m_server->on_ready_to_receive = [this] {
        auto buffer = m_server->receive(sizeof(DHCPv4Packet));
        dbgln_if(DHCPV4CLIENT_DEBUG, "Received {} bytes", buffer.size());
        if (buffer.size() < sizeof(DHCPv4Packet) - DHCPV4_OPTION_FIELD_MAX_LENGTH + 1 || buffer.size() > sizeof(DHCPv4Packet)) {
            dbgln("we expected {}-{} bytes, this is a bad packet", sizeof(DHCPv4Packet) - DHCPV4_OPTION_FIELD_MAX_LENGTH + 1, sizeof(DHCPv4Packet));
            return;
        }
        auto& packet = *(DHCPv4Packet*)buffer.data();
        process_incoming(packet);
    };

    if (!m_server->bind({}, 68)) {
        dbgln("The server we just created somehow came already bound, refusing to continue");
        VERIFY_NOT_REACHED();
    }

    for (auto& iface : m_ifnames)
        dhcp_discover(iface);
}

DHCPv4Client::~DHCPv4Client()
{
}

void DHCPv4Client::handle_offer(const DHCPv4Packet& packet, const ParsedDHCPv4Options& options)
{
    dbgln("We were offered {} for {}", packet.yiaddr().to_string(), options.get<u32>(DHCPOption::IPAddressLeaseTime).value_or(0));
    auto* transaction = const_cast<DHCPv4Transaction*>(m_ongoing_transactions.get(packet.xid()).value_or(nullptr));
    if (!transaction) {
        dbgln("we're not looking for {}", packet.xid());
        return;
    }
    if (transaction->has_ip)
        return;
    if (transaction->accepted_offer) {
        // we've accepted someone's offer, but they haven't given us an ack
        // TODO: maybe record this offer?
        return;
    }
    // TAKE IT...
    transaction->offered_lease_time = options.get<u32>(DHCPOption::IPAddressLeaseTime).value();
    dhcp_request(*transaction, packet);
}

void DHCPv4Client::handle_ack(const DHCPv4Packet& packet, const ParsedDHCPv4Options& options)
{
    if constexpr (DHCPV4CLIENT_DEBUG) {
        dbgln("The DHCP server handed us {}", packet.yiaddr().to_string());
        dbgln("Here are the options: {}", options.to_string());
    }

    auto* transaction = const_cast<DHCPv4Transaction*>(m_ongoing_transactions.get(packet.xid()).value_or(nullptr));
    if (!transaction) {
        dbgln("we're not looking for {}", packet.xid());
        return;
    }
    transaction->has_ip = true;
    auto& interface = transaction->interface;
    auto new_ip = packet.yiaddr();
    interface.m_current_ip_address = new_ip;
    auto lease_time = AK::convert_between_host_and_network_endian(options.get<u32>(DHCPOption::IPAddressLeaseTime).value_or(transaction->offered_lease_time));
    // set a timer for the duration of the lease, we shall renew if needed
    Core::Timer::create_single_shot(
        lease_time * 1000,
        [this, transaction, interface = InterfaceDescriptor { interface }] {
            transaction->accepted_offer = false;
            transaction->has_ip = false;
            dhcp_discover(interface);
        },
        this);
    set_params(transaction->interface, new_ip, options.get<IPv4Address>(DHCPOption::SubnetMask).value(), options.get_many<IPv4Address>(DHCPOption::Router, 1).first());
}

void DHCPv4Client::handle_nak(const DHCPv4Packet& packet, const ParsedDHCPv4Options& options)
{
    dbgln("The DHCP server told us to go chase our own tail about {}", packet.yiaddr().to_string());
    dbgln("Here are the options: {}", options.to_string());
    // make another request a bit later :shrug:
    auto* transaction = const_cast<DHCPv4Transaction*>(m_ongoing_transactions.get(packet.xid()).value_or(nullptr));
    if (!transaction) {
        dbgln("we're not looking for {}", packet.xid());
        return;
    }
    transaction->accepted_offer = false;
    transaction->has_ip = false;
    auto& iface = transaction->interface;
    Core::Timer::create_single_shot(
        10000,
        [this, iface = InterfaceDescriptor { iface }] {
            dhcp_discover(iface);
        },
        this);
}

void DHCPv4Client::process_incoming(const DHCPv4Packet& packet)
{
    auto options = packet.parse_options();

    dbgln_if(DHCPV4CLIENT_DEBUG, "Here are the options: {}", options.to_string());

    auto value = options.get<DHCPMessageType>(DHCPOption::DHCPMessageType).value();
    switch (value) {
    case DHCPMessageType::DHCPOffer:
        handle_offer(packet, options);
        break;
    case DHCPMessageType::DHCPAck:
        handle_ack(packet, options);
        break;
    case DHCPMessageType::DHCPNak:
        handle_nak(packet, options);
        break;
    case DHCPMessageType::DHCPDiscover:
    case DHCPMessageType::DHCPRequest:
    case DHCPMessageType::DHCPRelease:
        // These are not for us
        // we're just getting them because there are other people on our subnet
        // broadcasting stuff
        break;
    case DHCPMessageType::DHCPDecline:
    default:
        dbgln("I dunno what to do with this {}", (u8)value);
        VERIFY_NOT_REACHED();
        break;
    }
}

void DHCPv4Client::dhcp_discover(const InterfaceDescriptor& iface)
{
    auto transaction_id = get_random<u32>();

    if constexpr (DHCPV4CLIENT_DEBUG) {
        dbgln("Trying to lease an IP for {} with ID {}", iface.m_ifname, transaction_id);
        if (!iface.m_current_ip_address.is_zero())
            dbgln("going to request the server to hand us {}", iface.m_current_ip_address.to_string());
    }

    DHCPv4PacketBuilder builder;

    DHCPv4Packet& packet = builder.peek();
    packet.set_op(DHCPv4Op::BootRequest);
    packet.set_htype(1); // 10mb ethernet
    packet.set_hlen(sizeof(MACAddress));
    packet.set_xid(transaction_id);
    packet.set_flags(DHCPv4Flags::Broadcast);
    packet.ciaddr() = iface.m_current_ip_address;
    packet.set_chaddr(iface.m_mac_address);
    packet.set_secs(65535); // we lie

    // set packet options
    builder.set_message_type(DHCPMessageType::DHCPDiscover);
    auto& dhcp_packet = builder.build();

    // broadcast the discover request
    send(iface, dhcp_packet, this);
    m_ongoing_transactions.set(transaction_id, make<DHCPv4Transaction>(iface));
}

void DHCPv4Client::dhcp_request(DHCPv4Transaction& transaction, const DHCPv4Packet& offer)
{
    auto& iface = transaction.interface;
    dbgln("Leasing the IP {} for adapter {}", offer.yiaddr().to_string(), iface.m_ifname);
    DHCPv4PacketBuilder builder;

    DHCPv4Packet& packet = builder.peek();
    packet.set_op(DHCPv4Op::BootRequest);
    packet.ciaddr() = iface.m_current_ip_address;
    packet.set_htype(1); // 10mb ethernet
    packet.set_hlen(sizeof(MACAddress));
    packet.set_xid(offer.xid());
    packet.set_flags(DHCPv4Flags::Broadcast);
    packet.set_chaddr(iface.m_mac_address);
    packet.set_secs(65535); // we lie

    // set packet options
    builder.set_message_type(DHCPMessageType::DHCPRequest);
    builder.add_option(DHCPOption::RequestedIPAddress, sizeof(IPv4Address), &offer.yiaddr());
    auto& dhcp_packet = builder.build();

    // broadcast the "request" request
    send(iface, dhcp_packet, this);
    transaction.accepted_offer = true;
}
