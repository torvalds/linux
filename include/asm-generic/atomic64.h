/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic implementation of 64-bit atomics using spinlocks,
 * useful on processors that don't have 64-bit atomic instructions.
 *
 * Copyright © 2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */
#ifndef _ASM_GENERIC_ATOMIC64_H
#define _ASM_GENERIC_ATOMIC64_H
#include <linux/types.h>

typedef struct {
	s64 counter;
} atomic64_t;

#define ATOMIC64_INIT(i)	{ (i) }

extern s64 atomic64_read(const atomic64_t *v);
extern void atomic64_set(atomic64_t *v, s64 i);

#define atomic64_set_release(v, i)	atomic64_set((v), (i))

#define ATOMIC64_OP(op)							\
extern void	 atomic64_##op(s64 a, atomic64_t *v);

#define ATOMIC64_OP_RETURN(op)						\
extern s64 atomic64_##op##_return(s64 a, atomic64_t *v);

#define ATOMIC64_FETCH_OP(op)						\
extern s64 atomic64_fetch_##op(s64 a, atomic64_t *v);

#define ATOMIC64_OPS(op)	ATOMIC64_OP(op) ATOMIC64_OP_RETURN(op) ATOMIC64_FETCH_OP(op)

ATOMIC64_OPS(add)
ATOMIC64_OPS(sub)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op)	ATOMIC64_OP(op) ATOMIC64_FETCH_OP(op)

ATOMIC64_OPS(and)
ATOMIC64_OPS(or)
ATOMIC64_OPS(xor)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

extern s64 atomic64_dec_if_positive(atomic64_t *v);
#define atomic64_dec_if_positive atomic64_dec_if_positive
extern s64 atomic64_cmpxchg(atomic64_t *v, s64 o, s64 n);
extern s64 atomic64_xchg(atomic64_t *v, s64 new);
extern s64 atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u);
#define atomic64_fetch_add_unless atomic64_fetch_add_unless

#endif  /*  _ASM_GENERIC_ATOMIC64_H  */
