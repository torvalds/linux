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
HIDDEN _blake3_hash_many_avx2
HIDDEN blake3_hash_many_avx2
.global _blake3_hash_many_avx2
.global blake3_hash_many_avx2
#ifdef __APPLE__
.text
#else
.section .text
#endif
        .p2align  6
_blake3_hash_many_avx2:
blake3_hash_many_avx2:
        _CET_ENDBR
        push    r15
        push    r14
        push    r13
        push    r12
        push    rbx
        push    rbp
        mov     rbp, rsp
        sub     rsp, 680
        and     rsp, 0xFFFFFFFFFFFFFFC0
        neg     r9d
        vmovd   xmm0, r9d
        vpbroadcastd ymm0, xmm0
        vmovdqa ymmword ptr [rsp+0x280], ymm0
        vpand   ymm1, ymm0, ymmword ptr [ADD0+rip]
        vpand   ymm2, ymm0, ymmword ptr [ADD1+rip]
        vmovdqa ymmword ptr [rsp+0x220], ymm2
        vmovd   xmm2, r8d
        vpbroadcastd ymm2, xmm2
        vpaddd  ymm2, ymm2, ymm1
        vmovdqa ymmword ptr [rsp+0x240], ymm2
        vpxor   ymm1, ymm1, ymmword ptr [CMP_MSB_MASK+rip]
        vpxor   ymm2, ymm2, ymmword ptr [CMP_MSB_MASK+rip]
        vpcmpgtd ymm2, ymm1, ymm2
        shr     r8, 32
        vmovd   xmm3, r8d
        vpbroadcastd ymm3, xmm3
        vpsubd  ymm3, ymm3, ymm2
        vmovdqa ymmword ptr [rsp+0x260], ymm3
        shl     rdx, 6
        mov     qword ptr [rsp+0x2A0], rdx
        cmp     rsi, 8
        jc      3f
