/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <ctype.h>
#include <err.h>
#include <grp.h>
#include <libutil.h>
#include <paths.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#include "pw.h"
#include "bitmap.h"

static struct passwd *lookup_pwent(const char *user);
static void	delete_members(struct group *grp, char *list);
static int	print_group(struct group * grp, bool pretty);
static gid_t	gr_gidpolicy(struct userconf * cnf, intmax_t id);

static void
grp_set_passwd(struct group *grp, bool update, int fd, bool precrypted)
{
	int		 b;
	int		 istty;
	struct termios	 t, n;
	char		*p, line[256];

	if (fd == -1)
		return;

	if (fd == '-') {
		grp->gr_passwd = "*";	/* No access */
		return;
	}
	
	if ((istty = isatty(fd))) {
		n = t;
		/* Disable echo */
		n.c_lflag &= ~(ECHO);
		tcsetattr(fd, TCSANOW, &n);
		printf("%sassword for group %s:", update ? "New p" : "P",
		    grp->gr_name);
		fflush(stdout);
	}
	b = read(fd, line, sizeof(line) - 1);
	if (istty) {	/* Restore state */
		tcsetattr(fd, TCSANOW, &t);
		fputc('\n', stdout);
		fflush(stdout);
	}
	if (b < 0)
		err(EX_OSERR, "-h file descriptor");
	line[b] = '\0';
	if ((p = strpbrk(line, " \t\r\n")) != NULL)
		*p = '\0';
	if (!*line)
		errx(EX_DATAERR, "empty password read on file descriptor %d",
		    conf.fd);
	if (precrypted) {
		if (strchr(line, ':') != 0)
			errx(EX_DATAERR, "wrong encrypted passwrd");
		grp->gr_passwd = line;
	} else
		grp->gr_passwd = pw_pwcrypt(line);
}

int
pw_groupnext(struct userconf *cnf, bool quiet)
{
	gid_t next = gr_gidpolicy(cnf, -1);

	if (quiet)
		return (next);
	printf("%ju\n", (uintmax_t)next);

	return (EXIT_SUCCESS);
}

static struct group *
getgroup(char *name, intmax_t id, bool fatal)
{
	struct group *grp;

	if (id < 0 && name == NULL)
		errx(EX_DATAERR, "groupname or id required");
	grp = (name != NULL) ? GETGRNAM(name) : GETGRGID(id);
	if (grp == NULL) {
		if (!fatal)
			return (NULL);
		if (name == NULL)
			errx(EX_DATAERR, "unknown gid `%ju'", id);
		errx(EX_DATAERR, "unknown group `%s'", name);
	}
	return (grp);
}

/*
 * Lookup a passwd entry using a name or UID.
 */
static struct passwd *
lookup_pwent(const char *user)
{
	struct passwd *pwd;

	if ((pwd = GETPWNAM(user)) == NULL &&
	    (!isdigit((unsigned char)*user) ||
	    (pwd = getpwuid((uid_t) atoi(user))) == NULL))
		errx(EX_NOUSER, "user `%s' does not exist", user);

	return (pwd);
}


/*
 * Delete requested members from a group.
 */
static void
delete_members(struct group *grp, char *list)
{
	char *p;
	int k;

	if (grp->gr_mem == NULL)
		return;

	for (p = strtok(list, ", \t"); p != NULL; p = strtok(NULL, ", \t")) {
		for (k = 0; grp->gr_mem[k] != NULL; k++) {
			if (strcmp(grp->gr_mem[k], p) == 0)
				break;
		}
		if (grp->gr_mem[k] == NULL) /* No match */
			continue;

		for (; grp->gr_mem[k] != NULL; k++)
			grp->gr_mem[k] = grp->gr_mem[k+1];
	}
}

static gid_t
gr_gidpolicy(struct userconf * cnf, intmax_t id)
{
	struct group   *grp;
	struct bitmap   bm;
	gid_t           gid = (gid_t) - 1;

	/*
	 * Check the given gid, if any
	 */
	if (id > 0) {
		gid = (gid_t) id;

		if ((grp = GETGRGID(gid)) != NULL && conf.checkduplicate)
			errx(EX_DATAERR, "gid `%ju' has already been allocated",
			    (uintmax_t)grp->gr_gid);
		return (gid);
	}

	/*
	 * We need to allocate the next available gid under one of
	 * two policies a) Grab the first unused gid b) Grab the
	 * highest possible unused gid
	 */
	if (cnf->min_gid >= cnf->max_gid) {	/* Sanity claus^H^H^H^Hheck */
		cnf->min_gid = 1000;
		cnf->max_gid = 32000;
	}
	bm = bm_alloc(cnf->max_gid - cnf->min_gid + 1);

	/*
	 * Now, let's fill the bitmap from the password file
	 */
	SETGRENT();
	while ((grp = GETGRENT()) != NULL)
		if ((gid_t)grp->gr_gid >= (gid_t)cnf->min_gid &&
		    (gid_t)grp->gr_gid <= (gid_t)cnf->max_gid)
			bm_setbit(&bm, grp->gr_gid - cnf->min_gid);
	ENDGRENT();

	/*
	 * Then apply the policy, with fallback to reuse if necessary
	 */
	if (cnf->reuse_gids)
		gid = (gid_t) (bm_firstunset(&bm) + cnf->min_gid);
	else {
		gid = (gid_t) (bm_lastset(&bm) + 1);
		if (!bm_isset(&bm, gid))
			gid += cnf->min_gid;
		else
			gid = (gid_t) (bm_firstunset(&bm) + cnf->min_gid);
	}

	/*
	 * Another sanity check
	 */
	if (gid < cnf->min_gid || gid > cnf->max_gid)
		errx(EX_SOFTWARE, "unable to allocate a new gid - range fully "
		    "used");
	bm_dealloc(&bm);
	return (gid);
}

