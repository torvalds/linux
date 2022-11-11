// SPDX-License-Identifier: GPL-2.0
/*
 * in kernel monitor support: allows rv to control in-kernel monitors.
 *
 * Copyright (C) 2022 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <trace.h>
#include <utils.h>
#include <rv.h>

static int config_has_id;
static int config_my_pid;
static int config_trace;

static char *config_initial_reactor;
static char *config_reactor;

/*
 * __ikm_read_enable - reads monitor's enable status
 *
 * __does not log errors.
 *
 * Returns the current status, or -1 if the monitor does not exist,
 * __hence not logging errors.
 */
static int __ikm_read_enable(char *monitor_name)
{
	char path[MAX_PATH];
	long long enabled;
	int retval;

	snprintf(path, MAX_PATH, "rv/monitors/%s/enable", monitor_name);

	retval = tracefs_instance_file_read_number(NULL, path, &enabled);
	if (retval < 0)
		return -1;

	return enabled;
}

/*
 * ikm_read_enable - reads monitor's enable status
 *
 * Returns the current status, or -1 on error.
 */
static int ikm_read_enable(char *monitor_name)
{
	int enabled;

	enabled = __ikm_read_enable(monitor_name);
	if (enabled < 0) {
		err_msg("ikm: fail read enabled: %d\n", enabled);
		return -1;
	}

	debug_msg("ikm: read enabled: %d\n", enabled);

	return enabled;
}

/*
 * ikm_write_enable - write to the monitor's enable file
 *
 * Return the number of bytes written, -1 on error.
 */
static int ikm_write_enable(char *monitor_name, char *enable_disable)
{
	char path[MAX_PATH];
	int retval;

	debug_msg("ikm: writing enabled: %s\n", enable_disable);

	snprintf(path, MAX_PATH, "rv/monitors/%s/enable", monitor_name);
	retval = tracefs_instance_file_write(NULL, path, enable_disable);
	if (retval < strlen(enable_disable)) {
		err_msg("ikm: writing enabled: %s\n", enable_disable);
		return -1;
	}

	return retval;
}

/*
 * ikm_enable - enable a monitor
 *
 * Returns -1 on failure. Success otherwise.
 */
static int ikm_enable(char *monitor_name)
{
	return ikm_write_enable(monitor_name, "1");
}

/*
 * ikm_disable - disable a monitor
 *
 * Returns -1 on failure. Success otherwise.
 */
static int ikm_disable(char *monitor_name)
{
	return ikm_write_enable(monitor_name, "0");
}

/*
 * ikm_read_desc - read monitors' description
 *
 * Return a dynamically allocated string with the monitor's
 * description, NULL otherwise.
 */
static char *ikm_read_desc(char *monitor_name)
{
	char path[MAX_PATH];
	char *desc;

	snprintf(path, MAX_PATH, "rv/monitors/%s/desc", monitor_name);
	desc = tracefs_instance_file_read(NULL, path, NULL);
	if (!desc) {
		err_msg("ikm: error reading monitor %s desc\n", monitor_name);
		return NULL;
	}

	*strstr(desc, "\n") = '\0';

	return desc;
}

/*
 * ikm_fill_monitor_definition - fill monitor's definition
 *
 * Returns -1 on error, 0 otherwise.
 */
static int ikm_fill_monitor_definition(char *name, struct monitor *ikm)
{
	int enabled;
	char *desc;

	enabled = ikm_read_enable(name);
	if (enabled < 0) {
		err_msg("ikm: monitor %s fail to read enable file, bug?\n", name);
		return -1;
	}

	desc = ikm_read_desc(name);
	if (!desc) {
		err_msg("ikm: monitor %s does not have desc file, bug?\n", name);
		return -1;
	}

	strncpy(ikm->name, name, MAX_DA_NAME_LEN);
	ikm->enabled = enabled;
	strncpy(ikm->desc, desc, MAX_DESCRIPTION);

	free(desc);

	return 0;
}

/*
 * ikm_write_reactor - switch the reactor to *reactor
 *
 * Return the number or characters written, -1 on error.
 */
static int ikm_write_reactor(char *monitor_name, char *reactor)
{
	char path[MAX_PATH];
	int retval;

	snprintf(path, MAX_PATH, "rv/monitors/%s/reactors", monitor_name);
	retval = tracefs_instance_file_write(NULL, path, reactor);
	debug_msg("ikm: write \"%s\" reactors: %d\n", reactor, retval);

	return retval;
}

