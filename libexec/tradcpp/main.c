/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by David A. Holland.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "bool.h"
#include "version.h"
#include "config.h"
#include "utils.h"
#include "array.h"
#include "mode.h"
#include "place.h"
#include "files.h"
#include "directive.h"
#include "macro.h"

struct mode mode = {
	.werror = false,

	.input_allow_dollars = false,
	.input_tabstop = 8,

	.do_stdinc = true,
	.do_stddef = true,

	.do_output = true,
	.output_linenumbers = true,
	.output_cheaplinenumbers = false,
	.output_retain_comments = false,
	.output_file = NULL,

	.do_depend = false,
	.depend_report_system = false,
	.depend_assume_generated = false,
	.depend_issue_fakerules = false,
	.depend_quote_target = true,
	.depend_target = NULL,
	.depend_file = NULL,

	.do_macrolist = false,
	.macrolist_include_stddef = false,
	.macrolist_include_expansions = false,

	.do_trace = false,
	.trace_namesonly = false,
	.trace_indented = false,
};

struct warns warns = {
	.endiflabels = true,
	.nestcomment = false,
	.undef = false,
	.unused = false,
};

////////////////////////////////////////////////////////////
// commandline macros

struct commandline_macro {
	struct place where;
	struct place where2;
	const char *macro;
	const char *expansion;
};

static struct array commandline_macros;

static
void
commandline_macros_init(void)
{
	array_init(&commandline_macros);
}

static
void
commandline_macros_cleanup(void)
{
	unsigned i, num;
	struct commandline_macro *cm;

	num = array_num(&commandline_macros);
	for (i=0; i<num; i++) {
		cm = array_get(&commandline_macros, i);
		dofree(cm, sizeof(*cm));
	}
	array_setsize(&commandline_macros, 0);
	
	array_cleanup(&commandline_macros);
}

static
void
commandline_macro_add(const struct place *p, const char *macro,
		      const struct place *p2, const char *expansion)
{
	struct commandline_macro *cm;

	cm = domalloc(sizeof(*cm));
	cm->where = *p;
	cm->where2 = *p2;
	cm->macro = macro;
	cm->expansion = expansion;

	array_add(&commandline_macros, cm, NULL);
}

static
void
commandline_def(const struct place *p, char *str)
{
	struct place p2;
	char *val;

	if (*str == '\0') {
		complain(NULL, "-D: macro name expected");
		die();
	}

	val = strchr(str, '=');
	if (val != NULL) {
		*val = '\0';
		val++;
	}

	if (val) {
		p2 = *p;
		place_addcolumns(&p2, strlen(str));
	} else {
		place_setbuiltin(&p2, 1);
	}
	commandline_macro_add(p, str, &p2, val ? val : "1");
}

static
void
commandline_undef(const struct place *p, char *str)
{
	if (*str == '\0') {
		complain(NULL, "-U: macro name expected");
		die();
	}
	commandline_macro_add(p, str, p, NULL);
}

static
void
apply_commandline_macros(void)
{
	struct commandline_macro *cm;
	unsigned i, num;

	num = array_num(&commandline_macros);
	for (i=0; i<num; i++) {
		cm = array_get(&commandline_macros, i);
		if (cm->expansion != NULL) {
			macro_define_plain(&cm->where, cm->macro,
					   &cm->where2, cm->expansion);
		} else {
			macro_undef(cm->macro);
		}
		dofree(cm, sizeof(*cm));
	}
	array_setsize(&commandline_macros, 0);
}

static
void
apply_magic_macro(unsigned num, const char *name)
{
	struct place p;

	place_setbuiltin(&p, num);
	macro_define_magic(&p, name);
}

static
void
apply_builtin_macro(unsigned num, const char *name, const char *val)
{
	struct place p;

	place_setbuiltin(&p, num);
	macro_define_plain(&p, name, &p, val);
}

