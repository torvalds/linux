/*
 *  linux/fs/proc/array.c
 *
 *  Copyright (C) 1992  by Linus Torvalds
 *  based on ideas by Darren Senn
 *
 * Fixes:
 * Michael. K. Johnson: stat,statm extensions.
 *                      <johnsonm@stolaf.edu>
 *
 * Pauline Middelink :  Made cmdline,envline only break at '\0's, to
 *                      make sure SET_PROCTITLE works. Also removed
 *                      bad '!' which forced address recalculation for
 *                      EVERY character on the current page.
 *                      <middelin@polyware.iaf.nl>
 *
 * Danny ter Haar    :	added cpuinfo
 *			<dth@cistron.nl>
 *
 * Alessandro Rubini :  profile extension.
 *                      <rubini@ipvvis.unipv.it>
 *
 * Jeff Tranter      :  added BogoMips field to cpuinfo
 *                      <Jeff_Tranter@Mitel.COM>
 *
 * Bruno Haible      :  remove 4K limit for the maps file
 *			<haible@ma2s2.mathematik.uni-karlsruhe.de>
 *
 * Yves Arrouye      :  remove removal of trailing spaces in get_array.
 *			<Yves.Arrouye@marin.fdn.fr>
 *
 * Jerome Forissier  :  added per-CPU time information to /proc/stat
 *                      and /proc/<pid>/cpu extension
 *                      <forissier@isia.cma.fr>
 *			- Incorporation and non-SMP safe operation
 *			of forissier patch in 2.1.78 by
 *			Hans Marcus <crowbar@concepts.nl>
 *
 * aeb@cwi.nl        :  /proc/partitions
 *
 *
 * Alan Cox	     :  security fixes.
 *			<Alan.Cox@linux.org>
 *
 * Al Viro           :  safe handling of mm_struct
 *
 * Gerhard Wichert   :  added BIGMEM support
 * Siemens AG           <Gerhard.Wichert@pdb.siemens.de>
 *
 * Al Viro & Jeff Garzik :  moved most of the thing into base.c and
 *			 :  proc_misc.c. The rest may eventually go into
 *			 :  base.c too.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/mman.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/times.h>
#include <linux/cpuset.h>
#include <linux/rcupdate.h>
#include <linux/delayacct.h>
#include <linux/pid_namespace.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include "internal.h"

/* Gcc optimizes away "strlen(x)" for constant x */
#define ADDBUF(buffer, string) \
do { memcpy(buffer, string, strlen(string)); \
     buffer += strlen(string); } while (0)

static inline char *task_name(struct task_struct *p, char *buf)
{
	int i;
	char *name;
	char tcomm[sizeof(p->comm)];

	get_task_comm(tcomm, p);

	ADDBUF(buf, "Name:\t");
	name = tcomm;
	i = sizeof(tcomm);
	do {
		unsigned char c = *name;
		name++;
		i--;
		*buf = c;
		if (!c)
			break;
		if (c == '\\') {
			buf[1] = c;
			buf += 2;
			continue;
		}
		if (c == '\n') {
			buf[0] = '\\';
			buf[1] = 'n';
			buf += 2;
			continue;
		}
		buf++;
	} while (i);
	*buf = '\n';
	return buf+1;
}

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const char *task_state_array[] = {
	"R (running)",		/*  0 */
	"S (sleeping)",		/*  1 */
	"D (disk sleep)",	/*  2 */
	"T (stopped)",		/*  4 */
	"T (tracing stop)",	/*  8 */
	"Z (zombie)",		/* 16 */
	"X (dead)"		/* 32 */
};

static inline const char *get_task_state(struct task_struct *tsk)
{
	unsigned int state = (tsk->state & (TASK_RUNNING |
					    TASK_INTERRUPTIBLE |
					    TASK_UNINTERRUPTIBLE |
					    TASK_STOPPED |
					    TASK_TRACED)) |
					   tsk->exit_state;
	const char **p = &task_state_array[0];

	while (state) {
		p++;
		state >>= 1;
	}
	return *p;
}

