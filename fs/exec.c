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
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/key.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/swap.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/rmap.h>
#include <linux/tsacct_kern.h>
#include <linux/cn_proc.h>
#include <linux/audit.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

int core_uses_pid;
char core_pattern[CORENAME_MAX_SIZE] = "core";
int suid_dumpable = 0;

EXPORT_SYMBOL(suid_dumpable);
/* The maximal length of core_pattern is also specified in sysctl.c */

static LIST_HEAD(formats);
static DEFINE_RWLOCK(binfmt_lock);

int register_binfmt(struct linux_binfmt * fmt)
{
	if (!fmt)
		return -EINVAL;
	write_lock(&binfmt_lock);
	list_add(&fmt->lh, &formats);
	write_unlock(&binfmt_lock);
	return 0;	
}

EXPORT_SYMBOL(register_binfmt);

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
asmlinkage long sys_uselib(const char __user * library)
{
	struct file * file;
	struct nameidata nd;
	int error;

	error = __user_path_lookup_open(library, LOOKUP_FOLLOW, &nd, FMODE_READ|FMODE_EXEC);
	if (error)
		goto out;

	error = -EINVAL;
	if (!S_ISREG(nd.dentry->d_inode->i_mode))
		goto exit;

	error = vfs_permission(&nd, MAY_READ | MAY_EXEC);
	if (error)
		goto exit;

	file = nameidata_to_filp(&nd, O_RDONLY);
	error = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

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
	fput(file);
out:
  	return error;
exit:
	release_open_intent(&nd);
	path_release(&nd);
	goto out;
}

#ifdef CONFIG_MMU

