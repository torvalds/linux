#include "builtin.h"

#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"

#include "util/parse-options.h"

#include "perf.h"
#include "util/debug.h"

#include "util/trace-event.h"
#include "util/data_map.h"

static char		const *input_name = "perf.data";

static unsigned long	total = 0;
static unsigned long	total_comm = 0;

static struct perf_header *header;
static u64		sample_type;

static char		*cwd;
static int		cwdlen;


static int
process_comm_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread = threads__findnew(event->comm.pid);

	dump_printf("%p [%p]: PERF_RECORD_COMM: %s:%d\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->comm.comm, event->comm.pid);

	if (thread == NULL ||
	    thread__set_comm(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
		return -1;
	}
	total_comm++;

	return 0;
}

static int
process_sample_event(event_t *event, unsigned long offset, unsigned long head)
{
	u64 ip = event->ip.ip;
	u64 timestamp = -1;
	u32 cpu = -1;
	u64 period = 1;
	void *more_data = event->ip.__more_data;
	struct thread *thread = threads__findnew(event->ip.pid);

	if (sample_type & PERF_SAMPLE_TIME) {
		timestamp = *(u64 *)more_data;
		more_data += sizeof(u64);
	}

	if (sample_type & PERF_SAMPLE_CPU) {
		cpu = *(u32 *)more_data;
		more_data += sizeof(u32);
		more_data += sizeof(u32); /* reserved */
	}

	if (sample_type & PERF_SAMPLE_PERIOD) {
		period = *(u64 *)more_data;
		more_data += sizeof(u64);
	}

	dump_printf("%p [%p]: PERF_RECORD_SAMPLE (IP, %d): %d/%d: %p period: %Ld\n",
		(void *)(offset + head),
		(void *)(long)(event->header.size),
		event->header.misc,
		event->ip.pid, event->ip.tid,
		(void *)(long)ip,
		(long long)period);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (sample_type & PERF_SAMPLE_RAW) {
		struct {
			u32 size;
			char data[0];
		} *raw = more_data;

		/*
		 * FIXME: better resolve from pid from the struct trace_entry
		 * field, although it should be the same than this perf
		 * event pid
		 */
		print_event(cpu, raw->data, raw->size, timestamp, thread->comm);
	}
	total += period;

	return 0;
}

static int sample_type_check(u64 type)
{
	sample_type = type;

	if (!(sample_type & PERF_SAMPLE_RAW)) {
		fprintf(stderr,
			"No trace sample to read. Did you call perf record "
			"without -R?");
		return -1;
	}

	return 0;
}

static struct perf_file_handler file_handler = {
	.process_sample_event	= process_sample_event,
	.process_comm_event	= process_comm_event,
	.sample_type_check	= sample_type_check,
};

static int __cmd_trace(void)
{
	register_idle_thread();
	register_perf_file_handler(&file_handler);

	return mmap_dispatch_perf_file(&header, input_name, 0, 0, &cwdlen, &cwd);
}

static const char * const annotate_usage[] = {
	"perf trace [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('l', "latency", &latency_format,
		    "show latency attributes (irqs/preemption disabled, etc)"),
	OPT_END()
};

int cmd_trace(int argc, const char **argv, const char *prefix __used)
{
	symbol__init(0);

	argc = parse_options(argc, argv, options, annotate_usage, 0);
	if (argc) {
		/*
		 * Special case: if there's an argument left then assume tha
		 * it's a symbol filter:
		 */
		if (argc > 1)
			usage_with_options(annotate_usage, options);
	}

	setup_pager();

	return __cmd_trace();
}
