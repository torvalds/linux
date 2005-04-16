#ifndef _ASMARM_BUG_H
#define _ASMARM_BUG_H

#include <linux/config.h>

#ifdef CONFIG_DEBUG_BUGVERBOSE
extern volatile void __bug(const char *file, int line, void *data);

/* give file/line information */
#define BUG()		__bug(__FILE__, __LINE__, NULL)

#else

/* this just causes an oops */
#define BUG()		(*(int *)0 = 0)

#endif

#define HAVE_ARCH_BUG
#include <asm-generic/bug.h>

#endif
