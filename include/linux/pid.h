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
 * resources, and since a new struct pid is allocated when the numeric pid
 * value is reused (when pids wrap around) we don't mistakenly refer to new
 * processes.
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

extern struct pid *get_task_pid(struct task_struct *task, enum pid_type type);

/*
 * attach_pid() and detach_pid() must be called with the tasklist_lock
 * write-held.
 */
extern int FASTCALL(attach_pid(struct task_struct *task,
				enum pid_type type, struct pid *pid));
extern void FASTCALL(detach_pid(struct task_struct *task, enum pid_type));
extern void FASTCALL(transfer_pid(struct task_struct *old,
				  struct task_struct *new, enum pid_type));

/*
 * look up a PID in the hash table. Must be called with the tasklist_lock
 * or rcu_read_lock() held.
 */
extern struct pid *FASTCALL(find_pid(int nr));

/*
 * Lookup a PID in the hash table, and return with it's count elevated.
 */
extern struct pid *find_get_pid(int nr);
extern struct pid *find_ge_pid(int nr);

extern struct pid *alloc_pid(void);
extern void FASTCALL(free_pid(struct pid *pid));

static inline pid_t pid_nr(struct pid *pid)
{
	pid_t nr = 0;
	if (pid)
		nr = pid->nr;
	return nr;
}

#define do_each_pid_task(pid, type, task)				\
	do {								\
		struct hlist_node *pos___;				\
		if (pid != NULL)					\
			hlist_for_each_entry_rcu((task), pos___,	\
				&pid->tasks[type], pids[type].node) {

#define while_each_pid_task(pid, type, task)				\
			}						\
	} while (0)

#endif /* _LINUX_PID_H */
