// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */

#include <linux/init.h>
#include <linux/namei.h>
#include <linux/pid_namespace.h>
#include <linux/fs.h>
#include <linux/filter.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/btf_ids.h>
#include <linux/mm_types.h>
#include "mmap_unlock_work.h"

static const char * const iter_task_type_names[] = {
	"ALL",
	"TID",
	"PID",
};

struct bpf_iter_seq_task_common {
	struct pid_namespace *ns;
	enum bpf_iter_task_type	type;
	u32 pid;
	u32 pid_visiting;
};

struct bpf_iter_seq_task_info {
	/* The first field must be struct bpf_iter_seq_task_common.
	 * this is assumed by {init, fini}_seq_pidns() callback functions.
	 */
	struct bpf_iter_seq_task_common common;
	u32 tid;
};

static struct task_struct *task_group_seq_get_next(struct bpf_iter_seq_task_common *common,
						   u32 *tid,
						   bool skip_if_dup_files)
{
	struct task_struct *task;
	struct pid *pid;
	u32 next_tid;

	if (!*tid) {
		/* The first time, the iterator calls this function. */
		pid = find_pid_ns(common->pid, common->ns);
		task = get_pid_task(pid, PIDTYPE_TGID);
		if (!task)
			return NULL;

		*tid = common->pid;
		common->pid_visiting = common->pid;

		return task;
	}

	/* If the control returns to user space and comes back to the
	 * kernel again, *tid and common->pid_visiting should be the
	 * same for task_seq_start() to pick up the correct task.
	 */
	if (*tid == common->pid_visiting) {
		pid = find_pid_ns(common->pid_visiting, common->ns);
		task = get_pid_task(pid, PIDTYPE_PID);

		return task;
	}

	task = find_task_by_pid_ns(common->pid_visiting, common->ns);
	if (!task)
		return NULL;

retry:
	task = __next_thread(task);
	if (!task)
		return NULL;

	next_tid = __task_pid_nr_ns(task, PIDTYPE_PID, common->ns);
	if (!next_tid)
		goto retry;

	if (skip_if_dup_files && task->files == task->group_leader->files)
		goto retry;

	*tid = common->pid_visiting = next_tid;
	get_task_struct(task);
	return task;
}

