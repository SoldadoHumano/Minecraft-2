.data
c_255   DD 8 DUP(255)
c_1     DD 8 DUP(1)
c_2     DD 8 DUP(2)
c_3     DD 8 DUP(3)
c_6     real4 6.0, 6.0, 6.0, 6.0, 6.0, 6.0, 6.0, 6.0
c_15    real4 15.0, 15.0, 15.0, 15.0, 15.0, 15.0, 15.0, 15.0
c_10    real4 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0
c_sign  DD 8 DUP(080000000h)
c_1f    real4 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0

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

GRAD_MACRO MACRO dest, hash, x, y, t1, t2, t3
    vmovdqu t1, ymmword ptr [c_3]
    vpand t1, hash, t1
    
    vmovdqu t2, ymmword ptr [c_2]
    vpcmpgtd t2, t2, t1
    
    vblendvps t3, y, x, t2
    vblendvps dest, x, y, t2
    
    vmovdqu t2, ymmword ptr [c_1]
    vpand t2, t1, t2
    vpxor t1, t1, t1
    vpcmpeqd t2, t2, t1
    
    vmovups t1, ymmword ptr [c_sign]
    vxorps t1, t3, t1
    vblendvps t3, t1, t3, t2
    
    vmovdqu t1, ymmword ptr [c_3]
    vpand t1, hash, t1
    vmovdqu t2, ymmword ptr [c_2]
    vpand t2, t1, t2
    vpxor t1, t1, t1
    vpcmpeqd t2, t2, t1
    
    vmovups t1, ymmword ptr [c_sign]
    vxorps t1, dest, t1
    vblendvps dest, t1, dest, t2
    
    vaddps dest, dest, t3
ENDM

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

    vmovups ymm0, ymmword ptr [rdx]
    vmovups ymm1, ymmword ptr [r8]
    
    vroundps ymm2, ymm0, 1
    vroundps ymm3, ymm1, 1
    
    vcvtps2dq ymm4, ymm2
    vcvtps2dq ymm5, ymm3
    
    vmovdqu ymm6, ymmword ptr [c_255]
    vpand ymm4, ymm4, ymm6
    vpand ymm5, ymm5, ymm6
    
    vsubps ymm0, ymm0, ymm2
    vsubps ymm1, ymm1, ymm3
    
    FADE_MACRO ymm2, ymm0, ymm6, ymm7
    FADE_MACRO ymm3, ymm1, ymm6, ymm7
    
    vpcmpeqd ymm15, ymm15, ymm15
    vpgatherdd ymm6, dword ptr [r9 + ymm4 * 4], ymm15
    vpaddd ymm6, ymm6, ymm5
    
    vmovdqu ymm14, ymmword ptr [c_1]
    vpaddd ymm7, ymm4, ymm14
    vpcmpeqd ymm15, ymm15, ymm15
    vpgatherdd ymm8, dword ptr [r9 + ymm7 * 4], ymm15
    vpaddd ymm7, ymm8, ymm5
    
    vpaddd ymm8, ymm6, ymm14
    vpaddd ymm9, ymm7, ymm14
    
    vpcmpeqd ymm15, ymm15, ymm15
    vpgatherdd ymm10, dword ptr [r9 + ymm6 * 4], ymm15
    
    vpcmpeqd ymm15, ymm15, ymm15
    vpgatherdd ymm11, dword ptr [r9 + ymm7 * 4], ymm15
    
    vpcmpeqd ymm15, ymm15, ymm15
    vpgatherdd ymm12, dword ptr [r9 + ymm8 * 4], ymm15
    
    vpcmpeqd ymm15, ymm15, ymm15
    vpgatherdd ymm13, dword ptr [r9 + ymm9 * 4], ymm15
    
    GRAD_MACRO ymm6, ymm10, ymm0, ymm1, ymm7, ymm8, ymm9
    
    vmovups ymm10, ymmword ptr [c_1f]
    vsubps ymm14, ymm0, ymm10
    GRAD_MACRO ymm7, ymm11, ymm14, ymm1, ymm8, ymm9, ymm10
    
    vmovups ymm10, ymmword ptr [c_1f]
    vsubps ymm15, ymm1, ymm10
    GRAD_MACRO ymm8, ymm12, ymm0, ymm15, ymm9, ymm10, ymm11
    
    GRAD_MACRO ymm9, ymm13, ymm14, ymm15, ymm10, ymm11, ymm12
    
    LERP_MACRO ymm6, ymm2, ymm7, ymm10
    LERP_MACRO ymm8, ymm2, ymm9, ymm10
    LERP_MACRO ymm6, ymm3, ymm8, ymm10
    
    vmovups ymmword ptr [rcx], ymm6
    
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
