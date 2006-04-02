#ifndef _LINUX_PID_H
#define _LINUX_PID_H

#include <linux/rcupdate.h>

enum pid_type
{
	PIDTYPE_PID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX
};

/*
 * What is struct pid?
 *
 * A struct pid is the kernel's internal notion of a process identifier.
 * It refers to individual tasks, process groups, and sessions.  While
 * there are processes attached to it the struct pid lives in a hash
 * table, so it and then the processes that it refers to can be found
 * quickly from the numeric pid value.  The attached processes may be
 * quickly accessed by following pointers from struct pid.
 *
 * Storing pid_t values in the kernel and refering to them later has a
 * problem.  The process originally with that pid may have exited and the
 * pid allocator wrapped, and another process could have come along
 * and been assigned that pid.
 *
 * Referring to user space processes by holding a reference to struct
 * task_struct has a problem.  When the user space process exits
 * the now useless task_struct is still kept.  A task_struct plus a
 * stack consumes around 10K of low kernel memory.  More precisely
 * this is THREAD_SIZE + sizeof(struct task_struct).  By comparison
 * a struct pid is about 64 bytes.
 *
 * Holding a reference to struct pid solves both of these problems.
 * It is small so holding a reference does not consume a lot of
 * resources, and since a new struct pid is allocated when the numeric
 * pid value is reused we don't mistakenly refer to new processes.
 */

struct pid
{
	atomic_t count;
	/* Try to keep pid_chain in the same cacheline as nr for find_pid */
	int nr;
	struct hlist_node pid_chain;
	/* lists of tasks that use this pid */
	struct hlist_head tasks[PIDTYPE_MAX];
	struct rcu_head rcu;
};

struct pid_link
{
	struct hlist_node node;
	struct pid *pid;
};

static inline struct pid *get_pid(struct pid *pid)
{
	if (pid)
		atomic_inc(&pid->count);
	return pid;
}

extern void FASTCALL(put_pid(struct pid *pid));
extern struct task_struct *FASTCALL(pid_task(struct pid *pid, enum pid_type));
extern struct task_struct *FASTCALL(get_pid_task(struct pid *pid,
						enum pid_type));

/*
 * attach_pid() and detach_pid() must be called with the tasklist_lock
 * write-held.
 */
extern int FASTCALL(attach_pid(struct task_struct *task,
				enum pid_type type, int nr));

extern void FASTCALL(detach_pid(struct task_struct *task, enum pid_type));

/*
 * look up a PID in the hash table. Must be called with the tasklist_lock
 * or rcu_read_lock() held.
 */
extern struct pid *FASTCALL(find_pid(int nr));

/*
 * Lookup a PID in the hash table, and return with it's count elevated.
 */
extern struct pid *find_get_pid(int nr);

extern struct pid *alloc_pid(void);
extern void FASTCALL(free_pid(struct pid *pid));

#define pid_next(task, type)					\
	((task)->pids[(type)].node.next)

#define pid_next_task(task, type) 				\
	hlist_entry(pid_next(task, type), struct task_struct,	\
			pids[(type)].node)


/* We could use hlist_for_each_entry_rcu here but it takes more arguments
 * than the do_each_task_pid/while_each_task_pid.  So we roll our own
 * to preserve the existing interface.
 */
#define do_each_task_pid(who, type, task)				\
	if ((task = find_task_by_pid_type(type, who))) {		\
		prefetch(pid_next(task, type));				\
		do {

#define while_each_task_pid(who, type, task)				\
		} while (pid_next(task, type) &&  ({			\
				task = pid_next_task(task, type);	\
				rcu_dereference(task);			\
				prefetch(pid_next(task, type));		\
				1; }) );				\
	}

#endif /* _LINUX_PID_H */
