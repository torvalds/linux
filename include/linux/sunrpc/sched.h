/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/wait_bit.h>
#include <linux/workqueue.h>
#include <linux/sunrpc/xdr.h>

/*
 * This is the actual RPC procedure call info.
 */
struct rpc_procinfo;
struct rpc_message {
	const struct rpc_procinfo *rpc_proc;	/* Procedure information */
	void *			rpc_argp;	/* Arguments */
	void *			rpc_resp;	/* Result */
	const struct cred *	rpc_cred;	/* Credentials */
};

struct rpc_call_ops;
struct rpc_wait_queue;
struct rpc_wait {
	struct list_head	list;		/* wait queue links */
	struct list_head	links;		/* Links to related tasks */
	struct list_head	timer_list;	/* Timer list */
};

/*
 * This describes a timeout strategy
 */
struct rpc_timeout {
	unsigned long		to_initval,		/* initial timeout */
				to_maxval,		/* max timeout */
				to_increment;		/* if !exponential */
	unsigned int		to_retries;		/* max # of retries */
	unsigned char		to_exponential;
};

/*
 * This is the RPC task struct
 */
struct rpc_task {
	atomic_t		tk_count;	/* Reference count */
	int			tk_status;	/* result of last operation */
	struct list_head	tk_task;	/* global list of tasks */

	/*
	 * callback	to be executed after waking up
	 * action	next procedure for async tasks
	 */
	void			(*tk_callback)(struct rpc_task *);
	void			(*tk_action)(struct rpc_task *);

	unsigned long		tk_timeout;	/* timeout for rpc_sleep() */
	unsigned long		tk_runstate;	/* Task run status */

	struct rpc_wait_queue 	*tk_waitqueue;	/* RPC wait queue we're on */
	union {
		struct work_struct	tk_work;	/* Async task work queue */
		struct rpc_wait		tk_wait;	/* RPC wait */
	} u;

	/*
	 * RPC call state
	 */
	struct rpc_message	tk_msg;		/* RPC call info */
	void *			tk_calldata;	/* Caller private data */
	const struct rpc_call_ops *tk_ops;	/* Caller callbacks */

	struct rpc_clnt *	tk_client;	/* RPC client */
	struct rpc_xprt *	tk_xprt;	/* Transport */
	struct rpc_cred *	tk_op_cred;	/* cred being operated on */

	struct rpc_rqst *	tk_rqstp;	/* RPC request */

	struct workqueue_struct	*tk_workqueue;	/* Normally rpciod, but could
						 * be any workqueue
						 */
	ktime_t			tk_start;	/* RPC task init timestamp */

	pid_t			tk_owner;	/* Process id for batching tasks */

	int			tk_rpc_status;	/* Result of last RPC operation */
	unsigned short		tk_flags;	/* misc flags */
	unsigned short		tk_timeouts;	/* maj timeouts */

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG) || IS_ENABLED(CONFIG_TRACEPOINTS)
	unsigned short		tk_pid;		/* debugging aid */
#endif
	unsigned char		tk_priority : 2,/* Task priority */
				tk_garb_retry : 2,
				tk_cred_retry : 2;
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
	struct rpc_xprt *rpc_xprt;
	struct rpc_cred *rpc_op_cred;	/* credential being operated on */
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
#define RPC_TASK_MOVEABLE	0x0004		/* nfs4.1+ rpc tasks */
#define RPC_TASK_NULLCREDS	0x0010		/* Use AUTH_NULL credential */
#define RPC_CALL_MAJORSEEN	0x0020		/* major timeout seen */
#define RPC_TASK_DYNAMIC	0x0080		/* task was kmalloc'ed */
#define	RPC_TASK_NO_ROUND_ROBIN	0x0100		/* send requests on "main" xprt */
#define RPC_TASK_SOFT		0x0200		/* Use soft timeouts */
#define RPC_TASK_SOFTCONN	0x0400		/* Fail if can't connect */
#define RPC_TASK_SENT		0x0800		/* message was sent */
#define RPC_TASK_TIMEOUT	0x1000		/* fail with ETIMEDOUT on timeout */
#define RPC_TASK_NOCONNECT	0x2000		/* return ENOTCONN if not connected */
#define RPC_TASK_NO_RETRANS_TIMEOUT	0x4000		/* wait forever for a reply */
#define RPC_TASK_CRED_NOREF	0x8000		/* No refcount on the credential */

#define RPC_IS_ASYNC(t)		((t)->tk_flags & RPC_TASK_ASYNC)
#define RPC_IS_SWAPPER(t)	((t)->tk_flags & RPC_TASK_SWAPPER)
#define RPC_IS_SOFT(t)		((t)->tk_flags & (RPC_TASK_SOFT|RPC_TASK_TIMEOUT))
#define RPC_IS_SOFTCONN(t)	((t)->tk_flags & RPC_TASK_SOFTCONN)
#define RPC_WAS_SENT(t)		((t)->tk_flags & RPC_TASK_SENT)
#define RPC_IS_MOVEABLE(t)	((t)->tk_flags & RPC_TASK_MOVEABLE)

enum {
	RPC_TASK_RUNNING,
	RPC_TASK_QUEUED,
	RPC_TASK_ACTIVE,
	RPC_TASK_NEED_XMIT,
	RPC_TASK_NEED_RECV,
	RPC_TASK_MSG_PIN_WAIT,
};

#define rpc_test_and_set_running(t) \
				test_and_set_bit(RPC_TASK_RUNNING, &(t)->tk_runstate)
#define rpc_clear_running(t)	clear_bit(RPC_TASK_RUNNING, &(t)->tk_runstate)

