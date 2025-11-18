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
#include <linux/sort.h>
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
#include <linux/kmod.h>
#include <linux/fsnotify.h>
#include <linux/fs_struct.h>
#include <linux/pipe_fs_i.h>
#include <linux/oom.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/timekeeping.h>
#include <linux/sysctl.h>
#include <linux/elf.h>
#include <linux/pidfs.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/af_unix.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <uapi/linux/pidfd.h>
#include <uapi/linux/un.h>
#include <uapi/linux/coredump.h>

#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/exec.h>

#include <trace/events/task.h>
#include "internal.h"

#include <trace/events/sched.h>

static bool dump_vma_snapshot(struct coredump_params *cprm);
static void free_vma_snapshot(struct coredump_params *cprm);

#define CORE_FILE_NOTE_SIZE_DEFAULT (4*1024*1024)
/* Define a reasonable max cap */
#define CORE_FILE_NOTE_SIZE_MAX (16*1024*1024)
/*
 * File descriptor number for the pidfd for the thread-group leader of
 * the coredumping task installed into the usermode helper's file
 * descriptor table.
 */
#define COREDUMP_PIDFD_NUMBER 3

static int core_uses_pid;
static unsigned int core_pipe_limit;
static unsigned int core_sort_vma;
static char core_pattern[CORENAME_MAX_SIZE] = "core";
static int core_name_size = CORENAME_MAX_SIZE;
unsigned int core_file_note_size_limit = CORE_FILE_NOTE_SIZE_DEFAULT;
static atomic_t core_pipe_count = ATOMIC_INIT(0);

enum coredump_type_t {
	COREDUMP_FILE		= 1,
	COREDUMP_PIPE		= 2,
	COREDUMP_SOCK		= 3,
	COREDUMP_SOCK_REQ	= 4,
};

struct core_name {
	char *corename;
	int used, size;
	unsigned int core_pipe_limit;
	bool core_dumped;
	enum coredump_type_t core_type;
	u64 mask;
};