static
void
apply_builtin_macros(void)
{
	unsigned n = 1;

	apply_magic_macro(n++, "__FILE__");
	apply_magic_macro(n++, "__LINE__");

#ifdef CONFIG_OS
	apply_builtin_macro(n++, CONFIG_OS, "1");
#endif
#ifdef CONFIG_OS_2
	apply_builtin_macro(n++, CONFIG_OS_2, "1");
#endif

#ifdef CONFIG_CPU
	apply_builtin_macro(n++, CONFIG_CPU, "1");
#endif
#ifdef CONFIG_CPU_2
	apply_builtin_macro(n++, CONFIG_CPU_2, "1");
#endif

#ifdef CONFIG_SIZE
	apply_builtin_macro(n++, CONFIG_SIZE, "1");
#endif
#ifdef CONFIG_BINFMT
	apply_builtin_macro(n++, CONFIG_BINFMT, "1");
#endif

#ifdef CONFIG_COMPILER
	apply_builtin_macro(n++, CONFIG_COMPILER, VERSION_MAJOR);
	apply_builtin_macro(n++, CONFIG_COMPILER_MINOR, VERSION_MINOR);
	apply_builtin_macro(n++, "__VERSION__", VERSION_LONG);
#endif
}

////////////////////////////////////////////////////////////
// extra included files

struct commandline_file {
	struct place where;
	char *name;
	bool suppress_output;
};

static struct array commandline_files;

static
void
commandline_files_init(void)
{
	array_init(&commandline_files);
}

static
void
commandline_files_cleanup(void)
{
	unsigned i, num;
	struct commandline_file *cf;

	num = array_num(&commandline_files);
	for (i=0; i<num; i++) {
		cf = array_get(&commandline_files, i);
		if (cf != NULL) {
			dofree(cf, sizeof(*cf));
		}
	}
	array_setsize(&commandline_files, 0);

	array_cleanup(&commandline_files);
}

static
void
commandline_addfile(const struct place *p, char *name, bool suppress_output)
{
	struct commandline_file *cf;

	cf = domalloc(sizeof(*cf));
	cf->where = *p;
	cf->name = name;
	cf->suppress_output = suppress_output;
	array_add(&commandline_files, cf, NULL);
}

static
void
commandline_addfile_output(const struct place *p, char *name)
{
	commandline_addfile(p, name, false);
}

static
void
commandline_addfile_nooutput(const struct place *p, char *name)
{
	commandline_addfile(p, name, true);
}

static
void
read_commandline_files(void)
{
	struct commandline_file *cf;
	unsigned i, num;
	bool save = false;

	num = array_num(&commandline_files);
	for (i=0; i<num; i++) {
		cf = array_get(&commandline_files, i);
		array_set(&commandline_files, i, NULL);
		if (cf->suppress_output) {
			save = mode.do_output;
			mode.do_output = false;
			file_readquote(&cf->where, cf->name);
			mode.do_output = save;
		} else {
			file_readquote(&cf->where, cf->name);
		}
		dofree(cf, sizeof(*cf));
	}
	array_setsize(&commandline_files, 0);
}

////////////////////////////////////////////////////////////
// include path accumulation

static struct stringarray incpath_quote;
static struct stringarray incpath_user;
static struct stringarray incpath_system;
static struct stringarray incpath_late;
static const char *sysroot;

static
void
incpath_init(void)
{
	stringarray_init(&incpath_quote);
	stringarray_init(&incpath_user);
	stringarray_init(&incpath_system);
	stringarray_init(&incpath_late);
}

static
void
incpath_cleanup(void)
{
	stringarray_setsize(&incpath_quote, 0);
	stringarray_setsize(&incpath_user, 0);
	stringarray_setsize(&incpath_system, 0);
	stringarray_setsize(&incpath_late, 0);

	stringarray_cleanup(&incpath_quote);
	stringarray_cleanup(&incpath_user);
	stringarray_cleanup(&incpath_system);
	stringarray_cleanup(&incpath_late);
}

static
void
commandline_isysroot(const struct place *p, char *dir)
{
	(void)p;
	sysroot = dir;
}

static
void
commandline_addincpath(struct stringarray *arr, char *s)
{
	if (*s == '\0') {
		complain(NULL, "Empty include directory");
		die();
	}
	stringarray_add(arr, s, NULL);
}

static
void
commandline_addincpath_quote(const struct place *p, char *dir)
{
	(void)p;
	commandline_addincpath(&incpath_quote, dir);
}

