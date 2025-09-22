/*	$OpenBSD: sync.S,v 1.3 2017/08/20 11:12:42 visa Exp $	*/

/*
 * Copyright (c) 2015 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The hardware can do 4-byte and 8-byte atomic operations directly through
 * the ll/sc and lld/scd instructions. 1-byte and 2-byte atomic operations have
 * to be emulated. The emulation uses 4-byte atomic updates where the change is
 * confined to the desired 1-byte or 2-byte subword.
 */

	.set noreorder

#define LEAF(n) \
	.align 3; \
	.globl n; \
	.ent n, 0; \
n:

#define END(n) \
	.end n

/* Convert offset in memory to offset in machine word. */
#ifdef __MIPSEB__
#define GET_WORD_OFFSET(amask, r)	xori r, r, amask
#else
#define GET_WORD_OFFSET(amask, r)	/* nothing */
#endif

#define SYNC_EMUL_INIT(amask, vmask) \
	andi	$t0, $a0, amask;	/* Get byte offset. */ \
	xor	$a0, $a0, $t0;		/* Align the address. */ \
	GET_WORD_OFFSET(amask, $t0); \
	sll	$t0, $t0, 3;		/* Multiply by 8 to get bit shift. */ \
	li	$t1, vmask; \
	sll	$t1, $t1, $t0;		/* Make positive mask. */ \
	nor	$t2, $t1, $0		/* Make negative mask. */

#define NO_NEG
#define NEG_v0	nor $v0, $v0, $0
#define NEG_v1	nor $v1, $v1, $0

#define NO_TRUNC
#define TRUNC_32(r, t) \
	lui	t, 0xffff; \
	ori	t, t, 0xffff; \
	and	r, r, t

/*
 * type __sync_fetch_and_<op>_<N>(type *ptr, type value)
 */

