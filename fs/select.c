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
#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/rcupdate.h>

#include <asm/uaccess.h>

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

static struct poll_table_entry *poll_get_entry(poll_table *_p)
{
	struct poll_wqueues *p = container_of(_p, struct poll_wqueues, pt);
	struct poll_table_page *table = p->table;

	if (p->inline_index < N_INLINE_POLL_ENTRIES)
		return p->inline_entries + p->inline_index++;

	if (!table || POLL_TABLE_FULL(table)) {
		struct poll_table_page *new_table;

		new_table = (struct poll_table_page *) __get_free_page(GFP_KERNEL);
		if (!new_table) {
			p->error = -ENOMEM;
			__set_current_state(TASK_RUNNING);
			return NULL;
		}
		new_table->entry = new_table->entries;
		new_table->next = table;
		p->table = new_table;
		table = new_table;
	}

	return table->entry++;
}

/* Add a new entry */
static void __pollwait(struct file *filp, wait_queue_head_t *wait_address,
				poll_table *p)
{
	struct poll_table_entry *entry = poll_get_entry(p);
	if (!entry)
		return;
	get_file(filp);
	entry->filp = filp;
	entry->wait_address = wait_address;
	init_waitqueue_entry(&entry->wait, current);
	add_wait_queue(wait_address, &entry->wait);
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
	open_fds = fdt->open_fds->fds_bits+n;
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

#define BIT(i)		(1UL << ((i)&(__NFDBITS-1)))
#define MEM(i,m)	((m)+(unsigned)(i)/__NFDBITS)
#define ISSET(i,m)	(((i)&*(m)) != 0)
#define SET(i,m)	(*(m) |= (i))

#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

int do_select(int n, fd_set_bits *fds, s64 *timeout)
{
	struct poll_wqueues table;
	poll_table *wait;
	int retval, i;

	rcu_read_lock();
	retval = max_select_fd(n, fds);
	rcu_read_unlock();

	if (retval < 0)
		return retval;
	n = retval;

	poll_initwait(&table);
	wait = &table.pt;
	if (!*timeout)
		wait = NULL;
	retval = 0;
	for (;;) {
		unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;
		long __timeout;

		set_current_state(TASK_INTERRUPTIBLE);

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
					if (f_op && f_op->poll)
						mask = (*f_op->poll)(file, retval ? NULL : wait);
					fput_light(file, fput_needed);
					if ((mask & POLLIN_SET) && (in & bit)) {
						res_in |= bit;
						retval++;
					}
					if ((mask & POLLOUT_SET) && (out & bit)) {
						res_out |= bit;
						retval++;
					}
					if ((mask & POLLEX_SET) && (ex & bit)) {
						res_ex |= bit;
						retval++;
					}
				}
				cond_resched();
			}
			if (res_in)
				*rinp = res_in;
			if (res_out)
				*routp = res_out;
			if (res_ex)
				*rexp = res_ex;
		}
		wait = NULL;
		if (retval || !*timeout || signal_pending(current))
			break;
		if(table.error) {
			retval = table.error;
			break;
		}

		if (*timeout < 0) {
			/* Wait indefinitely */
			__timeout = MAX_SCHEDULE_TIMEOUT;
		} else if (unlikely(*timeout >= (s64)MAX_SCHEDULE_TIMEOUT - 1)) {
			/* Wait for longer than MAX_SCHEDULE_TIMEOUT. Do it in a loop */
			__timeout = MAX_SCHEDULE_TIMEOUT - 1;
			*timeout -= __timeout;
		} else {
			__timeout = *timeout;
			*timeout = 0;
		}
		__timeout = schedule_timeout(__timeout);
		if (*timeout >= 0)
			*timeout += __timeout;
	}
	__set_current_state(TASK_RUNNING);

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
#define MAX_SELECT_SECONDS \
	((unsigned long) (MAX_SCHEDULE_TIMEOUT / HZ)-1)

static int core_sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			   fd_set __user *exp, s64 *timeout)
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

	ret = do_select(n, &fds, timeout);

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

