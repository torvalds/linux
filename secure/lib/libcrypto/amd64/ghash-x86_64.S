/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from ghash-x86_64.pl. */
.text	


.globl	gcm_gmult_4bit
.type	gcm_gmult_4bit,@function
.align	16
gcm_gmult_4bit:
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
	subq	$280,%rsp
.cfi_adjust_cfa_offset	280
.Lgmult_prologue:

	movzbq	15(%rdi),%r8
	leaq	.Lrem_4bit(%rip),%r11
	xorq	%rax,%rax
	xorq	%rbx,%rbx
	movb	%r8b,%al
	movb	%r8b,%bl
	shlb	$4,%al
	movq	$14,%rcx
	movq	8(%rsi,%rax,1),%r8
	movq	(%rsi,%rax,1),%r9
	andb	$0xf0,%bl
	movq	%r8,%rdx
	jmp	.Loop1

.align	16
.Loop1:
	shrq	$4,%r8
	andq	$0xf,%rdx
	movq	%r9,%r10
	movb	(%rdi,%rcx,1),%al
	shrq	$4,%r9
	xorq	8(%rsi,%rbx,1),%r8
	shlq	$60,%r10
	xorq	(%rsi,%rbx,1),%r9
	movb	%al,%bl
	xorq	(%r11,%rdx,8),%r9
	movq	%r8,%rdx
	shlb	$4,%al
	xorq	%r10,%r8
	decq	%rcx
	js	.Lbreak1

	shrq	$4,%r8
	andq	$0xf,%rdx
	movq	%r9,%r10
	shrq	$4,%r9
	xorq	8(%rsi,%rax,1),%r8
	shlq	$60,%r10
	xorq	(%rsi,%rax,1),%r9
	andb	$0xf0,%bl
	xorq	(%r11,%rdx,8),%r9
	movq	%r8,%rdx
	xorq	%r10,%r8
	jmp	.Loop1

