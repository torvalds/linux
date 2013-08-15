#undef TRACE_SYSTEM
#define TRACE_SYSTEM rcu

#if !defined(_TRACE_RCU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RCU_H

#include <linux/tracepoint.h>

/*
 * Tracepoint for start/end markers used for utilization calculations.
 * By convention, the string is of the following forms:
 *
 * "Start <activity>" -- Mark the start of the specified activity,
 *			 such as "context switch".  Nesting is permitted.
 * "End <activity>" -- Mark the end of the specified activity.
 *
 * An "@" character within "<activity>" is a comment character: Data
 * reduction scripts will ignore the "@" and the remainder of the line.
 */
TRACE_EVENT(rcu_utilization,

	TP_PROTO(const char *s),

	TP_ARGS(s),

	TP_STRUCT__entry(
		__field(const char *, s)
	),

	TP_fast_assign(
		__entry->s = s;
	),

	TP_printk("%s", __entry->s)
);

#ifdef CONFIG_RCU_TRACE

#if defined(CONFIG_TREE_RCU) || defined(CONFIG_TREE_PREEMPT_RCU)

/*
 * Tracepoint for grace-period events.  Takes a string identifying the
 * RCU flavor, the grace-period number, and a string identifying the
 * grace-period-related event as follows:
 *
 *	"AccReadyCB": CPU acclerates new callbacks to RCU_NEXT_READY_TAIL.
 *	"AccWaitCB": CPU accelerates new callbacks to RCU_WAIT_TAIL.
 *	"newreq": Request a new grace period.
 *	"start": Start a grace period.
 *	"cpustart": CPU first notices a grace-period start.
 *	"cpuqs": CPU passes through a quiescent state.
 *	"cpuonl": CPU comes online.
 *	"cpuofl": CPU goes offline.
 *	"reqwait": GP kthread sleeps waiting for grace-period request.
 *	"reqwaitsig": GP kthread awakened by signal from reqwait state.
 *	"fqswait": GP kthread waiting until time to force quiescent states.
 *	"fqsstart": GP kthread starts forcing quiescent states.
 *	"fqsend": GP kthread done forcing quiescent states.
 *	"fqswaitsig": GP kthread awakened by signal from fqswait state.
 *	"end": End a grace period.
 *	"cpuend": CPU first notices a grace-period end.
 */
TRACE_EVENT(rcu_grace_period,

	TP_PROTO(const char *rcuname, unsigned long gpnum, const char *gpevent),

	TP_ARGS(rcuname, gpnum, gpevent),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(const char *, gpevent)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->gpevent = gpevent;
	),

	TP_printk("%s %lu %s",
		  __entry->rcuname, __entry->gpnum, __entry->gpevent)
);

/*
 * Tracepoint for future grace-period events, including those for no-callbacks
 * CPUs.  The caller should pull the data from the rcu_node structure,
 * other than rcuname, which comes from the rcu_state structure, and event,
 * which is one of the following:
 *
 * "Startleaf": Request a nocb grace period based on leaf-node data.
 * "Startedleaf": Leaf-node start proved sufficient.
 * "Startedleafroot": Leaf-node start proved sufficient after checking root.
 * "Startedroot": Requested a nocb grace period based on root-node data.
 * "StartWait": Start waiting for the requested grace period.
 * "ResumeWait": Resume waiting after signal.
 * "EndWait": Complete wait.
 * "Cleanup": Clean up rcu_node structure after previous GP.
 * "CleanupMore": Clean up, and another no-CB GP is needed.
 */
TRACE_EVENT(rcu_future_grace_period,

	TP_PROTO(const char *rcuname, unsigned long gpnum, unsigned long completed,
		 unsigned long c, u8 level, int grplo, int grphi,
		 const char *gpevent),

	TP_ARGS(rcuname, gpnum, completed, c, level, grplo, grphi, gpevent),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(unsigned long, completed)
		__field(unsigned long, c)
		__field(u8, level)
		__field(int, grplo)
		__field(int, grphi)
		__field(const char *, gpevent)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->completed = completed;
		__entry->c = c;
		__entry->level = level;
		__entry->grplo = grplo;
		__entry->grphi = grphi;
		__entry->gpevent = gpevent;
	),

	TP_printk("%s %lu %lu %lu %u %d %d %s",
		  __entry->rcuname, __entry->gpnum, __entry->completed,
		  __entry->c, __entry->level, __entry->grplo, __entry->grphi,
		  __entry->gpevent)
);

