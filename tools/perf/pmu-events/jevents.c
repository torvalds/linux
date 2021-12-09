#define  _XOPEN_SOURCE 500	/* needed for nftw() */
#define  _GNU_SOURCE		/* needed for asprintf() */

/* Parse event JSON files */

/*
 * Copyright (c) 2014, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <libgen.h>
#include <limits.h>
#include <dirent.h>
#include <sys/time.h>			/* getrlimit */
#include <sys/resource.h>		/* getrlimit */
#include <ftw.h>
#include <sys/stat.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include "jsmn.h"
#include "json.h"
#include "pmu-events.h"

int verbose;
char *prog;

struct json_event {
	char *name;
	char *compat;
	char *event;
	char *desc;
	char *long_desc;
	char *pmu;
	char *unit;
	char *perpkg;
	char *aggr_mode;
	char *metric_expr;
	char *metric_name;
	char *metric_group;
	char *deprecated;
	char *metric_constraint;
};

static enum aggr_mode_class convert(const char *aggr_mode)
{
	if (!strcmp(aggr_mode, "PerCore"))
		return PerCore;
	else if (!strcmp(aggr_mode, "PerChip"))
		return PerChip;

	pr_err("%s: Wrong AggregationMode value '%s'\n", prog, aggr_mode);
	return -1;
}

static LIST_HEAD(sys_event_tables);

struct sys_event_table {
	struct list_head list;
	char *soc_id;
};

static void free_sys_event_tables(void)
{
	struct sys_event_table *et, *next;

	list_for_each_entry_safe(et, next, &sys_event_tables, list) {
		free(et->soc_id);
		free(et);
	}
}

int eprintf(int level, int var, const char *fmt, ...)
{

	int ret;
	va_list args;

	if (var < level)
		return 0;

	va_start(args, fmt);

	ret = vfprintf(stderr, fmt, args);

	va_end(args);

	return ret;
}

static void addfield(char *map, char **dst, const char *sep,
		     const char *a, jsmntok_t *bt)
{
	unsigned int len = strlen(a) + 1 + strlen(sep);
	int olen = *dst ? strlen(*dst) : 0;
	int blen = bt ? json_len(bt) : 0;
	char *out;

	out = realloc(*dst, len + olen + blen);
	if (!out) {
		/* Don't add field in this case */
		return;
	}
	*dst = out;

	if (!olen)
		*(*dst) = 0;
	else
		strcat(*dst, sep);
	strcat(*dst, a);
	if (bt)
		strncat(*dst, map + bt->start, blen);
}

static void fixname(char *s)
{
	for (; *s; s++)
		*s = tolower(*s);
}

static void fixdesc(char *s)
{
	char *e = s + strlen(s);

	/* Remove trailing dots that look ugly in perf list */
	--e;
	while (e >= s && isspace(*e))
		--e;
	if (*e == '.')
		*e = 0;
}

/* Add escapes for '\' so they are proper C strings. */
static char *fixregex(char *s)
{
	int len = 0;
	int esc_count = 0;
	char *fixed = NULL;
	char *p, *q;

	/* Count the number of '\' in string */
	for (p = s; *p; p++) {
		++len;
		if (*p == '\\')
			++esc_count;
	}

	if (esc_count == 0)
		return s;

	/* allocate space for a new string */
	fixed = (char *) malloc(len + esc_count + 1);
	if (!fixed)
		return NULL;

	/* copy over the characters */
	q = fixed;
	for (p = s; *p; p++) {
		if (*p == '\\') {
			*q = '\\';
			++q;
		}
		*q = *p;
		++q;
	}
	*q = '\0';
	return fixed;
}

static struct msrmap {
	const char *num;
	const char *pname;
} msrmap[] = {
	{ "0x3F6", "ldlat=" },
	{ "0x1A6", "offcore_rsp=" },
	{ "0x1A7", "offcore_rsp=" },
	{ "0x3F7", "frontend=" },
	{ NULL, NULL }
};

static struct field {
	const char *field;
	const char *kernel;
} fields[] = {
	{ "UMask",	"umask=" },
	{ "CounterMask", "cmask=" },
	{ "Invert",	"inv=" },
	{ "AnyThread",	"any=" },
	{ "EdgeDetect",	"edge=" },
	{ "SampleAfterValue", "period=" },
	{ "FCMask",	"fc_mask=" },
	{ "PortMask",	"ch_mask=" },
	{ NULL, NULL }
};

static void cut_comma(char *map, jsmntok_t *newval)
{
	int i;

	/* Cut off everything after comma */
	for (i = newval->start; i < newval->end; i++) {
		if (map[i] == ',')
			newval->end = i;
	}
}

