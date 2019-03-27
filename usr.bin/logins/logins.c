/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct xpasswd {
	char		*pw_name;
	char		*pw_passwd;
	uid_t		 pw_uid;
	gid_t		 pw_gid;
	time_t		 pw_change;
	char		*pw_class;
	char		*pw_gecos;
	char		*pw_dir;
	char		*pw_shell;
	time_t		 pw_expire;
	int		 pw_selected;
};

struct xgroup {
	char		*gr_name;
	char		*gr_passwd;
	gid_t		 gr_gid;
	char		*gr_mem;
};

static int		 everything = 1;
static int		 a_flag;
static int		 d_flag;
static const char	*g_args;
static const char	*l_args;
static int		 m_flag;
static int		 o_flag;
static int		 p_flag;
static int		 s_flag;
static int		 t_flag;
static int		 u_flag;
static int		 x_flag;

static int
member(const char *elem, const char *list)
{
	char *p;
	int len;

	p = strstr(list, elem);
	len = strlen(elem);

	return (p != NULL &&
	    (p == list || p[-1] == ',') &&
	    (p[len] == '\0' || p[len] == ','));
}

static void *
xmalloc(size_t size)
{
	void *newptr;

	if ((newptr = malloc(size)) == NULL)
		err(1, "malloc()");
	return (newptr);
}

static void *
xrealloc(void *ptr, size_t size)
{
	void *newptr;

	if ((newptr = realloc(ptr, size)) == NULL)
		err(1, "realloc()");
	return (newptr);
}

static char *
xstrdup(const char *str)
{
	char *dupstr;

	if ((dupstr = strdup(str)) == NULL)
		err(1, "strdup()");
	return (dupstr);
}

static struct xgroup	*grps;
static size_t		 grpsz;
static size_t		 ngrps;

static void
get_groups(void)
{
	struct group *grp;
	size_t len;
	int i;

	setgrent();
	for (;;) {
		if (ngrps == grpsz) {
			grpsz += grpsz ? grpsz : 128;
			grps = xrealloc(grps, grpsz * sizeof *grps);
		}
		if ((grp = getgrent()) == NULL)
			break;
		grps[ngrps].gr_name = xstrdup(grp->gr_name);
		grps[ngrps].gr_passwd = xstrdup(grp->gr_passwd);
		grps[ngrps].gr_gid = grp->gr_gid;
		grps[ngrps].gr_mem = xstrdup("");
		for (i = 0, len = 1; grp->gr_mem[i] != NULL; ++i)
			len += strlen(grp->gr_mem[i]) + 1;
		grps[ngrps].gr_mem = xmalloc(len);
		for (i = 0, len = 0; grp->gr_mem[i] != NULL; ++i)
			len += sprintf(grps[ngrps].gr_mem + len,
			    i ? ",%s" : "%s", grp->gr_mem[i]);
		grps[ngrps].gr_mem[len] = '\0';
		ngrps++;
	}
	endgrent();
}

static struct xgroup *
find_group_bygid(gid_t gid)
{
	unsigned int i;

	for (i = 0; i < ngrps; ++i)
		if (grps[i].gr_gid == gid)
			return (&grps[i]);
	return (NULL);
}

#if 0
static struct xgroup *
find_group_byname(const char *name)
{
	unsigned int i;

	for (i = 0; i < ngrps; ++i)
		if (strcmp(grps[i].gr_name, name) == 0)
			return (&grps[i]);
	return (NULL);
}
#endif

static struct xpasswd	*pwds;
static size_t		 pwdsz;
static size_t		 npwds;

static int
pwd_cmp_byname(const void *ap, const void *bp)
{
	const struct passwd *a = ap;
	const struct passwd *b = bp;

	return (strcmp(a->pw_name, b->pw_name));
}

static int
pwd_cmp_byuid(const void *ap, const void *bp)
{
	const struct passwd *a = ap;
	const struct passwd *b = bp;

	return (a->pw_uid - b->pw_uid);
}

static void
get_users(void)
{
	struct passwd *pwd;

	setpwent();
	for (;;) {
		if (npwds == pwdsz) {
			pwdsz += pwdsz ? pwdsz : 128;
			pwds = xrealloc(pwds, pwdsz * sizeof *pwds);
		}
		if ((pwd = getpwent()) == NULL)
			break;
		pwds[npwds].pw_name = xstrdup(pwd->pw_name);
		pwds[npwds].pw_passwd = xstrdup(pwd->pw_passwd);
		pwds[npwds].pw_uid = pwd->pw_uid;
		pwds[npwds].pw_gid = pwd->pw_gid;
		pwds[npwds].pw_change = pwd->pw_change;
		pwds[npwds].pw_class = xstrdup(pwd->pw_class);
		pwds[npwds].pw_gecos = xstrdup(pwd->pw_gecos);
		pwds[npwds].pw_dir = xstrdup(pwd->pw_dir);
		pwds[npwds].pw_shell = xstrdup(pwd->pw_shell);
		pwds[npwds].pw_expire = pwd->pw_expire;
		pwds[npwds].pw_selected = 0;
		npwds++;
	}
	endpwent();
}

