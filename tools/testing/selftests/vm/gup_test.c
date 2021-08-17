#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <assert.h>
#include "../../../../mm/gup_test.h"

#define MB (1UL << 20)
#define PAGE_SIZE sysconf(_SC_PAGESIZE)

/* Just the flags we need, copied from mm.h: */
#define FOLL_WRITE	0x01	/* check pte is writable */
#define FOLL_TOUCH	0x02	/* mark page accessed */

static unsigned long cmd = GUP_FAST_BENCHMARK;
static int gup_fd, repeats = 1;
static unsigned long size = 128 * MB;
/* Serialize prints */
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *cmd_to_str(unsigned long cmd)
{
	switch (cmd) {
	case GUP_FAST_BENCHMARK:
		return "GUP_FAST_BENCHMARK";
	case PIN_FAST_BENCHMARK:
		return "PIN_FAST_BENCHMARK";
	case PIN_LONGTERM_BENCHMARK:
		return "PIN_LONGTERM_BENCHMARK";
	case GUP_BASIC_TEST:
		return "GUP_BASIC_TEST";
	case PIN_BASIC_TEST:
		return "PIN_BASIC_TEST";
	case DUMP_USER_PAGES_TEST:
		return "DUMP_USER_PAGES_TEST";
	}
	return "Unknown command";
}

void *gup_thread(void *data)
{
	struct gup_test gup = *(struct gup_test *)data;
	int i;

	/* Only report timing information on the *_BENCHMARK commands: */
	if ((cmd == PIN_FAST_BENCHMARK) || (cmd == GUP_FAST_BENCHMARK) ||
	     (cmd == PIN_LONGTERM_BENCHMARK)) {
		for (i = 0; i < repeats; i++) {
			gup.size = size;
			if (ioctl(gup_fd, cmd, &gup))
				perror("ioctl"), exit(1);

			pthread_mutex_lock(&print_mutex);
			printf("%s: Time: get:%lld put:%lld us",
			       cmd_to_str(cmd), gup.get_delta_usec,
			       gup.put_delta_usec);
			if (gup.size != size)
				printf(", truncated (size: %lld)", gup.size);
			printf("\n");
			pthread_mutex_unlock(&print_mutex);
		}
	} else {
		gup.size = size;
		if (ioctl(gup_fd, cmd, &gup)) {
			perror("ioctl");
			exit(1);
		}

		pthread_mutex_lock(&print_mutex);
		printf("%s: done\n", cmd_to_str(cmd));
		if (gup.size != size)
			printf("Truncated (size: %lld)\n", gup.size);
		pthread_mutex_unlock(&print_mutex);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct gup_test gup = { 0 };
	int filed, i, opt, nr_pages = 1, thp = -1, write = 1, nthreads = 1, ret;
	int flags = MAP_PRIVATE, touch = 0;
	char *file = "/dev/zero";
	pthread_t *tid;
	char *p;

	while ((opt = getopt(argc, argv, "m:r:n:F:f:abcj:tTLUuwWSHpz")) != -1) {
		switch (opt) {
		case 'a':
			cmd = PIN_FAST_BENCHMARK;
			break;
		case 'b':
			cmd = PIN_BASIC_TEST;
			break;
		case 'L':
			cmd = PIN_LONGTERM_BENCHMARK;
			break;
		case 'c':
			cmd = DUMP_USER_PAGES_TEST;
			/*
			 * Dump page 0 (index 1). May be overridden later, by
			 * user's non-option arguments.
			 *
			 * .which_pages is zero-based, so that zero can mean "do
			 * nothing".
			 */
			gup.which_pages[0] = 1;
			break;
		case 'p':
			/* works only with DUMP_USER_PAGES_TEST */
			gup.test_flags |= GUP_TEST_FLAG_DUMP_PAGES_USE_PIN;
			break;
		case 'F':
			/* strtol, so you can pass flags in hex form */
			gup.gup_flags = strtol(optarg, 0, 0);
			break;
		case 'j':
			nthreads = atoi(optarg);
			break;
		case 'm':
			size = atoi(optarg) * MB;
			break;
		case 'r':
			repeats = atoi(optarg);
			break;
		case 'n':
			nr_pages = atoi(optarg);
			break;
		case 't':
			thp = 1;
			break;
		case 'T':
			thp = 0;
			break;
		case 'U':
			cmd = GUP_BASIC_TEST;
			break;
		case 'u':
			cmd = GUP_FAST_BENCHMARK;
			break;
		case 'w':
			write = 1;
			break;
		case 'W':
			write = 0;
			break;
		case 'f':
			file = optarg;
			break;
		case 'S':
			flags &= ~MAP_PRIVATE;
			flags |= MAP_SHARED;
			break;
		case 'H':
			flags |= (MAP_HUGETLB | MAP_ANONYMOUS);
			break;
		case 'z':
			/* fault pages in gup, do not fault in userland */
			touch = 1;
			break;
		default:
			return -1;
		}
	}

	if (optind < argc) {
		int extra_arg_count = 0;
		/*
		 * For example:
		 *
		 *   ./gup_test -c 0 1 0x1001
		 *
		 * ...to dump pages 0, 1, and 4097
		 */

		while ((optind < argc) &&
		       (extra_arg_count < GUP_TEST_MAX_PAGES_TO_DUMP)) {
			/*
			 * Do the 1-based indexing here, so that the user can
			 * use normal 0-based indexing on the command line.
			 */
			long page_index = strtol(argv[optind], 0, 0) + 1;

			gup.which_pages[extra_arg_count] = page_index;
			extra_arg_count++;
			optind++;
		}
	}

	filed = open(file, O_RDWR|O_CREAT);
	if (filed < 0) {
		perror("open");
		exit(filed);
	}

	gup.nr_pages_per_call = nr_pages;
	if (write)
		gup.gup_flags |= FOLL_WRITE;

	gup_fd = open("/sys/kernel/debug/gup_test", O_RDWR);
	if (gup_fd == -1) {
		perror("open");
		exit(1);
	}

	p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, filed, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	gup.addr = (unsigned long)p;

	if (thp == 1)
		madvise(p, size, MADV_HUGEPAGE);
	else if (thp == 0)
		madvise(p, size, MADV_NOHUGEPAGE);

	/*
	 * FOLL_TOUCH, in gup_test, is used as an either/or case: either
	 * fault pages in from the kernel via FOLL_TOUCH, or fault them
	 * in here, from user space. This allows comparison of performance
	 * between those two cases.
	 */
	if (touch) {
		gup.gup_flags |= FOLL_TOUCH;
	} else {
		for (; (unsigned long)p < gup.addr + size; p += PAGE_SIZE)
			p[0] = 0;
	}

	tid = malloc(sizeof(pthread_t) * nthreads);
	assert(tid);
	for (i = 0; i < nthreads; i++) {
		ret = pthread_create(&tid[i], NULL, gup_thread, &gup);
		assert(ret == 0);
	}
	for (i = 0; i < nthreads; i++) {
		ret = pthread_join(tid[i], NULL);
		assert(ret == 0);
	}
	free(tid);

	return 0;
}
