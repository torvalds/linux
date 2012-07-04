#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/pfn.h>
#include <linux/init.h>

#include <asm/percpu.h>

/* enough to cover all DEFINE_PER_CPUs in modules */
#ifdef CONFIG_MODULES
#define PERCPU_MODULE_RESERVE		(8 << 10)
#else
#define PERCPU_MODULE_RESERVE		0
#endif

#ifndef PERCPU_ENOUGH_ROOM
#define PERCPU_ENOUGH_ROOM						\
	(ALIGN(__per_cpu_end - __per_cpu_start, SMP_CACHE_BYTES) +	\
	 PERCPU_MODULE_RESERVE)
#endif

/*
 * Must be an lvalue. Since @var must be a simple identifier,
 * we force a syntax error here if it isn't.
 */
#define get_cpu_var(var) (*({				\
	preempt_disable();				\
	&__get_cpu_var(var); }))

/*
 * The weird & is necessary because sparse considers (void)(var) to be
 * a direct dereference of percpu variable (var).
 */
#define put_cpu_var(var) do {				\
	(void)&(var);					\
	preempt_enable();				\
} while (0)

#define get_cpu_ptr(var) ({				\
	preempt_disable();				\
	this_cpu_ptr(var); })

#define put_cpu_ptr(var) do {				\
	(void)(var);					\
	preempt_enable();				\
} while (0)

/* minimum unit size, also is the maximum supported allocation size */
#define PCPU_MIN_UNIT_SIZE		PFN_ALIGN(32 << 10)

/*
 * Percpu allocator can serve percpu allocations before slab is
 * initialized which allows slab to depend on the percpu allocator.
 * The following two parameters decide how much resource to
 * preallocate for this.  Keep PERCPU_DYNAMIC_RESERVE equal to or
 * larger than PERCPU_DYNAMIC_EARLY_SIZE.
 */
#define PERCPU_DYNAMIC_EARLY_SLOTS	128
#define PERCPU_DYNAMIC_EARLY_SIZE	(12 << 10)

/*
 * PERCPU_DYNAMIC_RESERVE indicates the amount of free area to piggy
 * back on the first chunk for dynamic percpu allocation if arch is
 * manually allocating and mapping it for faster access (as a part of
 * large page mapping for example).
 *
 * The following values give between one and two pages of free space
 * after typical minimal boot (2-way SMP, single disk and NIC) with
 * both defconfig and a distro config on x86_64 and 32.  More
 * intelligent way to determine this would be nice.
 */
#if BITS_PER_LONG > 32
#define PERCPU_DYNAMIC_RESERVE		(20 << 10)
#else
#define PERCPU_DYNAMIC_RESERVE		(12 << 10)
#endif

extern void *pcpu_base_addr;
extern const unsigned long *pcpu_unit_offsets;

struct pcpu_group_info {
	int			nr_units;	/* aligned # of units */
	unsigned long		base_offset;	/* base address offset */
	unsigned int		*cpu_map;	/* unit->cpu map, empty
						 * entries contain NR_CPUS */
};

struct pcpu_alloc_info {
	size_t			static_size;
	size_t			reserved_size;
	size_t			dyn_size;
	size_t			unit_size;
	size_t			atom_size;
	size_t			alloc_size;
	size_t			__ai_size;	/* internal, don't use */
	int			nr_groups;	/* 0 if grouping unnecessary */
	struct pcpu_group_info	groups[];
};

enum pcpu_fc {
	PCPU_FC_AUTO,
	PCPU_FC_EMBED,
	PCPU_FC_PAGE,

	PCPU_FC_NR,
};
extern const char *pcpu_fc_names[PCPU_FC_NR];

extern enum pcpu_fc pcpu_chosen_fc;

typedef void * (*pcpu_fc_alloc_fn_t)(unsigned int cpu, size_t size,
				     size_t align);
typedef void (*pcpu_fc_free_fn_t)(void *ptr, size_t size);
typedef void (*pcpu_fc_populate_pte_fn_t)(unsigned long addr);
typedef int (pcpu_fc_cpu_distance_fn_t)(unsigned int from, unsigned int to);

