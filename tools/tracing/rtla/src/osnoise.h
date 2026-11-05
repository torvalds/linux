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

struct osnoise_context *osnoise_context_alloc(void);
int osnoise_get_context(struct osnoise_context *context);
void osnoise_put_context(struct osnoise_context *context);

int osnoise_set_runtime_period(struct osnoise_context *context,
			       long long runtime,
			       long long period);
void osnoise_restore_runtime_period(struct osnoise_context *context);

void osnoise_report_invalid_events(struct osnoise_tool *tool);
void osnoise_report_missed_events(struct osnoise_tool *tool);
int osnoise_apply_config(struct osnoise_tool *tool, struct osnoise_params *params);

int osnoise_enable(struct osnoise_tool *tool);
int osnoise_main(int argc, char **argv);
int hwnoise_main(int argc, char **argv);

int osnoise_init_ipi_filters(struct osnoise_tool *tool,
			     struct common_params *params,
			     bool *filters_enabled);

extern struct tool_ops timerlat_top_ops, timerlat_hist_ops;
extern struct tool_ops osnoise_top_ops, osnoise_hist_ops;

int run_tool(struct tool_ops *ops, int argc, char *argv[]);
