/*
 * builtin-inject.c
 *
 * Builtin inject command: Examine the live mode (stdin) event stream
 * and repipe it to stdout while optionally injecting additional
 * events into it.
 */
#include "builtin.h"

#include "perf.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/debug.h"

#include "util/parse-options.h"

struct perf_inject {
	struct perf_tool tool;
	bool		 build_ids;
};

static int perf_event__repipe_synth(struct perf_tool *tool __maybe_unused,
				    union perf_event *event,
				    struct machine *machine __maybe_unused)
{
	uint32_t size;
	void *buf = event;

	size = event->header.size;

	while (size) {
		int ret = write(STDOUT_FILENO, buf, size);
		if (ret < 0)
			return -errno;

		size -= ret;
		buf += ret;
	}

	return 0;
}

static int perf_event__repipe_op2_synth(struct perf_tool *tool,
					union perf_event *event,
					struct perf_session *session
					__maybe_unused)
{
	return perf_event__repipe_synth(tool, event, NULL);
}

static int perf_event__repipe_event_type_synth(struct perf_tool *tool,
					       union perf_event *event)
{
	return perf_event__repipe_synth(tool, event, NULL);
}

static int perf_event__repipe_tracing_data_synth(union perf_event *event,
						 struct perf_session *session
						 __maybe_unused)
{
	return perf_event__repipe_synth(NULL, event, NULL);
}

static int perf_event__repipe_attr(union perf_event *event,
				   struct perf_evlist **pevlist __maybe_unused)
{
	int ret;
	ret = perf_event__process_attr(event, pevlist);
	if (ret)
		return ret;

	return perf_event__repipe_synth(NULL, event, NULL);
}

static int perf_event__repipe(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine)
{
	return perf_event__repipe_synth(tool, event, machine);
}

static int perf_event__repipe_sample(struct perf_tool *tool,
				     union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct perf_evsel *evsel __maybe_unused,
			      struct machine *machine)
{
	return perf_event__repipe_synth(tool, event, machine);
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

static int perf_event__repipe_task(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_task(tool, event, sample, machine);
	perf_event__repipe(tool, event, sample, machine);

	return err;
}

static int perf_event__repipe_tracing_data(union perf_event *event,
					   struct perf_session *session)
{
	int err;

	perf_event__repipe_synth(NULL, event, NULL);
	err = perf_event__process_tracing_data(event, session);

	return err;
}

static int dso__read_build_id(struct dso *self)
{
	if (self->has_build_id)
		return 0;

	if (filename__read_build_id(self->long_name, self->build_id,
				    sizeof(self->build_id)) > 0) {
		self->has_build_id = true;
		return 0;
	}

	return -1;
}

static int dso__inject_build_id(struct dso *self, struct perf_tool *tool,
				struct machine *machine)
{
	u16 misc = PERF_RECORD_MISC_USER;
	int err;

	if (dso__read_build_id(self) < 0) {
		pr_debug("no build_id found for %s\n", self->long_name);
		return -1;
	}

	if (self->kernel)
		misc = PERF_RECORD_MISC_KERNEL;

	err = perf_event__synthesize_build_id(tool, self, misc, perf_event__repipe,
					      machine);
	if (err) {
		pr_err("Can't synthesize build_id event for %s\n", self->long_name);
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

	thread = machine__findnew_thread(machine, event->ip.pid);
	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
		       event->header.type);
		goto repipe;
	}

	thread__find_addr_map(thread, machine, cpumode, MAP__FUNCTION,
			      event->ip.ip, &al);

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
#ifdef LIBELF_SUPPORT
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

extern volatile int session_done;

static void sig_handler(int sig __maybe_unused)
{
	session_done = 1;
}

static int __cmd_inject(struct perf_inject *inject)
{
	struct perf_session *session;
	int ret = -EINVAL;

	signal(SIGINT, sig_handler);

	if (inject->build_ids) {
		inject->tool.sample	  = perf_event__inject_buildid;
		inject->tool.mmap	  = perf_event__repipe_mmap;
		inject->tool.fork	  = perf_event__repipe_task;
		inject->tool.tracing_data = perf_event__repipe_tracing_data;
	}

	session = perf_session__new("-", O_RDONLY, false, true, &inject->tool);
	if (session == NULL)
		return -ENOMEM;

	ret = perf_session__process_events(session, &inject->tool);

	perf_session__delete(session);

	return ret;
}

int cmd_inject(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct perf_inject inject = {
		.tool = {
			.sample		= perf_event__repipe_sample,
			.mmap		= perf_event__repipe,
			.comm		= perf_event__repipe,
			.fork		= perf_event__repipe,
			.exit		= perf_event__repipe,
			.lost		= perf_event__repipe,
			.read		= perf_event__repipe_sample,
			.throttle	= perf_event__repipe,
			.unthrottle	= perf_event__repipe,
			.attr		= perf_event__repipe_attr,
			.event_type	= perf_event__repipe_event_type_synth,
			.tracing_data	= perf_event__repipe_tracing_data_synth,
			.build_id	= perf_event__repipe_op2_synth,
		},
	};
	const struct option options[] = {
		OPT_BOOLEAN('b', "build-ids", &inject.build_ids,
			    "Inject build-ids into the output stream"),
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

	if (symbol__init() < 0)
		return -1;

	return __cmd_inject(&inject);
}