static
void
commandline_addincpath_user(const struct place *p, char *dir)
{
	(void)p;
	commandline_addincpath(&incpath_user, dir);
}

static
void
commandline_addincpath_system(const struct place *p, char *dir)
{
	(void)p;
	commandline_addincpath(&incpath_system, dir);
}

static
void
commandline_addincpath_late(const struct place *p, char *dir)
{
	(void)p;
	commandline_addincpath(&incpath_late, dir);
}

static
void
loadincludepath(void)
{
	unsigned i, num;
	const char *dir;
	char *t;

	num = stringarray_num(&incpath_quote);
	for (i=0; i<num; i++) {
		dir = stringarray_get(&incpath_quote, i);
		files_addquotepath(dir, false);
	}
	files_addquotepath(NULL, false);

	num = stringarray_num(&incpath_user);
	for (i=0; i<num; i++) {
		dir = stringarray_get(&incpath_user, i);
		files_addquotepath(dir, false);
		files_addbracketpath(dir, false);
	}

	if (mode.do_stdinc) {
		if (sysroot != NULL) {
			t = dostrdup3(sysroot, "/", CONFIG_LOCALINCLUDE);
			freestringlater(t);
			dir = t;
		} else {
			dir = CONFIG_LOCALINCLUDE;
		}
		files_addquotepath(dir, true);
		files_addbracketpath(dir, true);

		if (sysroot != NULL) {
			t = dostrdup3(sysroot, "/", CONFIG_SYSTEMINCLUDE);
			freestringlater(t);
			dir = t;
		} else {
			dir = CONFIG_SYSTEMINCLUDE;
		}
		files_addquotepath(dir, true);
		files_addbracketpath(dir, true);
	}

	num = stringarray_num(&incpath_system);
	for (i=0; i<num; i++) {
		dir = stringarray_get(&incpath_system, i);
		files_addquotepath(dir, true);
		files_addbracketpath(dir, true);
	}

	num = stringarray_num(&incpath_late);
	for (i=0; i<num; i++) {
		dir = stringarray_get(&incpath_late, i);
		files_addquotepath(dir, false);
		files_addbracketpath(dir, false);
	}
}

////////////////////////////////////////////////////////////
// silly commandline stuff

static const char *commandline_prefix;

static
void
commandline_setprefix(const struct place *p, char *prefix)
{
	(void)p;
	commandline_prefix = prefix;
}

static
void
commandline_addincpath_user_withprefix(const struct place *p, char *dir)
{
	char *s;

	if (commandline_prefix == NULL) {
		complain(NULL, "-iprefix needed");
		die();
	}
	s = dostrdup3(commandline_prefix, "/", dir);
	freestringlater(s);
	commandline_addincpath_user(p, s);
}

static
void
commandline_addincpath_late_withprefix(const struct place *p, char *dir)
{
	char *s;

	if (commandline_prefix == NULL) {
		complain(NULL, "-iprefix needed");
		die();
	}
	s = dostrdup3(commandline_prefix, "/", dir);
	freestringlater(s);
	commandline_addincpath_late(p, s);
}

static
void
commandline_setstd(const struct place *p, char *std)
{
	(void)p;

	if (!strcmp(std, "krc")) {
		return;
	}
	complain(NULL, "Standard %s not supported by this preprocessor", std);
	die();
}

static
void
commandline_setlang(const struct place *p, char *lang)
{
	(void)p;

	if (!strcmp(lang, "c") || !strcmp(lang, "assembler-with-cpp")) {
		return;
	}
	complain(NULL, "Language %s not supported by this preprocessor", lang);
	die();
}

////////////////////////////////////////////////////////////
// complex modes

DEAD static
void
commandline_iremap(const struct place *p, char *str)
{
	(void)p;
	/* XXX */
	(void)str;
	complain(NULL, "-iremap not supported");
	die();
}

static
void
commandline_tabstop(const struct place *p, char *s)
{
	char *t;
	unsigned long val;

	(void)p;

	t = strchr(s, '=');
	if (t == NULL) {
		/* should not happen */
		complain(NULL, "Invalid tabstop");
		die();
	}
	t++;
	errno = 0;
	val = strtoul(t, &t, 10);
	if (errno || *t != '\0') {
		complain(NULL, "Invalid tabstop");
		die();
	}
	if (val > 64) {
		complain(NULL, "Preposterously large tabstop");
		die();
	}
	mode.input_tabstop = val;
}

