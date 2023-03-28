/*
 * trace-event-perl.  Feed perf script events to an embedded Perl interpreter.
 *
 * Copyright (C) 2009 Tom Zanussi <tzanussi@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <linux/bitmap.h>
#include <linux/time64.h>
#include <traceevent/event-parse.h>

#include <stdbool.h>
/* perl needs the following define, right after including stdbool.h */
#define HAS_BOOL
#include <EXTERN.h>
#include <perl.h>

#include "../callchain.h"
#include "../dso.h"
#include "../machine.h"
#include "../map.h"
#include "../symbol.h"
#include "../thread.h"
#include "../event.h"
#include "../trace-event.h"
#include "../evsel.h"
#include "../debug.h"

void boot_Perf__Trace__Context(pTHX_ CV *cv);
void boot_DynaLoader(pTHX_ CV *cv);
typedef PerlInterpreter * INTERP;

void xs_init(pTHX);

void xs_init(pTHX)
{
	const char *file = __FILE__;
	dXSUB_SYS;

	newXS("Perf::Trace::Context::bootstrap", boot_Perf__Trace__Context,
	      file);
	newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);
}

INTERP my_perl;

#define TRACE_EVENT_TYPE_MAX				\
	((1 << (sizeof(unsigned short) * 8)) - 1)

static DECLARE_BITMAP(events_defined, TRACE_EVENT_TYPE_MAX);

extern struct scripting_context *scripting_context;

static char *cur_field_name;
static int zero_flag_atom;

