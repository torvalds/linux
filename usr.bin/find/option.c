/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Cimarron D. Taylor of the University of California, Berkeley.
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
 */

/*
static char sccsid[] = "@(#)option.c	8.2 (Berkeley) 4/16/94";
*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <err.h>
#include <fts.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "find.h"

static int typecompare(const void *, const void *);

/* NB: the following table must be sorted lexically. */
/* Options listed with C++ comments are in gnu find, but not our find */
static OPTION const options[] = {
	{ "!",		c_simple,	f_not,		0 },
	{ "(",		c_simple,	f_openparen,	0 },
	{ ")",		c_simple,	f_closeparen,	0 },
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	{ "-Bmin",	c_Xmin,		f_Xmin,		F_TIME_B },
	{ "-Bnewer",	c_newer,	f_newer,	F_TIME_B },
	{ "-Btime",	c_Xtime,	f_Xtime,	F_TIME_B },
#endif
	{ "-a",		c_and,		NULL,		0 },
#ifdef ACL_TYPE_NFS4
	{ "-acl",	c_acl,		f_acl,		0 },
#endif
	{ "-amin",	c_Xmin,		f_Xmin,		F_TIME_A },
	{ "-and",	c_and,		NULL,		0 },
	{ "-anewer",	c_newer,	f_newer,	F_TIME_A },
	{ "-atime",	c_Xtime,	f_Xtime,	F_TIME_A },
	{ "-cmin",	c_Xmin,		f_Xmin,		F_TIME_C },
	{ "-cnewer",	c_newer,	f_newer,	F_TIME_C },
	{ "-ctime",	c_Xtime,	f_Xtime,	F_TIME_C },
	{ "-d",		c_depth,	f_depth,	0 },
// -daystart
	{ "-delete",	c_delete,	f_delete,	0 },
	{ "-depth",	c_depth,	f_depth,	0 },
	{ "-empty",	c_empty,	f_empty,	0 },
	{ "-exec",	c_exec,		f_exec,		0 },
	{ "-execdir",	c_exec,		f_exec,		F_EXECDIR },
	{ "-false",	c_simple,	f_false,	0 },
#if HAVE_STRUCT_STAT_ST_FLAGS
	{ "-flags",	c_flags,	f_flags,	0 },
#endif
// -fls
	{ "-follow",	c_follow,	f_always_true,	0 },
// -fprint
// -fprint0
// -fprintf
#if HAVE_STRUCT_STATFS_F_FSTYPENAME
	{ "-fstype",	c_fstype,	f_fstype,	0 },
#endif
	{ "-gid",	c_group,	f_group,	0 },
	{ "-group",	c_group,	f_group,	0 },
	{ "-ignore_readdir_race",c_ignore_readdir_race, f_always_true,0 },
	{ "-ilname",	c_name,		f_name,		F_LINK | F_IGNCASE },
	{ "-iname",	c_name,		f_name,		F_IGNCASE },
	{ "-inum",	c_inum,		f_inum,		0 },
	{ "-ipath",	c_name,		f_path,		F_IGNCASE },
	{ "-iregex",	c_regex,	f_regex,	F_IGNCASE },
	{ "-iwholename",c_name,		f_path,		F_IGNCASE },
	{ "-links",	c_links,	f_links,	0 },
	{ "-lname",	c_name,		f_name,		F_LINK },
	{ "-ls",	c_ls,		f_ls,		0 },
	{ "-maxdepth",	c_mXXdepth,	f_always_true,	F_MAXDEPTH },
	{ "-mindepth",	c_mXXdepth,	f_always_true,	0 },
	{ "-mmin",	c_Xmin,		f_Xmin,		0 },
	{ "-mnewer",	c_newer,	f_newer,	0 },
	{ "-mount",	c_xdev,		f_always_true,	0 },
	{ "-mtime",	c_Xtime,	f_Xtime,	0 },
	{ "-name",	c_name,		f_name,		0 },
	{ "-newer",	c_newer,	f_newer,	0 },
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	{ "-newerBB",	c_newer,	f_newer,	F_TIME_B | F_TIME2_B },
	{ "-newerBa",	c_newer,	f_newer,	F_TIME_B | F_TIME2_A },
	{ "-newerBc",	c_newer,	f_newer,	F_TIME_B | F_TIME2_C },
	{ "-newerBm",	c_newer,	f_newer,	F_TIME_B },
	{ "-newerBt",	c_newer,	f_newer,	F_TIME_B | F_TIME2_T },
	{ "-neweraB",	c_newer,	f_newer,	F_TIME_A | F_TIME2_B },
#endif
	{ "-neweraa",	c_newer,	f_newer,	F_TIME_A | F_TIME2_A },
	{ "-newerac",	c_newer,	f_newer,	F_TIME_A | F_TIME2_C },
	{ "-neweram",	c_newer,	f_newer,	F_TIME_A },
	{ "-newerat",	c_newer,	f_newer,	F_TIME_A | F_TIME2_T },
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	{ "-newercB",	c_newer,	f_newer,	F_TIME_C | F_TIME2_B },
#endif
	{ "-newerca",	c_newer,	f_newer,	F_TIME_C | F_TIME2_A },
	{ "-newercc",	c_newer,	f_newer,	F_TIME_C | F_TIME2_C },
	{ "-newercm",	c_newer,	f_newer,	F_TIME_C },
	{ "-newerct",	c_newer,	f_newer,	F_TIME_C | F_TIME2_T },
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	{ "-newermB",	c_newer,	f_newer,	F_TIME2_B },
#endif
	{ "-newerma",	c_newer,	f_newer,	F_TIME2_A },
	{ "-newermc",	c_newer,	f_newer,	F_TIME2_C },
	{ "-newermm",	c_newer,	f_newer,	0 },
	{ "-newermt",	c_newer,	f_newer,	F_TIME2_T },
	{ "-nogroup",	c_nogroup,	f_nogroup,	0 },
	{ "-noignore_readdir_race",c_ignore_readdir_race, f_always_true,0 },
	{ "-noleaf",	c_simple,	f_always_true,	0 },
	{ "-not",	c_simple,	f_not,		0 },
	{ "-nouser",	c_nouser,	f_nouser,	0 },
	{ "-o",		c_simple,	f_or,		0 },
	{ "-ok",	c_exec,		f_exec,		F_NEEDOK },
	{ "-okdir",	c_exec,		f_exec,		F_NEEDOK | F_EXECDIR },
	{ "-or",	c_simple,	f_or,		0 },
	{ "-path", 	c_name,		f_path,		0 },
	{ "-perm",	c_perm,		f_perm,		0 },
	{ "-print",	c_print,	f_print,	0 },
	{ "-print0",	c_print,	f_print0,	0 },
// -printf
	{ "-prune",	c_simple,	f_prune,	0 },
	{ "-quit",	c_simple,	f_quit,		0 },
	{ "-regex",	c_regex,	f_regex,	0 },
	{ "-samefile",	c_samefile,	f_inum,		0 },
	{ "-size",	c_size,		f_size,		0 },
	{ "-sparse",	c_sparse,	f_sparse,	0 },
	{ "-true",	c_simple,	f_always_true,	0 },
	{ "-type",	c_type,		f_type,		0 },
	{ "-uid",	c_user,		f_user,		0 },
	{ "-user",	c_user,		f_user,		0 },
	{ "-wholename",	c_name,		f_path,		0 },
	{ "-xdev",	c_xdev,		f_always_true,	0 },
// -xtype
};

/*
 * find_create --
 *	create a node corresponding to a command line argument.
 *
 * TODO:
 *	add create/process function pointers to node, so we can skip
 *	this switch stuff.
 */
PLAN *
find_create(char ***argvp)
{
	OPTION *p;
	PLAN *new;
	char **argv;

	argv = *argvp;

	if ((p = lookup_option(*argv)) == NULL)
		errx(1, "%s: unknown primary or operator", *argv);
	++argv;

	new = (p->create)(p, &argv);
	*argvp = argv;
	return (new);
}

OPTION *
lookup_option(const char *name)
{
	OPTION tmp;

	tmp.name = name;
	return ((OPTION *)bsearch(&tmp, options,
	    sizeof(options)/sizeof(OPTION), sizeof(OPTION), typecompare));
}

static int
typecompare(const void *a, const void *b)
{
	return (strcmp(((const OPTION *)a)->name, ((const OPTION *)b)->name));
}
