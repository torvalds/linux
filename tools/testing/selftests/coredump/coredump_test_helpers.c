// SPDX-License-Identifier: GPL-2.0

#include <assert.h>
#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <link.h>
#include <linux/stddef.h>
#include <linux/io_uring.h>
#include <linux/swab.h>
#include <linux/coredump.h>
#include <linux/fs.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../filesystems/wrappers.h"
#include "../pidfd/pidfd.h"
#include "coredump_notify_signal.h"

/* Forward declarations to avoid including harness header */
struct __test_metadata;

/* Match the fixture definition from coredump_test.h */
struct _fixture_coredump_data {
	char original_core_pattern[256];
	pid_t pid_coredump_server;
	int fd_tmpfs_detached;
};

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define NUM_THREAD_SPAWN 128

void *do_nothing(void *arg)
{
	(void)arg;
	while (1)
		pause();

	return NULL;
}

void crashing_child(void)
{
	pthread_t thread;
	int i;

	for (i = 0; i < NUM_THREAD_SPAWN; ++i)
		pthread_create(&thread, NULL, do_nothing, NULL);

	/* crash on purpose */
	i = *(volatile int *)NULL;
}

int create_detached_tmpfs(void)
{
	int fd_context, fd_tmpfs;

	fd_context = sys_fsopen("tmpfs", 0);
	if (fd_context < 0)
		return -1;

	if (sys_fsconfig(fd_context, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0)
		return -1;

	fd_tmpfs = sys_fsmount(fd_context, 0, 0);
	close(fd_context);
	return fd_tmpfs;
}

int create_and_listen_unix_socket(const char *path)
{
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	assert(strlen(path) < sizeof(addr.sun_path) - 1);
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	size_t addr_len =
		offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
	int fd, ret;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		goto out;

	ret = bind(fd, (const struct sockaddr *)&addr, addr_len);
	if (ret < 0)
		goto out;

	ret = listen(fd, 128);
	if (ret < 0)
		goto out;

	return fd;

out:
	if (fd >= 0)
		close(fd);
	return -1;
}

bool set_core_pattern(const char *pattern)
{
	int fd;
	ssize_t ret;

	fd = open("/proc/sys/kernel/core_pattern", O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	ret = write(fd, pattern, strlen(pattern));
	close(fd);
	if (ret < 0)
		return false;

	fprintf(stderr, "Set core_pattern to '%s' | %zu == %zu\n", pattern, ret, strlen(pattern));
	return ret == strlen(pattern);
}

int get_peer_pidfd(int fd)
{
	int fd_peer_pidfd;
	socklen_t fd_peer_pidfd_len = sizeof(fd_peer_pidfd);
	int ret = getsockopt(fd, SOL_SOCKET, SO_PEERPIDFD, &fd_peer_pidfd,
			     &fd_peer_pidfd_len);
	if (ret < 0) {
		fprintf(stderr, "get_peer_pidfd: getsockopt(SO_PEERPIDFD) failed: %m\n");
		return -1;
	}
	fprintf(stderr, "get_peer_pidfd: successfully retrieved pidfd %d\n", fd_peer_pidfd);
	return fd_peer_pidfd;
}

bool get_pidfd_info(int fd_peer_pidfd, struct pidfd_info *info)
{
	int ret;
	memset(info, 0, sizeof(*info));
	info->mask = PIDFD_INFO_EXIT | PIDFD_INFO_COREDUMP | PIDFD_INFO_COREDUMP_SIGNAL;
	ret = ioctl(fd_peer_pidfd, PIDFD_GET_INFO, info);
	if (ret < 0) {
		fprintf(stderr, "get_pidfd_info: ioctl(PIDFD_GET_INFO) failed: %m\n");
		return false;
	}
	fprintf(stderr, "get_pidfd_info: mask=0x%llx, coredump_mask=0x%x, coredump_signal=%d, coredump_code=%d\n",
		(unsigned long long)info->mask, info->coredump_mask, info->coredump_signal, info->coredump_code);
	return true;
}

/* Protocol helper functions */

ssize_t recv_marker(int fd)
{
	enum coredump_mark mark = COREDUMP_MARK_REQACK;
	ssize_t ret;

	ret = recv(fd, &mark, sizeof(mark), MSG_WAITALL);
	if (ret != sizeof(mark))
		return -1;

	switch (mark) {
	case COREDUMP_MARK_REQACK:
		fprintf(stderr, "Received marker: ReqAck\n");
		return COREDUMP_MARK_REQACK;
	case COREDUMP_MARK_MINSIZE:
		fprintf(stderr, "Received marker: MinSize\n");
		return COREDUMP_MARK_MINSIZE;
	case COREDUMP_MARK_MAXSIZE:
		fprintf(stderr, "Received marker: MaxSize\n");
		return COREDUMP_MARK_MAXSIZE;
	case COREDUMP_MARK_UNSUPPORTED:
		fprintf(stderr, "Received marker: Unsupported\n");
		return COREDUMP_MARK_UNSUPPORTED;
	case COREDUMP_MARK_CONFLICTING:
		fprintf(stderr, "Received marker: Conflicting\n");
		return COREDUMP_MARK_CONFLICTING;
	default:
		fprintf(stderr, "Received unknown marker: %u\n", mark);
		break;
	}
	return -1;
}

bool read_marker(int fd, enum coredump_mark mark)
{
	ssize_t ret;

	ret = recv_marker(fd);
	if (ret < 0)
		return false;
	return ret == mark;
}

bool read_coredump_req(int fd, struct coredump_req *req)
{
	ssize_t ret;
	size_t field_size, user_size, ack_size, kernel_size, remaining_size;

	memset(req, 0, sizeof(*req));
	field_size = sizeof(req->size);

	/* Peek the size of the coredump request. */
	ret = recv(fd, req, field_size, MSG_PEEK | MSG_WAITALL);
	if (ret != field_size) {
		fprintf(stderr, "read_coredump_req: peek failed (got %zd, expected %zu): %m\n",
			ret, field_size);
		return false;
	}
	kernel_size = req->size;

	if (kernel_size < COREDUMP_ACK_SIZE_VER0) {
		fprintf(stderr, "read_coredump_req: kernel_size %zu < min %d\n",
			kernel_size, COREDUMP_ACK_SIZE_VER0);
		return false;
	}
	if (kernel_size >= PAGE_SIZE) {
		fprintf(stderr, "read_coredump_req: kernel_size %zu >= PAGE_SIZE %d\n",
			kernel_size, PAGE_SIZE);
		return false;
	}

	/* Use the minimum of user and kernel size to read the full request. */
	user_size = sizeof(struct coredump_req);
	ack_size = user_size < kernel_size ? user_size : kernel_size;
	ret = recv(fd, req, ack_size, MSG_WAITALL);
	if (ret != ack_size)
		return false;

	fprintf(stderr, "Read coredump request with size %u and mask 0x%llx\n",
		req->size, (unsigned long long)req->mask);

	if (user_size > kernel_size)
		remaining_size = user_size - kernel_size;
	else
		remaining_size = kernel_size - user_size;

	if (PAGE_SIZE <= remaining_size)
		return false;

	/*
	 * Discard any additional data if the kernel's request was larger than
	 * what we knew about or cared about.
	 */
	if (remaining_size) {
		char buffer[PAGE_SIZE];

		ret = recv(fd, buffer, sizeof(buffer), MSG_WAITALL);
		if (ret != remaining_size)
			return false;
		fprintf(stderr, "Discarded %zu bytes of data after coredump request\n", remaining_size);
	}

	return true;
}

bool send_coredump_ack(int fd, const struct coredump_req *req,
		       __u64 mask, size_t size_ack)
{
	ssize_t ret;
	/*
	 * Wrap struct coredump_ack in a larger struct so we can
	 * simulate sending to much data to the kernel.
	 */
	struct large_ack_for_size_testing {
		struct coredump_ack ack;
		char buffer[PAGE_SIZE];
	} large_ack = {};

	if (!size_ack)
		size_ack = sizeof(struct coredump_ack) < req->size_ack ?
				   sizeof(struct coredump_ack) :
				   req->size_ack;
	large_ack.ack.mask = mask;
	large_ack.ack.size = size_ack;
	ret = send(fd, &large_ack, size_ack, MSG_NOSIGNAL);
	if (ret != size_ack)
		return false;

	fprintf(stderr, "Sent coredump ack with size %zu and mask 0x%llx\n",
		size_ack, (unsigned long long)mask);
	return true;
}

bool check_coredump_req(const struct coredump_req *req, size_t min_size,
			__u64 required_mask)
{
	if (req->size < min_size)
		return false;
	if ((req->mask & required_mask) != required_mask)
		return false;
	if (req->mask & ~required_mask)
		return false;
	return true;
}

int open_coredump_tmpfile(int fd_tmpfs_detached)
{
	return openat(fd_tmpfs_detached, ".", O_TMPFILE | O_RDWR | O_EXCL, 0600);
}

void process_coredump_worker(int fd_coredump, int fd_peer_pidfd, int fd_core_file)
{
	int epfd = -1;
	int exit_code = EXIT_FAILURE;
	struct epoll_event ev;
	int flags;

	/* Set socket to non-blocking mode for edge-triggered epoll */
	flags = fcntl(fd_coredump, F_GETFL, 0);
	if (flags < 0) {
		fprintf(stderr, "Worker: fcntl(F_GETFL) failed: %m\n");
		goto out;
	}
	if (fcntl(fd_coredump, F_SETFL, flags | O_NONBLOCK) < 0) {
		fprintf(stderr, "Worker: fcntl(F_SETFL, O_NONBLOCK) failed: %m\n");
		goto out;
	}

	epfd = epoll_create1(0);
	if (epfd < 0) {
		fprintf(stderr, "Worker: epoll_create1() failed: %m\n");
		goto out;
	}

	ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
	ev.data.fd = fd_coredump;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd_coredump, &ev) < 0) {
		fprintf(stderr, "Worker: epoll_ctl(EPOLL_CTL_ADD) failed: %m\n");
		goto out;
	}

	for (;;) {
		struct epoll_event events[1];
		int n = epoll_wait(epfd, events, 1, -1);
		if (n < 0) {
			fprintf(stderr, "Worker: epoll_wait() failed: %m\n");
			break;
		}

		if (events[0].events & (EPOLLIN | EPOLLRDHUP)) {
			for (;;) {
				char buffer[4096];
				ssize_t bytes_read = read(fd_coredump, buffer, sizeof(buffer));
				if (bytes_read < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK)
						break;
					fprintf(stderr, "Worker: read() failed: %m\n");
					goto out;
				}
				if (bytes_read == 0)
					goto done;
				ssize_t bytes_write = write(fd_core_file, buffer, bytes_read);
				if (bytes_write != bytes_read) {
					if (bytes_write < 0 && errno == ENOSPC)
						continue;
					fprintf(stderr, "Worker: write() failed (read=%zd, write=%zd): %m\n",
						bytes_read, bytes_write);
					goto out;
				}
			}
		}
	}

