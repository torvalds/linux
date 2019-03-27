/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from poly1305-armv8.pl. */
#include "arm_arch.h"

.text

// forward "declarations" are required for Apple

.globl	poly1305_blocks
.globl	poly1305_emit

.globl	poly1305_init
.type	poly1305_init,%function
.align	5
poly1305_init:
	cmp	x1,xzr
	stp	xzr,xzr,[x0]		// zero hash value
	stp	xzr,xzr,[x0,#16]	// [along with is_base2_26]

	csel	x0,xzr,x0,eq
	b.eq	.Lno_key

#ifdef	__ILP32__
	ldrsw	x11,.LOPENSSL_armcap_P
#else
	ldr	x11,.LOPENSSL_armcap_P
#endif
	adr	x10,.LOPENSSL_armcap_P

	ldp	x7,x8,[x1]		// load key
	mov	x9,#0xfffffffc0fffffff
	movk	x9,#0x0fff,lsl#48
	ldr	w17,[x10,x11]
#ifdef	__ARMEB__
	rev	x7,x7			// flip bytes
	rev	x8,x8
#endif
	and	x7,x7,x9		// &=0ffffffc0fffffff
	and	x9,x9,#-4
	and	x8,x8,x9		// &=0ffffffc0ffffffc
	stp	x7,x8,[x0,#32]	// save key value

	tst	w17,#ARMV7_NEON

	adr	x12,poly1305_blocks
	adr	x7,poly1305_blocks_neon
	adr	x13,poly1305_emit
	adr	x8,poly1305_emit_neon

	csel	x12,x12,x7,eq
	csel	x13,x13,x8,eq

#ifdef	__ILP32__
	stp	w12,w13,[x2]
#else
	stp	x12,x13,[x2]
#endif

	mov	x0,#1
.Lno_key:
	ret
.size	poly1305_init,.-poly1305_init

.type	poly1305_blocks,%function
.align	5
poly1305_blocks:
	ands	x2,x2,#-16
	b.eq	.Lno_data

	ldp	x4,x5,[x0]		// load hash value
	ldp	x7,x8,[x0,#32]	// load key value
	ldr	x6,[x0,#16]
	add	x9,x8,x8,lsr#2	// s1 = r1 + (r1 >> 2)
	b	.Loop

.align	5
.Loop:
	ldp	x10,x11,[x1],#16	// load input
	sub	x2,x2,#16
#ifdef	__ARMEB__
	rev	x10,x10
	rev	x11,x11
#endif
	adds	x4,x4,x10		// accumulate input
	adcs	x5,x5,x11

	mul	x12,x4,x7		// h0*r0
	adc	x6,x6,x3
	umulh	x13,x4,x7

	mul	x10,x5,x9		// h1*5*r1
	umulh	x11,x5,x9

	adds	x12,x12,x10
	mul	x10,x4,x8		// h0*r1
	adc	x13,x13,x11
	umulh	x14,x4,x8

	adds	x13,x13,x10
	mul	x10,x5,x7		// h1*r0
	adc	x14,x14,xzr
	umulh	x11,x5,x7

	adds	x13,x13,x10
	mul	x10,x6,x9		// h2*5*r1
	adc	x14,x14,x11
	mul	x11,x6,x7		// h2*r0

	adds	x13,x13,x10
	adc	x14,x14,x11

	and	x10,x14,#-4		// final reduction
	and	x6,x14,#3
	add	x10,x10,x14,lsr#2
	adds	x4,x12,x10
	adcs	x5,x13,xzr
	adc	x6,x6,xzr

	cbnz	x2,.Loop

	stp	x4,x5,[x0]		// store hash value
	str	x6,[x0,#16]

.Lno_data:
	ret
.size	poly1305_blocks,.-poly1305_blocks

.type	poly1305_emit,%function
.align	5
poly1305_emit:
	ldp	x4,x5,[x0]		// load hash base 2^64
	ldr	x6,[x0,#16]
	ldp	x10,x11,[x2]	// load nonce

	adds	x12,x4,#5		// compare to modulus
	adcs	x13,x5,xzr
	adc	x14,x6,xzr

	tst	x14,#-4			// see if it's carried/borrowed

	csel	x4,x4,x12,eq
	csel	x5,x5,x13,eq

#ifdef	__ARMEB__
	ror	x10,x10,#32		// flip nonce words
	ror	x11,x11,#32
#endif
	adds	x4,x4,x10		// accumulate nonce
	adc	x5,x5,x11
#ifdef	__ARMEB__
	rev	x4,x4			// flip output bytes
	rev	x5,x5
#endif
	stp	x4,x5,[x1]		// write result

	ret
.size	poly1305_emit,.-poly1305_emit
.type	poly1305_mult,%function
.align	5
poly1305_mult:
	mul	x12,x4,x7		// h0*r0
	umulh	x13,x4,x7

	mul	x10,x5,x9		// h1*5*r1
	umulh	x11,x5,x9

	adds	x12,x12,x10
	mul	x10,x4,x8		// h0*r1
	adc	x13,x13,x11
	umulh	x14,x4,x8

	adds	x13,x13,x10
	mul	x10,x5,x7		// h1*r0
	adc	x14,x14,xzr
	umulh	x11,x5,x7

	adds	x13,x13,x10
	mul	x10,x6,x9		// h2*5*r1
	adc	x14,x14,x11
	mul	x11,x6,x7		// h2*r0

	adds	x13,x13,x10
	adc	x14,x14,x11

	and	x10,x14,#-4		// final reduction
	and	x6,x14,#3
	add	x10,x10,x14,lsr#2
	adds	x4,x12,x10
	adcs	x5,x13,xzr
	adc	x6,x6,xzr

	ret
.size	poly1305_mult,.-poly1305_mult

.type	poly1305_splat,%function
.align	5
poly1305_splat:
	and	x12,x4,#0x03ffffff	// base 2^64 -> base 2^26
	ubfx	x13,x4,#26,#26
	extr	x14,x5,x4,#52
	and	x14,x14,#0x03ffffff
	ubfx	x15,x5,#14,#26
	extr	x16,x6,x5,#40

	str	w12,[x0,#16*0]	// r0
	add	w12,w13,w13,lsl#2	// r1*5
	str	w13,[x0,#16*1]	// r1
	add	w13,w14,w14,lsl#2	// r2*5
	str	w12,[x0,#16*2]	// s1
	str	w14,[x0,#16*3]	// r2
	add	w14,w15,w15,lsl#2	// r3*5
	str	w13,[x0,#16*4]	// s2
	str	w15,[x0,#16*5]	// r3
	add	w15,w16,w16,lsl#2	// r4*5
	str	w14,[x0,#16*6]	// s3
	str	w16,[x0,#16*7]	// r4
	str	w15,[x0,#16*8]	// s4

	ret
.size	poly1305_splat,.-poly1305_splat

.type	poly1305_blocks_neon,%function
.align	5
poly1305_blocks_neon:
	ldr	x17,[x0,#24]
	cmp	x2,#128
	b.hs	.Lblocks_neon
	cbz	x17,poly1305_blocks

.Lblocks_neon:
.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-80]!
	add	x29,sp,#0

	ands	x2,x2,#-16
	b.eq	.Lno_data_neon

	cbz	x17,.Lbase2_64_neon

	ldp	w10,w11,[x0]		// load hash value base 2^26
	ldp	w12,w13,[x0,#8]
	ldr	w14,[x0,#16]

	tst	x2,#31
	b.eq	.Leven_neon

	ldp	x7,x8,[x0,#32]	// load key value

	add	x4,x10,x11,lsl#26	// base 2^26 -> base 2^64
	lsr	x5,x12,#12
	adds	x4,x4,x12,lsl#52
	add	x5,x5,x13,lsl#14
	adc	x5,x5,xzr
	lsr	x6,x14,#24
	adds	x5,x5,x14,lsl#40
	adc	x14,x6,xzr		// can be partially reduced...

	ldp	x12,x13,[x1],#16	// load input
	sub	x2,x2,#16
	add	x9,x8,x8,lsr#2	// s1 = r1 + (r1 >> 2)

	and	x10,x14,#-4		// ... so reduce
	and	x6,x14,#3
	add	x10,x10,x14,lsr#2
	adds	x4,x4,x10
	adcs	x5,x5,xzr
	adc	x6,x6,xzr

#ifdef	__ARMEB__
	rev	x12,x12
	rev	x13,x13
#endif
	adds	x4,x4,x12		// accumulate input
	adcs	x5,x5,x13
	adc	x6,x6,x3

	bl	poly1305_mult
	ldr	x30,[sp,#8]

	cbz	x3,.Lstore_base2_64_neon

	and	x10,x4,#0x03ffffff	// base 2^64 -> base 2^26
	ubfx	x11,x4,#26,#26
	extr	x12,x5,x4,#52
	and	x12,x12,#0x03ffffff
	ubfx	x13,x5,#14,#26
	extr	x14,x6,x5,#40

	cbnz	x2,.Leven_neon

	stp	w10,w11,[x0]		// store hash value base 2^26
	stp	w12,w13,[x0,#8]
	str	w14,[x0,#16]
	b	.Lno_data_neon

.align	4
.Lstore_base2_64_neon:
	stp	x4,x5,[x0]		// store hash value base 2^64
	stp	x6,xzr,[x0,#16]	// note that is_base2_26 is zeroed
	b	.Lno_data_neon

.align	4
.Lbase2_64_neon:
	ldp	x7,x8,[x0,#32]	// load key value

	ldp	x4,x5,[x0]		// load hash value base 2^64
	ldr	x6,[x0,#16]

	tst	x2,#31
	b.eq	.Linit_neon

	ldp	x12,x13,[x1],#16	// load input
	sub	x2,x2,#16
	add	x9,x8,x8,lsr#2	// s1 = r1 + (r1 >> 2)
#ifdef	__ARMEB__
	rev	x12,x12
	rev	x13,x13
#endif
	adds	x4,x4,x12		// accumulate input
	adcs	x5,x5,x13
	adc	x6,x6,x3

	bl	poly1305_mult

.Linit_neon:
	and	x10,x4,#0x03ffffff	// base 2^64 -> base 2^26
	ubfx	x11,x4,#26,#26
	extr	x12,x5,x4,#52
	and	x12,x12,#0x03ffffff
	ubfx	x13,x5,#14,#26
	extr	x14,x6,x5,#40

	stp	d8,d9,[sp,#16]		// meet ABI requirements
	stp	d10,d11,[sp,#32]
	stp	d12,d13,[sp,#48]
	stp	d14,d15,[sp,#64]

	fmov	d24,x10
	fmov	d25,x11
	fmov	d26,x12
	fmov	d27,x13
	fmov	d28,x14

	////////////////////////////////// initialize r^n table
	mov	x4,x7			// r^1
	add	x9,x8,x8,lsr#2	// s1 = r1 + (r1 >> 2)
	mov	x5,x8
	mov	x6,xzr
	add	x0,x0,#48+12
	bl	poly1305_splat

	bl	poly1305_mult		// r^2
	sub	x0,x0,#4
	bl	poly1305_splat

	bl	poly1305_mult		// r^3
	sub	x0,x0,#4
	bl	poly1305_splat

	bl	poly1305_mult		// r^4
	sub	x0,x0,#4
	bl	poly1305_splat
	ldr	x30,[sp,#8]

	add	x16,x1,#32
	adr	x17,.Lzeros
	subs	x2,x2,#64
	csel	x16,x17,x16,lo

	mov	x4,#1
	str	x4,[x0,#-24]		// set is_base2_26
	sub	x0,x0,#48		// restore original x0
	b	.Ldo_neon

.align	4
.Leven_neon:
	add	x16,x1,#32
	adr	x17,.Lzeros
	subs	x2,x2,#64
	csel	x16,x17,x16,lo

	stp	d8,d9,[sp,#16]		// meet ABI requirements
	stp	d10,d11,[sp,#32]
	stp	d12,d13,[sp,#48]
	stp	d14,d15,[sp,#64]

	fmov	d24,x10
	fmov	d25,x11
	fmov	d26,x12
	fmov	d27,x13
	fmov	d28,x14

.Ldo_neon:
	ldp	x8,x12,[x16],#16	// inp[2:3] (or zero)
	ldp	x9,x13,[x16],#48

	lsl	x3,x3,#24
	add	x15,x0,#48

#ifdef	__ARMEB__
	rev	x8,x8
	rev	x12,x12
	rev	x9,x9
	rev	x13,x13
#endif
	and	x4,x8,#0x03ffffff	// base 2^64 -> base 2^26
	and	x5,x9,#0x03ffffff
	ubfx	x6,x8,#26,#26
	ubfx	x7,x9,#26,#26
	add	x4,x4,x5,lsl#32		// bfi	x4,x5,#32,#32
	extr	x8,x12,x8,#52
	extr	x9,x13,x9,#52
	add	x6,x6,x7,lsl#32		// bfi	x6,x7,#32,#32
	fmov	d14,x4
	and	x8,x8,#0x03ffffff
	and	x9,x9,#0x03ffffff
	ubfx	x10,x12,#14,#26
	ubfx	x11,x13,#14,#26
	add	x12,x3,x12,lsr#40
	add	x13,x3,x13,lsr#40
	add	x8,x8,x9,lsl#32		// bfi	x8,x9,#32,#32
	fmov	d15,x6
	add	x10,x10,x11,lsl#32	// bfi	x10,x11,#32,#32
	add	x12,x12,x13,lsl#32	// bfi	x12,x13,#32,#32
	fmov	d16,x8
	fmov	d17,x10
	fmov	d18,x12

	ldp	x8,x12,[x1],#16	// inp[0:1]
	ldp	x9,x13,[x1],#48

	ld1	{v0.4s,v1.4s,v2.4s,v3.4s},[x15],#64
	ld1	{v4.4s,v5.4s,v6.4s,v7.4s},[x15],#64
	ld1	{v8.4s},[x15]

#ifdef	__ARMEB__
	rev	x8,x8
	rev	x12,x12
	rev	x9,x9
	rev	x13,x13
#endif
	and	x4,x8,#0x03ffffff	// base 2^64 -> base 2^26
	and	x5,x9,#0x03ffffff
	ubfx	x6,x8,#26,#26
	ubfx	x7,x9,#26,#26
	add	x4,x4,x5,lsl#32		// bfi	x4,x5,#32,#32
	extr	x8,x12,x8,#52
	extr	x9,x13,x9,#52
	add	x6,x6,x7,lsl#32		// bfi	x6,x7,#32,#32
	fmov	d9,x4
	and	x8,x8,#0x03ffffff
	and	x9,x9,#0x03ffffff
	ubfx	x10,x12,#14,#26
	ubfx	x11,x13,#14,#26
	add	x12,x3,x12,lsr#40
	add	x13,x3,x13,lsr#40
	add	x8,x8,x9,lsl#32		// bfi	x8,x9,#32,#32
	fmov	d10,x6
	add	x10,x10,x11,lsl#32	// bfi	x10,x11,#32,#32
	add	x12,x12,x13,lsl#32	// bfi	x12,x13,#32,#32
	movi	v31.2d,#-1
	fmov	d11,x8
	fmov	d12,x10
	fmov	d13,x12
	ushr	v31.2d,v31.2d,#38

	b.ls	.Lskip_loop

.align	4
.Loop_neon:
	////////////////////////////////////////////////////////////////
	// ((inp[0]*r^4+inp[2]*r^2+inp[4])*r^4+inp[6]*r^2
	// ((inp[1]*r^4+inp[3]*r^2+inp[5])*r^3+inp[7]*r
	//   ___________________/
	// ((inp[0]*r^4+inp[2]*r^2+inp[4])*r^4+inp[6]*r^2+inp[8])*r^2
	// ((inp[1]*r^4+inp[3]*r^2+inp[5])*r^4+inp[7]*r^2+inp[9])*r
	//   ___________________/ ____________________/
	//
	// Note that we start with inp[2:3]*r^2. This is because it
	// doesn't depend on reduction in previous iteration.
	////////////////////////////////////////////////////////////////
	// d4 = h0*r4 + h1*r3   + h2*r2   + h3*r1   + h4*r0
	// d3 = h0*r3 + h1*r2   + h2*r1   + h3*r0   + h4*5*r4
	// d2 = h0*r2 + h1*r1   + h2*r0   + h3*5*r4 + h4*5*r3
	// d1 = h0*r1 + h1*r0   + h2*5*r4 + h3*5*r3 + h4*5*r2
	// d0 = h0*r0 + h1*5*r4 + h2*5*r3 + h3*5*r2 + h4*5*r1

	subs	x2,x2,#64
	umull	v23.2d,v14.2s,v7.s[2]
	csel	x16,x17,x16,lo
	umull	v22.2d,v14.2s,v5.s[2]
	umull	v21.2d,v14.2s,v3.s[2]
	ldp	x8,x12,[x16],#16	// inp[2:3] (or zero)
	umull	v20.2d,v14.2s,v1.s[2]
	ldp	x9,x13,[x16],#48
	umull	v19.2d,v14.2s,v0.s[2]
#ifdef	__ARMEB__
	rev	x8,x8
	rev	x12,x12
	rev	x9,x9
	rev	x13,x13
#endif

	umlal	v23.2d,v15.2s,v5.s[2]
	and	x4,x8,#0x03ffffff	// base 2^64 -> base 2^26
	umlal	v22.2d,v15.2s,v3.s[2]
	and	x5,x9,#0x03ffffff
	umlal	v21.2d,v15.2s,v1.s[2]
	ubfx	x6,x8,#26,#26
	umlal	v20.2d,v15.2s,v0.s[2]
	ubfx	x7,x9,#26,#26
	umlal	v19.2d,v15.2s,v8.s[2]
	add	x4,x4,x5,lsl#32		// bfi	x4,x5,#32,#32

	umlal	v23.2d,v16.2s,v3.s[2]
	extr	x8,x12,x8,#52
	umlal	v22.2d,v16.2s,v1.s[2]
	extr	x9,x13,x9,#52
	umlal	v21.2d,v16.2s,v0.s[2]
	add	x6,x6,x7,lsl#32		// bfi	x6,x7,#32,#32
	umlal	v20.2d,v16.2s,v8.s[2]
	fmov	d14,x4
	umlal	v19.2d,v16.2s,v6.s[2]
	and	x8,x8,#0x03ffffff

	umlal	v23.2d,v17.2s,v1.s[2]
	and	x9,x9,#0x03ffffff
	umlal	v22.2d,v17.2s,v0.s[2]
	ubfx	x10,x12,#14,#26
	umlal	v21.2d,v17.2s,v8.s[2]
	ubfx	x11,x13,#14,#26
	umlal	v20.2d,v17.2s,v6.s[2]
	add	x8,x8,x9,lsl#32		// bfi	x8,x9,#32,#32
	umlal	v19.2d,v17.2s,v4.s[2]
	fmov	d15,x6

	add	v11.2s,v11.2s,v26.2s
	add	x12,x3,x12,lsr#40
	umlal	v23.2d,v18.2s,v0.s[2]
	add	x13,x3,x13,lsr#40
	umlal	v22.2d,v18.2s,v8.s[2]
	add	x10,x10,x11,lsl#32	// bfi	x10,x11,#32,#32
	umlal	v21.2d,v18.2s,v6.s[2]
	add	x12,x12,x13,lsl#32	// bfi	x12,x13,#32,#32
	umlal	v20.2d,v18.2s,v4.s[2]
	fmov	d16,x8
	umlal	v19.2d,v18.2s,v2.s[2]
	fmov	d17,x10

	////////////////////////////////////////////////////////////////
	// (hash+inp[0:1])*r^4 and accumulate

	add	v9.2s,v9.2s,v24.2s
	fmov	d18,x12
	umlal	v22.2d,v11.2s,v1.s[0]
	ldp	x8,x12,[x1],#16	// inp[0:1]
	umlal	v19.2d,v11.2s,v6.s[0]
	ldp	x9,x13,[x1],#48
	umlal	v23.2d,v11.2s,v3.s[0]
	umlal	v20.2d,v11.2s,v8.s[0]
	umlal	v21.2d,v11.2s,v0.s[0]
#ifdef	__ARMEB__
	rev	x8,x8
	rev	x12,x12
	rev	x9,x9
	rev	x13,x13
#endif

	add	v10.2s,v10.2s,v25.2s
	umlal	v22.2d,v9.2s,v5.s[0]
	umlal	v23.2d,v9.2s,v7.s[0]
	and	x4,x8,#0x03ffffff	// base 2^64 -> base 2^26
	umlal	v21.2d,v9.2s,v3.s[0]
	and	x5,x9,#0x03ffffff
	umlal	v19.2d,v9.2s,v0.s[0]
	ubfx	x6,x8,#26,#26
	umlal	v20.2d,v9.2s,v1.s[0]
	ubfx	x7,x9,#26,#26

	add	v12.2s,v12.2s,v27.2s
	add	x4,x4,x5,lsl#32		// bfi	x4,x5,#32,#32
	umlal	v22.2d,v10.2s,v3.s[0]
	extr	x8,x12,x8,#52
	umlal	v23.2d,v10.2s,v5.s[0]
	extr	x9,x13,x9,#52
	umlal	v19.2d,v10.2s,v8.s[0]
	add	x6,x6,x7,lsl#32		// bfi	x6,x7,#32,#32
	umlal	v21.2d,v10.2s,v1.s[0]
	fmov	d9,x4
	umlal	v20.2d,v10.2s,v0.s[0]
	and	x8,x8,#0x03ffffff

	add	v13.2s,v13.2s,v28.2s
	and	x9,x9,#0x03ffffff
	umlal	v22.2d,v12.2s,v0.s[0]
	ubfx	x10,x12,#14,#26
	umlal	v19.2d,v12.2s,v4.s[0]
	ubfx	x11,x13,#14,#26
	umlal	v23.2d,v12.2s,v1.s[0]
	add	x8,x8,x9,lsl#32		// bfi	x8,x9,#32,#32
	umlal	v20.2d,v12.2s,v6.s[0]
	fmov	d10,x6
	umlal	v21.2d,v12.2s,v8.s[0]
	add	x12,x3,x12,lsr#40

	umlal	v22.2d,v13.2s,v8.s[0]
	add	x13,x3,x13,lsr#40
	umlal	v19.2d,v13.2s,v2.s[0]
	add	x10,x10,x11,lsl#32	// bfi	x10,x11,#32,#32
	umlal	v23.2d,v13.2s,v0.s[0]
	add	x12,x12,x13,lsl#32	// bfi	x12,x13,#32,#32
	umlal	v20.2d,v13.2s,v4.s[0]
	fmov	d11,x8
	umlal	v21.2d,v13.2s,v6.s[0]
	fmov	d12,x10
	fmov	d13,x12

	/////////////////////////////////////////////////////////////////
	// lazy reduction as discussed in "NEON crypto" by D.J. Bernstein
	// and P. Schwabe
	//
	// [see discussion in poly1305-armv4 module]

	ushr	v29.2d,v22.2d,#26
	xtn	v27.2s,v22.2d
	ushr	v30.2d,v19.2d,#26
	and	v19.16b,v19.16b,v31.16b
	add	v23.2d,v23.2d,v29.2d	// h3 -> h4
	bic	v27.2s,#0xfc,lsl#24	// &=0x03ffffff
	add	v20.2d,v20.2d,v30.2d	// h0 -> h1

	ushr	v29.2d,v23.2d,#26
	xtn	v28.2s,v23.2d
	ushr	v30.2d,v20.2d,#26
	xtn	v25.2s,v20.2d
	bic	v28.2s,#0xfc,lsl#24
	add	v21.2d,v21.2d,v30.2d	// h1 -> h2

	add	v19.2d,v19.2d,v29.2d
	shl	v29.2d,v29.2d,#2
	shrn	v30.2s,v21.2d,#26
	xtn	v26.2s,v21.2d
	add	v19.2d,v19.2d,v29.2d	// h4 -> h0
	bic	v25.2s,#0xfc,lsl#24
	add	v27.2s,v27.2s,v30.2s		// h2 -> h3
	bic	v26.2s,#0xfc,lsl#24

	shrn	v29.2s,v19.2d,#26
	xtn	v24.2s,v19.2d
	ushr	v30.2s,v27.2s,#26
	bic	v27.2s,#0xfc,lsl#24
	bic	v24.2s,#0xfc,lsl#24
	add	v25.2s,v25.2s,v29.2s		// h0 -> h1
	add	v28.2s,v28.2s,v30.2s		// h3 -> h4

	b.hi	.Loop_neon

.Lskip_loop:
	dup	v16.2d,v16.d[0]
	add	v11.2s,v11.2s,v26.2s

	////////////////////////////////////////////////////////////////
	// multiply (inp[0:1]+hash) or inp[2:3] by r^2:r^1

	adds	x2,x2,#32
	b.ne	.Long_tail

	dup	v16.2d,v11.d[0]
	add	v14.2s,v9.2s,v24.2s
	add	v17.2s,v12.2s,v27.2s
	add	v15.2s,v10.2s,v25.2s
	add	v18.2s,v13.2s,v28.2s

.Long_tail:
	dup	v14.2d,v14.d[0]
	umull2	v19.2d,v16.4s,v6.4s
	umull2	v22.2d,v16.4s,v1.4s
	umull2	v23.2d,v16.4s,v3.4s
	umull2	v21.2d,v16.4s,v0.4s
	umull2	v20.2d,v16.4s,v8.4s

	dup	v15.2d,v15.d[0]
	umlal2	v19.2d,v14.4s,v0.4s
	umlal2	v21.2d,v14.4s,v3.4s
	umlal2	v22.2d,v14.4s,v5.4s
	umlal2	v23.2d,v14.4s,v7.4s
	umlal2	v20.2d,v14.4s,v1.4s

	dup	v17.2d,v17.d[0]
	umlal2	v19.2d,v15.4s,v8.4s
	umlal2	v22.2d,v15.4s,v3.4s
	umlal2	v21.2d,v15.4s,v1.4s
	umlal2	v23.2d,v15.4s,v5.4s
	umlal2	v20.2d,v15.4s,v0.4s

	dup	v18.2d,v18.d[0]
	umlal2	v22.2d,v17.4s,v0.4s
	umlal2	v23.2d,v17.4s,v1.4s
	umlal2	v19.2d,v17.4s,v4.4s
	umlal2	v20.2d,v17.4s,v6.4s
	umlal2	v21.2d,v17.4s,v8.4s

	umlal2	v22.2d,v18.4s,v8.4s
	umlal2	v19.2d,v18.4s,v2.4s
	umlal2	v23.2d,v18.4s,v0.4s
	umlal2	v20.2d,v18.4s,v4.4s
	umlal2	v21.2d,v18.4s,v6.4s

	b.eq	.Lshort_tail

	////////////////////////////////////////////////////////////////
	// (hash+inp[0:1])*r^4:r^3 and accumulate

	add	v9.2s,v9.2s,v24.2s
	umlal	v22.2d,v11.2s,v1.2s
	umlal	v19.2d,v11.2s,v6.2s
	umlal	v23.2d,v11.2s,v3.2s
	umlal	v20.2d,v11.2s,v8.2s
	umlal	v21.2d,v11.2s,v0.2s

	add	v10.2s,v10.2s,v25.2s
	umlal	v22.2d,v9.2s,v5.2s
	umlal	v19.2d,v9.2s,v0.2s
	umlal	v23.2d,v9.2s,v7.2s
	umlal	v20.2d,v9.2s,v1.2s
	umlal	v21.2d,v9.2s,v3.2s

	add	v12.2s,v12.2s,v27.2s
	umlal	v22.2d,v10.2s,v3.2s
	umlal	v19.2d,v10.2s,v8.2s
	umlal	v23.2d,v10.2s,v5.2s
	umlal	v20.2d,v10.2s,v0.2s
	umlal	v21.2d,v10.2s,v1.2s

	add	v13.2s,v13.2s,v28.2s
	umlal	v22.2d,v12.2s,v0.2s
	umlal	v19.2d,v12.2s,v4.2s
	umlal	v23.2d,v12.2s,v1.2s
	umlal	v20.2d,v12.2s,v6.2s
	umlal	v21.2d,v12.2s,v8.2s

	umlal	v22.2d,v13.2s,v8.2s
	umlal	v19.2d,v13.2s,v2.2s
	umlal	v23.2d,v13.2s,v0.2s
	umlal	v20.2d,v13.2s,v4.2s
	umlal	v21.2d,v13.2s,v6.2s

.Lshort_tail:
	////////////////////////////////////////////////////////////////
	// horizontal add

	addp	v22.2d,v22.2d,v22.2d
	ldp	d8,d9,[sp,#16]		// meet ABI requirements
	addp	v19.2d,v19.2d,v19.2d
	ldp	d10,d11,[sp,#32]
	addp	v23.2d,v23.2d,v23.2d
	ldp	d12,d13,[sp,#48]
	addp	v20.2d,v20.2d,v20.2d
	ldp	d14,d15,[sp,#64]
	addp	v21.2d,v21.2d,v21.2d

	////////////////////////////////////////////////////////////////
	// lazy reduction, but without narrowing

	ushr	v29.2d,v22.2d,#26
	and	v22.16b,v22.16b,v31.16b
	ushr	v30.2d,v19.2d,#26
	and	v19.16b,v19.16b,v31.16b

	add	v23.2d,v23.2d,v29.2d	// h3 -> h4
	add	v20.2d,v20.2d,v30.2d	// h0 -> h1

	ushr	v29.2d,v23.2d,#26
	and	v23.16b,v23.16b,v31.16b
	ushr	v30.2d,v20.2d,#26
	and	v20.16b,v20.16b,v31.16b
	add	v21.2d,v21.2d,v30.2d	// h1 -> h2

	add	v19.2d,v19.2d,v29.2d
	shl	v29.2d,v29.2d,#2
	ushr	v30.2d,v21.2d,#26
	and	v21.16b,v21.16b,v31.16b
	add	v19.2d,v19.2d,v29.2d	// h4 -> h0
	add	v22.2d,v22.2d,v30.2d	// h2 -> h3

	ushr	v29.2d,v19.2d,#26
	and	v19.16b,v19.16b,v31.16b
	ushr	v30.2d,v22.2d,#26
	and	v22.16b,v22.16b,v31.16b
	add	v20.2d,v20.2d,v29.2d	// h0 -> h1
	add	v23.2d,v23.2d,v30.2d	// h3 -> h4

	////////////////////////////////////////////////////////////////
	// write the result, can be partially reduced

	st4	{v19.s,v20.s,v21.s,v22.s}[0],[x0],#16
	st1	{v23.s}[0],[x0]

.Lno_data_neon:
.inst	0xd50323bf		// autiasp
	ldr	x29,[sp],#80
	ret
.size	poly1305_blocks_neon,.-poly1305_blocks_neon

.type	poly1305_emit_neon,%function
.align	5
poly1305_emit_neon:
	ldr	x17,[x0,#24]
	cbz	x17,poly1305_emit

	ldp	w10,w11,[x0]		// load hash value base 2^26
	ldp	w12,w13,[x0,#8]
	ldr	w14,[x0,#16]

	add	x4,x10,x11,lsl#26	// base 2^26 -> base 2^64
	lsr	x5,x12,#12
	adds	x4,x4,x12,lsl#52
	add	x5,x5,x13,lsl#14
	adc	x5,x5,xzr
	lsr	x6,x14,#24
	adds	x5,x5,x14,lsl#40
	adc	x6,x6,xzr		// can be partially reduced...

	ldp	x10,x11,[x2]	// load nonce

	and	x12,x6,#-4		// ... so reduce
	add	x12,x12,x6,lsr#2
	and	x6,x6,#3
	adds	x4,x4,x12
	adcs	x5,x5,xzr
	adc	x6,x6,xzr

	adds	x12,x4,#5		// compare to modulus
	adcs	x13,x5,xzr
	adc	x14,x6,xzr

	tst	x14,#-4			// see if it's carried/borrowed

	csel	x4,x4,x12,eq
	csel	x5,x5,x13,eq

#ifdef	__ARMEB__
	ror	x10,x10,#32		// flip nonce words
	ror	x11,x11,#32
#endif
	adds	x4,x4,x10		// accumulate nonce
	adc	x5,x5,x11
#ifdef	__ARMEB__
	rev	x4,x4			// flip output bytes
	rev	x5,x5
#endif
	stp	x4,x5,[x1]		// write result

	ret
.size	poly1305_emit_neon,.-poly1305_emit_neon

.align	5
.Lzeros:
.long	0,0,0,0,0,0,0,0
.LOPENSSL_armcap_P:
#ifdef	__ILP32__
.long	OPENSSL_armcap_P-.
#else
.quad	OPENSSL_armcap_P-.
#endif
.byte	80,111,108,121,49,51,48,53,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
.align	2