static void
select_users(void)
{
	unsigned int i, j;
	struct xgroup *grp;
	struct xpasswd *pwd;

	for (i = 0, pwd = pwds; i < npwds; ++i, ++pwd) {
		if (everything) {
			pwd->pw_selected = 1;
			continue;
		}
		if (d_flag)
			if ((i > 0 && pwd->pw_uid == pwd[-1].pw_uid) ||
			    (i < npwds - 1 && pwd->pw_uid == pwd[1].pw_uid)) {
				pwd->pw_selected = 1;
				continue;
			}
		if (g_args) {
			for (j = 0, grp = grps; j < ngrps; ++j, ++grp) {
				if (member(grp->gr_name, g_args) &&
				    member(pwd->pw_name, grp->gr_mem)) {
					pwd->pw_selected = 1;
					break;
				}
			}
			if (pwd->pw_selected)
				continue;
		}
		if (l_args)
			if (member(pwd->pw_name, l_args)) {
				pwd->pw_selected = 1;
				continue;
			}
		if (p_flag)
			if (pwd->pw_passwd[0] == '\0') {
				pwd->pw_selected = 1;
				continue;
			}
		if (s_flag)
			if (pwd->pw_uid < 1000 || pwd->pw_uid == 65534) {
				pwd->pw_selected = 1;
				continue;
			}
		if (u_flag)
			if (pwd->pw_uid >= 1000 && pwd->pw_uid != 65534) {
				pwd->pw_selected = 1;
				continue;
			}
	}
}

static void
sort_users(void)
{
	if (t_flag)
		mergesort(pwds, npwds, sizeof *pwds, pwd_cmp_byname);
	else
		mergesort(pwds, npwds, sizeof *pwds, pwd_cmp_byuid);
}

static void
display_user(struct xpasswd *pwd)
{
	struct xgroup *grp;
	unsigned int i;
	char cbuf[16], ebuf[16];
	struct tm *tm;

	grp = find_group_bygid(pwd->pw_gid);
	printf(o_flag ? "%s:%ld:%s:%ld:%s" : "%-15s %-7ld %-15s %-7ld %s\n",
	    pwd->pw_name, (long)pwd->pw_uid, grp ? grp->gr_name : "",
	    (long)pwd->pw_gid, pwd->pw_gecos);
	if (m_flag) {
		for (i = 0, grp = grps; i < ngrps; ++i, ++grp) {
			if (grp->gr_gid == pwd->pw_gid ||
			    !member(pwd->pw_name, grp->gr_mem))
				continue;
			printf(o_flag ? "%s:%s:%ld" : "%24s%-15s %-7ld\n",
			    "", grp->gr_name, (long)grp->gr_gid);
		}
	}
	if (x_flag) {
		printf(o_flag ? "%s:%s" : "%24s%s\n", "", pwd->pw_dir);
		printf(o_flag ? "%s:%s" : "%24s%s\n", "", pwd->pw_shell);
	}
	if (a_flag) {
		tm = gmtime(&pwd->pw_change);
		strftime(cbuf, sizeof(cbuf), pwd->pw_change ? "%F" : "0", tm);
		tm = gmtime(&pwd->pw_expire);
		strftime(ebuf, sizeof(ebuf), pwd->pw_expire ? "%F" : "0", tm);
		printf(o_flag ? "%s:%s:%s" : "%24s%s %s\n", "", cbuf, ebuf);
	}
	if (o_flag)
		printf("\n");
}

static void
list_users(void)
{
	struct xpasswd *pwd;
	unsigned int i;

	for (i = 0, pwd = pwds; i < npwds; ++i, ++pwd)
		if (pwd->pw_selected)
			display_user(pwd);
}

static void
usage(void)
{
	fprintf(stderr, "usage: logins [-admopstux] [-g group] [-l login]\n");
	exit(1);
}

int
main(int argc, char * const argv[])
{
	int o;

	while ((o = getopt(argc, argv, "adg:l:mopstux")) != -1)
		switch (o) {
		case 'a':
			a_flag = 1;
			break;
		case 'd':
			everything = 0;
			d_flag = 1;
			break;
		case 'g':
			everything = 0;
			g_args = optarg;
			break;
		case 'l':
			everything = 0;
			l_args = optarg;
			break;
		case 'm':
			m_flag = 1;
			break;
		case 'o':
			o_flag = 1;
			break;
		case 'p':
			everything = 0;
			p_flag = 1;
			break;
		case 's':
			everything = 0;
			s_flag = 1;
			break;
		case 't':
			t_flag = 1;
			break;
		case 'u':
			everything = 0;
			u_flag = 1;
			break;
		case 'x':
			x_flag = 1;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	get_groups();
	get_users();
	select_users();
	sort_users();
	list_users();
	exit(0);
}
