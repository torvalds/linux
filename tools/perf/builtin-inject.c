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
#include "util/auxtrace.h"
#include "util/jit.h"

#include <subcmd/parse-options.h>

#include <linux/list.h>

struct perf_inject {
	struct perf_tool	tool;
	struct perf_session	*session;
	bool			build_ids;
	bool			sched_stat;
	bool			have_auxtrace;
	bool			strip;
	bool			jit_mode;
	const char		*input_name;
	struct perf_data_file	output;
	u64			bytes_written;
	u64			aux_id;
	struct list_head	samples;
	struct itrace_synth_opts itrace_synth_opts;
};

struct event_entry {
	struct list_head node;
	u32		 tid;
	union perf_event event[0];
};

static int output_bytes(struct perf_inject *inject, void *buf, size_t sz)
{
	ssize_t size;

	size = perf_data_file__write(&inject->output, buf, sz);
	if (size < 0)
		return -errno;

	inject->bytes_written += size;
	return 0;
}

static int perf_event__repipe_synth(struct perf_tool *tool,
				    union perf_event *event)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);

	return output_bytes(inject, event, event->header.size);
}

static int perf_event__repipe_oe_synth(struct perf_tool *tool,
				       union perf_event *event,
				       struct ordered_events *oe __maybe_unused)
{
	return perf_event__repipe_synth(tool, event);
}

#ifdef HAVE_JITDUMP
static int perf_event__drop_oe(struct perf_tool *tool __maybe_unused,
			       union perf_event *event __maybe_unused,
			       struct ordered_events *oe __maybe_unused)
{
	return 0;
}
#endif

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

#ifdef HAVE_AUXTRACE_SUPPORT

static int copy_bytes(struct perf_inject *inject, int fd, off_t size)
{
	char buf[4096];
	ssize_t ssz;
	int ret;

	while (size > 0) {
		ssz = read(fd, buf, min(size, (off_t)sizeof(buf)));
		if (ssz < 0)
			return -errno;
		ret = output_bytes(inject, buf, ssz);
		if (ret)
			return ret;
		size -= ssz;
	}

	return 0;
}

static s64 perf_event__repipe_auxtrace(struct perf_tool *tool,
				       union perf_event *event,
				       struct perf_session *session)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);
	int ret;

	inject->have_auxtrace = true;

	if (!inject->output.is_pipe) {
		off_t offset;

		offset = lseek(inject->output.fd, 0, SEEK_CUR);
		if (offset == -1)
			return -errno;
		ret = auxtrace_index__auxtrace_event(&session->auxtrace_index,
						     event, offset);
		if (ret < 0)
			return ret;
	}

	if (perf_data_file__is_pipe(session->file) || !session->one_mmap) {
		ret = output_bytes(inject, event, event->header.size);
		if (ret < 0)
			return ret;
		ret = copy_bytes(inject, perf_data_file__fd(session->file),
				 event->auxtrace.size);
	} else {
		ret = output_bytes(inject, event,
				   event->header.size + event->auxtrace.size);
	}
	if (ret < 0)
		return ret;

	return event->auxtrace.size;
}

#else

static s64
perf_event__repipe_auxtrace(struct perf_tool *tool __maybe_unused,
			    union perf_event *event __maybe_unused,
			    struct perf_session *session __maybe_unused)
{
	pr_err("AUX area tracing not supported\n");
	return -EINVAL;
}

#endif

static int perf_event__repipe(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	return perf_event__repipe_synth(tool, event);
}

static int perf_event__drop(struct perf_tool *tool __maybe_unused,
			    union perf_event *event __maybe_unused,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine __maybe_unused)
{
	return 0;
}

static int perf_event__drop_aux(struct perf_tool *tool,
				union perf_event *event __maybe_unused,
				struct perf_sample *sample,
				struct machine *machine __maybe_unused)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);

	if (!inject->aux_id)
		inject->aux_id = sample->id;

	return 0;
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

