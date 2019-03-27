/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from e_padlock-x86.pl. */
#ifdef PIC
.text
.globl	padlock_capability
.type	padlock_capability,@function
.align	16
padlock_capability:
.L_padlock_capability_begin:
	pushl	%ebx
	pushfl
	popl	%eax
	movl	%eax,%ecx
	xorl	$2097152,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	xorl	%eax,%ecx
	xorl	%eax,%eax
	btl	$21,%ecx
	jnc	.L000noluck
	.byte	0x0f,0xa2
	xorl	%eax,%eax
	cmpl	$0x746e6543,%ebx
	jne	.L001zhaoxin
	cmpl	$0x48727561,%edx
	jne	.L000noluck
	cmpl	$0x736c7561,%ecx
	jne	.L000noluck
	jmp	.L002zhaoxinEnd
.L001zhaoxin:
	cmpl	$0x68532020,%ebx
	jne	.L000noluck
	cmpl	$0x68676e61,%edx
	jne	.L000noluck
	cmpl	$0x20206961,%ecx
	jne	.L000noluck
.L002zhaoxinEnd:
	movl	$3221225472,%eax
	.byte	0x0f,0xa2
	movl	%eax,%edx
	xorl	%eax,%eax
	cmpl	$3221225473,%edx
	jb	.L000noluck
	movl	$1,%eax
	.byte	0x0f,0xa2
	orl	$15,%eax
	xorl	%ebx,%ebx
	andl	$4095,%eax
	cmpl	$1791,%eax
	sete	%bl
	movl	$3221225473,%eax
	pushl	%ebx
	.byte	0x0f,0xa2
	popl	%ebx
	movl	%edx,%eax
	shll	$4,%ebx
	andl	$4294967279,%eax
	orl	%ebx,%eax
.L000noluck:
	popl	%ebx
	ret
.size	padlock_capability,.-.L_padlock_capability_begin
.globl	padlock_key_bswap
.type	padlock_key_bswap,@function
.align	16
padlock_key_bswap:
.L_padlock_key_bswap_begin:
	movl	4(%esp),%edx
	movl	240(%edx),%ecx
.L003bswap_loop:
	movl	(%edx),%eax
	bswap	%eax
	movl	%eax,(%edx)
	leal	4(%edx),%edx
	subl	$1,%ecx
	jnz	.L003bswap_loop
	ret
.size	padlock_key_bswap,.-.L_padlock_key_bswap_begin
.globl	padlock_verify_context
.type	padlock_verify_context,@function
.align	16
padlock_verify_context:
.L_padlock_verify_context_begin:
	movl	4(%esp),%edx
	leal	.Lpadlock_saved_context-.L004verify_pic_point,%eax
	pushfl
	call	_padlock_verify_ctx
.L004verify_pic_point:
	leal	4(%esp),%esp
	ret
.size	padlock_verify_context,.-.L_padlock_verify_context_begin
.type	_padlock_verify_ctx,@function
.align	16
_padlock_verify_ctx:
	addl	(%esp),%eax
	btl	$30,4(%esp)
	jnc	.L005verified
	cmpl	(%eax),%edx
	je	.L005verified
	pushfl
	popfl
.L005verified:
	movl	%edx,(%eax)
	ret
.size	_padlock_verify_ctx,.-_padlock_verify_ctx
.globl	padlock_reload_key
.type	padlock_reload_key,@function
.align	16
padlock_reload_key:
.L_padlock_reload_key_begin:
	pushfl
	popfl
	ret
.size	padlock_reload_key,.-.L_padlock_reload_key_begin
.globl	padlock_aes_block
.type	padlock_aes_block,@function
.align	16
padlock_aes_block:
.L_padlock_aes_block_begin:
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	16(%esp),%edi
	movl	20(%esp),%esi
	movl	24(%esp),%edx
	movl	$1,%ecx
	leal	32(%edx),%ebx
	leal	16(%edx),%edx
.byte	243,15,167,200
	popl	%ebx
	popl	%esi
	popl	%edi
	ret
.size	padlock_aes_block,.-.L_padlock_aes_block_begin
.globl	padlock_ecb_encrypt
.type	padlock_ecb_encrypt,@function
.align	16
padlock_ecb_encrypt:
.L_padlock_ecb_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L006ecb_abort
	testl	$15,%ecx
	jnz	.L006ecb_abort
	leal	.Lpadlock_saved_context-.L007ecb_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L007ecb_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L008ecb_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L008ecb_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	cmpl	%ebx,%ecx
	ja	.L009ecb_loop
	movl	%esi,%eax
	cmpl	%esp,%ebp
	cmovel	%edi,%eax
	addl	%ecx,%eax
	negl	%eax
	andl	$4095,%eax
	cmpl	$128,%eax
	movl	$-128,%eax
	cmovael	%ebx,%eax
	andl	%eax,%ebx
	jz	.L010ecb_unaligned_tail
	jmp	.L009ecb_loop
