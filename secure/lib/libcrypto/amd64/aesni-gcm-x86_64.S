/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from aesni-gcm-x86_64.pl. */
.text	

.type	_aesni_ctr32_ghash_6x,@function
.align	32
_aesni_ctr32_ghash_6x:
	vmovdqu	32(%r11),%xmm2
	subq	$6,%rdx
	vpxor	%xmm4,%xmm4,%xmm4
	vmovdqu	0-128(%rcx),%xmm15
	vpaddb	%xmm2,%xmm1,%xmm10
	vpaddb	%xmm2,%xmm10,%xmm11
	vpaddb	%xmm2,%xmm11,%xmm12
	vpaddb	%xmm2,%xmm12,%xmm13
	vpaddb	%xmm2,%xmm13,%xmm14
	vpxor	%xmm15,%xmm1,%xmm9
	vmovdqu	%xmm4,16+8(%rsp)
	jmp	.Loop6x

.align	32
.Loop6x:
	addl	$100663296,%ebx
	jc	.Lhandle_ctr32
	vmovdqu	0-32(%r9),%xmm3
	vpaddb	%xmm2,%xmm14,%xmm1
	vpxor	%xmm15,%xmm10,%xmm10
	vpxor	%xmm15,%xmm11,%xmm11

.Lresume_ctr32:
	vmovdqu	%xmm1,(%r8)
	vpclmulqdq	$0x10,%xmm3,%xmm7,%xmm5
	vpxor	%xmm15,%xmm12,%xmm12
	vmovups	16-128(%rcx),%xmm2
	vpclmulqdq	$0x01,%xmm3,%xmm7,%xmm6
	xorq	%r12,%r12
	cmpq	%r14,%r15

	vaesenc	%xmm2,%xmm9,%xmm9
	vmovdqu	48+8(%rsp),%xmm0
	vpxor	%xmm15,%xmm13,%xmm13
	vpclmulqdq	$0x00,%xmm3,%xmm7,%xmm1
	vaesenc	%xmm2,%xmm10,%xmm10
	vpxor	%xmm15,%xmm14,%xmm14
	setnc	%r12b
	vpclmulqdq	$0x11,%xmm3,%xmm7,%xmm7
	vaesenc	%xmm2,%xmm11,%xmm11
	vmovdqu	16-32(%r9),%xmm3
	negq	%r12
	vaesenc	%xmm2,%xmm12,%xmm12
	vpxor	%xmm5,%xmm6,%xmm6
	vpclmulqdq	$0x00,%xmm3,%xmm0,%xmm5
	vpxor	%xmm4,%xmm8,%xmm8
	vaesenc	%xmm2,%xmm13,%xmm13
	vpxor	%xmm5,%xmm1,%xmm4
	andq	$0x60,%r12
	vmovups	32-128(%rcx),%xmm15
	vpclmulqdq	$0x10,%xmm3,%xmm0,%xmm1
	vaesenc	%xmm2,%xmm14,%xmm14

	vpclmulqdq	$0x01,%xmm3,%xmm0,%xmm2
	leaq	(%r14,%r12,1),%r14
	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	16+8(%rsp),%xmm8,%xmm8
	vpclmulqdq	$0x11,%xmm3,%xmm0,%xmm3
	vmovdqu	64+8(%rsp),%xmm0
	vaesenc	%xmm15,%xmm10,%xmm10
	movbeq	88(%r14),%r13
	vaesenc	%xmm15,%xmm11,%xmm11
	movbeq	80(%r14),%r12
	vaesenc	%xmm15,%xmm12,%xmm12
	movq	%r13,32+8(%rsp)
	vaesenc	%xmm15,%xmm13,%xmm13
	movq	%r12,40+8(%rsp)
	vmovdqu	48-32(%r9),%xmm5
	vaesenc	%xmm15,%xmm14,%xmm14

	vmovups	48-128(%rcx),%xmm15
	vpxor	%xmm1,%xmm6,%xmm6
	vpclmulqdq	$0x00,%xmm5,%xmm0,%xmm1
	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	%xmm2,%xmm6,%xmm6
	vpclmulqdq	$0x10,%xmm5,%xmm0,%xmm2
	vaesenc	%xmm15,%xmm10,%xmm10
	vpxor	%xmm3,%xmm7,%xmm7
	vpclmulqdq	$0x01,%xmm5,%xmm0,%xmm3
	vaesenc	%xmm15,%xmm11,%xmm11
	vpclmulqdq	$0x11,%xmm5,%xmm0,%xmm5
	vmovdqu	80+8(%rsp),%xmm0
	vaesenc	%xmm15,%xmm12,%xmm12
	vaesenc	%xmm15,%xmm13,%xmm13
	vpxor	%xmm1,%xmm4,%xmm4
	vmovdqu	64-32(%r9),%xmm1
	vaesenc	%xmm15,%xmm14,%xmm14

	vmovups	64-128(%rcx),%xmm15
	vpxor	%xmm2,%xmm6,%xmm6
	vpclmulqdq	$0x00,%xmm1,%xmm0,%xmm2
	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	%xmm3,%xmm6,%xmm6
	vpclmulqdq	$0x10,%xmm1,%xmm0,%xmm3
	vaesenc	%xmm15,%xmm10,%xmm10
	movbeq	72(%r14),%r13
	vpxor	%xmm5,%xmm7,%xmm7
	vpclmulqdq	$0x01,%xmm1,%xmm0,%xmm5
	vaesenc	%xmm15,%xmm11,%xmm11
	movbeq	64(%r14),%r12
	vpclmulqdq	$0x11,%xmm1,%xmm0,%xmm1
	vmovdqu	96+8(%rsp),%xmm0
	vaesenc	%xmm15,%xmm12,%xmm12
	movq	%r13,48+8(%rsp)
	vaesenc	%xmm15,%xmm13,%xmm13
	movq	%r12,56+8(%rsp)
	vpxor	%xmm2,%xmm4,%xmm4
	vmovdqu	96-32(%r9),%xmm2
	vaesenc	%xmm15,%xmm14,%xmm14

	vmovups	80-128(%rcx),%xmm15
	vpxor	%xmm3,%xmm6,%xmm6
	vpclmulqdq	$0x00,%xmm2,%xmm0,%xmm3
	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	%xmm5,%xmm6,%xmm6
	vpclmulqdq	$0x10,%xmm2,%xmm0,%xmm5
	vaesenc	%xmm15,%xmm10,%xmm10
	movbeq	56(%r14),%r13
	vpxor	%xmm1,%xmm7,%xmm7
	vpclmulqdq	$0x01,%xmm2,%xmm0,%xmm1
	vpxor	112+8(%rsp),%xmm8,%xmm8
	vaesenc	%xmm15,%xmm11,%xmm11
	movbeq	48(%r14),%r12
	vpclmulqdq	$0x11,%xmm2,%xmm0,%xmm2
	vaesenc	%xmm15,%xmm12,%xmm12
	movq	%r13,64+8(%rsp)
	vaesenc	%xmm15,%xmm13,%xmm13
	movq	%r12,72+8(%rsp)
	vpxor	%xmm3,%xmm4,%xmm4
	vmovdqu	112-32(%r9),%xmm3
	vaesenc	%xmm15,%xmm14,%xmm14

	vmovups	96-128(%rcx),%xmm15
	vpxor	%xmm5,%xmm6,%xmm6
	vpclmulqdq	$0x10,%xmm3,%xmm8,%xmm5
	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	%xmm1,%xmm6,%xmm6
	vpclmulqdq	$0x01,%xmm3,%xmm8,%xmm1
	vaesenc	%xmm15,%xmm10,%xmm10
	movbeq	40(%r14),%r13
	vpxor	%xmm2,%xmm7,%xmm7
	vpclmulqdq	$0x00,%xmm3,%xmm8,%xmm2
	vaesenc	%xmm15,%xmm11,%xmm11
	movbeq	32(%r14),%r12
	vpclmulqdq	$0x11,%xmm3,%xmm8,%xmm8
	vaesenc	%xmm15,%xmm12,%xmm12
	movq	%r13,80+8(%rsp)
	vaesenc	%xmm15,%xmm13,%xmm13
	movq	%r12,88+8(%rsp)
	vpxor	%xmm5,%xmm6,%xmm6
	vaesenc	%xmm15,%xmm14,%xmm14
	vpxor	%xmm1,%xmm6,%xmm6

	vmovups	112-128(%rcx),%xmm15
	vpslldq	$8,%xmm6,%xmm5
	vpxor	%xmm2,%xmm4,%xmm4
	vmovdqu	16(%r11),%xmm3

	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	%xmm8,%xmm7,%xmm7
	vaesenc	%xmm15,%xmm10,%xmm10
	vpxor	%xmm5,%xmm4,%xmm4
	movbeq	24(%r14),%r13
	vaesenc	%xmm15,%xmm11,%xmm11
	movbeq	16(%r14),%r12
	vpalignr	$8,%xmm4,%xmm4,%xmm0
	vpclmulqdq	$0x10,%xmm3,%xmm4,%xmm4
	movq	%r13,96+8(%rsp)
	vaesenc	%xmm15,%xmm12,%xmm12
	movq	%r12,104+8(%rsp)
	vaesenc	%xmm15,%xmm13,%xmm13
	vmovups	128-128(%rcx),%xmm1
	vaesenc	%xmm15,%xmm14,%xmm14

	vaesenc	%xmm1,%xmm9,%xmm9
	vmovups	144-128(%rcx),%xmm15
	vaesenc	%xmm1,%xmm10,%xmm10
	vpsrldq	$8,%xmm6,%xmm6
	vaesenc	%xmm1,%xmm11,%xmm11
	vpxor	%xmm6,%xmm7,%xmm7
	vaesenc	%xmm1,%xmm12,%xmm12
	vpxor	%xmm0,%xmm4,%xmm4
	movbeq	8(%r14),%r13
	vaesenc	%xmm1,%xmm13,%xmm13
	movbeq	0(%r14),%r12
	vaesenc	%xmm1,%xmm14,%xmm14
	vmovups	160-128(%rcx),%xmm1
	cmpl	$11,%ebp
	jb	.Lenc_tail

	vaesenc	%xmm15,%xmm9,%xmm9
	vaesenc	%xmm15,%xmm10,%xmm10
	vaesenc	%xmm15,%xmm11,%xmm11
	vaesenc	%xmm15,%xmm12,%xmm12
	vaesenc	%xmm15,%xmm13,%xmm13
	vaesenc	%xmm15,%xmm14,%xmm14

	vaesenc	%xmm1,%xmm9,%xmm9
	vaesenc	%xmm1,%xmm10,%xmm10
	vaesenc	%xmm1,%xmm11,%xmm11
	vaesenc	%xmm1,%xmm12,%xmm12
	vaesenc	%xmm1,%xmm13,%xmm13
	vmovups	176-128(%rcx),%xmm15
	vaesenc	%xmm1,%xmm14,%xmm14
	vmovups	192-128(%rcx),%xmm1
	je	.Lenc_tail

	vaesenc	%xmm15,%xmm9,%xmm9
	vaesenc	%xmm15,%xmm10,%xmm10
	vaesenc	%xmm15,%xmm11,%xmm11
	vaesenc	%xmm15,%xmm12,%xmm12
	vaesenc	%xmm15,%xmm13,%xmm13
	vaesenc	%xmm15,%xmm14,%xmm14

	vaesenc	%xmm1,%xmm9,%xmm9
	vaesenc	%xmm1,%xmm10,%xmm10
	vaesenc	%xmm1,%xmm11,%xmm11
	vaesenc	%xmm1,%xmm12,%xmm12
	vaesenc	%xmm1,%xmm13,%xmm13
	vmovups	208-128(%rcx),%xmm15
	vaesenc	%xmm1,%xmm14,%xmm14
	vmovups	224-128(%rcx),%xmm1
	jmp	.Lenc_tail

