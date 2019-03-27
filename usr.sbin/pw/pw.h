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
 * $FreeBSD$
 */

#include <sys/stat.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "pwupd.h"

enum _mode
{
        M_ADD,
        M_DELETE,
        M_UPDATE,
        M_PRINT,
	M_NEXT,
	M_LOCK,
	M_UNLOCK,
        M_NUM
};

enum _passmode
{
	P_NO,
	P_NONE,
	P_RANDOM,
	P_YES
};

enum _which
{
        W_USER,
        W_GROUP,
        W_NUM
};

#define	_DEF_DIRMODE	(S_IRWXU | S_IRWXG | S_IRWXO)
#define	_PW_CONF	"pw.conf"
#define _UC_MAXLINE	1024
#define _UC_MAXSHELLS	32

struct userconf *get_userconfig(const char *cfg);
struct userconf *read_userconfig(char const * file);
int write_userconfig(struct userconf *cnf, char const * file);

int pw_group_add(int argc, char **argv, char *name);
int pw_group_del(int argc, char **argv, char *name);
int pw_group_mod(int argc, char **argv, char *name);
int pw_group_next(int argc, char **argv, char *name);
int pw_group_show(int argc, char **argv, char *name);
int pw_user_add(int argc, char **argv, char *name);
int pw_user_add(int argc, char **argv, char *name);
int pw_user_add(int argc, char **argv, char *name);
int pw_user_add(int argc, char **argv, char *name);
int pw_user_del(int argc, char **argv, char *name);
int pw_user_lock(int argc, char **argv, char *name);
int pw_user_mod(int argc, char **argv, char *name);
int pw_user_next(int argc, char **argv, char *name);
int pw_user_show(int argc, char **argv, char *name);
int pw_user_unlock(int argc, char **argv, char *name);
int pw_groupnext(struct userconf *cnf, bool quiet);
char *pw_checkname(char *name, int gecos);
uintmax_t pw_checkid(char *nptr, uintmax_t maxval);
int pw_checkfd(char *nptr);

int addnispwent(const char *path, struct passwd *pwd);
int delnispwent(const char *path, const char *login);
int chgnispwent(const char *path, const char *login, struct passwd *pwd);

int groupadd(struct userconf *, char *name, gid_t id, char *members, int fd,
    bool dryrun, bool pretty, bool precrypted);

int nis_update(void);

int boolean_val(char const * str, int dflt);
int passwd_val(char const * str, int dflt);
char const *boolean_str(int val);
char *newstr(char const * p);

void pw_log(struct userconf * cnf, int mode, int which, char const * fmt,...) __printflike(4, 5);
char *pw_pwcrypt(char *password);

extern const char *Modes[];
extern const char *Which[];

uintmax_t strtounum(const char * __restrict, uintmax_t, uintmax_t,
    const char ** __restrict);
