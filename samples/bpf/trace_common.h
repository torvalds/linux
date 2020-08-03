// SPDX-License-Identifier: GPL-2.0
#ifndef __TRACE_COMMON_H
#define __TRACE_COMMON_H

#ifdef __x86_64__
#define SYSCALL(SYS) "__x64_" __stringify(SYS)
#elif defined(__s390x__)
#define SYSCALL(SYS) "__s390x_" __stringify(SYS)
#else
#define SYSCALL(SYS)  __stringify(SYS)
#endif

#endif
