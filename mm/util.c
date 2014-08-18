#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/mman.h>
#include <linux/hugetlb.h>
#include <linux/vmalloc.h>

#include <asm/uaccess.h>

#include "internal.h"

/**
 * kstrdup - allocate space for and copy an existing string
 * @s: the string to duplicate
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 */
char *kstrdup(const char *s, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strlen(s) + 1;
	buf = kmalloc_track_caller(len, gfp);
	if (buf)
		memcpy(buf, s, len);
	return buf;
}
EXPORT_SYMBOL(kstrdup);

/**
 * kstrndup - allocate space for and copy an existing string
 * @s: the string to duplicate
 * @max: read at most @max chars from @s
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 */
char *kstrndup(const char *s, size_t max, gfp_t gfp)
{
	size_t len;
	char *buf;

	if (!s)
		return NULL;

	len = strnlen(s, max);
	buf = kmalloc_track_caller(len+1, gfp);
	if (buf) {
		memcpy(buf, s, len);
		buf[len] = '\0';
	}
	return buf;
}
EXPORT_SYMBOL(kstrndup);

/**
 * kmemdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 * @gfp: GFP mask to use
 */
void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = kmalloc_track_caller(len, gfp);
	if (p)
		memcpy(p, src, len);
	return p;
}
EXPORT_SYMBOL(kmemdup);

/**
 * memdup_user - duplicate memory region from user space
 *
 * @src: source address in user space
 * @len: number of bytes to copy
 *
 * Returns an ERR_PTR() on failure.
 */
void *memdup_user(const void __user *src, size_t len)
{
	void *p;

	/*
	 * Always use GFP_KERNEL, since copy_from_user() can sleep and
	 * cause pagefault, which makes it pointless to use GFP_NOFS
	 * or GFP_ATOMIC.
	 */
	p = kmalloc_track_caller(len, GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(p, src, len)) {
		kfree(p);
		return ERR_PTR(-EFAULT);
	}

	return p;
}
EXPORT_SYMBOL(memdup_user);

/*
 * strndup_user - duplicate an existing string from user space
 * @s: The string to duplicate
 * @n: Maximum number of bytes to copy, including the trailing NUL.
 */
char *strndup_user(const char __user *s, long n)
{
	char *p;
	long length;

	length = strnlen_user(s, n);

	if (!length)
		return ERR_PTR(-EFAULT);

	if (length > n)
		return ERR_PTR(-EINVAL);

	p = memdup_user(s, length);

	if (IS_ERR(p))
		return p;

	p[length - 1] = '\0';

	return p;
}
EXPORT_SYMBOL(strndup_user);

void __vma_link_list(struct mm_struct *mm, struct vm_area_struct *vma,
		struct vm_area_struct *prev, struct rb_node *rb_parent)
{
	struct vm_area_struct *next;

	vma->vm_prev = prev;
	if (prev) {
		next = prev->vm_next;
		prev->vm_next = vma;
	} else {
		mm->mmap = vma;
		if (rb_parent)
			next = rb_entry(rb_parent,
					struct vm_area_struct, vm_rb);
		else
			next = NULL;
	}
	vma->vm_next = next;
	if (next)
		next->vm_prev = vma;
}

/* Check if the vma is being used as a stack by this task */
static int vm_is_stack_for_task(struct task_struct *t,
				struct vm_area_struct *vma)
{
	return (vma->vm_start <= KSTK_ESP(t) && vma->vm_end >= KSTK_ESP(t));
}

/*
 * Check if the vma is being used as a stack.
 * If is_group is non-zero, check in the entire thread group or else
 * just check in the current task. Returns the pid of the task that
 * the vma is stack for.
 */
pid_t vm_is_stack(struct task_struct *task,
		  struct vm_area_struct *vma, int in_group)
{
	pid_t ret = 0;

	if (vm_is_stack_for_task(task, vma))
		return task->pid;

	if (in_group) {
		struct task_struct *t;

		rcu_read_lock();
		for_each_thread(task, t) {
			if (vm_is_stack_for_task(t, vma)) {
				ret = t->pid;
				goto done;
			}
		}
done:
		rcu_read_unlock();
	}

	return ret;
}

#if defined(CONFIG_MMU) && !defined(HAVE_ARCH_PICK_MMAP_LAYOUT)
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	mm->mmap_base = TASK_UNMAPPED_BASE;
	mm->get_unmapped_area = arch_get_unmapped_area;
}
#endif

/*
 * Like get_user_pages_fast() except its IRQ-safe in that it won't fall
 * back to the regular GUP.
 * If the architecture not support this function, simply return with no
 * page pinned
 */
int __weak __get_user_pages_fast(unsigned long start,
				 int nr_pages, int write, struct page **pages)
{
	return 0;
}
EXPORT_SYMBOL_GPL(__get_user_pages_fast);