2:
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
.p2align  5
9:
        movzx   ebx, byte ptr [rbp+0x48]
        or      ebx, eax
        add     rdx, 64
        cmp     rdx, qword ptr [rsp+0x2A0]
        cmove   eax, ebx
        mov     dword ptr [rsp+0x200], eax
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+0x20], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+0x40], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+0x60], ymm11
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp+0x80], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+0xA0], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+0xC0], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+0xE0], ymm11
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp+0x100], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+0x120], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+0x140], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+0x160], ymm11
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp+0x180], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+0x1A0], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+0x1C0], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+0x1E0], ymm11
        vpbroadcastd ymm15, dword ptr [rsp+0x200]
        prefetcht0 [r8+rdx+0x80]
        prefetcht0 [r12+rdx+0x80]
        prefetcht0 [r9+rdx+0x80]
        prefetcht0 [r13+rdx+0x80]
        prefetcht0 [r10+rdx+0x80]
        prefetcht0 [r14+rdx+0x80]
        prefetcht0 [r11+rdx+0x80]
        prefetcht0 [r15+rdx+0x80]
        vpaddd  ymm0, ymm0, ymmword ptr [rsp]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x40]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x80]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xC0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm0, ymmword ptr [rsp+0x240]
        vpxor   ymm13, ymm1, ymmword ptr [rsp+0x260]
        vpxor   ymm14, ymm2, ymmword ptr [BLAKE3_BLOCK_LEN+rip]
        vpxor   ymm15, ymm3, ymm15
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [BLAKE3_IV_0+rip]
        vpaddd  ymm9, ymm13, ymmword ptr [BLAKE3_IV_1+rip]
        vpaddd  ymm10, ymm14, ymmword ptr [BLAKE3_IV_2+rip]
        vpaddd  ymm11, ymm15, ymmword ptr [BLAKE3_IV_3+rip]
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x20]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x60]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0xA0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xE0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x100]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x140]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x180]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x120]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x160]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x40]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x60]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0xE0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x80]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0xC0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x140]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x20]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x180]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x120]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x160]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0xA0]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x100]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x60]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x140]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xE0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x80]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x180]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x40]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0xC0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x120]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x160]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x100]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0xA0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x20]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x140]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x180]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0xE0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x120]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x60]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x80]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x160]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0xA0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x20]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x40]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x100]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xC0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x180]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x120]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x160]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x140]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x100]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0xE0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0xA0]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xC0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x40]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x60]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x20]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x80]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x120]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x160]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x100]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0xA0]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x180]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x20]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x40]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x80]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x60]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x140]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0xC0]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xE0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x160]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0xA0]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x20]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x100]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x1E0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x120]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xC0]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x1C0]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x40]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x60]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0xE0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+0x200], ymm8
        vpsrld  ymm8, ymm5, 12
        vpslld  ymm5, ymm5, 20
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 12
        vpslld  ymm6, ymm6, 20
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 12
        vpslld  ymm7, ymm7, 20
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 12
        vpslld  ymm4, ymm4, 20
        vpor    ymm4, ymm4, ymm8
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0x140]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0x180]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0x80]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0x1A0]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8+rip]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+0x200]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vpxor   ymm0, ymm0, ymm8
        vpxor   ymm1, ymm1, ymm9
        vpxor   ymm2, ymm2, ymm10
        vpxor   ymm3, ymm3, ymm11
        vpsrld  ymm8, ymm5, 7
        vpslld  ymm5, ymm5, 25
        vpor    ymm5, ymm5, ymm8
        vpsrld  ymm8, ymm6, 7
        vpslld  ymm6, ymm6, 25
        vpor    ymm6, ymm6, ymm8
        vpsrld  ymm8, ymm7, 7
        vpslld  ymm7, ymm7, 25
        vpor    ymm7, ymm7, ymm8
        vpsrld  ymm8, ymm4, 7
        vpslld  ymm4, ymm4, 25
        vpor    ymm4, ymm4, ymm8
        vpxor   ymm4, ymm4, ymm12
        vpxor   ymm5, ymm5, ymm13
        vpxor   ymm6, ymm6, ymm14
        vpxor   ymm7, ymm7, ymm15
        movzx   eax, byte ptr [rbp+0x38]
        jne     9b
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
        vmovdqa ymm0, ymmword ptr [rsp+0x220]
        vpaddd  ymm1, ymm0, ymmword ptr [rsp+0x240]
        vmovdqa ymmword ptr [rsp+0x240], ymm1
        vpxor   ymm0, ymm0, ymmword ptr [CMP_MSB_MASK+rip]
        vpxor   ymm2, ymm1, ymmword ptr [CMP_MSB_MASK+rip]
        vpcmpgtd ymm2, ymm0, ymm2
        vmovdqa ymm0, ymmword ptr [rsp+0x260]
        vpsubd  ymm2, ymm0, ymm2
        vmovdqa ymmword ptr [rsp+0x260], ymm2
        add     rdi, 64
        add     rbx, 256
        mov     qword ptr [rbp+0x50], rbx
        sub     rsi, 8
        cmp     rsi, 8
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
.p2align  5
3:
        mov     rbx, qword ptr [rbp+0x50]
        mov     r15, qword ptr [rsp+0x2A0]
        movzx   r13d, byte ptr [rbp+0x38]
        movzx   r12d, byte ptr [rbp+0x48]
        test    rsi, 0x4
        je      3f
        vbroadcasti128 ymm0, xmmword ptr [rcx]
        vbroadcasti128 ymm1, xmmword ptr [rcx+0x10]
        vmovdqa ymm8, ymm0
        vmovdqa ymm9, ymm1
        vbroadcasti128 ymm12, xmmword ptr [rsp+0x240]
        vbroadcasti128 ymm13, xmmword ptr [rsp+0x260]
        vpunpckldq ymm14, ymm12, ymm13
        vpunpckhdq ymm15, ymm12, ymm13
        vpermq  ymm14, ymm14, 0x50
        vpermq  ymm15, ymm15, 0x50
        vbroadcasti128 ymm12, xmmword ptr [BLAKE3_BLOCK_LEN+rip]
        vpblendd ymm14, ymm14, ymm12, 0x44
        vpblendd ymm15, ymm15, ymm12, 0x44
        vmovdqa ymmword ptr [rsp], ymm14
        vmovdqa ymmword ptr [rsp+0x20], ymm15
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        mov     r10, qword ptr [rdi+0x10]
        mov     r11, qword ptr [rdi+0x18]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
