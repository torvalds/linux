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
	struct rpc_wait_queue *	rpc_waitq;	/* RPC wait queue we're on */
};

/*
 * This is the RPC task struct
 */
struct rpc_task {
#ifdef RPC_DEBUG
	unsigned long		tk_magic;	/* 0xf00baa */
#endif
	atomic_t		tk_count;	/* Reference count */
	struct list_head	tk_task;	/* global list of tasks */
	struct rpc_clnt *	tk_client;	/* RPC client */
	struct rpc_rqst *	tk_rqstp;	/* RPC request */
	int			tk_status;	/* result of last operation */

	/*
	 * RPC call state
	 */
	struct rpc_message	tk_msg;		/* RPC call info */
	__u8			tk_garb_retry;
	__u8			tk_cred_retry;

	unsigned long		tk_cookie;	/* Cookie for batching tasks */

	/*
	 * timeout_fn   to be executed by timer bottom half
	 * callback	to be executed after waking up
	 * action	next procedure for async tasks
	 * tk_ops	caller callbacks
	 */
	void			(*tk_timeout_fn)(struct rpc_task *);
	void			(*tk_callback)(struct rpc_task *);
	void			(*tk_action)(struct rpc_task *);
	const struct rpc_call_ops *tk_ops;
	void *			tk_calldata;

	/*
	 * tk_timer is used for async processing by the RPC scheduling
	 * primitives. You should not access this directly unless
	 * you have a pathological interest in kernel oopses.
	 */
	struct timer_list	tk_timer;	/* kernel timer */
	unsigned long		tk_timeout;	/* timeout for rpc_sleep() */
	unsigned short		tk_flags;	/* misc flags */
	unsigned char		tk_priority : 2;/* Task priority */
	unsigned long		tk_runstate;	/* Task run status */
	struct workqueue_struct	*tk_workqueue;	/* Normally rpciod, but could
						 * be any workqueue
						 */
	union {
		struct work_struct	tk_work;	/* Async task work queue */
		struct rpc_wait		tk_wait;	/* RPC wait */
	} u;

	unsigned short		tk_timeouts;	/* maj timeouts */
	size_t			tk_bytes_sent;	/* total bytes sent */
	unsigned long		tk_start;	/* RPC task init timestamp */
	long			tk_rtt;		/* round-trip time (jiffies) */

#ifdef RPC_DEBUG
	unsigned short		tk_pid;		/* debugging aid */
#endif
};
#define tk_auth			tk_client->cl_auth
#define tk_xprt			tk_client->cl_xprt

/* support walking a list of tasks on a wait queue */
#define	task_for_each(task, pos, head) \
	list_for_each(pos, head) \
		if ((task=list_entry(pos, struct rpc_task, u.tk_wait.list)),1)

#define	task_for_first(task, head) \
	if (!list_empty(head) &&  \
	    ((task=list_entry((head)->next, struct rpc_task, u.tk_wait.list)),1))

/* .. and walking list of all tasks */
#define	alltask_for_each(task, pos, head) \
	list_for_each(pos, head) \
		if ((task=list_entry(pos, struct rpc_task, tk_task)),1)

typedef void			(*rpc_action)(struct rpc_task *);

