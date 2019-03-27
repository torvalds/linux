/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from x86_64-mont.pl. */
.text	



.globl	bn_mul_mont
.type	bn_mul_mont,@function
.align	16
bn_mul_mont:
.cfi_startproc	
	movl	%r9d,%r9d
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
	testl	$3,%r9d
	jnz	.Lmul_enter
	cmpl	$8,%r9d
	jb	.Lmul_enter
	movl	OPENSSL_ia32cap_P+8(%rip),%r11d
	cmpq	%rsi,%rdx
	jne	.Lmul4x_enter
	testl	$7,%r9d
	jz	.Lsqr8x_enter
	jmp	.Lmul4x_enter

.align	16
.Lmul_enter:
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
	leaq	-16(%rsp,%r9,8),%r10
	negq	%r9
	andq	$-1024,%r10









	subq	%r10,%r11
	andq	$-4096,%r11
	leaq	(%r10,%r11,1),%rsp
	movq	(%rsp),%r11
	cmpq	%r10,%rsp
	ja	.Lmul_page_walk
	jmp	.Lmul_page_walk_done

.align	16
.Lmul_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r11
	cmpq	%r10,%rsp
	ja	.Lmul_page_walk
.Lmul_page_walk_done:

	movq	%rax,8(%rsp,%r9,8)
.cfi_escape	0x0f,0x0a,0x77,0x08,0x79,0x00,0x38,0x1e,0x22,0x06,0x23,0x08
.Lmul_body:
	movq	%rdx,%r12
	movq	(%r8),%r8
	movq	(%r12),%rbx
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
	movq	(%rsi),%rax
	adcq	$0,%rdx
	addq	%r11,%r13
	adcq	$0,%rdx
	movq	%r13,-16(%rsp,%r15,8)
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
	movq	(%r12,%r14,8),%rbx
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
	movq	(%rsi),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	movq	(%rsp,%r15,8),%r10
	adcq	$0,%rdx
	movq	%r13,-16(%rsp,%r15,8)
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
	movq	%r9,%r15

.align	16
.Lsub:	sbbq	(%rcx,%r14,8),%rax
	movq	%rax,(%rdi,%r14,8)
	movq	8(%rsp,%r14,8),%rax
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
	movq	%r9,(%rsp,%r14,8)
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
.size	bn_mul_mont,.-bn_mul_mont
.type	bn_mul4x_mont,@function
.align	16
bn_mul4x_mont:
.cfi_startproc	
	movl	%r9d,%r9d
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
.Lmul4x_enter:
	andl	$0x80100,%r11d
	cmpl	$0x80100,%r11d
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

	negq	%r9
	movq	%rsp,%r11
	leaq	-32(%rsp,%r9,8),%r10
	negq	%r9
	andq	$-1024,%r10

	subq	%r10,%r11
	andq	$-4096,%r11
	leaq	(%r10,%r11,1),%rsp
	movq	(%rsp),%r11
	cmpq	%r10,%rsp
	ja	.Lmul4x_page_walk
	jmp	.Lmul4x_page_walk_done

.Lmul4x_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r11
	cmpq	%r10,%rsp
	ja	.Lmul4x_page_walk
.Lmul4x_page_walk_done:

	movq	%rax,8(%rsp,%r9,8)
.cfi_escape	0x0f,0x0a,0x77,0x08,0x79,0x00,0x38,0x1e,0x22,0x06,0x23,0x08
.Lmul4x_body:
	movq	%rdi,16(%rsp,%r9,8)
	movq	%rdx,%r12
	movq	(%r8),%r8
	movq	(%r12),%rbx
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
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	16(%rsi),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	leaq	4(%r15),%r15
	adcq	$0,%rdx
	movq	%rdi,(%rsp)
	movq	%rdx,%r13
	jmp	.L1st4x
