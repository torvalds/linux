public _llvm_blake3_hash_many_avx512
public llvm_blake3_hash_many_avx512
public llvm_blake3_compress_in_place_avx512
public _llvm_blake3_compress_in_place_avx512
public llvm_blake3_compress_xof_avx512
public _llvm_blake3_compress_xof_avx512

_TEXT   SEGMENT ALIGN(16) 'CODE'

ALIGN   16
llvm_blake3_hash_many_avx512 PROC
_llvm_blake3_hash_many_avx512 PROC
        push    r15
        push    r14
        push    r13
        push    r12
        push    rdi
        push    rsi
        push    rbx
        push    rbp
        mov     rbp, rsp
        sub     rsp, 304
        and     rsp, 0FFFFFFFFFFFFFFC0H
        vmovdqa xmmword ptr [rsp+90H], xmm6
        vmovdqa xmmword ptr [rsp+0A0H], xmm7
        vmovdqa xmmword ptr [rsp+0B0H], xmm8
        vmovdqa xmmword ptr [rsp+0C0H], xmm9
        vmovdqa xmmword ptr [rsp+0D0H], xmm10
        vmovdqa xmmword ptr [rsp+0E0H], xmm11
        vmovdqa xmmword ptr [rsp+0F0H], xmm12
        vmovdqa xmmword ptr [rsp+100H], xmm13
        vmovdqa xmmword ptr [rsp+110H], xmm14
        vmovdqa xmmword ptr [rsp+120H], xmm15
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
        mov     rcx, r9
        mov     r8, qword ptr [rbp+68H]
        movzx   r9, byte ptr [rbp+70H]
        neg     r9
        kmovw   k1, r9d
        vmovd   xmm0, r8d
        vpbroadcastd ymm0, xmm0
        shr     r8, 32
        vmovd   xmm1, r8d
        vpbroadcastd ymm1, xmm1
        vmovdqa ymm4, ymm1
        vmovdqa ymm5, ymm1
        vpaddd  ymm2, ymm0, ymmword ptr [ADD0]
        vpaddd  ymm3, ymm0, ymmword ptr [ADD0+32]
        vpcmpud k2, ymm2, ymm0, 1
        vpcmpud k3, ymm3, ymm0, 1
        ; XXX: ml64.exe does not currently understand the syntax. We use a workaround.
        vpbroadcastd ymm6, dword ptr [ADD1]
        vpaddd  ymm4 {k2}, ymm4, ymm6
        vpaddd  ymm5 {k3}, ymm5, ymm6
        ; vpaddd  ymm4 {k2}, ymm4, dword ptr [ADD1] {1to8}
        ; vpaddd  ymm5 {k3}, ymm5, dword ptr [ADD1] {1to8}
        knotw   k2, k1
        vmovdqa32 ymm2 {k2}, ymm0
        vmovdqa32 ymm3 {k2}, ymm0
        vmovdqa32 ymm4 {k2}, ymm1
        vmovdqa32 ymm5 {k2}, ymm1
        vmovdqa ymmword ptr [rsp], ymm2
        vmovdqa ymmword ptr [rsp+20H], ymm3
        vmovdqa ymmword ptr [rsp+40H], ymm4
        vmovdqa ymmword ptr [rsp+60H], ymm5
        shl     rdx, 6
        mov     qword ptr [rsp+80H], rdx
        cmp     rsi, 16
        jc      final15blocks
outerloop16:
        vpbroadcastd zmm0, dword ptr [rcx]
        vpbroadcastd zmm1, dword ptr [rcx+1H*4H]
        vpbroadcastd zmm2, dword ptr [rcx+2H*4H]
        vpbroadcastd zmm3, dword ptr [rcx+3H*4H]
        vpbroadcastd zmm4, dword ptr [rcx+4H*4H]
        vpbroadcastd zmm5, dword ptr [rcx+5H*4H]
        vpbroadcastd zmm6, dword ptr [rcx+6H*4H]
        vpbroadcastd zmm7, dword ptr [rcx+7H*4H]
        movzx   eax, byte ptr [rbp+78H]
        movzx   ebx, byte ptr [rbp+80H]
        or      eax, ebx
        xor     edx, edx
