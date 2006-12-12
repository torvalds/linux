#ifndef _I386_BUG_H
#define _I386_BUG_H


/*
 * Tell the user there is some problem.
 * The offending file and line are encoded encoded in the __bug_table section.
 */

#ifdef CONFIG_BUG
#define HAVE_ARCH_BUG

#ifdef CONFIG_DEBUG_BUGVERBOSE
#define BUG()								\
	do {								\
		asm volatile("1:\tud2\n"				\
			     ".pushsection __bug_table,\"a\"\n"		\
			     "2:\t.long 1b, %c0\n"			\
			     "\t.word %c1, 0\n"				\
			     "\t.org 2b+%c2\n"				\
			     ".popsection"				\
			     : : "i" (__FILE__), "i" (__LINE__),	\
			     "i" (sizeof(struct bug_entry)));		\
		for(;;) ;						\
	} while(0)

#else
#define BUG()								\
	do {								\
		asm volatile("ud2");					\
		for(;;) ;						\
	} while(0)
#endif
#endif

#include <asm-generic/bug.h>
#endif
