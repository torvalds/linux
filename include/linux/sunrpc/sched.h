/*
 * linux/include/linux/sunrpc/sched.h
 *
 * Scheduling primitives for kernel Sun RPC.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_SCHED_H_
#define _LINUX_SUNRPC_SCHED_H_

#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/sunrpc/types.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/xdr.h>

/*
 * This is the actual RPC procedure call info.
 */
struct rpc_procinfo;
struct rpc_message {
	struct rpc_procinfo *	rpc_proc;	/* Procedure information */
	void *			rpc_argp;	/* Arguments */
	void *			rpc_resp;	/* Result */
	struct rpc_cred *	rpc_cred;	/* Credentials */
};

struct rpc_call_ops;
struct rpc_wait_queue;
struct rpc_wait {
	struct list_head	list;		/* wait queue links */
	struct list_head	links;		/* Links to related tasks */
	struct list_head	timer_list;	/* Timer list */
	unsigned long		expires;
};

/*
 * This is the RPC task struct
 */
struct rpc_task {
	atomic_t		tk_count;	/* Reference count */
	struct list_head	tk_task;	/* global list of tasks */
	struct rpc_clnt *	tk_client;	/* RPC client */
	struct rpc_rqst *	tk_rqstp;	/* RPC request */

	/*
	 * RPC call state
	 */
	struct rpc_message	tk_msg;		/* RPC call info */

	/*
	 * callback	to be executed after waking up
	 * action	next procedure for async tasks
	 * tk_ops	caller callbacks
	 */
	void			(*tk_callback)(struct rpc_task *);
	void			(*tk_action)(struct rpc_task *);
	const struct rpc_call_ops *tk_ops;
	void *			tk_calldata;

	unsigned long		tk_timeout;	/* timeout for rpc_sleep() */
	unsigned long		tk_runstate;	/* Task run status */
	struct workqueue_struct	*tk_workqueue;	/* Normally rpciod, but could
						 * be any workqueue
						 */
	struct rpc_wait_queue 	*tk_waitqueue;	/* RPC wait queue we're on */
	union {
		struct work_struct	tk_work;	/* Async task work queue */
		struct rpc_wait		tk_wait;	/* RPC wait */
	} u;

	ktime_t			tk_start;	/* RPC task init timestamp */

	pid_t			tk_owner;	/* Process id for batching tasks */
	int			tk_status;	/* result of last operation */
	unsigned short		tk_flags;	/* misc flags */
	unsigned short		tk_timeouts;	/* maj timeouts */

#if defined(RPC_DEBUG) || defined(RPC_TRACEPOINTS)
	unsigned short		tk_pid;		/* debugging aid */
#endif
	unsigned char		tk_priority : 2,/* Task priority */
				tk_garb_retry : 2,
				tk_cred_retry : 2,
				tk_rebind_retry : 2;
};

typedef void			(*rpc_action)(struct rpc_task *);

struct rpc_call_ops {
	void (*rpc_call_prepare)(struct rpc_task *, void *);
	void (*rpc_call_done)(struct rpc_task *, void *);
	void (*rpc_count_stats)(struct rpc_task *, void *);
	void (*rpc_release)(void *);
};

struct rpc_task_setup {
	struct rpc_task *task;
	struct rpc_clnt *rpc_client;
	const struct rpc_message *rpc_message;
	const struct rpc_call_ops *callback_ops;
	void *callback_data;
	struct workqueue_struct *workqueue;
	unsigned short flags;
	signed char priority;
};

/*
 * RPC task flags
 */
#define RPC_TASK_ASYNC		0x0001		/* is an async task */
#define RPC_TASK_SWAPPER	0x0002		/* is swapping in/out */
#define RPC_CALL_MAJORSEEN	0x0020		/* major timeout seen */
#define RPC_TASK_ROOTCREDS	0x0040		/* force root creds */
#define RPC_TASK_DYNAMIC	0x0080		/* task was kmalloc'ed */
#define RPC_TASK_KILLED		0x0100		/* task was killed */
#define RPC_TASK_SOFT		0x0200		/* Use soft timeouts */
#define RPC_TASK_SOFTCONN	0x0400		/* Fail if can't connect */
#define RPC_TASK_SENT		0x0800		/* message was sent */
#define RPC_TASK_TIMEOUT	0x1000		/* fail with ETIMEDOUT on timeout */
#define RPC_TASK_NOCONNECT	0x2000		/* return ENOTCONN if not connected */

#define RPC_IS_ASYNC(t)		((t)->tk_flags & RPC_TASK_ASYNC)
#define RPC_IS_SWAPPER(t)	((t)->tk_flags & RPC_TASK_SWAPPER)
#define RPC_DO_ROOTOVERRIDE(t)	((t)->tk_flags & RPC_TASK_ROOTCREDS)
#define RPC_ASSASSINATED(t)	((t)->tk_flags & RPC_TASK_KILLED)
#define RPC_IS_SOFT(t)		((t)->tk_flags & (RPC_TASK_SOFT|RPC_TASK_TIMEOUT))
#define RPC_IS_SOFTCONN(t)	((t)->tk_flags & RPC_TASK_SOFTCONN)
#define RPC_WAS_SENT(t)		((t)->tk_flags & RPC_TASK_SENT)

#define RPC_TASK_RUNNING	0
#define RPC_TASK_QUEUED		1
#define RPC_TASK_ACTIVE		2

