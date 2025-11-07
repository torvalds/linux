// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */
/* Copyright Meta Platforms, Inc. and affiliates */

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "main.h"

const char *bin_name;
static int last_argc;
static char **last_argv;
static int (*last_do_help)(int argc, char **argv);
json_writer_t *json_wtr;
bool pretty_output;
bool json_output;

static void __attribute__((noreturn)) clean_and_exit(int i)
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

static int do_help(int argc __attribute__((unused)),
		   char **argv __attribute__((unused)))
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s [OPTIONS] OBJECT { COMMAND | help }\n"
		"       %s version\n"
		"\n"
		"       OBJECT := { page-pool | qstats }\n"
		"       " HELP_SPEC_OPTIONS "\n"
		"",
		bin_name, bin_name);

	return 0;
}

static int do_version(int argc __attribute__((unused)),
		      char **argv __attribute__((unused)))
{
	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "version");
		jsonw_printf(json_wtr, SRC_VERSION);
		jsonw_end_object(json_wtr);
	} else {
		printf("%s " SRC_VERSION "\n", bin_name);
	}
	return 0;
}

static const struct cmd commands[] = {
	{ "help",	do_help },
	{ "page-pool",	do_page_pool },
	{ "qstats",	do_qstats },
	{ "version",	do_version },
	{ 0 }
};

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv))
{
	unsigned int i;

	last_argc = argc;
	last_argv = argv;
	last_do_help = help;

	if (argc < 1 && cmds[0].func)
		return cmds[0].func(argc, argv);

	for (i = 0; cmds[i].cmd; i++) {
		if (is_prefix(*argv, cmds[i].cmd)) {
			if (!cmds[i].func) {
				p_err("command '%s' is not available", cmds[i].cmd);
				return -1;
			}
			return cmds[i].func(argc - 1, argv + 1);
		}
	}

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

/* Last argument MUST be NULL pointer */
int detect_common_prefix(const char *arg, ...)
{
	unsigned int count = 0;
	const char *ref;
	char msg[256];
	va_list ap;

	snprintf(msg, sizeof(msg), "ambiguous prefix: '%s' could be '", arg);
	va_start(ap, arg);
	while ((ref = va_arg(ap, const char *))) {
		if (!is_prefix(arg, ref))
			continue;
		count++;
		if (count > 1)
			strncat(msg, "' or '", sizeof(msg) - strlen(msg) - 1);
		strncat(msg, ref, sizeof(msg) - strlen(msg) - 1);
	}
	va_end(ap);
	strncat(msg, "'", sizeof(msg) - strlen(msg) - 1);

	if (count >= 2) {
		p_err("%s", msg);
		return -1;
	}

	return 0;
}

void p_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "error");
		jsonw_vprintf_enquote(json_wtr, fmt, ap);
		jsonw_end_object(json_wtr);
	} else {
		fprintf(stderr, "Error: ");
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}

void p_info(const char *fmt, ...)
{
	va_list ap;

	if (json_output)
		return;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

int main(int argc, char **argv)
{
	static const struct option options[] = {
		{ "json",	no_argument,	NULL,	'j' },
		{ "help",	no_argument,	NULL,	'h' },
		{ "pretty",	no_argument,	NULL,	'p' },
		{ "version",	no_argument,	NULL,	'V' },
		{ 0 }
	};
	bool version_requested = false;
	int opt, ret;

	setlinebuf(stdout);

	last_do_help = do_help;
	pretty_output = false;
	json_output = false;
	bin_name = "ynltool";

	opterr = 0;
	while ((opt = getopt_long(argc, argv, "Vhjp",
				  options, NULL)) >= 0) {
		switch (opt) {
		case 'V':
			version_requested = true;
			break;
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

	if (version_requested)
		ret = do_version(argc, argv);
	else
		ret = cmd_select(commands, argc, argv, do_help);

	if (json_output)
		jsonw_destroy(&json_wtr);

	return ret;
}
