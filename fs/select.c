/*
 * This file contains the procedures for the handling of select and poll
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 *  4 February 1994
 *     COFF/ELF binary emulation. If the process has the STICKY_TIMEOUTS
 *     flag set in its personality we do *not* modify the given timeout
 *     parameter to reflect time remaining.
 *
 *  24 January 2000
 *     Changed sys_poll()/do_poll() to use PAGE_SIZE chunk-based allocation 
 *     of fds to overcome nfds < 16390 descriptors limit (Tigran Aivazian).
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/rcupdate.h>
#include <linux/hrtimer.h>

#include <asm/uaccess.h>


/*
 * Estimate expected accuracy in ns from a timeval.
 *
 * After quite a bit of churning around, we've settled on
 * a simple thing of taking 0.1% of the timeout as the
 * slack, with a cap of 100 msec.
 * "nice" tasks get a 0.5% slack instead.
 *
 * Consider this comment an open invitation to come up with even
 * better solutions..
 */

#define MAX_SLACK	(100 * NSEC_PER_MSEC)

static long __estimate_accuracy(struct timespec *tv)
{
	long slack;
	int divfactor = 1000;

	if (tv->tv_sec < 0)
		return 0;

	if (task_nice(current) > 0)
		divfactor = divfactor / 5;

	if (tv->tv_sec > MAX_SLACK / (NSEC_PER_SEC/divfactor))
		return MAX_SLACK;

	slack = tv->tv_nsec / divfactor;
	slack += tv->tv_sec * (NSEC_PER_SEC/divfactor);

	if (slack > MAX_SLACK)
		return MAX_SLACK;

	return slack;
}

long select_estimate_accuracy(struct timespec *tv)
{
	unsigned long ret;
	struct timespec now;

	/*
	 * Realtime tasks get a slack of 0 for obvious reasons.
	 */

	if (rt_task(current))
		return 0;

	ktime_get_ts(&now);
	now = timespec_sub(*tv, now);
	ret = __estimate_accuracy(&now);
	if (ret < current->timer_slack_ns)
		return current->timer_slack_ns;
	return ret;
}



struct poll_table_page {
	struct poll_table_page * next;
	struct poll_table_entry * entry;
	struct poll_table_entry entries[0];
};

#define POLL_TABLE_FULL(table) \
	((unsigned long)((table)->entry+1) > PAGE_SIZE + (unsigned long)(table))

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux
 * sleep/wakeup mechanism works.
 *
 * Two very simple procedures, poll_wait() and poll_freewait() make all the
 * work.  poll_wait() is an inline-function defined in <linux/poll.h>,
 * as all select/poll functions have to call it to add an entry to the
 * poll table.
 */
static void __pollwait(struct file *filp, wait_queue_head_t *wait_address,
		       poll_table *p);

void poll_initwait(struct poll_wqueues *pwq)
{
	init_poll_funcptr(&pwq->pt, __pollwait);
	pwq->polling_task = current;
	pwq->triggered = 0;
	pwq->error = 0;
	pwq->table = NULL;
	pwq->inline_index = 0;
}
EXPORT_SYMBOL(poll_initwait);

static void free_poll_entry(struct poll_table_entry *entry)
{
	remove_wait_queue(entry->wait_address, &entry->wait);
	fput(entry->filp);
}

void poll_freewait(struct poll_wqueues *pwq)
{
	struct poll_table_page * p = pwq->table;
	int i;
	for (i = 0; i < pwq->inline_index; i++)
		free_poll_entry(pwq->inline_entries + i);
	while (p) {
		struct poll_table_entry * entry;
		struct poll_table_page *old;

		entry = p->entry;
		do {
			entry--;
			free_poll_entry(entry);
		} while (entry > p->entries);
		old = p;
		p = p->next;
		free_page((unsigned long) old);
	}
}
EXPORT_SYMBOL(poll_freewait);

static struct poll_table_entry *poll_get_entry(struct poll_wqueues *p)
{
	struct poll_table_page *table = p->table;

	if (p->inline_index < N_INLINE_POLL_ENTRIES)
		return p->inline_entries + p->inline_index++;

