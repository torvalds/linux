/*
 *  fs/eventpoll.c (Efficent event polling implementation)
 *  Copyright (C) 2001,...,2007	 Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/spinlock.h>
#include <linux/syscalls.h>
#include <linux/rbtree.h>
#include <linux/wait.h>
#include <linux/eventpoll.h>
#include <linux/mount.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/anon_inodes.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/mman.h>
#include <asm/atomic.h>

/*
 * LOCKING:
 * There are three level of locking required by epoll :
 *
 * 1) epmutex (mutex)
 * 2) ep->mtx (mutex)
 * 3) ep->lock (spinlock)
 *
 * The acquire order is the one listed above, from 1 to 3.
 * We need a spinlock (ep->lock) because we manipulate objects
 * from inside the poll callback, that might be triggered from
 * a wake_up() that in turn might be called from IRQ context.
 * So we can't sleep inside the poll callback and hence we need
 * a spinlock. During the event transfer loop (from kernel to
 * user space) we could end up sleeping due a copy_to_user(), so
 * we need a lock that will allow us to sleep. This lock is a
 * mutex (ep->mtx). It is acquired during the event transfer loop,
 * during epoll_ctl(EPOLL_CTL_DEL) and during eventpoll_release_file().
 * Then we also need a global mutex to serialize eventpoll_release_file()
 * and ep_free().
 * This mutex is acquired by ep_free() during the epoll file
 * cleanup path and it is also acquired by eventpoll_release_file()
 * if a file has been pushed inside an epoll set and it is then
 * close()d without a previous call toepoll_ctl(EPOLL_CTL_DEL).
 * It is possible to drop the "ep->mtx" and to use the global
 * mutex "epmutex" (together with "ep->lock") to have it working,
 * but having "ep->mtx" will make the interface more scalable.
 * Events that require holding "epmutex" are very rare, while for
 * normal operations the epoll private "ep->mtx" will guarantee
 * a better scalability.
 */

#define DEBUG_EPOLL 0

#if DEBUG_EPOLL > 0
#define DPRINTK(x) printk x
#define DNPRINTK(n, x) do { if ((n) <= DEBUG_EPOLL) printk x; } while (0)
#else /* #if DEBUG_EPOLL > 0 */
#define DPRINTK(x) (void) 0
#define DNPRINTK(n, x) (void) 0
#endif /* #if DEBUG_EPOLL > 0 */

#define DEBUG_EPI 0

#if DEBUG_EPI != 0
#define EPI_SLAB_DEBUG (SLAB_DEBUG_FREE | SLAB_RED_ZONE /* | SLAB_POISON */)
#else /* #if DEBUG_EPI != 0 */
#define EPI_SLAB_DEBUG 0
#endif /* #if DEBUG_EPI != 0 */

/* Epoll private bits inside the event mask */
#define EP_PRIVATE_BITS (EPOLLONESHOT | EPOLLET)

/* Maximum number of poll wake up nests we are allowing */
#define EP_MAX_POLLWAKE_NESTS 4

/* Maximum msec timeout value storeable in a long int */
#define EP_MAX_MSTIMEO min(1000ULL * MAX_SCHEDULE_TIMEOUT / HZ, (LONG_MAX - 999ULL) / HZ)

#define EP_MAX_EVENTS (INT_MAX / sizeof(struct epoll_event))

#define EP_UNACTIVE_PTR ((void *) -1L)

struct epoll_filefd {
	struct file *file;
	int fd;
};

/*
 * Node that is linked into the "wake_task_list" member of the "struct poll_safewake".
 * It is used to keep track on all tasks that are currently inside the wake_up() code
 * to 1) short-circuit the one coming from the same task and same wait queue head
 * (loop) 2) allow a maximum number of epoll descriptors inclusion nesting
 * 3) let go the ones coming from other tasks.
 */
struct wake_task_node {
	struct list_head llink;
	struct task_struct *task;
	wait_queue_head_t *wq;
};

/*
 * This is used to implement the safe poll wake up avoiding to reenter
 * the poll callback from inside wake_up().
 */
struct poll_safewake {
	struct list_head wake_task_list;
	spinlock_t lock;
};

/*
 * Each file descriptor added to the eventpoll interface will
 * have an entry of this type linked to the "rbr" RB tree.
 */
struct epitem {
	/* RB tree node used to link this structure to the eventpoll RB tree */
	struct rb_node rbn;

	/* List header used to link this structure to the eventpoll ready list */
	struct list_head rdllink;

	/*
	 * Works together "struct eventpoll"->ovflist in keeping the
	 * single linked chain of items.
	 */
	struct epitem *next;

	/* The file descriptor information this item refers to */
	struct epoll_filefd ffd;

	/* Number of active wait queue attached to poll operations */
	int nwait;

	/* List containing poll wait queues */
	struct list_head pwqlist;

	/* The "container" of this item */
	struct eventpoll *ep;

	/* List header used to link this item to the "struct file" items list */
	struct list_head fllink;

	/* The structure that describe the interested events and the source fd */
	struct epoll_event event;
};

/*
 * This structure is stored inside the "private_data" member of the file
 * structure and rapresent the main data sructure for the eventpoll
 * interface.
 */
struct eventpoll {
	/* Protect the this structure access */
	spinlock_t lock;

