#ifndef _SPARC_BUG_H
#define _SPARC_BUG_H

/* Only use the inline asm until a gcc release that can handle __builtin_trap
 * -rob 2003-06-25
 *
 * gcc-3.3.1 and later will be OK -DaveM
 */
#if (__GNUC__ > 3) || \
    (__GNUC__ == 3 && __GNUC_MINOR__ > 3) || \
    (__GNUC__ == 3 && __GNUC_MINOR__ == 3 && __GNUC_PATCHLEVEL__ >= 4)
#define __bug_trap()		__builtin_trap()
#else
#define __bug_trap()					\
	__asm__ __volatile__ ("t 0x5\n\t" : : )
#endif

#ifdef CONFIG_DEBUG_BUGVERBOSE
extern void do_BUG(const char *file, int line);
#define BUG() do {					\
	do_BUG(__FILE__, __LINE__);			\
	__bug_trap();				\
} while (0)
#else
#define BUG()		__bug_trap()
#endif

#define HAVE_ARCH_BUG
#include <asm-generic/bug.h>

#endif
