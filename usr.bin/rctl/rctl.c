/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/rctl.h>
#include <sys/sysctl.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <libutil.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	RCTL_DEFAULT_BUFSIZE	128 * 1024

static int
parse_user(const char *s, id_t *uidp, const char *unexpanded_rule)
{
	char *end;
	struct passwd *pwd;

	pwd = getpwnam(s);
	if (pwd != NULL) {
		*uidp = pwd->pw_uid;
		return (0);
	}

	if (!isnumber(s[0])) {
		warnx("malformed rule '%s': unknown user '%s'",
		    unexpanded_rule, s);
		return (1);
	}

	*uidp = strtod(s, &end);
	if ((size_t)(end - s) != strlen(s)) {
		warnx("malformed rule '%s': trailing characters "
		    "after numerical id", unexpanded_rule);
		return (1);
	}

	return (0);
}

static int
parse_group(const char *s, id_t *gidp, const char *unexpanded_rule)
{
	char *end;
	struct group *grp;

	grp = getgrnam(s);
	if (grp != NULL) {
		*gidp = grp->gr_gid;
		return (0);
	}

	if (!isnumber(s[0])) {
		warnx("malformed rule '%s': unknown group '%s'",
		    unexpanded_rule, s);
		return (1);
	}

	*gidp = strtod(s, &end);
	if ((size_t)(end - s) != strlen(s)) {
		warnx("malformed rule '%s': trailing characters "
		    "after numerical id", unexpanded_rule);
		return (1);
	}

	return (0);
}

/*
 * Replace human-readable number with its expanded form.
 */
static char *
expand_amount(const char *rule, const char *unexpanded_rule)
{
	uint64_t num;
	const char *subject, *subject_id, *resource, *action, *amount, *per;
	char *copy, *expanded, *tofree;
	int ret;

	tofree = copy = strdup(rule);
	if (copy == NULL) {
		warn("strdup");
		return (NULL);
	}

	subject = strsep(&copy, ":");
	subject_id = strsep(&copy, ":");
	resource = strsep(&copy, ":");
	action = strsep(&copy, "=/");
	amount = strsep(&copy, "/");
	per = copy;

	if (amount == NULL || strlen(amount) == 0) {
		/*
		 * The "copy" has already been tinkered with by strsep().
		 */
		free(tofree);
		copy = strdup(rule);
		if (copy == NULL) {
			warn("strdup");
			return (NULL);
		}
		return (copy);
	}

	assert(subject != NULL);
	assert(subject_id != NULL);
	assert(resource != NULL);
	assert(action != NULL);

	if (expand_number(amount, &num)) {
		warnx("malformed rule '%s': invalid numeric value '%s'",
		    unexpanded_rule, amount);
		free(tofree);
		return (NULL);
	}

	if (per == NULL) {
		ret = asprintf(&expanded, "%s:%s:%s:%s=%ju",
		    subject, subject_id, resource, action, (uintmax_t)num);
	} else {
		ret = asprintf(&expanded, "%s:%s:%s:%s=%ju/%s",
		    subject, subject_id, resource, action, (uintmax_t)num, per);
	}

	if (ret <= 0) {
		warn("asprintf");
		free(tofree);
		return (NULL);
	}

	free(tofree);

	return (expanded);
}