.align	32
.Lhandle_ctr32:
	vmovdqu	(%r11),%xmm0
	vpshufb	%xmm0,%xmm1,%xmm6
	vmovdqu	48(%r11),%xmm5
	vpaddd	64(%r11),%xmm6,%xmm10
	vpaddd	%xmm5,%xmm6,%xmm11
	vmovdqu	0-32(%r9),%xmm3
	vpaddd	%xmm5,%xmm10,%xmm12
	vpshufb	%xmm0,%xmm10,%xmm10
	vpaddd	%xmm5,%xmm11,%xmm13
	vpshufb	%xmm0,%xmm11,%xmm11
	vpxor	%xmm15,%xmm10,%xmm10
	vpaddd	%xmm5,%xmm12,%xmm14
	vpshufb	%xmm0,%xmm12,%xmm12
	vpxor	%xmm15,%xmm11,%xmm11
	vpaddd	%xmm5,%xmm13,%xmm1
	vpshufb	%xmm0,%xmm13,%xmm13
	vpshufb	%xmm0,%xmm14,%xmm14
	vpshufb	%xmm0,%xmm1,%xmm1
	jmp	.Lresume_ctr32

.align	32
.Lenc_tail:
	vaesenc	%xmm15,%xmm9,%xmm9
	vmovdqu	%xmm7,16+8(%rsp)
	vpalignr	$8,%xmm4,%xmm4,%xmm8
	vaesenc	%xmm15,%xmm10,%xmm10
	vpclmulqdq	$0x10,%xmm3,%xmm4,%xmm4
	vpxor	0(%rdi),%xmm1,%xmm2
	vaesenc	%xmm15,%xmm11,%xmm11
	vpxor	16(%rdi),%xmm1,%xmm0
	vaesenc	%xmm15,%xmm12,%xmm12
	vpxor	32(%rdi),%xmm1,%xmm5
	vaesenc	%xmm15,%xmm13,%xmm13
	vpxor	48(%rdi),%xmm1,%xmm6
	vaesenc	%xmm15,%xmm14,%xmm14
	vpxor	64(%rdi),%xmm1,%xmm7
	vpxor	80(%rdi),%xmm1,%xmm3
	vmovdqu	(%r8),%xmm1

	vaesenclast	%xmm2,%xmm9,%xmm9
	vmovdqu	32(%r11),%xmm2
	vaesenclast	%xmm0,%xmm10,%xmm10
	vpaddb	%xmm2,%xmm1,%xmm0
	movq	%r13,112+8(%rsp)
	leaq	96(%rdi),%rdi
	vaesenclast	%xmm5,%xmm11,%xmm11
	vpaddb	%xmm2,%xmm0,%xmm5
	movq	%r12,120+8(%rsp)
	leaq	96(%rsi),%rsi
	vmovdqu	0-128(%rcx),%xmm15
	vaesenclast	%xmm6,%xmm12,%xmm12
	vpaddb	%xmm2,%xmm5,%xmm6
	vaesenclast	%xmm7,%xmm13,%xmm13
	vpaddb	%xmm2,%xmm6,%xmm7
	vaesenclast	%xmm3,%xmm14,%xmm14
	vpaddb	%xmm2,%xmm7,%xmm3

	addq	$0x60,%r10
	subq	$0x6,%rdx
	jc	.L6x_done

	vmovups	%xmm9,-96(%rsi)
	vpxor	%xmm15,%xmm1,%xmm9
	vmovups	%xmm10,-80(%rsi)
	vmovdqa	%xmm0,%xmm10
	vmovups	%xmm11,-64(%rsi)
	vmovdqa	%xmm5,%xmm11
	vmovups	%xmm12,-48(%rsi)
	vmovdqa	%xmm6,%xmm12
	vmovups	%xmm13,-32(%rsi)
	vmovdqa	%xmm7,%xmm13
	vmovups	%xmm14,-16(%rsi)
	vmovdqa	%xmm3,%xmm14
	vmovdqu	32+8(%rsp),%xmm7
	jmp	.Loop6x

