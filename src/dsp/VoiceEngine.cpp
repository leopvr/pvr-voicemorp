// PVR VoiceMorph - implementación del motor.
#include "VoiceEngine.h"
#include "VoiceProfile.h"
#include <algorithm>
#include <cmath>

namespace pvr
{
static constexpr float kTwoPi = 6.283185307179586f;
static constexpr float kMaxCorrection = 2.76f; // ~24 dB en log natural

static inline float wrapPhase (float p) noexcept
{
    p = std::fmod (p + (float) kPi, kTwoPi);
    if (p < 0.0f) p += kTwoPi;
    return p - (float) kPi;
}

int chooseFftSize (double sampleRate, bool lowLatency)
{
    // Calidad alta: ~43 ms de ventana. Baja latencia: ~21 ms.
    int base = lowLatency ? 1024 : 2048;
    if (sampleRate > 50000.0)  base *= 2;
    if (sampleRate > 100000.0) base *= 2;
    return base;
}

//==============================================================================
void EnvelopeEstimator::prepare (int fftSize, double sampleRate)
{
    n = fftSize;
    fft.prepare (n);
    buf.assign ((size_t) n, {});
    work.assign ((size_t) n / 2 + 1, 0.0f);
    // Corte del cepstrum ~1.1 ms: separa envolvente (formantes) de armónicos.
    lifter = std::clamp ((int) std::lround (sampleRate * 0.0011), 12, n / 4);
}

void EnvelopeEstimator::compute (const float* logMag, float* envOut)
{
    const int half = n / 2;
    for (int k = 0; k <= half; ++k) work[(size_t) k] = logMag[k];

    // "True envelope" (Röbel): iterar el suavizado cepstral sobre max(espectro, envolvente)
    // para que la envolvente toque los picos armónicos en vez de pasar por el medio.
    constexpr int iterations = 4;
    for (int it = 0; it < iterations; ++it)
    {
        for (int k = 0; k <= half; ++k) buf[(size_t) k] = { work[(size_t) k], 0.0f };
        for (int k = 1; k < half; ++k)  buf[(size_t) (n - k)] = buf[(size_t) k];

        fft.transform (buf.data(), true); // cepstrum real

        for (int q = lifter + 1; q < n - lifter; ++q) buf[(size_t) q] = {};
        // pequeño taper en el borde del lifter
        for (int q = std::max (1, lifter - 4); q <= lifter; ++q)
        {
            const float t = 0.5f + 0.5f * std::cos ((float) kPi * (float) (q - (lifter - 4)) / 5.0f);
            buf[(size_t) q] *= t;
            buf[(size_t) (n - q)] *= t;
        }

        fft.transform (buf.data(), false);

        for (int k = 0; k <= half; ++k)
        {
            const float e = buf[(size_t) k].real();
            envOut[k] = e;
            if (it + 1 < iterations)
                work[(size_t) k] = std::max (logMag[k], e);
        }
    }
}

//==============================================================================
void VoiceEngine::prepare (double sampleRate, int /*maxBlockSize*/, bool lowLatencyMode)
{
    sr = sampleRate;
    lowLatency = lowLatencyMode;
    fftSize = chooseFftSize (sr, lowLatency);
    hop = fftSize / 4;
    numBins = fftSize / 2 + 1;

    fft.prepare (fftSize);
    envEst.prepare (fftSize, sr);

    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) fftSize);

    auto bins = [this] (std::vector<float>& v) { v.assign ((size_t) numBins, 0.0f); };
    for (auto* v : { &logMag, &phase, &prevPhase, &outPhase, &trueFreq, &env, &fine,
                     &fineOut, &envWarp, &inputLtas, &ltasWarp, &correction, &outLog, &peakPhase, &newPhase })
        bins (*v);
    if (target.size() != (size_t) numBins) { target.assign ((size_t) numBins, 0.0f); hasTarget = false; }

    peakOf.assign ((size_t) numBins, 0);
    peakList.clear();
    peakList.reserve ((size_t) numBins);
    spec.assign ((size_t) fftSize, {});
    bandLo = 100.0f * (float) fftSize / (float) sr;
    bandHi = std::min (8000.0f, (float) sr * 0.5f) * (float) fftSize / (float) sr;

    // Promedio largo de la entrada: constante de tiempo ~1.5 s
    ltasCoeff = (float) std::exp (-(double) hop / (1.5 * sr));

    reset();
}

void VoiceEngine::reset()
{
    inQueue.assign ((size_t) fftSize, 0.0f);
    outAccum.assign ((size_t) fftSize, 0.0f);
    outQueue.assign ((size_t) hop, 0.0f);
    std::fill (prevPhase.begin(), prevPhase.end(), 0.0f);
    std::fill (outPhase.begin(), outPhase.end(), 0.0f);
    std::fill (inputLtas.begin(), inputLtas.end(), 0.0f);
    ltasCount = 0.0;
    hopCount = 0;
    wasShifting = false;
    dryDelay.assign ((size_t) getLatencySamples(), 0.0f);
    dryPos = 0;
}

