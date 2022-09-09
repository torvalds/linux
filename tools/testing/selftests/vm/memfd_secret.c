// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corporation, 2021
 *
 * Author: Mike Rapoport <rppt@linux.ibm.com>
 */

#define _GNU_SOURCE
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/capability.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "../kselftest.h"

#define fail(fmt, ...) ksft_test_result_fail(fmt, ##__VA_ARGS__)
#define pass(fmt, ...) ksft_test_result_pass(fmt, ##__VA_ARGS__)
#define skip(fmt, ...) ksft_test_result_skip(fmt, ##__VA_ARGS__)

#ifdef __NR_memfd_secret

#define PATTERN	0x55

static const int prot = PROT_READ | PROT_WRITE;
static const int mode = MAP_SHARED;

static unsigned long page_size;
static unsigned long mlock_limit_cur;
static unsigned long mlock_limit_max;

static int memfd_secret(unsigned int flags)
{
	return syscall(__NR_memfd_secret, flags);
}

static void test_file_apis(int fd)
{
	char buf[64];

	if ((read(fd, buf, sizeof(buf)) >= 0) ||
	    (write(fd, buf, sizeof(buf)) >= 0) ||
	    (pread(fd, buf, sizeof(buf), 0) >= 0) ||
	    (pwrite(fd, buf, sizeof(buf), 0) >= 0))
		fail("unexpected file IO\n");
	else
		pass("file IO is blocked as expected\n");
}

static void test_mlock_limit(int fd)
{
	size_t len;
	char *mem;

	len = mlock_limit_cur;
	mem = mmap(NULL, len, prot, mode, fd, 0);
	if (mem == MAP_FAILED) {
		fail("unable to mmap secret memory\n");
		return;
	}
	munmap(mem, len);

	len = mlock_limit_max * 2;
	mem = mmap(NULL, len, prot, mode, fd, 0);
	if (mem != MAP_FAILED) {
		fail("unexpected mlock limit violation\n");
		munmap(mem, len);
		return;
	}

	pass("mlock limit is respected\n");
}

static void try_process_vm_read(int fd, int pipefd[2])
{
	struct iovec liov, riov;
	char buf[64];
	char *mem;

	if (read(pipefd[0], &mem, sizeof(mem)) < 0) {
		fail("pipe write: %s\n", strerror(errno));
		exit(KSFT_FAIL);
	}

	liov.iov_len = riov.iov_len = sizeof(buf);
	liov.iov_base = buf;
	riov.iov_base = mem;

	if (process_vm_readv(getppid(), &liov, 1, &riov, 1, 0) < 0) {
		if (errno == ENOSYS)
			exit(KSFT_SKIP);
		exit(KSFT_PASS);
	}

	exit(KSFT_FAIL);
}

static void try_ptrace(int fd, int pipefd[2])
{
	pid_t ppid = getppid();
	int status;
	char *mem;
	long ret;

	if (read(pipefd[0], &mem, sizeof(mem)) < 0) {
		perror("pipe write");
		exit(KSFT_FAIL);
	}

	ret = ptrace(PTRACE_ATTACH, ppid, 0, 0);
	if (ret) {
		perror("ptrace_attach");
		exit(KSFT_FAIL);
	}

	ret = waitpid(ppid, &status, WUNTRACED);
	if ((ret != ppid) || !(WIFSTOPPED(status))) {
		fprintf(stderr, "weird waitppid result %ld stat %x\n",
			ret, status);
		exit(KSFT_FAIL);
	}

	if (ptrace(PTRACE_PEEKDATA, ppid, mem, 0))
		exit(KSFT_PASS);

	exit(KSFT_FAIL);
}

static void check_child_status(pid_t pid, const char *name)
{
	int status;

	waitpid(pid, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) == KSFT_SKIP) {
		skip("%s is not supported\n", name);
		return;
	}

	if ((WIFEXITED(status) && WEXITSTATUS(status) == KSFT_PASS) ||
	    WIFSIGNALED(status)) {
		pass("%s is blocked as expected\n", name);
		return;
	}

	fail("%s: unexpected memory access\n", name);
}

static void test_remote_access(int fd, const char *name,
			       void (*func)(int fd, int pipefd[2]))
{
	int pipefd[2];
	pid_t pid;
	char *mem;

	if (pipe(pipefd)) {
		fail("pipe failed: %s\n", strerror(errno));
		return;
	}

	pid = fork();
	if (pid < 0) {
		fail("fork failed: %s\n", strerror(errno));
		return;
	}

	if (pid == 0) {
		func(fd, pipefd);
		return;
	}

	mem = mmap(NULL, page_size, prot, mode, fd, 0);
	if (mem == MAP_FAILED) {
		fail("Unable to mmap secret memory\n");
		return;
	}

	ftruncate(fd, page_size);
	memset(mem, PATTERN, page_size);

	if (write(pipefd[1], &mem, sizeof(mem)) < 0) {
		fail("pipe write: %s\n", strerror(errno));
		return;
	}

	check_child_status(pid, name);
}

static void test_process_vm_read(int fd)
{
	test_remote_access(fd, "process_vm_read", try_process_vm_read);
}

static void test_ptrace(int fd)
{
	test_remote_access(fd, "ptrace", try_ptrace);
}

static int set_cap_limits(rlim_t max)
{
	struct rlimit new;
	cap_t cap = cap_init();

	new.rlim_cur = max;
	new.rlim_max = max;
	if (setrlimit(RLIMIT_MEMLOCK, &new)) {
		perror("setrlimit() returns error");
		return -1;
	}

	/* drop capabilities including CAP_IPC_LOCK */
	if (cap_set_proc(cap)) {
		perror("cap_set_proc() returns error");
		return -2;
	}

	return 0;
}

static void prepare(void)
{
	struct rlimit rlim;

	page_size = sysconf(_SC_PAGE_SIZE);
	if (!page_size)
		ksft_exit_fail_msg("Failed to get page size %s\n",
				   strerror(errno));

	if (getrlimit(RLIMIT_MEMLOCK, &rlim))
		ksft_exit_fail_msg("Unable to detect mlock limit: %s\n",
				   strerror(errno));

	mlock_limit_cur = rlim.rlim_cur;
	mlock_limit_max = rlim.rlim_max;

	printf("page_size: %ld, mlock.soft: %ld, mlock.hard: %ld\n",
	       page_size, mlock_limit_cur, mlock_limit_max);

	if (page_size > mlock_limit_cur)
		mlock_limit_cur = page_size;
	if (page_size > mlock_limit_max)
		mlock_limit_max = page_size;

	if (set_cap_limits(mlock_limit_max))
		ksft_exit_fail_msg("Unable to set mlock limit: %s\n",
				   strerror(errno));
}

#define NUM_TESTS 4

int main(int argc, char *argv[])
{
	int fd;

	prepare();

	ksft_print_header();
	ksft_set_plan(NUM_TESTS);

	fd = memfd_secret(0);
	if (fd < 0) {
		if (errno == ENOSYS)
			ksft_exit_skip("memfd_secret is not supported\n");
		else
			ksft_exit_fail_msg("memfd_secret failed: %s\n",
					   strerror(errno));
	}

	test_mlock_limit(fd);
	test_file_apis(fd);
	test_process_vm_read(fd);
	test_ptrace(fd);

	close(fd);

	ksft_finished();
}

#else /* __NR_memfd_secret */

int main(int argc, char *argv[])
{
	printf("skip: skipping memfd_secret test (missing __NR_memfd_secret)\n");
	return KSFT_SKIP;
}

#endif /* __NR_memfd_secret */
