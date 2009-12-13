#include "builtin.h"

#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/exec_cmd.h"
#include "util/trace-event.h"
#include "util/session.h"

static char const		*script_name;
static char const		*generate_script_lang;

static int default_start_script(const char *script __attribute((unused)))
{
	return 0;
}

static int default_stop_script(void)
{
	return 0;
}

static int default_generate_script(const char *outfile __attribute ((unused)))
{
	return 0;
}

static struct scripting_ops default_scripting_ops = {
	.start_script		= default_start_script,
	.stop_script		= default_stop_script,
	.process_event		= print_event,
	.generate_script	= default_generate_script,
};

static struct scripting_ops	*scripting_ops;

static void setup_scripting(void)
{
	/* make sure PERF_EXEC_PATH is set for scripts */
	perf_set_argv_exec_path(perf_exec_path());

	setup_perl_scripting();

	scripting_ops = &default_scripting_ops;
}

static int cleanup_scripting(void)
{
	return scripting_ops->stop_script();
}

#include "util/parse-options.h"

#include "perf.h"
#include "util/debug.h"

#include "util/trace-event.h"
#include "util/exec_cmd.h"

static char const		*input_name = "perf.data";

static u64			sample_type;

static int process_sample_event(event_t *event, struct perf_session *session __used)
{
	struct sample_data data;
	struct thread *thread;

	memset(&data, 0, sizeof(data));
	data.time = -1;
	data.cpu = -1;
	data.period = 1;

	event__parse_sample(event, sample_type, &data);

	dump_printf("(IP, %d): %d/%d: %p period: %Ld\n",
		event->header.misc,
		data.pid, data.tid,
		(void *)(long)data.ip,
		(long long)data.period);

	thread = threads__findnew(event->ip.pid);
	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	if (sample_type & PERF_SAMPLE_RAW) {
		/*
		 * FIXME: better resolve from pid from the struct trace_entry
		 * field, although it should be the same than this perf
		 * event pid
		 */
		scripting_ops->process_event(data.cpu, data.raw_data,
					     data.raw_size,
					     data.time, thread->comm);
	}
	event__stats.total += data.period;

	return 0;
}

static int sample_type_check(u64 type)
{
	sample_type = type;

	if (!(sample_type & PERF_SAMPLE_RAW)) {
		fprintf(stderr,
			"No trace sample to read. Did you call perf record "
			"without -R?");
		return -1;
	}

	return 0;
}

static struct perf_event_ops event_ops = {
	.process_sample_event	= process_sample_event,
	.process_comm_event	= event__process_comm,
	.sample_type_check	= sample_type_check,
};

static int __cmd_trace(struct perf_session *session)
{
	register_idle_thread();
	return perf_session__process_events(session, &event_ops, 0,
					    &event__cwdlen, &event__cwd);
}

struct script_spec {
	struct list_head	node;
	struct scripting_ops	*ops;
	char			spec[0];
};

LIST_HEAD(script_specs);

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

static void script_spec__delete(struct script_spec *s)
{
	free(s->spec);
	free(s);
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

static struct script_spec *script_spec__findnew(const char *spec,
						struct scripting_ops *ops)
{
	struct script_spec *s = script_spec__find(spec);

	if (s)
		return s;

	s = script_spec__new(spec, ops);
	if (!s)
		goto out_delete_spec;

	script_spec__add(s);

	return s;

out_delete_spec:
	script_spec__delete(s);

	return NULL;
}

int script_spec_register(const char *spec, struct scripting_ops *ops)
{
	struct script_spec *s;

	s = script_spec__find(spec);
	if (s)
		return -1;

	s = script_spec__findnew(spec, ops);
	if (!s)
		return -1;

	return 0;
}

static struct scripting_ops *script_spec__lookup(const char *spec)
{
	struct script_spec *s = script_spec__find(spec);
	if (!s)
		return NULL;

	return s->ops;
}

static void list_available_languages(void)
{
	struct script_spec *s;

	fprintf(stderr, "\n");
	fprintf(stderr, "Scripting language extensions (used in "
		"perf trace -s [spec:]script.[spec]):\n\n");

	list_for_each_entry(s, &script_specs, node)
		fprintf(stderr, "  %-42s [%s]\n", s->spec, s->ops->name);

	fprintf(stderr, "\n");
}

static int parse_scriptname(const struct option *opt __used,
			    const char *str, int unset __used)
{
	char spec[PATH_MAX];
	const char *script, *ext;
	int len;

	if (strcmp(str, "list") == 0) {
		list_available_languages();
		return 0;
	}

	script = strchr(str, ':');
	if (script) {
		len = script - str;
		if (len >= PATH_MAX) {
			fprintf(stderr, "invalid language specifier");
			return -1;
		}
		strncpy(spec, str, len);
		spec[len] = '\0';
		scripting_ops = script_spec__lookup(spec);
		if (!scripting_ops) {
			fprintf(stderr, "invalid language specifier");
			return -1;
		}
		script++;
	} else {
		script = str;
		ext = strchr(script, '.');
		if (!ext) {
			fprintf(stderr, "invalid script extension");
			return -1;
		}
		scripting_ops = script_spec__lookup(++ext);
		if (!scripting_ops) {
			fprintf(stderr, "invalid script extension");
			return -1;
		}
	}

	script_name = strdup(script);

	return 0;
}

static const char * const annotate_usage[] = {
	"perf trace [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('l', "latency", &latency_format,
		    "show latency attributes (irqs/preemption disabled, etc)"),
	OPT_CALLBACK('s', "script", NULL, "name",
		     "script file name (lang:script name, script name, or *)",
		     parse_scriptname),
	OPT_STRING('g', "gen-script", &generate_script_lang, "lang",
		   "generate perf-trace.xx script in specified language"),

	OPT_END()
};

int cmd_trace(int argc, const char **argv, const char *prefix __used)
{
	int err;
	struct perf_session *session;

	symbol__init(0);

	setup_scripting();

	argc = parse_options(argc, argv, options, annotate_usage, 0);
	if (argc) {
		/*
		 * Special case: if there's an argument left then assume tha
		 * it's a symbol filter:
		 */
		if (argc > 1)
			usage_with_options(annotate_usage, options);
	}

	setup_pager();

	session = perf_session__new(input_name, O_RDONLY, 0);
	if (session == NULL)
		return -ENOMEM;

	if (generate_script_lang) {
		struct stat perf_stat;

		int input = open(input_name, O_RDONLY);
		if (input < 0) {
			perror("failed to open file");
			exit(-1);
		}

		err = fstat(input, &perf_stat);
		if (err < 0) {
			perror("failed to stat file");
			exit(-1);
		}

		if (!perf_stat.st_size) {
			fprintf(stderr, "zero-sized file, nothing to do!\n");
			exit(0);
		}

		scripting_ops = script_spec__lookup(generate_script_lang);
		if (!scripting_ops) {
			fprintf(stderr, "invalid language specifier");
			return -1;
		}

		perf_header__read(&session->header, input);
		err = scripting_ops->generate_script("perf-trace");
		goto out;
	}

	if (script_name) {
		err = scripting_ops->start_script(script_name);
		if (err)
			goto out;
	}

	err = __cmd_trace(session);

	perf_session__delete(session);
	cleanup_scripting();
out:
	return err;
}
