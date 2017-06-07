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
 *			<alan@lxorguk.ukuu.org.uk>
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
#include <linux/sched/mm.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/sched/cputime.h>
#include <linux/proc_fs.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/signal.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/times.h>
#include <linux/cpuset.h>
#include <linux/rcupdate.h>
#include <linux/delayacct.h>
#include <linux/seq_file.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/tracehook.h>
#include <linux/string_helpers.h>
#include <linux/user_namespace.h>
#include <linux/fs_struct.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include "internal.h"

static inline void task_name(struct seq_file *m, struct task_struct *p)
{
	char *buf;
	size_t size;
	char tcomm[sizeof(p->comm)];
	int ret;

	get_task_comm(tcomm, p);

	seq_puts(m, "Name:\t");

	size = seq_get_buf(m, &buf);
	ret = string_escape_str(tcomm, buf, size, ESCAPE_SPACE | ESCAPE_SPECIAL, "\n\\");
	seq_commit(m, ret < size ? ret : -1);

	seq_putc(m, '\n');
}

/*
 * The task state array is a strange "bitmap" of
 * reasons to sleep. Thus "running" is zero, and
 * you can test for combinations of others with
 * simple bit tests.
 */
static const char * const task_state_array[] = {
	"R (running)",		/*   0 */
	"S (sleeping)",		/*   1 */
	"D (disk sleep)",	/*   2 */
	"T (stopped)",		/*   4 */
	"t (tracing stop)",	/*   8 */
	"X (dead)",		/*  16 */
	"Z (zombie)",		/*  32 */
};

static inline const char *get_task_state(struct task_struct *tsk)
{
	unsigned int state = (tsk->state | tsk->exit_state) & TASK_REPORT;

	/*
	 * Parked tasks do not run; they sit in __kthread_parkme().
	 * Without this check, we would report them as running, which is
	 * clearly wrong, so we report them as sleeping instead.
	 */
	if (tsk->state == TASK_PARKED)
		state = TASK_INTERRUPTIBLE;

	BUILD_BUG_ON(1 + ilog2(TASK_REPORT) != ARRAY_SIZE(task_state_array)-1);

	return task_state_array[fls(state)];
}

static inline int get_task_umask(struct task_struct *tsk)
{
	struct fs_struct *fs;
	int umask = -ENOENT;

	task_lock(tsk);
	fs = tsk->fs;
	if (fs)
		umask = fs->umask;
	task_unlock(tsk);
	return umask;
}

static inline void task_state(struct seq_file *m, struct pid_namespace *ns,
				struct pid *pid, struct task_struct *p)
{
	struct user_namespace *user_ns = seq_user_ns(m);
	struct group_info *group_info;
	int g, umask;
	struct task_struct *tracer;
	const struct cred *cred;
	pid_t ppid, tpid = 0, tgid, ngid;
	unsigned int max_fds = 0;

	rcu_read_lock();
	ppid = pid_alive(p) ?
		task_tgid_nr_ns(rcu_dereference(p->real_parent), ns) : 0;

	tracer = ptrace_parent(p);
	if (tracer)
		tpid = task_pid_nr_ns(tracer, ns);

	tgid = task_tgid_nr_ns(p, ns);
	ngid = task_numa_group_id(p);
	cred = get_task_cred(p);

	umask = get_task_umask(p);
	if (umask >= 0)
		seq_printf(m, "Umask:\t%#04o\n", umask);

	task_lock(p);
	if (p->files)
		max_fds = files_fdtable(p->files)->max_fds;
	task_unlock(p);
	rcu_read_unlock();

	seq_printf(m, "State:\t%s", get_task_state(p));

	seq_put_decimal_ull(m, "\nTgid:\t", tgid);
	seq_put_decimal_ull(m, "\nNgid:\t", ngid);
	seq_put_decimal_ull(m, "\nPid:\t", pid_nr_ns(pid, ns));
	seq_put_decimal_ull(m, "\nPPid:\t", ppid);
	seq_put_decimal_ull(m, "\nTracerPid:\t", tpid);
	seq_put_decimal_ull(m, "\nUid:\t", from_kuid_munged(user_ns, cred->uid));
	seq_put_decimal_ull(m, "\t", from_kuid_munged(user_ns, cred->euid));
	seq_put_decimal_ull(m, "\t", from_kuid_munged(user_ns, cred->suid));
	seq_put_decimal_ull(m, "\t", from_kuid_munged(user_ns, cred->fsuid));
	seq_put_decimal_ull(m, "\nGid:\t", from_kgid_munged(user_ns, cred->gid));
	seq_put_decimal_ull(m, "\t", from_kgid_munged(user_ns, cred->egid));
	seq_put_decimal_ull(m, "\t", from_kgid_munged(user_ns, cred->sgid));
	seq_put_decimal_ull(m, "\t", from_kgid_munged(user_ns, cred->fsgid));
	seq_put_decimal_ull(m, "\nFDSize:\t", max_fds);

