// SPDX-License-Identifier: GPL-2.0
/*
 * VPA DTL PMU support
 */

#include <linux/string.h>
#include <inttypes.h>
#include "color.h"
#include "evlist.h"
#include "session.h"
#include "auxtrace.h"
#include "data.h"
#include "machine.h"
#include "debug.h"
#include "powerpc-vpadtl.h"
#include "sample.h"
#include "tool.h"

/*
 * Structure to save the auxtrace queue
 */
struct powerpc_vpadtl {
	struct auxtrace			auxtrace;
	struct auxtrace_queues		queues;
	struct auxtrace_heap		heap;
	u32				auxtrace_type;
	struct perf_session		*session;
	struct machine			*machine;
	u32				pmu_type;
	u64				sample_id;
};

struct boottb_freq {
	u64     boot_tb;
	u64     tb_freq;
	u64     timebase;
	u64     padded[3];
};

struct powerpc_vpadtl_queue {
	struct powerpc_vpadtl	*vpa;
	unsigned int		queue_nr;
	struct auxtrace_buffer	*buffer;
	struct thread		*thread;
	bool			on_heap;
	struct powerpc_vpadtl_entry	*dtl;
	u64			timestamp;
	unsigned long		pkt_len;
	unsigned long		buf_len;
	u64			boot_tb;
	u64			tb_freq;
	unsigned int		tb_buffer;
	unsigned int		size;
	bool			done;
	pid_t			pid;
	pid_t			tid;
	int			cpu;
};

const char *dispatch_reasons[11] = {
	"external_interrupt",
	"firmware_internal_event",
	"H_PROD",
	"decrementer_interrupt",
	"system_reset",
	"firmware_internal_event",
	"conferred_cycles",
	"time_slice",
	"virtual_memory_page_fault",
	"expropriated_adjunct",
	"priv_doorbell"};

const char *preempt_reasons[10] = {
	"unused",
	"firmware_internal_event",
	"H_CEDE",
	"H_CONFER",
	"time_slice",
	"migration_hibernation_page_fault",
	"virtual_memory_page_fault",
	"H_CONFER_ADJUNCT",
	"hcall_adjunct",
	"HDEC_adjunct"};

#define	dtl_entry_size	sizeof(struct powerpc_vpadtl_entry)

/*
 * Function to dump the dispatch trace data when perf report
 * is invoked with -D
 */
static void powerpc_vpadtl_dump(struct powerpc_vpadtl *vpa __maybe_unused,
			 unsigned char *buf, size_t len)
{
	struct powerpc_vpadtl_entry *dtl;
	int pkt_len, pos = 0;
	const char *color = PERF_COLOR_BLUE;

	color_fprintf(stdout, color,
			". ... VPA DTL PMU data: size %zu bytes, entries is %zu\n",
			len, len/dtl_entry_size);

	if (len % dtl_entry_size)
		len = len - (len % dtl_entry_size);

	while (len) {
		pkt_len = dtl_entry_size;
		printf(".");
		color_fprintf(stdout, color, "  %08x: ", pos);
		dtl = (struct powerpc_vpadtl_entry *)buf;
		if (dtl->timebase != 0) {
			printf("dispatch_reason:%s, preempt_reason:%s, "
					"enqueue_to_dispatch_time:%d, ready_to_enqueue_time:%d, "
					"waiting_to_ready_time:%d\n",
					dispatch_reasons[dtl->dispatch_reason],
					preempt_reasons[dtl->preempt_reason],
					be32_to_cpu(dtl->enqueue_to_dispatch_time),
					be32_to_cpu(dtl->ready_to_enqueue_time),
					be32_to_cpu(dtl->waiting_to_ready_time));
		} else {
			struct boottb_freq *boot_tb = (struct boottb_freq *)buf;

			printf("boot_tb: %" PRIu64 ", tb_freq: %" PRIu64 "\n",
					boot_tb->boot_tb, boot_tb->tb_freq);
		}

		pos += pkt_len;
		buf += pkt_len;
		len -= pkt_len;
	}
}

static unsigned long long powerpc_vpadtl_timestamp(struct powerpc_vpadtl_queue *vpaq)
{
	struct powerpc_vpadtl_entry *record = vpaq->dtl;
	unsigned long long timestamp = 0;
	unsigned long long boot_tb;
	unsigned long long diff;
	double result, div;
	double boot_freq;
	/*
	 * Formula used to get timestamp that can be co-related with
	 * other perf events:
	 * ((timbase from DTL entry - boot time) / frequency) * 1000000000
	 */
	if (record->timebase) {
		boot_tb = vpaq->boot_tb;
		boot_freq = vpaq->tb_freq;
		diff = be64_to_cpu(record->timebase) - boot_tb;
		div = diff / boot_freq;
		result = div;
		result = result * 1000000000;
		timestamp = result;
	}

	return timestamp;
}

