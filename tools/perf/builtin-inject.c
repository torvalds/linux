/*
 * builtin-inject.c
 *
 * Builtin inject command: Examine the live mode (stdin) event stream
 * and repipe it to stdout while optionally injecting additional
 * events into it.
 */
#include "builtin.h"

#include "perf.h"
#include "util/color.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/debug.h"
#include "util/build-id.h"
#include "util/data.h"

#include "util/parse-options.h"

#include <linux/list.h>

struct perf_inject {
	struct perf_tool	tool;
	bool			build_ids;
	bool			sched_stat;
	const char		*input_name;
	struct perf_data_file	output;
	u64			bytes_written;
	struct list_head	samples;
};

struct event_entry {
	struct list_head node;
	u32		 tid;
	union perf_event event[0];
};

static int perf_event__repipe_synth(struct perf_tool *tool,
				    union perf_event *event)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	ssize_t size;

	size = perf_data_file__write(&inject->output, event,
				     event->header.size);
	if (size < 0)
		return -errno;

	inject->bytes_written += size;
	return 0;
}

static int perf_event__repipe_op2_synth(struct perf_tool *tool,
					union perf_event *event,
					struct perf_session *session
					__maybe_unused)
{
	return perf_event__repipe_synth(tool, event);
}

static int perf_event__repipe_attr(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_evlist **pevlist)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);
	int ret;

	ret = perf_event__process_attr(tool, event, pevlist);
	if (ret)
		return ret;

	if (!inject->output.is_pipe)
		return 0;

	return perf_event__repipe_synth(tool, event);
}

static int perf_event__repipe(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	return perf_event__repipe_synth(tool, event);
}

typedef int (*inject_handler)(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample,
			      struct perf_evsel *evsel,
			      struct machine *machine);

static int perf_event__repipe_sample(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct perf_evsel *evsel,
				     struct machine *machine)
{
	if (evsel->handler) {
		inject_handler f = evsel->handler;
		return f(tool, event, sample, evsel, machine);
	}

	build_id__mark_dso_hit(tool, event, sample, evsel, machine);

	return perf_event__repipe_synth(tool, event);
}

static int perf_event__repipe_mmap(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_mmap(tool, event, sample, machine);
	perf_event__repipe(tool, event, sample, machine);

	return err;
}

static int perf_event__repipe_mmap2(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_mmap2(tool, event, sample, machine);
	perf_event__repipe(tool, event, sample, machine);

	return err;
}

static int perf_event__repipe_fork(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_fork(tool, event, sample, machine);
	perf_event__repipe(tool, event, sample, machine);

	return err;
}

static int perf_event__repipe_tracing_data(struct perf_tool *tool,
					   union perf_event *event,
					   struct perf_session *session)
{
	int err;

	perf_event__repipe_synth(tool, event);
	err = perf_event__process_tracing_data(tool, event, session);

	return err;
}

static int dso__read_build_id(struct dso *dso)
{
	if (dso->has_build_id)
		return 0;

	if (filename__read_build_id(dso->long_name, dso->build_id,
				    sizeof(dso->build_id)) > 0) {
		dso->has_build_id = true;
		return 0;
	}

	return -1;
}

static int dso__inject_build_id(struct dso *dso, struct perf_tool *tool,
				struct machine *machine)
{
	u16 misc = PERF_RECORD_MISC_USER;
	int err;

	if (dso__read_build_id(dso) < 0) {
		pr_debug("no build_id found for %s\n", dso->long_name);
		return -1;
	}

	if (dso->kernel)
		misc = PERF_RECORD_MISC_KERNEL;

	err = perf_event__synthesize_build_id(tool, dso, misc, perf_event__repipe,
					      machine);
	if (err) {
		pr_err("Can't synthesize build_id event for %s\n", dso->long_name);
		return -1;
	}

	return 0;
}

static int perf_event__inject_buildid(struct perf_tool *tool,
				      union perf_event *event,
				      struct perf_sample *sample,
				      struct perf_evsel *evsel __maybe_unused,
				      struct machine *machine)
{
	struct addr_location al;
	struct thread *thread;
	u8 cpumode;

	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
		       event->header.type);
		goto repipe;
	}

	thread__find_addr_map(thread, machine, cpumode, MAP__FUNCTION,
			      sample->ip, &al);

	if (al.map != NULL) {
		if (!al.map->dso->hit) {
			al.map->dso->hit = 1;
			if (map__load(al.map, NULL) >= 0) {
				dso__inject_build_id(al.map->dso, tool, machine);
				/*
				 * If this fails, too bad, let the other side
				 * account this as unresolved.
				 */
			} else {
#ifdef HAVE_LIBELF_SUPPORT
				pr_warning("no symbols found in %s, maybe "
					   "install a debug package?\n",
					   al.map->dso->long_name);
#endif
			}
		}
	}

repipe:
	perf_event__repipe(tool, event, sample, machine);
	return 0;
}

static int perf_inject__sched_process_exit(struct perf_tool *tool,
					   union perf_event *event __maybe_unused,
					   struct perf_sample *sample,
					   struct perf_evsel *evsel __maybe_unused,
					   struct machine *machine __maybe_unused)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	struct event_entry *ent;

	list_for_each_entry(ent, &inject->samples, node) {
		if (sample->tid == ent->tid) {
			list_del_init(&ent->node);
			free(ent);
			break;
		}
	}

	return 0;
}

static int perf_inject__sched_switch(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct perf_evsel *evsel,
				     struct machine *machine)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	struct event_entry *ent;

	perf_inject__sched_process_exit(tool, event, sample, evsel, machine);

	ent = malloc(event->header.size + sizeof(struct event_entry));
	if (ent == NULL) {
		color_fprintf(stderr, PERF_COLOR_RED,
			     "Not enough memory to process sched switch event!");
		return -1;
	}

	ent->tid = sample->tid;
	memcpy(&ent->event, event, event->header.size);
	list_add(&ent->node, &inject->samples);
	return 0;
}

