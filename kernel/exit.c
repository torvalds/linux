/*
 *  linux/kernel/exit.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/iocontext.h>
#include <linux/key.h>
#include <linux/security.h>
#include <linux/cpu.h>
#include <linux/acct.h>
#include <linux/tsacct_kern.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/freezer.h>
#include <linux/binfmts.h>
#include <linux/nsproxy.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/profile.h>
#include <linux/mount.h>
#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/mempolicy.h>
#include <linux/taskstats_kern.h>
#include <linux/delayacct.h>
#include <linux/cgroup.h>
#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/posix-timers.h>
#include <linux/cn_proc.h>
#include <linux/mutex.h>
#include <linux/futex.h>
#include <linux/pipe_fs_i.h>
#include <linux/audit.h> /* for audit_free() */
#include <linux/resource.h>
#include <linux/blkdev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/tracehook.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>
#include <linux/perf_event.h>
#include <trace/events/sched.h>
#include <linux/hw_breakpoint.h>
#include <linux/oom.h>
#include <linux/writeback.h>
#include <linux/shm.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

static void exit_mm(struct task_struct *tsk);

static void __unhash_process(struct task_struct *p, bool group_dead)
{
	nr_threads--;
	detach_pid(p, PIDTYPE_PID);
	if (group_dead) {
		detach_pid(p, PIDTYPE_PGID);
		detach_pid(p, PIDTYPE_SID);

		list_del_rcu(&p->tasks);
		list_del_init(&p->sibling);
		__this_cpu_dec(process_counts);
	}
	list_del_rcu(&p->thread_group);
	list_del_rcu(&p->thread_node);
}

/*
 * This function expects the tasklist_lock write-locked.
 */
static void __exit_signal(struct task_struct *tsk)
{
	struct signal_struct *sig = tsk->signal;
	bool group_dead = thread_group_leader(tsk);
	struct sighand_struct *sighand;
	struct tty_struct *uninitialized_var(tty);
	cputime_t utime, stime;

	sighand = rcu_dereference_check(tsk->sighand,
					lockdep_tasklist_lock_is_held());
	spin_lock(&sighand->siglock);

	posix_cpu_timers_exit(tsk);
	if (group_dead) {
		posix_cpu_timers_exit_group(tsk);
		tty = sig->tty;
		sig->tty = NULL;
	} else {
		/*
		 * This can only happen if the caller is de_thread().
		 * FIXME: this is the temporary hack, we should teach
		 * posix-cpu-timers to handle this case correctly.
		 */
		if (unlikely(has_group_leader_pid(tsk)))
			posix_cpu_timers_exit_group(tsk);

		/*
		 * If there is any task waiting for the group exit
		 * then notify it:
		 */
		if (sig->notify_count > 0 && !--sig->notify_count)
			wake_up_process(sig->group_exit_task);

		if (tsk == sig->curr_target)
			sig->curr_target = next_thread(tsk);
	}

	/*
	 * Accumulate here the counters for all threads as they die. We could
	 * skip the group leader because it is the last user of signal_struct,
	 * but we want to avoid the race with thread_group_cputime() which can
	 * see the empty ->thread_head list.
	 */
	task_cputime(tsk, &utime, &stime);
	write_seqlock(&sig->stats_lock);
	sig->utime += utime;
	sig->stime += stime;
	sig->gtime += task_gtime(tsk);
	sig->min_flt += tsk->min_flt;
	sig->maj_flt += tsk->maj_flt;
	sig->nvcsw += tsk->nvcsw;
	sig->nivcsw += tsk->nivcsw;
	sig->inblock += task_io_get_inblock(tsk);
	sig->oublock += task_io_get_oublock(tsk);
	task_io_accounting_add(&sig->ioac, &tsk->ioac);
	sig->sum_sched_runtime += tsk->se.sum_exec_runtime;
	sig->nr_threads--;
	__unhash_process(tsk, group_dead);
	write_sequnlock(&sig->stats_lock);

	/*
	 * Do this under ->siglock, we can race with another thread
	 * doing sigqueue_free() if we have SIGQUEUE_PREALLOC signals.
	 */
	flush_sigqueue(&tsk->pending);
	tsk->sighand = NULL;
	spin_unlock(&sighand->siglock);

	__cleanup_sighand(sighand);
	clear_tsk_thread_flag(tsk, TIF_SIGPENDING);
	if (group_dead) {
		flush_sigqueue(&sig->shared_pending);
		tty_kref_put(tty);
	}
}

static void delayed_put_task_struct(struct rcu_head *rhp)
{
	struct task_struct *tsk = container_of(rhp, struct task_struct, rcu);

	perf_event_delayed_put(tsk);
	trace_sched_process_free(tsk);
	put_task_struct(tsk);
}


void release_task(struct task_struct *p)
{
	struct task_struct *leader;
	int zap_leader;
repeat:
	/* don't need to get the RCU readlock here - the process is dead and
	 * can't be modifying its own credentials. But shut RCU-lockdep up */
	rcu_read_lock();
	atomic_dec(&__task_cred(p)->user->processes);
	rcu_read_unlock();

	proc_flush_task(p);

	write_lock_irq(&tasklist_lock);
	ptrace_release_task(p);
	__exit_signal(p);

	/*
	 * If we are the last non-leader member of the thread
	 * group, and the leader is zombie, then notify the
	 * group leader's parent process. (if it wants notification.)
	 */
	zap_leader = 0;
	leader = p->group_leader;
	if (leader != p && thread_group_empty(leader)
			&& leader->exit_state == EXIT_ZOMBIE) {
		/*
		 * If we were the last child thread and the leader has
		 * exited already, and the leader's parent ignores SIGCHLD,
		 * then we are the one who should release the leader.
		 */
		zap_leader = do_notify_parent(leader, leader->exit_signal);
		if (zap_leader)
			leader->exit_state = EXIT_DEAD;
	}

	write_unlock_irq(&tasklist_lock);
	release_thread(p);
	call_rcu(&p->rcu, delayed_put_task_struct);

	p = leader;
	if (unlikely(zap_leader))
		goto repeat;
}

/*
 * This checks not only the pgrp, but falls back on the pid if no
 * satisfactory pgrp is found. I dunno - gdb doesn't work correctly
 * without this...
 *
 * The caller must hold rcu lock or the tasklist lock.
 */
struct pid *session_of_pgrp(struct pid *pgrp)
{
	struct task_struct *p;
	struct pid *sid = NULL;

