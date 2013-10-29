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

#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/swap.h>
#include <linux/string.h>
#include <linux/init.h>
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
#include <linux/tracehook.h>
#include <linux/kmod.h>
#include <linux/fsnotify.h>
#include <linux/fs_struct.h>
#include <linux/pipe_fs_i.h>
#include <linux/oom.h>
#include <linux/compat.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/exec.h>

#include <trace/events/task.h>
#include "internal.h"

#include <trace/events/sched.h>

int core_uses_pid;
char core_pattern[CORENAME_MAX_SIZE] = "core";
unsigned int core_pipe_limit;
int suid_dumpable = 0;

struct core_name {
	char *corename;
	int used, size;
};
static atomic_t call_count = ATOMIC_INIT(1);

/* The maximal length of core_pattern is also specified in sysctl.c */

static LIST_HEAD(formats);
static DEFINE_RWLOCK(binfmt_lock);

void __register_binfmt(struct linux_binfmt * fmt, int insert)
{
	BUG_ON(!fmt);
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

/*
 * Note that a shared library must be both readable and executable due to
 * security reasons.
 *
 * Also note that we take the address to load from from the file itself.
 */
SYSCALL_DEFINE1(uselib, const char __user *, library)
{
	struct file *file;
	char *tmp = getname(library);
	int error = PTR_ERR(tmp);
	static const struct open_flags uselib_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_READ | MAY_EXEC | MAY_OPEN,
		.intent = LOOKUP_OPEN
	};

	if (IS_ERR(tmp))
		goto out;

	file = do_filp_open(AT_FDCWD, tmp, &uselib_flags, LOOKUP_FOLLOW);
	putname(tmp);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	error = -EINVAL;
	if (!S_ISREG(file->f_path.dentry->d_inode->i_mode))
		goto exit;

	error = -EACCES;
	if (file->f_path.mnt->mnt_flags & MNT_NOEXEC)
		goto exit;

	fsnotify_open(file);

	error = -ENOEXEC;
	if(file->f_op) {
		struct linux_binfmt * fmt;

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
	}
exit:
	fput(file);
out:
  	return error;
}

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
	int ret;

#ifdef CONFIG_STACK_GROWSUP
	if (write) {
		ret = expand_downwards(bprm->vma, pos);
		if (ret < 0)
			return NULL;
	}
#endif
	ret = get_user_pages(current, bprm->mm, pos,
			1, write, 1, &page, NULL);
	if (ret <= 0)
		return NULL;

	if (write) {
		unsigned long size = bprm->vma->vm_end - bprm->vma->vm_start;
		struct rlimit *rlim;

		acct_arg_size(bprm, size / PAGE_SIZE);

		/*
		 * We've historically supported up to 32 pages (ARG_MAX)
		 * of argument strings even with small stacks
		 */
		if (size <= ARG_MAX)
			return page;

		/*
		 * Limit to 1/4-th the stack size for the argv+env strings.
		 * This ensures that:
		 *  - the remaining binfmt code will not run out of stack space,
		 *  - the program will have a reasonable amount of stack left
		 *    to work from.
		 */
		rlim = current->signal->rlim;
		if (size > ACCESS_ONCE(rlim[RLIMIT_STACK].rlim_cur) / 4) {
			put_page(page);
			return NULL;
		}
	}

	return page;
}

static void put_arg_page(struct page *page)
{
	put_page(page);
}

static void free_arg_page(struct linux_binprm *bprm, int i)
{
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

	bprm->vma = vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma)
		return -ENOMEM;

	down_write(&mm->mmap_sem);
	vma->vm_mm = mm;

	/*
	 * Place the stack at the largest stack address the architecture
	 * supports. Later, we'll move this to an appropriate place. We don't
	 * use STACK_TOP because that can depend on attributes which aren't
	 * configured yet.
	 */
	BUILD_BUG_ON(VM_STACK_FLAGS & VM_STACK_INCOMPLETE_SETUP);
	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;
	vma->vm_flags = VM_STACK_FLAGS | VM_STACK_INCOMPLETE_SETUP;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	INIT_LIST_HEAD(&vma->anon_vma_chain);

	err = security_file_mmap(NULL, 0, 0, 0, vma->vm_start, 1);
	if (err)
		goto err;

	err = insert_vm_struct(mm, vma);
	if (err)
		goto err;

	mm->stack_vm = mm->total_vm = 1;
	up_write(&mm->mmap_sem);
	bprm->p = vma->vm_end - sizeof(void *);
	return 0;
err:
	up_write(&mm->mmap_sem);
	bprm->vma = NULL;
	kmem_cache_free(vm_area_cachep, vma);
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
int bprm_mm_init(struct linux_binprm *bprm)
{
	int err;
	struct mm_struct *mm = NULL;

	bprm->mm = mm = mm_alloc();
	err = -ENOMEM;
	if (!mm)
		goto err;

	err = init_new_context(current, mm);
	if (err)
		goto err;

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
		compat_uptr_t __user *compat;
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

			if (i++ >= max)
				return -E2BIG;

			if (fatal_signal_pending(current))
				return -ERESTARTNOHAND;
			cond_resched();
		}
	}
	return i;
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

		/* We're going to work our way backwords. */
		pos = bprm->p;
		str += len;
		bprm->p -= len;

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
					flush_kernel_dcache_page(kmapped_page);
					kunmap(kmapped_page);
					put_arg_page(kmapped_page);
				}
				kmapped_page = page;
				kaddr = kmap(kmapped_page);
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
		flush_kernel_dcache_page(kmapped_page);
		kunmap(kmapped_page);
		put_arg_page(kmapped_page);
	}
	return ret;
}

/*
 * Like copy_strings, but get argv and its values from kernel memory.
 */
int copy_strings_kernel(int argc, const char *const *__argv,
			struct linux_binprm *bprm)
{
	int r;
	mm_segment_t oldfs = get_fs();
	struct user_arg_ptr argv = {
		.ptr.native = (const char __user *const  __user *)__argv,
	};

	set_fs(KERNEL_DS);
	r = copy_strings(argc, argv, bprm);
	set_fs(oldfs);

