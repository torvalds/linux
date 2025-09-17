/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UTSNAME_H
#define _LINUX_UTSNAME_H


#include <linux/sched.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/err.h>
#include <linux/uts_namespace.h>

enum uts_proc {
	UTS_PROC_ARCH,
	UTS_PROC_OSTYPE,
	UTS_PROC_OSRELEASE,
	UTS_PROC_VERSION,
	UTS_PROC_HOSTNAME,
	UTS_PROC_DOMAINNAME,
};

#ifdef CONFIG_PROC_SYSCTL
extern void uts_proc_notify(enum uts_proc proc);
#else
static inline void uts_proc_notify(enum uts_proc proc)
{
}
#endif

static inline struct new_utsname *utsname(void)
{
	return &current->nsproxy->uts_ns->name;
}

static inline struct new_utsname *init_utsname(void)
{
	return &init_uts_ns.name;
}

extern struct rw_semaphore uts_sem;

#endif /* _LINUX_UTSNAME_H */