	p = pid_task(pgrp, PIDTYPE_PGID);
	if (p == NULL)
		p = pid_task(pgrp, PIDTYPE_PID);
	if (p != NULL)
		sid = task_session(p);

	return sid;
}

/*
 * Determine if a process group is "orphaned", according to the POSIX
 * definition in 2.2.2.52.  Orphaned process groups are not to be affected
 * by terminal-generated stop signals.  Newly orphaned process groups are
 * to receive a SIGHUP and a SIGCONT.
 *
 * "I ask you, have you ever known what it is to be an orphan?"
 */
static int will_become_orphaned_pgrp(struct pid *pgrp,
					struct task_struct *ignored_task)
{
	struct task_struct *p;

	do_each_pid_task(pgrp, PIDTYPE_PGID, p) {
		if ((p == ignored_task) ||
		    (p->exit_state && thread_group_empty(p)) ||
		    is_global_init(p->real_parent))
			continue;

		if (task_pgrp(p->real_parent) != pgrp &&
		    task_session(p->real_parent) == task_session(p))
			return 0;
	} while_each_pid_task(pgrp, PIDTYPE_PGID, p);

	return 1;
}

int is_current_pgrp_orphaned(void)
{
	int retval;

	read_lock(&tasklist_lock);
	retval = will_become_orphaned_pgrp(task_pgrp(current), NULL);
	read_unlock(&tasklist_lock);

	return retval;
}

static bool has_stopped_jobs(struct pid *pgrp)
{
	struct task_struct *p;

	do_each_pid_task(pgrp, PIDTYPE_PGID, p) {
		if (p->signal->flags & SIGNAL_STOP_STOPPED)
			return true;
	} while_each_pid_task(pgrp, PIDTYPE_PGID, p);

	return false;
}

/*
 * Check to see if any process groups have become orphaned as
 * a result of our exiting, and if they have any stopped jobs,
 * send them a SIGHUP and then a SIGCONT. (POSIX 3.2.2.2)
 */
static void
kill_orphaned_pgrp(struct task_struct *tsk, struct task_struct *parent)
{
	struct pid *pgrp = task_pgrp(tsk);
	struct task_struct *ignored_task = tsk;

	if (!parent)
		/* exit: our father is in a different pgrp than
		 * we are and we were the only connection outside.
		 */
		parent = tsk->real_parent;
	else
		/* reparent: our child is in a different pgrp than
		 * we are, and it was the only connection outside.
		 */
		ignored_task = NULL;

	if (task_pgrp(parent) != pgrp &&
	    task_session(parent) == task_session(tsk) &&
	    will_become_orphaned_pgrp(pgrp, ignored_task) &&
	    has_stopped_jobs(pgrp)) {
		__kill_pgrp_info(SIGHUP, SEND_SIG_PRIV, pgrp);
		__kill_pgrp_info(SIGCONT, SEND_SIG_PRIV, pgrp);
	}
}

#ifdef CONFIG_MEMCG
/*
 * A task is exiting.   If it owned this mm, find a new owner for the mm.
 */
void mm_update_next_owner(struct mm_struct *mm)
{
	struct task_struct *c, *g, *p = current;

retry:
	/*
	 * If the exiting or execing task is not the owner, it's
	 * someone else's problem.
	 */
	if (mm->owner != p)
		return;
	/*
	 * The current owner is exiting/execing and there are no other
	 * candidates.  Do not leave the mm pointing to a possibly
	 * freed task structure.
	 */
	if (atomic_read(&mm->mm_users) <= 1) {
		mm->owner = NULL;
		return;
	}

	read_lock(&tasklist_lock);
	/*
	 * Search in the children
	 */
	list_for_each_entry(c, &p->children, sibling) {
		if (c->mm == mm)
			goto assign_new_owner;
	}

	/*
	 * Search in the siblings
	 */
	list_for_each_entry(c, &p->real_parent->children, sibling) {
		if (c->mm == mm)
			goto assign_new_owner;
	}

	/*
	 * Search through everything else, we should not get here often.
	 */
	for_each_process(g) {
		if (g->flags & PF_KTHREAD)
			continue;
		for_each_thread(g, c) {
			if (c->mm == mm)
				goto assign_new_owner;
			if (c->mm)
				break;
		}
	}
	read_unlock(&tasklist_lock);
	/*
	 * We found no owner yet mm_users > 1: this implies that we are
	 * most likely racing with swapoff (try_to_unuse()) or /proc or
	 * ptrace or page migration (get_task_mm()).  Mark owner as NULL.
	 */
	mm->owner = NULL;
	return;

assign_new_owner:
	BUG_ON(c == p);
	get_task_struct(c);
	/*
	 * The task_lock protects c->mm from changing.
	 * We always want mm->owner->mm == mm
	 */
	task_lock(c);
	/*
	 * Delay read_unlock() till we have the task_lock()
	 * to ensure that c does not slip away underneath us
	 */
	read_unlock(&tasklist_lock);
	if (c->mm != mm) {
		task_unlock(c);
		put_task_struct(c);
		goto retry;
	}
	mm->owner = c;
	task_unlock(c);
	put_task_struct(c);
}
#endif /* CONFIG_MEMCG */

/*
 * Turn us into a lazy TLB process if we
 * aren't already..
 */
static void exit_mm(struct task_struct *tsk)
{
	struct mm_struct *mm = tsk->mm;
	struct core_state *core_state;

	mm_release(tsk, mm);
	if (!mm)
		return;
	sync_mm_rss(mm);
	/*
	 * Serialize with any possible pending coredump.
	 * We must hold mmap_sem around checking core_state
	 * and clearing tsk->mm.  The core-inducing thread
	 * will increment ->nr_threads for each thread in the
	 * group with ->mm != NULL.
	 */
	down_read(&mm->mmap_sem);
	core_state = mm->core_state;
	if (core_state) {
		struct core_thread self;

		up_read(&mm->mmap_sem);

		self.task = tsk;
		self.next = xchg(&core_state->dumper.next, &self);
		/*
		 * Implies mb(), the result of xchg() must be visible
		 * to core_state->dumper.
		 */
		if (atomic_dec_and_test(&core_state->nr_threads))
			complete(&core_state->startup);

		for (;;) {
			set_task_state(tsk, TASK_UNINTERRUPTIBLE);
			if (!self.task) /* see coredump_finish() */
				break;
			freezable_schedule();
		}
		__set_task_state(tsk, TASK_RUNNING);
		down_read(&mm->mmap_sem);
	}
	atomic_inc(&mm->mm_count);
	BUG_ON(mm != tsk->active_mm);
	/* more a memory barrier than a real lock */
	task_lock(tsk);
	tsk->mm = NULL;
	up_read(&mm->mmap_sem);
	enter_lazy_tlb(mm, current);
	task_unlock(tsk);
	mm_update_next_owner(mm);
	mmput(mm);
	clear_thread_flag(TIF_MEMDIE);
}

