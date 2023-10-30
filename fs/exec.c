// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */
/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 *
 * Demand loading changed July 1993 by Eric Youngdale.   Use mmap instead,
 * current->executable is only used by the procfs.  This allows a dispatch
 * table to check for several different types  of binary formats.  We keep
 * trying until we recognize the file or we run out of supported binary
 * formats.
 */

#include <linux/kernel_read_file.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/swap.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/sched/signal.h>
#include <linux/sched/numa_balancing.h>
#include <linux/sched/task.h>
#include <linux/pagemap.h>
#include <linux/perf_event.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/key.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/tsacct_kern.h>
#include <linux/cn_proc.h>
#include <linux/audit.h>
#include <linux/kmod.h>
#include <linux/fsnotify.h>
#include <linux/fs_struct.h>
#include <linux/oom.h>
#include <linux/compat.h>
#include <linux/vmalloc.h>
#include <linux/io_uring.h>
#include <linux/syscall_user_dispatch.h>
#include <linux/coredump.h>
#include <linux/time_namespace.h>
#include <linux/user_events.h>

#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

#include <trace/events/task.h>
#include "internal.h"

#include <trace/events/sched.h>

static int bprm_creds_from_file(struct linux_binprm *bprm);

int suid_dumpable = 0;

static LIST_HEAD(formats);
static DEFINE_RWLOCK(binfmt_lock);

void __register_binfmt(struct linux_binfmt * fmt, int insert)
{
	write_lock(&binfmt_lock);
	insert ? list_add(&fmt->lh, &formats) :
		 list_add_tail(&fmt->lh, &formats);
	write_unlock(&binfmt_lock);
}

EXPORT_SYMBOL(__register_binfmt);

void unregister_binfmt(struct linux_binfmt * fmt)
{
	write_lock(&binfmt_lock);
	list_del(&fmt->lh);
	write_unlock(&binfmt_lock);
}

EXPORT_SYMBOL(unregister_binfmt);

static inline void put_binfmt(struct linux_binfmt * fmt)
{
	module_put(fmt->module);
}

bool path_noexec(const struct path *path)
{
	return (path->mnt->mnt_flags & MNT_NOEXEC) ||
	       (path->mnt->mnt_sb->s_iflags & SB_I_NOEXEC);
}

#ifdef CONFIG_USELIB
/*
 * Note that a shared library must be both readable and executable due to
 * security reasons.
 *
 * Also note that we take the address to load from the file itself.
 */
SYSCALL_DEFINE1(uselib, const char __user *, library)
{
	struct linux_binfmt *fmt;
	struct file *file;
	struct filename *tmp = getname(library);
	int error = PTR_ERR(tmp);
	static const struct open_flags uselib_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_READ | MAY_EXEC,
		.intent = LOOKUP_OPEN,
		.lookup_flags = LOOKUP_FOLLOW,
	};

	if (IS_ERR(tmp))
		goto out;

	file = do_filp_open(AT_FDCWD, tmp, &uselib_flags);
	putname(tmp);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	/*
	 * may_open() has already checked for this, so it should be
	 * impossible to trip now. But we need to be extra cautious
	 * and check again at the very end too.
	 */
	error = -EACCES;
	if (WARN_ON_ONCE(!S_ISREG(file_inode(file)->i_mode) ||
			 path_noexec(&file->f_path)))
		goto exit;

	error = -ENOEXEC;

	read_lock(&binfmt_lock);
	list_for_each_entry(fmt, &formats, lh) {
		if (!fmt->load_shlib)
			continue;
		if (!try_module_get(fmt->module))
			continue;
		read_unlock(&binfmt_lock);
		error = fmt->load_shlib(file);
		read_lock(&binfmt_lock);
		put_binfmt(fmt);
		if (error != -ENOEXEC)
			break;
	}
	read_unlock(&binfmt_lock);
exit:
	fput(file);
out:
	return error;
}
#endif /* #ifdef CONFIG_USELIB */

#ifdef CONFIG_MMU
/*
 * The nascent bprm->mm is not visible until exec_mmap() but it can
 * use a lot of memory, account these pages in current->mm temporary
 * for oom_badness()->get_mm_rss(). Once exec succeeds or fails, we
 * change the counter back via acct_arg_size(0).
 */
static void acct_arg_size(struct linux_binprm *bprm, unsigned long pages)
{
	struct mm_struct *mm = current->mm;
	long diff = (long)(pages - bprm->vma_pages);

	if (!mm || !diff)
		return;

	bprm->vma_pages = pages;
	add_mm_counter(mm, MM_ANONPAGES, diff);
}

static struct page *get_arg_page(struct linux_binprm *bprm, unsigned long pos,
		int write)
{
	struct page *page;
	struct vm_area_struct *vma = bprm->vma;
	struct mm_struct *mm = bprm->mm;
	int ret;

	/*
	 * Avoid relying on expanding the stack down in GUP (which
	 * does not work for STACK_GROWSUP anyway), and just do it
	 * by hand ahead of time.
	 */
	if (write && pos < vma->vm_start) {
		mmap_write_lock(mm);
		ret = expand_downwards(vma, pos);
		if (unlikely(ret < 0)) {
			mmap_write_unlock(mm);
			return NULL;
		}
		mmap_write_downgrade(mm);
	} else
		mmap_read_lock(mm);

	/*
	 * We are doing an exec().  'current' is the process
	 * doing the exec and 'mm' is the new process's mm.
	 */
	ret = get_user_pages_remote(mm, pos, 1,
			write ? FOLL_WRITE : 0,
			&page, NULL);
	mmap_read_unlock(mm);
	if (ret <= 0)
		return NULL;

	if (write)
		acct_arg_size(bprm, vma_pages(vma));

	return page;
}

static void put_arg_page(struct page *page)
{
	put_page(page);
}

static void free_arg_pages(struct linux_binprm *bprm)
{
}

static void flush_arg_page(struct linux_binprm *bprm, unsigned long pos,
		struct page *page)
{
	flush_cache_page(bprm->vma, pos, page_to_pfn(page));
}

static int __bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = bprm->mm;

	bprm->vma = vma = vm_area_alloc(mm);
	if (!vma)
		return -ENOMEM;
	vma_set_anonymous(vma);

	if (mmap_write_lock_killable(mm)) {
		err = -EINTR;
		goto err_free;
	}

	/*
	 * Place the stack at the largest stack address the architecture
	 * supports. Later, we'll move this to an appropriate place. We don't
	 * use STACK_TOP because that can depend on attributes which aren't
	 * configured yet.
	 */
	BUILD_BUG_ON(VM_STACK_FLAGS & VM_STACK_INCOMPLETE_SETUP);
	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;
	vm_flags_init(vma, VM_SOFTDIRTY | VM_STACK_FLAGS | VM_STACK_INCOMPLETE_SETUP);
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	err = insert_vm_struct(mm, vma);
	if (err)
		goto err;

	mm->stack_vm = mm->total_vm = 1;
	mmap_write_unlock(mm);
	bprm->p = vma->vm_end - sizeof(void *);
	return 0;
err:
	mmap_write_unlock(mm);
err_free:
	bprm->vma = NULL;
	vm_area_free(vma);
	return err;
}

static bool valid_arg_len(struct linux_binprm *bprm, long len)
{
	return len <= MAX_ARG_STRLEN;
}

#else

static inline void acct_arg_size(struct linux_binprm *bprm, unsigned long pages)
{
}

static struct page *get_arg_page(struct linux_binprm *bprm, unsigned long pos,
		int write)
{
	struct page *page;

	page = bprm->page[pos / PAGE_SIZE];
	if (!page && write) {
		page = alloc_page(GFP_HIGHUSER|__GFP_ZERO);
		if (!page)
			return NULL;
		bprm->page[pos / PAGE_SIZE] = page;
	}

	return page;
}

