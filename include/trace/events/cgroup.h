/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup

#if !defined(_TRACE_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CGROUP_H

#include <linux/cgroup.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(cgroup_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root),

	TP_STRUCT__entry(
		__field(	int,		root			)
		__field(	u16,		ss_mask			)
		__string(	name,		root->name		)
	),

	TP_fast_assign(
		__entry->root = root->hierarchy_id;
		__entry->ss_mask = root->subsys_mask;
		__assign_str(name, root->name);
	),

	TP_printk("root=%d ss_mask=%#x name=%s",
		  __entry->root, __entry->ss_mask, __get_str(name))
);

DEFINE_EVENT(cgroup_root, cgroup_setup_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)
);

DEFINE_EVENT(cgroup_root, cgroup_destroy_root,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)
);

DEFINE_EVENT(cgroup_root, cgroup_remount,

	TP_PROTO(struct cgroup_root *root),

	TP_ARGS(root)
);

DECLARE_EVENT_CLASS(cgroup,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path),

	TP_STRUCT__entry(
		__field(	int,		root			)
		__field(	int,		id			)
		__field(	int,		level			)
		__string(	path,		path			)
	),

	TP_fast_assign(
		__entry->root = cgrp->root->hierarchy_id;
		__entry->id = cgrp->id;
		__entry->level = cgrp->level;
		__assign_str(path, path);
	),

	TP_printk("root=%d id=%d level=%d path=%s",
		  __entry->root, __entry->id, __entry->level, __get_str(path))
);

DEFINE_EVENT(cgroup, cgroup_mkdir,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

DEFINE_EVENT(cgroup, cgroup_rmdir,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

DEFINE_EVENT(cgroup, cgroup_release,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

DEFINE_EVENT(cgroup, cgroup_rename,

	TP_PROTO(struct cgroup *cgrp, const char *path),

	TP_ARGS(cgrp, path)
);

DECLARE_EVENT_CLASS(cgroup_migrate,

	TP_PROTO(struct cgroup *dst_cgrp, const char *path,
		 struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, path, task, threadgroup),

	TP_STRUCT__entry(
		__field(	int,		dst_root		)
		__field(	int,		dst_id			)
		__field(	int,		dst_level		)
		__field(	int,		pid			)
		__string(	dst_path,	path			)
		__string(	comm,		task->comm		)
	),

	TP_fast_assign(
		__entry->dst_root = dst_cgrp->root->hierarchy_id;
		__entry->dst_id = dst_cgrp->id;
		__entry->dst_level = dst_cgrp->level;
		__assign_str(dst_path, path);
		__entry->pid = task->pid;
		__assign_str(comm, task->comm);
	),

	TP_printk("dst_root=%d dst_id=%d dst_level=%d dst_path=%s pid=%d comm=%s",
		  __entry->dst_root, __entry->dst_id, __entry->dst_level,
		  __get_str(dst_path), __entry->pid, __get_str(comm))
);

DEFINE_EVENT(cgroup_migrate, cgroup_attach_task,

	TP_PROTO(struct cgroup *dst_cgrp, const char *path,
		 struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, path, task, threadgroup)
);

DEFINE_EVENT(cgroup_migrate, cgroup_transfer_tasks,

	TP_PROTO(struct cgroup *dst_cgrp, const char *path,
		 struct task_struct *task, bool threadgroup),

	TP_ARGS(dst_cgrp, path, task, threadgroup)
);

#endif /* _TRACE_CGROUP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
