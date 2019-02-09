
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * Logic: we've got two memory sums for each process, "shared", and
 * "non-shared". Shared memory may get counted more than once, for
 * each process that owns it. Non-shared memory is counted
 * accurately.
 */
void task_mem(struct seq_file *m, struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *p;
	unsigned long bytes = 0, sbytes = 0, slack = 0, size;
        
	down_read(&mm->mmap_sem);
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p)) {
		vma = rb_entry(p, struct vm_area_struct, vm_rb);

		bytes += kobjsize(vma);

		region = vma->vm_region;
		if (region) {
			size = kobjsize(region);
			size += region->vm_end - region->vm_start;
		} else {
			size = vma->vm_end - vma->vm_start;
		}

		if (atomic_read(&mm->mm_count) > 1 ||
		    vma->vm_flags & VM_MAYSHARE) {
			sbytes += size;
		} else {
			bytes += size;
			if (region)
				slack = region->vm_end - vma->vm_end;
		}
	}

	if (atomic_read(&mm->mm_count) > 1)
		sbytes += kobjsize(mm);
	else
		bytes += kobjsize(mm);
	
	if (current->fs && current->fs->users > 1)
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
	struct vm_area_struct *vma;
	struct rb_node *p;
	unsigned long vsize = 0;

	down_read(&mm->mmap_sem);
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p)) {
		vma = rb_entry(p, struct vm_area_struct, vm_rb);
		vsize += vma->vm_end - vma->vm_start;
	}
	up_read(&mm->mmap_sem);
	return vsize;
}

unsigned long task_statm(struct mm_struct *mm,
			 unsigned long *shared, unsigned long *text,
			 unsigned long *data, unsigned long *resident)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *p;
	unsigned long size = kobjsize(mm);

	down_read(&mm->mmap_sem);
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p)) {
		vma = rb_entry(p, struct vm_area_struct, vm_rb);
		size += kobjsize(vma);
		region = vma->vm_region;
		if (region) {
			size += kobjsize(region);
			size += region->vm_end - region->vm_start;
		}
	}

	*text = (PAGE_ALIGN(mm->end_code) - (mm->start_code & PAGE_MASK))
		>> PAGE_SHIFT;
	*data = (PAGE_ALIGN(mm->start_stack) - (mm->start_data & PAGE_MASK))
		>> PAGE_SHIFT;
	up_read(&mm->mmap_sem);
	size >>= PAGE_SHIFT;
	size += *text + *data;
	*resident = size;
	return size;
}

static pid_t pid_of_stack(struct proc_maps_private *priv,
				struct vm_area_struct *vma, bool is_pid)
{
	struct inode *inode = priv->inode;
	struct task_struct *task;
	pid_t ret = 0;

	rcu_read_lock();
	task = pid_task(proc_pid(inode), PIDTYPE_PID);
	if (task) {
		task = task_of_stack(task, vma, is_pid);
		if (task)
			ret = task_pid_nr_ns(task, inode->i_sb->s_fs_info);
	}
	rcu_read_unlock();

	return ret;
}

/*
 * display a single VMA to a sequenced file
 */
static int nommu_vma_show(struct seq_file *m, struct vm_area_struct *vma,
			  int is_pid)
{
	struct mm_struct *mm = vma->vm_mm;
	struct proc_maps_private *priv = m->private;
	unsigned long ino = 0;
	struct file *file;
	dev_t dev = 0;
	int flags;
	unsigned long long pgoff = 0;

	flags = vma->vm_flags;
	file = vma->vm_file;

	if (file) {
		struct inode *inode = file_inode(vma->vm_file);
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = (loff_t)vma->vm_pgoff << PAGE_SHIFT;
	}

	seq_setwidth(m, 25 + sizeof(void *) * 6 - 1);
	seq_printf(m,
		   "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu ",
		   vma->vm_start,
		   vma->vm_end,
		   flags & VM_READ ? 'r' : '-',
		   flags & VM_WRITE ? 'w' : '-',
		   flags & VM_EXEC ? 'x' : '-',
		   flags & VM_MAYSHARE ? flags & VM_SHARED ? 'S' : 's' : 'p',
		   pgoff,
		   MAJOR(dev), MINOR(dev), ino);

	if (file) {
		seq_pad(m, ' ');
		seq_file_path(m, file, "");
	} else if (mm) {
		pid_t tid = pid_of_stack(priv, vma, is_pid);

		if (tid != 0) {
			seq_pad(m, ' ');
			/*
			 * Thread stack in /proc/PID/task/TID/maps or
			 * the main process stack.
			 */
			if (!is_pid || (vma->vm_start <= mm->start_stack &&
			    vma->vm_end >= mm->start_stack))
				seq_printf(m, "[stack]");
			else
				seq_printf(m, "[stack:%d]", tid);
		}
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display mapping lines for a particular process's /proc/pid/maps
 */
static int show_map(struct seq_file *m, void *_p, int is_pid)
{
	struct rb_node *p = _p;

	return nommu_vma_show(m, rb_entry(p, struct vm_area_struct, vm_rb),
			      is_pid);
}

static int show_pid_map(struct seq_file *m, void *_p)
{
	return show_map(m, _p, 1);
}

static int show_tid_map(struct seq_file *m, void *_p)
{
	return show_map(m, _p, 0);
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	struct mm_struct *mm;
	struct rb_node *p;
	loff_t n = *pos;

	/* pin the task and mm whilst we play with them */
	priv->task = get_proc_task(priv->inode);
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	mm = priv->mm;
	if (!mm || !atomic_inc_not_zero(&mm->mm_users))
		return NULL;

	down_read(&mm->mmap_sem);
	/* start from the Nth VMA */
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p))
		if (n-- == 0)
			return p;

	up_read(&mm->mmap_sem);
	mmput(mm);
	return NULL;
}

static void m_stop(struct seq_file *m, void *_vml)
{
	struct proc_maps_private *priv = m->private;

	if (!IS_ERR_OR_NULL(_vml)) {
		up_read(&priv->mm->mmap_sem);
		mmput(priv->mm);
	}
	if (priv->task) {
		put_task_struct(priv->task);
		priv->task = NULL;
	}
}

static void *m_next(struct seq_file *m, void *_p, loff_t *pos)
{
	struct rb_node *p = _p;

	(*pos)++;
	return p ? rb_next(p) : NULL;
}

static const struct seq_operations proc_pid_maps_ops = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_pid_map
};

static const struct seq_operations proc_tid_maps_ops = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_tid_map
};

static int maps_open(struct inode *inode, struct file *file,
		     const struct seq_operations *ops)
{
	struct proc_maps_private *priv;

	priv = __seq_open_private(file, ops, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	priv->inode = inode;
	priv->mm = proc_mem_open(inode, PTRACE_MODE_READ);
	if (IS_ERR(priv->mm)) {
		int err = PTR_ERR(priv->mm);

		seq_release_private(inode, file);
		return err;
	}

	return 0;
}


static int map_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct proc_maps_private *priv = seq->private;

	if (priv->mm)
		mmdrop(priv->mm);

	return seq_release_private(inode, file);
}

static int pid_maps_open(struct inode *inode, struct file *file)
{
	return maps_open(inode, file, &proc_pid_maps_ops);
}

static int tid_maps_open(struct inode *inode, struct file *file)
{
	return maps_open(inode, file, &proc_tid_maps_ops);
}

const struct file_operations proc_pid_maps_operations = {
	.open		= pid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= map_release,
};

const struct file_operations proc_tid_maps_operations = {
	.open		= tid_maps_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= map_release,
};