.p2align  5
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        mov     dword ptr [rsp+0x200], eax
        vmovups ymm2, ymmword ptr [r8+rdx-0x40]
        vinsertf128 ymm2, ymm2, xmmword ptr [r9+rdx-0x40], 0x01
        vmovups ymm3, ymmword ptr [r8+rdx-0x30]
        vinsertf128 ymm3, ymm3, xmmword ptr [r9+rdx-0x30], 0x01
        vshufps ymm4, ymm2, ymm3, 136
        vshufps ymm5, ymm2, ymm3, 221
        vmovups ymm2, ymmword ptr [r8+rdx-0x20]
        vinsertf128 ymm2, ymm2, xmmword ptr [r9+rdx-0x20], 0x01
        vmovups ymm3, ymmword ptr [r8+rdx-0x10]
        vinsertf128 ymm3, ymm3, xmmword ptr [r9+rdx-0x10], 0x01
        vshufps ymm6, ymm2, ymm3, 136
        vshufps ymm7, ymm2, ymm3, 221
        vpshufd ymm6, ymm6, 0x93
        vpshufd ymm7, ymm7, 0x93
        vmovups ymm10, ymmword ptr [r10+rdx-0x40]
        vinsertf128 ymm10, ymm10, xmmword ptr [r11+rdx-0x40], 0x01
        vmovups ymm11, ymmword ptr [r10+rdx-0x30]
        vinsertf128 ymm11, ymm11, xmmword ptr [r11+rdx-0x30], 0x01
        vshufps ymm12, ymm10, ymm11, 136
        vshufps ymm13, ymm10, ymm11, 221
        vmovups ymm10, ymmword ptr [r10+rdx-0x20]
        vinsertf128 ymm10, ymm10, xmmword ptr [r11+rdx-0x20], 0x01
        vmovups ymm11, ymmword ptr [r10+rdx-0x10]
        vinsertf128 ymm11, ymm11, xmmword ptr [r11+rdx-0x10], 0x01
        vshufps ymm14, ymm10, ymm11, 136
        vshufps ymm15, ymm10, ymm11, 221
        vpshufd ymm14, ymm14, 0x93
        vpshufd ymm15, ymm15, 0x93
        prefetcht0 [r8+rdx+0x80]
        prefetcht0 [r9+rdx+0x80]
        prefetcht0 [r10+rdx+0x80]
        prefetcht0 [r11+rdx+0x80]
        vpbroadcastd ymm2, dword ptr [rsp+0x200]
        vmovdqa ymm3, ymmword ptr [rsp]
        vmovdqa ymm11, ymmword ptr [rsp+0x20]
        vpblendd ymm3, ymm3, ymm2, 0x88
        vpblendd ymm11, ymm11, ymm2, 0x88
        vbroadcasti128 ymm2, xmmword ptr [BLAKE3_IV+rip]
        vmovdqa ymm10, ymm2
        mov     al, 7
