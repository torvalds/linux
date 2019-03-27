/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from poly1305-x86_64.pl. */
.text	



.globl	poly1305_init
.hidden	poly1305_init
.globl	poly1305_blocks
.hidden	poly1305_blocks
.globl	poly1305_emit
.hidden	poly1305_emit

.type	poly1305_init,@function
.align	32
poly1305_init:
	xorq	%rax,%rax
	movq	%rax,0(%rdi)
	movq	%rax,8(%rdi)
	movq	%rax,16(%rdi)

	cmpq	$0,%rsi
	je	.Lno_key

	leaq	poly1305_blocks(%rip),%r10
	leaq	poly1305_emit(%rip),%r11
	movq	OPENSSL_ia32cap_P+4(%rip),%r9
	leaq	poly1305_blocks_avx(%rip),%rax
	leaq	poly1305_emit_avx(%rip),%rcx
	btq	$28,%r9
	cmovcq	%rax,%r10
	cmovcq	%rcx,%r11
	leaq	poly1305_blocks_avx2(%rip),%rax
	btq	$37,%r9
	cmovcq	%rax,%r10
	movq	$0x0ffffffc0fffffff,%rax
	movq	$0x0ffffffc0ffffffc,%rcx
	andq	0(%rsi),%rax
	andq	8(%rsi),%rcx
	movq	%rax,24(%rdi)
	movq	%rcx,32(%rdi)
	movq	%r10,0(%rdx)
	movq	%r11,8(%rdx)
	movl	$1,%eax
.Lno_key:
	.byte	0xf3,0xc3
.size	poly1305_init,.-poly1305_init

.type	poly1305_blocks,@function
.align	32
poly1305_blocks:
.cfi_startproc	
.Lblocks:
	shrq	$4,%rdx
	jz	.Lno_data

	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-24
	pushq	%r12
.cfi_adjust_cfa_offset	8
.cfi_offset	%r12,-32
	pushq	%r13
.cfi_adjust_cfa_offset	8
.cfi_offset	%r13,-40
	pushq	%r14
.cfi_adjust_cfa_offset	8
.cfi_offset	%r14,-48
	pushq	%r15
.cfi_adjust_cfa_offset	8
.cfi_offset	%r15,-56
.Lblocks_body:

	movq	%rdx,%r15

	movq	24(%rdi),%r11
	movq	32(%rdi),%r13

	movq	0(%rdi),%r14
	movq	8(%rdi),%rbx
	movq	16(%rdi),%rbp

	movq	%r13,%r12
	shrq	$2,%r13
	movq	%r12,%rax
	addq	%r12,%r13
	jmp	.Loop

.align	32
.Loop:
	addq	0(%rsi),%r14
	adcq	8(%rsi),%rbx
	leaq	16(%rsi),%rsi
	adcq	%rcx,%rbp
	mulq	%r14
	movq	%rax,%r9
	movq	%r11,%rax
	movq	%rdx,%r10

	mulq	%r14
	movq	%rax,%r14
	movq	%r11,%rax
	movq	%rdx,%r8

	mulq	%rbx
	addq	%rax,%r9
	movq	%r13,%rax
	adcq	%rdx,%r10

	mulq	%rbx
	movq	%rbp,%rbx
	addq	%rax,%r14
	adcq	%rdx,%r8

	imulq	%r13,%rbx
	addq	%rbx,%r9
	movq	%r8,%rbx
	adcq	$0,%r10

	imulq	%r11,%rbp
	addq	%r9,%rbx
	movq	$-4,%rax
	adcq	%rbp,%r10

	andq	%r10,%rax
	movq	%r10,%rbp
	shrq	$2,%r10
	andq	$3,%rbp
	addq	%r10,%rax
	addq	%rax,%r14
	adcq	$0,%rbx
	adcq	$0,%rbp
	movq	%r12,%rax
	decq	%r15
	jnz	.Loop

	movq	%r14,0(%rdi)
	movq	%rbx,8(%rdi)
	movq	%rbp,16(%rdi)

	movq	0(%rsp),%r15
.cfi_restore	%r15
	movq	8(%rsp),%r14
.cfi_restore	%r14
	movq	16(%rsp),%r13
.cfi_restore	%r13
	movq	24(%rsp),%r12
.cfi_restore	%r12
	movq	32(%rsp),%rbp
.cfi_restore	%rbp
	movq	40(%rsp),%rbx
.cfi_restore	%rbx
	leaq	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lno_data:
.Lblocks_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	poly1305_blocks,.-poly1305_blocks

.type	poly1305_emit,@function
.align	32
poly1305_emit:
.Lemit:
	movq	0(%rdi),%r8
	movq	8(%rdi),%r9
	movq	16(%rdi),%r10

	movq	%r8,%rax
	addq	$5,%r8
	movq	%r9,%rcx
	adcq	$0,%r9
	adcq	$0,%r10
	shrq	$2,%r10
	cmovnzq	%r8,%rax
	cmovnzq	%r9,%rcx

	addq	0(%rdx),%rax
	adcq	8(%rdx),%rcx
	movq	%rax,0(%rsi)
	movq	%rcx,8(%rsi)

	.byte	0xf3,0xc3
.size	poly1305_emit,.-poly1305_emit
.type	__poly1305_block,@function
.align	32
__poly1305_block:
	mulq	%r14
	movq	%rax,%r9
	movq	%r11,%rax
	movq	%rdx,%r10

	mulq	%r14
	movq	%rax,%r14
	movq	%r11,%rax
	movq	%rdx,%r8

	mulq	%rbx
	addq	%rax,%r9
	movq	%r13,%rax
	adcq	%rdx,%r10

	mulq	%rbx
	movq	%rbp,%rbx
	addq	%rax,%r14
	adcq	%rdx,%r8

	imulq	%r13,%rbx
	addq	%rbx,%r9
	movq	%r8,%rbx
	adcq	$0,%r10

	imulq	%r11,%rbp
	addq	%r9,%rbx
	movq	$-4,%rax
	adcq	%rbp,%r10

	andq	%r10,%rax
	movq	%r10,%rbp
	shrq	$2,%r10
	andq	$3,%rbp
	addq	%r10,%rax
	addq	%rax,%r14
	adcq	$0,%rbx
	adcq	$0,%rbp
	.byte	0xf3,0xc3
.size	__poly1305_block,.-__poly1305_block

.type	__poly1305_init_avx,@function
.align	32
__poly1305_init_avx:
	movq	%r11,%r14
	movq	%r12,%rbx
	xorq	%rbp,%rbp

	leaq	48+64(%rdi),%rdi

	movq	%r12,%rax
	call	__poly1305_block

	movl	$0x3ffffff,%eax
	movl	$0x3ffffff,%edx
	movq	%r14,%r8
	andl	%r14d,%eax
	movq	%r11,%r9
	andl	%r11d,%edx
	movl	%eax,-64(%rdi)
	shrq	$26,%r8
	movl	%edx,-60(%rdi)
	shrq	$26,%r9

	movl	$0x3ffffff,%eax
	movl	$0x3ffffff,%edx
	andl	%r8d,%eax
	andl	%r9d,%edx
	movl	%eax,-48(%rdi)
	leal	(%rax,%rax,4),%eax
	movl	%edx,-44(%rdi)
	leal	(%rdx,%rdx,4),%edx
	movl	%eax,-32(%rdi)
	shrq	$26,%r8
	movl	%edx,-28(%rdi)
	shrq	$26,%r9

	movq	%rbx,%rax
	movq	%r12,%rdx
	shlq	$12,%rax
	shlq	$12,%rdx
	orq	%r8,%rax
	orq	%r9,%rdx
	andl	$0x3ffffff,%eax
	andl	$0x3ffffff,%edx
	movl	%eax,-16(%rdi)
	leal	(%rax,%rax,4),%eax
	movl	%edx,-12(%rdi)
	leal	(%rdx,%rdx,4),%edx
	movl	%eax,0(%rdi)
	movq	%rbx,%r8
	movl	%edx,4(%rdi)
	movq	%r12,%r9

	movl	$0x3ffffff,%eax
	movl	$0x3ffffff,%edx
	shrq	$14,%r8
	shrq	$14,%r9
	andl	%r8d,%eax
	andl	%r9d,%edx
	movl	%eax,16(%rdi)
	leal	(%rax,%rax,4),%eax
	movl	%edx,20(%rdi)
	leal	(%rdx,%rdx,4),%edx
	movl	%eax,32(%rdi)
	shrq	$26,%r8
	movl	%edx,36(%rdi)
	shrq	$26,%r9

	movq	%rbp,%rax
	shlq	$24,%rax
	orq	%rax,%r8
	movl	%r8d,48(%rdi)
	leaq	(%r8,%r8,4),%r8
	movl	%r9d,52(%rdi)
	leaq	(%r9,%r9,4),%r9
	movl	%r8d,64(%rdi)
	movl	%r9d,68(%rdi)

	movq	%r12,%rax
	call	__poly1305_block

	movl	$0x3ffffff,%eax
	movq	%r14,%r8
	andl	%r14d,%eax
	shrq	$26,%r8
	movl	%eax,-52(%rdi)

	movl	$0x3ffffff,%edx
	andl	%r8d,%edx
	movl	%edx,-36(%rdi)
	leal	(%rdx,%rdx,4),%edx
	shrq	$26,%r8
	movl	%edx,-20(%rdi)

	movq	%rbx,%rax
	shlq	$12,%rax
	orq	%r8,%rax
	andl	$0x3ffffff,%eax
	movl	%eax,-4(%rdi)
	leal	(%rax,%rax,4),%eax
	movq	%rbx,%r8
	movl	%eax,12(%rdi)

	movl	$0x3ffffff,%edx
	shrq	$14,%r8
	andl	%r8d,%edx
	movl	%edx,28(%rdi)
	leal	(%rdx,%rdx,4),%edx
	shrq	$26,%r8
	movl	%edx,44(%rdi)

	movq	%rbp,%rax
	shlq	$24,%rax
	orq	%rax,%r8
	movl	%r8d,60(%rdi)
	leaq	(%r8,%r8,4),%r8
	movl	%r8d,76(%rdi)

	movq	%r12,%rax
	call	__poly1305_block

	movl	$0x3ffffff,%eax
	movq	%r14,%r8
	andl	%r14d,%eax
	shrq	$26,%r8
	movl	%eax,-56(%rdi)

	movl	$0x3ffffff,%edx
	andl	%r8d,%edx
	movl	%edx,-40(%rdi)
	leal	(%rdx,%rdx,4),%edx
	shrq	$26,%r8
	movl	%edx,-24(%rdi)

	movq	%rbx,%rax
	shlq	$12,%rax
	orq	%r8,%rax
	andl	$0x3ffffff,%eax
	movl	%eax,-8(%rdi)
	leal	(%rax,%rax,4),%eax
	movq	%rbx,%r8
	movl	%eax,8(%rdi)

	movl	$0x3ffffff,%edx
	shrq	$14,%r8
	andl	%r8d,%edx
	movl	%edx,24(%rdi)
	leal	(%rdx,%rdx,4),%edx
	shrq	$26,%r8
	movl	%edx,40(%rdi)

	movq	%rbp,%rax
	shlq	$24,%rax
	orq	%rax,%r8
	movl	%r8d,56(%rdi)
	leaq	(%r8,%r8,4),%r8
	movl	%r8d,72(%rdi)

	leaq	-48-64(%rdi),%rdi
	.byte	0xf3,0xc3
