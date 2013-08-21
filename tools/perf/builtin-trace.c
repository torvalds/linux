#include <traceevent/event-parse.h>
#include "builtin.h"
#include "util/color.h"
#include "util/evlist.h"
#include "util/machine.h"
#include "util/thread.h"
#include "util/parse-options.h"
#include "util/strlist.h"
#include "util/thread_map.h"

#include <libaudit.h>
#include <stdlib.h>

static struct syscall_fmt {
	const char *name;
	const char *alias;
	bool	   errmsg;
	bool	   timeout;
} syscall_fmts[] = {
	{ .name	    = "access",	    .errmsg = true, },
	{ .name	    = "arch_prctl", .errmsg = true, .alias = "prctl", },
	{ .name	    = "connect",    .errmsg = true, },
	{ .name	    = "fstat",	    .errmsg = true, .alias = "newfstat", },
	{ .name	    = "fstatat",    .errmsg = true, .alias = "newfstatat", },
	{ .name	    = "futex",	    .errmsg = true, },
	{ .name	    = "open",	    .errmsg = true, },
	{ .name	    = "poll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "ppoll",	    .errmsg = true, .timeout = true, },
	{ .name	    = "read",	    .errmsg = true, },
	{ .name	    = "recvfrom",   .errmsg = true, },
	{ .name	    = "select",	    .errmsg = true, .timeout = true, },
	{ .name	    = "socket",	    .errmsg = true, },
	{ .name	    = "stat",	    .errmsg = true, .alias = "newstat", },
};

static int syscall_fmt__cmp(const void *name, const void *fmtp)
{
	const struct syscall_fmt *fmt = fmtp;
	return strcmp(name, fmt->name);
}

static struct syscall_fmt *syscall_fmt__find(const char *name)
{
	const int nmemb = ARRAY_SIZE(syscall_fmts);
	return bsearch(name, syscall_fmts, nmemb, sizeof(struct syscall_fmt), syscall_fmt__cmp);
}

struct syscall {
	struct event_format *tp_format;
	const char	    *name;
	bool		    filtered;
	struct syscall_fmt  *fmt;
};

static size_t fprintf_duration(unsigned long t, FILE *fp)
{
	double duration = (double)t / NSEC_PER_MSEC;
	size_t printed = fprintf(fp, "(");

	if (duration >= 1.0)
		printed += color_fprintf(fp, PERF_COLOR_RED, "%6.3f ms", duration);
	else if (duration >= 0.01)
		printed += color_fprintf(fp, PERF_COLOR_YELLOW, "%6.3f ms", duration);
	else
		printed += color_fprintf(fp, PERF_COLOR_NORMAL, "%6.3f ms", duration);
	return printed + fprintf(fp, "): ");
}

struct thread_trace {
	u64		  entry_time;
	u64		  exit_time;
	bool		  entry_pending;
	unsigned long	  nr_events;
	char		  *entry_str;
	double		  runtime_ms;
};

static struct thread_trace *thread_trace__new(void)
{
	return zalloc(sizeof(struct thread_trace));
}

static struct thread_trace *thread__trace(struct thread *thread, FILE *fp)
{
	struct thread_trace *ttrace;

	if (thread == NULL)
		goto fail;

	if (thread->priv == NULL)
		thread->priv = thread_trace__new();
		
	if (thread->priv == NULL)
		goto fail;

	ttrace = thread->priv;
	++ttrace->nr_events;

	return ttrace;
fail:
	color_fprintf(fp, PERF_COLOR_RED,
		      "WARNING: not enough memory, dropping samples!\n");
	return NULL;
}

struct trace {
	struct perf_tool	tool;
	int			audit_machine;
	struct {
		int		max;
		struct syscall  *table;
	} syscalls;
	struct perf_record_opts opts;
	struct machine		host;
	u64			base_time;
	FILE			*output;
	unsigned long		nr_events;
	struct strlist		*ev_qualifier;
	bool			not_ev_qualifier;
	bool			sched;
	bool			multiple_threads;
	double			duration_filter;
	double			runtime_ms;
};

static bool trace__filter_duration(struct trace *trace, double t)
{
	return t < (trace->duration_filter * NSEC_PER_MSEC);
}

static size_t trace__fprintf_tstamp(struct trace *trace, u64 tstamp, FILE *fp)
{
	double ts = (double)(tstamp - trace->base_time) / NSEC_PER_MSEC;

	return fprintf(fp, "%10.3f ", ts);
}

static bool done = false;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

static size_t trace__fprintf_entry_head(struct trace *trace, struct thread *thread,
					u64 duration, u64 tstamp, FILE *fp)
{
	size_t printed = trace__fprintf_tstamp(trace, tstamp, fp);
	printed += fprintf_duration(duration, fp);

	if (trace->multiple_threads)
		printed += fprintf(fp, "%d ", thread->tid);

	return printed;
}

static int trace__process_event(struct trace *trace, struct machine *machine,
				union perf_event *event)
{
	int ret = 0;

	switch (event->header.type) {
	case PERF_RECORD_LOST:
		color_fprintf(trace->output, PERF_COLOR_RED,
			      "LOST %" PRIu64 " events!\n", event->lost.lost);
		ret = machine__process_lost_event(machine, event);
	default:
		ret = machine__process_event(machine, event);
		break;
	}

	return ret;
}

static int trace__tool_process(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused,
			       struct machine *machine)
{
	struct trace *trace = container_of(tool, struct trace, tool);
	return trace__process_event(trace, machine, event);
}

static int trace__symbols_init(struct trace *trace, struct perf_evlist *evlist)
{
	int err = symbol__init();

	if (err)
		return err;

	machine__init(&trace->host, "", HOST_KERNEL_ID);
	machine__create_kernel_maps(&trace->host);

	if (perf_target__has_task(&trace->opts.target)) {
		err = perf_event__synthesize_thread_map(&trace->tool, evlist->threads,
							trace__tool_process,
							&trace->host);
	} else {
		err = perf_event__synthesize_threads(&trace->tool, trace__tool_process,
						     &trace->host);
	}

	if (err)
		symbol__exit();

	return err;
}

static int trace__read_syscall_info(struct trace *trace, int id)
{
	char tp_name[128];
	struct syscall *sc;
	const char *name = audit_syscall_to_name(id, trace->audit_machine);

	if (name == NULL)
		return -1;

	if (id > trace->syscalls.max) {
		struct syscall *nsyscalls = realloc(trace->syscalls.table, (id + 1) * sizeof(*sc));

		if (nsyscalls == NULL)
			return -1;

		if (trace->syscalls.max != -1) {
			memset(nsyscalls + trace->syscalls.max + 1, 0,
			       (id - trace->syscalls.max) * sizeof(*sc));
		} else {
			memset(nsyscalls, 0, (id + 1) * sizeof(*sc));
		}

		trace->syscalls.table = nsyscalls;
		trace->syscalls.max   = id;
	}

	sc = trace->syscalls.table + id;
	sc->name = name;

	if (trace->ev_qualifier) {
		bool in = strlist__find(trace->ev_qualifier, name) != NULL;

		if (!(in ^ trace->not_ev_qualifier)) {
			sc->filtered = true;
			/*
			 * No need to do read tracepoint information since this will be
			 * filtered out.
			 */
			return 0;
		}
	}

	sc->fmt  = syscall_fmt__find(sc->name);

	snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->name);
	sc->tp_format = event_format__new("syscalls", tp_name);

	if (sc->tp_format == NULL && sc->fmt && sc->fmt->alias) {
		snprintf(tp_name, sizeof(tp_name), "sys_enter_%s", sc->fmt->alias);
		sc->tp_format = event_format__new("syscalls", tp_name);
	}

	return sc->tp_format != NULL ? 0 : -1;
}