/*
 * When we die, we re-parent all our children, and try to:
 * 1. give them to another thread in our thread group, if such a member exists
 * 2. give it to the first ancestor process which prctl'd itself as a
 *    child_subreaper for its children (like a service manager)
 * 3. give it to the init process (PID 1) in our pid namespace
 */
static struct task_struct *find_new_reaper(struct task_struct *father)
	__releases(&tasklist_lock)
	__acquires(&tasklist_lock)
{
	struct pid_namespace *pid_ns = task_active_pid_ns(father);
	struct task_struct *thread;

	for_each_thread(father, thread) {
		if (thread->flags & PF_EXITING)
			continue;
		if (unlikely(pid_ns->child_reaper == father))
			pid_ns->child_reaper = thread;
		return thread;
	}

	if (unlikely(pid_ns->child_reaper == father)) {
		write_unlock_irq(&tasklist_lock);
		if (unlikely(pid_ns == &init_pid_ns)) {
			panic("Attempted to kill init! exitcode=0x%08x\n",
				father->signal->group_exit_code ?:
					father->exit_code);
		}

		zap_pid_ns_processes(pid_ns);
		write_lock_irq(&tasklist_lock);
	}

	if (father->signal->has_child_subreaper) {
		struct task_struct *reaper;

		/*
		 * Find the first ancestor marked as child_subreaper.
		 * Note that the code below checks same_thread_group(reaper,
		 * pid_ns->child_reaper).  This is what we need to DTRT in a
		 * PID namespace. However we still need the check above, see
		 * http://marc.info/?l=linux-kernel&m=131385460420380
		 */
		for (reaper = father;
		     reaper != &init_task;
		     reaper = reaper->real_parent) {
			if (same_thread_group(reaper, pid_ns->child_reaper))
				break;
			if (!reaper->signal->is_child_subreaper)
				continue;
			for_each_thread(reaper, thread) {
				if (!(thread->flags & PF_EXITING))
					return thread;
			}
		}
	}

	return pid_ns->child_reaper;
}

/*
* Any that need to be release_task'd are put on the @dead list.
 */
static void reparent_leader(struct task_struct *father, struct task_struct *p,
				struct list_head *dead)
{
	if (unlikely(p->exit_state == EXIT_DEAD))
		return;

	/* We don't want people slaying init. */
	p->exit_signal = SIGCHLD;

	/* If it has exited notify the new parent about this child's death. */
	if (!p->ptrace &&
	    p->exit_state == EXIT_ZOMBIE && thread_group_empty(p)) {
		if (do_notify_parent(p, p->exit_signal)) {
			p->exit_state = EXIT_DEAD;
			list_add(&p->ptrace_entry, dead);
		}
	}

	kill_orphaned_pgrp(p, father);
}

static void forget_original_parent(struct task_struct *father)
{
	struct task_struct *p, *t, *n, *reaper;
	LIST_HEAD(dead_children);

	write_lock_irq(&tasklist_lock);
	if (unlikely(!list_empty(&father->ptraced)))
		exit_ptrace(father, &dead_children);

	/* Can drop and reacquire tasklist_lock */
	reaper = find_new_reaper(father);
	list_for_each_entry(p, &father->children, sibling) {
		for_each_thread(p, t) {
			t->real_parent = reaper;
			BUG_ON((!t->ptrace) != (t->parent == father));
			if (likely(!t->ptrace))
				t->parent = t->real_parent;
			if (t->pdeath_signal)
				group_send_sig_info(t->pdeath_signal,
						    SEND_SIG_NOINFO, t);
		}
		/*
		 * If this is a threaded reparent there is no need to
		 * notify anyone anything has happened.
		 */
		if (!same_thread_group(reaper, father))
			reparent_leader(father, p, &dead_children);
	}
	list_splice_tail_init(&father->children, &reaper->children);
	write_unlock_irq(&tasklist_lock);

	list_for_each_entry_safe(p, n, &dead_children, ptrace_entry) {
		list_del_init(&p->ptrace_entry);
		release_task(p);
	}
}

/*
 * Send signals to all our closest relatives so that they know
 * to properly mourn us..
 */
static void exit_notify(struct task_struct *tsk, int group_dead)
{
	bool autoreap;

	/*
	 * This does two things:
	 *
	 * A.  Make init inherit all the child processes
	 * B.  Check to see if any process groups have become orphaned
	 *	as a result of our exiting, and if they have any stopped
	 *	jobs, send them a SIGHUP and then a SIGCONT.  (POSIX 3.2.2.2)
	 */
	forget_original_parent(tsk);

	write_lock_irq(&tasklist_lock);
	if (group_dead)
		kill_orphaned_pgrp(tsk->group_leader, NULL);

	if (unlikely(tsk->ptrace)) {
		int sig = thread_group_leader(tsk) &&
				thread_group_empty(tsk) &&
				!ptrace_reparented(tsk) ?
			tsk->exit_signal : SIGCHLD;
		autoreap = do_notify_parent(tsk, sig);
	} else if (thread_group_leader(tsk)) {
		autoreap = thread_group_empty(tsk) &&
			do_notify_parent(tsk, tsk->exit_signal);
	} else {
		autoreap = true;
	}

	tsk->exit_state = autoreap ? EXIT_DEAD : EXIT_ZOMBIE;

	/* mt-exec, de_thread() is waiting for group leader */
	if (unlikely(tsk->signal->notify_count < 0))
		wake_up_process(tsk->signal->group_exit_task);
	write_unlock_irq(&tasklist_lock);

	/* If the process is dead, release it - nobody will wait for it */
	if (autoreap)
		release_task(tsk);
}