static int match_field(char *map, jsmntok_t *field, int nz,
		       char **event, jsmntok_t *val)
{
	struct field *f;
	jsmntok_t newval = *val;

	for (f = fields; f->field; f++)
		if (json_streq(map, field, f->field) && nz) {
			cut_comma(map, &newval);
			addfield(map, event, ",", f->kernel, &newval);
			return 1;
		}
	return 0;
}

static struct msrmap *lookup_msr(char *map, jsmntok_t *val)
{
	jsmntok_t newval = *val;
	static bool warned;
	int i;

	cut_comma(map, &newval);
	for (i = 0; msrmap[i].num; i++)
		if (json_streq(map, &newval, msrmap[i].num))
			return &msrmap[i];
	if (!warned) {
		warned = true;
		pr_err("%s: Unknown MSR in event file %.*s\n", prog,
			json_len(val), map + val->start);
	}
	return NULL;
}

static struct map {
	const char *json;
	const char *perf;
} unit_to_pmu[] = {
	{ "CBO", "uncore_cbox" },
	{ "QPI LL", "uncore_qpi" },
	{ "SBO", "uncore_sbox" },
	{ "iMPH-U", "uncore_arb" },
	{ "CPU-M-CF", "cpum_cf" },
	{ "CPU-M-SF", "cpum_sf" },
	{ "UPI LL", "uncore_upi" },
	{ "hisi_sccl,ddrc", "hisi_sccl,ddrc" },
	{ "hisi_sccl,hha", "hisi_sccl,hha" },
	{ "hisi_sccl,l3c", "hisi_sccl,l3c" },
	/* it's not realistic to keep adding these, we need something more scalable ... */
	{ "imx8_ddr", "imx8_ddr" },
	{ "L3PMC", "amd_l3" },
	{ "DFPMC", "amd_df" },
	{ "cpu_core", "cpu_core" },
	{ "cpu_atom", "cpu_atom" },
	{}
};

static const char *field_to_perf(struct map *table, char *map, jsmntok_t *val)
{
	int i;

	for (i = 0; table[i].json; i++) {
		if (json_streq(map, val, table[i].json))
			return table[i].perf;
	}
	return NULL;
}

#define EXPECT(e, t, m) do { if (!(e)) {			\
	jsmntok_t *loc = (t);					\
	if (!(t)->start && (t) > tokens)			\
		loc = (t) - 1;					\
	pr_err("%s:%d: " m ", got %s\n", fn,			\
	       json_line(map, loc),				\
	       json_name(t));					\
	err = -EIO;						\
	goto out_free;						\
} } while (0)

static char *topic;

static char *get_topic(void)
{
	char *tp;
	int i;

	/* tp is free'd in process_one_file() */
	i = asprintf(&tp, "%s", topic);
	if (i < 0) {
		pr_info("%s: asprintf() error %s\n", prog);
		return NULL;
	}

	for (i = 0; i < (int) strlen(tp); i++) {
		char c = tp[i];

		if (c == '-')
			tp[i] = ' ';
		else if (c == '.') {
			tp[i] = '\0';
			break;
		}
	}

	return tp;
}

static int add_topic(char *bname)
{
	free(topic);
	topic = strdup(bname);
	if (!topic) {
		pr_info("%s: strdup() error %s for file %s\n", prog,
				strerror(errno), bname);
		return -ENOMEM;
	}
	return 0;
}

struct perf_entry_data {
	FILE *outfp;
	char *topic;
};

static int close_table;

static void print_events_table_prefix(FILE *fp, const char *tblname)
{
	fprintf(fp, "static const struct pmu_event %s[] = {\n", tblname);
	close_table = 1;
}

static int print_events_table_entry(void *data, struct json_event *je)
{
	struct perf_entry_data *pd = data;
	FILE *outfp = pd->outfp;
	char *topic_local = pd->topic;

	/*
	 * TODO: Remove formatting chars after debugging to reduce
	 *	 string lengths.
	 */
	fprintf(outfp, "{\n");

	if (je->name)
		fprintf(outfp, "\t.name = \"%s\",\n", je->name);
	if (je->event)
		fprintf(outfp, "\t.event = \"%s\",\n", je->event);
	fprintf(outfp, "\t.desc = \"%s\",\n", je->desc);
	if (je->compat)
		fprintf(outfp, "\t.compat = \"%s\",\n", je->compat);
	fprintf(outfp, "\t.topic = \"%s\",\n", topic_local);
	if (je->long_desc && je->long_desc[0])
		fprintf(outfp, "\t.long_desc = \"%s\",\n", je->long_desc);
	if (je->pmu)
		fprintf(outfp, "\t.pmu = \"%s\",\n", je->pmu);
	if (je->unit)
		fprintf(outfp, "\t.unit = \"%s\",\n", je->unit);
	if (je->perpkg)
		fprintf(outfp, "\t.perpkg = \"%s\",\n", je->perpkg);
	if (je->aggr_mode)
		fprintf(outfp, "\t.aggr_mode = \"%d\",\n", convert(je->aggr_mode));
	if (je->metric_expr)
		fprintf(outfp, "\t.metric_expr = \"%s\",\n", je->metric_expr);
	if (je->metric_name)
		fprintf(outfp, "\t.metric_name = \"%s\",\n", je->metric_name);
	if (je->metric_group)
		fprintf(outfp, "\t.metric_group = \"%s\",\n", je->metric_group);
	if (je->deprecated)
		fprintf(outfp, "\t.deprecated = \"%s\",\n", je->deprecated);
	if (je->metric_constraint)
		fprintf(outfp, "\t.metric_constraint = \"%s\",\n", je->metric_constraint);
	fprintf(outfp, "},\n");

	return 0;
}