	if (!table || POLL_TABLE_FULL(table)) {
		struct poll_table_page *new_table;

		new_table = (struct poll_table_page *) __get_free_page(GFP_KERNEL);
		if (!new_table) {
			p->error = -ENOMEM;
			return NULL;
		}
		new_table->entry = new_table->entries;
		new_table->next = table;
		p->table = new_table;
		table = new_table;
	}

	return table->entry++;
}

static int __pollwake(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	struct poll_wqueues *pwq = wait->private;
	DECLARE_WAITQUEUE(dummy_wait, pwq->polling_task);

	/*
	 * Although this function is called under waitqueue lock, LOCK
	 * doesn't imply write barrier and the users expect write
	 * barrier semantics on wakeup functions.  The following
	 * smp_wmb() is equivalent to smp_wmb() in try_to_wake_up()
	 * and is paired with set_mb() in poll_schedule_timeout.
	 */
	smp_wmb();
	pwq->triggered = 1;

	/*
	 * Perform the default wake up operation using a dummy
	 * waitqueue.
	 *
	 * TODO: This is hacky but there currently is no interface to
	 * pass in @sync.  @sync is scheduled to be removed and once
	 * that happens, wake_up_process() can be used directly.
	 */
	return default_wake_function(&dummy_wait, mode, sync, key);
}

static int pollwake(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	struct poll_table_entry *entry;

	entry = container_of(wait, struct poll_table_entry, wait);
	if (key && !((unsigned long)key & entry->key))
		return 0;
	return __pollwake(wait, mode, sync, key);
}

/* Add a new entry */
static void __pollwait(struct file *filp, wait_queue_head_t *wait_address,
				poll_table *p)
{
	struct poll_wqueues *pwq = container_of(p, struct poll_wqueues, pt);
	struct poll_table_entry *entry = poll_get_entry(pwq);
	if (!entry)
		return;
	get_file(filp);
	entry->filp = filp;
	entry->wait_address = wait_address;
	entry->key = p->_key;
	init_waitqueue_func_entry(&entry->wait, pollwake);
	entry->wait.private = pwq;
	add_wait_queue(wait_address, &entry->wait);
}

int poll_schedule_timeout(struct poll_wqueues *pwq, int state,
			  ktime_t *expires, unsigned long slack)
{
	int rc = -EINTR;

	set_current_state(state);
	if (!pwq->triggered)
		rc = schedule_hrtimeout_range(expires, slack, HRTIMER_MODE_ABS);
	__set_current_state(TASK_RUNNING);

	/*
	 * Prepare for the next iteration.
	 *
	 * The following set_mb() serves two purposes.  First, it's
	 * the counterpart rmb of the wmb in pollwake() such that data
	 * written before wake up is always visible after wake up.
	 * Second, the full barrier guarantees that triggered clearing
	 * doesn't pass event check of the next iteration.  Note that
	 * this problem doesn't exist for the first iteration as
	 * add_wait_queue() has full barrier semantics.
	 */
	set_mb(pwq->triggered, 0);

	return rc;
}
EXPORT_SYMBOL(poll_schedule_timeout);

/**
 * poll_select_set_timeout - helper function to setup the timeout value
 * @to:		pointer to timespec variable for the final timeout
 * @sec:	seconds (from user space)
 * @nsec:	nanoseconds (from user space)
 *
 * Note, we do not use a timespec for the user space value here, That
 * way we can use the function for timeval and compat interfaces as well.
 *
 * Returns -EINVAL if sec/nsec are not normalized. Otherwise 0.
 */
int poll_select_set_timeout(struct timespec *to, long sec, long nsec)
{
	struct timespec ts = {.tv_sec = sec, .tv_nsec = nsec};

	if (!timespec_valid(&ts))
		return -EINVAL;

	/* Optimize for the zero timeout value here */
	if (!sec && !nsec) {
		to->tv_sec = to->tv_nsec = 0;
	} else {
		ktime_get_ts(to);
		*to = timespec_add_safe(*to, ts);
	}
	return 0;
}