static void put_arg_page(struct page *page)
{
}

static void free_arg_page(struct linux_binprm *bprm, int i)
{
	if (bprm->page[i]) {
		__free_page(bprm->page[i]);
		bprm->page[i] = NULL;
	}
}

static void free_arg_pages(struct linux_binprm *bprm)
{
	int i;

	for (i = 0; i < MAX_ARG_PAGES; i++)
		free_arg_page(bprm, i);
}

static void flush_arg_page(struct linux_binprm *bprm, unsigned long pos,
		struct page *page)
{
}

static int __bprm_mm_init(struct linux_binprm *bprm)
{
	bprm->p = PAGE_SIZE * MAX_ARG_PAGES - sizeof(void *);
	return 0;
}

static bool valid_arg_len(struct linux_binprm *bprm, long len)
{
	return len <= bprm->p;
}

#endif /* CONFIG_MMU */

/*
 * Create a new mm_struct and populate it with a temporary stack
 * vm_area_struct.  We don't have enough context at this point to set the stack
 * flags, permissions, and offset, so we use temporary values.  We'll update
 * them later in setup_arg_pages().
 */
static int bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct mm_struct *mm = NULL;

	bprm->mm = mm = mm_alloc();
	err = -ENOMEM;
	if (!mm)
		goto err;

	/* Save current stack limit for all calculations made during exec. */
	task_lock(current->group_leader);
	bprm->rlim_stack = current->signal->rlim[RLIMIT_STACK];
	task_unlock(current->group_leader);

	err = __bprm_mm_init(bprm);
	if (err)
		goto err;

	return 0;

err:
	if (mm) {
		bprm->mm = NULL;
		mmdrop(mm);
	}

	return err;
}

struct user_arg_ptr {
#ifdef CONFIG_COMPAT
	bool is_compat;
#endif
	union {
		const char __user *const __user *native;
#ifdef CONFIG_COMPAT
		const compat_uptr_t __user *compat;
#endif
	} ptr;
};

static const char __user *get_user_arg_ptr(struct user_arg_ptr argv, int nr)
{
	const char __user *native;

#ifdef CONFIG_COMPAT
	if (unlikely(argv.is_compat)) {
		compat_uptr_t compat;

		if (get_user(compat, argv.ptr.compat + nr))
			return ERR_PTR(-EFAULT);

		return compat_ptr(compat);
	}
#endif

	if (get_user(native, argv.ptr.native + nr))
		return ERR_PTR(-EFAULT);

	return native;
}

/*
 * count() counts the number of strings in array ARGV.
 */
static int count(struct user_arg_ptr argv, int max)
{
	int i = 0;

	if (argv.ptr.native != NULL) {
		for (;;) {
			const char __user *p = get_user_arg_ptr(argv, i);

			if (!p)
				break;

			if (IS_ERR(p))
				return -EFAULT;

			if (i >= max)
				return -E2BIG;
			++i;

			if (fatal_signal_pending(current))
				return -ERESTARTNOHAND;
			cond_resched();
		}
	}
	return i;
}

static int count_strings_kernel(const char *const *argv)
{
	int i;

	if (!argv)
		return 0;

	for (i = 0; argv[i]; ++i) {
		if (i >= MAX_ARG_STRINGS)
			return -E2BIG;
		if (fatal_signal_pending(current))
			return -ERESTARTNOHAND;
		cond_resched();
	}
	return i;
}

static int bprm_stack_limits(struct linux_binprm *bprm)
{
	unsigned long limit, ptr_size;

	/*
	 * Limit to 1/4 of the max stack size or 3/4 of _STK_LIM
	 * (whichever is smaller) for the argv+env strings.
	 * This ensures that:
	 *  - the remaining binfmt code will not run out of stack space,
	 *  - the program will have a reasonable amount of stack left
	 *    to work from.
	 */
	limit = _STK_LIM / 4 * 3;
	limit = min(limit, bprm->rlim_stack.rlim_cur / 4);
	/*
	 * We've historically supported up to 32 pages (ARG_MAX)
	 * of argument strings even with small stacks
	 */
	limit = max_t(unsigned long, limit, ARG_MAX);
	/*
	 * We must account for the size of all the argv and envp pointers to
	 * the argv and envp strings, since they will also take up space in
	 * the stack. They aren't stored until much later when we can't
	 * signal to the parent that the child has run out of stack space.
	 * Instead, calculate it here so it's possible to fail gracefully.
	 *
	 * In the case of argc = 0, make sure there is space for adding a
	 * empty string (which will bump argc to 1), to ensure confused
	 * userspace programs don't start processing from argv[1], thinking
	 * argc can never be 0, to keep them from walking envp by accident.
	 * See do_execveat_common().
	 */
	ptr_size = (max(bprm->argc, 1) + bprm->envc) * sizeof(void *);
	if (limit <= ptr_size)
		return -E2BIG;
	limit -= ptr_size;

	bprm->argmin = bprm->p - limit;
	return 0;
}

/*
 * 'copy_strings()' copies argument/environment strings from the old
 * processes's memory to the new process's stack.  The call to get_user_pages()
 * ensures the destination page is created and not swapped out.
 */
static int copy_strings(int argc, struct user_arg_ptr argv,
			struct linux_binprm *bprm)
{
	struct page *kmapped_page = NULL;
	char *kaddr = NULL;
	unsigned long kpos = 0;
	int ret;

	while (argc-- > 0) {
		const char __user *str;
		int len;
		unsigned long pos;

		ret = -EFAULT;
		str = get_user_arg_ptr(argv, argc);
		if (IS_ERR(str))
			goto out;

		len = strnlen_user(str, MAX_ARG_STRLEN);
		if (!len)
			goto out;

		ret = -E2BIG;
		if (!valid_arg_len(bprm, len))
			goto out;

		/* We're going to work our way backwards. */
		pos = bprm->p;
		str += len;
		bprm->p -= len;
#ifdef CONFIG_MMU
		if (bprm->p < bprm->argmin)
			goto out;
#endif

		while (len > 0) {
			int offset, bytes_to_copy;

			if (fatal_signal_pending(current)) {
				ret = -ERESTARTNOHAND;
				goto out;
			}
			cond_resched();

			offset = pos % PAGE_SIZE;
			if (offset == 0)
				offset = PAGE_SIZE;

			bytes_to_copy = offset;
			if (bytes_to_copy > len)
				bytes_to_copy = len;

			offset -= bytes_to_copy;
			pos -= bytes_to_copy;
			str -= bytes_to_copy;
			len -= bytes_to_copy;

			if (!kmapped_page || kpos != (pos & PAGE_MASK)) {
				struct page *page;

				page = get_arg_page(bprm, pos, 1);
				if (!page) {
					ret = -E2BIG;
					goto out;
				}

				if (kmapped_page) {
					flush_dcache_page(kmapped_page);
					kunmap_local(kaddr);
					put_arg_page(kmapped_page);
				}
				kmapped_page = page;
				kaddr = kmap_local_page(kmapped_page);
				kpos = pos & PAGE_MASK;
				flush_arg_page(bprm, kpos, kmapped_page);
			}
			if (copy_from_user(kaddr+offset, str, bytes_to_copy)) {
				ret = -EFAULT;
				goto out;
			}
		}
	}
	ret = 0;
out:
	if (kmapped_page) {
		flush_dcache_page(kmapped_page);
		kunmap_local(kaddr);
		put_arg_page(kmapped_page);
	}
	return ret;
}

/*
 * Copy and argument/environment string from the kernel to the processes stack.
 */