static char *
expand_rule(const char *rule, bool resolve_ids)
{
	id_t id;
	const char *subject, *textid, *rest;
	char *copy, *expanded, *resolved, *tofree;
	int error, ret;

	tofree = copy = strdup(rule);
	if (copy == NULL) {
		warn("strdup");
		return (NULL);
	}

	subject = strsep(&copy, ":");
	textid = strsep(&copy, ":");
	if (textid == NULL) {
		warnx("malformed rule '%s': missing subject", rule);
		return (NULL);
	}
	if (copy != NULL)
		rest = copy;
	else
		rest = "";

	if (strcasecmp(subject, "u") == 0)
		subject = "user";
	else if (strcasecmp(subject, "g") == 0)
		subject = "group";
	else if (strcasecmp(subject, "p") == 0)
		subject = "process";
	else if (strcasecmp(subject, "l") == 0 ||
	    strcasecmp(subject, "c") == 0 ||
	    strcasecmp(subject, "class") == 0)
		subject = "loginclass";
	else if (strcasecmp(subject, "j") == 0)
		subject = "jail";

	if (resolve_ids &&
	    strcasecmp(subject, "user") == 0 && strlen(textid) > 0) {
		error = parse_user(textid, &id, rule);
		if (error != 0) {
			free(tofree);
			return (NULL);
		}
		ret = asprintf(&resolved, "%s:%d:%s", subject, (int)id, rest);
	} else if (resolve_ids &&
	    strcasecmp(subject, "group") == 0 && strlen(textid) > 0) {
		error = parse_group(textid, &id, rule);
		if (error != 0) {
			free(tofree);
			return (NULL);
		}
		ret = asprintf(&resolved, "%s:%d:%s", subject, (int)id, rest);
	} else {
		ret = asprintf(&resolved, "%s:%s:%s", subject, textid, rest);
	}

	if (ret <= 0) {
		warn("asprintf");
		free(tofree);
		return (NULL);
	}

	free(tofree);

	expanded = expand_amount(resolved, rule);
	free(resolved);

	return (expanded);
}

static char *
humanize_ids(char *rule)
{
	id_t id;
	struct passwd *pwd;
	struct group *grp;
	const char *subject, *textid, *rest;
	char *end, *humanized;
	int ret;

	subject = strsep(&rule, ":");
	textid = strsep(&rule, ":");
	if (textid == NULL)
		errx(1, "rule passed from the kernel didn't contain subject");
	if (rule != NULL)
		rest = rule;
	else
		rest = "";

	/* Replace numerical user and group ids with names. */
	if (strcasecmp(subject, "user") == 0) {
		id = strtod(textid, &end);
		if ((size_t)(end - textid) != strlen(textid))
			errx(1, "malformed uid '%s'", textid);
		pwd = getpwuid(id);
		if (pwd != NULL)
			textid = pwd->pw_name;
	} else if (strcasecmp(subject, "group") == 0) {
		id = strtod(textid, &end);
		if ((size_t)(end - textid) != strlen(textid))
			errx(1, "malformed gid '%s'", textid);
		grp = getgrgid(id);
		if (grp != NULL)
			textid = grp->gr_name;
	}

	ret = asprintf(&humanized, "%s:%s:%s", subject, textid, rest);
	if (ret <= 0)
		err(1, "asprintf");

	return (humanized);
}

static int
str2int64(const char *str, int64_t *value)
{
	char *end;

	if (str == NULL)
		return (EINVAL);

	*value = strtoul(str, &end, 10);
	if ((size_t)(end - str) != strlen(str))
		return (EINVAL);

	return (0);
}

static char *
humanize_amount(char *rule)
{
	int64_t num;
	const char *subject, *subject_id, *resource, *action, *amount, *per;
	char *copy, *humanized, buf[6], *tofree;
	int ret;

	tofree = copy = strdup(rule);
	if (copy == NULL)
		err(1, "strdup");

	subject = strsep(&copy, ":");
	subject_id = strsep(&copy, ":");
	resource = strsep(&copy, ":");
	action = strsep(&copy, "=/");
	amount = strsep(&copy, "/");
	per = copy;

	if (amount == NULL || strlen(amount) == 0 ||
	    str2int64(amount, &num) != 0) {
		free(tofree);
		return (rule);
	}

	assert(subject != NULL);
	assert(subject_id != NULL);
	assert(resource != NULL);
	assert(action != NULL);

	if (humanize_number(buf, sizeof(buf), num, "", HN_AUTOSCALE,
	    HN_DECIMAL | HN_NOSPACE) == -1)
		err(1, "humanize_number");

	if (per == NULL) {
		ret = asprintf(&humanized, "%s:%s:%s:%s=%s",
		    subject, subject_id, resource, action, buf);
	} else {
		ret = asprintf(&humanized, "%s:%s:%s:%s=%s/%s",
		    subject, subject_id, resource, action, buf, per);
	}

	if (ret <= 0)
		err(1, "asprintf");

	free(tofree);
	return (humanized);
}