static int poll_select_copy_remaining(struct timespec *end_time, void __user *p,
				      int timeval, int ret)
{
	struct timespec rts;
	struct timeval rtv;

	if (!p)
		return ret;

	if (current->personality & STICKY_TIMEOUTS)
		goto sticky;

	/* No update for zero timeout */
	if (!end_time->tv_sec && !end_time->tv_nsec)
		return ret;

	ktime_get_ts(&rts);
	rts = timespec_sub(*end_time, rts);
	if (rts.tv_sec < 0)
		rts.tv_sec = rts.tv_nsec = 0;

	if (timeval) {
		if (sizeof(rtv) > sizeof(rtv.tv_sec) + sizeof(rtv.tv_usec))
			memset(&rtv, 0, sizeof(rtv));
		rtv.tv_sec = rts.tv_sec;
		rtv.tv_usec = rts.tv_nsec / NSEC_PER_USEC;

		if (!copy_to_user(p, &rtv, sizeof(rtv)))
			return ret;

	} else if (!copy_to_user(p, &rts, sizeof(rts)))
		return ret;

	/*
	 * If an application puts its timeval in read-only memory, we
	 * don't want the Linux-specific update to the timeval to
	 * cause a fault after the select has completed
	 * successfully. However, because we're not updating the
	 * timeval, we can't restart the system call.
	 */

sticky:
	if (ret == -ERESTARTNOHAND)
		ret = -EINTR;
	return ret;
}

#define FDS_IN(fds, n)		(fds->in + n)
#define FDS_OUT(fds, n)		(fds->out + n)
#define FDS_EX(fds, n)		(fds->ex + n)

#define BITS(fds, n)	(*FDS_IN(fds, n)|*FDS_OUT(fds, n)|*FDS_EX(fds, n))

static int max_select_fd(unsigned long n, fd_set_bits *fds)
{
	unsigned long *open_fds;
	unsigned long set;
	int max;
	struct fdtable *fdt;

	/* handle last in-complete long-word first */
	set = ~(~0UL << (n & (__NFDBITS-1)));
	n /= __NFDBITS;
	fdt = files_fdtable(current->files);
	open_fds = fdt->open_fds + n;
	max = 0;
	if (set) {
		set &= BITS(fds, n);
		if (set) {
			if (!(set & ~*open_fds))
				goto get_max;
			return -EBADF;
		}
	}
	while (n) {
		open_fds--;
		n--;
		set = BITS(fds, n);
		if (!set)
			continue;
		if (set & ~*open_fds)
			return -EBADF;
		if (max)
			continue;
get_max:
		do {
			max++;
			set >>= 1;
		} while (set);
		max += n * __NFDBITS;
	}

	return max;
}

#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

static inline void wait_key_set(poll_table *wait, unsigned long in,
				unsigned long out, unsigned long bit)
{
	wait->_key = POLLEX_SET;
	if (in & bit)
		wait->_key |= POLLIN_SET;
	if (out & bit)
		wait->_key |= POLLOUT_SET;
}