static int expand_corename(struct core_name *cn, int size)
{
	char *corename;

	size = kmalloc_size_roundup(size);
	corename = krealloc(cn->corename, size, GFP_KERNEL);

	if (!corename)
		return -ENOMEM;

	if (size > core_name_size) /* racy but harmless */
		core_name_size = size;

	cn->size = size;
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

/*
 * coredump_parse will inspect the pattern parameter, and output a name
 * into corename, which must have space for at least CORENAME_MAX_SIZE
 * bytes plus one byte for the zero terminator.
 */
static bool coredump_parse(struct core_name *cn, struct coredump_params *cprm,
			   size_t **argv, int *argc)
{
	const struct cred *cred = current_cred();
	const char *pat_ptr = core_pattern;
	bool was_space = false;
	int pid_in_pattern = 0;
	int err = 0;

	cn->mask = COREDUMP_KERNEL;
	if (core_pipe_limit)
		cn->mask |= COREDUMP_WAIT;
	cn->used = 0;
	cn->corename = NULL;
	cn->core_pipe_limit = 0;
	cn->core_dumped = false;
	if (*pat_ptr == '|')
		cn->core_type = COREDUMP_PIPE;
	else if (*pat_ptr == '@')
		cn->core_type = COREDUMP_SOCK;
	else
		cn->core_type = COREDUMP_FILE;
	if (expand_corename(cn, core_name_size))
		return false;
	cn->corename[0] = '\0';

	switch (cn->core_type) {
	case COREDUMP_PIPE: {
		int argvs = sizeof(core_pattern) / 2;
		(*argv) = kmalloc_array(argvs, sizeof(**argv), GFP_KERNEL);
		if (!(*argv))
			return false;
		(*argv)[(*argc)++] = 0;
		++pat_ptr;
		if (!(*pat_ptr))
			return false;
		break;
	}
	case COREDUMP_SOCK: {
		/* skip the @ */
		pat_ptr++;
		if (!(*pat_ptr))
			return false;
		if (*pat_ptr == '@') {
			pat_ptr++;
			if (!(*pat_ptr))
				return false;

			cn->core_type = COREDUMP_SOCK_REQ;
		}

		err = cn_printf(cn, "%s", pat_ptr);
		if (err)
			return false;

		/* Require absolute paths. */
		if (cn->corename[0] != '/')
			return false;

		/*
		 * Ensure we can uses spaces to indicate additional
		 * parameters in the future.
		 */
		if (strchr(cn->corename, ' ')) {
			coredump_report_failure("Coredump socket may not %s contain spaces", cn->corename);
			return false;
		}

		/* Must not contain ".." in the path. */
		if (name_contains_dotdot(cn->corename)) {
			coredump_report_failure("Coredump socket may not %s contain '..' spaces", cn->corename);
			return false;
		}

		if (strlen(cn->corename) >= UNIX_PATH_MAX) {
			coredump_report_failure("Coredump socket path %s too long", cn->corename);
			return false;
		}

		/*
		 * Currently no need to parse any other options.
		 * Relevant information can be retrieved from the peer
		 * pidfd retrievable via SO_PEERPIDFD by the receiver or
		 * via /proc/<pid>, using the SO_PEERPIDFD to guard
		 * against pid recycling when opening /proc/<pid>.
		 */
		return true;
	}
	case COREDUMP_FILE:
		break;
	default:
		WARN_ON_ONCE(true);
		return false;
	}

	/* Repeat as long as we have more pattern to process and more output
	   space */
	while (*pat_ptr) {
		/*
		 * Split on spaces before doing template expansion so that
		 * %e and %E don't get split if they have spaces in them
		 */
		if (cn->core_type == COREDUMP_PIPE) {
			if (isspace(*pat_ptr)) {
				if (cn->used != 0)
					was_space = true;
				pat_ptr++;
				continue;
			} else if (was_space) {
				was_space = false;
				err = cn_printf(cn, "%c", '\0');
				if (err)
					return false;
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
			/* CPU the task ran on */
			case 'C':
				err = cn_printf(cn, "%d", cprm->cpu);
				break;
			/* pidfd number */
			case 'F': {
				/*
				 * Installing a pidfd only makes sense if
				 * we actually spawn a usermode helper.
				 */
				if (cn->core_type != COREDUMP_PIPE)
					break;

				/*
				 * Note that we'll install a pidfd for the
				 * thread-group leader. We know that task
				 * linkage hasn't been removed yet and even if
				 * this @current isn't the actual thread-group
				 * leader we know that the thread-group leader
				 * cannot be reaped until @current has exited.
				 */
				cprm->pid = task_tgid(current);
				err = cn_printf(cn, "%d", COREDUMP_PIDFD_NUMBER);
				break;
			}
			default:
				break;
			}
			++pat_ptr;
		}

		if (err)
			return false;
	}

out:
	/* Backward compatibility with core_uses_pid:
	 *
	 * If core_pattern does not include a %p (as is the default)
	 * and core_uses_pid is set, then .%pid will be appended to
	 * the filename. Do not do this for piped commands. */
	if (cn->core_type == COREDUMP_FILE && !pid_in_pattern && core_uses_pid)
		return cn_printf(cn, ".%d", task_tgid_vnr(current)) == 0;

	return true;
}

static int zap_process(struct signal_struct *signal, int exit_code)
{
	struct task_struct *t;
	int nr = 0;

	signal->flags = SIGNAL_GROUP_EXIT;
	signal->group_exit_code = exit_code;
	signal->group_stop_count = 0;

	__for_each_thread(signal, t) {
		task_clear_jobctl_pending(t, JOBCTL_PENDING_MASK);
		if (t != current && !(t->flags & PF_POSTCOREDUMP)) {
			sigaddset(&t->pending.signal, SIGKILL);
			signal_wake_up(t, 1);
			nr++;
		}
	}

	return nr;
}

static int zap_threads(struct task_struct *tsk,
			struct core_state *core_state, int exit_code)
{
	struct signal_struct *signal = tsk->signal;
	int nr = -EAGAIN;

	spin_lock_irq(&tsk->sighand->siglock);
	if (!(signal->flags & SIGNAL_GROUP_EXIT) && !signal->group_exec_task) {
		/* Allow SIGKILL, see prepare_signal() */
		signal->core_state = core_state;
		nr = zap_process(signal, exit_code);
		clear_tsk_thread_flag(tsk, TIF_SIGPENDING);
		tsk->flags |= PF_DUMPCORE;
		atomic_set(&core_state->nr_threads, nr);
	}
	spin_unlock_irq(&tsk->sighand->siglock);
	return nr;
}

static int coredump_wait(int exit_code, struct core_state *core_state)
{
	struct task_struct *tsk = current;
	int core_waiters = -EBUSY;

	init_completion(&core_state->startup);
	core_state->dumper.task = tsk;
	core_state->dumper.next = NULL;

	core_waiters = zap_threads(tsk, core_state, exit_code);
	if (core_waiters > 0) {
		struct core_thread *ptr;

		wait_for_completion_state(&core_state->startup,
					  TASK_UNINTERRUPTIBLE|TASK_FREEZABLE);
		/*
		 * Wait for all the threads to become inactive, so that
		 * all the thread context (extended register state, like
		 * fpu etc) gets copied to the memory.
		 */
		ptr = core_state->dumper.next;
		while (ptr != NULL) {
			wait_task_inactive(ptr->task, TASK_ANY);
			ptr = ptr->next;
		}
	}

	return core_waiters;
}

static void coredump_finish(bool core_dumped)
{
	struct core_thread *curr, *next;
	struct task_struct *task;

	spin_lock_irq(&current->sighand->siglock);
	if (core_dumped && !__fatal_signal_pending(current))
		current->signal->group_exit_code |= 0x80;
	next = current->signal->core_state->dumper.next;
	current->signal->core_state = NULL;
	spin_unlock_irq(&current->sighand->siglock);

	while ((curr = next) != NULL) {
		next = curr->next;
		task = curr->task;
		/*
		 * see coredump_task_exit(), curr->task must not see
		 * ->task == NULL before we read ->next.
		 */
		smp_mb();
		curr->task = NULL;
		wake_up_process(task);
	}
}

static bool dump_interrupted(void)
{
	/*
	 * SIGKILL or freezing() interrupt the coredumping. Perhaps we
	 * can do try_to_freeze() and check __fatal_signal_pending(),
	 * but then we need to teach dump_write() to restart and clear
	 * TIF_SIGPENDING.
	 */
	return fatal_signal_pending(current) || freezing(current);
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
 * umh_coredump_setup
 * helper function to customize the process used
 * to collect the core in userspace.  Specifically
 * it sets up a pipe and installs it as fd 0 (stdin)
 * for the process.  Returns 0 on success, or
 * PTR_ERR on failure.
 * Note that it also sets the core limit to 1.  This
 * is a special value that we use to trap recursive
 * core dumps
 */
static int umh_coredump_setup(struct subprocess_info *info, struct cred *new)
{
	struct file *files[2];
	struct coredump_params *cp = (struct coredump_params *)info->data;
	int err;

	if (cp->pid) {
		struct file *pidfs_file __free(fput) = NULL;

		pidfs_file = pidfs_alloc_file(cp->pid, 0);
		if (IS_ERR(pidfs_file))
			return PTR_ERR(pidfs_file);

		pidfs_coredump(cp);

		/*
		 * Usermode helpers are childen of either
		 * system_dfl_wq or of kthreadd. So we know that
		 * we're starting off with a clean file descriptor
		 * table. So we should always be able to use
		 * COREDUMP_PIDFD_NUMBER as our file descriptor value.
		 */
		err = replace_fd(COREDUMP_PIDFD_NUMBER, pidfs_file, 0);
		if (err < 0)
			return err;
	}

	err = create_pipe_files(files, 0);
	if (err)
		return err;

	cp->file = files[1];

	err = replace_fd(0, files[0], 0);
	fput(files[0]);
	if (err < 0)
		return err;

	/* and disallow core files too */
	current->signal->rlim[RLIMIT_CORE] = (struct rlimit){1, 1};

	return 0;
}

#ifdef CONFIG_UNIX
static bool coredump_sock_connect(struct core_name *cn, struct coredump_params *cprm)
{
	struct file *file __free(fput) = NULL;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
	};
	ssize_t addr_len;
	int retval;
	struct socket *socket;

	addr_len = strscpy(addr.sun_path, cn->corename);
	if (addr_len < 0)
		return false;
	addr_len += offsetof(struct sockaddr_un, sun_path) + 1;

	/*
	 * It is possible that the userspace process which is supposed
	 * to handle the coredump and is listening on the AF_UNIX socket
	 * coredumps. Userspace should just mark itself non dumpable.
	 */

	retval = sock_create_kern(&init_net, AF_UNIX, SOCK_STREAM, 0, &socket);
	if (retval < 0)
		return false;

	file = sock_alloc_file(socket, 0, NULL);
	if (IS_ERR(file))
		return false;

	/*
	 * Set the thread-group leader pid which is used for the peer
	 * credentials during connect() below. Then immediately register
	 * it in pidfs...
	 */
	cprm->pid = task_tgid(current);
	retval = pidfs_register_pid(cprm->pid);
	if (retval)
		return false;

	/*
	 * ... and set the coredump information so userspace has it
	 * available after connect()...
	 */
	pidfs_coredump(cprm);

	retval = kernel_connect(socket, (struct sockaddr *)(&addr), addr_len,
				O_NONBLOCK | SOCK_COREDUMP);

	if (retval) {
		if (retval == -EAGAIN)
			coredump_report_failure("Coredump socket %s receive queue full", addr.sun_path);
		else
			coredump_report_failure("Coredump socket connection %s failed %d", addr.sun_path, retval);
		return false;
	}

	/* ... and validate that @sk_peer_pid matches @cprm.pid. */
	if (WARN_ON_ONCE(unix_peer(socket->sk)->sk_peer_pid != cprm->pid))
		return false;

	cprm->limit = RLIM_INFINITY;
	cprm->file = no_free_ptr(file);

	return true;
}

static inline bool coredump_sock_recv(struct file *file, struct coredump_ack *ack, size_t size, int flags)
{
	struct msghdr msg = {};
	struct kvec iov = { .iov_base = ack, .iov_len = size };
	ssize_t ret;

	memset(ack, 0, size);
	ret = kernel_recvmsg(sock_from_file(file), &msg, &iov, 1, size, flags);
	return ret == size;
}

static inline bool coredump_sock_send(struct file *file, struct coredump_req *req)
{
	struct msghdr msg = { .msg_flags = MSG_NOSIGNAL };
	struct kvec iov = { .iov_base = req, .iov_len = sizeof(*req) };
	ssize_t ret;

	ret = kernel_sendmsg(sock_from_file(file), &msg, &iov, 1, sizeof(*req));
	return ret == sizeof(*req);
}

static_assert(sizeof(enum coredump_mark) == sizeof(__u32));

static inline bool coredump_sock_mark(struct file *file, enum coredump_mark mark)
{
	struct msghdr msg = { .msg_flags = MSG_NOSIGNAL };
	struct kvec iov = { .iov_base = &mark, .iov_len = sizeof(mark) };
	ssize_t ret;

	ret = kernel_sendmsg(sock_from_file(file), &msg, &iov, 1, sizeof(mark));
	return ret == sizeof(mark);
}

static inline void coredump_sock_wait(struct file *file)
{
	ssize_t n;

	/*
	 * We use a simple read to wait for the coredump processing to
	 * finish. Either the socket is closed or we get sent unexpected
	 * data. In both cases, we're done.
	 */
	n = __kernel_read(file, &(char){ 0 }, 1, NULL);
	if (n > 0)
		coredump_report_failure("Coredump socket had unexpected data");
	else if (n < 0)
		coredump_report_failure("Coredump socket failed");
}

static inline void coredump_sock_shutdown(struct file *file)
{
	struct socket *socket;

	socket = sock_from_file(file);
	if (!socket)
		return;

	/* Let userspace know we're done processing the coredump. */
	kernel_sock_shutdown(socket, SHUT_WR);
}

static bool coredump_sock_request(struct core_name *cn, struct coredump_params *cprm)
{
	struct coredump_req req = {
		.size		= sizeof(struct coredump_req),
		.mask		= COREDUMP_KERNEL | COREDUMP_USERSPACE |
				  COREDUMP_REJECT | COREDUMP_WAIT,
		.size_ack	= sizeof(struct coredump_ack),
	};
	struct coredump_ack ack = {};
	ssize_t usize;

	if (cn->core_type != COREDUMP_SOCK_REQ)
		return true;

	/* Let userspace know what we support. */
	if (!coredump_sock_send(cprm->file, &req))
		return false;

	/* Peek the size of the coredump_ack. */
	if (!coredump_sock_recv(cprm->file, &ack, sizeof(ack.size),
				MSG_PEEK | MSG_WAITALL))
		return false;

	/* Refuse unknown coredump_ack sizes. */
	usize = ack.size;
	if (usize < COREDUMP_ACK_SIZE_VER0) {
		coredump_sock_mark(cprm->file, COREDUMP_MARK_MINSIZE);
		return false;
	}

	if (usize > sizeof(ack)) {
		coredump_sock_mark(cprm->file, COREDUMP_MARK_MAXSIZE);
		return false;
	}

	/* Now retrieve the coredump_ack. */
	if (!coredump_sock_recv(cprm->file, &ack, usize, MSG_WAITALL))
		return false;
	if (ack.size != usize)
		return false;

	/* Refuse unknown coredump_ack flags. */
	if (ack.mask & ~req.mask) {
		coredump_sock_mark(cprm->file, COREDUMP_MARK_UNSUPPORTED);
		return false;
	}

	/* Refuse mutually exclusive options. */
	if (hweight64(ack.mask & (COREDUMP_USERSPACE | COREDUMP_KERNEL |
				  COREDUMP_REJECT)) != 1) {
		coredump_sock_mark(cprm->file, COREDUMP_MARK_CONFLICTING);
		return false;
	}

	if (ack.spare) {
		coredump_sock_mark(cprm->file, COREDUMP_MARK_UNSUPPORTED);
		return false;
	}

	cn->mask = ack.mask;
	return coredump_sock_mark(cprm->file, COREDUMP_MARK_REQACK);
}

static bool coredump_socket(struct core_name *cn, struct coredump_params *cprm)
{
	if (!coredump_sock_connect(cn, cprm))
		return false;

	return coredump_sock_request(cn, cprm);
}
#else
static inline void coredump_sock_wait(struct file *file) { }
static inline void coredump_sock_shutdown(struct file *file) { }
static inline bool coredump_socket(struct core_name *cn, struct coredump_params *cprm) { return false; }
#endif

/* cprm->mm_flags contains a stable snapshot of dumpability flags. */
static inline bool coredump_force_suid_safe(const struct coredump_params *cprm)
{
	/* Require nonrelative corefile path and be extra careful. */
	return __get_dumpable(cprm->mm_flags) == SUID_DUMP_ROOT;
}

static bool coredump_file(struct core_name *cn, struct coredump_params *cprm,
			  const struct linux_binfmt *binfmt)
{
	struct mnt_idmap *idmap;
	struct inode *inode;
	struct file *file __free(fput) = NULL;
	int open_flags = O_CREAT | O_WRONLY | O_NOFOLLOW | O_LARGEFILE | O_EXCL;

	if (cprm->limit < binfmt->min_coredump)
		return false;

	if (coredump_force_suid_safe(cprm) && cn->corename[0] != '/') {
		coredump_report_failure("this process can only dump core to a fully qualified path, skipping core dump");
		return false;
	}

	/*
	 * Unlink the file if it exists unless this is a SUID
	 * binary - in that case, we're running around with root
	 * privs and don't want to unlink another user's coredump.
	 */
	if (!coredump_force_suid_safe(cprm)) {
		/*
		 * If it doesn't exist, that's fine. If there's some
		 * other problem, we'll catch it at the filp_open().
		 */
		do_unlinkat(AT_FDCWD, getname_kernel(cn->corename));
	}

	/*
	 * There is a race between unlinking and creating the
	 * file, but if that causes an EEXIST here, that's
	 * fine - another process raced with us while creating
	 * the corefile, and the other process won. To userspace,
	 * what matters is that at least one of the two processes
	 * writes its coredump successfully, not which one.
	 */
	if (coredump_force_suid_safe(cprm)) {
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
		file = file_open_root(&root, cn->corename, open_flags, 0600);
		path_put(&root);
	} else {
		file = filp_open(cn->corename, open_flags, 0600);
	}
	if (IS_ERR(file))
		return false;

	inode = file_inode(file);
	if (inode->i_nlink > 1)
		return false;
	if (d_unhashed(file->f_path.dentry))
		return false;
	/*
	 * AK: actually i see no reason to not allow this for named
	 * pipes etc, but keep the previous behaviour for now.
	 */
	if (!S_ISREG(inode->i_mode))
		return false;
	/*
	 * Don't dump core if the filesystem changed owner or mode
	 * of the file during file creation. This is an issue when
	 * a process dumps core while its cwd is e.g. on a vfat
	 * filesystem.
	 */
	idmap = file_mnt_idmap(file);
	if (!vfsuid_eq_kuid(i_uid_into_vfsuid(idmap, inode), current_fsuid())) {
		coredump_report_failure("Core dump to %s aborted: cannot preserve file owner", cn->corename);
		return false;
	}
	if ((inode->i_mode & 0677) != 0600) {
		coredump_report_failure("Core dump to %s aborted: cannot preserve file permissions", cn->corename);
		return false;
	}
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return false;
	if (do_truncate(idmap, file->f_path.dentry, 0, 0, file))
		return false;

	cprm->file = no_free_ptr(file);
	return true;
}

static bool coredump_pipe(struct core_name *cn, struct coredump_params *cprm,
			  size_t *argv, int argc)
{
	int argi;
	char **helper_argv __free(kfree) = NULL;
	struct subprocess_info *sub_info;

	if (cprm->limit == 1) {
		/* See umh_coredump_setup() which sets RLIMIT_CORE = 1.
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
		coredump_report_failure("RLIMIT_CORE is set to 1, aborting core");
		return false;
	}
	cprm->limit = RLIM_INFINITY;

	cn->core_pipe_limit = atomic_inc_return(&core_pipe_count);
	if (core_pipe_limit && (core_pipe_limit < cn->core_pipe_limit)) {
		coredump_report_failure("over core_pipe_limit, skipping core dump");
		return false;
	}

	helper_argv = kmalloc_array(argc + 1, sizeof(*helper_argv), GFP_KERNEL);
	if (!helper_argv) {
		coredump_report_failure("%s failed to allocate memory", __func__);
		return false;
	}
	for (argi = 0; argi < argc; argi++)
		helper_argv[argi] = cn->corename + argv[argi];
	helper_argv[argi] = NULL;

	sub_info = call_usermodehelper_setup(helper_argv[0], helper_argv, NULL,
					     GFP_KERNEL, umh_coredump_setup,
					     NULL, cprm);
	if (!sub_info)
		return false;

	if (call_usermodehelper_exec(sub_info, UMH_WAIT_EXEC)) {
		coredump_report_failure("|%s pipe failed", cn->corename);
		return false;
	}

	/*
	 * umh disabled with CONFIG_STATIC_USERMODEHELPER_PATH="" would
	 * have this set to NULL.
	 */
	if (!cprm->file) {
		coredump_report_failure("Core dump to |%s disabled", cn->corename);
		return false;
	}

	return true;
}

static bool coredump_write(struct core_name *cn,
			  struct coredump_params *cprm,
			  struct linux_binfmt *binfmt)
{

	if (dump_interrupted())
		return true;

	if (!dump_vma_snapshot(cprm))
		return false;

	file_start_write(cprm->file);
	cn->core_dumped = binfmt->core_dump(cprm);
	/*
	 * Ensures that file size is big enough to contain the current
	 * file postion. This prevents gdb from complaining about
	 * a truncated file if the last "write" to the file was
	 * dump_skip.
	 */
	if (cprm->to_skip) {
		cprm->to_skip--;
		dump_emit(cprm, "", 1);
	}
	file_end_write(cprm->file);
	free_vma_snapshot(cprm);
	return true;
}

static void coredump_cleanup(struct core_name *cn, struct coredump_params *cprm)
{
	if (cprm->file)
		filp_close(cprm->file, NULL);
	if (cn->core_pipe_limit) {
		VFS_WARN_ON_ONCE(cn->core_type != COREDUMP_PIPE);
		atomic_dec(&core_pipe_count);
	}
	kfree(cn->corename);
	coredump_finish(cn->core_dumped);
}

static inline bool coredump_skip(const struct coredump_params *cprm,
				 const struct linux_binfmt *binfmt)
{
	if (!binfmt)
		return true;
	if (!binfmt->core_dump)
		return true;
	if (!__get_dumpable(cprm->mm_flags))
		return true;
	return false;
}

void vfs_coredump(const kernel_siginfo_t *siginfo)
{
	struct cred *cred __free(put_cred) = NULL;
	size_t *argv __free(kfree) = NULL;
	struct core_state core_state;
	struct core_name cn;
	struct mm_struct *mm = current->mm;
	struct linux_binfmt *binfmt = mm->binfmt;
	const struct cred *old_cred;
	int argc = 0;
	struct coredump_params cprm = {
		.siginfo = siginfo,
		.limit = rlimit(RLIMIT_CORE),
		/*
		 * We must use the same mm->flags while dumping core to avoid
		 * inconsistency of bit flags, since this flag is not protected
		 * by any locks.
		 *
		 * Note that we only care about MMF_DUMP* flags.
		 */
		.mm_flags = __mm_flags_get_dumpable(mm),
		.vma_meta = NULL,
		.cpu = raw_smp_processor_id(),
	};

	audit_core_dumps(siginfo->si_signo);

	if (coredump_skip(&cprm, binfmt))
		return;

	cred = prepare_creds();
	if (!cred)
		return;
	/*
	 * We cannot trust fsuid as being the "true" uid of the process
	 * nor do we know its entire history. We only know it was tainted
	 * so we dump it as root in mode 2, and only into a controlled
	 * environment (pipe handler or fully qualified path).
	 */
	if (coredump_force_suid_safe(&cprm))
		cred->fsuid = GLOBAL_ROOT_UID;

	if (coredump_wait(siginfo->si_signo, &core_state) < 0)
		return;

	old_cred = override_creds(cred);

	if (!coredump_parse(&cn, &cprm, &argv, &argc)) {
		coredump_report_failure("format_corename failed, aborting core");
		goto close_fail;
	}

	switch (cn.core_type) {
	case COREDUMP_FILE:
		if (!coredump_file(&cn, &cprm, binfmt))
			goto close_fail;
		break;
	case COREDUMP_PIPE:
		if (!coredump_pipe(&cn, &cprm, argv, argc))
			goto close_fail;
		break;
	case COREDUMP_SOCK_REQ:
		fallthrough;
	case COREDUMP_SOCK:
		if (!coredump_socket(&cn, &cprm))
			goto close_fail;
		break;
	default:
		WARN_ON_ONCE(true);
		goto close_fail;
	}

	/* Don't even generate the coredump. */
	if (cn.mask & COREDUMP_REJECT)
		goto close_fail;

	/* get us an unshared descriptor table; almost always a no-op */
	/* The cell spufs coredump code reads the file descriptor tables */
	if (unshare_files())
		goto close_fail;

	if ((cn.mask & COREDUMP_KERNEL) && !coredump_write(&cn, &cprm, binfmt))
		goto close_fail;

	coredump_sock_shutdown(cprm.file);

	/* Let the parent know that a coredump was generated. */
	if (cn.mask & COREDUMP_USERSPACE)
		cn.core_dumped = true;

	/*
	 * When core_pipe_limit is set we wait for the coredump server
	 * or usermodehelper to finish before exiting so it can e.g.,
	 * inspect /proc/<pid>.
	 */
	if (cn.mask & COREDUMP_WAIT) {
		switch (cn.core_type) {
		case COREDUMP_PIPE:
			wait_for_dump_helpers(cprm.file);
			break;
		case COREDUMP_SOCK_REQ:
			fallthrough;
		case COREDUMP_SOCK:
			coredump_sock_wait(cprm.file);
			break;
		default:
			break;
		}
	}

close_fail:
	coredump_cleanup(&cn, &cprm);
	revert_creds(old_cred);
	return;
}

/*
 * Core dumping helper functions.  These are the only things you should
 * do on a core-file: use only these functions to write out all the
 * necessary info.
 */
static int __dump_emit(struct coredump_params *cprm, const void *addr, int nr)
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

static int __dump_skip(struct coredump_params *cprm, size_t nr)
{
	static char zeroes[PAGE_SIZE];
	struct file *file = cprm->file;

	if (file->f_mode & FMODE_LSEEK) {
		if (dump_interrupted() || vfs_llseek(file, nr, SEEK_CUR) < 0)
			return 0;
		cprm->pos += nr;
		return 1;
	}

	while (nr > PAGE_SIZE) {
		if (!__dump_emit(cprm, zeroes, PAGE_SIZE))
			return 0;
		nr -= PAGE_SIZE;
	}

	return __dump_emit(cprm, zeroes, nr);
}

int dump_emit(struct coredump_params *cprm, const void *addr, int nr)
{
	if (cprm->to_skip) {
		if (!__dump_skip(cprm, cprm->to_skip))
			return 0;
		cprm->to_skip = 0;
	}
	return __dump_emit(cprm, addr, nr);
}
EXPORT_SYMBOL(dump_emit);

void dump_skip_to(struct coredump_params *cprm, unsigned long pos)
{
	cprm->to_skip = pos - cprm->pos;
}
EXPORT_SYMBOL(dump_skip_to);

void dump_skip(struct coredump_params *cprm, size_t nr)
{
	cprm->to_skip += nr;
}
EXPORT_SYMBOL(dump_skip);

#ifdef CONFIG_ELF_CORE
static int dump_emit_page(struct coredump_params *cprm, struct page *page)
{
	struct bio_vec bvec;
	struct iov_iter iter;
	struct file *file = cprm->file;
	loff_t pos;
	ssize_t n;

	if (!page)
		return 0;

	if (cprm->to_skip) {
		if (!__dump_skip(cprm, cprm->to_skip))
			return 0;
		cprm->to_skip = 0;
	}
	if (cprm->written + PAGE_SIZE > cprm->limit)
		return 0;
	if (dump_interrupted())
		return 0;
	pos = file->f_pos;
	bvec_set_page(&bvec, page, PAGE_SIZE, 0);
	iov_iter_bvec(&iter, ITER_SOURCE, &bvec, 1, PAGE_SIZE);
	n = __kernel_write_iter(cprm->file, &iter, &pos);
	if (n != PAGE_SIZE)
		return 0;
	file->f_pos = pos;
	cprm->written += PAGE_SIZE;
	cprm->pos += PAGE_SIZE;

	return 1;
}

/*
 * If we might get machine checks from kernel accesses during the
 * core dump, let's get those errors early rather than during the
 * IO. This is not performance-critical enough to warrant having
 * all the machine check logic in the iovec paths.
 */
#ifdef copy_mc_to_kernel

#define dump_page_alloc() alloc_page(GFP_KERNEL)
#define dump_page_free(x) __free_page(x)
static struct page *dump_page_copy(struct page *src, struct page *dst)
{
	void *buf = kmap_local_page(src);
	size_t left = copy_mc_to_kernel(page_address(dst), buf, PAGE_SIZE);
	kunmap_local(buf);
	return left ? NULL : dst;
}

#else

/* We just want to return non-NULL; it's never used. */
#define dump_page_alloc() ERR_PTR(-EINVAL)
#define dump_page_free(x) ((void)(x))
static inline struct page *dump_page_copy(struct page *src, struct page *dst)
{
	return src;
}
#endif

int dump_user_range(struct coredump_params *cprm, unsigned long start,
		    unsigned long len)
{
	unsigned long addr;
	struct page *dump_page;
	int locked, ret;

	dump_page = dump_page_alloc();
	if (!dump_page)
		return 0;

	ret = 0;
	locked = 0;
	for (addr = start; addr < start + len; addr += PAGE_SIZE) {
		struct page *page;

		if (!locked) {
			if (mmap_read_lock_killable(current->mm))
				goto out;
			locked = 1;
		}

		/*
		 * To avoid having to allocate page tables for virtual address
		 * ranges that have never been used yet, and also to make it
		 * easy to generate sparse core files, use a helper that returns
		 * NULL when encountering an empty page table entry that would
		 * otherwise have been filled with the zero page.
		 */
		page = get_dump_page(addr, &locked);
		if (page) {
			if (locked) {
				mmap_read_unlock(current->mm);
				locked = 0;
			}
			int stop = !dump_emit_page(cprm, dump_page_copy(page, dump_page));
			put_page(page);
			if (stop)
				goto out;
		} else {
			dump_skip(cprm, PAGE_SIZE);
		}

		if (dump_interrupted())
			goto out;

		if (!need_resched())
			continue;
		if (locked) {
			mmap_read_unlock(current->mm);
			locked = 0;
		}
		cond_resched();
	}
	ret = 1;
out:
	if (locked)
		mmap_read_unlock(current->mm);

	dump_page_free(dump_page);
	return ret;
}
#endif

int dump_align(struct coredump_params *cprm, int align)
{
	unsigned mod = (cprm->pos + cprm->to_skip) & (align - 1);
	if (align & (align - 1))
		return 0;
	if (mod)
		cprm->to_skip += align - mod;
	return 1;
}
EXPORT_SYMBOL(dump_align);

#ifdef CONFIG_SYSCTL

void validate_coredump_safety(void)
{
	if (suid_dumpable == SUID_DUMP_ROOT &&
	    core_pattern[0] != '/' && core_pattern[0] != '|' && core_pattern[0] != '@') {

		coredump_report_failure("Unsafe core_pattern used with fs.suid_dumpable=2: "
			"pipe handler or fully qualified core dump path required. "
			"Set kernel.core_pattern before fs.suid_dumpable.");
	}
}

static inline bool check_coredump_socket(void)
{
	const char *p;

	if (core_pattern[0] != '@')
		return true;

	/*
	 * Coredump socket must be located in the initial mount
	 * namespace. Don't give the impression that anything else is
	 * supported right now.
	 */
	if (current->nsproxy->mnt_ns != init_task.nsproxy->mnt_ns)
		return false;

	/* Must be an absolute path... */
	if (core_pattern[1] != '/') {
		/* ... or the socket request protocol... */
		if (core_pattern[1] != '@')
			return false;
		/* ... and if so must be an absolute path. */
		if (core_pattern[2] != '/')
			return false;
		p = &core_pattern[2];
	} else {
		p = &core_pattern[1];
	}

	/* The path obviously cannot exceed UNIX_PATH_MAX. */
	if (strlen(p) >= UNIX_PATH_MAX)
		return false;

	/* Must not contain ".." in the path. */
	if (name_contains_dotdot(core_pattern))
		return false;

	return true;
}

static int proc_dostring_coredump(const struct ctl_table *table, int write,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	int error;
	ssize_t retval;
	char old_core_pattern[CORENAME_MAX_SIZE];

	if (!write)
		return proc_dostring(table, write, buffer, lenp, ppos);

	retval = strscpy(old_core_pattern, core_pattern, CORENAME_MAX_SIZE);

	error = proc_dostring(table, write, buffer, lenp, ppos);
	if (error)
		return error;

	if (!check_coredump_socket()) {
		strscpy(core_pattern, old_core_pattern, retval + 1);
		return -EINVAL;
	}

	validate_coredump_safety();
	return error;
}

static const unsigned int core_file_note_size_min = CORE_FILE_NOTE_SIZE_DEFAULT;
static const unsigned int core_file_note_size_max = CORE_FILE_NOTE_SIZE_MAX;
static char core_modes[] = {
	"file\npipe"
#ifdef CONFIG_UNIX
	"\nsocket"
#endif
};

static const struct ctl_table coredump_sysctls[] = {
	{
		.procname	= "core_uses_pid",
		.data		= &core_uses_pid,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "core_pattern",
		.data		= core_pattern,
		.maxlen		= CORENAME_MAX_SIZE,
		.mode		= 0644,
		.proc_handler	= proc_dostring_coredump,
	},
	{
		.procname	= "core_pipe_limit",
		.data		= &core_pipe_limit,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_INT_MAX,
	},
	{
		.procname       = "core_file_note_size_limit",
		.data           = &core_file_note_size_limit,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= (unsigned int *)&core_file_note_size_min,
		.extra2		= (unsigned int *)&core_file_note_size_max,
	},
	{
		.procname	= "core_sort_vma",
		.data		= &core_sort_vma,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "core_modes",
		.data		= core_modes,
		.maxlen		= sizeof(core_modes) - 1,
		.mode		= 0444,
		.proc_handler	= proc_dostring,
	},
};

static int __init init_fs_coredump_sysctls(void)
{
	register_sysctl_init("kernel", coredump_sysctls);
	return 0;
}
fs_initcall(init_fs_coredump_sysctls);
#endif /* CONFIG_SYSCTL */

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

#define DUMP_SIZE_MAYBE_ELFHDR_PLACEHOLDER 1

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
	    vma->vm_pgoff == 0 && (vma->vm_flags & VM_READ)) {
		if ((READ_ONCE(file_inode(vma->vm_file)->i_mode) & 0111) != 0)
			return PAGE_SIZE;

		/*
		 * ELF libraries aren't always executable.
		 * We'll want to check whether the mapping starts with the ELF
		 * magic, but not now - we're holding the mmap lock,
		 * so copy_from_user() doesn't work here.
		 * Use a placeholder instead, and fix it up later in
		 * dump_vma_snapshot().
		 */
		return DUMP_SIZE_MAYBE_ELFHDR_PLACEHOLDER;
	}

#undef	FILTER

	return 0;

whole:
	return vma->vm_end - vma->vm_start;
}

