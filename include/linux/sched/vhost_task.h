/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VHOST_TASK_H
#define _LINUX_VHOST_TASK_H

#include <linux/completion.h>

struct task_struct;

struct vhost_task {
	int (*fn)(void *data);
	void *data;
	struct completion exited;
	unsigned long flags;
	struct task_struct *task;
};

struct vhost_task *vhost_task_create(int (*fn)(void *), void *arg,
				     const char *name);
void vhost_task_start(struct vhost_task *vtsk);
void vhost_task_stop(struct vhost_task *vtsk);
bool vhost_task_should_stop(struct vhost_task *vtsk);

#endif