int do_select(int n, fd_set_bits *fds, struct timespec *end_time)
{
	ktime_t expire, *to = NULL;
	struct poll_wqueues table;
	poll_table *wait;
	int retval, i, timed_out = 0;
	unsigned long slack = 0;

	rcu_read_lock();
	retval = max_select_fd(n, fds);
	rcu_read_unlock();

	if (retval < 0)
		return retval;
	n = retval;

	poll_initwait(&table);
	wait = &table.pt;
	if (end_time && !end_time->tv_sec && !end_time->tv_nsec) {
		wait->_qproc = NULL;
		timed_out = 1;
	}

	if (end_time && !timed_out)
		slack = select_estimate_accuracy(end_time);

	retval = 0;
	for (;;) {
		unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;

		inp = fds->in; outp = fds->out; exp = fds->ex;
		rinp = fds->res_in; routp = fds->res_out; rexp = fds->res_ex;

		for (i = 0; i < n; ++rinp, ++routp, ++rexp) {
			unsigned long in, out, ex, all_bits, bit = 1, mask, j;
			unsigned long res_in = 0, res_out = 0, res_ex = 0;
			const struct file_operations *f_op = NULL;
			struct file *file = NULL;

			in = *inp++; out = *outp++; ex = *exp++;
			all_bits = in | out | ex;
			if (all_bits == 0) {
				i += __NFDBITS;
				continue;
			}

			for (j = 0; j < __NFDBITS; ++j, ++i, bit <<= 1) {
				int fput_needed;
				if (i >= n)
					break;
				if (!(bit & all_bits))
					continue;
				file = fget_light(i, &fput_needed);
				if (file) {
					f_op = file->f_op;
					mask = DEFAULT_POLLMASK;
					if (f_op && f_op->poll) {
						wait_key_set(wait, in, out, bit);
						mask = (*f_op->poll)(file, wait);
					}
					fput_light(file, fput_needed);
					if ((mask & POLLIN_SET) && (in & bit)) {
						res_in |= bit;
						retval++;
						wait->_qproc = NULL;
					}
					if ((mask & POLLOUT_SET) && (out & bit)) {
						res_out |= bit;
						retval++;
						wait->_qproc = NULL;
					}
					if ((mask & POLLEX_SET) && (ex & bit)) {
						res_ex |= bit;
						retval++;
						wait->_qproc = NULL;
					}
				}
			}
			if (res_in)
				*rinp = res_in;
			if (res_out)
				*routp = res_out;
			if (res_ex)
				*rexp = res_ex;
			cond_resched();
		}
		wait->_qproc = NULL;
		if (retval || timed_out || signal_pending(current))
			break;
		if (table.error) {
			retval = table.error;
			break;
		}

		/*
		 * If this is the first loop and we have a timeout
		 * given, then we convert to ktime_t and set the to
		 * pointer to the expiry value.
		 */
		if (end_time && !to) {
			expire = timespec_to_ktime(*end_time);
			to = &expire;
		}

		if (!poll_schedule_timeout(&table, TASK_INTERRUPTIBLE,
					   to, slack))
			timed_out = 1;
	}

	poll_freewait(&table);

	return retval;
}

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
int core_sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			   fd_set __user *exp, struct timespec *end_time)
{
	fd_set_bits fds;
	void *bits;
	int ret, max_fds;
	unsigned int size;
	struct fdtable *fdt;
	/* Allocate small arguments on the stack to save memory and be faster */
	long stack_fds[SELECT_STACK_ALLOC/sizeof(long)];

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	/* max_fds can increase, so grab it once to avoid race */
	rcu_read_lock();
	fdt = files_fdtable(current->files);
	max_fds = fdt->max_fds;
	rcu_read_unlock();
	if (n > max_fds)
		n = max_fds;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	size = FDS_BYTES(n);
	bits = stack_fds;
	if (size > sizeof(stack_fds) / 6) {
		/* Not enough space in on-stack array; must use kmalloc */
		ret = -ENOMEM;
		bits = kmalloc(6 * size, GFP_KERNEL);
		if (!bits)
			goto out_nofds;
	}
	fds.in      = bits;
	fds.out     = bits +   size;
	fds.ex      = bits + 2*size;
	fds.res_in  = bits + 3*size;
	fds.res_out = bits + 4*size;
	fds.res_ex  = bits + 5*size;

	if ((ret = get_fd_set(n, inp, fds.in)) ||
	    (ret = get_fd_set(n, outp, fds.out)) ||
	    (ret = get_fd_set(n, exp, fds.ex)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, end_time);

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	if (set_fd_set(n, inp, fds.res_in) ||
	    set_fd_set(n, outp, fds.res_out) ||
	    set_fd_set(n, exp, fds.res_ex))
		ret = -EFAULT;

out:
	if (bits != stack_fds)
		kfree(bits);
out_nofds:
	return ret;
}

SYSCALL_DEFINE5(select, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct timeval __user *, tvp)
{
	struct timespec end_time, *to = NULL;
	struct timeval tv;
	int ret;

	if (tvp) {
		if (copy_from_user(&tv, tvp, sizeof(tv)))
			return -EFAULT;

		to = &end_time;
		if (poll_select_set_timeout(to,
				tv.tv_sec + (tv.tv_usec / USEC_PER_SEC),
				(tv.tv_usec % USEC_PER_SEC) * NSEC_PER_USEC))
			return -EINVAL;
	}

	ret = core_sys_select(n, inp, outp, exp, to);
	ret = poll_select_copy_remaining(&end_time, tvp, 1, ret);

	return ret;
}

static long do_pselect(int n, fd_set __user *inp, fd_set __user *outp,
		       fd_set __user *exp, struct timespec __user *tsp,
		       const sigset_t __user *sigmask, size_t sigsetsize)
{
	sigset_t ksigmask, sigsaved;
	struct timespec ts, end_time, *to = NULL;
	int ret;

	if (tsp) {
		if (copy_from_user(&ts, tsp, sizeof(ts)))
			return -EFAULT;

		to = &end_time;
		if (poll_select_set_timeout(to, ts.tv_sec, ts.tv_nsec))
			return -EINVAL;
	}

	if (sigmask) {
		/* XXX: Don't preclude handling different sized sigset_t's.  */
		if (sigsetsize != sizeof(sigset_t))
			return -EINVAL;
		if (copy_from_user(&ksigmask, sigmask, sizeof(ksigmask)))
			return -EFAULT;

		sigdelsetmask(&ksigmask, sigmask(SIGKILL)|sigmask(SIGSTOP));
		sigprocmask(SIG_SETMASK, &ksigmask, &sigsaved);
	}

	ret = core_sys_select(n, inp, outp, exp, to);
	ret = poll_select_copy_remaining(&end_time, tsp, 0, ret);

	if (ret == -ERESTARTNOHAND) {
		/*
		 * Don't restore the signal mask yet. Let do_signal() deliver
		 * the signal on the way back to userspace, before the signal
		 * mask is restored.
		 */
		if (sigmask) {
			memcpy(&current->saved_sigmask, &sigsaved,
					sizeof(sigsaved));
			set_restore_sigmask();
		}
	} else if (sigmask)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	return ret;
}

/*
 * Most architectures can't handle 7-argument syscalls. So we provide a
 * 6-argument version where the sixth argument is a pointer to a structure
 * which has a pointer to the sigset_t itself followed by a size_t containing
 * the sigset size.
 */
SYSCALL_DEFINE6(pselect6, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct timespec __user *, tsp,
		void __user *, sig)
{
	size_t sigsetsize = 0;
	sigset_t __user *up = NULL;

	if (sig) {
		if (!access_ok(VERIFY_READ, sig, sizeof(void *)+sizeof(size_t))
		    || __get_user(up, (sigset_t __user * __user *)sig)
		    || __get_user(sigsetsize,
				(size_t __user *)(sig+sizeof(void *))))
			return -EFAULT;
	}

	return do_pselect(n, inp, outp, exp, tsp, up, sigsetsize);
}

#ifdef __ARCH_WANT_SYS_OLD_SELECT
struct sel_arg_struct {
	unsigned long n;
	fd_set __user *inp, *outp, *exp;
	struct timeval __user *tvp;
};

SYSCALL_DEFINE1(old_select, struct sel_arg_struct __user *, arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}
#endif

struct poll_list {
	struct poll_list *next;
	int len;
	struct pollfd entries[0];
};

#define POLLFD_PER_PAGE  ((PAGE_SIZE-sizeof(struct poll_list)) / sizeof(struct pollfd))

/*
 * Fish for pollable events on the pollfd->fd file descriptor. We're only
 * interested in events matching the pollfd->events mask, and the result
 * matching that mask is both recorded in pollfd->revents and returned. The
 * pwait poll_table will be used by the fd-provided poll handler for waiting,
 * if pwait->_qproc is non-NULL.
 */
static inline unsigned int do_pollfd(struct pollfd *pollfd, poll_table *pwait)
{
	unsigned int mask;
	int fd;

	mask = 0;
	fd = pollfd->fd;
	if (fd >= 0) {
		int fput_needed;
		struct file * file;

		file = fget_light(fd, &fput_needed);
		mask = POLLNVAL;
		if (file != NULL) {
			mask = DEFAULT_POLLMASK;
			if (file->f_op && file->f_op->poll) {
				pwait->_key = pollfd->events|POLLERR|POLLHUP;
				mask = file->f_op->poll(file, pwait);
			}
			/* Mask out unneeded events. */
			mask &= pollfd->events | POLLERR | POLLHUP;
			fput_light(file, fput_needed);
		}
	}
	pollfd->revents = mask;

	return mask;
}

static int do_poll(unsigned int nfds,  struct poll_list *list,
		   struct poll_wqueues *wait, struct timespec *end_time)
{
	poll_table* pt = &wait->pt;
	ktime_t expire, *to = NULL;
	int timed_out = 0, count = 0;
	unsigned long slack = 0;

	/* Optimise the no-wait case */
	if (end_time && !end_time->tv_sec && !end_time->tv_nsec) {
		pt->_qproc = NULL;
		timed_out = 1;
	}

	if (end_time && !timed_out)
		slack = select_estimate_accuracy(end_time);

	for (;;) {
		struct poll_list *walk;

		for (walk = list; walk != NULL; walk = walk->next) {
			struct pollfd * pfd, * pfd_end;

			pfd = walk->entries;
			pfd_end = pfd + walk->len;
			for (; pfd != pfd_end; pfd++) {
				/*
				 * Fish for events. If we found one, record it
				 * and kill poll_table->_qproc, so we don't
				 * needlessly register any other waiters after
				 * this. They'll get immediately deregistered
				 * when we break out and return.
				 */
				if (do_pollfd(pfd, pt)) {
					count++;
					pt->_qproc = NULL;
				}
			}
		}
		/*
		 * All waiters have already been registered, so don't provide
		 * a poll_table->_qproc to them on the next loop iteration.
		 */
		pt->_qproc = NULL;
		if (!count) {
			count = wait->error;
			if (signal_pending(current))
				count = -EINTR;
		}
		if (count || timed_out)
			break;

		/*
		 * If this is the first loop and we have a timeout
		 * given, then we convert to ktime_t and set the to
		 * pointer to the expiry value.
		 */
		if (end_time && !to) {
			expire = timespec_to_ktime(*end_time);
			to = &expire;
		}

		if (!poll_schedule_timeout(wait, TASK_INTERRUPTIBLE, to, slack))
			timed_out = 1;
	}
	return count;
}

#define N_STACK_PPS ((sizeof(stack_pps) - sizeof(struct poll_list))  / \
			sizeof(struct pollfd))