.align	16
.L1st4x:
	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-24(%rsp,%r15,8)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-16(%rsp,%r15,8)
	movq	%rdx,%r13

	mulq	%rbx
	addq	%rax,%r10
	movq	(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	8(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-8(%rsp,%r15,8)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	leaq	4(%r15),%r15
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	-16(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-32(%rsp,%r15,8)
	movq	%rdx,%r13
	cmpq	%r9,%r15
	jb	.L1st4x

	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-24(%rsp,%r15,8)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-16(%rsp,%r15,8)
	movq	%rdx,%r13

	xorq	%rdi,%rdi
	addq	%r10,%r13
	adcq	$0,%rdi
	movq	%r13,-8(%rsp,%r15,8)
	movq	%rdi,(%rsp,%r15,8)

	leaq	1(%r14),%r14
.align	4
.Louter4x:
	movq	(%r12,%r14,8),%rbx
	xorq	%r15,%r15
	movq	(%rsp),%r10
	movq	%r8,%rbp
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
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx),%rax
	adcq	$0,%rdx
	addq	8(%rsp),%r11
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	16(%rsi),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	leaq	4(%r15),%r15
	adcq	$0,%rdx
	movq	%rdi,(%rsp)
	movq	%rdx,%r13
	jmp	.Linner4x
.align	16
.Linner4x:
	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	-16(%rsp,%r15,8),%r10
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-24(%rsp,%r15,8)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	-8(%rsp,%r15,8),%r11
	adcq	$0,%rdx
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-16(%rsp,%r15,8)
	movq	%rdx,%r13

	mulq	%rbx
	addq	%rax,%r10
	movq	(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	(%rsp,%r15,8),%r10
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	8(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-8(%rsp,%r15,8)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	8(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	8(%rsp,%r15,8),%r11
	adcq	$0,%rdx
	leaq	4(%r15),%r15
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	-16(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-32(%rsp,%r15,8)
	movq	%rdx,%r13
	cmpq	%r9,%r15
	jb	.Linner4x

	mulq	%rbx
	addq	%rax,%r10
	movq	-16(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	-16(%rsp,%r15,8),%r10
	adcq	$0,%rdx
	movq	%rdx,%r11

	mulq	%rbp
	addq	%rax,%r13
	movq	-8(%rsi,%r15,8),%rax
	adcq	$0,%rdx
	addq	%r10,%r13
	adcq	$0,%rdx
	movq	%r13,-24(%rsp,%r15,8)
	movq	%rdx,%rdi

	mulq	%rbx
	addq	%rax,%r11
	movq	-8(%rcx,%r15,8),%rax
	adcq	$0,%rdx
	addq	-8(%rsp,%r15,8),%r11
	adcq	$0,%rdx
	leaq	1(%r14),%r14
	movq	%rdx,%r10

	mulq	%rbp
	addq	%rax,%rdi
	movq	(%rsi),%rax
	adcq	$0,%rdx
	addq	%r11,%rdi
	adcq	$0,%rdx
	movq	%rdi,-16(%rsp,%r15,8)
	movq	%rdx,%r13

	xorq	%rdi,%rdi
	addq	%r10,%r13
	adcq	$0,%rdi
	addq	(%rsp,%r9,8),%r13
	adcq	$0,%rdi
	movq	%r13,-8(%rsp,%r15,8)
	movq	%rdi,(%rsp,%r15,8)

	cmpq	%r9,%r14
	jb	.Louter4x
	movq	16(%rsp,%r9,8),%rdi
	leaq	-4(%r9),%r15
	movq	0(%rsp),%rax
	movq	8(%rsp),%rdx
	shrq	$2,%r15
	leaq	(%rsp),%rsi
	xorq	%r14,%r14

	subq	0(%rcx),%rax
	movq	16(%rsi),%rbx
	movq	24(%rsi),%rbp
	sbbq	8(%rcx),%rdx

.Lsub4x:
	movq	%rax,0(%rdi,%r14,8)
	movq	%rdx,8(%rdi,%r14,8)
	sbbq	16(%rcx,%r14,8),%rbx
	movq	32(%rsi,%r14,8),%rax
	movq	40(%rsi,%r14,8),%rdx
	sbbq	24(%rcx,%r14,8),%rbp
	movq	%rbx,16(%rdi,%r14,8)
	movq	%rbp,24(%rdi,%r14,8)
	sbbq	32(%rcx,%r14,8),%rax
	movq	48(%rsi,%r14,8),%rbx
	movq	56(%rsi,%r14,8),%rbp
	sbbq	40(%rcx,%r14,8),%rdx
	leaq	4(%r14),%r14
	decq	%r15
	jnz	.Lsub4x

	movq	%rax,0(%rdi,%r14,8)
	movq	32(%rsi,%r14,8),%rax
	sbbq	16(%rcx,%r14,8),%rbx
	movq	%rdx,8(%rdi,%r14,8)
	sbbq	24(%rcx,%r14,8),%rbp
	movq	%rbx,16(%rdi,%r14,8)

	sbbq	$0,%rax
	movq	%rbp,24(%rdi,%r14,8)
	pxor	%xmm0,%xmm0
.byte	102,72,15,110,224
	pcmpeqd	%xmm5,%xmm5
	pshufd	$0,%xmm4,%xmm4
	movq	%r9,%r15
	pxor	%xmm4,%xmm5
	shrq	$2,%r15
	xorl	%eax,%eax

	jmp	.Lcopy4x
.align	16
.Lcopy4x:
	movdqa	(%rsp,%rax,1),%xmm1
	movdqu	(%rdi,%rax,1),%xmm2
	pand	%xmm4,%xmm1
	pand	%xmm5,%xmm2
	movdqa	16(%rsp,%rax,1),%xmm3
	movdqa	%xmm0,(%rsp,%rax,1)
	por	%xmm2,%xmm1
	movdqu	16(%rdi,%rax,1),%xmm2
	movdqu	%xmm1,(%rdi,%rax,1)
	pand	%xmm4,%xmm3
	pand	%xmm5,%xmm2
	movdqa	%xmm0,16(%rsp,%rax,1)
	por	%xmm2,%xmm3
	movdqu	%xmm3,16(%rdi,%rax,1)
	leaq	32(%rax),%rax
	decq	%r15
	jnz	.Lcopy4x
	movq	8(%rsp,%r9,8),%rsi
.cfi_def_cfa	%rsi, 8
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
.size	bn_mul4x_mont,.-bn_mul4x_mont



.type	bn_sqr8x_mont,@function
.align	32
bn_sqr8x_mont:
.cfi_startproc	
	movq	%rsp,%rax
.cfi_def_cfa_register	%rax
.Lsqr8x_enter:
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
.Lsqr8x_prologue:

	movl	%r9d,%r10d
	shll	$3,%r9d
	shlq	$3+2,%r10
	negq	%r9






	leaq	-64(%rsp,%r9,2),%r11
	movq	%rsp,%rbp
	movq	(%r8),%r8
	subq	%rsi,%r11
	andq	$4095,%r11
	cmpq	%r11,%r10
	jb	.Lsqr8x_sp_alt
	subq	%r11,%rbp
	leaq	-64(%rbp,%r9,2),%rbp
	jmp	.Lsqr8x_sp_done

.align	32
.Lsqr8x_sp_alt:
	leaq	4096-64(,%r9,2),%r10
	leaq	-64(%rbp,%r9,2),%rbp
	subq	%r10,%r11
	movq	$0,%r10
	cmovcq	%r10,%r11
	subq	%r11,%rbp
.Lsqr8x_sp_done:
	andq	$-64,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lsqr8x_page_walk
	jmp	.Lsqr8x_page_walk_done

.align	16
.Lsqr8x_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lsqr8x_page_walk
.Lsqr8x_page_walk_done:

	movq	%r9,%r10
	negq	%r9

	movq	%r8,32(%rsp)
	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
.Lsqr8x_body:

.byte	102,72,15,110,209
	pxor	%xmm0,%xmm0
.byte	102,72,15,110,207
.byte	102,73,15,110,218
	movl	OPENSSL_ia32cap_P+8(%rip),%eax
	andl	$0x80100,%eax
	cmpl	$0x80100,%eax
	jne	.Lsqr8x_nox

	call	bn_sqrx8x_internal




	leaq	(%r8,%rcx,1),%rbx
	movq	%rcx,%r9
	movq	%rcx,%rdx
.byte	102,72,15,126,207
	sarq	$3+2,%rcx
	jmp	.Lsqr8x_sub

.align	32
.Lsqr8x_nox:
	call	bn_sqr8x_internal




	leaq	(%rdi,%r9,1),%rbx
	movq	%r9,%rcx
	movq	%r9,%rdx
.byte	102,72,15,126,207
	sarq	$3+2,%rcx
	jmp	.Lsqr8x_sub

.align	32
.Lsqr8x_sub:
	movq	0(%rbx),%r12
	movq	8(%rbx),%r13
	movq	16(%rbx),%r14
	movq	24(%rbx),%r15
	leaq	32(%rbx),%rbx
	sbbq	0(%rbp),%r12
	sbbq	8(%rbp),%r13
	sbbq	16(%rbp),%r14
	sbbq	24(%rbp),%r15
	leaq	32(%rbp),%rbp
	movq	%r12,0(%rdi)
	movq	%r13,8(%rdi)
	movq	%r14,16(%rdi)
	movq	%r15,24(%rdi)
	leaq	32(%rdi),%rdi
	incq	%rcx
	jnz	.Lsqr8x_sub

	sbbq	$0,%rax
	leaq	(%rbx,%r9,1),%rbx
	leaq	(%rdi,%r9,1),%rdi

.byte	102,72,15,110,200
	pxor	%xmm0,%xmm0
	pshufd	$0,%xmm1,%xmm1
	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	jmp	.Lsqr8x_cond_copy

.align	32
.Lsqr8x_cond_copy:
	movdqa	0(%rbx),%xmm2
	movdqa	16(%rbx),%xmm3
	leaq	32(%rbx),%rbx
	movdqu	0(%rdi),%xmm4
	movdqu	16(%rdi),%xmm5
	leaq	32(%rdi),%rdi
	movdqa	%xmm0,-32(%rbx)
	movdqa	%xmm0,-16(%rbx)
	movdqa	%xmm0,-32(%rbx,%rdx,1)
	movdqa	%xmm0,-16(%rbx,%rdx,1)
	pcmpeqd	%xmm1,%xmm0
	pand	%xmm1,%xmm2
	pand	%xmm1,%xmm3
	pand	%xmm0,%xmm4
	pand	%xmm0,%xmm5
	pxor	%xmm0,%xmm0
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqu	%xmm4,-32(%rdi)
	movdqu	%xmm5,-16(%rdi)
	addq	$32,%r9
	jnz	.Lsqr8x_cond_copy

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
.Lsqr8x_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bn_sqr8x_mont,.-bn_sqr8x_mont
.type	bn_mulx4x_mont,@function
.align	32
bn_mulx4x_mont:
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
	xorq	%r10,%r10
	subq	%r9,%r10
	movq	(%r8),%r8
	leaq	-72(%rsp,%r10,1),%rbp
	andq	$-128,%rbp
	movq	%rsp,%r11
	subq	%rbp,%r11
	andq	$-4096,%r11
	leaq	(%r11,%rbp,1),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lmulx4x_page_walk
	jmp	.Lmulx4x_page_walk_done

.align	16
.Lmulx4x_page_walk:
	leaq	-4096(%rsp),%rsp
	movq	(%rsp),%r10
	cmpq	%rbp,%rsp
	ja	.Lmulx4x_page_walk
.Lmulx4x_page_walk_done:

	leaq	(%rdx,%r9,1),%r10












	movq	%r9,0(%rsp)
	shrq	$5,%r9
	movq	%r10,16(%rsp)
	subq	$1,%r9
	movq	%r8,24(%rsp)
	movq	%rdi,32(%rsp)
	movq	%rax,40(%rsp)
.cfi_escape	0x0f,0x05,0x77,0x28,0x06,0x23,0x08
	movq	%r9,48(%rsp)
	jmp	.Lmulx4x_body

.align	32
.Lmulx4x_body:
	leaq	8(%rdx),%rdi
	movq	(%rdx),%rdx
	leaq	64+32(%rsp),%rbx
	movq	%rdx,%r9

	mulxq	0(%rsi),%r8,%rax
	mulxq	8(%rsi),%r11,%r14
	addq	%rax,%r11
	movq	%rdi,8(%rsp)
	mulxq	16(%rsi),%r12,%r13
	adcq	%r14,%r12
	adcq	$0,%r13

	movq	%r8,%rdi
	imulq	24(%rsp),%r8
	xorq	%rbp,%rbp

	mulxq	24(%rsi),%rax,%r14
	movq	%r8,%rdx
	leaq	32(%rsi),%rsi
	adcxq	%rax,%r13
	adcxq	%rbp,%r14

	mulxq	0(%rcx),%rax,%r10
	adcxq	%rax,%rdi
	adoxq	%r11,%r10
	mulxq	8(%rcx),%rax,%r11
	adcxq	%rax,%r10
	adoxq	%r12,%r11
.byte	0xc4,0x62,0xfb,0xf6,0xa1,0x10,0x00,0x00,0x00
	movq	48(%rsp),%rdi
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

	movq	0(%rsp),%rax
	movq	8(%rsp),%rdi
	adcq	%rbp,%r15
	addq	%r15,%r14
	sbbq	%r15,%r15
	movq	%r14,-8(%rbx)
	jmp	.Lmulx4x_outer

.align	32
.Lmulx4x_outer:
	movq	(%rdi),%rdx
	leaq	8(%rdi),%rdi
	subq	%rax,%rsi
	movq	%r15,(%rbx)
	leaq	64+32(%rsp),%rbx
	subq	%rax,%rcx

	mulxq	0(%rsi),%r8,%r11
	xorl	%ebp,%ebp
	movq	%rdx,%r9
	mulxq	8(%rsi),%r14,%r12
	adoxq	-32(%rbx),%r8
	adcxq	%r14,%r11
	mulxq	16(%rsi),%r15,%r13
	adoxq	-24(%rbx),%r11
	adcxq	%r15,%r12
	adoxq	-16(%rbx),%r12
	adcxq	%rbp,%r13
	adoxq	%rbp,%r13

	movq	%rdi,8(%rsp)
	movq	%r8,%r15
	imulq	24(%rsp),%r8
	xorl	%ebp,%ebp

	mulxq	24(%rsi),%rax,%r14
	movq	%r8,%rdx
	adcxq	%rax,%r13
	adoxq	-8(%rbx),%r13
	adcxq	%rbp,%r14
	leaq	32(%rsi),%rsi
	adoxq	%rbp,%r14

	mulxq	0(%rcx),%rax,%r10
	adcxq	%rax,%r15
	adoxq	%r11,%r10
	mulxq	8(%rcx),%rax,%r11
	adcxq	%rax,%r10
	adoxq	%r12,%r11
	mulxq	16(%rcx),%rax,%r12
	movq	%r10,-32(%rbx)
	adcxq	%rax,%r11
	adoxq	%r13,%r12
	mulxq	24(%rcx),%rax,%r15
	movq	%r9,%rdx
	movq	%r11,-24(%rbx)
	leaq	32(%rcx),%rcx
	adcxq	%rax,%r12
	adoxq	%rbp,%r15
	movq	48(%rsp),%rdi
	movq	%r12,-16(%rbx)

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
	mulxq	24(%rcx),%rax,%r15
	movq	%r9,%rdx
	movq	%r11,-32(%rbx)
	movq	%r12,-24(%rbx)
	adcxq	%rax,%r13
	adoxq	%rbp,%r15
	leaq	32(%rcx),%rcx
	movq	%r13,-16(%rbx)

	decq	%rdi
	jnz	.Lmulx4x_inner

	movq	0(%rsp),%rax
	movq	8(%rsp),%rdi
	adcq	%rbp,%r15
	subq	0(%rbx),%rbp
	adcq	%r15,%r14
	sbbq	%r15,%r15
	movq	%r14,-8(%rbx)

	cmpq	16(%rsp),%rdi
	jne	.Lmulx4x_outer

	leaq	64(%rsp),%rbx
	subq	%rax,%rcx
	negq	%r15
	movq	%rax,%rdx
	shrq	$3+2,%rax
	movq	32(%rsp),%rdi
	jmp	.Lmulx4x_sub

.align	32
.Lmulx4x_sub:
	movq	0(%rbx),%r11
	movq	8(%rbx),%r12
	movq	16(%rbx),%r13
	movq	24(%rbx),%r14
	leaq	32(%rbx),%rbx
	sbbq	0(%rcx),%r11
	sbbq	8(%rcx),%r12
	sbbq	16(%rcx),%r13
	sbbq	24(%rcx),%r14
	leaq	32(%rcx),%rcx
	movq	%r11,0(%rdi)
	movq	%r12,8(%rdi)
	movq	%r13,16(%rdi)
	movq	%r14,24(%rdi)
	leaq	32(%rdi),%rdi
	decq	%rax
	jnz	.Lmulx4x_sub

	sbbq	$0,%r15
	leaq	64(%rsp),%rbx
	subq	%rdx,%rdi

.byte	102,73,15,110,207
	pxor	%xmm0,%xmm0
	pshufd	$0,%xmm1,%xmm1
	movq	40(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	jmp	.Lmulx4x_cond_copy

.align	32
.Lmulx4x_cond_copy:
	movdqa	0(%rbx),%xmm2
	movdqa	16(%rbx),%xmm3
	leaq	32(%rbx),%rbx
	movdqu	0(%rdi),%xmm4
	movdqu	16(%rdi),%xmm5
	leaq	32(%rdi),%rdi
	movdqa	%xmm0,-32(%rbx)
	movdqa	%xmm0,-16(%rbx)
	pcmpeqd	%xmm1,%xmm0
	pand	%xmm1,%xmm2
	pand	%xmm1,%xmm3
	pand	%xmm0,%xmm4
	pand	%xmm0,%xmm5
	pxor	%xmm0,%xmm0
	por	%xmm2,%xmm4
	por	%xmm3,%xmm5
	movdqu	%xmm4,-32(%rdi)
	movdqu	%xmm5,-16(%rdi)
	subq	$32,%rdx
	jnz	.Lmulx4x_cond_copy

	movq	%rdx,(%rbx)

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
.size	bn_mulx4x_mont,.-bn_mulx4x_mont
.byte	77,111,110,116,103,111,109,101,114,121,32,77,117,108,116,105,112,108,105,99,97,116,105,111,110,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	16
