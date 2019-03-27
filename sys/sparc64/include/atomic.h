/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Doug Rabson.
 * Copyright (c) 2001 Jake Burkholder.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: FreeBSD: src/sys/i386/include/atomic.h,v 1.20 2001/02/11
 * $FreeBSD$
 */

#ifndef	_MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#include <machine/cpufunc.h>

#define	mb()	__asm__ __volatile__ ("membar #MemIssue": : :"memory")
#define	wmb()	mb()
#define	rmb()	mb()

#include <sys/atomic_common.h>

/* Userland needs different ASI's. */
#ifdef _KERNEL
#define	__ASI_ATOMIC	ASI_N
#else
#define	__ASI_ATOMIC	ASI_P
#endif

/*
 * Various simple arithmetic on memory which is atomic in the presence
 * of interrupts and multiple processors.  See atomic(9) for details.
 * Note that efficient hardware support exists only for the 32 and 64
 * bit variants; the 8 and 16 bit versions are not provided and should
 * not be used in MI code.
 *
 * This implementation takes advantage of the fact that the sparc64
 * cas instruction is both a load and a store.  The loop is often coded
 * as follows:
 *
 *	do {
 *		expect = *p;
 *		new = expect + 1;
 *	} while (cas(p, expect, new) != expect);
 *
 * which performs an unnnecessary load on each iteration that the cas
 * operation fails.  Modified as follows:
 *
 *	expect = *p;
 *	for (;;) {
 *		new = expect + 1;
 *		result = cas(p, expect, new);
 *		if (result == expect)
 *			break;
 *		expect = result;
 *	}
 *
 * the return value of cas is used to avoid the extra reload.
 *
 * We only include a memory barrier in the rel variants as in total store
 * order which we use for running the kernel and all of the userland atomic
 * loads and stores behave as if the were followed by a membar with a mask
 * of #LoadLoad | #LoadStore | #StoreStore.  In order to be also sufficient
 * for use of relaxed memory ordering, the atomic_cas() in the acq variants
 * additionally would have to be followed by a membar #LoadLoad | #LoadStore.
 * Due to the suggested assembly syntax of the membar operands containing a
 * # character, they cannot be used in macros.  The cmask and mmask bits thus
 * are hard coded in machine/cpufunc.h and used here through macros.
 * Hopefully the bit numbers won't change in the future.
 */

#define	itype(sz)	uint ## sz ## _t

#define	atomic_cas_32(p, e, s)	casa((p), (e), (s), __ASI_ATOMIC)
#define	atomic_cas_64(p, e, s)	casxa((p), (e), (s), __ASI_ATOMIC)

#define	atomic_cas(p, e, s, sz)						\
	atomic_cas_ ## sz((p), (e), (s))

#define	atomic_cas_acq(p, e, s, sz) ({					\
	itype(sz) v;							\
	v = atomic_cas((p), (e), (s), sz);				\
	__compiler_membar();						\
	v;								\
})

#define	atomic_cas_rel(p, e, s, sz) ({					\
	itype(sz) v;							\
	membar(LoadStore | StoreStore);					\
	v = atomic_cas((p), (e), (s), sz);				\
	v;								\
})

#define	atomic_op(p, op, v, sz) ({					\
	itype(sz) e, r, s;						\
	for (e = *(volatile itype(sz) *)(p);; e = r) {			\
		s = e op (v);						\
		r = atomic_cas_ ## sz((p), e, s);			\
		if (r == e)						\
			break;						\
	}								\
	e;								\
})

#define	atomic_op_acq(p, op, v, sz) ({					\
	itype(sz) t;							\
	t = atomic_op((p), op, (v), sz);				\
	__compiler_membar();						\
	t;								\
})

#define	atomic_op_rel(p, op, v, sz) ({					\
	itype(sz) t;							\
	membar(LoadStore | StoreStore);					\
	t = atomic_op((p), op, (v), sz);				\
	t;								\
})

#define	atomic_ld_acq(p, sz) ({						\
	itype(sz) v;							\
	v = atomic_cas((p), 0, 0, sz);					\
	__compiler_membar();						\
	v;								\
})

#define	atomic_ld_clear(p, sz) ({					\
	itype(sz) e, r;							\
	for (e = *(volatile itype(sz) *)(p);; e = r) {			\
		r = atomic_cas((p), e, 0, sz);				\
		if (r == e)						\
			break;						\
	}								\
	e;								\
})

#define	atomic_st(p, v, sz) ({						\
	itype(sz) e, r;							\
	for (e = *(volatile itype(sz) *)(p);; e = r) {			\
		r = atomic_cas((p), e, (v), sz);			\
		if (r == e)						\
			break;						\
	}								\
	e;								\
})

#define	atomic_st_acq(p, v, sz) do {					\
	atomic_st((p), (v), sz);					\
	__compiler_membar();						\
} while (0)

#define	atomic_st_rel(p, v, sz) do {					\
	membar(LoadStore | StoreStore);					\
	atomic_st((p), (v), sz);					\
} while (0)

#define	ATOMIC_GEN(name, ptype, vtype, atype, sz)			\
									\
static __inline vtype							\
atomic_add_ ## name(volatile ptype p, atype v)				\
{									\
	return ((vtype)atomic_op((p), +, (v), sz));			\
}									\
static __inline vtype							\
atomic_add_acq_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_acq((p), +, (v), sz));			\
}									\
static __inline vtype							\
atomic_add_rel_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_rel((p), +, (v), sz));			\
}									\
									\
