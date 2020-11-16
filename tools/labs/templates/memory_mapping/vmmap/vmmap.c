/*
 * PSO - Memory Mapping Lab(#11)
 *
 * Exercise #2: memory mapping using vmalloc'd kernel areas
 */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "../test/mmap-test.h"


MODULE_DESCRIPTION("simple mmap driver");
MODULE_AUTHOR("PSO");
MODULE_LICENSE("Dual BSD/GPL");

#define MY_MAJOR	42

/* how many pages do we actually vmalloc */
#define NPAGES		16

/* character device basic structure */
static struct cdev mmap_cdev;

/* pointer to the vmalloc'd area, rounded up to a page boundary */
static char *vmalloc_area;

static int my_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int my_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t my_read(struct file *file, char __user *user_buffer,
		size_t size, loff_t *offset)
{
	/* TODO 2/2: check size doesn't exceed our mapped area size */
	if (size > NPAGES * PAGE_SIZE)
		size = NPAGES * PAGE_SIZE;

	/* TODO 2/2: copy from mapped area to user buffer */
	if (copy_to_user(user_buffer, vmalloc_area, size))
		return -EFAULT;

	return size;
}

static ssize_t my_write(struct file *file, const char __user *user_buffer,
		size_t size, loff_t *offset)
{
	/* TODO 2/2: check size doesn't exceed our mapped area size */
	if (size > NPAGES * PAGE_SIZE)
		size = NPAGES * PAGE_SIZE;

	/* TODO 2/3: copy from user buffer to mapped area */
	memset(vmalloc_area, 0, NPAGES * PAGE_SIZE);
	if (copy_from_user(vmalloc_area, user_buffer, size))
		return -EFAULT;

	return size;
}

static int my_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;
	long length = vma->vm_end - vma->vm_start;
	unsigned long start = vma->vm_start;
	char *vmalloc_area_ptr = vmalloc_area;
	unsigned long pfn;

	if (length > NPAGES * PAGE_SIZE)
		return -EIO;

	/* TODO 1/9: map pages individually */
	while (length > 0) {
		pfn = vmalloc_to_pfn(vmalloc_area_ptr);
		ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED);
		if (ret < 0)
			return ret;
		start += PAGE_SIZE;
		vmalloc_area_ptr += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	return 0;
}

static const struct file_operations mmap_fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.release = my_release,
	.mmap = my_mmap,
	.read = my_read,
	.write = my_write
};

static int my_seq_show(struct seq_file *seq, void *v)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma_iterator;
	unsigned long total = 0;

	/* TODO 3: Get current process' mm_struct */
	mm = get_task_mm(current);

	/* TODO 3/6: Iterate through all memory mappings and print ranges */
	vma_iterator = mm->mmap;
	while (vma_iterator != NULL) {
		pr_info("%lx %lx\n", vma_iterator->vm_start, vma_iterator->vm_end);
		total += vma_iterator->vm_end - vma_iterator->vm_start;
		vma_iterator = vma_iterator->vm_next;
	}

	/* TODO 3: Release mm_struct */
	mmput(mm);

	/* TODO 3: write the total count to file  */
	seq_printf(seq, "%lu %s\n", total, current->comm);
	return 0;
}

static int my_seq_open(struct inode *inode, struct file *file)
{
	/* TODO 3: Register the display function */
	return single_open(file, my_seq_show, NULL);
}

static const struct file_operations my_proc_file_ops = {
	.owner   = THIS_MODULE,
	.open    = my_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int __init my_init(void)
{
	int ret = 0;
	int i;
	/* TODO 3/7: create a new entry in procfs */
	struct proc_dir_entry *entry;

	entry = proc_create(PROC_ENTRY_NAME, 0, NULL, &my_proc_file_ops);
	if (!entry) {
		ret = -ENOMEM;
		goto out;
	}

	ret = register_chrdev_region(MKDEV(MY_MAJOR, 0), 1, "mymap");
	if (ret < 0) {
		pr_err("could not register region\n");
		goto out_no_chrdev;
	}

	/* TODO 1/6: allocate NPAGES using vmalloc */
	vmalloc_area = (char *)vmalloc(NPAGES * PAGE_SIZE);
	if (vmalloc_area == NULL) {
		ret = -ENOMEM;
		pr_err("could not allocate memory\n");
		goto out_unreg;
	}

	/* TODO 1/2: mark pages as reserved */
	for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
		SetPageReserved(vmalloc_to_page(vmalloc_area+i));

	/* TODO 1/6: write data in each page */
	for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE) {
		vmalloc_area[i] = 0xaa;
		vmalloc_area[i + 1] = 0xbb;
		vmalloc_area[i + 2] = 0xcc;
		vmalloc_area[i + 3] = 0xdd;
	}

	cdev_init(&mmap_cdev, &mmap_fops);
	ret = cdev_add(&mmap_cdev, MKDEV(MY_MAJOR, 0), 1);
	if (ret < 0) {
		pr_err("could not add device\n");
		goto out_vfree;
	}

	return 0;

out_vfree:
	vfree(vmalloc_area);
out_unreg:
	unregister_chrdev_region(MKDEV(MY_MAJOR, 0), 1);
out_no_chrdev:
	remove_proc_entry(PROC_ENTRY_NAME, NULL);
out:
	return ret;
}

static void __exit my_exit(void)
{
	int i;

	cdev_del(&mmap_cdev);

	/* TODO 1/3: clear reservation on pages and free mem.*/
	for (i = 0; i < NPAGES * PAGE_SIZE; i += PAGE_SIZE)
		ClearPageReserved(vmalloc_to_page(vmalloc_area+i));
	vfree(vmalloc_area);

	unregister_chrdev_region(MKDEV(MY_MAJOR, 0), 1);
	/* TODO 3: remove proc entry */
	remove_proc_entry(PROC_ENTRY_NAME, NULL);
}

module_init(my_init);
module_exit(my_exit);
