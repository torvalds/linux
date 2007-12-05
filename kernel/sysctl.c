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
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/net.h>
#include <linux/sysrq.h>
#include <linux/highuid.h>
#include <linux/writeback.h>
#include <linux/hugetlb.h>
#include <linux/security.h>
#include <linux/initrd.h>
#include <linux/times.h>
#include <linux/limits.h>
#include <linux/dcache.h>
#include <linux/syscalls.h>
#include <linux/nfs_fs.h>
#include <linux/acpi.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/processor.h>

#ifdef CONFIG_X86
#include <asm/nmi.h>
#include <asm/stacktrace.h>
#endif

static int deprecated_sysctl_warning(struct __sysctl_args *args);

#if defined(CONFIG_SYSCTL)

/* External variables not in a header file. */
extern int C_A_D;
extern int print_fatal_signals;
extern int sysctl_overcommit_memory;
extern int sysctl_overcommit_ratio;
extern int sysctl_panic_on_oom;
extern int sysctl_oom_kill_allocating_task;
extern int max_threads;
extern int core_uses_pid;
extern int suid_dumpable;
extern char core_pattern[];
extern int pid_max;
extern int min_free_kbytes;
extern int printk_ratelimit_jiffies;
extern int printk_ratelimit_burst;
extern int pid_max_min, pid_max_max;
extern int sysctl_drop_caches;
extern int percpu_pagelist_fraction;
extern int compat_log;
extern int maps_protect;
extern int sysctl_stat_interval;
extern int audit_argv_kb;
extern int latencytop_enabled;

/* Constants used for minimum and  maximum */
#ifdef CONFIG_DETECT_SOFTLOCKUP
static int one = 1;
static int sixty = 60;
#endif

#ifdef CONFIG_MMU
static int two = 2;
#endif

static int zero;
static int one_hundred = 100;

/* this is needed for the proc_dointvec_minmax for [fs_]overflow UID and GID */
static int maxolduid = 65535;
static int minolduid;
static int min_percpu_pagelist_fract = 8;

static int ngroups_max = NGROUPS_MAX;

#ifdef CONFIG_KMOD
extern char modprobe_path[];
#endif
#ifdef CONFIG_CHR_DEV_SG
extern int sg_big_buff;
#endif

#ifdef __sparc__
extern char reboot_command [];
extern int stop_a_enabled;
extern int scons_pwroff;
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

extern int sysctl_hz_timer;

#ifdef CONFIG_BSD_PROCESS_ACCT
extern int acct_parm[];
#endif

#ifdef CONFIG_IA64
extern int no_unaligned_warning;
#endif

#ifdef CONFIG_RT_MUTEXES
extern int max_lock_depth;
#endif

#ifdef CONFIG_SYSCTL_SYSCALL
static int parse_table(int __user *, int, void __user *, size_t __user *,
		void __user *, size_t, struct ctl_table *);
#endif


#ifdef CONFIG_PROC_SYSCTL
static int proc_do_cad_pid(struct ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos);
static int proc_dointvec_taint(struct ctl_table *table, int write, struct file *filp,
			       void __user *buffer, size_t *lenp, loff_t *ppos);
#endif

static struct ctl_table root_table[];
static struct ctl_table_root sysctl_table_root;
static struct ctl_table_header root_table_header = {
	.ctl_table = root_table,
	.ctl_entry = LIST_HEAD_INIT(sysctl_table_root.header_list),
	.root = &sysctl_table_root,
};
static struct ctl_table_root sysctl_table_root = {
	.root_list = LIST_HEAD_INIT(sysctl_table_root.root_list),
	.header_list = LIST_HEAD_INIT(root_table_header.ctl_entry),
};

static struct ctl_table kern_table[];
static struct ctl_table vm_table[];
static struct ctl_table fs_table[];
static struct ctl_table debug_table[];
static struct ctl_table dev_table[];
extern struct ctl_table random_table[];
#ifdef CONFIG_INOTIFY_USER
extern struct ctl_table inotify_table[];
#endif

#ifdef HAVE_ARCH_PICK_MMAP_LAYOUT
int sysctl_legacy_va_layout;
#endif

extern int prove_locking;
extern int lock_stat;

/* The default sysctl tables: */

static struct ctl_table root_table[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.mode		= 0555,
		.child		= kern_table,
	},
	{
		.ctl_name	= CTL_VM,
		.procname	= "vm",
		.mode		= 0555,
		.child		= vm_table,
	},
	{
		.ctl_name	= CTL_FS,
		.procname	= "fs",
		.mode		= 0555,
		.child		= fs_table,
	},
	{
		.ctl_name	= CTL_DEBUG,
		.procname	= "debug",
		.mode		= 0555,
		.child		= debug_table,
	},
	{
		.ctl_name	= CTL_DEV,
		.procname	= "dev",
		.mode		= 0555,
		.child		= dev_table,
	},
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ .ctl_name = 0 }
};

#ifdef CONFIG_SCHED_DEBUG
static int min_sched_granularity_ns = 100000;		/* 100 usecs */
static int max_sched_granularity_ns = NSEC_PER_SEC;	/* 1 second */
static int min_wakeup_granularity_ns;			/* 0 usecs */
static int max_wakeup_granularity_ns = NSEC_PER_SEC;	/* 1 second */
#endif

