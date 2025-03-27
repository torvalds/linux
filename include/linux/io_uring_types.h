#ifndef IO_URING_TYPES_H
#define IO_URING_TYPES_H

#include <linux/blkdev.h>
#include <linux/hashtable.h>
#include <linux/task_work.h>
#include <linux/bitmap.h>
#include <linux/llist.h>
#include <uapi/linux/io_uring.h>

enum {
	/*
	 * A hint to not wake right away but delay until there are enough of
	 * tw's queued to match the number of CQEs the task is waiting for.
	 *
	 * Must not be used with requests generating more than one CQE.
	 * It's also ignored unless IORING_SETUP_DEFER_TASKRUN is set.
	 */
	IOU_F_TWQ_LAZY_WAKE			= 1,
};

enum io_uring_cmd_flags {
	IO_URING_F_COMPLETE_DEFER	= 1,
	IO_URING_F_UNLOCKED		= 2,
	/* the request is executed from poll, it should not be freed */
	IO_URING_F_MULTISHOT		= 4,
	/* executed by io-wq */
	IO_URING_F_IOWQ			= 8,
	/* int's last bit, sign checks are usually faster than a bit test */
	IO_URING_F_NONBLOCK		= INT_MIN,

	/* ctx state flags, for URING_CMD */
	IO_URING_F_SQE128		= (1 << 8),
	IO_URING_F_CQE32		= (1 << 9),
	IO_URING_F_IOPOLL		= (1 << 10),

	/* set when uring wants to cancel a previously issued command */
	IO_URING_F_CANCEL		= (1 << 11),
	IO_URING_F_COMPAT		= (1 << 12),
	IO_URING_F_TASK_DEAD		= (1 << 13),
};

struct io_wq_work_node {
	struct io_wq_work_node *next;
};

struct io_wq_work_list {
	struct io_wq_work_node *first;
	struct io_wq_work_node *last;
};

struct io_wq_work {
	struct io_wq_work_node list;
	atomic_t flags;
	/* place it here instead of io_kiocb as it fills padding and saves 4B */
	int cancel_seq;
};

struct io_rsrc_data {
	unsigned int			nr;
	struct io_rsrc_node		**nodes;
};

struct io_file_table {
	struct io_rsrc_data data;
	unsigned long *bitmap;
	unsigned int alloc_hint;
};

struct io_hash_bucket {
	struct hlist_head	list;
} ____cacheline_aligned_in_smp;

struct io_hash_table {
	struct io_hash_bucket	*hbs;
	unsigned		hash_bits;
};

struct io_mapped_region {
	struct page		**pages;
	void			*ptr;
	unsigned		nr_pages;
	unsigned		flags;
};

/*
 * Arbitrary limit, can be raised if need be
 */
#define IO_RINGFD_REG_MAX 16

struct io_uring_task {
	/* submission side */
	int				cached_refs;
	const struct io_ring_ctx 	*last;
	struct task_struct		*task;
	struct io_wq			*io_wq;
	struct file			*registered_rings[IO_RINGFD_REG_MAX];

	struct xarray			xa;
	struct wait_queue_head		wait;
	atomic_t			in_cancel;
	atomic_t			inflight_tracked;
	struct percpu_counter		inflight;

	struct { /* task_work */
		struct llist_head	task_list;
		struct callback_head	task_work;
	} ____cacheline_aligned_in_smp;
};

struct io_uring {
	u32 head;
	u32 tail;
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
	bool			cq_flush;
	unsigned short		submit_nr;
	struct blk_plug		plug;
};

struct io_alloc_cache {
	void			**entries;
	unsigned int		nr_cached;
	unsigned int		max_cached;
	unsigned int		elem_size;
	unsigned int		init_clear;
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
		unsigned int		lockless_cq: 1;
		unsigned int		syscall_iopoll: 1;
		unsigned int		poll_activated: 1;
		unsigned int		drain_disabled: 1;
		unsigned int		compat: 1;
		unsigned int		iowq_limits_set : 1;

		struct task_struct	*submitter_task;
		struct io_rings		*rings;
		struct percpu_ref	refs;

		clockid_t		clockid;
		enum tk_offsets		clock_offset;

		enum task_work_notify_mode	notify_method;
		unsigned			sq_thread_idle;
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
		atomic_t		cancel_seq;

