/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from rc4-x86_64.pl. */
.text	


.globl	RC4
.type	RC4,@function
.align	16
RC4:	orq	%rsi,%rsi
	jne	.Lentry
	.byte	0xf3,0xc3
.Lentry:
.cfi_startproc	
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-16
	pushq	%r12
.cfi_adjust_cfa_offset	8
.cfi_offset	%r12,-24
	pushq	%r13
.cfi_adjust_cfa_offset	8
.cfi_offset	%r13,-32
.Lprologue:
	movq	%rsi,%r11
	movq	%rdx,%r12
	movq	%rcx,%r13
	xorq	%r10,%r10
	xorq	%rcx,%rcx

	leaq	8(%rdi),%rdi
	movb	-8(%rdi),%r10b
	movb	-4(%rdi),%cl
	cmpl	$-1,256(%rdi)
	je	.LRC4_CHAR
	movl	OPENSSL_ia32cap_P(%rip),%r8d
	xorq	%rbx,%rbx
	incb	%r10b
	subq	%r10,%rbx
	subq	%r12,%r13
	movl	(%rdi,%r10,4),%eax
	testq	$-16,%r11
	jz	.Lloop1
	btl	$30,%r8d
	jc	.Lintel
	andq	$7,%rbx
	leaq	1(%r10),%rsi
	jz	.Loop8
	subq	%rbx,%r11
.Loop8_warmup:
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	%edx,(%rdi,%r10,4)
	addb	%dl,%al
	incb	%r10b
	movl	(%rdi,%rax,4),%edx
	movl	(%rdi,%r10,4),%eax
	xorb	(%r12),%dl
	movb	%dl,(%r12,%r13,1)
	leaq	1(%r12),%r12
	decq	%rbx
	jnz	.Loop8_warmup

	leaq	1(%r10),%rsi
	jmp	.Loop8
.align	16
.Loop8:
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	0(%rdi,%rsi,4),%ebx
	rorq	$8,%r8
	movl	%edx,0(%rdi,%r10,4)
	addb	%al,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	%bl,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	movl	4(%rdi,%rsi,4),%eax
	rorq	$8,%r8
	movl	%edx,4(%rdi,%r10,4)
	addb	%bl,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	8(%rdi,%rsi,4),%ebx
	rorq	$8,%r8
	movl	%edx,8(%rdi,%r10,4)
	addb	%al,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	%bl,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	movl	12(%rdi,%rsi,4),%eax
	rorq	$8,%r8
	movl	%edx,12(%rdi,%r10,4)
	addb	%bl,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	16(%rdi,%rsi,4),%ebx
	rorq	$8,%r8
	movl	%edx,16(%rdi,%r10,4)
	addb	%al,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	%bl,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	movl	20(%rdi,%rsi,4),%eax
	rorq	$8,%r8
	movl	%edx,20(%rdi,%r10,4)
	addb	%bl,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	24(%rdi,%rsi,4),%ebx
	rorq	$8,%r8
	movl	%edx,24(%rdi,%r10,4)
	addb	%al,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	$8,%sil
	addb	%bl,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	movl	-4(%rdi,%rsi,4),%eax
	rorq	$8,%r8
	movl	%edx,28(%rdi,%r10,4)
	addb	%bl,%dl
	movb	(%rdi,%rdx,4),%r8b
	addb	$8,%r10b
	rorq	$8,%r8
	subq	$8,%r11

	xorq	(%r12),%r8
	movq	%r8,(%r12,%r13,1)
	leaq	8(%r12),%r12

	testq	$-8,%r11
	jnz	.Loop8
	cmpq	$0,%r11
	jne	.Lloop1
	jmp	.Lexit

.align	16
.Lintel:
	testq	$-32,%r11
	jz	.Lloop1
	andq	$15,%rbx
	jz	.Loop16_is_hot
	subq	%rbx,%r11
.Loop16_warmup:
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	%edx,(%rdi,%r10,4)
	addb	%dl,%al
	incb	%r10b
	movl	(%rdi,%rax,4),%edx
	movl	(%rdi,%r10,4),%eax
	xorb	(%r12),%dl
	movb	%dl,(%r12,%r13,1)
	leaq	1(%r12),%r12
	decq	%rbx
	jnz	.Loop16_warmup

	movq	%rcx,%rbx
	xorq	%rcx,%rcx
	movb	%bl,%cl