	/*
	 * This mutex is used to ensure that files are not removed
	 * while epoll is using them. This is held during the event
	 * collection loop, the file cleanup path, the epoll file exit
	 * code and the ctl operations.
	 */
	struct mutex mtx;

	/* Wait queue used by sys_epoll_wait() */
	wait_queue_head_t wq;

	/* Wait queue used by file->poll() */
	wait_queue_head_t poll_wait;

	/* List of ready file descriptors */
	struct list_head rdllist;

	/* RB tree root used to store monitored fd structs */
	struct rb_root rbr;

	/*
	 * This is a single linked list that chains all the "struct epitem" that
	 * happened while transfering ready events to userspace w/out
	 * holding ->lock.
	 */
	struct epitem *ovflist;
};

/* Wait structure used by the poll hooks */
struct eppoll_entry {
	/* List header used to link this structure to the "struct epitem" */
	struct list_head llink;

	/* The "base" pointer is set to the container "struct epitem" */
	void *base;

	/*
	 * Wait queue item that will be linked to the target file wait
	 * queue head.
	 */
	wait_queue_t wait;

	/* The wait queue head that linked the "wait" wait queue item */
	wait_queue_head_t *whead;
};

/* Wrapper struct used by poll queueing */
struct ep_pqueue {
	poll_table pt;
	struct epitem *epi;
};

/*
 * This mutex is used to serialize ep_free() and eventpoll_release_file().
 */
static struct mutex epmutex;

/* Safe wake up implementation */
static struct poll_safewake psw;

/* Slab cache used to allocate "struct epitem" */
static struct kmem_cache *epi_cache __read_mostly;

/* Slab cache used to allocate "struct eppoll_entry" */
static struct kmem_cache *pwq_cache __read_mostly;


/* Setup the structure that is used as key for the RB tree */
static inline void ep_set_ffd(struct epoll_filefd *ffd,
			      struct file *file, int fd)
{
	ffd->file = file;
	ffd->fd = fd;
}

/* Compare RB tree keys */
static inline int ep_cmp_ffd(struct epoll_filefd *p1,
			     struct epoll_filefd *p2)
{
	return (p1->file > p2->file ? +1:
	        (p1->file < p2->file ? -1 : p1->fd - p2->fd));
}

/* Special initialization for the RB tree node to detect linkage */
static inline void ep_rb_initnode(struct rb_node *n)
{
	rb_set_parent(n, n);
}

/* Removes a node from the RB tree and marks it for a fast is-linked check */
static inline void ep_rb_erase(struct rb_node *n, struct rb_root *r)
{
	rb_erase(n, r);
	rb_set_parent(n, n);
}

/* Fast check to verify that the item is linked to the main RB tree */
static inline int ep_rb_linked(struct rb_node *n)
{
	return rb_parent(n) != n;
}

/* Tells us if the item is currently linked */
static inline int ep_is_linked(struct list_head *p)
{
	return !list_empty(p);
}

/* Get the "struct epitem" from a wait queue pointer */
static inline struct epitem * ep_item_from_wait(wait_queue_t *p)
{
	return container_of(p, struct eppoll_entry, wait)->base;
}

/* Get the "struct epitem" from an epoll queue wrapper */
static inline struct epitem * ep_item_from_epqueue(poll_table *p)
{
	return container_of(p, struct ep_pqueue, pt)->epi;
}

/* Tells if the epoll_ctl(2) operation needs an event copy from userspace */
static inline int ep_op_has_event(int op)
{
	return op != EPOLL_CTL_DEL;
}

/* Initialize the poll safe wake up structure */
static void ep_poll_safewake_init(struct poll_safewake *psw)
{

	INIT_LIST_HEAD(&psw->wake_task_list);
	spin_lock_init(&psw->lock);
}

/*
 * Perform a safe wake up of the poll wait list. The problem is that
 * with the new callback'd wake up system, it is possible that the
 * poll callback is reentered from inside the call to wake_up() done
 * on the poll wait queue head. The rule is that we cannot reenter the
 * wake up code from the same task more than EP_MAX_POLLWAKE_NESTS times,
 * and we cannot reenter the same wait queue head at all. This will
 * enable to have a hierarchy of epoll file descriptor of no more than
 * EP_MAX_POLLWAKE_NESTS deep. We need the irq version of the spin lock
 * because this one gets called by the poll callback, that in turn is called
 * from inside a wake_up(), that might be called from irq context.
 */
static void ep_poll_safewake(struct poll_safewake *psw, wait_queue_head_t *wq)
{
	int wake_nests = 0;
	unsigned long flags;
	struct task_struct *this_task = current;
	struct list_head *lsthead = &psw->wake_task_list, *lnk;
	struct wake_task_node *tncur;
	struct wake_task_node tnode;

	spin_lock_irqsave(&psw->lock, flags);

	/* Try to see if the current task is already inside this wakeup call */
	list_for_each(lnk, lsthead) {
		tncur = list_entry(lnk, struct wake_task_node, llink);

		if (tncur->wq == wq ||
		    (tncur->task == this_task && ++wake_nests > EP_MAX_POLLWAKE_NESTS)) {
			/*
			 * Ops ... loop detected or maximum nest level reached.
			 * We abort this wake by breaking the cycle itself.
			 */
			spin_unlock_irqrestore(&psw->lock, flags);
			return;
		}
	}

	/* Add the current task to the list */
	tnode.task = this_task;
	tnode.wq = wq;
	list_add(&tnode.llink, lsthead);

	spin_unlock_irqrestore(&psw->lock, flags);

	/* Do really wake up now */
	wake_up(wq);

	/* Remove the current task from the list */
	spin_lock_irqsave(&psw->lock, flags);
	list_del(&tnode.llink);
	spin_unlock_irqrestore(&psw->lock, flags);
}

