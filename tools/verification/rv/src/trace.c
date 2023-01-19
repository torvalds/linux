// SPDX-License-Identifier: GPL-2.0
/*
 * trace helpers.
 *
 * Copyright (C) 2022 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <sys/sendfile.h>
#include <tracefs.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <rv.h>
#include <trace.h>
#include <utils.h>

/*
 * create_instance - create a trace instance with *instance_name
 */
static struct tracefs_instance *create_instance(char *instance_name)
{
	return tracefs_instance_create(instance_name);
}

/*
 * destroy_instance - remove a trace instance and free the data
 */
static void destroy_instance(struct tracefs_instance *inst)
{
	tracefs_instance_destroy(inst);
	tracefs_instance_free(inst);
}

/**
 * collect_registered_events - call the existing callback function for the event
 *
 * If an event has a registered callback function, call it.
 * Otherwise, ignore the event.
 *
 * Returns 0 if the event was collected, 1 if the tool should stop collecting trace.
 */
int
collect_registered_events(struct tep_event *event, struct tep_record *record,
			  int cpu, void *context)
{
	struct trace_instance *trace = context;
	struct trace_seq *s = trace->seq;

	if (should_stop())
		return 1;

	if (!event->handler)
		return 0;

	event->handler(s, record, event, context);

	return 0;
}

/**
 * trace_instance_destroy - destroy and free a rv trace instance
 */
void trace_instance_destroy(struct trace_instance *trace)
{
	if (trace->inst) {
		destroy_instance(trace->inst);
		trace->inst = NULL;
	}

	if (trace->seq) {
		free(trace->seq);
		trace->seq = NULL;
	}

	if (trace->tep) {
		tep_free(trace->tep);
		trace->tep = NULL;
	}
}

/**
 * trace_instance_init - create an trace instance
 *
 * It is more than the tracefs instance, as it contains other
 * things required for the tracing, such as the local events and
 * a seq file.
 *
 * Note that the trace instance is returned disabled. This allows
 * the tool to apply some other configs, like setting priority
 * to the kernel threads, before starting generating trace entries.
 *
 * Returns 0 on success, non-zero otherwise.
 */
int trace_instance_init(struct trace_instance *trace, char *name)
{
	trace->seq = calloc(1, sizeof(*trace->seq));
	if (!trace->seq)
		goto out_err;

	trace_seq_init(trace->seq);

	trace->inst = create_instance(name);
	if (!trace->inst)
		goto out_err;

	trace->tep = tracefs_local_events(NULL);
	if (!trace->tep)
		goto out_err;

	/*
	 * Let the main enable the record after setting some other
	 * things such as the priority of the tracer's threads.
	 */
	tracefs_trace_off(trace->inst);

	return 0;

out_err:
	trace_instance_destroy(trace);
	return 1;
}

/**
 * trace_instance_start - start tracing a given rv instance
 *
 * Returns 0 on success, -1 otherwise.
 */
int trace_instance_start(struct trace_instance *trace)
{
	return tracefs_trace_on(trace->inst);
}
