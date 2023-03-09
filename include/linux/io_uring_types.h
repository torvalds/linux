#ifndef IO_URING_TYPES_H
#define IO_URING_TYPES_H

#include <linux/blkdev.h>
#include <linux/task_work.h>
#include <linux/bitmap.h>
#include <linux/llist.h>
#include <uapi/linux/io_uring.h>

struct io_wq_work_node {
	struct io_wq_work_node *next;
};

struct io_wq_work_list {
	struct io_wq_work_node *first;
	struct io_wq_work_node *last;
};

struct io_wq_work {
	struct io_wq_work_node list;
	unsigned flags;
	/* place it here instead of io_kiocb as it fills padding and saves 4B */
	int cancel_seq;
};

struct io_fixed_file {
	/* file * with additional FFS_* flags */
	unsigned long file_ptr;
};

struct io_file_table {
	struct io_fixed_file *files;
	unsigned long *bitmap;
	unsigned int alloc_hint;
};

struct io_hash_bucket {
	spinlock_t		lock;
	struct hlist_head	list;
} ____cacheline_aligned_in_smp;

struct io_hash_table {
	struct io_hash_bucket	*hbs;
	unsigned		hash_bits;
};

/*
 * Arbitrary limit, can be raised if need be
 */
#define IO_RINGFD_REG_MAX 16

struct io_uring_task {
	/* submission side */
	int				cached_refs;
	const struct io_ring_ctx 	*last;
	struct io_wq			*io_wq;
	struct file			*registered_rings[IO_RINGFD_REG_MAX];

	struct xarray			xa;
	struct wait_queue_head		wait;
	atomic_t			in_idle;
	atomic_t			inflight_tracked;
	struct percpu_counter		inflight;

	struct { /* task_work */
		struct llist_head	task_list;
		struct callback_head	task_work;
	} ____cacheline_aligned_in_smp;
};

struct io_uring {
	u32 head ____cacheline_aligned_in_smp;
	u32 tail ____cacheline_aligned_in_smp;
};

/*
 * This data is shared with the application through the mmap at offsets
 * IORING_OFF_SQ_RING and IORING_OFF_CQ_RING.
 *
 * The offsets to the member fields are published through struct
 * io_sqring_offsets when calling io_uring_setup.
 */
struct io_rings {
	/*
	 * Head and tail offsets into the ring; the offsets need to be
	 * masked to get valid indices.
	 *
	 * The kernel controls head of the sq ring and the tail of the cq ring,
	 * and the application controls tail of the sq ring and the head of the
	 * cq ring.
	 */
	struct io_uring		sq, cq;
	/*
	 * Bitmasks to apply to head and tail offsets (constant, equals
	 * ring_entries - 1)
	 */
	u32			sq_ring_mask, cq_ring_mask;
	/* Ring sizes (constant, power of 2) */
	u32			sq_ring_entries, cq_ring_entries;
	/*
	 * Number of invalid entries dropped by the kernel due to
	 * invalid index stored in array
	 *
	 * Written by the kernel, shouldn't be modified by the
	 * application (i.e. get number of "new events" by comparing to
	 * cached value).
	 *
	 * After a new SQ head value was read by the application this
	 * counter includes all submissions that were dropped reaching
	 * the new SQ head (and possibly more).
	 */
	u32			sq_dropped;
	/*
	 * Runtime SQ flags
	 *
	 * Written by the kernel, shouldn't be modified by the
	 * application.
	 *
	 * The application needs a full memory barrier before checking
	 * for IORING_SQ_NEED_WAKEUP after updating the sq tail.
	 */
	atomic_t		sq_flags;
	/*
	 * Runtime CQ flags
	 *
	 * Written by the application, shouldn't be modified by the
	 * kernel.
	 */
	u32			cq_flags;
	/*
	 * Number of completion events lost because the queue was full;
	 * this should be avoided by the application by making sure
	 * there are not more requests pending than there is space in
	 * the completion queue.
	 *
	 * Written by the kernel, shouldn't be modified by the
	 * application (i.e. get number of "new events" by comparing to
	 * cached value).
	 *
	 * As completion events come in out of order this counter is not
	 * ordered with any other data.
	 */
	u32			cq_overflow;
	/*
	 * Ring buffer of completion events.
	 *
	 * The kernel writes completion events fresh every time they are
	 * produced, so the application is allowed to modify pending
	 * entries.
	 */
	struct io_uring_cqe	cqes[] ____cacheline_aligned_in_smp;
};

