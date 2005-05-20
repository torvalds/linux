#ifndef _ASMARM_BUG_H
#define _ASMARM_BUG_H

#include <linux/config.h>

#ifdef CONFIG_BUG
#ifdef CONFIG_DEBUG_BUGVERBOSE
extern volatile void __bug(const char *file, int line, void *data);
/* give file/line information */
#define BUG()		__bug(__FILE__, __LINE__, NULL)
#else
#define BUG()		(*(int *)0 = 0)
#endif

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif
