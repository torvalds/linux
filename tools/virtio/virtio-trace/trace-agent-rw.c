// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read/write thread of a guest agent for virtio-trace
 *
 * Copyright (C) 2012 Hitachi, Ltd.
 * Created by Yoshihiro Yunomae <yoshihiro.yunomae.ez@hitachi.com>
 *            Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "trace-agent.h"

#define READ_WAIT_USEC	100000

void *rw_thread_info_new(void)
{
	struct rw_thread_info *rw_ti;

	rw_ti = zalloc(sizeof(struct rw_thread_info));
	if (rw_ti == NULL) {
		pr_err("rw_thread_info zalloc error\n");
		exit(EXIT_FAILURE);
	}

	rw_ti->cpu_num = -1;
	rw_ti->in_fd = -1;
	rw_ti->out_fd = -1;
	rw_ti->read_pipe = -1;
	rw_ti->write_pipe = -1;
	rw_ti->pipe_size = PIPE_INIT;

	return rw_ti;
}

void *rw_thread_init(int cpu, const char *in_path, const char *out_path,
				bool stdout_flag, unsigned long pipe_size,
				struct rw_thread_info *rw_ti)
{
	int data_pipe[2];

	rw_ti->cpu_num = cpu;

	/* set read(input) fd */
	rw_ti->in_fd = open(in_path, O_RDONLY);
	if (rw_ti->in_fd == -1) {
		pr_err("Could not open in_fd (CPU:%d)\n", cpu);
		goto error;
	}

	/* set write(output) fd */
	if (!stdout_flag) {
		/* virtio-serial output mode */
		rw_ti->out_fd = open(out_path, O_WRONLY);
		if (rw_ti->out_fd == -1) {
			pr_err("Could not open out_fd (CPU:%d)\n", cpu);
			goto error;
		}
	} else
		/* stdout mode */
		rw_ti->out_fd = STDOUT_FILENO;

	if (pipe2(data_pipe, O_NONBLOCK) < 0) {
		pr_err("Could not create pipe in rw-thread(%d)\n", cpu);
		goto error;
	}

	/*
	 * Size of pipe is 64kB in default based on fs/pipe.c.
	 * To read/write trace data speedy, pipe size is changed.
	 */
	if (fcntl(*data_pipe, F_SETPIPE_SZ, pipe_size) < 0) {
		pr_err("Could not change pipe size in rw-thread(%d)\n", cpu);
		goto error;
	}

	rw_ti->read_pipe = data_pipe[1];
	rw_ti->write_pipe = data_pipe[0];
	rw_ti->pipe_size = pipe_size;

	return NULL;

error:
	exit(EXIT_FAILURE);
}

/* Bind a thread to a cpu */
static void bind_cpu(int cpu_num)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu_num, &mask);

	/* bind my thread to cpu_num by assigning zero to the first argument */
	if (sched_setaffinity(0, sizeof(mask), &mask) == -1)
		pr_err("Could not set CPU#%d affinity\n", (int)cpu_num);
}

static void *rw_thread_main(void *thread_info)
{
	ssize_t rlen, wlen;
	ssize_t ret;
	struct rw_thread_info *ts = (struct rw_thread_info *)thread_info;

	bind_cpu(ts->cpu_num);

	while (1) {
		/* Wait for a read order of trace data by Host OS */
		if (!global_run_operation) {
			pthread_mutex_lock(&mutex_notify);
			pthread_cond_wait(&cond_wakeup, &mutex_notify);
			pthread_mutex_unlock(&mutex_notify);
		}

		if (global_sig_receive)
			break;

		/*
		 * Each thread read trace_pipe_raw of each cpu bounding the
		 * thread, so contention of multi-threads does not occur.
		 */
		rlen = splice(ts->in_fd, NULL, ts->read_pipe, NULL,
				ts->pipe_size, SPLICE_F_MOVE | SPLICE_F_MORE);

		if (rlen < 0) {
			pr_err("Splice_read in rw-thread(%d)\n", ts->cpu_num);
			goto error;
		} else if (rlen == 0) {
			/*
			 * If trace data do not exist or are unreadable not
			 * for exceeding the page size, splice_read returns
			 * NULL. Then, this waits for being filled the data in a
			 * ring-buffer.
			 */
			usleep(READ_WAIT_USEC);
			pr_debug("Read retry(cpu:%d)\n", ts->cpu_num);
			continue;
		}

		wlen = 0;

		do {
			ret = splice(ts->write_pipe, NULL, ts->out_fd, NULL,
					rlen - wlen,
					SPLICE_F_MOVE | SPLICE_F_MORE);

			if (ret < 0) {
				pr_err("Splice_write in rw-thread(%d)\n",
								ts->cpu_num);
				goto error;
			} else if (ret == 0)
				/*
				 * When host reader is not in time for reading
				 * trace data, guest will be stopped. This is
				 * because char dev in QEMU is not supported
				 * non-blocking mode. Then, writer might be
				 * sleep in that case.
				 * This sleep will be removed by supporting
				 * non-blocking mode.
				 */
				sleep(1);
			wlen += ret;
		} while (wlen < rlen);
	}

	return NULL;

error:
	exit(EXIT_FAILURE);
}


pthread_t rw_thread_run(struct rw_thread_info *rw_ti)
{
	int ret;
	pthread_t rw_thread_per_cpu;

	ret = pthread_create(&rw_thread_per_cpu, NULL, rw_thread_main, rw_ti);
	if (ret != 0) {
		pr_err("Could not create a rw thread(%d)\n", rw_ti->cpu_num);
		exit(EXIT_FAILURE);
	}

	return rw_thread_per_cpu;
}