static int perf_inject__sched_stat(struct perf_tool *tool,
				   union perf_event *event __maybe_unused,
				   struct perf_sample *sample,
				   struct perf_evsel *evsel,
				   struct machine *machine)
{
	struct event_entry *ent;
	union perf_event *event_sw;
	struct perf_sample sample_sw;
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	u32 pid = perf_evsel__intval(evsel, sample, "pid");

	list_for_each_entry(ent, &inject->samples, node) {
		if (pid == ent->tid)
			goto found;
	}

	return 0;
found:
	event_sw = &ent->event[0];
	perf_evsel__parse_sample(evsel, event_sw, &sample_sw);

	sample_sw.period = sample->period;
	sample_sw.time	 = sample->time;
	perf_event__synthesize_sample(event_sw, evsel->attr.sample_type,
				      evsel->attr.read_format, &sample_sw,
				      false);
	build_id__mark_dso_hit(tool, event_sw, &sample_sw, evsel, machine);
	return perf_event__repipe(tool, event_sw, &sample_sw, machine);
}

static void sig_handler(int sig __maybe_unused)
{
	session_done = 1;
}

static int perf_evsel__check_stype(struct perf_evsel *evsel,
				   u64 sample_type, const char *sample_msg)
{
	struct perf_event_attr *attr = &evsel->attr;
	const char *name = perf_evsel__name(evsel);

	if (!(attr->sample_type & sample_type)) {
		pr_err("Samples for %s event do not have %s attribute set.",
			name, sample_msg);
		return -EINVAL;
	}

	return 0;
}

static int __cmd_inject(struct perf_inject *inject)
{
	struct perf_session *session;
	int ret = -EINVAL;
	struct perf_data_file file = {
		.path = inject->input_name,
		.mode = PERF_DATA_MODE_READ,
	};
	struct perf_data_file *file_out = &inject->output;

	signal(SIGINT, sig_handler);

	if (inject->build_ids || inject->sched_stat) {
		inject->tool.mmap	  = perf_event__repipe_mmap;
		inject->tool.mmap2	  = perf_event__repipe_mmap2;
		inject->tool.fork	  = perf_event__repipe_fork;
		inject->tool.tracing_data = perf_event__repipe_tracing_data;
	}

	session = perf_session__new(&file, true, &inject->tool);
	if (session == NULL)
		return -ENOMEM;

	if (inject->build_ids) {
		inject->tool.sample = perf_event__inject_buildid;
	} else if (inject->sched_stat) {
		struct perf_evsel *evsel;

		inject->tool.ordered_samples = true;

		evlist__for_each(session->evlist, evsel) {
			const char *name = perf_evsel__name(evsel);

			if (!strcmp(name, "sched:sched_switch")) {
				if (perf_evsel__check_stype(evsel, PERF_SAMPLE_TID, "TID"))
					return -EINVAL;

				evsel->handler = perf_inject__sched_switch;
			} else if (!strcmp(name, "sched:sched_process_exit"))
				evsel->handler = perf_inject__sched_process_exit;
			else if (!strncmp(name, "sched:sched_stat_", 17))
				evsel->handler = perf_inject__sched_stat;
		}
	}

	if (!file_out->is_pipe)
		lseek(file_out->fd, session->header.data_offset, SEEK_SET);

	ret = perf_session__process_events(session, &inject->tool);

	if (!file_out->is_pipe) {
		session->header.data_size = inject->bytes_written;
		perf_session__write_header(session, session->evlist, file_out->fd, true);
	}

	perf_session__delete(session);

	return ret;
}

int cmd_inject(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct perf_inject inject = {
		.tool = {
			.sample		= perf_event__repipe_sample,
			.mmap		= perf_event__repipe,
			.mmap2		= perf_event__repipe,
			.comm		= perf_event__repipe,
			.fork		= perf_event__repipe,
			.exit		= perf_event__repipe,
			.lost		= perf_event__repipe,
			.read		= perf_event__repipe_sample,
			.throttle	= perf_event__repipe,
			.unthrottle	= perf_event__repipe,
			.attr		= perf_event__repipe_attr,
			.tracing_data	= perf_event__repipe_op2_synth,
			.finished_round	= perf_event__repipe_op2_synth,
			.build_id	= perf_event__repipe_op2_synth,
		},
		.input_name  = "-",
		.samples = LIST_HEAD_INIT(inject.samples),
		.output = {
			.path = "-",
			.mode = PERF_DATA_MODE_WRITE,
		},
	};
	const struct option options[] = {
		OPT_BOOLEAN('b', "build-ids", &inject.build_ids,
			    "Inject build-ids into the output stream"),
		OPT_STRING('i', "input", &inject.input_name, "file",
			   "input file name"),
		OPT_STRING('o', "output", &inject.output.path, "file",
			   "output file name"),
		OPT_BOOLEAN('s', "sched-stat", &inject.sched_stat,
			    "Merge sched-stat and sched-switch for getting events "
			    "where and how long tasks slept"),
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show build ids, etc)"),
		OPT_END()
	};
	const char * const inject_usage[] = {
		"perf inject [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, inject_usage, 0);

	/*
	 * Any (unrecognized) arguments left?
	 */
	if (argc)
		usage_with_options(inject_usage, options);

	if (perf_data_file__open(&inject.output)) {
		perror("failed to create output file");
		return -1;
	}

	if (symbol__init() < 0)
		return -1;

	return __cmd_inject(&inject);
}
