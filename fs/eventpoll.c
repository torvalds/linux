// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  fs/eventpoll.c (Efficient event retrieval implementation)
 *  Copyright (C) 2001,...,2009	 Davide Libenzi
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
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
#include <linux/device.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/mman.h>
#include <linux/atomic.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/compat.h>
#include <linux/rculist.h>
#include <linux/capability.h>
#include <linux/seqlock.h>
#include <net/busy_poll.h>

/*
 * fs/eventpoll.c - Efficient event polling ("epoll") kernel implementation.
 *
 *
 * Overview
 * --------
 *
 * Each epoll_create(2) returns an anonymous [eventpoll] file whose
 * ->private_data is a struct eventpoll. Each EPOLL_CTL_ADD installs
 * a struct epitem linking one (watched file, fd) pair back to that
 * eventpoll via the watched file's f_op->poll() wait queue(s). When
 * the watched file signals readiness, ep_poll_callback() fires and
 * marks the epitem ready. epoll_wait(2) drains the ready list under
 * ep->mtx, re-queueing items in level-triggered mode.
 *
 * epoll instances can watch other epoll instances up to EP_MAX_NESTS
 * deep; cycles are forbidden and detected at EPOLL_CTL_ADD time.
 *
 *
 * Locking
 * -------
 *
 * Three levels, acquired from outer to inner:
 *
 *   epnested_mutex   (global; rare; taken only for EPOLL_CTL_ADD
 *                     loop / path checks)
 *     > ep->mtx     (per-eventpoll; sleepable; serializes most ops)
 *       > ep->lock  (per-eventpoll; IRQ-safe spinlock)
 *
 *   file->f_lock    (per-file; NOT IRQ-safe; guards f_ep hlist ops;
 *                    nested inside ep->mtx, outside ep->lock)
 *
 * Rationale:
 *   - ep->lock is a spinlock because ep_poll_callback() is called from
 *     wake_up() which may run in hard-IRQ context. All ep->lock
 *     critical sections use spin_lock_irqsave().
 *   - ep->mtx is a sleepable mutex because the event delivery loop
 *     calls copy_to_user(), and ep_insert() may sleep in
 *     kmem_cache_alloc() and f_op->poll().
 *   - epnested_mutex is global because cycle detection needs a global
 *     view of the epoll topology; a per-object scheme would let two
 *     concurrent inserts (A into B, B into A) construct a cycle
 *     without either observer seeing it.
 *   - Per-ep ep->mtx is preferred for scalability elsewhere. Events
 *     that require epnested_mutex are rare.
 *
 * When EPOLL_CTL_ADD nests one eventpoll inside another we acquire
 * ep->mtx on both: outer first, target second. Since cycles are
 * forbidden the set of live ep->mtx holds is always a strict chain,
 * communicated to lockdep via mutex_lock_nested() subclasses derived
 * from the current recursion depth.
 *
 *
 * Field protection
 * ----------------
 *
 * struct eventpoll:
 *   mtx              - self
 *   rbr              - ep->mtx
 *   ovflist, rdllist - ep->lock (IRQ-safe)
 *   wq               - ep->lock for queue mutation
 *   poll_wait        - internal waitqueue spinlock
 *   refs             - file->f_lock for adds; ep->mtx for removes;
 *                      RCU for readers (hlist_del_rcu + kfree_rcu(ep))
 *   ws               - ep->mtx
 *   gen, loop_check_depth - epnested_mutex
 *   file, user       - immutable after setup
 *   refcount         - atomic (refcount_t)
 *   napi_*           - READ_ONCE / WRITE_ONCE
 *
 * struct epitem:
 *   rbn / rcu union  - rbn: ep->mtx (while epi is linked in ep->rbr).
 *                      rcu: written only by kfree_rcu(epi) on the free
 *                      path; otherwise untouched by epoll code.
 *   rdllink, next    - ep->lock
 *   ffd, ep          - immutable after ep_insert()
 *   pwqlist          - ep->mtx for writes; POLLFREE clears pwq->whead
 *                      via smp_store_release(), see below
 *   fllink           - file->f_lock for mutation; hlist_del_rcu +
 *                      kfree_rcu(epi) for safe RCU readers
 *   ws               - RCU (rcu_assign_pointer /
 *                      rcu_dereference_check(mtx))
 *   event            - ep->mtx for writes; lockless read in
 *                      ep_poll_callback pairs with smp_mb() in
 *                      ep_modify()
 *
 *
 * Ready-list state machine
 * ------------------------
 *
 * Readiness is tracked in two lists under ep->lock:
 *
 *   rdllist   - doubly-linked FIFO; the "current" ready list.
 *   ovflist   - singly-linked LIFO; used during a scan to catch
 *               events that arrive while rdllist is being iterated
 *               without ep->lock.
 *
 * Encoded in ep->ovflist:
 *   EP_UNACTIVE_PTR - no scan active; callback appends to rdllist.
 *   NULL            - scan active, no spill yet.
 *   pointer to epi  - scan active with spilled items (LIFO).
 *
 * Encoded in epi->ovflist_next:
 *   EP_UNACTIVE_PTR - epi is not on ovflist.
 *   otherwise       - next epi on ovflist (NULL at tail).
 *
 * ep_start_scan() flips "not scanning" to "scanning" and splices
 * rdllist into a caller-local scan_batch. ep_done_scan() drains ovflist
 * back to rdllist (list_add head-insert reverses LIFO to FIFO),
 * flips back to "not scanning", and re-splices any items the caller
 * left in scan_batch (e.g., level-triggered re-queues).
 *
 *
 * Removal paths
 * -------------
 *
 * Three paths dispose of epitems and/or eventpolls:
 *
 *   A. ep_remove()              - EPOLL_CTL_DEL and ep_insert()
 *                                 rollback. Caller holds ep->mtx.
 *   B. ep_clear_and_put()       - close of the epoll fd itself
 *                                 (ep_eventpoll_release).
 *   C. eventpoll_release_file() - close of a watched file, invoked
 *                                 from __fput().
 *
 * Coordination:
 *   A and C exclude each other via the watched file's refcount.
 *   A pins the file with epi_fget() before touching file->f_ep or
 *   file->f_lock; if the pin fails, __fput() is in flight and C
 *   will clean this epi up. See the epi_fget() block comment.
 *   A and B both hold ep->mtx serially. B walks the rbtree with
 *   rb_next() captured before ep_remove() erases the current node.
 *   B and C both take ep->mtx; the loser sees fewer entries or an
 *   empty file->f_ep.
 *
 * Within every path the internal order is strict:
 *   ep_unregister_pollwait()  - drain pwqlist; synchronizes with any
 *                                in-flight ep_poll_callback via the
 *                                watched wait-queue head's lock.
 *   ep_remove_file()          - hlist_del_rcu of epi->fllink and,
 *                                if last watcher, clear file->f_ep,
 *                                under file->f_lock.
 *   ep_remove_epi()           - rb_erase, rdllist unlink (ep->lock),
 *                                wakeup_source_unregister,
 *                                kfree_rcu(epi).
 *
 * kfree_rcu(epi) defers the free past RCU readers in
 * reverse_path_check_proc(); kfree_rcu(ep) defers past readers in
 * ep_get_upwards_depth_proc().
 *
 *
 * POLLFREE handshake
 * ------------------
 *
 * When a subsystem tears down a wait-queue head that an epitem is
 * registered on (binder, signalfd, ...), it wakes the callback with
 * POLLFREE and must RCU-defer the head's free. The store/load pair:
 *
 *   ep_poll_callback() POLLFREE branch:
 *     smp_store_release(&pwq->whead, NULL)
 *
 *   ep_remove_wait_queue():
 *     smp_load_acquire(&pwq->whead)
 *
 * See those sites for the full argument.
 */

/* Epoll private bits inside the event mask */
#define EP_PRIVATE_BITS (EPOLLWAKEUP | EPOLLONESHOT | EPOLLET | EPOLLEXCLUSIVE)

#define EPOLLINOUT_BITS (EPOLLIN | EPOLLOUT)

#define EPOLLEXCLUSIVE_OK_BITS (EPOLLINOUT_BITS | EPOLLERR | EPOLLHUP | \
				EPOLLWAKEUP | EPOLLET | EPOLLEXCLUSIVE)

/* Maximum number of nesting allowed inside epoll sets */
#define EP_MAX_NESTS 4

#define EP_MAX_EVENTS (INT_MAX / sizeof(struct epoll_event))

#define EP_UNACTIVE_PTR ((void *) -1L)

#define EP_ITEM_COST (sizeof(struct epitem) + sizeof(struct eppoll_entry))

/* Wait structure used by the poll hooks */
struct eppoll_entry {
	/* List header used to link this structure to the "struct epitem" */
	struct eppoll_entry *next;

	/* The "base" pointer is set to the container "struct epitem" */
	struct epitem *base;

	/*
	 * Wait queue item that will be linked to the target file wait
	 * queue head.
	 */
	wait_queue_entry_t wait;

	/* The wait queue head that linked the "wait" wait queue item */
	wait_queue_head_t *whead;
};

/*
 * Each file descriptor added to the eventpoll interface will
 * have an entry of this type linked to the "rbr" RB tree.
 * Avoid increasing the size of this struct, there can be many thousands
 * of these on a server and we do not want this to take another cache line.
 */
struct epitem {
	union {
		/* RB tree node links this structure to the eventpoll RB tree */
		struct rb_node rbn;
		/* Used to free the struct epitem */
		struct rcu_head rcu;
	};

	/* Link on the owning eventpoll's ready list (ep->rdllist). */
	struct list_head rdllink;

	/*
	 * Link on the owning eventpoll's scan-overflow list (ep->ovflist),
	 * EP_UNACTIVE_PTR when not linked. See epi_on_ovflist() /
	 * epi_clear_ovflist() and the "Ready-list state machine" section
	 * in the top-of-file banner.
	 */
	struct epitem *ovflist_next;

	/* The file descriptor information this item refers to */
	struct epoll_key ffd;

	/* List containing poll wait queues */
	struct eppoll_entry *pwqlist;

	/* The "container" of this item */
	struct eventpoll *ep;

	/* List header used to link this item to the "struct file" items list */
	struct hlist_node fllink;

	/* wakeup_source used when EPOLLWAKEUP is set */
	struct wakeup_source __rcu *ws;

	/* The structure that describe the interested events and the source fd */
	struct epoll_event event;
};

/*
 * This structure is stored inside the "private_data" member of the file
 * structure and represents the main data structure for the eventpoll
 * interface.
 */
struct eventpoll {
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

	/* Lock which protects rdllist and ovflist */
	spinlock_t lock;

	/* Protect switching between rdllist and ovflist */
	seqcount_spinlock_t seq;

	/* RB tree root used to store monitored fd structs */
	struct rb_root_cached rbr;

	/*
	 * This is a single linked list that chains all the "struct epitem" that
	 * happened while transferring ready events to userspace w/out
	 * holding ->lock.
	 */
	struct epitem *ovflist;

	/* wakeup_source used when ep_send_events or __ep_eventpoll_poll is running */
	struct wakeup_source *ws;

	/* The user that created the eventpoll descriptor */
	struct user_struct *user;

	struct file *file;

	/* used to optimize loop detection check */
	u64 gen;
	struct hlist_head refs;
	u8 loop_check_depth;

	/* usage count, orchestrates "struct eventpoll" disposal */
	refcount_t refcount;

	/* used to defer freeing past ep_get_upwards_depth_proc() RCU walk */
	struct rcu_head rcu;

#ifdef CONFIG_NET_RX_BUSY_POLL
	/* used to track busy poll napi_id */
	unsigned int napi_id;
	/* busy poll timeout */
	u32 busy_poll_usecs;
	/* busy poll packet budget */
	u16 busy_poll_budget;
	bool prefer_busy_poll;
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	/* tracks wakeup nests for lockdep validation */
	u8 nests;
#endif
};

/* Wrapper struct used by poll queueing */
struct ep_pqueue {
	poll_table pt;
	struct epitem *epi;
};

