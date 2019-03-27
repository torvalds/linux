/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from x86_64cpuid.pl. */

.hidden	OPENSSL_cpuid_setup
.section	.init
	call	OPENSSL_cpuid_setup

.hidden	OPENSSL_ia32cap_P
.comm	OPENSSL_ia32cap_P,16,4

.text	

.globl	OPENSSL_atomic_add
.type	OPENSSL_atomic_add,@function
.align	16
OPENSSL_atomic_add:
	movl	(%rdi),%eax
.Lspin:	leaq	(%rsi,%rax,1),%r8
.byte	0xf0
	cmpxchgl	%r8d,(%rdi)
	jne	.Lspin
	movl	%r8d,%eax
.byte	0x48,0x98
	.byte	0xf3,0xc3
.size	OPENSSL_atomic_add,.-OPENSSL_atomic_add

.globl	OPENSSL_rdtsc
.type	OPENSSL_rdtsc,@function
.align	16
OPENSSL_rdtsc:
	rdtsc
	shlq	$32,%rdx
	orq	%rdx,%rax
	.byte	0xf3,0xc3
.size	OPENSSL_rdtsc,.-OPENSSL_rdtsc

.globl	OPENSSL_ia32_cpuid
.type	OPENSSL_ia32_cpuid,@function
.align	16
OPENSSL_ia32_cpuid:
.cfi_startproc	
	movq	%rbx,%r8
.cfi_register	%rbx,%r8

	xorl	%eax,%eax
	movq	%rax,8(%rdi)
	cpuid
	movl	%eax,%r11d

	xorl	%eax,%eax
	cmpl	$0x756e6547,%ebx
	setne	%al
	movl	%eax,%r9d
	cmpl	$0x49656e69,%edx
	setne	%al
	orl	%eax,%r9d
	cmpl	$0x6c65746e,%ecx
	setne	%al
	orl	%eax,%r9d
	jz	.Lintel

	cmpl	$0x68747541,%ebx
	setne	%al
	movl	%eax,%r10d
	cmpl	$0x69746E65,%edx
	setne	%al
	orl	%eax,%r10d
	cmpl	$0x444D4163,%ecx
	setne	%al
	orl	%eax,%r10d
	jnz	.Lintel


	movl	$0x80000000,%eax
	cpuid
	cmpl	$0x80000001,%eax
	jb	.Lintel
	movl	%eax,%r10d
	movl	$0x80000001,%eax
	cpuid
	orl	%ecx,%r9d
	andl	$0x00000801,%r9d

	cmpl	$0x80000008,%r10d
	jb	.Lintel

	movl	$0x80000008,%eax
	cpuid
	movzbq	%cl,%r10
	incq	%r10

	movl	$1,%eax
	cpuid
	btl	$28,%edx
	jnc	.Lgeneric
	shrl	$16,%ebx
	cmpb	%r10b,%bl
	ja	.Lgeneric
	andl	$0xefffffff,%edx
	jmp	.Lgeneric

.Lintel:
	cmpl	$4,%r11d
	movl	$-1,%r10d
	jb	.Lnocacheinfo

	movl	$4,%eax
	movl	$0,%ecx
	cpuid
	movl	%eax,%r10d
	shrl	$14,%r10d
	andl	$0xfff,%r10d

.Lnocacheinfo:
	movl	$1,%eax
	cpuid
	movd	%eax,%xmm0
	andl	$0xbfefffff,%edx
	cmpl	$0,%r9d
	jne	.Lnotintel
	orl	$0x40000000,%edx
	andb	$15,%ah
	cmpb	$15,%ah
	jne	.LnotP4
	orl	$0x00100000,%edx
.LnotP4:
	cmpb	$6,%ah
	jne	.Lnotintel
	andl	$0x0fff0ff0,%eax
	cmpl	$0x00050670,%eax
	je	.Lknights
	cmpl	$0x00080650,%eax
	jne	.Lnotintel
.Lknights:
	andl	$0xfbffffff,%ecx

.Lnotintel:
	btl	$28,%edx
	jnc	.Lgeneric
	andl	$0xefffffff,%edx
	cmpl	$0,%r10d
	je	.Lgeneric

	orl	$0x10000000,%edx
	shrl	$16,%ebx
	cmpb	$1,%bl
	ja	.Lgeneric
	andl	$0xefffffff,%edx
.Lgeneric:
	andl	$0x00000800,%r9d
	andl	$0xfffff7ff,%ecx
	orl	%ecx,%r9d

	movl	%edx,%r10d

	cmpl	$7,%r11d
	jb	.Lno_extended_info
	movl	$7,%eax
	xorl	%ecx,%ecx
	cpuid
	btl	$26,%r9d
	jc	.Lnotknights
	andl	$0xfff7ffff,%ebx
