// PVR VoiceMorph - análisis y generación de perfiles de voz.
#include "VoiceProfile.h"
#include "VoiceEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace pvr
{
void normaliseEnvelope (std::vector<float>& g)
{
    if (g.empty()) return;
    const int lo = (int) std::ceil (100.0f / kGridMaxHz * (kGridSize - 1));
    const int hi = (int) std::floor (8000.0f / kGridMaxHz * (kGridSize - 1));
    double sum = 0.0; int cnt = 0;
    for (int i = lo; i <= hi && i < (int) g.size(); ++i) { sum += g[(size_t) i]; ++cnt; }
    const float m = cnt > 0 ? (float) (sum / cnt) : 0.0f;
    for (auto& v : g) v -= m;
}

static float estimateF0 (const float* x, int n, double sr)
{
    const int minLag = (int) (sr / 1000.0), maxLag = std::min (n / 2, (int) (sr / 60.0));
    double r0 = 0.0;
    for (int i = 0; i < n; ++i) r0 += (double) x[i] * x[i];
    if (r0 <= 0.0) return 0.0f;
    double best = 0.0; int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double r = 0.0;
        for (int i = 0; i + lag < n; ++i) r += (double) x[i] * x[i + lag];
        r /= r0;
        if (r > best) { best = r; bestLag = lag; }
    }
    return (best > 0.5 && bestLag > 0) ? (float) (sr / bestLag) : 0.0f;
}

VoiceProfile analyseVoice (const float* samples, int numSamples, double sampleRate, const std::string& name)
{
    VoiceProfile p;
    p.name = name;
    const int N = chooseFftSize (sampleRate), hop = N / 4, half = N / 2;
    if (samples == nullptr || numSamples < N * 2) return p;

    EnvelopeEstimator est;
    est.prepare (N, sampleRate);
    SimpleFFT fft;
    fft.prepare (N);

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N);

    const int numFrames = (numSamples - N) / hop + 1;
    std::vector<float> rms ((size_t) numFrames);
    for (int f = 0; f < numFrames; ++f)
    {
        double e = 0.0;
        for (int i = 0; i < N; ++i) { const float s = samples[f * hop + i]; e += (double) s * s; }
        rms[(size_t) f] = (float) std::sqrt (e / N);
    }
    const float maxRms = *std::max_element (rms.begin(), rms.end());
    const float thresh = std::max (1.0e-4f, maxRms * 0.0316f); // -30 dB del pico

    std::vector<std::complex<float>> buf ((size_t) N);
    std::vector<float> logMag ((size_t) half + 1), env ((size_t) half + 1);
    std::vector<double> acc ((size_t) half + 1, 0.0);
    std::vector<float> f0s;
    int used = 0;

    for (int f = 0; f < numFrames; ++f)
    {
        if (rms[(size_t) f] < thresh) continue;
        const float* x = samples + f * hop;
        for (int i = 0; i < N; ++i) buf[(size_t) i] = { x[i] * win[(size_t) i], 0.0f };
        fft.transform (buf.data(), false);
        for (int k = 0; k <= half; ++k) logMag[(size_t) k] = std::log (std::abs (buf[(size_t) k]) + 1.0e-9f);
        est.compute (logMag.data(), env.data());
        for (int k = 0; k <= half; ++k) acc[(size_t) k] += env[(size_t) k];
        ++used;
        if ((f % 4) == 0)
            if (const float f0 = estimateF0 (x, N, sampleRate); f0 > 0.0f) f0s.push_back (f0);
    }
    if (used < 8) return p; // muy poca voz útil

    std::vector<float> meanEnv ((size_t) half + 1);
    for (int k = 0; k <= half; ++k) meanEnv[(size_t) k] = (float) (acc[(size_t) k] / used);

    p.envelope.resize ((size_t) kGridSize);
    for (int g = 0; g < kGridSize; ++g)
    {
        const float hz = (float) g / (float) (kGridSize - 1) * kGridMaxHz;
        const float pos = hz * (float) N / (float) sampleRate;
        const int i = (int) pos;
        if (i >= half) { p.envelope[(size_t) g] = meanEnv[(size_t) half]; continue; }
        const float fr = pos - (float) i;
        p.envelope[(size_t) g] = meanEnv[(size_t) i] + fr * (meanEnv[(size_t) i + 1] - meanEnv[(size_t) i]);
    }
    normaliseEnvelope (p.envelope);
    p.seconds = (float) used * (float) hop / (float) sampleRate;
    if (! f0s.empty())
    {
        std::nth_element (f0s.begin(), f0s.begin() + (long) f0s.size() / 2, f0s.end());
        p.medianF0 = f0s[f0s.size() / 2];
    }
    return p;
}

