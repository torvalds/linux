public _llvm_blake3_hash_many_sse41
public llvm_blake3_hash_many_sse41
public llvm_blake3_compress_in_place_sse41
public _llvm_blake3_compress_in_place_sse41
public llvm_blake3_compress_xof_sse41
public _llvm_blake3_compress_xof_sse41

_TEXT   SEGMENT ALIGN(16) 'CODE'

ALIGN   16
llvm_blake3_hash_many_sse41 PROC
_llvm_blake3_hash_many_sse41 PROC
        push    r15
        push    r14
        push    r13
        push    r12
        push    rsi
        push    rdi
        push    rbx
        push    rbp
        mov     rbp, rsp
        sub     rsp, 528
        and     rsp, 0FFFFFFFFFFFFFFC0H
        movdqa  xmmword ptr [rsp+170H], xmm6
        movdqa  xmmword ptr [rsp+180H], xmm7
        movdqa  xmmword ptr [rsp+190H], xmm8
        movdqa  xmmword ptr [rsp+1A0H], xmm9
        movdqa  xmmword ptr [rsp+1B0H], xmm10
        movdqa  xmmword ptr [rsp+1C0H], xmm11
        movdqa  xmmword ptr [rsp+1D0H], xmm12
        movdqa  xmmword ptr [rsp+1E0H], xmm13
        movdqa  xmmword ptr [rsp+1F0H], xmm14
        movdqa  xmmword ptr [rsp+200H], xmm15
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
        mov     rcx, r9
        mov     r8, qword ptr [rbp+68H]
        movzx   r9, byte ptr [rbp+70H]
        neg     r9d
        movd    xmm0, r9d
        pshufd  xmm0, xmm0, 00H
        movdqa  xmmword ptr [rsp+130H], xmm0
        movdqa  xmm1, xmm0
        pand    xmm1, xmmword ptr [ADD0]
        pand    xmm0, xmmword ptr [ADD1]
        movdqa  xmmword ptr [rsp+150H], xmm0
        movd    xmm0, r8d
        pshufd  xmm0, xmm0, 00H
        paddd   xmm0, xmm1
        movdqa  xmmword ptr [rsp+110H], xmm0
        pxor    xmm0, xmmword ptr [CMP_MSB_MASK]
        pxor    xmm1, xmmword ptr [CMP_MSB_MASK]
        pcmpgtd xmm1, xmm0
        shr     r8, 32
        movd    xmm2, r8d
        pshufd  xmm2, xmm2, 00H
        psubd   xmm2, xmm1
        movdqa  xmmword ptr [rsp+120H], xmm2
        mov     rbx, qword ptr [rbp+90H]
        mov     r15, rdx
        shl     r15, 6
        movzx   r13d, byte ptr [rbp+78H]
        movzx   r12d, byte ptr [rbp+88H]
        cmp     rsi, 4
        jc      final3blocks
outerloop4:
        movdqu  xmm3, xmmword ptr [rcx]
        pshufd  xmm0, xmm3, 00H
        pshufd  xmm1, xmm3, 55H
        pshufd  xmm2, xmm3, 0AAH
        pshufd  xmm3, xmm3, 0FFH
        movdqu  xmm7, xmmword ptr [rcx+10H]
        pshufd  xmm4, xmm7, 00H
        pshufd  xmm5, xmm7, 55H
        pshufd  xmm6, xmm7, 0AAH
        pshufd  xmm7, xmm7, 0FFH
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        mov     r10, qword ptr [rdi+10H]
        mov     r11, qword ptr [rdi+18H]
        movzx   eax, byte ptr [rbp+80H]
        or      eax, r13d
        xor     edx, edx