void VoiceEngine::setTargetGrid (const std::vector<float>* gridEnv)
{
    if (gridEnv == nullptr || (int) gridEnv->size() != kGridSize)
    {
        hasTarget = false;
        return;
    }
    for (int k = 0; k < numBins; ++k)
    {
        const float hz = (float) k * (float) sr / (float) fftSize;
        const float pos = hz / kGridMaxHz * (float) (kGridSize - 1);
        target[(size_t) k] = interp (*gridEnv, pos, gridEnv->back());
    }
    // normalizar en el dominio de bins
    double sum = 0.0; int cnt = 0;
    for (int k = (int) bandLo; k <= (int) bandHi && k < numBins; ++k) { sum += target[(size_t) k]; ++cnt; }
    const float m = cnt > 0 ? (float) (sum / cnt) : 0.0f;
    for (auto& t : target) t -= m;
    hasTarget = true;
}

float VoiceEngine::interp (const std::vector<float>& v, float pos, float fallback) noexcept
{
    if (pos < 0.0f) return v.front();
    const int i = (int) pos;
    if (i >= (int) v.size() - 1) return fallback;
    const float f = pos - (float) i;
    return v[(size_t) i] + f * (v[(size_t) i + 1] - v[(size_t) i]);
}

void VoiceEngine::process (float* io, int numSamples) noexcept
{
    const float mix = std::clamp (params.mix, 0.0f, 1.0f);
    const float gain = params.outputGain;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = io[i];

        inQueue[(size_t) (fftSize - hop + hopCount)] = x;
        float wet = outQueue[(size_t) hopCount];

        const float dry = dryDelay[(size_t) dryPos];
        dryDelay[(size_t) dryPos] = x;
        if (++dryPos >= (int) dryDelay.size()) dryPos = 0;

        if (! std::isfinite (wet)) wet = 0.0f;
        io[i] = (dry * (1.0f - mix) + wet * mix) * gain;

        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame();
            std::copy (outAccum.begin(), outAccum.begin() + hop, outQueue.begin());
            std::copy (outAccum.begin() + hop, outAccum.end(), outAccum.begin());
            std::fill (outAccum.end() - hop, outAccum.end(), 0.0f);
            std::copy (inQueue.begin() + hop, inQueue.end(), inQueue.begin());
        }
    }
}

