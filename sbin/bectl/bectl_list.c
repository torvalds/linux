/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <be.h>

#include "bectl.h"

struct printc {
	int	active_colsz_def;
	int	be_colsz;
	int	current_indent;
	int	mount_colsz;
	int	space_colsz;
	bool	script_fmt;
	bool	show_all_datasets;
	bool	show_snaps;
	bool	show_space;
};

static const char *get_origin_props(nvlist_t *dsprops, nvlist_t **originprops);
static void print_padding(const char *fval, int colsz, struct printc *pc);
static int print_snapshots(const char *dsname, struct printc *pc);
static void print_info(const char *name, nvlist_t *dsprops, struct printc *pc);
static void print_headers(nvlist_t *props, struct printc *pc);
static unsigned long long dataset_space(const char *oname);

#define	HEADER_BE	"BE"
#define	HEADER_BEPLUS	"BE/Dataset/Snapshot"
#define	HEADER_ACTIVE	"Active"
#define	HEADER_MOUNT	"Mountpoint"
#define	HEADER_SPACE	"Space"
#define	HEADER_CREATED	"Created"

/* Spaces */
#define	INDENT_INCREMENT	2

/*
 * Given a set of dataset properties (for a BE dataset), populate originprops
 * with the origin's properties.
 */
static const char *
get_origin_props(nvlist_t *dsprops, nvlist_t **originprops)
{
	char *propstr;

	if (nvlist_lookup_string(dsprops, "origin", &propstr) == 0) {
		if (be_prop_list_alloc(originprops) != 0) {
			fprintf(stderr,
			    "bectl list: failed to allocate origin prop nvlist\n");
			return (NULL);
		}
		if (be_get_dataset_props(be, propstr, *originprops) != 0) {
			/* XXX TODO: Real errors */
			fprintf(stderr,
			    "bectl list: failed to fetch origin properties\n");
			return (NULL);
		}

		return (propstr);
	}
	return (NULL);
}

static void
print_padding(const char *fval, int colsz, struct printc *pc)
{

	/* -H flag handling; all delimiters/padding are a single tab */
	if (pc->script_fmt) {
		printf("\t");
		return;
	}

	if (fval != NULL)
		colsz -= strlen(fval);
	printf("%*s ", colsz, "");
}

static unsigned long long
dataset_space(const char *oname)
{
	unsigned long long space;
	char *dsname, *propstr, *sep;
	nvlist_t *dsprops;

	space = 0;
	dsname = strdup(oname);
	if (dsname == NULL)
		return (0);

	/* Truncate snapshot to dataset name, as needed */
	if ((sep = strchr(dsname, '@')) != NULL)
		*sep = '\0';

	if (be_prop_list_alloc(&dsprops) != 0) {
		free(dsname);
		return (0);
	}

	if (be_get_dataset_props(be, dsname, dsprops) != 0) {
		nvlist_free(dsprops);
		free(dsname);
		return (0);
	}

	if (nvlist_lookup_string(dsprops, "used", &propstr) == 0)
		space = strtoull(propstr, NULL, 10);

	nvlist_free(dsprops);
	free(dsname);
	return (space);
}

static int
print_snapshots(const char *dsname, struct printc *pc)
{
	nvpair_t *cur;
	nvlist_t *props, *sprops;

	if (be_prop_list_alloc(&props) != 0) {
		fprintf(stderr, "bectl list: failed to allocate snapshot nvlist\n");
		return (1);
	}
	if (be_get_dataset_snapshots(be, dsname, props) != 0) {
		fprintf(stderr, "bectl list: failed to fetch boot ds snapshots\n");
		return (1);
	}
	for (cur = nvlist_next_nvpair(props, NULL); cur != NULL;
	    cur = nvlist_next_nvpair(props, cur)) {
		nvpair_value_nvlist(cur, &sprops);
		print_info(nvpair_name(cur), sprops, pc);
	}
	return (0);
}

