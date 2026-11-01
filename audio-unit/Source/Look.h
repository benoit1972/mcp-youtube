#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Minimal, flat, dark look with a single warm accent: thin ring knobs, spaced capitals, pill buttons.
namespace look
{
const juce::Colour bgTop    { 0xff191a1e };
const juce::Colour bgBottom { 0xff0f1013 };
const juce::Colour panel    { 0xff202228 };
const juce::Colour track    { 0xff33353c };
const juce::Colour line     { 0xff2c2e35 };
const juce::Colour lineHi   { 0xff4a4d57 };
const juce::Colour text     { 0xffebe7df };
const juce::Colour dim      { 0xff80838c };
const juce::Colour accent   { 0xffff5a36 };
const juce::Colour accentHi { 0xffff7d5e };

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
        if (id == "pillAccent")
        {
            g.setColour (down ? look::accent.darker (0.2f) : hl ? look::accentHi : look::accent);
            g.fillRoundedRectangle (rr, rr.getHeight() * 0.5f);
        }
        else
        {
            g.setColour (hl ? look::lineHi : look::line);
            g.drawRoundedRectangle (rr, rr.getHeight() * 0.5f, 1.4f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto id = b.getComponentID();
        juce::Colour c = look::text;
        if (id == "tab") c = b.getToggleState() ? look::text : look::dim;
        else if (id == "pillAccent") c = juce::Colour (0xff1a0d09);
        g.setColour (c);
        g.setFont (look::caps (id == "tab" ? 12.5f : 11.5f));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};
