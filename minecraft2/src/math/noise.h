#pragma once
#include <cstdint>

namespace mc::math {

extern "C" void Noise2D_AVX2_ASM(float *out_noise, const float *in_x,
                                 const float *in_y, int seed);

class PerlinNoise {
public:
  PerlinNoise(uint32_t seed = 0);

  float Noise2D(float x, float y) const;
  float Noise3D(float x, float y, float z) const;

  // Fractal Brownian Motion
  float Fractal2D(float x, float y, int octaves = 4, float persistence = 0.5f,
                  float lacunarity = 2.0f) const;

  // AVX2 SIMD Versions (8 floats at once)
  void Fractal2D_AVX2(float* out_val, const float* x, const float* y, int octaves = 4,
                        float persistence = 0.5f,
                        float lacunarity = 2.0f) const;

private:
  uint32_t m_seed;

  inline float Fade(float t) const {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
  }

  inline float Lerp(float t, float a, float b) const { return a + t * (b - a); }

  float Grad(int hash, float x, float y) const;
  float Grad(int hash, float x, float y, float z) const;
};

} // namespace mc::math
