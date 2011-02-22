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
#include "util/debug.h"

#include "util/parse-options.h"

static char		const *input_name = "-";
static bool		inject_build_ids;

static int event__repipe_synth(event_t *event,
			       struct perf_session *session __used)
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

static int event__repipe(event_t *event, struct sample_data *sample __used,
			 struct perf_session *session)
{
	return event__repipe_synth(event, session);
}

static int event__repipe_mmap(event_t *self, struct sample_data *sample,
			      struct perf_session *session)
{
	int err;

	err = event__process_mmap(self, sample, session);
	event__repipe(self, sample, session);

	return err;
}

static int event__repipe_task(event_t *self, struct sample_data *sample,
			      struct perf_session *session)
{
	int err;

	err = event__process_task(self, sample, session);
	event__repipe(self, sample, session);

	return err;
}

static int event__repipe_tracing_data(event_t *self,
				      struct perf_session *session)
{
	int err;

	event__repipe_synth(self, session);
	err = event__process_tracing_data(self, session);

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

static int dso__inject_build_id(struct dso *self, struct perf_session *session)
{
	u16 misc = PERF_RECORD_MISC_USER;
	struct machine *machine;
	int err;

	if (dso__read_build_id(self) < 0) {
		pr_debug("no build_id found for %s\n", self->long_name);
		return -1;
	}

	machine = perf_session__find_host_machine(session);
	if (machine == NULL) {
		pr_err("Can't find machine for session\n");
		return -1;
	}

	if (self->kernel)
		misc = PERF_RECORD_MISC_KERNEL;

	err = event__synthesize_build_id(self, misc, event__repipe,
					 machine, session);
	if (err) {
		pr_err("Can't synthesize build_id event for %s\n", self->long_name);
		return -1;
	}

	return 0;
}

static int event__inject_buildid(event_t *event, struct sample_data *sample,
				 struct perf_session *session)
{
	struct addr_location al;
	struct thread *thread;
	u8 cpumode;

	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	thread = perf_session__findnew(session, event->ip.pid);
	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
		       event->header.type);
		goto repipe;
	}

	thread__find_addr_map(thread, session, cpumode, MAP__FUNCTION,
			      event->ip.pid, event->ip.ip, &al);

	if (al.map != NULL) {
		if (!al.map->dso->hit) {
			al.map->dso->hit = 1;
			if (map__load(al.map, NULL) >= 0) {
				dso__inject_build_id(al.map->dso, session);
				/*
				 * If this fails, too bad, let the other side
				 * account this as unresolved.
				 */
			} else
				pr_warning("no symbols found in %s, maybe "
					   "install a debug package?\n",
					   al.map->dso->long_name);
		}
	}

repipe:
	event__repipe(event, sample, session);
	return 0;
}

struct perf_event_ops inject_ops = {
	.sample		= event__repipe,
	.mmap		= event__repipe,
	.comm		= event__repipe,
	.fork		= event__repipe,
	.exit		= event__repipe,
	.lost		= event__repipe,
	.read		= event__repipe,
	.throttle	= event__repipe,
	.unthrottle	= event__repipe,
	.attr		= event__repipe_synth,
	.event_type 	= event__repipe_synth,
	.tracing_data 	= event__repipe_synth,
	.build_id 	= event__repipe_synth,
};

extern volatile int session_done;

static void sig_handler(int sig __attribute__((__unused__)))
{
	session_done = 1;
}

static int __cmd_inject(void)
{
	struct perf_session *session;
	int ret = -EINVAL;

	signal(SIGINT, sig_handler);

	if (inject_build_ids) {
		inject_ops.sample	= event__inject_buildid;
		inject_ops.mmap		= event__repipe_mmap;
		inject_ops.fork		= event__repipe_task;
		inject_ops.tracing_data	= event__repipe_tracing_data;
	}

	session = perf_session__new(input_name, O_RDONLY, false, true, &inject_ops);
	if (session == NULL)
		return -ENOMEM;

	ret = perf_session__process_events(session, &inject_ops);

	perf_session__delete(session);

	return ret;
}

static const char * const report_usage[] = {
	"perf inject [<options>]",
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('b', "build-ids", &inject_build_ids,
		    "Inject build-ids into the output stream"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show build ids, etc)"),
	OPT_END()
};

int cmd_inject(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, report_usage, 0);

	/*
	 * Any (unrecognized) arguments left?
	 */
	if (argc)
		usage_with_options(report_usage, options);

	if (symbol__init() < 0)
		return -1;

	return __cmd_inject();
}