static struct task_struct *task_seq_get_next(struct bpf_iter_seq_task_common *common,
					     u32 *tid,
					     bool skip_if_dup_files)
{
	struct task_struct *task = NULL;
	struct pid *pid;

	if (common->type == BPF_TASK_ITER_TID) {
		if (*tid && *tid != common->pid)
			return NULL;
		rcu_read_lock();
		pid = find_pid_ns(common->pid, common->ns);
		if (pid) {
			task = get_pid_task(pid, PIDTYPE_PID);
			*tid = common->pid;
		}
		rcu_read_unlock();

		return task;
	}

	if (common->type == BPF_TASK_ITER_TGID) {
		rcu_read_lock();
		task = task_group_seq_get_next(common, tid, skip_if_dup_files);
		rcu_read_unlock();

		return task;
	}

	rcu_read_lock();
retry:
	pid = find_ge_pid(*tid, common->ns);
	if (pid) {
		*tid = pid_nr_ns(pid, common->ns);
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

	task = task_seq_get_next(&info->common, &info->tid, false);
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
	task = task_seq_get_next(&info->common, &info->tid, false);
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

static int bpf_iter_attach_task(struct bpf_prog *prog,
				union bpf_iter_link_info *linfo,
				struct bpf_iter_aux_info *aux)
{
	unsigned int flags;
	struct pid *pid;
	pid_t tgid;

	if ((!!linfo->task.tid + !!linfo->task.pid + !!linfo->task.pid_fd) > 1)
		return -EINVAL;

	aux->task.type = BPF_TASK_ITER_ALL;
	if (linfo->task.tid != 0) {
		aux->task.type = BPF_TASK_ITER_TID;
		aux->task.pid = linfo->task.tid;
	}
	if (linfo->task.pid != 0) {
		aux->task.type = BPF_TASK_ITER_TGID;
		aux->task.pid = linfo->task.pid;
	}
	if (linfo->task.pid_fd != 0) {
		aux->task.type = BPF_TASK_ITER_TGID;

		pid = pidfd_get_pid(linfo->task.pid_fd, &flags);
		if (IS_ERR(pid))
			return PTR_ERR(pid);

		tgid = pid_nr_ns(pid, task_active_pid_ns(current));
		aux->task.pid = tgid;
		put_pid(pid);
	}

	return 0;
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
	u32 saved_tid = info->tid;
	struct task_struct *curr_task;
	unsigned int curr_fd = info->fd;
	struct file *f;

	/* If this function returns a non-NULL file object,
	 * it held a reference to the task/file.
	 * Otherwise, it does not hold any reference.
	 */
again:
	if (info->task) {
		curr_task = info->task;
		curr_fd = info->fd;
	} else {
		curr_task = task_seq_get_next(&info->common, &info->tid, true);
                if (!curr_task) {
                        info->task = NULL;
                        return NULL;
                }

		/* set info->task */
		info->task = curr_task;
		if (saved_tid == info->tid)
			curr_fd = info->fd;
		else
			curr_fd = 0;
	}

	f = fget_task_next(curr_task, &curr_fd);
	if (f) {
		/* set info->fd */
		info->fd = curr_fd;
		return f;
	}

	/* the current task is done, go to the next task */
	put_task_struct(curr_task);

	if (info->common.type == BPF_TASK_ITER_TID) {
		info->task = NULL;
		return NULL;
	}

	info->task = NULL;
	info->fd = 0;
	saved_tid = ++(info->tid);
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
	common->type = aux->task.type;
	common->pid = aux->task.pid;

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
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	u32 tid;
	unsigned long prev_vm_start;
	unsigned long prev_vm_end;
};

enum bpf_task_vma_iter_find_op {
	task_vma_iter_first_vma,   /* use find_vma() with addr 0 */
	task_vma_iter_next_vma,    /* use vma_next() with curr_vma */
	task_vma_iter_find_vma,    /* use find_vma() to find next vma */
};

static struct vm_area_struct *
task_vma_seq_get_next(struct bpf_iter_seq_task_vma_info *info)
{
	enum bpf_task_vma_iter_find_op op;
	struct vm_area_struct *curr_vma;
	struct task_struct *curr_task;
	struct mm_struct *curr_mm;
	u32 saved_tid = info->tid;

	/* If this function returns a non-NULL vma, it holds a reference to
	 * the task_struct, holds a refcount on mm->mm_users, and holds
	 * read lock on vma->mm->mmap_lock.
	 * If this function returns NULL, it does not hold any reference or
	 * lock.
	 */
	if (info->task) {
		curr_task = info->task;
		curr_vma = info->vma;
		curr_mm = info->mm;
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
		if (mmap_lock_is_contended(curr_mm)) {
			info->prev_vm_start = curr_vma->vm_start;
			info->prev_vm_end = curr_vma->vm_end;
			op = task_vma_iter_find_vma;
			mmap_read_unlock(curr_mm);
			if (mmap_read_lock_killable(curr_mm)) {
				mmput(curr_mm);
				goto finish;
			}
		} else {
			op = task_vma_iter_next_vma;
		}
	} else {
again:
		curr_task = task_seq_get_next(&info->common, &info->tid, true);
		if (!curr_task) {
			info->tid++;
			goto finish;
		}

		if (saved_tid != info->tid) {
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

		curr_mm = get_task_mm(curr_task);
		if (!curr_mm)
			goto next_task;

		if (mmap_read_lock_killable(curr_mm)) {
			mmput(curr_mm);
			goto finish;
		}
	}

	switch (op) {
	case task_vma_iter_first_vma:
		curr_vma = find_vma(curr_mm, 0);
		break;
	case task_vma_iter_next_vma:
		curr_vma = find_vma(curr_mm, curr_vma->vm_end);
		break;
	case task_vma_iter_find_vma:
		/* We dropped mmap_lock so it is necessary to use find_vma
		 * to find the next vma. This is similar to the  mechanism
		 * in show_smaps_rollup().
		 */
		curr_vma = find_vma(curr_mm, info->prev_vm_end - 1);
		/* case 1) and 4.2) above just use curr_vma */

		/* check for case 2) or case 4.1) above */
		if (curr_vma &&
		    curr_vma->vm_start == info->prev_vm_start &&
		    curr_vma->vm_end == info->prev_vm_end)
			curr_vma = find_vma(curr_mm, curr_vma->vm_end);
		break;
	}
	if (!curr_vma) {
		/* case 3) above, or case 2) 4.1) with vma->next == NULL */
		mmap_read_unlock(curr_mm);
		mmput(curr_mm);
		goto next_task;
	}
	info->task = curr_task;
	info->vma = curr_vma;
	info->mm = curr_mm;
	return curr_vma;

next_task:
	if (info->common.type == BPF_TASK_ITER_TID)
		goto finish;

	put_task_struct(curr_task);
	info->task = NULL;
	info->mm = NULL;
	info->tid++;
	goto again;

finish:
	if (curr_task)
		put_task_struct(curr_task);
	info->task = NULL;
	info->vma = NULL;
	info->mm = NULL;
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
		mmap_read_unlock(info->mm);
		mmput(info->mm);
		info->mm = NULL;
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

static const struct bpf_iter_seq_info task_seq_info = {
	.seq_ops		= &task_seq_ops,
	.init_seq_private	= init_seq_pidns,
	.fini_seq_private	= fini_seq_pidns,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_task_info),
};

static int bpf_iter_fill_link_info(const struct bpf_iter_aux_info *aux, struct bpf_link_info *info)
{
	switch (aux->task.type) {
	case BPF_TASK_ITER_TID:
		info->iter.task.tid = aux->task.pid;
		break;
	case BPF_TASK_ITER_TGID:
		info->iter.task.pid = aux->task.pid;
		break;
	default:
		break;
	}
	return 0;
}

static void bpf_iter_task_show_fdinfo(const struct bpf_iter_aux_info *aux, struct seq_file *seq)
{
	seq_printf(seq, "task_type:\t%s\n", iter_task_type_names[aux->task.type]);
	if (aux->task.type == BPF_TASK_ITER_TID)
		seq_printf(seq, "tid:\t%u\n", aux->task.pid);
	else if (aux->task.type == BPF_TASK_ITER_TGID)
		seq_printf(seq, "pid:\t%u\n", aux->task.pid);
}

static struct bpf_iter_reg task_reg_info = {
	.target			= "task",
	.attach_target		= bpf_iter_attach_task,
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__task, task),
		  PTR_TO_BTF_ID_OR_NULL | PTR_TRUSTED },
	},
	.seq_info		= &task_seq_info,
	.fill_link_info		= bpf_iter_fill_link_info,
	.show_fdinfo		= bpf_iter_task_show_fdinfo,
};

