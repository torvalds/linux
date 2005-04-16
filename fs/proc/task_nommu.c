
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/seq_file.h>
#include "internal.h"

/*
 * Logic: we've got two memory sums for each process, "shared", and
 * "non-shared". Shared memory may get counted more then once, for
 * each process that owns it. Non-shared memory is counted
 * accurately.
 */
char *task_mem(struct mm_struct *mm, char *buffer)
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

	buffer += sprintf(buffer,
		"Mem:\t%8lu bytes\n"
		"Slack:\t%8lu bytes\n"
		"Shared:\t%8lu bytes\n",
		bytes, slack, sbytes);

	up_read(&mm->mmap_sem);
	return buffer;
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

int proc_exe_link(struct inode *inode, struct dentry **dentry, struct vfsmount **mnt)
{
	struct vm_list_struct *vml;
	struct vm_area_struct *vma;
	struct task_struct *task = proc_task(inode);
	struct mm_struct *mm = get_task_mm(task);
	int result = -ENOENT;

	if (!mm)
		goto out;
	down_read(&mm->mmap_sem);

	vml = mm->context.vmlist;
	vma = NULL;
	while (vml) {
		if ((vml->vma->vm_flags & VM_EXECUTABLE) && vml->vma->vm_file) {
			vma = vml->vma;
			break;
		}
		vml = vml->next;
	}

	if (vma) {
		*mnt = mntget(vma->vm_file->f_vfsmnt);
		*dentry = dget(vma->vm_file->f_dentry);
		result = 0;
	}

	up_read(&mm->mmap_sem);
	mmput(mm);
out:
	return result;
}

/*
 * Albert D. Cahalan suggested to fake entries for the traditional
 * sections here.  This might be worth investigating.
 */
static int show_map(struct seq_file *m, void *v)
{
	return 0;
}
static void *m_start(struct seq_file *m, loff_t *pos)
{
	return NULL;
}
static void m_stop(struct seq_file *m, void *v)
{
}
static void *m_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}
struct seq_operations proc_pid_maps_op = {
	.start	= m_start,
	.next	= m_next,
	.stop	= m_stop,
	.show	= show_map
};
