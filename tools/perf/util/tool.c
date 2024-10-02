// SPDX-License-Identifier: GPL-2.0
#include "data.h"
#include "debug.h"
#include "header.h"
#include "session.h"
#include "stat.h"
#include "tool.h"
#include "tsc.h"
#include <sys/mman.h>
#include <unistd.h>

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
#endif

static int process_event_synth_tracing_data_stub(struct perf_session *session
						 __maybe_unused,
						 union perf_event *event
						 __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_synth_attr_stub(const struct perf_tool *tool __maybe_unused,
					 union perf_event *event __maybe_unused,
					 struct evlist **pevlist
					 __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_synth_event_update_stub(const struct perf_tool *tool __maybe_unused,
						 union perf_event *event __maybe_unused,
						 struct evlist **pevlist
						 __maybe_unused)
{
	if (dump_trace)
		perf_event__fprintf_event_update(event, stdout);

	dump_printf(": unhandled!\n");
	return 0;
}

int process_event_sample_stub(const struct perf_tool *tool __maybe_unused,
			      union perf_event *event __maybe_unused,
			      struct perf_sample *sample __maybe_unused,
			      struct evsel *evsel __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_stub(const struct perf_tool *tool __maybe_unused,
			      union perf_event *event __maybe_unused,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_finished_round_stub(const struct perf_tool *tool __maybe_unused,
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

void perf_tool__init(struct perf_tool *tool, bool ordered_events)
{
	tool->ordered_events = ordered_events;
	tool->ordering_requires_timestamps = false;
	tool->namespace_events = false;
	tool->cgroup_events = false;
	tool->no_warn = false;
	tool->show_feat_hdr = SHOW_FEAT_NO_HEADER;

	tool->sample = process_event_sample_stub;
	tool->mmap = process_event_stub;
	tool->mmap2 = process_event_stub;
	tool->comm = process_event_stub;
	tool->namespaces = process_event_stub;
	tool->cgroup = process_event_stub;
	tool->fork = process_event_stub;
	tool->exit = process_event_stub;
	tool->lost = perf_event__process_lost;
	tool->lost_samples = perf_event__process_lost_samples;
	tool->aux = perf_event__process_aux;
	tool->itrace_start = perf_event__process_itrace_start;
	tool->context_switch = perf_event__process_switch;
	tool->ksymbol = perf_event__process_ksymbol;
	tool->bpf = perf_event__process_bpf;
	tool->text_poke = perf_event__process_text_poke;
	tool->aux_output_hw_id = perf_event__process_aux_output_hw_id;
	tool->read = process_event_sample_stub;
	tool->throttle = process_event_stub;
	tool->unthrottle = process_event_stub;
	tool->attr = process_event_synth_attr_stub;
	tool->event_update = process_event_synth_event_update_stub;
	tool->tracing_data = process_event_synth_tracing_data_stub;
	tool->build_id = process_event_op2_stub;

	if (ordered_events)
		tool->finished_round = perf_event__process_finished_round;
	else
		tool->finished_round = process_finished_round_stub;

	tool->id_index = process_event_op2_stub;
	tool->auxtrace_info = process_event_op2_stub;
	tool->auxtrace = process_event_auxtrace_stub;
	tool->auxtrace_error = process_event_op2_stub;
	tool->thread_map = process_event_thread_map_stub;
	tool->cpu_map = process_event_cpu_map_stub;
	tool->stat_config = process_event_stat_config_stub;
	tool->stat = process_stat_stub;
	tool->stat_round = process_stat_round_stub;
	tool->time_conv = process_event_time_conv_stub;
	tool->feature = process_event_op2_stub;
#ifdef HAVE_ZSTD_SUPPORT
	tool->compressed = perf_session__process_compressed_event;
#else
	tool->compressed = perf_session__process_compressed_event_stub;
#endif
	tool->finished_init = process_event_op2_stub;
}

bool perf_tool__compressed_is_stub(const struct perf_tool *tool)
{
	return tool->compressed == perf_session__process_compressed_event_stub;
}