/*
 * This function unregister poll callbacks from the associated file descriptor.
 * Since this must be called without holding "ep->lock" the atomic exchange trick
 * will protect us from multiple unregister.
 */
static void ep_unregister_pollwait(struct eventpoll *ep, struct epitem *epi)
{
	int nwait;
	struct list_head *lsthead = &epi->pwqlist;
	struct eppoll_entry *pwq;

	/* This is called without locks, so we need the atomic exchange */
	nwait = xchg(&epi->nwait, 0);

	if (nwait) {
		while (!list_empty(lsthead)) {
			pwq = list_first_entry(lsthead, struct eppoll_entry, llink);

			list_del_init(&pwq->llink);
			remove_wait_queue(pwq->whead, &pwq->wait);
			kmem_cache_free(pwq_cache, pwq);
		}
	}
}

/*
 * Removes a "struct epitem" from the eventpoll RB tree and deallocates
 * all the associated resources. Must be called with "mtx" held.
 */
static int ep_remove(struct eventpoll *ep, struct epitem *epi)
{
	unsigned long flags;
	struct file *file = epi->ffd.file;

	/*
	 * Removes poll wait queue hooks. We _have_ to do this without holding
	 * the "ep->lock" otherwise a deadlock might occur. This because of the
	 * sequence of the lock acquisition. Here we do "ep->lock" then the wait
	 * queue head lock when unregistering the wait queue. The wakeup callback
	 * will run by holding the wait queue head lock and will call our callback
	 * that will try to get "ep->lock".
	 */
	ep_unregister_pollwait(ep, epi);

	/* Remove the current item from the list of epoll hooks */
	spin_lock(&file->f_ep_lock);
	if (ep_is_linked(&epi->fllink))
		list_del_init(&epi->fllink);
	spin_unlock(&file->f_ep_lock);

	if (ep_rb_linked(&epi->rbn))
		ep_rb_erase(&epi->rbn, &ep->rbr);

	spin_lock_irqsave(&ep->lock, flags);
	if (ep_is_linked(&epi->rdllink))
		list_del_init(&epi->rdllink);
	spin_unlock_irqrestore(&ep->lock, flags);

	/* At this point it is safe to free the eventpoll item */
	kmem_cache_free(epi_cache, epi);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_remove(%p, %p)\n",
		     current, ep, file));

	return 0;
}

static void ep_free(struct eventpoll *ep)
{
	struct rb_node *rbp;
	struct epitem *epi;

	/* We need to release all tasks waiting for these file */
	if (waitqueue_active(&ep->poll_wait))
		ep_poll_safewake(&psw, &ep->poll_wait);

	/*
	 * We need to lock this because we could be hit by
	 * eventpoll_release_file() while we're freeing the "struct eventpoll".
	 * We do not need to hold "ep->mtx" here because the epoll file
	 * is on the way to be removed and no one has references to it
	 * anymore. The only hit might come from eventpoll_release_file() but
	 * holding "epmutex" is sufficent here.
	 */
	mutex_lock(&epmutex);

	/*
	 * Walks through the whole tree by unregistering poll callbacks.
	 */
	for (rbp = rb_first(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);

		ep_unregister_pollwait(ep, epi);
	}

	/*
	 * Walks through the whole tree by freeing each "struct epitem". At this
	 * point we are sure no poll callbacks will be lingering around, and also by
	 * holding "epmutex" we can be sure that no file cleanup code will hit
	 * us during this operation. So we can avoid the lock on "ep->lock".
	 */
	while ((rbp = rb_first(&ep->rbr)) != 0) {
		epi = rb_entry(rbp, struct epitem, rbn);
		ep_remove(ep, epi);
	}

	mutex_unlock(&epmutex);
	mutex_destroy(&ep->mtx);
	kfree(ep);
}

static int ep_eventpoll_release(struct inode *inode, struct file *file)
{
	struct eventpoll *ep = file->private_data;

	if (ep)
		ep_free(ep);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: close() ep=%p\n", current, ep));
	return 0;
}

static unsigned int ep_eventpoll_poll(struct file *file, poll_table *wait)
{
	unsigned int pollflags = 0;
	unsigned long flags;
	struct eventpoll *ep = file->private_data;

	/* Insert inside our poll wait queue */
	poll_wait(file, &ep->poll_wait, wait);

	/* Check our condition */
	spin_lock_irqsave(&ep->lock, flags);
	if (!list_empty(&ep->rdllist))
		pollflags = POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&ep->lock, flags);

	return pollflags;
}

/* File callbacks that implement the eventpoll file behaviour */
static const struct file_operations eventpoll_fops = {
	.release	= ep_eventpoll_release,
	.poll		= ep_eventpoll_poll
};