/*
 * ikm_read_reactor - read the reactors file
 *
 * Returns a dynamically allocated string with monitor's
 * available reactors, or NULL on error.
 */
static char *ikm_read_reactor(char *monitor_name)
{
	char path[MAX_PATH];
	char *reactors;

	snprintf(path, MAX_PATH, "rv/monitors/%s/reactors", monitor_name);
	reactors = tracefs_instance_file_read(NULL, path, NULL);
	if (!reactors) {
		err_msg("ikm: fail reading monitor's %s reactors file\n", monitor_name);
		return NULL;
	}

	return reactors;
}

/*
 * ikm_get_current_reactor - get the current enabled reactor
 *
 * Reads the reactors file and find the currently enabled
 * [reactor].
 *
 * Returns a dynamically allocated memory with the current
 * reactor. NULL otherwise.
 */
static char *ikm_get_current_reactor(char *monitor_name)
{
	char *reactors = ikm_read_reactor(monitor_name);
	char *start;
	char *end;
	char *curr_reactor;

	if (!reactors)
		return NULL;

	start = strstr(reactors, "[");
	if (!start)
		goto out_free;

	start++;

	end = strstr(start, "]");
	if (!end)
		goto out_free;

	*end = '\0';

	curr_reactor = calloc(strlen(start) + 1, sizeof(char));
	if (!curr_reactor)
		goto out_free;

	strncpy(curr_reactor, start, strlen(start));
	debug_msg("ikm: read current reactor %s\n", curr_reactor);

out_free:
	free(reactors);

	return curr_reactor;
}

static int ikm_has_id(char *monitor_name)
{
	char path[MAX_PATH];
	char *format;
	int has_id;

	snprintf(path, MAX_PATH, "events/rv/event_%s/format", monitor_name);
	format = tracefs_instance_file_read(NULL, path, NULL);
	if (!format) {
		err_msg("ikm: fail reading monitor's %s format event file\n", monitor_name);
		return -1;
	}

	/* print fmt: "%d: %s x %s -> %s %s", REC->id, ... */
	has_id = !!strstr(format, "REC->id");

	debug_msg("ikm: monitor %s has id: %s\n", monitor_name, has_id ? "yes" : "no");

	free(format);

	return has_id;
}

/**
 * ikm_list_monitors - list all available monitors
 *
 * Returns 0 on success, -1 otherwise.
 */
int ikm_list_monitors(void)
{
	char *available_monitors;
	struct monitor ikm;
	char *curr, *next;
	int retval;

	available_monitors = tracefs_instance_file_read(NULL, "rv/available_monitors", NULL);

	if (!available_monitors) {
		err_msg("ikm: available monitors is not available, is CONFIG_RV enabled?\n");
		return -1;
	}

	curr = available_monitors;
	do {
		next = strstr(curr, "\n");
		*next = '\0';

		retval = ikm_fill_monitor_definition(curr, &ikm);
		if (retval)
			err_msg("ikm: error reading %d in kernel monitor, skipping\n", curr);

		printf("%-24s %s %s\n", ikm.name, ikm.desc, ikm.enabled ? "[ON]" : "[OFF]");
		curr = ++next;

	} while (strlen(curr));

	free(available_monitors);

	return 0;
}

static void ikm_print_header(struct trace_seq *s)
{
	trace_seq_printf(s, "%16s-%-8s %5s %5s ", "<TASK>", "PID", "[CPU]", "TYPE");
	if (config_has_id)
		trace_seq_printf(s, "%8s ", "ID");

	trace_seq_printf(s, "%24s x %-24s -> %-24s %s\n",
			 "STATE",
			 "EVENT",
			 "NEXT_STATE",
			 "FINAL");

	trace_seq_printf(s, "%16s %-8s %5s %5s ", " | ", " | ", " | ", " | ");

	if (config_has_id)
		trace_seq_printf(s, "%8s ", " | ");

	trace_seq_printf(s, "%24s   %-24s    %-24s %s\n",
			 " | ",
			 " | ",
			 " | ",
			 "|");

}

/*
 * ikm_event_handler - callback to handle event events
 *
 * Called any time a rv:"monitor"_event events is generated.
 * It parses and print event.
 */
static int
ikm_event_handler(struct trace_seq *s, struct tep_record *record,
		  struct tep_event *trace_event, void *context)
{
	/* if needed: struct trace_instance *inst = context; */
	char *state, *event, *next_state;
	unsigned long long final_state;
	unsigned long long pid;
	unsigned long long id;
	int cpu = record->cpu;
	int val;

	if (config_has_id)
		tep_get_field_val(s, trace_event, "id", record, &id, 1);