/*
 * Configuration options available inside /proc/sys/fs/epoll/
 */
/* Maximum number of epoll watched descriptors, per user */
static long max_user_watches __read_mostly;

/*
 * Cycle and path-length checks at EPOLL_CTL_ADD
 * ---------------------------------------------
 *
 * When EPOLL_CTL_ADD creates a link that either targets an eventpoll
 * file or extends an existing chain of eventpolls, two checks run:
 *
 *   1. no cycle is being formed -- ep_loop_check() walks downward
 *      from the candidate target, and ep_get_upwards_depth_proc()
 *      walks upward from the outer ep, both bounded by EP_MAX_NESTS.
 *   2. no file accumulates more than path_limits[depth] wakeup paths
 *      of a given length -- reverse_path_check().
 *
 * Both need a global view of the epoll topology and must be atomic
 * with the insertion, so the check is serialized by epnested_mutex
 * and carries its scratch state on a stack-allocated struct
 * ep_ctl_ctx scoped to one do_epoll_ctl() call. Non-nested inserts
 * skip this machinery entirely and take only ep->mtx.
 *
 *   epnested_mutex     Serializes the whole check.
 *   loop_check_gen     Global monotonic stamp, bumped at the start of
 *                      a check and again at the end. ep->gen caches
 *                      the value under which ep was last visited by
 *                      ep_loop_check_proc() or
 *                      ep_get_upwards_depth_proc(); the post-check
 *                      bump ensures those cached stamps can no longer
 *                      equal loop_check_gen, so the
 *                      "ep->gen == loop_check_gen" trigger in
 *                      ep_ctl_lock() only fires while another check
 *                      is in flight.
 *
 * struct ep_ctl_ctx carries the rest (inserting_into, tfile_check_list,
 * path_count[]) through the walk; see its declaration below.
 *
 * Commits fdcfce93073d ("eventpoll: Fix integer overflow in
 * ep_loop_check_proc()") and f2e467a48287 ("eventpoll: Fix
 * semi-unbounded recursion") hardened the walk; any refactor must
 * preserve both bail-outs.
 */
static DEFINE_MUTEX(epnested_mutex);
static u64 loop_check_gen = 0;

#define PATH_ARR_SIZE 5

/*
 * Per-do_epoll_ctl() scratch for the loop / path checks. Allocated on
 * the caller's stack; populated by ep_ctl_lock() and the downward
 * walk; consumed by reverse_path_check(); released by ep_ctl_unlock().
 * Only valid while the caller holds epnested_mutex.
 */
struct ep_ctl_ctx {
	/*
	 * Outer eventpoll for one ep_loop_check(); if the downward walk
	 * reaches it the insert would form a cycle.
	 */
	struct eventpoll *inserting_into;

	/*
	 * Singly-linked list of epitems_head objects collected during
	 * ep_loop_check_proc(), then walked by reverse_path_check().
	 * Terminated by EP_UNACTIVE_PTR, not NULL: epitems_head->next
	 * doubles as a membership flag (a NULL ->next means "not on this
	 * list", see ep_remove_file()), so the list uses a non-NULL
	 * sentinel to keep the tail head distinguishable from an unlisted
	 * one.
	 */
	struct epitems_head *tfile_check_list;

	/*
	 * Per-depth wakeup-path tally used by reverse_path_check_proc();
	 * reinitialized to zero at the start of each reverse_path_check()
	 * iteration.
	 */
	int path_count[PATH_ARR_SIZE];
};

/* Slab cache used to allocate "struct epitem" */
static struct kmem_cache *epi_cache __ro_after_init;

/* Slab cache used to allocate "struct eppoll_entry" */
static struct kmem_cache *pwq_cache __ro_after_init;

/*
 * Wrapper anchor for file->f_ep when the watched file is not itself an
 * eventpoll; for the epoll-watches-epoll case, file->f_ep points at
 * &watched_ep->refs directly. The ->next field threads
 * ctx->tfile_check_list during one EPOLL_CTL_ADD path check.
 */
struct epitems_head {
	struct hlist_head epitems;
	struct epitems_head *next;
};

static struct kmem_cache *ephead_cache __ro_after_init;

static inline void free_ephead(struct epitems_head *head)
{
	if (head)
		kmem_cache_free(ephead_cache, head);
}

static void list_file(struct file *file, struct ep_ctl_ctx *ctx)
{
	struct epitems_head *head;

	head = container_of(file->f_ep, struct epitems_head, epitems);
	if (!head->next) {
		head->next = ctx->tfile_check_list;
		ctx->tfile_check_list = head;
	}
}

static void unlist_file(struct epitems_head *head)
{
	struct epitems_head *to_free = head;
	struct hlist_node *p = rcu_dereference(hlist_first_rcu(&head->epitems));
	if (p) {
		struct epitem *epi= container_of(p, struct epitem, fllink);
		spin_lock(&epi->ffd.file->f_lock);
		if (!hlist_empty(&head->epitems))
			to_free = NULL;
		head->next = NULL;
		spin_unlock(&epi->ffd.file->f_lock);
	}
	free_ephead(to_free);
}

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static long long_zero;
static long long_max = LONG_MAX;

static const struct ctl_table epoll_table[] = {
	{
		.procname	= "max_user_watches",
		.data		= &max_user_watches,
		.maxlen		= sizeof(max_user_watches),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &long_zero,
		.extra2		= &long_max,
	},
};

static void __init epoll_sysctls_init(void)
{
	register_sysctl("fs/epoll", epoll_table);
}
#else
#define epoll_sysctls_init() do { } while (0)
#endif /* CONFIG_SYSCTL */

static const struct file_operations eventpoll_fops;

bool is_file_epoll(struct file *f)
{
	return f->f_op == &eventpoll_fops;
}

/* Compare RB tree keys */
static inline int ep_cmp_ffd(struct epoll_key *p1, struct epoll_key *p2)
{
	return (p1->file > p2->file ? +1:
	        (p1->file < p2->file ? -1 : p1->fd - p2->fd));
}

/* True iff @epi is on its owning ep's ready list. */
static inline bool ep_is_linked(struct epitem *epi)
{
	return !list_empty(&epi->rdllink);
}

static inline struct eppoll_entry *ep_pwq_from_wait(wait_queue_entry_t *p)
{
	return container_of(p, struct eppoll_entry, wait);
}

/* Get the "struct epitem" from a wait queue pointer */
static inline struct epitem *ep_item_from_wait(wait_queue_entry_t *p)
{
	return container_of(p, struct eppoll_entry, wait)->base;
}

/*
 * Ready-list / ovflist state (see "Ready-list state machine" in the
 * top-of-file banner for the full state machine). EP_UNACTIVE_PTR is
 * the sentinel; these wrappers name each transition and each test so
 * call sites do not need to know the sentinel's value.
 */

/* True iff @ep is between ep_enter_scan() and ep_exit_scan(). */
static inline bool ep_is_scanning(struct eventpoll *ep)
{
	return READ_ONCE(ep->ovflist) != EP_UNACTIVE_PTR;
}

/* Called by ep_start_scan(): divert ep_poll_callback() to ovflist. */
static inline void ep_enter_scan(struct eventpoll *ep)
{
	WRITE_ONCE(ep->ovflist, NULL);
}

/* Called by ep_done_scan(): redirect ep_poll_callback() back to rdllist. */
static inline void ep_exit_scan(struct eventpoll *ep)
{
	WRITE_ONCE(ep->ovflist, EP_UNACTIVE_PTR);
}

/* True iff @epi is currently linked on its ep's ovflist. */
static inline bool epi_on_ovflist(const struct epitem *epi)
{
	return epi->ovflist_next != EP_UNACTIVE_PTR;
}

/* Mark @epi as not on any ovflist (init and post-drain). */
static inline void epi_clear_ovflist(struct epitem *epi)
{
	epi->ovflist_next = EP_UNACTIVE_PTR;
}

/* True iff @ep has ready events that epoll_wait() might harvest. */
static inline bool ep_events_available(struct eventpoll *ep)
{
	unsigned int seq = read_seqcount_begin(&ep->seq);

	return !list_empty_careful(&ep->rdllist) || ep_is_scanning(ep) ||
		read_seqcount_retry(&ep->seq, seq);
}

#ifdef CONFIG_NET_RX_BUSY_POLL
/**
 * busy_loop_ep_timeout - check if busy poll has timed out. The timeout value
 * from the epoll instance ep is preferred, but if it is not set fallback to
 * the system-wide global via busy_loop_timeout.
 *
 * @start_time: The start time used to compute the remaining time until timeout.
 * @ep: Pointer to the eventpoll context.
 *
 * Return: true if the timeout has expired, false otherwise.
 */
static bool busy_loop_ep_timeout(unsigned long start_time,
				 struct eventpoll *ep)
{
	unsigned long bp_usec = READ_ONCE(ep->busy_poll_usecs);

	if (bp_usec) {
		unsigned long end_time = start_time + bp_usec;
		unsigned long now = busy_loop_current_time();

		return time_after(now, end_time);
	} else {
		return busy_loop_timeout(start_time);
	}
}

static bool ep_busy_loop_on(struct eventpoll *ep)
{
	return !!READ_ONCE(ep->busy_poll_usecs) ||
	       READ_ONCE(ep->prefer_busy_poll) ||
	       net_busy_loop_on();
}

static bool ep_busy_loop_end(void *p, unsigned long start_time)
{
	struct eventpoll *ep = p;

	return ep_events_available(ep) || busy_loop_ep_timeout(start_time, ep);
}

/*
 * Busy poll if globally on and supporting sockets found && no events,
 * busy loop will return if need_resched or ep_events_available.
 *
 * we must do our busy polling with irqs enabled
 */
static bool ep_busy_loop(struct eventpoll *ep)
{
	unsigned int napi_id = READ_ONCE(ep->napi_id);
	u16 budget = READ_ONCE(ep->busy_poll_budget);
	bool prefer_busy_poll = READ_ONCE(ep->prefer_busy_poll);

	if (!budget)
		budget = BUSY_POLL_BUDGET;

	if (napi_id_valid(napi_id) && ep_busy_loop_on(ep)) {
		napi_busy_loop(napi_id, ep_busy_loop_end,
			       ep, prefer_busy_poll, budget);
		if (ep_events_available(ep))
			return true;
		/*
		 * Busy poll timed out.  Drop NAPI ID for now, we can add
		 * it back in when we have moved a socket with a valid NAPI
		 * ID onto the ready list.
		 */
		if (prefer_busy_poll)
			napi_resume_irqs(napi_id);
		ep->napi_id = 0;
		return false;
	}
	return false;
}

/*
 * Set epoll busy poll NAPI ID from sk.
 */
static inline void ep_set_busy_poll_napi_id(struct epitem *epi)
{
	struct eventpoll *ep = epi->ep;
	unsigned int napi_id;
	struct socket *sock;
	struct sock *sk;

	if (!ep_busy_loop_on(ep))
		return;

	sock = sock_from_file(epi->ffd.file);
	if (!sock)
		return;

	sk = sock->sk;
	if (!sk)
		return;

	napi_id = READ_ONCE(sk->sk_napi_id);

	/* Non-NAPI IDs can be rejected
	 *	or
	 * Nothing to do if we already have this ID
	 */
	if (!napi_id_valid(napi_id) || napi_id == ep->napi_id)
		return;

	/* record NAPI ID for use in next busy poll */
	ep->napi_id = napi_id;
}

static long ep_eventpoll_bp_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	struct eventpoll *ep = file->private_data;
	void __user *uarg = (void __user *)arg;
	struct epoll_params epoll_params;

	switch (cmd) {
	case EPIOCSPARAMS:
		if (copy_from_user(&epoll_params, uarg, sizeof(epoll_params)))
			return -EFAULT;

		/* pad byte must be zero */
		if (epoll_params.__pad)
			return -EINVAL;

		if (epoll_params.busy_poll_usecs > S32_MAX)
			return -EINVAL;

		if (epoll_params.prefer_busy_poll > 1)
			return -EINVAL;

		if (epoll_params.busy_poll_budget > NAPI_POLL_WEIGHT &&
		    !capable(CAP_NET_ADMIN))
			return -EPERM;

		WRITE_ONCE(ep->busy_poll_usecs, epoll_params.busy_poll_usecs);
		WRITE_ONCE(ep->busy_poll_budget, epoll_params.busy_poll_budget);
		WRITE_ONCE(ep->prefer_busy_poll, epoll_params.prefer_busy_poll);
		return 0;
	case EPIOCGPARAMS:
		memset(&epoll_params, 0, sizeof(epoll_params));
		epoll_params.busy_poll_usecs = READ_ONCE(ep->busy_poll_usecs);
		epoll_params.busy_poll_budget = READ_ONCE(ep->busy_poll_budget);
		epoll_params.prefer_busy_poll = READ_ONCE(ep->prefer_busy_poll);
		if (copy_to_user(uarg, &epoll_params, sizeof(epoll_params)))
			return -EFAULT;
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}

static void ep_suspend_napi_irqs(struct eventpoll *ep)
{
	unsigned int napi_id = READ_ONCE(ep->napi_id);

	if (napi_id_valid(napi_id) && READ_ONCE(ep->prefer_busy_poll))
		napi_suspend_irqs(napi_id);
}

static void ep_resume_napi_irqs(struct eventpoll *ep)
{
	unsigned int napi_id = READ_ONCE(ep->napi_id);

	if (napi_id_valid(napi_id) && READ_ONCE(ep->prefer_busy_poll))
		napi_resume_irqs(napi_id);
}

#else

static inline bool ep_busy_loop(struct eventpoll *ep)
{
	return false;
}

static inline void ep_set_busy_poll_napi_id(struct epitem *epi)
{
}

static long ep_eventpoll_bp_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	return -EOPNOTSUPP;
}

static void ep_suspend_napi_irqs(struct eventpoll *ep)
{
}

static void ep_resume_napi_irqs(struct eventpoll *ep)
{
}

#endif /* CONFIG_NET_RX_BUSY_POLL */

/*
 * As described in commit 0ccf831cb lockdep: annotate epoll
 * the use of wait queues used by epoll is done in a very controlled
 * manner. Wake ups can nest inside each other, but are never done
 * with the same locking. For example:
 *
 *   dfd = socket(...);
 *   efd1 = epoll_create();
 *   efd2 = epoll_create();
 *   epoll_ctl(efd1, EPOLL_CTL_ADD, dfd, ...);
 *   epoll_ctl(efd2, EPOLL_CTL_ADD, efd1, ...);
 *
 * When a packet arrives to the device underneath "dfd", the net code will
 * issue a wake_up() on its poll wake list. Epoll (efd1) has installed a
 * callback wakeup entry on that queue, and the wake_up() performed by the
 * "dfd" net code will end up in ep_poll_callback(). At this point epoll
 * (efd1) notices that it may have some event ready, so it needs to wake up
 * the waiters on its poll wait list (efd2). So it calls ep_poll_safewake()
 * that ends up in another wake_up(), after having checked about the
 * recursion constraints. That are, no more than EP_MAX_NESTS, to avoid
 * stack blasting.
 *
 * When CONFIG_DEBUG_LOCK_ALLOC is enabled, make sure lockdep can handle
 * this special case of epoll.
 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC

static void ep_poll_safewake(struct eventpoll *ep, struct epitem *epi,
			     unsigned pollflags)
{
	struct eventpoll *ep_src;
	unsigned long flags;
	u8 nests = 0;

	/*
	 * To set the subclass or nesting level for spin_lock_irqsave_nested()
	 * it might be natural to create a per-cpu nest count. However, since
	 * we can recurse on ep->poll_wait.lock, and a non-raw spinlock can
	 * schedule() in the -rt kernel, the per-cpu variable are no longer
	 * protected. Thus, we are introducing a per eventpoll nest field.
	 * If we are not being call from ep_poll_callback(), epi is NULL and
	 * we are at the first level of nesting, 0. Otherwise, we are being
	 * called from ep_poll_callback() and if a previous wakeup source is
	 * not an epoll file itself, we are at depth 1 since the wakeup source
	 * is depth 0. If the wakeup source is a previous epoll file in the
	 * wakeup chain then we use its nests value and record ours as
	 * nests + 1. The previous epoll file nests value is stable since its
	 * already holding its own poll_wait.lock.
	 */
	if (epi) {
		if ((is_file_epoll(epi->ffd.file))) {
			ep_src = epi->ffd.file->private_data;
			nests = ep_src->nests;
		} else {
			nests = 1;
		}
	}
	spin_lock_irqsave_nested(&ep->poll_wait.lock, flags, nests);
	ep->nests = nests + 1;
	wake_up_locked_poll(&ep->poll_wait, EPOLLIN | pollflags);
	ep->nests = 0;
	spin_unlock_irqrestore(&ep->poll_wait.lock, flags);
}

#else

static void ep_poll_safewake(struct eventpoll *ep, struct epitem *epi,
			     __poll_t pollflags)
{
	wake_up_poll(&ep->poll_wait, EPOLLIN | pollflags);
}

#endif

static void ep_remove_wait_queue(struct eppoll_entry *pwq)
{
	wait_queue_head_t *whead;

	rcu_read_lock();
	/*
	 * POLLFREE handshake, acquire side; see "POLLFREE handshake"
	 * at the top of this file.
	 *
	 * A NULL load is paired with the smp_store_release(&whead, NULL)
	 * in ep_poll_callback()'s POLLFREE branch: the teardown is
	 * complete and we must not touch whead again. On a non-NULL load
	 * rcu_read_lock() keeps the waitqueue memory alive (POLLFREE
	 * firers RCU-defer the free) and whead->lock inside
	 * remove_wait_queue() serializes us against the store side.
	 */
	whead = smp_load_acquire(&pwq->whead);
	if (whead)
		remove_wait_queue(whead, &pwq->wait);
	rcu_read_unlock();
}

/*
 * This function unregisters poll callbacks from the associated file
 * descriptor.  Must be called with "mtx" held.
 */
static void ep_unregister_pollwait(struct eventpoll *ep, struct epitem *epi)
{
	struct eppoll_entry **p = &epi->pwqlist;
	struct eppoll_entry *pwq;

	while ((pwq = *p) != NULL) {
		*p = pwq->next;
		ep_remove_wait_queue(pwq);
		kmem_cache_free(pwq_cache, pwq);
	}
}

/* call only when ep->mtx is held */
static inline struct wakeup_source *ep_wakeup_source(struct epitem *epi)
{
	return rcu_dereference_check(epi->ws, lockdep_is_held(&epi->ep->mtx));
}

/* call only when ep->mtx is held */
static inline void ep_pm_stay_awake(struct epitem *epi)
{
	struct wakeup_source *ws = ep_wakeup_source(epi);

	if (ws)
		__pm_stay_awake(ws);
}

static inline bool ep_has_wakeup_source(struct epitem *epi)
{
	return rcu_access_pointer(epi->ws) ? true : false;
}

/* call when ep->mtx cannot be held (ep_poll_callback) */
static inline void ep_pm_stay_awake_rcu(struct epitem *epi)
{
	struct wakeup_source *ws;

	rcu_read_lock();
	ws = rcu_dereference(epi->ws);
	if (ws)
		__pm_stay_awake(ws);
	rcu_read_unlock();
}


/*
 * ep->mutex needs to be held because we could be hit by
 * eventpoll_release_file() and epoll_ctl().
 */
static void ep_start_scan(struct eventpoll *ep, struct list_head *scan_batch)
{
	/*
	 * Steal the ready list, and re-init the original one to the
	 * empty list. Also, set ep->ovflist to NULL so that events
	 * happening while looping w/out locks, are not lost. We cannot
	 * have the poll callback to queue directly on ep->rdllist,
	 * because we want the "sproc" callback to be able to do it
	 * in a lockless way.
	 */
	lockdep_assert_irqs_enabled();
	spin_lock_irq(&ep->lock);
	write_seqcount_begin(&ep->seq);

	list_splice_init(&ep->rdllist, scan_batch);
	ep_enter_scan(ep);

	write_seqcount_end(&ep->seq);
	spin_unlock_irq(&ep->lock);
}

static void ep_done_scan(struct eventpoll *ep,
			 struct list_head *scan_batch)
{
	struct epitem *epi, *nepi;

	spin_lock_irq(&ep->lock);
	/*
	 * During the time we spent inside the "sproc" callback, some
	 * other events might have been queued by the poll callback.
	 * We re-insert them inside the main ready-list here.
	 */
	for (nepi = READ_ONCE(ep->ovflist); (epi = nepi) != NULL; ) {
		nepi = epi->ovflist_next;
		epi_clear_ovflist(epi);
		/*
		 * Skip items that the caller already returned via @scan_batch
		 * -- the list_splice() below takes care of those.
		 */
		if (!ep_is_linked(epi)) {
			/*
			 * ovflist is LIFO; list_add() head-insert here
			 * reverses the iteration order into FIFO.
			 */
			list_add(&epi->rdllink, &ep->rdllist);
			ep_pm_stay_awake(epi);
		}
	}

	write_seqcount_begin(&ep->seq);

	/* Back out of scan mode; callbacks target ep->rdllist again. */
	ep_exit_scan(ep);

	/*
	 * Quickly re-inject items left on "scan_batch".
	 */
	list_splice(scan_batch, &ep->rdllist);

	write_seqcount_end(&ep->seq);

	__pm_relax(ep->ws);

	if (!list_empty(&ep->rdllist)) {
		if (waitqueue_active(&ep->wq))
			wake_up(&ep->wq);
	}

	spin_unlock_irq(&ep->lock);
}

static void ep_get(struct eventpoll *ep)
{
	refcount_inc(&ep->refcount);
}

/*
 * Drop a reference to @ep; returns true iff it was the last, in which
 * case the caller is responsible for ep_free().
 */
static bool ep_put(struct eventpoll *ep)
{
	if (!refcount_dec_and_test(&ep->refcount))
		return false;

	WARN_ON_ONCE(!RB_EMPTY_ROOT(&ep->rbr.rb_root));
	return true;
}

static void ep_free(struct eventpoll *ep)
{
	ep_resume_napi_irqs(ep);
	mutex_destroy(&ep->mtx);
	free_uid(ep->user);
	wakeup_source_unregister(ep->ws);
	/* ep_get_upwards_depth_proc() may still hold epi->ep under RCU */
	kfree_rcu(ep, rcu);
}

/*
 * Pin @epi->ffd.file for operations that require both safe dereference
 * and exclusion from __fput().
 *
 * struct file uses SLAB_TYPESAFE_BY_RCU, so a freed slot can be
 * reassigned at any time. The bare load of epi->ffd.file is safe here
 * because the caller holds ep->mtx and eventpoll_release_file() blocks
 * on that mutex while tearing down the epi, so the backing file
 * allocation cannot be freed and reused under us. An rcu_read_lock()
 * is therefore unnecessary for the load.
 *
 * A successful file_ref_get() additionally blocks __fput() from
 * starting on this file: once the refcount has reached zero it cannot
 * come back. ep_remove() relies on that to touch file->f_lock and
 * file->f_ep without racing eventpoll_release_file() (see commit
 * a6dc643c6931). A NULL return means __fput() is already in flight;
 * the caller must bail without touching the file, and
 * eventpoll_release_file() will clean the epi up from its side.
 */
static struct file *epi_fget(const struct epitem *epi)
{
	struct file *file;

	file = epi->ffd.file;
	if (!file_ref_get(&file->f_ref))
		file = NULL;
	return file;
}

/*
 * Takes &file->f_lock; returns with it released.
 */
static void ep_remove_file(struct eventpoll *ep, struct epitem *epi,
			     struct file *file)
{
	struct epitems_head *to_free = NULL;
	struct hlist_head *head;

	lockdep_assert_held(&ep->mtx);

