/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_

#include <linux/compiler.h>
#include <linux/threads.h>
#include <linux/percpu-defs.h>

#ifdef CONFIG_SMP

/*
 * per_cpu_offset() is the offset that has to be added to a
 * percpu variable to get to the instance for a certain processor.
 *
 * Most arches use the __per_cpu_offset array for those offsets but
 * some arches have their own ways of determining the offset (x86_64, s390).
 */
#ifndef __per_cpu_offset
extern unsigned long __per_cpu_offset[NR_CPUS];

#define per_cpu_offset(x) (__per_cpu_offset[x])
#endif

/*
 * Determine the offset for the currently active processor.
 * An arch may define __my_cpu_offset to provide a more effective
 * means of obtaining the offset to the per cpu variables of the
 * current processor.
 */
#ifndef __my_cpu_offset
#define __my_cpu_offset per_cpu_offset(raw_smp_processor_id())
#endif
#ifdef CONFIG_DEBUG_PREEMPT
#define my_cpu_offset per_cpu_offset(smp_processor_id())
#else
#define my_cpu_offset __my_cpu_offset
#endif

/*
 * Arch may define arch_raw_cpu_ptr() to provide more efficient address
 * translations for raw_cpu_ptr().
 */
#ifndef arch_raw_cpu_ptr
#define arch_raw_cpu_ptr(ptr) SHIFT_PERCPU_PTR(ptr, __my_cpu_offset)
#endif

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
extern void setup_per_cpu_areas(void);
#endif

#endif	/* SMP */

#ifndef PER_CPU_BASE_SECTION
#ifdef CONFIG_SMP
#define PER_CPU_BASE_SECTION ".data..percpu"
#else
#define PER_CPU_BASE_SECTION ".data"
#endif
#endif

#ifndef PER_CPU_ATTRIBUTES
#define PER_CPU_ATTRIBUTES
#endif

#define raw_cpu_generic_read(pcp)					\
({									\
	*raw_cpu_ptr(&(pcp));						\
})

#define raw_cpu_generic_to_op(pcp, val, op)				\
do {									\
	*raw_cpu_ptr(&(pcp)) op val;					\
} while (0)

#define raw_cpu_generic_add_return(pcp, val)				\
({									\
	TYPEOF_UNQUAL(pcp) *__p = raw_cpu_ptr(&(pcp));			\
									\
	*__p += val;							\
	*__p;								\
})

#define raw_cpu_generic_xchg(pcp, nval)					\
({									\
	TYPEOF_UNQUAL(pcp) *__p = raw_cpu_ptr(&(pcp));			\
	TYPEOF_UNQUAL(pcp) __ret;					\
	__ret = *__p;							\
	*__p = nval;							\
	__ret;								\
})

#define __cpu_fallback_try_cmpxchg(pcp, ovalp, nval, _cmpxchg)		\
({									\
	TYPEOF_UNQUAL(pcp) __val, __old = *(ovalp);			\
	__val = _cmpxchg(pcp, __old, nval);				\
	if (__val != __old)						\
		*(ovalp) = __val;					\
	__val == __old;							\
})

#define raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)			\
({									\
	TYPEOF_UNQUAL(pcp) *__p = raw_cpu_ptr(&(pcp));			\
	TYPEOF_UNQUAL(pcp) __val = *__p, ___old = *(ovalp);		\
	bool __ret;							\
	if (__val == ___old) {						\
		*__p = nval;						\
		__ret = true;						\
	} else {							\
		*(ovalp) = __val;					\
		__ret = false;						\
	}								\
	__ret;								\
})

#define raw_cpu_generic_cmpxchg(pcp, oval, nval)			\
({									\
	TYPEOF_UNQUAL(pcp) __old = (oval);				\
	raw_cpu_generic_try_cmpxchg(pcp, &__old, nval);			\
	__old;								\
})

#define __this_cpu_generic_read_nopreempt(pcp)				\
({									\
	TYPEOF_UNQUAL(pcp) ___ret;					\
	preempt_disable_notrace();					\
	___ret = READ_ONCE(*raw_cpu_ptr(&(pcp)));			\
	preempt_enable_notrace();					\
	___ret;								\
})