innerloop4:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        movdqu  xmm8, xmmword ptr [r8+rdx-40H]
        movdqu  xmm9, xmmword ptr [r9+rdx-40H]
        movdqu  xmm10, xmmword ptr [r10+rdx-40H]
        movdqu  xmm11, xmmword ptr [r11+rdx-40H]
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
        movdqa  xmmword ptr [rsp+10H], xmm9
        movdqa  xmmword ptr [rsp+20H], xmm12
        movdqa  xmmword ptr [rsp+30H], xmm13
        movdqu  xmm8, xmmword ptr [r8+rdx-30H]
        movdqu  xmm9, xmmword ptr [r9+rdx-30H]
        movdqu  xmm10, xmmword ptr [r10+rdx-30H]
        movdqu  xmm11, xmmword ptr [r11+rdx-30H]
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
        movdqa  xmmword ptr [rsp+40H], xmm8
        movdqa  xmmword ptr [rsp+50H], xmm9
        movdqa  xmmword ptr [rsp+60H], xmm12
        movdqa  xmmword ptr [rsp+70H], xmm13
        movdqu  xmm8, xmmword ptr [r8+rdx-20H]
        movdqu  xmm9, xmmword ptr [r9+rdx-20H]
        movdqu  xmm10, xmmword ptr [r10+rdx-20H]
        movdqu  xmm11, xmmword ptr [r11+rdx-20H]
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
        movdqa  xmmword ptr [rsp+80H], xmm8
        movdqa  xmmword ptr [rsp+90H], xmm9
        movdqa  xmmword ptr [rsp+0A0H], xmm12
        movdqa  xmmword ptr [rsp+0B0H], xmm13
        movdqu  xmm8, xmmword ptr [r8+rdx-10H]
        movdqu  xmm9, xmmword ptr [r9+rdx-10H]
        movdqu  xmm10, xmmword ptr [r10+rdx-10H]
        movdqu  xmm11, xmmword ptr [r11+rdx-10H]
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
        movdqa  xmmword ptr [rsp+0C0H], xmm8
        movdqa  xmmword ptr [rsp+0D0H], xmm9
        movdqa  xmmword ptr [rsp+0E0H], xmm12
        movdqa  xmmword ptr [rsp+0F0H], xmm13
        movdqa  xmm9, xmmword ptr [BLAKE3_IV_1]
        movdqa  xmm10, xmmword ptr [BLAKE3_IV_2]
        movdqa  xmm11, xmmword ptr [BLAKE3_IV_3]
        movdqa  xmm12, xmmword ptr [rsp+110H]
        movdqa  xmm13, xmmword ptr [rsp+120H]
        movdqa  xmm14, xmmword ptr [BLAKE3_BLOCK_LEN]
        movd    xmm15, eax
        pshufd  xmm15, xmm15, 00H
        prefetcht0 byte ptr [r8+rdx+80H]
        prefetcht0 byte ptr [r9+rdx+80H]
        prefetcht0 byte ptr [r10+rdx+80H]
        prefetcht0 byte ptr [r11+rdx+80H]
        paddd   xmm0, xmmword ptr [rsp]
        paddd   xmm1, xmmword ptr [rsp+20H]
        paddd   xmm2, xmmword ptr [rsp+40H]
        paddd   xmm3, xmmword ptr [rsp+60H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [BLAKE3_IV_0]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+10H]
        paddd   xmm1, xmmword ptr [rsp+30H]
        paddd   xmm2, xmmword ptr [rsp+50H]
        paddd   xmm3, xmmword ptr [rsp+70H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+80H]
        paddd   xmm1, xmmword ptr [rsp+0A0H]
        paddd   xmm2, xmmword ptr [rsp+0C0H]
        paddd   xmm3, xmmword ptr [rsp+0E0H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+90H]
        paddd   xmm1, xmmword ptr [rsp+0B0H]
        paddd   xmm2, xmmword ptr [rsp+0D0H]
        paddd   xmm3, xmmword ptr [rsp+0F0H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+20H]
        paddd   xmm1, xmmword ptr [rsp+30H]
        paddd   xmm2, xmmword ptr [rsp+70H]
        paddd   xmm3, xmmword ptr [rsp+40H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+60H]
        paddd   xmm1, xmmword ptr [rsp+0A0H]
        paddd   xmm2, xmmword ptr [rsp]
        paddd   xmm3, xmmword ptr [rsp+0D0H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+10H]
        paddd   xmm1, xmmword ptr [rsp+0C0H]
        paddd   xmm2, xmmword ptr [rsp+90H]
        paddd   xmm3, xmmword ptr [rsp+0F0H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0B0H]
        paddd   xmm1, xmmword ptr [rsp+50H]
        paddd   xmm2, xmmword ptr [rsp+0E0H]
        paddd   xmm3, xmmword ptr [rsp+80H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+30H]
        paddd   xmm1, xmmword ptr [rsp+0A0H]
        paddd   xmm2, xmmword ptr [rsp+0D0H]
        paddd   xmm3, xmmword ptr [rsp+70H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+40H]
        paddd   xmm1, xmmword ptr [rsp+0C0H]
        paddd   xmm2, xmmword ptr [rsp+20H]
        paddd   xmm3, xmmword ptr [rsp+0E0H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+60H]
        paddd   xmm1, xmmword ptr [rsp+90H]
        paddd   xmm2, xmmword ptr [rsp+0B0H]
        paddd   xmm3, xmmword ptr [rsp+80H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+50H]
        paddd   xmm1, xmmword ptr [rsp]
        paddd   xmm2, xmmword ptr [rsp+0F0H]
        paddd   xmm3, xmmword ptr [rsp+10H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0A0H]
        paddd   xmm1, xmmword ptr [rsp+0C0H]
        paddd   xmm2, xmmword ptr [rsp+0E0H]
        paddd   xmm3, xmmword ptr [rsp+0D0H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+70H]
        paddd   xmm1, xmmword ptr [rsp+90H]
        paddd   xmm2, xmmword ptr [rsp+30H]
        paddd   xmm3, xmmword ptr [rsp+0F0H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+40H]
        paddd   xmm1, xmmword ptr [rsp+0B0H]
        paddd   xmm2, xmmword ptr [rsp+50H]
        paddd   xmm3, xmmword ptr [rsp+10H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm1, xmmword ptr [rsp+20H]
        paddd   xmm2, xmmword ptr [rsp+80H]
        paddd   xmm3, xmmword ptr [rsp+60H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0C0H]
        paddd   xmm1, xmmword ptr [rsp+90H]
        paddd   xmm2, xmmword ptr [rsp+0F0H]
        paddd   xmm3, xmmword ptr [rsp+0E0H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0D0H]
        paddd   xmm1, xmmword ptr [rsp+0B0H]
        paddd   xmm2, xmmword ptr [rsp+0A0H]
        paddd   xmm3, xmmword ptr [rsp+80H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+70H]
        paddd   xmm1, xmmword ptr [rsp+50H]
        paddd   xmm2, xmmword ptr [rsp]
        paddd   xmm3, xmmword ptr [rsp+60H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+20H]
        paddd   xmm1, xmmword ptr [rsp+30H]
        paddd   xmm2, xmmword ptr [rsp+10H]
        paddd   xmm3, xmmword ptr [rsp+40H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+90H]
        paddd   xmm1, xmmword ptr [rsp+0B0H]
        paddd   xmm2, xmmword ptr [rsp+80H]
        paddd   xmm3, xmmword ptr [rsp+0F0H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0E0H]
        paddd   xmm1, xmmword ptr [rsp+50H]
        paddd   xmm2, xmmword ptr [rsp+0C0H]
        paddd   xmm3, xmmword ptr [rsp+10H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0D0H]
        paddd   xmm1, xmmword ptr [rsp]
        paddd   xmm2, xmmword ptr [rsp+20H]
        paddd   xmm3, xmmword ptr [rsp+40H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+30H]
        paddd   xmm1, xmmword ptr [rsp+0A0H]
        paddd   xmm2, xmmword ptr [rsp+60H]
        paddd   xmm3, xmmword ptr [rsp+70H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0B0H]
        paddd   xmm1, xmmword ptr [rsp+50H]
        paddd   xmm2, xmmword ptr [rsp+10H]
        paddd   xmm3, xmmword ptr [rsp+80H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0F0H]
        paddd   xmm1, xmmword ptr [rsp]
        paddd   xmm2, xmmword ptr [rsp+90H]
        paddd   xmm3, xmmword ptr [rsp+60H]
        paddd   xmm0, xmm4
        paddd   xmm1, xmm5
        paddd   xmm2, xmm6
        paddd   xmm3, xmm7
        pxor    xmm12, xmm0
        pxor    xmm13, xmm1
        pxor    xmm14, xmm2
        pxor    xmm15, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        pshufb  xmm15, xmm8
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm12
        paddd   xmm9, xmm13
        paddd   xmm10, xmm14
        paddd   xmm11, xmm15
        pxor    xmm4, xmm8
        pxor    xmm5, xmm9
        pxor    xmm6, xmm10
        pxor    xmm7, xmm11
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0E0H]
        paddd   xmm1, xmmword ptr [rsp+20H]
        paddd   xmm2, xmmword ptr [rsp+30H]
        paddd   xmm3, xmmword ptr [rsp+70H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT16]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
        paddd   xmm8, xmm13
        paddd   xmm9, xmm14
        pxor    xmm5, xmm10
        pxor    xmm6, xmm11
        pxor    xmm7, xmm8
        pxor    xmm4, xmm9
        movdqa  xmmword ptr [rsp+100H], xmm8
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
        paddd   xmm0, xmmword ptr [rsp+0A0H]
        paddd   xmm1, xmmword ptr [rsp+0C0H]
        paddd   xmm2, xmmword ptr [rsp+40H]
        paddd   xmm3, xmmword ptr [rsp+0D0H]
        paddd   xmm0, xmm5
        paddd   xmm1, xmm6
        paddd   xmm2, xmm7
        paddd   xmm3, xmm4
        pxor    xmm15, xmm0
        pxor    xmm12, xmm1
        pxor    xmm13, xmm2
        pxor    xmm14, xmm3
        movdqa  xmm8, xmmword ptr [ROT8]
        pshufb  xmm15, xmm8
        pshufb  xmm12, xmm8
        pshufb  xmm13, xmm8
        pshufb  xmm14, xmm8
        paddd   xmm10, xmm15
        paddd   xmm11, xmm12
        movdqa  xmm8, xmmword ptr [rsp+100H]
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
        jne     innerloop4
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
        movdqu  xmmword ptr [rbx+20H], xmm1
        movdqu  xmmword ptr [rbx+40H], xmm9
        movdqu  xmmword ptr [rbx+60H], xmm3
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
        movdqu  xmmword ptr [rbx+10H], xmm4
        movdqu  xmmword ptr [rbx+30H], xmm5
        movdqu  xmmword ptr [rbx+50H], xmm9
        movdqu  xmmword ptr [rbx+70H], xmm7
        movdqa  xmm1, xmmword ptr [rsp+110H]
        movdqa  xmm0, xmm1
        paddd   xmm1, xmmword ptr [rsp+150H]
        movdqa  xmmword ptr [rsp+110H], xmm1
        pxor    xmm0, xmmword ptr [CMP_MSB_MASK]
        pxor    xmm1, xmmword ptr [CMP_MSB_MASK]
        pcmpgtd xmm0, xmm1
        movdqa  xmm1, xmmword ptr [rsp+120H]
        psubd   xmm1, xmm0
        movdqa  xmmword ptr [rsp+120H], xmm1
        add     rbx, 128
        add     rdi, 32
        sub     rsi, 4
        cmp     rsi, 4
        jnc     outerloop4
        test    rsi, rsi
        jne     final3blocks
