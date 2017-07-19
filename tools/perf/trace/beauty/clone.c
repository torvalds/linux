/*
 * trace/beauty/cone.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "trace/beauty/beauty.h"
#include <linux/kernel.h>
#include <sys/types.h>
#include <uapi/linux/sched.h>

static size_t clone__scnprintf_flags(unsigned long flags, char *bf, size_t size)
{
	int printed = 0;

#define	P_FLAG(n) \
	if (flags & CLONE_##n) { \
		printed += scnprintf(bf + printed, size - printed, "%s%s", printed ? "|" : "", #n); \
		flags &= ~CLONE_##n; \
	}

	P_FLAG(VM);
	P_FLAG(FS);
	P_FLAG(FILES);
	P_FLAG(SIGHAND);
	P_FLAG(PTRACE);
	P_FLAG(VFORK);
	P_FLAG(PARENT);
	P_FLAG(THREAD);
	P_FLAG(NEWNS);
	P_FLAG(SYSVSEM);
	P_FLAG(SETTLS);
	P_FLAG(PARENT_SETTID);
	P_FLAG(CHILD_CLEARTID);
	P_FLAG(DETACHED);
	P_FLAG(UNTRACED);
	P_FLAG(CHILD_SETTID);
	P_FLAG(NEWCGROUP);
	P_FLAG(NEWUTS);
	P_FLAG(NEWIPC);
	P_FLAG(NEWUSER);
	P_FLAG(NEWPID);
	P_FLAG(NEWNET);
	P_FLAG(IO);
#undef P_FLAG

	if (flags)
		printed += scnprintf(bf + printed, size - printed, "%s%#x", printed ? "|" : "", flags);

	return printed;
}

size_t syscall_arg__scnprintf_clone_flags(char *bf, size_t size, struct syscall_arg *arg)
{
	return clone__scnprintf_flags(arg->val, bf, size);
}
