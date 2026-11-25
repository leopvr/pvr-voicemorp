// PVR VoiceMorph - Perfil de voz: envolvente espectral promedio de una voz objetivo.
#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace pvr
{
// Grilla fija e independiente del sample rate: kGridSize puntos lineales entre 0 y kGridMaxHz.
constexpr int   kGridSize  = 512;
constexpr float kGridMaxHz = 22050.0f;

struct VoiceProfile
{
    std::string name;
    std::vector<float> envelope;   // log natural de magnitud, normalizado (media 0 en 100-8000 Hz)
    float seconds = 0.0f;          // segundos de voz útil analizados
    float medianF0 = 0.0f;         // Hz (0 = desconocido)

    bool isValid() const noexcept { return (int) envelope.size() == kGridSize; }
};

// Analiza una muestra mono (idealmente voz seca, 5-30 s) y devuelve su perfil.
VoiceProfile analyseVoice (const float* samples, int numSamples, double sampleRate, const std::string& name);

// Genera una voz sintética a partir de un "género" (0 = grave/masculino, 1 = agudo/femenino)
// y una semilla de color (0..1). Equivalente simple al generador por color de Vocoflex.
VoiceProfile generateVoice (float gender, float colour, uint32_t seed);

// Códigos compartibles para voces generadas, ej. "PVR-7FA3-C2-80".
std::string makeVoiceCode (float gender, float colour, uint32_t seed);
bool parseVoiceCode (const std::string& code, float& gender, float& colour, uint32_t& seed);

// Normaliza en el lugar: resta la media de la banda 100-8000 Hz.
void normaliseEnvelope (std::vector<float>& gridEnv);
} // namespace pvr
