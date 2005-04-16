/*
 * This is included by init/main.c to check for architecture-dependent bugs.
 *
 * Needs:
 *	void check_bugs(void);
 */
#ifndef _ASM_BUGS_H
#define _ASM_BUGS_H

#include <linux/config.h>

extern void check_bugs32(void);
extern void check_bugs64(void);

static inline void check_bugs(void)
{
	check_bugs32();
#ifdef CONFIG_MIPS64
	check_bugs64();
#endif
}

#endif /* _ASM_BUGS_H */