#ifdef CONFIG_DEBUG_STACK_USAGE
static void check_stack_usage(void)
{
	static DEFINE_SPINLOCK(low_water_lock);
	static int lowest_to_date = THREAD_SIZE;
	unsigned long free;

	free = stack_not_used(current);

	if (free >= lowest_to_date)
		return;

	spin_lock(&low_water_lock);
	if (free < lowest_to_date) {
		pr_warn("%s (%d) used greatest stack depth: %lu bytes left\n",
			current->comm, task_pid_nr(current), free);
		lowest_to_date = free;
	}
	spin_unlock(&low_water_lock);
}
#else
static inline void check_stack_usage(void) {}
#endif

void do_exit(long code)
{
	struct task_struct *tsk = current;
	int group_dead;
	TASKS_RCU(int tasks_rcu_i);

	profile_task_exit(tsk);

	WARN_ON(blk_needs_flush_plug(tsk));

	if (unlikely(in_interrupt()))
		panic("Aiee, killing interrupt handler!");
	if (unlikely(!tsk->pid))
		panic("Attempted to kill the idle task!");

	/*
	 * If do_exit is called because this processes oopsed, it's possible
	 * that get_fs() was left as KERNEL_DS, so reset it to USER_DS before
	 * continuing. Amongst other possible reasons, this is to prevent
	 * mm_release()->clear_child_tid() from writing to a user-controlled
	 * kernel address.
	 */
	set_fs(USER_DS);

	ptrace_event(PTRACE_EVENT_EXIT, code);

	validate_creds_for_do_exit(tsk);

	/*
	 * We're taking recursive faults here in do_exit. Safest is to just
	 * leave this task alone and wait for reboot.
	 */
	if (unlikely(tsk->flags & PF_EXITING)) {
		pr_alert("Fixing recursive fault but reboot is needed!\n");
		/*
		 * We can do this unlocked here. The futex code uses
		 * this flag just to verify whether the pi state
		 * cleanup has been done or not. In the worst case it
		 * loops once more. We pretend that the cleanup was
		 * done as there is no way to return. Either the
		 * OWNER_DIED bit is set by now or we push the blocked
		 * task into the wait for ever nirwana as well.
		 */
		tsk->flags |= PF_EXITPIDONE;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}

	exit_signals(tsk);  /* sets PF_EXITING */
	/*
	 * tsk->flags are checked in the futex code to protect against
	 * an exiting task cleaning up the robust pi futexes.
	 */
	smp_mb();
	raw_spin_unlock_wait(&tsk->pi_lock);

	if (unlikely(in_atomic()))
		pr_info("note: %s[%d] exited with preempt_count %d\n",
			current->comm, task_pid_nr(current),
			preempt_count());

	acct_update_integrals(tsk);
	/* sync mm's RSS info before statistics gathering */
	if (tsk->mm)
		sync_mm_rss(tsk->mm);
	group_dead = atomic_dec_and_test(&tsk->signal->live);
	if (group_dead) {
		hrtimer_cancel(&tsk->signal->real_timer);
		exit_itimers(tsk->signal);
		if (tsk->mm)
			setmax_mm_hiwater_rss(&tsk->signal->maxrss, tsk->mm);
	}
	acct_collect(code, group_dead);
	if (group_dead)
		tty_audit_exit();
	audit_free(tsk);

	tsk->exit_code = code;
	taskstats_exit(tsk, group_dead);

	exit_mm(tsk);

	if (group_dead)
		acct_process();
	trace_sched_process_exit(tsk);

	exit_sem(tsk);
	exit_shm(tsk);
	exit_files(tsk);
	exit_fs(tsk);
	if (group_dead)
		disassociate_ctty(1);
	exit_task_namespaces(tsk);
	exit_task_work(tsk);
	exit_thread();

	/*
	 * Flush inherited counters to the parent - before the parent
	 * gets woken up by child-exit notifications.
	 *
	 * because of cgroup mode, must be called before cgroup_exit()
	 */
	perf_event_exit_task(tsk);

	cgroup_exit(tsk);

	module_put(task_thread_info(tsk)->exec_domain->module);

	/*
	 * FIXME: do that only when needed, using sched_exit tracepoint
	 */
	flush_ptrace_hw_breakpoint(tsk);

	TASKS_RCU(tasks_rcu_i = __srcu_read_lock(&tasks_rcu_exit_srcu));
	exit_notify(tsk, group_dead);
	proc_exit_connector(tsk);
#ifdef CONFIG_NUMA
	task_lock(tsk);
	mpol_put(tsk->mempolicy);
	tsk->mempolicy = NULL;
	task_unlock(tsk);
#endif
#ifdef CONFIG_FUTEX
	if (unlikely(current->pi_state_cache))
		kfree(current->pi_state_cache);
#endif
	/*
	 * Make sure we are holding no locks:
	 */
	debug_check_no_locks_held();
	/*
	 * We can do this unlocked here. The futex code uses this flag
	 * just to verify whether the pi state cleanup has been done
	 * or not. In the worst case it loops once more.
	 */
	tsk->flags |= PF_EXITPIDONE;

	if (tsk->io_context)
		exit_io_context(tsk);

	if (tsk->splice_pipe)
		free_pipe_info(tsk->splice_pipe);

	if (tsk->task_frag.page)
		put_page(tsk->task_frag.page);

	validate_creds_for_do_exit(tsk);

	check_stack_usage();
	preempt_disable();
	if (tsk->nr_dirtied)
		__this_cpu_add(dirty_throttle_leaks, tsk->nr_dirtied);
	exit_rcu();
	TASKS_RCU(__srcu_read_unlock(&tasks_rcu_exit_srcu, tasks_rcu_i));

	/*
	 * The setting of TASK_RUNNING by try_to_wake_up() may be delayed
	 * when the following two conditions become true.
	 *   - There is race condition of mmap_sem (It is acquired by
	 *     exit_mm()), and
	 *   - SMI occurs before setting TASK_RUNINNG.
	 *     (or hypervisor of virtual machine switches to other guest)
	 *  As a result, we may become TASK_RUNNING after becoming TASK_DEAD
	 *
	 * To avoid it, we have to wait for releasing tsk->pi_lock which
	 * is held by try_to_wake_up()
	 */
	smp_mb();
	raw_spin_unlock_wait(&tsk->pi_lock);

	/* causes final put_task_struct in finish_task_switch(). */
	tsk->state = TASK_DEAD;
	tsk->flags |= PF_NOFREEZE;	/* tell freezer to ignore us */
	schedule();
	BUG();
	/* Avoid "noreturn function does return".  */
	for (;;)
		cpu_relax();	/* For when BUG is null */
}
EXPORT_SYMBOL_GPL(do_exit);