/*
 * Print rules, one per line.
 */
static void
print_rules(char *rules, int hflag, int nflag)
{
	char *rule;

	while ((rule = strsep(&rules, ",")) != NULL) {
		if (rule[0] == '\0')
			break; /* XXX */
		if (nflag == 0)
			rule = humanize_ids(rule);
		if (hflag)
			rule = humanize_amount(rule);
		printf("%s\n", rule);
	}
}

static void
enosys(void)
{
	int error, racct_enable;
	size_t racct_enable_len;

	racct_enable_len = sizeof(racct_enable);
	error = sysctlbyname("kern.racct.enable",
	    &racct_enable, &racct_enable_len, NULL, 0);

	if (error != 0) {
		if (errno == ENOENT)
			errx(1, "RACCT/RCTL support not present in kernel; see rctl(8) for details");

		err(1, "sysctlbyname");
	}

	if (racct_enable == 0)
		errx(1, "RACCT/RCTL present, but disabled; enable using kern.racct.enable=1 tunable");
}

static int
add_rule(const char *rule, const char *unexpanded_rule)
{
	int error;

	error = rctl_add_rule(rule, strlen(rule) + 1, NULL, 0);
	if (error != 0) {
		if (errno == ENOSYS)
			enosys();
		warn("failed to add rule '%s'", unexpanded_rule);
	}

	return (error);
}

static int
show_limits(const char *filter, const char *unexpanded_rule,
    int hflag, int nflag)
{
	int error;
	char *outbuf = NULL;
	size_t outbuflen = RCTL_DEFAULT_BUFSIZE / 4;

	for (;;) {
		outbuflen *= 4;
		outbuf = realloc(outbuf, outbuflen);
		if (outbuf == NULL)
			err(1, "realloc");
		error = rctl_get_limits(filter, strlen(filter) + 1,
		    outbuf, outbuflen);
		if (error == 0)
			break;
		if (errno == ERANGE)
			continue;
		if (errno == ENOSYS)
			enosys();
		warn("failed to get limits for '%s'", unexpanded_rule);
		free(outbuf);

		return (error);
	}

	print_rules(outbuf, hflag, nflag);
	free(outbuf);

	return (error);
}

static int
remove_rule(const char *filter, const char *unexpanded_rule)
{
	int error;

	error = rctl_remove_rule(filter, strlen(filter) + 1, NULL, 0);
	if (error != 0) {
		if (errno == ENOSYS)
			enosys();
		warn("failed to remove rule '%s'", unexpanded_rule);
	}

	return (error);
}

static char *
humanize_usage_amount(char *usage)
{
	int64_t num;
	const char *resource, *amount;
	char *copy, *humanized, buf[6], *tofree;
	int ret;

	tofree = copy = strdup(usage);
	if (copy == NULL)
		err(1, "strdup");

	resource = strsep(&copy, "=");
	amount = copy;

	assert(resource != NULL);
	assert(amount != NULL);

	if (str2int64(amount, &num) != 0 || 
	    humanize_number(buf, sizeof(buf), num, "", HN_AUTOSCALE,
	    HN_DECIMAL | HN_NOSPACE) == -1) {
		free(tofree);
		return (usage);
	}

	ret = asprintf(&humanized, "%s=%s", resource, buf);
	if (ret <= 0)
		err(1, "asprintf");

	free(tofree);
	return (humanized);
}

/*
 * Query the kernel about a resource usage and print it out.
 */