	spin_lock(&file->f_lock);
	head = file->f_ep;
	if (hlist_is_singular_node(&epi->fllink, head)) {
		/*
		 * Last watcher: publish NULL so the eventpoll_release()
		 * fastpath in include/linux/eventpoll.h can skip the slow
		 * path on a future __fput(). Safe because every f_ep writer
		 * either holds a pin on @file via epi_fget() or is __fput()
		 * itself -- see the comment in eventpoll_release().
		 */
		WRITE_ONCE(file->f_ep, NULL);
		if (!is_file_epoll(file)) {
			struct epitems_head *v;
			v = container_of(head, struct epitems_head, epitems);
			if (!smp_load_acquire(&v->next))
				to_free = v;
		}
	}
	hlist_del_rcu(&epi->fllink);
	spin_unlock(&file->f_lock);
	free_ephead(to_free);
}

static void ep_remove_epi(struct eventpoll *ep, struct epitem *epi)
{
	lockdep_assert_held(&ep->mtx);

	rb_erase_cached(&epi->rbn, &ep->rbr);

	spin_lock_irq(&ep->lock);
	if (ep_is_linked(epi))
		list_del_init(&epi->rdllink);
	spin_unlock_irq(&ep->lock);

	wakeup_source_unregister(ep_wakeup_source(epi));
	/*
	 * At this point it is safe to free the eventpoll item. Use the union
	 * field epi->rcu, since we are trying to minimize the size of
	 * 'struct epitem'. The 'rbn' field is no longer in use. Protected by
	 * ep->mtx. The rcu read side, reverse_path_check_proc(), does not make
	 * use of the rbn field.
	 */
	kfree_rcu(epi, rcu);

	percpu_counter_dec(&ep->user->epoll_watches);
}

/*
 * ep_remove variant for callers owing an additional reference to the ep
 */
static void ep_remove(struct eventpoll *ep, struct epitem *epi)
{
	struct file *file __free(fput) = NULL;

	lockdep_assert_irqs_enabled();
	lockdep_assert_held(&ep->mtx);

	ep_unregister_pollwait(ep, epi);

	/*
	 * If we manage to grab a reference it means we're not in
	 * eventpoll_release_file() and aren't going to be: once @file's
	 * refcount has reached zero, file_ref_get() cannot bring it back.
	 */
	file = epi_fget(epi);
	if (!file)
		return;

	ep_remove_file(ep, epi, file);
	ep_remove_epi(ep, epi);
	WARN_ON_ONCE(ep_put(ep));
}

/*
 * Pass 1 of ep_clear_and_put(): drain every epi's pwqlist.
 * ep_unregister_pollwait() takes each watched wait-queue head's lock,
 * which synchronizes with any in-flight ep_poll_callback(); after
 * this returns no callback can still be about to dereference an epi
 * on this ep. Must strictly precede ep_drain_tree() -- fusing the
 * two walks would let a callback queued on epi_i still fire after
 * epi_{i+k} had already been freed.
 */
static void ep_drain_pollwaits(struct eventpoll *ep)
{
	struct rb_node *rbp;
	struct epitem *epi;

	lockdep_assert_held(&ep->mtx);

	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);

		ep_unregister_pollwait(ep, epi);
		cond_resched();
	}
}

/*
 * Pass 2 of ep_clear_and_put(): ep_remove() every epi. The per-epi
 * pwqlist is already empty (ep_drain_pollwaits ran), but the rest of
 * ep_remove() still runs: epi_fget() pin, f_ep clear under f_lock,
 * rbtree erase, rdllist unlink, kfree_rcu(epi). rb_next() is captured
 * before each erase so the iteration is stable.
 *
 * A concurrent eventpoll_release_file() (removal path C) on a watched
 * file serializes with us via ep->mtx; ep_remove() transparently
 * hands off any epi whose file is in __fput() by bailing when
 * epi_fget() returns NULL, and path C will clean that epi up.
 */
static void ep_drain_tree(struct eventpoll *ep)
{
	struct rb_node *rbp, *next;
	struct epitem *epi;

	lockdep_assert_held(&ep->mtx);

	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = next) {
		next = rb_next(rbp);
		epi = rb_entry(rbp, struct epitem, rbn);
		ep_remove(ep, epi);
		cond_resched();
	}
}

/*
 * Removal path B (see "Removal paths" in the top-of-file banner):
 * close of the epoll fd itself, reached via ep_eventpoll_release().
 *
 * Two passes under ep->mtx: first ep_drain_pollwaits() quiesces
 * in-flight callbacks, then ep_drain_tree() frees the epis. The
 * ep->refcount is kept > 0 across the walk by the ep file's own
 * share, which we drop below; ep_free() runs iff we were the last
 * holder after the tree drained.
 */
static void ep_clear_and_put(struct eventpoll *ep)
{
	/* Release any threads blocked in poll-on-ep. */
	if (waitqueue_active(&ep->poll_wait))
		ep_poll_safewake(ep, NULL, 0);

	mutex_lock(&ep->mtx);
	ep_drain_pollwaits(ep);
	ep_drain_tree(ep);
	mutex_unlock(&ep->mtx);

	if (ep_put(ep))
		ep_free(ep);
}

static long ep_eventpoll_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	int ret;

	if (!is_file_epoll(file))
		return -EINVAL;

	switch (cmd) {
	case EPIOCSPARAMS:
	case EPIOCGPARAMS:
		ret = ep_eventpoll_bp_ioctl(file, cmd, arg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ep_eventpoll_release(struct inode *inode, struct file *file)
{
	struct eventpoll *ep = file->private_data;

	if (ep)
		ep_clear_and_put(ep);

	return 0;
}

static __poll_t ep_item_poll(const struct epitem *epi, poll_table *pt, int depth);

static __poll_t __ep_eventpoll_poll(struct file *file, poll_table *wait, int depth)
{
	struct eventpoll *ep = file->private_data;
	LIST_HEAD(scan_batch);
	struct epitem *epi, *tmp;
	poll_table pt;
	__poll_t res = 0;

	init_poll_funcptr(&pt, NULL);

	/* Insert inside our poll wait queue */
	poll_wait(file, &ep->poll_wait, wait);

	/*
	 * Proceed to find out if wanted events are really available inside
	 * the ready list.
	 */
	mutex_lock_nested(&ep->mtx, depth);
	ep_start_scan(ep, &scan_batch);
	list_for_each_entry_safe(epi, tmp, &scan_batch, rdllink) {
		if (ep_item_poll(epi, &pt, depth + 1)) {
			res = EPOLLIN | EPOLLRDNORM;
			break;
		} else {
			/*
			 * Item has been dropped into the ready list by the poll
			 * callback, but it's not actually ready, as far as
			 * caller requested events goes. We can remove it here.
			 */
			__pm_relax(ep_wakeup_source(epi));
			list_del_init(&epi->rdllink);
		}
	}
	ep_done_scan(ep, &scan_batch);
	mutex_unlock(&ep->mtx);
	return res;
}

/*
 * Differs from ep_eventpoll_poll() in that internal callers already have
 * the ep->mtx so we need to start from depth=1, such that mutex_lock_nested()
 * is correctly annotated.
 */
static __poll_t ep_item_poll(const struct epitem *epi, poll_table *pt,
				 int depth)
{
	struct file *file = epi_fget(epi);
	__poll_t res;

	/*
	 * We could return EPOLLERR | EPOLLHUP or something, but let's
	 * treat this more as "file doesn't exist, poll didn't happen".
	 */
	if (!file)
		return 0;

	pt->_key = epi->event.events;
	if (!is_file_epoll(file))
		res = vfs_poll(file, pt);
	else
		res = __ep_eventpoll_poll(file, pt, depth);
	fput(file);
	return res & epi->event.events;
}

static __poll_t ep_eventpoll_poll(struct file *file, poll_table *wait)
{
	return __ep_eventpoll_poll(file, wait, 0);
}

#ifdef CONFIG_PROC_FS
static void ep_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct eventpoll *ep = f->private_data;
	struct rb_node *rbp;

	mutex_lock(&ep->mtx);
	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		struct epitem *epi = rb_entry(rbp, struct epitem, rbn);
		struct inode *inode = file_inode(epi->ffd.file);

		seq_printf(m, "tfd: %8d events: %8x data: %16llx "
			   " pos:%lli ino:%llx sdev:%x\n",
			   epi->ffd.fd, epi->event.events,
			   (long long)epi->event.data,
			   (long long)epi->ffd.file->f_pos,
			   inode->i_ino, inode->i_sb->s_dev);
		if (seq_has_overflowed(m))
			break;
	}
	mutex_unlock(&ep->mtx);
}
#endif

