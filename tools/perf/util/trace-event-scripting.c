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

#include "archinsn.h"
#include "debug.h"
#include "event.h"
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

#if !defined(__i386__) && !defined(__x86_64__)
void arch_fetch_insn(struct perf_sample *sample __maybe_unused,
		     struct thread *thread __maybe_unused,
		     struct machine *machine __maybe_unused)
{
}
#endif

void script_fetch_insn(struct perf_sample *sample, struct thread *thread,
		       struct machine *machine, bool native_arch)
{
	if (sample->insn_len == 0 && native_arch)
		arch_fetch_insn(sample, thread, machine);
}

static const struct {
	u32 flags;
	const char *name;
} sample_flags[] = {
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL, "call"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN, "return"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CONDITIONAL, "jcc"},
	{PERF_IP_FLAG_BRANCH, "jmp"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_INTERRUPT, "int"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN | PERF_IP_FLAG_INTERRUPT, "iret"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_SYSCALLRET, "syscall"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN | PERF_IP_FLAG_SYSCALLRET, "sysret"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_ASYNC, "async"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_ASYNC |	PERF_IP_FLAG_INTERRUPT,
	 "hw int"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TX_ABORT, "tx abrt"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TRACE_BEGIN, "tr strt"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TRACE_END, "tr end"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_VMENTRY, "vmentry"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL | PERF_IP_FLAG_VMEXIT, "vmexit"},
	{PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_BRANCH_MISS, "br miss"},
	{0, NULL}
};

static const char *sample_flags_to_name(u32 flags)
{
	int i;

	for (i = 0; sample_flags[i].name ; i++) {
		if (sample_flags[i].flags == flags)
			return sample_flags[i].name;
	}

	return NULL;
}

int perf_sample__sprintf_flags(u32 flags, char *str, size_t sz)
{
	u32 xf = PERF_IP_FLAG_IN_TX | PERF_IP_FLAG_INTR_DISABLE |
		 PERF_IP_FLAG_INTR_TOGGLE;
	const char *chars = PERF_IP_FLAG_CHARS;
	const size_t n = strlen(PERF_IP_FLAG_CHARS);
	const char *name = NULL;
	size_t i, pos = 0;
	char xs[16] = {0};

	if (flags & xf)
		snprintf(xs, sizeof(xs), "(%s%s%s)",
			 flags & PERF_IP_FLAG_IN_TX ? "x" : "",
			 flags & PERF_IP_FLAG_INTR_DISABLE ? "D" : "",
			 flags & PERF_IP_FLAG_INTR_TOGGLE ? "t" : "");

	name = sample_flags_to_name(flags & ~xf);
	if (name)
		return snprintf(str, sz, "%-15s%6s", name, xs);

	if (flags & PERF_IP_FLAG_TRACE_BEGIN) {
		name = sample_flags_to_name(flags & ~(xf | PERF_IP_FLAG_TRACE_BEGIN));
		if (name)
			return snprintf(str, sz, "tr strt %-7s%6s", name, xs);
	}

	if (flags & PERF_IP_FLAG_TRACE_END) {
		name = sample_flags_to_name(flags & ~(xf | PERF_IP_FLAG_TRACE_END));
		if (name)
			return snprintf(str, sz, "tr end  %-7s%6s", name, xs);
	}

	for (i = 0; i < n; i++, flags >>= 1) {
		if ((flags & 1) && pos < sz)
			str[pos++] = chars[i];
	}
	for (; i < 32; i++, flags >>= 1) {
		if ((flags & 1) && pos < sz)
			str[pos++] = '?';
	}
	if (pos < sz)
		str[pos] = 0;

	return pos;
}