	seq_puts(m, "\nGroups:\t");
	group_info = cred->group_info;
	for (g = 0; g < group_info->ngroups; g++)
		seq_put_decimal_ull(m, g ? " " : "",
				from_kgid_munged(user_ns, group_info->gid[g]));
	put_cred(cred);
	/* Trailing space shouldn't have been added in the first place. */
	seq_putc(m, ' ');

#ifdef CONFIG_PID_NS
	seq_puts(m, "\nNStgid:");
	for (g = ns->level; g <= pid->level; g++)
		seq_put_decimal_ull(m, "\t", task_tgid_nr_ns(p, pid->numbers[g].ns));
	seq_puts(m, "\nNSpid:");
	for (g = ns->level; g <= pid->level; g++)
		seq_put_decimal_ull(m, "\t", task_pid_nr_ns(p, pid->numbers[g].ns));
	seq_puts(m, "\nNSpgid:");
	for (g = ns->level; g <= pid->level; g++)
		seq_put_decimal_ull(m, "\t", task_pgrp_nr_ns(p, pid->numbers[g].ns));
	seq_puts(m, "\nNSsid:");
	for (g = ns->level; g <= pid->level; g++)
		seq_put_decimal_ull(m, "\t", task_session_nr_ns(p, pid->numbers[g].ns));
#endif
	seq_putc(m, '\n');
}

void render_sigset_t(struct seq_file *m, const char *header,
				sigset_t *set)
{
	int i;

	seq_puts(m, header);

	i = _NSIG;
	do {
		int x = 0;

		i -= 4;
		if (sigismember(set, i+1)) x |= 1;
		if (sigismember(set, i+2)) x |= 2;
		if (sigismember(set, i+3)) x |= 4;
		if (sigismember(set, i+4)) x |= 8;
		seq_putc(m, hex_asc[x]);
	} while (i >= 4);

	seq_putc(m, '\n');
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

static inline void task_sig(struct seq_file *m, struct task_struct *p)
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

	if (lock_task_sighand(p, &flags)) {
		pending = p->pending.signal;
		shpending = p->signal->shared_pending.signal;
		blocked = p->blocked;
		collect_sigign_sigcatch(p, &ignored, &caught);
		num_threads = get_nr_threads(p);
		rcu_read_lock();  /* FIXME: is this correct? */
		qsize = atomic_read(&__task_cred(p)->user->sigpending);
		rcu_read_unlock();
		qlim = task_rlimit(p, RLIMIT_SIGPENDING);
		unlock_task_sighand(p, &flags);
	}

	seq_put_decimal_ull(m, "Threads:\t", num_threads);
	seq_put_decimal_ull(m, "\nSigQ:\t", qsize);
	seq_put_decimal_ull(m, "/", qlim);

	/* render them all */
	render_sigset_t(m, "\nSigPnd:\t", &pending);
	render_sigset_t(m, "ShdPnd:\t", &shpending);
	render_sigset_t(m, "SigBlk:\t", &blocked);
	render_sigset_t(m, "SigIgn:\t", &ignored);
	render_sigset_t(m, "SigCgt:\t", &caught);
}

static void render_cap_t(struct seq_file *m, const char *header,
			kernel_cap_t *a)
{
	unsigned __capi;

	seq_puts(m, header);
	CAP_FOR_EACH_U32(__capi) {
		seq_printf(m, "%08x",
			   a->cap[CAP_LAST_U32 - __capi]);
	}
	seq_putc(m, '\n');
}

static inline void task_cap(struct seq_file *m, struct task_struct *p)
{
	const struct cred *cred;
	kernel_cap_t cap_inheritable, cap_permitted, cap_effective,
			cap_bset, cap_ambient;

	rcu_read_lock();
	cred = __task_cred(p);
	cap_inheritable	= cred->cap_inheritable;
	cap_permitted	= cred->cap_permitted;
	cap_effective	= cred->cap_effective;
	cap_bset	= cred->cap_bset;
	cap_ambient	= cred->cap_ambient;
	rcu_read_unlock();

	render_cap_t(m, "CapInh:\t", &cap_inheritable);
	render_cap_t(m, "CapPrm:\t", &cap_permitted);
	render_cap_t(m, "CapEff:\t", &cap_effective);
	render_cap_t(m, "CapBnd:\t", &cap_bset);
	render_cap_t(m, "CapAmb:\t", &cap_ambient);
}

