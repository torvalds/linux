/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM io_uring

#if !defined(_TRACE_IO_URING_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IO_URING_H

#include <linux/tracepoint.h>
#include <uapi/linux/io_uring.h>
#include <linux/io_uring_types.h>
#include <linux/io_uring.h>

struct io_wq_work;

/**
 * io_uring_create - called after a new io_uring context was prepared
 *
 * @fd:		corresponding file descriptor
 * @ctx:	pointer to a ring context structure
 * @sq_entries:	actual SQ size
 * @cq_entries:	actual CQ size
 * @flags:	SQ ring flags, provided to io_uring_setup(2)
 *
 * Allows to trace io_uring creation and provide pointer to a context, that can
 * be used later to find correlated events.
 */
TRACE_EVENT(io_uring_create,

	TP_PROTO(int fd, void *ctx, u32 sq_entries, u32 cq_entries, u32 flags),

	TP_ARGS(fd, ctx, sq_entries, cq_entries, flags),

	TP_STRUCT__entry (
		__field(  int,		fd		)
		__field(  void *,	ctx		)
		__field(  u32,		sq_entries	)
		__field(  u32,		cq_entries	)
		__field(  u32,		flags		)
	),

	TP_fast_assign(
		__entry->fd		= fd;
		__entry->ctx		= ctx;
		__entry->sq_entries	= sq_entries;
		__entry->cq_entries	= cq_entries;
		__entry->flags		= flags;
	),

	TP_printk("ring %p, fd %d sq size %d, cq size %d, flags 0x%x",
			  __entry->ctx, __entry->fd, __entry->sq_entries,
			  __entry->cq_entries, __entry->flags)
);

/**
 * io_uring_register - called after a buffer/file/eventfd was successfully
 * 					   registered for a ring
 *
 * @ctx:		pointer to a ring context structure
 * @opcode:		describes which operation to perform
 * @nr_user_files:	number of registered files
 * @nr_user_bufs:	number of registered buffers
 * @ret:		return code
 *
 * Allows to trace fixed files/buffers, that could be registered to
 * avoid an overhead of getting references to them for every operation. This
 * event, together with io_uring_file_get, can provide a full picture of how
 * much overhead one can reduce via fixing.
 */
TRACE_EVENT(io_uring_register,

	TP_PROTO(void *ctx, unsigned opcode, unsigned nr_files,
			 unsigned nr_bufs, long ret),

	TP_ARGS(ctx, opcode, nr_files, nr_bufs, ret),

	TP_STRUCT__entry (
		__field(  void *,	ctx	)
		__field(  unsigned,	opcode	)
		__field(  unsigned,	nr_files)
		__field(  unsigned,	nr_bufs	)
		__field(  long,		ret	)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->opcode		= opcode;
		__entry->nr_files	= nr_files;
		__entry->nr_bufs	= nr_bufs;
		__entry->ret		= ret;
	),

	TP_printk("ring %p, opcode %d, nr_user_files %d, nr_user_bufs %d, "
			  "ret %ld",
			  __entry->ctx, __entry->opcode, __entry->nr_files,
			  __entry->nr_bufs, __entry->ret)
);

/**
 * io_uring_file_get - called before getting references to an SQE file
 *
 * @req:	pointer to a submitted request
 * @fd:		SQE file descriptor
 *
 * Allows to trace out how often an SQE file reference is obtained, which can
 * help figuring out if it makes sense to use fixed files, or check that fixed
 * files are used correctly.
 */
TRACE_EVENT(io_uring_file_get,

	TP_PROTO(struct io_kiocb *req, int fd),

	TP_ARGS(req, fd),

	TP_STRUCT__entry (
		__field(  void *,	ctx		)
		__field(  void *,	req		)
		__field(  u64,		user_data	)
		__field(  int,		fd		)
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= req->cqe.user_data;
		__entry->fd		= fd;
	),

	TP_printk("ring %p, req %p, user_data 0x%llx, fd %d",
		__entry->ctx, __entry->req, __entry->user_data, __entry->fd)
);