/**
 * get_user_pages_fast() - pin user pages in memory
 * @start:	starting user address
 * @nr_pages:	number of pages from start to pin
 * @write:	whether pages will be written to
 * @pages:	array that receives pointers to the pages pinned.
 *		Should be at least nr_pages long.
 *
 * Returns number of pages pinned. This may be fewer than the number
 * requested. If nr_pages is 0 or negative, returns 0. If no pages
 * were pinned, returns -errno.
 *
 * get_user_pages_fast provides equivalent functionality to get_user_pages,
 * operating on current and current->mm, with force=0 and vma=NULL. However
 * unlike get_user_pages, it must be called without mmap_sem held.
 *
 * get_user_pages_fast may take mmap_sem and page table locks, so no
 * assumptions can be made about lack of locking. get_user_pages_fast is to be
 * implemented in a way that is advantageous (vs get_user_pages()) when the
 * user memory area is already faulted in and present in ptes. However if the
 * pages have to be faulted in, it may turn out to be slightly slower so
 * callers need to carefully consider what to use. On many architectures,
 * get_user_pages_fast simply falls back to get_user_pages.
 */
int __weak get_user_pages_fast(unsigned long start,
				int nr_pages, int write, struct page **pages)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_read(&mm->mmap_sem);
	ret = get_user_pages(current, mm, start, nr_pages,
					write, 0, pages, NULL);
	up_read(&mm->mmap_sem);

	return ret;
}
EXPORT_SYMBOL_GPL(get_user_pages_fast);

unsigned long vm_mmap_pgoff(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long pgoff)
{
	unsigned long ret;
	struct mm_struct *mm = current->mm;
	unsigned long populate;

	ret = security_mmap_file(file, prot, flag);
	if (!ret) {
		down_write(&mm->mmap_sem);
		ret = do_mmap_pgoff(file, addr, len, prot, flag, pgoff,
				    &populate);
		up_write(&mm->mmap_sem);
		if (populate)
			mm_populate(ret, populate);
	}
	return ret;
}

unsigned long vm_mmap(struct file *file, unsigned long addr,
	unsigned long len, unsigned long prot,
	unsigned long flag, unsigned long offset)
{
	if (unlikely(offset + PAGE_ALIGN(len) < offset))
		return -EINVAL;
	if (unlikely(offset & ~PAGE_MASK))
		return -EINVAL;

	return vm_mmap_pgoff(file, addr, len, prot, flag, offset >> PAGE_SHIFT);
}
EXPORT_SYMBOL(vm_mmap);

void kvfree(const void *addr)
{
	if (is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}
EXPORT_SYMBOL(kvfree);

struct address_space *page_mapping(struct page *page)
{
	struct address_space *mapping = page->mapping;

	/* This happens if someone calls flush_dcache_page on slab page */
	if (unlikely(PageSlab(page)))
		return NULL;

	if (unlikely(PageSwapCache(page))) {
		swp_entry_t entry;

		entry.val = page_private(page);
		mapping = swap_address_space(entry);
	} else if ((unsigned long)mapping & PAGE_MAPPING_ANON)
		mapping = NULL;
	return mapping;
}

int overcommit_ratio_handler(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp,
			     loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		sysctl_overcommit_kbytes = 0;
	return ret;
}

int overcommit_kbytes_handler(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp,
			     loff_t *ppos)
{
	int ret;

	ret = proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
	if (ret == 0 && write)
		sysctl_overcommit_ratio = 0;
	return ret;
}

/*
 * Committed memory limit enforced when OVERCOMMIT_NEVER policy is used
 */
unsigned long vm_commit_limit(void)
{
	unsigned long allowed;

	if (sysctl_overcommit_kbytes)
		allowed = sysctl_overcommit_kbytes >> (PAGE_SHIFT - 10);
	else
		allowed = ((totalram_pages - hugetlb_total_pages())
			   * sysctl_overcommit_ratio / 100);
	allowed += total_swap_pages;

	return allowed;
}

/**
 * get_cmdline() - copy the cmdline value to a buffer.
 * @task:     the task whose cmdline value to copy.
 * @buffer:   the buffer to copy to.
 * @buflen:   the length of the buffer. Larger cmdline values are truncated
 *            to this length.
 * Returns the size of the cmdline field copied. Note that the copy does
 * not guarantee an ending NULL byte.
 */
int get_cmdline(struct task_struct *task, char *buffer, int buflen)
{
	int res = 0;
	unsigned int len;
	struct mm_struct *mm = get_task_mm(task);
	if (!mm)
		goto out;
	if (!mm->arg_end)
		goto out_mm;	/* Shh! No looking before we're done */

	len = mm->arg_end - mm->arg_start;

	if (len > buflen)
		len = buflen;

	res = access_process_vm(task, mm->arg_start, buffer, len, 0);

	/*
	 * If the nul at the end of args has been overwritten, then
	 * assume application is using setproctitle(3).
	 */
	if (res > 0 && buffer[res-1] != '\0' && len < buflen) {
		len = strnlen(buffer, res);
		if (len < res) {
			res = len;
		} else {
			len = mm->env_end - mm->env_start;
			if (len > buflen - res)
				len = buflen - res;
			res += access_process_vm(task, mm->env_start,
						 buffer+res, len, 0);
			res = strnlen(buffer, res);
		}
	}
out_mm:
	mmput(mm);
out:
	return res;
}
