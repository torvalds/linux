
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * Logic: we've got two memory sums for each process, "shared", and
 * "non-shared". Shared memory may get counted more then once, for
 * each process that owns it. Non-shared memory is counted
 * accurately.
 */
void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	struct vm_list_struct *vml;
	unsigned long bytes = 0, sbytes = 0, slack = 0;
        
	down_read(&mm->mmap_sem);
	for (vml = mm->context.vmlist; vml; vml = vml->next) {
		if (!vml->vma)
			continue;

		bytes += kobjsize(vml);
		if (atomic_read(&mm->mm_count) > 1 ||
		    atomic_read(&vml->vma->vm_usage) > 1
		    ) {
			sbytes += kobjsize((void *) vml->vma->vm_start);
			sbytes += kobjsize(vml->vma);
		} else {
			bytes += kobjsize((void *) vml->vma->vm_start);
			bytes += kobjsize(vml->vma);
			slack += kobjsize((void *) vml->vma->vm_start) -
				(vml->vma->vm_end - vml->vma->vm_start);
		}
	}

	if (atomic_read(&mm->mm_count) > 1)
		sbytes += kobjsize(mm);
	else
		bytes += kobjsize(mm);
	
	if (current->fs && atomic_read(&current->fs->count) > 1)
		sbytes += kobjsize(current->fs);
	else
		bytes += kobjsize(current->fs);

	if (current->files && atomic_read(&current->files->count) > 1)
		sbytes += kobjsize(current->files);
	else
		bytes += kobjsize(current->files);

	if (current->sighand && atomic_read(&current->sighand->count) > 1)
		sbytes += kobjsize(current->sighand);
	else
		bytes += kobjsize(current->sighand);

	bytes += kobjsize(current); /* includes kernel stack */

	seq_printf(m,
		"Mem:\t%8lu bytes\n"
		"Slack:\t%8lu bytes\n"
		"Shared:\t%8lu bytes\n",
		bytes, slack, sbytes);

	up_read(&mm->mmap_sem);
}

unsigned long task_vsize(struct mm_struct *mm)
{
	struct vm_list_struct *tbp;
	unsigned long vsize = 0;

	down_read(&mm->mmap_sem);
	for (tbp = mm->context.vmlist; tbp; tbp = tbp->next) {
		if (tbp->vma)
			vsize += kobjsize((void *) tbp->vma->vm_start);
	}
	up_read(&mm->mmap_sem);
	return vsize;
}

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	struct vm_list_struct *tbp;
	int size = kobjsize(mm);

	down_read(&mm->mmap_sem);
	for (tbp = mm->context.vmlist; tbp; tbp = tbp->next) {
		size += kobjsize(tbp);
		if (tbp->vma) {
			size += kobjsize(tbp->vma);
			size += kobjsize((void *) tbp->vma->vm_start);
		}
	}

	size += (*text = mm->end_code - mm->start_code);
	size += (*data = mm->start_stack - mm->start_data);
	up_read(&mm->mmap_sem);
	*resident = size;
	return size;
}

/*
 * display mapping lines for a particular process's /proc/pid/maps
 */
static int show_map(struct seq_file *m, void *_vml)
{
	struct vm_list_struct *vml = _vml;
	struct proc_maps_private *priv = m->private;
	struct task_struct *task = priv->task;

	if (maps_protect && !ptrace_may_attach(task))
		return -EACCES;

	return nommu_vma_show(m, vml->vma);
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	struct vm_list_struct *vml;
	struct mm_struct *mm;
	loff_t n = *pos;

	/* pin the task and mm whilst we play with them */
	priv->task = get_pid_task(priv->pid, PIDTYPE_PID);
	if (!priv->task)
		return NULL;

	mm = mm_for_maps(priv->task);
	if (!mm) {
		put_task_struct(priv->task);
		priv->task = NULL;
		return NULL;
	}

	/* start from the Nth VMA */
	for (vml = mm->context.vmlist; vml; vml = vml->next)
		if (n-- == 0)
			return vml;
	return NULL;
}

static void m_stop(struct seq_file *m, void *_vml)
{
	struct proc_maps_private *priv = m->private;

	if (priv->task) {
		struct mm_struct *mm = priv->task->mm;
		up_read(&mm->mmap_sem);
		mmput(mm);
		put_task_struct(priv->task);
	}
}

static void *m_next(struct seq_file *m, void *_vml, loff_t *pos)
{
	struct vm_list_struct *vml = _vml;

	(*pos)++;
	return vml ? vml->next : NULL;
}

static const struct seq_operations proc_pid_maps_ops = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};

static int maps_open(struct inode *inode, struct file *file)
{
	struct proc_maps_private *priv;
	int ret = -ENOMEM;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv) {
		priv->pid = proc_pid(inode);
		ret = seq_open(file, &proc_pid_maps_ops);
		if (!ret) {
			struct seq_file *m = file->private_data;
			m->private = priv;
		} else {
			kfree(priv);
		}
	}
	return ret;
}

const struct file_operations proc_maps_operations = {
	.open		= maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