struct io_restriction {
	DECLARE_BITMAP(register_op, IORING_REGISTER_LAST);
	DECLARE_BITMAP(sqe_op, IORING_OP_LAST);
	u8 sqe_flags_allowed;
	u8 sqe_flags_required;
	bool registered;
};

struct io_submit_link {
	struct io_kiocb		*head;
	struct io_kiocb		*last;
};

struct io_submit_state {
	/* inline/task_work completion list, under ->uring_lock */
	struct io_wq_work_node	free_list;
	/* batch completion logic */
	struct io_wq_work_list	compl_reqs;
	struct io_submit_link	link;

	bool			plug_started;
	bool			need_plug;
	unsigned short		submit_nr;
	unsigned int		cqes_count;
	struct blk_plug		plug;
	struct io_uring_cqe	cqes[16];
};

struct io_ev_fd {
	struct eventfd_ctx	*cq_ev_fd;
	unsigned int		eventfd_async: 1;
	struct rcu_head		rcu;
	atomic_t		refs;
	atomic_t		ops;
};

struct io_alloc_cache {
	struct hlist_head	list;
	unsigned int		nr_cached;
};

struct io_ring_ctx {
	/* const or read-mostly hot data */
	struct {
		unsigned int		flags;
		unsigned int		drain_next: 1;
		unsigned int		restricted: 1;
		unsigned int		off_timeout_used: 1;
		unsigned int		drain_active: 1;
		unsigned int		has_evfd: 1;
		/* all CQEs should be posted only by the submitter task */
		unsigned int		task_complete: 1;
		unsigned int		syscall_iopoll: 1;
		unsigned int		poll_activated: 1;
		unsigned int		drain_disabled: 1;
		unsigned int		compat: 1;

		enum task_work_notify_mode	notify_method;
		struct io_rings			*rings;
		struct task_struct		*submitter_task;
		struct percpu_ref		refs;
	} ____cacheline_aligned_in_smp;

	/* submission data */
	struct {
		struct mutex		uring_lock;

		/*
		 * Ring buffer of indices into array of io_uring_sqe, which is
		 * mmapped by the application using the IORING_OFF_SQES offset.
		 *
		 * This indirection could e.g. be used to assign fixed
		 * io_uring_sqe entries to operations and only submit them to
		 * the queue when needed.
		 *
		 * The kernel modifies neither the indices array nor the entries
		 * array.
		 */
		u32			*sq_array;
		struct io_uring_sqe	*sq_sqes;
		unsigned		cached_sq_head;
		unsigned		sq_entries;

		/*
		 * Fixed resources fast path, should be accessed only under
		 * uring_lock, and updated through io_uring_register(2)
		 */
		struct io_rsrc_node	*rsrc_node;
		int			rsrc_cached_refs;
		atomic_t		cancel_seq;
		struct io_file_table	file_table;
		unsigned		nr_user_files;
		unsigned		nr_user_bufs;
		struct io_mapped_ubuf	**user_bufs;

		struct io_submit_state	submit_state;

		struct io_buffer_list	*io_bl;
		struct xarray		io_bl_xa;
		struct list_head	io_buffers_cache;

		struct io_hash_table	cancel_table_locked;
		struct list_head	cq_overflow_list;
		struct io_alloc_cache	apoll_cache;
		struct io_alloc_cache	netmsg_cache;
	} ____cacheline_aligned_in_smp;

	/* IRQ completion list, under ->completion_lock */
	struct io_wq_work_list	locked_free_list;
	unsigned int		locked_free_nr;

	const struct cred	*sq_creds;	/* cred used for __io_sq_thread() */
	struct io_sq_data	*sq_data;	/* if using sq thread polling */

	struct wait_queue_head	sqo_sq_wait;
	struct list_head	sqd_list;

	unsigned long		check_cq;

	unsigned int		file_alloc_start;
	unsigned int		file_alloc_end;

	struct xarray		personalities;
	u32			pers_next;

	struct {
		/*
		 * We cache a range of free CQEs we can use, once exhausted it
		 * should go through a slower range setup, see __io_get_cqe()
		 */
		struct io_uring_cqe	*cqe_cached;
		struct io_uring_cqe	*cqe_sentinel;