static struct powerpc_vpadtl *session_to_vpa(struct perf_session *session)
{
	return container_of(session->auxtrace, struct powerpc_vpadtl, auxtrace);
}

static void powerpc_vpadtl_dump_event(struct powerpc_vpadtl *vpa, unsigned char *buf,
			       size_t len)
{
	printf(".\n");
	powerpc_vpadtl_dump(vpa, buf, len);
}

/*
 * Generate perf sample for each entry in the dispatch trace log.
 *   - sample ip is picked from srr0 field of powerpc_vpadtl_entry
 *   - sample cpu is logical cpu.
 *   - cpumode is set to PERF_RECORD_MISC_KERNEL
 *   - Additionally save the details in raw_data of sample. This
 *   is to print the relevant fields in perf_sample__fprintf_synth()
 *   when called from builtin-script
 */
static int powerpc_vpadtl_sample(struct powerpc_vpadtl_entry *record,
		struct powerpc_vpadtl *vpa, u64 save, int cpu)
{
	struct perf_sample sample;
	union perf_event event;

	sample.ip = be64_to_cpu(record->srr0);
	sample.period = 1;
	sample.cpu = cpu;
	sample.id = vpa->sample_id;
	sample.callchain = NULL;
	sample.branch_stack = NULL;
	memset(&event, 0, sizeof(event));
	sample.cpumode = PERF_RECORD_MISC_KERNEL;
	sample.time = save;
	sample.raw_data = record;
	sample.raw_size = sizeof(record);
	event.sample.header.type = PERF_RECORD_SAMPLE;
	event.sample.header.misc = sample.cpumode;
	event.sample.header.size = sizeof(struct perf_event_header);

	if (perf_session__deliver_synth_event(vpa->session, &event, &sample)) {
		pr_debug("Failed to create sample for dtl entry\n");
		return -1;
	}

	return 0;
}

static int powerpc_vpadtl_get_buffer(struct powerpc_vpadtl_queue *vpaq)
{
	struct auxtrace_buffer *buffer = vpaq->buffer;
	struct auxtrace_queues *queues = &vpaq->vpa->queues;
	struct auxtrace_queue *queue;

	queue = &queues->queue_array[vpaq->queue_nr];
	buffer = auxtrace_buffer__next(queue, buffer);

	if (!buffer)
		return 0;

	vpaq->buffer = buffer;
	vpaq->size = buffer->size;

	/* If the aux_buffer doesn't have data associated, try to load it */
	if (!buffer->data) {
		/* get the file desc associated with the perf data file */
		int fd = perf_data__fd(vpaq->vpa->session->data);

		buffer->data = auxtrace_buffer__get_data(buffer, fd);
		if (!buffer->data)
			return -ENOMEM;
	}

	vpaq->buf_len = buffer->size;

	if (buffer->size % dtl_entry_size)
		vpaq->buf_len = buffer->size - (buffer->size % dtl_entry_size);

	if (vpaq->tb_buffer != buffer->buffer_nr) {
		vpaq->pkt_len = 0;
		vpaq->tb_buffer = 0;
	}

	return 1;
}

/*
 * The first entry in the queue for VPA DTL PMU has the boot timebase,
 * frequency details which are needed to get timestamp which is required to
 * correlate with other events. Save the boot_tb and tb_freq as part of
 * powerpc_vpadtl_queue. The very next entry is the actual trace data to
 * be returned.
 */
static int powerpc_vpadtl_decode(struct powerpc_vpadtl_queue *vpaq)
{
	int ret;
	char *buf;
	struct boottb_freq *boottb;

	ret = powerpc_vpadtl_get_buffer(vpaq);
	if (ret <= 0)
		return ret;

	boottb = (struct boottb_freq *)vpaq->buffer->data;
	if (boottb->timebase == 0) {
		vpaq->boot_tb = boottb->boot_tb;
		vpaq->tb_freq = boottb->tb_freq;
		vpaq->pkt_len += dtl_entry_size;
	}

	buf = vpaq->buffer->data;
	buf += vpaq->pkt_len;
	vpaq->dtl = (struct powerpc_vpadtl_entry *)buf;

	vpaq->tb_buffer = vpaq->buffer->buffer_nr;
	vpaq->buffer = NULL;
	vpaq->buf_len = 0;

	return 1;
}

