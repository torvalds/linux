/* Public domain. */

#ifndef _LINUX_PROCESSOR_H
#define _LINUX_PROCESSOR_H

#include <sys/systm.h>
/* sparc64 cpu.h needs time.h and siginfo.h (indirect via param.h) */
#include <sys/param.h>
#include <machine/cpu.h>
#include <linux/jiffies.h>

static inline void
cpu_relax(void)
{
	CPU_BUSY_CYCLE();
	if (cold) {
		delay(tick);
		jiffies++;
	}
}

#ifndef CACHELINESIZE
#define CACHELINESIZE 64
#endif

#endif
