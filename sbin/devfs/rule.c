/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Dima Dorfman.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Rule subsystem manipulation.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ioctl.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static void rulespec_infp(FILE *fp, unsigned long request, devfs_rsnum rsnum);
static void rulespec_instr(struct devfs_rule *dr, const char *str,
    devfs_rsnum rsnum);
static void rulespec_intok(struct devfs_rule *dr, int ac, char **av,
    devfs_rsnum rsnum);
static void rulespec_outfp(FILE *fp, struct devfs_rule *dr);

static command_t rule_add, rule_apply, rule_applyset;
static command_t rule_del, rule_delset, rule_show, rule_showsets;

static ctbl_t ctbl_rule = {
	{ "add",		rule_add },
	{ "apply",		rule_apply },
	{ "applyset",		rule_applyset },
	{ "del",		rule_del },
	{ "delset",		rule_delset },
	{ "show",		rule_show },
	{ "showsets",		rule_showsets },
	{ NULL,			NULL }
};

static struct intstr ist_type[] = {
	{ "disk",		D_DISK },
	{ "mem",		D_MEM },
	{ "tape",		D_TAPE },
	{ "tty",		D_TTY },
	{ NULL,			-1 }
};

static devfs_rsnum in_rsnum;

int
rule_main(int ac, char **av)
{
	struct cmd *c;
	int ch;

	setprogname("devfs rule");
	optreset = optind = 1;
	while ((ch = getopt(ac, av, "s:")) != -1)
		switch (ch) {
		case 's':
			in_rsnum = eatonum(optarg);
			break;
		default:
			usage();
		}
	ac -= optind;
	av += optind;
	if (ac < 1)
		usage();

	for (c = ctbl_rule; c->name != NULL; ++c)
		if (strcmp(c->name, av[0]) == 0)
			exit((*c->handler)(ac, av));
	errx(1, "unknown command: %s", av[0]);
}

static int
rule_add(int ac, char **av)
{
	struct devfs_rule dr;
	int rv;

	if (ac < 2)
		usage();
	if (strcmp(av[1], "-") == 0)
		rulespec_infp(stdin, DEVFSIO_RADD, in_rsnum);
	else {
		rulespec_intok(&dr, ac - 1, av + 1, in_rsnum);
		rv = ioctl(mpfd, DEVFSIO_RADD, &dr);
		if (rv == -1)
			err(1, "ioctl DEVFSIO_RADD");
	}
	return (0);
}

static int
rule_apply(int ac __unused, char **av __unused)
{
	struct devfs_rule dr;
	devfs_rnum rnum;
	devfs_rid rid;
	int rv;

	if (ac < 2)
		usage();
	if (!atonum(av[1], &rnum)) {
		if (strcmp(av[1], "-") == 0)
			rulespec_infp(stdin, DEVFSIO_RAPPLY, in_rsnum);
		else {
			rulespec_intok(&dr, ac - 1, av + 1, in_rsnum);
			rv = ioctl(mpfd, DEVFSIO_RAPPLY, &dr);
			if (rv == -1)
				err(1, "ioctl DEVFSIO_RAPPLY");
		}
	} else {
		rid = mkrid(in_rsnum, rnum);
		rv = ioctl(mpfd, DEVFSIO_RAPPLYID, &rid);
		if (rv == -1)
			err(1, "ioctl DEVFSIO_RAPPLYID");
	}
	return (0);
}

static int
rule_applyset(int ac, char **av __unused)
{
	int rv;

	if (ac != 1)
		usage();
	rv = ioctl(mpfd, DEVFSIO_SAPPLY, &in_rsnum);
	if (rv == -1)
		err(1, "ioctl DEVFSIO_SAPPLY");
	return (0);
}

static int
rule_del(int ac __unused, char **av)
{
	devfs_rid rid;
	int rv;

	if (av[1] == NULL)
		usage();
	rid = mkrid(in_rsnum, eatoi(av[1]));
	rv = ioctl(mpfd, DEVFSIO_RDEL, &rid);
	if (rv == -1)
		err(1, "ioctl DEVFSIO_RDEL");
	return (0);
}

static int
rule_delset(int ac, char **av __unused)
{
	struct devfs_rule dr;
	int rv;

	if (ac != 1)
		usage();
	memset(&dr, '\0', sizeof(dr));
	dr.dr_magic = DEVFS_MAGIC;
	dr.dr_id = mkrid(in_rsnum, 0);
	while (ioctl(mpfd, DEVFSIO_RGETNEXT, &dr) != -1) {
		rv = ioctl(mpfd, DEVFSIO_RDEL, &dr.dr_id);
		if (rv == -1)
			err(1, "ioctl DEVFSIO_RDEL");
	}
	if (errno != ENOENT)
		err(1, "ioctl DEVFSIO_RGETNEXT");
	return (0);
}