#define RPC_IS_QUEUED(t)	test_bit(RPC_TASK_QUEUED, &(t)->tk_runstate)
#define rpc_set_queued(t)	set_bit(RPC_TASK_QUEUED, &(t)->tk_runstate)
#define rpc_clear_queued(t)	clear_bit(RPC_TASK_QUEUED, &(t)->tk_runstate)

#define RPC_IS_ACTIVATED(t)	test_bit(RPC_TASK_ACTIVE, &(t)->tk_runstate)

#define RPC_SIGNALLED(t)	(READ_ONCE(task->tk_rpc_status) == -ERESTARTSYS)

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
	struct list_head list;
	unsigned long expires;
	struct delayed_work dwork;
};

/*
 * RPC synchronization objects
 */
struct rpc_wait_queue {
	spinlock_t		lock;
	struct list_head	tasks[RPC_NR_PRIORITY];	/* task queue for each priority level */
	unsigned char		maxpriority;		/* maximum priority (0 if queue is not a priority queue) */
	unsigned char		priority;		/* current priority */
	unsigned char		nr;			/* # tasks remaining for cookie */
	unsigned int		qlen;			/* total # tasks waiting in queue */
	struct rpc_timer	timer_list;
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG) || IS_ENABLED(CONFIG_TRACEPOINTS)
	const char *		name;
#endif
};

/*
 * This is the # requests to send consecutively
 * from a single cookie.  The aim is to improve
 * performance of NFS operations such as read/write.
 */
#define RPC_IS_PRIORITY(q)		((q)->maxpriority > 0)

/*
 * Function prototypes
 */
struct rpc_task *rpc_new_task(const struct rpc_task_setup *);
struct rpc_task *rpc_run_task(const struct rpc_task_setup *);
struct rpc_task *rpc_run_bc_task(struct rpc_rqst *req,
		struct rpc_timeout *timeout);
void		rpc_put_task(struct rpc_task *);
void		rpc_put_task_async(struct rpc_task *);
bool		rpc_task_set_rpc_status(struct rpc_task *task, int rpc_status);
void		rpc_task_try_cancel(struct rpc_task *task, int error);
void		rpc_signal_task(struct rpc_task *);
void		rpc_exit_task(struct rpc_task *);
void		rpc_exit(struct rpc_task *, int);
void		rpc_release_calldata(const struct rpc_call_ops *, void *);
void		rpc_killall_tasks(struct rpc_clnt *);
unsigned long	rpc_cancel_tasks(struct rpc_clnt *clnt, int error,
				 bool (*fnmatch)(const struct rpc_task *,
						 const void *),
				 const void *data);
void		rpc_execute(struct rpc_task *);
void		rpc_init_priority_wait_queue(struct rpc_wait_queue *, const char *);
void		rpc_init_wait_queue(struct rpc_wait_queue *, const char *);
void		rpc_destroy_wait_queue(struct rpc_wait_queue *);
unsigned long	rpc_task_timeout(const struct rpc_task *task);
void		rpc_sleep_on_timeout(struct rpc_wait_queue *queue,
					struct rpc_task *task,
					rpc_action action,
					unsigned long timeout);
void		rpc_sleep_on(struct rpc_wait_queue *, struct rpc_task *,
					rpc_action action);
void		rpc_sleep_on_priority_timeout(struct rpc_wait_queue *queue,
					struct rpc_task *task,
					unsigned long timeout,
					int priority);
void		rpc_sleep_on_priority(struct rpc_wait_queue *,
					struct rpc_task *,
					int priority);
void		rpc_wake_up_queued_task(struct rpc_wait_queue *,
					struct rpc_task *);
void		rpc_wake_up_queued_task_set_status(struct rpc_wait_queue *,
						   struct rpc_task *,
						   int);
void		rpc_wake_up(struct rpc_wait_queue *);
struct rpc_task *rpc_wake_up_next(struct rpc_wait_queue *);
struct rpc_task *rpc_wake_up_first_on_wq(struct workqueue_struct *wq,
					struct rpc_wait_queue *,
					bool (*)(struct rpc_task *, void *),
					void *);
struct rpc_task *rpc_wake_up_first(struct rpc_wait_queue *,
					bool (*)(struct rpc_task *, void *),
					void *);
void		rpc_wake_up_status(struct rpc_wait_queue *, int);
void		rpc_delay(struct rpc_task *, unsigned long);
int		rpc_malloc(struct rpc_task *);
void		rpc_free(struct rpc_task *);
int		rpciod_up(void);
void		rpciod_down(void);
int		rpc_wait_for_completion_task(struct rpc_task *task);
#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
struct net;
void		rpc_show_tasks(struct net *);
#endif
int		rpc_init_mempool(void);
void		rpc_destroy_mempool(void);
extern struct workqueue_struct *rpciod_workqueue;
extern struct workqueue_struct *xprtiod_workqueue;
void		rpc_prepare_task(struct rpc_task *task);
gfp_t		rpc_task_gfp_mask(void);

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG) || IS_ENABLED(CONFIG_TRACEPOINTS)
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

#if IS_ENABLED(CONFIG_SUNRPC_SWAP)
int rpc_clnt_swap_activate(struct rpc_clnt *clnt);
void rpc_clnt_swap_deactivate(struct rpc_clnt *clnt);
#else
static inline int
rpc_clnt_swap_activate(struct rpc_clnt *clnt)
{
	return -EINVAL;
}

static inline void
rpc_clnt_swap_deactivate(struct rpc_clnt *clnt)
{
}
#endif /* CONFIG_SUNRPC_SWAP */

#endif /* _LINUX_SUNRPC_SCHED_H_ */
