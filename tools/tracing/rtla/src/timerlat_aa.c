// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <stdlib.h>
#include <errno.h>
#include "utils.h"
#include "osnoise.h"
#include "timerlat.h"
#include <unistd.h>

enum timelat_state {
	TIMERLAT_INIT = 0,
	TIMERLAT_WAITING_IRQ,
	TIMERLAT_WAITING_THREAD,
};

/* Used to fill spaces in the output */
static const char *spaces  = "                                                         ";

#define MAX_COMM		24

/*
 * Per-cpu data statistics and data.
 */
struct timerlat_aa_data {
	/* Current CPU state */
	int			curr_state;

	/* timerlat IRQ latency */
	unsigned long long	tlat_irq_seqnum;
	unsigned long long	tlat_irq_latency;
	unsigned long long	tlat_irq_timstamp;

	/* timerlat Thread latency */
	unsigned long long	tlat_thread_seqnum;
	unsigned long long	tlat_thread_latency;
	unsigned long long	tlat_thread_timstamp;

	/*
	 * Information about the thread running when the IRQ
	 * arrived.
	 *
	 * This can be blocking or interference, depending on the
	 * priority of the thread. Assuming timerlat is the highest
	 * prio, it is blocking. If timerlat has a lower prio, it is
	 * interference.
	 * note: "unsigned long long" because they are fetch using tep_get_field_val();
	 */
	unsigned long long	run_thread_pid;
	char			run_thread_comm[MAX_COMM];
	unsigned long long	thread_blocking_duration;
	unsigned long long	max_exit_idle_latency;

	/* Information about the timerlat timer irq */
	unsigned long long	timer_irq_start_time;
	unsigned long long	timer_irq_start_delay;
	unsigned long long	timer_irq_duration;
	unsigned long long	timer_exit_from_idle;

	/*
	 * Information about the last IRQ before the timerlat irq
	 * arrived.
	 *
	 * If now - timestamp is <= latency, it might have influenced
	 * in the timerlat irq latency. Otherwise, ignore it.
	 */
	unsigned long long	prev_irq_duration;
	unsigned long long	prev_irq_timstamp;

	/*
	 * Interference sum.
	 */
	unsigned long long	thread_nmi_sum;
	unsigned long long	thread_irq_sum;
	unsigned long long	thread_softirq_sum;
	unsigned long long	thread_thread_sum;

	/*
	 * Interference task information.
	 */
	struct trace_seq	*prev_irqs_seq;
	struct trace_seq	*nmi_seq;
	struct trace_seq	*irqs_seq;
	struct trace_seq	*softirqs_seq;
	struct trace_seq	*threads_seq;
	struct trace_seq	*stack_seq;

	/*
	 * Current thread.
	 */
	char			current_comm[MAX_COMM];
	unsigned long long	current_pid;

	/*
	 * Is the system running a kworker?
	 */
	unsigned long long	kworker;
	unsigned long long	kworker_func;
};

/*
 * The analysis context and system wide view
 */
struct timerlat_aa_context {
	int nr_cpus;
	int dump_tasks;

	/* per CPU data */
	struct timerlat_aa_data *taa_data;

	/*
	 * required to translate function names and register
	 * events.
	 */
	struct osnoise_tool *tool;
};

/*
 * The data is stored as a local variable, but accessed via a helper function.
 *
 * It could be stored inside the trace context. But every access would
 * require container_of() + a series of pointers. Do we need it? Not sure.
 *
 * For now keep it simple. If needed, store it in the tool, add the *context
 * as a parameter in timerlat_aa_get_ctx() and do the magic there.
 */
static struct timerlat_aa_context *__timerlat_aa_ctx;

static struct timerlat_aa_context *timerlat_aa_get_ctx(void)
{
	return __timerlat_aa_ctx;
}

/*
 * timerlat_aa_get_data - Get the per-cpu data from the timerlat context
 */
static struct timerlat_aa_data
*timerlat_aa_get_data(struct timerlat_aa_context *taa_ctx, int cpu)
{
	return &taa_ctx->taa_data[cpu];
}

/*
 * timerlat_aa_irq_latency - Handles timerlat IRQ event
 */
static int timerlat_aa_irq_latency(struct timerlat_aa_data *taa_data,
				   struct trace_seq *s, struct tep_record *record,
				   struct tep_event *event)
{
	/*
	 * For interference, we start now looking for things that can delay
	 * the thread.
	 */
	taa_data->curr_state = TIMERLAT_WAITING_THREAD;
	taa_data->tlat_irq_timstamp = record->ts;

