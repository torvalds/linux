// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-kwork.c
 *
 * Copyright (c) 2022  Huawei Inc,  Yang Jihong <yangjihong1@huawei.com>
 */

#include "builtin.h"

#include "util/data.h"
#include "util/kwork.h"
#include "util/debug.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/string2.h"
#include "util/callchain.h"
#include "util/evsel_fprintf.h"

#include <subcmd/pager.h>
#include <subcmd/parse-options.h>

#include <errno.h>
#include <inttypes.h>
#include <linux/err.h>
#include <linux/time64.h>
#include <linux/zalloc.h>

const struct evsel_str_handler irq_tp_handlers[] = {
	{ "irq:irq_handler_entry", NULL, },
	{ "irq:irq_handler_exit",  NULL, },
};

static struct kwork_class kwork_irq = {
	.name           = "irq",
	.type           = KWORK_CLASS_IRQ,
	.nr_tracepoints = 2,
	.tp_handlers    = irq_tp_handlers,
};

const struct evsel_str_handler softirq_tp_handlers[] = {
	{ "irq:softirq_raise", NULL, },
	{ "irq:softirq_entry", NULL, },
	{ "irq:softirq_exit",  NULL, },
};

static struct kwork_class kwork_softirq = {
	.name           = "softirq",
	.type           = KWORK_CLASS_SOFTIRQ,
	.nr_tracepoints = 3,
	.tp_handlers    = softirq_tp_handlers,
};

static struct kwork_class *kwork_class_supported_list[KWORK_CLASS_MAX] = {
	[KWORK_CLASS_IRQ]       = &kwork_irq,
	[KWORK_CLASS_SOFTIRQ]   = &kwork_softirq,
};

static void setup_event_list(struct perf_kwork *kwork,
			     const struct option *options,
			     const char * const usage_msg[])
{
	int i;
	struct kwork_class *class;
	char *tmp, *tok, *str;

	if (kwork->event_list_str == NULL)
		goto null_event_list_str;

	str = strdup(kwork->event_list_str);
	for (tok = strtok_r(str, ", ", &tmp);
	     tok; tok = strtok_r(NULL, ", ", &tmp)) {
		for (i = 0; i < KWORK_CLASS_MAX; i++) {
			class = kwork_class_supported_list[i];
			if (strcmp(tok, class->name) == 0) {
				list_add_tail(&class->list, &kwork->class_list);
				break;
			}
		}
		if (i == KWORK_CLASS_MAX) {
			usage_with_options_msg(usage_msg, options,
					       "Unknown --event key: `%s'", tok);
		}
	}
	free(str);

null_event_list_str:
	/*
	 * config all kwork events if not specified
	 */
	if (list_empty(&kwork->class_list)) {
		for (i = 0; i < KWORK_CLASS_MAX; i++) {
			list_add_tail(&kwork_class_supported_list[i]->list,
				      &kwork->class_list);
		}
	}

	pr_debug("Config event list:");
	list_for_each_entry(class, &kwork->class_list, list)
		pr_debug(" %s", class->name);
	pr_debug("\n");
}

static int perf_kwork__record(struct perf_kwork *kwork,
			      int argc, const char **argv)
{
	const char **rec_argv;
	unsigned int rec_argc, i, j;
	struct kwork_class *class;

	const char *const record_args[] = {
		"record",
		"-a",
		"-R",
		"-m", "1024",
		"-c", "1",
	};

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;

	list_for_each_entry(class, &kwork->class_list, list)
		rec_argc += 2 * class->nr_tracepoints;

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	list_for_each_entry(class, &kwork->class_list, list) {
		for (j = 0; j < class->nr_tracepoints; j++) {
			rec_argv[i++] = strdup("-e");
			rec_argv[i++] = strdup(class->tp_handlers[j].name);
		}
	}

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	pr_debug("record comm: ");
	for (j = 0; j < rec_argc; j++)
		pr_debug("%s ", rec_argv[j]);
	pr_debug("\n");

	return cmd_record(i, rec_argv);
}

int cmd_kwork(int argc, const char **argv)
{
	static struct perf_kwork kwork = {
		.class_list          = LIST_HEAD_INIT(kwork.class_list),
		.force               = false,
		.event_list_str      = NULL,
	};
	const struct option kwork_options[] = {
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "kwork", &kwork.event_list_str, "kwork",
		   "list of kwork to profile (irq, softirq, etc)"),
	OPT_BOOLEAN('f', "force", &kwork.force, "don't complain, do it"),
	OPT_END()
	};
	const char *kwork_usage[] = {
		NULL,
		NULL
	};
	const char *const kwork_subcommands[] = {
		"record", NULL
	};

	argc = parse_options_subcommand(argc, argv, kwork_options,
					kwork_subcommands, kwork_usage,
					PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(kwork_usage, kwork_options);

	setup_event_list(&kwork, kwork_options, kwork_usage);

	if (strlen(argv[0]) > 2 && strstarts("record", argv[0]))
		return perf_kwork__record(&kwork, argc, argv);
	else
		usage_with_options(kwork_usage, kwork_options);

	return 0;
}
