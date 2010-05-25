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
static bool			debug_ordering;
static u64			last_timestamp;

static int default_start_script(const char *script __unused,
				int argc __unused,
				const char **argv __unused)
{
	return 0;
}

static int default_stop_script(void)
{
	return 0;
}

static int default_generate_script(const char *outfile __unused)
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
	setup_python_scripting();

	scripting_ops = &default_scripting_ops;
}

static int cleanup_scripting(void)
{
	pr_debug("\nperf trace script stopped\n");

	return scripting_ops->stop_script();
}

#include "util/parse-options.h"

#include "perf.h"
#include "util/debug.h"

#include "util/trace-event.h"
#include "util/exec_cmd.h"

static char const		*input_name = "perf.data";

static int process_sample_event(event_t *event, struct perf_session *session)
{
	struct sample_data data;
	struct thread *thread;

	memset(&data, 0, sizeof(data));
	data.time = -1;
	data.cpu = -1;
	data.period = 1;

	event__parse_sample(event, session->sample_type, &data);

	dump_printf("(IP, %d): %d/%d: %#Lx period: %Ld\n", event->header.misc,
		    data.pid, data.tid, data.ip, data.period);

	thread = perf_session__findnew(session, event->ip.pid);
	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	if (session->sample_type & PERF_SAMPLE_RAW) {
		if (debug_ordering) {
			if (data.time < last_timestamp) {
				pr_err("Samples misordered, previous: %llu "
					"this: %llu\n", last_timestamp,
					data.time);
			}
			last_timestamp = data.time;
		}
		/*
		 * FIXME: better resolve from pid from the struct trace_entry
		 * field, although it should be the same than this perf
		 * event pid
		 */
		scripting_ops->process_event(data.cpu, data.raw_data,
					     data.raw_size,
					     data.time, thread->comm);
	}

	session->hists.stats.total_period += data.period;
	return 0;
}

static struct perf_event_ops event_ops = {
	.sample	= process_sample_event,
	.comm	= event__process_comm,
	.attr	= event__process_attr,
	.event_type = event__process_event_type,
	.tracing_data = event__process_tracing_data,
	.build_id = event__process_build_id,
	.ordered_samples = true,
};

extern volatile int session_done;

static void sig_handler(int sig __unused)
{
	session_done = 1;
}

