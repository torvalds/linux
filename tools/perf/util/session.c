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
#include <perf/event.h>

#include "map_symbol.h"
#include "branch.h"
#include "debug.h"
#include "dwarf-regs.h"
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
#include "annotate.h"
#include "perf.h"
#include <internal/lib.h>

static int perf_session__deliver_event(struct perf_session *session,
				       union perf_event *event,
				       const struct perf_tool *tool,
				       u64 file_offset,
				       const char *file_path);

static int perf_session__open(struct perf_session *session)
{
	struct perf_data *data = session->data;

	if (perf_session__read_header(session) < 0) {
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
	int ret =  perf_session__deliver_event(session, event->event,
					       session->tool, event->file_offset,
					       event->file_path);

	if (ret) {
		pr_err("%#" PRIx64 " [%#x]: ordered event processing failed (%d) for event of type: %s (%d)\n",
			event->file_offset, event->event->header.size, ret,
			perf_event__name(event->event->header.type),
			event->event->header.type);
	}
	return ret;
}

struct perf_session *__perf_session__new(struct perf_data *data,
					 struct perf_tool *tool,
					 bool trace_event_repipe,
					 struct perf_env *host_env)
{
	int ret = -ENOMEM;
	struct perf_session *session = zalloc(sizeof(*session));

	if (!session)
		goto out;

	session->trace_event_repipe = trace_event_repipe;
	session->tool   = tool;
	session->decomp_data.zstd_decomp = &session->zstd_data;
	session->active_decomp = &session->decomp_data;
	INIT_LIST_HEAD(&session->auxtrace_index);
	perf_env__init(&session->header.env);
	if (machines__init(&session->machines))
		goto out_delete;

	ordered_events__init(&session->ordered_events,
			     ordered_events__deliver_event, NULL);
	if (data) {
		ret = perf_data__open(data);
		if (ret < 0)
			goto out_delete;

		session->data = data;

		if (perf_data__is_read(data)) {
			ret = perf_session__open(session);
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

			evlist__init_trace_event_sample_raw(session->evlist, &session->header.env);

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
		assert(host_env != NULL);
		session->machines.host.env = host_env;
	}
	if (session->evlist)
		evlist__set_session(session->evlist, session);

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
	debuginfo_cache__delete();
	perf_session__destroy_kernel_maps(session);
	perf_decomp__release_events(session->decomp_data.decomp);
	perf_env__exit(&session->header.env);
	machines__exit(&session->machines);
	if (session->data) {
		if (perf_data__is_read(session->data))
			evlist__put(session->evlist);
		perf_data__close(session->data);
	}
#ifdef HAVE_LIBTRACEEVENT
	trace_event__cleanup(&session->tevent);
#endif
	free(session);
}

static void swap_sample_id_all(union perf_event *event, void *data)
{
	void *end = (void *) event + event->header.size;
	int size;

	if (data >= end)
		return;

	size = end - data;
	if (size % sizeof(u64)) {
		pr_warning("swap_sample_id_all: unaligned sample_id_all remainder (%d), skipping swap\n", size);
		return;
	}
	if (size > 0)
		mem_bswap_64(data, size);
}

static int perf_event__all64_swap(union perf_event *event,
				  bool sample_id_all __maybe_unused)
{
	struct perf_event_header *hdr = &event->header;
	size_t size = event->header.size - sizeof(*hdr);

	/* mem_bswap_64 rounds up to 8-byte chunks — unaligned size overruns the buffer */
	if (size % sizeof(u64))
		return -1;
	mem_bswap_64(hdr + 1, size);
	return 0;
}

static int perf_event__comm_swap(union perf_event *event, bool sample_id_all)
{
	event->comm.pid = bswap_32(event->comm.pid);
	event->comm.tid = bswap_32(event->comm.tid);

	if (sample_id_all) {
		void *data = &event->comm.comm;
		void *end = (void *)event + event->header.size;
		size_t len = strnlen(data, end - data);

		/*
		 * No NUL within the event boundary — can't locate where
		 * sample_id_all starts.  Reject so the event is skipped
		 * rather than swapping garbage.
		 */
		if (len == (size_t)(end - data))
			return -1;
		data += PERF_ALIGN(len + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
}

static int perf_event__mmap_swap(union perf_event *event,
				 bool sample_id_all)
{
	event->mmap.pid	  = bswap_32(event->mmap.pid);
	event->mmap.tid	  = bswap_32(event->mmap.tid);
	event->mmap.start = bswap_64(event->mmap.start);
	event->mmap.len	  = bswap_64(event->mmap.len);
	event->mmap.pgoff = bswap_64(event->mmap.pgoff);

	if (sample_id_all) {
		void *data = &event->mmap.filename;
		void *end = (void *)event + event->header.size;
		size_t len = strnlen(data, end - data);

		/* See comment in perf_event__comm_swap() */
		if (len == (size_t)(end - data))
			return -1;
		data += PERF_ALIGN(len + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
}

static int perf_event__mmap2_swap(union perf_event *event,
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
		void *end = (void *)event + event->header.size;
		size_t len = strnlen(data, end - data);

		/* See comment in perf_event__comm_swap() */
		if (len == (size_t)(end - data))
			return -1;
		data += PERF_ALIGN(len + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
}

static int perf_event__task_swap(union perf_event *event, bool sample_id_all)
{
	event->fork.pid	 = bswap_32(event->fork.pid);
	event->fork.tid	 = bswap_32(event->fork.tid);
	event->fork.ppid = bswap_32(event->fork.ppid);
	event->fork.ptid = bswap_32(event->fork.ptid);
	event->fork.time = bswap_64(event->fork.time);

	if (sample_id_all)
		swap_sample_id_all(event, &event->fork + 1);
	return 0;
}

static int perf_event__read_swap(union perf_event *event,
				 bool sample_id_all __maybe_unused)
{
	size_t tail;

	event->read.pid		 = bswap_32(event->read.pid);
	event->read.tid		 = bswap_32(event->read.tid);
	/*
	 * Everything after pid/tid is u64: the read values (variable
	 * set determined by attr.read_format, which we don't have
	 * here) optionally followed by sample_id_all fields.
	 * Since all are u64, swap the entire remaining tail at once.
	 */
	tail = event->header.size - offsetof(struct perf_record_read, value);
	/* mem_bswap_64 rounds up to 8-byte chunks — unaligned tail overruns the buffer */
	if (tail % sizeof(u64))
		return -1;
	mem_bswap_64(&event->read.value, tail);
	return 0;
}

static int perf_event__aux_swap(union perf_event *event, bool sample_id_all)
{
	event->aux.aux_offset = bswap_64(event->aux.aux_offset);
	event->aux.aux_size   = bswap_64(event->aux.aux_size);
	event->aux.flags      = bswap_64(event->aux.flags);

	if (sample_id_all)
		swap_sample_id_all(event, &event->aux + 1);
	return 0;
}

static int perf_event__itrace_start_swap(union perf_event *event,
					 bool sample_id_all)
{
	event->itrace_start.pid	 = bswap_32(event->itrace_start.pid);
	event->itrace_start.tid	 = bswap_32(event->itrace_start.tid);

	if (sample_id_all)
		swap_sample_id_all(event, &event->itrace_start + 1);
	return 0;
}

static int perf_event__switch_swap(union perf_event *event, bool sample_id_all)
{
	if (event->header.type == PERF_RECORD_SWITCH_CPU_WIDE) {
		event->context_switch.next_prev_pid =
				bswap_32(event->context_switch.next_prev_pid);
		event->context_switch.next_prev_tid =
				bswap_32(event->context_switch.next_prev_tid);
	}

	if (sample_id_all) {
		/*
		 * PERF_RECORD_SWITCH has no fields beyond the header;
		 * SWITCH_CPU_WIDE adds pid/tid.  Use the right offset
		 * so sample_id starts at the correct position.
		 */
		if (event->header.type == PERF_RECORD_SWITCH)
			swap_sample_id_all(event, (void *)event + sizeof(event->header));
		else
			swap_sample_id_all(event, &event->context_switch + 1);
	}
	return 0;
}

static int perf_event__text_poke_swap(union perf_event *event, bool sample_id_all)
{
	event->text_poke.addr    = bswap_64(event->text_poke.addr);
	event->text_poke.old_len = bswap_16(event->text_poke.old_len);
	event->text_poke.new_len = bswap_16(event->text_poke.new_len);

	if (sample_id_all) {
		void *data = &event->text_poke.old_len;
		void *end = (void *)event + event->header.size;
		size_t len = sizeof(event->text_poke.old_len) +
			     sizeof(event->text_poke.new_len) +
			     event->text_poke.old_len +
			     event->text_poke.new_len;

		/* old_len + new_len exceeds event — can't find sample_id_all */
		if (data + len > end)
			return -1;
		data += PERF_ALIGN(len, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
}

static int perf_event__throttle_swap(union perf_event *event,
				     bool sample_id_all)
{
	event->throttle.time	  = bswap_64(event->throttle.time);
	event->throttle.id	  = bswap_64(event->throttle.id);
	event->throttle.stream_id = bswap_64(event->throttle.stream_id);

	if (sample_id_all)
		swap_sample_id_all(event, &event->throttle + 1);
	return 0;
}

static int perf_event__namespaces_swap(union perf_event *event,
				       bool sample_id_all)
{
	u64 i, nr, max_nr;

	event->namespaces.pid		= bswap_32(event->namespaces.pid);
	event->namespaces.tid		= bswap_32(event->namespaces.tid);
	event->namespaces.nr_namespaces	= bswap_64(event->namespaces.nr_namespaces);

	nr = event->namespaces.nr_namespaces;
	/*
	 * Cannot underflow: perf_event__min_size[] guarantees header.size >= sizeof.
	 * When sample_id_all is present max_nr slightly overestimates the
	 * array space because header.size includes the trailing sample_id.
	 * Harmless: both the per-element bswap_64 loop and swap_sample_id_all()
	 * perform the same u64 byte swap, so the result is correct regardless
	 * of where the boundary between array and sample_id falls.
	 */
	max_nr = (event->header.size - sizeof(event->namespaces)) /
		 sizeof(event->namespaces.link_info[0]);
	/*
	 * Safe to clamp: each namespace entry is indexed by type;
	 * missing entries just won't be resolved.
	 */
	if (nr > max_nr) {
		pr_warning("WARNING: PERF_RECORD_NAMESPACES: nr_namespaces %" PRIu64 " exceeds payload (max %" PRIu64 "), clamping\n",
			   nr, max_nr);
		nr = max_nr;
		event->namespaces.nr_namespaces = nr;
	}

	for (i = 0; i < nr; i++) {
		struct perf_ns_link_info *ns = &event->namespaces.link_info[i];

		ns->dev = bswap_64(ns->dev);
		ns->ino = bswap_64(ns->ino);
	}

	if (sample_id_all)
		swap_sample_id_all(event, &event->namespaces.link_info[i]);
	return 0;
}

static int perf_event__cgroup_swap(union perf_event *event, bool sample_id_all)
{
	event->cgroup.id = bswap_64(event->cgroup.id);

	if (sample_id_all) {
		void *data = &event->cgroup.path;
		void *end = (void *)event + event->header.size;
		size_t len = strnlen(data, end - data);

		/* See comment in perf_event__comm_swap() */
		if (len == (size_t)(end - data))
			return -1;
		data += PERF_ALIGN(len + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
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

	/*
	 * ABI0: size == 0 means the producer didn't set it.
	 * Assume PERF_ATTR_SIZE_VER0 so bswap_safe() below
	 * correctly swaps the VER0 fields instead of skipping
	 * everything.  Same convention as read_attr().
	 */
	if (!attr->size)
		attr->size = PERF_ATTR_SIZE_VER0;

/* Verify the full field extent fits, not just its start offset */
#define bswap_safe(f, n)					\
	(attr->size >= (offsetof(struct perf_event_attr, f) +	\
			sizeof(attr->f) * ((n) + 1)))
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

static int perf_event__hdr_attr_swap(union perf_event *event,
				     bool sample_id_all __maybe_unused)
{
	u32 attr_size, payload_size;
	size_t size;

	/*
	 * Validate attr.size (still foreign-endian) before calling
	 * perf_event__attr_swap(), which uses it via bswap_safe()
	 * to decide which fields to swap.  A crafted attr.size
	 * larger than the event payload would swap past the event
	 * boundary and corrupt adjacent memory.
	 *
	 * header.size alignment is already validated by
	 * perf_session__process_event().  The min_size table
	 * guarantees header.size >= sizeof(header) +
	 * PERF_ATTR_SIZE_VER0, so attr.size is safe to access.
	 */
	attr_size = bswap_32(event->attr.attr.size);
	/*
	 * ABI0: size field not set.  This only happens in pipe/inject
	 * mode where HEADER_ATTR events carry their own attr.  For
	 * regular perf.data files, read_attr() uses f_header.attr_size
	 * from the file header instead.  Assume PERF_ATTR_SIZE_VER0.
	 */
	if (!attr_size)
		attr_size = PERF_ATTR_SIZE_VER0;
	payload_size = event->header.size - sizeof(event->header);

	if (attr_size < PERF_ATTR_SIZE_VER0 || attr_size % sizeof(u64) ||
	    attr_size > payload_size) {
		pr_err("PERF_RECORD_HEADER_ATTR: invalid attr.size %u (min: %d, max: %u, 8-byte aligned)\n",
		       attr_size, PERF_ATTR_SIZE_VER0, payload_size);
		return -1;
	}

	perf_event__attr_swap(&event->attr.attr);

	size = event->header.size;
	size -= perf_record_header_attr_id(event) - (void *)event;
	mem_bswap_64(perf_record_header_attr_id(event), size);
	return 0;
}

static int perf_event__build_id_swap(union perf_event *event,
				     bool sample_id_all)
{
	event->build_id.pid = bswap_32(event->build_id.pid);

	if (sample_id_all) {
		void *data = &event->build_id.filename;
		void *end = (void *)event + event->header.size;
		size_t len = strnlen(data, end - data);

		/* See comment in perf_event__comm_swap() */
		if (len == (size_t)(end - data))
			return -1;
		data += PERF_ALIGN(len + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
}

static int perf_event__event_update_swap(union perf_event *event,
					 bool sample_id_all __maybe_unused)
{
	struct perf_record_event_update *ev = &event->event_update;

	ev->type = bswap_64(ev->type);
	ev->id   = bswap_64(ev->id);

	/*
	 * Swap variant-specific fields so the processing path
	 * sees native byte order.
	 */
	if (ev->type == PERF_EVENT_UPDATE__SCALE) {
		if (event->header.size < offsetof(struct perf_record_event_update, scale) +
					 sizeof(ev->scale))
			return -1;
		mem_bswap_64(&ev->scale.scale, sizeof(ev->scale.scale));
	} else if (ev->type == PERF_EVENT_UPDATE__CPUS) {
		u32 cpus_payload;
		struct perf_record_cpu_map_data *data = &ev->cpus.cpus;

		/* CPUS fields start at the same offset as scale (union) */
		if (event->header.size < offsetof(struct perf_record_event_update, cpus) +
					 sizeof(__u16) + sizeof(struct perf_record_range_cpu_map))
			return -1;
		cpus_payload = event->header.size - offsetof(struct perf_record_event_update, cpus);
		data->type = bswap_16(data->type);
		/*
		 * Full swap including array elements — same logic as
		 * perf_event__cpu_map_swap() but scoped to the
		 * embedded cpu_map_data within EVENT_UPDATE.
		 */
		switch (data->type) {
		case PERF_CPU_MAP__CPUS: {
			u16 nr, max_nr;

			data->cpus_data.nr = bswap_16(data->cpus_data.nr);
			nr = data->cpus_data.nr;
			max_nr = (cpus_payload - offsetof(struct perf_record_cpu_map_data,
							  cpus_data.cpu)) /
				 sizeof(data->cpus_data.cpu[0]);
			if (nr > max_nr) {
				nr = max_nr;
				data->cpus_data.nr = nr;
			}
			for (unsigned int i = 0; i < nr; i++)
				data->cpus_data.cpu[i] = bswap_16(data->cpus_data.cpu[i]);
			break;
		}
		case PERF_CPU_MAP__MASK:
			data->mask32_data.long_size = bswap_16(data->mask32_data.long_size);
			switch (data->mask32_data.long_size) {
			case 4: {
				u16 nr, max_nr;

				data->mask32_data.nr = bswap_16(data->mask32_data.nr);
				nr = data->mask32_data.nr;
				max_nr = (cpus_payload - offsetof(struct perf_record_cpu_map_data,
								  mask32_data.mask)) /
					 sizeof(data->mask32_data.mask[0]);
				if (nr > max_nr) {
					nr = max_nr;
					data->mask32_data.nr = nr;
				}
				for (unsigned int i = 0; i < nr; i++)
					data->mask32_data.mask[i] = bswap_32(data->mask32_data.mask[i]);
				break;
			}
			case 8: {
				u16 nr, max_nr;

				data->mask64_data.nr = bswap_16(data->mask64_data.nr);
				nr = data->mask64_data.nr;
				if (cpus_payload < offsetof(struct perf_record_cpu_map_data, mask64_data.mask)) {
					data->mask64_data.nr = 0;
					break;
				}
				max_nr = (cpus_payload - offsetof(struct perf_record_cpu_map_data,
								  mask64_data.mask)) /
					 sizeof(data->mask64_data.mask[0]);
				if (nr > max_nr) {
					nr = max_nr;
					data->mask64_data.nr = nr;
				}
				for (unsigned int i = 0; i < nr; i++)
					data->mask64_data.mask[i] = bswap_64(data->mask64_data.mask[i]);
				break;
			}
			default:
				break;
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
	return 0;
}

static int perf_event__event_type_swap(union perf_event *event,
				       bool sample_id_all __maybe_unused)
{
	event->event_type.event_type.event_id =
		bswap_64(event->event_type.event_type.event_id);
	return 0;
}

static int perf_event__tracing_data_swap(union perf_event *event,
					 bool sample_id_all __maybe_unused)
{
	event->tracing_data.size = bswap_32(event->tracing_data.size);
	return 0;
}

static int perf_event__auxtrace_info_swap(union perf_event *event,
					  bool sample_id_all __maybe_unused)
{
	size_t size;

	event->auxtrace_info.type = bswap_32(event->auxtrace_info.type);

	size = event->header.size;
	size -= (void *)&event->auxtrace_info.priv - (void *)event;
	mem_bswap_64(event->auxtrace_info.priv, size);
	return 0;
}

static int perf_event__auxtrace_swap(union perf_event *event,
				     bool sample_id_all __maybe_unused)
{
	event->auxtrace.size      = bswap_64(event->auxtrace.size);
	event->auxtrace.offset    = bswap_64(event->auxtrace.offset);
	event->auxtrace.reference = bswap_64(event->auxtrace.reference);
	event->auxtrace.idx       = bswap_32(event->auxtrace.idx);
	event->auxtrace.tid       = bswap_32(event->auxtrace.tid);
	event->auxtrace.cpu       = bswap_32(event->auxtrace.cpu);
	return 0;
}

static int perf_event__auxtrace_error_swap(union perf_event *event,
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
		/*
		 * fmt >= 2 adds machine_pid and vcpu after msg[64].
		 * Older files may have fmt >= 2 but an event size
		 * that doesn't include these fields — downgrade to
		 * avoid swapping out of bounds.
		 */
		if (event->header.size < offsetof(typeof(event->auxtrace_error), vcpu) +
					 sizeof(event->auxtrace_error.vcpu)) {
			pr_warning("WARNING: PERF_RECORD_AUXTRACE_ERROR: fmt %u but event too small for machine_pid/vcpu (%u bytes), downgrading fmt\n",
				   event->auxtrace_error.fmt,
				   event->header.size);
			event->auxtrace_error.fmt = 1;
		} else {
			event->auxtrace_error.machine_pid = bswap_32(event->auxtrace_error.machine_pid);
			event->auxtrace_error.vcpu = bswap_32(event->auxtrace_error.vcpu);
		}
	}
	return 0;
}

static int perf_event__thread_map_swap(union perf_event *event,
				       bool sample_id_all __maybe_unused)
{
	unsigned int i;
	u64 nr;

	event->thread_map.nr = bswap_64(event->thread_map.nr);

	/*
	 * Reject rather than clamp: unlike namespaces (indexed by type)
	 * or stat_config (self-describing tags), a truncated thread map
	 * is structurally broken — downstream would get a wrong map.
	 */
	/* Cannot underflow: perf_event__min_size[] guarantees header.size >= sizeof */
	nr = event->thread_map.nr;
	if (nr > (event->header.size - sizeof(event->thread_map)) /
		  sizeof(event->thread_map.entries[0]))
		return -1;

	for (i = 0; i < nr; i++)
		event->thread_map.entries[i].pid = bswap_64(event->thread_map.entries[i].pid);
	return 0;
}

static int perf_event__cpu_map_swap(union perf_event *event,
				    bool sample_id_all __maybe_unused)
{
	struct perf_record_cpu_map_data *data = &event->cpu_map.data;
	u32 payload = event->header.size - sizeof(event->header);

	data->type = bswap_16(data->type);

	/*
	 * Safe to clamp: a shorter CPU map just means some CPUs
	 * are absent; tools process the CPUs that are present.
	 */
	switch (data->type) {
	case PERF_CPU_MAP__CPUS: {
		u16 nr, max_nr;

		data->cpus_data.nr = bswap_16(data->cpus_data.nr);
		nr = data->cpus_data.nr;
		max_nr = (payload - offsetof(struct perf_record_cpu_map_data,
					     cpus_data.cpu)) /
			 sizeof(data->cpus_data.cpu[0]);
		if (nr > max_nr) {
			pr_warning("WARNING: PERF_RECORD_CPU_MAP: nr %u exceeds payload (max %u), clamping\n",
				   nr, max_nr);
			nr = max_nr;
			data->cpus_data.nr = nr;
		}
		for (unsigned int i = 0; i < nr; i++)
			data->cpus_data.cpu[i] = bswap_16(data->cpus_data.cpu[i]);
		break;
	}
	case PERF_CPU_MAP__MASK:
		data->mask32_data.long_size = bswap_16(data->mask32_data.long_size);

		switch (data->mask32_data.long_size) {
		case 4: {
			u16 nr, max_nr;

			data->mask32_data.nr = bswap_16(data->mask32_data.nr);
			nr = data->mask32_data.nr;
			max_nr = (payload - offsetof(struct perf_record_cpu_map_data,
						     mask32_data.mask)) /
				 sizeof(data->mask32_data.mask[0]);
			if (nr > max_nr) {
				pr_warning("WARNING: PERF_RECORD_CPU_MAP mask32: nr %u exceeds payload (max %u), clamping\n",
					   nr, max_nr);
				nr = max_nr;
				data->mask32_data.nr = nr;
			}
			for (unsigned int i = 0; i < nr; i++)
				data->mask32_data.mask[i] = bswap_32(data->mask32_data.mask[i]);
			break;
		}
		case 8: {
			u16 nr, max_nr;

			data->mask64_data.nr = bswap_16(data->mask64_data.nr);
			nr = data->mask64_data.nr;
			if (payload < offsetof(struct perf_record_cpu_map_data, mask64_data.mask)) {
				data->mask64_data.nr = 0;
				break;
			}
			max_nr = (payload - offsetof(struct perf_record_cpu_map_data,
						     mask64_data.mask)) /
				 sizeof(data->mask64_data.mask[0]);
			if (nr > max_nr) {
				pr_warning("WARNING: PERF_RECORD_CPU_MAP mask64: nr %u exceeds payload (max %u), clamping\n",
					   nr, max_nr);
				nr = max_nr;
				data->mask64_data.nr = nr;
			}
			for (unsigned int i = 0; i < nr; i++)
				data->mask64_data.mask[i] = bswap_64(data->mask64_data.mask[i]);
			break;
		}
		default:
			pr_err("cpu_map swap: unsupported long size %u\n",
			       data->mask32_data.long_size);
		}
		break;
	case PERF_CPU_MAP__RANGE_CPUS:
		data->range_cpu_data.start_cpu = bswap_16(data->range_cpu_data.start_cpu);
		data->range_cpu_data.end_cpu = bswap_16(data->range_cpu_data.end_cpu);
		break;
	default:
		break;
	}
	return 0;
}

static int perf_event__stat_config_swap(union perf_event *event,
					bool sample_id_all __maybe_unused)
{
	u64 nr, max_nr, size;

	nr = bswap_64(event->stat_config.nr);
	/* Cannot underflow: perf_event__min_size[] guarantees header.size >= sizeof */
	max_nr = (event->header.size - sizeof(event->stat_config)) /
		 sizeof(event->stat_config.data[0]);
	/*
	 * Safe to clamp: each config entry is self-describing
	 * via its tag; missing entries keep their defaults.
	 */
	if (nr > max_nr) {
		pr_warning("WARNING: PERF_RECORD_STAT_CONFIG: nr %" PRIu64 " exceeds payload (max %" PRIu64 "), clamping\n",
			   nr, max_nr);
		nr = max_nr;
	}
	size = nr * sizeof(event->stat_config.data[0]);
	/* The swap starts at &nr, so add its size to cover the full range */
	size += sizeof(event->stat_config.nr);
	mem_bswap_64(&event->stat_config.nr, size);
	/* Persist the clamped value in native byte order */
	event->stat_config.nr = nr;
	return 0;
}

static int perf_event__stat_swap(union perf_event *event,
				 bool sample_id_all __maybe_unused)
{
	event->stat.id     = bswap_64(event->stat.id);
	event->stat.thread = bswap_32(event->stat.thread);
	event->stat.cpu    = bswap_32(event->stat.cpu);
	event->stat.val    = bswap_64(event->stat.val);
	event->stat.ena    = bswap_64(event->stat.ena);
	event->stat.run    = bswap_64(event->stat.run);
	return 0;
}

static int perf_event__stat_round_swap(union perf_event *event,
				       bool sample_id_all __maybe_unused)
{
	event->stat_round.type = bswap_64(event->stat_round.type);
	event->stat_round.time = bswap_64(event->stat_round.time);
	return 0;
}

static int perf_event__time_conv_swap(union perf_event *event,
				      bool sample_id_all __maybe_unused)
{
	event->time_conv.time_shift = bswap_64(event->time_conv.time_shift);
	event->time_conv.time_mult  = bswap_64(event->time_conv.time_mult);
	event->time_conv.time_zero  = bswap_64(event->time_conv.time_zero);

	if (event_contains(event->time_conv, time_cycles))
		event->time_conv.time_cycles = bswap_64(event->time_conv.time_cycles);
	if (event_contains(event->time_conv, time_mask))
		event->time_conv.time_mask = bswap_64(event->time_conv.time_mask);
	return 0;
}

static int perf_event__compressed2_swap(union perf_event *event,
					bool sample_id_all __maybe_unused)
{
	/* Only data_size needs swapping — compressed payload is a raw byte stream */
	event->pack2.data_size = bswap_64(event->pack2.data_size);
	return 0;
}

static int perf_event__bpf_metadata_swap(union perf_event *event,
					 bool sample_id_all __maybe_unused)
{
	u64 i, nr, max_nr;

	/* Fixed header must fit before accessing nr_entries or prog_name */
	if (event->header.size < sizeof(event->bpf_metadata))
		return -1;

	event->bpf_metadata.nr_entries = bswap_64(event->bpf_metadata.nr_entries);

	/*
	 * Ensure NUL-termination on the cross-endian path where the
	 * mapping is writable (MAP_PRIVATE + PROT_WRITE).  Fixing
	 * the string in place is preferred over rejecting because it
	 * preserves the event for downstream processing — only the
	 * last byte is lost.
	 *
	 * The native-endian path (MAP_SHARED + PROT_READ) cannot
	 * write, so it validates and skips unterminated events in
	 * perf_session__process_user_event() instead.  The two
	 * strategies produce different outcomes for the same
	 * malformed input (fix vs skip), which is inherent in the
	 * writable-vs-read-only mapping model.
	 */
	event->bpf_metadata.prog_name[BPF_PROG_NAME_LEN - 1] = '\0';

	nr = event->bpf_metadata.nr_entries;
	max_nr = (event->header.size - sizeof(event->bpf_metadata)) /
		 sizeof(event->bpf_metadata.entries[0]);
	if (nr > max_nr) {
		/* Persist clamped value so the native path processes entries, not skips */
		nr = max_nr;
		event->bpf_metadata.nr_entries = nr;
	}

	for (i = 0; i < nr; i++) {
		event->bpf_metadata.entries[i].key[BPF_METADATA_KEY_LEN - 1] = '\0';
		event->bpf_metadata.entries[i].value[BPF_METADATA_VALUE_LEN - 1] = '\0';
	}
	return 0;
}
static int
perf_event__schedstat_cpu_swap(union perf_event *event __maybe_unused,
			       bool sample_id_all __maybe_unused)
{
	/* FIXME */
	return 0;
}

static int
perf_event__schedstat_domain_swap(union perf_event *event __maybe_unused,
				  bool sample_id_all __maybe_unused)
{
	/* FIXME */
	return 0;
}

static int perf_event__ksymbol_swap(union perf_event *event,
				    bool sample_id_all)
{
	event->ksymbol.addr = bswap_64(event->ksymbol.addr);
	event->ksymbol.len = bswap_32(event->ksymbol.len);
	event->ksymbol.ksym_type = bswap_16(event->ksymbol.ksym_type);
	event->ksymbol.flags = bswap_16(event->ksymbol.flags);

	if (sample_id_all) {
		void *data = &event->ksymbol.name;
		void *end = (void *)event + event->header.size;
		size_t len = strnlen(data, end - data);

		/* See comment in perf_event__comm_swap() */
		if (len == (size_t)(end - data))
			return -1;
		data += PERF_ALIGN(len + 1, sizeof(u64));
		swap_sample_id_all(event, data);
	}
	return 0;
}

static int perf_event__bpf_event_swap(union perf_event *event,
				      bool sample_id_all)
{
	event->bpf.type  = bswap_16(event->bpf.type);
	event->bpf.flags = bswap_16(event->bpf.flags);
	event->bpf.id    = bswap_32(event->bpf.id);

	if (sample_id_all)
		swap_sample_id_all(event, &event->bpf + 1);
	return 0;
}

static int perf_event__header_feature_swap(union perf_event *event,
					   bool sample_id_all __maybe_unused)
{
	event->feat.feat_id = bswap_64(event->feat.feat_id);
	return 0;
}

typedef int (*perf_event__swap_op)(union perf_event *event,
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
	[PERF_RECORD_KSYMBOL]		  = perf_event__ksymbol_swap,
	[PERF_RECORD_BPF_EVENT]		  = perf_event__bpf_event_swap,
	[PERF_RECORD_TEXT_POKE]		  = perf_event__text_poke_swap,
	[PERF_RECORD_AUX_OUTPUT_HW_ID]	  = perf_event__all64_swap,
	[PERF_RECORD_CALLCHAIN_DEFERRED]  = perf_event__all64_swap,
	[PERF_RECORD_HEADER_ATTR]	  = perf_event__hdr_attr_swap,
	[PERF_RECORD_HEADER_EVENT_TYPE]	  = perf_event__event_type_swap,
	[PERF_RECORD_HEADER_TRACING_DATA] = perf_event__tracing_data_swap,
	[PERF_RECORD_HEADER_BUILD_ID]	  = perf_event__build_id_swap,
	[PERF_RECORD_HEADER_FEATURE]	  = perf_event__header_feature_swap,
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
	[PERF_RECORD_COMPRESSED2]	  = perf_event__compressed2_swap,
	[PERF_RECORD_BPF_METADATA]	  = perf_event__bpf_metadata_swap,
	[PERF_RECORD_SCHEDSTAT_CPU]	  = perf_event__schedstat_cpu_swap,
	[PERF_RECORD_SCHEDSTAT_DOMAIN]	  = perf_event__schedstat_domain_swap,
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
int perf_event__process_finished_round(const struct perf_tool *tool __maybe_unused,
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

static const char *callchain_context_str(u64 ip)
{
	switch (ip) {
	case PERF_CONTEXT_HV:
		return " (PERF_CONTEXT_HV)";
	case PERF_CONTEXT_KERNEL:
		return " (PERF_CONTEXT_KERNEL)";
	case PERF_CONTEXT_USER:
		return " (PERF_CONTEXT_USER)";
	case PERF_CONTEXT_GUEST:
		return " (PERF_CONTEXT_GUEST)";
	case PERF_CONTEXT_GUEST_KERNEL:
		return " (PERF_CONTEXT_GUEST_KERNEL)";
	case PERF_CONTEXT_GUEST_USER:
		return " (PERF_CONTEXT_GUEST_USER)";
	case PERF_CONTEXT_USER_DEFERRED:
		return " (PERF_CONTEXT_USER_DEFERRED)";
	default:
		return "";
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
		printf("..... %2d: %016" PRIx64 "%s\n",
		       i, callchain->ips[i],
		       callchain_context_str(callchain->ips[i]));

	if (sample->deferred_callchain)
		printf("...... (deferred)\n");
}

static void branch_stack__printf(struct perf_sample *sample,
				 struct evsel *evsel)
{
	struct branch_entry *entries = perf_sample__branch_entries(sample);
	bool callstack = evsel__has_branch_callstack(evsel);
	u64 *branch_stack_cntr = sample->branch_stack_cntr;
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
		unsigned int br_cntr_width, br_cntr_nr;

		perf_env__find_br_cntr_info(evsel__env(evsel), &br_cntr_nr, &br_cntr_width);
		printf("... branch stack counters: nr:%" PRIu64 " (counter width: %u max counter nr:%u)\n",
			sample->branch_stack->nr, br_cntr_width, br_cntr_nr);
		for (i = 0; i < sample->branch_stack->nr; i++)
			printf("..... %2"PRIu64": %016" PRIx64 "\n", i, branch_stack_cntr[i]);
	}
}

static void regs_dump__printf(u64 mask, u64 *regs, uint16_t e_machine, uint32_t e_flags)
{
	unsigned rid, i = 0;

	for_each_set_bit(rid, (unsigned long *) &mask, sizeof(mask) * 8) {
		u64 val = regs[i++];

		printf(".... %-5s 0x%016" PRIx64 "\n",
		       perf_reg_name(rid, e_machine, e_flags), val);
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

static void regs__printf(const char *type, struct regs_dump *regs,
			 uint16_t e_machine, uint32_t e_flags)
{
	u64 mask = regs->mask;

	printf("... %s regs: mask 0x%" PRIx64 " ABI %s\n",
	       type,
	       mask,
	       regs_dump_abi(regs));

	regs_dump__printf(mask, regs->regs, e_machine, e_flags);
}

static void regs_user__printf(struct perf_sample *sample, uint16_t e_machine, uint32_t e_flags)
{
	struct regs_dump *user_regs;

	if (!sample->user_regs)
		return;

	user_regs = perf_sample__user_regs(sample);

	if (user_regs->regs)
		regs__printf("user", user_regs, e_machine, e_flags);
}

static void regs_intr__printf(struct perf_sample *sample, uint16_t e_machine, uint32_t e_flags)
{
	struct regs_dump *intr_regs;

	if (!sample->intr_regs)
		return;

	intr_regs = perf_sample__intr_regs(sample);

	if (intr_regs->regs)
		regs__printf("intr", intr_regs, e_machine, e_flags);
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
	if (event->header.type == PERF_RECORD_SAMPLE && evlist__trace_event_sample_raw(evlist))
		evlist__trace_event_sample_raw(evlist)(evlist, event, sample);

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

static void dump_sample(struct machine *machine, union perf_event *event,
			struct perf_sample *sample)
{
	struct evsel *evsel = sample->evsel;
	u64 sample_type;
	char str[PAGE_SIZE_NAME_LEN];
	uint16_t e_machine = EM_NONE;
	uint32_t e_flags = 0;

	if (!dump_trace)
		return;

	sample_type = evsel->core.attr.sample_type;

	if (sample_type & (PERF_SAMPLE_REGS_USER | PERF_SAMPLE_REGS_INTR)) {
		struct thread *thread = machine__find_thread(machine, sample->pid, sample->pid);

		e_machine = thread__e_machine(thread, machine, &e_flags);
	}

	printf("(IP, 0x%x): %d/%d: %#" PRIx64 " period: %" PRIu64 " addr: %#" PRIx64 "\n",
	       event->header.misc, sample->pid, sample->tid, sample->ip,
	       sample->period, sample->addr);

	if (evsel__has_callchain(evsel))
		callchain__printf(evsel, sample);

	if (evsel__has_br_stack(evsel))
		branch_stack__printf(sample, evsel);

	if (sample_type & PERF_SAMPLE_REGS_USER)
		regs_user__printf(sample, e_machine, e_flags);

	if (sample_type & PERF_SAMPLE_REGS_INTR)
		regs_intr__printf(sample, e_machine, e_flags);

	if (sample_type & PERF_SAMPLE_STACK_USER)
		stack_user__printf(&sample->user_stack);

	if (sample_type & PERF_SAMPLE_WEIGHT_TYPE) {
		printf("... weight: %" PRIu64 "", sample->weight);
			if (sample_type & PERF_SAMPLE_WEIGHT_STRUCT) {
				printf(",0x%"PRIx16"", sample->ins_lat);
				printf(",0x%"PRIx16"", sample->weight3);
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

static void dump_deferred_callchain(union perf_event *event, struct perf_sample *sample)
{
	struct evsel *evsel = sample->evsel;

	if (!dump_trace)
		return;

	printf("(IP, 0x%x): %d/%d: %#" PRIx64 "\n",
	       event->header.misc, sample->pid, sample->tid, sample->deferred_cookie);

	if (evsel__has_callchain(evsel))
		callchain__printf(evsel, sample);
}

static void dump_read(struct evsel *evsel, union perf_event *event)
{
	u64 read_format;
	__u64 *array;
	void *end;

	if (!dump_trace)
		return;

	printf(": %d %d %s %" PRI_lu64 "\n", event->read.pid, event->read.tid,
	       evsel__name(evsel), event->read.value);

	if (!evsel)
		return;

	read_format = evsel->core.attr.read_format;
	/*
	 * The kernel packs only the enabled read_format fields
	 * after value, with no gaps.  Walk the packed array
	 * instead of using fixed struct offsets.
	 */
	array = &event->read.value + 1;
	end = (void *)event + event->header.size;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
		if ((void *)(array + 1) > end)
			return;
		printf("... time enabled : %" PRI_lu64 "\n", *array++);
	}

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
		if ((void *)(array + 1) > end)
			return;
		printf("... time running : %" PRI_lu64 "\n", *array++);
	}

	if (read_format & PERF_FORMAT_ID) {
		if ((void *)(array + 1) > end)
			return;
		printf("... id           : %" PRI_lu64 "\n", *array++);
	}

	if (read_format & PERF_FORMAT_LOST) {
		if ((void *)(array + 1) > end)
			return;
		printf("... lost         : %" PRI_lu64 "\n", *array++);
	}
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
				const struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct sample_read_value *v,
				struct machine *machine,
				bool per_thread)
{
	struct perf_sample_id *sid = evlist__id2sid(evlist, v->id);
	struct evsel *saved_evsel = sample->evsel;
	u64 *storage = NULL;
	int ret;

	if (sid) {
		storage = perf_sample_id__get_period_storage(sid, sample->tid, per_thread);
	}

	if (storage) {
		sample->id     = v->id;
		sample->period = v->value - *storage;
		*storage       = v->value;
	}

	if (!storage || sid->evsel == NULL) {
		++evlist__stats(evlist)->nr_unknown_id;
		return 0;
	}

	/*
	 * There's no reason to deliver sample
	 * for zero period, bail out.
	 */
	if (!sample->period)
		return 0;

	sample->evsel = container_of(sid->evsel, struct evsel, core);
	ret = tool->sample(tool, event, sample, machine);
	sample->evsel = saved_evsel;
	return ret;
}

static int deliver_sample_group(struct evlist *evlist,
				const struct perf_tool *tool,
				union  perf_event *event,
				struct perf_sample *sample,
				struct machine *machine,
				u64 read_format,
				bool per_thread)
{
	int ret = -EINVAL;
	struct sample_read_value *v = sample->read.group.values;

	if (tool->dont_split_sample_group)
		return deliver_sample_value(evlist, tool, event, sample, v, machine,
					    per_thread);

	sample_read_group__for_each(v, sample->read.group.nr, read_format) {
		ret = deliver_sample_value(evlist, tool, event, sample, v,
					   machine, per_thread);
		if (ret)
			break;
	}

	return ret;
}

static int evlist__deliver_sample(struct evlist *evlist, const struct perf_tool *tool,
				  union  perf_event *event, struct perf_sample *sample,
				  struct machine *machine)
{
	struct evsel *evsel = sample->evsel;
	/* We know evsel != NULL. */
	u64 sample_type = evsel->core.attr.sample_type;
	u64 read_format = evsel->core.attr.read_format;
	bool per_thread = perf_evsel__attr_has_per_thread_sample_period(&evsel->core);

	/* Standard sample delivery. */
	if (!(sample_type & PERF_SAMPLE_READ))
		return tool->sample(tool, event, sample, machine);

	/* For PERF_SAMPLE_READ we have either single or group mode. */
	if (read_format & PERF_FORMAT_GROUP)
		return deliver_sample_group(evlist, tool, event, sample,
					    machine, read_format, per_thread);
	else
		return deliver_sample_value(evlist, tool, event, sample,
					    &sample->read.one, machine,
					    per_thread);
}

/*
 * Samples with deferred callchains should wait for the next matching
 * PERF_RECORD_CALLCHAIN_RECORD entries.  Keep the events in a list and
 * deliver them once it finds the callchains.
 */
struct deferred_event {
	struct list_head list;
	union perf_event *event;
	u64 file_offset;
};

/*
 * This is called when a deferred callchain record comes up.  Find all matching
 * samples, merge the callchains and process them.
 */
static int evlist__deliver_deferred_callchain(struct evlist *evlist,
					      const struct perf_tool *tool,
					      union  perf_event *event,
					      struct perf_sample *sample,
					      struct machine *machine)
{
	struct deferred_event *de, *tmp;
	int ret = 0;

	if (!tool->merge_deferred_callchains) {
		struct evsel *saved_evsel = sample->evsel;

		sample->evsel = evlist__id2evsel(evlist, sample->id);
		if (sample->evsel)
			sample->evsel = evsel__get(sample->evsel);
		ret = tool->callchain_deferred(tool, event, sample, machine);
		evsel__put(sample->evsel);
		sample->evsel = saved_evsel;
		return ret;
	}

	list_for_each_entry_safe(de, tmp, evlist__deferred_samples(evlist), list) {
		struct perf_sample orig_sample;
		struct evsel *new_evsel;

		perf_sample__init(&orig_sample, /*all=*/false);
		ret = evlist__parse_sample(evlist, de->event, &orig_sample);
		if (ret < 0) {
			pr_err("failed to parse original sample\n");
			perf_sample__exit(&orig_sample);
			break;
		}
		orig_sample.file_offset = de->file_offset;

		if (sample->tid != orig_sample.tid) {
			perf_sample__exit(&orig_sample);
			continue;
		}

		if (event->callchain_deferred.cookie == orig_sample.deferred_cookie)
			sample__merge_deferred_callchain(&orig_sample, sample);
		else
			orig_sample.deferred_callchain = false;

		new_evsel = evlist__id2evsel(evlist, orig_sample.id);
		if (new_evsel != orig_sample.evsel) {
			evsel__put(orig_sample.evsel);
			orig_sample.evsel = evsel__get(new_evsel);
		}
		ret = evlist__deliver_sample(evlist, tool, de->event,
					     &orig_sample, machine);

		perf_sample__exit(&orig_sample);
		list_del(&de->list);
		free(de->event);
		free(de);

		if (ret)
			break;
	}
	return ret;
}

/*
 * This is called at the end of the data processing for the session.  Flush the
 * remaining samples as there's no hope for matching deferred callchains.
 */
static int session__flush_deferred_samples(struct perf_session *session,
					   const struct perf_tool *tool)
{
	struct evlist *evlist = session->evlist;
	struct machine *machine = &session->machines.host;
	struct deferred_event *de, *tmp;
	int ret = 0;

	list_for_each_entry_safe(de, tmp, evlist__deferred_samples(evlist), list) {
		struct perf_sample sample;
		struct evsel *new_evsel;

		perf_sample__init(&sample, /*all=*/false);
		ret = evlist__parse_sample(evlist, de->event, &sample);
		if (ret < 0) {
			pr_err("failed to parse original sample\n");
			perf_sample__exit(&sample);
			break;
		}
		sample.file_offset = de->file_offset;

		new_evsel = evlist__id2evsel(evlist, sample.id);
		if (new_evsel != sample.evsel) {
			evsel__put(sample.evsel);
			sample.evsel = evsel__get(new_evsel);
		}
		ret = evlist__deliver_sample(evlist, tool, de->event,
					     &sample, machine);

		perf_sample__exit(&sample);
		list_del(&de->list);
		free(de->event);
		free(de);

		if (ret)
			break;
	}
	return ret;
}

/*
 * Return true if the string field is properly null-terminated
 * within the event boundary.  Native-endian files are mapped
 * read-only (MAP_SHARED + PROT_READ) so we cannot write a
 * null byte in place; skip the event instead.
 */
static bool perf_event__check_nul(const char *str, const void *end,
				  const char *event_name, u64 file_offset)
{
	size_t max_len = (const char *)end - str;

	if (max_len == 0 || strnlen(str, max_len) == max_len) {
		pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_%s: string not null-terminated, skipping event\n",
			   file_offset, event_name);
		return false;
	}

	return true;
}

static int machines__deliver_event(struct machines *machines,
				   struct evlist *evlist,
				   union perf_event *event,
				   struct perf_sample *sample,
				   const struct perf_tool *tool, u64 file_offset,
				   const char *file_path)
{
	struct machine *machine;

	dump_event(evlist, event, file_offset, sample, file_path);

	if (!sample->evsel) {
		sample->evsel = evlist__id2evsel(evlist, sample->id);
		if (sample->evsel)
			sample->evsel = evsel__get(sample->evsel);
	}
	else
		assert(sample->evsel == evlist__id2evsel(evlist, sample->id));
	machine = machines__find_for_cpumode(machines, event, sample);

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		if (sample->evsel == NULL) {
			++evlist__stats(evlist)->nr_unknown_id;
			return 0;
		}
		if (machine == NULL) {
			++evlist__stats(evlist)->nr_unprocessable_samples;
			dump_sample(machine, event, sample);
			return 0;
		}
		dump_sample(machine, event, sample);
		if (sample->deferred_callchain && tool->merge_deferred_callchains) {
			struct deferred_event *de = malloc(sizeof(*de));
			size_t sz = event->header.size;

			if (de == NULL)
				return -ENOMEM;

			de->event = malloc(sz);
			if (de->event == NULL) {
				free(de);
				return -ENOMEM;
			}
			memcpy(de->event, event, sz);
			de->file_offset = sample->file_offset;
			list_add_tail(&de->list, evlist__deferred_samples(evlist));
			return 0;
		}
		return evlist__deliver_sample(evlist, tool, event, sample, machine);
	case PERF_RECORD_MMAP:
		if (!perf_event__check_nul(event->mmap.filename,
					   (void *)event + event->header.size,
					   "MMAP", file_offset))
			return 0;
		return tool->mmap(tool, event, sample, machine);
	case PERF_RECORD_MMAP2:
		if (event->header.misc & PERF_RECORD_MISC_PROC_MAP_PARSE_TIMEOUT)
			++evlist__stats(evlist)->nr_proc_map_timeout;
		if (!perf_event__check_nul(event->mmap2.filename,
					   (void *)event + event->header.size,
					   "MMAP2", file_offset))
			return 0;
		return tool->mmap2(tool, event, sample, machine);
	case PERF_RECORD_COMM:
		if (!perf_event__check_nul(event->comm.comm,
					   (void *)event + event->header.size,
					   "COMM", file_offset))
			return 0;
		return tool->comm(tool, event, sample, machine);
	case PERF_RECORD_NAMESPACES: {
		/*
		 * Cannot underflow: perf_event__min_size[] guarantees header.size >= sizeof.
		 * Includes trailing sample_id space when present, but prevents OOB.
		 */
		u64 max_nr = (event->header.size - sizeof(event->namespaces)) /
			     sizeof(event->namespaces.link_info[0]);

		/*
		 * Native-endian events are mmap'd read-only, so we
		 * cannot clamp nr in place.  Skip the event instead.
		 * The swap handler already clamps on the writable
		 * cross-endian path.
		 */
		if (event->namespaces.nr_namespaces > max_nr) {
			pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_NAMESPACES: nr_namespaces %" PRIu64 " exceeds payload (max %" PRIu64 "), skipping\n",
				   file_offset, (u64)event->namespaces.nr_namespaces, max_nr);
			return 0;
		}
		return tool->namespaces(tool, event, sample, machine);
	}
	case PERF_RECORD_CGROUP:
		if (!perf_event__check_nul(event->cgroup.path,
					   (void *)event + event->header.size,
					   "CGROUP", file_offset))
			return 0;
		return tool->cgroup(tool, event, sample, machine);
	case PERF_RECORD_FORK:
		return tool->fork(tool, event, sample, machine);
	case PERF_RECORD_EXIT:
		return tool->exit(tool, event, sample, machine);
	case PERF_RECORD_LOST:
		if (tool->lost == perf_event__process_lost)
			evlist__stats(evlist)->total_lost += event->lost.lost;
		return tool->lost(tool, event, sample, machine);
	case PERF_RECORD_LOST_SAMPLES:
		if (event->header.misc & PERF_RECORD_MISC_LOST_SAMPLES_BPF)
			evlist__stats(evlist)->total_dropped_samples += event->lost_samples.lost;
		else if (tool->lost_samples == perf_event__process_lost_samples)
			evlist__stats(evlist)->total_lost_samples += event->lost_samples.lost;
		return tool->lost_samples(tool, event, sample, machine);
	case PERF_RECORD_READ:
		dump_read(sample->evsel, event);
		return tool->read(tool, event, sample, machine);
	case PERF_RECORD_THROTTLE:
		return tool->throttle(tool, event, sample, machine);
	case PERF_RECORD_UNTHROTTLE:
		return tool->unthrottle(tool, event, sample, machine);
	case PERF_RECORD_AUX:
		if (tool->aux == perf_event__process_aux) {
			if (event->aux.flags & PERF_AUX_FLAG_TRUNCATED)
				evlist__stats(evlist)->total_aux_lost += 1;
			if (event->aux.flags & PERF_AUX_FLAG_PARTIAL)
				evlist__stats(evlist)->total_aux_partial += 1;
			if (event->aux.flags & PERF_AUX_FLAG_COLLISION)
				evlist__stats(evlist)->total_aux_collision += 1;
		}
		return tool->aux(tool, event, sample, machine);
	case PERF_RECORD_ITRACE_START:
		return tool->itrace_start(tool, event, sample, machine);
	case PERF_RECORD_SWITCH:
	case PERF_RECORD_SWITCH_CPU_WIDE:
		return tool->context_switch(tool, event, sample, machine);
	case PERF_RECORD_KSYMBOL:
		if (!perf_event__check_nul(event->ksymbol.name,
					   (void *)event + event->header.size,
					   "KSYMBOL", file_offset))
			return 0;
		return tool->ksymbol(tool, event, sample, machine);
	case PERF_RECORD_BPF_EVENT:
		return tool->bpf(tool, event, sample, machine);
	case PERF_RECORD_TEXT_POKE: {
		/* offsetof(bytes), not sizeof — sizeof includes padding past the flexible array */
		size_t text_poke_len = offsetof(struct perf_record_text_poke_event, bytes) +
				       event->text_poke.old_len +
				       event->text_poke.new_len;

		if (event->header.size < text_poke_len) {
			pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_TEXT_POKE: old_len+new_len exceeds event, skipping\n",
				   file_offset);
			return 0;
		}
		return tool->text_poke(tool, event, sample, machine);
	}
	case PERF_RECORD_AUX_OUTPUT_HW_ID:
		return tool->aux_output_hw_id(tool, event, sample, machine);
	case PERF_RECORD_CALLCHAIN_DEFERRED:
		dump_deferred_callchain(event, sample);
		return evlist__deliver_deferred_callchain(evlist, tool, event,
							  sample, machine);
	default:
		++evlist__stats(evlist)->nr_unknown_events;
		return -1;
	}
}

static int perf_session__deliver_event(struct perf_session *session,
				       union perf_event *event,
				       const struct perf_tool *tool,
				       u64 file_offset,
				       const char *file_path)
{
	struct perf_sample sample;
	struct evsel *evsel;
	int ret;

	perf_sample__init(&sample, /*all=*/false);
	evsel = evlist__event2evsel(session->evlist, event);
	if (!evsel) {
		pr_err("ERROR: at offset %#" PRIx64 ": no evsel found for %s (%u) event\n",
		       file_offset, perf_event__name(event->header.type),
		       event->header.type);
		ret = -EFAULT;
		goto out;
	}
	ret = evsel__parse_sample(evsel, event, &sample);
	if (ret) {
		pr_err("ERROR: at offset %#" PRIx64 ": can't parse %s (%u) sample, err = %d\n",
		       file_offset, perf_event__name(event->header.type),
		       event->header.type, ret);
		goto out;
	}
	sample.file_offset = file_offset;
	/*
	 * evsel__parse_sample() doesn't populate machine_pid/vcpu,
	 * which are needed by machines__find_for_cpumode() to
	 * attribute samples to guest VMs.  The SID table maps
	 * sample IDs to the guest that owns the event.
	 */
	if (perf_guest && sample.id) {
		struct perf_sample_id *sid = evlist__id2sid(session->evlist, sample.id);

		if (sid) {
			sample.machine_pid = sid->machine_pid;
			sample.vcpu = sid->vcpu.cpu;
		}
	}

	/*
	 * Validate sample.cpu before any callback can use it as an
	 * array index (kwork cpus_runtime, timechart cpus_cstate_*,
	 * sched cpu_last_switched).
	 *
	 * When PERF_SAMPLE_CPU is absent, evsel__parse_sample() leaves
	 * sample.cpu as (u32)-1 — a sentinel that downstream tools
	 * (script, inject) check to identify events without CPU info.
	 * Only check when sample.cpu was actually populated from event
	 * data: PERF_RECORD_SAMPLE always has it when PERF_SAMPLE_CPU
	 * is set; non-sample events only have it when sample_id_all is
	 * enabled.  Otherwise sample.cpu is the (u32)-1 sentinel from
	 * evsel__parse_sample() and must not be validated or clamped.
	 */
	if ((evsel->core.attr.sample_type & PERF_SAMPLE_CPU) &&
	    (event->header.type == PERF_RECORD_SAMPLE ||
	     evsel->core.attr.sample_id_all)) {
		int nr_cpus_avail = perf_session__env(session)->nr_cpus_avail;

		/*
		 * For perf.data files the MAX_NR_CPUS fallback in
		 * perf_session__read_header() guarantees this is set.
		 * For pipe mode, HEADER_NRCPUS may arrive late or not
		 * at all (pre-2017 perf, third-party tools).  Fall
		 * back to MAX_NR_CPUS so the bounds check still works
		 * against fixed-size downstream arrays.
		 *
		 * Do NOT write back to env: this function runs during
		 * recording (synthesized events) when nr_cpus_avail is
		 * legitimately 0.  Writing MAX_NR_CPUS would cause
		 * write_cpu_topology() to emit 4096 core_id/socket_id
		 * pairs instead of the real CPU count, corrupting the
		 * topology section in the generated perf.data.
		 */
		if (nr_cpus_avail <= 0)
			nr_cpus_avail = MAX_NR_CPUS;
		/*
		 * Cap at MAX_NR_CPUS for the bounds check — downstream
		 * consumers use fixed-size arrays of that size.  Keep
		 * the true nr_cpus_avail in env for header parsing
		 * (e.g. process_cpu_topology) which needs the real count.
		 */
		if (nr_cpus_avail > MAX_NR_CPUS)
			nr_cpus_avail = MAX_NR_CPUS;
		if (sample.cpu >= (u32)nr_cpus_avail &&
		    sample.cpu != (u32)-1) {
			/*
			 * Warn rather than abort: synthesized events
			 * (MMAP, COMM) lack sample_id_all data, so
			 * parse_id_sample reads garbage from the event
			 * payload.  Clamping to 0 protects downstream
			 * array indexing while keeping the session alive.
			 *
			 * Preserve (u32)-1: perf script and perf inject
			 * use it as a sentinel for "CPU not applicable."
			 * Downstream array users (timechart, kwork) have
			 * their own per-callback bounds checks.
			 */
			pr_warning_once("WARNING: at offset %#" PRIx64 ": sample CPU %u >= nr_cpus_avail %u, clamping to 0\n",
					file_offset, sample.cpu, nr_cpus_avail);
			sample.cpu = 0;
		}
	}

	ret = auxtrace__process_event(session, event, &sample, tool);
	if (ret < 0)
		goto out;
	if (ret > 0) {
		ret = 0;
		goto out;
	}

	ret = machines__deliver_event(&session->machines, session->evlist,
				      event, &sample, tool, file_offset, file_path);

	if (dump_trace && sample.aux_sample.size)
		auxtrace__dump_auxtrace_sample(session, &sample);
out:
	perf_sample__exit(&sample);
	return ret;
}

static s64 perf_session__process_user_event(struct perf_session *session,
					    union perf_event *event,
					    u64 file_offset,
					    const char *file_path)
{
	struct ordered_events *oe = &session->ordered_events;
	const struct perf_tool *tool = session->tool;
	const u32 event_size = READ_ONCE(event->header.size);
	struct perf_sample sample;
	int fd = perf_data__fd(session->data);
	s64 err;

	perf_sample__init(&sample, /*all=*/true);
	if ((event->header.type != PERF_RECORD_COMPRESSED &&
	     event->header.type != PERF_RECORD_COMPRESSED2) ||
	    perf_tool__compressed_is_stub(tool))
		dump_event(session->evlist, event, file_offset, &sample, file_path);

	/* These events are processed right away */
	switch (event->header.type) {
	case PERF_RECORD_HEADER_ATTR:
		err = tool->attr(tool, event, &session->evlist);
		if (err == 0) {
			perf_session__set_id_hdr_size(session);
			perf_session__set_comm_exec(session);
		}
		break;
	case PERF_RECORD_EVENT_UPDATE:
		err = tool->event_update(tool, event, &session->evlist);
		break;
	case PERF_RECORD_HEADER_EVENT_TYPE:
		/*
		 * Deprecated, but we need to handle it for sake
		 * of old data files create in pipe mode.
		 */
		err = 0;
		break;
	case PERF_RECORD_HEADER_TRACING_DATA:
		/*
		 * Setup for reading amidst mmap, but only when we
		 * are in 'file' mode. The 'pipe' fd is in proper
		 * place already.
		 */
		if (!perf_data__is_pipe(session->data))
			lseek(fd, file_offset, SEEK_SET);
		err = tool->tracing_data(tool, session, event);
		break;
	case PERF_RECORD_HEADER_BUILD_ID:
		if (!perf_event__check_nul(event->build_id.filename,
					   (void *)event + event_size,
					   "HEADER_BUILD_ID", file_offset)) {
			err = 0;
			break;
		}
		err = tool->build_id(tool, session, event);
		break;
	case PERF_RECORD_FINISHED_ROUND:
		err = tool->finished_round(tool, event, oe);
		break;
	case PERF_RECORD_ID_INDEX:
		err = tool->id_index(tool, session, event);
		break;
	case PERF_RECORD_AUXTRACE_INFO:
		err = tool->auxtrace_info(tool, session, event);
		break;
	case PERF_RECORD_AUXTRACE:
		/*
		 * Setup for reading amidst mmap, but only when we
		 * are in 'file' mode.  The 'pipe' fd is in proper
		 * place already.
		 */
		if (!perf_data__is_pipe(session->data))
			lseek(fd, file_offset + event_size, SEEK_SET);
		err = tool->auxtrace(tool, session, event);
		break;
	case PERF_RECORD_AUXTRACE_ERROR:
		perf_session__auxtrace_error_inc(session, event);
		err = tool->auxtrace_error(tool, session, event);
		break;
	case PERF_RECORD_THREAD_MAP: {
		u64 max_nr;

		if (event_size < sizeof(event->thread_map)) {
			pr_err("ERROR: at offset %#" PRIx64 ": PERF_RECORD_THREAD_MAP: header.size (%u) too small\n",
			       file_offset, event_size);
			err = -EINVAL;
			break;
		}

		max_nr = (event_size - sizeof(event->thread_map)) /
			 sizeof(event->thread_map.entries[0]);
		if (event->thread_map.nr > max_nr) {
			pr_err("ERROR: at offset %#" PRIx64 ": PERF_RECORD_THREAD_MAP: nr %" PRIu64 " exceeds max %" PRIu64 "\n",
			       file_offset, (u64)event->thread_map.nr, max_nr);
			err = -EINVAL;
			break;
		}

		err = tool->thread_map(tool, session, event);
		break;
	}
	case PERF_RECORD_CPU_MAP: {
		struct perf_record_cpu_map_data *data = &event->cpu_map.data;
		u32 payload = event_size - sizeof(event->header);

		/*
		 * Native-endian events are mmap'd read-only, so we
		 * cannot clamp nr fields in place.  Skip the event
		 * if any variant overflows.
		 */
		switch (data->type) {
		case PERF_CPU_MAP__CPUS: {
			u16 max_nr = (payload - offsetof(struct perf_record_cpu_map_data,
							 cpus_data.cpu)) /
				     sizeof(data->cpus_data.cpu[0]);

			if (data->cpus_data.nr > max_nr) {
				pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_CPU_MAP: nr %u exceeds payload (max %u), skipping\n",
					   file_offset, data->cpus_data.nr, max_nr);
				err = 0;
				goto out;
			}
			break;
		}
		case PERF_CPU_MAP__MASK:
			if (data->mask32_data.long_size == 4) {
				u16 max_nr = (payload - offsetof(struct perf_record_cpu_map_data,
								 mask32_data.mask)) /
					     sizeof(data->mask32_data.mask[0]);

				if (data->mask32_data.nr > max_nr) {
					pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_CPU_MAP mask32: nr %u exceeds payload (max %u), skipping\n",
						   file_offset, data->mask32_data.nr, max_nr);
					err = 0;
					goto out;
				}
			} else if (data->mask64_data.long_size == 8) {
				u16 max_nr;

				if (payload < offsetof(struct perf_record_cpu_map_data, mask64_data.mask)) {
					err = 0;
					goto out;
				}
				max_nr = (payload - offsetof(struct perf_record_cpu_map_data,
							     mask64_data.mask)) /
					 sizeof(data->mask64_data.mask[0]);
				if (data->mask64_data.nr > max_nr) {
					pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_CPU_MAP mask64: nr %u exceeds payload (max %u), skipping\n",
						   file_offset, data->mask64_data.nr, max_nr);
					err = 0;
					goto out;
				}
			} else {
				pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_CPU_MAP: unsupported long_size %u, skipping\n",
					   file_offset, data->mask32_data.long_size);
				err = 0;
				goto out;
			}
			break;
		default:
			break;
		}

		err = tool->cpu_map(tool, session, event);
		break;
	}
	case PERF_RECORD_STAT_CONFIG: {
		/* Cannot underflow: perf_event__min_size[] guarantees event_size >= sizeof */
		u64 max_nr = (event_size - sizeof(event->stat_config)) /
			     sizeof(event->stat_config.data[0]);

		/*
		 * Native-endian events are mmap'd read-only, so we
		 * cannot clamp nr in place.  Skip the event instead.
		 */
		if (event->stat_config.nr > max_nr) {
			pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_STAT_CONFIG: nr %" PRIu64 " exceeds payload (max %" PRIu64 "), skipping\n",
				   file_offset, (u64)event->stat_config.nr, max_nr);
			err = 0;
			goto out;
		}

		err = tool->stat_config(tool, session, event);
		break;
	}
	case PERF_RECORD_STAT:
		err = tool->stat(tool, session, event);
		break;
	case PERF_RECORD_STAT_ROUND:
		err = tool->stat_round(tool, session, event);
		break;
	case PERF_RECORD_TIME_CONV:
		/*
		 * Bounded copy: older kernels emit a shorter struct
		 * without time_cycles/time_mask/cap_user_time_*.
		 * Zero the rest so extended fields default to off.
		 */
		memset(&session->time_conv, 0, sizeof(session->time_conv));
		memcpy(&session->time_conv, &event->time_conv,
		       min((size_t)event_size, sizeof(session->time_conv)));
		err = tool->time_conv(tool, session, event);
		break;
	case PERF_RECORD_HEADER_FEATURE:
		err = tool->feature(tool, session, event);
		break;
	case PERF_RECORD_COMPRESSED:
	case PERF_RECORD_COMPRESSED2:
		err = tool->compressed(tool, session, event, file_offset, file_path);
		if (err)
			dump_event(session->evlist, event, file_offset, &sample, file_path);
		break;
	case PERF_RECORD_FINISHED_INIT:
		err = tool->finished_init(tool, session, event);
		break;
	case PERF_RECORD_BPF_METADATA: {
		u64 nr_entries, max_entries;

		if (event_size < sizeof(event->bpf_metadata)) {
			pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_BPF_METADATA: header.size (%u) too small, skipping\n",
				   file_offset, event_size);
			err = 0;
			break;
		}

		/*
		 * Native-endian files are mmap'd read-only — validate
		 * NUL-termination instead of writing.
		 */
		if (strnlen(event->bpf_metadata.prog_name,
			    BPF_PROG_NAME_LEN) == BPF_PROG_NAME_LEN) {
			pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_BPF_METADATA: prog_name not null-terminated, skipping\n",
				   file_offset);
			err = 0;
			break;
		}

		nr_entries = READ_ONCE(event->bpf_metadata.nr_entries);
		max_entries = (event_size - sizeof(event->bpf_metadata)) /
			      sizeof(event->bpf_metadata.entries[0]);
		if (nr_entries > max_entries) {
			pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_BPF_METADATA: nr_entries %" PRIu64 " exceeds max %" PRIu64 ", skipping\n",
				   file_offset, nr_entries, max_entries);
			err = 0;
			break;
		}

		for (u64 i = 0; i < nr_entries; i++) {
			if (strnlen(event->bpf_metadata.entries[i].key,
				    BPF_METADATA_KEY_LEN) == BPF_METADATA_KEY_LEN ||
			    strnlen(event->bpf_metadata.entries[i].value,
				    BPF_METADATA_VALUE_LEN) == BPF_METADATA_VALUE_LEN) {
				pr_warning("WARNING: at offset %#" PRIx64 ": PERF_RECORD_BPF_METADATA: entry %" PRIu64 " key/value not null-terminated, skipping\n",
					   file_offset, i);
				err = 0;
				goto out;
			}
		}

		err = tool->bpf_metadata(tool, session, event);
		break;
	}
	case PERF_RECORD_SCHEDSTAT_CPU:
		err = tool->schedstat_cpu(tool, session, event);
		break;
	case PERF_RECORD_SCHEDSTAT_DOMAIN:
		err = tool->schedstat_domain(tool, session, event);
		break;
	default:
		err = -EINVAL;
		break;
	}
out:
	perf_sample__exit(&sample);
	return err;
}

int perf_session__deliver_synth_event(struct perf_session *session,
				      union perf_event *event,
				      struct perf_sample *sample)
{
	struct evlist *evlist = session->evlist;
	const struct perf_tool *tool = session->tool;

	events_stats__inc(evlist__stats(evlist), event->header.type);

	if (event->header.type >= PERF_RECORD_USER_TYPE_START)
		return perf_session__process_user_event(session, event, 0, NULL);

	return machines__deliver_event(&session->machines, evlist, event, sample, tool, 0, NULL);
}

int perf_session__deliver_synth_attr_event(struct perf_session *session,
					   const struct perf_event_attr *attr,
					   u64 id)
{
	union {
		struct {
			struct perf_record_header_attr attr;
			u64 ids[1];
		} attr_id;
		union perf_event ev;
	} ev = {
		.attr_id.attr.header.type = PERF_RECORD_HEADER_ATTR,
		.attr_id.attr.header.size = sizeof(ev.attr_id),
		.attr_id.ids[0] = id,
	};

	if (attr->size != sizeof(ev.attr_id.attr.attr)) {
		pr_debug("Unexpected perf_event_attr size\n");
		return -EINVAL;
	}
	ev.attr_id.attr.attr = *attr;
	return perf_session__deliver_synth_event(session, &ev.ev, NULL);
}

/* Caller must ensure event->header.type < PERF_RECORD_HEADER_MAX */
static int event_swap(union perf_event *event, bool sample_id_all)
{
	perf_event__swap_op swap = perf_event__swap_ops[event->header.type];

	if (swap)
		return swap(event, sample_id_all);
	return 0;
}

/*
 * Minimum event sizes indexed by type.  Checked before swap and
 * processing so that both cross-endian and native-endian paths
 * are protected from accessing fields past the event boundary.
 * Zero means no minimum beyond the 8-byte header (already
 * enforced by the reader).
 *
 * These values represent the smallest event the kernel has ever
 * emitted for each type, so they do not reject legitimate legacy
 * perf.data files from older kernels.  Variable-length events
 * use offsetof() to the first variable field; the variable
 * content is validated separately (e.g., perf_event__check_nul).
 */
static const u32 perf_event__min_size[PERF_RECORD_HEADER_MAX] = {
	/*
	 * offsetof() + 1 for types with a trailing variable-length
	 * string (filename, comm, path, name, msg): the +1 ensures
	 * room for at least a null terminator.  Full null-termination
	 * within the event boundary is checked separately.
	 *
	 * PERF_RECORD_SAMPLE is omitted: all64_swap is bounded by
	 * header.size, and the internal layout varies by sample_type
	 * so a fixed minimum is not meaningful.
	 */
	[PERF_RECORD_MMAP]		  = offsetof(struct perf_record_mmap, filename) + 1,
	[PERF_RECORD_LOST]		  = sizeof(struct perf_record_lost),
	[PERF_RECORD_COMM]		  = offsetof(struct perf_record_comm, comm) + 1,
	[PERF_RECORD_EXIT]		  = sizeof(struct perf_record_fork),
	[PERF_RECORD_THROTTLE]		  = sizeof(struct perf_record_throttle),
	[PERF_RECORD_UNTHROTTLE]	  = sizeof(struct perf_record_throttle),
	[PERF_RECORD_FORK]		  = sizeof(struct perf_record_fork),
	/*
	 * The kernel dynamically sizes PERF_RECORD_READ based on
	 * attr.read_format — only the enabled fields are emitted,
	 * packed with no gaps.  The minimum valid event has just
	 * pid + tid + one u64 value (no optional fields).
	 */
	[PERF_RECORD_READ]		  = offsetof(struct perf_record_read, time_enabled),
	[PERF_RECORD_MMAP2]		  = offsetof(struct perf_record_mmap2, filename) + 1,
	[PERF_RECORD_LOST_SAMPLES]	  = sizeof(struct perf_record_lost_samples),
	[PERF_RECORD_AUX]		  = sizeof(struct perf_record_aux),
	[PERF_RECORD_ITRACE_START]	  = sizeof(struct perf_record_itrace_start),
	[PERF_RECORD_SWITCH]		  = sizeof(struct perf_event_header),
	[PERF_RECORD_SWITCH_CPU_WIDE]	  = sizeof(struct perf_record_switch),
	[PERF_RECORD_NAMESPACES]	  = sizeof(struct perf_record_namespaces),
	[PERF_RECORD_CGROUP]		  = offsetof(struct perf_record_cgroup, path) + 1,
	[PERF_RECORD_TEXT_POKE]		  = sizeof(struct perf_record_text_poke_event),
	[PERF_RECORD_KSYMBOL]		  = offsetof(struct perf_record_ksymbol, name) + 1,
	[PERF_RECORD_BPF_EVENT]		  = sizeof(struct perf_record_bpf_event),
	[PERF_RECORD_HEADER_ATTR]	  = sizeof(struct perf_event_header) + PERF_ATTR_SIZE_VER0,
	[PERF_RECORD_HEADER_EVENT_TYPE]	  = sizeof(struct perf_record_header_event_type),
	/* Legacy events predate the __u32 pad field, accept 12-byte records */
	[PERF_RECORD_HEADER_TRACING_DATA] = offsetof(struct perf_record_header_tracing_data, pad),
	[PERF_RECORD_AUX_OUTPUT_HW_ID]	  = sizeof(struct perf_record_aux_output_hw_id),
	[PERF_RECORD_AUXTRACE_INFO]	  = sizeof(struct perf_record_auxtrace_info),
	[PERF_RECORD_AUXTRACE]		  = sizeof(struct perf_record_auxtrace),
	[PERF_RECORD_AUXTRACE_ERROR]	  = offsetof(struct perf_record_auxtrace_error, msg) + 1,
	[PERF_RECORD_THREAD_MAP]	  = sizeof(struct perf_record_thread_map),
	/*
	 * sizeof(perf_record_cpu_map) is 20 because the outer struct
	 * isn't packed and GCC adds 2 bytes of trailing padding.
	 * The smallest valid variant (RANGE_CPUS) is only 16 bytes:
	 * header(8) + type(2) + range_cpu_data(6).  Per-variant
	 * bounds are checked in the swap handler via payload.
	 */
	[PERF_RECORD_CPU_MAP]		  = sizeof(struct perf_event_header) +
					    sizeof(__u16) +
					    sizeof(struct perf_record_range_cpu_map),
	[PERF_RECORD_STAT_CONFIG]	  = sizeof(struct perf_record_stat_config),
	[PERF_RECORD_STAT]		  = sizeof(struct perf_record_stat),
	[PERF_RECORD_STAT_ROUND]	  = sizeof(struct perf_record_stat_round),
	/*
	 * EVENT_UPDATE has a union whose largest member (cpus)
	 * inflates sizeof to 40, but SCALE events are only 32
	 * and UNIT/NAME events can be even smaller.  Use the
	 * fixed header fields (header + type + id) as minimum.
	 */
	[PERF_RECORD_EVENT_UPDATE]	  = offsetof(struct perf_record_event_update, scale),
	[PERF_RECORD_TIME_CONV]		  = offsetof(struct perf_record_time_conv, time_cycles),
	[PERF_RECORD_ID_INDEX]		  = sizeof(struct perf_record_id_index),
	[PERF_RECORD_HEADER_BUILD_ID]	  = sizeof(struct perf_record_header_build_id),
	[PERF_RECORD_HEADER_FEATURE]	  = sizeof(struct perf_record_header_feature),
	[PERF_RECORD_COMPRESSED2]	  = sizeof(struct perf_record_compressed2),
	[PERF_RECORD_BPF_METADATA]	  = sizeof(struct perf_record_bpf_metadata),
	[PERF_RECORD_CALLCHAIN_DEFERRED]  = sizeof(struct perf_event_header) + sizeof(__u64),
	/*
	 * SCHEDSTAT events have a version-dependent union after the
	 * fixed header fields; the minimum is the base (pre-union)
	 * portion so old and new versions both pass.
	 */
	[PERF_RECORD_SCHEDSTAT_CPU]	  = offsetof(struct perf_record_schedstat_cpu, v15),
	[PERF_RECORD_SCHEDSTAT_DOMAIN]	  = offsetof(struct perf_record_schedstat_domain, v15),
};

/*
 * Return true if the event is too small for its declared type.
 * Caller must ensure event->header.type < PERF_RECORD_HEADER_MAX.
 * If min is non-NULL, stores the required minimum on failure.
 */
bool perf_event__too_small(const union perf_event *event, u32 *min)
{
	u32 min_sz = perf_event__min_size[event->header.type];

	if (min_sz && event->header.size < min_sz) {
		if (min)
			*min = min_sz;
		return true;
	}

	return false;
}

/*
 * Read and validate the event at @file_offset.
 *
 * Returns:
 *   0  — success: *event_ptr is set and safe to access.
 *  -1  — error; check *event_ptr to decide whether to advance or abort:
 *          *event_ptr set  — event header was read but the event is
 *                            malformed (too small for its type, or byte-swap
 *                            failed).  header.size is still valid, so the
 *                            caller can advance past the event.
 *          *event_ptr NULL — fatal: couldn't read the header at all
 *                            (I/O error, offset out of range, pipe mode).
 *                            Caller must abort.
 */
int perf_session__peek_event(struct perf_session *session, off_t file_offset,
			     void *buf, size_t buf_sz,
			     union perf_event **event_ptr,
			     struct perf_sample *sample)
{
	union perf_event *event;
	size_t hdr_sz, rest;
	u32 min_sz;
	int fd;

	*event_ptr = NULL;

	if (session->one_mmap && !session->header.needs_swap) {
		u64 offset_in_mmap;

		/* Validate offset with integer arithmetic to avoid pointer UB */
		if ((u64)file_offset < session->one_mmap_offset)
			return -1;

		offset_in_mmap = (u64)file_offset - session->one_mmap_offset;

		/* Use subtraction to avoid addition overflow */
		if (offset_in_mmap >= session->one_mmap_size ||
		    session->one_mmap_size - offset_in_mmap < sizeof(struct perf_event_header))
			return -1;

		event = session->one_mmap_addr + offset_in_mmap;

		if (event->header.size < sizeof(struct perf_event_header))
			return -1;

		/* Ensure full event is within the mmap region */
		if (session->one_mmap_size - offset_in_mmap < event->header.size)
			return -1;
	} else {
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
	}

	/* Event data is fully loaded — expose so callers can advance */
	*event_ptr = event;

	/*
	 * Check alignment before type: an unaligned size misaligns the
	 * stream for all subsequent reads regardless of event type.
	 * Three legacy user events predate the 8-byte rule — exempt them.
	 */
	if (event->header.size % sizeof(u64) &&
	    event->header.type != PERF_RECORD_HEADER_TRACING_DATA &&
	    event->header.type != PERF_RECORD_COMPRESSED &&
	    event->header.type != PERF_RECORD_HEADER_FEATURE) {
		pr_warning("WARNING: at offset %#" PRIx64 ": %s (%u) event size %u not aligned to %zu\n",
			   (u64)file_offset, perf_event__name(event->header.type),
			   event->header.type, event->header.size, sizeof(u64));
		return -1;
	}

	if (event->header.type >= PERF_RECORD_HEADER_MAX) {
		pr_warning("WARNING: at offset %#" PRIx64 ": unsupported event type %u, skipping\n",
			   (u64)file_offset, event->header.type);
		return 0;
	}

	if (perf_event__too_small(event, &min_sz)) {
		pr_warning("WARNING: at offset %#" PRIx64 ": %s (%u) event size %u too small (min %u)\n",
			   (u64)file_offset, perf_event__name(event->header.type),
			   event->header.type, event->header.size, min_sz);
		return -1;
	}

	if (session->header.needs_swap &&
	    event_swap(event, evlist__sample_id_all(session->evlist))) {
		/*
		 * The header was already swapped so header.size is
		 * valid — expose the event so callers can advance
		 * past this malformed entry instead of aborting.
		 */
		*event_ptr = event;
		return -1;
	}

	if (sample && event->header.type < PERF_RECORD_USER_TYPE_START &&
	    evlist__parse_sample(session->evlist, event, sample))
		return -1;

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
		event = NULL;
		err = perf_session__peek_event(session, offset, buf,
					       PERF_SAMPLE_MAX_SIZE, &event,
					       NULL);
		if (err) {
			/*
			 * Recoverable error: peek_event returns -1 but
			 * sets event_ptr when the header was read
			 * successfully but the event is malformed (too
			 * small or swap failed).  Skip past it using
			 * header.size — don't invoke the callback since
			 * type-specific fields may be truncated.
			 *
			 * Must abort if: event_ptr is NULL (I/O error),
			 * size is 0 (can't advance), type is AUXTRACE
			 * (payload extends beyond header.size), or size
			 * is unaligned (would misalign all subsequent reads).
			 *
			 * Direct callers (auxtrace, cs-etm) treat any
			 * non-zero return as fatal — only this loop skips.
			 */
			if (event && event->header.size &&
			    event->header.type != PERF_RECORD_AUXTRACE &&
			    event->header.size % sizeof(u64) == 0) {
				offset += event->header.size;
				err = 0;
			} else {
				return err;
			}
			continue;
		}

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
	const struct perf_tool *tool = session->tool;
	u32 min_sz;
	int ret;

	/*
	 * The kernel aligns all event sizes to sizeof(u64) — see
	 * perf_event_comm_event() (ALIGN), perf_event_mmap_event(),
	 * perf_event_cgroup(), perf_event_ksymbol() (IS_ALIGNED loops),
	 * and perf_event_text_poke() (ALIGN) in kernel/events/core.c.
	 *
	 * An unaligned size means the file is corrupted or crafted.
	 * Abort: there is no point continuing to read unaligned records
	 * because the caller advances rd->head by event->header.size,
	 * so every subsequent read would start at a misaligned offset,
	 * producing garbage headers for the rest of the file.
	 *
	 * Exempt three legacy user events that predate the alignment rule:
	 *
	 * TRACING_DATA (66): struct tracing_data_event was 12 bytes before
	 *   b39c915a4f36 ("libperf event: Ensure tracing data is multiple
	 *   of 8 sized") added __u32 pad; old perf.data files still contain
	 *   12-byte records.
	 *   TODO: introduce HEADER_TRACING_DATA2 with guaranteed alignment.
	 *
	 * COMPRESSED (81): raw ZSTD output, arbitrary length.  Already
	 *   superseded by COMPRESSED2 (83) with PERF_ALIGN.
	 *
	 * HEADER_FEATURE (80): do_write_string() uses a 4-byte length
	 *   prefix with no padding to 8-byte total.
	 *   TODO: introduce HEADER_FEATURE2 with guaranteed alignment.
	 */
	if (event->header.size % sizeof(u64) &&
	    event->header.type != PERF_RECORD_HEADER_TRACING_DATA &&
	    event->header.type != PERF_RECORD_COMPRESSED &&
	    event->header.type != PERF_RECORD_HEADER_FEATURE) {
		pr_err("ERROR: at offset %#" PRIx64 ": %s (%u) event size %u is not 8-byte aligned, aborting\n",
		       file_offset, perf_event__name(event->header.type),
		       event->header.type, event->header.size);
		return -EINVAL;
	}

	if (event->header.type >= PERF_RECORD_HEADER_MAX) {
		/* This perf is outdated and does not support the latest event type. */
		ui__warning("Unsupported header type %u, please consider updating perf.\n",
			    event->header.type);
		/*
		 * Return 0 to skip: the caller (reader__read_event)
		 * already advances by event->header.size.
		 */
		return 0;
	}

	/*
	 * Skip rather than abort: a too-small-but-aligned event
	 * can be safely stepped over without misaligning the stream.
	 */
	if (perf_event__too_small(event, &min_sz)) {
		pr_warning("WARNING: at offset %#" PRIx64 ": %s (%u) event size %u too small (min %u), skipping\n",
			   file_offset, perf_event__name(event->header.type),
			   event->header.type, event->header.size, min_sz);
		return 0;
	}

	if (session->header.needs_swap &&
	    event_swap(event, evlist__sample_id_all(evlist))) {
		pr_warning("WARNING: at offset %#" PRIx64 ": swap failed for %s (%u) event, skipping\n",
			   file_offset, perf_event__name(event->header.type),
			   event->header.type);
		return 0;
	}

	events_stats__inc(evlist__stats(evlist), event->header.type);

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
	const struct events_stats *stats = evlist__stats(session->evlist);

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
	const struct perf_tool *tool = session->tool;
	struct ui_progress prog;
	union perf_event *event;
	uint32_t size, cur_size = 0;
	void *buf = NULL;
	s64 skip = 0;
	u64 head;
	ssize_t err;
	void *p;
	bool update_prog = false;

	/*
	 * If it's from a file saving pipe data (by redirection), it would have
	 * a file name other than "-".  Then we can get the total size and show
	 * the progress.
	 */
	if (strcmp(session->data->path, "-") && session->data->file.size) {
		ui_progress__init_size(&prog, session->data->file.size,
				       "Processing events...");
		update_prog = true;
	}

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
		pr_err("%#" PRIx64 " [%#x]: piped event processing failed for event of type: %s (%d)\n",
			head, event->header.size,
			perf_event__name(event->header.type),
			event->header.type);
		err = -EINVAL;
		goto out_err;
	}

	head += size;

	if (skip > 0)
		head += skip;

	err = __perf_session__process_decomp_events(session);
	if (err)
		goto out_err;

	if (update_prog)
		ui_progress__update(&prog, size);

	if (!session_done())
		goto more;
done:
	/* do the final flush for ordered samples */
	err = ordered_events__flush(oe, OE_FLUSH__FINAL);
	if (err)
		goto out_err;
	err = session__flush_deferred_samples(session, tool);
	if (err)
		goto out_err;
	err = auxtrace__flush_events(session, tool);
	if (err)
		goto out_err;
	err = perf_session__flush_thread_stacks(session);
out_err:
	free(buf);
	if (update_prog)
		ui_progress__finish();
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
			pr_err("%#" PRIx64 " [%#x]: decompress event processing failed for event of type: %s (%d)\n",
				decomp->file_pos + decomp->head, event->header.size,
				perf_event__name(event->header.type),
				event->header.type);
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

	/*
	 * Native-endian: MAP_SHARED + PROT_READ — the kernel
	 * guarantees page-level coherence but a concurrent writer
	 * could modify the file between validation and use.  This
	 * is a theoretical TOCTOU that affects the entire perf.data
	 * processing pipeline; fixing it would require copying each
	 * event to a private buffer before processing.
	 *
	 * Cross-endian: MAP_PRIVATE + PROT_WRITE — swap handlers
	 * get a copy-on-write snapshot immune to concurrent writes.
	 */
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
		/*
		 * mmap_size was set to the full file extent (data_offset +
		 * data_size) but file_offset was shifted forward by
		 * page_offset for page alignment.  Reduce by page_offset
		 * so the bounds check reflects the file-backed portion
		 * of the mapping — pages beyond the file cause SIGBUS.
		 */
		session->one_mmap_size = rd->mmap_size - page_offset;
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
		errno = -skip;
		pr_err("%#" PRIx64 " [%#x]: processing failed for event of type: %s (%d) [%m]\n",
		       rd->file_offset + rd->head, event->header.size,
		       perf_event__name(event->header.type),
		       event->header.type);
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
	const struct perf_tool *tool = session->tool;
	struct ui_progress prog;
	int err;

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
	err = session__flush_deferred_samples(session, tool);
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
	const struct perf_tool *tool = session->tool;
	int i, ret, readers, nr_readers;
	struct ui_progress prog;
	u64 total_size = perf_data__size(session->data);
	struct reader *rd;

	ui_progress__init_size(&prog, total_size, "Processing events...");

	nr_readers = 1;
	for (i = 0; i < data->dir.nr; i++) {
		if (data->dir.files[i].size)
			nr_readers++;
	}

	rd = calloc(nr_readers, sizeof(struct reader));
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
			.fd		 = perf_data_file__fd(&data->dir.files[i]),
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

	ret = session__flush_deferred_samples(session, tool);
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

bool perf_session__has_switch_events(struct perf_session *session)
{
	struct evsel *evsel;

	evlist__for_each_entry(session->evlist, evsel) {
		if (evsel->core.attr.context_switch)
			return true;
	}

	return false;
}

int map__set_kallsyms_ref_reloc_sym(struct map *map, const char *symbol_name, u64 addr)
{
	char *bracket, *name;
	struct ref_reloc_sym *ref;
	struct kmap *kmap;

	ref = zalloc(sizeof(struct ref_reloc_sym));
	if (ref == NULL)
		return -ENOMEM;

	ref->name = name = strdup(symbol_name);
	if (ref->name == NULL) {
		free(ref);
		return -ENOMEM;
	}

	bracket = strchr(name, ']');
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

size_t perf_session__fprintf_nr_events(struct perf_session *session, FILE *fp)
{
	size_t ret;
	const char *msg = "";

	if (perf_header__has_feat(&session->header, HEADER_AUXTRACE))
		msg = " (excludes AUX area (e.g. instruction trace) decoded / synthesized events)";

	ret = fprintf(fp, "\nAggregated stats:%s\n", msg);

	ret += events_stats__fprintf(evlist__stats(session->evlist), fp);
	return ret;
}

size_t perf_session__fprintf(struct perf_session *session, FILE *fp)
{
	size_t ret = machine__fprintf(&session->machines.host, fp);

	for (struct rb_node *nd = rb_first_cached(&session->machines.guests); nd; nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);

		ret += machine__fprintf(pos, fp);
	}
	return ret;
}

void perf_session__dump_kmaps(struct perf_session *session)
{
	int save_verbose = verbose;

	fflush(stdout);
	fprintf(stderr, "Kernel and module maps:\n");
	verbose = 0; /* Suppress verbose to print a summary only */
	maps__fprintf(machine__kernel_maps(&session->machines.host), stderr);
	verbose = save_verbose;
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
	unsigned int i;
	int err = -1;
	struct perf_cpu_map *map;
	int nr_cpus = min(perf_session__env(session)->nr_cpus_avail, MAX_NR_CPUS);
	struct perf_cpu cpu;

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

	perf_cpu_map__for_each_cpu(cpu, i, map) {
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

int perf_event__process_id_index(const struct perf_tool *tool __maybe_unused,
				 struct perf_session *session,
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

int perf_session__dsos_hit_all(struct perf_session *session)
{
	struct rb_node *nd;
	int err;

	err = machine__hit_all_dsos(&session->machines.host);
	if (err)
		return err;

	for (nd = rb_first_cached(&session->machines.guests); nd;
	     nd = rb_next(nd)) {
		struct machine *pos = rb_entry(nd, struct machine, rb_node);

		err = machine__hit_all_dsos(pos);
		if (err)
			return err;
	}

	return 0;
}

struct perf_env *perf_session__env(struct perf_session *session)
{
	return &session->header.env;
}

struct perf_session__e_machine_cb_args {
	uint32_t e_flags;
	uint16_t e_machine;
};

static int perf_session__e_machine_cb(struct thread *thread, void *_args)
{
	struct perf_session__e_machine_cb_args *args = _args;

	args->e_machine = thread__e_machine(thread, /*machine=*/NULL, &args->e_flags);
	return args->e_machine != EM_NONE ? 1 : 0;
}

/*
 * Note, a machine may have mixed 32-bit and 64-bit processes and so mixed
 * e_machines. Use thread__e_machine when this matters.
 */
uint16_t perf_session__e_machine(struct perf_session *session, uint32_t *e_flags)
{
	struct perf_session__e_machine_cb_args args = {
		.e_machine = EM_NONE,
	};
	struct perf_env *env;

	if (!session) {
		/* Default to assuming a host machine. */
		if (e_flags)
			*e_flags = EF_HOST;

		return EM_HOST;
	}

	/*
	 * Is the env caching an e_machine? If not we want to compute from the
	 * more accurate threads.
	 */
	env = perf_session__env(session);
	if (env && env->e_machine != EM_NONE)
		return perf_env__e_machine(env, e_flags);

	/*
	 * Compute from threads, note this is more accurate than
	 * perf_env__e_machine that falls back on EM_HOST and doesn't consider
	 * mixed 32-bit and 64-bit threads.
	 */
	machines__for_each_thread(&session->machines,
				  perf_session__e_machine_cb,
				  &args);

	if (args.e_machine != EM_NONE) {
		if (env) {
			env->e_machine = args.e_machine;
			env->e_flags = args.e_flags;
		}
		if (e_flags)
			*e_flags = args.e_flags;

		return args.e_machine;
	}

	/*
	 * Couldn't determine from the perf_env or current set of
	 * threads. Potentially use logic that uses the arch string otherwise
	 * default to the host. Don't cache in the perf_env in case later
	 * threads indicate a better ELF machine type.
	 */
	return perf_env__e_machine_nocache(env, e_flags);
}
