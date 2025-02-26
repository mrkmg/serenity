/*
 * Copyright (c) 2020-2021, Andreas Kling <kling@serenityos.org>
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

#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/HTMLIFrameElement.h>
#include <LibWeb/Layout/FrameBox.h>
#include <LibWeb/Origin.h>
#include <LibWeb/Page/Frame.h>

namespace Web::HTML {

HTMLIFrameElement::HTMLIFrameElement(DOM::Document& document, QualifiedName qualified_name)
    : FrameHostElement(document, move(qualified_name))
{
}

HTMLIFrameElement::~HTMLIFrameElement()
{
}

RefPtr<Layout::Node> HTMLIFrameElement::create_layout_node()
{
    auto style = document().style_resolver().resolve_style(*this);
    return adopt(*new Layout::FrameBox(document(), *this, move(style)));
}

void HTMLIFrameElement::parse_attribute(const FlyString& name, const String& value)
{
    HTMLElement::parse_attribute(name, value);
    if (name == HTML::AttributeNames::src)
        load_src(value);
}

void HTMLIFrameElement::inserted_into(Node& parent)
{
    FrameHostElement::inserted_into(parent);
    if (is_connected())
        load_src(attribute(HTML::AttributeNames::src));
}

void HTMLIFrameElement::load_src(const String& value)
{
    if (!m_content_frame)
        return;

    auto url = document().complete_url(value);
    if (!url.is_valid()) {
        dbgln("iframe failed to load URL: Invalid URL: {}", value);
        return;
    }
    if (url.protocol() == "file" && document().origin().protocol() != "file") {
        dbgln("iframe failed to load URL: Security violation: {} may not load {}", document().url(), url);
        return;
    }

    dbgln("Loading iframe document from {}", value);
    m_content_frame->loader().load(url, FrameLoader::Type::IFrame);
}

}
