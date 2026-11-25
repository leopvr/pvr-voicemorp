// PVR VoiceMorph - procesador del plugin.
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace IDs
{
    static const juce::String amount  { "amount" };
    static const juce::String formant { "formant" };
    static const juce::String pitch   { "pitch" };
    static const juce::String mix     { "mix" };
    static const juce::String output  { "output" };
    static const juce::String padX    { "padX" };
    static const juce::String padY    { "padY" };
    static const juce::String quality { "quality" };
}

juce::AudioProcessorValueTreeState::ParameterLayout PVRVoiceMorphProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    auto st  = AudioParameterFloatAttributes().withLabel ("st");
    auto db  = AudioParameterFloatAttributes().withLabel ("dB");

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::amount, 1 },  "Timbre",    NormalisableRange<float> (0.0f, 150.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::formant, 1 }, "Formantes", NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, st));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::pitch, 1 },   "Pitch",     NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f, st));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::mix, 1 },     "Mezcla",    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::output, 1 },  "Salida",    NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, db));
    // Pad X/Y: por defecto en la esquina A (arriba a la izquierda)
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::padX, 1 },    "Pad X",     NormalisableRange<float> (0.0f, 1.0f), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { IDs::padY, 1 },    "Pad Y",     NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { IDs::quality, 1 }, "Modo",
                                                  StringArray { "Calidad alta", "Baja latencia" }, 0));
    return l;
}

PVRVoiceMorphProcessor::PVRVoiceMorphProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PVRVoiceMorph", createLayout())
{
    formatManager.registerBasicFormats();
    pAmount  = apvts.getRawParameterValue (IDs::amount);
    pFormant = apvts.getRawParameterValue (IDs::formant);
    pPitch   = apvts.getRawParameterValue (IDs::pitch);
    pMix     = apvts.getRawParameterValue (IDs::mix);
    pOutput  = apvts.getRawParameterValue (IDs::output);
    pX       = apvts.getRawParameterValue (IDs::padX);
    pY       = apvts.getRawParameterValue (IDs::padY);
    pQuality = apvts.getRawParameterValue (IDs::quality);
    apvts.addParameterListener (IDs::quality, this);
    blendBuffer.assign ((size_t) pvr::kGridSize, 0.0f);
    engine.prepare (currentSampleRate, currentBlockSize, false);
    setLatencySamples (engine.getLatencySamples());
}

PVRVoiceMorphProcessor::~PVRVoiceMorphProcessor()
{
    apvts.removeParameterListener (IDs::quality, this);
    cancelPendingUpdate();
}

bool PVRVoiceMorphProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in  != juce::AudioChannelSet::mono() && in  != juce::AudioChannelSet::stereo()) return false;
    return in.size() <= out.size();
}

double PVRVoiceMorphProcessor::getTailLengthSeconds() const
{
    return (double) engine.getLatencySamples() / currentSampleRate;
}

void PVRVoiceMorphProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, samplesPerBlock);
    engine.prepare (sampleRate, currentBlockSize, pQuality->load() > 0.5f);
    setLatencySamples (engine.getLatencySamples());
    monoBuffer.setSize (1, currentBlockSize, false, true, false);
    lastBlendVersion = -1;
}

void PVRVoiceMorphProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == IDs::quality) triggerAsyncUpdate();
}

void PVRVoiceMorphProcessor::handleAsyncUpdate()
{
    const bool wantLow = pQuality->load() > 0.5f;
    if (wantLow == engine.isLowLatency()) return;
    suspendProcessing (true);
    engine.prepare (currentSampleRate, currentBlockSize, wantLow);
    lastBlendVersion = -1;
    suspendProcessing (false);
    setLatencySamples (engine.getLatencySamples());
}