ALIGN   16
innerloop16:
        movzx   ebx, byte ptr [rbp+88H]
        or      ebx, eax
        add     rdx, 64
        cmp     rdx, qword ptr [rsp+80H]
        cmove   eax, ebx
        mov     dword ptr [rsp+88H], eax
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        mov     r10, qword ptr [rdi+10H]
        mov     r11, qword ptr [rdi+18H]
        mov     r12, qword ptr [rdi+40H]
        mov     r13, qword ptr [rdi+48H]
        mov     r14, qword ptr [rdi+50H]
        mov     r15, qword ptr [rdi+58H]
        vmovdqu32 ymm16, ymmword ptr [rdx+r8-2H*20H]
        vinserti64x4 zmm16, zmm16, ymmword ptr [rdx+r12-2H*20H], 01H
        vmovdqu32 ymm17, ymmword ptr [rdx+r9-2H*20H]
        vinserti64x4 zmm17, zmm17, ymmword ptr [rdx+r13-2H*20H], 01H
        vpunpcklqdq zmm8, zmm16, zmm17
        vpunpckhqdq zmm9, zmm16, zmm17
        vmovdqu32 ymm18, ymmword ptr [rdx+r10-2H*20H]
        vinserti64x4 zmm18, zmm18, ymmword ptr [rdx+r14-2H*20H], 01H
        vmovdqu32 ymm19, ymmword ptr [rdx+r11-2H*20H]
        vinserti64x4 zmm19, zmm19, ymmword ptr [rdx+r15-2H*20H], 01H
        vpunpcklqdq zmm10, zmm18, zmm19
        vpunpckhqdq zmm11, zmm18, zmm19
        mov     r8, qword ptr [rdi+20H]
        mov     r9, qword ptr [rdi+28H]
        mov     r10, qword ptr [rdi+30H]
        mov     r11, qword ptr [rdi+38H]
        mov     r12, qword ptr [rdi+60H]
        mov     r13, qword ptr [rdi+68H]
        mov     r14, qword ptr [rdi+70H]
        mov     r15, qword ptr [rdi+78H]
        vmovdqu32 ymm16, ymmword ptr [rdx+r8-2H*20H]
        vinserti64x4 zmm16, zmm16, ymmword ptr [rdx+r12-2H*20H], 01H
        vmovdqu32 ymm17, ymmword ptr [rdx+r9-2H*20H]
        vinserti64x4 zmm17, zmm17, ymmword ptr [rdx+r13-2H*20H], 01H
        vpunpcklqdq zmm12, zmm16, zmm17
        vpunpckhqdq zmm13, zmm16, zmm17
        vmovdqu32 ymm18, ymmword ptr [rdx+r10-2H*20H]
        vinserti64x4 zmm18, zmm18, ymmword ptr [rdx+r14-2H*20H], 01H
        vmovdqu32 ymm19, ymmword ptr [rdx+r11-2H*20H]
        vinserti64x4 zmm19, zmm19, ymmword ptr [rdx+r15-2H*20H], 01H
        vpunpcklqdq zmm14, zmm18, zmm19
        vpunpckhqdq zmm15, zmm18, zmm19
        vmovdqa32 zmm27, zmmword ptr [INDEX0]
        vmovdqa32 zmm31, zmmword ptr [INDEX1]
        vshufps zmm16, zmm8, zmm10, 136
        vshufps zmm17, zmm12, zmm14, 136
        vmovdqa32 zmm20, zmm16
        vpermt2d zmm16, zmm27, zmm17
        vpermt2d zmm20, zmm31, zmm17
        vshufps zmm17, zmm8, zmm10, 221
        vshufps zmm30, zmm12, zmm14, 221
        vmovdqa32 zmm21, zmm17
        vpermt2d zmm17, zmm27, zmm30
        vpermt2d zmm21, zmm31, zmm30
        vshufps zmm18, zmm9, zmm11, 136
        vshufps zmm8, zmm13, zmm15, 136
        vmovdqa32 zmm22, zmm18
        vpermt2d zmm18, zmm27, zmm8
        vpermt2d zmm22, zmm31, zmm8
        vshufps zmm19, zmm9, zmm11, 221
        vshufps zmm8, zmm13, zmm15, 221
        vmovdqa32 zmm23, zmm19
        vpermt2d zmm19, zmm27, zmm8
        vpermt2d zmm23, zmm31, zmm8
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        mov     r10, qword ptr [rdi+10H]
        mov     r11, qword ptr [rdi+18H]
        mov     r12, qword ptr [rdi+40H]
        mov     r13, qword ptr [rdi+48H]
        mov     r14, qword ptr [rdi+50H]
        mov     r15, qword ptr [rdi+58H]
        vmovdqu32 ymm24, ymmword ptr [r8+rdx-1H*20H]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r12+rdx-1H*20H], 01H
        vmovdqu32 ymm25, ymmword ptr [r9+rdx-1H*20H]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r13+rdx-1H*20H], 01H
        vpunpcklqdq zmm8, zmm24, zmm25
        vpunpckhqdq zmm9, zmm24, zmm25
        vmovdqu32 ymm24, ymmword ptr [r10+rdx-1H*20H]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r14+rdx-1H*20H], 01H
        vmovdqu32 ymm25, ymmword ptr [r11+rdx-1H*20H]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r15+rdx-1H*20H], 01H
        vpunpcklqdq zmm10, zmm24, zmm25
        vpunpckhqdq zmm11, zmm24, zmm25
        prefetcht0 byte ptr [r8+rdx+80H]
        prefetcht0 byte ptr [r12+rdx+80H]
        prefetcht0 byte ptr [r9+rdx+80H]
        prefetcht0 byte ptr [r13+rdx+80H]
        prefetcht0 byte ptr [r10+rdx+80H]
        prefetcht0 byte ptr [r14+rdx+80H]
        prefetcht0 byte ptr [r11+rdx+80H]
        prefetcht0 byte ptr [r15+rdx+80H]
        mov     r8, qword ptr [rdi+20H]
        mov     r9, qword ptr [rdi+28H]
        mov     r10, qword ptr [rdi+30H]
        mov     r11, qword ptr [rdi+38H]
        mov     r12, qword ptr [rdi+60H]
        mov     r13, qword ptr [rdi+68H]
        mov     r14, qword ptr [rdi+70H]
        mov     r15, qword ptr [rdi+78H]
        vmovdqu32 ymm24, ymmword ptr [r8+rdx-1H*20H]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r12+rdx-1H*20H], 01H
        vmovdqu32 ymm25, ymmword ptr [r9+rdx-1H*20H]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r13+rdx-1H*20H], 01H
        vpunpcklqdq zmm12, zmm24, zmm25
        vpunpckhqdq zmm13, zmm24, zmm25
        vmovdqu32 ymm24, ymmword ptr [r10+rdx-1H*20H]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r14+rdx-1H*20H], 01H
        vmovdqu32 ymm25, ymmword ptr [r11+rdx-1H*20H]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r15+rdx-1H*20H], 01H
        vpunpcklqdq zmm14, zmm24, zmm25
        vpunpckhqdq zmm15, zmm24, zmm25
        prefetcht0 byte  ptr [r8+rdx+80H]
        prefetcht0 byte ptr [r12+rdx+80H]
        prefetcht0 byte ptr [r9+rdx+80H]
        prefetcht0 byte ptr [r13+rdx+80H]
        prefetcht0 byte ptr [r10+rdx+80H]
        prefetcht0 byte ptr [r14+rdx+80H]
        prefetcht0 byte ptr [r11+rdx+80H]
        prefetcht0 byte ptr [r15+rdx+80H]
        vshufps zmm24, zmm8, zmm10, 136
        vshufps zmm30, zmm12, zmm14, 136
        vmovdqa32 zmm28, zmm24
        vpermt2d zmm24, zmm27, zmm30
        vpermt2d zmm28, zmm31, zmm30
        vshufps zmm25, zmm8, zmm10, 221
        vshufps zmm30, zmm12, zmm14, 221
        vmovdqa32 zmm29, zmm25
        vpermt2d zmm25, zmm27, zmm30
        vpermt2d zmm29, zmm31, zmm30
        vshufps zmm26, zmm9, zmm11, 136
        vshufps zmm8, zmm13, zmm15, 136
        vmovdqa32 zmm30, zmm26
        vpermt2d zmm26, zmm27, zmm8
        vpermt2d zmm30, zmm31, zmm8
        vshufps zmm8, zmm9, zmm11, 221
        vshufps zmm10, zmm13, zmm15, 221
        vpermi2d zmm27, zmm8, zmm10
        vpermi2d zmm31, zmm8, zmm10
        vpbroadcastd zmm8, dword ptr [BLAKE3_IV_0]
        vpbroadcastd zmm9, dword ptr [BLAKE3_IV_1]
        vpbroadcastd zmm10, dword ptr [BLAKE3_IV_2]
        vpbroadcastd zmm11, dword ptr [BLAKE3_IV_3]
        vmovdqa32 zmm12, zmmword ptr [rsp]
        vmovdqa32 zmm13, zmmword ptr [rsp+1H*40H]
        vpbroadcastd zmm14, dword ptr [BLAKE3_BLOCK_LEN]
        vpbroadcastd zmm15, dword ptr [rsp+22H*4H]
        vpaddd  zmm0, zmm0, zmm16
        vpaddd  zmm1, zmm1, zmm18
        vpaddd  zmm2, zmm2, zmm20
        vpaddd  zmm3, zmm3, zmm22
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm17
        vpaddd  zmm1, zmm1, zmm19
        vpaddd  zmm2, zmm2, zmm21
        vpaddd  zmm3, zmm3, zmm23
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm24
        vpaddd  zmm1, zmm1, zmm26
        vpaddd  zmm2, zmm2, zmm28
        vpaddd  zmm3, zmm3, zmm30
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm25
        vpaddd  zmm1, zmm1, zmm27
        vpaddd  zmm2, zmm2, zmm29
        vpaddd  zmm3, zmm3, zmm31
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpaddd  zmm0, zmm0, zmm18
        vpaddd  zmm1, zmm1, zmm19
        vpaddd  zmm2, zmm2, zmm23
        vpaddd  zmm3, zmm3, zmm20
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm22
        vpaddd  zmm1, zmm1, zmm26
        vpaddd  zmm2, zmm2, zmm16
        vpaddd  zmm3, zmm3, zmm29
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm17
        vpaddd  zmm1, zmm1, zmm28
        vpaddd  zmm2, zmm2, zmm25
        vpaddd  zmm3, zmm3, zmm31
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm27
        vpaddd  zmm1, zmm1, zmm21
        vpaddd  zmm2, zmm2, zmm30
        vpaddd  zmm3, zmm3, zmm24
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpaddd  zmm0, zmm0, zmm19
        vpaddd  zmm1, zmm1, zmm26
        vpaddd  zmm2, zmm2, zmm29
        vpaddd  zmm3, zmm3, zmm23
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm20
        vpaddd  zmm1, zmm1, zmm28
        vpaddd  zmm2, zmm2, zmm18
        vpaddd  zmm3, zmm3, zmm30
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm22
        vpaddd  zmm1, zmm1, zmm25
        vpaddd  zmm2, zmm2, zmm27
        vpaddd  zmm3, zmm3, zmm24
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm21
        vpaddd  zmm1, zmm1, zmm16
        vpaddd  zmm2, zmm2, zmm31
        vpaddd  zmm3, zmm3, zmm17
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpaddd  zmm0, zmm0, zmm26
        vpaddd  zmm1, zmm1, zmm28
        vpaddd  zmm2, zmm2, zmm30
        vpaddd  zmm3, zmm3, zmm29
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm23
        vpaddd  zmm1, zmm1, zmm25
        vpaddd  zmm2, zmm2, zmm19
        vpaddd  zmm3, zmm3, zmm31
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm20
        vpaddd  zmm1, zmm1, zmm27
        vpaddd  zmm2, zmm2, zmm21
        vpaddd  zmm3, zmm3, zmm17
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm16
        vpaddd  zmm1, zmm1, zmm18
        vpaddd  zmm2, zmm2, zmm24
        vpaddd  zmm3, zmm3, zmm22
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpaddd  zmm0, zmm0, zmm28
        vpaddd  zmm1, zmm1, zmm25
        vpaddd  zmm2, zmm2, zmm31
        vpaddd  zmm3, zmm3, zmm30
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm29
        vpaddd  zmm1, zmm1, zmm27
        vpaddd  zmm2, zmm2, zmm26
        vpaddd  zmm3, zmm3, zmm24
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm23
        vpaddd  zmm1, zmm1, zmm21
        vpaddd  zmm2, zmm2, zmm16
        vpaddd  zmm3, zmm3, zmm22
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm18
        vpaddd  zmm1, zmm1, zmm19
        vpaddd  zmm2, zmm2, zmm17
        vpaddd  zmm3, zmm3, zmm20
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpaddd  zmm0, zmm0, zmm25
        vpaddd  zmm1, zmm1, zmm27
        vpaddd  zmm2, zmm2, zmm24
        vpaddd  zmm3, zmm3, zmm31
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm30
        vpaddd  zmm1, zmm1, zmm21
        vpaddd  zmm2, zmm2, zmm28
        vpaddd  zmm3, zmm3, zmm17
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm29
        vpaddd  zmm1, zmm1, zmm16
        vpaddd  zmm2, zmm2, zmm18
        vpaddd  zmm3, zmm3, zmm20
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm19
        vpaddd  zmm1, zmm1, zmm26
        vpaddd  zmm2, zmm2, zmm22
        vpaddd  zmm3, zmm3, zmm23
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpaddd  zmm0, zmm0, zmm27
        vpaddd  zmm1, zmm1, zmm21
        vpaddd  zmm2, zmm2, zmm17
        vpaddd  zmm3, zmm3, zmm24
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vprord  zmm15, zmm15, 16
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 12
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vpaddd  zmm0, zmm0, zmm31
        vpaddd  zmm1, zmm1, zmm16
        vpaddd  zmm2, zmm2, zmm25
        vpaddd  zmm3, zmm3, zmm22
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm1, zmm1, zmm5
        vpaddd  zmm2, zmm2, zmm6
        vpaddd  zmm3, zmm3, zmm7
        vpxord  zmm12, zmm12, zmm0
        vpxord  zmm13, zmm13, zmm1
        vpxord  zmm14, zmm14, zmm2
        vpxord  zmm15, zmm15, zmm3
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vprord  zmm15, zmm15, 8
        vpaddd  zmm8, zmm8, zmm12
        vpaddd  zmm9, zmm9, zmm13
        vpaddd  zmm10, zmm10, zmm14
        vpaddd  zmm11, zmm11, zmm15
        vpxord  zmm4, zmm4, zmm8
        vpxord  zmm5, zmm5, zmm9
        vpxord  zmm6, zmm6, zmm10
        vpxord  zmm7, zmm7, zmm11
        vprord  zmm4, zmm4, 7
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vpaddd  zmm0, zmm0, zmm30
        vpaddd  zmm1, zmm1, zmm18
        vpaddd  zmm2, zmm2, zmm19
        vpaddd  zmm3, zmm3, zmm23
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 16
        vprord  zmm12, zmm12, 16
        vprord  zmm13, zmm13, 16
        vprord  zmm14, zmm14, 16
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 12
        vprord  zmm6, zmm6, 12
        vprord  zmm7, zmm7, 12
        vprord  zmm4, zmm4, 12
        vpaddd  zmm0, zmm0, zmm26
        vpaddd  zmm1, zmm1, zmm28
        vpaddd  zmm2, zmm2, zmm20
        vpaddd  zmm3, zmm3, zmm29
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm1, zmm1, zmm6
        vpaddd  zmm2, zmm2, zmm7
        vpaddd  zmm3, zmm3, zmm4
        vpxord  zmm15, zmm15, zmm0
        vpxord  zmm12, zmm12, zmm1
        vpxord  zmm13, zmm13, zmm2
        vpxord  zmm14, zmm14, zmm3
        vprord  zmm15, zmm15, 8
        vprord  zmm12, zmm12, 8
        vprord  zmm13, zmm13, 8
        vprord  zmm14, zmm14, 8
        vpaddd  zmm10, zmm10, zmm15
        vpaddd  zmm11, zmm11, zmm12
        vpaddd  zmm8, zmm8, zmm13
        vpaddd  zmm9, zmm9, zmm14
        vpxord  zmm5, zmm5, zmm10
        vpxord  zmm6, zmm6, zmm11
        vpxord  zmm7, zmm7, zmm8
        vpxord  zmm4, zmm4, zmm9
        vprord  zmm5, zmm5, 7
        vprord  zmm6, zmm6, 7
        vprord  zmm7, zmm7, 7
        vprord  zmm4, zmm4, 7
        vpxord  zmm0, zmm0, zmm8
        vpxord  zmm1, zmm1, zmm9
        vpxord  zmm2, zmm2, zmm10
        vpxord  zmm3, zmm3, zmm11
        vpxord  zmm4, zmm4, zmm12
        vpxord  zmm5, zmm5, zmm13
        vpxord  zmm6, zmm6, zmm14
        vpxord  zmm7, zmm7, zmm15
        movzx   eax, byte ptr [rbp+78H]
        jne     innerloop16
        mov     rbx, qword ptr [rbp+90H]
        vpunpckldq zmm16, zmm0, zmm1
        vpunpckhdq zmm17, zmm0, zmm1
        vpunpckldq zmm18, zmm2, zmm3
        vpunpckhdq zmm19, zmm2, zmm3
        vpunpckldq zmm20, zmm4, zmm5
        vpunpckhdq zmm21, zmm4, zmm5
        vpunpckldq zmm22, zmm6, zmm7
        vpunpckhdq zmm23, zmm6, zmm7
        vpunpcklqdq zmm0, zmm16, zmm18
        vpunpckhqdq zmm1, zmm16, zmm18
        vpunpcklqdq zmm2, zmm17, zmm19
        vpunpckhqdq zmm3, zmm17, zmm19
        vpunpcklqdq zmm4, zmm20, zmm22
        vpunpckhqdq zmm5, zmm20, zmm22
        vpunpcklqdq zmm6, zmm21, zmm23
        vpunpckhqdq zmm7, zmm21, zmm23
        vshufi32x4 zmm16, zmm0, zmm4, 88H
        vshufi32x4 zmm17, zmm1, zmm5, 88H
        vshufi32x4 zmm18, zmm2, zmm6, 88H
        vshufi32x4 zmm19, zmm3, zmm7, 88H
        vshufi32x4 zmm20, zmm0, zmm4, 0DDH
        vshufi32x4 zmm21, zmm1, zmm5, 0DDH
        vshufi32x4 zmm22, zmm2, zmm6, 0DDH
        vshufi32x4 zmm23, zmm3, zmm7, 0DDH
        vshufi32x4 zmm0, zmm16, zmm17, 88H
        vshufi32x4 zmm1, zmm18, zmm19, 88H
        vshufi32x4 zmm2, zmm20, zmm21, 88H
        vshufi32x4 zmm3, zmm22, zmm23, 88H
        vshufi32x4 zmm4, zmm16, zmm17, 0DDH
        vshufi32x4 zmm5, zmm18, zmm19, 0DDH
        vshufi32x4 zmm6, zmm20, zmm21, 0DDH
        vshufi32x4 zmm7, zmm22, zmm23, 0DDH
        vmovdqu32 zmmword ptr [rbx], zmm0
        vmovdqu32 zmmword ptr [rbx+1H*40H], zmm1
        vmovdqu32 zmmword ptr [rbx+2H*40H], zmm2
        vmovdqu32 zmmword ptr [rbx+3H*40H], zmm3
        vmovdqu32 zmmword ptr [rbx+4H*40H], zmm4
        vmovdqu32 zmmword ptr [rbx+5H*40H], zmm5
        vmovdqu32 zmmword ptr [rbx+6H*40H], zmm6
        vmovdqu32 zmmword ptr [rbx+7H*40H], zmm7
        vmovdqa32 zmm0, zmmword ptr [rsp]
        vmovdqa32 zmm1, zmmword ptr [rsp+1H*40H]
        vmovdqa32 zmm2, zmm0
        ; XXX: ml64.exe does not currently understand the syntax. We use a workaround.
        vpbroadcastd zmm4, dword ptr [ADD16]
        vpbroadcastd zmm5, dword ptr [ADD1]
        vpaddd  zmm2{k1}, zmm0, zmm4
        ; vpaddd  zmm2{k1}, zmm0, dword ptr [ADD16] ; {1to16}
        vpcmpud k2, zmm2, zmm0, 1
        vpaddd  zmm1 {k2}, zmm1, zmm5
        ; vpaddd  zmm1 {k2}, zmm1, dword ptr [ADD1] ; {1to16}
        vmovdqa32 zmmword ptr [rsp], zmm2
        vmovdqa32 zmmword ptr [rsp+1H*40H], zmm1
        add     rdi, 128
        add     rbx, 512
        mov     qword ptr [rbp+90H], rbx
        sub     rsi, 16
        cmp     rsi, 16
        jnc     outerloop16
        test    rsi, rsi
        jne     final15blocks
