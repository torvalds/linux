public _llvm_blake3_hash_many_avx2
public llvm_blake3_hash_many_avx2

_TEXT   SEGMENT ALIGN(16) 'CODE'

ALIGN   16
llvm_blake3_hash_many_avx2 PROC
_llvm_blake3_hash_many_avx2 PROC
        push    r15
        push    r14
        push    r13
        push    r12
        push    rsi
        push    rdi
        push    rbx
        push    rbp
        mov     rbp, rsp
        sub     rsp, 880
        and     rsp, 0FFFFFFFFFFFFFFC0H
        vmovdqa xmmword ptr [rsp+2D0H], xmm6
        vmovdqa xmmword ptr [rsp+2E0H], xmm7
        vmovdqa xmmword ptr [rsp+2F0H], xmm8
        vmovdqa xmmword ptr [rsp+300H], xmm9
        vmovdqa xmmword ptr [rsp+310H], xmm10
        vmovdqa xmmword ptr [rsp+320H], xmm11
        vmovdqa xmmword ptr [rsp+330H], xmm12
        vmovdqa xmmword ptr [rsp+340H], xmm13
        vmovdqa xmmword ptr [rsp+350H], xmm14
        vmovdqa xmmword ptr [rsp+360H], xmm15
        mov     rdi, rcx
        mov     rsi, rdx
        mov     rdx, r8
        mov     rcx, r9
        mov     r8, qword ptr [rbp+68H]
        movzx   r9, byte ptr [rbp+70H]
        neg     r9d
        vmovd   xmm0, r9d
        vpbroadcastd ymm0, xmm0
        vmovdqa ymmword ptr [rsp+260H], ymm0
        vpand   ymm1, ymm0, ymmword ptr [ADD0]
        vpand   ymm2, ymm0, ymmword ptr [ADD1]
        vmovdqa ymmword ptr [rsp+2A0H], ymm2
        vmovd   xmm2, r8d
        vpbroadcastd ymm2, xmm2
        vpaddd  ymm2, ymm2, ymm1
        vmovdqa ymmword ptr [rsp+220H], ymm2
        vpxor   ymm1, ymm1, ymmword ptr [CMP_MSB_MASK]
        vpxor   ymm2, ymm2, ymmword ptr [CMP_MSB_MASK]
        vpcmpgtd ymm2, ymm1, ymm2
        shr     r8, 32
        vmovd   xmm3, r8d
        vpbroadcastd ymm3, xmm3
        vpsubd  ymm3, ymm3, ymm2
        vmovdqa ymmword ptr [rsp+240H], ymm3
        shl     rdx, 6
        mov     qword ptr [rsp+2C0H], rdx
        cmp     rsi, 8
        jc      final7blocks