unwind:
        movdqa  xmm6, xmmword ptr [rsp+170H]
        movdqa  xmm7, xmmword ptr [rsp+180H]
        movdqa  xmm8, xmmword ptr [rsp+190H]
        movdqa  xmm9, xmmword ptr [rsp+1A0H]
        movdqa  xmm10, xmmword ptr [rsp+1B0H]
        movdqa  xmm11, xmmword ptr [rsp+1C0H]
        movdqa  xmm12, xmmword ptr [rsp+1D0H]
        movdqa  xmm13, xmmword ptr [rsp+1E0H]
        movdqa  xmm14, xmmword ptr [rsp+1F0H]
        movdqa  xmm15, xmmword ptr [rsp+200H]
        mov     rsp, rbp
        pop     rbp
        pop     rbx
        pop     rdi
        pop     rsi
        pop     r12
        pop     r13
        pop     r14
        pop     r15
        ret
ALIGN   16
final3blocks:
        test    esi, 2H
        je      final1block
        movups  xmm0, xmmword ptr [rcx]
        movups  xmm1, xmmword ptr [rcx+10H]
        movaps  xmm8, xmm0
        movaps  xmm9, xmm1
        movd    xmm13, dword ptr [rsp+110H]
        pinsrd  xmm13, dword ptr [rsp+120H], 1
        pinsrd  xmm13, dword ptr [BLAKE3_BLOCK_LEN], 2
        movaps  xmmword ptr [rsp], xmm13
        movd    xmm14, dword ptr [rsp+114H]
        pinsrd  xmm14, dword ptr [rsp+124H], 1
        pinsrd  xmm14, dword ptr [BLAKE3_BLOCK_LEN], 2
        movaps  xmmword ptr [rsp+10H], xmm14
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        movzx   eax, byte ptr [rbp+80H]
        or      eax, r13d
        xor     edx, edx
