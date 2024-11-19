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
#ifdef HAVE_LIBTRACEEVENT
#include <event-parse.h>
#endif

#include "debug.h"
#include "trace-event.h"
#include "evsel.h"
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include "util/sample.h"

unsigned int scripting_max_stack = PERF_MAX_STACK_DEPTH;

struct scripting_context *scripting_context;

struct script_spec {
	struct list_head	node;
	struct scripting_ops	*ops;
	char			spec[];
};

static LIST_HEAD(script_specs);

static struct script_spec *script_spec__new(const char *spec,
					    struct scripting_ops *ops)
{
	struct script_spec *s = malloc(sizeof(*s) + strlen(spec) + 1);

	if (s != NULL) {
		strcpy(s->spec, spec);
		s->ops = ops;
	}

	return s;
}

static void script_spec__add(struct script_spec *s)
{
	list_add_tail(&s->node, &script_specs);
}

static struct script_spec *script_spec__find(const char *spec)
{
	struct script_spec *s;

	list_for_each_entry(s, &script_specs, node)
		if (strcasecmp(s->spec, spec) == 0)
			return s;
	return NULL;
}

static int script_spec_register(const char *spec, struct scripting_ops *ops)
{
	struct script_spec *s;

	s = script_spec__find(spec);
	if (s)
		return -1;

	s = script_spec__new(spec, ops);
	if (!s)
		return -1;

	script_spec__add(s);
	return 0;
}

struct scripting_ops *script_spec__lookup(const char *spec)
{
	struct script_spec *s = script_spec__find(spec);

	if (!s)
		return NULL;

	return s->ops;
}

int script_spec__for_each(int (*cb)(struct scripting_ops *ops, const char *spec))
{
	struct script_spec *s;
	int ret = 0;

	list_for_each_entry(s, &script_specs, node) {
		ret = cb(s->ops, s->spec);
		if (ret)
			break;
	}
	return ret;
}

void scripting_context__update(struct scripting_context *c,
			       union perf_event *event,
			       struct perf_sample *sample,
			       struct evsel *evsel,
			       struct addr_location *al,
			       struct addr_location *addr_al)
{
#ifdef HAVE_LIBTRACEEVENT
	const struct tep_event *tp_format = evsel__tp_format(evsel);

	c->pevent = tp_format ? tp_format->tep : NULL;
#else
	c->pevent = NULL;
#endif
	c->event_data = sample->raw_data;
	c->event = event;
	c->sample = sample;
	c->evsel = evsel;
	c->al = al;
	c->addr_al = addr_al;
}

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
				      struct evsel *evsel __maybe_unused,
				      struct addr_location *al __maybe_unused,
				      struct addr_location *addr_al __maybe_unused)
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
					   const char **argv __maybe_unused,
					   struct perf_session *session __maybe_unused)
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
	.dirname = "python",
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

#ifdef HAVE_LIBTRACEEVENT
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
					 const char **argv __maybe_unused,
					 struct perf_session *session __maybe_unused)
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
	.dirname = "perl",
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
#endif