static const struct bpf_iter_seq_info task_file_seq_info = {
	.seq_ops		= &task_file_seq_ops,
	.init_seq_private	= init_seq_pidns,
	.fini_seq_private	= fini_seq_pidns,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_task_file_info),
};

static struct bpf_iter_reg task_file_reg_info = {
	.target			= "task_file",
	.attach_target		= bpf_iter_attach_task,
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__task_file, task),
		  PTR_TO_BTF_ID_OR_NULL },
		{ offsetof(struct bpf_iter__task_file, file),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &task_file_seq_info,
	.fill_link_info		= bpf_iter_fill_link_info,
	.show_fdinfo		= bpf_iter_task_show_fdinfo,
};

static const struct bpf_iter_seq_info task_vma_seq_info = {
	.seq_ops		= &task_vma_seq_ops,
	.init_seq_private	= init_seq_pidns,
	.fini_seq_private	= fini_seq_pidns,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_task_vma_info),
};

static struct bpf_iter_reg task_vma_reg_info = {
	.target			= "task_vma",
	.attach_target		= bpf_iter_attach_task,
	.feature		= BPF_ITER_RESCHED,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__task_vma, task),
		  PTR_TO_BTF_ID_OR_NULL },
		{ offsetof(struct bpf_iter__task_vma, vma),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &task_vma_seq_info,
	.fill_link_info		= bpf_iter_fill_link_info,
	.show_fdinfo		= bpf_iter_task_show_fdinfo,
};

BPF_CALL_5(bpf_find_vma, struct task_struct *, task, u64, start,
	   bpf_callback_t, callback_fn, void *, callback_ctx, u64, flags)
{
	struct mmap_unlock_irq_work *work = NULL;
	struct vm_area_struct *vma;
	bool irq_work_busy = false;
	struct mm_struct *mm;
	int ret = -ENOENT;

	if (flags)
		return -EINVAL;

	if (!task)
		return -ENOENT;

	mm = task->mm;
	if (!mm)
		return -ENOENT;

	irq_work_busy = bpf_mmap_unlock_get_irq_work(&work);

	if (irq_work_busy || !mmap_read_trylock(mm))
		return -EBUSY;

	vma = find_vma(mm, start);

	if (vma && vma->vm_start <= start && vma->vm_end > start) {
		callback_fn((u64)(long)task, (u64)(long)vma,
			    (u64)(long)callback_ctx, 0, 0);
		ret = 0;
	}
	bpf_mmap_unlock_mm(work, mm);
	return ret;
}

const struct bpf_func_proto bpf_find_vma_proto = {
	.func		= bpf_find_vma,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &btf_tracing_ids[BTF_TRACING_TYPE_TASK],
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_PTR_TO_FUNC,
	.arg4_type	= ARG_PTR_TO_STACK_OR_NULL,
	.arg5_type	= ARG_ANYTHING,
};