/* Fast test to see if the file is an evenpoll file */
static inline int is_file_epoll(struct file *f)
{
	return f->f_op == &eventpoll_fops;
}

/*
 * This is called from eventpoll_release() to unlink files from the eventpoll
 * interface. We need to have this facility to cleanup correctly files that are
 * closed without being removed from the eventpoll interface.
 */
void eventpoll_release_file(struct file *file)
{
	struct list_head *lsthead = &file->f_ep_links;
	struct eventpoll *ep;
	struct epitem *epi;

	/*
	 * We don't want to get "file->f_ep_lock" because it is not
	 * necessary. It is not necessary because we're in the "struct file"
	 * cleanup path, and this means that noone is using this file anymore.
	 * So, for example, epoll_ctl() cannot hit here sicne if we reach this
	 * point, the file counter already went to zero and fget() would fail.
	 * The only hit might come from ep_free() but by holding the mutex
	 * will correctly serialize the operation. We do need to acquire
	 * "ep->mtx" after "epmutex" because ep_remove() requires it when called
	 * from anywhere but ep_free().
	 */
	mutex_lock(&epmutex);

	while (!list_empty(lsthead)) {
		epi = list_first_entry(lsthead, struct epitem, fllink);

		ep = epi->ep;
		list_del_init(&epi->fllink);
		mutex_lock(&ep->mtx);
		ep_remove(ep, epi);
		mutex_unlock(&ep->mtx);
	}

	mutex_unlock(&epmutex);
}

static int ep_alloc(struct eventpoll **pep)
{
	struct eventpoll *ep = kzalloc(sizeof(*ep), GFP_KERNEL);

	if (!ep)
		return -ENOMEM;

	spin_lock_init(&ep->lock);
	mutex_init(&ep->mtx);
	init_waitqueue_head(&ep->wq);
	init_waitqueue_head(&ep->poll_wait);
	INIT_LIST_HEAD(&ep->rdllist);
	ep->rbr = RB_ROOT;
	ep->ovflist = EP_UNACTIVE_PTR;

	*pep = ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_alloc() ep=%p\n",
		     current, ep));
	return 0;
}

/*
 * Search the file inside the eventpoll tree. The RB tree operations
 * are protected by the "mtx" mutex, and ep_find() must be called with
 * "mtx" held.
 */
static struct epitem *ep_find(struct eventpoll *ep, struct file *file, int fd)
{
	int kcmp;
	struct rb_node *rbp;
	struct epitem *epi, *epir = NULL;
	struct epoll_filefd ffd;

	ep_set_ffd(&ffd, file, fd);
	for (rbp = ep->rbr.rb_node; rbp; ) {
		epi = rb_entry(rbp, struct epitem, rbn);
		kcmp = ep_cmp_ffd(&ffd, &epi->ffd);
		if (kcmp > 0)
			rbp = rbp->rb_right;
		else if (kcmp < 0)
			rbp = rbp->rb_left;
		else {
			epir = epi;
			break;
		}
	}

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_find(%p) -> %p\n",
		     current, file, epir));

	return epir;
}

/*
 * This is the callback that is passed to the wait queue wakeup
 * machanism. It is called by the stored file descriptors when they
 * have events to report.
 */
static int ep_poll_callback(wait_queue_t *wait, unsigned mode, int sync, void *key)
{
	int pwake = 0;
	unsigned long flags;
	struct epitem *epi = ep_item_from_wait(wait);
	struct eventpoll *ep = epi->ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: poll_callback(%p) epi=%p ep=%p\n",
		     current, epi->ffd.file, epi, ep));

	spin_lock_irqsave(&ep->lock, flags);

	/*
	 * If the event mask does not contain any poll(2) event, we consider the
	 * descriptor to be disabled. This condition is likely the effect of the
	 * EPOLLONESHOT bit that disables the descriptor when an event is received,
	 * until the next EPOLL_CTL_MOD will be issued.
	 */
	if (!(epi->event.events & ~EP_PRIVATE_BITS))
		goto out_unlock;

	/*
	 * If we are trasfering events to userspace, we can hold no locks
	 * (because we're accessing user memory, and because of linux f_op->poll()
	 * semantics). All the events that happens during that period of time are
	 * chained in ep->ovflist and requeued later on.
	 */
	if (unlikely(ep->ovflist != EP_UNACTIVE_PTR)) {
		if (epi->next == EP_UNACTIVE_PTR) {
			epi->next = ep->ovflist;
			ep->ovflist = epi;
		}
		goto out_unlock;
	}

	/* If this file is already in the ready list we exit soon */
	if (ep_is_linked(&epi->rdllink))
		goto is_linked;

	list_add_tail(&epi->rdllink, &ep->rdllist);

is_linked:
	/*
	 * Wake up ( if active ) both the eventpoll wait list and the ->poll()
	 * wait list.
	 */
	if (waitqueue_active(&ep->wq))
		__wake_up_locked(&ep->wq, TASK_UNINTERRUPTIBLE |
				 TASK_INTERRUPTIBLE);
	if (waitqueue_active(&ep->poll_wait))
		pwake++;

out_unlock:
	spin_unlock_irqrestore(&ep->lock, flags);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(&psw, &ep->poll_wait);

	return 1;
}