.L6x_done:
	vpxor	16+8(%rsp),%xmm8,%xmm8
	vpxor	%xmm4,%xmm8,%xmm8

	.byte	0xf3,0xc3
.size	_aesni_ctr32_ghash_6x,.-_aesni_ctr32_ghash_6x
.globl	aesni_gcm_decrypt
.type	aesni_gcm_decrypt,@function
.align	32
aesni_gcm_decrypt:
.cfi_startproc	
	xorq	%r10,%r10
	cmpq	$0x60,%rdx
	jb	.Lgcm_dec_abort

	leaq	(%rsp),%rax
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
	vzeroupper

	vmovdqu	(%r8),%xmm1
	addq	$-128,%rsp
	movl	12(%r8),%ebx
	leaq	.Lbswap_mask(%rip),%r11
	leaq	-128(%rcx),%r14
	movq	$0xf80,%r15
	vmovdqu	(%r9),%xmm8
	andq	$-128,%rsp
	vmovdqu	(%r11),%xmm0
	leaq	128(%rcx),%rcx
	leaq	32+32(%r9),%r9
	movl	240-128(%rcx),%ebp
	vpshufb	%xmm0,%xmm8,%xmm8

	andq	%r15,%r14
	andq	%rsp,%r15
	subq	%r14,%r15
	jc	.Ldec_no_key_aliasing
	cmpq	$768,%r15
	jnc	.Ldec_no_key_aliasing
	subq	%r15,%rsp
