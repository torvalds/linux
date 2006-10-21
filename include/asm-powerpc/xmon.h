#ifndef __ASM_POWERPC_XMON_H
#define __ASM_POWERPC_XMON_H

/*
 * Copyrignt (C) 2006 IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef __KERNEL__

#ifdef CONFIG_XMON
extern void xmon_setup(void);
#else
static inline void xmon_setup(void) { };
#endif

#endif /* __KERNEL __ */
#endif /* __ASM_POWERPC_XMON_H */
