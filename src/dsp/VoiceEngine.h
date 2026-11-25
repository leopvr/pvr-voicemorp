// PVR VoiceMorph - Motor de transformación de timbre en tiempo real (mono).
//
// Por cada frame STFT:
//   1. Envolvente espectral E (cepstrum iterativo, "true envelope").
//   2. Excitación = log|X| - E  (lo que lleva el pitch y la articulación).
//   3. Pitch shift sobre la excitación (phase vocoder, preserva formantes).
//   4. Formant shift: E se estira/comprime en frecuencia.
//   5. Transferencia de timbre: filtro = Objetivo - PromedioLargoDeLaEntrada.
//      Así se conserva la pronunciación (vocales frame a frame) y cambia el color
//      general de la voz hacia la voz objetivo.
#pragma once
#include "SimpleFFT.h"
#include <vector>
#include <complex>

namespace pvr
{
class EnvelopeEstimator
{
public:
    void prepare (int fftSize, double sampleRate);
    // logMag: N/2+1 valores (log natural). envOut: N/2+1.
    void compute (const float* logMag, float* envOut);

private:
    int n = 0, lifter = 0;
    SimpleFFT fft;
    std::vector<std::complex<float>> buf;
    std::vector<float> work;
};

int chooseFftSize (double sampleRate, bool lowLatency = false);

class VoiceEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, bool lowLatencyMode = false);
    bool isLowLatency() const noexcept { return lowLatency; }
    void reset();

    int getLatencySamples() const noexcept { return fftSize; }
    int getNumBins() const noexcept { return numBins; }
    double getSampleRate() const noexcept { return sr; }

    struct Params
    {
        float amount = 1.0f;       // 0..1.5 cuánto timbre objetivo
        float formantSemis = 0.0f; // -12..+12
        float pitchSemis = 0.0f;   // -12..+12
        float mix = 1.0f;          // 0..1
        float outputGain = 1.0f;   // lineal
    };
    void setParams (const Params& p) noexcept { params = p; }

    // Envolvente objetivo en la grilla del perfil (kGridSize). Vacío = sin objetivo.
    // Llamar desde el hilo de audio (es barato) o con el procesamiento detenido.
    void setTargetGrid (const std::vector<float>* gridEnv);

    void process (float* io, int numSamples) noexcept;

private:
    void processFrame() noexcept;
    static float interp (const std::vector<float>& v, float pos, float fallback) noexcept;

    double sr = 48000.0;
    int fftSize = 1024, hop = 256, numBins = 513;
    SimpleFFT fft;
    EnvelopeEstimator envEst;
    Params params;

    std::vector<float> window, inQueue, outAccum, outQueue;
    int hopCount = 0;
    std::vector<std::complex<float>> spec;

    std::vector<float> logMag, phase, prevPhase, outPhase, trueFreq, env, fine;
    std::vector<float> fineOut, envWarp, inputLtas, ltasWarp, target, correction, outLog;
    std::vector<float> peakPhase, newPhase;
    std::vector<int> peakOf, peakList;
    bool hasTarget = false, wasShifting = false, lowLatency = false;
    double ltasCount = 0.0;
    float ltasCoeff = 0.99f;
    float bandLo = 0.0f, bandHi = 0.0f; // bins de la banda de normalización

    std::vector<float> dryDelay;
    int dryPos = 0;
};
} // namespace pvr
