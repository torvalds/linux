// SPDX-License-Identifier: GPL-2.0

/*
 * A user worker as the coredumping thread.
 *
 * io-wq workers and SQPOLL threads are threads of the process that never
 * return to userspace. They block every signal but SIGKILL and SIGSTOP,
 * but a tracer can replace that mask with PTRACE_SETSIGMASK and inject a
 * coredump signal. get_signal() then runs vfs_coredump() in the worker.
 * The worker's own exit bookkeeping runs only after the dump, so a
 * zapped sibling that waits for it in its exit path deadlocks with the
 * dumper and the whole thread group is stuck in D state.
 *
 * Inject SIGSEGV into a chosen thread and require that the thread group
 * is gone in bounded time, either because the dump completed or because
 * SIGKILL still works. A failure leaves the stuck process behind.
 */
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/io_uring.h>

#include "coredump_test.h"

#ifndef PTRACE_SETSIGMASK
#define PTRACE_SETSIGMASK 0x420b
#endif

/* The dump of the tiny child takes well under a second. */
#define EXIT_TIMEOUT_MS 5000

FIXTURE_SETUP(coredump)
{
	FILE *file;
	int ret;

	self->pid_coredump_server = -ESRCH;
	self->fd_tmpfs_detached = -1;
	file = fopen("/proc/sys/kernel/core_pattern", "r");
	ASSERT_NE(NULL, file);

	ret = fread(self->original_core_pattern, 1, sizeof(self->original_core_pattern), file);
	ASSERT_TRUE(ret || feof(file));
	ASSERT_LT(ret, sizeof(self->original_core_pattern));

	self->original_core_pattern[ret] = '\0';

	ret = fclose(file);
	ASSERT_EQ(0, ret);
}

FIXTURE_TEARDOWN(coredump)
{
	const char *reason;
	FILE *file;
	int ret;

	file = fopen("/proc/sys/kernel/core_pattern", "w");
	if (!file) {
		reason = "Unable to open core_pattern";
		goto fail;
	}

	ret = fprintf(file, "%s", self->original_core_pattern);
	if (ret < 0) {
		reason = "Unable to write to core_pattern";
		goto fail;
	}

	ret = fclose(file);
	if (ret) {
		reason = "Unable to close core_pattern";
		goto fail;
	}

	return;
fail:
	/* This should never happen */
	fprintf(stderr, "Failed to cleanup coredump test: %s\n", reason);
}

/* A raw ring, no liburing. */
struct uring {
	int fd;
	struct io_uring_params params;
	void *sq;
	size_t sq_len;
	struct io_uring_sqe *sqes;
	size_t sqes_len;
	unsigned int *sq_tail, *sq_mask, *sq_array;
	unsigned int *cq_head, *cq_tail, *cq_mask;
	struct io_uring_cqe *cqes;
};

static int uring_setup(struct uring *r, unsigned int flags)
{
	size_t cq_len;

	memset(r, 0, sizeof(*r));
	r->params.flags = flags;
	if (flags & IORING_SETUP_SQPOLL)
		r->params.sq_thread_idle = 2000;
	r->fd = syscall(__NR_io_uring_setup, 8, &r->params);
	if (r->fd < 0)
		return -1;
	if (!(r->params.features & IORING_FEAT_SINGLE_MMAP))
		return -1;

	r->sq_len = r->params.sq_off.array + r->params.sq_entries * sizeof(unsigned int);
	cq_len = r->params.cq_off.cqes + r->params.cq_entries * sizeof(struct io_uring_cqe);
	if (cq_len > r->sq_len)
		r->sq_len = cq_len;
	r->sq = mmap(NULL, r->sq_len, PROT_READ | PROT_WRITE,
		     MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQ_RING);
	if (r->sq == MAP_FAILED)
		return -1;
	r->sqes_len = r->params.sq_entries * sizeof(struct io_uring_sqe);
	r->sqes = mmap(NULL, r->sqes_len, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQES);
	if (r->sqes == MAP_FAILED)
		return -1;

	r->sq_tail = r->sq + r->params.sq_off.tail;
	r->sq_mask = r->sq + r->params.sq_off.ring_mask;
	r->sq_array = r->sq + r->params.sq_off.array;
	r->cq_head = r->sq + r->params.cq_off.head;
	r->cq_tail = r->sq + r->params.cq_off.tail;
	r->cq_mask = r->sq + r->params.cq_off.ring_mask;
	r->cqes = r->sq + r->params.cq_off.cqes;
	return 0;
}