static int powerpc_vpadtl_decode_all(struct powerpc_vpadtl_queue *vpaq)
{
	int ret;
	unsigned char *buf;

	if (!vpaq->buf_len || vpaq->pkt_len == vpaq->size) {
		ret = powerpc_vpadtl_get_buffer(vpaq);
		if (ret <= 0)
			return ret;
	}

	if (vpaq->buffer) {
		buf = vpaq->buffer->data;
		buf += vpaq->pkt_len;
		vpaq->dtl = (struct powerpc_vpadtl_entry *)buf;
		if ((long long)be64_to_cpu(vpaq->dtl->timebase) <= 0) {
			if (vpaq->pkt_len != dtl_entry_size && vpaq->buf_len) {
				vpaq->pkt_len += dtl_entry_size;
				vpaq->buf_len -= dtl_entry_size;
			}
			return -1;
		}
		vpaq->pkt_len += dtl_entry_size;
		vpaq->buf_len -= dtl_entry_size;
	} else {
		return 0;
	}

	return 1;
}

static int powerpc_vpadtl_run_decoder(struct powerpc_vpadtl_queue *vpaq, u64 *timestamp)
{
	struct powerpc_vpadtl *vpa = vpaq->vpa;
	struct powerpc_vpadtl_entry *record;
	int ret;
	unsigned long long vpaq_timestamp;

	while (1) {
		ret = powerpc_vpadtl_decode_all(vpaq);
		if (!ret) {
			pr_debug("All data in the queue has been processed.\n");
			return 1;
		}

		/*
		 * Error is detected when decoding VPA PMU trace. Continue to
		 * the next trace data and find out more dtl entries.
		 */
		if (ret < 0)
			continue;

		record = vpaq->dtl;

		vpaq_timestamp = powerpc_vpadtl_timestamp(vpaq);

		/* Update timestamp for the last record */
		if (vpaq_timestamp > vpaq->timestamp)
			vpaq->timestamp = vpaq_timestamp;

		/*
		 * If the timestamp of the queue is later than timestamp of the
		 * coming perf event, bail out so can allow the perf event to
		 * be processed ahead.
		 */
		if (vpaq->timestamp >= *timestamp) {
			*timestamp = vpaq->timestamp;
			vpaq->pkt_len -= dtl_entry_size;
			vpaq->buf_len += dtl_entry_size;
			return 0;
		}

		ret = powerpc_vpadtl_sample(record, vpa, vpaq_timestamp, vpaq->cpu);
		if (ret)
			continue;
	}
	return 0;
}

/*
 * For each of the PERF_RECORD_XX record, compare the timestamp
 * of perf record with timestamp of top element in the auxtrace heap.
 * Process the auxtrace queue if the timestamp of element from heap is
 * lower than timestamp from entry in perf record.
 *
 * Update the timestamp of the auxtrace heap with the timestamp
 * of last processed entry from the auxtrace buffer.
 */
static int powerpc_vpadtl_process_queues(struct powerpc_vpadtl *vpa, u64 timestamp)
{
	unsigned int queue_nr;
	u64 ts;
	int ret;

	while (1) {
		struct auxtrace_queue *queue;
		struct powerpc_vpadtl_queue *vpaq;

		if (!vpa->heap.heap_cnt)
			return 0;

		if (vpa->heap.heap_array[0].ordinal >= timestamp)
			return 0;

		queue_nr = vpa->heap.heap_array[0].queue_nr;
		queue = &vpa->queues.queue_array[queue_nr];
		vpaq = queue->priv;

		auxtrace_heap__pop(&vpa->heap);

		if (vpa->heap.heap_cnt) {
			ts = vpa->heap.heap_array[0].ordinal + 1;
			if (ts > timestamp)
				ts = timestamp;
		} else {
			ts = timestamp;
		}

		ret = powerpc_vpadtl_run_decoder(vpaq, &ts);
		if (ret < 0) {
			auxtrace_heap__add(&vpa->heap, queue_nr, ts);
			return ret;
		}

		if (!ret) {
			ret = auxtrace_heap__add(&vpa->heap, queue_nr, ts);
			if (ret < 0)
				return ret;
		} else {
			vpaq->on_heap = false;
		}
	}
	return 0;
}

