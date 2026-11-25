// PVR VoiceMorph - FFT radix-2 iterativa, sin dependencias.
#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <cstddef>

namespace pvr
{
constexpr double kPi = 3.14159265358979323846;

class SimpleFFT
{
public:
    using cpx = std::complex<float>;

    void prepare (int sizePow2)
    {
        n = sizePow2;
        bitrev.assign ((size_t) n, 0);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            bitrev[(size_t) i] = r;
        }
        twiddle.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * kPi * i / n;
            twiddle[(size_t) i] = cpx ((float) std::cos (a), (float) std::sin (a));
        }
    }

    int size() const noexcept { return n; }

    // In-place. inverse=true aplica la escala 1/N.
    void transform (cpx* data, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bitrev[(size_t) i];
            if (j > i) std::swap (data[i], data[j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    cpx w = twiddle[(size_t) (k * step)];
                    if (inverse) w = std::conj (w);
                    const cpx u = data[i + k];
                    const cpx v = data[i + k + half] * w;
                    data[i + k] = u + v;
                    data[i + k + half] = u - v;
                }
            }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) data[i] *= s;
        }
    }

private:
    int n = 0;
    std::vector<int> bitrev;
    std::vector<cpx> twiddle;
};
} // namespace pvr
