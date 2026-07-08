.code

; extern "C" void ChunkMesh_BuildVisibilityMask_AVX2(const uint8_t* sliceBlocks, const uint8_t* neighborBlocks, uint8_t* maskOut);
ChunkMesh_BuildVisibilityMask_AVX2 PROC
    ; rcx = sliceBlocks (256 bytes)
    ; rdx = neighborBlocks (256 bytes)
    ; r8 = maskOut (256 bytes)

    vpxor ymm0, ymm0, ymm0 ; Air block is 0
    vpcmpeqb ymm2, ymm2, ymm2 ; all ones (FF)

    ; iter 0
    vmovdqu ymm1, ymmword ptr [rcx + 0]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 0]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 0], ymm1
    
    ; iter 1
    vmovdqu ymm1, ymmword ptr [rcx + 32]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 32]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 32], ymm1

    ; iter 2
    vmovdqu ymm1, ymmword ptr [rcx + 64]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 64]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 64], ymm1

    ; iter 3
    vmovdqu ymm1, ymmword ptr [rcx + 96]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 96]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 96], ymm1

    ; iter 4
    vmovdqu ymm1, ymmword ptr [rcx + 128]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 128]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 128], ymm1

    ; iter 5
    vmovdqu ymm1, ymmword ptr [rcx + 160]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 160]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 160], ymm1

    ; iter 6
    vmovdqu ymm1, ymmword ptr [rcx + 192]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 192]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 192], ymm1

    ; iter 7
    vmovdqu ymm1, ymmword ptr [rcx + 224]
    vpcmpeqb ymm1, ymm1, ymm0
    vpxor ymm1, ymm1, ymm2
    vmovdqu ymm3, ymmword ptr [rdx + 224]
    vpcmpeqb ymm3, ymm3, ymm0
    vpand ymm1, ymm1, ymm3
    vmovdqu ymmword ptr [r8 + 224], ymm1
    
    vzeroupper
    ret
ChunkMesh_BuildVisibilityMask_AVX2 ENDP

END