.Lnotknights:
	movd	%xmm0,%eax
	andl	$0x0fff0ff0,%eax
	cmpl	$0x00050650,%eax
	jne	.Lnotskylakex
	andl	$0xfffeffff,%ebx

.Lnotskylakex:
	movl	%ebx,8(%rdi)
	movl	%ecx,12(%rdi)
.Lno_extended_info:

	btl	$27,%r9d
	jnc	.Lclear_avx
	xorl	%ecx,%ecx
.byte	0x0f,0x01,0xd0
	andl	$0xe6,%eax
	cmpl	$0xe6,%eax
	je	.Ldone
	andl	$0x3fdeffff,8(%rdi)




	andl	$6,%eax
	cmpl	$6,%eax
	je	.Ldone
.Lclear_avx:
	movl	$0xefffe7ff,%eax
	andl	%eax,%r9d
	movl	$0x3fdeffdf,%eax
	andl	%eax,8(%rdi)
.Ldone:
	shlq	$32,%r9
	movl	%r10d,%eax
	movq	%r8,%rbx
.cfi_restore	%rbx
	orq	%r9,%rax
	.byte	0xf3,0xc3
.cfi_endproc	
.size	OPENSSL_ia32_cpuid,.-OPENSSL_ia32_cpuid

.globl	OPENSSL_cleanse
.type	OPENSSL_cleanse,@function
.align	16
OPENSSL_cleanse:
	xorq	%rax,%rax
	cmpq	$15,%rsi
	jae	.Lot
	cmpq	$0,%rsi
	je	.Lret
.Little:
	movb	%al,(%rdi)
	subq	$1,%rsi
	leaq	1(%rdi),%rdi
	jnz	.Little
.Lret:
	.byte	0xf3,0xc3
.align	16
.Lot:
	testq	$7,%rdi
	jz	.Laligned
	movb	%al,(%rdi)
	leaq	-1(%rsi),%rsi
	leaq	1(%rdi),%rdi
	jmp	.Lot
.Laligned:
	movq	%rax,(%rdi)
	leaq	-8(%rsi),%rsi
	testq	$-8,%rsi
	leaq	8(%rdi),%rdi
	jnz	.Laligned
	cmpq	$0,%rsi
	jne	.Little
	.byte	0xf3,0xc3
.size	OPENSSL_cleanse,.-OPENSSL_cleanse

.globl	CRYPTO_memcmp
.type	CRYPTO_memcmp,@function
.align	16
CRYPTO_memcmp:
	xorq	%rax,%rax
	xorq	%r10,%r10
	cmpq	$0,%rdx
	je	.Lno_data
	cmpq	$16,%rdx
	jne	.Loop_cmp
	movq	(%rdi),%r10
	movq	8(%rdi),%r11
	movq	$1,%rdx
	xorq	(%rsi),%r10
	xorq	8(%rsi),%r11
	orq	%r11,%r10
	cmovnzq	%rdx,%rax
	.byte	0xf3,0xc3

.align	16
.Loop_cmp:
	movb	(%rdi),%r10b
	leaq	1(%rdi),%rdi
	xorb	(%rsi),%r10b
	leaq	1(%rsi),%rsi
	orb	%r10b,%al
	decq	%rdx
	jnz	.Loop_cmp
	negq	%rax
	shrq	$63,%rax
.Lno_data:
	.byte	0xf3,0xc3
.size	CRYPTO_memcmp,.-CRYPTO_memcmp
.globl	OPENSSL_wipe_cpu
.type	OPENSSL_wipe_cpu,@function
.align	16
OPENSSL_wipe_cpu:
	pxor	%xmm0,%xmm0
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
	xorq	%rcx,%rcx
	xorq	%rdx,%rdx
	xorq	%rsi,%rsi
	xorq	%rdi,%rdi
	xorq	%r8,%r8
	xorq	%r9,%r9
	xorq	%r10,%r10
	xorq	%r11,%r11
	leaq	8(%rsp),%rax
	.byte	0xf3,0xc3
.size	OPENSSL_wipe_cpu,.-OPENSSL_wipe_cpu
.globl	OPENSSL_instrument_bus
.type	OPENSSL_instrument_bus,@function
.align	16
OPENSSL_instrument_bus:
	movq	%rdi,%r10
	movq	%rsi,%rcx
	movq	%rsi,%r11

	rdtsc
	movl	%eax,%r8d
	movl	$0,%r9d
	clflush	(%r10)
.byte	0xf0
	addl	%r9d,(%r10)
	jmp	.Loop