	tep_get_common_field_val(s, trace_event, "common_pid", record, &pid, 1);

	if (config_has_id && (config_my_pid == id))
		return 0;
	else if (config_my_pid && (config_my_pid == pid))
		return 0;

	tep_print_event(trace_event->tep, s, record, "%16s-%-8d ", TEP_PRINT_COMM, TEP_PRINT_PID);

	trace_seq_printf(s, "[%.3d] event ", cpu);

	if (config_has_id)
		trace_seq_printf(s, "%8llu ", id);

	state = tep_get_field_raw(s, trace_event, "state", record, &val, 0);
	event = tep_get_field_raw(s, trace_event, "event", record, &val, 0);
	next_state = tep_get_field_raw(s, trace_event, "next_state", record, &val, 0);
	tep_get_field_val(s, trace_event, "final_state", record, &final_state, 1);

	trace_seq_printf(s, "%24s x %-24s -> %-24s %s\n",
			 state,
			 event,
			 next_state,
			 final_state ? "Y" : "N");

	trace_seq_do_printf(s);
	trace_seq_reset(s);

	return 0;
}

/*
 * ikm_error_handler - callback to handle error events
 *
 * Called any time a rv:"monitor"_errors events is generated.
 * It parses and print event.
 */
static int
ikm_error_handler(struct trace_seq *s, struct tep_record *record,
		  struct tep_event *trace_event, void *context)
{
	unsigned long long pid, id;
	int cpu = record->cpu;
	char *state, *event;
	int val;

	if (config_has_id)
		tep_get_field_val(s, trace_event, "id", record, &id, 1);

	tep_get_common_field_val(s, trace_event, "common_pid", record, &pid, 1);

	if (config_has_id && config_my_pid == id)
		return 0;
	else if (config_my_pid == pid)
		return 0;

	trace_seq_printf(s, "%8lld [%03d] error ", pid, cpu);

	if (config_has_id)
		trace_seq_printf(s, "%8llu ", id);

	state = tep_get_field_raw(s, trace_event, "state", record, &val, 0);
	event = tep_get_field_raw(s, trace_event, "event", record, &val, 0);

	trace_seq_printf(s, "%24s x %s\n", state, event);

	trace_seq_do_printf(s);
	trace_seq_reset(s);

	return 0;
}

/*
 * ikm_setup_trace_instance - set up a tracing instance to collect data
 *
 * Create a trace instance, enable rv: events and enable the trace.
 *
 * Returns the trace_instance * with all set, NULL otherwise.
 */
static struct trace_instance *ikm_setup_trace_instance(char *monitor_name)
{
	char event[MAX_DA_NAME_LEN + 7]; /* max(error_,event_) + '0' = 7 */
	struct trace_instance *inst;
	int retval;

	if (!config_trace)
		return NULL;

	config_has_id = ikm_has_id(monitor_name);
	if (config_has_id < 0) {
		err_msg("ikm: failed to read monitor %s event format\n", monitor_name);
		goto out_err;
	}

	/* alloc data */
	inst = calloc(1, sizeof(*inst));
	if (!inst) {
		err_msg("ikm: failed to allocate trace instance");
		goto out_err;
	}

	retval = trace_instance_init(inst, monitor_name);
	if (retval)
		goto out_free;

	/* enable events */
	snprintf(event, sizeof(event), "event_%s", monitor_name);
	retval = tracefs_event_enable(inst->inst, "rv",  event);
	if (retval)
		goto out_inst;

	tep_register_event_handler(inst->tep, -1, "rv", event,
				   ikm_event_handler, NULL);

	snprintf(event, sizeof(event), "error_%s", monitor_name);
	retval = tracefs_event_enable(inst->inst, "rv", event);
	if (retval)
		goto out_inst;

	tep_register_event_handler(inst->tep, -1, "rv", event,
				   ikm_error_handler, NULL);

	/* ready to enable */
	tracefs_trace_on(inst->inst);

	return inst;

out_inst:
	trace_instance_destroy(inst);
out_free:
	free(inst);
out_err:
	return NULL;
}

/**
 * ikm_destroy_trace_instance - destroy a previously created instance
 */
static void ikm_destroy_trace_instance(struct trace_instance *inst)
{
	if (!inst)
		return;

	trace_instance_destroy(inst);
	free(inst);
}

/*
 * ikm_usage_print_reactors - print all available reactors, one per line.
 */