#ifdef HAVE_JITDUMP
static int perf_event__jit_repipe_mmap(struct perf_tool *tool,
				       union perf_event *event,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	u64 n = 0;
	int ret;

	/*
	 * if jit marker, then inject jit mmaps and generate ELF images
	 */
	ret = jit_process(inject->session, &inject->output, machine,
			  event->mmap.filename, sample->pid, &n);
	if (ret < 0)
		return ret;
	if (ret) {
		inject->bytes_written += n;
		return 0;
	}
	return perf_event__repipe_mmap(tool, event, sample, machine);
}
#endif

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

#ifdef HAVE_JITDUMP
static int perf_event__jit_repipe_mmap2(struct perf_tool *tool,
					union perf_event *event,
					struct perf_sample *sample,
					struct machine *machine)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	u64 n = 0;
	int ret;

	/*
	 * if jit marker, then inject jit mmaps and generate ELF images
	 */
	ret = jit_process(inject->session, &inject->output, machine,
			  event->mmap2.filename, sample->pid, &n);
	if (ret < 0)
		return ret;
	if (ret) {
		inject->bytes_written += n;
		return 0;
	}
	return perf_event__repipe_mmap2(tool, event, sample, machine);
}
#endif

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

static int perf_event__repipe_comm(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_comm(tool, event, sample, machine);
	perf_event__repipe(tool, event, sample, machine);

	return err;
}

static int perf_event__repipe_exit(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_exit(tool, event, sample, machine);
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

static int perf_event__repipe_id_index(struct perf_tool *tool,
				       union perf_event *event,
				       struct perf_session *session)
{
	int err;

	perf_event__repipe_synth(tool, event);
	err = perf_event__process_id_index(tool, event, session);

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

	thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL) {
		pr_err("problem processing %d event, skipping it.\n",
		       event->header.type);
		goto repipe;
	}

	thread__find_addr_map(thread, sample->cpumode, MAP__FUNCTION, sample->ip, &al);

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

	thread__put(thread);
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

static int drop_sample(struct perf_tool *tool __maybe_unused,
		       union perf_event *event __maybe_unused,
		       struct perf_sample *sample __maybe_unused,
		       struct perf_evsel *evsel __maybe_unused,
		       struct machine *machine __maybe_unused)
{
	return 0;
}

static void strip_init(struct perf_inject *inject)
{
	struct perf_evlist *evlist = inject->session->evlist;
	struct perf_evsel *evsel;

	inject->tool.context_switch = perf_event__drop;

	evlist__for_each(evlist, evsel)
		evsel->handler = drop_sample;
}

static bool has_tracking(struct perf_evsel *evsel)
{
	return evsel->attr.mmap || evsel->attr.mmap2 || evsel->attr.comm ||
	       evsel->attr.task;
}

#define COMPAT_MASK (PERF_SAMPLE_ID | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | \
		     PERF_SAMPLE_ID | PERF_SAMPLE_CPU | PERF_SAMPLE_IDENTIFIER)

/*
 * In order that the perf.data file is parsable, tracking events like MMAP need
 * their selected event to exist, except if there is only 1 selected event left
 * and it has a compatible sample type.
 */
static bool ok_to_remove(struct perf_evlist *evlist,
			 struct perf_evsel *evsel_to_remove)
{
	struct perf_evsel *evsel;
	int cnt = 0;
	bool ok = false;

	if (!has_tracking(evsel_to_remove))
		return true;

	evlist__for_each(evlist, evsel) {
		if (evsel->handler != drop_sample) {
			cnt += 1;
			if ((evsel->attr.sample_type & COMPAT_MASK) ==
			    (evsel_to_remove->attr.sample_type & COMPAT_MASK))
				ok = true;
		}
	}

	return ok && cnt == 1;
}

static void strip_fini(struct perf_inject *inject)
{
	struct perf_evlist *evlist = inject->session->evlist;
	struct perf_evsel *evsel, *tmp;

	/* Remove non-synthesized evsels if possible */
	evlist__for_each_safe(evlist, tmp, evsel) {
		if (evsel->handler == drop_sample &&
		    ok_to_remove(evlist, evsel)) {
			pr_debug("Deleting %s\n", perf_evsel__name(evsel));
			perf_evlist__remove(evlist, evsel);
			perf_evsel__delete(evsel);
		}
	}
}

