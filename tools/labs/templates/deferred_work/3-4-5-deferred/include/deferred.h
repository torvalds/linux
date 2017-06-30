/*
 * SO2 - Lab 6 - Deferred Work
 *
 * Exercises #3, #4, #5: deferred work
 *
 * Header file.
 */

#ifndef __DEFERRED_H__
#define __DEFERRED_H__

#include <asm/ioctl.h>

#define MY_IOCTL_TIMER_SET	_IOW('k', 1, unsigned long)
#define MY_IOCTL_TIMER_CANCEL	_IO ('k', 2)
#define MY_IOCTL_TIMER_ALLOC	_IOW('k', 3, unsigned long)
#define MY_IOCTL_TIMER_MON	_IO ('k', 4)

/* converts ioctl command code to message */
inline static char *ioctl_command_to_string(int cmd)
{
	switch(cmd) {
	case MY_IOCTL_TIMER_SET:
		return "Set timer";
	case MY_IOCTL_TIMER_CANCEL:
		return "Cancel timer";
	case MY_IOCTL_TIMER_ALLOC:
		return "Allocate memory";
	case MY_IOCTL_TIMER_MON:
		return "Monitor pid";
	}
	return "Unknown command";
}

#endif /* __DEFERRED_H__ */
