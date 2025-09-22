#if defined(__x86_64__)

#include "llvm_blake3_prefix.h"

#if defined(__ELF__) && !(defined(__sun__) && defined(__svr4__))
.section .note.GNU-stack,"",%progbits
#endif

#if defined(__ELF__) && defined(__CET__) && defined(__has_include)
#if __has_include(<cet.h>)
#include <cet.h>
#endif
#endif

#if !defined(_CET_ENDBR)
#define _CET_ENDBR
#endif

#ifdef __APPLE__
#define HIDDEN .private_extern
#else
#define HIDDEN .hidden
#endif

.intel_syntax noprefix
HIDDEN _blake3_hash_many_avx512
HIDDEN blake3_hash_many_avx512
HIDDEN blake3_compress_in_place_avx512
HIDDEN _blake3_compress_in_place_avx512
HIDDEN blake3_compress_xof_avx512
HIDDEN _blake3_compress_xof_avx512
.global _blake3_hash_many_avx512
.global blake3_hash_many_avx512
.global blake3_compress_in_place_avx512
.global _blake3_compress_in_place_avx512
.global blake3_compress_xof_avx512
.global _blake3_compress_xof_avx512

#ifdef __APPLE__
.text
#else
.section .text
#endif
.p2align  6
_blake3_hash_many_avx512:
blake3_hash_many_avx512:
        _CET_ENDBR
        push    r15
        push    r14
        push    r13
        push    r12
        push    rbx
        push    rbp
        mov     rbp, rsp
        sub     rsp, 144
        and     rsp, 0xFFFFFFFFFFFFFFC0
        neg     r9
        kmovw   k1, r9d
        vmovd   xmm0, r8d
        vpbroadcastd ymm0, xmm0
        shr     r8, 32
        vmovd   xmm1, r8d
        vpbroadcastd ymm1, xmm1
        vmovdqa ymm4, ymm1
        vmovdqa ymm5, ymm1
        vpaddd  ymm2, ymm0, ymmword ptr [ADD0+rip]
        vpaddd  ymm3, ymm0, ymmword ptr [ADD0+32+rip]
        vpcmpltud k2, ymm2, ymm0
        vpcmpltud k3, ymm3, ymm0
        vpaddd  ymm4 {k2}, ymm4, dword ptr [ADD1+rip] {1to8}
        vpaddd  ymm5 {k3}, ymm5, dword ptr [ADD1+rip] {1to8}
        knotw   k2, k1
        vmovdqa32 ymm2 {k2}, ymm0
        vmovdqa32 ymm3 {k2}, ymm0
        vmovdqa32 ymm4 {k2}, ymm1
        vmovdqa32 ymm5 {k2}, ymm1
        vmovdqa ymmword ptr [rsp], ymm2
        vmovdqa ymmword ptr [rsp+0x1*0x20], ymm3
        vmovdqa ymmword ptr [rsp+0x2*0x20], ymm4
        vmovdqa ymmword ptr [rsp+0x3*0x20], ymm5
        shl     rdx, 6
        mov     qword ptr [rsp+0x80], rdx
        cmp     rsi, 16
        jc      3f
2:
        vpbroadcastd zmm0, dword ptr [rcx]
        vpbroadcastd zmm1, dword ptr [rcx+0x1*0x4]
        vpbroadcastd zmm2, dword ptr [rcx+0x2*0x4]
        vpbroadcastd zmm3, dword ptr [rcx+0x3*0x4]
        vpbroadcastd zmm4, dword ptr [rcx+0x4*0x4]
        vpbroadcastd zmm5, dword ptr [rcx+0x5*0x4]
        vpbroadcastd zmm6, dword ptr [rcx+0x6*0x4]
        vpbroadcastd zmm7, dword ptr [rcx+0x7*0x4]
        movzx   eax, byte ptr [rbp+0x38]
        movzx   ebx, byte ptr [rbp+0x40]
        or      eax, ebx
        xor     edx, edx