.align	16
.L009ecb_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L011ecb_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L011ecb_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,200
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L012ecb_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L012ecb_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jz	.L013ecb_break
	cmpl	%ebx,%ecx
	jae	.L009ecb_loop
.L010ecb_unaligned_tail:
	xorl	%eax,%eax
	cmpl	%ebp,%esp
	cmovel	%ecx,%eax
	subl	%eax,%esp
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L009ecb_loop
.align	16
.L013ecb_break:
	cmpl	%ebp,%esp
	je	.L014ecb_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L015ecb_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L015ecb_bzero
.L014ecb_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L016ecb_exit
.align	16
.L008ecb_aligned:
	leal	(%esi,%ecx,1),%ebp
	negl	%ebp
	andl	$4095,%ebp
	xorl	%eax,%eax
	cmpl	$128,%ebp
	movl	$127,%ebp
	cmovael	%eax,%ebp
	andl	%ecx,%ebp
	subl	%ebp,%ecx
	jz	.L017ecb_aligned_tail
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,200
	testl	%ebp,%ebp
	jz	.L016ecb_exit
.L017ecb_aligned_tail:
	movl	%ebp,%ecx
	leal	-24(%esp),%ebp
	movl	%ebp,%esp
	movl	%ebp,%eax
	subl	%ecx,%esp
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L009ecb_loop
.L016ecb_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L006ecb_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_ecb_encrypt,.-.L_padlock_ecb_encrypt_begin
.globl	padlock_cbc_encrypt
.type	padlock_cbc_encrypt,@function
.align	16
padlock_cbc_encrypt:
.L_padlock_cbc_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L018cbc_abort
	testl	$15,%ecx
	jnz	.L018cbc_abort
	leal	.Lpadlock_saved_context-.L019cbc_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L019cbc_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L020cbc_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L020cbc_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	cmpl	%ebx,%ecx
	ja	.L021cbc_loop
	movl	%esi,%eax
	cmpl	%esp,%ebp
	cmovel	%edi,%eax
	addl	%ecx,%eax
	negl	%eax
	andl	$4095,%eax
	cmpl	$64,%eax
	movl	$-64,%eax
	cmovael	%ebx,%eax
	andl	%eax,%ebx
	jz	.L022cbc_unaligned_tail
	jmp	.L021cbc_loop
.align	16
.L021cbc_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L023cbc_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L023cbc_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,208
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L024cbc_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L024cbc_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jz	.L025cbc_break
	cmpl	%ebx,%ecx
	jae	.L021cbc_loop
.L022cbc_unaligned_tail:
	xorl	%eax,%eax
	cmpl	%ebp,%esp
	cmovel	%ecx,%eax
	subl	%eax,%esp
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L021cbc_loop
.align	16
.L025cbc_break:
	cmpl	%ebp,%esp
	je	.L026cbc_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L027cbc_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L027cbc_bzero
.L026cbc_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L028cbc_exit
.align	16
.L020cbc_aligned:
	leal	(%esi,%ecx,1),%ebp
	negl	%ebp
	andl	$4095,%ebp
	xorl	%eax,%eax
	cmpl	$64,%ebp
	movl	$63,%ebp
	cmovael	%eax,%ebp
	andl	%ecx,%ebp
	subl	%ebp,%ecx
	jz	.L029cbc_aligned_tail
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,208
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	testl	%ebp,%ebp
	jz	.L028cbc_exit
.L029cbc_aligned_tail:
	movl	%ebp,%ecx
	leal	-24(%esp),%ebp
	movl	%ebp,%esp
	movl	%ebp,%eax
	subl	%ecx,%esp
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L021cbc_loop
.L028cbc_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L018cbc_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_cbc_encrypt,.-.L_padlock_cbc_encrypt_begin
.globl	padlock_cfb_encrypt
.type	padlock_cfb_encrypt,@function
.align	16
padlock_cfb_encrypt:
.L_padlock_cfb_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L030cfb_abort
	testl	$15,%ecx
	jnz	.L030cfb_abort
	leal	.Lpadlock_saved_context-.L031cfb_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L031cfb_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L032cfb_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L032cfb_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	jmp	.L033cfb_loop