/*
 * This is the callback that is used to add our wait queue to the
 * target file wakeup lists.
 */
static void ep_ptable_queue_proc(struct file *file, wait_queue_head_t *whead,
				 poll_table *pt)
{
	struct epitem *epi = ep_item_from_epqueue(pt);
	struct eppoll_entry *pwq;

	if (epi->nwait >= 0 && (pwq = kmem_cache_alloc(pwq_cache, GFP_KERNEL))) {
		init_waitqueue_func_entry(&pwq->wait, ep_poll_callback);
		pwq->whead = whead;
		pwq->base = epi;
		add_wait_queue(whead, &pwq->wait);
		list_add_tail(&pwq->llink, &epi->pwqlist);
		epi->nwait++;
	} else {
		/* We have to signal that an error occurred */
		epi->nwait = -1;
	}
}

static void ep_rbtree_insert(struct eventpoll *ep, struct epitem *epi)
{
	int kcmp;
	struct rb_node **p = &ep->rbr.rb_node, *parent = NULL;
	struct epitem *epic;

	while (*p) {
		parent = *p;
		epic = rb_entry(parent, struct epitem, rbn);
		kcmp = ep_cmp_ffd(&epi->ffd, &epic->ffd);
		if (kcmp > 0)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&epi->rbn, parent, p);
	rb_insert_color(&epi->rbn, &ep->rbr);
}

/*
 * Must be called with "mtx" held.
 */
static int ep_insert(struct eventpoll *ep, struct epoll_event *event,
		     struct file *tfile, int fd)
{
	int error, revents, pwake = 0;
	unsigned long flags;
	struct epitem *epi;
	struct ep_pqueue epq;

	error = -ENOMEM;
	if (!(epi = kmem_cache_alloc(epi_cache, GFP_KERNEL)))
		goto error_return;

	/* Item initialization follow here ... */
	ep_rb_initnode(&epi->rbn);
	INIT_LIST_HEAD(&epi->rdllink);
	INIT_LIST_HEAD(&epi->fllink);
	INIT_LIST_HEAD(&epi->pwqlist);
	epi->ep = ep;
	ep_set_ffd(&epi->ffd, tfile, fd);
	epi->event = *event;
	epi->nwait = 0;
	epi->next = EP_UNACTIVE_PTR;

	/* Initialize the poll table using the queue callback */
	epq.epi = epi;
	init_poll_funcptr(&epq.pt, ep_ptable_queue_proc);

	/*
	 * Attach the item to the poll hooks and get current event bits.
	 * We can safely use the file* here because its usage count has
	 * been increased by the caller of this function. Note that after
	 * this operation completes, the poll callback can start hitting
	 * the new item.
	 */
	revents = tfile->f_op->poll(tfile, &epq.pt);

	/*
	 * We have to check if something went wrong during the poll wait queue
	 * install process. Namely an allocation for a wait queue failed due
	 * high memory pressure.
	 */
	if (epi->nwait < 0)
		goto error_unregister;

	/* Add the current item to the list of active epoll hook for this file */
	spin_lock(&tfile->f_ep_lock);
	list_add_tail(&epi->fllink, &tfile->f_ep_links);
	spin_unlock(&tfile->f_ep_lock);

	/*
	 * Add the current item to the RB tree. All RB tree operations are
	 * protected by "mtx", and ep_insert() is called with "mtx" held.
	 */
	ep_rbtree_insert(ep, epi);

	/* We have to drop the new item inside our item list to keep track of it */
	spin_lock_irqsave(&ep->lock, flags);

	/* If the file is already "ready" we drop it inside the ready list */
	if ((revents & event->events) && !ep_is_linked(&epi->rdllink)) {
		list_add_tail(&epi->rdllink, &ep->rdllist);

		/* Notify waiting tasks that events are available */
		if (waitqueue_active(&ep->wq))
			__wake_up_locked(&ep->wq, TASK_UNINTERRUPTIBLE | TASK_INTERRUPTIBLE);
		if (waitqueue_active(&ep->poll_wait))
			pwake++;
	}

	spin_unlock_irqrestore(&ep->lock, flags);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(&psw, &ep->poll_wait);

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: ep_insert(%p, %p, %d)\n",
		     current, ep, tfile, fd));

	return 0;

error_unregister:
	ep_unregister_pollwait(ep, epi);

	/*
	 * We need to do this because an event could have been arrived on some
	 * allocated wait queue. Note that we don't care about the ep->ovflist
	 * list, since that is used/cleaned only inside a section bound by "mtx".
	 * And ep_insert() is called with "mtx" held.
	 */
	spin_lock_irqsave(&ep->lock, flags);
	if (ep_is_linked(&epi->rdllink))
		list_del_init(&epi->rdllink);
	spin_unlock_irqrestore(&ep->lock, flags);

	kmem_cache_free(epi_cache, epi);
error_return:
	return error;
}

/*
 * Modify the interest event mask by dropping an event if the new mask
 * has a match in the current file status. Must be called with "mtx" held.
 */
