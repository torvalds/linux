// SPDX-License-Identifier: GPL-2.0
/*
 *
 * sched-pipe.c
 *
 * pipe: Benchmark for pipe()
 *
 * Based on pipe-test-1m.c by Ingo Molnar <mingo@redhat.com>
 *  http://people.redhat.com/mingo/cfs-scheduler/tools/pipe-test-1m.c
 * Ported to perf by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 */
#include <subcmd/parse-options.h>
#include <api/fs/fs.h>
#include "bench.h"
#include "util/cgroup.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <linux/time64.h>

#include <pthread.h>

struct thread_data {
	int			nr;
	int			pipe_read;
	int			pipe_write;
	struct epoll_event      epoll_ev;
	int			epoll_fd;
	bool			cgroup_failed;
	pthread_t		pthread;
	char			*buf;
};

#define LOOPS_DEFAULT 1000000
static	int			loops = LOOPS_DEFAULT;

/* Use processes by default: */
static bool			threaded;

static bool			nonblocking;
static unsigned int		write_size = sizeof(int);
static char			*cgrp_names[2];
static struct cgroup		*cgrps[2];

static int parse_two_cgroups(const struct option *opt __maybe_unused,
			     const char *str, int unset __maybe_unused)
{
	char *p = strdup(str);
	char *q;
	int ret = -1;

	if (p == NULL) {
		fprintf(stderr, "memory allocation failure\n");
		return -1;
	}

	q = strchr(p, ',');
	if (q == NULL) {
		fprintf(stderr, "it should have two cgroup names: %s\n", p);
		goto out;
	}
	*q = '\0';

	cgrp_names[0] = strdup(p);
	cgrp_names[1] = strdup(q + 1);

	if (cgrp_names[0] == NULL || cgrp_names[1] == NULL) {
		fprintf(stderr, "memory allocation failure\n");
		goto out;
	}
	ret = 0;

out:
	free(p);
	return ret;
}

static const struct option options[] = {
	OPT_BOOLEAN('n', "nonblocking",	&nonblocking,	"Use non-blocking operations"),
	OPT_INTEGER('l', "loop",	&loops,		"Specify number of loops"),
	OPT_BOOLEAN('T', "threaded",	&threaded,	"Specify threads/process based task setup"),
	OPT_UINTEGER('s', "write-size", &write_size,
		     "Bytes per ping-pong write (default 4-bytes). Use larger values to exercise the pipe page-allocation path."),
	OPT_CALLBACK('G', "cgroups", NULL, "SEND,RECV",
		     "Put sender and receivers in given cgroups",
		     parse_two_cgroups),
	OPT_END()
};

static const char * const bench_sched_pipe_usage[] = {
	"perf bench sched pipe <options>",
	NULL
};

static int enter_cgroup(int nr)
{
	char buf[32];
	int fd, len, ret;
	int saved_errno;
	struct cgroup *cgrp;
	pid_t pid;

	if (cgrp_names[nr] == NULL)
		return 0;

	if (cgrps[nr] == NULL) {
		cgrps[nr] = cgroup__new(cgrp_names[nr], /*do_open=*/true);
		if (cgrps[nr] == NULL)
			goto err;
	}
	cgrp = cgrps[nr];

	if (threaded)
		pid = syscall(__NR_gettid);
	else
		pid = getpid();

	snprintf(buf, sizeof(buf), "%d\n", pid);
	len = strlen(buf);

	/* try cgroup v2 interface first */
	if (threaded)
		fd = openat(cgrp->fd, "cgroup.threads", O_WRONLY);
	else
		fd = openat(cgrp->fd, "cgroup.procs", O_WRONLY);

	/* try cgroup v1 if failed */
	if (fd < 0 && errno == ENOENT)
		fd = openat(cgrp->fd, "tasks", O_WRONLY);

	if (fd < 0)
		goto err;

	ret = write(fd, buf, len);
	close(fd);

	if (ret != len) {
		printf("Cannot enter to cgroup: %s\n", cgrp->name);
		return -1;
	}
	return 0;

err:
	saved_errno = errno;
	printf("Failed to open cgroup file in %s\n", cgrp_names[nr]);

	if (saved_errno == ENOENT) {
		char mnt[PATH_MAX];

		if (cgroupfs_find_mountpoint(mnt, sizeof(mnt), "perf_event") == 0)
			printf(" Hint: create the cgroup first, like 'mkdir %s/%s'\n",
			       mnt, cgrp_names[nr]);
	} else if (saved_errno == EACCES && geteuid() > 0) {
		printf(" Hint: try to run as root\n");
	}

	return -1;
}

static void exit_cgroup(int nr)
{
	cgroup__put(cgrps[nr]);
	free(cgrp_names[nr]);
}

/* Sleep until @fd is writable, so we don't busy-spin on EWOULDBLOCK. */
static inline void wait_writable(int fd)
{
	struct pollfd pfd = {
		.fd	= fd,
		.events	= POLLOUT,
	};

	poll(&pfd, 1, -1);
}

/*
 * Loop on short read()/write(): the kernel may return fewer bytes than
 * requested, retry on EINTR, and in non-blocking mode wait via poll()
 * when the writer transiently hits EWOULDBLOCK while the peer is still
 * draining a full pipe (capacity is sized to write_size).
 */
