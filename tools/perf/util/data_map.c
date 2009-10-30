#include "data_map.h"
#include "symbol.h"
#include "util.h"
#include "debug.h"


static struct perf_file_handler *curr_handler;
static unsigned long	mmap_window = 32;
static char		__cwd[PATH_MAX];

static int
process_event_stub(event_t *event __used,
		   unsigned long offset __used,
		   unsigned long head __used)
{
	return 0;
}

void register_perf_file_handler(struct perf_file_handler *handler)
{
	if (!handler->process_sample_event)
		handler->process_sample_event = process_event_stub;
	if (!handler->process_mmap_event)
		handler->process_mmap_event = process_event_stub;
	if (!handler->process_comm_event)
		handler->process_comm_event = process_event_stub;
	if (!handler->process_fork_event)
		handler->process_fork_event = process_event_stub;
	if (!handler->process_exit_event)
		handler->process_exit_event = process_event_stub;
	if (!handler->process_lost_event)
		handler->process_lost_event = process_event_stub;
	if (!handler->process_read_event)
		handler->process_read_event = process_event_stub;
	if (!handler->process_throttle_event)
		handler->process_throttle_event = process_event_stub;
	if (!handler->process_unthrottle_event)
		handler->process_unthrottle_event = process_event_stub;

	curr_handler = handler;
}

static int
process_event(event_t *event, unsigned long offset, unsigned long head)
{
	trace_event(event);

	switch (event->header.type) {
	case PERF_RECORD_SAMPLE:
		return curr_handler->process_sample_event(event, offset, head);
	case PERF_RECORD_MMAP:
		return curr_handler->process_mmap_event(event, offset, head);
	case PERF_RECORD_COMM:
		return curr_handler->process_comm_event(event, offset, head);
	case PERF_RECORD_FORK:
		return curr_handler->process_fork_event(event, offset, head);
	case PERF_RECORD_EXIT:
		return curr_handler->process_exit_event(event, offset, head);
	case PERF_RECORD_LOST:
		return curr_handler->process_lost_event(event, offset, head);
	case PERF_RECORD_READ:
		return curr_handler->process_read_event(event, offset, head);
	case PERF_RECORD_THROTTLE:
		return curr_handler->process_throttle_event(event, offset, head);
	case PERF_RECORD_UNTHROTTLE:
		return curr_handler->process_unthrottle_event(event, offset, head);
	default:
		curr_handler->total_unknown++;
		return -1;
	}
}

int mmap_dispatch_perf_file(struct perf_header **pheader,
			    const char *input_name,
			    int force,
			    int full_paths,
			    int *cwdlen,
			    char **cwd)
{
	int ret, rc = EXIT_FAILURE;
	struct perf_header *header;
	unsigned long head, shift;
	unsigned long offset = 0;
	struct stat input_stat;
	size_t	page_size;
	u64 sample_type;
	event_t *event;
	uint32_t size;
	int input;
	char *buf;

	if (!curr_handler)
		die("Forgot to register perf file handler");

	page_size = getpagesize();

	input = open(input_name, O_RDONLY);
	if (input < 0) {
		fprintf(stderr, " failed to open file: %s", input_name);
		if (!strcmp(input_name, "perf.data"))
			fprintf(stderr, "  (try 'perf record' first)");
		fprintf(stderr, "\n");
		exit(-1);
	}

	ret = fstat(input, &input_stat);
	if (ret < 0) {
		perror("failed to stat file");
		exit(-1);
	}

	if (!force && input_stat.st_uid && (input_stat.st_uid != geteuid())) {
		fprintf(stderr, "file: %s not owned by current user or root\n",
			input_name);
		exit(-1);
	}

	if (!input_stat.st_size) {
		fprintf(stderr, "zero-sized file, nothing to do!\n");
		exit(0);
	}

	*pheader = perf_header__read(input);
	header = *pheader;
	head = header->data_offset;

	sample_type = perf_header__sample_type(header);

	if (curr_handler->sample_type_check)
		if (curr_handler->sample_type_check(sample_type) < 0)
			exit(-1);

	if (load_kernel(NULL) < 0) {
		perror("failed to load kernel symbols");
		return EXIT_FAILURE;
	}

	if (!full_paths) {
		if (getcwd(__cwd, sizeof(__cwd)) == NULL) {
			perror("failed to get the current directory");
			return EXIT_FAILURE;
		}
		*cwd = __cwd;
		*cwdlen = strlen(*cwd);
	} else {
		*cwd = NULL;
		*cwdlen = 0;
	}

	shift = page_size * (head / page_size);
	offset += shift;
	head -= shift;

remap:
	buf = (char *)mmap(NULL, page_size * mmap_window, PROT_READ,
			   MAP_SHARED, input, offset);
	if (buf == MAP_FAILED) {
		perror("failed to mmap file");
		exit(-1);
	}

more:
	event = (event_t *)(buf + head);

	size = event->header.size;
	if (!size)
		size = 8;

	if (head + event->header.size >= page_size * mmap_window) {
		int munmap_ret;

		shift = page_size * (head / page_size);

		munmap_ret = munmap(buf, page_size * mmap_window);
		assert(munmap_ret == 0);

		offset += shift;
		head -= shift;
		goto remap;
	}

	size = event->header.size;

	dump_printf("\n%p [%p]: event: %d\n",
			(void *)(offset + head),
			(void *)(long)event->header.size,
			event->header.type);

	if (!size || process_event(event, offset, head) < 0) {

		dump_printf("%p [%p]: skipping unknown header type: %d\n",
			(void *)(offset + head),
			(void *)(long)(event->header.size),
			event->header.type);

		/*
		 * assume we lost track of the stream, check alignment, and
		 * increment a single u64 in the hope to catch on again 'soon'.
		 */

		if (unlikely(head & 7))
			head &= ~7ULL;

		size = 8;
	}

	head += size;

	if (offset + head >= header->data_offset + header->data_size)
		goto done;

	if (offset + head < (unsigned long)input_stat.st_size)
		goto more;

done:
	rc = EXIT_SUCCESS;
	close(input);

	return rc;
}


