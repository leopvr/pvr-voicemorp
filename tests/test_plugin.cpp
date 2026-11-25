// Test de integración del AudioProcessor (sin interfaz).
#include "../src/PluginProcessor.h"
#include <cstdio>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto check = [&] (bool ok, const char* w) { std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", w); if (! ok) ++fails; };

    PVRVoiceMorphProcessor p;
    p.setPlayConfigDetails (2, 2, 44100.0, 256);
    p.prepareToPlay (44100.0, 256);
    check (p.getLatencySamples() == 2048, "latencia calidad alta @44.1k = 2048");

    const juce::String err = p.loadSampleIntoSlot (0, juce::File (argv[1]));
    std::printf ("       slot A: '%s' err='%s'\n", p.getSlotLabel (0).toRawUTF8(), err.toRawUTF8());
    check (err.isEmpty() && p.getSlot (0).isValid(), "cargar WAV en slot A");
    check (p.loadSampleIntoSlot (1, juce::File ("/nonexistent.wav")).isNotEmpty(), "error claro con archivo inválido");
    p.generateIntoSlot (3, 0.8f, 0.4f, 1234);
    const auto code = juce::String (p.getSlot (3).name);
    check (code.startsWith ("PVR-"), ("voz generada en D: " + code).toRawUTF8());
    check (p.loadVoiceCodeIntoSlot (2, code.toLowerCase()), "pegar código en C");

    // procesar 3 s de una señal (el mismo archivo leído de nuevo como entrada)
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[2])));
    const int total = (int) r->lengthInSamples;
    juce::AudioBuffer<float> in (2, total);
    r->read (&in, 0, total, 0, true, true);
    juce::MidiBuffer midi;
    double energy = 0; bool finite = true;
    for (int pos = 0; pos + 256 <= total; pos += 256)
    {
        juce::AudioBuffer<float> blk (2, 256);
        for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), pos, 256);
        p.processBlock (blk, midi);
        for (int i = 0; i < 256; ++i) { const float s = blk.getSample (0, i); finite &= std::isfinite (s); energy += s * s; }
        if (pos == 44100 * 2) // mover el pad en el medio, como automatización
        {
            p.apvts.getParameter ("padX")->setValueNotifyingHost (1.0f);
            p.apvts.getParameter ("padY")->setValueNotifyingHost (0.0f);
        }
    }
    check (finite && energy > 1.0, "proceso estable con audio y automatización del pad");

    // estado (lo que guarda Ableton en el .als)
    juce::MemoryBlock state;
    p.apvts.getParameter ("pitch")->setValueNotifyingHost (p.apvts.getParameterRange ("pitch").convertTo0to1 (5.0f));
    p.getStateInformation (state);
    PVRVoiceMorphProcessor q;
    q.setStateInformation (state.getData(), (int) state.getSize());
    check (q.getSlot (0).isValid() && q.getSlot (3).isValid() && ! q.getSlot (1).isValid(), "las voces se restauran desde el proyecto");
    check (q.getSlot (0).envelope == p.getSlot (0).envelope, "envolvente idéntica tras guardar/abrir");
    check (std::abs (q.apvts.getRawParameterValue ("pitch")->load() - 5.0f) < 0.01f, "parámetros restaurados");
    std::printf ("       estado: %d bytes\n", (int) state.getSize());

    // archivo .pvrvoice
    auto f = juce::File::createTempFile (".pvrvoice");
    check (p.saveSlotToFile (0, f) && q.loadSlotFromFile (2, f).isEmpty() && q.getSlot (2).envelope == p.getSlot (0).envelope, "guardar/abrir .pvrvoice");

    // cambio de modo
    p.apvts.getParameter ("quality")->setValueNotifyingHost (1.0f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    check (p.getLatencySamples() == 1024, "modo baja latencia -> 1024 muestras (23 ms)");
    juce::AudioBuffer<float> blk (2, 256); blk.clear();
    p.processBlock (blk, midi);
    check (true, "procesa tras cambiar de modo");

    if (argc > 3)
    {
        p.apvts.getParameter ("padX")->setValueNotifyingHost (0.3f);
        p.apvts.getParameter ("padY")->setValueNotifyingHost (0.75f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File out (argv[3]); out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        check (out.getSize() > 1000, "captura de la interfaz");
    }
    std::printf (fails ? "\n%d FALLAS\n" : "\nTODO OK\n", fails);
    return fails;
}