/* File callbacks that implement the eventpoll file behaviour */
static const struct file_operations eventpoll_fops = {
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= ep_show_fdinfo,
#endif
	.release	= ep_eventpoll_release,
	.poll		= ep_eventpoll_poll,
	.llseek		= noop_llseek,
	.unlocked_ioctl	= ep_eventpoll_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

/*
 * This is called from eventpoll_release() to unlink files from the eventpoll
 * interface. We need to have this facility to cleanup correctly files that are
 * closed without being removed from the eventpoll interface.
 */
void eventpoll_release_file(struct file *file)
{
	struct eventpoll *ep;
	struct epitem *epi;

	/*
	 * A concurrent ep_remove() cannot outrace us: it pins @file via
	 * epi_fget(), which fails once __fput() has dropped the refcount
	 * to zero -- the path we're on. So any racing ep_remove() bails
	 * and leaves the epi for us to clean up here.
	 */
again:
	spin_lock(&file->f_lock);
	if (file->f_ep && file->f_ep->first) {
		epi = hlist_entry(file->f_ep->first, struct epitem, fllink);
		spin_unlock(&file->f_lock);

		/*
		 * ep access is safe as we still own a reference to the ep
		 * struct
		 */
		ep = epi->ep;
		mutex_lock(&ep->mtx);

		ep_unregister_pollwait(ep, epi);

		ep_remove_file(ep, epi, file);
		ep_remove_epi(ep, epi);

		mutex_unlock(&ep->mtx);

		if (ep_put(ep))
			ep_free(ep);
		goto again;
	}
	spin_unlock(&file->f_lock);
}

static int ep_alloc(struct eventpoll **pep)
{
	struct eventpoll *ep;

	ep = kzalloc_obj(*ep);
	if (unlikely(!ep))
		return -ENOMEM;

	mutex_init(&ep->mtx);
	spin_lock_init(&ep->lock);
	seqcount_spinlock_init(&ep->seq, &ep->lock);
	init_waitqueue_head(&ep->wq);
	init_waitqueue_head(&ep->poll_wait);
	INIT_LIST_HEAD(&ep->rdllist);
	ep->rbr = RB_ROOT_CACHED;
	ep->ovflist = EP_UNACTIVE_PTR;	/* not scanning */
	ep->user = get_current_user();
	refcount_set(&ep->refcount, 1);

	*pep = ep;

	return 0;
}

/*
 * Search the file inside the eventpoll tree. The RB tree operations
 * are protected by the "mtx" mutex, and ep_find() must be called with
 * "mtx" held.
 */
static struct epitem *ep_find(struct eventpoll *ep, struct epoll_key *tf)
{
	int kcmp;
	struct rb_node *rbp;
	struct epitem *epi, *epir = NULL;

	for (rbp = ep->rbr.rb_root.rb_node; rbp; ) {
		epi = rb_entry(rbp, struct epitem, rbn);
		kcmp = ep_cmp_ffd(tf, &epi->ffd);
		if (kcmp > 0)
			rbp = rbp->rb_right;
		else if (kcmp < 0)
			rbp = rbp->rb_left;
		else {
			epir = epi;
			break;
		}
	}

	return epir;
}

/*
 * This is the callback that is passed to the wait queue wakeup
 * mechanism. It is called by the stored file descriptors when they
 * have events to report.
 */
static int ep_poll_callback(wait_queue_entry_t *wait, unsigned mode, int sync, void *key)
{
	int pwake = 0;
	struct epitem *epi = ep_item_from_wait(wait);
	struct eventpoll *ep = epi->ep;
	__poll_t pollflags = key_to_poll(key);
	unsigned long flags;
	int ewake = 0;

	spin_lock_irqsave(&ep->lock, flags);

	ep_set_busy_poll_napi_id(epi);

	/*
	 * If the event mask does not contain any poll(2) event, we consider the
	 * descriptor to be disabled. This condition is likely the effect of the
	 * EPOLLONESHOT bit that disables the descriptor when an event is received,
	 * until the next EPOLL_CTL_MOD will be issued.
	 */
	if (!(epi->event.events & ~EP_PRIVATE_BITS))
		goto out_unlock;

	/*
	 * Check the events coming with the callback. At this stage, not
	 * every device reports the events in the "key" parameter of the
	 * callback. We need to be able to handle both cases here, hence the
	 * test for "key" != NULL before the event match test.
	 */
	if (pollflags && !(pollflags & epi->event.events))
		goto out_unlock;

	/*
	 * If we are transferring events to userspace, we can hold no locks
	 * (because we're accessing user memory, and because of linux f_op->poll()
	 * semantics). All the events that happen during that period of time are
	 * chained in ep->ovflist and requeued later on.
	 */
	if (ep_is_scanning(ep)) {
		if (!epi_on_ovflist(epi)) {
			epi->ovflist_next = READ_ONCE(ep->ovflist);
			WRITE_ONCE(ep->ovflist, epi);
			ep_pm_stay_awake_rcu(epi);
		}
	} else if (!ep_is_linked(epi)) {
		/* In the usual case, add event to ready list. */
		list_add_tail(&epi->rdllink, &ep->rdllist);
		ep_pm_stay_awake_rcu(epi);
	}

	/*
	 * Wake up ( if active ) both the eventpoll wait list and the ->poll()
	 * wait list.
	 */
	if (waitqueue_active(&ep->wq)) {
		if ((epi->event.events & EPOLLEXCLUSIVE) &&
					!(pollflags & POLLFREE)) {
			switch (pollflags & EPOLLINOUT_BITS) {
			case EPOLLIN:
				if (epi->event.events & EPOLLIN)
					ewake = 1;
				break;
			case EPOLLOUT:
				if (epi->event.events & EPOLLOUT)
					ewake = 1;
				break;
			case 0:
				ewake = 1;
				break;
			}
		}
		if (sync)
			wake_up_sync(&ep->wq);
		else
			wake_up(&ep->wq);
	}
	if (waitqueue_active(&ep->poll_wait))
		pwake++;

out_unlock:
	spin_unlock_irqrestore(&ep->lock, flags);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, epi, pollflags & EPOLL_URING_WAKE);

	if (!(epi->event.events & EPOLLEXCLUSIVE))
		ewake = 1;

	if (pollflags & POLLFREE) {
		/*
		 * POLLFREE handshake, release side; see "POLLFREE handshake"
		 * at the top of this file.
		 *
		 * Unlink our wait entry with list_del_init rather than
		 * __remove_wait_queue: a concurrent ep_remove_wait_queue()
		 * that already loaded a non-NULL whead may still call
		 * remove_wait_queue() after us, and list_del_init() tolerates
		 * the second delete.
		 *
		 * smp_store_release(&whead, NULL) publishes the teardown to
		 * ep_remove_wait_queue()'s smp_load_acquire(). Before this
		 * store, a racing ep_clear_and_put() / ep_remove() reaches
		 * ep_remove_wait_queue() which sees whead != NULL and takes
		 * whead->lock -- the same lock held by our caller, so it
		 * serializes behind us. Once whead is zeroed, nothing else
		 * protects ep / epi / wait.
		 */
		list_del_init(&wait->entry);
		smp_store_release(&ep_pwq_from_wait(wait)->whead, NULL);
	}

	return ewake;
}

/*
 * This is the callback that is used to add our wait queue to the
 * target file wakeup lists.
 */
static void ep_ptable_queue_proc(struct file *file, wait_queue_head_t *whead,
				 poll_table *pt)
{
	struct ep_pqueue *epq = container_of(pt, struct ep_pqueue, pt);
	struct epitem *epi = epq->epi;
	struct eppoll_entry *pwq;

	if (unlikely(!epi))	// an earlier allocation has failed
		return;

	pwq = kmem_cache_alloc(pwq_cache, GFP_KERNEL);
	if (unlikely(!pwq)) {
		epq->epi = NULL;
		return;
	}

	init_waitqueue_func_entry(&pwq->wait, ep_poll_callback);
	pwq->whead = whead;
	pwq->base = epi;
	if (epi->event.events & EPOLLEXCLUSIVE)
		add_wait_queue_exclusive(whead, &pwq->wait);
	else
		add_wait_queue(whead, &pwq->wait);
	pwq->next = epi->pwqlist;
	epi->pwqlist = pwq;
}

static void ep_rbtree_insert(struct eventpoll *ep, struct epitem *epi)
{
	int kcmp;
	struct rb_node **p = &ep->rbr.rb_root.rb_node, *parent = NULL;
	struct epitem *epic;
	bool leftmost = true;

	while (*p) {
		parent = *p;
		epic = rb_entry(parent, struct epitem, rbn);
		kcmp = ep_cmp_ffd(&epi->ffd, &epic->ffd);
		if (kcmp > 0) {
			p = &parent->rb_right;
			leftmost = false;
		} else
			p = &parent->rb_left;
	}
	rb_link_node(&epi->rbn, parent, p);
	rb_insert_color_cached(&epi->rbn, &ep->rbr, leftmost);
}



/*
 * Upper bound on wakeup paths emanating from any one watched file,
 * indexed by path depth (1..PATH_ARR_SIZE). For example, we allow
 * 1000 paths of length 1 from each watched file. These caps limit
 * the wakeup amplification that can be built from epoll-watches-
 * epoll topologies without rejecting reasonable usage.
 *
 * Enforced at EPOLL_CTL_ADD; CTL_MOD and CTL_DEL cannot add paths.
 * The running tallies live in ctx->path_count[] and are protected by
 * epnested_mutex.
 */
static const int path_limits[PATH_ARR_SIZE] = { 1000, 500, 100, 50, 10 };

static int path_count_inc(struct ep_ctl_ctx *ctx, int nests)
{
	/* Allow an arbitrary number of depth 1 paths */
	if (nests == 0)
		return 0;

	if (++ctx->path_count[nests] > path_limits[nests])
		return -1;
	return 0;
}

static void path_count_init(struct ep_ctl_ctx *ctx)
{
	int i;

	for (i = 0; i < PATH_ARR_SIZE; i++)
		ctx->path_count[i] = 0;
}

static int reverse_path_check_proc(struct ep_ctl_ctx *ctx,
				   struct hlist_head *refs, int depth)
{
	int error = 0;
	struct epitem *epi;

	if (depth > EP_MAX_NESTS) /* too deep nesting */
		return -1;

	/* CTL_DEL can remove links here, but that can't increase our count */
	hlist_for_each_entry_rcu(epi, refs, fllink) {
		struct hlist_head *refs = &epi->ep->refs;
		if (hlist_empty(refs))
			error = path_count_inc(ctx, depth);
		else
			error = reverse_path_check_proc(ctx, refs, depth + 1);
		if (error != 0)
			break;
	}
	return error;
}

/**
 * reverse_path_check - ctx->tfile_check_list is a list of epitems_head
 *                      anchoring files with newly proposed links; make
 *                      sure those links don't push any path-length bucket
 *                      over its limit in path_limits[].
 * @ctx: Per-do_epoll_ctl() scratch for the loop / path checks.
 *
 * Return: %zero if the proposed links don't create too many paths,
 *	    %-1 otherwise.
 */
static int reverse_path_check(struct ep_ctl_ctx *ctx)
{
	struct epitems_head *p;

	for (p = ctx->tfile_check_list; p != EP_UNACTIVE_PTR; p = p->next) {
		int error;
		path_count_init(ctx);
		rcu_read_lock();
		error = reverse_path_check_proc(ctx, &p->epitems, 0);
		rcu_read_unlock();
		if (error)
			return error;
	}
	return 0;
}

static int ep_create_wakeup_source(struct epitem *epi)
{
	struct name_snapshot n;
	struct wakeup_source *ws;

	if (!epi->ep->ws) {
		epi->ep->ws = wakeup_source_register(NULL, "eventpoll");
		if (!epi->ep->ws)
			return -ENOMEM;
	}

	take_dentry_name_snapshot(&n, epi->ffd.file->f_path.dentry);
	ws = wakeup_source_register(NULL, n.name.name);
	release_dentry_name_snapshot(&n);

	if (!ws)
		return -ENOMEM;
	rcu_assign_pointer(epi->ws, ws);

	return 0;
}

/* rare code path, only used when EPOLL_CTL_MOD removes a wakeup source */
static noinline void ep_destroy_wakeup_source(struct epitem *epi)
{
	struct wakeup_source *ws = ep_wakeup_source(epi);

	RCU_INIT_POINTER(epi->ws, NULL);

	/*
	 * wait for ep_pm_stay_awake_rcu to finish, synchronize_rcu is
	 * used internally by wakeup_source_remove, too (called by
	 * wakeup_source_unregister), so we cannot use call_rcu
	 */
	synchronize_rcu();
	wakeup_source_unregister(ws);
}

static int ep_attach_file(struct file *file, struct epitem *epi)
{
	struct epitems_head *to_free = NULL;
	struct hlist_head *head = NULL;
	struct eventpoll *ep = NULL;

	if (is_file_epoll(file))
		ep = file->private_data;

	if (ep) {
		head = &ep->refs;
	} else if (!READ_ONCE(file->f_ep)) {
allocate:
		to_free = kmem_cache_zalloc(ephead_cache, GFP_KERNEL);
		if (!to_free)
			return -ENOMEM;
		head = &to_free->epitems;
	}
	spin_lock(&file->f_lock);
	if (!file->f_ep) {
		if (unlikely(!head)) {
			spin_unlock(&file->f_lock);
			goto allocate;
		}
		/* See eventpoll_release() for details. */
		WRITE_ONCE(file->f_ep, head);
		to_free = NULL;
	}
	hlist_add_head_rcu(&epi->fllink, file->f_ep);
	spin_unlock(&file->f_lock);
	free_ephead(to_free);
	return 0;
}

/*
 * Charge the user's epoll_watches quota, allocate a fresh epitem for
 * @tf, and initialize its fields. The returned item is not yet linked
 * into any data structure; the caller must install it via
 * ep_register_epitem() (which takes over on success) or kmem_cache_free()
 * it and decrement epoll_watches on its own.
 *
 * Returns ERR_PTR(-ENOSPC) if the quota is exceeded, ERR_PTR(-ENOMEM)
 * if the slab allocation fails.
 */
static struct epitem *ep_alloc_epitem(struct eventpoll *ep,
				      const struct epoll_event *event,
				      struct epoll_key *tf)
{
	struct epitem *epi;

	if (unlikely(percpu_counter_compare(&ep->user->epoll_watches,
					    max_user_watches) >= 0))
		return ERR_PTR(-ENOSPC);
	percpu_counter_inc(&ep->user->epoll_watches);

	epi = kmem_cache_zalloc(epi_cache, GFP_KERNEL);
	if (unlikely(!epi)) {
		percpu_counter_dec(&ep->user->epoll_watches);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&epi->rdllink);
	epi->ep = ep;
	epi->ffd = *tf;
	epi->event = *event;
	epi_clear_ovflist(epi);

	return epi;
}

/*
 * Install @epi into its target file's f_ep hlist and into @ep's rbtree,
 * taking one additional reference on @ep for the lifetime of the item.
 *
 * If @tep is non-NULL, the target file is itself an eventpoll; we hold
 * tep->mtx at subclass 1 across the attach + rbtree insert to serialize
 * with the target side. RB tree ops are protected by @ep->mtx, which
 * the caller already holds.
 *
 * On failure the epi is freed and the epoll_watches counter decremented,
 * matching ep_alloc_epitem()'s allocation. After this returns
 * successfully, ep_insert()'s later error paths use ep_remove() for
 * unwind; that cannot drop @ep's refcount to zero because the ep file
 * itself still holds the original reference.
 */
