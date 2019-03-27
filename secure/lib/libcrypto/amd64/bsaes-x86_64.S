/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from bsaes-x86_64.pl. */
.text	




.type	_bsaes_encrypt8,@function
.align	64
_bsaes_encrypt8:
.cfi_startproc	
	leaq	.LBS0(%rip),%r11

	movdqa	(%rax),%xmm8
	leaq	16(%rax),%rax
	movdqa	80(%r11),%xmm7
	pxor	%xmm8,%xmm15
	pxor	%xmm8,%xmm0
	pxor	%xmm8,%xmm1
	pxor	%xmm8,%xmm2
.byte	102,68,15,56,0,255
.byte	102,15,56,0,199
	pxor	%xmm8,%xmm3
	pxor	%xmm8,%xmm4
.byte	102,15,56,0,207
.byte	102,15,56,0,215
	pxor	%xmm8,%xmm5
	pxor	%xmm8,%xmm6
.byte	102,15,56,0,223
.byte	102,15,56,0,231
.byte	102,15,56,0,239
.byte	102,15,56,0,247
_bsaes_encrypt8_bitslice:
	movdqa	0(%r11),%xmm7
	movdqa	16(%r11),%xmm8
	movdqa	%xmm5,%xmm9
	psrlq	$1,%xmm5
	movdqa	%xmm3,%xmm10
	psrlq	$1,%xmm3
	pxor	%xmm6,%xmm5
	pxor	%xmm4,%xmm3
	pand	%xmm7,%xmm5
	pand	%xmm7,%xmm3
	pxor	%xmm5,%xmm6
	psllq	$1,%xmm5
	pxor	%xmm3,%xmm4
	psllq	$1,%xmm3
	pxor	%xmm9,%xmm5
	pxor	%xmm10,%xmm3
	movdqa	%xmm1,%xmm9
	psrlq	$1,%xmm1
	movdqa	%xmm15,%xmm10
	psrlq	$1,%xmm15
	pxor	%xmm2,%xmm1
	pxor	%xmm0,%xmm15
	pand	%xmm7,%xmm1
	pand	%xmm7,%xmm15
	pxor	%xmm1,%xmm2
	psllq	$1,%xmm1
	pxor	%xmm15,%xmm0
	psllq	$1,%xmm15
	pxor	%xmm9,%xmm1
	pxor	%xmm10,%xmm15
	movdqa	32(%r11),%xmm7
	movdqa	%xmm4,%xmm9
	psrlq	$2,%xmm4
	movdqa	%xmm3,%xmm10
	psrlq	$2,%xmm3
	pxor	%xmm6,%xmm4
	pxor	%xmm5,%xmm3
	pand	%xmm8,%xmm4
	pand	%xmm8,%xmm3
	pxor	%xmm4,%xmm6
	psllq	$2,%xmm4
	pxor	%xmm3,%xmm5
	psllq	$2,%xmm3
	pxor	%xmm9,%xmm4
	pxor	%xmm10,%xmm3
	movdqa	%xmm0,%xmm9
	psrlq	$2,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$2,%xmm15
	pxor	%xmm2,%xmm0
	pxor	%xmm1,%xmm15
	pand	%xmm8,%xmm0
	pand	%xmm8,%xmm15
	pxor	%xmm0,%xmm2
	psllq	$2,%xmm0
	pxor	%xmm15,%xmm1
	psllq	$2,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	movdqa	%xmm2,%xmm9
	psrlq	$4,%xmm2
	movdqa	%xmm1,%xmm10
	psrlq	$4,%xmm1
	pxor	%xmm6,%xmm2
	pxor	%xmm5,%xmm1
	pand	%xmm7,%xmm2
	pand	%xmm7,%xmm1
	pxor	%xmm2,%xmm6
	psllq	$4,%xmm2
	pxor	%xmm1,%xmm5
	psllq	$4,%xmm1
	pxor	%xmm9,%xmm2
	pxor	%xmm10,%xmm1
	movdqa	%xmm0,%xmm9
	psrlq	$4,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$4,%xmm15
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm15
	pand	%xmm7,%xmm0
	pand	%xmm7,%xmm15
	pxor	%xmm0,%xmm4
	psllq	$4,%xmm0
	pxor	%xmm15,%xmm3
	psllq	$4,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	decl	%r10d
	jmp	.Lenc_sbox
.align	16
.Lenc_loop:
	pxor	0(%rax),%xmm15
	pxor	16(%rax),%xmm0
	pxor	32(%rax),%xmm1
	pxor	48(%rax),%xmm2
.byte	102,68,15,56,0,255
.byte	102,15,56,0,199
	pxor	64(%rax),%xmm3
	pxor	80(%rax),%xmm4
.byte	102,15,56,0,207
.byte	102,15,56,0,215
	pxor	96(%rax),%xmm5
	pxor	112(%rax),%xmm6
.byte	102,15,56,0,223
.byte	102,15,56,0,231
.byte	102,15,56,0,239
.byte	102,15,56,0,247
	leaq	128(%rax),%rax
.Lenc_sbox:
	pxor	%xmm5,%xmm4
	pxor	%xmm0,%xmm1
	pxor	%xmm15,%xmm2
	pxor	%xmm1,%xmm5
	pxor	%xmm15,%xmm4

	pxor	%xmm2,%xmm5
	pxor	%xmm6,%xmm2
	pxor	%xmm4,%xmm6
	pxor	%xmm3,%xmm2
	pxor	%xmm4,%xmm3
	pxor	%xmm0,%xmm2

	pxor	%xmm6,%xmm1
	pxor	%xmm4,%xmm0
	movdqa	%xmm6,%xmm10
	movdqa	%xmm0,%xmm9
	movdqa	%xmm4,%xmm8
	movdqa	%xmm1,%xmm12
	movdqa	%xmm5,%xmm11

	pxor	%xmm3,%xmm10
	pxor	%xmm1,%xmm9
	pxor	%xmm2,%xmm8
	movdqa	%xmm10,%xmm13
	pxor	%xmm3,%xmm12
	movdqa	%xmm9,%xmm7
	pxor	%xmm15,%xmm11
	movdqa	%xmm10,%xmm14

	por	%xmm8,%xmm9
	por	%xmm11,%xmm10
	pxor	%xmm7,%xmm14
	pand	%xmm11,%xmm13
	pxor	%xmm8,%xmm11
	pand	%xmm8,%xmm7
	pand	%xmm11,%xmm14
	movdqa	%xmm2,%xmm11
	pxor	%xmm15,%xmm11
	pand	%xmm11,%xmm12
	pxor	%xmm12,%xmm10
	pxor	%xmm12,%xmm9
	movdqa	%xmm6,%xmm12
	movdqa	%xmm4,%xmm11
	pxor	%xmm0,%xmm12
	pxor	%xmm5,%xmm11
	movdqa	%xmm12,%xmm8
	pand	%xmm11,%xmm12
	por	%xmm11,%xmm8
	pxor	%xmm12,%xmm7
	pxor	%xmm14,%xmm10
	pxor	%xmm13,%xmm9
	pxor	%xmm14,%xmm8
	movdqa	%xmm1,%xmm11
	pxor	%xmm13,%xmm7
	movdqa	%xmm3,%xmm12
	pxor	%xmm13,%xmm8
	movdqa	%xmm0,%xmm13
	pand	%xmm2,%xmm11
	movdqa	%xmm6,%xmm14
	pand	%xmm15,%xmm12
	pand	%xmm4,%xmm13
	por	%xmm5,%xmm14
	pxor	%xmm11,%xmm10
	pxor	%xmm12,%xmm9
	pxor	%xmm13,%xmm8
	pxor	%xmm14,%xmm7





	movdqa	%xmm10,%xmm11
	pand	%xmm8,%xmm10
	pxor	%xmm9,%xmm11

	movdqa	%xmm7,%xmm13
	movdqa	%xmm11,%xmm14
	pxor	%xmm10,%xmm13
	pand	%xmm13,%xmm14

	movdqa	%xmm8,%xmm12
	pxor	%xmm9,%xmm14
	pxor	%xmm7,%xmm12

	pxor	%xmm9,%xmm10

	pand	%xmm10,%xmm12

	movdqa	%xmm13,%xmm9
	pxor	%xmm7,%xmm12

	pxor	%xmm12,%xmm9
	pxor	%xmm12,%xmm8

	pand	%xmm7,%xmm9

	pxor	%xmm9,%xmm13
	pxor	%xmm9,%xmm8

	pand	%xmm14,%xmm13

	pxor	%xmm11,%xmm13
	movdqa	%xmm5,%xmm11
	movdqa	%xmm4,%xmm7
	movdqa	%xmm14,%xmm9
	pxor	%xmm13,%xmm9
	pand	%xmm5,%xmm9
	pxor	%xmm4,%xmm5
	pand	%xmm14,%xmm4
	pand	%xmm13,%xmm5
	pxor	%xmm4,%xmm5
	pxor	%xmm9,%xmm4
	pxor	%xmm15,%xmm11
	pxor	%xmm2,%xmm7
	pxor	%xmm12,%xmm14
	pxor	%xmm8,%xmm13
	movdqa	%xmm14,%xmm10
	movdqa	%xmm12,%xmm9
	pxor	%xmm13,%xmm10
	pxor	%xmm8,%xmm9
	pand	%xmm11,%xmm10
	pand	%xmm15,%xmm9
	pxor	%xmm7,%xmm11
	pxor	%xmm2,%xmm15
	pand	%xmm14,%xmm7
	pand	%xmm12,%xmm2
	pand	%xmm13,%xmm11
	pand	%xmm8,%xmm15
	pxor	%xmm11,%xmm7
	pxor	%xmm2,%xmm15
	pxor	%xmm10,%xmm11
	pxor	%xmm9,%xmm2
	pxor	%xmm11,%xmm5
	pxor	%xmm11,%xmm15
	pxor	%xmm7,%xmm4
	pxor	%xmm7,%xmm2

	movdqa	%xmm6,%xmm11
	movdqa	%xmm0,%xmm7
	pxor	%xmm3,%xmm11
	pxor	%xmm1,%xmm7
	movdqa	%xmm14,%xmm10
	movdqa	%xmm12,%xmm9
	pxor	%xmm13,%xmm10
	pxor	%xmm8,%xmm9
	pand	%xmm11,%xmm10
	pand	%xmm3,%xmm9
	pxor	%xmm7,%xmm11
	pxor	%xmm1,%xmm3
	pand	%xmm14,%xmm7
	pand	%xmm12,%xmm1
	pand	%xmm13,%xmm11
	pand	%xmm8,%xmm3
	pxor	%xmm11,%xmm7
	pxor	%xmm1,%xmm3
	pxor	%xmm10,%xmm11
	pxor	%xmm9,%xmm1
	pxor	%xmm12,%xmm14
	pxor	%xmm8,%xmm13
	movdqa	%xmm14,%xmm10
	pxor	%xmm13,%xmm10
	pand	%xmm6,%xmm10
	pxor	%xmm0,%xmm6
	pand	%xmm14,%xmm0
	pand	%xmm13,%xmm6
	pxor	%xmm0,%xmm6
	pxor	%xmm10,%xmm0
	pxor	%xmm11,%xmm6
	pxor	%xmm11,%xmm3
	pxor	%xmm7,%xmm0
	pxor	%xmm7,%xmm1
	pxor	%xmm15,%xmm6
	pxor	%xmm5,%xmm0
	pxor	%xmm6,%xmm3
	pxor	%xmm15,%xmm5
	pxor	%xmm0,%xmm15

	pxor	%xmm4,%xmm0
	pxor	%xmm1,%xmm4
	pxor	%xmm2,%xmm1
	pxor	%xmm4,%xmm2
	pxor	%xmm4,%xmm3

	pxor	%xmm2,%xmm5
	decl	%r10d
	jl	.Lenc_done
	pshufd	$0x93,%xmm15,%xmm7
	pshufd	$0x93,%xmm0,%xmm8
	pxor	%xmm7,%xmm15
	pshufd	$0x93,%xmm3,%xmm9
	pxor	%xmm8,%xmm0
	pshufd	$0x93,%xmm5,%xmm10
	pxor	%xmm9,%xmm3
	pshufd	$0x93,%xmm2,%xmm11
	pxor	%xmm10,%xmm5
	pshufd	$0x93,%xmm6,%xmm12
	pxor	%xmm11,%xmm2
	pshufd	$0x93,%xmm1,%xmm13
	pxor	%xmm12,%xmm6
	pshufd	$0x93,%xmm4,%xmm14
	pxor	%xmm13,%xmm1
	pxor	%xmm14,%xmm4

	pxor	%xmm15,%xmm8
	pxor	%xmm4,%xmm7
	pxor	%xmm4,%xmm8
	pshufd	$0x4E,%xmm15,%xmm15
	pxor	%xmm0,%xmm9
	pshufd	$0x4E,%xmm0,%xmm0
	pxor	%xmm2,%xmm12
	pxor	%xmm7,%xmm15
	pxor	%xmm6,%xmm13
	pxor	%xmm8,%xmm0
	pxor	%xmm5,%xmm11
	pshufd	$0x4E,%xmm2,%xmm7
	pxor	%xmm1,%xmm14
	pshufd	$0x4E,%xmm6,%xmm8
	pxor	%xmm3,%xmm10
	pshufd	$0x4E,%xmm5,%xmm2
	pxor	%xmm4,%xmm10
	pshufd	$0x4E,%xmm4,%xmm6
	pxor	%xmm4,%xmm11
	pshufd	$0x4E,%xmm1,%xmm5
	pxor	%xmm11,%xmm7
	pshufd	$0x4E,%xmm3,%xmm1
	pxor	%xmm12,%xmm8
	pxor	%xmm10,%xmm2
	pxor	%xmm14,%xmm6
	pxor	%xmm13,%xmm5
	movdqa	%xmm7,%xmm3
	pxor	%xmm9,%xmm1
	movdqa	%xmm8,%xmm4
	movdqa	48(%r11),%xmm7
	jnz	.Lenc_loop
	movdqa	64(%r11),%xmm7
	jmp	.Lenc_loop