.Ldec_no_key_aliasing:

	vmovdqu	80(%rdi),%xmm7
	leaq	(%rdi),%r14
	vmovdqu	64(%rdi),%xmm4
	leaq	-192(%rdi,%rdx,1),%r15
	vmovdqu	48(%rdi),%xmm5
	shrq	$4,%rdx
	xorq	%r10,%r10
	vmovdqu	32(%rdi),%xmm6
	vpshufb	%xmm0,%xmm7,%xmm7
	vmovdqu	16(%rdi),%xmm2
	vpshufb	%xmm0,%xmm4,%xmm4
	vmovdqu	(%rdi),%xmm3
	vpshufb	%xmm0,%xmm5,%xmm5
	vmovdqu	%xmm4,48(%rsp)
	vpshufb	%xmm0,%xmm6,%xmm6
	vmovdqu	%xmm5,64(%rsp)
	vpshufb	%xmm0,%xmm2,%xmm2
	vmovdqu	%xmm6,80(%rsp)
	vpshufb	%xmm0,%xmm3,%xmm3
	vmovdqu	%xmm2,96(%rsp)
	vmovdqu	%xmm3,112(%rsp)

	call	_aesni_ctr32_ghash_6x

	vmovups	%xmm9,-96(%rsi)
	vmovups	%xmm10,-80(%rsi)
	vmovups	%xmm11,-64(%rsi)
	vmovups	%xmm12,-48(%rsi)
	vmovups	%xmm13,-32(%rsi)
	vmovups	%xmm14,-16(%rsi)

	vpshufb	(%r11),%xmm8,%xmm8
	vmovdqu	%xmm8,-64(%r9)

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
.Lgcm_dec_abort:
	movq	%r10,%rax
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_gcm_decrypt,.-aesni_gcm_decrypt
.type	_aesni_ctr32_6x,@function
.align	32
_aesni_ctr32_6x:
	vmovdqu	0-128(%rcx),%xmm4
	vmovdqu	32(%r11),%xmm2
	leaq	-1(%rbp),%r13
	vmovups	16-128(%rcx),%xmm15
	leaq	32-128(%rcx),%r12
	vpxor	%xmm4,%xmm1,%xmm9
	addl	$100663296,%ebx
	jc	.Lhandle_ctr32_2
	vpaddb	%xmm2,%xmm1,%xmm10
	vpaddb	%xmm2,%xmm10,%xmm11
	vpxor	%xmm4,%xmm10,%xmm10
	vpaddb	%xmm2,%xmm11,%xmm12
	vpxor	%xmm4,%xmm11,%xmm11
	vpaddb	%xmm2,%xmm12,%xmm13
	vpxor	%xmm4,%xmm12,%xmm12
	vpaddb	%xmm2,%xmm13,%xmm14
	vpxor	%xmm4,%xmm13,%xmm13
	vpaddb	%xmm2,%xmm14,%xmm1
	vpxor	%xmm4,%xmm14,%xmm14
	jmp	.Loop_ctr32

