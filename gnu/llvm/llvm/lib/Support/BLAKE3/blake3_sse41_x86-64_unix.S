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
HIDDEN blake3_hash_many_sse41
HIDDEN _blake3_hash_many_sse41
HIDDEN blake3_compress_in_place_sse41
HIDDEN _blake3_compress_in_place_sse41
HIDDEN blake3_compress_xof_sse41
HIDDEN _blake3_compress_xof_sse41
.global blake3_hash_many_sse41
.global _blake3_hash_many_sse41
.global blake3_compress_in_place_sse41
.global _blake3_compress_in_place_sse41
.global blake3_compress_xof_sse41
.global _blake3_compress_xof_sse41
#ifdef __APPLE__
.text
#else
.section .text
#endif
        .p2align  6
_blake3_hash_many_sse41:
blake3_hash_many_sse41:
        _CET_ENDBR
        push    r15
        push    r14
        push    r13
        push    r12
        push    rbx
        push    rbp
        mov     rbp, rsp
        sub     rsp, 360
        and     rsp, 0xFFFFFFFFFFFFFFC0
        neg     r9d
        movd    xmm0, r9d
        pshufd  xmm0, xmm0, 0x00
        movdqa  xmmword ptr [rsp+0x130], xmm0
        movdqa  xmm1, xmm0
        pand    xmm1, xmmword ptr [ADD0+rip]
        pand    xmm0, xmmword ptr [ADD1+rip]
        movdqa  xmmword ptr [rsp+0x150], xmm0
        movd    xmm0, r8d
        pshufd  xmm0, xmm0, 0x00
        paddd   xmm0, xmm1
        movdqa  xmmword ptr [rsp+0x110], xmm0
        pxor    xmm0, xmmword ptr [CMP_MSB_MASK+rip]
        pxor    xmm1, xmmword ptr [CMP_MSB_MASK+rip]
        pcmpgtd xmm1, xmm0
        shr     r8, 32
        movd    xmm2, r8d
        pshufd  xmm2, xmm2, 0x00
        psubd   xmm2, xmm1
        movdqa  xmmword ptr [rsp+0x120], xmm2
        mov     rbx, qword ptr [rbp+0x50]
        mov     r15, rdx
        shl     r15, 6
        movzx   r13d, byte ptr [rbp+0x38]
        movzx   r12d, byte ptr [rbp+0x48]
        cmp     rsi, 4
        jc      3f
2:
        movdqu  xmm3, xmmword ptr [rcx]
        pshufd  xmm0, xmm3, 0x00
        pshufd  xmm1, xmm3, 0x55
        pshufd  xmm2, xmm3, 0xAA
        pshufd  xmm3, xmm3, 0xFF
        movdqu  xmm7, xmmword ptr [rcx+0x10]
        pshufd  xmm4, xmm7, 0x00
        pshufd  xmm5, xmm7, 0x55
        pshufd  xmm6, xmm7, 0xAA
        pshufd  xmm7, xmm7, 0xFF
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        mov     r10, qword ptr [rdi+0x10]
        mov     r11, qword ptr [rdi+0x18]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
9:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        movdqu  xmm8, xmmword ptr [r8+rdx-0x40]
        movdqu  xmm9, xmmword ptr [r9+rdx-0x40]
        movdqu  xmm10, xmmword ptr [r10+rdx-0x40]
        movdqu  xmm11, xmmword ptr [r11+rdx-0x40]
        movdqa  xmm12, xmm8
        punpckldq xmm8, xmm9
        punpckhdq xmm12, xmm9
        movdqa  xmm14, xmm10
        punpckldq xmm10, xmm11
        punpckhdq xmm14, xmm11
        movdqa  xmm9, xmm8
        punpcklqdq xmm8, xmm10
        punpckhqdq xmm9, xmm10
        movdqa  xmm13, xmm12
        punpcklqdq xmm12, xmm14
        punpckhqdq xmm13, xmm14
        movdqa  xmmword ptr [rsp], xmm8
        movdqa  xmmword ptr [rsp+0x10], xmm9
        movdqa  xmmword ptr [rsp+0x20], xmm12
        movdqa  xmmword ptr [rsp+0x30], xmm13
        movdqu  xmm8, xmmword ptr [r8+rdx-0x30]
        movdqu  xmm9, xmmword ptr [r9+rdx-0x30]
        movdqu  xmm10, xmmword ptr [r10+rdx-0x30]
        movdqu  xmm11, xmmword ptr [r11+rdx-0x30]
        movdqa  xmm12, xmm8
        punpckldq xmm8, xmm9
        punpckhdq xmm12, xmm9
        movdqa  xmm14, xmm10
        punpckldq xmm10, xmm11
        punpckhdq xmm14, xmm11
        movdqa  xmm9, xmm8
        punpcklqdq xmm8, xmm10
        punpckhqdq xmm9, xmm10
        movdqa  xmm13, xmm12
        punpcklqdq xmm12, xmm14
        punpckhqdq xmm13, xmm14
        movdqa  xmmword ptr [rsp+0x40], xmm8
        movdqa  xmmword ptr [rsp+0x50], xmm9
        movdqa  xmmword ptr [rsp+0x60], xmm12
        movdqa  xmmword ptr [rsp+0x70], xmm13
        movdqu  xmm8, xmmword ptr [r8+rdx-0x20]
        movdqu  xmm9, xmmword ptr [r9+rdx-0x20]
        movdqu  xmm10, xmmword ptr [r10+rdx-0x20]
        movdqu  xmm11, xmmword ptr [r11+rdx-0x20]
        movdqa  xmm12, xmm8
        punpckldq xmm8, xmm9
        punpckhdq xmm12, xmm9
        movdqa  xmm14, xmm10
        punpckldq xmm10, xmm11
        punpckhdq xmm14, xmm11
        movdqa  xmm9, xmm8
        punpcklqdq xmm8, xmm10
        punpckhqdq xmm9, xmm10
        movdqa  xmm13, xmm12
        punpcklqdq xmm12, xmm14
        punpckhqdq xmm13, xmm14
        movdqa  xmmword ptr [rsp+0x80], xmm8
        movdqa  xmmword ptr [rsp+0x90], xmm9
        movdqa  xmmword ptr [rsp+0xA0], xmm12
        movdqa  xmmword ptr [rsp+0xB0], xmm13
        movdqu  xmm8, xmmword ptr [r8+rdx-0x10]
        movdqu  xmm9, xmmword ptr [r9+rdx-0x10]
        movdqu  xmm10, xmmword ptr [r10+rdx-0x10]
        movdqu  xmm11, xmmword ptr [r11+rdx-0x10]
        movdqa  xmm12, xmm8
        punpckldq xmm8, xmm9
        punpckhdq xmm12, xmm9
        movdqa  xmm14, xmm10
        punpckldq xmm10, xmm11
        punpckhdq xmm14, xmm11
        movdqa  xmm9, xmm8
        punpcklqdq xmm8, xmm10
        punpckhqdq xmm9, xmm10
        movdqa  xmm13, xmm12
        punpcklqdq xmm12, xmm14
        punpckhqdq xmm13, xmm14
        movdqa  xmmword ptr [rsp+0xC0], xmm8
        movdqa  xmmword ptr [rsp+0xD0], xmm9
        movdqa  xmmword ptr [rsp+0xE0], xmm12
        movdqa  xmmword ptr [rsp+0xF0], xmm13
        movdqa  xmm9, xmmword ptr [BLAKE3_IV_1+rip]
        movdqa  xmm10, xmmword ptr [BLAKE3_IV_2+rip]
        movdqa  xmm11, xmmword ptr [BLAKE3_IV_3+rip]
        movdqa  xmm12, xmmword ptr [rsp+0x110]
        movdqa  xmm13, xmmword ptr [rsp+0x120]
        movdqa  xmm14, xmmword ptr [BLAKE3_BLOCK_LEN+rip]
        movd    xmm15, eax
        pshufd  xmm15, xmm15, 0x00
        prefetcht0 [r8+rdx+0x80]
        prefetcht0 [r9+rdx+0x80]
        prefetcht0 [r10+rdx+0x80]
        prefetcht0 [r11+rdx+0x80]
        paddd   xmm0, xmmword ptr [rsp]
        paddd   xmm1, xmmword ptr [rsp+0x20]
        paddd   xmm2, xmmword ptr [rsp+0x40]
        paddd   xmm3, xmmword ptr [rsp+0x60]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [BLAKE3_IV_0+rip]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x10]
        paddd   xmm1, xmmword ptr [rsp+0x30]
        paddd   xmm2, xmmword ptr [rsp+0x50]
        paddd   xmm3, xmmword ptr [rsp+0x70]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x80]
        paddd   xmm1, xmmword ptr [rsp+0xA0]
        paddd   xmm2, xmmword ptr [rsp+0xC0]
        paddd   xmm3, xmmword ptr [rsp+0xE0]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x90]
        paddd   xmm1, xmmword ptr [rsp+0xB0]
        paddd   xmm2, xmmword ptr [rsp+0xD0]
        paddd   xmm3, xmmword ptr [rsp+0xF0]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x20]
        paddd   xmm1, xmmword ptr [rsp+0x30]
        paddd   xmm2, xmmword ptr [rsp+0x70]
        paddd   xmm3, xmmword ptr [rsp+0x40]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x60]
        paddd   xmm1, xmmword ptr [rsp+0xA0]
        paddd   xmm2, xmmword ptr [rsp]
        paddd   xmm3, xmmword ptr [rsp+0xD0]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x10]
        paddd   xmm1, xmmword ptr [rsp+0xC0]
        paddd   xmm2, xmmword ptr [rsp+0x90]
        paddd   xmm3, xmmword ptr [rsp+0xF0]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xB0]
        paddd   xmm1, xmmword ptr [rsp+0x50]
        paddd   xmm2, xmmword ptr [rsp+0xE0]
        paddd   xmm3, xmmword ptr [rsp+0x80]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x30]
        paddd   xmm1, xmmword ptr [rsp+0xA0]
        paddd   xmm2, xmmword ptr [rsp+0xD0]
        paddd   xmm3, xmmword ptr [rsp+0x70]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x40]
        paddd   xmm1, xmmword ptr [rsp+0xC0]
        paddd   xmm2, xmmword ptr [rsp+0x20]
        paddd   xmm3, xmmword ptr [rsp+0xE0]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x60]
        paddd   xmm1, xmmword ptr [rsp+0x90]
        paddd   xmm2, xmmword ptr [rsp+0xB0]
        paddd   xmm3, xmmword ptr [rsp+0x80]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x50]
        paddd   xmm1, xmmword ptr [rsp]
        paddd   xmm2, xmmword ptr [rsp+0xF0]
        paddd   xmm3, xmmword ptr [rsp+0x10]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xA0]
        paddd   xmm1, xmmword ptr [rsp+0xC0]
        paddd   xmm2, xmmword ptr [rsp+0xE0]
        paddd   xmm3, xmmword ptr [rsp+0xD0]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x70]
        paddd   xmm1, xmmword ptr [rsp+0x90]
        paddd   xmm2, xmmword ptr [rsp+0x30]
        paddd   xmm3, xmmword ptr [rsp+0xF0]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x40]
        paddd   xmm1, xmmword ptr [rsp+0xB0]
        paddd   xmm2, xmmword ptr [rsp+0x50]
        paddd   xmm3, xmmword ptr [rsp+0x10]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp]
        paddd   xmm1, xmmword ptr [rsp+0x20]
        paddd   xmm2, xmmword ptr [rsp+0x80]
        paddd   xmm3, xmmword ptr [rsp+0x60]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xC0]
        paddd   xmm1, xmmword ptr [rsp+0x90]
        paddd   xmm2, xmmword ptr [rsp+0xF0]
        paddd   xmm3, xmmword ptr [rsp+0xE0]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xD0]
        paddd   xmm1, xmmword ptr [rsp+0xB0]
        paddd   xmm2, xmmword ptr [rsp+0xA0]
        paddd   xmm3, xmmword ptr [rsp+0x80]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x70]
        paddd   xmm1, xmmword ptr [rsp+0x50]
        paddd   xmm2, xmmword ptr [rsp]
        paddd   xmm3, xmmword ptr [rsp+0x60]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x20]
        paddd   xmm1, xmmword ptr [rsp+0x30]
        paddd   xmm2, xmmword ptr [rsp+0x10]
        paddd   xmm3, xmmword ptr [rsp+0x40]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x90]
        paddd   xmm1, xmmword ptr [rsp+0xB0]
        paddd   xmm2, xmmword ptr [rsp+0x80]
        paddd   xmm3, xmmword ptr [rsp+0xF0]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xE0]
        paddd   xmm1, xmmword ptr [rsp+0x50]
        paddd   xmm2, xmmword ptr [rsp+0xC0]
        paddd   xmm3, xmmword ptr [rsp+0x10]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xD0]
        paddd   xmm1, xmmword ptr [rsp]
        paddd   xmm2, xmmword ptr [rsp+0x20]
        paddd   xmm3, xmmword ptr [rsp+0x40]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0x30]
        paddd   xmm1, xmmword ptr [rsp+0xA0]
        paddd   xmm2, xmmword ptr [rsp+0x60]
        paddd   xmm3, xmmword ptr [rsp+0x70]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xB0]
        paddd   xmm1, xmmword ptr [rsp+0x50]
        paddd   xmm2, xmmword ptr [rsp+0x10]
        paddd   xmm3, xmmword ptr [rsp+0x80]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xF0]
        paddd   xmm1, xmmword ptr [rsp]
        paddd   xmm2, xmmword ptr [rsp+0x90]
        paddd   xmm3, xmmword ptr [rsp+0x60]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xE0]
        paddd   xmm1, xmmword ptr [rsp+0x20]
        paddd   xmm2, xmmword ptr [rsp+0x30]
        paddd   xmm3, xmmword ptr [rsp+0x70]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+0x100], xmm8
        movdqa  xmm8, xmm5
        psrld   xmm8, 12
        pslld   xmm5, 20
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 12
        pslld   xmm6, 20
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 12
        pslld   xmm7, 20
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 12
        pslld   xmm4, 20
        por     xmm4, xmm8
        paddd   xmm0, xmmword ptr [rsp+0xA0]
        paddd   xmm1, xmmword ptr [rsp+0xC0]
        paddd   xmm2, xmmword ptr [rsp+0x40]
        paddd   xmm3, xmmword ptr [rsp+0xD0]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8+rip]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+0x100]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        pxor    xmm0, xmm8
        pxor    xmm1, xmm9
        pxor    xmm2, xmm10
        pxor    xmm3, xmm11
        movdqa  xmm8, xmm5
        psrld   xmm8, 7
        pslld   xmm5, 25
        por     xmm5, xmm8
        movdqa  xmm8, xmm6
        psrld   xmm8, 7
        pslld   xmm6, 25
        por     xmm6, xmm8
        movdqa  xmm8, xmm7
        psrld   xmm8, 7
        pslld   xmm7, 25
        por     xmm7, xmm8
        movdqa  xmm8, xmm4
        psrld   xmm8, 7
        pslld   xmm4, 25
        por     xmm4, xmm8
        pxor    xmm4, xmm12
        pxor    xmm5, xmm13
        pxor    xmm6, xmm14
        pxor    xmm7, xmm15
        mov     eax, r13d
        jne     9b
        movdqa  xmm9, xmm0
        punpckldq xmm0, xmm1
        punpckhdq xmm9, xmm1
        movdqa  xmm11, xmm2
        punpckldq xmm2, xmm3
        punpckhdq xmm11, xmm3
        movdqa  xmm1, xmm0
        punpcklqdq xmm0, xmm2
        punpckhqdq xmm1, xmm2
        movdqa  xmm3, xmm9
        punpcklqdq xmm9, xmm11
        punpckhqdq xmm3, xmm11
        movdqu  xmmword ptr [rbx], xmm0
        movdqu  xmmword ptr [rbx+0x20], xmm1
        movdqu  xmmword ptr [rbx+0x40], xmm9
        movdqu  xmmword ptr [rbx+0x60], xmm3
        movdqa  xmm9, xmm4
        punpckldq xmm4, xmm5
        punpckhdq xmm9, xmm5
        movdqa  xmm11, xmm6
        punpckldq xmm6, xmm7
        punpckhdq xmm11, xmm7
        movdqa  xmm5, xmm4
        punpcklqdq xmm4, xmm6
        punpckhqdq xmm5, xmm6
        movdqa  xmm7, xmm9
        punpcklqdq xmm9, xmm11
        punpckhqdq xmm7, xmm11
        movdqu  xmmword ptr [rbx+0x10], xmm4
        movdqu  xmmword ptr [rbx+0x30], xmm5
        movdqu  xmmword ptr [rbx+0x50], xmm9
        movdqu  xmmword ptr [rbx+0x70], xmm7
        movdqa  xmm1, xmmword ptr [rsp+0x110]
        movdqa  xmm0, xmm1
        paddd   xmm1, xmmword ptr [rsp+0x150]
        movdqa  xmmword ptr [rsp+0x110], xmm1
        pxor    xmm0, xmmword ptr [CMP_MSB_MASK+rip]
        pxor    xmm1, xmmword ptr [CMP_MSB_MASK+rip]
        pcmpgtd xmm0, xmm1
        movdqa  xmm1, xmmword ptr [rsp+0x120]
        psubd   xmm1, xmm0
        movdqa  xmmword ptr [rsp+0x120], xmm1
        add     rbx, 128
        add     rdi, 32
        sub     rsi, 4
        cmp     rsi, 4
        jnc     2b
        test    rsi, rsi
        jnz     3f