outerloop8:
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
ALIGN   16
innerloop8:
        movzx   ebx, byte ptr [rbp+88H]
        or      ebx, eax
        add     rdx, 64
        cmp     rdx, qword ptr [rsp+2C0H]
        cmove   eax, ebx
        mov     dword ptr [rsp+200H], eax
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+20H], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+40H], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+60H], ymm11
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp+80H], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+0A0H], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+0C0H], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+0E0H], ymm11
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp+100H], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+120H], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+140H], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+160H], ymm11
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
        vshufps ymm8, ymm12, ymm14, 136
        vmovaps ymmword ptr [rsp+180H], ymm8
        vshufps ymm9, ymm12, ymm14, 221
        vmovaps ymmword ptr [rsp+1A0H], ymm9
        vshufps ymm10, ymm13, ymm15, 136
        vmovaps ymmword ptr [rsp+1C0H], ymm10
        vshufps ymm11, ymm13, ymm15, 221
        vmovaps ymmword ptr [rsp+1E0H], ymm11
        vpbroadcastd ymm15, dword ptr [rsp+200H]
        prefetcht0 byte ptr [r8+rdx+80H]
        prefetcht0 byte ptr [r12+rdx+80H]
        prefetcht0 byte ptr [r9+rdx+80H]
        prefetcht0 byte ptr [r13+rdx+80H]
        prefetcht0 byte ptr [r10+rdx+80H]
        prefetcht0 byte ptr [r14+rdx+80H]
        prefetcht0 byte ptr [r11+rdx+80H]
        prefetcht0 byte ptr [r15+rdx+80H]
        vpaddd  ymm0, ymm0, ymmword ptr [rsp]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+40H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+80H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0C0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm0, ymmword ptr [rsp+220H]
        vpxor   ymm13, ymm1, ymmword ptr [rsp+240H]
        vpxor   ymm14, ymm2, ymmword ptr [BLAKE3_BLOCK_LEN]
        vpxor   ymm15, ymm3, ymm15
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [BLAKE3_IV_0]
        vpaddd  ymm9, ymm13, ymmword ptr [BLAKE3_IV_1]
        vpaddd  ymm10, ymm14, ymmword ptr [BLAKE3_IV_2]
        vpaddd  ymm11, ymm15, ymmword ptr [BLAKE3_IV_3]
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+20H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+60H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0A0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0E0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+100H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+140H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+180H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1C0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+120H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+160H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+1A0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1E0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+40H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+60H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0E0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+80H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0C0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+140H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1A0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+20H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+180H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+120H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1E0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+160H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0A0H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+1C0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+100H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+60H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+140H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+1A0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0E0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+80H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+180H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+40H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1C0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0C0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+120H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+160H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+100H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0A0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+1E0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+20H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+140H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+180H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+1C0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1A0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0E0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+120H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+60H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1E0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+80H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+160H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0A0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+20H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+40H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+100H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0C0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+180H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+120H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+1E0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1C0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+1A0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+160H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+140H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+100H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+0E0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0A0H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0C0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+40H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+60H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+20H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+80H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+120H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+160H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+100H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1E0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+1C0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0A0H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+180H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+20H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+1A0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+40H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+80H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+60H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+140H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+0C0H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0E0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+160H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+0A0H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+20H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+100H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+1E0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+120H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0C0H]
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm1, ymm1, ymm5
        vpaddd  ymm2, ymm2, ymm6
        vpaddd  ymm3, ymm3, ymm7
        vpxor   ymm12, ymm12, ymm0
        vpxor   ymm13, ymm13, ymm1
        vpxor   ymm14, ymm14, ymm2
        vpxor   ymm15, ymm15, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpshufb ymm15, ymm15, ymm8
        vpaddd  ymm8, ymm12, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm13
        vpaddd  ymm10, ymm10, ymm14
        vpaddd  ymm11, ymm11, ymm15
        vpxor   ymm4, ymm4, ymm8
        vpxor   ymm5, ymm5, ymm9
        vpxor   ymm6, ymm6, ymm10
        vpxor   ymm7, ymm7, ymm11
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+1C0H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+40H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+60H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+0E0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT16]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
        vpaddd  ymm9, ymm9, ymm14
        vpxor   ymm5, ymm5, ymm10
        vpxor   ymm6, ymm6, ymm11
        vpxor   ymm7, ymm7, ymm8
        vpxor   ymm4, ymm4, ymm9
        vmovdqa ymmword ptr [rsp+200H], ymm8
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
        vpaddd  ymm0, ymm0, ymmword ptr [rsp+140H]
        vpaddd  ymm1, ymm1, ymmword ptr [rsp+180H]
        vpaddd  ymm2, ymm2, ymmword ptr [rsp+80H]
        vpaddd  ymm3, ymm3, ymmword ptr [rsp+1A0H]
        vpaddd  ymm0, ymm0, ymm5
        vpaddd  ymm1, ymm1, ymm6
        vpaddd  ymm2, ymm2, ymm7
        vpaddd  ymm3, ymm3, ymm4
        vpxor   ymm15, ymm15, ymm0
        vpxor   ymm12, ymm12, ymm1
        vpxor   ymm13, ymm13, ymm2
        vpxor   ymm14, ymm14, ymm3
        vbroadcasti128 ymm8, xmmword ptr [ROT8]
        vpshufb ymm15, ymm15, ymm8
        vpshufb ymm12, ymm12, ymm8
        vpshufb ymm13, ymm13, ymm8
        vpshufb ymm14, ymm14, ymm8
        vpaddd  ymm10, ymm10, ymm15
        vpaddd  ymm11, ymm11, ymm12
        vpaddd  ymm8, ymm13, ymmword ptr [rsp+200H]
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
        vmovdqa ymm0, ymmword ptr [rsp+2A0H]
        vpaddd  ymm1, ymm0, ymmword ptr [rsp+220H]
        vmovdqa ymmword ptr [rsp+220H], ymm1
        vpxor   ymm0, ymm0, ymmword ptr [CMP_MSB_MASK]
        vpxor   ymm2, ymm1, ymmword ptr [CMP_MSB_MASK]
        vpcmpgtd ymm2, ymm0, ymm2
        vmovdqa ymm0, ymmword ptr [rsp+240H]
        vpsubd  ymm2, ymm0, ymm2
        vmovdqa ymmword ptr [rsp+240H], ymm2
        add     rdi, 64
        add     rbx, 256
        mov     qword ptr [rbp+90H], rbx
        sub     rsi, 8
        cmp     rsi, 8
        jnc     outerloop8
        test    rsi, rsi
        jnz     final7blocks
