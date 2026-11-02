// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <linux/perf_event.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "stream.skel.h"
#include "stream_fail.skel.h"

#define NMI_TIMEOUT_NS (5ULL * 1000 * 1000 * 1000)

void test_stream_failure(void)
{
	RUN_TESTS(stream_fail);
}

void test_stream_success(void)
{
	RUN_TESTS(stream);
	return;
}

void test_stream_syscall(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	LIBBPF_OPTS(bpf_prog_stream_read_opts, ropts);
	struct stream *skel;
	int ret, prog_fd;
	char buf[64];

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_syscall);
	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");

	ASSERT_LT(bpf_prog_stream_read(0, BPF_STREAM_STDOUT, buf, sizeof(buf), &ropts), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -EINVAL, "bad prog_fd");

	ASSERT_LT(bpf_prog_stream_read(prog_fd, 0, buf, sizeof(buf), &ropts), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -ENOENT, "bad stream id");

	ASSERT_LT(bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, NULL, sizeof(buf), NULL), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -EFAULT, "bad stream buf");

	ASSERT_LT(bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, UINT_MAX, NULL), 0, "error");
	ret = -errno;
	ASSERT_EQ(ret, -EINVAL, "large stream buf");

	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 2, NULL);
	ASSERT_EQ(ret, 2, "bytes");
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 2, NULL);
	ASSERT_EQ(ret, 1, "bytes");
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 1, &ropts);
	ASSERT_EQ(ret, 0, "no bytes stdout");
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDERR, buf, 1, &ropts);
	ASSERT_EQ(ret, 0, "no bytes stderr");

	stream__destroy(skel);
}

static bool stream_fd_trigger(struct bpf_program *prog)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	int ret;

	ret = bpf_prog_test_run_opts(bpf_program__fd(prog), &opts);
	return ASSERT_OK(ret, "test_run") && ASSERT_OK(opts.retval, "retval");
}

static void test_stream_fd_open(void)
{
	LIBBPF_OPTS(bpf_prog_stream_open_opts, opts);
	struct stream *skel;
	int fd, prog_fd;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_syscall);
	fd = bpf_prog_stream_open(0, BPF_STREAM_STDOUT, NULL);
	ASSERT_EQ(fd, -EINVAL, "bad_prog_fd");

	fd = bpf_prog_stream_open(prog_fd, 0, NULL);
	ASSERT_EQ(fd, -ENOENT, "bad_stream_id");

	opts.flags = BPF_F_RDONLY;
	fd = bpf_prog_stream_open(prog_fd, BPF_STREAM_STDOUT, &opts);
	ASSERT_EQ(fd, -EINVAL, "access_flag");

	opts.flags = 1U << 31;
	fd = bpf_prog_stream_open(prog_fd, BPF_STREAM_STDOUT, &opts);
	ASSERT_EQ(fd, -EINVAL, "unknown_flag");

	fd = bpf_prog_stream_open(prog_fd, BPF_STREAM_STDERR, NULL);
	if (ASSERT_OK_FD(fd, "stderr"))
		close(fd);

	stream__destroy(skel);
}