static inline void task_seccomp(struct seq_file *m, struct task_struct *p)
{
	seq_put_decimal_ull(m, "NoNewPrivs:\t", task_no_new_privs(p));
#ifdef CONFIG_SECCOMP
	seq_put_decimal_ull(m, "\nSeccomp:\t", p->seccomp.mode);
#endif
	seq_putc(m, '\n');
}

static inline void task_context_switch_counts(struct seq_file *m,
						struct task_struct *p)
{
	seq_put_decimal_ull(m, "voluntary_ctxt_switches:\t", p->nvcsw);
	seq_put_decimal_ull(m, "\nnonvoluntary_ctxt_switches:\t", p->nivcsw);
	seq_putc(m, '\n');
}

static void task_cpus_allowed(struct seq_file *m, struct task_struct *task)
{
	seq_printf(m, "Cpus_allowed:\t%*pb\n",
		   cpumask_pr_args(&task->cpus_allowed));
	seq_printf(m, "Cpus_allowed_list:\t%*pbl\n",
		   cpumask_pr_args(&task->cpus_allowed));
}

int proc_pid_status(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task)
{
	struct mm_struct *mm = get_task_mm(task);

	task_name(m, task);
	task_state(m, ns, pid, task);

	if (mm) {
		task_mem(m, mm);
		mmput(mm);
	}
	task_sig(m, task);
	task_cap(m, task);
	task_seccomp(m, task);
	task_cpus_allowed(m, task);
	cpuset_task_status_allowed(m, task);
	task_context_switch_counts(m, task);
	return 0;
}