		unsigned		cached_cq_tail;
		unsigned		cq_entries;
		struct io_ev_fd	__rcu	*io_ev_fd;
		struct wait_queue_head	cq_wait;
		unsigned		cq_extra;
	} ____cacheline_aligned_in_smp;

	struct {
		spinlock_t		completion_lock;

		bool			poll_multi_queue;
		bool			cq_waiting;

		/*
		 * ->iopoll_list is protected by the ctx->uring_lock for
		 * io_uring instances that don't use IORING_SETUP_SQPOLL.
		 * For SQPOLL, only the single threaded io_sq_thread() will
		 * manipulate the list, hence no extra locking is needed there.
		 */
		struct io_wq_work_list	iopoll_list;
		struct io_hash_table	cancel_table;

		struct llist_head	work_llist;

		struct list_head	io_buffers_comp;
	} ____cacheline_aligned_in_smp;

	/* timeouts */
	struct {
		spinlock_t		timeout_lock;
		atomic_t		cq_timeouts;
		struct list_head	timeout_list;
		struct list_head	ltimeout_list;
		unsigned		cq_last_tm_flush;
	} ____cacheline_aligned_in_smp;

	/* Keep this last, we don't need it for the fast path */
	struct wait_queue_head		poll_wq;
	struct io_restriction		restrictions;

	/* slow path rsrc auxilary data, used by update/register */
	struct io_rsrc_node		*rsrc_backup_node;
	struct io_mapped_ubuf		*dummy_ubuf;
	struct io_rsrc_data		*file_data;
	struct io_rsrc_data		*buf_data;

	struct delayed_work		rsrc_put_work;
	struct callback_head		rsrc_put_tw;
	struct llist_head		rsrc_put_llist;
	struct list_head		rsrc_ref_list;
	spinlock_t			rsrc_ref_lock;

	struct list_head		io_buffers_pages;

	#if defined(CONFIG_UNIX)
		struct socket		*ring_sock;
	#endif
	/* hashed buffered write serialization */
	struct io_wq_hash		*hash_map;

	/* Only used for accounting purposes */
	struct user_struct		*user;
	struct mm_struct		*mm_account;

	/* ctx exit and cancelation */
	struct llist_head		fallback_llist;
	struct delayed_work		fallback_work;
	struct work_struct		exit_work;
	struct list_head		tctx_list;
	struct completion		ref_comp;

	/* io-wq management, e.g. thread count */
	u32				iowq_limits[2];
	bool				iowq_limits_set;

	struct callback_head		poll_wq_task_work;
	struct list_head		defer_list;
	unsigned			sq_thread_idle;
	/* protected by ->completion_lock */
	unsigned			evfd_last_cq_tail;
};

enum {
	REQ_F_FIXED_FILE_BIT	= IOSQE_FIXED_FILE_BIT,
	REQ_F_IO_DRAIN_BIT	= IOSQE_IO_DRAIN_BIT,
	REQ_F_LINK_BIT		= IOSQE_IO_LINK_BIT,
	REQ_F_HARDLINK_BIT	= IOSQE_IO_HARDLINK_BIT,
	REQ_F_FORCE_ASYNC_BIT	= IOSQE_ASYNC_BIT,
	REQ_F_BUFFER_SELECT_BIT	= IOSQE_BUFFER_SELECT_BIT,
	REQ_F_CQE_SKIP_BIT	= IOSQE_CQE_SKIP_SUCCESS_BIT,

	/* first byte is taken by user flags, shift it to not overlap */
	REQ_F_FAIL_BIT		= 8,
	REQ_F_INFLIGHT_BIT,
	REQ_F_CUR_POS_BIT,
	REQ_F_NOWAIT_BIT,
	REQ_F_LINK_TIMEOUT_BIT,
	REQ_F_NEED_CLEANUP_BIT,
	REQ_F_POLLED_BIT,
	REQ_F_BUFFER_SELECTED_BIT,
	REQ_F_BUFFER_RING_BIT,
	REQ_F_REISSUE_BIT,
	REQ_F_CREDS_BIT,
	REQ_F_REFCOUNT_BIT,
	REQ_F_ARM_LTIMEOUT_BIT,
	REQ_F_ASYNC_DATA_BIT,
	REQ_F_SKIP_LINK_CQES_BIT,
	REQ_F_SINGLE_POLL_BIT,
	REQ_F_DOUBLE_POLL_BIT,
	REQ_F_PARTIAL_IO_BIT,
	REQ_F_CQE32_INIT_BIT,
	REQ_F_APOLL_MULTISHOT_BIT,
	REQ_F_CLEAR_POLLIN_BIT,
	REQ_F_HASH_LOCKED_BIT,
	/* keep async read/write and isreg together and in order */
	REQ_F_SUPPORT_NOWAIT_BIT,
	REQ_F_ISREG_BIT,