/**
 * io_uring_queue_async_work - called before submitting a new async work
 *
 * @req:	pointer to a submitted request
 * @rw:		type of workqueue, hashed or normal
 *
 * Allows to trace asynchronous work submission.
 */
TRACE_EVENT(io_uring_queue_async_work,

	TP_PROTO(struct io_kiocb *req, int rw),

	TP_ARGS(req, rw),

	TP_STRUCT__entry (
		__field(  void *,			ctx		)
		__field(  void *,			req		)
		__field(  u64,				user_data	)
		__field(  u8,				opcode		)
		__field(  unsigned int,			flags		)
		__field(  struct io_wq_work *,		work		)
		__field(  int,				rw		)

		__string( op_str, io_uring_get_opcode(req->opcode)	)
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= req->cqe.user_data;
		__entry->flags		= req->flags;
		__entry->opcode		= req->opcode;
		__entry->work		= &req->work;
		__entry->rw		= rw;

		__assign_str(op_str, io_uring_get_opcode(req->opcode));
	),

	TP_printk("ring %p, request %p, user_data 0x%llx, opcode %s, flags 0x%x, %s queue, work %p",
		__entry->ctx, __entry->req, __entry->user_data,
		__get_str(op_str),
		__entry->flags, __entry->rw ? "hashed" : "normal", __entry->work)
);

/**
 * io_uring_defer - called when an io_uring request is deferred
 *
 * @req:	pointer to a deferred request
 *
 * Allows to track deferred requests, to get an insight about what requests are
 * not started immediately.
 */
TRACE_EVENT(io_uring_defer,

	TP_PROTO(struct io_kiocb *req),

	TP_ARGS(req),

	TP_STRUCT__entry (
		__field(  void *,		ctx	)
		__field(  void *,		req	)
		__field(  unsigned long long,	data	)
		__field(  u8,			opcode	)

		__string( op_str, io_uring_get_opcode(req->opcode) )
	),

	TP_fast_assign(
		__entry->ctx	= req->ctx;
		__entry->req	= req;
		__entry->data	= req->cqe.user_data;
		__entry->opcode	= req->opcode;

		__assign_str(op_str, io_uring_get_opcode(req->opcode));
	),

	TP_printk("ring %p, request %p, user_data 0x%llx, opcode %s",
		__entry->ctx, __entry->req, __entry->data,
		__get_str(op_str))
);

/**
 * io_uring_link - called before the io_uring request added into link_list of
 * 		   another request
 *
 * @req:		pointer to a linked request
 * @target_req:		pointer to a previous request, that would contain @req
 *
 * Allows to track linked requests, to understand dependencies between requests
 * and how does it influence their execution flow.
 */
TRACE_EVENT(io_uring_link,

	TP_PROTO(struct io_kiocb *req, struct io_kiocb *target_req),

	TP_ARGS(req, target_req),

	TP_STRUCT__entry (
		__field(  void *,	ctx		)
		__field(  void *,	req		)
		__field(  void *,	target_req	)
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->target_req	= target_req;
	),

	TP_printk("ring %p, request %p linked after %p",
			  __entry->ctx, __entry->req, __entry->target_req)
);

/**
 * io_uring_cqring_wait - called before start waiting for an available CQE
 *
 * @ctx:		pointer to a ring context structure
 * @min_events:	minimal number of events to wait for
 *
 * Allows to track waiting for CQE, so that we can e.g. troubleshoot
 * situations, when an application wants to wait for an event, that never
 * comes.
 */
TRACE_EVENT(io_uring_cqring_wait,

	TP_PROTO(void *ctx, int min_events),

	TP_ARGS(ctx, min_events),

	TP_STRUCT__entry (
		__field(  void *,	ctx		)
		__field(  int,		min_events	)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->min_events	= min_events;
	),

	TP_printk("ring %p, min_events %d", __entry->ctx, __entry->min_events)
);

/**
 * io_uring_fail_link - called before failing a linked request
 *
 * @req:	request, which links were cancelled
 * @link:	cancelled link
 *
 * Allows to track linked requests cancellation, to see not only that some work
 * was cancelled, but also which request was the reason.
 */