9:
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm8, ymm8, ymm12
        vmovdqa ymmword ptr [rsp+0x40], ymm4
        nop
        vmovdqa ymmword ptr [rsp+0x60], ymm12
        nop
        vpaddd  ymm0, ymm0, ymm1
        vpaddd  ymm8, ymm8, ymm9
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT16+rip]
        vpshufb ymm3, ymm3, ymm4
        vpshufb ymm11, ymm11, ymm4
        vpaddd  ymm2, ymm2, ymm3
        vpaddd  ymm10, ymm10, ymm11
        vpxor   ymm1, ymm1, ymm2
        vpxor   ymm9, ymm9, ymm10
        vpsrld  ymm4, ymm1, 12
        vpslld  ymm1, ymm1, 20
        vpor    ymm1, ymm1, ymm4
        vpsrld  ymm4, ymm9, 12
        vpslld  ymm9, ymm9, 20
        vpor    ymm9, ymm9, ymm4
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm8, ymm8, ymm13
        vpaddd  ymm0, ymm0, ymm1
        vpaddd  ymm8, ymm8, ymm9
        vmovdqa ymmword ptr [rsp+0x80], ymm5
        vmovdqa ymmword ptr [rsp+0xA0], ymm13
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT8+rip]
        vpshufb ymm3, ymm3, ymm4
        vpshufb ymm11, ymm11, ymm4
        vpaddd  ymm2, ymm2, ymm3
        vpaddd  ymm10, ymm10, ymm11
        vpxor   ymm1, ymm1, ymm2
        vpxor   ymm9, ymm9, ymm10
        vpsrld  ymm4, ymm1, 7
        vpslld  ymm1, ymm1, 25
        vpor    ymm1, ymm1, ymm4
        vpsrld  ymm4, ymm9, 7
        vpslld  ymm9, ymm9, 25
        vpor    ymm9, ymm9, ymm4
        vpshufd ymm0, ymm0, 0x93
        vpshufd ymm8, ymm8, 0x93
        vpshufd ymm3, ymm3, 0x4E
        vpshufd ymm11, ymm11, 0x4E
        vpshufd ymm2, ymm2, 0x39
        vpshufd ymm10, ymm10, 0x39
        vpaddd  ymm0, ymm0, ymm6
        vpaddd  ymm8, ymm8, ymm14
        vpaddd  ymm0, ymm0, ymm1
        vpaddd  ymm8, ymm8, ymm9
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT16+rip]
        vpshufb ymm3, ymm3, ymm4
        vpshufb ymm11, ymm11, ymm4
        vpaddd  ymm2, ymm2, ymm3
        vpaddd  ymm10, ymm10, ymm11
        vpxor   ymm1, ymm1, ymm2
        vpxor   ymm9, ymm9, ymm10
        vpsrld  ymm4, ymm1, 12
        vpslld  ymm1, ymm1, 20
        vpor    ymm1, ymm1, ymm4
        vpsrld  ymm4, ymm9, 12
        vpslld  ymm9, ymm9, 20
        vpor    ymm9, ymm9, ymm4
        vpaddd  ymm0, ymm0, ymm7
        vpaddd  ymm8, ymm8, ymm15
        vpaddd  ymm0, ymm0, ymm1
        vpaddd  ymm8, ymm8, ymm9
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT8+rip]
        vpshufb ymm3, ymm3, ymm4
        vpshufb ymm11, ymm11, ymm4
        vpaddd  ymm2, ymm2, ymm3
        vpaddd  ymm10, ymm10, ymm11
        vpxor   ymm1, ymm1, ymm2
        vpxor   ymm9, ymm9, ymm10
        vpsrld  ymm4, ymm1, 7
        vpslld  ymm1, ymm1, 25
        vpor    ymm1, ymm1, ymm4
        vpsrld  ymm4, ymm9, 7
        vpslld  ymm9, ymm9, 25
        vpor    ymm9, ymm9, ymm4
        vpshufd ymm0, ymm0, 0x39
        vpshufd ymm8, ymm8, 0x39
        vpshufd ymm3, ymm3, 0x4E
        vpshufd ymm11, ymm11, 0x4E
        vpshufd ymm2, ymm2, 0x93
        vpshufd ymm10, ymm10, 0x93
        dec     al
        je      9f
        vmovdqa ymm4, ymmword ptr [rsp+0x40]
        vmovdqa ymm5, ymmword ptr [rsp+0x80]
        vshufps ymm12, ymm4, ymm5, 214
        vpshufd ymm13, ymm4, 0x0F
        vpshufd ymm4, ymm12, 0x39
        vshufps ymm12, ymm6, ymm7, 250
        vpblendd ymm13, ymm13, ymm12, 0xAA
        vpunpcklqdq ymm12, ymm7, ymm5
        vpblendd ymm12, ymm12, ymm6, 0x88
        vpshufd ymm12, ymm12, 0x78
        vpunpckhdq ymm5, ymm5, ymm7
        vpunpckldq ymm6, ymm6, ymm5
        vpshufd ymm7, ymm6, 0x1E
        vmovdqa ymmword ptr [rsp+0x40], ymm13
        vmovdqa ymmword ptr [rsp+0x80], ymm12
        vmovdqa ymm12, ymmword ptr [rsp+0x60]
        vmovdqa ymm13, ymmword ptr [rsp+0xA0]
        vshufps ymm5, ymm12, ymm13, 214
        vpshufd ymm6, ymm12, 0x0F
        vpshufd ymm12, ymm5, 0x39
        vshufps ymm5, ymm14, ymm15, 250
        vpblendd ymm6, ymm6, ymm5, 0xAA
        vpunpcklqdq ymm5, ymm15, ymm13
        vpblendd ymm5, ymm5, ymm14, 0x88
        vpshufd ymm5, ymm5, 0x78
        vpunpckhdq ymm13, ymm13, ymm15
        vpunpckldq ymm14, ymm14, ymm13
        vpshufd ymm15, ymm14, 0x1E
        vmovdqa ymm13, ymm6
        vmovdqa ymm14, ymm5
        vmovdqa ymm5, ymmword ptr [rsp+0x40]
        vmovdqa ymm6, ymmword ptr [rsp+0x80]
        jmp     9b
9:
        vpxor   ymm0, ymm0, ymm2
        vpxor   ymm1, ymm1, ymm3
        vpxor   ymm8, ymm8, ymm10
        vpxor   ymm9, ymm9, ymm11
        mov     eax, r13d
        cmp     rdx, r15
        jne     2b
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+0x10], xmm1
        vextracti128 xmmword ptr [rbx+0x20], ymm0, 0x01
        vextracti128 xmmword ptr [rbx+0x30], ymm1, 0x01
        vmovdqu xmmword ptr [rbx+0x40], xmm8
        vmovdqu xmmword ptr [rbx+0x50], xmm9
        vextracti128 xmmword ptr [rbx+0x60], ymm8, 0x01
        vextracti128 xmmword ptr [rbx+0x70], ymm9, 0x01
        vmovaps xmm8, xmmword ptr [rsp+0x280]
        vmovaps xmm0, xmmword ptr [rsp+0x240]
        vmovaps xmm1, xmmword ptr [rsp+0x250]
        vmovaps xmm2, xmmword ptr [rsp+0x260]
        vmovaps xmm3, xmmword ptr [rsp+0x270]
        vblendvps xmm0, xmm0, xmm1, xmm8
        vblendvps xmm2, xmm2, xmm3, xmm8
        vmovaps xmmword ptr [rsp+0x240], xmm0
        vmovaps xmmword ptr [rsp+0x260], xmm2
        add     rbx, 128
        add     rdi, 32
        sub     rsi, 4
