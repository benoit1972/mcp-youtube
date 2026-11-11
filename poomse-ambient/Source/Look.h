#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// A blank sheet of paper: warm white, black ink, one moss green mark. Thin lines, no gradients, no shadows.
namespace look
{
const juce::Colour paper   { 0xfff5f1e8 };
const juce::Colour paper2  { 0xffebe5d6 };
const juce::Colour rule    { 0xffd8d0bd };
const juce::Colour ink     { 0xff1e1c19 };
const juce::Colour dim     { 0xff8d8677 };
const juce::Colour accent  { 0xff4f8a5b };

inline juce::Font caps (float size, bool bold = true, float kerning = 0.14f)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor (kerning);
}
} // namespace look

class PaperLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PaperLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, look::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, look::accent.withAlpha (0.3f));
        setColour (juce::TextEditor::backgroundColourId, look::paper);
        setColour (juce::TextEditor::textColourId, look::ink);
        setColour (juce::TextEditor::outlineColourId, look::accent);
        setColour (juce::CaretComponent::caretColourId, look::accent);
        setColour (juce::ComboBox::backgroundColourId, look::paper);
        setColour (juce::ComboBox::textColourId, look::ink);
        setColour (juce::ComboBox::outlineColourId, look::rule);
        setColour (juce::PopupMenu::backgroundColourId, look::paper);
        setColour (juce::PopupMenu::textColourId, look::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, look::ink);
        setColour (juce::PopupMenu::highlightedTextColourId, look::paper);
        setColour (juce::BubbleComponent::backgroundColourId, look::paper);
        setColour (juce::BubbleComponent::outlineColourId, look::ink);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.f)); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.isMouseOver() ? look::ink : look::rule);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.f);
        juce::Path chevron;
        const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
        chevron.startNewSubPath (cx - 3.f, cy - 1.5f); chevron.lineTo (cx, cy + 1.5f); chevron.lineTo (cx + 3.f, cy - 1.5f);
        g.setColour (look::dim);
        g.strokePath (chevron, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // a thin ring and a hand: nothing else
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
        const float dia = std::min (b.getWidth(), b.getHeight()), r = dia * 0.5f;
        const auto c = b.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool hot = s.isMouseOverOrDragging();

        g.setColour (hot ? look::ink : look::rule);
        g.drawEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f, 1.2f);

        float from = startAngle;
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) from = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);
        if (std::abs (angle - from) > 0.002f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r, r, 0.f, std::min (from, angle), std::max (from, angle), true);
            g.setColour (look::accent);
            g.strokePath (arc, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::Path hand;
        hand.startNewSubPath (0.f, -r * 0.25f);
        hand.lineTo (0.f, -r * 0.82f);
        hand.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
        g.setColour (look::ink);
        g.strokePath (hand, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr && ! label.isBeingEdited())
        {
            g.setColour (look::ink);
            g.setFont (juce::Font (juce::FontOptions (12.f)));
            g.drawText (label.getText(), label.getLocalBounds(), juce::Justification::centred, false);
            return;
        }
        juce::LookAndFeel_V4::drawLabel (g, label);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hl, bool down) override
    {
        auto rr = b.getLocalBounds().toFloat().reduced (0.75f);
        const bool on = b.getComponentID() == "ink" || (b.getComponentID() == "toggle" && b.getToggleState());
        if (on) { g.setColour (down ? look::accent.darker (0.2f) : look::accent); g.fillRoundedRectangle (rr, rr.getHeight() * 0.5f); }
        else    { g.setColour (hl ? look::ink : look::rule); g.drawRoundedRectangle (rr, rr.getHeight() * 0.5f, 1.2f); }
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const bool on = b.getComponentID() == "ink" || (b.getComponentID() == "toggle" && b.getToggleState());
        g.setColour (on ? look::paper : look::ink);
        g.setFont (look::caps (11.f));
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(), juce::Justification::centred, false);
    }
};