4:
        mov     rsp, rbp
        pop     rbp
        pop     rbx
        pop     r12
        pop     r13
        pop     r14
        pop     r15
        ret
.p2align 5
3:
        test    esi, 0x2
        je      3f
        movups  xmm0, xmmword ptr [rcx]
        movups  xmm1, xmmword ptr [rcx+0x10]
        movaps  xmm8, xmm0
        movaps  xmm9, xmm1
        movd    xmm13, dword ptr [rsp+0x110]
        pinsrd  xmm13, dword ptr [rsp+0x120], 1
        pinsrd  xmm13, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        movaps  xmmword ptr [rsp], xmm13
        movd    xmm14, dword ptr [rsp+0x114]
        pinsrd  xmm14, dword ptr [rsp+0x124], 1
        pinsrd  xmm14, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        movaps  xmmword ptr [rsp+0x10], xmm14
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+0x8]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        movaps  xmm2, xmmword ptr [BLAKE3_IV+rip]
        movaps  xmm10, xmm2
        movups  xmm4, xmmword ptr [r8+rdx-0x40]
        movups  xmm5, xmmword ptr [r8+rdx-0x30]
        movaps  xmm3, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm3, xmm5, 221
        movaps  xmm5, xmm3
        movups  xmm6, xmmword ptr [r8+rdx-0x20]
        movups  xmm7, xmmword ptr [r8+rdx-0x10]
        movaps  xmm3, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 0x93
        shufps  xmm3, xmm7, 221
        pshufd  xmm7, xmm3, 0x93
        movups  xmm12, xmmword ptr [r9+rdx-0x40]
        movups  xmm13, xmmword ptr [r9+rdx-0x30]
        movaps  xmm11, xmm12
        shufps  xmm12, xmm13, 136
        shufps  xmm11, xmm13, 221
        movaps  xmm13, xmm11
        movups  xmm14, xmmword ptr [r9+rdx-0x20]
        movups  xmm15, xmmword ptr [r9+rdx-0x10]
        movaps  xmm11, xmm14
        shufps  xmm14, xmm15, 136
        pshufd  xmm14, xmm14, 0x93
        shufps  xmm11, xmm15, 221
        pshufd  xmm15, xmm11, 0x93
        movaps  xmm3, xmmword ptr [rsp]
        movaps  xmm11, xmmword ptr [rsp+0x10]
        pinsrd  xmm3, eax, 3
        pinsrd  xmm11, eax, 3
        mov     al, 7
