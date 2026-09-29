// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sched.h>

#include <linux/compiler.h>

#include "osnoise.h"

#define DEFAULT_SAMPLE_PERIOD	1000000			/* 1s */
#define DEFAULT_SAMPLE_RUNTIME	1000000			/* 1s */

/*
 * osnoise_get_cpus - return the original "osnoise/cpus" content
 *
 * It also saves the value to be restored.
 */
char *osnoise_get_cpus(struct osnoise_context *context)
{
	if (context->curr_cpus)
		return context->curr_cpus;

	if (context->orig_cpus)
		return context->orig_cpus;

	context->orig_cpus = tracefs_instance_file_read(NULL, "osnoise/cpus", NULL);

	/*
	 * The error value (NULL) is the same for tracefs_instance_file_read()
	 * and this functions, so:
	 */
	return context->orig_cpus;
}

/*
 * osnoise_set_cpus - configure osnoise to run on *cpus
 *
 * "osnoise/cpus" file is used to set the cpus in which osnoise/timerlat
 * will run. This function opens this file, saves the current value,
 * and set the cpus passed as argument.
 */
int osnoise_set_cpus(struct osnoise_context *context, char *cpus)
{
	char *orig_cpus = osnoise_get_cpus(context);
	char buffer[1024];
	int retval;

	if (!orig_cpus)
		return -1;

	context->curr_cpus = strdup(cpus);
	if (!context->curr_cpus)
		return -1;

	snprintf(buffer, ARRAY_SIZE(buffer), "%s\n", cpus);

	debug_msg("setting cpus to %s from %s", cpus, context->orig_cpus);

	retval = tracefs_instance_file_write(NULL, "osnoise/cpus", buffer);
	if (retval < 0) {
		free(context->curr_cpus);
		context->curr_cpus = NULL;
		return -1;
	}

	return 0;
}

/*
 * osnoise_restore_cpus - restore the original "osnoise/cpus"
 *
 * osnoise_set_cpus() saves the original data for the "osnoise/cpus"
 * file. This function restore the original config it was previously
 * modified.
 */
void osnoise_restore_cpus(struct osnoise_context *context)
{
	int retval;

	if (!context->orig_cpus)
		return;

	if (!context->curr_cpus)
		return;

	/* nothing to do? */
	if (!strcmp(context->orig_cpus, context->curr_cpus))
		goto out_done;

	debug_msg("restoring cpus to %s", context->orig_cpus);

	retval = tracefs_instance_file_write(NULL, "osnoise/cpus", context->orig_cpus);
	if (retval < 0)
		err_msg("could not restore original osnoise cpus\n");

out_done:
	free(context->curr_cpus);
	context->curr_cpus = NULL;
}

/*
 * osnoise_put_cpus - restore cpus config and cleanup data
 */
void osnoise_put_cpus(struct osnoise_context *context)
{
	osnoise_restore_cpus(context);

	if (!context->orig_cpus)
		return;

	free(context->orig_cpus);
	context->orig_cpus = NULL;
}

/*
 * osnoise_get_cpu_count - get the number of CPUs as seen by the osnoise tracer
 *
 * Write "all" to osnoise/cpus and read back the real kernel-side list
 * of all possible cpus. Restore the original value afterwards.
 *
 * Returns the number of CPUs, or -1 on error.
 */
int osnoise_get_cpu_count(void)
{
	char *orig, *readback;
	int max_cpu;

	orig = tracefs_instance_file_read(NULL, "osnoise/cpus", NULL);
	if (!orig)
		return -1;

	if (tracefs_instance_file_write(NULL, "osnoise/cpus", "all\n") < 0) {
		free(orig);
		return -1;
	}

	readback = tracefs_instance_file_read(NULL, "osnoise/cpus", NULL);
	if (!readback) {
		tracefs_instance_file_write(NULL, "osnoise/cpus", orig);
		free(orig);
		return -1;
	}

	max_cpu = get_max_cpu_from_list(readback);
	free(readback);

	if (tracefs_instance_file_write(NULL, "osnoise/cpus", orig) < 0) {
		free(orig);
		return -1;
	}
	free(orig);

	if (max_cpu < 0)
		return -1;

	return max_cpu + 1;
}

