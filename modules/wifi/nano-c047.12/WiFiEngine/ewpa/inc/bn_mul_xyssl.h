/**
 * \file bn_mul.h
 */
/*
 *      Multiply source vector [s] with b, add result
 *       to destination vector [d] and set carry c.
 *
 *      Currently supports:
 *
 *         . IA-32 (386+)         . AMD64 / EM64T
 *         . IA-32 (SSE2)         . Motorola 68000
 *         . PowerPC, 32-bit      . MicroBlaze
 *         . PowerPC, 64-bit      . TriCore
 *         . SPARC v8             . ARM v3+
 *         . Alpha                . MIPS32
 *         . C, longlong          . C, generic
 */
#ifndef XYSSL_BN_MUL_H
#define XYSSL_BN_MUL_H

#if defined(__AGNUC__)
#if defined(__Ai386__)

#define MULADDC_INIT                            \
    asm( "movl   %%ebx, %0      " : "=m" (t));  \
    asm( "movl   %0, %%esi      " :: "m" (s));  \
    asm( "movl   %0, %%edi      " :: "m" (d));  \
    asm( "movl   %0, %%ecx      " :: "m" (c));  \
    asm( "movl   %0, %%ebx      " :: "m" (b));

#define MULADDC_CORE                            \
    asm( "lodsl                 " );            \
    asm( "mull   %ebx           " );            \
    asm( "addl   %ecx,   %eax   " );            \
    asm( "adcl   $0,     %edx   " );            \
    asm( "addl   (%edi), %eax   " );            \
    asm( "adcl   $0,     %edx   " );            \
    asm( "movl   %edx,   %ecx   " );            \
    asm( "stosl                 " );

#define MULADDC_STOP                            \
    asm( "movl   %0, %%ebx      " :: "m" (t));  \
    asm( "movl   %%ecx, %0      " : "=m" (c));  \
    asm( "movl   %%edi, %0      " : "=m" (d));  \
    asm( "movl   %%esi, %0      " : "=m" (s) :: \
    "eax", "ecx", "edx", "esi", "edi" );

#if defined(XYSSL_HAVE_SSE2)

#define MULADDC_HUIT                            \
    asm( "movd     %ecx,     %mm1     " );      \
    asm( "movd     %ebx,     %mm0     " );      \
    asm( "movd     (%edi),   %mm3     " );      \
    asm( "paddq    %mm3,     %mm1     " );      \
    asm( "movd     (%esi),   %mm2     " );      \
    asm( "pmuludq  %mm0,     %mm2     " );      \
    asm( "movd     4(%esi),  %mm4     " );      \
    asm( "pmuludq  %mm0,     %mm4     " );      \
    asm( "movd     8(%esi),  %mm6     " );      \
    asm( "pmuludq  %mm0,     %mm6     " );      \
    asm( "movd     12(%esi), %mm7     " );      \
    asm( "pmuludq  %mm0,     %mm7     " );      \
    asm( "paddq    %mm2,     %mm1     " );      \
    asm( "movd     4(%edi),  %mm3     " );      \
    asm( "paddq    %mm4,     %mm3     " );      \
    asm( "movd     8(%edi),  %mm5     " );      \
    asm( "paddq    %mm6,     %mm5     " );      \
    asm( "movd     12(%edi), %mm4     " );      \
    asm( "paddq    %mm4,     %mm7     " );      \
    asm( "movd     %mm1,     (%edi)   " );      \
    asm( "movd     16(%esi), %mm2     " );      \
    asm( "pmuludq  %mm0,     %mm2     " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "movd     20(%esi), %mm4     " );      \
    asm( "pmuludq  %mm0,     %mm4     " );      \
    asm( "paddq    %mm3,     %mm1     " );      \
    asm( "movd     24(%esi), %mm6     " );      \
    asm( "pmuludq  %mm0,     %mm6     " );      \
    asm( "movd     %mm1,     4(%edi)  " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "movd     28(%esi), %mm3     " );      \
    asm( "pmuludq  %mm0,     %mm3     " );      \
    asm( "paddq    %mm5,     %mm1     " );      \
    asm( "movd     16(%edi), %mm5     " );      \
    asm( "paddq    %mm5,     %mm2     " );      \
    asm( "movd     %mm1,     8(%edi)  " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "paddq    %mm7,     %mm1     " );      \
    asm( "movd     20(%edi), %mm5     " );      \
    asm( "paddq    %mm5,     %mm4     " );      \
    asm( "movd     %mm1,     12(%edi) " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "paddq    %mm2,     %mm1     " );      \
    asm( "movd     24(%edi), %mm5     " );      \
    asm( "paddq    %mm5,     %mm6     " );      \
    asm( "movd     %mm1,     16(%edi) " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "paddq    %mm4,     %mm1     " );      \
    asm( "movd     28(%edi), %mm5     " );      \
    asm( "paddq    %mm5,     %mm3     " );      \
    asm( "movd     %mm1,     20(%edi) " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "paddq    %mm6,     %mm1     " );      \
    asm( "movd     %mm1,     24(%edi) " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "paddq    %mm3,     %mm1     " );      \
    asm( "movd     %mm1,     28(%edi) " );      \
    asm( "addl     $32,      %edi     " );      \
    asm( "addl     $32,      %esi     " );      \
    asm( "psrlq    $32,      %mm1     " );      \
    asm( "movd     %mm1,     %ecx     " );