//==============================================================================
void PVRVoiceMorphProcessor::computeBlend (float x, float y, std::vector<float>& out, float& weight) const
{
    // Esquinas: A arriba-izq, B arriba-der, C abajo-izq, D abajo-der. Un slot vacío
    // equivale a "tu propia voz": reduce la cantidad de efecto en vez de agregar timbre.
    const float w[numSlots] = { (1.0f - x) * y, x * y, (1.0f - x) * (1.0f - y), x * (1.0f - y) };
    std::fill (out.begin(), out.end(), 0.0f);
    float total = 0.0f;
    for (int s = 0; s < numSlots; ++s)
    {
        if (! slots[(size_t) s].isValid() || w[s] <= 0.0f) continue;
        const auto& e = slots[(size_t) s].envelope;
        for (int g = 0; g < pvr::kGridSize; ++g) out[(size_t) g] += w[s] * e[(size_t) g];
        total += w[s];
    }
    if (total > 1.0e-6f)
        for (auto& v : out) v /= total;
    weight = total;
}

void PVRVoiceMorphProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Actualizar la mezcla de voces si cambió el pad o algún slot
    {
        const juce::SpinLock::ScopedTryLockType tl (slotLock);
        if (tl.isLocked())
        {
            const float x = pX->load(), y = pY->load();
            const int ver = slotsVersion.load();
            if (ver != lastBlendVersion || std::abs (x - lastX) > 1.0e-4f || std::abs (y - lastY) > 1.0e-4f)
            {
                computeBlend (x, y, blendBuffer, lastWeight);
                engine.setTargetGrid (lastWeight > 1.0e-4f ? &blendBuffer : nullptr);
                lastBlendVersion = ver; lastX = x; lastY = y;
            }
        }
    }

    pvr::VoiceEngine::Params p;
    p.amount       = pAmount->load() * 0.01f * lastWeight;
    p.formantSemis = pFormant->load();
    p.pitchSemis   = pPitch->load();
    p.mix          = pMix->load() * 0.01f;
    p.outputGain   = juce::Decibels::decibelsToGain (pOutput->load());
    engine.setParams (p);

    // La voz se procesa en mono (suma de entradas) y se copia a todas las salidas.
    float* mono = monoBuffer.getWritePointer (0);
    const int chunkMax = monoBuffer.getNumSamples();
    for (int start = 0; start < numSamples; start += chunkMax)
    {
        const int n = juce::jmin (chunkMax, numSamples - start);
        juce::FloatVectorOperations::clear (mono, n);
        if (numIn > 0)
        {
            const float g = 1.0f / (float) numIn;
            for (int ch = 0; ch < numIn; ++ch)
                juce::FloatVectorOperations::addWithMultiply (mono, buffer.getReadPointer (ch, start), g, n);
        }
        engine.process (mono, n);
        for (int ch = 0; ch < numOut; ++ch)
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch, start), mono, n);
    }
}

//==============================================================================
void PVRVoiceMorphProcessor::setSlot (int slot, pvr::VoiceProfile p)
{
    if (slot < 0 || slot >= numSlots) return;
    {
        const juce::SpinLock::ScopedLockType sl (slotLock);
        slots[(size_t) slot] = std::move (p);
    }
    ++slotsVersion;
}

pvr::VoiceProfile PVRVoiceMorphProcessor::getSlot (int slot) const
{
    const juce::SpinLock::ScopedLockType sl (slotLock);
    return slots[(size_t) juce::jlimit (0, numSlots - 1, slot)];
}

juce::String PVRVoiceMorphProcessor::getSlotLabel (int slot) const
{
    const auto p = getSlot (slot);
    if (! p.isValid()) return u8 ("(vacío = tu voz)");
    return juce::String::fromUTF8 (p.name.c_str());
}

std::vector<float> PVRVoiceMorphProcessor::getBlendedTargetForDisplay (float& effectiveWeight) const
{
    std::vector<float> out ((size_t) pvr::kGridSize, 0.0f);
    const juce::SpinLock::ScopedLockType sl (slotLock);
    computeBlend (pX->load(), pY->load(), out, effectiveWeight);
    return out;
}

juce::String PVRVoiceMorphProcessor::loadSampleIntoSlot (int slot, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return u8 ("No pude abrir el archivo. Usá WAV, AIFF, FLAC u OGG.");

    const auto maxLen = (juce::int64) (reader->sampleRate * 60.0); // hasta 60 s
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
    const int chans = (int) juce::jmax (1u, reader->numChannels);
    juce::AudioBuffer<float> buf (chans, len);
    reader->read (&buf, 0, len, 0, true, true);

    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < chans; ++ch)
        juce::FloatVectorOperations::addWithMultiply (mono.data(), buf.getReadPointer (ch), 1.0f / (float) chans, len);

    auto profile = pvr::analyseVoice (mono.data(), len, reader->sampleRate,
                                      file.getFileNameWithoutExtension().toStdString());
    if (! profile.isValid())
        return u8 ("No encontré suficiente voz clara en el archivo. Usá al menos 5-10 segundos de voz seca, sin música de fondo.");

    setSlot (slot, std::move (profile));
    return {};
}

