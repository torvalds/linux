// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * trace-event-scripting.  Scripting engine common and initialization code.
 *
 * Copyright (C) 2009-2010 Tom Zanussi <tzanussi@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../perf.h"
#include "debug.h"
#include "util.h"
#include "trace-event.h"

struct scripting_context *scripting_context;

static int flush_script_unsupported(void)
{
	return 0;
}

static int stop_script_unsupported(void)
{
	return 0;
}

static void process_event_unsupported(union perf_event *event __maybe_unused,
				      struct perf_sample *sample __maybe_unused,
				      struct perf_evsel *evsel __maybe_unused,
				      struct addr_location *al __maybe_unused)
{
}

static void print_python_unsupported_msg(void)
{
	fprintf(stderr, "Python scripting not supported."
		"  Install libpython and rebuild perf to enable it.\n"
		"For example:\n  # apt-get install python-dev (ubuntu)"
		"\n  # yum install python-devel (Fedora)"
		"\n  etc.\n");
}

static int python_start_script_unsupported(const char *script __maybe_unused,
					   int argc __maybe_unused,
					   const char **argv __maybe_unused)
{
	print_python_unsupported_msg();

	return -1;
}

static int python_generate_script_unsupported(struct tep_handle *pevent
					      __maybe_unused,
					      const char *outfile
					      __maybe_unused)
{
	print_python_unsupported_msg();

	return -1;
}

struct scripting_ops python_scripting_unsupported_ops = {
	.name = "Python",
	.start_script = python_start_script_unsupported,
	.flush_script = flush_script_unsupported,
	.stop_script = stop_script_unsupported,
	.process_event = process_event_unsupported,
	.generate_script = python_generate_script_unsupported,
};

static void register_python_scripting(struct scripting_ops *scripting_ops)
{
	if (scripting_context == NULL)
		scripting_context = malloc(sizeof(*scripting_context));

       if (scripting_context == NULL ||
	   script_spec_register("Python", scripting_ops) ||
	   script_spec_register("py", scripting_ops)) {
		pr_err("Error registering Python script extension: disabling it\n");
		zfree(&scripting_context);
	}
}

#ifndef HAVE_LIBPYTHON_SUPPORT
void setup_python_scripting(void)
{
	register_python_scripting(&python_scripting_unsupported_ops);
}
#else
extern struct scripting_ops python_scripting_ops;

void setup_python_scripting(void)
{
	register_python_scripting(&python_scripting_ops);
}
#endif

static void print_perl_unsupported_msg(void)
{
	fprintf(stderr, "Perl scripting not supported."
		"  Install libperl and rebuild perf to enable it.\n"
		"For example:\n  # apt-get install libperl-dev (ubuntu)"
		"\n  # yum install 'perl(ExtUtils::Embed)' (Fedora)"
		"\n  etc.\n");
}

static int perl_start_script_unsupported(const char *script __maybe_unused,
					 int argc __maybe_unused,
					 const char **argv __maybe_unused)
{
	print_perl_unsupported_msg();

	return -1;
}

static int perl_generate_script_unsupported(struct tep_handle *pevent
					    __maybe_unused,
					    const char *outfile __maybe_unused)
{
	print_perl_unsupported_msg();

	return -1;
}

struct scripting_ops perl_scripting_unsupported_ops = {
	.name = "Perl",
	.start_script = perl_start_script_unsupported,
	.flush_script = flush_script_unsupported,
	.stop_script = stop_script_unsupported,
	.process_event = process_event_unsupported,
	.generate_script = perl_generate_script_unsupported,
};

static void register_perl_scripting(struct scripting_ops *scripting_ops)
{
	if (scripting_context == NULL)
		scripting_context = malloc(sizeof(*scripting_context));

       if (scripting_context == NULL ||
	   script_spec_register("Perl", scripting_ops) ||
	   script_spec_register("pl", scripting_ops)) {
		pr_err("Error registering Perl script extension: disabling it\n");
		zfree(&scripting_context);
	}
}

#ifndef HAVE_LIBPERL_SUPPORT
void setup_perl_scripting(void)
{
	register_perl_scripting(&perl_scripting_unsupported_ops);
}
#else
extern struct scripting_ops perl_scripting_ops;

void setup_perl_scripting(void)
{
	register_perl_scripting(&perl_scripting_ops);
}
#endif
