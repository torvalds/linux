#include <linux/compiler.h>
#include "event.h"
#include "tests.h"
#include "stat.h"
#include "debug.h"

static bool has_term(struct stat_config_event *config,
		     u64 tag, u64 val)
{
	unsigned i;

	for (i = 0; i < config->nr; i++) {
		if ((config->data[i].tag == tag) &&
		    (config->data[i].val == val))
			return true;
	}

	return false;
}

static int process_event(struct perf_tool *tool __maybe_unused,
			 union perf_event *event,
			 struct perf_sample *sample __maybe_unused,
			 struct machine *machine __maybe_unused)
{
	struct stat_config_event *config = &event->stat_config;

#define HAS(term, val) \
	has_term(config, PERF_STAT_CONFIG_TERM__##term, val)

	TEST_ASSERT_VAL("wrong nr",        config->nr == PERF_STAT_CONFIG_TERM__MAX);
	TEST_ASSERT_VAL("wrong aggr_mode", HAS(AGGR_MODE, AGGR_CORE));
	TEST_ASSERT_VAL("wrong scale",     HAS(SCALE, 1));
	TEST_ASSERT_VAL("wrong interval",  HAS(INTERVAL, 1));

#undef HAS

	return 0;
}

int test__synthesize_stat_config(int subtest __maybe_unused)
{
	struct perf_stat_config stat_config = {
		.aggr_mode	= AGGR_CORE,
		.scale		= 1,
		.interval	= 1,
	};

	TEST_ASSERT_VAL("failed to synthesize stat_config",
		!perf_event__synthesize_stat_config(NULL, &stat_config, process_event, NULL));

	return 0;
}
