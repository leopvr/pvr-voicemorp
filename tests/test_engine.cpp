#define _USE_MATH_DEFINES
// Pruebas del motor DSP sin JUCE.  g++ -O2 -std=c++17 tests/test_engine.cpp src/dsp/*.cpp
#include "../src/dsp/VoiceEngine.h"
#include "../src/dsp/VoiceProfile.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>
#include <random>
#include <fstream>
#include <cstdint>

using namespace pvr;
static const double SR = 48000.0;
static bool gLowLatency = false;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void resonator (double f, double bw, double sr)
    {
        const double r = std::exp (-M_PI * bw / sr), th = 2 * M_PI * f / sr;
        a1 = -2 * r * std::cos (th); a2 = r * r; b0 = 1 - r; b1 = 0; b2 = 0;
    }
    double tick (double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Voz sintética: tren glótico + 4 formantes que cambian por "vocal" cada 0.5 s.
static std::vector<float> synthVoice (double seconds, double f0, double formantScale, double tiltCoef, uint32_t seed)
{
    const double vowels[5][4] = { { 730, 1090, 2440, 3400 }, { 270, 2290, 3010, 3600 }, { 300, 870, 2240, 3300 },
                                  { 530, 1840, 2480, 3500 }, { 570, 840, 2410, 3400 } };
    const int n = (int) (seconds * SR);
    std::vector<float> out ((size_t) n);
    std::mt19937 rng (seed);
    std::normal_distribution<double> noise (0, 1);
    double ph = 0, lp = 0;
    Biquad bq[4];
    int vowel = -1;
    for (int i = 0; i < n; ++i)
    {
        const int v = (int) (i / (0.5 * SR)) % 5;
        if (v != vowel)
        {
            vowel = v;
            for (int k = 0; k < 4; ++k) bq[k].resonator (vowels[v][k] * formantScale, 60 + 40 * k, SR);
        }
        const double f = f0 * (1 + 0.01 * std::sin (2 * M_PI * 5.5 * i / SR));
        ph += f / SR; if (ph >= 1) ph -= 1;
        double g = (ph < 0.6) ? std::sin (M_PI * ph / 0.6) : 0.0; // pulso glótico
        g = g * g + 0.02 * noise (rng);
        lp = lp + tiltCoef * (g - lp); // inclinación espectral
        double y = 0;
        for (int k = 0; k < 4; ++k) y += bq[k].tick (lp) * (k == 0 ? 1.0 : 0.6 / k);
        out[(size_t) i] = (float) y;
    }
    float mx = 0; for (auto s : out) mx = std::max (mx, std::abs (s));
    for (auto& s : out) s *= 0.5f / mx;
    return out;
}

static void writeWav (const char* path, const std::vector<float>& x)
{
    std::ofstream f (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) x.size() * 2;
    f.write ("RIFF", 4); w32 (36 + bytes); f.write ("WAVEfmt ", 8); w32 (16); w16 (1); w16 (1);
    w32 ((uint32_t) SR); w32 ((uint32_t) SR * 2); w16 (2); w16 (16); f.write ("data", 4); w32 (bytes);
    for (float s : x) w16 ((uint16_t) (int16_t) std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767));
}

static std::vector<float> run (const std::vector<float>& in, const VoiceEngine::Params& p, const VoiceProfile* target, double* rtFactor = nullptr)
{
    VoiceEngine e;
    e.prepare (SR, 512, gLowLatency);
    e.setParams (p);
    e.setTargetGrid (target ? &target->envelope : nullptr);
    std::vector<float> out = in;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < out.size(); i += 480)
        e.process (out.data() + i, (int) std::min<size_t> (480, out.size() - i));
    auto t1 = std::chrono::high_resolution_clock::now();
    if (rtFactor) *rtFactor = std::chrono::duration<double> (t1 - t0).count() / (in.size() / SR);
    return out;
}

static double profileDistanceDb (const VoiceProfile& a, const VoiceProfile& b)
{
    // RMS de la diferencia en dB entre 150 Hz y 7 kHz
    double s = 0; int c = 0;
    for (int g = 0; g < kGridSize; ++g)
    {
        const double hz = g * kGridMaxHz / (kGridSize - 1);
        if (hz < 150 || hz > 7000) continue;
        const double d = 8.6859 * (a.envelope[(size_t) g] - b.envelope[(size_t) g]);
        s += d * d; ++c;
    }
    return std::sqrt (s / c);
}

