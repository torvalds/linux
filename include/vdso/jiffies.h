/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __VDSO_JIFFIES_H
#define __VDSO_JIFFIES_H

#include <asm/param.h>			/* for HZ */
#include <vdso/time64.h>

/* TICK_NSEC is the time between ticks in nsec */
#define TICK_NSEC ((NSEC_PER_SEC+HZ/2)/HZ)

#endif /* __VDSO_JIFFIES_H */