3:
        test    rsi, 0x2
        je      3f
        vbroadcasti128 ymm0, xmmword ptr [rcx]
        vbroadcasti128 ymm1, xmmword ptr [rcx+0x10]
        vmovd   xmm13, dword ptr [rsp+0x240]
        vpinsrd xmm13, xmm13, dword ptr [rsp+0x260], 1
        vpinsrd xmm13, xmm13, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        vmovd   xmm14, dword ptr [rsp+0x244]
        vpinsrd xmm14, xmm14, dword ptr [rsp+0x264], 1
        vpinsrd xmm14, xmm14, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        vinserti128 ymm13, ymm13, xmm14, 0x01
        vbroadcasti128 ymm14, xmmword ptr [ROT16+rip]
        vbroadcasti128 ymm15, xmmword ptr [ROT8+rip]
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
.p2align  5
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        mov     dword ptr [rsp+0x200], eax
        vbroadcasti128 ymm2, xmmword ptr [BLAKE3_IV+rip]
        vpbroadcastd ymm8, dword ptr [rsp+0x200]
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
        vpxor   ymm3, ymm3, ymm0
        vpshufb ymm3, ymm3, ymm14
        vpaddd  ymm2, ymm2, ymm3
        vpxor   ymm1, ymm1, ymm2
        vpsrld  ymm8, ymm1, 12
        vpslld  ymm1, ymm1, 20
        vpor    ymm1, ymm1, ymm8
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm0, ymm0, ymm1
        vpxor   ymm3, ymm3, ymm0
        vpshufb ymm3, ymm3, ymm15
        vpaddd  ymm2, ymm2, ymm3
        vpxor   ymm1, ymm1, ymm2
        vpsrld  ymm8, ymm1, 7
        vpslld  ymm1, ymm1, 25
        vpor    ymm1, ymm1, ymm8
        vpshufd ymm0, ymm0, 0x93
        vpshufd ymm3, ymm3, 0x4E
        vpshufd ymm2, ymm2, 0x39
        vpaddd  ymm0, ymm0, ymm6
        vpaddd  ymm0, ymm0, ymm1
        vpxor   ymm3, ymm3, ymm0
        vpshufb ymm3, ymm3, ymm14
        vpaddd  ymm2, ymm2, ymm3
        vpxor   ymm1, ymm1, ymm2
        vpsrld  ymm8, ymm1, 12
        vpslld  ymm1, ymm1, 20
        vpor    ymm1, ymm1, ymm8
        vpaddd  ymm0, ymm0, ymm7
        vpaddd  ymm0, ymm0, ymm1
        vpxor   ymm3, ymm3, ymm0
        vpshufb ymm3, ymm3, ymm15
        vpaddd  ymm2, ymm2, ymm3
        vpxor   ymm1, ymm1, ymm2
        vpsrld  ymm8, ymm1, 7
        vpslld  ymm1, ymm1, 25
        vpor    ymm1, ymm1, ymm8
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
        vmovaps ymm8, ymmword ptr [rsp+0x280]
        vmovaps ymm0, ymmword ptr [rsp+0x240]
        vmovups ymm1, ymmword ptr [rsp+0x248]
        vmovaps ymm2, ymmword ptr [rsp+0x260]
        vmovups ymm3, ymmword ptr [rsp+0x268]
        vblendvps ymm0, ymm0, ymm1, ymm8
        vblendvps ymm2, ymm2, ymm3, ymm8
        vmovaps ymmword ptr [rsp+0x240], ymm0
        vmovaps ymmword ptr [rsp+0x260], ymm2
        add     rbx, 64
        add     rdi, 16
        sub     rsi, 2
3:
        test    rsi, 0x1
        je      4b
        vmovdqu xmm0, xmmword ptr [rcx]
        vmovdqu xmm1, xmmword ptr [rcx+0x10]
        vmovd   xmm3, dword ptr [rsp+0x240]
        vpinsrd xmm3, xmm3, dword ptr [rsp+0x260], 1
        vpinsrd xmm13, xmm3, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        vmovdqa xmm14, xmmword ptr [ROT16+rip]
        vmovdqa xmm15, xmmword ptr [ROT8+rip]
        mov     r8, qword ptr [rdi]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