unwind:
        vzeroupper
        vmovdqa xmm6, xmmword ptr [rsp+90H]
        vmovdqa xmm7, xmmword ptr [rsp+0A0H]
        vmovdqa xmm8, xmmword ptr [rsp+0B0H]
        vmovdqa xmm9, xmmword ptr [rsp+0C0H]
        vmovdqa xmm10, xmmword ptr [rsp+0D0H]
        vmovdqa xmm11, xmmword ptr [rsp+0E0H]
        vmovdqa xmm12, xmmword ptr [rsp+0F0H]
        vmovdqa xmm13, xmmword ptr [rsp+100H]
        vmovdqa xmm14, xmmword ptr [rsp+110H]
        vmovdqa xmm15, xmmword ptr [rsp+120H]
        mov     rsp, rbp
        pop     rbp
        pop     rbx
        pop     rsi
        pop     rdi
        pop     r12
        pop     r13
        pop     r14
        pop     r15
        ret
ALIGN   16
final15blocks:
        test    esi, 8H
        je      final7blocks
        vpbroadcastd ymm0, dword ptr [rcx]
        vpbroadcastd ymm1, dword ptr [rcx+4H]
        vpbroadcastd ymm2, dword ptr [rcx+8H]
        vpbroadcastd ymm3, dword ptr [rcx+0CH]
        vpbroadcastd ymm4, dword ptr [rcx+10H]
        vpbroadcastd ymm5, dword ptr [rcx+14H]
        vpbroadcastd ymm6, dword ptr [rcx+18H]
        vpbroadcastd ymm7, dword ptr [rcx+1CH]
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        mov     r10, qword ptr [rdi+10H]
        mov     r11, qword ptr [rdi+18H]
        mov     r12, qword ptr [rdi+20H]
        mov     r13, qword ptr [rdi+28H]
        mov     r14, qword ptr [rdi+30H]
        mov     r15, qword ptr [rdi+38H]
        movzx   eax, byte ptr [rbp+78H]
        movzx   ebx, byte ptr [rbp+80H]
        or      eax, ebx
        xor     edx, edx