/**
 * osnoise_read_ll_config - read a long long value from a tracefs config
 * @rel_path: tracefs-relative path to the config file
 * @set_value: pointer to the cached value set by RTLA, or -1 if unset
 * @orig_value: pointer to the cached original value read from tracefs, or -1 if unread
 *
 * Returns the current effective value for the config at @rel_path. If RTLA has
 * already written a value (@set_value != -1), that value is returned. If the
 * original has already been read (@orig_value != -1), that cached value is
 * returned. Otherwise, reads the value from tracefs, caches it in @orig_value
 * (so it can later be restored), and returns it.
 *
 * This is the shared read primitive used by both the manually implemented
 * get functions (osnoise_get_runtime, osnoise_get_period) and the
 * OSNOISE_LL_OPTION-generated set/restore/put functions.
 *
 * Returns the config value on success, -1 on error.
 */
static long long osnoise_read_ll_config(char *rel_path,
					long long *set_value,
					long long *orig_value)
{
	long long retval;
	char *buffer;

	if (*set_value != -1)
		/* option has been set by RTLA already */
		return *set_value;

	if (*orig_value != -1)
		/* RTLA has already read the option */
		return *orig_value;

	/* current value is not known to RTLA yet, read it from tracefs */
	buffer = tracefs_instance_file_read(NULL, rel_path, NULL);
	if (!buffer)
		return -1;

	/* get_llong_from_str returns -1 on error */
	retval = get_llong_from_str(buffer);

	debug_msg("reading %s returned %lld\n", rel_path, retval);

	free(buffer);

	if (retval < 0)
		goto out_err;

	/* save the value and return it */
	*orig_value = retval;
	return retval;

out_err:
	return -1;
}

/**
 * osnoise_write_ll_config - write a long long value to a tracefs config
 * @rel_path: tracefs-relative path to the config file
 * @value: the value to write
 * @set_value: pointer to the cached value set by RTLA, or -1 if unset
 * @orig_value: pointer to the cached original value read from tracefs, or -1 if unread
 *
 * Writes @value to the config at @rel_path. Before writing, calls
 * osnoise_read_ll_config() to ensure the original value is cached in
 * @orig_value (enabling later restoration). On successful write, records
 * the new value in @set_value.
 *
 * This is the shared write primitive used by both the manually implemented
 * write functions (__osnoise_write_runtime, __osnoise_write_period) and the
 * OSNOISE_LL_OPTION-generated set/restore/put functions.
 *
 * Returns 0 on success, -1 on read error (option likely unknown to kernel),
 * or -2 on write error.
 */
static int osnoise_write_ll_config(char *rel_path,
				   long long value,
				   long long *set_value,
				   long long *orig_value)
{
	long long curr = osnoise_read_ll_config(rel_path, set_value, orig_value);
	long long retval;
	char buffer[BUFF_U64_STR_SIZE];

	if (curr == -1)
		/* read failed, option likely unknown to kernel */
		return -1;

	snprintf(buffer, sizeof(buffer), "%lld\n", value);

	debug_msg("setting %s to %lld\n", rel_path, value);

	retval = tracefs_instance_file_write(NULL, rel_path, buffer);

	if (retval < 0)
		/* write failed, hard error */
		return -2;

	/* record the set value and return success */
	*set_value = value;
	return 0;
}

/**
 * osnoise_restore_ll_config - restore a long long config to its original value
 * @rel_path: tracefs-relative path to the config file
 * @set_value: pointer to the cached value set by RTLA, or -1 if unset
 * @orig_value: pointer to the cached original value read from tracefs, or -1 if unread
 *
 * Restores the config at @rel_path to the value cached in @orig_value (which
 * was saved by a prior osnoise_read_ll_config() or osnoise_write_ll_config()
 * call). If the original was never read, or if the current set value already
 * matches the original, no write is performed. After restoring, clears
 * @set_value to -1 to indicate RTLA no longer overrides this config.
 *
 * This is the shared restore primitive used by both the manually implemented
 * restore function (osnoise_restore_runtime_period) and the
 * OSNOISE_LL_OPTION-generated restore/put functions.
 */
static void osnoise_restore_ll_config(char *rel_path,
				      long long *set_value,
				      long long *orig_value)
{
	int retval;

	if (*orig_value == -1)
		return;

	if (*orig_value == *set_value)
		goto out_done;

	retval = osnoise_write_ll_config(rel_path, *orig_value, set_value, orig_value);
	if (retval < 0)
		err_msg("Could not restore original value for %s\n", rel_path);

out_done:
	*set_value = -1;
}

