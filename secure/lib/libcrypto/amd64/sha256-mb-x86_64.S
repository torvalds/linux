/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from sha256-mb-x86_64.pl. */
.text	



.globl	sha256_multi_block
.type	sha256_multi_block,@function
.align	32
sha256_multi_block:
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
.cfi_offset	%rbp,-24
	subq	$288,%rsp
	andq	$-256,%rsp
	movq	%rax,272(%rsp)
.cfi_escape	0x0f,0x06,0x77,0x90,0x02,0x06,0x23,0x08
.Lbody:
	leaq	K256+128(%rip),%rbp
	leaq	256(%rsp),%rbx
	leaq	128(%rdi),%rdi

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

	movdqu	0-128(%rdi),%xmm8
	leaq	128(%rsp),%rax
	movdqu	32-128(%rdi),%xmm9
	movdqu	64-128(%rdi),%xmm10
	movdqu	96-128(%rdi),%xmm11
	movdqu	128-128(%rdi),%xmm12
	movdqu	160-128(%rdi),%xmm13
	movdqu	192-128(%rdi),%xmm14
	movdqu	224-128(%rdi),%xmm15
	movdqu	.Lpbswap(%rip),%xmm6
	jmp	.Loop

.align	32
.Loop:
	movdqa	%xmm10,%xmm4
	pxor	%xmm9,%xmm4
	movd	0(%r8),%xmm5
	movd	0(%r9),%xmm0
	movd	0(%r10),%xmm1
	movd	0(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm12,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm12,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm12,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,0-128(%rax)
	paddd	%xmm15,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-128(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm12,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm14,%xmm0
	pand	%xmm13,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm8,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm9,%xmm3
	movdqa	%xmm8,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm8,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm9,%xmm15
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm15
	paddd	%xmm5,%xmm11
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm15
	paddd	%xmm7,%xmm15
	movd	4(%r8),%xmm5
	movd	4(%r9),%xmm0
	movd	4(%r10),%xmm1
	movd	4(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm11,%xmm7

	movdqa	%xmm11,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm11,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,16-128(%rax)
	paddd	%xmm14,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-96(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm11,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm13,%xmm0
	pand	%xmm12,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm15,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm8,%xmm4
	movdqa	%xmm15,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm15,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm8,%xmm14
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm14
	paddd	%xmm5,%xmm10
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm14
	paddd	%xmm7,%xmm14
	movd	8(%r8),%xmm5
	movd	8(%r9),%xmm0
	movd	8(%r10),%xmm1
	movd	8(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm10,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm10,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm10,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,32-128(%rax)
	paddd	%xmm13,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm10,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm12,%xmm0
	pand	%xmm11,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm14,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm15,%xmm3
	movdqa	%xmm14,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm14,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm15,%xmm13
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm13
	paddd	%xmm5,%xmm9
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm13
	paddd	%xmm7,%xmm13
	movd	12(%r8),%xmm5
	movd	12(%r9),%xmm0
	movd	12(%r10),%xmm1
	movd	12(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm9,%xmm7

	movdqa	%xmm9,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm9,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,48-128(%rax)
	paddd	%xmm12,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-32(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm9,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm11,%xmm0
	pand	%xmm10,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm13,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm14,%xmm4
	movdqa	%xmm13,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm13,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm14,%xmm12
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm12
	paddd	%xmm5,%xmm8
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm12
	paddd	%xmm7,%xmm12
	movd	16(%r8),%xmm5
	movd	16(%r9),%xmm0
	movd	16(%r10),%xmm1
	movd	16(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm8,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm8,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm8,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,64-128(%rax)
	paddd	%xmm11,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	0(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm8,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm10,%xmm0
	pand	%xmm9,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm12,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm13,%xmm3
	movdqa	%xmm12,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm12,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm13,%xmm11
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm11
	paddd	%xmm5,%xmm15
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm11
	paddd	%xmm7,%xmm11
	movd	20(%r8),%xmm5
	movd	20(%r9),%xmm0
	movd	20(%r10),%xmm1
	movd	20(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm15,%xmm7

	movdqa	%xmm15,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm15,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,80-128(%rax)
	paddd	%xmm10,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	32(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm15,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm9,%xmm0
	pand	%xmm8,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm11,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm12,%xmm4
	movdqa	%xmm11,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm11,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm12,%xmm10
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm10
	paddd	%xmm5,%xmm14
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm10
	paddd	%xmm7,%xmm10
	movd	24(%r8),%xmm5
	movd	24(%r9),%xmm0
	movd	24(%r10),%xmm1
	movd	24(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm14,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm14,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm14,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,96-128(%rax)
	paddd	%xmm9,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm14,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm8,%xmm0
	pand	%xmm15,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm10,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm11,%xmm3
	movdqa	%xmm10,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm10,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm11,%xmm9
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm9
	paddd	%xmm5,%xmm13
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm9
	paddd	%xmm7,%xmm9
	movd	28(%r8),%xmm5
	movd	28(%r9),%xmm0
	movd	28(%r10),%xmm1
	movd	28(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm13,%xmm7

	movdqa	%xmm13,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm13,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,112-128(%rax)
	paddd	%xmm8,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	96(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm13,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm15,%xmm0
	pand	%xmm14,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm9,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm10,%xmm4
	movdqa	%xmm9,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm9,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm10,%xmm8
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm8
	paddd	%xmm5,%xmm12
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm8
	paddd	%xmm7,%xmm8
	leaq	256(%rbp),%rbp
	movd	32(%r8),%xmm5
	movd	32(%r9),%xmm0
	movd	32(%r10),%xmm1
	movd	32(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm12,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm12,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm12,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,128-128(%rax)
	paddd	%xmm15,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-128(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm12,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm14,%xmm0
	pand	%xmm13,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm8,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm9,%xmm3
	movdqa	%xmm8,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm8,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm9,%xmm15
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm15
	paddd	%xmm5,%xmm11
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm15
	paddd	%xmm7,%xmm15
	movd	36(%r8),%xmm5
	movd	36(%r9),%xmm0
	movd	36(%r10),%xmm1
	movd	36(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm11,%xmm7

	movdqa	%xmm11,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm11,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,144-128(%rax)
	paddd	%xmm14,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-96(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm11,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm13,%xmm0
	pand	%xmm12,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm15,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm8,%xmm4
	movdqa	%xmm15,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm15,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm8,%xmm14
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm14
	paddd	%xmm5,%xmm10
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm14
	paddd	%xmm7,%xmm14
	movd	40(%r8),%xmm5
	movd	40(%r9),%xmm0
	movd	40(%r10),%xmm1
	movd	40(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm10,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm10,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm10,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,160-128(%rax)
	paddd	%xmm13,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm10,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm12,%xmm0
	pand	%xmm11,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm14,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm15,%xmm3
	movdqa	%xmm14,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm14,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm15,%xmm13
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm13
	paddd	%xmm5,%xmm9
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm13
	paddd	%xmm7,%xmm13
	movd	44(%r8),%xmm5
	movd	44(%r9),%xmm0
	movd	44(%r10),%xmm1
	movd	44(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm9,%xmm7

	movdqa	%xmm9,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm9,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,176-128(%rax)
	paddd	%xmm12,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-32(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm9,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm11,%xmm0
	pand	%xmm10,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm13,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm14,%xmm4
	movdqa	%xmm13,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm13,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm14,%xmm12
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm12
	paddd	%xmm5,%xmm8
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm12
	paddd	%xmm7,%xmm12
	movd	48(%r8),%xmm5
	movd	48(%r9),%xmm0
	movd	48(%r10),%xmm1
	movd	48(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm8,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm8,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm8,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,192-128(%rax)
	paddd	%xmm11,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	0(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm8,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm10,%xmm0
	pand	%xmm9,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm12,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm13,%xmm3
	movdqa	%xmm12,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm12,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm13,%xmm11
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm11
	paddd	%xmm5,%xmm15
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm11
	paddd	%xmm7,%xmm11
	movd	52(%r8),%xmm5
	movd	52(%r9),%xmm0
	movd	52(%r10),%xmm1
	movd	52(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm15,%xmm7

	movdqa	%xmm15,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm15,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,208-128(%rax)
	paddd	%xmm10,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	32(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm15,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm9,%xmm0
	pand	%xmm8,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm11,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm12,%xmm4
	movdqa	%xmm11,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm11,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm12,%xmm10
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm10
	paddd	%xmm5,%xmm14
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm10
	paddd	%xmm7,%xmm10
	movd	56(%r8),%xmm5
	movd	56(%r9),%xmm0
	movd	56(%r10),%xmm1
	movd	56(%r11),%xmm2
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm14,%xmm7
.byte	102,15,56,0,238
	movdqa	%xmm14,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm14,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,224-128(%rax)
	paddd	%xmm9,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm14,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm8,%xmm0
	pand	%xmm15,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm10,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm11,%xmm3
	movdqa	%xmm10,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm10,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm11,%xmm9
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm9
	paddd	%xmm5,%xmm13
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm9
	paddd	%xmm7,%xmm9
	movd	60(%r8),%xmm5
	leaq	64(%r8),%r8
	movd	60(%r9),%xmm0
	leaq	64(%r9),%r9
	movd	60(%r10),%xmm1
	leaq	64(%r10),%r10
	movd	60(%r11),%xmm2
	leaq	64(%r11),%r11
	punpckldq	%xmm1,%xmm5
	punpckldq	%xmm2,%xmm0
	punpckldq	%xmm0,%xmm5
	movdqa	%xmm13,%xmm7

	movdqa	%xmm13,%xmm2
.byte	102,15,56,0,238
	psrld	$6,%xmm7
	movdqa	%xmm13,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,240-128(%rax)
	paddd	%xmm8,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	96(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm13,%xmm0
	prefetcht0	63(%r8)
	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm15,%xmm0
	pand	%xmm14,%xmm4
	pxor	%xmm1,%xmm7

	prefetcht0	63(%r9)
	movdqa	%xmm9,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm4,%xmm0
	movdqa	%xmm10,%xmm4
	movdqa	%xmm9,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm9,%xmm4

	prefetcht0	63(%r10)
	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1

	prefetcht0	63(%r11)
	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm10,%xmm8
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm8
	paddd	%xmm5,%xmm12
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm8
	paddd	%xmm7,%xmm8
	leaq	256(%rbp),%rbp
	movdqu	0-128(%rax),%xmm5
	movl	$3,%ecx
	jmp	.Loop_16_xx
.align	32
.Loop_16_xx:
	movdqa	16-128(%rax),%xmm6
	paddd	144-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	224-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm12,%xmm7

	movdqa	%xmm12,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm12,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,0-128(%rax)
	paddd	%xmm15,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-128(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm12,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm14,%xmm0
	pand	%xmm13,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm8,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm9,%xmm3
	movdqa	%xmm8,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm8,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm9,%xmm15
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm15
	paddd	%xmm5,%xmm11
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm15
	paddd	%xmm7,%xmm15
	movdqa	32-128(%rax),%xmm5
	paddd	160-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	240-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm11,%xmm7

	movdqa	%xmm11,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm11,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,16-128(%rax)
	paddd	%xmm14,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-96(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm11,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm13,%xmm0
	pand	%xmm12,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm15,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm8,%xmm4
	movdqa	%xmm15,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm15,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm8,%xmm14
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm14
	paddd	%xmm6,%xmm10
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm14
	paddd	%xmm7,%xmm14
	movdqa	48-128(%rax),%xmm6
	paddd	176-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	0-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm10,%xmm7

	movdqa	%xmm10,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm10,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,32-128(%rax)
	paddd	%xmm13,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm10,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm12,%xmm0
	pand	%xmm11,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm14,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm15,%xmm3
	movdqa	%xmm14,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm14,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm15,%xmm13
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm13
	paddd	%xmm5,%xmm9
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm13
	paddd	%xmm7,%xmm13
	movdqa	64-128(%rax),%xmm5
	paddd	192-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	16-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm9,%xmm7

	movdqa	%xmm9,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm9,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,48-128(%rax)
	paddd	%xmm12,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-32(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm9,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm11,%xmm0
	pand	%xmm10,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm13,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm14,%xmm4
	movdqa	%xmm13,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm13,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm14,%xmm12
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm12
	paddd	%xmm6,%xmm8
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm12
	paddd	%xmm7,%xmm12
	movdqa	80-128(%rax),%xmm6
	paddd	208-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	32-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm8,%xmm7

	movdqa	%xmm8,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm8,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,64-128(%rax)
	paddd	%xmm11,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	0(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm8,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm10,%xmm0
	pand	%xmm9,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm12,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm13,%xmm3
	movdqa	%xmm12,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm12,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm13,%xmm11
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm11
	paddd	%xmm5,%xmm15
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm11
	paddd	%xmm7,%xmm11
	movdqa	96-128(%rax),%xmm5
	paddd	224-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	48-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm15,%xmm7

	movdqa	%xmm15,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm15,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,80-128(%rax)
	paddd	%xmm10,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	32(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm15,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm9,%xmm0
	pand	%xmm8,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm11,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm12,%xmm4
	movdqa	%xmm11,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm11,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm12,%xmm10
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm10
	paddd	%xmm6,%xmm14
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm10
	paddd	%xmm7,%xmm10
	movdqa	112-128(%rax),%xmm6
	paddd	240-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	64-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm14,%xmm7

	movdqa	%xmm14,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm14,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,96-128(%rax)
	paddd	%xmm9,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm14,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm8,%xmm0
	pand	%xmm15,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm10,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm11,%xmm3
	movdqa	%xmm10,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm10,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm11,%xmm9
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm9
	paddd	%xmm5,%xmm13
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm9
	paddd	%xmm7,%xmm9
	movdqa	128-128(%rax),%xmm5
	paddd	0-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	80-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm13,%xmm7

	movdqa	%xmm13,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm13,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,112-128(%rax)
	paddd	%xmm8,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	96(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm13,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm15,%xmm0
	pand	%xmm14,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm9,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm10,%xmm4
	movdqa	%xmm9,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm9,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm10,%xmm8
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm8
	paddd	%xmm6,%xmm12
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm8
	paddd	%xmm7,%xmm8
	leaq	256(%rbp),%rbp
	movdqa	144-128(%rax),%xmm6
	paddd	16-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	96-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm12,%xmm7

	movdqa	%xmm12,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm12,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,128-128(%rax)
	paddd	%xmm15,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-128(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm12,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm14,%xmm0
	pand	%xmm13,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm8,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm9,%xmm3
	movdqa	%xmm8,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm8,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm9,%xmm15
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm15
	paddd	%xmm5,%xmm11
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm15
	paddd	%xmm7,%xmm15
	movdqa	160-128(%rax),%xmm5
	paddd	32-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	112-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm11,%xmm7

	movdqa	%xmm11,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm11,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,144-128(%rax)
	paddd	%xmm14,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-96(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm11,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm13,%xmm0
	pand	%xmm12,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm15,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm8,%xmm4
	movdqa	%xmm15,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm15,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm8,%xmm14
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm14
	paddd	%xmm6,%xmm10
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm14
	paddd	%xmm7,%xmm14
	movdqa	176-128(%rax),%xmm6
	paddd	48-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	128-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm10,%xmm7

	movdqa	%xmm10,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm10,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,160-128(%rax)
	paddd	%xmm13,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm10,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm12,%xmm0
	pand	%xmm11,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm14,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm15,%xmm3
	movdqa	%xmm14,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm14,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm15,%xmm13
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm13
	paddd	%xmm5,%xmm9
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm13
	paddd	%xmm7,%xmm13
	movdqa	192-128(%rax),%xmm5
	paddd	64-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	144-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm9,%xmm7

	movdqa	%xmm9,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm9,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,176-128(%rax)
	paddd	%xmm12,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	-32(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm9,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm11,%xmm0
	pand	%xmm10,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm13,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm14,%xmm4
	movdqa	%xmm13,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm13,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm14,%xmm12
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm12
	paddd	%xmm6,%xmm8
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm12
	paddd	%xmm7,%xmm12
	movdqa	208-128(%rax),%xmm6
	paddd	80-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	160-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm8,%xmm7

	movdqa	%xmm8,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm8,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,192-128(%rax)
	paddd	%xmm11,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	0(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm8,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm8,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm10,%xmm0
	pand	%xmm9,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm12,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm12,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm13,%xmm3
	movdqa	%xmm12,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm12,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm13,%xmm11
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm11
	paddd	%xmm5,%xmm15
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm11
	paddd	%xmm7,%xmm11
	movdqa	224-128(%rax),%xmm5
	paddd	96-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	176-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm15,%xmm7

	movdqa	%xmm15,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm15,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,208-128(%rax)
	paddd	%xmm10,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	32(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm15,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm15,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm9,%xmm0
	pand	%xmm8,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm11,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm11,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm12,%xmm4
	movdqa	%xmm11,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm11,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm12,%xmm10
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm10
	paddd	%xmm6,%xmm14
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm10
	paddd	%xmm7,%xmm10
	movdqa	240-128(%rax),%xmm6
	paddd	112-128(%rax),%xmm5

	movdqa	%xmm6,%xmm7
	movdqa	%xmm6,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm6,%xmm2

	psrld	$7,%xmm1
	movdqa	192-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm3,%xmm1

	psrld	$17,%xmm3
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	psrld	$19-17,%xmm3
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm3,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm5
	movdqa	%xmm14,%xmm7

	movdqa	%xmm14,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm14,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm5,224-128(%rax)
	paddd	%xmm9,%xmm5

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	64(%rbp),%xmm5
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm14,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm14,%xmm3
	pslld	$26-21,%xmm2
	pandn	%xmm8,%xmm0
	pand	%xmm15,%xmm3
	pxor	%xmm1,%xmm7


	movdqa	%xmm10,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm10,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm5
	pxor	%xmm3,%xmm0
	movdqa	%xmm11,%xmm3
	movdqa	%xmm10,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm10,%xmm3


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm5
	pslld	$19-10,%xmm2
	pand	%xmm3,%xmm4
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm11,%xmm9
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm4,%xmm9
	paddd	%xmm5,%xmm13
	pxor	%xmm2,%xmm7

	paddd	%xmm5,%xmm9
	paddd	%xmm7,%xmm9
	movdqa	0-128(%rax),%xmm5
	paddd	128-128(%rax),%xmm6

	movdqa	%xmm5,%xmm7
	movdqa	%xmm5,%xmm1
	psrld	$3,%xmm7
	movdqa	%xmm5,%xmm2

	psrld	$7,%xmm1
	movdqa	208-128(%rax),%xmm0
	pslld	$14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$18-7,%xmm1
	movdqa	%xmm0,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$25-14,%xmm2
	pxor	%xmm1,%xmm7
	psrld	$10,%xmm0
	movdqa	%xmm4,%xmm1

	psrld	$17,%xmm4
	pxor	%xmm2,%xmm7
	pslld	$13,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	psrld	$19-17,%xmm4
	pxor	%xmm1,%xmm0
	pslld	$15-13,%xmm1
	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm0
	paddd	%xmm0,%xmm6
	movdqa	%xmm13,%xmm7

	movdqa	%xmm13,%xmm2

	psrld	$6,%xmm7
	movdqa	%xmm13,%xmm1
	pslld	$7,%xmm2
	movdqa	%xmm6,240-128(%rax)
	paddd	%xmm8,%xmm6

	psrld	$11,%xmm1
	pxor	%xmm2,%xmm7
	pslld	$21-7,%xmm2
	paddd	96(%rbp),%xmm6
	pxor	%xmm1,%xmm7

	psrld	$25-11,%xmm1
	movdqa	%xmm13,%xmm0

	pxor	%xmm2,%xmm7
	movdqa	%xmm13,%xmm4
	pslld	$26-21,%xmm2
	pandn	%xmm15,%xmm0
	pand	%xmm14,%xmm4
	pxor	%xmm1,%xmm7


	movdqa	%xmm9,%xmm1
	pxor	%xmm2,%xmm7
	movdqa	%xmm9,%xmm2
	psrld	$2,%xmm1
	paddd	%xmm7,%xmm6
	pxor	%xmm4,%xmm0
	movdqa	%xmm10,%xmm4
	movdqa	%xmm9,%xmm7
	pslld	$10,%xmm2
	pxor	%xmm9,%xmm4


	psrld	$13,%xmm7
	pxor	%xmm2,%xmm1
	paddd	%xmm0,%xmm6
	pslld	$19-10,%xmm2
	pand	%xmm4,%xmm3
	pxor	%xmm7,%xmm1


	psrld	$22-13,%xmm7
	pxor	%xmm2,%xmm1
	movdqa	%xmm10,%xmm8
	pslld	$30-19,%xmm2
	pxor	%xmm1,%xmm7
	pxor	%xmm3,%xmm8
	paddd	%xmm6,%xmm12
	pxor	%xmm2,%xmm7

	paddd	%xmm6,%xmm8
	paddd	%xmm7,%xmm8
	leaq	256(%rbp),%rbp
	decl	%ecx
	jnz	.Loop_16_xx

	movl	$1,%ecx
	leaq	K256+128(%rip),%rbp

	movdqa	(%rbx),%xmm7
	cmpl	0(%rbx),%ecx
	pxor	%xmm0,%xmm0
	cmovgeq	%rbp,%r8
	cmpl	4(%rbx),%ecx
	movdqa	%xmm7,%xmm6
	cmovgeq	%rbp,%r9
	cmpl	8(%rbx),%ecx
	pcmpgtd	%xmm0,%xmm6
	cmovgeq	%rbp,%r10
	cmpl	12(%rbx),%ecx
	paddd	%xmm6,%xmm7
	cmovgeq	%rbp,%r11

	movdqu	0-128(%rdi),%xmm0
	pand	%xmm6,%xmm8
	movdqu	32-128(%rdi),%xmm1
	pand	%xmm6,%xmm9
	movdqu	64-128(%rdi),%xmm2
	pand	%xmm6,%xmm10
	movdqu	96-128(%rdi),%xmm5
	pand	%xmm6,%xmm11
	paddd	%xmm0,%xmm8
	movdqu	128-128(%rdi),%xmm0
	pand	%xmm6,%xmm12
	paddd	%xmm1,%xmm9
	movdqu	160-128(%rdi),%xmm1
	pand	%xmm6,%xmm13
	paddd	%xmm2,%xmm10
	movdqu	192-128(%rdi),%xmm2
	pand	%xmm6,%xmm14
	paddd	%xmm5,%xmm11
	movdqu	224-128(%rdi),%xmm5
	pand	%xmm6,%xmm15
	paddd	%xmm0,%xmm12
	paddd	%xmm1,%xmm13
	movdqu	%xmm8,0-128(%rdi)
	paddd	%xmm2,%xmm14
	movdqu	%xmm9,32-128(%rdi)
	paddd	%xmm5,%xmm15
	movdqu	%xmm10,64-128(%rdi)
	movdqu	%xmm11,96-128(%rdi)
	movdqu	%xmm12,128-128(%rdi)
	movdqu	%xmm13,160-128(%rdi)
	movdqu	%xmm14,192-128(%rdi)
	movdqu	%xmm15,224-128(%rdi)

	movdqa	%xmm7,(%rbx)
	movdqa	.Lpbswap(%rip),%xmm6
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
.size	sha256_multi_block,.-sha256_multi_block
.type	sha256_multi_block_shaext,@function
.align	32
sha256_multi_block_shaext:
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
	leaq	128(%rdi),%rdi
	movq	%rax,272(%rsp)
.Lbody_shaext:
	leaq	256(%rsp),%rbx
	leaq	K256_shaext+128(%rip),%rbp

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

	movq	0-128(%rdi),%xmm12
	movq	32-128(%rdi),%xmm4
	movq	64-128(%rdi),%xmm13
	movq	96-128(%rdi),%xmm5
	movq	128-128(%rdi),%xmm8
	movq	160-128(%rdi),%xmm9
	movq	192-128(%rdi),%xmm10
	movq	224-128(%rdi),%xmm11

	punpckldq	%xmm4,%xmm12
	punpckldq	%xmm5,%xmm13
	punpckldq	%xmm9,%xmm8
	punpckldq	%xmm11,%xmm10
	movdqa	K256_shaext-16(%rip),%xmm3

	movdqa	%xmm12,%xmm14
	movdqa	%xmm13,%xmm15
	punpcklqdq	%xmm8,%xmm12
	punpcklqdq	%xmm10,%xmm13
	punpckhqdq	%xmm8,%xmm14
	punpckhqdq	%xmm10,%xmm15

	pshufd	$27,%xmm12,%xmm12
	pshufd	$27,%xmm13,%xmm13
	pshufd	$27,%xmm14,%xmm14
	pshufd	$27,%xmm15,%xmm15
	jmp	.Loop_shaext

.align	32
.Loop_shaext:
	movdqu	0(%r8),%xmm4
	movdqu	0(%r9),%xmm8
	movdqu	16(%r8),%xmm5
	movdqu	16(%r9),%xmm9
	movdqu	32(%r8),%xmm6
.byte	102,15,56,0,227
	movdqu	32(%r9),%xmm10
.byte	102,68,15,56,0,195
	movdqu	48(%r8),%xmm7
	leaq	64(%r8),%r8
	movdqu	48(%r9),%xmm11
	leaq	64(%r9),%r9

	movdqa	0-128(%rbp),%xmm0
.byte	102,15,56,0,235
	paddd	%xmm4,%xmm0
	pxor	%xmm12,%xmm4
	movdqa	%xmm0,%xmm1
	movdqa	0-128(%rbp),%xmm2
.byte	102,68,15,56,0,203
	paddd	%xmm8,%xmm2
	movdqa	%xmm13,80(%rsp)
.byte	69,15,56,203,236
	pxor	%xmm14,%xmm8
	movdqa	%xmm2,%xmm0
	movdqa	%xmm15,112(%rsp)
.byte	69,15,56,203,254
	pshufd	$0x0e,%xmm1,%xmm0
	pxor	%xmm12,%xmm4
	movdqa	%xmm12,64(%rsp)
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	pxor	%xmm14,%xmm8
	movdqa	%xmm14,96(%rsp)
	movdqa	16-128(%rbp),%xmm1
	paddd	%xmm5,%xmm1
.byte	102,15,56,0,243
.byte	69,15,56,203,247

	movdqa	%xmm1,%xmm0
	movdqa	16-128(%rbp),%xmm2
	paddd	%xmm9,%xmm2
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	prefetcht0	127(%r8)
.byte	102,15,56,0,251
.byte	102,68,15,56,0,211
	prefetcht0	127(%r9)
.byte	69,15,56,203,254
	pshufd	$0x0e,%xmm1,%xmm0
.byte	102,68,15,56,0,219
.byte	15,56,204,229
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	32-128(%rbp),%xmm1
	paddd	%xmm6,%xmm1
.byte	69,15,56,203,247

	movdqa	%xmm1,%xmm0
	movdqa	32-128(%rbp),%xmm2
	paddd	%xmm10,%xmm2
.byte	69,15,56,203,236
.byte	69,15,56,204,193
	movdqa	%xmm2,%xmm0
	movdqa	%xmm7,%xmm3
.byte	69,15,56,203,254
	pshufd	$0x0e,%xmm1,%xmm0
.byte	102,15,58,15,222,4
	paddd	%xmm3,%xmm4
	movdqa	%xmm11,%xmm3
.byte	102,65,15,58,15,218,4
.byte	15,56,204,238
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	48-128(%rbp),%xmm1
	paddd	%xmm7,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,202

	movdqa	%xmm1,%xmm0
	movdqa	48-128(%rbp),%xmm2
	paddd	%xmm3,%xmm8
	paddd	%xmm11,%xmm2
.byte	15,56,205,231
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm4,%xmm3
.byte	102,15,58,15,223,4
.byte	69,15,56,203,254
.byte	69,15,56,205,195
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm5
	movdqa	%xmm8,%xmm3
.byte	102,65,15,58,15,219,4
.byte	15,56,204,247
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	64-128(%rbp),%xmm1
	paddd	%xmm4,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,211
	movdqa	%xmm1,%xmm0
	movdqa	64-128(%rbp),%xmm2
	paddd	%xmm3,%xmm9
	paddd	%xmm8,%xmm2
.byte	15,56,205,236
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm5,%xmm3
.byte	102,15,58,15,220,4
.byte	69,15,56,203,254
.byte	69,15,56,205,200
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm6
	movdqa	%xmm9,%xmm3
.byte	102,65,15,58,15,216,4
.byte	15,56,204,252
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	80-128(%rbp),%xmm1
	paddd	%xmm5,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,216
	movdqa	%xmm1,%xmm0
	movdqa	80-128(%rbp),%xmm2
	paddd	%xmm3,%xmm10
	paddd	%xmm9,%xmm2
.byte	15,56,205,245
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm6,%xmm3
.byte	102,15,58,15,221,4
.byte	69,15,56,203,254
.byte	69,15,56,205,209
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm7
	movdqa	%xmm10,%xmm3
.byte	102,65,15,58,15,217,4
.byte	15,56,204,229
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	96-128(%rbp),%xmm1
	paddd	%xmm6,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,193
	movdqa	%xmm1,%xmm0
	movdqa	96-128(%rbp),%xmm2
	paddd	%xmm3,%xmm11
	paddd	%xmm10,%xmm2
.byte	15,56,205,254
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm7,%xmm3
.byte	102,15,58,15,222,4
.byte	69,15,56,203,254
.byte	69,15,56,205,218
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm4
	movdqa	%xmm11,%xmm3
.byte	102,65,15,58,15,218,4
.byte	15,56,204,238
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	112-128(%rbp),%xmm1
	paddd	%xmm7,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,202
	movdqa	%xmm1,%xmm0
	movdqa	112-128(%rbp),%xmm2
	paddd	%xmm3,%xmm8
	paddd	%xmm11,%xmm2
.byte	15,56,205,231
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm4,%xmm3
.byte	102,15,58,15,223,4
.byte	69,15,56,203,254
.byte	69,15,56,205,195
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm5
	movdqa	%xmm8,%xmm3
.byte	102,65,15,58,15,219,4
.byte	15,56,204,247
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	128-128(%rbp),%xmm1
	paddd	%xmm4,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,211
	movdqa	%xmm1,%xmm0
	movdqa	128-128(%rbp),%xmm2
	paddd	%xmm3,%xmm9
	paddd	%xmm8,%xmm2
.byte	15,56,205,236
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm5,%xmm3
.byte	102,15,58,15,220,4
.byte	69,15,56,203,254
.byte	69,15,56,205,200
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm6
	movdqa	%xmm9,%xmm3
.byte	102,65,15,58,15,216,4
.byte	15,56,204,252
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	144-128(%rbp),%xmm1
	paddd	%xmm5,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,216
	movdqa	%xmm1,%xmm0
	movdqa	144-128(%rbp),%xmm2
	paddd	%xmm3,%xmm10
	paddd	%xmm9,%xmm2
.byte	15,56,205,245
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm6,%xmm3
.byte	102,15,58,15,221,4
.byte	69,15,56,203,254
.byte	69,15,56,205,209
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm7
	movdqa	%xmm10,%xmm3
.byte	102,65,15,58,15,217,4
.byte	15,56,204,229
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	160-128(%rbp),%xmm1
	paddd	%xmm6,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,193
	movdqa	%xmm1,%xmm0
	movdqa	160-128(%rbp),%xmm2
	paddd	%xmm3,%xmm11
	paddd	%xmm10,%xmm2
.byte	15,56,205,254
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm7,%xmm3
.byte	102,15,58,15,222,4
.byte	69,15,56,203,254
.byte	69,15,56,205,218
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm4
	movdqa	%xmm11,%xmm3
.byte	102,65,15,58,15,218,4
.byte	15,56,204,238
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	176-128(%rbp),%xmm1
	paddd	%xmm7,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,202
	movdqa	%xmm1,%xmm0
	movdqa	176-128(%rbp),%xmm2
	paddd	%xmm3,%xmm8
	paddd	%xmm11,%xmm2
.byte	15,56,205,231
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm4,%xmm3
.byte	102,15,58,15,223,4
.byte	69,15,56,203,254
.byte	69,15,56,205,195
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm5
	movdqa	%xmm8,%xmm3
.byte	102,65,15,58,15,219,4
.byte	15,56,204,247
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	192-128(%rbp),%xmm1
	paddd	%xmm4,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,211
	movdqa	%xmm1,%xmm0
	movdqa	192-128(%rbp),%xmm2
	paddd	%xmm3,%xmm9
	paddd	%xmm8,%xmm2
.byte	15,56,205,236
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm5,%xmm3
.byte	102,15,58,15,220,4
.byte	69,15,56,203,254
.byte	69,15,56,205,200
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm6
	movdqa	%xmm9,%xmm3
.byte	102,65,15,58,15,216,4
.byte	15,56,204,252
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	208-128(%rbp),%xmm1
	paddd	%xmm5,%xmm1
.byte	69,15,56,203,247
.byte	69,15,56,204,216
	movdqa	%xmm1,%xmm0
	movdqa	208-128(%rbp),%xmm2
	paddd	%xmm3,%xmm10
	paddd	%xmm9,%xmm2
.byte	15,56,205,245
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movdqa	%xmm6,%xmm3
.byte	102,15,58,15,221,4
.byte	69,15,56,203,254
.byte	69,15,56,205,209
	pshufd	$0x0e,%xmm1,%xmm0
	paddd	%xmm3,%xmm7
	movdqa	%xmm10,%xmm3
.byte	102,65,15,58,15,217,4
	nop
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	224-128(%rbp),%xmm1
	paddd	%xmm6,%xmm1
.byte	69,15,56,203,247

	movdqa	%xmm1,%xmm0
	movdqa	224-128(%rbp),%xmm2
	paddd	%xmm3,%xmm11
	paddd	%xmm10,%xmm2
.byte	15,56,205,254
	nop
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	movl	$1,%ecx
	pxor	%xmm6,%xmm6
.byte	69,15,56,203,254
.byte	69,15,56,205,218
	pshufd	$0x0e,%xmm1,%xmm0
	movdqa	240-128(%rbp),%xmm1
	paddd	%xmm7,%xmm1
	movq	(%rbx),%xmm7
	nop
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	movdqa	240-128(%rbp),%xmm2
	paddd	%xmm11,%xmm2
.byte	69,15,56,203,247

	movdqa	%xmm1,%xmm0
	cmpl	0(%rbx),%ecx
	cmovgeq	%rsp,%r8
	cmpl	4(%rbx),%ecx
	cmovgeq	%rsp,%r9
	pshufd	$0x00,%xmm7,%xmm9
.byte	69,15,56,203,236
	movdqa	%xmm2,%xmm0
	pshufd	$0x55,%xmm7,%xmm10
	movdqa	%xmm7,%xmm11
.byte	69,15,56,203,254
	pshufd	$0x0e,%xmm1,%xmm0
	pcmpgtd	%xmm6,%xmm9
	pcmpgtd	%xmm6,%xmm10
.byte	69,15,56,203,229
	pshufd	$0x0e,%xmm2,%xmm0
	pcmpgtd	%xmm6,%xmm11
	movdqa	K256_shaext-16(%rip),%xmm3
.byte	69,15,56,203,247

	pand	%xmm9,%xmm13
	pand	%xmm10,%xmm15
	pand	%xmm9,%xmm12
	pand	%xmm10,%xmm14
	paddd	%xmm7,%xmm11

	paddd	80(%rsp),%xmm13
	paddd	112(%rsp),%xmm15
	paddd	64(%rsp),%xmm12
	paddd	96(%rsp),%xmm14

	movq	%xmm11,(%rbx)
	decl	%edx
	jnz	.Loop_shaext

	movl	280(%rsp),%edx

	pshufd	$27,%xmm12,%xmm12
	pshufd	$27,%xmm13,%xmm13
	pshufd	$27,%xmm14,%xmm14
	pshufd	$27,%xmm15,%xmm15

	movdqa	%xmm12,%xmm5
	movdqa	%xmm13,%xmm6
	punpckldq	%xmm14,%xmm12
	punpckhdq	%xmm14,%xmm5
	punpckldq	%xmm15,%xmm13
	punpckhdq	%xmm15,%xmm6

	movq	%xmm12,0-128(%rdi)
	psrldq	$8,%xmm12
	movq	%xmm5,128-128(%rdi)
	psrldq	$8,%xmm5
	movq	%xmm12,32-128(%rdi)
	movq	%xmm5,160-128(%rdi)

	movq	%xmm13,64-128(%rdi)
	psrldq	$8,%xmm13
	movq	%xmm6,192-128(%rdi)
	psrldq	$8,%xmm6
	movq	%xmm13,96-128(%rdi)
	movq	%xmm6,224-128(%rdi)

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
.size	sha256_multi_block_shaext,.-sha256_multi_block_shaext
.type	sha256_multi_block_avx,@function
.align	32
sha256_multi_block_avx:
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
	leaq	K256+128(%rip),%rbp
	leaq	256(%rsp),%rbx
	leaq	128(%rdi),%rdi

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

	vmovdqu	0-128(%rdi),%xmm8
	leaq	128(%rsp),%rax
	vmovdqu	32-128(%rdi),%xmm9
	vmovdqu	64-128(%rdi),%xmm10
	vmovdqu	96-128(%rdi),%xmm11
	vmovdqu	128-128(%rdi),%xmm12
	vmovdqu	160-128(%rdi),%xmm13
	vmovdqu	192-128(%rdi),%xmm14
	vmovdqu	224-128(%rdi),%xmm15
	vmovdqu	.Lpbswap(%rip),%xmm6
	jmp	.Loop_avx

.align	32
.Loop_avx:
	vpxor	%xmm9,%xmm10,%xmm4
	vmovd	0(%r8),%xmm5
	vmovd	0(%r9),%xmm0
	vpinsrd	$1,0(%r10),%xmm5,%xmm5
	vpinsrd	$1,0(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm12,%xmm7
	vpslld	$26,%xmm12,%xmm2
	vmovdqu	%xmm5,0-128(%rax)
	vpaddd	%xmm15,%xmm5,%xmm5

	vpsrld	$11,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm12,%xmm2
	vpaddd	-128(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm12,%xmm2
	vpandn	%xmm14,%xmm12,%xmm0
	vpand	%xmm13,%xmm12,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm8,%xmm15
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm8,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm8,%xmm9,%xmm3

	vpxor	%xmm1,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm8,%xmm1

	vpslld	$19,%xmm8,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm15,%xmm7

	vpsrld	$22,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm8,%xmm2
	vpxor	%xmm4,%xmm9,%xmm15
	vpaddd	%xmm5,%xmm11,%xmm11

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm15,%xmm15
	vmovd	4(%r8),%xmm5
	vmovd	4(%r9),%xmm0
	vpinsrd	$1,4(%r10),%xmm5,%xmm5
	vpinsrd	$1,4(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm11,%xmm7
	vpslld	$26,%xmm11,%xmm2
	vmovdqu	%xmm5,16-128(%rax)
	vpaddd	%xmm14,%xmm5,%xmm5

	vpsrld	$11,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm11,%xmm2
	vpaddd	-96(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm11,%xmm2
	vpandn	%xmm13,%xmm11,%xmm0
	vpand	%xmm12,%xmm11,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm15,%xmm14
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm15,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm15,%xmm8,%xmm4

	vpxor	%xmm1,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm15,%xmm1

	vpslld	$19,%xmm15,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm14,%xmm7

	vpsrld	$22,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm15,%xmm2
	vpxor	%xmm3,%xmm8,%xmm14
	vpaddd	%xmm5,%xmm10,%xmm10

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm14,%xmm14
	vmovd	8(%r8),%xmm5
	vmovd	8(%r9),%xmm0
	vpinsrd	$1,8(%r10),%xmm5,%xmm5
	vpinsrd	$1,8(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm10,%xmm7
	vpslld	$26,%xmm10,%xmm2
	vmovdqu	%xmm5,32-128(%rax)
	vpaddd	%xmm13,%xmm5,%xmm5

	vpsrld	$11,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm10,%xmm2
	vpaddd	-64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm10,%xmm2
	vpandn	%xmm12,%xmm10,%xmm0
	vpand	%xmm11,%xmm10,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm14,%xmm13
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm14,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm14,%xmm15,%xmm3

	vpxor	%xmm1,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm14,%xmm1

	vpslld	$19,%xmm14,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm13,%xmm7

	vpsrld	$22,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm14,%xmm2
	vpxor	%xmm4,%xmm15,%xmm13
	vpaddd	%xmm5,%xmm9,%xmm9

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm13,%xmm13
	vmovd	12(%r8),%xmm5
	vmovd	12(%r9),%xmm0
	vpinsrd	$1,12(%r10),%xmm5,%xmm5
	vpinsrd	$1,12(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm9,%xmm7
	vpslld	$26,%xmm9,%xmm2
	vmovdqu	%xmm5,48-128(%rax)
	vpaddd	%xmm12,%xmm5,%xmm5

	vpsrld	$11,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm9,%xmm2
	vpaddd	-32(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm9,%xmm2
	vpandn	%xmm11,%xmm9,%xmm0
	vpand	%xmm10,%xmm9,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm13,%xmm12
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm13,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm13,%xmm14,%xmm4

	vpxor	%xmm1,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm13,%xmm1

	vpslld	$19,%xmm13,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm12,%xmm7

	vpsrld	$22,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm13,%xmm2
	vpxor	%xmm3,%xmm14,%xmm12
	vpaddd	%xmm5,%xmm8,%xmm8

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm12,%xmm12
	vmovd	16(%r8),%xmm5
	vmovd	16(%r9),%xmm0
	vpinsrd	$1,16(%r10),%xmm5,%xmm5
	vpinsrd	$1,16(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm8,%xmm7
	vpslld	$26,%xmm8,%xmm2
	vmovdqu	%xmm5,64-128(%rax)
	vpaddd	%xmm11,%xmm5,%xmm5

	vpsrld	$11,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm8,%xmm2
	vpaddd	0(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm8,%xmm2
	vpandn	%xmm10,%xmm8,%xmm0
	vpand	%xmm9,%xmm8,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm12,%xmm11
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm12,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm12,%xmm13,%xmm3

	vpxor	%xmm1,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm12,%xmm1

	vpslld	$19,%xmm12,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm11,%xmm7

	vpsrld	$22,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm12,%xmm2
	vpxor	%xmm4,%xmm13,%xmm11
	vpaddd	%xmm5,%xmm15,%xmm15

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm11,%xmm11
	vmovd	20(%r8),%xmm5
	vmovd	20(%r9),%xmm0
	vpinsrd	$1,20(%r10),%xmm5,%xmm5
	vpinsrd	$1,20(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm15,%xmm7
	vpslld	$26,%xmm15,%xmm2
	vmovdqu	%xmm5,80-128(%rax)
	vpaddd	%xmm10,%xmm5,%xmm5

	vpsrld	$11,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm15,%xmm2
	vpaddd	32(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm15,%xmm2
	vpandn	%xmm9,%xmm15,%xmm0
	vpand	%xmm8,%xmm15,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm11,%xmm10
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm11,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm11,%xmm12,%xmm4

	vpxor	%xmm1,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm11,%xmm1

	vpslld	$19,%xmm11,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm10,%xmm7

	vpsrld	$22,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm11,%xmm2
	vpxor	%xmm3,%xmm12,%xmm10
	vpaddd	%xmm5,%xmm14,%xmm14

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm10,%xmm10
	vmovd	24(%r8),%xmm5
	vmovd	24(%r9),%xmm0
	vpinsrd	$1,24(%r10),%xmm5,%xmm5
	vpinsrd	$1,24(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm14,%xmm7
	vpslld	$26,%xmm14,%xmm2
	vmovdqu	%xmm5,96-128(%rax)
	vpaddd	%xmm9,%xmm5,%xmm5

	vpsrld	$11,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm14,%xmm2
	vpaddd	64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm14,%xmm2
	vpandn	%xmm8,%xmm14,%xmm0
	vpand	%xmm15,%xmm14,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm10,%xmm9
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm10,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm10,%xmm11,%xmm3

	vpxor	%xmm1,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm10,%xmm1

	vpslld	$19,%xmm10,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm9,%xmm7

	vpsrld	$22,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm10,%xmm2
	vpxor	%xmm4,%xmm11,%xmm9
	vpaddd	%xmm5,%xmm13,%xmm13

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm9,%xmm9
	vmovd	28(%r8),%xmm5
	vmovd	28(%r9),%xmm0
	vpinsrd	$1,28(%r10),%xmm5,%xmm5
	vpinsrd	$1,28(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm13,%xmm7
	vpslld	$26,%xmm13,%xmm2
	vmovdqu	%xmm5,112-128(%rax)
	vpaddd	%xmm8,%xmm5,%xmm5

	vpsrld	$11,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm13,%xmm2
	vpaddd	96(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm13,%xmm2
	vpandn	%xmm15,%xmm13,%xmm0
	vpand	%xmm14,%xmm13,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm9,%xmm8
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm9,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm9,%xmm10,%xmm4

	vpxor	%xmm1,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm9,%xmm1

	vpslld	$19,%xmm9,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm8,%xmm7

	vpsrld	$22,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm9,%xmm2
	vpxor	%xmm3,%xmm10,%xmm8
	vpaddd	%xmm5,%xmm12,%xmm12

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm8,%xmm8
	addq	$256,%rbp
	vmovd	32(%r8),%xmm5
	vmovd	32(%r9),%xmm0
	vpinsrd	$1,32(%r10),%xmm5,%xmm5
	vpinsrd	$1,32(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm12,%xmm7
	vpslld	$26,%xmm12,%xmm2
	vmovdqu	%xmm5,128-128(%rax)
	vpaddd	%xmm15,%xmm5,%xmm5

	vpsrld	$11,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm12,%xmm2
	vpaddd	-128(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm12,%xmm2
	vpandn	%xmm14,%xmm12,%xmm0
	vpand	%xmm13,%xmm12,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm8,%xmm15
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm8,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm8,%xmm9,%xmm3

	vpxor	%xmm1,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm8,%xmm1

	vpslld	$19,%xmm8,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm15,%xmm7

	vpsrld	$22,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm8,%xmm2
	vpxor	%xmm4,%xmm9,%xmm15
	vpaddd	%xmm5,%xmm11,%xmm11

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm15,%xmm15
	vmovd	36(%r8),%xmm5
	vmovd	36(%r9),%xmm0
	vpinsrd	$1,36(%r10),%xmm5,%xmm5
	vpinsrd	$1,36(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm11,%xmm7
	vpslld	$26,%xmm11,%xmm2
	vmovdqu	%xmm5,144-128(%rax)
	vpaddd	%xmm14,%xmm5,%xmm5

	vpsrld	$11,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm11,%xmm2
	vpaddd	-96(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm11,%xmm2
	vpandn	%xmm13,%xmm11,%xmm0
	vpand	%xmm12,%xmm11,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm15,%xmm14
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm15,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm15,%xmm8,%xmm4

	vpxor	%xmm1,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm15,%xmm1

	vpslld	$19,%xmm15,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm14,%xmm7

	vpsrld	$22,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm15,%xmm2
	vpxor	%xmm3,%xmm8,%xmm14
	vpaddd	%xmm5,%xmm10,%xmm10

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm14,%xmm14
	vmovd	40(%r8),%xmm5
	vmovd	40(%r9),%xmm0
	vpinsrd	$1,40(%r10),%xmm5,%xmm5
	vpinsrd	$1,40(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm10,%xmm7
	vpslld	$26,%xmm10,%xmm2
	vmovdqu	%xmm5,160-128(%rax)
	vpaddd	%xmm13,%xmm5,%xmm5

	vpsrld	$11,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm10,%xmm2
	vpaddd	-64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm10,%xmm2
	vpandn	%xmm12,%xmm10,%xmm0
	vpand	%xmm11,%xmm10,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm14,%xmm13
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm14,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm14,%xmm15,%xmm3

	vpxor	%xmm1,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm14,%xmm1

	vpslld	$19,%xmm14,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm13,%xmm7

	vpsrld	$22,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm14,%xmm2
	vpxor	%xmm4,%xmm15,%xmm13
	vpaddd	%xmm5,%xmm9,%xmm9

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm13,%xmm13
	vmovd	44(%r8),%xmm5
	vmovd	44(%r9),%xmm0
	vpinsrd	$1,44(%r10),%xmm5,%xmm5
	vpinsrd	$1,44(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm9,%xmm7
	vpslld	$26,%xmm9,%xmm2
	vmovdqu	%xmm5,176-128(%rax)
	vpaddd	%xmm12,%xmm5,%xmm5

	vpsrld	$11,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm9,%xmm2
	vpaddd	-32(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm9,%xmm2
	vpandn	%xmm11,%xmm9,%xmm0
	vpand	%xmm10,%xmm9,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm13,%xmm12
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm13,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm13,%xmm14,%xmm4

	vpxor	%xmm1,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm13,%xmm1

	vpslld	$19,%xmm13,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm12,%xmm7

	vpsrld	$22,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm13,%xmm2
	vpxor	%xmm3,%xmm14,%xmm12
	vpaddd	%xmm5,%xmm8,%xmm8

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm12,%xmm12
	vmovd	48(%r8),%xmm5
	vmovd	48(%r9),%xmm0
	vpinsrd	$1,48(%r10),%xmm5,%xmm5
	vpinsrd	$1,48(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm8,%xmm7
	vpslld	$26,%xmm8,%xmm2
	vmovdqu	%xmm5,192-128(%rax)
	vpaddd	%xmm11,%xmm5,%xmm5

	vpsrld	$11,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm8,%xmm2
	vpaddd	0(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm8,%xmm2
	vpandn	%xmm10,%xmm8,%xmm0
	vpand	%xmm9,%xmm8,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm12,%xmm11
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm12,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm12,%xmm13,%xmm3

	vpxor	%xmm1,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm12,%xmm1

	vpslld	$19,%xmm12,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm11,%xmm7

	vpsrld	$22,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm12,%xmm2
	vpxor	%xmm4,%xmm13,%xmm11
	vpaddd	%xmm5,%xmm15,%xmm15

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm11,%xmm11
	vmovd	52(%r8),%xmm5
	vmovd	52(%r9),%xmm0
	vpinsrd	$1,52(%r10),%xmm5,%xmm5
	vpinsrd	$1,52(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm15,%xmm7
	vpslld	$26,%xmm15,%xmm2
	vmovdqu	%xmm5,208-128(%rax)
	vpaddd	%xmm10,%xmm5,%xmm5

	vpsrld	$11,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm15,%xmm2
	vpaddd	32(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm15,%xmm2
	vpandn	%xmm9,%xmm15,%xmm0
	vpand	%xmm8,%xmm15,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm11,%xmm10
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm11,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm11,%xmm12,%xmm4

	vpxor	%xmm1,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm11,%xmm1

	vpslld	$19,%xmm11,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm10,%xmm7

	vpsrld	$22,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm11,%xmm2
	vpxor	%xmm3,%xmm12,%xmm10
	vpaddd	%xmm5,%xmm14,%xmm14

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm10,%xmm10
	vmovd	56(%r8),%xmm5
	vmovd	56(%r9),%xmm0
	vpinsrd	$1,56(%r10),%xmm5,%xmm5
	vpinsrd	$1,56(%r11),%xmm0,%xmm0
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm14,%xmm7
	vpslld	$26,%xmm14,%xmm2
	vmovdqu	%xmm5,224-128(%rax)
	vpaddd	%xmm9,%xmm5,%xmm5

	vpsrld	$11,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm14,%xmm2
	vpaddd	64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm14,%xmm2
	vpandn	%xmm8,%xmm14,%xmm0
	vpand	%xmm15,%xmm14,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm10,%xmm9
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm10,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm10,%xmm11,%xmm3

	vpxor	%xmm1,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm10,%xmm1

	vpslld	$19,%xmm10,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm9,%xmm7

	vpsrld	$22,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm10,%xmm2
	vpxor	%xmm4,%xmm11,%xmm9
	vpaddd	%xmm5,%xmm13,%xmm13

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm9,%xmm9
	vmovd	60(%r8),%xmm5
	leaq	64(%r8),%r8
	vmovd	60(%r9),%xmm0
	leaq	64(%r9),%r9
	vpinsrd	$1,60(%r10),%xmm5,%xmm5
	leaq	64(%r10),%r10
	vpinsrd	$1,60(%r11),%xmm0,%xmm0
	leaq	64(%r11),%r11
	vpunpckldq	%xmm0,%xmm5,%xmm5
	vpshufb	%xmm6,%xmm5,%xmm5
	vpsrld	$6,%xmm13,%xmm7
	vpslld	$26,%xmm13,%xmm2
	vmovdqu	%xmm5,240-128(%rax)
	vpaddd	%xmm8,%xmm5,%xmm5

	vpsrld	$11,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm13,%xmm2
	vpaddd	96(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	prefetcht0	63(%r8)
	vpslld	$7,%xmm13,%xmm2
	vpandn	%xmm15,%xmm13,%xmm0
	vpand	%xmm14,%xmm13,%xmm4
	prefetcht0	63(%r9)
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm9,%xmm8
	vpxor	%xmm2,%xmm7,%xmm7
	prefetcht0	63(%r10)
	vpslld	$30,%xmm9,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm9,%xmm10,%xmm4
	prefetcht0	63(%r11)
	vpxor	%xmm1,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm9,%xmm1

	vpslld	$19,%xmm9,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm8,%xmm7

	vpsrld	$22,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm9,%xmm2
	vpxor	%xmm3,%xmm10,%xmm8
	vpaddd	%xmm5,%xmm12,%xmm12

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm8,%xmm8
	addq	$256,%rbp
	vmovdqu	0-128(%rax),%xmm5
	movl	$3,%ecx
	jmp	.Loop_16_xx_avx
.align	32
.Loop_16_xx_avx:
	vmovdqu	16-128(%rax),%xmm6
	vpaddd	144-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	224-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm12,%xmm7
	vpslld	$26,%xmm12,%xmm2
	vmovdqu	%xmm5,0-128(%rax)
	vpaddd	%xmm15,%xmm5,%xmm5

	vpsrld	$11,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm12,%xmm2
	vpaddd	-128(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm12,%xmm2
	vpandn	%xmm14,%xmm12,%xmm0
	vpand	%xmm13,%xmm12,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm8,%xmm15
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm8,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm8,%xmm9,%xmm3

	vpxor	%xmm1,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm8,%xmm1

	vpslld	$19,%xmm8,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm15,%xmm7

	vpsrld	$22,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm8,%xmm2
	vpxor	%xmm4,%xmm9,%xmm15
	vpaddd	%xmm5,%xmm11,%xmm11

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm15,%xmm15
	vmovdqu	32-128(%rax),%xmm5
	vpaddd	160-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	240-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm11,%xmm7
	vpslld	$26,%xmm11,%xmm2
	vmovdqu	%xmm6,16-128(%rax)
	vpaddd	%xmm14,%xmm6,%xmm6

	vpsrld	$11,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm11,%xmm2
	vpaddd	-96(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm11,%xmm2
	vpandn	%xmm13,%xmm11,%xmm0
	vpand	%xmm12,%xmm11,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm15,%xmm14
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm15,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm15,%xmm8,%xmm4

	vpxor	%xmm1,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm15,%xmm1

	vpslld	$19,%xmm15,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm14,%xmm7

	vpsrld	$22,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm15,%xmm2
	vpxor	%xmm3,%xmm8,%xmm14
	vpaddd	%xmm6,%xmm10,%xmm10

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm14,%xmm14
	vmovdqu	48-128(%rax),%xmm6
	vpaddd	176-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	0-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm10,%xmm7
	vpslld	$26,%xmm10,%xmm2
	vmovdqu	%xmm5,32-128(%rax)
	vpaddd	%xmm13,%xmm5,%xmm5

	vpsrld	$11,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm10,%xmm2
	vpaddd	-64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm10,%xmm2
	vpandn	%xmm12,%xmm10,%xmm0
	vpand	%xmm11,%xmm10,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm14,%xmm13
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm14,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm14,%xmm15,%xmm3

	vpxor	%xmm1,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm14,%xmm1

	vpslld	$19,%xmm14,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm13,%xmm7

	vpsrld	$22,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm14,%xmm2
	vpxor	%xmm4,%xmm15,%xmm13
	vpaddd	%xmm5,%xmm9,%xmm9

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm13,%xmm13
	vmovdqu	64-128(%rax),%xmm5
	vpaddd	192-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	16-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm9,%xmm7
	vpslld	$26,%xmm9,%xmm2
	vmovdqu	%xmm6,48-128(%rax)
	vpaddd	%xmm12,%xmm6,%xmm6

	vpsrld	$11,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm9,%xmm2
	vpaddd	-32(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm9,%xmm2
	vpandn	%xmm11,%xmm9,%xmm0
	vpand	%xmm10,%xmm9,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm13,%xmm12
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm13,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm13,%xmm14,%xmm4

	vpxor	%xmm1,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm13,%xmm1

	vpslld	$19,%xmm13,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm12,%xmm7

	vpsrld	$22,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm13,%xmm2
	vpxor	%xmm3,%xmm14,%xmm12
	vpaddd	%xmm6,%xmm8,%xmm8

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm12,%xmm12
	vmovdqu	80-128(%rax),%xmm6
	vpaddd	208-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	32-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm8,%xmm7
	vpslld	$26,%xmm8,%xmm2
	vmovdqu	%xmm5,64-128(%rax)
	vpaddd	%xmm11,%xmm5,%xmm5

	vpsrld	$11,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm8,%xmm2
	vpaddd	0(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm8,%xmm2
	vpandn	%xmm10,%xmm8,%xmm0
	vpand	%xmm9,%xmm8,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm12,%xmm11
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm12,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm12,%xmm13,%xmm3

	vpxor	%xmm1,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm12,%xmm1

	vpslld	$19,%xmm12,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm11,%xmm7

	vpsrld	$22,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm12,%xmm2
	vpxor	%xmm4,%xmm13,%xmm11
	vpaddd	%xmm5,%xmm15,%xmm15

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm11,%xmm11
	vmovdqu	96-128(%rax),%xmm5
	vpaddd	224-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	48-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm15,%xmm7
	vpslld	$26,%xmm15,%xmm2
	vmovdqu	%xmm6,80-128(%rax)
	vpaddd	%xmm10,%xmm6,%xmm6

	vpsrld	$11,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm15,%xmm2
	vpaddd	32(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm15,%xmm2
	vpandn	%xmm9,%xmm15,%xmm0
	vpand	%xmm8,%xmm15,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm11,%xmm10
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm11,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm11,%xmm12,%xmm4

	vpxor	%xmm1,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm11,%xmm1

	vpslld	$19,%xmm11,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm10,%xmm7

	vpsrld	$22,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm11,%xmm2
	vpxor	%xmm3,%xmm12,%xmm10
	vpaddd	%xmm6,%xmm14,%xmm14

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm10,%xmm10
	vmovdqu	112-128(%rax),%xmm6
	vpaddd	240-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	64-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm14,%xmm7
	vpslld	$26,%xmm14,%xmm2
	vmovdqu	%xmm5,96-128(%rax)
	vpaddd	%xmm9,%xmm5,%xmm5

	vpsrld	$11,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm14,%xmm2
	vpaddd	64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm14,%xmm2
	vpandn	%xmm8,%xmm14,%xmm0
	vpand	%xmm15,%xmm14,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm10,%xmm9
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm10,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm10,%xmm11,%xmm3

	vpxor	%xmm1,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm10,%xmm1

	vpslld	$19,%xmm10,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm9,%xmm7

	vpsrld	$22,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm10,%xmm2
	vpxor	%xmm4,%xmm11,%xmm9
	vpaddd	%xmm5,%xmm13,%xmm13

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm9,%xmm9
	vmovdqu	128-128(%rax),%xmm5
	vpaddd	0-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	80-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm13,%xmm7
	vpslld	$26,%xmm13,%xmm2
	vmovdqu	%xmm6,112-128(%rax)
	vpaddd	%xmm8,%xmm6,%xmm6

	vpsrld	$11,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm13,%xmm2
	vpaddd	96(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm13,%xmm2
	vpandn	%xmm15,%xmm13,%xmm0
	vpand	%xmm14,%xmm13,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm9,%xmm8
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm9,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm9,%xmm10,%xmm4

	vpxor	%xmm1,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm9,%xmm1

	vpslld	$19,%xmm9,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm8,%xmm7

	vpsrld	$22,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm9,%xmm2
	vpxor	%xmm3,%xmm10,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm8,%xmm8
	addq	$256,%rbp
	vmovdqu	144-128(%rax),%xmm6
	vpaddd	16-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	96-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm12,%xmm7
	vpslld	$26,%xmm12,%xmm2
	vmovdqu	%xmm5,128-128(%rax)
	vpaddd	%xmm15,%xmm5,%xmm5

	vpsrld	$11,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm12,%xmm2
	vpaddd	-128(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm12,%xmm2
	vpandn	%xmm14,%xmm12,%xmm0
	vpand	%xmm13,%xmm12,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm8,%xmm15
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm8,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm8,%xmm9,%xmm3

	vpxor	%xmm1,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm8,%xmm1

	vpslld	$19,%xmm8,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm15,%xmm7

	vpsrld	$22,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm8,%xmm2
	vpxor	%xmm4,%xmm9,%xmm15
	vpaddd	%xmm5,%xmm11,%xmm11

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm15,%xmm15
	vpaddd	%xmm7,%xmm15,%xmm15
	vmovdqu	160-128(%rax),%xmm5
	vpaddd	32-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	112-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm11,%xmm7
	vpslld	$26,%xmm11,%xmm2
	vmovdqu	%xmm6,144-128(%rax)
	vpaddd	%xmm14,%xmm6,%xmm6

	vpsrld	$11,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm11,%xmm2
	vpaddd	-96(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm11,%xmm2
	vpandn	%xmm13,%xmm11,%xmm0
	vpand	%xmm12,%xmm11,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm15,%xmm14
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm15,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm15,%xmm8,%xmm4

	vpxor	%xmm1,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm15,%xmm1

	vpslld	$19,%xmm15,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm14,%xmm7

	vpsrld	$22,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm15,%xmm2
	vpxor	%xmm3,%xmm8,%xmm14
	vpaddd	%xmm6,%xmm10,%xmm10

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm14,%xmm14
	vpaddd	%xmm7,%xmm14,%xmm14
	vmovdqu	176-128(%rax),%xmm6
	vpaddd	48-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	128-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm10,%xmm7
	vpslld	$26,%xmm10,%xmm2
	vmovdqu	%xmm5,160-128(%rax)
	vpaddd	%xmm13,%xmm5,%xmm5

	vpsrld	$11,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm10,%xmm2
	vpaddd	-64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm10,%xmm2
	vpandn	%xmm12,%xmm10,%xmm0
	vpand	%xmm11,%xmm10,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm14,%xmm13
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm14,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm14,%xmm15,%xmm3

	vpxor	%xmm1,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm14,%xmm1

	vpslld	$19,%xmm14,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm13,%xmm7

	vpsrld	$22,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm14,%xmm2
	vpxor	%xmm4,%xmm15,%xmm13
	vpaddd	%xmm5,%xmm9,%xmm9

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm13,%xmm13
	vpaddd	%xmm7,%xmm13,%xmm13
	vmovdqu	192-128(%rax),%xmm5
	vpaddd	64-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	144-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm9,%xmm7
	vpslld	$26,%xmm9,%xmm2
	vmovdqu	%xmm6,176-128(%rax)
	vpaddd	%xmm12,%xmm6,%xmm6

	vpsrld	$11,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm9,%xmm2
	vpaddd	-32(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm9,%xmm2
	vpandn	%xmm11,%xmm9,%xmm0
	vpand	%xmm10,%xmm9,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm13,%xmm12
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm13,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm13,%xmm14,%xmm4

	vpxor	%xmm1,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm13,%xmm1

	vpslld	$19,%xmm13,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm12,%xmm7

	vpsrld	$22,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm13,%xmm2
	vpxor	%xmm3,%xmm14,%xmm12
	vpaddd	%xmm6,%xmm8,%xmm8

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm12,%xmm12
	vpaddd	%xmm7,%xmm12,%xmm12
	vmovdqu	208-128(%rax),%xmm6
	vpaddd	80-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	160-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm8,%xmm7
	vpslld	$26,%xmm8,%xmm2
	vmovdqu	%xmm5,192-128(%rax)
	vpaddd	%xmm11,%xmm5,%xmm5

	vpsrld	$11,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm8,%xmm2
	vpaddd	0(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm8,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm8,%xmm2
	vpandn	%xmm10,%xmm8,%xmm0
	vpand	%xmm9,%xmm8,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm12,%xmm11
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm12,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm12,%xmm13,%xmm3

	vpxor	%xmm1,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm12,%xmm1

	vpslld	$19,%xmm12,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm11,%xmm7

	vpsrld	$22,%xmm12,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm12,%xmm2
	vpxor	%xmm4,%xmm13,%xmm11
	vpaddd	%xmm5,%xmm15,%xmm15

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm11,%xmm11
	vpaddd	%xmm7,%xmm11,%xmm11
	vmovdqu	224-128(%rax),%xmm5
	vpaddd	96-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	176-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm15,%xmm7
	vpslld	$26,%xmm15,%xmm2
	vmovdqu	%xmm6,208-128(%rax)
	vpaddd	%xmm10,%xmm6,%xmm6

	vpsrld	$11,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm15,%xmm2
	vpaddd	32(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm15,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm15,%xmm2
	vpandn	%xmm9,%xmm15,%xmm0
	vpand	%xmm8,%xmm15,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm11,%xmm10
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm11,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm11,%xmm12,%xmm4

	vpxor	%xmm1,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm11,%xmm1

	vpslld	$19,%xmm11,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm10,%xmm7

	vpsrld	$22,%xmm11,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm11,%xmm2
	vpxor	%xmm3,%xmm12,%xmm10
	vpaddd	%xmm6,%xmm14,%xmm14

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm10,%xmm10
	vpaddd	%xmm7,%xmm10,%xmm10
	vmovdqu	240-128(%rax),%xmm6
	vpaddd	112-128(%rax),%xmm5,%xmm5

	vpsrld	$3,%xmm6,%xmm7
	vpsrld	$7,%xmm6,%xmm1
	vpslld	$25,%xmm6,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm6,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm6,%xmm2
	vmovdqu	192-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm5,%xmm5
	vpxor	%xmm1,%xmm3,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm5,%xmm5
	vpsrld	$6,%xmm14,%xmm7
	vpslld	$26,%xmm14,%xmm2
	vmovdqu	%xmm5,224-128(%rax)
	vpaddd	%xmm9,%xmm5,%xmm5

	vpsrld	$11,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm14,%xmm2
	vpaddd	64(%rbp),%xmm5,%xmm5
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm14,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm14,%xmm2
	vpandn	%xmm8,%xmm14,%xmm0
	vpand	%xmm15,%xmm14,%xmm3

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm10,%xmm9
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm10,%xmm1
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm10,%xmm11,%xmm3

	vpxor	%xmm1,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm5,%xmm5

	vpsrld	$13,%xmm10,%xmm1

	vpslld	$19,%xmm10,%xmm2
	vpaddd	%xmm0,%xmm5,%xmm5
	vpand	%xmm3,%xmm4,%xmm4

	vpxor	%xmm1,%xmm9,%xmm7

	vpsrld	$22,%xmm10,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm10,%xmm2
	vpxor	%xmm4,%xmm11,%xmm9
	vpaddd	%xmm5,%xmm13,%xmm13

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm5,%xmm9,%xmm9
	vpaddd	%xmm7,%xmm9,%xmm9
	vmovdqu	0-128(%rax),%xmm5
	vpaddd	128-128(%rax),%xmm6,%xmm6

	vpsrld	$3,%xmm5,%xmm7
	vpsrld	$7,%xmm5,%xmm1
	vpslld	$25,%xmm5,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$18,%xmm5,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$14,%xmm5,%xmm2
	vmovdqu	208-128(%rax),%xmm0
	vpsrld	$10,%xmm0,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7
	vpsrld	$17,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$15,%xmm0,%xmm2
	vpaddd	%xmm7,%xmm6,%xmm6
	vpxor	%xmm1,%xmm4,%xmm7
	vpsrld	$19,%xmm0,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$13,%xmm0,%xmm2
	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7
	vpaddd	%xmm7,%xmm6,%xmm6
	vpsrld	$6,%xmm13,%xmm7
	vpslld	$26,%xmm13,%xmm2
	vmovdqu	%xmm6,240-128(%rax)
	vpaddd	%xmm8,%xmm6,%xmm6

	vpsrld	$11,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpslld	$21,%xmm13,%xmm2
	vpaddd	96(%rbp),%xmm6,%xmm6
	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$25,%xmm13,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$7,%xmm13,%xmm2
	vpandn	%xmm15,%xmm13,%xmm0
	vpand	%xmm14,%xmm13,%xmm4

	vpxor	%xmm1,%xmm7,%xmm7

	vpsrld	$2,%xmm9,%xmm8
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$30,%xmm9,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm9,%xmm10,%xmm4

	vpxor	%xmm1,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm6,%xmm6

	vpsrld	$13,%xmm9,%xmm1

	vpslld	$19,%xmm9,%xmm2
	vpaddd	%xmm0,%xmm6,%xmm6
	vpand	%xmm4,%xmm3,%xmm3

	vpxor	%xmm1,%xmm8,%xmm7

	vpsrld	$22,%xmm9,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7

	vpslld	$10,%xmm9,%xmm2
	vpxor	%xmm3,%xmm10,%xmm8
	vpaddd	%xmm6,%xmm12,%xmm12

	vpxor	%xmm1,%xmm7,%xmm7
	vpxor	%xmm2,%xmm7,%xmm7

	vpaddd	%xmm6,%xmm8,%xmm8
	vpaddd	%xmm7,%xmm8,%xmm8
	addq	$256,%rbp
	decl	%ecx
	jnz	.Loop_16_xx_avx

	movl	$1,%ecx
	leaq	K256+128(%rip),%rbp
	cmpl	0(%rbx),%ecx
	cmovgeq	%rbp,%r8
	cmpl	4(%rbx),%ecx
	cmovgeq	%rbp,%r9
	cmpl	8(%rbx),%ecx
	cmovgeq	%rbp,%r10
	cmpl	12(%rbx),%ecx
	cmovgeq	%rbp,%r11
	vmovdqa	(%rbx),%xmm7
	vpxor	%xmm0,%xmm0,%xmm0
	vmovdqa	%xmm7,%xmm6
	vpcmpgtd	%xmm0,%xmm6,%xmm6
	vpaddd	%xmm6,%xmm7,%xmm7

	vmovdqu	0-128(%rdi),%xmm0
	vpand	%xmm6,%xmm8,%xmm8
	vmovdqu	32-128(%rdi),%xmm1
	vpand	%xmm6,%xmm9,%xmm9
	vmovdqu	64-128(%rdi),%xmm2
	vpand	%xmm6,%xmm10,%xmm10
	vmovdqu	96-128(%rdi),%xmm5
	vpand	%xmm6,%xmm11,%xmm11
	vpaddd	%xmm0,%xmm8,%xmm8
	vmovdqu	128-128(%rdi),%xmm0
	vpand	%xmm6,%xmm12,%xmm12
	vpaddd	%xmm1,%xmm9,%xmm9
	vmovdqu	160-128(%rdi),%xmm1
	vpand	%xmm6,%xmm13,%xmm13
	vpaddd	%xmm2,%xmm10,%xmm10
	vmovdqu	192-128(%rdi),%xmm2
	vpand	%xmm6,%xmm14,%xmm14
	vpaddd	%xmm5,%xmm11,%xmm11
	vmovdqu	224-128(%rdi),%xmm5
	vpand	%xmm6,%xmm15,%xmm15
	vpaddd	%xmm0,%xmm12,%xmm12
	vpaddd	%xmm1,%xmm13,%xmm13
	vmovdqu	%xmm8,0-128(%rdi)
	vpaddd	%xmm2,%xmm14,%xmm14
	vmovdqu	%xmm9,32-128(%rdi)
	vpaddd	%xmm5,%xmm15,%xmm15
	vmovdqu	%xmm10,64-128(%rdi)
	vmovdqu	%xmm11,96-128(%rdi)
	vmovdqu	%xmm12,128-128(%rdi)
	vmovdqu	%xmm13,160-128(%rdi)
	vmovdqu	%xmm14,192-128(%rdi)
	vmovdqu	%xmm15,224-128(%rdi)

	vmovdqu	%xmm7,(%rbx)
	vmovdqu	.Lpbswap(%rip),%xmm6
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
.size	sha256_multi_block_avx,.-sha256_multi_block_avx
.type	sha256_multi_block_avx2,@function
.align	32
sha256_multi_block_avx2:
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
	leaq	K256+128(%rip),%rbp
	leaq	128(%rdi),%rdi

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
	vmovdqu	0-128(%rdi),%ymm8
	leaq	128(%rsp),%rax
	vmovdqu	32-128(%rdi),%ymm9
	leaq	256+128(%rsp),%rbx
	vmovdqu	64-128(%rdi),%ymm10
	vmovdqu	96-128(%rdi),%ymm11
	vmovdqu	128-128(%rdi),%ymm12
	vmovdqu	160-128(%rdi),%ymm13
	vmovdqu	192-128(%rdi),%ymm14
	vmovdqu	224-128(%rdi),%ymm15
	vmovdqu	.Lpbswap(%rip),%ymm6
	jmp	.Loop_avx2

.align	32
.Loop_avx2:
	vpxor	%ymm9,%ymm10,%ymm4
	vmovd	0(%r12),%xmm5
	vmovd	0(%r8),%xmm0
	vmovd	0(%r13),%xmm1
	vmovd	0(%r9),%xmm2
	vpinsrd	$1,0(%r14),%xmm5,%xmm5
	vpinsrd	$1,0(%r10),%xmm0,%xmm0
	vpinsrd	$1,0(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,0(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm12,%ymm7
	vpslld	$26,%ymm12,%ymm2
	vmovdqu	%ymm5,0-128(%rax)
	vpaddd	%ymm15,%ymm5,%ymm5

	vpsrld	$11,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm12,%ymm2
	vpaddd	-128(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm12,%ymm2
	vpandn	%ymm14,%ymm12,%ymm0
	vpand	%ymm13,%ymm12,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm8,%ymm15
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm8,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm8,%ymm9,%ymm3

	vpxor	%ymm1,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm8,%ymm1

	vpslld	$19,%ymm8,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm15,%ymm7

	vpsrld	$22,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm8,%ymm2
	vpxor	%ymm4,%ymm9,%ymm15
	vpaddd	%ymm5,%ymm11,%ymm11

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm15,%ymm15
	vmovd	4(%r12),%xmm5
	vmovd	4(%r8),%xmm0
	vmovd	4(%r13),%xmm1
	vmovd	4(%r9),%xmm2
	vpinsrd	$1,4(%r14),%xmm5,%xmm5
	vpinsrd	$1,4(%r10),%xmm0,%xmm0
	vpinsrd	$1,4(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,4(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm11,%ymm7
	vpslld	$26,%ymm11,%ymm2
	vmovdqu	%ymm5,32-128(%rax)
	vpaddd	%ymm14,%ymm5,%ymm5

	vpsrld	$11,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm11,%ymm2
	vpaddd	-96(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm11,%ymm2
	vpandn	%ymm13,%ymm11,%ymm0
	vpand	%ymm12,%ymm11,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm15,%ymm14
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm15,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm15,%ymm8,%ymm4

	vpxor	%ymm1,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm15,%ymm1

	vpslld	$19,%ymm15,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm14,%ymm7

	vpsrld	$22,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm15,%ymm2
	vpxor	%ymm3,%ymm8,%ymm14
	vpaddd	%ymm5,%ymm10,%ymm10

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm14,%ymm14
	vmovd	8(%r12),%xmm5
	vmovd	8(%r8),%xmm0
	vmovd	8(%r13),%xmm1
	vmovd	8(%r9),%xmm2
	vpinsrd	$1,8(%r14),%xmm5,%xmm5
	vpinsrd	$1,8(%r10),%xmm0,%xmm0
	vpinsrd	$1,8(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,8(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm10,%ymm7
	vpslld	$26,%ymm10,%ymm2
	vmovdqu	%ymm5,64-128(%rax)
	vpaddd	%ymm13,%ymm5,%ymm5

	vpsrld	$11,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm10,%ymm2
	vpaddd	-64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm10,%ymm2
	vpandn	%ymm12,%ymm10,%ymm0
	vpand	%ymm11,%ymm10,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm14,%ymm13
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm14,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm14,%ymm15,%ymm3

	vpxor	%ymm1,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm14,%ymm1

	vpslld	$19,%ymm14,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm13,%ymm7

	vpsrld	$22,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm14,%ymm2
	vpxor	%ymm4,%ymm15,%ymm13
	vpaddd	%ymm5,%ymm9,%ymm9

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm13,%ymm13
	vmovd	12(%r12),%xmm5
	vmovd	12(%r8),%xmm0
	vmovd	12(%r13),%xmm1
	vmovd	12(%r9),%xmm2
	vpinsrd	$1,12(%r14),%xmm5,%xmm5
	vpinsrd	$1,12(%r10),%xmm0,%xmm0
	vpinsrd	$1,12(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,12(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm9,%ymm7
	vpslld	$26,%ymm9,%ymm2
	vmovdqu	%ymm5,96-128(%rax)
	vpaddd	%ymm12,%ymm5,%ymm5

	vpsrld	$11,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm9,%ymm2
	vpaddd	-32(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm9,%ymm2
	vpandn	%ymm11,%ymm9,%ymm0
	vpand	%ymm10,%ymm9,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm13,%ymm12
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm13,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm13,%ymm14,%ymm4

	vpxor	%ymm1,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm13,%ymm1

	vpslld	$19,%ymm13,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm12,%ymm7

	vpsrld	$22,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm13,%ymm2
	vpxor	%ymm3,%ymm14,%ymm12
	vpaddd	%ymm5,%ymm8,%ymm8

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm12,%ymm12
	vmovd	16(%r12),%xmm5
	vmovd	16(%r8),%xmm0
	vmovd	16(%r13),%xmm1
	vmovd	16(%r9),%xmm2
	vpinsrd	$1,16(%r14),%xmm5,%xmm5
	vpinsrd	$1,16(%r10),%xmm0,%xmm0
	vpinsrd	$1,16(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,16(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm8,%ymm7
	vpslld	$26,%ymm8,%ymm2
	vmovdqu	%ymm5,128-128(%rax)
	vpaddd	%ymm11,%ymm5,%ymm5

	vpsrld	$11,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm8,%ymm2
	vpaddd	0(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm8,%ymm2
	vpandn	%ymm10,%ymm8,%ymm0
	vpand	%ymm9,%ymm8,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm12,%ymm11
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm12,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm12,%ymm13,%ymm3

	vpxor	%ymm1,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm12,%ymm1

	vpslld	$19,%ymm12,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm11,%ymm7

	vpsrld	$22,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm12,%ymm2
	vpxor	%ymm4,%ymm13,%ymm11
	vpaddd	%ymm5,%ymm15,%ymm15

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm11,%ymm11
	vmovd	20(%r12),%xmm5
	vmovd	20(%r8),%xmm0
	vmovd	20(%r13),%xmm1
	vmovd	20(%r9),%xmm2
	vpinsrd	$1,20(%r14),%xmm5,%xmm5
	vpinsrd	$1,20(%r10),%xmm0,%xmm0
	vpinsrd	$1,20(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,20(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm15,%ymm7
	vpslld	$26,%ymm15,%ymm2
	vmovdqu	%ymm5,160-128(%rax)
	vpaddd	%ymm10,%ymm5,%ymm5

	vpsrld	$11,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm15,%ymm2
	vpaddd	32(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm15,%ymm2
	vpandn	%ymm9,%ymm15,%ymm0
	vpand	%ymm8,%ymm15,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm11,%ymm10
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm11,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm11,%ymm12,%ymm4

	vpxor	%ymm1,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm11,%ymm1

	vpslld	$19,%ymm11,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm10,%ymm7

	vpsrld	$22,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm11,%ymm2
	vpxor	%ymm3,%ymm12,%ymm10
	vpaddd	%ymm5,%ymm14,%ymm14

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm10,%ymm10
	vmovd	24(%r12),%xmm5
	vmovd	24(%r8),%xmm0
	vmovd	24(%r13),%xmm1
	vmovd	24(%r9),%xmm2
	vpinsrd	$1,24(%r14),%xmm5,%xmm5
	vpinsrd	$1,24(%r10),%xmm0,%xmm0
	vpinsrd	$1,24(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,24(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm14,%ymm7
	vpslld	$26,%ymm14,%ymm2
	vmovdqu	%ymm5,192-128(%rax)
	vpaddd	%ymm9,%ymm5,%ymm5

	vpsrld	$11,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm14,%ymm2
	vpaddd	64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm14,%ymm2
	vpandn	%ymm8,%ymm14,%ymm0
	vpand	%ymm15,%ymm14,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm10,%ymm9
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm10,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm10,%ymm11,%ymm3

	vpxor	%ymm1,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm10,%ymm1

	vpslld	$19,%ymm10,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm9,%ymm7

	vpsrld	$22,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm10,%ymm2
	vpxor	%ymm4,%ymm11,%ymm9
	vpaddd	%ymm5,%ymm13,%ymm13

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm9,%ymm9
	vmovd	28(%r12),%xmm5
	vmovd	28(%r8),%xmm0
	vmovd	28(%r13),%xmm1
	vmovd	28(%r9),%xmm2
	vpinsrd	$1,28(%r14),%xmm5,%xmm5
	vpinsrd	$1,28(%r10),%xmm0,%xmm0
	vpinsrd	$1,28(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,28(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm13,%ymm7
	vpslld	$26,%ymm13,%ymm2
	vmovdqu	%ymm5,224-128(%rax)
	vpaddd	%ymm8,%ymm5,%ymm5

	vpsrld	$11,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm13,%ymm2
	vpaddd	96(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm13,%ymm2
	vpandn	%ymm15,%ymm13,%ymm0
	vpand	%ymm14,%ymm13,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm9,%ymm8
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm9,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm9,%ymm10,%ymm4

	vpxor	%ymm1,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm9,%ymm1

	vpslld	$19,%ymm9,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm8,%ymm7

	vpsrld	$22,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm9,%ymm2
	vpxor	%ymm3,%ymm10,%ymm8
	vpaddd	%ymm5,%ymm12,%ymm12

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm8,%ymm8
	addq	$256,%rbp
	vmovd	32(%r12),%xmm5
	vmovd	32(%r8),%xmm0
	vmovd	32(%r13),%xmm1
	vmovd	32(%r9),%xmm2
	vpinsrd	$1,32(%r14),%xmm5,%xmm5
	vpinsrd	$1,32(%r10),%xmm0,%xmm0
	vpinsrd	$1,32(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,32(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm12,%ymm7
	vpslld	$26,%ymm12,%ymm2
	vmovdqu	%ymm5,256-256-128(%rbx)
	vpaddd	%ymm15,%ymm5,%ymm5

	vpsrld	$11,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm12,%ymm2
	vpaddd	-128(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm12,%ymm2
	vpandn	%ymm14,%ymm12,%ymm0
	vpand	%ymm13,%ymm12,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm8,%ymm15
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm8,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm8,%ymm9,%ymm3

	vpxor	%ymm1,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm8,%ymm1

	vpslld	$19,%ymm8,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm15,%ymm7

	vpsrld	$22,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm8,%ymm2
	vpxor	%ymm4,%ymm9,%ymm15
	vpaddd	%ymm5,%ymm11,%ymm11

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm15,%ymm15
	vmovd	36(%r12),%xmm5
	vmovd	36(%r8),%xmm0
	vmovd	36(%r13),%xmm1
	vmovd	36(%r9),%xmm2
	vpinsrd	$1,36(%r14),%xmm5,%xmm5
	vpinsrd	$1,36(%r10),%xmm0,%xmm0
	vpinsrd	$1,36(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,36(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm11,%ymm7
	vpslld	$26,%ymm11,%ymm2
	vmovdqu	%ymm5,288-256-128(%rbx)
	vpaddd	%ymm14,%ymm5,%ymm5

	vpsrld	$11,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm11,%ymm2
	vpaddd	-96(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm11,%ymm2
	vpandn	%ymm13,%ymm11,%ymm0
	vpand	%ymm12,%ymm11,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm15,%ymm14
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm15,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm15,%ymm8,%ymm4

	vpxor	%ymm1,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm15,%ymm1

	vpslld	$19,%ymm15,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm14,%ymm7

	vpsrld	$22,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm15,%ymm2
	vpxor	%ymm3,%ymm8,%ymm14
	vpaddd	%ymm5,%ymm10,%ymm10

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm14,%ymm14
	vmovd	40(%r12),%xmm5
	vmovd	40(%r8),%xmm0
	vmovd	40(%r13),%xmm1
	vmovd	40(%r9),%xmm2
	vpinsrd	$1,40(%r14),%xmm5,%xmm5
	vpinsrd	$1,40(%r10),%xmm0,%xmm0
	vpinsrd	$1,40(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,40(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm10,%ymm7
	vpslld	$26,%ymm10,%ymm2
	vmovdqu	%ymm5,320-256-128(%rbx)
	vpaddd	%ymm13,%ymm5,%ymm5

	vpsrld	$11,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm10,%ymm2
	vpaddd	-64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm10,%ymm2
	vpandn	%ymm12,%ymm10,%ymm0
	vpand	%ymm11,%ymm10,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm14,%ymm13
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm14,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm14,%ymm15,%ymm3

	vpxor	%ymm1,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm14,%ymm1

	vpslld	$19,%ymm14,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm13,%ymm7

	vpsrld	$22,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm14,%ymm2
	vpxor	%ymm4,%ymm15,%ymm13
	vpaddd	%ymm5,%ymm9,%ymm9

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm13,%ymm13
	vmovd	44(%r12),%xmm5
	vmovd	44(%r8),%xmm0
	vmovd	44(%r13),%xmm1
	vmovd	44(%r9),%xmm2
	vpinsrd	$1,44(%r14),%xmm5,%xmm5
	vpinsrd	$1,44(%r10),%xmm0,%xmm0
	vpinsrd	$1,44(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,44(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm9,%ymm7
	vpslld	$26,%ymm9,%ymm2
	vmovdqu	%ymm5,352-256-128(%rbx)
	vpaddd	%ymm12,%ymm5,%ymm5

	vpsrld	$11,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm9,%ymm2
	vpaddd	-32(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm9,%ymm2
	vpandn	%ymm11,%ymm9,%ymm0
	vpand	%ymm10,%ymm9,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm13,%ymm12
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm13,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm13,%ymm14,%ymm4

	vpxor	%ymm1,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm13,%ymm1

	vpslld	$19,%ymm13,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm12,%ymm7

	vpsrld	$22,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm13,%ymm2
	vpxor	%ymm3,%ymm14,%ymm12
	vpaddd	%ymm5,%ymm8,%ymm8

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm12,%ymm12
	vmovd	48(%r12),%xmm5
	vmovd	48(%r8),%xmm0
	vmovd	48(%r13),%xmm1
	vmovd	48(%r9),%xmm2
	vpinsrd	$1,48(%r14),%xmm5,%xmm5
	vpinsrd	$1,48(%r10),%xmm0,%xmm0
	vpinsrd	$1,48(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,48(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm8,%ymm7
	vpslld	$26,%ymm8,%ymm2
	vmovdqu	%ymm5,384-256-128(%rbx)
	vpaddd	%ymm11,%ymm5,%ymm5

	vpsrld	$11,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm8,%ymm2
	vpaddd	0(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm8,%ymm2
	vpandn	%ymm10,%ymm8,%ymm0
	vpand	%ymm9,%ymm8,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm12,%ymm11
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm12,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm12,%ymm13,%ymm3

	vpxor	%ymm1,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm12,%ymm1

	vpslld	$19,%ymm12,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm11,%ymm7

	vpsrld	$22,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm12,%ymm2
	vpxor	%ymm4,%ymm13,%ymm11
	vpaddd	%ymm5,%ymm15,%ymm15

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm11,%ymm11
	vmovd	52(%r12),%xmm5
	vmovd	52(%r8),%xmm0
	vmovd	52(%r13),%xmm1
	vmovd	52(%r9),%xmm2
	vpinsrd	$1,52(%r14),%xmm5,%xmm5
	vpinsrd	$1,52(%r10),%xmm0,%xmm0
	vpinsrd	$1,52(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,52(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm15,%ymm7
	vpslld	$26,%ymm15,%ymm2
	vmovdqu	%ymm5,416-256-128(%rbx)
	vpaddd	%ymm10,%ymm5,%ymm5

	vpsrld	$11,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm15,%ymm2
	vpaddd	32(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm15,%ymm2
	vpandn	%ymm9,%ymm15,%ymm0
	vpand	%ymm8,%ymm15,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm11,%ymm10
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm11,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm11,%ymm12,%ymm4

	vpxor	%ymm1,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm11,%ymm1

	vpslld	$19,%ymm11,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm10,%ymm7

	vpsrld	$22,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm11,%ymm2
	vpxor	%ymm3,%ymm12,%ymm10
	vpaddd	%ymm5,%ymm14,%ymm14

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm10,%ymm10
	vmovd	56(%r12),%xmm5
	vmovd	56(%r8),%xmm0
	vmovd	56(%r13),%xmm1
	vmovd	56(%r9),%xmm2
	vpinsrd	$1,56(%r14),%xmm5,%xmm5
	vpinsrd	$1,56(%r10),%xmm0,%xmm0
	vpinsrd	$1,56(%r15),%xmm1,%xmm1
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,56(%r11),%xmm2,%xmm2
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm14,%ymm7
	vpslld	$26,%ymm14,%ymm2
	vmovdqu	%ymm5,448-256-128(%rbx)
	vpaddd	%ymm9,%ymm5,%ymm5

	vpsrld	$11,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm14,%ymm2
	vpaddd	64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm14,%ymm2
	vpandn	%ymm8,%ymm14,%ymm0
	vpand	%ymm15,%ymm14,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm10,%ymm9
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm10,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm10,%ymm11,%ymm3

	vpxor	%ymm1,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm10,%ymm1

	vpslld	$19,%ymm10,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm9,%ymm7

	vpsrld	$22,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm10,%ymm2
	vpxor	%ymm4,%ymm11,%ymm9
	vpaddd	%ymm5,%ymm13,%ymm13

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm9,%ymm9
	vmovd	60(%r12),%xmm5
	leaq	64(%r12),%r12
	vmovd	60(%r8),%xmm0
	leaq	64(%r8),%r8
	vmovd	60(%r13),%xmm1
	leaq	64(%r13),%r13
	vmovd	60(%r9),%xmm2
	leaq	64(%r9),%r9
	vpinsrd	$1,60(%r14),%xmm5,%xmm5
	leaq	64(%r14),%r14
	vpinsrd	$1,60(%r10),%xmm0,%xmm0
	leaq	64(%r10),%r10
	vpinsrd	$1,60(%r15),%xmm1,%xmm1
	leaq	64(%r15),%r15
	vpunpckldq	%ymm1,%ymm5,%ymm5
	vpinsrd	$1,60(%r11),%xmm2,%xmm2
	leaq	64(%r11),%r11
	vpunpckldq	%ymm2,%ymm0,%ymm0
	vinserti128	$1,%xmm0,%ymm5,%ymm5
	vpshufb	%ymm6,%ymm5,%ymm5
	vpsrld	$6,%ymm13,%ymm7
	vpslld	$26,%ymm13,%ymm2
	vmovdqu	%ymm5,480-256-128(%rbx)
	vpaddd	%ymm8,%ymm5,%ymm5

	vpsrld	$11,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm13,%ymm2
	vpaddd	96(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	prefetcht0	63(%r12)
	vpslld	$7,%ymm13,%ymm2
	vpandn	%ymm15,%ymm13,%ymm0
	vpand	%ymm14,%ymm13,%ymm4
	prefetcht0	63(%r13)
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm9,%ymm8
	vpxor	%ymm2,%ymm7,%ymm7
	prefetcht0	63(%r14)
	vpslld	$30,%ymm9,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm9,%ymm10,%ymm4
	prefetcht0	63(%r15)
	vpxor	%ymm1,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm9,%ymm1
	prefetcht0	63(%r8)
	vpslld	$19,%ymm9,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm4,%ymm3,%ymm3
	prefetcht0	63(%r9)
	vpxor	%ymm1,%ymm8,%ymm7

	vpsrld	$22,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	prefetcht0	63(%r10)
	vpslld	$10,%ymm9,%ymm2
	vpxor	%ymm3,%ymm10,%ymm8
	vpaddd	%ymm5,%ymm12,%ymm12
	prefetcht0	63(%r11)
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm8,%ymm8
	addq	$256,%rbp
	vmovdqu	0-128(%rax),%ymm5
	movl	$3,%ecx
	jmp	.Loop_16_xx_avx2
.align	32
.Loop_16_xx_avx2:
	vmovdqu	32-128(%rax),%ymm6
	vpaddd	288-256-128(%rbx),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	448-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm12,%ymm7
	vpslld	$26,%ymm12,%ymm2
	vmovdqu	%ymm5,0-128(%rax)
	vpaddd	%ymm15,%ymm5,%ymm5

	vpsrld	$11,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm12,%ymm2
	vpaddd	-128(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm12,%ymm2
	vpandn	%ymm14,%ymm12,%ymm0
	vpand	%ymm13,%ymm12,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm8,%ymm15
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm8,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm8,%ymm9,%ymm3

	vpxor	%ymm1,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm8,%ymm1

	vpslld	$19,%ymm8,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm15,%ymm7

	vpsrld	$22,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm8,%ymm2
	vpxor	%ymm4,%ymm9,%ymm15
	vpaddd	%ymm5,%ymm11,%ymm11

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm15,%ymm15
	vmovdqu	64-128(%rax),%ymm5
	vpaddd	320-256-128(%rbx),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	480-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm11,%ymm7
	vpslld	$26,%ymm11,%ymm2
	vmovdqu	%ymm6,32-128(%rax)
	vpaddd	%ymm14,%ymm6,%ymm6

	vpsrld	$11,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm11,%ymm2
	vpaddd	-96(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm11,%ymm2
	vpandn	%ymm13,%ymm11,%ymm0
	vpand	%ymm12,%ymm11,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm15,%ymm14
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm15,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm15,%ymm8,%ymm4

	vpxor	%ymm1,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm15,%ymm1

	vpslld	$19,%ymm15,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm14,%ymm7

	vpsrld	$22,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm15,%ymm2
	vpxor	%ymm3,%ymm8,%ymm14
	vpaddd	%ymm6,%ymm10,%ymm10

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm14,%ymm14
	vmovdqu	96-128(%rax),%ymm6
	vpaddd	352-256-128(%rbx),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	0-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm10,%ymm7
	vpslld	$26,%ymm10,%ymm2
	vmovdqu	%ymm5,64-128(%rax)
	vpaddd	%ymm13,%ymm5,%ymm5

	vpsrld	$11,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm10,%ymm2
	vpaddd	-64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm10,%ymm2
	vpandn	%ymm12,%ymm10,%ymm0
	vpand	%ymm11,%ymm10,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm14,%ymm13
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm14,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm14,%ymm15,%ymm3

	vpxor	%ymm1,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm14,%ymm1

	vpslld	$19,%ymm14,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm13,%ymm7

	vpsrld	$22,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm14,%ymm2
	vpxor	%ymm4,%ymm15,%ymm13
	vpaddd	%ymm5,%ymm9,%ymm9

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm13,%ymm13
	vmovdqu	128-128(%rax),%ymm5
	vpaddd	384-256-128(%rbx),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	32-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm9,%ymm7
	vpslld	$26,%ymm9,%ymm2
	vmovdqu	%ymm6,96-128(%rax)
	vpaddd	%ymm12,%ymm6,%ymm6

	vpsrld	$11,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm9,%ymm2
	vpaddd	-32(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm9,%ymm2
	vpandn	%ymm11,%ymm9,%ymm0
	vpand	%ymm10,%ymm9,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm13,%ymm12
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm13,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm13,%ymm14,%ymm4

	vpxor	%ymm1,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm13,%ymm1

	vpslld	$19,%ymm13,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm12,%ymm7

	vpsrld	$22,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm13,%ymm2
	vpxor	%ymm3,%ymm14,%ymm12
	vpaddd	%ymm6,%ymm8,%ymm8

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm12,%ymm12
	vmovdqu	160-128(%rax),%ymm6
	vpaddd	416-256-128(%rbx),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	64-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm8,%ymm7
	vpslld	$26,%ymm8,%ymm2
	vmovdqu	%ymm5,128-128(%rax)
	vpaddd	%ymm11,%ymm5,%ymm5

	vpsrld	$11,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm8,%ymm2
	vpaddd	0(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm8,%ymm2
	vpandn	%ymm10,%ymm8,%ymm0
	vpand	%ymm9,%ymm8,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm12,%ymm11
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm12,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm12,%ymm13,%ymm3

	vpxor	%ymm1,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm12,%ymm1

	vpslld	$19,%ymm12,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm11,%ymm7

	vpsrld	$22,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm12,%ymm2
	vpxor	%ymm4,%ymm13,%ymm11
	vpaddd	%ymm5,%ymm15,%ymm15

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm11,%ymm11
	vmovdqu	192-128(%rax),%ymm5
	vpaddd	448-256-128(%rbx),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	96-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm15,%ymm7
	vpslld	$26,%ymm15,%ymm2
	vmovdqu	%ymm6,160-128(%rax)
	vpaddd	%ymm10,%ymm6,%ymm6

	vpsrld	$11,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm15,%ymm2
	vpaddd	32(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm15,%ymm2
	vpandn	%ymm9,%ymm15,%ymm0
	vpand	%ymm8,%ymm15,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm11,%ymm10
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm11,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm11,%ymm12,%ymm4

	vpxor	%ymm1,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm11,%ymm1

	vpslld	$19,%ymm11,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm10,%ymm7

	vpsrld	$22,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm11,%ymm2
	vpxor	%ymm3,%ymm12,%ymm10
	vpaddd	%ymm6,%ymm14,%ymm14

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm10,%ymm10
	vmovdqu	224-128(%rax),%ymm6
	vpaddd	480-256-128(%rbx),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	128-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm14,%ymm7
	vpslld	$26,%ymm14,%ymm2
	vmovdqu	%ymm5,192-128(%rax)
	vpaddd	%ymm9,%ymm5,%ymm5

	vpsrld	$11,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm14,%ymm2
	vpaddd	64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm14,%ymm2
	vpandn	%ymm8,%ymm14,%ymm0
	vpand	%ymm15,%ymm14,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm10,%ymm9
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm10,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm10,%ymm11,%ymm3

	vpxor	%ymm1,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm10,%ymm1

	vpslld	$19,%ymm10,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm9,%ymm7

	vpsrld	$22,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm10,%ymm2
	vpxor	%ymm4,%ymm11,%ymm9
	vpaddd	%ymm5,%ymm13,%ymm13

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm9,%ymm9
	vmovdqu	256-256-128(%rbx),%ymm5
	vpaddd	0-128(%rax),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	160-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm13,%ymm7
	vpslld	$26,%ymm13,%ymm2
	vmovdqu	%ymm6,224-128(%rax)
	vpaddd	%ymm8,%ymm6,%ymm6

	vpsrld	$11,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm13,%ymm2
	vpaddd	96(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm13,%ymm2
	vpandn	%ymm15,%ymm13,%ymm0
	vpand	%ymm14,%ymm13,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm9,%ymm8
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm9,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm9,%ymm10,%ymm4

	vpxor	%ymm1,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm9,%ymm1

	vpslld	$19,%ymm9,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm8,%ymm7

	vpsrld	$22,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm9,%ymm2
	vpxor	%ymm3,%ymm10,%ymm8
	vpaddd	%ymm6,%ymm12,%ymm12

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm8,%ymm8
	addq	$256,%rbp
	vmovdqu	288-256-128(%rbx),%ymm6
	vpaddd	32-128(%rax),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	192-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm12,%ymm7
	vpslld	$26,%ymm12,%ymm2
	vmovdqu	%ymm5,256-256-128(%rbx)
	vpaddd	%ymm15,%ymm5,%ymm5

	vpsrld	$11,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm12,%ymm2
	vpaddd	-128(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm12,%ymm2
	vpandn	%ymm14,%ymm12,%ymm0
	vpand	%ymm13,%ymm12,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm8,%ymm15
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm8,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm8,%ymm9,%ymm3

	vpxor	%ymm1,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm8,%ymm1

	vpslld	$19,%ymm8,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm15,%ymm7

	vpsrld	$22,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm8,%ymm2
	vpxor	%ymm4,%ymm9,%ymm15
	vpaddd	%ymm5,%ymm11,%ymm11

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm15,%ymm15
	vpaddd	%ymm7,%ymm15,%ymm15
	vmovdqu	320-256-128(%rbx),%ymm5
	vpaddd	64-128(%rax),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	224-128(%rax),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm11,%ymm7
	vpslld	$26,%ymm11,%ymm2
	vmovdqu	%ymm6,288-256-128(%rbx)
	vpaddd	%ymm14,%ymm6,%ymm6

	vpsrld	$11,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm11,%ymm2
	vpaddd	-96(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm11,%ymm2
	vpandn	%ymm13,%ymm11,%ymm0
	vpand	%ymm12,%ymm11,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm15,%ymm14
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm15,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm15,%ymm8,%ymm4

	vpxor	%ymm1,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm15,%ymm1

	vpslld	$19,%ymm15,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm14,%ymm7

	vpsrld	$22,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm15,%ymm2
	vpxor	%ymm3,%ymm8,%ymm14
	vpaddd	%ymm6,%ymm10,%ymm10

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm14,%ymm14
	vpaddd	%ymm7,%ymm14,%ymm14
	vmovdqu	352-256-128(%rbx),%ymm6
	vpaddd	96-128(%rax),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	256-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm10,%ymm7
	vpslld	$26,%ymm10,%ymm2
	vmovdqu	%ymm5,320-256-128(%rbx)
	vpaddd	%ymm13,%ymm5,%ymm5

	vpsrld	$11,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm10,%ymm2
	vpaddd	-64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm10,%ymm2
	vpandn	%ymm12,%ymm10,%ymm0
	vpand	%ymm11,%ymm10,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm14,%ymm13
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm14,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm14,%ymm15,%ymm3

	vpxor	%ymm1,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm14,%ymm1

	vpslld	$19,%ymm14,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm13,%ymm7

	vpsrld	$22,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm14,%ymm2
	vpxor	%ymm4,%ymm15,%ymm13
	vpaddd	%ymm5,%ymm9,%ymm9

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm13,%ymm13
	vpaddd	%ymm7,%ymm13,%ymm13
	vmovdqu	384-256-128(%rbx),%ymm5
	vpaddd	128-128(%rax),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	288-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm9,%ymm7
	vpslld	$26,%ymm9,%ymm2
	vmovdqu	%ymm6,352-256-128(%rbx)
	vpaddd	%ymm12,%ymm6,%ymm6

	vpsrld	$11,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm9,%ymm2
	vpaddd	-32(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm9,%ymm2
	vpandn	%ymm11,%ymm9,%ymm0
	vpand	%ymm10,%ymm9,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm13,%ymm12
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm13,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm13,%ymm14,%ymm4

	vpxor	%ymm1,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm13,%ymm1

	vpslld	$19,%ymm13,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm12,%ymm7

	vpsrld	$22,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm13,%ymm2
	vpxor	%ymm3,%ymm14,%ymm12
	vpaddd	%ymm6,%ymm8,%ymm8

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm12,%ymm12
	vpaddd	%ymm7,%ymm12,%ymm12
	vmovdqu	416-256-128(%rbx),%ymm6
	vpaddd	160-128(%rax),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	320-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm8,%ymm7
	vpslld	$26,%ymm8,%ymm2
	vmovdqu	%ymm5,384-256-128(%rbx)
	vpaddd	%ymm11,%ymm5,%ymm5

	vpsrld	$11,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm8,%ymm2
	vpaddd	0(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm8,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm8,%ymm2
	vpandn	%ymm10,%ymm8,%ymm0
	vpand	%ymm9,%ymm8,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm12,%ymm11
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm12,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm12,%ymm13,%ymm3

	vpxor	%ymm1,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm12,%ymm1

	vpslld	$19,%ymm12,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm11,%ymm7

	vpsrld	$22,%ymm12,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm12,%ymm2
	vpxor	%ymm4,%ymm13,%ymm11
	vpaddd	%ymm5,%ymm15,%ymm15

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm11,%ymm11
	vpaddd	%ymm7,%ymm11,%ymm11
	vmovdqu	448-256-128(%rbx),%ymm5
	vpaddd	192-128(%rax),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	352-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm15,%ymm7
	vpslld	$26,%ymm15,%ymm2
	vmovdqu	%ymm6,416-256-128(%rbx)
	vpaddd	%ymm10,%ymm6,%ymm6

	vpsrld	$11,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm15,%ymm2
	vpaddd	32(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm15,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm15,%ymm2
	vpandn	%ymm9,%ymm15,%ymm0
	vpand	%ymm8,%ymm15,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm11,%ymm10
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm11,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm11,%ymm12,%ymm4

	vpxor	%ymm1,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm11,%ymm1

	vpslld	$19,%ymm11,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm10,%ymm7

	vpsrld	$22,%ymm11,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm11,%ymm2
	vpxor	%ymm3,%ymm12,%ymm10
	vpaddd	%ymm6,%ymm14,%ymm14

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm10,%ymm10
	vpaddd	%ymm7,%ymm10,%ymm10
	vmovdqu	480-256-128(%rbx),%ymm6
	vpaddd	224-128(%rax),%ymm5,%ymm5

	vpsrld	$3,%ymm6,%ymm7
	vpsrld	$7,%ymm6,%ymm1
	vpslld	$25,%ymm6,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm6,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm6,%ymm2
	vmovdqu	384-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm5,%ymm5
	vpxor	%ymm1,%ymm3,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm5,%ymm5
	vpsrld	$6,%ymm14,%ymm7
	vpslld	$26,%ymm14,%ymm2
	vmovdqu	%ymm5,448-256-128(%rbx)
	vpaddd	%ymm9,%ymm5,%ymm5

	vpsrld	$11,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm14,%ymm2
	vpaddd	64(%rbp),%ymm5,%ymm5
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm14,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm14,%ymm2
	vpandn	%ymm8,%ymm14,%ymm0
	vpand	%ymm15,%ymm14,%ymm3

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm10,%ymm9
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm10,%ymm1
	vpxor	%ymm3,%ymm0,%ymm0
	vpxor	%ymm10,%ymm11,%ymm3

	vpxor	%ymm1,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm5,%ymm5

	vpsrld	$13,%ymm10,%ymm1

	vpslld	$19,%ymm10,%ymm2
	vpaddd	%ymm0,%ymm5,%ymm5
	vpand	%ymm3,%ymm4,%ymm4

	vpxor	%ymm1,%ymm9,%ymm7

	vpsrld	$22,%ymm10,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm10,%ymm2
	vpxor	%ymm4,%ymm11,%ymm9
	vpaddd	%ymm5,%ymm13,%ymm13

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm5,%ymm9,%ymm9
	vpaddd	%ymm7,%ymm9,%ymm9
	vmovdqu	0-128(%rax),%ymm5
	vpaddd	256-256-128(%rbx),%ymm6,%ymm6

	vpsrld	$3,%ymm5,%ymm7
	vpsrld	$7,%ymm5,%ymm1
	vpslld	$25,%ymm5,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$18,%ymm5,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$14,%ymm5,%ymm2
	vmovdqu	416-256-128(%rbx),%ymm0
	vpsrld	$10,%ymm0,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7
	vpsrld	$17,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$15,%ymm0,%ymm2
	vpaddd	%ymm7,%ymm6,%ymm6
	vpxor	%ymm1,%ymm4,%ymm7
	vpsrld	$19,%ymm0,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$13,%ymm0,%ymm2
	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7
	vpaddd	%ymm7,%ymm6,%ymm6
	vpsrld	$6,%ymm13,%ymm7
	vpslld	$26,%ymm13,%ymm2
	vmovdqu	%ymm6,480-256-128(%rbx)
	vpaddd	%ymm8,%ymm6,%ymm6

	vpsrld	$11,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7
	vpslld	$21,%ymm13,%ymm2
	vpaddd	96(%rbp),%ymm6,%ymm6
	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$25,%ymm13,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$7,%ymm13,%ymm2
	vpandn	%ymm15,%ymm13,%ymm0
	vpand	%ymm14,%ymm13,%ymm4

	vpxor	%ymm1,%ymm7,%ymm7

	vpsrld	$2,%ymm9,%ymm8
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$30,%ymm9,%ymm1
	vpxor	%ymm4,%ymm0,%ymm0
	vpxor	%ymm9,%ymm10,%ymm4

	vpxor	%ymm1,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm6,%ymm6

	vpsrld	$13,%ymm9,%ymm1

	vpslld	$19,%ymm9,%ymm2
	vpaddd	%ymm0,%ymm6,%ymm6
	vpand	%ymm4,%ymm3,%ymm3

	vpxor	%ymm1,%ymm8,%ymm7

	vpsrld	$22,%ymm9,%ymm1
	vpxor	%ymm2,%ymm7,%ymm7

	vpslld	$10,%ymm9,%ymm2
	vpxor	%ymm3,%ymm10,%ymm8
	vpaddd	%ymm6,%ymm12,%ymm12

	vpxor	%ymm1,%ymm7,%ymm7
	vpxor	%ymm2,%ymm7,%ymm7

	vpaddd	%ymm6,%ymm8,%ymm8
	vpaddd	%ymm7,%ymm8,%ymm8
	addq	$256,%rbp
	decl	%ecx
	jnz	.Loop_16_xx_avx2

	movl	$1,%ecx
	leaq	512(%rsp),%rbx
	leaq	K256+128(%rip),%rbp
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
	vmovdqa	(%rbx),%ymm7
	vpxor	%ymm0,%ymm0,%ymm0
	vmovdqa	%ymm7,%ymm6
	vpcmpgtd	%ymm0,%ymm6,%ymm6
	vpaddd	%ymm6,%ymm7,%ymm7

	vmovdqu	0-128(%rdi),%ymm0
	vpand	%ymm6,%ymm8,%ymm8
	vmovdqu	32-128(%rdi),%ymm1
	vpand	%ymm6,%ymm9,%ymm9
	vmovdqu	64-128(%rdi),%ymm2
	vpand	%ymm6,%ymm10,%ymm10
	vmovdqu	96-128(%rdi),%ymm5
	vpand	%ymm6,%ymm11,%ymm11
	vpaddd	%ymm0,%ymm8,%ymm8
	vmovdqu	128-128(%rdi),%ymm0
	vpand	%ymm6,%ymm12,%ymm12
	vpaddd	%ymm1,%ymm9,%ymm9
	vmovdqu	160-128(%rdi),%ymm1
	vpand	%ymm6,%ymm13,%ymm13
	vpaddd	%ymm2,%ymm10,%ymm10
	vmovdqu	192-128(%rdi),%ymm2
	vpand	%ymm6,%ymm14,%ymm14
	vpaddd	%ymm5,%ymm11,%ymm11
	vmovdqu	224-128(%rdi),%ymm5
	vpand	%ymm6,%ymm15,%ymm15
	vpaddd	%ymm0,%ymm12,%ymm12
	vpaddd	%ymm1,%ymm13,%ymm13
	vmovdqu	%ymm8,0-128(%rdi)
	vpaddd	%ymm2,%ymm14,%ymm14
	vmovdqu	%ymm9,32-128(%rdi)
	vpaddd	%ymm5,%ymm15,%ymm15
	vmovdqu	%ymm10,64-128(%rdi)
	vmovdqu	%ymm11,96-128(%rdi)
	vmovdqu	%ymm12,128-128(%rdi)
	vmovdqu	%ymm13,160-128(%rdi)
	vmovdqu	%ymm14,192-128(%rdi)
	vmovdqu	%ymm15,224-128(%rdi)

	vmovdqu	%ymm7,(%rbx)
	leaq	256+128(%rsp),%rbx
	vmovdqu	.Lpbswap(%rip),%ymm6
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
.size	sha256_multi_block_avx2,.-sha256_multi_block_avx2
.align	256
K256:
.long	1116352408,1116352408,1116352408,1116352408
.long	1116352408,1116352408,1116352408,1116352408
.long	1899447441,1899447441,1899447441,1899447441
.long	1899447441,1899447441,1899447441,1899447441
.long	3049323471,3049323471,3049323471,3049323471
.long	3049323471,3049323471,3049323471,3049323471
.long	3921009573,3921009573,3921009573,3921009573
.long	3921009573,3921009573,3921009573,3921009573
.long	961987163,961987163,961987163,961987163
.long	961987163,961987163,961987163,961987163
.long	1508970993,1508970993,1508970993,1508970993
.long	1508970993,1508970993,1508970993,1508970993
.long	2453635748,2453635748,2453635748,2453635748
.long	2453635748,2453635748,2453635748,2453635748
.long	2870763221,2870763221,2870763221,2870763221
.long	2870763221,2870763221,2870763221,2870763221
.long	3624381080,3624381080,3624381080,3624381080
.long	3624381080,3624381080,3624381080,3624381080
.long	310598401,310598401,310598401,310598401
.long	310598401,310598401,310598401,310598401
.long	607225278,607225278,607225278,607225278
.long	607225278,607225278,607225278,607225278
.long	1426881987,1426881987,1426881987,1426881987
.long	1426881987,1426881987,1426881987,1426881987
.long	1925078388,1925078388,1925078388,1925078388
.long	1925078388,1925078388,1925078388,1925078388
.long	2162078206,2162078206,2162078206,2162078206
.long	2162078206,2162078206,2162078206,2162078206
.long	2614888103,2614888103,2614888103,2614888103
.long	2614888103,2614888103,2614888103,2614888103
.long	3248222580,3248222580,3248222580,3248222580
.long	3248222580,3248222580,3248222580,3248222580
.long	3835390401,3835390401,3835390401,3835390401
.long	3835390401,3835390401,3835390401,3835390401
.long	4022224774,4022224774,4022224774,4022224774
.long	4022224774,4022224774,4022224774,4022224774
.long	264347078,264347078,264347078,264347078
.long	264347078,264347078,264347078,264347078
.long	604807628,604807628,604807628,604807628
.long	604807628,604807628,604807628,604807628
.long	770255983,770255983,770255983,770255983
.long	770255983,770255983,770255983,770255983
.long	1249150122,1249150122,1249150122,1249150122
.long	1249150122,1249150122,1249150122,1249150122
.long	1555081692,1555081692,1555081692,1555081692
.long	1555081692,1555081692,1555081692,1555081692
.long	1996064986,1996064986,1996064986,1996064986
.long	1996064986,1996064986,1996064986,1996064986
.long	2554220882,2554220882,2554220882,2554220882
.long	2554220882,2554220882,2554220882,2554220882
.long	2821834349,2821834349,2821834349,2821834349
.long	2821834349,2821834349,2821834349,2821834349
.long	2952996808,2952996808,2952996808,2952996808
.long	2952996808,2952996808,2952996808,2952996808
.long	3210313671,3210313671,3210313671,3210313671
.long	3210313671,3210313671,3210313671,3210313671
.long	3336571891,3336571891,3336571891,3336571891
.long	3336571891,3336571891,3336571891,3336571891
.long	3584528711,3584528711,3584528711,3584528711
.long	3584528711,3584528711,3584528711,3584528711
.long	113926993,113926993,113926993,113926993
.long	113926993,113926993,113926993,113926993
.long	338241895,338241895,338241895,338241895
.long	338241895,338241895,338241895,338241895
.long	666307205,666307205,666307205,666307205
.long	666307205,666307205,666307205,666307205
.long	773529912,773529912,773529912,773529912
.long	773529912,773529912,773529912,773529912
.long	1294757372,1294757372,1294757372,1294757372
.long	1294757372,1294757372,1294757372,1294757372
.long	1396182291,1396182291,1396182291,1396182291
.long	1396182291,1396182291,1396182291,1396182291
.long	1695183700,1695183700,1695183700,1695183700
.long	1695183700,1695183700,1695183700,1695183700
.long	1986661051,1986661051,1986661051,1986661051
.long	1986661051,1986661051,1986661051,1986661051
.long	2177026350,2177026350,2177026350,2177026350
.long	2177026350,2177026350,2177026350,2177026350
.long	2456956037,2456956037,2456956037,2456956037
.long	2456956037,2456956037,2456956037,2456956037
.long	2730485921,2730485921,2730485921,2730485921
.long	2730485921,2730485921,2730485921,2730485921
.long	2820302411,2820302411,2820302411,2820302411
.long	2820302411,2820302411,2820302411,2820302411
.long	3259730800,3259730800,3259730800,3259730800
.long	3259730800,3259730800,3259730800,3259730800
.long	3345764771,3345764771,3345764771,3345764771
.long	3345764771,3345764771,3345764771,3345764771
.long	3516065817,3516065817,3516065817,3516065817
.long	3516065817,3516065817,3516065817,3516065817
.long	3600352804,3600352804,3600352804,3600352804
.long	3600352804,3600352804,3600352804,3600352804
.long	4094571909,4094571909,4094571909,4094571909
.long	4094571909,4094571909,4094571909,4094571909
.long	275423344,275423344,275423344,275423344
.long	275423344,275423344,275423344,275423344
.long	430227734,430227734,430227734,430227734
.long	430227734,430227734,430227734,430227734
.long	506948616,506948616,506948616,506948616
.long	506948616,506948616,506948616,506948616
.long	659060556,659060556,659060556,659060556
.long	659060556,659060556,659060556,659060556
.long	883997877,883997877,883997877,883997877
.long	883997877,883997877,883997877,883997877
.long	958139571,958139571,958139571,958139571
.long	958139571,958139571,958139571,958139571
.long	1322822218,1322822218,1322822218,1322822218
.long	1322822218,1322822218,1322822218,1322822218
.long	1537002063,1537002063,1537002063,1537002063
.long	1537002063,1537002063,1537002063,1537002063
.long	1747873779,1747873779,1747873779,1747873779
.long	1747873779,1747873779,1747873779,1747873779
.long	1955562222,1955562222,1955562222,1955562222
.long	1955562222,1955562222,1955562222,1955562222
.long	2024104815,2024104815,2024104815,2024104815
.long	2024104815,2024104815,2024104815,2024104815
.long	2227730452,2227730452,2227730452,2227730452
.long	2227730452,2227730452,2227730452,2227730452
.long	2361852424,2361852424,2361852424,2361852424
.long	2361852424,2361852424,2361852424,2361852424
.long	2428436474,2428436474,2428436474,2428436474
.long	2428436474,2428436474,2428436474,2428436474
.long	2756734187,2756734187,2756734187,2756734187
.long	2756734187,2756734187,2756734187,2756734187
.long	3204031479,3204031479,3204031479,3204031479
.long	3204031479,3204031479,3204031479,3204031479
.long	3329325298,3329325298,3329325298,3329325298
.long	3329325298,3329325298,3329325298,3329325298
.Lpbswap:
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
K256_shaext:
.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
.byte	83,72,65,50,53,54,32,109,117,108,116,105,45,98,108,111,99,107,32,116,114,97,110,115,102,111,114,109,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