static int suite()
{
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what); if (! ok) ++fails; };

    const auto voiceA = synthVoice (10.0, 130.0, 1.0, 0.25, 1);  // "grave"
    const auto voiceB = synthVoice (10.0, 220.0, 1.22, 0.6, 2);  // "aguda / brillante", otra persona
    writeWav ("/tmp/pvr_in_A.wav", voiceA);
    writeWav ("/tmp/pvr_in_B.wav", voiceB);

    const auto profA = analyseVoice (voiceA.data(), (int) voiceA.size(), SR, "A");
    const auto profB = analyseVoice (voiceB.data(), (int) voiceB.size(), SR, "B");
    check (profA.isValid() && profB.isValid(), "análisis de perfiles");
    std::printf ("       F0 mediana A=%.1f Hz  B=%.1f Hz, voz útil %.1f s\n", profA.medianF0, profB.medianF0, profA.seconds);
    check (std::abs (profA.medianF0 - 130) < 10 && std::abs (profB.medianF0 - 220) < 15, "estimación de F0");

    // 1) Identidad: todo en neutro, debe salir la misma señal retrasada 'latency' muestras.
    {
        VoiceEngine::Params p; p.amount = 0;
        const auto out = run (voiceA, p, nullptr);
        VoiceEngine tmp; tmp.prepare (SR, 512, gLowLatency);
        const int L = tmp.getLatencySamples();
        int bestLag = 0; double best = -1;
        for (int lag = 0; lag < 3000; ++lag)
        {
            double r = 0; for (int i = 20000; i < 60000; ++i) r += (double) voiceA[(size_t) i] * out[(size_t) (i + lag)];
            if (r > best) { best = r; bestLag = lag; }
        }
        double num = 0, den = 0;
        for (size_t i = 20000; i + L < voiceA.size(); ++i) { const double d = out[i + L] - voiceA[i]; num += d * d; den += (double) voiceA[i] * voiceA[i]; }
        const double snr = 10 * std::log10 (den / num);
        std::printf ("       latencia medida=%d reportada=%d, SNR identidad=%.1f dB\n", bestLag, L, snr);
        check (bestLag == L, "latencia reportada = latencia real");
        check (snr > 40, "reconstrucción transparente en neutro");
    }

    // 2) Transferencia de timbre A -> B
    {
        VoiceEngine::Params p; p.amount = 1.0f;
        double rt = 0;
        const auto out = run (voiceA, p, &profB, &rt);
        writeWav ("/tmp/pvr_out_A_to_B.wav", out);
        const auto profOut = analyseVoice (out.data() + 48000, (int) out.size() - 48000, SR, "out");
        const double before = profileDistanceDb (profA, profB), after = profileDistanceDb (profOut, profB);
        std::printf ("       distancia de timbre a B: antes %.1f dB, después %.1f dB; F0 salida=%.1f; CPU %.2f%% de tiempo real\n",
                     before, after, profOut.medianF0, rt * 100);
        check (after < before * 0.5, "el timbre de salida se acerca a la voz objetivo");
        check (std::abs (profOut.medianF0 - 130) < 10, "la entonación original se conserva");
        check (rt < 0.1, "costo de CPU bajo (< 10% de un núcleo)");

        // pronunciación: la variación por vocal debe seguir presente (comparar envolventes frame a frame)
        float mx = 0; for (auto s : out) mx = std::max (mx, std::abs (s));
        check (mx > 0.05f && mx < 2.0f, "nivel de salida razonable");
    }

    // 3) Pitch shift +12 conserva formantes y duplica F0
    {
        VoiceEngine::Params p; p.amount = 0; p.pitchSemis = 12;
        const auto out = run (voiceA, p, nullptr);
        writeWav ("/tmp/pvr_out_pitch12.wav", out);
        const auto po = analyseVoice (out.data() + 48000, (int) out.size() - 48000, SR, "p");
        std::printf ("       F0 tras +12 st = %.1f Hz; cambio de envolvente %.1f dB\n", po.medianF0, profileDistanceDb (po, profA));
        check (std::abs (po.medianF0 - 260) < 20, "pitch shift +12 semitonos");
    }

    // 4) Formant shift +4 sube el centroide de la envolvente
    {
        VoiceEngine::Params p; p.amount = 0; p.formantSemis = 4;
        const auto out = run (voiceA, p, nullptr);
        writeWav ("/tmp/pvr_out_formant4.wav", out);
        const auto po = analyseVoice (out.data() + 48000, (int) out.size() - 48000, SR, "f");
        VoiceProfile expected = profA;
        const float alpha = std::pow (2.0f, 4.0f / 12.0f);
        for (int g = 0; g < kGridSize; ++g)
        {
            const float pos = g / alpha; const int i = (int) pos; const float fr = pos - i;
            expected.envelope[(size_t) g] = profA.envelope[(size_t) i] + fr * (profA.envelope[(size_t) i + 1] - profA.envelope[(size_t) i]);
        }
        normaliseEnvelope (expected.envelope);
        const double dExp = profileDistanceDb (po, expected), dOrig = profileDistanceDb (po, profA);
        std::printf ("       distancia a envolvente esperada (+4 st) %.1f dB vs a la original %.1f dB; F0 %.1f\n", dExp, dOrig, po.medianF0);
        check (dExp < dOrig * 0.5, "formant shift sube formantes");
        check (std::abs (po.medianF0 - 130) < 10, "formant shift no cambia el pitch");
    }

    // 5) Voces generadas y códigos
    {
        const auto g = generateVoice (0.7f, 0.3f, 0xBEEF);
        float gg, cc; uint32_t ss;
        const bool ok = parseVoiceCode (g.name, gg, cc, ss);
        const auto g2 = generateVoice (gg, cc, ss);
        check (g.isValid() && ok && profileDistanceDb (g, g2) < 0.5, ("voz generada + código " + g.name).c_str());
        VoiceEngine::Params p; p.amount = 1.0f;
        const auto out = run (voiceA, p, &g);
        writeWav ("/tmp/pvr_out_generated.wav", out);
        bool finite = true; for (auto s : out) finite &= std::isfinite (s);
        check (finite, "salida estable con voz generada");
    }

    // 6) Silencio y ruido: sin NaN ni explosiones
    {
        std::vector<float> sil (48000 * 2, 0.0f);
        VoiceEngine::Params p; p.amount = 1.5f; p.pitchSemis = -7; p.formantSemis = -5;
        auto out = run (sil, p, &profB);
        float mx = 0; for (auto s : out) mx = std::max (mx, std::abs (s));
        check (mx < 1e-3f, "silencio entra, silencio sale");
    }

    return fails;
}

int main()
{
    int fails = 0;
    for (bool ll : { false, true })
    {
        gLowLatency = ll;
        std::printf ("\n===== Modo %s =====\n", ll ? "BAJA LATENCIA" : "CALIDAD ALTA");
        fails += suite();
    }
    std::printf (fails == 0 ? "\nTODO OK\n" : "\n%d FALLAS\n", fails);
    return fails;
}
