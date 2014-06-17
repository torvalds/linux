/*
 * linux/percpu-defs.h - basic definitions for percpu areas
 *
 * DO NOT INCLUDE DIRECTLY OUTSIDE PERCPU IMPLEMENTATION PROPER.
 *
 * This file is separate from linux/percpu.h to avoid cyclic inclusion
 * dependency from arch header files.  Only to be included from
 * asm/percpu.h.
 *
 * This file includes macros necessary to declare percpu sections and
 * variables, and definitions of percpu accessors and operations.  It
 * should provide enough percpu features to arch header files even when
 * they can only include asm/percpu.h to avoid cyclic inclusion dependency.
 */

#ifndef _LINUX_PERCPU_DEFS_H
#define _LINUX_PERCPU_DEFS_H

#ifdef CONFIG_SMP

#ifdef MODULE
#define PER_CPU_SHARED_ALIGNED_SECTION ""
#define PER_CPU_ALIGNED_SECTION ""
#else
#define PER_CPU_SHARED_ALIGNED_SECTION "..shared_aligned"
#define PER_CPU_ALIGNED_SECTION "..shared_aligned"
#endif
#define PER_CPU_FIRST_SECTION "..first"

#else

#define PER_CPU_SHARED_ALIGNED_SECTION ""
#define PER_CPU_ALIGNED_SECTION "..shared_aligned"
#define PER_CPU_FIRST_SECTION ""

#endif

/*
 * Base implementations of per-CPU variable declarations and definitions, where
 * the section in which the variable is to be placed is provided by the
 * 'sec' argument.  This may be used to affect the parameters governing the
 * variable's storage.
 *
 * NOTE!  The sections for the DECLARE and for the DEFINE must match, lest
 * linkage errors occur due the compiler generating the wrong code to access
 * that section.
 */
#define __PCPU_ATTRS(sec)						\
	__percpu __attribute__((section(PER_CPU_BASE_SECTION sec)))	\
	PER_CPU_ATTRIBUTES

#define __PCPU_DUMMY_ATTRS						\
	__attribute__((section(".discard"), unused))

/*
 * Macro which verifies @ptr is a percpu pointer without evaluating
 * @ptr.  This is to be used in percpu accessors to verify that the
 * input parameter is a percpu pointer.
 *
 * + 0 is required in order to convert the pointer type from a
 * potential array type to a pointer to a single item of the array.
 */
#define __verify_pcpu_ptr(ptr)	do {					\
	const void __percpu *__vpp_verify = (typeof((ptr) + 0))NULL;	\
	(void)__vpp_verify;						\
} while (0)

/*
 * s390 and alpha modules require percpu variables to be defined as
 * weak to force the compiler to generate GOT based external
 * references for them.  This is necessary because percpu sections
 * will be located outside of the usually addressable area.
 *
 * This definition puts the following two extra restrictions when
 * defining percpu variables.
 *
 * 1. The symbol must be globally unique, even the static ones.
 * 2. Static percpu variables cannot be defined inside a function.
 *
 * Archs which need weak percpu definitions should define
 * ARCH_NEEDS_WEAK_PER_CPU in asm/percpu.h when necessary.
 *
 * To ensure that the generic code observes the above two
 * restrictions, if CONFIG_DEBUG_FORCE_WEAK_PER_CPU is set weak
 * definition is used for all cases.
 */
#if defined(ARCH_NEEDS_WEAK_PER_CPU) || defined(CONFIG_DEBUG_FORCE_WEAK_PER_CPU)
/*
 * __pcpu_scope_* dummy variable is used to enforce scope.  It
 * receives the static modifier when it's used in front of
 * DEFINE_PER_CPU() and will trigger build failure if
 * DECLARE_PER_CPU() is used for the same variable.
 *
 * __pcpu_unique_* dummy variable is used to enforce symbol uniqueness
 * such that hidden weak symbol collision, which will cause unrelated
 * variables to share the same address, can be detected during build.
 */
#define DECLARE_PER_CPU_SECTION(type, name, sec)			\
	extern __PCPU_DUMMY_ATTRS char __pcpu_scope_##name;		\
	extern __PCPU_ATTRS(sec) __typeof__(type) name

#define DEFINE_PER_CPU_SECTION(type, name, sec)				\
	__PCPU_DUMMY_ATTRS char __pcpu_scope_##name;			\
	extern __PCPU_DUMMY_ATTRS char __pcpu_unique_##name;		\
	__PCPU_DUMMY_ATTRS char __pcpu_unique_##name;			\
	extern __PCPU_ATTRS(sec) __typeof__(type) name;			\
	__PCPU_ATTRS(sec) PER_CPU_DEF_ATTRIBUTES __weak			\
	__typeof__(type) name
#else
/*
 * Normal declaration and definition macros.
 */
#define DECLARE_PER_CPU_SECTION(type, name, sec)			\
	extern __PCPU_ATTRS(sec) __typeof__(type) name

#define DEFINE_PER_CPU_SECTION(type, name, sec)				\
	__PCPU_ATTRS(sec) PER_CPU_DEF_ATTRIBUTES			\
	__typeof__(type) name
#endif

/*
 * Variant on the per-CPU variable declaration/definition theme used for
 * ordinary per-CPU variables.
 */
#define DECLARE_PER_CPU(type, name)					\
	DECLARE_PER_CPU_SECTION(type, name, "")

#define DEFINE_PER_CPU(type, name)					\
	DEFINE_PER_CPU_SECTION(type, name, "")

/*
 * Declaration/definition used for per-CPU variables that must come first in
 * the set of variables.
 */
#define DECLARE_PER_CPU_FIRST(type, name)				\
	DECLARE_PER_CPU_SECTION(type, name, PER_CPU_FIRST_SECTION)