extern struct pcpu_alloc_info * __init pcpu_alloc_alloc_info(int nr_groups,
							     int nr_units);
extern void __init pcpu_free_alloc_info(struct pcpu_alloc_info *ai);

extern int __init pcpu_setup_first_chunk(const struct pcpu_alloc_info *ai,
					 void *base_addr);

#ifdef CONFIG_NEED_PER_CPU_EMBED_FIRST_CHUNK
extern int __init pcpu_embed_first_chunk(size_t reserved_size, size_t dyn_size,
				size_t atom_size,
				pcpu_fc_cpu_distance_fn_t cpu_distance_fn,
				pcpu_fc_alloc_fn_t alloc_fn,
				pcpu_fc_free_fn_t free_fn);
#endif

#ifdef CONFIG_NEED_PER_CPU_PAGE_FIRST_CHUNK
extern int __init pcpu_page_first_chunk(size_t reserved_size,
				pcpu_fc_alloc_fn_t alloc_fn,
				pcpu_fc_free_fn_t free_fn,
				pcpu_fc_populate_pte_fn_t populate_pte_fn);
#endif

/*
 * Use this to get to a cpu's version of the per-cpu object
 * dynamically allocated. Non-atomic access to the current CPU's
 * version should probably be combined with get_cpu()/put_cpu().
 */
#ifdef CONFIG_SMP
#define per_cpu_ptr(ptr, cpu)	SHIFT_PERCPU_PTR((ptr), per_cpu_offset((cpu)))
#else
#define per_cpu_ptr(ptr, cpu)	({ (void)(cpu); VERIFY_PERCPU_PTR((ptr)); })
#endif

extern void __percpu *__alloc_reserved_percpu(size_t size, size_t align);
extern bool is_kernel_percpu_address(unsigned long addr);

#if !defined(CONFIG_SMP) || !defined(CONFIG_HAVE_SETUP_PER_CPU_AREA)
extern void __init setup_per_cpu_areas(void);
#endif
extern void __init percpu_init_late(void);

extern void __percpu *__alloc_percpu(size_t size, size_t align);
extern void free_percpu(void __percpu *__pdata);
extern phys_addr_t per_cpu_ptr_to_phys(void *addr);

#define alloc_percpu(type)	\
	(typeof(type) __percpu *)__alloc_percpu(sizeof(type), __alignof__(type))

/*
 * Branching function to split up a function into a set of functions that
 * are called for different scalar sizes of the objects handled.
 */

extern void __bad_size_call_parameter(void);

#define __pcpu_size_call_return(stem, variable)				\
({	typeof(variable) pscr_ret__;					\
	__verify_pcpu_ptr(&(variable));					\
	switch(sizeof(variable)) {					\
	case 1: pscr_ret__ = stem##1(variable);break;			\
	case 2: pscr_ret__ = stem##2(variable);break;			\
	case 4: pscr_ret__ = stem##4(variable);break;			\
	case 8: pscr_ret__ = stem##8(variable);break;			\
	default:							\
		__bad_size_call_parameter();break;			\
	}								\
	pscr_ret__;							\
})

#define __pcpu_size_call_return2(stem, variable, ...)			\
({									\
	typeof(variable) pscr2_ret__;					\
	__verify_pcpu_ptr(&(variable));					\
	switch(sizeof(variable)) {					\
	case 1: pscr2_ret__ = stem##1(variable, __VA_ARGS__); break;	\
	case 2: pscr2_ret__ = stem##2(variable, __VA_ARGS__); break;	\
	case 4: pscr2_ret__ = stem##4(variable, __VA_ARGS__); break;	\
	case 8: pscr2_ret__ = stem##8(variable, __VA_ARGS__); break;	\
	default:							\
		__bad_size_call_parameter(); break;			\
	}								\
	pscr2_ret__;							\
})

