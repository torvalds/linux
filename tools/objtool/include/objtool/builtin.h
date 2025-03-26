/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */
#ifndef _BUILTIN_H
#define _BUILTIN_H

#include <subcmd/parse-options.h>

struct opts {
	/* actions: */
	bool dump_orc;
	bool hack_jump_label;
	bool hack_noinstr;
	bool hack_skylake;
	bool ibt;
	bool mcount;
	bool noinstr;
	bool orc;
	bool retpoline;
	bool rethunk;
	bool unret;
	bool sls;
	bool stackval;
	bool static_call;
	bool uaccess;
	int prefix;
	bool cfi;

	/* options: */
	bool backtrace;
	bool dryrun;
	bool link;
	bool mnop;
	bool module;
	bool no_unreachable;
	const char *output;
	bool sec_address;
	bool stats;
	bool verbose;
	bool werror;
};

extern struct opts opts;

extern int cmd_parse_options(int argc, const char **argv, const char * const usage[]);

extern int objtool_run(int argc, const char **argv);

#endif /* _BUILTIN_H */
