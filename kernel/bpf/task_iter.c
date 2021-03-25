// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */

#include <linux/init.h>
#include <linux/namei.h>
#include <linux/pid_namespace.h>
#include <linux/fs.h>
#include <linux/fdtable.h>
#include <linux/filter.h>
#include <linux/btf_ids.h>

struct bpf_iter_seq_task_common {
	struct pid_namespace *ns;
};

struct bpf_iter_seq_task_info {
	/* The first field must be struct bpf_iter_seq_task_common.
	 * this is assumed by {init, fini}_seq_pidns() callback functions.
	 */
	struct bpf_iter_seq_task_common common;
	u32 tid;
};

static struct task_struct *task_seq_get_next(struct pid_namespace *ns,
					     u32 *tid,
					     bool skip_if_dup_files)
{
	struct task_struct *task = NULL;
	struct pid *pid;

	rcu_read_lock();
retry:
	pid = find_ge_pid(*tid, ns);
	if (pid) {
		*tid = pid_nr_ns(pid, ns);
		task = get_pid_task(pid, PIDTYPE_PID);
		if (!task) {
			++*tid;
			goto retry;
		} else if (skip_if_dup_files && !thread_group_leader(task) &&
			   task->files == task->group_leader->files) {
			put_task_struct(task);
			task = NULL;
			++*tid;
			goto retry;
		}
	}
	rcu_read_unlock();

	return task;
}

static void *task_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_task_info *info = seq->private;
	struct task_struct *task;

	task = task_seq_get_next(info->common.ns, &info->tid, false);
	if (!task)
		return NULL;

	if (*pos == 0)
		++*pos;
	return task;
}

static void *task_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_task_info *info = seq->private;
	struct task_struct *task;

	++*pos;
	++info->tid;
	put_task_struct((struct task_struct *)v);
	task = task_seq_get_next(info->common.ns, &info->tid, false);
	if (!task)
		return NULL;

	return task;
}

struct bpf_iter__task {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct task_struct *, task);
};

DEFINE_BPF_ITER_FUNC(task, struct bpf_iter_meta *meta, struct task_struct *task)

static int __task_seq_show(struct seq_file *seq, struct task_struct *task,
			   bool in_stop)
{
	struct bpf_iter_meta meta;
	struct bpf_iter__task ctx;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (!prog)
		return 0;

	meta.seq = seq;
	ctx.meta = &meta;
	ctx.task = task;
	return bpf_iter_run_prog(prog, &ctx);
}

static int task_seq_show(struct seq_file *seq, void *v)
{
	return __task_seq_show(seq, v, false);
}

static void task_seq_stop(struct seq_file *seq, void *v)
{
	if (!v)
		(void)__task_seq_show(seq, v, true);
	else
		put_task_struct((struct task_struct *)v);
}

static const struct seq_operations task_seq_ops = {
	.start	= task_seq_start,
	.next	= task_seq_next,
	.stop	= task_seq_stop,
	.show	= task_seq_show,
};

struct bpf_iter_seq_task_file_info {
	/* The first field must be struct bpf_iter_seq_task_common.
	 * this is assumed by {init, fini}_seq_pidns() callback functions.
	 */
	struct bpf_iter_seq_task_common common;
	struct task_struct *task;
	u32 tid;
	u32 fd;
};

static struct file *
task_file_seq_get_next(struct bpf_iter_seq_task_file_info *info)
{
	struct pid_namespace *ns = info->common.ns;
	u32 curr_tid = info->tid;
	struct task_struct *curr_task;
	unsigned int curr_fd = info->fd;

	/* If this function returns a non-NULL file object,
	 * it held a reference to the task/file.
	 * Otherwise, it does not hold any reference.
	 */
again:
	if (info->task) {
		curr_task = info->task;
		curr_fd = info->fd;
	} else {
                curr_task = task_seq_get_next(ns, &curr_tid, true);
                if (!curr_task) {
                        info->task = NULL;
                        info->tid = curr_tid;
                        return NULL;
                }

                /* set info->task and info->tid */
		info->task = curr_task;
		if (curr_tid == info->tid) {
			curr_fd = info->fd;
		} else {
			info->tid = curr_tid;
			curr_fd = 0;
		}
	}

	rcu_read_lock();
	for (;; curr_fd++) {
		struct file *f;
		f = task_lookup_next_fd_rcu(curr_task, &curr_fd);
		if (!f)
			break;
		if (!get_file_rcu(f))
			continue;

		/* set info->fd */
		info->fd = curr_fd;
		rcu_read_unlock();
		return f;
	}

	/* the current task is done, go to the next task */
	rcu_read_unlock();
	put_task_struct(curr_task);
	info->task = NULL;
	info->fd = 0;
	curr_tid = ++(info->tid);
	goto again;
}

