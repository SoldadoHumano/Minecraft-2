.data
const_32  dd 8 dup(42000000h) ; 32.0f
const_64  dd 8 dup(42800000h) ; 64.0f
const_1   dd 8 dup(3f800000h) ; 1.0f
const_255 dd 8 dup(437f0000h) ; 255.0f

.code

; extern "C" void CalculateHeights_AVX2(const float* noise, int* heights);
CalculateHeights_AVX2 PROC
    vmovups ymm0, ymmword ptr [rcx]
    vmovups ymm1, ymmword ptr [const_32]
    vmovups ymm2, ymmword ptr [const_64]
    
    ; ymm0 = (ymm0 * ymm1) + ymm2
    vfmadd213ps ymm0, ymm1, ymm2
    
    vmovups ymm3, ymmword ptr [const_1]
    vmaxps ymm0, ymm0, ymm3
    
    vmovups ymm4, ymmword ptr [const_255]
    vminps ymm0, ymm0, ymm4
    
    ; Convert to integers with truncation
    vcvttps2dq ymm0, ymm0
    
    ; Store to heights array
    vmovdqu ymmword ptr [rdx], ymm0
    
    vzeroupper
    ret
CalculateHeights_AVX2 ENDP

; extern "C" void FillStone_AVX2(uint8_t* col, int stoneHeight, uint8_t stoneType);
FillStone_AVX2 PROC
    ; rcx = col
    ; edx = stoneHeight
    ; r8b = stoneType
    
    ; Broadcast stoneType to ymm0
    movzx r8d, r8b
    movd xmm0, r8d
    vpbroadcastb ymm0, xmm0
    
    xor eax, eax ; y = 0
    mov r9d, edx
    sub r9d, 31
    cmp eax, r9d
    jge fill_remainder

fill_loop:
    vmovdqu ymmword ptr [rcx + rax], ymm0
    add eax, 32
    cmp eax, r9d
    jl fill_loop

fill_remainder:
    ; fill the rest scalar
    cmp eax, edx
    jge end_fill
scalar_loop:
    mov byte ptr [rcx + rax], r8b
    inc eax
    cmp eax, edx
    jl scalar_loop

end_fill:
    vzeroupper
    ret
FillStone_AVX2 ENDP

END
