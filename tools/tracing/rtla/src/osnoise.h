// SPDX-License-Identifier: GPL-2.0
#pragma once

#include "common.h"

enum osnoise_mode {
	MODE_OSNOISE = 0,
	MODE_HWNOISE
};

struct osnoise_params {
	struct common_params	common;
	unsigned long long	runtime;
	unsigned long long	period;
	long long		threshold;
	enum osnoise_mode	mode;
};

#define to_osnoise_params(ptr) container_of(ptr, struct osnoise_params, common)

/*
 * *_INIT_VALs are also invalid values, they are used to
 * communicate errors.
 */
#define OSNOISE_OPTION_INIT_VAL	(-1)
#define OSNOISE_TIME_INIT_VAL	(0)

struct osnoise_context *osnoise_context_alloc(void);
int osnoise_get_context(struct osnoise_context *context);
void osnoise_put_context(struct osnoise_context *context);

int osnoise_set_runtime_period(struct osnoise_context *context,
			       unsigned long long runtime,
			       unsigned long long period);
void osnoise_restore_runtime_period(struct osnoise_context *context);

int osnoise_set_stop_us(struct osnoise_context *context,
			long long stop_us);
void osnoise_restore_stop_us(struct osnoise_context *context);

int osnoise_set_stop_total_us(struct osnoise_context *context,
			      long long stop_total_us);
void osnoise_restore_stop_total_us(struct osnoise_context *context);

int osnoise_set_timerlat_period_us(struct osnoise_context *context,
				   long long timerlat_period_us);
void osnoise_restore_timerlat_period_us(struct osnoise_context *context);

int osnoise_set_tracing_thresh(struct osnoise_context *context,
			       long long tracing_thresh);
void osnoise_restore_tracing_thresh(struct osnoise_context *context);

void osnoise_restore_print_stack(struct osnoise_context *context);
int osnoise_set_print_stack(struct osnoise_context *context,
			    long long print_stack);

int osnoise_set_irq_disable(struct osnoise_context *context, bool onoff);
void osnoise_report_missed_events(struct osnoise_tool *tool);
int osnoise_apply_config(struct osnoise_tool *tool, struct osnoise_params *params);

int osnoise_hist_main(int argc, char *argv[]);
int osnoise_top_main(int argc, char **argv);
int osnoise_enable(struct osnoise_tool *tool);
int osnoise_main(int argc, char **argv);
int hwnoise_main(int argc, char **argv);

extern struct tool_ops timerlat_top_ops, timerlat_hist_ops;
extern struct tool_ops osnoise_top_ops, osnoise_hist_ops;

int run_tool(struct tool_ops *ops, int argc, char *argv[]);
int hist_main_loop(struct osnoise_tool *tool);