struct event_struct {
	struct list_head list;
	char *name;
	char *event;
	char *compat;
	char *desc;
	char *long_desc;
	char *pmu;
	char *unit;
	char *perpkg;
	char *aggr_mode;
	char *metric_expr;
	char *metric_name;
	char *metric_group;
	char *deprecated;
	char *metric_constraint;
};

#define ADD_EVENT_FIELD(field) do { if (je->field) {		\
	es->field = strdup(je->field);				\
	if (!es->field)						\
		goto out_free;					\
} } while (0)

#define FREE_EVENT_FIELD(field) free(es->field)

#define TRY_FIXUP_FIELD(field) do { if (es->field && !je->field) {\
	je->field = strdup(es->field);				\
	if (!je->field)						\
		return -ENOMEM;					\
} } while (0)

#define FOR_ALL_EVENT_STRUCT_FIELDS(op) do {			\
	op(name);						\
	op(event);						\
	op(desc);						\
	op(long_desc);						\
	op(pmu);						\
	op(unit);						\
	op(perpkg);						\
	op(aggr_mode);						\
	op(metric_expr);					\
	op(metric_name);					\
	op(metric_group);					\
	op(deprecated);						\
} while (0)

static LIST_HEAD(arch_std_events);

static void free_arch_std_events(void)
{
	struct event_struct *es, *next;

	list_for_each_entry_safe(es, next, &arch_std_events, list) {
		FOR_ALL_EVENT_STRUCT_FIELDS(FREE_EVENT_FIELD);
		list_del_init(&es->list);
		free(es);
	}
}

static int save_arch_std_events(void *data __maybe_unused, struct json_event *je)
{
	struct event_struct *es;

	es = malloc(sizeof(*es));
	if (!es)
		return -ENOMEM;
	memset(es, 0, sizeof(*es));
	FOR_ALL_EVENT_STRUCT_FIELDS(ADD_EVENT_FIELD);
	list_add_tail(&es->list, &arch_std_events);
	return 0;
out_free:
	FOR_ALL_EVENT_STRUCT_FIELDS(FREE_EVENT_FIELD);
	free(es);
	return -ENOMEM;
}

static void print_events_table_suffix(FILE *outfp)
{
	fprintf(outfp, "{\n");

	fprintf(outfp, "\t.name = 0,\n");
	fprintf(outfp, "\t.event = 0,\n");
	fprintf(outfp, "\t.desc = 0,\n");

	fprintf(outfp, "},\n");
	fprintf(outfp, "};\n");
	close_table = 0;
}

static struct fixed {
	const char *name;
	const char *event;
} fixed[] = {
	{ "inst_retired.any", "event=0xc0,period=2000003" },
	{ "inst_retired.any_p", "event=0xc0,period=2000003" },
	{ "cpu_clk_unhalted.ref", "event=0x0,umask=0x03,period=2000003" },
	{ "cpu_clk_unhalted.thread", "event=0x3c,period=2000003" },
	{ "cpu_clk_unhalted.core", "event=0x3c,period=2000003" },
	{ "cpu_clk_unhalted.thread_any", "event=0x3c,any=1,period=2000003" },
	{ NULL, NULL},
};

/*
 * Handle different fixed counter encodings between JSON and perf.
 */
static char *real_event(const char *name, char *event)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; fixed[i].name; i++)
		if (!strcasecmp(name, fixed[i].name))
			return (char *)fixed[i].event;
	return event;
}

static int
try_fixup(const char *fn, char *arch_std, struct json_event *je, char **event)
{
	/* try to find matching event from arch standard values */
	struct event_struct *es;

	list_for_each_entry(es, &arch_std_events, list) {
		if (!strcmp(arch_std, es->name)) {
			FOR_ALL_EVENT_STRUCT_FIELDS(TRY_FIXUP_FIELD);
			*event = je->event;
			return 0;
		}
	}

	pr_err("%s: could not find matching %s for %s\n",
					prog, arch_std, fn);
	return -1;
}

