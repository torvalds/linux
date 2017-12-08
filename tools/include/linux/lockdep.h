/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_LOCKDEP_H_
#define _LIBLOCKDEP_LOCKDEP_H_

#include <sys/prctl.h>
#include <sys/syscall.h>
#include <string.h>
#include <limits.h>
#include <linux/utsname.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/kern_levels.h>
#include <linux/err.h>
#include <linux/rcu.h>
#include <linux/list.h>
#include <linux/hardirq.h>
#include <unistd.h>

#define MAX_LOCK_DEPTH 63UL

#define asmlinkage
#define __visible

#include "../../../include/linux/lockdep.h"

struct task_struct {
	u64 curr_chain_key;
	int lockdep_depth;
	unsigned int lockdep_recursion;
	struct held_lock held_locks[MAX_LOCK_DEPTH];
	gfp_t lockdep_reclaim_gfp;
	int pid;
	char comm[17];
};

extern struct task_struct *__curr(void);

#define current (__curr())

static inline int debug_locks_off(void)
{
	return 1;
}

#define task_pid_nr(tsk) ((tsk)->pid)

#define KSYM_NAME_LEN 128
#define printk(...) dprintf(STDOUT_FILENO, __VA_ARGS__)
#define pr_err(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#define pr_warn pr_err

#define list_del_rcu list_del

#define atomic_t unsigned long
#define atomic_inc(x) ((*(x))++)

#define print_tainted() ""
#define static_obj(x) 1

#define debug_show_all_locks()
extern void debug_check_no_locks_held(void);

static __used bool __is_kernel_percpu_address(unsigned long addr, void *can_addr)
{
	return false;
}

#endif