/*
 * macrolist
 */

static
void
commandline_dD(void)
{
	mode.do_macrolist = true;
	mode.macrolist_include_stddef = false;
	mode.macrolist_include_expansions = true;
}

static
void
commandline_dM(void)
{
	mode.do_macrolist = true;
	mode.macrolist_include_stddef = true;
	mode.macrolist_include_expansions = true;
	mode.do_output = false;
}

static
void
commandline_dN(void)
{
	mode.do_macrolist = true;
	mode.macrolist_include_stddef = false;
	mode.macrolist_include_expansions = false;
}

/*
 * include trace
 */

static
void
commandline_dI(void)
{
	mode.do_trace = true;
	mode.trace_namesonly = false;
	mode.trace_indented = false;
}

static
void
commandline_H(void)
{
	mode.do_trace = true;
	mode.trace_namesonly = true;
	mode.trace_indented = true;
}

/*
 * depend
 */

static
void
commandline_setdependtarget(const struct place *p, char *str)
{
	(void)p;
	mode.depend_target = str;
	mode.depend_quote_target = false;
}

static
void
commandline_setdependtarget_quoted(const struct place *p, char *str)
{
	(void)p;
	mode.depend_target = str;
	mode.depend_quote_target = true;
}

static
void
commandline_setdependoutput(const struct place *p, char *str)
{
	(void)p;
	mode.depend_file = str;
}

static
void
commandline_M(void)
{
	mode.do_depend = true;
	mode.depend_report_system = true;
	mode.do_output = false;
}

static
void
commandline_MM(void)
{
	mode.do_depend = true;
	mode.depend_report_system = false;
	mode.do_output = false;
}

static
void
commandline_MD(void)
{
	mode.do_depend = true;
	mode.depend_report_system = true;
}

static
void
commandline_MMD(void)
{
	mode.do_depend = true;
	mode.depend_report_system = false;
}

static
void
commandline_wall(void)
{
	warns.nestcomment = true;
	warns.undef = true;
	warns.unused = true;
}

static
void
commandline_wnoall(void)
{
	warns.nestcomment = false;
	warns.undef = false;
	warns.unused = false;
}

static
void
commandline_wnone(void)
{
	warns.nestcomment = false;
	warns.endiflabels = false;
	warns.undef = false;
	warns.unused = false;
}

////////////////////////////////////////////////////////////
// options

struct ignore_option {
	const char *string;
};

struct flag_option {
	const char *string;
	bool *flag;
	bool setto;
};

struct act_option {
	const char *string;
	void (*func)(void);
};

struct prefix_option {
	const char *string;
	void (*func)(const struct place *, char *);
};

struct arg_option {
	const char *string;
	void (*func)(const struct place *, char *);
};

static const struct ignore_option ignore_options[] = {
	{ "m32" },
	{ "traditional" },
};
static const unsigned num_ignore_options = HOWMANY(ignore_options);

static const struct flag_option flag_options[] = {
	{ "C",                          &mode.output_retain_comments,  true },
	{ "CC",                         &mode.output_retain_comments,  true },
	{ "MG",                         &mode.depend_assume_generated, true },
	{ "MP",                         &mode.depend_issue_fakerules,  true },
	{ "P",                          &mode.output_linenumbers,      false },
	{ "Wcomment",                   &warns.nestcomment,    true },
	{ "Wendif-labels",              &warns.endiflabels,    true },
	{ "Werror",                     &mode.werror,          true },
	{ "Wno-comment",                &warns.nestcomment,    false },
	{ "Wno-endif-labels",           &warns.endiflabels,    false },
	{ "Wno-error",                  &mode.werror,          false },
	{ "Wno-undef",                  &warns.undef,          false },
	{ "Wno-unused-macros",          &warns.unused,         false },
	{ "Wundef",                     &warns.undef,          true },
	{ "Wunused-macros",             &warns.unused,         true },
	{ "fdollars-in-identifiers",    &mode.input_allow_dollars,     true },
	{ "fno-dollars-in-identifiers", &mode.input_allow_dollars,     false },
	{ "nostdinc",                   &mode.do_stdinc,               false },
	{ "p",                          &mode.output_cheaplinenumbers, true },
	{ "undef",                      &mode.do_stddef,               false },
};
static const unsigned num_flag_options = HOWMANY(flag_options);