	return r;
}
EXPORT_SYMBOL(copy_strings_kernel);

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
	struct mmu_gather tlb;

	BUG_ON(new_start > new_end);

	/*
	 * ensure there are no vmas between where we want to go
	 * and where we are
	 */
	if (vma != find_vma(mm, new_start))
		return -EFAULT;

	/*
	 * cover the whole range: [new_start, old_end)
	 */
	if (vma_adjust(vma, new_start, old_end, vma->vm_pgoff, NULL))
		return -ENOMEM;

	/*
	 * move the page tables downwards, on failure we rely on
	 * process cleanup to remove whatever mess we made.
	 */
	if (length != move_page_tables(vma, old_start,
				       vma, new_start, length))
		return -ENOMEM;

	lru_add_drain();
	tlb_gather_mmu(&tlb, mm, 0);
	if (new_end > old_start) {
		/*
		 * when the old and new regions overlap clear from new_end.
		 */
		free_pgd_range(&tlb, new_end, old_end, new_end,
			vma->vm_next ? vma->vm_next->vm_start : USER_PGTABLES_CEILING);
	} else {
		/*
		 * otherwise, clean from old_start; this is done to not touch
		 * the address space in [new_end, old_start) some architectures
		 * have constraints on va-space that make this illegal (IA64) -
		 * for the others its just a little faster.
		 */
		free_pgd_range(&tlb, old_start, old_end, new_end,
			vma->vm_next ? vma->vm_next->vm_start : USER_PGTABLES_CEILING);
	}
	tlb_finish_mmu(&tlb, new_end, old_end);

	/*
	 * Shrink the vma to just the new range.  Always succeeds.
	 */
	vma_adjust(vma, new_start, new_end, vma->vm_pgoff, NULL);

	return 0;
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

#ifdef CONFIG_STACK_GROWSUP
	/* Limit stack size to 1GB */
	stack_base = rlimit_max(RLIMIT_STACK);
	if (stack_base > (1 << 30))
		stack_base = 1 << 30;

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

	down_write(&mm->mmap_sem);
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

	ret = mprotect_fixup(vma, &prev, vma->vm_start, vma->vm_end,
			vm_flags);
	if (ret)
		goto out_unlock;
	BUG_ON(prev != vma);

	/* Move stack pages down in memory. */
	if (stack_shift) {
		ret = shift_arg_pages(vma, stack_shift);
		if (ret)
			goto out_unlock;
	}

	/* mprotect_fixup is overkill to remove the temporary stack flags */
	vma->vm_flags &= ~VM_STACK_INCOMPLETE_SETUP;

	stack_expand = 131072UL; /* randomly 32*4k (or 2*64k) pages */
	stack_size = vma->vm_end - vma->vm_start;
	/*
	 * Align this down to a page boundary as expand_stack
	 * will align it up.
	 */
	rlim_stack = rlimit(RLIMIT_STACK) & PAGE_MASK;
#ifdef CONFIG_STACK_GROWSUP
	if (stack_size + stack_expand > rlim_stack)
		stack_base = vma->vm_start + rlim_stack;
	else
		stack_base = vma->vm_end + stack_expand;
#else
	if (stack_size + stack_expand > rlim_stack)
		stack_base = vma->vm_end - rlim_stack;
	else
		stack_base = vma->vm_start - stack_expand;
#endif
	current->mm->start_stack = bprm->p;
	ret = expand_stack(vma, stack_base);
	if (ret)
		ret = -EFAULT;

out_unlock:
	up_write(&mm->mmap_sem);
	return ret;
}
EXPORT_SYMBOL(setup_arg_pages);

#endif /* CONFIG_MMU */

struct file *open_exec(const char *name)
{
	struct file *file;
	int err;
	static const struct open_flags open_exec_flags = {
		.open_flag = O_LARGEFILE | O_RDONLY | __FMODE_EXEC,
		.acc_mode = MAY_EXEC | MAY_OPEN,
		.intent = LOOKUP_OPEN
	};

	file = do_filp_open(AT_FDCWD, name, &open_exec_flags, LOOKUP_FOLLOW);
	if (IS_ERR(file))
		goto out;

	err = -EACCES;
	if (!S_ISREG(file->f_path.dentry->d_inode->i_mode))
		goto exit;

	if (file->f_path.mnt->mnt_flags & MNT_NOEXEC)
		goto exit;

	fsnotify_open(file);

	err = deny_write_access(file);
	if (err)
		goto exit;

out:
	return file;

exit:
	fput(file);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(open_exec);

int kernel_read(struct file *file, loff_t offset,
		char *addr, unsigned long count)
{
	mm_segment_t old_fs;
	loff_t pos = offset;
	int result;

	old_fs = get_fs();
	set_fs(get_ds());
	/* The cast to a user pointer is valid due to the set_fs() */
	result = vfs_read(file, (void __user *)addr, count, &pos);
	set_fs(old_fs);
	return result;
}

EXPORT_SYMBOL(kernel_read);

static int exec_mmap(struct mm_struct *mm)
{
	struct task_struct *tsk;
	struct mm_struct * old_mm, *active_mm;

	/* Notify parent that we're no longer interested in the old VM */
	tsk = current;
	old_mm = current->mm;
	mm_release(tsk, old_mm);

	if (old_mm) {
		sync_mm_rss(old_mm);
		/*
		 * Make sure that if there is a core dump in progress
		 * for the old mm, we get out and die instead of going
		 * through with the exec.  We must hold mmap_sem around
		 * checking core_state and changing tsk->mm.
		 */
		down_read(&old_mm->mmap_sem);
		if (unlikely(old_mm->core_state)) {
			up_read(&old_mm->mmap_sem);
			return -EINTR;
		}
	}
	task_lock(tsk);
	active_mm = tsk->active_mm;
	tsk->mm = mm;
	tsk->active_mm = mm;
	activate_mm(active_mm, mm);
	task_unlock(tsk);
	arch_pick_mmap_layout(mm);
	if (old_mm) {
		up_read(&old_mm->mmap_sem);
		BUG_ON(active_mm != old_mm);
		setmax_mm_hiwater_rss(&tsk->signal->maxrss, old_mm);
		mm_update_next_owner(old_mm);
		mmput(old_mm);
		return 0;
	}
	mmdrop(active_mm);
	return 0;
}

/*
 * This function makes sure the current process has its own signal table,
 * so that flush_signal_handlers can later reset the handlers without
 * disturbing other processes.  (Other processes might share the signal
 * table via the CLONE_SIGHAND option to clone().)
 */
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
	if (signal_group_exit(sig)) {
		/*
		 * Another group action in progress, just
		 * return so that the signal is processed.
		 */
		spin_unlock_irq(lock);
		return -EAGAIN;
	}