	/*
	 * Zero values.
	 */
	taa_data->thread_nmi_sum = 0;
	taa_data->thread_irq_sum = 0;
	taa_data->thread_softirq_sum = 0;
	taa_data->thread_thread_sum = 0;
	taa_data->thread_blocking_duration = 0;
	taa_data->timer_irq_start_time = 0;
	taa_data->timer_irq_duration = 0;
	taa_data->timer_exit_from_idle = 0;

	/*
	 * Zero interference tasks.
	 */
	trace_seq_reset(taa_data->nmi_seq);
	trace_seq_reset(taa_data->irqs_seq);
	trace_seq_reset(taa_data->softirqs_seq);
	trace_seq_reset(taa_data->threads_seq);

	/* IRQ latency values */
	tep_get_field_val(s, event, "timer_latency", record, &taa_data->tlat_irq_latency, 1);
	tep_get_field_val(s, event, "seqnum", record, &taa_data->tlat_irq_seqnum, 1);

	/* The thread that can cause blocking */
	tep_get_common_field_val(s, event, "common_pid", record, &taa_data->run_thread_pid, 1);

	/*
	 * Get exit from idle case.
	 *
	 * If it is not idle thread:
	 */
	if (taa_data->run_thread_pid)
		return 0;

	/*
	 * if the latency is shorter than the known exit from idle:
	 */
	if (taa_data->tlat_irq_latency < taa_data->max_exit_idle_latency)
		return 0;

	/*
	 * To be safe, ignore the cases in which an IRQ/NMI could have
	 * interfered with the timerlat IRQ.
	 */
	if (taa_data->tlat_irq_timstamp - taa_data->tlat_irq_latency
	    < taa_data->prev_irq_timstamp + taa_data->prev_irq_duration)
		return 0;

	taa_data->max_exit_idle_latency = taa_data->tlat_irq_latency;

	return 0;
}

/*
 * timerlat_aa_thread_latency - Handles timerlat thread event
 */
static int timerlat_aa_thread_latency(struct timerlat_aa_data *taa_data,
				      struct trace_seq *s, struct tep_record *record,
				      struct tep_event *event)
{
	/*
	 * For interference, we start now looking for things that can delay
	 * the IRQ of the next cycle.
	 */
	taa_data->curr_state = TIMERLAT_WAITING_IRQ;
	taa_data->tlat_thread_timstamp = record->ts;

	/* Thread latency values */
	tep_get_field_val(s, event, "timer_latency", record, &taa_data->tlat_thread_latency, 1);
	tep_get_field_val(s, event, "seqnum", record, &taa_data->tlat_thread_seqnum, 1);

	return 0;
}

/*
 * timerlat_aa_handler - Handle timerlat events
 *
 * This function is called to handle timerlat events recording statistics.
 *
 * Returns 0 on success, -1 otherwise.
 */
static int timerlat_aa_handler(struct trace_seq *s, struct tep_record *record,
			struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	unsigned long long thread;

	if (!taa_data)
		return -1;

	tep_get_field_val(s, event, "context", record, &thread, 1);
	if (!thread)
		return timerlat_aa_irq_latency(taa_data, s, record, event);
	else
		return timerlat_aa_thread_latency(taa_data, s, record, event);
}

/*
 * timerlat_aa_nmi_handler - Handles NMI noise
 *
 * It is used to collect information about interferences from NMI. It is
 * hooked to the osnoise:nmi_noise event.
 */
static int timerlat_aa_nmi_handler(struct trace_seq *s, struct tep_record *record,
				   struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	unsigned long long duration;
	unsigned long long start;

	tep_get_field_val(s, event, "duration", record, &duration, 1);
	tep_get_field_val(s, event, "start", record, &start, 1);

	if (taa_data->curr_state == TIMERLAT_WAITING_IRQ) {
		taa_data->prev_irq_duration = duration;
		taa_data->prev_irq_timstamp = start;

		trace_seq_reset(taa_data->prev_irqs_seq);
		trace_seq_printf(taa_data->prev_irqs_seq, "  %24s %.*s %9.2f us\n",
				 "nmi",
				 24, spaces,
				 ns_to_usf(duration));
		return 0;
	}

	taa_data->thread_nmi_sum += duration;
	trace_seq_printf(taa_data->nmi_seq, "  %24s %.*s %9.2f us\n",
			 "nmi",
			 24, spaces, ns_to_usf(duration));

	return 0;
}

