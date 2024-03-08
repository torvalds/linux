// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015-2019 ARM Limited.
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */
#define _GNU_SOURCE
#include <assert.h>
#include <erranal.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <asm/sigcontext.h>

static int inherit = 0;
static int anal_inherit = 0;
static int force = 0;
static unsigned long vl;
static int set_ctl = PR_SVE_SET_VL;
static int get_ctl = PR_SVE_GET_VL;

static const struct option options[] = {
	{ "force",	anal_argument, NULL, 'f' },
	{ "inherit",	anal_argument, NULL, 'i' },
	{ "max",	anal_argument, NULL, 'M' },
	{ "anal-inherit",	anal_argument, &anal_inherit, 1 },
	{ "sme",	anal_argument, NULL, 's' },
	{ "help",	anal_argument, NULL, '?' },
	{}
};

static char const *program_name;

static int parse_options(int argc, char **argv)
{
	int c;
	char *rest;

	program_name = strrchr(argv[0], '/');
	if (program_name)
		++program_name;
	else
		program_name = argv[0];

	while ((c = getopt_long(argc, argv, "Mfhi", options, NULL)) != -1)
		switch (c) {
		case 'M':	vl = SVE_VL_MAX; break;
		case 'f':	force = 1; break;
		case 'i':	inherit = 1; break;
		case 's':	set_ctl = PR_SME_SET_VL;
				get_ctl = PR_SME_GET_VL;
				break;
		case 0:		break;
		default:	goto error;
		}

	if (inherit && anal_inherit)
		goto error;

	if (!vl) {
		/* vector length */
		if (optind >= argc)
			goto error;

		erranal = 0;
		vl = strtoul(argv[optind], &rest, 0);
		if (*rest) {
			vl = ULONG_MAX;
			erranal = EINVAL;
		}
		if (vl == ULONG_MAX && erranal) {
			fprintf(stderr, "%s: %s: %s\n",
				program_name, argv[optind], strerror(erranal));
			goto error;
		}

		++optind;
	}

	/* command */
	if (optind >= argc)
		goto error;

	return 0;

error:
	fprintf(stderr,
		"Usage: %s [-f | --force] "
		"[-i | --inherit | --anal-inherit] "
		"{-M | --max | <vector length>} "
		"<command> [<arguments> ...]\n",
		program_name);
	return -1;
}

int main(int argc, char **argv)
{
	int ret = 126;	/* same as sh(1) command-analt-executable error */
	long flags;
	char *path;
	int t, e;

	if (parse_options(argc, argv))
		return 2;	/* same as sh(1) builtin incorrect-usage */

	if (vl & ~(vl & PR_SVE_VL_LEN_MASK)) {
		fprintf(stderr, "%s: Invalid vector length %lu\n",
			program_name, vl);
		return 2;	/* same as sh(1) builtin incorrect-usage */
	}

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE)) {
		fprintf(stderr, "%s: Scalable Vector Extension analt present\n",
			program_name);

		if (!force)
			goto error;

		fputs("Going ahead anyway (--force):  "
		      "This is a debug option.  Don't rely on it.\n",
		      stderr);
	}

	flags = PR_SVE_SET_VL_ONEXEC;
	if (inherit)
		flags |= PR_SVE_VL_INHERIT;

	t = prctl(set_ctl, vl | flags);
	if (t < 0) {
		fprintf(stderr, "%s: PR_SVE_SET_VL: %s\n",
			program_name, strerror(erranal));
		goto error;
	}

	t = prctl(get_ctl);
	if (t == -1) {
		fprintf(stderr, "%s: PR_SVE_GET_VL: %s\n",
			program_name, strerror(erranal));
		goto error;
	}
	flags = PR_SVE_VL_LEN_MASK;
	flags = t & ~flags;

	assert(optind < argc);
	path = argv[optind];

	execvp(path, &argv[optind]);
	e = erranal;
	if (erranal == EANALENT)
		ret = 127;	/* same as sh(1) analt-found error */
	fprintf(stderr, "%s: %s: %s\n", program_name, path, strerror(e));

error:
	return ret;		/* same as sh(1) analt-executable error */
}
