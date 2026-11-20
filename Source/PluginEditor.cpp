#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace FLColours
{
    const juce::Colour background      { 0xff282c30 }; // #282C30
    const juce::Colour tabFill         { 0xff212529 }; // #212529
    const juce::Colour tabBorder       { 0xff393f45 }; // #393F45
    const juce::Colour comboFill       { 0xff1d2024 }; // #1D2024
    const juce::Colour comboBorder     { 0xff3a4046 }; // #3A4046
    const juce::Colour textMain        { 0xffe6e9ec }; // #E6E9EC
    const juce::Colour highlightFill   { 0xff8c9aa8 }; // #8C9AA8
    const juce::Colour highlightText   { 0xffffffff }; // White
    const juce::Colour sectionBg       { 0xff31363b }; // #31363B
    const juce::Colour sectionText     { 0xff8a929a }; // #8A929A
}

FLStudioLookAndFeel::FLStudioLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, FLColours::comboFill);
    setColour (juce::ComboBox::outlineColourId,    FLColours::comboBorder);
    setColour (juce::ComboBox::textColourId,       FLColours::textMain);
    setColour (juce::ComboBox::arrowColourId,      FLColours::textMain);

    setColour (juce::PopupMenu::backgroundColourId,            FLColours::comboFill);
    setColour (juce::PopupMenu::textColourId,                  FLColours::textMain);
    setColour (juce::PopupMenu::headerTextColourId,            FLColours::sectionText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, FLColours::highlightFill);
    setColour (juce::PopupMenu::highlightedTextColourId,       FLColours::highlightText);
}

juce::Font FLStudioLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions ("Segoe UI", 13.0f, juce::Font::plain));
}

juce::Font FLStudioLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions ("Segoe UI", 13.0f, juce::Font::plain));
}

void FLStudioLookAndFeel::drawComboBox (juce::Graphics& g,
                                        int width,
                                        int height,
                                        bool /*isButtonDown*/,
                                        int /*buttonX*/,
                                        int /*buttonY*/,
                                        int /*buttonW*/,
                                        int /*buttonH*/,
                                        juce::ComboBox& /*box*/)
{
    const juce::Rectangle<float> bounds (0.5f, 0.5f, static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f);

    g.setColour (FLColours::comboFill);
    g.fillRoundedRectangle (bounds, 2.0f);

    g.setColour (FLColours::comboBorder);
    g.drawRoundedRectangle (bounds, 2.0f, 1.0f);

    const float arrowCenterX = static_cast<float> (width) - 11.0f;
    const float arrowCenterY = static_cast<float> (height) * 0.5f;

    juce::Path arrow;
    arrow.addTriangle (arrowCenterX - 3.5f, arrowCenterY - 1.8f,
                       arrowCenterX + 3.5f, arrowCenterY - 1.8f,
                       arrowCenterX,        arrowCenterY + 2.5f);

    g.setColour (FLColours::textMain);
    g.fillPath (arrow);
}

void FLStudioLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (7, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, FLColours::textMain);
}

void FLStudioLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (FLColours::comboFill);
    g.setColour (FLColours::comboBorder);
    g.drawRect (0, 0, width, height, 1);
}

void FLStudioLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                      const juce::Rectangle<int>& area,
                                                      const juce::String& sectionName)
{
    g.setColour (FLColours::sectionBg);
    g.fillRect (area);

    g.setColour (FLColours::sectionText);
    g.setFont (juce::Font (juce::FontOptions ("Segoe UI", 12.0f, juce::Font::plain)));
    g.drawFittedText (sectionName,
                      area.getX() + 8,
                      area.getY(),
                      area.getWidth() - 12,
                      area.getHeight(),
                      juce::Justification::centredLeft,
                      1);
}

void FLStudioLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                             const juce::Rectangle<int>& area,
                                             bool isSeparator,
                                             bool isActive,
                                             bool isHighlighted,
                                             bool isTicked,
                                             bool /*hasSubMenu*/,
                                             const juce::String& text,
                                             const juce::String& /*shortcutKeyText*/,
                                             const juce::Drawable* /*icon*/,
                                             const juce::Colour* /*textColourToUse*/)
{
    if (isSeparator)
    {
        g.setColour (FLColours::comboBorder);
        g.fillRect (area.reduced (4, 0).withHeight (1));
        return;
    }

    if (! isActive)
    {
        drawPopupMenuSectionHeader (g, area, text);
        return;
    }

    if (isHighlighted)
    {
        g.setColour (FLColours::highlightFill);
        g.fillRect (area);
    }
    else
    {
        g.setColour (FLColours::comboFill);
        g.fillRect (area);
    }

    g.setColour (isHighlighted ? FLColours::highlightText : FLColours::textMain);
    g.setFont (getPopupMenuFont());

    if (isTicked)
    {
        const float dotY = static_cast<float> (area.getCentreY());
        g.fillEllipse (static_cast<float> (area.getX() + 7), dotY - 2.0f, 4.0f, 4.0f);
    }

    g.drawFittedText (text,
                      area.getX() + 18,
                      area.getY(),
                      area.getWidth() - 24,
                      area.getHeight(),
                      juce::Justification::centredLeft,
                      1);
}

void FLStudioLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text,
                                                     bool isSeparator,
                                                     int /*standardMenuItemHeight*/,
                                                     int& idealWidth,
                                                     int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 4;
        return;
    }

    const auto font = getPopupMenuFont();
    idealWidth = juce::GlyphArrangement::getStringWidthInt (font, text) + 36;
    idealHeight = 20;
}

FLResamplingQualityAudioProcessorEditor::FLResamplingQualityAudioProcessorEditor (FLResamplingQualityAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&flLookAndFeel);

    // Populate the ComboBox PopupMenu with exact FL Studio non-clickable section headers
    // and 1-based item IDs (1..9) so ComboBoxAttachment maps 1:1 with the 9 parameter choices.
    if (auto* rootMenu = qualityComboBox.getRootMenu())
    {
        rootMenu->addSectionHeader ("Low quality / fastest");
        rootMenu->addItem (1, "2-point linear");
        rootMenu->addItem (2, "6-point Hermite");

        rootMenu->addSectionHeader ("Average quality");
        rootMenu->addItem (3, "16-point sinc");
        rootMenu->addItem (4, "24-point sinc");
        rootMenu->addItem (5, "32-point sinc");

        rootMenu->addSectionHeader ("High quality / slowest");
        rootMenu->addItem (6, "64-point sinc");
        rootMenu->addItem (7, "128-point sinc");
        rootMenu->addItem (8, "256-point sinc");
        rootMenu->addItem (9, "512-point sinc");
    }

    qualityComboBox.setEditableText (false);
    qualityComboBox.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (qualityComboBox);

    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        audioProcessor.getAPVTS(), "quality", qualityComboBox);

    setSize (310, 76);
    setResizable (false, false);
}

FLResamplingQualityAudioProcessorEditor::~FLResamplingQualityAudioProcessorEditor()
{
    qualityAttachment.reset();
    qualityComboBox.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void FLResamplingQualityAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Main background (#282C30)
    g.fillAll (FLColours::background);

    // Top "Mixer" tab (#212529) with #393F45 border
    const juce::Rectangle<int> tabRect (10, 6, 54, 21);
    const int panelTopY = tabRect.getBottom() - 1;
    const juce::Rectangle<int> contentFrame (10, panelTopY, getWidth() - 20, getHeight() - panelTopY - 8);

    g.setColour (FLColours::tabFill);
    g.fillRect (tabRect);

    g.setColour (FLColours::tabBorder);
    g.drawLine (static_cast<float> (tabRect.getX()),     static_cast<float> (tabRect.getY()),
                static_cast<float> (tabRect.getRight()), static_cast<float> (tabRect.getY()), 1.0f);
    g.drawLine (static_cast<float> (tabRect.getX()),     static_cast<float> (tabRect.getY()),
                static_cast<float> (tabRect.getX()),     static_cast<float> (tabRect.getBottom()), 1.0f);
    g.drawLine (static_cast<float> (tabRect.getRight()), static_cast<float> (tabRect.getY()),
                static_cast<float> (tabRect.getRight()), static_cast<float> (tabRect.getBottom()), 1.0f);

    g.drawLine (static_cast<float> (tabRect.getRight()), static_cast<float> (panelTopY),
                static_cast<float> (contentFrame.getRight()), static_cast<float> (panelTopY), 1.0f);

    g.drawLine (static_cast<float> (contentFrame.getX()),     static_cast<float> (panelTopY),
                static_cast<float> (contentFrame.getX()),     static_cast<float> (contentFrame.getBottom()), 1.0f);
    g.drawLine (static_cast<float> (contentFrame.getRight()), static_cast<float> (panelTopY),
                static_cast<float> (contentFrame.getRight()), static_cast<float> (contentFrame.getBottom()), 1.0f);
    g.drawLine (static_cast<float> (contentFrame.getX()),     static_cast<float> (contentFrame.getBottom()),
                static_cast<float> (contentFrame.getRight()), static_cast<float> (contentFrame.getBottom()), 1.0f);

    // "Mixer" tab label
    g.setColour (FLColours::textMain);
    g.setFont (juce::Font (juce::FontOptions ("Segoe UI", 12.5f, juce::Font::plain)));
    g.drawText ("Mixer", tabRect, juce::Justification::centred, false);

    // "Resampling quality" label to the right of the ComboBox (Segoe UI)
    g.setColour (FLColours::textMain);
    g.setFont (juce::Font (juce::FontOptions ("Segoe UI", 13.0f, juce::Font::plain)));
    const juce::Rectangle<int> labelBounds (150, 36, 145, 22);
    g.drawText ("Resampling quality", labelBounds, juce::Justification::centredLeft, false);
}

void FLResamplingQualityAudioProcessorEditor::resized()
{
    qualityComboBox.setBounds (20, 36, 122, 22);
}