.p2align 5
9:
        movzx   ebx, byte ptr [rbp+0x48]
        or      ebx, eax
        add     rdx, 64
        cmp     rdx, qword ptr [rsp+0x80]
        cmove   eax, ebx
        mov     dword ptr [rsp+0x88], eax
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        mov     r10, qword ptr [rdi+0x10]
        mov     r11, qword ptr [rdi+0x18]
        mov     r12, qword ptr [rdi+0x40]
        mov     r13, qword ptr [rdi+0x48]
        mov     r14, qword ptr [rdi+0x50]
        mov     r15, qword ptr [rdi+0x58]
        vmovdqu32 ymm16, ymmword ptr [rdx+r8-0x2*0x20]
        vinserti64x4 zmm16, zmm16, ymmword ptr [rdx+r12-0x2*0x20], 0x01
        vmovdqu32 ymm17, ymmword ptr [rdx+r9-0x2*0x20]
        vinserti64x4 zmm17, zmm17, ymmword ptr [rdx+r13-0x2*0x20], 0x01
        vpunpcklqdq zmm8, zmm16, zmm17
        vpunpckhqdq zmm9, zmm16, zmm17
        vmovdqu32 ymm18, ymmword ptr [rdx+r10-0x2*0x20]
        vinserti64x4 zmm18, zmm18, ymmword ptr [rdx+r14-0x2*0x20], 0x01
        vmovdqu32 ymm19, ymmword ptr [rdx+r11-0x2*0x20]
        vinserti64x4 zmm19, zmm19, ymmword ptr [rdx+r15-0x2*0x20], 0x01
        vpunpcklqdq zmm10, zmm18, zmm19
        vpunpckhqdq zmm11, zmm18, zmm19
        mov     r8, qword ptr [rdi+0x20]
        mov     r9, qword ptr [rdi+0x28]
        mov     r10, qword ptr [rdi+0x30]
        mov     r11, qword ptr [rdi+0x38]
        mov     r12, qword ptr [rdi+0x60]
        mov     r13, qword ptr [rdi+0x68]
        mov     r14, qword ptr [rdi+0x70]
        mov     r15, qword ptr [rdi+0x78]
        vmovdqu32 ymm16, ymmword ptr [rdx+r8-0x2*0x20]
        vinserti64x4 zmm16, zmm16, ymmword ptr [rdx+r12-0x2*0x20], 0x01
        vmovdqu32 ymm17, ymmword ptr [rdx+r9-0x2*0x20]
        vinserti64x4 zmm17, zmm17, ymmword ptr [rdx+r13-0x2*0x20], 0x01
        vpunpcklqdq zmm12, zmm16, zmm17
        vpunpckhqdq zmm13, zmm16, zmm17
        vmovdqu32 ymm18, ymmword ptr [rdx+r10-0x2*0x20]
        vinserti64x4 zmm18, zmm18, ymmword ptr [rdx+r14-0x2*0x20], 0x01
        vmovdqu32 ymm19, ymmword ptr [rdx+r11-0x2*0x20]
        vinserti64x4 zmm19, zmm19, ymmword ptr [rdx+r15-0x2*0x20], 0x01
        vpunpcklqdq zmm14, zmm18, zmm19
        vpunpckhqdq zmm15, zmm18, zmm19
        vmovdqa32 zmm27, zmmword ptr [INDEX0+rip]
        vmovdqa32 zmm31, zmmword ptr [INDEX1+rip]
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
        mov     r9, qword ptr [rdi+0x8]
        mov     r10, qword ptr [rdi+0x10]
        mov     r11, qword ptr [rdi+0x18]
        mov     r12, qword ptr [rdi+0x40]
        mov     r13, qword ptr [rdi+0x48]
        mov     r14, qword ptr [rdi+0x50]
        mov     r15, qword ptr [rdi+0x58]
        vmovdqu32 ymm24, ymmword ptr [r8+rdx-0x1*0x20]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r12+rdx-0x1*0x20], 0x01
        vmovdqu32 ymm25, ymmword ptr [r9+rdx-0x1*0x20]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r13+rdx-0x1*0x20], 0x01
        vpunpcklqdq zmm8, zmm24, zmm25
        vpunpckhqdq zmm9, zmm24, zmm25
        vmovdqu32 ymm24, ymmword ptr [r10+rdx-0x1*0x20]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r14+rdx-0x1*0x20], 0x01
        vmovdqu32 ymm25, ymmword ptr [r11+rdx-0x1*0x20]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r15+rdx-0x1*0x20], 0x01
        vpunpcklqdq zmm10, zmm24, zmm25
        vpunpckhqdq zmm11, zmm24, zmm25
        prefetcht0 [r8+rdx+0x80]
        prefetcht0 [r12+rdx+0x80]
        prefetcht0 [r9+rdx+0x80]
        prefetcht0 [r13+rdx+0x80]
        prefetcht0 [r10+rdx+0x80]
        prefetcht0 [r14+rdx+0x80]
        prefetcht0 [r11+rdx+0x80]
        prefetcht0 [r15+rdx+0x80]
        mov     r8, qword ptr [rdi+0x20]
        mov     r9, qword ptr [rdi+0x28]
        mov     r10, qword ptr [rdi+0x30]
        mov     r11, qword ptr [rdi+0x38]
        mov     r12, qword ptr [rdi+0x60]
        mov     r13, qword ptr [rdi+0x68]
        mov     r14, qword ptr [rdi+0x70]
        mov     r15, qword ptr [rdi+0x78]
        vmovdqu32 ymm24, ymmword ptr [r8+rdx-0x1*0x20]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r12+rdx-0x1*0x20], 0x01
        vmovdqu32 ymm25, ymmword ptr [r9+rdx-0x1*0x20]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r13+rdx-0x1*0x20], 0x01
        vpunpcklqdq zmm12, zmm24, zmm25
        vpunpckhqdq zmm13, zmm24, zmm25
        vmovdqu32 ymm24, ymmword ptr [r10+rdx-0x1*0x20]
        vinserti64x4 zmm24, zmm24, ymmword ptr [r14+rdx-0x1*0x20], 0x01
        vmovdqu32 ymm25, ymmword ptr [r11+rdx-0x1*0x20]
        vinserti64x4 zmm25, zmm25, ymmword ptr [r15+rdx-0x1*0x20], 0x01
        vpunpcklqdq zmm14, zmm24, zmm25
        vpunpckhqdq zmm15, zmm24, zmm25
        prefetcht0 [r8+rdx+0x80]
        prefetcht0 [r12+rdx+0x80]
        prefetcht0 [r9+rdx+0x80]
        prefetcht0 [r13+rdx+0x80]
        prefetcht0 [r10+rdx+0x80]
        prefetcht0 [r14+rdx+0x80]
        prefetcht0 [r11+rdx+0x80]
        prefetcht0 [r15+rdx+0x80]
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
        vpbroadcastd zmm8, dword ptr [BLAKE3_IV_0+rip]
        vpbroadcastd zmm9, dword ptr [BLAKE3_IV_1+rip]
        vpbroadcastd zmm10, dword ptr [BLAKE3_IV_2+rip]
        vpbroadcastd zmm11, dword ptr [BLAKE3_IV_3+rip]
        vmovdqa32 zmm12, zmmword ptr [rsp]
        vmovdqa32 zmm13, zmmword ptr [rsp+0x1*0x40]
        vpbroadcastd zmm14, dword ptr [BLAKE3_BLOCK_LEN+rip]
        vpbroadcastd zmm15, dword ptr [rsp+0x22*0x4]
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
        movzx   eax, byte ptr [rbp+0x38]
        jne     9b
        mov     rbx, qword ptr [rbp+0x50]
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
        vshufi32x4 zmm16, zmm0, zmm4, 0x88
        vshufi32x4 zmm17, zmm1, zmm5, 0x88
        vshufi32x4 zmm18, zmm2, zmm6, 0x88
        vshufi32x4 zmm19, zmm3, zmm7, 0x88
        vshufi32x4 zmm20, zmm0, zmm4, 0xDD
        vshufi32x4 zmm21, zmm1, zmm5, 0xDD
        vshufi32x4 zmm22, zmm2, zmm6, 0xDD
        vshufi32x4 zmm23, zmm3, zmm7, 0xDD
        vshufi32x4 zmm0, zmm16, zmm17, 0x88
        vshufi32x4 zmm1, zmm18, zmm19, 0x88
        vshufi32x4 zmm2, zmm20, zmm21, 0x88
        vshufi32x4 zmm3, zmm22, zmm23, 0x88
        vshufi32x4 zmm4, zmm16, zmm17, 0xDD
        vshufi32x4 zmm5, zmm18, zmm19, 0xDD
        vshufi32x4 zmm6, zmm20, zmm21, 0xDD
        vshufi32x4 zmm7, zmm22, zmm23, 0xDD
        vmovdqu32 zmmword ptr [rbx], zmm0
        vmovdqu32 zmmword ptr [rbx+0x1*0x40], zmm1
        vmovdqu32 zmmword ptr [rbx+0x2*0x40], zmm2
        vmovdqu32 zmmword ptr [rbx+0x3*0x40], zmm3
        vmovdqu32 zmmword ptr [rbx+0x4*0x40], zmm4
        vmovdqu32 zmmword ptr [rbx+0x5*0x40], zmm5
        vmovdqu32 zmmword ptr [rbx+0x6*0x40], zmm6
        vmovdqu32 zmmword ptr [rbx+0x7*0x40], zmm7
        vmovdqa32 zmm0, zmmword ptr [rsp]
        vmovdqa32 zmm1, zmmword ptr [rsp+0x1*0x40]
        vmovdqa32 zmm2, zmm0
        vpaddd  zmm2{k1}, zmm0, dword ptr [ADD16+rip] {1to16}
        vpcmpltud k2, zmm2, zmm0
        vpaddd  zmm1 {k2}, zmm1, dword ptr [ADD1+rip] {1to16}
        vmovdqa32 zmmword ptr [rsp], zmm2
        vmovdqa32 zmmword ptr [rsp+0x1*0x40], zmm1
        add     rdi, 128
        add     rbx, 512
        mov     qword ptr [rbp+0x50], rbx
        sub     rsi, 16
        cmp     rsi, 16
        jnc     2b
        test    rsi, rsi
        jnz     3f
