#ifndef __LINUX__AIO_H
#define __LINUX__AIO_H

#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/aio_abi.h>
#include <linux/uio.h>
#include <linux/rcupdate.h>

#include <linux/atomic.h>

struct kioctx;

#define KIOCB_SYNC_KEY		(~0U)

/* ki_flags bits */
#define KIF_CANCELLED		2

#define kiocbSetCancelled(iocb)	set_bit(KIF_CANCELLED, &(iocb)->ki_flags)

#define kiocbClearCancelled(iocb)	clear_bit(KIF_CANCELLED, &(iocb)->ki_flags)

#define kiocbIsCancelled(iocb)	test_bit(KIF_CANCELLED, &(iocb)->ki_flags)

/* is there a better place to document function pointer methods? */
/**
 * ki_retry	-	iocb forward progress callback
 * @kiocb:	The kiocb struct to advance by performing an operation.
 *
 * This callback is called when the AIO core wants a given AIO operation
 * to make forward progress.  The kiocb argument describes the operation
 * that is to be performed.  As the operation proceeds, perhaps partially,
 * ki_retry is expected to update the kiocb with progress made.  Typically
 * ki_retry is set in the AIO core and it itself calls file_operations
 * helpers.
 *
 * ki_retry's return value determines when the AIO operation is completed
 * and an event is generated in the AIO event ring.  Except the special
 * return values described below, the value that is returned from ki_retry
 * is transferred directly into the completion ring as the operation's
 * resulting status.  Once this has happened ki_retry *MUST NOT* reference
 * the kiocb pointer again.
 *
 * If ki_retry returns -EIOCBQUEUED it has made a promise that aio_complete()
 * will be called on the kiocb pointer in the future.  The AIO core will
 * not ask the method again -- ki_retry must ensure forward progress.
 * aio_complete() must be called once and only once in the future, multiple
 * calls may result in undefined behaviour.
 */
struct kiocb {
	unsigned long		ki_flags;
	int			ki_users;
	unsigned		ki_key;		/* id of this request */

	struct file		*ki_filp;
	struct kioctx		*ki_ctx;	/* may be NULL for sync ops */
	int			(*ki_cancel)(struct kiocb *, struct io_event *);
	ssize_t			(*ki_retry)(struct kiocb *);
	void			(*ki_dtor)(struct kiocb *);

	union {
		void __user		*user;
		struct task_struct	*tsk;
	} ki_obj;

	__u64			ki_user_data;	/* user's data for completion */
	loff_t			ki_pos;

	void			*private;
	/* State that we remember to be able to restart/retry  */
	unsigned short		ki_opcode;
	size_t			ki_nbytes; 	/* copy of iocb->aio_nbytes */
	char 			__user *ki_buf;	/* remaining iocb->aio_buf */
	size_t			ki_left; 	/* remaining bytes */
	struct iovec		ki_inline_vec;	/* inline vector */
 	struct iovec		*ki_iovec;
 	unsigned long		ki_nr_segs;
 	unsigned long		ki_cur_seg;

	struct list_head	ki_list;	/* the aio core uses this
						 * for cancellation */
	struct list_head	ki_batch;	/* batch allocation */

	/*
	 * If the aio_resfd field of the userspace iocb is not zero,
	 * this is the underlying eventfd context to deliver events to.
	 */
	struct eventfd_ctx	*ki_eventfd;
};

static inline bool is_sync_kiocb(struct kiocb *kiocb)
{
	return kiocb->ki_key == KIOCB_SYNC_KEY;
}

static inline void init_sync_kiocb(struct kiocb *kiocb, struct file *filp)
{
	*kiocb = (struct kiocb) {
			.ki_users = 1,
			.ki_key = KIOCB_SYNC_KEY,
			.ki_filp = filp,
			.ki_obj.tsk = current,
		};
}

#define AIO_RING_MAGIC			0xa10a10a1
#define AIO_RING_COMPAT_FEATURES	1
#define AIO_RING_INCOMPAT_FEATURES	0
struct aio_ring {
	unsigned	id;	/* kernel internal index number */
	unsigned	nr;	/* number of io_events */
	unsigned	head;
	unsigned	tail;

	unsigned	magic;
	unsigned	compat_features;
	unsigned	incompat_features;
	unsigned	header_length;	/* size of aio_ring */


	struct io_event		io_events[0];
}; /* 128 bytes + ring size */

#define AIO_RING_PAGES	8
struct aio_ring_info {
	unsigned long		mmap_base;
	unsigned long		mmap_size;

	struct page		**ring_pages;
	spinlock_t		ring_lock;
	long			nr_pages;

	unsigned		nr, tail;

	struct page		*internal_pages[AIO_RING_PAGES];
};

static inline unsigned aio_ring_avail(struct aio_ring_info *info,
					struct aio_ring *ring)
{
	return (ring->head + info->nr - 1 - ring->tail) % info->nr;
}

struct kioctx {
	atomic_t		users;
	int			dead;

	/* This needs improving */
	unsigned long		user_id;
	struct hlist_node	list;

	wait_queue_head_t	wait;

	spinlock_t		ctx_lock;

	int			reqs_active;
	struct list_head	active_reqs;	/* used for cancellation */

	/* sys_io_setup currently limits this to an unsigned int */
	unsigned		max_reqs;

	struct aio_ring_info	ring_info;

	struct rcu_head		rcu_head;
};

/* prototypes */
#ifdef CONFIG_AIO
extern ssize_t wait_on_sync_kiocb(struct kiocb *iocb);
extern int aio_put_req(struct kiocb *iocb);
extern int aio_complete(struct kiocb *iocb, long res, long res2);
struct mm_struct;
extern void exit_aio(struct mm_struct *mm);
extern long do_io_submit(aio_context_t ctx_id, long nr,
			 struct iocb __user *__user *iocbpp, bool compat);
#else
static inline ssize_t wait_on_sync_kiocb(struct kiocb *iocb) { return 0; }
static inline int aio_put_req(struct kiocb *iocb) { return 0; }
static inline int aio_complete(struct kiocb *iocb, long res, long res2) { return 0; }
struct mm_struct;
static inline void exit_aio(struct mm_struct *mm) { }
static inline long do_io_submit(aio_context_t ctx_id, long nr,
				struct iocb __user * __user *iocbpp,
				bool compat) { return 0; }
#endif /* CONFIG_AIO */

static inline struct kiocb *list_kiocb(struct list_head *h)
{
	return list_entry(h, struct kiocb, ki_list);
}

/* for sysctl: */
extern unsigned long aio_nr;
extern unsigned long aio_max_nr;

#endif /* __LINUX__AIO_H */
