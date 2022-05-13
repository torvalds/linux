// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "osnoise.h"
#include "timerlat.h"

/*
 * rtla_usage - print rtla usage
 */
static void rtla_usage(void)
{
	int i;

	static const char *msg[] = {
		"",
		"rtla version " VERSION,
		"",
		"  usage: rtla COMMAND ...",
		"",
		"  commands:",
		"     osnoise  - gives information about the operating system noise (osnoise)",
		"     timerlat - measures the timer irq and thread latency",
		"",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(1);
}

/*
 * run_command - try to run a rtla tool command
 *
 * It returns 0 if it fails. The tool's main will generally not
 * return as they should call exit().
 */
int run_command(int argc, char **argv, int start_position)
{
	if (strcmp(argv[start_position], "osnoise") == 0) {
		osnoise_main(argc-start_position, &argv[start_position]);
		goto ran;
	} else if (strcmp(argv[start_position], "timerlat") == 0) {
		timerlat_main(argc-start_position, &argv[start_position]);
		goto ran;
	}

	return 0;
ran:
	return 1;
}

int main(int argc, char *argv[])
{
	int retval;

	/* is it an alias? */
	retval = run_command(argc, argv, 0);
	if (retval)
		exit(0);

	if (argc < 2)
		goto usage;

	if (strcmp(argv[1], "-h") == 0) {
		rtla_usage();
		exit(0);
	} else if (strcmp(argv[1], "--help") == 0) {
		rtla_usage();
		exit(0);
	}

	retval = run_command(argc, argv, 1);
	if (retval)
		exit(0);

usage:
	rtla_usage();
	exit(1);
}
