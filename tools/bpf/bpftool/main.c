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
#include <getopt.h>
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
json_writer_t *json_wtr;
bool pretty_output;
bool json_output;
bool show_pinned;
struct pinned_obj_table prog_table;
struct pinned_obj_table map_table;

static void __noreturn clean_and_exit(int i)
{
	if (json_output)
		jsonw_destroy(&json_wtr);

	exit(i);
}

void usage(void)
{
	last_do_help(last_argc - 1, last_argv + 1);

	clean_and_exit(-1);
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s [OPTIONS] OBJECT { COMMAND | help }\n"
		"       %s batch file FILE\n"
		"       %s version\n"
		"\n"
		"       OBJECT := { prog | map | cgroup }\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, bin_name, bin_name);

	return 0;
}

static int do_version(int argc, char **argv)
{
	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "version");
		jsonw_printf(json_wtr, "\"%s\"", BPFTOOL_VERSION);
		jsonw_end_object(json_wtr);
	} else {
		printf("%s v%s\n", bin_name, BPFTOOL_VERSION);
	}
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

void fprint_hex(FILE *f, void *arg, unsigned int n, const char *sep)
{
	unsigned char *data = arg;
	unsigned int i;

	for (i = 0; i < n; i++) {
		const char *pfx = "";

		if (!i)
			/* nothing */;
		else if (!(i % 16))
			fprintf(f, "\n");
		else if (!(i % 8))
			fprintf(f, "  ");
		else
			pfx = sep;

		fprintf(f, "%s%02hhx", i ? pfx : "", data[i]);
	}
}

static int do_batch(int argc, char **argv);

static const struct cmd cmds[] = {
	{ "help",	do_help },
	{ "batch",	do_batch },
	{ "prog",	do_prog },
	{ "map",	do_map },
	{ "cgroup",	do_cgroup },
	{ "version",	do_version },
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
	int i;

	if (argc < 2) {
		p_err("too few parameters for batch");
		return -1;
	} else if (!is_prefix(*argv, "file")) {
		p_err("expected 'file', got: %s", *argv);
		return -1;
	} else if (argc > 2) {
		p_err("too many parameters for batch");
		return -1;
	}
	NEXT_ARG();

	fp = fopen(*argv, "r");
	if (!fp) {
		p_err("Can't open file (%s): %s", *argv, strerror(errno));
		return -1;
	}

	if (json_output)
		jsonw_start_array(json_wtr);
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
				p_err("line %d has too many arguments, skip",
				      lines);
				n_argc = 0;
				break;
			}
			n_argv[n_argc] = strtok(NULL, " \t\n");
		}

		if (!n_argc)
			continue;

		if (json_output) {
			jsonw_start_object(json_wtr);
			jsonw_name(json_wtr, "command");
			jsonw_start_array(json_wtr);
			for (i = 0; i < n_argc; i++)
				jsonw_string(json_wtr, n_argv[i]);
			jsonw_end_array(json_wtr);
			jsonw_name(json_wtr, "output");
		}

		err = cmd_select(cmds, n_argc, n_argv, do_help);

		if (json_output)
			jsonw_end_object(json_wtr);

		if (err)
			goto err_close;

		lines++;
	}

	if (errno && errno != ENOENT) {
		p_err("reading batch file failed: %s", strerror(errno));
		err = -1;
	} else {
		p_info("processed %d lines", lines);
		err = 0;
	}
err_close:
	fclose(fp);

	if (json_output)
		jsonw_end_array(json_wtr);

	return err;
}

int main(int argc, char **argv)
{
	static const struct option options[] = {
		{ "json",	no_argument,	NULL,	'j' },
		{ "help",	no_argument,	NULL,	'h' },
		{ "pretty",	no_argument,	NULL,	'p' },
		{ "version",	no_argument,	NULL,	'V' },
		{ "bpffs",	no_argument,	NULL,	'f' },
		{ 0 }
	};
	int opt, ret;

	last_do_help = do_help;
	pretty_output = false;
	json_output = false;
	show_pinned = false;
	bin_name = argv[0];

	hash_init(prog_table.table);
	hash_init(map_table.table);

	opterr = 0;
	while ((opt = getopt_long(argc, argv, "Vhpjf",
				  options, NULL)) >= 0) {
		switch (opt) {
		case 'V':
			return do_version(argc, argv);
		case 'h':
			return do_help(argc, argv);
		case 'p':
			pretty_output = true;
			/* fall through */
		case 'j':
			if (!json_output) {
				json_wtr = jsonw_new(stdout);
				if (!json_wtr) {
					p_err("failed to create JSON writer");
					return -1;
				}
				json_output = true;
			}
			jsonw_pretty(json_wtr, pretty_output);
			break;
		case 'f':
			show_pinned = true;
			break;
		default:
			p_err("unrecognized option '%s'", argv[optind - 1]);
			if (json_output)
				clean_and_exit(-1);
			else
				usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc < 0)
		usage();

	bfd_init();

	ret = cmd_select(cmds, argc, argv, do_help);

	if (json_output)
		jsonw_destroy(&json_wtr);

	if (show_pinned) {
		delete_pinned_obj_table(&prog_table);
		delete_pinned_obj_table(&map_table);
	}

	return ret;
}
