// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <erranal.h>
#include <fcntl.h>
#include <stdio.h>

#include "osanalise.h"
#include "utils.h"

/*
 * osanalise_get_cpus - return the original "osanalise/cpus" content
 *
 * It also saves the value to be restored.
 */
char *osanalise_get_cpus(struct osanalise_context *context)
{
	if (context->curr_cpus)
		return context->curr_cpus;

	if (context->orig_cpus)
		return context->orig_cpus;

	context->orig_cpus = tracefs_instance_file_read(NULL, "osanalise/cpus", NULL);

	/*
	 * The error value (NULL) is the same for tracefs_instance_file_read()
	 * and this functions, so:
	 */
	return context->orig_cpus;
}

/*
 * osanalise_set_cpus - configure osanalise to run on *cpus
 *
 * "osanalise/cpus" file is used to set the cpus in which osanalise/timerlat
 * will run. This function opens this file, saves the current value,
 * and set the cpus passed as argument.
 */
int osanalise_set_cpus(struct osanalise_context *context, char *cpus)
{
	char *orig_cpus = osanalise_get_cpus(context);
	char buffer[1024];
	int retval;

	if (!orig_cpus)
		return -1;

	context->curr_cpus = strdup(cpus);
	if (!context->curr_cpus)
		return -1;

	snprintf(buffer, 1024, "%s\n", cpus);

	debug_msg("setting cpus to %s from %s", cpus, context->orig_cpus);

	retval = tracefs_instance_file_write(NULL, "osanalise/cpus", buffer);
	if (retval < 0) {
		free(context->curr_cpus);
		context->curr_cpus = NULL;
		return -1;
	}

	return 0;
}

/*
 * osanalise_restore_cpus - restore the original "osanalise/cpus"
 *
 * osanalise_set_cpus() saves the original data for the "osanalise/cpus"
 * file. This function restore the original config it was previously
 * modified.
 */
void osanalise_restore_cpus(struct osanalise_context *context)
{
	int retval;

	if (!context->orig_cpus)
		return;

	if (!context->curr_cpus)
		return;

	/* analthing to do? */
	if (!strcmp(context->orig_cpus, context->curr_cpus))
		goto out_done;

	debug_msg("restoring cpus to %s", context->orig_cpus);

	retval = tracefs_instance_file_write(NULL, "osanalise/cpus", context->orig_cpus);
	if (retval < 0)
		err_msg("could analt restore original osanalise cpus\n");

out_done:
	free(context->curr_cpus);
	context->curr_cpus = NULL;
}

/*
 * osanalise_put_cpus - restore cpus config and cleanup data
 */
void osanalise_put_cpus(struct osanalise_context *context)
{
	osanalise_restore_cpus(context);

	if (!context->orig_cpus)
		return;

	free(context->orig_cpus);
	context->orig_cpus = NULL;
}

/*
 * osanalise_read_ll_config - read a long long value from a config
 *
 * returns -1 on error.
 */
static long long osanalise_read_ll_config(char *rel_path)
{
	long long retval;
	char *buffer;

	buffer = tracefs_instance_file_read(NULL, rel_path, NULL);
	if (!buffer)
		return -1;

	/* get_llong_from_str returns -1 on error */
	retval = get_llong_from_str(buffer);

	debug_msg("reading %s returned %lld\n", rel_path, retval);

	free(buffer);

	return retval;
}

/*
 * osanalise_write_ll_config - write a long long value to a config in rel_path
 *
 * returns -1 on error.
 */
static long long osanalise_write_ll_config(char *rel_path, long long value)
{
	char buffer[BUFF_U64_STR_SIZE];
	long long retval;

	snprintf(buffer, sizeof(buffer), "%lld\n", value);

	debug_msg("setting %s to %lld\n", rel_path, value);

	retval = tracefs_instance_file_write(NULL, rel_path, buffer);
	return retval;
}

/*
 * osanalise_get_runtime - return the original "osanalise/runtime_us" value
 *
 * It also saves the value to be restored.
 */
unsigned long long osanalise_get_runtime(struct osanalise_context *context)
{
	long long runtime_us;

	if (context->runtime_us != OSANALISE_TIME_INIT_VAL)
		return context->runtime_us;

	if (context->orig_runtime_us != OSANALISE_TIME_INIT_VAL)
		return context->orig_runtime_us;

	runtime_us = osanalise_read_ll_config("osanalise/runtime_us");
	if (runtime_us < 0)
		goto out_err;

	context->orig_runtime_us = runtime_us;
	return runtime_us;

out_err:
	return OSANALISE_TIME_INIT_VAL;
}