#define SYNC_FETCH_AND_OP(op, n, ll, sc, inst, neg) \
LEAF(__sync_fetch_and_##op##_##n); \
	sync; \
1:	##ll	$v0, ($a0); \
	##inst	$v1, $v0, $a1; \
	##neg; \
	##sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	j	$ra; \
	 nop; \
END(__sync_fetch_and_##op##_##n)

SYNC_FETCH_AND_OP(add, 8, lld, scd, daddu, NO_NEG)
SYNC_FETCH_AND_OP(sub, 8, lld, scd, dsubu, NO_NEG)
SYNC_FETCH_AND_OP(or, 8, lld, scd, or, NO_NEG)
SYNC_FETCH_AND_OP(and, 8, lld, scd, and, NO_NEG)
SYNC_FETCH_AND_OP(xor, 8, lld, scd, xor, NO_NEG)
SYNC_FETCH_AND_OP(nand, 8, lld, scd, and, NEG_v1)

SYNC_FETCH_AND_OP(add, 4, ll, sc, addu, NO_NEG)
SYNC_FETCH_AND_OP(sub, 4, ll, sc, subu, NO_NEG)
SYNC_FETCH_AND_OP(or, 4, ll, sc, or, NO_NEG)
SYNC_FETCH_AND_OP(and, 4, ll, sc, and, NO_NEG)
SYNC_FETCH_AND_OP(xor, 4, ll, sc, xor, NO_NEG)
SYNC_FETCH_AND_OP(nand, 4, ll, sc, and, NEG_v1)

#define SYNC_FETCH_AND_OP_EMUL(op, n, inst, neg, amask, vmask) \
LEAF(__sync_fetch_and_##op##_##n); \
	SYNC_EMUL_INIT(amask, vmask); \
	sll	$a1, $a1, $t0;		/* Align the parameter. */ \
	and	$a1, $a1, $t1;		/* Normalize the parameter. */ \
	sync; \
1:	ll	$v0, ($a0); \
	##inst	$v1, $v0, $a1; \
	##neg; \
	and	$v1, $v1, $t1;		/* Get the new bits. */ \
	and	$t3, $v0, $t2;		/* Get the old bits. */ \
	or	$v1, $v1, $t3;		/* Combine the result. */ \
	sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	and	$v0, $v0, $t1;		/* Get the old value. */ \
	j	$ra; \
	 srl	$v0, $v0, $t0;		/* Remove the shift. */ \
END(__sync_fetch_and_##op##_##n)

SYNC_FETCH_AND_OP_EMUL(add, 2, addu, NO_NEG, 2, 0xffff)
SYNC_FETCH_AND_OP_EMUL(sub, 2, subu, NO_NEG, 2, 0xffff)
SYNC_FETCH_AND_OP_EMUL(or, 2, or, NO_NEG, 2, 0xffff)
SYNC_FETCH_AND_OP_EMUL(and, 2, and, NO_NEG, 2, 0xffff)
SYNC_FETCH_AND_OP_EMUL(xor, 2, xor, NO_NEG, 2, 0xffff)
SYNC_FETCH_AND_OP_EMUL(nand, 2, and, NEG_v1, 2, 0xffff)

SYNC_FETCH_AND_OP_EMUL(add, 1, addu, NO_NEG, 3, 0xff)
SYNC_FETCH_AND_OP_EMUL(sub, 1, subu, NO_NEG, 3, 0xff)
SYNC_FETCH_AND_OP_EMUL(or, 1, or, NO_NEG, 3, 0xff)
SYNC_FETCH_AND_OP_EMUL(and, 1, and, NO_NEG, 3, 0xff)
SYNC_FETCH_AND_OP_EMUL(xor, 1, xor, NO_NEG, 3, 0xff)
SYNC_FETCH_AND_OP_EMUL(nand, 1, and, NEG_v1, 3, 0xff)

/*
 * type __sync_<op>_and_fetch_<N>(type *ptr, type value)
 */

#define SYNC_OP_AND_FETCH(op, n, ll, sc, inst, neg) \
LEAF(__sync_##op##_and_fetch_##n); \
	sync; \
1:	##ll	$v0, ($a0); \
	##inst	$v0, $v0, $a1; \
	##neg; \
	move	$v1, $v0; \
	##sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	j	$ra; \
	 nop; \
END(__sync_##op##_and_fetch_##n)

SYNC_OP_AND_FETCH(add, 8, lld, scd, daddu, NO_NEG)
SYNC_OP_AND_FETCH(sub, 8, lld, scd, dsubu, NO_NEG)
SYNC_OP_AND_FETCH(or, 8, lld, scd, or, NO_NEG)
SYNC_OP_AND_FETCH(and, 8, lld, scd, and, NO_NEG)
SYNC_OP_AND_FETCH(xor, 8, lld, scd, xor, NO_NEG)
SYNC_OP_AND_FETCH(nand, 8, lld, scd, and, NEG_v0)

SYNC_OP_AND_FETCH(add, 4, ll, sc, addu, NO_NEG)
SYNC_OP_AND_FETCH(sub, 4, ll, sc, subu, NO_NEG)
SYNC_OP_AND_FETCH(or, 4, ll, sc, or, NO_NEG)
SYNC_OP_AND_FETCH(and, 4, ll, sc, and, NO_NEG)
SYNC_OP_AND_FETCH(xor, 4, ll, sc, xor, NO_NEG)
SYNC_OP_AND_FETCH(nand, 4, ll, sc, and, NEG_v0)

#define SYNC_OP_AND_FETCH_EMUL(op, n, inst, neg, amask, vmask) \
LEAF(__sync_##op##_and_fetch_##n); \
	SYNC_EMUL_INIT(amask, vmask); \
	sll	$a1, $a1, $t0;		/* Align the parameter. */ \
	and	$a1, $a1, $t1;		/* Normalize the parameter. */ \
	sync; \
1:	ll	$v0, ($a0); \
	##inst	$v1, $v0, $a1; \
	##neg; \
	and	$t3, $v1, $t1;		/* Get the new bits. */ \
	and	$v0, $v0, $t2;		/* Get the old bits. */ \
	or	$v0, $v0, $t3;		/* Combine the result. */ \
	move	$v1, $v0; \
	sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	j	$ra; \
	 srl	$v0, $t3, $t0;		/* Remove the shift. */ \
END(__sync_##op##_and_fetch_##n)

SYNC_OP_AND_FETCH_EMUL(add, 2, addu, NO_NEG, 2, 0xffff)
SYNC_OP_AND_FETCH_EMUL(sub, 2, subu, NO_NEG, 2, 0xffff)
SYNC_OP_AND_FETCH_EMUL(or, 2, or, NO_NEG, 2, 0xffff)
SYNC_OP_AND_FETCH_EMUL(and, 2, and, NO_NEG, 2, 0xffff)
SYNC_OP_AND_FETCH_EMUL(xor, 2, xor, NO_NEG, 2, 0xffff)
SYNC_OP_AND_FETCH_EMUL(nand, 2, and, NEG_v1, 2, 0xffff)

SYNC_OP_AND_FETCH_EMUL(add, 1, addu, NO_NEG, 3, 0xff)
SYNC_OP_AND_FETCH_EMUL(sub, 1, subu, NO_NEG, 3, 0xff)
SYNC_OP_AND_FETCH_EMUL(or, 1, or, NO_NEG, 3, 0xff)
SYNC_OP_AND_FETCH_EMUL(and, 1, and, NO_NEG, 3, 0xff)
SYNC_OP_AND_FETCH_EMUL(xor, 1, xor, NO_NEG, 3, 0xff)
SYNC_OP_AND_FETCH_EMUL(nand, 1, and, NEG_v1, 3, 0xff)

/*
 * type __sync_bool_compare_and_swap_<N>(type *ptr, type oldv, type newv)
 */

#define SYNC_BOOL_COMPARE_AND_SWAP(n, ll, sc, trunc) \
LEAF(__sync_bool_compare_and_swap_##n); \
	trunc; \
	sync; \
1:	##ll	$v0, ($a0); \
	bne	$v0, $a1, 2f; \
	 move	$v1, $a2; \
	##sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	j	$ra; \
	 li	$v0, 1; \
2:	j	$ra; \
	 li	$v0, 0; \
END(__sync_bool_compare_and_swap_##n)

SYNC_BOOL_COMPARE_AND_SWAP(8, lld, scd, NO_TRUNC)
SYNC_BOOL_COMPARE_AND_SWAP(4, ll, sc, TRUNC_32($a1, $t0))

#define SYNC_BOOL_COMPARE_AND_SWAP_EMUL(n, amask, vmask) \
LEAF(__sync_bool_compare_and_swap_##n); \
	SYNC_EMUL_INIT(amask, vmask); \
	/* Align and normalize the parameters. */ \
	sll	$a1, $a1, $t0; \
	and	$a1, $a1, $t1; \
	sll	$a2, $a2, $t0; \
	and	$a2, $a2, $t1; \
	/* Do the update. */ \
	sync; \
1:	ll	$v0, ($a0); \
	and	$v1, $v0, $t1;		/* Get the old value. */ \
	bne	$v1, $a1, 2f; \
	 and	$v1, $v0, $t2;		/* Clear the old value. */ \
	or	$v1, $v1, $a2;		/* Insert the new value. */ \
	sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	j	$ra; \
	 li	$v0, 1; \
2:	j	$ra; \
	 li	$v0, 0; \
END(__sync_bool_compare_and_swap_##n)

SYNC_BOOL_COMPARE_AND_SWAP_EMUL(2, 2, 0xffff)
SYNC_BOOL_COMPARE_AND_SWAP_EMUL(1, 3, 0xff)

/*
 * type __sync_val_compare_and_swap_<N>(type *ptr, type oldv, type newv)
 */

#define SYNC_VAL_COMPARE_AND_SWAP(n, ll, sc, trunc) \
LEAF(__sync_val_compare_and_swap_##n); \
	trunc; \
	sync; \
1:	##ll	$v0, ($a0); \
	bne	$v0, $a1, 2f; \
	 move	$v1, $a2; \
	##sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
2:	j	$ra; \
	 nop; \
END(__sync_val_compare_and_swap_##n)

SYNC_VAL_COMPARE_AND_SWAP(8, lld, scd, NO_TRUNC)
SYNC_VAL_COMPARE_AND_SWAP(4, ll, sc, TRUNC_32($a1, $t0))

#define SYNC_VAL_COMPARE_AND_SWAP_EMUL(n, amask, vmask) \
LEAF(__sync_val_compare_and_swap_##n); \
	SYNC_EMUL_INIT(amask, vmask); \
	/* Align and normalize the parameters. */ \
	sll	$a1, $a1, $t0; \
	and	$a1, $a1, $t1; \
	sll	$a2, $a2, $t0; \
	and	$a2, $a2, $t1; \
	/* Do the update. */ \
	sync; \
1:	ll	$v0, ($a0); \
	and	$t3, $v0, $t1;		/* Get the old value. */ \
	bne	$t3, $a1, 2f; \
	 and	$v1, $v0, $t2;		/* Clear the old value. */ \
	or	$v1, $v1, $a2;		/* Insert the new value. */ \
	sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
2:	j	$ra; \
	 srl	$v0, $t3, $t0;		/* Remove the shift. */ \
END(__sync_val_compare_and_swap_##n)

SYNC_VAL_COMPARE_AND_SWAP_EMUL(2, 2, 0xffff)
SYNC_VAL_COMPARE_AND_SWAP_EMUL(1, 3, 0xff)

/*
 * type __sync_lock_test_and_set_<N>(type *ptr, type value)
 */

#define SYNC_LOCK_TEST_AND_SET(n, ll, sc) \
LEAF(__sync_lock_test_and_set_##n); \
	sync; \
1:	move	$v1, $a1; \
	##ll	$v0, ($a0); \
	##sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	j	$ra; \
	 nop; \
END(__sync_lock_test_and_set_##n)

SYNC_LOCK_TEST_AND_SET(8, lld, scd)
SYNC_LOCK_TEST_AND_SET(4, ll, sc)

#define SYNC_LOCK_TEST_AND_SET_EMUL(n, amask, vmask) \
LEAF(__sync_lock_test_and_set_##n); \
	SYNC_EMUL_INIT(amask, vmask); \
	sll	$a1, $a1, $t0;		/* Align the parameter. */ \
	and	$a1, $a1, $t1;		/* Normalize the parameter. */ \
	sync; \
1:	ll	$v0, ($a0); \
	and	$v1, $v0, $t2;		/* Clear the old value. */ \
	or	$v1, $v1, $a1;		/* Insert the new value. */ \
	sc	$v1, ($a0); \
	beq	$v1, $0, 1b; \
	 nop; \
	sync; \
	and	$v0, $v0, $t1;		/* Get the old value. */ \
	j	$ra; \
	 srl	$v0, $v0, $t0;		/* Remove the shift. */ \
END(__sync_lock_test_and_set_##n)

SYNC_LOCK_TEST_AND_SET_EMUL(2, 2, 0xffff)
SYNC_LOCK_TEST_AND_SET_EMUL(1, 3, 0xff)
