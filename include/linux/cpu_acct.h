
#ifndef _LINUX_CPU_ACCT_H
#define _LINUX_CPU_ACCT_H

#include <linux/cgroup.h>
#include <asm/cputime.h>

#ifdef CONFIG_CGROUP_CPUACCT
extern void cpuacct_charge(struct task_struct *, cputime_t cputime);
#else
static void inline cpuacct_charge(struct task_struct *p, cputime_t cputime) {}
#endif

#endif