/*
 * osnoise_get_runtime - return the original "osnoise/runtime_us" value
 *
 * It also saves the value to be restored.
 */
long long osnoise_get_runtime(struct osnoise_context *context)
{
	return osnoise_read_ll_config("osnoise/runtime_us",
				      &context->runtime_us,
				      &context->orig_runtime_us);
}

/*
 * osnoise_get_period - return the original "osnoise/period_us" value
 *
 * It also saves the value to be restored.
 */
long long osnoise_get_period(struct osnoise_context *context)
{
	return osnoise_read_ll_config("osnoise/period_us",
				      &context->period_us,
				      &context->orig_period_us);
}

static int __osnoise_write_runtime(struct osnoise_context *context,
				   long long runtime)
{
	return osnoise_write_ll_config("osnoise/runtime_us",
				       runtime,
				       &context->runtime_us,
				       &context->orig_runtime_us);
}

static int __osnoise_write_period(struct osnoise_context *context,
				  long long period)
{
	return osnoise_write_ll_config("osnoise/period_us",
				       period,
				       &context->period_us,
				       &context->orig_period_us);
}

/*
 * osnoise_set_runtime_period - set osnoise runtime and period
 *
 * Osnoise's runtime and period are related as runtime <= period.
 * Thus, this function saves the original values, and then tries
 * to set the runtime and period if they are != 0.
 */
int osnoise_set_runtime_period(struct osnoise_context *context,
			       long long runtime,
			       long long period)
{
	long long curr_runtime_us;
	long long curr_period_us;
	int retval;

	if (!period && !runtime)
		return 0;

	curr_runtime_us = osnoise_get_runtime(context);
	curr_period_us = osnoise_get_period(context);

	/* error getting any value? */
	if (curr_period_us == -1 || curr_runtime_us == -1)
		return -1;

	if (!period) {
		if (runtime > curr_period_us)
			return -1;
		return __osnoise_write_runtime(context, runtime);
	} else if (!runtime) {
		if (period < curr_runtime_us)
			return -1;
		return __osnoise_write_period(context, period);
	}

	if (runtime > curr_period_us) {
		retval = __osnoise_write_period(context, period);
		if (retval)
			return -1;
		retval = __osnoise_write_runtime(context, runtime);
		if (retval)
			return -1;
	} else {
		retval = __osnoise_write_runtime(context, runtime);
		if (retval)
			return -1;
		retval = __osnoise_write_period(context, period);
		if (retval)
			return -1;
	}

	return 0;
}

/*
 * osnoise_restore_runtime_period - restore the original runtime and period
 */
void osnoise_restore_runtime_period(struct osnoise_context *context)
{
	long long orig_runtime = context->orig_runtime_us;
	long long orig_period = context->orig_period_us;
	long long curr_runtime = context->runtime_us;
	long long curr_period = context->period_us;
	int retval;

	if ((orig_runtime == -1) && (orig_period == -1))
		return;

	if ((orig_period == curr_period) && (orig_runtime == curr_runtime))
		goto out_done;

	retval = osnoise_set_runtime_period(context, orig_runtime, orig_period);
	if (retval)
		err_msg("Could not restore original osnoise runtime/period\n");

out_done:
	context->runtime_us = -1;
	context->period_us = -1;
}

/*
 * osnoise_put_runtime_period - restore original values and cleanup data
 */
void osnoise_put_runtime_period(struct osnoise_context *context)
{
	osnoise_restore_runtime_period(context);

	if (context->orig_runtime_us != -1)
		context->orig_runtime_us = -1;

	if (context->orig_period_us != -1)
		context->orig_period_us = -1;
}

/*
 * Long long option set/restore/put functions, generated from OSNOISE_LL_OPTIONS.
 */