innerloop8:
        movzx   ebx, byte ptr [rbp+88H]
        or      ebx, eax
        add     rdx, 64
        cmp     rdx, qword ptr [rsp+80H]
        cmove   eax, ebx
        mov     dword ptr [rsp+88H], eax
        vmovups xmm8, xmmword ptr [r8+rdx-40H]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-40H], 01H
        vmovups xmm9, xmmword ptr [r9+rdx-40H]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-40H], 01H
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-40H]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-40H], 01H
        vmovups xmm11, xmmword ptr [r11+rdx-40H]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-40H], 01H
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm16, ymm12, ymm14, 136
        vshufps ymm17, ymm12, ymm14, 221
        vshufps ymm18, ymm13, ymm15, 136
        vshufps ymm19, ymm13, ymm15, 221
        vmovups xmm8, xmmword ptr [r8+rdx-30H]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-30H], 01H
        vmovups xmm9, xmmword ptr [r9+rdx-30H]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-30H], 01H
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-30H]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-30H], 01H
        vmovups xmm11, xmmword ptr [r11+rdx-30H]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-30H], 01H
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm20, ymm12, ymm14, 136
        vshufps ymm21, ymm12, ymm14, 221
        vshufps ymm22, ymm13, ymm15, 136
        vshufps ymm23, ymm13, ymm15, 221
        vmovups xmm8, xmmword ptr [r8+rdx-20H]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-20H], 01H
        vmovups xmm9, xmmword ptr [r9+rdx-20H]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-20H], 01H
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-20H]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-20H], 01H
        vmovups xmm11, xmmword ptr [r11+rdx-20H]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-20H], 01H
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm24, ymm12, ymm14, 136
        vshufps ymm25, ymm12, ymm14, 221
        vshufps ymm26, ymm13, ymm15, 136
        vshufps ymm27, ymm13, ymm15, 221
        vmovups xmm8, xmmword ptr [r8+rdx-10H]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-10H], 01H
        vmovups xmm9, xmmword ptr [r9+rdx-10H]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-10H], 01H
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-10H]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-10H], 01H
        vmovups xmm11, xmmword ptr [r11+rdx-10H]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-10H], 01H
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm28, ymm12, ymm14, 136
        vshufps ymm29, ymm12, ymm14, 221
        vshufps ymm30, ymm13, ymm15, 136
        vshufps ymm31, ymm13, ymm15, 221
        vpbroadcastd ymm8, dword ptr [BLAKE3_IV_0]
        vpbroadcastd ymm9, dword ptr [BLAKE3_IV_1]
        vpbroadcastd ymm10, dword ptr [BLAKE3_IV_2]
        vpbroadcastd ymm11, dword ptr [BLAKE3_IV_3]
        vmovdqa ymm12, ymmword ptr [rsp]
        vmovdqa ymm13, ymmword ptr [rsp+40H]
        vpbroadcastd ymm14, dword ptr [BLAKE3_BLOCK_LEN]
        vpbroadcastd ymm15, dword ptr [rsp+88H]
        vpaddd  ymm0, ymm0, ymm16
        vpaddd  ymm1, ymm1, ymm18
        vpaddd  ymm2, ymm2, ymm20
        vpaddd  ymm3, ymm3, ymm22
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm17
        vpaddd  ymm1, ymm1, ymm19
        vpaddd  ymm2, ymm2, ymm21
        vpaddd  ymm3, ymm3, ymm23
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm24
        vpaddd  ymm1, ymm1, ymm26
        vpaddd  ymm2, ymm2, ymm28
        vpaddd  ymm3, ymm3, ymm30
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm25
        vpaddd  ymm1, ymm1, ymm27
        vpaddd  ymm2, ymm2, ymm29
        vpaddd  ymm3, ymm3, ymm31
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpaddd  ymm0, ymm0, ymm18
        vpaddd  ymm1, ymm1, ymm19
        vpaddd  ymm2, ymm2, ymm23
        vpaddd  ymm3, ymm3, ymm20
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm22
        vpaddd  ymm1, ymm1, ymm26
        vpaddd  ymm2, ymm2, ymm16
        vpaddd  ymm3, ymm3, ymm29
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm17
        vpaddd  ymm1, ymm1, ymm28
        vpaddd  ymm2, ymm2, ymm25
        vpaddd  ymm3, ymm3, ymm31
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm27
        vpaddd  ymm1, ymm1, ymm21
        vpaddd  ymm2, ymm2, ymm30
        vpaddd  ymm3, ymm3, ymm24
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpaddd  ymm0, ymm0, ymm19
        vpaddd  ymm1, ymm1, ymm26
        vpaddd  ymm2, ymm2, ymm29
        vpaddd  ymm3, ymm3, ymm23
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm20
        vpaddd  ymm1, ymm1, ymm28
        vpaddd  ymm2, ymm2, ymm18
        vpaddd  ymm3, ymm3, ymm30
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm22
        vpaddd  ymm1, ymm1, ymm25
        vpaddd  ymm2, ymm2, ymm27
        vpaddd  ymm3, ymm3, ymm24
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm21
        vpaddd  ymm1, ymm1, ymm16
        vpaddd  ymm2, ymm2, ymm31
        vpaddd  ymm3, ymm3, ymm17
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpaddd  ymm0, ymm0, ymm26
        vpaddd  ymm1, ymm1, ymm28
        vpaddd  ymm2, ymm2, ymm30
        vpaddd  ymm3, ymm3, ymm29
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm23
        vpaddd  ymm1, ymm1, ymm25
        vpaddd  ymm2, ymm2, ymm19
        vpaddd  ymm3, ymm3, ymm31
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm20
        vpaddd  ymm1, ymm1, ymm27
        vpaddd  ymm2, ymm2, ymm21
        vpaddd  ymm3, ymm3, ymm17
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm16
        vpaddd  ymm1, ymm1, ymm18
        vpaddd  ymm2, ymm2, ymm24
        vpaddd  ymm3, ymm3, ymm22
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpaddd  ymm0, ymm0, ymm28
        vpaddd  ymm1, ymm1, ymm25
        vpaddd  ymm2, ymm2, ymm31
        vpaddd  ymm3, ymm3, ymm30
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm29
        vpaddd  ymm1, ymm1, ymm27
        vpaddd  ymm2, ymm2, ymm26
        vpaddd  ymm3, ymm3, ymm24
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm23
        vpaddd  ymm1, ymm1, ymm21
        vpaddd  ymm2, ymm2, ymm16
        vpaddd  ymm3, ymm3, ymm22
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm18
        vpaddd  ymm1, ymm1, ymm19
        vpaddd  ymm2, ymm2, ymm17
        vpaddd  ymm3, ymm3, ymm20
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpaddd  ymm0, ymm0, ymm25
        vpaddd  ymm1, ymm1, ymm27
        vpaddd  ymm2, ymm2, ymm24
        vpaddd  ymm3, ymm3, ymm31
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm30
        vpaddd  ymm1, ymm1, ymm21
        vpaddd  ymm2, ymm2, ymm28
        vpaddd  ymm3, ymm3, ymm17
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm29
        vpaddd  ymm1, ymm1, ymm16
        vpaddd  ymm2, ymm2, ymm18
        vpaddd  ymm3, ymm3, ymm20
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm19
        vpaddd  ymm1, ymm1, ymm26
        vpaddd  ymm2, ymm2, ymm22
        vpaddd  ymm3, ymm3, ymm23
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpaddd  ymm0, ymm0, ymm27
        vpaddd  ymm1, ymm1, ymm21
        vpaddd  ymm2, ymm2, ymm17
        vpaddd  ymm3, ymm3, ymm24
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vprord  ymm15, ymm15, 16
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 12
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vpaddd  ymm0, ymm0, ymm31
        vpaddd  ymm1, ymm1, ymm16
        vpaddd  ymm2, ymm2, ymm25
        vpaddd  ymm3, ymm3, ymm22
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxord  ymm12, ymm12, ymm0
        vpxord  ymm13, ymm13, ymm1
        vpxord  ymm14, ymm14, ymm2
        vpxord  ymm15, ymm15, ymm3
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vprord  ymm15, ymm15, 8
        vpaddd  ymm8, ymm8, ymm12
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxord  ymm4, ymm4, ymm8
        vpxord  ymm5, ymm5, ymm9
        vpxord  ymm6, ymm6, ymm10
        vpxord  ymm7, ymm7, ymm11
        vprord  ymm4, ymm4, 7
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vpaddd  ymm0, ymm0, ymm30
        vpaddd  ymm1, ymm1, ymm18
        vpaddd  ymm2, ymm2, ymm19
        vpaddd  ymm3, ymm3, ymm23
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 16
        vprord  ymm12, ymm12, 16
        vprord  ymm13, ymm13, 16
        vprord  ymm14, ymm14, 16
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 12
        vprord  ymm6, ymm6, 12
        vprord  ymm7, ymm7, 12
        vprord  ymm4, ymm4, 12
        vpaddd  ymm0, ymm0, ymm26
        vpaddd  ymm1, ymm1, ymm28
        vpaddd  ymm2, ymm2, ymm20
        vpaddd  ymm3, ymm3, ymm29
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxord  ymm15, ymm15, ymm0
        vpxord  ymm12, ymm12, ymm1
        vpxord  ymm13, ymm13, ymm2
        vpxord  ymm14, ymm14, ymm3
        vprord  ymm15, ymm15, 8
        vprord  ymm12, ymm12, 8
        vprord  ymm13, ymm13, 8
        vprord  ymm14, ymm14, 8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm9, ymm9, ymm14
        vpxord  ymm5, ymm5, ymm10
        vpxord  ymm6, ymm6, ymm11
        vpxord  ymm7, ymm7, ymm8
        vpxord  ymm4, ymm4, ymm9
        vprord  ymm5, ymm5, 7
        vprord  ymm6, ymm6, 7
        vprord  ymm7, ymm7, 7
        vprord  ymm4, ymm4, 7
        vpxor   ymm0, ymm0, ymm8
        vpxor   ymm1, ymm1, ymm9
        vpxor   ymm2, ymm2, ymm10
        vpxor   ymm3, ymm3, ymm11
        vpxor   ymm4, ymm4, ymm12
        vpxor   ymm5, ymm5, ymm13
        vpxor   ymm6, ymm6, ymm14
        vpxor   ymm7, ymm7, ymm15
        movzx   eax, byte ptr [rbp+78H]
        jne     innerloop8
        mov     rbx, qword ptr [rbp+90H]
        vunpcklps ymm8, ymm0, ymm1
        vunpcklps ymm9, ymm2, ymm3
        vunpckhps ymm10, ymm0, ymm1
        vunpcklps ymm11, ymm4, ymm5
        vunpcklps ymm0, ymm6, ymm7
        vshufps ymm12, ymm8, ymm9, 78
        vblendps ymm1, ymm8, ymm12, 0CCH
        vshufps ymm8, ymm11, ymm0, 78
        vunpckhps ymm13, ymm2, ymm3
        vblendps ymm2, ymm11, ymm8, 0CCH
        vblendps ymm3, ymm12, ymm9, 0CCH
        vperm2f128 ymm12, ymm1, ymm2, 20H
        vmovups ymmword ptr [rbx], ymm12
        vunpckhps ymm14, ymm4, ymm5
        vblendps ymm4, ymm8, ymm0, 0CCH
        vunpckhps ymm15, ymm6, ymm7
        vperm2f128 ymm7, ymm3, ymm4, 20H
        vmovups ymmword ptr [rbx+20H], ymm7
        vshufps ymm5, ymm10, ymm13, 78
        vblendps ymm6, ymm5, ymm13, 0CCH
        vshufps ymm13, ymm14, ymm15, 78
        vblendps ymm10, ymm10, ymm5, 0CCH
        vblendps ymm14, ymm14, ymm13, 0CCH
        vperm2f128 ymm8, ymm10, ymm14, 20H
        vmovups ymmword ptr [rbx+40H], ymm8
        vblendps ymm15, ymm13, ymm15, 0CCH
        vperm2f128 ymm13, ymm6, ymm15, 20H
        vmovups ymmword ptr [rbx+60H], ymm13
        vperm2f128 ymm9, ymm1, ymm2, 31H
        vperm2f128 ymm11, ymm3, ymm4, 31H
        vmovups ymmword ptr [rbx+80H], ymm9
        vperm2f128 ymm14, ymm10, ymm14, 31H
        vperm2f128 ymm15, ymm6, ymm15, 31H
        vmovups ymmword ptr [rbx+0A0H], ymm11
        vmovups ymmword ptr [rbx+0C0H], ymm14
        vmovups ymmword ptr [rbx+0E0H], ymm15
        vmovdqa ymm0, ymmword ptr [rsp]
        vmovdqa ymm2, ymmword ptr [rsp+40H]
        vmovdqa32 ymm0 {k1}, ymmword ptr [rsp+1H*20H]
        vmovdqa32 ymm2 {k1}, ymmword ptr [rsp+3H*20H]
        vmovdqa ymmword ptr [rsp], ymm0
        vmovdqa ymmword ptr [rsp+40H], ymm2
        add     rbx, 256
        mov     qword ptr [rbp+90H], rbx
        add     rdi, 64
        sub     rsi, 8