static void
print_info(const char *name, nvlist_t *dsprops, struct printc *pc)
{
#define	BUFSZ	64
	char buf[BUFSZ];
	unsigned long long ctimenum, space;
	nvlist_t *originprops;
	const char *oname;
	char *dsname, *propstr;
	int active_colsz;
	boolean_t active_now, active_reboot;

	dsname = NULL;
	originprops = NULL;
	printf("%*s%s", pc->current_indent, "", name);
	nvlist_lookup_string(dsprops, "dataset", &dsname);

	/* Recurse at the base level if we're breaking info down */
	if (pc->current_indent == 0 && (pc->show_all_datasets ||
	    pc->show_snaps)) {
		printf("\n");
		if (dsname == NULL)
			/* XXX TODO: Error? */
			return;
		/*
		 * Whether we're dealing with -a or -s, we'll always print the
		 * dataset name/information followed by its origin. For -s, we
		 * additionally iterate through all snapshots of this boot
		 * environment and also print their information.
		 */
		pc->current_indent += INDENT_INCREMENT;
		print_info(dsname, dsprops, pc);
		pc->current_indent += INDENT_INCREMENT;
		if ((oname = get_origin_props(dsprops, &originprops)) != NULL) {
			print_info(oname, originprops, pc);
			nvlist_free(originprops);
		}

		/* Back up a level; snapshots at the same level as dataset */
		pc->current_indent -= INDENT_INCREMENT;
		if (pc->show_snaps)
			print_snapshots(dsname, pc);
		pc->current_indent = 0;
		return;
	} else
		print_padding(name, pc->be_colsz - pc->current_indent, pc);

	active_colsz = pc->active_colsz_def;
	if (nvlist_lookup_boolean_value(dsprops, "active",
	    &active_now) == 0 && active_now) {
		printf("N");
		active_colsz--;
	}
	if (nvlist_lookup_boolean_value(dsprops, "nextboot",
	    &active_reboot) == 0 && active_reboot) {
		printf("R");
		active_colsz--;
	}
	if (active_colsz == pc->active_colsz_def) {
		printf("-");
		active_colsz--;
	}
	print_padding(NULL, active_colsz, pc);
	if (nvlist_lookup_string(dsprops, "mounted", &propstr) == 0) {
		printf("%s", propstr);
		print_padding(propstr, pc->mount_colsz, pc);
	} else {
		printf("%s", "-");
		print_padding("-", pc->mount_colsz, pc);
	}

	oname = get_origin_props(dsprops, &originprops);
	if (nvlist_lookup_string(dsprops, "used", &propstr) == 0) {
		/*
		 * The space used column is some composition of:
		 * - The "used" property of the dataset
		 * - The "used" property of the origin snapshot (not -a or -s)
		 * - The "used" property of the origin dataset (-D flag only)
		 *
		 * The -D flag is ignored if -a or -s are specified.
		 */
		space = strtoull(propstr, NULL, 10);

		if (!pc->show_all_datasets && !pc->show_snaps &&
		    originprops != NULL &&
		    nvlist_lookup_string(originprops, "used", &propstr) == 0)
			space += strtoull(propstr, NULL, 10);

		if (pc->show_space && oname != NULL)
			space += dataset_space(oname);

		/* Alas, there's more to it,. */
		be_nicenum(space, buf, 6);
		printf("%s", buf);
		print_padding(buf, pc->space_colsz, pc);
	} else {
		printf("-");
		print_padding("-", pc->space_colsz, pc);
	}

	if (nvlist_lookup_string(dsprops, "creation", &propstr) == 0) {
		ctimenum = strtoull(propstr, NULL, 10);
		strftime(buf, BUFSZ, "%Y-%m-%d %H:%M",
		    localtime((time_t *)&ctimenum));
		printf("%s", buf);
	}

	printf("\n");
	if (originprops != NULL)
		be_prop_list_free(originprops);
#undef BUFSZ
}