static int ep_register_epitem(struct ep_ctl_ctx *ctx, struct eventpoll *ep,
			      struct epitem *epi, struct eventpoll *tep,
			      int full_check)
{
	struct file *tfile = epi->ffd.file;
	int error;

	if (tep)
		mutex_lock_nested(&tep->mtx, 1);

	error = ep_attach_file(tfile, epi);
	if (unlikely(error)) {
		if (tep)
			mutex_unlock(&tep->mtx);
		kmem_cache_free(epi_cache, epi);
		percpu_counter_dec(&ep->user->epoll_watches);
		return error;
	}

	if (full_check && !tep)
		list_file(tfile, ctx);

	ep_rbtree_insert(ep, epi);

	if (tep)
		mutex_unlock(&tep->mtx);

	ep_get(ep);
	return 0;
}

/*
 * Must be called with "mtx" held.
 */
static int ep_insert(struct ep_ctl_ctx *ctx, struct eventpoll *ep,
		     const struct epoll_event *event, struct epoll_key *tf,
		     int full_check)
{
	int error, pwake = 0;
	__poll_t revents;
	struct epitem *epi;
	struct ep_pqueue epq;
	struct eventpoll *tep = NULL;

	if (is_file_epoll(tf->file))
		tep = tf->file->private_data;

	lockdep_assert_irqs_enabled();

	epi = ep_alloc_epitem(ep, event, tf);
	if (IS_ERR(epi))
		return PTR_ERR(epi);

	error = ep_register_epitem(ctx, ep, epi, tep, full_check);
	if (error)
		return error;

	/* Reject the insert if the new link would create too many back-paths. */
	if (unlikely(full_check && reverse_path_check(ctx))) {
		ep_remove(ep, epi);
		return -EINVAL;
	}

	if (epi->event.events & EPOLLWAKEUP) {
		error = ep_create_wakeup_source(epi);
		if (error) {
			ep_remove(ep, epi);
			return error;
		}
	}

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
	revents = ep_item_poll(epi, &epq.pt, 1);

	/* ep_ptable_queue_proc() signals allocation failure by clearing epq.epi. */
	if (unlikely(!epq.epi)) {
		ep_remove(ep, epi);
		return -ENOMEM;
	}

	/* Drop the new item onto the ready list if it is already ready. */
	spin_lock_irq(&ep->lock);

	ep_set_busy_poll_napi_id(epi);

	if (revents && !ep_is_linked(epi)) {
		list_add_tail(&epi->rdllink, &ep->rdllist);
		ep_pm_stay_awake(epi);

		if (waitqueue_active(&ep->wq))
			wake_up(&ep->wq);
		if (waitqueue_active(&ep->poll_wait))
			pwake++;
	}

	spin_unlock_irq(&ep->lock);

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, NULL, 0);

	return 0;
}

/*
 * Modify the interest event mask by dropping an event if the new mask
 * has a match in the current file status. Must be called with "mtx" held.
 */
static int ep_modify(struct eventpoll *ep, struct epitem *epi,
		     const struct epoll_event *event)
{
	int pwake = 0;
	poll_table pt;

	lockdep_assert_irqs_enabled();

	init_poll_funcptr(&pt, NULL);

	/*
	 * Set the new event interest mask before calling f_op->poll();
	 * otherwise we might miss an event that happens between the
	 * f_op->poll() call and the new event set registering.
	 */
	epi->event.events = event->events; /* need barrier below */
	epi->event.data = event->data; /* protected by mtx */
	if (epi->event.events & EPOLLWAKEUP) {
		if (!ep_has_wakeup_source(epi))
			ep_create_wakeup_source(epi);
	} else if (ep_has_wakeup_source(epi)) {
		ep_destroy_wakeup_source(epi);
	}

	/*
	 * The following barrier has two effects:
	 *
	 * 1) Flush epi changes above to other CPUs.  This ensures
	 *    we do not miss events from ep_poll_callback if an
	 *    event occurs immediately after we call f_op->poll().
	 *    We need this because we did not take ep->lock while
	 *    changing epi above (but ep_poll_callback does take
	 *    ep->lock).
	 *
	 * 2) We also need to ensure we do not miss _past_ events
	 *    when calling f_op->poll().  This barrier also
	 *    pairs with the barrier in wq_has_sleeper (see
	 *    comments for wq_has_sleeper).
	 *
	 * This barrier will now guarantee ep_poll_callback or f_op->poll
	 * (or both) will notice the readiness of an item.
	 */
	smp_mb();

	/*
	 * Get current event bits. We can safely use the file* here because
	 * its usage count has been increased by the caller of this function.
	 * If the item is "hot" and it is not registered inside the ready
	 * list, push it inside.
	 */
	if (ep_item_poll(epi, &pt, 1)) {
		spin_lock_irq(&ep->lock);
		if (!ep_is_linked(epi)) {
			list_add_tail(&epi->rdllink, &ep->rdllist);
			ep_pm_stay_awake(epi);

			/* Notify waiting tasks that events are available */
			if (waitqueue_active(&ep->wq))
				wake_up(&ep->wq);
			if (waitqueue_active(&ep->poll_wait))
				pwake++;
		}
		spin_unlock_irq(&ep->lock);
	}

	/* We have to call this outside the lock */
	if (pwake)
		ep_poll_safewake(ep, NULL, 0);

	return 0;
}

/*
 * Attempt to deliver one event for @epi into @*uevents.
 *
 * Returns 1 if an event was delivered (with *uevents advanced to the
 * next slot), 0 if the re-poll reported no caller-requested events
 * (@epi drops out of the ready list; a future callback will re-add
 * it), or -EFAULT if copy_to_user() faulted (in which case @epi is
 * re-inserted at the head of @scan_batch so ep_done_scan() merges it
 * back to rdllist for the next attempt).
 *
 * PM bookkeeping and level-triggered re-queue are handled here.
 * Caller holds ep->mtx and the scan is active.
 */
static int ep_deliver_event(struct eventpoll *ep, struct epitem *epi,
			    poll_table *pt,
			    struct epoll_event __user **uevents,
			    struct list_head *scan_batch)
{
	struct epoll_event __user *next;
	struct wakeup_source *ws;
	__poll_t revents;

	/*
	 * Activate ep->ws before deactivating epi->ws to prevent
	 * triggering auto-suspend here (in case we reactivate epi->ws
	 * below).  Rearranging to delay the deactivation would let
	 * epi->ws drift out of sync with ep_is_linked().
	 */
	ws = ep_wakeup_source(epi);
	if (ws) {
		if (ws->active)
			__pm_stay_awake(ep->ws);
		__pm_relax(ws);
	}

	list_del_init(&epi->rdllink);

	/*
	 * Re-poll under ep->mtx so userspace cannot change the item
	 * out from under us. If no caller-requested events remain,
	 * @epi stays off the ready list; the poll callback will
	 * re-queue it when events next appear.
	 */
	revents = ep_item_poll(epi, pt, 1);
	if (!revents)
		return 0;

	next = epoll_put_uevent(revents, epi->event.data, *uevents);
	if (!next) {
		/*
		 * copy_to_user() faulted: put the item back so
		 * ep_done_scan() splices it onto rdllist for the next
		 * attempt.
		 */
		list_add(&epi->rdllink, scan_batch);
		ep_pm_stay_awake(epi);
		return -EFAULT;
	}
	*uevents = next;

	if (epi->event.events & EPOLLONESHOT) {
		epi->event.events &= EP_PRIVATE_BITS;
	} else if (!(epi->event.events & EPOLLET)) {
		/*
		 * Level-triggered: re-queue so the next epoll_wait()
		 * rechecks availability. We are the sole writer to
		 * rdllist here -- epoll_ctl() callers are locked out
		 * by ep->mtx, and the poll callback queues to ovflist
		 * during scans.
		 */
		list_add_tail(&epi->rdllink, &ep->rdllist);
		ep_pm_stay_awake(epi);
	}
	return 1;
}

static int ep_send_events(struct eventpoll *ep,
			  struct epoll_event __user *events, int maxevents)
{
	struct epitem *epi, *tmp;
	LIST_HEAD(scan_batch);
	poll_table pt;
	int res = 0;

	/*
	 * Always short-circuit for fatal signals to allow threads to make a
	 * timely exit without the chance of finding more events available and
	 * fetching repeatedly.
	 */
	if (fatal_signal_pending(current))
		return -EINTR;

	init_poll_funcptr(&pt, NULL);

	mutex_lock(&ep->mtx);
	ep_start_scan(ep, &scan_batch);

	/*
	 * We can loop without lock because we are passed a task-private
	 * scan_batch; items cannot vanish while we hold ep->mtx.
	 */
	list_for_each_entry_safe(epi, tmp, &scan_batch, rdllink) {
		int delivered;

		if (res >= maxevents)
			break;

		delivered = ep_deliver_event(ep, epi, &pt, &events, &scan_batch);
		if (delivered < 0) {
			if (!res)
				res = delivered;
			break;
		}
		res += delivered;
	}

	ep_done_scan(ep, &scan_batch);
	mutex_unlock(&ep->mtx);

	return res;
}

static struct timespec64 *ep_timeout_to_timespec(struct timespec64 *to, long ms)
{
	struct timespec64 now;

	if (ms < 0)
		return NULL;

	if (!ms) {
		to->tv_sec = 0;
		to->tv_nsec = 0;
		return to;
	}

	to->tv_sec = ms / MSEC_PER_SEC;
	to->tv_nsec = NSEC_PER_MSEC * (ms % MSEC_PER_SEC);

	ktime_get_ts64(&now);
	*to = timespec64_add_safe(now, *to);
	return to;
}

/*
 * autoremove_wake_function, but remove even on failure to wake up, because we
 * know that default_wake_function/ttwu will only fail if the thread is already
 * woken, and in that case the ep_poll loop will remove the entry anyways, not
 * try to reuse it.
 */
static int ep_autoremove_wake_function(struct wait_queue_entry *wq_entry,
				       unsigned int mode, int sync, void *key)
{
	int ret = default_wake_function(wq_entry, mode, sync, key);

	/*
	 * Pairs with list_empty_careful in ep_poll, and ensures future loop
	 * iterations see the cause of this wakeup.
	 */
	list_del_init_careful(&wq_entry->entry);
	return ret;
}

static int ep_try_send_events(struct eventpoll *ep,
			      struct epoll_event __user *events, int maxevents)
{
	int res;

	/*
	 * Try to transfer events to user space. In case we get 0 events and
	 * there's still timeout left over, we go trying again in search of
	 * more luck.
	 */
	res = ep_send_events(ep, events, maxevents);
	if (res > 0)
		ep_suspend_napi_irqs(ep);
	return res;
}

static int ep_schedule_timeout(ktime_t *to)
{
	if (to)
		return ktime_after(*to, ktime_get());
	else
		return 1;
}

/**
 * ep_poll - Retrieves ready events, and delivers them to the caller-supplied
 *           event buffer.
 *
 * @ep: Pointer to the eventpoll context.
 * @events: Pointer to the userspace buffer where the ready events should be
 *          stored.
 * @maxevents: Size (in terms of number of events) of the caller event buffer.
 * @timeout: Maximum timeout for the ready events fetch operation, in
 *           timespec. If the timeout is zero, the function will not block,
 *           while if the @timeout ptr is NULL, the function will block
 *           until at least one event has been retrieved (or an error
 *           occurred).
 *
 * Return: the number of ready events which have been fetched, or an
 *          error code, in case of error.
 */