4:
        vzeroupper
        mov     rsp, rbp
        pop     rbp
        pop     rbx
        pop     r12
        pop     r13
        pop     r14
        pop     r15
        ret
.p2align 6
3:
        test    esi, 0x8
        je      3f
        vpbroadcastd ymm0, dword ptr [rcx]
        vpbroadcastd ymm1, dword ptr [rcx+0x4]
        vpbroadcastd ymm2, dword ptr [rcx+0x8]
        vpbroadcastd ymm3, dword ptr [rcx+0xC]
        vpbroadcastd ymm4, dword ptr [rcx+0x10]
        vpbroadcastd ymm5, dword ptr [rcx+0x14]
        vpbroadcastd ymm6, dword ptr [rcx+0x18]
        vpbroadcastd ymm7, dword ptr [rcx+0x1C]
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        mov     r10, qword ptr [rdi+0x10]
        mov     r11, qword ptr [rdi+0x18]
        mov     r12, qword ptr [rdi+0x20]
        mov     r13, qword ptr [rdi+0x28]
        mov     r14, qword ptr [rdi+0x30]
        mov     r15, qword ptr [rdi+0x38]
        movzx   eax, byte ptr [rbp+0x38]
        movzx   ebx, byte ptr [rbp+0x40]
        or      eax, ebx
        xor     edx, edx