static void *task_file_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_task_file_info *info = seq->private;
	struct file *file;

	info->task = NULL;
	file = task_file_seq_get_next(info);
	if (file && *pos == 0)
		++*pos;

	return file;
}

static void *task_file_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_task_file_info *info = seq->private;

	++*pos;
	++info->fd;
	fput((struct file *)v);
	return task_file_seq_get_next(info);
}

struct bpf_iter__task_file {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct task_struct *, task);
	u32 fd __aligned(8);
	__bpf_md_ptr(struct file *, file);
};

DEFINE_BPF_ITER_FUNC(task_file, struct bpf_iter_meta *meta,
		     struct task_struct *task, u32 fd,
		     struct file *file)

static int __task_file_seq_show(struct seq_file *seq, struct file *file,
				bool in_stop)
{
	struct bpf_iter_seq_task_file_info *info = seq->private;
	struct bpf_iter__task_file ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (!prog)
		return 0;

	ctx.meta = &meta;
	ctx.task = info->task;
	ctx.fd = info->fd;
	ctx.file = file;
	return bpf_iter_run_prog(prog, &ctx);
}

static int task_file_seq_show(struct seq_file *seq, void *v)
{
	return __task_file_seq_show(seq, v, false);
}

static void task_file_seq_stop(struct seq_file *seq, void *v)
{
	struct bpf_iter_seq_task_file_info *info = seq->private;

	if (!v) {
		(void)__task_file_seq_show(seq, v, true);
	} else {
		fput((struct file *)v);
		put_task_struct(info->task);
		info->task = NULL;
	}
}

static int init_seq_pidns(void *priv_data, struct bpf_iter_aux_info *aux)
{
	struct bpf_iter_seq_task_common *common = priv_data;

	common->ns = get_pid_ns(task_active_pid_ns(current));
	return 0;
}

static void fini_seq_pidns(void *priv_data)
{
	struct bpf_iter_seq_task_common *common = priv_data;

	put_pid_ns(common->ns);
}

static const struct seq_operations task_file_seq_ops = {
	.start	= task_file_seq_start,
	.next	= task_file_seq_next,
	.stop	= task_file_seq_stop,
	.show	= task_file_seq_show,
};

struct bpf_iter_seq_task_vma_info {
	/* The first field must be struct bpf_iter_seq_task_common.
	 * this is assumed by {init, fini}_seq_pidns() callback functions.
	 */
	struct bpf_iter_seq_task_common common;
	struct task_struct *task;
	struct vm_area_struct *vma;
	u32 tid;
	unsigned long prev_vm_start;
	unsigned long prev_vm_end;
};

enum bpf_task_vma_iter_find_op {
	task_vma_iter_first_vma,   /* use mm->mmap */
	task_vma_iter_next_vma,    /* use curr_vma->vm_next */
	task_vma_iter_find_vma,    /* use find_vma() to find next vma */
};

