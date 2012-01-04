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

	TP_PROTO(char *s),

	TP_ARGS(s),

	TP_STRUCT__entry(
		__field(char *, s)
	),

	TP_fast_assign(
		__entry->s = s;
	),

	TP_printk("%s", __entry->s)
);

#ifdef CONFIG_RCU_TRACE

#if defined(CONFIG_TREE_RCU) || defined(CONFIG_TREE_PREEMPT_RCU)

/*
 * Tracepoint for grace-period events: starting and ending a grace
 * period ("start" and "end", respectively), a CPU noting the start
 * of a new grace period or the end of an old grace period ("cpustart"
 * and "cpuend", respectively), a CPU passing through a quiescent
 * state ("cpuqs"), a CPU coming online or going offline ("cpuonl"
 * and "cpuofl", respectively), and a CPU being kicked for being too
 * long in dyntick-idle mode ("kick").
 */
TRACE_EVENT(rcu_grace_period,

	TP_PROTO(char *rcuname, unsigned long gpnum, char *gpevent),

	TP_ARGS(rcuname, gpnum, gpevent),

	TP_STRUCT__entry(
		__field(char *, rcuname)
		__field(unsigned long, gpnum)
		__field(char *, gpevent)
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
 * Tracepoint for grace-period-initialization events.  These are
 * distinguished by the type of RCU, the new grace-period number, the
 * rcu_node structure level, the starting and ending CPU covered by the
 * rcu_node structure, and the mask of CPUs that will be waited for.
 * All but the type of RCU are extracted from the rcu_node structure.
 */
TRACE_EVENT(rcu_grace_period_init,

	TP_PROTO(char *rcuname, unsigned long gpnum, u8 level,
		 int grplo, int grphi, unsigned long qsmask),

	TP_ARGS(rcuname, gpnum, level, grplo, grphi, qsmask),

	TP_STRUCT__entry(
		__field(char *, rcuname)
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
 * Tracepoint for tasks blocking within preemptible-RCU read-side
 * critical sections.  Track the type of RCU (which one day might
 * include SRCU), the grace-period number that the task is blocking
 * (the current or the next), and the task's PID.
 */
TRACE_EVENT(rcu_preempt_task,

	TP_PROTO(char *rcuname, int pid, unsigned long gpnum),

	TP_ARGS(rcuname, pid, gpnum),

	TP_STRUCT__entry(
		__field(char *, rcuname)
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

	TP_PROTO(char *rcuname, unsigned long gpnum, int pid),

	TP_ARGS(rcuname, gpnum, pid),

	TP_STRUCT__entry(
		__field(char *, rcuname)
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

	TP_PROTO(char *rcuname, unsigned long gpnum,
		 unsigned long mask, unsigned long qsmask,
		 u8 level, int grplo, int grphi, int gp_tasks),

	TP_ARGS(rcuname, gpnum, mask, qsmask, level, grplo, grphi, gp_tasks),

	TP_STRUCT__entry(
		__field(char *, rcuname)
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

	TP_PROTO(char *rcuname, unsigned long gpnum, int cpu, char *qsevent),

	TP_ARGS(rcuname, gpnum, cpu, qsevent),

	TP_STRUCT__entry(
		__field(char *, rcuname)
		__field(unsigned long, gpnum)
		__field(int, cpu)
		__field(char *, qsevent)
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
 * as argument: "Start" for entering dyntick-idle mode and "End" for
 * leaving it.
 */
TRACE_EVENT(rcu_dyntick,

	TP_PROTO(char *polarity),

	TP_ARGS(polarity),

	TP_STRUCT__entry(
		__field(char *, polarity)
	),

	TP_fast_assign(
		__entry->polarity = polarity;
	),

	TP_printk("%s", __entry->polarity)
);

/*
 * Tracepoint for the registration of a single RCU callback function.
 * The first argument is the type of RCU, the second argument is
 * a pointer to the RCU callback itself, and the third element is the
 * new RCU callback queue length for the current CPU.
 */
TRACE_EVENT(rcu_callback,

	TP_PROTO(char *rcuname, struct rcu_head *rhp, long qlen),

	TP_ARGS(rcuname, rhp, qlen),

	TP_STRUCT__entry(
		__field(char *, rcuname)
		__field(void *, rhp)
		__field(void *, func)
		__field(long, qlen)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->rhp = rhp;
		__entry->func = rhp->func;
		__entry->qlen = qlen;
	),

	TP_printk("%s rhp=%p func=%pf %ld",
		  __entry->rcuname, __entry->rhp, __entry->func, __entry->qlen)
);

/*
 * Tracepoint for the registration of a single RCU callback of the special
 * kfree() form.  The first argument is the RCU type, the second argument
 * is a pointer to the RCU callback, the third argument is the offset
 * of the callback within the enclosing RCU-protected data structure,
 * and the fourth argument is the new RCU callback queue length for the
 * current CPU.
 */
TRACE_EVENT(rcu_kfree_callback,

	TP_PROTO(char *rcuname, struct rcu_head *rhp, unsigned long offset,
		 long qlen),

	TP_ARGS(rcuname, rhp, offset, qlen),

	TP_STRUCT__entry(
		__field(char *, rcuname)
		__field(void *, rhp)
		__field(unsigned long, offset)
		__field(long, qlen)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->rhp = rhp;
		__entry->offset = offset;
		__entry->qlen = qlen;
	),

	TP_printk("%s rhp=%p func=%ld %ld",
		  __entry->rcuname, __entry->rhp, __entry->offset,
		  __entry->qlen)
);

/*
 * Tracepoint for marking the beginning rcu_do_batch, performed to start
 * RCU callback invocation.  The first argument is the RCU flavor,
 * the second is the total number of callbacks (including those that
 * are not yet ready to be invoked), and the third argument is the
 * current RCU-callback batch limit.
 */
TRACE_EVENT(rcu_batch_start,

	TP_PROTO(char *rcuname, long qlen, int blimit),

	TP_ARGS(rcuname, qlen, blimit),

	TP_STRUCT__entry(
		__field(char *, rcuname)
		__field(long, qlen)
		__field(int, blimit)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->qlen = qlen;
		__entry->blimit = blimit;
	),

	TP_printk("%s CBs=%ld bl=%d",
		  __entry->rcuname, __entry->qlen, __entry->blimit)
);

/*
 * Tracepoint for the invocation of a single RCU callback function.
 * The first argument is the type of RCU, and the second argument is
 * a pointer to the RCU callback itself.
 */
TRACE_EVENT(rcu_invoke_callback,

	TP_PROTO(char *rcuname, struct rcu_head *rhp),

	TP_ARGS(rcuname, rhp),

	TP_STRUCT__entry(
		__field(char *, rcuname)
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

	TP_PROTO(char *rcuname, struct rcu_head *rhp, unsigned long offset),

	TP_ARGS(rcuname, rhp, offset),

	TP_STRUCT__entry(
		__field(char *, rcuname)
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
 * invoked.  The first argument is the name of the RCU flavor and
 * the second argument is number of callbacks actually invoked.
 */
TRACE_EVENT(rcu_batch_end,

	TP_PROTO(char *rcuname, int callbacks_invoked),

	TP_ARGS(rcuname, callbacks_invoked),

	TP_STRUCT__entry(
		__field(char *, rcuname)
		__field(int, callbacks_invoked)
	),

	TP_fast_assign(
		__entry->rcuname = rcuname;
		__entry->callbacks_invoked = callbacks_invoked;
	),

	TP_printk("%s CBs-invoked=%d",
		  __entry->rcuname, __entry->callbacks_invoked)
);

#else /* #ifdef CONFIG_RCU_TRACE */

#define trace_rcu_grace_period(rcuname, gpnum, gpevent) do { } while (0)
#define trace_rcu_grace_period_init(rcuname, gpnum, level, grplo, grphi, qsmask) do { } while (0)
#define trace_rcu_preempt_task(rcuname, pid, gpnum) do { } while (0)
#define trace_rcu_unlock_preempted_task(rcuname, gpnum, pid) do { } while (0)
#define trace_rcu_quiescent_state_report(rcuname, gpnum, mask, qsmask, level, grplo, grphi, gp_tasks) do { } while (0)
#define trace_rcu_fqs(rcuname, gpnum, cpu, qsevent) do { } while (0)
#define trace_rcu_dyntick(polarity) do { } while (0)
#define trace_rcu_callback(rcuname, rhp, qlen) do { } while (0)
#define trace_rcu_kfree_callback(rcuname, rhp, offset, qlen) do { } while (0)
#define trace_rcu_batch_start(rcuname, qlen, blimit) do { } while (0)
#define trace_rcu_invoke_callback(rcuname, rhp) do { } while (0)
#define trace_rcu_invoke_kfree_callback(rcuname, rhp, offset) do { } while (0)
#define trace_rcu_batch_end(rcuname, callbacks_invoked) do { } while (0)

#endif /* #else #ifdef CONFIG_RCU_TRACE */

#endif /* _TRACE_RCU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