.align	16
.Lenc_done:
	movdqa	0(%r11),%xmm7
	movdqa	16(%r11),%xmm8
	movdqa	%xmm1,%xmm9
	psrlq	$1,%xmm1
	movdqa	%xmm2,%xmm10
	psrlq	$1,%xmm2
	pxor	%xmm4,%xmm1
	pxor	%xmm6,%xmm2
	pand	%xmm7,%xmm1
	pand	%xmm7,%xmm2
	pxor	%xmm1,%xmm4
	psllq	$1,%xmm1
	pxor	%xmm2,%xmm6
	psllq	$1,%xmm2
	pxor	%xmm9,%xmm1
	pxor	%xmm10,%xmm2
	movdqa	%xmm3,%xmm9
	psrlq	$1,%xmm3
	movdqa	%xmm15,%xmm10
	psrlq	$1,%xmm15
	pxor	%xmm5,%xmm3
	pxor	%xmm0,%xmm15
	pand	%xmm7,%xmm3
	pand	%xmm7,%xmm15
	pxor	%xmm3,%xmm5
	psllq	$1,%xmm3
	pxor	%xmm15,%xmm0
	psllq	$1,%xmm15
	pxor	%xmm9,%xmm3
	pxor	%xmm10,%xmm15
	movdqa	32(%r11),%xmm7
	movdqa	%xmm6,%xmm9
	psrlq	$2,%xmm6
	movdqa	%xmm2,%xmm10
	psrlq	$2,%xmm2
	pxor	%xmm4,%xmm6
	pxor	%xmm1,%xmm2
	pand	%xmm8,%xmm6
	pand	%xmm8,%xmm2
	pxor	%xmm6,%xmm4
	psllq	$2,%xmm6
	pxor	%xmm2,%xmm1
	psllq	$2,%xmm2
	pxor	%xmm9,%xmm6
	pxor	%xmm10,%xmm2
	movdqa	%xmm0,%xmm9
	psrlq	$2,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$2,%xmm15
	pxor	%xmm5,%xmm0
	pxor	%xmm3,%xmm15
	pand	%xmm8,%xmm0
	pand	%xmm8,%xmm15
	pxor	%xmm0,%xmm5
	psllq	$2,%xmm0
	pxor	%xmm15,%xmm3
	psllq	$2,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	movdqa	%xmm5,%xmm9
	psrlq	$4,%xmm5
	movdqa	%xmm3,%xmm10
	psrlq	$4,%xmm3
	pxor	%xmm4,%xmm5
	pxor	%xmm1,%xmm3
	pand	%xmm7,%xmm5
	pand	%xmm7,%xmm3
	pxor	%xmm5,%xmm4
	psllq	$4,%xmm5
	pxor	%xmm3,%xmm1
	psllq	$4,%xmm3
	pxor	%xmm9,%xmm5
	pxor	%xmm10,%xmm3
	movdqa	%xmm0,%xmm9
	psrlq	$4,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$4,%xmm15
	pxor	%xmm6,%xmm0
	pxor	%xmm2,%xmm15
	pand	%xmm7,%xmm0
	pand	%xmm7,%xmm15
	pxor	%xmm0,%xmm6
	psllq	$4,%xmm0
	pxor	%xmm15,%xmm2
	psllq	$4,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	movdqa	(%rax),%xmm7
	pxor	%xmm7,%xmm3
	pxor	%xmm7,%xmm5
	pxor	%xmm7,%xmm2
	pxor	%xmm7,%xmm6
	pxor	%xmm7,%xmm1
	pxor	%xmm7,%xmm4
	pxor	%xmm7,%xmm15
	pxor	%xmm7,%xmm0
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_bsaes_encrypt8,.-_bsaes_encrypt8

.type	_bsaes_decrypt8,@function
.align	64
_bsaes_decrypt8:
.cfi_startproc	
	leaq	.LBS0(%rip),%r11

	movdqa	(%rax),%xmm8
	leaq	16(%rax),%rax
	movdqa	-48(%r11),%xmm7
	pxor	%xmm8,%xmm15
	pxor	%xmm8,%xmm0
	pxor	%xmm8,%xmm1
	pxor	%xmm8,%xmm2
.byte	102,68,15,56,0,255
.byte	102,15,56,0,199
	pxor	%xmm8,%xmm3
	pxor	%xmm8,%xmm4
.byte	102,15,56,0,207
.byte	102,15,56,0,215
	pxor	%xmm8,%xmm5
	pxor	%xmm8,%xmm6