int copy_string_kernel(const char *arg, struct linux_binprm *bprm)
{
	int len = strnlen(arg, MAX_ARG_STRLEN) + 1 /* terminating NUL */;
	unsigned long pos = bprm->p;

	if (len == 0)
		return -EFAULT;
	if (!valid_arg_len(bprm, len))
		return -E2BIG;

	/* We're going to work our way backwards. */
	arg += len;
	bprm->p -= len;
	if (IS_ENABLED(CONFIG_MMU) && bprm->p < bprm->argmin)
		return -E2BIG;

	while (len > 0) {
		unsigned int bytes_to_copy = min_t(unsigned int, len,
				min_not_zero(offset_in_page(pos), PAGE_SIZE));
		struct page *page;

		pos -= bytes_to_copy;
		arg -= bytes_to_copy;
		len -= bytes_to_copy;

		page = get_arg_page(bprm, pos, 1);
		if (!page)
			return -E2BIG;
		flush_arg_page(bprm, pos & PAGE_MASK, page);
		memcpy_to_page(page, offset_in_page(pos), arg, bytes_to_copy);
		put_arg_page(page);
	}

	return 0;
}
EXPORT_SYMBOL(copy_string_kernel);

static int copy_strings_kernel(int argc, const char *const *argv,
			       struct linux_binprm *bprm)
{
	while (argc-- > 0) {
		int ret = copy_string_kernel(argv[argc], bprm);
		if (ret < 0)
			return ret;
		if (fatal_signal_pending(current))
			return -ERESTARTNOHAND;
		cond_resched();
	}
	return 0;
}

#ifdef CONFIG_MMU

/*
 * During bprm_mm_init(), we create a temporary stack at STACK_TOP_MAX.  Once
 * the binfmt code determines where the new stack should reside, we shift it to
 * its final location.  The process proceeds as follows:
 *
 * 1) Use shift to calculate the new vma endpoints.
 * 2) Extend vma to cover both the old and new ranges.  This ensures the
 *    arguments passed to subsequent functions are consistent.
 * 3) Move vma's page tables to the new range.
 * 4) Free up any cleared pgd range.
 * 5) Shrink the vma to cover only the new range.
 */
static int shift_arg_pages(struct vm_area_struct *vma, unsigned long shift)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long old_start = vma->vm_start;
	unsigned long old_end = vma->vm_end;
	unsigned long length = old_end - old_start;
	unsigned long new_start = old_start - shift;
	unsigned long new_end = old_end - shift;
	VMA_ITERATOR(vmi, mm, new_start);
	struct vm_area_struct *next;
	struct mmu_gather tlb;

	BUG_ON(new_start > new_end);

	/*
	 * ensure there are no vmas between where we want to go
	 * and where we are
	 */
	if (vma != vma_next(&vmi))
		return -EFAULT;

	vma_iter_prev_range(&vmi);
	/*
	 * cover the whole range: [new_start, old_end)
	 */
	if (vma_expand(&vmi, vma, new_start, old_end, vma->vm_pgoff, NULL))
		return -ENOMEM;

	/*
	 * move the page tables downwards, on failure we rely on
	 * process cleanup to remove whatever mess we made.
	 */
	if (length != move_page_tables(vma, old_start,
				       vma, new_start, length, false, true))
		return -ENOMEM;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm);
	next = vma_next(&vmi);
	if (new_end > old_start) {
		/*
		 * when the old and new regions overlap clear from new_end.
		 */
		free_pgd_range(&tlb, new_end, old_end, new_end,
			next ? next->vm_start : USER_PGTABLES_CEILING);
	} else {
		/*
		 * otherwise, clean from old_start; this is done to not touch
		 * the address space in [new_end, old_start) some architectures
		 * have constraints on va-space that make this illegal (IA64) -
		 * for the others its just a little faster.
		 */
		free_pgd_range(&tlb, old_start, old_end, new_end,
			next ? next->vm_start : USER_PGTABLES_CEILING);
	}
	tlb_finish_mmu(&tlb);

	vma_prev(&vmi);
	/* Shrink the vma to just the new range */
	return vma_shrink(&vmi, vma, new_start, new_end, vma->vm_pgoff);
}

/*
 * Finalizes the stack vm_area_struct. The flags and permissions are updated,
 * the stack is optionally relocated, and some extra space is added.
 */
int setup_arg_pages(struct linux_binprm *bprm,
		    unsigned long stack_top,
		    int executable_stack)
{
	unsigned long ret;
	unsigned long stack_shift;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = bprm->vma;
	struct vm_area_struct *prev = NULL;
	unsigned long vm_flags;
	unsigned long stack_base;
	unsigned long stack_size;
	unsigned long stack_expand;
	unsigned long rlim_stack;
	struct mmu_gather tlb;
	struct vma_iterator vmi;

#ifdef CONFIG_STACK_GROWSUP
	/* Limit stack size */
	stack_base = bprm->rlim_stack.rlim_max;

	stack_base = calc_max_stack_size(stack_base);

	/* Add space for stack randomization. */
	stack_base += (STACK_RND_MASK << PAGE_SHIFT);

	/* Make sure we didn't let the argument array grow too large. */
	if (vma->vm_end - vma->vm_start > stack_base)
		return -ENOMEM;

	stack_base = PAGE_ALIGN(stack_top - stack_base);

	stack_shift = vma->vm_start - stack_base;
	mm->arg_start = bprm->p - stack_shift;
	bprm->p = vma->vm_end - stack_shift;
#else
	stack_top = arch_align_stack(stack_top);
	stack_top = PAGE_ALIGN(stack_top);

	if (unlikely(stack_top < mmap_min_addr) ||
	    unlikely(vma->vm_end - vma->vm_start >= stack_top - mmap_min_addr))
		return -ENOMEM;

	stack_shift = vma->vm_end - stack_top;

	bprm->p -= stack_shift;
	mm->arg_start = bprm->p;
#endif

	if (bprm->loader)
		bprm->loader -= stack_shift;
	bprm->exec -= stack_shift;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	vm_flags = VM_STACK_FLAGS;

	/*
	 * Adjust stack execute permissions; explicitly enable for
	 * EXSTACK_ENABLE_X, disable for EXSTACK_DISABLE_X and leave alone
	 * (arch default) otherwise.
	 */
	if (unlikely(executable_stack == EXSTACK_ENABLE_X))
		vm_flags |= VM_EXEC;
	else if (executable_stack == EXSTACK_DISABLE_X)
		vm_flags &= ~VM_EXEC;
	vm_flags |= mm->def_flags;
	vm_flags |= VM_STACK_INCOMPLETE_SETUP;

	vma_iter_init(&vmi, mm, vma->vm_start);

	tlb_gather_mmu(&tlb, mm);
	ret = mprotect_fixup(&vmi, &tlb, vma, &prev, vma->vm_start, vma->vm_end,
			vm_flags);
	tlb_finish_mmu(&tlb);

	if (ret)
		goto out_unlock;
	BUG_ON(prev != vma);

	if (unlikely(vm_flags & VM_EXEC)) {
		pr_warn_once("process '%pD4' started with executable stack\n",
			     bprm->file);
	}

	/* Move stack pages down in memory. */
	if (stack_shift) {
		ret = shift_arg_pages(vma, stack_shift);
		if (ret)
			goto out_unlock;
	}

	/* mprotect_fixup is overkill to remove the temporary stack flags */
	vm_flags_clear(vma, VM_STACK_INCOMPLETE_SETUP);

	stack_expand = 131072UL; /* randomly 32*4k (or 2*64k) pages */
	stack_size = vma->vm_end - vma->vm_start;
	/*
	 * Align this down to a page boundary as expand_stack
	 * will align it up.
	 */
	rlim_stack = bprm->rlim_stack.rlim_cur & PAGE_MASK;

	stack_expand = min(rlim_stack, stack_size + stack_expand);

#ifdef CONFIG_STACK_GROWSUP
	stack_base = vma->vm_start + stack_expand;
#else
	stack_base = vma->vm_end - stack_expand;
#endif
	current->mm->start_stack = bprm->p;
	ret = expand_stack_locked(vma, stack_base);
	if (ret)
		ret = -EFAULT;

out_unlock:
	mmap_write_unlock(mm);
	return ret;
}
EXPORT_SYMBOL(setup_arg_pages);