static struct ctl_table kern_table[] = {
#ifdef CONFIG_SCHED_DEBUG
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_min_granularity_ns",
		.data		= &sysctl_sched_min_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &sched_nr_latency_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_sched_granularity_ns,
		.extra2		= &max_sched_granularity_ns,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_latency_ns",
		.data		= &sysctl_sched_latency,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &sched_nr_latency_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_sched_granularity_ns,
		.extra2		= &max_sched_granularity_ns,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_wakeup_granularity_ns",
		.data		= &sysctl_sched_wakeup_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_wakeup_granularity_ns,
		.extra2		= &max_wakeup_granularity_ns,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_batch_wakeup_granularity_ns",
		.data		= &sysctl_sched_batch_wakeup_granularity,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_wakeup_granularity_ns,
		.extra2		= &max_wakeup_granularity_ns,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_child_runs_first",
		.data		= &sysctl_sched_child_runs_first,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_features",
		.data		= &sysctl_sched_features,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_migration_cost",
		.data		= &sysctl_sched_migration_cost,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_nr_migrate",
		.data		= &sysctl_sched_nr_migrate,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_rt_period_ms",
		.data		= &sysctl_sched_rt_period,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_rt_ratio",
		.data		= &sysctl_sched_rt_ratio,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#if defined(CONFIG_FAIR_GROUP_SCHED) && defined(CONFIG_SMP)
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "sched_min_bal_int_shares",
		.data           = &sysctl_sched_min_bal_int_shares,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "sched_max_bal_int_shares",
		.data           = &sysctl_sched_max_bal_int_shares,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
#endif
#endif
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "sched_compat_yield",
		.data		= &sysctl_sched_compat_yield,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_PROVE_LOCKING
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "prove_locking",
		.data		= &prove_locking,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_LOCK_STAT
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "lock_stat",
		.data		= &lock_stat,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_PANIC,
		.procname	= "panic",
		.data		= &panic_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_CORE_USES_PID,
		.procname	= "core_uses_pid",
		.data		= &core_uses_pid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_AUDITSYSCALL
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "audit_argv_kb",
		.data		= &audit_argv_kb,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_CORE_PATTERN,
		.procname	= "core_pattern",
		.data		= core_pattern,
		.maxlen		= CORENAME_MAX_SIZE,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "tainted",
		.data		= &tainted,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_taint,
	},