unwind:
        vzeroupper
        vmovdqa xmm6, xmmword ptr [rsp+2D0H]
        vmovdqa xmm7, xmmword ptr [rsp+2E0H]
        vmovdqa xmm8, xmmword ptr [rsp+2F0H]
        vmovdqa xmm9, xmmword ptr [rsp+300H]
        vmovdqa xmm10, xmmword ptr [rsp+310H]
        vmovdqa xmm11, xmmword ptr [rsp+320H]
        vmovdqa xmm12, xmmword ptr [rsp+330H]
        vmovdqa xmm13, xmmword ptr [rsp+340H]
        vmovdqa xmm14, xmmword ptr [rsp+350H]
        vmovdqa xmm15, xmmword ptr [rsp+360H]
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
final7blocks:
        mov     rbx, qword ptr [rbp+90H]
        mov     r15, qword ptr [rsp+2C0H]
        movzx   r13d, byte ptr [rbp+78H]
        movzx   r12d, byte ptr [rbp+88H]
        test    rsi, 4H
        je      final3blocks
        vbroadcasti128 ymm0, xmmword ptr [rcx]
        vbroadcasti128 ymm1, xmmword ptr [rcx+10H]
        vmovdqa ymm8, ymm0
        vmovdqa ymm9, ymm1
        vbroadcasti128 ymm12, xmmword ptr [rsp+220H]
        vbroadcasti128 ymm13, xmmword ptr [rsp+240H]
        vpunpckldq ymm14, ymm12, ymm13
        vpunpckhdq ymm15, ymm12, ymm13
        vpermq  ymm14, ymm14, 50H
        vpermq  ymm15, ymm15, 50H
        vbroadcasti128 ymm12, xmmword ptr [BLAKE3_BLOCK_LEN]
        vpblendd ymm14, ymm14, ymm12, 44H
        vpblendd ymm15, ymm15, ymm12, 44H
        vmovdqa ymmword ptr [rsp], ymm14
        vmovdqa ymmword ptr [rsp+20H], ymm15
        mov     r8, qword ptr [rdi]
        mov     r9, qword ptr [rdi+8H]
        mov     r10, qword ptr [rdi+10H]
        mov     r11, qword ptr [rdi+18H]
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
        mov     dword ptr [rsp+200H], eax
        vmovups ymm2, ymmword ptr [r8+rdx-40H]
        vinsertf128 ymm2, ymm2, xmmword ptr [r9+rdx-40H], 01H
        vmovups ymm3, ymmword ptr [r8+rdx-30H]
        vinsertf128 ymm3, ymm3, xmmword ptr [r9+rdx-30H], 01H
        vshufps ymm4, ymm2, ymm3, 136
        vshufps ymm5, ymm2, ymm3, 221
        vmovups ymm2, ymmword ptr [r8+rdx-20H]
        vinsertf128 ymm2, ymm2, xmmword ptr [r9+rdx-20H], 01H
        vmovups ymm3, ymmword ptr [r8+rdx-10H]
        vinsertf128 ymm3, ymm3, xmmword ptr [r9+rdx-10H], 01H
        vshufps ymm6, ymm2, ymm3, 136
        vshufps ymm7, ymm2, ymm3, 221
        vpshufd ymm6, ymm6, 93H
        vpshufd ymm7, ymm7, 93H
        vmovups ymm10, ymmword ptr [r10+rdx-40H]
        vinsertf128 ymm10, ymm10, xmmword ptr [r11+rdx-40H], 01H
        vmovups ymm11, ymmword ptr [r10+rdx-30H]
        vinsertf128 ymm11, ymm11, xmmword ptr [r11+rdx-30H], 01H
        vshufps ymm12, ymm10, ymm11, 136
        vshufps ymm13, ymm10, ymm11, 221
        vmovups ymm10, ymmword ptr [r10+rdx-20H]
        vinsertf128 ymm10, ymm10, xmmword ptr [r11+rdx-20H], 01H
        vmovups ymm11, ymmword ptr [r10+rdx-10H]
        vinsertf128 ymm11, ymm11, xmmword ptr [r11+rdx-10H], 01H
        vshufps ymm14, ymm10, ymm11, 136
        vshufps ymm15, ymm10, ymm11, 221
        vpshufd ymm14, ymm14, 93H
        vpshufd ymm15, ymm15, 93H
        vpbroadcastd ymm2, dword ptr [rsp+200H]
        vmovdqa ymm3, ymmword ptr [rsp]
        vmovdqa ymm11, ymmword ptr [rsp+20H]
        vpblendd ymm3, ymm3, ymm2, 88H
        vpblendd ymm11, ymm11, ymm2, 88H
        vbroadcasti128 ymm2, xmmword ptr [BLAKE3_IV]
        vmovdqa ymm10, ymm2
        mov     al, 7