asmlinkage long sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			fd_set __user *exp, struct timeval __user *tvp)
{
	s64 timeout = -1;
	struct timeval tv;
	int ret;

	if (tvp) {
		if (copy_from_user(&tv, tvp, sizeof(tv)))
			return -EFAULT;

		if (tv.tv_sec < 0 || tv.tv_usec < 0)
			return -EINVAL;

		/* Cast to u64 to make GCC stop complaining */
		if ((u64)tv.tv_sec >= (u64)MAX_INT64_SECONDS)
			timeout = -1;	/* infinite */
		else {
			timeout = DIV_ROUND_UP(tv.tv_usec, USEC_PER_SEC/HZ);
			timeout += tv.tv_sec * HZ;
		}
	}

	ret = core_sys_select(n, inp, outp, exp, &timeout);

	if (tvp) {
		struct timeval rtv;

		if (current->personality & STICKY_TIMEOUTS)
			goto sticky;
		rtv.tv_usec = jiffies_to_usecs(do_div((*(u64*)&timeout), HZ));
		rtv.tv_sec = timeout;
		if (timeval_compare(&rtv, &tv) >= 0)
			rtv = tv;
		if (copy_to_user(tvp, &rtv, sizeof(rtv))) {
sticky:
			/*
			 * If an application puts its timeval in read-only
			 * memory, we don't want the Linux-specific update to
			 * the timeval to cause a fault after the select has
			 * completed successfully. However, because we're not
			 * updating the timeval, we can't restart the system
			 * call.
			 */
			if (ret == -ERESTARTNOHAND)
				ret = -EINTR;
		}
	}

	return ret;
}

#ifdef TIF_RESTORE_SIGMASK
asmlinkage long sys_pselect7(int n, fd_set __user *inp, fd_set __user *outp,
		fd_set __user *exp, struct timespec __user *tsp,
		const sigset_t __user *sigmask, size_t sigsetsize)
{
	s64 timeout = MAX_SCHEDULE_TIMEOUT;
	sigset_t ksigmask, sigsaved;
	struct timespec ts;
	int ret;

	if (tsp) {
		if (copy_from_user(&ts, tsp, sizeof(ts)))
			return -EFAULT;

		if (ts.tv_sec < 0 || ts.tv_nsec < 0)
			return -EINVAL;

		/* Cast to u64 to make GCC stop complaining */
		if ((u64)ts.tv_sec >= (u64)MAX_INT64_SECONDS)
			timeout = -1;	/* infinite */
		else {
			timeout = DIV_ROUND_UP(ts.tv_nsec, NSEC_PER_SEC/HZ);
			timeout += ts.tv_sec * HZ;
		}
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

	ret = core_sys_select(n, inp, outp, exp, &timeout);

	if (tsp) {
		struct timespec rts;

		if (current->personality & STICKY_TIMEOUTS)
			goto sticky;
		rts.tv_nsec = jiffies_to_usecs(do_div((*(u64*)&timeout), HZ)) *
						1000;
		rts.tv_sec = timeout;
		if (timespec_compare(&rts, &ts) >= 0)
			rts = ts;
		if (copy_to_user(tsp, &rts, sizeof(rts))) {
sticky:
			/*
			 * If an application puts its timeval in read-only
			 * memory, we don't want the Linux-specific update to
			 * the timeval to cause a fault after the select has
			 * completed successfully. However, because we're not
			 * updating the timeval, we can't restart the system
			 * call.
			 */
			if (ret == -ERESTARTNOHAND)
				ret = -EINTR;
		}
	}

	if (ret == -ERESTARTNOHAND) {
		/*
		 * Don't restore the signal mask yet. Let do_signal() deliver
		 * the signal on the way back to userspace, before the signal
		 * mask is restored.
		 */
		if (sigmask) {
			memcpy(&current->saved_sigmask, &sigsaved,
					sizeof(sigsaved));
			set_thread_flag(TIF_RESTORE_SIGMASK);
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
asmlinkage long sys_pselect6(int n, fd_set __user *inp, fd_set __user *outp,
	fd_set __user *exp, struct timespec __user *tsp, void __user *sig)
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

	return sys_pselect7(n, inp, outp, exp, tsp, up, sigsetsize);
}
#endif /* TIF_RESTORE_SIGMASK */

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
 * if non-NULL.
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
			if (file->f_op && file->f_op->poll)
				mask = file->f_op->poll(file, pwait);
			/* Mask out unneeded events. */
			mask &= pollfd->events | POLLERR | POLLHUP;
			fput_light(file, fput_needed);
		}
	}
	pollfd->revents = mask;

	return mask;
}

static int do_poll(unsigned int nfds,  struct poll_list *list,
		   struct poll_wqueues *wait, s64 *timeout)
{
	int count = 0;
	poll_table* pt = &wait->pt;

	/* Optimise the no-wait case */
	if (!(*timeout))
		pt = NULL;
 
