/* Public domain. */

#ifndef _LINUX_DELAY_H
#define _LINUX_DELAY_H

#include <sys/param.h>
#include <sys/systm.h>

static inline void
udelay(unsigned long usecs)
{
	DELAY(usecs);
}

static inline void
ndelay(unsigned long nsecs)
{
	DELAY(MAX(nsecs / 1000, 1));
}

static inline void
usleep_range(unsigned long min, unsigned long max)
{
	DELAY((min + max) / 2);
}

static inline void
mdelay(unsigned long msecs)
{
	int loops = msecs;
	while (loops--)
		DELAY(1000);
}

#define drm_msleep(x)		mdelay(x)

static inline void
fsleep(unsigned long usecs)
{
	DELAY(usecs);
}

static inline unsigned int
msleep_interruptible(unsigned int msecs)
{
	int r = tsleep_nsec(&nowake, PWAIT|PCATCH, "msleepi",
	    MSEC_TO_NSEC(msecs));
	if (r == EINTR)
		return 1;
	return 0;
}

#endif
