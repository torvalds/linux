#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <linux/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/mman.h>

#include "linux/ptrace.h"

static int sys_rt_sigqueueinfo(pid_t tgid, int sig, siginfo_t *uinfo)
{
	return syscall(SYS_rt_sigqueueinfo, tgid, sig, uinfo);
}

static int sys_rt_tgsigqueueinfo(pid_t tgid, pid_t tid,
					int sig, siginfo_t *uinfo)
{
	return syscall(SYS_rt_tgsigqueueinfo, tgid, tid, sig, uinfo);
}

static int sys_ptrace(int request, pid_t pid, void *addr, void *data)
{
	return syscall(SYS_ptrace, request, pid, addr, data);
}

#define SIGNR 10
#define TEST_SICODE_PRIV	-1
#define TEST_SICODE_SHARE	-2

#ifndef PAGE_SIZE
#define PAGE_SIZE sysconf(_SC_PAGESIZE)
#endif

#define err(fmt, ...)						\
		fprintf(stderr,					\
			"Error (%s:%d): " fmt,			\
			__FILE__, __LINE__, ##__VA_ARGS__)

static int check_error_paths(pid_t child)
{
	struct ptrace_peeksiginfo_args arg;
	int ret, exit_code = -1;
	void *addr_rw, *addr_ro;

	/*
	 * Allocate two contiguous pages. The first one is for read-write,
	 * another is for read-only.
	 */
	addr_rw = mmap(NULL, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr_rw == MAP_FAILED) {
		err("mmap() failed: %m\n");
		return 1;
	}

	addr_ro = mmap(addr_rw + PAGE_SIZE, PAGE_SIZE, PROT_READ,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (addr_ro == MAP_FAILED) {
		err("mmap() failed: %m\n");
		goto out;
	}

	arg.nr = SIGNR;
	arg.off = 0;

	/* Unsupported flags */
	arg.flags = ~0;
	ret = sys_ptrace(PTRACE_PEEKSIGINFO, child, &arg, addr_rw);
	if (ret != -1 || errno != EINVAL) {
		err("sys_ptrace() returns %d (expected -1),"
				" errno %d (expected %d): %m\n",
				ret, errno, EINVAL);
		goto out;
	}
	arg.flags = 0;

	/* A part of the buffer is read-only */
	ret = sys_ptrace(PTRACE_PEEKSIGINFO, child, &arg,
					addr_ro - sizeof(siginfo_t) * 2);
	if (ret != 2) {
		err("sys_ptrace() returns %d (expected 2): %m\n", ret);
		goto out;
	}

	/* Read-only buffer */
	ret = sys_ptrace(PTRACE_PEEKSIGINFO, child, &arg, addr_ro);
	if (ret != -1 && errno != EFAULT) {
		err("sys_ptrace() returns %d (expected -1),"
				" errno %d (expected %d): %m\n",
				ret, errno, EFAULT);
		goto out;
	}

	exit_code = 0;
out:
	munmap(addr_rw, 2 * PAGE_SIZE);
	return exit_code;
}

int check_direct_path(pid_t child, int shared, int nr)
{
	struct ptrace_peeksiginfo_args arg = {.flags = 0, .nr = nr, .off = 0};
	int i, j, ret, exit_code = -1;
	siginfo_t siginfo[SIGNR];
	int si_code;

	if (shared == 1) {
		arg.flags = PTRACE_PEEKSIGINFO_SHARED;
		si_code = TEST_SICODE_SHARE;
	} else {
		arg.flags = 0;
		si_code = TEST_SICODE_PRIV;
	}

	for (i = 0; i < SIGNR; ) {
		arg.off = i;
		ret = sys_ptrace(PTRACE_PEEKSIGINFO, child, &arg, siginfo);
		if (ret == -1) {
			err("ptrace() failed: %m\n");
			goto out;
		}

		if (ret == 0)
			break;

		for (j = 0; j < ret; j++, i++) {
			if (siginfo[j].si_code == si_code &&
			    siginfo[j].si_int == i)
				continue;

			err("%d: Wrong siginfo i=%d si_code=%d si_int=%d\n",
			     shared, i, siginfo[j].si_code, siginfo[j].si_int);
			goto out;
		}
	}

	if (i != SIGNR) {
		err("Only %d signals were read\n", i);
		goto out;
	}

	exit_code = 0;
out:
	return exit_code;
}

int main(int argc, char *argv[])
{
	siginfo_t siginfo[SIGNR];
	int i, exit_code = 1;
	sigset_t blockmask;
	pid_t child;

	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGRTMIN);
	sigprocmask(SIG_BLOCK, &blockmask, NULL);

	child = fork();
	if (child == -1) {
		err("fork() failed: %m");
		return 1;
	} else if (child == 0) {
		pid_t ppid = getppid();
		while (1) {
			if (ppid != getppid())
				break;
			sleep(1);
		}
		return 1;
	}

	/* Send signals in process-wide and per-thread queues */
	for (i = 0; i < SIGNR; i++) {
		siginfo->si_code = TEST_SICODE_SHARE;
		siginfo->si_int = i;
		sys_rt_sigqueueinfo(child, SIGRTMIN, siginfo);

		siginfo->si_code = TEST_SICODE_PRIV;
		siginfo->si_int = i;
		sys_rt_tgsigqueueinfo(child, child, SIGRTMIN, siginfo);
	}

	if (sys_ptrace(PTRACE_ATTACH, child, NULL, NULL) == -1)
		return 1;

	waitpid(child, NULL, 0);

	/* Dump signals one by one*/
	if (check_direct_path(child, 0, 1))
		goto out;
	/* Dump all signals for one call */
	if (check_direct_path(child, 0, SIGNR))
		goto out;

	/*
	 * Dump signal from the process-wide queue.
	 * The number of signals is not multible to the buffer size
	 */
	if (check_direct_path(child, 1, 3))
		goto out;

	if (check_error_paths(child))
		goto out;

	printf("PASS\n");
	exit_code = 0;
out:
	if (sys_ptrace(PTRACE_KILL, child, NULL, NULL) == -1)
		return 1;

	waitpid(child, NULL, 0);

	return exit_code;
}
