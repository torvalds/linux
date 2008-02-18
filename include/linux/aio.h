#ifndef __LINUX__AIO_H
#define __LINUX__AIO_H

#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/aio_abi.h>
#include <linux/uio.h>

#include <asm/atomic.h>
#include <linux/uio.h>

#define AIO_MAXSEGS		4
#define AIO_KIOGRP_NR_ATOMIC	8

struct kioctx;

/* Notes on cancelling a kiocb:
 *	If a kiocb is cancelled, aio_complete may return 0 to indicate 
 *	that cancel has not yet disposed of the kiocb.  All cancel 
 *	operations *must* call aio_put_req to dispose of the kiocb 
 *	to guard against races with the completion code.
 */
#define KIOCB_C_CANCELLED	0x01
#define KIOCB_C_COMPLETE	0x02

#define KIOCB_SYNC_KEY		(~0U)

/* ki_flags bits */
/*
 * This may be used for cancel/retry serialization in the future, but
 * for now it's unused and we probably don't want modules to even
 * think they can use it.
 */
/* #define KIF_LOCKED		0 */
#define KIF_KICKED		1
#define KIF_CANCELLED		2

#define kiocbTryLock(iocb)	test_and_set_bit(KIF_LOCKED, &(iocb)->ki_flags)
#define kiocbTryKick(iocb)	test_and_set_bit(KIF_KICKED, &(iocb)->ki_flags)

#define kiocbSetLocked(iocb)	set_bit(KIF_LOCKED, &(iocb)->ki_flags)
#define kiocbSetKicked(iocb)	set_bit(KIF_KICKED, &(iocb)->ki_flags)
#define kiocbSetCancelled(iocb)	set_bit(KIF_CANCELLED, &(iocb)->ki_flags)

#define kiocbClearLocked(iocb)	clear_bit(KIF_LOCKED, &(iocb)->ki_flags)
#define kiocbClearKicked(iocb)	clear_bit(KIF_KICKED, &(iocb)->ki_flags)
#define kiocbClearCancelled(iocb)	clear_bit(KIF_CANCELLED, &(iocb)->ki_flags)

#define kiocbIsLocked(iocb)	test_bit(KIF_LOCKED, &(iocb)->ki_flags)
#define kiocbIsKicked(iocb)	test_bit(KIF_KICKED, &(iocb)->ki_flags)
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
 *
 * If ki_retry returns -EIOCBRETRY it has made a promise that kick_iocb()
 * will be called on the kiocb pointer in the future.  This may happen
 * through generic helpers that associate kiocb->ki_wait with a wait
 * queue head that ki_retry uses via current->io_wait.  It can also happen
 * with custom tracking and manual calls to kick_iocb(), though that is
 * discouraged.  In either case, kick_iocb() must be called once and only
 * once.  ki_retry must ensure forward progress, the AIO core will wait
 * indefinitely for kick_iocb() to be called.
 */
struct kiocb {
	struct list_head	ki_run_list;
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
	wait_queue_t		ki_wait;
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

	/*
	 * If the aio_resfd field of the userspace iocb is not zero,
	 * this is the underlying file* to deliver event to.
	 */
	struct file		*ki_eventfd;
};

#define is_sync_kiocb(iocb)	((iocb)->ki_key == KIOCB_SYNC_KEY)
#define init_sync_kiocb(x, filp)			\
	do {						\
		struct task_struct *tsk = current;	\
		(x)->ki_flags = 0;			\
		(x)->ki_users = 1;			\
		(x)->ki_key = KIOCB_SYNC_KEY;		\
		(x)->ki_filp = (filp);			\
		(x)->ki_ctx = NULL;			\
		(x)->ki_cancel = NULL;			\
		(x)->ki_retry = NULL;			\
		(x)->ki_dtor = NULL;			\
		(x)->ki_obj.tsk = tsk;			\
		(x)->ki_user_data = 0;                  \
		init_wait((&(x)->ki_wait));             \
	} while (0)

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

#define aio_ring_avail(info, ring)	(((ring)->head + (info)->nr - 1 - (ring)->tail) % (info)->nr)

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

struct kioctx {
	atomic_t		users;
	int			dead;
	struct mm_struct	*mm;

	/* This needs improving */
	unsigned long		user_id;
	struct kioctx		*next;

	wait_queue_head_t	wait;

	spinlock_t		ctx_lock;

	int			reqs_active;
	struct list_head	active_reqs;	/* used for cancellation */
	struct list_head	run_list;	/* used for kicked reqs */

	/* sys_io_setup currently limits this to an unsigned int */
	unsigned		max_reqs;

	struct aio_ring_info	ring_info;

	struct delayed_work	wq;
};

/* prototypes */
extern unsigned aio_max_size;

extern ssize_t wait_on_sync_kiocb(struct kiocb *iocb);
extern int aio_put_req(struct kiocb *iocb);
extern void kick_iocb(struct kiocb *iocb);
extern int aio_complete(struct kiocb *iocb, long res, long res2);
extern void __put_ioctx(struct kioctx *ctx);
struct mm_struct;
extern void exit_aio(struct mm_struct *mm);
extern struct kioctx *lookup_ioctx(unsigned long ctx_id);
extern int io_submit_one(struct kioctx *ctx, struct iocb __user *user_iocb,
			 struct iocb *iocb);

/* semi private, but used by the 32bit emulations: */
struct kioctx *lookup_ioctx(unsigned long ctx_id);
int io_submit_one(struct kioctx *ctx, struct iocb __user *user_iocb,
		  struct iocb *iocb);

#define get_ioctx(kioctx) do {						\
	BUG_ON(atomic_read(&(kioctx)->users) <= 0);			\
	atomic_inc(&(kioctx)->users);					\
} while (0)
#define put_ioctx(kioctx) do {						\
	BUG_ON(atomic_read(&(kioctx)->users) <= 0);			\
	if (unlikely(atomic_dec_and_test(&(kioctx)->users))) 		\
		__put_ioctx(kioctx);					\
} while (0)

#define io_wait_to_kiocb(wait) container_of(wait, struct kiocb, ki_wait)

#include <linux/aio_abi.h>

static inline struct kiocb *list_kiocb(struct list_head *h)
{
	return list_entry(h, struct kiocb, ki_list);
}

/* for sysctl: */
extern unsigned long aio_nr;
extern unsigned long aio_max_nr;

#endif /* __LINUX__AIO_H */
