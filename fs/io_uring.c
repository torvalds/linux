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
#include <linux/refcount.h>
#include <linux/uio.h>
#include <linux/bits.h>

#include <linux/sched/signal.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmu_context.h>
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
	 * Runtime flags
	 *
	 * Written by the kernel, shouldn't be modified by the
	 * application.
	 *
	 * The application needs a full memory barrier before checking
	 * for IORING_SQ_NEED_WAKEUP after updating the sq tail.
	 */
	u32			sq_flags;
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
};

struct fixed_file_table {
	struct file		**files;
};

enum {
	FFD_F_ATOMIC,
};

struct fixed_file_data {
	struct fixed_file_table		*table;
	struct io_ring_ctx		*ctx;

	struct percpu_ref		refs;
	struct llist_head		put_llist;
	unsigned long			state;
	struct work_struct		ref_work;
	struct completion		done;
};

struct io_ring_ctx {
	struct {
		struct percpu_ref	refs;
	} ____cacheline_aligned_in_smp;

	struct {
		unsigned int		flags;
		unsigned int		compat: 1;
		unsigned int		account_mem: 1;
		unsigned int		cq_overflow_flushed: 1;
		unsigned int		drain_next: 1;
		unsigned int		eventfd_async: 1;

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
		atomic_t		cached_cq_overflow;
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
	struct task_struct	*sqo_thread;	/* if using sq thread polling */
	struct mm_struct	*sqo_mm;
	wait_queue_head_t	sqo_wait;

	/*
	 * If used, fixed file set. Writers must ensure that ->refs is dead,
	 * readers must ensure that ->refs is alive as long as the file* is
	 * used. Only updated through io_uring_register(2).
	 */
	struct fixed_file_data	*file_data;
	unsigned		nr_user_files;
	int 			ring_fd;
	struct file 		*ring_file;

	/* if used, fixed mapped user buffers */
	unsigned		nr_user_bufs;
	struct io_mapped_ubuf	*user_bufs;

	struct user_struct	*user;

	const struct cred	*creds;

	/* 0 is for ctx quiesce/reinit/free, 1 is for sqo_thread started */
	struct completion	*completions;

	/* if all else fails... */
	struct io_kiocb		*fallback_req;

#if defined(CONFIG_UNIX)
	struct socket		*ring_sock;
#endif

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
		struct llist_head	poll_llist;

		/*
		 * ->poll_list is protected by the ctx->uring_lock for
		 * io_uring instances that don't use IORING_SETUP_SQPOLL.
		 * For SQPOLL, only the single threaded io_sq_thread() will
		 * manipulate the list, hence no extra locking is needed there.
		 */
		struct list_head	poll_list;
		struct hlist_head	*cancel_hash;
		unsigned		cancel_hash_bits;
		bool			poll_multi_file;

		spinlock_t		inflight_lock;
		struct list_head	inflight_list;
	} ____cacheline_aligned_in_smp;
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
	u32				seq_offset;
};

struct io_accept {
	struct file			*file;
	struct sockaddr __user		*addr;
	int __user			*addr_len;
	int				flags;
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
	u64				addr;
	int				flags;
	unsigned			count;
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
		struct user_msghdr __user *msg;
		void __user		*buf;
	};
	int				msg_flags;
	size_t				len;
};

struct io_open {
	struct file			*file;
	int				dfd;
	union {
		unsigned		mask;
	};
	struct filename			*filename;
	struct statx __user		*buffer;
	struct open_how			how;
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
	struct iovec			*iov;
	ssize_t				nr_segs;
	ssize_t				size;
};

struct io_async_ctx {
	union {
		struct io_async_rw	rw;
		struct io_async_msghdr	msg;
		struct io_async_connect	connect;
		struct io_timeout_data	timeout;
	};
};

enum {
	REQ_F_FIXED_FILE_BIT	= IOSQE_FIXED_FILE_BIT,
	REQ_F_IO_DRAIN_BIT	= IOSQE_IO_DRAIN_BIT,
	REQ_F_LINK_BIT		= IOSQE_IO_LINK_BIT,
	REQ_F_HARDLINK_BIT	= IOSQE_IO_HARDLINK_BIT,
	REQ_F_FORCE_ASYNC_BIT	= IOSQE_ASYNC_BIT,

	REQ_F_LINK_NEXT_BIT,
	REQ_F_FAIL_LINK_BIT,
	REQ_F_INFLIGHT_BIT,
	REQ_F_CUR_POS_BIT,
	REQ_F_NOWAIT_BIT,
	REQ_F_IOPOLL_COMPLETED_BIT,
	REQ_F_LINK_TIMEOUT_BIT,
	REQ_F_TIMEOUT_BIT,
	REQ_F_ISREG_BIT,
	REQ_F_MUST_PUNT_BIT,
	REQ_F_TIMEOUT_NOSEQ_BIT,
	REQ_F_COMP_LOCKED_BIT,
	REQ_F_NEED_CLEANUP_BIT,
	REQ_F_OVERFLOW_BIT,
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

	/* already grabbed next link */
	REQ_F_LINK_NEXT		= BIT(REQ_F_LINK_NEXT_BIT),
	/* fail rest of links */
	REQ_F_FAIL_LINK		= BIT(REQ_F_FAIL_LINK_BIT),
	/* on inflight list */
	REQ_F_INFLIGHT		= BIT(REQ_F_INFLIGHT_BIT),
	/* read/write uses file position */
	REQ_F_CUR_POS		= BIT(REQ_F_CUR_POS_BIT),
	/* must not punt to workers */
	REQ_F_NOWAIT		= BIT(REQ_F_NOWAIT_BIT),
	/* polled IO has completed */
	REQ_F_IOPOLL_COMPLETED	= BIT(REQ_F_IOPOLL_COMPLETED_BIT),
	/* has linked timeout */
	REQ_F_LINK_TIMEOUT	= BIT(REQ_F_LINK_TIMEOUT_BIT),
	/* timeout request */
	REQ_F_TIMEOUT		= BIT(REQ_F_TIMEOUT_BIT),
	/* regular file */
	REQ_F_ISREG		= BIT(REQ_F_ISREG_BIT),
	/* must be punted even for NONBLOCK */
	REQ_F_MUST_PUNT		= BIT(REQ_F_MUST_PUNT_BIT),
	/* no timeout sequence */
	REQ_F_TIMEOUT_NOSEQ	= BIT(REQ_F_TIMEOUT_NOSEQ_BIT),
	/* completion under lock */
	REQ_F_COMP_LOCKED	= BIT(REQ_F_COMP_LOCKED_BIT),
	/* needs cleanup */
	REQ_F_NEED_CLEANUP	= BIT(REQ_F_NEED_CLEANUP_BIT),
	/* in overflow list */
	REQ_F_OVERFLOW		= BIT(REQ_F_OVERFLOW_BIT),
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
		struct io_connect	connect;
		struct io_sr_msg	sr_msg;
		struct io_open		open;
		struct io_close		close;
		struct io_files_update	files_update;
		struct io_fadvise	fadvise;
		struct io_madvise	madvise;
		struct io_epoll		epoll;
	};

	struct io_async_ctx		*io;
	/*
	 * llist_node is only used for poll deferred completions
	 */
	struct llist_node		llist_node;
	bool				in_async;
	bool				needs_fixed_file;
	u8				opcode;

	struct io_ring_ctx	*ctx;
	union {
		struct list_head	list;
		struct hlist_node	hash_node;
	};
	struct list_head	link_list;
	unsigned int		flags;
	refcount_t		refs;
	u64			user_data;
	u32			result;
	u32			sequence;

	struct list_head	inflight_entry;

	struct io_wq_work	work;
};

#define IO_PLUG_THRESHOLD		2
#define IO_IOPOLL_BATCH			8

struct io_submit_state {
	struct blk_plug		plug;

	/*
	 * io_kiocb alloc cache
	 */
	void			*reqs[IO_IOPOLL_BATCH];
	unsigned int		free_reqs;

	/*
	 * File reference cache
	 */
	struct file		*file;
	unsigned int		fd;
	unsigned int		has_refs;
	unsigned int		used_refs;
	unsigned int		ios_left;
};

struct io_op_def {
	/* needs req->io allocated for deferral/async */
	unsigned		async_ctx : 1;
	/* needs current->mm setup, does mm access */
	unsigned		needs_mm : 1;
	/* needs req->file assigned */
	unsigned		needs_file : 1;
	/* needs req->file assigned IFF fd is >= 0 */
	unsigned		fd_non_neg : 1;
	/* hash wq insertion if file is a regular file */
	unsigned		hash_reg_file : 1;
	/* unbound wq insertion if file is a non-regular file */
	unsigned		unbound_nonreg_file : 1;
	/* opcode is not supported by this kernel */
	unsigned		not_supported : 1;
	/* needs file table */
	unsigned		file_table : 1;
	/* needs ->fs */
	unsigned		needs_fs : 1;
};

static const struct io_op_def io_op_defs[] = {
	[IORING_OP_NOP] = {},
	[IORING_OP_READV] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_WRITEV] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_FSYNC] = {
		.needs_file		= 1,
	},
	[IORING_OP_READ_FIXED] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_WRITE_FIXED] = {
		.needs_file		= 1,
		.hash_reg_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_POLL_ADD] = {
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_POLL_REMOVE] = {},
	[IORING_OP_SYNC_FILE_RANGE] = {
		.needs_file		= 1,
	},
	[IORING_OP_SENDMSG] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.needs_fs		= 1,
	},
	[IORING_OP_RECVMSG] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.needs_fs		= 1,
	},
	[IORING_OP_TIMEOUT] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
	},
	[IORING_OP_TIMEOUT_REMOVE] = {},
	[IORING_OP_ACCEPT] = {
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
		.file_table		= 1,
	},
	[IORING_OP_ASYNC_CANCEL] = {},
	[IORING_OP_LINK_TIMEOUT] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
	},
	[IORING_OP_CONNECT] = {
		.async_ctx		= 1,
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_FALLOCATE] = {
		.needs_file		= 1,
	},
	[IORING_OP_OPENAT] = {
		.needs_file		= 1,
		.fd_non_neg		= 1,
		.file_table		= 1,
		.needs_fs		= 1,
	},
	[IORING_OP_CLOSE] = {
		.needs_file		= 1,
		.file_table		= 1,
	},
	[IORING_OP_FILES_UPDATE] = {
		.needs_mm		= 1,
		.file_table		= 1,
	},
	[IORING_OP_STATX] = {
		.needs_mm		= 1,
		.needs_file		= 1,
		.fd_non_neg		= 1,
		.needs_fs		= 1,
	},
	[IORING_OP_READ] = {
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_WRITE] = {
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_FADVISE] = {
		.needs_file		= 1,
	},
	[IORING_OP_MADVISE] = {
		.needs_mm		= 1,
	},
	[IORING_OP_SEND] = {
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_RECV] = {
		.needs_mm		= 1,
		.needs_file		= 1,
		.unbound_nonreg_file	= 1,
	},
	[IORING_OP_OPENAT2] = {
		.needs_file		= 1,
		.fd_non_neg		= 1,
		.file_table		= 1,
		.needs_fs		= 1,
	},
	[IORING_OP_EPOLL_CTL] = {
		.unbound_nonreg_file	= 1,
		.file_table		= 1,
	},
};

static void io_wq_submit_work(struct io_wq_work **workptr);
static void io_cqring_fill_event(struct io_kiocb *req, long res);
static void io_put_req(struct io_kiocb *req);
static void __io_double_put_req(struct io_kiocb *req);
static struct io_kiocb *io_prep_linked_timeout(struct io_kiocb *req);
static void io_queue_linked_timeout(struct io_kiocb *req);
static int __io_sqe_files_update(struct io_ring_ctx *ctx,
				 struct io_uring_files_update *ip,
				 unsigned nr_args);
static int io_grab_files(struct io_kiocb *req);
static void io_ring_file_ref_flush(struct fixed_file_data *data);
static void io_cleanup_req(struct io_kiocb *req);

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

static void io_ring_ctx_ref_free(struct percpu_ref *ref)
{
	struct io_ring_ctx *ctx = container_of(ref, struct io_ring_ctx, refs);

	complete(&ctx->completions[0]);
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

	ctx->completions = kmalloc(2 * sizeof(struct completion), GFP_KERNEL);
	if (!ctx->completions)
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
	init_waitqueue_head(&ctx->cq_wait);
	INIT_LIST_HEAD(&ctx->cq_overflow_list);
	init_completion(&ctx->completions[0]);
	init_completion(&ctx->completions[1]);
	idr_init(&ctx->personality_idr);
	mutex_init(&ctx->uring_lock);
	init_waitqueue_head(&ctx->wait);
	spin_lock_init(&ctx->completion_lock);
	init_llist_head(&ctx->poll_llist);
	INIT_LIST_HEAD(&ctx->poll_list);
	INIT_LIST_HEAD(&ctx->defer_list);
	INIT_LIST_HEAD(&ctx->timeout_list);
	init_waitqueue_head(&ctx->inflight_wait);
	spin_lock_init(&ctx->inflight_lock);
	INIT_LIST_HEAD(&ctx->inflight_list);
	return ctx;
err:
	if (ctx->fallback_req)
		kmem_cache_free(req_cachep, ctx->fallback_req);
	kfree(ctx->completions);
	kfree(ctx->cancel_hash);
	kfree(ctx);
	return NULL;
}

static inline bool __req_need_defer(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	return req->sequence != ctx->cached_cq_tail + ctx->cached_sq_dropped
					+ atomic_read(&ctx->cached_cq_overflow);
}

static inline bool req_need_defer(struct io_kiocb *req)
{
	if (unlikely(req->flags & REQ_F_IO_DRAIN))
		return __req_need_defer(req);

	return false;
}

static struct io_kiocb *io_get_deferred_req(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req;

	req = list_first_entry_or_null(&ctx->defer_list, struct io_kiocb, list);
	if (req && !req_need_defer(req)) {
		list_del_init(&req->list);
		return req;
	}

	return NULL;
}

static struct io_kiocb *io_get_timeout_req(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req;

	req = list_first_entry_or_null(&ctx->timeout_list, struct io_kiocb, list);
	if (req) {
		if (req->flags & REQ_F_TIMEOUT_NOSEQ)
			return NULL;
		if (!__req_need_defer(req)) {
			list_del_init(&req->list);
			return req;
		}
	}

	return NULL;
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

static inline void io_req_work_grab_env(struct io_kiocb *req,
					const struct io_op_def *def)
{
	if (!req->work.mm && def->needs_mm) {
		mmgrab(current->mm);
		req->work.mm = current->mm;
	}
	if (!req->work.creds)
		req->work.creds = get_current_cred();
	if (!req->work.fs && def->needs_fs) {
		spin_lock(&current->fs->lock);
		if (!current->fs->in_exec) {
			req->work.fs = current->fs;
			req->work.fs->users++;
		} else {
			req->work.flags |= IO_WQ_WORK_CANCEL;
		}
		spin_unlock(&current->fs->lock);
	}
	if (!req->work.task_pid)
		req->work.task_pid = task_pid_vnr(current);
}

static inline void io_req_work_drop_env(struct io_kiocb *req)
{
	if (req->work.mm) {
		mmdrop(req->work.mm);
		req->work.mm = NULL;
	}
	if (req->work.creds) {
		put_cred(req->work.creds);
		req->work.creds = NULL;
	}
	if (req->work.fs) {
		struct fs_struct *fs = req->work.fs;

		spin_lock(&req->work.fs->lock);
		if (--fs->users)
			fs = NULL;
		spin_unlock(&req->work.fs->lock);
		if (fs)
			free_fs_struct(fs);
	}
}

static inline bool io_prep_async_work(struct io_kiocb *req,
				      struct io_kiocb **link)
{
	const struct io_op_def *def = &io_op_defs[req->opcode];
	bool do_hashed = false;

	if (req->flags & REQ_F_ISREG) {
		if (def->hash_reg_file)
			do_hashed = true;
	} else {
		if (def->unbound_nonreg_file)
			req->work.flags |= IO_WQ_WORK_UNBOUND;
	}

	io_req_work_grab_env(req, def);

	*link = io_prep_linked_timeout(req);
	return do_hashed;
}

static inline void io_queue_async_work(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *link;
	bool do_hashed;

	do_hashed = io_prep_async_work(req, &link);

	trace_io_uring_queue_async_work(ctx, do_hashed, req, &req->work,
					req->flags);
	if (!do_hashed) {
		io_wq_enqueue(ctx->io_wq, &req->work);
	} else {
		io_wq_enqueue_hashed(ctx->io_wq, &req->work,
					file_inode(req->file));
	}

	if (link)
		io_queue_linked_timeout(link);
}

static void io_kill_timeout(struct io_kiocb *req)
{
	int ret;

	ret = hrtimer_try_to_cancel(&req->io->timeout.timer);
	if (ret != -1) {
		atomic_inc(&req->ctx->cq_timeouts);
		list_del_init(&req->list);
		io_cqring_fill_event(req, 0);
		io_put_req(req);
	}
}

static void io_kill_timeouts(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req, *tmp;

	spin_lock_irq(&ctx->completion_lock);
	list_for_each_entry_safe(req, tmp, &ctx->timeout_list, list)
		io_kill_timeout(req);
	spin_unlock_irq(&ctx->completion_lock);
}

static void io_commit_cqring(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req;

	while ((req = io_get_timeout_req(ctx)) != NULL)
		io_kill_timeout(req);

	__io_commit_cqring(ctx);

	while ((req = io_get_deferred_req(ctx)) != NULL)
		io_queue_async_work(req);
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
	if (!ctx->eventfd_async)
		return true;
	return io_wq_current_is_worker() || in_interrupt();
}

static void __io_cqring_ev_posted(struct io_ring_ctx *ctx, bool trigger_ev)
{
	if (waitqueue_active(&ctx->wait))
		wake_up(&ctx->wait);
	if (waitqueue_active(&ctx->sqo_wait))
		wake_up(&ctx->sqo_wait);
	if (trigger_ev)
		eventfd_signal(ctx->cq_ev_fd, 1);
}

static void io_cqring_ev_posted(struct io_ring_ctx *ctx)
{
	__io_cqring_ev_posted(ctx, io_should_trigger_evfd(ctx));
}

/* Returns true if there are no backlogged entries after the flush */
static bool io_cqring_overflow_flush(struct io_ring_ctx *ctx, bool force)
{
	struct io_rings *rings = ctx->rings;
	struct io_uring_cqe *cqe;
	struct io_kiocb *req;
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

	/* if force is set, the ring is going away. always drop after that */
	if (force)
		ctx->cq_overflow_flushed = 1;

	cqe = NULL;
	while (!list_empty(&ctx->cq_overflow_list)) {
		cqe = io_get_cqring(ctx);
		if (!cqe && !force)
			break;

		req = list_first_entry(&ctx->cq_overflow_list, struct io_kiocb,
						list);
		list_move(&req->list, &list);
		req->flags &= ~REQ_F_OVERFLOW;
		if (cqe) {
			WRITE_ONCE(cqe->user_data, req->user_data);
			WRITE_ONCE(cqe->res, req->result);
			WRITE_ONCE(cqe->flags, 0);
		} else {
			WRITE_ONCE(ctx->rings->cq_overflow,
				atomic_inc_return(&ctx->cached_cq_overflow));
		}
	}

	io_commit_cqring(ctx);
	if (cqe) {
		clear_bit(0, &ctx->sq_check_overflow);
		clear_bit(0, &ctx->cq_check_overflow);
	}
	spin_unlock_irqrestore(&ctx->completion_lock, flags);
	io_cqring_ev_posted(ctx);

	while (!list_empty(&list)) {
		req = list_first_entry(&list, struct io_kiocb, list);
		list_del(&req->list);
		io_put_req(req);
	}

	return cqe != NULL;
}

static void io_cqring_fill_event(struct io_kiocb *req, long res)
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
		WRITE_ONCE(cqe->flags, 0);
	} else if (ctx->cq_overflow_flushed) {
		WRITE_ONCE(ctx->rings->cq_overflow,
				atomic_inc_return(&ctx->cached_cq_overflow));
	} else {
		if (list_empty(&ctx->cq_overflow_list)) {
			set_bit(0, &ctx->sq_check_overflow);
			set_bit(0, &ctx->cq_check_overflow);
		}
		req->flags |= REQ_F_OVERFLOW;
		refcount_inc(&req->refs);
		req->result = res;
		list_add_tail(&req->list, &ctx->cq_overflow_list);
	}
}

