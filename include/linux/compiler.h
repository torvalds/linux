#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

#ifndef __ASSEMBLY__

#ifdef __CHECKER__
# define __user		__attribute__((noderef, address_space(1)))
# define __kernel	/* default address space */
# define __safe		__attribute__((safe))
# define __force	__attribute__((force))
# define __nocast	__attribute__((nocast))
# define __iomem	__attribute__((noderef, address_space(2)))
# define __acquires(x)	__attribute__((context(0,1)))
# define __releases(x)	__attribute__((context(1,0)))
# define __acquire(x)	__context__(1)
# define __release(x)	__context__(-1)
# define __cond_lock(x)	((x) ? ({ __context__(1); 1; }) : 0)
extern void __chk_user_ptr(void __user *);
extern void __chk_io_ptr(void __iomem *);
#else
# define __user
# define __kernel
# define __safe
# define __force
# define __nocast
# define __iomem
# define __chk_user_ptr(x) (void)0
# define __chk_io_ptr(x) (void)0
# define __builtin_warning(x, y...) (1)
# define __acquires(x)
# define __releases(x)
# define __acquire(x) (void)0
# define __release(x) (void)0
# define __cond_lock(x) (x)
#endif

#ifdef __KERNEL__

#if __GNUC__ > 4
#error no compiler-gcc.h file for this gcc version
#elif __GNUC__ == 4
# include <linux/compiler-gcc4.h>
#elif __GNUC__ == 3
# include <linux/compiler-gcc3.h>
#else
# error Sorry, your compiler is too old/not recognized.
#endif

/* Intel compiler defines __GNUC__. So we will overwrite implementations
 * coming from above header files here
 */
#ifdef __INTEL_COMPILER
# include <linux/compiler-intel.h>
#endif

/*
 * Generic compiler-dependent macros required for kernel
 * build go below this comment. Actual compiler/compiler version
 * specific implementations come from the above header files
 */

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/* Optimization barrier */
#ifndef barrier
# define barrier() __memory_barrier()
#endif

#ifndef RELOC_HIDE
# define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
     __ptr = (unsigned long) (ptr);				\
    (typeof(ptr)) (__ptr + (off)); })
#endif

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__
/*
 * Allow us to mark functions as 'deprecated' and have gcc emit a nice
 * warning for each use, in hopes of speeding the functions removal.
 * Usage is:
 * 		int __deprecated foo(void)
 */
#ifndef __deprecated
# define __deprecated		/* unimplemented */
#endif

#ifdef MODULE
#define __deprecated_for_modules __deprecated
#else
#define __deprecated_for_modules
#endif

#ifndef __must_check
#define __must_check
#endif

/*
 * Allow us to avoid 'defined but not used' warnings on functions and data,
 * as well as force them to be emitted to the assembly file.
 *
 * As of gcc 3.3, static functions that are not marked with attribute((used))
 * may be elided from the assembly file.  As of gcc 3.3, static data not so
 * marked will not be elided, but this may change in a future gcc version.
 *
 * In prior versions of gcc, such functions and data would be emitted, but
 * would be warned about except with attribute((unused)).
 */
#ifndef __attribute_used__
# define __attribute_used__	/* unimplemented */
#endif

/*
 * From the GCC manual:
 *
 * Many functions have no effects except the return value and their
 * return value depends only on the parameters and/or global
 * variables.  Such a function can be subject to common subexpression
 * elimination and loop optimization just as an arithmetic operator
 * would be.
 * [...]
 */
#ifndef __attribute_pure__
# define __attribute_pure__	/* unimplemented */
#endif

/*
 * From the GCC manual:
 *
 * Many functions do not examine any values except their arguments,
 * and have no effects except the return value.  Basically this is
 * just slightly more strict class than the `pure' attribute above,
 * since function is not allowed to read global memory.
 *
 * Note that a function that has pointer arguments and examines the
 * data pointed to must _not_ be declared `const'.  Likewise, a
 * function that calls a non-`const' function usually must not be
 * `const'.  It does not make sense for a `const' function to return
 * `void'.
 */
#ifndef __attribute_const__
# define __attribute_const__	/* unimplemented */
#endif

#ifndef noinline
#define noinline
#endif

#ifndef __always_inline
#define __always_inline inline
#endif

#endif /* __KERNEL__ */
#endif /* __LINUX_COMPILER_H */
