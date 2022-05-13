/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */
#ifndef _BUILTIN_H
#define _BUILTIN_H

#include <subcmd/parse-options.h>

extern const struct option check_options[];
extern bool no_fp, no_unreachable, retpoline, module, backtrace, uaccess, stats,
	    lto, vmlinux, mcount, noinstr, backup, sls, dryrun,
	    ibt;

extern int cmd_parse_options(int argc, const char **argv, const char * const usage[]);

extern int cmd_check(int argc, const char **argv);
extern int cmd_orc(int argc, const char **argv);

#endif /* _BUILTIN_H */