roundloop4:
        vpaddd  ymm0, ymm0, ymm4
        vpaddd  ymm8, ymm8, ymm12
        vmovdqa ymmword ptr [rsp+40H], ymm4
        nop
        vmovdqa ymmword ptr [rsp+60H], ymm12
        nop
        vpaddd  ymm0, ymm0, ymm1
        vpaddd  ymm8, ymm8, ymm9
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT16]
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
        vmovdqa ymmword ptr [rsp+80H], ymm5
        vmovdqa ymmword ptr [rsp+0A0H], ymm13
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT8]
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
        vpshufd ymm0, ymm0, 93H
        vpshufd ymm8, ymm8, 93H
        vpshufd ymm3, ymm3, 4EH
        vpshufd ymm11, ymm11, 4EH
        vpshufd ymm2, ymm2, 39H
        vpshufd ymm10, ymm10, 39H
        vpaddd  ymm0, ymm0, ymm6
        vpaddd  ymm8, ymm8, ymm14
        vpaddd  ymm0, ymm0, ymm1
        vpaddd  ymm8, ymm8, ymm9
        vpxor   ymm3, ymm3, ymm0
        vpxor   ymm11, ymm11, ymm8
        vbroadcasti128 ymm4, xmmword ptr [ROT16]
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
        vbroadcasti128 ymm4, xmmword ptr [ROT8]
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
        vpshufd ymm0, ymm0, 39H
        vpshufd ymm8, ymm8, 39H
        vpshufd ymm3, ymm3, 4EH
        vpshufd ymm11, ymm11, 4EH
        vpshufd ymm2, ymm2, 93H
        vpshufd ymm10, ymm10, 93H
        dec     al
        je      endroundloop4
        vmovdqa ymm4, ymmword ptr [rsp+40H]
        vmovdqa ymm5, ymmword ptr [rsp+80H]
        vshufps ymm12, ymm4, ymm5, 214
        vpshufd ymm13, ymm4, 0FH
        vpshufd ymm4, ymm12, 39H
        vshufps ymm12, ymm6, ymm7, 250
        vpblendd ymm13, ymm13, ymm12, 0AAH
        vpunpcklqdq ymm12, ymm7, ymm5
        vpblendd ymm12, ymm12, ymm6, 88H
        vpshufd ymm12, ymm12, 78H
        vpunpckhdq ymm5, ymm5, ymm7
        vpunpckldq ymm6, ymm6, ymm5
        vpshufd ymm7, ymm6, 1EH
        vmovdqa ymmword ptr [rsp+40H], ymm13
        vmovdqa ymmword ptr [rsp+80H], ymm12
        vmovdqa ymm12, ymmword ptr [rsp+60H]
        vmovdqa ymm13, ymmword ptr [rsp+0A0H]
        vshufps ymm5, ymm12, ymm13, 214
        vpshufd ymm6, ymm12, 0FH
        vpshufd ymm12, ymm5, 39H
        vshufps ymm5, ymm14, ymm15, 250
        vpblendd ymm6, ymm6, ymm5, 0AAH
        vpunpcklqdq ymm5, ymm15, ymm13
        vpblendd ymm5, ymm5, ymm14, 88H
        vpshufd ymm5, ymm5, 78H
        vpunpckhdq ymm13, ymm13, ymm15
        vpunpckldq ymm14, ymm14, ymm13
        vpshufd ymm15, ymm14, 1EH
        vmovdqa ymm13, ymm6
        vmovdqa ymm14, ymm5
        vmovdqa ymm5, ymmword ptr [rsp+40H]
        vmovdqa ymm6, ymmword ptr [rsp+80H]
        jmp     roundloop4
