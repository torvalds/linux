// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Oracle Corporation
 */
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/sched/task.h>
#include <linux/sched/vhost_task.h>
#include <linux/sched/signal.h>

enum vhost_task_flags {
	VHOST_TASK_FLAGS_STOP,
};

struct vhost_task {
	bool (*fn)(void *data);
	void *data;
	struct completion exited;
	unsigned long flags;
	struct task_struct *task;
};

static int vhost_task_fn(void *data)
{
	struct vhost_task *vtsk = data;
	bool dead = false;

	for (;;) {
		bool did_work;

		if (!dead && signal_pending(current)) {
			struct ksignal ksig;
			/*
			 * Calling get_signal will block in SIGSTOP,
			 * or clear fatal_signal_pending, but remember
			 * what was set.
			 *
			 * This thread won't actually exit until all
			 * of the file descriptors are closed, and
			 * the release function is called.
			 */
			dead = get_signal(&ksig);
			if (dead)
				clear_thread_flag(TIF_SIGPENDING);
		}

		/* mb paired w/ vhost_task_stop */
		set_current_state(TASK_INTERRUPTIBLE);

		if (test_bit(VHOST_TASK_FLAGS_STOP, &vtsk->flags)) {
			__set_current_state(TASK_RUNNING);
			break;
		}

		did_work = vtsk->fn(vtsk->data);
		if (!did_work)
			schedule();
	}

	complete(&vtsk->exited);
	do_exit(0);
}

/**
 * vhost_task_wake - wakeup the vhost_task
 * @vtsk: vhost_task to wake
 *
 * wake up the vhost_task worker thread
 */
void vhost_task_wake(struct vhost_task *vtsk)
{
	wake_up_process(vtsk->task);
}
EXPORT_SYMBOL_GPL(vhost_task_wake);

/**
 * vhost_task_stop - stop a vhost_task
 * @vtsk: vhost_task to stop
 *
 * vhost_task_fn ensures the worker thread exits after
 * VHOST_TASK_FLAGS_SOP becomes true.
 */
void vhost_task_stop(struct vhost_task *vtsk)
{
	set_bit(VHOST_TASK_FLAGS_STOP, &vtsk->flags);
	vhost_task_wake(vtsk);
	/*
	 * Make sure vhost_task_fn is no longer accessing the vhost_task before
	 * freeing it below.
	 */
	wait_for_completion(&vtsk->exited);
	kfree(vtsk);
}
EXPORT_SYMBOL_GPL(vhost_task_stop);

/**
 * vhost_task_create - create a copy of a task to be used by the kernel
 * @fn: vhost worker function
 * @arg: data to be passed to fn
 * @name: the thread's name
 *
 * This returns a specialized task for use by the vhost layer or NULL on
 * failure. The returned task is inactive, and the caller must fire it up
 * through vhost_task_start().
 */
struct vhost_task *vhost_task_create(bool (*fn)(void *), void *arg,
				     const char *name)
{
	struct kernel_clone_args args = {
		.flags		= CLONE_FS | CLONE_UNTRACED | CLONE_VM |
				  CLONE_THREAD | CLONE_SIGHAND,
		.exit_signal	= 0,
		.fn		= vhost_task_fn,
		.name		= name,
		.user_worker	= 1,
		.no_files	= 1,
	};
	struct vhost_task *vtsk;
	struct task_struct *tsk;

	vtsk = kzalloc(sizeof(*vtsk), GFP_KERNEL);
	if (!vtsk)
		return NULL;
	init_completion(&vtsk->exited);
	vtsk->data = arg;
	vtsk->fn = fn;

	args.fn_arg = vtsk;

	tsk = copy_process(NULL, 0, NUMA_NO_NODE, &args);
	if (IS_ERR(tsk)) {
		kfree(vtsk);
		return NULL;
	}

	vtsk->task = tsk;
	return vtsk;
}
EXPORT_SYMBOL_GPL(vhost_task_create);

/**
 * vhost_task_start - start a vhost_task created with vhost_task_create
 * @vtsk: vhost_task to wake up
 */
void vhost_task_start(struct vhost_task *vtsk)
{
	wake_up_new_task(vtsk->task);
}
EXPORT_SYMBOL_GPL(vhost_task_start);