int do_sys_poll(struct pollfd __user *ufds, unsigned int nfds,
		struct timespec *end_time)
{
	struct poll_wqueues table;
 	int err = -EFAULT, fdcount, len, size;
	/* Allocate small arguments on the stack to save memory and be
	   faster - use long to make sure the buffer is aligned properly
	   on 64 bit archs to avoid unaligned access */
	long stack_pps[POLL_STACK_ALLOC/sizeof(long)];
	struct poll_list *const head = (struct poll_list *)stack_pps;
 	struct poll_list *walk = head;
 	unsigned long todo = nfds;

	if (nfds > rlimit(RLIMIT_NOFILE))
		return -EINVAL;

	len = min_t(unsigned int, nfds, N_STACK_PPS);
	for (;;) {
		walk->next = NULL;
		walk->len = len;
		if (!len)
			break;

		if (copy_from_user(walk->entries, ufds + nfds-todo,
					sizeof(struct pollfd) * walk->len))
			goto out_fds;

		todo -= walk->len;
		if (!todo)
			break;

		len = min(todo, POLLFD_PER_PAGE);
		size = sizeof(struct poll_list) + sizeof(struct pollfd) * len;
		walk = walk->next = kmalloc(size, GFP_KERNEL);
		if (!walk) {
			err = -ENOMEM;
			goto out_fds;
		}
	}