static void test_stream_fd_nonblock(void)
{
	LIBBPF_OPTS(bpf_prog_stream_open_opts, opts,
		.flags = BPF_F_STREAM_NONBLOCK,
	);
	struct pollfd pfd = { .events = POLLIN | POLLHUP };
	struct stream *skel;
	char buf[4] = {};
	int fd, flags, ret;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	fd = bpf_prog_stream_open(bpf_program__fd(skel->progs.stream_syscall),
				  BPF_STREAM_STDOUT, &opts);
	if (!ASSERT_OK_FD(fd, "stream_open"))
		goto out_destroy;
	pfd.fd = fd;

	flags = fcntl(fd, F_GETFD);
	ASSERT_GE(flags, 0, "getfd");
	ASSERT_NEQ(flags & FD_CLOEXEC, 0, "cloexec");
	flags = fcntl(fd, F_GETFL);
	ASSERT_GE(flags, 0, "getfl");
	ASSERT_EQ(flags & O_ACCMODE, O_RDONLY, "readonly");
	ASSERT_NEQ(flags & O_NONBLOCK, 0, "nonblock");

	ret = write(fd, "x", 1);
	ASSERT_EQ(ret, -1, "write");
	ASSERT_EQ(errno, EBADF, "write_errno");
	ret = lseek(fd, 0, SEEK_SET);
	ASSERT_EQ(ret, -1, "lseek");
	ASSERT_EQ(errno, ESPIPE, "lseek_errno");

	ret = poll(&pfd, 1, 0);
	ASSERT_EQ(ret, 0, "poll_empty");
	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, -1, "read_empty");
	ASSERT_EQ(errno, EAGAIN, "read_empty_errno");

	if (!stream_fd_trigger(skel->progs.stream_syscall))
		goto out_close;
	ret = poll(&pfd, 1, 0);
	ASSERT_EQ(ret, 1, "poll_data");
	ASSERT_NEQ(pfd.revents & POLLIN, 0, "pollin");
	ASSERT_EQ(pfd.revents & POLLHUP, 0, "no_pollhup");

	ret = read(fd, buf, 2);
	ASSERT_EQ(ret, 2, "read_first");
	ASSERT_OK(memcmp(buf, "fo", 2), "read_first_data");
	pfd.revents = 0;
	ret = poll(&pfd, 1, 0);
	ASSERT_EQ(ret, 1, "poll_partial");
	ASSERT_NEQ(pfd.revents & POLLIN, 0, "pollin_partial");

	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, 1, "read_rest");
	ASSERT_EQ(buf[0], 'o', "read_rest_data");
	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, -1, "read_drained");
	ASSERT_EQ(errno, EAGAIN, "read_drained_errno");

out_close:
	close(fd);
out_destroy:
	stream__destroy(skel);
}

static void test_stream_fd_empty(void)
{
	LIBBPF_OPTS(bpf_prog_stream_open_opts, opts,
		.flags = BPF_F_STREAM_NONBLOCK,
	);
	struct pollfd pfd = { .events = POLLIN | POLLHUP };
	struct stream *skel;
	char buf[4];
	int fd, ret;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	fd = bpf_prog_stream_open(bpf_program__fd(skel->progs.stream_empty),
				  BPF_STREAM_STDOUT, &opts);
	if (!ASSERT_OK_FD(fd, "stream_open"))
		goto out_destroy;
	pfd.fd = fd;

	/* Empty output produces neither data nor readiness. */
	if (!stream_fd_trigger(skel->progs.stream_empty))
		goto out_close;
	ret = poll(&pfd, 1, 0);
	ASSERT_EQ(ret, 0, "poll_empty_write");
	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, -1, "read_empty_write");
	ASSERT_EQ(errno, EAGAIN, "read_empty_write_errno");

out_close:
	close(fd);
out_destroy:
	stream__destroy(skel);
}

struct stream_fd_read_ctx {
	int fd;
	ssize_t ret;
	char buf[4];
};

static void *stream_fd_read_thread(void *arg)
{
	struct stream_fd_read_ctx *ctx = arg;

	ctx->ret = read(ctx->fd, ctx->buf, sizeof(ctx->buf));
	return NULL;
}

static int stream_fd_timed_join(pthread_t thread)
{
	struct timespec timeout;

	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 5;
	return pthread_timedjoin_np(thread, NULL, &timeout);
}