static void io_cqring_add_event(struct io_kiocb *req, long res)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned long flags;

	spin_lock_irqsave(&ctx->completion_lock, flags);
	io_cqring_fill_event(req, res);
	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	io_cqring_ev_posted(ctx);
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
	if (!test_and_set_bit_lock(0, (unsigned long *) ctx->fallback_req))
		return req;

	return NULL;
}

static struct io_kiocb *io_get_req(struct io_ring_ctx *ctx,
				   struct io_submit_state *state)
{
	gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;
	struct io_kiocb *req;

	if (!state) {
		req = kmem_cache_alloc(req_cachep, gfp);
		if (unlikely(!req))
			goto fallback;
	} else if (!state->free_reqs) {
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
		state->free_reqs = ret - 1;
		req = state->reqs[ret - 1];
	} else {
		state->free_reqs--;
		req = state->reqs[state->free_reqs];
	}

got_it:
	req->io = NULL;
	req->file = NULL;
	req->ctx = ctx;
	req->flags = 0;
	/* one is dropped after submission, the other at completion */
	refcount_set(&req->refs, 2);
	req->result = 0;
	INIT_IO_WORK(&req->work, io_wq_submit_work);
	return req;
fallback:
	req = io_get_fallback_req(ctx);
	if (req)
		goto got_it;
	percpu_ref_put(&ctx->refs);
	return NULL;
}

static void __io_req_do_free(struct io_kiocb *req)
{
	if (likely(!io_is_fallback_req(req)))
		kmem_cache_free(req_cachep, req);
	else
		clear_bit_unlock(0, (unsigned long *) req->ctx->fallback_req);
}

static void __io_req_aux_free(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	kfree(req->io);
	if (req->file) {
		if (req->flags & REQ_F_FIXED_FILE)
			percpu_ref_put(&ctx->file_data->refs);
		else
			fput(req->file);
	}

	io_req_work_drop_env(req);
}

static void __io_free_req(struct io_kiocb *req)
{
	__io_req_aux_free(req);

	if (req->flags & REQ_F_NEED_CLEANUP)
		io_cleanup_req(req);

	if (req->flags & REQ_F_INFLIGHT) {
		struct io_ring_ctx *ctx = req->ctx;
		unsigned long flags;

		spin_lock_irqsave(&ctx->inflight_lock, flags);
		list_del(&req->inflight_entry);
		if (waitqueue_active(&ctx->inflight_wait))
			wake_up(&ctx->inflight_wait);
		spin_unlock_irqrestore(&ctx->inflight_lock, flags);
	}

	percpu_ref_put(&req->ctx->refs);
	__io_req_do_free(req);
}

struct req_batch {
	void *reqs[IO_IOPOLL_BATCH];
	int to_free;
	int need_iter;
};

static void io_free_req_many(struct io_ring_ctx *ctx, struct req_batch *rb)
{
	int fixed_refs = rb->to_free;

	if (!rb->to_free)
		return;
	if (rb->need_iter) {
		int i, inflight = 0;
		unsigned long flags;

		fixed_refs = 0;
		for (i = 0; i < rb->to_free; i++) {
			struct io_kiocb *req = rb->reqs[i];

			if (req->flags & REQ_F_FIXED_FILE) {
				req->file = NULL;
				fixed_refs++;
			}
			if (req->flags & REQ_F_INFLIGHT)
				inflight++;
			__io_req_aux_free(req);
		}
		if (!inflight)
			goto do_free;

		spin_lock_irqsave(&ctx->inflight_lock, flags);
		for (i = 0; i < rb->to_free; i++) {
			struct io_kiocb *req = rb->reqs[i];

			if (req->flags & REQ_F_INFLIGHT) {
				list_del(&req->inflight_entry);
				if (!--inflight)
					break;
			}
		}
		spin_unlock_irqrestore(&ctx->inflight_lock, flags);

		if (waitqueue_active(&ctx->inflight_wait))
			wake_up(&ctx->inflight_wait);
	}
do_free:
	kmem_cache_free_bulk(req_cachep, rb->to_free, rb->reqs);
	if (fixed_refs)
		percpu_ref_put_many(&ctx->file_data->refs, fixed_refs);
	percpu_ref_put_many(&ctx->refs, rb->to_free);
	rb->to_free = rb->need_iter = 0;
}

static bool io_link_cancel_timeout(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	ret = hrtimer_try_to_cancel(&req->io->timeout.timer);
	if (ret != -1) {
		io_cqring_fill_event(req, -ECANCELED);
		io_commit_cqring(ctx);
		req->flags &= ~REQ_F_LINK;
		io_put_req(req);
		return true;
	}

	return false;
}

static void io_req_link_next(struct io_kiocb *req, struct io_kiocb **nxtptr)
{
	struct io_ring_ctx *ctx = req->ctx;
	bool wake_ev = false;

	/* Already got next link */
	if (req->flags & REQ_F_LINK_NEXT)
		return;

	/*
	 * The list should never be empty when we are called here. But could
	 * potentially happen if the chain is messed up, check to be on the
	 * safe side.
	 */
	while (!list_empty(&req->link_list)) {
		struct io_kiocb *nxt = list_first_entry(&req->link_list,
						struct io_kiocb, link_list);

		if (unlikely((req->flags & REQ_F_LINK_TIMEOUT) &&
			     (nxt->flags & REQ_F_TIMEOUT))) {
			list_del_init(&nxt->link_list);
			wake_ev |= io_link_cancel_timeout(nxt);
			req->flags &= ~REQ_F_LINK_TIMEOUT;
			continue;
		}

		list_del_init(&req->link_list);
		if (!list_empty(&nxt->link_list))
			nxt->flags |= REQ_F_LINK;
		*nxtptr = nxt;
		break;
	}

	req->flags |= REQ_F_LINK_NEXT;
	if (wake_ev)
		io_cqring_ev_posted(ctx);
}

/*
 * Called if REQ_F_LINK is set, and we fail the head request
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

		if ((req->flags & REQ_F_LINK_TIMEOUT) &&
		    link->opcode == IORING_OP_LINK_TIMEOUT) {
			io_link_cancel_timeout(link);
		} else {
			io_cqring_fill_event(link, -ECANCELED);
			__io_double_put_req(link);
		}
		req->flags &= ~REQ_F_LINK_TIMEOUT;
	}

	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);
	io_cqring_ev_posted(ctx);
}

static void io_req_find_next(struct io_kiocb *req, struct io_kiocb **nxt)
{
	if (likely(!(req->flags & REQ_F_LINK)))
		return;

	/*
	 * If LINK is set, we have dependent requests in this chain. If we
	 * didn't fail this request, queue the first one up, moving any other
	 * dependencies to the next request. In case of failure, fail the rest
	 * of the chain.
	 */
	if (req->flags & REQ_F_FAIL_LINK) {
		io_fail_links(req);
	} else if ((req->flags & (REQ_F_LINK_TIMEOUT | REQ_F_COMP_LOCKED)) ==
			REQ_F_LINK_TIMEOUT) {
		struct io_ring_ctx *ctx = req->ctx;
		unsigned long flags;

		/*
		 * If this is a timeout link, we could be racing with the
		 * timeout timer. Grab the completion lock for this case to
		 * protect against that.
		 */
		spin_lock_irqsave(&ctx->completion_lock, flags);
		io_req_link_next(req, nxt);
		spin_unlock_irqrestore(&ctx->completion_lock, flags);
	} else {
		io_req_link_next(req, nxt);
	}
}

static void io_free_req(struct io_kiocb *req)
{
	struct io_kiocb *nxt = NULL;

	io_req_find_next(req, &nxt);
	__io_free_req(req);

	if (nxt)
		io_queue_async_work(nxt);
}

/*
 * Drop reference to request, return next in chain (if there is one) if this
 * was the last reference to this request.
 */
__attribute__((nonnull))
static void io_put_req_find_next(struct io_kiocb *req, struct io_kiocb **nxtptr)
{
	io_req_find_next(req, nxtptr);

	if (refcount_dec_and_test(&req->refs))
		__io_free_req(req);
}

static void io_put_req(struct io_kiocb *req)
{
	if (refcount_dec_and_test(&req->refs))
		io_free_req(req);
}

/*
 * Must only be used if we don't need to care about links, usually from
 * within the completion handling itself.
 */
static void __io_double_put_req(struct io_kiocb *req)
{
	/* drop both submit and complete references */
	if (refcount_sub_and_test(2, &req->refs))
		__io_free_req(req);
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
		if (noflush && !list_empty(&ctx->cq_overflow_list))
			return -1U;

		io_cqring_overflow_flush(ctx, false);
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

static inline bool io_req_multi_free(struct req_batch *rb, struct io_kiocb *req)
{
	if ((req->flags & REQ_F_LINK) || io_is_fallback_req(req))
		return false;

	if (!(req->flags & REQ_F_FIXED_FILE) || req->io)
		rb->need_iter++;

	rb->reqs[rb->to_free++] = req;
	if (unlikely(rb->to_free == ARRAY_SIZE(rb->reqs)))
		io_free_req_many(req->ctx, rb);
	return true;
}

/*
 * Find and free completed poll iocbs
 */
static void io_iopoll_complete(struct io_ring_ctx *ctx, unsigned int *nr_events,
			       struct list_head *done)
{
	struct req_batch rb;
	struct io_kiocb *req;

	rb.to_free = rb.need_iter = 0;
	while (!list_empty(done)) {
		req = list_first_entry(done, struct io_kiocb, list);
		list_del(&req->list);

		io_cqring_fill_event(req, req->result);
		(*nr_events)++;

		if (refcount_dec_and_test(&req->refs) &&
		    !io_req_multi_free(&rb, req))
			io_free_req(req);
	}

	io_commit_cqring(ctx);
	io_free_req_many(ctx, &rb);
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
	list_for_each_entry_safe(req, tmp, &ctx->poll_list, list) {
		struct kiocb *kiocb = &req->rw.kiocb;

		/*
		 * Move completed entries to our local list. If we find a
		 * request that requires polling, break out and complete
		 * the done list first, if we have entries there.
		 */
		if (req->flags & REQ_F_IOPOLL_COMPLETED) {
			list_move_tail(&req->list, &done);
			continue;
		}
		if (!list_empty(&done))
			break;

		ret = kiocb->ki_filp->f_op->iopoll(kiocb, spin);
		if (ret < 0)
			break;

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
	while (!list_empty(&ctx->poll_list) && !need_resched()) {
		int ret;

		ret = io_do_iopoll(ctx, nr_events, min);
		if (ret < 0)
			return ret;
		if (!min || *nr_events >= min)
			return 0;
	}

	return 1;
}

/*
 * We can't just wait for polled events to come to us, we have to actively
 * find and complete them.
 */
static void io_iopoll_reap_events(struct io_ring_ctx *ctx)
{
	if (!(ctx->flags & IORING_SETUP_IOPOLL))
		return;

	mutex_lock(&ctx->uring_lock);
	while (!list_empty(&ctx->poll_list)) {
		unsigned int nr_events = 0;

		io_iopoll_getevents(ctx, &nr_events, 1);

		/*
		 * Ensure we allow local-to-the-cpu processing to take place,
		 * in this case we need to ensure that we reap all events.
		 */
		cond_resched();
	}
	mutex_unlock(&ctx->uring_lock);
}

static int __io_iopoll_check(struct io_ring_ctx *ctx, unsigned *nr_events,
			    long min)
{
	int iters = 0, ret = 0;

	do {
		int tmin = 0;

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
			mutex_lock(&ctx->uring_lock);
		}

		if (*nr_events < min)
			tmin = min - *nr_events;

		ret = io_iopoll_getevents(ctx, nr_events, tmin);
		if (ret <= 0)
			break;
		ret = 0;
	} while (min && !*nr_events && !need_resched());

	return ret;
}

static int io_iopoll_check(struct io_ring_ctx *ctx, unsigned *nr_events,
			   long min)
{
	int ret;

	/*
	 * We disallow the app entering submit/complete with polling, but we
	 * still need to lock the ring to prevent racing with polled issue
	 * that got punted to a workqueue.
	 */
	mutex_lock(&ctx->uring_lock);
	ret = __io_iopoll_check(ctx, nr_events, min);
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

static inline void req_set_fail_links(struct io_kiocb *req)
{
	if ((req->flags & (REQ_F_LINK | REQ_F_HARDLINK)) == REQ_F_LINK)
		req->flags |= REQ_F_FAIL_LINK;
}

static void io_complete_rw_common(struct kiocb *kiocb, long res)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);

	if (kiocb->ki_flags & IOCB_WRITE)
		kiocb_end_write(req);

	if (res != req->result)
		req_set_fail_links(req);
	io_cqring_add_event(req, res);
}

static void io_complete_rw(struct kiocb *kiocb, long res, long res2)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);

	io_complete_rw_common(kiocb, res);
	io_put_req(req);
}

static struct io_kiocb *__io_complete_rw(struct kiocb *kiocb, long res)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);
	struct io_kiocb *nxt = NULL;

	io_complete_rw_common(kiocb, res);
	io_put_req_find_next(req, &nxt);

	return nxt;
}

static void io_complete_rw_iopoll(struct kiocb *kiocb, long res, long res2)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);

	if (kiocb->ki_flags & IOCB_WRITE)
		kiocb_end_write(req);

	if (res != req->result)
		req_set_fail_links(req);
	req->result = res;
	if (res != -EAGAIN)
		req->flags |= REQ_F_IOPOLL_COMPLETED;
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
	if (list_empty(&ctx->poll_list)) {
		ctx->poll_multi_file = false;
	} else if (!ctx->poll_multi_file) {
		struct io_kiocb *list_req;

		list_req = list_first_entry(&ctx->poll_list, struct io_kiocb,
						list);
		if (list_req->file != req->file)
			ctx->poll_multi_file = true;
	}

	/*
	 * For fast devices, IO may have already completed. If it has, add
	 * it to the front so we find it first.
	 */
	if (req->flags & REQ_F_IOPOLL_COMPLETED)
		list_add(&req->list, &ctx->poll_list);
	else
		list_add_tail(&req->list, &ctx->poll_list);
}

static void io_file_put(struct io_submit_state *state)
{
	if (state->file) {
		int diff = state->has_refs - state->used_refs;

		if (diff)
			fput_many(state->file, diff);
		state->file = NULL;
	}
}

/*
 * Get as many references to a file as we have IOs left in this submission,
 * assuming most submissions are for one file, or at least that each file
 * has more than one submission.
 */
static struct file *io_file_get(struct io_submit_state *state, int fd)
{
	if (!state)
		return fget(fd);

	if (state->file) {
		if (state->fd == fd) {
			state->used_refs++;
			state->ios_left--;
			return state->file;
		}
		io_file_put(state);
	}
	state->file = fget_many(fd, state->ios_left);
	if (!state->file)
		return NULL;