/*
 * Special handling for cmpxchg_double.  cmpxchg_double is passed two
 * percpu variables.  The first has to be aligned to a double word
 * boundary and the second has to follow directly thereafter.
 * We enforce this on all architectures even if they don't support
 * a double cmpxchg instruction, since it's a cheap requirement, and it
 * avoids breaking the requirement for architectures with the instruction.
 */
#define __pcpu_double_call_return_bool(stem, pcp1, pcp2, ...)		\
({									\
	bool pdcrb_ret__;						\
	__verify_pcpu_ptr(&pcp1);					\
	BUILD_BUG_ON(sizeof(pcp1) != sizeof(pcp2));			\
	VM_BUG_ON((unsigned long)(&pcp1) % (2 * sizeof(pcp1)));		\
	VM_BUG_ON((unsigned long)(&pcp2) !=				\
		  (unsigned long)(&pcp1) + sizeof(pcp1));		\
	switch(sizeof(pcp1)) {						\
	case 1: pdcrb_ret__ = stem##1(pcp1, pcp2, __VA_ARGS__); break;	\
	case 2: pdcrb_ret__ = stem##2(pcp1, pcp2, __VA_ARGS__); break;	\
	case 4: pdcrb_ret__ = stem##4(pcp1, pcp2, __VA_ARGS__); break;	\
	case 8: pdcrb_ret__ = stem##8(pcp1, pcp2, __VA_ARGS__); break;	\
	default:							\
		__bad_size_call_parameter(); break;			\
	}								\
	pdcrb_ret__;							\
})

#define __pcpu_size_call(stem, variable, ...)				\
do {									\
	__verify_pcpu_ptr(&(variable));					\
	switch(sizeof(variable)) {					\
		case 1: stem##1(variable, __VA_ARGS__);break;		\
		case 2: stem##2(variable, __VA_ARGS__);break;		\
		case 4: stem##4(variable, __VA_ARGS__);break;		\
		case 8: stem##8(variable, __VA_ARGS__);break;		\
		default: 						\
			__bad_size_call_parameter();break;		\
	}								\
} while (0)

/*
 * Optimized manipulation for memory allocated through the per cpu
 * allocator or for addresses of per cpu variables.
 *
 * These operation guarantee exclusivity of access for other operations
 * on the *same* processor. The assumption is that per cpu data is only
 * accessed by a single processor instance (the current one).
 *
 * The first group is used for accesses that must be done in a
 * preemption safe way since we know that the context is not preempt
 * safe. Interrupts may occur. If the interrupt modifies the variable
 * too then RMW actions will not be reliable.
 *
 * The arch code can provide optimized functions in two ways:
 *
 * 1. Override the function completely. F.e. define this_cpu_add().
 *    The arch must then ensure that the various scalar format passed
 *    are handled correctly.
 *
 * 2. Provide functions for certain scalar sizes. F.e. provide
 *    this_cpu_add_2() to provide per cpu atomic operations for 2 byte
 *    sized RMW actions. If arch code does not provide operations for
 *    a scalar size then the fallback in the generic code will be
 *    used.
 */

#define _this_cpu_generic_read(pcp)					\
({	typeof(pcp) ret__;						\
	preempt_disable();						\
	ret__ = *this_cpu_ptr(&(pcp));					\
	preempt_enable();						\
	ret__;								\
})

#ifndef this_cpu_read
# ifndef this_cpu_read_1
#  define this_cpu_read_1(pcp)	_this_cpu_generic_read(pcp)
# endif
# ifndef this_cpu_read_2
#  define this_cpu_read_2(pcp)	_this_cpu_generic_read(pcp)
# endif
# ifndef this_cpu_read_4
#  define this_cpu_read_4(pcp)	_this_cpu_generic_read(pcp)
# endif
# ifndef this_cpu_read_8
#  define this_cpu_read_8(pcp)	_this_cpu_generic_read(pcp)
# endif
# define this_cpu_read(pcp)	__pcpu_size_call_return(this_cpu_read_, (pcp))
#endif

#define _this_cpu_generic_to_op(pcp, val, op)				\
do {									\
	unsigned long flags;						\
	raw_local_irq_save(flags);					\
	*__this_cpu_ptr(&(pcp)) op val;					\
	raw_local_irq_restore(flags);					\
} while (0)