#define __this_cpu_generic_read_noirq(pcp)				\
({									\
	TYPEOF_UNQUAL(pcp) ___ret;					\
	unsigned long ___flags;						\
	raw_local_irq_save(___flags);					\
	___ret = raw_cpu_generic_read(pcp);				\
	raw_local_irq_restore(___flags);				\
	___ret;								\
})

#define this_cpu_generic_read(pcp)					\
({									\
	TYPEOF_UNQUAL(pcp) __ret;					\
	if (__native_word(pcp))						\
		__ret = __this_cpu_generic_read_nopreempt(pcp);		\
	else								\
		__ret = __this_cpu_generic_read_noirq(pcp);		\
	__ret;								\
})

#define this_cpu_generic_to_op(pcp, val, op)				\
do {									\
	unsigned long __flags;						\
	raw_local_irq_save(__flags);					\
	raw_cpu_generic_to_op(pcp, val, op);				\
	raw_local_irq_restore(__flags);					\
} while (0)


#define this_cpu_generic_add_return(pcp, val)				\
({									\
	TYPEOF_UNQUAL(pcp) __ret;					\
	unsigned long __flags;						\
	raw_local_irq_save(__flags);					\
	__ret = raw_cpu_generic_add_return(pcp, val);			\
	raw_local_irq_restore(__flags);					\
	__ret;								\
})

#define this_cpu_generic_xchg(pcp, nval)				\
({									\
	TYPEOF_UNQUAL(pcp) __ret;					\
	unsigned long __flags;						\
	raw_local_irq_save(__flags);					\
	__ret = raw_cpu_generic_xchg(pcp, nval);			\
	raw_local_irq_restore(__flags);					\
	__ret;								\
})

#define this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)			\
({									\
	bool __ret;							\
	unsigned long __flags;						\
	raw_local_irq_save(__flags);					\
	__ret = raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval);		\
	raw_local_irq_restore(__flags);					\
	__ret;								\
})

#define this_cpu_generic_cmpxchg(pcp, oval, nval)			\
({									\
	TYPEOF_UNQUAL(pcp) __ret;					\
	unsigned long __flags;						\
	raw_local_irq_save(__flags);					\
	__ret = raw_cpu_generic_cmpxchg(pcp, oval, nval);		\
	raw_local_irq_restore(__flags);					\
	__ret;								\
})

#ifndef raw_cpu_read_1
#define raw_cpu_read_1(pcp)		raw_cpu_generic_read(pcp)
#endif
#ifndef raw_cpu_read_2
#define raw_cpu_read_2(pcp)		raw_cpu_generic_read(pcp)
#endif
#ifndef raw_cpu_read_4
#define raw_cpu_read_4(pcp)		raw_cpu_generic_read(pcp)
#endif
#ifndef raw_cpu_read_8
#define raw_cpu_read_8(pcp)		raw_cpu_generic_read(pcp)
#endif

#ifndef raw_cpu_write_1
#define raw_cpu_write_1(pcp, val)	raw_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef raw_cpu_write_2
#define raw_cpu_write_2(pcp, val)	raw_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef raw_cpu_write_4
#define raw_cpu_write_4(pcp, val)	raw_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef raw_cpu_write_8
#define raw_cpu_write_8(pcp, val)	raw_cpu_generic_to_op(pcp, val, =)
#endif

#ifndef raw_cpu_add_1
#define raw_cpu_add_1(pcp, val)		raw_cpu_generic_to_op(pcp, val, +=)
#endif
#ifndef raw_cpu_add_2
#define raw_cpu_add_2(pcp, val)		raw_cpu_generic_to_op(pcp, val, +=)
#endif
#ifndef raw_cpu_add_4
#define raw_cpu_add_4(pcp, val)		raw_cpu_generic_to_op(pcp, val, +=)
#endif
#ifndef raw_cpu_add_8
#define raw_cpu_add_8(pcp, val)		raw_cpu_generic_to_op(pcp, val, +=)
#endif