	state->fd = fd;
	state->has_refs = state->ios_left;
	state->used_refs = 1;
	state->ios_left--;
	return state->file;
}

/*
 * If we tracked the file through the SCM inflight mechanism, we could support
 * any file. For now, just ensure that anything potentially problematic is done
 * inline.
 */
static bool io_file_supports_async(struct file *file)
{
	umode_t mode = file_inode(file)->i_mode;

	if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISSOCK(mode))
		return true;
	if (S_ISREG(mode) && file->f_op != &io_uring_fops)
		return true;

	return false;
}

static int io_prep_rw(struct io_kiocb *req, const struct io_uring_sqe *sqe,
		      bool force_nonblock)
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
	if ((kiocb->ki_flags & IOCB_NOWAIT) ||
	    (req->file->f_flags & O_NONBLOCK))
		req->flags |= REQ_F_NOWAIT;

	if (force_nonblock)
		kiocb->ki_flags |= IOCB_NOWAIT;

	if (ctx->flags & IORING_SETUP_IOPOLL) {
		if (!(kiocb->ki_flags & IOCB_DIRECT) ||
		    !kiocb->ki_filp->f_op->iopoll)
			return -EOPNOTSUPP;

		kiocb->ki_flags |= IOCB_HIPRI;
		kiocb->ki_complete = io_complete_rw_iopoll;
		req->result = 0;
	} else {
		if (kiocb->ki_flags & IOCB_HIPRI)
			return -EINVAL;
		kiocb->ki_complete = io_complete_rw;
	}

	req->rw.addr = READ_ONCE(sqe->addr);
	req->rw.len = READ_ONCE(sqe->len);
	/* we own ->private, reuse it for the buffer index */
	req->rw.kiocb.private = (void *) (unsigned long)
					READ_ONCE(sqe->buf_index);
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
		/* fall through */
	default:
		kiocb->ki_complete(kiocb, ret, 0);
	}
}

static void kiocb_done(struct kiocb *kiocb, ssize_t ret, struct io_kiocb **nxt,
		       bool in_async)
{
	struct io_kiocb *req = container_of(kiocb, struct io_kiocb, rw.kiocb);

	if (req->flags & REQ_F_CUR_POS)
		req->file->f_pos = kiocb->ki_pos;
	if (in_async && ret >= 0 && kiocb->ki_complete == io_complete_rw)
		*nxt = __io_complete_rw(kiocb, ret);
	else
		io_rw_done(kiocb, ret);
}

static ssize_t io_import_fixed(struct io_kiocb *req, int rw,
			       struct iov_iter *iter)
{
	struct io_ring_ctx *ctx = req->ctx;
	size_t len = req->rw.len;
	struct io_mapped_ubuf *imu;
	unsigned index, buf_index;
	size_t offset;
	u64 buf_addr;

	/* attempt to use fixed buffers without having provided iovecs */
	if (unlikely(!ctx->user_bufs))
		return -EFAULT;

	buf_index = (unsigned long) req->rw.kiocb.private;
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

static ssize_t io_import_iovec(int rw, struct io_kiocb *req,
			       struct iovec **iovec, struct iov_iter *iter)
{
	void __user *buf = u64_to_user_ptr(req->rw.addr);
	size_t sqe_len = req->rw.len;
	u8 opcode;

	opcode = req->opcode;
	if (opcode == IORING_OP_READ_FIXED || opcode == IORING_OP_WRITE_FIXED) {
		*iovec = NULL;
		return io_import_fixed(req, rw, iter);
	}

	/* buffer index only valid with fixed read/write */
	if (req->rw.kiocb.private)
		return -EINVAL;

	if (opcode == IORING_OP_READ || opcode == IORING_OP_WRITE) {
		ssize_t ret;
		ret = import_single_range(rw, buf, sqe_len, *iovec, iter);
		*iovec = NULL;
		return ret;
	}

	if (req->io) {
		struct io_async_rw *iorw = &req->io->rw;

		*iovec = iorw->iov;
		iov_iter_init(iter, rw, *iovec, iorw->nr_segs, iorw->size);
		if (iorw->iov == iorw->fast_iov)
			*iovec = NULL;
		return iorw->size;
	}

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		return compat_import_iovec(rw, buf, sqe_len, UIO_FASTIOV,
						iovec, iter);
#endif

	return import_iovec(rw, buf, sqe_len, UIO_FASTIOV, iovec, iter);
}

/*
 * For files that don't have ->read_iter() and ->write_iter(), handle them
 * by looping over ->read() or ->write() manually.
 */
static ssize_t loop_rw_iter(int rw, struct file *file, struct kiocb *kiocb,
			   struct iov_iter *iter)
{
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
			/* fixed buffers import bvec */
			iovec.iov_base = kmap(iter->bvec->bv_page)
						+ iter->iov_offset;
			iovec.iov_len = min(iter->count,
					iter->bvec->bv_len - iter->iov_offset);
		}

		if (rw == READ) {
			nr = file->f_op->read(file, iovec.iov_base,
					      iovec.iov_len, &kiocb->ki_pos);
		} else {
			nr = file->f_op->write(file, iovec.iov_base,
					       iovec.iov_len, &kiocb->ki_pos);
		}

		if (iov_iter_is_bvec(iter))
			kunmap(iter->bvec->bv_page);

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != iovec.iov_len)
			break;
		iov_iter_advance(iter, nr);
	}

	return ret;
}

static void io_req_map_rw(struct io_kiocb *req, ssize_t io_size,
			  struct iovec *iovec, struct iovec *fast_iov,
			  struct iov_iter *iter)
{
	req->io->rw.nr_segs = iter->nr_segs;
	req->io->rw.size = io_size;
	req->io->rw.iov = iovec;
	if (!req->io->rw.iov) {
		req->io->rw.iov = req->io->rw.fast_iov;
		memcpy(req->io->rw.iov, fast_iov,
			sizeof(struct iovec) * iter->nr_segs);
	} else {
		req->flags |= REQ_F_NEED_CLEANUP;
	}
}

static int io_alloc_async_ctx(struct io_kiocb *req)
{
	if (!io_op_defs[req->opcode].async_ctx)
		return 0;
	req->io = kmalloc(sizeof(*req->io), GFP_KERNEL);
	return req->io == NULL;
}

static int io_setup_async_rw(struct io_kiocb *req, ssize_t io_size,
			     struct iovec *iovec, struct iovec *fast_iov,
			     struct iov_iter *iter)
{
	if (!io_op_defs[req->opcode].async_ctx)
		return 0;
	if (!req->io) {
		if (io_alloc_async_ctx(req))
			return -ENOMEM;

		io_req_map_rw(req, io_size, iovec, fast_iov, iter);
	}
	return 0;
}

static int io_read_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			bool force_nonblock)
{
	struct io_async_ctx *io;
	struct iov_iter iter;
	ssize_t ret;

	ret = io_prep_rw(req, sqe, force_nonblock);
	if (ret)
		return ret;

	if (unlikely(!(req->file->f_mode & FMODE_READ)))
		return -EBADF;

	/* either don't need iovec imported or already have it */
	if (!req->io || req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	io = req->io;
	io->rw.iov = io->rw.fast_iov;
	req->io = NULL;
	ret = io_import_iovec(READ, req, &io->rw.iov, &iter);
	req->io = io;
	if (ret < 0)
		return ret;

	io_req_map_rw(req, ret, io->rw.iov, io->rw.fast_iov, &iter);
	return 0;
}

static int io_read(struct io_kiocb *req, struct io_kiocb **nxt,
		   bool force_nonblock)
{
	struct iovec inline_vecs[UIO_FASTIOV], *iovec = inline_vecs;
	struct kiocb *kiocb = &req->rw.kiocb;
	struct iov_iter iter;
	size_t iov_count;
	ssize_t io_size, ret;

	ret = io_import_iovec(READ, req, &iovec, &iter);
	if (ret < 0)
		return ret;

	/* Ensure we clear previously set non-block flag */
	if (!force_nonblock)
		req->rw.kiocb.ki_flags &= ~IOCB_NOWAIT;

	req->result = 0;
	io_size = ret;
	if (req->flags & REQ_F_LINK)
		req->result = io_size;

	/*
	 * If the file doesn't support async, mark it as REQ_F_MUST_PUNT so
	 * we know to async punt it even if it was opened O_NONBLOCK
	 */
	if (force_nonblock && !io_file_supports_async(req->file)) {
		req->flags |= REQ_F_MUST_PUNT;
		goto copy_iov;
	}

	iov_count = iov_iter_count(&iter);
	ret = rw_verify_area(READ, req->file, &kiocb->ki_pos, iov_count);
	if (!ret) {
		ssize_t ret2;

		if (req->file->f_op->read_iter)
			ret2 = call_read_iter(req->file, kiocb, &iter);
		else
			ret2 = loop_rw_iter(READ, req->file, kiocb, &iter);

		/* Catch -EAGAIN return for forced non-blocking submission */
		if (!force_nonblock || ret2 != -EAGAIN) {
			kiocb_done(kiocb, ret2, nxt, req->in_async);
		} else {
copy_iov:
			ret = io_setup_async_rw(req, io_size, iovec,
						inline_vecs, &iter);
			if (ret)
				goto out_free;
			return -EAGAIN;
		}
	}
out_free:
	kfree(iovec);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	return ret;
}

static int io_write_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			 bool force_nonblock)
{
	struct io_async_ctx *io;
	struct iov_iter iter;
	ssize_t ret;

	ret = io_prep_rw(req, sqe, force_nonblock);
	if (ret)
		return ret;

	if (unlikely(!(req->file->f_mode & FMODE_WRITE)))
		return -EBADF;

	/* either don't need iovec imported or already have it */
	if (!req->io || req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	io = req->io;
	io->rw.iov = io->rw.fast_iov;
	req->io = NULL;
	ret = io_import_iovec(WRITE, req, &io->rw.iov, &iter);
	req->io = io;
	if (ret < 0)
		return ret;

	io_req_map_rw(req, ret, io->rw.iov, io->rw.fast_iov, &iter);
	return 0;
}

static int io_write(struct io_kiocb *req, struct io_kiocb **nxt,
		    bool force_nonblock)
{
	struct iovec inline_vecs[UIO_FASTIOV], *iovec = inline_vecs;
	struct kiocb *kiocb = &req->rw.kiocb;
	struct iov_iter iter;
	size_t iov_count;
	ssize_t ret, io_size;

	ret = io_import_iovec(WRITE, req, &iovec, &iter);
	if (ret < 0)
		return ret;

	/* Ensure we clear previously set non-block flag */
	if (!force_nonblock)
		req->rw.kiocb.ki_flags &= ~IOCB_NOWAIT;

	req->result = 0;
	io_size = ret;
	if (req->flags & REQ_F_LINK)
		req->result = io_size;

	/*
	 * If the file doesn't support async, mark it as REQ_F_MUST_PUNT so
	 * we know to async punt it even if it was opened O_NONBLOCK
	 */
	if (force_nonblock && !io_file_supports_async(req->file)) {
		req->flags |= REQ_F_MUST_PUNT;
		goto copy_iov;
	}

	/* file path doesn't support NOWAIT for non-direct_IO */
	if (force_nonblock && !(kiocb->ki_flags & IOCB_DIRECT) &&
	    (req->flags & REQ_F_ISREG))
		goto copy_iov;

	iov_count = iov_iter_count(&iter);
	ret = rw_verify_area(WRITE, req->file, &kiocb->ki_pos, iov_count);
	if (!ret) {
		ssize_t ret2;

		/*
		 * Open-code file_start_write here to grab freeze protection,
		 * which will be released by another thread in
		 * io_complete_rw().  Fool lockdep by telling it the lock got
		 * released so that it doesn't complain about the held lock when
		 * we return to userspace.
		 */
		if (req->flags & REQ_F_ISREG) {
			__sb_start_write(file_inode(req->file)->i_sb,
						SB_FREEZE_WRITE, true);
			__sb_writers_release(file_inode(req->file)->i_sb,
						SB_FREEZE_WRITE);
		}
		kiocb->ki_flags |= IOCB_WRITE;

		if (req->file->f_op->write_iter)
			ret2 = call_write_iter(req->file, kiocb, &iter);
		else
			ret2 = loop_rw_iter(WRITE, req->file, kiocb, &iter);
		/*
		 * Raw bdev writes will -EOPNOTSUPP for IOCB_NOWAIT. Just
		 * retry them without IOCB_NOWAIT.
		 */
		if (ret2 == -EOPNOTSUPP && (kiocb->ki_flags & IOCB_NOWAIT))
			ret2 = -EAGAIN;
		if (!force_nonblock || ret2 != -EAGAIN) {
			kiocb_done(kiocb, ret2, nxt, req->in_async);
		} else {
copy_iov:
			ret = io_setup_async_rw(req, io_size, iovec,
						inline_vecs, &iter);
			if (ret)
				goto out_free;
			return -EAGAIN;
		}
	}
out_free:
	req->flags &= ~REQ_F_NEED_CLEANUP;
	kfree(iovec);
	return ret;
}

/*
 * IORING_OP_NOP just posts a completion event, nothing else.
 */
static int io_nop(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	if (unlikely(ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	io_cqring_add_event(req, 0);
	io_put_req(req);
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

static bool io_req_cancelled(struct io_kiocb *req)
{
	if (req->work.flags & IO_WQ_WORK_CANCEL) {
		req_set_fail_links(req);
		io_cqring_add_event(req, -ECANCELED);
		io_put_req(req);
		return true;
	}

	return false;
}

static void io_link_work_cb(struct io_wq_work **workptr)
{
	struct io_wq_work *work = *workptr;
	struct io_kiocb *link = work->data;

	io_queue_linked_timeout(link);
	work->func = io_wq_submit_work;
}

static void io_wq_assign_next(struct io_wq_work **workptr, struct io_kiocb *nxt)
{
	struct io_kiocb *link;

	io_prep_async_work(nxt, &link);
	*workptr = &nxt->work;
	if (link) {
		nxt->work.flags |= IO_WQ_WORK_CB;
		nxt->work.func = io_link_work_cb;
		nxt->work.data = link;
	}
}

static void io_fsync_finish(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);
	loff_t end = req->sync.off + req->sync.len;
	struct io_kiocb *nxt = NULL;
	int ret;

	if (io_req_cancelled(req))
		return;

	ret = vfs_fsync_range(req->file, req->sync.off,
				end > 0 ? end : LLONG_MAX,
				req->sync.flags & IORING_FSYNC_DATASYNC);
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, &nxt);
	if (nxt)
		io_wq_assign_next(workptr, nxt);
}

static int io_fsync(struct io_kiocb *req, struct io_kiocb **nxt,
		    bool force_nonblock)
{
	struct io_wq_work *work, *old_work;

	/* fsync always requires a blocking context */
	if (force_nonblock) {
		io_put_req(req);
		req->work.func = io_fsync_finish;
		return -EAGAIN;
	}

	work = old_work = &req->work;
	io_fsync_finish(&work);
	if (work && work != old_work)
		*nxt = container_of(work, struct io_kiocb, work);
	return 0;
}

static void io_fallocate_finish(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);
	struct io_kiocb *nxt = NULL;
	int ret;

	ret = vfs_fallocate(req->file, req->sync.mode, req->sync.off,
				req->sync.len);
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, &nxt);
	if (nxt)
		io_wq_assign_next(workptr, nxt);
}

static int io_fallocate_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe)
{
	if (sqe->ioprio || sqe->buf_index || sqe->rw_flags)
		return -EINVAL;

	req->sync.off = READ_ONCE(sqe->off);
	req->sync.len = READ_ONCE(sqe->addr);
	req->sync.mode = READ_ONCE(sqe->len);
	return 0;
}

static int io_fallocate(struct io_kiocb *req, struct io_kiocb **nxt,
			bool force_nonblock)
{
	struct io_wq_work *work, *old_work;

	/* fallocate always requiring blocking context */
	if (force_nonblock) {
		io_put_req(req);
		req->work.func = io_fallocate_finish;
		return -EAGAIN;
	}

	work = old_work = &req->work;
	io_fallocate_finish(&work);
	if (work && work != old_work)
		*nxt = container_of(work, struct io_kiocb, work);

	return 0;
}