static int __cmd_inject(struct perf_inject *inject)
{
	int ret = -EINVAL;
	struct perf_session *session = inject->session;
	struct perf_data_file *file_out = &inject->output;
	int fd = perf_data_file__fd(file_out);
	u64 output_data_offset;

	signal(SIGINT, sig_handler);

	if (inject->build_ids || inject->sched_stat ||
	    inject->itrace_synth_opts.set) {
		inject->tool.mmap	  = perf_event__repipe_mmap;
		inject->tool.mmap2	  = perf_event__repipe_mmap2;
		inject->tool.fork	  = perf_event__repipe_fork;
		inject->tool.tracing_data = perf_event__repipe_tracing_data;
	}

	output_data_offset = session->header.data_offset;

	if (inject->build_ids) {
		inject->tool.sample = perf_event__inject_buildid;
	} else if (inject->sched_stat) {
		struct perf_evsel *evsel;

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
	} else if (inject->itrace_synth_opts.set) {
		session->itrace_synth_opts = &inject->itrace_synth_opts;
		inject->itrace_synth_opts.inject = true;
		inject->tool.comm	    = perf_event__repipe_comm;
		inject->tool.exit	    = perf_event__repipe_exit;
		inject->tool.id_index	    = perf_event__repipe_id_index;
		inject->tool.auxtrace_info  = perf_event__process_auxtrace_info;
		inject->tool.auxtrace	    = perf_event__process_auxtrace;
		inject->tool.aux	    = perf_event__drop_aux;
		inject->tool.itrace_start   = perf_event__drop_aux,
		inject->tool.ordered_events = true;
		inject->tool.ordering_requires_timestamps = true;
		/* Allow space in the header for new attributes */
		output_data_offset = 4096;
		if (inject->strip)
			strip_init(inject);
	}

	if (!inject->itrace_synth_opts.set)
		auxtrace_index__free(&session->auxtrace_index);

	if (!file_out->is_pipe)
		lseek(fd, output_data_offset, SEEK_SET);

	ret = perf_session__process_events(session);

	if (!file_out->is_pipe) {
		if (inject->build_ids)
			perf_header__set_feat(&session->header,
					      HEADER_BUILD_ID);
		/*
		 * Keep all buildids when there is unprocessed AUX data because
		 * it is not known which ones the AUX trace hits.
		 */
		if (perf_header__has_feat(&session->header, HEADER_BUILD_ID) &&
		    inject->have_auxtrace && !inject->itrace_synth_opts.set)
			dsos__hit_all(session);
		/*
		 * The AUX areas have been removed and replaced with
		 * synthesized hardware events, so clear the feature flag and
		 * remove the evsel.
		 */
		if (inject->itrace_synth_opts.set) {
			struct perf_evsel *evsel;

			perf_header__clear_feat(&session->header,
						HEADER_AUXTRACE);
			if (inject->itrace_synth_opts.last_branch)
				perf_header__set_feat(&session->header,
						      HEADER_BRANCH_STACK);
			evsel = perf_evlist__id2evsel_strict(session->evlist,
							     inject->aux_id);
			if (evsel) {
				pr_debug("Deleting %s\n",
					 perf_evsel__name(evsel));
				perf_evlist__remove(session->evlist, evsel);
				perf_evsel__delete(evsel);
			}
			if (inject->strip)
				strip_fini(inject);
		}
		session->header.data_offset = output_data_offset;
		session->header.data_size = inject->bytes_written;
		perf_session__write_header(session, session->evlist, fd, true);
	}

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
			.lost_samples	= perf_event__repipe,
			.aux		= perf_event__repipe,
			.itrace_start	= perf_event__repipe,
			.context_switch	= perf_event__repipe,
			.read		= perf_event__repipe_sample,
			.throttle	= perf_event__repipe,
			.unthrottle	= perf_event__repipe,
			.attr		= perf_event__repipe_attr,
			.tracing_data	= perf_event__repipe_op2_synth,
			.auxtrace_info	= perf_event__repipe_op2_synth,
			.auxtrace	= perf_event__repipe_auxtrace,
			.auxtrace_error	= perf_event__repipe_op2_synth,
			.time_conv	= perf_event__repipe_op2_synth,
			.finished_round	= perf_event__repipe_oe_synth,
			.build_id	= perf_event__repipe_op2_synth,
			.id_index	= perf_event__repipe_op2_synth,
		},
		.input_name  = "-",
		.samples = LIST_HEAD_INIT(inject.samples),
		.output = {
			.path = "-",
			.mode = PERF_DATA_MODE_WRITE,
		},
	};
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_READ,
	};
	int ret;

	struct option options[] = {
		OPT_BOOLEAN('b', "build-ids", &inject.build_ids,
			    "Inject build-ids into the output stream"),
		OPT_STRING('i', "input", &inject.input_name, "file",
			   "input file name"),
		OPT_STRING('o', "output", &inject.output.path, "file",
			   "output file name"),
		OPT_BOOLEAN('s', "sched-stat", &inject.sched_stat,
			    "Merge sched-stat and sched-switch for getting events "
			    "where and how long tasks slept"),
#ifdef HAVE_JITDUMP
		OPT_BOOLEAN('j', "jit", &inject.jit_mode, "merge jitdump files into perf.data file"),
#endif
		OPT_INCR('v', "verbose", &verbose,
			 "be more verbose (show build ids, etc)"),
		OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name, "file",
			   "kallsyms pathname"),
		OPT_BOOLEAN('f', "force", &file.force, "don't complain, do it"),
		OPT_CALLBACK_OPTARG(0, "itrace", &inject.itrace_synth_opts,
				    NULL, "opts", "Instruction Tracing options",
				    itrace_parse_synth_opts),
		OPT_BOOLEAN(0, "strip", &inject.strip,
			    "strip non-synthesized events (use with --itrace)"),
		OPT_END()
	};
	const char * const inject_usage[] = {
		"perf inject [<options>]",
		NULL
	};