	poll_initwait(&table);
	fdcount = do_poll(nfds, head, &table, end_time);
	poll_freewait(&table);

	for (walk = head; walk; walk = walk->next) {
		struct pollfd *fds = walk->entries;
		int j;

		for (j = 0; j < walk->len; j++, ufds++)
			if (__put_user(fds[j].revents, &ufds->revents))
				goto out_fds;
  	}

	err = fdcount;
out_fds:
	walk = head->next;
	while (walk) {
		struct poll_list *pos = walk;
		walk = walk->next;
		kfree(pos);
	}

	return err;
}

static long do_restart_poll(struct restart_block *restart_block)
{
	struct pollfd __user *ufds = restart_block->poll.ufds;
	int nfds = restart_block->poll.nfds;
	struct timespec *to = NULL, end_time;
	int ret;

	if (restart_block->poll.has_timeout) {
		end_time.tv_sec = restart_block->poll.tv_sec;
		end_time.tv_nsec = restart_block->poll.tv_nsec;
		to = &end_time;
	}

	ret = do_sys_poll(ufds, nfds, to);

	if (ret == -EINTR) {
		restart_block->fn = do_restart_poll;
		ret = -ERESTART_RESTARTBLOCK;
	}
	return ret;
}

SYSCALL_DEFINE3(poll, struct pollfd __user *, ufds, unsigned int, nfds,
		int, timeout_msecs)
{
	struct timespec end_time, *to = NULL;
	int ret;

	if (timeout_msecs >= 0) {
		to = &end_time;
		poll_select_set_timeout(to, timeout_msecs / MSEC_PER_SEC,
			NSEC_PER_MSEC * (timeout_msecs % MSEC_PER_SEC));
	}

	ret = do_sys_poll(ufds, nfds, to);

	if (ret == -EINTR) {
		struct restart_block *restart_block;

		restart_block = &current_thread_info()->restart_block;
		restart_block->fn = do_restart_poll;
		restart_block->poll.ufds = ufds;
		restart_block->poll.nfds = nfds;

		if (timeout_msecs >= 0) {
			restart_block->poll.tv_sec = end_time.tv_sec;
			restart_block->poll.tv_nsec = end_time.tv_nsec;
			restart_block->poll.has_timeout = 1;
		} else
			restart_block->poll.has_timeout = 0;

		ret = -ERESTART_RESTARTBLOCK;
	}
	return ret;
}