.align	16
.L033cfb_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L034cfb_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L034cfb_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,224
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L035cfb_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L035cfb_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jnz	.L033cfb_loop
	cmpl	%ebp,%esp
	je	.L036cfb_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L037cfb_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L037cfb_bzero
.L036cfb_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L038cfb_exit
.align	16
.L032cfb_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,224
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
.L038cfb_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L030cfb_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_cfb_encrypt,.-.L_padlock_cfb_encrypt_begin
.globl	padlock_ofb_encrypt
.type	padlock_ofb_encrypt,@function
.align	16
padlock_ofb_encrypt:
.L_padlock_ofb_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L039ofb_abort
	testl	$15,%ecx
	jnz	.L039ofb_abort
	leal	.Lpadlock_saved_context-.L040ofb_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L040ofb_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L041ofb_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L041ofb_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	jmp	.L042ofb_loop
.align	16
.L042ofb_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L043ofb_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L043ofb_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,232
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L044ofb_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L044ofb_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jnz	.L042ofb_loop
	cmpl	%ebp,%esp
	je	.L045ofb_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L046ofb_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L046ofb_bzero
.L045ofb_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L047ofb_exit
.align	16
.L041ofb_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,232
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
.L047ofb_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L039ofb_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_ofb_encrypt,.-.L_padlock_ofb_encrypt_begin
.globl	padlock_ctr32_encrypt
.type	padlock_ctr32_encrypt,@function
.align	16
padlock_ctr32_encrypt:
.L_padlock_ctr32_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L048ctr32_abort
	testl	$15,%ecx
	jnz	.L048ctr32_abort
	leal	.Lpadlock_saved_context-.L049ctr32_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L049ctr32_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	movq	-16(%edx),%mm0
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	jmp	.L050ctr32_loop
.align	16
.L050ctr32_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	movl	-4(%edx),%ecx
	xorl	%edi,%edi
	movl	-8(%edx),%eax
.L051ctr32_prepare:
	movl	%ecx,12(%esp,%edi,1)
	bswap	%ecx
	movq	%mm0,(%esp,%edi,1)
	incl	%ecx
	movl	%eax,8(%esp,%edi,1)
	bswap	%ecx
	leal	16(%edi),%edi
	cmpl	%ebx,%edi
	jb	.L051ctr32_prepare
	movl	%ecx,-4(%edx)
	leal	(%esp),%esi
	leal	(%esp),%edi
	movl	%ebx,%ecx
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,200
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	movl	4(%ebp),%esi
	xorl	%ecx,%ecx
.L052ctr32_xor:
	movups	(%esi,%ecx,1),%xmm1
	leal	16(%ecx),%ecx
	pxor	-16(%esp,%ecx,1),%xmm1
	movups	%xmm1,-16(%edi,%ecx,1)
	cmpl	%ebx,%ecx
	jb	.L052ctr32_xor
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jnz	.L050ctr32_loop
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L053ctr32_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L053ctr32_bzero
.L054ctr32_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	movl	$1,%eax
	leal	4(%esp),%esp
	emms
.L048ctr32_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_ctr32_encrypt,.-.L_padlock_ctr32_encrypt_begin
.globl	padlock_xstore
.type	padlock_xstore,@function
.align	16
padlock_xstore:
.L_padlock_xstore_begin:
	pushl	%edi
	movl	8(%esp),%edi
	movl	12(%esp),%edx
.byte	15,167,192
	popl	%edi
	ret
.size	padlock_xstore,.-.L_padlock_xstore_begin
.type	_win32_segv_handler,@function
.align	16
_win32_segv_handler:
	movl	$1,%eax
	movl	4(%esp),%edx
	movl	12(%esp),%ecx
	cmpl	$3221225477,(%edx)
	jne	.L055ret
	addl	$4,184(%ecx)
	movl	$0,%eax
.L055ret:
	ret
.size	_win32_segv_handler,.-_win32_segv_handler
.globl	padlock_sha1_oneshot
.type	padlock_sha1_oneshot,@function
.align	16
padlock_sha1_oneshot:
.L_padlock_sha1_oneshot_begin:
	pushl	%edi
	pushl	%esi
	xorl	%eax,%eax
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movl	16(%edi),%eax
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movl	%eax,16(%esp)
	xorl	%eax,%eax
