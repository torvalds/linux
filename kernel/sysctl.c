/*
 * sysctl.c: General linux system control interface
 *
 * Begun 24 March 1995, Stephen Tweedie
 * Added /proc support, Dec 1995
 * Added bdflush entry and intvec min/max checking, 2/23/96, Tom Dyas.
 * Added hooks for /proc/sys/net (minor, minor patch), 96/4/1, Mike Shaver.
 * Added kernel/java-{interpreter,appletviewer}, 96/5/10, Mike Shaver.
 * Dynamic registration fixes, Stephen Tweedie.
 * Added kswapd-interval, ctrl-alt-del, printk stuff, 1/8/97, Chris Horn.
 * Made sysctl support optional via CONFIG_SYSCTL, 1/10/97, Chris
 *  Horn.
 * Added proc_doulongvec_ms_jiffies_minmax, 09/08/99, Carlos H. Bauer.
 * Added proc_doulongvec_minmax, 09/08/99, Carlos H. Bauer.
 * Changed linked lists to use list.h instead of lists.h, 02/24/00, Bill
 *  Wendling.
 * The list_for_each() macro wasn't appropriate for the sysctl loop.
 *  Removed it and replaced it with older style, 03/23/00, Bill Wendling
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/signal.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/ctype.h>
#include <linux/kmemcheck.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/net.h>
#include <linux/sysrq.h>
#include <linux/highuid.h>
#include <linux/writeback.h>
#include <linux/ratelimit.h>
#include <linux/compaction.h>
#include <linux/hugetlb.h>
#include <linux/initrd.h>
#include <linux/key.h>
#include <linux/times.h>
#include <linux/limits.h>
#include <linux/dcache.h>
#include <linux/dnotify.h>
#include <linux/syscalls.h>
#include <linux/vmstat.h>
#include <linux/nfs_fs.h>
#include <linux/acpi.h>
#include <linux/reboot.h>
#include <linux/ftrace.h>
#include <linux/perf_event.h>
#include <linux/kprobes.h>
#include <linux/pipe_fs_i.h>
#include <linux/oom.h>

#include <asm/uaccess.h>
#include <asm/processor.h>

#ifdef CONFIG_X86
#include <asm/nmi.h>
#include <asm/stacktrace.h>
#include <asm/io.h>
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
#include <linux/acct.h>
#endif
#ifdef CONFIG_RT_MUTEXES
#include <linux/rtmutex.h>
#endif
#if defined(CONFIG_PROVE_LOCKING) || defined(CONFIG_LOCK_STAT)
#include <linux/lockdep.h>
#endif
#ifdef CONFIG_CHR_DEV_SG
#include <scsi/sg.h>
#endif

#ifdef CONFIG_LOCKUP_DETECTOR
#include <linux/nmi.h>
#endif


#if defined(CONFIG_SYSCTL)

/* External variables not in a header file. */
extern int sysctl_overcommit_memory;
extern int sysctl_overcommit_ratio;
extern int max_threads;
extern int core_uses_pid;
extern int suid_dumpable;
extern char core_pattern[];
extern unsigned int core_pipe_limit;
extern int pid_max;
extern int min_free_kbytes;
extern int pid_max_min, pid_max_max;
extern int sysctl_drop_caches;
extern int percpu_pagelist_fraction;
extern int compat_log;
extern int latencytop_enabled;
extern int sysctl_nr_open_min, sysctl_nr_open_max;
#ifndef CONFIG_MMU
extern int sysctl_nr_trim_pages;
#endif
#ifdef CONFIG_BLOCK
extern int blk_iopoll_enabled;
#endif

/* Constants used for minimum and  maximum */
#ifdef CONFIG_LOCKUP_DETECTOR
static int sixty = 60;
static int neg_one = -1;
#endif

static int zero;
static int __maybe_unused one = 1;
static int __maybe_unused two = 2;
static unsigned long one_ul = 1;
static int one_hundred = 100;
#ifdef CONFIG_PRINTK
static int ten_thousand = 10000;
#endif

/* this is needed for the proc_doulongvec_minmax of vm_dirty_bytes */
static unsigned long dirty_bytes_min = 2 * PAGE_SIZE;

/* this is needed for the proc_dointvec_minmax for [fs_]overflow UID and GID */
static int maxolduid = 65535;
static int minolduid;
static int min_percpu_pagelist_fract = 8;

static int ngroups_max = NGROUPS_MAX;

#ifdef CONFIG_INOTIFY_USER
#include <linux/inotify.h>
#endif
#ifdef CONFIG_SPARC
#include <asm/system.h>
#endif

#ifdef CONFIG_SPARC64
extern int sysctl_tsb_ratio;
#endif

#ifdef __hppa__
extern int pwrsw_enabled;
extern int unaligned_enabled;
#endif

#ifdef CONFIG_S390
#ifdef CONFIG_MATHEMU
extern int sysctl_ieee_emulation_warnings;
#endif
extern int sysctl_userprocess_debug;
extern int spin_retry;
#endif

#ifdef CONFIG_IA64
extern int no_unaligned_warning;
extern int unaligned_dump_stack;
#endif

#ifdef CONFIG_PROC_SYSCTL
static int proc_do_cad_pid(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos);
static int proc_taint(struct ctl_table *table, int write,
			       void __user *buffer, size_t *lenp, loff_t *ppos);
#endif

#ifdef CONFIG_MAGIC_SYSRQ
static int __sysrq_enabled; /* Note: sysrq code ises it's own private copy */

static int sysrq_sysctl_handler(ctl_table *table, int write,
				void __user *buffer, size_t *lenp,
				loff_t *ppos)
{
	int error;

	error = proc_dointvec(table, write, buffer, lenp, ppos);
	if (error)
		return error;

	if (write)
		sysrq_toggle_support(__sysrq_enabled);

	return 0;
}

#endif

static struct ctl_table root_table[];
static struct ctl_table_root sysctl_table_root;
static struct ctl_table_header root_table_header = {
	.count = 1,
	.ctl_table = root_table,
	.ctl_entry = LIST_HEAD_INIT(sysctl_table_root.default_set.list),
	.root = &sysctl_table_root,
	.set = &sysctl_table_root.default_set,
};
static struct ctl_table_root sysctl_table_root = {
	.root_list = LIST_HEAD_INIT(sysctl_table_root.root_list),
	.default_set.list = LIST_HEAD_INIT(root_table_header.ctl_entry),
};

static struct ctl_table kern_table[];
static struct ctl_table vm_table[];
static struct ctl_table fs_table[];
static struct ctl_table debug_table[];
static struct ctl_table dev_table[];
extern struct ctl_table random_table[];
#ifdef CONFIG_EPOLL
extern struct ctl_table epoll_table[];
#endif

#ifdef HAVE_ARCH_PICK_MMAP_LAYOUT
int sysctl_legacy_va_layout;
#endif

/* The default sysctl tables: */

static struct ctl_table root_table[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= kern_table,
	},
	{
		.procname	= "vm",
		.mode		= 0555,
		.child		= vm_table,
	},
	{
		.procname	= "fs",
		.mode		= 0555,
		.child		= fs_table,
	},
	{
		.procname	= "debug",
		.mode		= 0555,
		.child		= debug_table,
	},
	{
		.procname	= "dev",
		.mode		= 0555,
		.child		= dev_table,
	},
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ }
};

#ifdef CONFIG_SCHED_DEBUG
static int min_sched_granularity_ns = 100000;		/* 100 usecs */
static int max_sched_granularity_ns = NSEC_PER_SEC;	/* 1 second */
static int min_wakeup_granularity_ns;			/* 0 usecs */
static int max_wakeup_granularity_ns = NSEC_PER_SEC;	/* 1 second */
static int min_sched_tunable_scaling = SCHED_TUNABLESCALING_NONE;
static int max_sched_tunable_scaling = SCHED_TUNABLESCALING_END-1;
#endif

#ifdef CONFIG_COMPACTION
static int min_extfrag_threshold;
static int max_extfrag_threshold = 1000;
#endif