/*
 * Helper function for iterating across a vma list.  It ensures that the caller
 * will visit `gate_vma' prior to terminating the search.
 */
static struct vm_area_struct *coredump_next_vma(struct vma_iterator *vmi,
				       struct vm_area_struct *vma,
				       struct vm_area_struct *gate_vma)
{
	if (gate_vma && (vma == gate_vma))
		return NULL;

	vma = vma_next(vmi);
	if (vma)
		return vma;
	return gate_vma;
}

static void free_vma_snapshot(struct coredump_params *cprm)
{
	if (cprm->vma_meta) {
		int i;
		for (i = 0; i < cprm->vma_count; i++) {
			struct file *file = cprm->vma_meta[i].file;
			if (file)
				fput(file);
		}
		kvfree(cprm->vma_meta);
		cprm->vma_meta = NULL;
	}
}

static int cmp_vma_size(const void *vma_meta_lhs_ptr, const void *vma_meta_rhs_ptr)
{
	const struct core_vma_metadata *vma_meta_lhs = vma_meta_lhs_ptr;
	const struct core_vma_metadata *vma_meta_rhs = vma_meta_rhs_ptr;

	if (vma_meta_lhs->dump_size < vma_meta_rhs->dump_size)
		return -1;
	if (vma_meta_lhs->dump_size > vma_meta_rhs->dump_size)
		return 1;
	return 0;
}

