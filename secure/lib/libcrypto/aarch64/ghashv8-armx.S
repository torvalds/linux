/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from ghashv8-armx.pl. */
#include "arm_arch.h"

#if __ARM_MAX_ARCH__>=7
.text
.globl	gcm_init_v8
.type	gcm_init_v8,%function
.align	4
gcm_init_v8:
	ld1	{v17.2d},[x1]		//load input H
	movi	v19.16b,#0xe1
	shl	v19.2d,v19.2d,#57		//0xc2.0
	ext	v3.16b,v17.16b,v17.16b,#8
	ushr	v18.2d,v19.2d,#63
	dup	v17.4s,v17.s[1]
	ext	v16.16b,v18.16b,v19.16b,#8		//t0=0xc2....01
	ushr	v18.2d,v3.2d,#63
	sshr	v17.4s,v17.4s,#31		//broadcast carry bit
	and	v18.16b,v18.16b,v16.16b
	shl	v3.2d,v3.2d,#1
	ext	v18.16b,v18.16b,v18.16b,#8
	and	v16.16b,v16.16b,v17.16b
	orr	v3.16b,v3.16b,v18.16b		//H<<<=1
	eor	v20.16b,v3.16b,v16.16b		//twisted H
	st1	{v20.2d},[x0],#16		//store Htable[0]

	//calculate H^2
	ext	v16.16b,v20.16b,v20.16b,#8		//Karatsuba pre-processing
	pmull	v0.1q,v20.1d,v20.1d
	eor	v16.16b,v16.16b,v20.16b
	pmull2	v2.1q,v20.2d,v20.2d
	pmull	v1.1q,v16.1d,v16.1d

	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	eor	v1.16b,v1.16b,v18.16b
	pmull	v18.1q,v0.1d,v19.1d		//1st phase

	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v22.16b,v0.16b,v18.16b

	ext	v17.16b,v22.16b,v22.16b,#8		//Karatsuba pre-processing
	eor	v17.16b,v17.16b,v22.16b
	ext	v21.16b,v16.16b,v17.16b,#8		//pack Karatsuba pre-processed
	st1	{v21.2d,v22.2d},[x0],#32	//store Htable[1..2]
	//calculate H^3 and H^4
	pmull	v0.1q,v20.1d, v22.1d
	pmull	v5.1q,v22.1d,v22.1d
	pmull2	v2.1q,v20.2d, v22.2d
	pmull2	v7.1q,v22.2d,v22.2d
	pmull	v1.1q,v16.1d,v17.1d
	pmull	v6.1q,v17.1d,v17.1d

	ext	v16.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	ext	v17.16b,v5.16b,v7.16b,#8
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v16.16b
	eor	v4.16b,v5.16b,v7.16b
	eor	v6.16b,v6.16b,v17.16b
	eor	v1.16b,v1.16b,v18.16b
	pmull	v18.1q,v0.1d,v19.1d		//1st phase
	eor	v6.16b,v6.16b,v4.16b
	pmull	v4.1q,v5.1d,v19.1d

	ins	v2.d[0],v1.d[1]
	ins	v7.d[0],v6.d[1]
	ins	v1.d[1],v0.d[0]
	ins	v6.d[1],v5.d[0]
	eor	v0.16b,v1.16b,v18.16b
	eor	v5.16b,v6.16b,v4.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase
	ext	v4.16b,v5.16b,v5.16b,#8
	pmull	v0.1q,v0.1d,v19.1d
	pmull	v5.1q,v5.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v4.16b,v4.16b,v7.16b
	eor	v20.16b, v0.16b,v18.16b		//H^3
	eor	v22.16b,v5.16b,v4.16b		//H^4

	ext	v16.16b,v20.16b, v20.16b,#8		//Karatsuba pre-processing
	ext	v17.16b,v22.16b,v22.16b,#8
	eor	v16.16b,v16.16b,v20.16b
	eor	v17.16b,v17.16b,v22.16b
	ext	v21.16b,v16.16b,v17.16b,#8		//pack Karatsuba pre-processed
	st1	{v20.2d,v21.2d,v22.2d},[x0]		//store Htable[3..5]
	ret