		/*
		 * ->iopoll_list is protected by the ctx->uring_lock for
		 * io_uring instances that don't use IORING_SETUP_SQPOLL.
		 * For SQPOLL, only the single threaded io_sq_thread() will
		 * manipulate the list, hence no extra locking is needed there.
		 */
		bool			poll_multi_queue;
		struct io_wq_work_list	iopoll_list;

		struct io_file_table	file_table;
		struct io_rsrc_data	buf_table;
		struct io_alloc_cache	node_cache;
		struct io_alloc_cache	imu_cache;

		struct io_submit_state	submit_state;

		/*
		 * Modifications are protected by ->uring_lock and ->mmap_lock.
		 * The flags, buf_pages and buf_nr_pages fields should be stable
		 * once published.
		 */
		struct xarray		io_bl_xa;

		struct io_hash_table	cancel_table;
		struct io_alloc_cache	apoll_cache;
		struct io_alloc_cache	netmsg_cache;
		struct io_alloc_cache	rw_cache;
		struct io_alloc_cache	uring_cache;

		/*
		 * Any cancelable uring_cmd is added to this list in
		 * ->uring_cmd() by io_uring_cmd_insert_cancelable()
		 */
		struct hlist_head	cancelable_uring_cmd;
		/*
		 * For Hybrid IOPOLL, runtime in hybrid polling, without
		 * scheduling time
		 */
		u64					hybrid_poll_time;
	} ____cacheline_aligned_in_smp;

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
		unsigned		cq_extra;

		void			*cq_wait_arg;
		size_t			cq_wait_size;
	} ____cacheline_aligned_in_smp;

	/*
	 * task_work and async notification delivery cacheline. Expected to
	 * regularly bounce b/w CPUs.
	 */
	struct {
		struct llist_head	work_llist;
		struct llist_head	retry_llist;
		unsigned long		check_cq;
		atomic_t		cq_wait_nr;
		atomic_t		cq_timeouts;
		struct wait_queue_head	cq_wait;
	} ____cacheline_aligned_in_smp;

	/* timeouts */
	struct {
		raw_spinlock_t		timeout_lock;
		struct list_head	timeout_list;
		struct list_head	ltimeout_list;
		unsigned		cq_last_tm_flush;
	} ____cacheline_aligned_in_smp;

	spinlock_t		completion_lock;

	struct list_head	cq_overflow_list;

	struct hlist_head	waitid_list;

#ifdef CONFIG_FUTEX
	struct hlist_head	futex_list;
	struct io_alloc_cache	futex_cache;
#endif

	const struct cred	*sq_creds;	/* cred used for __io_sq_thread() */
	struct io_sq_data	*sq_data;	/* if using sq thread polling */

	struct wait_queue_head	sqo_sq_wait;
	struct list_head	sqd_list;

	unsigned int		file_alloc_start;
	unsigned int		file_alloc_end;

	/* Keep this last, we don't need it for the fast path */
	struct wait_queue_head		poll_wq;
	struct io_restriction		restrictions;

	u32			pers_next;
	struct xarray		personalities;

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

	struct callback_head		poll_wq_task_work;
	struct list_head		defer_list;

	struct io_alloc_cache		msg_cache;
	spinlock_t			msg_lock;

#ifdef CONFIG_NET_RX_BUSY_POLL
	struct list_head	napi_list;	/* track busy poll napi_id */
	spinlock_t		napi_lock;	/* napi_list lock */

	/* napi busy poll default timeout */
	ktime_t			napi_busy_poll_dt;
	bool			napi_prefer_busy_poll;
	u8			napi_track_mode;

	DECLARE_HASHTABLE(napi_ht, 4);
#endif

	/* protected by ->completion_lock */
	unsigned			evfd_last_cq_tail;

	/*
	 * Protection for resize vs mmap races - both the mmap and resize
	 * side will need to grab this lock, to prevent either side from
	 * being run concurrently with the other.
	 */
	struct mutex			mmap_lock;

	struct io_mapped_region		sq_region;
	struct io_mapped_region		ring_region;
	/* used for optimised request parameter and wait argument passing  */
	struct io_mapped_region		param_region;
};

/*
 * Token indicating function is called in task work context:
 * ctx->uring_lock is held and any completions generated will be flushed.
 * ONLY core io_uring.c should instantiate this struct.
 */
