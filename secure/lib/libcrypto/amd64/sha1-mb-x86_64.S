/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from sha1-mb-x86_64.pl. */
.text	



.globl	sha1_multi_block
.type	sha1_multi_block,@function
.align	32
sha1_multi_block:
.cfi_startproc	
	movq	OPENSSL_ia32cap_P+4(%rip),%rcx
	btq	$61,%rcx
	jc	_shaext_shortcut
	testl	$268435456,%ecx
	jnz	_avx_shortcut
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	pushq	%rbx
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_offset	%rbx,-24
	subq	$288,%rsp
	andq	$-256,%rsp
	movq	%rax,272(%rsp)
.cfi_escape	0x0f,0x06,0x77,0x90,0x02,0x06,0x23,0x08
.Lbody:
	leaq	K_XX_XX(%rip),%rbp
	leaq	256(%rsp),%rbx

.Loop_grande:
	movl	%edx,280(%rsp)
	xorl	%edx,%edx
	movq	0(%rsi),%r8
	movl	8(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,0(%rbx)
	cmovleq	%rbp,%r8
	movq	16(%rsi),%r9
	movl	24(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,4(%rbx)
	cmovleq	%rbp,%r9
	movq	32(%rsi),%r10
	movl	40(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,8(%rbx)
	cmovleq	%rbp,%r10
	movq	48(%rsi),%r11
	movl	56(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,12(%rbx)
	cmovleq	%rbp,%r11
	testl	%edx,%edx
	jz	.Ldone

	movdqu	0(%rdi),%xmm10
	leaq	128(%rsp),%rax
	movdqu	32(%rdi),%xmm11
	movdqu	64(%rdi),%xmm12
	movdqu	96(%rdi),%xmm13
	movdqu	128(%rdi),%xmm14
	movdqa	96(%rbp),%xmm5
	movdqa	-32(%rbp),%xmm15
	jmp	.Loop

.align	32
.Loop:
	movd	(%r8),%xmm0
	leaq	64(%r8),%r8
	movd	(%r9),%xmm2
	leaq	64(%r9),%r9
	movd	(%r10),%xmm3
	leaq	64(%r10),%r10
	movd	(%r11),%xmm4
	leaq	64(%r11),%r11
	punpckldq	%xmm3,%xmm0
	movd	-60(%r8),%xmm1
	punpckldq	%xmm4,%xmm2
	movd	-60(%r9),%xmm9
	punpckldq	%xmm2,%xmm0
	movd	-60(%r10),%xmm8
.byte	102,15,56,0,197
	movd	-60(%r11),%xmm7
	punpckldq	%xmm8,%xmm1
	movdqa	%xmm10,%xmm8
	paddd	%xmm15,%xmm14
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm11,%xmm7
	movdqa	%xmm11,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm13,%xmm7
	pand	%xmm12,%xmm6
	punpckldq	%xmm9,%xmm1
	movdqa	%xmm10,%xmm9

	movdqa	%xmm0,0-128(%rax)
	paddd	%xmm0,%xmm14
	movd	-56(%r8),%xmm2
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm11,%xmm7

	por	%xmm9,%xmm8
	movd	-56(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
.byte	102,15,56,0,205
	movd	-56(%r10),%xmm8
	por	%xmm7,%xmm11
	movd	-56(%r11),%xmm7
	punpckldq	%xmm8,%xmm2
	movdqa	%xmm14,%xmm8
	paddd	%xmm15,%xmm13
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm10,%xmm7
	movdqa	%xmm10,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm12,%xmm7
	pand	%xmm11,%xmm6
	punpckldq	%xmm9,%xmm2
	movdqa	%xmm14,%xmm9

	movdqa	%xmm1,16-128(%rax)
	paddd	%xmm1,%xmm13
	movd	-52(%r8),%xmm3
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm10,%xmm7

	por	%xmm9,%xmm8
	movd	-52(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
.byte	102,15,56,0,213
	movd	-52(%r10),%xmm8
	por	%xmm7,%xmm10
	movd	-52(%r11),%xmm7
	punpckldq	%xmm8,%xmm3
	movdqa	%xmm13,%xmm8
	paddd	%xmm15,%xmm12
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm14,%xmm7
	movdqa	%xmm14,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm11,%xmm7
	pand	%xmm10,%xmm6
	punpckldq	%xmm9,%xmm3
	movdqa	%xmm13,%xmm9

	movdqa	%xmm2,32-128(%rax)
	paddd	%xmm2,%xmm12
	movd	-48(%r8),%xmm4
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm14,%xmm7

	por	%xmm9,%xmm8
	movd	-48(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
.byte	102,15,56,0,221
	movd	-48(%r10),%xmm8
	por	%xmm7,%xmm14
	movd	-48(%r11),%xmm7
	punpckldq	%xmm8,%xmm4
	movdqa	%xmm12,%xmm8
	paddd	%xmm15,%xmm11
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm13,%xmm7
	movdqa	%xmm13,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm10,%xmm7
	pand	%xmm14,%xmm6
	punpckldq	%xmm9,%xmm4
	movdqa	%xmm12,%xmm9

	movdqa	%xmm3,48-128(%rax)
	paddd	%xmm3,%xmm11
	movd	-44(%r8),%xmm0
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm13,%xmm7

	por	%xmm9,%xmm8
	movd	-44(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
.byte	102,15,56,0,229
	movd	-44(%r10),%xmm8
	por	%xmm7,%xmm13
	movd	-44(%r11),%xmm7
	punpckldq	%xmm8,%xmm0
	movdqa	%xmm11,%xmm8
	paddd	%xmm15,%xmm10
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm12,%xmm7
	movdqa	%xmm12,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm14,%xmm7
	pand	%xmm13,%xmm6
	punpckldq	%xmm9,%xmm0
	movdqa	%xmm11,%xmm9

	movdqa	%xmm4,64-128(%rax)
	paddd	%xmm4,%xmm10
	movd	-40(%r8),%xmm1
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm12,%xmm7

	por	%xmm9,%xmm8
	movd	-40(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
.byte	102,15,56,0,197
	movd	-40(%r10),%xmm8
	por	%xmm7,%xmm12
	movd	-40(%r11),%xmm7
	punpckldq	%xmm8,%xmm1
	movdqa	%xmm10,%xmm8
	paddd	%xmm15,%xmm14
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm11,%xmm7
	movdqa	%xmm11,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm13,%xmm7
	pand	%xmm12,%xmm6
	punpckldq	%xmm9,%xmm1
	movdqa	%xmm10,%xmm9

	movdqa	%xmm0,80-128(%rax)
	paddd	%xmm0,%xmm14
	movd	-36(%r8),%xmm2
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm11,%xmm7

	por	%xmm9,%xmm8
	movd	-36(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
.byte	102,15,56,0,205
	movd	-36(%r10),%xmm8
	por	%xmm7,%xmm11
	movd	-36(%r11),%xmm7
	punpckldq	%xmm8,%xmm2
	movdqa	%xmm14,%xmm8
	paddd	%xmm15,%xmm13
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm10,%xmm7
	movdqa	%xmm10,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm12,%xmm7
	pand	%xmm11,%xmm6
	punpckldq	%xmm9,%xmm2
	movdqa	%xmm14,%xmm9

	movdqa	%xmm1,96-128(%rax)
	paddd	%xmm1,%xmm13
	movd	-32(%r8),%xmm3
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm10,%xmm7

	por	%xmm9,%xmm8
	movd	-32(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
.byte	102,15,56,0,213
	movd	-32(%r10),%xmm8
	por	%xmm7,%xmm10
	movd	-32(%r11),%xmm7
	punpckldq	%xmm8,%xmm3
	movdqa	%xmm13,%xmm8
	paddd	%xmm15,%xmm12
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm14,%xmm7
	movdqa	%xmm14,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm11,%xmm7
	pand	%xmm10,%xmm6
	punpckldq	%xmm9,%xmm3
	movdqa	%xmm13,%xmm9

	movdqa	%xmm2,112-128(%rax)
	paddd	%xmm2,%xmm12
	movd	-28(%r8),%xmm4
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm14,%xmm7

	por	%xmm9,%xmm8
	movd	-28(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
.byte	102,15,56,0,221
	movd	-28(%r10),%xmm8
	por	%xmm7,%xmm14
	movd	-28(%r11),%xmm7
	punpckldq	%xmm8,%xmm4
	movdqa	%xmm12,%xmm8
	paddd	%xmm15,%xmm11
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm13,%xmm7
	movdqa	%xmm13,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm10,%xmm7
	pand	%xmm14,%xmm6
	punpckldq	%xmm9,%xmm4
	movdqa	%xmm12,%xmm9

	movdqa	%xmm3,128-128(%rax)
	paddd	%xmm3,%xmm11
	movd	-24(%r8),%xmm0
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm13,%xmm7

	por	%xmm9,%xmm8
	movd	-24(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
.byte	102,15,56,0,229
	movd	-24(%r10),%xmm8
	por	%xmm7,%xmm13
	movd	-24(%r11),%xmm7
	punpckldq	%xmm8,%xmm0
	movdqa	%xmm11,%xmm8
	paddd	%xmm15,%xmm10
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm12,%xmm7
	movdqa	%xmm12,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm14,%xmm7
	pand	%xmm13,%xmm6
	punpckldq	%xmm9,%xmm0
	movdqa	%xmm11,%xmm9

	movdqa	%xmm4,144-128(%rax)
	paddd	%xmm4,%xmm10
	movd	-20(%r8),%xmm1
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm12,%xmm7

	por	%xmm9,%xmm8
	movd	-20(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
.byte	102,15,56,0,197
	movd	-20(%r10),%xmm8
	por	%xmm7,%xmm12
	movd	-20(%r11),%xmm7
	punpckldq	%xmm8,%xmm1
	movdqa	%xmm10,%xmm8
	paddd	%xmm15,%xmm14
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm11,%xmm7
	movdqa	%xmm11,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm13,%xmm7
	pand	%xmm12,%xmm6
	punpckldq	%xmm9,%xmm1
	movdqa	%xmm10,%xmm9

	movdqa	%xmm0,160-128(%rax)
	paddd	%xmm0,%xmm14
	movd	-16(%r8),%xmm2
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm11,%xmm7

	por	%xmm9,%xmm8
	movd	-16(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
.byte	102,15,56,0,205
	movd	-16(%r10),%xmm8
	por	%xmm7,%xmm11
	movd	-16(%r11),%xmm7
	punpckldq	%xmm8,%xmm2
	movdqa	%xmm14,%xmm8
	paddd	%xmm15,%xmm13
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm10,%xmm7
	movdqa	%xmm10,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm12,%xmm7
	pand	%xmm11,%xmm6
	punpckldq	%xmm9,%xmm2
	movdqa	%xmm14,%xmm9

	movdqa	%xmm1,176-128(%rax)
	paddd	%xmm1,%xmm13
	movd	-12(%r8),%xmm3
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm10,%xmm7

	por	%xmm9,%xmm8
	movd	-12(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
.byte	102,15,56,0,213
	movd	-12(%r10),%xmm8
	por	%xmm7,%xmm10
	movd	-12(%r11),%xmm7
	punpckldq	%xmm8,%xmm3
	movdqa	%xmm13,%xmm8
	paddd	%xmm15,%xmm12
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm14,%xmm7
	movdqa	%xmm14,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm11,%xmm7
	pand	%xmm10,%xmm6
	punpckldq	%xmm9,%xmm3
	movdqa	%xmm13,%xmm9

	movdqa	%xmm2,192-128(%rax)
	paddd	%xmm2,%xmm12
	movd	-8(%r8),%xmm4
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm14,%xmm7

	por	%xmm9,%xmm8
	movd	-8(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
.byte	102,15,56,0,221
	movd	-8(%r10),%xmm8
	por	%xmm7,%xmm14
	movd	-8(%r11),%xmm7
	punpckldq	%xmm8,%xmm4
	movdqa	%xmm12,%xmm8
	paddd	%xmm15,%xmm11
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm13,%xmm7
	movdqa	%xmm13,%xmm6
	pslld	$5,%xmm8
	pandn	%xmm10,%xmm7
	pand	%xmm14,%xmm6
	punpckldq	%xmm9,%xmm4
	movdqa	%xmm12,%xmm9

	movdqa	%xmm3,208-128(%rax)
	paddd	%xmm3,%xmm11
	movd	-4(%r8),%xmm0
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm13,%xmm7

	por	%xmm9,%xmm8
	movd	-4(%r9),%xmm9
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
.byte	102,15,56,0,229
	movd	-4(%r10),%xmm8
	por	%xmm7,%xmm13
	movdqa	0-128(%rax),%xmm1
	movd	-4(%r11),%xmm7
	punpckldq	%xmm8,%xmm0
	movdqa	%xmm11,%xmm8
	paddd	%xmm15,%xmm10
	punpckldq	%xmm7,%xmm9
	movdqa	%xmm12,%xmm7
	movdqa	%xmm12,%xmm6
	pslld	$5,%xmm8
	prefetcht0	63(%r8)
	pandn	%xmm14,%xmm7
	pand	%xmm13,%xmm6
	punpckldq	%xmm9,%xmm0
	movdqa	%xmm11,%xmm9

	movdqa	%xmm4,224-128(%rax)
	paddd	%xmm4,%xmm10
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6
	movdqa	%xmm12,%xmm7
	prefetcht0	63(%r9)

	por	%xmm9,%xmm8
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm10
	prefetcht0	63(%r10)

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
.byte	102,15,56,0,197
	prefetcht0	63(%r11)
	por	%xmm7,%xmm12
	movdqa	16-128(%rax),%xmm2
	pxor	%xmm3,%xmm1
	movdqa	32-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	pxor	128-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	movdqa	%xmm11,%xmm7
	pslld	$5,%xmm8
	pxor	%xmm3,%xmm1
	movdqa	%xmm11,%xmm6
	pandn	%xmm13,%xmm7
	movdqa	%xmm1,%xmm5
	pand	%xmm12,%xmm6
	movdqa	%xmm10,%xmm9
	psrld	$31,%xmm5
	paddd	%xmm1,%xmm1

	movdqa	%xmm0,240-128(%rax)
	paddd	%xmm0,%xmm14
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6

	movdqa	%xmm11,%xmm7
	por	%xmm9,%xmm8
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	48-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	pxor	144-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	movdqa	%xmm10,%xmm7
	pslld	$5,%xmm8
	pxor	%xmm4,%xmm2
	movdqa	%xmm10,%xmm6
	pandn	%xmm12,%xmm7
	movdqa	%xmm2,%xmm5
	pand	%xmm11,%xmm6
	movdqa	%xmm14,%xmm9
	psrld	$31,%xmm5
	paddd	%xmm2,%xmm2

	movdqa	%xmm1,0-128(%rax)
	paddd	%xmm1,%xmm13
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6

	movdqa	%xmm10,%xmm7
	por	%xmm9,%xmm8
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	64-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	pxor	160-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	movdqa	%xmm14,%xmm7
	pslld	$5,%xmm8
	pxor	%xmm0,%xmm3
	movdqa	%xmm14,%xmm6
	pandn	%xmm11,%xmm7
	movdqa	%xmm3,%xmm5
	pand	%xmm10,%xmm6
	movdqa	%xmm13,%xmm9
	psrld	$31,%xmm5
	paddd	%xmm3,%xmm3

	movdqa	%xmm2,16-128(%rax)
	paddd	%xmm2,%xmm12
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6

	movdqa	%xmm14,%xmm7
	por	%xmm9,%xmm8
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	80-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	pxor	176-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	movdqa	%xmm13,%xmm7
	pslld	$5,%xmm8
	pxor	%xmm1,%xmm4
	movdqa	%xmm13,%xmm6
	pandn	%xmm10,%xmm7
	movdqa	%xmm4,%xmm5
	pand	%xmm14,%xmm6
	movdqa	%xmm12,%xmm9
	psrld	$31,%xmm5
	paddd	%xmm4,%xmm4

	movdqa	%xmm3,32-128(%rax)
	paddd	%xmm3,%xmm11
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6

	movdqa	%xmm13,%xmm7
	por	%xmm9,%xmm8
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	96-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	pxor	192-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	movdqa	%xmm12,%xmm7
	pslld	$5,%xmm8
	pxor	%xmm2,%xmm0
	movdqa	%xmm12,%xmm6
	pandn	%xmm14,%xmm7
	movdqa	%xmm0,%xmm5
	pand	%xmm13,%xmm6
	movdqa	%xmm11,%xmm9
	psrld	$31,%xmm5
	paddd	%xmm0,%xmm0

	movdqa	%xmm4,48-128(%rax)
	paddd	%xmm4,%xmm10
	psrld	$27,%xmm9
	pxor	%xmm7,%xmm6

	movdqa	%xmm12,%xmm7
	por	%xmm9,%xmm8
	pslld	$30,%xmm7
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	movdqa	0(%rbp),%xmm15
	pxor	%xmm3,%xmm1
	movdqa	112-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	208-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,64-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	128-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	224-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,80-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	144-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	240-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	movdqa	%xmm2,96-128(%rax)
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	160-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	0-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	movdqa	%xmm3,112-128(%rax)
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	176-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	16-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	movdqa	%xmm4,128-128(%rax)
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	192-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	32-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,144-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	208-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	48-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,160-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	224-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	64-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	movdqa	%xmm2,176-128(%rax)
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	240-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	80-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	movdqa	%xmm3,192-128(%rax)
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	0-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	96-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	movdqa	%xmm4,208-128(%rax)
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	16-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	112-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,224-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	32-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	128-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,240-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	48-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	144-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	movdqa	%xmm2,0-128(%rax)
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	64-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	160-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	movdqa	%xmm3,16-128(%rax)
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	80-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	176-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	movdqa	%xmm4,32-128(%rax)
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	96-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	192-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,48-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	112-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	208-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,64-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	128-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	224-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	movdqa	%xmm2,80-128(%rax)
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	144-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	240-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	movdqa	%xmm3,96-128(%rax)
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	160-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	0-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	movdqa	%xmm4,112-128(%rax)
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	movdqa	32(%rbp),%xmm15
	pxor	%xmm3,%xmm1
	movdqa	176-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm7
	pxor	16-128(%rax),%xmm1
	pxor	%xmm3,%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	movdqa	%xmm10,%xmm9
	pand	%xmm12,%xmm7

	movdqa	%xmm13,%xmm6
	movdqa	%xmm1,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm14
	pxor	%xmm12,%xmm6

	movdqa	%xmm0,128-128(%rax)
	paddd	%xmm0,%xmm14
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm11,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm1,%xmm1
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	192-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm7
	pxor	32-128(%rax),%xmm2
	pxor	%xmm4,%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	movdqa	%xmm14,%xmm9
	pand	%xmm11,%xmm7

	movdqa	%xmm12,%xmm6
	movdqa	%xmm2,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm13
	pxor	%xmm11,%xmm6

	movdqa	%xmm1,144-128(%rax)
	paddd	%xmm1,%xmm13
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm10,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm2,%xmm2
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	208-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm7
	pxor	48-128(%rax),%xmm3
	pxor	%xmm0,%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	movdqa	%xmm13,%xmm9
	pand	%xmm10,%xmm7

	movdqa	%xmm11,%xmm6
	movdqa	%xmm3,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm12
	pxor	%xmm10,%xmm6

	movdqa	%xmm2,160-128(%rax)
	paddd	%xmm2,%xmm12
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm14,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm3,%xmm3
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	224-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm7
	pxor	64-128(%rax),%xmm4
	pxor	%xmm1,%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	movdqa	%xmm12,%xmm9
	pand	%xmm14,%xmm7

	movdqa	%xmm10,%xmm6
	movdqa	%xmm4,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm11
	pxor	%xmm14,%xmm6

	movdqa	%xmm3,176-128(%rax)
	paddd	%xmm3,%xmm11
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm13,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm4,%xmm4
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	240-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm7
	pxor	80-128(%rax),%xmm0
	pxor	%xmm2,%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	movdqa	%xmm11,%xmm9
	pand	%xmm13,%xmm7

	movdqa	%xmm14,%xmm6
	movdqa	%xmm0,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm10
	pxor	%xmm13,%xmm6

	movdqa	%xmm4,192-128(%rax)
	paddd	%xmm4,%xmm10
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm12,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm0,%xmm0
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	0-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm7
	pxor	96-128(%rax),%xmm1
	pxor	%xmm3,%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	movdqa	%xmm10,%xmm9
	pand	%xmm12,%xmm7

	movdqa	%xmm13,%xmm6
	movdqa	%xmm1,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm14
	pxor	%xmm12,%xmm6

	movdqa	%xmm0,208-128(%rax)
	paddd	%xmm0,%xmm14
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm11,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm1,%xmm1
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	16-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm7
	pxor	112-128(%rax),%xmm2
	pxor	%xmm4,%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	movdqa	%xmm14,%xmm9
	pand	%xmm11,%xmm7

	movdqa	%xmm12,%xmm6
	movdqa	%xmm2,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm13
	pxor	%xmm11,%xmm6

	movdqa	%xmm1,224-128(%rax)
	paddd	%xmm1,%xmm13
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm10,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm2,%xmm2
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	32-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm7
	pxor	128-128(%rax),%xmm3
	pxor	%xmm0,%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	movdqa	%xmm13,%xmm9
	pand	%xmm10,%xmm7

	movdqa	%xmm11,%xmm6
	movdqa	%xmm3,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm12
	pxor	%xmm10,%xmm6

	movdqa	%xmm2,240-128(%rax)
	paddd	%xmm2,%xmm12
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm14,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm3,%xmm3
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	48-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm7
	pxor	144-128(%rax),%xmm4
	pxor	%xmm1,%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	movdqa	%xmm12,%xmm9
	pand	%xmm14,%xmm7

	movdqa	%xmm10,%xmm6
	movdqa	%xmm4,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm11
	pxor	%xmm14,%xmm6

	movdqa	%xmm3,0-128(%rax)
	paddd	%xmm3,%xmm11
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm13,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm4,%xmm4
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	64-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm7
	pxor	160-128(%rax),%xmm0
	pxor	%xmm2,%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	movdqa	%xmm11,%xmm9
	pand	%xmm13,%xmm7

	movdqa	%xmm14,%xmm6
	movdqa	%xmm0,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm10
	pxor	%xmm13,%xmm6

	movdqa	%xmm4,16-128(%rax)
	paddd	%xmm4,%xmm10
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm12,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm0,%xmm0
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	80-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm7
	pxor	176-128(%rax),%xmm1
	pxor	%xmm3,%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	movdqa	%xmm10,%xmm9
	pand	%xmm12,%xmm7

	movdqa	%xmm13,%xmm6
	movdqa	%xmm1,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm14
	pxor	%xmm12,%xmm6

	movdqa	%xmm0,32-128(%rax)
	paddd	%xmm0,%xmm14
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm11,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm1,%xmm1
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	96-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm7
	pxor	192-128(%rax),%xmm2
	pxor	%xmm4,%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	movdqa	%xmm14,%xmm9
	pand	%xmm11,%xmm7

	movdqa	%xmm12,%xmm6
	movdqa	%xmm2,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm13
	pxor	%xmm11,%xmm6

	movdqa	%xmm1,48-128(%rax)
	paddd	%xmm1,%xmm13
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm10,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm2,%xmm2
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	112-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm7
	pxor	208-128(%rax),%xmm3
	pxor	%xmm0,%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	movdqa	%xmm13,%xmm9
	pand	%xmm10,%xmm7

	movdqa	%xmm11,%xmm6
	movdqa	%xmm3,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm12
	pxor	%xmm10,%xmm6

	movdqa	%xmm2,64-128(%rax)
	paddd	%xmm2,%xmm12
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm14,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm3,%xmm3
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	128-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm7
	pxor	224-128(%rax),%xmm4
	pxor	%xmm1,%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	movdqa	%xmm12,%xmm9
	pand	%xmm14,%xmm7

	movdqa	%xmm10,%xmm6
	movdqa	%xmm4,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm11
	pxor	%xmm14,%xmm6

	movdqa	%xmm3,80-128(%rax)
	paddd	%xmm3,%xmm11
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm13,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm4,%xmm4
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	144-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm7
	pxor	240-128(%rax),%xmm0
	pxor	%xmm2,%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	movdqa	%xmm11,%xmm9
	pand	%xmm13,%xmm7

	movdqa	%xmm14,%xmm6
	movdqa	%xmm0,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm10
	pxor	%xmm13,%xmm6

	movdqa	%xmm4,96-128(%rax)
	paddd	%xmm4,%xmm10
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm12,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm0,%xmm0
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	160-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm7
	pxor	0-128(%rax),%xmm1
	pxor	%xmm3,%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	movdqa	%xmm10,%xmm9
	pand	%xmm12,%xmm7

	movdqa	%xmm13,%xmm6
	movdqa	%xmm1,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm14
	pxor	%xmm12,%xmm6

	movdqa	%xmm0,112-128(%rax)
	paddd	%xmm0,%xmm14
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm11,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm1,%xmm1
	paddd	%xmm6,%xmm14

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	176-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm7
	pxor	16-128(%rax),%xmm2
	pxor	%xmm4,%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	movdqa	%xmm14,%xmm9
	pand	%xmm11,%xmm7

	movdqa	%xmm12,%xmm6
	movdqa	%xmm2,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm13
	pxor	%xmm11,%xmm6

	movdqa	%xmm1,128-128(%rax)
	paddd	%xmm1,%xmm13
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm10,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm2,%xmm2
	paddd	%xmm6,%xmm13

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	192-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm7
	pxor	32-128(%rax),%xmm3
	pxor	%xmm0,%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	movdqa	%xmm13,%xmm9
	pand	%xmm10,%xmm7

	movdqa	%xmm11,%xmm6
	movdqa	%xmm3,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm12
	pxor	%xmm10,%xmm6

	movdqa	%xmm2,144-128(%rax)
	paddd	%xmm2,%xmm12
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm14,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm3,%xmm3
	paddd	%xmm6,%xmm12

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	208-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm7
	pxor	48-128(%rax),%xmm4
	pxor	%xmm1,%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	movdqa	%xmm12,%xmm9
	pand	%xmm14,%xmm7

	movdqa	%xmm10,%xmm6
	movdqa	%xmm4,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm11
	pxor	%xmm14,%xmm6

	movdqa	%xmm3,160-128(%rax)
	paddd	%xmm3,%xmm11
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm13,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm4,%xmm4
	paddd	%xmm6,%xmm11

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	224-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm7
	pxor	64-128(%rax),%xmm0
	pxor	%xmm2,%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	movdqa	%xmm11,%xmm9
	pand	%xmm13,%xmm7

	movdqa	%xmm14,%xmm6
	movdqa	%xmm0,%xmm5
	psrld	$27,%xmm9
	paddd	%xmm7,%xmm10
	pxor	%xmm13,%xmm6

	movdqa	%xmm4,176-128(%rax)
	paddd	%xmm4,%xmm10
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	pand	%xmm12,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	paddd	%xmm0,%xmm0
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	movdqa	64(%rbp),%xmm15
	pxor	%xmm3,%xmm1
	movdqa	240-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	80-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,192-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	0-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	96-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,208-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	16-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	112-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	movdqa	%xmm2,224-128(%rax)
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	32-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	128-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	movdqa	%xmm3,240-128(%rax)
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	48-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	144-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	movdqa	%xmm4,0-128(%rax)
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	64-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	160-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,16-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	80-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	176-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,32-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	96-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	192-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	movdqa	%xmm2,48-128(%rax)
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	112-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	208-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	movdqa	%xmm3,64-128(%rax)
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	128-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	224-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	movdqa	%xmm4,80-128(%rax)
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	144-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	240-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	movdqa	%xmm0,96-128(%rax)
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	160-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	0-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	movdqa	%xmm1,112-128(%rax)
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	176-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	16-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	192-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	32-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	pxor	%xmm2,%xmm0
	movdqa	208-128(%rax),%xmm2

	movdqa	%xmm11,%xmm8
	movdqa	%xmm14,%xmm6
	pxor	48-128(%rax),%xmm0
	paddd	%xmm15,%xmm10
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	paddd	%xmm4,%xmm10
	pxor	%xmm2,%xmm0
	psrld	$27,%xmm9
	pxor	%xmm13,%xmm6
	movdqa	%xmm12,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm0,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm10
	paddd	%xmm0,%xmm0

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm5,%xmm0
	por	%xmm7,%xmm12
	pxor	%xmm3,%xmm1
	movdqa	224-128(%rax),%xmm3

	movdqa	%xmm10,%xmm8
	movdqa	%xmm13,%xmm6
	pxor	64-128(%rax),%xmm1
	paddd	%xmm15,%xmm14
	pslld	$5,%xmm8
	pxor	%xmm11,%xmm6

	movdqa	%xmm10,%xmm9
	paddd	%xmm0,%xmm14
	pxor	%xmm3,%xmm1
	psrld	$27,%xmm9
	pxor	%xmm12,%xmm6
	movdqa	%xmm11,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm1,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm14
	paddd	%xmm1,%xmm1

	psrld	$2,%xmm11
	paddd	%xmm8,%xmm14
	por	%xmm5,%xmm1
	por	%xmm7,%xmm11
	pxor	%xmm4,%xmm2
	movdqa	240-128(%rax),%xmm4

	movdqa	%xmm14,%xmm8
	movdqa	%xmm12,%xmm6
	pxor	80-128(%rax),%xmm2
	paddd	%xmm15,%xmm13
	pslld	$5,%xmm8
	pxor	%xmm10,%xmm6

	movdqa	%xmm14,%xmm9
	paddd	%xmm1,%xmm13
	pxor	%xmm4,%xmm2
	psrld	$27,%xmm9
	pxor	%xmm11,%xmm6
	movdqa	%xmm10,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm2,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm13
	paddd	%xmm2,%xmm2

	psrld	$2,%xmm10
	paddd	%xmm8,%xmm13
	por	%xmm5,%xmm2
	por	%xmm7,%xmm10
	pxor	%xmm0,%xmm3
	movdqa	0-128(%rax),%xmm0

	movdqa	%xmm13,%xmm8
	movdqa	%xmm11,%xmm6
	pxor	96-128(%rax),%xmm3
	paddd	%xmm15,%xmm12
	pslld	$5,%xmm8
	pxor	%xmm14,%xmm6

	movdqa	%xmm13,%xmm9
	paddd	%xmm2,%xmm12
	pxor	%xmm0,%xmm3
	psrld	$27,%xmm9
	pxor	%xmm10,%xmm6
	movdqa	%xmm14,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm3,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm12
	paddd	%xmm3,%xmm3

	psrld	$2,%xmm14
	paddd	%xmm8,%xmm12
	por	%xmm5,%xmm3
	por	%xmm7,%xmm14
	pxor	%xmm1,%xmm4
	movdqa	16-128(%rax),%xmm1

	movdqa	%xmm12,%xmm8
	movdqa	%xmm10,%xmm6
	pxor	112-128(%rax),%xmm4
	paddd	%xmm15,%xmm11
	pslld	$5,%xmm8
	pxor	%xmm13,%xmm6

	movdqa	%xmm12,%xmm9
	paddd	%xmm3,%xmm11
	pxor	%xmm1,%xmm4
	psrld	$27,%xmm9
	pxor	%xmm14,%xmm6
	movdqa	%xmm13,%xmm7

	pslld	$30,%xmm7
	movdqa	%xmm4,%xmm5
	por	%xmm9,%xmm8
	psrld	$31,%xmm5
	paddd	%xmm6,%xmm11
	paddd	%xmm4,%xmm4

	psrld	$2,%xmm13
	paddd	%xmm8,%xmm11
	por	%xmm5,%xmm4
	por	%xmm7,%xmm13
	movdqa	%xmm11,%xmm8
	paddd	%xmm15,%xmm10
	movdqa	%xmm14,%xmm6
	pslld	$5,%xmm8
	pxor	%xmm12,%xmm6

	movdqa	%xmm11,%xmm9
	paddd	%xmm4,%xmm10
	psrld	$27,%xmm9
	movdqa	%xmm12,%xmm7
	pxor	%xmm13,%xmm6

	pslld	$30,%xmm7
	por	%xmm9,%xmm8
	paddd	%xmm6,%xmm10

	psrld	$2,%xmm12
	paddd	%xmm8,%xmm10
	por	%xmm7,%xmm12
	movdqa	(%rbx),%xmm0
	movl	$1,%ecx
	cmpl	0(%rbx),%ecx
	pxor	%xmm8,%xmm8
	cmovgeq	%rbp,%r8
	cmpl	4(%rbx),%ecx
	movdqa	%xmm0,%xmm1
	cmovgeq	%rbp,%r9
	cmpl	8(%rbx),%ecx
	pcmpgtd	%xmm8,%xmm1
	cmovgeq	%rbp,%r10
	cmpl	12(%rbx),%ecx
	paddd	%xmm1,%xmm0
	cmovgeq	%rbp,%r11

	movdqu	0(%rdi),%xmm6
	pand	%xmm1,%xmm10
	movdqu	32(%rdi),%xmm7
	pand	%xmm1,%xmm11
	paddd	%xmm6,%xmm10
	movdqu	64(%rdi),%xmm8
	pand	%xmm1,%xmm12
	paddd	%xmm7,%xmm11
	movdqu	96(%rdi),%xmm9
	pand	%xmm1,%xmm13
	paddd	%xmm8,%xmm12
	movdqu	128(%rdi),%xmm5
	pand	%xmm1,%xmm14
	movdqu	%xmm10,0(%rdi)
	paddd	%xmm9,%xmm13
	movdqu	%xmm11,32(%rdi)
	paddd	%xmm5,%xmm14
	movdqu	%xmm12,64(%rdi)
	movdqu	%xmm13,96(%rdi)
	movdqu	%xmm14,128(%rdi)

	movdqa	%xmm0,(%rbx)
	movdqa	96(%rbp),%xmm5
	movdqa	-32(%rbp),%xmm15
	decl	%edx
	jnz	.Loop

	movl	280(%rsp),%edx
	leaq	16(%rdi),%rdi
	leaq	64(%rsi),%rsi
	decl	%edx
	jnz	.Loop_grande

.Ldone:
	movq	272(%rsp),%rax
.cfi_def_cfa	%rax,8
	movq	-16(%rax),%rbp
.cfi_restore	%rbp
	movq	-8(%rax),%rbx
.cfi_restore	%rbx
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	sha1_multi_block,.-sha1_multi_block
.type	sha1_multi_block_shaext,@function
.align	32
sha1_multi_block_shaext:
.cfi_startproc	
_shaext_shortcut:
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	pushq	%rbx
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_offset	%rbp,-24
	subq	$288,%rsp
	shll	$1,%edx
	andq	$-256,%rsp
	leaq	64(%rdi),%rdi
	movq	%rax,272(%rsp)
.Lbody_shaext:
	leaq	256(%rsp),%rbx
	movdqa	K_XX_XX+128(%rip),%xmm3

.Loop_grande_shaext:
	movl	%edx,280(%rsp)
	xorl	%edx,%edx
	movq	0(%rsi),%r8
	movl	8(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,0(%rbx)
	cmovleq	%rsp,%r8
	movq	16(%rsi),%r9
	movl	24(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,4(%rbx)
	cmovleq	%rsp,%r9
	testl	%edx,%edx
	jz	.Ldone_shaext

	movq	0-64(%rdi),%xmm0
	movq	32-64(%rdi),%xmm4
	movq	64-64(%rdi),%xmm5
	movq	96-64(%rdi),%xmm6
	movq	128-64(%rdi),%xmm7

	punpckldq	%xmm4,%xmm0
	punpckldq	%xmm6,%xmm5

	movdqa	%xmm0,%xmm8
	punpcklqdq	%xmm5,%xmm0
	punpckhqdq	%xmm5,%xmm8

	pshufd	$63,%xmm7,%xmm1
	pshufd	$127,%xmm7,%xmm9
	pshufd	$27,%xmm0,%xmm0
	pshufd	$27,%xmm8,%xmm8
	jmp	.Loop_shaext

.align	32
.Loop_shaext:
	movdqu	0(%r8),%xmm4
	movdqu	0(%r9),%xmm11
	movdqu	16(%r8),%xmm5
	movdqu	16(%r9),%xmm12
	movdqu	32(%r8),%xmm6
.byte	102,15,56,0,227
	movdqu	32(%r9),%xmm13
.byte	102,68,15,56,0,219
	movdqu	48(%r8),%xmm7
	leaq	64(%r8),%r8
.byte	102,15,56,0,235
	movdqu	48(%r9),%xmm14
	leaq	64(%r9),%r9
.byte	102,68,15,56,0,227

	movdqa	%xmm1,80(%rsp)
	paddd	%xmm4,%xmm1
	movdqa	%xmm9,112(%rsp)
	paddd	%xmm11,%xmm9
	movdqa	%xmm0,64(%rsp)
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,96(%rsp)
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,0
.byte	15,56,200,213
.byte	69,15,58,204,193,0
.byte	69,15,56,200,212
.byte	102,15,56,0,243
	prefetcht0	127(%r8)
.byte	15,56,201,229
.byte	102,68,15,56,0,235
	prefetcht0	127(%r9)
.byte	69,15,56,201,220

.byte	102,15,56,0,251
	movdqa	%xmm0,%xmm1
.byte	102,68,15,56,0,243
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,0
.byte	15,56,200,206
.byte	69,15,58,204,194,0
.byte	69,15,56,200,205
	pxor	%xmm6,%xmm4
.byte	15,56,201,238
	pxor	%xmm13,%xmm11
.byte	69,15,56,201,229
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,0
.byte	15,56,200,215
.byte	69,15,58,204,193,0
.byte	69,15,56,200,214
.byte	15,56,202,231
.byte	69,15,56,202,222
	pxor	%xmm7,%xmm5
.byte	15,56,201,247
	pxor	%xmm14,%xmm12
.byte	69,15,56,201,238
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,0
.byte	15,56,200,204
.byte	69,15,58,204,194,0
.byte	69,15,56,200,203
.byte	15,56,202,236
.byte	69,15,56,202,227
	pxor	%xmm4,%xmm6
.byte	15,56,201,252
	pxor	%xmm11,%xmm13
.byte	69,15,56,201,243
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,0
.byte	15,56,200,213
.byte	69,15,58,204,193,0
.byte	69,15,56,200,212
.byte	15,56,202,245
.byte	69,15,56,202,236
	pxor	%xmm5,%xmm7
.byte	15,56,201,229
	pxor	%xmm12,%xmm14
.byte	69,15,56,201,220
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,1
.byte	15,56,200,206
.byte	69,15,58,204,194,1
.byte	69,15,56,200,205
.byte	15,56,202,254
.byte	69,15,56,202,245
	pxor	%xmm6,%xmm4
.byte	15,56,201,238
	pxor	%xmm13,%xmm11
.byte	69,15,56,201,229
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,1
.byte	15,56,200,215
.byte	69,15,58,204,193,1
.byte	69,15,56,200,214
.byte	15,56,202,231
.byte	69,15,56,202,222
	pxor	%xmm7,%xmm5
.byte	15,56,201,247
	pxor	%xmm14,%xmm12
.byte	69,15,56,201,238
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,1
.byte	15,56,200,204
.byte	69,15,58,204,194,1
.byte	69,15,56,200,203
.byte	15,56,202,236
.byte	69,15,56,202,227
	pxor	%xmm4,%xmm6
.byte	15,56,201,252
	pxor	%xmm11,%xmm13
.byte	69,15,56,201,243
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,1
.byte	15,56,200,213
.byte	69,15,58,204,193,1
.byte	69,15,56,200,212
.byte	15,56,202,245
.byte	69,15,56,202,236
	pxor	%xmm5,%xmm7
.byte	15,56,201,229
	pxor	%xmm12,%xmm14
.byte	69,15,56,201,220
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,1
.byte	15,56,200,206
.byte	69,15,58,204,194,1
.byte	69,15,56,200,205
.byte	15,56,202,254
.byte	69,15,56,202,245
	pxor	%xmm6,%xmm4
.byte	15,56,201,238
	pxor	%xmm13,%xmm11
.byte	69,15,56,201,229
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,2
.byte	15,56,200,215
.byte	69,15,58,204,193,2
.byte	69,15,56,200,214
.byte	15,56,202,231
.byte	69,15,56,202,222
	pxor	%xmm7,%xmm5
.byte	15,56,201,247
	pxor	%xmm14,%xmm12
.byte	69,15,56,201,238
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,2
.byte	15,56,200,204
.byte	69,15,58,204,194,2
.byte	69,15,56,200,203
.byte	15,56,202,236
.byte	69,15,56,202,227
	pxor	%xmm4,%xmm6
.byte	15,56,201,252
	pxor	%xmm11,%xmm13
.byte	69,15,56,201,243
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,2
.byte	15,56,200,213
.byte	69,15,58,204,193,2
.byte	69,15,56,200,212
.byte	15,56,202,245
.byte	69,15,56,202,236
	pxor	%xmm5,%xmm7
.byte	15,56,201,229
	pxor	%xmm12,%xmm14
.byte	69,15,56,201,220
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,2
.byte	15,56,200,206
.byte	69,15,58,204,194,2
.byte	69,15,56,200,205
.byte	15,56,202,254
.byte	69,15,56,202,245
	pxor	%xmm6,%xmm4
.byte	15,56,201,238
	pxor	%xmm13,%xmm11
.byte	69,15,56,201,229
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,2
.byte	15,56,200,215
.byte	69,15,58,204,193,2
.byte	69,15,56,200,214
.byte	15,56,202,231
.byte	69,15,56,202,222
	pxor	%xmm7,%xmm5
.byte	15,56,201,247
	pxor	%xmm14,%xmm12
.byte	69,15,56,201,238
	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,3
.byte	15,56,200,204
.byte	69,15,58,204,194,3
.byte	69,15,56,200,203
.byte	15,56,202,236
.byte	69,15,56,202,227
	pxor	%xmm4,%xmm6
.byte	15,56,201,252
	pxor	%xmm11,%xmm13
.byte	69,15,56,201,243
	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,3
.byte	15,56,200,213
.byte	69,15,58,204,193,3
.byte	69,15,56,200,212
.byte	15,56,202,245
.byte	69,15,56,202,236
	pxor	%xmm5,%xmm7
	pxor	%xmm12,%xmm14

	movl	$1,%ecx
	pxor	%xmm4,%xmm4
	cmpl	0(%rbx),%ecx
	cmovgeq	%rsp,%r8

	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,3
.byte	15,56,200,206
.byte	69,15,58,204,194,3
.byte	69,15,56,200,205
.byte	15,56,202,254
.byte	69,15,56,202,245

	cmpl	4(%rbx),%ecx
	cmovgeq	%rsp,%r9
	movq	(%rbx),%xmm6

	movdqa	%xmm0,%xmm2
	movdqa	%xmm8,%xmm10
.byte	15,58,204,193,3
.byte	15,56,200,215
.byte	69,15,58,204,193,3
.byte	69,15,56,200,214

	pshufd	$0x00,%xmm6,%xmm11
	pshufd	$0x55,%xmm6,%xmm12
	movdqa	%xmm6,%xmm7
	pcmpgtd	%xmm4,%xmm11
	pcmpgtd	%xmm4,%xmm12

	movdqa	%xmm0,%xmm1
	movdqa	%xmm8,%xmm9
.byte	15,58,204,194,3
.byte	15,56,200,204
.byte	69,15,58,204,194,3
.byte	68,15,56,200,204

	pcmpgtd	%xmm4,%xmm7
	pand	%xmm11,%xmm0
	pand	%xmm11,%xmm1
	pand	%xmm12,%xmm8
	pand	%xmm12,%xmm9
	paddd	%xmm7,%xmm6

	paddd	64(%rsp),%xmm0
	paddd	80(%rsp),%xmm1
	paddd	96(%rsp),%xmm8
	paddd	112(%rsp),%xmm9

	movq	%xmm6,(%rbx)
	decl	%edx
	jnz	.Loop_shaext

	movl	280(%rsp),%edx

	pshufd	$27,%xmm0,%xmm0
	pshufd	$27,%xmm8,%xmm8

	movdqa	%xmm0,%xmm6
	punpckldq	%xmm8,%xmm0
	punpckhdq	%xmm8,%xmm6
	punpckhdq	%xmm9,%xmm1
	movq	%xmm0,0-64(%rdi)
	psrldq	$8,%xmm0
	movq	%xmm6,64-64(%rdi)
	psrldq	$8,%xmm6
	movq	%xmm0,32-64(%rdi)
	psrldq	$8,%xmm1
	movq	%xmm6,96-64(%rdi)
	movq	%xmm1,128-64(%rdi)

	leaq	8(%rdi),%rdi
	leaq	32(%rsi),%rsi
	decl	%edx
	jnz	.Loop_grande_shaext

.Ldone_shaext:

	movq	-16(%rax),%rbp
.cfi_restore	%rbp
	movq	-8(%rax),%rbx
.cfi_restore	%rbx
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_shaext:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	sha1_multi_block_shaext,.-sha1_multi_block_shaext
.type	sha1_multi_block_avx,@function
.align	32
sha1_multi_block_avx:
.cfi_startproc	
_avx_shortcut:
	shrq	$32,%rcx
	cmpl	$2,%edx
	jb	.Lavx
	testl	$32,%ecx
	jnz	_avx2_shortcut
	jmp	.Lavx
.align	32
.Lavx:
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	pushq	%rbx
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_offset	%rbp,-24
	subq	$288,%rsp
	andq	$-256,%rsp
	movq	%rax,272(%rsp)
.cfi_escape	0x0f,0x06,0x77,0x90,0x02,0x06,0x23,0x08
.Lbody_avx:
	leaq	K_XX_XX(%rip),%rbp
	leaq	256(%rsp),%rbx

	vzeroupper
.Loop_grande_avx:
	movl	%edx,280(%rsp)
	xorl	%edx,%edx
	movq	0(%rsi),%r8
	movl	8(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,0(%rbx)
	cmovleq	%rbp,%r8
	movq	16(%rsi),%r9
	movl	24(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,4(%rbx)
	cmovleq	%rbp,%r9
	movq	32(%rsi),%r10
	movl	40(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,8(%rbx)
	cmovleq	%rbp,%r10
	movq	48(%rsi),%r11
	movl	56(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,12(%rbx)
	cmovleq	%rbp,%r11
	testl	%edx,%edx
	jz	.Ldone_avx

	vmovdqu	0(%rdi),%xmm10
	leaq	128(%rsp),%rax
	vmovdqu	32(%rdi),%xmm11
	vmovdqu	64(%rdi),%xmm12
	vmovdqu	96(%rdi),%xmm13
	vmovdqu	128(%rdi),%xmm14
	vmovdqu	96(%rbp),%xmm5
	jmp	.Loop_avx

.align	32
.Loop_avx:
	vmovdqa	-32(%rbp),%xmm15
	vmovd	(%r8),%xmm0
	leaq	64(%r8),%r8
	vmovd	(%r9),%xmm2
	leaq	64(%r9),%r9
	vpinsrd	$1,(%r10),%xmm0,%xmm0
	leaq	64(%r10),%r10
	vpinsrd	$1,(%r11),%xmm2,%xmm2
	leaq	64(%r11),%r11
	vmovd	-60(%r8),%xmm1
	vpunpckldq	%xmm2,%xmm0,%xmm0
	vmovd	-60(%r9),%xmm9
	vpshufb	%xmm5,%xmm0,%xmm0
	vpinsrd	$1,-60(%r10),%xmm1,%xmm1
	vpinsrd	$1,-60(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpandn	%xmm13,%xmm11,%xmm7
	vpand	%xmm12,%xmm11,%xmm6

	vmovdqa	%xmm0,0-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpunpckldq	%xmm9,%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-56(%r8),%xmm2

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-56(%r9),%xmm9
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpshufb	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpinsrd	$1,-56(%r10),%xmm2,%xmm2
	vpinsrd	$1,-56(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpandn	%xmm12,%xmm10,%xmm7
	vpand	%xmm11,%xmm10,%xmm6

	vmovdqa	%xmm1,16-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpunpckldq	%xmm9,%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-52(%r8),%xmm3

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-52(%r9),%xmm9
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpshufb	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpinsrd	$1,-52(%r10),%xmm3,%xmm3
	vpinsrd	$1,-52(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpandn	%xmm11,%xmm14,%xmm7
	vpand	%xmm10,%xmm14,%xmm6

	vmovdqa	%xmm2,32-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpunpckldq	%xmm9,%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-48(%r8),%xmm4

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-48(%r9),%xmm9
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpshufb	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpinsrd	$1,-48(%r10),%xmm4,%xmm4
	vpinsrd	$1,-48(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpandn	%xmm10,%xmm13,%xmm7
	vpand	%xmm14,%xmm13,%xmm6

	vmovdqa	%xmm3,48-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpunpckldq	%xmm9,%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-44(%r8),%xmm0

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-44(%r9),%xmm9
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpshufb	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpinsrd	$1,-44(%r10),%xmm0,%xmm0
	vpinsrd	$1,-44(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpandn	%xmm14,%xmm12,%xmm7
	vpand	%xmm13,%xmm12,%xmm6

	vmovdqa	%xmm4,64-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpunpckldq	%xmm9,%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-40(%r8),%xmm1

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-40(%r9),%xmm9
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpshufb	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpinsrd	$1,-40(%r10),%xmm1,%xmm1
	vpinsrd	$1,-40(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpandn	%xmm13,%xmm11,%xmm7
	vpand	%xmm12,%xmm11,%xmm6

	vmovdqa	%xmm0,80-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpunpckldq	%xmm9,%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-36(%r8),%xmm2

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-36(%r9),%xmm9
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpshufb	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpinsrd	$1,-36(%r10),%xmm2,%xmm2
	vpinsrd	$1,-36(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpandn	%xmm12,%xmm10,%xmm7
	vpand	%xmm11,%xmm10,%xmm6

	vmovdqa	%xmm1,96-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpunpckldq	%xmm9,%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-32(%r8),%xmm3

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-32(%r9),%xmm9
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpshufb	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpinsrd	$1,-32(%r10),%xmm3,%xmm3
	vpinsrd	$1,-32(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpandn	%xmm11,%xmm14,%xmm7
	vpand	%xmm10,%xmm14,%xmm6

	vmovdqa	%xmm2,112-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpunpckldq	%xmm9,%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-28(%r8),%xmm4

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-28(%r9),%xmm9
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpshufb	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpinsrd	$1,-28(%r10),%xmm4,%xmm4
	vpinsrd	$1,-28(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpandn	%xmm10,%xmm13,%xmm7
	vpand	%xmm14,%xmm13,%xmm6

	vmovdqa	%xmm3,128-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpunpckldq	%xmm9,%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-24(%r8),%xmm0

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-24(%r9),%xmm9
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpshufb	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpinsrd	$1,-24(%r10),%xmm0,%xmm0
	vpinsrd	$1,-24(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpandn	%xmm14,%xmm12,%xmm7
	vpand	%xmm13,%xmm12,%xmm6

	vmovdqa	%xmm4,144-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpunpckldq	%xmm9,%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-20(%r8),%xmm1

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-20(%r9),%xmm9
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpshufb	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpinsrd	$1,-20(%r10),%xmm1,%xmm1
	vpinsrd	$1,-20(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpandn	%xmm13,%xmm11,%xmm7
	vpand	%xmm12,%xmm11,%xmm6

	vmovdqa	%xmm0,160-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpunpckldq	%xmm9,%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-16(%r8),%xmm2

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-16(%r9),%xmm9
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpshufb	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpinsrd	$1,-16(%r10),%xmm2,%xmm2
	vpinsrd	$1,-16(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpandn	%xmm12,%xmm10,%xmm7
	vpand	%xmm11,%xmm10,%xmm6

	vmovdqa	%xmm1,176-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpunpckldq	%xmm9,%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-12(%r8),%xmm3

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-12(%r9),%xmm9
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpshufb	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpinsrd	$1,-12(%r10),%xmm3,%xmm3
	vpinsrd	$1,-12(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpandn	%xmm11,%xmm14,%xmm7
	vpand	%xmm10,%xmm14,%xmm6

	vmovdqa	%xmm2,192-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpunpckldq	%xmm9,%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-8(%r8),%xmm4

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-8(%r9),%xmm9
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpshufb	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpinsrd	$1,-8(%r10),%xmm4,%xmm4
	vpinsrd	$1,-8(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpandn	%xmm10,%xmm13,%xmm7
	vpand	%xmm14,%xmm13,%xmm6

	vmovdqa	%xmm3,208-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpunpckldq	%xmm9,%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vmovd	-4(%r8),%xmm0

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vmovd	-4(%r9),%xmm9
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpshufb	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vmovdqa	0-128(%rax),%xmm1
	vpinsrd	$1,-4(%r10),%xmm0,%xmm0
	vpinsrd	$1,-4(%r11),%xmm9,%xmm9
	vpaddd	%xmm15,%xmm10,%xmm10
	prefetcht0	63(%r8)
	vpslld	$5,%xmm11,%xmm8
	vpandn	%xmm14,%xmm12,%xmm7
	vpand	%xmm13,%xmm12,%xmm6

	vmovdqa	%xmm4,224-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpunpckldq	%xmm9,%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	prefetcht0	63(%r9)
	vpxor	%xmm7,%xmm6,%xmm6

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	prefetcht0	63(%r10)
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	prefetcht0	63(%r11)
	vpshufb	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vmovdqa	16-128(%rax),%xmm2
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	32-128(%rax),%xmm3

	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpandn	%xmm13,%xmm11,%xmm7

	vpand	%xmm12,%xmm11,%xmm6

	vmovdqa	%xmm0,240-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	128-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1


	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11

	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	48-128(%rax),%xmm4

	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpandn	%xmm12,%xmm10,%xmm7

	vpand	%xmm11,%xmm10,%xmm6

	vmovdqa	%xmm1,0-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	144-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2


	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10

	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	64-128(%rax),%xmm0

	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpandn	%xmm11,%xmm14,%xmm7

	vpand	%xmm10,%xmm14,%xmm6

	vmovdqa	%xmm2,16-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	160-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3


	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14

	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	80-128(%rax),%xmm1

	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpandn	%xmm10,%xmm13,%xmm7

	vpand	%xmm14,%xmm13,%xmm6

	vmovdqa	%xmm3,32-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	176-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4


	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13

	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	96-128(%rax),%xmm2

	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpandn	%xmm14,%xmm12,%xmm7

	vpand	%xmm13,%xmm12,%xmm6

	vmovdqa	%xmm4,48-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	192-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm7,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0


	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12

	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vmovdqa	0(%rbp),%xmm15
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	112-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,64-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	208-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	128-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,80-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	224-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	144-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vmovdqa	%xmm2,96-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	240-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	160-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vmovdqa	%xmm3,112-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	0-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	176-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vmovdqa	%xmm4,128-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	16-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	192-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,144-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	32-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	208-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,160-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	48-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	224-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vmovdqa	%xmm2,176-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	64-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	240-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vmovdqa	%xmm3,192-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	80-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	0-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vmovdqa	%xmm4,208-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	96-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	16-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,224-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	112-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	32-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,240-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	128-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	48-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vmovdqa	%xmm2,0-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	144-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	64-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vmovdqa	%xmm3,16-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	160-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	80-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vmovdqa	%xmm4,32-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	176-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	96-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,48-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	192-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	112-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,64-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	208-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	128-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vmovdqa	%xmm2,80-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	224-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	144-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vmovdqa	%xmm3,96-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	240-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	160-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vmovdqa	%xmm4,112-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	0-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vmovdqa	32(%rbp),%xmm15
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	176-128(%rax),%xmm3

	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpand	%xmm12,%xmm13,%xmm7
	vpxor	16-128(%rax),%xmm1,%xmm1

	vpaddd	%xmm7,%xmm14,%xmm14
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm13,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vmovdqu	%xmm0,128-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm1,%xmm5
	vpand	%xmm11,%xmm6,%xmm6
	vpaddd	%xmm1,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	192-128(%rax),%xmm4

	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpand	%xmm11,%xmm12,%xmm7
	vpxor	32-128(%rax),%xmm2,%xmm2

	vpaddd	%xmm7,%xmm13,%xmm13
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm12,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vmovdqu	%xmm1,144-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm2,%xmm5
	vpand	%xmm10,%xmm6,%xmm6
	vpaddd	%xmm2,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	208-128(%rax),%xmm0

	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpand	%xmm10,%xmm11,%xmm7
	vpxor	48-128(%rax),%xmm3,%xmm3

	vpaddd	%xmm7,%xmm12,%xmm12
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm11,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vmovdqu	%xmm2,160-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm3,%xmm5
	vpand	%xmm14,%xmm6,%xmm6
	vpaddd	%xmm3,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	224-128(%rax),%xmm1

	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpand	%xmm14,%xmm10,%xmm7
	vpxor	64-128(%rax),%xmm4,%xmm4

	vpaddd	%xmm7,%xmm11,%xmm11
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm10,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vmovdqu	%xmm3,176-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm4,%xmm5
	vpand	%xmm13,%xmm6,%xmm6
	vpaddd	%xmm4,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	240-128(%rax),%xmm2

	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpand	%xmm13,%xmm14,%xmm7
	vpxor	80-128(%rax),%xmm0,%xmm0

	vpaddd	%xmm7,%xmm10,%xmm10
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm14,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vmovdqu	%xmm4,192-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm0,%xmm5
	vpand	%xmm12,%xmm6,%xmm6
	vpaddd	%xmm0,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	0-128(%rax),%xmm3

	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpand	%xmm12,%xmm13,%xmm7
	vpxor	96-128(%rax),%xmm1,%xmm1

	vpaddd	%xmm7,%xmm14,%xmm14
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm13,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vmovdqu	%xmm0,208-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm1,%xmm5
	vpand	%xmm11,%xmm6,%xmm6
	vpaddd	%xmm1,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	16-128(%rax),%xmm4

	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpand	%xmm11,%xmm12,%xmm7
	vpxor	112-128(%rax),%xmm2,%xmm2

	vpaddd	%xmm7,%xmm13,%xmm13
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm12,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vmovdqu	%xmm1,224-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm2,%xmm5
	vpand	%xmm10,%xmm6,%xmm6
	vpaddd	%xmm2,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	32-128(%rax),%xmm0

	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpand	%xmm10,%xmm11,%xmm7
	vpxor	128-128(%rax),%xmm3,%xmm3

	vpaddd	%xmm7,%xmm12,%xmm12
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm11,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vmovdqu	%xmm2,240-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm3,%xmm5
	vpand	%xmm14,%xmm6,%xmm6
	vpaddd	%xmm3,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	48-128(%rax),%xmm1

	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpand	%xmm14,%xmm10,%xmm7
	vpxor	144-128(%rax),%xmm4,%xmm4

	vpaddd	%xmm7,%xmm11,%xmm11
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm10,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vmovdqu	%xmm3,0-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm4,%xmm5
	vpand	%xmm13,%xmm6,%xmm6
	vpaddd	%xmm4,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	64-128(%rax),%xmm2

	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpand	%xmm13,%xmm14,%xmm7
	vpxor	160-128(%rax),%xmm0,%xmm0

	vpaddd	%xmm7,%xmm10,%xmm10
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm14,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vmovdqu	%xmm4,16-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm0,%xmm5
	vpand	%xmm12,%xmm6,%xmm6
	vpaddd	%xmm0,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	80-128(%rax),%xmm3

	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpand	%xmm12,%xmm13,%xmm7
	vpxor	176-128(%rax),%xmm1,%xmm1

	vpaddd	%xmm7,%xmm14,%xmm14
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm13,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vmovdqu	%xmm0,32-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm1,%xmm5
	vpand	%xmm11,%xmm6,%xmm6
	vpaddd	%xmm1,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	96-128(%rax),%xmm4

	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpand	%xmm11,%xmm12,%xmm7
	vpxor	192-128(%rax),%xmm2,%xmm2

	vpaddd	%xmm7,%xmm13,%xmm13
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm12,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vmovdqu	%xmm1,48-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm2,%xmm5
	vpand	%xmm10,%xmm6,%xmm6
	vpaddd	%xmm2,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	112-128(%rax),%xmm0

	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpand	%xmm10,%xmm11,%xmm7
	vpxor	208-128(%rax),%xmm3,%xmm3

	vpaddd	%xmm7,%xmm12,%xmm12
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm11,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vmovdqu	%xmm2,64-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm3,%xmm5
	vpand	%xmm14,%xmm6,%xmm6
	vpaddd	%xmm3,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	128-128(%rax),%xmm1

	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpand	%xmm14,%xmm10,%xmm7
	vpxor	224-128(%rax),%xmm4,%xmm4

	vpaddd	%xmm7,%xmm11,%xmm11
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm10,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vmovdqu	%xmm3,80-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm4,%xmm5
	vpand	%xmm13,%xmm6,%xmm6
	vpaddd	%xmm4,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	144-128(%rax),%xmm2

	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpand	%xmm13,%xmm14,%xmm7
	vpxor	240-128(%rax),%xmm0,%xmm0

	vpaddd	%xmm7,%xmm10,%xmm10
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm14,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vmovdqu	%xmm4,96-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm0,%xmm5
	vpand	%xmm12,%xmm6,%xmm6
	vpaddd	%xmm0,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	160-128(%rax),%xmm3

	vpaddd	%xmm15,%xmm14,%xmm14
	vpslld	$5,%xmm10,%xmm8
	vpand	%xmm12,%xmm13,%xmm7
	vpxor	0-128(%rax),%xmm1,%xmm1

	vpaddd	%xmm7,%xmm14,%xmm14
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm13,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vmovdqu	%xmm0,112-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm1,%xmm5
	vpand	%xmm11,%xmm6,%xmm6
	vpaddd	%xmm1,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpaddd	%xmm6,%xmm14,%xmm14

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	176-128(%rax),%xmm4

	vpaddd	%xmm15,%xmm13,%xmm13
	vpslld	$5,%xmm14,%xmm8
	vpand	%xmm11,%xmm12,%xmm7
	vpxor	16-128(%rax),%xmm2,%xmm2

	vpaddd	%xmm7,%xmm13,%xmm13
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm12,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vmovdqu	%xmm1,128-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm2,%xmm5
	vpand	%xmm10,%xmm6,%xmm6
	vpaddd	%xmm2,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpaddd	%xmm6,%xmm13,%xmm13

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	192-128(%rax),%xmm0

	vpaddd	%xmm15,%xmm12,%xmm12
	vpslld	$5,%xmm13,%xmm8
	vpand	%xmm10,%xmm11,%xmm7
	vpxor	32-128(%rax),%xmm3,%xmm3

	vpaddd	%xmm7,%xmm12,%xmm12
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm11,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vmovdqu	%xmm2,144-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm3,%xmm5
	vpand	%xmm14,%xmm6,%xmm6
	vpaddd	%xmm3,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpaddd	%xmm6,%xmm12,%xmm12

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	208-128(%rax),%xmm1

	vpaddd	%xmm15,%xmm11,%xmm11
	vpslld	$5,%xmm12,%xmm8
	vpand	%xmm14,%xmm10,%xmm7
	vpxor	48-128(%rax),%xmm4,%xmm4

	vpaddd	%xmm7,%xmm11,%xmm11
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm10,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vmovdqu	%xmm3,160-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm4,%xmm5
	vpand	%xmm13,%xmm6,%xmm6
	vpaddd	%xmm4,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpaddd	%xmm6,%xmm11,%xmm11

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	224-128(%rax),%xmm2

	vpaddd	%xmm15,%xmm10,%xmm10
	vpslld	$5,%xmm11,%xmm8
	vpand	%xmm13,%xmm14,%xmm7
	vpxor	64-128(%rax),%xmm0,%xmm0

	vpaddd	%xmm7,%xmm10,%xmm10
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm14,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vmovdqu	%xmm4,176-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpor	%xmm9,%xmm8,%xmm8
	vpsrld	$31,%xmm0,%xmm5
	vpand	%xmm12,%xmm6,%xmm6
	vpaddd	%xmm0,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vmovdqa	64(%rbp),%xmm15
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	240-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,192-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	80-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	0-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,208-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	96-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	16-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vmovdqa	%xmm2,224-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	112-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	32-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vmovdqa	%xmm3,240-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	128-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	48-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vmovdqa	%xmm4,0-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	144-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	64-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,16-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	160-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	80-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,32-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	176-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	96-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vmovdqa	%xmm2,48-128(%rax)
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	192-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	112-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vmovdqa	%xmm3,64-128(%rax)
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	208-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	128-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vmovdqa	%xmm4,80-128(%rax)
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	224-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	144-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vmovdqa	%xmm0,96-128(%rax)
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	240-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	160-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vmovdqa	%xmm1,112-128(%rax)
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	0-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	176-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	16-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	192-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	32-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpxor	%xmm2,%xmm0,%xmm0
	vmovdqa	208-128(%rax),%xmm2

	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	48-128(%rax),%xmm0,%xmm0
	vpsrld	$27,%xmm11,%xmm9
	vpxor	%xmm13,%xmm6,%xmm6
	vpxor	%xmm2,%xmm0,%xmm0

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10
	vpsrld	$31,%xmm0,%xmm5
	vpaddd	%xmm0,%xmm0,%xmm0

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm5,%xmm0,%xmm0
	vpor	%xmm7,%xmm12,%xmm12
	vpxor	%xmm3,%xmm1,%xmm1
	vmovdqa	224-128(%rax),%xmm3

	vpslld	$5,%xmm10,%xmm8
	vpaddd	%xmm15,%xmm14,%xmm14
	vpxor	%xmm11,%xmm13,%xmm6
	vpaddd	%xmm0,%xmm14,%xmm14
	vpxor	64-128(%rax),%xmm1,%xmm1
	vpsrld	$27,%xmm10,%xmm9
	vpxor	%xmm12,%xmm6,%xmm6
	vpxor	%xmm3,%xmm1,%xmm1

	vpslld	$30,%xmm11,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm14,%xmm14
	vpsrld	$31,%xmm1,%xmm5
	vpaddd	%xmm1,%xmm1,%xmm1

	vpsrld	$2,%xmm11,%xmm11
	vpaddd	%xmm8,%xmm14,%xmm14
	vpor	%xmm5,%xmm1,%xmm1
	vpor	%xmm7,%xmm11,%xmm11
	vpxor	%xmm4,%xmm2,%xmm2
	vmovdqa	240-128(%rax),%xmm4

	vpslld	$5,%xmm14,%xmm8
	vpaddd	%xmm15,%xmm13,%xmm13
	vpxor	%xmm10,%xmm12,%xmm6
	vpaddd	%xmm1,%xmm13,%xmm13
	vpxor	80-128(%rax),%xmm2,%xmm2
	vpsrld	$27,%xmm14,%xmm9
	vpxor	%xmm11,%xmm6,%xmm6
	vpxor	%xmm4,%xmm2,%xmm2

	vpslld	$30,%xmm10,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm13,%xmm13
	vpsrld	$31,%xmm2,%xmm5
	vpaddd	%xmm2,%xmm2,%xmm2

	vpsrld	$2,%xmm10,%xmm10
	vpaddd	%xmm8,%xmm13,%xmm13
	vpor	%xmm5,%xmm2,%xmm2
	vpor	%xmm7,%xmm10,%xmm10
	vpxor	%xmm0,%xmm3,%xmm3
	vmovdqa	0-128(%rax),%xmm0

	vpslld	$5,%xmm13,%xmm8
	vpaddd	%xmm15,%xmm12,%xmm12
	vpxor	%xmm14,%xmm11,%xmm6
	vpaddd	%xmm2,%xmm12,%xmm12
	vpxor	96-128(%rax),%xmm3,%xmm3
	vpsrld	$27,%xmm13,%xmm9
	vpxor	%xmm10,%xmm6,%xmm6
	vpxor	%xmm0,%xmm3,%xmm3

	vpslld	$30,%xmm14,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12
	vpsrld	$31,%xmm3,%xmm5
	vpaddd	%xmm3,%xmm3,%xmm3

	vpsrld	$2,%xmm14,%xmm14
	vpaddd	%xmm8,%xmm12,%xmm12
	vpor	%xmm5,%xmm3,%xmm3
	vpor	%xmm7,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqa	16-128(%rax),%xmm1

	vpslld	$5,%xmm12,%xmm8
	vpaddd	%xmm15,%xmm11,%xmm11
	vpxor	%xmm13,%xmm10,%xmm6
	vpaddd	%xmm3,%xmm11,%xmm11
	vpxor	112-128(%rax),%xmm4,%xmm4
	vpsrld	$27,%xmm12,%xmm9
	vpxor	%xmm14,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm4

	vpslld	$30,%xmm13,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm11,%xmm11
	vpsrld	$31,%xmm4,%xmm5
	vpaddd	%xmm4,%xmm4,%xmm4

	vpsrld	$2,%xmm13,%xmm13
	vpaddd	%xmm8,%xmm11,%xmm11
	vpor	%xmm5,%xmm4,%xmm4
	vpor	%xmm7,%xmm13,%xmm13
	vpslld	$5,%xmm11,%xmm8
	vpaddd	%xmm15,%xmm10,%xmm10
	vpxor	%xmm12,%xmm14,%xmm6

	vpsrld	$27,%xmm11,%xmm9
	vpaddd	%xmm4,%xmm10,%xmm10
	vpxor	%xmm13,%xmm6,%xmm6

	vpslld	$30,%xmm12,%xmm7
	vpor	%xmm9,%xmm8,%xmm8
	vpaddd	%xmm6,%xmm10,%xmm10

	vpsrld	$2,%xmm12,%xmm12
	vpaddd	%xmm8,%xmm10,%xmm10
	vpor	%xmm7,%xmm12,%xmm12
	movl	$1,%ecx
	cmpl	0(%rbx),%ecx
	cmovgeq	%rbp,%r8
	cmpl	4(%rbx),%ecx
	cmovgeq	%rbp,%r9
	cmpl	8(%rbx),%ecx
	cmovgeq	%rbp,%r10
	cmpl	12(%rbx),%ecx
	cmovgeq	%rbp,%r11
	vmovdqu	(%rbx),%xmm6
	vpxor	%xmm8,%xmm8,%xmm8
	vmovdqa	%xmm6,%xmm7
	vpcmpgtd	%xmm8,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6

	vpand	%xmm7,%xmm10,%xmm10
	vpand	%xmm7,%xmm11,%xmm11
	vpaddd	0(%rdi),%xmm10,%xmm10
	vpand	%xmm7,%xmm12,%xmm12
	vpaddd	32(%rdi),%xmm11,%xmm11
	vpand	%xmm7,%xmm13,%xmm13
	vpaddd	64(%rdi),%xmm12,%xmm12
	vpand	%xmm7,%xmm14,%xmm14
	vpaddd	96(%rdi),%xmm13,%xmm13
	vpaddd	128(%rdi),%xmm14,%xmm14
	vmovdqu	%xmm10,0(%rdi)
	vmovdqu	%xmm11,32(%rdi)
	vmovdqu	%xmm12,64(%rdi)
	vmovdqu	%xmm13,96(%rdi)
	vmovdqu	%xmm14,128(%rdi)

	vmovdqu	%xmm6,(%rbx)
	vmovdqu	96(%rbp),%xmm5
	decl	%edx
	jnz	.Loop_avx

	movl	280(%rsp),%edx
	leaq	16(%rdi),%rdi
	leaq	64(%rsi),%rsi
	decl	%edx
	jnz	.Loop_grande_avx

.Ldone_avx:
	movq	272(%rsp),%rax
.cfi_def_cfa	%rax,8
	vzeroupper
	movq	-16(%rax),%rbp
.cfi_restore	%rbp
	movq	-8(%rax),%rbx
.cfi_restore	%rbx
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	sha1_multi_block_avx,.-sha1_multi_block_avx
.type	sha1_multi_block_avx2,@function
.align	32
sha1_multi_block_avx2:
.cfi_startproc	
_avx2_shortcut:
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	pushq	%rbx
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_offset	%rbp,-24
	pushq	%r12
.cfi_offset	%r12,-32
	pushq	%r13
.cfi_offset	%r13,-40
	pushq	%r14
.cfi_offset	%r14,-48
	pushq	%r15
.cfi_offset	%r15,-56
	subq	$576,%rsp
	andq	$-256,%rsp
	movq	%rax,544(%rsp)
.cfi_escape	0x0f,0x06,0x77,0xa0,0x04,0x06,0x23,0x08
.Lbody_avx2:
	leaq	K_XX_XX(%rip),%rbp
	shrl	$1,%edx

	vzeroupper
.Loop_grande_avx2:
	movl	%edx,552(%rsp)
	xorl	%edx,%edx
	leaq	512(%rsp),%rbx
	movq	0(%rsi),%r12
	movl	8(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,0(%rbx)
	cmovleq	%rbp,%r12
	movq	16(%rsi),%r13
	movl	24(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,4(%rbx)
	cmovleq	%rbp,%r13
	movq	32(%rsi),%r14
	movl	40(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,8(%rbx)
	cmovleq	%rbp,%r14
	movq	48(%rsi),%r15
	movl	56(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,12(%rbx)
	cmovleq	%rbp,%r15
	movq	64(%rsi),%r8
	movl	72(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,16(%rbx)
	cmovleq	%rbp,%r8
	movq	80(%rsi),%r9
	movl	88(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,20(%rbx)
	cmovleq	%rbp,%r9
	movq	96(%rsi),%r10
	movl	104(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,24(%rbx)
	cmovleq	%rbp,%r10
	movq	112(%rsi),%r11
	movl	120(%rsi),%ecx
	cmpl	%edx,%ecx
	cmovgl	%ecx,%edx
	testl	%ecx,%ecx
	movl	%ecx,28(%rbx)
	cmovleq	%rbp,%r11
	vmovdqu	0(%rdi),%ymm0
	leaq	128(%rsp),%rax
	vmovdqu	32(%rdi),%ymm1
	leaq	256+128(%rsp),%rbx
	vmovdqu	64(%rdi),%ymm2
	vmovdqu	96(%rdi),%ymm3
	vmovdqu	128(%rdi),%ymm4
	vmovdqu	96(%rbp),%ymm9
	jmp	.Loop_avx2

.align	32
.Loop_avx2:
	vmovdqa	-32(%rbp),%ymm15
	vmovd	(%r12),%xmm10
	leaq	64(%r12),%r12
	vmovd	(%r8),%xmm12
	leaq	64(%r8),%r8
	vmovd	(%r13),%xmm7
	leaq	64(%r13),%r13
	vmovd	(%r9),%xmm6
	leaq	64(%r9),%r9
	vpinsrd	$1,(%r14),%xmm10,%xmm10
	leaq	64(%r14),%r14
	vpinsrd	$1,(%r10),%xmm12,%xmm12
	leaq	64(%r10),%r10
	vpinsrd	$1,(%r15),%xmm7,%xmm7
	leaq	64(%r15),%r15
	vpunpckldq	%ymm7,%ymm10,%ymm10
	vpinsrd	$1,(%r11),%xmm6,%xmm6
	leaq	64(%r11),%r11
	vpunpckldq	%ymm6,%ymm12,%ymm12
	vmovd	-60(%r12),%xmm11
	vinserti128	$1,%xmm12,%ymm10,%ymm10
	vmovd	-60(%r8),%xmm8
	vpshufb	%ymm9,%ymm10,%ymm10
	vmovd	-60(%r13),%xmm7
	vmovd	-60(%r9),%xmm6
	vpinsrd	$1,-60(%r14),%xmm11,%xmm11
	vpinsrd	$1,-60(%r10),%xmm8,%xmm8
	vpinsrd	$1,-60(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm11,%ymm11
	vpinsrd	$1,-60(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpandn	%ymm3,%ymm1,%ymm6
	vpand	%ymm2,%ymm1,%ymm5

	vmovdqa	%ymm10,0-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vinserti128	$1,%xmm8,%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-56(%r12),%xmm12

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-56(%r8),%xmm8
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpshufb	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vmovd	-56(%r13),%xmm7
	vmovd	-56(%r9),%xmm6
	vpinsrd	$1,-56(%r14),%xmm12,%xmm12
	vpinsrd	$1,-56(%r10),%xmm8,%xmm8
	vpinsrd	$1,-56(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm12,%ymm12
	vpinsrd	$1,-56(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpandn	%ymm2,%ymm0,%ymm6
	vpand	%ymm1,%ymm0,%ymm5

	vmovdqa	%ymm11,32-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vinserti128	$1,%xmm8,%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-52(%r12),%xmm13

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-52(%r8),%xmm8
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpshufb	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vmovd	-52(%r13),%xmm7
	vmovd	-52(%r9),%xmm6
	vpinsrd	$1,-52(%r14),%xmm13,%xmm13
	vpinsrd	$1,-52(%r10),%xmm8,%xmm8
	vpinsrd	$1,-52(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm13,%ymm13
	vpinsrd	$1,-52(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpandn	%ymm1,%ymm4,%ymm6
	vpand	%ymm0,%ymm4,%ymm5

	vmovdqa	%ymm12,64-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vinserti128	$1,%xmm8,%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-48(%r12),%xmm14

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-48(%r8),%xmm8
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpshufb	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vmovd	-48(%r13),%xmm7
	vmovd	-48(%r9),%xmm6
	vpinsrd	$1,-48(%r14),%xmm14,%xmm14
	vpinsrd	$1,-48(%r10),%xmm8,%xmm8
	vpinsrd	$1,-48(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm14,%ymm14
	vpinsrd	$1,-48(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpandn	%ymm0,%ymm3,%ymm6
	vpand	%ymm4,%ymm3,%ymm5

	vmovdqa	%ymm13,96-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vinserti128	$1,%xmm8,%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-44(%r12),%xmm10

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-44(%r8),%xmm8
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpshufb	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vmovd	-44(%r13),%xmm7
	vmovd	-44(%r9),%xmm6
	vpinsrd	$1,-44(%r14),%xmm10,%xmm10
	vpinsrd	$1,-44(%r10),%xmm8,%xmm8
	vpinsrd	$1,-44(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm10,%ymm10
	vpinsrd	$1,-44(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpandn	%ymm4,%ymm2,%ymm6
	vpand	%ymm3,%ymm2,%ymm5

	vmovdqa	%ymm14,128-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vinserti128	$1,%xmm8,%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-40(%r12),%xmm11

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-40(%r8),%xmm8
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpshufb	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vmovd	-40(%r13),%xmm7
	vmovd	-40(%r9),%xmm6
	vpinsrd	$1,-40(%r14),%xmm11,%xmm11
	vpinsrd	$1,-40(%r10),%xmm8,%xmm8
	vpinsrd	$1,-40(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm11,%ymm11
	vpinsrd	$1,-40(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpandn	%ymm3,%ymm1,%ymm6
	vpand	%ymm2,%ymm1,%ymm5

	vmovdqa	%ymm10,160-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vinserti128	$1,%xmm8,%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-36(%r12),%xmm12

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-36(%r8),%xmm8
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpshufb	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vmovd	-36(%r13),%xmm7
	vmovd	-36(%r9),%xmm6
	vpinsrd	$1,-36(%r14),%xmm12,%xmm12
	vpinsrd	$1,-36(%r10),%xmm8,%xmm8
	vpinsrd	$1,-36(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm12,%ymm12
	vpinsrd	$1,-36(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpandn	%ymm2,%ymm0,%ymm6
	vpand	%ymm1,%ymm0,%ymm5

	vmovdqa	%ymm11,192-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vinserti128	$1,%xmm8,%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-32(%r12),%xmm13

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-32(%r8),%xmm8
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpshufb	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vmovd	-32(%r13),%xmm7
	vmovd	-32(%r9),%xmm6
	vpinsrd	$1,-32(%r14),%xmm13,%xmm13
	vpinsrd	$1,-32(%r10),%xmm8,%xmm8
	vpinsrd	$1,-32(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm13,%ymm13
	vpinsrd	$1,-32(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpandn	%ymm1,%ymm4,%ymm6
	vpand	%ymm0,%ymm4,%ymm5

	vmovdqa	%ymm12,224-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vinserti128	$1,%xmm8,%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-28(%r12),%xmm14

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-28(%r8),%xmm8
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpshufb	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vmovd	-28(%r13),%xmm7
	vmovd	-28(%r9),%xmm6
	vpinsrd	$1,-28(%r14),%xmm14,%xmm14
	vpinsrd	$1,-28(%r10),%xmm8,%xmm8
	vpinsrd	$1,-28(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm14,%ymm14
	vpinsrd	$1,-28(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpandn	%ymm0,%ymm3,%ymm6
	vpand	%ymm4,%ymm3,%ymm5

	vmovdqa	%ymm13,256-256-128(%rbx)
	vpaddd	%ymm13,%ymm1,%ymm1
	vinserti128	$1,%xmm8,%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-24(%r12),%xmm10

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-24(%r8),%xmm8
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpshufb	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vmovd	-24(%r13),%xmm7
	vmovd	-24(%r9),%xmm6
	vpinsrd	$1,-24(%r14),%xmm10,%xmm10
	vpinsrd	$1,-24(%r10),%xmm8,%xmm8
	vpinsrd	$1,-24(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm10,%ymm10
	vpinsrd	$1,-24(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpandn	%ymm4,%ymm2,%ymm6
	vpand	%ymm3,%ymm2,%ymm5

	vmovdqa	%ymm14,288-256-128(%rbx)
	vpaddd	%ymm14,%ymm0,%ymm0
	vinserti128	$1,%xmm8,%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-20(%r12),%xmm11

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-20(%r8),%xmm8
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpshufb	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vmovd	-20(%r13),%xmm7
	vmovd	-20(%r9),%xmm6
	vpinsrd	$1,-20(%r14),%xmm11,%xmm11
	vpinsrd	$1,-20(%r10),%xmm8,%xmm8
	vpinsrd	$1,-20(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm11,%ymm11
	vpinsrd	$1,-20(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpandn	%ymm3,%ymm1,%ymm6
	vpand	%ymm2,%ymm1,%ymm5

	vmovdqa	%ymm10,320-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vinserti128	$1,%xmm8,%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-16(%r12),%xmm12

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-16(%r8),%xmm8
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpshufb	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vmovd	-16(%r13),%xmm7
	vmovd	-16(%r9),%xmm6
	vpinsrd	$1,-16(%r14),%xmm12,%xmm12
	vpinsrd	$1,-16(%r10),%xmm8,%xmm8
	vpinsrd	$1,-16(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm12,%ymm12
	vpinsrd	$1,-16(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpandn	%ymm2,%ymm0,%ymm6
	vpand	%ymm1,%ymm0,%ymm5

	vmovdqa	%ymm11,352-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vinserti128	$1,%xmm8,%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-12(%r12),%xmm13

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-12(%r8),%xmm8
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpshufb	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vmovd	-12(%r13),%xmm7
	vmovd	-12(%r9),%xmm6
	vpinsrd	$1,-12(%r14),%xmm13,%xmm13
	vpinsrd	$1,-12(%r10),%xmm8,%xmm8
	vpinsrd	$1,-12(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm13,%ymm13
	vpinsrd	$1,-12(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpandn	%ymm1,%ymm4,%ymm6
	vpand	%ymm0,%ymm4,%ymm5

	vmovdqa	%ymm12,384-256-128(%rbx)
	vpaddd	%ymm12,%ymm2,%ymm2
	vinserti128	$1,%xmm8,%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-8(%r12),%xmm14

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-8(%r8),%xmm8
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpshufb	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vmovd	-8(%r13),%xmm7
	vmovd	-8(%r9),%xmm6
	vpinsrd	$1,-8(%r14),%xmm14,%xmm14
	vpinsrd	$1,-8(%r10),%xmm8,%xmm8
	vpinsrd	$1,-8(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm14,%ymm14
	vpinsrd	$1,-8(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpandn	%ymm0,%ymm3,%ymm6
	vpand	%ymm4,%ymm3,%ymm5

	vmovdqa	%ymm13,416-256-128(%rbx)
	vpaddd	%ymm13,%ymm1,%ymm1
	vinserti128	$1,%xmm8,%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vmovd	-4(%r12),%xmm10

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vmovd	-4(%r8),%xmm8
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpshufb	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vmovdqa	0-128(%rax),%ymm11
	vmovd	-4(%r13),%xmm7
	vmovd	-4(%r9),%xmm6
	vpinsrd	$1,-4(%r14),%xmm10,%xmm10
	vpinsrd	$1,-4(%r10),%xmm8,%xmm8
	vpinsrd	$1,-4(%r15),%xmm7,%xmm7
	vpunpckldq	%ymm7,%ymm10,%ymm10
	vpinsrd	$1,-4(%r11),%xmm6,%xmm6
	vpunpckldq	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm15,%ymm0,%ymm0
	prefetcht0	63(%r12)
	vpslld	$5,%ymm1,%ymm7
	vpandn	%ymm4,%ymm2,%ymm6
	vpand	%ymm3,%ymm2,%ymm5

	vmovdqa	%ymm14,448-256-128(%rbx)
	vpaddd	%ymm14,%ymm0,%ymm0
	vinserti128	$1,%xmm8,%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	prefetcht0	63(%r13)
	vpxor	%ymm6,%ymm5,%ymm5

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	prefetcht0	63(%r14)
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	prefetcht0	63(%r15)
	vpshufb	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vmovdqa	32-128(%rax),%ymm12
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	64-128(%rax),%ymm13

	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpandn	%ymm3,%ymm1,%ymm6
	prefetcht0	63(%r8)
	vpand	%ymm2,%ymm1,%ymm5

	vmovdqa	%ymm10,480-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	256-256-128(%rbx),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11
	prefetcht0	63(%r9)

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	prefetcht0	63(%r10)
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	prefetcht0	63(%r11)
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	96-128(%rax),%ymm14

	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpandn	%ymm2,%ymm0,%ymm6

	vpand	%ymm1,%ymm0,%ymm5

	vmovdqa	%ymm11,0-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	288-256-128(%rbx),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12


	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0

	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	128-128(%rax),%ymm10

	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpandn	%ymm1,%ymm4,%ymm6

	vpand	%ymm0,%ymm4,%ymm5

	vmovdqa	%ymm12,32-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	320-256-128(%rbx),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13


	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4

	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	160-128(%rax),%ymm11

	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpandn	%ymm0,%ymm3,%ymm6

	vpand	%ymm4,%ymm3,%ymm5

	vmovdqa	%ymm13,64-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	352-256-128(%rbx),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14


	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3

	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	192-128(%rax),%ymm12

	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpandn	%ymm4,%ymm2,%ymm6

	vpand	%ymm3,%ymm2,%ymm5

	vmovdqa	%ymm14,96-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	384-256-128(%rbx),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm6,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10


	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2

	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vmovdqa	0(%rbp),%ymm15
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	224-128(%rax),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,128-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	416-256-128(%rbx),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	256-256-128(%rbx),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,160-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	448-256-128(%rbx),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	288-256-128(%rbx),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vmovdqa	%ymm12,192-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	480-256-128(%rbx),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	320-256-128(%rbx),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vmovdqa	%ymm13,224-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	0-128(%rax),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	352-256-128(%rbx),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vmovdqa	%ymm14,256-256-128(%rbx)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	32-128(%rax),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	384-256-128(%rbx),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,288-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	64-128(%rax),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	416-256-128(%rbx),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,320-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	96-128(%rax),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	448-256-128(%rbx),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vmovdqa	%ymm12,352-256-128(%rbx)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	128-128(%rax),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	480-256-128(%rbx),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vmovdqa	%ymm13,384-256-128(%rbx)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	160-128(%rax),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	0-128(%rax),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vmovdqa	%ymm14,416-256-128(%rbx)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	192-128(%rax),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	32-128(%rax),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,448-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	224-128(%rax),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	64-128(%rax),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,480-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	256-256-128(%rbx),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	96-128(%rax),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vmovdqa	%ymm12,0-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	288-256-128(%rbx),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	128-128(%rax),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vmovdqa	%ymm13,32-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	320-256-128(%rbx),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	160-128(%rax),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vmovdqa	%ymm14,64-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	352-256-128(%rbx),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	192-128(%rax),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,96-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	384-256-128(%rbx),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	224-128(%rax),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,128-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	416-256-128(%rbx),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	256-256-128(%rbx),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vmovdqa	%ymm12,160-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	448-256-128(%rbx),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	288-256-128(%rbx),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vmovdqa	%ymm13,192-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	480-256-128(%rbx),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	320-256-128(%rbx),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vmovdqa	%ymm14,224-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	0-128(%rax),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vmovdqa	32(%rbp),%ymm15
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	352-256-128(%rbx),%ymm13

	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpand	%ymm2,%ymm3,%ymm6
	vpxor	32-128(%rax),%ymm11,%ymm11

	vpaddd	%ymm6,%ymm4,%ymm4
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm3,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vmovdqu	%ymm10,256-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm11,%ymm9
	vpand	%ymm1,%ymm5,%ymm5
	vpaddd	%ymm11,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	384-256-128(%rbx),%ymm14

	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpand	%ymm1,%ymm2,%ymm6
	vpxor	64-128(%rax),%ymm12,%ymm12

	vpaddd	%ymm6,%ymm3,%ymm3
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm2,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vmovdqu	%ymm11,288-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm12,%ymm9
	vpand	%ymm0,%ymm5,%ymm5
	vpaddd	%ymm12,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	416-256-128(%rbx),%ymm10

	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpand	%ymm0,%ymm1,%ymm6
	vpxor	96-128(%rax),%ymm13,%ymm13

	vpaddd	%ymm6,%ymm2,%ymm2
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm1,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vmovdqu	%ymm12,320-256-128(%rbx)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm13,%ymm9
	vpand	%ymm4,%ymm5,%ymm5
	vpaddd	%ymm13,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	448-256-128(%rbx),%ymm11

	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpand	%ymm4,%ymm0,%ymm6
	vpxor	128-128(%rax),%ymm14,%ymm14

	vpaddd	%ymm6,%ymm1,%ymm1
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm0,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vmovdqu	%ymm13,352-256-128(%rbx)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm14,%ymm9
	vpand	%ymm3,%ymm5,%ymm5
	vpaddd	%ymm14,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	480-256-128(%rbx),%ymm12

	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpand	%ymm3,%ymm4,%ymm6
	vpxor	160-128(%rax),%ymm10,%ymm10

	vpaddd	%ymm6,%ymm0,%ymm0
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm4,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vmovdqu	%ymm14,384-256-128(%rbx)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm10,%ymm9
	vpand	%ymm2,%ymm5,%ymm5
	vpaddd	%ymm10,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	0-128(%rax),%ymm13

	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpand	%ymm2,%ymm3,%ymm6
	vpxor	192-128(%rax),%ymm11,%ymm11

	vpaddd	%ymm6,%ymm4,%ymm4
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm3,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vmovdqu	%ymm10,416-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm11,%ymm9
	vpand	%ymm1,%ymm5,%ymm5
	vpaddd	%ymm11,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	32-128(%rax),%ymm14

	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpand	%ymm1,%ymm2,%ymm6
	vpxor	224-128(%rax),%ymm12,%ymm12

	vpaddd	%ymm6,%ymm3,%ymm3
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm2,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vmovdqu	%ymm11,448-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm12,%ymm9
	vpand	%ymm0,%ymm5,%ymm5
	vpaddd	%ymm12,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	64-128(%rax),%ymm10

	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpand	%ymm0,%ymm1,%ymm6
	vpxor	256-256-128(%rbx),%ymm13,%ymm13

	vpaddd	%ymm6,%ymm2,%ymm2
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm1,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vmovdqu	%ymm12,480-256-128(%rbx)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm13,%ymm9
	vpand	%ymm4,%ymm5,%ymm5
	vpaddd	%ymm13,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	96-128(%rax),%ymm11

	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpand	%ymm4,%ymm0,%ymm6
	vpxor	288-256-128(%rbx),%ymm14,%ymm14

	vpaddd	%ymm6,%ymm1,%ymm1
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm0,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vmovdqu	%ymm13,0-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm14,%ymm9
	vpand	%ymm3,%ymm5,%ymm5
	vpaddd	%ymm14,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	128-128(%rax),%ymm12

	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpand	%ymm3,%ymm4,%ymm6
	vpxor	320-256-128(%rbx),%ymm10,%ymm10

	vpaddd	%ymm6,%ymm0,%ymm0
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm4,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vmovdqu	%ymm14,32-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm10,%ymm9
	vpand	%ymm2,%ymm5,%ymm5
	vpaddd	%ymm10,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	160-128(%rax),%ymm13

	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpand	%ymm2,%ymm3,%ymm6
	vpxor	352-256-128(%rbx),%ymm11,%ymm11

	vpaddd	%ymm6,%ymm4,%ymm4
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm3,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vmovdqu	%ymm10,64-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm11,%ymm9
	vpand	%ymm1,%ymm5,%ymm5
	vpaddd	%ymm11,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	192-128(%rax),%ymm14

	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpand	%ymm1,%ymm2,%ymm6
	vpxor	384-256-128(%rbx),%ymm12,%ymm12

	vpaddd	%ymm6,%ymm3,%ymm3
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm2,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vmovdqu	%ymm11,96-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm12,%ymm9
	vpand	%ymm0,%ymm5,%ymm5
	vpaddd	%ymm12,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	224-128(%rax),%ymm10

	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpand	%ymm0,%ymm1,%ymm6
	vpxor	416-256-128(%rbx),%ymm13,%ymm13

	vpaddd	%ymm6,%ymm2,%ymm2
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm1,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vmovdqu	%ymm12,128-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm13,%ymm9
	vpand	%ymm4,%ymm5,%ymm5
	vpaddd	%ymm13,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	256-256-128(%rbx),%ymm11

	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpand	%ymm4,%ymm0,%ymm6
	vpxor	448-256-128(%rbx),%ymm14,%ymm14

	vpaddd	%ymm6,%ymm1,%ymm1
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm0,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vmovdqu	%ymm13,160-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm14,%ymm9
	vpand	%ymm3,%ymm5,%ymm5
	vpaddd	%ymm14,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	288-256-128(%rbx),%ymm12

	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpand	%ymm3,%ymm4,%ymm6
	vpxor	480-256-128(%rbx),%ymm10,%ymm10

	vpaddd	%ymm6,%ymm0,%ymm0
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm4,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vmovdqu	%ymm14,192-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm10,%ymm9
	vpand	%ymm2,%ymm5,%ymm5
	vpaddd	%ymm10,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	320-256-128(%rbx),%ymm13

	vpaddd	%ymm15,%ymm4,%ymm4
	vpslld	$5,%ymm0,%ymm7
	vpand	%ymm2,%ymm3,%ymm6
	vpxor	0-128(%rax),%ymm11,%ymm11

	vpaddd	%ymm6,%ymm4,%ymm4
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm3,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vmovdqu	%ymm10,224-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm11,%ymm9
	vpand	%ymm1,%ymm5,%ymm5
	vpaddd	%ymm11,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpaddd	%ymm5,%ymm4,%ymm4

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	352-256-128(%rbx),%ymm14

	vpaddd	%ymm15,%ymm3,%ymm3
	vpslld	$5,%ymm4,%ymm7
	vpand	%ymm1,%ymm2,%ymm6
	vpxor	32-128(%rax),%ymm12,%ymm12

	vpaddd	%ymm6,%ymm3,%ymm3
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm2,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vmovdqu	%ymm11,256-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm12,%ymm9
	vpand	%ymm0,%ymm5,%ymm5
	vpaddd	%ymm12,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpaddd	%ymm5,%ymm3,%ymm3

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	384-256-128(%rbx),%ymm10

	vpaddd	%ymm15,%ymm2,%ymm2
	vpslld	$5,%ymm3,%ymm7
	vpand	%ymm0,%ymm1,%ymm6
	vpxor	64-128(%rax),%ymm13,%ymm13

	vpaddd	%ymm6,%ymm2,%ymm2
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm1,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vmovdqu	%ymm12,288-256-128(%rbx)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm13,%ymm9
	vpand	%ymm4,%ymm5,%ymm5
	vpaddd	%ymm13,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpaddd	%ymm5,%ymm2,%ymm2

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	416-256-128(%rbx),%ymm11

	vpaddd	%ymm15,%ymm1,%ymm1
	vpslld	$5,%ymm2,%ymm7
	vpand	%ymm4,%ymm0,%ymm6
	vpxor	96-128(%rax),%ymm14,%ymm14

	vpaddd	%ymm6,%ymm1,%ymm1
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm0,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vmovdqu	%ymm13,320-256-128(%rbx)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm14,%ymm9
	vpand	%ymm3,%ymm5,%ymm5
	vpaddd	%ymm14,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpaddd	%ymm5,%ymm1,%ymm1

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	448-256-128(%rbx),%ymm12

	vpaddd	%ymm15,%ymm0,%ymm0
	vpslld	$5,%ymm1,%ymm7
	vpand	%ymm3,%ymm4,%ymm6
	vpxor	128-128(%rax),%ymm10,%ymm10

	vpaddd	%ymm6,%ymm0,%ymm0
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm4,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vmovdqu	%ymm14,352-256-128(%rbx)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpor	%ymm8,%ymm7,%ymm7
	vpsrld	$31,%ymm10,%ymm9
	vpand	%ymm2,%ymm5,%ymm5
	vpaddd	%ymm10,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vmovdqa	64(%rbp),%ymm15
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	480-256-128(%rbx),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,384-256-128(%rbx)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	160-128(%rax),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	0-128(%rax),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,416-256-128(%rbx)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	192-128(%rax),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	32-128(%rax),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vmovdqa	%ymm12,448-256-128(%rbx)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	224-128(%rax),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	64-128(%rax),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vmovdqa	%ymm13,480-256-128(%rbx)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	256-256-128(%rbx),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	96-128(%rax),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vmovdqa	%ymm14,0-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	288-256-128(%rbx),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	128-128(%rax),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,32-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	320-256-128(%rbx),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	160-128(%rax),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,64-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	352-256-128(%rbx),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	192-128(%rax),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vmovdqa	%ymm12,96-128(%rax)
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	384-256-128(%rbx),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	224-128(%rax),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vmovdqa	%ymm13,128-128(%rax)
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	416-256-128(%rbx),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	256-256-128(%rbx),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vmovdqa	%ymm14,160-128(%rax)
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	448-256-128(%rbx),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	288-256-128(%rbx),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vmovdqa	%ymm10,192-128(%rax)
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	480-256-128(%rbx),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	320-256-128(%rbx),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vmovdqa	%ymm11,224-128(%rax)
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	0-128(%rax),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	352-256-128(%rbx),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	32-128(%rax),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	384-256-128(%rbx),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	64-128(%rax),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpxor	%ymm12,%ymm10,%ymm10
	vmovdqa	416-256-128(%rbx),%ymm12

	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	96-128(%rax),%ymm10,%ymm10
	vpsrld	$27,%ymm1,%ymm8
	vpxor	%ymm3,%ymm5,%ymm5
	vpxor	%ymm12,%ymm10,%ymm10

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0
	vpsrld	$31,%ymm10,%ymm9
	vpaddd	%ymm10,%ymm10,%ymm10

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm9,%ymm10,%ymm10
	vpor	%ymm6,%ymm2,%ymm2
	vpxor	%ymm13,%ymm11,%ymm11
	vmovdqa	448-256-128(%rbx),%ymm13

	vpslld	$5,%ymm0,%ymm7
	vpaddd	%ymm15,%ymm4,%ymm4
	vpxor	%ymm1,%ymm3,%ymm5
	vpaddd	%ymm10,%ymm4,%ymm4
	vpxor	128-128(%rax),%ymm11,%ymm11
	vpsrld	$27,%ymm0,%ymm8
	vpxor	%ymm2,%ymm5,%ymm5
	vpxor	%ymm13,%ymm11,%ymm11

	vpslld	$30,%ymm1,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm4,%ymm4
	vpsrld	$31,%ymm11,%ymm9
	vpaddd	%ymm11,%ymm11,%ymm11

	vpsrld	$2,%ymm1,%ymm1
	vpaddd	%ymm7,%ymm4,%ymm4
	vpor	%ymm9,%ymm11,%ymm11
	vpor	%ymm6,%ymm1,%ymm1
	vpxor	%ymm14,%ymm12,%ymm12
	vmovdqa	480-256-128(%rbx),%ymm14

	vpslld	$5,%ymm4,%ymm7
	vpaddd	%ymm15,%ymm3,%ymm3
	vpxor	%ymm0,%ymm2,%ymm5
	vpaddd	%ymm11,%ymm3,%ymm3
	vpxor	160-128(%rax),%ymm12,%ymm12
	vpsrld	$27,%ymm4,%ymm8
	vpxor	%ymm1,%ymm5,%ymm5
	vpxor	%ymm14,%ymm12,%ymm12

	vpslld	$30,%ymm0,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm3,%ymm3
	vpsrld	$31,%ymm12,%ymm9
	vpaddd	%ymm12,%ymm12,%ymm12

	vpsrld	$2,%ymm0,%ymm0
	vpaddd	%ymm7,%ymm3,%ymm3
	vpor	%ymm9,%ymm12,%ymm12
	vpor	%ymm6,%ymm0,%ymm0
	vpxor	%ymm10,%ymm13,%ymm13
	vmovdqa	0-128(%rax),%ymm10

	vpslld	$5,%ymm3,%ymm7
	vpaddd	%ymm15,%ymm2,%ymm2
	vpxor	%ymm4,%ymm1,%ymm5
	vpaddd	%ymm12,%ymm2,%ymm2
	vpxor	192-128(%rax),%ymm13,%ymm13
	vpsrld	$27,%ymm3,%ymm8
	vpxor	%ymm0,%ymm5,%ymm5
	vpxor	%ymm10,%ymm13,%ymm13

	vpslld	$30,%ymm4,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm2,%ymm2
	vpsrld	$31,%ymm13,%ymm9
	vpaddd	%ymm13,%ymm13,%ymm13

	vpsrld	$2,%ymm4,%ymm4
	vpaddd	%ymm7,%ymm2,%ymm2
	vpor	%ymm9,%ymm13,%ymm13
	vpor	%ymm6,%ymm4,%ymm4
	vpxor	%ymm11,%ymm14,%ymm14
	vmovdqa	32-128(%rax),%ymm11

	vpslld	$5,%ymm2,%ymm7
	vpaddd	%ymm15,%ymm1,%ymm1
	vpxor	%ymm3,%ymm0,%ymm5
	vpaddd	%ymm13,%ymm1,%ymm1
	vpxor	224-128(%rax),%ymm14,%ymm14
	vpsrld	$27,%ymm2,%ymm8
	vpxor	%ymm4,%ymm5,%ymm5
	vpxor	%ymm11,%ymm14,%ymm14

	vpslld	$30,%ymm3,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm1,%ymm1
	vpsrld	$31,%ymm14,%ymm9
	vpaddd	%ymm14,%ymm14,%ymm14

	vpsrld	$2,%ymm3,%ymm3
	vpaddd	%ymm7,%ymm1,%ymm1
	vpor	%ymm9,%ymm14,%ymm14
	vpor	%ymm6,%ymm3,%ymm3
	vpslld	$5,%ymm1,%ymm7
	vpaddd	%ymm15,%ymm0,%ymm0
	vpxor	%ymm2,%ymm4,%ymm5

	vpsrld	$27,%ymm1,%ymm8
	vpaddd	%ymm14,%ymm0,%ymm0
	vpxor	%ymm3,%ymm5,%ymm5

	vpslld	$30,%ymm2,%ymm6
	vpor	%ymm8,%ymm7,%ymm7
	vpaddd	%ymm5,%ymm0,%ymm0

	vpsrld	$2,%ymm2,%ymm2
	vpaddd	%ymm7,%ymm0,%ymm0
	vpor	%ymm6,%ymm2,%ymm2
	movl	$1,%ecx
	leaq	512(%rsp),%rbx
	cmpl	0(%rbx),%ecx
	cmovgeq	%rbp,%r12
	cmpl	4(%rbx),%ecx
	cmovgeq	%rbp,%r13
	cmpl	8(%rbx),%ecx
	cmovgeq	%rbp,%r14
	cmpl	12(%rbx),%ecx
	cmovgeq	%rbp,%r15
	cmpl	16(%rbx),%ecx
	cmovgeq	%rbp,%r8
	cmpl	20(%rbx),%ecx
	cmovgeq	%rbp,%r9
	cmpl	24(%rbx),%ecx
	cmovgeq	%rbp,%r10
	cmpl	28(%rbx),%ecx
	cmovgeq	%rbp,%r11
	vmovdqu	(%rbx),%ymm5
	vpxor	%ymm7,%ymm7,%ymm7
	vmovdqa	%ymm5,%ymm6
	vpcmpgtd	%ymm7,%ymm6,%ymm6
	vpaddd	%ymm6,%ymm5,%ymm5

	vpand	%ymm6,%ymm0,%ymm0
	vpand	%ymm6,%ymm1,%ymm1
	vpaddd	0(%rdi),%ymm0,%ymm0
	vpand	%ymm6,%ymm2,%ymm2
	vpaddd	32(%rdi),%ymm1,%ymm1
	vpand	%ymm6,%ymm3,%ymm3
	vpaddd	64(%rdi),%ymm2,%ymm2
	vpand	%ymm6,%ymm4,%ymm4
	vpaddd	96(%rdi),%ymm3,%ymm3
	vpaddd	128(%rdi),%ymm4,%ymm4
	vmovdqu	%ymm0,0(%rdi)
	vmovdqu	%ymm1,32(%rdi)
	vmovdqu	%ymm2,64(%rdi)
	vmovdqu	%ymm3,96(%rdi)
	vmovdqu	%ymm4,128(%rdi)

	vmovdqu	%ymm5,(%rbx)
	leaq	256+128(%rsp),%rbx
	vmovdqu	96(%rbp),%ymm9
	decl	%edx
	jnz	.Loop_avx2







.Ldone_avx2:
	movq	544(%rsp),%rax
.cfi_def_cfa	%rax,8
	vzeroupper
	movq	-48(%rax),%r15
.cfi_restore	%r15
	movq	-40(%rax),%r14
.cfi_restore	%r14
	movq	-32(%rax),%r13
.cfi_restore	%r13
	movq	-24(%rax),%r12
.cfi_restore	%r12
	movq	-16(%rax),%rbp
.cfi_restore	%rbp
	movq	-8(%rax),%rbx
.cfi_restore	%rbx
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx2:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	sha1_multi_block_avx2,.-sha1_multi_block_avx2

.align	256
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999
K_XX_XX:
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
.byte	0xf,0xe,0xd,0xc,0xb,0xa,0x9,0x8,0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0
.byte	83,72,65,49,32,109,117,108,116,105,45,98,108,111,99,107,32,116,114,97,110,115,102,111,114,109,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