static size_t syscall__scnprintf_args(struct syscall *sc, char *bf, size_t size,
				      unsigned long *args)
{
	int i = 0;
	size_t printed = 0;

	if (sc->tp_format != NULL) {
		struct format_field *field;

		for (field = sc->tp_format->format.fields->next; field; field = field->next) {
			printed += scnprintf(bf + printed, size - printed,
					     "%s%s: %ld", printed ? ", " : "",
					     field->name, args[i++]);
		}
	} else {
		while (i < 6) {
			printed += scnprintf(bf + printed, size - printed,
					     "%sarg%d: %ld",
					     printed ? ", " : "", i, args[i]);
			++i;
		}
	}

	return printed;
}

typedef int (*tracepoint_handler)(struct trace *trace, struct perf_evsel *evsel,
				  struct perf_sample *sample);

static struct syscall *trace__syscall_info(struct trace *trace,
					   struct perf_evsel *evsel,
					   struct perf_sample *sample)
{
	int id = perf_evsel__intval(evsel, sample, "id");

	if (id < 0) {
		fprintf(trace->output, "Invalid syscall %d id, skipping...\n", id);
		return NULL;
	}

	if ((id > trace->syscalls.max || trace->syscalls.table[id].name == NULL) &&
	    trace__read_syscall_info(trace, id))
		goto out_cant_read;