#ifndef this_cpu_write
# ifndef this_cpu_write_1
#  define this_cpu_write_1(pcp, val)	_this_cpu_generic_to_op((pcp), (val), =)
# endif
# ifndef this_cpu_write_2
#  define this_cpu_write_2(pcp, val)	_this_cpu_generic_to_op((pcp), (val), =)
# endif
# ifndef this_cpu_write_4
#  define this_cpu_write_4(pcp, val)	_this_cpu_generic_to_op((pcp), (val), =)
# endif
# ifndef this_cpu_write_8
#  define this_cpu_write_8(pcp, val)	_this_cpu_generic_to_op((pcp), (val), =)
# endif
# define this_cpu_write(pcp, val)	__pcpu_size_call(this_cpu_write_, (pcp), (val))
#endif

#ifndef this_cpu_add
# ifndef this_cpu_add_1
#  define this_cpu_add_1(pcp, val)	_this_cpu_generic_to_op((pcp), (val), +=)
# endif
# ifndef this_cpu_add_2
#  define this_cpu_add_2(pcp, val)	_this_cpu_generic_to_op((pcp), (val), +=)
# endif
# ifndef this_cpu_add_4
#  define this_cpu_add_4(pcp, val)	_this_cpu_generic_to_op((pcp), (val), +=)
# endif
# ifndef this_cpu_add_8
#  define this_cpu_add_8(pcp, val)	_this_cpu_generic_to_op((pcp), (val), +=)
# endif
# define this_cpu_add(pcp, val)		__pcpu_size_call(this_cpu_add_, (pcp), (val))
#endif

#ifndef this_cpu_sub
# define this_cpu_sub(pcp, val)		this_cpu_add((pcp), -(val))
#endif

#ifndef this_cpu_inc
# define this_cpu_inc(pcp)		this_cpu_add((pcp), 1)
#endif

#ifndef this_cpu_dec
# define this_cpu_dec(pcp)		this_cpu_sub((pcp), 1)
#endif

#ifndef this_cpu_and
# ifndef this_cpu_and_1
#  define this_cpu_and_1(pcp, val)	_this_cpu_generic_to_op((pcp), (val), &=)
# endif
# ifndef this_cpu_and_2
#  define this_cpu_and_2(pcp, val)	_this_cpu_generic_to_op((pcp), (val), &=)
# endif
# ifndef this_cpu_and_4
#  define this_cpu_and_4(pcp, val)	_this_cpu_generic_to_op((pcp), (val), &=)
# endif
# ifndef this_cpu_and_8
#  define this_cpu_and_8(pcp, val)	_this_cpu_generic_to_op((pcp), (val), &=)
# endif
# define this_cpu_and(pcp, val)		__pcpu_size_call(this_cpu_and_, (pcp), (val))
#endif

#ifndef this_cpu_or
# ifndef this_cpu_or_1
#  define this_cpu_or_1(pcp, val)	_this_cpu_generic_to_op((pcp), (val), |=)
# endif
# ifndef this_cpu_or_2
#  define this_cpu_or_2(pcp, val)	_this_cpu_generic_to_op((pcp), (val), |=)
# endif
# ifndef this_cpu_or_4
#  define this_cpu_or_4(pcp, val)	_this_cpu_generic_to_op((pcp), (val), |=)
# endif
# ifndef this_cpu_or_8
#  define this_cpu_or_8(pcp, val)	_this_cpu_generic_to_op((pcp), (val), |=)
# endif
# define this_cpu_or(pcp, val)		__pcpu_size_call(this_cpu_or_, (pcp), (val))
#endif

#ifndef this_cpu_xor
# ifndef this_cpu_xor_1
#  define this_cpu_xor_1(pcp, val)	_this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# ifndef this_cpu_xor_2
#  define this_cpu_xor_2(pcp, val)	_this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# ifndef this_cpu_xor_4
#  define this_cpu_xor_4(pcp, val)	_this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# ifndef this_cpu_xor_8
#  define this_cpu_xor_8(pcp, val)	_this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# define this_cpu_xor(pcp, val)		__pcpu_size_call(this_cpu_or_, (pcp), (val))
#endif

