// SPDX-License-Identifier: GPL-2.0
/*
 * VPA DTL PMU support
 */

#include <inttypes.h>
#include "color.h"
#include "evlist.h"
#include "session.h"
#include "auxtrace.h"
#include "data.h"
#include "machine.h"
#include "debug.h"
#include "powerpc-vpadtl.h"

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

static int powerpc_vpadtl_process_event(struct perf_session *session __maybe_unused,
				 union perf_event *event __maybe_unused,
				 struct perf_sample *sample __maybe_unused,
				 const struct perf_tool *tool __maybe_unused)
{
	return 0;
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

	return 0;

err_free:
	free(vpa);
	return err;
}
