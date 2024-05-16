// SPDX-License-Identifier: GPL-2.0
#include "trace.h"

/*
 * osnoise_context - read, store, write, restore osnoise configs.
 */
struct osnoise_context {
	int			flags;
	int			ref;

	char			*curr_cpus;
	char			*orig_cpus;

	/* 0 as init value */
	unsigned long long	orig_runtime_us;
	unsigned long long	runtime_us;

	/* 0 as init value */
	unsigned long long	orig_period_us;
	unsigned long long	period_us;

	/* 0 as init value */
	long long		orig_timerlat_period_us;
	long long		timerlat_period_us;

	/* 0 as init value */
	long long		orig_tracing_thresh;
	long long		tracing_thresh;

	/* -1 as init value because 0 is disabled */
	long long		orig_stop_us;
	long long		stop_us;

	/* -1 as init value because 0 is disabled */
	long long		orig_stop_total_us;
	long long		stop_total_us;

	/* -1 as init value because 0 is disabled */
	long long		orig_print_stack;
	long long		print_stack;

	/* -1 as init value because 0 is off */
	int			orig_opt_irq_disable;
	int			opt_irq_disable;

	/* -1 as init value because 0 is off */
	int			orig_opt_workload;
	int			opt_workload;
};

/*
 * *_INIT_VALs are also invalid values, they are used to
 * communicate errors.
 */
#define OSNOISE_OPTION_INIT_VAL	(-1)
#define OSNOISE_TIME_INIT_VAL	(0)

struct osnoise_context *osnoise_context_alloc(void);
int osnoise_get_context(struct osnoise_context *context);
void osnoise_put_context(struct osnoise_context *context);

int osnoise_set_cpus(struct osnoise_context *context, char *cpus);
void osnoise_restore_cpus(struct osnoise_context *context);

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
int osnoise_set_workload(struct osnoise_context *context, bool onoff);

/*
 * osnoise_tool -  osnoise based tool definition.
 */
struct osnoise_tool {
	struct trace_instance		trace;
	struct osnoise_context		*context;
	void				*data;
	void				*params;
	time_t				start_time;
};

void osnoise_destroy_tool(struct osnoise_tool *top);
struct osnoise_tool *osnoise_init_tool(char *tool_name);
struct osnoise_tool *osnoise_init_trace_tool(char *tracer);

int osnoise_hist_main(int argc, char *argv[]);
int osnoise_top_main(int argc, char **argv);
int osnoise_main(int argc, char **argv);
int hwnoise_main(int argc, char **argv);