.Loop16_is_hot:
	leaq	(%rdi,%r10,4),%rsi
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	pxor	%xmm0,%xmm0
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	4(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,0(%rsi)
	addb	%bl,%cl
	pinsrw	$0,(%rdi,%rax,4),%xmm0
	jmp	.Loop16_enter
.align	16
.Loop16:
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	pxor	%xmm0,%xmm2
	psllq	$8,%xmm1
	pxor	%xmm0,%xmm0
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	4(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,0(%rsi)
	pxor	%xmm1,%xmm2
	addb	%bl,%cl
	pinsrw	$0,(%rdi,%rax,4),%xmm0
	movdqu	%xmm2,(%r12,%r13,1)
	leaq	16(%r12),%r12
.Loop16_enter:
	movl	(%rdi,%rcx,4),%edx
	pxor	%xmm1,%xmm1
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	8(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,4(%rsi)
	addb	%al,%cl
	pinsrw	$0,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	12(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,8(%rsi)
	addb	%bl,%cl
	pinsrw	$1,(%rdi,%rax,4),%xmm0
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	16(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,12(%rsi)
	addb	%al,%cl
	pinsrw	$1,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	20(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,16(%rsi)
	addb	%bl,%cl
	pinsrw	$2,(%rdi,%rax,4),%xmm0
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	24(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,20(%rsi)
	addb	%al,%cl
	pinsrw	$2,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	28(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,24(%rsi)
	addb	%bl,%cl
	pinsrw	$3,(%rdi,%rax,4),%xmm0
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	32(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,28(%rsi)
	addb	%al,%cl
	pinsrw	$3,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	36(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,32(%rsi)
	addb	%bl,%cl
	pinsrw	$4,(%rdi,%rax,4),%xmm0
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	40(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,36(%rsi)
	addb	%al,%cl
	pinsrw	$4,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	44(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,40(%rsi)
	addb	%bl,%cl
	pinsrw	$5,(%rdi,%rax,4),%xmm0
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	48(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,44(%rsi)
	addb	%al,%cl
	pinsrw	$5,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	52(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,48(%rsi)
	addb	%bl,%cl
	pinsrw	$6,(%rdi,%rax,4),%xmm0
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movl	56(%rsi),%eax
	movzbl	%bl,%ebx
	movl	%edx,52(%rsi)
	addb	%al,%cl
	pinsrw	$6,(%rdi,%rbx,4),%xmm1
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	addb	%dl,%al
	movl	60(%rsi),%ebx
	movzbl	%al,%eax
	movl	%edx,56(%rsi)
	addb	%bl,%cl
	pinsrw	$7,(%rdi,%rax,4),%xmm0
	addb	$16,%r10b
	movdqu	(%r12),%xmm2
	movl	(%rdi,%rcx,4),%edx
	movl	%ebx,(%rdi,%rcx,4)
	addb	%dl,%bl
	movzbl	%bl,%ebx
	movl	%edx,60(%rsi)
	leaq	(%rdi,%r10,4),%rsi
	pinsrw	$7,(%rdi,%rbx,4),%xmm1
	movl	(%rsi),%eax
	movq	%rcx,%rbx
	xorq	%rcx,%rcx
	subq	$16,%r11
	movb	%bl,%cl
	testq	$-16,%r11
	jnz	.Loop16

	psllq	$8,%xmm1
	pxor	%xmm0,%xmm2
	pxor	%xmm1,%xmm2
	movdqu	%xmm2,(%r12,%r13,1)
	leaq	16(%r12),%r12

	cmpq	$0,%r11
	jne	.Lloop1
	jmp	.Lexit

.align	16
.Lloop1:
	addb	%al,%cl
	movl	(%rdi,%rcx,4),%edx
	movl	%eax,(%rdi,%rcx,4)
	movl	%edx,(%rdi,%r10,4)
	addb	%dl,%al
	incb	%r10b
	movl	(%rdi,%rax,4),%edx
	movl	(%rdi,%r10,4),%eax
	xorb	(%r12),%dl
	movb	%dl,(%r12,%r13,1)
	leaq	1(%r12),%r12
	decq	%r11
	jnz	.Lloop1
	jmp	.Lexit

.align	16
.LRC4_CHAR:
	addb	$1,%r10b
	movzbl	(%rdi,%r10,1),%eax
	testq	$-8,%r11
	jz	.Lcloop1
	jmp	.Lcloop8
.align	16
.Lcloop8:
	movl	(%r12),%r8d
	movl	4(%r12),%r9d
	addb	%al,%cl
	leaq	1(%r10),%rsi
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%sil,%esi
	movzbl	(%rdi,%rsi,1),%ebx
	movb	%al,(%rdi,%rcx,1)
	cmpq	%rsi,%rcx
	movb	%dl,(%rdi,%r10,1)
	jne	.Lcmov0
	movq	%rax,%rbx
.Lcmov0:
	addb	%al,%dl
	xorb	(%rdi,%rdx,1),%r8b
	rorl	$8,%r8d
	addb	%bl,%cl
	leaq	1(%rsi),%r10
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%r10b,%r10d
	movzbl	(%rdi,%r10,1),%eax
	movb	%bl,(%rdi,%rcx,1)
	cmpq	%r10,%rcx
	movb	%dl,(%rdi,%rsi,1)
	jne	.Lcmov1
	movq	%rbx,%rax
.Lcmov1:
	addb	%bl,%dl
	xorb	(%rdi,%rdx,1),%r8b
	rorl	$8,%r8d
	addb	%al,%cl
	leaq	1(%r10),%rsi
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%sil,%esi
	movzbl	(%rdi,%rsi,1),%ebx
	movb	%al,(%rdi,%rcx,1)
	cmpq	%rsi,%rcx
	movb	%dl,(%rdi,%r10,1)
	jne	.Lcmov2
	movq	%rax,%rbx
.Lcmov2:
	addb	%al,%dl
	xorb	(%rdi,%rdx,1),%r8b
	rorl	$8,%r8d
	addb	%bl,%cl
	leaq	1(%rsi),%r10
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%r10b,%r10d
	movzbl	(%rdi,%r10,1),%eax
	movb	%bl,(%rdi,%rcx,1)
	cmpq	%r10,%rcx
	movb	%dl,(%rdi,%rsi,1)
	jne	.Lcmov3
	movq	%rbx,%rax
.Lcmov3:
	addb	%bl,%dl
	xorb	(%rdi,%rdx,1),%r8b
	rorl	$8,%r8d
	addb	%al,%cl
	leaq	1(%r10),%rsi
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%sil,%esi
	movzbl	(%rdi,%rsi,1),%ebx
	movb	%al,(%rdi,%rcx,1)
	cmpq	%rsi,%rcx
	movb	%dl,(%rdi,%r10,1)
	jne	.Lcmov4
	movq	%rax,%rbx
.Lcmov4:
	addb	%al,%dl
	xorb	(%rdi,%rdx,1),%r9b
	rorl	$8,%r9d
	addb	%bl,%cl
	leaq	1(%rsi),%r10
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%r10b,%r10d
	movzbl	(%rdi,%r10,1),%eax
	movb	%bl,(%rdi,%rcx,1)
	cmpq	%r10,%rcx
	movb	%dl,(%rdi,%rsi,1)
	jne	.Lcmov5
	movq	%rbx,%rax
.Lcmov5:
	addb	%bl,%dl
	xorb	(%rdi,%rdx,1),%r9b
	rorl	$8,%r9d
	addb	%al,%cl
	leaq	1(%r10),%rsi
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%sil,%esi
	movzbl	(%rdi,%rsi,1),%ebx
	movb	%al,(%rdi,%rcx,1)
	cmpq	%rsi,%rcx
	movb	%dl,(%rdi,%r10,1)
	jne	.Lcmov6
	movq	%rax,%rbx
.Lcmov6:
	addb	%al,%dl
	xorb	(%rdi,%rdx,1),%r9b
	rorl	$8,%r9d
	addb	%bl,%cl
	leaq	1(%rsi),%r10
	movzbl	(%rdi,%rcx,1),%edx
	movzbl	%r10b,%r10d
	movzbl	(%rdi,%r10,1),%eax
	movb	%bl,(%rdi,%rcx,1)
	cmpq	%r10,%rcx
	movb	%dl,(%rdi,%rsi,1)
	jne	.Lcmov7
	movq	%rbx,%rax
.Lcmov7:
	addb	%bl,%dl
	xorb	(%rdi,%rdx,1),%r9b
	rorl	$8,%r9d
	leaq	-8(%r11),%r11
	movl	%r8d,(%r13)
	leaq	8(%r12),%r12
	movl	%r9d,4(%r13)
	leaq	8(%r13),%r13

	testq	$-8,%r11
	jnz	.Lcloop8
	cmpq	$0,%r11
	jne	.Lcloop1
	jmp	.Lexit
.align	16
.Lcloop1:
	addb	%al,%cl
	movzbl	%cl,%ecx
	movzbl	(%rdi,%rcx,1),%edx
	movb	%al,(%rdi,%rcx,1)
	movb	%dl,(%rdi,%r10,1)
	addb	%al,%dl
	addb	$1,%r10b
	movzbl	%dl,%edx
	movzbl	%r10b,%r10d
	movzbl	(%rdi,%rdx,1),%edx
	movzbl	(%rdi,%r10,1),%eax
	xorb	(%r12),%dl
	leaq	1(%r12),%r12
	movb	%dl,(%r13)
	leaq	1(%r13),%r13
	subq	$1,%r11
	jnz	.Lcloop1
	jmp	.Lexit

.align	16
.Lexit:
	subb	$1,%r10b
	movl	%r10d,-8(%rdi)
	movl	%ecx,-4(%rdi)

	movq	(%rsp),%r13
.cfi_restore	%r13
	movq	8(%rsp),%r12
.cfi_restore	%r12
	movq	16(%rsp),%rbx
.cfi_restore	%rbx
	addq	$24,%rsp
.cfi_adjust_cfa_offset	-24
.Lepilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	RC4,.-RC4
.globl	RC4_set_key
.type	RC4_set_key,@function
.align	16
RC4_set_key:
	leaq	8(%rdi),%rdi
	leaq	(%rdx,%rsi,1),%rdx
	negq	%rsi
	movq	%rsi,%rcx
	xorl	%eax,%eax
	xorq	%r9,%r9
	xorq	%r10,%r10
	xorq	%r11,%r11

	movl	OPENSSL_ia32cap_P(%rip),%r8d
	btl	$20,%r8d
	jc	.Lc1stloop
	jmp	.Lw1stloop

.align	16
.Lw1stloop:
	movl	%eax,(%rdi,%rax,4)
	addb	$1,%al
	jnc	.Lw1stloop

	xorq	%r9,%r9
	xorq	%r8,%r8
.align	16
.Lw2ndloop:
	movl	(%rdi,%r9,4),%r10d
	addb	(%rdx,%rsi,1),%r8b
	addb	%r10b,%r8b
	addq	$1,%rsi
	movl	(%rdi,%r8,4),%r11d
	cmovzq	%rcx,%rsi
	movl	%r10d,(%rdi,%r8,4)
	movl	%r11d,(%rdi,%r9,4)
	addb	$1,%r9b
	jnc	.Lw2ndloop
	jmp	.Lexit_key

.align	16
.Lc1stloop:
	movb	%al,(%rdi,%rax,1)
	addb	$1,%al
	jnc	.Lc1stloop

	xorq	%r9,%r9
	xorq	%r8,%r8
.align	16
.Lc2ndloop:
	movb	(%rdi,%r9,1),%r10b
	addb	(%rdx,%rsi,1),%r8b
	addb	%r10b,%r8b
	addq	$1,%rsi
	movb	(%rdi,%r8,1),%r11b
	jnz	.Lcnowrap
	movq	%rcx,%rsi
.Lcnowrap:
	movb	%r10b,(%rdi,%r8,1)
	movb	%r11b,(%rdi,%r9,1)
	addb	$1,%r9b
	jnc	.Lc2ndloop
	movl	$-1,256(%rdi)

.align	16
.Lexit_key:
	xorl	%eax,%eax
	movl	%eax,-8(%rdi)
	movl	%eax,-4(%rdi)
	.byte	0xf3,0xc3
.size	RC4_set_key,.-RC4_set_key

.globl	RC4_options
.type	RC4_options,@function
.align	16
RC4_options:
	leaq	.Lopts(%rip),%rax
	movl	OPENSSL_ia32cap_P(%rip),%edx
	btl	$20,%edx
	jc	.L8xchar
	btl	$30,%edx
	jnc	.Ldone
	addq	$25,%rax
	.byte	0xf3,0xc3
.L8xchar:
	addq	$12,%rax
.Ldone:
	.byte	0xf3,0xc3
.align	64
.Lopts:
.byte	114,99,52,40,56,120,44,105,110,116,41,0
.byte	114,99,52,40,56,120,44,99,104,97,114,41,0
.byte	114,99,52,40,49,54,120,44,105,110,116,41,0
.byte	82,67,52,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	64
.size	RC4_options,.-RC4_options