#define _this_cpu_generic_add_return(pcp, val)				\
({									\
	typeof(pcp) ret__;						\
	unsigned long flags;						\
	raw_local_irq_save(flags);					\
	__this_cpu_add(pcp, val);					\
	ret__ = __this_cpu_read(pcp);					\
	raw_local_irq_restore(flags);					\
	ret__;								\
})

#ifndef this_cpu_add_return
# ifndef this_cpu_add_return_1
#  define this_cpu_add_return_1(pcp, val)	_this_cpu_generic_add_return(pcp, val)
# endif
# ifndef this_cpu_add_return_2
#  define this_cpu_add_return_2(pcp, val)	_this_cpu_generic_add_return(pcp, val)
# endif
# ifndef this_cpu_add_return_4
#  define this_cpu_add_return_4(pcp, val)	_this_cpu_generic_add_return(pcp, val)
# endif
# ifndef this_cpu_add_return_8
#  define this_cpu_add_return_8(pcp, val)	_this_cpu_generic_add_return(pcp, val)
# endif
# define this_cpu_add_return(pcp, val)	__pcpu_size_call_return2(this_cpu_add_return_, pcp, val)
#endif

#define this_cpu_sub_return(pcp, val)	this_cpu_add_return(pcp, -(val))
#define this_cpu_inc_return(pcp)	this_cpu_add_return(pcp, 1)
#define this_cpu_dec_return(pcp)	this_cpu_add_return(pcp, -1)

#define _this_cpu_generic_xchg(pcp, nval)				\
({	typeof(pcp) ret__;						\
	unsigned long flags;						\
	raw_local_irq_save(flags);					\
	ret__ = __this_cpu_read(pcp);					\
	__this_cpu_write(pcp, nval);					\
	raw_local_irq_restore(flags);					\
	ret__;								\
})

#ifndef this_cpu_xchg
# ifndef this_cpu_xchg_1
#  define this_cpu_xchg_1(pcp, nval)	_this_cpu_generic_xchg(pcp, nval)
# endif
# ifndef this_cpu_xchg_2
#  define this_cpu_xchg_2(pcp, nval)	_this_cpu_generic_xchg(pcp, nval)
# endif
# ifndef this_cpu_xchg_4
#  define this_cpu_xchg_4(pcp, nval)	_this_cpu_generic_xchg(pcp, nval)
# endif
# ifndef this_cpu_xchg_8
#  define this_cpu_xchg_8(pcp, nval)	_this_cpu_generic_xchg(pcp, nval)
# endif
# define this_cpu_xchg(pcp, nval)	\
	__pcpu_size_call_return2(this_cpu_xchg_, (pcp), nval)
#endif

#define _this_cpu_generic_cmpxchg(pcp, oval, nval)			\
({									\
	typeof(pcp) ret__;						\
	unsigned long flags;						\
	raw_local_irq_save(flags);					\
	ret__ = __this_cpu_read(pcp);					\
	if (ret__ == (oval))						\
		__this_cpu_write(pcp, nval);				\
	raw_local_irq_restore(flags);					\
	ret__;								\
})

#ifndef this_cpu_cmpxchg
# ifndef this_cpu_cmpxchg_1
#  define this_cpu_cmpxchg_1(pcp, oval, nval)	_this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# ifndef this_cpu_cmpxchg_2
#  define this_cpu_cmpxchg_2(pcp, oval, nval)	_this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# ifndef this_cpu_cmpxchg_4
#  define this_cpu_cmpxchg_4(pcp, oval, nval)	_this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# ifndef this_cpu_cmpxchg_8
#  define this_cpu_cmpxchg_8(pcp, oval, nval)	_this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# define this_cpu_cmpxchg(pcp, oval, nval)	\
	__pcpu_size_call_return2(this_cpu_cmpxchg_, pcp, oval, nval)
#endif