TRACE_EVENT(io_uring_fail_link,

	TP_PROTO(struct io_kiocb *req, struct io_kiocb *link),

	TP_ARGS(req, link),

	TP_STRUCT__entry (
		__field(  void *,		ctx		)
		__field(  void *,		req		)
		__field(  unsigned long long,	user_data	)
		__field(  u8,			opcode		)
		__field(  void *,		link		)

		__string( op_str, io_uring_get_opcode(req->opcode) )
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= req->cqe.user_data;
		__entry->opcode		= req->opcode;
		__entry->link		= link;

		__assign_str(op_str, io_uring_get_opcode(req->opcode));
	),

	TP_printk("ring %p, request %p, user_data 0x%llx, opcode %s, link %p",
		__entry->ctx, __entry->req, __entry->user_data,
		__get_str(op_str), __entry->link)
);

/**
 * io_uring_complete - called when completing an SQE
 *
 * @ctx:		pointer to a ring context structure
 * @req:		pointer to a submitted request
 * @user_data:		user data associated with the request
 * @res:		result of the request
 * @cflags:		completion flags
 * @extra1:		extra 64-bit data for CQE32
 * @extra2:		extra 64-bit data for CQE32
 *
 */
TRACE_EVENT(io_uring_complete,

	TP_PROTO(void *ctx, void *req, u64 user_data, int res, unsigned cflags,
		 u64 extra1, u64 extra2),

	TP_ARGS(ctx, req, user_data, res, cflags, extra1, extra2),

	TP_STRUCT__entry (
		__field(  void *,	ctx		)
		__field(  void *,	req		)
		__field(  u64,		user_data	)
		__field(  int,		res		)
		__field(  unsigned,	cflags		)
		__field(  u64,		extra1		)
		__field(  u64,		extra2		)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->req		= req;
		__entry->user_data	= user_data;
		__entry->res		= res;
		__entry->cflags		= cflags;
		__entry->extra1		= extra1;
		__entry->extra2		= extra2;
	),

	TP_printk("ring %p, req %p, user_data 0x%llx, result %d, cflags 0x%x "
		  "extra1 %llu extra2 %llu ",
		__entry->ctx, __entry->req,
		__entry->user_data,
		__entry->res, __entry->cflags,
		(unsigned long long) __entry->extra1,
		(unsigned long long) __entry->extra2)
);

/**
 * io_uring_submit_sqe - called before submitting one SQE
 *
 * @req:		pointer to a submitted request
 * @force_nonblock:	whether a context blocking or not
 *
 * Allows to track SQE submitting, to understand what was the source of it, SQ
 * thread or io_uring_enter call.
 */
TRACE_EVENT(io_uring_submit_sqe,

	TP_PROTO(struct io_kiocb *req, bool force_nonblock),

	TP_ARGS(req, force_nonblock),

	TP_STRUCT__entry (
		__field(  void *,		ctx		)
		__field(  void *,		req		)
		__field(  unsigned long long,	user_data	)
		__field(  u8,			opcode		)
		__field(  u32,			flags		)
		__field(  bool,			force_nonblock	)
		__field(  bool,			sq_thread	)

		__string( op_str, io_uring_get_opcode(req->opcode) )
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= req->cqe.user_data;
		__entry->opcode		= req->opcode;
		__entry->flags		= req->flags;
		__entry->force_nonblock	= force_nonblock;
		__entry->sq_thread	= req->ctx->flags & IORING_SETUP_SQPOLL;

		__assign_str(op_str, io_uring_get_opcode(req->opcode));
	),

	TP_printk("ring %p, req %p, user_data 0x%llx, opcode %s, flags 0x%x, "
		  "non block %d, sq_thread %d", __entry->ctx, __entry->req,
		  __entry->user_data, __get_str(op_str),
		  __entry->flags, __entry->force_nonblock, __entry->sq_thread)
);

/*
 * io_uring_poll_arm - called after arming a poll wait if successful
 *
 * @req:		pointer to the armed request
 * @mask:		request poll events mask
 * @events:		registered events of interest
 *
 * Allows to track which fds are waiting for and what are the events of
 * interest.
 */