.byte	243,15,166,200
	movaps	(%esp),%xmm0
	movl	16(%esp),%eax
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movl	%eax,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha1_oneshot,.-.L_padlock_sha1_oneshot_begin
.globl	padlock_sha1_blocks
.type	padlock_sha1_blocks,@function
.align	16
padlock_sha1_blocks:
.L_padlock_sha1_blocks_begin:
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	%esp,%edx
	movl	20(%esp),%ecx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movl	16(%edi),%eax
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movl	%eax,16(%esp)
	movl	$-1,%eax
.byte	243,15,166,200
	movaps	(%esp),%xmm0
	movl	16(%esp),%eax
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movl	%eax,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha1_blocks,.-.L_padlock_sha1_blocks_begin
.globl	padlock_sha256_oneshot
.type	padlock_sha256_oneshot,@function
.align	16
padlock_sha256_oneshot:
.L_padlock_sha256_oneshot_begin:
	pushl	%edi
	pushl	%esi
	xorl	%eax,%eax
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movups	16(%edi),%xmm1
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movaps	%xmm1,16(%esp)
	xorl	%eax,%eax
.byte	243,15,166,208
	movaps	(%esp),%xmm0
	movaps	16(%esp),%xmm1
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movups	%xmm1,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha256_oneshot,.-.L_padlock_sha256_oneshot_begin
.globl	padlock_sha256_blocks
.type	padlock_sha256_blocks,@function
.align	16
padlock_sha256_blocks:
.L_padlock_sha256_blocks_begin:
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movups	16(%edi),%xmm1
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movaps	%xmm1,16(%esp)
	movl	$-1,%eax
.byte	243,15,166,208
	movaps	(%esp),%xmm0
	movaps	16(%esp),%xmm1
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movups	%xmm1,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha256_blocks,.-.L_padlock_sha256_blocks_begin
.globl	padlock_sha512_blocks
.type	padlock_sha512_blocks,@function
.align	16
padlock_sha512_blocks:
.L_padlock_sha512_blocks_begin:
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movups	16(%edi),%xmm1
	movups	32(%edi),%xmm2
	movups	48(%edi),%xmm3
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movaps	%xmm1,16(%esp)
	movaps	%xmm2,32(%esp)
	movaps	%xmm3,48(%esp)
