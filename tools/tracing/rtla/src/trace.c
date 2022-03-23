// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/sendfile.h>
#include <tracefs.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "trace.h"
#include "utils.h"

/*
 * enable_tracer_by_name - enable a tracer on the given instance
 */
int enable_tracer_by_name(struct tracefs_instance *inst, const char *tracer_name)
{
	enum tracefs_tracers tracer;
	int retval;

	tracer = TRACEFS_TRACER_CUSTOM;

	debug_msg("Enabling %s tracer\n", tracer_name);

	retval = tracefs_tracer_set(inst, tracer, tracer_name);
	if (retval < 0) {
		if (errno == ENODEV)
			err_msg("Tracer %s not found!\n", tracer_name);

		err_msg("Failed to enable the %s tracer\n", tracer_name);
		return -1;
	}

	return 0;
}

/*
 * disable_tracer - set nop tracer to the insta
 */
void disable_tracer(struct tracefs_instance *inst)
{
	enum tracefs_tracers t = TRACEFS_TRACER_NOP;
	int retval;

	retval = tracefs_tracer_set(inst, t);
	if (retval < 0)
		err_msg("Oops, error disabling tracer\n");
}

/*
 * create_instance - create a trace instance with *instance_name
 */
struct tracefs_instance *create_instance(char *instance_name)
{
	return tracefs_instance_create(instance_name);
}

/*
 * destroy_instance - remove a trace instance and free the data
 */
void destroy_instance(struct tracefs_instance *inst)
{
	tracefs_instance_destroy(inst);
	tracefs_instance_free(inst);
}

/*
 * save_trace_to_file - save the trace output of the instance to the file
 */
int save_trace_to_file(struct tracefs_instance *inst, const char *filename)
{
	const char *file = "trace";
	mode_t mode = 0644;
	char buffer[4096];
	int out_fd, in_fd;
	int retval = -1;

	in_fd = tracefs_instance_file_open(inst, file, O_RDONLY);
	if (in_fd < 0) {
		err_msg("Failed to open trace file\n");
		return -1;
	}

	out_fd = creat(filename, mode);
	if (out_fd < 0) {
		err_msg("Failed to create output file %s\n", filename);
		goto out_close_in;
	}

	do {
		retval = read(in_fd, buffer, sizeof(buffer));
		if (retval <= 0)
			goto out_close;

		retval = write(out_fd, buffer, retval);
		if (retval < 0)
			goto out_close;
	} while (retval > 0);

	retval = 0;
out_close:
	close(out_fd);
out_close_in:
	close(in_fd);
	return retval;
}

/*
 * collect_registered_events - call the existing callback function for the event
 *
 * If an event has a registered callback function, call it.
 * Otherwise, ignore the event.
 */
int
collect_registered_events(struct tep_event *event, struct tep_record *record,
			  int cpu, void *context)
{
	struct trace_instance *trace = context;
	struct trace_seq *s = trace->seq;

	if (!event->handler)
		return 0;

	event->handler(s, record, event, context);

	return 0;
}

/*
 * trace_instance_destroy - destroy and free a rtla trace instance
 */
void trace_instance_destroy(struct trace_instance *trace)
{
	if (trace->inst) {
		disable_tracer(trace->inst);
		destroy_instance(trace->inst);
	}

	if (trace->seq)
		free(trace->seq);

	if (trace->tep)
		tep_free(trace->tep);
}

/*
 * trace_instance_init - create an rtla trace instance
 *
 * It is more than the tracefs instance, as it contains other
 * things required for the tracing, such as the local events and
 * a seq file.
 *
 * Note that the trace instance is returned disabled. This allows
 * the tool to apply some other configs, like setting priority
 * to the kernel threads, before starting generating trace entries.
 */
int trace_instance_init(struct trace_instance *trace, char *tool_name)
{
	trace->seq = calloc(1, sizeof(*trace->seq));
	if (!trace->seq)
		goto out_err;

	trace_seq_init(trace->seq);

	trace->inst = create_instance(tool_name);
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

/*
 * trace_instance_start - start tracing a given rtla instance
 */
int trace_instance_start(struct trace_instance *trace)
{
	return tracefs_trace_on(trace->inst);
}