static struct vm_area_struct *
task_vma_seq_get_next(struct bpf_iter_seq_task_vma_info *info)
{
	struct pid_namespace *ns = info->common.ns;
	enum bpf_task_vma_iter_find_op op;
	struct vm_area_struct *curr_vma;
	struct task_struct *curr_task;
	u32 curr_tid = info->tid;

	/* If this function returns a non-NULL vma, it holds a reference to
	 * the task_struct, and holds read lock on vma->mm->mmap_lock.
	 * If this function returns NULL, it does not hold any reference or
	 * lock.
	 */
	if (info->task) {
		curr_task = info->task;
		curr_vma = info->vma;
		/* In case of lock contention, drop mmap_lock to unblock
		 * the writer.
		 *
		 * After relock, call find(mm, prev_vm_end - 1) to find
		 * new vma to process.
		 *
		 *   +------+------+-----------+
		 *   | VMA1 | VMA2 | VMA3      |
		 *   +------+------+-----------+
		 *   |      |      |           |
		 *  4k     8k     16k         400k
		 *
		 * For example, curr_vma == VMA2. Before unlock, we set
		 *
		 *    prev_vm_start = 8k
		 *    prev_vm_end   = 16k
		 *
		 * There are a few cases:
		 *
		 * 1) VMA2 is freed, but VMA3 exists.
		 *
		 *    find_vma() will return VMA3, just process VMA3.
		 *
		 * 2) VMA2 still exists.
		 *
		 *    find_vma() will return VMA2, process VMA2->next.
		 *
		 * 3) no more vma in this mm.
		 *
		 *    Process the next task.
		 *
		 * 4) find_vma() returns a different vma, VMA2'.
		 *
		 *    4.1) If VMA2 covers same range as VMA2', skip VMA2',
		 *         because we already covered the range;
		 *    4.2) VMA2 and VMA2' covers different ranges, process
		 *         VMA2'.
		 */
		if (mmap_lock_is_contended(curr_task->mm)) {
			info->prev_vm_start = curr_vma->vm_start;
			info->prev_vm_end = curr_vma->vm_end;
			op = task_vma_iter_find_vma;
			mmap_read_unlock(curr_task->mm);
			if (mmap_read_lock_killable(curr_task->mm))
				goto finish;
		} else {
			op = task_vma_iter_next_vma;
		}
	} else {
again:
		curr_task = task_seq_get_next(ns, &curr_tid, true);
		if (!curr_task) {
			info->tid = curr_tid + 1;
			goto finish;
		}

		if (curr_tid != info->tid) {
			info->tid = curr_tid;
			/* new task, process the first vma */
			op = task_vma_iter_first_vma;
		} else {
			/* Found the same tid, which means the user space
			 * finished data in previous buffer and read more.
			 * We dropped mmap_lock before returning to user
			 * space, so it is necessary to use find_vma() to
			 * find the next vma to process.
			 */
			op = task_vma_iter_find_vma;
		}

		if (!curr_task->mm)
			goto next_task;

		if (mmap_read_lock_killable(curr_task->mm))
			goto finish;
	}

	switch (op) {
	case task_vma_iter_first_vma:
		curr_vma = curr_task->mm->mmap;
		break;
	case task_vma_iter_next_vma:
		curr_vma = curr_vma->vm_next;
		break;
	case task_vma_iter_find_vma:
		/* We dropped mmap_lock so it is necessary to use find_vma
		 * to find the next vma. This is similar to the  mechanism
		 * in show_smaps_rollup().
		 */
		curr_vma = find_vma(curr_task->mm, info->prev_vm_end - 1);
		/* case 1) and 4.2) above just use curr_vma */

		/* check for case 2) or case 4.1) above */
		if (curr_vma &&
		    curr_vma->vm_start == info->prev_vm_start &&
		    curr_vma->vm_end == info->prev_vm_end)
			curr_vma = curr_vma->vm_next;
		break;
	}
	if (!curr_vma) {
		/* case 3) above, or case 2) 4.1) with vma->next == NULL */
		mmap_read_unlock(curr_task->mm);
		goto next_task;
	}
	info->task = curr_task;
	info->vma = curr_vma;
	return curr_vma;

next_task:
	put_task_struct(curr_task);
	info->task = NULL;
	curr_tid++;
	goto again;

finish:
	if (curr_task)
		put_task_struct(curr_task);
	info->task = NULL;
	info->vma = NULL;
	return NULL;
}

static void *task_vma_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_task_vma_info *info = seq->private;
	struct vm_area_struct *vma;

	vma = task_vma_seq_get_next(info);
	if (vma && *pos == 0)
		++*pos;

	return vma;
}

static void *task_vma_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_task_vma_info *info = seq->private;

	++*pos;
	return task_vma_seq_get_next(info);
}

struct bpf_iter__task_vma {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct task_struct *, task);
	__bpf_md_ptr(struct vm_area_struct *, vma);
};

