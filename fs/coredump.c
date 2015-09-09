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
#include <linux/coredump.h>
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
unsigned int core_pipe_limit;
char core_pattern[CORENAME_MAX_SIZE] = "core";
static int core_name_size = CORENAME_MAX_SIZE;

struct core_name {
	char *corename;
	int used, size;
};

/* The maximal length of core_pattern is also specified in sysctl.c */

static int expand_corename(struct core_name *cn, int size)
{
	char *corename = krealloc(cn->corename, size, GFP_KERNEL);

	if (!corename)
		return -ENOMEM;

	if (size > core_name_size) /* racy but harmless */
		core_name_size = size;

	cn->size = ksize(corename);
	cn->corename = corename;
	return 0;
}

static int cn_vprintf(struct core_name *cn, const char *fmt, va_list arg)
{
	int free, need;
	va_list arg_copy;

again:
	free = cn->size - cn->used;

	va_copy(arg_copy, arg);
	need = vsnprintf(cn->corename + cn->used, free, fmt, arg_copy);
	va_end(arg_copy);

	if (need < free) {
		cn->used += need;
		return 0;
	}

	if (!expand_corename(cn, cn->size + need - free + 1))
		goto again;

	return -ENOMEM;
}

static int cn_printf(struct core_name *cn, const char *fmt, ...)
{
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = cn_vprintf(cn, fmt, arg);
	va_end(arg);

	return ret;
}

static int cn_esc_printf(struct core_name *cn, const char *fmt, ...)
{
	int cur = cn->used;
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = cn_vprintf(cn, fmt, arg);
	va_end(arg);

	for (; cur < cn->used; ++cur) {
		if (cn->corename[cur] == '/')
			cn->corename[cur] = '!';
	}
	return ret;
}