struct bpf_iter_task_vma_kern_data {
	struct task_struct *task;
	struct mm_struct *mm;
	struct mmap_unlock_irq_work *work;
	struct vma_iterator vmi;
};

struct bpf_iter_task_vma {
	/* opaque iterator state; having __u64 here allows to preserve correct
	 * alignment requirements in vmlinux.h, generated from BTF
	 */
	__u64 __opaque[1];
} __attribute__((aligned(8)));

/* Non-opaque version of bpf_iter_task_vma */
struct bpf_iter_task_vma_kern {
	struct bpf_iter_task_vma_kern_data *data;
} __attribute__((aligned(8)));

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_iter_task_vma_new(struct bpf_iter_task_vma *it,
				      struct task_struct *task, u64 addr)
{
	struct bpf_iter_task_vma_kern *kit = (void *)it;
	bool irq_work_busy = false;
	int err;

	BUILD_BUG_ON(sizeof(struct bpf_iter_task_vma_kern) != sizeof(struct bpf_iter_task_vma));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_task_vma_kern) != __alignof__(struct bpf_iter_task_vma));

	/* is_iter_reg_valid_uninit guarantees that kit hasn't been initialized
	 * before, so non-NULL kit->data doesn't point to previously
	 * bpf_mem_alloc'd bpf_iter_task_vma_kern_data
	 */
	kit->data = bpf_mem_alloc(&bpf_global_ma, sizeof(struct bpf_iter_task_vma_kern_data));
	if (!kit->data)
		return -ENOMEM;

	kit->data->task = get_task_struct(task);
	kit->data->mm = task->mm;
	if (!kit->data->mm) {
		err = -ENOENT;
		goto err_cleanup_iter;
	}

	/* kit->data->work == NULL is valid after bpf_mmap_unlock_get_irq_work */
	irq_work_busy = bpf_mmap_unlock_get_irq_work(&kit->data->work);
	if (irq_work_busy || !mmap_read_trylock(kit->data->mm)) {
		err = -EBUSY;
		goto err_cleanup_iter;
	}

	vma_iter_init(&kit->data->vmi, kit->data->mm, addr);
	return 0;

err_cleanup_iter:
	if (kit->data->task)
		put_task_struct(kit->data->task);
	bpf_mem_free(&bpf_global_ma, kit->data);
	/* NULL kit->data signals failed bpf_iter_task_vma initialization */
	kit->data = NULL;
	return err;
}

__bpf_kfunc struct vm_area_struct *bpf_iter_task_vma_next(struct bpf_iter_task_vma *it)
{
	struct bpf_iter_task_vma_kern *kit = (void *)it;

	if (!kit->data) /* bpf_iter_task_vma_new failed */
		return NULL;
	return vma_next(&kit->data->vmi);
}

__bpf_kfunc void bpf_iter_task_vma_destroy(struct bpf_iter_task_vma *it)
{
	struct bpf_iter_task_vma_kern *kit = (void *)it;

	if (kit->data) {
		bpf_mmap_unlock_mm(kit->data->work, kit->data->mm);
		put_task_struct(kit->data->task);
		bpf_mem_free(&bpf_global_ma, kit->data);
	}
}

__bpf_kfunc_end_defs();

#ifdef CONFIG_CGROUPS

struct bpf_iter_css_task {
	__u64 __opaque[1];
} __attribute__((aligned(8)));

struct bpf_iter_css_task_kern {
	struct css_task_iter *css_it;
} __attribute__((aligned(8)));

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_iter_css_task_new(struct bpf_iter_css_task *it,
		struct cgroup_subsys_state *css, unsigned int flags)
{
	struct bpf_iter_css_task_kern *kit = (void *)it;

	BUILD_BUG_ON(sizeof(struct bpf_iter_css_task_kern) != sizeof(struct bpf_iter_css_task));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_css_task_kern) !=
					__alignof__(struct bpf_iter_css_task));
	kit->css_it = NULL;
	switch (flags) {
	case CSS_TASK_ITER_PROCS | CSS_TASK_ITER_THREADED:
	case CSS_TASK_ITER_PROCS:
	case 0:
		break;
	default:
		return -EINVAL;
	}

	kit->css_it = bpf_mem_alloc(&bpf_global_ma, sizeof(struct css_task_iter));
	if (!kit->css_it)
		return -ENOMEM;
	css_task_iter_start(css, flags, kit->css_it);
	return 0;
}

__bpf_kfunc struct task_struct *bpf_iter_css_task_next(struct bpf_iter_css_task *it)
{
	struct bpf_iter_css_task_kern *kit = (void *)it;

	if (!kit->css_it)
		return NULL;
	return css_task_iter_next(kit->css_it);
}

