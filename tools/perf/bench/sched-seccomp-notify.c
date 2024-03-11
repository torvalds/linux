// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include "bench.h"

#include <uapi/linux/filter.h>
#include <sys/types.h>
#include <sys/time.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/time64.h>
#include <uapi/linux/seccomp.h>
#include <sys/prctl.h>

#include <unistd.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <inttypes.h>

#define LOOPS_DEFAULT 1000000UL
static uint64_t loops = LOOPS_DEFAULT;
static bool sync_mode;

static const struct option options[] = {
	OPT_U64('l', "loop",	&loops,		"Specify number of loops"),
	OPT_BOOLEAN('s', "sync-mode", &sync_mode,
		    "Enable the synchronous mode for seccomp notifications"),
	OPT_END()
};

static const char * const bench_seccomp_usage[] = {
	"perf bench sched secccomp-notify <options>",
	NULL
};

static int seccomp(unsigned int op, unsigned int flags, void *args)
{
	return syscall(__NR_seccomp, op, flags, args);
}

static int user_notif_syscall(int nr, unsigned int flags)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, nr, 0, 1),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_USER_NOTIF),
		BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
	};

	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	return seccomp(SECCOMP_SET_MODE_FILTER, flags, &prog);
}

#define USER_NOTIF_MAGIC INT_MAX
static void user_notification_sync_loop(int listener)
{
	struct seccomp_notif_resp resp;
	struct seccomp_notif req;
	uint64_t nr;

	for (nr = 0; nr < loops; nr++) {
		memset(&req, 0, sizeof(req));
		if (ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, &req))
			err(EXIT_FAILURE, "SECCOMP_IOCTL_NOTIF_RECV failed");

		if (req.data.nr != __NR_gettid)
			errx(EXIT_FAILURE, "unexpected syscall: %d", req.data.nr);

		resp.id = req.id;
		resp.error = 0;
		resp.val = USER_NOTIF_MAGIC;
		resp.flags = 0;
		if (ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, &resp))
			err(EXIT_FAILURE, "SECCOMP_IOCTL_NOTIF_SEND failed");
	}
}

#ifndef SECCOMP_USER_NOTIF_FD_SYNC_WAKE_UP
#define SECCOMP_USER_NOTIF_FD_SYNC_WAKE_UP (1UL << 0)
#define SECCOMP_IOCTL_NOTIF_SET_FLAGS  SECCOMP_IOW(4, __u64)
#endif
int bench_sched_seccomp_notify(int argc, const char **argv)
{
	struct timeval start, stop, diff;
	unsigned long long result_usec = 0;
	int status, listener;
	pid_t pid;
	long ret;

	argc = parse_options(argc, argv, options, bench_seccomp_usage, 0);

	gettimeofday(&start, NULL);

	prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	listener = user_notif_syscall(__NR_gettid,
				      SECCOMP_FILTER_FLAG_NEW_LISTENER);
	if (listener < 0)
		err(EXIT_FAILURE, "can't create a notification descriptor");

	pid = fork();
	if (pid < 0)
		err(EXIT_FAILURE, "fork");
	if (pid == 0) {
		if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0))
			err(EXIT_FAILURE, "can't set the parent death signal");
		while (1) {
			ret = syscall(__NR_gettid);
			if (ret == USER_NOTIF_MAGIC)
				continue;
			break;
		}
		_exit(1);
	}

	if (sync_mode) {
		if (ioctl(listener, SECCOMP_IOCTL_NOTIF_SET_FLAGS,
			     SECCOMP_USER_NOTIF_FD_SYNC_WAKE_UP, 0))
			err(EXIT_FAILURE,
			    "can't set SECCOMP_USER_NOTIF_FD_SYNC_WAKE_UP");
	}
	user_notification_sync_loop(listener);

	kill(pid, SIGKILL);
	if (waitpid(pid, &status, 0) != pid)
		err(EXIT_FAILURE, "waitpid(%d) failed", pid);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
		errx(EXIT_FAILURE, "unexpected exit code: %d", status);

	gettimeofday(&stop, NULL);
	timersub(&stop, &start, &diff);

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		printf("# Executed %" PRIu64 " system calls\n\n",
			loops);

		result_usec = diff.tv_sec * USEC_PER_SEC;
		result_usec += diff.tv_usec;

		printf(" %14s: %lu.%03lu [sec]\n\n", "Total time",
		       (unsigned long) diff.tv_sec,
		       (unsigned long) (diff.tv_usec / USEC_PER_MSEC));

		printf(" %14lf usecs/op\n",
		       (double)result_usec / (double)loops);
		printf(" %14d ops/sec\n",
		       (int)((double)loops /
			     ((double)result_usec / (double)USEC_PER_SEC)));
		break;

	case BENCH_FORMAT_SIMPLE:
		printf("%lu.%03lu\n",
		       (unsigned long) diff.tv_sec,
		       (unsigned long) (diff.tv_usec / USEC_PER_MSEC));
		break;

	default:
		/* reaching here is something disaster */
		fprintf(stderr, "Unknown format:%d\n", bench_format);
		exit(1);
		break;
	}

	return 0;
}