	sig->group_exit_task = tsk;
	sig->notify_count = zap_other_threads(tsk);
	if (!thread_group_leader(tsk))
		sig->notify_count--;

	while (sig->notify_count) {
		__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(lock);
		schedule();
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

		sig->notify_count = -1;	/* for exit_notify() */
		for (;;) {
			write_lock_irq(&tasklist_lock);
			if (likely(leader->exit_state))
				break;
			__set_current_state(TASK_UNINTERRUPTIBLE);
			write_unlock_irq(&tasklist_lock);
			schedule();
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

		BUG_ON(!same_thread_group(leader, tsk));
		BUG_ON(has_group_leader_pid(tsk));
		/*
		 * An exec() starts a new thread group with the
		 * TGID of the previous thread group. Rehash the
		 * two threads with a switched PID, and release
		 * the former thread group leader:
		 */

		/* Become a process group leader with the old leader's pid.
		 * The old leader becomes a thread of the this thread group.
		 * Note: The old leader also uses this pid until release_task
		 *       is called.  Odd but simple and correct.
		 */
		detach_pid(tsk, PIDTYPE_PID);
		tsk->pid = leader->pid;
		attach_pid(tsk, PIDTYPE_PID,  task_pid(leader));
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
		 * the tracer wont't block again waiting for this thread.
		 */
		if (unlikely(leader->ptrace))
			__wake_up_parent(leader, leader->parent);
		write_unlock_irq(&tasklist_lock);

		release_task(leader);
	}

	sig->group_exit_task = NULL;
	sig->notify_count = 0;

no_thread_group:
	/* we have changed execution domain */
	tsk->exit_signal = SIGCHLD;

	exit_itimers(sig);
	flush_itimer_signals();

	if (atomic_read(&oldsighand->count) != 1) {
		struct sighand_struct *newsighand;
		/*
		 * This ->sighand is shared with the CLONE_SIGHAND
		 * but not CLONE_THREAD task, switch to the new one.
		 */
		newsighand = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
		if (!newsighand)
			return -ENOMEM;

		atomic_set(&newsighand->count, 1);
		memcpy(newsighand->action, oldsighand->action,
		       sizeof(newsighand->action));

		write_lock_irq(&tasklist_lock);
		spin_lock(&oldsighand->siglock);
		rcu_assign_pointer(tsk->sighand, newsighand);
		spin_unlock(&oldsighand->siglock);
		write_unlock_irq(&tasklist_lock);

		__cleanup_sighand(oldsighand);
	}

	BUG_ON(!thread_group_leader(tsk));
	return 0;
}

/*
 * These functions flushes out all traces of the currently running executable
 * so that a new one can be started
 */
static void flush_old_files(struct files_struct * files)
{
	long j = -1;
	struct fdtable *fdt;

	spin_lock(&files->file_lock);
	for (;;) {
		unsigned long set, i;

		j++;
		i = j * BITS_PER_LONG;
		fdt = files_fdtable(files);
		if (i >= fdt->max_fds)
			break;
		set = fdt->close_on_exec[j];
		if (!set)
			continue;
		fdt->close_on_exec[j] = 0;
		spin_unlock(&files->file_lock);
		for ( ; set ; i++,set >>= 1) {
			if (set & 1) {
				sys_close(i);
			}
		}
		spin_lock(&files->file_lock);

	}
	spin_unlock(&files->file_lock);
}

char *get_task_comm(char *buf, struct task_struct *tsk)
{
	/* buf must be at least sizeof(tsk->comm) in size */
	task_lock(tsk);
	strncpy(buf, tsk->comm, sizeof(tsk->comm));
	task_unlock(tsk);
	return buf;
}
EXPORT_SYMBOL_GPL(get_task_comm);

void set_task_comm(struct task_struct *tsk, char *buf)
{
	task_lock(tsk);

	trace_task_rename(tsk, buf);

	/*
	 * Threads may access current->comm without holding
	 * the task lock, so write the string carefully.
	 * Readers without a lock may see incomplete new
	 * names but are safe from non-terminating string reads.
	 */
	memset(tsk->comm, 0, TASK_COMM_LEN);
	wmb();
	strlcpy(tsk->comm, buf, sizeof(tsk->comm));
	task_unlock(tsk);
	perf_event_comm(tsk);
}

static void filename_to_taskname(char *tcomm, const char *fn, unsigned int len)
{
	int i, ch;

	/* Copies the binary name from after last slash */
	for (i = 0; (ch = *(fn++)) != '\0';) {
		if (ch == '/')
			i = 0; /* overwrite what we wrote */
		else
			if (i < len - 1)
				tcomm[i++] = ch;
	}
	tcomm[i] = '\0';
}

int flush_old_exec(struct linux_binprm * bprm)
{
	int retval;

	/*
	 * Make sure we have a private signal table and that
	 * we are unassociated from the previous thread group.
	 */
	retval = de_thread(current);
	if (retval)
		goto out;

	set_mm_exe_file(bprm->mm, bprm->file);

	filename_to_taskname(bprm->tcomm, bprm->filename, sizeof(bprm->tcomm));
	/*
	 * Release all of the old mmap stuff
	 */
	acct_arg_size(bprm, 0);
	retval = exec_mmap(bprm->mm);
	if (retval)
		goto out;

	bprm->mm = NULL;		/* We're using it now */

	set_fs(USER_DS);
	current->flags &=
		~(PF_RANDOMIZE | PF_FORKNOEXEC | PF_KTHREAD | PF_NOFREEZE);
	flush_thread();
	current->personality &= ~bprm->per_clear;

	return 0;

out:
	return retval;
}
EXPORT_SYMBOL(flush_old_exec);

void would_dump(struct linux_binprm *bprm, struct file *file)
{
	if (inode_permission(file->f_path.dentry->d_inode, MAY_READ) < 0)
		bprm->interp_flags |= BINPRM_FLAGS_ENFORCE_NONDUMP;
}
EXPORT_SYMBOL(would_dump);

void setup_new_exec(struct linux_binprm * bprm)
{
	arch_pick_mmap_layout(current->mm);

	/* This is the point of no return */
	current->sas_ss_sp = current->sas_ss_size = 0;

	if (current_euid() == current_uid() && current_egid() == current_gid())
		set_dumpable(current->mm, 1);
	else
		set_dumpable(current->mm, suid_dumpable);

	set_task_comm(current, bprm->tcomm);

	/* Set the new mm task size. We have to do that late because it may
	 * depend on TIF_32BIT which is only updated in flush_thread() on
	 * some architectures like powerpc
	 */
	current->mm->task_size = TASK_SIZE;

	/* install the new credentials */
	if (bprm->cred->uid != current_euid() ||
	    bprm->cred->gid != current_egid()) {
		current->pdeath_signal = 0;
	} else {
		would_dump(bprm, bprm->file);
		if (bprm->interp_flags & BINPRM_FLAGS_ENFORCE_NONDUMP)
			set_dumpable(current->mm, suid_dumpable);
	}

	/*
	 * Flush performance counters when crossing a
	 * security domain:
	 */
	if (!get_dumpable(current->mm))
		perf_event_exit_task(current);

	/* An exec changes our domain. We are no longer part of the thread
	   group */

	current->self_exec_id++;
			
	flush_signal_handlers(current, 0);
	flush_old_files(current->files);
}
EXPORT_SYMBOL(setup_new_exec);

/*
 * Prepare credentials and lock ->cred_guard_mutex.
 * install_exec_creds() commits the new creds and drops the lock.
 * Or, if exec fails before, free_bprm() should release ->cred and
 * and unlock.
 */
int prepare_bprm_creds(struct linux_binprm *bprm)
{
	if (mutex_lock_interruptible(&current->signal->cred_guard_mutex))
		return -ERESTARTNOINTR;

	bprm->cred = prepare_exec_creds();
	if (likely(bprm->cred))
		return 0;

	mutex_unlock(&current->signal->cred_guard_mutex);
	return -ENOMEM;
}

void free_bprm(struct linux_binprm *bprm)
{
	free_arg_pages(bprm);
	if (bprm->cred) {
		mutex_unlock(&current->signal->cred_guard_mutex);
		abort_creds(bprm->cred);
	}
	/* If a binfmt changed the interp, free it. */
	if (bprm->interp != bprm->filename)
		kfree(bprm->interp);
	kfree(bprm);
}

int bprm_change_interp(char *interp, struct linux_binprm *bprm)
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
 * install the new credentials for this executable
 */
void install_exec_creds(struct linux_binprm *bprm)
{
	security_bprm_committing_creds(bprm);

	commit_creds(bprm->cred);
	bprm->cred = NULL;
	/*
	 * cred_guard_mutex must be held at least to this point to prevent
	 * ptrace_attach() from altering our determination of the task's
	 * credentials; any time after this it may be unlocked.
	 */
	security_bprm_committed_creds(bprm);
	mutex_unlock(&current->signal->cred_guard_mutex);
}
EXPORT_SYMBOL(install_exec_creds);

/*
 * determine how safe it is to execute the proposed program
 * - the caller must hold ->cred_guard_mutex to protect against
 *   PTRACE_ATTACH
 */
static int check_unsafe_exec(struct linux_binprm *bprm)
{
	struct task_struct *p = current, *t;
	unsigned n_fs;
	int res = 0;

	if (p->ptrace) {
		if (p->ptrace & PT_PTRACE_CAP)
			bprm->unsafe |= LSM_UNSAFE_PTRACE_CAP;
		else
			bprm->unsafe |= LSM_UNSAFE_PTRACE;
	}

	n_fs = 1;
	spin_lock(&p->fs->lock);
	rcu_read_lock();
	for (t = next_thread(p); t != p; t = next_thread(t)) {
		if (t->fs == p->fs)
			n_fs++;
	}
	rcu_read_unlock();

	if (p->fs->users > n_fs) {
		bprm->unsafe |= LSM_UNSAFE_SHARE;
	} else {
		res = -EAGAIN;
		if (!p->fs->in_exec) {
			p->fs->in_exec = 1;
			res = 1;
		}
	}
	spin_unlock(&p->fs->lock);

	return res;
}

/* 
 * Fill the binprm structure from the inode. 
 * Check permissions, then read the first 128 (BINPRM_BUF_SIZE) bytes
 *
 * This may be called multiple times for binary chains (scripts for example).
 */
int prepare_binprm(struct linux_binprm *bprm)
{
	umode_t mode;
	struct inode * inode = bprm->file->f_path.dentry->d_inode;
	int retval;

	mode = inode->i_mode;
	if (bprm->file->f_op == NULL)
		return -EACCES;

	/* clear any previous set[ug]id data from a previous binary */
	bprm->cred->euid = current_euid();
	bprm->cred->egid = current_egid();

	if (!(bprm->file->f_path.mnt->mnt_flags & MNT_NOSUID)) {
		/* Set-uid? */
		if (mode & S_ISUID) {
			bprm->per_clear |= PER_CLEAR_ON_SETID;
			bprm->cred->euid = inode->i_uid;
		}

		/* Set-gid? */
		/*
		 * If setgid is set but no group execute bit then this
		 * is a candidate for mandatory locking, not a setgid
		 * executable.
		 */
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			bprm->per_clear |= PER_CLEAR_ON_SETID;
			bprm->cred->egid = inode->i_gid;
		}
	}