static inline char *task_state(struct task_struct *p, char *buffer)
{
	struct group_info *group_info;
	int g;
	struct fdtable *fdt = NULL;
	struct pid_namespace *ns;
	pid_t ppid, tpid;

	ns = current->nsproxy->pid_ns;
	rcu_read_lock();
	ppid = pid_alive(p) ?
		task_tgid_nr_ns(rcu_dereference(p->real_parent), ns) : 0;
	tpid = pid_alive(p) && p->ptrace ?
		task_pid_nr_ns(rcu_dereference(p->parent), ns) : 0;
	buffer += sprintf(buffer,
		"State:\t%s\n"
		"Tgid:\t%d\n"
		"Pid:\t%d\n"
		"PPid:\t%d\n"
		"TracerPid:\t%d\n"
		"Uid:\t%d\t%d\t%d\t%d\n"
		"Gid:\t%d\t%d\t%d\t%d\n",
		get_task_state(p),
		task_tgid_nr_ns(p, ns),
		task_pid_nr_ns(p, ns),
		ppid, tpid,
		p->uid, p->euid, p->suid, p->fsuid,
		p->gid, p->egid, p->sgid, p->fsgid);

	task_lock(p);
	if (p->files)
		fdt = files_fdtable(p->files);
	buffer += sprintf(buffer,
		"FDSize:\t%d\n"
		"Groups:\t",
		fdt ? fdt->max_fds : 0);
	rcu_read_unlock();

	group_info = p->group_info;
	get_group_info(group_info);
	task_unlock(p);

	for (g = 0; g < min(group_info->ngroups, NGROUPS_SMALL); g++)
		buffer += sprintf(buffer, "%d ", GROUP_AT(group_info, g));
	put_group_info(group_info);

	buffer += sprintf(buffer, "\n");
	return buffer;
}

static char *render_sigset_t(const char *header, sigset_t *set, char *buffer)
{
	int i, len;

	len = strlen(header);
	memcpy(buffer, header, len);
	buffer += len;

	i = _NSIG;
	do {
		int x = 0;

		i -= 4;
		if (sigismember(set, i+1)) x |= 1;
		if (sigismember(set, i+2)) x |= 2;
		if (sigismember(set, i+3)) x |= 4;
		if (sigismember(set, i+4)) x |= 8;
		*buffer++ = (x < 10 ? '0' : 'a' - 10) + x;
	} while (i >= 4);

	*buffer++ = '\n';
	*buffer = 0;
	return buffer;
}

static void collect_sigign_sigcatch(struct task_struct *p, sigset_t *ign,
				    sigset_t *catch)
{
	struct k_sigaction *k;
	int i;

	k = p->sighand->action;
	for (i = 1; i <= _NSIG; ++i, ++k) {
		if (k->sa.sa_handler == SIG_IGN)
			sigaddset(ign, i);
		else if (k->sa.sa_handler != SIG_DFL)
			sigaddset(catch, i);
	}
}

static inline char *task_sig(struct task_struct *p, char *buffer)
{
	unsigned long flags;
	sigset_t pending, shpending, blocked, ignored, caught;
	int num_threads = 0;
	unsigned long qsize = 0;
	unsigned long qlim = 0;

	sigemptyset(&pending);
	sigemptyset(&shpending);
	sigemptyset(&blocked);
	sigemptyset(&ignored);
	sigemptyset(&caught);

	rcu_read_lock();
	if (lock_task_sighand(p, &flags)) {
		pending = p->pending.signal;
		shpending = p->signal->shared_pending.signal;
		blocked = p->blocked;
		collect_sigign_sigcatch(p, &ignored, &caught);
		num_threads = atomic_read(&p->signal->count);
		qsize = atomic_read(&p->user->sigpending);
		qlim = p->signal->rlim[RLIMIT_SIGPENDING].rlim_cur;
		unlock_task_sighand(p, &flags);
	}
	rcu_read_unlock();

	buffer += sprintf(buffer, "Threads:\t%d\n", num_threads);
	buffer += sprintf(buffer, "SigQ:\t%lu/%lu\n", qsize, qlim);

	/* render them all */
	buffer = render_sigset_t("SigPnd:\t", &pending, buffer);
	buffer = render_sigset_t("ShdPnd:\t", &shpending, buffer);
	buffer = render_sigset_t("SigBlk:\t", &blocked, buffer);
	buffer = render_sigset_t("SigIgn:\t", &ignored, buffer);
	buffer = render_sigset_t("SigCgt:\t", &caught, buffer);

	return buffer;
}

static inline char *task_cap(struct task_struct *p, char *buffer)
{
    return buffer + sprintf(buffer, "CapInh:\t%016x\n"
			    "CapPrm:\t%016x\n"
			    "CapEff:\t%016x\n",
			    cap_t(p->cap_inheritable),
			    cap_t(p->cap_permitted),
			    cap_t(p->cap_effective));
}

