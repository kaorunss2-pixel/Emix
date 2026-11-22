#pragma once

#include <cmath>
#include <algorithm>

class Biquad
{
public:
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    // ---- internal: normalise by a0 and store ----
    void setRaw(float nb0, float nb1, float nb2, float na0, float na1, float na2)
    {
        const float inv = 1.0f / na0;
        b0 = nb0 * inv;
        b1 = nb1 * inv;
        b2 = nb2 * inv;
        a1 = na1 * inv;
        a2 = na2 * inv;
    }

    static float clampFreq(float frequency, float sampleRate)
    {
        return std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    }

    void setPeaking(
        float sampleRate,
        float frequency,
        float gainDB,
        float q)
    {
        frequency = clampFreq(frequency, sampleRate);

        q = std::max(q, 0.1f);

        float A = std::pow(10.0f, gainDB / 40.0f);
        float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        float alpha = std::sin(w) / (2.0f * q);
        float cosW = std::cos(w);

        float a0 = 1.0f + alpha / A;

        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosW) / a0;
        b2 = (1.0f - alpha * A) / a0;

        a1 = (-2.0f * cosW) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void setLowPass(float sampleRate, float frequency, float q)
    {
        frequency = clampFreq(frequency, sampleRate);
        q = std::max(q, 0.1f);
        const float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        setRaw((1.0f - c) * 0.5f, 1.0f - c, (1.0f - c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setHighPass(float sampleRate, float frequency, float q)
    {
        frequency = clampFreq(frequency, sampleRate);
        q = std::max(q, 0.1f);
        const float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        setRaw((1.0f + c) * 0.5f, -(1.0f + c), (1.0f + c) * 0.5f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    // constant 0 dB peak gain band-pass
    void setBandPass(float sampleRate, float frequency, float q)
    {
        frequency = clampFreq(frequency, sampleRate);
        q = std::max(q, 0.1f);
        const float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        setRaw(alpha, 0.0f, -alpha,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setNotch(float sampleRate, float frequency, float q)
    {
        frequency = clampFreq(frequency, sampleRate);
        q = std::max(q, 0.1f);
        const float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        setRaw(1.0f, -2.0f * c, 1.0f,
            1.0f + alpha, -2.0f * c, 1.0f - alpha);
    }

    void setLowShelf(float sampleRate, float frequency, float gainDB, float q)
    {
        frequency = clampFreq(frequency, sampleRate);
        q = std::max(q, 0.1f);
        const float A = std::pow(10.0f, gainDB / 40.0f);
        const float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        const float t = 2.0f * std::sqrt(A) * alpha;
        setRaw(A * ((A + 1.0f) - (A - 1.0f) * c + t),
            2.0f * A * ((A - 1.0f) - (A + 1.0f) * c),
            A * ((A + 1.0f) - (A - 1.0f) * c - t),
            (A + 1.0f) + (A - 1.0f) * c + t,
            -2.0f * ((A - 1.0f) + (A + 1.0f) * c),
            (A + 1.0f) + (A - 1.0f) * c - t);
    }

    void setHighShelf(float sampleRate, float frequency, float gainDB, float q)
    {
        frequency = clampFreq(frequency, sampleRate);
        q = std::max(q, 0.1f);
        const float A = std::pow(10.0f, gainDB / 40.0f);
        const float w = 2.0f * 3.14159265359f * frequency / sampleRate;
        const float alpha = std::sin(w) / (2.0f * q);
        const float c = std::cos(w);
        const float t = 2.0f * std::sqrt(A) * alpha;
        setRaw(A * ((A + 1.0f) + (A - 1.0f) * c + t),
            -2.0f * A * ((A - 1.0f) + (A + 1.0f) * c),
            A * ((A + 1.0f) + (A - 1.0f) * c - t),
            (A + 1.0f) - (A - 1.0f) * c + t,
            2.0f * ((A - 1.0f) - (A + 1.0f) * c),
            (A + 1.0f) - (A - 1.0f) * c - t);
    }

    void copyCoeffsFrom(const Biquad& o)
    {
        b0 = o.b0; b1 = o.b1; b2 = o.b2;
        a1 = o.a1; a2 = o.a2;
    }

    float process(float input)
    {
        float output =
            b0 * input +
            b1 * x1 +
            b2 * x2 -
            a1 * y1 -
            a2 * y2;

        x2 = x1;
        x1 = input;

        y2 = y1;
        y1 = output;

        return output;
    }

    void reset()
    {
        x1 = x2 = 0.0f;
        y1 = y2 = 0.0f;
    }
};

class Equalizer
{
public:
    static constexpr int BandCount = 5;

    Biquad left[BandCount];
    Biquad right[BandCount];

    float frequencies[BandCount] =
    {
        150.0f,
        400.0f,
        1000.0f,
        3000.0f,
        10000.0f
    };

    float gains[BandCount] =
    {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f
    };

    float qValues[BandCount] =
    {
        0.8f, 1.0f, 1.0f, 1.0f, 0.8f
    };

    bool enabled = true;

    // last parameters the coefficients were computed for (avoids recomputing every block)
    float cachedFreq[BandCount] = {};
    float cachedGain[BandCount] = {};
    float cachedQ[BandCount] = {};
    float cachedRate = 0.0f;
    bool cacheValid = false;

    void reset()
    {
        for (int i = 0; i < BandCount; i++)
        {
            left[i].reset();
            right[i].reset();
        }
    }

    // gains / Q / frequencies back to factory values
    void resetParameters()
    {
        static const float defFreq[BandCount] = { 150.0f, 400.0f, 1000.0f, 3000.0f, 10000.0f };
        static const float defQ[BandCount] = { 0.8f, 1.0f, 1.0f, 1.0f, 0.8f };
        for (int i = 0; i < BandCount; i++)
        {
            frequencies[i] = defFreq[i];
            gains[i] = 0.0f;
            qValues[i] = defQ[i];
        }
        cacheValid = false;
    }

    void update(float sampleRate)
    {
        bool changed = !cacheValid || cachedRate != sampleRate;
        for (int i = 0; i < BandCount && !changed; i++)
            changed = cachedFreq[i] != frequencies[i] ||
            cachedGain[i] != gains[i] ||
            cachedQ[i] != qValues[i];

        if (!changed)
            return;

        for (int i = 0; i < BandCount; i++)
        {
            // only coefficients are replaced, the filter memory (x1,x2,y1,y2) is kept
            left[i].setPeaking(sampleRate, frequencies[i], gains[i], qValues[i]);
            right[i].copyCoeffsFrom(left[i]);

            cachedFreq[i] = frequencies[i];
            cachedGain[i] = gains[i];
            cachedQ[i] = qValues[i];
        }

        cachedRate = sampleRate;
        cacheValid = true;
    }

    void process(float* samples, unsigned int frameCount)
    {
        if (!enabled)
            return;

        for (unsigned int i = 0; i < frameCount; i++)
        {
            for (int band = 0; band < BandCount; band++)
            {
                samples[i * 2] =
                    left[band].process(samples[i * 2]);

                samples[i * 2 + 1] =
                    right[band].process(samples[i * 2 + 1]);
            }
        }
    }
};