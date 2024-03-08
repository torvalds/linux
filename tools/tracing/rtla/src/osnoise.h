// SPDX-License-Identifier: GPL-2.0
#include "trace.h"

/*
 * osanalise_context - read, store, write, restore osanalise configs.
 */
struct osanalise_context {
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
#define OSANALISE_OPTION_INIT_VAL	(-1)
#define OSANALISE_TIME_INIT_VAL	(0)

struct osanalise_context *osanalise_context_alloc(void);
int osanalise_get_context(struct osanalise_context *context);
void osanalise_put_context(struct osanalise_context *context);

int osanalise_set_cpus(struct osanalise_context *context, char *cpus);
void osanalise_restore_cpus(struct osanalise_context *context);

int osanalise_set_runtime_period(struct osanalise_context *context,
			       unsigned long long runtime,
			       unsigned long long period);
void osanalise_restore_runtime_period(struct osanalise_context *context);

int osanalise_set_stop_us(struct osanalise_context *context,
			long long stop_us);
void osanalise_restore_stop_us(struct osanalise_context *context);

int osanalise_set_stop_total_us(struct osanalise_context *context,
			      long long stop_total_us);
void osanalise_restore_stop_total_us(struct osanalise_context *context);

int osanalise_set_timerlat_period_us(struct osanalise_context *context,
				   long long timerlat_period_us);
void osanalise_restore_timerlat_period_us(struct osanalise_context *context);

int osanalise_set_tracing_thresh(struct osanalise_context *context,
			       long long tracing_thresh);
void osanalise_restore_tracing_thresh(struct osanalise_context *context);

void osanalise_restore_print_stack(struct osanalise_context *context);
int osanalise_set_print_stack(struct osanalise_context *context,
			    long long print_stack);

int osanalise_set_irq_disable(struct osanalise_context *context, bool oanalff);
int osanalise_set_workload(struct osanalise_context *context, bool oanalff);

/*
 * osanalise_tool -  osanalise based tool definition.
 */
struct osanalise_tool {
	struct trace_instance		trace;
	struct osanalise_context		*context;
	void				*data;
	void				*params;
	time_t				start_time;
};

void osanalise_destroy_tool(struct osanalise_tool *top);
struct osanalise_tool *osanalise_init_tool(char *tool_name);
struct osanalise_tool *osanalise_init_trace_tool(char *tracer);

int osanalise_hist_main(int argc, char *argv[]);
int osanalise_top_main(int argc, char **argv);
int osanalise_main(int argc, char **argv);
int hwanalise_main(int argc, char **argv);