#endif
#ifdef CONFIG_LATENCYTOP
	{
		.procname	= "latencytop",
		.data		= &latencytop_enabled,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_SECURITY_CAPABILITIES
	{
		.procname	= "cap-bound",
		.data		= &cap_bset,
		.maxlen		= sizeof(kernel_cap_t),
		.mode		= 0600,
		.proc_handler	= &proc_dointvec_bset,
	},
#endif /* def CONFIG_SECURITY_CAPABILITIES */
#ifdef CONFIG_BLK_DEV_INITRD
	{
		.ctl_name	= KERN_REALROOTDEV,
		.procname	= "real-root-dev",
		.data		= &real_root_dev,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "print-fatal-signals",
		.data		= &print_fatal_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef __sparc__
	{
		.ctl_name	= KERN_SPARC_REBOOT,
		.procname	= "reboot-cmd",
		.data		= reboot_command,
		.maxlen		= 256,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
	{
		.ctl_name	= KERN_SPARC_STOP_A,
		.procname	= "stop-a",
		.data		= &stop_a_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_SPARC_SCONS_PWROFF,
		.procname	= "scons-poweroff",
		.data		= &scons_pwroff,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef __hppa__
	{
		.ctl_name	= KERN_HPPA_PWRSW,
		.procname	= "soft-power",
		.data		= &pwrsw_enabled,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_HPPA_UNALIGNED,
		.procname	= "unaligned-trap",
		.data		= &unaligned_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_CTLALTDEL,
		.procname	= "ctrl-alt-del",
		.data		= &C_A_D,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_PRINTK,
		.procname	= "printk",
		.data		= &console_loglevel,
		.maxlen		= 4*sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_KMOD
	{
		.ctl_name	= KERN_MODPROBE,
		.procname	= "modprobe",
		.data		= &modprobe_path,
		.maxlen		= KMOD_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
#endif
#if defined(CONFIG_HOTPLUG) && defined(CONFIG_NET)
	{
		.ctl_name	= KERN_HOTPLUG,
		.procname	= "hotplug",
		.data		= &uevent_helper,
		.maxlen		= UEVENT_HELPER_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
#endif
#ifdef CONFIG_CHR_DEV_SG
	{
		.ctl_name	= KERN_SG_BIG_BUFF,
		.procname	= "sg-big-buff",
		.data		= &sg_big_buff,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	{
		.ctl_name	= KERN_ACCT,
		.procname	= "acct",
		.data		= &acct_parm,
		.maxlen		= 3*sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_MAGIC_SYSRQ
	{
		.ctl_name	= KERN_SYSRQ,
		.procname	= "sysrq",
		.data		= &__sysrq_enabled,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_PROC_SYSCTL
	{
		.procname	= "cad_pid",
		.data		= NULL,
		.maxlen		= sizeof (int),
		.mode		= 0600,
		.proc_handler	= &proc_do_cad_pid,
	},
#endif
	{
		.ctl_name	= KERN_MAX_THREADS,
		.procname	= "threads-max",
		.data		= &max_threads,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_RANDOM,
		.procname	= "random",
		.mode		= 0555,
		.child		= random_table,
	},
	{
		.ctl_name	= KERN_OVERFLOWUID,
		.procname	= "overflowuid",
		.data		= &overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.ctl_name	= KERN_OVERFLOWGID,
		.procname	= "overflowgid",
		.data		= &overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
#ifdef CONFIG_S390
#ifdef CONFIG_MATHEMU
	{
		.ctl_name	= KERN_IEEE_EMULATION_WARNINGS,
		.procname	= "ieee_emulation_warnings",
		.data		= &sysctl_ieee_emulation_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_NO_IDLE_HZ
	{
		.ctl_name       = KERN_HZ_TIMER,
		.procname       = "hz_timer",
		.data           = &sysctl_hz_timer,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_S390_USER_DEBUG_LOGGING,
		.procname	= "userprocess_debug",
		.data		= &sysctl_userprocess_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
	{
		.ctl_name	= KERN_PIDMAX,
		.procname	= "pid_max",
		.data		= &pid_max,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &pid_max_min,
		.extra2		= &pid_max_max,
	},
	{
		.ctl_name	= KERN_PANIC_ON_OOPS,
		.procname	= "panic_on_oops",
		.data		= &panic_on_oops,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_PRINTK_RATELIMIT,
		.procname	= "printk_ratelimit",
		.data		= &printk_ratelimit_jiffies,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= KERN_PRINTK_RATELIMIT_BURST,
		.procname	= "printk_ratelimit_burst",
		.data		= &printk_ratelimit_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_NGROUPS_MAX,
		.procname	= "ngroups_max",
		.data		= &ngroups_max,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
#if defined(CONFIG_X86_LOCAL_APIC) && defined(CONFIG_X86)
	{
		.ctl_name       = KERN_UNKNOWN_NMI_PANIC,
		.procname       = "unknown_nmi_panic",
		.data           = &unknown_nmi_panic,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
	{
		.procname       = "nmi_watchdog",
		.data           = &nmi_watchdog_enabled,
		.maxlen         = sizeof (int),
		.mode           = 0644,
		.proc_handler   = &proc_nmi_enabled,
	},
#endif
#if defined(CONFIG_X86)
	{
		.ctl_name	= KERN_PANIC_ON_NMI,
		.procname	= "panic_on_unrecovered_nmi",
		.data		= &panic_on_unrecovered_nmi,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= KERN_BOOTLOADER_TYPE,
		.procname	= "bootloader_type",
		.data		= &bootloader_type,
		.maxlen		= sizeof (int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "kstack_depth_to_print",
		.data		= &kstack_depth_to_print,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#if defined(CONFIG_MMU)
	{
		.ctl_name	= KERN_RANDOMIZE,
		.procname	= "randomize_va_space",
		.data		= &randomize_va_space,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#if defined(CONFIG_S390) && defined(CONFIG_SMP)
	{
		.ctl_name	= KERN_SPIN_RETRY,
		.procname	= "spin_retry",
		.data		= &spin_retry,
		.maxlen		= sizeof (int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#if	defined(CONFIG_ACPI_SLEEP) && defined(CONFIG_X86)
	{
		.procname	= "acpi_video_flags",
		.data		= &acpi_realmode_flags,
		.maxlen		= sizeof (unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
#endif
#ifdef CONFIG_IA64
	{
		.ctl_name	= KERN_IA64_UNALIGNED,
		.procname	= "ignore-unaligned-usertrap",
		.data		= &no_unaligned_warning,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_DETECT_SOFTLOCKUP
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "softlockup_thresh",
		.data		= &softlockup_thresh,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &one,
		.extra2		= &sixty,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "hung_task_check_count",
		.data		= &sysctl_hung_task_check_count,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.strategy	= &sysctl_intvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "hung_task_timeout_secs",
		.data		= &sysctl_hung_task_timeout_secs,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.strategy	= &sysctl_intvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "hung_task_warnings",
		.data		= &sysctl_hung_task_warnings,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
		.strategy	= &sysctl_intvec,
	},
#endif
#ifdef CONFIG_COMPAT
	{
		.ctl_name	= KERN_COMPAT_LOG,
		.procname	= "compat-log",
		.data		= &compat_log,
		.maxlen		= sizeof (int),
	 	.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_RT_MUTEXES
	{
		.ctl_name	= KERN_MAX_LOCK_DEPTH,
		.procname	= "max_lock_depth",
		.data		= &max_lock_depth,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_PROC_FS
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "maps_protect",
		.data           = &maps_protect,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec,
	},
#endif
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "poweroff_cmd",
		.data		= &poweroff_cmd,
		.maxlen		= POWEROFF_CMD_PATH_LEN,
		.mode		= 0644,
		.proc_handler	= &proc_dostring,
		.strategy	= &sysctl_string,
	},
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ .ctl_name = 0 }
};

static struct ctl_table vm_table[] = {
	{
		.ctl_name	= VM_OVERCOMMIT_MEMORY,
		.procname	= "overcommit_memory",
		.data		= &sysctl_overcommit_memory,
		.maxlen		= sizeof(sysctl_overcommit_memory),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_PANIC_ON_OOM,
		.procname	= "panic_on_oom",
		.data		= &sysctl_panic_on_oom,
		.maxlen		= sizeof(sysctl_panic_on_oom),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "oom_kill_allocating_task",
		.data		= &sysctl_oom_kill_allocating_task,
		.maxlen		= sizeof(sysctl_oom_kill_allocating_task),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_OVERCOMMIT_RATIO,
		.procname	= "overcommit_ratio",
		.data		= &sysctl_overcommit_ratio,
		.maxlen		= sizeof(sysctl_overcommit_ratio),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_PAGE_CLUSTER,
		.procname	= "page-cluster", 
		.data		= &page_cluster,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_DIRTY_BACKGROUND,
		.procname	= "dirty_background_ratio",
		.data		= &dirty_background_ratio,
		.maxlen		= sizeof(dirty_background_ratio),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.ctl_name	= VM_DIRTY_RATIO,
		.procname	= "dirty_ratio",
		.data		= &vm_dirty_ratio,
		.maxlen		= sizeof(vm_dirty_ratio),
		.mode		= 0644,
		.proc_handler	= &dirty_ratio_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.procname	= "dirty_writeback_centisecs",
		.data		= &dirty_writeback_interval,
		.maxlen		= sizeof(dirty_writeback_interval),
		.mode		= 0644,
		.proc_handler	= &dirty_writeback_centisecs_handler,
	},
	{
		.procname	= "dirty_expire_centisecs",
		.data		= &dirty_expire_interval,
		.maxlen		= sizeof(dirty_expire_interval),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_userhz_jiffies,
	},
	{
		.ctl_name	= VM_NR_PDFLUSH_THREADS,
		.procname	= "nr_pdflush_threads",
		.data		= &nr_pdflush_threads,
		.maxlen		= sizeof nr_pdflush_threads,
		.mode		= 0444 /* read-only*/,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= VM_SWAPPINESS,
		.procname	= "swappiness",
		.data		= &vm_swappiness,
		.maxlen		= sizeof(vm_swappiness),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#ifdef CONFIG_HUGETLB_PAGE
	 {
		.procname	= "nr_hugepages",
		.data		= &max_huge_pages,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &hugetlb_sysctl_handler,
		.extra1		= (void *)&hugetlb_zero,
		.extra2		= (void *)&hugetlb_infinity,
	 },
	 {
		.ctl_name	= VM_HUGETLB_GROUP,
		.procname	= "hugetlb_shm_group",
		.data		= &sysctl_hugetlb_shm_group,
		.maxlen		= sizeof(gid_t),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	 },
	 {
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "hugepages_treat_as_movable",
		.data		= &hugepages_treat_as_movable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &hugetlb_treat_movable_handler,
	},
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nr_overcommit_hugepages",
		.data		= &nr_overcommit_huge_pages,
		.maxlen		= sizeof(nr_overcommit_huge_pages),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
#endif
	{
		.ctl_name	= VM_LOWMEM_RESERVE_RATIO,
		.procname	= "lowmem_reserve_ratio",
		.data		= &sysctl_lowmem_reserve_ratio,
		.maxlen		= sizeof(sysctl_lowmem_reserve_ratio),
		.mode		= 0644,
		.proc_handler	= &lowmem_reserve_ratio_sysctl_handler,
		.strategy	= &sysctl_intvec,
	},
	{
		.ctl_name	= VM_DROP_PAGECACHE,
		.procname	= "drop_caches",
		.data		= &sysctl_drop_caches,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= drop_caches_sysctl_handler,
		.strategy	= &sysctl_intvec,
	},
	{
		.ctl_name	= VM_MIN_FREE_KBYTES,
		.procname	= "min_free_kbytes",
		.data		= &min_free_kbytes,
		.maxlen		= sizeof(min_free_kbytes),
		.mode		= 0644,
		.proc_handler	= &min_free_kbytes_sysctl_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
	{
		.ctl_name	= VM_PERCPU_PAGELIST_FRACTION,
		.procname	= "percpu_pagelist_fraction",
		.data		= &percpu_pagelist_fraction,
		.maxlen		= sizeof(percpu_pagelist_fraction),
		.mode		= 0644,
		.proc_handler	= &percpu_pagelist_fraction_sysctl_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_percpu_pagelist_fract,
	},
#ifdef CONFIG_MMU
	{
		.ctl_name	= VM_MAX_MAP_COUNT,
		.procname	= "max_map_count",
		.data		= &sysctl_max_map_count,
		.maxlen		= sizeof(sysctl_max_map_count),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif
	{
		.ctl_name	= VM_LAPTOP_MODE,
		.procname	= "laptop_mode",
		.data		= &laptop_mode,
		.maxlen		= sizeof(laptop_mode),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= VM_BLOCK_DUMP,
		.procname	= "block_dump",
		.data		= &block_dump,
		.maxlen		= sizeof(block_dump),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
	{
		.ctl_name	= VM_VFS_CACHE_PRESSURE,
		.procname	= "vfs_cache_pressure",
		.data		= &sysctl_vfs_cache_pressure,
		.maxlen		= sizeof(sysctl_vfs_cache_pressure),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
#ifdef HAVE_ARCH_PICK_MMAP_LAYOUT
	{
		.ctl_name	= VM_LEGACY_VA_LAYOUT,
		.procname	= "legacy_va_layout",
		.data		= &sysctl_legacy_va_layout,
		.maxlen		= sizeof(sysctl_legacy_va_layout),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
#endif
#ifdef CONFIG_NUMA
	{
		.ctl_name	= VM_ZONE_RECLAIM_MODE,
		.procname	= "zone_reclaim_mode",
		.data		= &zone_reclaim_mode,
		.maxlen		= sizeof(zone_reclaim_mode),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
	{
		.ctl_name	= VM_MIN_UNMAPPED,
		.procname	= "min_unmapped_ratio",
		.data		= &sysctl_min_unmapped_ratio,
		.maxlen		= sizeof(sysctl_min_unmapped_ratio),
		.mode		= 0644,
		.proc_handler	= &sysctl_min_unmapped_ratio_sysctl_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
	{
		.ctl_name	= VM_MIN_SLAB,
		.procname	= "min_slab_ratio",
		.data		= &sysctl_min_slab_ratio,
		.maxlen		= sizeof(sysctl_min_slab_ratio),
		.mode		= 0644,
		.proc_handler	= &sysctl_min_slab_ratio_sysctl_handler,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &one_hundred,
	},
#endif
#ifdef CONFIG_SMP
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "stat_interval",
		.data		= &sysctl_stat_interval,
		.maxlen		= sizeof(sysctl_stat_interval),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
#endif
#ifdef CONFIG_SECURITY
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "mmap_min_addr",
		.data		= &mmap_min_addr,
		.maxlen         = sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
#endif
#ifdef CONFIG_NUMA
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "numa_zonelist_order",
		.data		= &numa_zonelist_order,
		.maxlen		= NUMA_ZONELIST_ORDER_LEN,
		.mode		= 0644,
		.proc_handler	= &numa_zonelist_order_handler,
		.strategy	= &sysctl_string,
	},
#endif
#if (defined(CONFIG_X86_32) && !defined(CONFIG_UML))|| \
   (defined(CONFIG_SUPERH) && defined(CONFIG_VSYSCALL))
	{
		.ctl_name	= VM_VDSO_ENABLED,
		.procname	= "vdso_enabled",
		.data		= &vdso_enabled,
		.maxlen		= sizeof(vdso_enabled),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
#endif
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ .ctl_name = 0 }
};

#if defined(CONFIG_BINFMT_MISC) || defined(CONFIG_BINFMT_MISC_MODULE)
static struct ctl_table binfmt_misc_table[] = {
	{ .ctl_name = 0 }
};
#endif

static struct ctl_table fs_table[] = {
	{
		.ctl_name	= FS_NRINODE,
		.procname	= "inode-nr",
		.data		= &inodes_stat,
		.maxlen		= 2*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_STATINODE,
		.procname	= "inode-state",
		.data		= &inodes_stat,
		.maxlen		= 7*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.procname	= "file-nr",
		.data		= &files_stat,
		.maxlen		= 3*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_nr_files,
	},
	{
		.ctl_name	= FS_MAXFILE,
		.procname	= "file-max",
		.data		= &files_stat.max_files,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_DENTRY,
		.procname	= "dentry-state",
		.data		= &dentry_stat,
		.maxlen		= 6*sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= FS_OVERFLOWUID,
		.procname	= "overflowuid",
		.data		= &fs_overflowuid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.ctl_name	= FS_OVERFLOWGID,
		.procname	= "overflowgid",
		.data		= &fs_overflowgid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &minolduid,
		.extra2		= &maxolduid,
	},
	{
		.ctl_name	= FS_LEASES,
		.procname	= "leases-enable",
		.data		= &leases_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#ifdef CONFIG_DNOTIFY
	{
		.ctl_name	= FS_DIR_NOTIFY,
		.procname	= "dir-notify-enable",
		.data		= &dir_notify_enable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#endif
#ifdef CONFIG_MMU
	{
		.ctl_name	= FS_LEASE_TIME,
		.procname	= "lease-break-time",
		.data		= &lease_break_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
		.extra2		= &two,
	},
	{
		.procname	= "aio-nr",
		.data		= &aio_nr,
		.maxlen		= sizeof(aio_nr),
		.mode		= 0444,
		.proc_handler	= &proc_doulongvec_minmax,
	},
	{
		.procname	= "aio-max-nr",
		.data		= &aio_max_nr,
		.maxlen		= sizeof(aio_max_nr),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax,
	},
#ifdef CONFIG_INOTIFY_USER
	{
		.ctl_name	= FS_INOTIFY,
		.procname	= "inotify",
		.mode		= 0555,
		.child		= inotify_table,
	},
#endif	
#endif
	{
		.ctl_name	= KERN_SETUID_DUMPABLE,
		.procname	= "suid_dumpable",
		.data		= &suid_dumpable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
#if defined(CONFIG_BINFMT_MISC) || defined(CONFIG_BINFMT_MISC_MODULE)
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "binfmt_misc",
		.mode		= 0555,
		.child		= binfmt_misc_table,
	},
#endif
/*
 * NOTE: do not add new entries to this table unless you have read
 * Documentation/sysctl/ctl_unnumbered.txt
 */
	{ .ctl_name = 0 }
};

static struct ctl_table debug_table[] = {
#if defined(CONFIG_X86) || defined(CONFIG_PPC)
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "exception-trace",
		.data		= &show_unhandled_signals,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
#endif
	{ .ctl_name = 0 }
};

static struct ctl_table dev_table[] = {
	{ .ctl_name = 0 }
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
	}
	/*
	 * do not remove from the list until nobody holds it; walking the
	 * list in do_sysctl() relies on that.
	 */
	list_del_init(&p->ctl_entry);
}

void sysctl_head_finish(struct ctl_table_header *head)
{
	if (!head)
		return;
	spin_lock(&sysctl_lock);
	unuse_table(head);
	spin_unlock(&sysctl_lock);
}

static struct list_head *
lookup_header_list(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct list_head *header_list;
	header_list = &root->header_list;
	if (root->lookup)
		header_list = root->lookup(root, namespaces);
	return header_list;
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

#ifdef CONFIG_SYSCTL_SYSCALL
int do_sysctl(int __user *name, int nlen, void __user *oldval, size_t __user *oldlenp,
	       void __user *newval, size_t newlen)
{
	struct ctl_table_header *head;
	int error = -ENOTDIR;

	if (nlen <= 0 || nlen >= CTL_MAXNAME)
		return -ENOTDIR;
	if (oldval) {
		int old_len;
		if (!oldlenp || get_user(old_len, oldlenp))
			return -EFAULT;
	}

	for (head = sysctl_head_next(NULL); head;
			head = sysctl_head_next(head)) {
		error = parse_table(name, nlen, oldval, oldlenp, 
					newval, newlen, head->ctl_table);
		if (error != -ENOTDIR) {
			sysctl_head_finish(head);
			break;
		}
	}
	return error;
}

asmlinkage long sys_sysctl(struct __sysctl_args __user *args)
{
	struct __sysctl_args tmp;
	int error;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	error = deprecated_sysctl_warning(&tmp);
	if (error)
		goto out;

	lock_kernel();
	error = do_sysctl(tmp.name, tmp.nlen, tmp.oldval, tmp.oldlenp,
			  tmp.newval, tmp.newlen);
	unlock_kernel();
out:
	return error;
}
#endif /* CONFIG_SYSCTL_SYSCALL */

/*
 * sysctl_perm does NOT grant the superuser all rights automatically, because
 * some sysctl variables are readonly even to root.
 */

static int test_perm(int mode, int op)
{
	if (!current->euid)
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((mode & op & 0007) == op)
		return 0;
	return -EACCES;
}

int sysctl_perm(struct ctl_table *table, int op)
{
	int error;
	error = security_sysctl(table, op);
	if (error)
		return error;
	return test_perm(table->mode, op);
}

#ifdef CONFIG_SYSCTL_SYSCALL
static int parse_table(int __user *name, int nlen,
		       void __user *oldval, size_t __user *oldlenp,
		       void __user *newval, size_t newlen,
		       struct ctl_table *table)
{
	int n;
repeat:
	if (!nlen)
		return -ENOTDIR;
	if (get_user(n, name))
		return -EFAULT;
	for ( ; table->ctl_name || table->procname; table++) {
		if (!table->ctl_name)
			continue;
		if (n == table->ctl_name) {
			int error;
			if (table->child) {
				if (sysctl_perm(table, 001))
					return -EPERM;
				name++;
				nlen--;
				table = table->child;
				goto repeat;
			}
			error = do_sysctl_strategy(table, name, nlen,
						   oldval, oldlenp,
						   newval, newlen);
			return error;
		}
	}
	return -ENOTDIR;
}

/* Perform the actual read/write of a sysctl table entry. */
int do_sysctl_strategy (struct ctl_table *table,
			int __user *name, int nlen,
			void __user *oldval, size_t __user *oldlenp,
			void __user *newval, size_t newlen)
{
	int op = 0, rc;

	if (oldval)
		op |= 004;
	if (newval) 
		op |= 002;
	if (sysctl_perm(table, op))
		return -EPERM;

	if (table->strategy) {
		rc = table->strategy(table, name, nlen, oldval, oldlenp,
				     newval, newlen);
		if (rc < 0)
			return rc;
		if (rc > 0)
			return 0;
	}

	/* If there is no strategy routine, or if the strategy returns
	 * zero, proceed with automatic r/w */
	if (table->data && table->maxlen) {
		rc = sysctl_data(table, name, nlen, oldval, oldlenp,
				 newval, newlen);
		if (rc < 0)
			return rc;
	}
	return 0;
}
#endif /* CONFIG_SYSCTL_SYSCALL */

static void sysctl_set_parent(struct ctl_table *parent, struct ctl_table *table)
{
	for (; table->ctl_name || table->procname; table++) {
		table->parent = parent;
		if (table->child)
			sysctl_set_parent(table, table->child);
	}
}

static __init int sysctl_init(void)
{
	int err;
	sysctl_set_parent(NULL, root_table);
	err = sysctl_check_table(current->nsproxy, root_table);
	return 0;
}

core_initcall(sysctl_init);

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
 * ctl_name - This is the numeric sysctl value used by sysctl(2). The number
 *            must be unique within that level of sysctl
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
 * strategy - the strategy routine (described below)
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
 * More sophisticated management can be enabled by the provision of a
 * strategy routine with the table entry.  This will be called before
 * any automatic read or write of the data is performed.
 *
 * The strategy routine may return
 *
 * < 0 - Error occurred (error is passed to user process)
 *
 * 0   - OK - proceed with automatic read or write.
 *
 * > 0 - OK - read or write has been done by the strategy routine, so
 *       return immediately.
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
	struct list_head *header_list;
	struct ctl_table_header *header;
	struct ctl_table *new, **prevp;
	unsigned int n, npath;

	/* Count the path components */
	for (npath = 0; path[npath].ctl_name || path[npath].procname; ++npath)
		;

	/*
	 * For each path component, allocate a 2-element ctl_table array.
	 * The first array element will be filled with the sysctl entry
	 * for this, the second will be the sentinel (ctl_name == 0).
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
		new->ctl_name = path->ctl_name;
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
	if (sysctl_check_table(namespaces, header->ctl_table)) {
		kfree(header);
		return NULL;
	}
	spin_lock(&sysctl_lock);
	header_list = lookup_header_list(root, namespaces);
	list_add_tail(&header->ctl_entry, header_list);
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
	spin_unlock(&sysctl_lock);
	kfree(header);
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

#endif /* CONFIG_SYSCTL */

/*
 * /proc/sys support
 */

#ifdef CONFIG_PROC_SYSCTL

static int _proc_do_string(void* data, int maxlen, int write,
			   struct file *filp, void __user *buffer,
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
 * @filp: the file structure
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
int proc_dostring(struct ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return _proc_do_string(table->data, table->maxlen, write, filp,
			       buffer, lenp, ppos);
}


static int do_proc_dointvec_conv(int *negp, unsigned long *lvalp,
				 int *valp,
				 int write, void *data)
{
	if (write) {
		*valp = *negp ? -*lvalp : *lvalp;
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = -1;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = 0;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

static int __do_proc_dointvec(void *tbl_data, struct ctl_table *table,
		  int write, struct file *filp, void __user *buffer,
		  size_t *lenp, loff_t *ppos,
		  int (*conv)(int *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
#define TMPBUFLEN 21
	int *i, vleft, first=1, neg, val;
	unsigned long lval;
	size_t left, len;
	
	char buf[TMPBUFLEN], *p;
	char __user *s = buffer;
	
	if (!tbl_data || !table->maxlen || !*lenp ||
	    (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) tbl_data;
	vleft = table->maxlen / sizeof(*i);
	left = *lenp;

	if (!conv)
		conv = do_proc_dointvec_conv;

	for (; left && vleft--; i++, first=0) {
		if (write) {
			while (left) {
				char c;
				if (get_user(c, s))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				s++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > sizeof(buf) - 1)
				len = sizeof(buf) - 1;
			if (copy_from_user(buf, s, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				p++;
			}
			if (*p < '0' || *p > '9')
				break;

			lval = simple_strtoul(p, &p, 0);

			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			s += len;
			left -= len;

			if (conv(&neg, &lval, i, 1, data))
				break;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
	
			if (conv(&neg, &lval, i, 0, data))
				break;

			sprintf(p, "%s%lu", neg ? "-" : "", lval);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(s, buf, len))
				return -EFAULT;
			left -= len;
			s += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', s))
			return -EFAULT;
		left--, s++;
	}
	if (write) {
		while (left) {
			char c;
			if (get_user(c, s++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	*ppos += *lenp;
	return 0;
#undef TMPBUFLEN
}

static int do_proc_dointvec(struct ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos,
		  int (*conv)(int *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
	return __do_proc_dointvec(table->data, table, write, filp,
			buffer, lenp, ppos, conv, data);
}

/**
 * proc_dointvec - read a vector of integers
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 *
 * Returns 0 on success.
 */
int proc_dointvec(struct ctl_table *table, int write, struct file *filp,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
		    	    NULL,NULL);
}

#define OP_SET	0
#define OP_AND	1
#define OP_OR	2

static int do_proc_dointvec_bset_conv(int *negp, unsigned long *lvalp,
				      int *valp,
				      int write, void *data)
{
	int op = *(int *)data;
	if (write) {
		int val = *negp ? -*lvalp : *lvalp;
		switch(op) {
		case OP_SET:	*valp = val; break;
		case OP_AND:	*valp &= val; break;
		case OP_OR:	*valp |= val; break;
		}
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = -1;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = 0;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

#ifdef CONFIG_SECURITY_CAPABILITIES
/*
 *	init may raise the set.
 */

int proc_dointvec_bset(struct ctl_table *table, int write, struct file *filp,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int op;

	if (write && !capable(CAP_SYS_MODULE)) {
		return -EPERM;
	}

	op = is_global_init(current) ? OP_SET : OP_AND;
	return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
				do_proc_dointvec_bset_conv,&op);
}
#endif /* def CONFIG_SECURITY_CAPABILITIES */

/*
 *	Taint values can only be increased
 */
static int proc_dointvec_taint(struct ctl_table *table, int write, struct file *filp,
			       void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int op;

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	op = OP_OR;
	return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
				do_proc_dointvec_bset_conv,&op);
}

struct do_proc_dointvec_minmax_conv_param {
	int *min;
	int *max;
};

static int do_proc_dointvec_minmax_conv(int *negp, unsigned long *lvalp, 
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
			*negp = -1;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = 0;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

/**
 * proc_dointvec_minmax - read a vector of integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
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
int proc_dointvec_minmax(struct ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct do_proc_dointvec_minmax_conv_param param = {
		.min = (int *) table->extra1,
		.max = (int *) table->extra2,
	};
	return do_proc_dointvec(table, write, filp, buffer, lenp, ppos,
				do_proc_dointvec_minmax_conv, &param);
}

static int __do_proc_doulongvec_minmax(void *data, struct ctl_table *table, int write,
				     struct file *filp,
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
#define TMPBUFLEN 21
	unsigned long *i, *min, *max, val;
	int vleft, first=1, neg;
	size_t len, left;
	char buf[TMPBUFLEN], *p;
	char __user *s = buffer;
	
	if (!data || !table->maxlen || !*lenp ||
	    (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (unsigned long *) data;
	min = (unsigned long *) table->extra1;
	max = (unsigned long *) table->extra2;
	vleft = table->maxlen / sizeof(unsigned long);
	left = *lenp;
	
	for (; left && vleft--; i++, min++, max++, first=0) {
		if (write) {
			while (left) {
				char c;
				if (get_user(c, s))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				s++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if (copy_from_user(buf, s, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				p++;
			}
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0) * convmul / convdiv ;
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			s += len;
			left -= len;

			if(neg)
				continue;
			if ((min && val < *min) || (max && val > *max))
				continue;
			*i = val;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			sprintf(p, "%lu", convdiv * (*i) / convmul);
			len = strlen(buf);
			if (len > left)
				len = left;
			if(copy_to_user(s, buf, len))
				return -EFAULT;
			left -= len;
			s += len;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', s))
			return -EFAULT;
		left--, s++;
	}
	if (write) {
		while (left) {
			char c;
			if (get_user(c, s++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	*ppos += *lenp;
	return 0;
#undef TMPBUFLEN
}

static int do_proc_doulongvec_minmax(struct ctl_table *table, int write,
				     struct file *filp,
				     void __user *buffer,
				     size_t *lenp, loff_t *ppos,
				     unsigned long convmul,
				     unsigned long convdiv)
{
	return __do_proc_doulongvec_minmax(table->data, table, write,
			filp, buffer, lenp, ppos, convmul, convdiv);
}

/**
 * proc_doulongvec_minmax - read a vector of long integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
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
int proc_doulongvec_minmax(struct ctl_table *table, int write, struct file *filp,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_doulongvec_minmax(table, write, filp, buffer, lenp, ppos, 1l, 1l);
}

/**
 * proc_doulongvec_ms_jiffies_minmax - read a vector of millisecond values with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
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
				      struct file *filp,
				      void __user *buffer,
				      size_t *lenp, loff_t *ppos)
{
    return do_proc_doulongvec_minmax(table, write, filp, buffer,
				     lenp, ppos, HZ, 1000l);
}


static int do_proc_dointvec_jiffies_conv(int *negp, unsigned long *lvalp,
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
			*negp = -1;
			lval = (unsigned long)-val;
		} else {
			*negp = 0;
			lval = (unsigned long)val;
		}
		*lvalp = lval / HZ;
	}
	return 0;
}

static int do_proc_dointvec_userhz_jiffies_conv(int *negp, unsigned long *lvalp,
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
			*negp = -1;
			lval = (unsigned long)-val;
		} else {
			*negp = 0;
			lval = (unsigned long)val;
		}
		*lvalp = jiffies_to_clock_t(lval);
	}
	return 0;
}

static int do_proc_dointvec_ms_jiffies_conv(int *negp, unsigned long *lvalp,
					    int *valp,
					    int write, void *data)
{
	if (write) {
		*valp = msecs_to_jiffies(*negp ? -*lvalp : *lvalp);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = -1;
			lval = (unsigned long)-val;
		} else {
			*negp = 0;
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
 * @filp: the file structure
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
int proc_dointvec_jiffies(struct ctl_table *table, int write, struct file *filp,
			  void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
		    	    do_proc_dointvec_jiffies_conv,NULL);
}

/**
 * proc_dointvec_userhz_jiffies - read a vector of integers as 1/USER_HZ seconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
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
int proc_dointvec_userhz_jiffies(struct ctl_table *table, int write, struct file *filp,
				 void __user *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
		    	    do_proc_dointvec_userhz_jiffies_conv,NULL);
}

/**
 * proc_dointvec_ms_jiffies - read a vector of integers as 1 milliseconds
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
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
int proc_dointvec_ms_jiffies(struct ctl_table *table, int write, struct file *filp,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return do_proc_dointvec(table, write, filp, buffer, lenp, ppos,
				do_proc_dointvec_ms_jiffies_conv, NULL);
}

static int proc_do_cad_pid(struct ctl_table *table, int write, struct file *filp,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct pid *new_pid;
	pid_t tmp;
	int r;

	tmp = pid_nr_ns(cad_pid, current->nsproxy->pid_ns);

	r = __do_proc_dointvec(&tmp, table, write, filp, buffer,
			       lenp, ppos, NULL, NULL);
	if (r || !write)
		return r;

	new_pid = find_get_pid(tmp);
	if (!new_pid)
		return -ESRCH;

	put_pid(xchg(&cad_pid, new_pid));
	return 0;
}

#else /* CONFIG_PROC_FS */

int proc_dostring(struct ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec(struct ctl_table *table, int write, struct file *filp,
		  void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_bset(struct ctl_table *table, int write, struct file *filp,
			void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_minmax(struct ctl_table *table, int write, struct file *filp,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_jiffies(struct ctl_table *table, int write, struct file *filp,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_userhz_jiffies(struct ctl_table *table, int write, struct file *filp,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_dointvec_ms_jiffies(struct ctl_table *table, int write, struct file *filp,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_minmax(struct ctl_table *table, int write, struct file *filp,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	return -ENOSYS;
}

int proc_doulongvec_ms_jiffies_minmax(struct ctl_table *table, int write,
				      struct file *filp,
				      void __user *buffer,
				      size_t *lenp, loff_t *ppos)
{
    return -ENOSYS;
}


#endif /* CONFIG_PROC_FS */


#ifdef CONFIG_SYSCTL_SYSCALL
/*
 * General sysctl support routines 
 */

/* The generic sysctl data routine (used if no strategy routine supplied) */
int sysctl_data(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	size_t len;

	/* Get out of I don't have a variable */
	if (!table->data || !table->maxlen)
		return -ENOTDIR;

	if (oldval && oldlenp) {
		if (get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			if (len > table->maxlen)
				len = table->maxlen;
			if (copy_to_user(oldval, table->data, len))
				return -EFAULT;
			if (put_user(len, oldlenp))
				return -EFAULT;
		}
	}

	if (newval && newlen) {
		if (newlen > table->maxlen)
			newlen = table->maxlen;

		if (copy_from_user(table->data, newval, newlen))
			return -EFAULT;
	}
	return 1;
}

/* The generic string strategy routine: */
int sysctl_string(struct ctl_table *table, int __user *name, int nlen,
		  void __user *oldval, size_t __user *oldlenp,
		  void __user *newval, size_t newlen)
{
	if (!table->data || !table->maxlen) 
		return -ENOTDIR;
	
	if (oldval && oldlenp) {
		size_t bufsize;
		if (get_user(bufsize, oldlenp))
			return -EFAULT;
		if (bufsize) {
			size_t len = strlen(table->data), copied;

			/* This shouldn't trigger for a well-formed sysctl */
			if (len > table->maxlen)
				len = table->maxlen;

			/* Copy up to a max of bufsize-1 bytes of the string */
			copied = (len >= bufsize) ? bufsize - 1 : len;

			if (copy_to_user(oldval, table->data, copied) ||
			    put_user(0, (char __user *)(oldval + copied)))
				return -EFAULT;
			if (put_user(len, oldlenp))
				return -EFAULT;
		}
	}
	if (newval && newlen) {
		size_t len = newlen;
		if (len > table->maxlen)
			len = table->maxlen;
		if(copy_from_user(table->data, newval, len))
			return -EFAULT;
		if (len == table->maxlen)
			len--;
		((char *) table->data)[len] = 0;
	}
	return 1;
}

/*
 * This function makes sure that all of the integers in the vector
 * are between the minimum and maximum values given in the arrays
 * table->extra1 and table->extra2, respectively.
 */
int sysctl_intvec(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{

	if (newval && newlen) {
		int __user *vec = (int __user *) newval;
		int *min = (int *) table->extra1;
		int *max = (int *) table->extra2;
		size_t length;
		int i;

		if (newlen % sizeof(int) != 0)
			return -EINVAL;

		if (!table->extra1 && !table->extra2)
			return 0;

		if (newlen > table->maxlen)
			newlen = table->maxlen;
		length = newlen / sizeof(int);

		for (i = 0; i < length; i++) {
			int value;
			if (get_user(value, vec + i))
				return -EFAULT;
			if (min && value < min[i])
				return -EINVAL;
			if (max && value > max[i])
				return -EINVAL;
		}
	}
	return 0;
}

/* Strategy function to convert jiffies to seconds */ 
int sysctl_jiffies(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	if (oldval && oldlenp) {
		size_t olen;

		if (get_user(olen, oldlenp))
			return -EFAULT;
		if (olen) {
			int val;

			if (olen < sizeof(int))
				return -EINVAL;

			val = *(int *)(table->data) / HZ;
			if (put_user(val, (int __user *)oldval))
				return -EFAULT;
			if (put_user(sizeof(int), oldlenp))
				return -EFAULT;
		}
	}
	if (newval && newlen) { 
		int new;
		if (newlen != sizeof(int))
			return -EINVAL; 
		if (get_user(new, (int __user *)newval))
			return -EFAULT;
		*(int *)(table->data) = new*HZ; 
	}
	return 1;
}

/* Strategy function to convert jiffies to seconds */ 
int sysctl_ms_jiffies(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	if (oldval && oldlenp) {
		size_t olen;

		if (get_user(olen, oldlenp))
			return -EFAULT;
		if (olen) {
			int val;

			if (olen < sizeof(int))
				return -EINVAL;

			val = jiffies_to_msecs(*(int *)(table->data));
			if (put_user(val, (int __user *)oldval))
				return -EFAULT;
			if (put_user(sizeof(int), oldlenp))
				return -EFAULT;
		}
	}
	if (newval && newlen) { 
		int new;
		if (newlen != sizeof(int))
			return -EINVAL; 
		if (get_user(new, (int __user *)newval))
			return -EFAULT;
		*(int *)(table->data) = msecs_to_jiffies(new);
	}
	return 1;
}



#else /* CONFIG_SYSCTL_SYSCALL */


asmlinkage long sys_sysctl(struct __sysctl_args __user *args)
{
	struct __sysctl_args tmp;
	int error;

	if (copy_from_user(&tmp, args, sizeof(tmp)))
		return -EFAULT;

	error = deprecated_sysctl_warning(&tmp);

	/* If no error reading the parameters then just -ENOSYS ... */
	if (!error)
		error = -ENOSYS;

	return error;
}

int sysctl_data(struct ctl_table *table, int __user *name, int nlen,
		  void __user *oldval, size_t __user *oldlenp,
		  void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

int sysctl_string(struct ctl_table *table, int __user *name, int nlen,
		  void __user *oldval, size_t __user *oldlenp,
		  void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

int sysctl_intvec(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

int sysctl_jiffies(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

int sysctl_ms_jiffies(struct ctl_table *table, int __user *name, int nlen,
		void __user *oldval, size_t __user *oldlenp,
		void __user *newval, size_t newlen)
{
	return -ENOSYS;
}

#endif /* CONFIG_SYSCTL_SYSCALL */

static int deprecated_sysctl_warning(struct __sysctl_args *args)
{
	static int msg_count;
	int name[CTL_MAXNAME];
	int i;

	/* Check args->nlen. */
	if (args->nlen < 0 || args->nlen > CTL_MAXNAME)
		return -ENOTDIR;

	/* Read in the sysctl name for better debug message logging */
	for (i = 0; i < args->nlen; i++)
		if (get_user(name[i], args->name + i))
			return -EFAULT;

	/* Ignore accesses to kernel.version */
	if ((args->nlen == 2) && (name[0] == CTL_KERN) && (name[1] == KERN_VERSION))
		return 0;

	if (msg_count < 5) {
		msg_count++;
		printk(KERN_INFO
			"warning: process `%s' used the deprecated sysctl "
			"system call with ", current->comm);
		for (i = 0; i < args->nlen; i++)
			printk("%d.", name[i]);
		printk("\n");
	}
	return 0;
}

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
EXPORT_SYMBOL(sysctl_intvec);
EXPORT_SYMBOL(sysctl_jiffies);
EXPORT_SYMBOL(sysctl_ms_jiffies);
EXPORT_SYMBOL(sysctl_string);
EXPORT_SYMBOL(sysctl_data);
EXPORT_SYMBOL(unregister_sysctl_table);