/*
 * cmpxchg_double replaces two adjacent scalars at once.  The first
 * two parameters are per cpu variables which have to be of the same
 * size.  A truth value is returned to indicate success or failure
 * (since a double register result is difficult to handle).  There is
 * very limited hardware support for these operations, so only certain
 * sizes may work.
 */
#define _this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
({									\
	int ret__;							\
	unsigned long flags;						\
	raw_local_irq_save(flags);					\
	ret__ = __this_cpu_generic_cmpxchg_double(pcp1, pcp2,		\
			oval1, oval2, nval1, nval2);			\
	raw_local_irq_restore(flags);					\
	ret__;								\
})

#ifndef this_cpu_cmpxchg_double
# ifndef this_cpu_cmpxchg_double_1
#  define this_cpu_cmpxchg_double_1(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	_this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# ifndef this_cpu_cmpxchg_double_2
#  define this_cpu_cmpxchg_double_2(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	_this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# ifndef this_cpu_cmpxchg_double_4
#  define this_cpu_cmpxchg_double_4(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	_this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# ifndef this_cpu_cmpxchg_double_8
#  define this_cpu_cmpxchg_double_8(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	_this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# define this_cpu_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	__pcpu_double_call_return_bool(this_cpu_cmpxchg_double_, (pcp1), (pcp2), (oval1), (oval2), (nval1), (nval2))
#endif

/*
 * Generic percpu operations for context that are safe from preemption/interrupts.
 * Either we do not care about races or the caller has the
 * responsibility of handling preemption/interrupt issues. Arch code can still
 * override these instructions since the arch per cpu code may be more
 * efficient and may actually get race freeness for free (that is the
 * case for x86 for example).
 *
 * If there is no other protection through preempt disable and/or
 * disabling interupts then one of these RMW operations can show unexpected
 * behavior because the execution thread was rescheduled on another processor
 * or an interrupt occurred and the same percpu variable was modified from
 * the interrupt context.
 */
#ifndef __this_cpu_read
# ifndef __this_cpu_read_1
#  define __this_cpu_read_1(pcp)	(*__this_cpu_ptr(&(pcp)))
# endif
# ifndef __this_cpu_read_2
#  define __this_cpu_read_2(pcp)	(*__this_cpu_ptr(&(pcp)))
# endif
# ifndef __this_cpu_read_4
#  define __this_cpu_read_4(pcp)	(*__this_cpu_ptr(&(pcp)))
# endif
# ifndef __this_cpu_read_8
#  define __this_cpu_read_8(pcp)	(*__this_cpu_ptr(&(pcp)))
# endif
# define __this_cpu_read(pcp)	__pcpu_size_call_return(__this_cpu_read_, (pcp))
#endif

#define __this_cpu_generic_to_op(pcp, val, op)				\
do {									\
	*__this_cpu_ptr(&(pcp)) op val;					\
} while (0)

#ifndef __this_cpu_write
# ifndef __this_cpu_write_1
#  define __this_cpu_write_1(pcp, val)	__this_cpu_generic_to_op((pcp), (val), =)
# endif
# ifndef __this_cpu_write_2
#  define __this_cpu_write_2(pcp, val)	__this_cpu_generic_to_op((pcp), (val), =)
# endif
# ifndef __this_cpu_write_4
#  define __this_cpu_write_4(pcp, val)	__this_cpu_generic_to_op((pcp), (val), =)
# endif
# ifndef __this_cpu_write_8
#  define __this_cpu_write_8(pcp, val)	__this_cpu_generic_to_op((pcp), (val), =)
# endif
# define __this_cpu_write(pcp, val)	__pcpu_size_call(__this_cpu_write_, (pcp), (val))
#endif

