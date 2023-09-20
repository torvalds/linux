// SPDX-License-Identifier: GPL-2.0
/*
 *
 * sched-messaging.c
 *
 * messaging: Benchmark for scheduler and IPC mechanisms
 *
 * Based on hackbench by Rusty Russell <rusty@rustcorp.com.au>
 * Ported to perf by Hitoshi Mitake <mitake@dcl.info.waseda.ac.jp>
 *
 */

#include <subcmd/parse-options.h>
#include "bench.h"

/* Test groups of 20 processes spraying to 20 receivers */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <poll.h>
#include <limits.h>
#include <err.h>
#include <linux/list.h>
#include <linux/time64.h>

#define DATASIZE 100

static bool use_pipes = false;
static unsigned int nr_loops = 100;
static bool thread_mode = false;
static unsigned int num_groups = 10;
static struct list_head sender_contexts = LIST_HEAD_INIT(sender_contexts);
static struct list_head receiver_contexts = LIST_HEAD_INIT(receiver_contexts);

struct sender_context {
	struct list_head list;
	unsigned int num_fds;
	int ready_out;
	int wakefd;
	int out_fds[];
};

struct receiver_context {
	struct list_head list;
	unsigned int num_packets;
	int in_fds[2];
	int ready_out;
	int wakefd;
};

static void fdpair(int fds[2])
{
	if (use_pipes) {
		if (pipe(fds) == 0)
			return;
	} else {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0)
			return;
	}

	err(EXIT_FAILURE, use_pipes ? "pipe()" : "socketpair()");
}

/* Block until we're ready to go */
static void ready(int ready_out, int wakefd)
{
	struct pollfd pollfd = { .fd = wakefd, .events = POLLIN };

	/* Tell them we're ready. */
	if (write(ready_out, "R", 1) != 1)
		err(EXIT_FAILURE, "CLIENT: ready write");

	/* Wait for "GO" signal */
	if (poll(&pollfd, 1, -1) != 1)
		err(EXIT_FAILURE, "poll");
}

/* Sender sprays nr_loops messages down each file descriptor */
static void *sender(struct sender_context *ctx)
{
	char data[DATASIZE];
	unsigned int i, j;

	ready(ctx->ready_out, ctx->wakefd);
	memset(data, 'S', sizeof(data));

	/* Now pump to every receiver. */
	for (i = 0; i < nr_loops; i++) {
		for (j = 0; j < ctx->num_fds; j++) {
			int ret, done = 0;

again:
			ret = write(ctx->out_fds[j], data + done,
				    sizeof(data)-done);
			if (ret < 0)
				err(EXIT_FAILURE, "SENDER: write");
			done += ret;
			if (done < DATASIZE)
				goto again;
		}
	}

	return NULL;
}


/* One receiver per fd */
static void *receiver(struct receiver_context* ctx)
{
	unsigned int i;

	if (!thread_mode)
		close(ctx->in_fds[1]);

	/* Wait for start... */
	ready(ctx->ready_out, ctx->wakefd);

	/* Receive them all */
	for (i = 0; i < ctx->num_packets; i++) {
		char data[DATASIZE];
		int ret, done = 0;

again:
		ret = read(ctx->in_fds[0], data + done, DATASIZE - done);
		if (ret < 0)
			err(EXIT_FAILURE, "SERVER: read");
		done += ret;
		if (done < DATASIZE)
			goto again;
	}

	return NULL;
}

static pthread_t create_worker(void *ctx, void *(*func)(void *))
{
	pthread_attr_t attr;
	pthread_t childid;
	int ret;

	if (!thread_mode) {
		/* process mode */
		/* Fork the receiver. */
		switch (fork()) {
		case -1:
			err(EXIT_FAILURE, "fork()");
			break;
		case 0:
			(*func) (ctx);
			exit(0);
			break;
		default:
			break;
		}

		return (pthread_t)0;
	}

	if (pthread_attr_init(&attr) != 0)
		err(EXIT_FAILURE, "pthread_attr_init:");

#ifndef __ia64__
	if (pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN) != 0)
		err(EXIT_FAILURE, "pthread_attr_setstacksize");
#endif

	ret = pthread_create(&childid, &attr, func, ctx);
	if (ret != 0)
		err(EXIT_FAILURE, "pthread_create failed");

	pthread_attr_destroy(&attr);
	return childid;
}

static void reap_worker(pthread_t id)
{
	int proc_status;
	void *thread_status;

	if (!thread_mode) {
		/* process mode */
		wait(&proc_status);
		if (!WIFEXITED(proc_status))
			exit(1);
	} else {
		pthread_join(id, &thread_status);
	}
}