#ifndef raw_cpu_and_1
#define raw_cpu_and_1(pcp, val)		raw_cpu_generic_to_op(pcp, val, &=)
#endif
#ifndef raw_cpu_and_2
#define raw_cpu_and_2(pcp, val)		raw_cpu_generic_to_op(pcp, val, &=)
#endif
#ifndef raw_cpu_and_4
#define raw_cpu_and_4(pcp, val)		raw_cpu_generic_to_op(pcp, val, &=)
#endif
#ifndef raw_cpu_and_8
#define raw_cpu_and_8(pcp, val)		raw_cpu_generic_to_op(pcp, val, &=)
#endif

#ifndef raw_cpu_or_1
#define raw_cpu_or_1(pcp, val)		raw_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef raw_cpu_or_2
#define raw_cpu_or_2(pcp, val)		raw_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef raw_cpu_or_4
#define raw_cpu_or_4(pcp, val)		raw_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef raw_cpu_or_8
#define raw_cpu_or_8(pcp, val)		raw_cpu_generic_to_op(pcp, val, |=)
#endif

#ifndef raw_cpu_add_return_1
#define raw_cpu_add_return_1(pcp, val)	raw_cpu_generic_add_return(pcp, val)
#endif
#ifndef raw_cpu_add_return_2
#define raw_cpu_add_return_2(pcp, val)	raw_cpu_generic_add_return(pcp, val)
#endif
#ifndef raw_cpu_add_return_4
#define raw_cpu_add_return_4(pcp, val)	raw_cpu_generic_add_return(pcp, val)
#endif
#ifndef raw_cpu_add_return_8
#define raw_cpu_add_return_8(pcp, val)	raw_cpu_generic_add_return(pcp, val)
#endif

#ifndef raw_cpu_xchg_1
#define raw_cpu_xchg_1(pcp, nval)	raw_cpu_generic_xchg(pcp, nval)
#endif
#ifndef raw_cpu_xchg_2
#define raw_cpu_xchg_2(pcp, nval)	raw_cpu_generic_xchg(pcp, nval)
#endif
#ifndef raw_cpu_xchg_4
#define raw_cpu_xchg_4(pcp, nval)	raw_cpu_generic_xchg(pcp, nval)
#endif
#ifndef raw_cpu_xchg_8
#define raw_cpu_xchg_8(pcp, nval)	raw_cpu_generic_xchg(pcp, nval)
#endif

#ifndef raw_cpu_try_cmpxchg_1
#ifdef raw_cpu_cmpxchg_1
#define raw_cpu_try_cmpxchg_1(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, raw_cpu_cmpxchg_1)
#else
#define raw_cpu_try_cmpxchg_1(pcp, ovalp, nval) \
	raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef raw_cpu_try_cmpxchg_2
#ifdef raw_cpu_cmpxchg_2
#define raw_cpu_try_cmpxchg_2(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, raw_cpu_cmpxchg_2)
#else
#define raw_cpu_try_cmpxchg_2(pcp, ovalp, nval) \
	raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef raw_cpu_try_cmpxchg_4
#ifdef raw_cpu_cmpxchg_4
#define raw_cpu_try_cmpxchg_4(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, raw_cpu_cmpxchg_4)
#else
#define raw_cpu_try_cmpxchg_4(pcp, ovalp, nval) \
	raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef raw_cpu_try_cmpxchg_8
#ifdef raw_cpu_cmpxchg_8
#define raw_cpu_try_cmpxchg_8(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, raw_cpu_cmpxchg_8)
#else
#define raw_cpu_try_cmpxchg_8(pcp, ovalp, nval) \
	raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif

#ifndef raw_cpu_try_cmpxchg64
#ifdef raw_cpu_cmpxchg64
#define raw_cpu_try_cmpxchg64(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, raw_cpu_cmpxchg64)
#else
#define raw_cpu_try_cmpxchg64(pcp, ovalp, nval) \
	raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef raw_cpu_try_cmpxchg128