final7blocks:
        mov     rbx, qword ptr [rbp+90H]
        mov     r15, qword ptr [rsp+80H]
        movzx   r13, byte ptr [rbp+78H]
        movzx   r12, byte ptr [rbp+88H]
        test    esi, 4H
        je      final3blocks
        vbroadcasti32x4 zmm0, xmmword ptr [rcx]
        vbroadcasti32x4 zmm1, xmmword ptr [rcx+1H*10H]
        vmovdqa xmm12, xmmword ptr [rsp]
        vmovdqa xmm13, xmmword ptr [rsp+40H]
        vpunpckldq xmm14, xmm12, xmm13
        vpunpckhdq xmm15, xmm12, xmm13
        vpermq  ymm14, ymm14, 0DCH
        vpermq  ymm15, ymm15, 0DCH
        vpbroadcastd zmm12, dword ptr [BLAKE3_BLOCK_LEN]
        vinserti64x4 zmm13, zmm14, ymm15, 01H
        mov     eax, 17476
        kmovw   k2, eax
        vpblendmd zmm13 {k2}, zmm13, zmm12
        vbroadcasti32x4 zmm15, xmmword ptr [BLAKE3_IV]
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        mov     r10, qword ptr [rdi+10H]
        mov     r11, qword ptr [rdi+18H]
        mov     eax, 43690
        kmovw   k3, eax
        mov     eax, 34952
        kmovw   k4, eax
        movzx   eax, byte ptr [rbp+80H]
        or      eax, r13d
        xor     edx, edx