static struct powerpc_vpadtl_queue *powerpc_vpadtl__alloc_queue(struct powerpc_vpadtl *vpa,
						unsigned int queue_nr)
{
	struct powerpc_vpadtl_queue *vpaq;

	vpaq = zalloc(sizeof(*vpaq));
	if (!vpaq)
		return NULL;

	vpaq->vpa = vpa;
	vpaq->queue_nr = queue_nr;

	return vpaq;
}

/*
 * When the Dispatch Trace Log data is collected along with other events
 * like sched tracepoint events, it needs to be correlated and present
 * interleaved along with these events. Perf events can be collected
 * parallely across the CPUs.
 *
 * An auxtrace_queue is created for each CPU. Data within each queue is in
 * increasing order of timestamp. Allocate and setup auxtrace queues here.
 * All auxtrace queues is maintained in auxtrace heap in the increasing order
 * of timestamp. So always the lowest timestamp (entries to be processed first)
 * is on top of the heap.
 *
 * To add to auxtrace heap, fetch the timestamp from first DTL entry
 * for each of the queue.
 */
static int powerpc_vpadtl__setup_queue(struct powerpc_vpadtl *vpa,
		struct auxtrace_queue *queue,
		unsigned int queue_nr)
{
	struct powerpc_vpadtl_queue *vpaq = queue->priv;

	if (list_empty(&queue->head) || vpaq)
		return 0;

	vpaq = powerpc_vpadtl__alloc_queue(vpa, queue_nr);
	if (!vpaq)
		return -ENOMEM;

	queue->priv = vpaq;

	if (queue->cpu != -1)
		vpaq->cpu = queue->cpu;

	if (!vpaq->on_heap) {
		int ret;
retry:
		ret = powerpc_vpadtl_decode(vpaq);
		if (!ret)
			return 0;

		if (ret < 0)
			goto retry;

		vpaq->timestamp = powerpc_vpadtl_timestamp(vpaq);

		ret = auxtrace_heap__add(&vpa->heap, queue_nr, vpaq->timestamp);
		if (ret)
			return ret;
		vpaq->on_heap = true;
	}

	return 0;
}

static int powerpc_vpadtl__setup_queues(struct powerpc_vpadtl *vpa)
{
	unsigned int i;
	int ret;

	for (i = 0; i < vpa->queues.nr_queues; i++) {
		ret = powerpc_vpadtl__setup_queue(vpa, &vpa->queues.queue_array[i], i);
		if (ret)
			return ret;
	}

	return 0;
}

static int powerpc_vpadtl__update_queues(struct powerpc_vpadtl *vpa)
{
	if (vpa->queues.new_data) {
		vpa->queues.new_data = false;
		return powerpc_vpadtl__setup_queues(vpa);
	}

	return 0;
}

static int powerpc_vpadtl_process_event(struct perf_session *session,
				 union perf_event *event __maybe_unused,
				 struct perf_sample *sample,
				 const struct perf_tool *tool)
{
	struct powerpc_vpadtl *vpa = session_to_vpa(session);
	int err = 0;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("VPA requires ordered events\n");
		return -EINVAL;
	}

	if (sample->time) {
		err = powerpc_vpadtl__update_queues(vpa);
		if (err)
			return err;

		err = powerpc_vpadtl_process_queues(vpa, sample->time);
	}

	return err;
}

/*
 * Process PERF_RECORD_AUXTRACE records
 */
static int powerpc_vpadtl_process_auxtrace_event(struct perf_session *session,
					  union perf_event *event,
					  const struct perf_tool *tool __maybe_unused)
{
	struct powerpc_vpadtl *vpa = session_to_vpa(session);
	struct auxtrace_buffer *buffer;
	int fd = perf_data__fd(session->data);
	off_t data_offset;
	int err;

	if (!dump_trace)
		return 0;

	if (perf_data__is_pipe(session->data)) {
		data_offset = 0;
	} else {
		data_offset = lseek(fd, 0, SEEK_CUR);
		if (data_offset == -1)
			return -errno;
	}

	err = auxtrace_queues__add_event(&vpa->queues, session, event,
			data_offset, &buffer);

	if (err)
		return err;

	/* Dump here now we have copied a piped trace out of the pipe */
	if (auxtrace_buffer__get_data(buffer, fd)) {
		powerpc_vpadtl_dump_event(vpa, buffer->data, buffer->size);
		auxtrace_buffer__put_data(buffer);
	}

	return 0;
}

static int powerpc_vpadtl_flush(struct perf_session *session __maybe_unused,
			 const struct perf_tool *tool __maybe_unused)
{
	return 0;
}

