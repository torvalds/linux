#ifndef __LINUX_COMPILER_H
#error "Please don't include <linux/compiler-gcc.h> directly, include <linux/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */


/* Optimization barrier */
/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("": : :"memory")

/*
 * This macro obfuscates arithmetic on a variable address so that gcc
 * shouldn't recognize the original var, and make assumptions about it.
 *
 * This is needed because the C standard makes it undefined to do
 * pointer arithmetic on "objects" outside their boundaries and the
 * gcc optimizers assume this is the case. In particular they
 * assume such arithmetic does not wrap.
 *
 * A miscompilation has been observed because of this on PPC.
 * To work around it we hide the relationship of the pointer and the object
 * using this macro.
 *
 * Versions of the ppc64 compiler before 4.1 had a bug where use of
 * RELOC_HIDE could trash r30. The bug can be worked around by changing
 * the inline assembly constraint from =g to =r, in this particular
 * case either is valid.
 */
#define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
    __asm__ ("" : "=r"(__ptr) : "0"(ptr));		\
    (typeof(ptr)) (__ptr + (off)); })

#ifdef __CHECKER__
#define __must_be_array(arr) 0
#else
/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a) BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#endif

/*
 * Force always-inline if the user requests it so via the .config,
 * or if gcc is too old:
 */
#if !defined(CONFIG_ARCH_SUPPORTS_OPTIMIZED_INLINING) || \
    !defined(CONFIG_OPTIMIZE_INLINING) || (__GNUC__ < 4)
# define inline		inline		__attribute__((always_inline)) notrace
# define __inline__	__inline__	__attribute__((always_inline)) notrace
# define __inline	__inline	__attribute__((always_inline)) notrace
#else
/* A lot of inline functions can cause havoc with function tracing */
# define inline		inline		notrace
# define __inline__	__inline__	notrace
# define __inline	__inline	notrace
#endif

#define __deprecated			__attribute__((deprecated))
#define __packed			__attribute__((packed))
#define __weak				__attribute__((weak))

/*
 * it doesn't make sense on ARM (currently the only user of __naked) to trace
 * naked functions because then mcount is called without stack and frame pointer
 * being set up and there is no chance to restore the lr register to the value
 * before mcount was called.
 *
 * The asm() bodies of naked functions often depend on standard calling conventions,
 * therefore they must be noinline and noclone.  GCC 4.[56] currently fail to enforce
 * this, so we must do so ourselves.  See GCC PR44290.
 */
#define __naked				__attribute__((naked)) noinline __noclone notrace

#define __noreturn			__attribute__((noreturn))

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
#define __pure				__attribute__((pure))
#define __aligned(x)			__attribute__((aligned(x)))
#define __printf(a, b)			__attribute__((format(printf, a, b)))
#define __scanf(a, b)			__attribute__((format(scanf, a, b)))
#define  noinline			__attribute__((noinline))
#define __attribute_const__		__attribute__((__const__))
#define __maybe_unused			__attribute__((unused))
#define __always_unused			__attribute__((unused))

#define __gcc_header(x) #x
#define _gcc_header(x) __gcc_header(linux/compiler-gcc##x.h)
#define gcc_header(x) _gcc_header(x)
#include gcc_header(__GNUC__)

#if !defined(__noclone)
#define __noclone	/* not needed */
#endif

/*
 * A trick to suppress uninitialized variable warning without generating any
 * code
 */
#define uninitialized_var(x) x = x

#define __always_inline		inline __attribute__((always_inline))
