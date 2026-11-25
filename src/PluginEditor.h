// PVR VoiceMorph - interfaz.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

class PVRLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PVRLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class XYPad : public juce::Component
{
public:
    explicit XYPad (PVRVoiceMorphProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refreshLabels();

private:
    void setFromMouse (juce::Point<float>);
    juce::Rectangle<float> padArea() const;
    PVRVoiceMorphProcessor& proc;
    juce::ParameterAttachment xAttach, yAttach;
    float x = 0.0f, y = 1.0f;
    juce::StringArray labels;
};

class EnvelopeView : public juce::Component
{
public:
    explicit EnvelopeView (PVRVoiceMorphProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    PVRVoiceMorphProcessor& proc;
};

class PVRVoiceMorphEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PVRVoiceMorphEditor (PVRVoiceMorphProcessor&);
    ~PVRVoiceMorphEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showSlotMenu (int slot);
    void chooseSample (int slot);
    void openVoiceFile (int slot);
    void saveVoiceFile (int slot);
    void askForCode (int slot);
    void refreshSlots();
    void showMessage (const juce::String& title, const juce::String& text);

    PVRVoiceMorphProcessor& proc;
    PVRLookAndFeel lnf;

    XYPad pad;
    EnvelopeView envView;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    std::array<Knob, 5> knobs;

    std::array<juce::TextButton, PVRVoiceMorphProcessor::numSlots> slotButtons;
    juce::ComboBox qualityBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttach;
    juce::Label latencyLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    int lastSlotsVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PVRVoiceMorphEditor)
};