static int do_task_stat(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task, int whole)
{
	unsigned long vsize, eip, esp, wchan = 0;
	int priority, nice;
	int tty_pgrp = -1, tty_nr = 0;
	sigset_t sigign, sigcatch;
	char state;
	pid_t ppid = 0, pgid = -1, sid = -1;
	int num_threads = 0;
	int permitted;
	struct mm_struct *mm;
	unsigned long long start_time;
	unsigned long cmin_flt = 0, cmaj_flt = 0;
	unsigned long  min_flt = 0,  maj_flt = 0;
	u64 cutime, cstime, utime, stime;
	u64 cgtime, gtime;
	unsigned long rsslim = 0;
	char tcomm[sizeof(task->comm)];
	unsigned long flags;

	state = *get_task_state(task);
	vsize = eip = esp = 0;
	permitted = ptrace_may_access(task, PTRACE_MODE_READ_FSCREDS | PTRACE_MODE_NOAUDIT);
	mm = get_task_mm(task);
	if (mm) {
		vsize = task_vsize(mm);
		/*
		 * esp and eip are intentionally zeroed out.  There is no
		 * non-racy way to read them without freezing the task.
		 * Programs that need reliable values can use ptrace(2).
		 */
	}

	get_task_comm(tcomm, task);

	sigemptyset(&sigign);
	sigemptyset(&sigcatch);
	cutime = cstime = utime = stime = 0;
	cgtime = gtime = 0;

	if (lock_task_sighand(task, &flags)) {
		struct signal_struct *sig = task->signal;

		if (sig->tty) {
			struct pid *pgrp = tty_get_pgrp(sig->tty);
			tty_pgrp = pid_nr_ns(pgrp, ns);
			put_pid(pgrp);
			tty_nr = new_encode_dev(tty_devnum(sig->tty));
		}

		num_threads = get_nr_threads(task);
		collect_sigign_sigcatch(task, &sigign, &sigcatch);

		cmin_flt = sig->cmin_flt;
		cmaj_flt = sig->cmaj_flt;
		cutime = sig->cutime;
		cstime = sig->cstime;
		cgtime = sig->cgtime;
		rsslim = ACCESS_ONCE(sig->rlim[RLIMIT_RSS].rlim_cur);

		/* add up live thread stats at the group level */
		if (whole) {
			struct task_struct *t = task;
			do {
				min_flt += t->min_flt;
				maj_flt += t->maj_flt;
				gtime += task_gtime(t);
			} while_each_thread(task, t);

			min_flt += sig->min_flt;
			maj_flt += sig->maj_flt;
			thread_group_cputime_adjusted(task, &utime, &stime);
			gtime += sig->gtime;
		}

		sid = task_session_nr_ns(task, ns);
		ppid = task_tgid_nr_ns(task->real_parent, ns);
		pgid = task_pgrp_nr_ns(task, ns);

		unlock_task_sighand(task, &flags);
	}

	if (permitted && (!whole || num_threads < 2))
		wchan = get_wchan(task);
	if (!whole) {
		min_flt = task->min_flt;
		maj_flt = task->maj_flt;
		task_cputime_adjusted(task, &utime, &stime);
		gtime = task_gtime(task);
	}

	/* scale priority and nice values from timeslices to -20..20 */
	/* to make it look like a "normal" Unix priority/nice value  */
	priority = task_prio(task);
	nice = task_nice(task);

	/* convert nsec -> ticks */
	start_time = nsec_to_clock_t(task->real_start_time);

	seq_printf(m, "%d (%s) %c", pid_nr_ns(pid, ns), tcomm, state);
	seq_put_decimal_ll(m, " ", ppid);
	seq_put_decimal_ll(m, " ", pgid);
	seq_put_decimal_ll(m, " ", sid);
	seq_put_decimal_ll(m, " ", tty_nr);
	seq_put_decimal_ll(m, " ", tty_pgrp);
	seq_put_decimal_ull(m, " ", task->flags);
	seq_put_decimal_ull(m, " ", min_flt);
	seq_put_decimal_ull(m, " ", cmin_flt);
	seq_put_decimal_ull(m, " ", maj_flt);
	seq_put_decimal_ull(m, " ", cmaj_flt);
	seq_put_decimal_ull(m, " ", nsec_to_clock_t(utime));
	seq_put_decimal_ull(m, " ", nsec_to_clock_t(stime));
	seq_put_decimal_ll(m, " ", nsec_to_clock_t(cutime));
	seq_put_decimal_ll(m, " ", nsec_to_clock_t(cstime));
	seq_put_decimal_ll(m, " ", priority);
	seq_put_decimal_ll(m, " ", nice);
	seq_put_decimal_ll(m, " ", num_threads);
	seq_put_decimal_ull(m, " ", 0);
	seq_put_decimal_ull(m, " ", start_time);
	seq_put_decimal_ull(m, " ", vsize);
	seq_put_decimal_ull(m, " ", mm ? get_mm_rss(mm) : 0);
	seq_put_decimal_ull(m, " ", rsslim);
	seq_put_decimal_ull(m, " ", mm ? (permitted ? mm->start_code : 1) : 0);
	seq_put_decimal_ull(m, " ", mm ? (permitted ? mm->end_code : 1) : 0);
	seq_put_decimal_ull(m, " ", (permitted && mm) ? mm->start_stack : 0);
	seq_put_decimal_ull(m, " ", esp);
	seq_put_decimal_ull(m, " ", eip);
	/* The signal information here is obsolete.
	 * It must be decimal for Linux 2.0 compatibility.
	 * Use /proc/#/status for real-time signals.
	 */
	seq_put_decimal_ull(m, " ", task->pending.signal.sig[0] & 0x7fffffffUL);
	seq_put_decimal_ull(m, " ", task->blocked.sig[0] & 0x7fffffffUL);
	seq_put_decimal_ull(m, " ", sigign.sig[0] & 0x7fffffffUL);
	seq_put_decimal_ull(m, " ", sigcatch.sig[0] & 0x7fffffffUL);

	/*
	 * We used to output the absolute kernel address, but that's an
	 * information leak - so instead we show a 0/1 flag here, to signal
	 * to user-space whether there's a wchan field in /proc/PID/wchan.
	 *
	 * This works with older implementations of procps as well.
	 */
	if (wchan)
		seq_puts(m, " 1");
	else
		seq_puts(m, " 0");

	seq_put_decimal_ull(m, " ", 0);
	seq_put_decimal_ull(m, " ", 0);
	seq_put_decimal_ll(m, " ", task->exit_signal);
	seq_put_decimal_ll(m, " ", task_cpu(task));
	seq_put_decimal_ull(m, " ", task->rt_priority);
	seq_put_decimal_ull(m, " ", task->policy);
	seq_put_decimal_ull(m, " ", delayacct_blkio_ticks(task));
	seq_put_decimal_ull(m, " ", nsec_to_clock_t(gtime));
	seq_put_decimal_ll(m, " ", nsec_to_clock_t(cgtime));

	if (mm && permitted) {
		seq_put_decimal_ull(m, " ", mm->start_data);
		seq_put_decimal_ull(m, " ", mm->end_data);
		seq_put_decimal_ull(m, " ", mm->start_brk);
		seq_put_decimal_ull(m, " ", mm->arg_start);
		seq_put_decimal_ull(m, " ", mm->arg_end);
		seq_put_decimal_ull(m, " ", mm->env_start);
		seq_put_decimal_ull(m, " ", mm->env_end);
	} else
		seq_puts(m, " 0 0 0 0 0 0 0");

	if (permitted)
		seq_put_decimal_ll(m, " ", task->exit_code);
	else
		seq_puts(m, " 0");

	seq_putc(m, '\n');
	if (mm)
		mmput(mm);
	return 0;
}