.byte	243,15,166,224
	movaps	(%esp),%xmm0
	movaps	16(%esp),%xmm1
	movaps	32(%esp),%xmm2
	movaps	48(%esp),%xmm3
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movups	%xmm1,16(%edi)
	movups	%xmm2,32(%edi)
	movups	%xmm3,48(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha512_blocks,.-.L_padlock_sha512_blocks_begin
.byte	86,73,65,32,80,97,100,108,111,99,107,32,120,56,54,32
.byte	109,111,100,117,108,101,44,32,67,82,89,80,84,79,71,65
.byte	77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101
.byte	110,115,115,108,46,111,114,103,62,0
.align	16
.data
.align	4
.Lpadlock_saved_context:
.long	0
#else
.text
.globl	padlock_capability
.type	padlock_capability,@function
.align	16
padlock_capability:
.L_padlock_capability_begin:
	pushl	%ebx
	pushfl
	popl	%eax
	movl	%eax,%ecx
	xorl	$2097152,%eax
	pushl	%eax
	popfl
	pushfl
	popl	%eax
	xorl	%eax,%ecx
	xorl	%eax,%eax
	btl	$21,%ecx
	jnc	.L000noluck
	.byte	0x0f,0xa2
	xorl	%eax,%eax
	cmpl	$0x746e6543,%ebx
	jne	.L001zhaoxin
	cmpl	$0x48727561,%edx
	jne	.L000noluck
	cmpl	$0x736c7561,%ecx
	jne	.L000noluck
	jmp	.L002zhaoxinEnd
.L001zhaoxin:
	cmpl	$0x68532020,%ebx
	jne	.L000noluck
	cmpl	$0x68676e61,%edx
	jne	.L000noluck
	cmpl	$0x20206961,%ecx
	jne	.L000noluck
.L002zhaoxinEnd:
	movl	$3221225472,%eax
	.byte	0x0f,0xa2
	movl	%eax,%edx
	xorl	%eax,%eax
	cmpl	$3221225473,%edx
	jb	.L000noluck
	movl	$1,%eax
	.byte	0x0f,0xa2
	orl	$15,%eax
	xorl	%ebx,%ebx
	andl	$4095,%eax
	cmpl	$1791,%eax
	sete	%bl
	movl	$3221225473,%eax
	pushl	%ebx
	.byte	0x0f,0xa2
	popl	%ebx
	movl	%edx,%eax
	shll	$4,%ebx
	andl	$4294967279,%eax
	orl	%ebx,%eax
.L000noluck:
	popl	%ebx
	ret
.size	padlock_capability,.-.L_padlock_capability_begin
.globl	padlock_key_bswap
.type	padlock_key_bswap,@function
.align	16
padlock_key_bswap:
.L_padlock_key_bswap_begin:
	movl	4(%esp),%edx
	movl	240(%edx),%ecx
.L003bswap_loop:
	movl	(%edx),%eax
	bswap	%eax
	movl	%eax,(%edx)
	leal	4(%edx),%edx
	subl	$1,%ecx
	jnz	.L003bswap_loop
	ret
.size	padlock_key_bswap,.-.L_padlock_key_bswap_begin
.globl	padlock_verify_context
.type	padlock_verify_context,@function
.align	16
padlock_verify_context:
.L_padlock_verify_context_begin:
	movl	4(%esp),%edx
	leal	.Lpadlock_saved_context-.L004verify_pic_point,%eax
	pushfl
	call	_padlock_verify_ctx
.L004verify_pic_point:
	leal	4(%esp),%esp
	ret
.size	padlock_verify_context,.-.L_padlock_verify_context_begin
.type	_padlock_verify_ctx,@function
.align	16
_padlock_verify_ctx:
	addl	(%esp),%eax
	btl	$30,4(%esp)
	jnc	.L005verified
	cmpl	(%eax),%edx
	je	.L005verified
	pushfl
	popfl
.L005verified:
	movl	%edx,(%eax)
	ret
.size	_padlock_verify_ctx,.-_padlock_verify_ctx
.globl	padlock_reload_key
.type	padlock_reload_key,@function
.align	16
padlock_reload_key:
.L_padlock_reload_key_begin:
	pushfl
	popfl
	ret
.size	padlock_reload_key,.-.L_padlock_reload_key_begin
.globl	padlock_aes_block
.type	padlock_aes_block,@function
.align	16
padlock_aes_block:
.L_padlock_aes_block_begin:
	pushl	%edi
	pushl	%esi
	pushl	%ebx
	movl	16(%esp),%edi
	movl	20(%esp),%esi
	movl	24(%esp),%edx
	movl	$1,%ecx
	leal	32(%edx),%ebx
	leal	16(%edx),%edx
.byte	243,15,167,200
	popl	%ebx
	popl	%esi
	popl	%edi
	ret
.size	padlock_aes_block,.-.L_padlock_aes_block_begin
.globl	padlock_ecb_encrypt
.type	padlock_ecb_encrypt,@function
.align	16
padlock_ecb_encrypt:
.L_padlock_ecb_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L006ecb_abort
	testl	$15,%ecx
	jnz	.L006ecb_abort
	leal	.Lpadlock_saved_context-.L007ecb_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L007ecb_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L008ecb_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L008ecb_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	cmpl	%ebx,%ecx
	ja	.L009ecb_loop
	movl	%esi,%eax
	cmpl	%esp,%ebp
	cmovel	%edi,%eax
	addl	%ecx,%eax
	negl	%eax
	andl	$4095,%eax
	cmpl	$128,%eax
	movl	$-128,%eax
	cmovael	%ebx,%eax
	andl	%eax,%ebx
	jz	.L010ecb_unaligned_tail
	jmp	.L009ecb_loop
.align	16
.L009ecb_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L011ecb_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L011ecb_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,200
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L012ecb_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L012ecb_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jz	.L013ecb_break
	cmpl	%ebx,%ecx
	jae	.L009ecb_loop
.L010ecb_unaligned_tail:
	xorl	%eax,%eax
	cmpl	%ebp,%esp
	cmovel	%ecx,%eax
	subl	%eax,%esp
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L009ecb_loop
.align	16
.L013ecb_break:
	cmpl	%ebp,%esp
	je	.L014ecb_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L015ecb_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L015ecb_bzero
.L014ecb_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L016ecb_exit
.align	16
.L008ecb_aligned:
	leal	(%esi,%ecx,1),%ebp
	negl	%ebp
	andl	$4095,%ebp
	xorl	%eax,%eax
	cmpl	$128,%ebp
	movl	$127,%ebp
	cmovael	%eax,%ebp
	andl	%ecx,%ebp
	subl	%ebp,%ecx
	jz	.L017ecb_aligned_tail
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,200
	testl	%ebp,%ebp
	jz	.L016ecb_exit
.L017ecb_aligned_tail:
	movl	%ebp,%ecx
	leal	-24(%esp),%ebp
	movl	%ebp,%esp
	movl	%ebp,%eax
	subl	%ecx,%esp
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L009ecb_loop
.L016ecb_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L006ecb_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_ecb_encrypt,.-.L_padlock_ecb_encrypt_begin
.globl	padlock_cbc_encrypt
.type	padlock_cbc_encrypt,@function
.align	16
padlock_cbc_encrypt:
.L_padlock_cbc_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L018cbc_abort
	testl	$15,%ecx
	jnz	.L018cbc_abort
	leal	.Lpadlock_saved_context-.L019cbc_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L019cbc_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L020cbc_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L020cbc_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	cmpl	%ebx,%ecx
	ja	.L021cbc_loop
	movl	%esi,%eax
	cmpl	%esp,%ebp
	cmovel	%edi,%eax
	addl	%ecx,%eax
	negl	%eax
	andl	$4095,%eax
	cmpl	$64,%eax
	movl	$-64,%eax
	cmovael	%ebx,%eax
	andl	%eax,%ebx
	jz	.L022cbc_unaligned_tail
	jmp	.L021cbc_loop
.align	16
.L021cbc_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L023cbc_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L023cbc_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,208
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L024cbc_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L024cbc_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jz	.L025cbc_break
	cmpl	%ebx,%ecx
	jae	.L021cbc_loop
.L022cbc_unaligned_tail:
	xorl	%eax,%eax
	cmpl	%ebp,%esp
	cmovel	%ecx,%eax
	subl	%eax,%esp
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L021cbc_loop
.align	16
.L025cbc_break:
	cmpl	%ebp,%esp
	je	.L026cbc_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L027cbc_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L027cbc_bzero
.L026cbc_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L028cbc_exit
.align	16
.L020cbc_aligned:
	leal	(%esi,%ecx,1),%ebp
	negl	%ebp
	andl	$4095,%ebp
	xorl	%eax,%eax
	cmpl	$64,%ebp
	movl	$63,%ebp
	cmovael	%eax,%ebp
	andl	%ecx,%ebp
	subl	%ebp,%ecx
	jz	.L029cbc_aligned_tail
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,208
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	testl	%ebp,%ebp
	jz	.L028cbc_exit
.L029cbc_aligned_tail:
	movl	%ebp,%ecx
	leal	-24(%esp),%ebp
	movl	%ebp,%esp
	movl	%ebp,%eax
	subl	%ecx,%esp
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	movl	%edi,%eax
	movl	%ecx,%ebx
	shrl	$2,%ecx
	leal	(%esp),%edi
.byte	243,165
	movl	%esp,%esi
	movl	%eax,%edi
	movl	%ebx,%ecx
	jmp	.L021cbc_loop
.L028cbc_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L018cbc_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_cbc_encrypt,.-.L_padlock_cbc_encrypt_begin
.globl	padlock_cfb_encrypt
.type	padlock_cfb_encrypt,@function
.align	16
padlock_cfb_encrypt:
.L_padlock_cfb_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L030cfb_abort
	testl	$15,%ecx
	jnz	.L030cfb_abort
	leal	.Lpadlock_saved_context-.L031cfb_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L031cfb_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L032cfb_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L032cfb_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	jmp	.L033cfb_loop
.align	16
.L033cfb_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L034cfb_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L034cfb_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,224
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L035cfb_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L035cfb_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jnz	.L033cfb_loop
	cmpl	%ebp,%esp
	je	.L036cfb_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L037cfb_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L037cfb_bzero
.L036cfb_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L038cfb_exit
.align	16
.L032cfb_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,224
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
.L038cfb_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L030cfb_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_cfb_encrypt,.-.L_padlock_cfb_encrypt_begin
.globl	padlock_ofb_encrypt
.type	padlock_ofb_encrypt,@function
.align	16
padlock_ofb_encrypt:
.L_padlock_ofb_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L039ofb_abort
	testl	$15,%ecx
	jnz	.L039ofb_abort
	leal	.Lpadlock_saved_context-.L040ofb_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L040ofb_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	xorl	%ebx,%ebx
	testl	$32,(%edx)
	jnz	.L041ofb_aligned
	testl	$15,%edi
	setz	%al
	testl	$15,%esi
	setz	%bl
	testl	%ebx,%eax
	jnz	.L041ofb_aligned
	negl	%eax
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	jmp	.L042ofb_loop
.align	16
.L042ofb_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	testl	$15,%edi
	cmovnzl	%esp,%edi
	testl	$15,%esi
	jz	.L043ofb_inp_aligned
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
	movl	%ebx,%ecx
	movl	%edi,%esi
.L043ofb_inp_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,232
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	testl	$15,%edi
	jz	.L044ofb_out_aligned
	movl	%ebx,%ecx
	leal	(%esp),%esi
	shrl	$2,%ecx
.byte	243,165
	subl	%ebx,%edi
.L044ofb_out_aligned:
	movl	4(%ebp),%esi
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jnz	.L042ofb_loop
	cmpl	%ebp,%esp
	je	.L045ofb_done
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L046ofb_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L046ofb_bzero
.L045ofb_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	jmp	.L047ofb_exit
.align	16
.L041ofb_aligned:
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,232
	movaps	(%eax),%xmm0
	movaps	%xmm0,-16(%edx)
.L047ofb_exit:
	movl	$1,%eax
	leal	4(%esp),%esp
.L039ofb_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_ofb_encrypt,.-.L_padlock_ofb_encrypt_begin
.globl	padlock_ctr32_encrypt
.type	padlock_ctr32_encrypt,@function
.align	16
padlock_ctr32_encrypt:
.L_padlock_ctr32_encrypt_begin:
	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%edx
	movl	32(%esp),%ecx
	testl	$15,%edx
	jnz	.L048ctr32_abort
	testl	$15,%ecx
	jnz	.L048ctr32_abort
	leal	.Lpadlock_saved_context-.L049ctr32_pic_point,%eax
	pushfl
	cld
	call	_padlock_verify_ctx
.L049ctr32_pic_point:
	leal	16(%edx),%edx
	xorl	%eax,%eax
	movq	-16(%edx),%mm0
	movl	$512,%ebx
	notl	%eax
	leal	-24(%esp),%ebp
	cmpl	%ebx,%ecx
	cmovcl	%ecx,%ebx
	andl	%ebx,%eax
	movl	%ecx,%ebx
	negl	%eax
	andl	$511,%ebx
	leal	(%eax,%ebp,1),%esp
	movl	$512,%eax
	cmovzl	%eax,%ebx
	movl	%ebp,%eax
	andl	$-16,%ebp
	andl	$-16,%esp
	movl	%eax,16(%ebp)
	jmp	.L050ctr32_loop
.align	16
.L050ctr32_loop:
	movl	%edi,(%ebp)
	movl	%esi,4(%ebp)
	movl	%ecx,8(%ebp)
	movl	%ebx,%ecx
	movl	%ebx,12(%ebp)
	movl	-4(%edx),%ecx
	xorl	%edi,%edi
	movl	-8(%edx),%eax
.L051ctr32_prepare:
	movl	%ecx,12(%esp,%edi,1)
	bswap	%ecx
	movq	%mm0,(%esp,%edi,1)
	incl	%ecx
	movl	%eax,8(%esp,%edi,1)
	bswap	%ecx
	leal	16(%edi),%edi
	cmpl	%ebx,%edi
	jb	.L051ctr32_prepare
	movl	%ecx,-4(%edx)
	leal	(%esp),%esi
	leal	(%esp),%edi
	movl	%ebx,%ecx
	leal	-16(%edx),%eax
	leal	16(%edx),%ebx
	shrl	$4,%ecx
.byte	243,15,167,200
	movl	(%ebp),%edi
	movl	12(%ebp),%ebx
	movl	4(%ebp),%esi
	xorl	%ecx,%ecx
.L052ctr32_xor:
	movups	(%esi,%ecx,1),%xmm1
	leal	16(%ecx),%ecx
	pxor	-16(%esp,%ecx,1),%xmm1
	movups	%xmm1,-16(%edi,%ecx,1)
	cmpl	%ebx,%ecx
	jb	.L052ctr32_xor
	movl	8(%ebp),%ecx
	addl	%ebx,%edi
	addl	%ebx,%esi
	subl	%ebx,%ecx
	movl	$512,%ebx
	jnz	.L050ctr32_loop
	pxor	%xmm0,%xmm0
	leal	(%esp),%eax
.L053ctr32_bzero:
	movaps	%xmm0,(%eax)
	leal	16(%eax),%eax
	cmpl	%eax,%ebp
	ja	.L053ctr32_bzero
.L054ctr32_done:
	movl	16(%ebp),%ebp
	leal	24(%ebp),%esp
	movl	$1,%eax
	leal	4(%esp),%esp
	emms
.L048ctr32_abort:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	padlock_ctr32_encrypt,.-.L_padlock_ctr32_encrypt_begin
.globl	padlock_xstore
.type	padlock_xstore,@function
.align	16
padlock_xstore:
.L_padlock_xstore_begin:
	pushl	%edi
	movl	8(%esp),%edi
	movl	12(%esp),%edx
.byte	15,167,192
	popl	%edi
	ret
.size	padlock_xstore,.-.L_padlock_xstore_begin
.type	_win32_segv_handler,@function
.align	16
_win32_segv_handler:
	movl	$1,%eax
	movl	4(%esp),%edx
	movl	12(%esp),%ecx
	cmpl	$3221225477,(%edx)
	jne	.L055ret
	addl	$4,184(%ecx)
	movl	$0,%eax
.L055ret:
	ret
.size	_win32_segv_handler,.-_win32_segv_handler
.globl	padlock_sha1_oneshot
.type	padlock_sha1_oneshot,@function
.align	16
padlock_sha1_oneshot:
.L_padlock_sha1_oneshot_begin:
	pushl	%edi
	pushl	%esi
	xorl	%eax,%eax
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movl	16(%edi),%eax
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movl	%eax,16(%esp)
	xorl	%eax,%eax
.byte	243,15,166,200
	movaps	(%esp),%xmm0
	movl	16(%esp),%eax
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movl	%eax,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha1_oneshot,.-.L_padlock_sha1_oneshot_begin
.globl	padlock_sha1_blocks
.type	padlock_sha1_blocks,@function
.align	16
padlock_sha1_blocks:
.L_padlock_sha1_blocks_begin:
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	%esp,%edx
	movl	20(%esp),%ecx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movl	16(%edi),%eax
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movl	%eax,16(%esp)
	movl	$-1,%eax
.byte	243,15,166,200
	movaps	(%esp),%xmm0
	movl	16(%esp),%eax
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movl	%eax,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha1_blocks,.-.L_padlock_sha1_blocks_begin
.globl	padlock_sha256_oneshot
.type	padlock_sha256_oneshot,@function
.align	16
padlock_sha256_oneshot:
.L_padlock_sha256_oneshot_begin:
	pushl	%edi
	pushl	%esi
	xorl	%eax,%eax
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movups	16(%edi),%xmm1
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movaps	%xmm1,16(%esp)
	xorl	%eax,%eax
.byte	243,15,166,208
	movaps	(%esp),%xmm0
	movaps	16(%esp),%xmm1
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movups	%xmm1,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha256_oneshot,.-.L_padlock_sha256_oneshot_begin
.globl	padlock_sha256_blocks
.type	padlock_sha256_blocks,@function
.align	16
padlock_sha256_blocks:
.L_padlock_sha256_blocks_begin:
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movups	16(%edi),%xmm1
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movaps	%xmm1,16(%esp)
	movl	$-1,%eax
.byte	243,15,166,208
	movaps	(%esp),%xmm0
	movaps	16(%esp),%xmm1
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movups	%xmm1,16(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha256_blocks,.-.L_padlock_sha256_blocks_begin
.globl	padlock_sha512_blocks
.type	padlock_sha512_blocks,@function
.align	16
padlock_sha512_blocks:
.L_padlock_sha512_blocks_begin:
	pushl	%edi
	pushl	%esi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	movl	%esp,%edx
	addl	$-128,%esp
	movups	(%edi),%xmm0
	andl	$-16,%esp
	movups	16(%edi),%xmm1
	movups	32(%edi),%xmm2
	movups	48(%edi),%xmm3
	movaps	%xmm0,(%esp)
	movl	%esp,%edi
	movaps	%xmm1,16(%esp)
	movaps	%xmm2,32(%esp)
	movaps	%xmm3,48(%esp)
.byte	243,15,166,224
	movaps	(%esp),%xmm0
	movaps	16(%esp),%xmm1
	movaps	32(%esp),%xmm2
	movaps	48(%esp),%xmm3
	movl	%edx,%esp
	movl	12(%esp),%edi
	movups	%xmm0,(%edi)
	movups	%xmm1,16(%edi)
	movups	%xmm2,32(%edi)
	movups	%xmm3,48(%edi)
	popl	%esi
	popl	%edi
	ret
.size	padlock_sha512_blocks,.-.L_padlock_sha512_blocks_begin
.byte	86,73,65,32,80,97,100,108,111,99,107,32,120,56,54,32
.byte	109,111,100,117,108,101,44,32,67,82,89,80,84,79,71,65
.byte	77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101
.byte	110,115,115,108,46,111,114,103,62,0
.align	16
.data
.align	4
.Lpadlock_saved_context:
.long	0
#endif
