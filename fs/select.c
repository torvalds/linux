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

#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/poll.h>
#include <linux/personality.h> /* for STICKY_TIMEOUTS */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/rcupdate.h>

#include <asm/uaccess.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))
#define DEFAULT_POLLMASK (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)

struct poll_table_entry {
	struct file * filp;
	wait_queue_t wait;
	wait_queue_head_t * wait_address;
};

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
}

EXPORT_SYMBOL(poll_initwait);

void poll_freewait(struct poll_wqueues *pwq)
{
	struct poll_table_page * p = pwq->table;
	while (p) {
		struct poll_table_entry * entry;
		struct poll_table_page *old;

		entry = p->entry;
		do {
			entry--;
			remove_wait_queue(entry->wait_address,&entry->wait);
			fput(entry->filp);
		} while (entry > p->entries);
		old = p;
		p = p->next;
		free_page((unsigned long) old);
	}
}

EXPORT_SYMBOL(poll_freewait);

static void __pollwait(struct file *filp, wait_queue_head_t *wait_address,
		       poll_table *_p)
{
	struct poll_wqueues *p = container_of(_p, struct poll_wqueues, pt);
	struct poll_table_page *table = p->table;

	if (!table || POLL_TABLE_FULL(table)) {
		struct poll_table_page *new_table;

		new_table = (struct poll_table_page *) __get_free_page(GFP_KERNEL);
		if (!new_table) {
			p->error = -ENOMEM;
			__set_current_state(TASK_RUNNING);
			return;
		}
		new_table->entry = new_table->entries;
		new_table->next = table;
		p->table = new_table;
		table = new_table;
	}

	/* Add a new entry */
	{
		struct poll_table_entry * entry = table->entry;
		table->entry = entry+1;
	 	get_file(filp);
	 	entry->filp = filp;
		entry->wait_address = wait_address;
		init_waitqueue_entry(&entry->wait, current);
		add_wait_queue(wait_address,&entry->wait);
	}
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

int do_select(int n, fd_set_bits *fds, long *timeout)
{
	struct poll_wqueues table;
	poll_table *wait;
	int retval, i;
	long __timeout = *timeout;

	rcu_read_lock();
	retval = max_select_fd(n, fds);
	rcu_read_unlock();

	if (retval < 0)
		return retval;
	n = retval;

	poll_initwait(&table);
	wait = &table.pt;
	if (!__timeout)
		wait = NULL;
	retval = 0;
	for (;;) {
		unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;

		set_current_state(TASK_INTERRUPTIBLE);

		inp = fds->in; outp = fds->out; exp = fds->ex;
		rinp = fds->res_in; routp = fds->res_out; rexp = fds->res_ex;

		for (i = 0; i < n; ++rinp, ++routp, ++rexp) {
			unsigned long in, out, ex, all_bits, bit = 1, mask, j;
			unsigned long res_in = 0, res_out = 0, res_ex = 0;
			struct file_operations *f_op = NULL;
			struct file *file = NULL;

			in = *inp++; out = *outp++; ex = *exp++;
			all_bits = in | out | ex;
			if (all_bits == 0) {
				i += __NFDBITS;
				continue;
			}

			for (j = 0; j < __NFDBITS; ++j, ++i, bit <<= 1) {
				if (i >= n)
					break;
				if (!(bit & all_bits))
					continue;
				file = fget(i);
				if (file) {
					f_op = file->f_op;
					mask = DEFAULT_POLLMASK;
					if (f_op && f_op->poll)
						mask = (*f_op->poll)(file, retval ? NULL : wait);
					fput(file);
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
		if (retval || !__timeout || signal_pending(current))
			break;
		if(table.error) {
			retval = table.error;
			break;
		}
		__timeout = schedule_timeout(__timeout);
	}
	__set_current_state(TASK_RUNNING);

	poll_freewait(&table);

	/*
	 * Up-to-date the caller timeout.
	 */
	*timeout = __timeout;
	return retval;
}

static void *select_bits_alloc(int size)
{
	return kmalloc(6 * size, GFP_KERNEL);
}

static void select_bits_free(void *bits, int size)
{
	kfree(bits);
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

asmlinkage long
sys_select(int n, fd_set __user *inp, fd_set __user *outp, fd_set __user *exp, struct timeval __user *tvp)
{
	fd_set_bits fds;
	char *bits;
	long timeout;
	int ret, size, max_fdset;
	struct fdtable *fdt;

	timeout = MAX_SCHEDULE_TIMEOUT;
	if (tvp) {
		time_t sec, usec;

		if (!access_ok(VERIFY_READ, tvp, sizeof(*tvp))
		    || __get_user(sec, &tvp->tv_sec)
		    || __get_user(usec, &tvp->tv_usec)) {
			ret = -EFAULT;
			goto out_nofds;
		}

		ret = -EINVAL;
		if (sec < 0 || usec < 0)
			goto out_nofds;

		if ((unsigned long) sec < MAX_SELECT_SECONDS) {
			timeout = ROUND_UP(usec, 1000000/HZ);
			timeout += sec * (unsigned long) HZ;
		}
	}

	ret = -EINVAL;
	if (n < 0)
		goto out_nofds;

	/* max_fdset can increase, so grab it once to avoid race */
	rcu_read_lock();
	fdt = files_fdtable(current->files);
	max_fdset = fdt->max_fdset;
	rcu_read_unlock();
	if (n > max_fdset)
		n = max_fdset;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words. 
	 */
	ret = -ENOMEM;
	size = FDS_BYTES(n);
	bits = select_bits_alloc(size);
	if (!bits)
		goto out_nofds;
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	if ((ret = get_fd_set(n, inp, fds.in)) ||
	    (ret = get_fd_set(n, outp, fds.out)) ||
	    (ret = get_fd_set(n, exp, fds.ex)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, &timeout);

	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		time_t sec = 0, usec = 0;
		if (timeout) {
			sec = timeout / HZ;
			usec = timeout % HZ;
			usec *= (1000000/HZ);
		}
		put_user(sec, &tvp->tv_sec);
		put_user(usec, &tvp->tv_usec);
	}

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
	select_bits_free(bits, size);
out_nofds:
	return ret;
}

struct poll_list {
	struct poll_list *next;
	int len;
	struct pollfd entries[0];
};

#define POLLFD_PER_PAGE  ((PAGE_SIZE-sizeof(struct poll_list)) / sizeof(struct pollfd))

static void do_pollfd(unsigned int num, struct pollfd * fdpage,
	poll_table ** pwait, int *count)
{
	int i;

	for (i = 0; i < num; i++) {
		int fd;
		unsigned int mask;
		struct pollfd *fdp;

		mask = 0;
		fdp = fdpage+i;
		fd = fdp->fd;
		if (fd >= 0) {
			struct file * file = fget(fd);
			mask = POLLNVAL;
			if (file != NULL) {
				mask = DEFAULT_POLLMASK;
				if (file->f_op && file->f_op->poll)
					mask = file->f_op->poll(file, *pwait);
				mask &= fdp->events | POLLERR | POLLHUP;
				fput(file);
			}
			if (mask) {
				*pwait = NULL;
				(*count)++;
			}
		}
		fdp->revents = mask;
	}
}

static int do_poll(unsigned int nfds,  struct poll_list *list,
			struct poll_wqueues *wait, long timeout)
{
	int count = 0;
	poll_table* pt = &wait->pt;

	if (!timeout)
		pt = NULL;
 
	for (;;) {
		struct poll_list *walk;
		set_current_state(TASK_INTERRUPTIBLE);
		walk = list;
		while(walk != NULL) {
			do_pollfd( walk->len, walk->entries, &pt, &count);
			walk = walk->next;
		}
		pt = NULL;
		if (count || !timeout || signal_pending(current))
			break;
		count = wait->error;
		if (count)
			break;
		timeout = schedule_timeout(timeout);
	}
	__set_current_state(TASK_RUNNING);
	return count;
}

asmlinkage long sys_poll(struct pollfd __user * ufds, unsigned int nfds, long timeout)
{
	struct poll_wqueues table;
 	int fdcount, err;
 	unsigned int i;
	struct poll_list *head;
 	struct poll_list *walk;
	struct fdtable *fdt;
	int max_fdset;

	/* Do a sanity check on nfds ... */
	rcu_read_lock();
	fdt = files_fdtable(current->files);
	max_fdset = fdt->max_fdset;
	rcu_read_unlock();
	if (nfds > max_fdset && nfds > OPEN_MAX)
		return -EINVAL;

	if (timeout) {
		/* Careful about overflow in the intermediate values */
		if ((unsigned long) timeout < MAX_SCHEDULE_TIMEOUT / HZ)
			timeout = (unsigned long)(timeout*HZ+999)/1000+1;
		else /* Negative or overflow */
			timeout = MAX_SCHEDULE_TIMEOUT;
	}

	poll_initwait(&table);

	head = NULL;
	walk = NULL;
	i = nfds;
	err = -ENOMEM;
	while(i!=0) {
		struct poll_list *pp;
		pp = kmalloc(sizeof(struct poll_list)+
				sizeof(struct pollfd)*
				(i>POLLFD_PER_PAGE?POLLFD_PER_PAGE:i),
					GFP_KERNEL);
		if(pp==NULL)
			goto out_fds;
		pp->next=NULL;
		pp->len = (i>POLLFD_PER_PAGE?POLLFD_PER_PAGE:i);
		if (head == NULL)
			head = pp;
		else
			walk->next = pp;

		walk = pp;
		if (copy_from_user(pp->entries, ufds + nfds-i, 
				sizeof(struct pollfd)*pp->len)) {
			err = -EFAULT;
			goto out_fds;
		}
		i -= pp->len;
	}
	fdcount = do_poll(nfds, head, &table, timeout);

	/* OK, now copy the revents fields back to user space. */
	walk = head;
	err = -EFAULT;
	while(walk != NULL) {
		struct pollfd *fds = walk->entries;
		int j;

		for (j=0; j < walk->len; j++, ufds++) {
			if(__put_user(fds[j].revents, &ufds->revents))
				goto out_fds;
		}
		walk = walk->next;
  	}
	err = fdcount;
	if (!fdcount && signal_pending(current))
		err = -EINTR;
out_fds:
	walk = head;
	while(walk!=NULL) {
		struct poll_list *pp = walk->next;
		kfree(walk);
		walk = pp;
	}
	poll_freewait(&table);
	return err;
}
