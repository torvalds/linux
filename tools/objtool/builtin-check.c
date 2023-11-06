// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <subcmd/parse-options.h>
#include <string.h>
#include <stdlib.h>
#include <objtool/builtin.h>
#include <objtool/objtool.h>

#define ERROR(format, ...)				\
	fprintf(stderr,					\
		"error: objtool: " format "\n",		\
		##__VA_ARGS__)

struct opts opts;

static const char * const check_usage[] = {
	"objtool <actions> [<options>] file.o",
	NULL,
};

static const char * const env_usage[] = {
	"OBJTOOL_ARGS=\"<options>\"",
	NULL,
};

static int parse_dump(const struct option *opt, const char *str, int unset)
{
	if (!str || !strcmp(str, "orc")) {
		opts.dump_orc = true;
		return 0;
	}

	return -1;
}

static int parse_hacks(const struct option *opt, const char *str, int unset)
{
	bool found = false;

	/*
	 * Use strstr() as a lazy method of checking for comma-separated
	 * options.
	 *
	 * No string provided == enable all options.
	 */

	if (!str || strstr(str, "jump_label")) {
		opts.hack_jump_label = true;
		found = true;
	}

	if (!str || strstr(str, "noinstr")) {
		opts.hack_noinstr = true;
		found = true;
	}

	if (!str || strstr(str, "skylake")) {
		opts.hack_skylake = true;
		found = true;
	}

	return found ? 0 : -1;
}

static const struct option check_options[] = {
	OPT_GROUP("Actions:"),
	OPT_CALLBACK_OPTARG('h', "hacks", NULL, NULL, "jump_label,noinstr,skylake", "patch toolchain bugs/limitations", parse_hacks),
	OPT_BOOLEAN('i', "ibt", &opts.ibt, "validate and annotate IBT"),
	OPT_BOOLEAN('m', "mcount", &opts.mcount, "annotate mcount/fentry calls for ftrace"),
	OPT_BOOLEAN('n', "noinstr", &opts.noinstr, "validate noinstr rules"),
	OPT_BOOLEAN('o', "orc", &opts.orc, "generate ORC metadata"),
	OPT_BOOLEAN('r', "retpoline", &opts.retpoline, "validate and annotate retpoline usage"),
	OPT_BOOLEAN(0,   "rethunk", &opts.rethunk, "validate and annotate rethunk usage"),
	OPT_BOOLEAN(0,   "unret", &opts.unret, "validate entry unret placement"),
	OPT_INTEGER(0,   "prefix", &opts.prefix, "generate prefix symbols"),
	OPT_BOOLEAN('l', "sls", &opts.sls, "validate straight-line-speculation mitigations"),
	OPT_BOOLEAN('s', "stackval", &opts.stackval, "validate frame pointer rules"),
	OPT_BOOLEAN('t', "static-call", &opts.static_call, "annotate static calls"),
	OPT_BOOLEAN('u', "uaccess", &opts.uaccess, "validate uaccess rules for SMAP"),
	OPT_BOOLEAN(0  , "cfi", &opts.cfi, "annotate kernel control flow integrity (kCFI) function preambles"),
	OPT_CALLBACK_OPTARG(0, "dump", NULL, NULL, "orc", "dump metadata", parse_dump),

	OPT_GROUP("Options:"),
	OPT_BOOLEAN(0, "backtrace", &opts.backtrace, "unwind on error"),
	OPT_BOOLEAN(0, "backup", &opts.backup, "create .orig files before modification"),
	OPT_BOOLEAN(0, "dry-run", &opts.dryrun, "don't write modifications"),
	OPT_BOOLEAN(0, "link", &opts.link, "object is a linked object"),
	OPT_BOOLEAN(0, "module", &opts.module, "object is part of a kernel module"),
	OPT_BOOLEAN(0, "mnop", &opts.mnop, "nop out mcount call sites"),
	OPT_BOOLEAN(0, "no-unreachable", &opts.no_unreachable, "skip 'unreachable instruction' warnings"),
	OPT_BOOLEAN(0, "sec-address", &opts.sec_address, "print section addresses in warnings"),
	OPT_BOOLEAN(0, "stats", &opts.stats, "print statistics"),
	OPT_BOOLEAN('v', "verbose", &opts.verbose, "verbose warnings"),

	OPT_END(),
};

int cmd_parse_options(int argc, const char **argv, const char * const usage[])
{
	const char *envv[16] = { };
	char *env;
	int envc;

	env = getenv("OBJTOOL_ARGS");
	if (env) {
		envv[0] = "OBJTOOL_ARGS";
		for (envc = 1; envc < ARRAY_SIZE(envv); ) {
			envv[envc++] = env;
			env = strchr(env, ' ');
			if (!env)
				break;
			*env = '\0';
			env++;
		}

		parse_options(envc, envv, check_options, env_usage, 0);
	}

	env = getenv("OBJTOOL_VERBOSE");
	if (env && !strcmp(env, "1"))
		opts.verbose = true;

	argc = parse_options(argc, argv, check_options, usage, 0);
	if (argc != 1)
		usage_with_options(usage, check_options);
	return argc;
}

static bool opts_valid(void)
{
	if (opts.hack_jump_label	||
	    opts.hack_noinstr		||
	    opts.ibt			||
	    opts.mcount			||
	    opts.noinstr		||
	    opts.orc			||
	    opts.retpoline		||
	    opts.rethunk		||
	    opts.sls			||
	    opts.stackval		||
	    opts.static_call		||
	    opts.uaccess) {
		if (opts.dump_orc) {
			ERROR("--dump can't be combined with other options");
			return false;
		}

		return true;
	}

	if (opts.unret && !opts.rethunk) {
		ERROR("--unret requires --rethunk");
		return false;
	}

	if (opts.dump_orc)
		return true;

	ERROR("At least one command required");
	return false;
}

static bool mnop_opts_valid(void)
{
	if (opts.mnop && !opts.mcount) {
		ERROR("--mnop requires --mcount");
		return false;
	}

	return true;
}

static bool link_opts_valid(struct objtool_file *file)
{
	if (opts.link)
		return true;

	if (has_multiple_files(file->elf)) {
		ERROR("Linked object detected, forcing --link");
		opts.link = true;
		return true;
	}

	if (opts.noinstr) {
		ERROR("--noinstr requires --link");
		return false;
	}

	if (opts.ibt) {
		ERROR("--ibt requires --link");
		return false;
	}

	if (opts.unret) {
		ERROR("--unret requires --link");
		return false;
	}

	return true;
}

int objtool_run(int argc, const char **argv)
{
	const char *objname;
	struct objtool_file *file;
	int ret;

	argc = cmd_parse_options(argc, argv, check_usage);
	objname = argv[0];

	if (!opts_valid())
		return 1;

	if (opts.dump_orc)
		return orc_dump(objname);

	file = objtool_open_read(objname);
	if (!file)
		return 1;

	if (!mnop_opts_valid())
		return 1;

	if (!link_opts_valid(file))
		return 1;

	ret = check(file);
	if (ret)
		return ret;

	if (file->elf->changed)
		return elf_write(file->elf);

	return 0;
}