static void powerpc_vpadtl_free_events(struct perf_session *session)
{
	struct powerpc_vpadtl *vpa = session_to_vpa(session);
	struct auxtrace_queues *queues = &vpa->queues;

	for (unsigned int i = 0; i < queues->nr_queues; i++)
		zfree(&queues->queue_array[i].priv);

	auxtrace_queues__free(queues);
}

static void powerpc_vpadtl_free(struct perf_session *session)
{
	struct powerpc_vpadtl *vpa = session_to_vpa(session);

	auxtrace_heap__free(&vpa->heap);
	powerpc_vpadtl_free_events(session);
	session->auxtrace = NULL;
	free(vpa);
}

static const char * const powerpc_vpadtl_info_fmts[] = {
	[POWERPC_VPADTL_TYPE]		= "  PMU Type           %"PRId64"\n",
};

static void powerpc_vpadtl_print_info(__u64 *arr)
{
	if (!dump_trace)
		return;

	fprintf(stdout, powerpc_vpadtl_info_fmts[POWERPC_VPADTL_TYPE], arr[POWERPC_VPADTL_TYPE]);
}

static void set_event_name(struct evlist *evlist, u64 id,
		const char *name)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.id && evsel->core.id[0] == id) {
			if (evsel->name)
				zfree(&evsel->name);
			evsel->name = strdup(name);
			break;
		}
	}
}

static int
powerpc_vpadtl_synth_events(struct powerpc_vpadtl *vpa, struct perf_session *session)
{
	struct evlist *evlist = session->evlist;
	struct evsel *evsel;
	struct perf_event_attr attr;
	bool found = false;
	u64 id;
	int err;

	evlist__for_each_entry(evlist, evsel) {
		if (strstarts(evsel->name, "vpa_dtl")) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("No selected events with VPA trace data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.sample_type = evsel->core.attr.sample_type;
	attr.sample_id_all = evsel->core.attr.sample_id_all;
	attr.type = PERF_TYPE_SYNTH;
	attr.config = PERF_SYNTH_POWERPC_VPA_DTL;

	/* create new id val to be a fixed offset from evsel id */
	id = evsel->core.id[0] + 1000000000;
	if (!id)
		id = 1;

	err = perf_session__deliver_synth_attr_event(session, &attr, id);
	if (err)
		return err;

	vpa->sample_id = id;
	set_event_name(evlist, id, "vpa-dtl");

	return 0;
}

/*
 * Process the PERF_RECORD_AUXTRACE_INFO records and setup
 * the infrastructure to process auxtrace events. PERF_RECORD_AUXTRACE_INFO
 * is processed first since it is of type perf_user_event_type.
 * Initialise the aux buffer queues using auxtrace_queues__init().
 * auxtrace_queue is created for each CPU.
 */
int powerpc_vpadtl_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session)
{
	struct perf_record_auxtrace_info *auxtrace_info = &event->auxtrace_info;
	size_t min_sz = sizeof(u64) * POWERPC_VPADTL_TYPE;
	struct powerpc_vpadtl *vpa;
	int err;

	if (auxtrace_info->header.size < sizeof(struct perf_record_auxtrace_info) +
					min_sz)
		return -EINVAL;

	vpa = zalloc(sizeof(struct powerpc_vpadtl));
	if (!vpa)
		return -ENOMEM;

	err = auxtrace_queues__init(&vpa->queues);
	if (err)
		goto err_free;

	vpa->session = session;
	vpa->machine = &session->machines.host; /* No kvm support */
	vpa->auxtrace_type = auxtrace_info->type;
	vpa->pmu_type = auxtrace_info->priv[POWERPC_VPADTL_TYPE];

	vpa->auxtrace.process_event = powerpc_vpadtl_process_event;
	vpa->auxtrace.process_auxtrace_event = powerpc_vpadtl_process_auxtrace_event;
	vpa->auxtrace.flush_events = powerpc_vpadtl_flush;
	vpa->auxtrace.free_events = powerpc_vpadtl_free_events;
	vpa->auxtrace.free = powerpc_vpadtl_free;
	session->auxtrace = &vpa->auxtrace;

	powerpc_vpadtl_print_info(&auxtrace_info->priv[0]);

	if (dump_trace)
		return 0;

	err = powerpc_vpadtl_synth_events(vpa, session);
	if (err)
		goto err_free_queues;

	err = auxtrace_queues__process_index(&vpa->queues, session);
	if (err)
		goto err_free_queues;

	return 0;

err_free_queues:
	auxtrace_queues__free(&vpa->queues);
	session->auxtrace = NULL;

err_free:
	free(vpa);
	return err;
}