.byte	102,15,56,0,223
.byte	102,15,56,0,231
.byte	102,15,56,0,239
.byte	102,15,56,0,247
	movdqa	0(%r11),%xmm7
	movdqa	16(%r11),%xmm8
	movdqa	%xmm5,%xmm9
	psrlq	$1,%xmm5
	movdqa	%xmm3,%xmm10
	psrlq	$1,%xmm3
	pxor	%xmm6,%xmm5
	pxor	%xmm4,%xmm3
	pand	%xmm7,%xmm5
	pand	%xmm7,%xmm3
	pxor	%xmm5,%xmm6
	psllq	$1,%xmm5
	pxor	%xmm3,%xmm4
	psllq	$1,%xmm3
	pxor	%xmm9,%xmm5
	pxor	%xmm10,%xmm3
	movdqa	%xmm1,%xmm9
	psrlq	$1,%xmm1
	movdqa	%xmm15,%xmm10
	psrlq	$1,%xmm15
	pxor	%xmm2,%xmm1
	pxor	%xmm0,%xmm15
	pand	%xmm7,%xmm1
	pand	%xmm7,%xmm15
	pxor	%xmm1,%xmm2
	psllq	$1,%xmm1
	pxor	%xmm15,%xmm0
	psllq	$1,%xmm15
	pxor	%xmm9,%xmm1
	pxor	%xmm10,%xmm15
	movdqa	32(%r11),%xmm7
	movdqa	%xmm4,%xmm9
	psrlq	$2,%xmm4
	movdqa	%xmm3,%xmm10
	psrlq	$2,%xmm3
	pxor	%xmm6,%xmm4
	pxor	%xmm5,%xmm3
	pand	%xmm8,%xmm4
	pand	%xmm8,%xmm3
	pxor	%xmm4,%xmm6
	psllq	$2,%xmm4
	pxor	%xmm3,%xmm5
	psllq	$2,%xmm3
	pxor	%xmm9,%xmm4
	pxor	%xmm10,%xmm3
	movdqa	%xmm0,%xmm9
	psrlq	$2,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$2,%xmm15
	pxor	%xmm2,%xmm0
	pxor	%xmm1,%xmm15
	pand	%xmm8,%xmm0
	pand	%xmm8,%xmm15
	pxor	%xmm0,%xmm2
	psllq	$2,%xmm0
	pxor	%xmm15,%xmm1
	psllq	$2,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	movdqa	%xmm2,%xmm9
	psrlq	$4,%xmm2
	movdqa	%xmm1,%xmm10
	psrlq	$4,%xmm1
	pxor	%xmm6,%xmm2
	pxor	%xmm5,%xmm1
	pand	%xmm7,%xmm2
	pand	%xmm7,%xmm1
	pxor	%xmm2,%xmm6
	psllq	$4,%xmm2
	pxor	%xmm1,%xmm5
	psllq	$4,%xmm1
	pxor	%xmm9,%xmm2
	pxor	%xmm10,%xmm1
	movdqa	%xmm0,%xmm9
	psrlq	$4,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$4,%xmm15
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm15
	pand	%xmm7,%xmm0
	pand	%xmm7,%xmm15
	pxor	%xmm0,%xmm4
	psllq	$4,%xmm0
	pxor	%xmm15,%xmm3
	psllq	$4,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	decl	%r10d
	jmp	.Ldec_sbox
.align	16
.Ldec_loop:
	pxor	0(%rax),%xmm15
	pxor	16(%rax),%xmm0
	pxor	32(%rax),%xmm1
	pxor	48(%rax),%xmm2
.byte	102,68,15,56,0,255
.byte	102,15,56,0,199
	pxor	64(%rax),%xmm3
	pxor	80(%rax),%xmm4
.byte	102,15,56,0,207
.byte	102,15,56,0,215
	pxor	96(%rax),%xmm5
	pxor	112(%rax),%xmm6
.byte	102,15,56,0,223
.byte	102,15,56,0,231
.byte	102,15,56,0,239
.byte	102,15,56,0,247
	leaq	128(%rax),%rax
.Ldec_sbox:
	pxor	%xmm3,%xmm2

	pxor	%xmm6,%xmm3
	pxor	%xmm6,%xmm1
	pxor	%xmm3,%xmm5
	pxor	%xmm5,%xmm6
	pxor	%xmm6,%xmm0

	pxor	%xmm0,%xmm15
	pxor	%xmm4,%xmm1
	pxor	%xmm15,%xmm2
	pxor	%xmm15,%xmm4
	pxor	%xmm2,%xmm0
	movdqa	%xmm2,%xmm10
	movdqa	%xmm6,%xmm9
	movdqa	%xmm0,%xmm8
	movdqa	%xmm3,%xmm12
	movdqa	%xmm4,%xmm11

	pxor	%xmm15,%xmm10
	pxor	%xmm3,%xmm9
	pxor	%xmm5,%xmm8
	movdqa	%xmm10,%xmm13
	pxor	%xmm15,%xmm12
	movdqa	%xmm9,%xmm7
	pxor	%xmm1,%xmm11
	movdqa	%xmm10,%xmm14

	por	%xmm8,%xmm9
	por	%xmm11,%xmm10
	pxor	%xmm7,%xmm14
	pand	%xmm11,%xmm13
	pxor	%xmm8,%xmm11
	pand	%xmm8,%xmm7
	pand	%xmm11,%xmm14
	movdqa	%xmm5,%xmm11
	pxor	%xmm1,%xmm11
	pand	%xmm11,%xmm12
	pxor	%xmm12,%xmm10
	pxor	%xmm12,%xmm9
	movdqa	%xmm2,%xmm12
	movdqa	%xmm0,%xmm11
	pxor	%xmm6,%xmm12
	pxor	%xmm4,%xmm11
	movdqa	%xmm12,%xmm8
	pand	%xmm11,%xmm12
	por	%xmm11,%xmm8
	pxor	%xmm12,%xmm7
	pxor	%xmm14,%xmm10
	pxor	%xmm13,%xmm9
	pxor	%xmm14,%xmm8
	movdqa	%xmm3,%xmm11
	pxor	%xmm13,%xmm7
	movdqa	%xmm15,%xmm12
	pxor	%xmm13,%xmm8
	movdqa	%xmm6,%xmm13
	pand	%xmm5,%xmm11
	movdqa	%xmm2,%xmm14
	pand	%xmm1,%xmm12
	pand	%xmm0,%xmm13
	por	%xmm4,%xmm14
	pxor	%xmm11,%xmm10
	pxor	%xmm12,%xmm9
	pxor	%xmm13,%xmm8
	pxor	%xmm14,%xmm7





	movdqa	%xmm10,%xmm11
	pand	%xmm8,%xmm10
	pxor	%xmm9,%xmm11

	movdqa	%xmm7,%xmm13
	movdqa	%xmm11,%xmm14
	pxor	%xmm10,%xmm13
	pand	%xmm13,%xmm14

	movdqa	%xmm8,%xmm12
	pxor	%xmm9,%xmm14
	pxor	%xmm7,%xmm12

	pxor	%xmm9,%xmm10

	pand	%xmm10,%xmm12

	movdqa	%xmm13,%xmm9
	pxor	%xmm7,%xmm12

	pxor	%xmm12,%xmm9
	pxor	%xmm12,%xmm8

	pand	%xmm7,%xmm9

	pxor	%xmm9,%xmm13
	pxor	%xmm9,%xmm8

	pand	%xmm14,%xmm13

	pxor	%xmm11,%xmm13
	movdqa	%xmm4,%xmm11
	movdqa	%xmm0,%xmm7
	movdqa	%xmm14,%xmm9
	pxor	%xmm13,%xmm9
	pand	%xmm4,%xmm9
	pxor	%xmm0,%xmm4
	pand	%xmm14,%xmm0
	pand	%xmm13,%xmm4
	pxor	%xmm0,%xmm4
	pxor	%xmm9,%xmm0
	pxor	%xmm1,%xmm11
	pxor	%xmm5,%xmm7
	pxor	%xmm12,%xmm14
	pxor	%xmm8,%xmm13
	movdqa	%xmm14,%xmm10
	movdqa	%xmm12,%xmm9
	pxor	%xmm13,%xmm10
	pxor	%xmm8,%xmm9
	pand	%xmm11,%xmm10
	pand	%xmm1,%xmm9
	pxor	%xmm7,%xmm11
	pxor	%xmm5,%xmm1
	pand	%xmm14,%xmm7
	pand	%xmm12,%xmm5
	pand	%xmm13,%xmm11
	pand	%xmm8,%xmm1
	pxor	%xmm11,%xmm7
	pxor	%xmm5,%xmm1
	pxor	%xmm10,%xmm11
	pxor	%xmm9,%xmm5
	pxor	%xmm11,%xmm4
	pxor	%xmm11,%xmm1
	pxor	%xmm7,%xmm0
	pxor	%xmm7,%xmm5

	movdqa	%xmm2,%xmm11
	movdqa	%xmm6,%xmm7
	pxor	%xmm15,%xmm11
	pxor	%xmm3,%xmm7
	movdqa	%xmm14,%xmm10
	movdqa	%xmm12,%xmm9
	pxor	%xmm13,%xmm10
	pxor	%xmm8,%xmm9
	pand	%xmm11,%xmm10
	pand	%xmm15,%xmm9
	pxor	%xmm7,%xmm11
	pxor	%xmm3,%xmm15
	pand	%xmm14,%xmm7
	pand	%xmm12,%xmm3
	pand	%xmm13,%xmm11
	pand	%xmm8,%xmm15
	pxor	%xmm11,%xmm7
	pxor	%xmm3,%xmm15
	pxor	%xmm10,%xmm11
	pxor	%xmm9,%xmm3
	pxor	%xmm12,%xmm14
	pxor	%xmm8,%xmm13
	movdqa	%xmm14,%xmm10
	pxor	%xmm13,%xmm10
	pand	%xmm2,%xmm10
	pxor	%xmm6,%xmm2
	pand	%xmm14,%xmm6
	pand	%xmm13,%xmm2
	pxor	%xmm6,%xmm2
	pxor	%xmm10,%xmm6
	pxor	%xmm11,%xmm2
	pxor	%xmm11,%xmm15
	pxor	%xmm7,%xmm6
	pxor	%xmm7,%xmm3
	pxor	%xmm6,%xmm0
	pxor	%xmm4,%xmm5

	pxor	%xmm0,%xmm3
	pxor	%xmm6,%xmm1
	pxor	%xmm6,%xmm4
	pxor	%xmm1,%xmm3
	pxor	%xmm15,%xmm6
	pxor	%xmm4,%xmm3
	pxor	%xmm5,%xmm2
	pxor	%xmm0,%xmm5
	pxor	%xmm3,%xmm2

	pxor	%xmm15,%xmm3
	pxor	%xmm2,%xmm6
	decl	%r10d
	jl	.Ldec_done

	pshufd	$0x4E,%xmm15,%xmm7
	pshufd	$0x4E,%xmm2,%xmm13
	pxor	%xmm15,%xmm7
	pshufd	$0x4E,%xmm4,%xmm14
	pxor	%xmm2,%xmm13
	pshufd	$0x4E,%xmm0,%xmm8
	pxor	%xmm4,%xmm14
	pshufd	$0x4E,%xmm5,%xmm9
	pxor	%xmm0,%xmm8
	pshufd	$0x4E,%xmm3,%xmm10
	pxor	%xmm5,%xmm9
	pxor	%xmm13,%xmm15
	pxor	%xmm13,%xmm0
	pshufd	$0x4E,%xmm1,%xmm11
	pxor	%xmm3,%xmm10
	pxor	%xmm7,%xmm5
	pxor	%xmm8,%xmm3
	pshufd	$0x4E,%xmm6,%xmm12
	pxor	%xmm1,%xmm11
	pxor	%xmm14,%xmm0
	pxor	%xmm9,%xmm1
	pxor	%xmm6,%xmm12

	pxor	%xmm14,%xmm5
	pxor	%xmm13,%xmm3
	pxor	%xmm13,%xmm1
	pxor	%xmm10,%xmm6
	pxor	%xmm11,%xmm2
	pxor	%xmm14,%xmm1
	pxor	%xmm14,%xmm6
	pxor	%xmm12,%xmm4
	pshufd	$0x93,%xmm15,%xmm7
	pshufd	$0x93,%xmm0,%xmm8
	pxor	%xmm7,%xmm15
	pshufd	$0x93,%xmm5,%xmm9
	pxor	%xmm8,%xmm0
	pshufd	$0x93,%xmm3,%xmm10
	pxor	%xmm9,%xmm5
	pshufd	$0x93,%xmm1,%xmm11
	pxor	%xmm10,%xmm3
	pshufd	$0x93,%xmm6,%xmm12
	pxor	%xmm11,%xmm1
	pshufd	$0x93,%xmm2,%xmm13
	pxor	%xmm12,%xmm6
	pshufd	$0x93,%xmm4,%xmm14
	pxor	%xmm13,%xmm2
	pxor	%xmm14,%xmm4

	pxor	%xmm15,%xmm8
	pxor	%xmm4,%xmm7
	pxor	%xmm4,%xmm8
	pshufd	$0x4E,%xmm15,%xmm15
	pxor	%xmm0,%xmm9
	pshufd	$0x4E,%xmm0,%xmm0
	pxor	%xmm1,%xmm12
	pxor	%xmm7,%xmm15
	pxor	%xmm6,%xmm13
	pxor	%xmm8,%xmm0
	pxor	%xmm3,%xmm11
	pshufd	$0x4E,%xmm1,%xmm7
	pxor	%xmm2,%xmm14
	pshufd	$0x4E,%xmm6,%xmm8
	pxor	%xmm5,%xmm10
	pshufd	$0x4E,%xmm3,%xmm1
	pxor	%xmm4,%xmm10
	pshufd	$0x4E,%xmm4,%xmm6
	pxor	%xmm4,%xmm11
	pshufd	$0x4E,%xmm2,%xmm3
	pxor	%xmm11,%xmm7
	pshufd	$0x4E,%xmm5,%xmm2
	pxor	%xmm12,%xmm8
	pxor	%xmm1,%xmm10
	pxor	%xmm14,%xmm6
	pxor	%xmm3,%xmm13
	movdqa	%xmm7,%xmm3
	pxor	%xmm9,%xmm2
	movdqa	%xmm13,%xmm5
	movdqa	%xmm8,%xmm4
	movdqa	%xmm2,%xmm1
	movdqa	%xmm10,%xmm2
	movdqa	-16(%r11),%xmm7
	jnz	.Ldec_loop
	movdqa	-32(%r11),%xmm7
	jmp	.Ldec_loop