#define DEFINE_PER_CPU_FIRST(type, name)				\
	DEFINE_PER_CPU_SECTION(type, name, PER_CPU_FIRST_SECTION)

/*
 * Declaration/definition used for per-CPU variables that must be cacheline
 * aligned under SMP conditions so that, whilst a particular instance of the
 * data corresponds to a particular CPU, inefficiencies due to direct access by
 * other CPUs are reduced by preventing the data from unnecessarily spanning
 * cachelines.
 *
 * An example of this would be statistical data, where each CPU's set of data
 * is updated by that CPU alone, but the data from across all CPUs is collated
 * by a CPU processing a read from a proc file.
 */
#define DECLARE_PER_CPU_SHARED_ALIGNED(type, name)			\
	DECLARE_PER_CPU_SECTION(type, name, PER_CPU_SHARED_ALIGNED_SECTION) \
	____cacheline_aligned_in_smp

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)			\
	DEFINE_PER_CPU_SECTION(type, name, PER_CPU_SHARED_ALIGNED_SECTION) \
	____cacheline_aligned_in_smp

#define DECLARE_PER_CPU_ALIGNED(type, name)				\
	DECLARE_PER_CPU_SECTION(type, name, PER_CPU_ALIGNED_SECTION)	\
	____cacheline_aligned

#define DEFINE_PER_CPU_ALIGNED(type, name)				\
	DEFINE_PER_CPU_SECTION(type, name, PER_CPU_ALIGNED_SECTION)	\
	____cacheline_aligned

/*
 * Declaration/definition used for per-CPU variables that must be page aligned.
 */
#define DECLARE_PER_CPU_PAGE_ALIGNED(type, name)			\
	DECLARE_PER_CPU_SECTION(type, name, "..page_aligned")		\
	__aligned(PAGE_SIZE)

#define DEFINE_PER_CPU_PAGE_ALIGNED(type, name)				\
	DEFINE_PER_CPU_SECTION(type, name, "..page_aligned")		\
	__aligned(PAGE_SIZE)

/*
 * Declaration/definition used for per-CPU variables that must be read mostly.
 */
#define DECLARE_PER_CPU_READ_MOSTLY(type, name)			\
	DECLARE_PER_CPU_SECTION(type, name, "..readmostly")

#define DEFINE_PER_CPU_READ_MOSTLY(type, name)				\
	DEFINE_PER_CPU_SECTION(type, name, "..readmostly")

/*
 * Intermodule exports for per-CPU variables.  sparse forgets about
 * address space across EXPORT_SYMBOL(), change EXPORT_SYMBOL() to
 * noop if __CHECKER__.
 */
#ifndef __CHECKER__
#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(var)
#else
#define EXPORT_PER_CPU_SYMBOL(var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var)
#endif

/*
 * Accessors and operations.
 */
#ifndef __ASSEMBLY__

#ifdef CONFIG_SMP

/*
 * Add an offset to a pointer but keep the pointer as-is.  Use RELOC_HIDE()
 * to prevent the compiler from making incorrect assumptions about the
 * pointer value.  The weird cast keeps both GCC and sparse happy.
 */
#define SHIFT_PERCPU_PTR(__p, __offset)	({				\
	__verify_pcpu_ptr((__p));					\
	RELOC_HIDE((typeof(*(__p)) __kernel __force *)(__p), (__offset)); \
})

/*
 * A percpu variable may point to a discarded regions. The following are
 * established ways to produce a usable pointer from the percpu variable
 * offset.
 */
#define per_cpu(var, cpu) \
	(*SHIFT_PERCPU_PTR(&(var), per_cpu_offset(cpu)))

#define raw_cpu_ptr(ptr) arch_raw_cpu_ptr(ptr)

#ifdef CONFIG_DEBUG_PREEMPT
#define this_cpu_ptr(ptr) SHIFT_PERCPU_PTR(ptr, my_cpu_offset)
#else
#define this_cpu_ptr(ptr) raw_cpu_ptr(ptr)
#endif

#define __get_cpu_var(var) (*this_cpu_ptr(&(var)))
#define __raw_get_cpu_var(var) (*raw_cpu_ptr(&(var)))

#else	/* CONFIG_SMP */

#define VERIFY_PERCPU_PTR(__p) ({			\
	__verify_pcpu_ptr((__p));			\
	(typeof(*(__p)) __kernel __force *)(__p);	\
})

#define per_cpu(var, cpu)	(*((void)(cpu), VERIFY_PERCPU_PTR(&(var))))
#define __get_cpu_var(var)	(*VERIFY_PERCPU_PTR(&(var)))
#define __raw_get_cpu_var(var)	(*VERIFY_PERCPU_PTR(&(var)))
#define this_cpu_ptr(ptr)	per_cpu_ptr(ptr, 0)
#define raw_cpu_ptr(ptr)	this_cpu_ptr(ptr)

#endif	/* CONFIG_SMP */

/* keep until we have removed all uses of __this_cpu_ptr */
#define __this_cpu_ptr(ptr)	raw_cpu_ptr(ptr)

/*
 * Must be an lvalue. Since @var must be a simple identifier,
 * we force a syntax error here if it isn't.
 */
#define get_cpu_var(var) (*({				\
	preempt_disable();				\
	this_cpu_ptr(&var); }))

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

#ifdef CONFIG_SMP
#define per_cpu_ptr(ptr, cpu)	SHIFT_PERCPU_PTR((ptr), per_cpu_offset((cpu)))
#else
#define per_cpu_ptr(ptr, cpu)	({ (void)(cpu); VERIFY_PERCPU_PTR((ptr)); })
#endif

#endif /* __ASSEMBLY__ */
#endif /* _LINUX_PERCPU_DEFS_H */
