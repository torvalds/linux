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

static char		const *input_name = "perf.data";
static int		input;
static unsigned long	page_size;
static unsigned long	mmap_window = 32;

static unsigned long	total = 0;
static unsigned long	total_comm = 0;

static struct rb_root	threads;
static struct thread	*last_match;

static struct perf_header *header;
static u64		sample_type;


static int
process_comm_event(event_t *event, unsigned long offset, unsigned long head)
{
	struct thread *thread;

	thread = threads__findnew(event->comm.pid, &threads, &last_match);

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
	char level;
	int show = 0;
	struct dso *dso = NULL;
	struct thread *thread;
	u64 ip = event->ip.ip;
	u64 timestamp = -1;
	u32 cpu = -1;
	u64 period = 1;
	void *more_data = event->ip.__more_data;
	int cpumode;

	thread = threads__findnew(event->ip.pid, &threads, &last_match);

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

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (thread == NULL) {
		eprintf("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	if (cpumode == PERF_RECORD_MISC_KERNEL) {
		show = SHOW_KERNEL;
		level = 'k';

		dso = kernel_dso;

		dump_printf(" ...... dso: %s\n", dso->name);

	} else if (cpumode == PERF_RECORD_MISC_USER) {

		show = SHOW_USER;
		level = '.';

	} else {
		show = SHOW_HV;
		level = 'H';

		dso = hypervisor_dso;

		dump_printf(" ...... dso: [hypervisor]\n");
	}

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

static int
process_event(event_t *event, unsigned long offset, unsigned long head)
{
	trace_event(event);

	switch (event->header.type) {
	case PERF_RECORD_MMAP ... PERF_RECORD_LOST:
		return 0;

	case PERF_RECORD_COMM:
		return process_comm_event(event, offset, head);

	case PERF_RECORD_EXIT ... PERF_RECORD_READ:
		return 0;

	case PERF_RECORD_SAMPLE:
		return process_sample_event(event, offset, head);

	case PERF_RECORD_MAX:
	default:
		return -1;
	}

	return 0;
}

static int __cmd_trace(void)
{
	int ret, rc = EXIT_FAILURE;
	unsigned long offset = 0;
	unsigned long head = 0;
	struct stat perf_stat;
	event_t *event;
	uint32_t size;
	char *buf;

	trace_report();
	register_idle_thread(&threads, &last_match);

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		perror("failed to open file");
		exit(-1);
	}

	ret = fstat(input, &perf_stat);
	if (ret < 0) {
		perror("failed to stat file");
		exit(-1);
	}

	if (!perf_stat.st_size) {
		fprintf(stderr, "zero-sized file, nothing to do!\n");
		exit(0);
	}
	header = perf_header__read(input);
	head = header->data_offset;
	sample_type = perf_header__sample_type(header);

	if (!(sample_type & PERF_SAMPLE_RAW))
		die("No trace sample to read. Did you call perf record "
		    "without -R?");

	if (load_kernel() < 0) {
		perror("failed to load kernel symbols");
		return EXIT_FAILURE;
	}

remap:
	buf = (char *)mmap(NULL, page_size * mmap_window, PROT_READ,
			   MAP_SHARED, input, offset);
	if (buf == MAP_FAILED) {
		perror("failed to mmap file");
		exit(-1);
	}

more:
	event = (event_t *)(buf + head);

	if (head + event->header.size >= page_size * mmap_window) {
		unsigned long shift = page_size * (head / page_size);
		int res;

		res = munmap(buf, page_size * mmap_window);
		assert(res == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;

	if (!size || process_event(event, offset, head) < 0) {

		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */

		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}

	head += size;

	if (offset + head < (unsigned long)perf_stat.st_size)
		goto more;

	rc = EXIT_SUCCESS;
	close(input);

	return rc;
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
	OPT_END()
};

int cmd_trace(int argc, const char **argv, const char *prefix __used)
{
	symbol__init();
	page_size = getpagesize();

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