#ifdef raw_cpu_cmpxchg128
#define raw_cpu_try_cmpxchg128(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, raw_cpu_cmpxchg128)
#else
#define raw_cpu_try_cmpxchg128(pcp, ovalp, nval) \
	raw_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif

#ifndef raw_cpu_cmpxchg_1
#define raw_cpu_cmpxchg_1(pcp, oval, nval) \
	raw_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef raw_cpu_cmpxchg_2
#define raw_cpu_cmpxchg_2(pcp, oval, nval) \
	raw_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef raw_cpu_cmpxchg_4
#define raw_cpu_cmpxchg_4(pcp, oval, nval) \
	raw_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef raw_cpu_cmpxchg_8
#define raw_cpu_cmpxchg_8(pcp, oval, nval) \
	raw_cpu_generic_cmpxchg(pcp, oval, nval)
#endif

#ifndef raw_cpu_cmpxchg64
#define raw_cpu_cmpxchg64(pcp, oval, nval) \
	raw_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef raw_cpu_cmpxchg128
#define raw_cpu_cmpxchg128(pcp, oval, nval) \
	raw_cpu_generic_cmpxchg(pcp, oval, nval)
#endif

#ifndef this_cpu_read_1
#define this_cpu_read_1(pcp)		this_cpu_generic_read(pcp)
#endif
#ifndef this_cpu_read_2
#define this_cpu_read_2(pcp)		this_cpu_generic_read(pcp)
#endif
#ifndef this_cpu_read_4
#define this_cpu_read_4(pcp)		this_cpu_generic_read(pcp)
#endif
#ifndef this_cpu_read_8
#define this_cpu_read_8(pcp)		this_cpu_generic_read(pcp)
#endif

#ifndef this_cpu_write_1
#define this_cpu_write_1(pcp, val)	this_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef this_cpu_write_2
#define this_cpu_write_2(pcp, val)	this_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef this_cpu_write_4
#define this_cpu_write_4(pcp, val)	this_cpu_generic_to_op(pcp, val, =)
#endif
#ifndef this_cpu_write_8
#define this_cpu_write_8(pcp, val)	this_cpu_generic_to_op(pcp, val, =)
#endif

#ifndef this_cpu_add_1
#define this_cpu_add_1(pcp, val)	this_cpu_generic_to_op(pcp, val, +=)
#endif
#ifndef this_cpu_add_2
#define this_cpu_add_2(pcp, val)	this_cpu_generic_to_op(pcp, val, +=)
#endif
#ifndef this_cpu_add_4
#define this_cpu_add_4(pcp, val)	this_cpu_generic_to_op(pcp, val, +=)
#endif
#ifndef this_cpu_add_8
#define this_cpu_add_8(pcp, val)	this_cpu_generic_to_op(pcp, val, +=)
#endif

#ifndef this_cpu_and_1
#define this_cpu_and_1(pcp, val)	this_cpu_generic_to_op(pcp, val, &=)
#endif
#ifndef this_cpu_and_2
#define this_cpu_and_2(pcp, val)	this_cpu_generic_to_op(pcp, val, &=)
#endif
#ifndef this_cpu_and_4
#define this_cpu_and_4(pcp, val)	this_cpu_generic_to_op(pcp, val, &=)
#endif
#ifndef this_cpu_and_8
#define this_cpu_and_8(pcp, val)	this_cpu_generic_to_op(pcp, val, &=)
#endif

#ifndef this_cpu_or_1
#define this_cpu_or_1(pcp, val)		this_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef this_cpu_or_2
#define this_cpu_or_2(pcp, val)		this_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef this_cpu_or_4
#define this_cpu_or_4(pcp, val)		this_cpu_generic_to_op(pcp, val, |=)
#endif
#ifndef this_cpu_or_8
#define this_cpu_or_8(pcp, val)		this_cpu_generic_to_op(pcp, val, |=)
#endif