TRACE_EVENT(io_uring_poll_arm,

	TP_PROTO(struct io_kiocb *req, int mask, int events),

	TP_ARGS(req, mask, events),

	TP_STRUCT__entry (
		__field(  void *,		ctx		)
		__field(  void *,		req		)
		__field(  unsigned long long,	user_data	)
		__field(  u8,			opcode		)
		__field(  int,			mask		)
		__field(  int,			events		)

		__string( op_str, io_uring_get_opcode(req->opcode) )
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= req->cqe.user_data;
		__entry->opcode		= req->opcode;
		__entry->mask		= mask;
		__entry->events		= events;

		__assign_str(op_str, io_uring_get_opcode(req->opcode));
	),

	TP_printk("ring %p, req %p, user_data 0x%llx, opcode %s, mask 0x%x, events 0x%x",
		  __entry->ctx, __entry->req, __entry->user_data,
		  __get_str(op_str),
		  __entry->mask, __entry->events)
);

/*
 * io_uring_task_add - called after adding a task
 *
 * @req:		pointer to request
 * @mask:		request poll events mask
 *
 */
TRACE_EVENT(io_uring_task_add,

	TP_PROTO(struct io_kiocb *req, int mask),

	TP_ARGS(req, mask),

	TP_STRUCT__entry (
		__field(  void *,		ctx		)
		__field(  void *,		req		)
		__field(  unsigned long long,	user_data	)
		__field(  u8,			opcode		)
		__field(  int,			mask		)

		__string( op_str, io_uring_get_opcode(req->opcode) )
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= req->cqe.user_data;
		__entry->opcode		= req->opcode;
		__entry->mask		= mask;

		__assign_str(op_str, io_uring_get_opcode(req->opcode));
	),

	TP_printk("ring %p, req %p, user_data 0x%llx, opcode %s, mask %x",
		__entry->ctx, __entry->req, __entry->user_data,
		__get_str(op_str),
		__entry->mask)
);

/*
 * io_uring_req_failed - called when an sqe is errored dring submission
 *
 * @sqe:		pointer to the io_uring_sqe that failed
 * @req:		pointer to request
 * @error:		error it failed with
 *
 * Allows easier diagnosing of malformed requests in production systems.
 */
TRACE_EVENT(io_uring_req_failed,

	TP_PROTO(const struct io_uring_sqe *sqe, struct io_kiocb *req, int error),

	TP_ARGS(sqe, req, error),

	TP_STRUCT__entry (
		__field(  void *,		ctx		)
		__field(  void *,		req		)
		__field(  unsigned long long,	user_data	)
		__field(  u8,			opcode		)
		__field(  u8,			flags		)
		__field(  u8,			ioprio		)
		__field( u64,			off		)
		__field( u64,			addr		)
		__field( u32,			len		)
		__field( u32,			op_flags	)
		__field( u16,			buf_index	)
		__field( u16,			personality	)
		__field( u32,			file_index	)
		__field( u64,			pad1		)
		__field( u64,			addr3		)
		__field( int,			error		)

		__string( op_str, io_uring_get_opcode(sqe->opcode) )
	),

	TP_fast_assign(
		__entry->ctx		= req->ctx;
		__entry->req		= req;
		__entry->user_data	= sqe->user_data;
		__entry->opcode		= sqe->opcode;
		__entry->flags		= sqe->flags;
		__entry->ioprio		= sqe->ioprio;
		__entry->off		= sqe->off;
		__entry->addr		= sqe->addr;
		__entry->len		= sqe->len;
		__entry->op_flags	= sqe->poll32_events;
		__entry->buf_index	= sqe->buf_index;
		__entry->personality	= sqe->personality;
		__entry->file_index	= sqe->file_index;
		__entry->pad1		= sqe->__pad2[0];
		__entry->addr3		= sqe->addr3;
		__entry->error		= error;

		__assign_str(op_str, io_uring_get_opcode(sqe->opcode));
	),

	TP_printk("ring %p, req %p, user_data 0x%llx, "
		  "opcode %s, flags 0x%x, prio=%d, off=%llu, addr=%llu, "
		  "len=%u, rw_flags=0x%x, buf_index=%d, "
		  "personality=%d, file_index=%d, pad=0x%llx, addr3=%llx, "
		  "error=%d",
		  __entry->ctx, __entry->req, __entry->user_data,
		  __get_str(op_str),
		  __entry->flags, __entry->ioprio,
		  (unsigned long long)__entry->off,
		  (unsigned long long) __entry->addr, __entry->len,
		  __entry->op_flags,
		  __entry->buf_index, __entry->personality, __entry->file_index,
		  (unsigned long long) __entry->pad1,
		  (unsigned long long) __entry->addr3, __entry->error)
);