static __inline vtype							\
atomic_clear_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op((p), &, ~(v), sz));			\
}									\
static __inline vtype							\
atomic_clear_acq_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_acq((p), &, ~(v), sz));		\
}									\
static __inline vtype							\
atomic_clear_rel_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_rel((p), &, ~(v), sz));		\
}									\
									\
static __inline int							\
atomic_cmpset_ ## name(volatile ptype p, vtype e, vtype s)		\
{									\
	return (((vtype)atomic_cas((p), (e), (s), sz)) == (e));		\
}									\
static __inline int							\
atomic_cmpset_acq_ ## name(volatile ptype p, vtype e, vtype s)		\
{									\
	return (((vtype)atomic_cas_acq((p), (e), (s), sz)) == (e));	\
}									\
static __inline int							\
atomic_cmpset_rel_ ## name(volatile ptype p, vtype e, vtype s)		\
{									\
	return (((vtype)atomic_cas_rel((p), (e), (s), sz)) == (e));	\
}									\
									\
static __inline int							\
atomic_fcmpset_ ## name(volatile ptype p, vtype *ep, vtype s)		\
{									\
	vtype t;							\
									\
	t = (vtype)atomic_cas((p), (*ep), (s), sz);			\
	if (t == (*ep))	 						\
		return (1);						\
	*ep = t;							\
	return (0);							\
}									\
static __inline int							\
atomic_fcmpset_acq_ ## name(volatile ptype p, vtype *ep, vtype s)	\
{									\
	vtype t;							\
									\
	t = (vtype)atomic_cas_acq((p), (*ep), (s), sz);			\
	if (t == (*ep))	 						\
		return (1);						\
	*ep = t;							\
	return (0);							\
}									\
static __inline int							\
atomic_fcmpset_rel_ ## name(volatile ptype p, vtype *ep, vtype s)	\
{									\
	vtype t;							\
									\
	t = (vtype)atomic_cas_rel((p), (*ep), (s), sz);			\
	if (t == (*ep))	 						\
		return (1);						\
	*ep = t;							\
	return (0);							\
}									\
									\
static __inline vtype							\
atomic_load_acq_ ## name(volatile ptype p)				\
{									\
	return ((vtype)atomic_cas_acq((p), 0, 0, sz));			\
}									\
									\
static __inline vtype							\
atomic_readandclear_ ## name(volatile ptype p)				\
{									\
	return ((vtype)atomic_ld_clear((p), sz));			\
}									\
									\
static __inline vtype							\
atomic_set_ ## name(volatile ptype p, atype v)				\
{									\
	return ((vtype)atomic_op((p), |, (v), sz));			\
}									\
static __inline vtype							\
atomic_set_acq_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_acq((p), |, (v), sz));			\
}									\
static __inline vtype							\
atomic_set_rel_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_rel((p), |, (v), sz));			\
}									\
									\
static __inline vtype							\
atomic_subtract_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op((p), -, (v), sz));			\
}									\
static __inline vtype							\
atomic_subtract_acq_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_acq((p), -, (v), sz));			\
}									\
static __inline vtype							\
atomic_subtract_rel_ ## name(volatile ptype p, atype v)			\
{									\
	return ((vtype)atomic_op_rel((p), -, (v), sz));			\
}									\
									\
static __inline void							\
atomic_store_acq_ ## name(volatile ptype p, vtype v)			\
{									\
	atomic_st_acq((p), (v), sz);					\
}									\
static __inline void							\
atomic_store_rel_ ## name(volatile ptype p, vtype v)			\
{									\
	atomic_st_rel((p), (v), sz);					\
}									\
									\
static __inline vtype							\
atomic_swap_ ## name(volatile ptype p, vtype v)				\
{									\
	return ((vtype)atomic_st((p), (v), sz));			\
}

static __inline void
atomic_thread_fence_acq(void)
{

	__compiler_membar();
}

static __inline void
atomic_thread_fence_rel(void)
{

	__compiler_membar();
}

static __inline void
atomic_thread_fence_acq_rel(void)
{

	__compiler_membar();
}

static __inline void
atomic_thread_fence_seq_cst(void)
{

	membar(LoadLoad | LoadStore | StoreStore | StoreLoad);
}


ATOMIC_GEN(int, u_int *, u_int, u_int, 32);
ATOMIC_GEN(32, uint32_t *, uint32_t, uint32_t, 32);

ATOMIC_GEN(long, u_long *, u_long, u_long, 64);
ATOMIC_GEN(64, uint64_t *, uint64_t, uint64_t, 64);

ATOMIC_GEN(ptr, uintptr_t *, uintptr_t, uintptr_t, 64);

#define	atomic_fetchadd_int	atomic_add_int
#define	atomic_fetchadd_32	atomic_add_32
#define	atomic_fetchadd_long	atomic_add_long
#define	atomic_fetchadd_64	atomic_add_64

#undef ATOMIC_GEN
#undef atomic_cas
#undef atomic_cas_acq
#undef atomic_cas_rel
#undef atomic_op
#undef atomic_op_acq
#undef atomic_op_rel
#undef atomic_ld_acq
#undef atomic_ld_clear
#undef atomic_st
#undef atomic_st_acq
#undef atomic_st_rel

#endif /* !_MACHINE_ATOMIC_H_ */