/* Call func with each event in the json file */
static int json_events(const char *fn,
		int (*func)(void *data, struct json_event *je),
			void *data)
{
	int err;
	size_t size;
	jsmntok_t *tokens, *tok;
	int i, j, len;
	char *map;
	char buf[128];

	if (!fn)
		return -ENOENT;

	tokens = parse_json(fn, &map, &size, &len);
	if (!tokens)
		return -EIO;
	EXPECT(tokens->type == JSMN_ARRAY, tokens, "expected top level array");
	tok = tokens + 1;
	for (i = 0; i < tokens->size; i++) {
		char *event = NULL;
		char *extra_desc = NULL;
		char *filter = NULL;
		struct json_event je = {};
		char *arch_std = NULL;
		unsigned long long eventcode = 0;
		unsigned long long configcode = 0;
		struct msrmap *msr = NULL;
		jsmntok_t *msrval = NULL;
		jsmntok_t *precise = NULL;
		jsmntok_t *obj = tok++;
		bool configcode_present = false;

		EXPECT(obj->type == JSMN_OBJECT, obj, "expected object");
		for (j = 0; j < obj->size; j += 2) {
			jsmntok_t *field, *val;
			int nz;
			char *s;

			field = tok + j;
			EXPECT(field->type == JSMN_STRING, tok + j,
			       "Expected field name");
			val = tok + j + 1;
			EXPECT(val->type == JSMN_STRING, tok + j + 1,
			       "Expected string value");

			nz = !json_streq(map, val, "0");
			if (match_field(map, field, nz, &event, val)) {
				/* ok */
			} else if (json_streq(map, field, "EventCode")) {
				char *code = NULL;
				addfield(map, &code, "", "", val);
				eventcode |= strtoul(code, NULL, 0);
				free(code);
			} else if (json_streq(map, field, "ConfigCode")) {
				char *code = NULL;
				addfield(map, &code, "", "", val);
				configcode |= strtoul(code, NULL, 0);
				free(code);
				configcode_present = true;
			} else if (json_streq(map, field, "ExtSel")) {
				char *code = NULL;
				addfield(map, &code, "", "", val);
				eventcode |= strtoul(code, NULL, 0) << 21;
				free(code);
			} else if (json_streq(map, field, "EventName")) {
				addfield(map, &je.name, "", "", val);
			} else if (json_streq(map, field, "Compat")) {
				addfield(map, &je.compat, "", "", val);
			} else if (json_streq(map, field, "BriefDescription")) {
				addfield(map, &je.desc, "", "", val);
				fixdesc(je.desc);
			} else if (json_streq(map, field,
					     "PublicDescription")) {
				addfield(map, &je.long_desc, "", "", val);
				fixdesc(je.long_desc);
			} else if (json_streq(map, field, "PEBS") && nz) {
				precise = val;
			} else if (json_streq(map, field, "MSRIndex") && nz) {
				msr = lookup_msr(map, val);
			} else if (json_streq(map, field, "MSRValue")) {
				msrval = val;
			} else if (json_streq(map, field, "Errata") &&
				   !json_streq(map, val, "null")) {
				addfield(map, &extra_desc, ". ",
					" Spec update: ", val);
			} else if (json_streq(map, field, "Data_LA") && nz) {
				addfield(map, &extra_desc, ". ",
					" Supports address when precise",
					NULL);
			} else if (json_streq(map, field, "Unit")) {
				const char *ppmu;

				ppmu = field_to_perf(unit_to_pmu, map, val);
				if (ppmu) {
					je.pmu = strdup(ppmu);
				} else {
					if (!je.pmu)
						je.pmu = strdup("uncore_");
					addfield(map, &je.pmu, "", "", val);
					for (s = je.pmu; *s; s++)
						*s = tolower(*s);
				}
				addfield(map, &je.desc, ". ", "Unit: ", NULL);
				addfield(map, &je.desc, "", je.pmu, NULL);
				addfield(map, &je.desc, "", " ", NULL);
			} else if (json_streq(map, field, "Filter")) {
				addfield(map, &filter, "", "", val);
			} else if (json_streq(map, field, "ScaleUnit")) {
				addfield(map, &je.unit, "", "", val);
			} else if (json_streq(map, field, "PerPkg")) {
				addfield(map, &je.perpkg, "", "", val);
			} else if (json_streq(map, field, "AggregationMode")) {
				addfield(map, &je.aggr_mode, "", "", val);
			} else if (json_streq(map, field, "Deprecated")) {
				addfield(map, &je.deprecated, "", "", val);
			} else if (json_streq(map, field, "MetricName")) {
				addfield(map, &je.metric_name, "", "", val);
			} else if (json_streq(map, field, "MetricGroup")) {
				addfield(map, &je.metric_group, "", "", val);
			} else if (json_streq(map, field, "MetricConstraint")) {
				addfield(map, &je.metric_constraint, "", "", val);
			} else if (json_streq(map, field, "MetricExpr")) {
				addfield(map, &je.metric_expr, "", "", val);
				for (s = je.metric_expr; *s; s++)
					*s = tolower(*s);
			} else if (json_streq(map, field, "ArchStdEvent")) {
				addfield(map, &arch_std, "", "", val);
				for (s = arch_std; *s; s++)
					*s = tolower(*s);
			}
			/* ignore unknown fields */
		}
		if (precise && je.desc && !strstr(je.desc, "(Precise Event)")) {
			if (json_streq(map, precise, "2"))
				addfield(map, &extra_desc, " ",
						"(Must be precise)", NULL);
			else
				addfield(map, &extra_desc, " ",
						"(Precise event)", NULL);
		}
		if (configcode_present)
			snprintf(buf, sizeof buf, "config=%#llx", configcode);
		else
			snprintf(buf, sizeof buf, "event=%#llx", eventcode);
		addfield(map, &event, ",", buf, NULL);
		if (je.desc && extra_desc)
			addfield(map, &je.desc, " ", extra_desc, NULL);
		if (je.long_desc && extra_desc)
			addfield(map, &je.long_desc, " ", extra_desc, NULL);
		if (filter)
			addfield(map, &event, ",", filter, NULL);
		if (msr != NULL)
			addfield(map, &event, ",", msr->pname, msrval);
		if (je.name)
			fixname(je.name);

		if (arch_std) {
			/*
			 * An arch standard event is referenced, so try to
			 * fixup any unassigned values.
			 */
			err = try_fixup(fn, arch_std, &je, &event);
			if (err)
				goto free_strings;
		}
		je.event = real_event(je.name, event);
		err = func(data, &je);
free_strings:
		free(event);
		free(je.desc);
		free(je.name);
		free(je.compat);
		free(je.long_desc);
		free(extra_desc);
		free(je.pmu);
		free(filter);
		free(je.perpkg);
		free(je.aggr_mode);
		free(je.deprecated);
		free(je.unit);
		free(je.metric_expr);
		free(je.metric_name);
		free(je.metric_group);
		free(je.metric_constraint);
		free(arch_std);

		if (err)
			break;
		tok += j;
	}
	EXPECT(tok - tokens == len, tok, "unexpected objects at end");
	err = 0;
out_free:
	free_json(map, size, tokens);
	return err;
}