innerloop2:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        movaps  xmm2, xmmword ptr [BLAKE3_IV]
        movaps  xmm10, xmm2
        movups  xmm4, xmmword ptr [r8+rdx-40H]
        movups  xmm5, xmmword ptr [r8+rdx-30H]
        movaps  xmm3, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm3, xmm5, 221
        movaps  xmm5, xmm3
        movups  xmm6, xmmword ptr [r8+rdx-20H]
        movups  xmm7, xmmword ptr [r8+rdx-10H]
        movaps  xmm3, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 93H
        shufps  xmm3, xmm7, 221
        pshufd  xmm7, xmm3, 93H
        movups  xmm12, xmmword ptr [r9+rdx-40H]
        movups  xmm13, xmmword ptr [r9+rdx-30H]
        movaps  xmm11, xmm12
        shufps  xmm12, xmm13, 136
        shufps  xmm11, xmm13, 221
        movaps  xmm13, xmm11
        movups  xmm14, xmmword ptr [r9+rdx-20H]
        movups  xmm15, xmmword ptr [r9+rdx-10H]
        movaps  xmm11, xmm14
        shufps  xmm14, xmm15, 136
        pshufd  xmm14, xmm14, 93H
        shufps  xmm11, xmm15, 221
        pshufd  xmm15, xmm11, 93H
        movaps  xmm3, xmmword ptr [rsp]
        movaps  xmm11, xmmword ptr [rsp+10H]
        pinsrd  xmm3, eax, 3
        pinsrd  xmm11, eax, 3
        mov     al, 7
