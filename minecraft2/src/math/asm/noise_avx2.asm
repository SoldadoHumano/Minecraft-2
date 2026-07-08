.data
c_1     DD 8 DUP(1)
c_6     real4 6.0, 6.0, 6.0, 6.0, 6.0, 6.0, 6.0, 6.0
c_15    real4 15.0, 15.0, 15.0, 15.0, 15.0, 15.0, 15.0, 15.0
c_10    real4 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0
c_1f    real4 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0

; Hash constants
c_prime1 DD 8 DUP(374761393)
c_prime2 DD 8 DUP(668265263)
c_prime3 DD 8 DUP(1274126177)
c_mask7  DD 8 DUP(7)

; Gradient tables for LUT (vpermps)
; Indices 0 to 7 correspond to 8 unique 2D gradients for Perlin
; (1,1), (-1,1), (1,-1), (-1,-1), (1,0), (-1,0), (0,1), (0,-1)
c_gradX real4 1.0, -1.0, 1.0, -1.0, 1.0, -1.0, 0.0, 0.0
c_gradY real4 1.0, 1.0, -1.0, -1.0, 0.0, 0.0, 1.0, -1.0

.code

FADE_MACRO MACRO dest, t, t1, t2
    vmovups t1, ymmword ptr [c_6]
    vmulps t1, t1, t
    vmovups t2, ymmword ptr [c_15]
    vsubps t1, t1, t2
    
    vmulps t1, t1, t
    vmovups t2, ymmword ptr [c_10]
    vaddps t1, t1, t2
    
    vmulps t2, t, t
    vmulps t2, t2, t
    
    vmulps dest, t2, t1
ENDM

LERP_MACRO MACRO a, t, b, t1
    vsubps t1, b, a
    vmulps t1, t1, t
    vaddps a, a, t1
ENDM

HASH_MACRO MACRO dest, x, y, t1, seed
    vpmulld dest, x, ymmword ptr [c_prime1]
    vpmulld t1, y, ymmword ptr [c_prime2]
    vpaddd dest, dest, t1
    vpaddd dest, dest, seed
    vpsrld t1, dest, 13
    vpxor dest, dest, t1
    vpmulld dest, dest, ymmword ptr [c_prime3]
    vpand dest, dest, ymmword ptr [c_mask7]
ENDM

GRAD_MACRO MACRO dest, hash, dx, dy, t1, t2
    vpermps t1, hash, ymmword ptr [c_gradX]
    vpermps t2, hash, ymmword ptr [c_gradY]
    vmulps t1, t1, dx
    vmulps t2, t2, dy
    vaddps dest, t1, t2
ENDM

; rcx = out_noise
; rdx = in_x
; r8  = in_y
; r9d = seed
Noise2D_AVX2_ASM PROC
    sub rsp, 168
    movaps xmmword ptr [rsp + 0], xmm6
    movaps xmmword ptr [rsp + 16], xmm7
    movaps xmmword ptr [rsp + 32], xmm8
    movaps xmmword ptr [rsp + 48], xmm9
    movaps xmmword ptr [rsp + 64], xmm10
    movaps xmmword ptr [rsp + 80], xmm11
    movaps xmmword ptr [rsp + 96], xmm12
    movaps xmmword ptr [rsp + 112], xmm13
    movaps xmmword ptr [rsp + 128], xmm14
    movaps xmmword ptr [rsp + 144], xmm15

    vmovups ymm0, ymmword ptr [rdx] ; in_x
    vmovups ymm1, ymmword ptr [r8]  ; in_y
    
    ; Broadcast seed to ymm13
    vmovd xmm13, r9d
    vpbroadcastd ymm13, xmm13
    
    vroundps ymm2, ymm0, 1 ; floor(x)
    vroundps ymm3, ymm1, 1 ; floor(y)
    
    vcvtps2dq ymm4, ymm2 ; X integer
    vcvtps2dq ymm5, ymm3 ; Y integer
    
    ; dx, dy
    vsubps ymm0, ymm0, ymm2 ; dx
    vsubps ymm1, ymm1, ymm3 ; dy
    
    ; u, v (using ymm2 and ymm3 to store u and v)
    FADE_MACRO ymm2, ymm0, ymm6, ymm7 ; ymm2 = u
    FADE_MACRO ymm3, ymm1, ymm6, ymm7 ; ymm3 = v
    
    ; X+1, Y+1
    vmovdqu ymm14, ymmword ptr [c_1]
    vpaddd ymm6, ymm4, ymm14 ; ymm6 = X+1
    vpaddd ymm7, ymm5, ymm14 ; ymm7 = Y+1
    
    ; Hashes
    HASH_MACRO ymm8, ymm4, ymm5, ymm15, ymm13  ; h00
    HASH_MACRO ymm9, ymm6, ymm5, ymm15, ymm13  ; h10
    HASH_MACRO ymm10, ymm4, ymm7, ymm15, ymm13 ; h01
    HASH_MACRO ymm11, ymm6, ymm7, ymm15, ymm13 ; h11
    
    ; Gradients
    vmovups ymm12, ymmword ptr [c_1f]
    vsubps ymm14, ymm0, ymm12 ; dx - 1
    vsubps ymm15, ymm1, ymm12 ; dy - 1
    
    ; GRAD_MACRO dest, hash, dx, dy, t1, t2
    GRAD_MACRO ymm4, ymm8, ymm0, ymm1, ymm5, ymm6    ; g00 -> ymm4
    GRAD_MACRO ymm7, ymm9, ymm14, ymm1, ymm5, ymm6   ; g10 -> ymm7
    GRAD_MACRO ymm8, ymm10, ymm0, ymm15, ymm5, ymm6  ; g01 -> ymm8
    GRAD_MACRO ymm9, ymm11, ymm14, ymm15, ymm5, ymm6 ; g11 -> ymm9
    
    ; Lerp
    LERP_MACRO ymm4, ymm2, ymm7, ymm12 ; lerp u: g00, g10 -> ymm4
    LERP_MACRO ymm8, ymm2, ymm9, ymm12 ; lerp u: g01, g11 -> ymm8
    LERP_MACRO ymm4, ymm3, ymm8, ymm12 ; lerp v: ymm4, ymm8 -> ymm4 (final)
    
    vmovups ymmword ptr [rcx], ymm4
    
    vzeroupper
    
    movaps xmm6, xmmword ptr [rsp + 0]
    movaps xmm7, xmmword ptr [rsp + 16]
    movaps xmm8, xmmword ptr [rsp + 32]
    movaps xmm9, xmmword ptr [rsp + 48]
    movaps xmm10, xmmword ptr [rsp + 64]
    movaps xmm11, xmmword ptr [rsp + 80]
    movaps xmm12, xmmword ptr [rsp + 96]
    movaps xmm13, xmmword ptr [rsp + 112]
    movaps xmm14, xmmword ptr [rsp + 128]
    movaps xmm15, xmmword ptr [rsp + 144]
    add rsp, 168

    ret
Noise2D_AVX2_ASM ENDP

END