static struct page *get_arg_page(struct linux_binprm *bprm, unsigned long pos,
		int write)
{
	struct page *page;
	int ret;

#ifdef CONFIG_STACK_GROWSUP
	if (write) {
		ret = expand_stack_downwards(bprm->vma, pos);
		if (ret < 0)
			return NULL;
	}
#endif
	ret = get_user_pages(current, bprm->mm, pos,
			1, write, 1, &page, NULL);
	if (ret <= 0)
		return NULL;

	if (write) {
		struct rlimit *rlim = current->signal->rlim;
		unsigned long size = bprm->vma->vm_end - bprm->vma->vm_start;

		/*
		 * Limit to 1/4-th the stack size for the argv+env strings.
		 * This ensures that:
		 *  - the remaining binfmt code will not run out of stack space,
		 *  - the program will have a reasonable amount of stack left
		 *    to work from.
		 */
		if (size > rlim[RLIMIT_STACK].rlim_cur / 4) {
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
	int err = -ENOMEM;
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = bprm->mm;

	bprm->vma = vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma)
		goto err;

	down_write(&mm->mmap_sem);
	vma->vm_mm = mm;

	/*
	 * Place the stack at the largest stack address the architecture
	 * supports. Later, we'll move this to an appropriate place. We don't
	 * use STACK_TOP because that can depend on attributes which aren't
	 * configured yet.
	 */
	vma->vm_end = STACK_TOP_MAX;
	vma->vm_start = vma->vm_end - PAGE_SIZE;

	vma->vm_flags = VM_STACK_FLAGS;
	vma->vm_page_prot = protection_map[vma->vm_flags & 0x7];
	err = insert_vm_struct(mm, vma);
	if (err) {
		up_write(&mm->mmap_sem);
		goto err;
	}

	mm->stack_vm = mm->total_vm = 1;
	up_write(&mm->mmap_sem);

	bprm->p = vma->vm_end - sizeof(void *);

	return 0;

err:
	if (vma) {
		bprm->vma = NULL;
		kmem_cache_free(vm_area_cachep, vma);
	}

	return err;
}

static bool valid_arg_len(struct linux_binprm *bprm, long len)
{
	return len <= MAX_ARG_STRLEN;
}

#else

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

/*
 * count() counts the number of strings in array ARGV.
 */
static int count(char __user * __user * argv, int max)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			char __user * p;

			if (get_user(p, argv))
				return -EFAULT;
			if (!p)
				break;
			argv++;
			if(++i > max)
				return -E2BIG;
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
static int copy_strings(int argc, char __user * __user * argv,
			struct linux_binprm *bprm)
{
	struct page *kmapped_page = NULL;
	char *kaddr = NULL;
	unsigned long kpos = 0;
	int ret;

	while (argc-- > 0) {
		char __user *str;
		int len;
		unsigned long pos;

		if (get_user(str, argv+argc) ||
				!(len = strnlen_user(str, MAX_ARG_STRLEN))) {
			ret = -EFAULT;
			goto out;
		}

		if (!valid_arg_len(bprm, len)) {
			ret = -E2BIG;
			goto out;
		}

		/* We're going to work our way backwords. */
		pos = bprm->p;
		str += len;
		bprm->p -= len;

		while (len > 0) {
			int offset, bytes_to_copy;

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
int copy_strings_kernel(int argc,char ** argv, struct linux_binprm *bprm)
{
	int r;
	mm_segment_t oldfs = get_fs();
	set_fs(KERNEL_DS);
	r = copy_strings(argc, (char __user * __user *)argv, bprm);
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
	struct mmu_gather *tlb;

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
	vma_adjust(vma, new_start, old_end, vma->vm_pgoff, NULL);

	/*
	 * move the page tables downwards, on failure we rely on
	 * process cleanup to remove whatever mess we made.
	 */
	if (length != move_page_tables(vma, old_start,
				       vma, new_start, length))
		return -ENOMEM;

	lru_add_drain();
	tlb = tlb_gather_mmu(mm, 0);
	if (new_end > old_start) {
		/*
		 * when the old and new regions overlap clear from new_end.
		 */
		free_pgd_range(&tlb, new_end, old_end, new_end,
			vma->vm_next ? vma->vm_next->vm_start : 0);
	} else {
		/*
		 * otherwise, clean from old_start; this is done to not touch
		 * the address space in [new_end, old_start) some architectures
		 * have constraints on va-space that make this illegal (IA64) -
		 * for the others its just a little faster.
		 */
		free_pgd_range(&tlb, old_start, old_end, new_end,
			vma->vm_next ? vma->vm_next->vm_start : 0);
	}
	tlb_finish_mmu(tlb, new_end, old_end);

	/*
	 * shrink the vma to just the new range.
	 */
	vma_adjust(vma, new_start, new_end, vma->vm_pgoff, NULL);

	return 0;
}

#define EXTRA_STACK_VM_PAGES	20	/* random */

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

#ifdef CONFIG_STACK_GROWSUP
	/* Limit stack size to 1GB */
	stack_base = current->signal->rlim[RLIMIT_STACK].rlim_max;
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
	stack_shift = vma->vm_end - stack_top;

	bprm->p -= stack_shift;
	mm->arg_start = bprm->p;
#endif

	if (bprm->loader)
		bprm->loader -= stack_shift;
	bprm->exec -= stack_shift;

	down_write(&mm->mmap_sem);
	vm_flags = vma->vm_flags;

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

	ret = mprotect_fixup(vma, &prev, vma->vm_start, vma->vm_end,
			vm_flags);
	if (ret)
		goto out_unlock;
	BUG_ON(prev != vma);

	/* Move stack pages down in memory. */
	if (stack_shift) {
		ret = shift_arg_pages(vma, stack_shift);
		if (ret) {
			up_write(&mm->mmap_sem);
			return ret;
		}
	}

#ifdef CONFIG_STACK_GROWSUP
	stack_base = vma->vm_end + EXTRA_STACK_VM_PAGES * PAGE_SIZE;
#else
	stack_base = vma->vm_start - EXTRA_STACK_VM_PAGES * PAGE_SIZE;
#endif
	ret = expand_stack(vma, stack_base);
	if (ret)
		ret = -EFAULT;

out_unlock:
	up_write(&mm->mmap_sem);
	return 0;
}
EXPORT_SYMBOL(setup_arg_pages);

#endif /* CONFIG_MMU */

struct file *open_exec(const char *name)
{
	struct nameidata nd;
	int err;
	struct file *file;

	err = path_lookup_open(AT_FDCWD, name, LOOKUP_FOLLOW, &nd, FMODE_READ|FMODE_EXEC);
	file = ERR_PTR(err);

	if (!err) {
		struct inode *inode = nd.dentry->d_inode;
		file = ERR_PTR(-EACCES);
		if (S_ISREG(inode->i_mode)) {
			int err = vfs_permission(&nd, MAY_EXEC);
			file = ERR_PTR(err);
			if (!err) {
				file = nameidata_to_filp(&nd, O_RDONLY);
				if (!IS_ERR(file)) {
					err = deny_write_access(file);
					if (err) {
						fput(file);
						file = ERR_PTR(err);
					}
				}
out:
				return file;
			}
		}
		release_open_intent(&nd);
		path_release(&nd);
	}
	goto out;
}

EXPORT_SYMBOL(open_exec);

int kernel_read(struct file *file, unsigned long offset,
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
		/*
		 * Make sure that if there is a core dump in progress
		 * for the old mm, we get out and die instead of going
		 * through with the exec.  We must hold mmap_sem around
		 * checking core_waiters and changing tsk->mm.  The
		 * core-inducing thread will increment core_waiters for
		 * each thread whose ->mm == old_mm.
		 */
		down_read(&old_mm->mmap_sem);
		if (unlikely(old_mm->core_waiters)) {
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
	struct task_struct *leader = NULL;
	int count;

	if (thread_group_empty(tsk))
		goto no_thread_group;

	/*
	 * Kill all other threads in the thread group.
	 * We must hold tasklist_lock to call zap_other_threads.
	 */
	read_lock(&tasklist_lock);
	spin_lock_irq(lock);
	if (sig->flags & SIGNAL_GROUP_EXIT) {
		/*
		 * Another group action in progress, just
		 * return so that the signal is processed.
		 */
		spin_unlock_irq(lock);
		read_unlock(&tasklist_lock);
		return -EAGAIN;
	}

	/*
	 * child_reaper ignores SIGKILL, change it now.
	 * Reparenting needs write_lock on tasklist_lock,
	 * so it is safe to do it under read_lock.
	 */
	if (unlikely(tsk->group_leader == child_reaper(tsk)))
		tsk->nsproxy->pid_ns->child_reaper = tsk;

	zap_other_threads(tsk);
	read_unlock(&tasklist_lock);

	/*
	 * Account for the thread group leader hanging around:
	 */
	count = 1;
	if (!thread_group_leader(tsk)) {
		count = 2;
		/*
		 * The SIGALRM timer survives the exec, but needs to point
		 * at us as the new group leader now.  We have a race with
		 * a timer firing now getting the old leader, so we need to
		 * synchronize with any firing (by calling del_timer_sync)
		 * before we can safely let the old group leader die.
		 */
		sig->tsk = tsk;
		spin_unlock_irq(lock);
		if (hrtimer_cancel(&sig->real_timer))
			hrtimer_restart(&sig->real_timer);
		spin_lock_irq(lock);
	}

	sig->notify_count = count;
	sig->group_exit_task = tsk;
	while (atomic_read(&sig->count) > count) {
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
		leader = tsk->group_leader;

		sig->notify_count = -1;
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

		BUG_ON(leader->tgid != tsk->tgid);
		BUG_ON(tsk->pid == tsk->tgid);
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
		attach_pid(tsk, PIDTYPE_PID,  find_pid(tsk->pid));
		transfer_pid(leader, tsk, PIDTYPE_PGID);
		transfer_pid(leader, tsk, PIDTYPE_SID);
		list_replace_rcu(&leader->tasks, &tsk->tasks);

		tsk->group_leader = tsk;
		leader->group_leader = tsk;

		tsk->exit_signal = SIGCHLD;

		BUG_ON(leader->exit_state != EXIT_ZOMBIE);
		leader->exit_state = EXIT_DEAD;

		write_unlock_irq(&tasklist_lock);
        }

	sig->group_exit_task = NULL;
	sig->notify_count = 0;
	/*
	 * There may be one thread left which is just exiting,
	 * but it's safe to stop telling the group to kill themselves.
	 */
	sig->flags = 0;

no_thread_group:
	exit_itimers(sig);
	if (leader)
		release_task(leader);

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
		i = j * __NFDBITS;
		fdt = files_fdtable(files);
		if (i >= fdt->max_fds)
			break;
		set = fdt->close_on_exec->fds_bits[j];
		if (!set)
			continue;
		fdt->close_on_exec->fds_bits[j] = 0;
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

void get_task_comm(char *buf, struct task_struct *tsk)
{
	/* buf must be at least sizeof(tsk->comm) in size */
	task_lock(tsk);
	strncpy(buf, tsk->comm, sizeof(tsk->comm));
	task_unlock(tsk);
}

void set_task_comm(struct task_struct *tsk, char *buf)
{
	task_lock(tsk);
	strlcpy(tsk->comm, buf, sizeof(tsk->comm));
	task_unlock(tsk);
}

int flush_old_exec(struct linux_binprm * bprm)
{
	char * name;
	int i, ch, retval;
	struct files_struct *files;
	char tcomm[sizeof(current->comm)];

	/*
	 * Make sure we have a private signal table and that
	 * we are unassociated from the previous thread group.
	 */
	retval = de_thread(current);
	if (retval)
		goto out;

	/*
	 * Make sure we have private file handles. Ask the
	 * fork helper to do the work for us and the exit
	 * helper to do the cleanup of the old one.
	 */
	files = current->files;		/* refcounted so safe to hold */
	retval = unshare_files();
	if (retval)
		goto out;
	/*
	 * Release all of the old mmap stuff
	 */
	retval = exec_mmap(bprm->mm);
	if (retval)
		goto mmap_failed;

	bprm->mm = NULL;		/* We're using it now */

	/* This is the point of no return */
	put_files_struct(files);

	current->sas_ss_sp = current->sas_ss_size = 0;

	if (current->euid == current->uid && current->egid == current->gid)
		set_dumpable(current->mm, 1);
	else
		set_dumpable(current->mm, suid_dumpable);

	name = bprm->filename;

	/* Copies the binary name from after last slash */
	for (i=0; (ch = *(name++)) != '\0';) {
		if (ch == '/')
			i = 0; /* overwrite what we wrote */
		else
			if (i < (sizeof(tcomm) - 1))
				tcomm[i++] = ch;
	}
	tcomm[i] = '\0';
	set_task_comm(current, tcomm);

	current->flags &= ~PF_RANDOMIZE;
	flush_thread();

	/* Set the new mm task size. We have to do that late because it may
	 * depend on TIF_32BIT which is only updated in flush_thread() on
	 * some architectures like powerpc
	 */
	current->mm->task_size = TASK_SIZE;

	if (bprm->e_uid != current->euid || bprm->e_gid != current->egid) {
		suid_keys(current);
		set_dumpable(current->mm, suid_dumpable);
		current->pdeath_signal = 0;
	} else if (file_permission(bprm->file, MAY_READ) ||
			(bprm->interp_flags & BINPRM_FLAGS_ENFORCE_NONDUMP)) {
		suid_keys(current);
		set_dumpable(current->mm, suid_dumpable);
	}

	/* An exec changes our domain. We are no longer part of the thread
	   group */

	current->self_exec_id++;
			
	flush_signal_handlers(current, 0);
	flush_old_files(current->files);

	return 0;

mmap_failed:
	reset_files_struct(current, files);
out:
	return retval;
}

EXPORT_SYMBOL(flush_old_exec);

/* 
 * Fill the binprm structure from the inode. 
 * Check permissions, then read the first 128 (BINPRM_BUF_SIZE) bytes
 */
int prepare_binprm(struct linux_binprm *bprm)
{
	int mode;
	struct inode * inode = bprm->file->f_path.dentry->d_inode;
	int retval;

	mode = inode->i_mode;
	if (bprm->file->f_op == NULL)
		return -EACCES;

	bprm->e_uid = current->euid;
	bprm->e_gid = current->egid;

	if(!(bprm->file->f_path.mnt->mnt_flags & MNT_NOSUID)) {
		/* Set-uid? */
		if (mode & S_ISUID) {
			current->personality &= ~PER_CLEAR_ON_SETID;
			bprm->e_uid = inode->i_uid;
		}

		/* Set-gid? */
		/*
		 * If setgid is set but no group execute bit then this
		 * is a candidate for mandatory locking, not a setgid
		 * executable.
		 */
		if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
			current->personality &= ~PER_CLEAR_ON_SETID;
			bprm->e_gid = inode->i_gid;
		}
	}

	/* fill in binprm security blob */
	retval = security_bprm_set(bprm);
	if (retval)
		return retval;

	memset(bprm->buf,0,BINPRM_BUF_SIZE);
	return kernel_read(bprm->file,0,bprm->buf,BINPRM_BUF_SIZE);
}

EXPORT_SYMBOL(prepare_binprm);

static int unsafe_exec(struct task_struct *p)
{
	int unsafe = 0;
	if (p->ptrace & PT_PTRACED) {
		if (p->ptrace & PT_PTRACE_CAP)
			unsafe |= LSM_UNSAFE_PTRACE_CAP;
		else
			unsafe |= LSM_UNSAFE_PTRACE;
	}
	if (atomic_read(&p->fs->count) > 1 ||
	    atomic_read(&p->files->count) > 1 ||
	    atomic_read(&p->sighand->count) > 1)
		unsafe |= LSM_UNSAFE_SHARE;

	return unsafe;
}

void compute_creds(struct linux_binprm *bprm)
{
	int unsafe;

	if (bprm->e_uid != current->uid) {
		suid_keys(current);
		current->pdeath_signal = 0;
	}
	exec_keys(current);

	task_lock(current);
	unsafe = unsafe_exec(current);
	security_bprm_apply_creds(bprm, unsafe);
	task_unlock(current);
	security_bprm_post_apply_creds(bprm);
}
EXPORT_SYMBOL(compute_creds);

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
		kaddr = kmap_atomic(page, KM_USER0);

		for (; offset < PAGE_SIZE && kaddr[offset];
				offset++, bprm->p++)
			;

		kunmap_atomic(kaddr, KM_USER0);
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
	int try,retval;
	struct linux_binfmt *fmt;
#ifdef __alpha__
	/* handle /sbin/loader.. */
	{
	    struct exec * eh = (struct exec *) bprm->buf;

	    if (!bprm->loader && eh->fh.f_magic == 0x183 &&
		(eh->fh.f_flags & 0x3000) == 0x3000)
	    {
		struct file * file;
		unsigned long loader;

		allow_write_access(bprm->file);
		fput(bprm->file);
		bprm->file = NULL;

		loader = bprm->vma->vm_end - sizeof(void *);

		file = open_exec("/sbin/loader");
		retval = PTR_ERR(file);
		if (IS_ERR(file))
			return retval;

		/* Remember if the application is TASO.  */
		bprm->sh_bang = eh->ah.entry < 0x100000000UL;

		bprm->file = file;
		bprm->loader = loader;
		retval = prepare_binprm(bprm);
		if (retval<0)
			return retval;
		/* should call search_binary_handler recursively here,
		   but it does not matter */
	    }
	}
#endif
	retval = security_bprm_check(bprm);
	if (retval)
		return retval;

	/* kernel module loader fixup */
	/* so we don't try to load run modprobe in kernel space. */
	set_fs(USER_DS);

	retval = audit_bprm(bprm);
	if (retval)
		return retval;

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
			retval = fn(bprm, regs);
			if (retval >= 0) {
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
		if (retval != -ENOEXEC || bprm->mm == NULL) {
			break;
#ifdef CONFIG_KMOD
		}else{
#define printable(c) (((c)=='\t') || ((c)=='\n') || (0x20<=(c) && (c)<=0x7e))
			if (printable(bprm->buf[0]) &&
			    printable(bprm->buf[1]) &&
			    printable(bprm->buf[2]) &&
			    printable(bprm->buf[3]))
				break; /* -ENOEXEC */
			request_module("binfmt-%04x", *(unsigned short *)(&bprm->buf[2]));
#endif
		}
	}
	return retval;
}

EXPORT_SYMBOL(search_binary_handler);

/*
 * sys_execve() executes a new program.
 */
int do_execve(char * filename,
	char __user *__user *argv,
	char __user *__user *envp,
	struct pt_regs * regs)
{
	struct linux_binprm *bprm;
	struct file *file;
	unsigned long env_p;
	int retval;

	retval = -ENOMEM;
	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	if (!bprm)
		goto out_ret;

	file = open_exec(filename);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_kfree;

	sched_exec();

	bprm->file = file;
	bprm->filename = filename;
	bprm->interp = filename;

	retval = bprm_mm_init(bprm);
	if (retval)
		goto out_file;

	bprm->argc = count(argv, MAX_ARG_STRINGS);
	if ((retval = bprm->argc) < 0)
		goto out_mm;

	bprm->envc = count(envp, MAX_ARG_STRINGS);
	if ((retval = bprm->envc) < 0)
		goto out_mm;

	retval = security_bprm_alloc(bprm);
	if (retval)
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

	env_p = bprm->p;
	retval = copy_strings(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out;
	bprm->argv_len = env_p - bprm->p;

	retval = search_binary_handler(bprm,regs);
	if (retval >= 0) {
		/* execve success */
		free_arg_pages(bprm);
		security_bprm_free(bprm);
		acct_update_integrals(current);
		kfree(bprm);
		return retval;
	}

out:
	free_arg_pages(bprm);
	if (bprm->security)
		security_bprm_free(bprm);

out_mm:
	if (bprm->mm)
		mmput (bprm->mm);

out_file:
	if (bprm->file) {
		allow_write_access(bprm->file);
		fput(bprm->file);
	}
out_kfree:
	kfree(bprm);

out_ret:
	return retval;
}

int set_binfmt(struct linux_binfmt *new)
{
	struct linux_binfmt *old = current->binfmt;

	if (new) {
		if (!try_module_get(new->module))
			return -1;
	}
	current->binfmt = new;
	if (old)
		module_put(old->module);
	return 0;
}

EXPORT_SYMBOL(set_binfmt);

/* format_corename will inspect the pattern parameter, and output a
 * name into corename, which must have space for at least
 * CORENAME_MAX_SIZE bytes plus one byte for the zero terminator.
 */
static int format_corename(char *corename, const char *pattern, long signr)
{
	const char *pat_ptr = pattern;
	char *out_ptr = corename;
	char *const out_end = corename + CORENAME_MAX_SIZE;
	int rc;
	int pid_in_pattern = 0;
	int ispipe = 0;

	if (*pattern == '|')
		ispipe = 1;

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		if (*pat_ptr != '%') {
			if (out_ptr == out_end)
				goto out;
			*out_ptr++ = *pat_ptr++;
		} else {
			switch (*++pat_ptr) {
			case 0:
				goto out;
			/* Double percent, output one percent */
			case '%':
				if (out_ptr == out_end)
					goto out;
				*out_ptr++ = '%';
				break;
			/* pid */
			case 'p':
				pid_in_pattern = 1;
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%d", current->tgid);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* uid */
			case 'u':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%d", current->uid);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* gid */
			case 'g':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%d", current->gid);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* signal that caused the coredump */
			case 's':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%ld", signr);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* UNIX time of coredump */
			case 't': {
				struct timeval tv;
				do_gettimeofday(&tv);
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%lu", tv.tv_sec);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			}
			/* hostname */
			case 'h':
				down_read(&uts_sem);
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%s", utsname()->nodename);
				up_read(&uts_sem);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* executable */
			case 'e':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%s", current->comm);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			/* core limit size */
			case 'c':
				rc = snprintf(out_ptr, out_end - out_ptr,
					      "%lu", current->signal->rlim[RLIMIT_CORE].rlim_cur);
				if (rc > out_end - out_ptr)
					goto out;
				out_ptr += rc;
				break;
			default:
				break;
			}
			++pat_ptr;
		}
	}
	/* Backward compatibility with core_uses_pid:
	 *
	 * If core_pattern does not include a %p (as is the default)
	 * and core_uses_pid is set, then .%pid will be appended to
	 * the filename. Do not do this for piped commands. */
	if (!ispipe && !pid_in_pattern
            && (core_uses_pid || atomic_read(&current->mm->mm_users) != 1)) {
		rc = snprintf(out_ptr, out_end - out_ptr,
			      ".%d", current->tgid);
		if (rc > out_end - out_ptr)
			goto out;
		out_ptr += rc;
	}
out:
	*out_ptr = 0;
	return ispipe;
}

static void zap_process(struct task_struct *start)
{
	struct task_struct *t;

	start->signal->flags = SIGNAL_GROUP_EXIT;
	start->signal->group_stop_count = 0;

	t = start;
	do {
		if (t != current && t->mm) {
			t->mm->core_waiters++;
			sigaddset(&t->pending.signal, SIGKILL);
			signal_wake_up(t, 1);
		}
	} while ((t = next_thread(t)) != start);
}

static inline int zap_threads(struct task_struct *tsk, struct mm_struct *mm,
				int exit_code)
{
	struct task_struct *g, *p;
	unsigned long flags;
	int err = -EAGAIN;

	spin_lock_irq(&tsk->sighand->siglock);
	if (!(tsk->signal->flags & SIGNAL_GROUP_EXIT)) {
		tsk->signal->group_exit_code = exit_code;
		zap_process(tsk);
		err = 0;
	}
	spin_unlock_irq(&tsk->sighand->siglock);
	if (err)
		return err;

	if (atomic_read(&mm->mm_users) == mm->core_waiters + 1)
		goto done;

	rcu_read_lock();
	for_each_process(g) {
		if (g == tsk->group_leader)
			continue;

		p = g;
		do {
			if (p->mm) {
				if (p->mm == mm) {
					/*
					 * p->sighand can't disappear, but
					 * may be changed by de_thread()
					 */
					lock_task_sighand(p, &flags);
					zap_process(p);
					unlock_task_sighand(p, &flags);
				}
				break;
			}
		} while ((p = next_thread(p)) != g);
	}
	rcu_read_unlock();
done:
	return mm->core_waiters;
}

static int coredump_wait(int exit_code)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	struct completion startup_done;
	struct completion *vfork_done;
	int core_waiters;

	init_completion(&mm->core_done);
	init_completion(&startup_done);
	mm->core_startup_done = &startup_done;

	core_waiters = zap_threads(tsk, mm, exit_code);
	up_write(&mm->mmap_sem);

	if (unlikely(core_waiters < 0))
		goto fail;

	/*
	 * Make sure nobody is waiting for us to release the VM,
	 * otherwise we can deadlock when we wait on each other
	 */
	vfork_done = tsk->vfork_done;
	if (vfork_done) {
		tsk->vfork_done = NULL;
		complete(vfork_done);
	}

	if (core_waiters)
		wait_for_completion(&startup_done);
fail:
	BUG_ON(mm->core_waiters);
	return core_waiters;
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
EXPORT_SYMBOL_GPL(set_dumpable);

int get_dumpable(struct mm_struct *mm)
{
	int ret;

	ret = mm->flags & 0x3;
	return (ret >= 2) ? 2 : ret;
}

int do_coredump(long signr, int exit_code, struct pt_regs * regs)
{
	char corename[CORENAME_MAX_SIZE + 1];
	struct mm_struct *mm = current->mm;
	struct linux_binfmt * binfmt;
	struct inode * inode;
	struct file * file;
	int retval = 0;
	int fsuid = current->fsuid;
	int flag = 0;
	int ispipe = 0;
	unsigned long core_limit = current->signal->rlim[RLIMIT_CORE].rlim_cur;
	char **helper_argv = NULL;
	int helper_argc = 0;
	char *delimit;

	audit_core_dumps(signr);

	binfmt = current->binfmt;
	if (!binfmt || !binfmt->core_dump)
		goto fail;
	down_write(&mm->mmap_sem);
	if (!get_dumpable(mm)) {
		up_write(&mm->mmap_sem);
		goto fail;
	}

	/*
	 *	We cannot trust fsuid as being the "true" uid of the
	 *	process nor do we know its entire history. We only know it
	 *	was tainted so we dump it as root in mode 2.
	 */
	if (get_dumpable(mm) == 2) {	/* Setuid core dump mode */
		flag = O_EXCL;		/* Stop rewrite attacks */
		current->fsuid = 0;	/* Dump root private */
	}
	set_dumpable(mm, 0);

	retval = coredump_wait(exit_code);
	if (retval < 0)
		goto fail;

	/*
	 * Clear any false indication of pending signals that might
	 * be seen by the filesystem code called to write the core file.
	 */
	clear_thread_flag(TIF_SIGPENDING);

	/*
	 * lock_kernel() because format_corename() is controlled by sysctl, which
	 * uses lock_kernel()
	 */
 	lock_kernel();
	ispipe = format_corename(corename, core_pattern, signr);
	unlock_kernel();
	/*
	 * Don't bother to check the RLIMIT_CORE value if core_pattern points
	 * to a pipe.  Since we're not writing directly to the filesystem
	 * RLIMIT_CORE doesn't really apply, as no actual core file will be
	 * created unless the pipe reader choses to write out the core file
	 * at which point file size limits and permissions will be imposed
	 * as it does with any other process
	 */
	if ((!ispipe) && (core_limit < binfmt->min_coredump))
		goto fail_unlock;

 	if (ispipe) {
		helper_argv = argv_split(GFP_KERNEL, corename+1, &helper_argc);
		/* Terminate the string before the first option */
		delimit = strchr(corename, ' ');
		if (delimit)
			*delimit = '\0';
		delimit = strrchr(helper_argv[0], '/');
		if (delimit)
			delimit++;
		else
			delimit = helper_argv[0];
		if (!strcmp(delimit, current->comm)) {
			printk(KERN_NOTICE "Recursive core dump detected, "
					"aborting\n");
			goto fail_unlock;
		}

		core_limit = RLIM_INFINITY;

		/* SIGPIPE can happen, but it's just never processed */
 		if (call_usermodehelper_pipe(corename+1, helper_argv, NULL,
				&file)) {
 			printk(KERN_INFO "Core dump to %s pipe failed\n",
			       corename);
 			goto fail_unlock;
 		}
 	} else
 		file = filp_open(corename,
				 O_CREAT | 2 | O_NOFOLLOW | O_LARGEFILE | flag,
				 0600);
	if (IS_ERR(file))
		goto fail_unlock;
	inode = file->f_path.dentry->d_inode;
	if (inode->i_nlink > 1)
		goto close_fail;	/* multiple links - don't dump */
	if (!ispipe && d_unhashed(file->f_path.dentry))
		goto close_fail;

	/* AK: actually i see no reason to not allow this for named pipes etc.,
	   but keep the previous behaviour for now. */
	if (!ispipe && !S_ISREG(inode->i_mode))
		goto close_fail;
	if (!file->f_op)
		goto close_fail;
	if (!file->f_op->write)
		goto close_fail;
	if (!ispipe && do_truncate(file->f_path.dentry, 0, 0, file) != 0)
		goto close_fail;

	retval = binfmt->core_dump(signr, regs, file, core_limit);

	if (retval)
		current->signal->group_exit_code |= 0x80;
close_fail:
	filp_close(file, NULL);
fail_unlock:
	if (helper_argv)
		argv_free(helper_argv);

	current->fsuid = fsuid;
	complete_all(&mm->core_done);
fail:
	return retval;
}