.size	gcm_init_v8,.-gcm_init_v8
.globl	gcm_gmult_v8
.type	gcm_gmult_v8,%function
.align	4
gcm_gmult_v8:
	ld1	{v17.2d},[x0]		//load Xi
	movi	v19.16b,#0xe1
	ld1	{v20.2d,v21.2d},[x1]	//load twisted H, ...
	shl	v19.2d,v19.2d,#57
#ifndef __ARMEB__
	rev64	v17.16b,v17.16b
#endif
	ext	v3.16b,v17.16b,v17.16b,#8

	pmull	v0.1q,v20.1d,v3.1d		//H.lo·Xi.lo
	eor	v17.16b,v17.16b,v3.16b		//Karatsuba pre-processing
	pmull2	v2.1q,v20.2d,v3.2d		//H.hi·Xi.hi
	pmull	v1.1q,v21.1d,v17.1d		//(H.lo+H.hi)·(Xi.lo+Xi.hi)

	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	eor	v1.16b,v1.16b,v18.16b
	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction

	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v0.16b,v0.16b,v18.16b

#ifndef __ARMEB__
	rev64	v0.16b,v0.16b
#endif
	ext	v0.16b,v0.16b,v0.16b,#8
	st1	{v0.2d},[x0]		//write out Xi

	ret
.size	gcm_gmult_v8,.-gcm_gmult_v8
.globl	gcm_ghash_v8
.type	gcm_ghash_v8,%function
.align	4
gcm_ghash_v8:
	cmp	x3,#64
	b.hs	.Lgcm_ghash_v8_4x
	ld1	{v0.2d},[x0]		//load [rotated] Xi
						//"[rotated]" means that
						//loaded value would have
						//to be rotated in order to
						//make it appear as in
						//algorithm specification
	subs	x3,x3,#32		//see if x3 is 32 or larger
	mov	x12,#16		//x12 is used as post-
						//increment for input pointer;
						//as loop is modulo-scheduled
						//x12 is zeroed just in time
						//to preclude overstepping
						//inp[len], which means that
						//last block[s] are actually
						//loaded twice, but last
						//copy is not processed
	ld1	{v20.2d,v21.2d},[x1],#32	//load twisted H, ..., H^2
	movi	v19.16b,#0xe1
	ld1	{v22.2d},[x1]
	csel	x12,xzr,x12,eq			//is it time to zero x12?
	ext	v0.16b,v0.16b,v0.16b,#8		//rotate Xi
	ld1	{v16.2d},[x2],#16	//load [rotated] I[0]
	shl	v19.2d,v19.2d,#57		//compose 0xc2.0 constant
#ifndef __ARMEB__
	rev64	v16.16b,v16.16b
	rev64	v0.16b,v0.16b
#endif
	ext	v3.16b,v16.16b,v16.16b,#8		//rotate I[0]
	b.lo	.Lodd_tail_v8		//x3 was less than 32
	ld1	{v17.2d},[x2],x12	//load [rotated] I[1]
#ifndef __ARMEB__
	rev64	v17.16b,v17.16b
#endif
	ext	v7.16b,v17.16b,v17.16b,#8
	eor	v3.16b,v3.16b,v0.16b		//I[i]^=Xi
	pmull	v4.1q,v20.1d,v7.1d		//H·Ii+1
	eor	v17.16b,v17.16b,v7.16b		//Karatsuba pre-processing
	pmull2	v6.1q,v20.2d,v7.2d
	b	.Loop_mod2x_v8

.align	4
.Loop_mod2x_v8:
	ext	v18.16b,v3.16b,v3.16b,#8
	subs	x3,x3,#32		//is there more data?
	pmull	v0.1q,v22.1d,v3.1d		//H^2.lo·Xi.lo
	csel	x12,xzr,x12,lo			//is it time to zero x12?

	pmull	v5.1q,v21.1d,v17.1d
	eor	v18.16b,v18.16b,v3.16b		//Karatsuba pre-processing
	pmull2	v2.1q,v22.2d,v3.2d		//H^2.hi·Xi.hi
	eor	v0.16b,v0.16b,v4.16b		//accumulate
	pmull2	v1.1q,v21.2d,v18.2d		//(H^2.lo+H^2.hi)·(Xi.lo+Xi.hi)
	ld1	{v16.2d},[x2],x12	//load [rotated] I[i+2]

	eor	v2.16b,v2.16b,v6.16b
	csel	x12,xzr,x12,eq			//is it time to zero x12?
	eor	v1.16b,v1.16b,v5.16b

	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	ld1	{v17.2d},[x2],x12	//load [rotated] I[i+3]