#endif /* SSE2 */
#endif /* i386 */

#if defined(__amd64__) || defined (__x86_64__)

#define MULADDC_INIT                            \
    asm( "movq   %0, %%rsi      " :: "m" (s));  \
    asm( "movq   %0, %%rdi      " :: "m" (d));  \
    asm( "movq   %0, %%rcx      " :: "m" (c));  \
    asm( "movq   %0, %%rbx      " :: "m" (b));  \
    asm( "xorq   %r8, %r8       " );

#define MULADDC_CORE                            \
    asm( "movq  (%rsi),%rax     " );            \
    asm( "mulq   %rbx           " );            \
    asm( "addq   $8,   %rsi     " );            \
    asm( "addq   %rcx, %rax     " );            \
    asm( "movq   %r8,  %rcx     " );            \
    asm( "adcq   $0,   %rdx     " );            \
    asm( "nop                   " );            \
    asm( "addq   %rax, (%rdi)   " );            \
    asm( "adcq   %rdx, %rcx     " );            \
    asm( "addq   $8,   %rdi     " );

#define MULADDC_STOP                            \
    asm( "movq   %%rcx, %0      " : "=m" (c));  \
    asm( "movq   %%rdi, %0      " : "=m" (d));  \
    asm( "movq   %%rsi, %0      " : "=m" (s) :: \
    "rax", "rcx", "rdx", "rbx", "rsi", "rdi", "r8" );

#endif /* AMD64 */

#if defined(__mc68020__) || defined(__mcpu32__)

#define MULADDC_INIT                            \
    asm( "movl   %0, %%a2       " :: "m" (s));  \
    asm( "movl   %0, %%a3       " :: "m" (d));  \
    asm( "movl   %0, %%d3       " :: "m" (c));  \
    asm( "movl   %0, %%d2       " :: "m" (b));  \
    asm( "moveq  #0, %d0        " );