	/* fill in binprm security blob */
	retval = security_bprm_set_creds(bprm);
	if (retval)
		return retval;
	bprm->cred_prepared = 1;

	memset(bprm->buf, 0, BINPRM_BUF_SIZE);
	return kernel_read(bprm->file, 0, bprm->buf, BINPRM_BUF_SIZE);
}

EXPORT_SYMBOL(prepare_binprm);

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
		kaddr = kmap_atomic(page);

		for (; offset < PAGE_SIZE && kaddr[offset];
				offset++, bprm->p++)
			;

		kunmap_atomic(kaddr);
		put_arg_page(page);

		if (offset == PAGE_SIZE)
			free_arg_page(bprm, (bprm->p >> PAGE_SHIFT) - 1);
	} while (offset == PAGE_SIZE);

	bprm->p++;
	bprm->argc--;
	ret = 0;

out:
	return ret;
}
EXPORT_SYMBOL(remove_arg_zero);

/*
 * cycle the list of binary formats handler, until one recognizes the image
 */
int search_binary_handler(struct linux_binprm *bprm,struct pt_regs *regs)
{
	unsigned int depth = bprm->recursion_depth;
	int try,retval;
	struct linux_binfmt *fmt;
	pid_t old_pid, old_vpid;

	/* This allows 4 levels of binfmt rewrites before failing hard. */
	if (depth > 5)
		return -ELOOP;

	retval = security_bprm_check(bprm);
	if (retval)
		return retval;

	retval = audit_bprm(bprm);
	if (retval)
		return retval;

	/* Need to fetch pid before load_binary changes it */
	old_pid = current->pid;
	rcu_read_lock();
	old_vpid = task_pid_nr_ns(current, task_active_pid_ns(current->parent));
	rcu_read_unlock();

	retval = -ENOENT;
	for (try=0; try<2; try++) {
		read_lock(&binfmt_lock);
		list_for_each_entry(fmt, &formats, lh) {
			int (*fn)(struct linux_binprm *, struct pt_regs *) = fmt->load_binary;
			if (!fn)
				continue;
			if (!try_module_get(fmt->module))
				continue;
			read_unlock(&binfmt_lock);
			bprm->recursion_depth = depth + 1;
			retval = fn(bprm, regs);
			bprm->recursion_depth = depth;
			if (retval >= 0) {
				if (depth == 0) {
					trace_sched_process_exec(current, old_pid, bprm);
					ptrace_event(PTRACE_EVENT_EXEC, old_vpid);
				}
				put_binfmt(fmt);
				allow_write_access(bprm->file);
				if (bprm->file)
					fput(bprm->file);
				bprm->file = NULL;
				current->did_exec = 1;
				proc_exec_connector(current);
				return retval;
			}
			read_lock(&binfmt_lock);
			put_binfmt(fmt);
			if (retval != -ENOEXEC || bprm->mm == NULL)
				break;
			if (!bprm->file) {
				read_unlock(&binfmt_lock);
				return retval;
			}
		}
		read_unlock(&binfmt_lock);
#ifdef CONFIG_MODULES
		if (retval != -ENOEXEC || bprm->mm == NULL) {
			break;
		} else {
#define printable(c) (((c)=='\t') || ((c)=='\n') || (0x20<=(c) && (c)<=0x7e))
			if (printable(bprm->buf[0]) &&
			    printable(bprm->buf[1]) &&
			    printable(bprm->buf[2]) &&
			    printable(bprm->buf[3]))
				break; /* -ENOEXEC */
			if (try)
				break; /* -ENOEXEC */
			request_module("binfmt-%04x", *(unsigned short *)(&bprm->buf[2]));
		}
#else
		break;
#endif
	}
	return retval;
}