#ifndef __this_cpu_add
# ifndef __this_cpu_add_1
#  define __this_cpu_add_1(pcp, val)	__this_cpu_generic_to_op((pcp), (val), +=)
# endif
# ifndef __this_cpu_add_2
#  define __this_cpu_add_2(pcp, val)	__this_cpu_generic_to_op((pcp), (val), +=)
# endif
# ifndef __this_cpu_add_4
#  define __this_cpu_add_4(pcp, val)	__this_cpu_generic_to_op((pcp), (val), +=)
# endif
# ifndef __this_cpu_add_8
#  define __this_cpu_add_8(pcp, val)	__this_cpu_generic_to_op((pcp), (val), +=)
# endif
# define __this_cpu_add(pcp, val)	__pcpu_size_call(__this_cpu_add_, (pcp), (val))
#endif

#ifndef __this_cpu_sub
# define __this_cpu_sub(pcp, val)	__this_cpu_add((pcp), -(val))
#endif

#ifndef __this_cpu_inc
# define __this_cpu_inc(pcp)		__this_cpu_add((pcp), 1)
#endif

#ifndef __this_cpu_dec
# define __this_cpu_dec(pcp)		__this_cpu_sub((pcp), 1)
#endif

#ifndef __this_cpu_and
# ifndef __this_cpu_and_1
#  define __this_cpu_and_1(pcp, val)	__this_cpu_generic_to_op((pcp), (val), &=)
# endif
# ifndef __this_cpu_and_2
#  define __this_cpu_and_2(pcp, val)	__this_cpu_generic_to_op((pcp), (val), &=)
# endif
# ifndef __this_cpu_and_4
#  define __this_cpu_and_4(pcp, val)	__this_cpu_generic_to_op((pcp), (val), &=)
# endif
# ifndef __this_cpu_and_8
#  define __this_cpu_and_8(pcp, val)	__this_cpu_generic_to_op((pcp), (val), &=)
# endif
# define __this_cpu_and(pcp, val)	__pcpu_size_call(__this_cpu_and_, (pcp), (val))
#endif

#ifndef __this_cpu_or
# ifndef __this_cpu_or_1
#  define __this_cpu_or_1(pcp, val)	__this_cpu_generic_to_op((pcp), (val), |=)
# endif
# ifndef __this_cpu_or_2
#  define __this_cpu_or_2(pcp, val)	__this_cpu_generic_to_op((pcp), (val), |=)
# endif
# ifndef __this_cpu_or_4
#  define __this_cpu_or_4(pcp, val)	__this_cpu_generic_to_op((pcp), (val), |=)
# endif
# ifndef __this_cpu_or_8
#  define __this_cpu_or_8(pcp, val)	__this_cpu_generic_to_op((pcp), (val), |=)
# endif
# define __this_cpu_or(pcp, val)	__pcpu_size_call(__this_cpu_or_, (pcp), (val))
#endif

#ifndef __this_cpu_xor
# ifndef __this_cpu_xor_1
#  define __this_cpu_xor_1(pcp, val)	__this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# ifndef __this_cpu_xor_2
#  define __this_cpu_xor_2(pcp, val)	__this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# ifndef __this_cpu_xor_4
#  define __this_cpu_xor_4(pcp, val)	__this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# ifndef __this_cpu_xor_8
#  define __this_cpu_xor_8(pcp, val)	__this_cpu_generic_to_op((pcp), (val), ^=)
# endif
# define __this_cpu_xor(pcp, val)	__pcpu_size_call(__this_cpu_xor_, (pcp), (val))
#endif

#define __this_cpu_generic_add_return(pcp, val)				\
({									\
	__this_cpu_add(pcp, val);					\
	__this_cpu_read(pcp);						\
})

#ifndef __this_cpu_add_return
# ifndef __this_cpu_add_return_1
#  define __this_cpu_add_return_1(pcp, val)	__this_cpu_generic_add_return(pcp, val)
# endif
# ifndef __this_cpu_add_return_2
#  define __this_cpu_add_return_2(pcp, val)	__this_cpu_generic_add_return(pcp, val)
# endif
# ifndef __this_cpu_add_return_4
#  define __this_cpu_add_return_4(pcp, val)	__this_cpu_generic_add_return(pcp, val)
# endif
# ifndef __this_cpu_add_return_8
#  define __this_cpu_add_return_8(pcp, val)	__this_cpu_generic_add_return(pcp, val)
# endif
# define __this_cpu_add_return(pcp, val)	\
	__pcpu_size_call_return2(__this_cpu_add_return_, pcp, val)