#else

/*
 * Transfer the program arguments and environment from the holding pages
 * onto the stack. The provided stack pointer is adjusted accordingly.
 */
int transfer_args_to_stack(struct linux_binprm *bprm,
			   unsigned long *sp_location)
{
	unsigned long index, stop, sp;
	int ret = 0;

	stop = bprm->p >> PAGE_SHIFT;
	sp = *sp_location;

	for (index = MAX_ARG_PAGES - 1; index >= stop; index--) {
		unsigned int offset = index == stop ? bprm->p & ~PAGE_MASK : 0;
		char *src = kmap_local_page(bprm->page[index]) + offset;
		sp -= PAGE_SIZE - offset;
		if (copy_to_user((void *) sp, src, PAGE_SIZE - offset) != 0)
			ret = -EFAULT;
		kunmap_local(src);
		if (ret)
			goto out;
	}

	*sp_location = sp;

out:
	return ret;
}
EXPORT_SYMBOL(transfer_args_to_stack);

#endif /* CONFIG_MMU */

static struct file *do_open_execat(int fd, struct filename *name, int flags)
{
	struct file *file;
	int err;
	struct open_flags open_exec_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_EXEC,
		.intent = LOOKUP_OPEN,
		.lookup_flags = LOOKUP_FOLLOW,
	};

	if ((flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		return ERR_PTR(-EINVAL);
	if (flags & AT_SYMLINK_NOFOLLOW)
		open_exec_flags.lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		open_exec_flags.lookup_flags |= LOOKUP_EMPTY;

	file = do_filp_open(fd, name, &open_exec_flags);
	if (IS_ERR(file))
		goto out;

	/*
	 * may_open() has already checked for this, so it should be
	 * impossible to trip now. But we need to be extra cautious
	 * and check again at the very end too.
	 */
	err = -EACCES;
	if (WARN_ON_ONCE(!S_ISREG(file_inode(file)->i_mode) ||
			 path_noexec(&file->f_path)))
		goto exit;

	err = deny_write_access(file);
	if (err)
		goto exit;

out:
	return file;

exit:
	fput(file);
	return ERR_PTR(err);
}

struct file *open_exec(const char *name)
{
	struct filename *filename = getname_kernel(name);
	struct file *f = ERR_CAST(filename);

	if (!IS_ERR(filename)) {
		f = do_open_execat(AT_FDCWD, filename, 0);
		putname(filename);
	}
	return f;
}
EXPORT_SYMBOL(open_exec);

#if defined(CONFIG_BINFMT_FLAT) || defined(CONFIG_BINFMT_ELF_FDPIC)
ssize_t read_code(struct file *file, unsigned long addr, loff_t pos, size_t len)
{
	ssize_t res = vfs_read(file, (void __user *)addr, len, &pos);
	if (res > 0)
		flush_icache_user_range(addr, addr + len);
	return res;
}
EXPORT_SYMBOL(read_code);
#endif

/*
 * Maps the mm_struct mm into the current task struct.
 * On success, this function returns with exec_update_lock
 * held for writing.
 */
static int exec_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk;
	struct mm_struct *old_mm, *active_mm;
	int ret;

	/* Notify parent that we're no longer interested in the old VM */
	tsk = current;
	old_mm = current->mm;
	exec_mm_release(tsk, old_mm);

	ret = down_write_killable(&tsk->signal->exec_update_lock);
	if (ret)
		return ret;

	if (old_mm) {
		/*
		 * If there is a pending fatal signal perhaps a signal
		 * whose default action is to create a coredump get
		 * out and die instead of going through with the exec.
		 */
		ret = mmap_read_lock_killable(old_mm);
		if (ret) {
			up_write(&tsk->signal->exec_update_lock);
			return ret;
		}
	}

	task_lock(tsk);
	membarrier_exec_mmap(mm);

	local_irq_disable();
	active_mm = tsk->active_mm;
	tsk->active_mm = mm;
	tsk->mm = mm;
	mm_init_cid(mm);
	/*
	 * This prevents preemption while active_mm is being loaded and
	 * it and mm are being updated, which could cause problems for
	 * lazy tlb mm refcounting when these are updated by context
	 * switches. Not all architectures can handle irqs off over
	 * activate_mm yet.
	 */
	if (!IS_ENABLED(CONFIG_ARCH_WANT_IRQS_OFF_ACTIVATE_MM))
		local_irq_enable();
	activate_mm(active_mm, mm);
	if (IS_ENABLED(CONFIG_ARCH_WANT_IRQS_OFF_ACTIVATE_MM))
		local_irq_enable();
	lru_gen_add_mm(mm);
	task_unlock(tsk);
	lru_gen_use_mm(mm);
	if (old_mm) {
		mmap_read_unlock(old_mm);
		BUG_ON(active_mm != old_mm);
		setmax_mm_hiwater_rss(&tsk->signal->maxrss, old_mm);
		mm_update_next_owner(old_mm);
		mmput(old_mm);
		return 0;
	}
	mmdrop_lazy_tlb(active_mm);
	return 0;
}

static int de_thread(struct task_struct *tsk)
{
	struct signal_struct *sig = tsk->signal;
	struct sighand_struct *oldsighand = tsk->sighand;
	spinlock_t *lock = &oldsighand->siglock;

	if (thread_group_empty(tsk))
		goto no_thread_group;

	/*
	 * Kill all other threads in the thread group.
	 */
	spin_lock_irq(lock);
	if ((sig->flags & SIGNAL_GROUP_EXIT) || sig->group_exec_task) {
		/*
		 * Another group action in progress, just
		 * return so that the signal is processed.
		 */
		spin_unlock_irq(lock);
		return -EAGAIN;
	}

	sig->group_exec_task = tsk;
	sig->notify_count = zap_other_threads(tsk);
	if (!thread_group_leader(tsk))
		sig->notify_count--;

	while (sig->notify_count) {
		__set_current_state(TASK_KILLABLE);
		spin_unlock_irq(lock);
		schedule();
		if (__fatal_signal_pending(tsk))
			goto killed;
		spin_lock_irq(lock);
	}
	spin_unlock_irq(lock);

	/*
	 * At this point all other threads have exited, all we have to
	 * do is to wait for the thread group leader to become inactive,
	 * and to assume its PID:
	 */
	if (!thread_group_leader(tsk)) {
		struct task_struct *leader = tsk->group_leader;

		for (;;) {
			cgroup_threadgroup_change_begin(tsk);
			write_lock_irq(&tasklist_lock);
			/*
			 * Do this under tasklist_lock to ensure that
			 * exit_notify() can't miss ->group_exec_task
			 */
			sig->notify_count = -1;
			if (likely(leader->exit_state))
				break;
			__set_current_state(TASK_KILLABLE);
			write_unlock_irq(&tasklist_lock);
			cgroup_threadgroup_change_end(tsk);
			schedule();
			if (__fatal_signal_pending(tsk))
				goto killed;
		}

		/*
		 * The only record we have of the real-time age of a
		 * process, regardless of execs it's done, is start_time.
		 * All the past CPU time is accumulated in signal_struct
		 * from sister threads now dead.  But in this non-leader
		 * exec, nothing survives from the original leader thread,
		 * whose birth marks the true age of this process now.
		 * When we take on its identity by switching to its PID, we
		 * also take its birthdate (always earlier than our own).
		 */
		tsk->start_time = leader->start_time;
		tsk->start_boottime = leader->start_boottime;

		BUG_ON(!same_thread_group(leader, tsk));
		/*
		 * An exec() starts a new thread group with the
		 * TGID of the previous thread group. Rehash the
		 * two threads with a switched PID, and release
		 * the former thread group leader:
		 */

		/* Become a process group leader with the old leader's pid.
		 * The old leader becomes a thread of the this thread group.
		 */
		exchange_tids(tsk, leader);
		transfer_pid(leader, tsk, PIDTYPE_TGID);
		transfer_pid(leader, tsk, PIDTYPE_PGID);
		transfer_pid(leader, tsk, PIDTYPE_SID);

		list_replace_rcu(&leader->tasks, &tsk->tasks);
		list_replace_init(&leader->sibling, &tsk->sibling);

		tsk->group_leader = tsk;
		leader->group_leader = tsk;

		tsk->exit_signal = SIGCHLD;
		leader->exit_signal = -1;

		BUG_ON(leader->exit_state != EXIT_ZOMBIE);
		leader->exit_state = EXIT_DEAD;

		/*
		 * We are going to release_task()->ptrace_unlink() silently,
		 * the tracer can sleep in do_wait(). EXIT_DEAD guarantees
		 * the tracer won't block again waiting for this thread.
		 */
		if (unlikely(leader->ptrace))
			__wake_up_parent(leader, leader->parent);
		write_unlock_irq(&tasklist_lock);
		cgroup_threadgroup_change_end(tsk);

		release_task(leader);
	}

	sig->group_exec_task = NULL;
	sig->notify_count = 0;

no_thread_group:
	/* we have changed execution domain */
	tsk->exit_signal = SIGCHLD;

	BUG_ON(!thread_group_leader(tsk));
	return 0;

killed:
	/* protects against exit_notify() and __exit_signal() */
	read_lock(&tasklist_lock);
	sig->group_exec_task = NULL;
	sig->notify_count = 0;
	read_unlock(&tasklist_lock);
	return -EAGAIN;
}