ALIGN   16
innerloop4:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        mov     dword ptr [rsp+88H], eax
        vmovdqa32 zmm2, zmm15
        vpbroadcastd zmm8, dword ptr [rsp+22H*4H]
        vpblendmd zmm3 {k4}, zmm13, zmm8
        vmovups zmm8, zmmword ptr [r8+rdx-1H*40H]
        vinserti32x4 zmm8, zmm8, xmmword ptr [r9+rdx-4H*10H], 01H
        vinserti32x4 zmm8, zmm8, xmmword ptr [r10+rdx-4H*10H], 02H
        vinserti32x4 zmm8, zmm8, xmmword ptr [r11+rdx-4H*10H], 03H
        vmovups zmm9, zmmword ptr [r8+rdx-30H]
        vinserti32x4 zmm9, zmm9, xmmword ptr [r9+rdx-3H*10H], 01H
        vinserti32x4 zmm9, zmm9, xmmword ptr [r10+rdx-3H*10H], 02H
        vinserti32x4 zmm9, zmm9, xmmword ptr [r11+rdx-3H*10H], 03H
        vshufps zmm4, zmm8, zmm9, 136
        vshufps zmm5, zmm8, zmm9, 221
        vmovups zmm8, zmmword ptr [r8+rdx-20H]
        vinserti32x4 zmm8, zmm8, xmmword ptr [r9+rdx-2H*10H], 01H
        vinserti32x4 zmm8, zmm8, xmmword ptr [r10+rdx-2H*10H], 02H
        vinserti32x4 zmm8, zmm8, xmmword ptr [r11+rdx-2H*10H], 03H
        vmovups zmm9, zmmword ptr [r8+rdx-10H]
        vinserti32x4 zmm9, zmm9, xmmword ptr [r9+rdx-1H*10H], 01H
        vinserti32x4 zmm9, zmm9, xmmword ptr [r10+rdx-1H*10H], 02H
        vinserti32x4 zmm9, zmm9, xmmword ptr [r11+rdx-1H*10H], 03H
        vshufps zmm6, zmm8, zmm9, 136
        vshufps zmm7, zmm8, zmm9, 221
        vpshufd zmm6, zmm6, 93H
        vpshufd zmm7, zmm7, 93H
        mov     al, 7
roundloop4:
        vpaddd  zmm0, zmm0, zmm4
        vpaddd  zmm0, zmm0, zmm1
        vpxord  zmm3, zmm3, zmm0
        vprord  zmm3, zmm3, 16
        vpaddd  zmm2, zmm2, zmm3
        vpxord  zmm1, zmm1, zmm2
        vprord  zmm1, zmm1, 12
        vpaddd  zmm0, zmm0, zmm5
        vpaddd  zmm0, zmm0, zmm1
        vpxord  zmm3, zmm3, zmm0
        vprord  zmm3, zmm3, 8
        vpaddd  zmm2, zmm2, zmm3
        vpxord  zmm1, zmm1, zmm2
        vprord  zmm1, zmm1, 7
        vpshufd zmm0, zmm0, 93H
        vpshufd zmm3, zmm3, 4EH
        vpshufd zmm2, zmm2, 39H
        vpaddd  zmm0, zmm0, zmm6
        vpaddd  zmm0, zmm0, zmm1
        vpxord  zmm3, zmm3, zmm0
        vprord  zmm3, zmm3, 16
        vpaddd  zmm2, zmm2, zmm3
        vpxord  zmm1, zmm1, zmm2
        vprord  zmm1, zmm1, 12
        vpaddd  zmm0, zmm0, zmm7
        vpaddd  zmm0, zmm0, zmm1
        vpxord  zmm3, zmm3, zmm0
        vprord  zmm3, zmm3, 8
        vpaddd  zmm2, zmm2, zmm3
        vpxord  zmm1, zmm1, zmm2
        vprord  zmm1, zmm1, 7
        vpshufd zmm0, zmm0, 39H
        vpshufd zmm3, zmm3, 4EH
        vpshufd zmm2, zmm2, 93H
        dec     al
        jz      endroundloop4
        vshufps zmm8, zmm4, zmm5, 214
        vpshufd zmm9, zmm4, 0FH
        vpshufd zmm4, zmm8, 39H
        vshufps zmm8, zmm6, zmm7, 250
        vpblendmd zmm9 {k3}, zmm9, zmm8
        vpunpcklqdq zmm8, zmm7, zmm5
        vpblendmd zmm8 {k4}, zmm8, zmm6
        vpshufd zmm8, zmm8, 78H
        vpunpckhdq zmm5, zmm5, zmm7
        vpunpckldq zmm6, zmm6, zmm5
        vpshufd zmm7, zmm6, 1EH
        vmovdqa32 zmm5, zmm9
        vmovdqa32 zmm6, zmm8
        jmp     roundloop4
endroundloop4:
        vpxord  zmm0, zmm0, zmm2
        vpxord  zmm1, zmm1, zmm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     innerloop4
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+10H], xmm1
        vextracti128 xmmword ptr [rbx+20H], ymm0, 01H
        vextracti128 xmmword ptr [rbx+30H], ymm1, 01H
        vextracti32x4 xmmword ptr [rbx+4H*10H], zmm0, 02H
        vextracti32x4 xmmword ptr [rbx+5H*10H], zmm1, 02H
        vextracti32x4 xmmword ptr [rbx+6H*10H], zmm0, 03H
        vextracti32x4 xmmword ptr [rbx+7H*10H], zmm1, 03H
        vmovdqa xmm0, xmmword ptr [rsp]
        vmovdqa xmm2, xmmword ptr [rsp+40H]
        vmovdqa32 xmm0 {k1}, xmmword ptr [rsp+1H*10H]
        vmovdqa32 xmm2 {k1}, xmmword ptr [rsp+5H*10H]
        vmovdqa xmmword ptr [rsp], xmm0
        vmovdqa xmmword ptr [rsp+40H], xmm2
        add     rbx, 128
        add     rdi, 32
        sub     rsi, 4
final3blocks:
        test    esi, 2H
        je      final1block
        vbroadcasti128 ymm0, xmmword ptr [rcx]
        vbroadcasti128 ymm1, xmmword ptr [rcx+10H]
        vmovd   xmm13, dword ptr [rsp]
        vpinsrd xmm13, xmm13, dword ptr [rsp+40H], 1
        vpinsrd xmm13, xmm13, dword ptr [BLAKE3_BLOCK_LEN], 2
        vmovd   xmm14, dword ptr [rsp+4H]
        vpinsrd xmm14, xmm14, dword ptr [rsp+44H], 1
        vpinsrd xmm14, xmm14, dword ptr [BLAKE3_BLOCK_LEN], 2
        vinserti128 ymm13, ymm13, xmm14, 01H
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        movzx   eax, byte ptr [rbp+80H]
        or      eax, r13d
        xor     edx, edx