static int
print_group(struct group * grp, bool pretty)
{
	char *buf = NULL;
	int i;

	if (pretty) {
		printf("Group Name: %-15s   #%lu\n"
		       "   Members: ",
		       grp->gr_name, (long) grp->gr_gid);
		if (grp->gr_mem != NULL) {
			for (i = 0; grp->gr_mem[i]; i++)
				printf("%s%s", i ? "," : "", grp->gr_mem[i]);
		}
		fputs("\n\n", stdout);
		return (EXIT_SUCCESS);
	}

	buf = gr_make(grp);
	printf("%s\n", buf);
	free(buf);
	return (EXIT_SUCCESS);
}

int
pw_group_next(int argc, char **argv, char *arg1 __unused)
{
	struct userconf *cnf;
	const char *cfg = NULL;
	int ch;
	bool quiet = false;

	while ((ch = getopt(argc, argv, "C:q")) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);
	cnf = get_userconfig(cfg);
	return (pw_groupnext(cnf, quiet));
}

int
pw_group_show(int argc, char **argv, char *arg1)
{
	struct group *grp = NULL;
	char *name = NULL;
	intmax_t id = -1;
	int ch;
	bool all, force, quiet, pretty;

	all = force = quiet = pretty = false;

	struct group fakegroup = {
		"nogroup",
		"*",
		-1,
		NULL
	};

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, GID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, "C:qn:g:FPa")) != -1) {
		switch (ch) {
		case 'C':
			/* ignore compatibility */
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'g':
			id = pw_checkid(optarg, GID_MAX);
			break;
		case 'F':
			force = true;
			break;
		case 'P':
			pretty = true;
			break;
		case 'a':
			all = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);

	if (all) {
		SETGRENT();
		while ((grp = GETGRENT()) != NULL)
			print_group(grp, pretty);
		ENDGRENT();
		return (EXIT_SUCCESS);
	}

	grp = getgroup(name, id, !force);
	if (grp == NULL)
		grp = &fakegroup;

	return (print_group(grp, pretty));
}

int
pw_group_del(int argc, char **argv, char *arg1)
{
	struct userconf *cnf = NULL;
	struct group *grp = NULL;
	char *name;
	const char *cfg = NULL;
	intmax_t id = -1;
	int ch, rc;
	bool quiet = false;
	bool nis = false;

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, GID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, "C:qn:g:Y")) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'g':
			id = pw_checkid(optarg, GID_MAX);
			break;
		case 'Y':
			nis = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);
	grp = getgroup(name, id, true);
	cnf = get_userconfig(cfg);
	rc = delgrent(grp);
	if (rc == -1)
		err(EX_IOERR, "group '%s' not available (NIS?)", name);
	else if (rc != 0)
		err(EX_IOERR, "group update");
	pw_log(cnf, M_DELETE, W_GROUP, "%s(%ju) removed", name,
	    (uintmax_t)id);

	if (nis && nis_update() == 0)
		pw_log(cnf, M_DELETE, W_GROUP, "NIS maps updated");

	return (EXIT_SUCCESS);
}

static bool
grp_has_member(struct group *grp, const char *name)
{
	int j;

	for (j = 0; grp->gr_mem != NULL && grp->gr_mem[j] != NULL; j++)
		if (strcmp(grp->gr_mem[j], name) == 0)
			return (true);
	return (false);
}

static void
grp_add_members(struct group **grp, char *members)
{
	struct passwd *pwd;
	char *p;
	char tok[] = ", \t";

	if (members == NULL)
		return;
	for (p = strtok(members, tok); p != NULL; p = strtok(NULL, tok)) {
		pwd = lookup_pwent(p);
		if (grp_has_member(*grp, pwd->pw_name))
			continue;
		*grp = gr_add(*grp, pwd->pw_name);
	}
}