static int __cmd_trace(struct perf_session *session)
{
	signal(SIGINT, sig_handler);

	return perf_session__process_events(session, &event_ops);
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

	if (strcmp(str, "lang") == 0) {
		list_available_languages();
		exit(0);
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

#define for_each_lang(scripts_dir, lang_dirent, lang_next)		\
	while (!readdir_r(scripts_dir, &lang_dirent, &lang_next) &&	\
	       lang_next)						\
		if (lang_dirent.d_type == DT_DIR &&			\
		    (strcmp(lang_dirent.d_name, ".")) &&		\
		    (strcmp(lang_dirent.d_name, "..")))

#define for_each_script(lang_dir, script_dirent, script_next)		\
	while (!readdir_r(lang_dir, &script_dirent, &script_next) &&	\
	       script_next)						\
		if (script_dirent.d_type != DT_DIR)


#define RECORD_SUFFIX			"-record"
#define REPORT_SUFFIX			"-report"

struct script_desc {
	struct list_head	node;
	char			*name;
	char			*half_liner;
	char			*args;
};

LIST_HEAD(script_descs);

static struct script_desc *script_desc__new(const char *name)
{
	struct script_desc *s = zalloc(sizeof(*s));

	if (s != NULL)
		s->name = strdup(name);

	return s;
}

static void script_desc__delete(struct script_desc *s)
{
	free(s->name);
	free(s);
}

static void script_desc__add(struct script_desc *s)
{
	list_add_tail(&s->node, &script_descs);
}

static struct script_desc *script_desc__find(const char *name)
{
	struct script_desc *s;

	list_for_each_entry(s, &script_descs, node)
		if (strcasecmp(s->name, name) == 0)
			return s;
	return NULL;
}

static struct script_desc *script_desc__findnew(const char *name)
{
	struct script_desc *s = script_desc__find(name);

	if (s)
		return s;

	s = script_desc__new(name);
	if (!s)
		goto out_delete_desc;

	script_desc__add(s);

	return s;

out_delete_desc:
	script_desc__delete(s);

	return NULL;
}

static char *ends_with(char *str, const char *suffix)
{
	size_t suffix_len = strlen(suffix);
	char *p = str;

	if (strlen(str) > suffix_len) {
		p = str + strlen(str) - suffix_len;
		if (!strncmp(p, suffix, suffix_len))
			return p;
	}

	return NULL;
}

static char *ltrim(char *str)
{
	int len = strlen(str);

	while (len && isspace(*str)) {
		len--;
		str++;
	}

	return str;
}

static int read_script_info(struct script_desc *desc, const char *filename)
{
	char line[BUFSIZ], *p;
	FILE *fp;

	fp = fopen(filename, "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		p = ltrim(line);
		if (strlen(p) == 0)
			continue;
		if (*p != '#')
			continue;
		p++;
		if (strlen(p) && *p == '!')
			continue;

		p = ltrim(p);
		if (strlen(p) && p[strlen(p) - 1] == '\n')
			p[strlen(p) - 1] = '\0';

		if (!strncmp(p, "description:", strlen("description:"))) {
			p += strlen("description:");
			desc->half_liner = strdup(ltrim(p));
			continue;
		}

		if (!strncmp(p, "args:", strlen("args:"))) {
			p += strlen("args:");
			desc->args = strdup(ltrim(p));
			continue;
		}
	}

	fclose(fp);

	return 0;
}

static int list_available_scripts(const struct option *opt __used,
				  const char *s __used, int unset __used)
{
	struct dirent *script_next, *lang_next, script_dirent, lang_dirent;
	char scripts_path[MAXPATHLEN];
	DIR *scripts_dir, *lang_dir;
	char script_path[MAXPATHLEN];
	char lang_path[MAXPATHLEN];
	struct script_desc *desc;
	char first_half[BUFSIZ];
	char *script_root;
	char *str;

	snprintf(scripts_path, MAXPATHLEN, "%s/scripts", perf_exec_path());

	scripts_dir = opendir(scripts_path);
	if (!scripts_dir)
		return -1;

	for_each_lang(scripts_dir, lang_dirent, lang_next) {
		snprintf(lang_path, MAXPATHLEN, "%s/%s/bin", scripts_path,
			 lang_dirent.d_name);
		lang_dir = opendir(lang_path);
		if (!lang_dir)
			continue;

		for_each_script(lang_dir, script_dirent, script_next) {
			script_root = strdup(script_dirent.d_name);
			str = ends_with(script_root, REPORT_SUFFIX);
			if (str) {
				*str = '\0';
				desc = script_desc__findnew(script_root);
				snprintf(script_path, MAXPATHLEN, "%s/%s",
					 lang_path, script_dirent.d_name);
				read_script_info(desc, script_path);
			}
			free(script_root);
		}
	}

	fprintf(stdout, "List of available trace scripts:\n");
	list_for_each_entry(desc, &script_descs, node) {
		sprintf(first_half, "%s %s", desc->name,
			desc->args ? desc->args : "");
		fprintf(stdout, "  %-36s %s\n", first_half,
			desc->half_liner ? desc->half_liner : "");
	}

	exit(0);
}

static char *get_script_path(const char *script_root, const char *suffix)
{
	struct dirent *script_next, *lang_next, script_dirent, lang_dirent;
	char scripts_path[MAXPATHLEN];
	char script_path[MAXPATHLEN];
	DIR *scripts_dir, *lang_dir;
	char lang_path[MAXPATHLEN];
	char *str, *__script_root;
	char *path = NULL;

	snprintf(scripts_path, MAXPATHLEN, "%s/scripts", perf_exec_path());

	scripts_dir = opendir(scripts_path);
	if (!scripts_dir)
		return NULL;

	for_each_lang(scripts_dir, lang_dirent, lang_next) {
		snprintf(lang_path, MAXPATHLEN, "%s/%s/bin", scripts_path,
			 lang_dirent.d_name);
		lang_dir = opendir(lang_path);
		if (!lang_dir)
			continue;

		for_each_script(lang_dir, script_dirent, script_next) {
			__script_root = strdup(script_dirent.d_name);
			str = ends_with(__script_root, suffix);
			if (str) {
				*str = '\0';
				if (strcmp(__script_root, script_root))
					continue;
				snprintf(script_path, MAXPATHLEN, "%s/%s",
					 lang_path, script_dirent.d_name);
				path = strdup(script_path);
				free(__script_root);
				break;
			}
			free(__script_root);
		}
	}

	return path;
}

static const char * const trace_usage[] = {
	"perf trace [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('L', "Latency", &latency_format,
		    "show latency attributes (irqs/preemption disabled, etc)"),
	OPT_CALLBACK_NOOPT('l', "list", NULL, NULL, "list available scripts",
			   list_available_scripts),
	OPT_CALLBACK('s', "script", NULL, "name",
		     "script file name (lang:script name, script name, or *)",
		     parse_scriptname),
	OPT_STRING('g', "gen-script", &generate_script_lang, "lang",
		   "generate perf-trace.xx script in specified language"),
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_BOOLEAN('d', "debug-ordering", &debug_ordering,
		   "check that samples time ordering is monotonic"),

	OPT_END()
};

int cmd_trace(int argc, const char **argv, const char *prefix __used)
{
	struct perf_session *session;
	const char *suffix = NULL;
	const char **__argv;
	char *script_path;
	int i, err;

	if (argc >= 2 && strncmp(argv[1], "rec", strlen("rec")) == 0) {
		if (argc < 3) {
			fprintf(stderr,
				"Please specify a record script\n");
			return -1;
		}
		suffix = RECORD_SUFFIX;
	}

	if (argc >= 2 && strncmp(argv[1], "rep", strlen("rep")) == 0) {
		if (argc < 3) {
			fprintf(stderr,
				"Please specify a report script\n");
			return -1;
		}
		suffix = REPORT_SUFFIX;
	}

	if (!suffix && argc >= 2 && strncmp(argv[1], "-", strlen("-")) != 0) {
		char *record_script_path, *report_script_path;
		int live_pipe[2];
		pid_t pid;

		record_script_path = get_script_path(argv[1], RECORD_SUFFIX);
		if (!record_script_path) {
			fprintf(stderr, "record script not found\n");
			return -1;
		}

		report_script_path = get_script_path(argv[1], REPORT_SUFFIX);
		if (!report_script_path) {
			fprintf(stderr, "report script not found\n");
			return -1;
		}

		if (pipe(live_pipe) < 0) {
			perror("failed to create pipe");
			exit(-1);
		}

		pid = fork();
		if (pid < 0) {
			perror("failed to fork");
			exit(-1);
		}

		if (!pid) {
			dup2(live_pipe[1], 1);
			close(live_pipe[0]);

			__argv = malloc(5 * sizeof(const char *));
			__argv[0] = "/bin/sh";
			__argv[1] = record_script_path;
			__argv[2] = "-o";
			__argv[3] = "-";
			__argv[4] = NULL;

			execvp("/bin/sh", (char **)__argv);
			exit(-1);
		}

		dup2(live_pipe[0], 0);
		close(live_pipe[1]);

		__argv = malloc((argc + 3) * sizeof(const char *));
		__argv[0] = "/bin/sh";
		__argv[1] = report_script_path;
		for (i = 2; i < argc; i++)
			__argv[i] = argv[i];
		__argv[i++] = "-i";
		__argv[i++] = "-";
		__argv[i++] = NULL;

		execvp("/bin/sh", (char **)__argv);
		exit(-1);
	}

	if (suffix) {
		script_path = get_script_path(argv[2], suffix);
		if (!script_path) {
			fprintf(stderr, "script not found\n");
			return -1;
		}

		__argv = malloc((argc + 1) * sizeof(const char *));
		__argv[0] = "/bin/sh";
		__argv[1] = script_path;
		for (i = 3; i < argc; i++)
			__argv[i - 1] = argv[i];
		__argv[argc - 1] = NULL;

		execvp("/bin/sh", (char **)__argv);
		exit(-1);
	}

	setup_scripting();

	argc = parse_options(argc, argv, options, trace_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (symbol__init() < 0)
		return -1;
	if (!script_name)
		setup_pager();

	session = perf_session__new(input_name, O_RDONLY, 0, false);
	if (session == NULL)
		return -ENOMEM;

	if (strcmp(input_name, "-") &&
	    !perf_session__has_traces(session, "record -R"))
		return -EINVAL;

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

		err = scripting_ops->generate_script("perf-trace");
		goto out;
	}

	if (script_name) {
		err = scripting_ops->start_script(script_name, argc, argv);
		if (err)
			goto out;
		pr_debug("perf trace started with script %s\n\n", script_name);
	}

	err = __cmd_trace(session);

	perf_session__delete(session);
	cleanup_scripting();
out:
	return err;
}
