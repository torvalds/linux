#ifndef _TOOLS_LINUX_COMPILER_H_
#error "Please don't include <linux/compiler-gcc.h> directly, include <linux/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)

#if GCC_VERSION >= 70000 && !defined(__CHECKER__)
# define __fallthrough __attribute__ ((fallthrough))
#endif
