/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from x86_64-mont5.pl. */
.text	



.globl	bn_mul_mont_gather5
.type	bn_mul_mont_gather5,@function
.align	64
bn_mul_mont_gather5:
.cfi_startproc	
	movl	%r9d,%r9d
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	testl	$7,%r9d
	jnz	.Lmul_enter
	movl	OPENSSL_ia32cap_P+8(%rip),%r11d
	jmp	.Lmul4x_enter

.align	16
.Lmul_enter:
	movd	8(%rsp),%xmm5
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

	negq	%r9
	movq	%rsp,%r11
	leaq	-280(%rsp,%r9,8),%r10
	negq	%r9
	andq	$-1024,%r10









	subq	%r10,%r11
	andq	$-4096,%r11
	leaq	(%r10,%r11,1),%rsp
	movq	(%rsp),%r11
	cmpq	%r10,%rsp
	ja	.Lmul_page_walk
	jmp	.Lmul_page_walk_done

.Lmul_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r11
	cmpq	%r10,%rsp
	ja	.Lmul_page_walk
.Lmul_page_walk_done:

	leaq	.Linc(%rip),%r10
	movq	%rax,8(%rsp,%r9,8)
.cfi_escape	0x0f,0x0a,0x77,0x08,0x79,0x00,0x38,0x1e,0x22,0x06,0x23,0x08
.Lmul_body:

	leaq	128(%rdx),%r12
	movdqa	0(%r10),%xmm0
	movdqa	16(%r10),%xmm1
	leaq	24-112(%rsp,%r9,8),%r10
	andq	$-16,%r10

	pshufd	$0,%xmm5,%xmm5
	movdqa	%xmm1,%xmm4
	movdqa	%xmm1,%xmm2
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
.byte	0x67
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,112(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,128(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,144(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,160(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,176(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,192(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,208(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,224(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,240(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,256(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,272(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,288(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,304(%r10)

	paddd	%xmm2,%xmm3
.byte	0x67
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,320(%r10)

	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,336(%r10)
	pand	64(%r12),%xmm0

	pand	80(%r12),%xmm1
	pand	96(%r12),%xmm2
	movdqa	%xmm3,352(%r10)
	pand	112(%r12),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	-128(%r12),%xmm4
	movdqa	-112(%r12),%xmm5
	movdqa	-96(%r12),%xmm2
	pand	112(%r10),%xmm4
	movdqa	-80(%r12),%xmm3
	pand	128(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	144(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	160(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	-64(%r12),%xmm4
	movdqa	-48(%r12),%xmm5
	movdqa	-32(%r12),%xmm2
	pand	176(%r10),%xmm4
	movdqa	-16(%r12),%xmm3
	pand	192(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	208(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	224(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	0(%r12),%xmm4
	movdqa	16(%r12),%xmm5
	movdqa	32(%r12),%xmm2
	pand	240(%r10),%xmm4
	movdqa	48(%r12),%xmm3
	pand	256(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	272(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	288(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	por	%xmm1,%xmm0
	pshufd	$0x4e,%xmm0,%xmm1
	por	%xmm1,%xmm0
	leaq	256(%r12),%r12
.byte	102,72,15,126,195

	movq	(%r8),%r8
	movq	(%rsi),%rax

	xorq	%r14,%r14
	xorq	%r15,%r15

	movq	%r8,%rbp
	mulq	%rbx
	movq	%rax,%r10
	movq	(%rcx),%rax

	imulq	%r10,%rbp
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r10
	movq	8(%rsi),%rax
	adcq	$0,%rdx
	movq	%rdx,%r13

	leaq	1(%r15),%r15
	jmp	.L1st_enter

.align	16
.L1st:
	addq	%rax,%r13
	movq	(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r11,%r13
	movq	%r10,%r11
	adcq	$0,%rdx
	movq	%r13,-16(%rsp,%r15,8)
	movq	%rdx,%r13

.L1st_enter:
	mulq	%rbx
	addq	%rax,%r11
	movq	(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	leaq	1(%r15),%r15
	movq	%rdx,%r10

	mulq	%rbp
	cmpq	%r9,%r15
	jne	.L1st


	addq	%rax,%r13
	adcq	$0,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx
	movq	%r13,-16(%rsp,%r9,8)
	movq	%rdx,%r13
	movq	%r10,%r11

	xorq	%rdx,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx
	movq	%r13,-8(%rsp,%r9,8)
	movq	%rdx,(%rsp,%r9,8)

	leaq	1(%r14),%r14
	jmp	.Louter
.align	16
.Louter:
	leaq	24+128(%rsp,%r9,8),%rdx
	andq	$-16,%rdx
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	movdqa	-128(%r12),%xmm0
	movdqa	-112(%r12),%xmm1
	movdqa	-96(%r12),%xmm2
	movdqa	-80(%r12),%xmm3
	pand	-128(%rdx),%xmm0
	pand	-112(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	-96(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	-80(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	-64(%r12),%xmm0
	movdqa	-48(%r12),%xmm1
	movdqa	-32(%r12),%xmm2
	movdqa	-16(%r12),%xmm3
	pand	-64(%rdx),%xmm0
	pand	-48(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	-32(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	-16(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	0(%r12),%xmm0
	movdqa	16(%r12),%xmm1
	movdqa	32(%r12),%xmm2
	movdqa	48(%r12),%xmm3
	pand	0(%rdx),%xmm0
	pand	16(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	32(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	48(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	64(%r12),%xmm0
	movdqa	80(%r12),%xmm1
	movdqa	96(%r12),%xmm2
	movdqa	112(%r12),%xmm3
	pand	64(%rdx),%xmm0
	pand	80(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	96(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	112(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	por	%xmm5,%xmm4
	pshufd	$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	leaq	256(%r12),%r12

	movq	(%rsi),%rax
.byte	102,72,15,126,195

	xorq	%r15,%r15
	movq	%r8,%rbp
	movq	(%rsp),%r10

	mulq	%rbx
	addq	%rax,%r10
	movq	(%rcx),%rax
	adcq	$0,%rdx

	imulq	%r10,%rbp
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r10
	movq	8(%rsi),%rax
	adcq	$0,%rdx
	movq	8(%rsp),%r10
	movq	%rdx,%r13

	leaq	1(%r15),%r15
	jmp	.Linner_enter

.align	16
.Linner:
	addq	%rax,%r13
	movq	(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	movq	(%rsp,%r15,8),%r10
	adcq	$0,%rdx
	movq	%r13,-16(%rsp,%r15,8)
	movq	%rdx,%r13

.Linner_enter:
	mulq	%rbx
	addq	%rax,%r11
	movq	(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r11,%r10
	movq	%rdx,%r11
	adcq	$0,%r11
	leaq	1(%r15),%r15

	mulq	%rbp
	cmpq	%r9,%r15
	jne	.Linner

	addq	%rax,%r13
	adcq	$0,%rdx
	addq	%r10,%r13
	movq	(%rsp,%r9,8),%r10
	adcq	$0,%rdx
	movq	%r13,-16(%rsp,%r9,8)
	movq	%rdx,%r13

	xorq	%rdx,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-8(%rsp,%r9,8)
	movq	%rdx,(%rsp,%r9,8)

	leaq	1(%r14),%r14
	cmpq	%r9,%r14
	jb	.Louter

	xorq	%r14,%r14
	movq	(%rsp),%rax
	leaq	(%rsp),%rsi
	movq	%r9,%r15
	jmp	.Lsub
.align	16
.Lsub:	sbbq	(%rcx,%r14,8),%rax
	movq	%rax,(%rdi,%r14,8)
	movq	8(%rsi,%r14,8),%rax
	leaq	1(%r14),%r14
	decq	%r15
	jnz	.Lsub

	sbbq	$0,%rax
	movq	$-1,%rbx
	xorq	%rax,%rbx
	xorq	%r14,%r14
	movq	%r9,%r15

.Lcopy:
	movq	(%rdi,%r14,8),%rcx
	movq	(%rsp,%r14,8),%rdx
	andq	%rbx,%rcx
	andq	%rax,%rdx
	movq	%r14,(%rsp,%r14,8)
	orq	%rcx,%rdx
	movq	%rdx,(%rdi,%r14,8)
	leaq	1(%r14),%r14
	subq	$1,%r15
	jnz	.Lcopy

	movq	8(%rsp,%r9,8),%rsi
.cfi_def_cfa	%rsi,8
	movq	$1,%rax

	movq	-48(%rsi),%r15
.cfi_restore	%r15
	movq	-40(%rsi),%r14
.cfi_restore	%r14
	movq	-32(%rsi),%r13
.cfi_restore	%r13
	movq	-24(%rsi),%r12
.cfi_restore	%r12
	movq	-16(%rsi),%rbp
.cfi_restore	%rbp
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lmul_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_mul_mont_gather5,.-bn_mul_mont_gather5
.type	bn_mul4x_mont_gather5,@function
.align	32
bn_mul4x_mont_gather5:
.cfi_startproc	
.byte	0x67
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
.Lmul4x_enter:
	andl	$0x80108,%r11d
	cmpl	$0x80108,%r11d
	je	.Lmulx4x_enter
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
.Lmul4x_prologue:

.byte	0x67
	shll	$3,%r9d
	leaq	(%r9,%r9,2),%r10
	negq	%r9










	leaq	-320(%rsp,%r9,2),%r11
	movq	%rsp,%rbp
	subq	%rdi,%r11
	andq	$4095,%r11
	cmpq	%r11,%r10
	jb	.Lmul4xsp_alt
	subq	%r11,%rbp
	leaq	-320(%rbp,%r9,2),%rbp
	jmp	.Lmul4xsp_done

.align	32
.Lmul4xsp_alt:
	leaq	4096-320(,%r9,2),%r10
	leaq	-320(%rbp,%r9,2),%rbp
	subq	%r10,%r11
	movq	$0,%r10
	cmovcq	%r10,%r11
	subq	%r11,%rbp
.Lmul4xsp_done:
	andq	$-64,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lmul4x_page_walk
	jmp	.Lmul4x_page_walk_done

.Lmul4x_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lmul4x_page_walk
.Lmul4x_page_walk_done:

	negq	%r9

	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
.Lmul4x_body:

	call	mul4x_internal

	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	movq	$1,%rax

	movq	-48(%rsi),%r15
.cfi_restore	%r15
	movq	-40(%rsi),%r14
.cfi_restore	%r14
	movq	-32(%rsi),%r13
.cfi_restore	%r13
	movq	-24(%rsi),%r12
.cfi_restore	%r12
	movq	-16(%rsi),%rbp
.cfi_restore	%rbp
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lmul4x_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_mul4x_mont_gather5,.-bn_mul4x_mont_gather5

.type	mul4x_internal,@function
.align	32
mul4x_internal:
	shlq	$5,%r9
	movd	8(%rax),%xmm5
	leaq	.Linc(%rip),%rax
	leaq	128(%rdx,%r9,1),%r13
	shrq	$5,%r9
	movdqa	0(%rax),%xmm0
	movdqa	16(%rax),%xmm1
	leaq	88-112(%rsp,%r9,1),%r10
	leaq	128(%rdx),%r12

	pshufd	$0,%xmm5,%xmm5
	movdqa	%xmm1,%xmm4
.byte	0x67,0x67
	movdqa	%xmm1,%xmm2
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
.byte	0x67
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,112(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,128(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,144(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,160(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,176(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,192(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,208(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,224(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,240(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,256(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,272(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,288(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,304(%r10)

	paddd	%xmm2,%xmm3
.byte	0x67
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,320(%r10)

	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,336(%r10)
	pand	64(%r12),%xmm0

	pand	80(%r12),%xmm1
	pand	96(%r12),%xmm2
	movdqa	%xmm3,352(%r10)
	pand	112(%r12),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	-128(%r12),%xmm4
	movdqa	-112(%r12),%xmm5
	movdqa	-96(%r12),%xmm2
	pand	112(%r10),%xmm4
	movdqa	-80(%r12),%xmm3
	pand	128(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	144(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	160(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	-64(%r12),%xmm4
	movdqa	-48(%r12),%xmm5
	movdqa	-32(%r12),%xmm2
	pand	176(%r10),%xmm4
	movdqa	-16(%r12),%xmm3
	pand	192(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	208(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	224(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	0(%r12),%xmm4
	movdqa	16(%r12),%xmm5
	movdqa	32(%r12),%xmm2
	pand	240(%r10),%xmm4
	movdqa	48(%r12),%xmm3
	pand	256(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	272(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	288(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	por	%xmm1,%xmm0
	pshufd	$0x4e,%xmm0,%xmm1
	por	%xmm1,%xmm0
	leaq	256(%r12),%r12
.byte	102,72,15,126,195

	movq	%r13,16+8(%rsp)
	movq	%rdi,56+8(%rsp)

	movq	(%r8),%r8
	movq	(%rsi),%rax
	leaq	(%rsi,%r9,1),%rsi
	negq	%r9

	movq	%r8,%rbp
	mulq	%rbx
	movq	%rax,%r10
	movq	(%rcx),%rax

	imulq	%r10,%rbp
	leaq	64+8(%rsp),%r14
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r10
	movq	8(%rsi,%r9,1),%rax
	adcq	$0,%rdx
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	16(%rsi,%r9,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	leaq	32(%r9),%r15
	leaq	32(%rcx),%rcx
	adcq	$0,%rdx
	movq	%rdi,(%r14)
	movq	%rdx,%r13
	jmp	.L1st4x

.align	32
.L1st4x:
	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx),%rax
	leaq	32(%r14),%r14
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-24(%r14)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-16(%r14)
	movq	%rdx,%r13

	mulq	%rbx
	addq	%rax,%r10
	movq	0(%rcx),%rax
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	8(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-8(%r14)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	16(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	leaq	32(%rcx),%rcx
	adcq	$0,%rdx
	movq	%rdi,(%r14)
	movq	%rdx,%r13

	addq	$32,%r15
	jnz	.L1st4x

	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx),%rax
	leaq	32(%r14),%r14
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-24(%r14)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi,%r9,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-16(%r14)
	movq	%rdx,%r13

	leaq	(%rcx,%r9,1),%rcx

	xorq	%rdi,%rdi
	addq	%r10,%r13
	adcq	$0,%rdi
	movq	%r13,-8(%r14)

	jmp	.Louter4x

.align	32
.Louter4x:
	leaq	16+128(%r14),%rdx
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	movdqa	-128(%r12),%xmm0
	movdqa	-112(%r12),%xmm1
	movdqa	-96(%r12),%xmm2
	movdqa	-80(%r12),%xmm3
	pand	-128(%rdx),%xmm0
	pand	-112(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	-96(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	-80(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	-64(%r12),%xmm0
	movdqa	-48(%r12),%xmm1
	movdqa	-32(%r12),%xmm2
	movdqa	-16(%r12),%xmm3
	pand	-64(%rdx),%xmm0
	pand	-48(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	-32(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	-16(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	0(%r12),%xmm0
	movdqa	16(%r12),%xmm1
	movdqa	32(%r12),%xmm2
	movdqa	48(%r12),%xmm3
	pand	0(%rdx),%xmm0
	pand	16(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	32(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	48(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	64(%r12),%xmm0
	movdqa	80(%r12),%xmm1
	movdqa	96(%r12),%xmm2
	movdqa	112(%r12),%xmm3
	pand	64(%rdx),%xmm0
	pand	80(%rdx),%xmm1
	por	%xmm0,%xmm4
	pand	96(%rdx),%xmm2
	por	%xmm1,%xmm5
	pand	112(%rdx),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	por	%xmm5,%xmm4
	pshufd	$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	leaq	256(%r12),%r12
.byte	102,72,15,126,195

	movq	(%r14,%r9,1),%r10
	movq	%r8,%rbp
	mulq	%rbx
	addq	%rax,%r10
	movq	(%rcx),%rax
	adcq	$0,%rdx

	imulq	%r10,%rbp
	movq	%rdx,%r11
	movq	%rdi,(%r14)

	leaq	(%r14,%r9,1),%r14

	mulq	%rbp
	addq	%rax,%r10
	movq	8(%rsi,%r9,1),%rax
	adcq	$0,%rdx
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx),%rax
	adcq	$0,%rdx
	addq	8(%r14),%r11
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	16(%rsi,%r9,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	leaq	32(%r9),%r15
	leaq	32(%rcx),%rcx
	adcq	$0,%rdx
	movq	%rdx,%r13
	jmp	.Linner4x

.align	32
.Linner4x:
	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx),%rax
	adcq	$0,%rdx
	addq	16(%r14),%r10
	leaq	32(%r14),%r14
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%rdi,-32(%r14)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx),%rax
	adcq	$0,%rdx
	addq	-8(%r14),%r11
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%r13,-24(%r14)
	movq	%rdx,%r13

	mulq	%rbx
	addq	%rax,%r10
	movq	0(%rcx),%rax
	adcq	$0,%rdx
	addq	(%r14),%r10
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	8(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%rdi,-16(%r14)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx),%rax
	adcq	$0,%rdx
	addq	8(%r14),%r11
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	16(%rsi,%r15,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	leaq	32(%rcx),%rcx
	adcq	$0,%rdx
	movq	%r13,-8(%r14)
	movq	%rdx,%r13

	addq	$32,%r15
	jnz	.Linner4x

	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx),%rax
	adcq	$0,%rdx
	addq	16(%r14),%r10
	leaq	32(%r14),%r14
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%rdi,-32(%r14)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	%rbp,%rax
	movq	-8(%rcx),%rbp
	adcq	$0,%rdx
	addq	-8(%r14),%r11
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi,%r9,1),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%r13,-24(%r14)
	movq	%rdx,%r13

	movq	%rdi,-16(%r14)
	leaq	(%rcx,%r9,1),%rcx

	xorq	%rdi,%rdi
	addq	%r10,%r13
	adcq	$0,%rdi
	addq	(%r14),%r13
	adcq	$0,%rdi
	movq	%r13,-8(%r14)

	cmpq	16+8(%rsp),%r12
	jb	.Louter4x
	xorq	%rax,%rax
	subq	%r13,%rbp
	adcq	%r15,%r15
	orq	%r15,%rdi
	subq	%rdi,%rax
	leaq	(%r14,%r9,1),%rbx
	movq	(%rcx),%r12
	leaq	(%rcx),%rbp
	movq	%r9,%rcx
	sarq	$3+2,%rcx
	movq	56+8(%rsp),%rdi
	decq	%r12
	xorq	%r10,%r10
	movq	8(%rbp),%r13
	movq	16(%rbp),%r14
	movq	24(%rbp),%r15
	jmp	.Lsqr4x_sub_entry
.size	mul4x_internal,.-mul4x_internal
.globl	bn_power5
.type	bn_power5,@function
.align	32
bn_power5:
.cfi_startproc	
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	movl	OPENSSL_ia32cap_P+8(%rip),%r11d
	andl	$0x80108,%r11d
	cmpl	$0x80108,%r11d
	je	.Lpowerx5_enter
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
.Lpower5_prologue:

	shll	$3,%r9d
	leal	(%r9,%r9,2),%r10d
	negq	%r9
	movq	(%r8),%r8








	leaq	-320(%rsp,%r9,2),%r11
	movq	%rsp,%rbp
	subq	%rdi,%r11
	andq	$4095,%r11
	cmpq	%r11,%r10
	jb	.Lpwr_sp_alt
	subq	%r11,%rbp
	leaq	-320(%rbp,%r9,2),%rbp
	jmp	.Lpwr_sp_done

.align	32
.Lpwr_sp_alt:
	leaq	4096-320(,%r9,2),%r10
	leaq	-320(%rbp,%r9,2),%rbp
	subq	%r10,%r11
	movq	$0,%r10
	cmovcq	%r10,%r11
	subq	%r11,%rbp
.Lpwr_sp_done:
	andq	$-64,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lpwr_page_walk
	jmp	.Lpwr_page_walk_done

.Lpwr_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lpwr_page_walk
.Lpwr_page_walk_done:

	movq	%r9,%r10
	negq	%r9










	movq	%r8,32(%rsp)
	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
.Lpower5_body:
.byte	102,72,15,110,207
.byte	102,72,15,110,209
.byte	102,73,15,110,218
.byte	102,72,15,110,226

	call	__bn_sqr8x_internal
	call	__bn_post4x_internal
	call	__bn_sqr8x_internal
	call	__bn_post4x_internal
	call	__bn_sqr8x_internal
	call	__bn_post4x_internal
	call	__bn_sqr8x_internal
	call	__bn_post4x_internal
	call	__bn_sqr8x_internal
	call	__bn_post4x_internal

.byte	102,72,15,126,209
.byte	102,72,15,126,226
	movq	%rsi,%rdi
	movq	40(%rsp),%rax
	leaq	32(%rsp),%r8

	call	mul4x_internal

	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	movq	$1,%rax
	movq	-48(%rsi),%r15
.cfi_restore	%r15
	movq	-40(%rsi),%r14
.cfi_restore	%r14
	movq	-32(%rsi),%r13
.cfi_restore	%r13
	movq	-24(%rsi),%r12
.cfi_restore	%r12
	movq	-16(%rsi),%rbp
.cfi_restore	%rbp
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lpower5_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_power5,.-bn_power5

.globl	bn_sqr8x_internal
.hidden	bn_sqr8x_internal
.type	bn_sqr8x_internal,@function
.align	32
bn_sqr8x_internal:
__bn_sqr8x_internal:









































































	leaq	32(%r10),%rbp
	leaq	(%rsi,%r9,1),%rsi

	movq	%r9,%rcx


	movq	-32(%rsi,%rbp,1),%r14
	leaq	48+8(%rsp,%r9,2),%rdi
	movq	-24(%rsi,%rbp,1),%rax
	leaq	-32(%rdi,%rbp,1),%rdi
	movq	-16(%rsi,%rbp,1),%rbx
	movq	%rax,%r15

	mulq	%r14
	movq	%rax,%r10
	movq	%rbx,%rax
	movq	%rdx,%r11
	movq	%r10,-24(%rdi,%rbp,1)

	mulq	%r14
	addq	%rax,%r11
	movq	%rbx,%rax
	adcq	$0,%rdx
	movq	%r11,-16(%rdi,%rbp,1)
	movq	%rdx,%r10


	movq	-8(%rsi,%rbp,1),%rbx
	mulq	%r15
	movq	%rax,%r12
	movq	%rbx,%rax
	movq	%rdx,%r13

	leaq	(%rbp),%rcx
	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	movq	%rdx,%r11
	adcq	$0,%r11
	addq	%r12,%r10
	adcq	$0,%r11
	movq	%r10,-8(%rdi,%rcx,1)
	jmp	.Lsqr4x_1st

.align	32
.Lsqr4x_1st:
	movq	(%rsi,%rcx,1),%rbx
	mulq	%r15
	addq	%rax,%r13
	movq	%rbx,%rax
	movq	%rdx,%r12
	adcq	$0,%r12

	mulq	%r14
	addq	%rax,%r11
	movq	%rbx,%rax
	movq	8(%rsi,%rcx,1),%rbx
	movq	%rdx,%r10
	adcq	$0,%r10
	addq	%r13,%r11
	adcq	$0,%r10


	mulq	%r15
	addq	%rax,%r12
	movq	%rbx,%rax
	movq	%r11,(%rdi,%rcx,1)
	movq	%rdx,%r13
	adcq	$0,%r13

	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	movq	16(%rsi,%rcx,1),%rbx
	movq	%rdx,%r11
	adcq	$0,%r11
	addq	%r12,%r10
	adcq	$0,%r11

	mulq	%r15
	addq	%rax,%r13
	movq	%rbx,%rax
	movq	%r10,8(%rdi,%rcx,1)
	movq	%rdx,%r12
	adcq	$0,%r12

	mulq	%r14
	addq	%rax,%r11
	movq	%rbx,%rax
	movq	24(%rsi,%rcx,1),%rbx
	movq	%rdx,%r10
	adcq	$0,%r10
	addq	%r13,%r11
	adcq	$0,%r10


	mulq	%r15
	addq	%rax,%r12
	movq	%rbx,%rax
	movq	%r11,16(%rdi,%rcx,1)
	movq	%rdx,%r13
	adcq	$0,%r13
	leaq	32(%rcx),%rcx

	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	movq	%rdx,%r11
	adcq	$0,%r11
	addq	%r12,%r10
	adcq	$0,%r11
	movq	%r10,-8(%rdi,%rcx,1)

	cmpq	$0,%rcx
	jne	.Lsqr4x_1st

	mulq	%r15
	addq	%rax,%r13
	leaq	16(%rbp),%rbp
	adcq	$0,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx

	movq	%r13,(%rdi)
	movq	%rdx,%r12
	movq	%rdx,8(%rdi)
	jmp	.Lsqr4x_outer

.align	32
.Lsqr4x_outer:
	movq	-32(%rsi,%rbp,1),%r14
	leaq	48+8(%rsp,%r9,2),%rdi
	movq	-24(%rsi,%rbp,1),%rax
	leaq	-32(%rdi,%rbp,1),%rdi
	movq	-16(%rsi,%rbp,1),%rbx
	movq	%rax,%r15

	mulq	%r14
	movq	-24(%rdi,%rbp,1),%r10
	addq	%rax,%r10
	movq	%rbx,%rax
	adcq	$0,%rdx
	movq	%r10,-24(%rdi,%rbp,1)
	movq	%rdx,%r11

	mulq	%r14
	addq	%rax,%r11
	movq	%rbx,%rax
	adcq	$0,%rdx
	addq	-16(%rdi,%rbp,1),%r11
	movq	%rdx,%r10
	adcq	$0,%r10
	movq	%r11,-16(%rdi,%rbp,1)

	xorq	%r12,%r12

	movq	-8(%rsi,%rbp,1),%rbx
	mulq	%r15
	addq	%rax,%r12
	movq	%rbx,%rax
	adcq	$0,%rdx
	addq	-8(%rdi,%rbp,1),%r12
	movq	%rdx,%r13
	adcq	$0,%r13

	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	adcq	$0,%rdx
	addq	%r12,%r10
	movq	%rdx,%r11
	adcq	$0,%r11
	movq	%r10,-8(%rdi,%rbp,1)

	leaq	(%rbp),%rcx
	jmp	.Lsqr4x_inner

.align	32
.Lsqr4x_inner:
	movq	(%rsi,%rcx,1),%rbx
	mulq	%r15
	addq	%rax,%r13
	movq	%rbx,%rax
	movq	%rdx,%r12
	adcq	$0,%r12
	addq	(%rdi,%rcx,1),%r13
	adcq	$0,%r12

.byte	0x67
	mulq	%r14
	addq	%rax,%r11
	movq	%rbx,%rax
	movq	8(%rsi,%rcx,1),%rbx
	movq	%rdx,%r10
	adcq	$0,%r10
	addq	%r13,%r11
	adcq	$0,%r10

	mulq	%r15
	addq	%rax,%r12
	movq	%r11,(%rdi,%rcx,1)
	movq	%rbx,%rax
	movq	%rdx,%r13
	adcq	$0,%r13
	addq	8(%rdi,%rcx,1),%r12
	leaq	16(%rcx),%rcx
	adcq	$0,%r13

	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	adcq	$0,%rdx
	addq	%r12,%r10
	movq	%rdx,%r11
	adcq	$0,%r11
	movq	%r10,-8(%rdi,%rcx,1)

	cmpq	$0,%rcx
	jne	.Lsqr4x_inner

.byte	0x67
	mulq	%r15
	addq	%rax,%r13
	adcq	$0,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx

	movq	%r13,(%rdi)
	movq	%rdx,%r12
	movq	%rdx,8(%rdi)

	addq	$16,%rbp
	jnz	.Lsqr4x_outer


	movq	-32(%rsi),%r14
	leaq	48+8(%rsp,%r9,2),%rdi
	movq	-24(%rsi),%rax
	leaq	-32(%rdi,%rbp,1),%rdi
	movq	-16(%rsi),%rbx
	movq	%rax,%r15

	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	movq	%rdx,%r11
	adcq	$0,%r11

	mulq	%r14
	addq	%rax,%r11
	movq	%rbx,%rax
	movq	%r10,-24(%rdi)
	movq	%rdx,%r10
	adcq	$0,%r10
	addq	%r13,%r11
	movq	-8(%rsi),%rbx
	adcq	$0,%r10

	mulq	%r15
	addq	%rax,%r12
	movq	%rbx,%rax
	movq	%r11,-16(%rdi)
	movq	%rdx,%r13
	adcq	$0,%r13

	mulq	%r14
	addq	%rax,%r10
	movq	%rbx,%rax
	movq	%rdx,%r11
	adcq	$0,%r11
	addq	%r12,%r10
	adcq	$0,%r11
	movq	%r10,-8(%rdi)

	mulq	%r15
	addq	%rax,%r13
	movq	-16(%rsi),%rax
	adcq	$0,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx

	movq	%r13,(%rdi)
	movq	%rdx,%r12
	movq	%rdx,8(%rdi)

	mulq	%rbx
	addq	$16,%rbp
	xorq	%r14,%r14
	subq	%r9,%rbp
	xorq	%r15,%r15

	addq	%r12,%rax
	adcq	$0,%rdx
	movq	%rax,8(%rdi)
	movq	%rdx,16(%rdi)
	movq	%r15,24(%rdi)

	movq	-16(%rsi,%rbp,1),%rax
	leaq	48+8(%rsp),%rdi
	xorq	%r10,%r10
	movq	8(%rdi),%r11

	leaq	(%r14,%r10,2),%r12
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r13
	shrq	$63,%r11
	orq	%r10,%r13
	movq	16(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	24(%rdi),%r11
	adcq	%rax,%r12
	movq	-8(%rsi,%rbp,1),%rax
	movq	%r12,(%rdi)
	adcq	%rdx,%r13

	leaq	(%r14,%r10,2),%rbx
	movq	%r13,8(%rdi)
	sbbq	%r15,%r15
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r8
	shrq	$63,%r11
	orq	%r10,%r8
	movq	32(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	40(%rdi),%r11
	adcq	%rax,%rbx
	movq	0(%rsi,%rbp,1),%rax
	movq	%rbx,16(%rdi)
	adcq	%rdx,%r8
	leaq	16(%rbp),%rbp
	movq	%r8,24(%rdi)
	sbbq	%r15,%r15
	leaq	64(%rdi),%rdi
	jmp	.Lsqr4x_shift_n_add

.align	32
.Lsqr4x_shift_n_add:
	leaq	(%r14,%r10,2),%r12
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r13
	shrq	$63,%r11
	orq	%r10,%r13
	movq	-16(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	-8(%rdi),%r11
	adcq	%rax,%r12
	movq	-8(%rsi,%rbp,1),%rax
	movq	%r12,-32(%rdi)
	adcq	%rdx,%r13

	leaq	(%r14,%r10,2),%rbx
	movq	%r13,-24(%rdi)
	sbbq	%r15,%r15
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r8
	shrq	$63,%r11
	orq	%r10,%r8
	movq	0(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	8(%rdi),%r11
	adcq	%rax,%rbx
	movq	0(%rsi,%rbp,1),%rax
	movq	%rbx,-16(%rdi)
	adcq	%rdx,%r8

	leaq	(%r14,%r10,2),%r12
	movq	%r8,-8(%rdi)
	sbbq	%r15,%r15
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r13
	shrq	$63,%r11
	orq	%r10,%r13
	movq	16(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	24(%rdi),%r11
	adcq	%rax,%r12
	movq	8(%rsi,%rbp,1),%rax
	movq	%r12,0(%rdi)
	adcq	%rdx,%r13

	leaq	(%r14,%r10,2),%rbx
	movq	%r13,8(%rdi)
	sbbq	%r15,%r15
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r8
	shrq	$63,%r11
	orq	%r10,%r8
	movq	32(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	40(%rdi),%r11
	adcq	%rax,%rbx
	movq	16(%rsi,%rbp,1),%rax
	movq	%rbx,16(%rdi)
	adcq	%rdx,%r8
	movq	%r8,24(%rdi)
	sbbq	%r15,%r15
	leaq	64(%rdi),%rdi
	addq	$32,%rbp
	jnz	.Lsqr4x_shift_n_add

	leaq	(%r14,%r10,2),%r12
.byte	0x67
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r13
	shrq	$63,%r11
	orq	%r10,%r13
	movq	-16(%rdi),%r10
	movq	%r11,%r14
	mulq	%rax
	negq	%r15
	movq	-8(%rdi),%r11
	adcq	%rax,%r12
	movq	-8(%rsi),%rax
	movq	%r12,-32(%rdi)
	adcq	%rdx,%r13

	leaq	(%r14,%r10,2),%rbx
	movq	%r13,-24(%rdi)
	sbbq	%r15,%r15
	shrq	$63,%r10
	leaq	(%rcx,%r11,2),%r8
	shrq	$63,%r11
	orq	%r10,%r8
	mulq	%rax
	negq	%r15
	adcq	%rax,%rbx
	adcq	%rdx,%r8
	movq	%rbx,-16(%rdi)
	movq	%r8,-8(%rdi)
.byte	102,72,15,126,213
__bn_sqr8x_reduction:
	xorq	%rax,%rax
	leaq	(%r9,%rbp,1),%rcx
	leaq	48+8(%rsp,%r9,2),%rdx
	movq	%rcx,0+8(%rsp)
	leaq	48+8(%rsp,%r9,1),%rdi
	movq	%rdx,8+8(%rsp)
	negq	%r9
	jmp	.L8x_reduction_loop

.align	32
.L8x_reduction_loop:
	leaq	(%rdi,%r9,1),%rdi
.byte	0x66
	movq	0(%rdi),%rbx
	movq	8(%rdi),%r9
	movq	16(%rdi),%r10
	movq	24(%rdi),%r11
	movq	32(%rdi),%r12
	movq	40(%rdi),%r13
	movq	48(%rdi),%r14
	movq	56(%rdi),%r15
	movq	%rax,(%rdx)
	leaq	64(%rdi),%rdi

.byte	0x67
	movq	%rbx,%r8
	imulq	32+8(%rsp),%rbx
	movq	0(%rbp),%rax
	movl	$8,%ecx
	jmp	.L8x_reduce

.align	32
.L8x_reduce:
	mulq	%rbx
	movq	8(%rbp),%rax
	negq	%r8
	movq	%rdx,%r8
	adcq	$0,%r8

	mulq	%rbx
	addq	%rax,%r9
	movq	16(%rbp),%rax
	adcq	$0,%rdx
	addq	%r9,%r8
	movq	%rbx,48-8+8(%rsp,%rcx,8)
	movq	%rdx,%r9
	adcq	$0,%r9

	mulq	%rbx
	addq	%rax,%r10
	movq	24(%rbp),%rax
	adcq	$0,%rdx
	addq	%r10,%r9
	movq	32+8(%rsp),%rsi
	movq	%rdx,%r10
	adcq	$0,%r10

	mulq	%rbx
	addq	%rax,%r11
	movq	32(%rbp),%rax
	adcq	$0,%rdx
	imulq	%r8,%rsi
	addq	%r11,%r10
	movq	%rdx,%r11
	adcq	$0,%r11

	mulq	%rbx
	addq	%rax,%r12
	movq	40(%rbp),%rax
	adcq	$0,%rdx
	addq	%r12,%r11
	movq	%rdx,%r12
	adcq	$0,%r12

	mulq	%rbx
	addq	%rax,%r13
	movq	48(%rbp),%rax
	adcq	$0,%rdx
	addq	%r13,%r12
	movq	%rdx,%r13
	adcq	$0,%r13

	mulq	%rbx
	addq	%rax,%r14
	movq	56(%rbp),%rax
	adcq	$0,%rdx
	addq	%r14,%r13
	movq	%rdx,%r14
	adcq	$0,%r14

	mulq	%rbx
	movq	%rsi,%rbx
	addq	%rax,%r15
	movq	0(%rbp),%rax
	adcq	$0,%rdx
	addq	%r15,%r14
	movq	%rdx,%r15
	adcq	$0,%r15

	decl	%ecx
	jnz	.L8x_reduce

	leaq	64(%rbp),%rbp
	xorq	%rax,%rax
	movq	8+8(%rsp),%rdx
	cmpq	0+8(%rsp),%rbp
	jae	.L8x_no_tail

.byte	0x66
	addq	0(%rdi),%r8
	adcq	8(%rdi),%r9
	adcq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	sbbq	%rsi,%rsi

	movq	48+56+8(%rsp),%rbx
	movl	$8,%ecx
	movq	0(%rbp),%rax
	jmp	.L8x_tail

.align	32
.L8x_tail:
	mulq	%rbx
	addq	%rax,%r8
	movq	8(%rbp),%rax
	movq	%r8,(%rdi)
	movq	%rdx,%r8
	adcq	$0,%r8

	mulq	%rbx
	addq	%rax,%r9
	movq	16(%rbp),%rax
	adcq	$0,%rdx
	addq	%r9,%r8
	leaq	8(%rdi),%rdi
	movq	%rdx,%r9
	adcq	$0,%r9

	mulq	%rbx
	addq	%rax,%r10
	movq	24(%rbp),%rax
	adcq	$0,%rdx
	addq	%r10,%r9
	movq	%rdx,%r10
	adcq	$0,%r10

	mulq	%rbx
	addq	%rax,%r11
	movq	32(%rbp),%rax
	adcq	$0,%rdx
	addq	%r11,%r10
	movq	%rdx,%r11
	adcq	$0,%r11

	mulq	%rbx
	addq	%rax,%r12
	movq	40(%rbp),%rax
	adcq	$0,%rdx
	addq	%r12,%r11
	movq	%rdx,%r12
	adcq	$0,%r12

	mulq	%rbx
	addq	%rax,%r13
	movq	48(%rbp),%rax
	adcq	$0,%rdx
	addq	%r13,%r12
	movq	%rdx,%r13
	adcq	$0,%r13

	mulq	%rbx
	addq	%rax,%r14
	movq	56(%rbp),%rax
	adcq	$0,%rdx
	addq	%r14,%r13
	movq	%rdx,%r14
	adcq	$0,%r14

	mulq	%rbx
	movq	48-16+8(%rsp,%rcx,8),%rbx
	addq	%rax,%r15
	adcq	$0,%rdx
	addq	%r15,%r14
	movq	0(%rbp),%rax
	movq	%rdx,%r15
	adcq	$0,%r15

	decl	%ecx
	jnz	.L8x_tail

	leaq	64(%rbp),%rbp
	movq	8+8(%rsp),%rdx
	cmpq	0+8(%rsp),%rbp
	jae	.L8x_tail_done

	movq	48+56+8(%rsp),%rbx
	negq	%rsi
	movq	0(%rbp),%rax
	adcq	0(%rdi),%r8
	adcq	8(%rdi),%r9
	adcq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	sbbq	%rsi,%rsi

	movl	$8,%ecx
	jmp	.L8x_tail

.align	32
.L8x_tail_done:
	xorq	%rax,%rax
	addq	(%rdx),%r8
	adcq	$0,%r9
	adcq	$0,%r10
	adcq	$0,%r11
	adcq	$0,%r12
	adcq	$0,%r13
	adcq	$0,%r14
	adcq	$0,%r15
	adcq	$0,%rax

	negq	%rsi
.L8x_no_tail:
	adcq	0(%rdi),%r8
	adcq	8(%rdi),%r9
	adcq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	adcq	$0,%rax
	movq	-8(%rbp),%rcx
	xorq	%rsi,%rsi

.byte	102,72,15,126,213

	movq	%r8,0(%rdi)
	movq	%r9,8(%rdi)
.byte	102,73,15,126,217
	movq	%r10,16(%rdi)
	movq	%r11,24(%rdi)
	movq	%r12,32(%rdi)
	movq	%r13,40(%rdi)
	movq	%r14,48(%rdi)
	movq	%r15,56(%rdi)
	leaq	64(%rdi),%rdi

	cmpq	%rdx,%rdi
	jb	.L8x_reduction_loop
	.byte	0xf3,0xc3
.size	bn_sqr8x_internal,.-bn_sqr8x_internal
.type	__bn_post4x_internal,@function
.align	32
__bn_post4x_internal:
	movq	0(%rbp),%r12
	leaq	(%rdi,%r9,1),%rbx
	movq	%r9,%rcx
.byte	102,72,15,126,207
	negq	%rax
.byte	102,72,15,126,206
	sarq	$3+2,%rcx
	decq	%r12
	xorq	%r10,%r10
	movq	8(%rbp),%r13
	movq	16(%rbp),%r14
	movq	24(%rbp),%r15
	jmp	.Lsqr4x_sub_entry

.align	16
.Lsqr4x_sub:
	movq	0(%rbp),%r12
	movq	8(%rbp),%r13
	movq	16(%rbp),%r14
	movq	24(%rbp),%r15
.Lsqr4x_sub_entry:
	leaq	32(%rbp),%rbp
	notq	%r12
	notq	%r13
	notq	%r14
	notq	%r15
	andq	%rax,%r12
	andq	%rax,%r13
	andq	%rax,%r14
	andq	%rax,%r15

	negq	%r10
	adcq	0(%rbx),%r12
	adcq	8(%rbx),%r13
	adcq	16(%rbx),%r14
	adcq	24(%rbx),%r15
	movq	%r12,0(%rdi)
	leaq	32(%rbx),%rbx
	movq	%r13,8(%rdi)
	sbbq	%r10,%r10
	movq	%r14,16(%rdi)
	movq	%r15,24(%rdi)
	leaq	32(%rdi),%rdi

	incq	%rcx
	jnz	.Lsqr4x_sub

	movq	%r9,%r10
	negq	%r9
	.byte	0xf3,0xc3
.size	__bn_post4x_internal,.-__bn_post4x_internal
.globl	bn_from_montgomery
.type	bn_from_montgomery,@function
.align	32
bn_from_montgomery:
	testl	$7,%r9d
	jz	bn_from_mont8x
	xorl	%eax,%eax
	.byte	0xf3,0xc3
.size	bn_from_montgomery,.-bn_from_montgomery

.type	bn_from_mont8x,@function
.align	32
bn_from_mont8x:
.cfi_startproc	
.byte	0x67
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
.Lfrom_prologue:

	shll	$3,%r9d
	leaq	(%r9,%r9,2),%r10
	negq	%r9
	movq	(%r8),%r8








	leaq	-320(%rsp,%r9,2),%r11
	movq	%rsp,%rbp
	subq	%rdi,%r11
	andq	$4095,%r11
	cmpq	%r11,%r10
	jb	.Lfrom_sp_alt
	subq	%r11,%rbp
	leaq	-320(%rbp,%r9,2),%rbp
	jmp	.Lfrom_sp_done

.align	32
.Lfrom_sp_alt:
	leaq	4096-320(,%r9,2),%r10
	leaq	-320(%rbp,%r9,2),%rbp
	subq	%r10,%r11
	movq	$0,%r10
	cmovcq	%r10,%r11
	subq	%r11,%rbp
.Lfrom_sp_done:
	andq	$-64,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lfrom_page_walk
	jmp	.Lfrom_page_walk_done

.Lfrom_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lfrom_page_walk
.Lfrom_page_walk_done:

	movq	%r9,%r10
	negq	%r9










	movq	%r8,32(%rsp)
	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
.Lfrom_body:
	movq	%r9,%r11
	leaq	48(%rsp),%rax
	pxor	%xmm0,%xmm0
	jmp	.Lmul_by_1

.align	32
.Lmul_by_1:
	movdqu	(%rsi),%xmm1
	movdqu	16(%rsi),%xmm2
	movdqu	32(%rsi),%xmm3
	movdqa	%xmm0,(%rax,%r9,1)
	movdqu	48(%rsi),%xmm4
	movdqa	%xmm0,16(%rax,%r9,1)
.byte	0x48,0x8d,0xb6,0x40,0x00,0x00,0x00
	movdqa	%xmm1,(%rax)
	movdqa	%xmm0,32(%rax,%r9,1)
	movdqa	%xmm2,16(%rax)
	movdqa	%xmm0,48(%rax,%r9,1)
	movdqa	%xmm3,32(%rax)
	movdqa	%xmm4,48(%rax)
	leaq	64(%rax),%rax
	subq	$64,%r11
	jnz	.Lmul_by_1

.byte	102,72,15,110,207
.byte	102,72,15,110,209
.byte	0x67
	movq	%rcx,%rbp
.byte	102,73,15,110,218
	movl	OPENSSL_ia32cap_P+8(%rip),%r11d
	andl	$0x80108,%r11d
	cmpl	$0x80108,%r11d
	jne	.Lfrom_mont_nox

	leaq	(%rax,%r9,1),%rdi
	call	__bn_sqrx8x_reduction
	call	__bn_postx4x_internal

	pxor	%xmm0,%xmm0
	leaq	48(%rsp),%rax
	jmp	.Lfrom_mont_zero

.align	32
.Lfrom_mont_nox:
	call	__bn_sqr8x_reduction
	call	__bn_post4x_internal

	pxor	%xmm0,%xmm0
	leaq	48(%rsp),%rax
	jmp	.Lfrom_mont_zero

.align	32
.Lfrom_mont_zero:
	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	movdqa	%xmm0,0(%rax)
	movdqa	%xmm0,16(%rax)
	movdqa	%xmm0,32(%rax)
	movdqa	%xmm0,48(%rax)
	leaq	64(%rax),%rax
	subq	$32,%r9
	jnz	.Lfrom_mont_zero

	movq	$1,%rax
	movq	-48(%rsi),%r15
.cfi_restore	%r15
	movq	-40(%rsi),%r14
.cfi_restore	%r14
	movq	-32(%rsi),%r13
.cfi_restore	%r13
	movq	-24(%rsi),%r12
.cfi_restore	%r12
	movq	-16(%rsi),%rbp
.cfi_restore	%rbp
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lfrom_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_from_mont8x,.-bn_from_mont8x
.type	bn_mulx4x_mont_gather5,@function
.align	32
bn_mulx4x_mont_gather5:
.cfi_startproc	
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
.Lmulx4x_enter:
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
.Lmulx4x_prologue:

	shll	$3,%r9d
	leaq	(%r9,%r9,2),%r10
	negq	%r9
	movq	(%r8),%r8










	leaq	-320(%rsp,%r9,2),%r11
	movq	%rsp,%rbp
	subq	%rdi,%r11
	andq	$4095,%r11
	cmpq	%r11,%r10
	jb	.Lmulx4xsp_alt
	subq	%r11,%rbp
	leaq	-320(%rbp,%r9,2),%rbp
	jmp	.Lmulx4xsp_done

.Lmulx4xsp_alt:
	leaq	4096-320(,%r9,2),%r10
	leaq	-320(%rbp,%r9,2),%rbp
	subq	%r10,%r11
	movq	$0,%r10
	cmovcq	%r10,%r11
	subq	%r11,%rbp
.Lmulx4xsp_done:
	andq	$-64,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lmulx4x_page_walk
	jmp	.Lmulx4x_page_walk_done

.Lmulx4x_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lmulx4x_page_walk
.Lmulx4x_page_walk_done:













	movq	%r8,32(%rsp)
	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
.Lmulx4x_body:
	call	mulx4x_internal

	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	movq	$1,%rax

	movq	-48(%rsi),%r15
.cfi_restore	%r15
	movq	-40(%rsi),%r14
.cfi_restore	%r14
	movq	-32(%rsi),%r13
.cfi_restore	%r13
	movq	-24(%rsi),%r12
.cfi_restore	%r12
	movq	-16(%rsi),%rbp
.cfi_restore	%rbp
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lmulx4x_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_mulx4x_mont_gather5,.-bn_mulx4x_mont_gather5

.type	mulx4x_internal,@function
.align	32
mulx4x_internal:
	movq	%r9,8(%rsp)
	movq	%r9,%r10
	negq	%r9
	shlq	$5,%r9
	negq	%r10
	leaq	128(%rdx,%r9,1),%r13
	shrq	$5+5,%r9
	movd	8(%rax),%xmm5
	subq	$1,%r9
	leaq	.Linc(%rip),%rax
	movq	%r13,16+8(%rsp)
	movq	%r9,24+8(%rsp)
	movq	%rdi,56+8(%rsp)
	movdqa	0(%rax),%xmm0
	movdqa	16(%rax),%xmm1
	leaq	88-112(%rsp,%r10,1),%r10
	leaq	128(%rdx),%rdi

	pshufd	$0,%xmm5,%xmm5
	movdqa	%xmm1,%xmm4
.byte	0x67
	movdqa	%xmm1,%xmm2
.byte	0x67
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,112(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,128(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,144(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,160(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,176(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,192(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,208(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,224(%r10)
	movdqa	%xmm4,%xmm3
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,240(%r10)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,256(%r10)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,272(%r10)
	movdqa	%xmm4,%xmm2

	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,288(%r10)
	movdqa	%xmm4,%xmm3
.byte	0x67
	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,304(%r10)

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,320(%r10)

	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,336(%r10)

	pand	64(%rdi),%xmm0
	pand	80(%rdi),%xmm1
	pand	96(%rdi),%xmm2
	movdqa	%xmm3,352(%r10)
	pand	112(%rdi),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	-128(%rdi),%xmm4
	movdqa	-112(%rdi),%xmm5
	movdqa	-96(%rdi),%xmm2
	pand	112(%r10),%xmm4
	movdqa	-80(%rdi),%xmm3
	pand	128(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	144(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	160(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	-64(%rdi),%xmm4
	movdqa	-48(%rdi),%xmm5
	movdqa	-32(%rdi),%xmm2
	pand	176(%r10),%xmm4
	movdqa	-16(%rdi),%xmm3
	pand	192(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	208(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	224(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	movdqa	0(%rdi),%xmm4
	movdqa	16(%rdi),%xmm5
	movdqa	32(%rdi),%xmm2
	pand	240(%r10),%xmm4
	movdqa	48(%rdi),%xmm3
	pand	256(%r10),%xmm5
	por	%xmm4,%xmm0
	pand	272(%r10),%xmm2
	por	%xmm5,%xmm1
	pand	288(%r10),%xmm3
	por	%xmm2,%xmm0
	por	%xmm3,%xmm1
	pxor	%xmm1,%xmm0
	pshufd	$0x4e,%xmm0,%xmm1
	por	%xmm1,%xmm0
	leaq	256(%rdi),%rdi
.byte	102,72,15,126,194
	leaq	64+32+8(%rsp),%rbx

	movq	%rdx,%r9
	mulxq	0(%rsi),%r8,%rax
	mulxq	8(%rsi),%r11,%r12
	addq	%rax,%r11
	mulxq	16(%rsi),%rax,%r13
	adcq	%rax,%r12
	adcq	$0,%r13
	mulxq	24(%rsi),%rax,%r14

	movq	%r8,%r15
	imulq	32+8(%rsp),%r8
	xorq	%rbp,%rbp
	movq	%r8,%rdx

	movq	%rdi,8+8(%rsp)

	leaq	32(%rsi),%rsi
	adcxq	%rax,%r13
	adcxq	%rbp,%r14

	mulxq	0(%rcx),%rax,%r10
	adcxq	%rax,%r15
	adoxq	%r11,%r10
	mulxq	8(%rcx),%rax,%r11
	adcxq	%rax,%r10
	adoxq	%r12,%r11
	mulxq	16(%rcx),%rax,%r12
	movq	24+8(%rsp),%rdi
	movq	%r10,-32(%rbx)
	adcxq	%rax,%r11
	adoxq	%r13,%r12
	mulxq	24(%rcx),%rax,%r15
	movq	%r9,%rdx
	movq	%r11,-24(%rbx)
	adcxq	%rax,%r12
	adoxq	%rbp,%r15
	leaq	32(%rcx),%rcx
	movq	%r12,-16(%rbx)
	jmp	.Lmulx4x_1st

.align	32
.Lmulx4x_1st:
	adcxq	%rbp,%r15
	mulxq	0(%rsi),%r10,%rax
	adcxq	%r14,%r10
	mulxq	8(%rsi),%r11,%r14
	adcxq	%rax,%r11
	mulxq	16(%rsi),%r12,%rax
	adcxq	%r14,%r12
	mulxq	24(%rsi),%r13,%r14
.byte	0x67,0x67
	movq	%r8,%rdx
	adcxq	%rax,%r13
	adcxq	%rbp,%r14
	leaq	32(%rsi),%rsi
	leaq	32(%rbx),%rbx

	adoxq	%r15,%r10
	mulxq	0(%rcx),%rax,%r15
	adcxq	%rax,%r10
	adoxq	%r15,%r11
	mulxq	8(%rcx),%rax,%r15
	adcxq	%rax,%r11
	adoxq	%r15,%r12
	mulxq	16(%rcx),%rax,%r15
	movq	%r10,-40(%rbx)
	adcxq	%rax,%r12
	movq	%r11,-32(%rbx)
	adoxq	%r15,%r13
	mulxq	24(%rcx),%rax,%r15
	movq	%r9,%rdx
	movq	%r12,-24(%rbx)
	adcxq	%rax,%r13
	adoxq	%rbp,%r15
	leaq	32(%rcx),%rcx
	movq	%r13,-16(%rbx)

	decq	%rdi
	jnz	.Lmulx4x_1st

	movq	8(%rsp),%rax
	adcq	%rbp,%r15
	leaq	(%rsi,%rax,1),%rsi
	addq	%r15,%r14
	movq	8+8(%rsp),%rdi
	adcq	%rbp,%rbp
	movq	%r14,-8(%rbx)
	jmp	.Lmulx4x_outer

.align	32
.Lmulx4x_outer:
	leaq	16-256(%rbx),%r10
	pxor	%xmm4,%xmm4
.byte	0x67,0x67
	pxor	%xmm5,%xmm5
	movdqa	-128(%rdi),%xmm0
	movdqa	-112(%rdi),%xmm1
	movdqa	-96(%rdi),%xmm2
	pand	256(%r10),%xmm0
	movdqa	-80(%rdi),%xmm3
	pand	272(%r10),%xmm1
	por	%xmm0,%xmm4
	pand	288(%r10),%xmm2
	por	%xmm1,%xmm5
	pand	304(%r10),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	-64(%rdi),%xmm0
	movdqa	-48(%rdi),%xmm1
	movdqa	-32(%rdi),%xmm2
	pand	320(%r10),%xmm0
	movdqa	-16(%rdi),%xmm3
	pand	336(%r10),%xmm1
	por	%xmm0,%xmm4
	pand	352(%r10),%xmm2
	por	%xmm1,%xmm5
	pand	368(%r10),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	0(%rdi),%xmm0
	movdqa	16(%rdi),%xmm1
	movdqa	32(%rdi),%xmm2
	pand	384(%r10),%xmm0
	movdqa	48(%rdi),%xmm3
	pand	400(%r10),%xmm1
	por	%xmm0,%xmm4
	pand	416(%r10),%xmm2
	por	%xmm1,%xmm5
	pand	432(%r10),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	64(%rdi),%xmm0
	movdqa	80(%rdi),%xmm1
	movdqa	96(%rdi),%xmm2
	pand	448(%r10),%xmm0
	movdqa	112(%rdi),%xmm3
	pand	464(%r10),%xmm1
	por	%xmm0,%xmm4
	pand	480(%r10),%xmm2
	por	%xmm1,%xmm5
	pand	496(%r10),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	por	%xmm5,%xmm4
	pshufd	$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	leaq	256(%rdi),%rdi
.byte	102,72,15,126,194

	movq	%rbp,(%rbx)
	leaq	32(%rbx,%rax,1),%rbx
	mulxq	0(%rsi),%r8,%r11
	xorq	%rbp,%rbp
	movq	%rdx,%r9
	mulxq	8(%rsi),%r14,%r12
	adoxq	-32(%rbx),%r8
	adcxq	%r14,%r11
	mulxq	16(%rsi),%r15,%r13
	adoxq	-24(%rbx),%r11
	adcxq	%r15,%r12
	mulxq	24(%rsi),%rdx,%r14
	adoxq	-16(%rbx),%r12
	adcxq	%rdx,%r13
	leaq	(%rcx,%rax,1),%rcx
	leaq	32(%rsi),%rsi
	adoxq	-8(%rbx),%r13
	adcxq	%rbp,%r14
	adoxq	%rbp,%r14

	movq	%r8,%r15
	imulq	32+8(%rsp),%r8

	movq	%r8,%rdx
	xorq	%rbp,%rbp
	movq	%rdi,8+8(%rsp)

	mulxq	0(%rcx),%rax,%r10
	adcxq	%rax,%r15
	adoxq	%r11,%r10
	mulxq	8(%rcx),%rax,%r11
	adcxq	%rax,%r10
	adoxq	%r12,%r11
	mulxq	16(%rcx),%rax,%r12
	adcxq	%rax,%r11
	adoxq	%r13,%r12
	mulxq	24(%rcx),%rax,%r15
	movq	%r9,%rdx
	movq	24+8(%rsp),%rdi
	movq	%r10,-32(%rbx)
	adcxq	%rax,%r12
	movq	%r11,-24(%rbx)
	adoxq	%rbp,%r15
	movq	%r12,-16(%rbx)
	leaq	32(%rcx),%rcx
	jmp	.Lmulx4x_inner

.align	32
.Lmulx4x_inner:
	mulxq	0(%rsi),%r10,%rax
	adcxq	%rbp,%r15
	adoxq	%r14,%r10
	mulxq	8(%rsi),%r11,%r14
	adcxq	0(%rbx),%r10
	adoxq	%rax,%r11
	mulxq	16(%rsi),%r12,%rax
	adcxq	8(%rbx),%r11
	adoxq	%r14,%r12
	mulxq	24(%rsi),%r13,%r14
	movq	%r8,%rdx
	adcxq	16(%rbx),%r12
	adoxq	%rax,%r13
	adcxq	24(%rbx),%r13
	adoxq	%rbp,%r14
	leaq	32(%rsi),%rsi
	leaq	32(%rbx),%rbx
	adcxq	%rbp,%r14

	adoxq	%r15,%r10
	mulxq	0(%rcx),%rax,%r15
	adcxq	%rax,%r10
	adoxq	%r15,%r11
	mulxq	8(%rcx),%rax,%r15
	adcxq	%rax,%r11
	adoxq	%r15,%r12
	mulxq	16(%rcx),%rax,%r15
	movq	%r10,-40(%rbx)
	adcxq	%rax,%r12
	adoxq	%r15,%r13
	movq	%r11,-32(%rbx)
	mulxq	24(%rcx),%rax,%r15
	movq	%r9,%rdx
	leaq	32(%rcx),%rcx
	movq	%r12,-24(%rbx)
	adcxq	%rax,%r13
	adoxq	%rbp,%r15
	movq	%r13,-16(%rbx)

	decq	%rdi
	jnz	.Lmulx4x_inner

	movq	0+8(%rsp),%rax
	adcq	%rbp,%r15
	subq	0(%rbx),%rdi
	movq	8+8(%rsp),%rdi
	movq	16+8(%rsp),%r10
	adcq	%r15,%r14
	leaq	(%rsi,%rax,1),%rsi
	adcq	%rbp,%rbp
	movq	%r14,-8(%rbx)

	cmpq	%r10,%rdi
	jb	.Lmulx4x_outer

	movq	-8(%rcx),%r10
	movq	%rbp,%r8
	movq	(%rcx,%rax,1),%r12
	leaq	(%rcx,%rax,1),%rbp
	movq	%rax,%rcx
	leaq	(%rbx,%rax,1),%rdi
	xorl	%eax,%eax
	xorq	%r15,%r15
	subq	%r14,%r10
	adcq	%r15,%r15
	orq	%r15,%r8
	sarq	$3+2,%rcx
	subq	%r8,%rax
	movq	56+8(%rsp),%rdx
	decq	%r12
	movq	8(%rbp),%r13
	xorq	%r8,%r8
	movq	16(%rbp),%r14
	movq	24(%rbp),%r15
	jmp	.Lsqrx4x_sub_entry
.size	mulx4x_internal,.-mulx4x_internal
.type	bn_powerx5,@function
.align	32
bn_powerx5:
.cfi_startproc	
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
.Lpowerx5_enter:
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
.Lpowerx5_prologue:

	shll	$3,%r9d
	leaq	(%r9,%r9,2),%r10
	negq	%r9
	movq	(%r8),%r8








	leaq	-320(%rsp,%r9,2),%r11
	movq	%rsp,%rbp
	subq	%rdi,%r11
	andq	$4095,%r11
	cmpq	%r11,%r10
	jb	.Lpwrx_sp_alt
	subq	%r11,%rbp
	leaq	-320(%rbp,%r9,2),%rbp
	jmp	.Lpwrx_sp_done

.align	32
.Lpwrx_sp_alt:
	leaq	4096-320(,%r9,2),%r10
	leaq	-320(%rbp,%r9,2),%rbp
	subq	%r10,%r11
	movq	$0,%r10
	cmovcq	%r10,%r11
	subq	%r11,%rbp
.Lpwrx_sp_done:
	andq	$-64,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lpwrx_page_walk
	jmp	.Lpwrx_page_walk_done

.Lpwrx_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lpwrx_page_walk
.Lpwrx_page_walk_done:

	movq	%r9,%r10
	negq	%r9












	pxor	%xmm0,%xmm0
.byte	102,72,15,110,207
.byte	102,72,15,110,209
.byte	102,73,15,110,218
.byte	102,72,15,110,226
	movq	%r8,32(%rsp)
	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
.Lpowerx5_body:

	call	__bn_sqrx8x_internal
	call	__bn_postx4x_internal
	call	__bn_sqrx8x_internal
	call	__bn_postx4x_internal
	call	__bn_sqrx8x_internal
	call	__bn_postx4x_internal
	call	__bn_sqrx8x_internal
	call	__bn_postx4x_internal
	call	__bn_sqrx8x_internal
	call	__bn_postx4x_internal

	movq	%r10,%r9
	movq	%rsi,%rdi
.byte	102,72,15,126,209
.byte	102,72,15,126,226
	movq	40(%rsp),%rax

	call	mulx4x_internal

	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	movq	$1,%rax

	movq	-48(%rsi),%r15
.cfi_restore	%r15
	movq	-40(%rsi),%r14
.cfi_restore	%r14
	movq	-32(%rsi),%r13
.cfi_restore	%r13
	movq	-24(%rsi),%r12
.cfi_restore	%r12
	movq	-16(%rsi),%rbp
.cfi_restore	%rbp
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lpowerx5_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_powerx5,.-bn_powerx5

.globl	bn_sqrx8x_internal
.hidden	bn_sqrx8x_internal
.type	bn_sqrx8x_internal,@function
.align	32
bn_sqrx8x_internal:
__bn_sqrx8x_internal:
.cfi_startproc	








































	leaq	48+8(%rsp),%rdi
	leaq	(%rsi,%r9,1),%rbp
	movq	%r9,0+8(%rsp)
	movq	%rbp,8+8(%rsp)
	jmp	.Lsqr8x_zero_start

.align	32
.byte	0x66,0x66,0x66,0x2e,0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00
.Lsqrx8x_zero:
.byte	0x3e
	movdqa	%xmm0,0(%rdi)
	movdqa	%xmm0,16(%rdi)
	movdqa	%xmm0,32(%rdi)
	movdqa	%xmm0,48(%rdi)
.Lsqr8x_zero_start:
	movdqa	%xmm0,64(%rdi)
	movdqa	%xmm0,80(%rdi)
	movdqa	%xmm0,96(%rdi)
	movdqa	%xmm0,112(%rdi)
	leaq	128(%rdi),%rdi
	subq	$64,%r9
	jnz	.Lsqrx8x_zero

	movq	0(%rsi),%rdx

	xorq	%r10,%r10
	xorq	%r11,%r11
	xorq	%r12,%r12
	xorq	%r13,%r13
	xorq	%r14,%r14
	xorq	%r15,%r15
	leaq	48+8(%rsp),%rdi
	xorq	%rbp,%rbp
	jmp	.Lsqrx8x_outer_loop

.align	32
.Lsqrx8x_outer_loop:
	mulxq	8(%rsi),%r8,%rax
	adcxq	%r9,%r8
	adoxq	%rax,%r10
	mulxq	16(%rsi),%r9,%rax
	adcxq	%r10,%r9
	adoxq	%rax,%r11
.byte	0xc4,0xe2,0xab,0xf6,0x86,0x18,0x00,0x00,0x00
	adcxq	%r11,%r10
	adoxq	%rax,%r12
.byte	0xc4,0xe2,0xa3,0xf6,0x86,0x20,0x00,0x00,0x00
	adcxq	%r12,%r11
	adoxq	%rax,%r13
	mulxq	40(%rsi),%r12,%rax
	adcxq	%r13,%r12
	adoxq	%rax,%r14
	mulxq	48(%rsi),%r13,%rax
	adcxq	%r14,%r13
	adoxq	%r15,%rax
	mulxq	56(%rsi),%r14,%r15
	movq	8(%rsi),%rdx
	adcxq	%rax,%r14
	adoxq	%rbp,%r15
	adcq	64(%rdi),%r15
	movq	%r8,8(%rdi)
	movq	%r9,16(%rdi)
	sbbq	%rcx,%rcx
	xorq	%rbp,%rbp


	mulxq	16(%rsi),%r8,%rbx
	mulxq	24(%rsi),%r9,%rax
	adcxq	%r10,%r8
	adoxq	%rbx,%r9
	mulxq	32(%rsi),%r10,%rbx
	adcxq	%r11,%r9
	adoxq	%rax,%r10
.byte	0xc4,0xe2,0xa3,0xf6,0x86,0x28,0x00,0x00,0x00
	adcxq	%r12,%r10
	adoxq	%rbx,%r11
.byte	0xc4,0xe2,0x9b,0xf6,0x9e,0x30,0x00,0x00,0x00
	adcxq	%r13,%r11
	adoxq	%r14,%r12
.byte	0xc4,0x62,0x93,0xf6,0xb6,0x38,0x00,0x00,0x00
	movq	16(%rsi),%rdx
	adcxq	%rax,%r12
	adoxq	%rbx,%r13
	adcxq	%r15,%r13
	adoxq	%rbp,%r14
	adcxq	%rbp,%r14

	movq	%r8,24(%rdi)
	movq	%r9,32(%rdi)

	mulxq	24(%rsi),%r8,%rbx
	mulxq	32(%rsi),%r9,%rax
	adcxq	%r10,%r8
	adoxq	%rbx,%r9
	mulxq	40(%rsi),%r10,%rbx
	adcxq	%r11,%r9
	adoxq	%rax,%r10
.byte	0xc4,0xe2,0xa3,0xf6,0x86,0x30,0x00,0x00,0x00
	adcxq	%r12,%r10
	adoxq	%r13,%r11
.byte	0xc4,0x62,0x9b,0xf6,0xae,0x38,0x00,0x00,0x00
.byte	0x3e
	movq	24(%rsi),%rdx
	adcxq	%rbx,%r11
	adoxq	%rax,%r12
	adcxq	%r14,%r12
	movq	%r8,40(%rdi)
	movq	%r9,48(%rdi)
	mulxq	32(%rsi),%r8,%rax
	adoxq	%rbp,%r13
	adcxq	%rbp,%r13

	mulxq	40(%rsi),%r9,%rbx
	adcxq	%r10,%r8
	adoxq	%rax,%r9
	mulxq	48(%rsi),%r10,%rax
	adcxq	%r11,%r9
	adoxq	%r12,%r10
	mulxq	56(%rsi),%r11,%r12
	movq	32(%rsi),%rdx
	movq	40(%rsi),%r14
	adcxq	%rbx,%r10
	adoxq	%rax,%r11
	movq	48(%rsi),%r15
	adcxq	%r13,%r11
	adoxq	%rbp,%r12
	adcxq	%rbp,%r12

	movq	%r8,56(%rdi)
	movq	%r9,64(%rdi)

	mulxq	%r14,%r9,%rax
	movq	56(%rsi),%r8
	adcxq	%r10,%r9
	mulxq	%r15,%r10,%rbx
	adoxq	%rax,%r10
	adcxq	%r11,%r10
	mulxq	%r8,%r11,%rax
	movq	%r14,%rdx
	adoxq	%rbx,%r11
	adcxq	%r12,%r11

	adcxq	%rbp,%rax

	mulxq	%r15,%r14,%rbx
	mulxq	%r8,%r12,%r13
	movq	%r15,%rdx
	leaq	64(%rsi),%rsi
	adcxq	%r14,%r11
	adoxq	%rbx,%r12
	adcxq	%rax,%r12
	adoxq	%rbp,%r13

.byte	0x67,0x67
	mulxq	%r8,%r8,%r14
	adcxq	%r8,%r13
	adcxq	%rbp,%r14

	cmpq	8+8(%rsp),%rsi
	je	.Lsqrx8x_outer_break

	negq	%rcx
	movq	$-8,%rcx
	movq	%rbp,%r15
	movq	64(%rdi),%r8
	adcxq	72(%rdi),%r9
	adcxq	80(%rdi),%r10
	adcxq	88(%rdi),%r11
	adcq	96(%rdi),%r12
	adcq	104(%rdi),%r13
	adcq	112(%rdi),%r14
	adcq	120(%rdi),%r15
	leaq	(%rsi),%rbp
	leaq	128(%rdi),%rdi
	sbbq	%rax,%rax

	movq	-64(%rsi),%rdx
	movq	%rax,16+8(%rsp)
	movq	%rdi,24+8(%rsp)


	xorl	%eax,%eax
	jmp	.Lsqrx8x_loop

.align	32
.Lsqrx8x_loop:
	movq	%r8,%rbx
	mulxq	0(%rbp),%rax,%r8
	adcxq	%rax,%rbx
	adoxq	%r9,%r8

	mulxq	8(%rbp),%rax,%r9
	adcxq	%rax,%r8
	adoxq	%r10,%r9

	mulxq	16(%rbp),%rax,%r10
	adcxq	%rax,%r9
	adoxq	%r11,%r10

	mulxq	24(%rbp),%rax,%r11
	adcxq	%rax,%r10
	adoxq	%r12,%r11

.byte	0xc4,0x62,0xfb,0xf6,0xa5,0x20,0x00,0x00,0x00
	adcxq	%rax,%r11
	adoxq	%r13,%r12

	mulxq	40(%rbp),%rax,%r13
	adcxq	%rax,%r12
	adoxq	%r14,%r13

	mulxq	48(%rbp),%rax,%r14
	movq	%rbx,(%rdi,%rcx,8)
	movl	$0,%ebx
	adcxq	%rax,%r13
	adoxq	%r15,%r14

.byte	0xc4,0x62,0xfb,0xf6,0xbd,0x38,0x00,0x00,0x00
	movq	8(%rsi,%rcx,8),%rdx
	adcxq	%rax,%r14
	adoxq	%rbx,%r15
	adcxq	%rbx,%r15

.byte	0x67
	incq	%rcx
	jnz	.Lsqrx8x_loop

	leaq	64(%rbp),%rbp
	movq	$-8,%rcx
	cmpq	8+8(%rsp),%rbp
	je	.Lsqrx8x_break

	subq	16+8(%rsp),%rbx
.byte	0x66
	movq	-64(%rsi),%rdx
	adcxq	0(%rdi),%r8
	adcxq	8(%rdi),%r9
	adcq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	leaq	64(%rdi),%rdi
.byte	0x67
	sbbq	%rax,%rax
	xorl	%ebx,%ebx
	movq	%rax,16+8(%rsp)
	jmp	.Lsqrx8x_loop

.align	32
.Lsqrx8x_break:
	xorq	%rbp,%rbp
	subq	16+8(%rsp),%rbx
	adcxq	%rbp,%r8
	movq	24+8(%rsp),%rcx
	adcxq	%rbp,%r9
	movq	0(%rsi),%rdx
	adcq	$0,%r10
	movq	%r8,0(%rdi)
	adcq	$0,%r11
	adcq	$0,%r12
	adcq	$0,%r13
	adcq	$0,%r14
	adcq	$0,%r15
	cmpq	%rcx,%rdi
	je	.Lsqrx8x_outer_loop

	movq	%r9,8(%rdi)
	movq	8(%rcx),%r9
	movq	%r10,16(%rdi)
	movq	16(%rcx),%r10
	movq	%r11,24(%rdi)
	movq	24(%rcx),%r11
	movq	%r12,32(%rdi)
	movq	32(%rcx),%r12
	movq	%r13,40(%rdi)
	movq	40(%rcx),%r13
	movq	%r14,48(%rdi)
	movq	48(%rcx),%r14
	movq	%r15,56(%rdi)
	movq	56(%rcx),%r15
	movq	%rcx,%rdi
	jmp	.Lsqrx8x_outer_loop

.align	32
.Lsqrx8x_outer_break:
	movq	%r9,72(%rdi)
.byte	102,72,15,126,217
	movq	%r10,80(%rdi)
	movq	%r11,88(%rdi)
	movq	%r12,96(%rdi)
	movq	%r13,104(%rdi)
	movq	%r14,112(%rdi)
	leaq	48+8(%rsp),%rdi
	movq	(%rsi,%rcx,1),%rdx

	movq	8(%rdi),%r11
	xorq	%r10,%r10
	movq	0+8(%rsp),%r9
	adoxq	%r11,%r11
	movq	16(%rdi),%r12
	movq	24(%rdi),%r13


.align	32
.Lsqrx4x_shift_n_add:
	mulxq	%rdx,%rax,%rbx
	adoxq	%r12,%r12
	adcxq	%r10,%rax
.byte	0x48,0x8b,0x94,0x0e,0x08,0x00,0x00,0x00
.byte	0x4c,0x8b,0x97,0x20,0x00,0x00,0x00
	adoxq	%r13,%r13
	adcxq	%r11,%rbx
	movq	40(%rdi),%r11
	movq	%rax,0(%rdi)
	movq	%rbx,8(%rdi)

	mulxq	%rdx,%rax,%rbx
	adoxq	%r10,%r10
	adcxq	%r12,%rax
	movq	16(%rsi,%rcx,1),%rdx
	movq	48(%rdi),%r12
	adoxq	%r11,%r11
	adcxq	%r13,%rbx
	movq	56(%rdi),%r13
	movq	%rax,16(%rdi)
	movq	%rbx,24(%rdi)

	mulxq	%rdx,%rax,%rbx
	adoxq	%r12,%r12
	adcxq	%r10,%rax
	movq	24(%rsi,%rcx,1),%rdx
	leaq	32(%rcx),%rcx
	movq	64(%rdi),%r10
	adoxq	%r13,%r13
	adcxq	%r11,%rbx
	movq	72(%rdi),%r11
	movq	%rax,32(%rdi)
	movq	%rbx,40(%rdi)

	mulxq	%rdx,%rax,%rbx
	adoxq	%r10,%r10
	adcxq	%r12,%rax
	jrcxz	.Lsqrx4x_shift_n_add_break
.byte	0x48,0x8b,0x94,0x0e,0x00,0x00,0x00,0x00
	adoxq	%r11,%r11
	adcxq	%r13,%rbx
	movq	80(%rdi),%r12
	movq	88(%rdi),%r13
	movq	%rax,48(%rdi)
	movq	%rbx,56(%rdi)
	leaq	64(%rdi),%rdi
	nop
	jmp	.Lsqrx4x_shift_n_add

.align	32
.Lsqrx4x_shift_n_add_break:
	adcxq	%r13,%rbx
	movq	%rax,48(%rdi)
	movq	%rbx,56(%rdi)
	leaq	64(%rdi),%rdi
.byte	102,72,15,126,213
__bn_sqrx8x_reduction:
	xorl	%eax,%eax
	movq	32+8(%rsp),%rbx
	movq	48+8(%rsp),%rdx
	leaq	-64(%rbp,%r9,1),%rcx

	movq	%rcx,0+8(%rsp)
	movq	%rdi,8+8(%rsp)

	leaq	48+8(%rsp),%rdi
	jmp	.Lsqrx8x_reduction_loop

.align	32
.Lsqrx8x_reduction_loop:
	movq	8(%rdi),%r9
	movq	16(%rdi),%r10
	movq	24(%rdi),%r11
	movq	32(%rdi),%r12
	movq	%rdx,%r8
	imulq	%rbx,%rdx
	movq	40(%rdi),%r13
	movq	48(%rdi),%r14
	movq	56(%rdi),%r15
	movq	%rax,24+8(%rsp)

	leaq	64(%rdi),%rdi
	xorq	%rsi,%rsi
	movq	$-8,%rcx
	jmp	.Lsqrx8x_reduce

.align	32
.Lsqrx8x_reduce:
	movq	%r8,%rbx
	mulxq	0(%rbp),%rax,%r8
	adcxq	%rbx,%rax
	adoxq	%r9,%r8

	mulxq	8(%rbp),%rbx,%r9
	adcxq	%rbx,%r8
	adoxq	%r10,%r9

	mulxq	16(%rbp),%rbx,%r10
	adcxq	%rbx,%r9
	adoxq	%r11,%r10

	mulxq	24(%rbp),%rbx,%r11
	adcxq	%rbx,%r10
	adoxq	%r12,%r11

.byte	0xc4,0x62,0xe3,0xf6,0xa5,0x20,0x00,0x00,0x00
	movq	%rdx,%rax
	movq	%r8,%rdx
	adcxq	%rbx,%r11
	adoxq	%r13,%r12

	mulxq	32+8(%rsp),%rbx,%rdx
	movq	%rax,%rdx
	movq	%rax,64+48+8(%rsp,%rcx,8)

	mulxq	40(%rbp),%rax,%r13
	adcxq	%rax,%r12
	adoxq	%r14,%r13

	mulxq	48(%rbp),%rax,%r14
	adcxq	%rax,%r13
	adoxq	%r15,%r14

	mulxq	56(%rbp),%rax,%r15
	movq	%rbx,%rdx
	adcxq	%rax,%r14
	adoxq	%rsi,%r15
	adcxq	%rsi,%r15

.byte	0x67,0x67,0x67
	incq	%rcx
	jnz	.Lsqrx8x_reduce

	movq	%rsi,%rax
	cmpq	0+8(%rsp),%rbp
	jae	.Lsqrx8x_no_tail

	movq	48+8(%rsp),%rdx
	addq	0(%rdi),%r8
	leaq	64(%rbp),%rbp
	movq	$-8,%rcx
	adcxq	8(%rdi),%r9
	adcxq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	leaq	64(%rdi),%rdi
	sbbq	%rax,%rax

	xorq	%rsi,%rsi
	movq	%rax,16+8(%rsp)
	jmp	.Lsqrx8x_tail

.align	32
.Lsqrx8x_tail:
	movq	%r8,%rbx
	mulxq	0(%rbp),%rax,%r8
	adcxq	%rax,%rbx
	adoxq	%r9,%r8

	mulxq	8(%rbp),%rax,%r9
	adcxq	%rax,%r8
	adoxq	%r10,%r9

	mulxq	16(%rbp),%rax,%r10
	adcxq	%rax,%r9
	adoxq	%r11,%r10

	mulxq	24(%rbp),%rax,%r11
	adcxq	%rax,%r10
	adoxq	%r12,%r11

.byte	0xc4,0x62,0xfb,0xf6,0xa5,0x20,0x00,0x00,0x00
	adcxq	%rax,%r11
	adoxq	%r13,%r12

	mulxq	40(%rbp),%rax,%r13
	adcxq	%rax,%r12
	adoxq	%r14,%r13

	mulxq	48(%rbp),%rax,%r14
	adcxq	%rax,%r13
	adoxq	%r15,%r14

	mulxq	56(%rbp),%rax,%r15
	movq	72+48+8(%rsp,%rcx,8),%rdx
	adcxq	%rax,%r14
	adoxq	%rsi,%r15
	movq	%rbx,(%rdi,%rcx,8)
	movq	%r8,%rbx
	adcxq	%rsi,%r15

	incq	%rcx
	jnz	.Lsqrx8x_tail

	cmpq	0+8(%rsp),%rbp
	jae	.Lsqrx8x_tail_done

	subq	16+8(%rsp),%rsi
	movq	48+8(%rsp),%rdx
	leaq	64(%rbp),%rbp
	adcq	0(%rdi),%r8
	adcq	8(%rdi),%r9
	adcq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	leaq	64(%rdi),%rdi
	sbbq	%rax,%rax
	subq	$8,%rcx

	xorq	%rsi,%rsi
	movq	%rax,16+8(%rsp)
	jmp	.Lsqrx8x_tail

.align	32
.Lsqrx8x_tail_done:
	xorq	%rax,%rax
	addq	24+8(%rsp),%r8
	adcq	$0,%r9
	adcq	$0,%r10
	adcq	$0,%r11
	adcq	$0,%r12
	adcq	$0,%r13
	adcq	$0,%r14
	adcq	$0,%r15
	adcq	$0,%rax

	subq	16+8(%rsp),%rsi
.Lsqrx8x_no_tail:
	adcq	0(%rdi),%r8
.byte	102,72,15,126,217
	adcq	8(%rdi),%r9
	movq	56(%rbp),%rsi
.byte	102,72,15,126,213
	adcq	16(%rdi),%r10
	adcq	24(%rdi),%r11
	adcq	32(%rdi),%r12
	adcq	40(%rdi),%r13
	adcq	48(%rdi),%r14
	adcq	56(%rdi),%r15
	adcq	$0,%rax

	movq	32+8(%rsp),%rbx
	movq	64(%rdi,%rcx,1),%rdx

	movq	%r8,0(%rdi)
	leaq	64(%rdi),%r8
	movq	%r9,8(%rdi)
	movq	%r10,16(%rdi)
	movq	%r11,24(%rdi)
	movq	%r12,32(%rdi)
	movq	%r13,40(%rdi)
	movq	%r14,48(%rdi)
	movq	%r15,56(%rdi)

	leaq	64(%rdi,%rcx,1),%rdi
	cmpq	8+8(%rsp),%r8
	jb	.Lsqrx8x_reduction_loop
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_sqrx8x_internal,.-bn_sqrx8x_internal
.align	32
__bn_postx4x_internal:
	movq	0(%rbp),%r12
	movq	%rcx,%r10
	movq	%rcx,%r9
	negq	%rax
	sarq	$3+2,%rcx

.byte	102,72,15,126,202
.byte	102,72,15,126,206
	decq	%r12
	movq	8(%rbp),%r13
	xorq	%r8,%r8
	movq	16(%rbp),%r14
	movq	24(%rbp),%r15
	jmp	.Lsqrx4x_sub_entry

.align	16
.Lsqrx4x_sub:
	movq	0(%rbp),%r12
	movq	8(%rbp),%r13
	movq	16(%rbp),%r14
	movq	24(%rbp),%r15
.Lsqrx4x_sub_entry:
	andnq	%rax,%r12,%r12
	leaq	32(%rbp),%rbp
	andnq	%rax,%r13,%r13
	andnq	%rax,%r14,%r14
	andnq	%rax,%r15,%r15

	negq	%r8
	adcq	0(%rdi),%r12
	adcq	8(%rdi),%r13
	adcq	16(%rdi),%r14
	adcq	24(%rdi),%r15
	movq	%r12,0(%rdx)
	leaq	32(%rdi),%rdi
	movq	%r13,8(%rdx)
	sbbq	%r8,%r8
	movq	%r14,16(%rdx)
	movq	%r15,24(%rdx)
	leaq	32(%rdx),%rdx

	incq	%rcx
	jnz	.Lsqrx4x_sub

	negq	%r9

	.byte	0xf3,0xc3
.size	__bn_postx4x_internal,.-__bn_postx4x_internal
.globl	bn_get_bits5
.type	bn_get_bits5,@function
.align	16
bn_get_bits5:
	leaq	0(%rdi),%r10
	leaq	1(%rdi),%r11
	movl	%esi,%ecx
	shrl	$4,%esi
	andl	$15,%ecx
	leal	-8(%rcx),%eax
	cmpl	$11,%ecx
	cmovaq	%r11,%r10
	cmoval	%eax,%ecx
	movzwl	(%r10,%rsi,2),%eax
	shrl	%cl,%eax
	andl	$31,%eax
	.byte	0xf3,0xc3
.size	bn_get_bits5,.-bn_get_bits5

.globl	bn_scatter5
.type	bn_scatter5,@function
.align	16
bn_scatter5:
	cmpl	$0,%esi
	jz	.Lscatter_epilogue
	leaq	(%rdx,%rcx,8),%rdx
.Lscatter:
	movq	(%rdi),%rax
	leaq	8(%rdi),%rdi
	movq	%rax,(%rdx)
	leaq	256(%rdx),%rdx
	subl	$1,%esi
	jnz	.Lscatter
.Lscatter_epilogue:
	.byte	0xf3,0xc3
.size	bn_scatter5,.-bn_scatter5

.globl	bn_gather5
.type	bn_gather5,@function
.align	32
bn_gather5:
.LSEH_begin_bn_gather5:

.byte	0x4c,0x8d,0x14,0x24
.byte	0x48,0x81,0xec,0x08,0x01,0x00,0x00
	leaq	.Linc(%rip),%rax
	andq	$-16,%rsp

	movd	%ecx,%xmm5
	movdqa	0(%rax),%xmm0
	movdqa	16(%rax),%xmm1
	leaq	128(%rdx),%r11
	leaq	128(%rsp),%rax

	pshufd	$0,%xmm5,%xmm5
	movdqa	%xmm1,%xmm4
	movdqa	%xmm1,%xmm2
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm4,%xmm3

	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,-128(%rax)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,-112(%rax)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,-96(%rax)
	movdqa	%xmm4,%xmm2
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,-80(%rax)
	movdqa	%xmm4,%xmm3

	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,-64(%rax)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,-48(%rax)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,-32(%rax)
	movdqa	%xmm4,%xmm2
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,-16(%rax)
	movdqa	%xmm4,%xmm3

	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,0(%rax)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,16(%rax)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,32(%rax)
	movdqa	%xmm4,%xmm2
	paddd	%xmm0,%xmm1
	pcmpeqd	%xmm5,%xmm0
	movdqa	%xmm3,48(%rax)
	movdqa	%xmm4,%xmm3

	paddd	%xmm1,%xmm2
	pcmpeqd	%xmm5,%xmm1
	movdqa	%xmm0,64(%rax)
	movdqa	%xmm4,%xmm0

	paddd	%xmm2,%xmm3
	pcmpeqd	%xmm5,%xmm2
	movdqa	%xmm1,80(%rax)
	movdqa	%xmm4,%xmm1

	paddd	%xmm3,%xmm0
	pcmpeqd	%xmm5,%xmm3
	movdqa	%xmm2,96(%rax)
	movdqa	%xmm4,%xmm2
	movdqa	%xmm3,112(%rax)
	jmp	.Lgather

.align	32
.Lgather:
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	movdqa	-128(%r11),%xmm0
	movdqa	-112(%r11),%xmm1
	movdqa	-96(%r11),%xmm2
	pand	-128(%rax),%xmm0
	movdqa	-80(%r11),%xmm3
	pand	-112(%rax),%xmm1
	por	%xmm0,%xmm4
	pand	-96(%rax),%xmm2
	por	%xmm1,%xmm5
	pand	-80(%rax),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	-64(%r11),%xmm0
	movdqa	-48(%r11),%xmm1
	movdqa	-32(%r11),%xmm2
	pand	-64(%rax),%xmm0
	movdqa	-16(%r11),%xmm3
	pand	-48(%rax),%xmm1
	por	%xmm0,%xmm4
	pand	-32(%rax),%xmm2
	por	%xmm1,%xmm5
	pand	-16(%rax),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	0(%r11),%xmm0
	movdqa	16(%r11),%xmm1
	movdqa	32(%r11),%xmm2
	pand	0(%rax),%xmm0
	movdqa	48(%r11),%xmm3
	pand	16(%rax),%xmm1
	por	%xmm0,%xmm4
	pand	32(%rax),%xmm2
	por	%xmm1,%xmm5
	pand	48(%rax),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqa	64(%r11),%xmm0
	movdqa	80(%r11),%xmm1
	movdqa	96(%r11),%xmm2
	pand	64(%rax),%xmm0
	movdqa	112(%r11),%xmm3
	pand	80(%rax),%xmm1
	por	%xmm0,%xmm4
	pand	96(%rax),%xmm2
	por	%xmm1,%xmm5
	pand	112(%rax),%xmm3
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	por	%xmm5,%xmm4
	leaq	256(%r11),%r11
	pshufd	$0x4e,%xmm4,%xmm0
	por	%xmm4,%xmm0
	movq	%xmm0,(%rdi)
	leaq	8(%rdi),%rdi
	subl	$1,%esi
	jnz	.Lgather

	leaq	(%r10),%rsp
	.byte	0xf3,0xc3
.LSEH_end_bn_gather5:
.size	bn_gather5,.-bn_gather5
.align	64
.Linc:
.long	0,0, 1,1
.long	2,2, 2,2
.byte	77,111,110,116,103,111,109,101,114,121,32,77,117,108,116,105,112,108,105,99,97,116,105,111,110,32,119,105,116,104,32,115,99,97,116,116,101,114,47,103,97,116,104,101,114,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