static char *file_name_to_table_name(char *fname)
{
	unsigned int i;
	int n;
	int c;
	char *tblname;

	/*
	 * Ensure tablename starts with alphabetic character.
	 * Derive rest of table name from basename of the JSON file,
	 * replacing hyphens and stripping out .json suffix.
	 */
	n = asprintf(&tblname, "pme_%s", fname);
	if (n < 0) {
		pr_info("%s: asprintf() error %s for file %s\n", prog,
				strerror(errno), fname);
		return NULL;
	}

	for (i = 0; i < strlen(tblname); i++) {
		c = tblname[i];

		if (c == '-' || c == '/')
			tblname[i] = '_';
		else if (c == '.') {
			tblname[i] = '\0';
			break;
		} else if (!isalnum(c) && c != '_') {
			pr_err("%s: Invalid character '%c' in file name %s\n",
					prog, c, basename(fname));
			free(tblname);
			tblname = NULL;
			break;
		}
	}

	return tblname;
}

static bool is_sys_dir(char *fname)
{
	size_t len = strlen(fname), len2 = strlen("/sys");

	if (len2 > len)
		return false;
	return !strcmp(fname+len-len2, "/sys");
}

static void print_mapping_table_prefix(FILE *outfp)
{
	fprintf(outfp, "const struct pmu_events_map pmu_events_map[] = {\n");
}

static void print_mapping_table_suffix(FILE *outfp)
{
	/*
	 * Print the terminating, NULL entry.
	 */
	fprintf(outfp, "{\n");
	fprintf(outfp, "\t.cpuid = 0,\n");
	fprintf(outfp, "\t.version = 0,\n");
	fprintf(outfp, "\t.type = 0,\n");
	fprintf(outfp, "\t.table = 0,\n");
	fprintf(outfp, "},\n");

	/* and finally, the closing curly bracket for the struct */
	fprintf(outfp, "};\n");
}