.align	16
.Ldec_done:
	movdqa	0(%r11),%xmm7
	movdqa	16(%r11),%xmm8
	movdqa	%xmm2,%xmm9
	psrlq	$1,%xmm2
	movdqa	%xmm1,%xmm10
	psrlq	$1,%xmm1
	pxor	%xmm4,%xmm2
	pxor	%xmm6,%xmm1
	pand	%xmm7,%xmm2
	pand	%xmm7,%xmm1
	pxor	%xmm2,%xmm4
	psllq	$1,%xmm2
	pxor	%xmm1,%xmm6
	psllq	$1,%xmm1
	pxor	%xmm9,%xmm2
	pxor	%xmm10,%xmm1
	movdqa	%xmm5,%xmm9
	psrlq	$1,%xmm5
	movdqa	%xmm15,%xmm10
	psrlq	$1,%xmm15
	pxor	%xmm3,%xmm5
	pxor	%xmm0,%xmm15
	pand	%xmm7,%xmm5
	pand	%xmm7,%xmm15
	pxor	%xmm5,%xmm3
	psllq	$1,%xmm5
	pxor	%xmm15,%xmm0
	psllq	$1,%xmm15
	pxor	%xmm9,%xmm5
	pxor	%xmm10,%xmm15
	movdqa	32(%r11),%xmm7
	movdqa	%xmm6,%xmm9
	psrlq	$2,%xmm6
	movdqa	%xmm1,%xmm10
	psrlq	$2,%xmm1
	pxor	%xmm4,%xmm6
	pxor	%xmm2,%xmm1
	pand	%xmm8,%xmm6
	pand	%xmm8,%xmm1
	pxor	%xmm6,%xmm4
	psllq	$2,%xmm6
	pxor	%xmm1,%xmm2
	psllq	$2,%xmm1
	pxor	%xmm9,%xmm6
	pxor	%xmm10,%xmm1
	movdqa	%xmm0,%xmm9
	psrlq	$2,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$2,%xmm15
	pxor	%xmm3,%xmm0
	pxor	%xmm5,%xmm15
	pand	%xmm8,%xmm0
	pand	%xmm8,%xmm15
	pxor	%xmm0,%xmm3
	psllq	$2,%xmm0
	pxor	%xmm15,%xmm5
	psllq	$2,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	movdqa	%xmm3,%xmm9
	psrlq	$4,%xmm3
	movdqa	%xmm5,%xmm10
	psrlq	$4,%xmm5
	pxor	%xmm4,%xmm3
	pxor	%xmm2,%xmm5
	pand	%xmm7,%xmm3
	pand	%xmm7,%xmm5
	pxor	%xmm3,%xmm4
	psllq	$4,%xmm3
	pxor	%xmm5,%xmm2
	psllq	$4,%xmm5
	pxor	%xmm9,%xmm3
	pxor	%xmm10,%xmm5
	movdqa	%xmm0,%xmm9
	psrlq	$4,%xmm0
	movdqa	%xmm15,%xmm10
	psrlq	$4,%xmm15
	pxor	%xmm6,%xmm0
	pxor	%xmm1,%xmm15
	pand	%xmm7,%xmm0
	pand	%xmm7,%xmm15
	pxor	%xmm0,%xmm6
	psllq	$4,%xmm0
	pxor	%xmm15,%xmm1
	psllq	$4,%xmm15
	pxor	%xmm9,%xmm0
	pxor	%xmm10,%xmm15
	movdqa	(%rax),%xmm7
	pxor	%xmm7,%xmm5
	pxor	%xmm7,%xmm3
	pxor	%xmm7,%xmm1
	pxor	%xmm7,%xmm6
	pxor	%xmm7,%xmm2
	pxor	%xmm7,%xmm4
	pxor	%xmm7,%xmm15
	pxor	%xmm7,%xmm0
	.byte	0xf3,0xc3
.cfi_endproc	
.size	_bsaes_decrypt8,.-_bsaes_decrypt8
.type	_bsaes_key_convert,@function
.align	16
_bsaes_key_convert:
.cfi_startproc	
	leaq	.Lmasks(%rip),%r11
	movdqu	(%rcx),%xmm7
	leaq	16(%rcx),%rcx
	movdqa	0(%r11),%xmm0
	movdqa	16(%r11),%xmm1
	movdqa	32(%r11),%xmm2
	movdqa	48(%r11),%xmm3
	movdqa	64(%r11),%xmm4
	pcmpeqd	%xmm5,%xmm5

	movdqu	(%rcx),%xmm6
	movdqa	%xmm7,(%rax)
	leaq	16(%rax),%rax
	decl	%r10d
	jmp	.Lkey_loop
