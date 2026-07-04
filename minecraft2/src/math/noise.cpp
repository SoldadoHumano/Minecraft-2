#include "noise.h"
#include <cmath>

namespace mc::math {

inline int Hash2D(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393U + (uint32_t)y * 668265263U + seed;
  h = (h ^ (h >> 13)) * 1274126177U;
  return h & 7;
}

inline int Hash3D(int x, int y, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393U + (uint32_t)y * 668265263U +
               (uint32_t)z * 137561653U + seed;
  h = (h ^ (h >> 13)) * 1274126177U;
  return h & 15;
}

PerlinNoise::PerlinNoise(uint32_t seed) : m_seed(seed) {}

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
  int X = (int)std::floor(x);
  int Y = (int)std::floor(y);

  x -= std::floor(x);
  y -= std::floor(y);

  float u = Fade(x);
  float v = Fade(y);

  int h00 = Hash2D(X, Y, m_seed);
  int h10 = Hash2D(X + 1, Y, m_seed);
  int h01 = Hash2D(X, Y + 1, m_seed);
  int h11 = Hash2D(X + 1, Y + 1, m_seed);

  return Lerp(v, Lerp(u, Grad(h00, x, y), Grad(h10, x - 1, y)),
              Lerp(u, Grad(h01, x, y - 1), Grad(h11, x - 1, y - 1)));
}

float PerlinNoise::Noise3D(float x, float y, float z) const {
  int X = (int)std::floor(x);
  int Y = (int)std::floor(y);
  int Z = (int)std::floor(z);

  x -= std::floor(x);
  y -= std::floor(y);
  z -= std::floor(z);

  float u = Fade(x);
  float v = Fade(y);
  float w = Fade(z);

  int h000 = Hash3D(X, Y, Z, m_seed);
  int h100 = Hash3D(X + 1, Y, Z, m_seed);
  int h010 = Hash3D(X, Y + 1, Z, m_seed);
  int h110 = Hash3D(X + 1, Y + 1, Z, m_seed);
  int h001 = Hash3D(X, Y, Z + 1, m_seed);
  int h101 = Hash3D(X + 1, Y, Z + 1, m_seed);
  int h011 = Hash3D(X, Y + 1, Z + 1, m_seed);
  int h111 = Hash3D(X + 1, Y + 1, Z + 1, m_seed);

  return Lerp(
      w,
      Lerp(v, Lerp(u, Grad(h000, x, y, z), Grad(h100, x - 1, y, z)),
           Lerp(u, Grad(h010, x, y - 1, z), Grad(h110, x - 1, y - 1, z))),
      Lerp(v, Lerp(u, Grad(h001, x, y, z - 1), Grad(h101, x - 1, y, z - 1)),
           Lerp(u, Grad(h011, x, y - 1, z - 1),
                Grad(h111, x - 1, y - 1, z - 1))));
}

float PerlinNoise::Fractal2D(float x, float y, int octaves, float persistence,
                             float lacunarity) const {
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

__m256 PerlinNoise::Fractal2D_AVX2(__m256 x, __m256 y, int octaves,
                                   float persistence, float lacunarity) const {
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

    Noise2D_AVX2_ASM(out_arr, x_arr, y_arr, m_seed);

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