	if ((id > trace->syscalls.max || trace->syscalls.table[id].name == NULL))
		goto out_cant_read;

	return &trace->syscalls.table[id];

out_cant_read:
	fprintf(trace->output, "Problems reading syscall %d", id);
	if (id <= trace->syscalls.max && trace->syscalls.table[id].name != NULL)
		fprintf(trace->output, "(%s)", trace->syscalls.table[id].name);
	fputs(" information", trace->output);
	return NULL;
}

static int trace__sys_enter(struct trace *trace, struct perf_evsel *evsel,
			    struct perf_sample *sample)
{
	char *msg;
	void *args;
	size_t printed = 0;
	struct thread *thread;
	struct syscall *sc = trace__syscall_info(trace, evsel, sample);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	if (sc->filtered)
		return 0;

	thread = machine__findnew_thread(&trace->host, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		return -1;

	args = perf_evsel__rawptr(evsel, sample, "args");
	if (args == NULL) {
		fprintf(trace->output, "Problems reading syscall arguments\n");
		return -1;
	}

	ttrace = thread->priv;

	if (ttrace->entry_str == NULL) {
		ttrace->entry_str = malloc(1024);
		if (!ttrace->entry_str)
			return -1;
	}

	ttrace->entry_time = sample->time;
	msg = ttrace->entry_str;
	printed += scnprintf(msg + printed, 1024 - printed, "%s(", sc->name);

	printed += syscall__scnprintf_args(sc, msg + printed, 1024 - printed,  args);

	if (!strcmp(sc->name, "exit_group") || !strcmp(sc->name, "exit")) {
		if (!trace->duration_filter) {
			trace__fprintf_entry_head(trace, thread, 1, sample->time, trace->output);
			fprintf(trace->output, "%-70s\n", ttrace->entry_str);
		}
	} else
		ttrace->entry_pending = true;

	return 0;
}

static int trace__sys_exit(struct trace *trace, struct perf_evsel *evsel,
			   struct perf_sample *sample)
{
	int ret;
	u64 duration = 0;
	struct thread *thread;
	struct syscall *sc = trace__syscall_info(trace, evsel, sample);
	struct thread_trace *ttrace;

	if (sc == NULL)
		return -1;

	if (sc->filtered)
		return 0;

	thread = machine__findnew_thread(&trace->host, sample->tid);
	ttrace = thread__trace(thread, trace->output);
	if (ttrace == NULL)
		return -1;

	ret = perf_evsel__intval(evsel, sample, "ret");

	ttrace = thread->priv;

	ttrace->exit_time = sample->time;

	if (ttrace->entry_time) {
		duration = sample->time - ttrace->entry_time;
		if (trace__filter_duration(trace, duration))
			goto out;
	} else if (trace->duration_filter)
		goto out;

