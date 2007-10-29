/* 
 * Copyright (C) 2000 - 2004  Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef _UM_UNISTD_H_
#define _UM_UNISTD_H_

#include <linux/syscalls.h>
#include "linux/resource.h"
#include "asm/uaccess.h"

extern int um_execve(const char *file, char *const argv[], char *const env[]);

#ifdef __KERNEL__
/* We get __ARCH_WANT_OLD_STAT and __ARCH_WANT_STAT64 from the base arch */
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
#endif

#include "asm/arch/unistd.h"

#endif /* _UM_UNISTD_H_*/