/*
 * timerlat_aa_irq_handler - Handles IRQ noise
 *
 * It is used to collect information about interferences from IRQ. It is
 * hooked to the osnoise:irq_noise event.
 *
 * It is a little bit more complex than the other because it measures:
 *	- The IRQs that can delay the timer IRQ before it happened.
 *	- The Timerlat IRQ handler
 *	- The IRQs that happened between the timerlat IRQ and the timerlat thread
 *	  (IRQ interference).
 */
static int timerlat_aa_irq_handler(struct trace_seq *s, struct tep_record *record,
				   struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	unsigned long long expected_start;
	unsigned long long duration;
	unsigned long long vector;
	unsigned long long start;
	char *desc;
	int val;

	tep_get_field_val(s, event, "duration", record, &duration, 1);
	tep_get_field_val(s, event, "start", record, &start, 1);
	tep_get_field_val(s, event, "vector", record, &vector, 1);
	desc = tep_get_field_raw(s, event, "desc", record, &val, 1);

	/*
	 * Before the timerlat IRQ.
	 */
	if (taa_data->curr_state == TIMERLAT_WAITING_IRQ) {
		taa_data->prev_irq_duration = duration;
		taa_data->prev_irq_timstamp = start;

		trace_seq_reset(taa_data->prev_irqs_seq);
		trace_seq_printf(taa_data->prev_irqs_seq, "  %24s:%-3llu %.*s %9.2f us\n",
				 desc, vector,
				 15, spaces,
				 ns_to_usf(duration));
		return 0;
	}

	/*
	 * The timerlat IRQ: taa_data->timer_irq_start_time is zeroed at
	 * the timerlat irq handler.
	 */
	if (!taa_data->timer_irq_start_time) {
		expected_start = taa_data->tlat_irq_timstamp - taa_data->tlat_irq_latency;

		taa_data->timer_irq_start_time = start;
		taa_data->timer_irq_duration = duration;

		/*
		 * We are dealing with two different clock sources: the
		 * external clock source that timerlat uses as a reference
		 * and the clock used by the tracer. There are also two
		 * moments: the time reading the clock and the timer in
		 * which the event is placed in the buffer (the trace
		 * event timestamp). If the processor is slow or there
		 * is some hardware noise, the difference between the
		 * timestamp and the external clock read can be longer
		 * than the IRQ handler delay, resulting in a negative
		 * time. If so, set IRQ start delay as 0. In the end,
		 * it is less relevant than the noise.
		 */
		if (expected_start < taa_data->timer_irq_start_time)
			taa_data->timer_irq_start_delay = taa_data->timer_irq_start_time - expected_start;
		else
			taa_data->timer_irq_start_delay = 0;

		/*
		 * not exit from idle.
		 */
		if (taa_data->run_thread_pid)
			return 0;

		if (expected_start > taa_data->prev_irq_timstamp + taa_data->prev_irq_duration)
			taa_data->timer_exit_from_idle = taa_data->timer_irq_start_delay;

		return 0;
	}

	/*
	 * IRQ interference.
	 */
	taa_data->thread_irq_sum += duration;
	trace_seq_printf(taa_data->irqs_seq, "  %24s:%-3llu %.*s %9.2f us\n",
			 desc, vector,
			 24, spaces,
			 ns_to_usf(duration));

	return 0;
}

static char *softirq_name[] = { "HI", "TIMER",	"NET_TX", "NET_RX", "BLOCK",
				"IRQ_POLL", "TASKLET", "SCHED", "HRTIMER", "RCU" };


/*
 * timerlat_aa_softirq_handler - Handles Softirq noise
 *
 * It is used to collect information about interferences from Softirq. It is
 * hooked to the osnoise:softirq_noise event.
 *
 * It is only printed in the non-rt kernel, as softirqs become thread on RT.
 */
static int timerlat_aa_softirq_handler(struct trace_seq *s, struct tep_record *record,
				       struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	unsigned long long duration;
	unsigned long long vector;
	unsigned long long start;

	if (taa_data->curr_state == TIMERLAT_WAITING_IRQ)
		return 0;

	tep_get_field_val(s, event, "duration", record, &duration, 1);
	tep_get_field_val(s, event, "start", record, &start, 1);
	tep_get_field_val(s, event, "vector", record, &vector, 1);

	taa_data->thread_softirq_sum += duration;

	trace_seq_printf(taa_data->softirqs_seq, "  %24s:%-3llu %.*s %9.2f us\n",
			 softirq_name[vector], vector,
			 24, spaces,
			 ns_to_usf(duration));
	return 0;
}