9:
        paddd   xmm0, xmm4
        paddd   xmm8, xmm12
        movaps  xmmword ptr [rsp+0x20], xmm4
        movaps  xmmword ptr [rsp+0x30], xmm12
        paddd   xmm0, xmm1
        paddd   xmm8, xmm9
        pxor    xmm3, xmm0
        pxor    xmm11, xmm8
        movaps  xmm12, xmmword ptr [ROT16+rip]
        pshufb  xmm3, xmm12
        pshufb  xmm11, xmm12
        paddd   xmm2, xmm3
        paddd   xmm10, xmm11
        pxor    xmm1, xmm2
        pxor    xmm9, xmm10
        movdqa  xmm4, xmm1
        pslld   xmm1, 20
        psrld   xmm4, 12
        por     xmm1, xmm4
        movdqa  xmm4, xmm9
        pslld   xmm9, 20
        psrld   xmm4, 12
        por     xmm9, xmm4
        paddd   xmm0, xmm5
        paddd   xmm8, xmm13
        movaps  xmmword ptr [rsp+0x40], xmm5
        movaps  xmmword ptr [rsp+0x50], xmm13
        paddd   xmm0, xmm1
        paddd   xmm8, xmm9
        pxor    xmm3, xmm0
        pxor    xmm11, xmm8
        movaps  xmm13, xmmword ptr [ROT8+rip]
        pshufb  xmm3, xmm13
        pshufb  xmm11, xmm13
        paddd   xmm2, xmm3
        paddd   xmm10, xmm11
        pxor    xmm1, xmm2
        pxor    xmm9, xmm10
        movdqa  xmm4, xmm1
        pslld   xmm1, 25
        psrld   xmm4, 7
        por     xmm1, xmm4
        movdqa  xmm4, xmm9
        pslld   xmm9, 25
        psrld   xmm4, 7
        por     xmm9, xmm4
        pshufd  xmm0, xmm0, 0x93
        pshufd  xmm8, xmm8, 0x93
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm11, xmm11, 0x4E
        pshufd  xmm2, xmm2, 0x39
        pshufd  xmm10, xmm10, 0x39
        paddd   xmm0, xmm6
        paddd   xmm8, xmm14
        paddd   xmm0, xmm1
        paddd   xmm8, xmm9
        pxor    xmm3, xmm0
        pxor    xmm11, xmm8
        pshufb  xmm3, xmm12
        pshufb  xmm11, xmm12
        paddd   xmm2, xmm3
        paddd   xmm10, xmm11
        pxor    xmm1, xmm2
        pxor    xmm9, xmm10
        movdqa  xmm4, xmm1
        pslld   xmm1, 20
        psrld   xmm4, 12
        por     xmm1, xmm4
        movdqa  xmm4, xmm9
        pslld   xmm9, 20
        psrld   xmm4, 12
        por     xmm9, xmm4
        paddd   xmm0, xmm7
        paddd   xmm8, xmm15
        paddd   xmm0, xmm1
        paddd   xmm8, xmm9
        pxor    xmm3, xmm0
        pxor    xmm11, xmm8
        pshufb  xmm3, xmm13
        pshufb  xmm11, xmm13
        paddd   xmm2, xmm3
        paddd   xmm10, xmm11
        pxor    xmm1, xmm2
        pxor    xmm9, xmm10
        movdqa  xmm4, xmm1
        pslld   xmm1, 25
        psrld   xmm4, 7
        por     xmm1, xmm4
        movdqa  xmm4, xmm9
        pslld   xmm9, 25
        psrld   xmm4, 7
        por     xmm9, xmm4
        pshufd  xmm0, xmm0, 0x39
        pshufd  xmm8, xmm8, 0x39
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm11, xmm11, 0x4E
        pshufd  xmm2, xmm2, 0x93
        pshufd  xmm10, xmm10, 0x93
        dec     al
        je      9f
        movdqa  xmm12, xmmword ptr [rsp+0x20]
        movdqa  xmm5, xmmword ptr [rsp+0x40]
        pshufd  xmm13, xmm12, 0x0F
        shufps  xmm12, xmm5, 214
        pshufd  xmm4, xmm12, 0x39
        movdqa  xmm12, xmm6
        shufps  xmm12, xmm7, 250
        pblendw xmm13, xmm12, 0xCC
        movdqa  xmm12, xmm7
        punpcklqdq xmm12, xmm5
        pblendw xmm12, xmm6, 0xC0
        pshufd  xmm12, xmm12, 0x78
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 0x1E
        movdqa  xmmword ptr [rsp+0x20], xmm13
        movdqa  xmmword ptr [rsp+0x40], xmm12
        movdqa  xmm5, xmmword ptr [rsp+0x30]
        movdqa  xmm13, xmmword ptr [rsp+0x50]
        pshufd  xmm6, xmm5, 0x0F
        shufps  xmm5, xmm13, 214
        pshufd  xmm12, xmm5, 0x39
        movdqa  xmm5, xmm14
        shufps  xmm5, xmm15, 250
        pblendw xmm6, xmm5, 0xCC
        movdqa  xmm5, xmm15
        punpcklqdq xmm5, xmm13
        pblendw xmm5, xmm14, 0xC0
        pshufd  xmm5, xmm5, 0x78
        punpckhdq xmm13, xmm15
        punpckldq xmm14, xmm13
        pshufd  xmm15, xmm14, 0x1E
        movdqa  xmm13, xmm6
        movdqa  xmm14, xmm5
        movdqa  xmm5, xmmword ptr [rsp+0x20]
        movdqa  xmm6, xmmword ptr [rsp+0x40]
        jmp     9b