endroundloop4:
        vpxor   ymm0, ymm0, ymm2
        vpxor   ymm1, ymm1, ymm3
        vpxor   ymm8, ymm8, ymm10
        vpxor   ymm9, ymm9, ymm11
        mov     eax, r13d
        cmp     rdx, r15
        jne     innerloop4
        vmovdqu xmmword ptr [rbx], xmm0
        vmovdqu xmmword ptr [rbx+10H], xmm1
        vextracti128 xmmword ptr [rbx+20H], ymm0, 01H
        vextracti128 xmmword ptr [rbx+30H], ymm1, 01H
        vmovdqu xmmword ptr [rbx+40H], xmm8
        vmovdqu xmmword ptr [rbx+50H], xmm9
        vextracti128 xmmword ptr [rbx+60H], ymm8, 01H
        vextracti128 xmmword ptr [rbx+70H], ymm9, 01H
        vmovaps xmm8, xmmword ptr [rsp+260H]
        vmovaps xmm0, xmmword ptr [rsp+220H]
        vmovaps xmm1, xmmword ptr [rsp+230H]
        vmovaps xmm2, xmmword ptr [rsp+240H]
        vmovaps xmm3, xmmword ptr [rsp+250H]
        vblendvps xmm0, xmm0, xmm1, xmm8
        vblendvps xmm2, xmm2, xmm3, xmm8
        vmovaps xmmword ptr [rsp+220H], xmm0
        vmovaps xmmword ptr [rsp+240H], xmm2
        add     rbx, 128
        add     rdi, 32
        sub     rsi, 4