VoiceProfile generateVoice (float gender, float colour, uint32_t seed)
{
    gender = std::clamp (gender, 0.0f, 1.0f);
    colour = std::clamp (colour, 0.0f, 1.0f);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> jitter (-1.0f, 1.0f);

    // Formantes de una vocal neutra, escalados por "tamaño del tracto vocal".
    const float baseF[5] = { 500.0f, 1500.0f, 2500.0f, 3500.0f, 4500.0f };
    const float baseB[5] = { 80.0f, 100.0f, 140.0f, 200.0f, 260.0f };
    const float baseA[5] = { 0.0f, -6.0f, -14.0f, -20.0f, -24.0f };
    const float scale = 0.84f + 0.44f * gender;

    // El color mueve: inclinación espectral (oscuro/brillante), "formante del cantante" y aire.
    const float tilt = 13.0f - 8.0f * colour;                          // dB/oct después de 300 Hz
    const float singer = 10.0f * std::sin (colour * 3.14159f);        // realce 2.5-3.5 kHz
    const float air = -38.0f + 14.0f * std::abs (std::sin (colour * 9.0f)); // ruido arriba de 6 kHz

    float F[5], B[5], A[5];
    for (int i = 0; i < 5; ++i)
    {
        F[i] = baseF[i] * scale * (1.0f + 0.07f * jitter (rng));
        B[i] = baseB[i] * (1.0f + 0.3f * jitter (rng));
        A[i] = baseA[i] + 3.0f * jitter (rng) + ((i == 2 || i == 3) ? singer : 0.0f);
    }

    VoiceProfile p;
    p.name = makeVoiceCode (gender, colour, seed);
    p.envelope.resize ((size_t) kGridSize);
    for (int g = 0; g < kGridSize; ++g)
    {
        const float hz = std::max (20.0f, (float) g / (float) (kGridSize - 1) * kGridMaxHz);
        double power = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            const float d = (hz - F[i]) / (B[i] * 0.5f);
            power += std::pow (10.0, A[i] / 10.0) / (1.0 + d * d);
        }
        const float hfNoise = air + (hz > 6000.0f ? 0.0f : -20.0f * (6000.0f - hz) / 6000.0f);
        power += std::pow (10.0, hfNoise / 10.0);
        float db = 10.0f * (float) std::log10 (power);
        if (hz > 300.0f) db -= tilt * std::log2 (hz / 300.0f) * 0.35f;
        p.envelope[(size_t) g] = db / 8.685889f; // dB -> log natural
    }
    normaliseEnvelope (p.envelope);
    p.seconds = 0.0f;
    p.medianF0 = 110.0f + 110.0f * gender;
    return p;
}

std::string makeVoiceCode (float gender, float colour, uint32_t seed)
{
    char buf[32];
    std::snprintf (buf, sizeof (buf), "PVR-%04X-%02X-%02X",
                   (unsigned) (seed & 0xFFFFu),
                   (unsigned) std::lround (std::clamp (gender, 0.0f, 1.0f) * 255.0f),
                   (unsigned) std::lround (std::clamp (colour, 0.0f, 1.0f) * 255.0f));
    return buf;
}

bool parseVoiceCode (const std::string& code, float& gender, float& colour, uint32_t& seed)
{
    unsigned s = 0, g = 0, c = 0;
    if (std::sscanf (code.c_str(), "PVR-%4X-%2X-%2X", &s, &g, &c) != 3) return false;
    seed = s;
    gender = (float) g / 255.0f;
    colour = (float) c / 255.0f;
    return true;
}
} // namespace pvr