static void print_mapping_test_table(FILE *outfp)
{
	/*
	 * Print the terminating, NULL entry.
	 */
	fprintf(outfp, "{\n");
	fprintf(outfp, "\t.cpuid = \"testcpu\",\n");
	fprintf(outfp, "\t.version = \"v1\",\n");
	fprintf(outfp, "\t.type = \"core\",\n");
	fprintf(outfp, "\t.table = pme_test_soc_cpu,\n");
	fprintf(outfp, "},\n");
}

static void print_system_event_mapping_table_prefix(FILE *outfp)
{
	fprintf(outfp, "\nconst struct pmu_sys_events pmu_sys_event_tables[] = {");
}

static void print_system_event_mapping_table_suffix(FILE *outfp)
{
	fprintf(outfp, "\n\t{\n\t\t.table = 0\n\t},");
	fprintf(outfp, "\n};\n");
}

static int process_system_event_tables(FILE *outfp)
{
	struct sys_event_table *sys_event_table;

	print_system_event_mapping_table_prefix(outfp);

	list_for_each_entry(sys_event_table, &sys_event_tables, list) {
		fprintf(outfp, "\n\t{\n\t\t.table = %s,\n\t\t.name = \"%s\",\n\t},",
			sys_event_table->soc_id,
			sys_event_table->soc_id);
	}

	print_system_event_mapping_table_suffix(outfp);

	return 0;
}

static int process_mapfile(FILE *outfp, char *fpath)
{
	int n = 16384;
	FILE *mapfp;
	char *save = NULL;
	char *line, *p;
	int line_num;
	char *tblname;
	int ret = 0;

	pr_info("%s: Processing mapfile %s\n", prog, fpath);

	line = malloc(n);
	if (!line)
		return -1;

	mapfp = fopen(fpath, "r");
	if (!mapfp) {
		pr_info("%s: Error %s opening %s\n", prog, strerror(errno),
				fpath);
		free(line);
		return -1;
	}

	print_mapping_table_prefix(outfp);

	/* Skip first line (header) */
	p = fgets(line, n, mapfp);
	if (!p)
		goto out;

	line_num = 1;
	while (1) {
		char *cpuid, *version, *type, *fname;

		line_num++;
		p = fgets(line, n, mapfp);
		if (!p)
			break;

		if (line[0] == '#' || line[0] == '\n')
			continue;

		if (line[strlen(line)-1] != '\n') {
			/* TODO Deal with lines longer than 16K */
			pr_info("%s: Mapfile %s: line %d too long, aborting\n",
					prog, fpath, line_num);
			ret = -1;
			goto out;
		}
		line[strlen(line)-1] = '\0';

		cpuid = fixregex(strtok_r(p, ",", &save));
		version = strtok_r(NULL, ",", &save);
		fname = strtok_r(NULL, ",", &save);
		type = strtok_r(NULL, ",", &save);

		tblname = file_name_to_table_name(fname);
		fprintf(outfp, "{\n");
		fprintf(outfp, "\t.cpuid = \"%s\",\n", cpuid);
		fprintf(outfp, "\t.version = \"%s\",\n", version);
		fprintf(outfp, "\t.type = \"%s\",\n", type);

		/*
		 * CHECK: We can't use the type (eg "core") field in the
		 * table name. For us to do that, we need to somehow tweak
		 * the other caller of file_name_to_table(), process_json()
		 * to determine the type. process_json() file has no way
		 * of knowing these are "core" events unless file name has
		 * core in it. If filename has core in it, we can safely
		 * ignore the type field here also.
		 */
		fprintf(outfp, "\t.table = %s\n", tblname);
		fprintf(outfp, "},\n");
	}

out:
	print_mapping_test_table(outfp);
	print_mapping_table_suffix(outfp);
	fclose(mapfp);
	free(line);
	return ret;
}

/*
 * If we fail to locate/process JSON and map files, create a NULL mapping
 * table. This would at least allow perf to build even if we can't find/use
 * the aliases.
 */
static void create_empty_mapping(const char *output_file)
{
	FILE *outfp;

	pr_info("%s: Creating empty pmu_events_map[] table\n", prog);

	/* Truncate file to clear any partial writes to it */
	outfp = fopen(output_file, "w");
	if (!outfp) {
		perror("fopen()");
		_Exit(1);
	}

	fprintf(outfp, "#include \"pmu-events/pmu-events.h\"\n");
	print_mapping_table_prefix(outfp);
	print_mapping_table_suffix(outfp);
	print_system_event_mapping_table_prefix(outfp);
	print_system_event_mapping_table_suffix(outfp);
	fclose(outfp);
}

static int get_maxfds(void)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0)
		return min(rlim.rlim_max / 2, (rlim_t)512);

	return 512;
}

