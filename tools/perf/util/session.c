// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <signal.h>
#include <inttypes.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>

#include <byteswap.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <perf/cpumap.h>

#include "map_symbol.h"
#include "branch.h"
#include "debug.h"
#include "env.h"
#include "evlist.h"
#include "evsel.h"
#include "memswap.h"
#include "map.h"
#include "symbol.h"
#include "session.h"
#include "tool.h"
#include "perf_regs.h"
#include "asm/bug.h"
#include "auxtrace.h"
#include "thread.h"
#include "thread-stack.h"
#include "sample-raw.h"
#include "stat.h"
#include "tsc.h"
#include "ui/progress.h"
#include "util.h"
#include "arch/common.h"
#include "units.h"
#include <internal/lib.h>

#ifdef HAVE_ZSTD_SUPPORT
static int perf_session__process_compressed_event(struct perf_session *session,
						  union perf_event *event, u64 file_offset,
						  const char *file_path)
{
	void *src;
	size_t decomp_size, src_size;
	u64 decomp_last_rem = 0;
	size_t mmap_len, decomp_len = session->header.env.comp_mmap_len;
	struct decomp *decomp, *decomp_last = session->active_decomp->decomp_last;

	if (decomp_last) {
		decomp_last_rem = decomp_last->size - decomp_last->head;
		decomp_len += decomp_last_rem;
	}

	mmap_len = sizeof(struct decomp) + decomp_len;
	decomp = mmap(NULL, mmap_len, PROT_READ|PROT_WRITE,
		      MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	if (decomp == MAP_FAILED) {
		pr_err("Couldn't allocate memory for decompression\n");
		return -1;
	}

	decomp->file_pos = file_offset;
	decomp->file_path = file_path;
	decomp->mmap_len = mmap_len;
	decomp->head = 0;

	if (decomp_last_rem) {
		memcpy(decomp->data, &(decomp_last->data[decomp_last->head]), decomp_last_rem);
		decomp->size = decomp_last_rem;
	}

	src = (void *)event + sizeof(struct perf_record_compressed);
	src_size = event->pack.header.size - sizeof(struct perf_record_compressed);

	decomp_size = zstd_decompress_stream(session->active_decomp->zstd_decomp, src, src_size,
				&(decomp->data[decomp_last_rem]), decomp_len - decomp_last_rem);
	if (!decomp_size) {
		munmap(decomp, mmap_len);
		pr_err("Couldn't decompress data\n");
		return -1;
	}

	decomp->size += decomp_size;

	if (session->active_decomp->decomp == NULL)
		session->active_decomp->decomp = decomp;
	else
		session->active_decomp->decomp_last->next = decomp;

	session->active_decomp->decomp_last = decomp;

	pr_debug("decomp (B): %zd to %zd\n", src_size, decomp_size);

	return 0;
}
#else /* !HAVE_ZSTD_SUPPORT */
#define perf_session__process_compressed_event perf_session__process_compressed_event_stub
#endif

static int perf_session__deliver_event(struct perf_session *session,
				       union perf_event *event,
				       struct perf_tool *tool,
				       u64 file_offset,
				       const char *file_path);

static int perf_session__open(struct perf_session *session, int repipe_fd)
{
	struct perf_data *data = session->data;

	if (perf_session__read_header(session, repipe_fd) < 0) {
		pr_err("incompatible file format (rerun with -v to learn more)\n");
		return -1;
	}

	if (perf_header__has_feat(&session->header, HEADER_AUXTRACE)) {
		/* Auxiliary events may reference exited threads, hold onto dead ones. */
		symbol_conf.keep_exited_threads = true;
	}

	if (perf_data__is_pipe(data))
		return 0;

	if (perf_header__has_feat(&session->header, HEADER_STAT))
		return 0;

	if (!evlist__valid_sample_type(session->evlist)) {
		pr_err("non matching sample_type\n");
		return -1;
	}

	if (!evlist__valid_sample_id_all(session->evlist)) {
		pr_err("non matching sample_id_all\n");
		return -1;
	}

	if (!evlist__valid_read_format(session->evlist)) {
		pr_err("non matching read_format\n");
		return -1;
	}

	return 0;
}

void perf_session__set_id_hdr_size(struct perf_session *session)
{
	u16 id_hdr_size = evlist__id_hdr_size(session->evlist);

	machines__set_id_hdr_size(&session->machines, id_hdr_size);
}

int perf_session__create_kernel_maps(struct perf_session *session)
{
	int ret = machine__create_kernel_maps(&session->machines.host);

	if (ret >= 0)
		ret = machines__create_guest_kernel_maps(&session->machines);
	return ret;
}

static void perf_session__destroy_kernel_maps(struct perf_session *session)
{
	machines__destroy_kernel_maps(&session->machines);
}

static bool perf_session__has_comm_exec(struct perf_session *session)
{
	struct evsel *evsel;

	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->core.attr.comm_exec)
			return true;
	}

	return false;
}

static void perf_session__set_comm_exec(struct perf_session *session)
{
	bool comm_exec = perf_session__has_comm_exec(session);

	machines__set_comm_exec(&session->machines, comm_exec);
}

static int ordered_events__deliver_event(struct ordered_events *oe,
					 struct ordered_event *event)
{
	struct perf_session *session = container_of(oe, struct perf_session,
						    ordered_events);

	return perf_session__deliver_event(session, event->event,
					   session->tool, event->file_offset,
					   event->file_path);
}

struct perf_session *__perf_session__new(struct perf_data *data,
					 bool repipe, int repipe_fd,
					 struct perf_tool *tool)
{
	int ret = -ENOMEM;
	struct perf_session *session = zalloc(sizeof(*session));

	if (!session)
		goto out;

	session->repipe = repipe;
	session->tool   = tool;
	session->decomp_data.zstd_decomp = &session->zstd_data;
	session->active_decomp = &session->decomp_data;
	INIT_LIST_HEAD(&session->auxtrace_index);
	machines__init(&session->machines);
	ordered_events__init(&session->ordered_events,
			     ordered_events__deliver_event, NULL);

	perf_env__init(&session->header.env);
	if (data) {
		ret = perf_data__open(data);
		if (ret < 0)
			goto out_delete;

		session->data = data;

		if (perf_data__is_read(data)) {
			ret = perf_session__open(session, repipe_fd);
			if (ret < 0)
				goto out_delete;

			/*
			 * set session attributes that are present in perf.data
			 * but not in pipe-mode.
			 */
			if (!data->is_pipe) {
				perf_session__set_id_hdr_size(session);
				perf_session__set_comm_exec(session);
			}

			evlist__init_trace_event_sample_raw(session->evlist);

			/* Open the directory data. */
			if (data->is_dir) {
				ret = perf_data__open_dir(data);
				if (ret)
					goto out_delete;
			}

			if (!symbol_conf.kallsyms_name &&
			    !symbol_conf.vmlinux_name)
				symbol_conf.kallsyms_name = perf_data__kallsyms_name(data);
		}
	} else  {
		session->machines.host.env = &perf_env;
	}

	session->machines.host.single_address_space =
		perf_env__single_address_space(session->machines.host.env);

	if (!data || perf_data__is_write(data)) {
		/*
		 * In O_RDONLY mode this will be performed when reading the
		 * kernel MMAP event, in perf_event__process_mmap().
		 */
		if (perf_session__create_kernel_maps(session) < 0)
			pr_warning("Cannot read kernel map\n");
	}

	/*
	 * In pipe-mode, evlist is empty until PERF_RECORD_HEADER_ATTR is
	 * processed, so evlist__sample_id_all is not meaningful here.
	 */
	if ((!data || !data->is_pipe) && tool && tool->ordering_requires_timestamps &&
	    tool->ordered_events && !evlist__sample_id_all(session->evlist)) {
		dump_printf("WARNING: No sample_id_all support, falling back to unordered processing\n");
		tool->ordered_events = false;
	}

	return session;

 out_delete:
	perf_session__delete(session);
 out:
	return ERR_PTR(ret);
}

static void perf_decomp__release_events(struct decomp *next)
{
	struct decomp *decomp;
	size_t mmap_len;

	do {
		decomp = next;
		if (decomp == NULL)
			break;
		next = decomp->next;
		mmap_len = decomp->mmap_len;
		munmap(decomp, mmap_len);
	} while (1);
}

void perf_session__delete(struct perf_session *session)
{
	if (session == NULL)
		return;
	auxtrace__free(session);
	auxtrace_index__free(&session->auxtrace_index);
	perf_session__destroy_kernel_maps(session);
	perf_decomp__release_events(session->decomp_data.decomp);
	perf_env__exit(&session->header.env);
	machines__exit(&session->machines);
	if (session->data) {
		if (perf_data__is_read(session->data))
			evlist__delete(session->evlist);
		perf_data__close(session->data);
	}
#ifdef HAVE_LIBTRACEEVENT
	trace_event__cleanup(&session->tevent);
#endif
	free(session);
}