DEFINE_BPF_ITER_FUNC(task_vma, struct bpf_iter_meta *meta,
		     struct task_struct *task, struct vm_area_struct *vma)

static int __task_vma_seq_show(struct seq_file *seq, bool in_stop)
{
	struct bpf_iter_seq_task_vma_info *info = seq->private;
	struct bpf_iter__task_vma ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, in_stop);
	if (!prog)
		return 0;

	ctx.meta = &meta;
	ctx.task = info->task;
	ctx.vma = info->vma;
	return bpf_iter_run_prog(prog, &ctx);
}

static int task_vma_seq_show(struct seq_file *seq, void *v)
{
	return __task_vma_seq_show(seq, false);
}

static void task_vma_seq_stop(struct seq_file *seq, void *v)
{
	struct bpf_iter_seq_task_vma_info *info = seq->private;

	if (!v) {
		(void)__task_vma_seq_show(seq, true);
	} else {
		/* info->vma has not been seen by the BPF program. If the
		 * user space reads more, task_vma_seq_get_next should
		 * return this vma again. Set prev_vm_start to ~0UL,
		 * so that we don't skip the vma returned by the next
		 * find_vma() (case task_vma_iter_find_vma in
		 * task_vma_seq_get_next()).
		 */
		info->prev_vm_start = ~0UL;
		info->prev_vm_end = info->vma->vm_end;
		mmap_read_unlock(info->task->mm);
		put_task_struct(info->task);
		info->task = NULL;
	}
}

static const struct seq_operations task_vma_seq_ops = {
	.start	= task_vma_seq_start,
	.next	= task_vma_seq_next,
	.stop	= task_vma_seq_stop,
	.show	= task_vma_seq_show,
};

BTF_ID_LIST(btf_task_file_ids)
BTF_ID(struct, task_struct)
BTF_ID(struct, file)
BTF_ID(struct, vm_area_struct)

static const struct bpf_iter_seq_info task_seq_info = {
	.seq_ops		= &task_seq_ops,
	.init_seq_private	= init_seq_pidns,
	.fini_seq_private	= fini_seq_pidns,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_task_info),
};

static struct bpf_iter_reg task_reg_info = {
	.target			= "task",
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__task, task),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &task_seq_info,
};

static const struct bpf_iter_seq_info task_file_seq_info = {
	.seq_ops		= &task_file_seq_ops,
	.init_seq_private	= init_seq_pidns,
	.fini_seq_private	= fini_seq_pidns,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_task_file_info),
};

static struct bpf_iter_reg task_file_reg_info = {
	.target			= "task_file",
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__task_file, task),
		  PTR_TO_BTF_ID_OR_NULL },
		{ offsetof(struct bpf_iter__task_file, file),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &task_file_seq_info,
};

static const struct bpf_iter_seq_info task_vma_seq_info = {
	.seq_ops		= &task_vma_seq_ops,
	.init_seq_private	= init_seq_pidns,
	.fini_seq_private	= fini_seq_pidns,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_task_vma_info),
};

static struct bpf_iter_reg task_vma_reg_info = {
	.target			= "task_vma",
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__task_vma, task),
		  PTR_TO_BTF_ID_OR_NULL },
		{ offsetof(struct bpf_iter__task_vma, vma),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &task_vma_seq_info,
};

static int __init task_iter_init(void)
{
	int ret;

	task_reg_info.ctx_arg_info[0].btf_id = btf_task_file_ids[0];
	ret = bpf_iter_reg_target(&task_reg_info);
	if (ret)
		return ret;

	task_file_reg_info.ctx_arg_info[0].btf_id = btf_task_file_ids[0];
	task_file_reg_info.ctx_arg_info[1].btf_id = btf_task_file_ids[1];
	ret =  bpf_iter_reg_target(&task_file_reg_info);
	if (ret)
		return ret;

	task_vma_reg_info.ctx_arg_info[0].btf_id = btf_task_file_ids[0];
	task_vma_reg_info.ctx_arg_info[1].btf_id = btf_task_file_ids[2];
	return bpf_iter_reg_target(&task_vma_reg_info);
}
late_initcall(task_iter_init);
