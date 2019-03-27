/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from md5-586.pl. */
#ifdef PIC
.text
.globl	md5_block_asm_data_order
.type	md5_block_asm_data_order,@function
.align	16
md5_block_asm_data_order:
.L_md5_block_asm_data_order_begin:
	pushl	%esi
	pushl	%edi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	pushl	%ebp
	shll	$6,%ecx
	pushl	%ebx
	addl	%esi,%ecx
	subl	$64,%ecx
	movl	(%edi),%eax
	pushl	%ecx
	movl	4(%edi),%ebx
	movl	8(%edi),%ecx
	movl	12(%edi),%edx
.L000start:


	movl	%ecx,%edi
	movl	(%esi),%ebp

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	3614090360(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	4(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	3905402710(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	8(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	606105819(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	12(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	3250441966(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	16(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	4118548399(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	20(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	1200080426(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	24(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	2821735955(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	28(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	4249261313(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	32(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	1770035416(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	36(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	2336552879(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	40(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	4294925233(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	44(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	2304563134(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	48(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	1804603682(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	52(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	4254626195(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	56(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	2792965006(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	60(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	1236535329(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	4(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx



	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	4129170786(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	24(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	3225465664(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	44(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	643717713(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	3921069994(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	20(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	3593408605(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	40(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	38016083(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	60(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	3634488961(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	16(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	3889429448(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	36(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	568446438(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	56(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	3275163606(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	12(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	4107603335(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	32(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	1163531501(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	52(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	2850285829(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	8(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	4243563512(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	28(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	1735328473(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	48(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	2368359562(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	20(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx



	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	4294588738(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	32(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	2272392833(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	44(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	1839030562(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	56(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	4259657740(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	4(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx

	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	2763975236(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	16(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	1272893353(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	28(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	4139469664(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	40(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	3200236656(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	52(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx

	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	681279174(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	3936430074(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	12(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	3572445317(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	24(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	76029189(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	36(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx

	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	3654602809(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	48(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	3873151461(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	60(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	530742520(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	8(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	3299628645(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx



	xorl	%edx,%edi
	orl	%ebx,%edi
	leal	4096336452(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	28(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	1126891415(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	56(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	2878612391(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	20(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	4237533241(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	48(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$21,%ebx
	xorl	%edx,%edi
	addl	%ecx,%ebx

	orl	%ebx,%edi
	leal	1700485571(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	12(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	2399980690(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	40(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	4293915773(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	4(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	2240044497(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	32(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$21,%ebx
	xorl	%edx,%edi
	addl	%ecx,%ebx

	orl	%ebx,%edi
	leal	1873313359(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	60(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	4264355552(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	24(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	2734768916(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	52(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	1309151649(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	16(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$21,%ebx
	xorl	%edx,%edi
	addl	%ecx,%ebx

	orl	%ebx,%edi
	leal	4149444226(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	44(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	3174756917(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	8(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	718787259(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	36(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	3951481745(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	24(%esp),%ebp
	addl	%edi,%ebx
	addl	$64,%esi
	roll	$21,%ebx
	movl	(%ebp),%edi
	addl	%ecx,%ebx
	addl	%edi,%eax
	movl	4(%ebp),%edi
	addl	%edi,%ebx
	movl	8(%ebp),%edi
	addl	%edi,%ecx
	movl	12(%ebp),%edi
	addl	%edi,%edx
	movl	%eax,(%ebp)
	movl	%ebx,4(%ebp)
	movl	(%esp),%edi
	movl	%ecx,8(%ebp)
	movl	%edx,12(%ebp)
	cmpl	%esi,%edi
	jae	.L000start
	popl	%eax
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	md5_block_asm_data_order,.-.L_md5_block_asm_data_order_begin
#else
.text
.globl	md5_block_asm_data_order
.type	md5_block_asm_data_order,@function
.align	16
md5_block_asm_data_order:
.L_md5_block_asm_data_order_begin:
	pushl	%esi
	pushl	%edi
	movl	12(%esp),%edi
	movl	16(%esp),%esi
	movl	20(%esp),%ecx
	pushl	%ebp
	shll	$6,%ecx
	pushl	%ebx
	addl	%esi,%ecx
	subl	$64,%ecx
	movl	(%edi),%eax
	pushl	%ecx
	movl	4(%edi),%ebx
	movl	8(%edi),%ecx
	movl	12(%edi),%edx
.L000start:


	movl	%ecx,%edi
	movl	(%esi),%ebp

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	3614090360(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	4(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	3905402710(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	8(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	606105819(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	12(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	3250441966(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	16(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	4118548399(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	20(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	1200080426(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	24(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	2821735955(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	28(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	4249261313(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	32(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	1770035416(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	36(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	2336552879(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	40(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	4294925233(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	44(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	2304563134(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	48(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	1804603682(%eax,%ebp,1),%eax
	xorl	%edx,%edi
	movl	52(%esi),%ebp
	addl	%edi,%eax
	roll	$7,%eax
	movl	%ebx,%edi
	addl	%ebx,%eax

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	4254626195(%edx,%ebp,1),%edx
	xorl	%ecx,%edi
	movl	56(%esi),%ebp
	addl	%edi,%edx
	roll	$12,%edx
	movl	%eax,%edi
	addl	%eax,%edx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	2792965006(%ecx,%ebp,1),%ecx
	xorl	%ebx,%edi
	movl	60(%esi),%ebp
	addl	%edi,%ecx
	roll	$17,%ecx
	movl	%edx,%edi
	addl	%edx,%ecx

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	1236535329(%ebx,%ebp,1),%ebx
	xorl	%eax,%edi
	movl	4(%esi),%ebp
	addl	%edi,%ebx
	roll	$22,%ebx
	movl	%ecx,%edi
	addl	%ecx,%ebx



	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	4129170786(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	24(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	3225465664(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	44(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	643717713(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	3921069994(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	20(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	3593408605(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	40(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	38016083(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	60(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	3634488961(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	16(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	3889429448(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	36(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	568446438(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	56(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	3275163606(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	12(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	4107603335(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	32(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	1163531501(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	52(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx

	xorl	%ebx,%edi
	andl	%edx,%edi
	leal	2850285829(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	8(%esi),%ebp
	addl	%edi,%eax
	movl	%ebx,%edi
	roll	$5,%eax
	addl	%ebx,%eax

	xorl	%eax,%edi
	andl	%ecx,%edi
	leal	4243563512(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	28(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$9,%edx
	addl	%eax,%edx

	xorl	%edx,%edi
	andl	%ebx,%edi
	leal	1735328473(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	48(%esi),%ebp
	addl	%edi,%ecx
	movl	%edx,%edi
	roll	$14,%ecx
	addl	%edx,%ecx

	xorl	%ecx,%edi
	andl	%eax,%edi
	leal	2368359562(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	20(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$20,%ebx
	addl	%ecx,%ebx



	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	4294588738(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	32(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	2272392833(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	44(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	1839030562(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	56(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	4259657740(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	4(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx

	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	2763975236(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	16(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	1272893353(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	28(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	4139469664(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	40(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	3200236656(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	52(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx

	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	681279174(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	3936430074(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	12(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	3572445317(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	24(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	76029189(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	36(%esi),%ebp
	addl	%edi,%ebx
	movl	%ecx,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx

	xorl	%edx,%edi
	xorl	%ebx,%edi
	leal	3654602809(%eax,%ebp,1),%eax
	addl	%edi,%eax
	movl	48(%esi),%ebp
	roll	$4,%eax
	movl	%ebx,%edi

	addl	%ebx,%eax
	xorl	%ecx,%edi
	leal	3873151461(%edx,%ebp,1),%edx
	xorl	%eax,%edi
	movl	60(%esi),%ebp
	addl	%edi,%edx
	movl	%eax,%edi
	roll	$11,%edx
	addl	%eax,%edx

	xorl	%ebx,%edi
	xorl	%edx,%edi
	leal	530742520(%ecx,%ebp,1),%ecx
	addl	%edi,%ecx
	movl	8(%esi),%ebp
	roll	$16,%ecx
	movl	%edx,%edi

	addl	%edx,%ecx
	xorl	%eax,%edi
	leal	3299628645(%ebx,%ebp,1),%ebx
	xorl	%ecx,%edi
	movl	(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$23,%ebx
	addl	%ecx,%ebx



	xorl	%edx,%edi
	orl	%ebx,%edi
	leal	4096336452(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	28(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	1126891415(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	56(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	2878612391(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	20(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	4237533241(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	48(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$21,%ebx
	xorl	%edx,%edi
	addl	%ecx,%ebx

	orl	%ebx,%edi
	leal	1700485571(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	12(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	2399980690(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	40(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	4293915773(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	4(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	2240044497(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	32(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$21,%ebx
	xorl	%edx,%edi
	addl	%ecx,%ebx

	orl	%ebx,%edi
	leal	1873313359(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	60(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	4264355552(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	24(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	2734768916(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	52(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	1309151649(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	16(%esi),%ebp
	addl	%edi,%ebx
	movl	$-1,%edi
	roll	$21,%ebx
	xorl	%edx,%edi
	addl	%ecx,%ebx

	orl	%ebx,%edi
	leal	4149444226(%eax,%ebp,1),%eax
	xorl	%ecx,%edi
	movl	44(%esi),%ebp
	addl	%edi,%eax
	movl	$-1,%edi
	roll	$6,%eax
	xorl	%ecx,%edi
	addl	%ebx,%eax

	orl	%eax,%edi
	leal	3174756917(%edx,%ebp,1),%edx
	xorl	%ebx,%edi
	movl	8(%esi),%ebp
	addl	%edi,%edx
	movl	$-1,%edi
	roll	$10,%edx
	xorl	%ebx,%edi
	addl	%eax,%edx

	orl	%edx,%edi
	leal	718787259(%ecx,%ebp,1),%ecx
	xorl	%eax,%edi
	movl	36(%esi),%ebp
	addl	%edi,%ecx
	movl	$-1,%edi
	roll	$15,%ecx
	xorl	%eax,%edi
	addl	%edx,%ecx

	orl	%ecx,%edi
	leal	3951481745(%ebx,%ebp,1),%ebx
	xorl	%edx,%edi
	movl	24(%esp),%ebp
	addl	%edi,%ebx
	addl	$64,%esi
	roll	$21,%ebx
	movl	(%ebp),%edi
	addl	%ecx,%ebx
	addl	%edi,%eax
	movl	4(%ebp),%edi
	addl	%edi,%ebx
	movl	8(%ebp),%edi
	addl	%edi,%ecx
	movl	12(%ebp),%edi
	addl	%edi,%edx
	movl	%eax,(%ebp)
	movl	%ebx,4(%ebp)
	movl	(%esp),%edi
	movl	%ecx,8(%ebp)
	movl	%edx,12(%ebp)
	cmpl	%esi,%edi
	jae	.L000start
	popl	%eax
	popl	%ebx
	popl	%ebp
	popl	%edi
	popl	%esi
	ret
.size	md5_block_asm_data_order,.-.L_md5_block_asm_data_order_begin
#endif