#ifndef __ARMEB__
	rev64	v16.16b,v16.16b
#endif
	eor	v1.16b,v1.16b,v18.16b
	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction

#ifndef __ARMEB__
	rev64	v17.16b,v17.16b
#endif
	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	ext	v7.16b,v17.16b,v17.16b,#8
	ext	v3.16b,v16.16b,v16.16b,#8
	eor	v0.16b,v1.16b,v18.16b
	pmull	v4.1q,v20.1d,v7.1d		//H·Ii+1
	eor	v3.16b,v3.16b,v2.16b		//accumulate v3.16b early

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v3.16b,v3.16b,v18.16b
	eor	v17.16b,v17.16b,v7.16b		//Karatsuba pre-processing
	eor	v3.16b,v3.16b,v0.16b
	pmull2	v6.1q,v20.2d,v7.2d
	b.hs	.Loop_mod2x_v8		//there was at least 32 more bytes

	eor	v2.16b,v2.16b,v18.16b
	ext	v3.16b,v16.16b,v16.16b,#8		//re-construct v3.16b
	adds	x3,x3,#32		//re-construct x3
	eor	v0.16b,v0.16b,v2.16b		//re-construct v0.16b
	b.eq	.Ldone_v8		//is x3 zero?
.Lodd_tail_v8:
	ext	v18.16b,v0.16b,v0.16b,#8
	eor	v3.16b,v3.16b,v0.16b		//inp^=Xi
	eor	v17.16b,v16.16b,v18.16b		//v17.16b is rotated inp^Xi

	pmull	v0.1q,v20.1d,v3.1d		//H.lo·Xi.lo
	eor	v17.16b,v17.16b,v3.16b		//Karatsuba pre-processing
	pmull2	v2.1q,v20.2d,v3.2d		//H.hi·Xi.hi
	pmull	v1.1q,v21.1d,v17.1d		//(H.lo+H.hi)·(Xi.lo+Xi.hi)

	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	eor	v1.16b,v1.16b,v18.16b
	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction

	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v0.16b,v0.16b,v18.16b

.Ldone_v8:
#ifndef __ARMEB__
	rev64	v0.16b,v0.16b
#endif
	ext	v0.16b,v0.16b,v0.16b,#8
	st1	{v0.2d},[x0]		//write out Xi

	ret
.size	gcm_ghash_v8,.-gcm_ghash_v8
.type	gcm_ghash_v8_4x,%function
.align	4
gcm_ghash_v8_4x:
.Lgcm_ghash_v8_4x:
	ld1	{v0.2d},[x0]		//load [rotated] Xi
	ld1	{v20.2d,v21.2d,v22.2d},[x1],#48	//load twisted H, ..., H^2
	movi	v19.16b,#0xe1
	ld1	{v26.2d,v27.2d,v28.2d},[x1]	//load twisted H^3, ..., H^4
	shl	v19.2d,v19.2d,#57		//compose 0xc2.0 constant

	ld1	{v4.2d,v5.2d,v6.2d,v7.2d},[x2],#64
#ifndef __ARMEB__
	rev64	v0.16b,v0.16b
	rev64	v5.16b,v5.16b
	rev64	v6.16b,v6.16b
	rev64	v7.16b,v7.16b
	rev64	v4.16b,v4.16b