.size	__poly1305_init_avx,.-__poly1305_init_avx

.type	poly1305_blocks_avx,@function
.align	32
poly1305_blocks_avx:
.cfi_startproc	
	movl	20(%rdi),%r8d
	cmpq	$128,%rdx
	jae	.Lblocks_avx
	testl	%r8d,%r8d
	jz	.Lblocks

.Lblocks_avx:
	andq	$-16,%rdx
	jz	.Lno_data_avx

	vzeroupper

	testl	%r8d,%r8d
	jz	.Lbase2_64_avx

	testq	$31,%rdx
	jz	.Leven_avx

	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-24
	pushq	%r12
.cfi_adjust_cfa_offset	8
.cfi_offset	%r12,-32
	pushq	%r13
.cfi_adjust_cfa_offset	8
.cfi_offset	%r13,-40
	pushq	%r14
.cfi_adjust_cfa_offset	8
.cfi_offset	%r14,-48
	pushq	%r15
.cfi_adjust_cfa_offset	8
.cfi_offset	%r15,-56
.Lblocks_avx_body:

	movq	%rdx,%r15

	movq	0(%rdi),%r8
	movq	8(%rdi),%r9
	movl	16(%rdi),%ebp

	movq	24(%rdi),%r11
	movq	32(%rdi),%r13


	movl	%r8d,%r14d
	andq	$-2147483648,%r8
	movq	%r9,%r12
	movl	%r9d,%ebx
	andq	$-2147483648,%r9

	shrq	$6,%r8
	shlq	$52,%r12
	addq	%r8,%r14
	shrq	$12,%rbx
	shrq	$18,%r9
	addq	%r12,%r14
	adcq	%r9,%rbx

	movq	%rbp,%r8
	shlq	$40,%r8
	shrq	$24,%rbp
	addq	%r8,%rbx
	adcq	$0,%rbp

	movq	$-4,%r9
	movq	%rbp,%r8
	andq	%rbp,%r9
	shrq	$2,%r8
	andq	$3,%rbp
	addq	%r9,%r8
	addq	%r8,%r14
	adcq	$0,%rbx
	adcq	$0,%rbp

	movq	%r13,%r12
	movq	%r13,%rax
	shrq	$2,%r13
	addq	%r12,%r13

	addq	0(%rsi),%r14
	adcq	8(%rsi),%rbx
	leaq	16(%rsi),%rsi
	adcq	%rcx,%rbp

	call	__poly1305_block

	testq	%rcx,%rcx
	jz	.Lstore_base2_64_avx


	movq	%r14,%rax
	movq	%r14,%rdx
	shrq	$52,%r14
	movq	%rbx,%r11
	movq	%rbx,%r12
	shrq	$26,%rdx
	andq	$0x3ffffff,%rax
	shlq	$12,%r11
	andq	$0x3ffffff,%rdx
	shrq	$14,%rbx
	orq	%r11,%r14
	shlq	$24,%rbp
	andq	$0x3ffffff,%r14
	shrq	$40,%r12
	andq	$0x3ffffff,%rbx
	orq	%r12,%rbp

	subq	$16,%r15
	jz	.Lstore_base2_26_avx

	vmovd	%eax,%xmm0
	vmovd	%edx,%xmm1
	vmovd	%r14d,%xmm2
	vmovd	%ebx,%xmm3
	vmovd	%ebp,%xmm4
	jmp	.Lproceed_avx

.align	32
.Lstore_base2_64_avx:
	movq	%r14,0(%rdi)
	movq	%rbx,8(%rdi)
	movq	%rbp,16(%rdi)
	jmp	.Ldone_avx

.align	16
.Lstore_base2_26_avx:
	movl	%eax,0(%rdi)
	movl	%edx,4(%rdi)
	movl	%r14d,8(%rdi)
	movl	%ebx,12(%rdi)
	movl	%ebp,16(%rdi)
.align	16
.Ldone_avx:
	movq	0(%rsp),%r15
.cfi_restore	%r15
	movq	8(%rsp),%r14
.cfi_restore	%r14
	movq	16(%rsp),%r13
.cfi_restore	%r13
	movq	24(%rsp),%r12
.cfi_restore	%r12
	movq	32(%rsp),%rbp
.cfi_restore	%rbp
	movq	40(%rsp),%rbx
.cfi_restore	%rbx
	leaq	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lno_data_avx:
.Lblocks_avx_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	

.align	32
.Lbase2_64_avx:
.cfi_startproc	
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-24
	pushq	%r12
.cfi_adjust_cfa_offset	8
.cfi_offset	%r12,-32
	pushq	%r13
.cfi_adjust_cfa_offset	8
.cfi_offset	%r13,-40
	pushq	%r14
.cfi_adjust_cfa_offset	8
.cfi_offset	%r14,-48
	pushq	%r15
.cfi_adjust_cfa_offset	8
.cfi_offset	%r15,-56
.Lbase2_64_avx_body:

	movq	%rdx,%r15

	movq	24(%rdi),%r11
	movq	32(%rdi),%r13

	movq	0(%rdi),%r14
	movq	8(%rdi),%rbx
	movl	16(%rdi),%ebp

	movq	%r13,%r12
	movq	%r13,%rax
	shrq	$2,%r13
	addq	%r12,%r13

	testq	$31,%rdx
	jz	.Linit_avx

	addq	0(%rsi),%r14
	adcq	8(%rsi),%rbx
	leaq	16(%rsi),%rsi
	adcq	%rcx,%rbp
	subq	$16,%r15

	call	__poly1305_block

.Linit_avx:

	movq	%r14,%rax
	movq	%r14,%rdx
	shrq	$52,%r14
	movq	%rbx,%r8
	movq	%rbx,%r9
	shrq	$26,%rdx
	andq	$0x3ffffff,%rax
	shlq	$12,%r8
	andq	$0x3ffffff,%rdx
	shrq	$14,%rbx
	orq	%r8,%r14
	shlq	$24,%rbp
	andq	$0x3ffffff,%r14
	shrq	$40,%r9
	andq	$0x3ffffff,%rbx
	orq	%r9,%rbp

	vmovd	%eax,%xmm0
	vmovd	%edx,%xmm1
	vmovd	%r14d,%xmm2
	vmovd	%ebx,%xmm3
	vmovd	%ebp,%xmm4
	movl	$1,20(%rdi)

	call	__poly1305_init_avx

.Lproceed_avx:
	movq	%r15,%rdx

	movq	0(%rsp),%r15
.cfi_restore	%r15
	movq	8(%rsp),%r14
.cfi_restore	%r14
	movq	16(%rsp),%r13
.cfi_restore	%r13
	movq	24(%rsp),%r12
.cfi_restore	%r12
	movq	32(%rsp),%rbp
.cfi_restore	%rbp
	movq	40(%rsp),%rbx
.cfi_restore	%rbx
	leaq	48(%rsp),%rax
	leaq	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lbase2_64_avx_epilogue:
	jmp	.Ldo_avx
.cfi_endproc	

.align	32
.Leven_avx:
.cfi_startproc	
	vmovd	0(%rdi),%xmm0
	vmovd	4(%rdi),%xmm1
	vmovd	8(%rdi),%xmm2
	vmovd	12(%rdi),%xmm3
	vmovd	16(%rdi),%xmm4

.Ldo_avx:
	leaq	-88(%rsp),%r11
