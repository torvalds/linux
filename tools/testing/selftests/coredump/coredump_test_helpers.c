// SPDX-License-Identifier: GPL-2.0

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/coredump.h>
#include <linux/fs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../filesystems/wrappers.h"
#include "../pidfd/pidfd.h"

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
	__builtin_trap();
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
	fprintf(stderr, "get_pidfd_info: mask=0x%llx, coredump_mask=0x%x, coredump_signal=%d\n",
		(unsigned long long)info->mask, info->coredump_mask, info->coredump_signal);
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