/* Submit one sqe, wait for its completion and return the result. */
static int uring_submit_wait(struct uring *r, const struct io_uring_sqe *sqe)
{
	unsigned int tail = *r->sq_tail, idx = tail & *r->sq_mask;
	unsigned int flags = IORING_ENTER_GETEVENTS;
	int i;

	r->sqes[idx] = *sqe;
	r->sq_array[idx] = idx;
	__atomic_store_n(r->sq_tail, tail + 1, __ATOMIC_RELEASE);

	if (r->params.flags & IORING_SETUP_SQPOLL)
		flags |= IORING_ENTER_SQ_WAKEUP;

	for (i = 0; i < 100; i++) {
		if (syscall(__NR_io_uring_enter, r->fd, 1, 1, flags, NULL, 0) < 0 &&
		    errno != EINTR)
			return -1;
		if (__atomic_load_n(r->cq_tail, __ATOMIC_ACQUIRE) != *r->cq_head) {
			unsigned int head = *r->cq_head;
			int res = r->cqes[head & *r->cq_mask].res;

			__atomic_store_n(r->cq_head, head + 1, __ATOMIC_RELEASE);
			return res;
		}
		flags &= ~IORING_ENTER_SQ_WAKEUP;
		usleep(10 * 1000);
	}
	return -1;
}

