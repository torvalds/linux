// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "common.h"

struct trace_instance *trace_inst;
int stop_tracing;

static void stop_trace(int sig)
{
	if (stop_tracing) {
		/*
		 * Stop requested twice in a row; abort event processing and
		 * exit immediately
		 */
		tracefs_iterate_stop(trace_inst->inst);
		return;
	}
	stop_tracing = 1;
	if (trace_inst)
		trace_instance_stop(trace_inst);
}

/*
 * set_signals - handles the signal to stop the tool
 */
static void set_signals(struct common_params *params)
{
	signal(SIGINT, stop_trace);
	if (params->duration) {
		signal(SIGALRM, stop_trace);
		alarm(params->duration);
	}
}

/*
 * common_apply_config - apply common configs to the initialized tool
 */
int
common_apply_config(struct osnoise_tool *tool, struct common_params *params)
{
	int retval, i;

	if (!params->sleep_time)
		params->sleep_time = 1;

	retval = osnoise_set_cpus(tool->context, params->cpus ? params->cpus : "all");
	if (retval) {
		err_msg("Failed to apply CPUs config\n");
		goto out_err;
	}

	if (!params->cpus) {
		for (i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i++)
			CPU_SET(i, &params->monitored_cpus);
	}

	if (params->hk_cpus) {
		retval = sched_setaffinity(getpid(), sizeof(params->hk_cpu_set),
					   &params->hk_cpu_set);
		if (retval == -1) {
			err_msg("Failed to set rtla to the house keeping CPUs\n");
			goto out_err;
		}
	} else if (params->cpus) {
		/*
		 * Even if the user do not set a house-keeping CPU, try to
		 * move rtla to a CPU set different to the one where the user
		 * set the workload to run.
		 *
		 * No need to check results as this is an automatic attempt.
		 */
		auto_house_keeping(&params->monitored_cpus);
	}

	/*
	 * Set workload according to type of thread if the kernel supports it.
	 * On kernels without support, user threads will have already failed
	 * on missing fd, and kernel threads do not need it.
	 */
	retval = osnoise_set_workload(tool->context, params->kernel_workload);
	if (retval < -1) {
		err_msg("Failed to set OSNOISE_WORKLOAD option\n");
		goto out_err;
	}

	return 0;

out_err:
	return -1;
}


int run_tool(struct tool_ops *ops, int argc, char *argv[])
{
	struct common_params *params;
	enum result return_value = ERROR;
	struct osnoise_tool *tool;
	bool stopped;
	int retval;

	params = ops->parse_args(argc, argv);
	if (!params)
		exit(1);

	tool = ops->init_tool(params);
	if (!tool) {
		err_msg("Could not init osnoise tool\n");
		goto out_exit;
	}
	tool->ops = ops;
	tool->params = params;

	/*
	 * Save trace instance into global variable so that SIGINT can stop
	 * the timerlat tracer.
	 * Otherwise, rtla could loop indefinitely when overloaded.
	 */
	trace_inst = &tool->trace;

	retval = ops->apply_config(tool);
	if (retval) {
		err_msg("Could not apply config\n");
		goto out_free;
	}

	retval = enable_tracer_by_name(trace_inst->inst, ops->tracer);
	if (retval) {
		err_msg("Failed to enable %s tracer\n", ops->tracer);
		goto out_free;
	}

	if (params->set_sched) {
		retval = set_comm_sched_attr(ops->comm_prefix, &params->sched_param);
		if (retval) {
			err_msg("Failed to set sched parameters\n");
			goto out_free;
		}
	}

	if (params->cgroup && !params->user_data) {
		retval = set_comm_cgroup(ops->comm_prefix, params->cgroup_name);
		if (!retval) {
			err_msg("Failed to move threads to cgroup\n");
			goto out_free;
		}
	}


	if (params->threshold_actions.present[ACTION_TRACE_OUTPUT] ||
	    params->end_actions.present[ACTION_TRACE_OUTPUT]) {
		tool->record = osnoise_init_trace_tool(ops->tracer);
		if (!tool->record) {
			err_msg("Failed to enable the trace instance\n");
			goto out_free;
		}
		params->threshold_actions.trace_output_inst = tool->record->trace.inst;
		params->end_actions.trace_output_inst = tool->record->trace.inst;

		if (params->events) {
			retval = trace_events_enable(&tool->record->trace, params->events);
			if (retval)
				goto out_trace;
		}

		if (params->buffer_size > 0) {
			retval = trace_set_buffer_size(&tool->record->trace, params->buffer_size);
			if (retval)
				goto out_trace;
		}
	}

	if (params->user_workload) {
		pthread_t user_thread;

		/* rtla asked to stop */
		params->user.should_run = 1;
		/* all threads left */
		params->user.stopped_running = 0;

		params->user.set = &params->monitored_cpus;
		if (params->set_sched)
			params->user.sched_param = &params->sched_param;
		else
			params->user.sched_param = NULL;

		params->user.cgroup_name = params->cgroup_name;

		retval = pthread_create(&user_thread, NULL, timerlat_u_dispatcher, &params->user);
		if (retval)
			err_msg("Error creating timerlat user-space threads\n");
	}

	retval = ops->enable(tool);
	if (retval)
		goto out_trace;

	tool->start_time = time(NULL);
	set_signals(params);

	retval = ops->main(tool);
	if (retval)
		goto out_trace;

	if (params->user_workload && !params->user.stopped_running) {
		params->user.should_run = 0;
		sleep(1);
	}

	ops->print_stats(tool);

	actions_perform(&params->end_actions);

	return_value = PASSED;

	stopped = osnoise_trace_is_off(tool, tool->record) && !stop_tracing;
	if (stopped) {
		printf("%s hit stop tracing\n", ops->tracer);
		return_value = FAILED;
	}

	if (ops->analyze)
		ops->analyze(tool, stopped);

out_trace:
	trace_events_destroy(&tool->record->trace, params->events);
	params->events = NULL;
out_free:
	ops->free(tool);
	osnoise_destroy_tool(tool->record);
	osnoise_destroy_tool(tool);
	actions_destroy(&params->threshold_actions);
	actions_destroy(&params->end_actions);
	free(params);
out_exit:
	exit(return_value);
}

