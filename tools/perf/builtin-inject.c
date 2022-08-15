// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-inject.c
 *
 * Builtin inject command: Examine the live mode (stdin) event stream
 * and repipe it to stdout while optionally injecting additional
 * events into it.
 */
#include "builtin.h"

#include "util/color.h"
#include "util/dso.h"
#include "util/vdso.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/map.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/debug.h"
#include "util/build-id.h"
#include "util/data.h"
#include "util/auxtrace.h"
#include "util/jit.h"
#include "util/string2.h"
#include "util/symbol.h"
#include "util/synthetic-events.h"
#include "util/thread.h"
#include "util/namespaces.h"
#include "util/util.h"
#include "util/tsc.h"

#include <internal/lib.h>

#include <linux/err.h>
#include <subcmd/parse-options.h>
#include <uapi/linux/mman.h> /* To get things like MAP_HUGETLB even on older libc headers */

#include <linux/list.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <linux/hash.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <inttypes.h>

struct guest_event {
	struct perf_sample		sample;
	union perf_event		*event;
	char				event_buf[PERF_SAMPLE_MAX_SIZE];
};

struct guest_id {
	/* hlist_node must be first, see free_hlist() */
	struct hlist_node		node;
	u64				id;
	u64				host_id;
	u32				vcpu;
};

struct guest_tid {
	/* hlist_node must be first, see free_hlist() */
	struct hlist_node		node;
	/* Thread ID of QEMU thread */
	u32				tid;
	u32				vcpu;
};

struct guest_vcpu {
	/* Current host CPU */
	u32				cpu;
	/* Thread ID of QEMU thread */
	u32				tid;
};

struct guest_session {
	char				*perf_data_file;
	u32				machine_pid;
	u64				time_offset;
	double				time_scale;
	struct perf_tool		tool;
	struct perf_data		data;
	struct perf_session		*session;
	char				*tmp_file_name;
	int				tmp_fd;
	struct perf_tsc_conversion	host_tc;
	struct perf_tsc_conversion	guest_tc;
	bool				copy_kcore_dir;
	bool				have_tc;
	bool				fetched;
	bool				ready;
	u16				dflt_id_hdr_size;
	u64				dflt_id;
	u64				highest_id;
	/* Array of guest_vcpu */
	struct guest_vcpu		*vcpu;
	size_t				vcpu_cnt;
	/* Hash table for guest_id */
	struct hlist_head		heads[PERF_EVLIST__HLIST_SIZE];
	/* Hash table for guest_tid */
	struct hlist_head		tids[PERF_EVLIST__HLIST_SIZE];
	/* Place to stash next guest event */
	struct guest_event		ev;
};

struct perf_inject {
	struct perf_tool	tool;
	struct perf_session	*session;
	bool			build_ids;
	bool			build_id_all;
	bool			sched_stat;
	bool			have_auxtrace;
	bool			strip;
	bool			jit_mode;
	bool			in_place_update;
	bool			in_place_update_dry_run;
	bool			is_pipe;
	bool			copy_kcore_dir;
	const char		*input_name;
	struct perf_data	output;
	u64			bytes_written;
	u64			aux_id;
	struct list_head	samples;
	struct itrace_synth_opts itrace_synth_opts;
	char			event_copy[PERF_SAMPLE_MAX_SIZE];
	struct perf_file_section secs[HEADER_FEAT_BITS];
	struct guest_session	guest_session;
	struct strlist		*known_build_ids;
};

struct event_entry {
	struct list_head node;
	u32		 tid;
	union perf_event event[];
};

static int dso__inject_build_id(struct dso *dso, struct perf_tool *tool,
				struct machine *machine, u8 cpumode, u32 flags);

static int output_bytes(struct perf_inject *inject, void *buf, size_t sz)
{
	ssize_t size;

	size = perf_data__write(&inject->output, buf, sz);
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

static int perf_event__repipe_op2_synth(struct perf_session *session,
					union perf_event *event)
{
	return perf_event__repipe_synth(session->tool, event);
}

static int perf_event__repipe_op4_synth(struct perf_session *session,
					union perf_event *event,
					u64 data __maybe_unused,
					const char *str __maybe_unused)
{
	return perf_event__repipe_synth(session->tool, event);
}

static int perf_event__repipe_attr(struct perf_tool *tool,
				   union perf_event *event,
				   struct evlist **pevlist)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);
	int ret;

	ret = perf_event__process_attr(tool, event, pevlist);
	if (ret)
		return ret;

	if (!inject->is_pipe)
		return 0;

	return perf_event__repipe_synth(tool, event);
}

static int perf_event__repipe_event_update(struct perf_tool *tool,
					   union perf_event *event,
					   struct evlist **pevlist __maybe_unused)
{
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

static s64 perf_event__repipe_auxtrace(struct perf_session *session,
				       union perf_event *event)
{
	struct perf_tool *tool = session->tool;
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);
	int ret;

	inject->have_auxtrace = true;

	if (!inject->output.is_pipe) {
		off_t offset;

		offset = lseek(inject->output.file.fd, 0, SEEK_CUR);
		if (offset == -1)
			return -errno;
		ret = auxtrace_index__auxtrace_event(&session->auxtrace_index,
						     event, offset);
		if (ret < 0)
			return ret;
	}

	if (perf_data__is_pipe(session->data) || !session->one_mmap) {
		ret = output_bytes(inject, event, event->header.size);
		if (ret < 0)
			return ret;
		ret = copy_bytes(inject, perf_data__fd(session->data),
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
perf_event__repipe_auxtrace(struct perf_session *session __maybe_unused,
			    union perf_event *event __maybe_unused)
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

static union perf_event *
perf_inject__cut_auxtrace_sample(struct perf_inject *inject,
				 union perf_event *event,
				 struct perf_sample *sample)
{
	size_t sz1 = sample->aux_sample.data - (void *)event;
	size_t sz2 = event->header.size - sample->aux_sample.size - sz1;
	union perf_event *ev = (union perf_event *)inject->event_copy;

	if (sz1 > event->header.size || sz2 > event->header.size ||
	    sz1 + sz2 > event->header.size ||
	    sz1 < sizeof(struct perf_event_header) + sizeof(u64))
		return event;

	memcpy(ev, event, sz1);
	memcpy((void *)ev + sz1, (void *)event + event->header.size - sz2, sz2);
	ev->header.size = sz1 + sz2;
	((u64 *)((void *)ev + sz1))[-1] = 0;

	return ev;
}

typedef int (*inject_handler)(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample,
			      struct evsel *evsel,
			      struct machine *machine);

static int perf_event__repipe_sample(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct evsel *evsel,
				     struct machine *machine)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);

	if (evsel && evsel->handler) {
		inject_handler f = evsel->handler;
		return f(tool, event, sample, evsel, machine);
	}

	build_id__mark_dso_hit(tool, event, sample, evsel, machine);

	if (inject->itrace_synth_opts.set && sample->aux_sample.size)
		event = perf_inject__cut_auxtrace_sample(inject, event, sample);

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
			  event->mmap.filename, event->mmap.pid, event->mmap.tid, &n);
	if (ret < 0)
		return ret;
	if (ret) {
		inject->bytes_written += n;
		return 0;
	}
	return perf_event__repipe_mmap(tool, event, sample, machine);
}
#endif

static struct dso *findnew_dso(int pid, int tid, const char *filename,
			       struct dso_id *id, struct machine *machine)
{
	struct thread *thread;
	struct nsinfo *nsi = NULL;
	struct nsinfo *nnsi;
	struct dso *dso;
	bool vdso;

	thread = machine__findnew_thread(machine, pid, tid);
	if (thread == NULL) {
		pr_err("cannot find or create a task %d/%d.\n", tid, pid);
		return NULL;
	}

	vdso = is_vdso_map(filename);
	nsi = nsinfo__get(thread->nsinfo);

