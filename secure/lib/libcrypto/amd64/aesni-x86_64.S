/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from aesni-x86_64.pl. */
.text	

.globl	aesni_encrypt
.type	aesni_encrypt,@function
.align	16
aesni_encrypt:
.cfi_startproc	
	movups	(%rdi),%xmm2
	movl	240(%rdx),%eax
	movups	(%rdx),%xmm0
	movups	16(%rdx),%xmm1
	leaq	32(%rdx),%rdx
	xorps	%xmm0,%xmm2
.Loop_enc1_1:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%rdx),%xmm1
	leaq	16(%rdx),%rdx
	jnz	.Loop_enc1_1
.byte	102,15,56,221,209
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_encrypt,.-aesni_encrypt

.globl	aesni_decrypt
.type	aesni_decrypt,@function
.align	16
aesni_decrypt:
.cfi_startproc	
	movups	(%rdi),%xmm2
	movl	240(%rdx),%eax
	movups	(%rdx),%xmm0
	movups	16(%rdx),%xmm1
	leaq	32(%rdx),%rdx
	xorps	%xmm0,%xmm2
.Loop_dec1_2:
.byte	102,15,56,222,209
	decl	%eax
	movups	(%rdx),%xmm1
	leaq	16(%rdx),%rdx
	jnz	.Loop_dec1_2
.byte	102,15,56,223,209
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_decrypt, .-aesni_decrypt
.type	_aesni_encrypt2,@function
.align	16
_aesni_encrypt2:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	movups	32(%rcx),%xmm0
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
	addq	$16,%rax

.Lenc_loop2:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lenc_loop2

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,221,208
.byte	102,15,56,221,216
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_encrypt2,.-_aesni_encrypt2
.type	_aesni_decrypt2,@function
.align	16
_aesni_decrypt2:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	movups	32(%rcx),%xmm0
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
	addq	$16,%rax

.Ldec_loop2:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,222,208
.byte	102,15,56,222,216
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Ldec_loop2

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,223,208
.byte	102,15,56,223,216
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_decrypt2,.-_aesni_decrypt2
.type	_aesni_encrypt3,@function
.align	16
_aesni_encrypt3:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	xorps	%xmm0,%xmm4
	movups	32(%rcx),%xmm0
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
	addq	$16,%rax

.Lenc_loop3:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lenc_loop3

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,221,208
.byte	102,15,56,221,216
.byte	102,15,56,221,224
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_encrypt3,.-_aesni_encrypt3
.type	_aesni_decrypt3,@function
.align	16
_aesni_decrypt3:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	xorps	%xmm0,%xmm4
	movups	32(%rcx),%xmm0
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
	addq	$16,%rax

.Ldec_loop3:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Ldec_loop3

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,223,208
.byte	102,15,56,223,216
.byte	102,15,56,223,224
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_decrypt3,.-_aesni_decrypt3
.type	_aesni_encrypt4,@function
.align	16
_aesni_encrypt4:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	xorps	%xmm0,%xmm4
	xorps	%xmm0,%xmm5
	movups	32(%rcx),%xmm0
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
.byte	0x0f,0x1f,0x00
	addq	$16,%rax

.Lenc_loop4:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lenc_loop4

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,221,208
.byte	102,15,56,221,216
.byte	102,15,56,221,224
.byte	102,15,56,221,232
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_encrypt4,.-_aesni_encrypt4
.type	_aesni_decrypt4,@function
.align	16
_aesni_decrypt4:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	xorps	%xmm0,%xmm4
	xorps	%xmm0,%xmm5
	movups	32(%rcx),%xmm0
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
.byte	0x0f,0x1f,0x00
	addq	$16,%rax

.Ldec_loop4:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Ldec_loop4

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,223,208
.byte	102,15,56,223,216
.byte	102,15,56,223,224
.byte	102,15,56,223,232
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_decrypt4,.-_aesni_decrypt4
.type	_aesni_encrypt6,@function
.align	16
_aesni_encrypt6:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	pxor	%xmm0,%xmm3
	pxor	%xmm0,%xmm4
.byte	102,15,56,220,209
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
.byte	102,15,56,220,217
	pxor	%xmm0,%xmm5
	pxor	%xmm0,%xmm6
.byte	102,15,56,220,225
	pxor	%xmm0,%xmm7
	movups	(%rcx,%rax,1),%xmm0
	addq	$16,%rax
	jmp	.Lenc_loop6_enter
.align	16
.Lenc_loop6:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.Lenc_loop6_enter:
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lenc_loop6

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,15,56,221,208
.byte	102,15,56,221,216
.byte	102,15,56,221,224
.byte	102,15,56,221,232
.byte	102,15,56,221,240
.byte	102,15,56,221,248
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_encrypt6,.-_aesni_encrypt6
.type	_aesni_decrypt6,@function
.align	16
_aesni_decrypt6:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	pxor	%xmm0,%xmm3
	pxor	%xmm0,%xmm4
.byte	102,15,56,222,209
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
.byte	102,15,56,222,217
	pxor	%xmm0,%xmm5
	pxor	%xmm0,%xmm6
.byte	102,15,56,222,225
	pxor	%xmm0,%xmm7
	movups	(%rcx,%rax,1),%xmm0
	addq	$16,%rax
	jmp	.Ldec_loop6_enter
.align	16
.Ldec_loop6:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.Ldec_loop6_enter:
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Ldec_loop6

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,15,56,223,208
.byte	102,15,56,223,216
.byte	102,15,56,223,224
.byte	102,15,56,223,232
.byte	102,15,56,223,240
.byte	102,15,56,223,248
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_decrypt6,.-_aesni_decrypt6
.type	_aesni_encrypt8,@function
.align	16
_aesni_encrypt8:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	pxor	%xmm0,%xmm4
	pxor	%xmm0,%xmm5
	pxor	%xmm0,%xmm6
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
.byte	102,15,56,220,209
	pxor	%xmm0,%xmm7
	pxor	%xmm0,%xmm8
.byte	102,15,56,220,217
	pxor	%xmm0,%xmm9
	movups	(%rcx,%rax,1),%xmm0
	addq	$16,%rax
	jmp	.Lenc_loop8_inner
.align	16
.Lenc_loop8:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.Lenc_loop8_inner:
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
.Lenc_loop8_enter:
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lenc_loop8

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
.byte	102,15,56,221,208
.byte	102,15,56,221,216
.byte	102,15,56,221,224
.byte	102,15,56,221,232
.byte	102,15,56,221,240
.byte	102,15,56,221,248
.byte	102,68,15,56,221,192
.byte	102,68,15,56,221,200
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_encrypt8,.-_aesni_encrypt8
.type	_aesni_decrypt8,@function
.align	16
_aesni_decrypt8:
.cfi_startproc	
	movups	(%rcx),%xmm0
	shll	$4,%eax
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm2
	xorps	%xmm0,%xmm3
	pxor	%xmm0,%xmm4
	pxor	%xmm0,%xmm5
	pxor	%xmm0,%xmm6
	leaq	32(%rcx,%rax,1),%rcx
	negq	%rax
.byte	102,15,56,222,209
	pxor	%xmm0,%xmm7
	pxor	%xmm0,%xmm8
.byte	102,15,56,222,217
	pxor	%xmm0,%xmm9
	movups	(%rcx,%rax,1),%xmm0
	addq	$16,%rax
	jmp	.Ldec_loop8_inner