static void test_stream_fd_blocking(void)
{
	struct stream_fd_read_ctx ctx = {};
	struct stream *skel;
	pthread_t thread;
	bool thread_live = false;
	int fd, flags, ret;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	fd = bpf_prog_stream_open(bpf_program__fd(skel->progs.stream_syscall),
				  BPF_STREAM_STDOUT, NULL);
	if (!ASSERT_OK_FD(fd, "stream_open"))
		goto out_destroy;
	ctx.fd = fd;

	flags = fcntl(fd, F_GETFL);
	ASSERT_GE(flags, 0, "getfl");
	ASSERT_EQ(flags & O_NONBLOCK, 0, "blocking");

	ret = pthread_create(&thread, NULL, stream_fd_read_thread, &ctx);
	if (!ASSERT_OK(ret, "pthread_create"))
		goto out_close;
	thread_live = true;

	usleep(50000);
	ret = pthread_tryjoin_np(thread, NULL);
	if (!ASSERT_EQ(ret, EBUSY, "read_blocks")) {
		thread_live = ret != 0;
		goto out_thread;
	}
	if (!stream_fd_trigger(skel->progs.stream_syscall))
		goto out_thread;

	ret = stream_fd_timed_join(thread);
	if (!ASSERT_OK(ret, "pthread_join"))
		goto out_thread;
	thread_live = false;
	ASSERT_EQ(ctx.ret, 3, "read_len");
	ASSERT_OK(memcmp(ctx.buf, "foo", 3), "read_data");

	memset(&ctx, 0, sizeof(ctx));
	ctx.fd = fd;
	ret = pthread_create(&thread, NULL, stream_fd_read_thread, &ctx);
	if (!ASSERT_OK(ret, "pthread_create_eof"))
		goto out_close;
	thread_live = true;

	usleep(50000);
	ret = pthread_tryjoin_np(thread, NULL);
	if (!ASSERT_EQ(ret, EBUSY, "read_eof_blocks")) {
		thread_live = ret != 0;
		goto out_thread;
	}

	stream__destroy(skel);
	skel = NULL;
	ret = stream_fd_timed_join(thread);
	if (!ASSERT_OK(ret, "pthread_join_eof"))
		goto out_thread;
	thread_live = false;
	ASSERT_EQ(ctx.ret, 0, "read_eof");

out_thread:
	if (thread_live) {
		stream__destroy(skel);
		skel = NULL;
		ret = stream_fd_timed_join(thread);
		if (ret) {
			pthread_cancel(thread);
			pthread_join(thread, NULL);
		}
	}
out_close:
	close(fd);
out_destroy:
	stream__destroy(skel);
}

static void test_stream_fd_hup(void)
{
	LIBBPF_OPTS(bpf_prog_stream_open_opts, opts,
		.flags = BPF_F_STREAM_NONBLOCK,
	);
	struct epoll_event event = {
		.events = EPOLLIN | EPOLLET,
	};
	struct pollfd pfd = { .events = POLLIN | POLLHUP };
	struct stream *skel;
	char buf[4] = {};
	int epfd = -1, fd, ret;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	fd = bpf_prog_stream_open(bpf_program__fd(skel->progs.stream_syscall),
				  BPF_STREAM_STDOUT, &opts);
	if (!ASSERT_OK_FD(fd, "stream_open"))
		goto out_destroy;
	pfd.fd = fd;
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (!ASSERT_OK_FD(epfd, "epoll_create"))
		goto out_close;
	event.data.fd = fd;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
	if (!ASSERT_OK(ret, "epoll_ctl"))
		goto out_close;

	if (!stream_fd_trigger(skel->progs.stream_syscall))
		goto out_close;
	ret = epoll_wait(epfd, &event, 1, 5000);
	if (!ASSERT_EQ(ret, 1, "epoll_wait_data"))
		goto out_close;
	ASSERT_NEQ(event.events & EPOLLIN, 0, "epollin");
	ASSERT_EQ(event.events & EPOLLHUP, 0, "no_epollhup");

	stream__destroy(skel);
	skel = NULL;
	event.events = 0;
	ret = epoll_wait(epfd, &event, 1, 5000);
	if (!ASSERT_EQ(ret, 1, "epoll_wait_hup"))
		goto out_close;
	ASSERT_NEQ(event.events & EPOLLIN, 0, "epollin_with_hup");
	ASSERT_NEQ(event.events & EPOLLHUP, 0, "epollhup");

	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, 3, "read_buffered");
	ASSERT_OK(memcmp(buf, "foo", 3), "read_buffered_data");
	pfd.revents = 0;
	ret = poll(&pfd, 1, 0);
	ASSERT_EQ(ret, 1, "poll_drained_hup");
	ASSERT_EQ(pfd.revents & POLLIN, 0, "no_pollin_after_drain");
	ASSERT_NEQ(pfd.revents & POLLHUP, 0, "pollhup_after_drain");
	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, 0, "read_eof");

out_close:
	if (epfd >= 0)
		close(epfd);
	close(fd);
out_destroy:
	stream__destroy(skel);
}