int
groupadd(struct userconf *cnf, char *name, gid_t id, char *members, int fd,
    bool dryrun, bool pretty, bool precrypted)
{
	struct group *grp;
	int rc;

	struct group fakegroup = {
		"nogroup",
		"*",
		-1,
		NULL
	};

	grp = &fakegroup;
	grp->gr_name = pw_checkname(name, 0);
	grp->gr_passwd = "*";
	grp->gr_gid = gr_gidpolicy(cnf, id);
	grp->gr_mem = NULL;

	/*
	 * This allows us to set a group password Group passwords is an
	 * antique idea, rarely used and insecure (no secure database) Should
	 * be discouraged, but it is apparently still supported by some
	 * software.
	 */
	grp_set_passwd(grp, false, fd, precrypted);
	grp_add_members(&grp, members);
	if (dryrun)
		return (print_group(grp, pretty));

	if ((rc = addgrent(grp)) != 0) {
		if (rc == -1)
			errx(EX_IOERR, "group '%s' already exists",
			    grp->gr_name);
		else
			err(EX_IOERR, "group update");
	}

	pw_log(cnf, M_ADD, W_GROUP, "%s(%ju)", grp->gr_name,
	    (uintmax_t)grp->gr_gid);

	return (EXIT_SUCCESS);
}

int
pw_group_add(int argc, char **argv, char *arg1)
{
	struct userconf *cnf = NULL;
	char *name = NULL;
	char *members = NULL;
	const char *cfg = NULL;
	intmax_t id = -1;
	int ch, rc, fd = -1;
	bool quiet, precrypted, dryrun, pretty, nis;

	quiet = precrypted = dryrun = pretty = nis = false;

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, GID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, "C:qn:g:h:H:M:oNPY")) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'g':
			id = pw_checkid(optarg, GID_MAX);
			break;
		case 'H':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			precrypted = true;
			if (fd == '-')
				errx(EX_USAGE, "-H expects a file descriptor");
			break;
		case 'h':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			break;
		case 'M':
			members = optarg;
			break;
		case 'o':
			conf.checkduplicate = false;
			break;
		case 'N':
			dryrun = true;
			break;
		case 'P':
			pretty = true;
			break;
		case 'Y':
			nis = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);
	if (name == NULL)
		errx(EX_DATAERR, "group name required");
	if (GETGRNAM(name) != NULL)
		errx(EX_DATAERR, "group name `%s' already exists", name);
	cnf = get_userconfig(cfg);
	rc = groupadd(cnf, name, gr_gidpolicy(cnf, id), members, fd, dryrun,
	    pretty, precrypted);
	if (nis && rc == EXIT_SUCCESS && nis_update() == 0)
		pw_log(cnf, M_ADD, W_GROUP, "NIS maps updated");

	return (rc);
}

int
pw_group_mod(int argc, char **argv, char *arg1)
{
	struct userconf *cnf;
	struct group *grp = NULL;
	const char *cfg = NULL;
	char *oldmembers = NULL;
	char *members = NULL;
	char *newmembers = NULL;
	char *newname = NULL;
	char *name = NULL;
	intmax_t id = -1;
	int ch, rc, fd = -1;
	bool quiet, pretty, dryrun, nis, precrypted;

	quiet = pretty = dryrun = nis = precrypted = false;

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, GID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, "C:qn:d:g:l:h:H:M:m:NPY")) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'g':
			id = pw_checkid(optarg, GID_MAX);
			break;
		case 'd':
			oldmembers = optarg;
			break;
		case 'l':
			newname = optarg;
			break;
		case 'H':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			precrypted = true;
			if (fd == '-')
				errx(EX_USAGE, "-H expects a file descriptor");
			break;
		case 'h':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			break;
		case 'M':
			members = optarg;
			break;
		case 'm':
			newmembers = optarg;
			break;
		case 'N':
			dryrun = true;
			break;
		case 'P':
			pretty = true;
			break;
		case 'Y':
			nis = true;
			break;
		}
	}
	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);
	cnf = get_userconfig(cfg);
	grp = getgroup(name, id, true);
	if (name == NULL)
		name = grp->gr_name;
	if (id > 0)
		grp->gr_gid = id;

	if (newname != NULL)
		grp->gr_name = pw_checkname(newname, 0);

	grp_set_passwd(grp, true, fd, precrypted);
	/*
	 * Keep the same logic as old code for now:
	 * if -M is passed, -d and -m are ignored
	 * then id -d, -m is ignored
	 * last is -m
	 */

	if (members) {
		grp->gr_mem = NULL;
		grp_add_members(&grp, members);
	} else if (oldmembers) {
		delete_members(grp, oldmembers);
	} else if (newmembers) {
		grp_add_members(&grp, newmembers);
	}

	if (dryrun) {
		print_group(grp, pretty);
		return (EXIT_SUCCESS);
	}

	if ((rc = chggrent(name, grp)) != 0) {
		if (rc == -1)
			errx(EX_IOERR, "group '%s' not available (NIS?)",
			    grp->gr_name);
		else
			err(EX_IOERR, "group update");
	}

	if (newname)
		name = newname;

	/* grp may have been invalidated */
	if ((grp = GETGRNAM(name)) == NULL)
		errx(EX_SOFTWARE, "group disappeared during update");

	pw_log(cnf, M_UPDATE, W_GROUP, "%s(%ju)", grp->gr_name,
	    (uintmax_t)grp->gr_gid);

	if (nis && nis_update() == 0)
		pw_log(cnf, M_UPDATE, W_GROUP, "NIS maps updated");

	return (EXIT_SUCCESS);
}