void complete_and_exit(struct completion *comp, long code)
{
	if (comp)
		complete(comp);

	do_exit(code);
}
EXPORT_SYMBOL(complete_and_exit);

SYSCALL_DEFINE1(exit, int, error_code)
{
	do_exit((error_code&0xff)<<8);
}

/*
 * Take down every thread in the group.  This is called by fatal signals
 * as well as by sys_exit_group (below).
 */
void
do_group_exit(int exit_code)
{
	struct signal_struct *sig = current->signal;

	BUG_ON(exit_code & 0x80); /* core dumps don't get here */

	if (signal_group_exit(sig))
		exit_code = sig->group_exit_code;
	else if (!thread_group_empty(current)) {
		struct sighand_struct *const sighand = current->sighand;

		spin_lock_irq(&sighand->siglock);
		if (signal_group_exit(sig))
			/* Another thread got here before we took the lock.  */
			exit_code = sig->group_exit_code;
		else {
			sig->group_exit_code = exit_code;
			sig->flags = SIGNAL_GROUP_EXIT;
			zap_other_threads(current);
		}
		spin_unlock_irq(&sighand->siglock);
	}

	do_exit(exit_code);
	/* NOTREACHED */
}

/*
 * this kills every thread in the thread group. Note that any externally
 * wait4()-ing process will get the correct exit code - even if this
 * thread is not the thread group leader.
 */
SYSCALL_DEFINE1(exit_group, int, error_code)
{
	do_group_exit((error_code & 0xff) << 8);
	/* NOTREACHED */
	return 0;
}

struct wait_opts {
	enum pid_type		wo_type;
	int			wo_flags;
	struct pid		*wo_pid;

	struct siginfo __user	*wo_info;
	int __user		*wo_stat;
	struct rusage __user	*wo_rusage;

	wait_queue_t		child_wait;
	int			notask_error;
};

static inline
struct pid *task_pid_type(struct task_struct *task, enum pid_type type)
{
	if (type != PIDTYPE_PID)
		task = task->group_leader;
	return task->pids[type].pid;
}

static int eligible_pid(struct wait_opts *wo, struct task_struct *p)
{
	return	wo->wo_type == PIDTYPE_MAX ||
		task_pid_type(p, wo->wo_type) == wo->wo_pid;
}

static int eligible_child(struct wait_opts *wo, struct task_struct *p)
{
	if (!eligible_pid(wo, p))
		return 0;
	/* Wait for all children (clone and not) if __WALL is set;
	 * otherwise, wait for clone children *only* if __WCLONE is
	 * set; otherwise, wait for non-clone children *only*.  (Note:
	 * A "clone" child here is one that reports to its parent
	 * using a signal other than SIGCHLD.) */
	if (((p->exit_signal != SIGCHLD) ^ !!(wo->wo_flags & __WCLONE))
	    && !(wo->wo_flags & __WALL))
		return 0;

	return 1;
}

static int wait_noreap_copyout(struct wait_opts *wo, struct task_struct *p,
				pid_t pid, uid_t uid, int why, int status)
{
	struct siginfo __user *infop;
	int retval = wo->wo_rusage
		? getrusage(p, RUSAGE_BOTH, wo->wo_rusage) : 0;

	put_task_struct(p);
	infop = wo->wo_info;
	if (infop) {
		if (!retval)
			retval = put_user(SIGCHLD, &infop->si_signo);
		if (!retval)
			retval = put_user(0, &infop->si_errno);
		if (!retval)
			retval = put_user((short)why, &infop->si_code);
		if (!retval)
			retval = put_user(pid, &infop->si_pid);
		if (!retval)
			retval = put_user(uid, &infop->si_uid);
		if (!retval)
			retval = put_user(status, &infop->si_status);
	}
	if (!retval)
		retval = pid;
	return retval;
}

/*
 * Handle sys_wait4 work for one task in state EXIT_ZOMBIE.  We hold
 * read_lock(&tasklist_lock) on entry.  If we return zero, we still hold
 * the lock and this task is uninteresting.  If we return nonzero, we have
 * released the lock and the system call should return.
 */