static int ep_modify(struct eventpoll *ep, struct epitem *epi, struct epoll_event *event)
{
	int pwake = 0;
	unsigned int revents;
	unsigned long flags;

	/*
	 * Set the new event interest mask before calling f_op->poll(), otherwise
	 * a potential race might occur. In fact if we do this operation inside
	 * the lock, an event might happen between the f_op->poll() call and the
	 * new event set registering.
	 */
	epi->event.events = event->events;

	/*
	 * Get current event bits. We can safely use the file* here because
	 * its usage count has been increased by the caller of this function.
	 */
	revents = epi->ffd.file->f_op->poll(epi->ffd.file, NULL);

	spin_lock_irqsave(&ep->lock, flags);

	/* Copy the data member from inside the lock */
	epi->event.data = event->data;

	/*
	 * If the item is "hot" and it is not registered inside the ready
	 * list, push it inside.
	 */
	if (revents & event->events) {
		if (!ep_is_linked(&epi->rdllink)) {
			list_add_tail(&epi->rdllink, &ep->rdllist);

			/* Notify waiting tasks that events are available */
			if (waitqueue_active(&ep->wq))
				__wake_up_locked(&ep->wq, TASK_UNINTERRUPTIBLE |
						 TASK_INTERRUPTIBLE);
			if (waitqueue_active(&ep->poll_wait))
				pwake++;
		}
	}
	spin_unlock_irqrestore(&ep->lock, flags);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(&psw, &ep->poll_wait);

	return 0;
}

static int ep_send_events(struct eventpoll *ep, struct epoll_event __user *events,
			  int maxevents)
{
	int eventcnt, error = -EFAULT, pwake = 0;
	unsigned int revents;
	unsigned long flags;
	struct epitem *epi, *nepi;
	struct list_head txlist;

	INIT_LIST_HEAD(&txlist);

	/*
	 * We need to lock this because we could be hit by
	 * eventpoll_release_file() and epoll_ctl(EPOLL_CTL_DEL).
	 */
	mutex_lock(&ep->mtx);

	/*
	 * Steal the ready list, and re-init the original one to the
	 * empty list. Also, set ep->ovflist to NULL so that events
	 * happening while looping w/out locks, are not lost. We cannot
	 * have the poll callback to queue directly on ep->rdllist,
	 * because we are doing it in the loop below, in a lockless way.
	 */
	spin_lock_irqsave(&ep->lock, flags);
	list_splice(&ep->rdllist, &txlist);
	INIT_LIST_HEAD(&ep->rdllist);
	ep->ovflist = NULL;
	spin_unlock_irqrestore(&ep->lock, flags);

	/*
	 * We can loop without lock because this is a task private list.
	 * We just splice'd out the ep->rdllist in ep_collect_ready_items().
	 * Items cannot vanish during the loop because we are holding "mtx".
	 */
	for (eventcnt = 0; !list_empty(&txlist) && eventcnt < maxevents;) {
		epi = list_first_entry(&txlist, struct epitem, rdllink);

		list_del_init(&epi->rdllink);

		/*
		 * Get the ready file event set. We can safely use the file
		 * because we are holding the "mtx" and this will guarantee
		 * that both the file and the item will not vanish.
		 */
		revents = epi->ffd.file->f_op->poll(epi->ffd.file, NULL);
		revents &= epi->event.events;

		/*
		 * Is the event mask intersect the caller-requested one,
		 * deliver the event to userspace. Again, we are holding
		 * "mtx", so no operations coming from userspace can change
		 * the item.
		 */
		if (revents) {
			if (__put_user(revents,
				       &events[eventcnt].events) ||
			    __put_user(epi->event.data,
				       &events[eventcnt].data))
				goto errxit;
			if (epi->event.events & EPOLLONESHOT)
				epi->event.events &= EP_PRIVATE_BITS;
			eventcnt++;
		}
		/*
		 * At this point, noone can insert into ep->rdllist besides
		 * us. The epoll_ctl() callers are locked out by us holding
		 * "mtx" and the poll callback will queue them in ep->ovflist.
		 */
		if (!(epi->event.events & EPOLLET) &&
		    (revents & epi->event.events))
			list_add_tail(&epi->rdllink, &ep->rdllist);
	}
	error = 0;

errxit:

	spin_lock_irqsave(&ep->lock, flags);
	/*
	 * During the time we spent in the loop above, some other events
	 * might have been queued by the poll callback. We re-insert them
	 * here (in case they are not already queued, or they're one-shot).
	 */
	for (nepi = ep->ovflist; (epi = nepi) != NULL;
	     nepi = epi->next, epi->next = EP_UNACTIVE_PTR) {
		if (!ep_is_linked(&epi->rdllink) &&
		    (epi->event.events & ~EP_PRIVATE_BITS))
			list_add_tail(&epi->rdllink, &ep->rdllist);
	}
	/*
	 * We need to set back ep->ovflist to EP_UNACTIVE_PTR, so that after
	 * releasing the lock, events will be queued in the normal way inside
	 * ep->rdllist.
	 */
	ep->ovflist = EP_UNACTIVE_PTR;

	/*
	 * In case of error in the event-send loop, or in case the number of
	 * ready events exceeds the userspace limit, we need to splice the
	 * "txlist" back inside ep->rdllist.
	 */
	list_splice(&txlist, &ep->rdllist);

	if (!list_empty(&ep->rdllist)) {
		/*
		 * Wake up (if active) both the eventpoll wait list and the ->poll()
		 * wait list (delayed after we release the lock).
		 */
		if (waitqueue_active(&ep->wq))
			__wake_up_locked(&ep->wq, TASK_UNINTERRUPTIBLE |
					 TASK_INTERRUPTIBLE);
		if (waitqueue_active(&ep->poll_wait))
			pwake++;
	}
	spin_unlock_irqrestore(&ep->lock, flags);

	mutex_unlock(&ep->mtx);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(&psw, &ep->poll_wait);

	return eventcnt == 0 ? error: eventcnt;
}