static int
show_usage(const char *filter, const char *unexpanded_rule, int hflag)
{
	int error;
	char *copy, *outbuf = NULL, *tmp;
	size_t outbuflen = RCTL_DEFAULT_BUFSIZE / 4;

	for (;;) {
		outbuflen *= 4;
		outbuf = realloc(outbuf, outbuflen);
		if (outbuf == NULL)
			err(1, "realloc");
		error = rctl_get_racct(filter, strlen(filter) + 1,
		    outbuf, outbuflen);
		if (error == 0)
			break;
		if (errno == ERANGE)
			continue;
		if (errno == ENOSYS)
			enosys();
		warn("failed to show resource consumption for '%s'",
		    unexpanded_rule);
		free(outbuf);

		return (error);
	}

	copy = outbuf;
	while ((tmp = strsep(&copy, ",")) != NULL) {
		if (tmp[0] == '\0')
			break; /* XXX */

		if (hflag)
			tmp = humanize_usage_amount(tmp);

		printf("%s\n", tmp);
	}

	free(outbuf);

	return (error);
}

/*
 * Query the kernel about resource limit rules and print them out.
 */
static int
show_rules(const char *filter, const char *unexpanded_rule,
    int hflag, int nflag)
{
	int error;
	char *outbuf = NULL;
	size_t filterlen, outbuflen = RCTL_DEFAULT_BUFSIZE / 4;

	if (filter != NULL)
		filterlen = strlen(filter) + 1;
	else
		filterlen = 0;

	for (;;) {
		outbuflen *= 4;
		outbuf = realloc(outbuf, outbuflen);
		if (outbuf == NULL)
			err(1, "realloc");
		error = rctl_get_rules(filter, filterlen, outbuf, outbuflen);
		if (error == 0)
			break;
		if (errno == ERANGE)
			continue;
		if (errno == ENOSYS)
			enosys();
		warn("failed to show rules for '%s'", unexpanded_rule);
		free(outbuf);

		return (error);
	}

	print_rules(outbuf, hflag, nflag);
	free(outbuf);

	return (error);
}

static void
usage(void)
{

	fprintf(stderr, "usage: rctl [ -h ] [-a rule | -l filter | -r filter "
	    "| -u filter | filter]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int ch, aflag = 0, hflag = 0, nflag = 0, lflag = 0, rflag = 0,
	    uflag = 0;
	char *rule = NULL, *unexpanded_rule;
	int i, cumulated_error, error;

	while ((ch = getopt(argc, argv, "ahlnru")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'h':
			hflag = 1;
			break;
		case 'l':
			lflag = 1;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'u':
			uflag = 1;
			break;

		case '?':
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	
	if (aflag + lflag + rflag + uflag > 1)
		errx(1, "at most one of -a, -l, -r, or -u may be specified");

	if (argc == 0) {
		if (aflag + lflag + rflag + uflag == 0) {
			rule = strdup("::");
			show_rules(rule, rule, hflag, nflag);

			return (0);
		}

		usage();
	}

	cumulated_error = 0;

	for (i = 0; i < argc; i++) {
		unexpanded_rule = argv[i];

		/*
		 * Skip resolving if passed -n _and_ -a.  Ignore -n otherwise,
		 * so we can still do "rctl -n u:root" and see the rules without
		 * resolving the UID.
		 */
		if (aflag != 0 && nflag != 0)
			rule = expand_rule(unexpanded_rule, false);
		else
			rule = expand_rule(unexpanded_rule, true);

		if (rule == NULL) {
			cumulated_error++;
			continue;
		}

		/*
		 * The reason for passing the unexpanded_rule is to make
		 * it easier for the user to search for the problematic
		 * rule in the passed input.
		 */
		if (aflag) {
			error = add_rule(rule, unexpanded_rule);
		} else if (lflag) {
			error = show_limits(rule, unexpanded_rule,
			    hflag, nflag);
		} else if (rflag) {
			error = remove_rule(rule, unexpanded_rule);
		} else if (uflag) {
			error = show_usage(rule, unexpanded_rule, hflag);
		} else  {
			error = show_rules(rule, unexpanded_rule,
			    hflag, nflag);
		}

		if (error != 0)
			cumulated_error++;

		free(rule);
	}

	return (cumulated_error);
}
