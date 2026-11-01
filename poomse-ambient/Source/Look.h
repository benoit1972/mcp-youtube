#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Minimal, flat, dark look with a single warm accent: thin ring knobs, spaced capitals, pill buttons.
namespace look
{
// a rainy night: near-black blue-grey, with the warm amber of a street lamp as the only accent
const juce::Colour bgTop    { 0xff15181e };
const juce::Colour bgBottom { 0xff08090c };
const juce::Colour panel    { 0xff1b1f27 };
const juce::Colour track    { 0xff2c323d };
const juce::Colour line     { 0xff262b34 };
const juce::Colour lineHi   { 0xff4b5463 };
const juce::Colour text     { 0xffe9e6df };
const juce::Colour dim      { 0xff8a93a3 };
const juce::Colour accent   { 0xffeba646 };
const juce::Colour accentHi { 0xfff6bf6e };
const juce::Colour negative { 0xff6d8fd6 };
const juce::Colour onAccent { 0xff1c1103 };   // text on accent-coloured buttons
const juce::Colour moodLight { 0xff5c6f8f }, moodDeep { 0xff2a2f5e }, moodStill { 0xff30343d };

inline juce::Font caps (float size, bool bold = true, float kerning = 0.14f)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}
} // namespace look

class PoomseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PoomseLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, look::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, look::accent.withAlpha (0.35f));
        setColour (juce::TextEditor::backgroundColourId, look::panel);
        setColour (juce::TextEditor::textColourId, look::text);
        setColour (juce::TextEditor::outlineColourId, look::accent);
        setColour (juce::CaretComponent::caretColourId, look::accent);
        setColour (juce::ComboBox::backgroundColourId, look::panel);
        setColour (juce::ComboBox::textColourId, look::text);
        setColour (juce::ComboBox::outlineColourId, look::line);
        setColour (juce::ComboBox::arrowColourId, look::dim);
        setColour (juce::PopupMenu::backgroundColourId, look::panel);
        setColour (juce::PopupMenu::textColourId, look::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, look::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, look::onAccent);
        setColour (juce::BubbleComponent::backgroundColourId, look::panel);
        setColour (juce::BubbleComponent::outlineColourId, look::accent);
        setColour (juce::TooltipWindow::textColourId, look::text);
    }

    // thin bipolar bar used for the macro amounts
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const float x0 = (float) x + 5.f, x1 = (float) (x + w) - 5.f, cy = (float) y + (float) h * 0.5f;
        const float zero = x0 + (x1 - x0) * (float) s.valueToProportionOfLength (std::max (0.0, s.getMinimum()));
        const float pos = x0 + (x1 - x0) * (float) s.valueToProportionOfLength (s.getValue());
        const bool hot = s.isMouseOverOrDragging();

        g.setColour (look::track);
        g.fillRoundedRectangle (x0, cy - 1.5f, x1 - x0, 3.f, 1.5f);
        if (s.getMinimum() < 0.0)
        {
            g.setColour (look::lineHi);
            g.fillRect (zero - 0.5f, cy - 4.f, 1.f, 8.f);
        }
        if (std::abs (pos - zero) > 0.5f)
        {
            g.setColour (hot ? look::accentHi : look::accent);
            g.fillRoundedRectangle (std::min (zero, pos), cy - 1.5f, std::abs (pos - zero), 3.f, 1.5f);
        }
        g.setColour (look::text);
        g.fillEllipse (pos - 4.f, cy - 4.f, 8.f, 8.f);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (11.5f)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 20, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
        g.setColour (look::panel);
        g.fillRoundedRectangle (r, 5.f);
        g.setColour (box.isMouseOver() ? look::lineHi : look::line);
        g.drawRoundedRectangle (r, 5.f, 1.f);
        juce::Path chevron;
        const float cx = (float) w - 10.f, cy = (float) h * 0.5f;
        chevron.startNewSubPath (cx - 3.f, cy - 1.5f);
        chevron.lineTo (cx, cy + 1.5f);
        chevron.lineTo (cx + 3.f, cy - 1.5f);
        g.setColour (look::dim);
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.f);
        const float dia = std::min (bounds.getWidth(), bounds.getHeight());
        const auto c = bounds.getCentre();
        const float r = dia * 0.5f;
        const float thick = juce::jlimit (2.5f, 5.f, dia * 0.075f);
        const float arcR = r - thick * 0.5f;
        const float innerR = arcR - thick * 1.7f;
        const bool hot = s.isMouseOverOrDragging();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // body
        g.setColour (look::panel);
        g.fillEllipse (c.x - innerR, c.y - innerR, innerR * 2.f, innerR * 2.f);
        g.setColour (hot ? look::lineHi : look::line);
        g.drawEllipse (c.x - innerR, c.y - innerR, innerR * 2.f, innerR * 2.f, 1.f);

        // track
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startAngle, endAngle, true);
        g.setColour (look::track);
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc (from the centre for bipolar parameters)
        float from = startAngle;
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            from = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);
        if (std::abs (angle - from) > 0.002f)
        {
            juce::Path val;
            val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, std::min (from, angle), std::max (from, angle), true);
            g.setColour (hot ? look::accentHi : look::accent);
            g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // pointer
        juce::Path ptr;
        ptr.startNewSubPath (0.f, -innerR * 0.28f);
        ptr.lineTo (0.f, -innerR * 0.88f);
        ptr.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (look::text);
        g.strokePath (ptr, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr && ! label.isBeingEdited())
        {
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (juce::Font (juce::FontOptions (12.f)));
            g.drawText (label.getText(), label.getLocalBounds(), juce::Justification::centred, false);
            return;
        }
        juce::LookAndFeel_V4::drawLabel (g, label);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool down) override
    {
        auto r = b.getLocalBounds().toFloat();
        const auto id = b.getComponentID();
        if (id == "tab")
        {
            if (b.getToggleState())
            {
                g.setColour (look::accent);
                g.fillRect (r.removeFromBottom (2.f));
            }
            else if (hl)
            {
                g.setColour (look::lineHi);
                g.fillRect (r.removeFromBottom (2.f));
            }
            return;
        }
        auto rr = r.reduced (0.75f);
        if (id == "pillAccent" || (id == "toggle" && b.getToggleState()))
        {
            g.setColour (down ? look::accent.darker (0.2f) : hl ? look::accentHi : look::accent);
            g.fillRoundedRectangle (rr, rr.getHeight() * 0.5f);
        }
        else
        {
            g.setColour (hl || id == "toggle" ? look::lineHi : look::line);
            g.drawRoundedRectangle (rr, rr.getHeight() * 0.5f, 1.4f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto id = b.getComponentID();
        juce::Colour c = look::text;
        if (id == "tab") c = b.getToggleState() ? look::text : look::dim;
        else if (id == "pillAccent" || (id == "toggle" && b.getToggleState())) c = look::onAccent;
        g.setColour (c);
        g.setFont (look::caps (id == "tab" ? 12.5f : 11.5f));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};