/*
 * This function makes sure the current process has its own signal table,
 * so that flush_signal_handlers can later reset the handlers without
 * disturbing other processes.  (Other processes might share the signal
 * table via the CLONE_SIGHAND option to clone().)
 */
static int unshare_sighand(struct task_struct *me)
{
	struct sighand_struct *oldsighand = me->sighand;

	if (refcount_read(&oldsighand->count) != 1) {
		struct sighand_struct *newsighand;
		/*
		 * This ->sighand is shared with the CLONE_SIGHAND
		 * but not CLONE_THREAD task, switch to the new one.
		 */
		newsighand = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
		if (!newsighand)
			return -ENOMEM;

		refcount_set(&newsighand->count, 1);

		write_lock_irq(&tasklist_lock);
		spin_lock(&oldsighand->siglock);
		memcpy(newsighand->action, oldsighand->action,
		       sizeof(newsighand->action));
		rcu_assign_pointer(me->sighand, newsighand);
		spin_unlock(&oldsighand->siglock);
		write_unlock_irq(&tasklist_lock);

		__cleanup_sighand(oldsighand);
	}
	return 0;
}

char *__get_task_comm(char *buf, size_t buf_size, struct task_struct *tsk)
{
	task_lock(tsk);
	/* Always NUL terminated and zero-padded */
	strscpy_pad(buf, tsk->comm, buf_size);
	task_unlock(tsk);
	return buf;
}
EXPORT_SYMBOL_GPL(__get_task_comm);

/*
 * These functions flushes out all traces of the currently running executable
 * so that a new one can be started
 */

void __set_task_comm(struct task_struct *tsk, const char *buf, bool exec)
{
	task_lock(tsk);
	trace_task_rename(tsk, buf);
	strscpy_pad(tsk->comm, buf, sizeof(tsk->comm));
	task_unlock(tsk);
	perf_event_comm(tsk, exec);
}

/*
 * Calling this is the point of no return. None of the failures will be
 * seen by userspace since either the process is already taking a fatal
 * signal (via de_thread() or coredump), or will have SEGV raised
 * (after exec_mmap()) by search_binary_handler (see below).
 */
int begin_new_exec(struct linux_binprm * bprm)
{
	struct task_struct *me = current;
	int retval;

	/* Once we are committed compute the creds */
	retval = bprm_creds_from_file(bprm);
	if (retval)
		return retval;

	/*
	 * Ensure all future errors are fatal.
	 */
	bprm->point_of_no_return = true;

	/*
	 * Make this the only thread in the thread group.
	 */
	retval = de_thread(me);
	if (retval)
		goto out;

	/*
	 * Cancel any io_uring activity across execve
	 */
	io_uring_task_cancel();

	/* Ensure the files table is not shared. */
	retval = unshare_files();
	if (retval)
		goto out;

	/*
	 * Must be called _before_ exec_mmap() as bprm->mm is
	 * not visible until then. Doing it here also ensures
	 * we don't race against replace_mm_exe_file().
	 */
	retval = set_mm_exe_file(bprm->mm, bprm->file);
	if (retval)
		goto out;

	/* If the binary is not readable then enforce mm->dumpable=0 */
	would_dump(bprm, bprm->file);
	if (bprm->have_execfd)
		would_dump(bprm, bprm->executable);

	/*
	 * Release all of the old mmap stuff
	 */
	acct_arg_size(bprm, 0);
	retval = exec_mmap(bprm->mm);
	if (retval)
		goto out;

	bprm->mm = NULL;

	retval = exec_task_namespaces();
	if (retval)
		goto out_unlock;

#ifdef CONFIG_POSIX_TIMERS
	spin_lock_irq(&me->sighand->siglock);
	posix_cpu_timers_exit(me);
	spin_unlock_irq(&me->sighand->siglock);
	exit_itimers(me);
	flush_itimer_signals();
#endif

	/*
	 * Make the signal table private.
	 */
	retval = unshare_sighand(me);
	if (retval)
		goto out_unlock;

	me->flags &= ~(PF_RANDOMIZE | PF_FORKNOEXEC |
					PF_NOFREEZE | PF_NO_SETAFFINITY);
	flush_thread();
	me->personality &= ~bprm->per_clear;

	clear_syscall_work_syscall_user_dispatch(me);

	/*
	 * We have to apply CLOEXEC before we change whether the process is
	 * dumpable (in setup_new_exec) to avoid a race with a process in userspace
	 * trying to access the should-be-closed file descriptors of a process
	 * undergoing exec(2).
	 */
	do_close_on_exec(me->files);

	if (bprm->secureexec) {
		/* Make sure parent cannot signal privileged process. */
		me->pdeath_signal = 0;

		/*
		 * For secureexec, reset the stack limit to sane default to
		 * avoid bad behavior from the prior rlimits. This has to
		 * happen before arch_pick_mmap_layout(), which examines
		 * RLIMIT_STACK, but after the point of no return to avoid
		 * needing to clean up the change on failure.
		 */
		if (bprm->rlim_stack.rlim_cur > _STK_LIM)
			bprm->rlim_stack.rlim_cur = _STK_LIM;
	}

	me->sas_ss_sp = me->sas_ss_size = 0;

	/*
	 * Figure out dumpability. Note that this checking only of current
	 * is wrong, but userspace depends on it. This should be testing
	 * bprm->secureexec instead.
	 */
	if (bprm->interp_flags & BINPRM_FLAGS_ENFORCE_NONDUMP ||
	    !(uid_eq(current_euid(), current_uid()) &&
	      gid_eq(current_egid(), current_gid())))
		set_dumpable(current->mm, suid_dumpable);
	else
		set_dumpable(current->mm, SUID_DUMP_USER);

	perf_event_exec();
	__set_task_comm(me, kbasename(bprm->filename), true);

	/* An exec changes our domain. We are no longer part of the thread
	   group */
	WRITE_ONCE(me->self_exec_id, me->self_exec_id + 1);
	flush_signal_handlers(me, 0);

	retval = set_cred_ucounts(bprm->cred);
	if (retval < 0)
		goto out_unlock;

	/*
	 * install the new credentials for this executable
	 */
	security_bprm_committing_creds(bprm);

	commit_creds(bprm->cred);
	bprm->cred = NULL;

	/*
	 * Disable monitoring for regular users
	 * when executing setuid binaries. Must
	 * wait until new credentials are committed
	 * by commit_creds() above
	 */
	if (get_dumpable(me->mm) != SUID_DUMP_USER)
		perf_event_exit_task(me);
	/*
	 * cred_guard_mutex must be held at least to this point to prevent
	 * ptrace_attach() from altering our determination of the task's
	 * credentials; any time after this it may be unlocked.
	 */
	security_bprm_committed_creds(bprm);

	/* Pass the opened binary to the interpreter. */
	if (bprm->have_execfd) {
		retval = get_unused_fd_flags(0);
		if (retval < 0)
			goto out_unlock;
		fd_install(retval, bprm->executable);
		bprm->executable = NULL;
		bprm->execfd = retval;
	}
	return 0;

out_unlock:
	up_write(&me->signal->exec_update_lock);
out:
	return retval;
}
EXPORT_SYMBOL(begin_new_exec);

