// SPDX-License-Identifier: GPL-2.0
/*
 * Shared application/kernel submission and completion ring pairs, for
 * supporting fast/efficient IO.
 *
 * A note on the read/write ordering memory barriers that are matched between
 * the application and kernel side.
 *
 * After the application reads the CQ ring tail, it must use an
 * appropriate smp_rmb() to pair with the smp_wmb() the kernel uses
 * before writing the tail (using smp_load_acquire to read the tail will
 * do). It also needs a smp_mb() before updating CQ head (ordering the
 * entry load(s) with the head store), pairing with an implicit barrier
 * through a control-dependency in io_get_cqring (smp_store_release to
 * store head will do). Failure to do so could lead to reading invalid
 * CQ entries.
 *
 * Likewise, the application must use an appropriate smp_wmb() before
 * writing the SQ tail (ordering SQ entry stores with the tail store),
 * which pairs with smp_load_acquire in io_get_sqring (smp_store_release
 * to store the tail will do). And it needs a barrier ordering the SQ
 * head load before writing new SQ entries (smp_load_acquire to read
 * head will do).
 *
 * When using the SQ poll thread (IORING_SETUP_SQPOLL), the application
 * needs to check the SQ flags for IORING_SQ_NEED_WAKEUP *after*
 * updating the SQ tail; a full memory barrier smp_mb() is needed
 * between.
 *
 * Also see the examples in the liburing library:
 *
 *	git://git.kernel.dk/liburing
 *
 * io_uring also uses READ/WRITE_ONCE() for _any_ store or load that happens
 * from data shared between the kernel and application. This is done both
 * for ordering purposes, but also to ensure that once a value is loaded from
 * data that the application could potentially modify, it remains stable.
 *
 * Copyright (C) 2018-2019 Jens Axboe
 * Copyright (c) 2018-2019 Christoph Hellwig
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <net/compat.h>
#include <linux/refcount.h>
#include <linux/uio.h>
#include <linux/bits.h>

#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/blkdev.h>
#include <linux/bvec.h>
#include <linux/net.h>
#include <net/sock.h>
#include <net/af_unix.h>
#include <net/scm.h>
#include <linux/anon_inodes.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>
#include <linux/sizes.h>
#include <linux/hugetlb.h>
#include <linux/highmem.h>
#include <linux/namei.h>
#include <linux/fsnotify.h>
#include <linux/fadvise.h>
#include <linux/eventpoll.h>
#include <linux/fs_struct.h>
#include <linux/splice.h>
#include <linux/task_work.h>
#include <linux/pagemap.h>
#include <linux/io_uring.h>
#include <linux/blk-cgroup.h>
#include <linux/audit.h>

#define CREATE_TRACE_POINTS
#include <trace/events/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "internal.h"
#include "io-wq.h"

#define IORING_MAX_ENTRIES	32768
#define IORING_MAX_CQ_ENTRIES	(2 * IORING_MAX_ENTRIES)

/*
 * Shift of 9 is 512 entries, or exactly one page on 64-bit archs
 */
#define IORING_FILE_TABLE_SHIFT	9
#define IORING_MAX_FILES_TABLE	(1U << IORING_FILE_TABLE_SHIFT)
#define IORING_FILE_TABLE_MASK	(IORING_MAX_FILES_TABLE - 1)
#define IORING_MAX_FIXED_FILES	(64 * IORING_MAX_FILES_TABLE)
#define IORING_MAX_RESTRICTIONS	(IORING_RESTRICTION_LAST + \
				 IORING_REGISTER_LAST + IORING_OP_LAST)

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
	u32			sq_flags;
	/*
	 * Runtime CQ flags
	 *
	 * Written by the application, shouldn't be modified by the
	 * kernel.
	 */
	u32                     cq_flags;
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

struct io_mapped_ubuf {
	u64		ubuf;
	size_t		len;
	struct		bio_vec *bvec;
	unsigned int	nr_bvecs;
	unsigned long	acct_pages;
};

struct fixed_file_table {
	struct file		**files;
};

struct fixed_file_ref_node {
	struct percpu_ref		refs;
	struct list_head		node;
	struct list_head		file_list;
	struct fixed_file_data		*file_data;
	struct llist_node		llist;
	bool				done;
};

struct fixed_file_data {
	struct fixed_file_table		*table;
	struct io_ring_ctx		*ctx;

	struct fixed_file_ref_node	*node;
	struct percpu_ref		refs;
	struct completion		done;
	struct list_head		ref_list;
	spinlock_t			lock;
};

struct io_buffer {
	struct list_head list;
	__u64 addr;
	__s32 len;
	__u16 bid;
};

struct io_restriction {
	DECLARE_BITMAP(register_op, IORING_REGISTER_LAST);
	DECLARE_BITMAP(sqe_op, IORING_OP_LAST);
	u8 sqe_flags_allowed;
	u8 sqe_flags_required;
	bool registered;
};

struct io_sq_data {
	refcount_t		refs;
	struct mutex		lock;

	/* ctx's that are using this sqd */
	struct list_head	ctx_list;
	struct list_head	ctx_new_list;
	struct mutex		ctx_lock;

	struct task_struct	*thread;
	struct wait_queue_head	wait;
};

struct io_ring_ctx {
	struct {
		struct percpu_ref	refs;
	} ____cacheline_aligned_in_smp;

	struct {
		unsigned int		flags;
		unsigned int		compat: 1;
		unsigned int		limit_mem: 1;
		unsigned int		cq_overflow_flushed: 1;
		unsigned int		drain_next: 1;
		unsigned int		eventfd_async: 1;
		unsigned int		restricted: 1;

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
		unsigned		cached_sq_head;
		unsigned		sq_entries;
		unsigned		sq_mask;
		unsigned		sq_thread_idle;
		unsigned		cached_sq_dropped;
		unsigned		cached_cq_overflow;
		unsigned long		sq_check_overflow;

		struct list_head	defer_list;
		struct list_head	timeout_list;
		struct list_head	cq_overflow_list;

		wait_queue_head_t	inflight_wait;
		struct io_uring_sqe	*sq_sqes;
	} ____cacheline_aligned_in_smp;

	struct io_rings	*rings;

	/* IO offload */
	struct io_wq		*io_wq;

	/*
	 * For SQPOLL usage - we hold a reference to the parent task, so we
	 * have access to the ->files
	 */
	struct task_struct	*sqo_task;

	/* Only used for accounting purposes */
	struct mm_struct	*mm_account;

#ifdef CONFIG_BLK_CGROUP
	struct cgroup_subsys_state	*sqo_blkcg_css;
#endif

	struct io_sq_data	*sq_data;	/* if using sq thread polling */

	struct wait_queue_head	sqo_sq_wait;
	struct wait_queue_entry	sqo_wait_entry;
	struct list_head	sqd_list;

	/*
	 * If used, fixed file set. Writers must ensure that ->refs is dead,
	 * readers must ensure that ->refs is alive as long as the file* is
	 * used. Only updated through io_uring_register(2).
	 */
	struct fixed_file_data	*file_data;
	unsigned		nr_user_files;

	/* if used, fixed mapped user buffers */
	unsigned		nr_user_bufs;
	struct io_mapped_ubuf	*user_bufs;

	struct user_struct	*user;

	const struct cred	*creds;

#ifdef CONFIG_AUDIT
	kuid_t			loginuid;
	unsigned int		sessionid;
#endif

	struct completion	ref_comp;
	struct completion	sq_thread_comp;

	/* if all else fails... */
	struct io_kiocb		*fallback_req;

#if defined(CONFIG_UNIX)
	struct socket		*ring_sock;
#endif

	struct idr		io_buffer_idr;

	struct idr		personality_idr;

	struct {
		unsigned		cached_cq_tail;
		unsigned		cq_entries;
		unsigned		cq_mask;
		atomic_t		cq_timeouts;
		unsigned long		cq_check_overflow;
		struct wait_queue_head	cq_wait;
		struct fasync_struct	*cq_fasync;
		struct eventfd_ctx	*cq_ev_fd;
	} ____cacheline_aligned_in_smp;

	struct {
		struct mutex		uring_lock;
		wait_queue_head_t	wait;
	} ____cacheline_aligned_in_smp;

	struct {
		spinlock_t		completion_lock;

		/*
		 * ->iopoll_list is protected by the ctx->uring_lock for
		 * io_uring instances that don't use IORING_SETUP_SQPOLL.
		 * For SQPOLL, only the single threaded io_sq_thread() will
		 * manipulate the list, hence no extra locking is needed there.
		 */
		struct list_head	iopoll_list;
		struct hlist_head	*cancel_hash;
		unsigned		cancel_hash_bits;
		bool			poll_multi_file;

		spinlock_t		inflight_lock;
		struct list_head	inflight_list;
	} ____cacheline_aligned_in_smp;

	struct delayed_work		file_put_work;
	struct llist_head		file_put_llist;

	struct work_struct		exit_work;
	struct io_restriction		restrictions;
};

/*
 * First field must be the file pointer in all the
 * iocb unions! See also 'struct kiocb' in <linux/fs.h>
 */
struct io_poll_iocb {
	struct file			*file;
	union {
		struct wait_queue_head	*head;
		u64			addr;
	};
	__poll_t			events;
	bool				done;
	bool				canceled;
	struct wait_queue_entry		wait;
};

struct io_close {
	struct file			*file;
	struct file			*put_file;
	int				fd;
};

struct io_timeout_data {
	struct io_kiocb			*req;
	struct hrtimer			timer;
	struct timespec64		ts;
	enum hrtimer_mode		mode;
};

struct io_accept {
	struct file			*file;
	struct sockaddr __user		*addr;
	int __user			*addr_len;
	int				flags;
	unsigned long			nofile;
};

struct io_sync {
	struct file			*file;
	loff_t				len;
	loff_t				off;
	int				flags;
	int				mode;
};

struct io_cancel {
	struct file			*file;
	u64				addr;
};

struct io_timeout {
	struct file			*file;
	u32				off;
	u32				target_seq;
	struct list_head		list;
};

struct io_timeout_rem {
	struct file			*file;
	u64				addr;
};

struct io_rw {
	/* NOTE: kiocb has the file as the first member, so don't do it here */
	struct kiocb			kiocb;
	u64				addr;
	u64				len;
};

struct io_connect {
	struct file			*file;
	struct sockaddr __user		*addr;
	int				addr_len;
};

struct io_sr_msg {
	struct file			*file;
	union {
		struct user_msghdr __user *umsg;
		void __user		*buf;
	};
	int				msg_flags;
	int				bgid;
	size_t				len;
	struct io_buffer		*kbuf;
};

struct io_open {
	struct file			*file;
	int				dfd;
	bool				ignore_nonblock;
	struct filename			*filename;
	struct open_how			how;
	unsigned long			nofile;
};

struct io_files_update {
	struct file			*file;
	u64				arg;
	u32				nr_args;
	u32				offset;
};

struct io_fadvise {
	struct file			*file;
	u64				offset;
	u32				len;
	u32				advice;
};

struct io_madvise {
	struct file			*file;
	u64				addr;
	u32				len;
	u32				advice;
};

struct io_epoll {
	struct file			*file;
	int				epfd;
	int				op;
	int				fd;
	struct epoll_event		event;
};

struct io_splice {
	struct file			*file_out;
	struct file			*file_in;
	loff_t				off_out;
	loff_t				off_in;
	u64				len;
	unsigned int			flags;
};

struct io_provide_buf {
	struct file			*file;
	__u64				addr;
	__s32				len;
	__u32				bgid;
	__u16				nbufs;
	__u16				bid;
};

struct io_statx {
	struct file			*file;
	int				dfd;
	unsigned int			mask;
	unsigned int			flags;
	const char __user		*filename;
	struct statx __user		*buffer;
};

struct io_completion {
	struct file			*file;
	struct list_head		list;
	int				cflags;
};

struct io_async_connect {
	struct sockaddr_storage		address;
};

struct io_async_msghdr {
	struct iovec			fast_iov[UIO_FASTIOV];
	struct iovec			*iov;
	struct sockaddr __user		*uaddr;
	struct msghdr			msg;
	struct sockaddr_storage		addr;
};

struct io_async_rw {
	struct iovec			fast_iov[UIO_FASTIOV];
	const struct iovec		*free_iovec;
	struct iov_iter			iter;
	size_t				bytes_done;
	struct wait_page_queue		wpq;
};

enum {
	REQ_F_FIXED_FILE_BIT	= IOSQE_FIXED_FILE_BIT,
	REQ_F_IO_DRAIN_BIT	= IOSQE_IO_DRAIN_BIT,
	REQ_F_LINK_BIT		= IOSQE_IO_LINK_BIT,
	REQ_F_HARDLINK_BIT	= IOSQE_IO_HARDLINK_BIT,
	REQ_F_FORCE_ASYNC_BIT	= IOSQE_ASYNC_BIT,
	REQ_F_BUFFER_SELECT_BIT	= IOSQE_BUFFER_SELECT_BIT,

	REQ_F_LINK_HEAD_BIT,
	REQ_F_FAIL_LINK_BIT,
	REQ_F_INFLIGHT_BIT,
	REQ_F_CUR_POS_BIT,
	REQ_F_NOWAIT_BIT,
	REQ_F_LINK_TIMEOUT_BIT,
	REQ_F_ISREG_BIT,
	REQ_F_NEED_CLEANUP_BIT,
	REQ_F_POLLED_BIT,
	REQ_F_BUFFER_SELECTED_BIT,
	REQ_F_NO_FILE_TABLE_BIT,
	REQ_F_WORK_INITIALIZED_BIT,
	REQ_F_LTIMEOUT_ACTIVE_BIT,

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

	/* head of a link */
	REQ_F_LINK_HEAD		= BIT(REQ_F_LINK_HEAD_BIT),
	/* fail rest of links */
	REQ_F_FAIL_LINK		= BIT(REQ_F_FAIL_LINK_BIT),
	/* on inflight list */
	REQ_F_INFLIGHT		= BIT(REQ_F_INFLIGHT_BIT),
	/* read/write uses file position */
	REQ_F_CUR_POS		= BIT(REQ_F_CUR_POS_BIT),
	/* must not punt to workers */
	REQ_F_NOWAIT		= BIT(REQ_F_NOWAIT_BIT),
	/* has or had linked timeout */
	REQ_F_LINK_TIMEOUT	= BIT(REQ_F_LINK_TIMEOUT_BIT),
	/* regular file */
	REQ_F_ISREG		= BIT(REQ_F_ISREG_BIT),
	/* needs cleanup */
	REQ_F_NEED_CLEANUP	= BIT(REQ_F_NEED_CLEANUP_BIT),
	/* already went through poll handler */
	REQ_F_POLLED		= BIT(REQ_F_POLLED_BIT),
	/* buffer already selected */
	REQ_F_BUFFER_SELECTED	= BIT(REQ_F_BUFFER_SELECTED_BIT),
	/* doesn't need file table for this request */
	REQ_F_NO_FILE_TABLE	= BIT(REQ_F_NO_FILE_TABLE_BIT),
	/* io_wq_work is initialized */
	REQ_F_WORK_INITIALIZED	= BIT(REQ_F_WORK_INITIALIZED_BIT),
	/* linked timeout is active, i.e. prepared by link's head */
	REQ_F_LTIMEOUT_ACTIVE	= BIT(REQ_F_LTIMEOUT_ACTIVE_BIT),
};

struct async_poll {
	struct io_poll_iocb	poll;
	struct io_poll_iocb	*double_poll;
};

/*
 * NOTE! Each of the iocb union members has the file pointer
 * as the first entry in their struct definition. So you can
 * access the file pointer through any of the sub-structs,
 * or directly as just 'ki_filp' in this struct.
 */
struct io_kiocb {
	union {
		struct file		*file;
		struct io_rw		rw;
		struct io_poll_iocb	poll;
		struct io_accept	accept;
		struct io_sync		sync;
		struct io_cancel	cancel;
		struct io_timeout	timeout;
		struct io_timeout_rem	timeout_rem;
		struct io_connect	connect;
		struct io_sr_msg	sr_msg;
		struct io_open		open;
		struct io_close		close;
		struct io_files_update	files_update;
		struct io_fadvise	fadvise;
		struct io_madvise	madvise;
		struct io_epoll		epoll;
		struct io_splice	splice;
		struct io_provide_buf	pbuf;
		struct io_statx		statx;
		/* use only after cleaning per-op data, see io_clean_op() */
		struct io_completion	compl;
	};

	/* opcode allocated if it needs to store data for async defer */
	void				*async_data;
	u8				opcode;
	/* polled IO has completed */
	u8				iopoll_completed;

	u16				buf_index;
	u32				result;

	struct io_ring_ctx		*ctx;
	unsigned int			flags;
	refcount_t			refs;
	struct task_struct		*task;
	u64				user_data;

	struct list_head		link_list;

	/*
	 * 1. used with ctx->iopoll_list with reads/writes
	 * 2. to track reqs with ->files (see io_op_def::file_table)
	 */
	struct list_head		inflight_entry;

	struct percpu_ref		*fixed_file_refs;
	struct callback_head		task_work;
	/* for polled requests, i.e. IORING_OP_POLL_ADD and async armed poll */
	struct hlist_node		hash_node;
	struct async_poll		*apoll;
	struct io_wq_work		work;
};

struct io_defer_entry {
	struct list_head	list;
	struct io_kiocb		*req;
	u32			seq;
};

#define IO_IOPOLL_BATCH			8

struct io_comp_state {
	unsigned int		nr;
	struct list_head	list;
	struct io_ring_ctx	*ctx;
};

struct io_submit_state {
	struct blk_plug		plug;

	/*
	 * io_kiocb alloc cache
	 */
	void			*reqs[IO_IOPOLL_BATCH];
	unsigned int		free_reqs;

	/*
	 * Batch completion logic
	 */
	struct io_comp_state	comp;

	/*
	 * File reference cache
	 */
	struct file		*file;
	unsigned int		fd;
	unsigned int		has_refs;
	unsigned int		ios_left;
};

struct io_op_def {
	/* needs req->file assigned */
	unsigned		needs_file : 1;
	/* don't fail if file grab fails */
	unsigned		needs_file_no_error : 1;
	/* hash wq insertion if file is a regular file */
	unsigned		hash_reg_file : 1;
	/* unbound wq insertion if file is a non-regular file */
	unsigned		unbound_nonreg_file : 1;
	/* opcode is not supported by this kernel */
	unsigned		not_supported : 1;
	/* set if opcode supports polled "wait" */
	unsigned		pollin : 1;
	unsigned		pollout : 1;
	/* op supports buffer selection */
	unsigned		buffer_select : 1;
	/* must always have async data allocated */
	unsigned		needs_async_data : 1;
	/* size of async data needed, if any */
	unsigned short		async_size;
	unsigned		work_flags;
};

static const struct io_op_def io_op_defs[] = {
	[IORING_OP_NOP] = {},
	[IORING_OP_READV] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_async_rw),
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_WRITEV] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_async_rw),
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG |
						IO_WQ_WORK_FSIZE,
	},
	[IORING_OP_FSYNC] = {
		.needs_file		= 1,
		.work_flags		= IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_READ_FIXED] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.async_size		= sizeof(struct io_async_rw),
		.work_flags		= IO_WQ_WORK_BLKCG | IO_WQ_WORK_MM,
	},
	[IORING_OP_WRITE_FIXED] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.async_size		= sizeof(struct io_async_rw),
		.work_flags		= IO_WQ_WORK_BLKCG | IO_WQ_WORK_FSIZE |
						IO_WQ_WORK_MM,
	},
	[IORING_OP_POLL_ADD] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_POLL_REMOVE] = {},
	[IORING_OP_SYNC_FILE_RANGE] = {
		.needs_file		= 1,
		.work_flags		= IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_SENDMSG] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_async_msghdr),
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG |
						IO_WQ_WORK_FS,
	},
	[IORING_OP_RECVMSG] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_async_msghdr),
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG |
						IO_WQ_WORK_FS,
	},
	[IORING_OP_TIMEOUT] = {
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_timeout_data),
		.work_flags		= IO_WQ_WORK_MM,
	},
	[IORING_OP_TIMEOUT_REMOVE] = {},
	[IORING_OP_ACCEPT] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_FILES,
	},
	[IORING_OP_ASYNC_CANCEL] = {},
	[IORING_OP_LINK_TIMEOUT] = {
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_timeout_data),
		.work_flags		= IO_WQ_WORK_MM,
	},
	[IORING_OP_CONNECT] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.needs_async_data	= 1,
		.async_size		= sizeof(struct io_async_connect),
		.work_flags		= IO_WQ_WORK_MM,
	},
	[IORING_OP_FALLOCATE] = {
		.needs_file		= 1,
		.work_flags		= IO_WQ_WORK_BLKCG | IO_WQ_WORK_FSIZE,
	},
	[IORING_OP_OPENAT] = {
		.work_flags		= IO_WQ_WORK_FILES | IO_WQ_WORK_BLKCG |
						IO_WQ_WORK_FS,
	},
	[IORING_OP_CLOSE] = {
		.needs_file		= 1,
		.needs_file_no_error	= 1,
		.work_flags		= IO_WQ_WORK_FILES | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_FILES_UPDATE] = {
		.work_flags		= IO_WQ_WORK_FILES | IO_WQ_WORK_MM,
	},
	[IORING_OP_STATX] = {
		.work_flags		= IO_WQ_WORK_FILES | IO_WQ_WORK_MM |
						IO_WQ_WORK_FS | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_READ] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.async_size		= sizeof(struct io_async_rw),
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_WRITE] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.async_size		= sizeof(struct io_async_rw),
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG |
						IO_WQ_WORK_FSIZE,
	},
	[IORING_OP_FADVISE] = {
		.needs_file		= 1,
		.work_flags		= IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_MADVISE] = {
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_SEND] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollout		= 1,
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_RECV] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.pollin			= 1,
		.buffer_select		= 1,
		.work_flags		= IO_WQ_WORK_MM | IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_OPENAT2] = {
		.work_flags		= IO_WQ_WORK_FILES | IO_WQ_WORK_FS |
						IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_EPOLL_CTL] = {
		.unbound_nonreg_file	= 1,
		.work_flags		= IO_WQ_WORK_FILES,
	},
	[IORING_OP_SPLICE] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
		.work_flags		= IO_WQ_WORK_BLKCG,
	},
	[IORING_OP_PROVIDE_BUFFERS] = {},
	[IORING_OP_REMOVE_BUFFERS] = {},
	[IORING_OP_TEE] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
	},
};

enum io_mem_account {
	ACCT_LOCKED,
	ACCT_PINNED,
};

static void __io_complete_rw(struct io_kiocb *req, long res, long res2,
			     struct io_comp_state *cs);
static void io_cqring_fill_event(struct io_kiocb *req, long res);
static void io_put_req(struct io_kiocb *req);
static void io_put_req_deferred(struct io_kiocb *req, int nr);
static void io_double_put_req(struct io_kiocb *req);
static struct io_kiocb *io_prep_linked_timeout(struct io_kiocb *req);
static void __io_queue_linked_timeout(struct io_kiocb *req);
static void io_queue_linked_timeout(struct io_kiocb *req);
static int __io_sqe_files_update(struct io_ring_ctx *ctx,
				 struct io_uring_files_update *ip,
				 unsigned nr_args);
static void __io_clean_op(struct io_kiocb *req);
static struct file *io_file_get(struct io_submit_state *state,
				struct io_kiocb *req, int fd, bool fixed);
static void __io_queue_sqe(struct io_kiocb *req, struct io_comp_state *cs);
static void io_file_put_work(struct work_struct *work);

static ssize_t io_import_iovec(int rw, struct io_kiocb *req,
			       struct iovec **iovec, struct iov_iter *iter,
			       bool needs_lock);
static int io_setup_async_rw(struct io_kiocb *req, const struct iovec *iovec,
			     const struct iovec *fast_iov,
			     struct iov_iter *iter, bool force);

static struct kmem_cache *req_cachep;

static const struct file_operations io_uring_fops;

struct sock *io_uring_get_socket(struct file *file)
{
#if defined(CONFIG_UNIX)
	if (file->f_op == &io_uring_fops) {
		struct io_ring_ctx *ctx = file->private_data;

		return ctx->ring_sock->sk;
	}
#endif
	return NULL;
}
EXPORT_SYMBOL(io_uring_get_socket);

static inline void io_clean_op(struct io_kiocb *req)
{
	if (req->flags & (REQ_F_NEED_CLEANUP | REQ_F_BUFFER_SELECTED |
			  REQ_F_INFLIGHT))
		__io_clean_op(req);
}

static void io_sq_thread_drop_mm(void)
{
	struct mm_struct *mm = current->mm;

	if (mm) {
		kthread_unuse_mm(mm);
		mmput(mm);
		current->mm = NULL;
	}
}

static int __io_sq_thread_acquire_mm(struct io_ring_ctx *ctx)
{
	struct mm_struct *mm;

	if (current->mm)
		return 0;

	/* Should never happen */
	if (unlikely(!(ctx->flags & IORING_SETUP_SQPOLL)))
		return -EFAULT;

	task_lock(ctx->sqo_task);
	mm = ctx->sqo_task->mm;
	if (unlikely(!mm || !mmget_not_zero(mm)))
		mm = NULL;
	task_unlock(ctx->sqo_task);

	if (mm) {
		kthread_use_mm(mm);
		return 0;
	}

	return -EFAULT;
}

static int io_sq_thread_acquire_mm(struct io_ring_ctx *ctx,
				   struct io_kiocb *req)
{
	if (!(io_op_defs[req->opcode].work_flags & IO_WQ_WORK_MM))
		return 0;
	return __io_sq_thread_acquire_mm(ctx);
}

static void io_sq_thread_associate_blkcg(struct io_ring_ctx *ctx,
					 struct cgroup_subsys_state **cur_css)

{
#ifdef CONFIG_BLK_CGROUP
	/* puts the old one when swapping */
	if (*cur_css != ctx->sqo_blkcg_css) {
		kthread_associate_blkcg(ctx->sqo_blkcg_css);
		*cur_css = ctx->sqo_blkcg_css;
	}
#endif
}

static void io_sq_thread_unassociate_blkcg(void)
{
#ifdef CONFIG_BLK_CGROUP
	kthread_associate_blkcg(NULL);
#endif
}

static inline void req_set_fail_links(struct io_kiocb *req)
{
	if ((req->flags & (REQ_F_LINK | REQ_F_HARDLINK)) == REQ_F_LINK)
		req->flags |= REQ_F_FAIL_LINK;
}

/*
 * None of these are dereferenced, they are simply used to check if any of
 * them have changed. If we're under current and check they are still the
 * same, we're fine to grab references to them for actual out-of-line use.
 */
static void io_init_identity(struct io_identity *id)
{
	id->files = current->files;
	id->mm = current->mm;
#ifdef CONFIG_BLK_CGROUP
	rcu_read_lock();
	id->blkcg_css = blkcg_css();
	rcu_read_unlock();
#endif
	id->creds = current_cred();
	id->nsproxy = current->nsproxy;
	id->fs = current->fs;
	id->fsize = rlimit(RLIMIT_FSIZE);
#ifdef CONFIG_AUDIT
	id->loginuid = current->loginuid;
	id->sessionid = current->sessionid;
#endif
	refcount_set(&id->count, 1);
}

static inline void __io_req_init_async(struct io_kiocb *req)
{
	memset(&req->work, 0, sizeof(req->work));
	req->flags |= REQ_F_WORK_INITIALIZED;
}

/*
 * Note: must call io_req_init_async() for the first time you
 * touch any members of io_wq_work.
 */
static inline void io_req_init_async(struct io_kiocb *req)
{
	struct io_uring_task *tctx = current->io_uring;

	if (req->flags & REQ_F_WORK_INITIALIZED)
		return;

	__io_req_init_async(req);

	/* Grab a ref if this isn't our static identity */
	req->work.identity = tctx->identity;
	if (tctx->identity != &tctx->__identity)
		refcount_inc(&req->work.identity->count);
}

static inline bool io_async_submit(struct io_ring_ctx *ctx)
{
	return ctx->flags & IORING_SETUP_SQPOLL;
}

static void io_ring_ctx_ref_free(struct percpu_ref *ref)
{
	struct io_ring_ctx *ctx = container_of(ref, struct io_ring_ctx, refs);

	complete(&ctx->ref_comp);
}

static inline bool io_is_timeout_noseq(struct io_kiocb *req)
{
	return !req->timeout.off;
}

static struct io_ring_ctx *io_ring_ctx_alloc(struct io_uring_params *p)
{
	struct io_ring_ctx *ctx;
	int hash_bits;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->fallback_req = kmem_cache_alloc(req_cachep, GFP_KERNEL);
	if (!ctx->fallback_req)
		goto err;

	/*
	 * Use 5 bits less than the max cq entries, that should give us around
	 * 32 entries per hash list if totally full and uniformly spread.
	 */
	hash_bits = ilog2(p->cq_entries);
	hash_bits -= 5;
	if (hash_bits <= 0)
		hash_bits = 1;
	ctx->cancel_hash_bits = hash_bits;
	ctx->cancel_hash = kmalloc((1U << hash_bits) * sizeof(struct hlist_head),
					GFP_KERNEL);
	if (!ctx->cancel_hash)
		goto err;
	__hash_init(ctx->cancel_hash, 1U << hash_bits);

	if (percpu_ref_init(&ctx->refs, io_ring_ctx_ref_free,
			    PERCPU_REF_ALLOW_REINIT, GFP_KERNEL))
		goto err;

	ctx->flags = p->flags;
	init_waitqueue_head(&ctx->sqo_sq_wait);
	INIT_LIST_HEAD(&ctx->sqd_list);
	init_waitqueue_head(&ctx->cq_wait);
	INIT_LIST_HEAD(&ctx->cq_overflow_list);
	init_completion(&ctx->ref_comp);
	init_completion(&ctx->sq_thread_comp);
	idr_init(&ctx->io_buffer_idr);
	idr_init(&ctx->personality_idr);
	mutex_init(&ctx->uring_lock);
	init_waitqueue_head(&ctx->wait);
	spin_lock_init(&ctx->completion_lock);
	INIT_LIST_HEAD(&ctx->iopoll_list);
	INIT_LIST_HEAD(&ctx->defer_list);
	INIT_LIST_HEAD(&ctx->timeout_list);
	init_waitqueue_head(&ctx->inflight_wait);
	spin_lock_init(&ctx->inflight_lock);
	INIT_LIST_HEAD(&ctx->inflight_list);
	INIT_DELAYED_WORK(&ctx->file_put_work, io_file_put_work);
	init_llist_head(&ctx->file_put_llist);
	return ctx;
err:
	if (ctx->fallback_req)
		kmem_cache_free(req_cachep, ctx->fallback_req);
	kfree(ctx->cancel_hash);
	kfree(ctx);
	return NULL;
}

static bool req_need_defer(struct io_kiocb *req, u32 seq)
{
	if (unlikely(req->flags & REQ_F_IO_DRAIN)) {
		struct io_ring_ctx *ctx = req->ctx;

		return seq != ctx->cached_cq_tail
				+ READ_ONCE(ctx->cached_cq_overflow);
	}

	return false;
}

static void __io_commit_cqring(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;

	/* order cqe stores with ring update */
	smp_store_release(&rings->cq.tail, ctx->cached_cq_tail);

	if (wq_has_sleeper(&ctx->cq_wait)) {
		wake_up_interruptible(&ctx->cq_wait);
		kill_fasync(&ctx->cq_fasync, SIGIO, POLL_IN);
	}
}

static void io_put_identity(struct io_uring_task *tctx, struct io_kiocb *req)
{
	if (req->work.identity == &tctx->__identity)
		return;
	if (refcount_dec_and_test(&req->work.identity->count))
		kfree(req->work.identity);
}

static void io_req_clean_work(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_WORK_INITIALIZED))
		return;

	req->flags &= ~REQ_F_WORK_INITIALIZED;

	if (req->work.flags & IO_WQ_WORK_MM) {
		mmdrop(req->work.identity->mm);
		req->work.flags &= ~IO_WQ_WORK_MM;
	}
#ifdef CONFIG_BLK_CGROUP
	if (req->work.flags & IO_WQ_WORK_BLKCG) {
		css_put(req->work.identity->blkcg_css);
		req->work.flags &= ~IO_WQ_WORK_BLKCG;
	}
#endif
	if (req->work.flags & IO_WQ_WORK_CREDS) {
		put_cred(req->work.identity->creds);
		req->work.flags &= ~IO_WQ_WORK_CREDS;
	}
	if (req->work.flags & IO_WQ_WORK_FS) {
		struct fs_struct *fs = req->work.identity->fs;

		spin_lock(&req->work.identity->fs->lock);
		if (--fs->users)
			fs = NULL;
		spin_unlock(&req->work.identity->fs->lock);
		if (fs)
			free_fs_struct(fs);
		req->work.flags &= ~IO_WQ_WORK_FS;
	}

	io_put_identity(req->task->io_uring, req);
}

/*
 * Create a private copy of io_identity, since some fields don't match
 * the current context.
 */
static bool io_identity_cow(struct io_kiocb *req)
{
	struct io_uring_task *tctx = current->io_uring;
	const struct cred *creds = NULL;
	struct io_identity *id;

	if (req->work.flags & IO_WQ_WORK_CREDS)
		creds = req->work.identity->creds;

	id = kmemdup(req->work.identity, sizeof(*id), GFP_KERNEL);
	if (unlikely(!id)) {
		req->work.flags |= IO_WQ_WORK_CANCEL;
		return false;
	}

	/*
	 * We can safely just re-init the creds we copied  Either the field
	 * matches the current one, or we haven't grabbed it yet. The only
	 * exception is ->creds, through registered personalities, so handle
	 * that one separately.
	 */
	io_init_identity(id);
	if (creds)
		id->creds = creds;

	/* add one for this request */
	refcount_inc(&id->count);

	/* drop tctx and req identity references, if needed */
	if (tctx->identity != &tctx->__identity &&
	    refcount_dec_and_test(&tctx->identity->count))
		kfree(tctx->identity);
	if (req->work.identity != &tctx->__identity &&
	    refcount_dec_and_test(&req->work.identity->count))
		kfree(req->work.identity);

	req->work.identity = id;
	tctx->identity = id;
	return true;
}

static bool io_grab_identity(struct io_kiocb *req)
{
	const struct io_op_def *def = &io_op_defs[req->opcode];
	struct io_identity *id = req->work.identity;
	struct io_ring_ctx *ctx = req->ctx;

	if (def->work_flags & IO_WQ_WORK_FSIZE) {
		if (id->fsize != rlimit(RLIMIT_FSIZE))
			return false;
		req->work.flags |= IO_WQ_WORK_FSIZE;
	}
#ifdef CONFIG_BLK_CGROUP
	if (!(req->work.flags & IO_WQ_WORK_BLKCG) &&
	    (def->work_flags & IO_WQ_WORK_BLKCG)) {
		rcu_read_lock();
		if (id->blkcg_css != blkcg_css()) {
			rcu_read_unlock();
			return false;
		}
		/*
		 * This should be rare, either the cgroup is dying or the task
		 * is moving cgroups. Just punt to root for the handful of ios.
		 */
		if (css_tryget_online(id->blkcg_css))
			req->work.flags |= IO_WQ_WORK_BLKCG;
		rcu_read_unlock();
	}
#endif
	if (!(req->work.flags & IO_WQ_WORK_CREDS)) {
		if (id->creds != current_cred())
			return false;
		get_cred(id->creds);
		req->work.flags |= IO_WQ_WORK_CREDS;
	}
#ifdef CONFIG_AUDIT
	if (!uid_eq(current->loginuid, id->loginuid) ||
	    current->sessionid != id->sessionid)
		return false;
#endif
	if (!(req->work.flags & IO_WQ_WORK_FS) &&
	    (def->work_flags & IO_WQ_WORK_FS)) {
		if (current->fs != id->fs)
			return false;
		spin_lock(&id->fs->lock);
		if (!id->fs->in_exec) {
			id->fs->users++;
			req->work.flags |= IO_WQ_WORK_FS;
		} else {
			req->work.flags |= IO_WQ_WORK_CANCEL;
		}
		spin_unlock(&current->fs->lock);
	}
	if (!(req->work.flags & IO_WQ_WORK_FILES) &&
	    (def->work_flags & IO_WQ_WORK_FILES) &&
	    !(req->flags & REQ_F_NO_FILE_TABLE)) {
		if (id->files != current->files ||
		    id->nsproxy != current->nsproxy)
			return false;
		atomic_inc(&id->files->count);
		get_nsproxy(id->nsproxy);
		req->flags |= REQ_F_INFLIGHT;

		spin_lock_irq(&ctx->inflight_lock);
		list_add(&req->inflight_entry, &ctx->inflight_list);
		spin_unlock_irq(&ctx->inflight_lock);
		req->work.flags |= IO_WQ_WORK_FILES;
	}

	return true;
}

