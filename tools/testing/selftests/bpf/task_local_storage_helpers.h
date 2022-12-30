/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TASK_LOCAL_STORAGE_HELPER_H
#define __TASK_LOCAL_STORAGE_HELPER_H

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#ifndef __NR_pidfd_open
#ifdef __alpha__
#define __NR_pidfd_open 544
#else
#define __NR_pidfd_open 434
#endif
#endif

static inline int sys_pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(__NR_pidfd_open, pid, flags);
}

#endif