#define RPC_IS_RUNNING(t)	test_bit(RPC_TASK_RUNNING, &(t)->tk_runstate)
#define rpc_set_running(t)	set_bit(RPC_TASK_RUNNING, &(t)->tk_runstate)
#define rpc_test_and_set_running(t) \
				test_and_set_bit(RPC_TASK_RUNNING, &(t)->tk_runstate)
#define rpc_clear_running(t)	\
	do { \
		smp_mb__before_clear_bit(); \
		clear_bit(RPC_TASK_RUNNING, &(t)->tk_runstate); \
		smp_mb__after_clear_bit(); \
	} while (0)

#define RPC_IS_QUEUED(t)	test_bit(RPC_TASK_QUEUED, &(t)->tk_runstate)
#define rpc_set_queued(t)	set_bit(RPC_TASK_QUEUED, &(t)->tk_runstate)
#define rpc_clear_queued(t)	\
	do { \
		smp_mb__before_clear_bit(); \
		clear_bit(RPC_TASK_QUEUED, &(t)->tk_runstate); \
		smp_mb__after_clear_bit(); \
	} while (0)

#define RPC_IS_ACTIVATED(t)	test_bit(RPC_TASK_ACTIVE, &(t)->tk_runstate)

/*
 * Task priorities.
 * Note: if you change these, you must also change
 * the task initialization definitions below.
 */
#define RPC_PRIORITY_LOW	(-1)
#define RPC_PRIORITY_NORMAL	(0)
#define RPC_PRIORITY_HIGH	(1)
#define RPC_PRIORITY_PRIVILEGED	(2)
#define RPC_NR_PRIORITY		(1 + RPC_PRIORITY_PRIVILEGED - RPC_PRIORITY_LOW)

struct rpc_timer {
	struct timer_list timer;
	struct list_head list;
	unsigned long expires;
};

/*
 * RPC synchronization objects
 */
struct rpc_wait_queue {
	spinlock_t		lock;
	struct list_head	tasks[RPC_NR_PRIORITY];	/* task queue for each priority level */
	pid_t			owner;			/* process id of last task serviced */
	unsigned char		maxpriority;		/* maximum priority (0 if queue is not a priority queue) */
	unsigned char		priority;		/* current priority */
	unsigned char		nr;			/* # tasks remaining for cookie */
	unsigned short		qlen;			/* total # tasks waiting in queue */
	struct rpc_timer	timer_list;
#if defined(RPC_DEBUG) || defined(RPC_TRACEPOINTS)
	const char *		name;
#endif
};

/*
 * This is the # requests to send consecutively
 * from a single cookie.  The aim is to improve
 * performance of NFS operations such as read/write.
 */
#define RPC_BATCH_COUNT			16
#define RPC_IS_PRIORITY(q)		((q)->maxpriority > 0)

/*
 * Function prototypes
 */
struct rpc_task *rpc_new_task(const struct rpc_task_setup *);
struct rpc_task *rpc_run_task(const struct rpc_task_setup *);
struct rpc_task *rpc_run_bc_task(struct rpc_rqst *req,
				const struct rpc_call_ops *ops);
void		rpc_put_task(struct rpc_task *);
void		rpc_put_task_async(struct rpc_task *);
void		rpc_exit_task(struct rpc_task *);
void		rpc_exit(struct rpc_task *, int);
void		rpc_release_calldata(const struct rpc_call_ops *, void *);
void		rpc_killall_tasks(struct rpc_clnt *);
void		rpc_execute(struct rpc_task *);
void		rpc_init_priority_wait_queue(struct rpc_wait_queue *, const char *);
void		rpc_init_wait_queue(struct rpc_wait_queue *, const char *);
void		rpc_destroy_wait_queue(struct rpc_wait_queue *);
void		rpc_sleep_on(struct rpc_wait_queue *, struct rpc_task *,
					rpc_action action);
void		rpc_sleep_on_priority(struct rpc_wait_queue *,
					struct rpc_task *,
					rpc_action action,
					int priority);
void		rpc_wake_up_queued_task(struct rpc_wait_queue *,
					struct rpc_task *);
void		rpc_wake_up(struct rpc_wait_queue *);
struct rpc_task *rpc_wake_up_next(struct rpc_wait_queue *);
struct rpc_task *rpc_wake_up_first(struct rpc_wait_queue *,
					bool (*)(struct rpc_task *, void *),
					void *);
void		rpc_wake_up_status(struct rpc_wait_queue *, int);
void		rpc_delay(struct rpc_task *, unsigned long);
void *		rpc_malloc(struct rpc_task *, size_t);
void		rpc_free(void *);
int		rpciod_up(void);
void		rpciod_down(void);
int		__rpc_wait_for_completion_task(struct rpc_task *task, int (*)(void *));
#ifdef RPC_DEBUG
struct net;
void		rpc_show_tasks(struct net *);
#endif
int		rpc_init_mempool(void);
void		rpc_destroy_mempool(void);
extern struct workqueue_struct *rpciod_workqueue;
void		rpc_prepare_task(struct rpc_task *task);

static inline int rpc_wait_for_completion_task(struct rpc_task *task)
{
	return __rpc_wait_for_completion_task(task, NULL);
}

#if defined(RPC_DEBUG) || defined (RPC_TRACEPOINTS)
static inline const char * rpc_qname(const struct rpc_wait_queue *q)
{
	return ((q && q->name) ? q->name : "unknown");
}

static inline void rpc_assign_waitqueue_name(struct rpc_wait_queue *q,
		const char *name)
{
	q->name = name;
}
#else
static inline void rpc_assign_waitqueue_name(struct rpc_wait_queue *q,
		const char *name)
{
}
#endif

#endif /* _LINUX_SUNRPC_SCHED_H_ */