static void ikm_usage_print_reactors(void)
{
	char *reactors = tracefs_instance_file_read(NULL, "rv/available_reactors", NULL);
	char *start, *end;

	if (!reactors)
		return;

	fprintf(stderr, "  available reactors:");

	start = reactors;
	end = strstr(start, "\n");

	while (end) {
		*end = '\0';

		fprintf(stderr, " %s", start);

		start = ++end;
		end = strstr(start, "\n");
	};

	fprintf(stderr, "\n");
}
/*
 * ikm_usage - print usage
 */
static void ikm_usage(int exit_val, char *monitor_name, const char *fmt, ...)
{

	char message[1024];
	va_list ap;
	int i;

	static const char *const usage[] = {
		"",
		"	-h/--help: print this menu and the reactor list",
		"	-r/--reactor 'reactor': enables the 'reactor'",
		"	-s/--self: when tracing (-t), also trace rv command",
		"	-t/--trace: trace monitor's event",
		"	-v/--verbose: print debug messages",
		"",
		NULL,
	};

	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);

	fprintf(stderr, "  %s\n", message);

	fprintf(stderr, "\n  usage: rv mon %s [-h] [-q] [-r reactor] [-s] [-v]", monitor_name);

	for (i = 0; usage[i]; i++)
		fprintf(stderr, "%s\n", usage[i]);

	ikm_usage_print_reactors();
	exit(exit_val);
}

/*
 * parse_arguments - parse arguments and set config
 */
static int parse_arguments(char *monitor_name, int argc, char **argv)
{
	int c, retval;

	config_my_pid = getpid();

	while (1) {
		static struct option long_options[] = {
			{"help",		no_argument,		0, 'h'},
			{"reactor",		required_argument,	0, 'r'},
			{"self",		no_argument,		0, 's'},
			{"trace",		no_argument,		0, 't'},
			{"verbose",		no_argument,		0, 'v'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "hr:stv", long_options, &option_index);

		/* detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			ikm_usage(0, monitor_name, "help:");
			break;
		case 'r':
			config_reactor = optarg;
			break;
		case 's':
			config_my_pid = 0;
			break;
		case 't':
			config_trace = 1;
			break;
		case 'v':
			config_debug = 1;
			break;
		}
	}

	if (config_reactor) {
		config_initial_reactor = ikm_get_current_reactor(monitor_name);
		if (!config_initial_reactor)
			ikm_usage(1, monitor_name,
				  "ikm: failed to read current reactor, are reactors enabled?");

		retval = ikm_write_reactor(monitor_name, config_reactor);
		if (retval <= 0)
			ikm_usage(1, monitor_name,
				  "ikm: failed to set %s reactor, is it available?",
				  config_reactor);
	}

	debug_msg("ikm: my pid is %d\n", config_my_pid);

	return 0;
}

/**
 * ikm_run_monitor - apply configs and run the monitor
 *
 * Returns 1 if a monitor was found an executed, 0 if no
 * monitors were found, or -1 on error.
 */
int ikm_run_monitor(char *monitor_name, int argc, char **argv)
{
	struct trace_instance *inst = NULL;
	int retval;

	/*
	 * Check if monitor exists by seeing it is enabled.
	 */
	retval = __ikm_read_enable(monitor_name);
	if (retval < 0)
		return 0;

	if (retval) {
		err_msg("ikm: monitor %s (in-kernel) is already enabled\n", monitor_name);
		return -1;
	}

	/* we should be good to go */
	retval = parse_arguments(monitor_name, argc, argv);
	if (retval)
		ikm_usage(1, monitor_name, "ikm: failed parsing arguments");

	if (config_trace) {
		inst = ikm_setup_trace_instance(monitor_name);
		if (!inst)
			return -1;
	}

	retval = ikm_enable(monitor_name);
	if (retval < 0)
		goto out_free_instance;

	if (config_trace)
		ikm_print_header(inst->seq);

	while (!should_stop()) {
		if (config_trace) {
			retval = tracefs_iterate_raw_events(inst->tep,
							    inst->inst,
							    NULL,
							    0,
							    collect_registered_events,
							    inst);
			if (retval) {
				err_msg("ikm: error reading trace buffer\n");
				break;
			}
		}

		sleep(1);
	}

	ikm_disable(monitor_name);
	ikm_destroy_trace_instance(inst);

	if (config_reactor && config_initial_reactor)
		ikm_write_reactor(monitor_name, config_initial_reactor);

	return 1;

out_free_instance:
	ikm_destroy_trace_instance(inst);
	if (config_reactor && config_initial_reactor)
		ikm_write_reactor(monitor_name, config_initial_reactor);
	return -1;
}