static inline char *task_context_switch_counts(struct task_struct *p,
						char *buffer)
{
	return buffer + sprintf(buffer, "voluntary_ctxt_switches:\t%lu\n"
			    "nonvoluntary_ctxt_switches:\t%lu\n",
			    p->nvcsw,
			    p->nivcsw);
}

int proc_pid_status(struct task_struct *task, char *buffer)
{
	char *orig = buffer;
	struct mm_struct *mm = get_task_mm(task);

	buffer = task_name(task, buffer);
	buffer = task_state(task, buffer);

	if (mm) {
		buffer = task_mem(mm, buffer);
		mmput(mm);
	}
	buffer = task_sig(task, buffer);
	buffer = task_cap(task, buffer);
	buffer = cpuset_task_status_allowed(task, buffer);
#if defined(CONFIG_S390)
	buffer = task_show_regs(task, buffer);
#endif
	buffer = task_context_switch_counts(task, buffer);
	return buffer - orig;
}

/*
 * Use precise platform statistics if available:
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING
static cputime_t task_utime(struct task_struct *p)
{
	return p->utime;
}

static cputime_t task_stime(struct task_struct *p)
{
	return p->stime;
}
#else
static cputime_t task_utime(struct task_struct *p)
{
	clock_t utime = cputime_to_clock_t(p->utime),
		total = utime + cputime_to_clock_t(p->stime);
	u64 temp;

	/*
	 * Use CFS's precise accounting:
	 */
	temp = (u64)nsec_to_clock_t(p->se.sum_exec_runtime);

	if (total) {
		temp *= utime;
		do_div(temp, total);
	}
	utime = (clock_t)temp;

	p->prev_utime = max(p->prev_utime, clock_t_to_cputime(utime));
	return p->prev_utime;
}

static cputime_t task_stime(struct task_struct *p)
{
	clock_t stime;

	/*
	 * Use CFS's precise accounting. (we subtract utime from
	 * the total, to make sure the total observed by userspace
	 * grows monotonically - apps rely on that):
	 */
	stime = nsec_to_clock_t(p->se.sum_exec_runtime) -
			cputime_to_clock_t(task_utime(p));

	if (stime >= 0)
		p->prev_stime = max(p->prev_stime, clock_t_to_cputime(stime));

	return p->prev_stime;
}
#endif

static cputime_t task_gtime(struct task_struct *p)
{
	return p->gtime;
}

