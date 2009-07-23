#ifndef __LINUX_COMPILER_H
#error "Please don't include <linux/compiler-gcc3.h> directly, include <linux/compiler.h> instead."
#endif

#if __GNUC_MINOR__ < 2
# error Sorry, your compiler is too old - please upgrade it.
#endif

#if __GNUC_MINOR__ >= 3
# define __used			__attribute__((__used__))
#else
# define __used			__attribute__((__unused__))
#endif

#if __GNUC_MINOR__ >= 4
#define __must_check		__attribute__((warn_unused_result))
#endif

#ifdef CONFIG_GCOV_KERNEL
# if __GNUC_MINOR__ < 4
#   error "GCOV profiling support for gcc versions below 3.4 not included"
# endif /* __GNUC_MINOR__ */
#endif /* CONFIG_GCOV_KERNEL */

/*
 * A trick to suppress uninitialized variable warning without generating any
 * code
 */
#define uninitialized_var(x) x = x

#define __always_inline		inline __attribute__((always_inline))