.align	16
.Loop:	rdtsc
	movl	%eax,%edx
	subl	%r8d,%eax
	movl	%edx,%r8d
	movl	%eax,%r9d
	clflush	(%r10)
.byte	0xf0
	addl	%eax,(%r10)
	leaq	4(%r10),%r10
	subq	$1,%rcx
	jnz	.Loop

	movq	%r11,%rax
	.byte	0xf3,0xc3
.size	OPENSSL_instrument_bus,.-OPENSSL_instrument_bus

.globl	OPENSSL_instrument_bus2
.type	OPENSSL_instrument_bus2,@function
.align	16
OPENSSL_instrument_bus2:
	movq	%rdi,%r10
	movq	%rsi,%rcx
	movq	%rdx,%r11
	movq	%rcx,8(%rsp)

	rdtsc
	movl	%eax,%r8d
	movl	$0,%r9d

	clflush	(%r10)
.byte	0xf0
	addl	%r9d,(%r10)

	rdtsc
	movl	%eax,%edx
	subl	%r8d,%eax
	movl	%edx,%r8d
	movl	%eax,%r9d
.Loop2:
	clflush	(%r10)
.byte	0xf0
	addl	%eax,(%r10)

	subq	$1,%r11
	jz	.Ldone2

	rdtsc
	movl	%eax,%edx
	subl	%r8d,%eax
	movl	%edx,%r8d
	cmpl	%r9d,%eax
	movl	%eax,%r9d
	movl	$0,%edx
	setne	%dl
	subq	%rdx,%rcx
	leaq	(%r10,%rdx,4),%r10
	jnz	.Loop2

.Ldone2:
	movq	8(%rsp),%rax
	subq	%rcx,%rax
	.byte	0xf3,0xc3
.size	OPENSSL_instrument_bus2,.-OPENSSL_instrument_bus2
.globl	OPENSSL_ia32_rdrand_bytes
.type	OPENSSL_ia32_rdrand_bytes,@function
.align	16
OPENSSL_ia32_rdrand_bytes:
	xorq	%rax,%rax
	cmpq	$0,%rsi
	je	.Ldone_rdrand_bytes

	movq	$8,%r11
.Loop_rdrand_bytes:
.byte	73,15,199,242
	jc	.Lbreak_rdrand_bytes
	decq	%r11
	jnz	.Loop_rdrand_bytes
	jmp	.Ldone_rdrand_bytes

.align	16
.Lbreak_rdrand_bytes:
	cmpq	$8,%rsi
	jb	.Ltail_rdrand_bytes
	movq	%r10,(%rdi)
	leaq	8(%rdi),%rdi
	addq	$8,%rax
	subq	$8,%rsi
	jz	.Ldone_rdrand_bytes
	movq	$8,%r11
	jmp	.Loop_rdrand_bytes

.align	16
.Ltail_rdrand_bytes:
	movb	%r10b,(%rdi)
	leaq	1(%rdi),%rdi
	incq	%rax
	shrq	$8,%r10
	decq	%rsi
	jnz	.Ltail_rdrand_bytes

.Ldone_rdrand_bytes:
	xorq	%r10,%r10
	.byte	0xf3,0xc3
.size	OPENSSL_ia32_rdrand_bytes,.-OPENSSL_ia32_rdrand_bytes
.globl	OPENSSL_ia32_rdseed_bytes
.type	OPENSSL_ia32_rdseed_bytes,@function
.align	16
OPENSSL_ia32_rdseed_bytes:
	xorq	%rax,%rax
	cmpq	$0,%rsi
	je	.Ldone_rdseed_bytes

	movq	$8,%r11
.Loop_rdseed_bytes:
.byte	73,15,199,250
	jc	.Lbreak_rdseed_bytes
	decq	%r11
	jnz	.Loop_rdseed_bytes
	jmp	.Ldone_rdseed_bytes

.align	16
.Lbreak_rdseed_bytes:
	cmpq	$8,%rsi
	jb	.Ltail_rdseed_bytes
	movq	%r10,(%rdi)
	leaq	8(%rdi),%rdi
	addq	$8,%rax
	subq	$8,%rsi
	jz	.Ldone_rdseed_bytes
	movq	$8,%r11
	jmp	.Loop_rdseed_bytes

.align	16
.Ltail_rdseed_bytes:
	movb	%r10b,(%rdi)
	leaq	1(%rdi),%rdi
	incq	%rax
	shrq	$8,%r10
	decq	%rsi
	jnz	.Ltail_rdseed_bytes

.Ldone_rdseed_bytes:
	xorq	%r10,%r10
	.byte	0xf3,0xc3
.size	OPENSSL_ia32_rdseed_bytes,.-OPENSSL_ia32_rdseed_bytes