.align	16
.Ldec_loop8:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.Ldec_loop8_inner:
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
.Ldec_loop8_enter:
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Ldec_loop8

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
.byte	102,15,56,223,208
.byte	102,15,56,223,216
.byte	102,15,56,223,224
.byte	102,15,56,223,232
.byte	102,15,56,223,240
.byte	102,15,56,223,248
.byte	102,68,15,56,223,192
.byte	102,68,15,56,223,200
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_aesni_decrypt8,.-_aesni_decrypt8
.globl	aesni_ecb_encrypt
.type	aesni_ecb_encrypt,@function
.align	16
aesni_ecb_encrypt:
.cfi_startproc	
	andq	$-16,%rdx
	jz	.Lecb_ret

	movl	240(%rcx),%eax
	movups	(%rcx),%xmm0
	movq	%rcx,%r11
	movl	%eax,%r10d
	testl	%r8d,%r8d
	jz	.Lecb_decrypt

	cmpq	$0x80,%rdx
	jb	.Lecb_enc_tail

	movdqu	(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqu	32(%rdi),%xmm4
	movdqu	48(%rdi),%xmm5
	movdqu	64(%rdi),%xmm6
	movdqu	80(%rdi),%xmm7
	movdqu	96(%rdi),%xmm8
	movdqu	112(%rdi),%xmm9
	leaq	128(%rdi),%rdi
	subq	$0x80,%rdx
	jmp	.Lecb_enc_loop8_enter
.align	16
.Lecb_enc_loop8:
	movups	%xmm2,(%rsi)
	movq	%r11,%rcx
	movdqu	(%rdi),%xmm2
	movl	%r10d,%eax
	movups	%xmm3,16(%rsi)
	movdqu	16(%rdi),%xmm3
	movups	%xmm4,32(%rsi)
	movdqu	32(%rdi),%xmm4
	movups	%xmm5,48(%rsi)
	movdqu	48(%rdi),%xmm5
	movups	%xmm6,64(%rsi)
	movdqu	64(%rdi),%xmm6
	movups	%xmm7,80(%rsi)
	movdqu	80(%rdi),%xmm7
	movups	%xmm8,96(%rsi)
	movdqu	96(%rdi),%xmm8
	movups	%xmm9,112(%rsi)
	leaq	128(%rsi),%rsi
	movdqu	112(%rdi),%xmm9
	leaq	128(%rdi),%rdi
.Lecb_enc_loop8_enter:

	call	_aesni_encrypt8

	subq	$0x80,%rdx
	jnc	.Lecb_enc_loop8

	movups	%xmm2,(%rsi)
	movq	%r11,%rcx
	movups	%xmm3,16(%rsi)
	movl	%r10d,%eax
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	movups	%xmm6,64(%rsi)
	movups	%xmm7,80(%rsi)
	movups	%xmm8,96(%rsi)
	movups	%xmm9,112(%rsi)
	leaq	128(%rsi),%rsi
	addq	$0x80,%rdx
	jz	.Lecb_ret

.Lecb_enc_tail:
	movups	(%rdi),%xmm2
	cmpq	$0x20,%rdx
	jb	.Lecb_enc_one
	movups	16(%rdi),%xmm3
	je	.Lecb_enc_two
	movups	32(%rdi),%xmm4
	cmpq	$0x40,%rdx
	jb	.Lecb_enc_three
	movups	48(%rdi),%xmm5
	je	.Lecb_enc_four
	movups	64(%rdi),%xmm6
	cmpq	$0x60,%rdx
	jb	.Lecb_enc_five
	movups	80(%rdi),%xmm7
	je	.Lecb_enc_six
	movdqu	96(%rdi),%xmm8
	xorps	%xmm9,%xmm9
	call	_aesni_encrypt8
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	movups	%xmm6,64(%rsi)
	movups	%xmm7,80(%rsi)
	movups	%xmm8,96(%rsi)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_one:
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_enc1_3:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_enc1_3
.byte	102,15,56,221,209
	movups	%xmm2,(%rsi)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_two:
	call	_aesni_encrypt2
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_three:
	call	_aesni_encrypt3
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_four:
	call	_aesni_encrypt4
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_five:
	xorps	%xmm7,%xmm7
	call	_aesni_encrypt6
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	movups	%xmm6,64(%rsi)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_six:
	call	_aesni_encrypt6
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	movups	%xmm6,64(%rsi)
	movups	%xmm7,80(%rsi)
	jmp	.Lecb_ret

.align	16
.Lecb_decrypt:
	cmpq	$0x80,%rdx
	jb	.Lecb_dec_tail

	movdqu	(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqu	32(%rdi),%xmm4
	movdqu	48(%rdi),%xmm5
	movdqu	64(%rdi),%xmm6
	movdqu	80(%rdi),%xmm7
	movdqu	96(%rdi),%xmm8
	movdqu	112(%rdi),%xmm9
	leaq	128(%rdi),%rdi
	subq	$0x80,%rdx
	jmp	.Lecb_dec_loop8_enter
.align	16
.Lecb_dec_loop8:
	movups	%xmm2,(%rsi)
	movq	%r11,%rcx
	movdqu	(%rdi),%xmm2
	movl	%r10d,%eax
	movups	%xmm3,16(%rsi)
	movdqu	16(%rdi),%xmm3
	movups	%xmm4,32(%rsi)
	movdqu	32(%rdi),%xmm4
	movups	%xmm5,48(%rsi)
	movdqu	48(%rdi),%xmm5
	movups	%xmm6,64(%rsi)
	movdqu	64(%rdi),%xmm6
	movups	%xmm7,80(%rsi)
	movdqu	80(%rdi),%xmm7
	movups	%xmm8,96(%rsi)
	movdqu	96(%rdi),%xmm8
	movups	%xmm9,112(%rsi)
	leaq	128(%rsi),%rsi
	movdqu	112(%rdi),%xmm9
	leaq	128(%rdi),%rdi
.Lecb_dec_loop8_enter:

	call	_aesni_decrypt8

	movups	(%r11),%xmm0
	subq	$0x80,%rdx
	jnc	.Lecb_dec_loop8

	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movq	%r11,%rcx
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movl	%r10d,%eax
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	movups	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm6
	movups	%xmm7,80(%rsi)
	pxor	%xmm7,%xmm7
	movups	%xmm8,96(%rsi)
	pxor	%xmm8,%xmm8
	movups	%xmm9,112(%rsi)
	pxor	%xmm9,%xmm9
	leaq	128(%rsi),%rsi
	addq	$0x80,%rdx
	jz	.Lecb_ret

.Lecb_dec_tail:
	movups	(%rdi),%xmm2
	cmpq	$0x20,%rdx
	jb	.Lecb_dec_one
	movups	16(%rdi),%xmm3
	je	.Lecb_dec_two
	movups	32(%rdi),%xmm4
	cmpq	$0x40,%rdx
	jb	.Lecb_dec_three
	movups	48(%rdi),%xmm5
	je	.Lecb_dec_four
	movups	64(%rdi),%xmm6
	cmpq	$0x60,%rdx
	jb	.Lecb_dec_five
	movups	80(%rdi),%xmm7
	je	.Lecb_dec_six
	movups	96(%rdi),%xmm8
	movups	(%rcx),%xmm0
	xorps	%xmm9,%xmm9
	call	_aesni_decrypt8
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	movups	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm6
	movups	%xmm7,80(%rsi)
	pxor	%xmm7,%xmm7
	movups	%xmm8,96(%rsi)
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
	jmp	.Lecb_ret
.align	16
.Lecb_dec_one:
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_dec1_4:
.byte	102,15,56,222,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_dec1_4
.byte	102,15,56,223,209
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	jmp	.Lecb_ret
.align	16
.Lecb_dec_two:
	call	_aesni_decrypt2
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	jmp	.Lecb_ret
.align	16
.Lecb_dec_three:
	call	_aesni_decrypt3
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	jmp	.Lecb_ret
.align	16
.Lecb_dec_four:
	call	_aesni_decrypt4
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	jmp	.Lecb_ret
.align	16
.Lecb_dec_five:
	xorps	%xmm7,%xmm7
	call	_aesni_decrypt6
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	movups	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	jmp	.Lecb_ret
.align	16
.Lecb_dec_six:
	call	_aesni_decrypt6
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	movups	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm6
	movups	%xmm7,80(%rsi)
	pxor	%xmm7,%xmm7

.Lecb_ret:
	xorps	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_ecb_encrypt,.-aesni_ecb_encrypt
.globl	aesni_ccm64_encrypt_blocks
.type	aesni_ccm64_encrypt_blocks,@function
.align	16
aesni_ccm64_encrypt_blocks:
	movl	240(%rcx),%eax
	movdqu	(%r8),%xmm6
	movdqa	.Lincrement64(%rip),%xmm9
	movdqa	.Lbswap_mask(%rip),%xmm7

	shll	$4,%eax
	movl	$16,%r10d
	leaq	0(%rcx),%r11
	movdqu	(%r9),%xmm3
	movdqa	%xmm6,%xmm2
	leaq	32(%rcx,%rax,1),%rcx
.byte	102,15,56,0,247
	subq	%rax,%r10
	jmp	.Lccm64_enc_outer
.align	16
.Lccm64_enc_outer:
	movups	(%r11),%xmm0
	movq	%r10,%rax
	movups	(%rdi),%xmm8

	xorps	%xmm0,%xmm2
	movups	16(%r11),%xmm1
	xorps	%xmm8,%xmm0
	xorps	%xmm0,%xmm3
	movups	32(%r11),%xmm0

.Lccm64_enc2_loop:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lccm64_enc2_loop
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	paddq	%xmm9,%xmm6
	decq	%rdx
.byte	102,15,56,221,208
.byte	102,15,56,221,216

	leaq	16(%rdi),%rdi
	xorps	%xmm2,%xmm8
	movdqa	%xmm6,%xmm2
	movups	%xmm8,(%rsi)
.byte	102,15,56,0,215
	leaq	16(%rsi),%rsi
	jnz	.Lccm64_enc_outer

	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	movups	%xmm3,(%r9)
	pxor	%xmm3,%xmm3
	pxor	%xmm8,%xmm8
	pxor	%xmm6,%xmm6
	.byte	0xf3,0xc3
.size	aesni_ccm64_encrypt_blocks,.-aesni_ccm64_encrypt_blocks
.globl	aesni_ccm64_decrypt_blocks
.type	aesni_ccm64_decrypt_blocks,@function
.align	16
aesni_ccm64_decrypt_blocks:
	movl	240(%rcx),%eax
	movups	(%r8),%xmm6
	movdqu	(%r9),%xmm3
	movdqa	.Lincrement64(%rip),%xmm9
	movdqa	.Lbswap_mask(%rip),%xmm7

	movaps	%xmm6,%xmm2
	movl	%eax,%r10d
	movq	%rcx,%r11
.byte	102,15,56,0,247
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_enc1_5:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_enc1_5
.byte	102,15,56,221,209
	shll	$4,%r10d
	movl	$16,%eax
	movups	(%rdi),%xmm8
	paddq	%xmm9,%xmm6
	leaq	16(%rdi),%rdi
	subq	%r10,%rax
	leaq	32(%r11,%r10,1),%rcx
	movq	%rax,%r10
	jmp	.Lccm64_dec_outer
.align	16
.Lccm64_dec_outer:
	xorps	%xmm2,%xmm8
	movdqa	%xmm6,%xmm2
	movups	%xmm8,(%rsi)
	leaq	16(%rsi),%rsi
.byte	102,15,56,0,215

	subq	$1,%rdx
	jz	.Lccm64_dec_break

	movups	(%r11),%xmm0
	movq	%r10,%rax
	movups	16(%r11),%xmm1
	xorps	%xmm0,%xmm8
	xorps	%xmm0,%xmm2
	xorps	%xmm8,%xmm3
	movups	32(%r11),%xmm0
	jmp	.Lccm64_dec2_loop
.align	16
.Lccm64_dec2_loop:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax
.byte	102,15,56,220,208
.byte	102,15,56,220,216
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Lccm64_dec2_loop
	movups	(%rdi),%xmm8
	paddq	%xmm9,%xmm6
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,221,208
.byte	102,15,56,221,216
	leaq	16(%rdi),%rdi
	jmp	.Lccm64_dec_outer

.align	16
.Lccm64_dec_break:

	movl	240(%r11),%eax
	movups	(%r11),%xmm0
	movups	16(%r11),%xmm1
	xorps	%xmm0,%xmm8
	leaq	32(%r11),%r11
	xorps	%xmm8,%xmm3
.Loop_enc1_6:
.byte	102,15,56,220,217
	decl	%eax
	movups	(%r11),%xmm1
	leaq	16(%r11),%r11
	jnz	.Loop_enc1_6
.byte	102,15,56,221,217
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	movups	%xmm3,(%r9)
	pxor	%xmm3,%xmm3
	pxor	%xmm8,%xmm8
	pxor	%xmm6,%xmm6
	.byte	0xf3,0xc3
.size	aesni_ccm64_decrypt_blocks,.-aesni_ccm64_decrypt_blocks
.globl	aesni_ctr32_encrypt_blocks
.type	aesni_ctr32_encrypt_blocks,@function
.align	16
aesni_ctr32_encrypt_blocks:
.cfi_startproc	
	cmpq	$1,%rdx
	jne	.Lctr32_bulk



	movups	(%r8),%xmm2
	movups	(%rdi),%xmm3
	movl	240(%rcx),%edx
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_enc1_7:
.byte	102,15,56,220,209
	decl	%edx
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_enc1_7
.byte	102,15,56,221,209
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	xorps	%xmm3,%xmm2
	pxor	%xmm3,%xmm3
	movups	%xmm2,(%rsi)
	xorps	%xmm2,%xmm2
	jmp	.Lctr32_epilogue

.align	16
.Lctr32_bulk:
	leaq	(%rsp),%r11
.cfi_def_cfa_register	%r11
	pushq	%rbp
.cfi_offset	%rbp,-16
	subq	$128,%rsp
	andq	$-16,%rsp




	movdqu	(%r8),%xmm2
	movdqu	(%rcx),%xmm0
	movl	12(%r8),%r8d
	pxor	%xmm0,%xmm2
	movl	12(%rcx),%ebp
	movdqa	%xmm2,0(%rsp)
	bswapl	%r8d
	movdqa	%xmm2,%xmm3
	movdqa	%xmm2,%xmm4
	movdqa	%xmm2,%xmm5
	movdqa	%xmm2,64(%rsp)
	movdqa	%xmm2,80(%rsp)
	movdqa	%xmm2,96(%rsp)
	movq	%rdx,%r10
	movdqa	%xmm2,112(%rsp)

	leaq	1(%r8),%rax
	leaq	2(%r8),%rdx
	bswapl	%eax
	bswapl	%edx
	xorl	%ebp,%eax
	xorl	%ebp,%edx
.byte	102,15,58,34,216,3
	leaq	3(%r8),%rax
	movdqa	%xmm3,16(%rsp)
.byte	102,15,58,34,226,3
	bswapl	%eax
	movq	%r10,%rdx
	leaq	4(%r8),%r10
	movdqa	%xmm4,32(%rsp)
	xorl	%ebp,%eax
	bswapl	%r10d
.byte	102,15,58,34,232,3
	xorl	%ebp,%r10d
	movdqa	%xmm5,48(%rsp)
	leaq	5(%r8),%r9
	movl	%r10d,64+12(%rsp)
	bswapl	%r9d
	leaq	6(%r8),%r10
	movl	240(%rcx),%eax
	xorl	%ebp,%r9d
	bswapl	%r10d
	movl	%r9d,80+12(%rsp)
	xorl	%ebp,%r10d
	leaq	7(%r8),%r9
	movl	%r10d,96+12(%rsp)
	bswapl	%r9d
	movl	OPENSSL_ia32cap_P+4(%rip),%r10d
	xorl	%ebp,%r9d
	andl	$71303168,%r10d
	movl	%r9d,112+12(%rsp)

	movups	16(%rcx),%xmm1

	movdqa	64(%rsp),%xmm6
	movdqa	80(%rsp),%xmm7

	cmpq	$8,%rdx
	jb	.Lctr32_tail

	subq	$6,%rdx
	cmpl	$4194304,%r10d
	je	.Lctr32_6x

	leaq	128(%rcx),%rcx
	subq	$2,%rdx
	jmp	.Lctr32_loop8

.align	16
.Lctr32_6x:
	shll	$4,%eax
	movl	$48,%r10d
	bswapl	%ebp
	leaq	32(%rcx,%rax,1),%rcx
	subq	%rax,%r10
	jmp	.Lctr32_loop6

.align	16
.Lctr32_loop6:
	addl	$6,%r8d
	movups	-48(%rcx,%r10,1),%xmm0
.byte	102,15,56,220,209
	movl	%r8d,%eax
	xorl	%ebp,%eax
.byte	102,15,56,220,217
.byte	0x0f,0x38,0xf1,0x44,0x24,12
	leal	1(%r8),%eax
.byte	102,15,56,220,225
	xorl	%ebp,%eax
.byte	0x0f,0x38,0xf1,0x44,0x24,28
.byte	102,15,56,220,233
	leal	2(%r8),%eax
	xorl	%ebp,%eax
.byte	102,15,56,220,241
.byte	0x0f,0x38,0xf1,0x44,0x24,44
	leal	3(%r8),%eax
.byte	102,15,56,220,249
	movups	-32(%rcx,%r10,1),%xmm1
	xorl	%ebp,%eax

.byte	102,15,56,220,208
.byte	0x0f,0x38,0xf1,0x44,0x24,60
	leal	4(%r8),%eax
.byte	102,15,56,220,216
	xorl	%ebp,%eax
.byte	0x0f,0x38,0xf1,0x44,0x24,76
.byte	102,15,56,220,224
	leal	5(%r8),%eax
	xorl	%ebp,%eax
.byte	102,15,56,220,232
.byte	0x0f,0x38,0xf1,0x44,0x24,92
	movq	%r10,%rax
.byte	102,15,56,220,240
.byte	102,15,56,220,248
	movups	-16(%rcx,%r10,1),%xmm0

	call	.Lenc_loop6

	movdqu	(%rdi),%xmm8
	movdqu	16(%rdi),%xmm9
	movdqu	32(%rdi),%xmm10
	movdqu	48(%rdi),%xmm11
	movdqu	64(%rdi),%xmm12
	movdqu	80(%rdi),%xmm13
	leaq	96(%rdi),%rdi
	movups	-64(%rcx,%r10,1),%xmm1
	pxor	%xmm2,%xmm8
	movaps	0(%rsp),%xmm2
	pxor	%xmm3,%xmm9
	movaps	16(%rsp),%xmm3
	pxor	%xmm4,%xmm10
	movaps	32(%rsp),%xmm4
	pxor	%xmm5,%xmm11
	movaps	48(%rsp),%xmm5
	pxor	%xmm6,%xmm12
	movaps	64(%rsp),%xmm6
	pxor	%xmm7,%xmm13
	movaps	80(%rsp),%xmm7
	movdqu	%xmm8,(%rsi)
	movdqu	%xmm9,16(%rsi)
	movdqu	%xmm10,32(%rsi)
	movdqu	%xmm11,48(%rsi)
	movdqu	%xmm12,64(%rsi)
	movdqu	%xmm13,80(%rsi)
	leaq	96(%rsi),%rsi

	subq	$6,%rdx
	jnc	.Lctr32_loop6

	addq	$6,%rdx
	jz	.Lctr32_done

	leal	-48(%r10),%eax
	leaq	-80(%rcx,%r10,1),%rcx
	negl	%eax
	shrl	$4,%eax
	jmp	.Lctr32_tail

.align	32
.Lctr32_loop8:
	addl	$8,%r8d
	movdqa	96(%rsp),%xmm8
.byte	102,15,56,220,209
	movl	%r8d,%r9d
	movdqa	112(%rsp),%xmm9
.byte	102,15,56,220,217
	bswapl	%r9d
	movups	32-128(%rcx),%xmm0
.byte	102,15,56,220,225
	xorl	%ebp,%r9d
	nop
.byte	102,15,56,220,233
	movl	%r9d,0+12(%rsp)
	leaq	1(%r8),%r9
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movups	48-128(%rcx),%xmm1
	bswapl	%r9d
.byte	102,15,56,220,208
.byte	102,15,56,220,216
	xorl	%ebp,%r9d
.byte	0x66,0x90
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	movl	%r9d,16+12(%rsp)
	leaq	2(%r8),%r9
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	64-128(%rcx),%xmm0
	bswapl	%r9d
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	xorl	%ebp,%r9d
.byte	0x66,0x90
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movl	%r9d,32+12(%rsp)
	leaq	3(%r8),%r9
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movups	80-128(%rcx),%xmm1
	bswapl	%r9d
.byte	102,15,56,220,208
.byte	102,15,56,220,216
	xorl	%ebp,%r9d
.byte	0x66,0x90
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	movl	%r9d,48+12(%rsp)
	leaq	4(%r8),%r9
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	96-128(%rcx),%xmm0
	bswapl	%r9d
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	xorl	%ebp,%r9d
.byte	0x66,0x90
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movl	%r9d,64+12(%rsp)
	leaq	5(%r8),%r9
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movups	112-128(%rcx),%xmm1
	bswapl	%r9d
.byte	102,15,56,220,208
.byte	102,15,56,220,216
	xorl	%ebp,%r9d
.byte	0x66,0x90
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	movl	%r9d,80+12(%rsp)
	leaq	6(%r8),%r9
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	128-128(%rcx),%xmm0
	bswapl	%r9d
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	xorl	%ebp,%r9d
.byte	0x66,0x90
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movl	%r9d,96+12(%rsp)
	leaq	7(%r8),%r9
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movups	144-128(%rcx),%xmm1
	bswapl	%r9d
.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
	xorl	%ebp,%r9d
	movdqu	0(%rdi),%xmm10
.byte	102,15,56,220,232
	movl	%r9d,112+12(%rsp)
	cmpl	$11,%eax
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	160-128(%rcx),%xmm0

	jb	.Lctr32_enc_done

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movups	176-128(%rcx),%xmm1

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	192-128(%rcx),%xmm0
	je	.Lctr32_enc_done

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movups	208-128(%rcx),%xmm1

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
.byte	102,68,15,56,220,192
.byte	102,68,15,56,220,200
	movups	224-128(%rcx),%xmm0
	jmp	.Lctr32_enc_done

.align	16
.Lctr32_enc_done:
	movdqu	16(%rdi),%xmm11
	pxor	%xmm0,%xmm10
	movdqu	32(%rdi),%xmm12
	pxor	%xmm0,%xmm11
	movdqu	48(%rdi),%xmm13
	pxor	%xmm0,%xmm12
	movdqu	64(%rdi),%xmm14
	pxor	%xmm0,%xmm13
	movdqu	80(%rdi),%xmm15
	pxor	%xmm0,%xmm14
	pxor	%xmm0,%xmm15
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193
.byte	102,68,15,56,220,201
	movdqu	96(%rdi),%xmm1
	leaq	128(%rdi),%rdi

.byte	102,65,15,56,221,210
	pxor	%xmm0,%xmm1
	movdqu	112-128(%rdi),%xmm10
.byte	102,65,15,56,221,219
	pxor	%xmm0,%xmm10
	movdqa	0(%rsp),%xmm11
.byte	102,65,15,56,221,228
.byte	102,65,15,56,221,237
	movdqa	16(%rsp),%xmm12
	movdqa	32(%rsp),%xmm13
.byte	102,65,15,56,221,246
.byte	102,65,15,56,221,255
	movdqa	48(%rsp),%xmm14
	movdqa	64(%rsp),%xmm15
.byte	102,68,15,56,221,193
	movdqa	80(%rsp),%xmm0
	movups	16-128(%rcx),%xmm1
.byte	102,69,15,56,221,202

	movups	%xmm2,(%rsi)
	movdqa	%xmm11,%xmm2
	movups	%xmm3,16(%rsi)
	movdqa	%xmm12,%xmm3
	movups	%xmm4,32(%rsi)
	movdqa	%xmm13,%xmm4
	movups	%xmm5,48(%rsi)
	movdqa	%xmm14,%xmm5
	movups	%xmm6,64(%rsi)
	movdqa	%xmm15,%xmm6
	movups	%xmm7,80(%rsi)
	movdqa	%xmm0,%xmm7
	movups	%xmm8,96(%rsi)
	movups	%xmm9,112(%rsi)
	leaq	128(%rsi),%rsi

	subq	$8,%rdx
	jnc	.Lctr32_loop8

	addq	$8,%rdx
	jz	.Lctr32_done
	leaq	-128(%rcx),%rcx

.Lctr32_tail:


	leaq	16(%rcx),%rcx
	cmpq	$4,%rdx
	jb	.Lctr32_loop3
	je	.Lctr32_loop4


	shll	$4,%eax
	movdqa	96(%rsp),%xmm8
	pxor	%xmm9,%xmm9

	movups	16(%rcx),%xmm0
.byte	102,15,56,220,209
.byte	102,15,56,220,217
	leaq	32-16(%rcx,%rax,1),%rcx
	negq	%rax
.byte	102,15,56,220,225
	addq	$16,%rax
	movups	(%rdi),%xmm10
.byte	102,15,56,220,233
.byte	102,15,56,220,241
	movups	16(%rdi),%xmm11
	movups	32(%rdi),%xmm12
.byte	102,15,56,220,249
.byte	102,68,15,56,220,193

	call	.Lenc_loop8_enter

	movdqu	48(%rdi),%xmm13
	pxor	%xmm10,%xmm2
	movdqu	64(%rdi),%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm13,%xmm5
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm10,%xmm6
	movdqu	%xmm5,48(%rsi)
	movdqu	%xmm6,64(%rsi)
	cmpq	$6,%rdx
	jb	.Lctr32_done

	movups	80(%rdi),%xmm11
	xorps	%xmm11,%xmm7
	movups	%xmm7,80(%rsi)
	je	.Lctr32_done

	movups	96(%rdi),%xmm12
	xorps	%xmm12,%xmm8
	movups	%xmm8,96(%rsi)
	jmp	.Lctr32_done

.align	32
.Lctr32_loop4:
.byte	102,15,56,220,209
	leaq	16(%rcx),%rcx
	decl	%eax
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movups	(%rcx),%xmm1
	jnz	.Lctr32_loop4
.byte	102,15,56,221,209
.byte	102,15,56,221,217
	movups	(%rdi),%xmm10
	movups	16(%rdi),%xmm11
.byte	102,15,56,221,225
.byte	102,15,56,221,233
	movups	32(%rdi),%xmm12
	movups	48(%rdi),%xmm13

	xorps	%xmm10,%xmm2
	movups	%xmm2,(%rsi)
	xorps	%xmm11,%xmm3
	movups	%xmm3,16(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm13,%xmm5
	movdqu	%xmm5,48(%rsi)
	jmp	.Lctr32_done

.align	32
.Lctr32_loop3:
.byte	102,15,56,220,209
	leaq	16(%rcx),%rcx
	decl	%eax
.byte	102,15,56,220,217
.byte	102,15,56,220,225
	movups	(%rcx),%xmm1
	jnz	.Lctr32_loop3
.byte	102,15,56,221,209
.byte	102,15,56,221,217
.byte	102,15,56,221,225

	movups	(%rdi),%xmm10
	xorps	%xmm10,%xmm2
	movups	%xmm2,(%rsi)
	cmpq	$2,%rdx
	jb	.Lctr32_done

	movups	16(%rdi),%xmm11
	xorps	%xmm11,%xmm3
	movups	%xmm3,16(%rsi)
	je	.Lctr32_done

	movups	32(%rdi),%xmm12
	xorps	%xmm12,%xmm4
	movups	%xmm4,32(%rsi)

.Lctr32_done:
	xorps	%xmm0,%xmm0
	xorl	%ebp,%ebp
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	movaps	%xmm0,0(%rsp)
	pxor	%xmm8,%xmm8
	movaps	%xmm0,16(%rsp)
	pxor	%xmm9,%xmm9
	movaps	%xmm0,32(%rsp)
	pxor	%xmm10,%xmm10
	movaps	%xmm0,48(%rsp)
	pxor	%xmm11,%xmm11
	movaps	%xmm0,64(%rsp)
	pxor	%xmm12,%xmm12
	movaps	%xmm0,80(%rsp)
	pxor	%xmm13,%xmm13
	movaps	%xmm0,96(%rsp)
	pxor	%xmm14,%xmm14
	movaps	%xmm0,112(%rsp)
	pxor	%xmm15,%xmm15
	movq	-8(%r11),%rbp
.cfi_restore	%rbp
	leaq	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lctr32_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_ctr32_encrypt_blocks,.-aesni_ctr32_encrypt_blocks
.globl	aesni_xts_encrypt
.type	aesni_xts_encrypt,@function
.align	16
aesni_xts_encrypt:
.cfi_startproc	
	leaq	(%rsp),%r11
.cfi_def_cfa_register	%r11
	pushq	%rbp
.cfi_offset	%rbp,-16
	subq	$112,%rsp
	andq	$-16,%rsp
	movups	(%r9),%xmm2
	movl	240(%r8),%eax
	movl	240(%rcx),%r10d
	movups	(%r8),%xmm0
	movups	16(%r8),%xmm1
	leaq	32(%r8),%r8
	xorps	%xmm0,%xmm2
.Loop_enc1_8:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%r8),%xmm1
	leaq	16(%r8),%r8
	jnz	.Loop_enc1_8
.byte	102,15,56,221,209
	movups	(%rcx),%xmm0
	movq	%rcx,%rbp
	movl	%r10d,%eax
	shll	$4,%r10d
	movq	%rdx,%r9
	andq	$-16,%rdx

	movups	16(%rcx,%r10,1),%xmm1

	movdqa	.Lxts_magic(%rip),%xmm8
	movdqa	%xmm2,%xmm15
	pshufd	$0x5f,%xmm2,%xmm9
	pxor	%xmm0,%xmm1
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm10
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm10
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm11
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm11
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm12
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm12
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm13
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm13
	pxor	%xmm14,%xmm15
	movdqa	%xmm15,%xmm14
	psrad	$31,%xmm9
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm9
	pxor	%xmm0,%xmm14
	pxor	%xmm9,%xmm15
	movaps	%xmm1,96(%rsp)

	subq	$96,%rdx
	jc	.Lxts_enc_short

	movl	$16+96,%eax
	leaq	32(%rbp,%r10,1),%rcx
	subq	%r10,%rax
	movups	16(%rbp),%xmm1
	movq	%rax,%r10
	leaq	.Lxts_magic(%rip),%r8
	jmp	.Lxts_enc_grandloop

.align	32
.Lxts_enc_grandloop:
	movdqu	0(%rdi),%xmm2
	movdqa	%xmm0,%xmm8
	movdqu	16(%rdi),%xmm3
	pxor	%xmm10,%xmm2
	movdqu	32(%rdi),%xmm4
	pxor	%xmm11,%xmm3
.byte	102,15,56,220,209
	movdqu	48(%rdi),%xmm5
	pxor	%xmm12,%xmm4
.byte	102,15,56,220,217
	movdqu	64(%rdi),%xmm6
	pxor	%xmm13,%xmm5
.byte	102,15,56,220,225
	movdqu	80(%rdi),%xmm7
	pxor	%xmm15,%xmm8
	movdqa	96(%rsp),%xmm9
	pxor	%xmm14,%xmm6
.byte	102,15,56,220,233
	movups	32(%rbp),%xmm0
	leaq	96(%rdi),%rdi
	pxor	%xmm8,%xmm7

	pxor	%xmm9,%xmm10
.byte	102,15,56,220,241
	pxor	%xmm9,%xmm11
	movdqa	%xmm10,0(%rsp)
.byte	102,15,56,220,249
	movups	48(%rbp),%xmm1
	pxor	%xmm9,%xmm12

.byte	102,15,56,220,208
	pxor	%xmm9,%xmm13
	movdqa	%xmm11,16(%rsp)
.byte	102,15,56,220,216
	pxor	%xmm9,%xmm14
	movdqa	%xmm12,32(%rsp)
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	pxor	%xmm9,%xmm8
	movdqa	%xmm14,64(%rsp)
.byte	102,15,56,220,240
.byte	102,15,56,220,248
	movups	64(%rbp),%xmm0
	movdqa	%xmm8,80(%rsp)
	pshufd	$0x5f,%xmm15,%xmm9
	jmp	.Lxts_enc_loop6
.align	32
.Lxts_enc_loop6:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
	movups	-64(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
	movups	-80(%rcx,%rax,1),%xmm0
	jnz	.Lxts_enc_loop6

	movdqa	(%r8),%xmm8
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
.byte	102,15,56,220,209
	paddq	%xmm15,%xmm15
	psrad	$31,%xmm14
.byte	102,15,56,220,217
	pand	%xmm8,%xmm14
	movups	(%rbp),%xmm10
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
	pxor	%xmm14,%xmm15
	movaps	%xmm10,%xmm11
.byte	102,15,56,220,249
	movups	-64(%rcx),%xmm1

	movdqa	%xmm9,%xmm14
.byte	102,15,56,220,208
	paddd	%xmm9,%xmm9
	pxor	%xmm15,%xmm10
.byte	102,15,56,220,216
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	pand	%xmm8,%xmm14
	movaps	%xmm11,%xmm12
.byte	102,15,56,220,240
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
.byte	102,15,56,220,248
	movups	-48(%rcx),%xmm0

	paddd	%xmm9,%xmm9
.byte	102,15,56,220,209
	pxor	%xmm15,%xmm11
	psrad	$31,%xmm14
.byte	102,15,56,220,217
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movdqa	%xmm13,48(%rsp)
	pxor	%xmm14,%xmm15
.byte	102,15,56,220,241
	movaps	%xmm12,%xmm13
	movdqa	%xmm9,%xmm14
.byte	102,15,56,220,249
	movups	-32(%rcx),%xmm1

	paddd	%xmm9,%xmm9
.byte	102,15,56,220,208
	pxor	%xmm15,%xmm12
	psrad	$31,%xmm14
.byte	102,15,56,220,216
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
	pxor	%xmm14,%xmm15
	movaps	%xmm13,%xmm14
.byte	102,15,56,220,248

	movdqa	%xmm9,%xmm0
	paddd	%xmm9,%xmm9
.byte	102,15,56,220,209
	pxor	%xmm15,%xmm13
	psrad	$31,%xmm0
.byte	102,15,56,220,217
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm0
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	pxor	%xmm0,%xmm15
	movups	(%rbp),%xmm0
.byte	102,15,56,220,241
.byte	102,15,56,220,249
	movups	16(%rbp),%xmm1

	pxor	%xmm15,%xmm14
.byte	102,15,56,221,84,36,0
	psrad	$31,%xmm9
	paddq	%xmm15,%xmm15
.byte	102,15,56,221,92,36,16
.byte	102,15,56,221,100,36,32
	pand	%xmm8,%xmm9
	movq	%r10,%rax
.byte	102,15,56,221,108,36,48
.byte	102,15,56,221,116,36,64
.byte	102,15,56,221,124,36,80
	pxor	%xmm9,%xmm15

	leaq	96(%rsi),%rsi
	movups	%xmm2,-96(%rsi)
	movups	%xmm3,-80(%rsi)
	movups	%xmm4,-64(%rsi)
	movups	%xmm5,-48(%rsi)
	movups	%xmm6,-32(%rsi)
	movups	%xmm7,-16(%rsi)
	subq	$96,%rdx
	jnc	.Lxts_enc_grandloop

	movl	$16+96,%eax
	subl	%r10d,%eax
	movq	%rbp,%rcx
	shrl	$4,%eax

.Lxts_enc_short:

	movl	%eax,%r10d
	pxor	%xmm0,%xmm10
	addq	$96,%rdx
	jz	.Lxts_enc_done

	pxor	%xmm0,%xmm11
	cmpq	$0x20,%rdx
	jb	.Lxts_enc_one
	pxor	%xmm0,%xmm12
	je	.Lxts_enc_two

	pxor	%xmm0,%xmm13
	cmpq	$0x40,%rdx
	jb	.Lxts_enc_three
	pxor	%xmm0,%xmm14
	je	.Lxts_enc_four

	movdqu	(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqu	32(%rdi),%xmm4
	pxor	%xmm10,%xmm2
	movdqu	48(%rdi),%xmm5
	pxor	%xmm11,%xmm3
	movdqu	64(%rdi),%xmm6
	leaq	80(%rdi),%rdi
	pxor	%xmm12,%xmm4
	pxor	%xmm13,%xmm5
	pxor	%xmm14,%xmm6
	pxor	%xmm7,%xmm7

	call	_aesni_encrypt6

	xorps	%xmm10,%xmm2
	movdqa	%xmm15,%xmm10
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4
	movdqu	%xmm2,(%rsi)
	xorps	%xmm13,%xmm5
	movdqu	%xmm3,16(%rsi)
	xorps	%xmm14,%xmm6
	movdqu	%xmm4,32(%rsi)
	movdqu	%xmm5,48(%rsi)
	movdqu	%xmm6,64(%rsi)
	leaq	80(%rsi),%rsi
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_one:
	movups	(%rdi),%xmm2
	leaq	16(%rdi),%rdi
	xorps	%xmm10,%xmm2
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_enc1_9:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_enc1_9
.byte	102,15,56,221,209
	xorps	%xmm10,%xmm2
	movdqa	%xmm11,%xmm10
	movups	%xmm2,(%rsi)
	leaq	16(%rsi),%rsi
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_two:
	movups	(%rdi),%xmm2
	movups	16(%rdi),%xmm3
	leaq	32(%rdi),%rdi
	xorps	%xmm10,%xmm2
	xorps	%xmm11,%xmm3

	call	_aesni_encrypt2

	xorps	%xmm10,%xmm2
	movdqa	%xmm12,%xmm10
	xorps	%xmm11,%xmm3
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	leaq	32(%rsi),%rsi
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_three:
	movups	(%rdi),%xmm2
	movups	16(%rdi),%xmm3
	movups	32(%rdi),%xmm4
	leaq	48(%rdi),%rdi
	xorps	%xmm10,%xmm2
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4

	call	_aesni_encrypt3

	xorps	%xmm10,%xmm2
	movdqa	%xmm13,%xmm10
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	leaq	48(%rsi),%rsi
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_four:
	movups	(%rdi),%xmm2
	movups	16(%rdi),%xmm3
	movups	32(%rdi),%xmm4
	xorps	%xmm10,%xmm2
	movups	48(%rdi),%xmm5
	leaq	64(%rdi),%rdi
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4
	xorps	%xmm13,%xmm5

	call	_aesni_encrypt4

	pxor	%xmm10,%xmm2
	movdqa	%xmm14,%xmm10
	pxor	%xmm11,%xmm3
	pxor	%xmm12,%xmm4
	movdqu	%xmm2,(%rsi)
	pxor	%xmm13,%xmm5
	movdqu	%xmm3,16(%rsi)
	movdqu	%xmm4,32(%rsi)
	movdqu	%xmm5,48(%rsi)
	leaq	64(%rsi),%rsi
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_done:
	andq	$15,%r9
	jz	.Lxts_enc_ret
	movq	%r9,%rdx

.Lxts_enc_steal:
	movzbl	(%rdi),%eax
	movzbl	-16(%rsi),%ecx
	leaq	1(%rdi),%rdi
	movb	%al,-16(%rsi)
	movb	%cl,0(%rsi)
	leaq	1(%rsi),%rsi
	subq	$1,%rdx
	jnz	.Lxts_enc_steal

	subq	%r9,%rsi
	movq	%rbp,%rcx
	movl	%r10d,%eax

	movups	-16(%rsi),%xmm2
	xorps	%xmm10,%xmm2
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_enc1_10:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_enc1_10
.byte	102,15,56,221,209
	xorps	%xmm10,%xmm2
	movups	%xmm2,-16(%rsi)

.Lxts_enc_ret:
	xorps	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	movaps	%xmm0,0(%rsp)
	pxor	%xmm8,%xmm8
	movaps	%xmm0,16(%rsp)
	pxor	%xmm9,%xmm9
	movaps	%xmm0,32(%rsp)
	pxor	%xmm10,%xmm10
	movaps	%xmm0,48(%rsp)
	pxor	%xmm11,%xmm11
	movaps	%xmm0,64(%rsp)
	pxor	%xmm12,%xmm12
	movaps	%xmm0,80(%rsp)
	pxor	%xmm13,%xmm13
	movaps	%xmm0,96(%rsp)
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
	movq	-8(%r11),%rbp
.cfi_restore	%rbp
	leaq	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lxts_enc_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_xts_encrypt,.-aesni_xts_encrypt
.globl	aesni_xts_decrypt
.type	aesni_xts_decrypt,@function
.align	16
aesni_xts_decrypt:
.cfi_startproc	
	leaq	(%rsp),%r11
.cfi_def_cfa_register	%r11
	pushq	%rbp
.cfi_offset	%rbp,-16
	subq	$112,%rsp
	andq	$-16,%rsp
	movups	(%r9),%xmm2
	movl	240(%r8),%eax
	movl	240(%rcx),%r10d
	movups	(%r8),%xmm0
	movups	16(%r8),%xmm1
	leaq	32(%r8),%r8
	xorps	%xmm0,%xmm2
.Loop_enc1_11:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%r8),%xmm1
	leaq	16(%r8),%r8
	jnz	.Loop_enc1_11
.byte	102,15,56,221,209
	xorl	%eax,%eax
	testq	$15,%rdx
	setnz	%al
	shlq	$4,%rax
	subq	%rax,%rdx

	movups	(%rcx),%xmm0
	movq	%rcx,%rbp
	movl	%r10d,%eax
	shll	$4,%r10d
	movq	%rdx,%r9
	andq	$-16,%rdx

	movups	16(%rcx,%r10,1),%xmm1

	movdqa	.Lxts_magic(%rip),%xmm8
	movdqa	%xmm2,%xmm15
	pshufd	$0x5f,%xmm2,%xmm9
	pxor	%xmm0,%xmm1
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm10
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm10
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm11
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm11
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm12
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm12
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
	movdqa	%xmm15,%xmm13
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
	pxor	%xmm0,%xmm13
	pxor	%xmm14,%xmm15
	movdqa	%xmm15,%xmm14
	psrad	$31,%xmm9
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm9
	pxor	%xmm0,%xmm14
	pxor	%xmm9,%xmm15
	movaps	%xmm1,96(%rsp)

	subq	$96,%rdx
	jc	.Lxts_dec_short

	movl	$16+96,%eax
	leaq	32(%rbp,%r10,1),%rcx
	subq	%r10,%rax
	movups	16(%rbp),%xmm1
	movq	%rax,%r10
	leaq	.Lxts_magic(%rip),%r8
	jmp	.Lxts_dec_grandloop

.align	32
.Lxts_dec_grandloop:
	movdqu	0(%rdi),%xmm2
	movdqa	%xmm0,%xmm8
	movdqu	16(%rdi),%xmm3
	pxor	%xmm10,%xmm2
	movdqu	32(%rdi),%xmm4
	pxor	%xmm11,%xmm3
.byte	102,15,56,222,209
	movdqu	48(%rdi),%xmm5
	pxor	%xmm12,%xmm4
.byte	102,15,56,222,217
	movdqu	64(%rdi),%xmm6
	pxor	%xmm13,%xmm5
.byte	102,15,56,222,225
	movdqu	80(%rdi),%xmm7
	pxor	%xmm15,%xmm8
	movdqa	96(%rsp),%xmm9
	pxor	%xmm14,%xmm6
.byte	102,15,56,222,233
	movups	32(%rbp),%xmm0
	leaq	96(%rdi),%rdi
	pxor	%xmm8,%xmm7

	pxor	%xmm9,%xmm10
.byte	102,15,56,222,241
	pxor	%xmm9,%xmm11
	movdqa	%xmm10,0(%rsp)
.byte	102,15,56,222,249
	movups	48(%rbp),%xmm1
	pxor	%xmm9,%xmm12

.byte	102,15,56,222,208
	pxor	%xmm9,%xmm13
	movdqa	%xmm11,16(%rsp)
.byte	102,15,56,222,216
	pxor	%xmm9,%xmm14
	movdqa	%xmm12,32(%rsp)
.byte	102,15,56,222,224
.byte	102,15,56,222,232
	pxor	%xmm9,%xmm8
	movdqa	%xmm14,64(%rsp)
.byte	102,15,56,222,240
.byte	102,15,56,222,248
	movups	64(%rbp),%xmm0
	movdqa	%xmm8,80(%rsp)
	pshufd	$0x5f,%xmm15,%xmm9
	jmp	.Lxts_dec_loop6
.align	32
.Lxts_dec_loop6:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
	movups	-64(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
	movups	-80(%rcx,%rax,1),%xmm0
	jnz	.Lxts_dec_loop6

	movdqa	(%r8),%xmm8
	movdqa	%xmm9,%xmm14
	paddd	%xmm9,%xmm9
.byte	102,15,56,222,209
	paddq	%xmm15,%xmm15
	psrad	$31,%xmm14
.byte	102,15,56,222,217
	pand	%xmm8,%xmm14
	movups	(%rbp),%xmm10
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
	pxor	%xmm14,%xmm15
	movaps	%xmm10,%xmm11
.byte	102,15,56,222,249
	movups	-64(%rcx),%xmm1

	movdqa	%xmm9,%xmm14
.byte	102,15,56,222,208
	paddd	%xmm9,%xmm9
	pxor	%xmm15,%xmm10
.byte	102,15,56,222,216
	psrad	$31,%xmm14
	paddq	%xmm15,%xmm15
.byte	102,15,56,222,224
.byte	102,15,56,222,232
	pand	%xmm8,%xmm14
	movaps	%xmm11,%xmm12
.byte	102,15,56,222,240
	pxor	%xmm14,%xmm15
	movdqa	%xmm9,%xmm14
.byte	102,15,56,222,248
	movups	-48(%rcx),%xmm0

	paddd	%xmm9,%xmm9
.byte	102,15,56,222,209
	pxor	%xmm15,%xmm11
	psrad	$31,%xmm14
.byte	102,15,56,222,217
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	movdqa	%xmm13,48(%rsp)
	pxor	%xmm14,%xmm15
.byte	102,15,56,222,241
	movaps	%xmm12,%xmm13
	movdqa	%xmm9,%xmm14
.byte	102,15,56,222,249
	movups	-32(%rcx),%xmm1

	paddd	%xmm9,%xmm9
.byte	102,15,56,222,208
	pxor	%xmm15,%xmm12
	psrad	$31,%xmm14
.byte	102,15,56,222,216
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm14
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
	pxor	%xmm14,%xmm15
	movaps	%xmm13,%xmm14
.byte	102,15,56,222,248

	movdqa	%xmm9,%xmm0
	paddd	%xmm9,%xmm9
.byte	102,15,56,222,209
	pxor	%xmm15,%xmm13
	psrad	$31,%xmm0
.byte	102,15,56,222,217
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm0
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	pxor	%xmm0,%xmm15
	movups	(%rbp),%xmm0
.byte	102,15,56,222,241
.byte	102,15,56,222,249
	movups	16(%rbp),%xmm1

	pxor	%xmm15,%xmm14
.byte	102,15,56,223,84,36,0
	psrad	$31,%xmm9
	paddq	%xmm15,%xmm15
.byte	102,15,56,223,92,36,16
.byte	102,15,56,223,100,36,32
	pand	%xmm8,%xmm9
	movq	%r10,%rax
.byte	102,15,56,223,108,36,48
.byte	102,15,56,223,116,36,64
.byte	102,15,56,223,124,36,80
	pxor	%xmm9,%xmm15

	leaq	96(%rsi),%rsi
	movups	%xmm2,-96(%rsi)
	movups	%xmm3,-80(%rsi)
	movups	%xmm4,-64(%rsi)
	movups	%xmm5,-48(%rsi)
	movups	%xmm6,-32(%rsi)
	movups	%xmm7,-16(%rsi)
	subq	$96,%rdx
	jnc	.Lxts_dec_grandloop

	movl	$16+96,%eax
	subl	%r10d,%eax
	movq	%rbp,%rcx
	shrl	$4,%eax

.Lxts_dec_short:

	movl	%eax,%r10d
	pxor	%xmm0,%xmm10
	pxor	%xmm0,%xmm11
	addq	$96,%rdx
	jz	.Lxts_dec_done

	pxor	%xmm0,%xmm12
	cmpq	$0x20,%rdx
	jb	.Lxts_dec_one
	pxor	%xmm0,%xmm13
	je	.Lxts_dec_two

	pxor	%xmm0,%xmm14
	cmpq	$0x40,%rdx
	jb	.Lxts_dec_three
	je	.Lxts_dec_four

	movdqu	(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqu	32(%rdi),%xmm4
	pxor	%xmm10,%xmm2
	movdqu	48(%rdi),%xmm5
	pxor	%xmm11,%xmm3
	movdqu	64(%rdi),%xmm6
	leaq	80(%rdi),%rdi
	pxor	%xmm12,%xmm4
	pxor	%xmm13,%xmm5
	pxor	%xmm14,%xmm6

	call	_aesni_decrypt6

	xorps	%xmm10,%xmm2
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4
	movdqu	%xmm2,(%rsi)
	xorps	%xmm13,%xmm5
	movdqu	%xmm3,16(%rsi)
	xorps	%xmm14,%xmm6
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm14,%xmm14
	movdqu	%xmm5,48(%rsi)
	pcmpgtd	%xmm15,%xmm14
	movdqu	%xmm6,64(%rsi)
	leaq	80(%rsi),%rsi
	pshufd	$0x13,%xmm14,%xmm11
	andq	$15,%r9
	jz	.Lxts_dec_ret

	movdqa	%xmm15,%xmm10
	paddq	%xmm15,%xmm15
	pand	%xmm8,%xmm11
	pxor	%xmm15,%xmm11
	jmp	.Lxts_dec_done2

.align	16
.Lxts_dec_one:
	movups	(%rdi),%xmm2
	leaq	16(%rdi),%rdi
	xorps	%xmm10,%xmm2
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_dec1_12:
.byte	102,15,56,222,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_dec1_12
.byte	102,15,56,223,209
	xorps	%xmm10,%xmm2
	movdqa	%xmm11,%xmm10
	movups	%xmm2,(%rsi)
	movdqa	%xmm12,%xmm11
	leaq	16(%rsi),%rsi
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_two:
	movups	(%rdi),%xmm2
	movups	16(%rdi),%xmm3
	leaq	32(%rdi),%rdi
	xorps	%xmm10,%xmm2
	xorps	%xmm11,%xmm3

	call	_aesni_decrypt2

	xorps	%xmm10,%xmm2
	movdqa	%xmm12,%xmm10
	xorps	%xmm11,%xmm3
	movdqa	%xmm13,%xmm11
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	leaq	32(%rsi),%rsi
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_three:
	movups	(%rdi),%xmm2
	movups	16(%rdi),%xmm3
	movups	32(%rdi),%xmm4
	leaq	48(%rdi),%rdi
	xorps	%xmm10,%xmm2
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4

	call	_aesni_decrypt3

	xorps	%xmm10,%xmm2
	movdqa	%xmm13,%xmm10
	xorps	%xmm11,%xmm3
	movdqa	%xmm14,%xmm11
	xorps	%xmm12,%xmm4
	movups	%xmm2,(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	leaq	48(%rsi),%rsi
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_four:
	movups	(%rdi),%xmm2
	movups	16(%rdi),%xmm3
	movups	32(%rdi),%xmm4
	xorps	%xmm10,%xmm2
	movups	48(%rdi),%xmm5
	leaq	64(%rdi),%rdi
	xorps	%xmm11,%xmm3
	xorps	%xmm12,%xmm4
	xorps	%xmm13,%xmm5

	call	_aesni_decrypt4

	pxor	%xmm10,%xmm2
	movdqa	%xmm14,%xmm10
	pxor	%xmm11,%xmm3
	movdqa	%xmm15,%xmm11
	pxor	%xmm12,%xmm4
	movdqu	%xmm2,(%rsi)
	pxor	%xmm13,%xmm5
	movdqu	%xmm3,16(%rsi)
	movdqu	%xmm4,32(%rsi)
	movdqu	%xmm5,48(%rsi)
	leaq	64(%rsi),%rsi
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_done:
	andq	$15,%r9
	jz	.Lxts_dec_ret
.Lxts_dec_done2:
	movq	%r9,%rdx
	movq	%rbp,%rcx
	movl	%r10d,%eax

	movups	(%rdi),%xmm2
	xorps	%xmm11,%xmm2
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_dec1_13:
.byte	102,15,56,222,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_dec1_13
.byte	102,15,56,223,209
	xorps	%xmm11,%xmm2
	movups	%xmm2,(%rsi)

.Lxts_dec_steal:
	movzbl	16(%rdi),%eax
	movzbl	(%rsi),%ecx
	leaq	1(%rdi),%rdi
	movb	%al,(%rsi)
	movb	%cl,16(%rsi)
	leaq	1(%rsi),%rsi
	subq	$1,%rdx
	jnz	.Lxts_dec_steal

	subq	%r9,%rsi
	movq	%rbp,%rcx
	movl	%r10d,%eax

	movups	(%rsi),%xmm2
	xorps	%xmm10,%xmm2
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_dec1_14:
.byte	102,15,56,222,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_dec1_14
.byte	102,15,56,223,209
	xorps	%xmm10,%xmm2
	movups	%xmm2,(%rsi)

.Lxts_dec_ret:
	xorps	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	movaps	%xmm0,0(%rsp)
	pxor	%xmm8,%xmm8
	movaps	%xmm0,16(%rsp)
	pxor	%xmm9,%xmm9
	movaps	%xmm0,32(%rsp)
	pxor	%xmm10,%xmm10
	movaps	%xmm0,48(%rsp)
	pxor	%xmm11,%xmm11
	movaps	%xmm0,64(%rsp)
	pxor	%xmm12,%xmm12
	movaps	%xmm0,80(%rsp)
	pxor	%xmm13,%xmm13
	movaps	%xmm0,96(%rsp)
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
	movq	-8(%r11),%rbp
.cfi_restore	%rbp
	leaq	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lxts_dec_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_xts_decrypt,.-aesni_xts_decrypt
.globl	aesni_ocb_encrypt
.type	aesni_ocb_encrypt,@function
.align	32
aesni_ocb_encrypt:
.cfi_startproc	
	leaq	(%rsp),%rax
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
	movq	8(%rax),%rbx
	movq	8+8(%rax),%rbp

	movl	240(%rcx),%r10d
	movq	%rcx,%r11
	shll	$4,%r10d
	movups	(%rcx),%xmm9
	movups	16(%rcx,%r10,1),%xmm1

	movdqu	(%r9),%xmm15
	pxor	%xmm1,%xmm9
	pxor	%xmm1,%xmm15

	movl	$16+32,%eax
	leaq	32(%r11,%r10,1),%rcx
	movups	16(%r11),%xmm1
	subq	%r10,%rax
	movq	%rax,%r10

	movdqu	(%rbx),%xmm10
	movdqu	(%rbp),%xmm8

	testq	$1,%r8
	jnz	.Locb_enc_odd

	bsfq	%r8,%r12
	addq	$1,%r8
	shlq	$4,%r12
	movdqu	(%rbx,%r12,1),%xmm7
	movdqu	(%rdi),%xmm2
	leaq	16(%rdi),%rdi

	call	__ocb_encrypt1

	movdqa	%xmm7,%xmm15
	movups	%xmm2,(%rsi)
	leaq	16(%rsi),%rsi
	subq	$1,%rdx
	jz	.Locb_enc_done

.Locb_enc_odd:
	leaq	1(%r8),%r12
	leaq	3(%r8),%r13
	leaq	5(%r8),%r14
	leaq	6(%r8),%r8
	bsfq	%r12,%r12
	bsfq	%r13,%r13
	bsfq	%r14,%r14
	shlq	$4,%r12
	shlq	$4,%r13
	shlq	$4,%r14

	subq	$6,%rdx
	jc	.Locb_enc_short
	jmp	.Locb_enc_grandloop

.align	32
.Locb_enc_grandloop:
	movdqu	0(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqu	32(%rdi),%xmm4
	movdqu	48(%rdi),%xmm5
	movdqu	64(%rdi),%xmm6
	movdqu	80(%rdi),%xmm7
	leaq	96(%rdi),%rdi

	call	__ocb_encrypt6

	movups	%xmm2,0(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	movups	%xmm6,64(%rsi)
	movups	%xmm7,80(%rsi)
	leaq	96(%rsi),%rsi
	subq	$6,%rdx
	jnc	.Locb_enc_grandloop

.Locb_enc_short:
	addq	$6,%rdx
	jz	.Locb_enc_done

	movdqu	0(%rdi),%xmm2
	cmpq	$2,%rdx
	jb	.Locb_enc_one
	movdqu	16(%rdi),%xmm3
	je	.Locb_enc_two

	movdqu	32(%rdi),%xmm4
	cmpq	$4,%rdx
	jb	.Locb_enc_three
	movdqu	48(%rdi),%xmm5
	je	.Locb_enc_four

	movdqu	64(%rdi),%xmm6
	pxor	%xmm7,%xmm7

	call	__ocb_encrypt6

	movdqa	%xmm14,%xmm15
	movups	%xmm2,0(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)
	movups	%xmm6,64(%rsi)

	jmp	.Locb_enc_done

.align	16
.Locb_enc_one:
	movdqa	%xmm10,%xmm7

	call	__ocb_encrypt1

	movdqa	%xmm7,%xmm15
	movups	%xmm2,0(%rsi)
	jmp	.Locb_enc_done

.align	16
.Locb_enc_two:
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5

	call	__ocb_encrypt4

	movdqa	%xmm11,%xmm15
	movups	%xmm2,0(%rsi)
	movups	%xmm3,16(%rsi)

	jmp	.Locb_enc_done

.align	16
.Locb_enc_three:
	pxor	%xmm5,%xmm5

	call	__ocb_encrypt4

	movdqa	%xmm12,%xmm15
	movups	%xmm2,0(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)

	jmp	.Locb_enc_done

.align	16
.Locb_enc_four:
	call	__ocb_encrypt4

	movdqa	%xmm13,%xmm15
	movups	%xmm2,0(%rsi)
	movups	%xmm3,16(%rsi)
	movups	%xmm4,32(%rsi)
	movups	%xmm5,48(%rsi)

.Locb_enc_done:
	pxor	%xmm0,%xmm15
	movdqu	%xmm8,(%rbp)
	movdqu	%xmm15,(%r9)

	xorps	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
	pxor	%xmm10,%xmm10
	pxor	%xmm11,%xmm11
	pxor	%xmm12,%xmm12
	pxor	%xmm13,%xmm13
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
	leaq	40(%rsp),%rax
.cfi_def_cfa	%rax,8
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
.Locb_enc_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_ocb_encrypt,.-aesni_ocb_encrypt

.type	__ocb_encrypt6,@function
.align	32
__ocb_encrypt6:
	pxor	%xmm9,%xmm15
	movdqu	(%rbx,%r12,1),%xmm11
	movdqa	%xmm10,%xmm12
	movdqu	(%rbx,%r13,1),%xmm13
	movdqa	%xmm10,%xmm14
	pxor	%xmm15,%xmm10
	movdqu	(%rbx,%r14,1),%xmm15
	pxor	%xmm10,%xmm11
	pxor	%xmm2,%xmm8
	pxor	%xmm10,%xmm2
	pxor	%xmm11,%xmm12
	pxor	%xmm3,%xmm8
	pxor	%xmm11,%xmm3
	pxor	%xmm12,%xmm13
	pxor	%xmm4,%xmm8
	pxor	%xmm12,%xmm4
	pxor	%xmm13,%xmm14
	pxor	%xmm5,%xmm8
	pxor	%xmm13,%xmm5
	pxor	%xmm14,%xmm15
	pxor	%xmm6,%xmm8
	pxor	%xmm14,%xmm6
	pxor	%xmm7,%xmm8
	pxor	%xmm15,%xmm7
	movups	32(%r11),%xmm0

	leaq	1(%r8),%r12
	leaq	3(%r8),%r13
	leaq	5(%r8),%r14
	addq	$6,%r8
	pxor	%xmm9,%xmm10
	bsfq	%r12,%r12
	bsfq	%r13,%r13
	bsfq	%r14,%r14

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	pxor	%xmm9,%xmm11
	pxor	%xmm9,%xmm12
.byte	102,15,56,220,241
	pxor	%xmm9,%xmm13
	pxor	%xmm9,%xmm14
.byte	102,15,56,220,249
	movups	48(%r11),%xmm1
	pxor	%xmm9,%xmm15

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
	movups	64(%r11),%xmm0
	shlq	$4,%r12
	shlq	$4,%r13
	jmp	.Locb_enc_loop6

.align	32
.Locb_enc_loop6:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
.byte	102,15,56,220,240
.byte	102,15,56,220,248
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Locb_enc_loop6

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
.byte	102,15,56,220,241
.byte	102,15,56,220,249
	movups	16(%r11),%xmm1
	shlq	$4,%r14

.byte	102,65,15,56,221,210
	movdqu	(%rbx),%xmm10
	movq	%r10,%rax
.byte	102,65,15,56,221,219
.byte	102,65,15,56,221,228
.byte	102,65,15,56,221,237
.byte	102,65,15,56,221,246
.byte	102,65,15,56,221,255
	.byte	0xf3,0xc3
.size	__ocb_encrypt6,.-__ocb_encrypt6

.type	__ocb_encrypt4,@function
.align	32
__ocb_encrypt4:
	pxor	%xmm9,%xmm15
	movdqu	(%rbx,%r12,1),%xmm11
	movdqa	%xmm10,%xmm12
	movdqu	(%rbx,%r13,1),%xmm13
	pxor	%xmm15,%xmm10
	pxor	%xmm10,%xmm11
	pxor	%xmm2,%xmm8
	pxor	%xmm10,%xmm2
	pxor	%xmm11,%xmm12
	pxor	%xmm3,%xmm8
	pxor	%xmm11,%xmm3
	pxor	%xmm12,%xmm13
	pxor	%xmm4,%xmm8
	pxor	%xmm12,%xmm4
	pxor	%xmm5,%xmm8
	pxor	%xmm13,%xmm5
	movups	32(%r11),%xmm0

	pxor	%xmm9,%xmm10
	pxor	%xmm9,%xmm11
	pxor	%xmm9,%xmm12
	pxor	%xmm9,%xmm13

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movups	48(%r11),%xmm1

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	movups	64(%r11),%xmm0
	jmp	.Locb_enc_loop4

.align	32
.Locb_enc_loop4:
.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,220,208
.byte	102,15,56,220,216
.byte	102,15,56,220,224
.byte	102,15,56,220,232
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Locb_enc_loop4

.byte	102,15,56,220,209
.byte	102,15,56,220,217
.byte	102,15,56,220,225
.byte	102,15,56,220,233
	movups	16(%r11),%xmm1
	movq	%r10,%rax

.byte	102,65,15,56,221,210
.byte	102,65,15,56,221,219
.byte	102,65,15,56,221,228
.byte	102,65,15,56,221,237
	.byte	0xf3,0xc3
.size	__ocb_encrypt4,.-__ocb_encrypt4

.type	__ocb_encrypt1,@function
.align	32
__ocb_encrypt1:
	pxor	%xmm15,%xmm7
	pxor	%xmm9,%xmm7
	pxor	%xmm2,%xmm8
	pxor	%xmm7,%xmm2
	movups	32(%r11),%xmm0

.byte	102,15,56,220,209
	movups	48(%r11),%xmm1
	pxor	%xmm9,%xmm7

.byte	102,15,56,220,208
	movups	64(%r11),%xmm0
	jmp	.Locb_enc_loop1

.align	32
.Locb_enc_loop1:
.byte	102,15,56,220,209
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,220,208
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Locb_enc_loop1

.byte	102,15,56,220,209
	movups	16(%r11),%xmm1
	movq	%r10,%rax

.byte	102,15,56,221,215
	.byte	0xf3,0xc3
.size	__ocb_encrypt1,.-__ocb_encrypt1

.globl	aesni_ocb_decrypt
.type	aesni_ocb_decrypt,@function
.align	32
aesni_ocb_decrypt:
.cfi_startproc	
	leaq	(%rsp),%rax
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
	movq	8(%rax),%rbx
	movq	8+8(%rax),%rbp

	movl	240(%rcx),%r10d
	movq	%rcx,%r11
	shll	$4,%r10d
	movups	(%rcx),%xmm9
	movups	16(%rcx,%r10,1),%xmm1

	movdqu	(%r9),%xmm15
	pxor	%xmm1,%xmm9
	pxor	%xmm1,%xmm15

	movl	$16+32,%eax
	leaq	32(%r11,%r10,1),%rcx
	movups	16(%r11),%xmm1
	subq	%r10,%rax
	movq	%rax,%r10

	movdqu	(%rbx),%xmm10
	movdqu	(%rbp),%xmm8

	testq	$1,%r8
	jnz	.Locb_dec_odd

	bsfq	%r8,%r12
	addq	$1,%r8
	shlq	$4,%r12
	movdqu	(%rbx,%r12,1),%xmm7
	movdqu	(%rdi),%xmm2
	leaq	16(%rdi),%rdi

	call	__ocb_decrypt1

	movdqa	%xmm7,%xmm15
	movups	%xmm2,(%rsi)
	xorps	%xmm2,%xmm8
	leaq	16(%rsi),%rsi
	subq	$1,%rdx
	jz	.Locb_dec_done

.Locb_dec_odd:
	leaq	1(%r8),%r12
	leaq	3(%r8),%r13
	leaq	5(%r8),%r14
	leaq	6(%r8),%r8
	bsfq	%r12,%r12
	bsfq	%r13,%r13
	bsfq	%r14,%r14
	shlq	$4,%r12
	shlq	$4,%r13
	shlq	$4,%r14

	subq	$6,%rdx
	jc	.Locb_dec_short
	jmp	.Locb_dec_grandloop

.align	32
.Locb_dec_grandloop:
	movdqu	0(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqu	32(%rdi),%xmm4
	movdqu	48(%rdi),%xmm5
	movdqu	64(%rdi),%xmm6
	movdqu	80(%rdi),%xmm7
	leaq	96(%rdi),%rdi

	call	__ocb_decrypt6

	movups	%xmm2,0(%rsi)
	pxor	%xmm2,%xmm8
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm8
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm8
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm8
	movups	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm8
	movups	%xmm7,80(%rsi)
	pxor	%xmm7,%xmm8
	leaq	96(%rsi),%rsi
	subq	$6,%rdx
	jnc	.Locb_dec_grandloop

.Locb_dec_short:
	addq	$6,%rdx
	jz	.Locb_dec_done

	movdqu	0(%rdi),%xmm2
	cmpq	$2,%rdx
	jb	.Locb_dec_one
	movdqu	16(%rdi),%xmm3
	je	.Locb_dec_two

	movdqu	32(%rdi),%xmm4
	cmpq	$4,%rdx
	jb	.Locb_dec_three
	movdqu	48(%rdi),%xmm5
	je	.Locb_dec_four

	movdqu	64(%rdi),%xmm6
	pxor	%xmm7,%xmm7

	call	__ocb_decrypt6

	movdqa	%xmm14,%xmm15
	movups	%xmm2,0(%rsi)
	pxor	%xmm2,%xmm8
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm8
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm8
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm8
	movups	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm8

	jmp	.Locb_dec_done

.align	16
.Locb_dec_one:
	movdqa	%xmm10,%xmm7

	call	__ocb_decrypt1

	movdqa	%xmm7,%xmm15
	movups	%xmm2,0(%rsi)
	xorps	%xmm2,%xmm8
	jmp	.Locb_dec_done

.align	16
.Locb_dec_two:
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5

	call	__ocb_decrypt4

	movdqa	%xmm11,%xmm15
	movups	%xmm2,0(%rsi)
	xorps	%xmm2,%xmm8
	movups	%xmm3,16(%rsi)
	xorps	%xmm3,%xmm8

	jmp	.Locb_dec_done

.align	16
.Locb_dec_three:
	pxor	%xmm5,%xmm5

	call	__ocb_decrypt4

	movdqa	%xmm12,%xmm15
	movups	%xmm2,0(%rsi)
	xorps	%xmm2,%xmm8
	movups	%xmm3,16(%rsi)
	xorps	%xmm3,%xmm8
	movups	%xmm4,32(%rsi)
	xorps	%xmm4,%xmm8

	jmp	.Locb_dec_done

.align	16
.Locb_dec_four:
	call	__ocb_decrypt4

	movdqa	%xmm13,%xmm15
	movups	%xmm2,0(%rsi)
	pxor	%xmm2,%xmm8
	movups	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm8
	movups	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm8
	movups	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm8

.Locb_dec_done:
	pxor	%xmm0,%xmm15
	movdqu	%xmm8,(%rbp)
	movdqu	%xmm15,(%r9)

	xorps	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
	pxor	%xmm10,%xmm10
	pxor	%xmm11,%xmm11
	pxor	%xmm12,%xmm12
	pxor	%xmm13,%xmm13
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
	leaq	40(%rsp),%rax
.cfi_def_cfa	%rax,8
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
.Locb_dec_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_ocb_decrypt,.-aesni_ocb_decrypt

.type	__ocb_decrypt6,@function
.align	32
__ocb_decrypt6:
	pxor	%xmm9,%xmm15
	movdqu	(%rbx,%r12,1),%xmm11
	movdqa	%xmm10,%xmm12
	movdqu	(%rbx,%r13,1),%xmm13
	movdqa	%xmm10,%xmm14
	pxor	%xmm15,%xmm10
	movdqu	(%rbx,%r14,1),%xmm15
	pxor	%xmm10,%xmm11
	pxor	%xmm10,%xmm2
	pxor	%xmm11,%xmm12
	pxor	%xmm11,%xmm3
	pxor	%xmm12,%xmm13
	pxor	%xmm12,%xmm4
	pxor	%xmm13,%xmm14
	pxor	%xmm13,%xmm5
	pxor	%xmm14,%xmm15
	pxor	%xmm14,%xmm6
	pxor	%xmm15,%xmm7
	movups	32(%r11),%xmm0

	leaq	1(%r8),%r12
	leaq	3(%r8),%r13
	leaq	5(%r8),%r14
	addq	$6,%r8
	pxor	%xmm9,%xmm10
	bsfq	%r12,%r12
	bsfq	%r13,%r13
	bsfq	%r14,%r14

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	pxor	%xmm9,%xmm11
	pxor	%xmm9,%xmm12
.byte	102,15,56,222,241
	pxor	%xmm9,%xmm13
	pxor	%xmm9,%xmm14
.byte	102,15,56,222,249
	movups	48(%r11),%xmm1
	pxor	%xmm9,%xmm15

.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
	movups	64(%r11),%xmm0
	shlq	$4,%r12
	shlq	$4,%r13
	jmp	.Locb_dec_loop6

.align	32
.Locb_dec_loop6:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Locb_dec_loop6

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
	movups	16(%r11),%xmm1
	shlq	$4,%r14

.byte	102,65,15,56,223,210
	movdqu	(%rbx),%xmm10
	movq	%r10,%rax
.byte	102,65,15,56,223,219
.byte	102,65,15,56,223,228
.byte	102,65,15,56,223,237
.byte	102,65,15,56,223,246
.byte	102,65,15,56,223,255
	.byte	0xf3,0xc3
.size	__ocb_decrypt6,.-__ocb_decrypt6

.type	__ocb_decrypt4,@function
.align	32
__ocb_decrypt4:
	pxor	%xmm9,%xmm15
	movdqu	(%rbx,%r12,1),%xmm11
	movdqa	%xmm10,%xmm12
	movdqu	(%rbx,%r13,1),%xmm13
	pxor	%xmm15,%xmm10
	pxor	%xmm10,%xmm11
	pxor	%xmm10,%xmm2
	pxor	%xmm11,%xmm12
	pxor	%xmm11,%xmm3
	pxor	%xmm12,%xmm13
	pxor	%xmm12,%xmm4
	pxor	%xmm13,%xmm5
	movups	32(%r11),%xmm0

	pxor	%xmm9,%xmm10
	pxor	%xmm9,%xmm11
	pxor	%xmm9,%xmm12
	pxor	%xmm9,%xmm13

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	movups	48(%r11),%xmm1

.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
	movups	64(%r11),%xmm0
	jmp	.Locb_dec_loop4

.align	32
.Locb_dec_loop4:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Locb_dec_loop4

.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	movups	16(%r11),%xmm1
	movq	%r10,%rax

.byte	102,65,15,56,223,210
.byte	102,65,15,56,223,219
.byte	102,65,15,56,223,228
.byte	102,65,15,56,223,237
	.byte	0xf3,0xc3
.size	__ocb_decrypt4,.-__ocb_decrypt4

.type	__ocb_decrypt1,@function
.align	32
__ocb_decrypt1:
	pxor	%xmm15,%xmm7
	pxor	%xmm9,%xmm7
	pxor	%xmm7,%xmm2
	movups	32(%r11),%xmm0

.byte	102,15,56,222,209
	movups	48(%r11),%xmm1
	pxor	%xmm9,%xmm7

.byte	102,15,56,222,208
	movups	64(%r11),%xmm0
	jmp	.Locb_dec_loop1

.align	32
.Locb_dec_loop1:
.byte	102,15,56,222,209
	movups	(%rcx,%rax,1),%xmm1
	addq	$32,%rax

.byte	102,15,56,222,208
	movups	-16(%rcx,%rax,1),%xmm0
	jnz	.Locb_dec_loop1

.byte	102,15,56,222,209
	movups	16(%r11),%xmm1
	movq	%r10,%rax

.byte	102,15,56,223,215
	.byte	0xf3,0xc3
.size	__ocb_decrypt1,.-__ocb_decrypt1
.globl	aesni_cbc_encrypt
.type	aesni_cbc_encrypt,@function
.align	16
aesni_cbc_encrypt:
.cfi_startproc	
	testq	%rdx,%rdx
	jz	.Lcbc_ret

	movl	240(%rcx),%r10d
	movq	%rcx,%r11
	testl	%r9d,%r9d
	jz	.Lcbc_decrypt

	movups	(%r8),%xmm2
	movl	%r10d,%eax
	cmpq	$16,%rdx
	jb	.Lcbc_enc_tail
	subq	$16,%rdx
	jmp	.Lcbc_enc_loop
.align	16
.Lcbc_enc_loop:
	movups	(%rdi),%xmm3
	leaq	16(%rdi),%rdi

	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	xorps	%xmm0,%xmm3
	leaq	32(%rcx),%rcx
	xorps	%xmm3,%xmm2
.Loop_enc1_15:
.byte	102,15,56,220,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_enc1_15
.byte	102,15,56,221,209
	movl	%r10d,%eax
	movq	%r11,%rcx
	movups	%xmm2,0(%rsi)
	leaq	16(%rsi),%rsi
	subq	$16,%rdx
	jnc	.Lcbc_enc_loop
	addq	$16,%rdx
	jnz	.Lcbc_enc_tail
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	movups	%xmm2,(%r8)
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	jmp	.Lcbc_ret

.Lcbc_enc_tail:
	movq	%rdx,%rcx
	xchgq	%rdi,%rsi
.long	0x9066A4F3
	movl	$16,%ecx
	subq	%rdx,%rcx
	xorl	%eax,%eax
.long	0x9066AAF3
	leaq	-16(%rdi),%rdi
	movl	%r10d,%eax
	movq	%rdi,%rsi
	movq	%r11,%rcx
	xorq	%rdx,%rdx
	jmp	.Lcbc_enc_loop

.align	16
.Lcbc_decrypt:
	cmpq	$16,%rdx
	jne	.Lcbc_decrypt_bulk



	movdqu	(%rdi),%xmm2
	movdqu	(%r8),%xmm3
	movdqa	%xmm2,%xmm4
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_dec1_16:
.byte	102,15,56,222,209
	decl	%r10d
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_dec1_16
.byte	102,15,56,223,209
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	movdqu	%xmm4,(%r8)
	xorps	%xmm3,%xmm2
	pxor	%xmm3,%xmm3
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	jmp	.Lcbc_ret
.align	16
.Lcbc_decrypt_bulk:
	leaq	(%rsp),%r11
.cfi_def_cfa_register	%r11
	pushq	%rbp
.cfi_offset	%rbp,-16
	subq	$16,%rsp
	andq	$-16,%rsp
	movq	%rcx,%rbp
	movups	(%r8),%xmm10
	movl	%r10d,%eax
	cmpq	$0x50,%rdx
	jbe	.Lcbc_dec_tail

	movups	(%rcx),%xmm0
	movdqu	0(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqa	%xmm2,%xmm11
	movdqu	32(%rdi),%xmm4
	movdqa	%xmm3,%xmm12
	movdqu	48(%rdi),%xmm5
	movdqa	%xmm4,%xmm13
	movdqu	64(%rdi),%xmm6
	movdqa	%xmm5,%xmm14
	movdqu	80(%rdi),%xmm7
	movdqa	%xmm6,%xmm15
	movl	OPENSSL_ia32cap_P+4(%rip),%r9d
	cmpq	$0x70,%rdx
	jbe	.Lcbc_dec_six_or_seven

	andl	$71303168,%r9d
	subq	$0x50,%rdx
	cmpl	$4194304,%r9d
	je	.Lcbc_dec_loop6_enter
	subq	$0x20,%rdx
	leaq	112(%rcx),%rcx
	jmp	.Lcbc_dec_loop8_enter
.align	16
.Lcbc_dec_loop8:
	movups	%xmm9,(%rsi)
	leaq	16(%rsi),%rsi
.Lcbc_dec_loop8_enter:
	movdqu	96(%rdi),%xmm8
	pxor	%xmm0,%xmm2
	movdqu	112(%rdi),%xmm9
	pxor	%xmm0,%xmm3
	movups	16-112(%rcx),%xmm1
	pxor	%xmm0,%xmm4
	movq	$-1,%rbp
	cmpq	$0x70,%rdx
	pxor	%xmm0,%xmm5
	pxor	%xmm0,%xmm6
	pxor	%xmm0,%xmm7
	pxor	%xmm0,%xmm8

.byte	102,15,56,222,209
	pxor	%xmm0,%xmm9
	movups	32-112(%rcx),%xmm0
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
	adcq	$0,%rbp
	andq	$128,%rbp
.byte	102,68,15,56,222,201
	addq	%rdi,%rbp
	movups	48-112(%rcx),%xmm1
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	64-112(%rcx),%xmm0
	nop
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
	movups	80-112(%rcx),%xmm1
	nop
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	96-112(%rcx),%xmm0
	nop
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
	movups	112-112(%rcx),%xmm1
	nop
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	128-112(%rcx),%xmm0
	nop
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
	movups	144-112(%rcx),%xmm1
	cmpl	$11,%eax
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	160-112(%rcx),%xmm0
	jb	.Lcbc_dec_done
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
	movups	176-112(%rcx),%xmm1
	nop
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	192-112(%rcx),%xmm0
	je	.Lcbc_dec_done
.byte	102,15,56,222,209
.byte	102,15,56,222,217
.byte	102,15,56,222,225
.byte	102,15,56,222,233
.byte	102,15,56,222,241
.byte	102,15,56,222,249
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
	movups	208-112(%rcx),%xmm1
	nop
.byte	102,15,56,222,208
.byte	102,15,56,222,216
.byte	102,15,56,222,224
.byte	102,15,56,222,232
.byte	102,15,56,222,240
.byte	102,15,56,222,248
.byte	102,68,15,56,222,192
.byte	102,68,15,56,222,200
	movups	224-112(%rcx),%xmm0
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_done:
.byte	102,15,56,222,209
.byte	102,15,56,222,217
	pxor	%xmm0,%xmm10
	pxor	%xmm0,%xmm11
.byte	102,15,56,222,225
.byte	102,15,56,222,233
	pxor	%xmm0,%xmm12
	pxor	%xmm0,%xmm13
.byte	102,15,56,222,241
.byte	102,15,56,222,249
	pxor	%xmm0,%xmm14
	pxor	%xmm0,%xmm15
.byte	102,68,15,56,222,193
.byte	102,68,15,56,222,201
	movdqu	80(%rdi),%xmm1

.byte	102,65,15,56,223,210
	movdqu	96(%rdi),%xmm10
	pxor	%xmm0,%xmm1
.byte	102,65,15,56,223,219
	pxor	%xmm0,%xmm10
	movdqu	112(%rdi),%xmm0
.byte	102,65,15,56,223,228
	leaq	128(%rdi),%rdi
	movdqu	0(%rbp),%xmm11
.byte	102,65,15,56,223,237
.byte	102,65,15,56,223,246
	movdqu	16(%rbp),%xmm12
	movdqu	32(%rbp),%xmm13
.byte	102,65,15,56,223,255
.byte	102,68,15,56,223,193
	movdqu	48(%rbp),%xmm14
	movdqu	64(%rbp),%xmm15
.byte	102,69,15,56,223,202
	movdqa	%xmm0,%xmm10
	movdqu	80(%rbp),%xmm1
	movups	-112(%rcx),%xmm0

	movups	%xmm2,(%rsi)
	movdqa	%xmm11,%xmm2
	movups	%xmm3,16(%rsi)
	movdqa	%xmm12,%xmm3
	movups	%xmm4,32(%rsi)
	movdqa	%xmm13,%xmm4
	movups	%xmm5,48(%rsi)
	movdqa	%xmm14,%xmm5
	movups	%xmm6,64(%rsi)
	movdqa	%xmm15,%xmm6
	movups	%xmm7,80(%rsi)
	movdqa	%xmm1,%xmm7
	movups	%xmm8,96(%rsi)
	leaq	112(%rsi),%rsi

	subq	$0x80,%rdx
	ja	.Lcbc_dec_loop8

	movaps	%xmm9,%xmm2
	leaq	-112(%rcx),%rcx
	addq	$0x70,%rdx
	jle	.Lcbc_dec_clear_tail_collected
	movups	%xmm9,(%rsi)
	leaq	16(%rsi),%rsi
	cmpq	$0x50,%rdx
	jbe	.Lcbc_dec_tail

	movaps	%xmm11,%xmm2
.Lcbc_dec_six_or_seven:
	cmpq	$0x60,%rdx
	ja	.Lcbc_dec_seven

	movaps	%xmm7,%xmm8
	call	_aesni_decrypt6
	pxor	%xmm10,%xmm2
	movaps	%xmm8,%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	pxor	%xmm13,%xmm5
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	pxor	%xmm14,%xmm6
	movdqu	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	pxor	%xmm15,%xmm7
	movdqu	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm6
	leaq	80(%rsi),%rsi
	movdqa	%xmm7,%xmm2
	pxor	%xmm7,%xmm7
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_seven:
	movups	96(%rdi),%xmm8
	xorps	%xmm9,%xmm9
	call	_aesni_decrypt8
	movups	80(%rdi),%xmm9
	pxor	%xmm10,%xmm2
	movups	96(%rdi),%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	pxor	%xmm13,%xmm5
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	pxor	%xmm14,%xmm6
	movdqu	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	pxor	%xmm15,%xmm7
	movdqu	%xmm6,64(%rsi)
	pxor	%xmm6,%xmm6
	pxor	%xmm9,%xmm8
	movdqu	%xmm7,80(%rsi)
	pxor	%xmm7,%xmm7
	leaq	96(%rsi),%rsi
	movdqa	%xmm8,%xmm2
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_loop6:
	movups	%xmm7,(%rsi)
	leaq	16(%rsi),%rsi
	movdqu	0(%rdi),%xmm2
	movdqu	16(%rdi),%xmm3
	movdqa	%xmm2,%xmm11
	movdqu	32(%rdi),%xmm4
	movdqa	%xmm3,%xmm12
	movdqu	48(%rdi),%xmm5
	movdqa	%xmm4,%xmm13
	movdqu	64(%rdi),%xmm6
	movdqa	%xmm5,%xmm14
	movdqu	80(%rdi),%xmm7
	movdqa	%xmm6,%xmm15
.Lcbc_dec_loop6_enter:
	leaq	96(%rdi),%rdi
	movdqa	%xmm7,%xmm8

	call	_aesni_decrypt6

	pxor	%xmm10,%xmm2
	movdqa	%xmm8,%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm13,%xmm5
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm14,%xmm6
	movq	%rbp,%rcx
	movdqu	%xmm5,48(%rsi)
	pxor	%xmm15,%xmm7
	movl	%r10d,%eax
	movdqu	%xmm6,64(%rsi)
	leaq	80(%rsi),%rsi
	subq	$0x60,%rdx
	ja	.Lcbc_dec_loop6

	movdqa	%xmm7,%xmm2
	addq	$0x50,%rdx
	jle	.Lcbc_dec_clear_tail_collected
	movups	%xmm7,(%rsi)
	leaq	16(%rsi),%rsi

.Lcbc_dec_tail:
	movups	(%rdi),%xmm2
	subq	$0x10,%rdx
	jbe	.Lcbc_dec_one

	movups	16(%rdi),%xmm3
	movaps	%xmm2,%xmm11
	subq	$0x10,%rdx
	jbe	.Lcbc_dec_two

	movups	32(%rdi),%xmm4
	movaps	%xmm3,%xmm12
	subq	$0x10,%rdx
	jbe	.Lcbc_dec_three

	movups	48(%rdi),%xmm5
	movaps	%xmm4,%xmm13
	subq	$0x10,%rdx
	jbe	.Lcbc_dec_four

	movups	64(%rdi),%xmm6
	movaps	%xmm5,%xmm14
	movaps	%xmm6,%xmm15
	xorps	%xmm7,%xmm7
	call	_aesni_decrypt6
	pxor	%xmm10,%xmm2
	movaps	%xmm15,%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	pxor	%xmm13,%xmm5
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	pxor	%xmm14,%xmm6
	movdqu	%xmm5,48(%rsi)
	pxor	%xmm5,%xmm5
	leaq	64(%rsi),%rsi
	movdqa	%xmm6,%xmm2
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	subq	$0x10,%rdx
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_one:
	movaps	%xmm2,%xmm11
	movups	(%rcx),%xmm0
	movups	16(%rcx),%xmm1
	leaq	32(%rcx),%rcx
	xorps	%xmm0,%xmm2
.Loop_dec1_17:
.byte	102,15,56,222,209
	decl	%eax
	movups	(%rcx),%xmm1
	leaq	16(%rcx),%rcx
	jnz	.Loop_dec1_17
.byte	102,15,56,223,209
	xorps	%xmm10,%xmm2
	movaps	%xmm11,%xmm10
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_two:
	movaps	%xmm3,%xmm12
	call	_aesni_decrypt2
	pxor	%xmm10,%xmm2
	movaps	%xmm12,%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	movdqa	%xmm3,%xmm2
	pxor	%xmm3,%xmm3
	leaq	16(%rsi),%rsi
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_three:
	movaps	%xmm4,%xmm13
	call	_aesni_decrypt3
	pxor	%xmm10,%xmm2
	movaps	%xmm13,%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	movdqa	%xmm4,%xmm2
	pxor	%xmm4,%xmm4
	leaq	32(%rsi),%rsi
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_four:
	movaps	%xmm5,%xmm14
	call	_aesni_decrypt4
	pxor	%xmm10,%xmm2
	movaps	%xmm14,%xmm10
	pxor	%xmm11,%xmm3
	movdqu	%xmm2,(%rsi)
	pxor	%xmm12,%xmm4
	movdqu	%xmm3,16(%rsi)
	pxor	%xmm3,%xmm3
	pxor	%xmm13,%xmm5
	movdqu	%xmm4,32(%rsi)
	pxor	%xmm4,%xmm4
	movdqa	%xmm5,%xmm2
	pxor	%xmm5,%xmm5
	leaq	48(%rsi),%rsi
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_clear_tail_collected:
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
.Lcbc_dec_tail_collected:
	movups	%xmm10,(%r8)
	andq	$15,%rdx
	jnz	.Lcbc_dec_tail_partial
	movups	%xmm2,(%rsi)
	pxor	%xmm2,%xmm2
	jmp	.Lcbc_dec_ret
.align	16
.Lcbc_dec_tail_partial:
	movaps	%xmm2,(%rsp)
	pxor	%xmm2,%xmm2
	movq	$16,%rcx
	movq	%rsi,%rdi
	subq	%rdx,%rcx
	leaq	(%rsp),%rsi
.long	0x9066A4F3
	movdqa	%xmm2,(%rsp)

.Lcbc_dec_ret:
	xorps	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	movq	-8(%r11),%rbp
.cfi_restore	%rbp
	leaq	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lcbc_ret:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	aesni_cbc_encrypt,.-aesni_cbc_encrypt
.globl	aesni_set_decrypt_key
.type	aesni_set_decrypt_key,@function
.align	16
aesni_set_decrypt_key:
.cfi_startproc	
.byte	0x48,0x83,0xEC,0x08
.cfi_adjust_cfa_offset	8
	call	__aesni_set_encrypt_key
	shll	$4,%esi
	testl	%eax,%eax
	jnz	.Ldec_key_ret
	leaq	16(%rdx,%rsi,1),%rdi

	movups	(%rdx),%xmm0
	movups	(%rdi),%xmm1
	movups	%xmm0,(%rdi)
	movups	%xmm1,(%rdx)
	leaq	16(%rdx),%rdx
	leaq	-16(%rdi),%rdi

.Ldec_key_inverse:
	movups	(%rdx),%xmm0
	movups	(%rdi),%xmm1
.byte	102,15,56,219,192
.byte	102,15,56,219,201
	leaq	16(%rdx),%rdx
	leaq	-16(%rdi),%rdi
	movups	%xmm0,16(%rdi)
	movups	%xmm1,-16(%rdx)
	cmpq	%rdx,%rdi
	ja	.Ldec_key_inverse

	movups	(%rdx),%xmm0
.byte	102,15,56,219,192
	pxor	%xmm1,%xmm1
	movups	%xmm0,(%rdi)
	pxor	%xmm0,%xmm0
.Ldec_key_ret:
	addq	$8,%rsp
.cfi_adjust_cfa_offset	-8
	.byte	0xf3,0xc3
.cfi_endproc	
.LSEH_end_set_decrypt_key:
.size	aesni_set_decrypt_key,.-aesni_set_decrypt_key
.globl	aesni_set_encrypt_key
.type	aesni_set_encrypt_key,@function
.align	16
aesni_set_encrypt_key:
__aesni_set_encrypt_key:
.cfi_startproc	
.byte	0x48,0x83,0xEC,0x08
.cfi_adjust_cfa_offset	8
	movq	$-1,%rax
	testq	%rdi,%rdi
	jz	.Lenc_key_ret
	testq	%rdx,%rdx
	jz	.Lenc_key_ret

	movl	$268437504,%r10d
	movups	(%rdi),%xmm0
	xorps	%xmm4,%xmm4
	andl	OPENSSL_ia32cap_P+4(%rip),%r10d
	leaq	16(%rdx),%rax
	cmpl	$256,%esi
	je	.L14rounds
	cmpl	$192,%esi
	je	.L12rounds
	cmpl	$128,%esi
	jne	.Lbad_keybits

.L10rounds:
	movl	$9,%esi
	cmpl	$268435456,%r10d
	je	.L10rounds_alt

	movups	%xmm0,(%rdx)
.byte	102,15,58,223,200,1
	call	.Lkey_expansion_128_cold
.byte	102,15,58,223,200,2
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,4
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,8
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,16
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,32
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,64
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,128
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,27
	call	.Lkey_expansion_128
.byte	102,15,58,223,200,54
	call	.Lkey_expansion_128
	movups	%xmm0,(%rax)
	movl	%esi,80(%rax)
	xorl	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L10rounds_alt:
	movdqa	.Lkey_rotate(%rip),%xmm5
	movl	$8,%r10d
	movdqa	.Lkey_rcon1(%rip),%xmm4
	movdqa	%xmm0,%xmm2
	movdqu	%xmm0,(%rdx)
	jmp	.Loop_key128

.align	16
.Loop_key128:
.byte	102,15,56,0,197
.byte	102,15,56,221,196
	pslld	$1,%xmm4
	leaq	16(%rax),%rax

	movdqa	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm3,%xmm2

	pxor	%xmm2,%xmm0
	movdqu	%xmm0,-16(%rax)
	movdqa	%xmm0,%xmm2

	decl	%r10d
	jnz	.Loop_key128

	movdqa	.Lkey_rcon1b(%rip),%xmm4

.byte	102,15,56,0,197
.byte	102,15,56,221,196
	pslld	$1,%xmm4

	movdqa	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm3,%xmm2

	pxor	%xmm2,%xmm0
	movdqu	%xmm0,(%rax)

	movdqa	%xmm0,%xmm2
.byte	102,15,56,0,197
.byte	102,15,56,221,196

	movdqa	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm2,%xmm3
	pslldq	$4,%xmm2
	pxor	%xmm3,%xmm2

	pxor	%xmm2,%xmm0
	movdqu	%xmm0,16(%rax)

	movl	%esi,96(%rax)
	xorl	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L12rounds:
	movq	16(%rdi),%xmm2
	movl	$11,%esi
	cmpl	$268435456,%r10d
	je	.L12rounds_alt

	movups	%xmm0,(%rdx)
.byte	102,15,58,223,202,1
	call	.Lkey_expansion_192a_cold
.byte	102,15,58,223,202,2
	call	.Lkey_expansion_192b
.byte	102,15,58,223,202,4
	call	.Lkey_expansion_192a
.byte	102,15,58,223,202,8
	call	.Lkey_expansion_192b
.byte	102,15,58,223,202,16
	call	.Lkey_expansion_192a
.byte	102,15,58,223,202,32
	call	.Lkey_expansion_192b
.byte	102,15,58,223,202,64
	call	.Lkey_expansion_192a
.byte	102,15,58,223,202,128
	call	.Lkey_expansion_192b
	movups	%xmm0,(%rax)
	movl	%esi,48(%rax)
	xorq	%rax,%rax
	jmp	.Lenc_key_ret

.align	16
.L12rounds_alt:
	movdqa	.Lkey_rotate192(%rip),%xmm5
	movdqa	.Lkey_rcon1(%rip),%xmm4
	movl	$8,%r10d
	movdqu	%xmm0,(%rdx)
	jmp	.Loop_key192

.align	16
.Loop_key192:
	movq	%xmm2,0(%rax)
	movdqa	%xmm2,%xmm1
.byte	102,15,56,0,213
.byte	102,15,56,221,212
	pslld	$1,%xmm4
	leaq	24(%rax),%rax

	movdqa	%xmm0,%xmm3
	pslldq	$4,%xmm0
	pxor	%xmm0,%xmm3
	pslldq	$4,%xmm0
	pxor	%xmm0,%xmm3
	pslldq	$4,%xmm0
	pxor	%xmm3,%xmm0

	pshufd	$0xff,%xmm0,%xmm3
	pxor	%xmm1,%xmm3
	pslldq	$4,%xmm1
	pxor	%xmm1,%xmm3

	pxor	%xmm2,%xmm0
	pxor	%xmm3,%xmm2
	movdqu	%xmm0,-16(%rax)

	decl	%r10d
	jnz	.Loop_key192

	movl	%esi,32(%rax)
	xorl	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L14rounds:
	movups	16(%rdi),%xmm2
	movl	$13,%esi
	leaq	16(%rax),%rax
	cmpl	$268435456,%r10d
	je	.L14rounds_alt

	movups	%xmm0,(%rdx)
	movups	%xmm2,16(%rdx)
.byte	102,15,58,223,202,1
	call	.Lkey_expansion_256a_cold
.byte	102,15,58,223,200,1
	call	.Lkey_expansion_256b
.byte	102,15,58,223,202,2
	call	.Lkey_expansion_256a
.byte	102,15,58,223,200,2
	call	.Lkey_expansion_256b
.byte	102,15,58,223,202,4
	call	.Lkey_expansion_256a
.byte	102,15,58,223,200,4
	call	.Lkey_expansion_256b
.byte	102,15,58,223,202,8
	call	.Lkey_expansion_256a
.byte	102,15,58,223,200,8
	call	.Lkey_expansion_256b
.byte	102,15,58,223,202,16
	call	.Lkey_expansion_256a
.byte	102,15,58,223,200,16
	call	.Lkey_expansion_256b
.byte	102,15,58,223,202,32
	call	.Lkey_expansion_256a
.byte	102,15,58,223,200,32
	call	.Lkey_expansion_256b
.byte	102,15,58,223,202,64
	call	.Lkey_expansion_256a
	movups	%xmm0,(%rax)
	movl	%esi,16(%rax)
	xorq	%rax,%rax
	jmp	.Lenc_key_ret

.align	16
.L14rounds_alt:
	movdqa	.Lkey_rotate(%rip),%xmm5
	movdqa	.Lkey_rcon1(%rip),%xmm4
	movl	$7,%r10d
	movdqu	%xmm0,0(%rdx)
	movdqa	%xmm2,%xmm1
	movdqu	%xmm2,16(%rdx)
	jmp	.Loop_key256

.align	16
.Loop_key256:
.byte	102,15,56,0,213
.byte	102,15,56,221,212

	movdqa	%xmm0,%xmm3
	pslldq	$4,%xmm0
	pxor	%xmm0,%xmm3
	pslldq	$4,%xmm0
	pxor	%xmm0,%xmm3
	pslldq	$4,%xmm0
	pxor	%xmm3,%xmm0
	pslld	$1,%xmm4

	pxor	%xmm2,%xmm0
	movdqu	%xmm0,(%rax)

	decl	%r10d
	jz	.Ldone_key256

	pshufd	$0xff,%xmm0,%xmm2
	pxor	%xmm3,%xmm3
.byte	102,15,56,221,211

	movdqa	%xmm1,%xmm3
	pslldq	$4,%xmm1
	pxor	%xmm1,%xmm3
	pslldq	$4,%xmm1
	pxor	%xmm1,%xmm3
	pslldq	$4,%xmm1
	pxor	%xmm3,%xmm1

	pxor	%xmm1,%xmm2
	movdqu	%xmm2,16(%rax)
	leaq	32(%rax),%rax
	movdqa	%xmm2,%xmm1

	jmp	.Loop_key256

.Ldone_key256:
	movl	%esi,16(%rax)
	xorl	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.Lbad_keybits:
	movq	$-2,%rax
.Lenc_key_ret:
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	addq	$8,%rsp
.cfi_adjust_cfa_offset	-8
	.byte	0xf3,0xc3
.cfi_endproc	
.LSEH_end_set_encrypt_key:

.align	16
.Lkey_expansion_128:
	movups	%xmm0,(%rax)
	leaq	16(%rax),%rax
.Lkey_expansion_128_cold:
	shufps	$16,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	$140,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	$255,%xmm1,%xmm1
	xorps	%xmm1,%xmm0
	.byte	0xf3,0xc3

.align	16
.Lkey_expansion_192a:
	movups	%xmm0,(%rax)
	leaq	16(%rax),%rax
.Lkey_expansion_192a_cold:
	movaps	%xmm2,%xmm5
.Lkey_expansion_192b_warm:
	shufps	$16,%xmm0,%xmm4
	movdqa	%xmm2,%xmm3
	xorps	%xmm4,%xmm0
	shufps	$140,%xmm0,%xmm4
	pslldq	$4,%xmm3
	xorps	%xmm4,%xmm0
	pshufd	$85,%xmm1,%xmm1
	pxor	%xmm3,%xmm2
	pxor	%xmm1,%xmm0
	pshufd	$255,%xmm0,%xmm3
	pxor	%xmm3,%xmm2
	.byte	0xf3,0xc3

.align	16
.Lkey_expansion_192b:
	movaps	%xmm0,%xmm3
	shufps	$68,%xmm0,%xmm5
	movups	%xmm5,(%rax)
	shufps	$78,%xmm2,%xmm3
	movups	%xmm3,16(%rax)
	leaq	32(%rax),%rax
	jmp	.Lkey_expansion_192b_warm

.align	16
.Lkey_expansion_256a:
	movups	%xmm2,(%rax)
	leaq	16(%rax),%rax
.Lkey_expansion_256a_cold:
	shufps	$16,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	$140,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	$255,%xmm1,%xmm1
	xorps	%xmm1,%xmm0
	.byte	0xf3,0xc3

.align	16
.Lkey_expansion_256b:
	movups	%xmm0,(%rax)
	leaq	16(%rax),%rax

	shufps	$16,%xmm2,%xmm4
	xorps	%xmm4,%xmm2
	shufps	$140,%xmm2,%xmm4
	xorps	%xmm4,%xmm2
	shufps	$170,%xmm1,%xmm1
	xorps	%xmm1,%xmm2
	.byte	0xf3,0xc3
.size	aesni_set_encrypt_key,.-aesni_set_encrypt_key
.size	__aesni_set_encrypt_key,.-__aesni_set_encrypt_key
.align	64
.Lbswap_mask:
.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.Lincrement32:
.long	6,6,6,0
.Lincrement64:
.long	1,0,0,0
.Lxts_magic:
.long	0x87,0,1,0
.Lincrement1:
.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
.Lkey_rotate:
.long	0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d
.Lkey_rotate192:
.long	0x04070605,0x04070605,0x04070605,0x04070605
.Lkey_rcon1:
.long	1,1,1,1
.Lkey_rcon1b:
.long	0x1b,0x1b,0x1b,0x1b

.byte	65,69,83,32,102,111,114,32,73,110,116,101,108,32,65,69,83,45,78,73,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	64
