#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_
#include <linux/compiler.h>
#include <linux/threads.h>

/*
 * Determine the real variable name from the name visible in the
 * kernel sources.
 */
#define per_cpu_var(var) per_cpu__##var

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
 * Add a offset to a pointer but keep the pointer as is.
 *
 * Only S390 provides its own means of moving the pointer.
 */
#ifndef SHIFT_PERCPU_PTR
#define SHIFT_PERCPU_PTR(__p, __offset)	RELOC_HIDE((__p), (__offset))
#endif

/*
 * A percpu variable may point to a discarded regions. The following are
 * established ways to produce a usable pointer from the percpu variable
 * offset.
 */
#define per_cpu(var, cpu) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), per_cpu_offset(cpu)))
#define __get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), my_cpu_offset))
#define __raw_get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), __my_cpu_offset))


#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
extern void setup_per_cpu_areas(void);
#endif

#else /* ! SMP */

#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu_var(var)))
#define __get_cpu_var(var)			per_cpu_var(var)
#define __raw_get_cpu_var(var)			per_cpu_var(var)

#endif	/* SMP */

#ifndef PER_CPU_ATTRIBUTES
#define PER_CPU_ATTRIBUTES
#endif

#define DECLARE_PER_CPU(type, name) extern PER_CPU_ATTRIBUTES \
					__typeof__(type) per_cpu_var(name)

/*
 * Optional methods for optimized non-lvalue per-cpu variable access.
 *
 * @var can be a percpu variable or a field of it and its size should
 * equal char, int or long.  percpu_read() evaluates to a lvalue and
 * all others to void.
 *
 * These operations are guaranteed to be atomic w.r.t. preemption.
 * The generic versions use plain get/put_cpu_var().  Archs are
 * encouraged to implement single-instruction alternatives which don't
 * require preemption protection.
 */
#ifndef percpu_read
# define percpu_read(var)						\
  ({									\
	typeof(per_cpu_var(var)) __tmp_var__;				\
	__tmp_var__ = get_cpu_var(var);					\
	put_cpu_var(var);						\
	__tmp_var__;							\
  })
#endif

#define __percpu_generic_to_op(var, val, op)				\
do {									\
	get_cpu_var(var) op val;					\
	put_cpu_var(var);						\
} while (0)

#ifndef percpu_write
# define percpu_write(var, val)		__percpu_generic_to_op(var, (val), =)
#endif

#ifndef percpu_add
# define percpu_add(var, val)		__percpu_generic_to_op(var, (val), +=)
#endif

#ifndef percpu_sub
# define percpu_sub(var, val)		__percpu_generic_to_op(var, (val), -=)
#endif

#ifndef percpu_and
# define percpu_and(var, val)		__percpu_generic_to_op(var, (val), &=)
#endif

#ifndef percpu_or
# define percpu_or(var, val)		__percpu_generic_to_op(var, (val), |=)
#endif

#ifndef percpu_xor
# define percpu_xor(var, val)		__percpu_generic_to_op(var, (val), ^=)
#endif

#endif /* _ASM_GENERIC_PERCPU_H_ */