ALIGN   16
innerloop2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        mov     dword ptr [rsp+88H], eax
        vbroadcasti128 ymm2, xmmword ptr [BLAKE3_IV]
        vpbroadcastd ymm8, dword ptr [rsp+88H]
        vpblendd ymm3, ymm13, ymm8, 88H
        vmovups ymm8, ymmword ptr [r8+rdx-40H]
        vinsertf128 ymm8, ymm8, xmmword ptr [r9+rdx-40H], 01H
        vmovups ymm9, ymmword ptr [r8+rdx-30H]
        vinsertf128 ymm9, ymm9, xmmword ptr [r9+rdx-30H], 01H
        vshufps ymm4, ymm8, ymm9, 136
        vshufps ymm5, ymm8, ymm9, 221
        vmovups ymm8, ymmword ptr [r8+rdx-20H]
        vinsertf128 ymm8, ymm8, xmmword ptr [r9+rdx-20H], 01H
        vmovups ymm9, ymmword ptr [r8+rdx-10H]
        vinsertf128 ymm9, ymm9, xmmword ptr [r9+rdx-10H], 01H
        vshufps ymm6, ymm8, ymm9, 136
        vshufps ymm7, ymm8, ymm9, 221
        vpshufd ymm6, ymm6, 93H
        vpshufd ymm7, ymm7, 93H
        mov     al, 7
roundloop2:
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm0, ymm0, ymm1
        vpxord  ymm3, ymm3, ymm0
        vprord  ymm3, ymm3, 16
        vpaddd  ymm2, ymm2, ymm3
        vpxord  ymm1, ymm1, ymm2
        vprord  ymm1, ymm1, 12
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm0, ymm0, ymm1
        vpxord  ymm3, ymm3, ymm0
        vprord  ymm3, ymm3, 8
        vpaddd  ymm2, ymm2, ymm3
        vpxord  ymm1, ymm1, ymm2
        vprord  ymm1, ymm1, 7
        vpshufd ymm0, ymm0, 93H
        vpshufd ymm3, ymm3, 4EH
        vpshufd ymm2, ymm2, 39H
        vpaddd  ymm0, ymm0, ymm6
        vpaddd  ymm0, ymm0, ymm1
        vpxord  ymm3, ymm3, ymm0
        vprord  ymm3, ymm3, 16
        vpaddd  ymm2, ymm2, ymm3
        vpxord  ymm1, ymm1, ymm2
        vprord  ymm1, ymm1, 12
        vpaddd  ymm0, ymm0, ymm7
        vpaddd  ymm0, ymm0, ymm1
        vpxord  ymm3, ymm3, ymm0
        vprord  ymm3, ymm3, 8
        vpaddd  ymm2, ymm2, ymm3
        vpxord  ymm1, ymm1, ymm2
        vprord  ymm1, ymm1, 7
        vpshufd ymm0, ymm0, 39H
        vpshufd ymm3, ymm3, 4EH
        vpshufd ymm2, ymm2, 93H
        dec     al
        jz      endroundloop2
        vshufps ymm8, ymm4, ymm5, 214
        vpshufd ymm9, ymm4, 0FH
        vpshufd ymm4, ymm8, 39H
        vshufps ymm8, ymm6, ymm7, 250
        vpblendd ymm9, ymm9, ymm8, 0AAH
        vpunpcklqdq ymm8, ymm7, ymm5
        vpblendd ymm8, ymm8, ymm6, 88H
        vpshufd ymm8, ymm8, 78H
        vpunpckhdq ymm5, ymm5, ymm7
        vpunpckldq ymm6, ymm6, ymm5
        vpshufd ymm7, ymm6, 1EH
        vmovdqa ymm5, ymm9
        vmovdqa ymm6, ymm8
        jmp     roundloop2
endroundloop2:
        vpxor   ymm0, ymm0, ymm2
        vpxor   ymm1, ymm1, ymm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     innerloop2
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+10H], xmm1
        vextracti128 xmmword ptr [rbx+20H], ymm0, 01H
        vextracti128 xmmword ptr [rbx+30H], ymm1, 01H
        vmovdqa xmm0, xmmword ptr [rsp]
        vmovdqa xmm2, xmmword ptr [rsp+40H]
        vmovdqu32 xmm0 {k1}, xmmword ptr [rsp+8H]
        vmovdqu32 xmm2 {k1}, xmmword ptr [rsp+48H]
        vmovdqa xmmword ptr [rsp], xmm0
        vmovdqa xmmword ptr [rsp+40H], xmm2
        add     rbx, 64
        add     rdi, 16
        sub     rsi, 2
final1block:
        test    esi, 1H
        je      unwind
        vmovdqu xmm0, xmmword ptr [rcx]
        vmovdqu xmm1, xmmword ptr [rcx+10H]
        vmovd   xmm14, dword ptr [rsp]
        vpinsrd xmm14, xmm14, dword ptr [rsp+40H], 1
        vpinsrd xmm14, xmm14, dword ptr [BLAKE3_BLOCK_LEN], 2
        vmovdqa xmm15, xmmword ptr [BLAKE3_IV]
        mov     r8, qword ptr [rdi]
        movzx   eax, byte ptr [rbp+80H]
        or      eax, r13d
        xor     edx, edx
ALIGN   16
innerloop1:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        vpinsrd xmm3, xmm14, eax, 3
        vmovdqa xmm2, xmm15
        vmovups xmm8, xmmword ptr [r8+rdx-40H]
        vmovups xmm9, xmmword ptr [r8+rdx-30H]
        vshufps xmm4, xmm8, xmm9, 136
        vshufps xmm5, xmm8, xmm9, 221
        vmovups xmm8, xmmword ptr [r8+rdx-20H]
        vmovups xmm9, xmmword ptr [r8+rdx-10H]
        vshufps xmm6, xmm8, xmm9, 136
        vshufps xmm7, xmm8, xmm9, 221
        vpshufd xmm6, xmm6, 93H
        vpshufd xmm7, xmm7, 93H
        mov     al, 7
roundloop1:
        vpaddd  xmm0, xmm0, xmm4
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 16
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 12
        vpaddd  xmm0, xmm0, xmm5
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 8
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 7
        vpshufd xmm0, xmm0, 93H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 39H
        vpaddd  xmm0, xmm0, xmm6
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 16
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 12
        vpaddd  xmm0, xmm0, xmm7
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 8
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 7
        vpshufd xmm0, xmm0, 39H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 93H
        dec     al
        jz      endroundloop1
        vshufps xmm8, xmm4, xmm5, 214
        vpshufd xmm9, xmm4, 0FH
        vpshufd xmm4, xmm8, 39H
        vshufps xmm8, xmm6, xmm7, 250
        vpblendd xmm9, xmm9, xmm8, 0AAH
        vpunpcklqdq xmm8, xmm7, xmm5
        vpblendd xmm8, xmm8, xmm6, 88H
        vpshufd xmm8, xmm8, 78H
        vpunpckhdq xmm5, xmm5, xmm7
        vpunpckldq xmm6, xmm6, xmm5
        vpshufd xmm7, xmm6, 1EH
        vmovdqa xmm5, xmm9
        vmovdqa xmm6, xmm8
        jmp     roundloop1
endroundloop1:
        vpxor   xmm0, xmm0, xmm2
        vpxor   xmm1, xmm1, xmm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     innerloop1
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+10H], xmm1
        jmp     unwind

_llvm_blake3_hash_many_avx512 ENDP
llvm_blake3_hash_many_avx512 ENDP

