/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_VHOST_TASK_H
#define _LINUX_SCHED_VHOST_TASK_H

struct vhost_task;

struct vhost_task *vhost_task_create(bool (*fn)(void *), void *arg,
				     const char *name);
void vhost_task_start(struct vhost_task *vtsk);
void vhost_task_stop(struct vhost_task *vtsk);
void vhost_task_wake(struct vhost_task *vtsk);

#endif /* _LINUX_SCHED_VHOST_TASK_H */
