/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRACE_AGENT_H__
#define __TRACE_AGENT_H__
#include <pthread.h>
#include <stdbool.h>

#define MAX_CPUS	256
#define PIPE_INIT       (1024*1024)

/*
 * agent_info - structure managing total information of guest agent
 * @pipe_size:	size of pipe (default 1MB)
 * @use_stdout:	set to true when o option is added (default false)
 * @cpus:	total number of CPUs
 * @ctl_fd:	fd of control path, /dev/virtio-ports/agent-ctl-path
 * @rw_ti:	structure managing information of read/write threads
 */
struct agent_info {
	unsigned long pipe_size;
	bool use_stdout;
	int cpus;
	int ctl_fd;
	struct rw_thread_info *rw_ti[MAX_CPUS];
};

/*
 * rw_thread_info - structure managing a read/write thread a cpu
 * @cpu_num:	cpu number operating this read/write thread
 * @in_fd:	fd of reading trace data path in cpu_num
 * @out_fd:	fd of writing trace data path in cpu_num
 * @read_pipe:	fd of read pipe
 * @write_pipe:	fd of write pipe
 * @pipe_size:	size of pipe (default 1MB)
 */
struct rw_thread_info {
	int cpu_num;
	int in_fd;
	int out_fd;
	int read_pipe;
	int write_pipe;
	unsigned long pipe_size;
};

/* use for stopping rw threads */
extern bool global_sig_receive;

/* use for notification */
extern bool global_run_operation;
extern pthread_mutex_t mutex_notify;
extern pthread_cond_t cond_wakeup;

/* for controller of read/write threads */
extern int rw_ctl_init(const char *ctl_path);
extern void *rw_ctl_loop(int ctl_fd);

/* for trace read/write thread */
extern void *rw_thread_info_new(void);
extern void *rw_thread_init(int cpu, const char *in_path, const char *out_path,
			bool stdout_flag, unsigned long pipe_size,
			struct rw_thread_info *rw_ti);
extern pthread_t rw_thread_run(struct rw_thread_info *rw_ti);

static inline void *zalloc(size_t size)
{
	return calloc(1, size);
}

#define pr_err(format, ...) fprintf(stderr, format, ## __VA_ARGS__)
#define pr_info(format, ...) fprintf(stdout, format, ## __VA_ARGS__)
#ifdef DEBUG
#define pr_debug(format, ...) fprintf(stderr, format, ## __VA_ARGS__)
#else
#define pr_debug(format, ...) do {} while (0)
#endif

#endif /*__TRACE_AGENT_H__*/
