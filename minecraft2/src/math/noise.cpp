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

void PerlinNoise::Fractal2D_AVX2(float* out_val, const float* x, const float* y, int octaves,
                                   float persistence, float lacunarity) const {
  alignas(32) float total[8] = {0};
  float frequency = 1.0f;
  float amplitude = 1.0f;
  float maxValScalar = 0.0f;

  for (int i = 0; i < octaves; ++i) {
    alignas(32) float x_arr[8];
    alignas(32) float y_arr[8];
    alignas(32) float noise_arr[8];
    
    for (int j = 0; j < 8; ++j) {
        x_arr[j] = x[j] * frequency;
        y_arr[j] = y[j] * frequency;
    }

    Noise2D_AVX2_ASM(noise_arr, x_arr, y_arr, m_seed);

    for (int j = 0; j < 8; ++j) {
        total[j] += noise_arr[j] * amplitude;
    }

    maxValScalar += amplitude;
    amplitude *= persistence;
    frequency *= lacunarity;
  }

  for (int j = 0; j < 8; ++j) {
      out_val[j] = total[j] / maxValScalar;
  }
}

} // namespace mc::math