ALIGN 16
llvm_blake3_compress_in_place_avx512 PROC
_llvm_blake3_compress_in_place_avx512 PROC
        sub     rsp, 72
        vmovdqa xmmword ptr [rsp], xmm6
        vmovdqa xmmword ptr [rsp+10H], xmm7
        vmovdqa xmmword ptr [rsp+20H], xmm8
        vmovdqa xmmword ptr [rsp+30H], xmm9
        vmovdqu xmm0, xmmword ptr [rcx]
        vmovdqu xmm1, xmmword ptr [rcx+10H]
        movzx   eax, byte ptr [rsp+70H]
        movzx   r8d, r8b
        shl     rax, 32
        add     r8, rax
        vmovq   xmm3, r9
        vmovq   xmm4, r8
        vpunpcklqdq xmm3, xmm3, xmm4
        vmovaps xmm2, xmmword ptr [BLAKE3_IV]
        vmovups xmm8, xmmword ptr [rdx]
        vmovups xmm9, xmmword ptr [rdx+10H]
        vshufps xmm4, xmm8, xmm9, 136
        vshufps xmm5, xmm8, xmm9, 221
        vmovups xmm8, xmmword ptr [rdx+20H]
        vmovups xmm9, xmmword ptr [rdx+30H]
        vshufps xmm6, xmm8, xmm9, 136
        vshufps xmm7, xmm8, xmm9, 221
        vpshufd xmm6, xmm6, 93H
        vpshufd xmm7, xmm7, 93H
        mov     al, 7
@@:
        vpaddd  xmm0, xmm0, xmm4
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 16
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 12
        vpaddd  xmm0, xmm0, xmm5
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 8
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 7
        vpshufd xmm0, xmm0, 93H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 39H
        vpaddd  xmm0, xmm0, xmm6
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 16
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 12
        vpaddd  xmm0, xmm0, xmm7
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 8
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 7
        vpshufd xmm0, xmm0, 39H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 93H
        dec     al
        jz      @F
        vshufps xmm8, xmm4, xmm5, 214
        vpshufd xmm9, xmm4, 0FH
        vpshufd xmm4, xmm8, 39H
        vshufps xmm8, xmm6, xmm7, 250
        vpblendd xmm9, xmm9, xmm8, 0AAH
        vpunpcklqdq xmm8, xmm7, xmm5
        vpblendd xmm8, xmm8, xmm6, 88H
        vpshufd xmm8, xmm8, 78H
        vpunpckhdq xmm5, xmm5, xmm7
        vpunpckldq xmm6, xmm6, xmm5
        vpshufd xmm7, xmm6, 1EH
        vmovdqa xmm5, xmm9
        vmovdqa xmm6, xmm8
        jmp     @B
@@:
        vpxor   xmm0, xmm0, xmm2
        vpxor   xmm1, xmm1, xmm3
        vmovdqu xmmword ptr [rcx], xmm0
        vmovdqu xmmword ptr [rcx+10H], xmm1
        vmovdqa xmm6, xmmword ptr [rsp]
        vmovdqa xmm7, xmmword ptr [rsp+10H]
        vmovdqa xmm8, xmmword ptr [rsp+20H]
        vmovdqa xmm9, xmmword ptr [rsp+30H]
        add     rsp, 72
        ret
_llvm_blake3_compress_in_place_avx512 ENDP
llvm_blake3_compress_in_place_avx512 ENDP

ALIGN 16
llvm_blake3_compress_xof_avx512 PROC
_llvm_blake3_compress_xof_avx512 PROC
        sub     rsp, 72
        vmovdqa xmmword ptr [rsp], xmm6
        vmovdqa xmmword ptr [rsp+10H], xmm7
        vmovdqa xmmword ptr [rsp+20H], xmm8
        vmovdqa xmmword ptr [rsp+30H], xmm9
        vmovdqu xmm0, xmmword ptr [rcx]
        vmovdqu xmm1, xmmword ptr [rcx+10H]
        movzx   eax, byte ptr [rsp+70H]
        movzx   r8d, r8b
        mov     r10, qword ptr [rsp+78H]
        shl     rax, 32
        add     r8, rax
        vmovq   xmm3, r9
        vmovq   xmm4, r8
        vpunpcklqdq xmm3, xmm3, xmm4
        vmovaps xmm2, xmmword ptr [BLAKE3_IV]
        vmovups xmm8, xmmword ptr [rdx]
        vmovups xmm9, xmmword ptr [rdx+10H]
        vshufps xmm4, xmm8, xmm9, 136
        vshufps xmm5, xmm8, xmm9, 221
        vmovups xmm8, xmmword ptr [rdx+20H]
        vmovups xmm9, xmmword ptr [rdx+30H]
        vshufps xmm6, xmm8, xmm9, 136
        vshufps xmm7, xmm8, xmm9, 221
        vpshufd xmm6, xmm6, 93H
        vpshufd xmm7, xmm7, 93H
        mov     al, 7
@@:
        vpaddd  xmm0, xmm0, xmm4
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 16
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 12
        vpaddd  xmm0, xmm0, xmm5
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 8
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 7
        vpshufd xmm0, xmm0, 93H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 39H
        vpaddd  xmm0, xmm0, xmm6
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 16
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 12
        vpaddd  xmm0, xmm0, xmm7
        vpaddd  xmm0, xmm0, xmm1
        vpxord  xmm3, xmm3, xmm0
        vprord  xmm3, xmm3, 8
        vpaddd  xmm2, xmm2, xmm3
        vpxord  xmm1, xmm1, xmm2
        vprord  xmm1, xmm1, 7
        vpshufd xmm0, xmm0, 39H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 93H
        dec     al
        jz      @F
        vshufps xmm8, xmm4, xmm5, 214
        vpshufd xmm9, xmm4, 0FH
        vpshufd xmm4, xmm8, 39H
        vshufps xmm8, xmm6, xmm7, 250
        vpblendd xmm9, xmm9, xmm8, 0AAH
        vpunpcklqdq xmm8, xmm7, xmm5
        vpblendd xmm8, xmm8, xmm6, 88H
        vpshufd xmm8, xmm8, 78H
        vpunpckhdq xmm5, xmm5, xmm7
        vpunpckldq xmm6, xmm6, xmm5
        vpshufd xmm7, xmm6, 1EH
        vmovdqa xmm5, xmm9
        vmovdqa xmm6, xmm8
        jmp     @B
@@:
        vpxor   xmm0, xmm0, xmm2
        vpxor   xmm1, xmm1, xmm3
        vpxor   xmm2, xmm2, xmmword ptr [rcx]
        vpxor   xmm3, xmm3, xmmword ptr [rcx+10H]
        vmovdqu xmmword ptr [r10], xmm0
        vmovdqu xmmword ptr [r10+10H], xmm1
        vmovdqu xmmword ptr [r10+20H], xmm2
        vmovdqu xmmword ptr [r10+30H], xmm3
        vmovdqa xmm6, xmmword ptr [rsp]
        vmovdqa xmm7, xmmword ptr [rsp+10H]
        vmovdqa xmm8, xmmword ptr [rsp+20H]
        vmovdqa xmm9, xmmword ptr [rsp+30H]
        add     rsp, 72
        ret
_llvm_blake3_compress_xof_avx512 ENDP
llvm_blake3_compress_xof_avx512 ENDP

_TEXT ENDS

_RDATA SEGMENT READONLY PAGE ALIAS(".rdata") 'CONST'
ALIGN   64
INDEX0:
        dd    0,  1,  2,  3, 16, 17, 18, 19
        dd    8,  9, 10, 11, 24, 25, 26, 27
INDEX1:
        dd    4,  5,  6,  7, 20, 21, 22, 23
        dd   12, 13, 14, 15, 28, 29, 30, 31
ADD0:
        dd    0,  1,  2,  3,  4,  5,  6,  7
        dd    8,  9, 10, 11, 12, 13, 14, 15
ADD1:   
        dd    1
ADD16:  
        dd   16
BLAKE3_BLOCK_LEN:
        dd   64
ALIGN   64
BLAKE3_IV:
BLAKE3_IV_0:
        dd   06A09E667H
BLAKE3_IV_1:
        dd   0BB67AE85H
BLAKE3_IV_2:
        dd   03C6EF372H
BLAKE3_IV_3:
        dd   0A54FF53AH

_RDATA ENDS
END