.align	16
.Loop_ctr32:
	vaesenc	%xmm15,%xmm9,%xmm9
	vaesenc	%xmm15,%xmm10,%xmm10
	vaesenc	%xmm15,%xmm11,%xmm11
	vaesenc	%xmm15,%xmm12,%xmm12
	vaesenc	%xmm15,%xmm13,%xmm13
	vaesenc	%xmm15,%xmm14,%xmm14
	vmovups	(%r12),%xmm15
	leaq	16(%r12),%r12
	decl	%r13d
	jnz	.Loop_ctr32

	vmovdqu	(%r12),%xmm3
	vaesenc	%xmm15,%xmm9,%xmm9
	vpxor	0(%rdi),%xmm3,%xmm4
	vaesenc	%xmm15,%xmm10,%xmm10
	vpxor	16(%rdi),%xmm3,%xmm5
	vaesenc	%xmm15,%xmm11,%xmm11
	vpxor	32(%rdi),%xmm3,%xmm6
	vaesenc	%xmm15,%xmm12,%xmm12
	vpxor	48(%rdi),%xmm3,%xmm8
	vaesenc	%xmm15,%xmm13,%xmm13
	vpxor	64(%rdi),%xmm3,%xmm2
	vaesenc	%xmm15,%xmm14,%xmm14
	vpxor	80(%rdi),%xmm3,%xmm3
	leaq	96(%rdi),%rdi

	vaesenclast	%xmm4,%xmm9,%xmm9
	vaesenclast	%xmm5,%xmm10,%xmm10
	vaesenclast	%xmm6,%xmm11,%xmm11
	vaesenclast	%xmm8,%xmm12,%xmm12
	vaesenclast	%xmm2,%xmm13,%xmm13
	vaesenclast	%xmm3,%xmm14,%xmm14
	vmovups	%xmm9,0(%rsi)
	vmovups	%xmm10,16(%rsi)
	vmovups	%xmm11,32(%rsi)
	vmovups	%xmm12,48(%rsi)
	vmovups	%xmm13,64(%rsi)
	vmovups	%xmm14,80(%rsi)
	leaq	96(%rsi),%rsi

	.byte	0xf3,0xc3