2:
        movzx   ebx, byte ptr [rbp+0x48]
        or      ebx, eax
        add     rdx, 64
        cmp     rdx, qword ptr [rsp+0x80]
        cmove   eax, ebx
        mov     dword ptr [rsp+0x88], eax
        vmovups xmm8, xmmword ptr [r8+rdx-0x40]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-0x40], 0x01
        vmovups xmm9, xmmword ptr [r9+rdx-0x40]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-0x40], 0x01
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-0x40]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-0x40], 0x01
        vmovups xmm11, xmmword ptr [r11+rdx-0x40]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-0x40], 0x01
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm16, ymm12, ymm14, 136
        vshufps ymm17, ymm12, ymm14, 221
        vshufps ymm18, ymm13, ymm15, 136
        vshufps ymm19, ymm13, ymm15, 221
        vmovups xmm8, xmmword ptr [r8+rdx-0x30]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-0x30], 0x01
        vmovups xmm9, xmmword ptr [r9+rdx-0x30]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-0x30], 0x01
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-0x30]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-0x30], 0x01
        vmovups xmm11, xmmword ptr [r11+rdx-0x30]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-0x30], 0x01
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm20, ymm12, ymm14, 136
        vshufps ymm21, ymm12, ymm14, 221
        vshufps ymm22, ymm13, ymm15, 136
        vshufps ymm23, ymm13, ymm15, 221
        vmovups xmm8, xmmword ptr [r8+rdx-0x20]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-0x20], 0x01
        vmovups xmm9, xmmword ptr [r9+rdx-0x20]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-0x20], 0x01
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-0x20]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-0x20], 0x01
        vmovups xmm11, xmmword ptr [r11+rdx-0x20]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-0x20], 0x01
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm24, ymm12, ymm14, 136
        vshufps ymm25, ymm12, ymm14, 221
        vshufps ymm26, ymm13, ymm15, 136
        vshufps ymm27, ymm13, ymm15, 221
        vmovups xmm8, xmmword ptr [r8+rdx-0x10]
        vinsertf128 ymm8, ymm8, xmmword ptr [r12+rdx-0x10], 0x01
        vmovups xmm9, xmmword ptr [r9+rdx-0x10]
        vinsertf128 ymm9, ymm9, xmmword ptr [r13+rdx-0x10], 0x01
        vunpcklpd ymm12, ymm8, ymm9
        vunpckhpd ymm13, ymm8, ymm9
        vmovups xmm10, xmmword ptr [r10+rdx-0x10]
        vinsertf128 ymm10, ymm10, xmmword ptr [r14+rdx-0x10], 0x01
        vmovups xmm11, xmmword ptr [r11+rdx-0x10]
        vinsertf128 ymm11, ymm11, xmmword ptr [r15+rdx-0x10], 0x01
        vunpcklpd ymm14, ymm10, ymm11
        vunpckhpd ymm15, ymm10, ymm11
        vshufps ymm28, ymm12, ymm14, 136
        vshufps ymm29, ymm12, ymm14, 221
        vshufps ymm30, ymm13, ymm15, 136
        vshufps ymm31, ymm13, ymm15, 221
        vpbroadcastd ymm8, dword ptr [BLAKE3_IV_0+rip]
        vpbroadcastd ymm9, dword ptr [BLAKE3_IV_1+rip]
        vpbroadcastd ymm10, dword ptr [BLAKE3_IV_2+rip]
        vpbroadcastd ymm11, dword ptr [BLAKE3_IV_3+rip]
        vmovdqa ymm12, ymmword ptr [rsp]
        vmovdqa ymm13, ymmword ptr [rsp+0x40]
        vpbroadcastd ymm14, dword ptr [BLAKE3_BLOCK_LEN+rip]
        vpbroadcastd ymm15, dword ptr [rsp+0x88]
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
        movzx   eax, byte ptr [rbp+0x38]
        jne     2b
        mov     rbx, qword ptr [rbp+0x50]
        vunpcklps ymm8, ymm0, ymm1
        vunpcklps ymm9, ymm2, ymm3
        vunpckhps ymm10, ymm0, ymm1
        vunpcklps ymm11, ymm4, ymm5
        vunpcklps ymm0, ymm6, ymm7
        vshufps ymm12, ymm8, ymm9, 78
        vblendps ymm1, ymm8, ymm12, 0xCC
        vshufps ymm8, ymm11, ymm0, 78
        vunpckhps ymm13, ymm2, ymm3
        vblendps ymm2, ymm11, ymm8, 0xCC
        vblendps ymm3, ymm12, ymm9, 0xCC
        vperm2f128 ymm12, ymm1, ymm2, 0x20
        vmovups ymmword ptr [rbx], ymm12
        vunpckhps ymm14, ymm4, ymm5
        vblendps ymm4, ymm8, ymm0, 0xCC
        vunpckhps ymm15, ymm6, ymm7
        vperm2f128 ymm7, ymm3, ymm4, 0x20
        vmovups ymmword ptr [rbx+0x20], ymm7
        vshufps ymm5, ymm10, ymm13, 78
        vblendps ymm6, ymm5, ymm13, 0xCC
        vshufps ymm13, ymm14, ymm15, 78
        vblendps ymm10, ymm10, ymm5, 0xCC
        vblendps ymm14, ymm14, ymm13, 0xCC
        vperm2f128 ymm8, ymm10, ymm14, 0x20
        vmovups ymmword ptr [rbx+0x40], ymm8
        vblendps ymm15, ymm13, ymm15, 0xCC
        vperm2f128 ymm13, ymm6, ymm15, 0x20
        vmovups ymmword ptr [rbx+0x60], ymm13
        vperm2f128 ymm9, ymm1, ymm2, 0x31
        vperm2f128 ymm11, ymm3, ymm4, 0x31
        vmovups ymmword ptr [rbx+0x80], ymm9
        vperm2f128 ymm14, ymm10, ymm14, 0x31
        vperm2f128 ymm15, ymm6, ymm15, 0x31
        vmovups ymmword ptr [rbx+0xA0], ymm11
        vmovups ymmword ptr [rbx+0xC0], ymm14
        vmovups ymmword ptr [rbx+0xE0], ymm15
        vmovdqa ymm0, ymmword ptr [rsp]
        vmovdqa ymm2, ymmword ptr [rsp+0x2*0x20]
        vmovdqa32 ymm0 {k1}, ymmword ptr [rsp+0x1*0x20]
        vmovdqa32 ymm2 {k1}, ymmword ptr [rsp+0x3*0x20]
        vmovdqa ymmword ptr [rsp], ymm0
        vmovdqa ymmword ptr [rsp+0x2*0x20], ymm2
        add     rbx, 256
        mov     qword ptr [rbp+0x50], rbx
        add     rdi, 64
        sub     rsi, 8
