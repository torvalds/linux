/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from co-586.pl. */
#ifdef PIC
.text
.globl	bn_mul_comba8
.type	bn_mul_comba8,@function
.align	16
bn_mul_comba8:
.L_bn_mul_comba8_begin:
	pushl	%esi
	movl	12(%esp),%esi
	pushl	%edi
	movl	20(%esp),%edi
	pushl	%ebp
	pushl	%ebx
	xorl	%ebx,%ebx
	movl	(%esi),%eax
	xorl	%ecx,%ecx
	movl	(%edi),%edx

	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%eax)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,4(%eax)
	movl	8(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	4(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%eax)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	4(%esi),%eax
	adcl	%edx,%ecx
	movl	8(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,12(%eax)
	movl	16(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	12(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	8(%esi),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	4(%esi),%eax
	adcl	%edx,%ebp
	movl	12(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%eax)
	movl	20(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	16(%esi),%eax
	adcl	%edx,%ebx
	movl	4(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	12(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	8(%esi),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	16(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	(%esi),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,20(%eax)
	movl	24(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esi),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	16(%esi),%eax
	adcl	%edx,%ecx
	movl	8(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	12(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	16(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	4(%esi),%eax
	adcl	%edx,%ecx
	movl	20(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	(%esi),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,24(%eax)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	24(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esi),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	16(%esi),%eax
	adcl	%edx,%ebp
	movl	12(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	12(%esi),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	8(%esi),%eax
	adcl	%edx,%ebp
	movl	20(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	4(%esi),%eax
	adcl	%edx,%ebp
	movl	24(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,28(%eax)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	24(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esi),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	16(%esi),%eax
	adcl	%edx,%ebx
	movl	16(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	12(%esi),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	8(%esi),%eax
	adcl	%edx,%ebx
	movl	24(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	28(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,32(%eax)
	movl	28(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	24(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esi),%eax
	adcl	%edx,%ecx
	movl	16(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	16(%esi),%eax
	adcl	%edx,%ecx
	movl	20(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	12(%esi),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	28(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,36(%eax)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	24(%esi),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esi),%eax
	adcl	%edx,%ebp
	movl	20(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	16(%esi),%eax
	adcl	%edx,%ebp
	movl	24(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	12(%esi),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,40(%eax)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	24(%esi),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esi),%eax
	adcl	%edx,%ebx
	movl	24(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	16(%esi),%eax
	adcl	%edx,%ebx
	movl	28(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,44(%eax)
	movl	28(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	24(%esi),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esi),%eax
	adcl	%edx,%ecx
	movl	28(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,48(%eax)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	24(%esi),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,52(%eax)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	adcl	$0,%ecx
	movl	%ebp,56(%eax)


	movl	%ebx,60(%eax)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_mul_comba8,.-.L_bn_mul_comba8_begin
.globl	bn_mul_comba4
.type	bn_mul_comba4,@function
.align	16
bn_mul_comba4:
.L_bn_mul_comba4_begin:
	pushl	%esi
	movl	12(%esp),%esi
	pushl	%edi
	movl	20(%esp),%edi
	pushl	%ebp
	pushl	%ebx
	xorl	%ebx,%ebx
	movl	(%esi),%eax
	xorl	%ecx,%ecx
	movl	(%edi),%edx

	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%eax)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,4(%eax)
	movl	8(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	4(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%eax)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	4(%esi),%eax
	adcl	%edx,%ecx
	movl	8(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,12(%eax)
	movl	12(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	8(%esi),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	4(%esi),%eax
	adcl	%edx,%ebp
	movl	12(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%eax)
	movl	12(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	8(%esi),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,20(%eax)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	adcl	$0,%ebp
	movl	%ebx,24(%eax)


	movl	%ecx,28(%eax)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_mul_comba4,.-.L_bn_mul_comba4_begin
.globl	bn_sqr_comba8
.type	bn_sqr_comba8,@function
.align	16
bn_sqr_comba8:
.L_bn_sqr_comba8_begin:
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	pushl	%ebx
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	xorl	%ebx,%ebx
	xorl	%ecx,%ecx
	movl	(%esi),%eax

	xorl	%ebp,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%edi)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,4(%edi)
	movl	(%esi),%edx


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	4(%esi),%eax
	adcl	$0,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	(%esi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%edi)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	8(%esi),%eax
	adcl	$0,%ebp
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	16(%esi),%eax
	adcl	$0,%ebp
	movl	%ebx,12(%edi)
	movl	(%esi),%edx


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	12(%esi),%eax
	adcl	$0,%ebx
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx

	mull	%eax
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	(%esi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%edi)
	movl	20(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	16(%esi),%eax
	adcl	$0,%ecx
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	12(%esi),%eax
	adcl	$0,%ecx
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	24(%esi),%eax
	adcl	$0,%ecx
	movl	%ebp,20(%edi)
	movl	(%esi),%edx


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	20(%esi),%eax
	adcl	$0,%ebp
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	16(%esi),%eax
	adcl	$0,%ebp
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	12(%esi),%eax
	adcl	$0,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,24(%edi)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	24(%esi),%eax
	adcl	$0,%ebx
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	20(%esi),%eax
	adcl	$0,%ebx
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	16(%esi),%eax
	adcl	$0,%ebx
	movl	12(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	28(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,28(%edi)
	movl	4(%esi),%edx


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	24(%esi),%eax
	adcl	$0,%ecx
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	20(%esi),%eax
	adcl	$0,%ecx
	movl	12(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	16(%esi),%eax
	adcl	$0,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	8(%esi),%edx
	adcl	$0,%ecx
	movl	%ebp,32(%edi)
	movl	28(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	24(%esi),%eax
	adcl	$0,%ebp
	movl	12(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	20(%esi),%eax
	adcl	$0,%ebp
	movl	16(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	28(%esi),%eax
	adcl	$0,%ebp
	movl	%ebx,36(%edi)
	movl	12(%esi),%edx


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	24(%esi),%eax
	adcl	$0,%ebx
	movl	16(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	20(%esi),%eax
	adcl	$0,%ebx

	mull	%eax
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	16(%esi),%edx
	adcl	$0,%ebx
	movl	%ecx,40(%edi)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	24(%esi),%eax
	adcl	$0,%ecx
	movl	20(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	28(%esi),%eax
	adcl	$0,%ecx
	movl	%ebp,44(%edi)
	movl	20(%esi),%edx


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	24(%esi),%eax
	adcl	$0,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	24(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,48(%edi)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	28(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,52(%edi)


	xorl	%ecx,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	adcl	$0,%ecx
	movl	%ebp,56(%edi)

	movl	%ebx,60(%edi)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_sqr_comba8,.-.L_bn_sqr_comba8_begin
.globl	bn_sqr_comba4
.type	bn_sqr_comba4,@function
.align	16
bn_sqr_comba4:
.L_bn_sqr_comba4_begin:
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	pushl	%ebx
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	xorl	%ebx,%ebx
	xorl	%ecx,%ecx
	movl	(%esi),%eax

	xorl	%ebp,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%edi)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,4(%edi)
	movl	(%esi),%edx


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	4(%esi),%eax
	adcl	$0,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	(%esi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%edi)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	8(%esi),%eax
	adcl	$0,%ebp
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	12(%esi),%eax
	adcl	$0,%ebp
	movl	%ebx,12(%edi)
	movl	4(%esi),%edx


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx

	mull	%eax
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%edi)
	movl	12(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	12(%esi),%eax
	adcl	$0,%ecx
	movl	%ebp,20(%edi)


	xorl	%ebp,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	adcl	$0,%ebp
	movl	%ebx,24(%edi)

	movl	%ecx,28(%edi)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_sqr_comba4,.-.L_bn_sqr_comba4_begin
#else
.text
.globl	bn_mul_comba8
.type	bn_mul_comba8,@function
.align	16
bn_mul_comba8:
.L_bn_mul_comba8_begin:
	pushl	%esi
	movl	12(%esp),%esi
	pushl	%edi
	movl	20(%esp),%edi
	pushl	%ebp
	pushl	%ebx
	xorl	%ebx,%ebx
	movl	(%esi),%eax
	xorl	%ecx,%ecx
	movl	(%edi),%edx

	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%eax)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,4(%eax)
	movl	8(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	4(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%eax)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	4(%esi),%eax
	adcl	%edx,%ecx
	movl	8(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,12(%eax)
	movl	16(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	12(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	8(%esi),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	4(%esi),%eax
	adcl	%edx,%ebp
	movl	12(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%eax)
	movl	20(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	16(%esi),%eax
	adcl	%edx,%ebx
	movl	4(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	12(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	8(%esi),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	16(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	(%esi),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,20(%eax)
	movl	24(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esi),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	16(%esi),%eax
	adcl	%edx,%ecx
	movl	8(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	12(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	16(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	4(%esi),%eax
	adcl	%edx,%ecx
	movl	20(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	(%esi),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,24(%eax)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	24(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esi),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	16(%esi),%eax
	adcl	%edx,%ebp
	movl	12(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	12(%esi),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	8(%esi),%eax
	adcl	%edx,%ebp
	movl	20(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	4(%esi),%eax
	adcl	%edx,%ebp
	movl	24(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,28(%eax)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	24(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esi),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	16(%esi),%eax
	adcl	%edx,%ebx
	movl	16(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	12(%esi),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	8(%esi),%eax
	adcl	%edx,%ebx
	movl	24(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	28(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,32(%eax)
	movl	28(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	24(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esi),%eax
	adcl	%edx,%ecx
	movl	16(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	16(%esi),%eax
	adcl	%edx,%ecx
	movl	20(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	12(%esi),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	28(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,36(%eax)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	24(%esi),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esi),%eax
	adcl	%edx,%ebp
	movl	20(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	16(%esi),%eax
	adcl	%edx,%ebp
	movl	24(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	12(%esi),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	16(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,40(%eax)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	24(%esi),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esi),%eax
	adcl	%edx,%ebx
	movl	24(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	16(%esi),%eax
	adcl	%edx,%ebx
	movl	28(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	20(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,44(%eax)
	movl	28(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	24(%esi),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esi),%eax
	adcl	%edx,%ecx
	movl	28(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	24(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,48(%eax)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	24(%esi),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	28(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,52(%eax)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	adcl	$0,%ecx
	movl	%ebp,56(%eax)


	movl	%ebx,60(%eax)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_mul_comba8,.-.L_bn_mul_comba8_begin
.globl	bn_mul_comba4
.type	bn_mul_comba4,@function
.align	16
bn_mul_comba4:
.L_bn_mul_comba4_begin:
	pushl	%esi
	movl	12(%esp),%esi
	pushl	%edi
	movl	20(%esp),%edi
	pushl	%ebp
	pushl	%ebx
	xorl	%ebx,%ebx
	movl	(%esi),%eax
	xorl	%ecx,%ecx
	movl	(%edi),%edx

	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%eax)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	(%esi),%eax
	adcl	%edx,%ebp
	movl	4(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,4(%eax)
	movl	8(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	4(%esi),%eax
	adcl	%edx,%ebx
	movl	4(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	(%esi),%eax
	adcl	%edx,%ebx
	movl	8(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%eax)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	8(%esi),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	4(%esi),%eax
	adcl	%edx,%ecx
	movl	8(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	(%esi),%eax
	adcl	%edx,%ecx
	movl	12(%edi),%edx
	adcl	$0,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	movl	4(%edi),%edx
	adcl	$0,%ebp
	movl	%ebx,12(%eax)
	movl	12(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	8(%esi),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	4(%esi),%eax
	adcl	%edx,%ebp
	movl	12(%edi),%edx
	adcl	$0,%ebx

	mull	%edx
	addl	%eax,%ecx
	movl	20(%esp),%eax
	adcl	%edx,%ebp
	movl	8(%edi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%eax)
	movl	12(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	8(%esi),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx

	mull	%edx
	addl	%eax,%ebp
	movl	20(%esp),%eax
	adcl	%edx,%ebx
	movl	12(%edi),%edx
	adcl	$0,%ecx
	movl	%ebp,20(%eax)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%ebx
	movl	20(%esp),%eax
	adcl	%edx,%ecx
	adcl	$0,%ebp
	movl	%ebx,24(%eax)


	movl	%ecx,28(%eax)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_mul_comba4,.-.L_bn_mul_comba4_begin
.globl	bn_sqr_comba8
.type	bn_sqr_comba8,@function
.align	16
bn_sqr_comba8:
.L_bn_sqr_comba8_begin:
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	pushl	%ebx
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	xorl	%ebx,%ebx
	xorl	%ecx,%ecx
	movl	(%esi),%eax

	xorl	%ebp,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%edi)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,4(%edi)
	movl	(%esi),%edx


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	4(%esi),%eax
	adcl	$0,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	(%esi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%edi)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	8(%esi),%eax
	adcl	$0,%ebp
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	16(%esi),%eax
	adcl	$0,%ebp
	movl	%ebx,12(%edi)
	movl	(%esi),%edx


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	12(%esi),%eax
	adcl	$0,%ebx
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx

	mull	%eax
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	(%esi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%edi)
	movl	20(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	16(%esi),%eax
	adcl	$0,%ecx
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	12(%esi),%eax
	adcl	$0,%ecx
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	24(%esi),%eax
	adcl	$0,%ecx
	movl	%ebp,20(%edi)
	movl	(%esi),%edx


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	20(%esi),%eax
	adcl	$0,%ebp
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	16(%esi),%eax
	adcl	$0,%ebp
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	12(%esi),%eax
	adcl	$0,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,24(%edi)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	24(%esi),%eax
	adcl	$0,%ebx
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	20(%esi),%eax
	adcl	$0,%ebx
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	16(%esi),%eax
	adcl	$0,%ebx
	movl	12(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	28(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,28(%edi)
	movl	4(%esi),%edx


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	24(%esi),%eax
	adcl	$0,%ecx
	movl	8(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	20(%esi),%eax
	adcl	$0,%ecx
	movl	12(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	16(%esi),%eax
	adcl	$0,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	8(%esi),%edx
	adcl	$0,%ecx
	movl	%ebp,32(%edi)
	movl	28(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	24(%esi),%eax
	adcl	$0,%ebp
	movl	12(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	20(%esi),%eax
	adcl	$0,%ebp
	movl	16(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	28(%esi),%eax
	adcl	$0,%ebp
	movl	%ebx,36(%edi)
	movl	12(%esi),%edx


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	24(%esi),%eax
	adcl	$0,%ebx
	movl	16(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	20(%esi),%eax
	adcl	$0,%ebx

	mull	%eax
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	16(%esi),%edx
	adcl	$0,%ebx
	movl	%ecx,40(%edi)
	movl	28(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	24(%esi),%eax
	adcl	$0,%ecx
	movl	20(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	28(%esi),%eax
	adcl	$0,%ecx
	movl	%ebp,44(%edi)
	movl	20(%esi),%edx


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	24(%esi),%eax
	adcl	$0,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	24(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,48(%edi)
	movl	28(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	28(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,52(%edi)


	xorl	%ecx,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	adcl	$0,%ecx
	movl	%ebp,56(%edi)

	movl	%ebx,60(%edi)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_sqr_comba8,.-.L_bn_sqr_comba8_begin
.globl	bn_sqr_comba4
.type	bn_sqr_comba4,@function
.align	16
bn_sqr_comba4:
.L_bn_sqr_comba4_begin:
	pushl	%esi
	pushl	%edi
	pushl	%ebp
	pushl	%ebx
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	xorl	%ebx,%ebx
	xorl	%ecx,%ecx
	movl	(%esi),%eax

	xorl	%ebp,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	(%esi),%edx
	adcl	$0,%ebp
	movl	%ebx,(%edi)
	movl	4(%esi),%eax


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx
	movl	%ecx,4(%edi)
	movl	(%esi),%edx


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	4(%esi),%eax
	adcl	$0,%ecx

	mull	%eax
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	(%esi),%edx
	adcl	$0,%ecx
	movl	%ebp,8(%edi)
	movl	12(%esi),%eax


	xorl	%ebp,%ebp

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	8(%esi),%eax
	adcl	$0,%ebp
	movl	4(%esi),%edx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebp
	addl	%eax,%ebx
	adcl	%edx,%ecx
	movl	12(%esi),%eax
	adcl	$0,%ebp
	movl	%ebx,12(%edi)
	movl	4(%esi),%edx


	xorl	%ebx,%ebx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ebx
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%eax
	adcl	$0,%ebx

	mull	%eax
	addl	%eax,%ecx
	adcl	%edx,%ebp
	movl	8(%esi),%edx
	adcl	$0,%ebx
	movl	%ecx,16(%edi)
	movl	12(%esi),%eax


	xorl	%ecx,%ecx

	mull	%edx
	addl	%eax,%eax
	adcl	%edx,%edx
	adcl	$0,%ecx
	addl	%eax,%ebp
	adcl	%edx,%ebx
	movl	12(%esi),%eax
	adcl	$0,%ecx
	movl	%ebp,20(%edi)


	xorl	%ebp,%ebp

	mull	%eax
	addl	%eax,%ebx
	adcl	%edx,%ecx
	adcl	$0,%ebp
	movl	%ebx,24(%edi)

	movl	%ecx,28(%edi)
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	bn_sqr_comba4,.-.L_bn_sqr_comba4_begin
#endif