9:
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        pxor    xmm8, xmm10
        pxor    xmm9, xmm11
        mov     eax, r13d
        cmp     rdx, r15
        jne     2b
        movups  xmmword ptr [rbx], xmm0
        movups  xmmword ptr [rbx+0x10], xmm1
        movups  xmmword ptr [rbx+0x20], xmm8
        movups  xmmword ptr [rbx+0x30], xmm9
        movdqa  xmm0, xmmword ptr [rsp+0x130]
        movdqa  xmm1, xmmword ptr [rsp+0x110]
        movdqa  xmm2, xmmword ptr [rsp+0x120]
        movdqu  xmm3, xmmword ptr [rsp+0x118]
        movdqu  xmm4, xmmword ptr [rsp+0x128]
        blendvps xmm1, xmm3, xmm0
        blendvps xmm2, xmm4, xmm0
        movdqa  xmmword ptr [rsp+0x110], xmm1
        movdqa  xmmword ptr [rsp+0x120], xmm2
        add     rdi, 16
        add     rbx, 64
        sub     rsi, 2
3:
        test    esi, 0x1
        je      4b
        movups  xmm0, xmmword ptr [rcx]
        movups  xmm1, xmmword ptr [rcx+0x10]
        movd    xmm13, dword ptr [rsp+0x110]
        pinsrd  xmm13, dword ptr [rsp+0x120], 1
        pinsrd  xmm13, dword ptr [BLAKE3_BLOCK_LEN+rip], 2
        movaps  xmm14, xmmword ptr [ROT8+rip]
        movaps  xmm15, xmmword ptr [ROT16+rip]
        mov     r8, qword ptr [rdi]
        movzx   eax, byte ptr [rbp+0x40]
        or      eax, r13d
        xor     edx, edx