	trace__fprintf_entry_head(trace, thread, duration, sample->time, trace->output);

	if (ttrace->entry_pending) {
		fprintf(trace->output, "%-70s", ttrace->entry_str);
	} else {
		fprintf(trace->output, " ... [");
		color_fprintf(trace->output, PERF_COLOR_YELLOW, "continued");
		fprintf(trace->output, "]: %s()", sc->name);
	}

	if (ret < 0 && sc->fmt && sc->fmt->errmsg) {
		char bf[256];
		const char *emsg = strerror_r(-ret, bf, sizeof(bf)),
			   *e = audit_errno_to_name(-ret);

		fprintf(trace->output, ") = -1 %s %s", e, emsg);
	} else if (ret == 0 && sc->fmt && sc->fmt->timeout)
		fprintf(trace->output, ") = 0 Timeout");
	else
		fprintf(trace->output, ") = %d", ret);

	fputc('\n', trace->output);
out:
	ttrace->entry_pending = false;

	return 0;
}

static int trace__sched_stat_runtime(struct trace *trace, struct perf_evsel *evsel,
				     struct perf_sample *sample)
{
        u64 runtime = perf_evsel__intval(evsel, sample, "runtime");
	double runtime_ms = (double)runtime / NSEC_PER_MSEC;
	struct thread *thread = machine__findnew_thread(&trace->host, sample->tid);
	struct thread_trace *ttrace = thread__trace(thread, trace->output);

	if (ttrace == NULL)
		goto out_dump;

