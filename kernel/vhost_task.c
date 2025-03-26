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
	VHOST_TASK_FLAGS_KILLED,
};

struct vhost_task {
	bool (*fn)(void *data);
	void (*handle_sigkill)(void *data);
	void *data;
	struct completion exited;
	unsigned long flags;
	struct task_struct *task;
	/* serialize SIGKILL and vhost_task_stop calls */
	struct mutex exit_mutex;
};

static int vhost_task_fn(void *data)
{
	struct vhost_task *vtsk = data;

	for (;;) {
		bool did_work;

		if (signal_pending(current)) {
			struct ksignal ksig;

			if (get_signal(&ksig))
				break;
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

	mutex_lock(&vtsk->exit_mutex);
	/*
	 * If a vhost_task_stop and SIGKILL race, we can ignore the SIGKILL.
	 * When the vhost layer has called vhost_task_stop it's already stopped
	 * new work and flushed.
	 */
	if (!test_bit(VHOST_TASK_FLAGS_STOP, &vtsk->flags)) {
		set_bit(VHOST_TASK_FLAGS_KILLED, &vtsk->flags);
		vtsk->handle_sigkill(vtsk->data);
	}
	mutex_unlock(&vtsk->exit_mutex);
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
 * VHOST_TASK_FLAGS_STOP becomes true.
 */
void vhost_task_stop(struct vhost_task *vtsk)
{
	mutex_lock(&vtsk->exit_mutex);
	if (!test_bit(VHOST_TASK_FLAGS_KILLED, &vtsk->flags)) {
		set_bit(VHOST_TASK_FLAGS_STOP, &vtsk->flags);
		vhost_task_wake(vtsk);
	}
	mutex_unlock(&vtsk->exit_mutex);

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
 * @handle_sigkill: vhost function to handle when we are killed
 * @arg: data to be passed to fn and handled_kill
 * @name: the thread's name
 *
 * This returns a specialized task for use by the vhost layer or NULL on
 * failure. The returned task is inactive, and the caller must fire it up
 * through vhost_task_start().
 */
struct vhost_task *vhost_task_create(bool (*fn)(void *),
				     void (*handle_sigkill)(void *), void *arg,
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
		return ERR_PTR(-ENOMEM);
	init_completion(&vtsk->exited);
	mutex_init(&vtsk->exit_mutex);
	vtsk->data = arg;
	vtsk->fn = fn;
	vtsk->handle_sigkill = handle_sigkill;

	args.fn_arg = vtsk;

	tsk = copy_process(NULL, 0, NUMA_NO_NODE, &args);
	if (IS_ERR(tsk)) {
		kfree(vtsk);
		return ERR_PTR(PTR_ERR(tsk));
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