static void test_stream_fd_edge(void)
{
	LIBBPF_OPTS(bpf_prog_stream_open_opts, opts,
		.flags = BPF_F_STREAM_NONBLOCK,
	);
	struct epoll_event event = {
		.events = EPOLLIN | EPOLLET,
	};
	struct stream *skel;
	char buf[8] = {};
	int epfd = -1, fd, ret;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	fd = bpf_prog_stream_open(bpf_program__fd(skel->progs.stream_syscall),
				  BPF_STREAM_STDOUT, &opts);
	if (!ASSERT_OK_FD(fd, "stream_open"))
		goto out_destroy;
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (!ASSERT_OK_FD(epfd, "epoll_create"))
		goto out_close;
	event.data.fd = fd;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
	if (!ASSERT_OK(ret, "epoll_ctl"))
		goto out_close;

	/* Output into an empty stream raises an edge. */
	if (!stream_fd_trigger(skel->progs.stream_syscall))
		goto out_close;
	ret = epoll_wait(epfd, &event, 1, 5000);
	if (!ASSERT_EQ(ret, 1, "epoll_wait_first"))
		goto out_close;
	ASSERT_NEQ(event.events & EPOLLIN, 0, "epollin_first");

	/*
	 * Leave data behind, add more, then drain everything. Whether output
	 * into a non-empty stream raises an edge of its own is not part of the
	 * contract, so discard any pending event before re-arming the stream.
	 */
	ret = read(fd, buf, 2);
	ASSERT_EQ(ret, 2, "read_partial");
	if (!stream_fd_trigger(skel->progs.stream_syscall))
		goto out_close;
	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, 4, "read_rest");
	ASSERT_OK(memcmp(buf, "ofoo", 4), "read_rest_data");
	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, -1, "read_drained");
	ASSERT_EQ(errno, EAGAIN, "read_drained_errno");
	epoll_wait(epfd, &event, 1, 0);

	/* Output into the drained stream raises an edge again. */
	if (!stream_fd_trigger(skel->progs.stream_syscall))
		goto out_close;
	ret = epoll_wait(epfd, &event, 1, 5000);
	if (!ASSERT_EQ(ret, 1, "epoll_wait_after_drain"))
		goto out_close;
	ASSERT_NEQ(event.events & EPOLLIN, 0, "epollin_after_drain");

out_close:
	if (epfd >= 0)
		close(epfd);
	close(fd);
out_destroy:
	stream__destroy(skel);
}

static void test_stream_fd_nmi_epoll(void)
{
	LIBBPF_OPTS(bpf_prog_stream_open_opts, opts,
		.flags = BPF_F_STREAM_NONBLOCK,
	);
	struct perf_event_attr attr = {
		.size = sizeof(attr),
		.type = PERF_TYPE_HARDWARE,
		.config = PERF_COUNT_HW_CPU_CYCLES,
		.freq = 1,
		.sample_freq = 10,
	};
	struct epoll_event event = {
		.events = EPOLLIN,
	};
	struct bpf_link *link = NULL;
	struct stream *skel;
	__u64 deadline;
	char buf[4] = {};
	int epfd = -1, fd = -1, pmu_fd = -1;
	int ret;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;
	fd = bpf_prog_stream_open(bpf_program__fd(skel->progs.stream_nmi),
				  BPF_STREAM_STDOUT, &opts);
	if (!ASSERT_OK_FD(fd, "stream_open"))
		goto out;

	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (!ASSERT_OK_FD(epfd, "epoll_create"))
		goto out;
	event.data.fd = fd;
	ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
	if (!ASSERT_OK(ret, "epoll_ctl"))
		goto out;

	pmu_fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1,
			 PERF_FLAG_FD_CLOEXEC);
	if (pmu_fd < 0 && (errno == ENOENT || errno == EOPNOTSUPP)) {
		printf("%s:SKIP:no PERF_COUNT_HW_CPU_CYCLES\n", __func__);
		test__skip();
		goto out;
	}
	if (!ASSERT_GE(pmu_fd, 0, "perf_event_open"))
		goto out;

	link = bpf_program__attach_perf_event(skel->progs.stream_nmi, pmu_fd);
	if (!ASSERT_OK_PTR(link, "attach_perf_event")) {
		link = NULL;
		goto out;
	}
	pmu_fd = -1;

	deadline = get_time_ns() + NMI_TIMEOUT_NS;
	do {
		ret = epoll_wait(epfd, &event, 1, 0);
	} while (ret == 0 && get_time_ns() < deadline);
	if (!ASSERT_EQ(ret, 1, "epoll_wait"))
		goto out;
	ASSERT_NEQ(event.events & EPOLLIN, 0, "epollin");

	ret = read(fd, buf, sizeof(buf));
	ASSERT_EQ(ret, 3, "read_len");
	ASSERT_OK(memcmp(buf, "nmi", 3), "read_data");

