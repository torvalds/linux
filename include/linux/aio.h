#ifndef __LINUX__AIO_H
#define __LINUX__AIO_H

#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/aio_abi.h>

#include <asm/atomic.h>

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
#define KIF_LOCKED		0
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

struct kiocb {
	struct list_head	ki_run_list;
	long			ki_flags;
	int			ki_users;
	unsigned		ki_key;		/* id of this request */

	struct file		*ki_filp;
	struct kioctx		*ki_ctx;	/* may be NULL for sync ops */
	int			(*ki_cancel)(struct kiocb *, struct io_event *);
	ssize_t			(*ki_retry)(struct kiocb *);
	void			(*ki_dtor)(struct kiocb *);

	struct list_head	ki_list;	/* the aio core uses this
						 * for cancellation */

	union {
		void __user		*user;
		struct task_struct	*tsk;
	} ki_obj;
	__u64			ki_user_data;	/* user's data for completion */
	loff_t			ki_pos;
	/* State that we remember to be able to restart/retry  */
	unsigned short		ki_opcode;
	size_t			ki_nbytes; 	/* copy of iocb->aio_nbytes */
	char 			__user *ki_buf;	/* remaining iocb->aio_buf */
	size_t			ki_left; 	/* remaining bytes */
	wait_queue_t		ki_wait;
	long			ki_retried; 	/* just for testing */
	long			ki_kicked; 	/* just for testing */
	long			ki_queued; 	/* just for testing */

	void			*private;
};

#define is_sync_kiocb(iocb)	((iocb)->ki_key == KIOCB_SYNC_KEY)
#define init_sync_kiocb(x, filp)			\
	do {						\
		struct task_struct *tsk = current;	\
		(x)->ki_flags = 0;			\
		(x)->ki_users = 1;			\
		(x)->ki_key = KIOCB_SYNC_KEY;		\
		(x)->ki_filp = (filp);			\
		(x)->ki_ctx = &tsk->active_mm->default_kioctx;	\
		(x)->ki_cancel = NULL;			\
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

	unsigned		max_reqs;

	struct aio_ring_info	ring_info;

	struct work_struct	wq;
};

/* prototypes */
extern unsigned aio_max_size;

extern ssize_t FASTCALL(wait_on_sync_kiocb(struct kiocb *iocb));
extern int FASTCALL(aio_put_req(struct kiocb *iocb));
extern void FASTCALL(kick_iocb(struct kiocb *iocb));
extern int FASTCALL(aio_complete(struct kiocb *iocb, long res, long res2));
extern void FASTCALL(__put_ioctx(struct kioctx *ctx));
struct mm_struct;
extern void FASTCALL(exit_aio(struct mm_struct *mm));
extern struct kioctx *lookup_ioctx(unsigned long ctx_id);
extern int FASTCALL(io_submit_one(struct kioctx *ctx,
			struct iocb __user *user_iocb, struct iocb *iocb));

/* semi private, but used by the 32bit emulations: */
struct kioctx *lookup_ioctx(unsigned long ctx_id);
int FASTCALL(io_submit_one(struct kioctx *ctx, struct iocb __user *user_iocb,
				  struct iocb *iocb));

#define get_ioctx(kioctx)	do { if (unlikely(atomic_read(&(kioctx)->users) <= 0)) BUG(); atomic_inc(&(kioctx)->users); } while (0)
#define put_ioctx(kioctx)	do { if (unlikely(atomic_dec_and_test(&(kioctx)->users))) __put_ioctx(kioctx); else if (unlikely(atomic_read(&(kioctx)->users) < 0)) BUG(); } while (0)

#define in_aio() !is_sync_wait(current->io_wait)
/* may be used for debugging */
#define warn_if_async()							\
do {									\
	if (in_aio()) {							\
		printk(KERN_ERR "%s(%s:%d) called in async context!\n",	\
			__FUNCTION__, __FILE__, __LINE__);		\
		dump_stack();						\
	}								\
} while (0)

#define io_wait_to_kiocb(wait) container_of(wait, struct kiocb, ki_wait)
#define is_retried_kiocb(iocb) ((iocb)->ki_retried > 1)

#include <linux/aio_abi.h>

static inline struct kiocb *list_kiocb(struct list_head *h)
{
	return list_entry(h, struct kiocb, ki_list);
}

/* for sysctl: */
extern atomic_t aio_nr;
extern unsigned aio_max_nr;

#endif /* __LINUX__AIO_H */