static int wait_task_zombie(struct wait_opts *wo, struct task_struct *p)
{
	int state, retval, status;
	pid_t pid = task_pid_vnr(p);
	uid_t uid = from_kuid_munged(current_user_ns(), task_uid(p));
	struct siginfo __user *infop;

	if (!likely(wo->wo_flags & WEXITED))
		return 0;

	if (unlikely(wo->wo_flags & WNOWAIT)) {
		int exit_code = p->exit_code;
		int why;

		get_task_struct(p);
		read_unlock(&tasklist_lock);
		sched_annotate_sleep();

		if ((exit_code & 0x7f) == 0) {
			why = CLD_EXITED;
			status = exit_code >> 8;
		} else {
			why = (exit_code & 0x80) ? CLD_DUMPED : CLD_KILLED;
			status = exit_code & 0x7f;
		}
		return wait_noreap_copyout(wo, p, pid, uid, why, status);
	}
	/*
	 * Move the task's state to DEAD/TRACE, only one thread can do this.
	 */
	state = (ptrace_reparented(p) && thread_group_leader(p)) ?
		EXIT_TRACE : EXIT_DEAD;
	if (cmpxchg(&p->exit_state, EXIT_ZOMBIE, state) != EXIT_ZOMBIE)
		return 0;
	/*
	 * We own this thread, nobody else can reap it.
	 */
	read_unlock(&tasklist_lock);
	sched_annotate_sleep();

	/*
	 * Check thread_group_leader() to exclude the traced sub-threads.
	 */
	if (state == EXIT_DEAD && thread_group_leader(p)) {
		struct signal_struct *sig = p->signal;
		struct signal_struct *psig = current->signal;
		unsigned long maxrss;
		cputime_t tgutime, tgstime;

		/*
		 * The resource counters for the group leader are in its
		 * own task_struct.  Those for dead threads in the group
		 * are in its signal_struct, as are those for the child
		 * processes it has previously reaped.  All these
		 * accumulate in the parent's signal_struct c* fields.
		 *
		 * We don't bother to take a lock here to protect these
		 * p->signal fields because the whole thread group is dead
		 * and nobody can change them.
		 *
		 * psig->stats_lock also protects us from our sub-theads
		 * which can reap other children at the same time. Until
		 * we change k_getrusage()-like users to rely on this lock
		 * we have to take ->siglock as well.
		 *
		 * We use thread_group_cputime_adjusted() to get times for
		 * the thread group, which consolidates times for all threads
		 * in the group including the group leader.
		 */
		thread_group_cputime_adjusted(p, &tgutime, &tgstime);
		spin_lock_irq(&current->sighand->siglock);
		write_seqlock(&psig->stats_lock);
		psig->cutime += tgutime + sig->cutime;
		psig->cstime += tgstime + sig->cstime;
		psig->cgtime += task_gtime(p) + sig->gtime + sig->cgtime;
		psig->cmin_flt +=
			p->min_flt + sig->min_flt + sig->cmin_flt;
		psig->cmaj_flt +=
			p->maj_flt + sig->maj_flt + sig->cmaj_flt;
		psig->cnvcsw +=
			p->nvcsw + sig->nvcsw + sig->cnvcsw;
		psig->cnivcsw +=
			p->nivcsw + sig->nivcsw + sig->cnivcsw;
		psig->cinblock +=
			task_io_get_inblock(p) +
			sig->inblock + sig->cinblock;
		psig->coublock +=
			task_io_get_oublock(p) +
			sig->oublock + sig->coublock;
		maxrss = max(sig->maxrss, sig->cmaxrss);
		if (psig->cmaxrss < maxrss)
			psig->cmaxrss = maxrss;
		task_io_accounting_add(&psig->ioac, &p->ioac);
		task_io_accounting_add(&psig->ioac, &sig->ioac);
		write_sequnlock(&psig->stats_lock);
		spin_unlock_irq(&current->sighand->siglock);
	}

	retval = wo->wo_rusage
		? getrusage(p, RUSAGE_BOTH, wo->wo_rusage) : 0;
	status = (p->signal->flags & SIGNAL_GROUP_EXIT)
		? p->signal->group_exit_code : p->exit_code;
	if (!retval && wo->wo_stat)
		retval = put_user(status, wo->wo_stat);

	infop = wo->wo_info;
	if (!retval && infop)
		retval = put_user(SIGCHLD, &infop->si_signo);
	if (!retval && infop)
		retval = put_user(0, &infop->si_errno);
	if (!retval && infop) {
		int why;

		if ((status & 0x7f) == 0) {
			why = CLD_EXITED;
			status >>= 8;
		} else {
			why = (status & 0x80) ? CLD_DUMPED : CLD_KILLED;
			status &= 0x7f;
		}
		retval = put_user((short)why, &infop->si_code);
		if (!retval)
			retval = put_user(status, &infop->si_status);
	}
	if (!retval && infop)
		retval = put_user(pid, &infop->si_pid);
	if (!retval && infop)
		retval = put_user(uid, &infop->si_uid);
	if (!retval)
		retval = pid;

	if (state == EXIT_TRACE) {
		write_lock_irq(&tasklist_lock);
		/* We dropped tasklist, ptracer could die and untrace */
		ptrace_unlink(p);

		/* If parent wants a zombie, don't release it now */
		state = EXIT_ZOMBIE;
		if (do_notify_parent(p, p->exit_signal))
			state = EXIT_DEAD;
		p->exit_state = state;
		write_unlock_irq(&tasklist_lock);
	}
	if (state == EXIT_DEAD)
		release_task(p);

	return retval;
}

static int *task_stopped_code(struct task_struct *p, bool ptrace)
{
	if (ptrace) {
		if (task_is_stopped_or_traced(p) &&
		    !(p->jobctl & JOBCTL_LISTENING))
			return &p->exit_code;
	} else {
		if (p->signal->flags & SIGNAL_STOP_STOPPED)
			return &p->signal->group_exit_code;
	}
	return NULL;
}

/**
 * wait_task_stopped - Wait for %TASK_STOPPED or %TASK_TRACED
 * @wo: wait options
 * @ptrace: is the wait for ptrace
 * @p: task to wait for
 *
 * Handle sys_wait4() work for %p in state %TASK_STOPPED or %TASK_TRACED.
 *
 * CONTEXT:
 * read_lock(&tasklist_lock), which is released if return value is
 * non-zero.  Also, grabs and releases @p->sighand->siglock.
 *
 * RETURNS:
 * 0 if wait condition didn't exist and search for other wait conditions
 * should continue.  Non-zero return, -errno on failure and @p's pid on
 * success, implies that tasklist_lock is released and wait condition
 * search should terminate.
 */
static int wait_task_stopped(struct wait_opts *wo,
				int ptrace, struct task_struct *p)
{
	struct siginfo __user *infop;
	int retval, exit_code, *p_code, why;
	uid_t uid = 0; /* unneeded, required by compiler */
	pid_t pid;

	/*
	 * Traditionally we see ptrace'd stopped tasks regardless of options.
	 */
	if (!ptrace && !(wo->wo_flags & WUNTRACED))
		return 0;

	if (!task_stopped_code(p, ptrace))
		return 0;

	exit_code = 0;
	spin_lock_irq(&p->sighand->siglock);

	p_code = task_stopped_code(p, ptrace);
	if (unlikely(!p_code))
		goto unlock_sig;

	exit_code = *p_code;
	if (!exit_code)
		goto unlock_sig;

	if (!unlikely(wo->wo_flags & WNOWAIT))
		*p_code = 0;

	uid = from_kuid_munged(current_user_ns(), task_uid(p));
unlock_sig:
	spin_unlock_irq(&p->sighand->siglock);
	if (!exit_code)
		return 0;

	/*
	 * Now we are pretty sure this task is interesting.
	 * Make sure it doesn't get reaped out from under us while we
	 * give up the lock and then examine it below.  We don't want to
	 * keep holding onto the tasklist_lock while we call getrusage and
	 * possibly take page faults for user memory.
	 */
	get_task_struct(p);
	pid = task_pid_vnr(p);
	why = ptrace ? CLD_TRAPPED : CLD_STOPPED;
	read_unlock(&tasklist_lock);
	sched_annotate_sleep();

	if (unlikely(wo->wo_flags & WNOWAIT))
		return wait_noreap_copyout(wo, p, pid, uid, why, exit_code);

	retval = wo->wo_rusage
		? getrusage(p, RUSAGE_BOTH, wo->wo_rusage) : 0;
	if (!retval && wo->wo_stat)
		retval = put_user((exit_code << 8) | 0x7f, wo->wo_stat);

	infop = wo->wo_info;
	if (!retval && infop)
		retval = put_user(SIGCHLD, &infop->si_signo);
	if (!retval && infop)
		retval = put_user(0, &infop->si_errno);
	if (!retval && infop)
		retval = put_user((short)why, &infop->si_code);
	if (!retval && infop)
		retval = put_user(exit_code, &infop->si_status);
	if (!retval && infop)
		retval = put_user(pid, &infop->si_pid);
	if (!retval && infop)
		retval = put_user(uid, &infop->si_uid);
	if (!retval)
		retval = pid;
	put_task_struct(p);

	BUG_ON(!retval);
	return retval;
}