static void io_prep_async_work(struct io_kiocb *req)
{
	const struct io_op_def *def = &io_op_defs[req->opcode];
	struct io_ring_ctx *ctx = req->ctx;
	struct io_identity *id;

	io_req_init_async(req);
	id = req->work.identity;

	if (req->flags & REQ_F_FORCE_ASYNC)
		req->work.flags |= IO_WQ_WORK_CONCURRENT;

	if (req->flags & REQ_F_ISREG) {
		if (def->hash_reg_file || (ctx->flags & IORING_SETUP_IOPOLL))
			io_wq_hash_work(&req->work, file_inode(req->file));
	} else {
		if (def->unbound_nonreg_file)
			req->work.flags |= IO_WQ_WORK_UNBOUND;
	}

	/* ->mm can never change on us */
	if (!(req->work.flags & IO_WQ_WORK_MM) &&
	    (def->work_flags & IO_WQ_WORK_MM)) {
		mmgrab(id->mm);
		req->work.flags |= IO_WQ_WORK_MM;
	}

	/* if we fail grabbing identity, we must COW, regrab, and retry */
	if (io_grab_identity(req))
		return;

	if (!io_identity_cow(req))
		return;

	/* can't fail at this point */
	if (!io_grab_identity(req))
		WARN_ON(1);
}

static void io_prep_async_link(struct io_kiocb *req)
{
	struct io_kiocb *cur;

	io_prep_async_work(req);
	if (req->flags & REQ_F_LINK_HEAD)
		list_for_each_entry(cur, &req->link_list, link_list)
			io_prep_async_work(cur);
}

static struct io_kiocb *__io_queue_async_work(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *link = io_prep_linked_timeout(req);

	trace_io_uring_queue_async_work(ctx, io_wq_is_hashed(&req->work), req,
					&req->work, req->flags);
	io_wq_enqueue(ctx->io_wq, &req->work);
	return link;
}

static void io_queue_async_work(struct io_kiocb *req)
{
	struct io_kiocb *link;

	/* init ->work of the whole link before punting */
	io_prep_async_link(req);
	link = __io_queue_async_work(req);

	if (link)
		io_queue_linked_timeout(link);
}

static void io_kill_timeout(struct io_kiocb *req)
{
	struct io_timeout_data *io = req->async_data;
	int ret;

	ret = hrtimer_try_to_cancel(&io->timer);
	if (ret != -1) {
		atomic_set(&req->ctx->cq_timeouts,
			atomic_read(&req->ctx->cq_timeouts) + 1);
		list_del_init(&req->timeout.list);
		io_cqring_fill_event(req, 0);
		io_put_req_deferred(req, 1);
	}
}

static bool io_task_match(struct io_kiocb *req, struct task_struct *tsk)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (!tsk || req->task == tsk)
		return true;
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		if (ctx->sq_data && req->task == ctx->sq_data->thread)
			return true;
	}
	return false;
}

/*
 * Returns true if we found and killed one or more timeouts
 */
static bool io_kill_timeouts(struct io_ring_ctx *ctx, struct task_struct *tsk)
{
	struct io_kiocb *req, *tmp;
	int canceled = 0;

	spin_lock_irq(&ctx->completion_lock);
	list_for_each_entry_safe(req, tmp, &ctx->timeout_list, timeout.list) {
		if (io_task_match(req, tsk)) {
			io_kill_timeout(req);
			canceled++;
		}
	}
	spin_unlock_irq(&ctx->completion_lock);
	return canceled != 0;
}

static void __io_queue_deferred(struct io_ring_ctx *ctx)
{
	do {
		struct io_defer_entry *de = list_first_entry(&ctx->defer_list,
						struct io_defer_entry, list);
		struct io_kiocb *link;

		if (req_need_defer(de->req, de->seq))
			break;
		list_del_init(&de->list);
		/* punt-init is done before queueing for defer */
		link = __io_queue_async_work(de->req);
		if (link) {
			__io_queue_linked_timeout(link);
			/* drop submission reference */
			io_put_req_deferred(link, 1);
		}
		kfree(de);
	} while (!list_empty(&ctx->defer_list));
}

static void io_flush_timeouts(struct io_ring_ctx *ctx)
{
	while (!list_empty(&ctx->timeout_list)) {
		struct io_kiocb *req = list_first_entry(&ctx->timeout_list,
						struct io_kiocb, timeout.list);

		if (io_is_timeout_noseq(req))
			break;
		if (req->timeout.target_seq != ctx->cached_cq_tail
					- atomic_read(&ctx->cq_timeouts))
			break;

		list_del_init(&req->timeout.list);
		io_kill_timeout(req);
	}
}

static void io_commit_cqring(struct io_ring_ctx *ctx)
{
	io_flush_timeouts(ctx);
	__io_commit_cqring(ctx);

	if (unlikely(!list_empty(&ctx->defer_list)))
		__io_queue_deferred(ctx);
}

static inline bool io_sqring_full(struct io_ring_ctx *ctx)
{
	struct io_rings *r = ctx->rings;

	return READ_ONCE(r->sq.tail) - ctx->cached_sq_head == r->sq_ring_entries;
}

static struct io_uring_cqe *io_get_cqring(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;
	unsigned tail;

	tail = ctx->cached_cq_tail;
	/*
	 * writes to the cq entry need to come after reading head; the
	 * control dependency is enough as we're using WRITE_ONCE to
	 * fill the cq entry
	 */
	if (tail - READ_ONCE(rings->cq.head) == rings->cq_ring_entries)
		return NULL;

	ctx->cached_cq_tail++;
	return &rings->cqes[tail & ctx->cq_mask];
}

static inline bool io_should_trigger_evfd(struct io_ring_ctx *ctx)
{
	if (!ctx->cq_ev_fd)
		return false;
	if (READ_ONCE(ctx->rings->cq_flags) & IORING_CQ_EVENTFD_DISABLED)
		return false;
	if (!ctx->eventfd_async)
		return true;
	return io_wq_current_is_worker();
}

static void io_cqring_ev_posted(struct io_ring_ctx *ctx)
{
	if (waitqueue_active(&ctx->wait))
		wake_up(&ctx->wait);
	if (ctx->sq_data && waitqueue_active(&ctx->sq_data->wait))
		wake_up(&ctx->sq_data->wait);
	if (io_should_trigger_evfd(ctx))
		eventfd_signal(ctx->cq_ev_fd, 1);
}

static void io_cqring_mark_overflow(struct io_ring_ctx *ctx)
{
	if (list_empty(&ctx->cq_overflow_list)) {
		clear_bit(0, &ctx->sq_check_overflow);
		clear_bit(0, &ctx->cq_check_overflow);
		ctx->rings->sq_flags &= ~IORING_SQ_CQ_OVERFLOW;
	}
}

static inline bool __io_match_files(struct io_kiocb *req,
				    struct files_struct *files)
{
	return ((req->flags & REQ_F_WORK_INITIALIZED) &&
	        (req->work.flags & IO_WQ_WORK_FILES)) &&
		req->work.identity->files == files;
}

static bool io_match_files(struct io_kiocb *req,
			   struct files_struct *files)
{
	struct io_kiocb *link;

	if (!files)
		return true;
	if (__io_match_files(req, files))
		return true;
	if (req->flags & REQ_F_LINK_HEAD) {
		list_for_each_entry(link, &req->link_list, link_list) {
			if (__io_match_files(link, files))
				return true;
		}
	}
	return false;
}

/* Returns true if there are no backlogged entries after the flush */
static bool io_cqring_overflow_flush(struct io_ring_ctx *ctx, bool force,
				     struct task_struct *tsk,
				     struct files_struct *files)
{
	struct io_rings *rings = ctx->rings;
	struct io_kiocb *req, *tmp;
	struct io_uring_cqe *cqe;
	unsigned long flags;
	LIST_HEAD(list);

	if (!force) {
		if (list_empty_careful(&ctx->cq_overflow_list))
			return true;
		if ((ctx->cached_cq_tail - READ_ONCE(rings->cq.head) ==
		    rings->cq_ring_entries))
			return false;
	}

	spin_lock_irqsave(&ctx->completion_lock, flags);

	cqe = NULL;
	list_for_each_entry_safe(req, tmp, &ctx->cq_overflow_list, compl.list) {
		if (tsk && req->task != tsk)
			continue;
		if (!io_match_files(req, files))
			continue;

		cqe = io_get_cqring(ctx);
		if (!cqe && !force)
			break;

		list_move(&req->compl.list, &list);
		if (cqe) {
			WRITE_ONCE(cqe->user_data, req->user_data);
			WRITE_ONCE(cqe->res, req->result);
			WRITE_ONCE(cqe->flags, req->compl.cflags);
		} else {
			ctx->cached_cq_overflow++;
			WRITE_ONCE(ctx->rings->cq_overflow,
				   ctx->cached_cq_overflow);
		}
	}

	io_commit_cqring(ctx);
	io_cqring_mark_overflow(ctx);

	spin_unlock_irqrestore(&ctx->completion_lock, flags);
	io_cqring_ev_posted(ctx);

	while (!list_empty(&list)) {
		req = list_first_entry(&list, struct io_kiocb, compl.list);
		list_del(&req->compl.list);
		io_put_req(req);
	}

	return cqe != NULL;
}

static void __io_cqring_fill_event(struct io_kiocb *req, long res, long cflags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_uring_cqe *cqe;

	trace_io_uring_complete(ctx, req->user_data, res);

	/*
	 * If we can't get a cq entry, userspace overflowed the
	 * submission (by quite a lot). Increment the overflow count in
	 * the ring.
	 */
	cqe = io_get_cqring(ctx);
	if (likely(cqe)) {
		WRITE_ONCE(cqe->user_data, req->user_data);
		WRITE_ONCE(cqe->res, res);
		WRITE_ONCE(cqe->flags, cflags);
	} else if (ctx->cq_overflow_flushed ||
		   atomic_read(&req->task->io_uring->in_idle)) {
		/*
		 * If we're in ring overflow flush mode, or in task cancel mode,
		 * then we cannot store the request for later flushing, we need
		 * to drop it on the floor.
		 */
		ctx->cached_cq_overflow++;
		WRITE_ONCE(ctx->rings->cq_overflow, ctx->cached_cq_overflow);
	} else {
		if (list_empty(&ctx->cq_overflow_list)) {
			set_bit(0, &ctx->sq_check_overflow);
			set_bit(0, &ctx->cq_check_overflow);
			ctx->rings->sq_flags |= IORING_SQ_CQ_OVERFLOW;
		}
		io_clean_op(req);
		req->result = res;
		req->compl.cflags = cflags;
		refcount_inc(&req->refs);
		list_add_tail(&req->compl.list, &ctx->cq_overflow_list);
	}
}

static void io_cqring_fill_event(struct io_kiocb *req, long res)
{
	__io_cqring_fill_event(req, res, 0);
}

static void io_cqring_add_event(struct io_kiocb *req, long res, long cflags)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	spin_lock_irqsave(&ctx->completion_lock, flags);
	__io_cqring_fill_event(req, res, cflags);
	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	io_cqring_ev_posted(ctx);
}

static void io_submit_flush_completions(struct io_comp_state *cs)
{
	struct io_ring_ctx *ctx = cs->ctx;

	spin_lock_irq(&ctx->completion_lock);
	while (!list_empty(&cs->list)) {
		struct io_kiocb *req;

		req = list_first_entry(&cs->list, struct io_kiocb, compl.list);
		list_del(&req->compl.list);
		__io_cqring_fill_event(req, req->result, req->compl.cflags);

		/*
		 * io_free_req() doesn't care about completion_lock unless one
		 * of these flags is set. REQ_F_WORK_INITIALIZED is in the list
		 * because of a potential deadlock with req->work.fs->lock
		 */
		if (req->flags & (REQ_F_FAIL_LINK|REQ_F_LINK_TIMEOUT
				 |REQ_F_WORK_INITIALIZED)) {
			spin_unlock_irq(&ctx->completion_lock);
			io_put_req(req);
			spin_lock_irq(&ctx->completion_lock);
		} else {
			io_put_req(req);
		}
	}
	io_commit_cqring(ctx);
	spin_unlock_irq(&ctx->completion_lock);

	io_cqring_ev_posted(ctx);
	cs->nr = 0;
}

static void __io_req_complete(struct io_kiocb *req, long res, unsigned cflags,
			      struct io_comp_state *cs)
{
	if (!cs) {
		io_cqring_add_event(req, res, cflags);
		io_put_req(req);
	} else {
		io_clean_op(req);
		req->result = res;
		req->compl.cflags = cflags;
		list_add_tail(&req->compl.list, &cs->list);
		if (++cs->nr >= 32)
			io_submit_flush_completions(cs);
	}
}

static void io_req_complete(struct io_kiocb *req, long res)
{
	__io_req_complete(req, res, 0, NULL);
}

static inline bool io_is_fallback_req(struct io_kiocb *req)
{
	return req == (struct io_kiocb *)
			((unsigned long) req->ctx->fallback_req & ~1UL);
}

static struct io_kiocb *io_get_fallback_req(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req;

	req = ctx->fallback_req;
	if (!test_and_set_bit_lock(0, (unsigned long *) &ctx->fallback_req))
		return req;

	return NULL;
}

static struct io_kiocb *io_alloc_req(struct io_ring_ctx *ctx,
				     struct io_submit_state *state)
{
	if (!state->free_reqs) {
		gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;
		size_t sz;
		int ret;

		sz = min_t(size_t, state->ios_left, ARRAY_SIZE(state->reqs));
		ret = kmem_cache_alloc_bulk(req_cachep, gfp, sz, state->reqs);

		/*
		 * Bulk alloc is all-or-nothing. If we fail to get a batch,
		 * retry single alloc to be on the safe side.
		 */
		if (unlikely(ret <= 0)) {
			state->reqs[0] = kmem_cache_alloc(req_cachep, gfp);
			if (!state->reqs[0])
				goto fallback;
			ret = 1;
		}
		state->free_reqs = ret;
	}

	state->free_reqs--;
	return state->reqs[state->free_reqs];
fallback:
	return io_get_fallback_req(ctx);
}

static inline void io_put_file(struct io_kiocb *req, struct file *file,
			  bool fixed)
{
	if (fixed)
		percpu_ref_put(req->fixed_file_refs);
	else
		fput(file);
}

static void io_dismantle_req(struct io_kiocb *req)
{
	io_clean_op(req);

	if (req->async_data)
		kfree(req->async_data);
	if (req->file)
		io_put_file(req, req->file, (req->flags & REQ_F_FIXED_FILE));

	io_req_clean_work(req);
}

static void __io_free_req(struct io_kiocb *req)
{
	struct io_uring_task *tctx = req->task->io_uring;
	struct io_ring_ctx *ctx = req->ctx;

	io_dismantle_req(req);

	percpu_counter_dec(&tctx->inflight);
	if (atomic_read(&tctx->in_idle))
		wake_up(&tctx->wait);
	put_task_struct(req->task);

	if (likely(!io_is_fallback_req(req)))
		kmem_cache_free(req_cachep, req);
	else
		clear_bit_unlock(0, (unsigned long *) &ctx->fallback_req);
	percpu_ref_put(&ctx->refs);
}

static void io_kill_linked_timeout(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *link;
	bool cancelled = false;
	unsigned long flags;

	spin_lock_irqsave(&ctx->completion_lock, flags);
	link = list_first_entry_or_null(&req->link_list, struct io_kiocb,
					link_list);
	/*
	 * Can happen if a linked timeout fired and link had been like
	 * req -> link t-out -> link t-out [-> ...]
	 */
	if (link && (link->flags & REQ_F_LTIMEOUT_ACTIVE)) {
		struct io_timeout_data *io = link->async_data;
		int ret;

		list_del_init(&link->link_list);
		ret = hrtimer_try_to_cancel(&io->timer);
		if (ret != -1) {
			io_cqring_fill_event(link, -ECANCELED);
			io_commit_cqring(ctx);
			cancelled = true;
		}
	}
	req->flags &= ~REQ_F_LINK_TIMEOUT;
	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	if (cancelled) {
		io_cqring_ev_posted(ctx);
		io_put_req(link);
	}
}

static struct io_kiocb *io_req_link_next(struct io_kiocb *req)
{
	struct io_kiocb *nxt;

	/*
	 * The list should never be empty when we are called here. But could
	 * potentially happen if the chain is messed up, check to be on the
	 * safe side.
	 */
	if (unlikely(list_empty(&req->link_list)))
		return NULL;

	nxt = list_first_entry(&req->link_list, struct io_kiocb, link_list);
	list_del_init(&req->link_list);
	if (!list_empty(&nxt->link_list))
		nxt->flags |= REQ_F_LINK_HEAD;
	return nxt;
}

/*
 * Called if REQ_F_LINK_HEAD is set, and we fail the head request
 */
static void io_fail_links(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	spin_lock_irqsave(&ctx->completion_lock, flags);
	while (!list_empty(&req->link_list)) {
		struct io_kiocb *link = list_first_entry(&req->link_list,
						struct io_kiocb, link_list);

		list_del_init(&link->link_list);
		trace_io_uring_fail_link(req, link);

		io_cqring_fill_event(link, -ECANCELED);

		/*
		 * It's ok to free under spinlock as they're not linked anymore,
		 * but avoid REQ_F_WORK_INITIALIZED because it may deadlock on
		 * work.fs->lock.
		 */
		if (link->flags & REQ_F_WORK_INITIALIZED)
			io_put_req_deferred(link, 2);
		else
			io_double_put_req(link);
	}

	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	io_cqring_ev_posted(ctx);
}

static struct io_kiocb *__io_req_find_next(struct io_kiocb *req)
{
	req->flags &= ~REQ_F_LINK_HEAD;
	if (req->flags & REQ_F_LINK_TIMEOUT)
		io_kill_linked_timeout(req);

	/*
	 * If LINK is set, we have dependent requests in this chain. If we
	 * didn't fail this request, queue the first one up, moving any other
	 * dependencies to the next request. In case of failure, fail the rest
	 * of the chain.
	 */
	if (likely(!(req->flags & REQ_F_FAIL_LINK)))
		return io_req_link_next(req);
	io_fail_links(req);
	return NULL;
}

static struct io_kiocb *io_req_find_next(struct io_kiocb *req)
{
	if (likely(!(req->flags & REQ_F_LINK_HEAD)))
		return NULL;
	return __io_req_find_next(req);
}

static int io_req_task_work_add(struct io_kiocb *req, bool twa_signal_ok)
{
	struct task_struct *tsk = req->task;
	struct io_ring_ctx *ctx = req->ctx;
	enum task_work_notify_mode notify;
	int ret;

	if (tsk->flags & PF_EXITING)
		return -ESRCH;

	/*
	 * SQPOLL kernel thread doesn't need notification, just a wakeup. For
	 * all other cases, use TWA_SIGNAL unconditionally to ensure we're
	 * processing task_work. There's no reliable way to tell if TWA_RESUME
	 * will do the job.
	 */
	notify = TWA_NONE;
	if (!(ctx->flags & IORING_SETUP_SQPOLL) && twa_signal_ok)
		notify = TWA_SIGNAL;

	ret = task_work_add(tsk, &req->task_work, notify);
	if (!ret)
		wake_up_process(tsk);

	return ret;
}

static void __io_req_task_cancel(struct io_kiocb *req, int error)
{
	struct io_ring_ctx *ctx = req->ctx;

	spin_lock_irq(&ctx->completion_lock);
	io_cqring_fill_event(req, error);
	io_commit_cqring(ctx);
	spin_unlock_irq(&ctx->completion_lock);

	io_cqring_ev_posted(ctx);
	req_set_fail_links(req);
	io_double_put_req(req);
}

static void io_req_task_cancel(struct callback_head *cb)
{
	struct io_kiocb *req = container_of(cb, struct io_kiocb, task_work);
	struct io_ring_ctx *ctx = req->ctx;

	__io_req_task_cancel(req, -ECANCELED);
	percpu_ref_put(&ctx->refs);
}

static void __io_req_task_submit(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (!__io_sq_thread_acquire_mm(ctx)) {
		mutex_lock(&ctx->uring_lock);
		__io_queue_sqe(req, NULL);
		mutex_unlock(&ctx->uring_lock);
	} else {
		__io_req_task_cancel(req, -EFAULT);
	}
}

static void io_req_task_submit(struct callback_head *cb)
{
	struct io_kiocb *req = container_of(cb, struct io_kiocb, task_work);
	struct io_ring_ctx *ctx = req->ctx;

	__io_req_task_submit(req);
	percpu_ref_put(&ctx->refs);
}

static void io_req_task_queue(struct io_kiocb *req)
{
	int ret;

	init_task_work(&req->task_work, io_req_task_submit);
	percpu_ref_get(&req->ctx->refs);

	ret = io_req_task_work_add(req, true);
	if (unlikely(ret)) {
		struct task_struct *tsk;

		init_task_work(&req->task_work, io_req_task_cancel);
		tsk = io_wq_get_task(req->ctx->io_wq);
		task_work_add(tsk, &req->task_work, TWA_NONE);
		wake_up_process(tsk);
	}
}

static void io_queue_next(struct io_kiocb *req)
{
	struct io_kiocb *nxt = io_req_find_next(req);

	if (nxt)
		io_req_task_queue(nxt);
}

static void io_free_req(struct io_kiocb *req)
{
	io_queue_next(req);
	__io_free_req(req);
}

struct req_batch {
	void *reqs[IO_IOPOLL_BATCH];
	int to_free;

	struct task_struct	*task;
	int			task_refs;
};

static inline void io_init_req_batch(struct req_batch *rb)
{
	rb->to_free = 0;
	rb->task_refs = 0;
	rb->task = NULL;
}

static void __io_req_free_batch_flush(struct io_ring_ctx *ctx,
				      struct req_batch *rb)
{
	kmem_cache_free_bulk(req_cachep, rb->to_free, rb->reqs);
	percpu_ref_put_many(&ctx->refs, rb->to_free);
	rb->to_free = 0;
}

static void io_req_free_batch_finish(struct io_ring_ctx *ctx,
				     struct req_batch *rb)
{
	if (rb->to_free)
		__io_req_free_batch_flush(ctx, rb);
	if (rb->task) {
		struct io_uring_task *tctx = rb->task->io_uring;

		percpu_counter_sub(&tctx->inflight, rb->task_refs);
		put_task_struct_many(rb->task, rb->task_refs);
		rb->task = NULL;
	}
}

static void io_req_free_batch(struct req_batch *rb, struct io_kiocb *req)
{
	if (unlikely(io_is_fallback_req(req))) {
		io_free_req(req);
		return;
	}
	if (req->flags & REQ_F_LINK_HEAD)
		io_queue_next(req);

	if (req->task != rb->task) {
		if (rb->task) {
			struct io_uring_task *tctx = rb->task->io_uring;

			percpu_counter_sub(&tctx->inflight, rb->task_refs);
			put_task_struct_many(rb->task, rb->task_refs);
		}
		rb->task = req->task;
		rb->task_refs = 0;
	}
	rb->task_refs++;

	io_dismantle_req(req);
	rb->reqs[rb->to_free++] = req;
	if (unlikely(rb->to_free == ARRAY_SIZE(rb->reqs)))
		__io_req_free_batch_flush(req->ctx, rb);
}

/*
 * Drop reference to request, return next in chain (if there is one) if this
 * was the last reference to this request.
 */
static struct io_kiocb *io_put_req_find_next(struct io_kiocb *req)
{
	struct io_kiocb *nxt = NULL;

	if (refcount_dec_and_test(&req->refs)) {
		nxt = io_req_find_next(req);
		__io_free_req(req);
	}
	return nxt;
}

static void io_put_req(struct io_kiocb *req)
{
	if (refcount_dec_and_test(&req->refs))
		io_free_req(req);
}

static void io_put_req_deferred_cb(struct callback_head *cb)
{
	struct io_kiocb *req = container_of(cb, struct io_kiocb, task_work);

	io_free_req(req);
}

static void io_free_req_deferred(struct io_kiocb *req)
{
	int ret;

	init_task_work(&req->task_work, io_put_req_deferred_cb);
	ret = io_req_task_work_add(req, true);
	if (unlikely(ret)) {
		struct task_struct *tsk;

		tsk = io_wq_get_task(req->ctx->io_wq);
		task_work_add(tsk, &req->task_work, TWA_NONE);
		wake_up_process(tsk);
	}
}

static inline void io_put_req_deferred(struct io_kiocb *req, int refs)
{
	if (refcount_sub_and_test(refs, &req->refs))
		io_free_req_deferred(req);
}

static struct io_wq_work *io_steal_work(struct io_kiocb *req)
{
	struct io_kiocb *nxt;

	/*
	 * A ref is owned by io-wq in which context we're. So, if that's the
	 * last one, it's safe to steal next work. False negatives are Ok,
	 * it just will be re-punted async in io_put_work()
	 */
	if (refcount_read(&req->refs) != 1)
		return NULL;

	nxt = io_req_find_next(req);
	return nxt ? &nxt->work : NULL;
}

static void io_double_put_req(struct io_kiocb *req)
{
	/* drop both submit and complete references */
	if (refcount_sub_and_test(2, &req->refs))
		io_free_req(req);
}

static unsigned io_cqring_events(struct io_ring_ctx *ctx, bool noflush)
{
	struct io_rings *rings = ctx->rings;

	if (test_bit(0, &ctx->cq_check_overflow)) {
		/*
		 * noflush == true is from the waitqueue handler, just ensure
		 * we wake up the task, and the next invocation will flush the
		 * entries. We cannot safely to it from here.
		 */
		if (noflush)
			return -1U;

		io_cqring_overflow_flush(ctx, false, NULL, NULL);
	}

	/* See comment at the top of this file */
	smp_rmb();
	return ctx->cached_cq_tail - READ_ONCE(rings->cq.head);
}

static inline unsigned int io_sqring_entries(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;

	/* make sure SQ entry isn't read before tail */
	return smp_load_acquire(&rings->sq.tail) - ctx->cached_sq_head;
}

static unsigned int io_put_kbuf(struct io_kiocb *req, struct io_buffer *kbuf)
{
	unsigned int cflags;

	cflags = kbuf->bid << IORING_CQE_BUFFER_SHIFT;
	cflags |= IORING_CQE_F_BUFFER;
	req->flags &= ~REQ_F_BUFFER_SELECTED;
	kfree(kbuf);
	return cflags;
}

static inline unsigned int io_put_rw_kbuf(struct io_kiocb *req)
{
	struct io_buffer *kbuf;

	kbuf = (struct io_buffer *) (unsigned long) req->rw.addr;
	return io_put_kbuf(req, kbuf);
}

static inline bool io_run_task_work(void)
{
	/*
	 * Not safe to run on exiting task, and the task_work handling will
	 * not add work to such a task.
	 */
	if (unlikely(current->flags & PF_EXITING))
		return false;
	if (current->task_works) {
		__set_current_state(TASK_RUNNING);
		task_work_run();
		return true;
	}

	return false;
}

static void io_iopoll_queue(struct list_head *again)
{
	struct io_kiocb *req;

	do {
		req = list_first_entry(again, struct io_kiocb, inflight_entry);
		list_del(&req->inflight_entry);
		__io_complete_rw(req, -EAGAIN, 0, NULL);
	} while (!list_empty(again));
}

/*
 * Find and free completed poll iocbs
 */
static void io_iopoll_complete(struct io_ring_ctx *ctx, unsigned int *nr_events,
			       struct list_head *done)
{
	struct req_batch rb;
	struct io_kiocb *req;
	LIST_HEAD(again);

	/* order with ->result store in io_complete_rw_iopoll() */
	smp_rmb();

	io_init_req_batch(&rb);
	while (!list_empty(done)) {
		int cflags = 0;

		req = list_first_entry(done, struct io_kiocb, inflight_entry);
		if (READ_ONCE(req->result) == -EAGAIN) {
			req->result = 0;
			req->iopoll_completed = 0;
			list_move_tail(&req->inflight_entry, &again);
			continue;
		}
		list_del(&req->inflight_entry);

		if (req->flags & REQ_F_BUFFER_SELECTED)
			cflags = io_put_rw_kbuf(req);

		__io_cqring_fill_event(req, req->result, cflags);
		(*nr_events)++;

		if (refcount_dec_and_test(&req->refs))
			io_req_free_batch(&rb, req);
	}

	io_commit_cqring(ctx);
	if (ctx->flags & IORING_SETUP_SQPOLL)
		io_cqring_ev_posted(ctx);
	io_req_free_batch_finish(ctx, &rb);

	if (!list_empty(&again))
		io_iopoll_queue(&again);
}

static int io_do_iopoll(struct io_ring_ctx *ctx, unsigned int *nr_events,
			long min)
{
	struct io_kiocb *req, *tmp;
	LIST_HEAD(done);
	bool spin;
	int ret;

	/*
	 * Only spin for completions if we don't have multiple devices hanging
	 * off our complete list, and we're under the requested amount.
	 */
	spin = !ctx->poll_multi_file && *nr_events < min;

	ret = 0;
	list_for_each_entry_safe(req, tmp, &ctx->iopoll_list, inflight_entry) {
		struct kiocb *kiocb = &req->rw.kiocb;

		/*
		 * Move completed and retryable entries to our local lists.
		 * If we find a request that requires polling, break out
		 * and complete those lists first, if we have entries there.
		 */
		if (READ_ONCE(req->iopoll_completed)) {
			list_move_tail(&req->inflight_entry, &done);
			continue;
		}
		if (!list_empty(&done))
			break;

		ret = kiocb->ki_filp->f_op->iopoll(kiocb, spin);
		if (ret < 0)
			break;

		/* iopoll may have completed current req */
		if (READ_ONCE(req->iopoll_completed))
			list_move_tail(&req->inflight_entry, &done);

		if (ret && spin)
			spin = false;
		ret = 0;
	}

	if (!list_empty(&done))
		io_iopoll_complete(ctx, nr_events, &done);

	return ret;
}

/*
 * Poll for a minimum of 'min' events. Note that if min == 0 we consider that a
 * non-spinning poll check - we'll still enter the driver poll loop, but only
 * as a non-spinning completion check.
 */
static int io_iopoll_getevents(struct io_ring_ctx *ctx, unsigned int *nr_events,
				long min)
{
	while (!list_empty(&ctx->iopoll_list) && !need_resched()) {
		int ret;

		ret = io_do_iopoll(ctx, nr_events, min);
		if (ret < 0)
			return ret;
		if (*nr_events >= min)
			return 0;
	}

	return 1;
}

/*
 * We can't just wait for polled events to come to us, we have to actively
 * find and complete them.
 */
static void io_iopoll_try_reap_events(struct io_ring_ctx *ctx)
{
	if (!(ctx->flags & IORING_SETUP_IOPOLL))
		return;

	mutex_lock(&ctx->uring_lock);
	while (!list_empty(&ctx->iopoll_list)) {
		unsigned int nr_events = 0;

		io_do_iopoll(ctx, &nr_events, 0);

		/* let it sleep and repeat later if can't complete a request */
		if (nr_events == 0)
			break;
		/*
		 * Ensure we allow local-to-the-cpu processing to take place,
		 * in this case we need to ensure that we reap all events.
		 * Also let task_work, etc. to progress by releasing the mutex
		 */
		if (need_resched()) {
			mutex_unlock(&ctx->uring_lock);
			cond_resched();
			mutex_lock(&ctx->uring_lock);
		}
	}
	mutex_unlock(&ctx->uring_lock);
}

static int io_iopoll_check(struct io_ring_ctx *ctx, long min)
{
	unsigned int nr_events = 0;
	int iters = 0, ret = 0;

	/*
	 * We disallow the app entering submit/complete with polling, but we
	 * still need to lock the ring to prevent racing with polled issue
	 * that got punted to a workqueue.
	 */
	mutex_lock(&ctx->uring_lock);
	do {
		/*
		 * Don't enter poll loop if we already have events pending.
		 * If we do, we can potentially be spinning for commands that
		 * already triggered a CQE (eg in error).
		 */
		if (io_cqring_events(ctx, false))
			break;

		/*
		 * If a submit got punted to a workqueue, we can have the
		 * application entering polling for a command before it gets
		 * issued. That app will hold the uring_lock for the duration
		 * of the poll right here, so we need to take a breather every
		 * now and then to ensure that the issue has a chance to add
		 * the poll to the issued list. Otherwise we can spin here
		 * forever, while the workqueue is stuck trying to acquire the
		 * very same mutex.
		 */
		if (!(++iters & 7)) {
			mutex_unlock(&ctx->uring_lock);
			io_run_task_work();
			mutex_lock(&ctx->uring_lock);
		}

		ret = io_iopoll_getevents(ctx, &nr_events, min);
		if (ret <= 0)
			break;
		ret = 0;
	} while (min && !nr_events && !need_resched());

	mutex_unlock(&ctx->uring_lock);
	return ret;
}

static void kiocb_end_write(struct io_kiocb *req)
{
	/*
	 * Tell lockdep we inherited freeze protection from submission
	 * thread.
	 */
	if (req->flags & REQ_F_ISREG) {
		struct inode *inode = file_inode(req->file);

		__sb_writers_acquired(inode->i_sb, SB_FREEZE_WRITE);
	}
	file_end_write(req->file);
}

static void io_complete_rw_common(struct kiocb *kiocb, long res,
				  struct io_comp_state *cs)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);
	int cflags = 0;

	if (kiocb->ki_flags & IOCB_WRITE)
		kiocb_end_write(req);

	if (res != req->result)
		req_set_fail_links(req);
	if (req->flags & REQ_F_BUFFER_SELECTED)
		cflags = io_put_rw_kbuf(req);
	__io_req_complete(req, res, cflags, cs);
}

#ifdef CONFIG_BLOCK
static bool io_resubmit_prep(struct io_kiocb *req, int error)
{
	struct iovec inline_vecs[UIO_FASTIOV], *iovec = inline_vecs;
	ssize_t ret = -ECANCELED;
	struct iov_iter iter;
	int rw;

	if (error) {
		ret = error;
		goto end_req;
	}

	switch (req->opcode) {
	case IORING_OP_READV:
	case IORING_OP_READ_FIXED:
	case IORING_OP_READ:
		rw = READ;
		break;
	case IORING_OP_WRITEV:
	case IORING_OP_WRITE_FIXED:
	case IORING_OP_WRITE:
		rw = WRITE;
		break;
	default:
		printk_once(KERN_WARNING "io_uring: bad opcode in resubmit %d\n",
				req->opcode);
		goto end_req;
	}

	if (!req->async_data) {
		ret = io_import_iovec(rw, req, &iovec, &iter, false);
		if (ret < 0)
			goto end_req;
		ret = io_setup_async_rw(req, iovec, inline_vecs, &iter, false);
		if (!ret)
			return true;
		kfree(iovec);
	} else {
		return true;
	}
end_req:
	req_set_fail_links(req);
	return false;
}
#endif

