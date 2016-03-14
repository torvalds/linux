/*
 * builtin-config.c
 *
 * Copyright (C) 2015, Taeung Song <treeze.taeung@gmail.com>
 *
 */
#include "builtin.h"

#include "perf.h"

#include "util/cache.h"
#include <subcmd/parse-options.h>
#include "util/util.h"
#include "util/debug.h"

static const char * const config_usage[] = {
	"perf config [options]",
	NULL
};

enum actions {
	ACTION_LIST = 1
} actions;

static struct option config_options[] = {
	OPT_SET_UINT('l', "list", &actions,
		     "show current config variables", ACTION_LIST),
	OPT_END()
};

static int show_config(const char *key, const char *value,
		       void *cb __maybe_unused)
{
	if (value)
		printf("%s=%s\n", key, value);
	else
		printf("%s\n", key);

	return 0;
}

int cmd_config(int argc, const char **argv, const char *prefix __maybe_unused)
{
	int ret = 0;

	argc = parse_options(argc, argv, config_options, config_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	switch (actions) {
	case ACTION_LIST:
		if (argc) {
			pr_err("Error: takes no arguments\n");
			parse_options_usage(config_usage, config_options, "l", 1);
		} else {
			ret = perf_config(show_config, NULL);
			if (ret < 0)
				pr_err("Nothing configured, "
				       "please check your ~/.perfconfig file\n");
		}
		break;
	default:
		usage_with_options(config_usage, config_options);
	}

	return ret;
}
