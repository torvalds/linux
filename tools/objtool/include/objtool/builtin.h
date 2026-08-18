/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2015 Josh Poimboeuf <jpoimboe@redhat.com>
 */
#ifndef _BUILTIN_H
#define _BUILTIN_H

#include <subcmd/parse-options.h>

struct opts {
	/* actions: */
	bool checksum;
	const char *disas;
	bool dump_orc;
	bool hack_jump_label;
	bool hack_noinstr;
	bool hack_skylake;
	bool ibt;
	bool mcount;
	bool noabs;
	bool noinstr;
	bool orc;
	int prefix;
	bool retpoline;
	bool rethunk;
	bool unret;
	bool sls;
	bool stackval;
	bool static_call;
	bool uaccess;

	/* options: */
	bool backtrace;
	bool backup;
	bool cfi;
	const char *debug_checksum;
	bool dryrun;
	bool fineibt;
	bool link;
	bool mnop;
	bool module;
	bool no_unreachable;
	const char *output;
	bool sec_address;
	bool stats;
	const char *trace;
	bool verbose;
	bool werror;
	bool wide;
};

extern struct opts opts;

int cmd_parse_options(int argc, const char **argv, const char * const usage[]);

int objtool_run(int argc, const char **argv);

int make_backup(void);

int cmd_klp(int argc, const char **argv);

#endif /* _BUILTIN_H */