void VoiceEngine::processFrame() noexcept
{
    const int N = fftSize, half = N / 2;

    double energy = 0.0;
    for (int i = 0; i < N; ++i)
    {
        const float s = inQueue[(size_t) i];
        energy += (double) s * s;
        // análisis de fase cero: rotar N/2 para que la ventana quede centrada en t=0
        spec[(size_t) ((i + N / 2) % N)] = { s * window[(size_t) i], 0.0f };
    }
    const bool voiced = (energy / N) > 1.0e-6; // > -60 dBFS RMS

    fft.transform (spec.data(), false);

    for (int k = 0; k <= half; ++k)
    {
        const auto c = spec[(size_t) k];
        logMag[(size_t) k] = std::log (std::abs (c) + 1.0e-9f);
        phase[(size_t) k] = std::arg (c);
    }

    envEst.compute (logMag.data(), env.data());

    // promedio largo de la envolvente de la entrada (solo frames con señal)
    if (voiced)
    {
        ltasCount += 1.0;
        const float a = std::min (ltasCoeff, (float) (1.0 - 1.0 / ltasCount));
        for (int k = 0; k <= half; ++k)
            inputLtas[(size_t) k] = a * inputLtas[(size_t) k] + (1.0f - a) * env[(size_t) k];
    }

    for (int k = 0; k <= half; ++k)
        fine[(size_t) k] = logMag[(size_t) k] - env[(size_t) k];

    // frecuencia instantánea por bin (rad/sample)
    for (int k = 0; k <= half; ++k)
    {
        const float expected = kTwoPi * (float) k * (float) hop / (float) N;
        const float d = wrapPhase (phase[(size_t) k] - prevPhase[(size_t) k] - expected);
        trueFreq[(size_t) k] = kTwoPi * (float) k / (float) N + d / (float) hop;
        prevPhase[(size_t) k] = phase[(size_t) k];
    }

    // --- pitch shift (sobre la excitación; los formantes quedan en su lugar)
    const float beta = std::pow (2.0f, std::clamp (params.pitchSemis, -24.0f, 24.0f) / 12.0f);
    const bool shifting = std::abs (beta - 1.0f) > 1.0e-4f;
    if (shifting)
    {
        if (! wasShifting) outPhase = phase;
        if (lowLatency)
        {
            // phase vocoder simple (ventana corta: los armónicos graves no se resuelven
            // bien como para hacer phase locking)
            for (int k = 0; k <= half; ++k)
            {
                const float s = (float) k / beta;
                if (s >= (float) half) { fineOut[(size_t) k] = -30.0f; continue; }
                fineOut[(size_t) k] = interp (fine, s, -30.0f);
                const int nearest = std::min (half, (int) std::lround (s));
                outPhase[(size_t) k] = wrapPhase (outPhase[(size_t) k] + (float) hop * trueFreq[(size_t) nearest] * beta);
            }
        }
        else
        {

            // Identity phase locking (Laroche-Dolson): cada bin sigue la fase del pico
            // armónico de su región -> mucho menos sonido "metálico/phasey".
            std::vector<int>& peaks = peakList; // capacidad reservada en prepare()
            peaks.clear();
            for (int k = 2; k < half - 2; ++k)
            {
                const float m = logMag[(size_t) k];
                if (m > logMag[(size_t) k - 1] && m >= logMag[(size_t) k + 1]
                    && m > logMag[(size_t) k - 2] && m >= logMag[(size_t) k + 2])
                    peaks.push_back (k);
            }
            if (peaks.empty()) peaks.push_back (1);
            for (size_t p = 0, k = 0; k <= (size_t) half; ++k)
            {
                while (p + 1 < peaks.size() && (int) k > (peaks[p] + peaks[p + 1]) / 2) ++p;
                peakOf[k] = peaks[p];
            }

            // fase sintetizada de cada pico
            for (int pk : peaks)
            {
                const int kp = std::min (half, (int) std::lround ((float) pk * beta));
                peakPhase[(size_t) pk] = wrapPhase (outPhase[(size_t) kp] + (float) hop * trueFreq[(size_t) pk] * beta);
            }

            for (int k = 0; k <= half; ++k)
            {
                const float s = (float) k / beta;
                if (s >= (float) half)
                {
                    fineOut[(size_t) k] = -30.0f;
                    newPhase[(size_t) k] = outPhase[(size_t) k];
                    continue;
                }
                fineOut[(size_t) k] = interp (fine, s, -30.0f);
                const int sn = std::min (half, (int) std::lround (s));
                const int pk = peakOf[(size_t) sn];
                newPhase[(size_t) k] = wrapPhase (peakPhase[(size_t) pk] + phase[(size_t) sn] - phase[(size_t) pk]);
            }
            outPhase.swap (newPhase);
        }
    }
    else
    {
        fineOut = fine;
        outPhase = phase;
    }
    wasShifting = shifting;

    // --- transferencia de timbre: envolvente corregida = E + amount * (Objetivo - LTAS)
    const float amount = hasTarget ? std::clamp (params.amount, 0.0f, 1.5f) : 0.0f;
    if (amount > 0.0f && ltasCount > 0.0)
    {
        double sum = 0.0; int cnt = 0;
        for (int k = (int) bandLo; k <= (int) bandHi && k <= half; ++k) { sum += inputLtas[(size_t) k]; ++cnt; }
        const float m = cnt > 0 ? (float) (sum / cnt) : 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const float c = amount * (target[(size_t) k] - (inputLtas[(size_t) k] - m));
            correction[(size_t) k] = std::clamp (c, -kMaxCorrection, kMaxCorrection);
            ltasWarp[(size_t) k] = env[(size_t) k] + correction[(size_t) k]; // envolvente corregida
        }
    }
    else
    {
        ltasWarp = env;
    }

    // --- formant shift: estirar la envolvente corregida
    const float alpha = std::pow (2.0f, std::clamp (params.formantSemis, -24.0f, 24.0f) / 12.0f);
    if (std::abs (alpha - 1.0f) > 1.0e-4f)
    {
        const float last = ltasWarp[(size_t) half];
        for (int k = 0; k <= half; ++k)
            envWarp[(size_t) k] = interp (ltasWarp, (float) k / alpha, last);
    }
    else
    {
        envWarp = ltasWarp;
    }

    // --- resíntesis
    for (int k = 0; k <= half; ++k)
    {
        const float lm = std::min (fineOut[(size_t) k] + envWarp[(size_t) k], 12.0f);
        spec[(size_t) k] = std::polar (std::exp (lm), outPhase[(size_t) k]);
    }
    spec[0] = { spec[0].real(), 0.0f };
    spec[(size_t) half] = { spec[(size_t) half].real(), 0.0f };
    for (int k = 1; k < half; ++k) spec[(size_t) (N - k)] = std::conj (spec[(size_t) k]);

    fft.transform (spec.data(), true);

    constexpr float olaNorm = 1.0f / 1.5f; // Hann² con 75% de solapamiento
    for (int i = 0; i < N; ++i)
        outAccum[(size_t) i] += spec[(size_t) ((i + N / 2) % N)].real() * window[(size_t) i] * olaNorm;
}
} // namespace pvr