3:
        mov     rbx, qword ptr [rbp+0x50]
        mov     r15, qword ptr [rsp+0x80]
        movzx   r13, byte ptr [rbp+0x38]
        movzx   r12, byte ptr [rbp+0x48]
        test    esi, 0x4
        je      3f
        vbroadcasti32x4 zmm0, xmmword ptr [rcx]
        vbroadcasti32x4 zmm1, xmmword ptr [rcx+0x1*0x10]
        vmovdqa xmm12, xmmword ptr [rsp]
        vmovdqa xmm13, xmmword ptr [rsp+0x4*0x10]
        vpunpckldq xmm14, xmm12, xmm13
        vpunpckhdq xmm15, xmm12, xmm13
        vpermq  ymm14, ymm14, 0xDC
        vpermq  ymm15, ymm15, 0xDC
        vpbroadcastd zmm12, dword ptr [BLAKE3_BLOCK_LEN+rip]
        vinserti64x4 zmm13, zmm14, ymm15, 0x01
        mov     eax, 17476
        kmovw   k2, eax
        vpblendmd zmm13 {k2}, zmm13, zmm12
        vbroadcasti32x4 zmm15, xmmword ptr [BLAKE3_IV+rip]
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        mov     r10, qword ptr [rdi+0x10]
        mov     r11, qword ptr [rdi+0x18]
        mov     eax, 43690
        kmovw   k3, eax
        mov     eax, 34952
        kmovw   k4, eax
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
.p2align 5
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        mov     dword ptr [rsp+0x88], eax
        vmovdqa32 zmm2, zmm15
        vpbroadcastd zmm8, dword ptr [rsp+0x22*0x4]
        vpblendmd zmm3 {k4}, zmm13, zmm8
        vmovups zmm8, zmmword ptr [r8+rdx-0x1*0x40]
        vinserti32x4 zmm8, zmm8, xmmword ptr [r9+rdx-0x4*0x10], 0x01
        vinserti32x4 zmm8, zmm8, xmmword ptr [r10+rdx-0x4*0x10], 0x02
        vinserti32x4 zmm8, zmm8, xmmword ptr [r11+rdx-0x4*0x10], 0x03
        vmovups zmm9, zmmword ptr [r8+rdx-0x30]
        vinserti32x4 zmm9, zmm9, xmmword ptr [r9+rdx-0x3*0x10], 0x01
        vinserti32x4 zmm9, zmm9, xmmword ptr [r10+rdx-0x3*0x10], 0x02
        vinserti32x4 zmm9, zmm9, xmmword ptr [r11+rdx-0x3*0x10], 0x03
        vshufps zmm4, zmm8, zmm9, 136
        vshufps zmm5, zmm8, zmm9, 221
        vmovups zmm8, zmmword ptr [r8+rdx-0x20]
        vinserti32x4 zmm8, zmm8, xmmword ptr [r9+rdx-0x2*0x10], 0x01
        vinserti32x4 zmm8, zmm8, xmmword ptr [r10+rdx-0x2*0x10], 0x02
        vinserti32x4 zmm8, zmm8, xmmword ptr [r11+rdx-0x2*0x10], 0x03
        vmovups zmm9, zmmword ptr [r8+rdx-0x10]
        vinserti32x4 zmm9, zmm9, xmmword ptr [r9+rdx-0x1*0x10], 0x01
        vinserti32x4 zmm9, zmm9, xmmword ptr [r10+rdx-0x1*0x10], 0x02
        vinserti32x4 zmm9, zmm9, xmmword ptr [r11+rdx-0x1*0x10], 0x03
        vshufps zmm6, zmm8, zmm9, 136
        vshufps zmm7, zmm8, zmm9, 221
        vpshufd zmm6, zmm6, 0x93
        vpshufd zmm7, zmm7, 0x93
        mov     al, 7