EXPORT_SYMBOL(search_binary_handler);

/*
 * sys_execve() executes a new program.
 */
static int do_execve_common(const char *filename,
				struct user_arg_ptr argv,
				struct user_arg_ptr envp,
				struct pt_regs *regs)
{
	struct linux_binprm *bprm;
	struct file *file;
	struct files_struct *displaced;
	bool clear_in_exec;
	int retval;
	const struct cred *cred = current_cred();

	/*
	 * We move the actual failure in case of RLIMIT_NPROC excess from
	 * set*uid() to execve() because too many poorly written programs
	 * don't check setuid() return code.  Here we additionally recheck
	 * whether NPROC limit is still exceeded.
	 */
	if ((current->flags & PF_NPROC_EXCEEDED) &&
	    atomic_read(&cred->user->processes) > rlimit(RLIMIT_NPROC)) {
		retval = -EAGAIN;
		goto out_ret;
	}

	/* We're below the limit (still or again), so we don't want to make
	 * further execve() calls fail. */
	current->flags &= ~PF_NPROC_EXCEEDED;

	retval = unshare_files(&displaced);
	if (retval)
		goto out_ret;

	retval = -ENOMEM;
	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	if (!bprm)
		goto out_files;

	retval = prepare_bprm_creds(bprm);
	if (retval)
		goto out_free;

	retval = check_unsafe_exec(bprm);
	if (retval < 0)
		goto out_free;
	clear_in_exec = retval;
	current->in_execve = 1;

	file = open_exec(filename);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_unmark;

	sched_exec();

	bprm->file = file;
	bprm->filename = filename;
	bprm->interp = filename;

	retval = bprm_mm_init(bprm);
	if (retval)
		goto out_file;

	bprm->argc = count(argv, MAX_ARG_STRINGS);
	if ((retval = bprm->argc) < 0)
		goto out;

	bprm->envc = count(envp, MAX_ARG_STRINGS);
	if ((retval = bprm->envc) < 0)
		goto out;