/*
 * Tracepoint for grace-period-initialization events.  These are
 * distinguished by the type of RCU, the new grace-period number, the
 * rcu_node structure level, the starting and ending CPU covered by the
 * rcu_node structure, and the mask of CPUs that will be waited for.
 * All but the type of RCU are extracted from the rcu_node structure.
 */
TRACE_EVENT(rcu_grace_period_init,

	TP_PROTO(const char *rcuname, unsigned long gpnum, u8 level,
		 int grplo, int grphi, unsigned long qsmask),

	TP_ARGS(rcuname, gpnum, level, grplo, grphi, qsmask),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(u8, level)
		__field(int, grplo)
		__field(int, grphi)
		__field(unsigned long, qsmask)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->level = level;
		__entry->grplo = grplo;
		__entry->grphi = grphi;
		__entry->qsmask = qsmask;
	),

	TP_printk("%s %lu %u %d %d %lx",
		  __entry->rcuname, __entry->gpnum, __entry->level,
		  __entry->grplo, __entry->grphi, __entry->qsmask)
);

/*
 * Tracepoint for RCU no-CBs CPU callback handoffs.  This event is intended
 * to assist debugging of these handoffs.
 *
 * The first argument is the name of the RCU flavor, and the second is
 * the number of the offloaded CPU are extracted.  The third and final
 * argument is a string as follows:
 *
 *	"WakeEmpty": Wake rcuo kthread, first CB to empty list.
 *	"WakeOvf": Wake rcuo kthread, CB list is huge.
 *	"WakeNot": Don't wake rcuo kthread.
 *	"WakeNotPoll": Don't wake rcuo kthread because it is polling.
 *	"Poll": Start of new polling cycle for rcu_nocb_poll.
 *	"Sleep": Sleep waiting for CBs for !rcu_nocb_poll.
 *	"WokeEmpty": rcuo kthread woke to find empty list.
 *	"WokeNonEmpty": rcuo kthread woke to find non-empty list.
 *	"WaitQueue": Enqueue partially done, timed wait for it to complete.
 *	"WokeQueue": Partial enqueue now complete.
 */
TRACE_EVENT(rcu_nocb_wake,

	TP_PROTO(const char *rcuname, int cpu, const char *reason),

	TP_ARGS(rcuname, cpu, reason),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(int, cpu)
		__field(const char *, reason)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->cpu = cpu;
		__entry->reason = reason;
	),

	TP_printk("%s %d %s", __entry->rcuname, __entry->cpu, __entry->reason)
);

/*
 * Tracepoint for tasks blocking within preemptible-RCU read-side
 * critical sections.  Track the type of RCU (which one day might
 * include SRCU), the grace-period number that the task is blocking
 * (the current or the next), and the task's PID.
 */
TRACE_EVENT(rcu_preempt_task,

	TP_PROTO(const char *rcuname, int pid, unsigned long gpnum),

	TP_ARGS(rcuname, pid, gpnum),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->pid = pid;
	),

	TP_printk("%s %lu %d",
		  __entry->rcuname, __entry->gpnum, __entry->pid)
);

/*
 * Tracepoint for tasks that blocked within a given preemptible-RCU
 * read-side critical section exiting that critical section.  Track the
 * type of RCU (which one day might include SRCU) and the task's PID.
 */
TRACE_EVENT(rcu_unlock_preempted_task,

	TP_PROTO(const char *rcuname, unsigned long gpnum, int pid),

	TP_ARGS(rcuname, gpnum, pid),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->pid = pid;
	),

	TP_printk("%s %lu %d", __entry->rcuname, __entry->gpnum, __entry->pid)
);