9:
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
        vpshufd zmm0, zmm0, 0x93
        vpshufd zmm3, zmm3, 0x4E
        vpshufd zmm2, zmm2, 0x39
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
        vpshufd zmm0, zmm0, 0x39
        vpshufd zmm3, zmm3, 0x4E
        vpshufd zmm2, zmm2, 0x93
        dec     al
        jz      9f
        vshufps zmm8, zmm4, zmm5, 214
        vpshufd zmm9, zmm4, 0x0F
        vpshufd zmm4, zmm8, 0x39
        vshufps zmm8, zmm6, zmm7, 250
        vpblendmd zmm9 {k3}, zmm9, zmm8
        vpunpcklqdq zmm8, zmm7, zmm5
        vpblendmd zmm8 {k4}, zmm8, zmm6
        vpshufd zmm8, zmm8, 0x78
        vpunpckhdq zmm5, zmm5, zmm7
        vpunpckldq zmm6, zmm6, zmm5
        vpshufd zmm7, zmm6, 0x1E
        vmovdqa32 zmm5, zmm9
        vmovdqa32 zmm6, zmm8
        jmp     9b
9:
        vpxord  zmm0, zmm0, zmm2
        vpxord  zmm1, zmm1, zmm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     2b
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+0x10], xmm1
        vextracti128 xmmword ptr [rbx+0x20], ymm0, 0x01
        vextracti128 xmmword ptr [rbx+0x30], ymm1, 0x01
        vextracti32x4 xmmword ptr [rbx+0x4*0x10], zmm0, 0x02
        vextracti32x4 xmmword ptr [rbx+0x5*0x10], zmm1, 0x02
        vextracti32x4 xmmword ptr [rbx+0x6*0x10], zmm0, 0x03
        vextracti32x4 xmmword ptr [rbx+0x7*0x10], zmm1, 0x03
        vmovdqa xmm0, xmmword ptr [rsp]
        vmovdqa xmm2, xmmword ptr [rsp+0x40]
        vmovdqa32 xmm0 {k1}, xmmword ptr [rsp+0x1*0x10]
        vmovdqa32 xmm2 {k1}, xmmword ptr [rsp+0x5*0x10]
        vmovdqa xmmword ptr [rsp], xmm0
        vmovdqa xmmword ptr [rsp+0x40], xmm2
        add     rbx, 128
        add     rdi, 32
        sub     rsi, 4
3:
        test    esi, 0x2
        je      3f
        vbroadcasti128 ymm0, xmmword ptr [rcx]
        vbroadcasti128 ymm1, xmmword ptr [rcx+0x10]
        vmovd   xmm13, dword ptr [rsp]
        vpinsrd xmm13, xmm13, dword ptr [rsp+0x40], 1
        vpinsrd xmm13, xmm13, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        vmovd   xmm14, dword ptr [rsp+0x4]
        vpinsrd xmm14, xmm14, dword ptr [rsp+0x44], 1
        vpinsrd xmm14, xmm14, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        vinserti128 ymm13, ymm13, xmm14, 0x01
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
.p2align 5
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        mov     dword ptr [rsp+0x88], eax
        vbroadcasti128 ymm2, xmmword ptr [BLAKE3_IV+rip]
        vpbroadcastd ymm8, dword ptr [rsp+0x88]
        vpblendd ymm3, ymm13, ymm8, 0x88
        vmovups ymm8, ymmword ptr [r8+rdx-0x40]
        vinsertf128 ymm8, ymm8, xmmword ptr [r9+rdx-0x40], 0x01
        vmovups ymm9, ymmword ptr [r8+rdx-0x30]
        vinsertf128 ymm9, ymm9, xmmword ptr [r9+rdx-0x30], 0x01
        vshufps ymm4, ymm8, ymm9, 136
        vshufps ymm5, ymm8, ymm9, 221
        vmovups ymm8, ymmword ptr [r8+rdx-0x20]
        vinsertf128 ymm8, ymm8, xmmword ptr [r9+rdx-0x20], 0x01
        vmovups ymm9, ymmword ptr [r8+rdx-0x10]
        vinsertf128 ymm9, ymm9, xmmword ptr [r9+rdx-0x10], 0x01
        vshufps ymm6, ymm8, ymm9, 136
        vshufps ymm7, ymm8, ymm9, 221
        vpshufd ymm6, ymm6, 0x93
        vpshufd ymm7, ymm7, 0x93
        mov     al, 7