SYSCALL_DEFINE5(ppoll, struct pollfd __user *, ufds, unsigned int, nfds,
		struct timespec __user *, tsp, const sigset_t __user *, sigmask,
		size_t, sigsetsize)
{
	sigset_t ksigmask, sigsaved;
	struct timespec ts, end_time, *to = NULL;
	int ret;

	if (tsp) {
		if (copy_from_user(&ts, tsp, sizeof(ts)))
			return -EFAULT;

		to = &end_time;
		if (poll_select_set_timeout(to, ts.tv_sec, ts.tv_nsec))
			return -EINVAL;
	}

	if (sigmask) {
		/* XXX: Don't preclude handling different sized sigset_t's.  */
		if (sigsetsize != sizeof(sigset_t))
			return -EINVAL;
		if (copy_from_user(&ksigmask, sigmask, sizeof(ksigmask)))
			return -EFAULT;

		sigdelsetmask(&ksigmask, sigmask(SIGKILL)|sigmask(SIGSTOP));
		sigprocmask(SIG_SETMASK, &ksigmask, &sigsaved);
	}

	ret = do_sys_poll(ufds, nfds, to);

	/* We can restart this syscall, usually */
	if (ret == -EINTR) {
		/*
		 * Don't restore the signal mask yet. Let do_signal() deliver
		 * the signal on the way back to userspace, before the signal
		 * mask is restored.
		 */
		if (sigmask) {
			memcpy(&current->saved_sigmask, &sigsaved,
					sizeof(sigsaved));
			set_restore_sigmask();
		}
		ret = -ERESTARTNOHAND;
	} else if (sigmask)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	ret = poll_select_copy_remaining(&end_time, tsp, 0, ret);

	return ret;
}
