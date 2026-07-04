#include "noise.h"
#include <numeric>
#include <random>
#include <cmath>
#include <algorithm>

namespace mc::math {

PerlinNoise::PerlinNoise(uint32_t seed) {
    p.resize(256);
    std::iota(p.begin(), p.end(), 0);
    
    std::mt19937 engine(seed);
    std::shuffle(p.begin(), p.end(), engine);
    
    // Duplicate the permutation table to avoid overflow
    p.insert(p.end(), p.begin(), p.end());
}

float PerlinNoise::Grad(int hash, float x, float y) const {
    int h = hash & 3; // Convert low 2 bits of hash code
    float u = h < 2 ? x : y;
    float v = h < 2 ? y : x;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float PerlinNoise::Grad(int hash, float x, float y, float z) const {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float PerlinNoise::Noise2D(float x, float y) const {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;

    x -= std::floor(x);
    y -= std::floor(y);

    float u = Fade(x);
    float v = Fade(y);

    int A = p[X] + Y;
    int B = p[X + 1] + Y;

    return Lerp(v, Lerp(u, Grad(p[A], x, y),
                           Grad(p[B], x - 1, y)),
                   Lerp(u, Grad(p[A + 1], x, y - 1),
                           Grad(p[B + 1], x - 1, y - 1)));
}

float PerlinNoise::Noise3D(float x, float y, float z) const {
    int X = (int)std::floor(x) & 255;
    int Y = (int)std::floor(y) & 255;
    int Z = (int)std::floor(z) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    float u = Fade(x);
    float v = Fade(y);
    float w = Fade(z);

    int A  = p[X] + Y;
    int AA = p[A] + Z;
    int AB = p[A + 1] + Z;
    int B  = p[X + 1] + Y;
    int BA = p[B] + Z;
    int BB = p[B + 1] + Z;

    return Lerp(w, Lerp(v, Lerp(u, Grad(p[AA], x, y, z),
                                   Grad(p[BA], x - 1, y, z)),
                           Lerp(u, Grad(p[AB], x, y - 1, z),
                                   Grad(p[BB], x - 1, y - 1, z))),
                   Lerp(v, Lerp(u, Grad(p[AA + 1], x, y, z - 1),
                                   Grad(p[BA + 1], x - 1, y, z - 1)),
                           Lerp(u, Grad(p[AB + 1], x, y - 1, z - 1),
                                   Grad(p[BB + 1], x - 1, y - 1, z - 1))));
}

float PerlinNoise::Fractal2D(float x, float y, int octaves, float persistence, float lacunarity) const {
    float total = 0.0f;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0.0f;
    
    for (int i = 0; i < octaves; ++i) {
        total += Noise2D(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    
    return total / maxValue;
}

// ------------------------------------------------------------------------------------------------
// AVX2 SIMD Optimizations
// ------------------------------------------------------------------------------------------------

__m256 PerlinNoise::Fractal2D_AVX2(__m256 x, __m256 y, int octaves, float persistence, float lacunarity) const {
    __m256 total = _mm256_setzero_ps();
    __m256 frequency = _mm256_set1_ps(1.0f);
    __m256 amplitude = _mm256_set1_ps(1.0f);
    float maxValScalar = 0.0f;
    float ampScalar = 1.0f;
    
    __m256 vPersistence = _mm256_set1_ps(persistence);
    __m256 vLacunarity = _mm256_set1_ps(lacunarity);
    
    for (int i = 0; i < octaves; ++i) {
        __m256 nx = _mm256_mul_ps(x, frequency);
        __m256 ny = _mm256_mul_ps(y, frequency);
        
        alignas(32) float x_arr[8];
        alignas(32) float y_arr[8];
        alignas(32) float out_arr[8];
        _mm256_store_ps(x_arr, nx);
        _mm256_store_ps(y_arr, ny);
        
        Noise2D_AVX2_ASM(out_arr, x_arr, y_arr, p.data());
        
        __m256 noiseVal = _mm256_load_ps(out_arr);
        total = _mm256_add_ps(total, _mm256_mul_ps(noiseVal, amplitude));
        
        maxValScalar += ampScalar;
        ampScalar *= persistence;
        
        amplitude = _mm256_mul_ps(amplitude, vPersistence);
        frequency = _mm256_mul_ps(frequency, vLacunarity);
    }
    
    return _mm256_div_ps(total, _mm256_set1_ps(maxValScalar));
}

} // namespace mc::math