#define MULADDC_CORE                            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d4:%d1   " );            \
    asm( "addl   %d3, %d1       " );            \
    asm( "addxl  %d0, %d4       " );            \
    asm( "moveq  #0,  %d3       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "addxl  %d4, %d3       " );

#define MULADDC_STOP                            \
    asm( "movl   %%d3, %0       " : "=m" (c));  \
    asm( "movl   %%a3, %0       " : "=m" (d));  \
    asm( "movl   %%a2, %0       " : "=m" (s) :: \
    "d0", "d1", "d2", "d3", "d4", "a2", "a3" );

#define MULADDC_HUIT                            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d4:%d1   " );            \
    asm( "addxl  %d3, %d1       " );            \
    asm( "addxl  %d0, %d4       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d3:%d1   " );            \
    asm( "addxl  %d4, %d1       " );            \
    asm( "addxl  %d0, %d3       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d4:%d1   " );            \
    asm( "addxl  %d3, %d1       " );            \
    asm( "addxl  %d0, %d4       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d3:%d1   " );            \
    asm( "addxl  %d4, %d1       " );            \
    asm( "addxl  %d0, %d3       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d4:%d1   " );            \
    asm( "addxl  %d3, %d1       " );            \
    asm( "addxl  %d0, %d4       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d3:%d1   " );            \
    asm( "addxl  %d4, %d1       " );            \
    asm( "addxl  %d0, %d3       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d4:%d1   " );            \
    asm( "addxl  %d3, %d1       " );            \
    asm( "addxl  %d0, %d4       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "movel  %a2@+, %d1     " );            \
    asm( "mulul  %d2, %d3:%d1   " );            \
    asm( "addxl  %d4, %d1       " );            \
    asm( "addxl  %d0, %d3       " );            \
    asm( "addl   %d1, %a3@+     " );            \
    asm( "addxl  %d0, %d3       " );

#endif /* MC68000 */

#if defined(__powerpc__)   || defined(__ppc__)
#if defined(__powerpc64__) || defined(__ppc64__)

#define MULADDC_INIT                            \
    asm( "ld     %%r3, %0       " :: "m" (s));  \
    asm( "ld     %%r4, %0       " :: "m" (d));  \
    asm( "ld     %%r5, %0       " :: "m" (c));  \
    asm( "ld     %%r6, %0       " :: "m" (b));  \
    asm( "addi   %r3, %r3, -8   " );            \
    asm( "addi   %r4, %r4, -8   " );            \
    asm( "addic  %r5, %r5,  0   " );

#define MULADDC_CORE                            \
    asm( "ldu    %r7, 8(%r3)    " );            \
    asm( "mulld  %r8, %r7, %r6  " );            \
    asm( "mulhdu %r9, %r7, %r6  " );            \
    asm( "adde   %r8, %r8, %r5  " );            \
    asm( "ld     %r7, 8(%r4)    " );            \
    asm( "addze  %r5, %r9       " );            \
    asm( "addc   %r8, %r8, %r7  " );            \
    asm( "stdu   %r8, 8(%r4)    " );

#define MULADDC_STOP                            \
    asm( "addze  %r5, %r5       " );            \
    asm( "addi   %r4, %r4, 8    " );            \
    asm( "addi   %r3, %r3, 8    " );            \
    asm( "std    %%r5, %0       " : "=m" (c));  \
    asm( "std    %%r4, %0       " : "=m" (d));  \
    asm( "std    %%r3, %0       " : "=m" (s) :: \
    "r3", "r4", "r5", "r6", "r7", "r8", "r9" );

#else

#define MULADDC_INIT                            \
    asm( "lwz    %%r3, %0       " :: "m" (s));  \
    asm( "lwz    %%r4, %0       " :: "m" (d));  \
    asm( "lwz    %%r5, %0       " :: "m" (c));  \
    asm( "lwz    %%r6, %0       " :: "m" (b));  \
    asm( "addi   %r3, %r3, -4   " );            \
    asm( "addi   %r4, %r4, -4   " );            \
    asm( "addic  %r5, %r5,  0   " );

#define MULADDC_CORE                            \
    asm( "lwzu   %r7, 4(%r3)    " );            \
    asm( "mullw  %r8, %r7, %r6  " );            \
    asm( "mulhwu %r9, %r7, %r6  " );            \
    asm( "adde   %r8, %r8, %r5  " );            \
    asm( "lwz    %r7, 4(%r4)    " );            \
    asm( "addze  %r5, %r9       " );            \
    asm( "addc   %r8, %r8, %r7  " );            \
    asm( "stwu   %r8, 4(%r4)    " );

#define MULADDC_STOP                            \
    asm( "addze  %r5, %r5       " );            \
    asm( "addi   %r4, %r4, 4    " );            \
    asm( "addi   %r3, %r3, 4    " );            \
    asm( "stw    %%r5, %0       " : "=m" (c));  \
    asm( "stw    %%r4, %0       " : "=m" (d));  \
    asm( "stw    %%r3, %0       " : "=m" (s) :: \
    "r3", "r4", "r5", "r6", "r7", "r8", "r9" );

#endif /* PPC32 */
#endif /* PPC64 */

#if defined(__sparc__)

#define MULADDC_INIT                            \
    asm( "ld     %0, %%o0       " :: "m" (s));  \
    asm( "ld     %0, %%o1       " :: "m" (d));  \
    asm( "ld     %0, %%o2       " :: "m" (c));  \
    asm( "ld     %0, %%o3       " :: "m" (b));

#define MULADDC_CORE                            \
    asm( "ld    [%o0], %o4      " );            \
    asm( "inc      4,  %o0      " );            \
    asm( "ld    [%o1], %o5      " );            \
    asm( "umul   %o3,  %o4, %o4 " );            \
    asm( "addcc  %o4,  %o2, %o4 " );            \
    asm( "rd      %y,  %g1      " );            \
    asm( "addx   %g1,    0, %g1 " );            \
    asm( "addcc  %o4,  %o5, %o4 " );            \
    asm( "st     %o4, [%o1]     " );            \
    asm( "addx   %g1,    0, %o2 " );            \
    asm( "inc      4,  %o1      " );

#define MULADDC_STOP                            \
    asm( "st     %%o2, %0       " : "=m" (c));  \
    asm( "st     %%o1, %0       " : "=m" (d));  \
    asm( "st     %%o0, %0       " : "=m" (s) :: \
    "g1", "o0", "o1", "o2", "o3", "o4", "o5" );

#endif /* SPARCv8 */

#if defined(__microblaze__) || defined(microblaze)

#define MULADDC_INIT                            \
    asm( "lwi   r3,   %0        " :: "m" (s));  \
    asm( "lwi   r4,   %0        " :: "m" (d));  \
    asm( "lwi   r5,   %0        " :: "m" (c));  \
    asm( "lwi   r6,   %0        " :: "m" (b));  \
    asm( "andi  r7,   r6, 0xffff" );            \
    asm( "bsrli r6,   r6, 16    " );

#define MULADDC_CORE                            \
    asm( "lhui  r8,   r3,   0   " );            \
    asm( "addi  r3,   r3,   2   " );            \
    asm( "lhui  r9,   r3,   0   " );            \
    asm( "addi  r3,   r3,   2   " );            \
    asm( "mul   r10,  r9,  r6   " );            \
    asm( "mul   r11,  r8,  r7   " );            \
    asm( "mul   r12,  r9,  r7   " );            \
    asm( "mul   r13,  r8,  r6   " );            \
    asm( "bsrli  r8, r10,  16   " );            \
    asm( "bsrli  r9, r11,  16   " );            \
    asm( "add   r13, r13,  r8   " );            \
    asm( "add   r13, r13,  r9   " );            \
    asm( "bslli r10, r10,  16   " );            \
    asm( "bslli r11, r11,  16   " );            \
    asm( "add   r12, r12, r10   " );            \
    asm( "addc  r13, r13,  r0   " );            \
    asm( "add   r12, r12, r11   " );            \
    asm( "addc  r13, r13,  r0   " );            \
    asm( "lwi   r10,  r4,   0   " );            \
    asm( "add   r12, r12, r10   " );            \
    asm( "addc  r13, r13,  r0   " );            \
    asm( "add   r12, r12,  r5   " );            \
    asm( "addc   r5, r13,  r0   " );            \
    asm( "swi   r12,  r4,   0   " );            \
    asm( "addi   r4,  r4,   4   " );

#define MULADDC_STOP                            \
    asm( "swi   r5,   %0        " : "=m" (c));  \
    asm( "swi   r4,   %0        " : "=m" (d));  \
    asm( "swi   r3,   %0        " : "=m" (s) :: \
     "r3", "r4" , "r5" , "r6" , "r7" , "r8" ,   \
     "r9", "r10", "r11", "r12", "r13" );

#endif /* MicroBlaze */

#if defined(__tricore__)

#define MULADDC_INIT                            \
    asm( "ld.a   %%a2, %0       " :: "m" (s));  \
    asm( "ld.a   %%a3, %0       " :: "m" (d));  \
    asm( "ld.w   %%d4, %0       " :: "m" (c));  \
    asm( "ld.w   %%d1, %0       " :: "m" (b));  \
    asm( "xor    %d5, %d5       " );

#define MULADDC_CORE                            \
    asm( "ld.w   %d0,   [%a2+]      " );        \
    asm( "madd.u %e2, %e4, %d0, %d1 " );        \
    asm( "ld.w   %d0,   [%a3]       " );        \
    asm( "addx   %d2,    %d2,  %d0  " );        \
    asm( "addc   %d3,    %d3,    0  " );        \
    asm( "mov    %d4,    %d3        " );        \
    asm( "st.w  [%a3+],  %d2        " );

#define MULADDC_STOP                            \
    asm( "st.w   %0, %%d4       " : "=m" (c));  \
    asm( "st.a   %0, %%a3       " : "=m" (d));  \
    asm( "st.a   %0, %%a2       " : "=m" (s) :: \
    "d0", "d1", "e2", "d4", "a2", "a3" );

#endif /* TriCore */

#if defined(__arm__)

#define MULADDC_INIT                            \
    asm( "ldr    r0, %0         " :: "m" (s));  \
    asm( "ldr    r1, %0         " :: "m" (d));  \
    asm( "ldr    r2, %0         " :: "m" (c));  \
    asm( "ldr    r3, %0         " :: "m" (b));

#define MULADDC_CORE                            \
    asm( "ldr    r4, [r0], #4   " );            \
    asm( "mov    r5, #0         " );            \
    asm( "ldr    r6, [r1]       " );            \
    asm( "umlal  r2, r5, r3, r4 " );            \
    asm( "adds   r7, r6, r2     " );            \
    asm( "adc    r2, r5, #0     " );            \
    asm( "str    r7, [r1], #4   " );

#define MULADDC_STOP                            \
    asm( "str    r2, %0         " : "=m" (c));  \
    asm( "str    r1, %0         " : "=m" (d));  \
    asm( "str    r0, %0         " : "=m" (s) :: \
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7" );

#endif /* ARMv3 */

#if defined(__alpha__)

#define MULADDC_INIT                            \
    asm( "ldq    $1, %0         " :: "m" (s));  \
    asm( "ldq    $2, %0         " :: "m" (d));  \
    asm( "ldq    $3, %0         " :: "m" (c));  \
    asm( "ldq    $4, %0         " :: "m" (b));

#define MULADDC_CORE                            \
    asm( "ldq    $6,  0($1)     " );            \
    asm( "addq   $1,  8, $1     " );            \
    asm( "mulq   $6, $4, $7     " );            \
    asm( "umulh  $6, $4, $6     " );            \
    asm( "addq   $7, $3, $7     " );            \
    asm( "cmpult $7, $3, $3     " );            \
    asm( "ldq    $5,  0($2)     " );            \
    asm( "addq   $7, $5, $7     " );            \
    asm( "cmpult $7, $5, $5     " );            \
    asm( "stq    $7,  0($2)     " );            \
    asm( "addq   $2,  8, $2     " );            \
    asm( "addq   $6, $3, $3     " );            \
    asm( "addq   $5, $3, $3     " );

#define MULADDC_STOP                            \
    asm( "stq    $3, %0         " : "=m" (c));  \
    asm( "stq    $2, %0         " : "=m" (d));  \
    asm( "stq    $1, %0         " : "=m" (s) :: \
    "$1", "$2", "$3", "$4", "$5", "$6", "$7" );

#endif /* Alpha */

#if defined(__mips__)

#define MULADDC_INIT                            \
    asm( "lw     $10, %0        " :: "m" (s));  \
    asm( "lw     $11, %0        " :: "m" (d));  \
    asm( "lw     $12, %0        " :: "m" (c));  \
    asm( "lw     $13, %0        " :: "m" (b));

#define MULADDC_CORE                            \
    asm( "lw     $14, 0($10)    " );            \
    asm( "multu  $13, $14       " );            \
    asm( "addi   $10, $10, 4    " );            \
    asm( "mflo   $14            " );            \
    asm( "mfhi   $9             " );            \
    asm( "addu   $14, $12, $14  " );            \
    asm( "lw     $15, 0($11)    " );            \
    asm( "sltu   $12, $14, $12  " );            \
    asm( "addu   $15, $14, $15  " );            \
    asm( "sltu   $14, $15, $14  " );            \
    asm( "addu   $12, $12, $9   " );            \
    asm( "sw     $15, 0($11)    " );            \
    asm( "addu   $12, $12, $14  " );            \
    asm( "addi   $11, $11, 4    " );

#define MULADDC_STOP                            \
    asm( "sw     $12, %0        " : "=m" (c));  \
    asm( "sw     $11, %0        " : "=m" (d));  \
    asm( "sw     $10, %0        " : "=m" (s) :: \
    "$9", "$10", "$11", "$12", "$13", "$14", "$15" );

#endif /* MIPS */
#endif /* GNUC */

#if (defined(_MSC_VER) && defined(_M_IX86)) || defined(__WATCOMC__)

#define MULADDC_INIT                            \
    __asm   mov     esi, s                      \
    __asm   mov     edi, d                      \
    __asm   mov     ecx, c                      \
    __asm   mov     ebx, b

#define MULADDC_CORE                            \
    __asm   lodsd                               \
    __asm   mul     ebx                         \
    __asm   add     eax, ecx                    \
    __asm   adc     edx, 0                      \
    __asm   add     eax, [edi]                  \
    __asm   adc     edx, 0                      \
    __asm   mov     ecx, edx                    \
    __asm   stosd

#define MULADDC_STOP                            \
    __asm   mov     c, ecx                      \
    __asm   mov     d, edi                      \
    __asm   mov     s, esi                      \

#if defined(XYSSL_HAVE_SSE2)

#define EMIT __asm _emit

#define MULADDC_HUIT                            \
    EMIT 0x0F  EMIT 0x6E  EMIT 0xC9             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0xC3             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x1F             \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCB             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x16             \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xD0             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x66  EMIT 0x04  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xE0             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x76  EMIT 0x08  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xF0             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x7E  EMIT 0x0C  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xF8             \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCA             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x5F  EMIT 0x04  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xDC             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x6F  EMIT 0x08  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xEE             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x67  EMIT 0x0C  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xFC             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x0F             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x56  EMIT 0x10  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xD0             \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x66  EMIT 0x14  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xE0             \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCB             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x76  EMIT 0x18  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xF0             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x04  \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x5E  EMIT 0x1C  \
    EMIT 0x0F  EMIT 0xF4  EMIT 0xD8             \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCD             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x6F  EMIT 0x10  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xD5             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x08  \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCF             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x6F  EMIT 0x14  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xE5             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x0C  \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCA             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x6F  EMIT 0x18  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xF5             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x10  \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCC             \
    EMIT 0x0F  EMIT 0x6E  EMIT 0x6F  EMIT 0x1C  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xDD             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x14  \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCE             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x18  \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0xD4  EMIT 0xCB             \
    EMIT 0x0F  EMIT 0x7E  EMIT 0x4F  EMIT 0x1C  \
    EMIT 0x83  EMIT 0xC7  EMIT 0x20             \
    EMIT 0x83  EMIT 0xC6  EMIT 0x20             \
    EMIT 0x0F  EMIT 0x73  EMIT 0xD1  EMIT 0x20  \
    EMIT 0x0F  EMIT 0x7E  EMIT 0xC9

#endif /* SSE2 */
#endif /* MSVC */

#if !defined(MULADDC_CORE)
#if defined(XYSSL_HAVE_LONGLONG)

#define MULADDC_INIT                    \
{                                       \
    t_dbl r;                            \
    t_int_xyssl r0, r1;

#define MULADDC_CORE                    \
    r   = *(s++) * (t_dbl) b;           \
    r0  = r;                            \
    r1  = r >> biL;                     \
    r0 += c;  r1 += (r0 <  c);          \
    r0 += *d; r1 += (r0 < *d);          \
    c = r1; *(d++) = r0;

#define MULADDC_STOP                    \
}

#else
#define MULADDC_INIT                    \
{                                       \
    t_int_xyssl s0, s1, b0, b1;               \
    t_int_xyssl r0, r1, rx, ry;               \
    b0 = ( b << biH ) >> biH;           \
    b1 = ( b >> biH );

#define MULADDC_CORE                    \
    s0 = ( *s << biH ) >> biH;          \
    s1 = ( *s >> biH ); s++;            \
    rx = s0 * b1; r0 = s0 * b0;         \
    ry = s1 * b0; r1 = s1 * b1;         \
    r1 += ( rx >> biH );                \
    r1 += ( ry >> biH );                \
    rx <<= biH; ry <<= biH;             \
    r0 += rx; r1 += (r0 < rx);          \
    r0 += ry; r1 += (r0 < ry);          \
    r0 +=  c; r1 += (r0 <  c);          \
    r0 += *d; r1 += (r0 < *d);          \
    c = r1; *(d++) = r0;

#define MULADDC_STOP                    \
}

#endif /* C (generic)  */
#endif /* C (longlong) */

#endif /* bn_mul.h */