/* One group of senders and receivers */
static unsigned int group(pthread_t *pth,
		unsigned int num_fds,
		int ready_out,
		int wakefd)
{
	unsigned int i;
	struct sender_context *snd_ctx = malloc(sizeof(struct sender_context)
			+ num_fds * sizeof(int));

	if (!snd_ctx)
		err(EXIT_FAILURE, "malloc()");

	list_add(&snd_ctx->list, &sender_contexts);
	for (i = 0; i < num_fds; i++) {
		int fds[2];
		struct receiver_context *ctx = malloc(sizeof(*ctx));

		if (!ctx)
			err(EXIT_FAILURE, "malloc()");

		list_add(&ctx->list, &receiver_contexts);

		/* Create the pipe between client and server */
		fdpair(fds);

		ctx->num_packets = num_fds * nr_loops;
		ctx->in_fds[0] = fds[0];
		ctx->in_fds[1] = fds[1];
		ctx->ready_out = ready_out;
		ctx->wakefd = wakefd;

		pth[i] = create_worker(ctx, (void *)receiver);

		snd_ctx->out_fds[i] = fds[1];
		if (!thread_mode)
			close(fds[0]);
	}

	/* Now we have all the fds, fork the senders */
	for (i = 0; i < num_fds; i++) {
		snd_ctx->ready_out = ready_out;
		snd_ctx->wakefd = wakefd;
		snd_ctx->num_fds = num_fds;

		pth[num_fds+i] = create_worker(snd_ctx, (void *)sender);
	}

	/* Close the fds we have left */
	if (!thread_mode)
		for (i = 0; i < num_fds; i++)
			close(snd_ctx->out_fds[i]);

	/* Return number of children to reap */
	return num_fds * 2;
}

static const struct option options[] = {
	OPT_BOOLEAN('p', "pipe", &use_pipes,
		    "Use pipe() instead of socketpair()"),
	OPT_BOOLEAN('t', "thread", &thread_mode,
		    "Be multi thread instead of multi process"),
	OPT_UINTEGER('g', "group", &num_groups, "Specify number of groups"),
	OPT_UINTEGER('l', "nr_loops", &nr_loops, "Specify the number of loops to run (default: 100)"),
	OPT_END()
};

static const char * const bench_sched_message_usage[] = {
	"perf bench sched messaging <options>",
	NULL
};

int bench_sched_messaging(int argc, const char **argv)
{
	unsigned int i, total_children;
	struct timeval start, stop, diff;
	unsigned int num_fds = 20;
	int readyfds[2], wakefds[2];
	char dummy;
	pthread_t *pth_tab;
	struct sender_context *pos, *n;

	argc = parse_options(argc, argv, options,
			     bench_sched_message_usage, 0);

	pth_tab = malloc(num_fds * 2 * num_groups * sizeof(pthread_t));
	if (!pth_tab)
		err(EXIT_FAILURE, "main:malloc()");

	fdpair(readyfds);
	fdpair(wakefds);

	total_children = 0;
	for (i = 0; i < num_groups; i++)
		total_children += group(pth_tab+total_children, num_fds,
					readyfds[1], wakefds[0]);

	/* Wait for everyone to be ready */
	for (i = 0; i < total_children; i++)
		if (read(readyfds[0], &dummy, 1) != 1)
			err(EXIT_FAILURE, "Reading for readyfds");

	gettimeofday(&start, NULL);

	/* Kick them off */
	if (write(wakefds[1], &dummy, 1) != 1)
		err(EXIT_FAILURE, "Writing to start them");

	/* Reap them all */
	for (i = 0; i < total_children; i++)
		reap_worker(pth_tab[i]);

	gettimeofday(&stop, NULL);

	timersub(&stop, &start, &diff);

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		printf("# %d sender and receiver %s per group\n",
		       num_fds, thread_mode ? "threads" : "processes");
		printf("# %d groups == %d %s run\n\n",
		       num_groups, num_groups * 2 * num_fds,
		       thread_mode ? "threads" : "processes");
		printf(" %14s: %lu.%03lu [sec]\n", "Total time",
		       (unsigned long) diff.tv_sec,
		       (unsigned long) (diff.tv_usec / USEC_PER_MSEC));
		break;
	case BENCH_FORMAT_SIMPLE:
		printf("%lu.%03lu\n", (unsigned long) diff.tv_sec,
		       (unsigned long) (diff.tv_usec / USEC_PER_MSEC));
		break;
	default:
		/* reaching here is something disaster */
		fprintf(stderr, "Unknown format:%d\n", bench_format);
		exit(1);
		break;
	}

	free(pth_tab);
	list_for_each_entry_safe(pos, n, &sender_contexts, list) {
		list_del_init(&pos->list);
		free(pos);
	}
	list_for_each_entry_safe(pos, n, &receiver_contexts, list) {
		list_del_init(&pos->list);
		free(pos);
	}
	return 0;
}