.p2align  5
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        vmovdqa xmm2, xmmword ptr [BLAKE3_IV+rip]
        vmovdqa xmm3, xmm13
        vpinsrd xmm3, xmm3, eax, 3
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
        vpxor   xmm3, xmm3, xmm0
        vpshufb xmm3, xmm3, xmm14
        vpaddd  xmm2, xmm2, xmm3
        vpxor   xmm1, xmm1, xmm2
        vpsrld  xmm8, xmm1, 12
        vpslld  xmm1, xmm1, 20
        vpor    xmm1, xmm1, xmm8
        vpaddd  xmm0, xmm0, xmm5
        vpaddd  xmm0, xmm0, xmm1
        vpxor   xmm3, xmm3, xmm0
        vpshufb xmm3, xmm3, xmm15
        vpaddd  xmm2, xmm2, xmm3
        vpxor   xmm1, xmm1, xmm2
        vpsrld  xmm8, xmm1, 7
        vpslld  xmm1, xmm1, 25
        vpor    xmm1, xmm1, xmm8
        vpshufd xmm0, xmm0, 0x93
        vpshufd xmm3, xmm3, 0x4E
        vpshufd xmm2, xmm2, 0x39
        vpaddd  xmm0, xmm0, xmm6
        vpaddd  xmm0, xmm0, xmm1
        vpxor   xmm3, xmm3, xmm0
        vpshufb xmm3, xmm3, xmm14
        vpaddd  xmm2, xmm2, xmm3
        vpxor   xmm1, xmm1, xmm2
        vpsrld  xmm8, xmm1, 12
        vpslld  xmm1, xmm1, 20
        vpor    xmm1, xmm1, xmm8
        vpaddd  xmm0, xmm0, xmm7
        vpaddd  xmm0, xmm0, xmm1
        vpxor   xmm3, xmm3, xmm0
        vpshufb xmm3, xmm3, xmm15
        vpaddd  xmm2, xmm2, xmm3
        vpxor   xmm1, xmm1, xmm2
        vpsrld  xmm8, xmm1, 7
        vpslld  xmm1, xmm1, 25
        vpor    xmm1, xmm1, xmm8
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


#ifdef __APPLE__
.static_data
#else
.section .rodata
#endif
.p2align  6
ADD0:
        .long  0, 1, 2, 3, 4, 5, 6, 7
ADD1:
        .long  8, 8, 8, 8, 8, 8, 8, 8
BLAKE3_IV_0:
        .long  0x6A09E667, 0x6A09E667, 0x6A09E667, 0x6A09E667
        .long  0x6A09E667, 0x6A09E667, 0x6A09E667, 0x6A09E667
BLAKE3_IV_1:
        .long  0xBB67AE85, 0xBB67AE85, 0xBB67AE85, 0xBB67AE85
        .long  0xBB67AE85, 0xBB67AE85, 0xBB67AE85, 0xBB67AE85
BLAKE3_IV_2:
        .long  0x3C6EF372, 0x3C6EF372, 0x3C6EF372, 0x3C6EF372
        .long  0x3C6EF372, 0x3C6EF372, 0x3C6EF372, 0x3C6EF372
BLAKE3_IV_3:
        .long  0xA54FF53A, 0xA54FF53A, 0xA54FF53A, 0xA54FF53A
        .long  0xA54FF53A, 0xA54FF53A, 0xA54FF53A, 0xA54FF53A
BLAKE3_BLOCK_LEN:
        .long  0x00000040, 0x00000040, 0x00000040, 0x00000040
        .long  0x00000040, 0x00000040, 0x00000040, 0x00000040
ROT16:
        .byte  2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
ROT8:
        .byte  1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
CMP_MSB_MASK:
        .long  0x80000000, 0x80000000, 0x80000000, 0x80000000
        .long  0x80000000, 0x80000000, 0x80000000, 0x80000000
BLAKE3_IV:
        .long  0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A

#endif
