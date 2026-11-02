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
#include <stdbool.h>
#include <string.h>
#include <mm/gup_test.h>
#include <mm/hugepage_settings.h>

#define MB (1UL << 20)

/* Just the flags we need, copied from the kernel internals. */
#define FOLL_WRITE	0x01	/* check pte is writable */

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
	}
	return "Unknown command";
}

void *gup_thread(void *data)
{
	struct gup_test gup = *(struct gup_test *)data;
	int i, status;

	for (i = 0; i < repeats; i++) {
		gup.size = size;
		status = ioctl(gup_fd, cmd, &gup);
		if (status) {
			int err = errno;

			pthread_mutex_lock(&print_mutex);
			fprintf(stderr, "%s ioctl failed: %s\n", cmd_to_str(cmd),
				strerror(err));
			pthread_mutex_unlock(&print_mutex);
			return data;
		}

		pthread_mutex_lock(&print_mutex);
		printf("%s: Time: get:%lld put:%lld us",
			cmd_to_str(cmd), gup.get_delta_usec,
			gup.put_delta_usec);
		if (gup.size != size)
			printf(", truncated (size: %lld)", gup.size);
		printf("\n");
		pthread_mutex_unlock(&print_mutex);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct gup_test gup = { 0 };
	int filed, i, opt, nr_pages = 1, thp = -1, write = 1, nthreads = 1, ret;
	int flags = MAP_PRIVATE, started_threads = 0, exit_status = 1;
	char *file = "/dev/zero";
	bool hugetlb = false, thread_error = false;
	void *thread_result;
	pthread_t *tid;
	char *p;

	while ((opt = getopt(argc, argv, "m:r:n:F:f:aj:tTLuwWSH")) != -1) {
		switch (opt) {
		case 'a':
			cmd = PIN_FAST_BENCHMARK;
			break;
		case 'L':
			cmd = PIN_LONGTERM_BENCHMARK;
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
			if (nr_pages < 0)
				nr_pages = size / getpagesize();
			break;
		case 't':
			thp = 1;
			break;
		case 'T':
			thp = 0;
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
			hugetlb = true;
			break;
		default:
			fprintf(stderr, "Wrong argument\n");
			exit(1);
		}
	}

	if (optind != argc) {
		fprintf(stderr, "Unexpected argument '%s'\n", argv[optind]);
		exit(1);
	}

	if (geteuid()) {
		fprintf(stderr, "Please run this test as root\n");
		exit(1);
	}

	if (hugetlb) {
		unsigned long hp_size = default_huge_page_size();

		if (!hp_size) {
			fprintf(stderr, "Could not determine huge page size\n");
			return 1;
		}

		size = (size + hp_size - 1) & ~(hp_size - 1);
		if (!hugetlb_setup_default(size / hp_size)) {
			fprintf(stderr, "Not enough huge pages\n");
			return 1;
		}
	}

	filed = open(file, O_RDWR|O_CREAT, 0664);
	if (filed < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", file, strerror(errno));
		return 1;
	}

	gup.nr_pages_per_call = nr_pages;
	if (write)
		gup.gup_flags |= FOLL_WRITE;

	gup_fd = open(GUP_TEST_FILE, O_RDWR);
	if (gup_fd == -1) {
		switch (errno) {
		case ENOENT:
			if (opendir("/sys/kernel/debug") == NULL)
				fprintf(stderr, "mount debugfs at /sys/kernel/debug\n");
			fprintf(stderr, "check if CONFIG_GUP_TEST is enabled in kernel config\n");
			break;
		default:
			fprintf(stderr, "failed to open %s: %s\n", GUP_TEST_FILE,
				strerror(errno));
			break;
		}
		goto err_close_filed;
	}

	p = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, filed, 0);
	if (p == MAP_FAILED) {
		fprintf(stderr, "mmap: %s\n", strerror(errno));
		goto err_close_gup_fd;
	}
	gup.addr = (unsigned long)p;

	if (thp == 1)
		madvise(p, size, MADV_HUGEPAGE);
	else if (thp == 0)
		madvise(p, size, MADV_NOHUGEPAGE);

	/* Fault them in here, from user space. */
	for (; (unsigned long)p < gup.addr + size; p += getpagesize())
		p[0] = 0;

	tid = malloc(sizeof(pthread_t) * nthreads);
	if (!tid) {
		fprintf(stderr, "Failed to allocate %d threads: %s\n",
			nthreads, strerror(errno));
		goto err_unmap;
	}

	for (i = 0; i < nthreads; i++) {
		ret = pthread_create(&tid[i], NULL, gup_thread, &gup);
		if (ret) {
			fprintf(stderr, "pthread_create failed: %s\n", strerror(ret));
			thread_error = true;
			break;
		}
		started_threads++;
	}
	for (i = 0; i < started_threads; i++) {
		ret = pthread_join(tid[i], &thread_result);
		if (ret) {
			fprintf(stderr, "pthread_join failed: %s\n", strerror(ret));
			thread_error = true;
		} else if (thread_result)
			thread_error = true;
	}

	free(tid);
	if (!thread_error)
		exit_status = 0;

err_unmap:
	munmap((void *)gup.addr, size);
err_close_gup_fd:
	close(gup_fd);
err_close_filed:
	close(filed);
	return exit_status;
}
