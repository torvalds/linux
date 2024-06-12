#define __SANE_USERSPACE_TYPES__ // Use ll64
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <assert.h>
#include <mm/gup_test.h>
#include "../kselftest.h"
#include "vm_util.h"

#define MB (1UL << 20)

/* Just the flags we need, copied from mm.h: */
#define FOLL_WRITE	0x01	/* check pte is writable */
#define FOLL_TOUCH	0x02	/* mark page accessed */

#define GUP_TEST_FILE "/sys/kernel/debug/gup_test"

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
	int i, status;

	/* Only report timing information on the *_BENCHMARK commands: */
	if ((cmd == PIN_FAST_BENCHMARK) || (cmd == GUP_FAST_BENCHMARK) ||
	     (cmd == PIN_LONGTERM_BENCHMARK)) {
		for (i = 0; i < repeats; i++) {
			gup.size = size;
			status = ioctl(gup_fd, cmd, &gup);
			if (status)
				break;

			pthread_mutex_lock(&print_mutex);
			ksft_print_msg("%s: Time: get:%lld put:%lld us",
				       cmd_to_str(cmd), gup.get_delta_usec,
				       gup.put_delta_usec);
			if (gup.size != size)
				ksft_print_msg(", truncated (size: %lld)", gup.size);
			ksft_print_msg("\n");
			pthread_mutex_unlock(&print_mutex);
		}
	} else {
		gup.size = size;
		status = ioctl(gup_fd, cmd, &gup);
		if (status)
			goto return_;

		pthread_mutex_lock(&print_mutex);
		ksft_print_msg("%s: done\n", cmd_to_str(cmd));
		if (gup.size != size)
			ksft_print_msg("Truncated (size: %lld)\n", gup.size);
		pthread_mutex_unlock(&print_mutex);
	}

return_:
	ksft_test_result(!status, "ioctl status %d\n", status);
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
			ksft_exit_fail_msg("Wrong argument\n");
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

	ksft_print_header();
	ksft_set_plan(nthreads);

	filed = open(file, O_RDWR|O_CREAT, 0664);
	if (filed < 0)
		ksft_exit_fail_msg("Unable to open %s: %s\n", file, strerror(errno));

	gup.nr_pages_per_call = nr_pages;
	if (write)
		gup.gup_flags |= FOLL_WRITE;

	gup_fd = open(GUP_TEST_FILE, O_RDWR);
	if (gup_fd == -1) {
		switch (errno) {
		case EACCES:
			if (getuid())
				ksft_print_msg("Please run this test as root\n");
			break;
		case ENOENT:
			if (opendir("/sys/kernel/debug") == NULL)
				ksft_print_msg("mount debugfs at /sys/kernel/debug\n");
			ksft_print_msg("check if CONFIG_GUP_TEST is enabled in kernel config\n");
			break;
		default:
			ksft_print_msg("failed to open %s: %s\n", GUP_TEST_FILE, strerror(errno));
			break;
		}
		ksft_test_result_skip("Please run this test as root\n");
		ksft_exit_pass();
	}

	p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, filed, 0);
	if (p == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));
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
		for (; (unsigned long)p < gup.addr + size; p += psize())
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

	ksft_exit_pass();
}