#ifndef this_cpu_add_return_1
#define this_cpu_add_return_1(pcp, val)	this_cpu_generic_add_return(pcp, val)
#endif
#ifndef this_cpu_add_return_2
#define this_cpu_add_return_2(pcp, val)	this_cpu_generic_add_return(pcp, val)
#endif
#ifndef this_cpu_add_return_4
#define this_cpu_add_return_4(pcp, val)	this_cpu_generic_add_return(pcp, val)
#endif
#ifndef this_cpu_add_return_8
#define this_cpu_add_return_8(pcp, val)	this_cpu_generic_add_return(pcp, val)
#endif

#ifndef this_cpu_xchg_1
#define this_cpu_xchg_1(pcp, nval)	this_cpu_generic_xchg(pcp, nval)
#endif
#ifndef this_cpu_xchg_2
#define this_cpu_xchg_2(pcp, nval)	this_cpu_generic_xchg(pcp, nval)
#endif
#ifndef this_cpu_xchg_4
#define this_cpu_xchg_4(pcp, nval)	this_cpu_generic_xchg(pcp, nval)
#endif
#ifndef this_cpu_xchg_8
#define this_cpu_xchg_8(pcp, nval)	this_cpu_generic_xchg(pcp, nval)
#endif

#ifndef this_cpu_try_cmpxchg_1
#ifdef this_cpu_cmpxchg_1
#define this_cpu_try_cmpxchg_1(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, this_cpu_cmpxchg_1)
#else
#define this_cpu_try_cmpxchg_1(pcp, ovalp, nval) \
	this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef this_cpu_try_cmpxchg_2
#ifdef this_cpu_cmpxchg_2
#define this_cpu_try_cmpxchg_2(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, this_cpu_cmpxchg_2)
#else
#define this_cpu_try_cmpxchg_2(pcp, ovalp, nval) \
	this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef this_cpu_try_cmpxchg_4
#ifdef this_cpu_cmpxchg_4
#define this_cpu_try_cmpxchg_4(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, this_cpu_cmpxchg_4)
#else
#define this_cpu_try_cmpxchg_4(pcp, ovalp, nval) \
	this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef this_cpu_try_cmpxchg_8
#ifdef this_cpu_cmpxchg_8
#define this_cpu_try_cmpxchg_8(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, this_cpu_cmpxchg_8)
#else
#define this_cpu_try_cmpxchg_8(pcp, ovalp, nval) \
	this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif

#ifndef this_cpu_try_cmpxchg64
#ifdef this_cpu_cmpxchg64
#define this_cpu_try_cmpxchg64(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, this_cpu_cmpxchg64)
#else
#define this_cpu_try_cmpxchg64(pcp, ovalp, nval) \
	this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif
#ifndef this_cpu_try_cmpxchg128
#ifdef this_cpu_cmpxchg128
#define this_cpu_try_cmpxchg128(pcp, ovalp, nval) \
	__cpu_fallback_try_cmpxchg(pcp, ovalp, nval, this_cpu_cmpxchg128)
#else
#define this_cpu_try_cmpxchg128(pcp, ovalp, nval) \
	this_cpu_generic_try_cmpxchg(pcp, ovalp, nval)
#endif
#endif

#ifndef this_cpu_cmpxchg_1
#define this_cpu_cmpxchg_1(pcp, oval, nval) \
	this_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef this_cpu_cmpxchg_2
#define this_cpu_cmpxchg_2(pcp, oval, nval) \
	this_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef this_cpu_cmpxchg_4
#define this_cpu_cmpxchg_4(pcp, oval, nval) \
	this_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef this_cpu_cmpxchg_8
#define this_cpu_cmpxchg_8(pcp, oval, nval) \
	this_cpu_generic_cmpxchg(pcp, oval, nval)
#endif

#ifndef this_cpu_cmpxchg64
#define this_cpu_cmpxchg64(pcp, oval, nval) \
	this_cpu_generic_cmpxchg(pcp, oval, nval)
#endif
#ifndef this_cpu_cmpxchg128
#define this_cpu_cmpxchg128(pcp, oval, nval) \
	this_cpu_generic_cmpxchg(pcp, oval, nval)
#endif

#endif /* _ASM_GENERIC_PERCPU_H_ */
