
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs_struct.h>
#include <linux/mount.h>
#include <linux/ptrace.h>
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

int task_statm(struct mm_struct *mm, int *shared, int *text,
	       int *data, int *resident)
{
	struct vm_area_struct *vma;
	struct vm_region *region;
	struct rb_node *p;
	int size = kobjsize(mm);

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

	size += (*text = mm->end_code - mm->start_code);
	size += (*data = mm->start_stack - mm->start_data);
	up_read(&mm->mmap_sem);
	*resident = size;
	return size;
}

/*
 * display a single VMA to a sequenced file
 */
static int nommu_vma_show(struct seq_file *m, struct vm_area_struct *vma)
{
	unsigned long ino = 0;
	struct file *file;
	dev_t dev = 0;
	int flags, len;
	unsigned long long pgoff = 0;

	flags = vma->vm_flags;
	file = vma->vm_file;

	if (file) {
		struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = (loff_t)vma->pg_off << PAGE_SHIFT;
	}

	seq_printf(m,
		   "%08lx-%08lx %c%c%c%c %08llx %02x:%02x %lu %n",
		   vma->vm_start,
		   vma->vm_end,
		   flags & VM_READ ? 'r' : '-',
		   flags & VM_WRITE ? 'w' : '-',
		   flags & VM_EXEC ? 'x' : '-',
		   flags & VM_MAYSHARE ? flags & VM_SHARED ? 'S' : 's' : 'p',
		   pgoff,
		   MAJOR(dev), MINOR(dev), ino, &len);

	if (file) {
		len = 25 + sizeof(void *) * 6 - len;
		if (len < 1)
			len = 1;
		seq_printf(m, "%*c", len, ' ');
		seq_path(m, &file->f_path, "");
	}

	seq_putc(m, '\n');
	return 0;
}

/*
 * display mapping lines for a particular process's /proc/pid/maps
 */
static int show_map(struct seq_file *m, void *_p)
{
	struct rb_node *p = _p;

	return nommu_vma_show(m, rb_entry(p, struct vm_area_struct, vm_rb));
}

static void *m_start(struct seq_file *m, loff_t *pos)
{
	struct proc_maps_private *priv = m->private;
	struct mm_struct *mm;
	struct rb_node *p;
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
	for (p = rb_first(&mm->mm_rb); p; p = rb_next(p))
		if (n-- == 0)
			return p;
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