/*
 * timerlat_aa_softirq_handler - Handles thread noise
 *
 * It is used to collect information about interferences from threads. It is
 * hooked to the osnoise:thread_noise event.
 *
 * Note: if you see thread noise, your timerlat thread was not the highest prio one.
 */
static int timerlat_aa_thread_handler(struct trace_seq *s, struct tep_record *record,
				      struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	unsigned long long duration;
	unsigned long long start;
	unsigned long long pid;
	const char *comm;
	int val;

	if (taa_data->curr_state == TIMERLAT_WAITING_IRQ)
		return 0;

	tep_get_field_val(s, event, "duration", record, &duration, 1);
	tep_get_field_val(s, event, "start", record, &start, 1);

	tep_get_common_field_val(s, event, "common_pid", record, &pid, 1);
	comm = tep_get_field_raw(s, event, "comm", record, &val, 1);

	if (pid == taa_data->run_thread_pid && !taa_data->thread_blocking_duration) {
		taa_data->thread_blocking_duration = duration;

		if (comm)
			strncpy(taa_data->run_thread_comm, comm, MAX_COMM);
		else
			sprintf(taa_data->run_thread_comm, "<...>");

	} else {
		taa_data->thread_thread_sum += duration;

		trace_seq_printf(taa_data->threads_seq, "  %24s:%-12llu %.*s %9.2f us\n",
				 comm, pid,
				 15, spaces,
				 ns_to_usf(duration));
	}

	return 0;
}

/*
 * timerlat_aa_stack_handler - Handles timerlat IRQ stack trace
 *
 * Saves and parse the stack trace generated by the timerlat IRQ.
 */
static int timerlat_aa_stack_handler(struct trace_seq *s, struct tep_record *record,
			      struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	unsigned long *caller;
	const char *function;
	int val, i;

	trace_seq_reset(taa_data->stack_seq);

	trace_seq_printf(taa_data->stack_seq, "    Blocking thread stack trace\n");
	caller = tep_get_field_raw(s, event, "caller", record, &val, 1);
	if (caller) {
		for (i = 0; ; i++) {
			function = tep_find_function(taa_ctx->tool->trace.tep, caller[i]);
			if (!function)
				break;
			trace_seq_printf(taa_data->stack_seq, " %.*s -> %s\n",
					 14, spaces, function);
		}
	}
	return 0;
}

/*
 * timerlat_aa_sched_switch_handler - Tracks the current thread running on the CPU
 *
 * Handles the sched:sched_switch event to trace the current thread running on the
 * CPU. It is used to display the threads running on the other CPUs when the trace
 * stops.
 */
static int timerlat_aa_sched_switch_handler(struct trace_seq *s, struct tep_record *record,
					    struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);
	const char *comm;
	int val;

	tep_get_field_val(s, event, "next_pid", record, &taa_data->current_pid, 1);
	comm = tep_get_field_raw(s, event, "next_comm", record, &val, 1);

	strncpy(taa_data->current_comm, comm, MAX_COMM);

	/*
	 * If this was a kworker, clean the last kworkers that ran.
	 */
	taa_data->kworker = 0;
	taa_data->kworker_func = 0;

	return 0;
}

/*
 * timerlat_aa_kworker_start_handler - Tracks a kworker running on the CPU
 *
 * Handles workqueue:workqueue_execute_start event, keeping track of
 * the job that a kworker could be doing in the CPU.
 *
 * We already catch problems of hardware related latencies caused by work queues
 * running driver code that causes hardware stall. For example, with DRM drivers.
 */
static int timerlat_aa_kworker_start_handler(struct trace_seq *s, struct tep_record *record,
					     struct tep_event *event, void *context)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	struct timerlat_aa_data *taa_data = timerlat_aa_get_data(taa_ctx, record->cpu);

	tep_get_field_val(s, event, "work", record, &taa_data->kworker, 1);
	tep_get_field_val(s, event, "function", record, &taa_data->kworker_func, 1);
	return 0;
}

/*
 * timerlat_thread_analysis - Prints the analysis of a CPU that hit a stop tracing
 *
 * This is the core of the analysis.
 */
