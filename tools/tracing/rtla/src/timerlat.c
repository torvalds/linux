// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "timerlat.h"

static void timerlat_usage(int err)
{
	int i;

	static const char * const msg[] = {
		"",
		"timerlat version " VERSION,
		"",
		"  usage: [rtla] timerlat [MODE] ...",
		"",
		"  modes:",
		"     top   - prints the summary from timerlat tracer",
		"     hist  - prints a histogram of timer latencies",
		"",
		"if no MODE is given, the top mode is called, passing the arguments",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(err);
}

int timerlat_main(int argc, char *argv[])
{
	if (argc == 0)
		goto usage;

	/*
	 * if timerlat was called without any argument, run the
	 * default cmdline.
	 */
	if (argc == 1) {
		timerlat_top_main(argc, argv);
		exit(0);
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
		timerlat_usage(0);
	} else if (strncmp(argv[1], "-", 1) == 0) {
		/* the user skipped the tool, call the default one */
		timerlat_top_main(argc, argv);
		exit(0);
	} else if (strcmp(argv[1], "top") == 0) {
		timerlat_top_main(argc-1, &argv[1]);
		exit(0);
	} else if (strcmp(argv[1], "hist") == 0) {
		timerlat_hist_main(argc-1, &argv[1]);
		exit(0);
	}

usage:
	timerlat_usage(1);
	exit(1);
}