int proc_tid_stat(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task)
{
	return do_task_stat(m, ns, pid, task, 0);
}

int proc_tgid_stat(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task)
{
	return do_task_stat(m, ns, pid, task, 1);
}

int proc_pid_statm(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task)
{
	unsigned long size = 0, resident = 0, shared = 0, text = 0, data = 0;
	struct mm_struct *mm = get_task_mm(task);

	if (mm) {
		size = task_statm(mm, &shared, &text, &data, &resident);
		mmput(mm);
	}
	/*
	 * For quick read, open code by putting numbers directly
	 * expected format is
	 * seq_printf(m, "%lu %lu %lu %lu 0 %lu 0\n",
	 *               size, resident, shared, text, data);
	 */
	seq_put_decimal_ull(m, "", size);
	seq_put_decimal_ull(m, " ", resident);
	seq_put_decimal_ull(m, " ", shared);
	seq_put_decimal_ull(m, " ", text);
	seq_put_decimal_ull(m, " ", 0);
	seq_put_decimal_ull(m, " ", data);
	seq_put_decimal_ull(m, " ", 0);
	seq_putc(m, '\n');

	return 0;
}

#ifdef CONFIG_PROC_CHILDREN
static struct pid *
get_children_pid(struct inode *inode, struct pid *pid_prev, loff_t pos)
{
	struct task_struct *start, *task;
	struct pid *pid = NULL;

	read_lock(&tasklist_lock);

	start = pid_task(proc_pid(inode), PIDTYPE_PID);
	if (!start)
		goto out;

	/*
	 * Lets try to continue searching first, this gives
	 * us significant speedup on children-rich processes.
	 */
	if (pid_prev) {
		task = pid_task(pid_prev, PIDTYPE_PID);
		if (task && task->real_parent == start &&
		    !(list_empty(&task->sibling))) {
			if (list_is_last(&task->sibling, &start->children))
				goto out;
			task = list_first_entry(&task->sibling,
						struct task_struct, sibling);
			pid = get_pid(task_pid(task));
			goto out;
		}
	}

	/*
	 * Slow search case.
	 *
	 * We might miss some children here if children
	 * are exited while we were not holding the lock,
	 * but it was never promised to be accurate that
	 * much.
	 *
	 * "Just suppose that the parent sleeps, but N children
	 *  exit after we printed their tids. Now the slow paths
	 *  skips N extra children, we miss N tasks." (c)
	 *
	 * So one need to stop or freeze the leader and all
	 * its children to get a precise result.
	 */
	list_for_each_entry(task, &start->children, sibling) {
		if (pos-- == 0) {
			pid = get_pid(task_pid(task));
			break;
		}
	}

out:
	read_unlock(&tasklist_lock);
	return pid;
}

static int children_seq_show(struct seq_file *seq, void *v)
{
	struct inode *inode = seq->private;
	pid_t pid;

	pid = pid_nr_ns(v, inode->i_sb->s_fs_info);
	seq_printf(seq, "%d ", pid);

	return 0;
}

static void *children_seq_start(struct seq_file *seq, loff_t *pos)
{
	return get_children_pid(seq->private, NULL, *pos);
}

static void *children_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct pid *pid;

	pid = get_children_pid(seq->private, v, *pos + 1);
	put_pid(v);

	++*pos;
	return pid;
}

static void children_seq_stop(struct seq_file *seq, void *v)
{
	put_pid(v);
}

static const struct seq_operations children_seq_ops = {
	.start	= children_seq_start,
	.next	= children_seq_next,
	.stop	= children_seq_stop,
	.show	= children_seq_show,
};

static int children_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *m;
	int ret;

	ret = seq_open(file, &children_seq_ops);
	if (ret)
		return ret;

	m = file->private_data;
	m->private = inode;

	return ret;
}

int children_seq_release(struct inode *inode, struct file *file)
{
	seq_release(inode, file);
	return 0;
}

const struct file_operations proc_tid_children_operations = {
	.open    = children_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = children_seq_release,
};
#endif /* CONFIG_PROC_CHILDREN */
