// SPDX-License-Identifier: GPL-2.0
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/freezer.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/swap.h>
#include <linux/ctype.h>
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
#include <linux/sched/coredump.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
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
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/timekeeping.h>

#include <linux/uaccess.h>
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

static __printf(2, 0) int cn_vprintf(struct core_name *cn, const char *fmt,
				     va_list arg)
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

static __printf(2, 3) int cn_printf(struct core_name *cn, const char *fmt, ...)
{
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = cn_vprintf(cn, fmt, arg);
	va_end(arg);

	return ret;
}

static __printf(2, 3)
int cn_esc_printf(struct core_name *cn, const char *fmt, ...)
{
	int cur = cn->used;
	va_list arg;
	int ret;

	va_start(arg, fmt);
	ret = cn_vprintf(cn, fmt, arg);
	va_end(arg);

	if (ret == 0) {
		/*
		 * Ensure that this coredump name component can't cause the
		 * resulting corefile path to consist of a ".." or ".".
		 */
		if ((cn->used - cur == 1 && cn->corename[cur] == '.') ||
				(cn->used - cur == 2 && cn->corename[cur] == '.'
				&& cn->corename[cur+1] == '.'))
			cn->corename[cur] = '!';

		/*
		 * Empty names are fishy and could be used to create a "//" in a
		 * corefile name, causing the coredump to happen one directory
		 * level too high. Enforce that all components of the core
		 * pattern are at least one character long.
		 */
		if (cn->used == cur)
			ret = cn_printf(cn, "!");
	}

	for (; cur < cn->used; ++cur) {
		if (cn->corename[cur] == '/')
			cn->corename[cur] = '!';
	}
	return ret;
}