	if (vdso) {
		/* The vdso maps are always on the host and not the
		 * container.  Ensure that we don't use setns to look
		 * them up.
		 */
		nnsi = nsinfo__copy(nsi);
		if (nnsi) {
			nsinfo__put(nsi);
			nsinfo__clear_need_setns(nnsi);
			nsi = nnsi;
		}
		dso = machine__findnew_vdso(machine, thread);
	} else {
		dso = machine__findnew_dso_id(machine, filename, id);
	}

	if (dso) {
		nsinfo__put(dso->nsinfo);
		dso->nsinfo = nsi;
	} else
		nsinfo__put(nsi);

	thread__put(thread);
	return dso;
}

static int perf_event__repipe_buildid_mmap(struct perf_tool *tool,
					   union perf_event *event,
					   struct perf_sample *sample,
					   struct machine *machine)
{
	struct dso *dso;

	dso = findnew_dso(event->mmap.pid, event->mmap.tid,
			  event->mmap.filename, NULL, machine);

	if (dso && !dso->hit) {
		dso->hit = 1;
		dso__inject_build_id(dso, tool, machine, sample->cpumode, 0);
	}
	dso__put(dso);

	return perf_event__repipe(tool, event, sample, machine);
}

static int perf_event__repipe_mmap2(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine)
{
	int err;

	err = perf_event__process_mmap2(tool, event, sample, machine);
	perf_event__repipe(tool, event, sample, machine);

	if (event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID) {
		struct dso *dso;

		dso = findnew_dso(event->mmap2.pid, event->mmap2.tid,
				  event->mmap2.filename, NULL, machine);
		if (dso) {
			/* mark it not to inject build-id */
			dso->hit = 1;
		}
		dso__put(dso);
	}

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
			  event->mmap2.filename, event->mmap2.pid, event->mmap2.tid, &n);
	if (ret < 0)
		return ret;
	if (ret) {
		inject->bytes_written += n;
		return 0;
	}
	return perf_event__repipe_mmap2(tool, event, sample, machine);
}
#endif

static int perf_event__repipe_buildid_mmap2(struct perf_tool *tool,
					    union perf_event *event,
					    struct perf_sample *sample,
					    struct machine *machine)
{
	struct dso_id dso_id = {
		.maj = event->mmap2.maj,
		.min = event->mmap2.min,
		.ino = event->mmap2.ino,
		.ino_generation = event->mmap2.ino_generation,
	};
	struct dso *dso;

	if (event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID) {
		/* cannot use dso_id since it'd have invalid info */
		dso = findnew_dso(event->mmap2.pid, event->mmap2.tid,
				  event->mmap2.filename, NULL, machine);
		if (dso) {
			/* mark it not to inject build-id */
			dso->hit = 1;
		}
		dso__put(dso);
		return 0;
	}

	dso = findnew_dso(event->mmap2.pid, event->mmap2.tid,
			  event->mmap2.filename, &dso_id, machine);

	if (dso && !dso->hit) {
		dso->hit = 1;
		dso__inject_build_id(dso, tool, machine, sample->cpumode,
				     event->mmap2.flags);
	}
	dso__put(dso);

	perf_event__repipe(tool, event, sample, machine);

	return 0;
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

static int perf_event__repipe_namespaces(struct perf_tool *tool,
					 union perf_event *event,
					 struct perf_sample *sample,
					 struct machine *machine)
{
	int err = perf_event__process_namespaces(tool, event, sample, machine);

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

static int perf_event__repipe_tracing_data(struct perf_session *session,
					   union perf_event *event)
{
	perf_event__repipe_synth(session->tool, event);

	return perf_event__process_tracing_data(session, event);
}

static int dso__read_build_id(struct dso *dso)
{
	struct nscookie nsc;

	if (dso->has_build_id)
		return 0;

	nsinfo__mountns_enter(dso->nsinfo, &nsc);
	if (filename__read_build_id(dso->long_name, &dso->bid) > 0)
		dso->has_build_id = true;
	else if (dso->nsinfo) {
		char *new_name;

		new_name = filename_with_chroot(dso->nsinfo->pid,
						dso->long_name);
		if (new_name && filename__read_build_id(new_name, &dso->bid) > 0)
			dso->has_build_id = true;
		free(new_name);
	}
	nsinfo__mountns_exit(&nsc);

	return dso->has_build_id ? 0 : -1;
}

static struct strlist *perf_inject__parse_known_build_ids(
	const char *known_build_ids_string)
{
	struct str_node *pos, *tmp;
	struct strlist *known_build_ids;
	int bid_len;

	known_build_ids = strlist__new(known_build_ids_string, NULL);
	if (known_build_ids == NULL)
		return NULL;
	strlist__for_each_entry_safe(pos, tmp, known_build_ids) {
		const char *build_id, *dso_name;

		build_id = skip_spaces(pos->s);
		dso_name = strchr(build_id, ' ');
		if (dso_name == NULL) {
			strlist__remove(known_build_ids, pos);
			continue;
		}
		bid_len = dso_name - pos->s;
		dso_name = skip_spaces(dso_name);
		if (bid_len % 2 != 0 || bid_len >= SBUILD_ID_SIZE) {
			strlist__remove(known_build_ids, pos);
			continue;
		}
		for (int ix = 0; 2 * ix + 1 < bid_len; ++ix) {
			if (!isxdigit(build_id[2 * ix]) ||
			    !isxdigit(build_id[2 * ix + 1])) {
				strlist__remove(known_build_ids, pos);
				break;
			}
		}
	}
	return known_build_ids;
}

static bool perf_inject__lookup_known_build_id(struct perf_inject *inject,
					       struct dso *dso)
{
	struct str_node *pos;
	int bid_len;

	strlist__for_each_entry(pos, inject->known_build_ids) {
		const char *build_id, *dso_name;

		build_id = skip_spaces(pos->s);
		dso_name = strchr(build_id, ' ');
		bid_len = dso_name - pos->s;
		dso_name = skip_spaces(dso_name);
		if (strcmp(dso->long_name, dso_name))
			continue;
		for (int ix = 0; 2 * ix + 1 < bid_len; ++ix) {
			dso->bid.data[ix] = (hex(build_id[2 * ix]) << 4 |
					     hex(build_id[2 * ix + 1]));
		}
		dso->bid.size = bid_len / 2;
		dso->has_build_id = 1;
		return true;
	}
	return false;
}

static int dso__inject_build_id(struct dso *dso, struct perf_tool *tool,
				struct machine *machine, u8 cpumode, u32 flags)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject,
						  tool);
	int err;

	if (is_anon_memory(dso->long_name) || flags & MAP_HUGETLB)
		return 0;
	if (is_no_dso_memory(dso->long_name))
		return 0;

	if (inject->known_build_ids != NULL &&
	    perf_inject__lookup_known_build_id(inject, dso))
		return 1;

	if (dso__read_build_id(dso) < 0) {
		pr_debug("no build_id found for %s\n", dso->long_name);
		return -1;
	}

	err = perf_event__synthesize_build_id(tool, dso, cpumode,
					      perf_event__repipe, machine);
	if (err) {
		pr_err("Can't synthesize build_id event for %s\n", dso->long_name);
		return -1;
	}

	return 0;
}