/*
 * io_uring_cqe_overflow - a CQE overflowed
 *
 * @ctx:		pointer to a ring context structure
 * @user_data:		user data associated with the request
 * @res:		CQE result
 * @cflags:		CQE flags
 * @ocqe:		pointer to the overflow cqe (if available)
 *
 */
TRACE_EVENT(io_uring_cqe_overflow,

	TP_PROTO(void *ctx, unsigned long long user_data, s32 res, u32 cflags,
		 void *ocqe),

	TP_ARGS(ctx, user_data, res, cflags, ocqe),

	TP_STRUCT__entry (
		__field(  void *,		ctx		)
		__field(  unsigned long long,	user_data	)
		__field(  s32,			res		)
		__field(  u32,			cflags		)
		__field(  void *,		ocqe		)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->user_data	= user_data;
		__entry->res		= res;
		__entry->cflags		= cflags;
		__entry->ocqe		= ocqe;
	),

	TP_printk("ring %p, user_data 0x%llx, res %d, cflags 0x%x, "
		  "overflow_cqe %p",
		  __entry->ctx, __entry->user_data, __entry->res,
		  __entry->cflags, __entry->ocqe)
);

/*
 * io_uring_task_work_run - ran task work
 *
 * @tctx:		pointer to a io_uring_task
 * @count:		how many functions it ran
 * @loops:		how many loops it ran
 *
 */
TRACE_EVENT(io_uring_task_work_run,

	TP_PROTO(void *tctx, unsigned int count, unsigned int loops),

	TP_ARGS(tctx, count, loops),

	TP_STRUCT__entry (
		__field(  void *,		tctx		)
		__field(  unsigned int,		count		)
		__field(  unsigned int,		loops		)
	),

	TP_fast_assign(
		__entry->tctx		= tctx;
		__entry->count		= count;
		__entry->loops		= loops;
	),

	TP_printk("tctx %p, count %u, loops %u",
		 __entry->tctx, __entry->count, __entry->loops)
);

TRACE_EVENT(io_uring_short_write,

	TP_PROTO(void *ctx, u64 fpos, u64 wanted, u64 got),

	TP_ARGS(ctx, fpos, wanted, got),

	TP_STRUCT__entry(
		__field(void *,	ctx)
		__field(u64,	fpos)
		__field(u64,	wanted)
		__field(u64,	got)
	),

	TP_fast_assign(
		__entry->ctx	= ctx;
		__entry->fpos	= fpos;
		__entry->wanted	= wanted;
		__entry->got	= got;
	),

	TP_printk("ring %p, fpos %lld, wanted %lld, got %lld",
			  __entry->ctx, __entry->fpos,
			  __entry->wanted, __entry->got)
);

/*
 * io_uring_local_work_run - ran ring local task work
 *
 * @tctx:		pointer to a io_uring_ctx
 * @count:		how many functions it ran
 * @loops:		how many loops it ran
 *
 */
TRACE_EVENT(io_uring_local_work_run,

	TP_PROTO(void *ctx, int count, unsigned int loops),

	TP_ARGS(ctx, count, loops),

	TP_STRUCT__entry (
		__field(void *,		ctx	)
		__field(int,		count	)
		__field(unsigned int,	loops	)
	),

	TP_fast_assign(
		__entry->ctx		= ctx;
		__entry->count		= count;
		__entry->loops		= loops;
	),

	TP_printk("ring %p, count %d, loops %u", __entry->ctx, __entry->count, __entry->loops)
);

#endif /* _TRACE_IO_URING_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