static int ep_poll(struct eventpoll *ep, struct epoll_event __user *events,
		   int maxevents, struct timespec64 *timeout)
{
	int res, timed_out = 0;
	bool eavail;
	u64 slack = 0;
	wait_queue_entry_t wait;
	ktime_t expires, *to = NULL;

	lockdep_assert_irqs_enabled();

	if (timeout && (timeout->tv_sec | timeout->tv_nsec)) {
		slack = select_estimate_accuracy(timeout);
		to = &expires;
		*to = timespec64_to_ktime(*timeout);
	} else if (timeout) {
		/*
		 * Avoid the unnecessary trip to the wait queue loop, if the
		 * caller specified a non blocking operation.
		 */
		timed_out = 1;
	}

	/*
	 * This call is racy: We may or may not see events that are being added
	 * to the ready list under the lock (e.g., in IRQ callbacks). For cases
	 * with a non-zero timeout, this thread will check the ready list under
	 * lock and will add to the wait queue.  For cases with a zero
	 * timeout, the user by definition should not care and will have to
	 * recheck again.
	 */
	eavail = ep_events_available(ep);

	while (1) {
		if (eavail) {
			res = ep_try_send_events(ep, events, maxevents);
			if (res)
				return res;
		}

		if (timed_out)
			return 0;

		eavail = ep_busy_loop(ep);
		if (eavail)
			continue;

		if (signal_pending(current))
			return -EINTR;

		/*
		 * Internally init_wait() uses autoremove_wake_function(),
		 * thus wait entry is removed from the wait queue on each
		 * wakeup. Why it is important? In case of several waiters
		 * each new wakeup will hit the next waiter, giving it the
		 * chance to harvest new event. Otherwise wakeup can be
		 * lost. This is also good performance-wise, because on
		 * normal wakeup path no need to call __remove_wait_queue()
		 * explicitly, thus ep->lock is not taken, which halts the
		 * event delivery.
		 *
		 * In fact, we now use an even more aggressive function that
		 * unconditionally removes, because we don't reuse the wait
		 * entry between loop iterations. This lets us also avoid the
		 * performance issue if a process is killed, causing all of its
		 * threads to wake up without being removed normally.
		 */
		init_wait(&wait);
		wait.func = ep_autoremove_wake_function;

		spin_lock_irq(&ep->lock);
		/*
		 * Barrierless variant, waitqueue_active() is called under
		 * the same lock on wakeup ep_poll_callback() side, so it
		 * is safe to avoid an explicit barrier.
		 */
		__set_current_state(TASK_INTERRUPTIBLE);

		/*
		 * Do the final check under the lock. ep_start/done_scan()
		 * plays with two lists (->rdllist and ->ovflist) and there
		 * is always a race when both lists are empty for short
		 * period of time although events are pending, so lock is
		 * important.
		 */
		eavail = ep_events_available(ep);
		if (!eavail)
			__add_wait_queue_exclusive(&ep->wq, &wait);

		spin_unlock_irq(&ep->lock);

		if (!eavail)
			timed_out = !ep_schedule_timeout(to) ||
				!schedule_hrtimeout_range(to, slack,
							  HRTIMER_MODE_ABS);
		__set_current_state(TASK_RUNNING);

		/*
		 * We were woken up, thus go and try to harvest some events.
		 * If timed out and still on the wait queue, recheck eavail
		 * carefully under lock, below.
		 */
		eavail = true;

		if (!list_empty_careful(&wait.entry)) {
			spin_lock_irq(&ep->lock);
			/*
			 * If the thread timed out and is not on the wait queue,
			 * it means that the thread was woken up after its
			 * timeout expired before it could reacquire the lock.
			 * Thus, when wait.entry is empty, it needs to harvest
			 * events.
			 */
			if (timed_out)
				eavail = list_empty(&wait.entry);
			__remove_wait_queue(&ep->wq, &wait);
			spin_unlock_irq(&ep->lock);
		}
	}
}

/**
 * ep_loop_check_proc - verify that adding an epoll file @ep inside another
 *                      epoll file does not create closed loops, and
 *                      determine the depth of the subtree starting at @ep
 *
 * @ctx: Per-do_epoll_ctl() scratch for the loop / path checks.
 * @ep: the &struct eventpoll to be currently checked.
 * @depth: Current depth of the path being checked.
 *
 * Return: depth of the subtree, or a value bigger than EP_MAX_NESTS if we found
 * a loop or went too deep.
 */
static int ep_loop_check_proc(struct ep_ctl_ctx *ctx,
			      struct eventpoll *ep, int depth)
{
	int result = 0;
	struct rb_node *rbp;
	struct epitem *epi;

	if (ep->gen == loop_check_gen)
		return ep->loop_check_depth;

	mutex_lock_nested(&ep->mtx, depth + 1);
	ep->gen = loop_check_gen;
	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);
		if (unlikely(is_file_epoll(epi->ffd.file))) {
			struct eventpoll *ep_tovisit;
			ep_tovisit = epi->ffd.file->private_data;
			if (ep_tovisit == ctx->inserting_into ||
			    depth > EP_MAX_NESTS)
				result = EP_MAX_NESTS+1;
			else
				result = max(result,
					     ep_loop_check_proc(ctx, ep_tovisit,
								depth + 1) + 1);
			if (result > EP_MAX_NESTS)
				break;
		} else {
			/*
			 * A non-epoll leaf. Queue it for the companion
			 * reverse_path_check() that runs after this walk so
			 * any new links we propose don't add too many wakeup
			 * paths.
			 */
			list_file(epi->ffd.file, ctx);
		}
	}
	ep->loop_check_depth = result;
	mutex_unlock(&ep->mtx);

	return result;
}

/* ep_get_upwards_depth_proc - determine depth of @ep when traversed upwards */
static int ep_get_upwards_depth_proc(struct eventpoll *ep, int depth)
{
	int result = 0;
	struct epitem *epi;

	if (ep->gen == loop_check_gen)
		return ep->loop_check_depth;
	hlist_for_each_entry_rcu(epi, &ep->refs, fllink)
		result = max(result, ep_get_upwards_depth_proc(epi->ep, depth + 1) + 1);
	ep->gen = loop_check_gen;
	ep->loop_check_depth = result;
	return result;
}

/**
 * ep_loop_check - Performs a check to verify that adding an epoll file (@to)
 *                 into another epoll file (represented by @ep) does not create
 *                 closed loops or too deep chains.
 *
 * @ctx: Per-CTL_ADD scratch context.
 * @ep:  Pointer to the epoll we are inserting into.
 * @to:  Pointer to the epoll to be inserted.
 *
 * Return: %zero if adding the epoll @to inside the epoll @from
 * does not violate the constraints, or %-1 otherwise.
 */
static int ep_loop_check(struct ep_ctl_ctx *ctx, struct eventpoll *ep,
			 struct eventpoll *to)
{
	int depth, upwards_depth;

	ctx->inserting_into = ep;
	/*
	 * Check how deep down we can get from @to, and whether it is possible
	 * to loop up to @ep.
	 */
	depth = ep_loop_check_proc(ctx, to, 0);
	if (depth > EP_MAX_NESTS)
		return -1;
	/* Check how far up we can go from @ep. */
	rcu_read_lock();
	upwards_depth = ep_get_upwards_depth_proc(ep, 0);
	rcu_read_unlock();

	return (depth+1+upwards_depth > EP_MAX_NESTS) ? -1 : 0;
}

static void clear_tfile_check_list(struct ep_ctl_ctx *ctx)
{
	rcu_read_lock();
	while (ctx->tfile_check_list != EP_UNACTIVE_PTR) {
		struct epitems_head *head = ctx->tfile_check_list;
		ctx->tfile_check_list = head->next;
		unlist_file(head);
	}
	rcu_read_unlock();
}

/*
 * Open an eventpoll file descriptor.
 */
static int do_epoll_create(int flags)
{
	int error;
	struct eventpoll *ep;

	/* Check the EPOLL_* constant for consistency.  */
	BUILD_BUG_ON(EPOLL_CLOEXEC != O_CLOEXEC);

	if (flags & ~EPOLL_CLOEXEC)
		return -EINVAL;
	/*
	 * Create the internal data structure ("struct eventpoll").
	 */
	error = ep_alloc(&ep);
	if (error < 0)
		return error;
	/*
	 * Creates all the items needed to setup an eventpoll file. That is,
	 * a file structure and a free file descriptor.
	 */
	FD_PREPARE(fdf, O_RDWR | (flags & O_CLOEXEC),
		   anon_inode_getfile("[eventpoll]", &eventpoll_fops, ep,
				      O_RDWR | (flags & O_CLOEXEC)));
	if (fdf.err) {
		ep_clear_and_put(ep);
		return fdf.err;
	}
	ep->file = fd_prepare_file(fdf);
	return fd_publish(fdf);
}

SYSCALL_DEFINE1(epoll_create1, int, flags)
{
	return do_epoll_create(flags);
}

SYSCALL_DEFINE1(epoll_create, int, size)
{
	if (size <= 0)
		return -EINVAL;

	return do_epoll_create(0);
}

#ifdef CONFIG_PM_SLEEP
static inline void ep_take_care_of_epollwakeup(struct epoll_event *epev)
{
	if ((epev->events & EPOLLWAKEUP) && !capable(CAP_BLOCK_SUSPEND))
		epev->events &= ~EPOLLWAKEUP;
}
#else
static inline void ep_take_care_of_epollwakeup(struct epoll_event *epev)
{
	epev->events &= ~EPOLLWAKEUP;
}
#endif

static inline int epoll_mutex_lock(struct mutex *mutex, bool nonblock)
{
	if (!nonblock) {
		mutex_lock(mutex);
		return 0;
	}
	return mutex_trylock(mutex) ? 0 : -EAGAIN;
}

/*
 * Acquire the locks required for do_epoll_ctl() on @ep for @op.
 *
 * Always takes ep->mtx. For EPOLL_CTL_ADD, additionally runs the
 * loop / path check under epnested_mutex when the topology can
 * change: @ep is already watched (epfile->f_ep non-NULL), @ep was
 * recently loop-checked (ep->gen == loop_check_gen), or @tfile is
 * itself an eventpoll.
 *
 * Return value encodes both outcome and lock state:
 *
 *   0        success; ep->mtx held.
 *   1        success; ep->mtx held AND the full check ran under
 *            epnested_mutex (which is also still held). The value
 *            doubles as the @full_check argument to ep_insert().
 *   -errno   failure; no locks held.
 *
 * The caller releases what was taken with ep_ctl_unlock(ep, ret).
 *
 * Holding epnested_mutex on add is what prevents two racing
 * EPOLL_CTL_ADDs on different eps from building a cycle without
 * either walker observing it.
 */
static int ep_ctl_lock(struct ep_ctl_ctx *ctx, struct eventpoll *ep, int op,
		       struct file *epfile, struct file *tfile, bool nonblock)
{
	struct eventpoll *tep;
	int error;

	error = epoll_mutex_lock(&ep->mtx, nonblock);
	if (error)
		return error;

	if (op != EPOLL_CTL_ADD)
		return 0;
	if (!READ_ONCE(epfile->f_ep) && ep->gen != loop_check_gen &&
	    !is_file_epoll(tfile))
		return 0;

	/* Full check needed: drop ep->mtx so we can take epnested_mutex. */
	mutex_unlock(&ep->mtx);
	error = epoll_mutex_lock(&epnested_mutex, nonblock);
	if (error)
		return error;

	loop_check_gen++;

	if (is_file_epoll(tfile)) {
		tep = tfile->private_data;
		if (ep_loop_check(ctx, ep, tep) != 0) {
			error = -ELOOP;
			goto err_unlock_nested;
		}
	}

	error = epoll_mutex_lock(&ep->mtx, nonblock);
	if (error)
		goto err_unlock_nested;

	return 1;

err_unlock_nested:
	clear_tfile_check_list(ctx);
	loop_check_gen++;
	mutex_unlock(&epnested_mutex);
	return error;
}

static void ep_ctl_unlock(struct ep_ctl_ctx *ctx, struct eventpoll *ep,
			  int full_check)
{
	mutex_unlock(&ep->mtx);
	if (full_check) {
		clear_tfile_check_list(ctx);
		loop_check_gen++;
		mutex_unlock(&epnested_mutex);
	}
}

