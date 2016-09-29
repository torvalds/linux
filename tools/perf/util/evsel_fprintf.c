#include <stdio.h>
#include <stdbool.h>
#include <traceevent/event-parse.h>
#include "evsel.h"
#include "callchain.h"
#include "map.h"
#include "symbol.h"

static int comma_fprintf(FILE *fp, bool *first, const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (!*first) {
		ret += fprintf(fp, ",");
	} else {
		ret += fprintf(fp, ":");
		*first = false;
	}

	va_start(args, fmt);
	ret += vfprintf(fp, fmt, args);
	va_end(args);
	return ret;
}

static int __print_attr__fprintf(FILE *fp, const char *name, const char *val, void *priv)
{
	return comma_fprintf(fp, (bool *)priv, " %s: %s", name, val);
}

int perf_evsel__fprintf(struct perf_evsel *evsel,
			struct perf_attr_details *details, FILE *fp)
{
	bool first = true;
	int printed = 0;

	if (details->event_group) {
		struct perf_evsel *pos;

		if (!perf_evsel__is_group_leader(evsel))
			return 0;

		if (evsel->nr_members > 1)
			printed += fprintf(fp, "%s{", evsel->group_name ?: "");

		printed += fprintf(fp, "%s", perf_evsel__name(evsel));
		for_each_group_member(pos, evsel)
			printed += fprintf(fp, ",%s", perf_evsel__name(pos));

		if (evsel->nr_members > 1)
			printed += fprintf(fp, "}");
		goto out;
	}

	printed += fprintf(fp, "%s", perf_evsel__name(evsel));

	if (details->verbose) {
		printed += perf_event_attr__fprintf(fp, &evsel->attr,
						    __print_attr__fprintf, &first);
	} else if (details->freq) {
		const char *term = "sample_freq";

		if (!evsel->attr.freq)
			term = "sample_period";

		printed += comma_fprintf(fp, &first, " %s=%" PRIu64,
					 term, (u64)evsel->attr.sample_freq);
	}

	if (details->trace_fields) {
		struct format_field *field;

		if (evsel->attr.type != PERF_TYPE_TRACEPOINT) {
			printed += comma_fprintf(fp, &first, " (not a tracepoint)");
			goto out;
		}

		field = evsel->tp_format->format.fields;
		if (field == NULL) {
			printed += comma_fprintf(fp, &first, " (no trace field)");
			goto out;
		}

		printed += comma_fprintf(fp, &first, " trace_fields: %s", field->name);

		field = field->next;
		while (field) {
			printed += comma_fprintf(fp, &first, "%s", field->name);
			field = field->next;
		}
	}
out:
	fputc('\n', fp);
	return ++printed;
}

int sample__fprintf_callchain(struct perf_sample *sample, int left_alignment,
			      unsigned int print_opts, struct callchain_cursor *cursor,
			      FILE *fp)
{
	int printed = 0;
	struct callchain_cursor_node *node;
	int print_ip = print_opts & EVSEL__PRINT_IP;
	int print_sym = print_opts & EVSEL__PRINT_SYM;
	int print_dso = print_opts & EVSEL__PRINT_DSO;
	int print_symoffset = print_opts & EVSEL__PRINT_SYMOFFSET;
	int print_oneline = print_opts & EVSEL__PRINT_ONELINE;
	int print_srcline = print_opts & EVSEL__PRINT_SRCLINE;
	int print_unknown_as_addr = print_opts & EVSEL__PRINT_UNKNOWN_AS_ADDR;
	char s = print_oneline ? ' ' : '\t';

	if (sample->callchain) {
		struct addr_location node_al;

		callchain_cursor_commit(cursor);

		while (1) {
			u64 addr = 0;

			node = callchain_cursor_current(cursor);
			if (!node)
				break;

			printed += fprintf(fp, "%-*.*s", left_alignment, left_alignment, " ");

			if (print_ip)
				printed += fprintf(fp, "%c%16" PRIx64, s, node->ip);

			if (node->map)
				addr = node->map->map_ip(node->map, node->ip);

			if (print_sym) {
				printed += fprintf(fp, " ");
				node_al.addr = addr;
				node_al.map  = node->map;

				if (print_symoffset) {
					printed += __symbol__fprintf_symname_offs(node->sym, &node_al,
										  print_unknown_as_addr, fp);
				} else {
					printed += __symbol__fprintf_symname(node->sym, &node_al,
									     print_unknown_as_addr, fp);
				}
			}

			if (print_dso) {
				printed += fprintf(fp, " (");
				printed += map__fprintf_dsoname(node->map, fp);
				printed += fprintf(fp, ")");
			}

			if (print_srcline)
				printed += map__fprintf_srcline(node->map, addr, "\n  ", fp);

			if (!print_oneline)
				printed += fprintf(fp, "\n");

			callchain_cursor_advance(cursor);
		}
	}

	return printed;
}

int sample__fprintf_sym(struct perf_sample *sample, struct addr_location *al,
			int left_alignment, unsigned int print_opts,
			struct callchain_cursor *cursor, FILE *fp)
{
	int printed = 0;
	int print_ip = print_opts & EVSEL__PRINT_IP;
	int print_sym = print_opts & EVSEL__PRINT_SYM;
	int print_dso = print_opts & EVSEL__PRINT_DSO;
	int print_symoffset = print_opts & EVSEL__PRINT_SYMOFFSET;
	int print_srcline = print_opts & EVSEL__PRINT_SRCLINE;
	int print_unknown_as_addr = print_opts & EVSEL__PRINT_UNKNOWN_AS_ADDR;

	if (cursor != NULL) {
		printed += sample__fprintf_callchain(sample, left_alignment,
						     print_opts, cursor, fp);
	} else {
		printed += fprintf(fp, "%-*.*s", left_alignment, left_alignment, " ");

		if (print_ip)
			printed += fprintf(fp, "%16" PRIx64, sample->ip);

		if (print_sym) {
			printed += fprintf(fp, " ");
			if (print_symoffset) {
				printed += __symbol__fprintf_symname_offs(al->sym, al,
									  print_unknown_as_addr, fp);
			} else {
				printed += __symbol__fprintf_symname(al->sym, al,
								     print_unknown_as_addr, fp);
			}
		}

		if (print_dso) {
			printed += fprintf(fp, " (");
			printed += map__fprintf_dsoname(al->map, fp);
			printed += fprintf(fp, ")");
		}

		if (print_srcline)
			printed += map__fprintf_srcline(al->map, al->addr, "\n  ", fp);
	}

	return printed;
}