.cfi_def_cfa	%r11,0x60
	subq	$0x178,%rsp
	subq	$64,%rdx
	leaq	-32(%rsi),%rax
	cmovcq	%rax,%rsi

	vmovdqu	48(%rdi),%xmm14
	leaq	112(%rdi),%rdi
	leaq	.Lconst(%rip),%rcx



	vmovdqu	32(%rsi),%xmm5
	vmovdqu	48(%rsi),%xmm6
	vmovdqa	64(%rcx),%xmm15

	vpsrldq	$6,%xmm5,%xmm7
	vpsrldq	$6,%xmm6,%xmm8
	vpunpckhqdq	%xmm6,%xmm5,%xmm9
	vpunpcklqdq	%xmm6,%xmm5,%xmm5
	vpunpcklqdq	%xmm8,%xmm7,%xmm8

	vpsrlq	$40,%xmm9,%xmm9
	vpsrlq	$26,%xmm5,%xmm6
	vpand	%xmm15,%xmm5,%xmm5
	vpsrlq	$4,%xmm8,%xmm7
	vpand	%xmm15,%xmm6,%xmm6
	vpsrlq	$30,%xmm8,%xmm8
	vpand	%xmm15,%xmm7,%xmm7
	vpand	%xmm15,%xmm8,%xmm8
	vpor	32(%rcx),%xmm9,%xmm9

	jbe	.Lskip_loop_avx


	vmovdqu	-48(%rdi),%xmm11
	vmovdqu	-32(%rdi),%xmm12
	vpshufd	$0xEE,%xmm14,%xmm13
	vpshufd	$0x44,%xmm14,%xmm10
	vmovdqa	%xmm13,-144(%r11)
	vmovdqa	%xmm10,0(%rsp)
	vpshufd	$0xEE,%xmm11,%xmm14
	vmovdqu	-16(%rdi),%xmm10
	vpshufd	$0x44,%xmm11,%xmm11
	vmovdqa	%xmm14,-128(%r11)
	vmovdqa	%xmm11,16(%rsp)
	vpshufd	$0xEE,%xmm12,%xmm13
	vmovdqu	0(%rdi),%xmm11
	vpshufd	$0x44,%xmm12,%xmm12
	vmovdqa	%xmm13,-112(%r11)
	vmovdqa	%xmm12,32(%rsp)
	vpshufd	$0xEE,%xmm10,%xmm14
	vmovdqu	16(%rdi),%xmm12
	vpshufd	$0x44,%xmm10,%xmm10
	vmovdqa	%xmm14,-96(%r11)
	vmovdqa	%xmm10,48(%rsp)
	vpshufd	$0xEE,%xmm11,%xmm13
	vmovdqu	32(%rdi),%xmm10
	vpshufd	$0x44,%xmm11,%xmm11
	vmovdqa	%xmm13,-80(%r11)
	vmovdqa	%xmm11,64(%rsp)
	vpshufd	$0xEE,%xmm12,%xmm14
	vmovdqu	48(%rdi),%xmm11
	vpshufd	$0x44,%xmm12,%xmm12
	vmovdqa	%xmm14,-64(%r11)
	vmovdqa	%xmm12,80(%rsp)
	vpshufd	$0xEE,%xmm10,%xmm13
	vmovdqu	64(%rdi),%xmm12
	vpshufd	$0x44,%xmm10,%xmm10
	vmovdqa	%xmm13,-48(%r11)
	vmovdqa	%xmm10,96(%rsp)
	vpshufd	$0xEE,%xmm11,%xmm14
	vpshufd	$0x44,%xmm11,%xmm11
	vmovdqa	%xmm14,-32(%r11)
	vmovdqa	%xmm11,112(%rsp)
	vpshufd	$0xEE,%xmm12,%xmm13
	vmovdqa	0(%rsp),%xmm14
	vpshufd	$0x44,%xmm12,%xmm12
	vmovdqa	%xmm13,-16(%r11)
	vmovdqa	%xmm12,128(%rsp)

	jmp	.Loop_avx

.align	32
.Loop_avx:




















	vpmuludq	%xmm5,%xmm14,%xmm10
	vpmuludq	%xmm6,%xmm14,%xmm11
	vmovdqa	%xmm2,32(%r11)
	vpmuludq	%xmm7,%xmm14,%xmm12
	vmovdqa	16(%rsp),%xmm2
	vpmuludq	%xmm8,%xmm14,%xmm13
	vpmuludq	%xmm9,%xmm14,%xmm14

	vmovdqa	%xmm0,0(%r11)
	vpmuludq	32(%rsp),%xmm9,%xmm0
	vmovdqa	%xmm1,16(%r11)
	vpmuludq	%xmm8,%xmm2,%xmm1
	vpaddq	%xmm0,%xmm10,%xmm10
	vpaddq	%xmm1,%xmm14,%xmm14
	vmovdqa	%xmm3,48(%r11)
	vpmuludq	%xmm7,%xmm2,%xmm0
	vpmuludq	%xmm6,%xmm2,%xmm1
	vpaddq	%xmm0,%xmm13,%xmm13
	vmovdqa	48(%rsp),%xmm3
	vpaddq	%xmm1,%xmm12,%xmm12
	vmovdqa	%xmm4,64(%r11)
	vpmuludq	%xmm5,%xmm2,%xmm2
	vpmuludq	%xmm7,%xmm3,%xmm0
	vpaddq	%xmm2,%xmm11,%xmm11

	vmovdqa	64(%rsp),%xmm4
	vpaddq	%xmm0,%xmm14,%xmm14
	vpmuludq	%xmm6,%xmm3,%xmm1
	vpmuludq	%xmm5,%xmm3,%xmm3
	vpaddq	%xmm1,%xmm13,%xmm13
	vmovdqa	80(%rsp),%xmm2
	vpaddq	%xmm3,%xmm12,%xmm12
	vpmuludq	%xmm9,%xmm4,%xmm0
	vpmuludq	%xmm8,%xmm4,%xmm4
	vpaddq	%xmm0,%xmm11,%xmm11
	vmovdqa	96(%rsp),%xmm3
	vpaddq	%xmm4,%xmm10,%xmm10

	vmovdqa	128(%rsp),%xmm4
	vpmuludq	%xmm6,%xmm2,%xmm1
	vpmuludq	%xmm5,%xmm2,%xmm2
	vpaddq	%xmm1,%xmm14,%xmm14
	vpaddq	%xmm2,%xmm13,%xmm13
	vpmuludq	%xmm9,%xmm3,%xmm0
	vpmuludq	%xmm8,%xmm3,%xmm1
	vpaddq	%xmm0,%xmm12,%xmm12
	vmovdqu	0(%rsi),%xmm0
	vpaddq	%xmm1,%xmm11,%xmm11
	vpmuludq	%xmm7,%xmm3,%xmm3
	vpmuludq	%xmm7,%xmm4,%xmm7
	vpaddq	%xmm3,%xmm10,%xmm10

	vmovdqu	16(%rsi),%xmm1
	vpaddq	%xmm7,%xmm11,%xmm11
	vpmuludq	%xmm8,%xmm4,%xmm8
	vpmuludq	%xmm9,%xmm4,%xmm9
	vpsrldq	$6,%xmm0,%xmm2
	vpaddq	%xmm8,%xmm12,%xmm12
	vpaddq	%xmm9,%xmm13,%xmm13
	vpsrldq	$6,%xmm1,%xmm3
	vpmuludq	112(%rsp),%xmm5,%xmm9
	vpmuludq	%xmm6,%xmm4,%xmm5
	vpunpckhqdq	%xmm1,%xmm0,%xmm4
	vpaddq	%xmm9,%xmm14,%xmm14
	vmovdqa	-144(%r11),%xmm9
	vpaddq	%xmm5,%xmm10,%xmm10

	vpunpcklqdq	%xmm1,%xmm0,%xmm0
	vpunpcklqdq	%xmm3,%xmm2,%xmm3


	vpsrldq	$5,%xmm4,%xmm4
	vpsrlq	$26,%xmm0,%xmm1
	vpand	%xmm15,%xmm0,%xmm0
	vpsrlq	$4,%xmm3,%xmm2
	vpand	%xmm15,%xmm1,%xmm1
	vpand	0(%rcx),%xmm4,%xmm4
	vpsrlq	$30,%xmm3,%xmm3
	vpand	%xmm15,%xmm2,%xmm2
	vpand	%xmm15,%xmm3,%xmm3
	vpor	32(%rcx),%xmm4,%xmm4

	vpaddq	0(%r11),%xmm0,%xmm0
	vpaddq	16(%r11),%xmm1,%xmm1
	vpaddq	32(%r11),%xmm2,%xmm2
	vpaddq	48(%r11),%xmm3,%xmm3
	vpaddq	64(%r11),%xmm4,%xmm4

	leaq	32(%rsi),%rax
	leaq	64(%rsi),%rsi
	subq	$64,%rdx
	cmovcq	%rax,%rsi










	vpmuludq	%xmm0,%xmm9,%xmm5
	vpmuludq	%xmm1,%xmm9,%xmm6
	vpaddq	%xmm5,%xmm10,%xmm10
	vpaddq	%xmm6,%xmm11,%xmm11
	vmovdqa	-128(%r11),%xmm7
	vpmuludq	%xmm2,%xmm9,%xmm5
	vpmuludq	%xmm3,%xmm9,%xmm6
	vpaddq	%xmm5,%xmm12,%xmm12
	vpaddq	%xmm6,%xmm13,%xmm13
	vpmuludq	%xmm4,%xmm9,%xmm9
	vpmuludq	-112(%r11),%xmm4,%xmm5
	vpaddq	%xmm9,%xmm14,%xmm14

	vpaddq	%xmm5,%xmm10,%xmm10
	vpmuludq	%xmm2,%xmm7,%xmm6
	vpmuludq	%xmm3,%xmm7,%xmm5
	vpaddq	%xmm6,%xmm13,%xmm13
	vmovdqa	-96(%r11),%xmm8
	vpaddq	%xmm5,%xmm14,%xmm14
	vpmuludq	%xmm1,%xmm7,%xmm6
	vpmuludq	%xmm0,%xmm7,%xmm7
	vpaddq	%xmm6,%xmm12,%xmm12
	vpaddq	%xmm7,%xmm11,%xmm11

	vmovdqa	-80(%r11),%xmm9
	vpmuludq	%xmm2,%xmm8,%xmm5
	vpmuludq	%xmm1,%xmm8,%xmm6
	vpaddq	%xmm5,%xmm14,%xmm14
	vpaddq	%xmm6,%xmm13,%xmm13
	vmovdqa	-64(%r11),%xmm7
	vpmuludq	%xmm0,%xmm8,%xmm8
	vpmuludq	%xmm4,%xmm9,%xmm5
	vpaddq	%xmm8,%xmm12,%xmm12
	vpaddq	%xmm5,%xmm11,%xmm11
	vmovdqa	-48(%r11),%xmm8
	vpmuludq	%xmm3,%xmm9,%xmm9
	vpmuludq	%xmm1,%xmm7,%xmm6
	vpaddq	%xmm9,%xmm10,%xmm10

	vmovdqa	-16(%r11),%xmm9
	vpaddq	%xmm6,%xmm14,%xmm14
	vpmuludq	%xmm0,%xmm7,%xmm7
	vpmuludq	%xmm4,%xmm8,%xmm5
	vpaddq	%xmm7,%xmm13,%xmm13
	vpaddq	%xmm5,%xmm12,%xmm12
	vmovdqu	32(%rsi),%xmm5
	vpmuludq	%xmm3,%xmm8,%xmm7
	vpmuludq	%xmm2,%xmm8,%xmm8
	vpaddq	%xmm7,%xmm11,%xmm11
	vmovdqu	48(%rsi),%xmm6
	vpaddq	%xmm8,%xmm10,%xmm10

	vpmuludq	%xmm2,%xmm9,%xmm2
	vpmuludq	%xmm3,%xmm9,%xmm3
	vpsrldq	$6,%xmm5,%xmm7
	vpaddq	%xmm2,%xmm11,%xmm11
	vpmuludq	%xmm4,%xmm9,%xmm4
	vpsrldq	$6,%xmm6,%xmm8
	vpaddq	%xmm3,%xmm12,%xmm2
	vpaddq	%xmm4,%xmm13,%xmm3
	vpmuludq	-32(%r11),%xmm0,%xmm4
	vpmuludq	%xmm1,%xmm9,%xmm0
	vpunpckhqdq	%xmm6,%xmm5,%xmm9
	vpaddq	%xmm4,%xmm14,%xmm4
	vpaddq	%xmm0,%xmm10,%xmm0

	vpunpcklqdq	%xmm6,%xmm5,%xmm5
	vpunpcklqdq	%xmm8,%xmm7,%xmm8


	vpsrldq	$5,%xmm9,%xmm9
	vpsrlq	$26,%xmm5,%xmm6
	vmovdqa	0(%rsp),%xmm14
	vpand	%xmm15,%xmm5,%xmm5
	vpsrlq	$4,%xmm8,%xmm7
	vpand	%xmm15,%xmm6,%xmm6
	vpand	0(%rcx),%xmm9,%xmm9
	vpsrlq	$30,%xmm8,%xmm8
	vpand	%xmm15,%xmm7,%xmm7
	vpand	%xmm15,%xmm8,%xmm8
	vpor	32(%rcx),%xmm9,%xmm9





	vpsrlq	$26,%xmm3,%xmm13
	vpand	%xmm15,%xmm3,%xmm3
	vpaddq	%xmm13,%xmm4,%xmm4

	vpsrlq	$26,%xmm0,%xmm10
	vpand	%xmm15,%xmm0,%xmm0
	vpaddq	%xmm10,%xmm11,%xmm1

	vpsrlq	$26,%xmm4,%xmm10
	vpand	%xmm15,%xmm4,%xmm4

	vpsrlq	$26,%xmm1,%xmm11
	vpand	%xmm15,%xmm1,%xmm1
	vpaddq	%xmm11,%xmm2,%xmm2

	vpaddq	%xmm10,%xmm0,%xmm0
	vpsllq	$2,%xmm10,%xmm10
	vpaddq	%xmm10,%xmm0,%xmm0

	vpsrlq	$26,%xmm2,%xmm12
	vpand	%xmm15,%xmm2,%xmm2
	vpaddq	%xmm12,%xmm3,%xmm3

	vpsrlq	$26,%xmm0,%xmm10
	vpand	%xmm15,%xmm0,%xmm0
	vpaddq	%xmm10,%xmm1,%xmm1

	vpsrlq	$26,%xmm3,%xmm13
	vpand	%xmm15,%xmm3,%xmm3
	vpaddq	%xmm13,%xmm4,%xmm4

	ja	.Loop_avx