__bpf_kfunc void bpf_iter_css_task_destroy(struct bpf_iter_css_task *it)
{
	struct bpf_iter_css_task_kern *kit = (void *)it;

	if (!kit->css_it)
		return;
	css_task_iter_end(kit->css_it);
	bpf_mem_free(&bpf_global_ma, kit->css_it);
}

__bpf_kfunc_end_defs();

#endif /* CONFIG_CGROUPS */

struct bpf_iter_task {
	__u64 __opaque[3];
} __attribute__((aligned(8)));

struct bpf_iter_task_kern {
	struct task_struct *task;
	struct task_struct *pos;
	unsigned int flags;
} __attribute__((aligned(8)));

enum {
	/* all process in the system */
	BPF_TASK_ITER_ALL_PROCS,
	/* all threads in the system */
	BPF_TASK_ITER_ALL_THREADS,
	/* all threads of a specific process */
	BPF_TASK_ITER_PROC_THREADS
};

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_iter_task_new(struct bpf_iter_task *it,
		struct task_struct *task__nullable, unsigned int flags)
{
	struct bpf_iter_task_kern *kit = (void *)it;

	BUILD_BUG_ON(sizeof(struct bpf_iter_task_kern) > sizeof(struct bpf_iter_task));
	BUILD_BUG_ON(__alignof__(struct bpf_iter_task_kern) !=
					__alignof__(struct bpf_iter_task));

	kit->pos = NULL;

	switch (flags) {
	case BPF_TASK_ITER_ALL_THREADS:
	case BPF_TASK_ITER_ALL_PROCS:
		break;
	case BPF_TASK_ITER_PROC_THREADS:
		if (!task__nullable)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (flags == BPF_TASK_ITER_PROC_THREADS)
		kit->task = task__nullable;
	else
		kit->task = &init_task;
	kit->pos = kit->task;
	kit->flags = flags;
	return 0;
}

__bpf_kfunc struct task_struct *bpf_iter_task_next(struct bpf_iter_task *it)
{
	struct bpf_iter_task_kern *kit = (void *)it;
	struct task_struct *pos;
	unsigned int flags;

	flags = kit->flags;
	pos = kit->pos;

	if (!pos)
		return pos;

	if (flags == BPF_TASK_ITER_ALL_PROCS)
		goto get_next_task;

	kit->pos = __next_thread(kit->pos);
	if (kit->pos || flags == BPF_TASK_ITER_PROC_THREADS)
		return pos;

get_next_task:
	kit->task = next_task(kit->task);
	if (kit->task == &init_task)
		kit->pos = NULL;
	else
		kit->pos = kit->task;

	return pos;
}

__bpf_kfunc void bpf_iter_task_destroy(struct bpf_iter_task *it)
{
}

__bpf_kfunc_end_defs();

DEFINE_PER_CPU(struct mmap_unlock_irq_work, mmap_unlock_work);

static void do_mmap_read_unlock(struct irq_work *entry)
{
	struct mmap_unlock_irq_work *work;

	if (WARN_ON_ONCE(IS_ENABLED(CONFIG_PREEMPT_RT)))
		return;

	work = container_of(entry, struct mmap_unlock_irq_work, irq_work);
	mmap_read_unlock_non_owner(work->mm);
}

static int __init task_iter_init(void)
{
	struct mmap_unlock_irq_work *work;
	int ret, cpu;

	for_each_possible_cpu(cpu) {
		work = per_cpu_ptr(&mmap_unlock_work, cpu);
		init_irq_work(&work->irq_work, do_mmap_read_unlock);
	}

	task_reg_info.ctx_arg_info[0].btf_id = btf_tracing_ids[BTF_TRACING_TYPE_TASK];
	ret = bpf_iter_reg_target(&task_reg_info);
	if (ret)
		return ret;

	task_file_reg_info.ctx_arg_info[0].btf_id = btf_tracing_ids[BTF_TRACING_TYPE_TASK];
	task_file_reg_info.ctx_arg_info[1].btf_id = btf_tracing_ids[BTF_TRACING_TYPE_FILE];
	ret =  bpf_iter_reg_target(&task_file_reg_info);
	if (ret)
		return ret;

	task_vma_reg_info.ctx_arg_info[0].btf_id = btf_tracing_ids[BTF_TRACING_TYPE_TASK];
	task_vma_reg_info.ctx_arg_info[1].btf_id = btf_tracing_ids[BTF_TRACING_TYPE_VMA];
	return bpf_iter_reg_target(&task_vma_reg_info);
}
late_initcall(task_iter_init);