static void timerlat_thread_analysis(struct timerlat_aa_data *taa_data, int cpu,
				     int irq_thresh, int thread_thresh)
{
	long long exp_irq_ts;
	int total;
	int irq;

	/*
	 * IRQ latency or Thread latency?
	 */
	if (taa_data->tlat_irq_seqnum > taa_data->tlat_thread_seqnum) {
		irq = 1;
		total = taa_data->tlat_irq_latency;
	} else {
		irq = 0;
		total = taa_data->tlat_thread_latency;
	}

	/*
	 * Expected IRQ arrival time using the trace clock as the base.
	 *
	 * TODO: Add a list of previous IRQ, and then run the list backwards.
	 */
	exp_irq_ts = taa_data->timer_irq_start_time - taa_data->timer_irq_start_delay;
	if (exp_irq_ts < taa_data->prev_irq_timstamp + taa_data->prev_irq_duration) {
		if (taa_data->prev_irq_timstamp < taa_data->timer_irq_start_time)
			printf("  Previous IRQ interference: %.*s up to  %9.2f us\n",
			       16, spaces,
			       ns_to_usf(taa_data->prev_irq_duration));
	}

	/*
	 * The delay that the IRQ suffered before starting.
	 */
	printf("  IRQ handler delay: %.*s %16s  %9.2f us (%.2f %%)\n", 16, spaces,
	       (ns_to_usf(taa_data->timer_exit_from_idle) > 10) ? "(exit from idle)" : "",
	       ns_to_usf(taa_data->timer_irq_start_delay),
	       ns_to_per(total, taa_data->timer_irq_start_delay));

	/*
	 * Timerlat IRQ.
	 */
	printf("  IRQ latency: %.*s %9.2f us\n", 40, spaces,
	       ns_to_usf(taa_data->tlat_irq_latency));

	if (irq) {
		/*
		 * If the trace stopped due to IRQ, the other events will not happen
		 * because... the trace stopped :-).
		 *
		 * That is all folks, the stack trace was printed before the stop,
		 * so it will be displayed, it is the key.
		 */
		printf("  Blocking thread:\n");
		printf(" %.*s %24s:%-9llu\n", 6, spaces, taa_data->run_thread_comm,
		       taa_data->run_thread_pid);
	} else  {
		/*
		 * The duration of the IRQ handler that handled the timerlat IRQ.
		 */
		printf("  Timerlat IRQ duration: %.*s %9.2f us (%.2f %%)\n",
		       30, spaces,
		       ns_to_usf(taa_data->timer_irq_duration),
		       ns_to_per(total, taa_data->timer_irq_duration));

		/*
		 * The amount of time that the current thread postponed the scheduler.
		 *
		 * Recalling that it is net from NMI/IRQ/Softirq interference, so there
		 * is no need to compute values here.
		 */
		printf("  Blocking thread: %.*s %9.2f us (%.2f %%)\n", 36, spaces,
		       ns_to_usf(taa_data->thread_blocking_duration),
		       ns_to_per(total, taa_data->thread_blocking_duration));

		printf(" %.*s %24s:%-9llu %.*s %9.2f us\n", 6, spaces,
		       taa_data->run_thread_comm, taa_data->run_thread_pid,
		       12, spaces, ns_to_usf(taa_data->thread_blocking_duration));
	}

	/*
	 * Print the stack trace!
	 */
	trace_seq_do_printf(taa_data->stack_seq);

	/*
	 * NMIs can happen during the IRQ, so they are always possible.
	 */
	if (taa_data->thread_nmi_sum)
		printf("  NMI interference %.*s %9.2f us (%.2f %%)\n", 36, spaces,
		       ns_to_usf(taa_data->thread_nmi_sum),
		       ns_to_per(total, taa_data->thread_nmi_sum));

	/*
	 * If it is an IRQ latency, the other factors can be skipped.
	 */
	if (irq)
		goto print_total;

	/*
	 * Prints the interference caused by IRQs to the thread latency.
	 */
	if (taa_data->thread_irq_sum) {
		printf("  IRQ interference %.*s %9.2f us (%.2f %%)\n", 36, spaces,
		       ns_to_usf(taa_data->thread_irq_sum),
		       ns_to_per(total, taa_data->thread_irq_sum));

		trace_seq_do_printf(taa_data->irqs_seq);
	}

	/*
	 * Prints the interference caused by Softirqs to the thread latency.
	 */
	if (taa_data->thread_softirq_sum) {
		printf("  Softirq interference %.*s %9.2f us (%.2f %%)\n", 32, spaces,
		       ns_to_usf(taa_data->thread_softirq_sum),
		       ns_to_per(total, taa_data->thread_softirq_sum));

		trace_seq_do_printf(taa_data->softirqs_seq);
	}

	/*
	 * Prints the interference caused by other threads to the thread latency.
	 *
	 * If this happens, your timerlat is not the highest prio. OK, migration
	 * thread can happen. But otherwise, you are not measuring the "scheduling
	 * latency" only, and here is the difference from scheduling latency and
	 * timer handling latency.
	 */
	if (taa_data->thread_thread_sum) {
		printf("  Thread interference %.*s %9.2f us (%.2f %%)\n", 33, spaces,
		       ns_to_usf(taa_data->thread_thread_sum),
		       ns_to_per(total, taa_data->thread_thread_sum));

		trace_seq_do_printf(taa_data->threads_seq);
	}

	/*
	 * Done.
	 */
print_total:
	printf("------------------------------------------------------------------------\n");
	printf("  %s latency: %.*s %9.2f us (100%%)\n", irq ? "   IRQ" : "Thread",
	       37, spaces, ns_to_usf(total));
}