static int do_task_stat(struct task_struct *task, char *buffer, int whole)
{
	unsigned long vsize, eip, esp, wchan = ~0UL;
	long priority, nice;
	int tty_pgrp = -1, tty_nr = 0;
	sigset_t sigign, sigcatch;
	char state;
	int res;
	pid_t ppid = 0, pgid = -1, sid = -1;
	int num_threads = 0;
	struct mm_struct *mm;
	unsigned long long start_time;
	unsigned long cmin_flt = 0, cmaj_flt = 0;
	unsigned long  min_flt = 0,  maj_flt = 0;
	cputime_t cutime, cstime, utime, stime;
	cputime_t cgtime, gtime;
	unsigned long rsslim = 0;
	char tcomm[sizeof(task->comm)];
	unsigned long flags;
	struct pid_namespace *ns;

	ns = current->nsproxy->pid_ns;

	state = *get_task_state(task);
	vsize = eip = esp = 0;
	mm = get_task_mm(task);
	if (mm) {
		vsize = task_vsize(mm);
		eip = KSTK_EIP(task);
		esp = KSTK_ESP(task);
	}

	get_task_comm(tcomm, task);

	sigemptyset(&sigign);
	sigemptyset(&sigcatch);
	cutime = cstime = utime = stime = cputime_zero;
	cgtime = gtime = cputime_zero;

	rcu_read_lock();
	if (lock_task_sighand(task, &flags)) {
		struct signal_struct *sig = task->signal;

		if (sig->tty) {
			tty_pgrp = pid_nr_ns(sig->tty->pgrp, ns);
			tty_nr = new_encode_dev(tty_devnum(sig->tty));
		}

		num_threads = atomic_read(&sig->count);
		collect_sigign_sigcatch(task, &sigign, &sigcatch);

		cmin_flt = sig->cmin_flt;
		cmaj_flt = sig->cmaj_flt;
		cutime = sig->cutime;
		cstime = sig->cstime;
		cgtime = sig->cgtime;
		rsslim = sig->rlim[RLIMIT_RSS].rlim_cur;

		/* add up live thread stats at the group level */
		if (whole) {
			struct task_struct *t = task;
			do {
				min_flt += t->min_flt;
				maj_flt += t->maj_flt;
				utime = cputime_add(utime, task_utime(t));
				stime = cputime_add(stime, task_stime(t));
				gtime = cputime_add(gtime, task_gtime(t));
				t = next_thread(t);
			} while (t != task);

			min_flt += sig->min_flt;
			maj_flt += sig->maj_flt;
			utime = cputime_add(utime, sig->utime);
			stime = cputime_add(stime, sig->stime);
			gtime = cputime_add(gtime, sig->gtime);
		}

		sid = task_session_nr_ns(task, ns);
		ppid = task_tgid_nr_ns(task->real_parent, ns);
		pgid = task_pgrp_nr_ns(task, ns);

		unlock_task_sighand(task, &flags);
	}
	rcu_read_unlock();

	if (!whole || num_threads < 2)
		wchan = get_wchan(task);
	if (!whole) {
		min_flt = task->min_flt;
		maj_flt = task->maj_flt;
		utime = task_utime(task);
		stime = task_stime(task);
		gtime = task_gtime(task);
	}

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = task_prio(task);
	nice = task_nice(task);

	/* Temporary variable needed for gcc-2.96 */
	/* convert timespec -> nsec*/
	start_time =
		(unsigned long long)task->real_start_time.tv_sec * NSEC_PER_SEC
				+ task->real_start_time.tv_nsec;
	/* convert nsec -> ticks */
	start_time = nsec_to_clock_t(start_time);

	res = sprintf(buffer, "%d (%s) %c %d %d %d %d %d %u %lu \
%lu %lu %lu %lu %lu %ld %ld %ld %ld %d 0 %llu %lu %ld %lu %lu %lu %lu %lu \
%lu %lu %lu %lu %lu %lu %lu %lu %d %d %u %u %llu %lu %ld\n",
		task_pid_nr_ns(task, ns),
		tcomm,
		state,
		ppid,
		pgid,
		sid,
		tty_nr,
		tty_pgrp,
		task->flags,
		min_flt,
		cmin_flt,
		maj_flt,
		cmaj_flt,
		cputime_to_clock_t(utime),
		cputime_to_clock_t(stime),
		cputime_to_clock_t(cutime),
		cputime_to_clock_t(cstime),
		priority,
		nice,
		num_threads,
		start_time,
		vsize,
		mm ? get_mm_rss(mm) : 0,
		rsslim,
		mm ? mm->start_code : 0,
		mm ? mm->end_code : 0,
		mm ? mm->start_stack : 0,
		esp,
		eip,
		/* The signal information here is obsolete.
		 * It must be decimal for Linux 2.0 compatibility.
		 * Use /proc/#/status for real-time signals.
		 */
		task->pending.signal.sig[0] & 0x7fffffffUL,
		task->blocked.sig[0] & 0x7fffffffUL,
		sigign      .sig[0] & 0x7fffffffUL,
		sigcatch    .sig[0] & 0x7fffffffUL,
		wchan,
		0UL,
		0UL,
		task->exit_signal,
		task_cpu(task),
		task->rt_priority,
		task->policy,
		(unsigned long long)delayacct_blkio_ticks(task),
		cputime_to_clock_t(gtime),
		cputime_to_clock_t(cgtime));
	if (mm)
		mmput(mm);
	return res;
}

int proc_tid_stat(struct task_struct *task, char *buffer)
{
	return do_task_stat(task, buffer, 0);
}

int proc_tgid_stat(struct task_struct *task, char *buffer)
{
	return do_task_stat(task, buffer, 1);
}

int proc_pid_statm(struct task_struct *task, char *buffer)
{
	int size = 0, resident = 0, shared = 0, text = 0, lib = 0, data = 0;
	struct mm_struct *mm = get_task_mm(task);

	if (mm) {
		size = task_statm(mm, &shared, &text, &data, &resident);
		mmput(mm);
	}

	return sprintf(buffer, "%d %d %d %d %d %d %d\n",
		       size, resident, shared, text, lib, data, 0);
}
