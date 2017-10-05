/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Author: Jakub Kicinski <kubakici@wp.pl> */

#include <bfd.h>
#include <ctype.h>
#include <errno.h>
#include <linux/bpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bpf.h>

#include "main.h"

const char *bin_name;
static int last_argc;
static char **last_argv;
static int (*last_do_help)(int argc, char **argv);

void usage(void)
{
	last_do_help(last_argc - 1, last_argv + 1);

	exit(-1);
}

static int do_help(int argc, char **argv)
{
	fprintf(stderr,
		"Usage: %s OBJECT { COMMAND | help }\n"
		"       %s batch file FILE\n"
		"\n"
		"       OBJECT := { prog | map }\n",
		bin_name, bin_name);

	return 0;
}

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv))
{
	unsigned int i;

	last_argc = argc;
	last_argv = argv;
	last_do_help = help;

	if (argc < 1 && cmds[0].func)
		return cmds[0].func(argc, argv);

	for (i = 0; cmds[i].func; i++)
		if (is_prefix(*argv, cmds[i].cmd))
			return cmds[i].func(argc - 1, argv + 1);

	help(argc - 1, argv + 1);

	return -1;
}

bool is_prefix(const char *pfx, const char *str)
{
	if (!pfx)
		return false;
	if (strlen(str) < strlen(pfx))
		return false;

	return !memcmp(str, pfx, strlen(pfx));
}

void print_hex(void *arg, unsigned int n, const char *sep)
{
	unsigned char *data = arg;
	unsigned int i;

	for (i = 0; i < n; i++) {
		const char *pfx = "";

		if (!i)
			/* nothing */;
		else if (!(i % 16))
			printf("\n");
		else if (!(i % 8))
			printf("  ");
		else
			pfx = sep;

		printf("%s%02hhx", i ? pfx : "", data[i]);
	}
}

static int do_batch(int argc, char **argv);

static const struct cmd cmds[] = {
	{ "help",	do_help },
	{ "batch",	do_batch },
	{ "prog",	do_prog },
	{ "map",	do_map },
	{ 0 }
};

static int do_batch(int argc, char **argv)
{
	unsigned int lines = 0;
	char *n_argv[4096];
	char buf[65536];
	int n_argc;
	FILE *fp;
	int err;

	if (argc < 2) {
		err("too few parameters for batch\n");
		return -1;
	} else if (!is_prefix(*argv, "file")) {
		err("expected 'file', got: %s\n", *argv);
		return -1;
	} else if (argc > 2) {
		err("too many parameters for batch\n");
		return -1;
	}
	NEXT_ARG();

	fp = fopen(*argv, "r");
	if (!fp) {
		err("Can't open file (%s): %s\n", *argv, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if (strlen(buf) == sizeof(buf) - 1) {
			errno = E2BIG;
			break;
		}

		n_argc = 0;
		n_argv[n_argc] = strtok(buf, " \t\n");

		while (n_argv[n_argc]) {
			n_argc++;
			if (n_argc == ARRAY_SIZE(n_argv)) {
				err("line %d has too many arguments, skip\n",
				    lines);
				n_argc = 0;
				break;
			}
			n_argv[n_argc] = strtok(NULL, " \t\n");
		}

		if (!n_argc)
			continue;

		err = cmd_select(cmds, n_argc, n_argv, do_help);
		if (err)
			goto err_close;

		lines++;
	}

	if (errno && errno != ENOENT) {
		perror("reading batch file failed");
		err = -1;
	} else {
		info("processed %d lines\n", lines);
		err = 0;
	}
err_close:
	fclose(fp);

	return err;
}

int main(int argc, char **argv)
{
	bin_name = argv[0];
	NEXT_ARG();

	bfd_init();

	return cmd_select(cmds, argc, argv, do_help);
}
