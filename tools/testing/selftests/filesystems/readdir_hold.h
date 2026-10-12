/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hold a readdir of a directory in the page fault of its buffer and see
 * whether a create and a lookup in that directory wait for it. For a
 * directory that is permanently empty they must not.
 */
#ifndef __SELFTESTS_READDIR_HOLD_H
#define __SELFTESTS_READDIR_HOLD_H

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#define HOLD_FAULT_MS	5000	/* for the reader to reach the fault */
#define HOLD_QUEUE_MS	2000	/* for the create to queue up behind it */
#define HOLD_LOOKUP_MS	5000	/* for the lookup to come back */

struct readdir_hold {
	int uffd;
	int taskdir;		/* /proc/self/task, from before any namespace change */
	long page_size;
	char *page;		/* faults until released */
	int dfd;
	pid_t creator_tid;
	int created;
	int done[2];		/* the finder writes a byte when it is back */
};

/*
 * The fault happens in the kernel, so the userfaultfd needs CAP_SYS_PTRACE
 * in the initial user namespace or vm.unprivileged_userfaultfd. Call this
 * before entering a user namespace.
 */
static inline int readdir_hold_init(struct readdir_hold *h)
{
	struct uffdio_api api = { .api = UFFD_API };

	h->page = MAP_FAILED;
	h->page_size = sysconf(_SC_PAGESIZE);
	h->taskdir = open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	h->uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (h->uffd < 0 || h->taskdir < 0 || ioctl(h->uffd, UFFDIO_API, &api)) {
		if (h->uffd >= 0)
			close(h->uffd);
		if (h->taskdir >= 0)
			close(h->taskdir);
		h->uffd = h->taskdir = -1;
		return -1;
	}
	return 0;
}

static inline void readdir_hold_destroy(struct readdir_hold *h)
{
	if (h->uffd >= 0)
		close(h->uffd);
	if (h->taskdir >= 0)
		close(h->taskdir);
	h->uffd = h->taskdir = -1;
}

static inline void *readdir_hold_reader(void *arg)
{
	struct readdir_hold *h = arg;

	/* the first byte written to the buffer faults until released */
	syscall(__NR_getdents64, h->dfd, h->page, h->page_size);
	return NULL;
}

static inline void *readdir_hold_creator(void *arg)
{
	struct readdir_hold *h = arg;

	h->creator_tid = syscall(__NR_gettid);
	/* takes the directory lock exclusive before it fails */
	mkdirat(h->dfd, "x", 0755);
	__atomic_store_n(&h->created, 1, __ATOMIC_RELEASE);
	return NULL;
}

static inline void *readdir_hold_finder(void *arg)
{
	struct readdir_hold *h = arg;
	int fd;

	/* a lookup that misses the dcache takes the lock shared */
	fd = openat(h->dfd, "no_such_name", O_RDONLY | O_CLOEXEC);
	if (fd >= 0)
		close(fd);
	if (write(h->done[1], "x", 1) != 1)
		perror("readdir_hold: finder");
	return NULL;
}

/* the creator is back, or waits in the kernel for the lock */
static inline bool readdir_hold_creator_settled(struct readdir_hold *h)
{
	char path[32], buf[256], *p;
	ssize_t n;
	int fd;

	if (__atomic_load_n(&h->created, __ATOMIC_ACQUIRE))
		return true;
	if (!h->creator_tid)
		return false;
	snprintf(path, sizeof(path), "%d/stat", h->creator_tid);
	fd = openat(h->taskdir, path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return false;
	buf[n] = 0;
	/* "pid (comm) state ..." */
	p = strrchr(buf, ')');
	return p && p[1] == ' ' && p[2] == 'D';
}

static inline bool readdir_hold_faulted(struct readdir_hold *h)
{
	struct pollfd pfd = { .fd = h->uffd, .events = POLLIN };
	struct uffd_msg msg;

	if (poll(&pfd, 1, HOLD_FAULT_MS) != 1)
		return false;
	if (read(h->uffd, &msg, sizeof(msg)) != sizeof(msg))
		return false;
	return msg.event == UFFD_EVENT_PAGEFAULT;
}

/* let the reader go on */
static inline void readdir_hold_release(struct readdir_hold *h)
{
	struct uffdio_copy cp = {
		.dst = (unsigned long)h->page,
		.len = h->page_size,
	};
	void *zero;

	zero = calloc(1, h->page_size);
	if (!zero)
		return;
	cp.src = (unsigned long)zero;
	if (ioctl(h->uffd, UFFDIO_COPY, &cp) && errno != EEXIST)
		perror("readdir_hold: UFFDIO_COPY");
	free(zero);
}

/*
 * A readdir of @dfd that sticks in the fault of its buffer, then a create
 * and a lookup in @dfd. Whether the lookup came back while the readdir was
 * still stuck goes to @stalled. Returns -1 when that could not be found
 * out.
 */
static inline int readdir_hold_check(struct readdir_hold *h, int dfd,
				     bool *stalled)
{
	pthread_t reader, creator, finder;
	struct uffdio_register reg = {};
	struct pollfd pfd;
	int ret = -1, i;

	h->dfd = dfd;
	h->created = 0;
	h->creator_tid = 0;
	h->page = mmap(NULL, h->page_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (h->page == MAP_FAILED)
		return -1;
	reg.range.start = (unsigned long)h->page;
	reg.range.len = h->page_size;
	reg.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(h->uffd, UFFDIO_REGISTER, &reg) || pipe2(h->done, O_CLOEXEC))
		goto out_page;

	if (pthread_create(&reader, NULL, readdir_hold_reader, h))
		goto out_pipe;
	if (!readdir_hold_faulted(h))
		goto out_reader;
	if (pthread_create(&creator, NULL, readdir_hold_creator, h))
		goto out_reader;
	for (i = 0; i < HOLD_QUEUE_MS / 10 && !readdir_hold_creator_settled(h); i++)
		usleep(10000);
	if (pthread_create(&finder, NULL, readdir_hold_finder, h))
		goto out_creator;

	pfd.fd = h->done[0];
	pfd.events = POLLIN;
	*stalled = poll(&pfd, 1, HOLD_LOOKUP_MS) != 1;
	ret = 0;

	readdir_hold_release(h);
	pthread_join(finder, NULL);
out_creator:
	if (ret)
		readdir_hold_release(h);
	pthread_join(creator, NULL);
out_reader:
	if (ret)
		readdir_hold_release(h);
	pthread_join(reader, NULL);
out_pipe:
	close(h->done[0]);
	close(h->done[1]);
out_page:
	munmap(h->page, h->page_size);
	h->page = MAP_FAILED;
	return ret;
}

#endif /* __SELFTESTS_READDIR_HOLD_H */
