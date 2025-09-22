/* Public domain. */

#ifndef _LINUX_SCHED_MM_H
#define _LINUX_SCHED_MM_H

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>

static inline void
might_alloc(const unsigned int flags)
{
	if (flags & M_WAITOK)
		assertwaitok();
}

#endif