2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        movaps  xmm2, xmmword ptr [BLAKE3_IV+rip]
        movaps  xmm3, xmm13
        pinsrd  xmm3, eax, 3
        movups  xmm4, xmmword ptr [r8+rdx-0x40]
        movups  xmm5, xmmword ptr [r8+rdx-0x30]
        movaps  xmm8, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm8, xmm5, 221
        movaps  xmm5, xmm8
        movups  xmm6, xmmword ptr [r8+rdx-0x20]
        movups  xmm7, xmmword ptr [r8+rdx-0x10]
        movaps  xmm8, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 0x93
        shufps  xmm8, xmm7, 221
        pshufd  xmm7, xmm8, 0x93
        mov     al, 7
9:
        paddd   xmm0, xmm4
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm15
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 20
        psrld   xmm11, 12
        por     xmm1, xmm11
        paddd   xmm0, xmm5
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm14
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 25
        psrld   xmm11, 7
        por     xmm1, xmm11
        pshufd  xmm0, xmm0, 0x93
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm2, xmm2, 0x39
        paddd   xmm0, xmm6
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm15
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 20
        psrld   xmm11, 12
        por     xmm1, xmm11
        paddd   xmm0, xmm7
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm14
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 25
        psrld   xmm11, 7
        por     xmm1, xmm11
        pshufd  xmm0, xmm0, 0x39
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm2, xmm2, 0x93
        dec     al
        jz      9f
        movdqa  xmm8, xmm4
        shufps  xmm8, xmm5, 214
        pshufd  xmm9, xmm4, 0x0F
        pshufd  xmm4, xmm8, 0x39
        movdqa  xmm8, xmm6
        shufps  xmm8, xmm7, 250
        pblendw xmm9, xmm8, 0xCC
        movdqa  xmm8, xmm7
        punpcklqdq xmm8, xmm5
        pblendw xmm8, xmm6, 0xC0
        pshufd  xmm8, xmm8, 0x78
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 0x1E
        movdqa  xmm5, xmm9
        movdqa  xmm6, xmm8
        jmp     9b
