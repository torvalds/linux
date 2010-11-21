#include "builtin.h"

#include "perf.h"
#include "util/cache.h"
#include "util/debug.h"
#include "util/exec_cmd.h"
#include "util/header.h"
#include "util/parse-options.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/trace-event.h"
#include "util/parse-options.h"
#include "util/util.h"

static char const		*script_name;
static char const		*generate_script_lang;
static bool			debug_mode;
static u64			last_timestamp;
static u64			nr_unordered;
extern const struct option	record_options[];

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
	setup_perl_scripting();
	setup_python_scripting();

	scripting_ops = &default_scripting_ops;
}

static int cleanup_scripting(void)
{
	pr_debug("\nperf trace script stopped\n");

	return scripting_ops->stop_script();
}

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
		if (debug_mode) {
			if (data.time < last_timestamp) {
				pr_err("Samples misordered, previous: %llu "
					"this: %llu\n", last_timestamp,
					data.time);
				nr_unordered++;
			}
			last_timestamp = data.time;
			return 0;
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

static u64 nr_lost;

static int process_lost_event(event_t *event, struct perf_session *session __used)
{
	nr_lost += event->lost.lost;

	return 0;
}

static struct perf_event_ops event_ops = {
	.sample	= process_sample_event,
	.comm	= event__process_comm,
	.attr	= event__process_attr,
	.event_type = event__process_event_type,
	.tracing_data = event__process_tracing_data,
	.build_id = event__process_build_id,
	.lost = process_lost_event,
	.ordered_samples = true,
};

extern volatile int session_done;

static void sig_handler(int sig __unused)
{
	session_done = 1;
}

static int __cmd_trace(struct perf_session *session)
{
	int ret;

	signal(SIGINT, sig_handler);

	ret = perf_session__process_events(session, &event_ops);

	if (debug_mode) {
		pr_err("Misordered timestamps: %llu\n", nr_unordered);
		pr_err("Lost events: %llu\n", nr_lost);
	}

	return ret;
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
		ext = strrchr(script, '.');
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

/* Helper function for filesystems that return a dent->d_type DT_UNKNOWN */
static int is_directory(const char *base_path, const struct dirent *dent)
{
	char path[PATH_MAX];
	struct stat st;

	sprintf(path, "%s/%s", base_path, dent->d_name);
	if (stat(path, &st))
		return 0;

	return S_ISDIR(st.st_mode);
}

#define for_each_lang(scripts_path, scripts_dir, lang_dirent, lang_next)\
	while (!readdir_r(scripts_dir, &lang_dirent, &lang_next) &&	\
	       lang_next)						\
		if ((lang_dirent.d_type == DT_DIR ||			\
		     (lang_dirent.d_type == DT_UNKNOWN &&		\
		      is_directory(scripts_path, &lang_dirent))) &&	\
		    (strcmp(lang_dirent.d_name, ".")) &&		\
		    (strcmp(lang_dirent.d_name, "..")))

#define for_each_script(lang_path, lang_dir, script_dirent, script_next)\
	while (!readdir_r(lang_dir, &script_dirent, &script_next) &&	\
	       script_next)						\
		if (script_dirent.d_type != DT_DIR &&			\
		    (script_dirent.d_type != DT_UNKNOWN ||		\
		     !is_directory(lang_path, &script_dirent)))


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

	if (s != NULL && name)
		s->name = strdup(name);

	return s;
}

static void script_desc__delete(struct script_desc *s)
{
	free(s->name);
	free(s->half_liner);
	free(s->args);
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

	for_each_lang(scripts_path, scripts_dir, lang_dirent, lang_next) {
		snprintf(lang_path, MAXPATHLEN, "%s/%s/bin", scripts_path,
			 lang_dirent.d_name);
		lang_dir = opendir(lang_path);
		if (!lang_dir)
			continue;

		for_each_script(lang_path, lang_dir, script_dirent, script_next) {
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

	for_each_lang(scripts_path, scripts_dir, lang_dirent, lang_next) {
		snprintf(lang_path, MAXPATHLEN, "%s/%s/bin", scripts_path,
			 lang_dirent.d_name);
		lang_dir = opendir(lang_path);
		if (!lang_dir)
			continue;

		for_each_script(lang_path, lang_dir, script_dirent, script_next) {
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

static bool is_top_script(const char *script_path)
{
	return ends_with((char *)script_path, "top") == NULL ? false : true;
}

static int has_required_arg(char *script_path)
{
	struct script_desc *desc;
	int n_args = 0;
	char *p;

	desc = script_desc__new(NULL);

	if (read_script_info(desc, script_path))
		goto out;

	if (!desc->args)
		goto out;

	for (p = desc->args; *p; p++)
		if (*p == '<')
			n_args++;
out:
	script_desc__delete(desc);

	return n_args;
}

static const char * const trace_usage[] = {
	"perf trace [<options>]",
	"perf trace [<options>] record <script> [<record-options>] <command>",
	"perf trace [<options>] report <script> [script-args]",
	"perf trace [<options>] <script> [<record-options>] <command>",
	"perf trace [<options>] <top-script> [script-args]",
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
	OPT_BOOLEAN('d', "debug-mode", &debug_mode,
		   "do various checks like samples ordering and lost events"),

	OPT_END()
};

static bool have_cmd(int argc, const char **argv)
{
	char **__argv = malloc(sizeof(const char *) * argc);

	if (!__argv)
		die("malloc");
	memcpy(__argv, argv, sizeof(const char *) * argc);
	argc = parse_options(argc, (const char **)__argv, record_options,
			     NULL, PARSE_OPT_STOP_AT_NON_OPTION);
	free(__argv);

	return argc != 0;
}

int cmd_trace(int argc, const char **argv, const char *prefix __used)
{
	char *rec_script_path = NULL;
	char *rep_script_path = NULL;
	struct perf_session *session;
	char *script_path = NULL;
	const char **__argv;
	bool system_wide;
	int i, j, err;

	setup_scripting();

	argc = parse_options(argc, argv, options, trace_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc > 1 && !strncmp(argv[0], "rec", strlen("rec"))) {
		rec_script_path = get_script_path(argv[1], RECORD_SUFFIX);
		if (!rec_script_path)
			return cmd_record(argc, argv, NULL);
	}

	if (argc > 1 && !strncmp(argv[0], "rep", strlen("rep"))) {
		rep_script_path = get_script_path(argv[1], REPORT_SUFFIX);
		if (!rep_script_path) {
			fprintf(stderr,
				"Please specify a valid report script"
				"(see 'perf trace -l' for listing)\n");
			return -1;
		}
	}

	/* make sure PERF_EXEC_PATH is set for scripts */
	perf_set_argv_exec_path(perf_exec_path());

	if (argc && !script_name && !rec_script_path && !rep_script_path) {
		int live_pipe[2];
		int rep_args;
		pid_t pid;

		rec_script_path = get_script_path(argv[0], RECORD_SUFFIX);
		rep_script_path = get_script_path(argv[0], REPORT_SUFFIX);

		if (!rec_script_path && !rep_script_path) {
			fprintf(stderr, " Couldn't find script %s\n\n See perf"
				" trace -l for available scripts.\n", argv[0]);
			usage_with_options(trace_usage, options);
		}

		if (is_top_script(argv[0])) {
			rep_args = argc - 1;
		} else {
			int rec_args;

			rep_args = has_required_arg(rep_script_path);
			rec_args = (argc - 1) - rep_args;
			if (rec_args < 0) {
				fprintf(stderr, " %s script requires options."
					"\n\n See perf trace -l for available "
					"scripts and options.\n", argv[0]);
				usage_with_options(trace_usage, options);
			}
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
			system_wide = true;
			j = 0;

			dup2(live_pipe[1], 1);
			close(live_pipe[0]);

			if (!is_top_script(argv[0]))
				system_wide = !have_cmd(argc - rep_args,
							&argv[rep_args]);

			__argv = malloc((argc + 6) * sizeof(const char *));
			if (!__argv)
				die("malloc");

			__argv[j++] = "/bin/sh";
			__argv[j++] = rec_script_path;
			if (system_wide)
				__argv[j++] = "-a";
			__argv[j++] = "-q";
			__argv[j++] = "-o";
			__argv[j++] = "-";
			for (i = rep_args + 1; i < argc; i++)
				__argv[j++] = argv[i];
			__argv[j++] = NULL;

			execvp("/bin/sh", (char **)__argv);
			free(__argv);
			exit(-1);
		}

		dup2(live_pipe[0], 0);
		close(live_pipe[1]);

		__argv = malloc((argc + 4) * sizeof(const char *));
		if (!__argv)
			die("malloc");
		j = 0;
		__argv[j++] = "/bin/sh";
		__argv[j++] = rep_script_path;
		for (i = 1; i < rep_args + 1; i++)
			__argv[j++] = argv[i];
		__argv[j++] = "-i";
		__argv[j++] = "-";
		__argv[j++] = NULL;

		execvp("/bin/sh", (char **)__argv);
		free(__argv);
		exit(-1);
	}

	if (rec_script_path)
		script_path = rec_script_path;
	if (rep_script_path)
		script_path = rep_script_path;

	if (script_path) {
		system_wide = false;
		j = 0;

		if (rec_script_path)
			system_wide = !have_cmd(argc - 1, &argv[1]);

		__argv = malloc((argc + 2) * sizeof(const char *));
		if (!__argv)
			die("malloc");
		__argv[j++] = "/bin/sh";
		__argv[j++] = script_path;
		if (system_wide)
			__argv[j++] = "-a";
		for (i = 2; i < argc; i++)
			__argv[j++] = argv[i];
		__argv[j++] = NULL;

		execvp("/bin/sh", (char **)__argv);
		free(__argv);
		exit(-1);
	}

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