static bool io_rw_reissue(struct io_kiocb *req, long res)
{
#ifdef CONFIG_BLOCK
	umode_t mode = file_inode(req->file)->i_mode;
	int ret;

	if (!S_ISBLK(mode) && !S_ISREG(mode))
		return false;
	if ((res != -EAGAIN && res != -EOPNOTSUPP) || io_wq_current_is_worker())
		return false;

	ret = io_sq_thread_acquire_mm(req->ctx, req);

	if (io_resubmit_prep(req, ret)) {
		refcount_inc(&req->refs);
		io_queue_async_work(req);
		return true;
	}

#endif
	return false;
}

static void __io_complete_rw(struct io_kiocb *req, long res, long res2,
			     struct io_comp_state *cs)
{
	if (!io_rw_reissue(req, res))
		io_complete_rw_common(&req->rw.kiocb, res, cs);
}

static void io_complete_rw(struct kiocb *kiocb, long res, long res2)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);

	__io_complete_rw(req, res, res2, NULL);
}

static void io_complete_rw_iopoll(struct kiocb *kiocb, long res, long res2)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);

	if (kiocb->ki_flags & IOCB_WRITE)
		kiocb_end_write(req);

	if (res != -EAGAIN && res != req->result)
		req_set_fail_links(req);

	WRITE_ONCE(req->result, res);
	/* order with io_poll_complete() checking ->result */
	smp_wmb();
	WRITE_ONCE(req->iopoll_completed, 1);
}

/*
 * After the iocb has been issued, it's safe to be found on the poll list.
 * Adding the kiocb to the list AFTER submission ensures that we don't
 * find it from a io_iopoll_getevents() thread before the issuer is done
 * accessing the kiocb cookie.
 */
static void io_iopoll_req_issued(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	/*
	 * Track whether we have multiple files in our lists. This will impact
	 * how we do polling eventually, not spinning if we're on potentially
	 * different devices.
	 */
	if (list_empty(&ctx->iopoll_list)) {
		ctx->poll_multi_file = false;
	} else if (!ctx->poll_multi_file) {
		struct io_kiocb *list_req;

		list_req = list_first_entry(&ctx->iopoll_list, struct io_kiocb,
						inflight_entry);
		if (list_req->file != req->file)
			ctx->poll_multi_file = true;
	}

	/*
	 * For fast devices, IO may have already completed. If it has, add
	 * it to the front so we find it first.
	 */
	if (READ_ONCE(req->iopoll_completed))
		list_add(&req->inflight_entry, &ctx->iopoll_list);
	else
		list_add_tail(&req->inflight_entry, &ctx->iopoll_list);

	if ((ctx->flags & IORING_SETUP_SQPOLL) &&
	    wq_has_sleeper(&ctx->sq_data->wait))
		wake_up(&ctx->sq_data->wait);
}

static void __io_state_file_put(struct io_submit_state *state)
{
	if (state->has_refs)
		fput_many(state->file, state->has_refs);
	state->file = NULL;
}

static inline void io_state_file_put(struct io_submit_state *state)
{
	if (state->file)
		__io_state_file_put(state);
}

/*
 * Get as many references to a file as we have IOs left in this submission,
 * assuming most submissions are for one file, or at least that each file
 * has more than one submission.
 */
static struct file *__io_file_get(struct io_submit_state *state, int fd)
{
	if (!state)
		return fget(fd);

	if (state->file) {
		if (state->fd == fd) {
			state->has_refs--;
			return state->file;
		}
		__io_state_file_put(state);
	}
	state->file = fget_many(fd, state->ios_left);
	if (!state->file)
		return NULL;

	state->fd = fd;
	state->has_refs = state->ios_left - 1;
	return state->file;
}

static bool io_bdev_nowait(struct block_device *bdev)
{
#ifdef CONFIG_BLOCK
	return !bdev || blk_queue_nowait(bdev_get_queue(bdev));
#else
	return true;
#endif
}

/*
 * If we tracked the file through the SCM inflight mechanism, we could support
 * any file. For now, just ensure that anything potentially problematic is done
 * inline.
 */
static bool io_file_supports_async(struct file *file, int rw)
{
	umode_t mode = file_inode(file)->i_mode;

	if (S_ISBLK(mode)) {
		if (io_bdev_nowait(file->f_inode->i_bdev))
			return true;
		return false;
	}
	if (S_ISCHR(mode) || S_ISSOCK(mode))
		return true;
	if (S_ISREG(mode)) {
		if (io_bdev_nowait(file->f_inode->i_sb->s_bdev) &&
		    file->f_op != &io_uring_fops)
			return true;
		return false;
	}

	/* any ->read/write should understand O_NONBLOCK */
	if (file->f_flags & O_NONBLOCK)
		return true;

	if (!(file->f_mode & FMODE_NOWAIT))
		return false;

	if (rw == READ)
		return file->f_op->read_iter != NULL;

	return file->f_op->write_iter != NULL;
}

static int io_prep_rw(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct kiocb *kiocb = &req->rw.kiocb;
	unsigned ioprio;
	int ret;

	if (S_ISREG(file_inode(req->file)->i_mode))
		req->flags |= REQ_F_ISREG;

	kiocb->ki_pos = READ_ONCE(sqe->off);
	if (kiocb->ki_pos == -1 && !(req->file->f_mode & FMODE_STREAM)) {
		req->flags |= REQ_F_CUR_POS;
		kiocb->ki_pos = req->file->f_pos;
	}
	kiocb->ki_hint = ki_hint_validate(file_write_hint(kiocb->ki_filp));
	kiocb->ki_flags = iocb_flags(kiocb->ki_filp);
	ret = kiocb_set_rw_flags(kiocb, READ_ONCE(sqe->rw_flags));
	if (unlikely(ret))
		return ret;

	ioprio = READ_ONCE(sqe->ioprio);
	if (ioprio) {
		ret = ioprio_check_cap(ioprio);
		if (ret)
			return ret;

		kiocb->ki_ioprio = ioprio;
	} else
		kiocb->ki_ioprio = get_current_ioprio();

	/* don't allow async punt if RWF_NOWAIT was requested */
	if (kiocb->ki_flags & IOCB_NOWAIT)
		req->flags |= REQ_F_NOWAIT;

	if (ctx->flags & IORING_SETUP_IOPOLL) {
		if (!(kiocb->ki_flags & IOCB_DIRECT) ||
		    !kiocb->ki_filp->f_op->iopoll)
			return -EOPNOTSUPP;

		kiocb->ki_flags |= IOCB_HIPRI;
		kiocb->ki_complete = io_complete_rw_iopoll;
		req->iopoll_completed = 0;
	} else {
		if (kiocb->ki_flags & IOCB_HIPRI)
			return -EINVAL;
		kiocb->ki_complete = io_complete_rw;
	}

	req->rw.addr = READ_ONCE(sqe->addr);
	req->rw.len = READ_ONCE(sqe->len);
	req->buf_index = READ_ONCE(sqe->buf_index);
	return 0;
}

static inline void io_rw_done(struct kiocb *kiocb, ssize_t ret)
{
	switch (ret) {
	case -EIOCBQUEUED:
		break;
	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
	case -ERESTARTNOHAND:
	case -ERESTART_RESTARTBLOCK:
		/*
		 * We can't just restart the syscall, since previously
		 * submitted sqes may already be in progress. Just fail this
		 * IO with EINTR.
		 */
		ret = -EINTR;
		fallthrough;
	default:
		kiocb->ki_complete(kiocb, ret, 0);
	}
}

static void kiocb_done(struct kiocb *kiocb, ssize_t ret,
		       struct io_comp_state *cs)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);
	struct io_async_rw *io = req->async_data;

	/* add previously done IO, if any */
	if (io && io->bytes_done > 0) {
		if (ret < 0)
			ret = io->bytes_done;
		else
			ret += io->bytes_done;
	}

	if (req->flags & REQ_F_CUR_POS)
		req->file->f_pos = kiocb->ki_pos;
	if (ret >= 0 && kiocb->ki_complete == io_complete_rw)
		__io_complete_rw(req, ret, 0, cs);
	else
		io_rw_done(kiocb, ret);
}

static ssize_t io_import_fixed(struct io_kiocb *req, int rw,
			       struct iov_iter *iter)
{
	struct io_ring_ctx *ctx = req->ctx;
	size_t len = req->rw.len;
	struct io_mapped_ubuf *imu;
	u16 index, buf_index = req->buf_index;
	size_t offset;
	u64 buf_addr;

	if (unlikely(buf_index >= ctx->nr_user_bufs))
		return -EFAULT;
	index = array_index_nospec(buf_index, ctx->nr_user_bufs);
	imu = &ctx->user_bufs[index];
	buf_addr = req->rw.addr;

	/* overflow */
	if (buf_addr + len < buf_addr)
		return -EFAULT;
	/* not inside the mapped region */
	if (buf_addr < imu->ubuf || buf_addr + len > imu->ubuf + imu->len)
		return -EFAULT;

	/*
	 * May not be a start of buffer, set size appropriately
	 * and advance us to the beginning.
	 */
	offset = buf_addr - imu->ubuf;
	iov_iter_bvec(iter, rw, imu->bvec, imu->nr_bvecs, offset + len);

	if (offset) {
		/*
		 * Don't use iov_iter_advance() here, as it's really slow for
		 * using the latter parts of a big fixed buffer - it iterates
		 * over each segment manually. We can cheat a bit here, because
		 * we know that:
		 *
		 * 1) it's a BVEC iter, we set it up
		 * 2) all bvecs are PAGE_SIZE in size, except potentially the
		 *    first and last bvec
		 *
		 * So just find our index, and adjust the iterator afterwards.
		 * If the offset is within the first bvec (or the whole first
		 * bvec, just use iov_iter_advance(). This makes it easier
		 * since we can just skip the first segment, which may not
		 * be PAGE_SIZE aligned.
		 */
		const struct bio_vec *bvec = imu->bvec;

		if (offset <= bvec->bv_len) {
			iov_iter_advance(iter, offset);
		} else {
			unsigned long seg_skip;

			/* skip first vec */
			offset -= bvec->bv_len;
			seg_skip = 1 + (offset >> PAGE_SHIFT);

			iter->bvec = bvec + seg_skip;
			iter->nr_segs -= seg_skip;
			iter->count -= bvec->bv_len + offset;
			iter->iov_offset = offset & ~PAGE_MASK;
		}
	}

	return len;
}

static void io_ring_submit_unlock(struct io_ring_ctx *ctx, bool needs_lock)
{
	if (needs_lock)
		mutex_unlock(&ctx->uring_lock);
}

static void io_ring_submit_lock(struct io_ring_ctx *ctx, bool needs_lock)
{
	/*
	 * "Normal" inline submissions always hold the uring_lock, since we
	 * grab it from the system call. Same is true for the SQPOLL offload.
	 * The only exception is when we've detached the request and issue it
	 * from an async worker thread, grab the lock for that case.
	 */
	if (needs_lock)
		mutex_lock(&ctx->uring_lock);
}

static struct io_buffer *io_buffer_select(struct io_kiocb *req, size_t *len,
					  int bgid, struct io_buffer *kbuf,
					  bool needs_lock)
{
	struct io_buffer *head;

	if (req->flags & REQ_F_BUFFER_SELECTED)
		return kbuf;

	io_ring_submit_lock(req->ctx, needs_lock);

	lockdep_assert_held(&req->ctx->uring_lock);

	head = idr_find(&req->ctx->io_buffer_idr, bgid);
	if (head) {
		if (!list_empty(&head->list)) {
			kbuf = list_last_entry(&head->list, struct io_buffer,
							list);
			list_del(&kbuf->list);
		} else {
			kbuf = head;
			idr_remove(&req->ctx->io_buffer_idr, bgid);
		}
		if (*len > kbuf->len)
			*len = kbuf->len;
	} else {
		kbuf = ERR_PTR(-ENOBUFS);
	}

	io_ring_submit_unlock(req->ctx, needs_lock);

	return kbuf;
}

static void __user *io_rw_buffer_select(struct io_kiocb *req, size_t *len,
					bool needs_lock)
{
	struct io_buffer *kbuf;
	u16 bgid;

	kbuf = (struct io_buffer *) (unsigned long) req->rw.addr;
	bgid = req->buf_index;
	kbuf = io_buffer_select(req, len, bgid, kbuf, needs_lock);
	if (IS_ERR(kbuf))
		return kbuf;
	req->rw.addr = (u64) (unsigned long) kbuf;
	req->flags |= REQ_F_BUFFER_SELECTED;
	return u64_to_user_ptr(kbuf->addr);
}

#ifdef CONFIG_COMPAT
static ssize_t io_compat_import(struct io_kiocb *req, struct iovec *iov,
				bool needs_lock)
{
	struct compat_iovec __user *uiov;
	compat_ssize_t clen;
	void __user *buf;
	ssize_t len;

	uiov = u64_to_user_ptr(req->rw.addr);
	if (!access_ok(uiov, sizeof(*uiov)))
		return -EFAULT;
	if (__get_user(clen, &uiov->iov_len))
		return -EFAULT;
	if (clen < 0)
		return -EINVAL;

	len = clen;
	buf = io_rw_buffer_select(req, &len, needs_lock);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	iov[0].iov_base = buf;
	iov[0].iov_len = (compat_size_t) len;
	return 0;
}
#endif

static ssize_t __io_iov_buffer_select(struct io_kiocb *req, struct iovec *iov,
				      bool needs_lock)
{
	struct iovec __user *uiov = u64_to_user_ptr(req->rw.addr);
	void __user *buf;
	ssize_t len;

	if (copy_from_user(iov, uiov, sizeof(*uiov)))
		return -EFAULT;

	len = iov[0].iov_len;
	if (len < 0)
		return -EINVAL;
	buf = io_rw_buffer_select(req, &len, needs_lock);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	iov[0].iov_base = buf;
	iov[0].iov_len = len;
	return 0;
}

static ssize_t io_iov_buffer_select(struct io_kiocb *req, struct iovec *iov,
				    bool needs_lock)
{
	if (req->flags & REQ_F_BUFFER_SELECTED) {
		struct io_buffer *kbuf;

		kbuf = (struct io_buffer *) (unsigned long) req->rw.addr;
		iov[0].iov_base = u64_to_user_ptr(kbuf->addr);
		iov[0].iov_len = kbuf->len;
		return 0;
	}
	if (req->rw.len != 1)
		return -EINVAL;

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		return io_compat_import(req, iov, needs_lock);
#endif

	return __io_iov_buffer_select(req, iov, needs_lock);
}

static ssize_t __io_import_iovec(int rw, struct io_kiocb *req,
				 struct iovec **iovec, struct iov_iter *iter,
				 bool needs_lock)
{
	void __user *buf = u64_to_user_ptr(req->rw.addr);
	size_t sqe_len = req->rw.len;
	ssize_t ret;
	u8 opcode;

	opcode = req->opcode;
	if (opcode == IORING_OP_READ_FIXED || opcode == IORING_OP_WRITE_FIXED) {
		*iovec = NULL;
		return io_import_fixed(req, rw, iter);
	}

	/* buffer index only valid with fixed read/write, or buffer select  */
	if (req->buf_index && !(req->flags & REQ_F_BUFFER_SELECT))
		return -EINVAL;

	if (opcode == IORING_OP_READ || opcode == IORING_OP_WRITE) {
		if (req->flags & REQ_F_BUFFER_SELECT) {
			buf = io_rw_buffer_select(req, &sqe_len, needs_lock);
			if (IS_ERR(buf))
				return PTR_ERR(buf);
			req->rw.len = sqe_len;
		}

		ret = import_single_range(rw, buf, sqe_len, *iovec, iter);
		*iovec = NULL;
		return ret < 0 ? ret : sqe_len;
	}

	if (req->flags & REQ_F_BUFFER_SELECT) {
		ret = io_iov_buffer_select(req, *iovec, needs_lock);
		if (!ret) {
			ret = (*iovec)->iov_len;
			iov_iter_init(iter, rw, *iovec, 1, ret);
		}
		*iovec = NULL;
		return ret;
	}

	return __import_iovec(rw, buf, sqe_len, UIO_FASTIOV, iovec, iter,
			      req->ctx->compat);
}

static ssize_t io_import_iovec(int rw, struct io_kiocb *req,
			       struct iovec **iovec, struct iov_iter *iter,
			       bool needs_lock)
{
	struct io_async_rw *iorw = req->async_data;

	if (!iorw)
		return __io_import_iovec(rw, req, iovec, iter, needs_lock);
	*iovec = NULL;
	return iov_iter_count(&iorw->iter);
}

static inline loff_t *io_kiocb_ppos(struct kiocb *kiocb)
{
	return (kiocb->ki_filp->f_mode & FMODE_STREAM) ? NULL : &kiocb->ki_pos;
}

/*
 * For files that don't have ->read_iter() and ->write_iter(), handle them
 * by looping over ->read() or ->write() manually.
 */
static ssize_t loop_rw_iter(int rw, struct io_kiocb *req, struct iov_iter *iter)
{
	struct kiocb *kiocb = &req->rw.kiocb;
	struct file *file = req->file;
	ssize_t ret = 0;

	/*
	 * Don't support polled IO through this interface, and we can't
	 * support non-blocking either. For the latter, this just causes
	 * the kiocb to be handled from an async context.
	 */
	if (kiocb->ki_flags & IOCB_HIPRI)
		return -EOPNOTSUPP;
	if (kiocb->ki_flags & IOCB_NOWAIT)
		return -EAGAIN;

	while (iov_iter_count(iter)) {
		struct iovec iovec;
		ssize_t nr;

		if (!iov_iter_is_bvec(iter)) {
			iovec = iov_iter_iovec(iter);
		} else {
			iovec.iov_base = u64_to_user_ptr(req->rw.addr);
			iovec.iov_len = req->rw.len;
		}

		if (rw == READ) {
			nr = file->f_op->read(file, iovec.iov_base,
					      iovec.iov_len, io_kiocb_ppos(kiocb));
		} else {
			nr = file->f_op->write(file, iovec.iov_base,
					       iovec.iov_len, io_kiocb_ppos(kiocb));
		}

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != iovec.iov_len)
			break;
		req->rw.len -= nr;
		req->rw.addr += nr;
		iov_iter_advance(iter, nr);
	}

	return ret;
}

static void io_req_map_rw(struct io_kiocb *req, const struct iovec *iovec,
			  const struct iovec *fast_iov, struct iov_iter *iter)
{
	struct io_async_rw *rw = req->async_data;

	memcpy(&rw->iter, iter, sizeof(*iter));
	rw->free_iovec = iovec;
	rw->bytes_done = 0;
	/* can only be fixed buffers, no need to do anything */
	if (iov_iter_is_bvec(iter))
		return;
	if (!iovec) {
		unsigned iov_off = 0;

		rw->iter.iov = rw->fast_iov;
		if (iter->iov != fast_iov) {
			iov_off = iter->iov - fast_iov;
			rw->iter.iov += iov_off;
		}
		if (rw->fast_iov != fast_iov)
			memcpy(rw->fast_iov + iov_off, fast_iov + iov_off,
			       sizeof(struct iovec) * iter->nr_segs);
	} else {
		req->flags |= REQ_F_NEED_CLEANUP;
	}
}

static inline int __io_alloc_async_data(struct io_kiocb *req)
{
	WARN_ON_ONCE(!io_op_defs[req->opcode].async_size);
	req->async_data = kmalloc(io_op_defs[req->opcode].async_size, GFP_KERNEL);
	return req->async_data == NULL;
}

static int io_alloc_async_data(struct io_kiocb *req)
{
	if (!io_op_defs[req->opcode].needs_async_data)
		return 0;

	return  __io_alloc_async_data(req);
}

static int io_setup_async_rw(struct io_kiocb *req, const struct iovec *iovec,
			     const struct iovec *fast_iov,
			     struct iov_iter *iter, bool force)
{
	if (!force && !io_op_defs[req->opcode].needs_async_data)
		return 0;
	if (!req->async_data) {
		if (__io_alloc_async_data(req))
			return -ENOMEM;

		io_req_map_rw(req, iovec, fast_iov, iter);
	}
	return 0;
}

static inline int io_rw_prep_async(struct io_kiocb *req, int rw)
{
	struct io_async_rw *iorw = req->async_data;
	struct iovec *iov = iorw->fast_iov;
	ssize_t ret;

	ret = __io_import_iovec(rw, req, &iov, &iorw->iter, false);
	if (unlikely(ret < 0))
		return ret;

	iorw->bytes_done = 0;
	iorw->free_iovec = iov;
	if (iov)
		req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

static int io_read_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	ssize_t ret;

	ret = io_prep_rw(req, sqe);
	if (ret)
		return ret;

	if (unlikely(!(req->file->f_mode & FMODE_READ)))
		return -EBADF;

	/* either don't need iovec imported or already have it */
	if (!req->async_data)
		return 0;
	return io_rw_prep_async(req, READ);
}

/*
 * This is our waitqueue callback handler, registered through lock_page_async()
 * when we initially tried to do the IO with the iocb armed our waitqueue.
 * This gets called when the page is unlocked, and we generally expect that to
 * happen when the page IO is completed and the page is now uptodate. This will
 * queue a task_work based retry of the operation, attempting to copy the data
 * again. If the latter fails because the page was NOT uptodate, then we will
 * do a thread based blocking retry of the operation. That's the unexpected
 * slow path.
 */
static int io_async_buf_func(struct wait_queue_entry *wait, unsigned mode,
			     int sync, void *arg)
{
	struct wait_page_queue *wpq;
	struct io_kiocb *req = wait->private;
	struct wait_page_key *key = arg;
	int ret;

	wpq = container_of(wait, struct wait_page_queue, wait);

	if (!wake_page_match(wpq, key))
		return 0;

	req->rw.kiocb.ki_flags &= ~IOCB_WAITQ;
	list_del_init(&wait->entry);

	init_task_work(&req->task_work, io_req_task_submit);
	percpu_ref_get(&req->ctx->refs);

	/* submit ref gets dropped, acquire a new one */
	refcount_inc(&req->refs);
	ret = io_req_task_work_add(req, true);
	if (unlikely(ret)) {
		struct task_struct *tsk;

		/* queue just for cancelation */
		init_task_work(&req->task_work, io_req_task_cancel);
		tsk = io_wq_get_task(req->ctx->io_wq);
		task_work_add(tsk, &req->task_work, TWA_NONE);
		wake_up_process(tsk);
	}
	return 1;
}

/*
 * This controls whether a given IO request should be armed for async page
 * based retry. If we return false here, the request is handed to the async
 * worker threads for retry. If we're doing buffered reads on a regular file,
 * we prepare a private wait_page_queue entry and retry the operation. This
 * will either succeed because the page is now uptodate and unlocked, or it
 * will register a callback when the page is unlocked at IO completion. Through
 * that callback, io_uring uses task_work to setup a retry of the operation.
 * That retry will attempt the buffered read again. The retry will generally
 * succeed, or in rare cases where it fails, we then fall back to using the
 * async worker threads for a blocking retry.
 */
static bool io_rw_should_retry(struct io_kiocb *req)
{
	struct io_async_rw *rw = req->async_data;
	struct wait_page_queue *wait = &rw->wpq;
	struct kiocb *kiocb = &req->rw.kiocb;

	/* never retry for NOWAIT, we just complete with -EAGAIN */
	if (req->flags & REQ_F_NOWAIT)
		return false;

	/* Only for buffered IO */
	if (kiocb->ki_flags & (IOCB_DIRECT | IOCB_HIPRI))
		return false;

	/*
	 * just use poll if we can, and don't attempt if the fs doesn't
	 * support callback based unlocks
	 */
	if (file_can_poll(req->file) || !(req->file->f_mode & FMODE_BUF_RASYNC))
		return false;

	wait->wait.func = io_async_buf_func;
	wait->wait.private = req;
	wait->wait.flags = 0;
	INIT_LIST_HEAD(&wait->wait.entry);
	kiocb->ki_flags |= IOCB_WAITQ;
	kiocb->ki_flags &= ~IOCB_NOWAIT;
	kiocb->ki_waitq = wait;
	return true;
}

static int io_iter_do_read(struct io_kiocb *req, struct iov_iter *iter)
{
	if (req->file->f_op->read_iter)
		return call_read_iter(req->file, &req->rw.kiocb, iter);
	else if (req->file->f_op->read)
		return loop_rw_iter(READ, req, iter);
	else
		return -EINVAL;
}

static int io_read(struct io_kiocb *req, bool force_nonblock,
		   struct io_comp_state *cs)
{
	struct iovec inline_vecs[UIO_FASTIOV], *iovec = inline_vecs;
	struct kiocb *kiocb = &req->rw.kiocb;
	struct iov_iter __iter, *iter = &__iter;
	struct io_async_rw *rw = req->async_data;
	ssize_t io_size, ret, ret2;
	size_t iov_count;
	bool no_async;

	if (rw)
		iter = &rw->iter;

	ret = io_import_iovec(READ, req, &iovec, iter, !force_nonblock);
	if (ret < 0)
		return ret;
	iov_count = iov_iter_count(iter);
	io_size = ret;
	req->result = io_size;
	ret = 0;

	/* Ensure we clear previously set non-block flag */
	if (!force_nonblock)
		kiocb->ki_flags &= ~IOCB_NOWAIT;
	else
		kiocb->ki_flags |= IOCB_NOWAIT;


	/* If the file doesn't support async, just async punt */
	no_async = force_nonblock && !io_file_supports_async(req->file, READ);
	if (no_async)
		goto copy_iov;

	ret = rw_verify_area(READ, req->file, io_kiocb_ppos(kiocb), iov_count);
	if (unlikely(ret))
		goto out_free;

	ret = io_iter_do_read(req, iter);

	if (!ret) {
		goto done;
	} else if (ret == -EIOCBQUEUED) {
		ret = 0;
		goto out_free;
	} else if (ret == -EAGAIN) {
		/* IOPOLL retry should happen for io-wq threads */
		if (!force_nonblock && !(req->ctx->flags & IORING_SETUP_IOPOLL))
			goto done;
		/* no retry on NONBLOCK marked file */
		if (req->file->f_flags & O_NONBLOCK)
			goto done;
		/* some cases will consume bytes even on error returns */
		iov_iter_revert(iter, iov_count - iov_iter_count(iter));
		ret = 0;
		goto copy_iov;
	} else if (ret < 0) {
		/* make sure -ERESTARTSYS -> -EINTR is done */
		goto done;
	}

	/* read it all, or we did blocking attempt. no retry. */
	if (!iov_iter_count(iter) || !force_nonblock ||
	    (req->file->f_flags & O_NONBLOCK))
		goto done;

	io_size -= ret;
copy_iov:
	ret2 = io_setup_async_rw(req, iovec, inline_vecs, iter, true);
	if (ret2) {
		ret = ret2;
		goto out_free;
	}
	if (no_async)
		return -EAGAIN;
	rw = req->async_data;
	/* it's copied and will be cleaned with ->io */
	iovec = NULL;
	/* now use our persistent iterator, if we aren't already */
	iter = &rw->iter;
retry:
	rw->bytes_done += ret;
	/* if we can retry, do so with the callbacks armed */
	if (!io_rw_should_retry(req)) {
		kiocb->ki_flags &= ~IOCB_WAITQ;
		return -EAGAIN;
	}

	/*
	 * Now retry read with the IOCB_WAITQ parts set in the iocb. If we
	 * get -EIOCBQUEUED, then we'll get a notification when the desired
	 * page gets unlocked. We can also get a partial read here, and if we
	 * do, then just retry at the new offset.
	 */
	ret = io_iter_do_read(req, iter);
	if (ret == -EIOCBQUEUED) {
		ret = 0;
		goto out_free;
	} else if (ret > 0 && ret < io_size) {
		/* we got some bytes, but not all. retry. */
		goto retry;
	}
done:
	kiocb_done(kiocb, ret, cs);
	ret = 0;
out_free:
	/* it's reportedly faster than delegating the null check to kfree() */
	if (iovec)
		kfree(iovec);
	return ret;
}

static int io_write_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	ssize_t ret;

	ret = io_prep_rw(req, sqe);
	if (ret)
		return ret;

	if (unlikely(!(req->file->f_mode & FMODE_WRITE)))
		return -EBADF;

	/* either don't need iovec imported or already have it */
	if (!req->async_data)
		return 0;
	return io_rw_prep_async(req, WRITE);
}

static int io_write(struct io_kiocb *req, bool force_nonblock,
		    struct io_comp_state *cs)
{
	struct iovec inline_vecs[UIO_FASTIOV], *iovec = inline_vecs;
	struct kiocb *kiocb = &req->rw.kiocb;
	struct iov_iter __iter, *iter = &__iter;
	struct io_async_rw *rw = req->async_data;
	size_t iov_count;
	ssize_t ret, ret2, io_size;

	if (rw)
		iter = &rw->iter;

	ret = io_import_iovec(WRITE, req, &iovec, iter, !force_nonblock);
	if (ret < 0)
		return ret;
	iov_count = iov_iter_count(iter);
	io_size = ret;
	req->result = io_size;

	/* Ensure we clear previously set non-block flag */
	if (!force_nonblock)
		kiocb->ki_flags &= ~IOCB_NOWAIT;
	else
		kiocb->ki_flags |= IOCB_NOWAIT;

	/* If the file doesn't support async, just async punt */
	if (force_nonblock && !io_file_supports_async(req->file, WRITE))
		goto copy_iov;

	/* file path doesn't support NOWAIT for non-direct_IO */
	if (force_nonblock && !(kiocb->ki_flags & IOCB_DIRECT) &&
	    (req->flags & REQ_F_ISREG))
		goto copy_iov;

	ret = rw_verify_area(WRITE, req->file, io_kiocb_ppos(kiocb), iov_count);
	if (unlikely(ret))
		goto out_free;

	/*
	 * Open-code file_start_write here to grab freeze protection,
	 * which will be released by another thread in
	 * io_complete_rw().  Fool lockdep by telling it the lock got
	 * released so that it doesn't complain about the held lock when
	 * we return to userspace.
	 */
	if (req->flags & REQ_F_ISREG) {
		sb_start_write(file_inode(req->file)->i_sb);
		__sb_writers_release(file_inode(req->file)->i_sb,
					SB_FREEZE_WRITE);
	}
	kiocb->ki_flags |= IOCB_WRITE;

	if (req->file->f_op->write_iter)
		ret2 = call_write_iter(req->file, kiocb, iter);
	else if (req->file->f_op->write)
		ret2 = loop_rw_iter(WRITE, req, iter);
	else
		ret2 = -EINVAL;

	/*
	 * Raw bdev writes will return -EOPNOTSUPP for IOCB_NOWAIT. Just
	 * retry them without IOCB_NOWAIT.
	 */
	if (ret2 == -EOPNOTSUPP && (kiocb->ki_flags & IOCB_NOWAIT))
		ret2 = -EAGAIN;
	/* no retry on NONBLOCK marked file */
	if (ret2 == -EAGAIN && (req->file->f_flags & O_NONBLOCK))
		goto done;
	if (!force_nonblock || ret2 != -EAGAIN) {
		/* IOPOLL retry should happen for io-wq threads */
		if ((req->ctx->flags & IORING_SETUP_IOPOLL) && ret2 == -EAGAIN)
			goto copy_iov;
done:
		kiocb_done(kiocb, ret2, cs);
	} else {
copy_iov:
		/* some cases will consume bytes even on error returns */
		iov_iter_revert(iter, iov_count - iov_iter_count(iter));
		ret = io_setup_async_rw(req, iovec, inline_vecs, iter, false);
		if (!ret)
			return -EAGAIN;
	}
out_free:
	/* it's reportedly faster than delegating the null check to kfree() */
	if (iovec)
		kfree(iovec);
	return ret;
}

static int __io_splice_prep(struct io_kiocb *req,
			    const struct io_uring_sqe *sqe)
{
	struct io_splice* sp = &req->splice;
	unsigned int valid_flags = SPLICE_F_FD_IN_FIXED | SPLICE_F_ALL;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sp->file_in = NULL;
	sp->len = READ_ONCE(sqe->len);
	sp->flags = READ_ONCE(sqe->splice_flags);

	if (unlikely(sp->flags & ~valid_flags))
		return -EINVAL;

	sp->file_in = io_file_get(NULL, req, READ_ONCE(sqe->splice_fd_in),
				  (sp->flags & SPLICE_F_FD_IN_FIXED));
	if (!sp->file_in)
		return -EBADF;
	req->flags |= REQ_F_NEED_CLEANUP;

	if (!S_ISREG(file_inode(sp->file_in)->i_mode)) {
		/*
		 * Splice operation will be punted aync, and here need to
		 * modify io_wq_work.flags, so initialize io_wq_work firstly.
		 */
		io_req_init_async(req);
		req->work.flags |= IO_WQ_WORK_UNBOUND;
	}

	return 0;
}

static int io_tee_prep(struct io_kiocb *req,
		       const struct io_uring_sqe *sqe)
{
	if (READ_ONCE(sqe->splice_off_in) || READ_ONCE(sqe->off))
		return -EINVAL;
	return __io_splice_prep(req, sqe);
}

static int io_tee(struct io_kiocb *req, bool force_nonblock)
{
	struct io_splice *sp = &req->splice;
	struct file *in = sp->file_in;
	struct file *out = sp->file_out;
	unsigned int flags = sp->flags & ~SPLICE_F_FD_IN_FIXED;
	long ret = 0;

	if (force_nonblock)
		return -EAGAIN;
	if (sp->len)
		ret = do_tee(in, out, sp->len, flags);

	io_put_file(req, in, (sp->flags & SPLICE_F_FD_IN_FIXED));
	req->flags &= ~REQ_F_NEED_CLEANUP;

	if (ret != sp->len)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int io_splice_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_splice* sp = &req->splice;

	sp->off_in = READ_ONCE(sqe->splice_off_in);
	sp->off_out = READ_ONCE(sqe->off);
	return __io_splice_prep(req, sqe);
}