void would_dump(struct linux_binprm *bprm, struct file *file)
{
	struct inode *inode = file_inode(file);
	struct mnt_idmap *idmap = file_mnt_idmap(file);
	if (inode_permission(idmap, inode, MAY_READ) < 0) {
		struct user_namespace *old, *user_ns;
		bprm->interp_flags |= BINPRM_FLAGS_ENFORCE_NONDUMP;

		/* Ensure mm->user_ns contains the executable */
		user_ns = old = bprm->mm->user_ns;
		while ((user_ns != &init_user_ns) &&
		       !privileged_wrt_inode_uidgid(user_ns, idmap, inode))
			user_ns = user_ns->parent;

		if (old != user_ns) {
			bprm->mm->user_ns = get_user_ns(user_ns);
			put_user_ns(old);
		}
	}
}
EXPORT_SYMBOL(would_dump);

void setup_new_exec(struct linux_binprm * bprm)
{
	/* Setup things that can depend upon the personality */
	struct task_struct *me = current;

	arch_pick_mmap_layout(me->mm, &bprm->rlim_stack);

	arch_setup_new_exec();

	/* Set the new mm task size. We have to do that late because it may
	 * depend on TIF_32BIT which is only updated in flush_thread() on
	 * some architectures like powerpc
	 */
	me->mm->task_size = TASK_SIZE;
	up_write(&me->signal->exec_update_lock);
	mutex_unlock(&me->signal->cred_guard_mutex);
}
EXPORT_SYMBOL(setup_new_exec);

/* Runs immediately before start_thread() takes over. */
void finalize_exec(struct linux_binprm *bprm)
{
	/* Store any stack rlimit changes before starting thread. */
	task_lock(current->group_leader);
	current->signal->rlim[RLIMIT_STACK] = bprm->rlim_stack;
	task_unlock(current->group_leader);
}
EXPORT_SYMBOL(finalize_exec);

/*
 * Prepare credentials and lock ->cred_guard_mutex.
 * setup_new_exec() commits the new creds and drops the lock.
 * Or, if exec fails before, free_bprm() should release ->cred
 * and unlock.
 */
static int prepare_bprm_creds(struct linux_binprm *bprm)
{
	if (mutex_lock_interruptible(&current->signal->cred_guard_mutex))
		return -ERESTARTNOINTR;

	bprm->cred = prepare_exec_creds();
	if (likely(bprm->cred))
		return 0;

	mutex_unlock(&current->signal->cred_guard_mutex);
	return -ENOMEM;
}

static void free_bprm(struct linux_binprm *bprm)
{
	if (bprm->mm) {
		acct_arg_size(bprm, 0);
		mmput(bprm->mm);
	}
	free_arg_pages(bprm);
	if (bprm->cred) {
		mutex_unlock(&current->signal->cred_guard_mutex);
		abort_creds(bprm->cred);
	}
	if (bprm->file) {
		allow_write_access(bprm->file);
		fput(bprm->file);
	}
	if (bprm->executable)
		fput(bprm->executable);
	/* If a binfmt changed the interp, free it. */
	if (bprm->interp != bprm->filename)
		kfree(bprm->interp);
	kfree(bprm->fdpath);
	kfree(bprm);
}

static struct linux_binprm *alloc_bprm(int fd, struct filename *filename)
{
	struct linux_binprm *bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	int retval = -ENOMEM;
	if (!bprm)
		goto out;

	if (fd == AT_FDCWD || filename->name[0] == '/') {
		bprm->filename = filename->name;
	} else {
		if (filename->name[0] == '\0')
			bprm->fdpath = kasprintf(GFP_KERNEL, "/dev/fd/%d", fd);
		else
			bprm->fdpath = kasprintf(GFP_KERNEL, "/dev/fd/%d/%s",
						  fd, filename->name);
		if (!bprm->fdpath)
			goto out_free;

		bprm->filename = bprm->fdpath;
	}
	bprm->interp = bprm->filename;

	retval = bprm_mm_init(bprm);
	if (retval)
		goto out_free;
	return bprm;

out_free:
	free_bprm(bprm);
out:
	return ERR_PTR(retval);
}

int bprm_change_interp(const char *interp, struct linux_binprm *bprm)
{
	/* If a binfmt changed the interp, free it first. */
	if (bprm->interp != bprm->filename)
		kfree(bprm->interp);
	bprm->interp = kstrdup(interp, GFP_KERNEL);
	if (!bprm->interp)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(bprm_change_interp);

/*
 * determine how safe it is to execute the proposed program
 * - the caller must hold ->cred_guard_mutex to protect against
 *   PTRACE_ATTACH or seccomp thread-sync
 */
static void check_unsafe_exec(struct linux_binprm *bprm)
{
	struct task_struct *p = current, *t;
	unsigned n_fs;

	if (p->ptrace)
		bprm->unsafe |= LSM_UNSAFE_PTRACE;

	/*
	 * This isn't strictly necessary, but it makes it harder for LSMs to
	 * mess up.
	 */
	if (task_no_new_privs(current))
		bprm->unsafe |= LSM_UNSAFE_NO_NEW_PRIVS;

	/*
	 * If another task is sharing our fs, we cannot safely
	 * suid exec because the differently privileged task
	 * will be able to manipulate the current directory, etc.
	 * It would be nice to force an unshare instead...
	 */
	n_fs = 1;
	spin_lock(&p->fs->lock);
	rcu_read_lock();
	for_other_threads(p, t) {
		if (t->fs == p->fs)
			n_fs++;
	}
	rcu_read_unlock();

	if (p->fs->users > n_fs)
		bprm->unsafe |= LSM_UNSAFE_SHARE;
	else
		p->fs->in_exec = 1;
	spin_unlock(&p->fs->lock);
}

static void bprm_fill_uid(struct linux_binprm *bprm, struct file *file)
{
	/* Handle suid and sgid on files */
	struct mnt_idmap *idmap;
	struct inode *inode = file_inode(file);
	unsigned int mode;
	vfsuid_t vfsuid;
	vfsgid_t vfsgid;

	if (!mnt_may_suid(file->f_path.mnt))
		return;

	if (task_no_new_privs(current))
		return;

	mode = READ_ONCE(inode->i_mode);
	if (!(mode & (S_ISUID|S_ISGID)))
		return;

	idmap = file_mnt_idmap(file);

	/* Be careful if suid/sgid is set */
	inode_lock(inode);

	/* reload atomically mode/uid/gid now that lock held */
	mode = inode->i_mode;
	vfsuid = i_uid_into_vfsuid(idmap, inode);
	vfsgid = i_gid_into_vfsgid(idmap, inode);
	inode_unlock(inode);

	/* We ignore suid/sgid if there are no mappings for them in the ns */
	if (!vfsuid_has_mapping(bprm->cred->user_ns, vfsuid) ||
	    !vfsgid_has_mapping(bprm->cred->user_ns, vfsgid))
		return;

	if (mode & S_ISUID) {
		bprm->per_clear |= PER_CLEAR_ON_SETID;
		bprm->cred->euid = vfsuid_into_kuid(vfsuid);
	}

	if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
		bprm->per_clear |= PER_CLEAR_ON_SETID;
		bprm->cred->egid = vfsgid_into_kgid(vfsgid);
	}
}