done:
	exit_code = EXIT_SUCCESS;
	fprintf(stderr, "Worker: completed successfully\n");
out:
	if (epfd >= 0)
		close(epfd);
	if (fd_core_file >= 0)
		close(fd_core_file);
	if (fd_peer_pidfd >= 0)
		close(fd_peer_pidfd);
	if (fd_coredump >= 0)
		close(fd_coredump);
	_exit(exit_code);
}

/*
 * TIF_NOTIFY_SIGNAL coredump helpers.
 *
 * __dump_emit() takes anything short of a full write as the end of the
 * dump, so every emit that blocks on a full transport can lose the rest
 * of it. The NT_FILE note is the one emit that is certain to block,
 * because it is the only one larger than the transport, so these helpers
 * build a note large enough for that and then raise TIF_NOTIFY_SIGNAL on
 * the dumping task while that write is in flight.
 */

static int io_uring_setup_raw(unsigned int entries, struct io_uring_params *p)
{
	return syscall(__NR_io_uring_setup, entries, p);
}

/* io_uring reads poll32_events back through swahw32() on big endian. */
static __u32 notify_poll_mask(__u32 events)
{
#if defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN
	return __swahw32(events);
#else
	return events;
#endif
}

bool coredump_io_uring_available(void)
{
	struct io_uring_params p = {};
	int fd;

	fd = io_uring_setup_raw(1, &p);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

/*
 * Arm a poll on @fd. io_uring leaves ctx->notify_method at TWA_SIGNAL
 * unless the ring asks for SQPOLL or COOP_TASKRUN, so the completion runs
 * set_notify_signal() against the task that submitted it. That is us, and
 * we are about to become the coredumping task.
 */
static int arm_poll_notify(int trigger_fd)
{
	struct io_uring_params p = {};
	unsigned int *sq_tail, *sq_array;
	struct io_uring_sqe *sqes;
	size_t sqring_sz;
	void *sq;
	int ring;

	ring = io_uring_setup_raw(8, &p);
	if (ring < 0)
		return -1;

	sqring_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned int);
	sq = mmap(NULL, sqring_sz, PROT_READ | PROT_WRITE,
		  MAP_SHARED | MAP_POPULATE, ring, IORING_OFF_SQ_RING);
	if (sq == MAP_FAILED)
		return -1;

	sqes = mmap(NULL, p.sq_entries * sizeof(*sqes), PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_POPULATE, ring, IORING_OFF_SQES);
	if (sqes == MAP_FAILED)
		return -1;

	sq_tail = (unsigned int *)((char *)sq + p.sq_off.tail);
	sq_array = (unsigned int *)((char *)sq + p.sq_off.array);

	memset(&sqes[0], 0, sizeof(sqes[0]));
	sqes[0].opcode = IORING_OP_POLL_ADD;
	sqes[0].fd = trigger_fd;
	sqes[0].poll32_events = notify_poll_mask(POLLIN);

	sq_array[0] = 0;
	__atomic_store_n(sq_tail, 1, __ATOMIC_RELEASE);

	if (syscall(__NR_io_uring_enter, ring, 1, 0, 0, NULL, 0) < 0)
		return -1;

	/* Deliberately leaked, we are about to crash. */
	return 0;
}