9:
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     2b
        movups  xmmword ptr [rbx], xmm0
        movups  xmmword ptr [rbx+0x10], xmm1
        jmp     4b

.p2align 6
blake3_compress_in_place_sse41:
_blake3_compress_in_place_sse41:
        _CET_ENDBR
        movups  xmm0, xmmword ptr [rdi]
        movups  xmm1, xmmword ptr [rdi+0x10]
        movaps  xmm2, xmmword ptr [BLAKE3_IV+rip]
        shl     r8, 32
        add     rdx, r8
        movq    xmm3, rcx
        movq    xmm4, rdx
        punpcklqdq xmm3, xmm4
        movups  xmm4, xmmword ptr [rsi]
        movups  xmm5, xmmword ptr [rsi+0x10]
        movaps  xmm8, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm8, xmm5, 221
        movaps  xmm5, xmm8
        movups  xmm6, xmmword ptr [rsi+0x20]
        movups  xmm7, xmmword ptr [rsi+0x30]
        movaps  xmm8, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 0x93
        shufps  xmm8, xmm7, 221
        pshufd  xmm7, xmm8, 0x93
        movaps  xmm14, xmmword ptr [ROT8+rip]
        movaps  xmm15, xmmword ptr [ROT16+rip]
        mov     al, 7
9:
        paddd   xmm0, xmm4
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm15
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 20
        psrld   xmm11, 12
        por     xmm1, xmm11
        paddd   xmm0, xmm5
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm14
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 25
        psrld   xmm11, 7
        por     xmm1, xmm11
        pshufd  xmm0, xmm0, 0x93
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm2, xmm2, 0x39
        paddd   xmm0, xmm6
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm15
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 20
        psrld   xmm11, 12
        por     xmm1, xmm11
        paddd   xmm0, xmm7
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm14
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 25
        psrld   xmm11, 7
        por     xmm1, xmm11
        pshufd  xmm0, xmm0, 0x39
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm2, xmm2, 0x93
        dec     al
        jz      9f
        movdqa  xmm8, xmm4
        shufps  xmm8, xmm5, 214
        pshufd  xmm9, xmm4, 0x0F
        pshufd  xmm4, xmm8, 0x39
        movdqa  xmm8, xmm6
        shufps  xmm8, xmm7, 250
        pblendw xmm9, xmm8, 0xCC
        movdqa  xmm8, xmm7
        punpcklqdq xmm8, xmm5
        pblendw xmm8, xmm6, 0xC0
        pshufd  xmm8, xmm8, 0x78
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 0x1E
        movdqa  xmm5, xmm9
        movdqa  xmm6, xmm8
        jmp     9b