9:
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
        vpshufd ymm0, ymm0, 0x93
        vpshufd ymm3, ymm3, 0x4E
        vpshufd ymm2, ymm2, 0x39
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
        vpshufd ymm0, ymm0, 0x39
        vpshufd ymm3, ymm3, 0x4E
        vpshufd ymm2, ymm2, 0x93
        dec     al
        jz      9f
        vshufps ymm8, ymm4, ymm5, 214
        vpshufd ymm9, ymm4, 0x0F
        vpshufd ymm4, ymm8, 0x39
        vshufps ymm8, ymm6, ymm7, 250
        vpblendd ymm9, ymm9, ymm8, 0xAA
        vpunpcklqdq ymm8, ymm7, ymm5
        vpblendd ymm8, ymm8, ymm6, 0x88
        vpshufd ymm8, ymm8, 0x78
        vpunpckhdq ymm5, ymm5, ymm7
        vpunpckldq ymm6, ymm6, ymm5
        vpshufd ymm7, ymm6, 0x1E
        vmovdqa ymm5, ymm9
        vmovdqa ymm6, ymm8
        jmp     9b
9:
        vpxor   ymm0, ymm0, ymm2
        vpxor   ymm1, ymm1, ymm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     2b
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+0x10], xmm1
        vextracti128 xmmword ptr [rbx+0x20], ymm0, 0x01
        vextracti128 xmmword ptr [rbx+0x30], ymm1, 0x01
        vmovdqa xmm0, xmmword ptr [rsp]
        vmovdqa xmm2, xmmword ptr [rsp+0x4*0x10]
        vmovdqu32 xmm0 {k1}, xmmword ptr [rsp+0x8]
        vmovdqu32 xmm2 {k1}, xmmword ptr [rsp+0x48]
        vmovdqa xmmword ptr [rsp], xmm0
        vmovdqa xmmword ptr [rsp+0x4*0x10], xmm2
        add     rbx, 64
        add     rdi, 16
        sub     rsi, 2
3:
        test    esi, 0x1
        je      4b
        vmovdqu xmm0, xmmword ptr [rcx]
        vmovdqu xmm1, xmmword ptr [rcx+0x10]
        vmovd   xmm14, dword ptr [rsp]
        vpinsrd xmm14, xmm14, dword ptr [rsp+0x40], 1
        vpinsrd xmm14, xmm14, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        vmovdqa xmm15, xmmword ptr [BLAKE3_IV+rip]
        mov     r8, qword ptr [rdi]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
.p2align 5
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        vpinsrd xmm3, xmm14, eax, 3
        vmovdqa xmm2, xmm15
        vmovups xmm8, xmmword ptr [r8+rdx-0x40]
        vmovups xmm9, xmmword ptr [r8+rdx-0x30]
        vshufps xmm4, xmm8, xmm9, 136
        vshufps xmm5, xmm8, xmm9, 221
        vmovups xmm8, xmmword ptr [r8+rdx-0x20]
        vmovups xmm9, xmmword ptr [r8+rdx-0x10]
        vshufps xmm6, xmm8, xmm9, 136
        vshufps xmm7, xmm8, xmm9, 221
        vpshufd xmm6, xmm6, 0x93
        vpshufd xmm7, xmm7, 0x93
        mov     al, 7
9:
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
        vpshufd xmm0, xmm0, 0x93
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x39
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
        vpshufd xmm0, xmm0, 0x39
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x93
        dec     al
        jz      9f
        vshufps xmm8, xmm4, xmm5, 214
        vpshufd xmm9, xmm4, 0x0F
        vpshufd xmm4, xmm8, 0x39
        vshufps xmm8, xmm6, xmm7, 250
        vpblendd xmm9, xmm9, xmm8, 0xAA
        vpunpcklqdq xmm8, xmm7, xmm5
        vpblendd xmm8, xmm8, xmm6, 0x88
        vpshufd xmm8, xmm8, 0x78
        vpunpckhdq xmm5, xmm5, xmm7
        vpunpckldq xmm6, xmm6, xmm5
        vpshufd xmm7, xmm6, 0x1E
        vmovdqa xmm5, xmm9
        vmovdqa xmm6, xmm8
        jmp     9b
9:
        vpxor   xmm0, xmm0, xmm2
        vpxor   xmm1, xmm1, xmm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     2b
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+0x10], xmm1
        jmp     4b
.p2align 6
_blake3_compress_in_place_avx512:
blake3_compress_in_place_avx512:
        _CET_ENDBR
        vmovdqu xmm0, xmmword ptr [rdi]
        vmovdqu xmm1, xmmword ptr [rdi+0x10]
        movzx   eax, r8b
        movzx   edx, dl
        shl     rax, 32
        add     rdx, rax
        vmovq   xmm3, rcx
        vmovq   xmm4, rdx
        vpunpcklqdq xmm3, xmm3, xmm4
        vmovaps xmm2, xmmword ptr [BLAKE3_IV+rip]
        vmovups xmm8, xmmword ptr [rsi]
        vmovups xmm9, xmmword ptr [rsi+0x10]
        vshufps xmm4, xmm8, xmm9, 136
        vshufps xmm5, xmm8, xmm9, 221
        vmovups xmm8, xmmword ptr [rsi+0x20]
        vmovups xmm9, xmmword ptr [rsi+0x30]
        vshufps xmm6, xmm8, xmm9, 136
        vshufps xmm7, xmm8, xmm9, 221
        vpshufd xmm6, xmm6, 0x93
        vpshufd xmm7, xmm7, 0x93
        mov     al, 7