out:
	bpf_link__destroy(link);
	if (pmu_fd >= 0)
		close(pmu_fd);
	if (epfd >= 0)
		close(epfd);
	if (fd >= 0)
		close(fd);
	stream__destroy(skel);
}

void test_stream_fd(void)
{
	if (test__start_subtest("open"))
		test_stream_fd_open();
	if (test__start_subtest("nonblock"))
		test_stream_fd_nonblock();
	if (test__start_subtest("empty"))
		test_stream_fd_empty();
	if (test__start_subtest("blocking"))
		test_stream_fd_blocking();
	if (test__start_subtest("hup"))
		test_stream_fd_hup();
	if (test__start_subtest("edge"))
		test_stream_fd_edge();
	if (test__start_subtest("nmi_epoll"))
		test_stream_fd_nmi_epoll();
}

void test_stream_oversize(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct stream *skel;
	int ret, prog_fd;

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_oversize);
	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "oversize run");
	ASSERT_OK(opts.retval, "oversize retval");

	stream__destroy(skel);
}

void test_stream_partial_read(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct stream *skel;
	int ret, prog_fd;
	long page_size;
	char *page, *buf;
	char rest[8] = {};

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	prog_fd = bpf_program__fd(skel->progs.stream_syscall);
	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");

	page_size = sysconf(_SC_PAGESIZE);
	page = mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!ASSERT_NEQ(page, MAP_FAILED, "mmap")) {
		stream__destroy(skel);
		return;
	}
	/* Leave only the first page mapped so a straddling copy faults. */
	if (!ASSERT_OK(munmap(page + page_size, page_size), "munmap second page")) {
		munmap(page, page_size * 2);
		stream__destroy(skel);
		return;
	}

	buf = page + page_size - 1;
	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, buf, 3, NULL);
	ASSERT_EQ(ret, 1, "partial bytes");
	ASSERT_EQ(buf[0], 'f', "first byte");

	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDOUT, rest, sizeof(rest), NULL);
	ASSERT_EQ(ret, 2, "remaining bytes");
	ASSERT_OK(memcmp(rest, "oo", 2), "remaining data");

	munmap(page, page_size);
	stream__destroy(skel);
}

static void test_address(struct bpf_program *prog, unsigned long *fault_addr_p)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	LIBBPF_OPTS(bpf_prog_stream_read_opts, ropts);
	int ret, prog_fd;
	char fault_addr[64];
	char buf[1024];

	prog_fd = bpf_program__fd(prog);

	ret = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(ret, "ret");
	ASSERT_OK(opts.retval, "retval");

	sprintf(fault_addr, "0x%lx", *fault_addr_p);

	ret = bpf_prog_stream_read(prog_fd, BPF_STREAM_STDERR, buf, sizeof(buf), &ropts);
	ASSERT_GT(ret, 0, "stream read");
	ASSERT_LE(ret, 1023, "len for buf");
	buf[ret] = '\0';

	if (!ASSERT_HAS_SUBSTR(buf, fault_addr, "fault_addr")) {
		fprintf(stderr, "Output from stream:\n%s\n", buf);
		fprintf(stderr, "Fault Addr: %s\n", fault_addr);
	}
}

void test_stream_arena_fault_address(void)
{
	struct stream *skel;

#if !defined(__x86_64__) && !defined(__aarch64__)
	printf("%s:SKIP: arena fault reporting not supported\n", __func__);
	test__skip();
	return;
#endif

	skel = stream__open_and_load();
	if (!ASSERT_OK_PTR(skel, "stream__open_and_load"))
		return;

	if (test__start_subtest("read_fault"))
		test_address(skel->progs.stream_arena_read_fault, &skel->bss->fault_addr);
	if (test__start_subtest("write_fault"))
		test_address(skel->progs.stream_arena_write_fault, &skel->bss->fault_addr);
	if (test__start_subtest("load_acquire_fault"))
		test_address(skel->progs.stream_arena_load_acquire_fault, &skel->bss->fault_addr);
	if (test__start_subtest("xchg_fault"))
		test_address(skel->progs.stream_arena_xchg_fault, &skel->bss->fault_addr);
	if (test__start_subtest("cmpxchg_fault"))
		test_address(skel->progs.stream_arena_cmpxchg_fault, &skel->bss->fault_addr);

	stream__destroy(skel);
}