struct io_tw_state {
};
/* Alias to use in code that doesn't instantiate struct io_tw_state */
typedef struct io_tw_state io_tw_token_t;

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
	REQ_F_HYBRID_IOPOLL_STATE_BIT,
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
	REQ_F_APOLL_MULTISHOT_BIT,
	REQ_F_CLEAR_POLLIN_BIT,
	/* keep async read/write and isreg together and in order */
	REQ_F_SUPPORT_NOWAIT_BIT,
	REQ_F_ISREG_BIT,
	REQ_F_POLL_NO_LAZY_BIT,
	REQ_F_CAN_POLL_BIT,
	REQ_F_BL_EMPTY_BIT,
	REQ_F_BL_NO_RECYCLE_BIT,
	REQ_F_BUFFERS_COMMIT_BIT,
	REQ_F_BUF_NODE_BIT,
	REQ_F_HAS_METADATA_BIT,

	/* not a real bit, just to check we're not overflowing the space */
	__REQ_F_LAST_BIT,
};

typedef u64 __bitwise io_req_flags_t;
#define IO_REQ_FLAG(bitno)	((__force io_req_flags_t) BIT_ULL((bitno)))

enum {
	/* ctx owns file */
	REQ_F_FIXED_FILE	= IO_REQ_FLAG(REQ_F_FIXED_FILE_BIT),
	/* drain existing IO first */
	REQ_F_IO_DRAIN		= IO_REQ_FLAG(REQ_F_IO_DRAIN_BIT),
	/* linked sqes */
	REQ_F_LINK		= IO_REQ_FLAG(REQ_F_LINK_BIT),
	/* doesn't sever on completion < 0 */
	REQ_F_HARDLINK		= IO_REQ_FLAG(REQ_F_HARDLINK_BIT),
	/* IOSQE_ASYNC */
	REQ_F_FORCE_ASYNC	= IO_REQ_FLAG(REQ_F_FORCE_ASYNC_BIT),
	/* IOSQE_BUFFER_SELECT */
	REQ_F_BUFFER_SELECT	= IO_REQ_FLAG(REQ_F_BUFFER_SELECT_BIT),
	/* IOSQE_CQE_SKIP_SUCCESS */
	REQ_F_CQE_SKIP		= IO_REQ_FLAG(REQ_F_CQE_SKIP_BIT),

	/* fail rest of links */
	REQ_F_FAIL		= IO_REQ_FLAG(REQ_F_FAIL_BIT),
	/* on inflight list, should be cancelled and waited on exit reliably */
	REQ_F_INFLIGHT		= IO_REQ_FLAG(REQ_F_INFLIGHT_BIT),
	/* read/write uses file position */
	REQ_F_CUR_POS		= IO_REQ_FLAG(REQ_F_CUR_POS_BIT),
	/* must not punt to workers */
	REQ_F_NOWAIT		= IO_REQ_FLAG(REQ_F_NOWAIT_BIT),
	/* has or had linked timeout */
	REQ_F_LINK_TIMEOUT	= IO_REQ_FLAG(REQ_F_LINK_TIMEOUT_BIT),
	/* needs cleanup */
	REQ_F_NEED_CLEANUP	= IO_REQ_FLAG(REQ_F_NEED_CLEANUP_BIT),
	/* already went through poll handler */
	REQ_F_POLLED		= IO_REQ_FLAG(REQ_F_POLLED_BIT),
	/* every req only blocks once in hybrid poll */
	REQ_F_IOPOLL_STATE        = IO_REQ_FLAG(REQ_F_HYBRID_IOPOLL_STATE_BIT),
	/* buffer already selected */
	REQ_F_BUFFER_SELECTED	= IO_REQ_FLAG(REQ_F_BUFFER_SELECTED_BIT),
	/* buffer selected from ring, needs commit */
	REQ_F_BUFFER_RING	= IO_REQ_FLAG(REQ_F_BUFFER_RING_BIT),
	/* caller should reissue async */
	REQ_F_REISSUE		= IO_REQ_FLAG(REQ_F_REISSUE_BIT),
	/* supports async reads/writes */
	REQ_F_SUPPORT_NOWAIT	= IO_REQ_FLAG(REQ_F_SUPPORT_NOWAIT_BIT),
	/* regular file */
	REQ_F_ISREG		= IO_REQ_FLAG(REQ_F_ISREG_BIT),
	/* has creds assigned */
	REQ_F_CREDS		= IO_REQ_FLAG(REQ_F_CREDS_BIT),
	/* skip refcounting if not set */
	REQ_F_REFCOUNT		= IO_REQ_FLAG(REQ_F_REFCOUNT_BIT),
	/* there is a linked timeout that has to be armed */
	REQ_F_ARM_LTIMEOUT	= IO_REQ_FLAG(REQ_F_ARM_LTIMEOUT_BIT),
	/* ->async_data allocated */
	REQ_F_ASYNC_DATA	= IO_REQ_FLAG(REQ_F_ASYNC_DATA_BIT),
	/* don't post CQEs while failing linked requests */
	REQ_F_SKIP_LINK_CQES	= IO_REQ_FLAG(REQ_F_SKIP_LINK_CQES_BIT),
	/* single poll may be active */
	REQ_F_SINGLE_POLL	= IO_REQ_FLAG(REQ_F_SINGLE_POLL_BIT),
	/* double poll may active */
	REQ_F_DOUBLE_POLL	= IO_REQ_FLAG(REQ_F_DOUBLE_POLL_BIT),
	/* fast poll multishot mode */
	REQ_F_APOLL_MULTISHOT	= IO_REQ_FLAG(REQ_F_APOLL_MULTISHOT_BIT),
	/* recvmsg special flag, clear EPOLLIN */
	REQ_F_CLEAR_POLLIN	= IO_REQ_FLAG(REQ_F_CLEAR_POLLIN_BIT),
	/* don't use lazy poll wake for this request */
	REQ_F_POLL_NO_LAZY	= IO_REQ_FLAG(REQ_F_POLL_NO_LAZY_BIT),
	/* file is pollable */
	REQ_F_CAN_POLL		= IO_REQ_FLAG(REQ_F_CAN_POLL_BIT),
	/* buffer list was empty after selection of buffer */
	REQ_F_BL_EMPTY		= IO_REQ_FLAG(REQ_F_BL_EMPTY_BIT),
	/* don't recycle provided buffers for this request */
	REQ_F_BL_NO_RECYCLE	= IO_REQ_FLAG(REQ_F_BL_NO_RECYCLE_BIT),
	/* buffer ring head needs incrementing on put */
	REQ_F_BUFFERS_COMMIT	= IO_REQ_FLAG(REQ_F_BUFFERS_COMMIT_BIT),
	/* buf node is valid */
	REQ_F_BUF_NODE		= IO_REQ_FLAG(REQ_F_BUF_NODE_BIT),
	/* request has read/write metadata assigned */
	REQ_F_HAS_METADATA	= IO_REQ_FLAG(REQ_F_HAS_METADATA_BIT),
};