static int io_splice(struct io_kiocb *req, bool force_nonblock)
{
	struct io_splice *sp = &req->splice;
	struct file *in = sp->file_in;
	struct file *out = sp->file_out;
	unsigned int flags = sp->flags & ~SPLICE_F_FD_IN_FIXED;
	loff_t *poff_in, *poff_out;
	long ret = 0;

	if (force_nonblock)
		return -EAGAIN;

	poff_in = (sp->off_in == -1) ? NULL : &sp->off_in;
	poff_out = (sp->off_out == -1) ? NULL : &sp->off_out;

	if (sp->len)
		ret = do_splice(in, poff_in, out, poff_out, sp->len, flags);

	io_put_file(req, in, (sp->flags & SPLICE_F_FD_IN_FIXED));
	req->flags &= ~REQ_F_NEED_CLEANUP;

	if (ret != sp->len)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

/*
 * IORING_OP_NOP just posts a completion event, nothing else.
 */
static int io_nop(struct io_kiocb *req, struct io_comp_state *cs)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (unlikely(ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	__io_req_complete(req, 0, 0, cs);
	return 0;
}

static int io_prep_fsync(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (!req->file)
		return -EBADF;

	if (unlikely(ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (unlikely(sqe->addr || sqe->ioprio || sqe->buf_index))
		return -EINVAL;

	req->sync.flags = READ_ONCE(sqe->fsync_flags);
	if (unlikely(req->sync.flags & ~IORING_FSYNC_DATASYNC))
		return -EINVAL;

	req->sync.off = READ_ONCE(sqe->off);
	req->sync.len = READ_ONCE(sqe->len);
	return 0;
}

static int io_fsync(struct io_kiocb *req, bool force_nonblock)
{
	loff_t end = req->sync.off + req->sync.len;
	int ret;

	/* fsync always requires a blocking context */
	if (force_nonblock)
		return -EAGAIN;

	ret = vfs_fsync_range(req->file, req->sync.off,
				end > 0 ? end : LLONG_MAX,
				req->sync.flags & IORING_FSYNC_DATASYNC);
	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int io_fallocate_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe)
{
	if (sqe->ioprio || sqe->buf_index || sqe->rw_flags)
		return -EINVAL;
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	req->sync.off = READ_ONCE(sqe->off);
	req->sync.len = READ_ONCE(sqe->addr);
	req->sync.mode = READ_ONCE(sqe->len);
	return 0;
}

static int io_fallocate(struct io_kiocb *req, bool force_nonblock)
{
	int ret;

	/* fallocate always requiring blocking context */
	if (force_nonblock)
		return -EAGAIN;
	ret = vfs_fallocate(req->file, req->sync.mode, req->sync.off,
				req->sync.len);
	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int __io_openat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	const char __user *fname;
	int ret;

	if (unlikely(sqe->ioprio || sqe->buf_index))
		return -EINVAL;
	if (unlikely(req->flags & REQ_F_FIXED_FILE))
		return -EBADF;

	/* open.how should be already initialised */
	if (!(req->open.how.flags & O_PATH) && force_o_largefile())
		req->open.how.flags |= O_LARGEFILE;

	req->open.dfd = READ_ONCE(sqe->fd);
	fname = u64_to_user_ptr(READ_ONCE(sqe->addr));
	req->open.filename = getname(fname);
	if (IS_ERR(req->open.filename)) {
		ret = PTR_ERR(req->open.filename);
		req->open.filename = NULL;
		return ret;
	}
	req->open.nofile = rlimit(RLIMIT_NOFILE);
	req->open.ignore_nonblock = false;
	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

static int io_openat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	u64 flags, mode;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	mode = READ_ONCE(sqe->len);
	flags = READ_ONCE(sqe->open_flags);
	req->open.how = build_open_how(flags, mode);
	return __io_openat_prep(req, sqe);
}

static int io_openat2_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct open_how __user *how;
	size_t len;
	int ret;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	how = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	len = READ_ONCE(sqe->len);
	if (len < OPEN_HOW_SIZE_VER0)
		return -EINVAL;

	ret = copy_struct_from_user(&req->open.how, sizeof(req->open.how), how,
					len);
	if (ret)
		return ret;

	return __io_openat_prep(req, sqe);
}

static int io_openat2(struct io_kiocb *req, bool force_nonblock)
{
	struct open_flags op;
	struct file *file;
	int ret;

	if (force_nonblock && !req->open.ignore_nonblock)
		return -EAGAIN;

	ret = build_open_flags(&req->open.how, &op);
	if (ret)
		goto err;

	ret = __get_unused_fd_flags(req->open.how.flags, req->open.nofile);
	if (ret < 0)
		goto err;

	file = do_filp_open(req->open.dfd, req->open.filename, &op);
	if (IS_ERR(file)) {
		put_unused_fd(ret);
		ret = PTR_ERR(file);
		/*
		 * A work-around to ensure that /proc/self works that way
		 * that it should - if we get -EOPNOTSUPP back, then assume
		 * that proc_self_get_link() failed us because we're in async
		 * context. We should be safe to retry this from the task
		 * itself with force_nonblock == false set, as it should not
		 * block on lookup. Would be nice to know this upfront and
		 * avoid the async dance, but doesn't seem feasible.
		 */
		if (ret == -EOPNOTSUPP && io_wq_current_is_worker()) {
			req->open.ignore_nonblock = true;
			refcount_inc(&req->refs);
			io_req_task_queue(req);
			return 0;
		}
	} else {
		fsnotify_open(file);
		fd_install(ret, file);
	}
err:
	putname(req->open.filename);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int io_openat(struct io_kiocb *req, bool force_nonblock)
{
	return io_openat2(req, force_nonblock);
}

static int io_remove_buffers_prep(struct io_kiocb *req,
				  const struct io_uring_sqe *sqe)
{
	struct io_provide_buf *p = &req->pbuf;
	u64 tmp;

	if (sqe->ioprio || sqe->rw_flags || sqe->addr || sqe->len || sqe->off)
		return -EINVAL;

	tmp = READ_ONCE(sqe->fd);
	if (!tmp || tmp > USHRT_MAX)
		return -EINVAL;

	memset(p, 0, sizeof(*p));
	p->nbufs = tmp;
	p->bgid = READ_ONCE(sqe->buf_group);
	return 0;
}

static int __io_remove_buffers(struct io_ring_ctx *ctx, struct io_buffer *buf,
			       int bgid, unsigned nbufs)
{
	unsigned i = 0;

	/* shouldn't happen */
	if (!nbufs)
		return 0;

	/* the head kbuf is the list itself */
	while (!list_empty(&buf->list)) {
		struct io_buffer *nxt;

		nxt = list_first_entry(&buf->list, struct io_buffer, list);
		list_del(&nxt->list);
		kfree(nxt);
		if (++i == nbufs)
			return i;
	}
	i++;
	kfree(buf);
	idr_remove(&ctx->io_buffer_idr, bgid);

	return i;
}

static int io_remove_buffers(struct io_kiocb *req, bool force_nonblock,
			     struct io_comp_state *cs)
{
	struct io_provide_buf *p = &req->pbuf;
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer *head;
	int ret = 0;

	io_ring_submit_lock(ctx, !force_nonblock);

	lockdep_assert_held(&ctx->uring_lock);

	ret = -ENOENT;
	head = idr_find(&ctx->io_buffer_idr, p->bgid);
	if (head)
		ret = __io_remove_buffers(ctx, head, p->bgid, p->nbufs);
	if (ret < 0)
		req_set_fail_links(req);

	/* need to hold the lock to complete IOPOLL requests */
	if (ctx->flags & IORING_SETUP_IOPOLL) {
		__io_req_complete(req, ret, 0, cs);
		io_ring_submit_unlock(ctx, !force_nonblock);
	} else {
		io_ring_submit_unlock(ctx, !force_nonblock);
		__io_req_complete(req, ret, 0, cs);
	}
	return 0;
}

static int io_provide_buffers_prep(struct io_kiocb *req,
				   const struct io_uring_sqe *sqe)
{
	struct io_provide_buf *p = &req->pbuf;
	u64 tmp;

	if (sqe->ioprio || sqe->rw_flags)
		return -EINVAL;

	tmp = READ_ONCE(sqe->fd);
	if (!tmp || tmp > USHRT_MAX)
		return -E2BIG;
	p->nbufs = tmp;
	p->addr = READ_ONCE(sqe->addr);
	p->len = READ_ONCE(sqe->len);

	if (!access_ok(u64_to_user_ptr(p->addr), (p->len * p->nbufs)))
		return -EFAULT;

	p->bgid = READ_ONCE(sqe->buf_group);
	tmp = READ_ONCE(sqe->off);
	if (tmp > USHRT_MAX)
		return -E2BIG;
	p->bid = tmp;
	return 0;
}

static int io_add_buffers(struct io_provide_buf *pbuf, struct io_buffer **head)
{
	struct io_buffer *buf;
	u64 addr = pbuf->addr;
	int i, bid = pbuf->bid;

	for (i = 0; i < pbuf->nbufs; i++) {
		buf = kmalloc(sizeof(*buf), GFP_KERNEL);
		if (!buf)
			break;

		buf->addr = addr;
		buf->len = pbuf->len;
		buf->bid = bid;
		addr += pbuf->len;
		bid++;
		if (!*head) {
			INIT_LIST_HEAD(&buf->list);
			*head = buf;
		} else {
			list_add_tail(&buf->list, &(*head)->list);
		}
	}

	return i ? i : -ENOMEM;
}

static int io_provide_buffers(struct io_kiocb *req, bool force_nonblock,
			      struct io_comp_state *cs)
{
	struct io_provide_buf *p = &req->pbuf;
	struct io_ring_ctx *ctx = req->ctx;
	struct io_buffer *head, *list;
	int ret = 0;

	io_ring_submit_lock(ctx, !force_nonblock);

	lockdep_assert_held(&ctx->uring_lock);

	list = head = idr_find(&ctx->io_buffer_idr, p->bgid);

	ret = io_add_buffers(p, &head);
	if (ret < 0)
		goto out;

	if (!list) {
		ret = idr_alloc(&ctx->io_buffer_idr, head, p->bgid, p->bgid + 1,
					GFP_KERNEL);
		if (ret < 0) {
			__io_remove_buffers(ctx, head, p->bgid, -1U);
			goto out;
		}
	}
out:
	if (ret < 0)
		req_set_fail_links(req);

	/* need to hold the lock to complete IOPOLL requests */
	if (ctx->flags & IORING_SETUP_IOPOLL) {
		__io_req_complete(req, ret, 0, cs);
		io_ring_submit_unlock(ctx, !force_nonblock);
	} else {
		io_ring_submit_unlock(ctx, !force_nonblock);
		__io_req_complete(req, ret, 0, cs);
	}
	return 0;
}

static int io_epoll_ctl_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_EPOLL)
	if (sqe->ioprio || sqe->buf_index)
		return -EINVAL;
	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL)))
		return -EINVAL;

	req->epoll.epfd = READ_ONCE(sqe->fd);
	req->epoll.op = READ_ONCE(sqe->len);
	req->epoll.fd = READ_ONCE(sqe->off);

	if (ep_op_has_event(req->epoll.op)) {
		struct epoll_event __user *ev;

		ev = u64_to_user_ptr(READ_ONCE(sqe->addr));
		if (copy_from_user(&req->epoll.event, ev, sizeof(*ev)))
			return -EFAULT;
	}

	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_epoll_ctl(struct io_kiocb *req, bool force_nonblock,
			struct io_comp_state *cs)
{
#if defined(CONFIG_EPOLL)
	struct io_epoll *ie = &req->epoll;
	int ret;

	ret = do_epoll_ctl(ie->epfd, ie->op, ie->fd, &ie->event, force_nonblock);
	if (force_nonblock && ret == -EAGAIN)
		return -EAGAIN;

	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, 0, cs);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_madvise_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_ADVISE_SYSCALLS) && defined(CONFIG_MMU)
	if (sqe->ioprio || sqe->buf_index || sqe->off)
		return -EINVAL;
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	req->madvise.addr = READ_ONCE(sqe->addr);
	req->madvise.len = READ_ONCE(sqe->len);
	req->madvise.advice = READ_ONCE(sqe->fadvise_advice);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_madvise(struct io_kiocb *req, bool force_nonblock)
{
#if defined(CONFIG_ADVISE_SYSCALLS) && defined(CONFIG_MMU)
	struct io_madvise *ma = &req->madvise;
	int ret;

	if (force_nonblock)
		return -EAGAIN;

	ret = do_madvise(current->mm, ma->addr, ma->len, ma->advice);
	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_fadvise_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	if (sqe->ioprio || sqe->buf_index || sqe->addr)
		return -EINVAL;
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	req->fadvise.offset = READ_ONCE(sqe->off);
	req->fadvise.len = READ_ONCE(sqe->len);
	req->fadvise.advice = READ_ONCE(sqe->fadvise_advice);
	return 0;
}

static int io_fadvise(struct io_kiocb *req, bool force_nonblock)
{
	struct io_fadvise *fa = &req->fadvise;
	int ret;

	if (force_nonblock) {
		switch (fa->advice) {
		case POSIX_FADV_NORMAL:
		case POSIX_FADV_RANDOM:
		case POSIX_FADV_SEQUENTIAL:
			break;
		default:
			return -EAGAIN;
		}
	}

	ret = vfs_fadvise(req->file, fa->offset, fa->len, fa->advice);
	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int io_statx_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL)))
		return -EINVAL;
	if (sqe->ioprio || sqe->buf_index)
		return -EINVAL;
	if (req->flags & REQ_F_FIXED_FILE)
		return -EBADF;

	req->statx.dfd = READ_ONCE(sqe->fd);
	req->statx.mask = READ_ONCE(sqe->len);
	req->statx.filename = u64_to_user_ptr(READ_ONCE(sqe->addr));
	req->statx.buffer = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	req->statx.flags = READ_ONCE(sqe->statx_flags);

	return 0;
}

static int io_statx(struct io_kiocb *req, bool force_nonblock)
{
	struct io_statx *ctx = &req->statx;
	int ret;

	if (force_nonblock) {
		/* only need file table for an actual valid fd */
		if (ctx->dfd == -1 || ctx->dfd == AT_FDCWD)
			req->flags |= REQ_F_NO_FILE_TABLE;
		return -EAGAIN;
	}

	ret = do_statx(ctx->dfd, ctx->filename, ctx->flags, ctx->mask,
		       ctx->buffer);

	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int io_close_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	/*
	 * If we queue this for async, it must not be cancellable. That would
	 * leave the 'file' in an undeterminate state, and here need to modify
	 * io_wq_work.flags, so initialize io_wq_work firstly.
	 */
	io_req_init_async(req);
	req->work.flags |= IO_WQ_WORK_NO_CANCEL;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	if (sqe->ioprio || sqe->off || sqe->addr || sqe->len ||
	    sqe->rw_flags || sqe->buf_index)
		return -EINVAL;
	if (req->flags & REQ_F_FIXED_FILE)
		return -EBADF;

	req->close.fd = READ_ONCE(sqe->fd);
	if ((req->file && req->file->f_op == &io_uring_fops))
		return -EBADF;

	req->close.put_file = NULL;
	return 0;
}

static int io_close(struct io_kiocb *req, bool force_nonblock,
		    struct io_comp_state *cs)
{
	struct io_close *close = &req->close;
	int ret;

	/* might be already done during nonblock submission */
	if (!close->put_file) {
		ret = __close_fd_get_file(close->fd, &close->put_file);
		if (ret < 0)
			return (ret == -ENOENT) ? -EBADF : ret;
	}

	/* if the file has a flush method, be safe and punt to async */
	if (close->put_file->f_op->flush && force_nonblock) {
		/* was never set, but play safe */
		req->flags &= ~REQ_F_NOWAIT;
		/* avoid grabbing files - we don't need the files */
		req->flags |= REQ_F_NO_FILE_TABLE;
		return -EAGAIN;
	}

	/* No ->flush() or already async, safely close from here */
	ret = filp_close(close->put_file, req->work.identity->files);
	if (ret < 0)
		req_set_fail_links(req);
	fput(close->put_file);
	close->put_file = NULL;
	__io_req_complete(req, ret, 0, cs);
	return 0;
}

static int io_prep_sfr(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (!req->file)
		return -EBADF;

	if (unlikely(ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (unlikely(sqe->addr || sqe->ioprio || sqe->buf_index))
		return -EINVAL;

	req->sync.off = READ_ONCE(sqe->off);
	req->sync.len = READ_ONCE(sqe->len);
	req->sync.flags = READ_ONCE(sqe->sync_range_flags);
	return 0;
}

static int io_sync_file_range(struct io_kiocb *req, bool force_nonblock)
{
	int ret;

	/* sync_file_range always requires a blocking context */
	if (force_nonblock)
		return -EAGAIN;

	ret = sync_file_range(req->file, req->sync.off, req->sync.len,
				req->sync.flags);
	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

#if defined(CONFIG_NET)
static int io_setup_async_msg(struct io_kiocb *req,
			      struct io_async_msghdr *kmsg)
{
	struct io_async_msghdr *async_msg = req->async_data;

	if (async_msg)
		return -EAGAIN;
	if (io_alloc_async_data(req)) {
		if (kmsg->iov != kmsg->fast_iov)
			kfree(kmsg->iov);
		return -ENOMEM;
	}
	async_msg = req->async_data;
	req->flags |= REQ_F_NEED_CLEANUP;
	memcpy(async_msg, kmsg, sizeof(*kmsg));
	return -EAGAIN;
}

static int io_sendmsg_copy_hdr(struct io_kiocb *req,
			       struct io_async_msghdr *iomsg)
{
	iomsg->iov = iomsg->fast_iov;
	iomsg->msg.msg_name = &iomsg->addr;
	return sendmsg_copy_msghdr(&iomsg->msg, req->sr_msg.umsg,
				   req->sr_msg.msg_flags, &iomsg->iov);
}

static int io_sendmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_async_msghdr *async_msg = req->async_data;
	struct io_sr_msg *sr = &req->sr_msg;
	int ret;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sr->msg_flags = READ_ONCE(sqe->msg_flags);
	sr->umsg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sr->len = READ_ONCE(sqe->len);

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		sr->msg_flags |= MSG_CMSG_COMPAT;
#endif

	if (!async_msg || !io_op_defs[req->opcode].needs_async_data)
		return 0;
	ret = io_sendmsg_copy_hdr(req, async_msg);
	if (!ret)
		req->flags |= REQ_F_NEED_CLEANUP;
	return ret;
}

static int io_sendmsg(struct io_kiocb *req, bool force_nonblock,
		      struct io_comp_state *cs)
{
	struct io_async_msghdr iomsg, *kmsg;
	struct socket *sock;
	unsigned flags;
	int ret;

	sock = sock_from_file(req->file, &ret);
	if (unlikely(!sock))
		return ret;

	if (req->async_data) {
		kmsg = req->async_data;
		kmsg->msg.msg_name = &kmsg->addr;
		/* if iov is set, it's allocated already */
		if (!kmsg->iov)
			kmsg->iov = kmsg->fast_iov;
		kmsg->msg.msg_iter.iov = kmsg->iov;
	} else {
		ret = io_sendmsg_copy_hdr(req, &iomsg);
		if (ret)
			return ret;
		kmsg = &iomsg;
	}

	flags = req->sr_msg.msg_flags;
	if (flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;
	else if (force_nonblock)
		flags |= MSG_DONTWAIT;

	ret = __sys_sendmsg_sock(sock, &kmsg->msg, flags);
	if (force_nonblock && ret == -EAGAIN)
		return io_setup_async_msg(req, kmsg);
	if (ret == -ERESTARTSYS)
		ret = -EINTR;

	if (kmsg->iov != kmsg->fast_iov)
		kfree(kmsg->iov);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, 0, cs);
	return 0;
}

static int io_send(struct io_kiocb *req, bool force_nonblock,
		   struct io_comp_state *cs)
{
	struct io_sr_msg *sr = &req->sr_msg;
	struct msghdr msg;
	struct iovec iov;
	struct socket *sock;
	unsigned flags;
	int ret;

	sock = sock_from_file(req->file, &ret);
	if (unlikely(!sock))
		return ret;

	ret = import_single_range(WRITE, sr->buf, sr->len, &iov, &msg.msg_iter);
	if (unlikely(ret))
		return ret;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;

	flags = req->sr_msg.msg_flags;
	if (flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;
	else if (force_nonblock)
		flags |= MSG_DONTWAIT;

	msg.msg_flags = flags;
	ret = sock_sendmsg(sock, &msg);
	if (force_nonblock && ret == -EAGAIN)
		return -EAGAIN;
	if (ret == -ERESTARTSYS)
		ret = -EINTR;

	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, 0, cs);
	return 0;
}

static int __io_recvmsg_copy_hdr(struct io_kiocb *req,
				 struct io_async_msghdr *iomsg)
{
	struct io_sr_msg *sr = &req->sr_msg;
	struct iovec __user *uiov;
	size_t iov_len;
	int ret;

	ret = __copy_msghdr_from_user(&iomsg->msg, sr->umsg,
					&iomsg->uaddr, &uiov, &iov_len);
	if (ret)
		return ret;

	if (req->flags & REQ_F_BUFFER_SELECT) {
		if (iov_len > 1)
			return -EINVAL;
		if (copy_from_user(iomsg->iov, uiov, sizeof(*uiov)))
			return -EFAULT;
		sr->len = iomsg->iov[0].iov_len;
		iov_iter_init(&iomsg->msg.msg_iter, READ, iomsg->iov, 1,
				sr->len);
		iomsg->iov = NULL;
	} else {
		ret = __import_iovec(READ, uiov, iov_len, UIO_FASTIOV,
				     &iomsg->iov, &iomsg->msg.msg_iter,
				     false);
		if (ret > 0)
			ret = 0;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static int __io_compat_recvmsg_copy_hdr(struct io_kiocb *req,
					struct io_async_msghdr *iomsg)
{
	struct compat_msghdr __user *msg_compat;
	struct io_sr_msg *sr = &req->sr_msg;
	struct compat_iovec __user *uiov;
	compat_uptr_t ptr;
	compat_size_t len;
	int ret;

	msg_compat = (struct compat_msghdr __user *) sr->umsg;
	ret = __get_compat_msghdr(&iomsg->msg, msg_compat, &iomsg->uaddr,
					&ptr, &len);
	if (ret)
		return ret;

	uiov = compat_ptr(ptr);
	if (req->flags & REQ_F_BUFFER_SELECT) {
		compat_ssize_t clen;

		if (len > 1)
			return -EINVAL;
		if (!access_ok(uiov, sizeof(*uiov)))
			return -EFAULT;
		if (__get_user(clen, &uiov->iov_len))
			return -EFAULT;
		if (clen < 0)
			return -EINVAL;
		sr->len = clen;
		iomsg->iov[0].iov_len = clen;
		iomsg->iov = NULL;
	} else {
		ret = __import_iovec(READ, (struct iovec __user *)uiov, len,
				   UIO_FASTIOV, &iomsg->iov,
				   &iomsg->msg.msg_iter, true);
		if (ret < 0)
			return ret;
	}

	return 0;
}
#endif

static int io_recvmsg_copy_hdr(struct io_kiocb *req,
			       struct io_async_msghdr *iomsg)
{
	iomsg->msg.msg_name = &iomsg->addr;
	iomsg->iov = iomsg->fast_iov;

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		return __io_compat_recvmsg_copy_hdr(req, iomsg);
#endif

	return __io_recvmsg_copy_hdr(req, iomsg);
}

static struct io_buffer *io_recv_buffer_select(struct io_kiocb *req,
					       bool needs_lock)
{
	struct io_sr_msg *sr = &req->sr_msg;
	struct io_buffer *kbuf;

	kbuf = io_buffer_select(req, &sr->len, sr->bgid, sr->kbuf, needs_lock);
	if (IS_ERR(kbuf))
		return kbuf;

	sr->kbuf = kbuf;
	req->flags |= REQ_F_BUFFER_SELECTED;
	return kbuf;
}

static inline unsigned int io_put_recv_kbuf(struct io_kiocb *req)
{
	return io_put_kbuf(req, req->sr_msg.kbuf);
}

static int io_recvmsg_prep(struct io_kiocb *req,
			   const struct io_uring_sqe *sqe)
{
	struct io_async_msghdr *async_msg = req->async_data;
	struct io_sr_msg *sr = &req->sr_msg;
	int ret;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sr->msg_flags = READ_ONCE(sqe->msg_flags);
	sr->umsg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sr->len = READ_ONCE(sqe->len);
	sr->bgid = READ_ONCE(sqe->buf_group);

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		sr->msg_flags |= MSG_CMSG_COMPAT;
#endif

	if (!async_msg || !io_op_defs[req->opcode].needs_async_data)
		return 0;
	ret = io_recvmsg_copy_hdr(req, async_msg);
	if (!ret)
		req->flags |= REQ_F_NEED_CLEANUP;
	return ret;
}

static int io_recvmsg(struct io_kiocb *req, bool force_nonblock,
		      struct io_comp_state *cs)
{
	struct io_async_msghdr iomsg, *kmsg;
	struct socket *sock;
	struct io_buffer *kbuf;
	unsigned flags;
	int ret, cflags = 0;

	sock = sock_from_file(req->file, &ret);
	if (unlikely(!sock))
		return ret;

	if (req->async_data) {
		kmsg = req->async_data;
		kmsg->msg.msg_name = &kmsg->addr;
		/* if iov is set, it's allocated already */
		if (!kmsg->iov)
			kmsg->iov = kmsg->fast_iov;
		kmsg->msg.msg_iter.iov = kmsg->iov;
	} else {
		ret = io_recvmsg_copy_hdr(req, &iomsg);
		if (ret)
			return ret;
		kmsg = &iomsg;
	}

	if (req->flags & REQ_F_BUFFER_SELECT) {
		kbuf = io_recv_buffer_select(req, !force_nonblock);
		if (IS_ERR(kbuf))
			return PTR_ERR(kbuf);
		kmsg->fast_iov[0].iov_base = u64_to_user_ptr(kbuf->addr);
		iov_iter_init(&kmsg->msg.msg_iter, READ, kmsg->iov,
				1, req->sr_msg.len);
	}

	flags = req->sr_msg.msg_flags;
	if (flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;
	else if (force_nonblock)
		flags |= MSG_DONTWAIT;

	ret = __sys_recvmsg_sock(sock, &kmsg->msg, req->sr_msg.umsg,
					kmsg->uaddr, flags);
	if (force_nonblock && ret == -EAGAIN)
		return io_setup_async_msg(req, kmsg);
	if (ret == -ERESTARTSYS)
		ret = -EINTR;

	if (req->flags & REQ_F_BUFFER_SELECTED)
		cflags = io_put_recv_kbuf(req);
	if (kmsg->iov != kmsg->fast_iov)
		kfree(kmsg->iov);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, cflags, cs);
	return 0;
}

static int io_recv(struct io_kiocb *req, bool force_nonblock,
		   struct io_comp_state *cs)
{
	struct io_buffer *kbuf;
	struct io_sr_msg *sr = &req->sr_msg;
	struct msghdr msg;
	void __user *buf = sr->buf;
	struct socket *sock;
	struct iovec iov;
	unsigned flags;
	int ret, cflags = 0;

	sock = sock_from_file(req->file, &ret);
	if (unlikely(!sock))
		return ret;

	if (req->flags & REQ_F_BUFFER_SELECT) {
		kbuf = io_recv_buffer_select(req, !force_nonblock);
		if (IS_ERR(kbuf))
			return PTR_ERR(kbuf);
		buf = u64_to_user_ptr(kbuf->addr);
	}

	ret = import_single_range(READ, buf, sr->len, &iov, &msg.msg_iter);
	if (unlikely(ret))
		goto out_free;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	msg.msg_iocb = NULL;
	msg.msg_flags = 0;

	flags = req->sr_msg.msg_flags;
	if (flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;
	else if (force_nonblock)
		flags |= MSG_DONTWAIT;

	ret = sock_recvmsg(sock, &msg, flags);
	if (force_nonblock && ret == -EAGAIN)
		return -EAGAIN;
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
out_free:
	if (req->flags & REQ_F_BUFFER_SELECTED)
		cflags = io_put_recv_kbuf(req);
	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, cflags, cs);
	return 0;
}

static int io_accept_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_accept *accept = &req->accept;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	if (sqe->ioprio || sqe->len || sqe->buf_index)
		return -EINVAL;

	accept->addr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	accept->addr_len = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	accept->flags = READ_ONCE(sqe->accept_flags);
	accept->nofile = rlimit(RLIMIT_NOFILE);
	return 0;
}

static int io_accept(struct io_kiocb *req, bool force_nonblock,
		     struct io_comp_state *cs)
{
	struct io_accept *accept = &req->accept;
	unsigned int file_flags = force_nonblock ? O_NONBLOCK : 0;
	int ret;

	if (req->file->f_flags & O_NONBLOCK)
		req->flags |= REQ_F_NOWAIT;

	ret = __sys_accept4_file(req->file, file_flags, accept->addr,
					accept->addr_len, accept->flags,
					accept->nofile);
	if (ret == -EAGAIN && force_nonblock)
		return -EAGAIN;
	if (ret < 0) {
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail_links(req);
	}
	__io_req_complete(req, ret, 0, cs);
	return 0;
}

static int io_connect_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_connect *conn = &req->connect;
	struct io_async_connect *io = req->async_data;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	if (sqe->ioprio || sqe->len || sqe->buf_index || sqe->rw_flags)
		return -EINVAL;

	conn->addr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	conn->addr_len =  READ_ONCE(sqe->addr2);

	if (!io)
		return 0;

	return move_addr_to_kernel(conn->addr, conn->addr_len,
					&io->address);
}

static int io_connect(struct io_kiocb *req, bool force_nonblock,
		      struct io_comp_state *cs)
{
	struct io_async_connect __io, *io;
	unsigned file_flags;
	int ret;

	if (req->async_data) {
		io = req->async_data;
	} else {
		ret = move_addr_to_kernel(req->connect.addr,
						req->connect.addr_len,
						&__io.address);
		if (ret)
			goto out;
		io = &__io;
	}

	file_flags = force_nonblock ? O_NONBLOCK : 0;

	ret = __sys_connect_file(req->file, &io->address,
					req->connect.addr_len, file_flags);
	if ((ret == -EAGAIN || ret == -EINPROGRESS) && force_nonblock) {
		if (req->async_data)
			return -EAGAIN;
		if (io_alloc_async_data(req)) {
			ret = -ENOMEM;
			goto out;
		}
		io = req->async_data;
		memcpy(req->async_data, &__io, sizeof(__io));
		return -EAGAIN;
	}
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
out:
	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, 0, cs);
	return 0;
}
#else /* !CONFIG_NET */
static int io_sendmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return -EOPNOTSUPP;
}

static int io_sendmsg(struct io_kiocb *req, bool force_nonblock,
		      struct io_comp_state *cs)
{
	return -EOPNOTSUPP;
}

static int io_send(struct io_kiocb *req, bool force_nonblock,
		   struct io_comp_state *cs)
{
	return -EOPNOTSUPP;
}

static int io_recvmsg_prep(struct io_kiocb *req,
			   const struct io_uring_sqe *sqe)
{
	return -EOPNOTSUPP;
}

static int io_recvmsg(struct io_kiocb *req, bool force_nonblock,
		      struct io_comp_state *cs)
{
	return -EOPNOTSUPP;
}

static int io_recv(struct io_kiocb *req, bool force_nonblock,
		   struct io_comp_state *cs)
{
	return -EOPNOTSUPP;
}

static int io_accept_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return -EOPNOTSUPP;
}

static int io_accept(struct io_kiocb *req, bool force_nonblock,
		     struct io_comp_state *cs)
{
	return -EOPNOTSUPP;
}

static int io_connect_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	return -EOPNOTSUPP;
}

static int io_connect(struct io_kiocb *req, bool force_nonblock,
		      struct io_comp_state *cs)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_NET */

struct io_poll_table {
	struct poll_table_struct pt;
	struct io_kiocb *req;
	int error;
};

static int __io_async_wake(struct io_kiocb *req, struct io_poll_iocb *poll,
			   __poll_t mask, task_work_func_t func)
{
	bool twa_signal_ok;
	int ret;

	/* for instances that support it check for an event match first: */
	if (mask && !(mask & poll->events))
		return 0;

	trace_io_uring_task_add(req->ctx, req->opcode, req->user_data, mask);

	list_del_init(&poll->wait.entry);

	req->result = mask;
	init_task_work(&req->task_work, func);
	percpu_ref_get(&req->ctx->refs);

	/*
	 * If we using the signalfd wait_queue_head for this wakeup, then
	 * it's not safe to use TWA_SIGNAL as we could be recursing on the
	 * tsk->sighand->siglock on doing the wakeup. Should not be needed
	 * either, as the normal wakeup will suffice.
	 */
	twa_signal_ok = (poll->head != &req->task->sighand->signalfd_wqh);

	/*
	 * If this fails, then the task is exiting. When a task exits, the
	 * work gets canceled, so just cancel this request as well instead
	 * of executing it. We can't safely execute it anyway, as we may not
	 * have the needed state needed for it anyway.
	 */
	ret = io_req_task_work_add(req, twa_signal_ok);
	if (unlikely(ret)) {
		struct task_struct *tsk;

		WRITE_ONCE(poll->canceled, true);
		tsk = io_wq_get_task(req->ctx->io_wq);
		task_work_add(tsk, &req->task_work, TWA_NONE);
		wake_up_process(tsk);
	}
	return 1;
}

static bool io_poll_rewait(struct io_kiocb *req, struct io_poll_iocb *poll)
	__acquires(&req->ctx->completion_lock)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (!req->result && !READ_ONCE(poll->canceled)) {
		struct poll_table_struct pt = { ._key = poll->events };

		req->result = vfs_poll(req->file, &pt) & poll->events;
	}

	spin_lock_irq(&ctx->completion_lock);
	if (!req->result && !READ_ONCE(poll->canceled)) {
		add_wait_queue(poll->head, &poll->wait);
		return true;
	}

	return false;
}

static struct io_poll_iocb *io_poll_get_double(struct io_kiocb *req)
{
	/* pure poll stashes this in ->async_data, poll driven retry elsewhere */
	if (req->opcode == IORING_OP_POLL_ADD)
		return req->async_data;
	return req->apoll->double_poll;
}

static struct io_poll_iocb *io_poll_get_single(struct io_kiocb *req)
{
	if (req->opcode == IORING_OP_POLL_ADD)
		return &req->poll;
	return &req->apoll->poll;
}

static void io_poll_remove_double(struct io_kiocb *req)
{
	struct io_poll_iocb *poll = io_poll_get_double(req);

	lockdep_assert_held(&req->ctx->completion_lock);

	if (poll && poll->head) {
		struct wait_queue_head *head = poll->head;

		spin_lock(&head->lock);
		list_del_init(&poll->wait.entry);
		if (poll->wait.private)
			refcount_dec(&req->refs);
		poll->head = NULL;
		spin_unlock(&head->lock);
	}
}

static void io_poll_complete(struct io_kiocb *req, __poll_t mask, int error)
{
	struct io_ring_ctx *ctx = req->ctx;

	io_poll_remove_double(req);
	req->poll.done = true;
	io_cqring_fill_event(req, error ? error : mangle_poll(mask));
	io_commit_cqring(ctx);
}

static void io_poll_task_func(struct callback_head *cb)
{
	struct io_kiocb *req = container_of(cb, struct io_kiocb, task_work);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *nxt;

	if (io_poll_rewait(req, &req->poll)) {
		spin_unlock_irq(&ctx->completion_lock);
	} else {
		hash_del(&req->hash_node);
		io_poll_complete(req, req->result, 0);
		spin_unlock_irq(&ctx->completion_lock);

		nxt = io_put_req_find_next(req);
		io_cqring_ev_posted(ctx);
		if (nxt)
			__io_req_task_submit(nxt);
	}

	percpu_ref_put(&ctx->refs);
}

static int io_poll_double_wake(struct wait_queue_entry *wait, unsigned mode,
			       int sync, void *key)
{
	struct io_kiocb *req = wait->private;
	struct io_poll_iocb *poll = io_poll_get_single(req);
	__poll_t mask = key_to_poll(key);

	/* for instances that support it check for an event match first: */
	if (mask && !(mask & poll->events))
		return 0;

	list_del_init(&wait->entry);

	if (poll && poll->head) {
		bool done;

		spin_lock(&poll->head->lock);
		done = list_empty(&poll->wait.entry);
		if (!done)
			list_del_init(&poll->wait.entry);
		/* make sure double remove sees this as being gone */
		wait->private = NULL;
		spin_unlock(&poll->head->lock);
		if (!done) {
			/* use wait func handler, so it matches the rq type */
			poll->wait.func(&poll->wait, mode, sync, key);
		}
	}
	refcount_dec(&req->refs);
	return 1;
}

static void io_init_poll_iocb(struct io_poll_iocb *poll, __poll_t events,
			      wait_queue_func_t wake_func)
{
	poll->head = NULL;
	poll->done = false;
	poll->canceled = false;
	poll->events = events;
	INIT_LIST_HEAD(&poll->wait.entry);
	init_waitqueue_func_entry(&poll->wait, wake_func);
}

static void __io_queue_proc(struct io_poll_iocb *poll, struct io_poll_table *pt,
			    struct wait_queue_head *head,
			    struct io_poll_iocb **poll_ptr)
{
	struct io_kiocb *req = pt->req;