.align	16
.Lkey_loop:
.byte	102,15,56,0,244

	movdqa	%xmm0,%xmm8
	movdqa	%xmm1,%xmm9

	pand	%xmm6,%xmm8
	pand	%xmm6,%xmm9
	movdqa	%xmm2,%xmm10
	pcmpeqb	%xmm0,%xmm8
	psllq	$4,%xmm0
	movdqa	%xmm3,%xmm11
	pcmpeqb	%xmm1,%xmm9
	psllq	$4,%xmm1

	pand	%xmm6,%xmm10
	pand	%xmm6,%xmm11
	movdqa	%xmm0,%xmm12
	pcmpeqb	%xmm2,%xmm10
	psllq	$4,%xmm2
	movdqa	%xmm1,%xmm13
	pcmpeqb	%xmm3,%xmm11
	psllq	$4,%xmm3

	movdqa	%xmm2,%xmm14
	movdqa	%xmm3,%xmm15
	pxor	%xmm5,%xmm8
	pxor	%xmm5,%xmm9

	pand	%xmm6,%xmm12
	pand	%xmm6,%xmm13
	movdqa	%xmm8,0(%rax)
	pcmpeqb	%xmm0,%xmm12
	psrlq	$4,%xmm0
	movdqa	%xmm9,16(%rax)
	pcmpeqb	%xmm1,%xmm13
	psrlq	$4,%xmm1
	leaq	16(%rcx),%rcx

	pand	%xmm6,%xmm14
	pand	%xmm6,%xmm15
	movdqa	%xmm10,32(%rax)
	pcmpeqb	%xmm2,%xmm14
	psrlq	$4,%xmm2
	movdqa	%xmm11,48(%rax)
	pcmpeqb	%xmm3,%xmm15
	psrlq	$4,%xmm3
	movdqu	(%rcx),%xmm6

	pxor	%xmm5,%xmm13
	pxor	%xmm5,%xmm14
	movdqa	%xmm12,64(%rax)
	movdqa	%xmm13,80(%rax)
	movdqa	%xmm14,96(%rax)
	movdqa	%xmm15,112(%rax)
	leaq	128(%rax),%rax
	decl	%r10d
	jnz	.Lkey_loop

	movdqa	80(%r11),%xmm7

	.byte	0xf3,0xc3
.cfi_endproc	
.size	_bsaes_key_convert,.-_bsaes_key_convert

.globl	bsaes_cbc_encrypt
.type	bsaes_cbc_encrypt,@function
.align	16
bsaes_cbc_encrypt:
.cfi_startproc	
	cmpl	$0,%r9d
	jne	asm_AES_cbc_encrypt
	cmpq	$128,%rdx
	jb	asm_AES_cbc_encrypt

	movq	%rsp,%rax
.Lcbc_dec_prologue:
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-16
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-24
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
	leaq	-72(%rsp),%rsp
.cfi_adjust_cfa_offset	0x48
	movq	%rsp,%rbp
.cfi_def_cfa_register	%rbp
	movl	240(%rcx),%eax
	movq	%rdi,%r12
	movq	%rsi,%r13
	movq	%rdx,%r14
	movq	%rcx,%r15
	movq	%r8,%rbx
	shrq	$4,%r14

	movl	%eax,%edx
	shlq	$7,%rax
	subq	$96,%rax
	subq	%rax,%rsp

	movq	%rsp,%rax
	movq	%r15,%rcx
	movl	%edx,%r10d
	call	_bsaes_key_convert
	pxor	(%rsp),%xmm7
	movdqa	%xmm6,(%rax)
	movdqa	%xmm7,(%rsp)

	movdqu	(%rbx),%xmm14
	subq	$8,%r14
.Lcbc_dec_loop:
	movdqu	0(%r12),%xmm15
	movdqu	16(%r12),%xmm0
	movdqu	32(%r12),%xmm1
	movdqu	48(%r12),%xmm2
	movdqu	64(%r12),%xmm3
	movdqu	80(%r12),%xmm4
	movq	%rsp,%rax
	movdqu	96(%r12),%xmm5
	movl	%edx,%r10d
	movdqu	112(%r12),%xmm6
	movdqa	%xmm14,32(%rbp)

	call	_bsaes_decrypt8

	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm0
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm5
	movdqu	48(%r12),%xmm10
	pxor	%xmm9,%xmm3
	movdqu	64(%r12),%xmm11
	pxor	%xmm10,%xmm1
	movdqu	80(%r12),%xmm12
	pxor	%xmm11,%xmm6
	movdqu	96(%r12),%xmm13
	pxor	%xmm12,%xmm2
	movdqu	112(%r12),%xmm14
	pxor	%xmm13,%xmm4
	movdqu	%xmm15,0(%r13)
	leaq	128(%r12),%r12
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	movdqu	%xmm3,48(%r13)
	movdqu	%xmm1,64(%r13)
	movdqu	%xmm6,80(%r13)
	movdqu	%xmm2,96(%r13)
	movdqu	%xmm4,112(%r13)
	leaq	128(%r13),%r13
	subq	$8,%r14
	jnc	.Lcbc_dec_loop

	addq	$8,%r14
	jz	.Lcbc_dec_done

	movdqu	0(%r12),%xmm15
	movq	%rsp,%rax
	movl	%edx,%r10d
	cmpq	$2,%r14
	jb	.Lcbc_dec_one
	movdqu	16(%r12),%xmm0
	je	.Lcbc_dec_two
	movdqu	32(%r12),%xmm1
	cmpq	$4,%r14
	jb	.Lcbc_dec_three
	movdqu	48(%r12),%xmm2
	je	.Lcbc_dec_four
	movdqu	64(%r12),%xmm3
	cmpq	$6,%r14
	jb	.Lcbc_dec_five
	movdqu	80(%r12),%xmm4
	je	.Lcbc_dec_six
	movdqu	96(%r12),%xmm5
	movdqa	%xmm14,32(%rbp)
	call	_bsaes_decrypt8
	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm0
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm5
	movdqu	48(%r12),%xmm10
	pxor	%xmm9,%xmm3
	movdqu	64(%r12),%xmm11
	pxor	%xmm10,%xmm1
	movdqu	80(%r12),%xmm12
	pxor	%xmm11,%xmm6
	movdqu	96(%r12),%xmm14
	pxor	%xmm12,%xmm2
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	movdqu	%xmm3,48(%r13)
	movdqu	%xmm1,64(%r13)
	movdqu	%xmm6,80(%r13)
	movdqu	%xmm2,96(%r13)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_six:
	movdqa	%xmm14,32(%rbp)
	call	_bsaes_decrypt8
	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm0
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm5
	movdqu	48(%r12),%xmm10
	pxor	%xmm9,%xmm3
	movdqu	64(%r12),%xmm11
	pxor	%xmm10,%xmm1
	movdqu	80(%r12),%xmm14
	pxor	%xmm11,%xmm6
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	movdqu	%xmm3,48(%r13)
	movdqu	%xmm1,64(%r13)
	movdqu	%xmm6,80(%r13)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_five:
	movdqa	%xmm14,32(%rbp)
	call	_bsaes_decrypt8
	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm0
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm5
	movdqu	48(%r12),%xmm10
	pxor	%xmm9,%xmm3
	movdqu	64(%r12),%xmm14
	pxor	%xmm10,%xmm1
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	movdqu	%xmm3,48(%r13)
	movdqu	%xmm1,64(%r13)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_four:
	movdqa	%xmm14,32(%rbp)
	call	_bsaes_decrypt8
	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm0
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm5
	movdqu	48(%r12),%xmm14
	pxor	%xmm9,%xmm3
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	movdqu	%xmm3,48(%r13)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_three:
	movdqa	%xmm14,32(%rbp)
	call	_bsaes_decrypt8
	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm0
	movdqu	32(%r12),%xmm14
	pxor	%xmm8,%xmm5
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_two:
	movdqa	%xmm14,32(%rbp)
	call	_bsaes_decrypt8
	pxor	32(%rbp),%xmm15
	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm14
	pxor	%xmm7,%xmm0
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	jmp	.Lcbc_dec_done
.align	16
.Lcbc_dec_one:
	leaq	(%r12),%rdi
	leaq	32(%rbp),%rsi
	leaq	(%r15),%rdx
	call	asm_AES_decrypt
	pxor	32(%rbp),%xmm14
	movdqu	%xmm14,(%r13)
	movdqa	%xmm15,%xmm14

.Lcbc_dec_done:
	movdqu	%xmm14,(%rbx)
	leaq	(%rsp),%rax
	pxor	%xmm0,%xmm0
.Lcbc_dec_bzero:
	movdqa	%xmm0,0(%rax)
	movdqa	%xmm0,16(%rax)
	leaq	32(%rax),%rax
	cmpq	%rax,%rbp
	ja	.Lcbc_dec_bzero

	leaq	120(%rbp),%rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax),%r15
.cfi_restore	%r15
	movq	-40(%rax),%r14
.cfi_restore	%r14
	movq	-32(%rax),%r13
.cfi_restore	%r13
	movq	-24(%rax),%r12
.cfi_restore	%r12
	movq	-16(%rax),%rbx
.cfi_restore	%rbx
	movq	-8(%rax),%rbp
.cfi_restore	%rbp
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lcbc_dec_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bsaes_cbc_encrypt,.-bsaes_cbc_encrypt

.globl	bsaes_ctr32_encrypt_blocks
.type	bsaes_ctr32_encrypt_blocks,@function
.align	16
bsaes_ctr32_encrypt_blocks:
.cfi_startproc	
	movq	%rsp,%rax
.Lctr_enc_prologue:
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-16
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-24
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
	leaq	-72(%rsp),%rsp
.cfi_adjust_cfa_offset	0x48
	movq	%rsp,%rbp
.cfi_def_cfa_register	%rbp
	movdqu	(%r8),%xmm0
	movl	240(%rcx),%eax
	movq	%rdi,%r12
	movq	%rsi,%r13
	movq	%rdx,%r14
	movq	%rcx,%r15
	movdqa	%xmm0,32(%rbp)
	cmpq	$8,%rdx
	jb	.Lctr_enc_short

	movl	%eax,%ebx
	shlq	$7,%rax
	subq	$96,%rax
	subq	%rax,%rsp

	movq	%rsp,%rax
	movq	%r15,%rcx
	movl	%ebx,%r10d
	call	_bsaes_key_convert
	pxor	%xmm6,%xmm7
	movdqa	%xmm7,(%rax)

	movdqa	(%rsp),%xmm8
	leaq	.LADD1(%rip),%r11
	movdqa	32(%rbp),%xmm15
	movdqa	-32(%r11),%xmm7
