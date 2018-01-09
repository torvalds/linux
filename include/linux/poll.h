/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_POLL_H
#define _LINUX_POLL_H


#include <linux/compiler.h>
#include <linux/ktime.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include <uapi/linux/poll.h>
#include <uapi/linux/eventpoll.h>

extern struct ctl_table epoll_table[]; /* for sysctl */
/* ~832 bytes of stack space used max in sys_select/sys_poll before allocating
   additional memory. */
#define MAX_STACK_ALLOC 832
#define FRONTEND_STACK_ALLOC	256
#define SELECT_STACK_ALLOC	FRONTEND_STACK_ALLOC
#define POLL_STACK_ALLOC	FRONTEND_STACK_ALLOC
#define WQUEUES_STACK_ALLOC	(MAX_STACK_ALLOC - FRONTEND_STACK_ALLOC)
#define N_INLINE_POLL_ENTRIES	(WQUEUES_STACK_ALLOC / sizeof(struct poll_table_entry))

#define DEFAULT_POLLMASK (EPOLLIN | EPOLLOUT | EPOLLRDNORM | EPOLLWRNORM)

struct poll_table_struct;

/* 
 * structures and helpers for f_op->poll implementations
 */
typedef void (*poll_queue_proc)(struct file *, wait_queue_head_t *, struct poll_table_struct *);

/*
 * Do not touch the structure directly, use the access functions
 * poll_does_not_wait() and poll_requested_events() instead.
 */
typedef struct poll_table_struct {
	poll_queue_proc _qproc;
	__poll_t _key;
} poll_table;

static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && p->_qproc && wait_address)
		p->_qproc(filp, wait_address, p);
}

/*
 * Return true if it is guaranteed that poll will not wait. This is the case
 * if the poll() of another file descriptor in the set got an event, so there
 * is no need for waiting.
 */
static inline bool poll_does_not_wait(const poll_table *p)
{
	return p == NULL || p->_qproc == NULL;
}

/*
 * Return the set of events that the application wants to poll for.
 * This is useful for drivers that need to know whether a DMA transfer has
 * to be started implicitly on poll(). You typically only want to do that
 * if the application is actually polling for POLLIN and/or POLLOUT.
 */
static inline __poll_t poll_requested_events(const poll_table *p)
{
	return p ? p->_key : ~(__poll_t)0;
}

static inline void init_poll_funcptr(poll_table *pt, poll_queue_proc qproc)
{
	pt->_qproc = qproc;
	pt->_key   = ~(__poll_t)0; /* all events enabled */
}

static inline bool file_has_poll_mask(struct file *file)
{
	return file->f_op->get_poll_head && file->f_op->poll_mask;
}

static inline bool file_can_poll(struct file *file)
{
	return file->f_op->poll || file_has_poll_mask(file);
}

__poll_t vfs_poll(struct file *file, struct poll_table_struct *pt);

struct poll_table_entry {
	struct file *filp;
	__poll_t key;
	wait_queue_entry_t wait;
	wait_queue_head_t *wait_address;
};

/*
 * Structures and helpers for select/poll syscall
 */
struct poll_wqueues {
	poll_table pt;
	struct poll_table_page *table;
	struct task_struct *polling_task;
	int triggered;
	int error;
	int inline_index;
	struct poll_table_entry inline_entries[N_INLINE_POLL_ENTRIES];
};

extern void poll_initwait(struct poll_wqueues *pwq);
extern void poll_freewait(struct poll_wqueues *pwq);
extern u64 select_estimate_accuracy(struct timespec64 *tv);

#define MAX_INT64_SECONDS (((s64)(~((u64)0)>>1)/HZ)-1)

extern int core_sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			   fd_set __user *exp, struct timespec64 *end_time);

extern int poll_select_set_timeout(struct timespec64 *to, time64_t sec,
				   long nsec);

#define __MAP(v, from, to) \
	(from < to ? (v & from) * (to/from) : (v & from) / (from/to))

static inline __u16 mangle_poll(__poll_t val)
{
	__u16 v = (__force __u16)val;
#define M(X) __MAP(v, (__force __u16)EPOLL##X, POLL##X)
	return M(IN) | M(OUT) | M(PRI) | M(ERR) | M(NVAL) |
		M(RDNORM) | M(RDBAND) | M(WRNORM) | M(WRBAND) |
		M(HUP) | M(RDHUP) | M(MSG);
#undef M
}

static inline __poll_t demangle_poll(u16 val)
{
#define M(X) (__force __poll_t)__MAP(val, POLL##X, (__force __u16)EPOLL##X)
	return M(IN) | M(OUT) | M(PRI) | M(ERR) | M(NVAL) |
		M(RDNORM) | M(RDBAND) | M(WRNORM) | M(WRBAND) |
		M(HUP) | M(RDHUP) | M(MSG);
#undef M
}
#undef __MAP


#endif /* _LINUX_POLL_H */