int do_epoll_ctl_file(struct file *f, int op, struct epoll_key *tf,
		      struct epoll_event *epds, bool nonblock)
{
	int error;
	int full_check;
	struct eventpoll *ep;
	struct epitem *epi;
	struct ep_ctl_ctx ctx = {
		.tfile_check_list = EP_UNACTIVE_PTR,
	};

	/* The target file descriptor must support poll */
	if (!file_can_poll(tf->file))
		return -EPERM;

	/* Check if EPOLLWAKEUP is allowed */
	if (ep_op_has_event(op))
		ep_take_care_of_epollwakeup(epds);

	/*
	 * The @f file must itself be an eventpoll, and we do not permit
	 * adding an epoll file descriptor inside itself.
	 */
	if (f == tf->file || !is_file_epoll(f))
		return -EINVAL;

	/*
	 * epoll adds to the wakeup queue at EPOLL_CTL_ADD time only,
	 * so EPOLLEXCLUSIVE is not allowed for a EPOLL_CTL_MOD operation.
	 * Also, nested exclusive wakeups are not supported.
	 */
	if (ep_op_has_event(op) && (epds->events & EPOLLEXCLUSIVE)) {
		if (op == EPOLL_CTL_MOD)
			return -EINVAL;
		if (op == EPOLL_CTL_ADD && (is_file_epoll(tf->file) ||
				(epds->events & ~EPOLLEXCLUSIVE_OK_BITS)))
			return -EINVAL;
	}

	ep = f->private_data;

	full_check = ep_ctl_lock(&ctx, ep, op, f, tf->file, nonblock);
	if (full_check < 0)
		return full_check;

	/*
	 * Look the target up in ep's RB tree. We hold ep->mtx, so the
	 * item stays valid until we release.
	 */
	epi = ep_find(ep, tf);

	error = -EINVAL;
	switch (op) {
	case EPOLL_CTL_ADD:
		if (!epi) {
			epds->events |= EPOLLERR | EPOLLHUP;
			error = ep_insert(&ctx, ep, epds, tf, full_check);
		} else
			error = -EEXIST;
		break;
	case EPOLL_CTL_DEL:
		if (epi) {
			/*
			 * The eventpoll itself is still alive: the refcount
			 * can't go to zero here.
			 */
			ep_remove(ep, epi);
			error = 0;
		} else {
			error = -ENOENT;
		}
		break;
	case EPOLL_CTL_MOD:
		if (epi) {
			if (!(epi->event.events & EPOLLEXCLUSIVE)) {
				epds->events |= EPOLLERR | EPOLLHUP;
				error = ep_modify(ep, epi, epds);
			}
		} else
			error = -ENOENT;
		break;
	}

	ep_ctl_unlock(&ctx, ep, full_check);
	return error;
}

int do_epoll_ctl(int epfd, int op, int fd, struct epoll_event *epds,
		 bool nonblock)
{
	struct epoll_key efd;

	CLASS(fd, f)(epfd);
	if (fd_empty(f))
		return -EBADF;

	/* Get the "struct file *" for the target file */
	CLASS(fd, tf)(fd);
	if (fd_empty(tf))
		return -EBADF;

	efd.file = fd_file(tf);
	efd.fd = fd;
	return do_epoll_ctl_file(fd_file(f), op, &efd, epds, nonblock);
}

/*
 * The following function implements the controller interface for
 * the eventpoll file that enables the insertion/removal/change of
 * file descriptors inside the interest set.
 */
SYSCALL_DEFINE4(epoll_ctl, int, epfd, int, op, int, fd,
		struct epoll_event __user *, event)
{
	struct epoll_event epds;

	if (ep_op_has_event(op) &&
	    copy_from_user(&epds, event, sizeof(struct epoll_event)))
		return -EFAULT;

	return do_epoll_ctl(epfd, op, fd, &epds, false);
}

static int ep_check_params(struct file *file, struct epoll_event __user *evs,
			   int maxevents)
{
	/* The maximum number of event must be greater than zero */
	if (maxevents <= 0 || maxevents > EP_MAX_EVENTS)
		return -EINVAL;

	/* Verify that the area passed by the user is writeable */
	if (!access_ok(evs, maxevents * sizeof(struct epoll_event)))
		return -EFAULT;

	/*
	 * We have to check that the file structure underneath the fd
	 * the user passed to us _is_ an eventpoll file.
	 */
	if (!is_file_epoll(file))
		return -EINVAL;

	return 0;
}

int epoll_sendevents(struct file *file, struct epoll_event __user *events,
		     int maxevents)
{
	struct eventpoll *ep;
	int ret;

	ret = ep_check_params(file, events, maxevents);
	if (unlikely(ret))
		return ret;

	ep = file->private_data;
	/*
	 * Racy call, but that's ok - it should get retried based on
	 * poll readiness anyway.
	 */
	if (ep_events_available(ep))
		return ep_try_send_events(ep, events, maxevents);
	return 0;
}

/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_wait(2).
 */
static int do_epoll_wait(int epfd, struct epoll_event __user *events,
			 int maxevents, struct timespec64 *to)
{
	struct eventpoll *ep;
	int ret;

	/* Get the "struct file *" for the eventpoll file */
	CLASS(fd, f)(epfd);
	if (fd_empty(f))
		return -EBADF;

	ret = ep_check_params(fd_file(f), events, maxevents);
	if (unlikely(ret))
		return ret;

	/*
	 * At this point it is safe to assume that the "private_data" contains
	 * our own data structure.
	 */
	ep = fd_file(f)->private_data;

	/* Time to fish for events ... */
	return ep_poll(ep, events, maxevents, to);
}

SYSCALL_DEFINE4(epoll_wait, int, epfd, struct epoll_event __user *, events,
		int, maxevents, int, timeout)
{
	struct timespec64 to;

	return do_epoll_wait(epfd, events, maxevents,
			     ep_timeout_to_timespec(&to, timeout));
}

/*
 * Implement the event wait interface for the eventpoll file. It is the kernel
 * part of the user space epoll_pwait(2).
 */
static int do_epoll_pwait(int epfd, struct epoll_event __user *events,
			  int maxevents, struct timespec64 *to,
			  const sigset_t __user *sigmask, size_t sigsetsize)
{
	int error;

	/*
	 * If the caller wants a certain signal mask to be set during the wait,
	 * we apply it here.
	 */
	error = set_user_sigmask(sigmask, sigsetsize);
	if (error)
		return error;

	error = do_epoll_wait(epfd, events, maxevents, to);

	restore_saved_sigmask_unless(error == -EINTR);

	return error;
}

SYSCALL_DEFINE6(epoll_pwait, int, epfd, struct epoll_event __user *, events,
		int, maxevents, int, timeout, const sigset_t __user *, sigmask,
		size_t, sigsetsize)
{
	struct timespec64 to;

	return do_epoll_pwait(epfd, events, maxevents,
			      ep_timeout_to_timespec(&to, timeout),
			      sigmask, sigsetsize);
}

SYSCALL_DEFINE6(epoll_pwait2, int, epfd, struct epoll_event __user *, events,
		int, maxevents, const struct __kernel_timespec __user *, timeout,
		const sigset_t __user *, sigmask, size_t, sigsetsize)
{
	struct timespec64 ts, *to = NULL;

	if (timeout) {
		if (get_timespec64(&ts, timeout))
			return -EFAULT;
		to = &ts;
		if (poll_select_set_timeout(to, ts.tv_sec, ts.tv_nsec))
			return -EINVAL;
	}

	return do_epoll_pwait(epfd, events, maxevents, to,
			      sigmask, sigsetsize);
}

#ifdef CONFIG_KCMP
static struct epitem *ep_find_tfd(struct eventpoll *ep, int tfd, unsigned long toff)
{
	struct rb_node *rbp;
	struct epitem *epi;

	for (rbp = rb_first_cached(&ep->rbr); rbp; rbp = rb_next(rbp)) {
		epi = rb_entry(rbp, struct epitem, rbn);
		if (epi->ffd.fd == tfd) {
			if (toff == 0)
				return epi;
			else
				toff--;
		}
		cond_resched();
	}

	return NULL;
}

struct file *get_epoll_tfile_raw_ptr(struct file *file, int tfd,
				     unsigned long toff)
{
	struct file *file_raw;
	struct eventpoll *ep;
	struct epitem *epi;

	if (!is_file_epoll(file))
		return ERR_PTR(-EINVAL);

	ep = file->private_data;

	mutex_lock(&ep->mtx);
	epi = ep_find_tfd(ep, tfd, toff);
	if (epi)
		file_raw = epi->ffd.file;
	else
		file_raw = ERR_PTR(-ENOENT);
	mutex_unlock(&ep->mtx);

	return file_raw;
}
#endif /* CONFIG_KCMP */

#ifdef CONFIG_COMPAT
static int do_compat_epoll_pwait(int epfd, struct epoll_event __user *events,
				 int maxevents, struct timespec64 *timeout,
				 const compat_sigset_t __user *sigmask,
				 compat_size_t sigsetsize)
{
	long err;

	/*
	 * If the caller wants a certain signal mask to be set during the wait,
	 * we apply it here.
	 */
	err = set_compat_user_sigmask(sigmask, sigsetsize);
	if (err)
		return err;

	err = do_epoll_wait(epfd, events, maxevents, timeout);

	restore_saved_sigmask_unless(err == -EINTR);

	return err;
}

COMPAT_SYSCALL_DEFINE6(epoll_pwait, int, epfd,
		       struct epoll_event __user *, events,
		       int, maxevents, int, timeout,
		       const compat_sigset_t __user *, sigmask,
		       compat_size_t, sigsetsize)
{
	struct timespec64 to;

	return do_compat_epoll_pwait(epfd, events, maxevents,
				     ep_timeout_to_timespec(&to, timeout),
				     sigmask, sigsetsize);
}

COMPAT_SYSCALL_DEFINE6(epoll_pwait2, int, epfd,
		       struct epoll_event __user *, events,
		       int, maxevents,
		       const struct __kernel_timespec __user *, timeout,
		       const compat_sigset_t __user *, sigmask,
		       compat_size_t, sigsetsize)
{
	struct timespec64 ts, *to = NULL;

	if (timeout) {
		if (get_timespec64(&ts, timeout))
			return -EFAULT;
		to = &ts;
		if (poll_select_set_timeout(to, ts.tv_sec, ts.tv_nsec))
			return -EINVAL;
	}

	return do_compat_epoll_pwait(epfd, events, maxevents, to,
				     sigmask, sigsetsize);
}

#endif

static int __init eventpoll_init(void)
{
	struct sysinfo si;

	si_meminfo(&si);
	/*
	 * Allows top 4% of lomem to be allocated for epoll watches (per user).
	 */
	max_user_watches = (((si.totalram - si.totalhigh) / 25) << PAGE_SHIFT) /
		EP_ITEM_COST;
	BUG_ON(max_user_watches < 0);

	/*
	 * We can have many thousands of epitems, so prevent this from
	 * using an extra cache line on 64-bit (and smaller) CPUs
	 */
	BUILD_BUG_ON(sizeof(void *) <= 8 && sizeof(struct epitem) > 128);

	/* Allocates slab cache used to allocate "struct epitem" items */
	epi_cache = kmem_cache_create("eventpoll_epi", sizeof(struct epitem),
			0, SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT, NULL);

	/* Allocates slab cache used to allocate "struct eppoll_entry" */
	pwq_cache = kmem_cache_create("eventpoll_pwq",
		sizeof(struct eppoll_entry), 0, SLAB_PANIC|SLAB_ACCOUNT, NULL);
	epoll_sysctls_init();

	ephead_cache = kmem_cache_create("ep_head",
		sizeof(struct epitems_head), 0, SLAB_PANIC|SLAB_ACCOUNT, NULL);

	return 0;
}
fs_initcall(eventpoll_init);