.byte	102,68,15,56,0,199
.byte	102,68,15,56,0,255
	movdqa	%xmm8,(%rsp)
	jmp	.Lctr_enc_loop
.align	16
.Lctr_enc_loop:
	movdqa	%xmm15,32(%rbp)
	movdqa	%xmm15,%xmm0
	movdqa	%xmm15,%xmm1
	paddd	0(%r11),%xmm0
	movdqa	%xmm15,%xmm2
	paddd	16(%r11),%xmm1
	movdqa	%xmm15,%xmm3
	paddd	32(%r11),%xmm2
	movdqa	%xmm15,%xmm4
	paddd	48(%r11),%xmm3
	movdqa	%xmm15,%xmm5
	paddd	64(%r11),%xmm4
	movdqa	%xmm15,%xmm6
	paddd	80(%r11),%xmm5
	paddd	96(%r11),%xmm6



	movdqa	(%rsp),%xmm8
	leaq	16(%rsp),%rax
	movdqa	-16(%r11),%xmm7
	pxor	%xmm8,%xmm15
	pxor	%xmm8,%xmm0
	pxor	%xmm8,%xmm1
	pxor	%xmm8,%xmm2
.byte	102,68,15,56,0,255
.byte	102,15,56,0,199
	pxor	%xmm8,%xmm3
	pxor	%xmm8,%xmm4
.byte	102,15,56,0,207
.byte	102,15,56,0,215
	pxor	%xmm8,%xmm5
	pxor	%xmm8,%xmm6
.byte	102,15,56,0,223
.byte	102,15,56,0,231
.byte	102,15,56,0,239
.byte	102,15,56,0,247
	leaq	.LBS0(%rip),%r11
	movl	%ebx,%r10d

	call	_bsaes_encrypt8_bitslice

	subq	$8,%r14
	jc	.Lctr_enc_loop_done

	movdqu	0(%r12),%xmm7
	movdqu	16(%r12),%xmm8
	movdqu	32(%r12),%xmm9
	movdqu	48(%r12),%xmm10
	movdqu	64(%r12),%xmm11
	movdqu	80(%r12),%xmm12
	movdqu	96(%r12),%xmm13
	movdqu	112(%r12),%xmm14
	leaq	128(%r12),%r12
	pxor	%xmm15,%xmm7
	movdqa	32(%rbp),%xmm15
	pxor	%xmm8,%xmm0
	movdqu	%xmm7,0(%r13)
	pxor	%xmm9,%xmm3
	movdqu	%xmm0,16(%r13)
	pxor	%xmm10,%xmm5
	movdqu	%xmm3,32(%r13)
	pxor	%xmm11,%xmm2
	movdqu	%xmm5,48(%r13)
	pxor	%xmm12,%xmm6
	movdqu	%xmm2,64(%r13)
	pxor	%xmm13,%xmm1
	movdqu	%xmm6,80(%r13)
	pxor	%xmm14,%xmm4
	movdqu	%xmm1,96(%r13)
	leaq	.LADD1(%rip),%r11
	movdqu	%xmm4,112(%r13)
	leaq	128(%r13),%r13
	paddd	112(%r11),%xmm15
	jnz	.Lctr_enc_loop

	jmp	.Lctr_enc_done
.align	16
.Lctr_enc_loop_done:
	addq	$8,%r14
	movdqu	0(%r12),%xmm7
	pxor	%xmm7,%xmm15
	movdqu	%xmm15,0(%r13)
	cmpq	$2,%r14
	jb	.Lctr_enc_done
	movdqu	16(%r12),%xmm8
	pxor	%xmm8,%xmm0
	movdqu	%xmm0,16(%r13)
	je	.Lctr_enc_done
	movdqu	32(%r12),%xmm9
	pxor	%xmm9,%xmm3
	movdqu	%xmm3,32(%r13)
	cmpq	$4,%r14
	jb	.Lctr_enc_done
	movdqu	48(%r12),%xmm10
	pxor	%xmm10,%xmm5
	movdqu	%xmm5,48(%r13)
	je	.Lctr_enc_done
	movdqu	64(%r12),%xmm11
	pxor	%xmm11,%xmm2
	movdqu	%xmm2,64(%r13)
	cmpq	$6,%r14
	jb	.Lctr_enc_done
	movdqu	80(%r12),%xmm12
	pxor	%xmm12,%xmm6
	movdqu	%xmm6,80(%r13)
	je	.Lctr_enc_done
	movdqu	96(%r12),%xmm13
	pxor	%xmm13,%xmm1
	movdqu	%xmm1,96(%r13)
	jmp	.Lctr_enc_done

.align	16
.Lctr_enc_short:
	leaq	32(%rbp),%rdi
	leaq	48(%rbp),%rsi
	leaq	(%r15),%rdx
	call	asm_AES_encrypt
	movdqu	(%r12),%xmm0
	leaq	16(%r12),%r12
	movl	44(%rbp),%eax
	bswapl	%eax
	pxor	48(%rbp),%xmm0
	incl	%eax
	movdqu	%xmm0,(%r13)
	bswapl	%eax
	leaq	16(%r13),%r13
	movl	%eax,44(%rsp)
	decq	%r14
	jnz	.Lctr_enc_short

.Lctr_enc_done:
	leaq	(%rsp),%rax
	pxor	%xmm0,%xmm0
.Lctr_enc_bzero:
	movdqa	%xmm0,0(%rax)
	movdqa	%xmm0,16(%rax)
	leaq	32(%rax),%rax
	cmpq	%rax,%rbp
	ja	.Lctr_enc_bzero

	leaq	120(%rbp),%rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax),%r15
.cfi_restore	%r15
	movq	-40(%rax),%r14
.cfi_restore	%r14
	movq	-32(%rax),%r13
.cfi_restore	%r13
	movq	-24(%rax),%r12
.cfi_restore	%r12
	movq	-16(%rax),%rbx
.cfi_restore	%rbx
	movq	-8(%rax),%rbp
.cfi_restore	%rbp
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lctr_enc_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bsaes_ctr32_encrypt_blocks,.-bsaes_ctr32_encrypt_blocks
.globl	bsaes_xts_encrypt
.type	bsaes_xts_encrypt,@function
.align	16
bsaes_xts_encrypt:
.cfi_startproc	
	movq	%rsp,%rax
.Lxts_enc_prologue:
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-16
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-24
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
	leaq	-72(%rsp),%rsp
.cfi_adjust_cfa_offset	0x48
	movq	%rsp,%rbp
.cfi_def_cfa_register	%rbp
	movq	%rdi,%r12
	movq	%rsi,%r13
	movq	%rdx,%r14
	movq	%rcx,%r15

	leaq	(%r9),%rdi
	leaq	32(%rbp),%rsi
	leaq	(%r8),%rdx
	call	asm_AES_encrypt

	movl	240(%r15),%eax
	movq	%r14,%rbx

	movl	%eax,%edx
	shlq	$7,%rax
	subq	$96,%rax
	subq	%rax,%rsp

	movq	%rsp,%rax
	movq	%r15,%rcx
	movl	%edx,%r10d
	call	_bsaes_key_convert
	pxor	%xmm6,%xmm7
	movdqa	%xmm7,(%rax)

	andq	$-16,%r14
	subq	$0x80,%rsp
	movdqa	32(%rbp),%xmm6

	pxor	%xmm14,%xmm14
	movdqa	.Lxts_magic(%rip),%xmm12
	pcmpgtd	%xmm6,%xmm14

	subq	$0x80,%r14
	jc	.Lxts_enc_short
	jmp	.Lxts_enc_loop