	/*
	 * If poll->head is already set, it's because the file being polled
	 * uses multiple waitqueues for poll handling (eg one for read, one
	 * for write). Setup a separate io_poll_iocb if this happens.
	 */
	if (unlikely(poll->head)) {
		struct io_poll_iocb *poll_one = poll;

		/* already have a 2nd entry, fail a third attempt */
		if (*poll_ptr) {
			pt->error = -EINVAL;
			return;
		}
		poll = kmalloc(sizeof(*poll), GFP_ATOMIC);
		if (!poll) {
			pt->error = -ENOMEM;
			return;
		}
		io_init_poll_iocb(poll, poll_one->events, io_poll_double_wake);
		refcount_inc(&req->refs);
		poll->wait.private = req;
		*poll_ptr = poll;
	}

	pt->error = 0;
	poll->head = head;

	if (poll->events & EPOLLEXCLUSIVE)
		add_wait_queue_exclusive(head, &poll->wait);
	else
		add_wait_queue(head, &poll->wait);
}

static void io_async_queue_proc(struct file *file, struct wait_queue_head *head,
			       struct poll_table_struct *p)
{
	struct io_poll_table *pt = container_of(p, struct io_poll_table, pt);
	struct async_poll *apoll = pt->req->apoll;

	__io_queue_proc(&apoll->poll, pt, head, &apoll->double_poll);
}

static void io_async_task_func(struct callback_head *cb)
{
	struct io_kiocb *req = container_of(cb, struct io_kiocb, task_work);
	struct async_poll *apoll = req->apoll;
	struct io_ring_ctx *ctx = req->ctx;

	trace_io_uring_task_run(req->ctx, req->opcode, req->user_data);

	if (io_poll_rewait(req, &apoll->poll)) {
		spin_unlock_irq(&ctx->completion_lock);
		percpu_ref_put(&ctx->refs);
		return;
	}

	/* If req is still hashed, it cannot have been canceled. Don't check. */
	if (hash_hashed(&req->hash_node))
		hash_del(&req->hash_node);

	io_poll_remove_double(req);
	spin_unlock_irq(&ctx->completion_lock);

	if (!READ_ONCE(apoll->poll.canceled))
		__io_req_task_submit(req);
	else
		__io_req_task_cancel(req, -ECANCELED);

	percpu_ref_put(&ctx->refs);
	kfree(apoll->double_poll);
	kfree(apoll);
}

static int io_async_wake(struct wait_queue_entry *wait, unsigned mode, int sync,
			void *key)
{
	struct io_kiocb *req = wait->private;
	struct io_poll_iocb *poll = &req->apoll->poll;

	trace_io_uring_poll_wake(req->ctx, req->opcode, req->user_data,
					key_to_poll(key));

	return __io_async_wake(req, poll, key_to_poll(key), io_async_task_func);
}

static void io_poll_req_insert(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct hlist_head *list;

	list = &ctx->cancel_hash[hash_long(req->user_data, ctx->cancel_hash_bits)];
	hlist_add_head(&req->hash_node, list);
}

static __poll_t __io_arm_poll_handler(struct io_kiocb *req,
				      struct io_poll_iocb *poll,
				      struct io_poll_table *ipt, __poll_t mask,
				      wait_queue_func_t wake_func)
	__acquires(&ctx->completion_lock)
{
	struct io_ring_ctx *ctx = req->ctx;
	bool cancel = false;

	INIT_HLIST_NODE(&req->hash_node);
	io_init_poll_iocb(poll, mask, wake_func);
	poll->file = req->file;
	poll->wait.private = req;

	ipt->pt._key = mask;
	ipt->req = req;
	ipt->error = -EINVAL;

	mask = vfs_poll(req->file, &ipt->pt) & poll->events;

	spin_lock_irq(&ctx->completion_lock);
	if (likely(poll->head)) {
		spin_lock(&poll->head->lock);
		if (unlikely(list_empty(&poll->wait.entry))) {
			if (ipt->error)
				cancel = true;
			ipt->error = 0;
			mask = 0;
		}
		if (mask || ipt->error)
			list_del_init(&poll->wait.entry);
		else if (cancel)
			WRITE_ONCE(poll->canceled, true);
		else if (!poll->done) /* actually waiting for an event */
			io_poll_req_insert(req);
		spin_unlock(&poll->head->lock);
	}

	return mask;
}

static bool io_arm_poll_handler(struct io_kiocb *req)
{
	const struct io_op_def *def = &io_op_defs[req->opcode];
	struct io_ring_ctx *ctx = req->ctx;
	struct async_poll *apoll;
	struct io_poll_table ipt;
	__poll_t mask, ret;
	int rw;

	if (!req->file || !file_can_poll(req->file))
		return false;
	if (req->flags & REQ_F_POLLED)
		return false;
	if (def->pollin)
		rw = READ;
	else if (def->pollout)
		rw = WRITE;
	else
		return false;
	/* if we can't nonblock try, then no point in arming a poll handler */
	if (!io_file_supports_async(req->file, rw))
		return false;

	apoll = kmalloc(sizeof(*apoll), GFP_ATOMIC);
	if (unlikely(!apoll))
		return false;
	apoll->double_poll = NULL;

	req->flags |= REQ_F_POLLED;
	req->apoll = apoll;

	mask = 0;
	if (def->pollin)
		mask |= POLLIN | POLLRDNORM;
	if (def->pollout)
		mask |= POLLOUT | POLLWRNORM;

	/* If reading from MSG_ERRQUEUE using recvmsg, ignore POLLIN */
	if ((req->opcode == IORING_OP_RECVMSG) &&
	    (req->sr_msg.msg_flags & MSG_ERRQUEUE))
		mask &= ~POLLIN;

	mask |= POLLERR | POLLPRI;

	ipt.pt._qproc = io_async_queue_proc;

	ret = __io_arm_poll_handler(req, &apoll->poll, &ipt, mask,
					io_async_wake);
	if (ret || ipt.error) {
		io_poll_remove_double(req);
		spin_unlock_irq(&ctx->completion_lock);
		kfree(apoll->double_poll);
		kfree(apoll);
		return false;
	}
	spin_unlock_irq(&ctx->completion_lock);
	trace_io_uring_poll_arm(ctx, req->opcode, req->user_data, mask,
					apoll->poll.events);
	return true;
}

static bool __io_poll_remove_one(struct io_kiocb *req,
				 struct io_poll_iocb *poll)
{
	bool do_complete = false;

	spin_lock(&poll->head->lock);
	WRITE_ONCE(poll->canceled, true);
	if (!list_empty(&poll->wait.entry)) {
		list_del_init(&poll->wait.entry);
		do_complete = true;
	}
	spin_unlock(&poll->head->lock);
	hash_del(&req->hash_node);
	return do_complete;
}

static bool io_poll_remove_one(struct io_kiocb *req)
{
	bool do_complete;

	io_poll_remove_double(req);

	if (req->opcode == IORING_OP_POLL_ADD) {
		do_complete = __io_poll_remove_one(req, &req->poll);
	} else {
		struct async_poll *apoll = req->apoll;

		/* non-poll requests have submit ref still */
		do_complete = __io_poll_remove_one(req, &apoll->poll);
		if (do_complete) {
			io_put_req(req);
			kfree(apoll->double_poll);
			kfree(apoll);
		}
	}

	if (do_complete) {
		io_cqring_fill_event(req, -ECANCELED);
		io_commit_cqring(req->ctx);
		req_set_fail_links(req);
		io_put_req_deferred(req, 1);
	}

	return do_complete;
}

/*
 * Returns true if we found and killed one or more poll requests
 */
static bool io_poll_remove_all(struct io_ring_ctx *ctx, struct task_struct *tsk)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	int posted = 0, i;

	spin_lock_irq(&ctx->completion_lock);
	for (i = 0; i < (1U << ctx->cancel_hash_bits); i++) {
		struct hlist_head *list;

		list = &ctx->cancel_hash[i];
		hlist_for_each_entry_safe(req, tmp, list, hash_node) {
			if (io_task_match(req, tsk))
				posted += io_poll_remove_one(req);
		}
	}
	spin_unlock_irq(&ctx->completion_lock);

	if (posted)
		io_cqring_ev_posted(ctx);

	return posted != 0;
}

static int io_poll_cancel(struct io_ring_ctx *ctx, __u64 sqe_addr)
{
	struct hlist_head *list;
	struct io_kiocb *req;

	list = &ctx->cancel_hash[hash_long(sqe_addr, ctx->cancel_hash_bits)];
	hlist_for_each_entry(req, list, hash_node) {
		if (sqe_addr != req->user_data)
			continue;
		if (io_poll_remove_one(req))
			return 0;
		return -EALREADY;
	}

	return -ENOENT;
}

static int io_poll_remove_prep(struct io_kiocb *req,
			       const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->ioprio || sqe->off || sqe->len || sqe->buf_index ||
	    sqe->poll_events)
		return -EINVAL;

	req->poll.addr = READ_ONCE(sqe->addr);
	return 0;
}

/*
 * Find a running poll command that matches one specified in sqe->addr,
 * and remove it if found.
 */
static int io_poll_remove(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	u64 addr;
	int ret;

	addr = req->poll.addr;
	spin_lock_irq(&ctx->completion_lock);
	ret = io_poll_cancel(ctx, addr);
	spin_unlock_irq(&ctx->completion_lock);

	if (ret < 0)
		req_set_fail_links(req);
	io_req_complete(req, ret);
	return 0;
}

static int io_poll_wake(struct wait_queue_entry *wait, unsigned mode, int sync,
			void *key)
{
	struct io_kiocb *req = wait->private;
	struct io_poll_iocb *poll = &req->poll;

	return __io_async_wake(req, poll, key_to_poll(key), io_poll_task_func);
}

static void io_poll_queue_proc(struct file *file, struct wait_queue_head *head,
			       struct poll_table_struct *p)
{
	struct io_poll_table *pt = container_of(p, struct io_poll_table, pt);

	__io_queue_proc(&pt->req->poll, pt, head, (struct io_poll_iocb **) &pt->req->async_data);
}

static int io_poll_add_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_poll_iocb *poll = &req->poll;
	u32 events;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->addr || sqe->ioprio || sqe->off || sqe->len || sqe->buf_index)
		return -EINVAL;

	events = READ_ONCE(sqe->poll32_events);
#ifdef __BIG_ENDIAN
	events = swahw32(events);
#endif
	poll->events = demangle_poll(events) | EPOLLERR | EPOLLHUP |
		       (events & EPOLLEXCLUSIVE);
	return 0;
}

static int io_poll_add(struct io_kiocb *req)
{
	struct io_poll_iocb *poll = &req->poll;
	struct io_ring_ctx *ctx = req->ctx;
	struct io_poll_table ipt;
	__poll_t mask;

	ipt.pt._qproc = io_poll_queue_proc;

	mask = __io_arm_poll_handler(req, &req->poll, &ipt, poll->events,
					io_poll_wake);

	if (mask) { /* no async, we'd stolen it */
		ipt.error = 0;
		io_poll_complete(req, mask, 0);
	}
	spin_unlock_irq(&ctx->completion_lock);

	if (mask) {
		io_cqring_ev_posted(ctx);
		io_put_req(req);
	}
	return ipt.error;
}

static enum hrtimer_restart io_timeout_fn(struct hrtimer *timer)
{
	struct io_timeout_data *data = container_of(timer,
						struct io_timeout_data, timer);
	struct io_kiocb *req = data->req;
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	spin_lock_irqsave(&ctx->completion_lock, flags);
	list_del_init(&req->timeout.list);
	atomic_set(&req->ctx->cq_timeouts,
		atomic_read(&req->ctx->cq_timeouts) + 1);

	io_cqring_fill_event(req, -ETIME);
	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	io_cqring_ev_posted(ctx);
	req_set_fail_links(req);
	io_put_req(req);
	return HRTIMER_NORESTART;
}

static int __io_timeout_cancel(struct io_kiocb *req)
{
	struct io_timeout_data *io = req->async_data;
	int ret;

	ret = hrtimer_try_to_cancel(&io->timer);
	if (ret == -1)
		return -EALREADY;
	list_del_init(&req->timeout.list);

	req_set_fail_links(req);
	io_cqring_fill_event(req, -ECANCELED);
	io_put_req_deferred(req, 1);
	return 0;
}

static int io_timeout_cancel(struct io_ring_ctx *ctx, __u64 user_data)
{
	struct io_kiocb *req;
	int ret = -ENOENT;

	list_for_each_entry(req, &ctx->timeout_list, timeout.list) {
		if (user_data == req->user_data) {
			ret = 0;
			break;
		}
	}

	if (ret == -ENOENT)
		return ret;

	return __io_timeout_cancel(req);
}

static int io_timeout_remove_prep(struct io_kiocb *req,
				  const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (unlikely(req->flags & (REQ_F_FIXED_FILE | REQ_F_BUFFER_SELECT)))
		return -EINVAL;
	if (sqe->ioprio || sqe->buf_index || sqe->len || sqe->timeout_flags)
		return -EINVAL;

	req->timeout_rem.addr = READ_ONCE(sqe->addr);
	return 0;
}

/*
 * Remove or update an existing timeout command
 */
static int io_timeout_remove(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	spin_lock_irq(&ctx->completion_lock);
	ret = io_timeout_cancel(ctx, req->timeout_rem.addr);

	io_cqring_fill_event(req, ret);
	io_commit_cqring(ctx);
	spin_unlock_irq(&ctx->completion_lock);
	io_cqring_ev_posted(ctx);
	if (ret < 0)
		req_set_fail_links(req);
	io_put_req(req);
	return 0;
}

static int io_timeout_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			   bool is_timeout_link)
{
	struct io_timeout_data *data;
	unsigned flags;
	u32 off = READ_ONCE(sqe->off);

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->ioprio || sqe->buf_index || sqe->len != 1)
		return -EINVAL;
	if (off && is_timeout_link)
		return -EINVAL;
	flags = READ_ONCE(sqe->timeout_flags);
	if (flags & ~IORING_TIMEOUT_ABS)
		return -EINVAL;

	req->timeout.off = off;

	if (!req->async_data && io_alloc_async_data(req))
		return -ENOMEM;

	data = req->async_data;
	data->req = req;

	if (get_timespec64(&data->ts, u64_to_user_ptr(sqe->addr)))
		return -EFAULT;

	if (flags & IORING_TIMEOUT_ABS)
		data->mode = HRTIMER_MODE_ABS;
	else
		data->mode = HRTIMER_MODE_REL;

	hrtimer_init(&data->timer, CLOCK_MONOTONIC, data->mode);
	return 0;
}

static int io_timeout(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_timeout_data *data = req->async_data;
	struct list_head *entry;
	u32 tail, off = req->timeout.off;

	spin_lock_irq(&ctx->completion_lock);

	/*
	 * sqe->off holds how many events that need to occur for this
	 * timeout event to be satisfied. If it isn't set, then this is
	 * a pure timeout request, sequence isn't used.
	 */
	if (io_is_timeout_noseq(req)) {
		entry = ctx->timeout_list.prev;
		goto add;
	}

	tail = ctx->cached_cq_tail - atomic_read(&ctx->cq_timeouts);
	req->timeout.target_seq = tail + off;

	/*
	 * Insertion sort, ensuring the first entry in the list is always
	 * the one we need first.
	 */
	list_for_each_prev(entry, &ctx->timeout_list) {
		struct io_kiocb *nxt = list_entry(entry, struct io_kiocb,
						  timeout.list);

		if (io_is_timeout_noseq(nxt))
			continue;
		/* nxt.seq is behind @tail, otherwise would've been completed */
		if (off >= nxt->timeout.target_seq - tail)
			break;
	}
add:
	list_add(&req->timeout.list, entry);
	data->timer.function = io_timeout_fn;
	hrtimer_start(&data->timer, timespec64_to_ktime(data->ts), data->mode);
	spin_unlock_irq(&ctx->completion_lock);
	return 0;
}

static bool io_cancel_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);

	return req->user_data == (unsigned long) data;
}

static int io_async_cancel_one(struct io_ring_ctx *ctx, void *sqe_addr)
{
	enum io_wq_cancel cancel_ret;
	int ret = 0;

	cancel_ret = io_wq_cancel_cb(ctx->io_wq, io_cancel_cb, sqe_addr, false);
	switch (cancel_ret) {
	case IO_WQ_CANCEL_OK:
		ret = 0;
		break;
	case IO_WQ_CANCEL_RUNNING:
		ret = -EALREADY;
		break;
	case IO_WQ_CANCEL_NOTFOUND:
		ret = -ENOENT;
		break;
	}

	return ret;
}

static void io_async_find_and_cancel(struct io_ring_ctx *ctx,
				     struct io_kiocb *req, __u64 sqe_addr,
				     int success_ret)
{
	unsigned long flags;
	int ret;

	ret = io_async_cancel_one(ctx, (void *) (unsigned long) sqe_addr);
	if (ret != -ENOENT) {
		spin_lock_irqsave(&ctx->completion_lock, flags);
		goto done;
	}

	spin_lock_irqsave(&ctx->completion_lock, flags);
	ret = io_timeout_cancel(ctx, sqe_addr);
	if (ret != -ENOENT)
		goto done;
	ret = io_poll_cancel(ctx, sqe_addr);
done:
	if (!ret)
		ret = success_ret;
	io_cqring_fill_event(req, ret);
	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);
	io_cqring_ev_posted(ctx);

	if (ret < 0)
		req_set_fail_links(req);
	io_put_req(req);
}

static int io_async_cancel_prep(struct io_kiocb *req,
				const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (unlikely(req->flags & (REQ_F_FIXED_FILE | REQ_F_BUFFER_SELECT)))
		return -EINVAL;
	if (sqe->ioprio || sqe->off || sqe->len || sqe->cancel_flags)
		return -EINVAL;

	req->cancel.addr = READ_ONCE(sqe->addr);
	return 0;
}

static int io_async_cancel(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	io_async_find_and_cancel(ctx, req, req->cancel.addr, 0);
	return 0;
}

static int io_files_update_prep(struct io_kiocb *req,
				const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & IORING_SETUP_SQPOLL))
		return -EINVAL;
	if (unlikely(req->flags & (REQ_F_FIXED_FILE | REQ_F_BUFFER_SELECT)))
		return -EINVAL;
	if (sqe->ioprio || sqe->rw_flags)
		return -EINVAL;

	req->files_update.offset = READ_ONCE(sqe->off);
	req->files_update.nr_args = READ_ONCE(sqe->len);
	if (!req->files_update.nr_args)
		return -EINVAL;
	req->files_update.arg = READ_ONCE(sqe->addr);
	return 0;
}

static int io_files_update(struct io_kiocb *req, bool force_nonblock,
			   struct io_comp_state *cs)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_uring_files_update up;
	int ret;

	if (force_nonblock)
		return -EAGAIN;

	up.offset = req->files_update.offset;
	up.fds = req->files_update.arg;

	mutex_lock(&ctx->uring_lock);
	ret = __io_sqe_files_update(ctx, &up, req->files_update.nr_args);
	mutex_unlock(&ctx->uring_lock);

	if (ret < 0)
		req_set_fail_links(req);
	__io_req_complete(req, ret, 0, cs);
	return 0;
}

static int io_req_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	switch (req->opcode) {
	case IORING_OP_NOP:
		return 0;
	case IORING_OP_READV:
	case IORING_OP_READ_FIXED:
	case IORING_OP_READ:
		return io_read_prep(req, sqe);
	case IORING_OP_WRITEV:
	case IORING_OP_WRITE_FIXED:
	case IORING_OP_WRITE:
		return io_write_prep(req, sqe);
	case IORING_OP_POLL_ADD:
		return io_poll_add_prep(req, sqe);
	case IORING_OP_POLL_REMOVE:
		return io_poll_remove_prep(req, sqe);
	case IORING_OP_FSYNC:
		return io_prep_fsync(req, sqe);
	case IORING_OP_SYNC_FILE_RANGE:
		return io_prep_sfr(req, sqe);
	case IORING_OP_SENDMSG:
	case IORING_OP_SEND:
		return io_sendmsg_prep(req, sqe);
	case IORING_OP_RECVMSG:
	case IORING_OP_RECV:
		return io_recvmsg_prep(req, sqe);
	case IORING_OP_CONNECT:
		return io_connect_prep(req, sqe);
	case IORING_OP_TIMEOUT:
		return io_timeout_prep(req, sqe, false);
	case IORING_OP_TIMEOUT_REMOVE:
		return io_timeout_remove_prep(req, sqe);
	case IORING_OP_ASYNC_CANCEL:
		return io_async_cancel_prep(req, sqe);
	case IORING_OP_LINK_TIMEOUT:
		return io_timeout_prep(req, sqe, true);
	case IORING_OP_ACCEPT:
		return io_accept_prep(req, sqe);
	case IORING_OP_FALLOCATE:
		return io_fallocate_prep(req, sqe);
	case IORING_OP_OPENAT:
		return io_openat_prep(req, sqe);
	case IORING_OP_CLOSE:
		return io_close_prep(req, sqe);
	case IORING_OP_FILES_UPDATE:
		return io_files_update_prep(req, sqe);
	case IORING_OP_STATX:
		return io_statx_prep(req, sqe);
	case IORING_OP_FADVISE:
		return io_fadvise_prep(req, sqe);
	case IORING_OP_MADVISE:
		return io_madvise_prep(req, sqe);
	case IORING_OP_OPENAT2:
		return io_openat2_prep(req, sqe);
	case IORING_OP_EPOLL_CTL:
		return io_epoll_ctl_prep(req, sqe);
	case IORING_OP_SPLICE:
		return io_splice_prep(req, sqe);
	case IORING_OP_PROVIDE_BUFFERS:
		return io_provide_buffers_prep(req, sqe);
	case IORING_OP_REMOVE_BUFFERS:
		return io_remove_buffers_prep(req, sqe);
	case IORING_OP_TEE:
		return io_tee_prep(req, sqe);
	}

	printk_once(KERN_WARNING "io_uring: unhandled opcode %d\n",
			req->opcode);
	return-EINVAL;
}

static int io_req_defer_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe)
{
	if (!sqe)
		return 0;
	if (io_alloc_async_data(req))
		return -EAGAIN;
	return io_req_prep(req, sqe);
}

static u32 io_get_sequence(struct io_kiocb *req)
{
	struct io_kiocb *pos;
	struct io_ring_ctx *ctx = req->ctx;
	u32 total_submitted, nr_reqs = 1;

	if (req->flags & REQ_F_LINK_HEAD)
		list_for_each_entry(pos, &req->link_list, link_list)
			nr_reqs++;

	total_submitted = ctx->cached_sq_head - ctx->cached_sq_dropped;
	return total_submitted - nr_reqs;
}

static int io_req_defer(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_defer_entry *de;
	int ret;
	u32 seq;

	/* Still need defer if there is pending req in defer list. */
	if (likely(list_empty_careful(&ctx->defer_list) &&
		!(req->flags & REQ_F_IO_DRAIN)))
		return 0;

	seq = io_get_sequence(req);
	/* Still a chance to pass the sequence check */
	if (!req_need_defer(req, seq) && list_empty_careful(&ctx->defer_list))
		return 0;

	if (!req->async_data) {
		ret = io_req_defer_prep(req, sqe);
		if (ret)
			return ret;
	}
	io_prep_async_link(req);
	de = kmalloc(sizeof(*de), GFP_KERNEL);
	if (!de)
		return -ENOMEM;

	spin_lock_irq(&ctx->completion_lock);
	if (!req_need_defer(req, seq) && list_empty(&ctx->defer_list)) {
		spin_unlock_irq(&ctx->completion_lock);
		kfree(de);
		io_queue_async_work(req);
		return -EIOCBQUEUED;
	}

	trace_io_uring_defer(ctx, req, req->user_data);
	de->req = req;
	de->seq = seq;
	list_add_tail(&de->list, &ctx->defer_list);
	spin_unlock_irq(&ctx->completion_lock);
	return -EIOCBQUEUED;
}

static void io_req_drop_files(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	spin_lock_irqsave(&ctx->inflight_lock, flags);
	list_del(&req->inflight_entry);
	if (waitqueue_active(&ctx->inflight_wait))
		wake_up(&ctx->inflight_wait);
	spin_unlock_irqrestore(&ctx->inflight_lock, flags);
	req->flags &= ~REQ_F_INFLIGHT;
	put_files_struct(req->work.identity->files);
	put_nsproxy(req->work.identity->nsproxy);
	req->work.flags &= ~IO_WQ_WORK_FILES;
}

static void __io_clean_op(struct io_kiocb *req)
{
	if (req->flags & REQ_F_BUFFER_SELECTED) {
		switch (req->opcode) {
		case IORING_OP_READV:
		case IORING_OP_READ_FIXED:
		case IORING_OP_READ:
			kfree((void *)(unsigned long)req->rw.addr);
			break;
		case IORING_OP_RECVMSG:
		case IORING_OP_RECV:
			kfree(req->sr_msg.kbuf);
			break;
		}
		req->flags &= ~REQ_F_BUFFER_SELECTED;
	}

	if (req->flags & REQ_F_NEED_CLEANUP) {
		switch (req->opcode) {
		case IORING_OP_READV:
		case IORING_OP_READ_FIXED:
		case IORING_OP_READ:
		case IORING_OP_WRITEV:
		case IORING_OP_WRITE_FIXED:
		case IORING_OP_WRITE: {
			struct io_async_rw *io = req->async_data;
			if (io->free_iovec)
				kfree(io->free_iovec);
			break;
			}
		case IORING_OP_RECVMSG:
		case IORING_OP_SENDMSG: {
			struct io_async_msghdr *io = req->async_data;
			if (io->iov != io->fast_iov)
				kfree(io->iov);
			break;
			}
		case IORING_OP_SPLICE:
		case IORING_OP_TEE:
			io_put_file(req, req->splice.file_in,
				    (req->splice.flags & SPLICE_F_FD_IN_FIXED));
			break;
		case IORING_OP_OPENAT:
		case IORING_OP_OPENAT2:
			if (req->open.filename)
				putname(req->open.filename);
			break;
		}
		req->flags &= ~REQ_F_NEED_CLEANUP;
	}

	if (req->flags & REQ_F_INFLIGHT)
		io_req_drop_files(req);
}

static int io_issue_sqe(struct io_kiocb *req, bool force_nonblock,
			struct io_comp_state *cs)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	switch (req->opcode) {
	case IORING_OP_NOP:
		ret = io_nop(req, cs);
		break;
	case IORING_OP_READV:
	case IORING_OP_READ_FIXED:
	case IORING_OP_READ:
		ret = io_read(req, force_nonblock, cs);
		break;
	case IORING_OP_WRITEV:
	case IORING_OP_WRITE_FIXED:
	case IORING_OP_WRITE:
		ret = io_write(req, force_nonblock, cs);
		break;
	case IORING_OP_FSYNC:
		ret = io_fsync(req, force_nonblock);
		break;
	case IORING_OP_POLL_ADD:
		ret = io_poll_add(req);
		break;
	case IORING_OP_POLL_REMOVE:
		ret = io_poll_remove(req);
		break;
	case IORING_OP_SYNC_FILE_RANGE:
		ret = io_sync_file_range(req, force_nonblock);
		break;
	case IORING_OP_SENDMSG:
		ret = io_sendmsg(req, force_nonblock, cs);
		break;
	case IORING_OP_SEND:
		ret = io_send(req, force_nonblock, cs);
		break;
	case IORING_OP_RECVMSG:
		ret = io_recvmsg(req, force_nonblock, cs);
		break;
	case IORING_OP_RECV:
		ret = io_recv(req, force_nonblock, cs);
		break;
	case IORING_OP_TIMEOUT:
		ret = io_timeout(req);
		break;
	case IORING_OP_TIMEOUT_REMOVE:
		ret = io_timeout_remove(req);
		break;
	case IORING_OP_ACCEPT:
		ret = io_accept(req, force_nonblock, cs);
		break;
	case IORING_OP_CONNECT:
		ret = io_connect(req, force_nonblock, cs);
		break;
	case IORING_OP_ASYNC_CANCEL:
		ret = io_async_cancel(req);
		break;
	case IORING_OP_FALLOCATE:
		ret = io_fallocate(req, force_nonblock);
		break;
	case IORING_OP_OPENAT:
		ret = io_openat(req, force_nonblock);
		break;
	case IORING_OP_CLOSE:
		ret = io_close(req, force_nonblock, cs);
		break;
	case IORING_OP_FILES_UPDATE:
		ret = io_files_update(req, force_nonblock, cs);
		break;
	case IORING_OP_STATX:
		ret = io_statx(req, force_nonblock);
		break;
	case IORING_OP_FADVISE:
		ret = io_fadvise(req, force_nonblock);
		break;
	case IORING_OP_MADVISE:
		ret = io_madvise(req, force_nonblock);
		break;
	case IORING_OP_OPENAT2:
		ret = io_openat2(req, force_nonblock);
		break;
	case IORING_OP_EPOLL_CTL:
		ret = io_epoll_ctl(req, force_nonblock, cs);
		break;
	case IORING_OP_SPLICE:
		ret = io_splice(req, force_nonblock);
		break;
	case IORING_OP_PROVIDE_BUFFERS:
		ret = io_provide_buffers(req, force_nonblock, cs);
		break;
	case IORING_OP_REMOVE_BUFFERS:
		ret = io_remove_buffers(req, force_nonblock, cs);
		break;
	case IORING_OP_TEE:
		ret = io_tee(req, force_nonblock);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	/* If the op doesn't have a file, we're not polling for it */
	if ((ctx->flags & IORING_SETUP_IOPOLL) && req->file) {
		const bool in_async = io_wq_current_is_worker();

		/* workqueue context doesn't hold uring_lock, grab it now */
		if (in_async)
			mutex_lock(&ctx->uring_lock);

		io_iopoll_req_issued(req);

		if (in_async)
			mutex_unlock(&ctx->uring_lock);
	}

	return 0;
}

static struct io_wq_work *io_wq_submit_work(struct io_wq_work *work)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_kiocb *timeout;
	int ret = 0;

	timeout = io_prep_linked_timeout(req);
	if (timeout)
		io_queue_linked_timeout(timeout);

	/* if NO_CANCEL is set, we must still run the work */
	if ((work->flags & (IO_WQ_WORK_CANCEL|IO_WQ_WORK_NO_CANCEL)) ==
				IO_WQ_WORK_CANCEL) {
		ret = -ECANCELED;
	}

	if (!ret) {
		do {
			ret = io_issue_sqe(req, false, NULL);
			/*
			 * We can get EAGAIN for polled IO even though we're
			 * forcing a sync submission from here, since we can't
			 * wait for request slots on the block side.
			 */
			if (ret != -EAGAIN)
				break;
			cond_resched();
		} while (1);
	}

	if (ret) {
		struct io_ring_ctx *lock_ctx = NULL;

		if (req->ctx->flags & IORING_SETUP_IOPOLL)
			lock_ctx = req->ctx;

		/*
		 * io_iopoll_complete() does not hold completion_lock to
		 * complete polled io, so here for polled io, we can not call
		 * io_req_complete() directly, otherwise there maybe concurrent
		 * access to cqring, defer_list, etc, which is not safe. Given
		 * that io_iopoll_complete() is always called under uring_lock,
		 * so here for polled io, we also get uring_lock to complete
		 * it.
		 */
		if (lock_ctx)
			mutex_lock(&lock_ctx->uring_lock);

		req_set_fail_links(req);
		io_req_complete(req, ret);

		if (lock_ctx)
			mutex_unlock(&lock_ctx->uring_lock);
	}

	return io_steal_work(req);
}

static inline struct file *io_file_from_index(struct io_ring_ctx *ctx,
					      int index)
{
	struct fixed_file_table *table;

	table = &ctx->file_data->table[index >> IORING_FILE_TABLE_SHIFT];
	return table->files[index & IORING_FILE_TABLE_MASK];
}

static struct file *io_file_get(struct io_submit_state *state,
				struct io_kiocb *req, int fd, bool fixed)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct file *file;

	if (fixed) {
		if (unlikely((unsigned int)fd >= ctx->nr_user_files))
			return NULL;
		fd = array_index_nospec(fd, ctx->nr_user_files);
		file = io_file_from_index(ctx, fd);
		if (file) {
			req->fixed_file_refs = &ctx->file_data->node->refs;
			percpu_ref_get(req->fixed_file_refs);
		}
	} else {
		trace_io_uring_file_get(ctx, fd);
		file = __io_file_get(state, fd);
	}

	return file;
}

static int io_req_set_file(struct io_submit_state *state, struct io_kiocb *req,
			   int fd)
{
	bool fixed;

	fixed = (req->flags & REQ_F_FIXED_FILE) != 0;
	if (unlikely(!fixed && io_async_submit(req->ctx)))
		return -EBADF;

	req->file = io_file_get(state, req, fd, fixed);
	if (req->file || io_op_defs[req->opcode].needs_file_no_error)
		return 0;
	return -EBADF;
}

static enum hrtimer_restart io_link_timeout_fn(struct hrtimer *timer)
{
	struct io_timeout_data *data = container_of(timer,
						struct io_timeout_data, timer);
	struct io_kiocb *req = data->req;
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *prev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ctx->completion_lock, flags);

	/*
	 * We don't expect the list to be empty, that will only happen if we
	 * race with the completion of the linked work.
	 */
	if (!list_empty(&req->link_list)) {
		prev = list_entry(req->link_list.prev, struct io_kiocb,
				  link_list);
		if (refcount_inc_not_zero(&prev->refs))
			list_del_init(&req->link_list);
		else
			prev = NULL;
	}

	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	if (prev) {
		req_set_fail_links(prev);
		io_async_find_and_cancel(ctx, req, prev->user_data, -ETIME);
		io_put_req(prev);
	} else {
		io_req_complete(req, -ETIME);
	}
	return HRTIMER_NORESTART;
}

static void __io_queue_linked_timeout(struct io_kiocb *req)
{
	/*
	 * If the list is now empty, then our linked request finished before
	 * we got a chance to setup the timer
	 */
	if (!list_empty(&req->link_list)) {
		struct io_timeout_data *data = req->async_data;

		data->timer.function = io_link_timeout_fn;
		hrtimer_start(&data->timer, timespec64_to_ktime(data->ts),
				data->mode);
	}
}

static void io_queue_linked_timeout(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	spin_lock_irq(&ctx->completion_lock);
	__io_queue_linked_timeout(req);
	spin_unlock_irq(&ctx->completion_lock);

	/* drop submission reference */
	io_put_req(req);
}

static struct io_kiocb *io_prep_linked_timeout(struct io_kiocb *req)
{
	struct io_kiocb *nxt;

	if (!(req->flags & REQ_F_LINK_HEAD))
		return NULL;
	if (req->flags & REQ_F_LINK_TIMEOUT)
		return NULL;

	nxt = list_first_entry_or_null(&req->link_list, struct io_kiocb,
					link_list);
	if (!nxt || nxt->opcode != IORING_OP_LINK_TIMEOUT)
		return NULL;

	nxt->flags |= REQ_F_LTIMEOUT_ACTIVE;
	req->flags |= REQ_F_LINK_TIMEOUT;
	return nxt;
}

static void __io_queue_sqe(struct io_kiocb *req, struct io_comp_state *cs)
{
	struct io_kiocb *linked_timeout;
	const struct cred *old_creds = NULL;
	int ret;

again:
	linked_timeout = io_prep_linked_timeout(req);

	if ((req->flags & REQ_F_WORK_INITIALIZED) &&
	    (req->work.flags & IO_WQ_WORK_CREDS) &&
	    req->work.identity->creds != current_cred()) {
		if (old_creds)
			revert_creds(old_creds);
		if (old_creds == req->work.identity->creds)
			old_creds = NULL; /* restored original creds */
		else
			old_creds = override_creds(req->work.identity->creds);
	}

	ret = io_issue_sqe(req, true, cs);

	/*
	 * We async punt it if the file wasn't marked NOWAIT, or if the file
	 * doesn't support non-blocking read/write attempts
	 */
	if (ret == -EAGAIN && !(req->flags & REQ_F_NOWAIT)) {
		if (!io_arm_poll_handler(req)) {
			/*
			 * Queued up for async execution, worker will release
			 * submit reference when the iocb is actually submitted.
			 */
			io_queue_async_work(req);
		}

		if (linked_timeout)
			io_queue_linked_timeout(linked_timeout);
	} else if (likely(!ret)) {
		/* drop submission reference */
		req = io_put_req_find_next(req);
		if (linked_timeout)
			io_queue_linked_timeout(linked_timeout);

		if (req) {
			if (!(req->flags & REQ_F_FORCE_ASYNC))
				goto again;
			io_queue_async_work(req);
		}
	} else {
		/* un-prep timeout, so it'll be killed as any other linked */
		req->flags &= ~REQ_F_LINK_TIMEOUT;
		req_set_fail_links(req);
		io_put_req(req);
		io_req_complete(req, ret);
	}

	if (old_creds)
		revert_creds(old_creds);
}