static int
rule_show(int ac __unused, char **av)
{
	struct devfs_rule dr;
	devfs_rnum rnum;
	int rv;

	memset(&dr, '\0', sizeof(dr));
	dr.dr_magic = DEVFS_MAGIC;
	if (av[1] != NULL) {
		rnum = eatoi(av[1]);
		dr.dr_id = mkrid(in_rsnum, rnum - 1);
		rv = ioctl(mpfd, DEVFSIO_RGETNEXT, &dr);
		if (rv == -1)
			err(1, "ioctl DEVFSIO_RGETNEXT");
		if (rid2rn(dr.dr_id) == rnum)
			rulespec_outfp(stdout, &dr);
	} else {
		dr.dr_id = mkrid(in_rsnum, 0);
		while (ioctl(mpfd, DEVFSIO_RGETNEXT, &dr) != -1)
			rulespec_outfp(stdout, &dr);
		if (errno != ENOENT)
			err(1, "ioctl DEVFSIO_RGETNEXT");
	}
	return (0);
}

static int
rule_showsets(int ac, char **av __unused)
{
	devfs_rsnum rsnum;

	if (ac != 1)
		usage();
	rsnum = 0;
	while (ioctl(mpfd, DEVFSIO_SGETNEXT, &rsnum) != -1)
		printf("%d\n", rsnum);
	if (errno != ENOENT)
		err(1, "ioctl DEVFSIO_SGETNEXT");
	return (0);
}

int
ruleset_main(int ac, char **av)
{
	devfs_rsnum rsnum;
	int rv;

	setprogname("devfs ruleset");
	if (ac < 2)
		usage();
	rsnum = eatonum(av[1]);
	rv = ioctl(mpfd, DEVFSIO_SUSE, &rsnum);
	if (rv == -1)
		err(1, "ioctl DEVFSIO_SUSE");
	return (0);
}


/*
 * Input rules from a file (probably the standard input).  This
 * differs from the other rulespec_in*() routines in that it also
 * calls ioctl() for the rules, since it is impractical (and not very
 * useful) to return a list (or array) of rules, just so the caller
 * can call ioctl() for each of them.
 */
static void
rulespec_infp(FILE *fp, unsigned long request, devfs_rsnum rsnum)
{
	struct devfs_rule dr;
	char *line;
	int rv;

	assert(fp == stdin);	/* XXX: De-hardcode "stdin" from error msg. */
	while (efgetln(fp, &line)) {
		rulespec_instr(&dr, line, rsnum);
		rv = ioctl(mpfd, request, &dr);
		if (rv == -1)
			err(1, "ioctl");
		free(line);	/* efgetln() always malloc()s. */
	}
	if (ferror(stdin))
		err(1, "stdin");
}

/*
 * Construct a /struct devfs_rule/ from a string.
 */
static void
rulespec_instr(struct devfs_rule *dr, const char *str, devfs_rsnum rsnum)
{
	char **av;
	int ac;

	tokenize(str, &ac, &av);
	if (ac == 0)
		errx(1, "unexpected end of rulespec");
	rulespec_intok(dr, ac, av, rsnum);
	free(av[0]);
	free(av);
}

/*
 * Construct a /struct devfs_rule/ from ac and av.
 */
