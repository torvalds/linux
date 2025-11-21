// SPDX-License-Identifier: GPL-2.0
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
	struct common_params	common;
	long long		timerlat_period_us;
	long long		print_stack;
	int			dma_latency;
	int			no_aa;
	int			dump_tasks;
	int			deepest_idle_state;
	enum timerlat_tracing_mode mode;
};

#define to_timerlat_params(ptr) container_of(ptr, struct timerlat_params, common)

int timerlat_apply_config(struct osnoise_tool *tool, struct timerlat_params *params);
int timerlat_main(int argc, char *argv[]);
int timerlat_enable(struct osnoise_tool *tool);
void timerlat_analyze(struct osnoise_tool *tool, bool stopped);
void timerlat_free(struct osnoise_tool *tool);