static void io_queue_sqe(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			 struct io_comp_state *cs)
{
	int ret;

	ret = io_req_defer(req, sqe);
	if (ret) {
		if (ret != -EIOCBQUEUED) {
fail_req:
			req_set_fail_links(req);
			io_put_req(req);
			io_req_complete(req, ret);
		}
	} else if (req->flags & REQ_F_FORCE_ASYNC) {
		if (!req->async_data) {
			ret = io_req_defer_prep(req, sqe);
			if (unlikely(ret))
				goto fail_req;
		}
		io_queue_async_work(req);
	} else {
		if (sqe) {
			ret = io_req_prep(req, sqe);
			if (unlikely(ret))
				goto fail_req;
		}
		__io_queue_sqe(req, cs);
	}
}

static inline void io_queue_link_head(struct io_kiocb *req,
				      struct io_comp_state *cs)
{
	if (unlikely(req->flags & REQ_F_FAIL_LINK)) {
		io_put_req(req);
		io_req_complete(req, -ECANCELED);
	} else
		io_queue_sqe(req, NULL, cs);
}

static int io_submit_sqe(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			 struct io_kiocb **link, struct io_comp_state *cs)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	/*
	 * If we already have a head request, queue this one for async
	 * submittal once the head completes. If we don't have a head but
	 * IOSQE_IO_LINK is set in the sqe, start a new head. This one will be
	 * submitted sync once the chain is complete. If none of those
	 * conditions are true (normal request), then just queue it.
	 */
	if (*link) {
		struct io_kiocb *head = *link;

		/*
		 * Taking sequential execution of a link, draining both sides
		 * of the link also fullfils IOSQE_IO_DRAIN semantics for all
		 * requests in the link. So, it drains the head and the
		 * next after the link request. The last one is done via
		 * drain_next flag to persist the effect across calls.
		 */
		if (req->flags & REQ_F_IO_DRAIN) {
			head->flags |= REQ_F_IO_DRAIN;
			ctx->drain_next = 1;
		}
		ret = io_req_defer_prep(req, sqe);
		if (unlikely(ret)) {
			/* fail even hard links since we don't submit */
			head->flags |= REQ_F_FAIL_LINK;
			return ret;
		}
		trace_io_uring_link(ctx, req, head);
		list_add_tail(&req->link_list, &head->link_list);

		/* last request of a link, enqueue the link */
		if (!(req->flags & (REQ_F_LINK | REQ_F_HARDLINK))) {
			io_queue_link_head(head, cs);
			*link = NULL;
		}
	} else {
		if (unlikely(ctx->drain_next)) {
			req->flags |= REQ_F_IO_DRAIN;
			ctx->drain_next = 0;
		}
		if (req->flags & (REQ_F_LINK | REQ_F_HARDLINK)) {
			req->flags |= REQ_F_LINK_HEAD;
			INIT_LIST_HEAD(&req->link_list);

			ret = io_req_defer_prep(req, sqe);
			if (unlikely(ret))
				req->flags |= REQ_F_FAIL_LINK;
			*link = req;
		} else {
			io_queue_sqe(req, sqe, cs);
		}
	}

	return 0;
}

/*
 * Batched submission is done, ensure local IO is flushed out.
 */
static void io_submit_state_end(struct io_submit_state *state)
{
	if (!list_empty(&state->comp.list))
		io_submit_flush_completions(&state->comp);
	blk_finish_plug(&state->plug);
	io_state_file_put(state);
	if (state->free_reqs)
		kmem_cache_free_bulk(req_cachep, state->free_reqs, state->reqs);
}

/*
 * Start submission side cache.
 */
static void io_submit_state_start(struct io_submit_state *state,
				  struct io_ring_ctx *ctx, unsigned int max_ios)
{
	blk_start_plug(&state->plug);
	state->comp.nr = 0;
	INIT_LIST_HEAD(&state->comp.list);
	state->comp.ctx = ctx;
	state->free_reqs = 0;
	state->file = NULL;
	state->ios_left = max_ios;
}

static void io_commit_sqring(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;

	/*
	 * Ensure any loads from the SQEs are done at this point,
	 * since once we write the new head, the application could
	 * write new data to them.
	 */
	smp_store_release(&rings->sq.head, ctx->cached_sq_head);
}

/*
 * Fetch an sqe, if one is available. Note that sqe_ptr will point to memory
 * that is mapped by userspace. This means that care needs to be taken to
 * ensure that reads are stable, as we cannot rely on userspace always
 * being a good citizen. If members of the sqe are validated and then later
 * used, it's important that those reads are done through READ_ONCE() to
 * prevent a re-load down the line.
 */
static const struct io_uring_sqe *io_get_sqe(struct io_ring_ctx *ctx)
{
	u32 *sq_array = ctx->sq_array;
	unsigned head;

	/*
	 * The cached sq head (or cq tail) serves two purposes:
	 *
	 * 1) allows us to batch the cost of updating the user visible
	 *    head updates.
	 * 2) allows the kernel side to track the head on its own, even
	 *    though the application is the one updating it.
	 */
	head = READ_ONCE(sq_array[ctx->cached_sq_head & ctx->sq_mask]);
	if (likely(head < ctx->sq_entries))
		return &ctx->sq_sqes[head];

	/* drop invalid entries */
	ctx->cached_sq_dropped++;
	WRITE_ONCE(ctx->rings->sq_dropped, ctx->cached_sq_dropped);
	return NULL;
}

static inline void io_consume_sqe(struct io_ring_ctx *ctx)
{
	ctx->cached_sq_head++;
}

/*
 * Check SQE restrictions (opcode and flags).
 *
 * Returns 'true' if SQE is allowed, 'false' otherwise.
 */
static inline bool io_check_restriction(struct io_ring_ctx *ctx,
					struct io_kiocb *req,
					unsigned int sqe_flags)
{
	if (!ctx->restricted)
		return true;

	if (!test_bit(req->opcode, ctx->restrictions.sqe_op))
		return false;

	if ((sqe_flags & ctx->restrictions.sqe_flags_required) !=
	    ctx->restrictions.sqe_flags_required)
		return false;

	if (sqe_flags & ~(ctx->restrictions.sqe_flags_allowed |
			  ctx->restrictions.sqe_flags_required))
		return false;

	return true;
}

#define SQE_VALID_FLAGS	(IOSQE_FIXED_FILE|IOSQE_IO_DRAIN|IOSQE_IO_LINK|	\
				IOSQE_IO_HARDLINK | IOSQE_ASYNC | \
				IOSQE_BUFFER_SELECT)

static int io_init_req(struct io_ring_ctx *ctx, struct io_kiocb *req,
		       const struct io_uring_sqe *sqe,
		       struct io_submit_state *state)
{
	unsigned int sqe_flags;
	int id, ret;

	req->opcode = READ_ONCE(sqe->opcode);
	req->user_data = READ_ONCE(sqe->user_data);
	req->async_data = NULL;
	req->file = NULL;
	req->ctx = ctx;
	req->flags = 0;
	/* one is dropped after submission, the other at completion */
	refcount_set(&req->refs, 2);
	req->task = current;
	req->result = 0;

	if (unlikely(req->opcode >= IORING_OP_LAST))
		return -EINVAL;

	if (unlikely(io_sq_thread_acquire_mm(ctx, req)))
		return -EFAULT;

	sqe_flags = READ_ONCE(sqe->flags);
	/* enforce forwards compatibility on users */
	if (unlikely(sqe_flags & ~SQE_VALID_FLAGS))
		return -EINVAL;

	if (unlikely(!io_check_restriction(ctx, req, sqe_flags)))
		return -EACCES;

	if ((sqe_flags & IOSQE_BUFFER_SELECT) &&
	    !io_op_defs[req->opcode].buffer_select)
		return -EOPNOTSUPP;

	id = READ_ONCE(sqe->personality);
	if (id) {
		struct io_identity *iod;

		iod = idr_find(&ctx->personality_idr, id);
		if (unlikely(!iod))
			return -EINVAL;
		refcount_inc(&iod->count);

		__io_req_init_async(req);
		get_cred(iod->creds);
		req->work.identity = iod;
		req->work.flags |= IO_WQ_WORK_CREDS;
	}

	/* same numerical values with corresponding REQ_F_*, safe to copy */
	req->flags |= sqe_flags;

	if (!io_op_defs[req->opcode].needs_file)
		return 0;

	ret = io_req_set_file(state, req, READ_ONCE(sqe->fd));
	state->ios_left--;
	return ret;
}

static int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr)
{
	struct io_submit_state state;
	struct io_kiocb *link = NULL;
	int i, submitted = 0;

	/* if we have a backlog and couldn't flush it all, return BUSY */
	if (test_bit(0, &ctx->sq_check_overflow)) {
		if (!list_empty(&ctx->cq_overflow_list) &&
		    !io_cqring_overflow_flush(ctx, false, NULL, NULL))
			return -EBUSY;
	}

	/* make sure SQ entry isn't read before tail */
	nr = min3(nr, ctx->sq_entries, io_sqring_entries(ctx));

	if (!percpu_ref_tryget_many(&ctx->refs, nr))
		return -EAGAIN;

	percpu_counter_add(&current->io_uring->inflight, nr);
	refcount_add(nr, &current->usage);

	io_submit_state_start(&state, ctx, nr);

	for (i = 0; i < nr; i++) {
		const struct io_uring_sqe *sqe;
		struct io_kiocb *req;
		int err;

		sqe = io_get_sqe(ctx);
		if (unlikely(!sqe)) {
			io_consume_sqe(ctx);
			break;
		}
		req = io_alloc_req(ctx, &state);
		if (unlikely(!req)) {
			if (!submitted)
				submitted = -EAGAIN;
			break;
		}
		io_consume_sqe(ctx);
		/* will complete beyond this point, count as submitted */
		submitted++;

		err = io_init_req(ctx, req, sqe, &state);
		if (unlikely(err)) {
fail_req:
			io_put_req(req);
			io_req_complete(req, err);
			break;
		}

		trace_io_uring_submit_sqe(ctx, req->opcode, req->user_data,
						true, io_async_submit(ctx));
		err = io_submit_sqe(req, sqe, &link, &state.comp);
		if (err)
			goto fail_req;
	}

	if (unlikely(submitted != nr)) {
		int ref_used = (submitted == -EAGAIN) ? 0 : submitted;
		struct io_uring_task *tctx = current->io_uring;
		int unused = nr - ref_used;

		percpu_ref_put_many(&ctx->refs, unused);
		percpu_counter_sub(&tctx->inflight, unused);
		put_task_struct_many(current, unused);
	}
	if (link)
		io_queue_link_head(link, &state.comp);
	io_submit_state_end(&state);

	 /* Commit SQ ring head once we've consumed and submitted all SQEs */
	io_commit_sqring(ctx);

	return submitted;
}

static inline void io_ring_set_wakeup_flag(struct io_ring_ctx *ctx)
{
	/* Tell userspace we may need a wakeup call */
	spin_lock_irq(&ctx->completion_lock);
	ctx->rings->sq_flags |= IORING_SQ_NEED_WAKEUP;
	spin_unlock_irq(&ctx->completion_lock);
}

static inline void io_ring_clear_wakeup_flag(struct io_ring_ctx *ctx)
{
	spin_lock_irq(&ctx->completion_lock);
	ctx->rings->sq_flags &= ~IORING_SQ_NEED_WAKEUP;
	spin_unlock_irq(&ctx->completion_lock);
}

static int io_sq_wake_function(struct wait_queue_entry *wqe, unsigned mode,
			       int sync, void *key)
{
	struct io_ring_ctx *ctx = container_of(wqe, struct io_ring_ctx, sqo_wait_entry);
	int ret;

	ret = autoremove_wake_function(wqe, mode, sync, key);
	if (ret) {
		unsigned long flags;

		spin_lock_irqsave(&ctx->completion_lock, flags);
		ctx->rings->sq_flags &= ~IORING_SQ_NEED_WAKEUP;
		spin_unlock_irqrestore(&ctx->completion_lock, flags);
	}
	return ret;
}

enum sq_ret {
	SQT_IDLE	= 1,
	SQT_SPIN	= 2,
	SQT_DID_WORK	= 4,
};

static enum sq_ret __io_sq_thread(struct io_ring_ctx *ctx,
				  unsigned long start_jiffies, bool cap_entries)
{
	unsigned long timeout = start_jiffies + ctx->sq_thread_idle;
	struct io_sq_data *sqd = ctx->sq_data;
	unsigned int to_submit;
	int ret = 0;

again:
	if (!list_empty(&ctx->iopoll_list)) {
		unsigned nr_events = 0;

		mutex_lock(&ctx->uring_lock);
		if (!list_empty(&ctx->iopoll_list) && !need_resched())
			io_do_iopoll(ctx, &nr_events, 0);
		mutex_unlock(&ctx->uring_lock);
	}

	to_submit = io_sqring_entries(ctx);

	/*
	 * If submit got -EBUSY, flag us as needing the application
	 * to enter the kernel to reap and flush events.
	 */
	if (!to_submit || ret == -EBUSY || need_resched()) {
		/*
		 * Drop cur_mm before scheduling, we can't hold it for
		 * long periods (or over schedule()). Do this before
		 * adding ourselves to the waitqueue, as the unuse/drop
		 * may sleep.
		 */
		io_sq_thread_drop_mm();

		/*
		 * We're polling. If we're within the defined idle
		 * period, then let us spin without work before going
		 * to sleep. The exception is if we got EBUSY doing
		 * more IO, we should wait for the application to
		 * reap events and wake us up.
		 */
		if (!list_empty(&ctx->iopoll_list) || need_resched() ||
		    (!time_after(jiffies, timeout) && ret != -EBUSY &&
		    !percpu_ref_is_dying(&ctx->refs)))
			return SQT_SPIN;

		prepare_to_wait(&sqd->wait, &ctx->sqo_wait_entry,
					TASK_INTERRUPTIBLE);

		/*
		 * While doing polled IO, before going to sleep, we need
		 * to check if there are new reqs added to iopoll_list,
		 * it is because reqs may have been punted to io worker
		 * and will be added to iopoll_list later, hence check
		 * the iopoll_list again.
		 */
		if ((ctx->flags & IORING_SETUP_IOPOLL) &&
		    !list_empty_careful(&ctx->iopoll_list)) {
			finish_wait(&sqd->wait, &ctx->sqo_wait_entry);
			goto again;
		}

		to_submit = io_sqring_entries(ctx);
		if (!to_submit || ret == -EBUSY)
			return SQT_IDLE;
	}

	finish_wait(&sqd->wait, &ctx->sqo_wait_entry);
	io_ring_clear_wakeup_flag(ctx);

	/* if we're handling multiple rings, cap submit size for fairness */
	if (cap_entries && to_submit > 8)
		to_submit = 8;

	mutex_lock(&ctx->uring_lock);
	if (likely(!percpu_ref_is_dying(&ctx->refs)))
		ret = io_submit_sqes(ctx, to_submit);
	mutex_unlock(&ctx->uring_lock);

	if (!io_sqring_full(ctx) && wq_has_sleeper(&ctx->sqo_sq_wait))
		wake_up(&ctx->sqo_sq_wait);

	return SQT_DID_WORK;
}

static void io_sqd_init_new(struct io_sq_data *sqd)
{
	struct io_ring_ctx *ctx;

	while (!list_empty(&sqd->ctx_new_list)) {
		ctx = list_first_entry(&sqd->ctx_new_list, struct io_ring_ctx, sqd_list);
		init_wait(&ctx->sqo_wait_entry);
		ctx->sqo_wait_entry.func = io_sq_wake_function;
		list_move_tail(&ctx->sqd_list, &sqd->ctx_list);
		complete(&ctx->sq_thread_comp);
	}
}

static int io_sq_thread(void *data)
{
	struct cgroup_subsys_state *cur_css = NULL;
	const struct cred *old_cred = NULL;
	struct io_sq_data *sqd = data;
	struct io_ring_ctx *ctx;
	unsigned long start_jiffies;

	start_jiffies = jiffies;
	while (!kthread_should_stop()) {
		enum sq_ret ret = 0;
		bool cap_entries;

		/*
		 * Any changes to the sqd lists are synchronized through the
		 * kthread parking. This synchronizes the thread vs users,
		 * the users are synchronized on the sqd->ctx_lock.
		 */
		if (kthread_should_park())
			kthread_parkme();

		if (unlikely(!list_empty(&sqd->ctx_new_list)))
			io_sqd_init_new(sqd);

		cap_entries = !list_is_singular(&sqd->ctx_list);

		list_for_each_entry(ctx, &sqd->ctx_list, sqd_list) {
			if (current->cred != ctx->creds) {
				if (old_cred)
					revert_creds(old_cred);
				old_cred = override_creds(ctx->creds);
			}
			io_sq_thread_associate_blkcg(ctx, &cur_css);
#ifdef CONFIG_AUDIT
			current->loginuid = ctx->loginuid;
			current->sessionid = ctx->sessionid;
#endif

			ret |= __io_sq_thread(ctx, start_jiffies, cap_entries);

			io_sq_thread_drop_mm();
		}

		if (ret & SQT_SPIN) {
			io_run_task_work();
			cond_resched();
		} else if (ret == SQT_IDLE) {
			if (kthread_should_park())
				continue;
			list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
				io_ring_set_wakeup_flag(ctx);
			schedule();
			start_jiffies = jiffies;
			list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
				io_ring_clear_wakeup_flag(ctx);
		}
	}

	io_run_task_work();

	if (cur_css)
		io_sq_thread_unassociate_blkcg();
	if (old_cred)
		revert_creds(old_cred);

	kthread_parkme();

	return 0;
}

struct io_wait_queue {
	struct wait_queue_entry wq;
	struct io_ring_ctx *ctx;
	unsigned to_wait;
	unsigned nr_timeouts;
};

static inline bool io_should_wake(struct io_wait_queue *iowq, bool noflush)
{
	struct io_ring_ctx *ctx = iowq->ctx;

	/*
	 * Wake up if we have enough events, or if a timeout occurred since we
	 * started waiting. For timeouts, we always want to return to userspace,
	 * regardless of event count.
	 */
	return io_cqring_events(ctx, noflush) >= iowq->to_wait ||
			atomic_read(&ctx->cq_timeouts) != iowq->nr_timeouts;
}

static int io_wake_function(struct wait_queue_entry *curr, unsigned int mode,
			    int wake_flags, void *key)
{
	struct io_wait_queue *iowq = container_of(curr, struct io_wait_queue,
							wq);

	/* use noflush == true, as we can't safely rely on locking context */
	if (!io_should_wake(iowq, true))
		return -1;

	return autoremove_wake_function(curr, mode, wake_flags, key);
}

static int io_run_task_work_sig(void)
{
	if (io_run_task_work())
		return 1;
	if (!signal_pending(current))
		return 0;
	if (current->jobctl & JOBCTL_TASK_WORK) {
		spin_lock_irq(&current->sighand->siglock);
		current->jobctl &= ~JOBCTL_TASK_WORK;
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
		return 1;
	}
	return -EINTR;
}

/*
 * Wait until events become available, if we don't already have some. The
 * application must reap them itself, as they reside on the shared cq ring.
 */
static int io_cqring_wait(struct io_ring_ctx *ctx, int min_events,
			  const sigset_t __user *sig, size_t sigsz)
{
	struct io_wait_queue iowq = {
		.wq = {
			.private	= current,
			.func		= io_wake_function,
			.entry		= LIST_HEAD_INIT(iowq.wq.entry),
		},
		.ctx		= ctx,
		.to_wait	= min_events,
	};
	struct io_rings *rings = ctx->rings;
	int ret = 0;

	do {
		if (io_cqring_events(ctx, false) >= min_events)
			return 0;
		if (!io_run_task_work())
			break;
	} while (1);

	if (sig) {
#ifdef CONFIG_COMPAT
		if (in_compat_syscall())
			ret = set_compat_user_sigmask((const compat_sigset_t __user *)sig,
						      sigsz);
		else
#endif
			ret = set_user_sigmask(sig, sigsz);

		if (ret)
			return ret;
	}

	iowq.nr_timeouts = atomic_read(&ctx->cq_timeouts);
	trace_io_uring_cqring_wait(ctx, min_events);
	do {
		prepare_to_wait_exclusive(&ctx->wait, &iowq.wq,
						TASK_INTERRUPTIBLE);
		/* make sure we run task_work before checking for signals */
		ret = io_run_task_work_sig();
		if (ret > 0)
			continue;
		else if (ret < 0)
			break;
		if (io_should_wake(&iowq, false))
			break;
		schedule();
	} while (1);
	finish_wait(&ctx->wait, &iowq.wq);

	restore_saved_sigmask_unless(ret == -EINTR);

	return READ_ONCE(rings->cq.head) == READ_ONCE(rings->cq.tail) ? ret : 0;
}

static void __io_sqe_files_unregister(struct io_ring_ctx *ctx)
{
#if defined(CONFIG_UNIX)
	if (ctx->ring_sock) {
		struct sock *sock = ctx->ring_sock->sk;
		struct sk_buff *skb;

		while ((skb = skb_dequeue(&sock->sk_receive_queue)) != NULL)
			kfree_skb(skb);
	}
#else
	int i;

	for (i = 0; i < ctx->nr_user_files; i++) {
		struct file *file;

		file = io_file_from_index(ctx, i);
		if (file)
			fput(file);
	}
#endif
}

static void io_file_ref_kill(struct percpu_ref *ref)
{
	struct fixed_file_data *data;

	data = container_of(ref, struct fixed_file_data, refs);
	complete(&data->done);
}

static int io_sqe_files_unregister(struct io_ring_ctx *ctx)
{
	struct fixed_file_data *data = ctx->file_data;
	struct fixed_file_ref_node *ref_node = NULL;
	unsigned nr_tables, i;

	if (!data)
		return -ENXIO;

	spin_lock(&data->lock);
	ref_node = data->node;
	spin_unlock(&data->lock);
	if (ref_node)
		percpu_ref_kill(&ref_node->refs);

	percpu_ref_kill(&data->refs);

	/* wait for all refs nodes to complete */
	flush_delayed_work(&ctx->file_put_work);
	wait_for_completion(&data->done);

	__io_sqe_files_unregister(ctx);
	nr_tables = DIV_ROUND_UP(ctx->nr_user_files, IORING_MAX_FILES_TABLE);
	for (i = 0; i < nr_tables; i++)
		kfree(data->table[i].files);
	kfree(data->table);
	percpu_ref_exit(&data->refs);
	kfree(data);
	ctx->file_data = NULL;
	ctx->nr_user_files = 0;
	return 0;
}

static void io_put_sq_data(struct io_sq_data *sqd)
{
	if (refcount_dec_and_test(&sqd->refs)) {
		/*
		 * The park is a bit of a work-around, without it we get
		 * warning spews on shutdown with SQPOLL set and affinity
		 * set to a single CPU.
		 */
		if (sqd->thread) {
			kthread_park(sqd->thread);
			kthread_stop(sqd->thread);
		}

		kfree(sqd);
	}
}

static struct io_sq_data *io_attach_sq_data(struct io_uring_params *p)
{
	struct io_ring_ctx *ctx_attach;
	struct io_sq_data *sqd;
	struct fd f;

	f = fdget(p->wq_fd);
	if (!f.file)
		return ERR_PTR(-ENXIO);
	if (f.file->f_op != &io_uring_fops) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	ctx_attach = f.file->private_data;
	sqd = ctx_attach->sq_data;
	if (!sqd) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	refcount_inc(&sqd->refs);
	fdput(f);
	return sqd;
}

static struct io_sq_data *io_get_sq_data(struct io_uring_params *p)
{
	struct io_sq_data *sqd;

	if (p->flags & IORING_SETUP_ATTACH_WQ)
		return io_attach_sq_data(p);

	sqd = kzalloc(sizeof(*sqd), GFP_KERNEL);
	if (!sqd)
		return ERR_PTR(-ENOMEM);

	refcount_set(&sqd->refs, 1);
	INIT_LIST_HEAD(&sqd->ctx_list);
	INIT_LIST_HEAD(&sqd->ctx_new_list);
	mutex_init(&sqd->ctx_lock);
	mutex_init(&sqd->lock);
	init_waitqueue_head(&sqd->wait);
	return sqd;
}

static void io_sq_thread_unpark(struct io_sq_data *sqd)
	__releases(&sqd->lock)
{
	if (!sqd->thread)
		return;
	kthread_unpark(sqd->thread);
	mutex_unlock(&sqd->lock);
}

static void io_sq_thread_park(struct io_sq_data *sqd)
	__acquires(&sqd->lock)
{
	if (!sqd->thread)
		return;
	mutex_lock(&sqd->lock);
	kthread_park(sqd->thread);
}

static void io_sq_thread_stop(struct io_ring_ctx *ctx)
{
	struct io_sq_data *sqd = ctx->sq_data;

	if (sqd) {
		if (sqd->thread) {
			/*
			 * We may arrive here from the error branch in
			 * io_sq_offload_create() where the kthread is created
			 * without being waked up, thus wake it up now to make
			 * sure the wait will complete.
			 */
			wake_up_process(sqd->thread);
			wait_for_completion(&ctx->sq_thread_comp);

			io_sq_thread_park(sqd);
		}

		mutex_lock(&sqd->ctx_lock);
		list_del(&ctx->sqd_list);
		mutex_unlock(&sqd->ctx_lock);

		if (sqd->thread) {
			finish_wait(&sqd->wait, &ctx->sqo_wait_entry);
			io_sq_thread_unpark(sqd);
		}

		io_put_sq_data(sqd);
		ctx->sq_data = NULL;
	}
}

static void io_finish_async(struct io_ring_ctx *ctx)
{
	io_sq_thread_stop(ctx);

	if (ctx->io_wq) {
		io_wq_destroy(ctx->io_wq);
		ctx->io_wq = NULL;
	}
}

#if defined(CONFIG_UNIX)
/*
 * Ensure the UNIX gc is aware of our file set, so we are certain that
 * the io_uring can be safely unregistered on process exit, even if we have
 * loops in the file referencing.
 */
static int __io_sqe_files_scm(struct io_ring_ctx *ctx, int nr, int offset)
{
	struct sock *sk = ctx->ring_sock->sk;
	struct scm_fp_list *fpl;
	struct sk_buff *skb;
	int i, nr_files;

	fpl = kzalloc(sizeof(*fpl), GFP_KERNEL);
	if (!fpl)
		return -ENOMEM;

	skb = alloc_skb(0, GFP_KERNEL);
	if (!skb) {
		kfree(fpl);
		return -ENOMEM;
	}

	skb->sk = sk;

	nr_files = 0;
	fpl->user = get_uid(ctx->user);
	for (i = 0; i < nr; i++) {
		struct file *file = io_file_from_index(ctx, i + offset);

		if (!file)
			continue;
		fpl->fp[nr_files] = get_file(file);
		unix_inflight(fpl->user, fpl->fp[nr_files]);
		nr_files++;
	}

	if (nr_files) {
		fpl->max = SCM_MAX_FD;
		fpl->count = nr_files;
		UNIXCB(skb).fp = fpl;
		skb->destructor = unix_destruct_scm;
		refcount_add(skb->truesize, &sk->sk_wmem_alloc);
		skb_queue_head(&sk->sk_receive_queue, skb);

		for (i = 0; i < nr_files; i++)
			fput(fpl->fp[i]);
	} else {
		kfree_skb(skb);
		kfree(fpl);
	}

	return 0;
}

/*
 * If UNIX sockets are enabled, fd passing can cause a reference cycle which
 * causes regular reference counting to break down. We rely on the UNIX
 * garbage collection to take care of this problem for us.
 */
static int io_sqe_files_scm(struct io_ring_ctx *ctx)
{
	unsigned left, total;
	int ret = 0;

	total = 0;
	left = ctx->nr_user_files;
	while (left) {
		unsigned this_files = min_t(unsigned, left, SCM_MAX_FD);

		ret = __io_sqe_files_scm(ctx, this_files, total);
		if (ret)
			break;
		left -= this_files;
		total += this_files;
	}

	if (!ret)
		return 0;

	while (total < ctx->nr_user_files) {
		struct file *file = io_file_from_index(ctx, total);

		if (file)
			fput(file);
		total++;
	}

	return ret;
}
#else
static int io_sqe_files_scm(struct io_ring_ctx *ctx)
{
	return 0;
}
#endif

static int io_sqe_alloc_file_tables(struct fixed_file_data *file_data,
				    unsigned nr_tables, unsigned nr_files)
{
	int i;

	for (i = 0; i < nr_tables; i++) {
		struct fixed_file_table *table = &file_data->table[i];
		unsigned this_files;

		this_files = min(nr_files, IORING_MAX_FILES_TABLE);
		table->files = kcalloc(this_files, sizeof(struct file *),
					GFP_KERNEL);
		if (!table->files)
			break;
		nr_files -= this_files;
	}

	if (i == nr_tables)
		return 0;

	for (i = 0; i < nr_tables; i++) {
		struct fixed_file_table *table = &file_data->table[i];
		kfree(table->files);
	}
	return 1;
}

static void io_ring_file_put(struct io_ring_ctx *ctx, struct file *file)
{
#if defined(CONFIG_UNIX)
	struct sock *sock = ctx->ring_sock->sk;
	struct sk_buff_head list, *head = &sock->sk_receive_queue;
	struct sk_buff *skb;
	int i;

	__skb_queue_head_init(&list);

	/*
	 * Find the skb that holds this file in its SCM_RIGHTS. When found,
	 * remove this entry and rearrange the file array.
	 */
	skb = skb_dequeue(head);
	while (skb) {
		struct scm_fp_list *fp;

		fp = UNIXCB(skb).fp;
		for (i = 0; i < fp->count; i++) {
			int left;

			if (fp->fp[i] != file)
				continue;

			unix_notinflight(fp->user, fp->fp[i]);
			left = fp->count - 1 - i;
			if (left) {
				memmove(&fp->fp[i], &fp->fp[i + 1],
						left * sizeof(struct file *));
			}
			fp->count--;
			if (!fp->count) {
				kfree_skb(skb);
				skb = NULL;
			} else {
				__skb_queue_tail(&list, skb);
			}
			fput(file);
			file = NULL;
			break;
		}

		if (!file)
			break;

		__skb_queue_tail(&list, skb);

		skb = skb_dequeue(head);
	}

	if (skb_peek(&list)) {
		spin_lock_irq(&head->lock);
		while ((skb = __skb_dequeue(&list)) != NULL)
			__skb_queue_tail(head, skb);
		spin_unlock_irq(&head->lock);
	}
#else
	fput(file);
#endif
}

struct io_file_put {
	struct list_head list;
	struct file *file;
};

static void __io_file_put_work(struct fixed_file_ref_node *ref_node)
{
	struct fixed_file_data *file_data = ref_node->file_data;
	struct io_ring_ctx *ctx = file_data->ctx;
	struct io_file_put *pfile, *tmp;

	list_for_each_entry_safe(pfile, tmp, &ref_node->file_list, list) {
		list_del(&pfile->list);
		io_ring_file_put(ctx, pfile->file);
		kfree(pfile);
	}

	percpu_ref_exit(&ref_node->refs);
	kfree(ref_node);
	percpu_ref_put(&file_data->refs);
}

static void io_file_put_work(struct work_struct *work)
{
	struct io_ring_ctx *ctx;
	struct llist_node *node;

	ctx = container_of(work, struct io_ring_ctx, file_put_work.work);
	node = llist_del_all(&ctx->file_put_llist);

	while (node) {
		struct fixed_file_ref_node *ref_node;
		struct llist_node *next = node->next;

		ref_node = llist_entry(node, struct fixed_file_ref_node, llist);
		__io_file_put_work(ref_node);
		node = next;
	}
}

static void io_file_data_ref_zero(struct percpu_ref *ref)
{
	struct fixed_file_ref_node *ref_node;
	struct fixed_file_data *data;
	struct io_ring_ctx *ctx;
	bool first_add = false;
	int delay = HZ;

	ref_node = container_of(ref, struct fixed_file_ref_node, refs);
	data = ref_node->file_data;
	ctx = data->ctx;

	spin_lock(&data->lock);
	ref_node->done = true;

	while (!list_empty(&data->ref_list)) {
		ref_node = list_first_entry(&data->ref_list,
					struct fixed_file_ref_node, node);
		/* recycle ref nodes in order */
		if (!ref_node->done)
			break;
		list_del(&ref_node->node);
		first_add |= llist_add(&ref_node->llist, &ctx->file_put_llist);
	}
	spin_unlock(&data->lock);

	if (percpu_ref_is_dying(&data->refs))
		delay = 0;

	if (!delay)
		mod_delayed_work(system_wq, &ctx->file_put_work, 0);
	else if (first_add)
		queue_delayed_work(system_wq, &ctx->file_put_work, delay);
}

static struct fixed_file_ref_node *alloc_fixed_file_ref_node(
			struct io_ring_ctx *ctx)
{
	struct fixed_file_ref_node *ref_node;

	ref_node = kzalloc(sizeof(*ref_node), GFP_KERNEL);
	if (!ref_node)
		return ERR_PTR(-ENOMEM);

	if (percpu_ref_init(&ref_node->refs, io_file_data_ref_zero,
			    0, GFP_KERNEL)) {
		kfree(ref_node);
		return ERR_PTR(-ENOMEM);
	}
	INIT_LIST_HEAD(&ref_node->node);
	INIT_LIST_HEAD(&ref_node->file_list);
	ref_node->file_data = ctx->file_data;
	ref_node->done = false;
	return ref_node;
}

static void destroy_fixed_file_ref_node(struct fixed_file_ref_node *ref_node)
{
	percpu_ref_exit(&ref_node->refs);
	kfree(ref_node);
}