static void
rulespec_intok(struct devfs_rule *dr, int ac __unused, char **av,
    devfs_rsnum rsnum)
{
	struct intstr *is;
	struct passwd *pw;
	struct group *gr;
	devfs_rnum rnum;
	void *set;

	memset(dr, '\0', sizeof(*dr));

	/*
	 * We don't maintain ac hereinafter.
	 */
	if (av[0] == NULL)
		errx(1, "unexpected end of rulespec");

	/* If the first argument is an integer, treat it as a rule number. */
	if (!atonum(av[0], &rnum))
		rnum = 0;		/* auto-number */
	else
		++av;

	/*
	 * These aren't table-driven since that would result in more
	 * tiny functions than I care to deal with.
	 */
	for (;;) {
		if (av[0] == NULL)
			break;
		else if (strcmp(av[0], "type") == 0) {
			if (av[1] == NULL)
				errx(1, "expecting argument for type");
			for (is = ist_type; is->s != NULL; ++is)
				if (strcmp(av[1], is->s) == 0) {
					dr->dr_dswflags |= is->i;
					break;
				}
			if (is->s == NULL)
				errx(1, "unknown type: %s", av[1]);
			dr->dr_icond |= DRC_DSWFLAGS;
			av += 2;
		} else if (strcmp(av[0], "path") == 0) {
			if (av[1] == NULL)
				errx(1, "expecting argument for path");
			if (strlcpy(dr->dr_pathptrn, av[1], DEVFS_MAXPTRNLEN)
			    >= DEVFS_MAXPTRNLEN)
				warnx("pattern specified too long; truncated");
			dr->dr_icond |= DRC_PATHPTRN;
			av += 2;
		} else
			break;
	}
	while (av[0] != NULL) {
		if (strcmp(av[0], "hide") == 0) {
			dr->dr_iacts |= DRA_BACTS;
			dr->dr_bacts |= DRB_HIDE;
			++av;
		} else if (strcmp(av[0], "unhide") == 0) {
			dr->dr_iacts |= DRA_BACTS;
			dr->dr_bacts |= DRB_UNHIDE;
			++av;
		} else if (strcmp(av[0], "user") == 0) {
			if (av[1] == NULL)
				errx(1, "expecting argument for user");
			dr->dr_iacts |= DRA_UID;
			pw = getpwnam(av[1]);
			if (pw != NULL)
				dr->dr_uid = pw->pw_uid;
			else
				dr->dr_uid = eatoi(av[1]); /* XXX overflow */
			av += 2;
		} else if (strcmp(av[0], "group") == 0) {
			if (av[1] == NULL)
				errx(1, "expecting argument for group");
			dr->dr_iacts |= DRA_GID;
			gr = getgrnam(av[1]);
			if (gr != NULL)
				dr->dr_gid = gr->gr_gid;
			else
				dr->dr_gid = eatoi(av[1]); /* XXX overflow */
			av += 2;
		} else if (strcmp(av[0], "mode") == 0) {
			if (av[1] == NULL)
				errx(1, "expecting argument for mode");
			dr->dr_iacts |= DRA_MODE;
			set = setmode(av[1]);
			if (set == NULL)
				errx(1, "invalid mode: %s", av[1]);
			dr->dr_mode = getmode(set, 0);
			av += 2;
		} else if (strcmp(av[0], "include") == 0) {
			if (av[1] == NULL)
				errx(1, "expecting argument for include");
			dr->dr_iacts |= DRA_INCSET;
			dr->dr_incset = eatonum(av[1]);
			av += 2;
		} else
			errx(1, "unknown argument: %s", av[0]);
	}

	dr->dr_id = mkrid(rsnum, rnum);
	dr->dr_magic = DEVFS_MAGIC;
}

/*
 * Write a human-readable (and machine-parsable, by rulespec_in*())
 * representation of dr to bufp.  *bufp should be free(3)'d when the
 * caller is finished with it.
 */
static void
rulespec_outfp(FILE *fp, struct devfs_rule *dr)
{
	struct intstr *is;
	struct passwd *pw;
	struct group *gr;

	fprintf(fp, "%d", rid2rn(dr->dr_id));

	if (dr->dr_icond & DRC_DSWFLAGS)
		for (is = ist_type; is->s != NULL; ++is)
			if (dr->dr_dswflags & is->i)
				fprintf(fp, " type %s", is->s);
	if (dr->dr_icond & DRC_PATHPTRN)
		fprintf(fp, " path %s", dr->dr_pathptrn);

	if (dr->dr_iacts & DRA_BACTS) {
		if (dr->dr_bacts & DRB_HIDE)
			fprintf(fp, " hide");
		if (dr->dr_bacts & DRB_UNHIDE)
			fprintf(fp, " unhide");
	}
	if (dr->dr_iacts & DRA_UID) {
		pw = getpwuid(dr->dr_uid);
		if (pw == NULL)
			fprintf(fp, " user %d", dr->dr_uid);
		else
			fprintf(fp, " user %s", pw->pw_name);
	}
	if (dr->dr_iacts & DRA_GID) {
		gr = getgrgid(dr->dr_gid);
		if (gr == NULL)
			fprintf(fp, " group %d", dr->dr_gid);
		else
			fprintf(fp, " group %s", gr->gr_name);
	}
	if (dr->dr_iacts & DRA_MODE)
		fprintf(fp, " mode %o", dr->dr_mode);
	if (dr->dr_iacts & DRA_INCSET)
		fprintf(fp, " include %d", dr->dr_incset);

	fprintf(fp, "\n");
}