static inline int write_pipe(struct thread_data *td)
{
	unsigned int done = 0;
	int ret;

	while (done < write_size) {
		ret = write(td->pipe_write, td->buf + done, write_size - done);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			if (nonblocking && errno == EWOULDBLOCK) {
				wait_writable(td->pipe_write);
				continue;
			}
			return ret;
		}
		done += ret;
	}
	return done;
}

static inline int read_pipe(struct thread_data *td)
{
	unsigned int done = 0;
	int ret;

	while (done < write_size) {
		if (nonblocking) {
			ret = epoll_wait(td->epoll_fd, &td->epoll_ev, 1, -1);
			if (ret < 0) {
				if (errno == EINTR)
					continue;
				return ret;
			}
		}
		ret = read(td->pipe_read, td->buf + done, write_size - done);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			if (nonblocking && errno == EWOULDBLOCK)
				continue;
			return ret;
		}
		if (ret == 0)
			return done;
		done += ret;
	}
	return done;
}

static void *worker_thread(void *__tdata)
{
	struct thread_data *td = __tdata;
	int i, ret;

	ret = enter_cgroup(td->nr);
	if (ret < 0) {
		td->cgroup_failed = true;
		return NULL;
	}

	if (nonblocking) {
		td->epoll_ev.events = EPOLLIN;
		td->epoll_fd = epoll_create(1);
		BUG_ON(td->epoll_fd < 0);
		BUG_ON(epoll_ctl(td->epoll_fd, EPOLL_CTL_ADD, td->pipe_read, &td->epoll_ev) < 0);
	}

	for (i = 0; i < loops; i++) {
		ret = write_pipe(td);
		BUG_ON(ret != (int)write_size);
		ret = read_pipe(td);
		BUG_ON(ret != (int)write_size);
	}

	return NULL;
}

/*
 * On a custom write_size, resize the pipes so a single payload fits.
 */
static int resize_pipes(int wfd1, int wfd2)
{
	int r1, r2;

	if (write_size <= sizeof(int))
		return 0;

	r1 = fcntl(wfd1, F_SETPIPE_SZ, write_size);
	r2 = fcntl(wfd2, F_SETPIPE_SZ, write_size);
	if (r1 < 0 || r2 < 0 ||
	    (unsigned int)r1 < write_size ||
	    (unsigned int)r2 < write_size) {
		fprintf(stderr,
			"--write-size %u exceeds /proc/sys/fs/pipe-max-size\n",
			write_size);
		return -1;
	}
	return 0;
}

int bench_sched_pipe(int argc, const char **argv)
{
	struct thread_data threads[2] = {};
	struct thread_data *td;
	int pipe_1[2], pipe_2[2];
	struct timeval start, stop, diff;
	unsigned long long result_usec = 0;
	int nr_threads = 2;
	int t;

	/*
	 * why does "ret" exist?
	 * discarding returned value of read(), write()
	 * causes error in building environment for perf
	 */
	int __maybe_unused ret, wait_stat, flags = 0;
	pid_t pid, retpid __maybe_unused;

	argc = parse_options(argc, argv, options, bench_sched_pipe_usage, 0);

	/*
	 * The error paths below return early without closing the pipes or
	 * freeing the cgroup state. That is fine: bench_sched_pipe() runs
	 * once and the process exits right after it returns, so these are
	 * not real leaks.
	 */
	if (write_size == 0 || write_size > INT_MAX) {
		fprintf(stderr, "--write-size must be in 1..%d\n", INT_MAX);
		return -1;
	}

	if (nonblocking)
		flags |= O_NONBLOCK;

	BUG_ON(pipe2(pipe_1, flags));
	BUG_ON(pipe2(pipe_2, flags));

	if (resize_pipes(pipe_1[1], pipe_2[1]) < 0)
		return -1;

	for (t = 0; t < nr_threads; t++) {
		threads[t].buf = calloc(1, write_size);
		BUG_ON(!threads[t].buf);
	}

	gettimeofday(&start, NULL);

	for (t = 0; t < nr_threads; t++) {
		td = threads + t;

		td->nr = t;

		if (t == 0) {
			td->pipe_read = pipe_1[0];
			td->pipe_write = pipe_2[1];
		} else {
			td->pipe_write = pipe_1[1];
			td->pipe_read = pipe_2[0];
		}
	}

	if (threaded) {
		for (t = 0; t < nr_threads; t++) {
			td = threads + t;

			ret = pthread_create(&td->pthread, NULL, worker_thread, td);
			BUG_ON(ret);
		}

		for (t = 0; t < nr_threads; t++) {
			td = threads + t;

			ret = pthread_join(td->pthread, NULL);
			BUG_ON(ret);
		}
	} else {
		pid = fork();
		assert(pid >= 0);

		if (!pid) {
			worker_thread(threads + 0);
			exit(0);
		} else {
			worker_thread(threads + 1);
		}

		retpid = waitpid(pid, &wait_stat, 0);
		assert((retpid == pid) && WIFEXITED(wait_stat));
	}

	gettimeofday(&stop, NULL);
	timersub(&stop, &start, &diff);

	for (t = 0; t < nr_threads; t++)
		free(threads[t].buf);

	exit_cgroup(0);
	exit_cgroup(1);

	if (threads[0].cgroup_failed || threads[1].cgroup_failed)
		return 0;

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		printf("# Executed %d pipe operations between two %s\n\n",
			loops, threaded ? "threads" : "processes");

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