.Lskip_loop_avx:



	vpshufd	$0x10,%xmm14,%xmm14
	addq	$32,%rdx
	jnz	.Long_tail_avx

	vpaddq	%xmm2,%xmm7,%xmm7
	vpaddq	%xmm0,%xmm5,%xmm5
	vpaddq	%xmm1,%xmm6,%xmm6
	vpaddq	%xmm3,%xmm8,%xmm8
	vpaddq	%xmm4,%xmm9,%xmm9

.Long_tail_avx:
	vmovdqa	%xmm2,32(%r11)
	vmovdqa	%xmm0,0(%r11)
	vmovdqa	%xmm1,16(%r11)
	vmovdqa	%xmm3,48(%r11)
	vmovdqa	%xmm4,64(%r11)







	vpmuludq	%xmm7,%xmm14,%xmm12
	vpmuludq	%xmm5,%xmm14,%xmm10
	vpshufd	$0x10,-48(%rdi),%xmm2
	vpmuludq	%xmm6,%xmm14,%xmm11
	vpmuludq	%xmm8,%xmm14,%xmm13
	vpmuludq	%xmm9,%xmm14,%xmm14

	vpmuludq	%xmm8,%xmm2,%xmm0
	vpaddq	%xmm0,%xmm14,%xmm14
	vpshufd	$0x10,-32(%rdi),%xmm3
	vpmuludq	%xmm7,%xmm2,%xmm1
	vpaddq	%xmm1,%xmm13,%xmm13
	vpshufd	$0x10,-16(%rdi),%xmm4
	vpmuludq	%xmm6,%xmm2,%xmm0
	vpaddq	%xmm0,%xmm12,%xmm12
	vpmuludq	%xmm5,%xmm2,%xmm2
	vpaddq	%xmm2,%xmm11,%xmm11
	vpmuludq	%xmm9,%xmm3,%xmm3
	vpaddq	%xmm3,%xmm10,%xmm10

	vpshufd	$0x10,0(%rdi),%xmm2
	vpmuludq	%xmm7,%xmm4,%xmm1
	vpaddq	%xmm1,%xmm14,%xmm14
	vpmuludq	%xmm6,%xmm4,%xmm0
	vpaddq	%xmm0,%xmm13,%xmm13
	vpshufd	$0x10,16(%rdi),%xmm3
	vpmuludq	%xmm5,%xmm4,%xmm4
	vpaddq	%xmm4,%xmm12,%xmm12
	vpmuludq	%xmm9,%xmm2,%xmm1
	vpaddq	%xmm1,%xmm11,%xmm11
	vpshufd	$0x10,32(%rdi),%xmm4
	vpmuludq	%xmm8,%xmm2,%xmm2
	vpaddq	%xmm2,%xmm10,%xmm10

	vpmuludq	%xmm6,%xmm3,%xmm0
	vpaddq	%xmm0,%xmm14,%xmm14
	vpmuludq	%xmm5,%xmm3,%xmm3
	vpaddq	%xmm3,%xmm13,%xmm13
	vpshufd	$0x10,48(%rdi),%xmm2
	vpmuludq	%xmm9,%xmm4,%xmm1
	vpaddq	%xmm1,%xmm12,%xmm12
	vpshufd	$0x10,64(%rdi),%xmm3
	vpmuludq	%xmm8,%xmm4,%xmm0
	vpaddq	%xmm0,%xmm11,%xmm11
	vpmuludq	%xmm7,%xmm4,%xmm4
	vpaddq	%xmm4,%xmm10,%xmm10

	vpmuludq	%xmm5,%xmm2,%xmm2
	vpaddq	%xmm2,%xmm14,%xmm14
	vpmuludq	%xmm9,%xmm3,%xmm1
	vpaddq	%xmm1,%xmm13,%xmm13
	vpmuludq	%xmm8,%xmm3,%xmm0
	vpaddq	%xmm0,%xmm12,%xmm12
	vpmuludq	%xmm7,%xmm3,%xmm1
	vpaddq	%xmm1,%xmm11,%xmm11
	vpmuludq	%xmm6,%xmm3,%xmm3
	vpaddq	%xmm3,%xmm10,%xmm10

	jz	.Lshort_tail_avx

	vmovdqu	0(%rsi),%xmm0
	vmovdqu	16(%rsi),%xmm1

	vpsrldq	$6,%xmm0,%xmm2
	vpsrldq	$6,%xmm1,%xmm3
	vpunpckhqdq	%xmm1,%xmm0,%xmm4
	vpunpcklqdq	%xmm1,%xmm0,%xmm0
	vpunpcklqdq	%xmm3,%xmm2,%xmm3

	vpsrlq	$40,%xmm4,%xmm4
	vpsrlq	$26,%xmm0,%xmm1
	vpand	%xmm15,%xmm0,%xmm0
	vpsrlq	$4,%xmm3,%xmm2
	vpand	%xmm15,%xmm1,%xmm1
	vpsrlq	$30,%xmm3,%xmm3
	vpand	%xmm15,%xmm2,%xmm2
	vpand	%xmm15,%xmm3,%xmm3
	vpor	32(%rcx),%xmm4,%xmm4

	vpshufd	$0x32,-64(%rdi),%xmm9
	vpaddq	0(%r11),%xmm0,%xmm0
	vpaddq	16(%r11),%xmm1,%xmm1
	vpaddq	32(%r11),%xmm2,%xmm2
	vpaddq	48(%r11),%xmm3,%xmm3
	vpaddq	64(%r11),%xmm4,%xmm4




	vpmuludq	%xmm0,%xmm9,%xmm5
	vpaddq	%xmm5,%xmm10,%xmm10
	vpmuludq	%xmm1,%xmm9,%xmm6
	vpaddq	%xmm6,%xmm11,%xmm11
	vpmuludq	%xmm2,%xmm9,%xmm5
	vpaddq	%xmm5,%xmm12,%xmm12
	vpshufd	$0x32,-48(%rdi),%xmm7
	vpmuludq	%xmm3,%xmm9,%xmm6
	vpaddq	%xmm6,%xmm13,%xmm13
	vpmuludq	%xmm4,%xmm9,%xmm9
	vpaddq	%xmm9,%xmm14,%xmm14

	vpmuludq	%xmm3,%xmm7,%xmm5
	vpaddq	%xmm5,%xmm14,%xmm14
	vpshufd	$0x32,-32(%rdi),%xmm8
	vpmuludq	%xmm2,%xmm7,%xmm6
	vpaddq	%xmm6,%xmm13,%xmm13
	vpshufd	$0x32,-16(%rdi),%xmm9
	vpmuludq	%xmm1,%xmm7,%xmm5
	vpaddq	%xmm5,%xmm12,%xmm12
	vpmuludq	%xmm0,%xmm7,%xmm7
	vpaddq	%xmm7,%xmm11,%xmm11
	vpmuludq	%xmm4,%xmm8,%xmm8
	vpaddq	%xmm8,%xmm10,%xmm10

	vpshufd	$0x32,0(%rdi),%xmm7
	vpmuludq	%xmm2,%xmm9,%xmm6
	vpaddq	%xmm6,%xmm14,%xmm14
	vpmuludq	%xmm1,%xmm9,%xmm5
	vpaddq	%xmm5,%xmm13,%xmm13
	vpshufd	$0x32,16(%rdi),%xmm8
	vpmuludq	%xmm0,%xmm9,%xmm9
	vpaddq	%xmm9,%xmm12,%xmm12
	vpmuludq	%xmm4,%xmm7,%xmm6
	vpaddq	%xmm6,%xmm11,%xmm11
	vpshufd	$0x32,32(%rdi),%xmm9
	vpmuludq	%xmm3,%xmm7,%xmm7
	vpaddq	%xmm7,%xmm10,%xmm10

	vpmuludq	%xmm1,%xmm8,%xmm5
	vpaddq	%xmm5,%xmm14,%xmm14
	vpmuludq	%xmm0,%xmm8,%xmm8
	vpaddq	%xmm8,%xmm13,%xmm13
	vpshufd	$0x32,48(%rdi),%xmm7
	vpmuludq	%xmm4,%xmm9,%xmm6
	vpaddq	%xmm6,%xmm12,%xmm12
	vpshufd	$0x32,64(%rdi),%xmm8
	vpmuludq	%xmm3,%xmm9,%xmm5
	vpaddq	%xmm5,%xmm11,%xmm11
	vpmuludq	%xmm2,%xmm9,%xmm9
	vpaddq	%xmm9,%xmm10,%xmm10

	vpmuludq	%xmm0,%xmm7,%xmm7
	vpaddq	%xmm7,%xmm14,%xmm14
	vpmuludq	%xmm4,%xmm8,%xmm6
	vpaddq	%xmm6,%xmm13,%xmm13
	vpmuludq	%xmm3,%xmm8,%xmm5
	vpaddq	%xmm5,%xmm12,%xmm12
	vpmuludq	%xmm2,%xmm8,%xmm6
	vpaddq	%xmm6,%xmm11,%xmm11
	vpmuludq	%xmm1,%xmm8,%xmm8
	vpaddq	%xmm8,%xmm10,%xmm10