.align	16
.Lxts_enc_loop:
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm15
	movdqa	%xmm6,0(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm0
	movdqa	%xmm6,16(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	0(%r12),%xmm7
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm1
	movdqa	%xmm6,32(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm15
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm2
	movdqa	%xmm6,48(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm0
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm3
	movdqa	%xmm6,64(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	48(%r12),%xmm10
	pxor	%xmm9,%xmm1
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm4
	movdqa	%xmm6,80(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	64(%r12),%xmm11
	pxor	%xmm10,%xmm2
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm5
	movdqa	%xmm6,96(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	80(%r12),%xmm12
	pxor	%xmm11,%xmm3
	movdqu	96(%r12),%xmm13
	pxor	%xmm12,%xmm4
	movdqu	112(%r12),%xmm14
	leaq	128(%r12),%r12
	movdqa	%xmm6,112(%rsp)
	pxor	%xmm13,%xmm5
	leaq	128(%rsp),%rax
	pxor	%xmm14,%xmm6
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm3
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm5
	movdqu	%xmm3,32(%r13)
	pxor	64(%rsp),%xmm2
	movdqu	%xmm5,48(%r13)
	pxor	80(%rsp),%xmm6
	movdqu	%xmm2,64(%r13)
	pxor	96(%rsp),%xmm1
	movdqu	%xmm6,80(%r13)
	pxor	112(%rsp),%xmm4
	movdqu	%xmm1,96(%r13)
	movdqu	%xmm4,112(%r13)
	leaq	128(%r13),%r13

	movdqa	112(%rsp),%xmm6
	pxor	%xmm14,%xmm14
	movdqa	.Lxts_magic(%rip),%xmm12
	pcmpgtd	%xmm6,%xmm14
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6

	subq	$0x80,%r14
	jnc	.Lxts_enc_loop

.Lxts_enc_short:
	addq	$0x80,%r14
	jz	.Lxts_enc_done
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm15
	movdqa	%xmm6,0(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm0
	movdqa	%xmm6,16(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	0(%r12),%xmm7
	cmpq	$16,%r14
	je	.Lxts_enc_1
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm1
	movdqa	%xmm6,32(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	16(%r12),%xmm8
	cmpq	$32,%r14
	je	.Lxts_enc_2
	pxor	%xmm7,%xmm15
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm2
	movdqa	%xmm6,48(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	32(%r12),%xmm9
	cmpq	$48,%r14
	je	.Lxts_enc_3
	pxor	%xmm8,%xmm0
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm3
	movdqa	%xmm6,64(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	48(%r12),%xmm10
	cmpq	$64,%r14
	je	.Lxts_enc_4
	pxor	%xmm9,%xmm1
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm4
	movdqa	%xmm6,80(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	64(%r12),%xmm11
	cmpq	$80,%r14
	je	.Lxts_enc_5
	pxor	%xmm10,%xmm2
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm5
	movdqa	%xmm6,96(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	80(%r12),%xmm12
	cmpq	$96,%r14
	je	.Lxts_enc_6
	pxor	%xmm11,%xmm3
	movdqu	96(%r12),%xmm13
	pxor	%xmm12,%xmm4
	movdqa	%xmm6,112(%rsp)
	leaq	112(%r12),%r12
	pxor	%xmm13,%xmm5
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm3
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm5
	movdqu	%xmm3,32(%r13)
	pxor	64(%rsp),%xmm2
	movdqu	%xmm5,48(%r13)
	pxor	80(%rsp),%xmm6
	movdqu	%xmm2,64(%r13)
	pxor	96(%rsp),%xmm1
	movdqu	%xmm6,80(%r13)
	movdqu	%xmm1,96(%r13)
	leaq	112(%r13),%r13

	movdqa	112(%rsp),%xmm6
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_6:
	pxor	%xmm11,%xmm3
	leaq	96(%r12),%r12
	pxor	%xmm12,%xmm4
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm3
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm5
	movdqu	%xmm3,32(%r13)
	pxor	64(%rsp),%xmm2
	movdqu	%xmm5,48(%r13)
	pxor	80(%rsp),%xmm6
	movdqu	%xmm2,64(%r13)
	movdqu	%xmm6,80(%r13)
	leaq	96(%r13),%r13

	movdqa	96(%rsp),%xmm6
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_5:
	pxor	%xmm10,%xmm2
	leaq	80(%r12),%r12
	pxor	%xmm11,%xmm3
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm3
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm5
	movdqu	%xmm3,32(%r13)
	pxor	64(%rsp),%xmm2
	movdqu	%xmm5,48(%r13)
	movdqu	%xmm2,64(%r13)
	leaq	80(%r13),%r13

	movdqa	80(%rsp),%xmm6
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_4:
	pxor	%xmm9,%xmm1
	leaq	64(%r12),%r12
	pxor	%xmm10,%xmm2
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm3
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm5
	movdqu	%xmm3,32(%r13)
	movdqu	%xmm5,48(%r13)
	leaq	64(%r13),%r13

	movdqa	64(%rsp),%xmm6
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_3:
	pxor	%xmm8,%xmm0
	leaq	48(%r12),%r12
	pxor	%xmm9,%xmm1
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm3
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm3,32(%r13)
	leaq	48(%r13),%r13

	movdqa	48(%rsp),%xmm6
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_2:
	pxor	%xmm7,%xmm15
	leaq	32(%r12),%r12
	pxor	%xmm8,%xmm0
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_encrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	leaq	32(%r13),%r13

	movdqa	32(%rsp),%xmm6
	jmp	.Lxts_enc_done
.align	16
.Lxts_enc_1:
	pxor	%xmm15,%xmm7
	leaq	16(%r12),%r12
	movdqa	%xmm7,32(%rbp)
	leaq	32(%rbp),%rdi
	leaq	32(%rbp),%rsi
	leaq	(%r15),%rdx
	call	asm_AES_encrypt
	pxor	32(%rbp),%xmm15





	movdqu	%xmm15,0(%r13)
	leaq	16(%r13),%r13

	movdqa	16(%rsp),%xmm6

.Lxts_enc_done:
	andl	$15,%ebx
	jz	.Lxts_enc_ret
	movq	%r13,%rdx

.Lxts_enc_steal:
	movzbl	(%r12),%eax
	movzbl	-16(%rdx),%ecx
	leaq	1(%r12),%r12
	movb	%al,-16(%rdx)
	movb	%cl,0(%rdx)
	leaq	1(%rdx),%rdx
	subl	$1,%ebx
	jnz	.Lxts_enc_steal

	movdqu	-16(%r13),%xmm15
	leaq	32(%rbp),%rdi
	pxor	%xmm6,%xmm15
	leaq	32(%rbp),%rsi
	movdqa	%xmm15,32(%rbp)
	leaq	(%r15),%rdx
	call	asm_AES_encrypt
	pxor	32(%rbp),%xmm6
	movdqu	%xmm6,-16(%r13)

.Lxts_enc_ret:
	leaq	(%rsp),%rax
	pxor	%xmm0,%xmm0
.Lxts_enc_bzero:
	movdqa	%xmm0,0(%rax)
	movdqa	%xmm0,16(%rax)
	leaq	32(%rax),%rax
	cmpq	%rax,%rbp
	ja	.Lxts_enc_bzero

	leaq	120(%rbp),%rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax),%r15
.cfi_restore	%r15
	movq	-40(%rax),%r14
.cfi_restore	%r14
	movq	-32(%rax),%r13
.cfi_restore	%r13
	movq	-24(%rax),%r12
.cfi_restore	%r12
	movq	-16(%rax),%rbx
.cfi_restore	%rbx
	movq	-8(%rax),%rbp
.cfi_restore	%rbp
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lxts_enc_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bsaes_xts_encrypt,.-bsaes_xts_encrypt

.globl	bsaes_xts_decrypt
.type	bsaes_xts_decrypt,@function
.align	16
bsaes_xts_decrypt:
.cfi_startproc	
	movq	%rsp,%rax
.Lxts_dec_prologue:
	pushq	%rbp
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbp,-16
	pushq	%rbx
.cfi_adjust_cfa_offset	8
.cfi_offset	%rbx,-24
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
	leaq	-72(%rsp),%rsp
.cfi_adjust_cfa_offset	0x48
	movq	%rsp,%rbp
	movq	%rdi,%r12
	movq	%rsi,%r13
	movq	%rdx,%r14
	movq	%rcx,%r15

	leaq	(%r9),%rdi
	leaq	32(%rbp),%rsi
	leaq	(%r8),%rdx
	call	asm_AES_encrypt

	movl	240(%r15),%eax
	movq	%r14,%rbx

	movl	%eax,%edx
	shlq	$7,%rax
	subq	$96,%rax
	subq	%rax,%rsp

	movq	%rsp,%rax
	movq	%r15,%rcx
	movl	%edx,%r10d
	call	_bsaes_key_convert
	pxor	(%rsp),%xmm7
	movdqa	%xmm6,(%rax)
	movdqa	%xmm7,(%rsp)

	xorl	%eax,%eax
	andq	$-16,%r14
	testl	$15,%ebx
	setnz	%al
	shlq	$4,%rax
	subq	%rax,%r14

	subq	$0x80,%rsp
	movdqa	32(%rbp),%xmm6

	pxor	%xmm14,%xmm14
	movdqa	.Lxts_magic(%rip),%xmm12
	pcmpgtd	%xmm6,%xmm14

	subq	$0x80,%r14
	jc	.Lxts_dec_short
	jmp	.Lxts_dec_loop

.align	16
.Lxts_dec_loop:
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm15
	movdqa	%xmm6,0(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm0
	movdqa	%xmm6,16(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	0(%r12),%xmm7
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm1
	movdqa	%xmm6,32(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	16(%r12),%xmm8
	pxor	%xmm7,%xmm15
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm2
	movdqa	%xmm6,48(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	32(%r12),%xmm9
	pxor	%xmm8,%xmm0
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm3
	movdqa	%xmm6,64(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	48(%r12),%xmm10
	pxor	%xmm9,%xmm1
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm4
	movdqa	%xmm6,80(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	64(%r12),%xmm11
	pxor	%xmm10,%xmm2
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm5
	movdqa	%xmm6,96(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	80(%r12),%xmm12
	pxor	%xmm11,%xmm3
	movdqu	96(%r12),%xmm13
	pxor	%xmm12,%xmm4
	movdqu	112(%r12),%xmm14
	leaq	128(%r12),%r12
	movdqa	%xmm6,112(%rsp)
	pxor	%xmm13,%xmm5
	leaq	128(%rsp),%rax
	pxor	%xmm14,%xmm6
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm5
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm3
	movdqu	%xmm5,32(%r13)
	pxor	64(%rsp),%xmm1
	movdqu	%xmm3,48(%r13)
	pxor	80(%rsp),%xmm6
	movdqu	%xmm1,64(%r13)
	pxor	96(%rsp),%xmm2
	movdqu	%xmm6,80(%r13)
	pxor	112(%rsp),%xmm4
	movdqu	%xmm2,96(%r13)
	movdqu	%xmm4,112(%r13)
	leaq	128(%r13),%r13

	movdqa	112(%rsp),%xmm6
	pxor	%xmm14,%xmm14
	movdqa	.Lxts_magic(%rip),%xmm12
	pcmpgtd	%xmm6,%xmm14
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6

	subq	$0x80,%r14
	jnc	.Lxts_dec_loop

.Lxts_dec_short:
	addq	$0x80,%r14
	jz	.Lxts_dec_done
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm15
	movdqa	%xmm6,0(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm0
	movdqa	%xmm6,16(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	0(%r12),%xmm7
	cmpq	$16,%r14
	je	.Lxts_dec_1
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm1
	movdqa	%xmm6,32(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	16(%r12),%xmm8
	cmpq	$32,%r14
	je	.Lxts_dec_2
	pxor	%xmm7,%xmm15
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm2
	movdqa	%xmm6,48(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	32(%r12),%xmm9
	cmpq	$48,%r14
	je	.Lxts_dec_3
	pxor	%xmm8,%xmm0
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm3
	movdqa	%xmm6,64(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	48(%r12),%xmm10
	cmpq	$64,%r14
	je	.Lxts_dec_4
	pxor	%xmm9,%xmm1
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm4
	movdqa	%xmm6,80(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	64(%r12),%xmm11
	cmpq	$80,%r14
	je	.Lxts_dec_5
	pxor	%xmm10,%xmm2
	pshufd	$0x13,%xmm14,%xmm13
	pxor	%xmm14,%xmm14
	movdqa	%xmm6,%xmm5
	movdqa	%xmm6,96(%rsp)
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	pcmpgtd	%xmm6,%xmm14
	pxor	%xmm13,%xmm6
	movdqu	80(%r12),%xmm12
	cmpq	$96,%r14
	je	.Lxts_dec_6
	pxor	%xmm11,%xmm3
	movdqu	96(%r12),%xmm13
	pxor	%xmm12,%xmm4
	movdqa	%xmm6,112(%rsp)
	leaq	112(%r12),%r12
	pxor	%xmm13,%xmm5
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm5
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm3
	movdqu	%xmm5,32(%r13)
	pxor	64(%rsp),%xmm1
	movdqu	%xmm3,48(%r13)
	pxor	80(%rsp),%xmm6
	movdqu	%xmm1,64(%r13)
	pxor	96(%rsp),%xmm2
	movdqu	%xmm6,80(%r13)
	movdqu	%xmm2,96(%r13)
	leaq	112(%r13),%r13

	movdqa	112(%rsp),%xmm6
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_6:
	pxor	%xmm11,%xmm3
	leaq	96(%r12),%r12
	pxor	%xmm12,%xmm4
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm5
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm3
	movdqu	%xmm5,32(%r13)
	pxor	64(%rsp),%xmm1
	movdqu	%xmm3,48(%r13)
	pxor	80(%rsp),%xmm6
	movdqu	%xmm1,64(%r13)
	movdqu	%xmm6,80(%r13)
	leaq	96(%r13),%r13

	movdqa	96(%rsp),%xmm6
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_5:
	pxor	%xmm10,%xmm2
	leaq	80(%r12),%r12
	pxor	%xmm11,%xmm3
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm5
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm3
	movdqu	%xmm5,32(%r13)
	pxor	64(%rsp),%xmm1
	movdqu	%xmm3,48(%r13)
	movdqu	%xmm1,64(%r13)
	leaq	80(%r13),%r13

	movdqa	80(%rsp),%xmm6
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_4:
	pxor	%xmm9,%xmm1
	leaq	64(%r12),%r12
	pxor	%xmm10,%xmm2
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm5
	movdqu	%xmm0,16(%r13)
	pxor	48(%rsp),%xmm3
	movdqu	%xmm5,32(%r13)
	movdqu	%xmm3,48(%r13)
	leaq	64(%r13),%r13

	movdqa	64(%rsp),%xmm6
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_3:
	pxor	%xmm8,%xmm0
	leaq	48(%r12),%r12
	pxor	%xmm9,%xmm1
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	pxor	32(%rsp),%xmm5
	movdqu	%xmm0,16(%r13)
	movdqu	%xmm5,32(%r13)
	leaq	48(%r13),%r13

	movdqa	48(%rsp),%xmm6
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_2:
	pxor	%xmm7,%xmm15
	leaq	32(%r12),%r12
	pxor	%xmm8,%xmm0
	leaq	128(%rsp),%rax
	movl	%edx,%r10d

	call	_bsaes_decrypt8

	pxor	0(%rsp),%xmm15
	pxor	16(%rsp),%xmm0
	movdqu	%xmm15,0(%r13)
	movdqu	%xmm0,16(%r13)
	leaq	32(%r13),%r13

	movdqa	32(%rsp),%xmm6
	jmp	.Lxts_dec_done
.align	16
.Lxts_dec_1:
	pxor	%xmm15,%xmm7
	leaq	16(%r12),%r12
	movdqa	%xmm7,32(%rbp)
	leaq	32(%rbp),%rdi
	leaq	32(%rbp),%rsi
	leaq	(%r15),%rdx
	call	asm_AES_decrypt
	pxor	32(%rbp),%xmm15





	movdqu	%xmm15,0(%r13)
	leaq	16(%r13),%r13

	movdqa	16(%rsp),%xmm6

.Lxts_dec_done:
	andl	$15,%ebx
	jz	.Lxts_dec_ret

	pxor	%xmm14,%xmm14
	movdqa	.Lxts_magic(%rip),%xmm12
	pcmpgtd	%xmm6,%xmm14
	pshufd	$0x13,%xmm14,%xmm13
	movdqa	%xmm6,%xmm5
	paddq	%xmm6,%xmm6
	pand	%xmm12,%xmm13
	movdqu	(%r12),%xmm15
	pxor	%xmm13,%xmm6

	leaq	32(%rbp),%rdi
	pxor	%xmm6,%xmm15
	leaq	32(%rbp),%rsi
	movdqa	%xmm15,32(%rbp)
	leaq	(%r15),%rdx
	call	asm_AES_decrypt
	pxor	32(%rbp),%xmm6
	movq	%r13,%rdx
	movdqu	%xmm6,(%r13)

.Lxts_dec_steal:
	movzbl	16(%r12),%eax
	movzbl	(%rdx),%ecx
	leaq	1(%r12),%r12
	movb	%al,(%rdx)
	movb	%cl,16(%rdx)
	leaq	1(%rdx),%rdx
	subl	$1,%ebx
	jnz	.Lxts_dec_steal

	movdqu	(%r13),%xmm15
	leaq	32(%rbp),%rdi
	pxor	%xmm5,%xmm15
	leaq	32(%rbp),%rsi
	movdqa	%xmm15,32(%rbp)
	leaq	(%r15),%rdx
	call	asm_AES_decrypt
	pxor	32(%rbp),%xmm5
	movdqu	%xmm5,(%r13)

.Lxts_dec_ret:
	leaq	(%rsp),%rax
	pxor	%xmm0,%xmm0
.Lxts_dec_bzero:
	movdqa	%xmm0,0(%rax)
	movdqa	%xmm0,16(%rax)
	leaq	32(%rax),%rax
	cmpq	%rax,%rbp
	ja	.Lxts_dec_bzero

	leaq	120(%rbp),%rax
.cfi_def_cfa	%rax,8
	movq	-48(%rax),%r15
.cfi_restore	%r15
	movq	-40(%rax),%r14
.cfi_restore	%r14
	movq	-32(%rax),%r13
.cfi_restore	%r13
	movq	-24(%rax),%r12
.cfi_restore	%r12
	movq	-16(%rax),%rbx
.cfi_restore	%rbx
	movq	-8(%rax),%rbp
.cfi_restore	%rbp
	leaq	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lxts_dec_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	bsaes_xts_decrypt,.-bsaes_xts_decrypt
.type	_bsaes_const,@object
.align	64
_bsaes_const:
.LM0ISR:
.quad	0x0a0e0206070b0f03, 0x0004080c0d010509
.LISRM0:
.quad	0x01040b0e0205080f, 0x0306090c00070a0d
.LISR:
.quad	0x0504070602010003, 0x0f0e0d0c080b0a09
.LBS0:
.quad	0x5555555555555555, 0x5555555555555555
.LBS1:
.quad	0x3333333333333333, 0x3333333333333333
.LBS2:
.quad	0x0f0f0f0f0f0f0f0f, 0x0f0f0f0f0f0f0f0f
.LSR:
.quad	0x0504070600030201, 0x0f0e0d0c0a09080b
.LSRM0:
.quad	0x0304090e00050a0f, 0x01060b0c0207080d
.LM0SR:
.quad	0x0a0e02060f03070b, 0x0004080c05090d01
.LSWPUP:
.quad	0x0706050403020100, 0x0c0d0e0f0b0a0908
.LSWPUPM0SR:
.quad	0x0a0d02060c03070b, 0x0004080f05090e01
.LADD1:
.quad	0x0000000000000000, 0x0000000100000000
.LADD2:
.quad	0x0000000000000000, 0x0000000200000000
.LADD3:
.quad	0x0000000000000000, 0x0000000300000000
.LADD4:
.quad	0x0000000000000000, 0x0000000400000000
.LADD5:
.quad	0x0000000000000000, 0x0000000500000000
.LADD6:
.quad	0x0000000000000000, 0x0000000600000000
.LADD7:
.quad	0x0000000000000000, 0x0000000700000000
.LADD8:
.quad	0x0000000000000000, 0x0000000800000000
.Lxts_magic:
.long	0x87,0,1,0
.Lmasks:
.quad	0x0101010101010101, 0x0101010101010101
.quad	0x0202020202020202, 0x0202020202020202
.quad	0x0404040404040404, 0x0404040404040404
.quad	0x0808080808080808, 0x0808080808080808
.LM0:
.quad	0x02060a0e03070b0f, 0x0004080c0105090d
.L63:
.quad	0x6363636363636363, 0x6363636363636363
.byte	66,105,116,45,115,108,105,99,101,100,32,65,69,83,32,102,111,114,32,120,56,54,95,54,52,47,83,83,83,69,51,44,32,69,109,105,108,105,97,32,75,195,164,115,112,101,114,44,32,80,101,116,101,114,32,83,99,104,119,97,98,101,44,32,65,110,100,121,32,80,111,108,121,97,107,111,118,0
.align	64
.size	_bsaes_const,.-_bsaes_const