/*
 * Tracepoint for quiescent-state-reporting events.  These are
 * distinguished by the type of RCU, the grace-period number, the
 * mask of quiescent lower-level entities, the rcu_node structure level,
 * the starting and ending CPU covered by the rcu_node structure, and
 * whether there are any blocked tasks blocking the current grace period.
 * All but the type of RCU are extracted from the rcu_node structure.
 */
TRACE_EVENT(rcu_quiescent_state_report,

	TP_PROTO(const char *rcuname, unsigned long gpnum,
		 unsigned long mask, unsigned long qsmask,
		 u8 level, int grplo, int grphi, int gp_tasks),

	TP_ARGS(rcuname, gpnum, mask, qsmask, level, grplo, grphi, gp_tasks),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(unsigned long, mask)
		__field(unsigned long, qsmask)
		__field(u8, level)
		__field(int, grplo)
		__field(int, grphi)
		__field(u8, gp_tasks)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->mask = mask;
		__entry->qsmask = qsmask;
		__entry->level = level;
		__entry->grplo = grplo;
		__entry->grphi = grphi;
		__entry->gp_tasks = gp_tasks;
	),

	TP_printk("%s %lu %lx>%lx %u %d %d %u",
		  __entry->rcuname, __entry->gpnum,
		  __entry->mask, __entry->qsmask, __entry->level,
		  __entry->grplo, __entry->grphi, __entry->gp_tasks)
);

/*
 * Tracepoint for quiescent states detected by force_quiescent_state().
 * These trace events include the type of RCU, the grace-period number
 * that was blocked by the CPU, the CPU itself, and the type of quiescent
 * state, which can be "dti" for dyntick-idle mode, "ofl" for CPU offline,
 * or "kick" when kicking a CPU that has been in dyntick-idle mode for
 * too long.
 */
TRACE_EVENT(rcu_fqs,

	TP_PROTO(const char *rcuname, unsigned long gpnum, int cpu, const char *qsevent),

	TP_ARGS(rcuname, gpnum, cpu, qsevent),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(unsigned long, gpnum)
		__field(int, cpu)
		__field(const char *, qsevent)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->gpnum = gpnum;
		__entry->cpu = cpu;
		__entry->qsevent = qsevent;
	),

	TP_printk("%s %lu %d %s",
		  __entry->rcuname, __entry->gpnum,
		  __entry->cpu, __entry->qsevent)
);

#endif /* #if defined(CONFIG_TREE_RCU) || defined(CONFIG_TREE_PREEMPT_RCU) */

/*
 * Tracepoint for dyntick-idle entry/exit events.  These take a string
 * as argument: "Start" for entering dyntick-idle mode, "End" for
 * leaving it, "--=" for events moving towards idle, and "++=" for events
 * moving away from idle.  "Error on entry: not idle task" and "Error on
 * exit: not idle task" indicate that a non-idle task is erroneously
 * toying with the idle loop.
 *
 * These events also take a pair of numbers, which indicate the nesting
 * depth before and after the event of interest.  Note that task-related
 * events use the upper bits of each number, while interrupt-related
 * events use the lower bits.
 */
TRACE_EVENT(rcu_dyntick,

	TP_PROTO(const char *polarity, long long oldnesting, long long newnesting),

	TP_ARGS(polarity, oldnesting, newnesting),

	TP_STRUCT__entry(
		__field(const char *, polarity)
		__field(long long, oldnesting)
		__field(long long, newnesting)
	),

	TP_fast_assign(
		__entry->polarity = polarity;
		__entry->oldnesting = oldnesting;
		__entry->newnesting = newnesting;
	),

	TP_printk("%s %llx %llx", __entry->polarity,
		  __entry->oldnesting, __entry->newnesting)
);

