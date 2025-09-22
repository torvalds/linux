/* Public domain. */

#ifndef _LINUX_CAPABILITY_H
#define _LINUX_CAPABILITY_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ucred.h>
#include <machine/cpu.h>

#define CAP_SYS_ADMIN	0x1
#define CAP_SYS_NICE	0x2

static inline bool
capable(int cap) 
{ 
	switch (cap) {
	case CAP_SYS_ADMIN:
	case CAP_SYS_NICE:
		return suser(curproc) == 0;
	default:
		panic("unhandled capability");
	}
} 

static inline bool
perfmon_capable(void)
{
	return false;
}

#endif
