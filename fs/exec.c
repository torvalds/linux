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
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/key.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/swap.h>
#include <linux/utsname.h>
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

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

int core_uses_pid;
char core_pattern[128] = "core";
int suid_dumpable = 0;

EXPORT_SYMBOL(suid_dumpable);
/* The maximal length of core_pattern is also specified in sysctl.c */

static struct linux_binfmt *formats;
static DEFINE_RWLOCK(binfmt_lock);

int register_binfmt(struct linux_binfmt * fmt)
{
	struct linux_binfmt ** tmp = &formats;

	if (!fmt)
		return -EINVAL;
	if (fmt->next)
		return -EBUSY;
	write_lock(&binfmt_lock);
	while (*tmp) {
		if (fmt == *tmp) {
			write_unlock(&binfmt_lock);
			return -EBUSY;
		}
		tmp = &(*tmp)->next;
	}
	fmt->next = formats;
	formats = fmt;
	write_unlock(&binfmt_lock);
	return 0;	
}

EXPORT_SYMBOL(register_binfmt);

int unregister_binfmt(struct linux_binfmt * fmt)
{
	struct linux_binfmt ** tmp = &formats;

	write_lock(&binfmt_lock);
	while (*tmp) {
		if (fmt == *tmp) {
			*tmp = fmt->next;
			write_unlock(&binfmt_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	write_unlock(&binfmt_lock);
	return -EINVAL;
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
		for (fmt = formats ; fmt ; fmt = fmt->next) {
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
 * 'copy_strings()' copies argument/environment strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 */
static int copy_strings(int argc, char __user * __user * argv,
			struct linux_binprm *bprm)
{
	struct page *kmapped_page = NULL;
	char *kaddr = NULL;
	int ret;

	while (argc-- > 0) {
		char __user *str;
		int len;
		unsigned long pos;

		if (get_user(str, argv+argc) ||
				!(len = strnlen_user(str, bprm->p))) {
			ret = -EFAULT;
			goto out;
		}

		if (bprm->p < len)  {
			ret = -E2BIG;
			goto out;
		}

		bprm->p -= len;
		/* XXX: add architecture specific overflow check here. */
		pos = bprm->p;

		while (len > 0) {
			int i, new, err;
			int offset, bytes_to_copy;
			struct page *page;

			offset = pos % PAGE_SIZE;
			i = pos/PAGE_SIZE;
			page = bprm->page[i];
			new = 0;
			if (!page) {
				page = alloc_page(GFP_HIGHUSER);
				bprm->page[i] = page;
				if (!page) {
					ret = -ENOMEM;
					goto out;
				}
				new = 1;
			}

			if (page != kmapped_page) {
				if (kmapped_page)
					kunmap(kmapped_page);
				kmapped_page = page;
				kaddr = kmap(kmapped_page);
			}
			if (new && offset)
				memset(kaddr, 0, offset);
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len) {
				bytes_to_copy = len;
				if (new)
					memset(kaddr+offset+len, 0,
						PAGE_SIZE-offset-len);
			}
			err = copy_from_user(kaddr+offset, str, bytes_to_copy);
			if (err) {
				ret = -EFAULT;
				goto out;
			}

			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	ret = 0;
out:
	if (kmapped_page)
		kunmap(kmapped_page);
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
 * This routine is used to map in a page into an address space: needed by
 * execve() for the initial stack and environment pages.
 *
 * vma->vm_mm->mmap_sem is held for writing.
 */
void install_arg_page(struct vm_area_struct *vma,
			struct page *page, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t * pte;
	spinlock_t *ptl;

	if (unlikely(anon_vma_prepare(vma)))
		goto out;

	flush_dcache_page(page);
	pte = get_locked_pte(mm, address, &ptl);
	if (!pte)
		goto out;
	if (!pte_none(*pte)) {
		pte_unmap_unlock(pte, ptl);
		goto out;
	}
	inc_mm_counter(mm, anon_rss);
	lru_cache_add_active(page);
	set_pte_at(mm, address, pte, pte_mkdirty(pte_mkwrite(mk_pte(
					page, vma->vm_page_prot))));
	page_add_new_anon_rmap(page, vma, address);
	pte_unmap_unlock(pte, ptl);

	/* no need for flush_tlb */
	return;
out:
	__free_page(page);
	force_sig(SIGKILL, current);
}

#define EXTRA_STACK_VM_PAGES	20	/* random */

int setup_arg_pages(struct linux_binprm *bprm,
		    unsigned long stack_top,
		    int executable_stack)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	struct mm_struct *mm = current->mm;
	int i, ret;
	long arg_size;

#ifdef CONFIG_STACK_GROWSUP
	/* Move the argument and environment strings to the bottom of the
	 * stack space.
	 */
	int offset, j;
	char *to, *from;

	/* Start by shifting all the pages down */
	i = 0;
	for (j = 0; j < MAX_ARG_PAGES; j++) {
		struct page *page = bprm->page[j];
		if (!page)
			continue;
		bprm->page[i++] = page;
	}

	/* Now move them within their pages */
	offset = bprm->p % PAGE_SIZE;
	to = kmap(bprm->page[0]);
	for (j = 1; j < i; j++) {
		memmove(to, to + offset, PAGE_SIZE - offset);
		from = kmap(bprm->page[j]);
		memcpy(to + PAGE_SIZE - offset, from, offset);
		kunmap(bprm->page[j - 1]);
		to = from;
	}
	memmove(to, to + offset, PAGE_SIZE - offset);
	kunmap(bprm->page[j - 1]);

	/* Limit stack size to 1GB */
	stack_base = current->signal->rlim[RLIMIT_STACK].rlim_max;
	if (stack_base > (1 << 30))
		stack_base = 1 << 30;
	stack_base = PAGE_ALIGN(stack_top - stack_base);

	/* Adjust bprm->p to point to the end of the strings. */
	bprm->p = stack_base + PAGE_SIZE * i - offset;

	mm->arg_start = stack_base;
	arg_size = i << PAGE_SHIFT;

	/* zero pages that were copied above */
	while (i < MAX_ARG_PAGES)
		bprm->page[i++] = NULL;
#else
	stack_base = arch_align_stack(stack_top - MAX_ARG_PAGES*PAGE_SIZE);
	stack_base = PAGE_ALIGN(stack_base);
	bprm->p += stack_base;
	mm->arg_start = bprm->p;
	arg_size = stack_top - (PAGE_MASK & (unsigned long) mm->arg_start);
#endif

	arg_size += EXTRA_STACK_VM_PAGES * PAGE_SIZE;

	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!mpnt)
		return -ENOMEM;

	memset(mpnt, 0, sizeof(*mpnt));

	down_write(&mm->mmap_sem);
	{
		mpnt->vm_mm = mm;
#ifdef CONFIG_STACK_GROWSUP
		mpnt->vm_start = stack_base;
		mpnt->vm_end = stack_base + arg_size;
#else
		mpnt->vm_end = stack_top;
		mpnt->vm_start = mpnt->vm_end - arg_size;
#endif
		/* Adjust stack execute permissions; explicitly enable
		 * for EXSTACK_ENABLE_X, disable for EXSTACK_DISABLE_X
		 * and leave alone (arch default) otherwise. */
		if (unlikely(executable_stack == EXSTACK_ENABLE_X))
			mpnt->vm_flags = VM_STACK_FLAGS |  VM_EXEC;
		else if (executable_stack == EXSTACK_DISABLE_X)
			mpnt->vm_flags = VM_STACK_FLAGS & ~VM_EXEC;
		else
			mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_flags |= mm->def_flags;
		mpnt->vm_page_prot = protection_map[mpnt->vm_flags & 0x7];
		if ((ret = insert_vm_struct(mm, mpnt))) {
			up_write(&mm->mmap_sem);
			kmem_cache_free(vm_area_cachep, mpnt);
			return ret;
		}
		mm->stack_vm = mm->total_vm = vma_pages(mpnt);
	}

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page *page = bprm->page[i];
		if (page) {
			bprm->page[i] = NULL;
			install_arg_page(mpnt, page, stack_base);
		}
		stack_base += PAGE_SIZE;
	}
	up_write(&mm->mmap_sem);
	
	return 0;
}

EXPORT_SYMBOL(setup_arg_pages);

#define free_arg_pages(bprm) do { } while (0)

#else

static inline void free_arg_pages(struct linux_binprm *bprm)
{
	int i;

	for (i = 0; i < MAX_ARG_PAGES; i++) {
		if (bprm->page[i])
			__free_page(bprm->page[i]);
		bprm->page[i] = NULL;
	}
}

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
		if (!(nd.mnt->mnt_flags & MNT_NOEXEC) &&
		    S_ISREG(inode->i_mode)) {
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
	struct sighand_struct *newsighand, *oldsighand = tsk->sighand;
	spinlock_t *lock = &oldsighand->siglock;
	struct task_struct *leader = NULL;
	int count;

	/*
	 * If we don't share sighandlers, then we aren't sharing anything
	 * and we can just re-use it all.
	 */
	if (atomic_read(&oldsighand->count) <= 1) {
		BUG_ON(atomic_read(&sig->count) != 1);
		exit_itimers(sig);
		return 0;
	}

	newsighand = kmem_cache_alloc(sighand_cachep, GFP_KERNEL);
	if (!newsighand)
		return -ENOMEM;

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
		kmem_cache_free(sighand_cachep, newsighand);
		return -EAGAIN;
	}

	/*
	 * child_reaper ignores SIGKILL, change it now.
	 * Reparenting needs write_lock on tasklist_lock,
	 * so it is safe to do it under read_lock.
	 */
	if (unlikely(tsk->group_leader == child_reaper))
		child_reaper = tsk;

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
	while (atomic_read(&sig->count) > count) {
		sig->group_exit_task = tsk;
		sig->notify_count = count;
		__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(lock);
		schedule();
		spin_lock_irq(lock);
	}
	sig->group_exit_task = NULL;
	sig->notify_count = 0;
	spin_unlock_irq(lock);

	/*
	 * At this point all other threads have exited, all we have to
	 * do is to wait for the thread group leader to become inactive,
	 * and to assume its PID:
	 */
	if (!thread_group_leader(tsk)) {
		/*
		 * Wait for the thread group leader to be a zombie.
		 * It should already be zombie at this point, most
		 * of the time.
		 */
		leader = tsk->group_leader;
		while (leader->exit_state != EXIT_ZOMBIE)
			yield();

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

		write_lock_irq(&tasklist_lock);

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
		attach_pid(tsk, PIDTYPE_PID,  tsk->pid);
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

	/*
	 * There may be one thread left which is just exiting,
	 * but it's safe to stop telling the group to kill themselves.
	 */
	sig->flags = 0;

no_thread_group:
	exit_itimers(sig);
	if (leader)
		release_task(leader);

	BUG_ON(atomic_read(&sig->count) != 1);

	if (atomic_read(&oldsighand->count) == 1) {
		/*
		 * Now that we nuked the rest of the thread group,
		 * it turns out we are not sharing sighand any more either.
		 * So we can just keep it.
		 */
		kmem_cache_free(sighand_cachep, newsighand);
	} else {
		/*
		 * Move our state over to newsighand and switch it in.
		 */
		atomic_set(&newsighand->count, 1);
		memcpy(newsighand->action, oldsighand->action,
		       sizeof(newsighand->action));

		write_lock_irq(&tasklist_lock);
		spin_lock(&oldsighand->siglock);
		spin_lock_nested(&newsighand->siglock, SINGLE_DEPTH_NESTING);

		rcu_assign_pointer(tsk->sighand, newsighand);
		recalc_sigpending();

		spin_unlock(&newsighand->siglock);
		spin_unlock(&oldsighand->siglock);
		write_unlock_irq(&tasklist_lock);

		if (atomic_dec_and_test(&oldsighand->count))
			kmem_cache_free(sighand_cachep, oldsighand);
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
		if (i >= fdt->max_fds || i >= fdt->max_fdset)
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
		current->mm->dumpable = 1;
	else
		current->mm->dumpable = suid_dumpable;

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

	if (bprm->e_uid != current->euid || bprm->e_gid != current->egid || 
	    file_permission(bprm->file, MAY_READ) ||
	    (bprm->interp_flags & BINPRM_FLAGS_ENFORCE_NONDUMP)) {
		suid_keys(current);
		current->mm->dumpable = suid_dumpable;
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
	struct inode * inode = bprm->file->f_dentry->d_inode;
	int retval;

	mode = inode->i_mode;
	if (bprm->file->f_op == NULL)
		return -EACCES;

	bprm->e_uid = current->euid;
	bprm->e_gid = current->egid;

	if(!(bprm->file->f_vfsmnt->mnt_flags & MNT_NOSUID)) {
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

	if (bprm->e_uid != current->uid)
		suid_keys(current);
	exec_keys(current);

	task_lock(current);
	unsafe = unsafe_exec(current);
	security_bprm_apply_creds(bprm, unsafe);
	task_unlock(current);
	security_bprm_post_apply_creds(bprm);
}

EXPORT_SYMBOL(compute_creds);

void remove_arg_zero(struct linux_binprm *bprm)
{
	if (bprm->argc) {
		unsigned long offset;
		char * kaddr;
		struct page *page;

		offset = bprm->p % PAGE_SIZE;
		goto inside;

		while (bprm->p++, *(kaddr+offset++)) {
			if (offset != PAGE_SIZE)
				continue;
			offset = 0;
			kunmap_atomic(kaddr, KM_USER0);
inside:
			page = bprm->page[bprm->p/PAGE_SIZE];
			kaddr = kmap_atomic(page, KM_USER0);
		}
		kunmap_atomic(kaddr, KM_USER0);
		bprm->argc--;
	}
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

	        loader = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);

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
		for (fmt = formats ; fmt ; fmt = fmt->next) {
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
	int retval;
	int i;

	retval = -ENOMEM;
	bprm = kzalloc(sizeof(*bprm), GFP_KERNEL);
	if (!bprm)
		goto out_ret;

	file = open_exec(filename);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto out_kfree;

	sched_exec();

	bprm->p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);

	bprm->file = file;
	bprm->filename = filename;
	bprm->interp = filename;
	bprm->mm = mm_alloc();
	retval = -ENOMEM;
	if (!bprm->mm)
		goto out_file;

	retval = init_new_context(current, bprm->mm);
	if (retval < 0)
		goto out_mm;

	bprm->argc = count(argv, bprm->p / sizeof(void *));
	if ((retval = bprm->argc) < 0)
		goto out_mm;

	bprm->envc = count(envp, bprm->p / sizeof(void *));
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

	retval = copy_strings(bprm->argc, argv, bprm);
	if (retval < 0)
		goto out;

	retval = search_binary_handler(bprm,regs);
	if (retval >= 0) {
		free_arg_pages(bprm);

		/* execve success */
		security_bprm_free(bprm);
		acct_update_integrals(current);
		kfree(bprm);
		return retval;
	}

out:
	/* Something went wrong, return the inode and free the argument pages*/
	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		struct page * page = bprm->page[i];
		if (page)
			__free_page(page);
	}

	if (bprm->security)
		security_bprm_free(bprm);

out_mm:
	if (bprm->mm)
		mmdrop(bprm->mm);

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

#define CORENAME_MAX_SIZE 64

/* format_corename will inspect the pattern parameter, and output a
 * name into corename, which must have space for at least
 * CORENAME_MAX_SIZE bytes plus one byte for the zero terminator.
 */
static void format_corename(char *corename, const char *pattern, long signr)
{
	const char *pat_ptr = pattern;
	char *out_ptr = corename;
	char *const out_end = corename + CORENAME_MAX_SIZE;
	int rc;
	int pid_in_pattern = 0;

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
	 * the filename */
	if (!pid_in_pattern
            && (core_uses_pid || atomic_read(&current->mm->mm_users) != 1)) {
		rc = snprintf(out_ptr, out_end - out_ptr,
			      ".%d", current->tgid);
		if (rc > out_end - out_ptr)
			goto out;
		out_ptr += rc;
	}
      out:
	*out_ptr = 0;
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

	binfmt = current->binfmt;
	if (!binfmt || !binfmt->core_dump)
		goto fail;
	down_write(&mm->mmap_sem);
	if (!mm->dumpable) {
		up_write(&mm->mmap_sem);
		goto fail;
	}

	/*
	 *	We cannot trust fsuid as being the "true" uid of the
	 *	process nor do we know its entire history. We only know it
	 *	was tainted so we dump it as root in mode 2.
	 */
	if (mm->dumpable == 2) {	/* Setuid core dump mode */
		flag = O_EXCL;		/* Stop rewrite attacks */
		current->fsuid = 0;	/* Dump root private */
	}
	mm->dumpable = 0;

	retval = coredump_wait(exit_code);
	if (retval < 0)
		goto fail;

	/*
	 * Clear any false indication of pending signals that might
	 * be seen by the filesystem code called to write the core file.
	 */
	clear_thread_flag(TIF_SIGPENDING);

	if (current->signal->rlim[RLIMIT_CORE].rlim_cur < binfmt->min_coredump)
		goto fail_unlock;

	/*
	 * lock_kernel() because format_corename() is controlled by sysctl, which
	 * uses lock_kernel()
	 */
 	lock_kernel();
	format_corename(corename, core_pattern, signr);
	unlock_kernel();
 	if (corename[0] == '|') {
		/* SIGPIPE can happen, but it's just never processed */
 		if(call_usermodehelper_pipe(corename+1, NULL, NULL, &file)) {
 			printk(KERN_INFO "Core dump to %s pipe failed\n",
			       corename);
 			goto fail_unlock;
 		}
		ispipe = 1;
 	} else
 		file = filp_open(corename,
				 O_CREAT | 2 | O_NOFOLLOW | O_LARGEFILE | flag,
				 0600);
	if (IS_ERR(file))
		goto fail_unlock;
	inode = file->f_dentry->d_inode;
	if (inode->i_nlink > 1)
		goto close_fail;	/* multiple links - don't dump */
	if (!ispipe && d_unhashed(file->f_dentry))
		goto close_fail;

	/* AK: actually i see no reason to not allow this for named pipes etc.,
	   but keep the previous behaviour for now. */
	if (!ispipe && !S_ISREG(inode->i_mode))
		goto close_fail;
	if (!file->f_op)
		goto close_fail;
	if (!file->f_op->write)
		goto close_fail;
	if (!ispipe && do_truncate(file->f_dentry, 0, 0, file) != 0)
		goto close_fail;

	retval = binfmt->core_dump(signr, regs, file);

	if (retval)
		current->signal->group_exit_code |= 0x80;
close_fail:
	filp_close(file, NULL);
fail_unlock:
	current->fsuid = fsuid;
	complete_all(&mm->core_done);
fail:
	return retval;
}