struct rpc_call_ops {
	void (*rpc_call_prepare)(struct rpc_task *, void *);
	void (*rpc_call_done)(struct rpc_task *, void *);
	void (*rpc_release)(void *);
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
#define RPC_TASK_NOINTR		0x0400		/* uninterruptible task */

#define RPC_IS_ASYNC(t)		((t)->tk_flags & RPC_TASK_ASYNC)
#define RPC_IS_SWAPPER(t)	((t)->tk_flags & RPC_TASK_SWAPPER)
#define RPC_DO_ROOTOVERRIDE(t)	((t)->tk_flags & RPC_TASK_ROOTCREDS)
#define RPC_ASSASSINATED(t)	((t)->tk_flags & RPC_TASK_KILLED)
#define RPC_DO_CALLBACK(t)	((t)->tk_callback != NULL)
#define RPC_IS_SOFT(t)		((t)->tk_flags & RPC_TASK_SOFT)
#define RPC_TASK_UNINTERRUPTIBLE(t) ((t)->tk_flags & RPC_TASK_NOINTR)

#define RPC_TASK_RUNNING	0
#define RPC_TASK_QUEUED		1
#define RPC_TASK_WAKEUP		2
#define RPC_TASK_HAS_TIMER	3
#define RPC_TASK_ACTIVE		4

#define RPC_IS_RUNNING(t)	(test_bit(RPC_TASK_RUNNING, &(t)->tk_runstate))
#define rpc_set_running(t)	(set_bit(RPC_TASK_RUNNING, &(t)->tk_runstate))
#define rpc_test_and_set_running(t) \
				(test_and_set_bit(RPC_TASK_RUNNING, &(t)->tk_runstate))
#define rpc_clear_running(t)	\
	do { \
		smp_mb__before_clear_bit(); \
		clear_bit(RPC_TASK_RUNNING, &(t)->tk_runstate); \
		smp_mb__after_clear_bit(); \
	} while (0)

#define RPC_IS_QUEUED(t)	(test_bit(RPC_TASK_QUEUED, &(t)->tk_runstate))
#define rpc_set_queued(t)	(set_bit(RPC_TASK_QUEUED, &(t)->tk_runstate))
#define rpc_clear_queued(t)	\
	do { \
		smp_mb__before_clear_bit(); \
		clear_bit(RPC_TASK_QUEUED, &(t)->tk_runstate); \
		smp_mb__after_clear_bit(); \
	} while (0)

#define rpc_start_wakeup(t) \
	(test_and_set_bit(RPC_TASK_WAKEUP, &(t)->tk_runstate) == 0)
#define rpc_finish_wakeup(t) \
	do { \
		smp_mb__before_clear_bit(); \
		clear_bit(RPC_TASK_WAKEUP, &(t)->tk_runstate); \
		smp_mb__after_clear_bit(); \
	} while (0)

#define RPC_IS_ACTIVATED(t)	(test_bit(RPC_TASK_ACTIVE, &(t)->tk_runstate))

/*
 * Task priorities.
 * Note: if you change these, you must also change
 * the task initialization definitions below.
 */
#define RPC_PRIORITY_LOW	0
#define RPC_PRIORITY_NORMAL	1
#define RPC_PRIORITY_HIGH	2
#define RPC_NR_PRIORITY		(RPC_PRIORITY_HIGH+1)

/*
 * RPC synchronization objects
 */
struct rpc_wait_queue {
	spinlock_t		lock;
	struct list_head	tasks[RPC_NR_PRIORITY];	/* task queue for each priority level */
	unsigned long		cookie;			/* cookie of last task serviced */
	unsigned char		maxpriority;		/* maximum priority (0 if queue is not a priority queue) */
	unsigned char		priority;		/* current priority */
	unsigned char		count;			/* # task groups remaining serviced so far */
	unsigned char		nr;			/* # tasks remaining for cookie */
	unsigned short		qlen;			/* total # tasks waiting in queue */
#ifdef RPC_DEBUG
	const char *		name;
#endif
};

/*
 * This is the # requests to send consecutively
 * from a single cookie.  The aim is to improve
 * performance of NFS operations such as read/write.
 */
#define RPC_BATCH_COUNT			16

#ifndef RPC_DEBUG
# define RPC_WAITQ_INIT(var,qname) { \
		.lock = SPIN_LOCK_UNLOCKED, \
		.tasks = { \
			[0] = LIST_HEAD_INIT(var.tasks[0]), \
			[1] = LIST_HEAD_INIT(var.tasks[1]), \
			[2] = LIST_HEAD_INIT(var.tasks[2]), \
		}, \
	}
#else
# define RPC_WAITQ_INIT(var,qname) { \
		.lock = SPIN_LOCK_UNLOCKED, \
		.tasks = { \
			[0] = LIST_HEAD_INIT(var.tasks[0]), \
			[1] = LIST_HEAD_INIT(var.tasks[1]), \
			[2] = LIST_HEAD_INIT(var.tasks[2]), \
		}, \
		.name = qname, \
	}
#endif
# define RPC_WAITQ(var,qname)      struct rpc_wait_queue var = RPC_WAITQ_INIT(var,qname)

#define RPC_IS_PRIORITY(q)		((q)->maxpriority > 0)

/*
 * Function prototypes
 */
struct rpc_task *rpc_new_task(struct rpc_clnt *, int flags,
				const struct rpc_call_ops *ops, void *data);
struct rpc_task *rpc_run_task(struct rpc_clnt *clnt, int flags,
				const struct rpc_call_ops *ops, void *data);
void		rpc_init_task(struct rpc_task *task, struct rpc_clnt *clnt,
				int flags, const struct rpc_call_ops *ops,
				void *data);
void		rpc_put_task(struct rpc_task *);
void		rpc_release_task(struct rpc_task *);
void		rpc_exit_task(struct rpc_task *);
void		rpc_killall_tasks(struct rpc_clnt *);
int		rpc_execute(struct rpc_task *);
void		rpc_init_priority_wait_queue(struct rpc_wait_queue *, const char *);
void		rpc_init_wait_queue(struct rpc_wait_queue *, const char *);
void		rpc_sleep_on(struct rpc_wait_queue *, struct rpc_task *,
					rpc_action action, rpc_action timer);
void		rpc_wake_up_task(struct rpc_task *);
void		rpc_wake_up(struct rpc_wait_queue *);
struct rpc_task *rpc_wake_up_next(struct rpc_wait_queue *);
void		rpc_wake_up_status(struct rpc_wait_queue *, int);
void		rpc_delay(struct rpc_task *, unsigned long);
void *		rpc_malloc(struct rpc_task *, size_t);
void		rpc_free(struct rpc_task *);
int		rpciod_up(void);
void		rpciod_down(void);
int		__rpc_wait_for_completion_task(struct rpc_task *task, int (*)(void *));
#ifdef RPC_DEBUG
void		rpc_show_tasks(void);
#endif
int		rpc_init_mempool(void);
void		rpc_destroy_mempool(void);
extern struct workqueue_struct *rpciod_workqueue;

static inline void rpc_exit(struct rpc_task *task, int status)
{
	task->tk_status = status;
	task->tk_action = rpc_exit_task;
}

static inline int rpc_wait_for_completion_task(struct rpc_task *task)
{
	return __rpc_wait_for_completion_task(task, NULL);
}

#ifdef RPC_DEBUG
static inline const char * rpc_qname(struct rpc_wait_queue *q)
{
	return ((q && q->name) ? q->name : "unknown");
}
#endif

#endif /* _LINUX_SUNRPC_SCHED_H_ */
