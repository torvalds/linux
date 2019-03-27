/* $FreeBSD$ */
/* Do not modify. This file is auto-generated from keccak1600-armv8.pl. */
.text

.align	8	// strategic alignment and padding that allows to use
		// address value as loop termination condition...
.quad	0,0,0,0,0,0,0,0
.type	iotas,%object
iotas:
.quad	0x0000000000000001
.quad	0x0000000000008082
.quad	0x800000000000808a
.quad	0x8000000080008000
.quad	0x000000000000808b
.quad	0x0000000080000001
.quad	0x8000000080008081
.quad	0x8000000000008009
.quad	0x000000000000008a
.quad	0x0000000000000088
.quad	0x0000000080008009
.quad	0x000000008000000a
.quad	0x000000008000808b
.quad	0x800000000000008b
.quad	0x8000000000008089
.quad	0x8000000000008003
.quad	0x8000000000008002
.quad	0x8000000000000080
.quad	0x000000000000800a
.quad	0x800000008000000a
.quad	0x8000000080008081
.quad	0x8000000000008080
.quad	0x0000000080000001
.quad	0x8000000080008008
.size	iotas,.-iotas
.type	KeccakF1600_int,%function
.align	5
KeccakF1600_int:
	adr	x28,iotas
.inst	0xd503233f			// paciasp
	stp	x28,x30,[sp,#16]		// 32 bytes on top are mine
	b	.Loop
.align	4
.Loop:
	////////////////////////////////////////// Theta
	eor	x26,x0,x5
	stp	x4,x9,[sp,#0]	// offload pair...
	eor	x27,x1,x6
	eor	x28,x2,x7
	eor	x30,x3,x8
	eor	x4,x4,x9
	eor	x26,x26,x10
	eor	x27,x27,x11
	eor	x28,x28,x12
	eor	x30,x30,x13
	eor	x4,x4,x14
	eor	x26,x26,x15
	eor	x27,x27,x16
	eor	x28,x28,x17
	eor	x30,x30,x25
	eor	x4,x4,x19
	eor	x26,x26,x20
	eor	x28,x28,x22
	eor	x27,x27,x21
	eor	x30,x30,x23
	eor	x4,x4,x24

	eor	x9,x26,x28,ror#63

	eor	x1,x1,x9
	eor	x6,x6,x9
	eor	x11,x11,x9
	eor	x16,x16,x9
	eor	x21,x21,x9

	eor	x9,x27,x30,ror#63
	eor	x28,x28,x4,ror#63
	eor	x30,x30,x26,ror#63
	eor	x4,x4,x27,ror#63

	eor	x27,   x2,x9		// mov	x27,x2
	eor	x7,x7,x9
	eor	x12,x12,x9
	eor	x17,x17,x9
	eor	x22,x22,x9

	eor	x0,x0,x4
	eor	x5,x5,x4
	eor	x10,x10,x4
	eor	x15,x15,x4
	eor	x20,x20,x4
	ldp	x4,x9,[sp,#0]	// re-load offloaded data
	eor	x26,   x3,x28		// mov	x26,x3
	eor	x8,x8,x28
	eor	x13,x13,x28
	eor	x25,x25,x28
	eor	x23,x23,x28

	eor	x28,   x4,x30		// mov	x28,x4
	eor	x9,x9,x30
	eor	x14,x14,x30
	eor	x19,x19,x30
	eor	x24,x24,x30

	////////////////////////////////////////// Rho+Pi
	mov	x30,x1
	ror	x1,x6,#64-44
	//mov	x27,x2
	ror	x2,x12,#64-43
	//mov	x26,x3
	ror	x3,x25,#64-21
	//mov	x28,x4
	ror	x4,x24,#64-14

	ror	x6,x9,#64-20
	ror	x12,x13,#64-25
	ror	x25,x17,#64-15
	ror	x24,x21,#64-2

	ror	x9,x22,#64-61
	ror	x13,x19,#64-8
	ror	x17,x11,#64-10
	ror	x21,x8,#64-55

	ror	x22,x14,#64-39
	ror	x19,x23,#64-56
	ror	x11,x7,#64-6
	ror	x8,x16,#64-45

	ror	x14,x20,#64-18
	ror	x23,x15,#64-41
	ror	x7,x10,#64-3
	ror	x16,x5,#64-36

	ror	x5,x26,#64-28
	ror	x10,x30,#64-1
	ror	x15,x28,#64-27
	ror	x20,x27,#64-62

	////////////////////////////////////////// Chi+Iota
	bic	x26,x2,x1
	bic	x27,x3,x2
	bic	x28,x0,x4
	bic	x30,x1,x0
	eor	x0,x0,x26
	bic	x26,x4,x3
	eor	x1,x1,x27
	ldr	x27,[sp,#16]
	eor	x3,x3,x28
	eor	x4,x4,x30
	eor	x2,x2,x26
	ldr	x30,[x27],#8		// Iota[i++]

	bic	x26,x7,x6
	tst	x27,#255			// are we done?
	str	x27,[sp,#16]
	bic	x27,x8,x7
	bic	x28,x5,x9
	eor	x0,x0,x30		// A[0][0] ^= Iota
	bic	x30,x6,x5
	eor	x5,x5,x26
	bic	x26,x9,x8
	eor	x6,x6,x27
	eor	x8,x8,x28
	eor	x9,x9,x30
	eor	x7,x7,x26

	bic	x26,x12,x11
	bic	x27,x13,x12
	bic	x28,x10,x14
	bic	x30,x11,x10
	eor	x10,x10,x26
	bic	x26,x14,x13
	eor	x11,x11,x27
	eor	x13,x13,x28
	eor	x14,x14,x30
	eor	x12,x12,x26

	bic	x26,x17,x16
	bic	x27,x25,x17
	bic	x28,x15,x19
	bic	x30,x16,x15
	eor	x15,x15,x26
	bic	x26,x19,x25
	eor	x16,x16,x27
	eor	x25,x25,x28
	eor	x19,x19,x30
	eor	x17,x17,x26

	bic	x26,x22,x21
	bic	x27,x23,x22
	bic	x28,x20,x24
	bic	x30,x21,x20
	eor	x20,x20,x26
	bic	x26,x24,x23
	eor	x21,x21,x27
	eor	x23,x23,x28
	eor	x24,x24,x30
	eor	x22,x22,x26

	bne	.Loop

	ldr	x30,[sp,#24]
.inst	0xd50323bf			// autiasp
	ret
.size	KeccakF1600_int,.-KeccakF1600_int

.type	KeccakF1600,%function
.align	5
KeccakF1600:
.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	sub	sp,sp,#48

	str	x0,[sp,#32]			// offload argument
	mov	x26,x0
	ldp	x0,x1,[x0,#16*0]
	ldp	x2,x3,[x26,#16*1]
	ldp	x4,x5,[x26,#16*2]
	ldp	x6,x7,[x26,#16*3]
	ldp	x8,x9,[x26,#16*4]
	ldp	x10,x11,[x26,#16*5]
	ldp	x12,x13,[x26,#16*6]
	ldp	x14,x15,[x26,#16*7]
	ldp	x16,x17,[x26,#16*8]
	ldp	x25,x19,[x26,#16*9]
	ldp	x20,x21,[x26,#16*10]
	ldp	x22,x23,[x26,#16*11]
	ldr	x24,[x26,#16*12]

	bl	KeccakF1600_int

	ldr	x26,[sp,#32]
	stp	x0,x1,[x26,#16*0]
	stp	x2,x3,[x26,#16*1]
	stp	x4,x5,[x26,#16*2]
	stp	x6,x7,[x26,#16*3]
	stp	x8,x9,[x26,#16*4]
	stp	x10,x11,[x26,#16*5]
	stp	x12,x13,[x26,#16*6]
	stp	x14,x15,[x26,#16*7]
	stp	x16,x17,[x26,#16*8]
	stp	x25,x19,[x26,#16*9]
	stp	x20,x21,[x26,#16*10]
	stp	x22,x23,[x26,#16*11]
	str	x24,[x26,#16*12]

	ldp	x19,x20,[x29,#16]
	add	sp,sp,#48
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#128
.inst	0xd50323bf			// autiasp
	ret
.size	KeccakF1600,.-KeccakF1600

.globl	SHA3_absorb
.type	SHA3_absorb,%function
.align	5
SHA3_absorb:
.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-128]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]
	stp	x23,x24,[sp,#48]
	stp	x25,x26,[sp,#64]
	stp	x27,x28,[sp,#80]
	sub	sp,sp,#64

	stp	x0,x1,[sp,#32]			// offload arguments
	stp	x2,x3,[sp,#48]

	mov	x26,x0			// uint64_t A[5][5]
	mov	x27,x1			// const void *inp
	mov	x28,x2			// size_t len
	mov	x30,x3			// size_t bsz
	ldp	x0,x1,[x26,#16*0]
	ldp	x2,x3,[x26,#16*1]
	ldp	x4,x5,[x26,#16*2]
	ldp	x6,x7,[x26,#16*3]
	ldp	x8,x9,[x26,#16*4]
	ldp	x10,x11,[x26,#16*5]
	ldp	x12,x13,[x26,#16*6]
	ldp	x14,x15,[x26,#16*7]
	ldp	x16,x17,[x26,#16*8]
	ldp	x25,x19,[x26,#16*9]
	ldp	x20,x21,[x26,#16*10]
	ldp	x22,x23,[x26,#16*11]
	ldr	x24,[x26,#16*12]
	b	.Loop_absorb

.align	4
.Loop_absorb:
	subs	x26,x28,x30		// len - bsz
	blo	.Labsorbed

	str	x26,[sp,#48]			// save len - bsz
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x0,x0,x26
	cmp	x30,#8*(0+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x1,x1,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x2,x2,x26
	cmp	x30,#8*(2+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x3,x3,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x4,x4,x26
	cmp	x30,#8*(4+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x5,x5,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x6,x6,x26
	cmp	x30,#8*(6+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x7,x7,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x8,x8,x26
	cmp	x30,#8*(8+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x9,x9,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x10,x10,x26
	cmp	x30,#8*(10+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x11,x11,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x12,x12,x26
	cmp	x30,#8*(12+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x13,x13,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x14,x14,x26
	cmp	x30,#8*(14+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x15,x15,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x16,x16,x26
	cmp	x30,#8*(16+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x17,x17,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x25,x25,x26
	cmp	x30,#8*(18+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x19,x19,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x20,x20,x26
	cmp	x30,#8*(20+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x21,x21,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x22,x22,x26
	cmp	x30,#8*(22+2)
	blo	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x23,x23,x26
	beq	.Lprocess_block
	ldr	x26,[x27],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	x26,x26
#endif
	eor	x24,x24,x26

.Lprocess_block:
	str	x27,[sp,#40]			// save inp

	bl	KeccakF1600_int

	ldr	x27,[sp,#40]			// restore arguments
	ldp	x28,x30,[sp,#48]
	b	.Loop_absorb

.align	4
.Labsorbed:
	ldr	x27,[sp,#32]
	stp	x0,x1,[x27,#16*0]
	stp	x2,x3,[x27,#16*1]
	stp	x4,x5,[x27,#16*2]
	stp	x6,x7,[x27,#16*3]
	stp	x8,x9,[x27,#16*4]
	stp	x10,x11,[x27,#16*5]
	stp	x12,x13,[x27,#16*6]
	stp	x14,x15,[x27,#16*7]
	stp	x16,x17,[x27,#16*8]
	stp	x25,x19,[x27,#16*9]
	stp	x20,x21,[x27,#16*10]
	stp	x22,x23,[x27,#16*11]
	str	x24,[x27,#16*12]

	mov	x0,x28			// return value
	ldp	x19,x20,[x29,#16]
	add	sp,sp,#64
	ldp	x21,x22,[x29,#32]
	ldp	x23,x24,[x29,#48]
	ldp	x25,x26,[x29,#64]
	ldp	x27,x28,[x29,#80]
	ldp	x29,x30,[sp],#128
.inst	0xd50323bf			// autiasp
	ret
.size	SHA3_absorb,.-SHA3_absorb
.globl	SHA3_squeeze
.type	SHA3_squeeze,%function
.align	5
SHA3_squeeze:
.inst	0xd503233f			// paciasp
	stp	x29,x30,[sp,#-48]!
	add	x29,sp,#0
	stp	x19,x20,[sp,#16]
	stp	x21,x22,[sp,#32]

	mov	x19,x0			// put aside arguments
	mov	x20,x1
	mov	x21,x2
	mov	x22,x3

.Loop_squeeze:
	ldr	x4,[x0],#8
	cmp	x21,#8
	blo	.Lsqueeze_tail
#ifdef	__AARCH64EB__
	rev	x4,x4
#endif
	str	x4,[x20],#8
	subs	x21,x21,#8
	beq	.Lsqueeze_done

	subs	x3,x3,#8
	bhi	.Loop_squeeze

	mov	x0,x19
	bl	KeccakF1600
	mov	x0,x19
	mov	x3,x22
	b	.Loop_squeeze

.align	4
.Lsqueeze_tail:
	strb	w4,[x20],#1
	lsr	x4,x4,#8
	subs	x21,x21,#1
	beq	.Lsqueeze_done
	strb	w4,[x20],#1
	lsr	x4,x4,#8
	subs	x21,x21,#1
	beq	.Lsqueeze_done
	strb	w4,[x20],#1
	lsr	x4,x4,#8
	subs	x21,x21,#1
	beq	.Lsqueeze_done
	strb	w4,[x20],#1
	lsr	x4,x4,#8
	subs	x21,x21,#1
	beq	.Lsqueeze_done
	strb	w4,[x20],#1
	lsr	x4,x4,#8
	subs	x21,x21,#1
	beq	.Lsqueeze_done
	strb	w4,[x20],#1
	lsr	x4,x4,#8
	subs	x21,x21,#1
	beq	.Lsqueeze_done
	strb	w4,[x20],#1

.Lsqueeze_done:
	ldp	x19,x20,[sp,#16]
	ldp	x21,x22,[sp,#32]
	ldp	x29,x30,[sp],#48
.inst	0xd50323bf			// autiasp
	ret
.size	SHA3_squeeze,.-SHA3_squeeze
.type	KeccakF1600_ce,%function
.align	5
KeccakF1600_ce:
	mov	x9,#12
	adr	x10,iotas
	b	.Loop_ce
.align	4
.Loop_ce:
	////////////////////////////////////////////////// Theta
.inst	0xce052819	//eor3 v25.16b,v0.16b,v5.16b,v10.16b
.inst	0xce062c3a	//eor3 v26.16b,v1.16b,v6.16b,v11.16b
.inst	0xce07305b	//eor3 v27.16b,v2.16b,v7.16b,v12.16b
.inst	0xce08347c	//eor3 v28.16b,v3.16b,v8.16b,v13.16b
.inst	0xce09389d	//eor3 v29.16b,v4.16b,v9.16b,v14.16b
.inst	0xce0f5339	//eor3 v25.16b,v25.16b,   v15.16b,v20.16b
.inst	0xce10575a	//eor3 v26.16b,v26.16b,   v16.16b,v21.16b
.inst	0xce115b7b	//eor3 v27.16b,v27.16b,   v17.16b,v22.16b
.inst	0xce125f9c	//eor3 v28.16b,v28.16b,   v18.16b,v23.16b
.inst	0xce1363bd	//eor3 v29.16b,v29.16b,   v19.16b,v24.16b

.inst	0xce7b8f3e	//rax1 v30.16b,v25.16b,v27.16b			// D[1]
.inst	0xce7c8f5f	//rax1 v31.16b,v26.16b,v28.16b			// D[2]
.inst	0xce7d8f7b	//rax1 v27.16b,v27.16b,v29.16b			// D[3]
.inst	0xce798f9c	//rax1 v28.16b,v28.16b,v25.16b			// D[4]
.inst	0xce7a8fbd	//rax1 v29.16b,v29.16b,v26.16b			// D[0]

	////////////////////////////////////////////////// Theta+Rho+Pi
.inst	0xce9e50d9	//xar v25.16b,   v6.16b,v30.16b,#64-44	// C[0]=A[0][1]
.inst	0xce9cb126	//xar v6.16b,v9.16b,v28.16b,#64-20
.inst	0xce9f0ec9	//xar v9.16b,v22.16b,v31.16b,#64-61
.inst	0xce9c65d6	//xar v22.16b,v14.16b,v28.16b,#64-39
.inst	0xce9dba8e	//xar v14.16b,v20.16b,v29.16b,#64-18

.inst	0xce9f0854	//xar v20.16b,v2.16b,v31.16b,#64-62

.inst	0xce9f5582	//xar v2.16b,v12.16b,v31.16b,#64-43
.inst	0xce9b9dac	//xar v12.16b,v13.16b,v27.16b,#64-25
.inst	0xce9ce26d	//xar v13.16b,v19.16b,v28.16b,#64-8
.inst	0xce9b22f3	//xar v19.16b,v23.16b,v27.16b,#64-56
.inst	0xce9d5df7	//xar v23.16b,v15.16b,v29.16b,#64-41

.inst	0xce9c948f	//xar v15.16b,v4.16b,v28.16b,#64-27

	eor	v0.16b,v0.16b,v29.16b
	ldr	x11,[x10],#8

.inst	0xce9bae5a	//xar v26.16b,   v18.16b,v27.16b,#64-21	// C[1]=A[0][3]
.inst	0xce9fc632	//xar v18.16b,v17.16b,v31.16b,#64-15
.inst	0xce9ed971	//xar v17.16b,v11.16b,v30.16b,#64-10
.inst	0xce9fe8eb	//xar v11.16b,v7.16b,v31.16b,#64-6
.inst	0xce9df547	//xar v7.16b,v10.16b,v29.16b,#64-3

.inst	0xce9efc2a	//xar v10.16b,v1.16b,v30.16b,#64-1	// *

.inst	0xce9ccb04	//xar v4.16b,v24.16b,v28.16b,#64-14
.inst	0xce9efab8	//xar v24.16b,v21.16b,v30.16b,#64-2
.inst	0xce9b2515	//xar v21.16b,v8.16b,v27.16b,#64-55
.inst	0xce9e4e08	//xar v8.16b,v16.16b,v30.16b,#64-45
.inst	0xce9d70b0	//xar v16.16b,v5.16b,v29.16b,#64-36

.inst	0xce9b907b	//xar v27.16b,   v3.16b,v27.16b,#64-28	// C[2]=A[1][0]

	////////////////////////////////////////////////// Chi+Iota
	dup	v31.2d,x11				// borrow C[6]
.inst	0xce22641c	//bcax v28.16b,   v0.16b,v2.16b,v25.16b	// *
.inst	0xce3a0b21	//bcax v1.16b,v25.16b,   v26.16b,   v2.16b	// *
.inst	0xce246842	//bcax v2.16b,v2.16b,v4.16b,v26.16b
.inst	0xce201343	//bcax v3.16b,v26.16b,   v0.16b,v4.16b
.inst	0xce390084	//bcax v4.16b,v4.16b,v25.16b,   v0.16b

.inst	0xce271b65	//bcax v5.16b,v27.16b,   v7.16b,v6.16b	// *
.inst	0xce281cd9	//bcax v25.16b,   v6.16b,v8.16b,v7.16b	// *
.inst	0xce2920e7	//bcax v7.16b,v7.16b,v9.16b,v8.16b
.inst	0xce3b2508	//bcax v8.16b,v8.16b,v27.16b,   v9.16b
.inst	0xce266d29	//bcax v9.16b,v9.16b,v6.16b,v27.16b

	eor	v0.16b,v28.16b,v31.16b			// Iota

.inst	0xce2c2d5a	//bcax v26.16b,   v10.16b,v12.16b,v11.16b	// *
.inst	0xce2d317b	//bcax v27.16b,   v11.16b,v13.16b,v12.16b	// *
.inst	0xce2e358c	//bcax v12.16b,v12.16b,v14.16b,v13.16b
.inst	0xce2a39ad	//bcax v13.16b,v13.16b,v10.16b,v14.16b
.inst	0xce2b29ce	//bcax v14.16b,v14.16b,v11.16b,v10.16b

.inst	0xce3141fc	//bcax v28.16b,   v15.16b,v17.16b,v16.16b	// *
.inst	0xce32461d	//bcax v29.16b,   v16.16b,v18.16b,v17.16b	// *
.inst	0xce334a31	//bcax v17.16b,v17.16b,v19.16b,v18.16b
.inst	0xce2f4e52	//bcax v18.16b,v18.16b,v15.16b,v19.16b
.inst	0xce303e73	//bcax v19.16b,v19.16b,v16.16b,v15.16b

.inst	0xce36569e	//bcax v30.16b,   v20.16b,v22.16b,v21.16b	// *
.inst	0xce375abf	//bcax v31.16b,   v21.16b,v23.16b,v22.16b	// *
.inst	0xce385ed6	//bcax v22.16b,v22.16b,v24.16b,v23.16b
.inst	0xce3462f7	//bcax v23.16b,v23.16b,v20.16b,v24.16b
.inst	0xce355318	//bcax v24.16b,v24.16b,v21.16b,v20.16b
	////////////////////////////////////////////////// Theta
.inst	0xce056806	//eor3 v6.16b,v0.16b,v5.16b,v26.16b
.inst	0xce196c2a	//eor3 v10.16b,v1.16b,v25.16b,v27.16b
.inst	0xce07304b	//eor3 v11.16b,v2.16b,v7.16b,v12.16b
.inst	0xce08346f	//eor3 v15.16b,v3.16b,v8.16b,v13.16b
.inst	0xce093890	//eor3 v16.16b,v4.16b,v9.16b,v14.16b
.inst	0xce1c78c6	//eor3 v6.16b,v6.16b,   v28.16b,v30.16b
.inst	0xce1d7d4a	//eor3 v10.16b,v10.16b,   v29.16b,v31.16b
.inst	0xce11596b	//eor3 v11.16b,v11.16b,   v17.16b,v22.16b
.inst	0xce125def	//eor3 v15.16b,v15.16b,   v18.16b,v23.16b
.inst	0xce136210	//eor3 v16.16b,v16.16b,   v19.16b,v24.16b

.inst	0xce6b8cd4	//rax1 v20.16b,v6.16b,v11.16b			// D[1]
.inst	0xce6f8d55	//rax1 v21.16b,v10.16b,v15.16b			// D[2]
.inst	0xce708d6b	//rax1 v11.16b,v11.16b,v16.16b			// D[3]
.inst	0xce668def	//rax1 v15.16b,v15.16b,v6.16b			// D[4]
.inst	0xce6a8e10	//rax1 v16.16b,v16.16b,v10.16b			// D[0]

	////////////////////////////////////////////////// Theta+Rho+Pi
.inst	0xce945326	//xar v6.16b,   v25.16b,v20.16b,#64-44	// C[0]=A[0][1]
.inst	0xce8fb139	//xar v25.16b,v9.16b,v15.16b,#64-20
.inst	0xce950ec9	//xar v9.16b,v22.16b,v21.16b,#64-61
.inst	0xce8f65d6	//xar v22.16b,v14.16b,v15.16b,#64-39
.inst	0xce90bbce	//xar v14.16b,v30.16b,v16.16b,#64-18

.inst	0xce95085e	//xar v30.16b,v2.16b,v21.16b,#64-62

.inst	0xce955582	//xar v2.16b,v12.16b,v21.16b,#64-43
.inst	0xce8b9dac	//xar v12.16b,v13.16b,v11.16b,#64-25
.inst	0xce8fe26d	//xar v13.16b,v19.16b,v15.16b,#64-8
.inst	0xce8b22f3	//xar v19.16b,v23.16b,v11.16b,#64-56
.inst	0xce905f97	//xar v23.16b,v28.16b,v16.16b,#64-41

.inst	0xce8f949c	//xar v28.16b,v4.16b,v15.16b,#64-27

	eor	v0.16b,v0.16b,v16.16b
	ldr	x11,[x10],#8

.inst	0xce8bae4a	//xar v10.16b,   v18.16b,v11.16b,#64-21	// C[1]=A[0][3]
.inst	0xce95c632	//xar v18.16b,v17.16b,v21.16b,#64-15
.inst	0xce94db71	//xar v17.16b,v27.16b,v20.16b,#64-10
.inst	0xce95e8fb	//xar v27.16b,v7.16b,v21.16b,#64-6
.inst	0xce90f747	//xar v7.16b,v26.16b,v16.16b,#64-3

.inst	0xce94fc3a	//xar v26.16b,v1.16b,v20.16b,#64-1	// *

.inst	0xce8fcb04	//xar v4.16b,v24.16b,v15.16b,#64-14
.inst	0xce94fbf8	//xar v24.16b,v31.16b,v20.16b,#64-2
.inst	0xce8b251f	//xar v31.16b,v8.16b,v11.16b,#64-55
.inst	0xce944fa8	//xar v8.16b,v29.16b,v20.16b,#64-45
.inst	0xce9070bd	//xar v29.16b,v5.16b,v16.16b,#64-36

.inst	0xce8b906b	//xar v11.16b,   v3.16b,v11.16b,#64-28	// C[2]=A[1][0]

	////////////////////////////////////////////////// Chi+Iota
	dup	v21.2d,x11				// borrow C[6]
.inst	0xce22180f	//bcax v15.16b,   v0.16b,v2.16b,v6.16b	// *
.inst	0xce2a08c1	//bcax v1.16b,v6.16b,   v10.16b,   v2.16b	// *
.inst	0xce242842	//bcax v2.16b,v2.16b,v4.16b,v10.16b
.inst	0xce201143	//bcax v3.16b,v10.16b,   v0.16b,v4.16b
.inst	0xce260084	//bcax v4.16b,v4.16b,v6.16b,   v0.16b

.inst	0xce276565	//bcax v5.16b,v11.16b,   v7.16b,v25.16b	// *
.inst	0xce281f26	//bcax v6.16b,   v25.16b,v8.16b,v7.16b	// *
.inst	0xce2920e7	//bcax v7.16b,v7.16b,v9.16b,v8.16b
.inst	0xce2b2508	//bcax v8.16b,v8.16b,v11.16b,   v9.16b
.inst	0xce392d29	//bcax v9.16b,v9.16b,v25.16b,v11.16b

	eor	v0.16b,v15.16b,v21.16b			// Iota

.inst	0xce2c6f4a	//bcax v10.16b,   v26.16b,v12.16b,v27.16b	// *
.inst	0xce2d336b	//bcax v11.16b,   v27.16b,v13.16b,v12.16b	// *
.inst	0xce2e358c	//bcax v12.16b,v12.16b,v14.16b,v13.16b
.inst	0xce3a39ad	//bcax v13.16b,v13.16b,v26.16b,v14.16b
.inst	0xce3b69ce	//bcax v14.16b,v14.16b,v27.16b,v26.16b

.inst	0xce31778f	//bcax v15.16b,   v28.16b,v17.16b,v29.16b	// *
.inst	0xce3247b0	//bcax v16.16b,   v29.16b,v18.16b,v17.16b	// *
.inst	0xce334a31	//bcax v17.16b,v17.16b,v19.16b,v18.16b
.inst	0xce3c4e52	//bcax v18.16b,v18.16b,v28.16b,v19.16b
.inst	0xce3d7273	//bcax v19.16b,v19.16b,v29.16b,v28.16b

.inst	0xce367fd4	//bcax v20.16b,   v30.16b,v22.16b,v31.16b	// *
.inst	0xce375bf5	//bcax v21.16b,   v31.16b,v23.16b,v22.16b	// *
.inst	0xce385ed6	//bcax v22.16b,v22.16b,v24.16b,v23.16b
.inst	0xce3e62f7	//bcax v23.16b,v23.16b,v30.16b,v24.16b
.inst	0xce3f7b18	//bcax v24.16b,v24.16b,v31.16b,v30.16b
	subs	x9,x9,#1
	bne	.Loop_ce

	ret
.size	KeccakF1600_ce,.-KeccakF1600_ce

.type	KeccakF1600_cext,%function
.align	5
KeccakF1600_cext:
.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-80]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#16]		// per ABI requirement
	stp	d10,d11,[sp,#32]
	stp	d12,d13,[sp,#48]
	stp	d14,d15,[sp,#64]
	ldp	d0,d1,[x0,#8*0]
	ldp	d2,d3,[x0,#8*2]
	ldp	d4,d5,[x0,#8*4]
	ldp	d6,d7,[x0,#8*6]
	ldp	d8,d9,[x0,#8*8]
	ldp	d10,d11,[x0,#8*10]
	ldp	d12,d13,[x0,#8*12]
	ldp	d14,d15,[x0,#8*14]
	ldp	d16,d17,[x0,#8*16]
	ldp	d18,d19,[x0,#8*18]
	ldp	d20,d21,[x0,#8*20]
	ldp	d22,d23,[x0,#8*22]
	ldr	d24,[x0,#8*24]
	bl	KeccakF1600_ce
	ldr	x30,[sp,#8]
	stp	d0,d1,[x0,#8*0]
	stp	d2,d3,[x0,#8*2]
	stp	d4,d5,[x0,#8*4]
	stp	d6,d7,[x0,#8*6]
	stp	d8,d9,[x0,#8*8]
	stp	d10,d11,[x0,#8*10]
	stp	d12,d13,[x0,#8*12]
	stp	d14,d15,[x0,#8*14]
	stp	d16,d17,[x0,#8*16]
	stp	d18,d19,[x0,#8*18]
	stp	d20,d21,[x0,#8*20]
	stp	d22,d23,[x0,#8*22]
	str	d24,[x0,#8*24]

	ldp	d8,d9,[sp,#16]
	ldp	d10,d11,[sp,#32]
	ldp	d12,d13,[sp,#48]
	ldp	d14,d15,[sp,#64]
	ldr	x29,[sp],#80
.inst	0xd50323bf		// autiasp
	ret
.size	KeccakF1600_cext,.-KeccakF1600_cext
.globl	SHA3_absorb_cext
.type	SHA3_absorb_cext,%function
.align	5
SHA3_absorb_cext:
.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-80]!
	add	x29,sp,#0
	stp	d8,d9,[sp,#16]		// per ABI requirement
	stp	d10,d11,[sp,#32]
	stp	d12,d13,[sp,#48]
	stp	d14,d15,[sp,#64]
	ldp	d0,d1,[x0,#8*0]
	ldp	d2,d3,[x0,#8*2]
	ldp	d4,d5,[x0,#8*4]
	ldp	d6,d7,[x0,#8*6]
	ldp	d8,d9,[x0,#8*8]
	ldp	d10,d11,[x0,#8*10]
	ldp	d12,d13,[x0,#8*12]
	ldp	d14,d15,[x0,#8*14]
	ldp	d16,d17,[x0,#8*16]
	ldp	d18,d19,[x0,#8*18]
	ldp	d20,d21,[x0,#8*20]
	ldp	d22,d23,[x0,#8*22]
	ldr	d24,[x0,#8*24]
	b	.Loop_absorb_ce

.align	4
.Loop_absorb_ce:
	subs	x2,x2,x3		// len - bsz
	blo	.Labsorbed_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v0.16b,v0.16b,v31.16b
	cmp	x3,#8*(0+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v1.16b,v1.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v2.16b,v2.16b,v31.16b
	cmp	x3,#8*(2+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v3.16b,v3.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v4.16b,v4.16b,v31.16b
	cmp	x3,#8*(4+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v5.16b,v5.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v6.16b,v6.16b,v31.16b
	cmp	x3,#8*(6+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v7.16b,v7.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v8.16b,v8.16b,v31.16b
	cmp	x3,#8*(8+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v9.16b,v9.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v10.16b,v10.16b,v31.16b
	cmp	x3,#8*(10+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v11.16b,v11.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v12.16b,v12.16b,v31.16b
	cmp	x3,#8*(12+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v13.16b,v13.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v14.16b,v14.16b,v31.16b
	cmp	x3,#8*(14+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v15.16b,v15.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v16.16b,v16.16b,v31.16b
	cmp	x3,#8*(16+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v17.16b,v17.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v18.16b,v18.16b,v31.16b
	cmp	x3,#8*(18+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v19.16b,v19.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v20.16b,v20.16b,v31.16b
	cmp	x3,#8*(20+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v21.16b,v21.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev64	v31.16b,v31.16b
#endif
	eor	v22.16b,v22.16b,v31.16b
	cmp	x3,#8*(22+2)
	blo	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v23.16b,v23.16b,v31.16b
	beq	.Lprocess_block_ce
	ldr	d31,[x1],#8		// *inp++
#ifdef	__AARCH64EB__
	rev	v31.16b,v31.16b
#endif
	eor	v24.16b,v24.16b,v31.16b

.Lprocess_block_ce:

	bl	KeccakF1600_ce

	b	.Loop_absorb_ce

.align	4
.Labsorbed_ce:
	stp	d0,d1,[x0,#8*0]
	stp	d2,d3,[x0,#8*2]
	stp	d4,d5,[x0,#8*4]
	stp	d6,d7,[x0,#8*6]
	stp	d8,d9,[x0,#8*8]
	stp	d10,d11,[x0,#8*10]
	stp	d12,d13,[x0,#8*12]
	stp	d14,d15,[x0,#8*14]
	stp	d16,d17,[x0,#8*16]
	stp	d18,d19,[x0,#8*18]
	stp	d20,d21,[x0,#8*20]
	stp	d22,d23,[x0,#8*22]
	str	d24,[x0,#8*24]
	add	x0,x2,x3		// return value

	ldp	d8,d9,[sp,#16]
	ldp	d10,d11,[sp,#32]
	ldp	d12,d13,[sp,#48]
	ldp	d14,d15,[sp,#64]
	ldp	x29,x30,[sp],#80
.inst	0xd50323bf		// autiasp
	ret
.size	SHA3_absorb_cext,.-SHA3_absorb_cext
.globl	SHA3_squeeze_cext
.type	SHA3_squeeze_cext,%function
.align	5
SHA3_squeeze_cext:
.inst	0xd503233f		// paciasp
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	mov	x9,x0
	mov	x10,x3

.Loop_squeeze_ce:
	ldr	x4,[x9],#8
	cmp	x2,#8
	blo	.Lsqueeze_tail_ce
#ifdef	__AARCH64EB__
	rev	x4,x4
#endif
	str	x4,[x1],#8
	beq	.Lsqueeze_done_ce

	sub	x2,x2,#8
	subs	x10,x10,#8
	bhi	.Loop_squeeze_ce

	bl	KeccakF1600_cext
	ldr	x30,[sp,#8]
	mov	x9,x0
	mov	x10,x3
	b	.Loop_squeeze_ce

.align	4
.Lsqueeze_tail_ce:
	strb	w4,[x1],#1
	lsr	x4,x4,#8
	subs	x2,x2,#1
	beq	.Lsqueeze_done_ce
	strb	w4,[x1],#1
	lsr	x4,x4,#8
	subs	x2,x2,#1
	beq	.Lsqueeze_done_ce
	strb	w4,[x1],#1
	lsr	x4,x4,#8
	subs	x2,x2,#1
	beq	.Lsqueeze_done_ce
	strb	w4,[x1],#1
	lsr	x4,x4,#8
	subs	x2,x2,#1
	beq	.Lsqueeze_done_ce
	strb	w4,[x1],#1
	lsr	x4,x4,#8
	subs	x2,x2,#1
	beq	.Lsqueeze_done_ce
	strb	w4,[x1],#1
	lsr	x4,x4,#8
	subs	x2,x2,#1
	beq	.Lsqueeze_done_ce
	strb	w4,[x1],#1

.Lsqueeze_done_ce:
	ldr	x29,[sp],#16
.inst	0xd50323bf		// autiasp
	ret
.size	SHA3_squeeze_cext,.-SHA3_squeeze_cext
.byte	75,101,99,99,97,107,45,49,54,48,48,32,97,98,115,111,114,98,32,97,110,100,32,115,113,117,101,101,122,101,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2