/*
 * Adjacent mappings with identical flags and contiguous file offsets are
 * merged into one VMA, which would collapse NT_FILE back to nothing, so
 * alternate the protection to keep every mapping an entry of its own.
 * Not PROT_EXEC, /tmp is often mounted noexec. Nothing is ever written
 * through these so they get no anon_vma and stay out of the dump itself.
 */
static int make_file_mappings(void)
{
	long pgsz = sysconf(_SC_PAGESIZE);
	int fd, i;

	fd = open(NOTIFY_SIGNAL_MAPFILE,
		  O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return 0;
	if (ftruncate(fd, (off_t)NOTIFY_SIGNAL_MAP_COUNT * pgsz)) {
		close(fd);
		return 0;
	}

	for (i = 0; i < NOTIFY_SIGNAL_MAP_COUNT; i++) {
		int prot = (i & 1) ? PROT_READ : (PROT_READ | PROT_WRITE);

		if (mmap(NULL, pgsz, prot, MAP_PRIVATE, fd,
			 (off_t)i * pgsz) == MAP_FAILED)
			break;
	}
	close(fd);
	return i;
}

void crashing_child_notify_signal(void)
{
	long pgsz = sysconf(_SC_PAGESIZE);
	unsigned char *p;
	int trigger_fd;
	unsigned long off;

	/* Open the read side first so the reader's open() cannot block. */
	trigger_fd = open(NOTIFY_SIGNAL_TRIGGER, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	/* Exit rather than crash: a dump without the poll armed proves nothing. */
	if (trigger_fd < 0)
		_exit(EXIT_FAILURE);

	if (make_file_mappings() < NOTIFY_SIGNAL_MAP_COUNT)
		_exit(EXIT_FAILURE);

	p = mmap(NULL, NOTIFY_SIGNAL_ANON_BYTES, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		_exit(EXIT_FAILURE);
	for (off = 0; off < NOTIFY_SIGNAL_ANON_BYTES; off += pgsz)
		p[off] = 1;

	if (arm_poll_notify(trigger_fd))
		_exit(EXIT_FAILURE);

	/* crash on purpose */
	*(volatile int *)NULL = 0;
	_exit(EXIT_FAILURE);
}

static int pull_trigger(void)
{
	int fd;

	fd = open(NOTIFY_SIGNAL_TRIGGER, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "%s: open failed: %m\n", __func__);
		return -1;
	}
	if (write(fd, "x", 1) != 1) {
		fprintf(stderr, "%s: write failed: %m\n", __func__);
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/*
 * phdr[0] is the PT_NOTE entry: elf_core_dump() emits it right after the
 * ELF header. Its p_offset is where the notes begin.
 */
static long long note_offset(const unsigned char *hdr)
{
	ElfW(Phdr) ph;
	ElfW(Ehdr) eh;

	memcpy(&eh, hdr, sizeof(eh));
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_type != ET_CORE)
		return -1;
	memcpy(&ph, hdr + sizeof(eh), sizeof(ph));
	if (ph.p_type != PT_NOTE)
		return -1;
	return (long long)ph.p_offset;
}

/*
 * Drain a coredump off @fd, counting what arrives. Once the notes have
 * started the kernel is inside the one big note write, so poke the fifo
 * the crashing task is polling and stop reading, which keeps the
 * transport full and the write blocked with a partial count when the
 * wakeup lands. Then carry on to end of file.
 *
 * What arrives is written to @fd_out when that is not negative.
 * Returns the number of bytes received, or -1. Failing to trip the fifo
 * is an error too: a dump that was never interrupted proves nothing.
 */
ssize_t recv_coredump_notify_signal(int fd, int fd_out, bool arm)
{
	unsigned char hdr[sizeof(ElfW(Ehdr)) + sizeof(ElfW(Phdr))];
	static char buf[64 << 10];
	long pgsz = sysconf(_SC_PAGESIZE);
	long long note_off = 0;
	size_t hdrlen = 0;
	ssize_t total = 0;
	bool armed = false;

	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		if (fd_out >= 0 && write(fd_out, buf, n) != n)
			return -1;

		if (hdrlen < sizeof(hdr)) {
			size_t want = sizeof(hdr) - hdrlen;

			if (want > (size_t)n)
				want = (size_t)n;
			memcpy(hdr + hdrlen, buf, want);
			hdrlen += want;
			if (hdrlen == sizeof(hdr))
				note_off = note_offset(hdr);
		}

		total += n;

		if (arm && !armed && note_off > 0 &&
		    total > note_off + (long long)pgsz) {
			if (pull_trigger())
				return -1;
			armed = true;
			usleep(NOTIFY_SIGNAL_STALL_US);
			continue;
		}
	}

	if (arm && !armed)
		return -1;

	return total;
}

/*
 * How large the dump was meant to be. The ELF header and the program
 * headers are the first thing emitted, so even a truncated dump says how
 * far it should have run: the end is max(p_offset + p_filesz).
 *
 * That end is exact even when the last segment ends in a hole:
 * coredump_write() flushes the pending cprm->to_skip with a final one
 * byte emit and __dump_skip() writes zeroes for transports that cannot
 * seek, so a whole dump carries every byte the headers promise.
 */
long long coredump_expected_size(const char *path)
{
	ElfW(Phdr) *phdr = NULL;
	long long expected = 0;
	unsigned int nphdr, i;
	ElfW(Ehdr) eh;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (read(fd, &eh, sizeof(eh)) != sizeof(eh))
		goto err;
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_type != ET_CORE)
		goto err;
	if (!eh.e_phnum || eh.e_phentsize != sizeof(*phdr))
		goto err;

	nphdr = eh.e_phnum;
	phdr = calloc(nphdr, sizeof(*phdr));
	if (!phdr)
		goto err;
	if (pread(fd, phdr, (size_t)nphdr * sizeof(*phdr), (off_t)eh.e_phoff) !=
	    (ssize_t)((size_t)nphdr * sizeof(*phdr)))
		goto err;

	for (i = 0; i < nphdr; i++) {
		long long end = (long long)phdr[i].p_offset +
				(long long)phdr[i].p_filesz;
		if (end > expected)
			expected = end;
	}

	free(phdr);
	close(fd);
	return expected;
err:
	free(phdr);
	close(fd);
	return -1;
}