/*
 * Handle do_wait work for one task in a live, non-stopped state.
 * read_lock(&tasklist_lock) on entry.  If we return zero, we still hold
 * the lock and this task is uninteresting.  If we return nonzero, we have
 * released the lock and the system call should return.
 */
static int wait_task_continued(struct wait_opts *wo, struct task_struct *p)
{
	int retval;
	pid_t pid;
	uid_t uid;

	if (!unlikely(wo->wo_flags & WCONTINUED))
		return 0;

	if (!(p->signal->flags & SIGNAL_STOP_CONTINUED))
		return 0;

	spin_lock_irq(&p->sighand->siglock);
	/* Re-check with the lock held.  */
	if (!(p->signal->flags & SIGNAL_STOP_CONTINUED)) {
		spin_unlock_irq(&p->sighand->siglock);
		return 0;
	}
	if (!unlikely(wo->wo_flags & WNOWAIT))
		p->signal->flags &= ~SIGNAL_STOP_CONTINUED;
	uid = from_kuid_munged(current_user_ns(), task_uid(p));
	spin_unlock_irq(&p->sighand->siglock);

	pid = task_pid_vnr(p);
	get_task_struct(p);
	read_unlock(&tasklist_lock);
	sched_annotate_sleep();

	if (!wo->wo_info) {
		retval = wo->wo_rusage
			? getrusage(p, RUSAGE_BOTH, wo->wo_rusage) : 0;
		put_task_struct(p);
		if (!retval && wo->wo_stat)
			retval = put_user(0xffff, wo->wo_stat);
		if (!retval)
			retval = pid;
	} else {
		retval = wait_noreap_copyout(wo, p, pid, uid,
					     CLD_CONTINUED, SIGCONT);
		BUG_ON(retval == 0);
	}

	return retval;
}

/*
 * Consider @p for a wait by @parent.
 *
 * -ECHILD should be in ->notask_error before the first call.
 * Returns nonzero for a final return, when we have unlocked tasklist_lock.
 * Returns zero if the search for a child should continue;
 * then ->notask_error is 0 if @p is an eligible child,
 * or another error from security_task_wait(), or still -ECHILD.
 */
static int wait_consider_task(struct wait_opts *wo, int ptrace,
				struct task_struct *p)
{
	int ret;

	if (unlikely(p->exit_state == EXIT_DEAD))
		return 0;

	ret = eligible_child(wo, p);
	if (!ret)
		return ret;

	ret = security_task_wait(p);
	if (unlikely(ret < 0)) {
		/*
		 * If we have not yet seen any eligible child,
		 * then let this error code replace -ECHILD.
		 * A permission error will give the user a clue
		 * to look for security policy problems, rather
		 * than for mysterious wait bugs.
		 */
		if (wo->notask_error)
			wo->notask_error = ret;
		return 0;
	}

	if (unlikely(p->exit_state == EXIT_TRACE)) {
		/*
		 * ptrace == 0 means we are the natural parent. In this case
		 * we should clear notask_error, debugger will notify us.
		 */
		if (likely(!ptrace))
			wo->notask_error = 0;
		return 0;
	}

	if (likely(!ptrace) && unlikely(p->ptrace)) {
		/*
		 * If it is traced by its real parent's group, just pretend
		 * the caller is ptrace_do_wait() and reap this child if it
		 * is zombie.
		 *
		 * This also hides group stop state from real parent; otherwise
		 * a single stop can be reported twice as group and ptrace stop.
		 * If a ptracer wants to distinguish these two events for its
		 * own children it should create a separate process which takes
		 * the role of real parent.
		 */
		if (!ptrace_reparented(p))
			ptrace = 1;
	}

	/* slay zombie? */
	if (p->exit_state == EXIT_ZOMBIE) {
		/* we don't reap group leaders with subthreads */
		if (!delay_group_leader(p)) {
			/*
			 * A zombie ptracee is only visible to its ptracer.
			 * Notification and reaping will be cascaded to the
			 * real parent when the ptracer detaches.
			 */
			if (unlikely(ptrace) || likely(!p->ptrace))
				return wait_task_zombie(wo, p);
		}

		/*
		 * Allow access to stopped/continued state via zombie by
		 * falling through.  Clearing of notask_error is complex.
		 *
		 * When !@ptrace:
		 *
		 * If WEXITED is set, notask_error should naturally be
		 * cleared.  If not, subset of WSTOPPED|WCONTINUED is set,
		 * so, if there are live subthreads, there are events to
		 * wait for.  If all subthreads are dead, it's still safe
		 * to clear - this function will be called again in finite
		 * amount time once all the subthreads are released and
		 * will then return without clearing.
		 *
		 * When @ptrace:
		 *
		 * Stopped state is per-task and thus can't change once the
		 * target task dies.  Only continued and exited can happen.
		 * Clear notask_error if WCONTINUED | WEXITED.
		 */
		if (likely(!ptrace) || (wo->wo_flags & (WCONTINUED | WEXITED)))
			wo->notask_error = 0;
	} else {
		/*
		 * @p is alive and it's gonna stop, continue or exit, so
		 * there always is something to wait for.
		 */
		wo->notask_error = 0;
	}

	/*
	 * Wait for stopped.  Depending on @ptrace, different stopped state
	 * is used and the two don't interact with each other.
	 */
	ret = wait_task_stopped(wo, ptrace, p);
	if (ret)
		return ret;

	/*
	 * Wait for continued.  There's only one continued state and the
	 * ptracer can consume it which can confuse the real parent.  Don't
	 * use WCONTINUED from ptracer.  You don't need or want it.
	 */
	return wait_task_continued(wo, p);
}