static int ep_poll(struct eventpoll *ep, struct epoll_event __user *events,
		   int maxevents, long timeout)
{
	int res, eavail;
	unsigned long flags;
	long jtimeout;
	wait_queue_t wait;

	/*
	 * Calculate the timeout by checking for the "infinite" value ( -1 )
	 * and the overflow condition. The passed timeout is in milliseconds,
	 * that why (t * HZ) / 1000.
	 */
	jtimeout = (timeout < 0 || timeout >= EP_MAX_MSTIMEO) ?
		MAX_SCHEDULE_TIMEOUT : (timeout * HZ + 999) / 1000;

retry:
	spin_lock_irqsave(&ep->lock, flags);

	res = 0;
	if (list_empty(&ep->rdllist)) {
		/*
		 * We don't have any available event to return to the caller.
		 * We need to sleep here, and we will be wake up by
		 * ep_poll_callback() when events will become available.
		 */
		init_waitqueue_entry(&wait, current);
		wait.flags |= WQ_FLAG_EXCLUSIVE;
		__add_wait_queue(&ep->wq, &wait);

		for (;;) {
			/*
			 * We don't want to sleep if the ep_poll_callback() sends us
			 * a wakeup in between. That's why we set the task state
			 * to TASK_INTERRUPTIBLE before doing the checks.
			 */
			set_current_state(TASK_INTERRUPTIBLE);
			if (!list_empty(&ep->rdllist) || !jtimeout)
				break;
			if (signal_pending(current)) {
				res = -EINTR;
				break;
			}

			spin_unlock_irqrestore(&ep->lock, flags);
			jtimeout = schedule_timeout(jtimeout);
			spin_lock_irqsave(&ep->lock, flags);
		}
		__remove_wait_queue(&ep->wq, &wait);

		set_current_state(TASK_RUNNING);
	}

	/* Is it worth to try to dig for events ? */
	eavail = !list_empty(&ep->rdllist);

	spin_unlock_irqrestore(&ep->lock, flags);

	/*
	 * Try to transfer events to user space. In case we get 0 events and
	 * there's still timeout left over, we go trying again in search of
	 * more luck.
	 */
	if (!res && eavail &&
	    !(res = ep_send_events(ep, events, maxevents)) && jtimeout)
		goto retry;

	return res;
}

/*
 * It opens an eventpoll file descriptor. The "size" parameter is there
 * for historical reasons, when epoll was using an hash instead of an
 * RB tree. With the current implementation, the "size" parameter is ignored
 * (besides sanity checks).
 */
asmlinkage long sys_epoll_create(int size)
{
	int error, fd = -1;
	struct eventpoll *ep;
	struct inode *inode;
	struct file *file;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d)\n",
		     current, size));

	/*
	 * Sanity check on the size parameter, and create the internal data
	 * structure ( "struct eventpoll" ).
	 */
	error = -EINVAL;
	if (size <= 0 || (error = ep_alloc(&ep)) != 0)
		goto error_return;

	/*
	 * Creates all the items needed to setup an eventpoll file. That is,
	 * a file structure, and inode and a free file descriptor.
	 */
	error = anon_inode_getfd(&fd, &inode, &file, "[eventpoll]",
				 &eventpoll_fops, ep);
	if (error)
		goto error_free;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d) = %d\n",
		     current, size, fd));

	return fd;

error_free:
	ep_free(ep);
error_return:
	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_create(%d) = %d\n",
		     current, size, error));
	return error;
}

/*
 * The following function implements the controller interface for
 * the eventpoll file that enables the insertion/removal/change of
 * file descriptors inside the interest set.
 */