#endif
	ext	v25.16b,v7.16b,v7.16b,#8
	ext	v24.16b,v6.16b,v6.16b,#8
	ext	v23.16b,v5.16b,v5.16b,#8

	pmull	v29.1q,v20.1d,v25.1d		//H·Ii+3
	eor	v7.16b,v7.16b,v25.16b
	pmull2	v31.1q,v20.2d,v25.2d
	pmull	v30.1q,v21.1d,v7.1d

	pmull	v16.1q,v22.1d,v24.1d		//H^2·Ii+2
	eor	v6.16b,v6.16b,v24.16b
	pmull2	v24.1q,v22.2d,v24.2d
	pmull2	v6.1q,v21.2d,v6.2d

	eor	v29.16b,v29.16b,v16.16b
	eor	v31.16b,v31.16b,v24.16b
	eor	v30.16b,v30.16b,v6.16b

	pmull	v7.1q,v26.1d,v23.1d		//H^3·Ii+1
	eor	v5.16b,v5.16b,v23.16b
	pmull2	v23.1q,v26.2d,v23.2d
	pmull	v5.1q,v27.1d,v5.1d

	eor	v29.16b,v29.16b,v7.16b
	eor	v31.16b,v31.16b,v23.16b
	eor	v30.16b,v30.16b,v5.16b

	subs	x3,x3,#128
	b.lo	.Ltail4x

	b	.Loop4x

.align	4
.Loop4x:
	eor	v16.16b,v4.16b,v0.16b
	ld1	{v4.2d,v5.2d,v6.2d,v7.2d},[x2],#64
	ext	v3.16b,v16.16b,v16.16b,#8
#ifndef __ARMEB__
	rev64	v5.16b,v5.16b
	rev64	v6.16b,v6.16b
	rev64	v7.16b,v7.16b
	rev64	v4.16b,v4.16b
#endif

	pmull	v0.1q,v28.1d,v3.1d		//H^4·(Xi+Ii)
	eor	v16.16b,v16.16b,v3.16b
	pmull2	v2.1q,v28.2d,v3.2d
	ext	v25.16b,v7.16b,v7.16b,#8
	pmull2	v1.1q,v27.2d,v16.2d

	eor	v0.16b,v0.16b,v29.16b
	eor	v2.16b,v2.16b,v31.16b
	ext	v24.16b,v6.16b,v6.16b,#8
	eor	v1.16b,v1.16b,v30.16b
	ext	v23.16b,v5.16b,v5.16b,#8

	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	pmull	v29.1q,v20.1d,v25.1d		//H·Ii+3
	eor	v7.16b,v7.16b,v25.16b
	eor	v1.16b,v1.16b,v17.16b
	pmull2	v31.1q,v20.2d,v25.2d
	eor	v1.16b,v1.16b,v18.16b
	pmull	v30.1q,v21.1d,v7.1d

	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction
	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	pmull	v16.1q,v22.1d,v24.1d		//H^2·Ii+2
	eor	v6.16b,v6.16b,v24.16b
	pmull2	v24.1q,v22.2d,v24.2d
	eor	v0.16b,v1.16b,v18.16b
	pmull2	v6.1q,v21.2d,v6.2d

	eor	v29.16b,v29.16b,v16.16b
	eor	v31.16b,v31.16b,v24.16b
	eor	v30.16b,v30.16b,v6.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	pmull	v7.1q,v26.1d,v23.1d		//H^3·Ii+1
	eor	v5.16b,v5.16b,v23.16b
	eor	v18.16b,v18.16b,v2.16b
	pmull2	v23.1q,v26.2d,v23.2d
	pmull	v5.1q,v27.1d,v5.1d

	eor	v0.16b,v0.16b,v18.16b
	eor	v29.16b,v29.16b,v7.16b
	eor	v31.16b,v31.16b,v23.16b
	ext	v0.16b,v0.16b,v0.16b,#8
	eor	v30.16b,v30.16b,v5.16b

	subs	x3,x3,#64
	b.hs	.Loop4x

.Ltail4x:
	eor	v16.16b,v4.16b,v0.16b
	ext	v3.16b,v16.16b,v16.16b,#8

	pmull	v0.1q,v28.1d,v3.1d		//H^4·(Xi+Ii)
	eor	v16.16b,v16.16b,v3.16b
	pmull2	v2.1q,v28.2d,v3.2d
	pmull2	v1.1q,v27.2d,v16.2d

	eor	v0.16b,v0.16b,v29.16b
	eor	v2.16b,v2.16b,v31.16b
	eor	v1.16b,v1.16b,v30.16b

	adds	x3,x3,#64
	b.eq	.Ldone4x

	cmp	x3,#32
	b.lo	.Lone
	b.eq	.Ltwo
.Lthree:
	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	ld1	{v4.2d,v5.2d,v6.2d},[x2]
	eor	v1.16b,v1.16b,v18.16b