roundloop2:
        paddd   xmm0, xmm4
        paddd   xmm8, xmm12
        movaps  xmmword ptr [rsp+20H], xmm4
        movaps  xmmword ptr [rsp+30H], xmm12
        paddd   xmm0, xmm1
        paddd   xmm8, xmm9
        pxor    xmm3, xmm0
        pxor    xmm11, xmm8
        movaps  xmm12, xmmword ptr [ROT16]
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
        movaps  xmmword ptr [rsp+40H], xmm5
        movaps  xmmword ptr [rsp+50H], xmm13
        paddd   xmm0, xmm1
        paddd   xmm8, xmm9
        pxor    xmm3, xmm0
        pxor    xmm11, xmm8
        movaps  xmm13, xmmword ptr [ROT8]
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
        pshufd  xmm0, xmm0, 93H
        pshufd  xmm8, xmm8, 93H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm11, xmm11, 4EH
        pshufd  xmm2, xmm2, 39H
        pshufd  xmm10, xmm10, 39H
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
        pshufd  xmm0, xmm0, 39H
        pshufd  xmm8, xmm8, 39H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm11, xmm11, 4EH
        pshufd  xmm2, xmm2, 93H
        pshufd  xmm10, xmm10, 93H
        dec     al
        je      endroundloop2
        movdqa  xmm12, xmmword ptr [rsp+20H]
        movdqa  xmm5, xmmword ptr [rsp+40H]
        pshufd  xmm13, xmm12, 0FH
        shufps  xmm12, xmm5, 214
        pshufd  xmm4, xmm12, 39H
        movdqa  xmm12, xmm6
        shufps  xmm12, xmm7, 250
        pblendw xmm13, xmm12, 0CCH
        movdqa  xmm12, xmm7
        punpcklqdq xmm12, xmm5
        pblendw xmm12, xmm6, 0C0H
        pshufd  xmm12, xmm12, 78H
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 1EH
        movdqa  xmmword ptr [rsp+20H], xmm13
        movdqa  xmmword ptr [rsp+40H], xmm12
        movdqa  xmm5, xmmword ptr [rsp+30H]
        movdqa  xmm13, xmmword ptr [rsp+50H]
        pshufd  xmm6, xmm5, 0FH
        shufps  xmm5, xmm13, 214
        pshufd  xmm12, xmm5, 39H
        movdqa  xmm5, xmm14
        shufps  xmm5, xmm15, 250
        pblendw xmm6, xmm5, 0CCH
        movdqa  xmm5, xmm15
        punpcklqdq xmm5, xmm13
        pblendw xmm5, xmm14, 0C0H
        pshufd  xmm5, xmm5, 78H
        punpckhdq xmm13, xmm15
        punpckldq xmm14, xmm13
        pshufd  xmm15, xmm14, 1EH
        movdqa  xmm13, xmm6
        movdqa  xmm14, xmm5
        movdqa  xmm5, xmmword ptr [rsp+20H]
        movdqa  xmm6, xmmword ptr [rsp+40H]
        jmp     roundloop2