static void define_symbolic_value(const char *ev_name,
				  const char *field_name,
				  const char *field_value,
				  const char *field_str)
{
	unsigned long long value;
	dSP;

	value = eval_flag(field_value);

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs(sv_2mortal(newSVpv(ev_name, 0)));
	XPUSHs(sv_2mortal(newSVpv(field_name, 0)));
	XPUSHs(sv_2mortal(newSVuv(value)));
	XPUSHs(sv_2mortal(newSVpv(field_str, 0)));

	PUTBACK;
	if (get_cv("main::define_symbolic_value", 0))
		call_pv("main::define_symbolic_value", G_SCALAR);
	SPAGAIN;
	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void define_symbolic_values(struct tep_print_flag_sym *field,
				   const char *ev_name,
				   const char *field_name)
{
	define_symbolic_value(ev_name, field_name, field->value, field->str);
	if (field->next)
		define_symbolic_values(field->next, ev_name, field_name);
}

static void define_symbolic_field(const char *ev_name,
				  const char *field_name)
{
	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs(sv_2mortal(newSVpv(ev_name, 0)));
	XPUSHs(sv_2mortal(newSVpv(field_name, 0)));

	PUTBACK;
	if (get_cv("main::define_symbolic_field", 0))
		call_pv("main::define_symbolic_field", G_SCALAR);
	SPAGAIN;
	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void define_flag_value(const char *ev_name,
			      const char *field_name,
			      const char *field_value,
			      const char *field_str)
{
	unsigned long long value;
	dSP;

	value = eval_flag(field_value);

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs(sv_2mortal(newSVpv(ev_name, 0)));
	XPUSHs(sv_2mortal(newSVpv(field_name, 0)));
	XPUSHs(sv_2mortal(newSVuv(value)));
	XPUSHs(sv_2mortal(newSVpv(field_str, 0)));

	PUTBACK;
	if (get_cv("main::define_flag_value", 0))
		call_pv("main::define_flag_value", G_SCALAR);
	SPAGAIN;
	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void define_flag_values(struct tep_print_flag_sym *field,
			       const char *ev_name,
			       const char *field_name)
{
	define_flag_value(ev_name, field_name, field->value, field->str);
	if (field->next)
		define_flag_values(field->next, ev_name, field_name);
}

static void define_flag_field(const char *ev_name,
			      const char *field_name,
			      const char *delim)
{
	dSP;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs(sv_2mortal(newSVpv(ev_name, 0)));
	XPUSHs(sv_2mortal(newSVpv(field_name, 0)));
	XPUSHs(sv_2mortal(newSVpv(delim, 0)));

	PUTBACK;
	if (get_cv("main::define_flag_field", 0))
		call_pv("main::define_flag_field", G_SCALAR);
	SPAGAIN;
	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void define_event_symbols(struct tep_event *event,
				 const char *ev_name,
				 struct tep_print_arg *args)
{
	if (args == NULL)
		return;

	switch (args->type) {
	case TEP_PRINT_NULL:
		break;
	case TEP_PRINT_ATOM:
		define_flag_value(ev_name, cur_field_name, "0",
				  args->atom.atom);
		zero_flag_atom = 0;
		break;
	case TEP_PRINT_FIELD:
		free(cur_field_name);
		cur_field_name = strdup(args->field.name);
		break;
	case TEP_PRINT_FLAGS:
		define_event_symbols(event, ev_name, args->flags.field);
		define_flag_field(ev_name, cur_field_name, args->flags.delim);
		define_flag_values(args->flags.flags, ev_name, cur_field_name);
		break;
	case TEP_PRINT_SYMBOL:
		define_event_symbols(event, ev_name, args->symbol.field);
		define_symbolic_field(ev_name, cur_field_name);
		define_symbolic_values(args->symbol.symbols, ev_name,
				       cur_field_name);
		break;
	case TEP_PRINT_HEX:
	case TEP_PRINT_HEX_STR:
		define_event_symbols(event, ev_name, args->hex.field);
		define_event_symbols(event, ev_name, args->hex.size);
		break;
	case TEP_PRINT_INT_ARRAY:
		define_event_symbols(event, ev_name, args->int_array.field);
		define_event_symbols(event, ev_name, args->int_array.count);
		define_event_symbols(event, ev_name, args->int_array.el_size);
		break;
	case TEP_PRINT_BSTRING:
	case TEP_PRINT_DYNAMIC_ARRAY:
	case TEP_PRINT_DYNAMIC_ARRAY_LEN:
	case TEP_PRINT_STRING:
	case TEP_PRINT_BITMASK:
		break;
	case TEP_PRINT_TYPE:
		define_event_symbols(event, ev_name, args->typecast.item);
		break;
	case TEP_PRINT_OP:
		if (strcmp(args->op.op, ":") == 0)
			zero_flag_atom = 1;
		define_event_symbols(event, ev_name, args->op.left);
		define_event_symbols(event, ev_name, args->op.right);
		break;
	case TEP_PRINT_FUNC:
	default:
		pr_err("Unsupported print arg type\n");
		/* we should warn... */
		return;
	}

	if (args->next)
		define_event_symbols(event, ev_name, args->next);
}

static SV *perl_process_callchain(struct perf_sample *sample,
				  struct evsel *evsel,
				  struct addr_location *al)
{
	AV *list;

	list = newAV();
	if (!list)
		goto exit;

	if (!symbol_conf.use_callchain || !sample->callchain)
		goto exit;

	if (thread__resolve_callchain(al->thread, &callchain_cursor, evsel,
				      sample, NULL, NULL, scripting_max_stack) != 0) {
		pr_err("Failed to resolve callchain. Skipping\n");
		goto exit;
	}
	callchain_cursor_commit(&callchain_cursor);


	while (1) {
		HV *elem;
		struct callchain_cursor_node *node;
		node = callchain_cursor_current(&callchain_cursor);
		if (!node)
			break;

		elem = newHV();
		if (!elem)
			goto exit;

		if (!hv_stores(elem, "ip", newSVuv(node->ip))) {
			hv_undef(elem);
			goto exit;
		}

		if (node->ms.sym) {
			HV *sym = newHV();
			if (!sym) {
				hv_undef(elem);
				goto exit;
			}
			if (!hv_stores(sym, "start",   newSVuv(node->ms.sym->start)) ||
			    !hv_stores(sym, "end",     newSVuv(node->ms.sym->end)) ||
			    !hv_stores(sym, "binding", newSVuv(node->ms.sym->binding)) ||
			    !hv_stores(sym, "name",    newSVpvn(node->ms.sym->name,
								node->ms.sym->namelen)) ||
			    !hv_stores(elem, "sym",    newRV_noinc((SV*)sym))) {
				hv_undef(sym);
				hv_undef(elem);
				goto exit;
			}
		}

		if (node->ms.map) {
			struct map *map = node->ms.map;
			const char *dsoname = "[unknown]";
			if (map && map->dso) {
				if (symbol_conf.show_kernel_path && map->dso->long_name)
					dsoname = map->dso->long_name;
				else
					dsoname = map->dso->name;
			}
			if (!hv_stores(elem, "dso", newSVpv(dsoname,0))) {
				hv_undef(elem);
				goto exit;
			}
		}

		callchain_cursor_advance(&callchain_cursor);
		av_push(list, newRV_noinc((SV*)elem));
	}

exit:
	return newRV_noinc((SV*)list);
}

static void perl_process_tracepoint(struct perf_sample *sample,
				    struct evsel *evsel,
				    struct addr_location *al)
{
	struct thread *thread = al->thread;
	struct tep_event *event = evsel->tp_format;
	struct tep_format_field *field;
	static char handler[256];
	unsigned long long val;
	unsigned long s, ns;
	int pid;
	int cpu = sample->cpu;
	void *data = sample->raw_data;
	unsigned long long nsecs = sample->time;
	const char *comm = thread__comm_str(thread);

	dSP;

	if (evsel->core.attr.type != PERF_TYPE_TRACEPOINT)
		return;

	if (!event) {
		pr_debug("ug! no event found for type %" PRIu64, (u64)evsel->core.attr.config);
		return;
	}

	pid = raw_field_value(event, "common_pid", data);

	sprintf(handler, "%s::%s", event->system, event->name);

	if (!__test_and_set_bit(event->id, events_defined))
		define_event_symbols(event, handler, event->print_fmt.args);

	s = nsecs / NSEC_PER_SEC;
	ns = nsecs - s * NSEC_PER_SEC;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);

	XPUSHs(sv_2mortal(newSVpv(handler, 0)));
	XPUSHs(sv_2mortal(newSViv(PTR2IV(scripting_context))));
	XPUSHs(sv_2mortal(newSVuv(cpu)));
	XPUSHs(sv_2mortal(newSVuv(s)));
	XPUSHs(sv_2mortal(newSVuv(ns)));
	XPUSHs(sv_2mortal(newSViv(pid)));
	XPUSHs(sv_2mortal(newSVpv(comm, 0)));
	XPUSHs(sv_2mortal(perl_process_callchain(sample, evsel, al)));

	/* common fields other than pid can be accessed via xsub fns */

	for (field = event->format.fields; field; field = field->next) {
		if (field->flags & TEP_FIELD_IS_STRING) {
			int offset;
			if (field->flags & TEP_FIELD_IS_DYNAMIC) {
				offset = *(int *)(data + field->offset);
				offset &= 0xffff;
				if (tep_field_is_relative(field->flags))
					offset += field->offset + field->size;
			} else
				offset = field->offset;
			XPUSHs(sv_2mortal(newSVpv((char *)data + offset, 0)));
		} else { /* FIELD_IS_NUMERIC */
			val = read_size(event, data + field->offset,
					field->size);
			if (field->flags & TEP_FIELD_IS_SIGNED) {
				XPUSHs(sv_2mortal(newSViv(val)));
			} else {
				XPUSHs(sv_2mortal(newSVuv(val)));
			}
		}
	}

	PUTBACK;

	if (get_cv(handler, 0))
		call_pv(handler, G_SCALAR);
	else if (get_cv("main::trace_unhandled", 0)) {
		XPUSHs(sv_2mortal(newSVpv(handler, 0)));
		XPUSHs(sv_2mortal(newSViv(PTR2IV(scripting_context))));
		XPUSHs(sv_2mortal(newSVuv(cpu)));
		XPUSHs(sv_2mortal(newSVuv(nsecs)));
		XPUSHs(sv_2mortal(newSViv(pid)));
		XPUSHs(sv_2mortal(newSVpv(comm, 0)));
		XPUSHs(sv_2mortal(perl_process_callchain(sample, evsel, al)));
		call_pv("main::trace_unhandled", G_SCALAR);
	}
	SPAGAIN;
	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void perl_process_event_generic(union perf_event *event,
				       struct perf_sample *sample,
				       struct evsel *evsel)
{
	dSP;

	if (!get_cv("process_event", 0))
		return;

	ENTER;
	SAVETMPS;
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVpvn((const char *)event, event->header.size)));
	XPUSHs(sv_2mortal(newSVpvn((const char *)&evsel->core.attr, sizeof(evsel->core.attr))));
	XPUSHs(sv_2mortal(newSVpvn((const char *)sample, sizeof(*sample))));
	XPUSHs(sv_2mortal(newSVpvn((const char *)sample->raw_data, sample->raw_size)));
	PUTBACK;
	call_pv("process_event", G_SCALAR);
	SPAGAIN;
	PUTBACK;
	FREETMPS;
	LEAVE;
}

static void perl_process_event(union perf_event *event,
			       struct perf_sample *sample,
			       struct evsel *evsel,
			       struct addr_location *al,
			       struct addr_location *addr_al)
{
	scripting_context__update(scripting_context, event, sample, evsel, al, addr_al);
	perl_process_tracepoint(sample, evsel, al);
	perl_process_event_generic(event, sample, evsel);
}

static void run_start_sub(void)
{
	dSP; /* access to Perl stack */
	PUSHMARK(SP);

	if (get_cv("main::trace_begin", 0))
		call_pv("main::trace_begin", G_DISCARD | G_NOARGS);
}

/*
 * Start trace script
 */
static int perl_start_script(const char *script, int argc, const char **argv,
			     struct perf_session *session)
{
	const char **command_line;
	int i, err = 0;

	scripting_context->session = session;

	command_line = malloc((argc + 2) * sizeof(const char *));
	command_line[0] = "";
	command_line[1] = script;
	for (i = 2; i < argc + 2; i++)
		command_line[i] = argv[i - 2];

	my_perl = perl_alloc();
	perl_construct(my_perl);

	if (perl_parse(my_perl, xs_init, argc + 2, (char **)command_line,
		       (char **)NULL)) {
		err = -1;
		goto error;
	}

	if (perl_run(my_perl)) {
		err = -1;
		goto error;
	}

	if (SvTRUE(ERRSV)) {
		err = -1;
		goto error;
	}

	run_start_sub();

	free(command_line);
	return 0;
error:
	perl_free(my_perl);
	free(command_line);

	return err;
}

static int perl_flush_script(void)
{
	return 0;
}

/*
 * Stop trace script
 */
static int perl_stop_script(void)
{
	dSP; /* access to Perl stack */
	PUSHMARK(SP);

	if (get_cv("main::trace_end", 0))
		call_pv("main::trace_end", G_DISCARD | G_NOARGS);

	perl_destruct(my_perl);
	perl_free(my_perl);

	return 0;
}

static int perl_generate_script(struct tep_handle *pevent, const char *outfile)
{
	int i, not_first, count, nr_events;
	struct tep_event **all_events;
	struct tep_event *event = NULL;
	struct tep_format_field *f;
	char fname[PATH_MAX];
	FILE *ofp;

	sprintf(fname, "%s.pl", outfile);
	ofp = fopen(fname, "w");
	if (ofp == NULL) {
		fprintf(stderr, "couldn't open %s\n", fname);
		return -1;
	}

	fprintf(ofp, "# perf script event handlers, "
		"generated by perf script -g perl\n");

	fprintf(ofp, "# Licensed under the terms of the GNU GPL"
		" License version 2\n\n");

	fprintf(ofp, "# The common_* event handler fields are the most useful "
		"fields common to\n");

	fprintf(ofp, "# all events.  They don't necessarily correspond to "
		"the 'common_*' fields\n");

	fprintf(ofp, "# in the format files.  Those fields not available as "
		"handler params can\n");

	fprintf(ofp, "# be retrieved using Perl functions of the form "
		"common_*($context).\n");

	fprintf(ofp, "# See Context.pm for the list of available "
		"functions.\n\n");

	fprintf(ofp, "use lib \"$ENV{'PERF_EXEC_PATH'}/scripts/perl/"
		"Perf-Trace-Util/lib\";\n");

	fprintf(ofp, "use lib \"./Perf-Trace-Util/lib\";\n");
	fprintf(ofp, "use Perf::Trace::Core;\n");
	fprintf(ofp, "use Perf::Trace::Context;\n");
	fprintf(ofp, "use Perf::Trace::Util;\n\n");

	fprintf(ofp, "sub trace_begin\n{\n\t# optional\n}\n\n");
	fprintf(ofp, "sub trace_end\n{\n\t# optional\n}\n");


	fprintf(ofp, "\n\
sub print_backtrace\n\
{\n\
	my $callchain = shift;\n\
	for my $node (@$callchain)\n\
	{\n\
		if(exists $node->{sym})\n\
		{\n\
			printf( \"\\t[\\%%x] \\%%s\\n\", $node->{ip}, $node->{sym}{name});\n\
		}\n\
		else\n\
		{\n\
			printf( \"\\t[\\%%x]\\n\", $node{ip});\n\
		}\n\
	}\n\
}\n\n\
");

	nr_events = tep_get_events_count(pevent);
	all_events = tep_list_events(pevent, TEP_EVENT_SORT_ID);

	for (i = 0; all_events && i < nr_events; i++) {
		event = all_events[i];
		fprintf(ofp, "sub %s::%s\n{\n", event->system, event->name);
		fprintf(ofp, "\tmy (");

		fprintf(ofp, "$event_name, ");
		fprintf(ofp, "$context, ");
		fprintf(ofp, "$common_cpu, ");
		fprintf(ofp, "$common_secs, ");
		fprintf(ofp, "$common_nsecs,\n");
		fprintf(ofp, "\t    $common_pid, ");
		fprintf(ofp, "$common_comm, ");
		fprintf(ofp, "$common_callchain,\n\t    ");

		not_first = 0;
		count = 0;

		for (f = event->format.fields; f; f = f->next) {
			if (not_first++)
				fprintf(ofp, ", ");
			if (++count % 5 == 0)
				fprintf(ofp, "\n\t    ");

			fprintf(ofp, "$%s", f->name);
		}
		fprintf(ofp, ") = @_;\n\n");

		fprintf(ofp, "\tprint_header($event_name, $common_cpu, "
			"$common_secs, $common_nsecs,\n\t             "
			"$common_pid, $common_comm, $common_callchain);\n\n");

		fprintf(ofp, "\tprintf(\"");

		not_first = 0;
		count = 0;

		for (f = event->format.fields; f; f = f->next) {
			if (not_first++)
				fprintf(ofp, ", ");
			if (count && count % 4 == 0) {
				fprintf(ofp, "\".\n\t       \"");
			}
			count++;

			fprintf(ofp, "%s=", f->name);
			if (f->flags & TEP_FIELD_IS_STRING ||
			    f->flags & TEP_FIELD_IS_FLAG ||
			    f->flags & TEP_FIELD_IS_SYMBOLIC)
				fprintf(ofp, "%%s");
			else if (f->flags & TEP_FIELD_IS_SIGNED)
				fprintf(ofp, "%%d");
			else
				fprintf(ofp, "%%u");
		}

		fprintf(ofp, "\\n\",\n\t       ");

		not_first = 0;
		count = 0;

		for (f = event->format.fields; f; f = f->next) {
			if (not_first++)
				fprintf(ofp, ", ");

			if (++count % 5 == 0)
				fprintf(ofp, "\n\t       ");

			if (f->flags & TEP_FIELD_IS_FLAG) {
				if ((count - 1) % 5 != 0) {
					fprintf(ofp, "\n\t       ");
					count = 4;
				}
				fprintf(ofp, "flag_str(\"");
				fprintf(ofp, "%s::%s\", ", event->system,
					event->name);
				fprintf(ofp, "\"%s\", $%s)", f->name,
					f->name);
			} else if (f->flags & TEP_FIELD_IS_SYMBOLIC) {
				if ((count - 1) % 5 != 0) {
					fprintf(ofp, "\n\t       ");
					count = 4;
				}
				fprintf(ofp, "symbol_str(\"");
				fprintf(ofp, "%s::%s\", ", event->system,
					event->name);
				fprintf(ofp, "\"%s\", $%s)", f->name,
					f->name);
			} else
				fprintf(ofp, "$%s", f->name);
		}

		fprintf(ofp, ");\n\n");

		fprintf(ofp, "\tprint_backtrace($common_callchain);\n");

		fprintf(ofp, "}\n\n");
	}

	fprintf(ofp, "sub trace_unhandled\n{\n\tmy ($event_name, $context, "
		"$common_cpu, $common_secs, $common_nsecs,\n\t    "
		"$common_pid, $common_comm, $common_callchain) = @_;\n\n");

	fprintf(ofp, "\tprint_header($event_name, $common_cpu, "
		"$common_secs, $common_nsecs,\n\t             $common_pid, "
		"$common_comm, $common_callchain);\n");
	fprintf(ofp, "\tprint_backtrace($common_callchain);\n");
	fprintf(ofp, "}\n\n");

	fprintf(ofp, "sub print_header\n{\n"
		"\tmy ($event_name, $cpu, $secs, $nsecs, $pid, $comm) = @_;\n\n"
		"\tprintf(\"%%-20s %%5u %%05u.%%09u %%8u %%-20s \",\n\t       "
		"$event_name, $cpu, $secs, $nsecs, $pid, $comm);\n}\n");

	fprintf(ofp,
		"\n# Packed byte string args of process_event():\n"
		"#\n"
		"# $event:\tunion perf_event\tutil/event.h\n"
		"# $attr:\tstruct perf_event_attr\tlinux/perf_event.h\n"
		"# $sample:\tstruct perf_sample\tutil/event.h\n"
		"# $raw_data:\tperf_sample->raw_data\tutil/event.h\n"
		"\n"
		"sub process_event\n"
		"{\n"
		"\tmy ($event, $attr, $sample, $raw_data) = @_;\n"
		"\n"
		"\tmy @event\t= unpack(\"LSS\", $event);\n"
		"\tmy @attr\t= unpack(\"LLQQQQQLLQQ\", $attr);\n"
		"\tmy @sample\t= unpack(\"QLLQQQQQLL\", $sample);\n"
		"\tmy @raw_data\t= unpack(\"C*\", $raw_data);\n"
		"\n"
		"\tuse Data::Dumper;\n"
		"\tprint Dumper \\@event, \\@attr, \\@sample, \\@raw_data;\n"
		"}\n");

	fclose(ofp);

	fprintf(stderr, "generated Perl script: %s\n", fname);

	return 0;
}

struct scripting_ops perl_scripting_ops = {
	.name = "Perl",
	.dirname = "perl",
	.start_script = perl_start_script,
	.flush_script = perl_flush_script,
	.stop_script = perl_stop_script,
	.process_event = perl_process_event,
	.generate_script = perl_generate_script,
};