int perf_event__inject_buildid(struct perf_tool *tool, union perf_event *event,
			       struct perf_sample *sample,
			       struct evsel *evsel __maybe_unused,
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

	if (thread__find_map(thread, sample->cpumode, sample->ip, &al)) {
		if (!al.map->dso->hit) {
			al.map->dso->hit = 1;
			dso__inject_build_id(al.map->dso, tool, machine,
					     sample->cpumode, al.map->flags);
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
					   struct evsel *evsel __maybe_unused,
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
				     struct evsel *evsel,
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
				   struct evsel *evsel,
				   struct machine *machine)
{
	struct event_entry *ent;
	union perf_event *event_sw;
	struct perf_sample sample_sw;
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	u32 pid = evsel__intval(evsel, sample, "pid");

	list_for_each_entry(ent, &inject->samples, node) {
		if (pid == ent->tid)
			goto found;
	}

	return 0;
found:
	event_sw = &ent->event[0];
	evsel__parse_sample(evsel, event_sw, &sample_sw);

	sample_sw.period = sample->period;
	sample_sw.time	 = sample->time;
	perf_event__synthesize_sample(event_sw, evsel->core.attr.sample_type,
				      evsel->core.attr.read_format, &sample_sw);
	build_id__mark_dso_hit(tool, event_sw, &sample_sw, evsel, machine);
	return perf_event__repipe(tool, event_sw, &sample_sw, machine);
}

static struct guest_vcpu *guest_session__vcpu(struct guest_session *gs, u32 vcpu)
{
	if (realloc_array_as_needed(gs->vcpu, gs->vcpu_cnt, vcpu, NULL))
		return NULL;
	return &gs->vcpu[vcpu];
}

static int guest_session__output_bytes(struct guest_session *gs, void *buf, size_t sz)
{
	ssize_t ret = writen(gs->tmp_fd, buf, sz);

	return ret < 0 ? ret : 0;
}

static int guest_session__repipe(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_sample *sample __maybe_unused,
				 struct machine *machine __maybe_unused)
{
	struct guest_session *gs = container_of(tool, struct guest_session, tool);

	return guest_session__output_bytes(gs, event, event->header.size);
}

static int guest_session__map_tid(struct guest_session *gs, u32 tid, u32 vcpu)
{
	struct guest_tid *guest_tid = zalloc(sizeof(*guest_tid));
	int hash;

	if (!guest_tid)
		return -ENOMEM;

	guest_tid->tid = tid;
	guest_tid->vcpu = vcpu;
	hash = hash_32(guest_tid->tid, PERF_EVLIST__HLIST_BITS);
	hlist_add_head(&guest_tid->node, &gs->tids[hash]);

	return 0;
}

static int host_peek_vm_comms_cb(struct perf_session *session __maybe_unused,
				 union perf_event *event,
				 u64 offset __maybe_unused, void *data)
{
	struct guest_session *gs = data;
	unsigned int vcpu;
	struct guest_vcpu *guest_vcpu;
	int ret;

	if (event->header.type != PERF_RECORD_COMM ||
	    event->comm.pid != gs->machine_pid)
		return 0;

	/*
	 * QEMU option -name debug-threads=on, causes thread names formatted as
	 * below, although it is not an ABI. Also libvirt seems to use this by
	 * default. Here we rely on it to tell us which thread is which VCPU.
	 */
	ret = sscanf(event->comm.comm, "CPU %u/KVM", &vcpu);
	if (ret <= 0)
		return ret;
	pr_debug("Found VCPU: tid %u comm %s vcpu %u\n",
		 event->comm.tid, event->comm.comm, vcpu);
	if (vcpu > INT_MAX) {
		pr_err("Invalid VCPU %u\n", vcpu);
		return -EINVAL;
	}
	guest_vcpu = guest_session__vcpu(gs, vcpu);
	if (!guest_vcpu)
		return -ENOMEM;
	if (guest_vcpu->tid && guest_vcpu->tid != event->comm.tid) {
		pr_err("Fatal error: Two threads found with the same VCPU\n");
		return -EINVAL;
	}
	guest_vcpu->tid = event->comm.tid;

	return guest_session__map_tid(gs, event->comm.tid, vcpu);
}

static int host_peek_vm_comms(struct perf_session *session, struct guest_session *gs)
{
	return perf_session__peek_events(session, session->header.data_offset,
					 session->header.data_size,
					 host_peek_vm_comms_cb, gs);
}

static bool evlist__is_id_used(struct evlist *evlist, u64 id)
{
	return evlist__id2sid(evlist, id);
}

static u64 guest_session__allocate_new_id(struct guest_session *gs, struct evlist *host_evlist)
{
	do {
		gs->highest_id += 1;
	} while (!gs->highest_id || evlist__is_id_used(host_evlist, gs->highest_id));

	return gs->highest_id;
}

static int guest_session__map_id(struct guest_session *gs, u64 id, u64 host_id, u32 vcpu)
{
	struct guest_id *guest_id = zalloc(sizeof(*guest_id));
	int hash;

	if (!guest_id)
		return -ENOMEM;

	guest_id->id = id;
	guest_id->host_id = host_id;
	guest_id->vcpu = vcpu;
	hash = hash_64(guest_id->id, PERF_EVLIST__HLIST_BITS);
	hlist_add_head(&guest_id->node, &gs->heads[hash]);

	return 0;
}

static u64 evlist__find_highest_id(struct evlist *evlist)
{
	struct evsel *evsel;
	u64 highest_id = 1;

	evlist__for_each_entry(evlist, evsel) {
		u32 j;

		for (j = 0; j < evsel->core.ids; j++) {
			u64 id = evsel->core.id[j];

			if (id > highest_id)
				highest_id = id;
		}
	}

	return highest_id;
}

static int guest_session__map_ids(struct guest_session *gs, struct evlist *host_evlist)
{
	struct evlist *evlist = gs->session->evlist;
	struct evsel *evsel;
	int ret;

	evlist__for_each_entry(evlist, evsel) {
		u32 j;

		for (j = 0; j < evsel->core.ids; j++) {
			struct perf_sample_id *sid;
			u64 host_id;
			u64 id;

			id = evsel->core.id[j];
			sid = evlist__id2sid(evlist, id);
			if (!sid || sid->cpu.cpu == -1)
				continue;
			host_id = guest_session__allocate_new_id(gs, host_evlist);
			ret = guest_session__map_id(gs, id, host_id, sid->cpu.cpu);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static struct guest_id *guest_session__lookup_id(struct guest_session *gs, u64 id)
{
	struct hlist_head *head;
	struct guest_id *guest_id;
	int hash;

	hash = hash_64(id, PERF_EVLIST__HLIST_BITS);
	head = &gs->heads[hash];

	hlist_for_each_entry(guest_id, head, node)
		if (guest_id->id == id)
			return guest_id;

	return NULL;
}

static int process_attr(struct perf_tool *tool, union perf_event *event,
			struct perf_sample *sample __maybe_unused,
			struct machine *machine __maybe_unused)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);

	return perf_event__process_attr(tool, event, &inject->session->evlist);
}

static int guest_session__add_attr(struct guest_session *gs, struct evsel *evsel)
{
	struct perf_inject *inject = container_of(gs, struct perf_inject, guest_session);
	struct perf_event_attr attr = evsel->core.attr;
	u64 *id_array;
	u32 *vcpu_array;
	int ret = -ENOMEM;
	u32 i;

	id_array = calloc(evsel->core.ids, sizeof(*id_array));
	if (!id_array)
		return -ENOMEM;

	vcpu_array = calloc(evsel->core.ids, sizeof(*vcpu_array));
	if (!vcpu_array)
		goto out;

	for (i = 0; i < evsel->core.ids; i++) {
		u64 id = evsel->core.id[i];
		struct guest_id *guest_id = guest_session__lookup_id(gs, id);

		if (!guest_id) {
			pr_err("Failed to find guest id %"PRIu64"\n", id);
			ret = -EINVAL;
			goto out;
		}
		id_array[i] = guest_id->host_id;
		vcpu_array[i] = guest_id->vcpu;
	}

	attr.sample_type |= PERF_SAMPLE_IDENTIFIER;
	attr.exclude_host = 1;
	attr.exclude_guest = 0;

	ret = perf_event__synthesize_attr(&inject->tool, &attr, evsel->core.ids,
					  id_array, process_attr);
	if (ret)
		pr_err("Failed to add guest attr.\n");

	for (i = 0; i < evsel->core.ids; i++) {
		struct perf_sample_id *sid;
		u32 vcpu = vcpu_array[i];

		sid = evlist__id2sid(inject->session->evlist, id_array[i]);
		/* Guest event is per-thread from the host point of view */
		sid->cpu.cpu = -1;
		sid->tid = gs->vcpu[vcpu].tid;
		sid->machine_pid = gs->machine_pid;
		sid->vcpu.cpu = vcpu;
	}
out:
	free(vcpu_array);
	free(id_array);
	return ret;
}

static int guest_session__add_attrs(struct guest_session *gs)
{
	struct evlist *evlist = gs->session->evlist;
	struct evsel *evsel;
	int ret;

	evlist__for_each_entry(evlist, evsel) {
		ret = guest_session__add_attr(gs, evsel);
		if (ret)
			return ret;
	}

	return 0;
}

static int synthesize_id_index(struct perf_inject *inject, size_t new_cnt)
{
	struct perf_session *session = inject->session;
	struct evlist *evlist = session->evlist;
	struct machine *machine = &session->machines.host;
	size_t from = evlist->core.nr_entries - new_cnt;

	return __perf_event__synthesize_id_index(&inject->tool, perf_event__repipe,
						 evlist, machine, from);
}

static struct guest_tid *guest_session__lookup_tid(struct guest_session *gs, u32 tid)
{
	struct hlist_head *head;
	struct guest_tid *guest_tid;
	int hash;

	hash = hash_32(tid, PERF_EVLIST__HLIST_BITS);
	head = &gs->tids[hash];

	hlist_for_each_entry(guest_tid, head, node)
		if (guest_tid->tid == tid)
			return guest_tid;

	return NULL;
}

static bool dso__is_in_kernel_space(struct dso *dso)
{
	if (dso__is_vdso(dso))
		return false;

	return dso__is_kcore(dso) ||
	       dso->kernel ||
	       is_kernel_module(dso->long_name, PERF_RECORD_MISC_CPUMODE_UNKNOWN);
}

static u64 evlist__first_id(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.ids)
			return evsel->core.id[0];
	}
	return 0;
}

static int process_build_id(struct perf_tool *tool,
			    union perf_event *event,
			    struct perf_sample *sample __maybe_unused,
			    struct machine *machine __maybe_unused)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);

	return perf_event__process_build_id(inject->session, event);
}

static int synthesize_build_id(struct perf_inject *inject, struct dso *dso, pid_t machine_pid)
{
	struct machine *machine = perf_session__findnew_machine(inject->session, machine_pid);
	u8 cpumode = dso__is_in_kernel_space(dso) ?
			PERF_RECORD_MISC_GUEST_KERNEL :
			PERF_RECORD_MISC_GUEST_USER;

	if (!machine)
		return -ENOMEM;

	dso->hit = 1;

	return perf_event__synthesize_build_id(&inject->tool, dso, cpumode,
					       process_build_id, machine);
}

static int guest_session__add_build_ids(struct guest_session *gs)
{
	struct perf_inject *inject = container_of(gs, struct perf_inject, guest_session);
	struct machine *machine = &gs->session->machines.host;
	struct dso *dso;
	int ret;

	/* Build IDs will be put in the Build ID feature section */
	perf_header__set_feat(&inject->session->header, HEADER_BUILD_ID);

	dsos__for_each_with_build_id(dso, &machine->dsos.head) {
		ret = synthesize_build_id(inject, dso, gs->machine_pid);
		if (ret)
			return ret;
	}

	return 0;
}

static int guest_session__ksymbol_event(struct perf_tool *tool,
					union perf_event *event,
					struct perf_sample *sample __maybe_unused,
					struct machine *machine __maybe_unused)
{
	struct guest_session *gs = container_of(tool, struct guest_session, tool);

	/* Only support out-of-line i.e. no BPF support */
	if (event->ksymbol.ksym_type != PERF_RECORD_KSYMBOL_TYPE_OOL)
		return 0;

	return guest_session__output_bytes(gs, event, event->header.size);
}

static int guest_session__start(struct guest_session *gs, const char *name, bool force)
{
	char tmp_file_name[] = "/tmp/perf-inject-guest_session-XXXXXX";
	struct perf_session *session;
	int ret;

	/* Only these events will be injected */
	gs->tool.mmap		= guest_session__repipe;
	gs->tool.mmap2		= guest_session__repipe;
	gs->tool.comm		= guest_session__repipe;
	gs->tool.fork		= guest_session__repipe;
	gs->tool.exit		= guest_session__repipe;
	gs->tool.lost		= guest_session__repipe;
	gs->tool.context_switch	= guest_session__repipe;
	gs->tool.ksymbol	= guest_session__ksymbol_event;
	gs->tool.text_poke	= guest_session__repipe;
	/*
	 * Processing a build ID creates a struct dso with that build ID. Later,
	 * all guest dsos are iterated and the build IDs processed into the host
	 * session where they will be output to the Build ID feature section
	 * when the perf.data file header is written.
	 */
	gs->tool.build_id	= perf_event__process_build_id;
	/* Process the id index to know what VCPU an ID belongs to */
	gs->tool.id_index	= perf_event__process_id_index;

	gs->tool.ordered_events	= true;
	gs->tool.ordering_requires_timestamps = true;

	gs->data.path	= name;
	gs->data.force	= force;
	gs->data.mode	= PERF_DATA_MODE_READ;

	session = perf_session__new(&gs->data, &gs->tool);
	if (IS_ERR(session))
		return PTR_ERR(session);
	gs->session = session;

	/*
	 * Initial events have zero'd ID samples. Get default ID sample size
	 * used for removing them.
	 */
	gs->dflt_id_hdr_size = session->machines.host.id_hdr_size;
	/* And default ID for adding back a host-compatible ID sample */
	gs->dflt_id = evlist__first_id(session->evlist);
	if (!gs->dflt_id) {
		pr_err("Guest data has no sample IDs");
		return -EINVAL;
	}

	/* Temporary file for guest events */
	gs->tmp_file_name = strdup(tmp_file_name);
	if (!gs->tmp_file_name)
		return -ENOMEM;
	gs->tmp_fd = mkstemp(gs->tmp_file_name);
	if (gs->tmp_fd < 0)
		return -errno;

	if (zstd_init(&gs->session->zstd_data, 0) < 0)
		pr_warning("Guest session decompression initialization failed.\n");

	/*
	 * perf does not support processing 2 sessions simultaneously, so output
	 * guest events to a temporary file.
	 */
	ret = perf_session__process_events(gs->session);
	if (ret)
		return ret;

	if (lseek(gs->tmp_fd, 0, SEEK_SET))
		return -errno;

	return 0;
}

/* Free hlist nodes assuming hlist_node is the first member of hlist entries */
static void free_hlist(struct hlist_head *heads, size_t hlist_sz)
{
	struct hlist_node *pos, *n;
	size_t i;

	for (i = 0; i < hlist_sz; ++i) {
		hlist_for_each_safe(pos, n, &heads[i]) {
			hlist_del(pos);
			free(pos);
		}
	}
}

static void guest_session__exit(struct guest_session *gs)
{
	if (gs->session) {
		perf_session__delete(gs->session);
		free_hlist(gs->heads, PERF_EVLIST__HLIST_SIZE);
		free_hlist(gs->tids, PERF_EVLIST__HLIST_SIZE);
	}
	if (gs->tmp_file_name) {
		if (gs->tmp_fd >= 0)
			close(gs->tmp_fd);
		unlink(gs->tmp_file_name);
		free(gs->tmp_file_name);
	}
	free(gs->vcpu);
	free(gs->perf_data_file);
}

static void get_tsc_conv(struct perf_tsc_conversion *tc, struct perf_record_time_conv *time_conv)
{
	tc->time_shift		= time_conv->time_shift;
	tc->time_mult		= time_conv->time_mult;
	tc->time_zero		= time_conv->time_zero;
	tc->time_cycles		= time_conv->time_cycles;
	tc->time_mask		= time_conv->time_mask;
	tc->cap_user_time_zero	= time_conv->cap_user_time_zero;
	tc->cap_user_time_short	= time_conv->cap_user_time_short;
}

static void guest_session__get_tc(struct guest_session *gs)
{
	struct perf_inject *inject = container_of(gs, struct perf_inject, guest_session);

	get_tsc_conv(&gs->host_tc, &inject->session->time_conv);
	get_tsc_conv(&gs->guest_tc, &gs->session->time_conv);
}

static void guest_session__convert_time(struct guest_session *gs, u64 guest_time, u64 *host_time)
{
	u64 tsc;

	if (!guest_time) {
		*host_time = 0;
		return;
	}

	if (gs->guest_tc.cap_user_time_zero)
		tsc = perf_time_to_tsc(guest_time, &gs->guest_tc);
	else
		tsc = guest_time;

	/*
	 * This is the correct order of operations for x86 if the TSC Offset and
	 * Multiplier values are used.
	 */
	tsc -= gs->time_offset;
	tsc /= gs->time_scale;

	if (gs->host_tc.cap_user_time_zero)
		*host_time = tsc_to_perf_time(tsc, &gs->host_tc);
	else
		*host_time = tsc;
}

static int guest_session__fetch(struct guest_session *gs)
{
	void *buf = gs->ev.event_buf;
	struct perf_event_header *hdr = buf;
	size_t hdr_sz = sizeof(*hdr);
	ssize_t ret;

	ret = readn(gs->tmp_fd, buf, hdr_sz);
	if (ret < 0)
		return ret;

	if (!ret) {
		/* Zero size means EOF */
		hdr->size = 0;
		return 0;
	}

	buf += hdr_sz;

	ret = readn(gs->tmp_fd, buf, hdr->size - hdr_sz);
	if (ret < 0)
		return ret;

	gs->ev.event = (union perf_event *)gs->ev.event_buf;
	gs->ev.sample.time = 0;

	if (hdr->type >= PERF_RECORD_USER_TYPE_START) {
		pr_err("Unexpected type fetching guest event");
		return 0;
	}

	ret = evlist__parse_sample(gs->session->evlist, gs->ev.event, &gs->ev.sample);
	if (ret) {
		pr_err("Parse failed fetching guest event");
		return ret;
	}

	if (!gs->have_tc) {
		guest_session__get_tc(gs);
		gs->have_tc = true;
	}

	guest_session__convert_time(gs, gs->ev.sample.time, &gs->ev.sample.time);

	return 0;
}

static int evlist__append_id_sample(struct evlist *evlist, union perf_event *ev,
				    const struct perf_sample *sample)
{
	struct evsel *evsel;
	void *array;
	int ret;

	evsel = evlist__id2evsel(evlist, sample->id);
	array = ev;

	if (!evsel) {
		pr_err("No evsel for id %"PRIu64"\n", sample->id);
		return -EINVAL;
	}

	array += ev->header.size;
	ret = perf_event__synthesize_id_sample(array, evsel->core.attr.sample_type, sample);
	if (ret < 0)
		return ret;

	if (ret & 7) {
		pr_err("Bad id sample size %d\n", ret);
		return -EINVAL;
	}

	ev->header.size += ret;

	return 0;
}

static int guest_session__inject_events(struct guest_session *gs, u64 timestamp)
{
	struct perf_inject *inject = container_of(gs, struct perf_inject, guest_session);
	int ret;

	if (!gs->ready)
		return 0;

	while (1) {
		struct perf_sample *sample;
		struct guest_id *guest_id;
		union perf_event *ev;
		u16 id_hdr_size;
		u8 cpumode;
		u64 id;

		if (!gs->fetched) {
			ret = guest_session__fetch(gs);
			if (ret)
				return ret;
			gs->fetched = true;
		}

		ev = gs->ev.event;
		sample = &gs->ev.sample;

		if (!ev->header.size)
			return 0; /* EOF */

		if (sample->time > timestamp)
			return 0;

		/* Change cpumode to guest */
		cpumode = ev->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
		if (cpumode & PERF_RECORD_MISC_USER)
			cpumode = PERF_RECORD_MISC_GUEST_USER;
		else
			cpumode = PERF_RECORD_MISC_GUEST_KERNEL;
		ev->header.misc &= ~PERF_RECORD_MISC_CPUMODE_MASK;
		ev->header.misc |= cpumode;

		id = sample->id;
		if (!id) {
			id = gs->dflt_id;
			id_hdr_size = gs->dflt_id_hdr_size;
		} else {
			struct evsel *evsel = evlist__id2evsel(gs->session->evlist, id);

			id_hdr_size = evsel__id_hdr_size(evsel);
		}

		if (id_hdr_size & 7) {
			pr_err("Bad id_hdr_size %u\n", id_hdr_size);
			return -EINVAL;
		}

		if (ev->header.size & 7) {
			pr_err("Bad event size %u\n", ev->header.size);
			return -EINVAL;
		}

		/* Remove guest id sample */
		ev->header.size -= id_hdr_size;

		if (ev->header.size & 7) {
			pr_err("Bad raw event size %u\n", ev->header.size);
			return -EINVAL;
		}

		guest_id = guest_session__lookup_id(gs, id);
		if (!guest_id) {
			pr_err("Guest event with unknown id %llu\n",
			       (unsigned long long)id);
			return -EINVAL;
		}

		/* Change to host ID to avoid conflicting ID values */
		sample->id = guest_id->host_id;
		sample->stream_id = guest_id->host_id;

		if (sample->cpu != (u32)-1) {
			if (sample->cpu >= gs->vcpu_cnt) {
				pr_err("Guest event with unknown VCPU %u\n",
				       sample->cpu);
				return -EINVAL;
			}
			/* Change to host CPU instead of guest VCPU */
			sample->cpu = gs->vcpu[sample->cpu].cpu;
		}

		/* New id sample with new ID and CPU */
		ret = evlist__append_id_sample(inject->session->evlist, ev, sample);
		if (ret)
			return ret;

		if (ev->header.size & 7) {
			pr_err("Bad new event size %u\n", ev->header.size);
			return -EINVAL;
		}

		gs->fetched = false;

		ret = output_bytes(inject, ev, ev->header.size);
		if (ret)
			return ret;
	}
}

static int guest_session__flush_events(struct guest_session *gs)
{
	return guest_session__inject_events(gs, -1);
}

static int host__repipe(struct perf_tool *tool,
			union perf_event *event,
			struct perf_sample *sample,
			struct machine *machine)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	int ret;

	ret = guest_session__inject_events(&inject->guest_session, sample->time);
	if (ret)
		return ret;

	return perf_event__repipe(tool, event, sample, machine);
}

static int host__finished_init(struct perf_session *session, union perf_event *event)
{
	struct perf_inject *inject = container_of(session->tool, struct perf_inject, tool);
	struct guest_session *gs = &inject->guest_session;
	int ret;

	/*
	 * Peek through host COMM events to find QEMU threads and the VCPU they
	 * are running.
	 */
	ret = host_peek_vm_comms(session, gs);
	if (ret)
		return ret;

	if (!gs->vcpu_cnt) {
		pr_err("No VCPU threads found for pid %u\n", gs->machine_pid);
		return -EINVAL;
	}

	/*
	 * Allocate new (unused) host sample IDs and map them to the guest IDs.
	 */
	gs->highest_id = evlist__find_highest_id(session->evlist);
	ret = guest_session__map_ids(gs, session->evlist);
	if (ret)
		return ret;

	ret = guest_session__add_attrs(gs);
	if (ret)
		return ret;

	ret = synthesize_id_index(inject, gs->session->evlist->core.nr_entries);
	if (ret) {
		pr_err("Failed to synthesize id_index\n");
		return ret;
	}

	ret = guest_session__add_build_ids(gs);
	if (ret) {
		pr_err("Failed to add guest build IDs\n");
		return ret;
	}

	gs->ready = true;

	ret = guest_session__inject_events(gs, 0);
	if (ret)
		return ret;

	return perf_event__repipe_op2_synth(session, event);
}

/*
 * Obey finished-round ordering. The FINISHED_ROUND event is first processed
 * which flushes host events to file up until the last flush time. Then inject
 * guest events up to the same time. Finally write out the FINISHED_ROUND event
 * itself.
 */
static int host__finished_round(struct perf_tool *tool,
				union perf_event *event,
				struct ordered_events *oe)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	int ret = perf_event__process_finished_round(tool, event, oe);
	u64 timestamp = ordered_events__last_flush_time(oe);

	if (ret)
		return ret;

	ret = guest_session__inject_events(&inject->guest_session, timestamp);
	if (ret)
		return ret;

	return perf_event__repipe_oe_synth(tool, event, oe);
}

static int host__context_switch(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct machine *machine)
{
	struct perf_inject *inject = container_of(tool, struct perf_inject, tool);
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;
	struct guest_session *gs = &inject->guest_session;
	u32 pid = event->context_switch.next_prev_pid;
	u32 tid = event->context_switch.next_prev_tid;
	struct guest_tid *guest_tid;
	u32 vcpu;

	if (out || pid != gs->machine_pid)
		goto out;

	guest_tid = guest_session__lookup_tid(gs, tid);
	if (!guest_tid)
		goto out;

	if (sample->cpu == (u32)-1) {
		pr_err("Switch event does not have CPU\n");
		return -EINVAL;
	}

	vcpu = guest_tid->vcpu;
	if (vcpu >= gs->vcpu_cnt)
		return -EINVAL;

	/* Guest is switching in, record which CPU the VCPU is now running on */
	gs->vcpu[vcpu].cpu = sample->cpu;
out:
	return host__repipe(tool, event, sample, machine);
}

static void sig_handler(int sig __maybe_unused)
{
	session_done = 1;
}

static int evsel__check_stype(struct evsel *evsel, u64 sample_type, const char *sample_msg)
{
	struct perf_event_attr *attr = &evsel->core.attr;
	const char *name = evsel__name(evsel);

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
		       struct evsel *evsel __maybe_unused,
		       struct machine *machine __maybe_unused)
{
	return 0;
}

static void strip_init(struct perf_inject *inject)
{
	struct evlist *evlist = inject->session->evlist;
	struct evsel *evsel;

	inject->tool.context_switch = perf_event__drop;

	evlist__for_each_entry(evlist, evsel)
		evsel->handler = drop_sample;
}

static int parse_vm_time_correlation(const struct option *opt, const char *str, int unset)
{
	struct perf_inject *inject = opt->value;
	const char *args;
	char *dry_run;

	if (unset)
		return 0;

	inject->itrace_synth_opts.set = true;
	inject->itrace_synth_opts.vm_time_correlation = true;
	inject->in_place_update = true;

	if (!str)
		return 0;

	dry_run = skip_spaces(str);
	if (!strncmp(dry_run, "dry-run", strlen("dry-run"))) {
		inject->itrace_synth_opts.vm_tm_corr_dry_run = true;
		inject->in_place_update_dry_run = true;
		args = dry_run + strlen("dry-run");
	} else {
		args = str;
	}

	inject->itrace_synth_opts.vm_tm_corr_args = strdup(args);

	return inject->itrace_synth_opts.vm_tm_corr_args ? 0 : -ENOMEM;
}

static int parse_guest_data(const struct option *opt, const char *str, int unset)
{
	struct perf_inject *inject = opt->value;
	struct guest_session *gs = &inject->guest_session;
	char *tok;
	char *s;

	if (unset)
		return 0;

	if (!str)
		goto bad_args;

	s = strdup(str);
	if (!s)
		return -ENOMEM;

	gs->perf_data_file = strsep(&s, ",");
	if (!gs->perf_data_file)
		goto bad_args;

	gs->copy_kcore_dir = has_kcore_dir(gs->perf_data_file);
	if (gs->copy_kcore_dir)
		inject->output.is_dir = true;

	tok = strsep(&s, ",");
	if (!tok)
		goto bad_args;
	gs->machine_pid = strtoul(tok, NULL, 0);
	if (!inject->guest_session.machine_pid)
		goto bad_args;

	gs->time_scale = 1;

	tok = strsep(&s, ",");
	if (!tok)
		goto out;
	gs->time_offset = strtoull(tok, NULL, 0);

	tok = strsep(&s, ",");
	if (!tok)
		goto out;
	gs->time_scale = strtod(tok, NULL);
	if (!gs->time_scale)
		goto bad_args;
out:
	return 0;

bad_args:
	pr_err("--guest-data option requires guest perf.data file name, "
	       "guest machine PID, and optionally guest timestamp offset, "
	       "and guest timestamp scale factor, separated by commas.\n");
	return -1;
}

static int save_section_info_cb(struct perf_file_section *section,
				struct perf_header *ph __maybe_unused,
				int feat, int fd __maybe_unused, void *data)
{
	struct perf_inject *inject = data;

	inject->secs[feat] = *section;
	return 0;
}

static int save_section_info(struct perf_inject *inject)
{
	struct perf_header *header = &inject->session->header;
	int fd = perf_data__fd(inject->session->data);

	return perf_header__process_sections(header, fd, inject, save_section_info_cb);
}

static bool keep_feat(int feat)
{
	switch (feat) {
	/* Keep original information that describes the machine or software */
	case HEADER_TRACING_DATA:
	case HEADER_HOSTNAME:
	case HEADER_OSRELEASE:
	case HEADER_VERSION:
	case HEADER_ARCH:
	case HEADER_NRCPUS:
	case HEADER_CPUDESC:
	case HEADER_CPUID:
	case HEADER_TOTAL_MEM:
	case HEADER_CPU_TOPOLOGY:
	case HEADER_NUMA_TOPOLOGY:
	case HEADER_PMU_MAPPINGS:
	case HEADER_CACHE:
	case HEADER_MEM_TOPOLOGY:
	case HEADER_CLOCKID:
	case HEADER_BPF_PROG_INFO:
	case HEADER_BPF_BTF:
	case HEADER_CPU_PMU_CAPS:
	case HEADER_CLOCK_DATA:
	case HEADER_HYBRID_TOPOLOGY:
	case HEADER_PMU_CAPS:
		return true;
	/* Information that can be updated */
	case HEADER_BUILD_ID:
	case HEADER_CMDLINE:
	case HEADER_EVENT_DESC:
	case HEADER_BRANCH_STACK:
	case HEADER_GROUP_DESC:
	case HEADER_AUXTRACE:
	case HEADER_STAT:
	case HEADER_SAMPLE_TIME:
	case HEADER_DIR_FORMAT:
	case HEADER_COMPRESSED:
	default:
		return false;
	};
}

static int read_file(int fd, u64 offs, void *buf, size_t sz)
{
	ssize_t ret = preadn(fd, buf, sz, offs);

	if (ret < 0)
		return -errno;
	if ((size_t)ret != sz)
		return -EINVAL;
	return 0;
}

static int feat_copy(struct perf_inject *inject, int feat, struct feat_writer *fw)
{
	int fd = perf_data__fd(inject->session->data);
	u64 offs = inject->secs[feat].offset;
	size_t sz = inject->secs[feat].size;
	void *buf = malloc(sz);
	int ret;

	if (!buf)
		return -ENOMEM;

	ret = read_file(fd, offs, buf, sz);
	if (ret)
		goto out_free;

	ret = fw->write(fw, buf, sz);
out_free:
	free(buf);
	return ret;
}

struct inject_fc {
	struct feat_copier fc;
	struct perf_inject *inject;
};

static int feat_copy_cb(struct feat_copier *fc, int feat, struct feat_writer *fw)
{
	struct inject_fc *inj_fc = container_of(fc, struct inject_fc, fc);
	struct perf_inject *inject = inj_fc->inject;
	int ret;

	if (!inject->secs[feat].offset ||
	    !keep_feat(feat))
		return 0;

	ret = feat_copy(inject, feat, fw);
	if (ret < 0)
		return ret;

	return 1; /* Feature section copied */
}

static int copy_kcore_dir(struct perf_inject *inject)
{
	char *cmd;
	int ret;

	ret = asprintf(&cmd, "cp -r -n %s/kcore_dir* %s >/dev/null 2>&1",
		       inject->input_name, inject->output.path);
	if (ret < 0)
		return ret;
	pr_debug("%s\n", cmd);
	ret = system(cmd);
	free(cmd);
	return ret;
}

static int guest_session__copy_kcore_dir(struct guest_session *gs)
{
	struct perf_inject *inject = container_of(gs, struct perf_inject, guest_session);
	char *cmd;
	int ret;

	ret = asprintf(&cmd, "cp -r -n %s/kcore_dir %s/kcore_dir__%u >/dev/null 2>&1",
		       gs->perf_data_file, inject->output.path, gs->machine_pid);
	if (ret < 0)
		return ret;
	pr_debug("%s\n", cmd);
	ret = system(cmd);
	free(cmd);
	return ret;
}

static int output_fd(struct perf_inject *inject)
{
	return inject->in_place_update ? -1 : perf_data__fd(&inject->output);
}

static int __cmd_inject(struct perf_inject *inject)
{
	int ret = -EINVAL;
	struct guest_session *gs = &inject->guest_session;
	struct perf_session *session = inject->session;
	int fd = output_fd(inject);
	u64 output_data_offset;

	signal(SIGINT, sig_handler);

	if (inject->build_ids || inject->sched_stat ||
	    inject->itrace_synth_opts.set || inject->build_id_all) {
		inject->tool.mmap	  = perf_event__repipe_mmap;
		inject->tool.mmap2	  = perf_event__repipe_mmap2;
		inject->tool.fork	  = perf_event__repipe_fork;
		inject->tool.tracing_data = perf_event__repipe_tracing_data;
	}

	output_data_offset = perf_session__data_offset(session->evlist);

	if (inject->build_id_all) {
		inject->tool.mmap	  = perf_event__repipe_buildid_mmap;
		inject->tool.mmap2	  = perf_event__repipe_buildid_mmap2;
	} else if (inject->build_ids) {
		inject->tool.sample = perf_event__inject_buildid;
	} else if (inject->sched_stat) {
		struct evsel *evsel;

		evlist__for_each_entry(session->evlist, evsel) {
			const char *name = evsel__name(evsel);

			if (!strcmp(name, "sched:sched_switch")) {
				if (evsel__check_stype(evsel, PERF_SAMPLE_TID, "TID"))
					return -EINVAL;

				evsel->handler = perf_inject__sched_switch;
			} else if (!strcmp(name, "sched:sched_process_exit"))
				evsel->handler = perf_inject__sched_process_exit;
			else if (!strncmp(name, "sched:sched_stat_", 17))
				evsel->handler = perf_inject__sched_stat;
		}
	} else if (inject->itrace_synth_opts.vm_time_correlation) {
		session->itrace_synth_opts = &inject->itrace_synth_opts;
		memset(&inject->tool, 0, sizeof(inject->tool));
		inject->tool.id_index	    = perf_event__process_id_index;
		inject->tool.auxtrace_info  = perf_event__process_auxtrace_info;
		inject->tool.auxtrace	    = perf_event__process_auxtrace;
		inject->tool.auxtrace_error = perf_event__process_auxtrace_error;
		inject->tool.ordered_events = true;
		inject->tool.ordering_requires_timestamps = true;
	} else if (inject->itrace_synth_opts.set) {
		session->itrace_synth_opts = &inject->itrace_synth_opts;
		inject->itrace_synth_opts.inject = true;
		inject->tool.comm	    = perf_event__repipe_comm;
		inject->tool.namespaces	    = perf_event__repipe_namespaces;
		inject->tool.exit	    = perf_event__repipe_exit;
		inject->tool.id_index	    = perf_event__process_id_index;
		inject->tool.auxtrace_info  = perf_event__process_auxtrace_info;
		inject->tool.auxtrace	    = perf_event__process_auxtrace;
		inject->tool.aux	    = perf_event__drop_aux;
		inject->tool.itrace_start   = perf_event__drop_aux;
		inject->tool.aux_output_hw_id = perf_event__drop_aux;
		inject->tool.ordered_events = true;
		inject->tool.ordering_requires_timestamps = true;
		/* Allow space in the header for new attributes */
		output_data_offset = roundup(8192 + session->header.data_offset, 4096);
		if (inject->strip)
			strip_init(inject);
	} else if (gs->perf_data_file) {
		char *name = gs->perf_data_file;

		/*
		 * Not strictly necessary, but keep these events in order wrt
		 * guest events.
		 */
		inject->tool.mmap		= host__repipe;
		inject->tool.mmap2		= host__repipe;
		inject->tool.comm		= host__repipe;
		inject->tool.fork		= host__repipe;
		inject->tool.exit		= host__repipe;
		inject->tool.lost		= host__repipe;
		inject->tool.context_switch	= host__repipe;
		inject->tool.ksymbol		= host__repipe;
		inject->tool.text_poke		= host__repipe;
		/*
		 * Once the host session has initialized, set up sample ID
		 * mapping and feed in guest attrs, build IDs and initial
		 * events.
		 */
		inject->tool.finished_init	= host__finished_init;
		/* Obey finished round ordering */
		inject->tool.finished_round	= host__finished_round,
		/* Keep track of which CPU a VCPU is runnng on */
		inject->tool.context_switch	= host__context_switch;
		/*
		 * Must order events to be able to obey finished round
		 * ordering.
		 */
		inject->tool.ordered_events	= true;
		inject->tool.ordering_requires_timestamps = true;
		/* Set up a separate session to process guest perf.data file */
		ret = guest_session__start(gs, name, session->data->force);
		if (ret) {
			pr_err("Failed to process %s, error %d\n", name, ret);
			return ret;
		}
		/* Allow space in the header for guest attributes */
		output_data_offset += gs->session->header.data_offset;
		output_data_offset = roundup(output_data_offset, 4096);
	}

	if (!inject->itrace_synth_opts.set)
		auxtrace_index__free(&session->auxtrace_index);

	if (!inject->is_pipe && !inject->in_place_update)
		lseek(fd, output_data_offset, SEEK_SET);

	ret = perf_session__process_events(session);
	if (ret)
		return ret;

	if (gs->session) {
		/*
		 * Remaining guest events have later timestamps. Flush them
		 * out to file.
		 */
		ret = guest_session__flush_events(gs);
		if (ret) {
			pr_err("Failed to flush guest events\n");
			return ret;
		}
	}

	if (!inject->is_pipe && !inject->in_place_update) {
		struct inject_fc inj_fc = {
			.fc.copy = feat_copy_cb,
			.inject = inject,
		};

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
		 * synthesized hardware events, so clear the feature flag.
		 */
		if (inject->itrace_synth_opts.set) {
			perf_header__clear_feat(&session->header,
						HEADER_AUXTRACE);
			if (inject->itrace_synth_opts.last_branch ||
			    inject->itrace_synth_opts.add_last_branch)
				perf_header__set_feat(&session->header,
						      HEADER_BRANCH_STACK);
		}
		session->header.data_offset = output_data_offset;
		session->header.data_size = inject->bytes_written;
		perf_session__inject_header(session, session->evlist, fd, &inj_fc.fc);

		if (inject->copy_kcore_dir) {
			ret = copy_kcore_dir(inject);
			if (ret) {
				pr_err("Failed to copy kcore\n");
				return ret;
			}
		}
		if (gs->copy_kcore_dir) {
			ret = guest_session__copy_kcore_dir(gs);
			if (ret) {
				pr_err("Failed to copy guest kcore\n");
				return ret;
			}
		}
	}

	return ret;
}

int cmd_inject(int argc, const char **argv)
{
	struct perf_inject inject = {
		.tool = {
			.sample		= perf_event__repipe_sample,
			.read		= perf_event__repipe_sample,
			.mmap		= perf_event__repipe,
			.mmap2		= perf_event__repipe,
			.comm		= perf_event__repipe,
			.namespaces	= perf_event__repipe,
			.cgroup		= perf_event__repipe,
			.fork		= perf_event__repipe,
			.exit		= perf_event__repipe,
			.lost		= perf_event__repipe,
			.lost_samples	= perf_event__repipe,
			.aux		= perf_event__repipe,
			.itrace_start	= perf_event__repipe,
			.aux_output_hw_id = perf_event__repipe,
			.context_switch	= perf_event__repipe,
			.throttle	= perf_event__repipe,
			.unthrottle	= perf_event__repipe,
			.ksymbol	= perf_event__repipe,
			.bpf		= perf_event__repipe,
			.text_poke	= perf_event__repipe,
			.attr		= perf_event__repipe_attr,
			.event_update	= perf_event__repipe_event_update,
			.tracing_data	= perf_event__repipe_op2_synth,
			.finished_round	= perf_event__repipe_oe_synth,
			.build_id	= perf_event__repipe_op2_synth,
			.id_index	= perf_event__repipe_op2_synth,
			.auxtrace_info	= perf_event__repipe_op2_synth,
			.auxtrace_error	= perf_event__repipe_op2_synth,
			.time_conv	= perf_event__repipe_op2_synth,
			.thread_map	= perf_event__repipe_op2_synth,
			.cpu_map	= perf_event__repipe_op2_synth,
			.stat_config	= perf_event__repipe_op2_synth,
			.stat		= perf_event__repipe_op2_synth,
			.stat_round	= perf_event__repipe_op2_synth,
			.feature	= perf_event__repipe_op2_synth,
			.finished_init	= perf_event__repipe_op2_synth,
			.compressed	= perf_event__repipe_op4_synth,
			.auxtrace	= perf_event__repipe_auxtrace,
		},
		.input_name  = "-",
		.samples = LIST_HEAD_INIT(inject.samples),
		.output = {
			.path = "-",
			.mode = PERF_DATA_MODE_WRITE,
			.use_stdio = true,
		},
	};
	struct perf_data data = {
		.mode = PERF_DATA_MODE_READ,
		.use_stdio = true,
	};
	int ret;
	bool repipe = true;
	const char *known_build_ids = NULL;

	struct option options[] = {
		OPT_BOOLEAN('b', "build-ids", &inject.build_ids,
			    "Inject build-ids into the output stream"),
		OPT_BOOLEAN(0, "buildid-all", &inject.build_id_all,
			    "Inject build-ids of all DSOs into the output stream"),
		OPT_STRING(0, "known-build-ids", &known_build_ids,
			   "buildid path [,buildid path...]",
			   "build-ids to use for given paths"),
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
		OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
			   "file", "vmlinux pathname"),
		OPT_BOOLEAN(0, "ignore-vmlinux", &symbol_conf.ignore_vmlinux,
			    "don't load vmlinux even if found"),
		OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name, "file",
			   "kallsyms pathname"),
		OPT_BOOLEAN('f', "force", &data.force, "don't complain, do it"),
		OPT_CALLBACK_OPTARG(0, "itrace", &inject.itrace_synth_opts,
				    NULL, "opts", "Instruction Tracing options\n"
				    ITRACE_HELP,
				    itrace_parse_synth_opts),
		OPT_BOOLEAN(0, "strip", &inject.strip,
			    "strip non-synthesized events (use with --itrace)"),
		OPT_CALLBACK_OPTARG(0, "vm-time-correlation", &inject, NULL, "opts",
				    "correlate time between VM guests and the host",
				    parse_vm_time_correlation),
		OPT_CALLBACK_OPTARG(0, "guest-data", &inject, NULL, "opts",
				    "inject events from a guest perf.data file",
				    parse_guest_data),
		OPT_STRING(0, "guestmount", &symbol_conf.guestmount, "directory",
			   "guest mount directory under which every guest os"
			   " instance has a subdir"),
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

	if (symbol__validate_sym_arguments())
		return -1;

	if (inject.in_place_update) {
		if (!strcmp(inject.input_name, "-")) {
			pr_err("Input file name required for in-place updating\n");
			return -1;
		}
		if (strcmp(inject.output.path, "-")) {
			pr_err("Output file name must not be specified for in-place updating\n");
			return -1;
		}
		if (!data.force && !inject.in_place_update_dry_run) {
			pr_err("The input file would be updated in place, "
				"the --force option is required.\n");
			return -1;
		}
		if (!inject.in_place_update_dry_run)
			data.in_place_update = true;
	} else {
		if (strcmp(inject.output.path, "-") && !inject.strip &&
		    has_kcore_dir(inject.input_name)) {
			inject.output.is_dir = true;
			inject.copy_kcore_dir = true;
		}
		if (perf_data__open(&inject.output)) {
			perror("failed to create output file");
			return -1;
		}
	}

	data.path = inject.input_name;
	if (!strcmp(inject.input_name, "-") || inject.output.is_pipe) {
		inject.is_pipe = true;
		/*
		 * Do not repipe header when input is a regular file
		 * since either it can rewrite the header at the end
		 * or write a new pipe header.
		 */
		if (strcmp(inject.input_name, "-"))
			repipe = false;
	}

	inject.session = __perf_session__new(&data, repipe,
					     output_fd(&inject),
					     &inject.tool);
	if (IS_ERR(inject.session)) {
		ret = PTR_ERR(inject.session);
		goto out_close_output;
	}

	if (zstd_init(&(inject.session->zstd_data), 0) < 0)
		pr_warning("Decompression initialization failed.\n");

	/* Save original section info before feature bits change */
	ret = save_section_info(&inject);
	if (ret)
		goto out_delete;

	if (!data.is_pipe && inject.output.is_pipe) {
		ret = perf_header__write_pipe(perf_data__fd(&inject.output));
		if (ret < 0) {
			pr_err("Couldn't write a new pipe header.\n");
			goto out_delete;
		}

		ret = perf_event__synthesize_for_pipe(&inject.tool,
						      inject.session,
						      &inject.output,
						      perf_event__repipe);
		if (ret < 0)
			goto out_delete;
	}

	if (inject.build_ids && !inject.build_id_all) {
		/*
		 * to make sure the mmap records are ordered correctly
		 * and so that the correct especially due to jitted code
		 * mmaps. We cannot generate the buildid hit list and
		 * inject the jit mmaps at the same time for now.
		 */
		inject.tool.ordered_events = true;
		inject.tool.ordering_requires_timestamps = true;
		if (known_build_ids != NULL) {
			inject.known_build_ids =
				perf_inject__parse_known_build_ids(known_build_ids);

			if (inject.known_build_ids == NULL) {
				pr_err("Couldn't parse known build ids.\n");
				goto out_delete;
			}
		}
	}

	if (inject.sched_stat) {
		inject.tool.ordered_events = true;
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

	guest_session__exit(&inject.guest_session);

out_delete:
	strlist__delete(inject.known_build_ids);
	zstd_fini(&(inject.session->zstd_data));
	perf_session__delete(inject.session);
out_close_output:
	if (!inject.in_place_update)
		perf_data__close(&inject.output);
	free(inject.itrace_synth_opts.vm_tm_corr_args);
	return ret;
}