static int process_event_synth_tracing_data_stub(struct perf_session *session
						 __maybe_unused,
						 union perf_event *event
						 __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_synth_attr_stub(struct perf_tool *tool __maybe_unused,
					 union perf_event *event __maybe_unused,
					 struct evlist **pevlist
					 __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_synth_event_update_stub(struct perf_tool *tool __maybe_unused,
						 union perf_event *event __maybe_unused,
						 struct evlist **pevlist
						 __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_event_update(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_sample_stub(struct perf_tool *tool __maybe_unused,
				     union perf_event *event __maybe_unused,
				     struct perf_sample *sample __maybe_unused,
				     struct evsel *evsel __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_stub(struct perf_tool *tool __maybe_unused,
			      union perf_event *event __maybe_unused,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_finished_round_stub(struct perf_tool *tool __maybe_unused,
				       union perf_event *event __maybe_unused,
				       struct ordered_events *oe __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int skipn(int fd, off_t n)
{
	char buf[4096];
	ssize_t ret;

	while (n > 0) {
		ret = read(fd, buf, min(n, (off_t)sizeof(buf)));
		if (ret <= 0)
			return ret;
		n -= ret;
	}

	return 0;
}

static s64 process_event_auxtrace_stub(struct perf_session *session __maybe_unused,
				       union perf_event *event)
{
	dump_printf(": unhandled!\n");
	if (perf_data__is_pipe(session->data))
		skipn(perf_data__fd(session->data), event->auxtrace.size);
	return event->auxtrace.size;
}

static int process_event_op2_stub(struct perf_session *session __maybe_unused,
				  union perf_event *event __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}


static
int process_event_thread_map_stub(struct perf_session *session __maybe_unused,
				  union perf_event *event __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_thread_map(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static
int process_event_cpu_map_stub(struct perf_session *session __maybe_unused,
			       union perf_event *event __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_cpu_map(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static
int process_event_stat_config_stub(struct perf_session *session __maybe_unused,
				   union perf_event *event __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_stat_config(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static int process_stat_stub(struct perf_session *perf_session __maybe_unused,
			     union perf_event *event)
{
	if (dump_trace)
		perf_event__fprintf_stat(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static int process_stat_round_stub(struct perf_session *perf_session __maybe_unused,
				   union perf_event *event)
{
	if (dump_trace)
		perf_event__fprintf_stat_round(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_time_conv_stub(struct perf_session *perf_session __maybe_unused,
					union perf_event *event)
{
	if (dump_trace)
		perf_event__fprintf_time_conv(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

static int perf_session__process_compressed_event_stub(struct perf_session *session __maybe_unused,
						       union perf_event *event __maybe_unused,
						       u64 file_offset __maybe_unused,
						       const char *file_path __maybe_unused)
{
       dump_printf(": unhandled!\n");
       return 0;
}

void perf_tool__fill_defaults(struct perf_tool *tool)
{
	if (tool->sample == NULL)
		tool->sample = process_event_sample_stub;
	if (tool->mmap == NULL)
		tool->mmap = process_event_stub;
	if (tool->mmap2 == NULL)
		tool->mmap2 = process_event_stub;
	if (tool->comm == NULL)
		tool->comm = process_event_stub;
	if (tool->namespaces == NULL)
		tool->namespaces = process_event_stub;
	if (tool->cgroup == NULL)
		tool->cgroup = process_event_stub;
	if (tool->fork == NULL)
		tool->fork = process_event_stub;
	if (tool->exit == NULL)
		tool->exit = process_event_stub;
	if (tool->lost == NULL)
		tool->lost = perf_event__process_lost;
	if (tool->lost_samples == NULL)
		tool->lost_samples = perf_event__process_lost_samples;
	if (tool->aux == NULL)
		tool->aux = perf_event__process_aux;
	if (tool->itrace_start == NULL)
		tool->itrace_start = perf_event__process_itrace_start;
	if (tool->context_switch == NULL)
		tool->context_switch = perf_event__process_switch;
	if (tool->ksymbol == NULL)
		tool->ksymbol = perf_event__process_ksymbol;
	if (tool->bpf == NULL)
		tool->bpf = perf_event__process_bpf;
	if (tool->text_poke == NULL)
		tool->text_poke = perf_event__process_text_poke;
	if (tool->aux_output_hw_id == NULL)
		tool->aux_output_hw_id = perf_event__process_aux_output_hw_id;
	if (tool->read == NULL)
		tool->read = process_event_sample_stub;
	if (tool->throttle == NULL)
		tool->throttle = process_event_stub;
	if (tool->unthrottle == NULL)
		tool->unthrottle = process_event_stub;
	if (tool->attr == NULL)
		tool->attr = process_event_synth_attr_stub;
	if (tool->event_update == NULL)
		tool->event_update = process_event_synth_event_update_stub;
	if (tool->tracing_data == NULL)
		tool->tracing_data = process_event_synth_tracing_data_stub;
	if (tool->build_id == NULL)
		tool->build_id = process_event_op2_stub;
	if (tool->finished_round == NULL) {
		if (tool->ordered_events)
			tool->finished_round = perf_event__process_finished_round;
		else
			tool->finished_round = process_finished_round_stub;
	}
	if (tool->id_index == NULL)
		tool->id_index = process_event_op2_stub;
	if (tool->auxtrace_info == NULL)
		tool->auxtrace_info = process_event_op2_stub;
	if (tool->auxtrace == NULL)
		tool->auxtrace = process_event_auxtrace_stub;
	if (tool->auxtrace_error == NULL)
		tool->auxtrace_error = process_event_op2_stub;
	if (tool->thread_map == NULL)
		tool->thread_map = process_event_thread_map_stub;
	if (tool->cpu_map == NULL)
		tool->cpu_map = process_event_cpu_map_stub;
	if (tool->stat_config == NULL)
		tool->stat_config = process_event_stat_config_stub;
	if (tool->stat == NULL)
		tool->stat = process_stat_stub;
	if (tool->stat_round == NULL)
		tool->stat_round = process_stat_round_stub;
	if (tool->time_conv == NULL)
		tool->time_conv = process_event_time_conv_stub;
	if (tool->feature == NULL)
		tool->feature = process_event_op2_stub;
	if (tool->compressed == NULL)
		tool->compressed = perf_session__process_compressed_event;
	if (tool->finished_init == NULL)
		tool->finished_init = process_event_op2_stub;
}

static void swap_sample_id_all(union perf_event *event, void *data)
{
	void *end = (void *) event + event->header.size;
	int size = end - data;

	BUG_ON(size % sizeof(u64));
	mem_bswap_64(data, size);
}

static void perf_event__all64_swap(union perf_event *event,
				   bool sample_id_all __maybe_unused)
{
	struct perf_event_header *hdr = &event->header;
	mem_bswap_64(hdr + 1, event->header.size - sizeof(*hdr));
}

static void perf_event__comm_swap(union perf_event *event, bool sample_id_all)
{
	event->comm.pid = bswap_32(event->comm.pid);
	event->comm.tid = bswap_32(event->comm.tid);

	if (sample_id_all) {
		void *data = &event->comm.comm;

		data += PERF_ALIGN(strlen(data) + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
}

static void perf_event__mmap_swap(union perf_event *event,
				  bool sample_id_all)
{
	event->mmap.pid	  = bswap_32(event->mmap.pid);
	event->mmap.tid	  = bswap_32(event->mmap.tid);
	event->mmap.start = bswap_64(event->mmap.start);
	event->mmap.len	  = bswap_64(event->mmap.len);
	event->mmap.pgoff = bswap_64(event->mmap.pgoff);

	if (sample_id_all) {
		void *data = &event->mmap.filename;

		data += PERF_ALIGN(strlen(data) + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
}

static void perf_event__mmap2_swap(union perf_event *event,
				  bool sample_id_all)
{
	event->mmap2.pid   = bswap_32(event->mmap2.pid);
	event->mmap2.tid   = bswap_32(event->mmap2.tid);
	event->mmap2.start = bswap_64(event->mmap2.start);
	event->mmap2.len   = bswap_64(event->mmap2.len);
	event->mmap2.pgoff = bswap_64(event->mmap2.pgoff);

	if (!(event->header.misc & PERF_RECORD_MISC_MMAP_BUILD_ID)) {
		event->mmap2.maj   = bswap_32(event->mmap2.maj);
		event->mmap2.min   = bswap_32(event->mmap2.min);
		event->mmap2.ino   = bswap_64(event->mmap2.ino);
		event->mmap2.ino_generation = bswap_64(event->mmap2.ino_generation);
	}

	if (sample_id_all) {
		void *data = &event->mmap2.filename;

		data += PERF_ALIGN(strlen(data) + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
}
static void perf_event__task_swap(union perf_event *event, bool sample_id_all)
{
	event->fork.pid	 = bswap_32(event->fork.pid);
	event->fork.tid	 = bswap_32(event->fork.tid);
	event->fork.ppid = bswap_32(event->fork.ppid);
	event->fork.ptid = bswap_32(event->fork.ptid);
	event->fork.time = bswap_64(event->fork.time);

	if (sample_id_all)
		swap_sample_id_all(event, &event->fork + 1);
}

static void perf_event__read_swap(union perf_event *event, bool sample_id_all)
{
	event->read.pid		 = bswap_32(event->read.pid);
	event->read.tid		 = bswap_32(event->read.tid);
	event->read.value	 = bswap_64(event->read.value);
	event->read.time_enabled = bswap_64(event->read.time_enabled);
	event->read.time_running = bswap_64(event->read.time_running);
	event->read.id		 = bswap_64(event->read.id);

	if (sample_id_all)
		swap_sample_id_all(event, &event->read + 1);
}

static void perf_event__aux_swap(union perf_event *event, bool sample_id_all)
{
	event->aux.aux_offset = bswap_64(event->aux.aux_offset);
	event->aux.aux_size   = bswap_64(event->aux.aux_size);
	event->aux.flags      = bswap_64(event->aux.flags);

	if (sample_id_all)
		swap_sample_id_all(event, &event->aux + 1);
}

static void perf_event__itrace_start_swap(union perf_event *event,
					  bool sample_id_all)
{
	event->itrace_start.pid	 = bswap_32(event->itrace_start.pid);
	event->itrace_start.tid	 = bswap_32(event->itrace_start.tid);

	if (sample_id_all)
		swap_sample_id_all(event, &event->itrace_start + 1);
}

static void perf_event__switch_swap(union perf_event *event, bool sample_id_all)
{
	if (event->header.type == PERF_RECORD_SWITCH_CPU_WIDE) {
		event->context_switch.next_prev_pid =
				bswap_32(event->context_switch.next_prev_pid);
		event->context_switch.next_prev_tid =
				bswap_32(event->context_switch.next_prev_tid);
	}

	if (sample_id_all)
		swap_sample_id_all(event, &event->context_switch + 1);
}

static void perf_event__text_poke_swap(union perf_event *event, bool sample_id_all)
{
	event->text_poke.addr    = bswap_64(event->text_poke.addr);
	event->text_poke.old_len = bswap_16(event->text_poke.old_len);
	event->text_poke.new_len = bswap_16(event->text_poke.new_len);

	if (sample_id_all) {
		size_t len = sizeof(event->text_poke.old_len) +
			     sizeof(event->text_poke.new_len) +
			     event->text_poke.old_len +
			     event->text_poke.new_len;
		void *data = &event->text_poke.old_len;

		data += PERF_ALIGN(len, sizeof(u64));
		swap_sample_id_all(event, data);
	}
}

static void perf_event__throttle_swap(union perf_event *event,
				      bool sample_id_all)
{
	event->throttle.time	  = bswap_64(event->throttle.time);
	event->throttle.id	  = bswap_64(event->throttle.id);
	event->throttle.stream_id = bswap_64(event->throttle.stream_id);

	if (sample_id_all)
		swap_sample_id_all(event, &event->throttle + 1);
}

static void perf_event__namespaces_swap(union perf_event *event,
					bool sample_id_all)
{
	u64 i;

	event->namespaces.pid		= bswap_32(event->namespaces.pid);
	event->namespaces.tid		= bswap_32(event->namespaces.tid);
	event->namespaces.nr_namespaces	= bswap_64(event->namespaces.nr_namespaces);

	for (i = 0; i < event->namespaces.nr_namespaces; i++) {
		struct perf_ns_link_info *ns = &event->namespaces.link_info[i];

		ns->dev = bswap_64(ns->dev);
		ns->ino = bswap_64(ns->ino);
	}

	if (sample_id_all)
		swap_sample_id_all(event, &event->namespaces.link_info[i]);
}

static void perf_event__cgroup_swap(union perf_event *event, bool sample_id_all)
{
	event->cgroup.id = bswap_64(event->cgroup.id);

	if (sample_id_all) {
		void *data = &event->cgroup.path;

		data += PERF_ALIGN(strlen(data) + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
}

static u8 revbyte(u8 b)
{
	int rev = (b >> 4) | ((b & 0xf) << 4);
	rev = ((rev & 0xcc) >> 2) | ((rev & 0x33) << 2);
	rev = ((rev & 0xaa) >> 1) | ((rev & 0x55) << 1);
	return (u8) rev;
}

/*
 * XXX this is hack in attempt to carry flags bitfield
 * through endian village. ABI says:
 *
 * Bit-fields are allocated from right to left (least to most significant)
 * on little-endian implementations and from left to right (most to least
 * significant) on big-endian implementations.
 *
 * The above seems to be byte specific, so we need to reverse each
 * byte of the bitfield. 'Internet' also says this might be implementation
 * specific and we probably need proper fix and carry perf_event_attr
 * bitfield flags in separate data file FEAT_ section. Thought this seems
 * to work for now.
 */
static void swap_bitfield(u8 *p, unsigned len)
{
	unsigned i;

	for (i = 0; i < len; i++) {
		*p = revbyte(*p);
		p++;
	}
}

/* exported for swapping attributes in file header */
void perf_event__attr_swap(struct perf_event_attr *attr)
{
	attr->type		= bswap_32(attr->type);
	attr->size		= bswap_32(attr->size);

#define bswap_safe(f, n) 					\
	(attr->size > (offsetof(struct perf_event_attr, f) + 	\
		       sizeof(attr->f) * (n)))
#define bswap_field(f, sz) 			\
do { 						\
	if (bswap_safe(f, 0))			\
		attr->f = bswap_##sz(attr->f);	\
} while(0)
#define bswap_field_16(f) bswap_field(f, 16)
#define bswap_field_32(f) bswap_field(f, 32)
#define bswap_field_64(f) bswap_field(f, 64)

	bswap_field_64(config);
	bswap_field_64(sample_period);
	bswap_field_64(sample_type);
	bswap_field_64(read_format);
	bswap_field_32(wakeup_events);
	bswap_field_32(bp_type);
	bswap_field_64(bp_addr);
	bswap_field_64(bp_len);
	bswap_field_64(branch_sample_type);
	bswap_field_64(sample_regs_user);
	bswap_field_32(sample_stack_user);
	bswap_field_32(aux_watermark);
	bswap_field_16(sample_max_stack);
	bswap_field_32(aux_sample_size);

	/*
	 * After read_format are bitfields. Check read_format because
	 * we are unable to use offsetof on bitfield.
	 */
	if (bswap_safe(read_format, 1))
		swap_bitfield((u8 *) (&attr->read_format + 1),
			      sizeof(u64));
#undef bswap_field_64
#undef bswap_field_32
#undef bswap_field
#undef bswap_safe
}

static void perf_event__hdr_attr_swap(union perf_event *event,
				      bool sample_id_all __maybe_unused)
{
	size_t size;

	perf_event__attr_swap(&event->attr.attr);

	size = event->header.size;
	size -= perf_record_header_attr_id(event) - (void *)event;
	mem_bswap_64(perf_record_header_attr_id(event), size);
}

static void perf_event__event_update_swap(union perf_event *event,
					  bool sample_id_all __maybe_unused)
{
	event->event_update.type = bswap_64(event->event_update.type);
	event->event_update.id   = bswap_64(event->event_update.id);
}

static void perf_event__event_type_swap(union perf_event *event,
					bool sample_id_all __maybe_unused)
{
	event->event_type.event_type.event_id =
		bswap_64(event->event_type.event_type.event_id);
}

static void perf_event__tracing_data_swap(union perf_event *event,
					  bool sample_id_all __maybe_unused)
{
	event->tracing_data.size = bswap_32(event->tracing_data.size);
}

static void perf_event__auxtrace_info_swap(union perf_event *event,
					   bool sample_id_all __maybe_unused)
{
	size_t size;

	event->auxtrace_info.type = bswap_32(event->auxtrace_info.type);

	size = event->header.size;
	size -= (void *)&event->auxtrace_info.priv - (void *)event;
	mem_bswap_64(event->auxtrace_info.priv, size);
}

static void perf_event__auxtrace_swap(union perf_event *event,
				      bool sample_id_all __maybe_unused)
{
	event->auxtrace.size      = bswap_64(event->auxtrace.size);
	event->auxtrace.offset    = bswap_64(event->auxtrace.offset);
	event->auxtrace.reference = bswap_64(event->auxtrace.reference);
	event->auxtrace.idx       = bswap_32(event->auxtrace.idx);
	event->auxtrace.tid       = bswap_32(event->auxtrace.tid);
	event->auxtrace.cpu       = bswap_32(event->auxtrace.cpu);
}

static void perf_event__auxtrace_error_swap(union perf_event *event,
					    bool sample_id_all __maybe_unused)
{
	event->auxtrace_error.type = bswap_32(event->auxtrace_error.type);
	event->auxtrace_error.code = bswap_32(event->auxtrace_error.code);
	event->auxtrace_error.cpu  = bswap_32(event->auxtrace_error.cpu);
	event->auxtrace_error.pid  = bswap_32(event->auxtrace_error.pid);
	event->auxtrace_error.tid  = bswap_32(event->auxtrace_error.tid);
	event->auxtrace_error.fmt  = bswap_32(event->auxtrace_error.fmt);
	event->auxtrace_error.ip   = bswap_64(event->auxtrace_error.ip);
	if (event->auxtrace_error.fmt)
		event->auxtrace_error.time = bswap_64(event->auxtrace_error.time);
	if (event->auxtrace_error.fmt >= 2) {
		event->auxtrace_error.machine_pid = bswap_32(event->auxtrace_error.machine_pid);
		event->auxtrace_error.vcpu = bswap_32(event->auxtrace_error.vcpu);
	}
}

static void perf_event__thread_map_swap(union perf_event *event,
					bool sample_id_all __maybe_unused)
{
	unsigned i;

	event->thread_map.nr = bswap_64(event->thread_map.nr);

	for (i = 0; i < event->thread_map.nr; i++)
		event->thread_map.entries[i].pid = bswap_64(event->thread_map.entries[i].pid);
}

static void perf_event__cpu_map_swap(union perf_event *event,
				     bool sample_id_all __maybe_unused)
{
	struct perf_record_cpu_map_data *data = &event->cpu_map.data;

	data->type = bswap_16(data->type);

	switch (data->type) {
	case PERF_CPU_MAP__CPUS:
		data->cpus_data.nr = bswap_16(data->cpus_data.nr);

		for (unsigned i = 0; i < data->cpus_data.nr; i++)
			data->cpus_data.cpu[i] = bswap_16(data->cpus_data.cpu[i]);
		break;
	case PERF_CPU_MAP__MASK:
		data->mask32_data.long_size = bswap_16(data->mask32_data.long_size);

		switch (data->mask32_data.long_size) {
		case 4:
			data->mask32_data.nr = bswap_16(data->mask32_data.nr);
			for (unsigned i = 0; i < data->mask32_data.nr; i++)
				data->mask32_data.mask[i] = bswap_32(data->mask32_data.mask[i]);
			break;
		case 8:
			data->mask64_data.nr = bswap_16(data->mask64_data.nr);
			for (unsigned i = 0; i < data->mask64_data.nr; i++)
				data->mask64_data.mask[i] = bswap_64(data->mask64_data.mask[i]);
			break;
		default:
			pr_err("cpu_map swap: unsupported long size\n");
		}
		break;
	case PERF_CPU_MAP__RANGE_CPUS:
		data->range_cpu_data.start_cpu = bswap_16(data->range_cpu_data.start_cpu);
		data->range_cpu_data.end_cpu = bswap_16(data->range_cpu_data.end_cpu);
		break;
	default:
		break;
	}
}

static void perf_event__stat_config_swap(union perf_event *event,
					 bool sample_id_all __maybe_unused)
{
	u64 size;

	size  = bswap_64(event->stat_config.nr) * sizeof(event->stat_config.data[0]);
	size += 1; /* nr item itself */
	mem_bswap_64(&event->stat_config.nr, size);
}

static void perf_event__stat_swap(union perf_event *event,
				  bool sample_id_all __maybe_unused)
{
	event->stat.id     = bswap_64(event->stat.id);
	event->stat.thread = bswap_32(event->stat.thread);
	event->stat.cpu    = bswap_32(event->stat.cpu);
	event->stat.val    = bswap_64(event->stat.val);
	event->stat.ena    = bswap_64(event->stat.ena);
	event->stat.run    = bswap_64(event->stat.run);
}

static void perf_event__stat_round_swap(union perf_event *event,
					bool sample_id_all __maybe_unused)
{
	event->stat_round.type = bswap_64(event->stat_round.type);
	event->stat_round.time = bswap_64(event->stat_round.time);
}

static void perf_event__time_conv_swap(union perf_event *event,
				       bool sample_id_all __maybe_unused)
{
	event->time_conv.time_shift = bswap_64(event->time_conv.time_shift);
	event->time_conv.time_mult  = bswap_64(event->time_conv.time_mult);
	event->time_conv.time_zero  = bswap_64(event->time_conv.time_zero);

	if (event_contains(event->time_conv, time_cycles)) {
		event->time_conv.time_cycles = bswap_64(event->time_conv.time_cycles);
		event->time_conv.time_mask = bswap_64(event->time_conv.time_mask);
	}
}

typedef void (*perf_event__swap_op)(union perf_event *event,
				    bool sample_id_all);

static perf_event__swap_op perf_event__swap_ops[] = {
	[PERF_RECORD_MMAP]		  = perf_event__mmap_swap,
	[PERF_RECORD_MMAP2]		  = perf_event__mmap2_swap,
	[PERF_RECORD_COMM]		  = perf_event__comm_swap,
	[PERF_RECORD_FORK]		  = perf_event__task_swap,
	[PERF_RECORD_EXIT]		  = perf_event__task_swap,
	[PERF_RECORD_LOST]		  = perf_event__all64_swap,
	[PERF_RECORD_READ]		  = perf_event__read_swap,
	[PERF_RECORD_THROTTLE]		  = perf_event__throttle_swap,
	[PERF_RECORD_UNTHROTTLE]	  = perf_event__throttle_swap,
	[PERF_RECORD_SAMPLE]		  = perf_event__all64_swap,
	[PERF_RECORD_AUX]		  = perf_event__aux_swap,
	[PERF_RECORD_ITRACE_START]	  = perf_event__itrace_start_swap,
	[PERF_RECORD_LOST_SAMPLES]	  = perf_event__all64_swap,
	[PERF_RECORD_SWITCH]		  = perf_event__switch_swap,
	[PERF_RECORD_SWITCH_CPU_WIDE]	  = perf_event__switch_swap,
	[PERF_RECORD_NAMESPACES]	  = perf_event__namespaces_swap,
	[PERF_RECORD_CGROUP]		  = perf_event__cgroup_swap,
	[PERF_RECORD_TEXT_POKE]		  = perf_event__text_poke_swap,
	[PERF_RECORD_AUX_OUTPUT_HW_ID]	  = perf_event__all64_swap,
	[PERF_RECORD_HEADER_ATTR]	  = perf_event__hdr_attr_swap,
	[PERF_RECORD_HEADER_EVENT_TYPE]	  = perf_event__event_type_swap,
	[PERF_RECORD_HEADER_TRACING_DATA] = perf_event__tracing_data_swap,
	[PERF_RECORD_HEADER_BUILD_ID]	  = NULL,
	[PERF_RECORD_ID_INDEX]		  = perf_event__all64_swap,
	[PERF_RECORD_AUXTRACE_INFO]	  = perf_event__auxtrace_info_swap,
	[PERF_RECORD_AUXTRACE]		  = perf_event__auxtrace_swap,
	[PERF_RECORD_AUXTRACE_ERROR]	  = perf_event__auxtrace_error_swap,
	[PERF_RECORD_THREAD_MAP]	  = perf_event__thread_map_swap,
	[PERF_RECORD_CPU_MAP]		  = perf_event__cpu_map_swap,
	[PERF_RECORD_STAT_CONFIG]	  = perf_event__stat_config_swap,
	[PERF_RECORD_STAT]		  = perf_event__stat_swap,
	[PERF_RECORD_STAT_ROUND]	  = perf_event__stat_round_swap,
	[PERF_RECORD_EVENT_UPDATE]	  = perf_event__event_update_swap,
	[PERF_RECORD_TIME_CONV]		  = perf_event__time_conv_swap,
	[PERF_RECORD_HEADER_MAX]	  = NULL,
};

/*
 * When perf record finishes a pass on every buffers, it records this pseudo
 * event.
 * We record the max timestamp t found in the pass n.
 * Assuming these timestamps are monotonic across cpus, we know that if
 * a buffer still has events with timestamps below t, they will be all
 * available and then read in the pass n + 1.
 * Hence when we start to read the pass n + 2, we can safely flush every
 * events with timestamps below t.
 *
 *    ============ PASS n =================
 *       CPU 0         |   CPU 1
 *                     |
 *    cnt1 timestamps  |   cnt2 timestamps
 *          1          |         2
 *          2          |         3
 *          -          |         4  <--- max recorded
 *
 *    ============ PASS n + 1 ==============
 *       CPU 0         |   CPU 1
 *                     |
 *    cnt1 timestamps  |   cnt2 timestamps
 *          3          |         5
 *          4          |         6
 *          5          |         7 <---- max recorded
 *
 *      Flush every events below timestamp 4
 *
 *    ============ PASS n + 2 ==============
 *       CPU 0         |   CPU 1
 *                     |
 *    cnt1 timestamps  |   cnt2 timestamps
 *          6          |         8
 *          7          |         9
 *          -          |         10
 *
 *      Flush every events below timestamp 7
 *      etc...
 */
int perf_event__process_finished_round(struct perf_tool *tool __maybe_unused,
				       union perf_event *event __maybe_unused,
				       struct ordered_events *oe)
{
	if (dump_trace)
		fprintf(stdout, "\n");
	return ordered_events__flush(oe, OE_FLUSH__ROUND);
}

int perf_session__queue_event(struct perf_session *s, union perf_event *event,
			      u64 timestamp, u64 file_offset, const char *file_path)
{
	return ordered_events__queue(&s->ordered_events, event, timestamp, file_offset, file_path);
}

static void callchain__lbr_callstack_printf(struct perf_sample *sample)
{
	struct ip_callchain *callchain = sample->callchain;
	struct branch_stack *lbr_stack = sample->branch_stack;
	struct branch_entry *entries = perf_sample__branch_entries(sample);
	u64 kernel_callchain_nr = callchain->nr;
	unsigned int i;

	for (i = 0; i < kernel_callchain_nr; i++) {
		if (callchain->ips[i] == PERF_CONTEXT_USER)
			break;
	}

	if ((i != kernel_callchain_nr) && lbr_stack->nr) {
		u64 total_nr;
		/*
		 * LBR callstack can only get user call chain,
		 * i is kernel call chain number,
		 * 1 is PERF_CONTEXT_USER.
		 *
		 * The user call chain is stored in LBR registers.
		 * LBR are pair registers. The caller is stored
		 * in "from" register, while the callee is stored
		 * in "to" register.
		 * For example, there is a call stack
		 * "A"->"B"->"C"->"D".
		 * The LBR registers will be recorded like
		 * "C"->"D", "B"->"C", "A"->"B".
		 * So only the first "to" register and all "from"
		 * registers are needed to construct the whole stack.
		 */
		total_nr = i + 1 + lbr_stack->nr + 1;
		kernel_callchain_nr = i + 1;

		printf("... LBR call chain: nr:%" PRIu64 "\n", total_nr);

		for (i = 0; i < kernel_callchain_nr; i++)
			printf("..... %2d: %016" PRIx64 "\n",
			       i, callchain->ips[i]);

		printf("..... %2d: %016" PRIx64 "\n",
		       (int)(kernel_callchain_nr), entries[0].to);
		for (i = 0; i < lbr_stack->nr; i++)
			printf("..... %2d: %016" PRIx64 "\n",
			       (int)(i + kernel_callchain_nr + 1), entries[i].from);
	}
}

static void callchain__printf(struct evsel *evsel,
			      struct perf_sample *sample)
{
	unsigned int i;
	struct ip_callchain *callchain = sample->callchain;

	if (evsel__has_branch_callstack(evsel))
		callchain__lbr_callstack_printf(sample);

	printf("... FP chain: nr:%" PRIu64 "\n", callchain->nr);

	for (i = 0; i < callchain->nr; i++)
		printf("..... %2d: %016" PRIx64 "\n",
		       i, callchain->ips[i]);
}

static void branch_stack__printf(struct perf_sample *sample,
				 struct evsel *evsel)
{
	struct branch_entry *entries = perf_sample__branch_entries(sample);
	bool callstack = evsel__has_branch_callstack(evsel);
	u64 *branch_stack_cntr = sample->branch_stack_cntr;
	struct perf_env *env = evsel__env(evsel);
	uint64_t i;

	if (!callstack) {
		printf("%s: nr:%" PRIu64 "\n", "... branch stack", sample->branch_stack->nr);
	} else {
		/* the reason of adding 1 to nr is because after expanding
		 * branch stack it generates nr + 1 callstack records. e.g.,
		 *         B()->C()
		 *         A()->B()
		 * the final callstack should be:
		 *         C()
		 *         B()
		 *         A()
		 */
		printf("%s: nr:%" PRIu64 "\n", "... branch callstack", sample->branch_stack->nr+1);
	}

	for (i = 0; i < sample->branch_stack->nr; i++) {
		struct branch_entry *e = &entries[i];

		if (!callstack) {
			printf("..... %2"PRIu64": %016" PRIx64 " -> %016" PRIx64 " %hu cycles %s%s%s%s %x %s %s\n",
				i, e->from, e->to,
				(unsigned short)e->flags.cycles,
				e->flags.mispred ? "M" : " ",
				e->flags.predicted ? "P" : " ",
				e->flags.abort ? "A" : " ",
				e->flags.in_tx ? "T" : " ",
				(unsigned)e->flags.reserved,
				get_branch_type(e),
				e->flags.spec ? branch_spec_desc(e->flags.spec) : "");
		} else {
			if (i == 0) {
				printf("..... %2"PRIu64": %016" PRIx64 "\n"
				       "..... %2"PRIu64": %016" PRIx64 "\n",
						i, e->to, i+1, e->from);
			} else {
				printf("..... %2"PRIu64": %016" PRIx64 "\n", i+1, e->from);
			}
		}
	}

	if (branch_stack_cntr) {
		printf("... branch stack counters: nr:%" PRIu64 " (counter width: %u max counter nr:%u)\n",
			sample->branch_stack->nr, env->br_cntr_width, env->br_cntr_nr);
		for (i = 0; i < sample->branch_stack->nr; i++)
			printf("..... %2"PRIu64": %016" PRIx64 "\n", i, branch_stack_cntr[i]);
	}
}

static void regs_dump__printf(u64 mask, u64 *regs, const char *arch)
{
	unsigned rid, i = 0;

	for_each_set_bit(rid, (unsigned long *) &mask, sizeof(mask) * 8) {
		u64 val = regs[i++];

		printf(".... %-5s 0x%016" PRIx64 "\n",
		       perf_reg_name(rid, arch), val);
	}
}

static const char *regs_abi[] = {
	[PERF_SAMPLE_REGS_ABI_NONE] = "none",
	[PERF_SAMPLE_REGS_ABI_32] = "32-bit",
	[PERF_SAMPLE_REGS_ABI_64] = "64-bit",
};

static inline const char *regs_dump_abi(struct regs_dump *d)
{
	if (d->abi > PERF_SAMPLE_REGS_ABI_64)
		return "unknown";

	return regs_abi[d->abi];
}

static void regs__printf(const char *type, struct regs_dump *regs, const char *arch)
{
	u64 mask = regs->mask;

	printf("... %s regs: mask 0x%" PRIx64 " ABI %s\n",
	       type,
	       mask,
	       regs_dump_abi(regs));

	regs_dump__printf(mask, regs->regs, arch);
}

static void regs_user__printf(struct perf_sample *sample, const char *arch)
{
	struct regs_dump *user_regs = &sample->user_regs;

	if (user_regs->regs)
		regs__printf("user", user_regs, arch);
}

static void regs_intr__printf(struct perf_sample *sample, const char *arch)
{
	struct regs_dump *intr_regs = &sample->intr_regs;

	if (intr_regs->regs)
		regs__printf("intr", intr_regs, arch);
}

static void stack_user__printf(struct stack_dump *dump)
{
	printf("... ustack: size %" PRIu64 ", offset 0x%x\n",
	       dump->size, dump->offset);
}

static void evlist__print_tstamp(struct evlist *evlist, union perf_event *event, struct perf_sample *sample)
{
	u64 sample_type = __evlist__combined_sample_type(evlist);

	if (event->header.type != PERF_RECORD_SAMPLE &&
	    !evlist__sample_id_all(evlist)) {
		fputs("-1 -1 ", stdout);
		return;
	}

	if ((sample_type & PERF_SAMPLE_CPU))
		printf("%u ", sample->cpu);

	if (sample_type & PERF_SAMPLE_TIME)
		printf("%" PRIu64 " ", sample->time);
}

static void sample_read__printf(struct perf_sample *sample, u64 read_format)
{
	printf("... sample_read:\n");

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		printf("...... time enabled %016" PRIx64 "\n",
		       sample->read.time_enabled);

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		printf("...... time running %016" PRIx64 "\n",
		       sample->read.time_running);

	if (read_format & PERF_FORMAT_GROUP) {
		struct sample_read_value *value = sample->read.group.values;

		printf(".... group nr %" PRIu64 "\n", sample->read.group.nr);

		sample_read_group__for_each(value, sample->read.group.nr, read_format) {
			printf("..... id %016" PRIx64
			       ", value %016" PRIx64,
			       value->id, value->value);
			if (read_format & PERF_FORMAT_LOST)
				printf(", lost %" PRIu64, value->lost);
			printf("\n");
		}
	} else {
		printf("..... id %016" PRIx64 ", value %016" PRIx64,
			sample->read.one.id, sample->read.one.value);
		if (read_format & PERF_FORMAT_LOST)
			printf(", lost %" PRIu64, sample->read.one.lost);
		printf("\n");
	}
}

static void dump_event(struct evlist *evlist, union perf_event *event,
		       u64 file_offset, struct perf_sample *sample,
		       const char *file_path)
{
	if (!dump_trace)
		return;

	printf("\n%#" PRIx64 "@%s [%#x]: event: %d\n",
	       file_offset, file_path, event->header.size, event->header.type);

	trace_event(event);
	if (event->header.type == PERF_RECORD_SAMPLE && evlist->trace_event_sample_raw)
		evlist->trace_event_sample_raw(evlist, event, sample);

	if (sample)
		evlist__print_tstamp(evlist, event, sample);

	printf("%#" PRIx64 " [%#x]: PERF_RECORD_%s", file_offset,
	       event->header.size, perf_event__name(event->header.type));
}

char *get_page_size_name(u64 size, char *str)
{
	if (!size || !unit_number__scnprintf(str, PAGE_SIZE_NAME_LEN, size))
		snprintf(str, PAGE_SIZE_NAME_LEN, "%s", "N/A");

	return str;
}

static void dump_sample(struct evsel *evsel, union perf_event *event,
			struct perf_sample *sample, const char *arch)
{
	u64 sample_type;
	char str[PAGE_SIZE_NAME_LEN];

	if (!dump_trace)
		return;

	printf("(IP, 0x%x): %d/%d: %#" PRIx64 " period: %" PRIu64 " addr: %#" PRIx64 "\n",
	       event->header.misc, sample->pid, sample->tid, sample->ip,
	       sample->period, sample->addr);

	sample_type = evsel->core.attr.sample_type;

	if (evsel__has_callchain(evsel))
		callchain__printf(evsel, sample);

	if (evsel__has_br_stack(evsel))
		branch_stack__printf(sample, evsel);

	if (sample_type & PERF_SAMPLE_REGS_USER)
		regs_user__printf(sample, arch);

	if (sample_type & PERF_SAMPLE_REGS_INTR)
		regs_intr__printf(sample, arch);

	if (sample_type & PERF_SAMPLE_STACK_USER)
		stack_user__printf(&sample->user_stack);

	if (sample_type & PERF_SAMPLE_WEIGHT_TYPE) {
		printf("... weight: %" PRIu64 "", sample->weight);
			if (sample_type & PERF_SAMPLE_WEIGHT_STRUCT) {
				printf(",0x%"PRIx16"", sample->ins_lat);
				printf(",0x%"PRIx16"", sample->p_stage_cyc);
			}
		printf("\n");
	}

	if (sample_type & PERF_SAMPLE_DATA_SRC)
		printf(" . data_src: 0x%"PRIx64"\n", sample->data_src);

	if (sample_type & PERF_SAMPLE_PHYS_ADDR)
		printf(" .. phys_addr: 0x%"PRIx64"\n", sample->phys_addr);

	if (sample_type & PERF_SAMPLE_DATA_PAGE_SIZE)
		printf(" .. data page size: %s\n", get_page_size_name(sample->data_page_size, str));

	if (sample_type & PERF_SAMPLE_CODE_PAGE_SIZE)
		printf(" .. code page size: %s\n", get_page_size_name(sample->code_page_size, str));

	if (sample_type & PERF_SAMPLE_TRANSACTION)
		printf("... transaction: %" PRIx64 "\n", sample->transaction);

	if (sample_type & PERF_SAMPLE_READ)
		sample_read__printf(sample, evsel->core.attr.read_format);
}

static void dump_read(struct evsel *evsel, union perf_event *event)
{
	struct perf_record_read *read_event = &event->read;
	u64 read_format;

	if (!dump_trace)
		return;

	printf(": %d %d %s %" PRI_lu64 "\n", event->read.pid, event->read.tid,
	       evsel__name(evsel), event->read.value);

	if (!evsel)
		return;

	read_format = evsel->core.attr.read_format;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		printf("... time enabled : %" PRI_lu64 "\n", read_event->time_enabled);

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		printf("... time running : %" PRI_lu64 "\n", read_event->time_running);

	if (read_format & PERF_FORMAT_ID)
		printf("... id           : %" PRI_lu64 "\n", read_event->id);

	if (read_format & PERF_FORMAT_LOST)
		printf("... lost         : %" PRI_lu64 "\n", read_event->lost);
}

static struct machine *machines__find_for_cpumode(struct machines *machines,
					       union perf_event *event,
					       struct perf_sample *sample)
{
	if (perf_guest &&
	    ((sample->cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ||
	     (sample->cpumode == PERF_RECORD_MISC_GUEST_USER))) {
		u32 pid;

		if (sample->machine_pid)
			pid = sample->machine_pid;
		else if (event->header.type == PERF_RECORD_MMAP
		    || event->header.type == PERF_RECORD_MMAP2)
			pid = event->mmap.pid;
		else
			pid = sample->pid;

		/*
		 * Guest code machine is created as needed and does not use
		 * DEFAULT_GUEST_KERNEL_ID.
		 */
		if (symbol_conf.guest_code)
			return machines__findnew(machines, pid);

		return machines__find_guest(machines, pid);
	}

	return &machines->host;
}

static int deliver_sample_value(struct evlist *evlist,
				struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct sample_read_value *v,
				struct machine *machine)
{
	struct perf_sample_id *sid = evlist__id2sid(evlist, v->id);
	struct evsel *evsel;

	if (sid) {
		sample->id     = v->id;
		sample->period = v->value - sid->period;
		sid->period    = v->value;
	}

	if (!sid || sid->evsel == NULL) {
		++evlist->stats.nr_unknown_id;
		return 0;
	}

	/*
	 * There's no reason to deliver sample
	 * for zero period, bail out.
	 */
	if (!sample->period)
		return 0;

	evsel = container_of(sid->evsel, struct evsel, core);
	return tool->sample(tool, event, sample, evsel, machine);
}

static int deliver_sample_group(struct evlist *evlist,
				struct perf_tool *tool,
				union  perf_event *event,
				struct perf_sample *sample,
				struct machine *machine,
				u64 read_format)
{
	int ret = -EINVAL;
	struct sample_read_value *v = sample->read.group.values;

	sample_read_group__for_each(v, sample->read.group.nr, read_format) {
		ret = deliver_sample_value(evlist, tool, event, sample, v,
					   machine);
		if (ret)
			break;
	}

	return ret;
}

static int evlist__deliver_sample(struct evlist *evlist, struct perf_tool *tool,
				  union  perf_event *event, struct perf_sample *sample,
				  struct evsel *evsel, struct machine *machine)
{
	/* We know evsel != NULL. */
	u64 sample_type = evsel->core.attr.sample_type;
	u64 read_format = evsel->core.attr.read_format;

	/* Standard sample delivery. */
	if (!(sample_type & PERF_SAMPLE_READ))
		return tool->sample(tool, event, sample, evsel, machine);

	/* For PERF_SAMPLE_READ we have either single or group mode. */
	if (read_format & PERF_FORMAT_GROUP)
		return deliver_sample_group(evlist, tool, event, sample,
					    machine, read_format);
	else
		return deliver_sample_value(evlist, tool, event, sample,
					    &sample->read.one, machine);
}

static int machines__deliver_event(struct machines *machines,
				   struct evlist *evlist,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct perf_tool *tool, u64 file_offset,
				   const char *file_path)
{
	struct evsel *evsel;
	struct machine *machine;

	dump_event(evlist, event, file_offset, sample, file_path);

	evsel = evlist__id2evsel(evlist, sample->id);

	machine = machines__find_for_cpumode(machines, event, sample);

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		if (evsel == NULL) {
			++evlist->stats.nr_unknown_id;
			return 0;
		}
		if (machine == NULL) {
			++evlist->stats.nr_unprocessable_samples;
			dump_sample(evsel, event, sample, perf_env__arch(NULL));
			return 0;
		}
		dump_sample(evsel, event, sample, perf_env__arch(machine->env));
		return evlist__deliver_sample(evlist, tool, event, sample, evsel, machine);
	case PERF_RECORD_MMAP:
		return tool->mmap(tool, event, sample, machine);
	case PERF_RECORD_MMAP2:
		if (event->header.misc & PERF_RECORD_MISC_PROC_MAP_PARSE_TIMEOUT)
			++evlist->stats.nr_proc_map_timeout;
		return tool->mmap2(tool, event, sample, machine);
	case PERF_RECORD_COMM:
		return tool->comm(tool, event, sample, machine);
	case PERF_RECORD_NAMESPACES:
		return tool->namespaces(tool, event, sample, machine);
	case PERF_RECORD_CGROUP:
		return tool->cgroup(tool, event, sample, machine);
	case PERF_RECORD_FORK:
		return tool->fork(tool, event, sample, machine);
	case PERF_RECORD_EXIT:
		return tool->exit(tool, event, sample, machine);
	case PERF_RECORD_LOST:
		if (tool->lost == perf_event__process_lost)
			evlist->stats.total_lost += event->lost.lost;
		return tool->lost(tool, event, sample, machine);
	case PERF_RECORD_LOST_SAMPLES:
		if (tool->lost_samples == perf_event__process_lost_samples &&
		    !(event->header.misc & PERF_RECORD_MISC_LOST_SAMPLES_BPF))
			evlist->stats.total_lost_samples += event->lost_samples.lost;
		return tool->lost_samples(tool, event, sample, machine);
	case PERF_RECORD_READ:
		dump_read(evsel, event);
		return tool->read(tool, event, sample, evsel, machine);
	case PERF_RECORD_THROTTLE:
		return tool->throttle(tool, event, sample, machine);
	case PERF_RECORD_UNTHROTTLE:
		return tool->unthrottle(tool, event, sample, machine);
	case PERF_RECORD_AUX:
		if (tool->aux == perf_event__process_aux) {
			if (event->aux.flags & PERF_AUX_FLAG_TRUNCATED)
				evlist->stats.total_aux_lost += 1;
			if (event->aux.flags & PERF_AUX_FLAG_PARTIAL)
				evlist->stats.total_aux_partial += 1;
			if (event->aux.flags & PERF_AUX_FLAG_COLLISION)
				evlist->stats.total_aux_collision += 1;
		}
		return tool->aux(tool, event, sample, machine);
	case PERF_RECORD_ITRACE_START:
		return tool->itrace_start(tool, event, sample, machine);
	case PERF_RECORD_SWITCH:
	case PERF_RECORD_SWITCH_CPU_WIDE:
		return tool->context_switch(tool, event, sample, machine);
	case PERF_RECORD_KSYMBOL:
		return tool->ksymbol(tool, event, sample, machine);
	case PERF_RECORD_BPF_EVENT:
		return tool->bpf(tool, event, sample, machine);
	case PERF_RECORD_TEXT_POKE:
		return tool->text_poke(tool, event, sample, machine);
	case PERF_RECORD_AUX_OUTPUT_HW_ID:
		return tool->aux_output_hw_id(tool, event, sample, machine);
	default:
		++evlist->stats.nr_unknown_events;
		return -1;
	}
}

static int perf_session__deliver_event(struct perf_session *session,
				       union perf_event *event,
				       struct perf_tool *tool,
				       u64 file_offset,
				       const char *file_path)
{
	struct perf_sample sample;
	int ret = evlist__parse_sample(session->evlist, event, &sample);

	if (ret) {
		pr_err("Can't parse sample, err = %d\n", ret);
		return ret;
	}

	ret = auxtrace__process_event(session, event, &sample, tool);
	if (ret < 0)
		return ret;
	if (ret > 0)
		return 0;

	ret = machines__deliver_event(&session->machines, session->evlist,
				      event, &sample, tool, file_offset, file_path);

	if (dump_trace && sample.aux_sample.size)
		auxtrace__dump_auxtrace_sample(session, &sample);

	return ret;
}

static s64 perf_session__process_user_event(struct perf_session *session,
					    union perf_event *event,
					    u64 file_offset,
					    const char *file_path)
{
	struct ordered_events *oe = &session->ordered_events;
	struct perf_tool *tool = session->tool;
	struct perf_sample sample = { .time = 0, };
	int fd = perf_data__fd(session->data);
	int err;

	if (event->header.type != PERF_RECORD_COMPRESSED ||
	    tool->compressed == perf_session__process_compressed_event_stub)
		dump_event(session->evlist, event, file_offset, &sample, file_path);

	/* These events are processed right away */
	switch (event->header.type) {
	case PERF_RECORD_HEADER_ATTR:
		err = tool->attr(tool, event, &session->evlist);
		if (err == 0) {
			perf_session__set_id_hdr_size(session);
			perf_session__set_comm_exec(session);
		}
		return err;
	case PERF_RECORD_EVENT_UPDATE:
		return tool->event_update(tool, event, &session->evlist);
	case PERF_RECORD_HEADER_EVENT_TYPE:
		/*
		 * Deprecated, but we need to handle it for sake
		 * of old data files create in pipe mode.
		 */
		return 0;
	case PERF_RECORD_HEADER_TRACING_DATA:
		/*
		 * Setup for reading amidst mmap, but only when we
		 * are in 'file' mode. The 'pipe' fd is in proper
		 * place already.
		 */
		if (!perf_data__is_pipe(session->data))
			lseek(fd, file_offset, SEEK_SET);
		return tool->tracing_data(session, event);
	case PERF_RECORD_HEADER_BUILD_ID:
		return tool->build_id(session, event);
	case PERF_RECORD_FINISHED_ROUND:
		return tool->finished_round(tool, event, oe);
	case PERF_RECORD_ID_INDEX:
		return tool->id_index(session, event);
	case PERF_RECORD_AUXTRACE_INFO:
		return tool->auxtrace_info(session, event);
	case PERF_RECORD_AUXTRACE:
		/*
		 * Setup for reading amidst mmap, but only when we
		 * are in 'file' mode.  The 'pipe' fd is in proper
		 * place already.
		 */
		if (!perf_data__is_pipe(session->data))
			lseek(fd, file_offset + event->header.size, SEEK_SET);
		return tool->auxtrace(session, event);
	case PERF_RECORD_AUXTRACE_ERROR:
		perf_session__auxtrace_error_inc(session, event);
		return tool->auxtrace_error(session, event);
	case PERF_RECORD_THREAD_MAP:
		return tool->thread_map(session, event);
	case PERF_RECORD_CPU_MAP:
		return tool->cpu_map(session, event);
	case PERF_RECORD_STAT_CONFIG:
		return tool->stat_config(session, event);
	case PERF_RECORD_STAT:
		return tool->stat(session, event);
	case PERF_RECORD_STAT_ROUND:
		return tool->stat_round(session, event);
	case PERF_RECORD_TIME_CONV:
		session->time_conv = event->time_conv;
		return tool->time_conv(session, event);
	case PERF_RECORD_HEADER_FEATURE:
		return tool->feature(session, event);
	case PERF_RECORD_COMPRESSED:
		err = tool->compressed(session, event, file_offset, file_path);
		if (err)
			dump_event(session->evlist, event, file_offset, &sample, file_path);
		return err;
	case PERF_RECORD_FINISHED_INIT:
		return tool->finished_init(session, event);
	default:
		return -EINVAL;
	}
}

int perf_session__deliver_synth_event(struct perf_session *session,
				      union perf_event *event,
				      struct perf_sample *sample)
{
	struct evlist *evlist = session->evlist;
	struct perf_tool *tool = session->tool;

	events_stats__inc(&evlist->stats, event->header.type);

	if (event->header.type >= PERF_RECORD_USER_TYPE_START)
		return perf_session__process_user_event(session, event, 0, NULL);

	return machines__deliver_event(&session->machines, evlist, event, sample, tool, 0, NULL);
}

static void event_swap(union perf_event *event, bool sample_id_all)
{
	perf_event__swap_op swap;

	swap = perf_event__swap_ops[event->header.type];
	if (swap)
		swap(event, sample_id_all);
}

int perf_session__peek_event(struct perf_session *session, off_t file_offset,
			     void *buf, size_t buf_sz,
			     union perf_event **event_ptr,
			     struct perf_sample *sample)
{
	union perf_event *event;
	size_t hdr_sz, rest;
	int fd;

	if (session->one_mmap && !session->header.needs_swap) {
		event = file_offset - session->one_mmap_offset +
			session->one_mmap_addr;
		goto out_parse_sample;
	}

	if (perf_data__is_pipe(session->data))
		return -1;

	fd = perf_data__fd(session->data);
	hdr_sz = sizeof(struct perf_event_header);

	if (buf_sz < hdr_sz)
		return -1;

	if (lseek(fd, file_offset, SEEK_SET) == (off_t)-1 ||
	    readn(fd, buf, hdr_sz) != (ssize_t)hdr_sz)
		return -1;

	event = (union perf_event *)buf;

	if (session->header.needs_swap)
		perf_event_header__bswap(&event->header);

	if (event->header.size < hdr_sz || event->header.size > buf_sz)
		return -1;

	buf += hdr_sz;
	rest = event->header.size - hdr_sz;

	if (readn(fd, buf, rest) != (ssize_t)rest)
		return -1;

	if (session->header.needs_swap)
		event_swap(event, evlist__sample_id_all(session->evlist));

out_parse_sample:

	if (sample && event->header.type < PERF_RECORD_USER_TYPE_START &&
	    evlist__parse_sample(session->evlist, event, sample))
		return -1;

	*event_ptr = event;

	return 0;
}

int perf_session__peek_events(struct perf_session *session, u64 offset,
			      u64 size, peek_events_cb_t cb, void *data)
{
	u64 max_offset = offset + size;
	char buf[PERF_SAMPLE_MAX_SIZE];
	union perf_event *event;
	int err;

	do {
		err = perf_session__peek_event(session, offset, buf,
					       PERF_SAMPLE_MAX_SIZE, &event,
					       NULL);
		if (err)
			return err;

		err = cb(session, event, offset, data);
		if (err)
			return err;

		offset += event->header.size;
		if (event->header.type == PERF_RECORD_AUXTRACE)
			offset += event->auxtrace.size;

	} while (offset < max_offset);

	return err;
}

static s64 perf_session__process_event(struct perf_session *session,
				       union perf_event *event, u64 file_offset,
				       const char *file_path)
{
	struct evlist *evlist = session->evlist;
	struct perf_tool *tool = session->tool;
	int ret;

	if (session->header.needs_swap)
		event_swap(event, evlist__sample_id_all(evlist));

	if (event->header.type >= PERF_RECORD_HEADER_MAX)
		return -EINVAL;

	events_stats__inc(&evlist->stats, event->header.type);

	if (event->header.type >= PERF_RECORD_USER_TYPE_START)
		return perf_session__process_user_event(session, event, file_offset, file_path);

	if (tool->ordered_events) {
		u64 timestamp = -1ULL;

		ret = evlist__parse_sample_timestamp(evlist, event, &timestamp);
		if (ret && ret != -1)
			return ret;

		ret = perf_session__queue_event(session, event, timestamp, file_offset, file_path);
		if (ret != -ETIME)
			return ret;
	}

	return perf_session__deliver_event(session, event, tool, file_offset, file_path);
}

void perf_event_header__bswap(struct perf_event_header *hdr)
{
	hdr->type = bswap_32(hdr->type);
	hdr->misc = bswap_16(hdr->misc);
	hdr->size = bswap_16(hdr->size);
}

struct thread *perf_session__findnew(struct perf_session *session, pid_t pid)
{
	return machine__findnew_thread(&session->machines.host, -1, pid);
}

int perf_session__register_idle_thread(struct perf_session *session)
{
	struct thread *thread = machine__idle_thread(&session->machines.host);

	/* machine__idle_thread() got the thread, so put it */
	thread__put(thread);
	return thread ? 0 : -1;
}

static void
perf_session__warn_order(const struct perf_session *session)
{
	const struct ordered_events *oe = &session->ordered_events;
	struct evsel *evsel;
	bool should_warn = true;

	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->core.attr.write_backward)
			should_warn = false;
	}

	if (!should_warn)
		return;
	if (oe->nr_unordered_events != 0)
		ui__warning("%u out of order events recorded.\n", oe->nr_unordered_events);
}

static void perf_session__warn_about_errors(const struct perf_session *session)
{
	const struct events_stats *stats = &session->evlist->stats;

	if (session->tool->lost == perf_event__process_lost &&
	    stats->nr_events[PERF_RECORD_LOST] != 0) {
		ui__warning("Processed %d events and lost %d chunks!\n\n"
			    "Check IO/CPU overload!\n\n",
			    stats->nr_events[0],
			    stats->nr_events[PERF_RECORD_LOST]);
	}

	if (session->tool->lost_samples == perf_event__process_lost_samples) {
		double drop_rate;

		drop_rate = (double)stats->total_lost_samples /
			    (double) (stats->nr_events[PERF_RECORD_SAMPLE] + stats->total_lost_samples);
		if (drop_rate > 0.05) {
			ui__warning("Processed %" PRIu64 " samples and lost %3.2f%%!\n\n",
				    stats->nr_events[PERF_RECORD_SAMPLE] + stats->total_lost_samples,
				    drop_rate * 100.0);
		}
	}

	if (session->tool->aux == perf_event__process_aux &&
	    stats->total_aux_lost != 0) {
		ui__warning("AUX data lost %" PRIu64 " times out of %u!\n\n",
			    stats->total_aux_lost,
			    stats->nr_events[PERF_RECORD_AUX]);
	}

	if (session->tool->aux == perf_event__process_aux &&
	    stats->total_aux_partial != 0) {
		bool vmm_exclusive = false;

		(void)sysfs__read_bool("module/kvm_intel/parameters/vmm_exclusive",
		                       &vmm_exclusive);

		ui__warning("AUX data had gaps in it %" PRIu64 " times out of %u!\n\n"
		            "Are you running a KVM guest in the background?%s\n\n",
			    stats->total_aux_partial,
			    stats->nr_events[PERF_RECORD_AUX],
			    vmm_exclusive ?
			    "\nReloading kvm_intel module with vmm_exclusive=0\n"
			    "will reduce the gaps to only guest's timeslices." :
			    "");
	}

	if (session->tool->aux == perf_event__process_aux &&
	    stats->total_aux_collision != 0) {
		ui__warning("AUX data detected collision  %" PRIu64 " times out of %u!\n\n",
			    stats->total_aux_collision,
			    stats->nr_events[PERF_RECORD_AUX]);
	}

	if (stats->nr_unknown_events != 0) {
		ui__warning("Found %u unknown events!\n\n"
			    "Is this an older tool processing a perf.data "
			    "file generated by a more recent tool?\n\n"
			    "If that is not the case, consider "
			    "reporting to linux-kernel@vger.kernel.org.\n\n",
			    stats->nr_unknown_events);
	}

	if (stats->nr_unknown_id != 0) {
		ui__warning("%u samples with id not present in the header\n",
			    stats->nr_unknown_id);
	}

	if (stats->nr_invalid_chains != 0) {
		ui__warning("Found invalid callchains!\n\n"
			    "%u out of %u events were discarded for this reason.\n\n"
			    "Consider reporting to linux-kernel@vger.kernel.org.\n\n",
			    stats->nr_invalid_chains,
			    stats->nr_events[PERF_RECORD_SAMPLE]);
	}

	if (stats->nr_unprocessable_samples != 0) {
		ui__warning("%u unprocessable samples recorded.\n"
			    "Do you have a KVM guest running and not using 'perf kvm'?\n",
			    stats->nr_unprocessable_samples);
	}

	perf_session__warn_order(session);

	events_stats__auxtrace_error_warn(stats);

	if (stats->nr_proc_map_timeout != 0) {
		ui__warning("%d map information files for pre-existing threads were\n"
			    "not processed, if there are samples for addresses they\n"
			    "will not be resolved, you may find out which are these\n"
			    "threads by running with -v and redirecting the output\n"
			    "to a file.\n"
			    "The time limit to process proc map is too short?\n"
			    "Increase it by --proc-map-timeout\n",
			    stats->nr_proc_map_timeout);
	}
}

static int perf_session__flush_thread_stack(struct thread *thread,
					    void *p __maybe_unused)
{
	return thread_stack__flush(thread);
}

static int perf_session__flush_thread_stacks(struct perf_session *session)
{
	return machines__for_each_thread(&session->machines,
					 perf_session__flush_thread_stack,
					 NULL);
}

volatile sig_atomic_t session_done;

static int __perf_session__process_decomp_events(struct perf_session *session);

static int __perf_session__process_pipe_events(struct perf_session *session)
{
	struct ordered_events *oe = &session->ordered_events;
	struct perf_tool *tool = session->tool;
	union perf_event *event;
	uint32_t size, cur_size = 0;
	void *buf = NULL;
	s64 skip = 0;
	u64 head;
	ssize_t err;
	void *p;

	perf_tool__fill_defaults(tool);

	head = 0;
	cur_size = sizeof(union perf_event);

	buf = malloc(cur_size);
	if (!buf)
		return -errno;
	ordered_events__set_copy_on_queue(oe, true);
more:
	event = buf;
	err = perf_data__read(session->data, event,
			      sizeof(struct perf_event_header));
	if (err <= 0) {
		if (err == 0)
			goto done;

		pr_err("failed to read event header\n");
		goto out_err;
	}

	if (session->header.needs_swap)
		perf_event_header__bswap(&event->header);

	size = event->header.size;
	if (size < sizeof(struct perf_event_header)) {
		pr_err("bad event header size\n");
		goto out_err;
	}

	if (size > cur_size) {
		void *new = realloc(buf, size);
		if (!new) {
			pr_err("failed to allocate memory to read event\n");
			goto out_err;
		}
		buf = new;
		cur_size = size;
		event = buf;
	}
	p = event;
	p += sizeof(struct perf_event_header);

	if (size - sizeof(struct perf_event_header)) {
		err = perf_data__read(session->data, p,
				      size - sizeof(struct perf_event_header));
		if (err <= 0) {
			if (err == 0) {
				pr_err("unexpected end of event stream\n");
				goto done;
			}

			pr_err("failed to read event data\n");
			goto out_err;
		}
	}

	if ((skip = perf_session__process_event(session, event, head, "pipe")) < 0) {
		pr_err("%#" PRIx64 " [%#x]: failed to process type: %d\n",
		       head, event->header.size, event->header.type);
		err = -EINVAL;
		goto out_err;
	}

	head += size;

	if (skip > 0)
		head += skip;

	err = __perf_session__process_decomp_events(session);
	if (err)
		goto out_err;

	if (!session_done())
		goto more;
done:
	/* do the final flush for ordered samples */
	err = ordered_events__flush(oe, OE_FLUSH__FINAL);
	if (err)
		goto out_err;
	err = auxtrace__flush_events(session, tool);
	if (err)
		goto out_err;
	err = perf_session__flush_thread_stacks(session);
out_err:
	free(buf);
	if (!tool->no_warn)
		perf_session__warn_about_errors(session);
	ordered_events__free(&session->ordered_events);
	auxtrace__free_events(session);
	return err;
}

static union perf_event *
prefetch_event(char *buf, u64 head, size_t mmap_size,
	       bool needs_swap, union perf_event *error)
{
	union perf_event *event;
	u16 event_size;

	/*
	 * Ensure we have enough space remaining to read
	 * the size of the event in the headers.
	 */
	if (head + sizeof(event->header) > mmap_size)
		return NULL;

	event = (union perf_event *)(buf + head);
	if (needs_swap)
		perf_event_header__bswap(&event->header);

	event_size = event->header.size;
	if (head + event_size <= mmap_size)
		return event;

	/* We're not fetching the event so swap back again */
	if (needs_swap)
		perf_event_header__bswap(&event->header);

	/* Check if the event fits into the next mmapped buf. */
	if (event_size <= mmap_size - head % page_size) {
		/* Remap buf and fetch again. */
		return NULL;
	}

	/* Invalid input. Event size should never exceed mmap_size. */
	pr_debug("%s: head=%#" PRIx64 " event->header.size=%#x, mmap_size=%#zx:"
		 " fuzzed or compressed perf.data?\n", __func__, head, event_size, mmap_size);

	return error;
}

static union perf_event *
fetch_mmaped_event(u64 head, size_t mmap_size, char *buf, bool needs_swap)
{
	return prefetch_event(buf, head, mmap_size, needs_swap, ERR_PTR(-EINVAL));
}

static union perf_event *
fetch_decomp_event(u64 head, size_t mmap_size, char *buf, bool needs_swap)
{
	return prefetch_event(buf, head, mmap_size, needs_swap, NULL);
}

static int __perf_session__process_decomp_events(struct perf_session *session)
{
	s64 skip;
	u64 size;
	struct decomp *decomp = session->active_decomp->decomp_last;

	if (!decomp)
		return 0;

	while (decomp->head < decomp->size && !session_done()) {
		union perf_event *event = fetch_decomp_event(decomp->head, decomp->size, decomp->data,
							     session->header.needs_swap);

		if (!event)
			break;

		size = event->header.size;

		if (size < sizeof(struct perf_event_header) ||
		    (skip = perf_session__process_event(session, event, decomp->file_pos,
							decomp->file_path)) < 0) {
			pr_err("%#" PRIx64 " [%#x]: failed to process type: %d\n",
				decomp->file_pos + decomp->head, event->header.size, event->header.type);
			return -EINVAL;
		}

		if (skip)
			size += skip;

		decomp->head += size;
	}

	return 0;
}

/*
 * On 64bit we can mmap the data file in one go. No need for tiny mmap
 * slices. On 32bit we use 32MB.
 */
#if BITS_PER_LONG == 64
#define MMAP_SIZE ULLONG_MAX
#define NUM_MMAPS 1
#else
#define MMAP_SIZE (32 * 1024 * 1024ULL)
#define NUM_MMAPS 128
#endif

struct reader;

typedef s64 (*reader_cb_t)(struct perf_session *session,
			   union perf_event *event,
			   u64 file_offset,
			   const char *file_path);

struct reader {
	int		 fd;
	const char	 *path;
	u64		 data_size;
	u64		 data_offset;
	reader_cb_t	 process;
	bool		 in_place_update;
	char		 *mmaps[NUM_MMAPS];
	size_t		 mmap_size;
	int		 mmap_idx;
	char		 *mmap_cur;
	u64		 file_pos;
	u64		 file_offset;
	u64		 head;
	u64		 size;
	bool		 done;
	struct zstd_data   zstd_data;
	struct decomp_data decomp_data;
};

static int
reader__init(struct reader *rd, bool *one_mmap)
{
	u64 data_size = rd->data_size;
	char **mmaps = rd->mmaps;

	rd->head = rd->data_offset;
	data_size += rd->data_offset;

	rd->mmap_size = MMAP_SIZE;
	if (rd->mmap_size > data_size) {
		rd->mmap_size = data_size;
		if (one_mmap)
			*one_mmap = true;
	}

	memset(mmaps, 0, sizeof(rd->mmaps));

	if (zstd_init(&rd->zstd_data, 0))
		return -1;
	rd->decomp_data.zstd_decomp = &rd->zstd_data;

	return 0;
}

static void
reader__release_decomp(struct reader *rd)
{
	perf_decomp__release_events(rd->decomp_data.decomp);
	zstd_fini(&rd->zstd_data);
}

static int
reader__mmap(struct reader *rd, struct perf_session *session)
{
	int mmap_prot, mmap_flags;
	char *buf, **mmaps = rd->mmaps;
	u64 page_offset;

	mmap_prot  = PROT_READ;
	mmap_flags = MAP_SHARED;

	if (rd->in_place_update) {
		mmap_prot  |= PROT_WRITE;
	} else if (session->header.needs_swap) {
		mmap_prot  |= PROT_WRITE;
		mmap_flags = MAP_PRIVATE;
	}

	if (mmaps[rd->mmap_idx]) {
		munmap(mmaps[rd->mmap_idx], rd->mmap_size);
		mmaps[rd->mmap_idx] = NULL;
	}

	page_offset = page_size * (rd->head / page_size);
	rd->file_offset += page_offset;
	rd->head -= page_offset;

	buf = mmap(NULL, rd->mmap_size, mmap_prot, mmap_flags, rd->fd,
		   rd->file_offset);
	if (buf == MAP_FAILED) {
		pr_err("failed to mmap file\n");
		return -errno;
	}
	mmaps[rd->mmap_idx] = rd->mmap_cur = buf;
	rd->mmap_idx = (rd->mmap_idx + 1) & (ARRAY_SIZE(rd->mmaps) - 1);
	rd->file_pos = rd->file_offset + rd->head;
	if (session->one_mmap) {
		session->one_mmap_addr = buf;
		session->one_mmap_offset = rd->file_offset;
	}

	return 0;
}

enum {
	READER_OK,
	READER_NODATA,
};

static int
reader__read_event(struct reader *rd, struct perf_session *session,
		   struct ui_progress *prog)
{
	u64 size;
	int err = READER_OK;
	union perf_event *event;
	s64 skip;

	event = fetch_mmaped_event(rd->head, rd->mmap_size, rd->mmap_cur,
				   session->header.needs_swap);
	if (IS_ERR(event))
		return PTR_ERR(event);

	if (!event)
		return READER_NODATA;

	size = event->header.size;

	skip = -EINVAL;

	if (size < sizeof(struct perf_event_header) ||
	    (skip = rd->process(session, event, rd->file_pos, rd->path)) < 0) {
		pr_err("%#" PRIx64 " [%#x]: failed to process type: %d [%s]\n",
		       rd->file_offset + rd->head, event->header.size,
		       event->header.type, strerror(-skip));
		err = skip;
		goto out;
	}

	if (skip)
		size += skip;

	rd->size += size;
	rd->head += size;
	rd->file_pos += size;

	err = __perf_session__process_decomp_events(session);
	if (err)
		goto out;

	ui_progress__update(prog, size);

out:
	return err;
}

static inline bool
reader__eof(struct reader *rd)
{
	return (rd->file_pos >= rd->data_size + rd->data_offset);
}

static int
reader__process_events(struct reader *rd, struct perf_session *session,
		       struct ui_progress *prog)
{
	int err;

	err = reader__init(rd, &session->one_mmap);
	if (err)
		goto out;

	session->active_decomp = &rd->decomp_data;

remap:
	err = reader__mmap(rd, session);
	if (err)
		goto out;

more:
	err = reader__read_event(rd, session, prog);
	if (err < 0)
		goto out;
	else if (err == READER_NODATA)
		goto remap;

	if (session_done())
		goto out;

	if (!reader__eof(rd))
		goto more;

out:
	session->active_decomp = &session->decomp_data;
	return err;
}

static s64 process_simple(struct perf_session *session,
			  union perf_event *event,
			  u64 file_offset,
			  const char *file_path)
{
	return perf_session__process_event(session, event, file_offset, file_path);
}

static int __perf_session__process_events(struct perf_session *session)
{
	struct reader rd = {
		.fd		= perf_data__fd(session->data),
		.path		= session->data->file.path,
		.data_size	= session->header.data_size,
		.data_offset	= session->header.data_offset,
		.process	= process_simple,
		.in_place_update = session->data->in_place_update,
	};
	struct ordered_events *oe = &session->ordered_events;
	struct perf_tool *tool = session->tool;
	struct ui_progress prog;
	int err;

	perf_tool__fill_defaults(tool);

	if (rd.data_size == 0)
		return -1;

	ui_progress__init_size(&prog, rd.data_size, "Processing events...");

	err = reader__process_events(&rd, session, &prog);
	if (err)
		goto out_err;
	/* do the final flush for ordered samples */
	err = ordered_events__flush(oe, OE_FLUSH__FINAL);
	if (err)
		goto out_err;
	err = auxtrace__flush_events(session, tool);
	if (err)
		goto out_err;
	err = perf_session__flush_thread_stacks(session);
out_err:
	ui_progress__finish();
	if (!tool->no_warn)
		perf_session__warn_about_errors(session);
	/*
	 * We may switching perf.data output, make ordered_events
	 * reusable.
	 */
	ordered_events__reinit(&session->ordered_events);
	auxtrace__free_events(session);
	reader__release_decomp(&rd);
	session->one_mmap = false;
	return err;
}

/*
 * Processing 2 MB of data from each reader in sequence,
 * because that's the way the ordered events sorting works
 * most efficiently.
 */
#define READER_MAX_SIZE (2 * 1024 * 1024)

/*
 * This function reads, merge and process directory data.
 * It assumens the version 1 of directory data, where each
 * data file holds per-cpu data, already sorted by kernel.
 */
static int __perf_session__process_dir_events(struct perf_session *session)
{
	struct perf_data *data = session->data;
	struct perf_tool *tool = session->tool;
	int i, ret, readers, nr_readers;
	struct ui_progress prog;
	u64 total_size = perf_data__size(session->data);
	struct reader *rd;

	perf_tool__fill_defaults(tool);

	ui_progress__init_size(&prog, total_size, "Sorting events...");

	nr_readers = 1;
	for (i = 0; i < data->dir.nr; i++) {
		if (data->dir.files[i].size)
			nr_readers++;
	}

	rd = zalloc(nr_readers * sizeof(struct reader));
	if (!rd)
		return -ENOMEM;

	rd[0] = (struct reader) {
		.fd		 = perf_data__fd(session->data),
		.path		 = session->data->file.path,
		.data_size	 = session->header.data_size,
		.data_offset	 = session->header.data_offset,
		.process	 = process_simple,
		.in_place_update = session->data->in_place_update,
	};
	ret = reader__init(&rd[0], NULL);
	if (ret)
		goto out_err;
	ret = reader__mmap(&rd[0], session);
	if (ret)
		goto out_err;
	readers = 1;

	for (i = 0; i < data->dir.nr; i++) {
		if (!data->dir.files[i].size)
			continue;
		rd[readers] = (struct reader) {
			.fd		 = data->dir.files[i].fd,
			.path		 = data->dir.files[i].path,
			.data_size	 = data->dir.files[i].size,
			.data_offset	 = 0,
			.process	 = process_simple,
			.in_place_update = session->data->in_place_update,
		};
		ret = reader__init(&rd[readers], NULL);
		if (ret)
			goto out_err;
		ret = reader__mmap(&rd[readers], session);
		if (ret)
			goto out_err;
		readers++;
	}

	i = 0;
	while (readers) {
		if (session_done())
			break;

		if (rd[i].done) {
			i = (i + 1) % nr_readers;
			continue;
		}
		if (reader__eof(&rd[i])) {
			rd[i].done = true;
			readers--;
			continue;
		}

		session->active_decomp = &rd[i].decomp_data;
		ret = reader__read_event(&rd[i], session, &prog);
		if (ret < 0) {
			goto out_err;
		} else if (ret == READER_NODATA) {
			ret = reader__mmap(&rd[i], session);
			if (ret)
				goto out_err;
		}

		if (rd[i].size >= READER_MAX_SIZE) {
			rd[i].size = 0;
			i = (i + 1) % nr_readers;
		}
	}

	ret = ordered_events__flush(&session->ordered_events, OE_FLUSH__FINAL);
	if (ret)
		goto out_err;

	ret = perf_session__flush_thread_stacks(session);
out_err:
	ui_progress__finish();

	if (!tool->no_warn)
		perf_session__warn_about_errors(session);

	/*
	 * We may switching perf.data output, make ordered_events
	 * reusable.
	 */
	ordered_events__reinit(&session->ordered_events);

	session->one_mmap = false;

	session->active_decomp = &session->decomp_data;
	for (i = 0; i < nr_readers; i++)
		reader__release_decomp(&rd[i]);
	zfree(&rd);

	return ret;
}

int perf_session__process_events(struct perf_session *session)
{
	if (perf_session__register_idle_thread(session) < 0)
		return -ENOMEM;

	if (perf_data__is_pipe(session->data))
		return __perf_session__process_pipe_events(session);

	if (perf_data__is_dir(session->data) && session->data->dir.nr)
		return __perf_session__process_dir_events(session);

	return __perf_session__process_events(session);
}

bool perf_session__has_traces(struct perf_session *session, const char *msg)
{
	struct evsel *evsel;

	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->core.attr.type == PERF_TYPE_TRACEPOINT)
			return true;
	}

	pr_err("No trace sample to read. Did you call 'perf %s'?\n", msg);
	return false;
}

int map__set_kallsyms_ref_reloc_sym(struct map *map, const char *symbol_name, u64 addr)
{
	char *bracket;
	struct ref_reloc_sym *ref;
	struct kmap *kmap;

	ref = zalloc(sizeof(struct ref_reloc_sym));
	if (ref == NULL)
		return -ENOMEM;

	ref->name = strdup(symbol_name);
	if (ref->name == NULL) {
		free(ref);
		return -ENOMEM;
	}

	bracket = strchr(ref->name, ']');
	if (bracket)
		*bracket = '\0';

	ref->addr = addr;

	kmap = map__kmap(map);
	if (kmap)
		kmap->ref_reloc_sym = ref;

	return 0;
}

size_t perf_session__fprintf_dsos(struct perf_session *session, FILE *fp)
{
	return machines__fprintf_dsos(&session->machines, fp);
}

size_t perf_session__fprintf_dsos_buildid(struct perf_session *session, FILE *fp,
					  bool (skip)(struct dso *dso, int parm), int parm)
{
	return machines__fprintf_dsos_buildid(&session->machines, fp, skip, parm);
}

size_t perf_session__fprintf_nr_events(struct perf_session *session, FILE *fp,
				       bool skip_empty)
{
	size_t ret;
	const char *msg = "";

	if (perf_header__has_feat(&session->header, HEADER_AUXTRACE))
		msg = " (excludes AUX area (e.g. instruction trace) decoded / synthesized events)";

	ret = fprintf(fp, "\nAggregated stats:%s\n", msg);

	ret += events_stats__fprintf(&session->evlist->stats, fp, skip_empty);
	return ret;
}

size_t perf_session__fprintf(struct perf_session *session, FILE *fp)
{
	/*
	 * FIXME: Here we have to actually print all the machines in this
	 * session, not just the host...
	 */
	return machine__fprintf(&session->machines.host, fp);
}

struct evsel *perf_session__find_first_evtype(struct perf_session *session,
					      unsigned int type)
{
	struct evsel *pos;

	evlist__for_each_entry(session->evlist, pos) {
		if (pos->core.attr.type == type)
			return pos;
	}
	return NULL;
}

int perf_session__cpu_bitmap(struct perf_session *session,
			     const char *cpu_list, unsigned long *cpu_bitmap)
{
	int i, err = -1;
	struct perf_cpu_map *map;
	int nr_cpus = min(session->header.env.nr_cpus_avail, MAX_NR_CPUS);

	for (i = 0; i < PERF_TYPE_MAX; ++i) {
		struct evsel *evsel;

		evsel = perf_session__find_first_evtype(session, i);
		if (!evsel)
			continue;

		if (!(evsel->core.attr.sample_type & PERF_SAMPLE_CPU)) {
			pr_err("File does not contain CPU events. "
			       "Remove -C option to proceed.\n");
			return -1;
		}
	}

	map = perf_cpu_map__new(cpu_list);
	if (map == NULL) {
		pr_err("Invalid cpu_list\n");
		return -1;
	}

	for (i = 0; i < perf_cpu_map__nr(map); i++) {
		struct perf_cpu cpu = perf_cpu_map__cpu(map, i);

		if (cpu.cpu >= nr_cpus) {
			pr_err("Requested CPU %d too large. "
			       "Consider raising MAX_NR_CPUS\n", cpu.cpu);
			goto out_delete_map;
		}

		__set_bit(cpu.cpu, cpu_bitmap);
	}

	err = 0;

out_delete_map:
	perf_cpu_map__put(map);
	return err;
}

void perf_session__fprintf_info(struct perf_session *session, FILE *fp,
				bool full)
{
	if (session == NULL || fp == NULL)
		return;

	fprintf(fp, "# ========\n");
	perf_header__fprintf_info(session, fp, full);
	fprintf(fp, "# ========\n#\n");
}

static int perf_session__register_guest(struct perf_session *session, pid_t machine_pid)
{
	struct machine *machine = machines__findnew(&session->machines, machine_pid);
	struct thread *thread;

	if (!machine)
		return -ENOMEM;

	machine->single_address_space = session->machines.host.single_address_space;

	thread = machine__idle_thread(machine);
	if (!thread)
		return -ENOMEM;
	thread__put(thread);

	machine->kallsyms_filename = perf_data__guest_kallsyms_name(session->data, machine_pid);

	return 0;
}

static int perf_session__set_guest_cpu(struct perf_session *session, pid_t pid,
				       pid_t tid, int guest_cpu)
{
	struct machine *machine = &session->machines.host;
	struct thread *thread = machine__findnew_thread(machine, pid, tid);

	if (!thread)
		return -ENOMEM;
	thread__set_guest_cpu(thread, guest_cpu);
	thread__put(thread);

	return 0;
}

int perf_event__process_id_index(struct perf_session *session,
				 union perf_event *event)
{
	struct evlist *evlist = session->evlist;
	struct perf_record_id_index *ie = &event->id_index;
	size_t sz = ie->header.size - sizeof(*ie);
	size_t i, nr, max_nr;
	size_t e1_sz = sizeof(struct id_index_entry);
	size_t e2_sz = sizeof(struct id_index_entry_2);
	size_t etot_sz = e1_sz + e2_sz;
	struct id_index_entry_2 *e2;
	pid_t last_pid = 0;

	max_nr = sz / e1_sz;
	nr = ie->nr;
	if (nr > max_nr) {
		printf("Too big: nr %zu max_nr %zu\n", nr, max_nr);
		return -EINVAL;
	}

	if (sz >= nr * etot_sz) {
		max_nr = sz / etot_sz;
		if (nr > max_nr) {
			printf("Too big2: nr %zu max_nr %zu\n", nr, max_nr);
			return -EINVAL;
		}
		e2 = (void *)ie + sizeof(*ie) + nr * e1_sz;
	} else {
		e2 = NULL;
	}

	if (dump_trace)
		fprintf(stdout, " nr: %zu\n", nr);

	for (i = 0; i < nr; i++, (e2 ? e2++ : 0)) {
		struct id_index_entry *e = &ie->entries[i];
		struct perf_sample_id *sid;
		int ret;

		if (dump_trace) {
			fprintf(stdout,	" ... id: %"PRI_lu64, e->id);
			fprintf(stdout,	"  idx: %"PRI_lu64, e->idx);
			fprintf(stdout,	"  cpu: %"PRI_ld64, e->cpu);
			fprintf(stdout, "  tid: %"PRI_ld64, e->tid);
			if (e2) {
				fprintf(stdout, "  machine_pid: %"PRI_ld64, e2->machine_pid);
				fprintf(stdout, "  vcpu: %"PRI_lu64"\n", e2->vcpu);
			} else {
				fprintf(stdout, "\n");
			}
		}

		sid = evlist__id2sid(evlist, e->id);
		if (!sid)
			return -ENOENT;

		sid->idx = e->idx;
		sid->cpu.cpu = e->cpu;
		sid->tid = e->tid;

		if (!e2)
			continue;

		sid->machine_pid = e2->machine_pid;
		sid->vcpu.cpu = e2->vcpu;

		if (!sid->machine_pid)
			continue;

		if (sid->machine_pid != last_pid) {
			ret = perf_session__register_guest(session, sid->machine_pid);
			if (ret)
				return ret;
			last_pid = sid->machine_pid;
			perf_guest = true;
		}

		ret = perf_session__set_guest_cpu(session, sid->machine_pid, e->tid, e2->vcpu);
		if (ret)
			return ret;
	}
	return 0;
}