/*
 * nftw() doesn't let us pass an argument to the processing function,
 * so use a global variables.
 */
static FILE *eventsfp;
static char *mapfile;

static int is_leaf_dir(const char *fpath)
{
	DIR *d;
	struct dirent *dir;
	int res = 1;

	d = opendir(fpath);
	if (!d)
		return 0;

	while ((dir = readdir(d)) != NULL) {
		if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
			continue;

		if (dir->d_type == DT_DIR) {
			res = 0;
			break;
		} else if (dir->d_type == DT_UNKNOWN) {
			char path[PATH_MAX];
			struct stat st;

			sprintf(path, "%s/%s", fpath, dir->d_name);
			if (stat(path, &st))
				break;

			if (S_ISDIR(st.st_mode)) {
				res = 0;
				break;
			}
		}
	}

	closedir(d);

	return res;
}

static int is_json_file(const char *name)
{
	const char *suffix;

	if (strlen(name) < 5)
		return 0;

	suffix = name + strlen(name) - 5;

	if (strncmp(suffix, ".json", 5) == 0)
		return 1;
	return 0;
}

static int preprocess_arch_std_files(const char *fpath, const struct stat *sb,
				int typeflag, struct FTW *ftwbuf)
{
	int level = ftwbuf->level;
	int is_file = typeflag == FTW_F;

	if (level == 1 && is_file && is_json_file(fpath))
		return json_events(fpath, save_arch_std_events, (void *)sb);

	return 0;
}

static int process_one_file(const char *fpath, const struct stat *sb,
			    int typeflag, struct FTW *ftwbuf)
{
	char *tblname, *bname;
	int is_dir  = typeflag == FTW_D;
	int is_file = typeflag == FTW_F;
	int level   = ftwbuf->level;
	int err = 0;

	if (level >= 2 && is_dir) {
		int count = 0;
		/*
		 * For level 2 directory, bname will include parent name,
		 * like vendor/platform. So search back from platform dir
		 * to find this.
		 * Something similar for level 3 directory, but we're a PMU
		 * category folder, like vendor/platform/cpu.
		 */
		bname = (char *) fpath + ftwbuf->base - 2;
		for (;;) {
			if (*bname == '/')
				count++;
			if (count == level - 1)
				break;
			bname--;
		}
		bname++;
	} else
		bname = (char *) fpath + ftwbuf->base;

	pr_debug("%s %d %7jd %-20s %s\n",
		 is_file ? "f" : is_dir ? "d" : "x",
		 level, sb->st_size, bname, fpath);

	/* base dir or too deep */
	if (level == 0 || level > 4)
		return 0;


	/* model directory, reset topic */
	if ((level == 1 && is_dir && is_leaf_dir(fpath)) ||
	    (level >= 2 && is_dir && is_leaf_dir(fpath))) {
		if (close_table)
			print_events_table_suffix(eventsfp);

		/*
		 * Drop file name suffix. Replace hyphens with underscores.
		 * Fail if file name contains any alphanum characters besides
		 * underscores.
		 */
		tblname = file_name_to_table_name(bname);
		if (!tblname) {
			pr_info("%s: Error determining table name for %s\n", prog,
				bname);
			return -1;
		}

		if (is_sys_dir(bname)) {
			struct sys_event_table *sys_event_table;

			sys_event_table = malloc(sizeof(*sys_event_table));
			if (!sys_event_table)
				return -1;

			sys_event_table->soc_id = strdup(tblname);
			if (!sys_event_table->soc_id) {
				free(sys_event_table);
				return -1;
			}
			list_add_tail(&sys_event_table->list,
				      &sys_event_tables);
		}

		print_events_table_prefix(eventsfp, tblname);
		return 0;
	}

	/*
	 * Save the mapfile name for now. We will process mapfile
	 * after processing all JSON files (so we can write out the
	 * mapping table after all PMU events tables).
	 *
	 */
	if (level == 1 && is_file) {
		if (!strcmp(bname, "mapfile.csv")) {
			mapfile = strdup(fpath);
			return 0;
		}
		if (is_json_file(bname))
			pr_debug("%s: ArchStd json is preprocessed %s\n", prog, fpath);
		else
			pr_info("%s: Ignoring file %s\n", prog, fpath);
		return 0;
	}

	/*
	 * If the file name does not have a .json extension,
	 * ignore it. It could be a readme.txt for instance.
	 */
	if (is_file) {
		if (!is_json_file(bname)) {
			pr_info("%s: Ignoring file without .json suffix %s\n", prog,
				fpath);
			return 0;
		}
	}

	if (level > 1 && add_topic(bname))
		return -ENOMEM;

	/*
	 * Assume all other files are JSON files.
	 *
	 * If mapfile refers to 'power7_core.json', we create a table
	 * named 'power7_core'. Any inconsistencies between the mapfile
	 * and directory tree could result in build failure due to table
	 * names not being found.
	 *
	 * At least for now, be strict with processing JSON file names.
	 * i.e. if JSON file name cannot be mapped to C-style table name,
	 * fail.
	 */
	if (is_file) {
		struct perf_entry_data data = {
			.topic = get_topic(),
			.outfp = eventsfp,
		};

		err = json_events(fpath, print_events_table_entry, &data);

		free(data.topic);
	}

	return err;
}

