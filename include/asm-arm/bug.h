#ifndef _ASMARM_BUG_H
#define _ASMARM_BUG_H

#include <linux/config.h>
#include <linux/stddef.h>

#ifdef CONFIG_BUG
#ifdef CONFIG_DEBUG_BUGVERBOSE
extern void __bug(const char *file, int line, void *data) __attribute__((noreturn));

/* give file/line information */
#define BUG()		__bug(__FILE__, __LINE__, NULL)

#else

/* this just causes an oops */
#define BUG()		(*(int *)0 = 0)

#endif

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif
