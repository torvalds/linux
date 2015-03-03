#include <linux/kernel.h>
#include <traceevent/event-parse.h>

#include <byteswap.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "evlist.h"
#include "evsel.h"
#include "session.h"
#include "tool.h"
#include "sort.h"
#include "util.h"
#include "cpumap.h"
#include "perf_regs.h"
#include "asm/bug.h"

static int machines__deliver_event(struct machines *machines,
				   struct perf_evlist *evlist,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct perf_tool *tool, u64 file_offset);

static int perf_session__open(struct perf_session *session)
{
	struct perf_data_file *file = session->file;

	if (perf_session__read_header(session) < 0) {
		pr_err("incompatible file format (rerun with -v to learn more)");
		return -1;
	}

	if (perf_data_file__is_pipe(file))
		return 0;

	if (!perf_evlist__valid_sample_type(session->evlist)) {
		pr_err("non matching sample_type");
		return -1;
	}

	if (!perf_evlist__valid_sample_id_all(session->evlist)) {
		pr_err("non matching sample_id_all");
		return -1;
	}

	if (!perf_evlist__valid_read_format(session->evlist)) {
		pr_err("non matching read_format");
		return -1;
	}

	return 0;
}

void perf_session__set_id_hdr_size(struct perf_session *session)
{
	u16 id_hdr_size = perf_evlist__id_hdr_size(session->evlist);

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
	struct perf_evsel *evsel;

	evlist__for_each(session->evlist, evsel) {
		if (evsel->attr.comm_exec)
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
					 struct ordered_event *event,
					 struct perf_sample *sample)
{
	return machines__deliver_event(oe->machines, oe->evlist, event->event,
				       sample, oe->tool, event->file_offset);
}

struct perf_session *perf_session__new(struct perf_data_file *file,
				       bool repipe, struct perf_tool *tool)
{
	struct perf_session *session = zalloc(sizeof(*session));

	if (!session)
		goto out;

	session->repipe = repipe;
	machines__init(&session->machines);

	if (file) {
		if (perf_data_file__open(file))
			goto out_delete;

		session->file = file;

		if (perf_data_file__is_read(file)) {
			if (perf_session__open(session) < 0)
				goto out_close;

			perf_session__set_id_hdr_size(session);
			perf_session__set_comm_exec(session);
		}
	}

	if (!file || perf_data_file__is_write(file)) {
		/*
		 * In O_RDONLY mode this will be performed when reading the
		 * kernel MMAP event, in perf_event__process_mmap().
		 */
		if (perf_session__create_kernel_maps(session) < 0)
			pr_warning("Cannot read kernel map\n");
	}

	if (tool && tool->ordering_requires_timestamps &&
	    tool->ordered_events && !perf_evlist__sample_id_all(session->evlist)) {
		dump_printf("WARNING: No sample_id_all support, falling back to unordered processing\n");
		tool->ordered_events = false;
	} else {
		ordered_events__init(&session->ordered_events, &session->machines,
				     session->evlist, tool, ordered_events__deliver_event);
	}

	return session;

 out_close:
	perf_data_file__close(file);
 out_delete:
	perf_session__delete(session);
 out:
	return NULL;
}

static void perf_session__delete_threads(struct perf_session *session)
{
	machine__delete_threads(&session->machines.host);
}

static void perf_session_env__delete(struct perf_session_env *env)
{
	zfree(&env->hostname);
	zfree(&env->os_release);
	zfree(&env->version);
	zfree(&env->arch);
	zfree(&env->cpu_desc);
	zfree(&env->cpuid);

	zfree(&env->cmdline);
	zfree(&env->sibling_cores);
	zfree(&env->sibling_threads);
	zfree(&env->numa_nodes);
	zfree(&env->pmu_mappings);
}

void perf_session__delete(struct perf_session *session)
{
	perf_session__destroy_kernel_maps(session);
	perf_session__delete_threads(session);
	perf_session_env__delete(&session->header.env);
	machines__exit(&session->machines);
	if (session->file)
		perf_data_file__close(session->file);
	free(session);
}

static int process_event_synth_tracing_data_stub(struct perf_tool *tool
						 __maybe_unused,
						 union perf_event *event
						 __maybe_unused,
						 struct perf_session *session
						__maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_synth_attr_stub(struct perf_tool *tool __maybe_unused,
					 union perf_event *event __maybe_unused,
					 struct perf_evlist **pevlist
					 __maybe_unused)
{
	dump_printf(": unhandled!\n");
	return 0;
}

static int process_event_sample_stub(struct perf_tool *tool __maybe_unused,
				     union perf_event *event __maybe_unused,
				     struct perf_sample *sample __maybe_unused,
				     struct perf_evsel *evsel __maybe_unused,
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

static int process_build_id_stub(struct perf_tool *tool __maybe_unused,
				 union perf_event *event __maybe_unused,
				 struct perf_session *session __maybe_unused)
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

static int process_finished_round(struct perf_tool *tool,
				  union perf_event *event,
				  struct ordered_events *oe);

static int process_id_index_stub(struct perf_tool *tool __maybe_unused,
				 union perf_event *event __maybe_unused,
				 struct perf_session *perf_session
				 __maybe_unused)
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
	if (tool->fork == NULL)
		tool->fork = process_event_stub;
	if (tool->exit == NULL)
		tool->exit = process_event_stub;
	if (tool->lost == NULL)
		tool->lost = perf_event__process_lost;
	if (tool->read == NULL)
		tool->read = process_event_sample_stub;
	if (tool->throttle == NULL)
		tool->throttle = process_event_stub;
	if (tool->unthrottle == NULL)
		tool->unthrottle = process_event_stub;
	if (tool->attr == NULL)
		tool->attr = process_event_synth_attr_stub;
	if (tool->tracing_data == NULL)
		tool->tracing_data = process_event_synth_tracing_data_stub;
	if (tool->build_id == NULL)
		tool->build_id = process_build_id_stub;
	if (tool->finished_round == NULL) {
		if (tool->ordered_events)
			tool->finished_round = process_finished_round;
		else
			tool->finished_round = process_finished_round_stub;
	}
	if (tool->id_index == NULL)
		tool->id_index = process_id_index_stub;
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
	event->mmap2.maj   = bswap_32(event->mmap2.maj);
	event->mmap2.min   = bswap_32(event->mmap2.min);
	event->mmap2.ino   = bswap_64(event->mmap2.ino);

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

static void perf_event__throttle_swap(union perf_event *event,
				      bool sample_id_all)
{
	event->throttle.time	  = bswap_64(event->throttle.time);
	event->throttle.id	  = bswap_64(event->throttle.id);
	event->throttle.stream_id = bswap_64(event->throttle.stream_id);

	if (sample_id_all)
		swap_sample_id_all(event, &event->throttle + 1);
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
 * throught endian village. ABI says:
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
	attr->config		= bswap_64(attr->config);
	attr->sample_period	= bswap_64(attr->sample_period);
	attr->sample_type	= bswap_64(attr->sample_type);
	attr->read_format	= bswap_64(attr->read_format);
	attr->wakeup_events	= bswap_32(attr->wakeup_events);
	attr->bp_type		= bswap_32(attr->bp_type);
	attr->bp_addr		= bswap_64(attr->bp_addr);
	attr->bp_len		= bswap_64(attr->bp_len);
	attr->branch_sample_type = bswap_64(attr->branch_sample_type);
	attr->sample_regs_user	 = bswap_64(attr->sample_regs_user);
	attr->sample_stack_user  = bswap_32(attr->sample_stack_user);

	swap_bitfield((u8 *) (&attr->read_format + 1), sizeof(u64));
}

static void perf_event__hdr_attr_swap(union perf_event *event,
				      bool sample_id_all __maybe_unused)
{
	size_t size;

	perf_event__attr_swap(&event->attr.attr);

	size = event->header.size;
	size -= (void *)&event->attr.id - (void *)event;
	mem_bswap_64(event->attr.id, size);
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
	[PERF_RECORD_HEADER_ATTR]	  = perf_event__hdr_attr_swap,
	[PERF_RECORD_HEADER_EVENT_TYPE]	  = perf_event__event_type_swap,
	[PERF_RECORD_HEADER_TRACING_DATA] = perf_event__tracing_data_swap,
	[PERF_RECORD_HEADER_BUILD_ID]	  = NULL,
	[PERF_RECORD_ID_INDEX]		  = perf_event__all64_swap,
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
static int process_finished_round(struct perf_tool *tool __maybe_unused,
				  union perf_event *event __maybe_unused,
				  struct ordered_events *oe)
{
	return ordered_events__flush(oe, OE_FLUSH__ROUND);
}

int perf_session__queue_event(struct perf_session *s, union perf_event *event,
			      struct perf_sample *sample, u64 file_offset)
{
	return ordered_events__queue(&s->ordered_events, event, sample, file_offset);
}

static void callchain__lbr_callstack_printf(struct perf_sample *sample)
{
	struct ip_callchain *callchain = sample->callchain;
	struct branch_stack *lbr_stack = sample->branch_stack;
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
		 * The LBR registers will recorde like
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
		       (int)(kernel_callchain_nr), lbr_stack->entries[0].to);
		for (i = 0; i < lbr_stack->nr; i++)
			printf("..... %2d: %016" PRIx64 "\n",
			       (int)(i + kernel_callchain_nr + 1), lbr_stack->entries[i].from);
	}
}

static void callchain__printf(struct perf_evsel *evsel,
			      struct perf_sample *sample)
{
	unsigned int i;
	struct ip_callchain *callchain = sample->callchain;

	if (has_branch_callstack(evsel))
		callchain__lbr_callstack_printf(sample);

	printf("... FP chain: nr:%" PRIu64 "\n", callchain->nr);

	for (i = 0; i < callchain->nr; i++)
		printf("..... %2d: %016" PRIx64 "\n",
		       i, callchain->ips[i]);
}

static void branch_stack__printf(struct perf_sample *sample)
{
	uint64_t i;

	printf("... branch stack: nr:%" PRIu64 "\n", sample->branch_stack->nr);

	for (i = 0; i < sample->branch_stack->nr; i++)
		printf("..... %2"PRIu64": %016" PRIx64 " -> %016" PRIx64 "\n",
			i, sample->branch_stack->entries[i].from,
			sample->branch_stack->entries[i].to);
}

static void regs_dump__printf(u64 mask, u64 *regs)
{
	unsigned rid, i = 0;

	for_each_set_bit(rid, (unsigned long *) &mask, sizeof(mask) * 8) {
		u64 val = regs[i++];

		printf(".... %-5s 0x%" PRIx64 "\n",
		       perf_reg_name(rid), val);
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

static void regs__printf(const char *type, struct regs_dump *regs)
{
	u64 mask = regs->mask;

	printf("... %s regs: mask 0x%" PRIx64 " ABI %s\n",
	       type,
	       mask,
	       regs_dump_abi(regs));

	regs_dump__printf(mask, regs->regs);
}

static void regs_user__printf(struct perf_sample *sample)
{
	struct regs_dump *user_regs = &sample->user_regs;

	if (user_regs->regs)
		regs__printf("user", user_regs);
}

static void regs_intr__printf(struct perf_sample *sample)
{
	struct regs_dump *intr_regs = &sample->intr_regs;

	if (intr_regs->regs)
		regs__printf("intr", intr_regs);
}

static void stack_user__printf(struct stack_dump *dump)
{
	printf("... ustack: size %" PRIu64 ", offset 0x%x\n",
	       dump->size, dump->offset);
}

static void perf_evlist__print_tstamp(struct perf_evlist *evlist,
				       union perf_event *event,
				       struct perf_sample *sample)
{
	u64 sample_type = __perf_evlist__combined_sample_type(evlist);

	if (event->header.type != PERF_RECORD_SAMPLE &&
	    !perf_evlist__sample_id_all(evlist)) {
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
		u64 i;

		printf(".... group nr %" PRIu64 "\n", sample->read.group.nr);

		for (i = 0; i < sample->read.group.nr; i++) {
			struct sample_read_value *value;

			value = &sample->read.group.values[i];
			printf("..... id %016" PRIx64
			       ", value %016" PRIx64 "\n",
			       value->id, value->value);
		}
	} else
		printf("..... id %016" PRIx64 ", value %016" PRIx64 "\n",
			sample->read.one.id, sample->read.one.value);
}

static void dump_event(struct perf_evlist *evlist, union perf_event *event,
		       u64 file_offset, struct perf_sample *sample)
{
	if (!dump_trace)
		return;

	printf("\n%#" PRIx64 " [%#x]: event: %d\n",
	       file_offset, event->header.size, event->header.type);

	trace_event(event);

	if (sample)
		perf_evlist__print_tstamp(evlist, event, sample);

	printf("%#" PRIx64 " [%#x]: PERF_RECORD_%s", file_offset,
	       event->header.size, perf_event__name(event->header.type));
}

static void dump_sample(struct perf_evsel *evsel, union perf_event *event,
			struct perf_sample *sample)
{
	u64 sample_type;

	if (!dump_trace)
		return;

	printf("(IP, 0x%x): %d/%d: %#" PRIx64 " period: %" PRIu64 " addr: %#" PRIx64 "\n",
	       event->header.misc, sample->pid, sample->tid, sample->ip,
	       sample->period, sample->addr);

	sample_type = evsel->attr.sample_type;

	if (sample_type & PERF_SAMPLE_CALLCHAIN)
		callchain__printf(evsel, sample);

	if ((sample_type & PERF_SAMPLE_BRANCH_STACK) && !has_branch_callstack(evsel))
		branch_stack__printf(sample);

	if (sample_type & PERF_SAMPLE_REGS_USER)
		regs_user__printf(sample);

	if (sample_type & PERF_SAMPLE_REGS_INTR)
		regs_intr__printf(sample);

	if (sample_type & PERF_SAMPLE_STACK_USER)
		stack_user__printf(&sample->user_stack);

	if (sample_type & PERF_SAMPLE_WEIGHT)
		printf("... weight: %" PRIu64 "\n", sample->weight);

	if (sample_type & PERF_SAMPLE_DATA_SRC)
		printf(" . data_src: 0x%"PRIx64"\n", sample->data_src);

	if (sample_type & PERF_SAMPLE_TRANSACTION)
		printf("... transaction: %" PRIx64 "\n", sample->transaction);

	if (sample_type & PERF_SAMPLE_READ)
		sample_read__printf(sample, evsel->attr.read_format);
}

static struct machine *machines__find_for_cpumode(struct machines *machines,
					       union perf_event *event,
					       struct perf_sample *sample)
{
	const u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	struct machine *machine;

	if (perf_guest &&
	    ((cpumode == PERF_RECORD_MISC_GUEST_KERNEL) ||
	     (cpumode == PERF_RECORD_MISC_GUEST_USER))) {
		u32 pid;

		if (event->header.type == PERF_RECORD_MMAP
		    || event->header.type == PERF_RECORD_MMAP2)
			pid = event->mmap.pid;
		else
			pid = sample->pid;

		machine = machines__find(machines, pid);
		if (!machine)
			machine = machines__find(machines, DEFAULT_GUEST_KERNEL_ID);
		return machine;
	}

	return &machines->host;
}

static int deliver_sample_value(struct perf_evlist *evlist,
				struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct sample_read_value *v,
				struct machine *machine)
{
	struct perf_sample_id *sid = perf_evlist__id2sid(evlist, v->id);

	if (sid) {
		sample->id     = v->id;
		sample->period = v->value - sid->period;
		sid->period    = v->value;
	}

	if (!sid || sid->evsel == NULL) {
		++evlist->stats.nr_unknown_id;
		return 0;
	}

	return tool->sample(tool, event, sample, sid->evsel, machine);
}

static int deliver_sample_group(struct perf_evlist *evlist,
				struct perf_tool *tool,
				union  perf_event *event,
				struct perf_sample *sample,
				struct machine *machine)
{
	int ret = -EINVAL;
	u64 i;

	for (i = 0; i < sample->read.group.nr; i++) {
		ret = deliver_sample_value(evlist, tool, event, sample,
					   &sample->read.group.values[i],
					   machine);
		if (ret)
			break;
	}

	return ret;
}

static int
 perf_evlist__deliver_sample(struct perf_evlist *evlist,
			     struct perf_tool *tool,
			     union  perf_event *event,
			     struct perf_sample *sample,
			     struct perf_evsel *evsel,
			     struct machine *machine)
{
	/* We know evsel != NULL. */
	u64 sample_type = evsel->attr.sample_type;
	u64 read_format = evsel->attr.read_format;

	/* Standard sample delievery. */
	if (!(sample_type & PERF_SAMPLE_READ))
		return tool->sample(tool, event, sample, evsel, machine);

	/* For PERF_SAMPLE_READ we have either single or group mode. */
	if (read_format & PERF_FORMAT_GROUP)
		return deliver_sample_group(evlist, tool, event, sample,
					    machine);
	else
		return deliver_sample_value(evlist, tool, event, sample,
					    &sample->read.one, machine);
}

static int machines__deliver_event(struct machines *machines,
				   struct perf_evlist *evlist,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct perf_tool *tool, u64 file_offset)
{
	struct perf_evsel *evsel;
	struct machine *machine;

	dump_event(evlist, event, file_offset, sample);

	evsel = perf_evlist__id2evsel(evlist, sample->id);

	machine = machines__find_for_cpumode(machines, event, sample);

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		dump_sample(evsel, event, sample);
		if (evsel == NULL) {
			++evlist->stats.nr_unknown_id;
			return 0;
		}
		if (machine == NULL) {
			++evlist->stats.nr_unprocessable_samples;
			return 0;
		}
		return perf_evlist__deliver_sample(evlist, tool, event, sample, evsel, machine);
	case PERF_RECORD_MMAP:
		return tool->mmap(tool, event, sample, machine);
	case PERF_RECORD_MMAP2:
		return tool->mmap2(tool, event, sample, machine);
	case PERF_RECORD_COMM:
		return tool->comm(tool, event, sample, machine);
	case PERF_RECORD_FORK:
		return tool->fork(tool, event, sample, machine);
	case PERF_RECORD_EXIT:
		return tool->exit(tool, event, sample, machine);
	case PERF_RECORD_LOST:
		if (tool->lost == perf_event__process_lost)
			evlist->stats.total_lost += event->lost.lost;
		return tool->lost(tool, event, sample, machine);
	case PERF_RECORD_READ:
		return tool->read(tool, event, sample, evsel, machine);
	case PERF_RECORD_THROTTLE:
		return tool->throttle(tool, event, sample, machine);
	case PERF_RECORD_UNTHROTTLE:
		return tool->unthrottle(tool, event, sample, machine);
	default:
		++evlist->stats.nr_unknown_events;
		return -1;
	}
}

static s64 perf_session__process_user_event(struct perf_session *session,
					    union perf_event *event,
					    u64 file_offset)
{
	struct ordered_events *oe = &session->ordered_events;
	struct perf_tool *tool = oe->tool;
	int fd = perf_data_file__fd(session->file);
	int err;

	dump_event(session->evlist, event, file_offset, NULL);

	/* These events are processed right away */
	switch (event->header.type) {
	case PERF_RECORD_HEADER_ATTR:
		err = tool->attr(tool, event, &session->evlist);
		if (err == 0) {
			perf_session__set_id_hdr_size(session);
			perf_session__set_comm_exec(session);
		}
		return err;
	case PERF_RECORD_HEADER_EVENT_TYPE:
		/*
		 * Depreceated, but we need to handle it for sake
		 * of old data files create in pipe mode.
		 */
		return 0;
	case PERF_RECORD_HEADER_TRACING_DATA:
		/* setup for reading amidst mmap */
		lseek(fd, file_offset, SEEK_SET);
		return tool->tracing_data(tool, event, session);
	case PERF_RECORD_HEADER_BUILD_ID:
		return tool->build_id(tool, event, session);
	case PERF_RECORD_FINISHED_ROUND:
		return tool->finished_round(tool, event, oe);
	case PERF_RECORD_ID_INDEX:
		return tool->id_index(tool, event, session);
	default:
		return -EINVAL;
	}
}

int perf_session__deliver_synth_event(struct perf_session *session,
				      union perf_event *event,
				      struct perf_sample *sample)
{
	struct perf_evlist *evlist = session->evlist;
	struct perf_tool *tool = session->ordered_events.tool;

	events_stats__inc(&evlist->stats, event->header.type);

	if (event->header.type >= PERF_RECORD_USER_TYPE_START)
		return perf_session__process_user_event(session, event, 0);

	return machines__deliver_event(&session->machines, evlist, event, sample, tool, 0);
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

	if (perf_data_file__is_pipe(session->file))
		return -1;

	fd = perf_data_file__fd(session->file);
	hdr_sz = sizeof(struct perf_event_header);

	if (buf_sz < hdr_sz)
		return -1;

	if (lseek(fd, file_offset, SEEK_SET) == (off_t)-1 ||
	    readn(fd, &buf, hdr_sz) != (ssize_t)hdr_sz)
		return -1;

	event = (union perf_event *)buf;

	if (session->header.needs_swap)
		perf_event_header__bswap(&event->header);

	if (event->header.size < hdr_sz)
		return -1;

	rest = event->header.size - hdr_sz;

	if (readn(fd, &buf, rest) != (ssize_t)rest)
		return -1;

	if (session->header.needs_swap)
		event_swap(event, perf_evlist__sample_id_all(session->evlist));

out_parse_sample:

	if (sample && event->header.type < PERF_RECORD_USER_TYPE_START &&
	    perf_evlist__parse_sample(session->evlist, event, sample))
		return -1;

	*event_ptr = event;

	return 0;
}

static s64 perf_session__process_event(struct perf_session *session,
				       union perf_event *event, u64 file_offset)
{
	struct perf_evlist *evlist = session->evlist;
	struct perf_tool *tool = session->ordered_events.tool;
	struct perf_sample sample;
	int ret;

	if (session->header.needs_swap)
		event_swap(event, perf_evlist__sample_id_all(evlist));

	if (event->header.type >= PERF_RECORD_HEADER_MAX)
		return -EINVAL;

	events_stats__inc(&evlist->stats, event->header.type);

	if (event->header.type >= PERF_RECORD_USER_TYPE_START)
		return perf_session__process_user_event(session, event, file_offset);

	/*
	 * For all kernel events we get the sample data
	 */
	ret = perf_evlist__parse_sample(evlist, event, &sample);
	if (ret)
		return ret;

	if (tool->ordered_events) {
		ret = perf_session__queue_event(session, event, &sample, file_offset);
		if (ret != -ETIME)
			return ret;
	}

	return machines__deliver_event(&session->machines, evlist, event,
				       &sample, tool, file_offset);
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

static struct thread *perf_session__register_idle_thread(struct perf_session *session)
{
	struct thread *thread;

	thread = machine__findnew_thread(&session->machines.host, 0, 0);
	if (thread == NULL || thread__set_comm(thread, "swapper", 0)) {
		pr_err("problem inserting idle task.\n");
		thread = NULL;
	}

	return thread;
}

static void perf_tool__warn_about_errors(const struct perf_tool *tool,
					 const struct events_stats *stats)
{
	if (tool->lost == perf_event__process_lost &&
	    stats->nr_events[PERF_RECORD_LOST] != 0) {
		ui__warning("Processed %d events and lost %d chunks!\n\n"
			    "Check IO/CPU overload!\n\n",
			    stats->nr_events[0],
			    stats->nr_events[PERF_RECORD_LOST]);
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

	if (stats->nr_unordered_events != 0)
		ui__warning("%u out of order events recorded.\n", stats->nr_unordered_events);
}

volatile int session_done;

static int __perf_session__process_pipe_events(struct perf_session *session)
{
	struct ordered_events *oe = &session->ordered_events;
	struct perf_tool *tool = oe->tool;
	int fd = perf_data_file__fd(session->file);
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
more:
	event = buf;
	err = readn(fd, event, sizeof(struct perf_event_header));
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
		err = readn(fd, p, size - sizeof(struct perf_event_header));
		if (err <= 0) {
			if (err == 0) {
				pr_err("unexpected end of event stream\n");
				goto done;
			}

			pr_err("failed to read event data\n");
			goto out_err;
		}
	}

	if ((skip = perf_session__process_event(session, event, head)) < 0) {
		pr_err("%#" PRIx64 " [%#x]: failed to process type: %d\n",
		       head, event->header.size, event->header.type);
		err = -EINVAL;
		goto out_err;
	}

	head += size;

	if (skip > 0)
		head += skip;

	if (!session_done())
		goto more;
done:
	/* do the final flush for ordered samples */
	err = ordered_events__flush(oe, OE_FLUSH__FINAL);
out_err:
	free(buf);
	perf_tool__warn_about_errors(tool, &session->evlist->stats);
	ordered_events__free(&session->ordered_events);
	return err;
}

static union perf_event *
fetch_mmaped_event(struct perf_session *session,
		   u64 head, size_t mmap_size, char *buf)
{
	union perf_event *event;

	/*
	 * Ensure we have enough space remaining to read
	 * the size of the event in the headers.
	 */
	if (head + sizeof(event->header) > mmap_size)
		return NULL;

	event = (union perf_event *)(buf + head);

	if (session->header.needs_swap)
		perf_event_header__bswap(&event->header);

	if (head + event->header.size > mmap_size) {
		/* We're not fetching the event so swap back again */
		if (session->header.needs_swap)
			perf_event_header__bswap(&event->header);
		return NULL;
	}

	return event;
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

static int __perf_session__process_events(struct perf_session *session,
					  u64 data_offset, u64 data_size,
					  u64 file_size)
{
	struct ordered_events *oe = &session->ordered_events;
	struct perf_tool *tool = oe->tool;
	int fd = perf_data_file__fd(session->file);
	u64 head, page_offset, file_offset, file_pos, size;
	int err, mmap_prot, mmap_flags, map_idx = 0;
	size_t	mmap_size;
	char *buf, *mmaps[NUM_MMAPS];
	union perf_event *event;
	struct ui_progress prog;
	s64 skip;

	perf_tool__fill_defaults(tool);

	page_offset = page_size * (data_offset / page_size);
	file_offset = page_offset;
	head = data_offset - page_offset;

	if (data_size && (data_offset + data_size < file_size))
		file_size = data_offset + data_size;

	ui_progress__init(&prog, file_size, "Processing events...");

	mmap_size = MMAP_SIZE;
	if (mmap_size > file_size) {
		mmap_size = file_size;
		session->one_mmap = true;
	}

	memset(mmaps, 0, sizeof(mmaps));

	mmap_prot  = PROT_READ;
	mmap_flags = MAP_SHARED;

	if (session->header.needs_swap) {
		mmap_prot  |= PROT_WRITE;
		mmap_flags = MAP_PRIVATE;
	}
remap:
	buf = mmap(NULL, mmap_size, mmap_prot, mmap_flags, fd,
		   file_offset);
	if (buf == MAP_FAILED) {
		pr_err("failed to mmap file\n");
		err = -errno;
		goto out_err;
	}
	mmaps[map_idx] = buf;
	map_idx = (map_idx + 1) & (ARRAY_SIZE(mmaps) - 1);
	file_pos = file_offset + head;
	if (session->one_mmap) {
		session->one_mmap_addr = buf;
		session->one_mmap_offset = file_offset;
	}

more:
	event = fetch_mmaped_event(session, head, mmap_size, buf);
	if (!event) {
		if (mmaps[map_idx]) {
			munmap(mmaps[map_idx], mmap_size);
			mmaps[map_idx] = NULL;
		}

		page_offset = page_size * (head / page_size);
		file_offset += page_offset;
		head -= page_offset;
		goto remap;
	}

	size = event->header.size;

	if (size < sizeof(struct perf_event_header) ||
	    (skip = perf_session__process_event(session, event, file_pos)) < 0) {
		pr_err("%#" PRIx64 " [%#x]: failed to process type: %d\n",
		       file_offset + head, event->header.size,
		       event->header.type);
		err = -EINVAL;
		goto out_err;
	}

	if (skip)
		size += skip;

	head += size;
	file_pos += size;

	ui_progress__update(&prog, size);

	if (session_done())
		goto out;

	if (file_pos < file_size)
		goto more;

out:
	/* do the final flush for ordered samples */
	err = ordered_events__flush(oe, OE_FLUSH__FINAL);
out_err:
	ui_progress__finish();
	perf_tool__warn_about_errors(tool, &session->evlist->stats);
	ordered_events__free(&session->ordered_events);
	session->one_mmap = false;
	return err;
}

int perf_session__process_events(struct perf_session *session)
{
	u64 size = perf_data_file__size(session->file);
	int err;

	if (perf_session__register_idle_thread(session) == NULL)
		return -ENOMEM;

	if (!perf_data_file__is_pipe(session->file))
		err = __perf_session__process_events(session,
						     session->header.data_offset,
						     session->header.data_size, size);
	else
		err = __perf_session__process_pipe_events(session);

	return err;
}

bool perf_session__has_traces(struct perf_session *session, const char *msg)
{
	struct perf_evsel *evsel;

	evlist__for_each(session->evlist, evsel) {
		if (evsel->attr.type == PERF_TYPE_TRACEPOINT)
			return true;
	}

	pr_err("No trace sample to read. Did you call 'perf %s'?\n", msg);
	return false;
}

int maps__set_kallsyms_ref_reloc_sym(struct map **maps,
				     const char *symbol_name, u64 addr)
{
	char *bracket;
	enum map_type i;
	struct ref_reloc_sym *ref;

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

	for (i = 0; i < MAP__NR_TYPES; ++i) {
		struct kmap *kmap = map__kmap(maps[i]);
		kmap->ref_reloc_sym = ref;
	}

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
	size_t ret = fprintf(fp, "Aggregated stats:\n");

	ret += events_stats__fprintf(&session->evlist->stats, fp);
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

struct perf_evsel *perf_session__find_first_evtype(struct perf_session *session,
					      unsigned int type)
{
	struct perf_evsel *pos;

	evlist__for_each(session->evlist, pos) {
		if (pos->attr.type == type)
			return pos;
	}
	return NULL;
}

void perf_evsel__print_ip(struct perf_evsel *evsel, struct perf_sample *sample,
			  struct addr_location *al,
			  unsigned int print_opts, unsigned int stack_depth)
{
	struct callchain_cursor_node *node;
	int print_ip = print_opts & PRINT_IP_OPT_IP;
	int print_sym = print_opts & PRINT_IP_OPT_SYM;
	int print_dso = print_opts & PRINT_IP_OPT_DSO;
	int print_symoffset = print_opts & PRINT_IP_OPT_SYMOFFSET;
	int print_oneline = print_opts & PRINT_IP_OPT_ONELINE;
	int print_srcline = print_opts & PRINT_IP_OPT_SRCLINE;
	char s = print_oneline ? ' ' : '\t';

	if (symbol_conf.use_callchain && sample->callchain) {
		struct addr_location node_al;

		if (thread__resolve_callchain(al->thread, evsel,
					      sample, NULL, NULL,
					      PERF_MAX_STACK_DEPTH) != 0) {
			if (verbose)
				error("Failed to resolve callchain. Skipping\n");
			return;
		}
		callchain_cursor_commit(&callchain_cursor);

		if (print_symoffset)
			node_al = *al;

		while (stack_depth) {
			u64 addr = 0;

			node = callchain_cursor_current(&callchain_cursor);
			if (!node)
				break;

			if (node->sym && node->sym->ignore)
				goto next;

			if (print_ip)
				printf("%c%16" PRIx64, s, node->ip);

			if (node->map)
				addr = node->map->map_ip(node->map, node->ip);

			if (print_sym) {
				printf(" ");
				if (print_symoffset) {
					node_al.addr = addr;
					node_al.map  = node->map;
					symbol__fprintf_symname_offs(node->sym, &node_al, stdout);
				} else
					symbol__fprintf_symname(node->sym, stdout);
			}

			if (print_dso) {
				printf(" (");
				map__fprintf_dsoname(node->map, stdout);
				printf(")");
			}

			if (print_srcline)
				map__fprintf_srcline(node->map, addr, "\n  ",
						     stdout);

			if (!print_oneline)
				printf("\n");

			stack_depth--;
next:
			callchain_cursor_advance(&callchain_cursor);
		}

	} else {
		if (al->sym && al->sym->ignore)
			return;

		if (print_ip)
			printf("%16" PRIx64, sample->ip);

		if (print_sym) {
			printf(" ");
			if (print_symoffset)
				symbol__fprintf_symname_offs(al->sym, al,
							     stdout);
			else
				symbol__fprintf_symname(al->sym, stdout);
		}

		if (print_dso) {
			printf(" (");
			map__fprintf_dsoname(al->map, stdout);
			printf(")");
		}

		if (print_srcline)
			map__fprintf_srcline(al->map, al->addr, "\n  ", stdout);
	}
}

int perf_session__cpu_bitmap(struct perf_session *session,
			     const char *cpu_list, unsigned long *cpu_bitmap)
{
	int i, err = -1;
	struct cpu_map *map;

	for (i = 0; i < PERF_TYPE_MAX; ++i) {
		struct perf_evsel *evsel;

		evsel = perf_session__find_first_evtype(session, i);
		if (!evsel)
			continue;

		if (!(evsel->attr.sample_type & PERF_SAMPLE_CPU)) {
			pr_err("File does not contain CPU events. "
			       "Remove -c option to proceed.\n");
			return -1;
		}
	}

	map = cpu_map__new(cpu_list);
	if (map == NULL) {
		pr_err("Invalid cpu_list\n");
		return -1;
	}

	for (i = 0; i < map->nr; i++) {
		int cpu = map->map[i];

		if (cpu >= MAX_NR_CPUS) {
			pr_err("Requested CPU %d too large. "
			       "Consider raising MAX_NR_CPUS\n", cpu);
			goto out_delete_map;
		}

		set_bit(cpu, cpu_bitmap);
	}

	err = 0;

out_delete_map:
	cpu_map__delete(map);
	return err;
}

void perf_session__fprintf_info(struct perf_session *session, FILE *fp,
				bool full)
{
	struct stat st;
	int fd, ret;

	if (session == NULL || fp == NULL)
		return;

	fd = perf_data_file__fd(session->file);

	ret = fstat(fd, &st);
	if (ret == -1)
		return;

	fprintf(fp, "# ========\n");
	fprintf(fp, "# captured on: %s", ctime(&st.st_ctime));
	perf_header__fprintf_info(session, fp, full);
	fprintf(fp, "# ========\n#\n");
}


int __perf_session__set_tracepoints_handlers(struct perf_session *session,
					     const struct perf_evsel_str_handler *assocs,
					     size_t nr_assocs)
{
	struct perf_evsel *evsel;
	size_t i;
	int err;

	for (i = 0; i < nr_assocs; i++) {
		/*
		 * Adding a handler for an event not in the session,
		 * just ignore it.
		 */
		evsel = perf_evlist__find_tracepoint_by_name(session->evlist, assocs[i].name);
		if (evsel == NULL)
			continue;

		err = -EEXIST;
		if (evsel->handler != NULL)
			goto out;
		evsel->handler = assocs[i].handler;
	}

	err = 0;
out:
	return err;
}

int perf_event__process_id_index(struct perf_tool *tool __maybe_unused,
				 union perf_event *event,
				 struct perf_session *session)
{
	struct perf_evlist *evlist = session->evlist;
	struct id_index_event *ie = &event->id_index;
	size_t i, nr, max_nr;

	max_nr = (ie->header.size - sizeof(struct id_index_event)) /
		 sizeof(struct id_index_entry);
	nr = ie->nr;
	if (nr > max_nr)
		return -EINVAL;

	if (dump_trace)
		fprintf(stdout, " nr: %zu\n", nr);

	for (i = 0; i < nr; i++) {
		struct id_index_entry *e = &ie->entries[i];
		struct perf_sample_id *sid;

		if (dump_trace) {
			fprintf(stdout,	" ... id: %"PRIu64, e->id);
			fprintf(stdout,	"  idx: %"PRIu64, e->idx);
			fprintf(stdout,	"  cpu: %"PRId64, e->cpu);
			fprintf(stdout,	"  tid: %"PRId64"\n", e->tid);
		}

		sid = perf_evlist__id2sid(evlist, e->id);
		if (!sid)
			return -ENOENT;
		sid->idx = e->idx;
		sid->cpu = e->cpu;
		sid->tid = e->tid;
	}
	return 0;
}

int perf_event__synthesize_id_index(struct perf_tool *tool,
				    perf_event__handler_t process,
				    struct perf_evlist *evlist,
				    struct machine *machine)
{
	union perf_event *ev;
	struct perf_evsel *evsel;
	size_t nr = 0, i = 0, sz, max_nr, n;
	int err;

	pr_debug2("Synthesizing id index\n");

	max_nr = (UINT16_MAX - sizeof(struct id_index_event)) /
		 sizeof(struct id_index_entry);

	evlist__for_each(evlist, evsel)
		nr += evsel->ids;

	n = nr > max_nr ? max_nr : nr;
	sz = sizeof(struct id_index_event) + n * sizeof(struct id_index_entry);
	ev = zalloc(sz);
	if (!ev)
		return -ENOMEM;

	ev->id_index.header.type = PERF_RECORD_ID_INDEX;
	ev->id_index.header.size = sz;
	ev->id_index.nr = n;

	evlist__for_each(evlist, evsel) {
		u32 j;

		for (j = 0; j < evsel->ids; j++) {
			struct id_index_entry *e;
			struct perf_sample_id *sid;

			if (i >= n) {
				err = process(tool, ev, NULL, machine);
				if (err)
					goto out_err;
				nr -= n;
				i = 0;
			}

			e = &ev->id_index.entries[i++];

			e->id = evsel->id[j];

			sid = perf_evlist__id2sid(evlist, e->id);
			if (!sid) {
				free(ev);
				return -ENOENT;
			}

			e->idx = sid->idx;
			e->cpu = sid->cpu;
			e->tid = sid->tid;
		}
	}

	sz = sizeof(struct id_index_event) + nr * sizeof(struct id_index_entry);
	ev->id_index.header.size = sz;
	ev->id_index.nr = nr;

	err = process(tool, ev, NULL, machine);
out_err:
	free(ev);

	return err;
}