/*
 * Tracepoint for RCU preparation for idle, the goal being to get RCU
 * processing done so that the current CPU can shut off its scheduling
 * clock and enter dyntick-idle mode.  One way to accomplish this is
 * to drain all RCU callbacks from this CPU, and the other is to have
 * done everything RCU requires for the current grace period.  In this
 * latter case, the CPU will be awakened at the end of the current grace
 * period in order to process the remainder of its callbacks.
 *
 * These tracepoints take a string as argument:
 *
 *	"No callbacks": Nothing to do, no callbacks on this CPU.
 *	"In holdoff": Nothing to do, holding off after unsuccessful attempt.
 *	"Begin holdoff": Attempt failed, don't retry until next jiffy.
 *	"Dyntick with callbacks": Entering dyntick-idle despite callbacks.
 *	"Dyntick with lazy callbacks": Entering dyntick-idle w/lazy callbacks.
 *	"More callbacks": Still more callbacks, try again to clear them out.
 *	"Callbacks drained": All callbacks processed, off to dyntick idle!
 *	"Timer": Timer fired to cause CPU to continue processing callbacks.
 *	"Demigrate": Timer fired on wrong CPU, woke up correct CPU.
 *	"Cleanup after idle": Idle exited, timer canceled.
 */
TRACE_EVENT(rcu_prep_idle,

	TP_PROTO(const char *reason),

	TP_ARGS(reason),

	TP_STRUCT__entry(
		__field(const char *, reason)
	),

	TP_fast_assign(
		__entry->reason = reason;
	),

	TP_printk("%s", __entry->reason)
);

/*
 * Tracepoint for the registration of a single RCU callback function.
 * The first argument is the type of RCU, the second argument is
 * a pointer to the RCU callback itself, the third element is the
 * number of lazy callbacks queued, and the fourth element is the
 * total number of callbacks queued.
 */
TRACE_EVENT(rcu_callback,

	TP_PROTO(const char *rcuname, struct rcu_head *rhp, long qlen_lazy,
		 long qlen),

	TP_ARGS(rcuname, rhp, qlen_lazy, qlen),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(void *, rhp)
		__field(void *, func)
		__field(long, qlen_lazy)
		__field(long, qlen)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->rhp = rhp;
		__entry->func = rhp->func;
		__entry->qlen_lazy = qlen_lazy;
		__entry->qlen = qlen;
	),

	TP_printk("%s rhp=%p func=%pf %ld/%ld",
		  __entry->rcuname, __entry->rhp, __entry->func,
		  __entry->qlen_lazy, __entry->qlen)
);

/*
 * Tracepoint for the registration of a single RCU callback of the special
 * kfree() form.  The first argument is the RCU type, the second argument
 * is a pointer to the RCU callback, the third argument is the offset
 * of the callback within the enclosing RCU-protected data structure,
 * the fourth argument is the number of lazy callbacks queued, and the
 * fifth argument is the total number of callbacks queued.
 */
TRACE_EVENT(rcu_kfree_callback,

	TP_PROTO(const char *rcuname, struct rcu_head *rhp, unsigned long offset,
		 long qlen_lazy, long qlen),

	TP_ARGS(rcuname, rhp, offset, qlen_lazy, qlen),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(void *, rhp)
		__field(unsigned long, offset)
		__field(long, qlen_lazy)
		__field(long, qlen)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->rhp = rhp;
		__entry->offset = offset;
		__entry->qlen_lazy = qlen_lazy;
		__entry->qlen = qlen;
	),

	TP_printk("%s rhp=%p func=%ld %ld/%ld",
		  __entry->rcuname, __entry->rhp, __entry->offset,
		  __entry->qlen_lazy, __entry->qlen)
);

/*
 * Tracepoint for marking the beginning rcu_do_batch, performed to start
 * RCU callback invocation.  The first argument is the RCU flavor,
 * the second is the number of lazy callbacks queued, the third is
 * the total number of callbacks queued, and the fourth argument is
 * the current RCU-callback batch limit.
 */
TRACE_EVENT(rcu_batch_start,

	TP_PROTO(const char *rcuname, long qlen_lazy, long qlen, long blimit),

	TP_ARGS(rcuname, qlen_lazy, qlen, blimit),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(long, qlen_lazy)
		__field(long, qlen)
		__field(long, blimit)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->qlen_lazy = qlen_lazy;
		__entry->qlen = qlen;
		__entry->blimit = blimit;
	),

	TP_printk("%s CBs=%ld/%ld bl=%ld",
		  __entry->rcuname, __entry->qlen_lazy, __entry->qlen,
		  __entry->blimit)
);