.align	32
.Lhandle_ctr32_2:
	vpshufb	%xmm0,%xmm1,%xmm6
	vmovdqu	48(%r11),%xmm5
	vpaddd	64(%r11),%xmm6,%xmm10
	vpaddd	%xmm5,%xmm6,%xmm11
	vpaddd	%xmm5,%xmm10,%xmm12
	vpshufb	%xmm0,%xmm10,%xmm10
	vpaddd	%xmm5,%xmm11,%xmm13
	vpshufb	%xmm0,%xmm11,%xmm11
	vpxor	%xmm4,%xmm10,%xmm10
	vpaddd	%xmm5,%xmm12,%xmm14
	vpshufb	%xmm0,%xmm12,%xmm12
	vpxor	%xmm4,%xmm11,%xmm11
	vpaddd	%xmm5,%xmm13,%xmm1
	vpshufb	%xmm0,%xmm13,%xmm13
	vpxor	%xmm4,%xmm12,%xmm12
	vpshufb	%xmm0,%xmm14,%xmm14
	vpxor	%xmm4,%xmm13,%xmm13
	vpshufb	%xmm0,%xmm1,%xmm1
	vpxor	%xmm4,%xmm14,%xmm14
	jmp	.Loop_ctr32
.size	_aesni_ctr32_6x,.-_aesni_ctr32_6x

.globl	aesni_gcm_encrypt
.type	aesni_gcm_encrypt,@function
.align	32
aesni_gcm_encrypt:
.cfi_startproc	
	xorq	%r10,%r10
	cmpq	$288,%rdx
	jb	.Lgcm_enc_abort

	leaq	(%rsp),%rax
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
	vzeroupper

	vmovdqu	(%r8),%xmm1
	addq	$-128,%rsp
	movl	12(%r8),%ebx
	leaq	.Lbswap_mask(%rip),%r11
	leaq	-128(%rcx),%r14
	movq	$0xf80,%r15
	leaq	128(%rcx),%rcx
	vmovdqu	(%r11),%xmm0
	andq	$-128,%rsp
	movl	240-128(%rcx),%ebp

	andq	%r15,%r14
	andq	%rsp,%r15
	subq	%r14,%r15
	jc	.Lenc_no_key_aliasing
	cmpq	$768,%r15
	jnc	.Lenc_no_key_aliasing
	subq	%r15,%rsp