/*
 * Compute brpm->cred based upon the final binary.
 */
static int bprm_creds_from_file(struct linux_binprm *bprm)
{
	/* Compute creds based on which file? */
	struct file *file = bprm->execfd_creds ? bprm->executable : bprm->file;

	bprm_fill_uid(bprm, file);
	return security_bprm_creds_from_file(bprm, file);
}

/*
 * Fill the binprm structure from the inode.
 * Read the first BINPRM_BUF_SIZE bytes
 *
 * This may be called multiple times for binary chains (scripts for example).
 */
static int prepare_binprm(struct linux_binprm *bprm)
{
	loff_t pos = 0;

	memset(bprm->buf, 0, BINPRM_BUF_SIZE);
	return kernel_read(bprm->file, bprm->buf, BINPRM_BUF_SIZE, &pos);
}

/*
 * Arguments are '\0' separated strings found at the location bprm->p
 * points to; chop off the first by relocating brpm->p to right after
 * the first '\0' encountered.
 */
int remove_arg_zero(struct linux_binprm *bprm)
{
	int ret = 0;
	unsigned long offset;
	char *kaddr;
	struct page *page;

	if (!bprm->argc)
		return 0;

	do {
		offset = bprm->p & ~PAGE_MASK;
		page = get_arg_page(bprm, bprm->p, 0);
		if (!page) {
			ret = -EFAULT;
			goto out;
		}
		kaddr = kmap_local_page(page);

		for (; offset < PAGE_SIZE && kaddr[offset];
				offset++, bprm->p++)
			;

		kunmap_local(kaddr);
		put_arg_page(page);
	} while (offset == PAGE_SIZE);

	bprm->p++;
	bprm->argc--;
	ret = 0;

out:
	return ret;
}
EXPORT_SYMBOL(remove_arg_zero);

#define printable(c) (((c)=='\t') || ((c)=='\n') || (0x20<=(c) && (c)<=0x7e))
/*
 * cycle the list of binary formats handler, until one recognizes the image
 */
static int search_binary_handler(struct linux_binprm *bprm)
{
	bool need_retry = IS_ENABLED(CONFIG_MODULES);
	struct linux_binfmt *fmt;
	int retval;

	retval = prepare_binprm(bprm);
	if (retval < 0)
		return retval;

	retval = security_bprm_check(bprm);
	if (retval)
		return retval;

	retval = -ENOENT;
 retry:
	read_lock(&binfmt_lock);
	list_for_each_entry(fmt, &formats, lh) {
		if (!try_module_get(fmt->module))
			continue;
		read_unlock(&binfmt_lock);

		retval = fmt->load_binary(bprm);

		read_lock(&binfmt_lock);
		put_binfmt(fmt);
		if (bprm->point_of_no_return || (retval != -ENOEXEC)) {
			read_unlock(&binfmt_lock);
			return retval;
		}
	}
	read_unlock(&binfmt_lock);

	if (need_retry) {
		if (printable(bprm->buf[0]) && printable(bprm->buf[1]) &&
		    printable(bprm->buf[2]) && printable(bprm->buf[3]))
			return retval;
		if (request_module("binfmt-%04x", *(ushort *)(bprm->buf + 2)) < 0)
			return retval;
		need_retry = false;
		goto retry;
	}

	return retval;
}

/* binfmt handlers will call back into begin_new_exec() on success. */
static int exec_binprm(struct linux_binprm *bprm)
{
	pid_t old_pid, old_vpid;
	int ret, depth;

	/* Need to fetch pid before load_binary changes it */
	old_pid = current->pid;
	rcu_read_lock();
	old_vpid = task_pid_nr_ns(current, task_active_pid_ns(current->parent));
	rcu_read_unlock();

	/* This allows 4 levels of binfmt rewrites before failing hard. */
	for (depth = 0;; depth++) {
		struct file *exec;
		if (depth > 5)
			return -ELOOP;

		ret = search_binary_handler(bprm);
		if (ret < 0)
			return ret;
		if (!bprm->interpreter)
			break;

		exec = bprm->file;
		bprm->file = bprm->interpreter;
		bprm->interpreter = NULL;

		allow_write_access(exec);
		if (unlikely(bprm->have_execfd)) {
			if (bprm->executable) {
				fput(exec);
				return -ENOEXEC;
			}
			bprm->executable = exec;
		} else
			fput(exec);
	}

	audit_bprm(bprm);
	trace_sched_process_exec(current, old_pid, bprm);
	ptrace_event(PTRACE_EVENT_EXEC, old_vpid);
	proc_exec_connector(current);
	return 0;
}

/*
 * sys_execve() executes a new program.
 */
static int bprm_execve(struct linux_binprm *bprm,
		       int fd, struct filename *filename, int flags)
{
	struct file *file;
	int retval;

	retval = prepare_bprm_creds(bprm);
	if (retval)
		return retval;

	/*
	 * Check for unsafe execution states before exec_binprm(), which
	 * will call back into begin_new_exec(), into bprm_creds_from_file(),
	 * where setuid-ness is evaluated.
	 */
	check_unsafe_exec(bprm);
	current->in_execve = 1;
	sched_mm_cid_before_execve(current);

	file = do_open_execat(fd, filename, flags);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_unmark;

	sched_exec();

	bprm->file = file;
	/*
	 * Record that a name derived from an O_CLOEXEC fd will be
	 * inaccessible after exec.  This allows the code in exec to
	 * choose to fail when the executable is not mmaped into the
	 * interpreter and an open file descriptor is not passed to
	 * the interpreter.  This makes for a better user experience
	 * than having the interpreter start and then immediately fail
	 * when it finds the executable is inaccessible.
	 */
	if (bprm->fdpath && get_close_on_exec(fd))
		bprm->interp_flags |= BINPRM_FLAGS_PATH_INACCESSIBLE;

	/* Set the unchanging part of bprm->cred */
	retval = security_bprm_creds_for_exec(bprm);
	if (retval)
		goto out;

	retval = exec_binprm(bprm);
	if (retval < 0)
		goto out;

	sched_mm_cid_after_execve(current);
	/* execve succeeded */
	current->fs->in_exec = 0;
	current->in_execve = 0;
	rseq_execve(current);
	user_events_execve(current);
	acct_update_integrals(current);
	task_numa_free(current, false);
	return retval;

out:
	/*
	 * If past the point of no return ensure the code never
	 * returns to the userspace process.  Use an existing fatal
	 * signal if present otherwise terminate the process with
	 * SIGSEGV.
	 */
	if (bprm->point_of_no_return && !fatal_signal_pending(current))
		force_fatal_sig(SIGSEGV);

out_unmark:
	sched_mm_cid_after_execve(current);
	current->fs->in_exec = 0;
	current->in_execve = 0;

	return retval;
}

