#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class FLStudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FLStudioLookAndFeel();
    ~FLStudioLookAndFeel() override = default;

    juce::Font getComboBoxFont (juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;

    void drawComboBox (juce::Graphics& g,
                       int width,
                       int height,
                       bool isButtonDown,
                       int buttonX,
                       int buttonY,
                       int buttonW,
                       int buttonH,
                       juce::ComboBox& box) override;

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override;

    void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override;

    void drawPopupMenuSectionHeader (juce::Graphics& g,
                                     const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;

    void drawPopupMenuItem (juce::Graphics& g,
                            const juce::Rectangle<int>& area,
                            bool isSeparator,
                            bool isActive,
                            bool isHighlighted,
                            bool isTicked,
                            bool hasSubMenu,
                            const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* icon,
                            const juce::Colour* textColourToUse) override;

    void getIdealPopupMenuItemSize (const juce::String& text,
                                    bool isSeparator,
                                    int standardMenuItemHeight,
                                    int& idealWidth,
                                    int& idealHeight) override;
};

class FLResamplingQualityAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FLResamplingQualityAudioProcessorEditor (FLResamplingQualityAudioProcessor&);
    ~FLResamplingQualityAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FLResamplingQualityAudioProcessor& audioProcessor;
    FLStudioLookAndFeel flLookAndFeel;

    juce::ComboBox qualityComboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FLResamplingQualityAudioProcessorEditor)
};
