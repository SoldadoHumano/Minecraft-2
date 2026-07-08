# Minecraft-2

> A Minecraft-inspired voxel engine built from scratch using **Assembly**, **C++**, **AVX2**, and **Vulkan**.

---

## Features

* Vulkan renderer
* Infinite procedural terrain
* Multi-threaded chunk generation
* Greedy meshing
* Handwritten x86-64 Assembly optimizations
* AVX2 SIMD acceleration
* Procedurally generated Sun™
* Software ray tracing (3D DDA, GPU-accelerated)

---

## Tech Stack

| Technology      | Purpose                       |
| --------------- | ----------------------------- |
| C++             | Core engine                   |
| x86-64 Assembly | Performance-critical routines |
| AVX2            | SIMD acceleration             |
| Vulkan          | Rendering                     |
| CMake           | Build system                  |

---

## The Sun™

Unlike traditional game engines, **The Sun™** is not loaded from a PNG.

It is procedurally generated at startup by handwritten x86-64 AVX2 assembly.

```
Texture Resolution : 32 × 32
Texture Size       : 4096 bytes
sun.asm Size       : 4096 bytes
Storage Medium     : Pure determination
```

---

## Performance

Current development targets:

* High chunk generation throughput
* Low memory overhead
* Minimal CPU bottlenecks
* Efficient SIMD utilization
* Modern Vulkan rendering pipeline

---

## 💻 Platform & Requirements

### Tested on

* **Operating System:** Windows 11 (64-bit)

### Recommended Hardware

* x86-64 processor with **AVX2** support
* Dedicated GPU with **Vulkan 1.3** support
* 512 MB RAM minimum

### Build Requirements

* CMake
* A C++20 compatible compiler
* Vulkan SDK
* MASM (Microsoft Macro Assembler)

> **Note:** Some performance-critical components are implemented using handwritten x86-64 AVX2 assembly. CPUs without AVX2 support are currently **not supported**.

---

## Screenshots

> *(Coming soon)*

---

## Current Status

This project is under active development.

Expect bugs, broken features, and questionable engineering decisions.

Sometimes all three at once.

---

## License

This project is licensed under the MIT License.

Except **The Sun™**, which has its own legal department.