	/* not a real bit, just to check we're not overflowing the space */
	__REQ_F_LAST_BIT,
};

enum {
	/* ctx owns file */
	REQ_F_FIXED_FILE	= BIT(REQ_F_FIXED_FILE_BIT),
	/* drain existing IO first */
	REQ_F_IO_DRAIN		= BIT(REQ_F_IO_DRAIN_BIT),
	/* linked sqes */
	REQ_F_LINK		= BIT(REQ_F_LINK_BIT),
	/* doesn't sever on completion < 0 */
	REQ_F_HARDLINK		= BIT(REQ_F_HARDLINK_BIT),
	/* IOSQE_ASYNC */
	REQ_F_FORCE_ASYNC	= BIT(REQ_F_FORCE_ASYNC_BIT),
	/* IOSQE_BUFFER_SELECT */
	REQ_F_BUFFER_SELECT	= BIT(REQ_F_BUFFER_SELECT_BIT),
	/* IOSQE_CQE_SKIP_SUCCESS */
	REQ_F_CQE_SKIP		= BIT(REQ_F_CQE_SKIP_BIT),

	/* fail rest of links */
	REQ_F_FAIL		= BIT(REQ_F_FAIL_BIT),
	/* on inflight list, should be cancelled and waited on exit reliably */
	REQ_F_INFLIGHT		= BIT(REQ_F_INFLIGHT_BIT),
	/* read/write uses file position */
	REQ_F_CUR_POS		= BIT(REQ_F_CUR_POS_BIT),
	/* must not punt to workers */
	REQ_F_NOWAIT		= BIT(REQ_F_NOWAIT_BIT),
	/* has or had linked timeout */
	REQ_F_LINK_TIMEOUT	= BIT(REQ_F_LINK_TIMEOUT_BIT),
	/* needs cleanup */
	REQ_F_NEED_CLEANUP	= BIT(REQ_F_NEED_CLEANUP_BIT),
	/* already went through poll handler */
	REQ_F_POLLED		= BIT(REQ_F_POLLED_BIT),
	/* buffer already selected */
	REQ_F_BUFFER_SELECTED	= BIT(REQ_F_BUFFER_SELECTED_BIT),
	/* buffer selected from ring, needs commit */
	REQ_F_BUFFER_RING	= BIT(REQ_F_BUFFER_RING_BIT),
	/* caller should reissue async */
	REQ_F_REISSUE		= BIT(REQ_F_REISSUE_BIT),
	/* supports async reads/writes */
	REQ_F_SUPPORT_NOWAIT	= BIT(REQ_F_SUPPORT_NOWAIT_BIT),
	/* regular file */
	REQ_F_ISREG		= BIT(REQ_F_ISREG_BIT),
	/* has creds assigned */
	REQ_F_CREDS		= BIT(REQ_F_CREDS_BIT),
	/* skip refcounting if not set */
	REQ_F_REFCOUNT		= BIT(REQ_F_REFCOUNT_BIT),
	/* there is a linked timeout that has to be armed */
	REQ_F_ARM_LTIMEOUT	= BIT(REQ_F_ARM_LTIMEOUT_BIT),
	/* ->async_data allocated */
	REQ_F_ASYNC_DATA	= BIT(REQ_F_ASYNC_DATA_BIT),
	/* don't post CQEs while failing linked requests */
	REQ_F_SKIP_LINK_CQES	= BIT(REQ_F_SKIP_LINK_CQES_BIT),
	/* single poll may be active */
	REQ_F_SINGLE_POLL	= BIT(REQ_F_SINGLE_POLL_BIT),
	/* double poll may active */
	REQ_F_DOUBLE_POLL	= BIT(REQ_F_DOUBLE_POLL_BIT),
	/* request has already done partial IO */
	REQ_F_PARTIAL_IO	= BIT(REQ_F_PARTIAL_IO_BIT),
	/* fast poll multishot mode */
	REQ_F_APOLL_MULTISHOT	= BIT(REQ_F_APOLL_MULTISHOT_BIT),
	/* ->extra1 and ->extra2 are initialised */
	REQ_F_CQE32_INIT	= BIT(REQ_F_CQE32_INIT_BIT),
	/* recvmsg special flag, clear EPOLLIN */
	REQ_F_CLEAR_POLLIN	= BIT(REQ_F_CLEAR_POLLIN_BIT),
	/* hashed into ->cancel_hash_locked, protected by ->uring_lock */
	REQ_F_HASH_LOCKED	= BIT(REQ_F_HASH_LOCKED_BIT),
};