	retval = prepare_binprm(bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings_kernel(1, &bprm->filename, bprm);
	if (retval < 0)
		goto out;

	bprm->exec = bprm->p;
	retval = copy_strings(bprm->envc, envp, bprm);
	if (retval < 0)
		goto out;

	retval = copy_strings(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out;

	retval = search_binary_handler(bprm,regs);
	if (retval < 0)
		goto out;

	/* execve succeeded */
	current->fs->in_exec = 0;
	current->in_execve = 0;
	acct_update_integrals(current);
	free_bprm(bprm);
	if (displaced)
		put_files_struct(displaced);
	return retval;

out:
	if (bprm->mm) {
		acct_arg_size(bprm, 0);
		mmput(bprm->mm);
	}

out_file:
	if (bprm->file) {
		allow_write_access(bprm->file);
		fput(bprm->file);
	}

out_unmark:
	if (clear_in_exec)
		current->fs->in_exec = 0;
	current->in_execve = 0;

out_free:
	free_bprm(bprm);

out_files:
	if (displaced)
		reset_files_struct(displaced);
out_ret:
	return retval;
}

int do_execve(const char *filename,
	const char __user *const __user *__argv,
	const char __user *const __user *__envp,
	struct pt_regs *regs)
{
	struct user_arg_ptr argv = { .ptr.native = __argv };
	struct user_arg_ptr envp = { .ptr.native = __envp };
	return do_execve_common(filename, argv, envp, regs);
}

#ifdef CONFIG_COMPAT
int compat_do_execve(char *filename,
	compat_uptr_t __user *__argv,
	compat_uptr_t __user *__envp,
	struct pt_regs *regs)
{
	struct user_arg_ptr argv = {
		.is_compat = true,
		.ptr.compat = __argv,
	};
	struct user_arg_ptr envp = {
		.is_compat = true,
		.ptr.compat = __envp,
	};
	return do_execve_common(filename, argv, envp, regs);
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

static int expand_corename(struct core_name *cn)
{
	char *old_corename = cn->corename;

	cn->size = CORENAME_MAX_SIZE * atomic_inc_return(&call_count);
	cn->corename = krealloc(old_corename, cn->size, GFP_KERNEL);

	if (!cn->corename) {
		kfree(old_corename);
		return -ENOMEM;
	}

	return 0;
}

static int cn_printf(struct core_name *cn, const char *fmt, ...)
{
	char *cur;
	int need;
	int ret;
	va_list arg;

	va_start(arg, fmt);
	need = vsnprintf(NULL, 0, fmt, arg);
	va_end(arg);

	if (likely(need < cn->size - cn->used - 1))
		goto out_printf;

	ret = expand_corename(cn);
	if (ret)
		goto expand_fail;

out_printf:
	cur = cn->corename + cn->used;
	va_start(arg, fmt);
	vsnprintf(cur, need + 1, fmt, arg);
	va_end(arg);
	cn->used += need;
	return 0;

expand_fail:
	return ret;
}

static void cn_escape(char *str)
{
	for (; *str; str++)
		if (*str == '/')
			*str = '!';
}

static int cn_print_exe_file(struct core_name *cn)
{
	struct file *exe_file;
	char *pathbuf, *path;
	int ret;

	exe_file = get_mm_exe_file(current->mm);
	if (!exe_file) {
		char *commstart = cn->corename + cn->used;
		ret = cn_printf(cn, "%s (path unknown)", current->comm);
		cn_escape(commstart);
		return ret;
	}

	pathbuf = kmalloc(PATH_MAX, GFP_TEMPORARY);
	if (!pathbuf) {
		ret = -ENOMEM;
		goto put_exe_file;
	}

	path = d_path(&exe_file->f_path, pathbuf, PATH_MAX);
	if (IS_ERR(path)) {
		ret = PTR_ERR(path);
		goto free_buf;
	}

	cn_escape(path);

	ret = cn_printf(cn, "%s", path);

free_buf:
	kfree(pathbuf);
put_exe_file:
	fput(exe_file);
	return ret;
}

/* format_corename will inspect the pattern parameter, and output a
 * name into corename, which must have space for at least
 * CORENAME_MAX_SIZE bytes plus one byte for the zero terminator.
 */
static int format_corename(struct core_name *cn, long signr)
{
	const struct cred *cred = current_cred();
	const char *pat_ptr = core_pattern;
	int ispipe = (*pat_ptr == '|');
	int pid_in_pattern = 0;
	int err = 0;

	cn->size = CORENAME_MAX_SIZE * atomic_read(&call_count);
	cn->corename = kmalloc(cn->size, GFP_KERNEL);
	cn->used = 0;

	if (!cn->corename)
		return -ENOMEM;

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		if (*pat_ptr != '%') {
			if (*pat_ptr == 0)
				goto out;
			err = cn_printf(cn, "%c", *pat_ptr++);
		} else {
			switch (*++pat_ptr) {
			/* single % at the end, drop that */
			case 0:
				goto out;
			/* Double percent, output one percent */
			case '%':
				err = cn_printf(cn, "%c", '%');
				break;
			/* pid */
			case 'p':
				pid_in_pattern = 1;
				err = cn_printf(cn, "%d",
					      task_tgid_vnr(current));
				break;
			/* uid */
			case 'u':
				err = cn_printf(cn, "%d", cred->uid);
				break;
			/* gid */
			case 'g':
				err = cn_printf(cn, "%d", cred->gid);
				break;
			/* signal that caused the coredump */
			case 's':
				err = cn_printf(cn, "%ld", signr);
				break;
			/* UNIX time of coredump */
			case 't': {
				struct timeval tv;
				do_gettimeofday(&tv);
				err = cn_printf(cn, "%lu", tv.tv_sec);
				break;
			}
			/* hostname */
			case 'h': {
				char *namestart = cn->corename + cn->used;
				down_read(&uts_sem);
				err = cn_printf(cn, "%s",
					      utsname()->nodename);
				up_read(&uts_sem);
				cn_escape(namestart);
				break;
			}
			/* executable */
			case 'e': {
				char *commstart = cn->corename + cn->used;
				err = cn_printf(cn, "%s", current->comm);
				cn_escape(commstart);
				break;
			}
			case 'E':
				err = cn_print_exe_file(cn);
				break;
			/* core limit size */
			case 'c':
				err = cn_printf(cn, "%lu",
					      rlimit(RLIMIT_CORE));
				break;
			default:
				break;
			}
			++pat_ptr;
		}

		if (err)
			return err;
	}

	/* Backward compatibility with core_uses_pid:
	 *
	 * If core_pattern does not include a %p (as is the default)
	 * and core_uses_pid is set, then .%pid will be appended to
	 * the filename. Do not do this for piped commands. */
	if (!ispipe && !pid_in_pattern && core_uses_pid) {
		err = cn_printf(cn, ".%d", task_tgid_vnr(current));
		if (err)
			return err;
	}
out:
	return ispipe;
}

static int zap_process(struct task_struct *start, int exit_code)
{
	struct task_struct *t;
	int nr = 0;

	start->signal->flags = SIGNAL_GROUP_EXIT;
	start->signal->group_exit_code = exit_code;
	start->signal->group_stop_count = 0;

	t = start;
	do {
		task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
		if (t != current && t->mm) {
			sigaddset(&t->pending.signal, SIGKILL);
			signal_wake_up(t, 1);
			nr++;
		}
	} while_each_thread(start, t);

	return nr;
}

static inline int zap_threads(struct task_struct *tsk, struct mm_struct *mm,
				struct core_state *core_state, int exit_code)
{
	struct task_struct *g, *p;
	unsigned long flags;
	int nr = -EAGAIN;

	spin_lock_irq(&tsk->sighand->siglock);
	if (!signal_group_exit(tsk->signal)) {
		mm->core_state = core_state;
		nr = zap_process(tsk, exit_code);
	}
	spin_unlock_irq(&tsk->sighand->siglock);
	if (unlikely(nr < 0))
		return nr;

	if (atomic_read(&mm->mm_users) == nr + 1)
		goto done;
	/*
	 * We should find and kill all tasks which use this mm, and we should
	 * count them correctly into ->nr_threads. We don't take tasklist
	 * lock, but this is safe wrt:
	 *
	 * fork:
	 *	None of sub-threads can fork after zap_process(leader). All
	 *	processes which were created before this point should be
	 *	visible to zap_threads() because copy_process() adds the new
	 *	process to the tail of init_task.tasks list, and lock/unlock
	 *	of ->siglock provides a memory barrier.
	 *
	 * do_exit:
	 *	The caller holds mm->mmap_sem. This means that the task which
	 *	uses this mm can't pass exit_mm(), so it can't exit or clear
	 *	its ->mm.
	 *
	 * de_thread:
	 *	It does list_replace_rcu(&leader->tasks, &current->tasks),
	 *	we must see either old or new leader, this does not matter.
	 *	However, it can change p->sighand, so lock_task_sighand(p)
	 *	must be used. Since p->mm != NULL and we hold ->mmap_sem
	 *	it can't fail.
	 *
	 *	Note also that "g" can be the old leader with ->mm == NULL
	 *	and already unhashed and thus removed from ->thread_group.
	 *	This is OK, __unhash_process()->list_del_rcu() does not
	 *	clear the ->next pointer, we will find the new leader via
	 *	next_thread().
	 */
	rcu_read_lock();
	for_each_process(g) {
		if (g == tsk->group_leader)
			continue;
		if (g->flags & PF_KTHREAD)
			continue;
		p = g;
		do {
			if (p->mm) {
				if (unlikely(p->mm == mm)) {
					lock_task_sighand(p, &flags);
					nr += zap_process(p, exit_code);
					unlock_task_sighand(p, &flags);
				}
				break;
			}
		} while_each_thread(g, p);
	}
	rcu_read_unlock();
done:
	atomic_set(&core_state->nr_threads, nr);
	return nr;
}

static int coredump_wait(int exit_code, struct core_state *core_state)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	int core_waiters = -EBUSY;

	init_completion(&core_state->startup);
	core_state->dumper.task = tsk;
	core_state->dumper.next = NULL;

	down_write(&mm->mmap_sem);
	if (!mm->core_state)
		core_waiters = zap_threads(tsk, mm, core_state, exit_code);
	up_write(&mm->mmap_sem);

	if (core_waiters > 0)
		wait_for_completion(&core_state->startup);

	return core_waiters;
}

static void coredump_finish(struct mm_struct *mm)
{
	struct core_thread *curr, *next;
	struct task_struct *task;

	next = mm->core_state->dumper.next;
	while ((curr = next) != NULL) {
		next = curr->next;
		task = curr->task;
		/*
		 * see exit_mm(), curr->task must not see
		 * ->task == NULL before we read ->next.
		 */
		smp_mb();
		curr->task = NULL;
		wake_up_process(task);
	}

	mm->core_state = NULL;
}

/*
 * set_dumpable converts traditional three-value dumpable to two flags and
 * stores them into mm->flags.  It modifies lower two bits of mm->flags, but
 * these bits are not changed atomically.  So get_dumpable can observe the
 * intermediate state.  To avoid doing unexpected behavior, get get_dumpable
 * return either old dumpable or new one by paying attention to the order of
 * modifying the bits.
 *
 * dumpable |   mm->flags (binary)
 * old  new | initial interim  final
 * ---------+-----------------------
 *  0    1  |   00      01      01
 *  0    2  |   00      10(*)   11
 *  1    0  |   01      00      00
 *  1    2  |   01      11      11
 *  2    0  |   11      10(*)   00
 *  2    1  |   11      11      01
 *
 * (*) get_dumpable regards interim value of 10 as 11.
 */
void set_dumpable(struct mm_struct *mm, int value)
{
	switch (value) {
	case 0:
		clear_bit(MMF_DUMPABLE, &mm->flags);
		smp_wmb();
		clear_bit(MMF_DUMP_SECURELY, &mm->flags);
		break;
	case 1:
		set_bit(MMF_DUMPABLE, &mm->flags);
		smp_wmb();
		clear_bit(MMF_DUMP_SECURELY, &mm->flags);
		break;
	case 2:
		set_bit(MMF_DUMP_SECURELY, &mm->flags);
		smp_wmb();
		set_bit(MMF_DUMPABLE, &mm->flags);
		break;
	}
}

static int __get_dumpable(unsigned long mm_flags)
{
	int ret;

	ret = mm_flags & MMF_DUMPABLE_MASK;
	return (ret >= 2) ? 2 : ret;
}

int get_dumpable(struct mm_struct *mm)
{
	return __get_dumpable(mm->flags);
}

static void wait_for_dump_helpers(struct file *file)
{
	struct pipe_inode_info *pipe;

	pipe = file->f_path.dentry->d_inode->i_pipe;

	pipe_lock(pipe);
	pipe->readers++;
	pipe->writers--;

	while ((pipe->readers > 1) && (!signal_pending(current))) {
		wake_up_interruptible_sync(&pipe->wait);
		kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
		pipe_wait(pipe);
	}

	pipe->readers--;
	pipe->writers++;
	pipe_unlock(pipe);

}


/*
 * umh_pipe_setup
 * helper function to customize the process used
 * to collect the core in userspace.  Specifically
 * it sets up a pipe and installs it as fd 0 (stdin)
 * for the process.  Returns 0 on success, or
 * PTR_ERR on failure.
 * Note that it also sets the core limit to 1.  This
 * is a special value that we use to trap recursive
 * core dumps
 */
static int umh_pipe_setup(struct subprocess_info *info, struct cred *new)
{
	struct file *rp, *wp;
	struct fdtable *fdt;
	struct coredump_params *cp = (struct coredump_params *)info->data;
	struct files_struct *cf = current->files;

	wp = create_write_pipe(0);
	if (IS_ERR(wp))
		return PTR_ERR(wp);

	rp = create_read_pipe(wp, 0);
	if (IS_ERR(rp)) {
		free_write_pipe(wp);
		return PTR_ERR(rp);
	}

	cp->file = wp;

	sys_close(0);
	fd_install(0, rp);
	spin_lock(&cf->file_lock);
	fdt = files_fdtable(cf);
	__set_open_fd(0, fdt);
	__clear_close_on_exec(0, fdt);
	spin_unlock(&cf->file_lock);

	/* and disallow core files too */
	current->signal->rlim[RLIMIT_CORE] = (struct rlimit){1, 1};

	return 0;
}

void do_coredump(long signr, int exit_code, struct pt_regs *regs)
{
	struct core_state core_state;
	struct core_name cn;
	struct mm_struct *mm = current->mm;
	struct linux_binfmt * binfmt;
	const struct cred *old_cred;
	struct cred *cred;
	int retval = 0;
	int flag = 0;
	int ispipe;
	static atomic_t core_dump_count = ATOMIC_INIT(0);
	struct coredump_params cprm = {
		.signr = signr,
		.regs = regs,
		.limit = rlimit(RLIMIT_CORE),
		/*
		 * We must use the same mm->flags while dumping core to avoid
		 * inconsistency of bit flags, since this flag is not protected
		 * by any locks.
		 */
		.mm_flags = mm->flags,
	};

	audit_core_dumps(signr);

	binfmt = mm->binfmt;
	if (!binfmt || !binfmt->core_dump)
		goto fail;
	if (!__get_dumpable(cprm.mm_flags))
		goto fail;

	cred = prepare_creds();
	if (!cred)
		goto fail;
	/*
	 *	We cannot trust fsuid as being the "true" uid of the
	 *	process nor do we know its entire history. We only know it
	 *	was tainted so we dump it as root in mode 2.
	 */
	if (__get_dumpable(cprm.mm_flags) == 2) {
		/* Setuid core dump mode */
		flag = O_EXCL;		/* Stop rewrite attacks */
		cred->fsuid = 0;	/* Dump root private */
	}

	retval = coredump_wait(exit_code, &core_state);
	if (retval < 0)
		goto fail_creds;

	old_cred = override_creds(cred);

	/*
	 * Clear any false indication of pending signals that might
	 * be seen by the filesystem code called to write the core file.
	 */
	clear_thread_flag(TIF_SIGPENDING);

	ispipe = format_corename(&cn, signr);

 	if (ispipe) {
		int dump_count;
		char **helper_argv;

		if (ispipe < 0) {
			printk(KERN_WARNING "format_corename failed\n");
			printk(KERN_WARNING "Aborting core\n");
			goto fail_corename;
		}

		if (cprm.limit == 1) {
			/*
			 * Normally core limits are irrelevant to pipes, since
			 * we're not writing to the file system, but we use
			 * cprm.limit of 1 here as a speacial value. Any
			 * non-1 limit gets set to RLIM_INFINITY below, but
			 * a limit of 0 skips the dump.  This is a consistent
			 * way to catch recursive crashes.  We can still crash
			 * if the core_pattern binary sets RLIM_CORE =  !1
			 * but it runs as root, and can do lots of stupid things
			 * Note that we use task_tgid_vnr here to grab the pid
			 * of the process group leader.  That way we get the
			 * right pid if a thread in a multi-threaded
			 * core_pattern process dies.
			 */
			printk(KERN_WARNING
				"Process %d(%s) has RLIMIT_CORE set to 1\n",
				task_tgid_vnr(current), current->comm);
			printk(KERN_WARNING "Aborting core\n");
			goto fail_unlock;
		}
		cprm.limit = RLIM_INFINITY;

		dump_count = atomic_inc_return(&core_dump_count);
		if (core_pipe_limit && (core_pipe_limit < dump_count)) {
			printk(KERN_WARNING "Pid %d(%s) over core_pipe_limit\n",
			       task_tgid_vnr(current), current->comm);
			printk(KERN_WARNING "Skipping core dump\n");
			goto fail_dropcount;
		}

		helper_argv = argv_split(GFP_KERNEL, cn.corename+1, NULL);
		if (!helper_argv) {
			printk(KERN_WARNING "%s failed to allocate memory\n",
			       __func__);
			goto fail_dropcount;
		}

		retval = call_usermodehelper_fns(helper_argv[0], helper_argv,
					NULL, UMH_WAIT_EXEC, umh_pipe_setup,
					NULL, &cprm);
		argv_free(helper_argv);
		if (retval) {
 			printk(KERN_INFO "Core dump to %s pipe failed\n",
			       cn.corename);
			goto close_fail;
 		}
	} else {
		struct inode *inode;

		if (cprm.limit < binfmt->min_coredump)
			goto fail_unlock;

		cprm.file = filp_open(cn.corename,
				 O_CREAT | 2 | O_NOFOLLOW | O_LARGEFILE | flag,
				 0600);
		if (IS_ERR(cprm.file))
			goto fail_unlock;

		inode = cprm.file->f_path.dentry->d_inode;
		if (inode->i_nlink > 1)
			goto close_fail;
		if (d_unhashed(cprm.file->f_path.dentry))
			goto close_fail;
		/*
		 * AK: actually i see no reason to not allow this for named
		 * pipes etc, but keep the previous behaviour for now.
		 */
		if (!S_ISREG(inode->i_mode))
			goto close_fail;
		/*
		 * Dont allow local users get cute and trick others to coredump
		 * into their pre-created files.
		 */
		if (inode->i_uid != current_fsuid())
			goto close_fail;
		if (!cprm.file->f_op || !cprm.file->f_op->write)
			goto close_fail;
		if (do_truncate(cprm.file->f_path.dentry, 0, 0, cprm.file))
			goto close_fail;
	}

	retval = binfmt->core_dump(&cprm);
	if (retval)
		current->signal->group_exit_code |= 0x80;

	if (ispipe && core_pipe_limit)
		wait_for_dump_helpers(cprm.file);
close_fail:
	if (cprm.file)
		filp_close(cprm.file, NULL);
fail_dropcount:
	if (ispipe)
		atomic_dec(&core_dump_count);
fail_unlock:
	kfree(cn.corename);
fail_corename:
	coredump_finish(mm);
	revert_creds(old_cred);
fail_creds:
	put_cred(cred);
fail:
	return;
}

/*
 * Core dumping helper functions.  These are the only things you should
 * do on a core-file: use only these functions to write out all the
 * necessary info.
 */
int dump_write(struct file *file, const void *addr, int nr)
{
	return access_ok(VERIFY_READ, addr, nr) && file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}
EXPORT_SYMBOL(dump_write);

int dump_seek(struct file *file, loff_t off)
{
	int ret = 1;

	if (file->f_op->llseek && file->f_op->llseek != no_llseek) {
		if (file->f_op->llseek(file, off, SEEK_CUR) < 0)
			return 0;
	} else {
		char *buf = (char *)get_zeroed_page(GFP_KERNEL);

		if (!buf)
			return 0;
		while (off > 0) {
			unsigned long n = off;

			if (n > PAGE_SIZE)
				n = PAGE_SIZE;
			if (!dump_write(file, buf, n)) {
				ret = 0;
				break;
			}
			off -= n;
		}
		free_page((unsigned long)buf);
	}
	return ret;
}
EXPORT_SYMBOL(dump_seek);