	ttrace->runtime_ms += runtime_ms;
	trace->runtime_ms += runtime_ms;
	return 0;

out_dump:
	fprintf(trace->output, "%s: comm=%s,pid=%u,runtime=%" PRIu64 ",vruntime=%" PRIu64 ")\n",
	       evsel->name,
	       perf_evsel__strval(evsel, sample, "comm"),
	       (pid_t)perf_evsel__intval(evsel, sample, "pid"),
	       runtime,
	       perf_evsel__intval(evsel, sample, "vruntime"));
	return 0;
}

static int trace__run(struct trace *trace, int argc, const char **argv)
{
	struct perf_evlist *evlist = perf_evlist__new();
	struct perf_evsel *evsel;
	int err = -1, i;
	unsigned long before;
	const bool forks = argc > 0;

	if (evlist == NULL) {
		fprintf(trace->output, "Not enough memory to run!\n");
		goto out;
	}

	if (perf_evlist__add_newtp(evlist, "raw_syscalls", "sys_enter", trace__sys_enter) ||
	    perf_evlist__add_newtp(evlist, "raw_syscalls", "sys_exit", trace__sys_exit)) {
		fprintf(trace->output, "Couldn't read the raw_syscalls tracepoints information!\n");
		goto out_delete_evlist;
	}

	if (trace->sched &&
	    perf_evlist__add_newtp(evlist, "sched", "sched_stat_runtime",
				   trace__sched_stat_runtime)) {
		fprintf(trace->output, "Couldn't read the sched_stat_runtime tracepoint information!\n");
		goto out_delete_evlist;
	}

	err = perf_evlist__create_maps(evlist, &trace->opts.target);
	if (err < 0) {
		fprintf(trace->output, "Problems parsing the target to trace, check your options!\n");
		goto out_delete_evlist;
	}

	err = trace__symbols_init(trace, evlist);
	if (err < 0) {
		fprintf(trace->output, "Problems initializing symbol libraries!\n");
		goto out_delete_maps;
	}

	perf_evlist__config(evlist, &trace->opts);

	signal(SIGCHLD, sig_handler);
	signal(SIGINT, sig_handler);

	if (forks) {
		err = perf_evlist__prepare_workload(evlist, &trace->opts.target,
						    argv, false, false);
		if (err < 0) {
			fprintf(trace->output, "Couldn't run the workload!\n");
			goto out_delete_maps;
		}
	}

	err = perf_evlist__open(evlist);
	if (err < 0) {
		fprintf(trace->output, "Couldn't create the events: %s\n", strerror(errno));
		goto out_delete_maps;
	}

	err = perf_evlist__mmap(evlist, UINT_MAX, false);
	if (err < 0) {
		fprintf(trace->output, "Couldn't mmap the events: %s\n", strerror(errno));
		goto out_close_evlist;
	}

	perf_evlist__enable(evlist);

	if (forks)
		perf_evlist__start_workload(evlist);

	trace->multiple_threads = evlist->threads->map[0] == -1 || evlist->threads->nr > 1;
again:
	before = trace->nr_events;

	for (i = 0; i < evlist->nr_mmaps; i++) {
		union perf_event *event;

		while ((event = perf_evlist__mmap_read(evlist, i)) != NULL) {
			const u32 type = event->header.type;
			tracepoint_handler handler;
			struct perf_sample sample;

			++trace->nr_events;

			err = perf_evlist__parse_sample(evlist, event, &sample);
			if (err) {
				fprintf(trace->output, "Can't parse sample, err = %d, skipping...\n", err);
				continue;
			}

			if (trace->base_time == 0)
				trace->base_time = sample.time;

			if (type != PERF_RECORD_SAMPLE) {
				trace__process_event(trace, &trace->host, event);
				continue;
			}

			evsel = perf_evlist__id2evsel(evlist, sample.id);
			if (evsel == NULL) {
				fprintf(trace->output, "Unknown tp ID %" PRIu64 ", skipping...\n", sample.id);
				continue;
			}

			if (sample.raw_data == NULL) {
				fprintf(trace->output, "%s sample with no payload for tid: %d, cpu %d, raw_size=%d, skipping...\n",
				       perf_evsel__name(evsel), sample.tid,
				       sample.cpu, sample.raw_size);
				continue;
			}

			handler = evsel->handler.func;
			handler(trace, evsel, &sample);
		}
	}

	if (trace->nr_events == before) {
		if (done)
			goto out_unmap_evlist;

		poll(evlist->pollfd, evlist->nr_fds, -1);
	}

	if (done)
		perf_evlist__disable(evlist);

	goto again;

out_unmap_evlist:
	perf_evlist__munmap(evlist);
out_close_evlist:
	perf_evlist__close(evlist);
out_delete_maps:
	perf_evlist__delete_maps(evlist);
out_delete_evlist:
	perf_evlist__delete(evlist);
out:
	return err;
}

static size_t trace__fprintf_threads_header(FILE *fp)
{
	size_t printed;

	printed  = fprintf(fp, "\n _____________________________________________________________________\n");
	printed += fprintf(fp," __)    Summary of events    (__\n\n");
	printed += fprintf(fp,"              [ task - pid ]     [ events ] [ ratio ]  [ runtime ]\n");
	printed += fprintf(fp," _____________________________________________________________________\n\n");

	return printed;
}

static size_t trace__fprintf_thread_summary(struct trace *trace, FILE *fp)
{
	size_t printed = trace__fprintf_threads_header(fp);
	struct rb_node *nd;

	for (nd = rb_first(&trace->host.threads); nd; nd = rb_next(nd)) {
		struct thread *thread = rb_entry(nd, struct thread, rb_node);
		struct thread_trace *ttrace = thread->priv;
		const char *color;
		double ratio;

		if (ttrace == NULL)
			continue;

		ratio = (double)ttrace->nr_events / trace->nr_events * 100.0;

		color = PERF_COLOR_NORMAL;
		if (ratio > 50.0)
			color = PERF_COLOR_RED;
		else if (ratio > 25.0)
			color = PERF_COLOR_GREEN;
		else if (ratio > 5.0)
			color = PERF_COLOR_YELLOW;

		printed += color_fprintf(fp, color, "%20s", thread->comm);
		printed += fprintf(fp, " - %-5d :%11lu   [", thread->tid, ttrace->nr_events);
		printed += color_fprintf(fp, color, "%5.1f%%", ratio);
		printed += fprintf(fp, " ] %10.3f ms\n", ttrace->runtime_ms);
	}

	return printed;
}

static int trace__set_duration(const struct option *opt, const char *str,
			       int unset __maybe_unused)
{
	struct trace *trace = opt->value;

	trace->duration_filter = atof(str);
	return 0;
}

static int trace__open_output(struct trace *trace, const char *filename)
{
	struct stat st;

	if (!stat(filename, &st) && st.st_size) {
		char oldname[PATH_MAX];

		scnprintf(oldname, sizeof(oldname), "%s.old", filename);
		unlink(oldname);
		rename(filename, oldname);
	}

	trace->output = fopen(filename, "w");

	return trace->output == NULL ? -errno : 0;
}

int cmd_trace(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const trace_usage[] = {
		"perf trace [<options>] [<command>]",
		"perf trace [<options>] -- <command> [<options>]",
		NULL
	};
	struct trace trace = {
		.audit_machine = audit_detect_machine(),
		.syscalls = {
			. max = -1,
		},
		.opts = {
			.target = {
				.uid	   = UINT_MAX,
				.uses_mmap = true,
			},
			.user_freq     = UINT_MAX,
			.user_interval = ULLONG_MAX,
			.no_delay      = true,
			.mmap_pages    = 1024,
		},
		.output = stdout,
	};
	const char *output_name = NULL;
	const char *ev_qualifier_str = NULL;
	const struct option trace_options[] = {
	OPT_STRING('e', "expr", &ev_qualifier_str, "expr",
		    "list of events to trace"),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_STRING('p', "pid", &trace.opts.target.pid, "pid",
		    "trace events on existing process id"),
	OPT_STRING('t', "tid", &trace.opts.target.tid, "tid",
		    "trace events on existing thread id"),
	OPT_BOOLEAN('a', "all-cpus", &trace.opts.target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &trace.opts.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_BOOLEAN('i', "no-inherit", &trace.opts.no_inherit,
		    "child tasks do not inherit counters"),
	OPT_UINTEGER('m', "mmap-pages", &trace.opts.mmap_pages,
		     "number of mmap data pages"),
	OPT_STRING('u', "uid", &trace.opts.target.uid_str, "user",
		   "user to profile"),
	OPT_CALLBACK(0, "duration", &trace, "float",
		     "show only events with duration > N.M ms",
		     trace__set_duration),
	OPT_BOOLEAN(0, "sched", &trace.sched, "show blocking scheduler events"),
	OPT_END()
	};
	int err;
	char bf[BUFSIZ];

	argc = parse_options(argc, argv, trace_options, trace_usage, 0);

	if (output_name != NULL) {
		err = trace__open_output(&trace, output_name);
		if (err < 0) {
			perror("failed to create output file");
			goto out;
		}
	}

	if (ev_qualifier_str != NULL) {
		const char *s = ev_qualifier_str;

		trace.not_ev_qualifier = *s == '!';
		if (trace.not_ev_qualifier)
			++s;
		trace.ev_qualifier = strlist__new(true, s);
		if (trace.ev_qualifier == NULL) {
			fputs("Not enough memory to parse event qualifier",
			      trace.output);
			err = -ENOMEM;
			goto out_close;
		}
	}

	err = perf_target__validate(&trace.opts.target);
	if (err) {
		perf_target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	err = perf_target__parse_uid(&trace.opts.target);
	if (err) {
		perf_target__strerror(&trace.opts.target, err, bf, sizeof(bf));
		fprintf(trace.output, "%s", bf);
		goto out_close;
	}

	if (!argc && perf_target__none(&trace.opts.target))
		trace.opts.target.system_wide = true;

	err = trace__run(&trace, argc, argv);

	if (trace.sched && !err)
		trace__fprintf_thread_summary(&trace, trace.output);

out_close:
	if (output_name != NULL)
		fclose(trace.output);
out:
	return err;
}