final3blocks:
        test    rsi, 2H
        je      final1blocks
        vbroadcasti128 ymm0, xmmword ptr [rcx]
        vbroadcasti128 ymm1, xmmword ptr [rcx+10H]
        vmovd   xmm13, dword ptr [rsp+220H]
        vpinsrd xmm13, xmm13, dword ptr [rsp+240H], 1
        vpinsrd xmm13, xmm13, dword ptr [BLAKE3_BLOCK_LEN], 2
        vmovd   xmm14, dword ptr [rsp+224H]
        vpinsrd xmm14, xmm14, dword ptr [rsp+244H], 1
        vpinsrd xmm14, xmm14, dword ptr [BLAKE3_BLOCK_LEN], 2
        vinserti128 ymm13, ymm13, xmm14, 01H
        vbroadcasti128 ymm14, xmmword ptr [ROT16]
        vbroadcasti128 ymm15, xmmword ptr [ROT8]
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
        mov     dword ptr [rsp+200H], eax
        vbroadcasti128 ymm2, xmmword ptr [BLAKE3_IV]
        vpbroadcastd ymm8, dword ptr [rsp+200H]
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
        vpshufd ymm0, ymm0, 93H
        vpshufd ymm3, ymm3, 4EH
        vpshufd ymm2, ymm2, 39H
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
        vmovaps ymm8, ymmword ptr [rsp+260H]
        vmovaps ymm0, ymmword ptr [rsp+220H]
        vmovups ymm1, ymmword ptr [rsp+228H]
        vmovaps ymm2, ymmword ptr [rsp+240H]
        vmovups ymm3, ymmword ptr [rsp+248H]
        vblendvps ymm0, ymm0, ymm1, ymm8
        vblendvps ymm2, ymm2, ymm3, ymm8
        vmovaps ymmword ptr [rsp+220H], ymm0
        vmovaps ymmword ptr [rsp+240H], ymm2
        add     rbx, 64
        add     rdi, 16
        sub     rsi, 2
final1blocks:
        test    rsi, 1H
        je      unwind
        vmovdqu xmm0, xmmword ptr [rcx]
        vmovdqu xmm1, xmmword ptr [rcx+10H]
        vmovd   xmm3, dword ptr [rsp+220H]
        vpinsrd xmm3, xmm3, dword ptr [rsp+240H], 1
        vpinsrd xmm13, xmm3, dword ptr [BLAKE3_BLOCK_LEN], 2
        vmovdqa xmm14, xmmword ptr [ROT16]
        vmovdqa xmm15, xmmword ptr [ROT8]
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
        vmovdqa xmm2, xmmword ptr [BLAKE3_IV]
        vmovdqa xmm3, xmm13
        vpinsrd xmm3, xmm3, eax, 3
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
        vpshufd xmm0, xmm0, 93H
        vpshufd xmm3, xmm3, 4EH
        vpshufd xmm2, xmm2, 39H
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

_llvm_blake3_hash_many_avx2 ENDP
llvm_blake3_hash_many_avx2 ENDP
_TEXT ENDS

_RDATA SEGMENT READONLY PAGE ALIAS(".rdata") 'CONST'
ALIGN   64
ADD0:
        dd 0, 1, 2, 3, 4, 5, 6, 7

ADD1:
        dd 8 dup (8)

BLAKE3_IV_0:
        dd 8 dup (6A09E667H)

BLAKE3_IV_1:
        dd 8 dup (0BB67AE85H)

BLAKE3_IV_2:
        dd 8 dup (3C6EF372H)

BLAKE3_IV_3:
        dd 8 dup (0A54FF53AH)

BLAKE3_BLOCK_LEN:
        dd 8 dup (64)

ROT16:
        db 2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13

ROT8:
        db 1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12

CMP_MSB_MASK:
        dd 8 dup(80000000H)

BLAKE3_IV:
        dd 6A09E667H, 0BB67AE85H, 3C6EF372H, 0A54FF53AH

_RDATA ENDS
END