/*
 * Tracepoint for the invocation of a single RCU callback function.
 * The first argument is the type of RCU, and the second argument is
 * a pointer to the RCU callback itself.
 */
TRACE_EVENT(rcu_invoke_callback,

	TP_PROTO(const char *rcuname, struct rcu_head *rhp),

	TP_ARGS(rcuname, rhp),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(void *, rhp)
		__field(void *, func)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->rhp = rhp;
		__entry->func = rhp->func;
	),

	TP_printk("%s rhp=%p func=%pf",
		  __entry->rcuname, __entry->rhp, __entry->func)
);

/*
 * Tracepoint for the invocation of a single RCU callback of the special
 * kfree() form.  The first argument is the RCU flavor, the second
 * argument is a pointer to the RCU callback, and the third argument
 * is the offset of the callback within the enclosing RCU-protected
 * data structure.
 */
TRACE_EVENT(rcu_invoke_kfree_callback,

	TP_PROTO(const char *rcuname, struct rcu_head *rhp, unsigned long offset),

	TP_ARGS(rcuname, rhp, offset),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(void *, rhp)
		__field(unsigned long, offset)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->rhp = rhp;
		__entry->offset	= offset;
	),

	TP_printk("%s rhp=%p func=%ld",
		  __entry->rcuname, __entry->rhp, __entry->offset)
);

/*
 * Tracepoint for exiting rcu_do_batch after RCU callbacks have been
 * invoked.  The first argument is the name of the RCU flavor,
 * the second argument is number of callbacks actually invoked,
 * the third argument (cb) is whether or not any of the callbacks that
 * were ready to invoke at the beginning of this batch are still
 * queued, the fourth argument (nr) is the return value of need_resched(),
 * the fifth argument (iit) is 1 if the current task is the idle task,
 * and the sixth argument (risk) is the return value from
 * rcu_is_callbacks_kthread().
 */
TRACE_EVENT(rcu_batch_end,

	TP_PROTO(const char *rcuname, int callbacks_invoked,
		 bool cb, bool nr, bool iit, bool risk),

	TP_ARGS(rcuname, callbacks_invoked, cb, nr, iit, risk),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(int, callbacks_invoked)
		__field(bool, cb)
		__field(bool, nr)
		__field(bool, iit)
		__field(bool, risk)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->callbacks_invoked = callbacks_invoked;
		__entry->cb = cb;
		__entry->nr = nr;
		__entry->iit = iit;
		__entry->risk = risk;
	),

	TP_printk("%s CBs-invoked=%d idle=%c%c%c%c",
		  __entry->rcuname, __entry->callbacks_invoked,
		  __entry->cb ? 'C' : '.',
		  __entry->nr ? 'S' : '.',
		  __entry->iit ? 'I' : '.',
		  __entry->risk ? 'R' : '.')
);

/*
 * Tracepoint for rcutorture readers.  The first argument is the name
 * of the RCU flavor from rcutorture's viewpoint and the second argument
 * is the callback address.
 */
TRACE_EVENT(rcu_torture_read,

	TP_PROTO(const char *rcutorturename, struct rcu_head *rhp,
		 unsigned long secs, unsigned long c_old, unsigned long c),

	TP_ARGS(rcutorturename, rhp, secs, c_old, c),

	TP_STRUCT__entry(
		__field(const char *, rcutorturename)
		__field(struct rcu_head *, rhp)
		__field(unsigned long, secs)
		__field(unsigned long, c_old)
		__field(unsigned long, c)
	),

	TP_fast_assign(
		__entry->rcutorturename = rcutorturename;
		__entry->rhp = rhp;
		__entry->secs = secs;
		__entry->c_old = c_old;
		__entry->c = c;
	),

	TP_printk("%s torture read %p %luus c: %lu %lu",
		  __entry->rcutorturename, __entry->rhp,
		  __entry->secs, __entry->c_old, __entry->c)
);

