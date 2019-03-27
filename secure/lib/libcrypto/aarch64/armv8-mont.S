/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from armv8-mont.pl. */
.text

.globl	bn_mul_mont
.type	bn_mul_mont,%function
.align	5
bn_mul_mont:
	tst	x5,#7
	b.eq	__bn_sqr8x_mont
	tst	x5,#3
	b.eq	__bn_mul4x_mont
.Lmul_mont:
	stp	x29,x30,[sp,#-64]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]

	ldr	x9,[x2],#8		// bp[0]
	sub	x22,sp,x5,lsl#3
	ldp	x7,x8,[x1],#16	// ap[0..1]
	lsl	x5,x5,#3
	ldr	x4,[x4]		// *n0
	and	x22,x22,#-16		// ABI says so
	ldp	x13,x14,[x3],#16	// np[0..1]

	mul	x6,x7,x9		// ap[0]*bp[0]
	sub	x21,x5,#16		// j=num-2
	umulh	x7,x7,x9
	mul	x10,x8,x9		// ap[1]*bp[0]
	umulh	x11,x8,x9

	mul	x15,x6,x4		// "tp[0]"*n0
	mov	sp,x22			// alloca

	// (*)	mul	x12,x13,x15	// np[0]*m1
	umulh	x13,x13,x15
	mul	x16,x14,x15		// np[1]*m1
	// (*)	adds	x12,x12,x6	// discarded
	// (*)	As for removal of first multiplication and addition
	//	instructions. The outcome of first addition is
	//	guaranteed to be zero, which leaves two computationally
	//	significant outcomes: it either carries or not. Then
	//	question is when does it carry? Is there alternative
	//	way to deduce it? If you follow operations, you can
	//	observe that condition for carry is quite simple:
	//	x6 being non-zero. So that carry can be calculated
	//	by adding -1 to x6. That's what next instruction does.
	subs	xzr,x6,#1		// (*)
	umulh	x17,x14,x15
	adc	x13,x13,xzr
	cbz	x21,.L1st_skip

.L1st:
	ldr	x8,[x1],#8
	adds	x6,x10,x7
	sub	x21,x21,#8		// j--
	adc	x7,x11,xzr

	ldr	x14,[x3],#8
	adds	x12,x16,x13
	mul	x10,x8,x9		// ap[j]*bp[0]
	adc	x13,x17,xzr
	umulh	x11,x8,x9

	adds	x12,x12,x6
	mul	x16,x14,x15		// np[j]*m1
	adc	x13,x13,xzr
	umulh	x17,x14,x15
	str	x12,[x22],#8		// tp[j-1]
	cbnz	x21,.L1st

.L1st_skip:
	adds	x6,x10,x7
	sub	x1,x1,x5		// rewind x1
	adc	x7,x11,xzr

	adds	x12,x16,x13
	sub	x3,x3,x5		// rewind x3
	adc	x13,x17,xzr

	adds	x12,x12,x6
	sub	x20,x5,#8		// i=num-1
	adcs	x13,x13,x7

	adc	x19,xzr,xzr		// upmost overflow bit
	stp	x12,x13,[x22]

.Louter:
	ldr	x9,[x2],#8		// bp[i]
	ldp	x7,x8,[x1],#16
	ldr	x23,[sp]		// tp[0]
	add	x22,sp,#8

	mul	x6,x7,x9		// ap[0]*bp[i]
	sub	x21,x5,#16		// j=num-2
	umulh	x7,x7,x9
	ldp	x13,x14,[x3],#16
	mul	x10,x8,x9		// ap[1]*bp[i]
	adds	x6,x6,x23
	umulh	x11,x8,x9
	adc	x7,x7,xzr

	mul	x15,x6,x4
	sub	x20,x20,#8		// i--

	// (*)	mul	x12,x13,x15	// np[0]*m1
	umulh	x13,x13,x15
	mul	x16,x14,x15		// np[1]*m1
	// (*)	adds	x12,x12,x6
	subs	xzr,x6,#1		// (*)
	umulh	x17,x14,x15
	cbz	x21,.Linner_skip

.Linner:
	ldr	x8,[x1],#8
	adc	x13,x13,xzr
	ldr	x23,[x22],#8		// tp[j]
	adds	x6,x10,x7
	sub	x21,x21,#8		// j--
	adc	x7,x11,xzr

	adds	x12,x16,x13
	ldr	x14,[x3],#8
	adc	x13,x17,xzr

	mul	x10,x8,x9		// ap[j]*bp[i]
	adds	x6,x6,x23
	umulh	x11,x8,x9
	adc	x7,x7,xzr

	mul	x16,x14,x15		// np[j]*m1
	adds	x12,x12,x6
	umulh	x17,x14,x15
	str	x12,[x22,#-16]		// tp[j-1]
	cbnz	x21,.Linner

.Linner_skip:
	ldr	x23,[x22],#8		// tp[j]
	adc	x13,x13,xzr
	adds	x6,x10,x7
	sub	x1,x1,x5		// rewind x1
	adc	x7,x11,xzr

	adds	x12,x16,x13
	sub	x3,x3,x5		// rewind x3
	adcs	x13,x17,x19
	adc	x19,xzr,xzr

	adds	x6,x6,x23
	adc	x7,x7,xzr

	adds	x12,x12,x6
	adcs	x13,x13,x7
	adc	x19,x19,xzr		// upmost overflow bit
	stp	x12,x13,[x22,#-16]

	cbnz	x20,.Louter

	// Final step. We see if result is larger than modulus, and
	// if it is, subtract the modulus. But comparison implies
	// subtraction. So we subtract modulus, see if it borrowed,
	// and conditionally copy original value.
	ldr	x23,[sp]		// tp[0]
	add	x22,sp,#8
	ldr	x14,[x3],#8		// np[0]
	subs	x21,x5,#8		// j=num-1 and clear borrow
	mov	x1,x0
.Lsub:
	sbcs	x8,x23,x14		// tp[j]-np[j]
	ldr	x23,[x22],#8
	sub	x21,x21,#8		// j--
	ldr	x14,[x3],#8
	str	x8,[x1],#8		// rp[j]=tp[j]-np[j]
	cbnz	x21,.Lsub

	sbcs	x8,x23,x14
	sbcs	x19,x19,xzr		// did it borrow?
	str	x8,[x1],#8		// rp[num-1]

	ldr	x23,[sp]		// tp[0]
	add	x22,sp,#8
	ldr	x8,[x0],#8		// rp[0]
	sub	x5,x5,#8		// num--
	nop
.Lcond_copy:
	sub	x5,x5,#8		// num--
	csel	x14,x23,x8,lo		// did it borrow?
	ldr	x23,[x22],#8
	ldr	x8,[x0],#8
	str	xzr,[x22,#-16]		// wipe tp
	str	x14,[x0,#-16]
	cbnz	x5,.Lcond_copy

	csel	x14,x23,x8,lo
	str	xzr,[x22,#-8]		// wipe tp
	str	x14,[x0,#-8]

	ldp	x19,x20,[x29,#16]
	mov	sp,x29
	ldp	x21,x22,[x29,#32]
	mov	x0,#1
	ldp	x23,x24,[x29,#48]
	ldr	x29,[sp],#64
	ret
.size	bn_mul_mont,.-bn_mul_mont
.type	__bn_sqr8x_mont,%function
.align	5
__bn_sqr8x_mont:
	cmp	x1,x2
	b.ne	__bn_mul4x_mont
.Lsqr8x_mont:
.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	stp	x0,x3,[sp,#96]	// offload rp and np

	ldp	x6,x7,[x1,#8*0]
	ldp	x8,x9,[x1,#8*2]
	ldp	x10,x11,[x1,#8*4]
	ldp	x12,x13,[x1,#8*6]

	sub	x2,sp,x5,lsl#4
	lsl	x5,x5,#3
	ldr	x4,[x4]		// *n0
	mov	sp,x2			// alloca
	sub	x27,x5,#8*8
	b	.Lsqr8x_zero_start

.Lsqr8x_zero:
	sub	x27,x27,#8*8
	stp	xzr,xzr,[x2,#8*0]
	stp	xzr,xzr,[x2,#8*2]
	stp	xzr,xzr,[x2,#8*4]
	stp	xzr,xzr,[x2,#8*6]
.Lsqr8x_zero_start:
	stp	xzr,xzr,[x2,#8*8]
	stp	xzr,xzr,[x2,#8*10]
	stp	xzr,xzr,[x2,#8*12]
	stp	xzr,xzr,[x2,#8*14]
	add	x2,x2,#8*16
	cbnz	x27,.Lsqr8x_zero

	add	x3,x1,x5
	add	x1,x1,#8*8
	mov	x19,xzr
	mov	x20,xzr
	mov	x21,xzr
	mov	x22,xzr
	mov	x23,xzr
	mov	x24,xzr
	mov	x25,xzr
	mov	x26,xzr
	mov	x2,sp
	str	x4,[x29,#112]		// offload n0

	// Multiply everything but a[i]*a[i]
.align	4
.Lsqr8x_outer_loop:
        //                                                 a[1]a[0]	(i)
        //                                             a[2]a[0]
        //                                         a[3]a[0]
        //                                     a[4]a[0]
        //                                 a[5]a[0]
        //                             a[6]a[0]
        //                         a[7]a[0]
        //                                         a[2]a[1]		(ii)
        //                                     a[3]a[1]
        //                                 a[4]a[1]
        //                             a[5]a[1]
        //                         a[6]a[1]
        //                     a[7]a[1]
        //                                 a[3]a[2]			(iii)
        //                             a[4]a[2]
        //                         a[5]a[2]
        //                     a[6]a[2]
        //                 a[7]a[2]
        //                         a[4]a[3]				(iv)
        //                     a[5]a[3]
        //                 a[6]a[3]
        //             a[7]a[3]
        //                 a[5]a[4]					(v)
        //             a[6]a[4]
        //         a[7]a[4]
        //         a[6]a[5]						(vi)
        //     a[7]a[5]
        // a[7]a[6]							(vii)

	mul	x14,x7,x6		// lo(a[1..7]*a[0])		(i)
	mul	x15,x8,x6
	mul	x16,x9,x6
	mul	x17,x10,x6
	adds	x20,x20,x14		// t[1]+lo(a[1]*a[0])
	mul	x14,x11,x6
	adcs	x21,x21,x15
	mul	x15,x12,x6
	adcs	x22,x22,x16
	mul	x16,x13,x6
	adcs	x23,x23,x17
	umulh	x17,x7,x6		// hi(a[1..7]*a[0])
	adcs	x24,x24,x14
	umulh	x14,x8,x6
	adcs	x25,x25,x15
	umulh	x15,x9,x6
	adcs	x26,x26,x16
	umulh	x16,x10,x6
	stp	x19,x20,[x2],#8*2	// t[0..1]
	adc	x19,xzr,xzr		// t[8]
	adds	x21,x21,x17		// t[2]+lo(a[1]*a[0])
	umulh	x17,x11,x6
	adcs	x22,x22,x14
	umulh	x14,x12,x6
	adcs	x23,x23,x15
	umulh	x15,x13,x6
	adcs	x24,x24,x16
	mul	x16,x8,x7		// lo(a[2..7]*a[1])		(ii)
	adcs	x25,x25,x17
	mul	x17,x9,x7
	adcs	x26,x26,x14
	mul	x14,x10,x7
	adc	x19,x19,x15

	mul	x15,x11,x7
	adds	x22,x22,x16
	mul	x16,x12,x7
	adcs	x23,x23,x17
	mul	x17,x13,x7
	adcs	x24,x24,x14
	umulh	x14,x8,x7		// hi(a[2..7]*a[1])
	adcs	x25,x25,x15
	umulh	x15,x9,x7
	adcs	x26,x26,x16
	umulh	x16,x10,x7
	adcs	x19,x19,x17
	umulh	x17,x11,x7
	stp	x21,x22,[x2],#8*2	// t[2..3]
	adc	x20,xzr,xzr		// t[9]
	adds	x23,x23,x14
	umulh	x14,x12,x7
	adcs	x24,x24,x15
	umulh	x15,x13,x7
	adcs	x25,x25,x16
	mul	x16,x9,x8		// lo(a[3..7]*a[2])		(iii)
	adcs	x26,x26,x17
	mul	x17,x10,x8
	adcs	x19,x19,x14
	mul	x14,x11,x8
	adc	x20,x20,x15

	mul	x15,x12,x8
	adds	x24,x24,x16
	mul	x16,x13,x8
	adcs	x25,x25,x17
	umulh	x17,x9,x8		// hi(a[3..7]*a[2])
	adcs	x26,x26,x14
	umulh	x14,x10,x8
	adcs	x19,x19,x15
	umulh	x15,x11,x8
	adcs	x20,x20,x16
	umulh	x16,x12,x8
	stp	x23,x24,[x2],#8*2	// t[4..5]
	adc	x21,xzr,xzr		// t[10]
	adds	x25,x25,x17
	umulh	x17,x13,x8
	adcs	x26,x26,x14
	mul	x14,x10,x9		// lo(a[4..7]*a[3])		(iv)
	adcs	x19,x19,x15
	mul	x15,x11,x9
	adcs	x20,x20,x16
	mul	x16,x12,x9
	adc	x21,x21,x17

	mul	x17,x13,x9
	adds	x26,x26,x14
	umulh	x14,x10,x9		// hi(a[4..7]*a[3])
	adcs	x19,x19,x15
	umulh	x15,x11,x9
	adcs	x20,x20,x16
	umulh	x16,x12,x9
	adcs	x21,x21,x17
	umulh	x17,x13,x9
	stp	x25,x26,[x2],#8*2	// t[6..7]
	adc	x22,xzr,xzr		// t[11]
	adds	x19,x19,x14
	mul	x14,x11,x10		// lo(a[5..7]*a[4])		(v)
	adcs	x20,x20,x15
	mul	x15,x12,x10
	adcs	x21,x21,x16
	mul	x16,x13,x10
	adc	x22,x22,x17

	umulh	x17,x11,x10		// hi(a[5..7]*a[4])
	adds	x20,x20,x14
	umulh	x14,x12,x10
	adcs	x21,x21,x15
	umulh	x15,x13,x10
	adcs	x22,x22,x16
	mul	x16,x12,x11		// lo(a[6..7]*a[5])		(vi)
	adc	x23,xzr,xzr		// t[12]
	adds	x21,x21,x17
	mul	x17,x13,x11
	adcs	x22,x22,x14
	umulh	x14,x12,x11		// hi(a[6..7]*a[5])
	adc	x23,x23,x15

	umulh	x15,x13,x11
	adds	x22,x22,x16
	mul	x16,x13,x12		// lo(a[7]*a[6])		(vii)
	adcs	x23,x23,x17
	umulh	x17,x13,x12		// hi(a[7]*a[6])
	adc	x24,xzr,xzr		// t[13]
	adds	x23,x23,x14
	sub	x27,x3,x1	// done yet?
	adc	x24,x24,x15

	adds	x24,x24,x16
	sub	x14,x3,x5	// rewinded ap
	adc	x25,xzr,xzr		// t[14]
	add	x25,x25,x17

	cbz	x27,.Lsqr8x_outer_break

	mov	x4,x6
	ldp	x6,x7,[x2,#8*0]
	ldp	x8,x9,[x2,#8*2]
	ldp	x10,x11,[x2,#8*4]
	ldp	x12,x13,[x2,#8*6]
	adds	x19,x19,x6
	adcs	x20,x20,x7
	ldp	x6,x7,[x1,#8*0]
	adcs	x21,x21,x8
	adcs	x22,x22,x9
	ldp	x8,x9,[x1,#8*2]
	adcs	x23,x23,x10
	adcs	x24,x24,x11
	ldp	x10,x11,[x1,#8*4]
	adcs	x25,x25,x12
	mov	x0,x1
	adcs	x26,xzr,x13
	ldp	x12,x13,[x1,#8*6]
	add	x1,x1,#8*8
	//adc	x28,xzr,xzr		// moved below
	mov	x27,#-8*8

	//                                                         a[8]a[0]
	//                                                     a[9]a[0]
	//                                                 a[a]a[0]
	//                                             a[b]a[0]
	//                                         a[c]a[0]
	//                                     a[d]a[0]
	//                                 a[e]a[0]
	//                             a[f]a[0]
	//                                                     a[8]a[1]
	//                         a[f]a[1]........................
	//                                                 a[8]a[2]
	//                     a[f]a[2]........................
	//                                             a[8]a[3]
	//                 a[f]a[3]........................
	//                                         a[8]a[4]
	//             a[f]a[4]........................
	//                                     a[8]a[5]
	//         a[f]a[5]........................
	//                                 a[8]a[6]
	//     a[f]a[6]........................
	//                             a[8]a[7]
	// a[f]a[7]........................
.Lsqr8x_mul:
	mul	x14,x6,x4
	adc	x28,xzr,xzr		// carry bit, modulo-scheduled
	mul	x15,x7,x4
	add	x27,x27,#8
	mul	x16,x8,x4
	mul	x17,x9,x4
	adds	x19,x19,x14
	mul	x14,x10,x4
	adcs	x20,x20,x15
	mul	x15,x11,x4
	adcs	x21,x21,x16
	mul	x16,x12,x4
	adcs	x22,x22,x17
	mul	x17,x13,x4
	adcs	x23,x23,x14
	umulh	x14,x6,x4
	adcs	x24,x24,x15
	umulh	x15,x7,x4
	adcs	x25,x25,x16
	umulh	x16,x8,x4
	adcs	x26,x26,x17
	umulh	x17,x9,x4
	adc	x28,x28,xzr
	str	x19,[x2],#8
	adds	x19,x20,x14
	umulh	x14,x10,x4
	adcs	x20,x21,x15
	umulh	x15,x11,x4
	adcs	x21,x22,x16
	umulh	x16,x12,x4
	adcs	x22,x23,x17
	umulh	x17,x13,x4
	ldr	x4,[x0,x27]
	adcs	x23,x24,x14
	adcs	x24,x25,x15
	adcs	x25,x26,x16
	adcs	x26,x28,x17
	//adc	x28,xzr,xzr		// moved above
	cbnz	x27,.Lsqr8x_mul
					// note that carry flag is guaranteed
					// to be zero at this point
	cmp	x1,x3		// done yet?
	b.eq	.Lsqr8x_break

	ldp	x6,x7,[x2,#8*0]
	ldp	x8,x9,[x2,#8*2]
	ldp	x10,x11,[x2,#8*4]
	ldp	x12,x13,[x2,#8*6]
	adds	x19,x19,x6
	ldr	x4,[x0,#-8*8]
	adcs	x20,x20,x7
	ldp	x6,x7,[x1,#8*0]
	adcs	x21,x21,x8
	adcs	x22,x22,x9
	ldp	x8,x9,[x1,#8*2]
	adcs	x23,x23,x10
	adcs	x24,x24,x11
	ldp	x10,x11,[x1,#8*4]
	adcs	x25,x25,x12
	mov	x27,#-8*8
	adcs	x26,x26,x13
	ldp	x12,x13,[x1,#8*6]
	add	x1,x1,#8*8
	//adc	x28,xzr,xzr		// moved above
	b	.Lsqr8x_mul

.align	4
.Lsqr8x_break:
	ldp	x6,x7,[x0,#8*0]
	add	x1,x0,#8*8
	ldp	x8,x9,[x0,#8*2]
	sub	x14,x3,x1		// is it last iteration?
	ldp	x10,x11,[x0,#8*4]
	sub	x15,x2,x14
	ldp	x12,x13,[x0,#8*6]
	cbz	x14,.Lsqr8x_outer_loop

	stp	x19,x20,[x2,#8*0]
	ldp	x19,x20,[x15,#8*0]
	stp	x21,x22,[x2,#8*2]
	ldp	x21,x22,[x15,#8*2]
	stp	x23,x24,[x2,#8*4]
	ldp	x23,x24,[x15,#8*4]
	stp	x25,x26,[x2,#8*6]
	mov	x2,x15
	ldp	x25,x26,[x15,#8*6]
	b	.Lsqr8x_outer_loop

.align	4
.Lsqr8x_outer_break:
	// Now multiply above result by 2 and add a[n-1]*a[n-1]|...|a[0]*a[0]
	ldp	x7,x9,[x14,#8*0]	// recall that x14 is &a[0]
	ldp	x15,x16,[sp,#8*1]
	ldp	x11,x13,[x14,#8*2]
	add	x1,x14,#8*4
	ldp	x17,x14,[sp,#8*3]

	stp	x19,x20,[x2,#8*0]
	mul	x19,x7,x7
	stp	x21,x22,[x2,#8*2]
	umulh	x7,x7,x7
	stp	x23,x24,[x2,#8*4]
	mul	x8,x9,x9
	stp	x25,x26,[x2,#8*6]
	mov	x2,sp
	umulh	x9,x9,x9
	adds	x20,x7,x15,lsl#1
	extr	x15,x16,x15,#63
	sub	x27,x5,#8*4

.Lsqr4x_shift_n_add:
	adcs	x21,x8,x15
	extr	x16,x17,x16,#63
	sub	x27,x27,#8*4
	adcs	x22,x9,x16
	ldp	x15,x16,[x2,#8*5]
	mul	x10,x11,x11
	ldp	x7,x9,[x1],#8*2
	umulh	x11,x11,x11
	mul	x12,x13,x13
	umulh	x13,x13,x13
	extr	x17,x14,x17,#63
	stp	x19,x20,[x2,#8*0]
	adcs	x23,x10,x17
	extr	x14,x15,x14,#63
	stp	x21,x22,[x2,#8*2]
	adcs	x24,x11,x14
	ldp	x17,x14,[x2,#8*7]
	extr	x15,x16,x15,#63
	adcs	x25,x12,x15
	extr	x16,x17,x16,#63
	adcs	x26,x13,x16
	ldp	x15,x16,[x2,#8*9]
	mul	x6,x7,x7
	ldp	x11,x13,[x1],#8*2
	umulh	x7,x7,x7
	mul	x8,x9,x9
	umulh	x9,x9,x9
	stp	x23,x24,[x2,#8*4]
	extr	x17,x14,x17,#63
	stp	x25,x26,[x2,#8*6]
	add	x2,x2,#8*8
	adcs	x19,x6,x17
	extr	x14,x15,x14,#63
	adcs	x20,x7,x14
	ldp	x17,x14,[x2,#8*3]
	extr	x15,x16,x15,#63
	cbnz	x27,.Lsqr4x_shift_n_add
	ldp	x1,x4,[x29,#104]	// pull np and n0

	adcs	x21,x8,x15
	extr	x16,x17,x16,#63
	adcs	x22,x9,x16
	ldp	x15,x16,[x2,#8*5]
	mul	x10,x11,x11
	umulh	x11,x11,x11
	stp	x19,x20,[x2,#8*0]
	mul	x12,x13,x13
	umulh	x13,x13,x13
	stp	x21,x22,[x2,#8*2]
	extr	x17,x14,x17,#63
	adcs	x23,x10,x17
	extr	x14,x15,x14,#63
	ldp	x19,x20,[sp,#8*0]
	adcs	x24,x11,x14
	extr	x15,x16,x15,#63
	ldp	x6,x7,[x1,#8*0]
	adcs	x25,x12,x15
	extr	x16,xzr,x16,#63
	ldp	x8,x9,[x1,#8*2]
	adc	x26,x13,x16
	ldp	x10,x11,[x1,#8*4]

	// Reduce by 512 bits per iteration
	mul	x28,x4,x19		// t[0]*n0
	ldp	x12,x13,[x1,#8*6]
	add	x3,x1,x5
	ldp	x21,x22,[sp,#8*2]
	stp	x23,x24,[x2,#8*4]
	ldp	x23,x24,[sp,#8*4]
	stp	x25,x26,[x2,#8*6]
	ldp	x25,x26,[sp,#8*6]
	add	x1,x1,#8*8
	mov	x30,xzr		// initial top-most carry
	mov	x2,sp
	mov	x27,#8

.Lsqr8x_reduction:
	// (*)	mul	x14,x6,x28	// lo(n[0-7])*lo(t[0]*n0)
	mul	x15,x7,x28
	sub	x27,x27,#1
	mul	x16,x8,x28
	str	x28,[x2],#8		// put aside t[0]*n0 for tail processing
	mul	x17,x9,x28
	// (*)	adds	xzr,x19,x14
	subs	xzr,x19,#1		// (*)
	mul	x14,x10,x28
	adcs	x19,x20,x15
	mul	x15,x11,x28
	adcs	x20,x21,x16
	mul	x16,x12,x28
	adcs	x21,x22,x17
	mul	x17,x13,x28
	adcs	x22,x23,x14
	umulh	x14,x6,x28		// hi(n[0-7])*lo(t[0]*n0)
	adcs	x23,x24,x15
	umulh	x15,x7,x28
	adcs	x24,x25,x16
	umulh	x16,x8,x28
	adcs	x25,x26,x17
	umulh	x17,x9,x28
	adc	x26,xzr,xzr
	adds	x19,x19,x14
	umulh	x14,x10,x28
	adcs	x20,x20,x15
	umulh	x15,x11,x28
	adcs	x21,x21,x16
	umulh	x16,x12,x28
	adcs	x22,x22,x17
	umulh	x17,x13,x28
	mul	x28,x4,x19		// next t[0]*n0
	adcs	x23,x23,x14
	adcs	x24,x24,x15
	adcs	x25,x25,x16
	adc	x26,x26,x17
	cbnz	x27,.Lsqr8x_reduction

	ldp	x14,x15,[x2,#8*0]
	ldp	x16,x17,[x2,#8*2]
	mov	x0,x2
	sub	x27,x3,x1	// done yet?
	adds	x19,x19,x14
	adcs	x20,x20,x15
	ldp	x14,x15,[x2,#8*4]
	adcs	x21,x21,x16
	adcs	x22,x22,x17
	ldp	x16,x17,[x2,#8*6]
	adcs	x23,x23,x14
	adcs	x24,x24,x15
	adcs	x25,x25,x16
	adcs	x26,x26,x17
	//adc	x28,xzr,xzr		// moved below
	cbz	x27,.Lsqr8x8_post_condition

	ldr	x4,[x2,#-8*8]
	ldp	x6,x7,[x1,#8*0]
	ldp	x8,x9,[x1,#8*2]
	ldp	x10,x11,[x1,#8*4]
	mov	x27,#-8*8
	ldp	x12,x13,[x1,#8*6]
	add	x1,x1,#8*8

.Lsqr8x_tail:
	mul	x14,x6,x4
	adc	x28,xzr,xzr		// carry bit, modulo-scheduled
	mul	x15,x7,x4
	add	x27,x27,#8
	mul	x16,x8,x4
	mul	x17,x9,x4
	adds	x19,x19,x14
	mul	x14,x10,x4
	adcs	x20,x20,x15
	mul	x15,x11,x4
	adcs	x21,x21,x16
	mul	x16,x12,x4
	adcs	x22,x22,x17
	mul	x17,x13,x4
	adcs	x23,x23,x14
	umulh	x14,x6,x4
	adcs	x24,x24,x15
	umulh	x15,x7,x4
	adcs	x25,x25,x16
	umulh	x16,x8,x4
	adcs	x26,x26,x17
	umulh	x17,x9,x4
	adc	x28,x28,xzr
	str	x19,[x2],#8
	adds	x19,x20,x14
	umulh	x14,x10,x4
	adcs	x20,x21,x15
	umulh	x15,x11,x4
	adcs	x21,x22,x16
	umulh	x16,x12,x4
	adcs	x22,x23,x17
	umulh	x17,x13,x4
	ldr	x4,[x0,x27]
	adcs	x23,x24,x14
	adcs	x24,x25,x15
	adcs	x25,x26,x16
	adcs	x26,x28,x17
	//adc	x28,xzr,xzr		// moved above
	cbnz	x27,.Lsqr8x_tail
					// note that carry flag is guaranteed
					// to be zero at this point
	ldp	x6,x7,[x2,#8*0]
	sub	x27,x3,x1	// done yet?
	sub	x16,x3,x5	// rewinded np
	ldp	x8,x9,[x2,#8*2]
	ldp	x10,x11,[x2,#8*4]
	ldp	x12,x13,[x2,#8*6]
	cbz	x27,.Lsqr8x_tail_break

	ldr	x4,[x0,#-8*8]
	adds	x19,x19,x6
	adcs	x20,x20,x7
	ldp	x6,x7,[x1,#8*0]
	adcs	x21,x21,x8
	adcs	x22,x22,x9
	ldp	x8,x9,[x1,#8*2]
	adcs	x23,x23,x10
	adcs	x24,x24,x11
	ldp	x10,x11,[x1,#8*4]
	adcs	x25,x25,x12
	mov	x27,#-8*8
	adcs	x26,x26,x13
	ldp	x12,x13,[x1,#8*6]
	add	x1,x1,#8*8
	//adc	x28,xzr,xzr		// moved above
	b	.Lsqr8x_tail

.align	4
.Lsqr8x_tail_break:
	ldr	x4,[x29,#112]		// pull n0
	add	x27,x2,#8*8		// end of current t[num] window

	subs	xzr,x30,#1		// "move" top-most carry to carry bit
	adcs	x14,x19,x6
	adcs	x15,x20,x7
	ldp	x19,x20,[x0,#8*0]
	adcs	x21,x21,x8
	ldp	x6,x7,[x16,#8*0]	// recall that x16 is &n[0]
	adcs	x22,x22,x9
	ldp	x8,x9,[x16,#8*2]
	adcs	x23,x23,x10
	adcs	x24,x24,x11
	ldp	x10,x11,[x16,#8*4]
	adcs	x25,x25,x12
	adcs	x26,x26,x13
	ldp	x12,x13,[x16,#8*6]
	add	x1,x16,#8*8
	adc	x30,xzr,xzr	// top-most carry
	mul	x28,x4,x19
	stp	x14,x15,[x2,#8*0]
	stp	x21,x22,[x2,#8*2]
	ldp	x21,x22,[x0,#8*2]
	stp	x23,x24,[x2,#8*4]
	ldp	x23,x24,[x0,#8*4]
	cmp	x27,x29		// did we hit the bottom?
	stp	x25,x26,[x2,#8*6]
	mov	x2,x0			// slide the window
	ldp	x25,x26,[x0,#8*6]
	mov	x27,#8
	b.ne	.Lsqr8x_reduction

	// Final step. We see if result is larger than modulus, and
	// if it is, subtract the modulus. But comparison implies
	// subtraction. So we subtract modulus, see if it borrowed,
	// and conditionally copy original value.
	ldr	x0,[x29,#96]		// pull rp
	add	x2,x2,#8*8
	subs	x14,x19,x6
	sbcs	x15,x20,x7
	sub	x27,x5,#8*8
	mov	x3,x0		// x0 copy

.Lsqr8x_sub:
	sbcs	x16,x21,x8
	ldp	x6,x7,[x1,#8*0]
	sbcs	x17,x22,x9
	stp	x14,x15,[x0,#8*0]
	sbcs	x14,x23,x10
	ldp	x8,x9,[x1,#8*2]
	sbcs	x15,x24,x11
	stp	x16,x17,[x0,#8*2]
	sbcs	x16,x25,x12
	ldp	x10,x11,[x1,#8*4]
	sbcs	x17,x26,x13
	ldp	x12,x13,[x1,#8*6]
	add	x1,x1,#8*8
	ldp	x19,x20,[x2,#8*0]
	sub	x27,x27,#8*8
	ldp	x21,x22,[x2,#8*2]
	ldp	x23,x24,[x2,#8*4]
	ldp	x25,x26,[x2,#8*6]
	add	x2,x2,#8*8
	stp	x14,x15,[x0,#8*4]
	sbcs	x14,x19,x6
	stp	x16,x17,[x0,#8*6]
	add	x0,x0,#8*8
	sbcs	x15,x20,x7
	cbnz	x27,.Lsqr8x_sub

	sbcs	x16,x21,x8
	mov	x2,sp
	add	x1,sp,x5
	ldp	x6,x7,[x3,#8*0]
	sbcs	x17,x22,x9
	stp	x14,x15,[x0,#8*0]
	sbcs	x14,x23,x10
	ldp	x8,x9,[x3,#8*2]
	sbcs	x15,x24,x11
	stp	x16,x17,[x0,#8*2]
	sbcs	x16,x25,x12
	ldp	x19,x20,[x1,#8*0]
	sbcs	x17,x26,x13
	ldp	x21,x22,[x1,#8*2]
	sbcs	xzr,x30,xzr	// did it borrow?
	ldr	x30,[x29,#8]		// pull return address
	stp	x14,x15,[x0,#8*4]
	stp	x16,x17,[x0,#8*6]

	sub	x27,x5,#8*4
.Lsqr4x_cond_copy:
	sub	x27,x27,#8*4
	csel	x14,x19,x6,lo
	stp	xzr,xzr,[x2,#8*0]
	csel	x15,x20,x7,lo
	ldp	x6,x7,[x3,#8*4]
	ldp	x19,x20,[x1,#8*4]
	csel	x16,x21,x8,lo
	stp	xzr,xzr,[x2,#8*2]
	add	x2,x2,#8*4
	csel	x17,x22,x9,lo
	ldp	x8,x9,[x3,#8*6]
	ldp	x21,x22,[x1,#8*6]
	add	x1,x1,#8*4
	stp	x14,x15,[x3,#8*0]
	stp	x16,x17,[x3,#8*2]
	add	x3,x3,#8*4
	stp	xzr,xzr,[x1,#8*0]
	stp	xzr,xzr,[x1,#8*2]
	cbnz	x27,.Lsqr4x_cond_copy

	csel	x14,x19,x6,lo
	stp	xzr,xzr,[x2,#8*0]
	csel	x15,x20,x7,lo
	stp	xzr,xzr,[x2,#8*2]
	csel	x16,x21,x8,lo
	csel	x17,x22,x9,lo
	stp	x14,x15,[x3,#8*0]
	stp	x16,x17,[x3,#8*2]

	b	.Lsqr8x_done

.align	4
.Lsqr8x8_post_condition:
	adc	x28,xzr,xzr
	ldr	x30,[x29,#8]		// pull return address
	// x19-7,x28 hold result, x6-7 hold modulus
	subs	x6,x19,x6
	ldr	x1,[x29,#96]		// pull rp
	sbcs	x7,x20,x7
	stp	xzr,xzr,[sp,#8*0]
	sbcs	x8,x21,x8
	stp	xzr,xzr,[sp,#8*2]
	sbcs	x9,x22,x9
	stp	xzr,xzr,[sp,#8*4]
	sbcs	x10,x23,x10
	stp	xzr,xzr,[sp,#8*6]
	sbcs	x11,x24,x11
	stp	xzr,xzr,[sp,#8*8]
	sbcs	x12,x25,x12
	stp	xzr,xzr,[sp,#8*10]
	sbcs	x13,x26,x13
	stp	xzr,xzr,[sp,#8*12]
	sbcs	x28,x28,xzr	// did it borrow?
	stp	xzr,xzr,[sp,#8*14]

	// x6-7 hold result-modulus
	csel	x6,x19,x6,lo
	csel	x7,x20,x7,lo
	csel	x8,x21,x8,lo
	csel	x9,x22,x9,lo
	stp	x6,x7,[x1,#8*0]
	csel	x10,x23,x10,lo
	csel	x11,x24,x11,lo
	stp	x8,x9,[x1,#8*2]
	csel	x12,x25,x12,lo
	csel	x13,x26,x13,lo
	stp	x10,x11,[x1,#8*4]
	stp	x12,x13,[x1,#8*6]

.Lsqr8x_done:
	ldp	x19,x20,[x29,#16]
	mov	sp,x29
	ldp	x21,x22,[x29,#32]
	mov	x0,#1
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldr	x29,[sp],#128
.inst	0xd50323bf		// autiasp
	ret
.size	__bn_sqr8x_mont,.-__bn_sqr8x_mont
.type	__bn_mul4x_mont,%function
.align	5
__bn_mul4x_mont:
.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]

	sub	x26,sp,x5,lsl#3
	lsl	x5,x5,#3
	ldr	x4,[x4]		// *n0
	sub	sp,x26,#8*4		// alloca

	add	x10,x2,x5
	add	x27,x1,x5
	stp	x0,x10,[x29,#96]	// offload rp and &b[num]

	ldr	x24,[x2,#8*0]		// b[0]
	ldp	x6,x7,[x1,#8*0]	// a[0..3]
	ldp	x8,x9,[x1,#8*2]
	add	x1,x1,#8*4
	mov	x19,xzr
	mov	x20,xzr
	mov	x21,xzr
	mov	x22,xzr
	ldp	x14,x15,[x3,#8*0]	// n[0..3]
	ldp	x16,x17,[x3,#8*2]
	adds	x3,x3,#8*4		// clear carry bit
	mov	x0,xzr
	mov	x28,#0
	mov	x26,sp

.Loop_mul4x_1st_reduction:
	mul	x10,x6,x24		// lo(a[0..3]*b[0])
	adc	x0,x0,xzr	// modulo-scheduled
	mul	x11,x7,x24
	add	x28,x28,#8
	mul	x12,x8,x24
	and	x28,x28,#31
	mul	x13,x9,x24
	adds	x19,x19,x10
	umulh	x10,x6,x24		// hi(a[0..3]*b[0])
	adcs	x20,x20,x11
	mul	x25,x19,x4		// t[0]*n0
	adcs	x21,x21,x12
	umulh	x11,x7,x24
	adcs	x22,x22,x13
	umulh	x12,x8,x24
	adc	x23,xzr,xzr
	umulh	x13,x9,x24
	ldr	x24,[x2,x28]		// next b[i] (or b[0])
	adds	x20,x20,x10
	// (*)	mul	x10,x14,x25	// lo(n[0..3]*t[0]*n0)
	str	x25,[x26],#8		// put aside t[0]*n0 for tail processing
	adcs	x21,x21,x11
	mul	x11,x15,x25
	adcs	x22,x22,x12
	mul	x12,x16,x25
	adc	x23,x23,x13		// can't overflow
	mul	x13,x17,x25
	// (*)	adds	xzr,x19,x10
	subs	xzr,x19,#1		// (*)
	umulh	x10,x14,x25		// hi(n[0..3]*t[0]*n0)
	adcs	x19,x20,x11
	umulh	x11,x15,x25
	adcs	x20,x21,x12
	umulh	x12,x16,x25
	adcs	x21,x22,x13
	umulh	x13,x17,x25
	adcs	x22,x23,x0
	adc	x0,xzr,xzr
	adds	x19,x19,x10
	sub	x10,x27,x1
	adcs	x20,x20,x11
	adcs	x21,x21,x12
	adcs	x22,x22,x13
	//adc	x0,x0,xzr
	cbnz	x28,.Loop_mul4x_1st_reduction

	cbz	x10,.Lmul4x4_post_condition

	ldp	x6,x7,[x1,#8*0]	// a[4..7]
	ldp	x8,x9,[x1,#8*2]
	add	x1,x1,#8*4
	ldr	x25,[sp]		// a[0]*n0
	ldp	x14,x15,[x3,#8*0]	// n[4..7]
	ldp	x16,x17,[x3,#8*2]
	add	x3,x3,#8*4

.Loop_mul4x_1st_tail:
	mul	x10,x6,x24		// lo(a[4..7]*b[i])
	adc	x0,x0,xzr	// modulo-scheduled
	mul	x11,x7,x24
	add	x28,x28,#8
	mul	x12,x8,x24
	and	x28,x28,#31
	mul	x13,x9,x24
	adds	x19,x19,x10
	umulh	x10,x6,x24		// hi(a[4..7]*b[i])
	adcs	x20,x20,x11
	umulh	x11,x7,x24
	adcs	x21,x21,x12
	umulh	x12,x8,x24
	adcs	x22,x22,x13
	umulh	x13,x9,x24
	adc	x23,xzr,xzr
	ldr	x24,[x2,x28]		// next b[i] (or b[0])
	adds	x20,x20,x10
	mul	x10,x14,x25		// lo(n[4..7]*a[0]*n0)
	adcs	x21,x21,x11
	mul	x11,x15,x25
	adcs	x22,x22,x12
	mul	x12,x16,x25
	adc	x23,x23,x13		// can't overflow
	mul	x13,x17,x25
	adds	x19,x19,x10
	umulh	x10,x14,x25		// hi(n[4..7]*a[0]*n0)
	adcs	x20,x20,x11
	umulh	x11,x15,x25
	adcs	x21,x21,x12
	umulh	x12,x16,x25
	adcs	x22,x22,x13
	adcs	x23,x23,x0
	umulh	x13,x17,x25
	adc	x0,xzr,xzr
	ldr	x25,[sp,x28]		// next t[0]*n0
	str	x19,[x26],#8		// result!!!
	adds	x19,x20,x10
	sub	x10,x27,x1		// done yet?
	adcs	x20,x21,x11
	adcs	x21,x22,x12
	adcs	x22,x23,x13
	//adc	x0,x0,xzr
	cbnz	x28,.Loop_mul4x_1st_tail

	sub	x11,x27,x5	// rewinded x1
	cbz	x10,.Lmul4x_proceed

	ldp	x6,x7,[x1,#8*0]
	ldp	x8,x9,[x1,#8*2]
	add	x1,x1,#8*4
	ldp	x14,x15,[x3,#8*0]
	ldp	x16,x17,[x3,#8*2]
	add	x3,x3,#8*4
	b	.Loop_mul4x_1st_tail

.align	5
.Lmul4x_proceed:
	ldr	x24,[x2,#8*4]!		// *++b
	adc	x30,x0,xzr
	ldp	x6,x7,[x11,#8*0]	// a[0..3]
	sub	x3,x3,x5		// rewind np
	ldp	x8,x9,[x11,#8*2]
	add	x1,x11,#8*4

	stp	x19,x20,[x26,#8*0]	// result!!!
	ldp	x19,x20,[sp,#8*4]	// t[0..3]
	stp	x21,x22,[x26,#8*2]	// result!!!
	ldp	x21,x22,[sp,#8*6]

	ldp	x14,x15,[x3,#8*0]	// n[0..3]
	mov	x26,sp
	ldp	x16,x17,[x3,#8*2]
	adds	x3,x3,#8*4		// clear carry bit
	mov	x0,xzr

.align	4
.Loop_mul4x_reduction:
	mul	x10,x6,x24		// lo(a[0..3]*b[4])
	adc	x0,x0,xzr	// modulo-scheduled
	mul	x11,x7,x24
	add	x28,x28,#8
	mul	x12,x8,x24
	and	x28,x28,#31
	mul	x13,x9,x24
	adds	x19,x19,x10
	umulh	x10,x6,x24		// hi(a[0..3]*b[4])
	adcs	x20,x20,x11
	mul	x25,x19,x4		// t[0]*n0
	adcs	x21,x21,x12
	umulh	x11,x7,x24
	adcs	x22,x22,x13
	umulh	x12,x8,x24
	adc	x23,xzr,xzr
	umulh	x13,x9,x24
	ldr	x24,[x2,x28]		// next b[i]
	adds	x20,x20,x10
	// (*)	mul	x10,x14,x25
	str	x25,[x26],#8		// put aside t[0]*n0 for tail processing
	adcs	x21,x21,x11
	mul	x11,x15,x25		// lo(n[0..3]*t[0]*n0
	adcs	x22,x22,x12
	mul	x12,x16,x25
	adc	x23,x23,x13		// can't overflow
	mul	x13,x17,x25
	// (*)	adds	xzr,x19,x10
	subs	xzr,x19,#1		// (*)
	umulh	x10,x14,x25		// hi(n[0..3]*t[0]*n0
	adcs	x19,x20,x11
	umulh	x11,x15,x25
	adcs	x20,x21,x12
	umulh	x12,x16,x25
	adcs	x21,x22,x13
	umulh	x13,x17,x25
	adcs	x22,x23,x0
	adc	x0,xzr,xzr
	adds	x19,x19,x10
	adcs	x20,x20,x11
	adcs	x21,x21,x12
	adcs	x22,x22,x13
	//adc	x0,x0,xzr
	cbnz	x28,.Loop_mul4x_reduction

	adc	x0,x0,xzr
	ldp	x10,x11,[x26,#8*4]	// t[4..7]
	ldp	x12,x13,[x26,#8*6]
	ldp	x6,x7,[x1,#8*0]	// a[4..7]
	ldp	x8,x9,[x1,#8*2]
	add	x1,x1,#8*4
	adds	x19,x19,x10
	adcs	x20,x20,x11
	adcs	x21,x21,x12
	adcs	x22,x22,x13
	//adc	x0,x0,xzr

	ldr	x25,[sp]		// t[0]*n0
	ldp	x14,x15,[x3,#8*0]	// n[4..7]
	ldp	x16,x17,[x3,#8*2]
	add	x3,x3,#8*4

.align	4
.Loop_mul4x_tail:
	mul	x10,x6,x24		// lo(a[4..7]*b[4])
	adc	x0,x0,xzr	// modulo-scheduled
	mul	x11,x7,x24
	add	x28,x28,#8
	mul	x12,x8,x24
	and	x28,x28,#31
	mul	x13,x9,x24
	adds	x19,x19,x10
	umulh	x10,x6,x24		// hi(a[4..7]*b[4])
	adcs	x20,x20,x11
	umulh	x11,x7,x24
	adcs	x21,x21,x12
	umulh	x12,x8,x24
	adcs	x22,x22,x13
	umulh	x13,x9,x24
	adc	x23,xzr,xzr
	ldr	x24,[x2,x28]		// next b[i]
	adds	x20,x20,x10
	mul	x10,x14,x25		// lo(n[4..7]*t[0]*n0)
	adcs	x21,x21,x11
	mul	x11,x15,x25
	adcs	x22,x22,x12
	mul	x12,x16,x25
	adc	x23,x23,x13		// can't overflow
	mul	x13,x17,x25
	adds	x19,x19,x10
	umulh	x10,x14,x25		// hi(n[4..7]*t[0]*n0)
	adcs	x20,x20,x11
	umulh	x11,x15,x25
	adcs	x21,x21,x12
	umulh	x12,x16,x25
	adcs	x22,x22,x13
	umulh	x13,x17,x25
	adcs	x23,x23,x0
	ldr	x25,[sp,x28]		// next a[0]*n0
	adc	x0,xzr,xzr
	str	x19,[x26],#8		// result!!!
	adds	x19,x20,x10
	sub	x10,x27,x1		// done yet?
	adcs	x20,x21,x11
	adcs	x21,x22,x12
	adcs	x22,x23,x13
	//adc	x0,x0,xzr
	cbnz	x28,.Loop_mul4x_tail

	sub	x11,x3,x5		// rewinded np?
	adc	x0,x0,xzr
	cbz	x10,.Loop_mul4x_break

	ldp	x10,x11,[x26,#8*4]
	ldp	x12,x13,[x26,#8*6]
	ldp	x6,x7,[x1,#8*0]
	ldp	x8,x9,[x1,#8*2]
	add	x1,x1,#8*4
	adds	x19,x19,x10
	adcs	x20,x20,x11
	adcs	x21,x21,x12
	adcs	x22,x22,x13
	//adc	x0,x0,xzr
	ldp	x14,x15,[x3,#8*0]
	ldp	x16,x17,[x3,#8*2]
	add	x3,x3,#8*4
	b	.Loop_mul4x_tail

.align	4
.Loop_mul4x_break:
	ldp	x12,x13,[x29,#96]	// pull rp and &b[num]
	adds	x19,x19,x30
	add	x2,x2,#8*4		// bp++
	adcs	x20,x20,xzr
	sub	x1,x1,x5		// rewind ap
	adcs	x21,x21,xzr
	stp	x19,x20,[x26,#8*0]	// result!!!
	adcs	x22,x22,xzr
	ldp	x19,x20,[sp,#8*4]	// t[0..3]
	adc	x30,x0,xzr
	stp	x21,x22,[x26,#8*2]	// result!!!
	cmp	x2,x13			// done yet?
	ldp	x21,x22,[sp,#8*6]
	ldp	x14,x15,[x11,#8*0]	// n[0..3]
	ldp	x16,x17,[x11,#8*2]
	add	x3,x11,#8*4
	b.eq	.Lmul4x_post

	ldr	x24,[x2]
	ldp	x6,x7,[x1,#8*0]	// a[0..3]
	ldp	x8,x9,[x1,#8*2]
	adds	x1,x1,#8*4		// clear carry bit
	mov	x0,xzr
	mov	x26,sp
	b	.Loop_mul4x_reduction

.align	4
.Lmul4x_post:
	// Final step. We see if result is larger than modulus, and
	// if it is, subtract the modulus. But comparison implies
	// subtraction. So we subtract modulus, see if it borrowed,
	// and conditionally copy original value.
	mov	x0,x12
	mov	x27,x12		// x0 copy
	subs	x10,x19,x14
	add	x26,sp,#8*8
	sbcs	x11,x20,x15
	sub	x28,x5,#8*4

.Lmul4x_sub:
	sbcs	x12,x21,x16
	ldp	x14,x15,[x3,#8*0]
	sub	x28,x28,#8*4
	ldp	x19,x20,[x26,#8*0]
	sbcs	x13,x22,x17
	ldp	x16,x17,[x3,#8*2]
	add	x3,x3,#8*4
	ldp	x21,x22,[x26,#8*2]
	add	x26,x26,#8*4
	stp	x10,x11,[x0,#8*0]
	sbcs	x10,x19,x14
	stp	x12,x13,[x0,#8*2]
	add	x0,x0,#8*4
	sbcs	x11,x20,x15
	cbnz	x28,.Lmul4x_sub

	sbcs	x12,x21,x16
	mov	x26,sp
	add	x1,sp,#8*4
	ldp	x6,x7,[x27,#8*0]
	sbcs	x13,x22,x17
	stp	x10,x11,[x0,#8*0]
	ldp	x8,x9,[x27,#8*2]
	stp	x12,x13,[x0,#8*2]
	ldp	x19,x20,[x1,#8*0]
	ldp	x21,x22,[x1,#8*2]
	sbcs	xzr,x30,xzr	// did it borrow?
	ldr	x30,[x29,#8]		// pull return address

	sub	x28,x5,#8*4
.Lmul4x_cond_copy:
	sub	x28,x28,#8*4
	csel	x10,x19,x6,lo
	stp	xzr,xzr,[x26,#8*0]
	csel	x11,x20,x7,lo
	ldp	x6,x7,[x27,#8*4]
	ldp	x19,x20,[x1,#8*4]
	csel	x12,x21,x8,lo
	stp	xzr,xzr,[x26,#8*2]
	add	x26,x26,#8*4
	csel	x13,x22,x9,lo
	ldp	x8,x9,[x27,#8*6]
	ldp	x21,x22,[x1,#8*6]
	add	x1,x1,#8*4
	stp	x10,x11,[x27,#8*0]
	stp	x12,x13,[x27,#8*2]
	add	x27,x27,#8*4
	cbnz	x28,.Lmul4x_cond_copy

	csel	x10,x19,x6,lo
	stp	xzr,xzr,[x26,#8*0]
	csel	x11,x20,x7,lo
	stp	xzr,xzr,[x26,#8*2]
	csel	x12,x21,x8,lo
	stp	xzr,xzr,[x26,#8*3]
	csel	x13,x22,x9,lo
	stp	xzr,xzr,[x26,#8*4]
	stp	x10,x11,[x27,#8*0]
	stp	x12,x13,[x27,#8*2]

	b	.Lmul4x_done

.align	4
.Lmul4x4_post_condition:
	adc	x0,x0,xzr
	ldr	x1,[x29,#96]		// pull rp
	// x19-3,x0 hold result, x14-7 hold modulus
	subs	x6,x19,x14
	ldr	x30,[x29,#8]		// pull return address
	sbcs	x7,x20,x15
	stp	xzr,xzr,[sp,#8*0]
	sbcs	x8,x21,x16
	stp	xzr,xzr,[sp,#8*2]
	sbcs	x9,x22,x17
	stp	xzr,xzr,[sp,#8*4]
	sbcs	xzr,x0,xzr		// did it borrow?
	stp	xzr,xzr,[sp,#8*6]

	// x6-3 hold result-modulus
	csel	x6,x19,x6,lo
	csel	x7,x20,x7,lo
	csel	x8,x21,x8,lo
	csel	x9,x22,x9,lo
	stp	x6,x7,[x1,#8*0]
	stp	x8,x9,[x1,#8*2]

.Lmul4x_done:
	ldp	x19,x20,[x29,#16]
	mov	sp,x29
	ldp	x21,x22,[x29,#32]
	mov	x0,#1
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldr	x29,[sp],#128
.inst	0xd50323bf		// autiasp
	ret
.size	__bn_mul4x_mont,.-__bn_mul4x_mont
.byte	77,111,110,116,103,111,109,101,114,121,32,77,117,108,116,105,112,108,105,99,97,116,105,111,110,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	4