static int cn_print_exe_file(struct core_name *cn, bool name_only)
{
	struct file *exe_file;
	char *pathbuf, *path, *ptr;
	int ret;

	exe_file = get_mm_exe_file(current->mm);
	if (!exe_file)
		return cn_esc_printf(cn, "%s (path unknown)", current->comm);

	pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!pathbuf) {
		ret = -ENOMEM;
		goto put_exe_file;
	}

	path = file_path(exe_file, pathbuf, PATH_MAX);
	if (IS_ERR(path)) {
		ret = PTR_ERR(path);
		goto free_buf;
	}

	if (name_only) {
		ptr = strrchr(path, '/');
		if (ptr)
			path = ptr + 1;
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
static int format_corename(struct core_name *cn, struct coredump_params *cprm,
			   size_t **argv, int *argc)
{
	const struct cred *cred = current_cred();
	const char *pat_ptr = core_pattern;
	int ispipe = (*pat_ptr == '|');
	bool was_space = false;
	int pid_in_pattern = 0;
	int err = 0;

	cn->used = 0;
	cn->corename = NULL;
	if (expand_corename(cn, core_name_size))
		return -ENOMEM;
	cn->corename[0] = '\0';

	if (ispipe) {
		int argvs = sizeof(core_pattern) / 2;
		(*argv) = kmalloc_array(argvs, sizeof(**argv), GFP_KERNEL);
		if (!(*argv))
			return -ENOMEM;
		(*argv)[(*argc)++] = 0;
		++pat_ptr;
		if (!(*pat_ptr))
			return -ENOMEM;
	}

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		/*
		 * Split on spaces before doing template expansion so that
		 * %e and %E don't get split if they have spaces in them
		 */
		if (ispipe) {
			if (isspace(*pat_ptr)) {
				if (cn->used != 0)
					was_space = true;
				pat_ptr++;
				continue;
			} else if (was_space) {
				was_space = false;
				err = cn_printf(cn, "%c", '\0');
				if (err)
					return err;
				(*argv)[(*argc)++] = cn->used;
			}
		}
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
				err = cn_printf(cn, "%u",
						from_kuid(&init_user_ns,
							  cred->uid));
				break;
			/* gid */
			case 'g':
				err = cn_printf(cn, "%u",
						from_kgid(&init_user_ns,
							  cred->gid));
				break;
			case 'd':
				err = cn_printf(cn, "%d",
					__get_dumpable(cprm->mm_flags));
				break;
			/* signal that caused the coredump */
			case 's':
				err = cn_printf(cn, "%d",
						cprm->siginfo->si_signo);
				break;
			/* UNIX time of coredump */
			case 't': {
				time64_t time;

				time = ktime_get_real_seconds();
				err = cn_printf(cn, "%lld", time);
				break;
			}
			/* hostname */
			case 'h':
				down_read(&uts_sem);
				err = cn_esc_printf(cn, "%s",
					      utsname()->nodename);
				up_read(&uts_sem);
				break;
			/* executable, could be changed by prctl PR_SET_NAME etc */
			case 'e':
				err = cn_esc_printf(cn, "%s", current->comm);
				break;
			/* file name of executable */
			case 'f':
				err = cn_print_exe_file(cn, true);
				break;
			case 'E':
				err = cn_print_exe_file(cn, false);
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

static int zap_process(struct task_struct *start, int exit_code, int flags)
{
	struct task_struct *t;
	int nr = 0;

	/* ignore all signals except SIGKILL, see prepare_signal() */
	start->signal->flags = SIGNAL_GROUP_COREDUMP | flags;
	start->signal->group_exit_code = exit_code;
	start->signal->group_stop_count = 0;

	for_each_thread(start, t) {
		task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
		if (t != current && t->mm) {
			sigaddset(&t->pending.signal, SIGKILL);
			signal_wake_up(t, 1);
			nr++;
		}
	}

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
		tsk->signal->group_exit_task = tsk;
		nr = zap_process(tsk, exit_code, 0);
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
	 *	The caller holds mm->mmap_lock. This means that the task which
	 *	uses this mm can't pass exit_mm(), so it can't exit or clear
	 *	its ->mm.
	 *
	 * de_thread:
	 *	It does list_replace_rcu(&leader->tasks, &current->tasks),
	 *	we must see either old or new leader, this does not matter.
	 *	However, it can change p->sighand, so lock_task_sighand(p)
	 *	must be used. Since p->mm != NULL and we hold ->mmap_lock
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

		for_each_thread(g, p) {
			if (unlikely(!p->mm))
				continue;
			if (unlikely(p->mm == mm)) {
				lock_task_sighand(p, &flags);
				nr += zap_process(p, exit_code,
							SIGNAL_GROUP_EXIT);
				unlock_task_sighand(p, &flags);
			}
			break;
		}
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

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (!mm->core_state)
		core_waiters = zap_threads(tsk, mm, core_state, exit_code);
	mmap_write_unlock(mm);

	if (core_waiters > 0) {
		struct core_thread *ptr;

		freezer_do_not_count();
		wait_for_completion(&core_state->startup);
		freezer_count();
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
	wake_up_interruptible_sync(&pipe->rd_wait);
	kill_fasync(&pipe->fasync_readers, SIGIO, POLL_IN);
	pipe_unlock(pipe);

	/*
	 * We actually want wait_event_freezable() but then we need
	 * to clear TIF_SIGPENDING and improve dump_interrupted().
	 */
	wait_event_interruptible(pipe->rd_wait, pipe->readers == 1);

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

void do_coredump(const kernel_siginfo_t *siginfo)
{
	struct core_state core_state;
	struct core_name cn;
	struct mm_struct *mm = current->mm;
	struct linux_binfmt * binfmt;
	const struct cred *old_cred;
	struct cred *cred;
	int retval = 0;
	int ispipe;
	size_t *argv = NULL;
	int argc = 0;
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

	ispipe = format_corename(&cn, &cprm, &argv, &argc);

	if (ispipe) {
		int argi;
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

		helper_argv = kmalloc_array(argc + 1, sizeof(*helper_argv),
					    GFP_KERNEL);
		if (!helper_argv) {
			printk(KERN_WARNING "%s failed to allocate memory\n",
			       __func__);
			goto fail_dropcount;
		}
		for (argi = 0; argi < argc; argi++)
			helper_argv[argi] = cn.corename + argv[argi];
		helper_argv[argi] = NULL;

		retval = -ENOMEM;
		sub_info = call_usermodehelper_setup(helper_argv[0],
						helper_argv, NULL, GFP_KERNEL,
						umh_pipe_setup, NULL, &cprm);
		if (sub_info)
			retval = call_usermodehelper_exec(sub_info,
							  UMH_WAIT_EXEC);

		kfree(helper_argv);
		if (retval) {
			printk(KERN_INFO "Core dump to |%s pipe failed\n",
			       cn.corename);
			goto close_fail;
		}
	} else {
		struct inode *inode;
		int open_flags = O_CREAT | O_RDWR | O_NOFOLLOW |
				 O_LARGEFILE | O_EXCL;

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
			/*
			 * If it doesn't exist, that's fine. If there's some
			 * other problem, we'll catch it at the filp_open().
			 */
			do_unlinkat(AT_FDCWD, getname_kernel(cn.corename));
		}

		/*
		 * There is a race between unlinking and creating the
		 * file, but if that causes an EEXIST here, that's
		 * fine - another process raced with us while creating
		 * the corefile, and the other process won. To userspace,
		 * what matters is that at least one of the two processes
		 * writes its coredump successfully, not which one.
		 */
		if (need_suid_safe) {
			/*
			 * Using user namespaces, normal user tasks can change
			 * their current->fs->root to point to arbitrary
			 * directories. Since the intention of the "only dump
			 * with a fully qualified path" rule is to control where
			 * coredumps may be placed using root privileges,
			 * current->fs->root must not be used. Instead, use the
			 * root directory of init_task.
			 */
			struct path root;

			task_lock(&init_task);
			get_fs_root(init_task.fs, &root);
			task_unlock(&init_task);
			cprm.file = file_open_root(root.dentry, root.mnt,
				cn.corename, open_flags, 0600);
			path_put(&root);
		} else {
			cprm.file = filp_open(cn.corename, open_flags, 0600);
		}
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
		/*
		 * umh disabled with CONFIG_STATIC_USERMODEHELPER_PATH="" would
		 * have this set to NULL.
		 */
		if (!cprm.file) {
			pr_info("Core dump to |%s disabled\n", cn.corename);
			goto close_fail;
		}
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
	kfree(argv);
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


	if (dump_interrupted())
		return 0;
	n = __kernel_write(file, addr, nr, &pos);
	if (n != nr)
		return 0;
	file->f_pos = pos;
	cprm->written += n;
	cprm->pos += n;

	return 1;
}
EXPORT_SYMBOL(dump_emit);

int dump_skip(struct coredump_params *cprm, size_t nr)
{
	static char zeroes[PAGE_SIZE];
	struct file *file = cprm->file;
	if (file->f_op->llseek && file->f_op->llseek != no_llseek) {
		if (dump_interrupted() ||
		    file->f_op->llseek(file, nr, SEEK_CUR) < 0)
			return 0;
		cprm->pos += nr;
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

#ifdef CONFIG_ELF_CORE
int dump_user_range(struct coredump_params *cprm, unsigned long start,
		    unsigned long len)
{
	unsigned long addr;

	for (addr = start; addr < start + len; addr += PAGE_SIZE) {
		struct page *page;
		int stop;

		/*
		 * To avoid having to allocate page tables for virtual address
		 * ranges that have never been used yet, and also to make it
		 * easy to generate sparse core files, use a helper that returns
		 * NULL when encountering an empty page table entry that would
		 * otherwise have been filled with the zero page.
		 */
		page = get_dump_page(addr);
		if (page) {
			void *kaddr = kmap(page);

			stop = !dump_emit(cprm, kaddr, PAGE_SIZE);
			kunmap(page);
			put_page(page);
		} else {
			stop = !dump_skip(cprm, PAGE_SIZE);
		}
		if (stop)
			return 0;
	}
	return 1;
}
#endif

int dump_align(struct coredump_params *cprm, int align)
{
	unsigned mod = cprm->pos & (align - 1);
	if (align & (align - 1))
		return 0;
	return mod ? dump_skip(cprm, align - mod) : 1;
}
EXPORT_SYMBOL(dump_align);

/*
 * Ensures that file size is big enough to contain the current file
 * postion. This prevents gdb from complaining about a truncated file
 * if the last "write" to the file was dump_skip.
 */
void dump_truncate(struct coredump_params *cprm)
{
	struct file *file = cprm->file;
	loff_t offset;

	if (file->f_op->llseek && file->f_op->llseek != no_llseek) {
		offset = file->f_op->llseek(file, 0, SEEK_CUR);
		if (i_size_read(file->f_mapping->host) < offset)
			do_truncate(file->f_path.dentry, offset, 0, file);
	}
}
EXPORT_SYMBOL(dump_truncate);

/*
 * The purpose of always_dump_vma() is to make sure that special kernel mappings
 * that are useful for post-mortem analysis are included in every core dump.
 * In that way we ensure that the core dump is fully interpretable later
 * without matching up the same kernel and hardware config to see what PC values
 * meant. These special mappings include - vDSO, vsyscall, and other
 * architecture specific mappings
 */
static bool always_dump_vma(struct vm_area_struct *vma)
{
	/* Any vsyscall mappings? */
	if (vma == get_gate_vma(vma->vm_mm))
		return true;

	/*
	 * Assume that all vmas with a .name op should always be dumped.
	 * If this changes, a new vm_ops field can easily be added.
	 */
	if (vma->vm_ops && vma->vm_ops->name && vma->vm_ops->name(vma))
		return true;

	/*
	 * arch_vma_name() returns non-NULL for special architecture mappings,
	 * such as vDSO sections.
	 */
	if (arch_vma_name(vma))
		return true;

	return false;
}

/*
 * Decide how much of @vma's contents should be included in a core dump.
 */
static unsigned long vma_dump_size(struct vm_area_struct *vma,
				   unsigned long mm_flags)
{
#define FILTER(type)	(mm_flags & (1UL << MMF_DUMP_##type))

	/* always dump the vdso and vsyscall sections */
	if (always_dump_vma(vma))
		goto whole;

	if (vma->vm_flags & VM_DONTDUMP)
		return 0;

	/* support for DAX */
	if (vma_is_dax(vma)) {
		if ((vma->vm_flags & VM_SHARED) && FILTER(DAX_SHARED))
			goto whole;
		if (!(vma->vm_flags & VM_SHARED) && FILTER(DAX_PRIVATE))
			goto whole;
		return 0;
	}

	/* Hugetlb memory check */
	if (is_vm_hugetlb_page(vma)) {
		if ((vma->vm_flags & VM_SHARED) && FILTER(HUGETLB_SHARED))
			goto whole;
		if (!(vma->vm_flags & VM_SHARED) && FILTER(HUGETLB_PRIVATE))
			goto whole;
		return 0;
	}

	/* Do not dump I/O mapped devices or special mappings */
	if (vma->vm_flags & VM_IO)
		return 0;

	/* By default, dump shared memory if mapped from an anonymous file. */
	if (vma->vm_flags & VM_SHARED) {
		if (file_inode(vma->vm_file)->i_nlink == 0 ?
		    FILTER(ANON_SHARED) : FILTER(MAPPED_SHARED))
			goto whole;
		return 0;
	}

	/* Dump segments that have been written to.  */
	if ((!IS_ENABLED(CONFIG_MMU) || vma->anon_vma) && FILTER(ANON_PRIVATE))
		goto whole;
	if (vma->vm_file == NULL)
		return 0;

	if (FILTER(MAPPED_PRIVATE))
		goto whole;

	/*
	 * If this is the beginning of an executable file mapping,
	 * dump the first page to aid in determining what was mapped here.
	 */
	if (FILTER(ELF_HEADERS) &&
	    vma->vm_pgoff == 0 && (vma->vm_flags & VM_READ) &&
	    (READ_ONCE(file_inode(vma->vm_file)->i_mode) & 0111) != 0)
		return PAGE_SIZE;

#undef	FILTER

	return 0;

whole:
	return vma->vm_end - vma->vm_start;
}

static struct vm_area_struct *first_vma(struct task_struct *tsk,
					struct vm_area_struct *gate_vma)
{
	struct vm_area_struct *ret = tsk->mm->mmap;

	if (ret)
		return ret;
	return gate_vma;
}

/*
 * Helper function for iterating across a vma list.  It ensures that the caller
 * will visit `gate_vma' prior to terminating the search.
 */
static struct vm_area_struct *next_vma(struct vm_area_struct *this_vma,
				       struct vm_area_struct *gate_vma)
{
	struct vm_area_struct *ret;

	ret = this_vma->vm_next;
	if (ret)
		return ret;
	if (this_vma == gate_vma)
		return NULL;
	return gate_vma;
}

/*
 * Under the mmap_lock, take a snapshot of relevant information about the task's
 * VMAs.
 */
int dump_vma_snapshot(struct coredump_params *cprm, int *vma_count,
		      struct core_vma_metadata **vma_meta,
		      size_t *vma_data_size_ptr)
{
	struct vm_area_struct *vma, *gate_vma;
	struct mm_struct *mm = current->mm;
	int i;
	size_t vma_data_size = 0;

	/*
	 * Once the stack expansion code is fixed to not change VMA bounds
	 * under mmap_lock in read mode, this can be changed to take the
	 * mmap_lock in read mode.
	 */
	if (mmap_write_lock_killable(mm))
		return -EINTR;

	gate_vma = get_gate_vma(mm);
	*vma_count = mm->map_count + (gate_vma ? 1 : 0);

	*vma_meta = kvmalloc_array(*vma_count, sizeof(**vma_meta), GFP_KERNEL);
	if (!*vma_meta) {
		mmap_write_unlock(mm);
		return -ENOMEM;
	}

	for (i = 0, vma = first_vma(current, gate_vma); vma != NULL;
			vma = next_vma(vma, gate_vma), i++) {
		struct core_vma_metadata *m = (*vma_meta) + i;

		m->start = vma->vm_start;
		m->end = vma->vm_end;
		m->flags = vma->vm_flags;
		m->dump_size = vma_dump_size(vma, cprm->mm_flags);

		vma_data_size += m->dump_size;
	}

	mmap_write_unlock(mm);

	if (WARN_ON(i != *vma_count))
		return -EFAULT;

	*vma_data_size_ptr = vma_data_size;
	return 0;
}