typedef void (*io_req_tw_func_t)(struct io_kiocb *req, bool *locked);

struct io_task_work {
	struct llist_node		node;
	io_req_tw_func_t		func;
};

struct io_cqe {
	__u64	user_data;
	__s32	res;
	/* fd initially, then cflags for completion */
	union {
		__u32	flags;
		int	fd;
	};
};

/*
 * Each request type overlays its private data structure on top of this one.
 * They must not exceed this one in size.
 */
struct io_cmd_data {
	struct file		*file;
	/* each command gets 56 bytes of data */
	__u8			data[56];
};

static inline void io_kiocb_cmd_sz_check(size_t cmd_sz)
{
	BUILD_BUG_ON(cmd_sz > sizeof(struct io_cmd_data));
}
#define io_kiocb_to_cmd(req, cmd_type) ( \
	io_kiocb_cmd_sz_check(sizeof(cmd_type)) , \
	((cmd_type *)&(req)->cmd) \
)
#define cmd_to_io_kiocb(ptr)	((struct io_kiocb *) ptr)

struct io_kiocb {
	union {
		/*
		 * NOTE! Each of the io_kiocb union members has the file pointer
		 * as the first entry in their struct definition. So you can
		 * access the file pointer through any of the sub-structs,
		 * or directly as just 'file' in this struct.
		 */
		struct file		*file;
		struct io_cmd_data	cmd;
	};

	u8				opcode;
	/* polled IO has completed */
	u8				iopoll_completed;
	/*
	 * Can be either a fixed buffer index, or used with provided buffers.
	 * For the latter, before issue it points to the buffer group ID,
	 * and after selection it points to the buffer ID itself.
	 */
	u16				buf_index;
	unsigned int			flags;

	struct io_cqe			cqe;

	struct io_ring_ctx		*ctx;
	struct task_struct		*task;

	struct io_rsrc_node		*rsrc_node;

	union {
		/* store used ubuf, so we can prevent reloading */
		struct io_mapped_ubuf	*imu;

		/* stores selected buf, valid IFF REQ_F_BUFFER_SELECTED is set */
		struct io_buffer	*kbuf;

		/*
		 * stores buffer ID for ring provided buffers, valid IFF
		 * REQ_F_BUFFER_RING is set.
		 */
		struct io_buffer_list	*buf_list;
	};

	union {
		/* used by request caches, completion batching and iopoll */
		struct io_wq_work_node	comp_list;
		/* cache ->apoll->events */
		__poll_t apoll_events;
	};
	atomic_t			refs;
	atomic_t			poll_refs;
	struct io_task_work		io_task_work;
	/* for polled requests, i.e. IORING_OP_POLL_ADD and async armed poll */
	union {
		struct hlist_node	hash_node;
		struct {
			u64		extra1;
			u64		extra2;
		};
	};
	/* internal polling, see IORING_FEAT_FAST_POLL */
	struct async_poll		*apoll;
	/* opcode allocated if it needs to store data for async defer */
	void				*async_data;
	/* linked requests, IFF REQ_F_HARDLINK or REQ_F_LINK are set */
	struct io_kiocb			*link;
	/* custom credentials, valid IFF REQ_F_CREDS is set */
	const struct cred		*creds;
	struct io_wq_work		work;
};

struct io_overflow_cqe {
	struct list_head list;
	struct io_uring_cqe cqe;
};

#endif
