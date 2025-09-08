// SPDX-License-Identifier: GPL-2.0
#include "actions.h"
#include "osnoise.h"

/*
 * Define timerlat tracing mode.
 *
 * There are three tracing modes:
 * - tracefs-only, used when BPF is unavailable.
 * - BPF-only, used when BPF is available and neither trace saving nor
 * auto-analysis are enabled.
 * - mixed mode, used when BPF is available and either trace saving or
 * auto-analysis is enabled (which rely on sample collection through
 * tracefs).
 */
enum timerlat_tracing_mode {
	TRACING_MODE_BPF,
	TRACING_MODE_TRACEFS,
	TRACING_MODE_MIXED,
};

struct timerlat_params {
	/* Common params */
	char			*cpus;
	cpu_set_t		monitored_cpus;
	char			*cgroup_name;
	unsigned long long	runtime;
	long long		stop_us;
	long long		stop_total_us;
	long long		timerlat_period_us;
	long long		print_stack;
	int			sleep_time;
	int			output_divisor;
	int			duration;
	int			set_sched;
	int			dma_latency;
	int			no_aa;
	int			dump_tasks;
	int			cgroup;
	int			hk_cpus;
	int			user_workload;
	int			kernel_workload;
	int			user_data;
	int			warmup;
	int			buffer_size;
	int			deepest_idle_state;
	cpu_set_t		hk_cpu_set;
	struct sched_attr	sched_param;
	struct trace_events	*events;
	enum timerlat_tracing_mode mode;

	struct actions threshold_actions;
	struct actions end_actions;

	union {
		struct {
			/* top only */
			int			quiet;
			int			aa_only;
			int			pretty_output;
		};
		struct {
			/* hist only */
			char			no_irq;
			char			no_thread;
			char			no_header;
			char			no_summary;
			char			no_index;
			char			with_zeros;
			int			bucket_size;
			int			entries;
		};
	};
};

int timerlat_apply_config(struct osnoise_tool *tool, struct timerlat_params *params);

int timerlat_hist_main(int argc, char *argv[]);
int timerlat_top_main(int argc, char *argv[]);
int timerlat_main(int argc, char *argv[]);