void PVRVoiceMorphProcessor::generateIntoSlot (int slot, float gender, float colour, uint32_t seed)
{
    setSlot (slot, pvr::generateVoice (gender, colour, seed));
}

bool PVRVoiceMorphProcessor::loadVoiceCodeIntoSlot (int slot, const juce::String& code)
{
    float g = 0, c = 0; uint32_t s = 0;
    if (! pvr::parseVoiceCode (code.trim().toUpperCase().toStdString(), g, c, s)) return false;
    generateIntoSlot (slot, g, c, s);
    return true;
}

void PVRVoiceMorphProcessor::clearSlot (int slot)
{
    setSlot (slot, pvr::VoiceProfile {});
}

//==============================================================================
juce::ValueTree PVRVoiceMorphProcessor::profileToTree (const pvr::VoiceProfile& p, int slot)
{
    juce::ValueTree t ("VOICE");
    t.setProperty ("slot", slot, nullptr);
    t.setProperty ("name", juce::String::fromUTF8 (p.name.c_str()), nullptr);
    t.setProperty ("seconds", p.seconds, nullptr);
    t.setProperty ("f0", p.medianF0, nullptr);
    juce::MemoryBlock mb (p.envelope.data(), p.envelope.size() * sizeof (float));
    t.setProperty ("envelope", mb.toBase64Encoding(), nullptr);
    return t;
}

pvr::VoiceProfile PVRVoiceMorphProcessor::treeToProfile (const juce::ValueTree& t)
{
    pvr::VoiceProfile p;
    p.name = t.getProperty ("name").toString().toStdString();
    p.seconds = (float) t.getProperty ("seconds", 0.0f);
    p.medianF0 = (float) t.getProperty ("f0", 0.0f);
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (t.getProperty ("envelope").toString())
        && mb.getSize() == (size_t) pvr::kGridSize * sizeof (float))
    {
        p.envelope.resize ((size_t) pvr::kGridSize);
        std::memcpy (p.envelope.data(), mb.getData(), mb.getSize());
    }
    return p;
}

bool PVRVoiceMorphProcessor::saveSlotToFile (int slot, const juce::File& file) const
{
    const auto p = getSlot (slot);
    if (! p.isValid()) return false;
    auto tree = profileToTree (p, slot);
    tree.setProperty ("format", "pvrvoice-1", nullptr);
    if (auto xml = tree.createXml()) return xml->writeTo (file);
    return false;
}

juce::String PVRVoiceMorphProcessor::loadSlotFromFile (int slot, const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) return u8 ("El archivo no es una voz válida (.pvrvoice).");
    auto p = treeToProfile (juce::ValueTree::fromXml (*xml));
    if (! p.isValid()) return u8 ("El archivo no es una voz válida (.pvrvoice).");
    setSlot (slot, std::move (p));
    return {};
}

void PVRVoiceMorphProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree voices ("VOICES");
    for (int s = 0; s < numSlots; ++s)
    {
        const auto p = getSlot (s);
        if (p.isValid()) voices.appendChild (profileToTree (p, s), nullptr);
    }
    state.appendChild (voices, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PVRVoiceMorphProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (apvts.state.getType())) return;

    auto voices = tree.getChildWithName ("VOICES");
    if (voices.isValid()) tree.removeChild (voices, nullptr);
    apvts.replaceState (tree);

    for (int s = 0; s < numSlots; ++s) clearSlot (s);
    for (auto v : voices)
    {
        const int s = (int) v.getProperty ("slot", -1);
        if (s >= 0 && s < numSlots) setSlot (s, treeToProfile (v));
    }
}

juce::AudioProcessorEditor* PVRVoiceMorphProcessor::createEditor()
{
    return new PVRVoiceMorphEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PVRVoiceMorphProcessor();
}
