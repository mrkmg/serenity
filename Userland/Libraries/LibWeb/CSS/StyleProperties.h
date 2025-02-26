/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
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

#pragma once

#include <AK/HashMap.h>
#include <AK/NonnullRefPtr.h>
#include <LibGfx/Font.h>
#include <LibGfx/Forward.h>
#include <LibWeb/CSS/LengthBox.h>
#include <LibWeb/CSS/StyleValue.h>

namespace Web::CSS {

class StyleProperties : public RefCounted<StyleProperties> {
public:
    StyleProperties();

    explicit StyleProperties(const StyleProperties&);

    static NonnullRefPtr<StyleProperties> create() { return adopt(*new StyleProperties); }

    NonnullRefPtr<StyleProperties> clone() const;

    template<typename Callback>
    inline void for_each_property(Callback callback) const
    {
        for (auto& it : m_property_values)
            callback((CSS::PropertyID)it.key, *it.value);
    }

    void set_property(CSS::PropertyID, NonnullRefPtr<StyleValue> value);
    void set_property(CSS::PropertyID, const StringView&);
    Optional<NonnullRefPtr<StyleValue>> property(CSS::PropertyID) const;

    Length length_or_fallback(CSS::PropertyID, const Length& fallback) const;
    LengthBox length_box(CSS::PropertyID left_id, CSS::PropertyID top_id, CSS::PropertyID right_id, CSS::PropertyID bottom_id, const CSS::Length& default_value) const;
    String string_or_fallback(CSS::PropertyID, const StringView& fallback) const;
    Color color_or_fallback(CSS::PropertyID, const DOM::Document&, Color fallback) const;
    Optional<CSS::TextAlign> text_align() const;
    CSS::Display display() const;
    Optional<CSS::Float> float_() const;
    Optional<CSS::Clear> clear() const;
    Optional<CSS::Cursor> cursor() const;
    Optional<CSS::WhiteSpace> white_space() const;
    Optional<CSS::LineStyle> line_style(CSS::PropertyID) const;
    Optional<CSS::TextDecorationLine> text_decoration_line() const;
    Optional<CSS::TextTransform> text_transform() const;
    Optional<CSS::ListStyleType> list_style_type() const;
    Optional<CSS::FlexDirection> flex_direction() const;
    Optional<CSS::Overflow> overflow_x() const;
    Optional<CSS::Overflow> overflow_y() const;
    Optional<CSS::Repeat> background_repeat() const;

    const Gfx::Font& font() const
    {
        if (!m_font)
            load_font();
        return *m_font;
    }

    float line_height(const Layout::Node&) const;

    bool operator==(const StyleProperties&) const;
    bool operator!=(const StyleProperties& other) const { return !(*this == other); }

    Optional<CSS::Position> position() const;
    Optional<int> z_index() const;

private:
    HashMap<unsigned, NonnullRefPtr<StyleValue>> m_property_values;
    Optional<CSS::Overflow> overflow(CSS::PropertyID) const;

    void load_font() const;

    mutable RefPtr<Gfx::Font> m_font;
};

}