static int cn_print_exe_file(struct core_name *cn)
{
	struct file *exe_file;
	char *pathbuf, *path;
	int ret;

	exe_file = get_mm_exe_file(current->mm);
	if (!exe_file)
		return cn_esc_printf(cn, "%s (path unknown)", current->comm);

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

	ret = cn_esc_printf(cn, "%s", path);

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
static int format_corename(struct core_name *cn, struct coredump_params *cprm)
{
	const struct cred *cred = current_cred();
	const char *pat_ptr = core_pattern;
	int ispipe = (*pat_ptr == '|');
	int pid_in_pattern = 0;
	int err = 0;

	cn->used = 0;
	cn->corename = NULL;
	if (expand_corename(cn, core_name_size))
		return -ENOMEM;
	cn->corename[0] = '\0';

	if (ispipe)
		++pat_ptr;

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		if (*pat_ptr != '%') {
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
			/* global pid */
			case 'P':
				err = cn_printf(cn, "%d",
					      task_tgid_nr(current));
				break;
			case 'i':
				err = cn_printf(cn, "%d",
					      task_pid_vnr(current));
				break;
			case 'I':
				err = cn_printf(cn, "%d",
					      task_pid_nr(current));
				break;
			/* uid */
			case 'u':
				err = cn_printf(cn, "%d", cred->uid);
				break;
			/* gid */
			case 'g':
				err = cn_printf(cn, "%d", cred->gid);
				break;
			case 'd':
				err = cn_printf(cn, "%d",
					__get_dumpable(cprm->mm_flags));
				break;
			/* signal that caused the coredump */
			case 's':
				err = cn_printf(cn, "%ld", cprm->siginfo->si_signo);
				break;
			/* UNIX time of coredump */
			case 't': {
				struct timeval tv;
				do_gettimeofday(&tv);
				err = cn_printf(cn, "%lu", tv.tv_sec);
				break;
			}
			/* hostname */
			case 'h':
				down_read(&uts_sem);
				err = cn_esc_printf(cn, "%s",
					      utsname()->nodename);
				up_read(&uts_sem);
				break;
			/* executable */
			case 'e':
				err = cn_esc_printf(cn, "%s", current->comm);
				break;
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

out:
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
	return ispipe;
}

static int zap_process(struct task_struct *start, int exit_code)
{
	struct task_struct *t;
	int nr = 0;

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

static int zap_threads(struct task_struct *tsk, struct mm_struct *mm,
			struct core_state *core_state, int exit_code)
{
	struct task_struct *g, *p;
	unsigned long flags;
	int nr = -EAGAIN;

	spin_lock_irq(&tsk->sighand->siglock);
	if (!signal_group_exit(tsk->signal)) {
		mm->core_state = core_state;
		nr = zap_process(tsk, exit_code);
		tsk->signal->group_exit_task = tsk;
		/* ignore all signals except SIGKILL, see prepare_signal() */
		tsk->signal->flags = SIGNAL_GROUP_COREDUMP;
		clear_tsk_thread_flag(tsk, TIF_SIGPENDING);
	}
	spin_unlock_irq(&tsk->sighand->siglock);
	if (unlikely(nr < 0))
		return nr;

	tsk->flags |= PF_DUMPCORE;
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
					p->signal->flags = SIGNAL_GROUP_EXIT;
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

	if (core_waiters > 0) {
		struct core_thread *ptr;

		wait_for_completion(&core_state->startup);
		/*
		 * Wait for all the threads to become inactive, so that
		 * all the thread context (extended register state, like
		 * fpu etc) gets copied to the memory.
		 */
		ptr = core_state->dumper.next;
		while (ptr != NULL) {
			wait_task_inactive(ptr->task, 0);
			ptr = ptr->next;
		}
	}

	return core_waiters;
}

static void coredump_finish(struct mm_struct *mm, bool core_dumped)
{
	struct core_thread *curr, *next;
	struct task_struct *task;

	spin_lock_irq(&current->sighand->siglock);
	if (core_dumped && !__fatal_signal_pending(current))
		current->signal->group_exit_code |= 0x80;
	current->signal->group_exit_task = NULL;
	current->signal->flags = SIGNAL_GROUP_EXIT;
	spin_unlock_irq(&current->sighand->siglock);

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

static bool dump_interrupted(void)
{
	/*
	 * SIGKILL or freezing() interrupt the coredumping. Perhaps we
	 * can do try_to_freeze() and check __fatal_signal_pending(),
	 * but then we need to teach dump_write() to restart and clear
	 * TIF_SIGPENDING.
	 */
	return signal_pending(current);
}

static void wait_for_dump_helpers(struct file *file)
{
	struct pipe_inode_info *pipe = file->private_data;

	pipe_lock(pipe);
	pipe->readers++;
	pipe->writers--;
	wake_up_interruptible_sync(&pipe->wait);
	kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	pipe_unlock(pipe);

	/*
	 * We actually want wait_event_freezable() but then we need
	 * to clear TIF_SIGPENDING and improve dump_interrupted().
	 */
	wait_event_interruptible(pipe->wait, pipe->readers == 1);

	pipe_lock(pipe);
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
	struct file *files[2];
	struct coredump_params *cp = (struct coredump_params *)info->data;
	int err = create_pipe_files(files, 0);
	if (err)
		return err;

	cp->file = files[1];

	err = replace_fd(0, files[0], 0);
	fput(files[0]);
	/* and disallow core files too */
	current->signal->rlim[RLIMIT_CORE] = (struct rlimit){1, 1};

	return err;
}

void do_coredump(const siginfo_t *siginfo)
{
	struct core_state core_state;
	struct core_name cn;
	struct mm_struct *mm = current->mm;
	struct linux_binfmt * binfmt;
	const struct cred *old_cred;
	struct cred *cred;
	int retval = 0;
	int ispipe;
	struct files_struct *displaced;
	/* require nonrelative corefile path and be extra careful */
	bool need_suid_safe = false;
	bool core_dumped = false;
	static atomic_t core_dump_count = ATOMIC_INIT(0);
	struct coredump_params cprm = {
		.siginfo = siginfo,
		.regs = signal_pt_regs(),
		.limit = rlimit(RLIMIT_CORE),
		/*
		 * We must use the same mm->flags while dumping core to avoid
		 * inconsistency of bit flags, since this flag is not protected
		 * by any locks.
		 */
		.mm_flags = mm->flags,
	};

	audit_core_dumps(siginfo->si_signo);

	binfmt = mm->binfmt;
	if (!binfmt || !binfmt->core_dump)
		goto fail;
	if (!__get_dumpable(cprm.mm_flags))
		goto fail;

	cred = prepare_creds();
	if (!cred)
		goto fail;
	/*
	 * We cannot trust fsuid as being the "true" uid of the process
	 * nor do we know its entire history. We only know it was tainted
	 * so we dump it as root in mode 2, and only into a controlled
	 * environment (pipe handler or fully qualified path).
	 */
	if (__get_dumpable(cprm.mm_flags) == SUID_DUMP_ROOT) {
		/* Setuid core dump mode */
		cred->fsuid = GLOBAL_ROOT_UID;	/* Dump root private */
		need_suid_safe = true;
	}

	retval = coredump_wait(siginfo->si_signo, &core_state);
	if (retval < 0)
		goto fail_creds;

	old_cred = override_creds(cred);

	ispipe = format_corename(&cn, &cprm);

	if (ispipe) {
		int dump_count;
		char **helper_argv;
		struct subprocess_info *sub_info;

		if (ispipe < 0) {
			printk(KERN_WARNING "format_corename failed\n");
			printk(KERN_WARNING "Aborting core\n");
			goto fail_unlock;
		}

		if (cprm.limit == 1) {
			/* See umh_pipe_setup() which sets RLIMIT_CORE = 1.
			 *
			 * Normally core limits are irrelevant to pipes, since
			 * we're not writing to the file system, but we use
			 * cprm.limit of 1 here as a special value, this is a
			 * consistent way to catch recursive crashes.
			 * We can still crash if the core_pattern binary sets
			 * RLIM_CORE = !1, but it runs as root, and can do
			 * lots of stupid things.
			 *
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

		helper_argv = argv_split(GFP_KERNEL, cn.corename, NULL);
		if (!helper_argv) {
			printk(KERN_WARNING "%s failed to allocate memory\n",
			       __func__);
			goto fail_dropcount;
		}

		retval = -ENOMEM;
		sub_info = call_usermodehelper_setup(helper_argv[0],
						helper_argv, NULL, GFP_KERNEL,
						umh_pipe_setup, NULL, &cprm);
		if (sub_info)
			retval = call_usermodehelper_exec(sub_info,
							  UMH_WAIT_EXEC);

		argv_free(helper_argv);
		if (retval) {
			printk(KERN_INFO "Core dump to |%s pipe failed\n",
			       cn.corename);
			goto close_fail;
		}
	} else {
		struct inode *inode;

		if (cprm.limit < binfmt->min_coredump)
			goto fail_unlock;

		if (need_suid_safe && cn.corename[0] != '/') {
			printk(KERN_WARNING "Pid %d(%s) can only dump core "\
				"to fully qualified path!\n",
				task_tgid_vnr(current), current->comm);
			printk(KERN_WARNING "Skipping core dump\n");
			goto fail_unlock;
		}

		/*
		 * Unlink the file if it exists unless this is a SUID
		 * binary - in that case, we're running around with root
		 * privs and don't want to unlink another user's coredump.
		 */
		if (!need_suid_safe) {
			mm_segment_t old_fs;

			old_fs = get_fs();
			set_fs(KERNEL_DS);
			/*
			 * If it doesn't exist, that's fine. If there's some
			 * other problem, we'll catch it at the filp_open().
			 */
			(void) sys_unlink((const char __user *)cn.corename);
			set_fs(old_fs);
		}

		/*
		 * There is a race between unlinking and creating the
		 * file, but if that causes an EEXIST here, that's
		 * fine - another process raced with us while creating
		 * the corefile, and the other process won. To userspace,
		 * what matters is that at least one of the two processes
		 * writes its coredump successfully, not which one.
		 */
		cprm.file = filp_open(cn.corename,
				 O_CREAT | 2 | O_NOFOLLOW |
				 O_LARGEFILE | O_EXCL,
				 0600);
		if (IS_ERR(cprm.file))
			goto fail_unlock;

		inode = file_inode(cprm.file);
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
		 * Don't dump core if the filesystem changed owner or mode
		 * of the file during file creation. This is an issue when
		 * a process dumps core while its cwd is e.g. on a vfat
		 * filesystem.
		 */
		if (!uid_eq(inode->i_uid, current_fsuid()))
			goto close_fail;
		if ((inode->i_mode & 0677) != 0600)
			goto close_fail;
		if (!(cprm.file->f_mode & FMODE_CAN_WRITE))
			goto close_fail;
		if (do_truncate(cprm.file->f_path.dentry, 0, 0, cprm.file))
			goto close_fail;
	}

	/* get us an unshared descriptor table; almost always a no-op */
	retval = unshare_files(&displaced);
	if (retval)
		goto close_fail;
	if (displaced)
		put_files_struct(displaced);
	if (!dump_interrupted()) {
		file_start_write(cprm.file);
		core_dumped = binfmt->core_dump(&cprm);
		file_end_write(cprm.file);
	}
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
	coredump_finish(mm, core_dumped);
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
int dump_emit(struct coredump_params *cprm, const void *addr, int nr)
{
	struct file *file = cprm->file;
	loff_t pos = file->f_pos;
	ssize_t n;
	if (cprm->written + nr > cprm->limit)
		return 0;
	while (nr) {
		if (dump_interrupted())
			return 0;
		n = __kernel_write(file, addr, nr, &pos);
		if (n <= 0)
			return 0;
		file->f_pos = pos;
		cprm->written += n;
		nr -= n;
	}
	return 1;
}
EXPORT_SYMBOL(dump_emit);

int dump_skip(struct coredump_params *cprm, size_t nr)
{
	static char zeroes[PAGE_SIZE];
	struct file *file = cprm->file;
	if (file->f_op->llseek && file->f_op->llseek != no_llseek) {
		if (cprm->written + nr > cprm->limit)
			return 0;
		if (dump_interrupted() ||
		    file->f_op->llseek(file, nr, SEEK_CUR) < 0)
			return 0;
		cprm->written += nr;
		return 1;
	} else {
		while (nr > PAGE_SIZE) {
			if (!dump_emit(cprm, zeroes, PAGE_SIZE))
				return 0;
			nr -= PAGE_SIZE;
		}
		return dump_emit(cprm, zeroes, nr);
	}
}
EXPORT_SYMBOL(dump_skip);

int dump_align(struct coredump_params *cprm, int align)
{
	unsigned mod = cprm->written & (align - 1);
	if (align & (align - 1))
		return 0;
	return mod ? dump_skip(cprm, align - mod) : 1;
}
EXPORT_SYMBOL(dump_align);