asmlinkage long sys_epoll_ctl(int epfd, int op, int fd,
			      struct epoll_event __user *event)
{
	int error;
	struct file *file, *tfile;
	struct eventpoll *ep;
	struct epitem *epi;
	struct epoll_event epds;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_ctl(%d, %d, %d, %p)\n",
		     current, epfd, op, fd, event));

	error = -EFAULT;
	if (ep_op_has_event(op) &&
	    copy_from_user(&epds, event, sizeof(struct epoll_event)))
		goto error_return;

	/* Get the "struct file *" for the eventpoll file */
	error = -EBADF;
	file = fget(epfd);
	if (!file)
		goto error_return;

	/* Get the "struct file *" for the target file */
	tfile = fget(fd);
	if (!tfile)
		goto error_fput;

	/* The target file descriptor must support poll */
	error = -EPERM;
	if (!tfile->f_op || !tfile->f_op->poll)
		goto error_tgt_fput;

	/*
	 * We have to check that the file structure underneath the file descriptor
	 * the user passed to us _is_ an eventpoll file. And also we do not permit
	 * adding an epoll file descriptor inside itself.
	 */
	error = -EINVAL;
	if (file == tfile || !is_file_epoll(file))
		goto error_tgt_fput;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = file->private_data;

	mutex_lock(&ep->mtx);

	/*
	 * Try to lookup the file inside our RB tree, Since we grabbed "mtx"
	 * above, we can be sure to be able to use the item looked up by
	 * ep_find() till we release the mutex.
	 */
	epi = ep_find(ep, tfile, fd);

	error = -EINVAL;
	switch (op) {
	case EPOLL_CTL_ADD:
		if (!epi) {
			epds.events |= POLLERR | POLLHUP;

			error = ep_insert(ep, &epds, tfile, fd);
		} else
			error = -EEXIST;
		break;
	case EPOLL_CTL_DEL:
		if (epi)
			error = ep_remove(ep, epi);
		else
			error = -ENOENT;
		break;
	case EPOLL_CTL_MOD:
		if (epi) {
			epds.events |= POLLERR | POLLHUP;
			error = ep_modify(ep, epi, &epds);
		} else
			error = -ENOENT;
		break;
	}
	mutex_unlock(&ep->mtx);

error_tgt_fput:
	fput(tfile);
error_fput:
	fput(file);
error_return:
	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_ctl(%d, %d, %d, %p) = %d\n",
		     current, epfd, op, fd, event, error));

	return error;
}

/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_wait(2).
 */
asmlinkage long sys_epoll_wait(int epfd, struct epoll_event __user *events,
			       int maxevents, int timeout)
{
	int error;
	struct file *file;
	struct eventpoll *ep;

	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_wait(%d, %p, %d, %d)\n",
		     current, epfd, events, maxevents, timeout));

	/* The maximum number of event must be greater than zero */
	if (maxevents <= 0 || maxevents > EP_MAX_EVENTS)
		return -EINVAL;

	/* Verify that the area passed by the user is writeable */
	if (!access_ok(VERIFY_WRITE, events, maxevents * sizeof(struct epoll_event))) {
		error = -EFAULT;
		goto error_return;
	}

	/* Get the "struct file *" for the eventpoll file */
	error = -EBADF;
	file = fget(epfd);
	if (!file)
		goto error_return;

	/*
	 * We have to check that the file structure underneath the fd
	 * the user passed to us _is_ an eventpoll file.
	 */
	error = -EINVAL;
	if (!is_file_epoll(file))
		goto error_fput;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = file->private_data;

	/* Time to fish for events ... */
	error = ep_poll(ep, events, maxevents, timeout);

error_fput:
	fput(file);
error_return:
	DNPRINTK(3, (KERN_INFO "[%p] eventpoll: sys_epoll_wait(%d, %p, %d, %d) = %d\n",
		     current, epfd, events, maxevents, timeout, error));

	return error;
}

#ifdef TIF_RESTORE_SIGMASK

/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_pwait(2).
 */
asmlinkage long sys_epoll_pwait(int epfd, struct epoll_event __user *events,
		int maxevents, int timeout, const sigset_t __user *sigmask,
		size_t sigsetsize)
{
	int error;
	sigset_t ksigmask, sigsaved;

	/*
	 * If the caller wants a certain signal mask to be set during the wait,
	 * we apply it here.
	 */
	if (sigmask) {
		if (sigsetsize != sizeof(sigset_t))
			return -EINVAL;
		if (copy_from_user(&ksigmask, sigmask, sizeof(ksigmask)))
			return -EFAULT;
		sigdelsetmask(&ksigmask, sigmask(SIGKILL) | sigmask(SIGSTOP));
		sigprocmask(SIG_SETMASK, &ksigmask, &sigsaved);
	}

	error = sys_epoll_wait(epfd, events, maxevents, timeout);

	/*
	 * If we changed the signal mask, we need to restore the original one.
	 * In case we've got a signal while waiting, we do not restore the
	 * signal mask yet, and we allow do_signal() to deliver the signal on
	 * the way back to userspace, before the signal mask is restored.
	 */
	if (sigmask) {
		if (error == -EINTR) {
			memcpy(&current->saved_sigmask, &sigsaved,
			       sizeof(sigsaved));
			set_thread_flag(TIF_RESTORE_SIGMASK);
		} else
			sigprocmask(SIG_SETMASK, &sigsaved, NULL);
	}

	return error;
}

#endif /* #ifdef TIF_RESTORE_SIGMASK */

static int __init eventpoll_init(void)
{
	mutex_init(&epmutex);

	/* Initialize the structure used to perform safe poll wait head wake ups */
	ep_poll_safewake_init(&psw);

	/* Allocates slab cache used to allocate "struct epitem" items */
	epi_cache = kmem_cache_create("eventpoll_epi", sizeof(struct epitem),
			0, SLAB_HWCACHE_ALIGN|EPI_SLAB_DEBUG|SLAB_PANIC,
			NULL, NULL);

	/* Allocates slab cache used to allocate "struct eppoll_entry" */
	pwq_cache = kmem_cache_create("eventpoll_pwq",
			sizeof(struct eppoll_entry), 0,
			EPI_SLAB_DEBUG|SLAB_PANIC, NULL, NULL);

	return 0;
}
fs_initcall(eventpoll_init);