static const struct act_option act_options[] = {
	{ "H",         commandline_H },
	{ "M",         commandline_M },
	{ "MD",        commandline_MD },
	{ "MM",        commandline_MM },
	{ "MMD",       commandline_MMD },
	{ "Wall",      commandline_wall },
	{ "Wno-all",   commandline_wnoall },
	{ "dD",        commandline_dD },
	{ "dI",        commandline_dI },
	{ "dM",        commandline_dM },
	{ "dN",        commandline_dN },
	{ "w",         commandline_wnone },
};
static const unsigned num_act_options = HOWMANY(act_options);

static const struct prefix_option prefix_options[] = {
	{ "D",         commandline_def },
	{ "I",         commandline_addincpath_user },
	{ "U",         commandline_undef },
	{ "ftabstop=", commandline_tabstop },
	{ "std=",      commandline_setstd },
};
static const unsigned num_prefix_options = HOWMANY(prefix_options);

static const struct arg_option arg_options[] = {
	{ "MF",          commandline_setdependoutput },
	{ "MQ",          commandline_setdependtarget_quoted },
	{ "MT",          commandline_setdependtarget },
	{ "debuglog",    debuglog_open },
	{ "idirafter",   commandline_addincpath_late },
	{ "imacros",     commandline_addfile_nooutput },
	{ "include",     commandline_addfile_output },
	{ "iprefix",     commandline_setprefix },
	{ "iquote",      commandline_addincpath_quote },
	{ "iremap",      commandline_iremap },
	{ "isysroot",    commandline_isysroot },
	{ "isystem",     commandline_addincpath_system },
	{ "iwithprefix", commandline_addincpath_late_withprefix },
	{ "iwithprefixbefore", commandline_addincpath_user_withprefix },
	{ "x",           commandline_setlang },
};
static const unsigned num_arg_options = HOWMANY(arg_options);

static
bool
check_ignore_option(const char *opt)
{
	unsigned i;
	int r;

	for (i=0; i<num_ignore_options; i++) {
		r = strcmp(opt, ignore_options[i].string);
		if (r == 0) {
			return true;
		}
		if (r < 0) {
			break;
		}
	}
	return false;
}

static
bool
check_flag_option(const char *opt)
{
	unsigned i;
	int r;

	for (i=0; i<num_flag_options; i++) {
		r = strcmp(opt, flag_options[i].string);
		if (r == 0) {
			*flag_options[i].flag = flag_options[i].setto;
			return true;
		}
		if (r < 0) {
			break;
		}
	}
	return false;
}

static
bool
check_act_option(const char *opt)
{
	unsigned i;
	int r;

	for (i=0; i<num_act_options; i++) {
		r = strcmp(opt, act_options[i].string);
		if (r == 0) {
			act_options[i].func();
			return true;
		}
		if (r < 0) {
			break;
		}
	}
	return false;
}

static
bool
check_prefix_option(const struct place *p, char *opt)
{
	unsigned i, len;
	int r;

	for (i=0; i<num_prefix_options; i++) {
		len = strlen(prefix_options[i].string);
		r = strncmp(opt, prefix_options[i].string, len);
		if (r == 0) {
			prefix_options[i].func(p, opt + len);
			return true;
		}
		if (r < 0) {
			break;
		}
	}
	return false;
}

static
bool
check_arg_option(const char *opt, const struct place *argplace, char *arg)
{
	unsigned i;
	int r;

	for (i=0; i<num_arg_options; i++) {
		r = strcmp(opt, arg_options[i].string);
		if (r == 0) {
			if (arg == NULL) {
				complain(NULL,
					 "Option -%s requires an argument",
					 opt);
				die();
			}
			arg_options[i].func(argplace, arg);
			return true;
		}
		if (r < 0) {
			break;
		}
	}
	return false;
}