static int io_sqe_files_register(struct io_ring_ctx *ctx, void __user *arg,
				 unsigned nr_args)
{
	__s32 __user *fds = (__s32 __user *) arg;
	unsigned nr_tables, i;
	struct file *file;
	int fd, ret = -ENOMEM;
	struct fixed_file_ref_node *ref_node;
	struct fixed_file_data *file_data;

	if (ctx->file_data)
		return -EBUSY;
	if (!nr_args)
		return -EINVAL;
	if (nr_args > IORING_MAX_FIXED_FILES)
		return -EMFILE;

	file_data = kzalloc(sizeof(*ctx->file_data), GFP_KERNEL);
	if (!file_data)
		return -ENOMEM;
	file_data->ctx = ctx;
	init_completion(&file_data->done);
	INIT_LIST_HEAD(&file_data->ref_list);
	spin_lock_init(&file_data->lock);

	nr_tables = DIV_ROUND_UP(nr_args, IORING_MAX_FILES_TABLE);
	file_data->table = kcalloc(nr_tables, sizeof(*file_data->table),
				   GFP_KERNEL);
	if (!file_data->table)
		goto out_free;

	if (percpu_ref_init(&file_data->refs, io_file_ref_kill,
				PERCPU_REF_ALLOW_REINIT, GFP_KERNEL))
		goto out_free;

	if (io_sqe_alloc_file_tables(file_data, nr_tables, nr_args))
		goto out_ref;
	ctx->file_data = file_data;

	for (i = 0; i < nr_args; i++, ctx->nr_user_files++) {
		struct fixed_file_table *table;
		unsigned index;

		if (copy_from_user(&fd, &fds[i], sizeof(fd))) {
			ret = -EFAULT;
			goto out_fput;
		}
		/* allow sparse sets */
		if (fd == -1)
			continue;

		file = fget(fd);
		ret = -EBADF;
		if (!file)
			goto out_fput;

		/*
		 * Don't allow io_uring instances to be registered. If UNIX
		 * isn't enabled, then this causes a reference cycle and this
		 * instance can never get freed. If UNIX is enabled we'll
		 * handle it just fine, but there's still no point in allowing
		 * a ring fd as it doesn't support regular read/write anyway.
		 */
		if (file->f_op == &io_uring_fops) {
			fput(file);
			goto out_fput;
		}
		table = &file_data->table[i >> IORING_FILE_TABLE_SHIFT];
		index = i & IORING_FILE_TABLE_MASK;
		table->files[index] = file;
	}

	ret = io_sqe_files_scm(ctx);
	if (ret) {
		io_sqe_files_unregister(ctx);
		return ret;
	}

	ref_node = alloc_fixed_file_ref_node(ctx);
	if (IS_ERR(ref_node)) {
		io_sqe_files_unregister(ctx);
		return PTR_ERR(ref_node);
	}

	file_data->node = ref_node;
	spin_lock(&file_data->lock);
	list_add_tail(&ref_node->node, &file_data->ref_list);
	spin_unlock(&file_data->lock);
	percpu_ref_get(&file_data->refs);
	return ret;
out_fput:
	for (i = 0; i < ctx->nr_user_files; i++) {
		file = io_file_from_index(ctx, i);
		if (file)
			fput(file);
	}
	for (i = 0; i < nr_tables; i++)
		kfree(file_data->table[i].files);
	ctx->nr_user_files = 0;
out_ref:
	percpu_ref_exit(&file_data->refs);
out_free:
	kfree(file_data->table);
	kfree(file_data);
	ctx->file_data = NULL;
	return ret;
}

static int io_sqe_file_register(struct io_ring_ctx *ctx, struct file *file,
				int index)
{
#if defined(CONFIG_UNIX)
	struct sock *sock = ctx->ring_sock->sk;
	struct sk_buff_head *head = &sock->sk_receive_queue;
	struct sk_buff *skb;

	/*
	 * See if we can merge this file into an existing skb SCM_RIGHTS
	 * file set. If there's no room, fall back to allocating a new skb
	 * and filling it in.
	 */
	spin_lock_irq(&head->lock);
	skb = skb_peek(head);
	if (skb) {
		struct scm_fp_list *fpl = UNIXCB(skb).fp;

		if (fpl->count < SCM_MAX_FD) {
			__skb_unlink(skb, head);
			spin_unlock_irq(&head->lock);
			fpl->fp[fpl->count] = get_file(file);
			unix_inflight(fpl->user, fpl->fp[fpl->count]);
			fpl->count++;
			spin_lock_irq(&head->lock);
			__skb_queue_head(head, skb);
		} else {
			skb = NULL;
		}
	}
	spin_unlock_irq(&head->lock);

	if (skb) {
		fput(file);
		return 0;
	}

	return __io_sqe_files_scm(ctx, 1, index);
#else
	return 0;
#endif
}

static int io_queue_file_removal(struct fixed_file_data *data,
				 struct file *file)
{
	struct io_file_put *pfile;
	struct fixed_file_ref_node *ref_node = data->node;

	pfile = kzalloc(sizeof(*pfile), GFP_KERNEL);
	if (!pfile)
		return -ENOMEM;

	pfile->file = file;
	list_add(&pfile->list, &ref_node->file_list);

	return 0;
}

static int __io_sqe_files_update(struct io_ring_ctx *ctx,
				 struct io_uring_files_update *up,
				 unsigned nr_args)
{
	struct fixed_file_data *data = ctx->file_data;
	struct fixed_file_ref_node *ref_node;
	struct file *file;
	__s32 __user *fds;
	int fd, i, err;
	__u32 done;
	bool needs_switch = false;

	if (check_add_overflow(up->offset, nr_args, &done))
		return -EOVERFLOW;
	if (done > ctx->nr_user_files)
		return -EINVAL;

	ref_node = alloc_fixed_file_ref_node(ctx);
	if (IS_ERR(ref_node))
		return PTR_ERR(ref_node);

	done = 0;
	fds = u64_to_user_ptr(up->fds);
	while (nr_args) {
		struct fixed_file_table *table;
		unsigned index;

		err = 0;
		if (copy_from_user(&fd, &fds[done], sizeof(fd))) {
			err = -EFAULT;
			break;
		}
		i = array_index_nospec(up->offset, ctx->nr_user_files);
		table = &ctx->file_data->table[i >> IORING_FILE_TABLE_SHIFT];
		index = i & IORING_FILE_TABLE_MASK;
		if (table->files[index]) {
			file = table->files[index];
			err = io_queue_file_removal(data, file);
			if (err)
				break;
			table->files[index] = NULL;
			needs_switch = true;
		}
		if (fd != -1) {
			file = fget(fd);
			if (!file) {
				err = -EBADF;
				break;
			}
			/*
			 * Don't allow io_uring instances to be registered. If
			 * UNIX isn't enabled, then this causes a reference
			 * cycle and this instance can never get freed. If UNIX
			 * is enabled we'll handle it just fine, but there's
			 * still no point in allowing a ring fd as it doesn't
			 * support regular read/write anyway.
			 */
			if (file->f_op == &io_uring_fops) {
				fput(file);
				err = -EBADF;
				break;
			}
			table->files[index] = file;
			err = io_sqe_file_register(ctx, file, i);
			if (err) {
				table->files[index] = NULL;
				fput(file);
				break;
			}
		}
		nr_args--;
		done++;
		up->offset++;
	}

	if (needs_switch) {
		percpu_ref_kill(&data->node->refs);
		spin_lock(&data->lock);
		list_add_tail(&ref_node->node, &data->ref_list);
		data->node = ref_node;
		spin_unlock(&data->lock);
		percpu_ref_get(&ctx->file_data->refs);
	} else
		destroy_fixed_file_ref_node(ref_node);

	return done ? done : err;
}

static int io_sqe_files_update(struct io_ring_ctx *ctx, void __user *arg,
			       unsigned nr_args)
{
	struct io_uring_files_update up;

	if (!ctx->file_data)
		return -ENXIO;
	if (!nr_args)
		return -EINVAL;
	if (copy_from_user(&up, arg, sizeof(up)))
		return -EFAULT;
	if (up.resv)
		return -EINVAL;

	return __io_sqe_files_update(ctx, &up, nr_args);
}

static void io_free_work(struct io_wq_work *work)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);

	/* Consider that io_steal_work() relies on this ref */
	io_put_req(req);
}

static int io_init_wq_offload(struct io_ring_ctx *ctx,
			      struct io_uring_params *p)
{
	struct io_wq_data data;
	struct fd f;
	struct io_ring_ctx *ctx_attach;
	unsigned int concurrency;
	int ret = 0;

	data.user = ctx->user;
	data.free_work = io_free_work;
	data.do_work = io_wq_submit_work;

	if (!(p->flags & IORING_SETUP_ATTACH_WQ)) {
		/* Do QD, or 4 * CPUS, whatever is smallest */
		concurrency = min(ctx->sq_entries, 4 * num_online_cpus());

		ctx->io_wq = io_wq_create(concurrency, &data);
		if (IS_ERR(ctx->io_wq)) {
			ret = PTR_ERR(ctx->io_wq);
			ctx->io_wq = NULL;
		}
		return ret;
	}

	f = fdget(p->wq_fd);
	if (!f.file)
		return -EBADF;

	if (f.file->f_op != &io_uring_fops) {
		ret = -EINVAL;
		goto out_fput;
	}

	ctx_attach = f.file->private_data;
	/* @io_wq is protected by holding the fd */
	if (!io_wq_get(ctx_attach->io_wq, &data)) {
		ret = -EINVAL;
		goto out_fput;
	}

	ctx->io_wq = ctx_attach->io_wq;
out_fput:
	fdput(f);
	return ret;
}

static int io_uring_alloc_task_context(struct task_struct *task)
{
	struct io_uring_task *tctx;
	int ret;

	tctx = kmalloc(sizeof(*tctx), GFP_KERNEL);
	if (unlikely(!tctx))
		return -ENOMEM;

	ret = percpu_counter_init(&tctx->inflight, 0, GFP_KERNEL);
	if (unlikely(ret)) {
		kfree(tctx);
		return ret;
	}

	xa_init(&tctx->xa);
	init_waitqueue_head(&tctx->wait);
	tctx->last = NULL;
	atomic_set(&tctx->in_idle, 0);
	tctx->sqpoll = false;
	io_init_identity(&tctx->__identity);
	tctx->identity = &tctx->__identity;
	task->io_uring = tctx;
	return 0;
}

void __io_uring_free(struct task_struct *tsk)
{
	struct io_uring_task *tctx = tsk->io_uring;

	WARN_ON_ONCE(!xa_empty(&tctx->xa));
	WARN_ON_ONCE(refcount_read(&tctx->identity->count) != 1);
	if (tctx->identity != &tctx->__identity)
		kfree(tctx->identity);
	percpu_counter_destroy(&tctx->inflight);
	kfree(tctx);
	tsk->io_uring = NULL;
}

static int io_sq_offload_create(struct io_ring_ctx *ctx,
				struct io_uring_params *p)
{
	int ret;

	if (ctx->flags & IORING_SETUP_SQPOLL) {
		struct io_sq_data *sqd;

		ret = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto err;

		sqd = io_get_sq_data(p);
		if (IS_ERR(sqd)) {
			ret = PTR_ERR(sqd);
			goto err;
		}

		ctx->sq_data = sqd;
		io_sq_thread_park(sqd);
		mutex_lock(&sqd->ctx_lock);
		list_add(&ctx->sqd_list, &sqd->ctx_new_list);
		mutex_unlock(&sqd->ctx_lock);
		io_sq_thread_unpark(sqd);

		ctx->sq_thread_idle = msecs_to_jiffies(p->sq_thread_idle);
		if (!ctx->sq_thread_idle)
			ctx->sq_thread_idle = HZ;

		if (sqd->thread)
			goto done;

		if (p->flags & IORING_SETUP_SQ_AFF) {
			int cpu = p->sq_thread_cpu;

			ret = -EINVAL;
			if (cpu >= nr_cpu_ids)
				goto err;
			if (!cpu_online(cpu))
				goto err;

			sqd->thread = kthread_create_on_cpu(io_sq_thread, sqd,
							cpu, "io_uring-sq");
		} else {
			sqd->thread = kthread_create(io_sq_thread, sqd,
							"io_uring-sq");
		}
		if (IS_ERR(sqd->thread)) {
			ret = PTR_ERR(sqd->thread);
			sqd->thread = NULL;
			goto err;
		}
		ret = io_uring_alloc_task_context(sqd->thread);
		if (ret)
			goto err;
	} else if (p->flags & IORING_SETUP_SQ_AFF) {
		/* Can't have SQ_AFF without SQPOLL */
		ret = -EINVAL;
		goto err;
	}

done:
	ret = io_init_wq_offload(ctx, p);
	if (ret)
		goto err;

	return 0;
err:
	io_finish_async(ctx);
	return ret;
}

static void io_sq_offload_start(struct io_ring_ctx *ctx)
{
	struct io_sq_data *sqd = ctx->sq_data;

	if ((ctx->flags & IORING_SETUP_SQPOLL) && sqd->thread)
		wake_up_process(sqd->thread);
}

static inline void __io_unaccount_mem(struct user_struct *user,
				      unsigned long nr_pages)
{
	atomic_long_sub(nr_pages, &user->locked_vm);
}

static inline int __io_account_mem(struct user_struct *user,
				   unsigned long nr_pages)
{
	unsigned long page_limit, cur_pages, new_pages;

	/* Don't allow more pages than we can safely lock */
	page_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	do {
		cur_pages = atomic_long_read(&user->locked_vm);
		new_pages = cur_pages + nr_pages;
		if (new_pages > page_limit)
			return -ENOMEM;
	} while (atomic_long_cmpxchg(&user->locked_vm, cur_pages,
					new_pages) != cur_pages);

	return 0;
}

static void io_unaccount_mem(struct io_ring_ctx *ctx, unsigned long nr_pages,
			     enum io_mem_account acct)
{
	if (ctx->limit_mem)
		__io_unaccount_mem(ctx->user, nr_pages);

	if (ctx->mm_account) {
		if (acct == ACCT_LOCKED)
			ctx->mm_account->locked_vm -= nr_pages;
		else if (acct == ACCT_PINNED)
			atomic64_sub(nr_pages, &ctx->mm_account->pinned_vm);
	}
}

static int io_account_mem(struct io_ring_ctx *ctx, unsigned long nr_pages,
			  enum io_mem_account acct)
{
	int ret;

	if (ctx->limit_mem) {
		ret = __io_account_mem(ctx->user, nr_pages);
		if (ret)
			return ret;
	}

	if (ctx->mm_account) {
		if (acct == ACCT_LOCKED)
			ctx->mm_account->locked_vm += nr_pages;
		else if (acct == ACCT_PINNED)
			atomic64_add(nr_pages, &ctx->mm_account->pinned_vm);
	}

	return 0;
}

static void io_mem_free(void *ptr)
{
	struct page *page;

	if (!ptr)
		return;

	page = virt_to_head_page(ptr);
	if (put_page_testzero(page))
		free_compound_page(page);
}

static void *io_mem_alloc(size_t size)
{
	gfp_t gfp_flags = GFP_KERNEL | __GFP_ZERO | __GFP_NOWARN | __GFP_COMP |
				__GFP_NORETRY;

	return (void *) __get_free_pages(gfp_flags, get_order(size));
}

static unsigned long rings_size(unsigned sq_entries, unsigned cq_entries,
				size_t *sq_offset)
{
	struct io_rings *rings;
	size_t off, sq_array_size;

	off = struct_size(rings, cqes, cq_entries);
	if (off == SIZE_MAX)
		return SIZE_MAX;

#ifdef CONFIG_SMP
	off = ALIGN(off, SMP_CACHE_BYTES);
	if (off == 0)
		return SIZE_MAX;
#endif

	if (sq_offset)
		*sq_offset = off;

	sq_array_size = array_size(sizeof(u32), sq_entries);
	if (sq_array_size == SIZE_MAX)
		return SIZE_MAX;

	if (check_add_overflow(off, sq_array_size, &off))
		return SIZE_MAX;

	return off;
}

static unsigned long ring_pages(unsigned sq_entries, unsigned cq_entries)
{
	size_t pages;

	pages = (size_t)1 << get_order(
		rings_size(sq_entries, cq_entries, NULL));
	pages += (size_t)1 << get_order(
		array_size(sizeof(struct io_uring_sqe), sq_entries));

	return pages;
}

static int io_sqe_buffer_unregister(struct io_ring_ctx *ctx)
{
	int i, j;

	if (!ctx->user_bufs)
		return -ENXIO;

	for (i = 0; i < ctx->nr_user_bufs; i++) {
		struct io_mapped_ubuf *imu = &ctx->user_bufs[i];

		for (j = 0; j < imu->nr_bvecs; j++)
			unpin_user_page(imu->bvec[j].bv_page);

		if (imu->acct_pages)
			io_unaccount_mem(ctx, imu->acct_pages, ACCT_PINNED);
		kvfree(imu->bvec);
		imu->nr_bvecs = 0;
	}

	kfree(ctx->user_bufs);
	ctx->user_bufs = NULL;
	ctx->nr_user_bufs = 0;
	return 0;
}

static int io_copy_iov(struct io_ring_ctx *ctx, struct iovec *dst,
		       void __user *arg, unsigned index)
{
	struct iovec __user *src;

#ifdef CONFIG_COMPAT
	if (ctx->compat) {
		struct compat_iovec __user *ciovs;
		struct compat_iovec ciov;

		ciovs = (struct compat_iovec __user *) arg;
		if (copy_from_user(&ciov, &ciovs[index], sizeof(ciov)))
			return -EFAULT;

		dst->iov_base = u64_to_user_ptr((u64)ciov.iov_base);
		dst->iov_len = ciov.iov_len;
		return 0;
	}
#endif
	src = (struct iovec __user *) arg;
	if (copy_from_user(dst, &src[index], sizeof(*dst)))
		return -EFAULT;
	return 0;
}

/*
 * Not super efficient, but this is just a registration time. And we do cache
 * the last compound head, so generally we'll only do a full search if we don't
 * match that one.
 *
 * We check if the given compound head page has already been accounted, to
 * avoid double accounting it. This allows us to account the full size of the
 * page, not just the constituent pages of a huge page.
 */
static bool headpage_already_acct(struct io_ring_ctx *ctx, struct page **pages,
				  int nr_pages, struct page *hpage)
{
	int i, j;

	/* check current page array */
	for (i = 0; i < nr_pages; i++) {
		if (!PageCompound(pages[i]))
			continue;
		if (compound_head(pages[i]) == hpage)
			return true;
	}

	/* check previously registered pages */
	for (i = 0; i < ctx->nr_user_bufs; i++) {
		struct io_mapped_ubuf *imu = &ctx->user_bufs[i];

		for (j = 0; j < imu->nr_bvecs; j++) {
			if (!PageCompound(imu->bvec[j].bv_page))
				continue;
			if (compound_head(imu->bvec[j].bv_page) == hpage)
				return true;
		}
	}

	return false;
}

static int io_buffer_account_pin(struct io_ring_ctx *ctx, struct page **pages,
				 int nr_pages, struct io_mapped_ubuf *imu,
				 struct page **last_hpage)
{
	int i, ret;

	for (i = 0; i < nr_pages; i++) {
		if (!PageCompound(pages[i])) {
			imu->acct_pages++;
		} else {
			struct page *hpage;

			hpage = compound_head(pages[i]);
			if (hpage == *last_hpage)
				continue;
			*last_hpage = hpage;
			if (headpage_already_acct(ctx, pages, i, hpage))
				continue;
			imu->acct_pages += page_size(hpage) >> PAGE_SHIFT;
		}
	}

	if (!imu->acct_pages)
		return 0;

	ret = io_account_mem(ctx, imu->acct_pages, ACCT_PINNED);
	if (ret)
		imu->acct_pages = 0;
	return ret;
}

static int io_sqe_buffer_register(struct io_ring_ctx *ctx, void __user *arg,
				  unsigned nr_args)
{
	struct vm_area_struct **vmas = NULL;
	struct page **pages = NULL;
	struct page *last_hpage = NULL;
	int i, j, got_pages = 0;
	int ret = -EINVAL;

	if (ctx->user_bufs)
		return -EBUSY;
	if (!nr_args || nr_args > UIO_MAXIOV)
		return -EINVAL;

	ctx->user_bufs = kcalloc(nr_args, sizeof(struct io_mapped_ubuf),
					GFP_KERNEL);
	if (!ctx->user_bufs)
		return -ENOMEM;

	for (i = 0; i < nr_args; i++) {
		struct io_mapped_ubuf *imu = &ctx->user_bufs[i];
		unsigned long off, start, end, ubuf;
		int pret, nr_pages;
		struct iovec iov;
		size_t size;

		ret = io_copy_iov(ctx, &iov, arg, i);
		if (ret)
			goto err;

		/*
		 * Don't impose further limits on the size and buffer
		 * constraints here, we'll -EINVAL later when IO is
		 * submitted if they are wrong.
		 */
		ret = -EFAULT;
		if (!iov.iov_base || !iov.iov_len)
			goto err;

		/* arbitrary limit, but we need something */
		if (iov.iov_len > SZ_1G)
			goto err;

		ubuf = (unsigned long) iov.iov_base;
		end = (ubuf + iov.iov_len + PAGE_SIZE - 1) >> PAGE_SHIFT;
		start = ubuf >> PAGE_SHIFT;
		nr_pages = end - start;

		ret = 0;
		if (!pages || nr_pages > got_pages) {
			kvfree(vmas);
			kvfree(pages);
			pages = kvmalloc_array(nr_pages, sizeof(struct page *),
						GFP_KERNEL);
			vmas = kvmalloc_array(nr_pages,
					sizeof(struct vm_area_struct *),
					GFP_KERNEL);
			if (!pages || !vmas) {
				ret = -ENOMEM;
				goto err;
			}
			got_pages = nr_pages;
		}

		imu->bvec = kvmalloc_array(nr_pages, sizeof(struct bio_vec),
						GFP_KERNEL);
		ret = -ENOMEM;
		if (!imu->bvec)
			goto err;

		ret = 0;
		mmap_read_lock(current->mm);
		pret = pin_user_pages(ubuf, nr_pages,
				      FOLL_WRITE | FOLL_LONGTERM,
				      pages, vmas);
		if (pret == nr_pages) {
			/* don't support file backed memory */
			for (j = 0; j < nr_pages; j++) {
				struct vm_area_struct *vma = vmas[j];

				if (vma->vm_file &&
				    !is_file_hugepages(vma->vm_file)) {
					ret = -EOPNOTSUPP;
					break;
				}
			}
		} else {
			ret = pret < 0 ? pret : -EFAULT;
		}
		mmap_read_unlock(current->mm);
		if (ret) {
			/*
			 * if we did partial map, or found file backed vmas,
			 * release any pages we did get
			 */
			if (pret > 0)
				unpin_user_pages(pages, pret);
			kvfree(imu->bvec);
			goto err;
		}

		ret = io_buffer_account_pin(ctx, pages, pret, imu, &last_hpage);
		if (ret) {
			unpin_user_pages(pages, pret);
			kvfree(imu->bvec);
			goto err;
		}

		off = ubuf & ~PAGE_MASK;
		size = iov.iov_len;
		for (j = 0; j < nr_pages; j++) {
			size_t vec_len;

			vec_len = min_t(size_t, size, PAGE_SIZE - off);
			imu->bvec[j].bv_page = pages[j];
			imu->bvec[j].bv_len = vec_len;
			imu->bvec[j].bv_offset = off;
			off = 0;
			size -= vec_len;
		}
		/* store original address for later verification */
		imu->ubuf = ubuf;
		imu->len = iov.iov_len;
		imu->nr_bvecs = nr_pages;

		ctx->nr_user_bufs++;
	}
	kvfree(pages);
	kvfree(vmas);
	return 0;
err:
	kvfree(pages);
	kvfree(vmas);
	io_sqe_buffer_unregister(ctx);
	return ret;
}

static int io_eventfd_register(struct io_ring_ctx *ctx, void __user *arg)
{
	__s32 __user *fds = arg;
	int fd;

	if (ctx->cq_ev_fd)
		return -EBUSY;

	if (copy_from_user(&fd, fds, sizeof(*fds)))
		return -EFAULT;

	ctx->cq_ev_fd = eventfd_ctx_fdget(fd);
	if (IS_ERR(ctx->cq_ev_fd)) {
		int ret = PTR_ERR(ctx->cq_ev_fd);
		ctx->cq_ev_fd = NULL;
		return ret;
	}

	return 0;
}

static int io_eventfd_unregister(struct io_ring_ctx *ctx)
{
	if (ctx->cq_ev_fd) {
		eventfd_ctx_put(ctx->cq_ev_fd);
		ctx->cq_ev_fd = NULL;
		return 0;
	}

	return -ENXIO;
}

static int __io_destroy_buffers(int id, void *p, void *data)
{
	struct io_ring_ctx *ctx = data;
	struct io_buffer *buf = p;

	__io_remove_buffers(ctx, buf, id, -1U);
	return 0;
}

static void io_destroy_buffers(struct io_ring_ctx *ctx)
{
	idr_for_each(&ctx->io_buffer_idr, __io_destroy_buffers, ctx);
	idr_destroy(&ctx->io_buffer_idr);
}

static void io_ring_ctx_free(struct io_ring_ctx *ctx)
{
	io_finish_async(ctx);
	io_sqe_buffer_unregister(ctx);

	if (ctx->sqo_task) {
		put_task_struct(ctx->sqo_task);
		ctx->sqo_task = NULL;
		mmdrop(ctx->mm_account);
		ctx->mm_account = NULL;
	}

#ifdef CONFIG_BLK_CGROUP
	if (ctx->sqo_blkcg_css)
		css_put(ctx->sqo_blkcg_css);
#endif

	io_sqe_files_unregister(ctx);
	io_eventfd_unregister(ctx);
	io_destroy_buffers(ctx);
	idr_destroy(&ctx->personality_idr);

#if defined(CONFIG_UNIX)
	if (ctx->ring_sock) {
		ctx->ring_sock->file = NULL; /* so that iput() is called */
		sock_release(ctx->ring_sock);
	}
#endif

	io_mem_free(ctx->rings);
	io_mem_free(ctx->sq_sqes);

	percpu_ref_exit(&ctx->refs);
	free_uid(ctx->user);
	put_cred(ctx->creds);
	kfree(ctx->cancel_hash);
	kmem_cache_free(req_cachep, ctx->fallback_req);
	kfree(ctx);
}

static __poll_t io_uring_poll(struct file *file, poll_table *wait)
{
	struct io_ring_ctx *ctx = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &ctx->cq_wait, wait);
	/*
	 * synchronizes with barrier from wq_has_sleeper call in
	 * io_commit_cqring
	 */
	smp_rmb();
	if (!io_sqring_full(ctx))
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (io_cqring_events(ctx, false))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static int io_uring_fasync(int fd, struct file *file, int on)
{
	struct io_ring_ctx *ctx = file->private_data;

	return fasync_helper(fd, file, on, &ctx->cq_fasync);
}

static int io_remove_personalities(int id, void *p, void *data)
{
	struct io_ring_ctx *ctx = data;
	struct io_identity *iod;

	iod = idr_remove(&ctx->personality_idr, id);
	if (iod) {
		put_cred(iod->creds);
		if (refcount_dec_and_test(&iod->count))
			kfree(iod);
	}
	return 0;
}

static void io_ring_exit_work(struct work_struct *work)
{
	struct io_ring_ctx *ctx = container_of(work, struct io_ring_ctx,
					       exit_work);

	/*
	 * If we're doing polled IO and end up having requests being
	 * submitted async (out-of-line), then completions can come in while
	 * we're waiting for refs to drop. We need to reap these manually,
	 * as nobody else will be looking for them.
	 */
	do {
		io_iopoll_try_reap_events(ctx);
	} while (!wait_for_completion_timeout(&ctx->ref_comp, HZ/20));
	io_ring_ctx_free(ctx);
}

static bool io_cancel_ctx_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);

	return req->ctx == data;
}

static void io_ring_ctx_wait_and_kill(struct io_ring_ctx *ctx)
{
	mutex_lock(&ctx->uring_lock);
	percpu_ref_kill(&ctx->refs);
	/* if force is set, the ring is going away. always drop after that */
	ctx->cq_overflow_flushed = 1;
	if (ctx->rings)
		io_cqring_overflow_flush(ctx, true, NULL, NULL);
	mutex_unlock(&ctx->uring_lock);

	io_kill_timeouts(ctx, NULL);
	io_poll_remove_all(ctx, NULL);

	if (ctx->io_wq)
		io_wq_cancel_cb(ctx->io_wq, io_cancel_ctx_cb, ctx, true);

	/* if we failed setting up the ctx, we might not have any rings */
	io_iopoll_try_reap_events(ctx);
	idr_for_each(&ctx->personality_idr, io_remove_personalities, ctx);

	/*
	 * Do this upfront, so we won't have a grace period where the ring
	 * is closed but resources aren't reaped yet. This can cause
	 * spurious failure in setting up a new ring.
	 */
	io_unaccount_mem(ctx, ring_pages(ctx->sq_entries, ctx->cq_entries),
			 ACCT_LOCKED);

	INIT_WORK(&ctx->exit_work, io_ring_exit_work);
	/*
	 * Use system_unbound_wq to avoid spawning tons of event kworkers
	 * if we're exiting a ton of rings at the same time. It just adds
	 * noise and overhead, there's no discernable change in runtime
	 * over using system_wq.
	 */
	queue_work(system_unbound_wq, &ctx->exit_work);
}

static int io_uring_release(struct inode *inode, struct file *file)
{
	struct io_ring_ctx *ctx = file->private_data;

	file->private_data = NULL;
	io_ring_ctx_wait_and_kill(ctx);
	return 0;
}

/*
 * Returns true if 'preq' is the link parent of 'req'
 */
static bool io_match_link(struct io_kiocb *preq, struct io_kiocb *req)
{
	struct io_kiocb *link;

	if (!(preq->flags & REQ_F_LINK_HEAD))
		return false;

	list_for_each_entry(link, &preq->link_list, link_list) {
		if (link == req)
			return true;
	}

	return false;
}

/*
 * We're looking to cancel 'req' because it's holding on to our files, but
 * 'req' could be a link to another request. See if it is, and cancel that
 * parent request if so.
 */
static bool io_poll_remove_link(struct io_ring_ctx *ctx, struct io_kiocb *req)
{
	struct hlist_node *tmp;
	struct io_kiocb *preq;
	bool found = false;
	int i;

	spin_lock_irq(&ctx->completion_lock);
	for (i = 0; i < (1U << ctx->cancel_hash_bits); i++) {
		struct hlist_head *list;

		list = &ctx->cancel_hash[i];
		hlist_for_each_entry_safe(preq, tmp, list, hash_node) {
			found = io_match_link(preq, req);
			if (found) {
				io_poll_remove_one(preq);
				break;
			}
		}
	}
	spin_unlock_irq(&ctx->completion_lock);
	return found;
}

static bool io_timeout_remove_link(struct io_ring_ctx *ctx,
				   struct io_kiocb *req)
{
	struct io_kiocb *preq;
	bool found = false;

	spin_lock_irq(&ctx->completion_lock);
	list_for_each_entry(preq, &ctx->timeout_list, timeout.list) {
		found = io_match_link(preq, req);
		if (found) {
			__io_timeout_cancel(preq);
			break;
		}
	}
	spin_unlock_irq(&ctx->completion_lock);
	return found;
}

static bool io_cancel_link_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	bool ret;

	if (req->flags & REQ_F_LINK_TIMEOUT) {
		unsigned long flags;
		struct io_ring_ctx *ctx = req->ctx;

		/* protect against races with linked timeouts */
		spin_lock_irqsave(&ctx->completion_lock, flags);
		ret = io_match_link(req, data);
		spin_unlock_irqrestore(&ctx->completion_lock, flags);
	} else {
		ret = io_match_link(req, data);
	}
	return ret;
}

static void io_attempt_cancel(struct io_ring_ctx *ctx, struct io_kiocb *req)
{
	enum io_wq_cancel cret;

	/* cancel this particular work, if it's running */
	cret = io_wq_cancel_work(ctx->io_wq, &req->work);
	if (cret != IO_WQ_CANCEL_NOTFOUND)
		return;

	/* find links that hold this pending, cancel those */
	cret = io_wq_cancel_cb(ctx->io_wq, io_cancel_link_cb, req, true);
	if (cret != IO_WQ_CANCEL_NOTFOUND)
		return;

	/* if we have a poll link holding this pending, cancel that */
	if (io_poll_remove_link(ctx, req))
		return;

	/* final option, timeout link is holding this req pending */
	io_timeout_remove_link(ctx, req);
}

static void io_cancel_defer_files(struct io_ring_ctx *ctx,
				  struct task_struct *task,
				  struct files_struct *files)
{
	struct io_defer_entry *de = NULL;
	LIST_HEAD(list);

	spin_lock_irq(&ctx->completion_lock);
	list_for_each_entry_reverse(de, &ctx->defer_list, list) {
		if (io_task_match(de->req, task) &&
		    io_match_files(de->req, files)) {
			list_cut_position(&list, &ctx->defer_list, &de->list);
			break;
		}
	}
	spin_unlock_irq(&ctx->completion_lock);

	while (!list_empty(&list)) {
		de = list_first_entry(&list, struct io_defer_entry, list);
		list_del_init(&de->list);
		req_set_fail_links(de->req);
		io_put_req(de->req);
		io_req_complete(de->req, -ECANCELED);
		kfree(de);
	}
}

/*
 * Returns true if we found and killed one or more files pinning requests
 */
static bool io_uring_cancel_files(struct io_ring_ctx *ctx,
				  struct task_struct *task,
				  struct files_struct *files)
{
	if (list_empty_careful(&ctx->inflight_list))
		return false;

	while (!list_empty_careful(&ctx->inflight_list)) {
		struct io_kiocb *cancel_req = NULL, *req;
		DEFINE_WAIT(wait);

		spin_lock_irq(&ctx->inflight_lock);
		list_for_each_entry(req, &ctx->inflight_list, inflight_entry) {
			if (req->task == task &&
			    (req->work.flags & IO_WQ_WORK_FILES) &&
			    req->work.identity->files != files)
				continue;
			/* req is being completed, ignore */
			if (!refcount_inc_not_zero(&req->refs))
				continue;
			cancel_req = req;
			break;
		}
		if (cancel_req)
			prepare_to_wait(&ctx->inflight_wait, &wait,
						TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&ctx->inflight_lock);

		/* We need to keep going until we don't find a matching req */
		if (!cancel_req)
			break;
		/* cancel this request, or head link requests */
		io_attempt_cancel(ctx, cancel_req);
		io_put_req(cancel_req);
		/* cancellations _may_ trigger task work */
		io_run_task_work();
		schedule();
		finish_wait(&ctx->inflight_wait, &wait);
	}

	return true;
}

static bool io_cancel_task_cb(struct io_wq_work *work, void *data)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct task_struct *task = data;

	return io_task_match(req, task);
}

static bool __io_uring_cancel_task_requests(struct io_ring_ctx *ctx,
					    struct task_struct *task,
					    struct files_struct *files)
{
	bool ret;

	ret = io_uring_cancel_files(ctx, task, files);
	if (!files) {
		enum io_wq_cancel cret;

		cret = io_wq_cancel_cb(ctx->io_wq, io_cancel_task_cb, task, true);
		if (cret != IO_WQ_CANCEL_NOTFOUND)
			ret = true;

		/* SQPOLL thread does its own polling */
		if (!(ctx->flags & IORING_SETUP_SQPOLL)) {
			while (!list_empty_careful(&ctx->iopoll_list)) {
				io_iopoll_try_reap_events(ctx);
				ret = true;
			}
		}

		ret |= io_poll_remove_all(ctx, task);
		ret |= io_kill_timeouts(ctx, task);
	}

	return ret;
}

/*
 * We need to iteratively cancel requests, in case a request has dependent
 * hard links. These persist even for failure of cancelations, hence keep
 * looping until none are found.
 */
static void io_uring_cancel_task_requests(struct io_ring_ctx *ctx,
					  struct files_struct *files)
{
	struct task_struct *task = current;

	if ((ctx->flags & IORING_SETUP_SQPOLL) && ctx->sq_data) {
		task = ctx->sq_data->thread;
		atomic_inc(&task->io_uring->in_idle);
		io_sq_thread_park(ctx->sq_data);
	}

	io_cancel_defer_files(ctx, task, files);
	io_ring_submit_lock(ctx, (ctx->flags & IORING_SETUP_IOPOLL));
	io_cqring_overflow_flush(ctx, true, task, files);
	io_ring_submit_unlock(ctx, (ctx->flags & IORING_SETUP_IOPOLL));

	while (__io_uring_cancel_task_requests(ctx, task, files)) {
		io_run_task_work();
		cond_resched();
	}

	if ((ctx->flags & IORING_SETUP_SQPOLL) && ctx->sq_data) {
		atomic_dec(&task->io_uring->in_idle);
		/*
		 * If the files that are going away are the ones in the thread
		 * identity, clear them out.
		 */
		if (task->io_uring->identity->files == files)
			task->io_uring->identity->files = NULL;
		io_sq_thread_unpark(ctx->sq_data);
	}
}