typedef void (*io_req_tw_func_t)(struct io_kiocb *req, io_tw_token_t tw);

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

static inline struct io_kiocb *cmd_to_io_kiocb(void *ptr)
{
	return ptr;
}

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

	unsigned			nr_tw;

	/* REQ_F_* flags */
	io_req_flags_t			flags;

	struct io_cqe			cqe;

	struct io_ring_ctx		*ctx;
	struct io_uring_task		*tctx;

	union {
		/* stores selected buf, valid IFF REQ_F_BUFFER_SELECTED is set */
		struct io_buffer	*kbuf;

		/*
		 * stores buffer ID for ring provided buffers, valid IFF
		 * REQ_F_BUFFER_RING is set.
		 */
		struct io_buffer_list	*buf_list;

		struct io_rsrc_node	*buf_node;
	};

	union {
		/* used by request caches, completion batching and iopoll */
		struct io_wq_work_node	comp_list;
		/* cache ->apoll->events */
		__poll_t apoll_events;
	};

	struct io_rsrc_node		*file_node;

	atomic_t			refs;
	bool				cancel_seq_set;
	struct io_task_work		io_task_work;
	union {
		/*
		 * for polled requests, i.e. IORING_OP_POLL_ADD and async armed
		 * poll
		 */
		struct hlist_node	hash_node;
		/* For IOPOLL setup queues, with hybrid polling */
		u64                     iopoll_start;
	};
	/* internal polling, see IORING_FEAT_FAST_POLL */
	struct async_poll		*apoll;
	/* opcode allocated if it needs to store data for async defer */
	void				*async_data;
	/* linked requests, IFF REQ_F_HARDLINK or REQ_F_LINK are set */
	atomic_t			poll_refs;
	struct io_kiocb			*link;
	/* custom credentials, valid IFF REQ_F_CREDS is set */
	const struct cred		*creds;
	struct io_wq_work		work;

	struct {
		u64			extra1;
		u64			extra2;
	} big_cqe;
};

struct io_overflow_cqe {
	struct list_head list;
	struct io_uring_cqe cqe;
};

static inline bool io_ctx_cqe32(struct io_ring_ctx *ctx)
{
	return ctx->flags & IORING_SETUP_CQE32;
}

#endif