DEAD PF(2, 3) static
void
usage(const char *progname, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");

	fprintf(stderr, "usage: %s [options] [infile [outfile]]\n", progname);
	fprintf(stderr, "Common options:\n");
	fprintf(stderr, "   -C               Retain comments\n");
	fprintf(stderr, "   -Dmacro[=def]    Predefine macro\n");
	fprintf(stderr, "   -Idir            Add to include path\n");
	fprintf(stderr, "   -M               Issue depend info\n");
	fprintf(stderr, "   -MD              Issue depend info and output\n");
	fprintf(stderr, "   -MM              -M w/o system headers\n");
	fprintf(stderr, "   -MMD             -MD w/o system headers\n");
	fprintf(stderr, "   -nostdinc        Drop default include path\n");
	fprintf(stderr, "   -Umacro          Undefine macro\n");
	fprintf(stderr, "   -undef           Undefine everything\n");
	fprintf(stderr, "   -Wall            Enable all warnings\n");
	fprintf(stderr, "   -Werror          Make warnings into errors\n");
	fprintf(stderr, "   -w               Disable all warnings\n");
	die();
}

////////////////////////////////////////////////////////////
// exit and cleanup

static struct stringarray freestrings;

static
void
init(void)
{
	stringarray_init(&freestrings);

	incpath_init();
	commandline_macros_init();
	commandline_files_init();

	place_init();
	files_init();
	directive_init();
	macros_init();
}

static
void
cleanup(void)
{
	unsigned i, num;

	macros_cleanup();
	directive_cleanup();
	files_cleanup();
	place_cleanup();

	commandline_files_cleanup();
	commandline_macros_cleanup();
	incpath_cleanup();
	debuglog_close();

	num = stringarray_num(&freestrings);
	for (i=0; i<num; i++) {
		dostrfree(stringarray_get(&freestrings, i));
	}
	stringarray_setsize(&freestrings, 0);
	stringarray_cleanup(&freestrings);
}

void
die(void)
{
	cleanup();
	exit(EXIT_FAILURE);
}

void
freestringlater(char *s)
{
	stringarray_add(&freestrings, s, NULL);
}

////////////////////////////////////////////////////////////
// main

int
main(int argc, char *argv[])
{
	const char *progname;
	const char *inputfile = NULL;
	const char *outputfile = NULL;
	struct place cmdplace;
	int i;

	progname = strrchr(argv[0], '/');
	progname = progname == NULL ? argv[0] : progname + 1;
	complain_init(progname);

	if (pledge("stdio rpath wpath cpath", NULL) == -1) {
		fprintf(stderr, "%s: pledge: %s", progname, strerror(errno));
		exit(1);
	}

	init();

	for (i=1; i<argc; i++) {
		if (argv[i][0] != '-' || argv[i][1] == 0) {
			break;
		}
		place_setcommandline(&cmdplace, i, 1);
		if (check_ignore_option(argv[i]+1)) {
			continue;
		}
		if (check_flag_option(argv[i]+1)) {
			continue;
		}
		if (check_act_option(argv[i]+1)) {
			continue;
		}
		if (check_prefix_option(&cmdplace, argv[i]+1)) {
			continue;
		}
		place_setcommandline(&cmdplace, i+1, 1);
		if (check_arg_option(argv[i]+1, &cmdplace, argv[i+1])) {
			i++;
			continue;
		}
		usage(progname, "Invalid option %s", argv[i]);
	}
	if (i < argc) {
		inputfile = argv[i++];
		if (!strcmp(inputfile, "-")) {
			inputfile = NULL;
		}
	}
	if (i < argc) {
		outputfile = argv[i++];
		if (!strcmp(outputfile, "-")) {
			outputfile = NULL;
		}
	}
	if (i < argc) {
		usage(progname, "Extra non-option argument %s", argv[i]);
	}

	mode.output_file = outputfile;

	loadincludepath();
	apply_builtin_macros();
	apply_commandline_macros();
	read_commandline_files();
	place_setnowhere(&cmdplace);
	file_readabsolute(&cmdplace, inputfile);

	cleanup();
	if (complain_failed()) {
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