static int timerlat_auto_analysis_collect_trace(struct timerlat_aa_context *taa_ctx)
{
	struct trace_instance *trace = &taa_ctx->tool->trace;
	int retval;

	retval = tracefs_iterate_raw_events(trace->tep,
					    trace->inst,
					    NULL,
					    0,
					    collect_registered_events,
					    trace);
		if (retval < 0) {
			err_msg("Error iterating on events\n");
			return 0;
		}

	return 1;
}

/**
 * timerlat_auto_analysis - Analyze the collected data
 */
void timerlat_auto_analysis(int irq_thresh, int thread_thresh)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();
	unsigned long long max_exit_from_idle = 0;
	struct timerlat_aa_data *taa_data;
	int max_exit_from_idle_cpu;
	struct tep_handle *tep;
	int cpu;

	timerlat_auto_analysis_collect_trace(taa_ctx);

	/* bring stop tracing to the ns scale */
	irq_thresh = irq_thresh * 1000;
	thread_thresh = thread_thresh * 1000;

	for (cpu = 0; cpu < taa_ctx->nr_cpus; cpu++) {
		taa_data = timerlat_aa_get_data(taa_ctx, cpu);

		if (irq_thresh && taa_data->tlat_irq_latency >= irq_thresh) {
			printf("## CPU %d hit stop tracing, analyzing it ##\n", cpu);
			timerlat_thread_analysis(taa_data, cpu, irq_thresh, thread_thresh);
		} else if (thread_thresh && (taa_data->tlat_thread_latency) >= thread_thresh) {
			printf("## CPU %d hit stop tracing, analyzing it ##\n", cpu);
			timerlat_thread_analysis(taa_data, cpu, irq_thresh, thread_thresh);
		}

		if (taa_data->max_exit_idle_latency > max_exit_from_idle) {
			max_exit_from_idle = taa_data->max_exit_idle_latency;
			max_exit_from_idle_cpu = cpu;
		}

	}

	if (max_exit_from_idle) {
		printf("\n");
		printf("Max timerlat IRQ latency from idle: %.2f us in cpu %d\n",
			ns_to_usf(max_exit_from_idle), max_exit_from_idle_cpu);
	}
	if (!taa_ctx->dump_tasks)
		return;

	printf("\n");
	printf("Printing CPU tasks:\n");
	for (cpu = 0; cpu < taa_ctx->nr_cpus; cpu++) {
		taa_data = timerlat_aa_get_data(taa_ctx, cpu);
		tep = taa_ctx->tool->trace.tep;

		printf("    [%.3d] %24s:%llu", cpu, taa_data->current_comm, taa_data->current_pid);

		if (taa_data->kworker_func)
			printf(" kworker:%s:%s",
				tep_find_function(tep, taa_data->kworker) ? : "<...>",
				tep_find_function(tep, taa_data->kworker_func));
		printf("\n");
	}

}

/*
 * timerlat_aa_destroy_seqs - Destroy seq files used to store parsed data
 */
