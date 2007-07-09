
#ifdef CONFIG_SCHEDSTATS
/*
 * bump this up when changing the output format or the meaning of an existing
 * format, so that tools can adapt (or abort)
 */
#define SCHEDSTAT_VERSION 14

static int show_schedstat(struct seq_file *seq, void *v)
{
	int cpu;

	seq_printf(seq, "version %d\n", SCHEDSTAT_VERSION);
	seq_printf(seq, "timestamp %lu\n", jiffies);
	for_each_online_cpu(cpu) {
		struct rq *rq = cpu_rq(cpu);
#ifdef CONFIG_SMP
		struct sched_domain *sd;
		int dcnt = 0;
#endif

		/* runqueue-specific stats */
		seq_printf(seq,
		    "cpu%d %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
		    cpu, rq->yld_both_empty,
		    rq->yld_act_empty, rq->yld_exp_empty, rq->yld_cnt,
		    rq->sched_switch, rq->sched_cnt, rq->sched_goidle,
		    rq->ttwu_cnt, rq->ttwu_local,
		    rq->rq_sched_info.cpu_time,
		    rq->rq_sched_info.run_delay, rq->rq_sched_info.pcnt);

		seq_printf(seq, "\n");

#ifdef CONFIG_SMP
		/* domain-specific stats */
		preempt_disable();
		for_each_domain(cpu, sd) {
			enum cpu_idle_type itype;
			char mask_str[NR_CPUS];

			cpumask_scnprintf(mask_str, NR_CPUS, sd->span);
			seq_printf(seq, "domain%d %s", dcnt++, mask_str);
			for (itype = CPU_IDLE; itype < CPU_MAX_IDLE_TYPES;
					itype++) {
				seq_printf(seq, " %lu %lu %lu %lu %lu %lu %lu "
						"%lu",
				    sd->lb_cnt[itype],
				    sd->lb_balanced[itype],
				    sd->lb_failed[itype],
				    sd->lb_imbalance[itype],
				    sd->lb_gained[itype],
				    sd->lb_hot_gained[itype],
				    sd->lb_nobusyq[itype],
				    sd->lb_nobusyg[itype]);
			}
			seq_printf(seq, " %lu %lu %lu %lu %lu %lu %lu %lu %lu"
			    " %lu %lu %lu\n",
			    sd->alb_cnt, sd->alb_failed, sd->alb_pushed,
			    sd->sbe_cnt, sd->sbe_balanced, sd->sbe_pushed,
			    sd->sbf_cnt, sd->sbf_balanced, sd->sbf_pushed,
			    sd->ttwu_wake_remote, sd->ttwu_move_affine,
			    sd->ttwu_move_balance);
		}
		preempt_enable();
#endif
	}
	return 0;
}

static int schedstat_open(struct inode *inode, struct file *file)
{
	unsigned int size = PAGE_SIZE * (1 + num_online_cpus() / 32);
	char *buf = kmalloc(size, GFP_KERNEL);
	struct seq_file *m;
	int res;

	if (!buf)
		return -ENOMEM;
	res = single_open(file, show_schedstat, NULL);
	if (!res) {
		m = file->private_data;
		m->buf = buf;
		m->size = size;
	} else
		kfree(buf);
	return res;
}

const struct file_operations proc_schedstat_operations = {
	.open    = schedstat_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{
	if (rq) {
		rq->rq_sched_info.run_delay += delta;
		rq->rq_sched_info.pcnt++;
	}
}

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_sched_info.cpu_time += delta;
}
# define schedstat_inc(rq, field)	do { (rq)->field++; } while (0)
# define schedstat_add(rq, field, amt)	do { (rq)->field += (amt); } while (0)
#else /* !CONFIG_SCHEDSTATS */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{}
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{}
# define schedstat_inc(rq, field)	do { } while (0)
# define schedstat_add(rq, field, amt)	do { } while (0)
#endif

#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_TASK_DELAY_ACCT)
/*
 * Called when a process is dequeued from the active array and given
 * the cpu.  We should note that with the exception of interactive
 * tasks, the expired queue will become the active queue after the active
 * queue is empty, without explicitly dequeuing and requeuing tasks in the
 * expired queue.  (Interactive tasks may be requeued directly to the
 * active queue, thus delaying tasks in the expired queue from running;
 * see scheduler_tick()).
 *
 * This function is only called from sched_info_arrive(), rather than
 * dequeue_task(). Even though a task may be queued and dequeued multiple
 * times as it is shuffled about, we're really interested in knowing how
 * long it was from the *first* time it was queued to the time that it
 * finally hit a cpu.
 */
static inline void sched_info_dequeued(struct task_struct *t)
{
	t->sched_info.last_queued = 0;
}

/*
 * Called when a task finally hits the cpu.  We can now calculate how
 * long it was waiting to run.  We also note when it began so that we
 * can keep stats on how long its timeslice is.
 */
static void sched_info_arrive(struct task_struct *t)
{
	unsigned long long now = sched_clock(), delta = 0;

	if (t->sched_info.last_queued)
		delta = now - t->sched_info.last_queued;
	sched_info_dequeued(t);
	t->sched_info.run_delay += delta;
	t->sched_info.last_arrival = now;
	t->sched_info.pcnt++;

	rq_sched_info_arrive(task_rq(t), delta);
}

/*
 * Called when a process is queued into either the active or expired
 * array.  The time is noted and later used to determine how long we
 * had to wait for us to reach the cpu.  Since the expired queue will
 * become the active queue after active queue is empty, without dequeuing
 * and requeuing any tasks, we are interested in queuing to either. It
 * is unusual but not impossible for tasks to be dequeued and immediately
 * requeued in the same or another array: this can happen in sched_yield(),
 * set_user_nice(), and even load_balance() as it moves tasks from runqueue
 * to runqueue.
 *
 * This function is only called from enqueue_task(), but also only updates
 * the timestamp if it is already not set.  It's assumed that
 * sched_info_dequeued() will clear that stamp when appropriate.
 */
static inline void sched_info_queued(struct task_struct *t)
{
	if (unlikely(sched_info_on()))
		if (!t->sched_info.last_queued)
			t->sched_info.last_queued = sched_clock();
}

/*
 * Called when a process ceases being the active-running process, either
 * voluntarily or involuntarily.  Now we can calculate how long we ran.
 */
static inline void sched_info_depart(struct task_struct *t)
{
	unsigned long long delta = sched_clock() - t->sched_info.last_arrival;

	t->sched_info.cpu_time += delta;
	rq_sched_info_depart(task_rq(t), delta);
}

/*
 * Called when tasks are switched involuntarily due, typically, to expiring
 * their time slice.  (This may also be called when switching to or from
 * the idle task.)  We are only called when prev != next.
 */
static inline void
__sched_info_switch(struct task_struct *prev, struct task_struct *next)
{
	struct rq *rq = task_rq(prev);

	/*
	 * prev now departs the cpu.  It's not interesting to record
	 * stats about how efficient we were at scheduling the idle
	 * process, however.
	 */
	if (prev != rq->idle)
		sched_info_depart(prev);

	if (next != rq->idle)
		sched_info_arrive(next);
}
static inline void
sched_info_switch(struct task_struct *prev, struct task_struct *next)
{
	if (unlikely(sched_info_on()))
		__sched_info_switch(prev, next);
}
#else
#define sched_info_queued(t)		do { } while (0)
#define sched_info_switch(t, next)	do { } while (0)
#endif /* CONFIG_SCHEDSTATS || CONFIG_TASK_DELAY_ACCT */