#ifndef HAVE_JITDUMP
	set_option_nobuild(options, 'j', "jit", "NO_LIBELF=1", true);
#endif
	argc = parse_options(argc, argv, options, inject_usage, 0);

	/*
	 * Any (unrecognized) arguments left?
	 */
	if (argc)
		usage_with_options(inject_usage, options);

	if (inject.strip && !inject.itrace_synth_opts.set) {
		pr_err("--strip option requires --itrace option\n");
		return -1;
	}

	if (perf_data_file__open(&inject.output)) {
		perror("failed to create output file");
		return -1;
	}

	inject.tool.ordered_events = inject.sched_stat;

	file.path = inject.input_name;
	inject.session = perf_session__new(&file, true, &inject.tool);
	if (inject.session == NULL)
		return -1;

	if (inject.build_ids) {
		/*
		 * to make sure the mmap records are ordered correctly
		 * and so that the correct especially due to jitted code
		 * mmaps. We cannot generate the buildid hit list and
		 * inject the jit mmaps at the same time for now.
		 */
		inject.tool.ordered_events = true;
		inject.tool.ordering_requires_timestamps = true;
	}
#ifdef HAVE_JITDUMP
	if (inject.jit_mode) {
		inject.tool.mmap2	   = perf_event__jit_repipe_mmap2;
		inject.tool.mmap	   = perf_event__jit_repipe_mmap;
		inject.tool.ordered_events = true;
		inject.tool.ordering_requires_timestamps = true;
		/*
		 * JIT MMAP injection injects all MMAP events in one go, so it
		 * does not obey finished_round semantics.
		 */
		inject.tool.finished_round = perf_event__drop_oe;
	}
#endif
	ret = symbol__init(&inject.session->header.env);
	if (ret < 0)
		goto out_delete;

	ret = __cmd_inject(&inject);

out_delete:
	perf_session__delete(inject.session);
	return ret;
}