/*
 * Under the mmap_lock, take a snapshot of relevant information about the task's
 * VMAs.
 */
static bool dump_vma_snapshot(struct coredump_params *cprm)
{
	struct vm_area_struct *gate_vma, *vma = NULL;
	struct mm_struct *mm = current->mm;
	VMA_ITERATOR(vmi, mm, 0);
	int i = 0;

	/*
	 * Once the stack expansion code is fixed to not change VMA bounds
	 * under mmap_lock in read mode, this can be changed to take the
	 * mmap_lock in read mode.
	 */
	if (mmap_write_lock_killable(mm))
		return false;

	cprm->vma_data_size = 0;
	gate_vma = get_gate_vma(mm);
	cprm->vma_count = mm->map_count + (gate_vma ? 1 : 0);

	cprm->vma_meta = kvmalloc_array(cprm->vma_count, sizeof(*cprm->vma_meta), GFP_KERNEL);
	if (!cprm->vma_meta) {
		mmap_write_unlock(mm);
		return false;
	}

	while ((vma = coredump_next_vma(&vmi, vma, gate_vma)) != NULL) {
		struct core_vma_metadata *m = cprm->vma_meta + i;

		m->start = vma->vm_start;
		m->end = vma->vm_end;
		m->flags = vma->vm_flags;
		m->dump_size = vma_dump_size(vma, cprm->mm_flags);
		m->pgoff = vma->vm_pgoff;
		m->file = vma->vm_file;
		if (m->file)
			get_file(m->file);
		i++;
	}

	mmap_write_unlock(mm);

	for (i = 0; i < cprm->vma_count; i++) {
		struct core_vma_metadata *m = cprm->vma_meta + i;

		if (m->dump_size == DUMP_SIZE_MAYBE_ELFHDR_PLACEHOLDER) {
			char elfmag[SELFMAG];

			if (copy_from_user(elfmag, (void __user *)m->start, SELFMAG) ||
					memcmp(elfmag, ELFMAG, SELFMAG) != 0) {
				m->dump_size = 0;
			} else {
				m->dump_size = PAGE_SIZE;
			}
		}

		cprm->vma_data_size += m->dump_size;
	}

	if (core_sort_vma)
		sort(cprm->vma_meta, cprm->vma_count, sizeof(*cprm->vma_meta),
		     cmp_vma_size, NULL);

	return true;
}