int top_main_loop(struct osnoise_tool *tool)
{
	struct common_params *params = tool->params;
	struct trace_instance *trace = &tool->trace;
	struct osnoise_tool *record = tool->record;
	int retval;

	while (!stop_tracing) {
		sleep(params->sleep_time);

		if (params->aa_only && !osnoise_trace_is_off(tool, record))
			continue;

		retval = tracefs_iterate_raw_events(trace->tep,
						    trace->inst,
						    NULL,
						    0,
						    collect_registered_events,
						    trace);
		if (retval < 0) {
			err_msg("Error iterating on events\n");
			return retval;
		}

		if (!params->quiet)
			tool->ops->print_stats(tool);

		if (osnoise_trace_is_off(tool, record)) {
			actions_perform(&params->threshold_actions);

			if (!params->threshold_actions.continue_flag)
				/* continue flag not set, break */
				return 0;

			/* continue action reached, re-enable tracing */
			if (record)
				trace_instance_start(&record->trace);
			if (tool->aa)
				trace_instance_start(&tool->aa->trace);
			trace_instance_start(trace);
		}

		/* is there still any user-threads ? */
		if (params->user_workload) {
			if (params->user.stopped_running) {
				debug_msg("timerlat user space threads stopped!\n");
				break;
			}
		}
	}

	return 0;
}

int hist_main_loop(struct osnoise_tool *tool)
{
	struct common_params *params = tool->params;
	struct trace_instance *trace = &tool->trace;
	int retval = 0;

	while (!stop_tracing) {
		sleep(params->sleep_time);

		retval = tracefs_iterate_raw_events(trace->tep,
						    trace->inst,
						    NULL,
						    0,
						    collect_registered_events,
						    trace);
		if (retval < 0) {
			err_msg("Error iterating on events\n");
			break;
		}

		if (osnoise_trace_is_off(tool, tool->record)) {
			actions_perform(&params->threshold_actions);

			if (!params->threshold_actions.continue_flag) {
				/* continue flag not set, break */
				break;

				/* continue action reached, re-enable tracing */
				if (tool->record)
					trace_instance_start(&tool->record->trace);
				if (tool->aa)
					trace_instance_start(&tool->aa->trace);
				trace_instance_start(&tool->trace);
			}
			break;
		}

		/* is there still any user-threads ? */
		if (params->user_workload) {
			if (params->user.stopped_running) {
				debug_msg("user-space threads stopped!\n");
				break;
			}
		}
	}

	return retval;
}