/*
 * Do the work of do_wait() for one thread in the group, @tsk.
 *
 * -ECHILD should be in ->notask_error before the first call.
 * Returns nonzero for a final return, when we have unlocked tasklist_lock.
 * Returns zero if the search for a child should continue; then
 * ->notask_error is 0 if there were any eligible children,
 * or another error from security_task_wait(), or still -ECHILD.
 */
static int do_wait_thread(struct wait_opts *wo, struct task_struct *tsk)
{
	struct task_struct *p;

	list_for_each_entry(p, &tsk->children, sibling) {
		int ret = wait_consider_task(wo, 0, p);

		if (ret)
			return ret;
	}

	return 0;
}

static int ptrace_do_wait(struct wait_opts *wo, struct task_struct *tsk)
{
	struct task_struct *p;

	list_for_each_entry(p, &tsk->ptraced, ptrace_entry) {
		int ret = wait_consider_task(wo, 1, p);

		if (ret)
			return ret;
	}

	return 0;
}

static int child_wait_callback(wait_queue_t *wait, unsigned mode,
				int sync, void *key)
{
	struct wait_opts *wo = container_of(wait, struct wait_opts,
						child_wait);
	struct task_struct *p = key;

	if (!eligible_pid(wo, p))
		return 0;

	if ((wo->wo_flags & __WNOTHREAD) && wait->private != p->parent)
		return 0;

	return default_wake_function(wait, mode, sync, key);
}

void __wake_up_parent(struct task_struct *p, struct task_struct *parent)
{
	__wake_up_sync_key(&parent->signal->wait_chldexit,
				TASK_INTERRUPTIBLE, 1, p);
}

static long do_wait(struct wait_opts *wo)
{
	struct task_struct *tsk;
	int retval;

	trace_sched_process_wait(wo->wo_pid);

	init_waitqueue_func_entry(&wo->child_wait, child_wait_callback);
	wo->child_wait.private = current;
	add_wait_queue(&current->signal->wait_chldexit, &wo->child_wait);
repeat:
	/*
	 * If there is nothing that can match our critiera just get out.
	 * We will clear ->notask_error to zero if we see any child that
	 * might later match our criteria, even if we are not able to reap
	 * it yet.
	 */
	wo->notask_error = -ECHILD;
	if ((wo->wo_type < PIDTYPE_MAX) &&
	   (!wo->wo_pid || hlist_empty(&wo->wo_pid->tasks[wo->wo_type])))
		goto notask;

	set_current_state(TASK_INTERRUPTIBLE);
	read_lock(&tasklist_lock);
	tsk = current;
	do {
		retval = do_wait_thread(wo, tsk);
		if (retval)
			goto end;

		retval = ptrace_do_wait(wo, tsk);
		if (retval)
			goto end;

		if (wo->wo_flags & __WNOTHREAD)
			break;
	} while_each_thread(current, tsk);
	read_unlock(&tasklist_lock);

notask:
	retval = wo->notask_error;
	if (!retval && !(wo->wo_flags & WNOHANG)) {
		retval = -ERESTARTSYS;
		if (!signal_pending(current)) {
			schedule();
			goto repeat;
		}
	}
end:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&current->signal->wait_chldexit, &wo->child_wait);
	return retval;
}

SYSCALL_DEFINE5(waitid, int, which, pid_t, upid, struct siginfo __user *,
		infop, int, options, struct rusage __user *, ru)
{
	struct wait_opts wo;
	struct pid *pid = NULL;
	enum pid_type type;
	long ret;

	if (options & ~(WNOHANG|WNOWAIT|WEXITED|WSTOPPED|WCONTINUED))
		return -EINVAL;
	if (!(options & (WEXITED|WSTOPPED|WCONTINUED)))
		return -EINVAL;

	switch (which) {
	case P_ALL:
		type = PIDTYPE_MAX;
		break;
	case P_PID:
		type = PIDTYPE_PID;
		if (upid <= 0)
			return -EINVAL;
		break;
	case P_PGID:
		type = PIDTYPE_PGID;
		if (upid <= 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (type < PIDTYPE_MAX)
		pid = find_get_pid(upid);

	wo.wo_type	= type;
	wo.wo_pid	= pid;
	wo.wo_flags	= options;
	wo.wo_info	= infop;
	wo.wo_stat	= NULL;
	wo.wo_rusage	= ru;
	ret = do_wait(&wo);

	if (ret > 0) {
		ret = 0;
	} else if (infop) {
		/*
		 * For a WNOHANG return, clear out all the fields
		 * we would set so the user can easily tell the
		 * difference.
		 */
		if (!ret)
			ret = put_user(0, &infop->si_signo);
		if (!ret)
			ret = put_user(0, &infop->si_errno);
		if (!ret)
			ret = put_user(0, &infop->si_code);
		if (!ret)
			ret = put_user(0, &infop->si_pid);
		if (!ret)
			ret = put_user(0, &infop->si_uid);
		if (!ret)
			ret = put_user(0, &infop->si_status);
	}

	put_pid(pid);
	return ret;
}

SYSCALL_DEFINE4(wait4, pid_t, upid, int __user *, stat_addr,
		int, options, struct rusage __user *, ru)
{
	struct wait_opts wo;
	struct pid *pid = NULL;
	enum pid_type type;
	long ret;

	if (options & ~(WNOHANG|WUNTRACED|WCONTINUED|
			__WNOTHREAD|__WCLONE|__WALL))
		return -EINVAL;

	if (upid == -1)
		type = PIDTYPE_MAX;
	else if (upid < 0) {
		type = PIDTYPE_PGID;
		pid = find_get_pid(-upid);
	} else if (upid == 0) {
		type = PIDTYPE_PGID;
		pid = get_task_pid(current, PIDTYPE_PGID);
	} else /* upid > 0 */ {
		type = PIDTYPE_PID;
		pid = find_get_pid(upid);
	}

	wo.wo_type	= type;
	wo.wo_pid	= pid;
	wo.wo_flags	= options | WEXITED;
	wo.wo_info	= NULL;
	wo.wo_stat	= stat_addr;
	wo.wo_rusage	= ru;
	ret = do_wait(&wo);
	put_pid(pid);

	return ret;
}

#ifdef __ARCH_WANT_SYS_WAITPID

/*
 * sys_waitpid() remains for compatibility. waitpid() should be
 * implemented by calling sys_wait4() from libc.a.
 */
SYSCALL_DEFINE3(waitpid, pid_t, pid, int __user *, stat_addr, int, options)
{
	return sys_wait4(pid, stat_addr, options, NULL);
}

#endif
