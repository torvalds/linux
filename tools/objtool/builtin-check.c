// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2017 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#include <subcmd/parse-options.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <objtool/builtin.h>
#include <objtool/objtool.h>

#define ERROR(format, ...)				\
	fprintf(stderr,					\
		"error: objtool: " format "\n",		\
		##__VA_ARGS__)

const char *objname;

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
	OPT_BOOLEAN(0,   "orc", &opts.orc, "generate ORC metadata"),
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
	OPT_BOOLEAN(0,   "backtrace", &opts.backtrace, "unwind on error"),
	OPT_BOOLEAN(0,   "dry-run", &opts.dryrun, "don't write modifications"),
	OPT_BOOLEAN(0,   "link", &opts.link, "object is a linked object"),
	OPT_BOOLEAN(0,   "module", &opts.module, "object is part of a kernel module"),
	OPT_BOOLEAN(0,   "mnop", &opts.mnop, "nop out mcount call sites"),
	OPT_BOOLEAN(0,   "no-unreachable", &opts.no_unreachable, "skip 'unreachable instruction' warnings"),
	OPT_STRING('o',  "output", &opts.output, "file", "output file name"),
	OPT_BOOLEAN(0,   "sec-address", &opts.sec_address, "print section addresses in warnings"),
	OPT_BOOLEAN(0,   "stats", &opts.stats, "print statistics"),
	OPT_BOOLEAN('v', "verbose", &opts.verbose, "verbose warnings"),
	OPT_BOOLEAN(0,   "Werror", &opts.werror, "return error on warnings"),

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
	if (opts.mnop && !opts.mcount) {
		ERROR("--mnop requires --mcount");
		return false;
	}

	if (opts.noinstr && !opts.link) {
		ERROR("--noinstr requires --link");
		return false;
	}

	if (opts.ibt && !opts.link) {
		ERROR("--ibt requires --link");
		return false;
	}

	if (opts.unret && !opts.link) {
		ERROR("--unret requires --link");
		return false;
	}

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
			ERROR("--dump can't be combined with other actions");
			return false;
		}

		return true;
	}

	if (opts.dump_orc)
		return true;

	ERROR("At least one action required");
	return false;
}

static int copy_file(const char *src, const char *dst)
{
	size_t to_copy, copied;
	int dst_fd, src_fd;
	struct stat stat;
	off_t offset = 0;

	src_fd = open(src, O_RDONLY);
	if (src_fd == -1) {
		ERROR("can't open '%s' for reading", src);
		return 1;
	}

	dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0400);
	if (dst_fd == -1) {
		ERROR("can't open '%s' for writing", dst);
		return 1;
	}

	if (fstat(src_fd, &stat) == -1) {
		perror("fstat");
		return 1;
	}

	if (fchmod(dst_fd, stat.st_mode) == -1) {
		perror("fchmod");
		return 1;
	}

	for (to_copy = stat.st_size; to_copy > 0; to_copy -= copied) {
		copied = sendfile(dst_fd, src_fd, &offset, to_copy);
		if (copied == -1) {
			perror("sendfile");
			return 1;
		}
	}

	close(dst_fd);
	close(src_fd);
	return 0;
}

static char **save_argv(int argc, const char **argv)
{
	char **orig_argv;

	orig_argv = calloc(argc, sizeof(char *));
	if (!orig_argv) {
		perror("calloc");
		return NULL;
	}

	for (int i = 0; i < argc; i++) {
		orig_argv[i] = strdup(argv[i]);
		if (!orig_argv[i]) {
			perror("strdup");
			return NULL;
		}
	};

	return orig_argv;
}

#define ORIG_SUFFIX ".orig"

int objtool_run(int argc, const char **argv)
{
	struct objtool_file *file;
	char *backup = NULL;
	char **orig_argv;
	int ret = 0;

	orig_argv = save_argv(argc, argv);
	if (!orig_argv)
		return 1;

	cmd_parse_options(argc, argv, check_usage);

	if (!opts_valid())
		return 1;

	objname = argv[0];

	if (opts.dump_orc)
		return orc_dump(objname);

	if (!opts.dryrun && opts.output) {
		/* copy original .o file to output file */
		if (copy_file(objname, opts.output))
			return 1;

		/* from here on, work directly on the output file */
		objname = opts.output;
	}

	file = objtool_open_read(objname);
	if (!file)
		goto err;

	if (!opts.link && has_multiple_files(file->elf)) {
		ERROR("Linked object requires --link");
		goto err;
	}

	ret = check(file);
	if (ret)
		goto err;

	if (!opts.dryrun && file->elf->changed && elf_write(file->elf))
		goto err;

	return 0;

err:
	if (opts.dryrun)
		goto err_msg;

	if (opts.output) {
		unlink(opts.output);
		goto err_msg;
	}

	/*
	 * Make a backup before kbuild deletes the file so the error
	 * can be recreated without recompiling or relinking.
	 */
	backup = malloc(strlen(objname) + strlen(ORIG_SUFFIX) + 1);
	if (!backup) {
		perror("malloc");
		return 1;
	}

	strcpy(backup, objname);
	strcat(backup, ORIG_SUFFIX);
	if (copy_file(objname, backup))
		return 1;

err_msg:
	fprintf(stderr, "%s", orig_argv[0]);

	for (int i = 1; i < argc; i++) {
		char *arg = orig_argv[i];

		if (backup && !strcmp(arg, objname))
			fprintf(stderr, " %s -o %s", backup, objname);
		else
			fprintf(stderr, " %s", arg);
	}

	fprintf(stderr, "\n");

	return 1;
}