endroundloop2:
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        pxor    xmm8, xmm10
        pxor    xmm9, xmm11
        mov     eax, r13d
        cmp     rdx, r15
        jne     innerloop2
        movups  xmmword ptr [rbx], xmm0
        movups  xmmword ptr [rbx+10H], xmm1
        movups  xmmword ptr [rbx+20H], xmm8
        movups  xmmword ptr [rbx+30H], xmm9
        movdqa  xmm0, xmmword ptr [rsp+130H]
        movdqa  xmm1, xmmword ptr [rsp+110H]
        movdqa  xmm2, xmmword ptr [rsp+120H]
        movdqu  xmm3, xmmword ptr [rsp+118H]
        movdqu  xmm4, xmmword ptr [rsp+128H]
        blendvps xmm1, xmm3, xmm0
        blendvps xmm2, xmm4, xmm0
        movdqa  xmmword ptr [rsp+110H], xmm1
        movdqa  xmmword ptr [rsp+120H], xmm2
        add     rdi, 16
        add     rbx, 64
        sub     rsi, 2
final1block:
        test    esi, 1H
        je      unwind
        movups  xmm0, xmmword ptr [rcx]
        movups  xmm1, xmmword ptr [rcx+10H]
        movd    xmm13, dword ptr [rsp+110H]
        pinsrd  xmm13, dword ptr [rsp+120H], 1
        pinsrd  xmm13, dword ptr [BLAKE3_BLOCK_LEN], 2
        movaps  xmm14, xmmword ptr [ROT8]
        movaps  xmm15, xmmword ptr [ROT16]
        mov     r8, qword ptr [rdi]
        movzx   eax, byte ptr [rbp+80H]
        or      eax, r13d
        xor     edx, edx
innerloop1:
        mov     r14d, eax
        or      eax, r12d
        add     rdx, 64
        cmp     rdx, r15
        cmovne  eax, r14d
        movaps  xmm2, xmmword ptr [BLAKE3_IV]
        movaps  xmm3, xmm13
        pinsrd  xmm3, eax, 3
        movups  xmm4, xmmword ptr [r8+rdx-40H]
        movups  xmm5, xmmword ptr [r8+rdx-30H]
        movaps  xmm8, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm8, xmm5, 221
        movaps  xmm5, xmm8
        movups  xmm6, xmmword ptr [r8+rdx-20H]
        movups  xmm7, xmmword ptr [r8+rdx-10H]
        movaps  xmm8, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 93H
        shufps  xmm8, xmm7, 221
        pshufd  xmm7, xmm8, 93H
        mov     al, 7
roundloop1:
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
        pshufd  xmm0, xmm0, 93H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm2, xmm2, 39H
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
        pshufd  xmm0, xmm0, 39H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm2, xmm2, 93H
        dec     al
        jz      endroundloop1
        movdqa  xmm8, xmm4
        shufps  xmm8, xmm5, 214
        pshufd  xmm9, xmm4, 0FH
        pshufd  xmm4, xmm8, 39H
        movdqa  xmm8, xmm6
        shufps  xmm8, xmm7, 250
        pblendw xmm9, xmm8, 0CCH
        movdqa  xmm8, xmm7
        punpcklqdq xmm8, xmm5
        pblendw xmm8, xmm6, 0C0H
        pshufd  xmm8, xmm8, 78H
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 1EH
        movdqa  xmm5, xmm9
        movdqa  xmm6, xmm8
        jmp     roundloop1