#ifndef	__ARMEB__
	rev64	v5.16b,v5.16b
	rev64	v6.16b,v6.16b
	rev64	v4.16b,v4.16b
#endif

	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction
	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	ext	v24.16b,v6.16b,v6.16b,#8
	ext	v23.16b,v5.16b,v5.16b,#8
	eor	v0.16b,v1.16b,v18.16b

	pmull	v29.1q,v20.1d,v24.1d		//H·Ii+2
	eor	v6.16b,v6.16b,v24.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	pmull2	v31.1q,v20.2d,v24.2d
	pmull	v30.1q,v21.1d,v6.1d
	eor	v0.16b,v0.16b,v18.16b
	pmull	v7.1q,v22.1d,v23.1d		//H^2·Ii+1
	eor	v5.16b,v5.16b,v23.16b
	ext	v0.16b,v0.16b,v0.16b,#8

	pmull2	v23.1q,v22.2d,v23.2d
	eor	v16.16b,v4.16b,v0.16b
	pmull2	v5.1q,v21.2d,v5.2d
	ext	v3.16b,v16.16b,v16.16b,#8

	eor	v29.16b,v29.16b,v7.16b
	eor	v31.16b,v31.16b,v23.16b
	eor	v30.16b,v30.16b,v5.16b

	pmull	v0.1q,v26.1d,v3.1d		//H^3·(Xi+Ii)
	eor	v16.16b,v16.16b,v3.16b
	pmull2	v2.1q,v26.2d,v3.2d
	pmull	v1.1q,v27.1d,v16.1d

	eor	v0.16b,v0.16b,v29.16b
	eor	v2.16b,v2.16b,v31.16b
	eor	v1.16b,v1.16b,v30.16b
	b	.Ldone4x

.align	4
.Ltwo:
	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	ld1	{v4.2d,v5.2d},[x2]
	eor	v1.16b,v1.16b,v18.16b
#ifndef	__ARMEB__
	rev64	v5.16b,v5.16b
	rev64	v4.16b,v4.16b
#endif

	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction
	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	ext	v23.16b,v5.16b,v5.16b,#8
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v0.16b,v0.16b,v18.16b
	ext	v0.16b,v0.16b,v0.16b,#8

	pmull	v29.1q,v20.1d,v23.1d		//H·Ii+1
	eor	v5.16b,v5.16b,v23.16b

	eor	v16.16b,v4.16b,v0.16b
	ext	v3.16b,v16.16b,v16.16b,#8

	pmull2	v31.1q,v20.2d,v23.2d
	pmull	v30.1q,v21.1d,v5.1d

	pmull	v0.1q,v22.1d,v3.1d		//H^2·(Xi+Ii)
	eor	v16.16b,v16.16b,v3.16b
	pmull2	v2.1q,v22.2d,v3.2d
	pmull2	v1.1q,v21.2d,v16.2d

	eor	v0.16b,v0.16b,v29.16b
	eor	v2.16b,v2.16b,v31.16b
	eor	v1.16b,v1.16b,v30.16b
	b	.Ldone4x

.align	4
.Lone:
	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	ld1	{v4.2d},[x2]
	eor	v1.16b,v1.16b,v18.16b
#ifndef	__ARMEB__
	rev64	v4.16b,v4.16b
#endif

	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction
	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v0.16b,v0.16b,v18.16b
	ext	v0.16b,v0.16b,v0.16b,#8

	eor	v16.16b,v4.16b,v0.16b
	ext	v3.16b,v16.16b,v16.16b,#8

	pmull	v0.1q,v20.1d,v3.1d
	eor	v16.16b,v16.16b,v3.16b
	pmull2	v2.1q,v20.2d,v3.2d
	pmull	v1.1q,v21.1d,v16.1d

.Ldone4x:
	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	eor	v1.16b,v1.16b,v18.16b

	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction
	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v0.16b,v0.16b,v18.16b
	ext	v0.16b,v0.16b,v0.16b,#8

#ifndef __ARMEB__
	rev64	v0.16b,v0.16b
#endif
	st1	{v0.2d},[x0]		//write out Xi

	ret
.size	gcm_ghash_v8_4x,.-gcm_ghash_v8_4x
.byte	71,72,65,83,72,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
#endif