/*
 * Tracepoint for _rcu_barrier() execution.  The string "s" describes
 * the _rcu_barrier phase:
 *	"Begin": rcu_barrier_callback() started.
 *	"Check": rcu_barrier_callback() checking for piggybacking.
 *	"EarlyExit": rcu_barrier_callback() piggybacked, thus early exit.
 *	"Inc1": rcu_barrier_callback() piggyback check counter incremented.
 *	"Offline": rcu_barrier_callback() found offline CPU
 *	"OnlineNoCB": rcu_barrier_callback() found online no-CBs CPU.
 *	"OnlineQ": rcu_barrier_callback() found online CPU with callbacks.
 *	"OnlineNQ": rcu_barrier_callback() found online CPU, no callbacks.
 *	"IRQ": An rcu_barrier_callback() callback posted on remote CPU.
 *	"CB": An rcu_barrier_callback() invoked a callback, not the last.
 *	"LastCB": An rcu_barrier_callback() invoked the last callback.
 *	"Inc2": rcu_barrier_callback() piggyback check counter incremented.
 * The "cpu" argument is the CPU or -1 if meaningless, the "cnt" argument
 * is the count of remaining callbacks, and "done" is the piggybacking count.
 */
TRACE_EVENT(rcu_barrier,

	TP_PROTO(const char *rcuname, const char *s, int cpu, int cnt, unsigned long done),

	TP_ARGS(rcuname, s, cpu, cnt, done),

	TP_STRUCT__entry(
		__field(const char *, rcuname)
		__field(const char *, s)
		__field(int, cpu)
		__field(int, cnt)
		__field(unsigned long, done)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->s = s;
		__entry->cpu = cpu;
		__entry->cnt = cnt;
		__entry->done = done;
	),

	TP_printk("%s %s cpu %d remaining %d # %lu",
		  __entry->rcuname, __entry->s, __entry->cpu, __entry->cnt,
		  __entry->done)
);

#else /* #ifdef CONFIG_RCU_TRACE */

#define trace_rcu_grace_period(rcuname, gpnum, gpevent) do { } while (0)
#define trace_rcu_grace_period_init(rcuname, gpnum, level, grplo, grphi, \
				    qsmask) do { } while (0)
#define trace_rcu_future_grace_period(rcuname, gpnum, completed, c, \
				      level, grplo, grphi, event) \
				      do { } while (0)
#define trace_rcu_nocb_wake(rcuname, cpu, reason) do { } while (0)
#define trace_rcu_preempt_task(rcuname, pid, gpnum) do { } while (0)
#define trace_rcu_unlock_preempted_task(rcuname, gpnum, pid) do { } while (0)
#define trace_rcu_quiescent_state_report(rcuname, gpnum, mask, qsmask, level, \
					 grplo, grphi, gp_tasks) do { } \
	while (0)
#define trace_rcu_fqs(rcuname, gpnum, cpu, qsevent) do { } while (0)
#define trace_rcu_dyntick(polarity, oldnesting, newnesting) do { } while (0)
#define trace_rcu_prep_idle(reason) do { } while (0)
#define trace_rcu_callback(rcuname, rhp, qlen_lazy, qlen) do { } while (0)
#define trace_rcu_kfree_callback(rcuname, rhp, offset, qlen_lazy, qlen) \
	do { } while (0)
#define trace_rcu_batch_start(rcuname, qlen_lazy, qlen, blimit) \
	do { } while (0)
#define trace_rcu_invoke_callback(rcuname, rhp) do { } while (0)
#define trace_rcu_invoke_kfree_callback(rcuname, rhp, offset) do { } while (0)
#define trace_rcu_batch_end(rcuname, callbacks_invoked, cb, nr, iit, risk) \
	do { } while (0)
#define trace_rcu_torture_read(rcutorturename, rhp, secs, c_old, c) \
	do { } while (0)
#define trace_rcu_barrier(name, s, cpu, cnt, done) do { } while (0)

#endif /* #else #ifdef CONFIG_RCU_TRACE */

#endif /* _TRACE_RCU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