.Lshort_tail_avx:



	vpsrldq	$8,%xmm14,%xmm9
	vpsrldq	$8,%xmm13,%xmm8
	vpsrldq	$8,%xmm11,%xmm6
	vpsrldq	$8,%xmm10,%xmm5
	vpsrldq	$8,%xmm12,%xmm7
	vpaddq	%xmm8,%xmm13,%xmm13
	vpaddq	%xmm9,%xmm14,%xmm14
	vpaddq	%xmm5,%xmm10,%xmm10
	vpaddq	%xmm6,%xmm11,%xmm11
	vpaddq	%xmm7,%xmm12,%xmm12




	vpsrlq	$26,%xmm13,%xmm3
	vpand	%xmm15,%xmm13,%xmm13
	vpaddq	%xmm3,%xmm14,%xmm14

	vpsrlq	$26,%xmm10,%xmm0
	vpand	%xmm15,%xmm10,%xmm10
	vpaddq	%xmm0,%xmm11,%xmm11

	vpsrlq	$26,%xmm14,%xmm4
	vpand	%xmm15,%xmm14,%xmm14

	vpsrlq	$26,%xmm11,%xmm1
	vpand	%xmm15,%xmm11,%xmm11
	vpaddq	%xmm1,%xmm12,%xmm12

	vpaddq	%xmm4,%xmm10,%xmm10
	vpsllq	$2,%xmm4,%xmm4
	vpaddq	%xmm4,%xmm10,%xmm10

	vpsrlq	$26,%xmm12,%xmm2
	vpand	%xmm15,%xmm12,%xmm12
	vpaddq	%xmm2,%xmm13,%xmm13

	vpsrlq	$26,%xmm10,%xmm0
	vpand	%xmm15,%xmm10,%xmm10
	vpaddq	%xmm0,%xmm11,%xmm11

	vpsrlq	$26,%xmm13,%xmm3
	vpand	%xmm15,%xmm13,%xmm13
	vpaddq	%xmm3,%xmm14,%xmm14

	vmovd	%xmm10,-112(%rdi)
	vmovd	%xmm11,-108(%rdi)
	vmovd	%xmm12,-104(%rdi)
	vmovd	%xmm13,-100(%rdi)
	vmovd	%xmm14,-96(%rdi)
	leaq	88(%r11),%rsp
.cfi_def_cfa	%rsp,8
	vzeroupper
	.byte	0xf3,0xc3
.cfi_endproc	
.size	poly1305_blocks_avx,.-poly1305_blocks_avx

.type	poly1305_emit_avx,@function
.align	32
poly1305_emit_avx:
	cmpl	$0,20(%rdi)
	je	.Lemit

	movl	0(%rdi),%eax
	movl	4(%rdi),%ecx
	movl	8(%rdi),%r8d
	movl	12(%rdi),%r11d
	movl	16(%rdi),%r10d

	shlq	$26,%rcx
	movq	%r8,%r9
	shlq	$52,%r8
	addq	%rcx,%rax
	shrq	$12,%r9
	addq	%rax,%r8
	adcq	$0,%r9

	shlq	$14,%r11
	movq	%r10,%rax
	shrq	$24,%r10
	addq	%r11,%r9
	shlq	$40,%rax
	addq	%rax,%r9
	adcq	$0,%r10

	movq	%r10,%rax
	movq	%r10,%rcx
	andq	$3,%r10
	shrq	$2,%rax
	andq	$-4,%rcx
	addq	%rcx,%rax
	addq	%rax,%r8
	adcq	$0,%r9
	adcq	$0,%r10

	movq	%r8,%rax
	addq	$5,%r8
	movq	%r9,%rcx
	adcq	$0,%r9
	adcq	$0,%r10
	shrq	$2,%r10
	cmovnzq	%r8,%rax
	cmovnzq	%r9,%rcx

	addq	0(%rdx),%rax
	adcq	8(%rdx),%rcx
	movq	%rax,0(%rsi)
	movq	%rcx,8(%rsi)

	.byte	0xf3,0xc3
.size	poly1305_emit_avx,.-poly1305_emit_avx
.type	poly1305_blocks_avx2,@function
.align	32
poly1305_blocks_avx2:
.cfi_startproc	
	movl	20(%rdi),%r8d
	cmpq	$128,%rdx
	jae	.Lblocks_avx2
	testl	%r8d,%r8d
	jz	.Lblocks

