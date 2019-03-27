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
 *
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <pwd.h>
#include <grp.h>
#include <libutil.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>

#include "pwupd.h"

static FILE * pwd_fp = NULL;
static int pwd_scanflag;
static const char *pwd_filename;

void
vendpwent(void)
{
	if (pwd_fp != NULL) {
		fclose(pwd_fp);
		pwd_fp = NULL;
	}
}

void
vsetpwent(void)
{
	vendpwent();
}

static struct passwd *
vnextpwent(char const *nam, uid_t uid, int doclose)
{
	struct passwd *pw;
	char *line;
	size_t linecap;
	ssize_t linelen;

	pw = NULL;
	line = NULL;
	linecap = 0;

	if (pwd_fp == NULL) {
		if (geteuid() == 0) {
			pwd_filename = _MASTERPASSWD;
			pwd_scanflag = PWSCAN_MASTER;
		} else {
			pwd_filename = _PASSWD;
			pwd_scanflag = 0;
		}
		pwd_fp = fopen(getpwpath(pwd_filename), "r");
	}
	 
	if (pwd_fp != NULL) {
		while ((linelen = getline(&line, &linecap, pwd_fp)) > 0) {
			/* Skip comments and empty lines */
			if (*line == '\n' || *line == '#')
				continue;
			/* trim latest \n */
			if (line[linelen - 1 ] == '\n')
				line[linelen - 1] = '\0';
			pw = pw_scan(line, pwd_scanflag);
			if (pw == NULL)
				errx(EXIT_FAILURE, "Invalid user entry in '%s':"
				    " '%s'", getpwpath(pwd_filename), line);
			if (uid != (uid_t)-1) {
				if (uid == pw->pw_uid)
					break;
			} else if (nam != NULL) {
				if (strcmp(nam, pw->pw_name) == 0)
					break;
			} else
				break;
			free(pw);
			pw = NULL;
		}
		if (doclose)
			vendpwent();
	}
	free(line);

	return (pw);
}

struct passwd *
vgetpwent(void)
{
  return vnextpwent(NULL, -1, 0);
}

struct passwd *
vgetpwuid(uid_t uid)
{
  return vnextpwent(NULL, uid, 1);
}

struct passwd *
vgetpwnam(const char * nam)
{
  return vnextpwent(nam, -1, 1);
}


static FILE * grp_fp = NULL;

void
vendgrent(void)
{
	if (grp_fp != NULL) {
		fclose(grp_fp);
		grp_fp = NULL;
	}
}

void
vsetgrent(void)
{
	vendgrent();
}

static struct group *
vnextgrent(char const *nam, gid_t gid, int doclose)
{
	struct group *gr;
	char *line;
	size_t linecap;
	ssize_t linelen;

	gr = NULL;
	line = NULL;
	linecap = 0;

	if (grp_fp != NULL || (grp_fp = fopen(getgrpath(_GROUP), "r")) != NULL) {
		while ((linelen = getline(&line, &linecap, grp_fp)) > 0) {
			/* Skip comments and empty lines */
			if (*line == '\n' || *line == '#')
				continue;
			/* trim latest \n */
			if (line[linelen - 1 ] == '\n')
				line[linelen - 1] = '\0';
			gr = gr_scan(line);
			if (gr == NULL)
				errx(EXIT_FAILURE, "Invalid group entry in '%s':"
				    " '%s'", getgrpath(_GROUP), line);
			if (gid != (gid_t)-1) {
				if (gid == gr->gr_gid)
					break;
			} else if (nam != NULL) {
				if (strcmp(nam, gr->gr_name) == 0)
					break;
			} else
				break;
			free(gr);
			gr = NULL;
		}
		if (doclose)
			vendgrent();
	}
	free(line);

	return (gr);
}

struct group *
vgetgrent(void)
{
  return vnextgrent(NULL, -1, 0);
}


struct group *
vgetgrgid(gid_t gid)
{
  return vnextgrent(NULL, gid, 1);
}

struct group *
vgetgrnam(const char * nam)
{
  return vnextgrent(nam, -1, 1);
}

