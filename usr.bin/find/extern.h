/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)extern.h	8.3 (Berkeley) 4/16/94
 *	$FreeBSD$
 */

#include <sys/cdefs.h>

void	 brace_subst(char *, char **, char *, size_t);
PLAN	*find_create(char ***);
int	 find_execute(PLAN *, char **);
PLAN	*find_formplan(char **);
PLAN	*not_squish(PLAN *);
PLAN	*or_squish(PLAN *);
PLAN	*paren_squish(PLAN *);
time_t	 get_date(char *);
struct stat;
void	 printlong(char *, char *, struct stat *);
int	 queryuser(char **);
OPTION	*lookup_option(const char *);
void	 finish_execplus(void);

creat_f	c_Xmin;
creat_f	c_Xtime;
creat_f	c_acl;
creat_f	c_and;
creat_f	c_delete;
creat_f	c_depth;
creat_f	c_empty;
creat_f	c_exec;
creat_f	c_flags;
creat_f	c_follow;
creat_f	c_fstype;
creat_f	c_group;
creat_f	c_ignore_readdir_race;
creat_f	c_inum;
creat_f	c_links;
creat_f	c_ls;
creat_f	c_mXXdepth;
creat_f	c_name;
creat_f	c_newer;
creat_f	c_nogroup;
creat_f	c_nouser;
creat_f	c_perm;
creat_f	c_print;
creat_f	c_regex;
creat_f	c_samefile;
creat_f	c_simple;
creat_f	c_size;
creat_f	c_sparse;
creat_f	c_type;
creat_f	c_user;
creat_f	c_xdev;

exec_f	f_Xmin;
exec_f	f_Xtime;
exec_f	f_acl;
exec_f	f_always_true;
exec_f	f_closeparen;
exec_f	f_delete;
exec_f	f_depth;
exec_f	f_empty;
exec_f	f_exec;
exec_f	f_expr;
exec_f	f_false;
exec_f	f_flags;
exec_f	f_fstype;
exec_f	f_group;
exec_f	f_inum;
exec_f	f_links;
exec_f	f_ls;
exec_f	f_name;
exec_f	f_newer;
exec_f	f_nogroup;
exec_f	f_not;
exec_f	f_nouser;
exec_f	f_openparen;
exec_f	f_or;
exec_f	f_path;
exec_f	f_perm;
exec_f	f_print;
exec_f	f_print0;
exec_f	f_prune;
exec_f	f_quit;
exec_f	f_regex;
exec_f	f_size;
exec_f	f_sparse;
exec_f	f_type;
exec_f	f_user;

extern int ftsoptions, ignore_readdir_race, isdepth, isoutput;
extern int issort, isxargs;
extern int mindepth, maxdepth;
extern int regexp_flags;
extern int exitstatus;
extern time_t now;
extern int dotfd;
extern FTS *tree;