static bool uring_available(unsigned int flags)
{
	struct io_uring_params params = { .flags = flags };
	int fd;

	fd = syscall(__NR_io_uring_setup, 2, &params);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

/*
 * Keep a ring, an idle io-wq worker and with SQPOLL an SQPOLL thread
 * alive. The last worker of a ring never exits on its idle timeout.
 */
static void worker_child(bool sqpoll, int fd_ipc)
{
	struct rlimit rl = { RLIM_INFINITY, RLIM_INFINITY };
	struct io_uring_sqe sqe = {};
	static char buf[64];
	struct uring ring;
	int memfd;

	if (setrlimit(RLIMIT_CORE, &rl))
		_exit(EXIT_FAILURE);

	memfd = memfd_create("coredump_worker", 0);
	if (memfd < 0 || write(memfd, "hello", 5) != 5)
		_exit(EXIT_FAILURE);

	if (uring_setup(&ring, sqpoll ? IORING_SETUP_SQPOLL : 0))
		_exit(EXIT_FAILURE);

	/* IOSQE_ASYNC forces the read through io-wq so a worker appears. */
	sqe.opcode = IORING_OP_READ;
	sqe.fd = memfd;
	sqe.addr = (__u64)(uintptr_t)buf;
	sqe.len = sizeof(buf);
	sqe.flags = IOSQE_ASYNC;
	if (uring_submit_wait(&ring, &sqe) != 5)
		_exit(EXIT_FAILURE);

	if (write_nointr(fd_ipc, "1", 1) != 1)
		_exit(EXIT_FAILURE);
	close(fd_ipc);

	for (;;)
		pause();
}

/* Find the thread of @pid whose comm starts with @prefix. */
static pid_t find_thread(pid_t pid, const char *prefix)
{
	char path[64], comm[64];
	pid_t tid = -1;
	struct dirent *de;
	ssize_t bytes;
	DIR *dir;
	int fd;

	snprintf(path, sizeof(path), "/proc/%d/task", pid);
	dir = opendir(path);
	if (!dir)
		return -1;

	while (tid < 0 && (de = readdir(dir))) {
		if (!isdigit(de->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "/proc/%d/task/%s/comm", pid, de->d_name);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		bytes = read(fd, comm, sizeof(comm) - 1);
		close(fd);
		if (bytes <= 0)
			continue;
		comm[bytes] = '\0';
		if (!strncmp(comm, prefix, strlen(prefix)))
			tid = atoi(de->d_name);
	}
	closedir(dir);
	return tid;
}

/*
 * Attach, stop the thread with SIGSTOP, drop the signal mask that
 * copy_process() gave it and resume it with SIGSEGV. Returns 1 when the
 * mask was changed, 0 when PTRACE_SETSIGMASK was refused (a user worker
 * keeps its mask and the SIGSEGV stays pending), -1 on any other failure.
 */
static int inject_coredump_signal(pid_t pid, pid_t tid)
{
	__u64 mask = 0;
	int status, ret = 1;

	if (ptrace(PTRACE_SEIZE, tid, NULL, NULL))
		return -1;
	if (syscall(SYS_tgkill, pid, tid, SIGSTOP))
		return -1;
	if (waitpid(tid, &status, __WALL) != tid)
		return -1;
	if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP)
		return -1;
	if (ptrace(PTRACE_SETSIGMASK, tid, sizeof(mask), &mask)) {
		if (errno != EPERM)
			return -1;
		ret = 0;
	}
	if (ptrace(PTRACE_DETACH, tid, NULL, (void *)(long)SIGSEGV))
		return -1;
	return ret;
}

/* Reap @pid within @timeout_ms, -1 when it is still there. */
static int wait_exit(pid_t pid, int *status, int timeout_ms)
{
	int i;

	for (i = 0; i < timeout_ms / 10; i++) {
		pid_t ret = waitpid(pid, status, WNOHANG);

		if (ret == pid)
			return 0;
		if (ret < 0)
			return -1;
		usleep(10 * 1000);
	}
	return -1;
}

static void log_threads(struct __test_metadata *const _metadata, pid_t pid)
{
	char path[64], line[256], comm[64] = {};
	struct dirent *de;
	DIR *dir;
	FILE *f;

	snprintf(path, sizeof(path), "/proc/%d/task", pid);
	dir = opendir(path);
	if (!dir)
		return;
	while ((de = readdir(dir))) {
		if (!isdigit(de->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "/proc/%d/task/%s/status", pid, de->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\n")] = '\0';
			if (!strncmp(line, "Name:", 5))
				snprintf(comm, sizeof(comm), "%s", line + 6);
			else if (!strncmp(line, "State:", 6))
				TH_LOG("tid %s (%s) %s", de->d_name, comm, line + 7);
		}
		fclose(f);
	}
	closedir(dir);
}

enum dumper {
	DUMPER_MAIN,
	DUMPER_WORKER,
	DUMPER_SQPOLL,
};

static void run_dumper(struct __test_metadata *const _metadata, bool sqpoll,
		       enum dumper dumper)
{
	bool killed = false;
	char path[64], c;
	int ipc[2], status, fd, ret;
	pid_t pid, tid;

	ASSERT_TRUE(set_core_pattern("/tmp/coredump.file.%p"));
	ASSERT_EQ(pipe2(ipc, O_CLOEXEC), 0);

	pid = fork();
	ASSERT_GE(pid, 0);
	if (pid == 0) {
		close(ipc[0]);
		worker_child(sqpoll, ipc[1]);
	}
	close(ipc[1]);
	ASSERT_EQ(read_nointr(ipc[0], &c, 1), 1);
	close(ipc[0]);

	switch (dumper) {
	case DUMPER_MAIN:
		tid = pid;
		break;
	case DUMPER_WORKER:
		tid = find_thread(pid, "iou-wrk-");
		break;
	case DUMPER_SQPOLL:
		tid = find_thread(pid, "iou-sqp-");
		break;
	}
	ASSERT_GT(tid, 0);
	ret = inject_coredump_signal(pid, tid);
	ASSERT_GE(ret, 0);
	if (!ret) {
		/* The signal sits on the worker, the group must be untouched. */
		ASSERT_NE(dumper, DUMPER_MAIN);
		TH_LOG("PTRACE_SETSIGMASK refused for tid %d, the SIGSEGV stays pending", tid);
		ASSERT_EQ(wait_exit(pid, &status, 1000), -1);
		kill(pid, SIGKILL);
		ASSERT_EQ(wait_exit(pid, &status, EXIT_TIMEOUT_MS), 0);
		ASSERT_TRUE(WIFSIGNALED(status));
		ASSERT_EQ(WTERMSIG(status), SIGKILL);
		return;
	}

	if (wait_exit(pid, &status, EXIT_TIMEOUT_MS)) {
		/* No dump. Whatever happened, SIGKILL must still work. */
		log_threads(_metadata, pid);
		kill(pid, SIGKILL);
		killed = true;
		ASSERT_EQ(wait_exit(pid, &status, EXIT_TIMEOUT_MS), 0) {
			TH_LOG("thread group %d is stuck after SIGSEGV to tid %d",
			       pid, tid);
		}
	}

	ASSERT_TRUE(WIFSIGNALED(status));
	if (killed) {
		TH_LOG("tid %d did not dump, the group was killed instead", tid);
		ASSERT_EQ(WTERMSIG(status), SIGKILL);
		return;
	}
	ASSERT_EQ(WTERMSIG(status), SIGSEGV);
	ASSERT_TRUE(WCOREDUMP(status));

	snprintf(path, sizeof(path), "/tmp/coredump.file.%d", pid);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	unlink(path);
	ASSERT_GE(fd, 0);
	ASSERT_TRUE(check_coredump_extent(fd));
	close(fd);
}

/* The mechanics: an injected SIGSEGV into a normal thread dumps core. */
TEST_F(coredump, main_thread_dumper)
{
	if (!uring_available(0))
		SKIP(return, "io_uring is not available");
	run_dumper(_metadata, false, DUMPER_MAIN);
}

TEST_F(coredump, plain_worker_dumper)
{
	if (!uring_available(0))
		SKIP(return, "io_uring is not available");
	run_dumper(_metadata, false, DUMPER_WORKER);
}

TEST_F(coredump, sqpoll_thread_dumper)
{
	if (!uring_available(IORING_SETUP_SQPOLL))
		SKIP(return, "io_uring SQPOLL is not available");
	run_dumper(_metadata, true, DUMPER_SQPOLL);
}

/*
 * The SQPOLL thread leaves its loop on the zap and waits for its io-wq
 * workers to exit before it parks. The dumping worker never does.
 */
TEST_F(coredump, sqpoll_worker_dumper)
{
	if (!uring_available(IORING_SETUP_SQPOLL))
		SKIP(return, "io_uring SQPOLL is not available");
	run_dumper(_metadata, true, DUMPER_WORKER);
}

TEST_HARNESS_MAIN