#endif

#define __this_cpu_sub_return(pcp, val)	__this_cpu_add_return(pcp, -(val))
#define __this_cpu_inc_return(pcp)	__this_cpu_add_return(pcp, 1)
#define __this_cpu_dec_return(pcp)	__this_cpu_add_return(pcp, -1)

#define __this_cpu_generic_xchg(pcp, nval)				\
({	typeof(pcp) ret__;						\
	ret__ = __this_cpu_read(pcp);					\
	__this_cpu_write(pcp, nval);					\
	ret__;								\
})

#ifndef __this_cpu_xchg
# ifndef __this_cpu_xchg_1
#  define __this_cpu_xchg_1(pcp, nval)	__this_cpu_generic_xchg(pcp, nval)
# endif
# ifndef __this_cpu_xchg_2
#  define __this_cpu_xchg_2(pcp, nval)	__this_cpu_generic_xchg(pcp, nval)
# endif
# ifndef __this_cpu_xchg_4
#  define __this_cpu_xchg_4(pcp, nval)	__this_cpu_generic_xchg(pcp, nval)
# endif
# ifndef __this_cpu_xchg_8
#  define __this_cpu_xchg_8(pcp, nval)	__this_cpu_generic_xchg(pcp, nval)
# endif
# define __this_cpu_xchg(pcp, nval)	\
	__pcpu_size_call_return2(__this_cpu_xchg_, (pcp), nval)
#endif

#define __this_cpu_generic_cmpxchg(pcp, oval, nval)			\
({									\
	typeof(pcp) ret__;						\
	ret__ = __this_cpu_read(pcp);					\
	if (ret__ == (oval))						\
		__this_cpu_write(pcp, nval);				\
	ret__;								\
})

#ifndef __this_cpu_cmpxchg
# ifndef __this_cpu_cmpxchg_1
#  define __this_cpu_cmpxchg_1(pcp, oval, nval)	__this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# ifndef __this_cpu_cmpxchg_2
#  define __this_cpu_cmpxchg_2(pcp, oval, nval)	__this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# ifndef __this_cpu_cmpxchg_4
#  define __this_cpu_cmpxchg_4(pcp, oval, nval)	__this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# ifndef __this_cpu_cmpxchg_8
#  define __this_cpu_cmpxchg_8(pcp, oval, nval)	__this_cpu_generic_cmpxchg(pcp, oval, nval)
# endif
# define __this_cpu_cmpxchg(pcp, oval, nval)	\
	__pcpu_size_call_return2(__this_cpu_cmpxchg_, pcp, oval, nval)
#endif

#define __this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
({									\
	int __ret = 0;							\
	if (__this_cpu_read(pcp1) == (oval1) &&				\
			 __this_cpu_read(pcp2)  == (oval2)) {		\
		__this_cpu_write(pcp1, (nval1));			\
		__this_cpu_write(pcp2, (nval2));			\
		__ret = 1;						\
	}								\
	(__ret);							\
})

#ifndef __this_cpu_cmpxchg_double
# ifndef __this_cpu_cmpxchg_double_1
#  define __this_cpu_cmpxchg_double_1(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	__this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# ifndef __this_cpu_cmpxchg_double_2
#  define __this_cpu_cmpxchg_double_2(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	__this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# ifndef __this_cpu_cmpxchg_double_4
#  define __this_cpu_cmpxchg_double_4(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	__this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# ifndef __this_cpu_cmpxchg_double_8
#  define __this_cpu_cmpxchg_double_8(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	__this_cpu_generic_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)
# endif
# define __this_cpu_cmpxchg_double(pcp1, pcp2, oval1, oval2, nval1, nval2)	\
	__pcpu_double_call_return_bool(__this_cpu_cmpxchg_double_, (pcp1), (pcp2), (oval1), (oval2), (nval1), (nval2))
#endif

#endif /* __LINUX_PERCPU_H */