9:
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        movups  xmmword ptr [rdi], xmm0
        movups  xmmword ptr [rdi+0x10], xmm1
        ret

.p2align 6
blake3_compress_xof_sse41:
_blake3_compress_xof_sse41:
        _CET_ENDBR
        movups  xmm0, xmmword ptr [rdi]
        movups  xmm1, xmmword ptr [rdi+0x10]
        movaps  xmm2, xmmword ptr [BLAKE3_IV+rip]
        movzx   eax, r8b
        movzx   edx, dl
        shl     rax, 32
        add     rdx, rax
        movq    xmm3, rcx
        movq    xmm4, rdx
        punpcklqdq xmm3, xmm4
        movups  xmm4, xmmword ptr [rsi]
        movups  xmm5, xmmword ptr [rsi+0x10]
        movaps  xmm8, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm8, xmm5, 221
        movaps  xmm5, xmm8
        movups  xmm6, xmmword ptr [rsi+0x20]
        movups  xmm7, xmmword ptr [rsi+0x30]
        movaps  xmm8, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 0x93
        shufps  xmm8, xmm7, 221
        pshufd  xmm7, xmm8, 0x93
        movaps  xmm14, xmmword ptr [ROT8+rip]
        movaps  xmm15, xmmword ptr [ROT16+rip]
        mov     al, 7
9:
        paddd   xmm0, xmm4
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm15
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 20
        psrld   xmm11, 12
        por     xmm1, xmm11
        paddd   xmm0, xmm5
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm14
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 25
        psrld   xmm11, 7
        por     xmm1, xmm11
        pshufd  xmm0, xmm0, 0x93
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm2, xmm2, 0x39
        paddd   xmm0, xmm6
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm15
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 20
        psrld   xmm11, 12
        por     xmm1, xmm11
        paddd   xmm0, xmm7
        paddd   xmm0, xmm1
        pxor    xmm3, xmm0
        pshufb  xmm3, xmm14
        paddd   xmm2, xmm3
        pxor    xmm1, xmm2
        movdqa  xmm11, xmm1
        pslld   xmm1, 25
        psrld   xmm11, 7
        por     xmm1, xmm11
        pshufd  xmm0, xmm0, 0x39
        pshufd  xmm3, xmm3, 0x4E
        pshufd  xmm2, xmm2, 0x93
        dec     al
        jz      9f
        movdqa  xmm8, xmm4
        shufps  xmm8, xmm5, 214
        pshufd  xmm9, xmm4, 0x0F
        pshufd  xmm4, xmm8, 0x39
        movdqa  xmm8, xmm6
        shufps  xmm8, xmm7, 250
        pblendw xmm9, xmm8, 0xCC
        movdqa  xmm8, xmm7
        punpcklqdq xmm8, xmm5
        pblendw xmm8, xmm6, 0xC0
        pshufd  xmm8, xmm8, 0x78
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 0x1E
        movdqa  xmm5, xmm9
        movdqa  xmm6, xmm8
        jmp     9b
9:
        movdqu  xmm4, xmmword ptr [rdi]
        movdqu  xmm5, xmmword ptr [rdi+0x10]
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        pxor    xmm2, xmm4
        pxor    xmm3, xmm5
        movups  xmmword ptr [r9], xmm0
        movups  xmmword ptr [r9+0x10], xmm1
        movups  xmmword ptr [r9+0x20], xmm2
        movups  xmmword ptr [r9+0x30], xmm3
        ret


#ifdef __APPLE__
.static_data
#else
.section .rodata
#endif
.p2align  6
BLAKE3_IV:
        .long  0x6A09E667, 0xBB67AE85
        .long  0x3C6EF372, 0xA54FF53A
ROT16:
        .byte  2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
ROT8:
        .byte  1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12
ADD0:	
        .long  0, 1, 2, 3
ADD1:
	.long  4, 4, 4, 4
BLAKE3_IV_0:
	.long  0x6A09E667, 0x6A09E667, 0x6A09E667, 0x6A09E667
BLAKE3_IV_1:
	.long  0xBB67AE85, 0xBB67AE85, 0xBB67AE85, 0xBB67AE85
BLAKE3_IV_2:
	.long  0x3C6EF372, 0x3C6EF372, 0x3C6EF372, 0x3C6EF372
BLAKE3_IV_3:
	.long  0xA54FF53A, 0xA54FF53A, 0xA54FF53A, 0xA54FF53A
BLAKE3_BLOCK_LEN:
	.long  64, 64, 64, 64
CMP_MSB_MASK:
	.long  0x80000000, 0x80000000, 0x80000000, 0x80000000

#endif