#ifndef PATH_MAX
#define PATH_MAX	4096
#endif

/*
 * Starting in directory 'start_dirname', find the "mapfile.csv" and
 * the set of JSON files for the architecture 'arch'.
 *
 * From each JSON file, create a C-style "PMU events table" from the
 * JSON file (see struct pmu_event).
 *
 * From the mapfile, create a mapping between the CPU revisions and
 * PMU event tables (see struct pmu_events_map).
 *
 * Write out the PMU events tables and the mapping table to pmu-event.c.
 */
int main(int argc, char *argv[])
{
	int rc, ret = 0, empty_map = 0;
	int maxfds;
	char ldirname[PATH_MAX];
	const char *arch;
	const char *output_file;
	const char *start_dirname;
	const char *err_string_ext = "";
	struct stat stbuf;

	prog = basename(argv[0]);
	if (argc < 4) {
		pr_err("Usage: %s <arch> <starting_dir> <output_file>\n", prog);
		return 1;
	}

	arch = argv[1];
	start_dirname = argv[2];
	output_file = argv[3];

	if (argc > 4)
		verbose = atoi(argv[4]);

	eventsfp = fopen(output_file, "w");
	if (!eventsfp) {
		pr_err("%s Unable to create required file %s (%s)\n",
				prog, output_file, strerror(errno));
		return 2;
	}

	sprintf(ldirname, "%s/%s", start_dirname, arch);

	/* If architecture does not have any event lists, bail out */
	if (stat(ldirname, &stbuf) < 0) {
		pr_info("%s: Arch %s has no PMU event lists\n", prog, arch);
		empty_map = 1;
		goto err_close_eventsfp;
	}

	/* Include pmu-events.h first */
	fprintf(eventsfp, "#include \"pmu-events/pmu-events.h\"\n");

	/*
	 * The mapfile allows multiple CPUids to point to the same JSON file,
	 * so, not sure if there is a need for symlinks within the pmu-events
	 * directory.
	 *
	 * For now, treat symlinks of JSON files as regular files and create
	 * separate tables for each symlink (presumably, each symlink refers
	 * to specific version of the CPU).
	 */

	maxfds = get_maxfds();
	rc = nftw(ldirname, preprocess_arch_std_files, maxfds, 0);
	if (rc)
		goto err_processing_std_arch_event_dir;

	rc = nftw(ldirname, process_one_file, maxfds, 0);
	if (rc)
		goto err_processing_dir;

	sprintf(ldirname, "%s/test", start_dirname);

	rc = nftw(ldirname, preprocess_arch_std_files, maxfds, 0);
	if (rc)
		goto err_processing_std_arch_event_dir;

	rc = nftw(ldirname, process_one_file, maxfds, 0);
	if (rc)
		goto err_processing_dir;

	if (close_table)
		print_events_table_suffix(eventsfp);

	if (!mapfile) {
		pr_info("%s: No CPU->JSON mapping?\n", prog);
		empty_map = 1;
		goto err_close_eventsfp;
	}

	rc = process_mapfile(eventsfp, mapfile);
	if (rc) {
		pr_info("%s: Error processing mapfile %s\n", prog, mapfile);
		/* Make build fail */
		ret = 1;
		goto err_close_eventsfp;
	}

	rc = process_system_event_tables(eventsfp);
	fclose(eventsfp);
	if (rc) {
		ret = 1;
		goto err_out;
	}

	free_arch_std_events();
	free_sys_event_tables();
	free(mapfile);
	return 0;

err_processing_std_arch_event_dir:
	err_string_ext = " for std arch event";
err_processing_dir:
	if (verbose) {
		pr_info("%s: Error walking file tree %s%s\n", prog, ldirname,
			err_string_ext);
		empty_map = 1;
	} else if (rc < 0) {
		ret = 1;
	} else {
		empty_map = 1;
	}
err_close_eventsfp:
	fclose(eventsfp);
	if (empty_map)
		create_empty_mapping(output_file);
err_out:
	free_arch_std_events();
	free_sys_event_tables();
	free(mapfile);
	return ret;
}