.align	16
.Lbreak1:
	shrq	$4,%r8
	andq	$0xf,%rdx
	movq	%r9,%r10
	shrq	$4,%r9
	xorq	8(%rsi,%rax,1),%r8
	shlq	$60,%r10
	xorq	(%rsi,%rax,1),%r9
	andb	$0xf0,%bl
	xorq	(%r11,%rdx,8),%r9
	movq	%r8,%rdx
	xorq	%r10,%r8

	shrq	$4,%r8
	andq	$0xf,%rdx
	movq	%r9,%r10
	shrq	$4,%r9
	xorq	8(%rsi,%rbx,1),%r8
	shlq	$60,%r10
	xorq	(%rsi,%rbx,1),%r9
	xorq	%r10,%r8
	xorq	(%r11,%rdx,8),%r9

	bswapq	%r8
	bswapq	%r9
	movq	%r8,8(%rdi)
	movq	%r9,(%rdi)

	leaq	280+48(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	movq	-8(%rsi),%rbx
.cfi_restore	%rbx
	leaq	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lgmult_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_gmult_4bit,.-gcm_gmult_4bit
.globl	gcm_ghash_4bit
.type	gcm_ghash_4bit,@function
.align	16
gcm_ghash_4bit:
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
	subq	$280,%rsp
.cfi_adjust_cfa_offset	280
.Lghash_prologue:
	movq	%rdx,%r14
	movq	%rcx,%r15
	subq	$-128,%rsi
	leaq	16+128(%rsp),%rbp
	xorl	%edx,%edx
	movq	0+0-128(%rsi),%r8
	movq	0+8-128(%rsi),%rax
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	16+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	16+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,0(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,0(%rbp)
	movq	32+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,0-128(%rbp)
	movq	32+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,1(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,8(%rbp)
	movq	48+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,8-128(%rbp)
	movq	48+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,2(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,16(%rbp)
	movq	64+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,16-128(%rbp)
	movq	64+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,3(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,24(%rbp)
	movq	80+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,24-128(%rbp)
	movq	80+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,4(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,32(%rbp)
	movq	96+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,32-128(%rbp)
	movq	96+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,5(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,40(%rbp)
	movq	112+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,40-128(%rbp)
	movq	112+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,6(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,48(%rbp)
	movq	128+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,48-128(%rbp)
	movq	128+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,7(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,56(%rbp)
	movq	144+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,56-128(%rbp)
	movq	144+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,8(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,64(%rbp)
	movq	160+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,64-128(%rbp)
	movq	160+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,9(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,72(%rbp)
	movq	176+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,72-128(%rbp)
	movq	176+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,10(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,80(%rbp)
	movq	192+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,80-128(%rbp)
	movq	192+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,11(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,88(%rbp)
	movq	208+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,88-128(%rbp)
	movq	208+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,12(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,96(%rbp)
	movq	224+0-128(%rsi),%r8
	shlb	$4,%dl
	movq	%rax,96-128(%rbp)
	movq	224+8-128(%rsi),%rax
	shlq	$60,%r10
	movb	%dl,13(%rsp)
	orq	%r10,%rbx
	movb	%al,%dl
	shrq	$4,%rax
	movq	%r8,%r10
	shrq	$4,%r8
	movq	%r9,104(%rbp)
	movq	240+0-128(%rsi),%r9
	shlb	$4,%dl
	movq	%rbx,104-128(%rbp)
	movq	240+8-128(%rsi),%rbx
	shlq	$60,%r10
	movb	%dl,14(%rsp)
	orq	%r10,%rax
	movb	%bl,%dl
	shrq	$4,%rbx
	movq	%r9,%r10
	shrq	$4,%r9
	movq	%r8,112(%rbp)
	shlb	$4,%dl
	movq	%rax,112-128(%rbp)
	shlq	$60,%r10
	movb	%dl,15(%rsp)
	orq	%r10,%rbx
	movq	%r9,120(%rbp)
	movq	%rbx,120-128(%rbp)
	addq	$-128,%rsi
	movq	8(%rdi),%r8
	movq	0(%rdi),%r9
	addq	%r14,%r15
	leaq	.Lrem_8bit(%rip),%r11
	jmp	.Louter_loop
.align	16
.Louter_loop:
	xorq	(%r14),%r9
	movq	8(%r14),%rdx
	leaq	16(%r14),%r14
	xorq	%r8,%rdx
	movq	%r9,(%rdi)
	movq	%rdx,8(%rdi)
	shrq	$32,%rdx
	xorq	%rax,%rax
	roll	$8,%edx
	movb	%dl,%al
	movzbl	%dl,%ebx
	shlb	$4,%al
	shrl	$4,%ebx
	roll	$8,%edx
	movq	8(%rsi,%rax,1),%r8
	movq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	xorq	%r8,%r12
	movq	%r9,%r10
	shrq	$8,%r8
	movzbq	%r12b,%r12
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	movl	8(%rdi),%edx
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	movl	4(%rdi),%edx
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	movl	0(%rdi),%edx
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	shrl	$4,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r12,2),%r12
	movzbl	%dl,%ebx
	shlb	$4,%al
	movzbq	(%rsp,%rcx,1),%r13
	shrl	$4,%ebx
	shlq	$48,%r12
	xorq	%r8,%r13
	movq	%r9,%r10
	xorq	%r12,%r9
	shrq	$8,%r8
	movzbq	%r13b,%r13
	shrq	$8,%r9
	xorq	-128(%rbp,%rcx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rcx,8),%r9
	roll	$8,%edx
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	movb	%dl,%al
	xorq	%r10,%r8
	movzwq	(%r11,%r13,2),%r13
	movzbl	%dl,%ecx
	shlb	$4,%al
	movzbq	(%rsp,%rbx,1),%r12
	andl	$240,%ecx
	shlq	$48,%r13
	xorq	%r8,%r12
	movq	%r9,%r10
	xorq	%r13,%r9
	shrq	$8,%r8
	movzbq	%r12b,%r12
	movl	-4(%rdi),%edx
	shrq	$8,%r9
	xorq	-128(%rbp,%rbx,8),%r8
	shlq	$56,%r10
	xorq	(%rbp,%rbx,8),%r9
	movzwq	(%r11,%r12,2),%r12
	xorq	8(%rsi,%rax,1),%r8
	xorq	(%rsi,%rax,1),%r9
	shlq	$48,%r12
	xorq	%r10,%r8
	xorq	%r12,%r9
	movzbq	%r8b,%r13
	shrq	$4,%r8
	movq	%r9,%r10
	shlb	$4,%r13b
	shrq	$4,%r9
	xorq	8(%rsi,%rcx,1),%r8
	movzwq	(%r11,%r13,2),%r13
	shlq	$60,%r10
	xorq	(%rsi,%rcx,1),%r9
	xorq	%r10,%r8
	shlq	$48,%r13
	bswapq	%r8
	xorq	%r13,%r9
	bswapq	%r9
	cmpq	%r15,%r14
	jb	.Louter_loop
	movq	%r8,8(%rdi)
	movq	%r9,(%rdi)

	leaq	280+48(%rsp),%rsi
.cfi_def_cfa	%rsi,8
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
	leaq	0(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lghash_epilogue:
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_ghash_4bit,.-gcm_ghash_4bit
.globl	gcm_init_clmul
.type	gcm_init_clmul,@function
.align	16
gcm_init_clmul:
.cfi_startproc	
.L_init_clmul:
	movdqu	(%rsi),%xmm2
	pshufd	$78,%xmm2,%xmm2


	pshufd	$255,%xmm2,%xmm4
	movdqa	%xmm2,%xmm3
	psllq	$1,%xmm2
	pxor	%xmm5,%xmm5
	psrlq	$63,%xmm3
	pcmpgtd	%xmm4,%xmm5
	pslldq	$8,%xmm3
	por	%xmm3,%xmm2


	pand	.L0x1c2_polynomial(%rip),%xmm5
	pxor	%xmm5,%xmm2


	pshufd	$78,%xmm2,%xmm6
	movdqa	%xmm2,%xmm0
	pxor	%xmm2,%xmm6
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm3
	pxor	%xmm0,%xmm3
.byte	102,15,58,68,194,0
.byte	102,15,58,68,202,17
.byte	102,15,58,68,222,0
	pxor	%xmm0,%xmm3
	pxor	%xmm1,%xmm3

	movdqa	%xmm3,%xmm4
	psrldq	$8,%xmm3
	pslldq	$8,%xmm4
	pxor	%xmm3,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
	pshufd	$78,%xmm2,%xmm3
	pshufd	$78,%xmm0,%xmm4
	pxor	%xmm2,%xmm3
	movdqu	%xmm2,0(%rdi)
	pxor	%xmm0,%xmm4
	movdqu	%xmm0,16(%rdi)
.byte	102,15,58,15,227,8
	movdqu	%xmm4,32(%rdi)
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm3
	pxor	%xmm0,%xmm3
.byte	102,15,58,68,194,0
.byte	102,15,58,68,202,17
.byte	102,15,58,68,222,0
	pxor	%xmm0,%xmm3
	pxor	%xmm1,%xmm3

	movdqa	%xmm3,%xmm4
	psrldq	$8,%xmm3
	pslldq	$8,%xmm4
	pxor	%xmm3,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
	movdqa	%xmm0,%xmm5
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm3
	pxor	%xmm0,%xmm3
.byte	102,15,58,68,194,0
.byte	102,15,58,68,202,17
.byte	102,15,58,68,222,0
	pxor	%xmm0,%xmm3
	pxor	%xmm1,%xmm3

	movdqa	%xmm3,%xmm4
	psrldq	$8,%xmm3
	pslldq	$8,%xmm4
	pxor	%xmm3,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
	pshufd	$78,%xmm5,%xmm3
	pshufd	$78,%xmm0,%xmm4
	pxor	%xmm5,%xmm3
	movdqu	%xmm5,48(%rdi)
	pxor	%xmm0,%xmm4
	movdqu	%xmm0,64(%rdi)
.byte	102,15,58,15,227,8
	movdqu	%xmm4,80(%rdi)
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_init_clmul,.-gcm_init_clmul
.globl	gcm_gmult_clmul
.type	gcm_gmult_clmul,@function
.align	16
gcm_gmult_clmul:
.cfi_startproc	
.L_gmult_clmul:
	movdqu	(%rdi),%xmm0
	movdqa	.Lbswap_mask(%rip),%xmm5
	movdqu	(%rsi),%xmm2
	movdqu	32(%rsi),%xmm4
.byte	102,15,56,0,197
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm3
	pxor	%xmm0,%xmm3
.byte	102,15,58,68,194,0
.byte	102,15,58,68,202,17
.byte	102,15,58,68,220,0
	pxor	%xmm0,%xmm3
	pxor	%xmm1,%xmm3

	movdqa	%xmm3,%xmm4
	psrldq	$8,%xmm3
	pslldq	$8,%xmm4
	pxor	%xmm3,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
.byte	102,15,56,0,197
	movdqu	%xmm0,(%rdi)
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_gmult_clmul,.-gcm_gmult_clmul
.globl	gcm_ghash_clmul
.type	gcm_ghash_clmul,@function
.align	32
gcm_ghash_clmul:
.cfi_startproc	
.L_ghash_clmul:
	movdqa	.Lbswap_mask(%rip),%xmm10

	movdqu	(%rdi),%xmm0
	movdqu	(%rsi),%xmm2
	movdqu	32(%rsi),%xmm7
.byte	102,65,15,56,0,194

	subq	$0x10,%rcx
	jz	.Lodd_tail

	movdqu	16(%rsi),%xmm6
	movl	OPENSSL_ia32cap_P+4(%rip),%eax
	cmpq	$0x30,%rcx
	jb	.Lskip4x

	andl	$71303168,%eax
	cmpl	$4194304,%eax
	je	.Lskip4x

	subq	$0x30,%rcx
	movq	$0xA040608020C0E000,%rax
	movdqu	48(%rsi),%xmm14
	movdqu	64(%rsi),%xmm15




	movdqu	48(%rdx),%xmm3
	movdqu	32(%rdx),%xmm11
.byte	102,65,15,56,0,218
.byte	102,69,15,56,0,218
	movdqa	%xmm3,%xmm5
	pshufd	$78,%xmm3,%xmm4
	pxor	%xmm3,%xmm4
.byte	102,15,58,68,218,0
.byte	102,15,58,68,234,17
.byte	102,15,58,68,231,0

	movdqa	%xmm11,%xmm13
	pshufd	$78,%xmm11,%xmm12
	pxor	%xmm11,%xmm12
.byte	102,68,15,58,68,222,0
.byte	102,68,15,58,68,238,17
.byte	102,68,15,58,68,231,16
	xorps	%xmm11,%xmm3
	xorps	%xmm13,%xmm5
	movups	80(%rsi),%xmm7
	xorps	%xmm12,%xmm4

	movdqu	16(%rdx),%xmm11
	movdqu	0(%rdx),%xmm8
.byte	102,69,15,56,0,218
.byte	102,69,15,56,0,194
	movdqa	%xmm11,%xmm13
	pshufd	$78,%xmm11,%xmm12
	pxor	%xmm8,%xmm0
	pxor	%xmm11,%xmm12
.byte	102,69,15,58,68,222,0
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm8
	pxor	%xmm0,%xmm8
.byte	102,69,15,58,68,238,17
.byte	102,68,15,58,68,231,0
	xorps	%xmm11,%xmm3
	xorps	%xmm13,%xmm5

	leaq	64(%rdx),%rdx
	subq	$0x40,%rcx
	jc	.Ltail4x

	jmp	.Lmod4_loop
.align	32
.Lmod4_loop:
.byte	102,65,15,58,68,199,0
	xorps	%xmm12,%xmm4
	movdqu	48(%rdx),%xmm11
.byte	102,69,15,56,0,218
.byte	102,65,15,58,68,207,17
	xorps	%xmm3,%xmm0
	movdqu	32(%rdx),%xmm3
	movdqa	%xmm11,%xmm13
.byte	102,68,15,58,68,199,16
	pshufd	$78,%xmm11,%xmm12
	xorps	%xmm5,%xmm1
	pxor	%xmm11,%xmm12
.byte	102,65,15,56,0,218
	movups	32(%rsi),%xmm7
	xorps	%xmm4,%xmm8
.byte	102,68,15,58,68,218,0
	pshufd	$78,%xmm3,%xmm4

	pxor	%xmm0,%xmm8
	movdqa	%xmm3,%xmm5
	pxor	%xmm1,%xmm8
	pxor	%xmm3,%xmm4
	movdqa	%xmm8,%xmm9
.byte	102,68,15,58,68,234,17
	pslldq	$8,%xmm8
	psrldq	$8,%xmm9
	pxor	%xmm8,%xmm0
	movdqa	.L7_mask(%rip),%xmm8
	pxor	%xmm9,%xmm1
.byte	102,76,15,110,200

	pand	%xmm0,%xmm8
.byte	102,69,15,56,0,200
	pxor	%xmm0,%xmm9
.byte	102,68,15,58,68,231,0
	psllq	$57,%xmm9
	movdqa	%xmm9,%xmm8
	pslldq	$8,%xmm9
.byte	102,15,58,68,222,0
	psrldq	$8,%xmm8
	pxor	%xmm9,%xmm0
	pxor	%xmm8,%xmm1
	movdqu	0(%rdx),%xmm8

	movdqa	%xmm0,%xmm9
	psrlq	$1,%xmm0
.byte	102,15,58,68,238,17
	xorps	%xmm11,%xmm3
	movdqu	16(%rdx),%xmm11
.byte	102,69,15,56,0,218
.byte	102,15,58,68,231,16
	xorps	%xmm13,%xmm5
	movups	80(%rsi),%xmm7
.byte	102,69,15,56,0,194
	pxor	%xmm9,%xmm1
	pxor	%xmm0,%xmm9
	psrlq	$5,%xmm0

	movdqa	%xmm11,%xmm13
	pxor	%xmm12,%xmm4
	pshufd	$78,%xmm11,%xmm12
	pxor	%xmm9,%xmm0
	pxor	%xmm8,%xmm1
	pxor	%xmm11,%xmm12
.byte	102,69,15,58,68,222,0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
	movdqa	%xmm0,%xmm1
.byte	102,69,15,58,68,238,17
	xorps	%xmm11,%xmm3
	pshufd	$78,%xmm0,%xmm8
	pxor	%xmm0,%xmm8

.byte	102,68,15,58,68,231,0
	xorps	%xmm13,%xmm5

	leaq	64(%rdx),%rdx
	subq	$0x40,%rcx
	jnc	.Lmod4_loop

.Ltail4x:
.byte	102,65,15,58,68,199,0
.byte	102,65,15,58,68,207,17
.byte	102,68,15,58,68,199,16
	xorps	%xmm12,%xmm4
	xorps	%xmm3,%xmm0
	xorps	%xmm5,%xmm1
	pxor	%xmm0,%xmm1
	pxor	%xmm4,%xmm8

	pxor	%xmm1,%xmm8
	pxor	%xmm0,%xmm1

	movdqa	%xmm8,%xmm9
	psrldq	$8,%xmm8
	pslldq	$8,%xmm9
	pxor	%xmm8,%xmm1
	pxor	%xmm9,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
	addq	$0x40,%rcx
	jz	.Ldone
	movdqu	32(%rsi),%xmm7
	subq	$0x10,%rcx
	jz	.Lodd_tail
.Lskip4x:





	movdqu	(%rdx),%xmm8
	movdqu	16(%rdx),%xmm3
.byte	102,69,15,56,0,194
.byte	102,65,15,56,0,218
	pxor	%xmm8,%xmm0

	movdqa	%xmm3,%xmm5
	pshufd	$78,%xmm3,%xmm4
	pxor	%xmm3,%xmm4
.byte	102,15,58,68,218,0
.byte	102,15,58,68,234,17
.byte	102,15,58,68,231,0

	leaq	32(%rdx),%rdx
	nop
	subq	$0x20,%rcx
	jbe	.Leven_tail
	nop
	jmp	.Lmod_loop

.align	32
.Lmod_loop:
	movdqa	%xmm0,%xmm1
	movdqa	%xmm4,%xmm8
	pshufd	$78,%xmm0,%xmm4
	pxor	%xmm0,%xmm4

.byte	102,15,58,68,198,0
.byte	102,15,58,68,206,17
.byte	102,15,58,68,231,16

	pxor	%xmm3,%xmm0
	pxor	%xmm5,%xmm1
	movdqu	(%rdx),%xmm9
	pxor	%xmm0,%xmm8
.byte	102,69,15,56,0,202
	movdqu	16(%rdx),%xmm3

	pxor	%xmm1,%xmm8
	pxor	%xmm9,%xmm1
	pxor	%xmm8,%xmm4
.byte	102,65,15,56,0,218
	movdqa	%xmm4,%xmm8
	psrldq	$8,%xmm8
	pslldq	$8,%xmm4
	pxor	%xmm8,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm3,%xmm5

	movdqa	%xmm0,%xmm9
	movdqa	%xmm0,%xmm8
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm8
.byte	102,15,58,68,218,0
	psllq	$1,%xmm0
	pxor	%xmm8,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm8
	pslldq	$8,%xmm0
	psrldq	$8,%xmm8
	pxor	%xmm9,%xmm0
	pshufd	$78,%xmm5,%xmm4
	pxor	%xmm8,%xmm1
	pxor	%xmm5,%xmm4

	movdqa	%xmm0,%xmm9
	psrlq	$1,%xmm0
.byte	102,15,58,68,234,17
	pxor	%xmm9,%xmm1
	pxor	%xmm0,%xmm9
	psrlq	$5,%xmm0
	pxor	%xmm9,%xmm0
	leaq	32(%rdx),%rdx
	psrlq	$1,%xmm0
.byte	102,15,58,68,231,0
	pxor	%xmm1,%xmm0

	subq	$0x20,%rcx
	ja	.Lmod_loop

.Leven_tail:
	movdqa	%xmm0,%xmm1
	movdqa	%xmm4,%xmm8
	pshufd	$78,%xmm0,%xmm4
	pxor	%xmm0,%xmm4

.byte	102,15,58,68,198,0
.byte	102,15,58,68,206,17
.byte	102,15,58,68,231,16

	pxor	%xmm3,%xmm0
	pxor	%xmm5,%xmm1
	pxor	%xmm0,%xmm8
	pxor	%xmm1,%xmm8
	pxor	%xmm8,%xmm4
	movdqa	%xmm4,%xmm8
	psrldq	$8,%xmm8
	pslldq	$8,%xmm4
	pxor	%xmm8,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
	testq	%rcx,%rcx
	jnz	.Ldone

.Lodd_tail:
	movdqu	(%rdx),%xmm8
.byte	102,69,15,56,0,194
	pxor	%xmm8,%xmm0
	movdqa	%xmm0,%xmm1
	pshufd	$78,%xmm0,%xmm3
	pxor	%xmm0,%xmm3
.byte	102,15,58,68,194,0
.byte	102,15,58,68,202,17
.byte	102,15,58,68,223,0
	pxor	%xmm0,%xmm3
	pxor	%xmm1,%xmm3

	movdqa	%xmm3,%xmm4
	psrldq	$8,%xmm3
	pslldq	$8,%xmm4
	pxor	%xmm3,%xmm1
	pxor	%xmm4,%xmm0

	movdqa	%xmm0,%xmm4
	movdqa	%xmm0,%xmm3
	psllq	$5,%xmm0
	pxor	%xmm0,%xmm3
	psllq	$1,%xmm0
	pxor	%xmm3,%xmm0
	psllq	$57,%xmm0
	movdqa	%xmm0,%xmm3
	pslldq	$8,%xmm0
	psrldq	$8,%xmm3
	pxor	%xmm4,%xmm0
	pxor	%xmm3,%xmm1


	movdqa	%xmm0,%xmm4
	psrlq	$1,%xmm0
	pxor	%xmm4,%xmm1
	pxor	%xmm0,%xmm4
	psrlq	$5,%xmm0
	pxor	%xmm4,%xmm0
	psrlq	$1,%xmm0
	pxor	%xmm1,%xmm0
.Ldone:
.byte	102,65,15,56,0,194
	movdqu	%xmm0,(%rdi)
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_ghash_clmul,.-gcm_ghash_clmul
.globl	gcm_init_avx
.type	gcm_init_avx,@function
.align	32
gcm_init_avx:
.cfi_startproc	
	vzeroupper

	vmovdqu	(%rsi),%xmm2
	vpshufd	$78,%xmm2,%xmm2


	vpshufd	$255,%xmm2,%xmm4
	vpsrlq	$63,%xmm2,%xmm3
	vpsllq	$1,%xmm2,%xmm2
	vpxor	%xmm5,%xmm5,%xmm5
	vpcmpgtd	%xmm4,%xmm5,%xmm5
	vpslldq	$8,%xmm3,%xmm3
	vpor	%xmm3,%xmm2,%xmm2


	vpand	.L0x1c2_polynomial(%rip),%xmm5,%xmm5
	vpxor	%xmm5,%xmm2,%xmm2

	vpunpckhqdq	%xmm2,%xmm2,%xmm6
	vmovdqa	%xmm2,%xmm0
	vpxor	%xmm2,%xmm6,%xmm6
	movq	$4,%r10
	jmp	.Linit_start_avx
.align	32
.Linit_loop_avx:
	vpalignr	$8,%xmm3,%xmm4,%xmm5
	vmovdqu	%xmm5,-16(%rdi)
	vpunpckhqdq	%xmm0,%xmm0,%xmm3
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm2,%xmm0,%xmm1
	vpclmulqdq	$0x00,%xmm2,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm3,%xmm3
	vpxor	%xmm0,%xmm1,%xmm4
	vpxor	%xmm4,%xmm3,%xmm3

	vpslldq	$8,%xmm3,%xmm4
	vpsrldq	$8,%xmm3,%xmm3
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm3,%xmm1,%xmm1
	vpsllq	$57,%xmm0,%xmm3
	vpsllq	$62,%xmm0,%xmm4
	vpxor	%xmm3,%xmm4,%xmm4
	vpsllq	$63,%xmm0,%xmm3
	vpxor	%xmm3,%xmm4,%xmm4
	vpslldq	$8,%xmm4,%xmm3
	vpsrldq	$8,%xmm4,%xmm4
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm4,%xmm1,%xmm1

	vpsrlq	$1,%xmm0,%xmm4
	vpxor	%xmm0,%xmm1,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$5,%xmm4,%xmm4
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$1,%xmm0,%xmm0
	vpxor	%xmm1,%xmm0,%xmm0
.Linit_start_avx:
	vmovdqa	%xmm0,%xmm5
	vpunpckhqdq	%xmm0,%xmm0,%xmm3
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm2,%xmm0,%xmm1
	vpclmulqdq	$0x00,%xmm2,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm3,%xmm3
	vpxor	%xmm0,%xmm1,%xmm4
	vpxor	%xmm4,%xmm3,%xmm3

	vpslldq	$8,%xmm3,%xmm4
	vpsrldq	$8,%xmm3,%xmm3
	vpxor	%xmm4,%xmm0,%xmm0
	vpxor	%xmm3,%xmm1,%xmm1
	vpsllq	$57,%xmm0,%xmm3
	vpsllq	$62,%xmm0,%xmm4
	vpxor	%xmm3,%xmm4,%xmm4
	vpsllq	$63,%xmm0,%xmm3
	vpxor	%xmm3,%xmm4,%xmm4
	vpslldq	$8,%xmm4,%xmm3
	vpsrldq	$8,%xmm4,%xmm4
	vpxor	%xmm3,%xmm0,%xmm0
	vpxor	%xmm4,%xmm1,%xmm1

	vpsrlq	$1,%xmm0,%xmm4
	vpxor	%xmm0,%xmm1,%xmm1
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$5,%xmm4,%xmm4
	vpxor	%xmm4,%xmm0,%xmm0
	vpsrlq	$1,%xmm0,%xmm0
	vpxor	%xmm1,%xmm0,%xmm0
	vpshufd	$78,%xmm5,%xmm3
	vpshufd	$78,%xmm0,%xmm4
	vpxor	%xmm5,%xmm3,%xmm3
	vmovdqu	%xmm5,0(%rdi)
	vpxor	%xmm0,%xmm4,%xmm4
	vmovdqu	%xmm0,16(%rdi)
	leaq	48(%rdi),%rdi
	subq	$1,%r10
	jnz	.Linit_loop_avx

	vpalignr	$8,%xmm4,%xmm3,%xmm5
	vmovdqu	%xmm5,-16(%rdi)

	vzeroupper
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_init_avx,.-gcm_init_avx
.globl	gcm_gmult_avx
.type	gcm_gmult_avx,@function
.align	32
gcm_gmult_avx:
.cfi_startproc	
	jmp	.L_gmult_clmul
.cfi_endproc	
.size	gcm_gmult_avx,.-gcm_gmult_avx
.globl	gcm_ghash_avx
.type	gcm_ghash_avx,@function
.align	32
gcm_ghash_avx:
.cfi_startproc	
	vzeroupper

	vmovdqu	(%rdi),%xmm10
	leaq	.L0x1c2_polynomial(%rip),%r10
	leaq	64(%rsi),%rsi
	vmovdqu	.Lbswap_mask(%rip),%xmm13
	vpshufb	%xmm13,%xmm10,%xmm10
	cmpq	$0x80,%rcx
	jb	.Lshort_avx
	subq	$0x80,%rcx

	vmovdqu	112(%rdx),%xmm14
	vmovdqu	0-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm14
	vmovdqu	32-64(%rsi),%xmm7

	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vmovdqu	96(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm14,%xmm9,%xmm9
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	16-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vmovdqu	80(%rdx),%xmm14
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8

	vpshufb	%xmm13,%xmm14,%xmm14
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	48-64(%rsi),%xmm6
	vpxor	%xmm14,%xmm9,%xmm9
	vmovdqu	64(%rdx),%xmm15
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	80-64(%rsi),%xmm7

	vpshufb	%xmm13,%xmm15,%xmm15
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm1,%xmm4,%xmm4
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	64-64(%rsi),%xmm6
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8

	vmovdqu	48(%rdx),%xmm14
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpxor	%xmm4,%xmm1,%xmm1
	vpshufb	%xmm13,%xmm14,%xmm14
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	96-64(%rsi),%xmm6
	vpxor	%xmm5,%xmm2,%xmm2
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	128-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9

	vmovdqu	32(%rdx),%xmm15
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm1,%xmm4,%xmm4
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	112-64(%rsi),%xmm6
	vpxor	%xmm2,%xmm5,%xmm5
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8

	vmovdqu	16(%rdx),%xmm14
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpxor	%xmm4,%xmm1,%xmm1
	vpshufb	%xmm13,%xmm14,%xmm14
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	144-64(%rsi),%xmm6
	vpxor	%xmm5,%xmm2,%xmm2
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	176-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9

	vmovdqu	(%rdx),%xmm15
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm1,%xmm4,%xmm4
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	160-64(%rsi),%xmm6
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x10,%xmm7,%xmm9,%xmm2

	leaq	128(%rdx),%rdx
	cmpq	$0x80,%rcx
	jb	.Ltail_avx

	vpxor	%xmm10,%xmm15,%xmm15
	subq	$0x80,%rcx
	jmp	.Loop8x_avx

.align	32
.Loop8x_avx:
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vmovdqu	112(%rdx),%xmm14
	vpxor	%xmm0,%xmm3,%xmm3
	vpxor	%xmm15,%xmm8,%xmm8
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm10
	vpshufb	%xmm13,%xmm14,%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm11
	vmovdqu	0-64(%rsi),%xmm6
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm12
	vmovdqu	32-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9

	vmovdqu	96(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpxor	%xmm3,%xmm10,%xmm10
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vxorps	%xmm4,%xmm11,%xmm11
	vmovdqu	16-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm5,%xmm12,%xmm12
	vxorps	%xmm15,%xmm8,%xmm8

	vmovdqu	80(%rdx),%xmm14
	vpxor	%xmm10,%xmm12,%xmm12
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpxor	%xmm11,%xmm12,%xmm12
	vpslldq	$8,%xmm12,%xmm9
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vpsrldq	$8,%xmm12,%xmm12
	vpxor	%xmm9,%xmm10,%xmm10
	vmovdqu	48-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm14
	vxorps	%xmm12,%xmm11,%xmm11
	vpxor	%xmm1,%xmm4,%xmm4
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	80-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	64(%rdx),%xmm15
	vpalignr	$8,%xmm10,%xmm10,%xmm12
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpshufb	%xmm13,%xmm15,%xmm15
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	64-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm4,%xmm1,%xmm1
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vxorps	%xmm15,%xmm8,%xmm8
	vpxor	%xmm5,%xmm2,%xmm2

	vmovdqu	48(%rdx),%xmm14
	vpclmulqdq	$0x10,(%r10),%xmm10,%xmm10
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpshufb	%xmm13,%xmm14,%xmm14
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	96-64(%rsi),%xmm6
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	128-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	32(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpshufb	%xmm13,%xmm15,%xmm15
	vpxor	%xmm3,%xmm0,%xmm0
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	112-64(%rsi),%xmm6
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm4,%xmm1,%xmm1
	vpclmulqdq	$0x00,%xmm7,%xmm9,%xmm2
	vpxor	%xmm15,%xmm8,%xmm8
	vpxor	%xmm5,%xmm2,%xmm2
	vxorps	%xmm12,%xmm10,%xmm10

	vmovdqu	16(%rdx),%xmm14
	vpalignr	$8,%xmm10,%xmm10,%xmm12
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm3
	vpshufb	%xmm13,%xmm14,%xmm14
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm4
	vmovdqu	144-64(%rsi),%xmm6
	vpclmulqdq	$0x10,(%r10),%xmm10,%xmm10
	vxorps	%xmm11,%xmm12,%xmm12
	vpunpckhqdq	%xmm14,%xmm14,%xmm9
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x10,%xmm7,%xmm8,%xmm5
	vmovdqu	176-64(%rsi),%xmm7
	vpxor	%xmm14,%xmm9,%xmm9
	vpxor	%xmm2,%xmm5,%xmm5

	vmovdqu	(%rdx),%xmm15
	vpclmulqdq	$0x00,%xmm6,%xmm14,%xmm0
	vpshufb	%xmm13,%xmm15,%xmm15
	vpclmulqdq	$0x11,%xmm6,%xmm14,%xmm1
	vmovdqu	160-64(%rsi),%xmm6
	vpxor	%xmm12,%xmm15,%xmm15
	vpclmulqdq	$0x10,%xmm7,%xmm9,%xmm2
	vpxor	%xmm10,%xmm15,%xmm15

	leaq	128(%rdx),%rdx
	subq	$0x80,%rcx
	jnc	.Loop8x_avx

	addq	$0x80,%rcx
	jmp	.Ltail_no_xor_avx

.align	32
.Lshort_avx:
	vmovdqu	-16(%rdx,%rcx,1),%xmm14
	leaq	(%rdx,%rcx,1),%rdx
	vmovdqu	0-64(%rsi),%xmm6
	vmovdqu	32-64(%rsi),%xmm7
	vpshufb	%xmm13,%xmm14,%xmm15

	vmovdqa	%xmm0,%xmm3
	vmovdqa	%xmm1,%xmm4
	vmovdqa	%xmm2,%xmm5
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-32(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	16-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vpsrldq	$8,%xmm7,%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-48(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	48-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vmovdqu	80-64(%rsi),%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-64(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	64-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vpsrldq	$8,%xmm7,%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-80(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	96-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vmovdqu	128-64(%rsi),%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-96(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	112-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vpsrldq	$8,%xmm7,%xmm7
	subq	$0x10,%rcx
	jz	.Ltail_avx

	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vmovdqu	-112(%rdx),%xmm14
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vmovdqu	144-64(%rsi),%xmm6
	vpshufb	%xmm13,%xmm14,%xmm15
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2
	vmovq	184-64(%rsi),%xmm7
	subq	$0x10,%rcx
	jmp	.Ltail_avx

.align	32
.Ltail_avx:
	vpxor	%xmm10,%xmm15,%xmm15
.Ltail_no_xor_avx:
	vpunpckhqdq	%xmm15,%xmm15,%xmm8
	vpxor	%xmm0,%xmm3,%xmm3
	vpclmulqdq	$0x00,%xmm6,%xmm15,%xmm0
	vpxor	%xmm15,%xmm8,%xmm8
	vpxor	%xmm1,%xmm4,%xmm4
	vpclmulqdq	$0x11,%xmm6,%xmm15,%xmm1
	vpxor	%xmm2,%xmm5,%xmm5
	vpclmulqdq	$0x00,%xmm7,%xmm8,%xmm2

	vmovdqu	(%r10),%xmm12

	vpxor	%xmm0,%xmm3,%xmm10
	vpxor	%xmm1,%xmm4,%xmm11
	vpxor	%xmm2,%xmm5,%xmm5

	vpxor	%xmm10,%xmm5,%xmm5
	vpxor	%xmm11,%xmm5,%xmm5
	vpslldq	$8,%xmm5,%xmm9
	vpsrldq	$8,%xmm5,%xmm5
	vpxor	%xmm9,%xmm10,%xmm10
	vpxor	%xmm5,%xmm11,%xmm11

	vpclmulqdq	$0x10,%xmm12,%xmm10,%xmm9
	vpalignr	$8,%xmm10,%xmm10,%xmm10
	vpxor	%xmm9,%xmm10,%xmm10

	vpclmulqdq	$0x10,%xmm12,%xmm10,%xmm9
	vpalignr	$8,%xmm10,%xmm10,%xmm10
	vpxor	%xmm11,%xmm10,%xmm10
	vpxor	%xmm9,%xmm10,%xmm10

	cmpq	$0,%rcx
	jne	.Lshort_avx

	vpshufb	%xmm13,%xmm10,%xmm10
	vmovdqu	%xmm10,(%rdi)
	vzeroupper
	.byte	0xf3,0xc3
.cfi_endproc	
.size	gcm_ghash_avx,.-gcm_ghash_avx
.align	64
.Lbswap_mask:
.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.L0x1c2_polynomial:
.byte	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
.L7_mask:
.long	7,0,7,0
.L7_mask_poly:
.long	7,0,450,0
.align	64
.type	.Lrem_4bit,@object
.Lrem_4bit:
.long	0,0,0,471859200,0,943718400,0,610271232
.long	0,1887436800,0,1822425088,0,1220542464,0,1423966208
.long	0,3774873600,0,4246732800,0,3644850176,0,3311403008
.long	0,2441084928,0,2376073216,0,2847932416,0,3051356160
.type	.Lrem_8bit,@object
.Lrem_8bit:
.value	0x0000,0x01C2,0x0384,0x0246,0x0708,0x06CA,0x048C,0x054E
.value	0x0E10,0x0FD2,0x0D94,0x0C56,0x0918,0x08DA,0x0A9C,0x0B5E
.value	0x1C20,0x1DE2,0x1FA4,0x1E66,0x1B28,0x1AEA,0x18AC,0x196E
.value	0x1230,0x13F2,0x11B4,0x1076,0x1538,0x14FA,0x16BC,0x177E
.value	0x3840,0x3982,0x3BC4,0x3A06,0x3F48,0x3E8A,0x3CCC,0x3D0E
.value	0x3650,0x3792,0x35D4,0x3416,0x3158,0x309A,0x32DC,0x331E
.value	0x2460,0x25A2,0x27E4,0x2626,0x2368,0x22AA,0x20EC,0x212E
.value	0x2A70,0x2BB2,0x29F4,0x2836,0x2D78,0x2CBA,0x2EFC,0x2F3E
.value	0x7080,0x7142,0x7304,0x72C6,0x7788,0x764A,0x740C,0x75CE
.value	0x7E90,0x7F52,0x7D14,0x7CD6,0x7998,0x785A,0x7A1C,0x7BDE
.value	0x6CA0,0x6D62,0x6F24,0x6EE6,0x6BA8,0x6A6A,0x682C,0x69EE
.value	0x62B0,0x6372,0x6134,0x60F6,0x65B8,0x647A,0x663C,0x67FE
.value	0x48C0,0x4902,0x4B44,0x4A86,0x4FC8,0x4E0A,0x4C4C,0x4D8E
.value	0x46D0,0x4712,0x4554,0x4496,0x41D8,0x401A,0x425C,0x439E
.value	0x54E0,0x5522,0x5764,0x56A6,0x53E8,0x522A,0x506C,0x51AE
.value	0x5AF0,0x5B32,0x5974,0x58B6,0x5DF8,0x5C3A,0x5E7C,0x5FBE
.value	0xE100,0xE0C2,0xE284,0xE346,0xE608,0xE7CA,0xE58C,0xE44E
.value	0xEF10,0xEED2,0xEC94,0xED56,0xE818,0xE9DA,0xEB9C,0xEA5E
.value	0xFD20,0xFCE2,0xFEA4,0xFF66,0xFA28,0xFBEA,0xF9AC,0xF86E
.value	0xF330,0xF2F2,0xF0B4,0xF176,0xF438,0xF5FA,0xF7BC,0xF67E
.value	0xD940,0xD882,0xDAC4,0xDB06,0xDE48,0xDF8A,0xDDCC,0xDC0E
.value	0xD750,0xD692,0xD4D4,0xD516,0xD058,0xD19A,0xD3DC,0xD21E
.value	0xC560,0xC4A2,0xC6E4,0xC726,0xC268,0xC3AA,0xC1EC,0xC02E
.value	0xCB70,0xCAB2,0xC8F4,0xC936,0xCC78,0xCDBA,0xCFFC,0xCE3E
.value	0x9180,0x9042,0x9204,0x93C6,0x9688,0x974A,0x950C,0x94CE
.value	0x9F90,0x9E52,0x9C14,0x9DD6,0x9898,0x995A,0x9B1C,0x9ADE
.value	0x8DA0,0x8C62,0x8E24,0x8FE6,0x8AA8,0x8B6A,0x892C,0x88EE
.value	0x83B0,0x8272,0x8034,0x81F6,0x84B8,0x857A,0x873C,0x86FE
.value	0xA9C0,0xA802,0xAA44,0xAB86,0xAEC8,0xAF0A,0xAD4C,0xAC8E
.value	0xA7D0,0xA612,0xA454,0xA596,0xA0D8,0xA11A,0xA35C,0xA29E
.value	0xB5E0,0xB422,0xB664,0xB7A6,0xB2E8,0xB32A,0xB16C,0xB0AE
.value	0xBBF0,0xBA32,0xB874,0xB9B6,0xBCF8,0xBD3A,0xBF7C,0xBEBE

.byte	71,72,65,83,72,32,102,111,114,32,120,56,54,95,54,52,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	64