static void timerlat_aa_destroy_seqs(struct timerlat_aa_context *taa_ctx)
{
	struct timerlat_aa_data *taa_data;
	int i;

	if (!taa_ctx->taa_data)
		return;

	for (i = 0; i < taa_ctx->nr_cpus; i++) {
		taa_data = timerlat_aa_get_data(taa_ctx, i);

		if (taa_data->prev_irqs_seq) {
			trace_seq_destroy(taa_data->prev_irqs_seq);
			free(taa_data->prev_irqs_seq);
		}

		if (taa_data->nmi_seq) {
			trace_seq_destroy(taa_data->nmi_seq);
			free(taa_data->nmi_seq);
		}

		if (taa_data->irqs_seq) {
			trace_seq_destroy(taa_data->irqs_seq);
			free(taa_data->irqs_seq);
		}

		if (taa_data->softirqs_seq) {
			trace_seq_destroy(taa_data->softirqs_seq);
			free(taa_data->softirqs_seq);
		}

		if (taa_data->threads_seq) {
			trace_seq_destroy(taa_data->threads_seq);
			free(taa_data->threads_seq);
		}

		if (taa_data->stack_seq) {
			trace_seq_destroy(taa_data->stack_seq);
			free(taa_data->stack_seq);
		}
	}
}

/*
 * timerlat_aa_init_seqs - Init seq files used to store parsed information
 *
 * Instead of keeping data structures to store raw data, use seq files to
 * store parsed data.
 *
 * Allocates and initialize seq files.
 *
 * Returns 0 on success, -1 otherwise.
 */
static int timerlat_aa_init_seqs(struct timerlat_aa_context *taa_ctx)
{
	struct timerlat_aa_data *taa_data;
	int i;

	for (i = 0; i < taa_ctx->nr_cpus; i++) {

		taa_data = timerlat_aa_get_data(taa_ctx, i);

		taa_data->prev_irqs_seq = calloc(1, sizeof(*taa_data->prev_irqs_seq));
		if (!taa_data->prev_irqs_seq)
			goto out_err;

		trace_seq_init(taa_data->prev_irqs_seq);

		taa_data->nmi_seq = calloc(1, sizeof(*taa_data->nmi_seq));
		if (!taa_data->nmi_seq)
			goto out_err;

		trace_seq_init(taa_data->nmi_seq);

		taa_data->irqs_seq = calloc(1, sizeof(*taa_data->irqs_seq));
		if (!taa_data->irqs_seq)
			goto out_err;

		trace_seq_init(taa_data->irqs_seq);

		taa_data->softirqs_seq = calloc(1, sizeof(*taa_data->softirqs_seq));
		if (!taa_data->softirqs_seq)
			goto out_err;

		trace_seq_init(taa_data->softirqs_seq);

		taa_data->threads_seq = calloc(1, sizeof(*taa_data->threads_seq));
		if (!taa_data->threads_seq)
			goto out_err;

		trace_seq_init(taa_data->threads_seq);

		taa_data->stack_seq = calloc(1, sizeof(*taa_data->stack_seq));
		if (!taa_data->stack_seq)
			goto out_err;

		trace_seq_init(taa_data->stack_seq);
	}

	return 0;

out_err:
	timerlat_aa_destroy_seqs(taa_ctx);
	return -1;
}

/*
 * timerlat_aa_unregister_events - Unregister events used in the auto-analysis
 */
static void timerlat_aa_unregister_events(struct osnoise_tool *tool, int dump_tasks)
{

	tep_unregister_event_handler(tool->trace.tep, -1, "ftrace", "timerlat",
				     timerlat_aa_handler, tool);

	tracefs_event_disable(tool->trace.inst, "osnoise", NULL);

	tep_unregister_event_handler(tool->trace.tep, -1, "osnoise", "nmi_noise",
				     timerlat_aa_nmi_handler, tool);

	tep_unregister_event_handler(tool->trace.tep, -1, "osnoise", "irq_noise",
				     timerlat_aa_irq_handler, tool);

	tep_unregister_event_handler(tool->trace.tep, -1, "osnoise", "softirq_noise",
				     timerlat_aa_softirq_handler, tool);

	tep_unregister_event_handler(tool->trace.tep, -1, "osnoise", "thread_noise",
				     timerlat_aa_thread_handler, tool);

	tep_unregister_event_handler(tool->trace.tep, -1, "ftrace", "kernel_stack",
				     timerlat_aa_stack_handler, tool);
	if (!dump_tasks)
		return;

	tracefs_event_disable(tool->trace.inst, "sched", "sched_switch");
	tep_unregister_event_handler(tool->trace.tep, -1, "sched", "sched_switch",
				     timerlat_aa_sched_switch_handler, tool);

	tracefs_event_disable(tool->trace.inst, "workqueue", "workqueue_execute_start");
	tep_unregister_event_handler(tool->trace.tep, -1, "workqueue", "workqueue_execute_start",
				     timerlat_aa_kworker_start_handler, tool);
}