endroundloop1:
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        mov     eax, r13d
        cmp     rdx, r15
        jne     innerloop1
        movups  xmmword ptr [rbx], xmm0
        movups  xmmword ptr [rbx+10H], xmm1
        jmp     unwind
_llvm_blake3_hash_many_sse41 ENDP
llvm_blake3_hash_many_sse41 ENDP

llvm_blake3_compress_in_place_sse41 PROC
_llvm_blake3_compress_in_place_sse41 PROC
        sub     rsp, 120
        movdqa  xmmword ptr [rsp], xmm6
        movdqa  xmmword ptr [rsp+10H], xmm7
        movdqa  xmmword ptr [rsp+20H], xmm8
        movdqa  xmmword ptr [rsp+30H], xmm9
        movdqa  xmmword ptr [rsp+40H], xmm11
        movdqa  xmmword ptr [rsp+50H], xmm14
        movdqa  xmmword ptr [rsp+60H], xmm15
        movups  xmm0, xmmword ptr [rcx]
        movups  xmm1, xmmword ptr [rcx+10H]
        movaps  xmm2, xmmword ptr [BLAKE3_IV]
        movzx   eax, byte ptr [rsp+0A0H]
        movzx   r8d, r8b
        shl     rax, 32
        add     r8, rax
        movd    xmm3, r9
        movd    xmm4, r8
        punpcklqdq xmm3, xmm4
        movups  xmm4, xmmword ptr [rdx]
        movups  xmm5, xmmword ptr [rdx+10H]
        movaps  xmm8, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm8, xmm5, 221
        movaps  xmm5, xmm8
        movups  xmm6, xmmword ptr [rdx+20H]
        movups  xmm7, xmmword ptr [rdx+30H]
        movaps  xmm8, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 93H
        shufps  xmm8, xmm7, 221
        pshufd  xmm7, xmm8, 93H
        movaps  xmm14, xmmword ptr [ROT8]
        movaps  xmm15, xmmword ptr [ROT16]
        mov     al, 7
@@:
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
        pshufd  xmm0, xmm0, 93H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm2, xmm2, 39H
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
        pshufd  xmm0, xmm0, 39H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm2, xmm2, 93H
        dec     al
        jz      @F
        movdqa  xmm8, xmm4
        shufps  xmm8, xmm5, 214
        pshufd  xmm9, xmm4, 0FH
        pshufd  xmm4, xmm8, 39H
        movdqa  xmm8, xmm6
        shufps  xmm8, xmm7, 250
        pblendw xmm9, xmm8, 0CCH
        movdqa  xmm8, xmm7
        punpcklqdq xmm8, xmm5
        pblendw xmm8, xmm6, 0C0H
        pshufd  xmm8, xmm8, 78H
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 1EH
        movdqa  xmm5, xmm9
        movdqa  xmm6, xmm8
        jmp     @B
@@:
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        movups  xmmword ptr [rcx], xmm0
        movups  xmmword ptr [rcx+10H], xmm1
        movdqa  xmm6, xmmword ptr [rsp]
        movdqa  xmm7, xmmword ptr [rsp+10H]
        movdqa  xmm8, xmmword ptr [rsp+20H]
        movdqa  xmm9, xmmword ptr [rsp+30H]
        movdqa  xmm11, xmmword ptr [rsp+40H]
        movdqa  xmm14, xmmword ptr [rsp+50H]
        movdqa  xmm15, xmmword ptr [rsp+60H]
        add     rsp, 120
        ret
_llvm_blake3_compress_in_place_sse41 ENDP
llvm_blake3_compress_in_place_sse41 ENDP