static struct ctl_table kern_table[] = {
	{
		.procname	= "sched_child_runs_first",
		.data		= &sysctl_sched_child_runs_first,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SCHED_DEBUG
	{
		.procname	= "sched_min_granularity_ns",
		.data		= &sysctl_sched_min_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_sched_granularity_ns,
		.extra2		= &max_sched_granularity_ns,
	},
	{
		.procname	= "sched_latency_ns",
		.data		= &sysctl_sched_latency,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_sched_granularity_ns,
		.extra2		= &max_sched_granularity_ns,
	},
	{
		.procname	= "sched_wakeup_granularity_ns",
		.data		= &sysctl_sched_wakeup_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_wakeup_granularity_ns,
		.extra2		= &max_wakeup_granularity_ns,
	},
	{
		.procname	= "sched_tunable_scaling",
		.data		= &sysctl_sched_tunable_scaling,
		.maxlen		= sizeof(enum sched_tunable_scaling),
		.mode		= 0644,
		.proc_handler	= sched_proc_update_handler,
		.extra1		= &min_sched_tunable_scaling,
		.extra2		= &max_sched_tunable_scaling,
	},
	{
		.procname	= "sched_migration_cost",
		.data		= &sysctl_sched_migration_cost,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_nr_migrate",
		.data		= &sysctl_sched_nr_migrate,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_time_avg",
		.data		= &sysctl_sched_time_avg,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_shares_window",
		.data		= &sysctl_sched_shares_window,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "timer_migration",
		.data		= &sysctl_timer_migration,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{
		.procname	= "sched_rt_period_us",
		.data		= &sysctl_sched_rt_period,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= sched_rt_handler,
	},
	{
		.procname	= "sched_rt_runtime_us",
		.data		= &sysctl_sched_rt_runtime,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sched_rt_handler,
	},
	{
		.procname	= "sched_compat_yield",
		.data		= &sysctl_sched_compat_yield,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_PROVE_LOCKING
	{
		.procname	= "prove_locking",
		.data		= &prove_locking,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_LOCK_STAT
	{
		.procname	= "lock_stat",
		.data		= &lock_stat,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "panic",
		.data		= &panic_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "core_uses_pid",
		.data		= &core_uses_pid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "core_pattern",
		.data		= core_pattern,
		.maxlen		= CORENAME_MAX_SIZE,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "core_pipe_limit",
		.data		= &core_pipe_limit,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "tainted",
		.maxlen 	= sizeof(long),
		.mode		= 0644,
		.proc_handler	= proc_taint,
	},
#endif
#ifdef CONFIG_LATENCYTOP
	{
		.procname	= "latencytop",
		.data		= &latencytop_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	{
		.procname	= "real-root-dev",
		.data		= &real_root_dev,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "print-fatal-signals",
		.data		= &print_fatal_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_SPARC
	{
		.procname	= "reboot-cmd",
		.data		= reboot_command,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "stop-a",
		.data		= &stop_a_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "scons-poweroff",
		.data		= &scons_pwroff,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_SPARC64
	{
		.procname	= "tsb-ratio",
		.data		= &sysctl_tsb_ratio,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef __hppa__
	{
		.procname	= "soft-power",
		.data		= &pwrsw_enabled,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "unaligned-trap",
		.data		= &unaligned_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "ctrl-alt-del",
		.data		= &C_A_D,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#ifdef CONFIG_FUNCTION_TRACER
	{
		.procname	= "ftrace_enabled",
		.data		= &ftrace_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= ftrace_enable_sysctl,
	},
#endif
#ifdef CONFIG_STACK_TRACER
	{
		.procname	= "stack_tracer_enabled",
		.data		= &stack_tracer_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= stack_trace_sysctl,
	},
#endif
#ifdef CONFIG_TRACING
	{
		.procname	= "ftrace_dump_on_oops",
		.data		= &ftrace_dump_on_oops,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_MODULES
	{
		.procname	= "modprobe",
		.data		= &modprobe_path,
		.maxlen		= KMOD_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
	{
		.procname	= "modules_disabled",
		.data		= &modules_disabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		/* only handle a transition from default "0" to "1" */
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &one,
		.extra2		= &one,
	},
#endif
#ifdef CONFIG_HOTPLUG
	{
		.procname	= "hotplug",
		.data		= &uevent_helper,
		.maxlen		= UEVENT_HELPER_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
#endif
#ifdef CONFIG_CHR_DEV_SG
	{
		.procname	= "sg-big-buff",
		.data		= &sg_big_buff,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	{
		.procname	= "acct",
		.data		= &acct_parm,
		.maxlen		= 3*sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_MAGIC_SYSRQ
	{
		.procname	= "sysrq",
		.data		= &__sysrq_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= sysrq_sysctl_handler,
	},
#endif
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "cad_pid",
		.data		= NULL,
		.maxlen		= sizeof (int),
		.mode		= 0600,
		.proc_handler	= proc_do_cad_pid,
	},
#endif
	{
		.procname	= "threads-max",
		.data		= &max_threads,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "random",
		.mode		= 0555,
		.child		= random_table,
	},
	{
		.procname	= "overflowuid",
		.data		= &overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.procname	= "overflowgid",
		.data		= &overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
#ifdef CONFIG_S390
#ifdef CONFIG_MATHEMU
	{
		.procname	= "ieee_emulation_warnings",
		.data		= &sysctl_ieee_emulation_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "userprocess_debug",
		.data		= &show_unhandled_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "pid_max",
		.data		= &pid_max,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &pid_max_min,
		.extra2		= &pid_max_max,
	},
	{
		.procname	= "panic_on_oops",
		.data		= &panic_on_oops,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#if defined CONFIG_PRINTK
	{
		.procname	= "printk",
		.data		= &console_loglevel,
		.maxlen		= 4*sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "printk_ratelimit",
		.data		= &printk_ratelimit_state.interval,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "printk_ratelimit_burst",
		.data		= &printk_ratelimit_state.burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "printk_delay",
		.data		= &printk_delay_msec,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &ten_thousand,
	},
	{
		.procname	= "dmesg_restrict",
		.data		= &dmesg_restrict,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{
		.procname	= "ngroups_max",
		.data		= &ngroups_max,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
#if defined(CONFIG_LOCKUP_DETECTOR)
	{
		.procname       = "watchdog",
		.data           = &watchdog_enabled,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_dowatchdog_enabled,
	},
	{
		.procname	= "watchdog_thresh",
		.data		= &softlockup_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dowatchdog_thresh,
		.extra1		= &neg_one,
		.extra2		= &sixty,
	},
	{
		.procname	= "softlockup_panic",
		.data		= &softlockup_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86) && !defined(CONFIG_LOCKUP_DETECTOR)
	{
		.procname       = "unknown_nmi_panic",
		.data           = &unknown_nmi_panic,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
	{
		.procname       = "nmi_watchdog",
		.data           = &nmi_watchdog_enabled,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = proc_nmi_enabled,
	},
#endif
#if defined(CONFIG_X86)
	{
		.procname	= "panic_on_unrecovered_nmi",
		.data		= &panic_on_unrecovered_nmi,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "panic_on_io_nmi",
		.data		= &panic_on_io_nmi,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "bootloader_type",
		.data		= &bootloader_type,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "bootloader_version",
		.data		= &bootloader_version,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "kstack_depth_to_print",
		.data		= &kstack_depth_to_print,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "io_delay_type",
		.data		= &io_delay_type,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#if defined(CONFIG_MMU)
	{
		.procname	= "randomize_va_space",
		.data		= &randomize_va_space,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#if defined(CONFIG_S390) && defined(CONFIG_SMP)
	{
		.procname	= "spin_retry",
		.data		= &spin_retry,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#if	defined(CONFIG_ACPI_SLEEP) && defined(CONFIG_X86)
	{
		.procname	= "acpi_video_flags",
		.data		= &acpi_realmode_flags,
		.maxlen		= sizeof (unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
#endif
#ifdef CONFIG_IA64
	{
		.procname	= "ignore-unaligned-usertrap",
		.data		= &no_unaligned_warning,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "unaligned-dump-stack",
		.data		= &unaligned_dump_stack,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_DETECT_HUNG_TASK
	{
		.procname	= "hung_task_panic",
		.data		= &sysctl_hung_task_panic,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "hung_task_check_count",
		.data		= &sysctl_hung_task_check_count,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "hung_task_timeout_secs",
		.data		= &sysctl_hung_task_timeout_secs,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_dohung_task_timeout_secs,
	},
	{
		.procname	= "hung_task_warnings",
		.data		= &sysctl_hung_task_warnings,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
#endif
#ifdef CONFIG_COMPAT
	{
		.procname	= "compat-log",
		.data		= &compat_log,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_RT_MUTEXES
	{
		.procname	= "max_lock_depth",
		.data		= &max_lock_depth,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{
		.procname	= "poweroff_cmd",
		.data		= &poweroff_cmd,
		.maxlen		= POWEROFF_CMD_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= proc_dostring,
	},
#ifdef CONFIG_KEYS
	{
		.procname	= "keys",
		.mode		= 0555,
		.child		= key_sysctls,
	},
#endif
#ifdef CONFIG_RCU_TORTURE_TEST
	{
		.procname       = "rcutorture_runnable",
		.data           = &rcutorture_runnable,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_PERF_EVENTS
	{
		.procname	= "perf_event_paranoid",
		.data		= &sysctl_perf_event_paranoid,
		.maxlen		= sizeof(sysctl_perf_event_paranoid),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "perf_event_mlock_kb",
		.data		= &sysctl_perf_event_mlock,
		.maxlen		= sizeof(sysctl_perf_event_mlock),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "perf_event_max_sample_rate",
		.data		= &sysctl_perf_event_sample_rate,
		.maxlen		= sizeof(sysctl_perf_event_sample_rate),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_KMEMCHECK
	{
		.procname	= "kmemcheck",
		.data		= &kmemcheck_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_BLOCK
	{
		.procname	= "blk_iopoll",
		.data		= &blk_iopoll_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ }
};

static struct ctl_table vm_table[] = {
	{
		.procname	= "overcommit_memory",
		.data		= &sysctl_overcommit_memory,
		.maxlen		= sizeof(sysctl_overcommit_memory),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "panic_on_oom",
		.data		= &sysctl_panic_on_oom,
		.maxlen		= sizeof(sysctl_panic_on_oom),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oom_kill_allocating_task",
		.data		= &sysctl_oom_kill_allocating_task,
		.maxlen		= sizeof(sysctl_oom_kill_allocating_task),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "oom_dump_tasks",
		.data		= &sysctl_oom_dump_tasks,
		.maxlen		= sizeof(sysctl_oom_dump_tasks),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "overcommit_ratio",
		.data		= &sysctl_overcommit_ratio,
		.maxlen		= sizeof(sysctl_overcommit_ratio),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "page-cluster", 
		.data		= &page_cluster,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "dirty_background_ratio",
		.data		= &dirty_background_ratio,
		.maxlen		= sizeof(dirty_background_ratio),
		.mode		= 0644,
		.proc_handler	= dirty_background_ratio_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "dirty_background_bytes",
		.data		= &dirty_background_bytes,
		.maxlen		= sizeof(dirty_background_bytes),
		.mode		= 0644,
		.proc_handler	= dirty_background_bytes_handler,
		.extra1		= &one_ul,
	},
	{
		.procname	= "dirty_ratio",
		.data		= &vm_dirty_ratio,
		.maxlen		= sizeof(vm_dirty_ratio),
		.mode		= 0644,
		.proc_handler	= dirty_ratio_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "dirty_bytes",
		.data		= &vm_dirty_bytes,
		.maxlen		= sizeof(vm_dirty_bytes),
		.mode		= 0644,
		.proc_handler	= dirty_bytes_handler,
		.extra1		= &dirty_bytes_min,
	},
	{
		.procname	= "dirty_writeback_centisecs",
		.data		= &dirty_writeback_interval,
		.maxlen		= sizeof(dirty_writeback_interval),
		.mode		= 0644,
		.proc_handler	= dirty_writeback_centisecs_handler,
	},
	{
		.procname	= "dirty_expire_centisecs",
		.data		= &dirty_expire_interval,
		.maxlen		= sizeof(dirty_expire_interval),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "nr_pdflush_threads",
		.data		= &nr_pdflush_threads,
		.maxlen		= sizeof nr_pdflush_threads,
		.mode		= 0444 /* read-only*/,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "swappiness",
		.data		= &vm_swappiness,
		.maxlen		= sizeof(vm_swappiness),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#ifdef CONFIG_HUGETLB_PAGE
	{
		.procname	= "nr_hugepages",
		.data		= NULL,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hugetlb_sysctl_handler,
		.extra1		= (void *)&hugetlb_zero,
		.extra2		= (void *)&hugetlb_infinity,
	},
#ifdef CONFIG_NUMA
	{
		.procname       = "nr_hugepages_mempolicy",
		.data           = NULL,
		.maxlen         = sizeof(unsigned long),
		.mode           = 0644,
		.proc_handler   = &hugetlb_mempolicy_sysctl_handler,
		.extra1		= (void *)&hugetlb_zero,
		.extra2		= (void *)&hugetlb_infinity,
	},
#endif
	 {
		.procname	= "hugetlb_shm_group",
		.data		= &sysctl_hugetlb_shm_group,
		.maxlen		= sizeof(gid_t),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	 },
	 {
		.procname	= "hugepages_treat_as_movable",
		.data		= &hugepages_treat_as_movable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= hugetlb_treat_movable_handler,
	},
	{
		.procname	= "nr_overcommit_hugepages",
		.data		= NULL,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= hugetlb_overcommit_handler,
		.extra1		= (void *)&hugetlb_zero,
		.extra2		= (void *)&hugetlb_infinity,
	},
#endif
	{
		.procname	= "lowmem_reserve_ratio",
		.data		= &sysctl_lowmem_reserve_ratio,
		.maxlen		= sizeof(sysctl_lowmem_reserve_ratio),
		.mode		= 0644,
		.proc_handler	= lowmem_reserve_ratio_sysctl_handler,
	},
	{
		.procname	= "drop_caches",
		.data		= &sysctl_drop_caches,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= drop_caches_sysctl_handler,
	},
#ifdef CONFIG_COMPACTION
	{
		.procname	= "compact_memory",
		.data		= &sysctl_compact_memory,
		.maxlen		= sizeof(int),
		.mode		= 0200,
		.proc_handler	= sysctl_compaction_handler,
	},
	{
		.procname	= "extfrag_threshold",
		.data		= &sysctl_extfrag_threshold,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= sysctl_extfrag_handler,
		.extra1		= &min_extfrag_threshold,
		.extra2		= &max_extfrag_threshold,
	},

#endif /* CONFIG_COMPACTION */
	{
		.procname	= "min_free_kbytes",
		.data		= &min_free_kbytes,
		.maxlen		= sizeof(min_free_kbytes),
		.mode		= 0644,
		.proc_handler	= min_free_kbytes_sysctl_handler,
		.extra1		= &zero,
	},
	{
		.procname	= "percpu_pagelist_fraction",
		.data		= &percpu_pagelist_fraction,
		.maxlen		= sizeof(percpu_pagelist_fraction),
		.mode		= 0644,
		.proc_handler	= percpu_pagelist_fraction_sysctl_handler,
		.extra1		= &min_percpu_pagelist_fract,
	},
#ifdef CONFIG_MMU
	{
		.procname	= "max_map_count",
		.data		= &sysctl_max_map_count,
		.maxlen		= sizeof(sysctl_max_map_count),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
#else
	{
		.procname	= "nr_trim_pages",
		.data		= &sysctl_nr_trim_pages,
		.maxlen		= sizeof(sysctl_nr_trim_pages),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
	},
#endif
	{
		.procname	= "laptop_mode",
		.data		= &laptop_mode,
		.maxlen		= sizeof(laptop_mode),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{
		.procname	= "block_dump",
		.data		= &block_dump,
		.maxlen		= sizeof(block_dump),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
	{
		.procname	= "vfs_cache_pressure",
		.data		= &sysctl_vfs_cache_pressure,
		.maxlen		= sizeof(sysctl_vfs_cache_pressure),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
#ifdef HAVE_ARCH_PICK_MMAP_LAYOUT
	{
		.procname	= "legacy_va_layout",
		.data		= &sysctl_legacy_va_layout,
		.maxlen		= sizeof(sysctl_legacy_va_layout),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
#endif
#ifdef CONFIG_NUMA
	{
		.procname	= "zone_reclaim_mode",
		.data		= &zone_reclaim_mode,
		.maxlen		= sizeof(zone_reclaim_mode),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
	{
		.procname	= "min_unmapped_ratio",
		.data		= &sysctl_min_unmapped_ratio,
		.maxlen		= sizeof(sysctl_min_unmapped_ratio),
		.mode		= 0644,
		.proc_handler	= sysctl_min_unmapped_ratio_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "min_slab_ratio",
		.data		= &sysctl_min_slab_ratio,
		.maxlen		= sizeof(sysctl_min_slab_ratio),
		.mode		= 0644,
		.proc_handler	= sysctl_min_slab_ratio_sysctl_handler,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#endif
#ifdef CONFIG_SMP
	{
		.procname	= "stat_interval",
		.data		= &sysctl_stat_interval,
		.maxlen		= sizeof(sysctl_stat_interval),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
#endif
#ifdef CONFIG_MMU
	{
		.procname	= "mmap_min_addr",
		.data		= &dac_mmap_min_addr,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= mmap_min_addr_handler,
	},
#endif
#ifdef CONFIG_NUMA
	{
		.procname	= "numa_zonelist_order",
		.data		= &numa_zonelist_order,
		.maxlen		= NUMA_ZONELIST_ORDER_LEN,
		.mode		= 0644,
		.proc_handler	= numa_zonelist_order_handler,
	},
#endif
#if (defined(CONFIG_X86_32) && !defined(CONFIG_UML))|| \
   (defined(CONFIG_SUPERH) && defined(CONFIG_VSYSCALL))
	{
		.procname	= "vdso_enabled",
		.data		= &vdso_enabled,
		.maxlen		= sizeof(vdso_enabled),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &zero,
	},
#endif
#ifdef CONFIG_HIGHMEM
	{
		.procname	= "highmem_is_dirtyable",
		.data		= &vm_highmem_is_dirtyable,
		.maxlen		= sizeof(vm_highmem_is_dirtyable),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{
		.procname	= "scan_unevictable_pages",
		.data		= &scan_unevictable_pages,
		.maxlen		= sizeof(scan_unevictable_pages),
		.mode		= 0644,
		.proc_handler	= scan_unevictable_handler,
	},
#ifdef CONFIG_MEMORY_FAILURE
	{
		.procname	= "memory_failure_early_kill",
		.data		= &sysctl_memory_failure_early_kill,
		.maxlen		= sizeof(sysctl_memory_failure_early_kill),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
	{
		.procname	= "memory_failure_recovery",
		.data		= &sysctl_memory_failure_recovery,
		.maxlen		= sizeof(sysctl_memory_failure_recovery),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif

/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ }
};

#if defined(CONFIG_BINFMT_MISC) || defined(CONFIG_BINFMT_MISC_MODULE)
static struct ctl_table binfmt_misc_table[] = {
	{ }
};
#endif

static struct ctl_table fs_table[] = {
	{
		.procname	= "inode-nr",
		.data		= &inodes_stat,
		.maxlen		= 2*sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_nr_inodes,
	},
	{
		.procname	= "inode-state",
		.data		= &inodes_stat,
		.maxlen		= 7*sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_nr_inodes,
	},
	{
		.procname	= "file-nr",
		.data		= &files_stat,
		.maxlen		= sizeof(files_stat),
		.mode		= 0444,
		.proc_handler	= proc_nr_files,
	},
	{
		.procname	= "file-max",
		.data		= &files_stat.max_files,
		.maxlen		= sizeof(files_stat.max_files),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "nr_open",
		.data		= &sysctl_nr_open,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &sysctl_nr_open_min,
		.extra2		= &sysctl_nr_open_max,
	},
	{
		.procname	= "dentry-state",
		.data		= &dentry_stat,
		.maxlen		= 6*sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_nr_dentry,
	},
	{
		.procname	= "overflowuid",
		.data		= &fs_overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.procname	= "overflowgid",
		.data		= &fs_overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
#ifdef CONFIG_FILE_LOCKING
	{
		.procname	= "leases-enable",
		.data		= &leases_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_DNOTIFY
	{
		.procname	= "dir-notify-enable",
		.data		= &dir_notify_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_MMU
#ifdef CONFIG_FILE_LOCKING
	{
		.procname	= "lease-break-time",
		.data		= &lease_break_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
#ifdef CONFIG_AIO
	{
		.procname	= "aio-nr",
		.data		= &aio_nr,
		.maxlen		= sizeof(aio_nr),
		.mode		= 0444,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{
		.procname	= "aio-max-nr",
		.data		= &aio_max_nr,
		.maxlen		= sizeof(aio_max_nr),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
#endif /* CONFIG_AIO */
#ifdef CONFIG_INOTIFY_USER
	{
		.procname	= "inotify",
		.mode		= 0555,
		.child		= inotify_table,
	},
#endif	
#ifdef CONFIG_EPOLL
	{
		.procname	= "epoll",
		.mode		= 0555,
		.child		= epoll_table,
	},
#endif
#endif
	{
		.procname	= "suid_dumpable",
		.data		= &suid_dumpable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &zero,
		.extra2		= &two,
	},
#if defined(CONFIG_BINFMT_MISC) || defined(CONFIG_BINFMT_MISC_MODULE)
	{
		.procname	= "binfmt_misc",
		.mode		= 0555,
		.child		= binfmt_misc_table,
	},
#endif
	{
		.procname	= "pipe-max-size",
		.data		= &pipe_max_size,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &pipe_proc_fn,
		.extra1		= &pipe_min_size,
	},
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ }
};

static struct ctl_table debug_table[] = {
#if defined(CONFIG_X86) || defined(CONFIG_PPC) || defined(CONFIG_SPARC) || \
    defined(CONFIG_S390)
	{
		.procname	= "exception-trace",
		.data		= &show_unhandled_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#endif
#if defined(CONFIG_OPTPROBES)
	{
		.procname	= "kprobes-optimization",
		.data		= &sysctl_kprobes_optimization,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_kprobes_optimization_handler,
		.extra1		= &zero,
		.extra2		= &one,
	},
#endif
	{ }
};

static struct ctl_table dev_table[] = {
	{ }
};

static DEFINE_SPINLOCK(sysctl_lock);

/* called under sysctl_lock */
static int use_table(struct ctl_table_header *p)
{
	if (unlikely(p->unregistering))
		return 0;
	p->used++;
	return 1;
}

/* called under sysctl_lock */
static void unuse_table(struct ctl_table_header *p)
{
	if (!--p->used)
		if (unlikely(p->unregistering))
			complete(p->unregistering);
}

/* called under sysctl_lock, will reacquire if has to wait */
static void start_unregistering(struct ctl_table_header *p)
{
	/*
	 * if p->used is 0, nobody will ever touch that entry again;
	 * we'll eliminate all paths to it before dropping sysctl_lock
	 */
	if (unlikely(p->used)) {
		struct completion wait;
		init_completion(&wait);
		p->unregistering = &wait;
		spin_unlock(&sysctl_lock);
		wait_for_completion(&wait);
		spin_lock(&sysctl_lock);
	} else {
		/* anything non-NULL; we'll never dereference it */
		p->unregistering = ERR_PTR(-EINVAL);
	}
	/*
	 * do not remove from the list until nobody holds it; walking the
	 * list in do_sysctl() relies on that.
	 */
	list_del_init(&p->ctl_entry);
}

void sysctl_head_get(struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	head->count++;
	spin_unlock(&sysctl_lock);
}

void sysctl_head_put(struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	if (!--head->count)
		kfree(head);
	spin_unlock(&sysctl_lock);
}

struct ctl_table_header *sysctl_head_grab(struct ctl_table_header *head)
{
	if (!head)
		BUG();
	spin_lock(&sysctl_lock);
	if (!use_table(head))
		head = ERR_PTR(-ENOENT);
	spin_unlock(&sysctl_lock);
	return head;
}

void sysctl_head_finish(struct ctl_table_header *head)
{
	if (!head)
		return;
	spin_lock(&sysctl_lock);
	unuse_table(head);
	spin_unlock(&sysctl_lock);
}

static struct ctl_table_set *
lookup_header_set(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct ctl_table_set *set = &root->default_set;
	if (root->lookup)
		set = root->lookup(root, namespaces);
	return set;
}

static struct list_head *
lookup_header_list(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct ctl_table_set *set = lookup_header_set(root, namespaces);
	return &set->list;
}

struct ctl_table_header *__sysctl_head_next(struct nsproxy *namespaces,
					    struct ctl_table_header *prev)
{
	struct ctl_table_root *root;
	struct list_head *header_list;
	struct ctl_table_header *head;
	struct list_head *tmp;

	spin_lock(&sysctl_lock);
	if (prev) {
		head = prev;
		tmp = &prev->ctl_entry;
		unuse_table(prev);
		goto next;
	}
	tmp = &root_table_header.ctl_entry;
	for (;;) {
		head = list_entry(tmp, struct ctl_table_header, ctl_entry);

		if (!use_table(head))
			goto next;
		spin_unlock(&sysctl_lock);
		return head;
	next:
		root = head->root;
		tmp = tmp->next;
		header_list = lookup_header_list(root, namespaces);
		if (tmp != header_list)
			continue;

		do {
			root = list_entry(root->root_list.next,
					struct ctl_table_root, root_list);
			if (root == &sysctl_table_root)
				goto out;
			header_list = lookup_header_list(root, namespaces);
		} while (list_empty(header_list));
		tmp = header_list->next;
	}
out:
	spin_unlock(&sysctl_lock);
	return NULL;
}

struct ctl_table_header *sysctl_head_next(struct ctl_table_header *prev)
{
	return __sysctl_head_next(current->nsproxy, prev);
}

void register_sysctl_root(struct ctl_table_root *root)
{
	spin_lock(&sysctl_lock);
	list_add_tail(&root->root_list, &sysctl_table_root.root_list);
	spin_unlock(&sysctl_lock);
}

/*
 * sysctl_perm does NOT grant the superuser all rights automatically, because
 * some sysctl variables are readonly even to root.
 */

static int test_perm(int mode, int op)
{
	if (!current_euid())
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((op & ~mode & (MAY_READ|MAY_WRITE|MAY_EXEC)) == 0)
		return 0;
	return -EACCES;
}

int sysctl_perm(struct ctl_table_root *root, struct ctl_table *table, int op)
{
	int error;
	int mode;

	error = security_sysctl(table, op & (MAY_READ | MAY_WRITE | MAY_EXEC));
	if (error)
		return error;

	if (root->permissions)
		mode = root->permissions(root, current->nsproxy, table);
	else
		mode = table->mode;

	return test_perm(mode, op);
}

static void sysctl_set_parent(struct ctl_table *parent, struct ctl_table *table)
{
	for (; table->procname; table++) {
		table->parent = parent;
		if (table->child)
			sysctl_set_parent(table, table->child);
	}
}

static __init int sysctl_init(void)
{
	sysctl_set_parent(NULL, root_table);
#ifdef CONFIG_SYSCTL_SYSCALL_CHECK
	sysctl_check_table(current->nsproxy, root_table);
#endif
	return 0;
}

core_initcall(sysctl_init);

static struct ctl_table *is_branch_in(struct ctl_table *branch,
				      struct ctl_table *table)
{
	struct ctl_table *p;
	const char *s = branch->procname;

	/* branch should have named subdirectory as its first element */
	if (!s || !branch->child)
		return NULL;

	/* ... and nothing else */
	if (branch[1].procname)
		return NULL;

	/* table should contain subdirectory with the same name */
	for (p = table; p->procname; p++) {
		if (!p->child)
			continue;
		if (p->procname && strcmp(p->procname, s) == 0)
			return p;
	}
	return NULL;
}

/* see if attaching q to p would be an improvement */
static void try_attach(struct ctl_table_header *p, struct ctl_table_header *q)
{
	struct ctl_table *to = p->ctl_table, *by = q->ctl_table;
	struct ctl_table *next;
	int is_better = 0;
	int not_in_parent = !p->attached_by;

	while ((next = is_branch_in(by, to)) != NULL) {
		if (by == q->attached_by)
			is_better = 1;
		if (to == p->attached_by)
			not_in_parent = 1;
		by = by->child;
		to = next->child;
	}

	if (is_better && not_in_parent) {
		q->attached_by = by;
		q->attached_to = to;
		q->parent = p;
	}
}

/**
 * __register_sysctl_paths - register a sysctl hierarchy
 * @root: List of sysctl headers to register on
 * @namespaces: Data to compute which lists of sysctl entries are visible
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * The members of the &struct ctl_table structure are used as follows:
 *
 * procname - the name of the sysctl file under /proc/sys. Set to %NULL to not
 *            enter a sysctl file
 *
 * data - a pointer to data for use by proc_handler
 *
 * maxlen - the maximum size in bytes of the data
 *
 * mode - the file permissions for the /proc/sys file, and for sysctl(2)
 *
 * child - a pointer to the child sysctl table if this entry is a directory, or
 *         %NULL.
 *
 * proc_handler - the text handler routine (described below)
 *
 * de - for internal use by the sysctl routines
 *
 * extra1, extra2 - extra pointers usable by the proc handler routines
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.
 *
 * sysctl(2) can automatically manage read and write requests through
 * the sysctl table.  The data and maxlen fields of the ctl_table
 * struct enable minimal validation of the values being written to be
 * performed, and the mode field allows minimal authentication.
 *
 * There must be a proc_handler routine for any terminal nodes
 * mirrored under /proc/sys (non-terminals are handled by a built-in
 * directory handler).  Several default handlers are available to
 * cover common cases -
 *
 * proc_dostring(), proc_dointvec(), proc_dointvec_jiffies(),
 * proc_dointvec_userhz_jiffies(), proc_dointvec_minmax(), 
 * proc_doulongvec_ms_jiffies_minmax(), proc_doulongvec_minmax()
 *
 * It is the handler's job to read the input buffer from user memory
 * and process it. The handler should return 0 on success.
 *
 * This routine returns %NULL on a failure to register, and a pointer
 * to the table header on success.
 */
struct ctl_table_header *__register_sysctl_paths(
	struct ctl_table_root *root,
	struct nsproxy *namespaces,
	const struct ctl_path *path, struct ctl_table *table)
{
	struct ctl_table_header *header;
	struct ctl_table *new, **prevp;
	unsigned int n, npath;
	struct ctl_table_set *set;

	/* Count the path components */
	for (npath = 0; path[npath].procname; ++npath)
		;

	/*
	 * For each path component, allocate a 2-element ctl_table array.
	 * The first array element will be filled with the sysctl entry
	 * for this, the second will be the sentinel (procname == 0).
	 *
	 * We allocate everything in one go so that we don't have to
	 * worry about freeing additional memory in unregister_sysctl_table.
	 */
	header = kzalloc(sizeof(struct ctl_table_header) +
			 (2 * npath * sizeof(struct ctl_table)), GFP_KERNEL);
	if (!header)
		return NULL;

	new = (struct ctl_table *) (header + 1);

	/* Now connect the dots */
	prevp = &header->ctl_table;
	for (n = 0; n < npath; ++n, ++path) {
		/* Copy the procname */
		new->procname = path->procname;
		new->mode     = 0555;

		*prevp = new;
		prevp = &new->child;

		new += 2;
	}
	*prevp = table;
	header->ctl_table_arg = table;

	INIT_LIST_HEAD(&header->ctl_entry);
	header->used = 0;
	header->unregistering = NULL;
	header->root = root;
	sysctl_set_parent(NULL, header->ctl_table);
	header->count = 1;
#ifdef CONFIG_SYSCTL_SYSCALL_CHECK
	if (sysctl_check_table(namespaces, header->ctl_table)) {
		kfree(header);
		return NULL;
	}
#endif
	spin_lock(&sysctl_lock);
	header->set = lookup_header_set(root, namespaces);
	header->attached_by = header->ctl_table;
	header->attached_to = root_table;
	header->parent = &root_table_header;
	for (set = header->set; set; set = set->parent) {
		struct ctl_table_header *p;
		list_for_each_entry(p, &set->list, ctl_entry) {
			if (p->unregistering)
				continue;
			try_attach(p, header);
		}
	}
	header->parent->count++;
	list_add_tail(&header->ctl_entry, &header->set->list);
	spin_unlock(&sysctl_lock);

	return header;
}

/**
 * register_sysctl_table_path - register a sysctl table hierarchy
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See __register_sysctl_paths for more details.
 */
struct ctl_table_header *register_sysctl_paths(const struct ctl_path *path,
						struct ctl_table *table)
{
	return __register_sysctl_paths(&sysctl_table_root, current->nsproxy,
					path, table);
}

/**
 * register_sysctl_table - register a sysctl table hierarchy
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See register_sysctl_paths for more details.
 */
struct ctl_table_header *register_sysctl_table(struct ctl_table *table)
{
	static const struct ctl_path null_path[] = { {} };

	return register_sysctl_paths(null_path, table);
}

/**
 * unregister_sysctl_table - unregister a sysctl table hierarchy
 * @header: the header returned from register_sysctl_table
 *
 * Unregisters the sysctl table and all children. proc entries may not
 * actually be removed until they are no longer used by anyone.
 */
void unregister_sysctl_table(struct ctl_table_header * header)
{
	might_sleep();

	if (header == NULL)
		return;

	spin_lock(&sysctl_lock);
	start_unregistering(header);
	if (!--header->parent->count) {
		WARN_ON(1);
		kfree(header->parent);
	}
	if (!--header->count)
		kfree(header);
	spin_unlock(&sysctl_lock);
}

int sysctl_is_seen(struct ctl_table_header *p)
{
	struct ctl_table_set *set = p->set;
	int res;
	spin_lock(&sysctl_lock);
	if (p->unregistering)
		res = 0;
	else if (!set->is_seen)
		res = 1;
	else
		res = set->is_seen(set);
	spin_unlock(&sysctl_lock);
	return res;
}

void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_set *parent,
	int (*is_seen)(struct ctl_table_set *))
{
	INIT_LIST_HEAD(&p->list);
	p->parent = parent ? parent : &sysctl_table_root.default_set;
	p->is_seen = is_seen;
}

#else /* !CONFIG_SYSCTL */
struct ctl_table_header *register_sysctl_table(struct ctl_table * table)
{
	return NULL;
}

struct ctl_table_header *register_sysctl_paths(const struct ctl_path *path,
						    struct ctl_table *table)
{
	return NULL;
}

void unregister_sysctl_table(struct ctl_table_header * table)
{
}

void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_set *parent,
	int (*is_seen)(struct ctl_table_set *))
{
}

void sysctl_head_put(struct ctl_table_header *head)
{
}

#endif /* CONFIG_SYSCTL */

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_SYSCTL

static int _proc_do_string(void* data, int maxlen, int write,
			   void __user *buffer,
			   size_t *lenp, loff_t *ppos)
{
	size_t len;
	char __user *p;
	char c;

	if (!data || !maxlen || !*lenp) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		len = 0;
		p = buffer;
		while (len < *lenp) {
			if (get_user(c, p++))
				return -EFAULT;
			if (c == 0 || c == '\n')
				break;
			len++;
		}
		if (len >= maxlen)
			len = maxlen-1;
		if(copy_from_user(data, buffer, len))
			return -EFAULT;
		((char *) data)[len] = 0;
		*ppos += *lenp;
	} else {
		len = strlen(data);
		if (len > maxlen)
			len = maxlen;

		if (*ppos > len) {
			*lenp = 0;
			return 0;
		}

		data += *ppos;
		len  -= *ppos;

		if (len > *lenp)
			len = *lenp;
		if (len)
			if(copy_to_user(buffer, data, len))
				return -EFAULT;
		if (len < *lenp) {
			if(put_user('\n', ((char __user *) buffer) + len))
				return -EFAULT;
			len++;
		}
		*lenp = len;
		*ppos += len;
	}
	return 0;
}

/**
 * proc_dostring - read a string sysctl
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes a string from/to the user buffer. If the kernel
 * buffer provided is not large enough to hold the string, the
 * string is truncated. The copied string is %NULL-terminated.
 * If the string is being read by the user process, it is copied
 * and a newline '\n' is added. It is truncated if the buffer is
 * not large enough.
 *
 * Returns 0 on success.
 */
int proc_dostring(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return _proc_do_string(table->data, table->maxlen, write,
			       buffer, lenp, ppos);
}

static size_t proc_skip_spaces(char **buf)
{
	size_t ret;
	char *tmp = skip_spaces(*buf);
	ret = tmp - *buf;
	*buf = tmp;
	return ret;
}

static void proc_skip_char(char **buf, size_t *size, const char v)
{
	while (*size) {
		if (**buf != v)
			break;
		(*size)--;
		(*buf)++;
	}
}

#define TMPBUFLEN 22
/**
 * proc_get_long - reads an ASCII formatted integer from a user buffer
 *
 * @buf: a kernel buffer
 * @size: size of the kernel buffer
 * @val: this is where the number will be stored
 * @neg: set to %TRUE if number is negative
 * @perm_tr: a vector which contains the allowed trailers
 * @perm_tr_len: size of the perm_tr vector
 * @tr: pointer to store the trailer character
 *
 * In case of success %0 is returned and @buf and @size are updated with
 * the amount of bytes read. If @tr is non-NULL and a trailing
 * character exists (size is non-zero after returning from this
 * function), @tr is updated with the trailing character.
 */
static int proc_get_long(char **buf, size_t *size,
			  unsigned long *val, bool *neg,
			  const char *perm_tr, unsigned perm_tr_len, char *tr)
{
	int len;
	char *p, tmp[TMPBUFLEN];

	if (!*size)
		return -EINVAL;

	len = *size;
	if (len > TMPBUFLEN - 1)
		len = TMPBUFLEN - 1;

	memcpy(tmp, *buf, len);

	tmp[len] = 0;
	p = tmp;
	if (*p == '-' && *size > 1) {
		*neg = true;
		p++;
	} else
		*neg = false;
	if (!isdigit(*p))
		return -EINVAL;

	*val = simple_strtoul(p, &p, 0);

	len = p - tmp;

	/* We don't know if the next char is whitespace thus we may accept
	 * invalid integers (e.g. 1234...a) or two integers instead of one
	 * (e.g. 123...1). So lets not allow such large numbers. */
	if (len == TMPBUFLEN - 1)
		return -EINVAL;

	if (len < *size && perm_tr_len && !memchr(perm_tr, *p, perm_tr_len))
		return -EINVAL;

	if (tr && (len < *size))
		*tr = *p;

	*buf += len;
	*size -= len;

	return 0;
}

/**
 * proc_put_long - converts an integer to a decimal ASCII formatted string
 *
 * @buf: the user buffer
 * @size: the size of the user buffer
 * @val: the integer to be converted
 * @neg: sign of the number, %TRUE for negative
 *
 * In case of success %0 is returned and @buf and @size are updated with
 * the amount of bytes written.
 */
static int proc_put_long(void __user **buf, size_t *size, unsigned long val,
			  bool neg)
{
	int len;
	char tmp[TMPBUFLEN], *p = tmp;

	sprintf(p, "%s%lu", neg ? "-" : "", val);
	len = strlen(tmp);
	if (len > *size)
		len = *size;
	if (copy_to_user(*buf, tmp, len))
		return -EFAULT;
	*size -= len;
	*buf += len;
	return 0;
}
#undef TMPBUFLEN

static int proc_put_char(void __user **buf, size_t *size, char c)
{
	if (*size) {
		char __user **buffer = (char __user **)buf;
		if (put_user(c, *buffer))
			return -EFAULT;
		(*size)--, (*buffer)++;
		*buf = *buffer;
	}
	return 0;
}

static int do_proc_dointvec_conv(bool *negp, unsigned long *lvalp,
				 int *valp,
				 int write, void *data)
{
	if (write) {
		*valp = *negp ? -*lvalp : *lvalp;
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = true;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = false;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

static const char proc_wspace_sep[] = { ' ', '\t', '\n' };

static int __do_proc_dointvec(void *tbl_data, struct ctl_table *table,
		  int write, void __user *buffer,
		  size_t *lenp, loff_t *ppos,
		  int (*conv)(bool *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
	int *i, vleft, first = 1, err = 0;
	unsigned long page = 0;
	size_t left;
	char *kbuf;
	
	if (!tbl_data || !table->maxlen || !*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) tbl_data;
	vleft = table->maxlen / sizeof(*i);
	left = *lenp;

	if (!conv)
		conv = do_proc_dointvec_conv;

	if (write) {
		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;
		page = __get_free_page(GFP_TEMPORARY);
		kbuf = (char *) page;
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buffer, left)) {
			err = -EFAULT;
			goto free;
		}
		kbuf[left] = 0;
	}

	for (; left && vleft--; i++, first=0) {
		unsigned long lval;
		bool neg;

		if (write) {
			left -= proc_skip_spaces(&kbuf);

			if (!left)
				break;
			err = proc_get_long(&kbuf, &left, &lval, &neg,
					     proc_wspace_sep,
					     sizeof(proc_wspace_sep), NULL);
			if (err)
				break;
			if (conv(&neg, &lval, i, 1, data)) {
				err = -EINVAL;
				break;
			}
		} else {
			if (conv(&neg, &lval, i, 0, data)) {
				err = -EINVAL;
				break;
			}
			if (!first)
				err = proc_put_char(&buffer, &left, '\t');
			if (err)
				break;
			err = proc_put_long(&buffer, &left, lval, neg);
			if (err)
				break;
		}
	}

	if (!write && !first && left && !err)
		err = proc_put_char(&buffer, &left, '\n');
	if (write && !err && left)
		left -= proc_skip_spaces(&kbuf);
free:
	if (write) {
		free_page(page);
		if (first)
			return err ? : -EINVAL;
	}
	*lenp -= left;
	*ppos += *lenp;
	return err;
}

static int do_proc_dointvec(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos,
		  int (*conv)(bool *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
	return __do_proc_dointvec(table->data, table, write,
			buffer, lenp, ppos, conv, data);
}

/**
 * proc_dointvec - read a vector of integers
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 *
 * Returns 0 on success.
 */
int proc_dointvec(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,buffer,lenp,ppos,
		    	    NULL,NULL);
}

/*
 * Taint values can only be increased
 * This means we can safely use a temporary.
 */
static int proc_taint(struct ctl_table *table, int write,
			       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	unsigned long tmptaint = get_taint();
	int err;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	t = *table;
	t.data = &tmptaint;
	err = proc_doulongvec_minmax(&t, write, buffer, lenp, ppos);
	if (err < 0)
		return err;

	if (write) {
		/*
		 * Poor man's atomic or. Not worth adding a primitive
		 * to everyone's atomic.h for this
		 */
		int i;
		for (i = 0; i < BITS_PER_LONG && tmptaint >> i; i++) {
			if ((tmptaint >> i) & 1)
				add_taint(i);
		}
	}

	return err;
}

struct do_proc_dointvec_minmax_conv_param {
	int *min;
	int *max;
};

static int do_proc_dointvec_minmax_conv(bool *negp, unsigned long *lvalp,
					int *valp,
					int write, void *data)
{
	struct do_proc_dointvec_minmax_conv_param *param = data;
	if (write) {
		int val = *negp ? -*lvalp : *lvalp;
		if ((param->min && *param->min > val) ||
		    (param->max && *param->max < val))
			return -EINVAL;
		*valp = val;
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = true;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = false;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

/**
 * proc_dointvec_minmax - read a vector of integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_dointvec_minmax(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct do_proc_dointvec_minmax_conv_param param = {
		.min = (int *) table->extra1,
		.max = (int *) table->extra2,
	};
	return do_proc_dointvec(table, write, buffer, lenp, ppos,
				do_proc_dointvec_minmax_conv, &param);
}

static int __do_proc_doulongvec_minmax(void *data, struct ctl_table *table, int write,
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
	unsigned long *i, *min, *max;
	int vleft, first = 1, err = 0;
	unsigned long page = 0;
	size_t left;
	char *kbuf;

	if (!data || !table->maxlen || !*lenp || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	i = (unsigned long *) data;
	min = (unsigned long *) table->extra1;
	max = (unsigned long *) table->extra2;
	vleft = table->maxlen / sizeof(unsigned long);
	left = *lenp;

	if (write) {
		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;
		page = __get_free_page(GFP_TEMPORARY);
		kbuf = (char *) page;
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buffer, left)) {
			err = -EFAULT;
			goto free;
		}
		kbuf[left] = 0;
	}

	for (; left && vleft--; i++, first = 0) {
		unsigned long val;

		if (write) {
			bool neg;

			left -= proc_skip_spaces(&kbuf);

			err = proc_get_long(&kbuf, &left, &val, &neg,
					     proc_wspace_sep,
					     sizeof(proc_wspace_sep), NULL);
			if (err)
				break;
			if (neg)
				continue;
			if ((min && val < *min) || (max && val > *max))
				continue;
			*i = val;
		} else {
			val = convdiv * (*i) / convmul;
			if (!first)
				err = proc_put_char(&buffer, &left, '\t');
			err = proc_put_long(&buffer, &left, val, false);
			if (err)
				break;
		}
	}

	if (!write && !first && left && !err)
		err = proc_put_char(&buffer, &left, '\n');
	if (write && !err)
		left -= proc_skip_spaces(&kbuf);
free:
	if (write) {
		free_page(page);
		if (first)
			return err ? : -EINVAL;
	}
	*lenp -= left;
	*ppos += *lenp;
	return err;
}

static int do_proc_doulongvec_minmax(struct ctl_table *table, int write,
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
	return __do_proc_doulongvec_minmax(table->data, table, write,
			buffer, lenp, ppos, convmul, convdiv);
}

/**
 * proc_doulongvec_minmax - read a vector of long integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_minmax(struct ctl_table *table, int write,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_doulongvec_minmax(table, write, buffer, lenp, ppos, 1l, 1l);
}

/**
 * proc_doulongvec_ms_jiffies_minmax - read a vector of millisecond values with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string. The values
 * are treated as milliseconds, and converted to jiffies when they are stored.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_ms_jiffies_minmax(struct ctl_table *table, int write,
				      void __user *buffer,
				      size_t *lenp, loff_t *ppos)
{
    return do_proc_doulongvec_minmax(table, write, buffer,
				     lenp, ppos, HZ, 1000l);
}


static int do_proc_dointvec_jiffies_conv(bool *negp, unsigned long *lvalp,
					 int *valp,
					 int write, void *data)
{
	if (write) {
		if (*lvalp > LONG_MAX / HZ)
			return 1;
		*valp = *negp ? -(*lvalp*HZ) : (*lvalp*HZ);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = true;
			lval = (unsigned long)-val;
		} else {
			*negp = false;
			lval = (unsigned long)val;
		}
		*lvalp = lval / HZ;
	}
	return 0;
}

static int do_proc_dointvec_userhz_jiffies_conv(bool *negp, unsigned long *lvalp,
						int *valp,
						int write, void *data)
{
	if (write) {
		if (USER_HZ < HZ && *lvalp > (LONG_MAX / HZ) * USER_HZ)
			return 1;
		*valp = clock_t_to_jiffies(*negp ? -*lvalp : *lvalp);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = true;
			lval = (unsigned long)-val;
		} else {
			*negp = false;
			lval = (unsigned long)val;
		}
		*lvalp = jiffies_to_clock_t(lval);
	}
	return 0;
}

static int do_proc_dointvec_ms_jiffies_conv(bool *negp, unsigned long *lvalp,
					    int *valp,
					    int write, void *data)
{
	if (write) {
		*valp = msecs_to_jiffies(*negp ? -*lvalp : *lvalp);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = true;
			lval = (unsigned long)-val;
		} else {
			*negp = false;
			lval = (unsigned long)val;
		}
		*lvalp = jiffies_to_msecs(lval);
	}
	return 0;
}

/**
 * proc_dointvec_jiffies - read a vector of integers as seconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 * The values read are assumed to be in seconds, and are converted into
 * jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_jiffies(struct ctl_table *table, int write,
			  void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,buffer,lenp,ppos,
		    	    do_proc_dointvec_jiffies_conv,NULL);
}

/**
 * proc_dointvec_userhz_jiffies - read a vector of integers as 1/USER_HZ seconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: pointer to the file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 * The values read are assumed to be in 1/USER_HZ seconds, and 
 * are converted into jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_userhz_jiffies(struct ctl_table *table, int write,
				 void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,buffer,lenp,ppos,
		    	    do_proc_dointvec_userhz_jiffies_conv,NULL);
}

/**
 * proc_dointvec_ms_jiffies - read a vector of integers as 1 milliseconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 * @ppos: the current position in the file
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 * The values read are assumed to be in 1/1000 seconds, and 
 * are converted into jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_ms_jiffies(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, write, buffer, lenp, ppos,
				do_proc_dointvec_ms_jiffies_conv, NULL);
}

static int proc_do_cad_pid(struct ctl_table *table, int write,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct pid *new_pid;
	pid_t tmp;
	int r;

	tmp = pid_vnr(cad_pid);

	r = __do_proc_dointvec(&tmp, table, write, buffer,
			       lenp, ppos, NULL, NULL);
	if (r || !write)
		return r;

	new_pid = find_get_pid(tmp);
	if (!new_pid)
		return -ESRCH;

	put_pid(xchg(&cad_pid, new_pid));
	return 0;
}

/**
 * proc_do_large_bitmap - read/write from/to a large bitmap
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * The bitmap is stored at table->data and the bitmap length (in bits)
 * in table->maxlen.
 *
 * We use a range comma separated format (e.g. 1,3-4,10-10) so that
 * large bitmaps may be represented in a compact manner. Writing into
 * the file will clear the bitmap then update it with the given input.
 *
 * Returns 0 on success.
 */
int proc_do_large_bitmap(struct ctl_table *table, int write,
			 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int err = 0;
	bool first = 1;
	size_t left = *lenp;
	unsigned long bitmap_len = table->maxlen;
	unsigned long *bitmap = (unsigned long *) table->data;
	unsigned long *tmp_bitmap = NULL;
	char tr_a[] = { '-', ',', '\n' }, tr_b[] = { ',', '\n', 0 }, c;

	if (!bitmap_len || !left || (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		unsigned long page = 0;
		char *kbuf;

		if (left > PAGE_SIZE - 1)
			left = PAGE_SIZE - 1;

		page = __get_free_page(GFP_TEMPORARY);
		kbuf = (char *) page;
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buffer, left)) {
			free_page(page);
			return -EFAULT;
                }
		kbuf[left] = 0;

		tmp_bitmap = kzalloc(BITS_TO_LONGS(bitmap_len) * sizeof(unsigned long),
				     GFP_KERNEL);
		if (!tmp_bitmap) {
			free_page(page);
			return -ENOMEM;
		}
		proc_skip_char(&kbuf, &left, '\n');
		while (!err && left) {
			unsigned long val_a, val_b;
			bool neg;

			err = proc_get_long(&kbuf, &left, &val_a, &neg, tr_a,
					     sizeof(tr_a), &c);
			if (err)
				break;
			if (val_a >= bitmap_len || neg) {
				err = -EINVAL;
				break;
			}

			val_b = val_a;
			if (left) {
				kbuf++;
				left--;
			}

			if (c == '-') {
				err = proc_get_long(&kbuf, &left, &val_b,
						     &neg, tr_b, sizeof(tr_b),
						     &c);
				if (err)
					break;
				if (val_b >= bitmap_len || neg ||
				    val_a > val_b) {
					err = -EINVAL;
					break;
				}
				if (left) {
					kbuf++;
					left--;
				}
			}

			while (val_a <= val_b)
				set_bit(val_a++, tmp_bitmap);

			first = 0;
			proc_skip_char(&kbuf, &left, '\n');
		}
		free_page(page);
	} else {
		unsigned long bit_a, bit_b = 0;

		while (left) {
			bit_a = find_next_bit(bitmap, bitmap_len, bit_b);
			if (bit_a >= bitmap_len)
				break;
			bit_b = find_next_zero_bit(bitmap, bitmap_len,
						   bit_a + 1) - 1;

			if (!first) {
				err = proc_put_char(&buffer, &left, ',');
				if (err)
					break;
			}
			err = proc_put_long(&buffer, &left, bit_a, false);
			if (err)
				break;
			if (bit_a != bit_b) {
				err = proc_put_char(&buffer, &left, '-');
				if (err)
					break;
				err = proc_put_long(&buffer, &left, bit_b, false);
				if (err)
					break;
			}

			first = 0; bit_b++;
		}
		if (!err)
			err = proc_put_char(&buffer, &left, '\n');
	}

	if (!err) {
		if (write) {
			if (*ppos)
				bitmap_or(bitmap, bitmap, tmp_bitmap, bitmap_len);
			else
				memcpy(bitmap, tmp_bitmap,
					BITS_TO_LONGS(bitmap_len) * sizeof(unsigned long));
		}
		kfree(tmp_bitmap);
		*lenp -= left;
		*ppos += *lenp;
		return 0;
	} else {
		kfree(tmp_bitmap);
		return err;
	}
}

#else /* CONFIG_PROC_FS */

int proc_dostring(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec(struct ctl_table *table, int write,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_userhz_jiffies(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_ms_jiffies(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_ms_jiffies_minmax(struct ctl_table *table, int write,
				      void __user *buffer,
				      size_t *lenp, loff_t *ppos)
{
    return -ENOSYS;
}


#endif /* CONFIG_PROC_FS */

/*
 * No sense putting this after each symbol definition, twice,
 * exception granted :-)
 */
EXPORT_SYMBOL(proc_dointvec);
EXPORT_SYMBOL(proc_dointvec_jiffies);
EXPORT_SYMBOL(proc_dointvec_minmax);
EXPORT_SYMBOL(proc_dointvec_userhz_jiffies);
EXPORT_SYMBOL(proc_dointvec_ms_jiffies);
EXPORT_SYMBOL(proc_dostring);
EXPORT_SYMBOL(proc_doulongvec_minmax);
EXPORT_SYMBOL(proc_doulongvec_ms_jiffies_minmax);
EXPORT_SYMBOL(register_sysctl_table);
EXPORT_SYMBOL(register_sysctl_paths);
EXPORT_SYMBOL(unregister_sysctl_table);
