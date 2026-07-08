; The Sun™
; Version 1.1
; Now 100% brighter.

.DATA
center      REAL4 15.5, 15.5, 15.5, 15.5, 15.5, 15.5, 15.5, 15.5
r_sq        REAL4 256.0, 256.0, 256.0, 256.0, 256.0, 256.0, 256.0, 256.0
x_offsets   REAL4 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0
ones        REAL4 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0
two         REAL4 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0
c255_float  REAL4 255.0, 255.0, 255.0, 255.0, 255.0, 255.0, 255.0, 255.0
c255_int    DWORD 255, 255, 255, 255, 255, 255, 255, 255

.CODE
PUBLIC generate_sun_asm

; void generate_sun_asm(uint32_t* texture_buffer)
; Buffer pointer is in RCX (Windows x64 ABI)
generate_sun_asm PROC
    ; Load constants into YMM registers (256-bit)
    vmovups ymm0, ymmword ptr [center]
    vmovups ymm1, ymmword ptr [r_sq]
    vmovups ymm2, ymmword ptr [x_offsets]
    vmovups ymm3, ymmword ptr [ones]
    vmovups ymm4, ymmword ptr [c255_float]
    vmovups ymm13, ymmword ptr [two]
    vmovdqu ymm14, ymmword ptr [c255_int]    ; R channel constant
    vxorps  ymm5, ymm5, ymm5                 ; ymm5 = 0.0 (for negative value clamping)

    xor rax, rax                 ; rax (Y) = 0
loop_y:
    ; ymm6 = float(Y) broadcasted to all 8 slots
    cvtsi2ss xmm6, eax
    vbroadcastss ymm6, xmm6
    
    ; dy = Y - center
    vsubps ymm6, ymm6, ymm0
    ; dy_sq = dy * dy
    vmulps ymm7, ymm6, ymm6      ; ymm7 stores dy_sq for the inner loop

    xor rdx, rdx                 ; rdx (X) = 0
loop_x:
    ; ymm8 = float(X base) broadcasted to 8 slots
    cvtsi2ss xmm8, edx
    vbroadcastss ymm8, xmm8
    
    ; vx = X base + offsets [0, 1, 2, 3, 4, 5, 6, 7]
    vaddps ymm8, ymm8, ymm2
    
    ; dx = vx - center
    vsubps ymm8, ymm8, ymm0
    ; dx_sq = dx * dx
    vmulps ymm8, ymm8, ymm8
    
    ; dist_sq = dx_sq + dy_sq
    vaddps ymm8, ymm8, ymm7
    
    ; ratio = dist_sq / r_sq
    vdivps ymm8, ymm8, ymm1
    
    ; intensity = (1.0 - ratio) * 2.0
    vsubps ymm8, ymm3, ymm8
    vmulps ymm8, ymm8, ymm13
    
    ; clamp: min(max(intensity, 0.0), 1.0)
    vmaxps ymm8, ymm8, ymm5
    vminps ymm8, ymm8, ymm3
    
    ; intensity255 = intensity * 255.0
    vmulps ymm8, ymm8, ymm4
    ; Convert the 8 floats to 8 truncated integers (int_intensity)
    vcvttps2dq ymm8, ymm8
    
    ; --- RGBA Packing (Bitwise on 32-bit Integers) ---
    ; Alpha: A = intensity << 24
    vpslld ymm9, ymm8, 24
    
    ; Blue: B = (intensity >> 1) << 16
    vpsrld ymm10, ymm8, 1
    vpslld ymm10, ymm10, 16
    
    ; Green: G = intensity << 8
    vpslld ymm11, ymm8, 8
    
    ; Red: R = 255 (already in ymm14)
    
    ; OR everything: pixel = A | B | G | R
    vpor ymm12, ymm9, ymm10
    vpor ymm12, ymm12, ymm11
    vpor ymm12, ymm12, ymm14
    
    ; Calculate memory offset: (Y * 32 + X) * 4
    mov r8, rax
    shl r8, 5                   ; Y * 32
    add r8, rdx                 ; (Y * 32) + X
    shl r8, 2                   ; multiply by 4 bytes (uint32_t)
    
    ; Store the 8 pixels (32 bytes) directly to memory
    vmovdqu ymmword ptr [rcx + r8], ymm12
    
    ; Increment X by 8
    add rdx, 8
    cmp rdx, 32
    jl loop_x

    ; Increment Y by 1
    inc rax
    cmp rax, 32
    jl loop_y

    ; Clear upper half of YMM registers
    vzeroupper
    ret
generate_sun_asm ENDP

END