	for (;;) {
		struct poll_list *walk;
		long __timeout;

		set_current_state(TASK_INTERRUPTIBLE);
		for (walk = list; walk != NULL; walk = walk->next) {
			struct pollfd * pfd, * pfd_end;

			pfd = walk->entries;
			pfd_end = pfd + walk->len;
			for (; pfd != pfd_end; pfd++) {
				/*
				 * Fish for events. If we found one, record it
				 * and kill the poll_table, so we don't
				 * needlessly register any other waiters after
				 * this. They'll get immediately deregistered
				 * when we break out and return.
				 */
				if (do_pollfd(pfd, pt)) {
					count++;
					pt = NULL;
				}
			}
		}
		/*
		 * All waiters have already been registered, so don't provide
		 * a poll_table to them on the next loop iteration.
		 */
		pt = NULL;
		if (count || !*timeout || signal_pending(current))
			break;
		count = wait->error;
		if (count)
			break;

		if (*timeout < 0) {
			/* Wait indefinitely */
			__timeout = MAX_SCHEDULE_TIMEOUT;
		} else if (unlikely(*timeout >= (s64)MAX_SCHEDULE_TIMEOUT-1)) {
			/*
			 * Wait for longer than MAX_SCHEDULE_TIMEOUT. Do it in
			 * a loop
			 */
			__timeout = MAX_SCHEDULE_TIMEOUT - 1;
			*timeout -= __timeout;
		} else {
			__timeout = *timeout;
			*timeout = 0;
		}

		__timeout = schedule_timeout(__timeout);
		if (*timeout >= 0)
			*timeout += __timeout;
	}
	__set_current_state(TASK_RUNNING);
	return count;
}

#define N_STACK_PPS ((sizeof(stack_pps) - sizeof(struct poll_list))  / \
			sizeof(struct pollfd))

int do_sys_poll(struct pollfd __user *ufds, unsigned int nfds, s64 *timeout)
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

	if (nfds > current->signal->rlim[RLIMIT_NOFILE].rlim_cur)
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
	fdcount = do_poll(nfds, head, &table, timeout);
	if (!fdcount && signal_pending(current))
		fdcount = -EINTR;
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

asmlinkage long sys_poll(struct pollfd __user *ufds, unsigned int nfds,
			long timeout_msecs)
{
	s64 timeout_jiffies;

	if (timeout_msecs > 0) {
#if HZ > 1000
		/* We can only overflow if HZ > 1000 */
		if (timeout_msecs / 1000 > (s64)0x7fffffffffffffffULL / (s64)HZ)
			timeout_jiffies = -1;
		else
#endif
			timeout_jiffies = msecs_to_jiffies(timeout_msecs);
	} else {
		/* Infinite (< 0) or no (0) timeout */
		timeout_jiffies = timeout_msecs;
	}

	return do_sys_poll(ufds, nfds, &timeout_jiffies);
}

#ifdef TIF_RESTORE_SIGMASK
asmlinkage long sys_ppoll(struct pollfd __user *ufds, unsigned int nfds,
	struct timespec __user *tsp, const sigset_t __user *sigmask,
	size_t sigsetsize)
{
	sigset_t ksigmask, sigsaved;
	struct timespec ts;
	s64 timeout = -1;
	int ret;

	if (tsp) {
		if (copy_from_user(&ts, tsp, sizeof(ts)))
			return -EFAULT;

		/* Cast to u64 to make GCC stop complaining */
		if ((u64)ts.tv_sec >= (u64)MAX_INT64_SECONDS)
			timeout = -1;	/* infinite */
		else {
			timeout = DIV_ROUND_UP(ts.tv_nsec, NSEC_PER_SEC/HZ);
			timeout += ts.tv_sec * HZ;
		}
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

	ret = do_sys_poll(ufds, nfds, &timeout);

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
			set_thread_flag(TIF_RESTORE_SIGMASK);
		}
		ret = -ERESTARTNOHAND;
	} else if (sigmask)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	if (tsp && timeout >= 0) {
		struct timespec rts;

		if (current->personality & STICKY_TIMEOUTS)
			goto sticky;
		/* Yes, we know it's actually an s64, but it's also positive. */
		rts.tv_nsec = jiffies_to_usecs(do_div((*(u64*)&timeout), HZ)) *
						1000;
		rts.tv_sec = timeout;
		if (timespec_compare(&rts, &ts) >= 0)
			rts = ts;
		if (copy_to_user(tsp, &rts, sizeof(rts))) {
		sticky:
			/*
			 * If an application puts its timeval in read-only
			 * memory, we don't want the Linux-specific update to
			 * the timeval to cause a fault after the select has
			 * completed successfully. However, because we're not
			 * updating the timeval, we can't restart the system
			 * call.
			 */
			if (ret == -ERESTARTNOHAND && timeout >= 0)
				ret = -EINTR;
		}
	}

	return ret;
}
#endif /* TIF_RESTORE_SIGMASK */