ALIGN 16
llvm_blake3_compress_xof_sse41 PROC
_llvm_blake3_compress_xof_sse41 PROC
        sub     rsp, 120
        movdqa  xmmword ptr [rsp], xmm6
        movdqa  xmmword ptr [rsp+10H], xmm7
        movdqa  xmmword ptr [rsp+20H], xmm8
        movdqa  xmmword ptr [rsp+30H], xmm9
        movdqa  xmmword ptr [rsp+40H], xmm11
        movdqa  xmmword ptr [rsp+50H], xmm14
        movdqa  xmmword ptr [rsp+60H], xmm15
        movups  xmm0, xmmword ptr [rcx]
        movups  xmm1, xmmword ptr [rcx+10H]
        movaps  xmm2, xmmword ptr [BLAKE3_IV]
        movzx   eax, byte ptr [rsp+0A0H]
        movzx   r8d, r8b
        mov     r10, qword ptr [rsp+0A8H]
        shl     rax, 32
        add     r8, rax
        movd    xmm3, r9
        movd    xmm4, r8
        punpcklqdq xmm3, xmm4
        movups  xmm4, xmmword ptr [rdx]
        movups  xmm5, xmmword ptr [rdx+10H]
        movaps  xmm8, xmm4
        shufps  xmm4, xmm5, 136
        shufps  xmm8, xmm5, 221
        movaps  xmm5, xmm8
        movups  xmm6, xmmword ptr [rdx+20H]
        movups  xmm7, xmmword ptr [rdx+30H]
        movaps  xmm8, xmm6
        shufps  xmm6, xmm7, 136
        pshufd  xmm6, xmm6, 93H
        shufps  xmm8, xmm7, 221
        pshufd  xmm7, xmm8, 93H
        movaps  xmm14, xmmword ptr [ROT8]
        movaps  xmm15, xmmword ptr [ROT16]
        mov     al, 7
@@:
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
        pshufd  xmm0, xmm0, 93H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm2, xmm2, 39H
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
        pshufd  xmm0, xmm0, 39H
        pshufd  xmm3, xmm3, 4EH
        pshufd  xmm2, xmm2, 93H
        dec     al
        jz      @F
        movdqa  xmm8, xmm4
        shufps  xmm8, xmm5, 214
        pshufd  xmm9, xmm4, 0FH
        pshufd  xmm4, xmm8, 39H
        movdqa  xmm8, xmm6
        shufps  xmm8, xmm7, 250
        pblendw xmm9, xmm8, 0CCH
        movdqa  xmm8, xmm7
        punpcklqdq xmm8, xmm5
        pblendw xmm8, xmm6, 0C0H
        pshufd  xmm8, xmm8, 78H
        punpckhdq xmm5, xmm7
        punpckldq xmm6, xmm5
        pshufd  xmm7, xmm6, 1EH
        movdqa  xmm5, xmm9
        movdqa  xmm6, xmm8
        jmp     @B
@@:
        movdqu  xmm4, xmmword ptr [rcx]
        movdqu  xmm5, xmmword ptr [rcx+10H]
        pxor    xmm0, xmm2
        pxor    xmm1, xmm3
        pxor    xmm2, xmm4
        pxor    xmm3, xmm5
        movups  xmmword ptr [r10], xmm0
        movups  xmmword ptr [r10+10H], xmm1
        movups  xmmword ptr [r10+20H], xmm2
        movups  xmmword ptr [r10+30H], xmm3
        movdqa  xmm6, xmmword ptr [rsp]
        movdqa  xmm7, xmmword ptr [rsp+10H]
        movdqa  xmm8, xmmword ptr [rsp+20H]
        movdqa  xmm9, xmmword ptr [rsp+30H]
        movdqa  xmm11, xmmword ptr [rsp+40H]
        movdqa  xmm14, xmmword ptr [rsp+50H]
        movdqa  xmm15, xmmword ptr [rsp+60H]
        add     rsp, 120
        ret
_llvm_blake3_compress_xof_sse41 ENDP
llvm_blake3_compress_xof_sse41 ENDP

_TEXT ENDS


_RDATA SEGMENT READONLY PAGE ALIAS(".rdata") 'CONST'
ALIGN   64
BLAKE3_IV:
        dd 6A09E667H, 0BB67AE85H, 3C6EF372H, 0A54FF53AH

ADD0:
        dd 0, 1, 2, 3

ADD1:
        dd 4 dup (4)

BLAKE3_IV_0:
        dd 4 dup (6A09E667H)

BLAKE3_IV_1:
        dd 4 dup (0BB67AE85H)

BLAKE3_IV_2:
        dd 4 dup (3C6EF372H)

BLAKE3_IV_3:
        dd 4 dup (0A54FF53AH)

BLAKE3_BLOCK_LEN:
        dd 4 dup (64)

ROT16:
        db 2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13

ROT8:
        db 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12

CMP_MSB_MASK:
        dd 8 dup(80000000H)

_RDATA ENDS
END

