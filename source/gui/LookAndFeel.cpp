#include "LookAndFeel.h"

namespace harmeq
{
HarmLookAndFeel::HarmLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, line);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, muted);
    setColour (juce::TextButton::buttonColourId, well);
    setColour (juce::TextButton::buttonOnColourId, line);
    setColour (juce::TextButton::textColourOffId, muted);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::Slider::rotarySliderFillColourId, band (0));
    setColour (juce::Slider::textBoxOutlineColourId, line);
    setColour (juce::ToggleButton::tickColourId, band (0));
    setColour (juce::BubbleComponent::backgroundColourId, panel);
    setColour (juce::BubbleComponent::outlineColourId, line);
    setColour (juce::TooltipWindow::backgroundColourId, panel);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, line);
}

void HarmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float lineW = juce::jmax (2.5f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;
    const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::line.withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    if (std::abs (angle - fromAngle) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (fromAngle, angle),
                             juce::jmax (fromAngle, angle), true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
        g.strokePath (value, stroke);
    }

    const float bodyR = radius - lineW - 2.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a3038).withMultipliedAlpha (alpha), centre.x,
                                             centre.y - bodyR, juce::Colour (0xff181c22).withMultipliedAlpha (alpha),
                                             centre.x, centre.y + bodyR, false));
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));

    const auto tip = centre.getPointOnCircumference (bodyR * 0.85f, angle);
    g.setColour (theme::text.withMultipliedAlpha (alpha));
    g.drawLine ({ centre.getPointOnCircumference (bodyR * 0.2f, angle), tip }, 1.8f);
}

// Value boxes: a vertical bar slider drawn as a box showing its text, dragged up/down.
void HarmLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float minPos, float maxPos, juce::Slider::SliderStyle style,
                                        juce::Slider& s)
{
    if (style != juce::Slider::LinearBarVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, s);
        return;
    }

    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (0.5f);
    g.setColour (theme::well);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (s.isMouseOverOrDragging() && s.isEnabled() ? theme::faint : theme::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    g.setColour (s.isEnabled() ? theme::text : theme::faint);
    g.setFont (theme::numbers (12.0f));
    g.drawFittedText (s.getTextFromValue (s.getValue()), r.toNearestInt().reduced (4, 0),
                      juce::Justification::centred, 1, 0.8f);
}

void HarmLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                    juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (theme::well);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.isMouseOver (true) && box.isEnabled() ? theme::faint : theme::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    if ((bool) box.getProperties().getWithDefault ("arrow", false))
    {
        const auto a = juce::Rectangle<float> ((float) width - 16.0f, 0.0f, 10.0f, (float) height)
                           .withSizeKeepingCentre (7.0f, 4.0f);
        juce::Path p;
        p.startNewSubPath (a.getX(), a.getY());
        p.lineTo (a.getCentreX(), a.getBottom());
        p.lineTo (a.getRight(), a.getY());
        g.setColour (theme::muted.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

juce::Font HarmLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return theme::label (12.0f);
}

void HarmLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const bool arrow = (bool) box.getProperties().getWithDefault ("arrow", false);
    // Narrow boxes (the slope menu) centre their text; wider ones read from the left.
    const bool narrow = box.getWidth() < 50;
    label.setBounds (narrow ? 1 : 6, 1, box.getWidth() - (arrow ? 22 : narrow ? 2 : 8), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (narrow ? juce::Justification::centred : juce::Justification::centredLeft);
}

void HarmLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                            bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto segment = b.getProperties().getWithDefault ("segment", "").toString();
    const bool on = b.getToggleState();
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    constexpr float radius = 4.0f;

    juce::Path shape;
    const bool roundLeft = segment.isEmpty() || segment == "left";
    const bool roundRight = segment.isEmpty() || segment == "right";
    shape.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), radius, radius, roundLeft,
                               roundRight, roundLeft, roundRight);

    auto fill = on ? accent : theme::well;
    if (down)
        fill = fill.brighter (0.1f);
    else if (highlighted && ! on)
        fill = fill.brighter (0.05f);

    g.setColour (fill);
    g.fillPath (shape);
    g.setColour (on && accent != theme::line ? accent.brighter (0.3f) : theme::line);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void HarmLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (theme::caps (11.0f));
    g.setColour (b.getToggleState() ? theme::text : theme::muted);
    g.drawFittedText (b.getButtonText().toUpperCase(), b.getLocalBounds().reduced (3, 0),
                      juce::Justification::centred, 1, 0.8f);
}

// On/off switches: a small pill with a sliding dot in the accent colour.
void HarmLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().withSizeKeepingCentre (28.0f, 15.0f);
    const bool on = b.getToggleState();
    g.setColour (theme::well);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (highlighted ? theme::faint : theme::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    const float d = r.getHeight() - 5.0f;
    const auto dot = juce::Rectangle<float> (d, d).withCentre (
        { on ? r.getRight() - 2.5f - d * 0.5f : r.getX() + 2.5f + d * 0.5f, r.getCentreY() });
    g.setColour (on ? b.findColour (juce::ToggleButton::tickColourId) : theme::faint);
    g.fillEllipse (dot);
}
} // namespace harmeq