.Lblocks_avx2:
	andq	$-16,%rdx
	jz	.Lno_data_avx2

	vzeroupper

	testl	%r8d,%r8d
	jz	.Lbase2_64_avx2

	testq	$63,%rdx
	jz	.Leven_avx2

	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-24
	pushq	%r12
.cfi_adjust_cfa_offset	8
.cfi_offset	%r12,-32
	pushq	%r13
.cfi_adjust_cfa_offset	8
.cfi_offset	%r13,-40
	pushq	%r14
.cfi_adjust_cfa_offset	8
.cfi_offset	%r14,-48
	pushq	%r15
.cfi_adjust_cfa_offset	8
.cfi_offset	%r15,-56
.Lblocks_avx2_body:

	movq	%rdx,%r15

	movq	0(%rdi),%r8
	movq	8(%rdi),%r9
	movl	16(%rdi),%ebp

	movq	24(%rdi),%r11
	movq	32(%rdi),%r13


	movl	%r8d,%r14d
	andq	$-2147483648,%r8
	movq	%r9,%r12
	movl	%r9d,%ebx
	andq	$-2147483648,%r9

	shrq	$6,%r8
	shlq	$52,%r12
	addq	%r8,%r14
	shrq	$12,%rbx
	shrq	$18,%r9
	addq	%r12,%r14
	adcq	%r9,%rbx

	movq	%rbp,%r8
	shlq	$40,%r8
	shrq	$24,%rbp
	addq	%r8,%rbx
	adcq	$0,%rbp

	movq	$-4,%r9
	movq	%rbp,%r8
	andq	%rbp,%r9
	shrq	$2,%r8
	andq	$3,%rbp
	addq	%r9,%r8
	addq	%r8,%r14
	adcq	$0,%rbx
	adcq	$0,%rbp

	movq	%r13,%r12
	movq	%r13,%rax
	shrq	$2,%r13
	addq	%r12,%r13

.Lbase2_26_pre_avx2:
	addq	0(%rsi),%r14
	adcq	8(%rsi),%rbx
	leaq	16(%rsi),%rsi
	adcq	%rcx,%rbp
	subq	$16,%r15

	call	__poly1305_block
	movq	%r12,%rax

	testq	$63,%r15
	jnz	.Lbase2_26_pre_avx2

	testq	%rcx,%rcx
	jz	.Lstore_base2_64_avx2


	movq	%r14,%rax
	movq	%r14,%rdx
	shrq	$52,%r14
	movq	%rbx,%r11
	movq	%rbx,%r12
	shrq	$26,%rdx
	andq	$0x3ffffff,%rax
	shlq	$12,%r11
	andq	$0x3ffffff,%rdx
	shrq	$14,%rbx
	orq	%r11,%r14
	shlq	$24,%rbp
	andq	$0x3ffffff,%r14
	shrq	$40,%r12
	andq	$0x3ffffff,%rbx
	orq	%r12,%rbp

	testq	%r15,%r15
	jz	.Lstore_base2_26_avx2

	vmovd	%eax,%xmm0
	vmovd	%edx,%xmm1
	vmovd	%r14d,%xmm2
	vmovd	%ebx,%xmm3
	vmovd	%ebp,%xmm4
	jmp	.Lproceed_avx2

.align	32
.Lstore_base2_64_avx2:
	movq	%r14,0(%rdi)
	movq	%rbx,8(%rdi)
	movq	%rbp,16(%rdi)
	jmp	.Ldone_avx2

.align	16
.Lstore_base2_26_avx2:
	movl	%eax,0(%rdi)
	movl	%edx,4(%rdi)
	movl	%r14d,8(%rdi)
	movl	%ebx,12(%rdi)
	movl	%ebp,16(%rdi)
.align	16
.Ldone_avx2:
	movq	0(%rsp),%r15
.cfi_restore	%r15
	movq	8(%rsp),%r14
.cfi_restore	%r14
	movq	16(%rsp),%r13
.cfi_restore	%r13
	movq	24(%rsp),%r12
.cfi_restore	%r12
	movq	32(%rsp),%rbp
.cfi_restore	%rbp
	movq	40(%rsp),%rbx
.cfi_restore	%rbx
	leaq	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lno_data_avx2:
.Lblocks_avx2_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	

.align	32
.Lbase2_64_avx2:
.cfi_startproc	
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-16
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-24
	pushq	%r12
.cfi_adjust_cfa_offset	8
.cfi_offset	%r12,-32
	pushq	%r13
.cfi_adjust_cfa_offset	8
.cfi_offset	%r13,-40
	pushq	%r14
.cfi_adjust_cfa_offset	8
.cfi_offset	%r14,-48
	pushq	%r15
.cfi_adjust_cfa_offset	8
.cfi_offset	%r15,-56
.Lbase2_64_avx2_body:

	movq	%rdx,%r15

	movq	24(%rdi),%r11
	movq	32(%rdi),%r13

	movq	0(%rdi),%r14
	movq	8(%rdi),%rbx
	movl	16(%rdi),%ebp

	movq	%r13,%r12
	movq	%r13,%rax
	shrq	$2,%r13
	addq	%r12,%r13

	testq	$63,%rdx
	jz	.Linit_avx2

.Lbase2_64_pre_avx2:
	addq	0(%rsi),%r14
	adcq	8(%rsi),%rbx
	leaq	16(%rsi),%rsi
	adcq	%rcx,%rbp
	subq	$16,%r15

	call	__poly1305_block
	movq	%r12,%rax

	testq	$63,%r15
	jnz	.Lbase2_64_pre_avx2

.Linit_avx2:

	movq	%r14,%rax
	movq	%r14,%rdx
	shrq	$52,%r14
	movq	%rbx,%r8
	movq	%rbx,%r9
	shrq	$26,%rdx
	andq	$0x3ffffff,%rax
	shlq	$12,%r8
	andq	$0x3ffffff,%rdx
	shrq	$14,%rbx
	orq	%r8,%r14
	shlq	$24,%rbp
	andq	$0x3ffffff,%r14
	shrq	$40,%r9
	andq	$0x3ffffff,%rbx
	orq	%r9,%rbp

	vmovd	%eax,%xmm0
	vmovd	%edx,%xmm1
	vmovd	%r14d,%xmm2
	vmovd	%ebx,%xmm3
	vmovd	%ebp,%xmm4
	movl	$1,20(%rdi)

	call	__poly1305_init_avx

.Lproceed_avx2:
	movq	%r15,%rdx
	movl	OPENSSL_ia32cap_P+8(%rip),%r10d
	movl	$3221291008,%r11d

	movq	0(%rsp),%r15
.cfi_restore	%r15
	movq	8(%rsp),%r14
.cfi_restore	%r14
	movq	16(%rsp),%r13
.cfi_restore	%r13
	movq	24(%rsp),%r12
.cfi_restore	%r12
	movq	32(%rsp),%rbp
.cfi_restore	%rbp
	movq	40(%rsp),%rbx
.cfi_restore	%rbx
	leaq	48(%rsp),%rax
	leaq	48(%rsp),%rsp
.cfi_adjust_cfa_offset	-48
.Lbase2_64_avx2_epilogue:
	jmp	.Ldo_avx2
.cfi_endproc	

.align	32
.Leven_avx2:
.cfi_startproc	
	movl	OPENSSL_ia32cap_P+8(%rip),%r10d
	vmovd	0(%rdi),%xmm0
	vmovd	4(%rdi),%xmm1
	vmovd	8(%rdi),%xmm2
	vmovd	12(%rdi),%xmm3
	vmovd	16(%rdi),%xmm4

.Ldo_avx2:
	leaq	-8(%rsp),%r11
.cfi_def_cfa	%r11,16
	subq	$0x128,%rsp
	leaq	.Lconst(%rip),%rcx
	leaq	48+64(%rdi),%rdi
	vmovdqa	96(%rcx),%ymm7


	vmovdqu	-64(%rdi),%xmm9
	andq	$-512,%rsp
	vmovdqu	-48(%rdi),%xmm10
	vmovdqu	-32(%rdi),%xmm6
	vmovdqu	-16(%rdi),%xmm11
	vmovdqu	0(%rdi),%xmm12
	vmovdqu	16(%rdi),%xmm13
	leaq	144(%rsp),%rax
	vmovdqu	32(%rdi),%xmm14
	vpermd	%ymm9,%ymm7,%ymm9
	vmovdqu	48(%rdi),%xmm15
	vpermd	%ymm10,%ymm7,%ymm10
	vmovdqu	64(%rdi),%xmm5
	vpermd	%ymm6,%ymm7,%ymm6
	vmovdqa	%ymm9,0(%rsp)
	vpermd	%ymm11,%ymm7,%ymm11
	vmovdqa	%ymm10,32-144(%rax)
	vpermd	%ymm12,%ymm7,%ymm12
	vmovdqa	%ymm6,64-144(%rax)
	vpermd	%ymm13,%ymm7,%ymm13
	vmovdqa	%ymm11,96-144(%rax)
	vpermd	%ymm14,%ymm7,%ymm14
	vmovdqa	%ymm12,128-144(%rax)
	vpermd	%ymm15,%ymm7,%ymm15
	vmovdqa	%ymm13,160-144(%rax)
	vpermd	%ymm5,%ymm7,%ymm5
	vmovdqa	%ymm14,192-144(%rax)
	vmovdqa	%ymm15,224-144(%rax)
	vmovdqa	%ymm5,256-144(%rax)
	vmovdqa	64(%rcx),%ymm5



	vmovdqu	0(%rsi),%xmm7
	vmovdqu	16(%rsi),%xmm8
	vinserti128	$1,32(%rsi),%ymm7,%ymm7
	vinserti128	$1,48(%rsi),%ymm8,%ymm8
	leaq	64(%rsi),%rsi

	vpsrldq	$6,%ymm7,%ymm9
	vpsrldq	$6,%ymm8,%ymm10
	vpunpckhqdq	%ymm8,%ymm7,%ymm6
	vpunpcklqdq	%ymm10,%ymm9,%ymm9
	vpunpcklqdq	%ymm8,%ymm7,%ymm7

	vpsrlq	$30,%ymm9,%ymm10
	vpsrlq	$4,%ymm9,%ymm9
	vpsrlq	$26,%ymm7,%ymm8
	vpsrlq	$40,%ymm6,%ymm6
	vpand	%ymm5,%ymm9,%ymm9
	vpand	%ymm5,%ymm7,%ymm7
	vpand	%ymm5,%ymm8,%ymm8
	vpand	%ymm5,%ymm10,%ymm10
	vpor	32(%rcx),%ymm6,%ymm6

	vpaddq	%ymm2,%ymm9,%ymm2
	subq	$64,%rdx
	jz	.Ltail_avx2
	jmp	.Loop_avx2