9:
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
        vpshufd xmm0, xmm0, 0x93
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x39
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
        vpshufd xmm0, xmm0, 0x39
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x93
        dec     al
        jz      9f
        vshufps xmm8, xmm4, xmm5, 214
        vpshufd xmm9, xmm4, 0x0F
        vpshufd xmm4, xmm8, 0x39
        vshufps xmm8, xmm6, xmm7, 250
        vpblendd xmm9, xmm9, xmm8, 0xAA
        vpunpcklqdq xmm8, xmm7, xmm5
        vpblendd xmm8, xmm8, xmm6, 0x88
        vpshufd xmm8, xmm8, 0x78
        vpunpckhdq xmm5, xmm5, xmm7
        vpunpckldq xmm6, xmm6, xmm5
        vpshufd xmm7, xmm6, 0x1E
        vmovdqa xmm5, xmm9
        vmovdqa xmm6, xmm8
        jmp     9b
9:
        vpxor   xmm0, xmm0, xmm2
        vpxor   xmm1, xmm1, xmm3
        vmovdqu xmmword ptr [rdi], xmm0
        vmovdqu xmmword ptr [rdi+0x10], xmm1
        ret

.p2align 6
_blake3_compress_xof_avx512:
blake3_compress_xof_avx512:
        _CET_ENDBR
        vmovdqu xmm0, xmmword ptr [rdi]
        vmovdqu xmm1, xmmword ptr [rdi+0x10]
        movzx   eax, r8b
        movzx   edx, dl
        shl     rax, 32
        add     rdx, rax
        vmovq   xmm3, rcx
        vmovq   xmm4, rdx
        vpunpcklqdq xmm3, xmm3, xmm4
        vmovaps xmm2, xmmword ptr [BLAKE3_IV+rip]
        vmovups xmm8, xmmword ptr [rsi]
        vmovups xmm9, xmmword ptr [rsi+0x10]
        vshufps xmm4, xmm8, xmm9, 136
        vshufps xmm5, xmm8, xmm9, 221
        vmovups xmm8, xmmword ptr [rsi+0x20]
        vmovups xmm9, xmmword ptr [rsi+0x30]
        vshufps xmm6, xmm8, xmm9, 136
        vshufps xmm7, xmm8, xmm9, 221
        vpshufd xmm6, xmm6, 0x93
        vpshufd xmm7, xmm7, 0x93
        mov     al, 7
9:
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
        vpshufd xmm0, xmm0, 0x93
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x39
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
        vpshufd xmm0, xmm0, 0x39
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x93
        dec     al
        jz      9f
        vshufps xmm8, xmm4, xmm5, 214
        vpshufd xmm9, xmm4, 0x0F
        vpshufd xmm4, xmm8, 0x39
        vshufps xmm8, xmm6, xmm7, 250
        vpblendd xmm9, xmm9, xmm8, 0xAA
        vpunpcklqdq xmm8, xmm7, xmm5
        vpblendd xmm8, xmm8, xmm6, 0x88
        vpshufd xmm8, xmm8, 0x78
        vpunpckhdq xmm5, xmm5, xmm7
        vpunpckldq xmm6, xmm6, xmm5
        vpshufd xmm7, xmm6, 0x1E
        vmovdqa xmm5, xmm9
        vmovdqa xmm6, xmm8
        jmp     9b
9:
        vpxor   xmm0, xmm0, xmm2
        vpxor   xmm1, xmm1, xmm3
        vpxor   xmm2, xmm2, [rdi]
        vpxor   xmm3, xmm3, [rdi+0x10]
        vmovdqu xmmword ptr [r9], xmm0
        vmovdqu xmmword ptr [r9+0x10], xmm1
        vmovdqu xmmword ptr [r9+0x20], xmm2
        vmovdqu xmmword ptr [r9+0x30], xmm3
        ret

#ifdef __APPLE__
.static_data
#else
.section .rodata
#endif
.p2align  6
INDEX0:
        .long    0,  1,  2,  3, 16, 17, 18, 19
        .long    8,  9, 10, 11, 24, 25, 26, 27
INDEX1:
        .long    4,  5,  6,  7, 20, 21, 22, 23
        .long   12, 13, 14, 15, 28, 29, 30, 31
ADD0:
        .long    0,  1,  2,  3,  4,  5,  6,  7
        .long    8,  9, 10, 11, 12, 13, 14, 15
ADD1:   .long    1

ADD16:  .long   16
BLAKE3_BLOCK_LEN:
        .long   64
.p2align 6
BLAKE3_IV:
BLAKE3_IV_0:
        .long   0x6A09E667
BLAKE3_IV_1:
        .long   0xBB67AE85
BLAKE3_IV_2:
        .long   0x3C6EF372
BLAKE3_IV_3:
        .long   0xA54FF53A

#endif