static void
print_headers(nvlist_t *props, struct printc *pc)
{
	const char *chosen_be_header;
	nvpair_t *cur;
	nvlist_t *dsprops;
	char *propstr;
	size_t be_maxcol;

	if (pc->show_all_datasets || pc->show_snaps)
		chosen_be_header = HEADER_BEPLUS;
	else
		chosen_be_header = HEADER_BE;
	be_maxcol = strlen(chosen_be_header);
	for (cur = nvlist_next_nvpair(props, NULL); cur != NULL;
	    cur = nvlist_next_nvpair(props, cur)) {
		be_maxcol = MAX(be_maxcol, strlen(nvpair_name(cur)));
		if (!pc->show_all_datasets && !pc->show_snaps)
			continue;
		nvpair_value_nvlist(cur, &dsprops);
		if (nvlist_lookup_string(dsprops, "dataset", &propstr) != 0)
			continue;
		be_maxcol = MAX(be_maxcol, strlen(propstr) + INDENT_INCREMENT);
		if (nvlist_lookup_string(dsprops, "origin", &propstr) != 0)
			continue;
		be_maxcol = MAX(be_maxcol,
		    strlen(propstr) + INDENT_INCREMENT * 2);
	}

	pc->be_colsz = be_maxcol;
	pc->active_colsz_def = strlen(HEADER_ACTIVE);
	pc->mount_colsz = strlen(HEADER_MOUNT);
	pc->space_colsz = strlen(HEADER_SPACE);
	printf("%*s %s %s %s %s\n", -pc->be_colsz, chosen_be_header,
	    HEADER_ACTIVE, HEADER_MOUNT, HEADER_SPACE, HEADER_CREATED);

	/*
	 * All other invocations in which we aren't using the default header
	 * will produce quite a bit of input.  Throw an extra blank line after
	 * the header to make it look nicer.
	 */
	if (strcmp(chosen_be_header, HEADER_BE) != 0)
		printf("\n");
}

int
bectl_cmd_list(int argc, char *argv[])
{
	struct printc pc;
	nvpair_t *cur;
	nvlist_t *dsprops, *props;
	int opt, printed;
	boolean_t active_now, active_reboot;

	props = NULL;
	printed = 0;
	bzero(&pc, sizeof(pc));
	while ((opt = getopt(argc, argv, "aDHs")) != -1) {
		switch (opt) {
		case 'a':
			pc.show_all_datasets = true;
			break;
		case 'D':
			pc.show_space = true;
			break;
		case 'H':
			pc.script_fmt = true;
			break;
		case 's':
			pc.show_snaps = true;
			break;
		default:
			fprintf(stderr, "bectl list: unknown option '-%c'\n",
			    optopt);
			return (usage(false));
		}
	}

	argc -= optind;

	if (argc != 0) {
		fprintf(stderr, "bectl list: extra argument provided\n");
		return (usage(false));
	}

	if (be_prop_list_alloc(&props) != 0) {
		fprintf(stderr, "bectl list: failed to allocate prop nvlist\n");
		return (1);
	}
	if (be_get_bootenv_props(be, props) != 0) {
		/* XXX TODO: Real errors */
		fprintf(stderr, "bectl list: failed to fetch boot environments\n");
		return (1);
	}

	/* Force -D off if either -a or -s are specified */
	if (pc.show_all_datasets || pc.show_snaps)
		pc.show_space = false;
	if (!pc.script_fmt)
		print_headers(props, &pc);
	/* Do a first pass to print active and next active first */
	for (cur = nvlist_next_nvpair(props, NULL); cur != NULL;
	    cur = nvlist_next_nvpair(props, cur)) {
		nvpair_value_nvlist(cur, &dsprops);
		active_now = active_reboot = false;

		nvlist_lookup_boolean_value(dsprops, "active", &active_now);
		nvlist_lookup_boolean_value(dsprops, "nextboot",
		    &active_reboot);
		if (!active_now && !active_reboot)
			continue;
		if (printed > 0 && (pc.show_all_datasets || pc.show_snaps))
			printf("\n");
		print_info(nvpair_name(cur), dsprops, &pc);
		printed++;
	}

	/* Now pull everything else */
	for (cur = nvlist_next_nvpair(props, NULL); cur != NULL;
	    cur = nvlist_next_nvpair(props, cur)) {
		nvpair_value_nvlist(cur, &dsprops);
		active_now = active_reboot = false;

		nvlist_lookup_boolean_value(dsprops, "active", &active_now);
		nvlist_lookup_boolean_value(dsprops, "nextboot",
		    &active_reboot);
		if (active_now || active_reboot)
			continue;
		if (printed > 0 && (pc.show_all_datasets || pc.show_snaps))
			printf("\n");
		print_info(nvpair_name(cur), dsprops, &pc);
		printed++;
	}
	be_prop_list_free(props);

	return (0);
}