.align	32
.Loop_avx2:








	vpaddq	%ymm0,%ymm7,%ymm0
	vmovdqa	0(%rsp),%ymm7
	vpaddq	%ymm1,%ymm8,%ymm1
	vmovdqa	32(%rsp),%ymm8
	vpaddq	%ymm3,%ymm10,%ymm3
	vmovdqa	96(%rsp),%ymm9
	vpaddq	%ymm4,%ymm6,%ymm4
	vmovdqa	48(%rax),%ymm10
	vmovdqa	112(%rax),%ymm5
















	vpmuludq	%ymm2,%ymm7,%ymm13
	vpmuludq	%ymm2,%ymm8,%ymm14
	vpmuludq	%ymm2,%ymm9,%ymm15
	vpmuludq	%ymm2,%ymm10,%ymm11
	vpmuludq	%ymm2,%ymm5,%ymm12

	vpmuludq	%ymm0,%ymm8,%ymm6
	vpmuludq	%ymm1,%ymm8,%ymm2
	vpaddq	%ymm6,%ymm12,%ymm12
	vpaddq	%ymm2,%ymm13,%ymm13
	vpmuludq	%ymm3,%ymm8,%ymm6
	vpmuludq	64(%rsp),%ymm4,%ymm2
	vpaddq	%ymm6,%ymm15,%ymm15
	vpaddq	%ymm2,%ymm11,%ymm11
	vmovdqa	-16(%rax),%ymm8

	vpmuludq	%ymm0,%ymm7,%ymm6
	vpmuludq	%ymm1,%ymm7,%ymm2
	vpaddq	%ymm6,%ymm11,%ymm11
	vpaddq	%ymm2,%ymm12,%ymm12
	vpmuludq	%ymm3,%ymm7,%ymm6
	vpmuludq	%ymm4,%ymm7,%ymm2
	vmovdqu	0(%rsi),%xmm7
	vpaddq	%ymm6,%ymm14,%ymm14
	vpaddq	%ymm2,%ymm15,%ymm15
	vinserti128	$1,32(%rsi),%ymm7,%ymm7

	vpmuludq	%ymm3,%ymm8,%ymm6
	vpmuludq	%ymm4,%ymm8,%ymm2
	vmovdqu	16(%rsi),%xmm8
	vpaddq	%ymm6,%ymm11,%ymm11
	vpaddq	%ymm2,%ymm12,%ymm12
	vmovdqa	16(%rax),%ymm2
	vpmuludq	%ymm1,%ymm9,%ymm6
	vpmuludq	%ymm0,%ymm9,%ymm9
	vpaddq	%ymm6,%ymm14,%ymm14
	vpaddq	%ymm9,%ymm13,%ymm13
	vinserti128	$1,48(%rsi),%ymm8,%ymm8
	leaq	64(%rsi),%rsi

	vpmuludq	%ymm1,%ymm2,%ymm6
	vpmuludq	%ymm0,%ymm2,%ymm2
	vpsrldq	$6,%ymm7,%ymm9
	vpaddq	%ymm6,%ymm15,%ymm15
	vpaddq	%ymm2,%ymm14,%ymm14
	vpmuludq	%ymm3,%ymm10,%ymm6
	vpmuludq	%ymm4,%ymm10,%ymm2
	vpsrldq	$6,%ymm8,%ymm10
	vpaddq	%ymm6,%ymm12,%ymm12
	vpaddq	%ymm2,%ymm13,%ymm13
	vpunpckhqdq	%ymm8,%ymm7,%ymm6

	vpmuludq	%ymm3,%ymm5,%ymm3
	vpmuludq	%ymm4,%ymm5,%ymm4
	vpunpcklqdq	%ymm8,%ymm7,%ymm7
	vpaddq	%ymm3,%ymm13,%ymm2
	vpaddq	%ymm4,%ymm14,%ymm3
	vpunpcklqdq	%ymm10,%ymm9,%ymm10
	vpmuludq	80(%rax),%ymm0,%ymm4
	vpmuludq	%ymm1,%ymm5,%ymm0
	vmovdqa	64(%rcx),%ymm5
	vpaddq	%ymm4,%ymm15,%ymm4
	vpaddq	%ymm0,%ymm11,%ymm0




	vpsrlq	$26,%ymm3,%ymm14
	vpand	%ymm5,%ymm3,%ymm3
	vpaddq	%ymm14,%ymm4,%ymm4

	vpsrlq	$26,%ymm0,%ymm11
	vpand	%ymm5,%ymm0,%ymm0
	vpaddq	%ymm11,%ymm12,%ymm1

	vpsrlq	$26,%ymm4,%ymm15
	vpand	%ymm5,%ymm4,%ymm4

	vpsrlq	$4,%ymm10,%ymm9

	vpsrlq	$26,%ymm1,%ymm12
	vpand	%ymm5,%ymm1,%ymm1
	vpaddq	%ymm12,%ymm2,%ymm2

	vpaddq	%ymm15,%ymm0,%ymm0
	vpsllq	$2,%ymm15,%ymm15
	vpaddq	%ymm15,%ymm0,%ymm0

	vpand	%ymm5,%ymm9,%ymm9
	vpsrlq	$26,%ymm7,%ymm8

	vpsrlq	$26,%ymm2,%ymm13
	vpand	%ymm5,%ymm2,%ymm2
	vpaddq	%ymm13,%ymm3,%ymm3

	vpaddq	%ymm9,%ymm2,%ymm2
	vpsrlq	$30,%ymm10,%ymm10

	vpsrlq	$26,%ymm0,%ymm11
	vpand	%ymm5,%ymm0,%ymm0
	vpaddq	%ymm11,%ymm1,%ymm1

	vpsrlq	$40,%ymm6,%ymm6

	vpsrlq	$26,%ymm3,%ymm14
	vpand	%ymm5,%ymm3,%ymm3
	vpaddq	%ymm14,%ymm4,%ymm4

	vpand	%ymm5,%ymm7,%ymm7
	vpand	%ymm5,%ymm8,%ymm8
	vpand	%ymm5,%ymm10,%ymm10
	vpor	32(%rcx),%ymm6,%ymm6

	subq	$64,%rdx
	jnz	.Loop_avx2

