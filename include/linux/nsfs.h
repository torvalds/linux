/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Christian Brauner <brauner@kernel.org> */

#ifndef _LINUX_NSFS_H
#define _LINUX_NSFS_H

#include <linux/ns_common.h>

struct path;
struct task_struct;
struct proc_ns_operations;

int ns_get_path(struct path *path, struct task_struct *task,
		const struct proc_ns_operations *ns_ops);
typedef struct ns_common *ns_get_path_helper_t(void *);
int ns_get_path_cb(struct path *path, ns_get_path_helper_t ns_get_cb,
		   void *private_data);

bool ns_match(const struct ns_common *ns, dev_t dev, ino_t ino);

int ns_get_name(char *buf, size_t size, struct task_struct *task,
			const struct proc_ns_operations *ns_ops);
void nsfs_init(void);

#endif /* _LINUX_NSFS_H */

