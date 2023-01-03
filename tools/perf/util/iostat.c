// SPDX-License-Identifier: GPL-2.0
#include "util/iostat.h"
#include "util/debug.h"

enum iostat_mode_t iostat_mode = IOSTAT_NONE;

__weak int iostat_prepare(struct evlist *evlist __maybe_unused,
			  struct perf_stat_config *config __maybe_unused)
{
	return -1;
}

__weak int iostat_parse(const struct option *opt __maybe_unused,
			 const char *str __maybe_unused,
			 int unset __maybe_unused)
{
	pr_err("iostat mode is not supported on current platform\n");
	return -1;
}

__weak void iostat_list(struct evlist *evlist __maybe_unused,
		       struct perf_stat_config *config __maybe_unused)
{
}

__weak void iostat_release(struct evlist *evlist __maybe_unused)
{
}

__weak void iostat_print_header_prefix(struct perf_stat_config *config __maybe_unused)
{
}

__weak void iostat_print_metric(struct perf_stat_config *config __maybe_unused,
				struct evsel *evsel __maybe_unused,
				struct perf_stat_output_ctx *out __maybe_unused)
{
}

__weak void iostat_prefix(struct evlist *evlist __maybe_unused,
			  struct perf_stat_config *config __maybe_unused,
			  char *prefix __maybe_unused,
			  struct timespec *ts __maybe_unused)
{
}

__weak void iostat_print_counters(struct evlist *evlist __maybe_unused,
				  struct perf_stat_config *config __maybe_unused,
				  struct timespec *ts __maybe_unused,
				  char *prefix __maybe_unused,
				  iostat_print_counter_t print_cnt_cb __maybe_unused,
				  void *arg __maybe_unused)
{
}