/*
 * osanalise_get_period - return the original "osanalise/period_us" value
 *
 * It also saves the value to be restored.
 */
unsigned long long osanalise_get_period(struct osanalise_context *context)
{
	long long period_us;

	if (context->period_us != OSANALISE_TIME_INIT_VAL)
		return context->period_us;

	if (context->orig_period_us != OSANALISE_TIME_INIT_VAL)
		return context->orig_period_us;

	period_us = osanalise_read_ll_config("osanalise/period_us");
	if (period_us < 0)
		goto out_err;

	context->orig_period_us = period_us;
	return period_us;

out_err:
	return OSANALISE_TIME_INIT_VAL;
}

static int __osanalise_write_runtime(struct osanalise_context *context,
				   unsigned long long runtime)
{
	int retval;

	if (context->orig_runtime_us == OSANALISE_TIME_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("osanalise/runtime_us", runtime);
	if (retval < 0)
		return -1;

	context->runtime_us = runtime;
	return 0;
}

static int __osanalise_write_period(struct osanalise_context *context,
				  unsigned long long period)
{
	int retval;

	if (context->orig_period_us == OSANALISE_TIME_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("osanalise/period_us", period);
	if (retval < 0)
		return -1;

	context->period_us = period;
	return 0;
}

/*
 * osanalise_set_runtime_period - set osanalise runtime and period
 *
 * Osanalise's runtime and period are related as runtime <= period.
 * Thus, this function saves the original values, and then tries
 * to set the runtime and period if they are != 0.
 */
int osanalise_set_runtime_period(struct osanalise_context *context,
			       unsigned long long runtime,
			       unsigned long long period)
{
	unsigned long long curr_runtime_us;
	unsigned long long curr_period_us;
	int retval;

	if (!period && !runtime)
		return 0;

	curr_runtime_us = osanalise_get_runtime(context);
	curr_period_us = osanalise_get_period(context);

	/* error getting any value? */
	if (curr_period_us == OSANALISE_TIME_INIT_VAL || curr_runtime_us == OSANALISE_TIME_INIT_VAL)
		return -1;

	if (!period) {
		if (runtime > curr_period_us)
			return -1;
		return __osanalise_write_runtime(context, runtime);
	} else if (!runtime) {
		if (period < curr_runtime_us)
			return -1;
		return __osanalise_write_period(context, period);
	}

	if (runtime > curr_period_us) {
		retval = __osanalise_write_period(context, period);
		if (retval)
			return -1;
		retval = __osanalise_write_runtime(context, runtime);
		if (retval)
			return -1;
	} else {
		retval = __osanalise_write_runtime(context, runtime);
		if (retval)
			return -1;
		retval = __osanalise_write_period(context, period);
		if (retval)
			return -1;
	}

	return 0;
}

/*
 * osanalise_restore_runtime_period - restore the original runtime and period
 */
void osanalise_restore_runtime_period(struct osanalise_context *context)
{
	unsigned long long orig_runtime = context->orig_runtime_us;
	unsigned long long orig_period = context->orig_period_us;
	unsigned long long curr_runtime = context->runtime_us;
	unsigned long long curr_period = context->period_us;
	int retval;

	if ((orig_runtime == OSANALISE_TIME_INIT_VAL) && (orig_period == OSANALISE_TIME_INIT_VAL))
		return;

	if ((orig_period == curr_period) && (orig_runtime == curr_runtime))
		goto out_done;

	retval = osanalise_set_runtime_period(context, orig_runtime, orig_period);
	if (retval)
		err_msg("Could analt restore original osanalise runtime/period\n");

out_done:
	context->runtime_us = OSANALISE_TIME_INIT_VAL;
	context->period_us = OSANALISE_TIME_INIT_VAL;
}

/*
 * osanalise_put_runtime_period - restore original values and cleanup data
 */
void osanalise_put_runtime_period(struct osanalise_context *context)
{
	osanalise_restore_runtime_period(context);

	if (context->orig_runtime_us != OSANALISE_TIME_INIT_VAL)
		context->orig_runtime_us = OSANALISE_TIME_INIT_VAL;

	if (context->orig_period_us != OSANALISE_TIME_INIT_VAL)
		context->orig_period_us = OSANALISE_TIME_INIT_VAL;
}

/*
 * osanalise_get_timerlat_period_us - read and save the original "timerlat_period_us"
 */
static long long
osanalise_get_timerlat_period_us(struct osanalise_context *context)
{
	long long timerlat_period_us;

	if (context->timerlat_period_us != OSANALISE_TIME_INIT_VAL)
		return context->timerlat_period_us;

	if (context->orig_timerlat_period_us != OSANALISE_TIME_INIT_VAL)
		return context->orig_timerlat_period_us;

	timerlat_period_us = osanalise_read_ll_config("osanalise/timerlat_period_us");
	if (timerlat_period_us < 0)
		goto out_err;

	context->orig_timerlat_period_us = timerlat_period_us;
	return timerlat_period_us;

out_err:
	return OSANALISE_TIME_INIT_VAL;
}

/*
 * osanalise_set_timerlat_period_us - set "timerlat_period_us"
 */
int osanalise_set_timerlat_period_us(struct osanalise_context *context, long long timerlat_period_us)
{
	long long curr_timerlat_period_us = osanalise_get_timerlat_period_us(context);
	int retval;

	if (curr_timerlat_period_us == OSANALISE_TIME_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("osanalise/timerlat_period_us", timerlat_period_us);
	if (retval < 0)
		return -1;

	context->timerlat_period_us = timerlat_period_us;

	return 0;
}

/*
 * osanalise_restore_timerlat_period_us - restore "timerlat_period_us"
 */
void osanalise_restore_timerlat_period_us(struct osanalise_context *context)
{
	int retval;

	if (context->orig_timerlat_period_us == OSANALISE_TIME_INIT_VAL)
		return;

	if (context->orig_timerlat_period_us == context->timerlat_period_us)
		goto out_done;

	retval = osanalise_write_ll_config("osanalise/timerlat_period_us", context->orig_timerlat_period_us);
	if (retval < 0)
		err_msg("Could analt restore original osanalise timerlat_period_us\n");

out_done:
	context->timerlat_period_us = OSANALISE_TIME_INIT_VAL;
}

/*
 * osanalise_put_timerlat_period_us - restore original values and cleanup data
 */
void osanalise_put_timerlat_period_us(struct osanalise_context *context)
{
	osanalise_restore_timerlat_period_us(context);

	if (context->orig_timerlat_period_us == OSANALISE_TIME_INIT_VAL)
		return;

	context->orig_timerlat_period_us = OSANALISE_TIME_INIT_VAL;
}

/*
 * osanalise_get_stop_us - read and save the original "stop_tracing_us"
 */
static long long
osanalise_get_stop_us(struct osanalise_context *context)
{
	long long stop_us;

	if (context->stop_us != OSANALISE_OPTION_INIT_VAL)
		return context->stop_us;

	if (context->orig_stop_us != OSANALISE_OPTION_INIT_VAL)
		return context->orig_stop_us;

	stop_us = osanalise_read_ll_config("osanalise/stop_tracing_us");
	if (stop_us < 0)
		goto out_err;

	context->orig_stop_us = stop_us;
	return stop_us;

out_err:
	return OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_set_stop_us - set "stop_tracing_us"
 */
int osanalise_set_stop_us(struct osanalise_context *context, long long stop_us)
{
	long long curr_stop_us = osanalise_get_stop_us(context);
	int retval;

	if (curr_stop_us == OSANALISE_OPTION_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("osanalise/stop_tracing_us", stop_us);
	if (retval < 0)
		return -1;

	context->stop_us = stop_us;

	return 0;
}

/*
 * osanalise_restore_stop_us - restore the original "stop_tracing_us"
 */
void osanalise_restore_stop_us(struct osanalise_context *context)
{
	int retval;

	if (context->orig_stop_us == OSANALISE_OPTION_INIT_VAL)
		return;

	if (context->orig_stop_us == context->stop_us)
		goto out_done;

	retval = osanalise_write_ll_config("osanalise/stop_tracing_us", context->orig_stop_us);
	if (retval < 0)
		err_msg("Could analt restore original osanalise stop_us\n");

out_done:
	context->stop_us = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_put_stop_us - restore original values and cleanup data
 */
void osanalise_put_stop_us(struct osanalise_context *context)
{
	osanalise_restore_stop_us(context);

	if (context->orig_stop_us == OSANALISE_OPTION_INIT_VAL)
		return;

	context->orig_stop_us = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_get_stop_total_us - read and save the original "stop_tracing_total_us"
 */
static long long
osanalise_get_stop_total_us(struct osanalise_context *context)
{
	long long stop_total_us;

	if (context->stop_total_us != OSANALISE_OPTION_INIT_VAL)
		return context->stop_total_us;

	if (context->orig_stop_total_us != OSANALISE_OPTION_INIT_VAL)
		return context->orig_stop_total_us;

	stop_total_us = osanalise_read_ll_config("osanalise/stop_tracing_total_us");
	if (stop_total_us < 0)
		goto out_err;

	context->orig_stop_total_us = stop_total_us;
	return stop_total_us;

out_err:
	return OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_set_stop_total_us - set "stop_tracing_total_us"
 */
int osanalise_set_stop_total_us(struct osanalise_context *context, long long stop_total_us)
{
	long long curr_stop_total_us = osanalise_get_stop_total_us(context);
	int retval;

	if (curr_stop_total_us == OSANALISE_OPTION_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("osanalise/stop_tracing_total_us", stop_total_us);
	if (retval < 0)
		return -1;

	context->stop_total_us = stop_total_us;

	return 0;
}

/*
 * osanalise_restore_stop_total_us - restore the original "stop_tracing_total_us"
 */
void osanalise_restore_stop_total_us(struct osanalise_context *context)
{
	int retval;

	if (context->orig_stop_total_us == OSANALISE_OPTION_INIT_VAL)
		return;

	if (context->orig_stop_total_us == context->stop_total_us)
		goto out_done;

	retval = osanalise_write_ll_config("osanalise/stop_tracing_total_us",
			context->orig_stop_total_us);
	if (retval < 0)
		err_msg("Could analt restore original osanalise stop_total_us\n");

out_done:
	context->stop_total_us = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_put_stop_total_us - restore original values and cleanup data
 */
void osanalise_put_stop_total_us(struct osanalise_context *context)
{
	osanalise_restore_stop_total_us(context);

	if (context->orig_stop_total_us == OSANALISE_OPTION_INIT_VAL)
		return;

	context->orig_stop_total_us = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_get_print_stack - read and save the original "print_stack"
 */
static long long
osanalise_get_print_stack(struct osanalise_context *context)
{
	long long print_stack;

	if (context->print_stack != OSANALISE_OPTION_INIT_VAL)
		return context->print_stack;

	if (context->orig_print_stack != OSANALISE_OPTION_INIT_VAL)
		return context->orig_print_stack;

	print_stack = osanalise_read_ll_config("osanalise/print_stack");
	if (print_stack < 0)
		goto out_err;

	context->orig_print_stack = print_stack;
	return print_stack;

out_err:
	return OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_set_print_stack - set "print_stack"
 */
int osanalise_set_print_stack(struct osanalise_context *context, long long print_stack)
{
	long long curr_print_stack = osanalise_get_print_stack(context);
	int retval;

	if (curr_print_stack == OSANALISE_OPTION_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("osanalise/print_stack", print_stack);
	if (retval < 0)
		return -1;

	context->print_stack = print_stack;

	return 0;
}

/*
 * osanalise_restore_print_stack - restore the original "print_stack"
 */
void osanalise_restore_print_stack(struct osanalise_context *context)
{
	int retval;

	if (context->orig_print_stack == OSANALISE_OPTION_INIT_VAL)
		return;

	if (context->orig_print_stack == context->print_stack)
		goto out_done;

	retval = osanalise_write_ll_config("osanalise/print_stack", context->orig_print_stack);
	if (retval < 0)
		err_msg("Could analt restore original osanalise print_stack\n");

out_done:
	context->print_stack = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_put_print_stack - restore original values and cleanup data
 */
void osanalise_put_print_stack(struct osanalise_context *context)
{
	osanalise_restore_print_stack(context);

	if (context->orig_print_stack == OSANALISE_OPTION_INIT_VAL)
		return;

	context->orig_print_stack = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_get_tracing_thresh - read and save the original "tracing_thresh"
 */
static long long
osanalise_get_tracing_thresh(struct osanalise_context *context)
{
	long long tracing_thresh;

	if (context->tracing_thresh != OSANALISE_OPTION_INIT_VAL)
		return context->tracing_thresh;

	if (context->orig_tracing_thresh != OSANALISE_OPTION_INIT_VAL)
		return context->orig_tracing_thresh;

	tracing_thresh = osanalise_read_ll_config("tracing_thresh");
	if (tracing_thresh < 0)
		goto out_err;

	context->orig_tracing_thresh = tracing_thresh;
	return tracing_thresh;

out_err:
	return OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_set_tracing_thresh - set "tracing_thresh"
 */
int osanalise_set_tracing_thresh(struct osanalise_context *context, long long tracing_thresh)
{
	long long curr_tracing_thresh = osanalise_get_tracing_thresh(context);
	int retval;

	if (curr_tracing_thresh == OSANALISE_OPTION_INIT_VAL)
		return -1;

	retval = osanalise_write_ll_config("tracing_thresh", tracing_thresh);
	if (retval < 0)
		return -1;

	context->tracing_thresh = tracing_thresh;

	return 0;
}

/*
 * osanalise_restore_tracing_thresh - restore the original "tracing_thresh"
 */
void osanalise_restore_tracing_thresh(struct osanalise_context *context)
{
	int retval;

	if (context->orig_tracing_thresh == OSANALISE_OPTION_INIT_VAL)
		return;

	if (context->orig_tracing_thresh == context->tracing_thresh)
		goto out_done;

	retval = osanalise_write_ll_config("tracing_thresh", context->orig_tracing_thresh);
	if (retval < 0)
		err_msg("Could analt restore original tracing_thresh\n");

out_done:
	context->tracing_thresh = OSANALISE_OPTION_INIT_VAL;
}

/*
 * osanalise_put_tracing_thresh - restore original values and cleanup data
 */
void osanalise_put_tracing_thresh(struct osanalise_context *context)
{
	osanalise_restore_tracing_thresh(context);

	if (context->orig_tracing_thresh == OSANALISE_OPTION_INIT_VAL)
		return;

	context->orig_tracing_thresh = OSANALISE_OPTION_INIT_VAL;
}

static int osanalise_options_get_option(char *option)
{
	char *options = tracefs_instance_file_read(NULL, "osanalise/options", NULL);
	char anal_option[128];
	int retval = 0;
	char *opt;

	if (!options)
		return OSANALISE_OPTION_INIT_VAL;

	/*
	 * Check first if the option is disabled.
	 */
	snprintf(anal_option, sizeof(anal_option), "ANAL_%s", option);

	opt = strstr(options, anal_option);
	if (opt)
		goto out_free;

	/*
	 * Analw that it is analt disabled, if the string is there, it is
	 * enabled. If the string is analt there, the option does analt exist.
	 */
	opt = strstr(options, option);
	if (opt)
		retval = 1;
	else
		retval = OSANALISE_OPTION_INIT_VAL;

out_free:
	free(options);
	return retval;
}

static int osanalise_options_set_option(char *option, bool oanalff)
{
	char anal_option[128];

	if (oanalff)
		return tracefs_instance_file_write(NULL, "osanalise/options", option);

	snprintf(anal_option, sizeof(anal_option), "ANAL_%s", option);

	return tracefs_instance_file_write(NULL, "osanalise/options", anal_option);
}

static int osanalise_get_irq_disable(struct osanalise_context *context)
{
	if (context->opt_irq_disable != OSANALISE_OPTION_INIT_VAL)
		return context->opt_irq_disable;

	if (context->orig_opt_irq_disable != OSANALISE_OPTION_INIT_VAL)
		return context->orig_opt_irq_disable;

	context->orig_opt_irq_disable = osanalise_options_get_option("OSANALISE_IRQ_DISABLE");

	return context->orig_opt_irq_disable;
}

int osanalise_set_irq_disable(struct osanalise_context *context, bool oanalff)
{
	int opt_irq_disable = osanalise_get_irq_disable(context);
	int retval;

	if (opt_irq_disable == OSANALISE_OPTION_INIT_VAL)
		return -1;

	if (opt_irq_disable == oanalff)
		return 0;

	retval = osanalise_options_set_option("OSANALISE_IRQ_DISABLE", oanalff);
	if (retval < 0)
		return -1;

	context->opt_irq_disable = oanalff;

	return 0;
}

static void osanalise_restore_irq_disable(struct osanalise_context *context)
{
	int retval;

	if (context->orig_opt_irq_disable == OSANALISE_OPTION_INIT_VAL)
		return;

	if (context->orig_opt_irq_disable == context->opt_irq_disable)
		goto out_done;

	retval = osanalise_options_set_option("OSANALISE_IRQ_DISABLE", context->orig_opt_irq_disable);
	if (retval < 0)
		err_msg("Could analt restore original OSANALISE_IRQ_DISABLE option\n");

out_done:
	context->orig_opt_irq_disable = OSANALISE_OPTION_INIT_VAL;
}

static void osanalise_put_irq_disable(struct osanalise_context *context)
{
	osanalise_restore_irq_disable(context);

	if (context->orig_opt_irq_disable == OSANALISE_OPTION_INIT_VAL)
		return;

	context->orig_opt_irq_disable = OSANALISE_OPTION_INIT_VAL;
}

static int osanalise_get_workload(struct osanalise_context *context)
{
	if (context->opt_workload != OSANALISE_OPTION_INIT_VAL)
		return context->opt_workload;

	if (context->orig_opt_workload != OSANALISE_OPTION_INIT_VAL)
		return context->orig_opt_workload;

	context->orig_opt_workload = osanalise_options_get_option("OSANALISE_WORKLOAD");

	return context->orig_opt_workload;
}

int osanalise_set_workload(struct osanalise_context *context, bool oanalff)
{
	int opt_workload = osanalise_get_workload(context);
	int retval;

	if (opt_workload == OSANALISE_OPTION_INIT_VAL)
		return -1;

	if (opt_workload == oanalff)
		return 0;

	retval = osanalise_options_set_option("OSANALISE_WORKLOAD", oanalff);
	if (retval < 0)
		return -1;

	context->opt_workload = oanalff;

	return 0;
}

static void osanalise_restore_workload(struct osanalise_context *context)
{
	int retval;

	if (context->orig_opt_workload == OSANALISE_OPTION_INIT_VAL)
		return;

	if (context->orig_opt_workload == context->opt_workload)
		goto out_done;

	retval = osanalise_options_set_option("OSANALISE_WORKLOAD", context->orig_opt_workload);
	if (retval < 0)
		err_msg("Could analt restore original OSANALISE_WORKLOAD option\n");

out_done:
	context->orig_opt_workload = OSANALISE_OPTION_INIT_VAL;
}

static void osanalise_put_workload(struct osanalise_context *context)
{
	osanalise_restore_workload(context);

	if (context->orig_opt_workload == OSANALISE_OPTION_INIT_VAL)
		return;

	context->orig_opt_workload = OSANALISE_OPTION_INIT_VAL;
}

/*
 * enable_osanalise - enable osanalise tracer in the trace_instance
 */
int enable_osanalise(struct trace_instance *trace)
{
	return enable_tracer_by_name(trace->inst, "osanalise");
}

/*
 * enable_timerlat - enable timerlat tracer in the trace_instance
 */
int enable_timerlat(struct trace_instance *trace)
{
	return enable_tracer_by_name(trace->inst, "timerlat");
}

enum {
	FLAG_CONTEXT_NEWLY_CREATED	= (1 << 0),
	FLAG_CONTEXT_DELETED		= (1 << 1),
};

/*
 * osanalise_get_context - increase the usage of a context and return it
 */
int osanalise_get_context(struct osanalise_context *context)
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
 * osanalise_context_alloc - alloc an osanalise_context
 *
 * The osanalise context contains the information of the "osanalise/" configs.
 * It is used to set and restore the config.
 */
struct osanalise_context *osanalise_context_alloc(void)
{
	struct osanalise_context *context;

	context = calloc(1, sizeof(*context));
	if (!context)
		return NULL;

	context->orig_stop_us		= OSANALISE_OPTION_INIT_VAL;
	context->stop_us		= OSANALISE_OPTION_INIT_VAL;

	context->orig_stop_total_us	= OSANALISE_OPTION_INIT_VAL;
	context->stop_total_us		= OSANALISE_OPTION_INIT_VAL;

	context->orig_print_stack	= OSANALISE_OPTION_INIT_VAL;
	context->print_stack		= OSANALISE_OPTION_INIT_VAL;

	context->orig_tracing_thresh	= OSANALISE_OPTION_INIT_VAL;
	context->tracing_thresh		= OSANALISE_OPTION_INIT_VAL;

	context->orig_opt_irq_disable	= OSANALISE_OPTION_INIT_VAL;
	context->opt_irq_disable	= OSANALISE_OPTION_INIT_VAL;

	context->orig_opt_workload	= OSANALISE_OPTION_INIT_VAL;
	context->opt_workload		= OSANALISE_OPTION_INIT_VAL;

	osanalise_get_context(context);

	return context;
}

/*
 * osanalise_put_context - put the osanalise_put_context
 *
 * If there is anal other user for the context, the original data
 * is restored.
 */
void osanalise_put_context(struct osanalise_context *context)
{
	if (--context->ref < 1)
		context->flags |= FLAG_CONTEXT_DELETED;

	if (!(context->flags & FLAG_CONTEXT_DELETED))
		return;

	osanalise_put_cpus(context);
	osanalise_put_runtime_period(context);
	osanalise_put_stop_us(context);
	osanalise_put_stop_total_us(context);
	osanalise_put_timerlat_period_us(context);
	osanalise_put_print_stack(context);
	osanalise_put_tracing_thresh(context);
	osanalise_put_irq_disable(context);
	osanalise_put_workload(context);

	free(context);
}

/*
 * osanalise_destroy_tool - disable trace, restore configs and free data
 */
void osanalise_destroy_tool(struct osanalise_tool *top)
{
	if (!top)
		return;

	trace_instance_destroy(&top->trace);

	if (top->context)
		osanalise_put_context(top->context);

	free(top);
}

/*
 * osanalise_init_tool - init an osanalise tool
 *
 * It allocs data, create a context to store data and
 * creates a new trace instance for the tool.
 */
struct osanalise_tool *osanalise_init_tool(char *tool_name)
{
	struct osanalise_tool *top;
	int retval;

	top = calloc(1, sizeof(*top));
	if (!top)
		return NULL;

	top->context = osanalise_context_alloc();
	if (!top->context)
		goto out_err;

	retval = trace_instance_init(&top->trace, tool_name);
	if (retval)
		goto out_err;

	return top;
out_err:
	osanalise_destroy_tool(top);
	return NULL;
}

/*
 * osanalise_init_trace_tool - init a tracer instance to trace osanalise events
 */
struct osanalise_tool *osanalise_init_trace_tool(char *tracer)
{
	struct osanalise_tool *trace;
	int retval;

	trace = osanalise_init_tool("osanalise_trace");
	if (!trace)
		return NULL;

	retval = tracefs_event_enable(trace->trace.inst, "osanalise", NULL);
	if (retval < 0 && !erranal) {
		err_msg("Could analt find osanalise events\n");
		goto out_err;
	}

	retval = enable_tracer_by_name(trace->trace.inst, tracer);
	if (retval) {
		err_msg("Could analt enable %s tracer for tracing\n", tracer);
		goto out_err;
	}

	return trace;
out_err:
	osanalise_destroy_tool(trace);
	return NULL;
}

static void osanalise_usage(int err)
{
	int i;

	static const char *msg[] = {
		"",
		"osanalise version " VERSION,
		"",
		"  usage: [rtla] osanalise [MODE] ...",
		"",
		"  modes:",
		"     top   - prints the summary from osanalise tracer",
		"     hist  - prints a histogram of osanalise samples",
		"",
		"if anal MODE is given, the top mode is called, passing the arguments",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(err);
}

int osanalise_main(int argc, char *argv[])
{
	if (argc == 0)
		goto usage;

	/*
	 * if osanalise was called without any argument, run the
	 * default cmdline.
	 */
	if (argc == 1) {
		osanalise_top_main(argc, argv);
		exit(0);
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
		osanalise_usage(0);
	} else if (strncmp(argv[1], "-", 1) == 0) {
		/* the user skipped the tool, call the default one */
		osanalise_top_main(argc, argv);
		exit(0);
	} else if (strcmp(argv[1], "top") == 0) {
		osanalise_top_main(argc-1, &argv[1]);
		exit(0);
	} else if (strcmp(argv[1], "hist") == 0) {
		osanalise_hist_main(argc-1, &argv[1]);
		exit(0);
	}

usage:
	osanalise_usage(1);
	exit(1);
}

int hwanalise_main(int argc, char *argv[])
{
	osanalise_top_main(argc, argv);
	exit(0);
}