/*
 * Note that this task has used io_uring. We use it for cancelation purposes.
 */
static int io_uring_add_task_file(struct io_ring_ctx *ctx, struct file *file)
{
	struct io_uring_task *tctx = current->io_uring;
	int ret;

	if (unlikely(!tctx)) {
		ret = io_uring_alloc_task_context(current);
		if (unlikely(ret))
			return ret;
		tctx = current->io_uring;
	}
	if (tctx->last != file) {
		void *old = xa_load(&tctx->xa, (unsigned long)file);

		if (!old) {
			get_file(file);
			ret = xa_err(xa_store(&tctx->xa, (unsigned long)file,
						file, GFP_KERNEL));
			if (ret) {
				fput(file);
				return ret;
			}
		}
		tctx->last = file;
	}

	/*
	 * This is race safe in that the task itself is doing this, hence it
	 * cannot be going through the exit/cancel paths at the same time.
	 * This cannot be modified while exit/cancel is running.
	 */
	if (!tctx->sqpoll && (ctx->flags & IORING_SETUP_SQPOLL))
		tctx->sqpoll = true;

	return 0;
}

/*
 * Remove this io_uring_file -> task mapping.
 */
static void io_uring_del_task_file(struct file *file)
{
	struct io_uring_task *tctx = current->io_uring;

	if (tctx->last == file)
		tctx->last = NULL;
	file = xa_erase(&tctx->xa, (unsigned long)file);
	if (file)
		fput(file);
}

/*
 * Drop task note for this file if we're the only ones that hold it after
 * pending fput()
 */
static void io_uring_attempt_task_drop(struct file *file)
{
	if (!current->io_uring)
		return;
	/*
	 * fput() is pending, will be 2 if the only other ref is our potential
	 * task file note. If the task is exiting, drop regardless of count.
	 */
	if (fatal_signal_pending(current) || (current->flags & PF_EXITING) ||
	    atomic_long_read(&file->f_count) == 2)
		io_uring_del_task_file(file);
}

void __io_uring_files_cancel(struct files_struct *files)
{
	struct io_uring_task *tctx = current->io_uring;
	struct file *file;
	unsigned long index;

	/* make sure overflow events are dropped */
	atomic_inc(&tctx->in_idle);

	xa_for_each(&tctx->xa, index, file) {
		struct io_ring_ctx *ctx = file->private_data;

		io_uring_cancel_task_requests(ctx, files);
		if (files)
			io_uring_del_task_file(file);
	}

	atomic_dec(&tctx->in_idle);
}

static s64 tctx_inflight(struct io_uring_task *tctx)
{
	unsigned long index;
	struct file *file;
	s64 inflight;

	inflight = percpu_counter_sum(&tctx->inflight);
	if (!tctx->sqpoll)
		return inflight;

	/*
	 * If we have SQPOLL rings, then we need to iterate and find them, and
	 * add the pending count for those.
	 */
	xa_for_each(&tctx->xa, index, file) {
		struct io_ring_ctx *ctx = file->private_data;

		if (ctx->flags & IORING_SETUP_SQPOLL) {
			struct io_uring_task *__tctx = ctx->sqo_task->io_uring;

			inflight += percpu_counter_sum(&__tctx->inflight);
		}
	}

	return inflight;
}

/*
 * Find any io_uring fd that this task has registered or done IO on, and cancel
 * requests.
 */
void __io_uring_task_cancel(void)
{
	struct io_uring_task *tctx = current->io_uring;
	DEFINE_WAIT(wait);
	s64 inflight;

	/* make sure overflow events are dropped */
	atomic_inc(&tctx->in_idle);

	do {
		/* read completions before cancelations */
		inflight = tctx_inflight(tctx);
		if (!inflight)
			break;
		__io_uring_files_cancel(NULL);

		prepare_to_wait(&tctx->wait, &wait, TASK_UNINTERRUPTIBLE);

		/*
		 * If we've seen completions, retry. This avoids a race where
		 * a completion comes in before we did prepare_to_wait().
		 */
		if (inflight != tctx_inflight(tctx))
			continue;
		schedule();
	} while (1);

	finish_wait(&tctx->wait, &wait);
	atomic_dec(&tctx->in_idle);
}

static int io_uring_flush(struct file *file, void *data)
{
	io_uring_attempt_task_drop(file);
	return 0;
}

static void *io_uring_validate_mmap_request(struct file *file,
					    loff_t pgoff, size_t sz)
{
	struct io_ring_ctx *ctx = file->private_data;
	loff_t offset = pgoff << PAGE_SHIFT;
	struct page *page;
	void *ptr;

	switch (offset) {
	case IORING_OFF_SQ_RING:
	case IORING_OFF_CQ_RING:
		ptr = ctx->rings;
		break;
	case IORING_OFF_SQES:
		ptr = ctx->sq_sqes;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	page = virt_to_head_page(ptr);
	if (sz > page_size(page))
		return ERR_PTR(-EINVAL);

	return ptr;
}

#ifdef CONFIG_MMU

static int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t sz = vma->vm_end - vma->vm_start;
	unsigned long pfn;
	void *ptr;

	ptr = io_uring_validate_mmap_request(file, vma->vm_pgoff, sz);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	pfn = virt_to_phys(ptr) >> PAGE_SHIFT;
	return remap_pfn_range(vma, vma->vm_start, pfn, sz, vma->vm_page_prot);
}

#else /* !CONFIG_MMU */

static int io_uring_mmap(struct file *file, struct vm_area_struct *vma)
{
	return vma->vm_flags & (VM_SHARED | VM_MAYSHARE) ? 0 : -EINVAL;
}

static unsigned int io_uring_nommu_mmap_capabilities(struct file *file)
{
	return NOMMU_MAP_DIRECT | NOMMU_MAP_READ | NOMMU_MAP_WRITE;
}

static unsigned long io_uring_nommu_get_unmapped_area(struct file *file,
	unsigned long addr, unsigned long len,
	unsigned long pgoff, unsigned long flags)
{
	void *ptr;

	ptr = io_uring_validate_mmap_request(file, pgoff, len);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	return (unsigned long) ptr;
}

#endif /* !CONFIG_MMU */

static void io_sqpoll_wait_sq(struct io_ring_ctx *ctx)
{
	DEFINE_WAIT(wait);

	do {
		if (!io_sqring_full(ctx))
			break;

		prepare_to_wait(&ctx->sqo_sq_wait, &wait, TASK_INTERRUPTIBLE);

		if (!io_sqring_full(ctx))
			break;

		schedule();
	} while (!signal_pending(current));

	finish_wait(&ctx->sqo_sq_wait, &wait);
}

SYSCALL_DEFINE6(io_uring_enter, unsigned int, fd, u32, to_submit,
		u32, min_complete, u32, flags, const sigset_t __user *, sig,
		size_t, sigsz)
{
	struct io_ring_ctx *ctx;
	long ret = -EBADF;
	int submitted = 0;
	struct fd f;

	io_run_task_work();

	if (flags & ~(IORING_ENTER_GETEVENTS | IORING_ENTER_SQ_WAKEUP |
			IORING_ENTER_SQ_WAIT))
		return -EINVAL;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	ret = -EOPNOTSUPP;
	if (f.file->f_op != &io_uring_fops)
		goto out_fput;

	ret = -ENXIO;
	ctx = f.file->private_data;
	if (!percpu_ref_tryget(&ctx->refs))
		goto out_fput;

	ret = -EBADFD;
	if (ctx->flags & IORING_SETUP_R_DISABLED)
		goto out;

	/*
	 * For SQ polling, the thread will do all submissions and completions.
	 * Just return the requested submit count, and wake the thread if
	 * we were asked to.
	 */
	ret = 0;
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		io_ring_submit_lock(ctx, (ctx->flags & IORING_SETUP_IOPOLL));
		if (!list_empty_careful(&ctx->cq_overflow_list))
			io_cqring_overflow_flush(ctx, false, NULL, NULL);
		io_ring_submit_unlock(ctx, (ctx->flags & IORING_SETUP_IOPOLL));
		if (flags & IORING_ENTER_SQ_WAKEUP)
			wake_up(&ctx->sq_data->wait);
		if (flags & IORING_ENTER_SQ_WAIT)
			io_sqpoll_wait_sq(ctx);
		submitted = to_submit;
	} else if (to_submit) {
		ret = io_uring_add_task_file(ctx, f.file);
		if (unlikely(ret))
			goto out;
		mutex_lock(&ctx->uring_lock);
		submitted = io_submit_sqes(ctx, to_submit);
		mutex_unlock(&ctx->uring_lock);

		if (submitted != to_submit)
			goto out;
	}
	if (flags & IORING_ENTER_GETEVENTS) {
		min_complete = min(min_complete, ctx->cq_entries);

		/*
		 * When SETUP_IOPOLL and SETUP_SQPOLL are both enabled, user
		 * space applications don't need to do io completion events
		 * polling again, they can rely on io_sq_thread to do polling
		 * work, which can reduce cpu usage and uring_lock contention.
		 */
		if (ctx->flags & IORING_SETUP_IOPOLL &&
		    !(ctx->flags & IORING_SETUP_SQPOLL)) {
			ret = io_iopoll_check(ctx, min_complete);
		} else {
			ret = io_cqring_wait(ctx, min_complete, sig, sigsz);
		}
	}

out:
	percpu_ref_put(&ctx->refs);
out_fput:
	fdput(f);
	return submitted ? submitted : ret;
}

#ifdef CONFIG_PROC_FS
static int io_uring_show_cred(int id, void *p, void *data)
{
	struct io_identity *iod = p;
	const struct cred *cred = iod->creds;
	struct seq_file *m = data;
	struct user_namespace *uns = seq_user_ns(m);
	struct group_info *gi;
	kernel_cap_t cap;
	unsigned __capi;
	int g;

	seq_printf(m, "%5d\n", id);
	seq_put_decimal_ull(m, "\tUid:\t", from_kuid_munged(uns, cred->uid));
	seq_put_decimal_ull(m, "\t\t", from_kuid_munged(uns, cred->euid));
	seq_put_decimal_ull(m, "\t\t", from_kuid_munged(uns, cred->suid));
	seq_put_decimal_ull(m, "\t\t", from_kuid_munged(uns, cred->fsuid));
	seq_put_decimal_ull(m, "\n\tGid:\t", from_kgid_munged(uns, cred->gid));
	seq_put_decimal_ull(m, "\t\t", from_kgid_munged(uns, cred->egid));
	seq_put_decimal_ull(m, "\t\t", from_kgid_munged(uns, cred->sgid));
	seq_put_decimal_ull(m, "\t\t", from_kgid_munged(uns, cred->fsgid));
	seq_puts(m, "\n\tGroups:\t");
	gi = cred->group_info;
	for (g = 0; g < gi->ngroups; g++) {
		seq_put_decimal_ull(m, g ? " " : "",
					from_kgid_munged(uns, gi->gid[g]));
	}
	seq_puts(m, "\n\tCapEff:\t");
	cap = cred->cap_effective;
	CAP_FOR_EACH_U32(__capi)
		seq_put_hex_ll(m, NULL, cap.cap[CAP_LAST_U32 - __capi], 8);
	seq_putc(m, '\n');
	return 0;
}

static void __io_uring_show_fdinfo(struct io_ring_ctx *ctx, struct seq_file *m)
{
	struct io_sq_data *sq = NULL;
	bool has_lock;
	int i;

	/*
	 * Avoid ABBA deadlock between the seq lock and the io_uring mutex,
	 * since fdinfo case grabs it in the opposite direction of normal use
	 * cases. If we fail to get the lock, we just don't iterate any
	 * structures that could be going away outside the io_uring mutex.
	 */
	has_lock = mutex_trylock(&ctx->uring_lock);

	if (has_lock && (ctx->flags & IORING_SETUP_SQPOLL))
		sq = ctx->sq_data;

	seq_printf(m, "SqThread:\t%d\n", sq ? task_pid_nr(sq->thread) : -1);
	seq_printf(m, "SqThreadCpu:\t%d\n", sq ? task_cpu(sq->thread) : -1);
	seq_printf(m, "UserFiles:\t%u\n", ctx->nr_user_files);
	for (i = 0; has_lock && i < ctx->nr_user_files; i++) {
		struct fixed_file_table *table;
		struct file *f;

		table = &ctx->file_data->table[i >> IORING_FILE_TABLE_SHIFT];
		f = table->files[i & IORING_FILE_TABLE_MASK];
		if (f)
			seq_printf(m, "%5u: %s\n", i, file_dentry(f)->d_iname);
		else
			seq_printf(m, "%5u: <none>\n", i);
	}
	seq_printf(m, "UserBufs:\t%u\n", ctx->nr_user_bufs);
	for (i = 0; has_lock && i < ctx->nr_user_bufs; i++) {
		struct io_mapped_ubuf *buf = &ctx->user_bufs[i];

		seq_printf(m, "%5u: 0x%llx/%u\n", i, buf->ubuf,
						(unsigned int) buf->len);
	}
	if (has_lock && !idr_is_empty(&ctx->personality_idr)) {
		seq_printf(m, "Personalities:\n");
		idr_for_each(&ctx->personality_idr, io_uring_show_cred, m);
	}
	seq_printf(m, "PollList:\n");
	spin_lock_irq(&ctx->completion_lock);
	for (i = 0; i < (1U << ctx->cancel_hash_bits); i++) {
		struct hlist_head *list = &ctx->cancel_hash[i];
		struct io_kiocb *req;

		hlist_for_each_entry(req, list, hash_node)
			seq_printf(m, "  op=%d, task_works=%d\n", req->opcode,
					req->task->task_works != NULL);
	}
	spin_unlock_irq(&ctx->completion_lock);
	if (has_lock)
		mutex_unlock(&ctx->uring_lock);
}

static void io_uring_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct io_ring_ctx *ctx = f->private_data;

	if (percpu_ref_tryget(&ctx->refs)) {
		__io_uring_show_fdinfo(ctx, m);
		percpu_ref_put(&ctx->refs);
	}
}
#endif

static const struct file_operations io_uring_fops = {
	.release	= io_uring_release,
	.flush		= io_uring_flush,
	.mmap		= io_uring_mmap,
#ifndef CONFIG_MMU
	.get_unmapped_area = io_uring_nommu_get_unmapped_area,
	.mmap_capabilities = io_uring_nommu_mmap_capabilities,
#endif
	.poll		= io_uring_poll,
	.fasync		= io_uring_fasync,
#ifdef CONFIG_PROC_FS
	.show_fdinfo	= io_uring_show_fdinfo,
#endif
};

static int io_allocate_scq_urings(struct io_ring_ctx *ctx,
				  struct io_uring_params *p)
{
	struct io_rings *rings;
	size_t size, sq_array_offset;

	/* make sure these are sane, as we already accounted them */
	ctx->sq_entries = p->sq_entries;
	ctx->cq_entries = p->cq_entries;

	size = rings_size(p->sq_entries, p->cq_entries, &sq_array_offset);
	if (size == SIZE_MAX)
		return -EOVERFLOW;

	rings = io_mem_alloc(size);
	if (!rings)
		return -ENOMEM;

	ctx->rings = rings;
	ctx->sq_array = (u32 *)((char *)rings + sq_array_offset);
	rings->sq_ring_mask = p->sq_entries - 1;
	rings->cq_ring_mask = p->cq_entries - 1;
	rings->sq_ring_entries = p->sq_entries;
	rings->cq_ring_entries = p->cq_entries;
	ctx->sq_mask = rings->sq_ring_mask;
	ctx->cq_mask = rings->cq_ring_mask;

	size = array_size(sizeof(struct io_uring_sqe), p->sq_entries);
	if (size == SIZE_MAX) {
		io_mem_free(ctx->rings);
		ctx->rings = NULL;
		return -EOVERFLOW;
	}

	ctx->sq_sqes = io_mem_alloc(size);
	if (!ctx->sq_sqes) {
		io_mem_free(ctx->rings);
		ctx->rings = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int io_uring_install_fd(struct io_ring_ctx *ctx, struct file *file)
{
	int ret, fd;

	fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return fd;

	ret = io_uring_add_task_file(ctx, file);
	if (ret) {
		put_unused_fd(fd);
		return ret;
	}
	fd_install(fd, file);
	return fd;
}

/*
 * Allocate an anonymous fd, this is what constitutes the application
 * visible backing of an io_uring instance. The application mmaps this
 * fd to gain access to the SQ/CQ ring details. If UNIX sockets are enabled,
 * we have to tie this fd to a socket for file garbage collection purposes.
 */
static struct file *io_uring_get_file(struct io_ring_ctx *ctx)
{
	struct file *file;
#if defined(CONFIG_UNIX)
	int ret;

	ret = sock_create_kern(&init_net, PF_UNIX, SOCK_RAW, IPPROTO_IP,
				&ctx->ring_sock);
	if (ret)
		return ERR_PTR(ret);
#endif

	file = anon_inode_getfile("[io_uring]", &io_uring_fops, ctx,
					O_RDWR | O_CLOEXEC);
#if defined(CONFIG_UNIX)
	if (IS_ERR(file)) {
		sock_release(ctx->ring_sock);
		ctx->ring_sock = NULL;
	} else {
		ctx->ring_sock->file = file;
	}
#endif
	return file;
}

static int io_uring_create(unsigned entries, struct io_uring_params *p,
			   struct io_uring_params __user *params)
{
	struct user_struct *user = NULL;
	struct io_ring_ctx *ctx;
	struct file *file;
	bool limit_mem;
	int ret;

	if (!entries)
		return -EINVAL;
	if (entries > IORING_MAX_ENTRIES) {
		if (!(p->flags & IORING_SETUP_CLAMP))
			return -EINVAL;
		entries = IORING_MAX_ENTRIES;
	}

	/*
	 * Use twice as many entries for the CQ ring. It's possible for the
	 * application to drive a higher depth than the size of the SQ ring,
	 * since the sqes are only used at submission time. This allows for
	 * some flexibility in overcommitting a bit. If the application has
	 * set IORING_SETUP_CQSIZE, it will have passed in the desired number
	 * of CQ ring entries manually.
	 */
	p->sq_entries = roundup_pow_of_two(entries);
	if (p->flags & IORING_SETUP_CQSIZE) {
		/*
		 * If IORING_SETUP_CQSIZE is set, we do the same roundup
		 * to a power-of-two, if it isn't already. We do NOT impose
		 * any cq vs sq ring sizing.
		 */
		if (!p->cq_entries)
			return -EINVAL;
		if (p->cq_entries > IORING_MAX_CQ_ENTRIES) {
			if (!(p->flags & IORING_SETUP_CLAMP))
				return -EINVAL;
			p->cq_entries = IORING_MAX_CQ_ENTRIES;
		}
		p->cq_entries = roundup_pow_of_two(p->cq_entries);
		if (p->cq_entries < p->sq_entries)
			return -EINVAL;
	} else {
		p->cq_entries = 2 * p->sq_entries;
	}

	user = get_uid(current_user());
	limit_mem = !capable(CAP_IPC_LOCK);

	if (limit_mem) {
		ret = __io_account_mem(user,
				ring_pages(p->sq_entries, p->cq_entries));
		if (ret) {
			free_uid(user);
			return ret;
		}
	}

	ctx = io_ring_ctx_alloc(p);
	if (!ctx) {
		if (limit_mem)
			__io_unaccount_mem(user, ring_pages(p->sq_entries,
								p->cq_entries));
		free_uid(user);
		return -ENOMEM;
	}
	ctx->compat = in_compat_syscall();
	ctx->user = user;
	ctx->creds = get_current_cred();
#ifdef CONFIG_AUDIT
	ctx->loginuid = current->loginuid;
	ctx->sessionid = current->sessionid;
#endif
	ctx->sqo_task = get_task_struct(current);

	/*
	 * This is just grabbed for accounting purposes. When a process exits,
	 * the mm is exited and dropped before the files, hence we need to hang
	 * on to this mm purely for the purposes of being able to unaccount
	 * memory (locked/pinned vm). It's not used for anything else.
	 */
	mmgrab(current->mm);
	ctx->mm_account = current->mm;

#ifdef CONFIG_BLK_CGROUP
	/*
	 * The sq thread will belong to the original cgroup it was inited in.
	 * If the cgroup goes offline (e.g. disabling the io controller), then
	 * issued bios will be associated with the closest cgroup later in the
	 * block layer.
	 */
	rcu_read_lock();
	ctx->sqo_blkcg_css = blkcg_css();
	ret = css_tryget_online(ctx->sqo_blkcg_css);
	rcu_read_unlock();
	if (!ret) {
		/* don't init against a dying cgroup, have the user try again */
		ctx->sqo_blkcg_css = NULL;
		ret = -ENODEV;
		goto err;
	}
#endif

	/*
	 * Account memory _before_ installing the file descriptor. Once
	 * the descriptor is installed, it can get closed at any time. Also
	 * do this before hitting the general error path, as ring freeing
	 * will un-account as well.
	 */
	io_account_mem(ctx, ring_pages(p->sq_entries, p->cq_entries),
		       ACCT_LOCKED);
	ctx->limit_mem = limit_mem;

	ret = io_allocate_scq_urings(ctx, p);
	if (ret)
		goto err;

	ret = io_sq_offload_create(ctx, p);
	if (ret)
		goto err;

	if (!(p->flags & IORING_SETUP_R_DISABLED))
		io_sq_offload_start(ctx);

	memset(&p->sq_off, 0, sizeof(p->sq_off));
	p->sq_off.head = offsetof(struct io_rings, sq.head);
	p->sq_off.tail = offsetof(struct io_rings, sq.tail);
	p->sq_off.ring_mask = offsetof(struct io_rings, sq_ring_mask);
	p->sq_off.ring_entries = offsetof(struct io_rings, sq_ring_entries);
	p->sq_off.flags = offsetof(struct io_rings, sq_flags);
	p->sq_off.dropped = offsetof(struct io_rings, sq_dropped);
	p->sq_off.array = (char *)ctx->sq_array - (char *)ctx->rings;

	memset(&p->cq_off, 0, sizeof(p->cq_off));
	p->cq_off.head = offsetof(struct io_rings, cq.head);
	p->cq_off.tail = offsetof(struct io_rings, cq.tail);
	p->cq_off.ring_mask = offsetof(struct io_rings, cq_ring_mask);
	p->cq_off.ring_entries = offsetof(struct io_rings, cq_ring_entries);
	p->cq_off.overflow = offsetof(struct io_rings, cq_overflow);
	p->cq_off.cqes = offsetof(struct io_rings, cqes);
	p->cq_off.flags = offsetof(struct io_rings, cq_flags);

	p->features = IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP |
			IORING_FEAT_SUBMIT_STABLE | IORING_FEAT_RW_CUR_POS |
			IORING_FEAT_CUR_PERSONALITY | IORING_FEAT_FAST_POLL |
			IORING_FEAT_POLL_32BITS;

	if (copy_to_user(params, p, sizeof(*p))) {
		ret = -EFAULT;
		goto err;
	}

	file = io_uring_get_file(ctx);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto err;
	}

	/*
	 * Install ring fd as the very last thing, so we don't risk someone
	 * having closed it before we finish setup
	 */
	ret = io_uring_install_fd(ctx, file);
	if (ret < 0) {
		/* fput will clean it up */
		fput(file);
		return ret;
	}

	trace_io_uring_create(ret, ctx, p->sq_entries, p->cq_entries, p->flags);
	return ret;
err:
	io_ring_ctx_wait_and_kill(ctx);
	return ret;
}

/*
 * Sets up an aio uring context, and returns the fd. Applications asks for a
 * ring size, we return the actual sq/cq ring sizes (among other things) in the
 * params structure passed in.
 */
static long io_uring_setup(u32 entries, struct io_uring_params __user *params)
{
	struct io_uring_params p;
	int i;

	if (copy_from_user(&p, params, sizeof(p)))
		return -EFAULT;
	for (i = 0; i < ARRAY_SIZE(p.resv); i++) {
		if (p.resv[i])
			return -EINVAL;
	}

	if (p.flags & ~(IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL |
			IORING_SETUP_SQ_AFF | IORING_SETUP_CQSIZE |
			IORING_SETUP_CLAMP | IORING_SETUP_ATTACH_WQ |
			IORING_SETUP_R_DISABLED))
		return -EINVAL;

	return  io_uring_create(entries, &p, params);
}

SYSCALL_DEFINE2(io_uring_setup, u32, entries,
		struct io_uring_params __user *, params)
{
	return io_uring_setup(entries, params);
}

static int io_probe(struct io_ring_ctx *ctx, void __user *arg, unsigned nr_args)
{
	struct io_uring_probe *p;
	size_t size;
	int i, ret;

	size = struct_size(p, ops, nr_args);
	if (size == SIZE_MAX)
		return -EOVERFLOW;
	p = kzalloc(size, GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(p, arg, size))
		goto out;
	ret = -EINVAL;
	if (memchr_inv(p, 0, size))
		goto out;

	p->last_op = IORING_OP_LAST - 1;
	if (nr_args > IORING_OP_LAST)
		nr_args = IORING_OP_LAST;

	for (i = 0; i < nr_args; i++) {
		p->ops[i].op = i;
		if (!io_op_defs[i].not_supported)
			p->ops[i].flags = IO_URING_OP_SUPPORTED;
	}
	p->ops_len = i;

	ret = 0;
	if (copy_to_user(arg, p, size))
		ret = -EFAULT;
out:
	kfree(p);
	return ret;
}

static int io_register_personality(struct io_ring_ctx *ctx)
{
	struct io_identity *id;
	int ret;

	id = kmalloc(sizeof(*id), GFP_KERNEL);
	if (unlikely(!id))
		return -ENOMEM;

	io_init_identity(id);
	id->creds = get_current_cred();

	ret = idr_alloc_cyclic(&ctx->personality_idr, id, 1, USHRT_MAX, GFP_KERNEL);
	if (ret < 0) {
		put_cred(id->creds);
		kfree(id);
	}
	return ret;
}

static int io_unregister_personality(struct io_ring_ctx *ctx, unsigned id)
{
	struct io_identity *iod;

	iod = idr_remove(&ctx->personality_idr, id);
	if (iod) {
		put_cred(iod->creds);
		if (refcount_dec_and_test(&iod->count))
			kfree(iod);
		return 0;
	}

	return -EINVAL;
}

static int io_register_restrictions(struct io_ring_ctx *ctx, void __user *arg,
				    unsigned int nr_args)
{
	struct io_uring_restriction *res;
	size_t size;
	int i, ret;

	/* Restrictions allowed only if rings started disabled */
	if (!(ctx->flags & IORING_SETUP_R_DISABLED))
		return -EBADFD;

	/* We allow only a single restrictions registration */
	if (ctx->restrictions.registered)
		return -EBUSY;

	if (!arg || nr_args > IORING_MAX_RESTRICTIONS)
		return -EINVAL;

	size = array_size(nr_args, sizeof(*res));
	if (size == SIZE_MAX)
		return -EOVERFLOW;

	res = memdup_user(arg, size);
	if (IS_ERR(res))
		return PTR_ERR(res);

	ret = 0;

	for (i = 0; i < nr_args; i++) {
		switch (res[i].opcode) {
		case IORING_RESTRICTION_REGISTER_OP:
			if (res[i].register_op >= IORING_REGISTER_LAST) {
				ret = -EINVAL;
				goto out;
			}

			__set_bit(res[i].register_op,
				  ctx->restrictions.register_op);
			break;
		case IORING_RESTRICTION_SQE_OP:
			if (res[i].sqe_op >= IORING_OP_LAST) {
				ret = -EINVAL;
				goto out;
			}

			__set_bit(res[i].sqe_op, ctx->restrictions.sqe_op);
			break;
		case IORING_RESTRICTION_SQE_FLAGS_ALLOWED:
			ctx->restrictions.sqe_flags_allowed = res[i].sqe_flags;
			break;
		case IORING_RESTRICTION_SQE_FLAGS_REQUIRED:
			ctx->restrictions.sqe_flags_required = res[i].sqe_flags;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
	}

out:
	/* Reset all restrictions if an error happened */
	if (ret != 0)
		memset(&ctx->restrictions, 0, sizeof(ctx->restrictions));
	else
		ctx->restrictions.registered = true;

	kfree(res);
	return ret;
}

static int io_register_enable_rings(struct io_ring_ctx *ctx)
{
	if (!(ctx->flags & IORING_SETUP_R_DISABLED))
		return -EBADFD;

	if (ctx->restrictions.registered)
		ctx->restricted = 1;

	ctx->flags &= ~IORING_SETUP_R_DISABLED;

	io_sq_offload_start(ctx);

	return 0;
}

static bool io_register_op_must_quiesce(int op)
{
	switch (op) {
	case IORING_UNREGISTER_FILES:
	case IORING_REGISTER_FILES_UPDATE:
	case IORING_REGISTER_PROBE:
	case IORING_REGISTER_PERSONALITY:
	case IORING_UNREGISTER_PERSONALITY:
		return false;
	default:
		return true;
	}
}

static int __io_uring_register(struct io_ring_ctx *ctx, unsigned opcode,
			       void __user *arg, unsigned nr_args)
	__releases(ctx->uring_lock)
	__acquires(ctx->uring_lock)
{
	int ret;

	/*
	 * We're inside the ring mutex, if the ref is already dying, then
	 * someone else killed the ctx or is already going through
	 * io_uring_register().
	 */
	if (percpu_ref_is_dying(&ctx->refs))
		return -ENXIO;

	if (io_register_op_must_quiesce(opcode)) {
		percpu_ref_kill(&ctx->refs);

		/*
		 * Drop uring mutex before waiting for references to exit. If
		 * another thread is currently inside io_uring_enter() it might
		 * need to grab the uring_lock to make progress. If we hold it
		 * here across the drain wait, then we can deadlock. It's safe
		 * to drop the mutex here, since no new references will come in
		 * after we've killed the percpu ref.
		 */
		mutex_unlock(&ctx->uring_lock);
		do {
			ret = wait_for_completion_interruptible(&ctx->ref_comp);
			if (!ret)
				break;
			ret = io_run_task_work_sig();
			if (ret < 0)
				break;
		} while (1);

		mutex_lock(&ctx->uring_lock);

		if (ret) {
			percpu_ref_resurrect(&ctx->refs);
			goto out_quiesce;
		}
	}

	if (ctx->restricted) {
		if (opcode >= IORING_REGISTER_LAST) {
			ret = -EINVAL;
			goto out;
		}

		if (!test_bit(opcode, ctx->restrictions.register_op)) {
			ret = -EACCES;
			goto out;
		}
	}

	switch (opcode) {
	case IORING_REGISTER_BUFFERS:
		ret = io_sqe_buffer_register(ctx, arg, nr_args);
		break;
	case IORING_UNREGISTER_BUFFERS:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_sqe_buffer_unregister(ctx);
		break;
	case IORING_REGISTER_FILES:
		ret = io_sqe_files_register(ctx, arg, nr_args);
		break;
	case IORING_UNREGISTER_FILES:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_sqe_files_unregister(ctx);
		break;
	case IORING_REGISTER_FILES_UPDATE:
		ret = io_sqe_files_update(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_EVENTFD:
	case IORING_REGISTER_EVENTFD_ASYNC:
		ret = -EINVAL;
		if (nr_args != 1)
			break;
		ret = io_eventfd_register(ctx, arg);
		if (ret)
			break;
		if (opcode == IORING_REGISTER_EVENTFD_ASYNC)
			ctx->eventfd_async = 1;
		else
			ctx->eventfd_async = 0;
		break;
	case IORING_UNREGISTER_EVENTFD:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_eventfd_unregister(ctx);
		break;
	case IORING_REGISTER_PROBE:
		ret = -EINVAL;
		if (!arg || nr_args > 256)
			break;
		ret = io_probe(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_PERSONALITY:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_register_personality(ctx);
		break;
	case IORING_UNREGISTER_PERSONALITY:
		ret = -EINVAL;
		if (arg)
			break;
		ret = io_unregister_personality(ctx, nr_args);
		break;
	case IORING_REGISTER_ENABLE_RINGS:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_register_enable_rings(ctx);
		break;
	case IORING_REGISTER_RESTRICTIONS:
		ret = io_register_restrictions(ctx, arg, nr_args);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	if (io_register_op_must_quiesce(opcode)) {
		/* bring the ctx back to life */
		percpu_ref_reinit(&ctx->refs);
out_quiesce:
		reinit_completion(&ctx->ref_comp);
	}
	return ret;
}

SYSCALL_DEFINE4(io_uring_register, unsigned int, fd, unsigned int, opcode,
		void __user *, arg, unsigned int, nr_args)
{
	struct io_ring_ctx *ctx;
	long ret = -EBADF;
	struct fd f;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	ret = -EOPNOTSUPP;
	if (f.file->f_op != &io_uring_fops)
		goto out_fput;

	ctx = f.file->private_data;

	mutex_lock(&ctx->uring_lock);
	ret = __io_uring_register(ctx, opcode, arg, nr_args);
	mutex_unlock(&ctx->uring_lock);
	trace_io_uring_register(ctx, opcode, ctx->nr_user_files, ctx->nr_user_bufs,
							ctx->cq_ev_fd != NULL, ret);
out_fput:
	fdput(f);
	return ret;
}

static int __init io_uring_init(void)
{
#define __BUILD_BUG_VERIFY_ELEMENT(stype, eoffset, etype, ename) do { \
	BUILD_BUG_ON(offsetof(stype, ename) != eoffset); \
	BUILD_BUG_ON(sizeof(etype) != sizeof_field(stype, ename)); \
} while (0)

#define BUILD_BUG_SQE_ELEM(eoffset, etype, ename) \
	__BUILD_BUG_VERIFY_ELEMENT(struct io_uring_sqe, eoffset, etype, ename)
	BUILD_BUG_ON(sizeof(struct io_uring_sqe) != 64);
	BUILD_BUG_SQE_ELEM(0,  __u8,   opcode);
	BUILD_BUG_SQE_ELEM(1,  __u8,   flags);
	BUILD_BUG_SQE_ELEM(2,  __u16,  ioprio);
	BUILD_BUG_SQE_ELEM(4,  __s32,  fd);
	BUILD_BUG_SQE_ELEM(8,  __u64,  off);
	BUILD_BUG_SQE_ELEM(8,  __u64,  addr2);
	BUILD_BUG_SQE_ELEM(16, __u64,  addr);
	BUILD_BUG_SQE_ELEM(16, __u64,  splice_off_in);
	BUILD_BUG_SQE_ELEM(24, __u32,  len);
	BUILD_BUG_SQE_ELEM(28,     __kernel_rwf_t, rw_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */   int, rw_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */ __u32, rw_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  fsync_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */ __u16,  poll_events);
	BUILD_BUG_SQE_ELEM(28, __u32,  poll32_events);
	BUILD_BUG_SQE_ELEM(28, __u32,  sync_range_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  msg_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  timeout_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  accept_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  cancel_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  open_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  statx_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  fadvise_advice);
	BUILD_BUG_SQE_ELEM(28, __u32,  splice_flags);
	BUILD_BUG_SQE_ELEM(32, __u64,  user_data);
	BUILD_BUG_SQE_ELEM(40, __u16,  buf_index);
	BUILD_BUG_SQE_ELEM(42, __u16,  personality);
	BUILD_BUG_SQE_ELEM(44, __s32,  splice_fd_in);

	BUILD_BUG_ON(ARRAY_SIZE(io_op_defs) != IORING_OP_LAST);
	BUILD_BUG_ON(__REQ_F_LAST_BIT >= 8 * sizeof(int));
	req_cachep = KMEM_CACHE(io_kiocb, SLAB_HWCACHE_ALIGN | SLAB_PANIC);
	return 0;
};
__initcall(io_uring_init);