#define OSNOISE_LL_OPTION(name, path)						\
int osnoise_set_##name(struct osnoise_context *context, long long value)	\
{										\
	return osnoise_write_ll_config(path,					\
				       value,					\
				       &context->name,				\
				       &context->orig_##name);			\
}										\
										\
void osnoise_restore_##name(struct osnoise_context *context)			\
{										\
	osnoise_restore_ll_config(path, &context->name, &context->orig_##name);	\
}										\
										\
static void osnoise_put_##name(struct osnoise_context *context)			\
{										\
	osnoise_restore_##name(context);					\
										\
	if (context->orig_##name == -1)						\
		return;								\
										\
	context->orig_##name = -1;						\
}
OSNOISE_LL_OPTIONS
#undef OSNOISE_LL_OPTION

/**
 * osnoise_get_option - read a boolean flag from osnoise/options
 * @option: the option name string to look for (e.g. "OSNOISE_IRQ_DISABLE")
 * @set_value: pointer to the cached value set by RTLA, or -1 if unset
 * @orig_value: pointer to the cached original value read from tracefs, or -1 if unread
 *
 * Returns the current state of the flag @option. If RTLA has already written a
 * value (@set_value != -1), that value is returned. If the original has already
 * been read (@orig_value != -1), that cached value is returned. Otherwise, reads
 * the "osnoise/options" file from tracefs, checks whether @option appears with or
 * without a "NO_" prefix, caches the result in @orig_value (so it can later be
 * restored), and returns it.
 *
 * This is the shared read primitive used by the OSNOISE_FLAG_OPTION-generated
 * set/restore/put functions.
 *
 * Returns 1 if enabled, 0 if disabled, or -1 on error (option unknown to kernel).
 */
static int osnoise_get_option(char *option,
			      int *set_value,
			      int *orig_value)
{
	char *options;
	char no_option[128];
	int retval = 0;
	char *opt;

	if (*set_value != -1)
		/* option has been set by RTLA already */
		return *set_value;

	if (*orig_value != -1)
		/* RTLA has already read the option */
		return *orig_value;

	/* current value is not known to RTLA yet, read it from tracefs */
	options = tracefs_instance_file_read(NULL, "osnoise/options", NULL);
	if (!options)
		return -1;

	/*
	 * Check first if the option is disabled.
	 */
	snprintf(no_option, sizeof(no_option), "NO_%s", option);

	opt = strstr(options, no_option);
	if (opt)
		goto out_free;

	/*
	 * Now that it is not disabled, if the string is there, it is
	 * enabled. If the string is not there, the option does not exist.
	 */
	opt = strstr(options, option);
	if (opt)
		retval = 1;
	else
		retval = -1;

out_free:
	free(options);

	if (retval < 0)
		goto out_err;

	/* save the value and return it */
	*orig_value = retval;
	return retval;

out_err:
	return -1;
}

/**
 * osnoise_set_option - write a boolean flag to osnoise/options
 * @option: the option name string (e.g. "OSNOISE_IRQ_DISABLE")
 * @onoff: the desired state (true to enable, false to disable)
 * @set_value: pointer to the cached value set by RTLA, or -1 if unset
 * @orig_value: pointer to the cached original value read from tracefs, or -1 if unread
 *
 * Sets the flag @option to the state @onoff. Before writing, calls
 * osnoise_get_option() to ensure the original value is cached in @orig_value
 * (enabling later restoration). If the current value already matches @onoff,
 * no write is performed. On successful write, records the new state in
 * @set_value.
 *
 * This is the shared write primitive used by the OSNOISE_FLAG_OPTION-generated
 * set/restore/put functions.
 *
 * Returns 0 on success, -1 on read error (option likely unknown to kernel),
 * or -2 on write error.
 */
static int osnoise_set_option(char *option,
			      bool onoff,
			      int *set_value,
			      int *orig_value)
{
	int curr = osnoise_get_option(option, set_value, orig_value);
	char no_option[128];
	int retval;

	if (curr == -1)
		/* read failed, option likely unknown to kernel */
		return -1;

	if (curr == onoff)
		return 0;

	if (onoff) {
		retval = tracefs_instance_file_write(NULL, "osnoise/options", option);
	} else {
		snprintf(no_option, sizeof(no_option), "NO_%s", option);
		retval = tracefs_instance_file_write(NULL, "osnoise/options", no_option);
	}

	if (retval < 0)
		/* write failed, hard error */
		return -2;

	/* record the set value and return success */
	*set_value = onoff;
	return 0;
}

/**
 * osnoise_restore_option - restore a boolean flag to its original value
 * @option: the option name string (e.g. "OSNOISE_IRQ_DISABLE")
 * @set_value: pointer to the cached value set by RTLA, or -1 if unset
 * @orig_value: pointer to the cached original value read from tracefs, or -1 if unread
 *
 * Restores the flag @option to the state cached in @orig_value (which was saved
 * by a prior osnoise_get_option() or osnoise_set_option() call). If the original
 * was never read, or if the current set value already matches the original, no
 * write is performed. After restoring, clears @set_value to -1 to indicate RTLA
 * no longer overrides this option.
 *
 * This is the shared restore primitive used by the OSNOISE_FLAG_OPTION-generated
 * restore/put functions.
 */
static void osnoise_restore_option(char *option,
				   int *set_value,
				   int *orig_value)
{
	int retval;

	if (*orig_value == -1)
		return;

	if (*orig_value == *set_value)
		goto out_done;

	retval = osnoise_set_option(option, *orig_value, set_value, orig_value);
	if (retval < 0)
		err_msg("Could not restore original %s option\n", option);

out_done:
	*set_value = -1;
}

/*
 * Flag option set/restore/put functions, generated from OSNOISE_FLAG_OPTIONS.
 */
#define OSNOISE_FLAG_OPTION(name, option_str)				\
int osnoise_set_##name(struct osnoise_context *context, bool onoff)	\
{									\
	return osnoise_set_option(option_str,				\
				  onoff,				\
				  &context->opt_##name,			\
				  &context->orig_opt_##name);		\
}									\
									\
void osnoise_restore_##name(struct osnoise_context *context)		\
{									\
	osnoise_restore_option(option_str,				\
			       &context->opt_##name,			\
			       &context->orig_opt_##name);		\
}									\
									\
static void osnoise_put_##name(struct osnoise_context *context)		\
{									\
	osnoise_restore_##name(context);				\
									\
	if (context->orig_opt_##name == -1)				\
		return;							\
									\
	context->orig_opt_##name = -1;					\
}
OSNOISE_FLAG_OPTIONS
#undef OSNOISE_FLAG_OPTION

enum {
	FLAG_CONTEXT_NEWLY_CREATED	= (1 << 0),
	FLAG_CONTEXT_DELETED		= (1 << 1),
};

/*
 * osnoise_get_context - increase the usage of a context and return it
 */
int osnoise_get_context(struct osnoise_context *context)
{
	int ret;

	if (context->flags & FLAG_CONTEXT_DELETED) {
		ret = -1;
	} else {
		context->ref++;
		ret = 0;
	}

	return ret;
}

/*
 * osnoise_context_alloc - alloc an osnoise_context
 *
 * The osnoise context contains the information of the "osnoise/" configs.
 * It is used to set and restore the config.
 */
struct osnoise_context *osnoise_context_alloc(void)
{
	struct osnoise_context *context;

	context = calloc_fatal(1, sizeof(*context));

	/* First allocate manually implemented options... */
	context->orig_runtime_us = -1;
	context->runtime_us	 = -1;
	context->orig_period_us	 = -1;
	context->period_us	 = -1;

	/* ...then the automatically generated ones. */
#define OSNOISE_LL_OPTION(name, path)		\
	context->orig_##name	 = -1;		\
	context->name		 = -1;
#define OSNOISE_FLAG_OPTION(name, option_str)	\
	context->orig_opt_##name = -1;		\
	context->opt_##name	 = -1;
	OSNOISE_LL_OPTIONS
	OSNOISE_FLAG_OPTIONS
#undef OSNOISE_LL_OPTION
#undef OSNOISE_FLAG_OPTION

	osnoise_get_context(context);

	return context;
}

/*
 * osnoise_put_context - put the osnoise_put_context
 *
 * If there is no other user for the context, the original data
 * is restored.
 */
void osnoise_put_context(struct osnoise_context *context)
{
	if (--context->ref < 1)
		context->flags |= FLAG_CONTEXT_DELETED;

	if (!(context->flags & FLAG_CONTEXT_DELETED))
		return;

	/* First restore the original values of the options... */
	osnoise_put_cpus(context);
	osnoise_put_runtime_period(context);

	/* ...then the automatically generated ones. */
#define OSNOISE_LL_OPTION(name, path)		osnoise_put_##name(context);
#define OSNOISE_FLAG_OPTION(name, option_str)	osnoise_put_##name(context);
	OSNOISE_LL_OPTIONS
	OSNOISE_FLAG_OPTIONS
#undef OSNOISE_LL_OPTION
#undef OSNOISE_FLAG_OPTION

	free(context);
}

/*
 * osnoise_destroy_tool - disable trace, restore configs and free data
 */
void osnoise_destroy_tool(struct osnoise_tool *top)
{
	if (!top)
		return;

	trace_instance_destroy(&top->trace);

	if (top->context)
		osnoise_put_context(top->context);

	free(top);
}

/*
 * osnoise_init_tool - init an osnoise tool
 *
 * It allocs data, create a context to store data and
 * creates a new trace instance for the tool.
 */
struct osnoise_tool *osnoise_init_tool(char *tool_name)
{
	struct osnoise_tool *top;

	top = calloc_fatal(1, sizeof(*top));
	top->context = osnoise_context_alloc();

	if (trace_instance_init(&top->trace, tool_name)) {
		osnoise_destroy_tool(top);
		return NULL;
	}

	return top;
}

/*
 * osnoise_init_ipi_filters - Initialize event filtering for IPI events
 */
int osnoise_init_ipi_filters(struct osnoise_tool *tool,
				    struct common_params *params,
				    bool *filters_enabled)
{
	char filter[MAX_PATH];
	int retval;
	/*
	 * If tracing on a subset of possible CPUs, leverage the kernel filtering
	 * infrastructure to only generate events on traced CPUs.
	 * Older kernels (pre v6.6) may have the IPI events but not the ability
	 * to filter them, so allow that to fail gracefully.
	 */

	snprintf(filter, ARRAY_SIZE(filter), "cpu & CPUS{%s}\n", params->cpus);
	retval = tracefs_event_file_write(tool->trace.inst,
					  "ipi", "ipi_send_cpu", "filter",
					  filter);
	if (retval < 0) {
		debug_msg("Could not set ipi_send_cpu CPU filter\n");
		*filters_enabled = false;
		return 0;
	}


	snprintf(filter, ARRAY_SIZE(filter), "cpumask & CPUS{%s}\n", params->cpus);
	retval = tracefs_event_file_write(tool->trace.inst,
					  "ipi", "ipi_send_cpumask", "filter",
					  filter);
	if (retval < 0) {
		/*
		 * If we managed to set up the previous filter but not
		 * this one, something's really wrong
		 */
		err_msg("Could not set ipi_send_cpumask CPU filter\n");
		*filters_enabled = false;
		return -1;
	}

	*filters_enabled = true;
	return 0;
}

/*
 * osnoise_init_trace_tool - init a tracer instance to trace osnoise events
 */
struct osnoise_tool *osnoise_init_trace_tool(struct common_params *params,
					     const char *tracer)
{
	struct osnoise_tool *trace;
	int retval;

	trace = osnoise_init_tool("osnoise_trace");
	if (!trace)
		return NULL;

	retval = tracefs_event_enable(trace->trace.inst, "osnoise", NULL);
	if (retval < 0 && !errno) {
		err_msg("Could not find osnoise events\n");
		goto out_err;
	}

	if (!params->ipi)
		goto done;

	retval = tracefs_event_enable(trace->trace.inst, "ipi", "ipi_send_cpu");
	if (retval < 0 && !errno) {
		err_msg("Could not find ipi_send_cpu event\n");
		goto out_err;
	}

	retval = tracefs_event_enable(trace->trace.inst, "ipi", "ipi_send_cpumask");
	if (retval < 0 && !errno) {
		err_msg("Could not find ipi_send_cpumask event\n");
		goto out_err;
	}

	if (params->cpus) {
		bool unused;

		retval = osnoise_init_ipi_filters(trace, params, &unused);
		if (retval < 0)
			goto out_err;
	}

done:
	retval = enable_tracer_by_name(trace->trace.inst, tracer);
	if (retval) {
		err_msg("Could not enable %s tracer for tracing\n", tracer);
		goto out_err;
	}

	return trace;
out_err:
	osnoise_destroy_tool(trace);
	return NULL;
}

bool osnoise_trace_is_off(struct osnoise_tool *tool, struct osnoise_tool *record)
{
	/*
	 * The tool instance is always present, it is the one used to collect
	 * data.
	 */
	if (!tracefs_trace_is_on(tool->trace.inst))
		return true;

	/*
	 * The trace record instance is only enabled when -t is set. IOW, when the system
	 * is tracing.
	 */
	return record && !tracefs_trace_is_on(record->trace.inst);
}

/*
 * osnoise_report_invalid_events - report number of invalid events
 */
void
osnoise_report_invalid_events(struct osnoise_tool *tool)
{
	unsigned long long total_events;

	if (tool->trace.invalid_events > 0) {
		if (tool->trace.missed_events != UINT64_MAX) {
			total_events = tool->trace.processed_events + tool->trace.invalid_events +
				tool->trace.missed_events;

			printf("%lld (%.2f%%) invalid events, results might not be accurate\n",
				tool->trace.invalid_events,
				(double) tool->trace.invalid_events / total_events * 100.0);
		} else {
			printf("%lld invalid events, results might not be accurate\n",
				tool->trace.invalid_events);
		}
	}
}

/*
 * osnoise_report_missed_events - report number of events dropped by trace
 * buffer
 */
void
osnoise_report_missed_events(struct osnoise_tool *tool)
{
	unsigned long long total_events;

	if (tool->trace.missed_events == UINT64_MAX)
		printf("unknown number of events missed, results might not be accurate\n");
	else if (tool->trace.missed_events > 0) {
		total_events = tool->trace.processed_events + tool->trace.invalid_events +
			tool->trace.missed_events;

		printf("%lld (%.2f%%) events missed, results might not be accurate\n",
		       tool->trace.missed_events,
		       (double) tool->trace.missed_events / total_events * 100.0);
	}
}

/*
 * osnoise_apply_config - apply osnoise configs to the initialized tool
 */
int
osnoise_apply_config(struct osnoise_tool *tool, struct osnoise_params *params)
{
	int retval;

	params->common.kernel_workload = true;

	if (params->runtime || params->period) {
		retval = osnoise_set_runtime_period(tool->context,
						    params->runtime,
						    params->period);
	} else {
		retval = osnoise_set_runtime_period(tool->context,
						    DEFAULT_SAMPLE_PERIOD,
						    DEFAULT_SAMPLE_RUNTIME);
	}

	if (retval) {
		err_msg("Failed to set runtime and/or period\n");
		goto out_err;
	}

	retval = osnoise_set_tracing_thresh(tool->context, params->threshold);
	if (retval) {
		err_msg("Failed to set tracing_thresh\n");
		goto out_err;
	}

	return common_apply_config(tool, &params->common);

out_err:
	return -1;
}

int osnoise_enable(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	int retval;

	/*
	 * Start the tracer here, after having set all instances.
	 *
	 * Let the trace instance start first for the case of hitting a stop
	 * tracing while enabling other instances. The trace instance is the
	 * one with most valuable information.
	 */
	if (tool->record)
		trace_instance_start(&tool->record->trace);
	trace_instance_start(&tool->trace);

	if (params->common.warmup > 0) {
		debug_msg("Warming up for %d seconds\n", params->common.warmup);
		sleep(params->common.warmup);
		if (stop_tracing)
			return -1;

		/*
		 * Clean up the buffer. The osnoise workload do not run
		 * with tracing off to avoid creating a performance penalty
		 * when not needed.
		 */
		retval = tracefs_instance_file_write(tool->trace.inst, "trace", "");
		if (retval < 0) {
			debug_msg("Error cleaning up the buffer");
			return retval;
		}
	}

	retval = osn_set_stop(tool);
	if (retval)
		return retval;

	return 0;
}

__noreturn static void osnoise_usage(int err)
{
	int i;

	static const char *msg[] = {
		"",
		"osnoise version " VERSION,
		"",
		"  usage: [rtla] osnoise [MODE] ...",
		"",
		"  modes:",
		"     top   - prints the summary from osnoise tracer",
		"     hist  - prints a histogram of osnoise samples",
		"",
		"if no MODE is given, the top mode is called, passing the arguments",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(err);
}

int osnoise_main(int argc, char *argv[])
{
	if (argc == 0)
		goto usage;

	/*
	 * if osnoise was called without any argument, run the
	 * default cmdline.
	 */
	if (argc == 1) {
		run_tool(&osnoise_top_ops, argc, argv);
		exit(0);
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
		osnoise_usage(129);
	} else if (str_has_prefix(argv[1], "-")) {
		/* the user skipped the tool, call the default one */
		run_tool(&osnoise_top_ops, argc, argv);
		exit(0);
	} else if (strcmp(argv[1], "top") == 0) {
		run_tool(&osnoise_top_ops, argc-1, &argv[1]);
		exit(0);
	} else if (strcmp(argv[1], "hist") == 0) {
		run_tool(&osnoise_hist_ops, argc-1, &argv[1]);
		exit(0);
	}

usage:
	osnoise_usage(129);
}

int hwnoise_main(int argc, char *argv[])
{
	run_tool(&osnoise_top_ops, argc, argv);
	exit(0);
}
