// PVR VoiceMorph - procesador del plugin.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/VoiceEngine.h"
#include "dsp/VoiceProfile.h"
#include <array>
#include <atomic>

inline juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

class PVRVoiceMorphProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::AsyncUpdater
{
public:
    static constexpr int numSlots = 4;

    PVRVoiceMorphProcessor();
    ~PVRVoiceMorphProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PVR VoiceMorph"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Slots de voz (llamar desde el hilo de mensajes)
    juce::String loadSampleIntoSlot (int slot, const juce::File& file);   // devuelve mensaje de error o ""
    void generateIntoSlot (int slot, float gender, float colour, uint32_t seed);
    bool loadVoiceCodeIntoSlot (int slot, const juce::String& code);
    bool saveSlotToFile (int slot, const juce::File& file) const;
    juce::String loadSlotFromFile (int slot, const juce::File& file);
    void clearSlot (int slot);

    pvr::VoiceProfile getSlot (int slot) const;
    juce::String getSlotLabel (int slot) const;   // nombre corto para la UI
    int getSlotsVersion() const noexcept { return slotsVersion.load(); }

    // Para el visor: envolvente objetivo mezclada actual (en grilla) y la ganancia efectiva.
    std::vector<float> getBlendedTargetForDisplay (float& effectiveWeight) const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void setSlot (int slot, pvr::VoiceProfile p);
    void computeBlend (float x, float y, std::vector<float>& out, float& weight) const;  // requiere lock
    static juce::ValueTree profileToTree (const pvr::VoiceProfile&, int slot);
    static pvr::VoiceProfile treeToProfile (const juce::ValueTree&);

    pvr::VoiceEngine engine;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;

    mutable juce::SpinLock slotLock;
    std::array<pvr::VoiceProfile, numSlots> slots;
    std::atomic<int> slotsVersion { 0 };

    // caché de la mezcla en el hilo de audio
    std::vector<float> blendBuffer;
    int lastBlendVersion = -1;
    float lastX = -1.0f, lastY = -1.0f, lastWeight = 0.0f;

    juce::AudioBuffer<float> monoBuffer;
    juce::AudioFormatManager formatManager;

    std::atomic<float>* pAmount = nullptr; std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pPitch  = nullptr; std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr; std::atomic<float>* pX = nullptr;
    std::atomic<float>* pY      = nullptr; std::atomic<float>* pQuality = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PVRVoiceMorphProcessor)
};