.byte	0x66,0x90
.Ltail_avx2:







	vpaddq	%ymm0,%ymm7,%ymm0
	vmovdqu	4(%rsp),%ymm7
	vpaddq	%ymm1,%ymm8,%ymm1
	vmovdqu	36(%rsp),%ymm8
	vpaddq	%ymm3,%ymm10,%ymm3
	vmovdqu	100(%rsp),%ymm9
	vpaddq	%ymm4,%ymm6,%ymm4
	vmovdqu	52(%rax),%ymm10
	vmovdqu	116(%rax),%ymm5

	vpmuludq	%ymm2,%ymm7,%ymm13
	vpmuludq	%ymm2,%ymm8,%ymm14
	vpmuludq	%ymm2,%ymm9,%ymm15
	vpmuludq	%ymm2,%ymm10,%ymm11
	vpmuludq	%ymm2,%ymm5,%ymm12

	vpmuludq	%ymm0,%ymm8,%ymm6
	vpmuludq	%ymm1,%ymm8,%ymm2
	vpaddq	%ymm6,%ymm12,%ymm12
	vpaddq	%ymm2,%ymm13,%ymm13
	vpmuludq	%ymm3,%ymm8,%ymm6
	vpmuludq	68(%rsp),%ymm4,%ymm2
	vpaddq	%ymm6,%ymm15,%ymm15
	vpaddq	%ymm2,%ymm11,%ymm11

	vpmuludq	%ymm0,%ymm7,%ymm6
	vpmuludq	%ymm1,%ymm7,%ymm2
	vpaddq	%ymm6,%ymm11,%ymm11
	vmovdqu	-12(%rax),%ymm8
	vpaddq	%ymm2,%ymm12,%ymm12
	vpmuludq	%ymm3,%ymm7,%ymm6
	vpmuludq	%ymm4,%ymm7,%ymm2
	vpaddq	%ymm6,%ymm14,%ymm14
	vpaddq	%ymm2,%ymm15,%ymm15

	vpmuludq	%ymm3,%ymm8,%ymm6
	vpmuludq	%ymm4,%ymm8,%ymm2
	vpaddq	%ymm6,%ymm11,%ymm11
	vpaddq	%ymm2,%ymm12,%ymm12
	vmovdqu	20(%rax),%ymm2
	vpmuludq	%ymm1,%ymm9,%ymm6
	vpmuludq	%ymm0,%ymm9,%ymm9
	vpaddq	%ymm6,%ymm14,%ymm14
	vpaddq	%ymm9,%ymm13,%ymm13

	vpmuludq	%ymm1,%ymm2,%ymm6
	vpmuludq	%ymm0,%ymm2,%ymm2
	vpaddq	%ymm6,%ymm15,%ymm15
	vpaddq	%ymm2,%ymm14,%ymm14
	vpmuludq	%ymm3,%ymm10,%ymm6
	vpmuludq	%ymm4,%ymm10,%ymm2
	vpaddq	%ymm6,%ymm12,%ymm12
	vpaddq	%ymm2,%ymm13,%ymm13

	vpmuludq	%ymm3,%ymm5,%ymm3
	vpmuludq	%ymm4,%ymm5,%ymm4
	vpaddq	%ymm3,%ymm13,%ymm2
	vpaddq	%ymm4,%ymm14,%ymm3
	vpmuludq	84(%rax),%ymm0,%ymm4
	vpmuludq	%ymm1,%ymm5,%ymm0
	vmovdqa	64(%rcx),%ymm5
	vpaddq	%ymm4,%ymm15,%ymm4
	vpaddq	%ymm0,%ymm11,%ymm0




	vpsrldq	$8,%ymm12,%ymm8
	vpsrldq	$8,%ymm2,%ymm9
	vpsrldq	$8,%ymm3,%ymm10
	vpsrldq	$8,%ymm4,%ymm6
	vpsrldq	$8,%ymm0,%ymm7
	vpaddq	%ymm8,%ymm12,%ymm12
	vpaddq	%ymm9,%ymm2,%ymm2
	vpaddq	%ymm10,%ymm3,%ymm3
	vpaddq	%ymm6,%ymm4,%ymm4
	vpaddq	%ymm7,%ymm0,%ymm0

	vpermq	$0x2,%ymm3,%ymm10
	vpermq	$0x2,%ymm4,%ymm6
	vpermq	$0x2,%ymm0,%ymm7
	vpermq	$0x2,%ymm12,%ymm8
	vpermq	$0x2,%ymm2,%ymm9
	vpaddq	%ymm10,%ymm3,%ymm3
	vpaddq	%ymm6,%ymm4,%ymm4
	vpaddq	%ymm7,%ymm0,%ymm0
	vpaddq	%ymm8,%ymm12,%ymm12
	vpaddq	%ymm9,%ymm2,%ymm2




	vpsrlq	$26,%ymm3,%ymm14
	vpand	%ymm5,%ymm3,%ymm3
	vpaddq	%ymm14,%ymm4,%ymm4

	vpsrlq	$26,%ymm0,%ymm11
	vpand	%ymm5,%ymm0,%ymm0
	vpaddq	%ymm11,%ymm12,%ymm1

	vpsrlq	$26,%ymm4,%ymm15
	vpand	%ymm5,%ymm4,%ymm4

	vpsrlq	$26,%ymm1,%ymm12
	vpand	%ymm5,%ymm1,%ymm1
	vpaddq	%ymm12,%ymm2,%ymm2

	vpaddq	%ymm15,%ymm0,%ymm0
	vpsllq	$2,%ymm15,%ymm15
	vpaddq	%ymm15,%ymm0,%ymm0

	vpsrlq	$26,%ymm2,%ymm13
	vpand	%ymm5,%ymm2,%ymm2
	vpaddq	%ymm13,%ymm3,%ymm3

	vpsrlq	$26,%ymm0,%ymm11
	vpand	%ymm5,%ymm0,%ymm0
	vpaddq	%ymm11,%ymm1,%ymm1

	vpsrlq	$26,%ymm3,%ymm14
	vpand	%ymm5,%ymm3,%ymm3
	vpaddq	%ymm14,%ymm4,%ymm4

	vmovd	%xmm0,-112(%rdi)
	vmovd	%xmm1,-108(%rdi)
	vmovd	%xmm2,-104(%rdi)
	vmovd	%xmm3,-100(%rdi)
	vmovd	%xmm4,-96(%rdi)
	leaq	8(%r11),%rsp
.cfi_def_cfa	%rsp,8
	vzeroupper
	.byte	0xf3,0xc3
.cfi_endproc	
.size	poly1305_blocks_avx2,.-poly1305_blocks_avx2
.align	64
.Lconst:
.Lmask24:
.long	0x0ffffff,0,0x0ffffff,0,0x0ffffff,0,0x0ffffff,0
.L129:
.long	16777216,0,16777216,0,16777216,0,16777216,0
.Lmask26:
.long	0x3ffffff,0,0x3ffffff,0,0x3ffffff,0,0x3ffffff,0
.Lpermd_avx2:
.long	2,2,2,3,2,0,2,1
.Lpermd_avx512:
.long	0,0,0,1, 0,2,0,3, 0,4,0,5, 0,6,0,7

.L2_44_inp_permd:
.long	0,1,1,2,2,3,7,7
.L2_44_inp_shift:
.quad	0,12,24,64
.L2_44_mask:
.quad	0xfffffffffff,0xfffffffffff,0x3ffffffffff,0xffffffffffffffff
.L2_44_shift_rgt:
.quad	44,44,42,64
.L2_44_shift_lft:
.quad	8,8,10,64

.align	64
.Lx_mask44:
.quad	0xfffffffffff,0xfffffffffff,0xfffffffffff,0xfffffffffff
.quad	0xfffffffffff,0xfffffffffff,0xfffffffffff,0xfffffffffff
.Lx_mask42:
.quad	0x3ffffffffff,0x3ffffffffff,0x3ffffffffff,0x3ffffffffff
.quad	0x3ffffffffff,0x3ffffffffff,0x3ffffffffff,0x3ffffffffff
.byte	80,111,108,121,49,51,48,53,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	16
.globl	xor128_encrypt_n_pad
.type	xor128_encrypt_n_pad,@function
.align	16
xor128_encrypt_n_pad:
	subq	%rdx,%rsi
	subq	%rdx,%rdi
	movq	%rcx,%r10
	shrq	$4,%rcx
	jz	.Ltail_enc
	nop
.Loop_enc_xmm:
	movdqu	(%rsi,%rdx,1),%xmm0
	pxor	(%rdx),%xmm0
	movdqu	%xmm0,(%rdi,%rdx,1)
	movdqa	%xmm0,(%rdx)
	leaq	16(%rdx),%rdx
	decq	%rcx
	jnz	.Loop_enc_xmm

	andq	$15,%r10
	jz	.Ldone_enc

.Ltail_enc:
	movq	$16,%rcx
	subq	%r10,%rcx
	xorl	%eax,%eax
.Loop_enc_byte:
	movb	(%rsi,%rdx,1),%al
	xorb	(%rdx),%al
	movb	%al,(%rdi,%rdx,1)
	movb	%al,(%rdx)
	leaq	1(%rdx),%rdx
	decq	%r10
	jnz	.Loop_enc_byte

	xorl	%eax,%eax
.Loop_enc_pad:
	movb	%al,(%rdx)
	leaq	1(%rdx),%rdx
	decq	%rcx
	jnz	.Loop_enc_pad

.Ldone_enc:
	movq	%rdx,%rax
	.byte	0xf3,0xc3
.size	xor128_encrypt_n_pad,.-xor128_encrypt_n_pad

.globl	xor128_decrypt_n_pad
.type	xor128_decrypt_n_pad,@function
.align	16
xor128_decrypt_n_pad:
	subq	%rdx,%rsi
	subq	%rdx,%rdi
	movq	%rcx,%r10
	shrq	$4,%rcx
	jz	.Ltail_dec
	nop
.Loop_dec_xmm:
	movdqu	(%rsi,%rdx,1),%xmm0
	movdqa	(%rdx),%xmm1
	pxor	%xmm0,%xmm1
	movdqu	%xmm1,(%rdi,%rdx,1)
	movdqa	%xmm0,(%rdx)
	leaq	16(%rdx),%rdx
	decq	%rcx
	jnz	.Loop_dec_xmm

	pxor	%xmm1,%xmm1
	andq	$15,%r10
	jz	.Ldone_dec

.Ltail_dec:
	movq	$16,%rcx
	subq	%r10,%rcx
	xorl	%eax,%eax
	xorq	%r11,%r11
.Loop_dec_byte:
	movb	(%rsi,%rdx,1),%r11b
	movb	(%rdx),%al
	xorb	%r11b,%al
	movb	%al,(%rdi,%rdx,1)
	movb	%r11b,(%rdx)
	leaq	1(%rdx),%rdx
	decq	%r10
	jnz	.Loop_dec_byte

	xorl	%eax,%eax
.Loop_dec_pad:
	movb	%al,(%rdx)
	leaq	1(%rdx),%rdx
	decq	%rcx
	jnz	.Loop_dec_pad

.Ldone_dec:
	movq	%rdx,%rax
	.byte	0xf3,0xc3
.size	xor128_decrypt_n_pad,.-xor128_decrypt_n_pad