static int io_openat_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	const char __user *fname;
	int ret;

	if (sqe->ioprio || sqe->buf_index)
		return -EINVAL;
	if (sqe->flags & IOSQE_FIXED_FILE)
		return -EBADF;
	if (req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	req->open.dfd = READ_ONCE(sqe->fd);
	req->open.how.mode = READ_ONCE(sqe->len);
	fname = u64_to_user_ptr(READ_ONCE(sqe->addr));
	req->open.how.flags = READ_ONCE(sqe->open_flags);

	req->open.filename = getname(fname);
	if (IS_ERR(req->open.filename)) {
		ret = PTR_ERR(req->open.filename);
		req->open.filename = NULL;
		return ret;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

static int io_openat2_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct open_how __user *how;
	const char __user *fname;
	size_t len;
	int ret;

	if (sqe->ioprio || sqe->buf_index)
		return -EINVAL;
	if (sqe->flags & IOSQE_FIXED_FILE)
		return -EBADF;
	if (req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	req->open.dfd = READ_ONCE(sqe->fd);
	fname = u64_to_user_ptr(READ_ONCE(sqe->addr));
	how = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	len = READ_ONCE(sqe->len);

	if (len < OPEN_HOW_SIZE_VER0)
		return -EINVAL;

	ret = copy_struct_from_user(&req->open.how, sizeof(req->open.how), how,
					len);
	if (ret)
		return ret;

	if (!(req->open.how.flags & O_PATH) && force_o_largefile())
		req->open.how.flags |= O_LARGEFILE;

	req->open.filename = getname(fname);
	if (IS_ERR(req->open.filename)) {
		ret = PTR_ERR(req->open.filename);
		req->open.filename = NULL;
		return ret;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

static int io_openat2(struct io_kiocb *req, struct io_kiocb **nxt,
		      bool force_nonblock)
{
	struct open_flags op;
	struct file *file;
	int ret;

	if (force_nonblock)
		return -EAGAIN;

	ret = build_open_flags(&req->open.how, &op);
	if (ret)
		goto err;

	ret = get_unused_fd_flags(req->open.how.flags);
	if (ret < 0)
		goto err;

	file = do_filp_open(req->open.dfd, req->open.filename, &op);
	if (IS_ERR(file)) {
		put_unused_fd(ret);
		ret = PTR_ERR(file);
	} else {
		fsnotify_open(file);
		fd_install(ret, file);
	}
err:
	putname(req->open.filename);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
	return 0;
}

static int io_openat(struct io_kiocb *req, struct io_kiocb **nxt,
		     bool force_nonblock)
{
	req->open.how = build_open_how(req->open.how.flags, req->open.how.mode);
	return io_openat2(req, nxt, force_nonblock);
}

static int io_epoll_ctl_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_EPOLL)
	if (sqe->ioprio || sqe->buf_index)
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

static int io_epoll_ctl(struct io_kiocb *req, struct io_kiocb **nxt,
			bool force_nonblock)
{
#if defined(CONFIG_EPOLL)
	struct io_epoll *ie = &req->epoll;
	int ret;

	ret = do_epoll_ctl(ie->epfd, ie->op, ie->fd, &ie->event, force_nonblock);
	if (force_nonblock && ret == -EAGAIN)
		return -EAGAIN;

	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
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

	req->madvise.addr = READ_ONCE(sqe->addr);
	req->madvise.len = READ_ONCE(sqe->len);
	req->madvise.advice = READ_ONCE(sqe->fadvise_advice);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_madvise(struct io_kiocb *req, struct io_kiocb **nxt,
		      bool force_nonblock)
{
#if defined(CONFIG_ADVISE_SYSCALLS) && defined(CONFIG_MMU)
	struct io_madvise *ma = &req->madvise;
	int ret;

	if (force_nonblock)
		return -EAGAIN;

	ret = do_madvise(ma->addr, ma->len, ma->advice);
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_fadvise_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	if (sqe->ioprio || sqe->buf_index || sqe->addr)
		return -EINVAL;

	req->fadvise.offset = READ_ONCE(sqe->off);
	req->fadvise.len = READ_ONCE(sqe->len);
	req->fadvise.advice = READ_ONCE(sqe->fadvise_advice);
	return 0;
}

static int io_fadvise(struct io_kiocb *req, struct io_kiocb **nxt,
		      bool force_nonblock)
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
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
	return 0;
}

static int io_statx_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	const char __user *fname;
	unsigned lookup_flags;
	int ret;

	if (sqe->ioprio || sqe->buf_index)
		return -EINVAL;
	if (sqe->flags & IOSQE_FIXED_FILE)
		return -EBADF;
	if (req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	req->open.dfd = READ_ONCE(sqe->fd);
	req->open.mask = READ_ONCE(sqe->len);
	fname = u64_to_user_ptr(READ_ONCE(sqe->addr));
	req->open.buffer = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	req->open.how.flags = READ_ONCE(sqe->statx_flags);

	if (vfs_stat_set_lookup_flags(&lookup_flags, req->open.how.flags))
		return -EINVAL;

	req->open.filename = getname_flags(fname, lookup_flags, NULL);
	if (IS_ERR(req->open.filename)) {
		ret = PTR_ERR(req->open.filename);
		req->open.filename = NULL;
		return ret;
	}

	req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

static int io_statx(struct io_kiocb *req, struct io_kiocb **nxt,
		    bool force_nonblock)
{
	struct io_open *ctx = &req->open;
	unsigned lookup_flags;
	struct path path;
	struct kstat stat;
	int ret;

	if (force_nonblock)
		return -EAGAIN;

	if (vfs_stat_set_lookup_flags(&lookup_flags, ctx->how.flags))
		return -EINVAL;

retry:
	/* filename_lookup() drops it, keep a reference */
	ctx->filename->refcnt++;

	ret = filename_lookup(ctx->dfd, ctx->filename, lookup_flags, &path,
				NULL);
	if (ret)
		goto err;

	ret = vfs_getattr(&path, &stat, ctx->mask, ctx->how.flags);
	path_put(&path);
	if (retry_estale(ret, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	if (!ret)
		ret = cp_statx(&stat, ctx->buffer);
err:
	putname(ctx->filename);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
	return 0;
}

static int io_close_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	/*
	 * If we queue this for async, it must not be cancellable. That would
	 * leave the 'file' in an undeterminate state.
	 */
	req->work.flags |= IO_WQ_WORK_NO_CANCEL;

	if (sqe->ioprio || sqe->off || sqe->addr || sqe->len ||
	    sqe->rw_flags || sqe->buf_index)
		return -EINVAL;
	if (sqe->flags & IOSQE_FIXED_FILE)
		return -EBADF;

	req->close.fd = READ_ONCE(sqe->fd);
	if (req->file->f_op == &io_uring_fops ||
	    req->close.fd == req->ctx->ring_fd)
		return -EBADF;

	return 0;
}

/* only called when __close_fd_get_file() is done */
static void __io_close_finish(struct io_kiocb *req, struct io_kiocb **nxt)
{
	int ret;

	ret = filp_close(req->close.put_file, req->work.files);
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	fput(req->close.put_file);
	io_put_req_find_next(req, nxt);
}

static void io_close_finish(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);
	struct io_kiocb *nxt = NULL;

	__io_close_finish(req, &nxt);
	if (nxt)
		io_wq_assign_next(workptr, nxt);
}

static int io_close(struct io_kiocb *req, struct io_kiocb **nxt,
		    bool force_nonblock)
{
	int ret;

	req->close.put_file = NULL;
	ret = __close_fd_get_file(req->close.fd, &req->close.put_file);
	if (ret < 0)
		return ret;

	/* if the file has a flush method, be safe and punt to async */
	if (req->close.put_file->f_op->flush && !io_wq_current_is_worker())
		goto eagain;

	/*
	 * No ->flush(), safely close from here and just punt the
	 * fput() to async context.
	 */
	__io_close_finish(req, nxt);
	return 0;
eagain:
	req->work.func = io_close_finish;
	/*
	 * Do manual async queue here to avoid grabbing files - we don't
	 * need the files, and it'll cause io_close_finish() to close
	 * the file again and cause a double CQE entry for this request
	 */
	io_queue_async_work(req);
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

static void io_sync_file_range_finish(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);
	struct io_kiocb *nxt = NULL;
	int ret;

	if (io_req_cancelled(req))
		return;

	ret = sync_file_range(req->file, req->sync.off, req->sync.len,
				req->sync.flags);
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, &nxt);
	if (nxt)
		io_wq_assign_next(workptr, nxt);
}

static int io_sync_file_range(struct io_kiocb *req, struct io_kiocb **nxt,
			      bool force_nonblock)
{
	struct io_wq_work *work, *old_work;

	/* sync_file_range always requires a blocking context */
	if (force_nonblock) {
		io_put_req(req);
		req->work.func = io_sync_file_range_finish;
		return -EAGAIN;
	}

	work = old_work = &req->work;
	io_sync_file_range_finish(&work);
	if (work && work != old_work)
		*nxt = container_of(work, struct io_kiocb, work);
	return 0;
}

static int io_sendmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_NET)
	struct io_sr_msg *sr = &req->sr_msg;
	struct io_async_ctx *io = req->io;
	int ret;

	sr->msg_flags = READ_ONCE(sqe->msg_flags);
	sr->msg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sr->len = READ_ONCE(sqe->len);

	if (!io || req->opcode == IORING_OP_SEND)
		return 0;
	/* iovec is already imported */
	if (req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	io->msg.iov = io->msg.fast_iov;
	ret = sendmsg_copy_msghdr(&io->msg.msg, sr->msg, sr->msg_flags,
					&io->msg.iov);
	if (!ret)
		req->flags |= REQ_F_NEED_CLEANUP;
	return ret;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_sendmsg(struct io_kiocb *req, struct io_kiocb **nxt,
		      bool force_nonblock)
{
#if defined(CONFIG_NET)
	struct io_async_msghdr *kmsg = NULL;
	struct socket *sock;
	int ret;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sock = sock_from_file(req->file, &ret);
	if (sock) {
		struct io_async_ctx io;
		unsigned flags;

		if (req->io) {
			kmsg = &req->io->msg;
			kmsg->msg.msg_name = &req->io->msg.addr;
			/* if iov is set, it's allocated already */
			if (!kmsg->iov)
				kmsg->iov = kmsg->fast_iov;
			kmsg->msg.msg_iter.iov = kmsg->iov;
		} else {
			struct io_sr_msg *sr = &req->sr_msg;

			kmsg = &io.msg;
			kmsg->msg.msg_name = &io.msg.addr;

			io.msg.iov = io.msg.fast_iov;
			ret = sendmsg_copy_msghdr(&io.msg.msg, sr->msg,
					sr->msg_flags, &io.msg.iov);
			if (ret)
				return ret;
		}

		flags = req->sr_msg.msg_flags;
		if (flags & MSG_DONTWAIT)
			req->flags |= REQ_F_NOWAIT;
		else if (force_nonblock)
			flags |= MSG_DONTWAIT;

		ret = __sys_sendmsg_sock(sock, &kmsg->msg, flags);
		if (force_nonblock && ret == -EAGAIN) {
			if (req->io)
				return -EAGAIN;
			if (io_alloc_async_ctx(req)) {
				if (kmsg && kmsg->iov != kmsg->fast_iov)
					kfree(kmsg->iov);
				return -ENOMEM;
			}
			req->flags |= REQ_F_NEED_CLEANUP;
			memcpy(&req->io->msg, &io.msg, sizeof(io.msg));
			return -EAGAIN;
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
	}

	if (kmsg && kmsg->iov != kmsg->fast_iov)
		kfree(kmsg->iov);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_cqring_add_event(req, ret);
	if (ret < 0)
		req_set_fail_links(req);
	io_put_req_find_next(req, nxt);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_send(struct io_kiocb *req, struct io_kiocb **nxt,
		   bool force_nonblock)
{
#if defined(CONFIG_NET)
	struct socket *sock;
	int ret;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sock = sock_from_file(req->file, &ret);
	if (sock) {
		struct io_sr_msg *sr = &req->sr_msg;
		struct msghdr msg;
		struct iovec iov;
		unsigned flags;

		ret = import_single_range(WRITE, sr->buf, sr->len, &iov,
						&msg.msg_iter);
		if (ret)
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
	}

	io_cqring_add_event(req, ret);
	if (ret < 0)
		req_set_fail_links(req);
	io_put_req_find_next(req, nxt);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_recvmsg_prep(struct io_kiocb *req,
			   const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_NET)
	struct io_sr_msg *sr = &req->sr_msg;
	struct io_async_ctx *io = req->io;
	int ret;

	sr->msg_flags = READ_ONCE(sqe->msg_flags);
	sr->msg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sr->len = READ_ONCE(sqe->len);

	if (!io || req->opcode == IORING_OP_RECV)
		return 0;
	/* iovec is already imported */
	if (req->flags & REQ_F_NEED_CLEANUP)
		return 0;

	io->msg.iov = io->msg.fast_iov;
	ret = recvmsg_copy_msghdr(&io->msg.msg, sr->msg, sr->msg_flags,
					&io->msg.uaddr, &io->msg.iov);
	if (!ret)
		req->flags |= REQ_F_NEED_CLEANUP;
	return ret;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_recvmsg(struct io_kiocb *req, struct io_kiocb **nxt,
		      bool force_nonblock)
{
#if defined(CONFIG_NET)
	struct io_async_msghdr *kmsg = NULL;
	struct socket *sock;
	int ret;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sock = sock_from_file(req->file, &ret);
	if (sock) {
		struct io_async_ctx io;
		unsigned flags;

		if (req->io) {
			kmsg = &req->io->msg;
			kmsg->msg.msg_name = &req->io->msg.addr;
			/* if iov is set, it's allocated already */
			if (!kmsg->iov)
				kmsg->iov = kmsg->fast_iov;
			kmsg->msg.msg_iter.iov = kmsg->iov;
		} else {
			struct io_sr_msg *sr = &req->sr_msg;

			kmsg = &io.msg;
			kmsg->msg.msg_name = &io.msg.addr;

			io.msg.iov = io.msg.fast_iov;
			ret = recvmsg_copy_msghdr(&io.msg.msg, sr->msg,
					sr->msg_flags, &io.msg.uaddr,
					&io.msg.iov);
			if (ret)
				return ret;
		}

		flags = req->sr_msg.msg_flags;
		if (flags & MSG_DONTWAIT)
			req->flags |= REQ_F_NOWAIT;
		else if (force_nonblock)
			flags |= MSG_DONTWAIT;

		ret = __sys_recvmsg_sock(sock, &kmsg->msg, req->sr_msg.msg,
						kmsg->uaddr, flags);
		if (force_nonblock && ret == -EAGAIN) {
			if (req->io)
				return -EAGAIN;
			if (io_alloc_async_ctx(req)) {
				if (kmsg && kmsg->iov != kmsg->fast_iov)
					kfree(kmsg->iov);
				return -ENOMEM;
			}
			memcpy(&req->io->msg, &io.msg, sizeof(io.msg));
			req->flags |= REQ_F_NEED_CLEANUP;
			return -EAGAIN;
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
	}

	if (kmsg && kmsg->iov != kmsg->fast_iov)
		kfree(kmsg->iov);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_cqring_add_event(req, ret);
	if (ret < 0)
		req_set_fail_links(req);
	io_put_req_find_next(req, nxt);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_recv(struct io_kiocb *req, struct io_kiocb **nxt,
		   bool force_nonblock)
{
#if defined(CONFIG_NET)
	struct socket *sock;
	int ret;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;

	sock = sock_from_file(req->file, &ret);
	if (sock) {
		struct io_sr_msg *sr = &req->sr_msg;
		struct msghdr msg;
		struct iovec iov;
		unsigned flags;

		ret = import_single_range(READ, sr->buf, sr->len, &iov,
						&msg.msg_iter);
		if (ret)
			return ret;

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
	}

	io_cqring_add_event(req, ret);
	if (ret < 0)
		req_set_fail_links(req);
	io_put_req_find_next(req, nxt);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}


static int io_accept_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_NET)
	struct io_accept *accept = &req->accept;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	if (sqe->ioprio || sqe->len || sqe->buf_index)
		return -EINVAL;

	accept->addr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	accept->addr_len = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	accept->flags = READ_ONCE(sqe->accept_flags);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

#if defined(CONFIG_NET)
static int __io_accept(struct io_kiocb *req, struct io_kiocb **nxt,
		       bool force_nonblock)
{
	struct io_accept *accept = &req->accept;
	unsigned file_flags;
	int ret;

	file_flags = force_nonblock ? O_NONBLOCK : 0;
	ret = __sys_accept4_file(req->file, file_flags, accept->addr,
					accept->addr_len, accept->flags);
	if (ret == -EAGAIN && force_nonblock)
		return -EAGAIN;
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
	return 0;
}

static void io_accept_finish(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);
	struct io_kiocb *nxt = NULL;

	if (io_req_cancelled(req))
		return;
	__io_accept(req, &nxt, false);
	if (nxt)
		io_wq_assign_next(workptr, nxt);
}
#endif

static int io_accept(struct io_kiocb *req, struct io_kiocb **nxt,
		     bool force_nonblock)
{
#if defined(CONFIG_NET)
	int ret;

	ret = __io_accept(req, nxt, force_nonblock);
	if (ret == -EAGAIN && force_nonblock) {
		req->work.func = io_accept_finish;
		io_put_req(req);
		return -EAGAIN;
	}
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static int io_connect_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
#if defined(CONFIG_NET)
	struct io_connect *conn = &req->connect;
	struct io_async_ctx *io = req->io;

	if (unlikely(req->ctx->flags & (IORING_SETUP_IOPOLL|IORING_SETUP_SQPOLL)))
		return -EINVAL;
	if (sqe->ioprio || sqe->len || sqe->buf_index || sqe->rw_flags)
		return -EINVAL;

	conn->addr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	conn->addr_len =  READ_ONCE(sqe->addr2);

	if (!io)
		return 0;

	return move_addr_to_kernel(conn->addr, conn->addr_len,
					&io->connect.address);
#else
	return -EOPNOTSUPP;
#endif
}

static int io_connect(struct io_kiocb *req, struct io_kiocb **nxt,
		      bool force_nonblock)
{
#if defined(CONFIG_NET)
	struct io_async_ctx __io, *io;
	unsigned file_flags;
	int ret;

	if (req->io) {
		io = req->io;
	} else {
		ret = move_addr_to_kernel(req->connect.addr,
						req->connect.addr_len,
						&__io.connect.address);
		if (ret)
			goto out;
		io = &__io;
	}

	file_flags = force_nonblock ? O_NONBLOCK : 0;

	ret = __sys_connect_file(req->file, &io->connect.address,
					req->connect.addr_len, file_flags);
	if ((ret == -EAGAIN || ret == -EINPROGRESS) && force_nonblock) {
		if (req->io)
			return -EAGAIN;
		if (io_alloc_async_ctx(req)) {
			ret = -ENOMEM;
			goto out;
		}
		memcpy(&req->io->connect, &__io.connect, sizeof(__io.connect));
		return -EAGAIN;
	}
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
out:
	if (ret < 0)
		req_set_fail_links(req);
	io_cqring_add_event(req, ret);
	io_put_req_find_next(req, nxt);
	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static void io_poll_remove_one(struct io_kiocb *req)
{
	struct io_poll_iocb *poll = &req->poll;

	spin_lock(&poll->head->lock);
	WRITE_ONCE(poll->canceled, true);
	if (!list_empty(&poll->wait.entry)) {
		list_del_init(&poll->wait.entry);
		io_queue_async_work(req);
	}
	spin_unlock(&poll->head->lock);
	hash_del(&req->hash_node);
}

static void io_poll_remove_all(struct io_ring_ctx *ctx)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	int i;

	spin_lock_irq(&ctx->completion_lock);
	for (i = 0; i < (1U << ctx->cancel_hash_bits); i++) {
		struct hlist_head *list;

		list = &ctx->cancel_hash[i];
		hlist_for_each_entry_safe(req, tmp, list, hash_node)
			io_poll_remove_one(req);
	}
	spin_unlock_irq(&ctx->completion_lock);
}

static int io_poll_cancel(struct io_ring_ctx *ctx, __u64 sqe_addr)
{
	struct hlist_head *list;
	struct io_kiocb *req;

	list = &ctx->cancel_hash[hash_long(sqe_addr, ctx->cancel_hash_bits)];
	hlist_for_each_entry(req, list, hash_node) {
		if (sqe_addr == req->user_data) {
			io_poll_remove_one(req);
			return 0;
		}
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

	io_cqring_add_event(req, ret);
	if (ret < 0)
		req_set_fail_links(req);
	io_put_req(req);
	return 0;
}

static void io_poll_complete(struct io_kiocb *req, __poll_t mask, int error)
{
	struct io_ring_ctx *ctx = req->ctx;

	req->poll.done = true;
	if (error)
		io_cqring_fill_event(req, error);
	else
		io_cqring_fill_event(req, mangle_poll(mask));
	io_commit_cqring(ctx);
}

static void io_poll_complete_work(struct io_wq_work **workptr)
{
	struct io_wq_work *work = *workptr;
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_poll_iocb *poll = &req->poll;
	struct poll_table_struct pt = { ._key = poll->events };
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *nxt = NULL;
	__poll_t mask = 0;
	int ret = 0;

	if (work->flags & IO_WQ_WORK_CANCEL) {
		WRITE_ONCE(poll->canceled, true);
		ret = -ECANCELED;
	} else if (READ_ONCE(poll->canceled)) {
		ret = -ECANCELED;
	}

	if (ret != -ECANCELED)
		mask = vfs_poll(poll->file, &pt) & poll->events;

	/*
	 * Note that ->ki_cancel callers also delete iocb from active_reqs after
	 * calling ->ki_cancel.  We need the ctx_lock roundtrip here to
	 * synchronize with them.  In the cancellation case the list_del_init
	 * itself is not actually needed, but harmless so we keep it in to
	 * avoid further branches in the fast path.
	 */
	spin_lock_irq(&ctx->completion_lock);
	if (!mask && ret != -ECANCELED) {
		add_wait_queue(poll->head, &poll->wait);
		spin_unlock_irq(&ctx->completion_lock);
		return;
	}
	hash_del(&req->hash_node);
	io_poll_complete(req, mask, ret);
	spin_unlock_irq(&ctx->completion_lock);

	io_cqring_ev_posted(ctx);

	if (ret < 0)
		req_set_fail_links(req);
	io_put_req_find_next(req, &nxt);
	if (nxt)
		io_wq_assign_next(workptr, nxt);
}

static void __io_poll_flush(struct io_ring_ctx *ctx, struct llist_node *nodes)
{
	struct io_kiocb *req, *tmp;
	struct req_batch rb;

	rb.to_free = rb.need_iter = 0;
	spin_lock_irq(&ctx->completion_lock);
	llist_for_each_entry_safe(req, tmp, nodes, llist_node) {
		hash_del(&req->hash_node);
		io_poll_complete(req, req->result, 0);

		if (refcount_dec_and_test(&req->refs) &&
		    !io_req_multi_free(&rb, req)) {
			req->flags |= REQ_F_COMP_LOCKED;
			io_free_req(req);
		}
	}
	spin_unlock_irq(&ctx->completion_lock);

	io_cqring_ev_posted(ctx);
	io_free_req_many(ctx, &rb);
}

static void io_poll_flush(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);
	struct llist_node *nodes;

	nodes = llist_del_all(&req->ctx->poll_llist);
	if (nodes)
		__io_poll_flush(req->ctx, nodes);
}

static void io_poll_trigger_evfd(struct io_wq_work **workptr)
{
	struct io_kiocb *req = container_of(*workptr, struct io_kiocb, work);

	eventfd_signal(req->ctx->cq_ev_fd, 1);
	io_put_req(req);
}

static int io_poll_wake(struct wait_queue_entry *wait, unsigned mode, int sync,
			void *key)
{
	struct io_poll_iocb *poll = wait->private;
	struct io_kiocb *req = container_of(poll, struct io_kiocb, poll);
	struct io_ring_ctx *ctx = req->ctx;
	__poll_t mask = key_to_poll(key);

	/* for instances that support it check for an event match first: */
	if (mask && !(mask & poll->events))
		return 0;

	list_del_init(&poll->wait.entry);

	/*
	 * Run completion inline if we can. We're using trylock here because
	 * we are violating the completion_lock -> poll wq lock ordering.
	 * If we have a link timeout we're going to need the completion_lock
	 * for finalizing the request, mark us as having grabbed that already.
	 */
	if (mask) {
		unsigned long flags;

		if (llist_empty(&ctx->poll_llist) &&
		    spin_trylock_irqsave(&ctx->completion_lock, flags)) {
			bool trigger_ev;

			hash_del(&req->hash_node);
			io_poll_complete(req, mask, 0);

			trigger_ev = io_should_trigger_evfd(ctx);
			if (trigger_ev && eventfd_signal_count()) {
				trigger_ev = false;
				req->work.func = io_poll_trigger_evfd;
			} else {
				req->flags |= REQ_F_COMP_LOCKED;
				io_put_req(req);
				req = NULL;
			}
			spin_unlock_irqrestore(&ctx->completion_lock, flags);
			__io_cqring_ev_posted(ctx, trigger_ev);
		} else {
			req->result = mask;
			req->llist_node.next = NULL;
			/* if the list wasn't empty, we're done */
			if (!llist_add(&req->llist_node, &ctx->poll_llist))
				req = NULL;
			else
				req->work.func = io_poll_flush;
		}
	}
	if (req)
		io_queue_async_work(req);

	return 1;
}

struct io_poll_table {
	struct poll_table_struct pt;
	struct io_kiocb *req;
	int error;
};

static void io_poll_queue_proc(struct file *file, struct wait_queue_head *head,
			       struct poll_table_struct *p)
{
	struct io_poll_table *pt = container_of(p, struct io_poll_table, pt);

	if (unlikely(pt->req->poll.head)) {
		pt->error = -EINVAL;
		return;
	}

	pt->error = 0;
	pt->req->poll.head = head;
	add_wait_queue(head, &pt->req->poll.wait);
}

static void io_poll_req_insert(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct hlist_head *list;

	list = &ctx->cancel_hash[hash_long(req->user_data, ctx->cancel_hash_bits)];
	hlist_add_head(&req->hash_node, list);
}

static int io_poll_add_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_poll_iocb *poll = &req->poll;
	u16 events;

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->addr || sqe->ioprio || sqe->off || sqe->len || sqe->buf_index)
		return -EINVAL;
	if (!poll->file)
		return -EBADF;

	events = READ_ONCE(sqe->poll_events);
	poll->events = demangle_poll(events) | EPOLLERR | EPOLLHUP;
	return 0;
}

static int io_poll_add(struct io_kiocb *req, struct io_kiocb **nxt)
{
	struct io_poll_iocb *poll = &req->poll;
	struct io_ring_ctx *ctx = req->ctx;
	struct io_poll_table ipt;
	bool cancel = false;
	__poll_t mask;

	INIT_IO_WORK(&req->work, io_poll_complete_work);
	INIT_HLIST_NODE(&req->hash_node);

	poll->head = NULL;
	poll->done = false;
	poll->canceled = false;

	ipt.pt._qproc = io_poll_queue_proc;
	ipt.pt._key = poll->events;
	ipt.req = req;
	ipt.error = -EINVAL; /* same as no support for IOCB_CMD_POLL */

	/* initialized the list so that we can do list_empty checks */
	INIT_LIST_HEAD(&poll->wait.entry);
	init_waitqueue_func_entry(&poll->wait, io_poll_wake);
	poll->wait.private = poll;

	INIT_LIST_HEAD(&req->list);

	mask = vfs_poll(poll->file, &ipt.pt) & poll->events;

	spin_lock_irq(&ctx->completion_lock);
	if (likely(poll->head)) {
		spin_lock(&poll->head->lock);
		if (unlikely(list_empty(&poll->wait.entry))) {
			if (ipt.error)
				cancel = true;
			ipt.error = 0;
			mask = 0;
		}
		if (mask || ipt.error)
			list_del_init(&poll->wait.entry);
		else if (cancel)
			WRITE_ONCE(poll->canceled, true);
		else if (!poll->done) /* actually waiting for an event */
			io_poll_req_insert(req);
		spin_unlock(&poll->head->lock);
	}
	if (mask) { /* no async, we'd stolen it */
		ipt.error = 0;
		io_poll_complete(req, mask, 0);
	}
	spin_unlock_irq(&ctx->completion_lock);

	if (mask) {
		io_cqring_ev_posted(ctx);
		io_put_req_find_next(req, nxt);
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

	atomic_inc(&ctx->cq_timeouts);

	spin_lock_irqsave(&ctx->completion_lock, flags);
	/*
	 * We could be racing with timeout deletion. If the list is empty,
	 * then timeout lookup already found it and will be handling it.
	 */
	if (!list_empty(&req->list)) {
		struct io_kiocb *prev;

		/*
		 * Adjust the reqs sequence before the current one because it
		 * will consume a slot in the cq_ring and the cq_tail
		 * pointer will be increased, otherwise other timeout reqs may
		 * return in advance without waiting for enough wait_nr.
		 */
		prev = req;
		list_for_each_entry_continue_reverse(prev, &ctx->timeout_list, list)
			prev->sequence++;
		list_del_init(&req->list);
	}

	io_cqring_fill_event(req, -ETIME);
	io_commit_cqring(ctx);
	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	io_cqring_ev_posted(ctx);
	req_set_fail_links(req);
	io_put_req(req);
	return HRTIMER_NORESTART;
}

static int io_timeout_cancel(struct io_ring_ctx *ctx, __u64 user_data)
{
	struct io_kiocb *req;
	int ret = -ENOENT;

	list_for_each_entry(req, &ctx->timeout_list, list) {
		if (user_data == req->user_data) {
			list_del_init(&req->list);
			ret = 0;
			break;
		}
	}

	if (ret == -ENOENT)
		return ret;

	ret = hrtimer_try_to_cancel(&req->io->timeout.timer);
	if (ret == -1)
		return -EALREADY;

	req_set_fail_links(req);
	io_cqring_fill_event(req, -ECANCELED);
	io_put_req(req);
	return 0;
}

static int io_timeout_remove_prep(struct io_kiocb *req,
				  const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->flags || sqe->ioprio || sqe->buf_index || sqe->len)
		return -EINVAL;

	req->timeout.addr = READ_ONCE(sqe->addr);
	req->timeout.flags = READ_ONCE(sqe->timeout_flags);
	if (req->timeout.flags)
		return -EINVAL;

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
	ret = io_timeout_cancel(ctx, req->timeout.addr);

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

	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->ioprio || sqe->buf_index || sqe->len != 1)
		return -EINVAL;
	if (sqe->off && is_timeout_link)
		return -EINVAL;
	flags = READ_ONCE(sqe->timeout_flags);
	if (flags & ~IORING_TIMEOUT_ABS)
		return -EINVAL;

	req->timeout.count = READ_ONCE(sqe->off);

	if (!req->io && io_alloc_async_ctx(req))
		return -ENOMEM;

	data = &req->io->timeout;
	data->req = req;
	req->flags |= REQ_F_TIMEOUT;

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
	unsigned count;
	struct io_ring_ctx *ctx = req->ctx;
	struct io_timeout_data *data;
	struct list_head *entry;
	unsigned span = 0;

	data = &req->io->timeout;

	/*
	 * sqe->off holds how many events that need to occur for this
	 * timeout event to be satisfied. If it isn't set, then this is
	 * a pure timeout request, sequence isn't used.
	 */
	count = req->timeout.count;
	if (!count) {
		req->flags |= REQ_F_TIMEOUT_NOSEQ;
		spin_lock_irq(&ctx->completion_lock);
		entry = ctx->timeout_list.prev;
		goto add;
	}

	req->sequence = ctx->cached_sq_head + count - 1;
	data->seq_offset = count;

	/*
	 * Insertion sort, ensuring the first entry in the list is always
	 * the one we need first.
	 */
	spin_lock_irq(&ctx->completion_lock);
	list_for_each_prev(entry, &ctx->timeout_list) {
		struct io_kiocb *nxt = list_entry(entry, struct io_kiocb, list);
		unsigned nxt_sq_head;
		long long tmp, tmp_nxt;
		u32 nxt_offset = nxt->io->timeout.seq_offset;

		if (nxt->flags & REQ_F_TIMEOUT_NOSEQ)
			continue;

		/*
		 * Since cached_sq_head + count - 1 can overflow, use type long
		 * long to store it.
		 */
		tmp = (long long)ctx->cached_sq_head + count - 1;
		nxt_sq_head = nxt->sequence - nxt_offset + 1;
		tmp_nxt = (long long)nxt_sq_head + nxt_offset - 1;

		/*
		 * cached_sq_head may overflow, and it will never overflow twice
		 * once there is some timeout req still be valid.
		 */
		if (ctx->cached_sq_head < nxt_sq_head)
			tmp += UINT_MAX;

		if (tmp > tmp_nxt)
			break;

		/*
		 * Sequence of reqs after the insert one and itself should
		 * be adjusted because each timeout req consumes a slot.
		 */
		span++;
		nxt->sequence++;
	}
	req->sequence -= span;
add:
	list_add(&req->list, entry);
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

	cancel_ret = io_wq_cancel_cb(ctx->io_wq, io_cancel_cb, sqe_addr);
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
				     struct io_kiocb **nxt, int success_ret)
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
	io_put_req_find_next(req, nxt);
}

static int io_async_cancel_prep(struct io_kiocb *req,
				const struct io_uring_sqe *sqe)
{
	if (unlikely(req->ctx->flags & IORING_SETUP_IOPOLL))
		return -EINVAL;
	if (sqe->flags || sqe->ioprio || sqe->off || sqe->len ||
	    sqe->cancel_flags)
		return -EINVAL;

	req->cancel.addr = READ_ONCE(sqe->addr);
	return 0;
}

static int io_async_cancel(struct io_kiocb *req, struct io_kiocb **nxt)
{
	struct io_ring_ctx *ctx = req->ctx;

	io_async_find_and_cancel(ctx, req, req->cancel.addr, nxt, 0);
	return 0;
}

static int io_files_update_prep(struct io_kiocb *req,
				const struct io_uring_sqe *sqe)
{
	if (sqe->flags || sqe->ioprio || sqe->rw_flags)
		return -EINVAL;

	req->files_update.offset = READ_ONCE(sqe->off);
	req->files_update.nr_args = READ_ONCE(sqe->len);
	if (!req->files_update.nr_args)
		return -EINVAL;
	req->files_update.arg = READ_ONCE(sqe->addr);
	return 0;
}

static int io_files_update(struct io_kiocb *req, bool force_nonblock)
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
	io_cqring_add_event(req, ret);
	io_put_req(req);
	return 0;
}

static int io_req_defer_prep(struct io_kiocb *req,
			     const struct io_uring_sqe *sqe)
{
	ssize_t ret = 0;

	if (io_op_defs[req->opcode].file_table) {
		ret = io_grab_files(req);
		if (unlikely(ret))
			return ret;
	}

	io_req_work_grab_env(req, &io_op_defs[req->opcode]);

	switch (req->opcode) {
	case IORING_OP_NOP:
		break;
	case IORING_OP_READV:
	case IORING_OP_READ_FIXED:
	case IORING_OP_READ:
		ret = io_read_prep(req, sqe, true);
		break;
	case IORING_OP_WRITEV:
	case IORING_OP_WRITE_FIXED:
	case IORING_OP_WRITE:
		ret = io_write_prep(req, sqe, true);
		break;
	case IORING_OP_POLL_ADD:
		ret = io_poll_add_prep(req, sqe);
		break;
	case IORING_OP_POLL_REMOVE:
		ret = io_poll_remove_prep(req, sqe);
		break;
	case IORING_OP_FSYNC:
		ret = io_prep_fsync(req, sqe);
		break;
	case IORING_OP_SYNC_FILE_RANGE:
		ret = io_prep_sfr(req, sqe);
		break;
	case IORING_OP_SENDMSG:
	case IORING_OP_SEND:
		ret = io_sendmsg_prep(req, sqe);
		break;
	case IORING_OP_RECVMSG:
	case IORING_OP_RECV:
		ret = io_recvmsg_prep(req, sqe);
		break;
	case IORING_OP_CONNECT:
		ret = io_connect_prep(req, sqe);
		break;
	case IORING_OP_TIMEOUT:
		ret = io_timeout_prep(req, sqe, false);
		break;
	case IORING_OP_TIMEOUT_REMOVE:
		ret = io_timeout_remove_prep(req, sqe);
		break;
	case IORING_OP_ASYNC_CANCEL:
		ret = io_async_cancel_prep(req, sqe);
		break;
	case IORING_OP_LINK_TIMEOUT:
		ret = io_timeout_prep(req, sqe, true);
		break;
	case IORING_OP_ACCEPT:
		ret = io_accept_prep(req, sqe);
		break;
	case IORING_OP_FALLOCATE:
		ret = io_fallocate_prep(req, sqe);
		break;
	case IORING_OP_OPENAT:
		ret = io_openat_prep(req, sqe);
		break;
	case IORING_OP_CLOSE:
		ret = io_close_prep(req, sqe);
		break;
	case IORING_OP_FILES_UPDATE:
		ret = io_files_update_prep(req, sqe);
		break;
	case IORING_OP_STATX:
		ret = io_statx_prep(req, sqe);
		break;
	case IORING_OP_FADVISE:
		ret = io_fadvise_prep(req, sqe);
		break;
	case IORING_OP_MADVISE:
		ret = io_madvise_prep(req, sqe);
		break;
	case IORING_OP_OPENAT2:
		ret = io_openat2_prep(req, sqe);
		break;
	case IORING_OP_EPOLL_CTL:
		ret = io_epoll_ctl_prep(req, sqe);
		break;
	default:
		printk_once(KERN_WARNING "io_uring: unhandled opcode %d\n",
				req->opcode);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int io_req_defer(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	/* Still need defer if there is pending req in defer list. */
	if (!req_need_defer(req) && list_empty(&ctx->defer_list))
		return 0;

	if (!req->io && io_alloc_async_ctx(req))
		return -EAGAIN;

	ret = io_req_defer_prep(req, sqe);
	if (ret < 0)
		return ret;

	spin_lock_irq(&ctx->completion_lock);
	if (!req_need_defer(req) && list_empty(&ctx->defer_list)) {
		spin_unlock_irq(&ctx->completion_lock);
		return 0;
	}

	trace_io_uring_defer(ctx, req, req->user_data);
	list_add_tail(&req->list, &ctx->defer_list);
	spin_unlock_irq(&ctx->completion_lock);
	return -EIOCBQUEUED;
}

static void io_cleanup_req(struct io_kiocb *req)
{
	struct io_async_ctx *io = req->io;

	switch (req->opcode) {
	case IORING_OP_READV:
	case IORING_OP_READ_FIXED:
	case IORING_OP_READ:
	case IORING_OP_WRITEV:
	case IORING_OP_WRITE_FIXED:
	case IORING_OP_WRITE:
		if (io->rw.iov != io->rw.fast_iov)
			kfree(io->rw.iov);
		break;
	case IORING_OP_SENDMSG:
	case IORING_OP_RECVMSG:
		if (io->msg.iov != io->msg.fast_iov)
			kfree(io->msg.iov);
		break;
	case IORING_OP_OPENAT:
	case IORING_OP_OPENAT2:
	case IORING_OP_STATX:
		putname(req->open.filename);
		break;
	}

	req->flags &= ~REQ_F_NEED_CLEANUP;
}

static int io_issue_sqe(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			struct io_kiocb **nxt, bool force_nonblock)
{
	struct io_ring_ctx *ctx = req->ctx;
	int ret;

	switch (req->opcode) {
	case IORING_OP_NOP:
		ret = io_nop(req);
		break;
	case IORING_OP_READV:
	case IORING_OP_READ_FIXED:
	case IORING_OP_READ:
		if (sqe) {
			ret = io_read_prep(req, sqe, force_nonblock);
			if (ret < 0)
				break;
		}
		ret = io_read(req, nxt, force_nonblock);
		break;
	case IORING_OP_WRITEV:
	case IORING_OP_WRITE_FIXED:
	case IORING_OP_WRITE:
		if (sqe) {
			ret = io_write_prep(req, sqe, force_nonblock);
			if (ret < 0)
				break;
		}
		ret = io_write(req, nxt, force_nonblock);
		break;
	case IORING_OP_FSYNC:
		if (sqe) {
			ret = io_prep_fsync(req, sqe);
			if (ret < 0)
				break;
		}
		ret = io_fsync(req, nxt, force_nonblock);
		break;
	case IORING_OP_POLL_ADD:
		if (sqe) {
			ret = io_poll_add_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_poll_add(req, nxt);
		break;
	case IORING_OP_POLL_REMOVE:
		if (sqe) {
			ret = io_poll_remove_prep(req, sqe);
			if (ret < 0)
				break;
		}
		ret = io_poll_remove(req);
		break;
	case IORING_OP_SYNC_FILE_RANGE:
		if (sqe) {
			ret = io_prep_sfr(req, sqe);
			if (ret < 0)
				break;
		}
		ret = io_sync_file_range(req, nxt, force_nonblock);
		break;
	case IORING_OP_SENDMSG:
	case IORING_OP_SEND:
		if (sqe) {
			ret = io_sendmsg_prep(req, sqe);
			if (ret < 0)
				break;
		}
		if (req->opcode == IORING_OP_SENDMSG)
			ret = io_sendmsg(req, nxt, force_nonblock);
		else
			ret = io_send(req, nxt, force_nonblock);
		break;
	case IORING_OP_RECVMSG:
	case IORING_OP_RECV:
		if (sqe) {
			ret = io_recvmsg_prep(req, sqe);
			if (ret)
				break;
		}
		if (req->opcode == IORING_OP_RECVMSG)
			ret = io_recvmsg(req, nxt, force_nonblock);
		else
			ret = io_recv(req, nxt, force_nonblock);
		break;
	case IORING_OP_TIMEOUT:
		if (sqe) {
			ret = io_timeout_prep(req, sqe, false);
			if (ret)
				break;
		}
		ret = io_timeout(req);
		break;
	case IORING_OP_TIMEOUT_REMOVE:
		if (sqe) {
			ret = io_timeout_remove_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_timeout_remove(req);
		break;
	case IORING_OP_ACCEPT:
		if (sqe) {
			ret = io_accept_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_accept(req, nxt, force_nonblock);
		break;
	case IORING_OP_CONNECT:
		if (sqe) {
			ret = io_connect_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_connect(req, nxt, force_nonblock);
		break;
	case IORING_OP_ASYNC_CANCEL:
		if (sqe) {
			ret = io_async_cancel_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_async_cancel(req, nxt);
		break;
	case IORING_OP_FALLOCATE:
		if (sqe) {
			ret = io_fallocate_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_fallocate(req, nxt, force_nonblock);
		break;
	case IORING_OP_OPENAT:
		if (sqe) {
			ret = io_openat_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_openat(req, nxt, force_nonblock);
		break;
	case IORING_OP_CLOSE:
		if (sqe) {
			ret = io_close_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_close(req, nxt, force_nonblock);
		break;
	case IORING_OP_FILES_UPDATE:
		if (sqe) {
			ret = io_files_update_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_files_update(req, force_nonblock);
		break;
	case IORING_OP_STATX:
		if (sqe) {
			ret = io_statx_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_statx(req, nxt, force_nonblock);
		break;
	case IORING_OP_FADVISE:
		if (sqe) {
			ret = io_fadvise_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_fadvise(req, nxt, force_nonblock);
		break;
	case IORING_OP_MADVISE:
		if (sqe) {
			ret = io_madvise_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_madvise(req, nxt, force_nonblock);
		break;
	case IORING_OP_OPENAT2:
		if (sqe) {
			ret = io_openat2_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_openat2(req, nxt, force_nonblock);
		break;
	case IORING_OP_EPOLL_CTL:
		if (sqe) {
			ret = io_epoll_ctl_prep(req, sqe);
			if (ret)
				break;
		}
		ret = io_epoll_ctl(req, nxt, force_nonblock);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	if (ctx->flags & IORING_SETUP_IOPOLL) {
		const bool in_async = io_wq_current_is_worker();

		if (req->result == -EAGAIN)
			return -EAGAIN;

		/* workqueue context doesn't hold uring_lock, grab it now */
		if (in_async)
			mutex_lock(&ctx->uring_lock);

		io_iopoll_req_issued(req);

		if (in_async)
			mutex_unlock(&ctx->uring_lock);
	}

	return 0;
}

static void io_wq_submit_work(struct io_wq_work **workptr)
{
	struct io_wq_work *work = *workptr;
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);
	struct io_kiocb *nxt = NULL;
	int ret = 0;

	/* if NO_CANCEL is set, we must still run the work */
	if ((work->flags & (IO_WQ_WORK_CANCEL|IO_WQ_WORK_NO_CANCEL)) ==
				IO_WQ_WORK_CANCEL) {
		ret = -ECANCELED;
	}

	if (!ret) {
		req->in_async = true;
		do {
			ret = io_issue_sqe(req, NULL, &nxt, false);
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

	/* drop submission reference */
	io_put_req(req);

	if (ret) {
		req_set_fail_links(req);
		io_cqring_add_event(req, ret);
		io_put_req(req);
	}

	/* if a dependent link is ready, pass it back */
	if (!ret && nxt)
		io_wq_assign_next(workptr, nxt);
}

static int io_req_needs_file(struct io_kiocb *req, int fd)
{
	if (!io_op_defs[req->opcode].needs_file)
		return 0;
	if ((fd == -1 || fd == AT_FDCWD) && io_op_defs[req->opcode].fd_non_neg)
		return 0;
	return 1;
}

static inline struct file *io_file_from_index(struct io_ring_ctx *ctx,
					      int index)
{
	struct fixed_file_table *table;

	table = &ctx->file_data->table[index >> IORING_FILE_TABLE_SHIFT];
	return table->files[index & IORING_FILE_TABLE_MASK];;
}

static int io_req_set_file(struct io_submit_state *state, struct io_kiocb *req,
			   const struct io_uring_sqe *sqe)
{
	struct io_ring_ctx *ctx = req->ctx;
	unsigned flags;
	int fd;

	flags = READ_ONCE(sqe->flags);
	fd = READ_ONCE(sqe->fd);

	if (!io_req_needs_file(req, fd))
		return 0;

	if (flags & IOSQE_FIXED_FILE) {
		if (unlikely(!ctx->file_data ||
		    (unsigned) fd >= ctx->nr_user_files))
			return -EBADF;
		fd = array_index_nospec(fd, ctx->nr_user_files);
		req->file = io_file_from_index(ctx, fd);
		if (!req->file)
			return -EBADF;
		req->flags |= REQ_F_FIXED_FILE;
		percpu_ref_get(&ctx->file_data->refs);
	} else {
		if (req->needs_fixed_file)
			return -EBADF;
		trace_io_uring_file_get(ctx, fd);
		req->file = io_file_get(state, fd);
		if (unlikely(!req->file))
			return -EBADF;
	}

	return 0;
}

static int io_grab_files(struct io_kiocb *req)
{
	int ret = -EBADF;
	struct io_ring_ctx *ctx = req->ctx;

	if (req->work.files)
		return 0;
	if (!ctx->ring_file)
		return -EBADF;

	rcu_read_lock();
	spin_lock_irq(&ctx->inflight_lock);
	/*
	 * We use the f_ops->flush() handler to ensure that we can flush
	 * out work accessing these files if the fd is closed. Check if
	 * the fd has changed since we started down this path, and disallow
	 * this operation if it has.
	 */
	if (fcheck(ctx->ring_fd) == ctx->ring_file) {
		list_add(&req->inflight_entry, &ctx->inflight_list);
		req->flags |= REQ_F_INFLIGHT;
		req->work.files = current->files;
		ret = 0;
	}
	spin_unlock_irq(&ctx->inflight_lock);
	rcu_read_unlock();

	return ret;
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
		if (refcount_inc_not_zero(&prev->refs)) {
			list_del_init(&req->link_list);
			prev->flags &= ~REQ_F_LINK_TIMEOUT;
		} else
			prev = NULL;
	}

	spin_unlock_irqrestore(&ctx->completion_lock, flags);

	if (prev) {
		req_set_fail_links(prev);
		io_async_find_and_cancel(ctx, req, prev->user_data, NULL,
						-ETIME);
		io_put_req(prev);
	} else {
		io_cqring_add_event(req, -ETIME);
		io_put_req(req);
	}
	return HRTIMER_NORESTART;
}

static void io_queue_linked_timeout(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;

	/*
	 * If the list is now empty, then our linked request finished before
	 * we got a chance to setup the timer
	 */
	spin_lock_irq(&ctx->completion_lock);
	if (!list_empty(&req->link_list)) {
		struct io_timeout_data *data = &req->io->timeout;

		data->timer.function = io_link_timeout_fn;
		hrtimer_start(&data->timer, timespec64_to_ktime(data->ts),
				data->mode);
	}
	spin_unlock_irq(&ctx->completion_lock);

	/* drop submission reference */
	io_put_req(req);
}

static struct io_kiocb *io_prep_linked_timeout(struct io_kiocb *req)
{
	struct io_kiocb *nxt;

	if (!(req->flags & REQ_F_LINK))
		return NULL;

	nxt = list_first_entry_or_null(&req->link_list, struct io_kiocb,
					link_list);
	if (!nxt || nxt->opcode != IORING_OP_LINK_TIMEOUT)
		return NULL;

	req->flags |= REQ_F_LINK_TIMEOUT;
	return nxt;
}

static void __io_queue_sqe(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_kiocb *linked_timeout;
	struct io_kiocb *nxt = NULL;
	int ret;

again:
	linked_timeout = io_prep_linked_timeout(req);

	ret = io_issue_sqe(req, sqe, &nxt, true);

	/*
	 * We async punt it if the file wasn't marked NOWAIT, or if the file
	 * doesn't support non-blocking read/write attempts
	 */
	if (ret == -EAGAIN && (!(req->flags & REQ_F_NOWAIT) ||
	    (req->flags & REQ_F_MUST_PUNT))) {
punt:
		if (io_op_defs[req->opcode].file_table) {
			ret = io_grab_files(req);
			if (ret)
				goto err;
		}

		/*
		 * Queued up for async execution, worker will release
		 * submit reference when the iocb is actually submitted.
		 */
		io_queue_async_work(req);
		goto done_req;
	}

err:
	/* drop submission reference */
	io_put_req(req);

	if (linked_timeout) {
		if (!ret)
			io_queue_linked_timeout(linked_timeout);
		else
			io_put_req(linked_timeout);
	}

	/* and drop final reference, if we failed */
	if (ret) {
		io_cqring_add_event(req, ret);
		req_set_fail_links(req);
		io_put_req(req);
	}
done_req:
	if (nxt) {
		req = nxt;
		nxt = NULL;

		if (req->flags & REQ_F_FORCE_ASYNC)
			goto punt;
		goto again;
	}
}

static void io_queue_sqe(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	int ret;

	ret = io_req_defer(req, sqe);
	if (ret) {
		if (ret != -EIOCBQUEUED) {
fail_req:
			io_cqring_add_event(req, ret);
			req_set_fail_links(req);
			io_double_put_req(req);
		}
	} else if (req->flags & REQ_F_FORCE_ASYNC) {
		ret = io_req_defer_prep(req, sqe);
		if (unlikely(ret < 0))
			goto fail_req;
		/*
		 * Never try inline submit of IOSQE_ASYNC is set, go straight
		 * to async execution.
		 */
		req->work.flags |= IO_WQ_WORK_CONCURRENT;
		io_queue_async_work(req);
	} else {
		__io_queue_sqe(req, sqe);
	}
}

static inline void io_queue_link_head(struct io_kiocb *req)
{
	if (unlikely(req->flags & REQ_F_FAIL_LINK)) {
		io_cqring_add_event(req, -ECANCELED);
		io_double_put_req(req);
	} else
		io_queue_sqe(req, NULL);
}

#define SQE_VALID_FLAGS	(IOSQE_FIXED_FILE|IOSQE_IO_DRAIN|IOSQE_IO_LINK|	\
				IOSQE_IO_HARDLINK | IOSQE_ASYNC)

static bool io_submit_sqe(struct io_kiocb *req, const struct io_uring_sqe *sqe,
			  struct io_submit_state *state, struct io_kiocb **link)
{
	const struct cred *old_creds = NULL;
	struct io_ring_ctx *ctx = req->ctx;
	unsigned int sqe_flags;
	int ret, id;

	sqe_flags = READ_ONCE(sqe->flags);

	/* enforce forwards compatibility on users */
	if (unlikely(sqe_flags & ~SQE_VALID_FLAGS)) {
		ret = -EINVAL;
		goto err_req;
	}

	id = READ_ONCE(sqe->personality);
	if (id) {
		const struct cred *personality_creds;

		personality_creds = idr_find(&ctx->personality_idr, id);
		if (unlikely(!personality_creds)) {
			ret = -EINVAL;
			goto err_req;
		}
		old_creds = override_creds(personality_creds);
	}

	/* same numerical values with corresponding REQ_F_*, safe to copy */
	req->flags |= sqe_flags & (IOSQE_IO_DRAIN|IOSQE_IO_HARDLINK|
					IOSQE_ASYNC);

	ret = io_req_set_file(state, req, sqe);
	if (unlikely(ret)) {
err_req:
		io_cqring_add_event(req, ret);
		io_double_put_req(req);
		if (old_creds)
			revert_creds(old_creds);
		return false;
	}

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
		if (sqe_flags & IOSQE_IO_DRAIN) {
			head->flags |= REQ_F_IO_DRAIN;
			ctx->drain_next = 1;
		}
		if (io_alloc_async_ctx(req)) {
			ret = -EAGAIN;
			goto err_req;
		}

		ret = io_req_defer_prep(req, sqe);
		if (ret) {
			/* fail even hard links since we don't submit */
			head->flags |= REQ_F_FAIL_LINK;
			goto err_req;
		}
		trace_io_uring_link(ctx, req, head);
		list_add_tail(&req->link_list, &head->link_list);

		/* last request of a link, enqueue the link */
		if (!(sqe_flags & (IOSQE_IO_LINK|IOSQE_IO_HARDLINK))) {
			io_queue_link_head(head);
			*link = NULL;
		}
	} else {
		if (unlikely(ctx->drain_next)) {
			req->flags |= REQ_F_IO_DRAIN;
			req->ctx->drain_next = 0;
		}
		if (sqe_flags & (IOSQE_IO_LINK|IOSQE_IO_HARDLINK)) {
			req->flags |= REQ_F_LINK;
			INIT_LIST_HEAD(&req->link_list);
			ret = io_req_defer_prep(req, sqe);
			if (ret)
				req->flags |= REQ_F_FAIL_LINK;
			*link = req;
		} else {
			io_queue_sqe(req, sqe);
		}
	}

	if (old_creds)
		revert_creds(old_creds);
	return true;
}

/*
 * Batched submission is done, ensure local IO is flushed out.
 */
static void io_submit_state_end(struct io_submit_state *state)
{
	blk_finish_plug(&state->plug);
	io_file_put(state);
	if (state->free_reqs)
		kmem_cache_free_bulk(req_cachep, state->free_reqs, state->reqs);
}

/*
 * Start submission side cache.
 */
static void io_submit_state_start(struct io_submit_state *state,
				  unsigned int max_ios)
{
	blk_start_plug(&state->plug);
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
static bool io_get_sqring(struct io_ring_ctx *ctx, struct io_kiocb *req,
			  const struct io_uring_sqe **sqe_ptr)
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
	if (likely(head < ctx->sq_entries)) {
		/*
		 * All io need record the previous position, if LINK vs DARIN,
		 * it can be used to mark the position of the first IO in the
		 * link list.
		 */
		req->sequence = ctx->cached_sq_head;
		*sqe_ptr = &ctx->sq_sqes[head];
		req->opcode = READ_ONCE((*sqe_ptr)->opcode);
		req->user_data = READ_ONCE((*sqe_ptr)->user_data);
		ctx->cached_sq_head++;
		return true;
	}

	/* drop invalid entries */
	ctx->cached_sq_head++;
	ctx->cached_sq_dropped++;
	WRITE_ONCE(ctx->rings->sq_dropped, ctx->cached_sq_dropped);
	return false;
}

static int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr,
			  struct file *ring_file, int ring_fd,
			  struct mm_struct **mm, bool async)
{
	struct io_submit_state state, *statep = NULL;
	struct io_kiocb *link = NULL;
	int i, submitted = 0;
	bool mm_fault = false;

	/* if we have a backlog and couldn't flush it all, return BUSY */
	if (test_bit(0, &ctx->sq_check_overflow)) {
		if (!list_empty(&ctx->cq_overflow_list) &&
		    !io_cqring_overflow_flush(ctx, false))
			return -EBUSY;
	}

	/* make sure SQ entry isn't read before tail */
	nr = min3(nr, ctx->sq_entries, io_sqring_entries(ctx));

	if (!percpu_ref_tryget_many(&ctx->refs, nr))
		return -EAGAIN;

	if (nr > IO_PLUG_THRESHOLD) {
		io_submit_state_start(&state, nr);
		statep = &state;
	}

	ctx->ring_fd = ring_fd;
	ctx->ring_file = ring_file;

	for (i = 0; i < nr; i++) {
		const struct io_uring_sqe *sqe;
		struct io_kiocb *req;
		int err;

		req = io_get_req(ctx, statep);
		if (unlikely(!req)) {
			if (!submitted)
				submitted = -EAGAIN;
			break;
		}
		if (!io_get_sqring(ctx, req, &sqe)) {
			__io_req_do_free(req);
			break;
		}

		/* will complete beyond this point, count as submitted */
		submitted++;

		if (unlikely(req->opcode >= IORING_OP_LAST)) {
			err = -EINVAL;
fail_req:
			io_cqring_add_event(req, err);
			io_double_put_req(req);
			break;
		}

		if (io_op_defs[req->opcode].needs_mm && !*mm) {
			mm_fault = mm_fault || !mmget_not_zero(ctx->sqo_mm);
			if (unlikely(mm_fault)) {
				err = -EFAULT;
				goto fail_req;
			}
			use_mm(ctx->sqo_mm);
			*mm = ctx->sqo_mm;
		}

		req->in_async = async;
		req->needs_fixed_file = async;
		trace_io_uring_submit_sqe(ctx, req->opcode, req->user_data,
						true, async);
		if (!io_submit_sqe(req, sqe, statep, &link))
			break;
	}

	if (unlikely(submitted != nr)) {
		int ref_used = (submitted == -EAGAIN) ? 0 : submitted;

		percpu_ref_put_many(&ctx->refs, nr - ref_used);
	}
	if (link)
		io_queue_link_head(link);
	if (statep)
		io_submit_state_end(&state);

	 /* Commit SQ ring head once we've consumed and submitted all SQEs */
	io_commit_sqring(ctx);

	return submitted;
}

static int io_sq_thread(void *data)
{
	struct io_ring_ctx *ctx = data;
	struct mm_struct *cur_mm = NULL;
	const struct cred *old_cred;
	mm_segment_t old_fs;
	DEFINE_WAIT(wait);
	unsigned inflight;
	unsigned long timeout;
	int ret;

	complete(&ctx->completions[1]);

	old_fs = get_fs();
	set_fs(USER_DS);
	old_cred = override_creds(ctx->creds);

	ret = timeout = inflight = 0;
	while (!kthread_should_park()) {
		unsigned int to_submit;

		if (inflight) {
			unsigned nr_events = 0;

			if (ctx->flags & IORING_SETUP_IOPOLL) {
				/*
				 * inflight is the count of the maximum possible
				 * entries we submitted, but it can be smaller
				 * if we dropped some of them. If we don't have
				 * poll entries available, then we know that we
				 * have nothing left to poll for. Reset the
				 * inflight count to zero in that case.
				 */
				mutex_lock(&ctx->uring_lock);
				if (!list_empty(&ctx->poll_list))
					__io_iopoll_check(ctx, &nr_events, 0);
				else
					inflight = 0;
				mutex_unlock(&ctx->uring_lock);
			} else {
				/*
				 * Normal IO, just pretend everything completed.
				 * We don't have to poll completions for that.
				 */
				nr_events = inflight;
			}

			inflight -= nr_events;
			if (!inflight)
				timeout = jiffies + ctx->sq_thread_idle;
		}

		to_submit = io_sqring_entries(ctx);

		/*
		 * If submit got -EBUSY, flag us as needing the application
		 * to enter the kernel to reap and flush events.
		 */
		if (!to_submit || ret == -EBUSY) {
			/*
			 * We're polling. If we're within the defined idle
			 * period, then let us spin without work before going
			 * to sleep. The exception is if we got EBUSY doing
			 * more IO, we should wait for the application to
			 * reap events and wake us up.
			 */
			if (inflight ||
			    (!time_after(jiffies, timeout) && ret != -EBUSY &&
			    !percpu_ref_is_dying(&ctx->refs))) {
				cond_resched();
				continue;
			}

			/*
			 * Drop cur_mm before scheduling, we can't hold it for
			 * long periods (or over schedule()). Do this before
			 * adding ourselves to the waitqueue, as the unuse/drop
			 * may sleep.
			 */
			if (cur_mm) {
				unuse_mm(cur_mm);
				mmput(cur_mm);
				cur_mm = NULL;
			}

			prepare_to_wait(&ctx->sqo_wait, &wait,
						TASK_INTERRUPTIBLE);

			/* Tell userspace we may need a wakeup call */
			ctx->rings->sq_flags |= IORING_SQ_NEED_WAKEUP;
			/* make sure to read SQ tail after writing flags */
			smp_mb();

			to_submit = io_sqring_entries(ctx);
			if (!to_submit || ret == -EBUSY) {
				if (kthread_should_park()) {
					finish_wait(&ctx->sqo_wait, &wait);
					break;
				}
				if (signal_pending(current))
					flush_signals(current);
				schedule();
				finish_wait(&ctx->sqo_wait, &wait);

				ctx->rings->sq_flags &= ~IORING_SQ_NEED_WAKEUP;
				continue;
			}
			finish_wait(&ctx->sqo_wait, &wait);

			ctx->rings->sq_flags &= ~IORING_SQ_NEED_WAKEUP;
		}

		mutex_lock(&ctx->uring_lock);
		ret = io_submit_sqes(ctx, to_submit, NULL, -1, &cur_mm, true);
		mutex_unlock(&ctx->uring_lock);
		if (ret > 0)
			inflight += ret;
	}

	set_fs(old_fs);
	if (cur_mm) {
		unuse_mm(cur_mm);
		mmput(cur_mm);
	}
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

	if (io_cqring_events(ctx, false) >= min_events)
		return 0;

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
		if (io_should_wake(&iowq, false))
			break;
		schedule();
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
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
	unsigned nr_tables, i;

	if (!data)
		return -ENXIO;

	percpu_ref_kill_and_confirm(&data->refs, io_file_ref_kill);
	flush_work(&data->ref_work);
	wait_for_completion(&data->done);
	io_ring_file_ref_flush(data);
	percpu_ref_exit(&data->refs);

	__io_sqe_files_unregister(ctx);
	nr_tables = DIV_ROUND_UP(ctx->nr_user_files, IORING_MAX_FILES_TABLE);
	for (i = 0; i < nr_tables; i++)
		kfree(data->table[i].files);
	kfree(data->table);
	kfree(data);
	ctx->file_data = NULL;
	ctx->nr_user_files = 0;
	return 0;
}

static void io_sq_thread_stop(struct io_ring_ctx *ctx)
{
	if (ctx->sqo_thread) {
		wait_for_completion(&ctx->completions[1]);
		/*
		 * The park is a bit of a work-around, without it we get
		 * warning spews on shutdown with SQPOLL set and affinity
		 * set to a single CPU.
		 */
		kthread_park(ctx->sqo_thread);
		kthread_stop(ctx->sqo_thread);
		ctx->sqo_thread = NULL;
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

	if (!capable(CAP_SYS_RESOURCE) && !capable(CAP_SYS_ADMIN)) {
		unsigned long inflight = ctx->user->unix_inflight + nr;

		if (inflight > task_rlimit(current, RLIMIT_NOFILE))
			return -EMFILE;
	}

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

static int io_sqe_alloc_file_tables(struct io_ring_ctx *ctx, unsigned nr_tables,
				    unsigned nr_files)
{
	int i;

	for (i = 0; i < nr_tables; i++) {
		struct fixed_file_table *table = &ctx->file_data->table[i];
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
		struct fixed_file_table *table = &ctx->file_data->table[i];
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
	struct llist_node llist;
	struct file *file;
	struct completion *done;
};

static void io_ring_file_ref_flush(struct fixed_file_data *data)
{
	struct io_file_put *pfile, *tmp;
	struct llist_node *node;

	while ((node = llist_del_all(&data->put_llist)) != NULL) {
		llist_for_each_entry_safe(pfile, tmp, node, llist) {
			io_ring_file_put(data->ctx, pfile->file);
			if (pfile->done)
				complete(pfile->done);
			else
				kfree(pfile);
		}
	}
}

static void io_ring_file_ref_switch(struct work_struct *work)
{
	struct fixed_file_data *data;

	data = container_of(work, struct fixed_file_data, ref_work);
	io_ring_file_ref_flush(data);
	percpu_ref_get(&data->refs);
	percpu_ref_switch_to_percpu(&data->refs);
}

static void io_file_data_ref_zero(struct percpu_ref *ref)
{
	struct fixed_file_data *data;

	data = container_of(ref, struct fixed_file_data, refs);

	/*
	 * We can't safely switch from inside this context, punt to wq. If
	 * the table ref is going away, the table is being unregistered.
	 * Don't queue up the async work for that case, the caller will
	 * handle it.
	 */
	if (!percpu_ref_is_dying(&data->refs))
		queue_work(system_wq, &data->ref_work);
}

static int io_sqe_files_register(struct io_ring_ctx *ctx, void __user *arg,
				 unsigned nr_args)
{
	__s32 __user *fds = (__s32 __user *) arg;
	unsigned nr_tables;
	struct file *file;
	int fd, ret = 0;
	unsigned i;

	if (ctx->file_data)
		return -EBUSY;
	if (!nr_args)
		return -EINVAL;
	if (nr_args > IORING_MAX_FIXED_FILES)
		return -EMFILE;

	ctx->file_data = kzalloc(sizeof(*ctx->file_data), GFP_KERNEL);
	if (!ctx->file_data)
		return -ENOMEM;
	ctx->file_data->ctx = ctx;
	init_completion(&ctx->file_data->done);

	nr_tables = DIV_ROUND_UP(nr_args, IORING_MAX_FILES_TABLE);
	ctx->file_data->table = kcalloc(nr_tables,
					sizeof(struct fixed_file_table),
					GFP_KERNEL);
	if (!ctx->file_data->table) {
		kfree(ctx->file_data);
		ctx->file_data = NULL;
		return -ENOMEM;
	}

	if (percpu_ref_init(&ctx->file_data->refs, io_file_data_ref_zero,
				PERCPU_REF_ALLOW_REINIT, GFP_KERNEL)) {
		kfree(ctx->file_data->table);
		kfree(ctx->file_data);
		ctx->file_data = NULL;
		return -ENOMEM;
	}
	ctx->file_data->put_llist.first = NULL;
	INIT_WORK(&ctx->file_data->ref_work, io_ring_file_ref_switch);

	if (io_sqe_alloc_file_tables(ctx, nr_tables, nr_args)) {
		percpu_ref_exit(&ctx->file_data->refs);
		kfree(ctx->file_data->table);
		kfree(ctx->file_data);
		ctx->file_data = NULL;
		return -ENOMEM;
	}

	for (i = 0; i < nr_args; i++, ctx->nr_user_files++) {
		struct fixed_file_table *table;
		unsigned index;

		ret = -EFAULT;
		if (copy_from_user(&fd, &fds[i], sizeof(fd)))
			break;
		/* allow sparse sets */
		if (fd == -1) {
			ret = 0;
			continue;
		}

		table = &ctx->file_data->table[i >> IORING_FILE_TABLE_SHIFT];
		index = i & IORING_FILE_TABLE_MASK;
		file = fget(fd);

		ret = -EBADF;
		if (!file)
			break;

		/*
		 * Don't allow io_uring instances to be registered. If UNIX
		 * isn't enabled, then this causes a reference cycle and this
		 * instance can never get freed. If UNIX is enabled we'll
		 * handle it just fine, but there's still no point in allowing
		 * a ring fd as it doesn't support regular read/write anyway.
		 */
		if (file->f_op == &io_uring_fops) {
			fput(file);
			break;
		}
		ret = 0;
		table->files[index] = file;
	}

	if (ret) {
		for (i = 0; i < ctx->nr_user_files; i++) {
			file = io_file_from_index(ctx, i);
			if (file)
				fput(file);
		}
		for (i = 0; i < nr_tables; i++)
			kfree(ctx->file_data->table[i].files);

		kfree(ctx->file_data->table);
		kfree(ctx->file_data);
		ctx->file_data = NULL;
		ctx->nr_user_files = 0;
		return ret;
	}

	ret = io_sqe_files_scm(ctx);
	if (ret)
		io_sqe_files_unregister(ctx);

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

static void io_atomic_switch(struct percpu_ref *ref)
{
	struct fixed_file_data *data;

	data = container_of(ref, struct fixed_file_data, refs);
	clear_bit(FFD_F_ATOMIC, &data->state);
}

static bool io_queue_file_removal(struct fixed_file_data *data,
				  struct file *file)
{
	struct io_file_put *pfile, pfile_stack;
	DECLARE_COMPLETION_ONSTACK(done);

	/*
	 * If we fail allocating the struct we need for doing async reomval
	 * of this file, just punt to sync and wait for it.
	 */
	pfile = kzalloc(sizeof(*pfile), GFP_KERNEL);
	if (!pfile) {
		pfile = &pfile_stack;
		pfile->done = &done;
	}

	pfile->file = file;
	llist_add(&pfile->llist, &data->put_llist);

	if (pfile == &pfile_stack) {
		if (!test_and_set_bit(FFD_F_ATOMIC, &data->state)) {
			percpu_ref_put(&data->refs);
			percpu_ref_switch_to_atomic(&data->refs,
							io_atomic_switch);
		}
		wait_for_completion(&done);
		flush_work(&data->ref_work);
		return false;
	}

	return true;
}

static int __io_sqe_files_update(struct io_ring_ctx *ctx,
				 struct io_uring_files_update *up,
				 unsigned nr_args)
{
	struct fixed_file_data *data = ctx->file_data;
	bool ref_switch = false;
	struct file *file;
	__s32 __user *fds;
	int fd, i, err;
	__u32 done;

	if (check_add_overflow(up->offset, nr_args, &done))
		return -EOVERFLOW;
	if (done > ctx->nr_user_files)
		return -EINVAL;

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
			file = io_file_from_index(ctx, index);
			table->files[index] = NULL;
			if (io_queue_file_removal(data, file))
				ref_switch = true;
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
			if (err)
				break;
		}
		nr_args--;
		done++;
		up->offset++;
	}

	if (ref_switch && !test_and_set_bit(FFD_F_ATOMIC, &data->state)) {
		percpu_ref_put(&data->refs);
		percpu_ref_switch_to_atomic(&data->refs, io_atomic_switch);
	}

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

static void io_put_work(struct io_wq_work *work)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);

	io_put_req(req);
}

static void io_get_work(struct io_wq_work *work)
{
	struct io_kiocb *req = container_of(work, struct io_kiocb, work);

	refcount_inc(&req->refs);
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
	data.get_work = io_get_work;
	data.put_work = io_put_work;

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

static int io_sq_offload_start(struct io_ring_ctx *ctx,
			       struct io_uring_params *p)
{
	int ret;

	init_waitqueue_head(&ctx->sqo_wait);
	mmgrab(current->mm);
	ctx->sqo_mm = current->mm;

	if (ctx->flags & IORING_SETUP_SQPOLL) {
		ret = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto err;

		ctx->sq_thread_idle = msecs_to_jiffies(p->sq_thread_idle);
		if (!ctx->sq_thread_idle)
			ctx->sq_thread_idle = HZ;

		if (p->flags & IORING_SETUP_SQ_AFF) {
			int cpu = p->sq_thread_cpu;

			ret = -EINVAL;
			if (cpu >= nr_cpu_ids)
				goto err;
			if (!cpu_online(cpu))
				goto err;

			ctx->sqo_thread = kthread_create_on_cpu(io_sq_thread,
							ctx, cpu,
							"io_uring-sq");
		} else {
			ctx->sqo_thread = kthread_create(io_sq_thread, ctx,
							"io_uring-sq");
		}
		if (IS_ERR(ctx->sqo_thread)) {
			ret = PTR_ERR(ctx->sqo_thread);
			ctx->sqo_thread = NULL;
			goto err;
		}
		wake_up_process(ctx->sqo_thread);
	} else if (p->flags & IORING_SETUP_SQ_AFF) {
		/* Can't have SQ_AFF without SQPOLL */
		ret = -EINVAL;
		goto err;
	}

	ret = io_init_wq_offload(ctx, p);
	if (ret)
		goto err;

	return 0;
err:
	io_finish_async(ctx);
	mmdrop(ctx->sqo_mm);
	ctx->sqo_mm = NULL;
	return ret;
}

static void io_unaccount_mem(struct user_struct *user, unsigned long nr_pages)
{
	atomic_long_sub(nr_pages, &user->locked_vm);
}

static int io_account_mem(struct user_struct *user, unsigned long nr_pages)
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

	sq_array_size = array_size(sizeof(u32), sq_entries);
	if (sq_array_size == SIZE_MAX)
		return SIZE_MAX;

	if (check_add_overflow(off, sq_array_size, &off))
		return SIZE_MAX;

	if (sq_offset)
		*sq_offset = off;

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

		if (ctx->account_mem)
			io_unaccount_mem(ctx->user, imu->nr_bvecs);
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

static int io_sqe_buffer_register(struct io_ring_ctx *ctx, void __user *arg,
				  unsigned nr_args)
{
	struct vm_area_struct **vmas = NULL;
	struct page **pages = NULL;
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

		if (ctx->account_mem) {
			ret = io_account_mem(ctx->user, nr_pages);
			if (ret)
				goto err;
		}

		ret = 0;
		if (!pages || nr_pages > got_pages) {
			kfree(vmas);
			kfree(pages);
			pages = kvmalloc_array(nr_pages, sizeof(struct page *),
						GFP_KERNEL);
			vmas = kvmalloc_array(nr_pages,
					sizeof(struct vm_area_struct *),
					GFP_KERNEL);
			if (!pages || !vmas) {
				ret = -ENOMEM;
				if (ctx->account_mem)
					io_unaccount_mem(ctx->user, nr_pages);
				goto err;
			}
			got_pages = nr_pages;
		}

		imu->bvec = kvmalloc_array(nr_pages, sizeof(struct bio_vec),
						GFP_KERNEL);
		ret = -ENOMEM;
		if (!imu->bvec) {
			if (ctx->account_mem)
				io_unaccount_mem(ctx->user, nr_pages);
			goto err;
		}

		ret = 0;
		down_read(&current->mm->mmap_sem);
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
		up_read(&current->mm->mmap_sem);
		if (ret) {
			/*
			 * if we did partial map, or found file backed vmas,
			 * release any pages we did get
			 */
			if (pret > 0)
				unpin_user_pages(pages, pret);
			if (ctx->account_mem)
				io_unaccount_mem(ctx->user, nr_pages);
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

static void io_ring_ctx_free(struct io_ring_ctx *ctx)
{
	io_finish_async(ctx);
	if (ctx->sqo_mm)
		mmdrop(ctx->sqo_mm);

	io_iopoll_reap_events(ctx);
	io_sqe_buffer_unregister(ctx);
	io_sqe_files_unregister(ctx);
	io_eventfd_unregister(ctx);

#if defined(CONFIG_UNIX)
	if (ctx->ring_sock) {
		ctx->ring_sock->file = NULL; /* so that iput() is called */
		sock_release(ctx->ring_sock);
	}
#endif

	io_mem_free(ctx->rings);
	io_mem_free(ctx->sq_sqes);

	percpu_ref_exit(&ctx->refs);
	if (ctx->account_mem)
		io_unaccount_mem(ctx->user,
				ring_pages(ctx->sq_entries, ctx->cq_entries));
	free_uid(ctx->user);
	put_cred(ctx->creds);
	kfree(ctx->completions);
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
	if (READ_ONCE(ctx->rings->sq.tail) - ctx->cached_sq_head !=
	    ctx->rings->sq_ring_entries)
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
	const struct cred *cred;

	cred = idr_remove(&ctx->personality_idr, id);
	if (cred)
		put_cred(cred);
	return 0;
}

static void io_ring_ctx_wait_and_kill(struct io_ring_ctx *ctx)
{
	mutex_lock(&ctx->uring_lock);
	percpu_ref_kill(&ctx->refs);
	mutex_unlock(&ctx->uring_lock);

	/*
	 * Wait for sq thread to idle, if we have one. It won't spin on new
	 * work after we've killed the ctx ref above. This is important to do
	 * before we cancel existing commands, as the thread could otherwise
	 * be queueing new work post that. If that's work we need to cancel,
	 * it could cause shutdown to hang.
	 */
	while (ctx->sqo_thread && !wq_has_sleeper(&ctx->sqo_wait))
		cpu_relax();

	io_kill_timeouts(ctx);
	io_poll_remove_all(ctx);

	if (ctx->io_wq)
		io_wq_cancel_all(ctx->io_wq);

	io_iopoll_reap_events(ctx);
	/* if we failed setting up the ctx, we might not have any rings */
	if (ctx->rings)
		io_cqring_overflow_flush(ctx, true);
	idr_for_each(&ctx->personality_idr, io_remove_personalities, ctx);
	wait_for_completion(&ctx->completions[0]);
	io_ring_ctx_free(ctx);
}

static int io_uring_release(struct inode *inode, struct file *file)
{
	struct io_ring_ctx *ctx = file->private_data;

	file->private_data = NULL;
	io_ring_ctx_wait_and_kill(ctx);
	return 0;
}

static void io_uring_cancel_files(struct io_ring_ctx *ctx,
				  struct files_struct *files)
{
	struct io_kiocb *req;
	DEFINE_WAIT(wait);

	while (!list_empty_careful(&ctx->inflight_list)) {
		struct io_kiocb *cancel_req = NULL;

		spin_lock_irq(&ctx->inflight_lock);
		list_for_each_entry(req, &ctx->inflight_list, inflight_entry) {
			if (req->work.files != files)
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

		if (cancel_req->flags & REQ_F_OVERFLOW) {
			spin_lock_irq(&ctx->completion_lock);
			list_del(&cancel_req->list);
			cancel_req->flags &= ~REQ_F_OVERFLOW;
			if (list_empty(&ctx->cq_overflow_list)) {
				clear_bit(0, &ctx->sq_check_overflow);
				clear_bit(0, &ctx->cq_check_overflow);
			}
			spin_unlock_irq(&ctx->completion_lock);

			WRITE_ONCE(ctx->rings->cq_overflow,
				atomic_inc_return(&ctx->cached_cq_overflow));

			/*
			 * Put inflight ref and overflow ref. If that's
			 * all we had, then we're done with this request.
			 */
			if (refcount_sub_and_test(2, &cancel_req->refs)) {
				io_put_req(cancel_req);
				continue;
			}
		}

		io_wq_cancel_work(ctx->io_wq, &cancel_req->work);
		io_put_req(cancel_req);
		schedule();
	}
	finish_wait(&ctx->inflight_wait, &wait);
}

static int io_uring_flush(struct file *file, void *data)
{
	struct io_ring_ctx *ctx = file->private_data;

	io_uring_cancel_files(ctx, data);

	/*
	 * If the task is going away, cancel work it may have pending
	 */
	if (fatal_signal_pending(current) || (current->flags & PF_EXITING))
		io_wq_cancel_pid(ctx->io_wq, task_pid_vnr(current));

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

SYSCALL_DEFINE6(io_uring_enter, unsigned int, fd, u32, to_submit,
		u32, min_complete, u32, flags, const sigset_t __user *, sig,
		size_t, sigsz)
{
	struct io_ring_ctx *ctx;
	long ret = -EBADF;
	int submitted = 0;
	struct fd f;

	if (flags & ~(IORING_ENTER_GETEVENTS | IORING_ENTER_SQ_WAKEUP))
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

	/*
	 * For SQ polling, the thread will do all submissions and completions.
	 * Just return the requested submit count, and wake the thread if
	 * we were asked to.
	 */
	ret = 0;
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		if (!list_empty_careful(&ctx->cq_overflow_list))
			io_cqring_overflow_flush(ctx, false);
		if (flags & IORING_ENTER_SQ_WAKEUP)
			wake_up(&ctx->sqo_wait);
		submitted = to_submit;
	} else if (to_submit) {
		struct mm_struct *cur_mm;

		mutex_lock(&ctx->uring_lock);
		/* already have mm, so io_submit_sqes() won't try to grab it */
		cur_mm = ctx->sqo_mm;
		submitted = io_submit_sqes(ctx, to_submit, f.file, fd,
					   &cur_mm, false);
		mutex_unlock(&ctx->uring_lock);

		if (submitted != to_submit)
			goto out;
	}
	if (flags & IORING_ENTER_GETEVENTS) {
		unsigned nr_events = 0;

		min_complete = min(min_complete, ctx->cq_entries);

		if (ctx->flags & IORING_SETUP_IOPOLL) {
			ret = io_iopoll_check(ctx, &nr_events, min_complete);
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

static int io_uring_show_cred(int id, void *p, void *data)
{
	const struct cred *cred = p;
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
	int i;

	mutex_lock(&ctx->uring_lock);
	seq_printf(m, "UserFiles:\t%u\n", ctx->nr_user_files);
	for (i = 0; i < ctx->nr_user_files; i++) {
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
	for (i = 0; i < ctx->nr_user_bufs; i++) {
		struct io_mapped_ubuf *buf = &ctx->user_bufs[i];

		seq_printf(m, "%5u: 0x%llx/%u\n", i, buf->ubuf,
						(unsigned int) buf->len);
	}
	if (!idr_is_empty(&ctx->personality_idr)) {
		seq_printf(m, "Personalities:\n");
		idr_for_each(&ctx->personality_idr, io_uring_show_cred, m);
	}
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
	.show_fdinfo	= io_uring_show_fdinfo,
};

static int io_allocate_scq_urings(struct io_ring_ctx *ctx,
				  struct io_uring_params *p)
{
	struct io_rings *rings;
	size_t size, sq_array_offset;

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
	ctx->sq_entries = rings->sq_ring_entries;
	ctx->cq_entries = rings->cq_ring_entries;

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

/*
 * Allocate an anonymous fd, this is what constitutes the application
 * visible backing of an io_uring instance. The application mmaps this
 * fd to gain access to the SQ/CQ ring details. If UNIX sockets are enabled,
 * we have to tie this fd to a socket for file garbage collection purposes.
 */
static int io_uring_get_fd(struct io_ring_ctx *ctx)
{
	struct file *file;
	int ret;

#if defined(CONFIG_UNIX)
	ret = sock_create_kern(&init_net, PF_UNIX, SOCK_RAW, IPPROTO_IP,
				&ctx->ring_sock);
	if (ret)
		return ret;
#endif

	ret = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (ret < 0)
		goto err;

	file = anon_inode_getfile("[io_uring]", &io_uring_fops, ctx,
					O_RDWR | O_CLOEXEC);
	if (IS_ERR(file)) {
		put_unused_fd(ret);
		ret = PTR_ERR(file);
		goto err;
	}

#if defined(CONFIG_UNIX)
	ctx->ring_sock->file = file;
#endif
	fd_install(ret, file);
	return ret;
err:
#if defined(CONFIG_UNIX)
	sock_release(ctx->ring_sock);
	ctx->ring_sock = NULL;
#endif
	return ret;
}

static int io_uring_create(unsigned entries, struct io_uring_params *p)
{
	struct user_struct *user = NULL;
	struct io_ring_ctx *ctx;
	bool account_mem;
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
		if (p->cq_entries < p->sq_entries)
			return -EINVAL;
		if (p->cq_entries > IORING_MAX_CQ_ENTRIES) {
			if (!(p->flags & IORING_SETUP_CLAMP))
				return -EINVAL;
			p->cq_entries = IORING_MAX_CQ_ENTRIES;
		}
		p->cq_entries = roundup_pow_of_two(p->cq_entries);
	} else {
		p->cq_entries = 2 * p->sq_entries;
	}

	user = get_uid(current_user());
	account_mem = !capable(CAP_IPC_LOCK);

	if (account_mem) {
		ret = io_account_mem(user,
				ring_pages(p->sq_entries, p->cq_entries));
		if (ret) {
			free_uid(user);
			return ret;
		}
	}

	ctx = io_ring_ctx_alloc(p);
	if (!ctx) {
		if (account_mem)
			io_unaccount_mem(user, ring_pages(p->sq_entries,
								p->cq_entries));
		free_uid(user);
		return -ENOMEM;
	}
	ctx->compat = in_compat_syscall();
	ctx->account_mem = account_mem;
	ctx->user = user;
	ctx->creds = get_current_cred();

	ret = io_allocate_scq_urings(ctx, p);
	if (ret)
		goto err;

	ret = io_sq_offload_start(ctx, p);
	if (ret)
		goto err;

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

	/*
	 * Install ring fd as the very last thing, so we don't risk someone
	 * having closed it before we finish setup
	 */
	ret = io_uring_get_fd(ctx);
	if (ret < 0)
		goto err;

	p->features = IORING_FEAT_SINGLE_MMAP | IORING_FEAT_NODROP |
			IORING_FEAT_SUBMIT_STABLE | IORING_FEAT_RW_CUR_POS |
			IORING_FEAT_CUR_PERSONALITY;
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
	long ret;
	int i;

	if (copy_from_user(&p, params, sizeof(p)))
		return -EFAULT;
	for (i = 0; i < ARRAY_SIZE(p.resv); i++) {
		if (p.resv[i])
			return -EINVAL;
	}

	if (p.flags & ~(IORING_SETUP_IOPOLL | IORING_SETUP_SQPOLL |
			IORING_SETUP_SQ_AFF | IORING_SETUP_CQSIZE |
			IORING_SETUP_CLAMP | IORING_SETUP_ATTACH_WQ))
		return -EINVAL;

	ret = io_uring_create(entries, &p);
	if (ret < 0)
		return ret;

	if (copy_to_user(params, &p, sizeof(p)))
		return -EFAULT;

	return ret;
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
	const struct cred *creds = get_current_cred();
	int id;

	id = idr_alloc_cyclic(&ctx->personality_idr, (void *) creds, 1,
				USHRT_MAX, GFP_KERNEL);
	if (id < 0)
		put_cred(creds);
	return id;
}

static int io_unregister_personality(struct io_ring_ctx *ctx, unsigned id)
{
	const struct cred *old_creds;

	old_creds = idr_remove(&ctx->personality_idr, id);
	if (old_creds) {
		put_cred(old_creds);
		return 0;
	}

	return -EINVAL;
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
		ret = wait_for_completion_interruptible(&ctx->completions[0]);
		mutex_lock(&ctx->uring_lock);
		if (ret) {
			percpu_ref_resurrect(&ctx->refs);
			ret = -EINTR;
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
	default:
		ret = -EINVAL;
		break;
	}

	if (io_register_op_must_quiesce(opcode)) {
		/* bring the ctx back to life */
		percpu_ref_reinit(&ctx->refs);
out:
		reinit_completion(&ctx->completions[0]);
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
	BUILD_BUG_SQE_ELEM(24, __u32,  len);
	BUILD_BUG_SQE_ELEM(28,     __kernel_rwf_t, rw_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */   int, rw_flags);
	BUILD_BUG_SQE_ELEM(28, /* compat */ __u32, rw_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  fsync_flags);
	BUILD_BUG_SQE_ELEM(28, __u16,  poll_events);
	BUILD_BUG_SQE_ELEM(28, __u32,  sync_range_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  msg_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  timeout_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  accept_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  cancel_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  open_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  statx_flags);
	BUILD_BUG_SQE_ELEM(28, __u32,  fadvise_advice);
	BUILD_BUG_SQE_ELEM(32, __u64,  user_data);
	BUILD_BUG_SQE_ELEM(40, __u16,  buf_index);
	BUILD_BUG_SQE_ELEM(42, __u16,  personality);

	BUILD_BUG_ON(ARRAY_SIZE(io_op_defs) != IORING_OP_LAST);
	req_cachep = KMEM_CACHE(io_kiocb, SLAB_HWCACHE_ALIGN | SLAB_PANIC);
	return 0;
};
__initcall(io_uring_init);