/*
 * timerlat_aa_register_events - Register events used in the auto-analysis
 *
 * Returns 0 on success, -1 otherwise.
 */
static int timerlat_aa_register_events(struct osnoise_tool *tool, int dump_tasks)
{
	int retval;

	tep_register_event_handler(tool->trace.tep, -1, "ftrace", "timerlat",
				timerlat_aa_handler, tool);


	/*
	 * register auto-analysis handlers.
	 */
	retval = tracefs_event_enable(tool->trace.inst, "osnoise", NULL);
	if (retval < 0 && !errno) {
		err_msg("Could not find osnoise events\n");
		goto out_err;
	}

	tep_register_event_handler(tool->trace.tep, -1, "osnoise", "nmi_noise",
				   timerlat_aa_nmi_handler, tool);

	tep_register_event_handler(tool->trace.tep, -1, "osnoise", "irq_noise",
				   timerlat_aa_irq_handler, tool);

	tep_register_event_handler(tool->trace.tep, -1, "osnoise", "softirq_noise",
				   timerlat_aa_softirq_handler, tool);

	tep_register_event_handler(tool->trace.tep, -1, "osnoise", "thread_noise",
				   timerlat_aa_thread_handler, tool);

	tep_register_event_handler(tool->trace.tep, -1, "ftrace", "kernel_stack",
				   timerlat_aa_stack_handler, tool);

	if (!dump_tasks)
		return 0;

	/*
	 * Dump task events.
	 */
	retval = tracefs_event_enable(tool->trace.inst, "sched", "sched_switch");
	if (retval < 0 && !errno) {
		err_msg("Could not find sched_switch\n");
		goto out_err;
	}

	tep_register_event_handler(tool->trace.tep, -1, "sched", "sched_switch",
				   timerlat_aa_sched_switch_handler, tool);

	retval = tracefs_event_enable(tool->trace.inst, "workqueue", "workqueue_execute_start");
	if (retval < 0 && !errno) {
		err_msg("Could not find workqueue_execute_start\n");
		goto out_err;
	}

	tep_register_event_handler(tool->trace.tep, -1, "workqueue", "workqueue_execute_start",
				   timerlat_aa_kworker_start_handler, tool);

	return 0;

out_err:
	timerlat_aa_unregister_events(tool, dump_tasks);
	return -1;
}

/**
 * timerlat_aa_destroy - Destroy timerlat auto-analysis
 */
void timerlat_aa_destroy(void)
{
	struct timerlat_aa_context *taa_ctx = timerlat_aa_get_ctx();

	if (!taa_ctx)
		return;

	if (!taa_ctx->taa_data)
		goto out_ctx;

	timerlat_aa_unregister_events(taa_ctx->tool, taa_ctx->dump_tasks);
	timerlat_aa_destroy_seqs(taa_ctx);
	free(taa_ctx->taa_data);
out_ctx:
	free(taa_ctx);
}

/**
 * timerlat_aa_init - Initialize timerlat auto-analysis
 *
 * Returns 0 on success, -1 otherwise.
 */
int timerlat_aa_init(struct osnoise_tool *tool, int dump_tasks)
{
	int nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	struct timerlat_aa_context *taa_ctx;
	int retval;

	taa_ctx = calloc(1, sizeof(*taa_ctx));
	if (!taa_ctx)
		return -1;

	__timerlat_aa_ctx = taa_ctx;

	taa_ctx->nr_cpus = nr_cpus;
	taa_ctx->tool = tool;
	taa_ctx->dump_tasks = dump_tasks;

	taa_ctx->taa_data = calloc(nr_cpus, sizeof(*taa_ctx->taa_data));
	if (!taa_ctx->taa_data)
		goto out_err;

	retval = timerlat_aa_init_seqs(taa_ctx);
	if (retval)
		goto out_err;

	retval = timerlat_aa_register_events(tool, dump_tasks);
	if (retval)
		goto out_err;

	return 0;

out_err:
	timerlat_aa_destroy();
	return -1;
}