static int do_execveat_common(int fd, struct filename *filename,
			      struct user_arg_ptr argv,
			      struct user_arg_ptr envp,
			      int flags)
{
	struct linux_binprm *bprm;
	int retval;

	if (IS_ERR(filename))
		return PTR_ERR(filename);

	/*
	 * We move the actual failure in case of RLIMIT_NPROC excess from
	 * set*uid() to execve() because too many poorly written programs
	 * don't check setuid() return code.  Here we additionally recheck
	 * whether NPROC limit is still exceeded.
	 */
	if ((current->flags & PF_NPROC_EXCEEDED) &&
	    is_rlimit_overlimit(current_ucounts(), UCOUNT_RLIMIT_NPROC, rlimit(RLIMIT_NPROC))) {
		retval = -EAGAIN;
		goto out_ret;
	}

	/* We're below the limit (still or again), so we don't want to make
	 * further execve() calls fail. */
	current->flags &= ~PF_NPROC_EXCEEDED;

	bprm = alloc_bprm(fd, filename);
	if (IS_ERR(bprm)) {
		retval = PTR_ERR(bprm);
		goto out_ret;
	}

	retval = count(argv, MAX_ARG_STRINGS);
	if (retval == 0)
		pr_warn_once("process '%s' launched '%s' with NULL argv: empty string added\n",
			     current->comm, bprm->filename);
	if (retval < 0)
		goto out_free;
	bprm->argc = retval;

	retval = count(envp, MAX_ARG_STRINGS);
	if (retval < 0)
		goto out_free;
	bprm->envc = retval;

	retval = bprm_stack_limits(bprm);
	if (retval < 0)
		goto out_free;

	retval = copy_string_kernel(bprm->filename, bprm);
	if (retval < 0)
		goto out_free;
	bprm->exec = bprm->p;

	retval = copy_strings(bprm->envc, envp, bprm);
	if (retval < 0)
		goto out_free;

	retval = copy_strings(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out_free;

	/*
	 * When argv is empty, add an empty string ("") as argv[0] to
	 * ensure confused userspace programs that start processing
	 * from argv[1] won't end up walking envp. See also
	 * bprm_stack_limits().
	 */
	if (bprm->argc == 0) {
		retval = copy_string_kernel("", bprm);
		if (retval < 0)
			goto out_free;
		bprm->argc = 1;
	}

	retval = bprm_execve(bprm, fd, filename, flags);
out_free:
	free_bprm(bprm);

out_ret:
	putname(filename);
	return retval;
}

int kernel_execve(const char *kernel_filename,
		  const char *const *argv, const char *const *envp)
{
	struct filename *filename;
	struct linux_binprm *bprm;
	int fd = AT_FDCWD;
	int retval;

	/* It is non-sense for kernel threads to call execve */
	if (WARN_ON_ONCE(current->flags & PF_KTHREAD))
		return -EINVAL;

	filename = getname_kernel(kernel_filename);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	bprm = alloc_bprm(fd, filename);
	if (IS_ERR(bprm)) {
		retval = PTR_ERR(bprm);
		goto out_ret;
	}

	retval = count_strings_kernel(argv);
	if (WARN_ON_ONCE(retval == 0))
		retval = -EINVAL;
	if (retval < 0)
		goto out_free;
	bprm->argc = retval;

	retval = count_strings_kernel(envp);
	if (retval < 0)
		goto out_free;
	bprm->envc = retval;

	retval = bprm_stack_limits(bprm);
	if (retval < 0)
		goto out_free;

	retval = copy_string_kernel(bprm->filename, bprm);
	if (retval < 0)
		goto out_free;
	bprm->exec = bprm->p;

	retval = copy_strings_kernel(bprm->envc, envp, bprm);
	if (retval < 0)
		goto out_free;

	retval = copy_strings_kernel(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out_free;

	retval = bprm_execve(bprm, fd, filename, 0);
out_free:
	free_bprm(bprm);
out_ret:
	putname(filename);
	return retval;
}

static int do_execve(struct filename *filename,
	const char __user *const __user *__argv,
	const char __user *const __user *__envp)
{
	struct user_arg_ptr argv = { .ptr.native = __argv };
	struct user_arg_ptr envp = { .ptr.native = __envp };
	return do_execveat_common(AT_FDCWD, filename, argv, envp, 0);
}

static int do_execveat(int fd, struct filename *filename,
		const char __user *const __user *__argv,
		const char __user *const __user *__envp,
		int flags)
{
	struct user_arg_ptr argv = { .ptr.native = __argv };
	struct user_arg_ptr envp = { .ptr.native = __envp };

	return do_execveat_common(fd, filename, argv, envp, flags);
}

#ifdef CONFIG_COMPAT
static int compat_do_execve(struct filename *filename,
	const compat_uptr_t __user *__argv,
	const compat_uptr_t __user *__envp)
{
	struct user_arg_ptr argv = {
		.is_compat = true,
		.ptr.compat = __argv,
	};
	struct user_arg_ptr envp = {
		.is_compat = true,
		.ptr.compat = __envp,
	};
	return do_execveat_common(AT_FDCWD, filename, argv, envp, 0);
}

static int compat_do_execveat(int fd, struct filename *filename,
			      const compat_uptr_t __user *__argv,
			      const compat_uptr_t __user *__envp,
			      int flags)
{
	struct user_arg_ptr argv = {
		.is_compat = true,
		.ptr.compat = __argv,
	};
	struct user_arg_ptr envp = {
		.is_compat = true,
		.ptr.compat = __envp,
	};
	return do_execveat_common(fd, filename, argv, envp, flags);
}
#endif

void set_binfmt(struct linux_binfmt *new)
{
	struct mm_struct *mm = current->mm;

	if (mm->binfmt)
		module_put(mm->binfmt->module);

	mm->binfmt = new;
	if (new)
		__module_get(new->module);
}
EXPORT_SYMBOL(set_binfmt);

/*
 * set_dumpable stores three-value SUID_DUMP_* into mm->flags.
 */
void set_dumpable(struct mm_struct *mm, int value)
{
	if (WARN_ON((unsigned)value > SUID_DUMP_ROOT))
		return;

	set_mask_bits(&mm->flags, MMF_DUMPABLE_MASK, value);
}

SYSCALL_DEFINE3(execve,
		const char __user *, filename,
		const char __user *const __user *, argv,
		const char __user *const __user *, envp)
{
	return do_execve(getname(filename), argv, envp);
}

SYSCALL_DEFINE5(execveat,
		int, fd, const char __user *, filename,
		const char __user *const __user *, argv,
		const char __user *const __user *, envp,
		int, flags)
{
	return do_execveat(fd,
			   getname_uflags(filename, flags),
			   argv, envp, flags);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(execve, const char __user *, filename,
	const compat_uptr_t __user *, argv,
	const compat_uptr_t __user *, envp)
{
	return compat_do_execve(getname(filename), argv, envp);
}

COMPAT_SYSCALL_DEFINE5(execveat, int, fd,
		       const char __user *, filename,
		       const compat_uptr_t __user *, argv,
		       const compat_uptr_t __user *, envp,
		       int,  flags)
{
	return compat_do_execveat(fd,
				  getname_uflags(filename, flags),
				  argv, envp, flags);
}
#endif

#ifdef CONFIG_SYSCTL

static int proc_dointvec_minmax_coredump(struct ctl_table *table, int write,
		void *buffer, size_t *lenp, loff_t *ppos)
{
	int error = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (!error)
		validate_coredump_safety();
	return error;
}

static struct ctl_table fs_exec_sysctls[] = {
	{
		.procname	= "suid_dumpable",
		.data		= &suid_dumpable,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax_coredump,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_TWO,
	},
	{ }
};

static int __init init_fs_exec_sysctls(void)
{
	register_sysctl_init("fs", fs_exec_sysctls);
	return 0;
}

fs_initcall(init_fs_exec_sysctls);
#endif /* CONFIG_SYSCTL */