.Lenc_no_key_aliasing:

	leaq	(%rsi),%r14
	leaq	-192(%rsi,%rdx,1),%r15
	shrq	$4,%rdx

	call	_aesni_ctr32_6x
	vpshufb	%xmm0,%xmm9,%xmm8
	vpshufb	%xmm0,%xmm10,%xmm2
	vmovdqu	%xmm8,112(%rsp)
	vpshufb	%xmm0,%xmm11,%xmm4
	vmovdqu	%xmm2,96(%rsp)
	vpshufb	%xmm0,%xmm12,%xmm5
	vmovdqu	%xmm4,80(%rsp)
	vpshufb	%xmm0,%xmm13,%xmm6
	vmovdqu	%xmm5,64(%rsp)
	vpshufb	%xmm0,%xmm14,%xmm7
	vmovdqu	%xmm6,48(%rsp)

	call	_aesni_ctr32_6x

	vmovdqu	(%r9),%xmm8
	leaq	32+32(%r9),%r9
	subq	$12,%rdx
	movq	$192,%r10
	vpshufb	%xmm0,%xmm8,%xmm8

	call	_aesni_ctr32_ghash_6x
	vmovdqu	32(%rsp),%xmm7
	vmovdqu	(%r11),%xmm0
	vmovdqu	0-32(%r9),%xmm3
	vpunpckhqdq	%xmm7,%xmm7,%xmm1
	vmovdqu	32-32(%r9),%xmm15
	vmovups	%xmm9,-96(%rsi)
	vpshufb	%xmm0,%xmm9,%xmm9
	vpxor	%xmm7,%xmm1,%xmm1
	vmovups	%xmm10,-80(%rsi)
	vpshufb	%xmm0,%xmm10,%xmm10
	vmovups	%xmm11,-64(%rsi)
	vpshufb	%xmm0,%xmm11,%xmm11
	vmovups	%xmm12,-48(%rsi)
	vpshufb	%xmm0,%xmm12,%xmm12
	vmovups	%xmm13,-32(%rsi)
	vpshufb	%xmm0,%xmm13,%xmm13
	vmovups	%xmm14,-16(%rsi)
	vpshufb	%xmm0,%xmm14,%xmm14
	vmovdqu	%xmm9,16(%rsp)
	vmovdqu	48(%rsp),%xmm6
	vmovdqu	16-32(%r9),%xmm0
	vpunpckhqdq	%xmm6,%xmm6,%xmm2
	vpclmulqdq	$0x00,%xmm3,%xmm7,%xmm5
	vpxor	%xmm6,%xmm2,%xmm2
	vpclmulqdq	$0x11,%xmm3,%xmm7,%xmm7
	vpclmulqdq	$0x00,%xmm15,%xmm1,%xmm1

	vmovdqu	64(%rsp),%xmm9
	vpclmulqdq	$0x00,%xmm0,%xmm6,%xmm4
	vmovdqu	48-32(%r9),%xmm3
	vpxor	%xmm5,%xmm4,%xmm4
	vpunpckhqdq	%xmm9,%xmm9,%xmm5
	vpclmulqdq	$0x11,%xmm0,%xmm6,%xmm6
	vpxor	%xmm9,%xmm5,%xmm5
	vpxor	%xmm7,%xmm6,%xmm6
	vpclmulqdq	$0x10,%xmm15,%xmm2,%xmm2
	vmovdqu	80-32(%r9),%xmm15
	vpxor	%xmm1,%xmm2,%xmm2

	vmovdqu	80(%rsp),%xmm1
	vpclmulqdq	$0x00,%xmm3,%xmm9,%xmm7
	vmovdqu	64-32(%r9),%xmm0
	vpxor	%xmm4,%xmm7,%xmm7
	vpunpckhqdq	%xmm1,%xmm1,%xmm4
	vpclmulqdq	$0x11,%xmm3,%xmm9,%xmm9
	vpxor	%xmm1,%xmm4,%xmm4
	vpxor	%xmm6,%xmm9,%xmm9
	vpclmulqdq	$0x00,%xmm15,%xmm5,%xmm5
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	96(%rsp),%xmm2
	vpclmulqdq	$0x00,%xmm0,%xmm1,%xmm6
	vmovdqu	96-32(%r9),%xmm3
	vpxor	%xmm7,%xmm6,%xmm6
	vpunpckhqdq	%xmm2,%xmm2,%xmm7
	vpclmulqdq	$0x11,%xmm0,%xmm1,%xmm1
	vpxor	%xmm2,%xmm7,%xmm7
	vpxor	%xmm9,%xmm1,%xmm1
	vpclmulqdq	$0x10,%xmm15,%xmm4,%xmm4
	vmovdqu	128-32(%r9),%xmm15
	vpxor	%xmm5,%xmm4,%xmm4

	vpxor	112(%rsp),%xmm8,%xmm8
	vpclmulqdq	$0x00,%xmm3,%xmm2,%xmm5
	vmovdqu	112-32(%r9),%xmm0
	vpunpckhqdq	%xmm8,%xmm8,%xmm9
	vpxor	%xmm6,%xmm5,%xmm5
	vpclmulqdq	$0x11,%xmm3,%xmm2,%xmm2
	vpxor	%xmm8,%xmm9,%xmm9
	vpxor	%xmm1,%xmm2,%xmm2
	vpclmulqdq	$0x00,%xmm15,%xmm7,%xmm7
	vpxor	%xmm4,%xmm7,%xmm4

	vpclmulqdq	$0x00,%xmm0,%xmm8,%xmm6
	vmovdqu	0-32(%r9),%xmm3
	vpunpckhqdq	%xmm14,%xmm14,%xmm1
	vpclmulqdq	$0x11,%xmm0,%xmm8,%xmm8
	vpxor	%xmm14,%xmm1,%xmm1
	vpxor	%xmm5,%xmm6,%xmm5
	vpclmulqdq	$0x10,%xmm15,%xmm9,%xmm9
	vmovdqu	32-32(%r9),%xmm15
	vpxor	%xmm2,%xmm8,%xmm7
	vpxor	%xmm4,%xmm9,%xmm6

	vmovdqu	16-32(%r9),%xmm0
	vpxor	%xmm5,%xmm7,%xmm9
	vpclmulqdq	$0x00,%xmm3,%xmm14,%xmm4
	vpxor	%xmm9,%xmm6,%xmm6
	vpunpckhqdq	%xmm13,%xmm13,%xmm2
	vpclmulqdq	$0x11,%xmm3,%xmm14,%xmm14
	vpxor	%xmm13,%xmm2,%xmm2
	vpslldq	$8,%xmm6,%xmm9
	vpclmulqdq	$0x00,%xmm15,%xmm1,%xmm1
	vpxor	%xmm9,%xmm5,%xmm8
	vpsrldq	$8,%xmm6,%xmm6
	vpxor	%xmm6,%xmm7,%xmm7

	vpclmulqdq	$0x00,%xmm0,%xmm13,%xmm5
	vmovdqu	48-32(%r9),%xmm3
	vpxor	%xmm4,%xmm5,%xmm5
	vpunpckhqdq	%xmm12,%xmm12,%xmm9
	vpclmulqdq	$0x11,%xmm0,%xmm13,%xmm13
	vpxor	%xmm12,%xmm9,%xmm9
	vpxor	%xmm14,%xmm13,%xmm13
	vpalignr	$8,%xmm8,%xmm8,%xmm14
	vpclmulqdq	$0x10,%xmm15,%xmm2,%xmm2
	vmovdqu	80-32(%r9),%xmm15
	vpxor	%xmm1,%xmm2,%xmm2

	vpclmulqdq	$0x00,%xmm3,%xmm12,%xmm4
	vmovdqu	64-32(%r9),%xmm0
	vpxor	%xmm5,%xmm4,%xmm4
	vpunpckhqdq	%xmm11,%xmm11,%xmm1
	vpclmulqdq	$0x11,%xmm3,%xmm12,%xmm12
	vpxor	%xmm11,%xmm1,%xmm1
	vpxor	%xmm13,%xmm12,%xmm12
	vxorps	16(%rsp),%xmm7,%xmm7
	vpclmulqdq	$0x00,%xmm15,%xmm9,%xmm9
	vpxor	%xmm2,%xmm9,%xmm9

	vpclmulqdq	$0x10,16(%r11),%xmm8,%xmm8
	vxorps	%xmm14,%xmm8,%xmm8

	vpclmulqdq	$0x00,%xmm0,%xmm11,%xmm5
	vmovdqu	96-32(%r9),%xmm3
	vpxor	%xmm4,%xmm5,%xmm5
	vpunpckhqdq	%xmm10,%xmm10,%xmm2
	vpclmulqdq	$0x11,%xmm0,%xmm11,%xmm11
	vpxor	%xmm10,%xmm2,%xmm2
	vpalignr	$8,%xmm8,%xmm8,%xmm14
	vpxor	%xmm12,%xmm11,%xmm11
	vpclmulqdq	$0x10,%xmm15,%xmm1,%xmm1
	vmovdqu	128-32(%r9),%xmm15
	vpxor	%xmm9,%xmm1,%xmm1

	vxorps	%xmm7,%xmm14,%xmm14
	vpclmulqdq	$0x10,16(%r11),%xmm8,%xmm8
	vxorps	%xmm14,%xmm8,%xmm8

	vpclmulqdq	$0x00,%xmm3,%xmm10,%xmm4
	vmovdqu	112-32(%r9),%xmm0
	vpxor	%xmm5,%xmm4,%xmm4
	vpunpckhqdq	%xmm8,%xmm8,%xmm9
	vpclmulqdq	$0x11,%xmm3,%xmm10,%xmm10
	vpxor	%xmm8,%xmm9,%xmm9
	vpxor	%xmm11,%xmm10,%xmm10
	vpclmulqdq	$0x00,%xmm15,%xmm2,%xmm2
	vpxor	%xmm1,%xmm2,%xmm2

	vpclmulqdq	$0x00,%xmm0,%xmm8,%xmm5
	vpclmulqdq	$0x11,%xmm0,%xmm8,%xmm7
	vpxor	%xmm4,%xmm5,%xmm5
	vpclmulqdq	$0x10,%xmm15,%xmm9,%xmm6
	vpxor	%xmm10,%xmm7,%xmm7
	vpxor	%xmm2,%xmm6,%xmm6

	vpxor	%xmm5,%xmm7,%xmm4
	vpxor	%xmm4,%xmm6,%xmm6
	vpslldq	$8,%xmm6,%xmm1
	vmovdqu	16(%r11),%xmm3
	vpsrldq	$8,%xmm6,%xmm6
	vpxor	%xmm1,%xmm5,%xmm8
	vpxor	%xmm6,%xmm7,%xmm7

	vpalignr	$8,%xmm8,%xmm8,%xmm2
	vpclmulqdq	$0x10,%xmm3,%xmm8,%xmm8
	vpxor	%xmm2,%xmm8,%xmm8

	vpalignr	$8,%xmm8,%xmm8,%xmm2
	vpclmulqdq	$0x10,%xmm3,%xmm8,%xmm8
	vpxor	%xmm7,%xmm2,%xmm2
	vpxor	%xmm2,%xmm8,%xmm8
	vpshufb	(%r11),%xmm8,%xmm8
	vmovdqu	%xmm8,-64(%r9)

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
.Lgcm_enc_abort:
	movq	%r10,%rax
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_gcm_encrypt,.-aesni_gcm_encrypt
.align	64
.Lbswap_mask:
.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.Lpoly:
.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
.Lone_msb:
.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
.Ltwo_lsb:
.byte	2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.Lone_lsb:
.byte	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.byte	65,69,83,45,78,73,32,71,67,77,32,109,111,100,117,108,101,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	64
