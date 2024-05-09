// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/blk-mq.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fsnotify.h>
#include <linux/poll.h>
#include <linux/nospec.h>
#include <linux/compat.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "opdef.h"
#include "kbuf.h"
#include "rsrc.h"
#include "rw.h"

struct io_rw {
	/* NOTE: kiocb has the file as the first member, so don't do it here */
	struct kiocb			kiocb;
	u64				addr;
	u32				len;
	rwf_t				flags;
};

static inline bool io_file_supports_nowait(struct io_kiocb *req)
{
	return req->flags & REQ_F_SUPPORT_NOWAIT;
}

#ifdef CONFIG_COMPAT
static int io_iov_compat_buffer_select_prep(struct io_rw *rw)
{
	struct compat_iovec __user *uiov;
	compat_ssize_t clen;

	uiov = u64_to_user_ptr(rw->addr);
	if (!access_ok(uiov, sizeof(*uiov)))
		return -EFAULT;
	if (__get_user(clen, &uiov->iov_len))
		return -EFAULT;
	if (clen < 0)
		return -EINVAL;

	rw->len = clen;
	return 0;
}
#endif

static int io_iov_buffer_select_prep(struct io_kiocb *req)
{
	struct iovec __user *uiov;
	struct iovec iov;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	if (rw->len != 1)
		return -EINVAL;

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		return io_iov_compat_buffer_select_prep(rw);
#endif

	uiov = u64_to_user_ptr(rw->addr);
	if (copy_from_user(&iov, uiov, sizeof(*uiov)))
		return -EFAULT;
	rw->len = iov.iov_len;
	return 0;
}

int io_prep_rw(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	unsigned ioprio;
	int ret;

	rw->kiocb.ki_pos = READ_ONCE(sqe->off);
	/* used for fixed read/write too - just read unconditionally */
	req->buf_index = READ_ONCE(sqe->buf_index);

	if (req->opcode == IORING_OP_READ_FIXED ||
	    req->opcode == IORING_OP_WRITE_FIXED) {
		struct io_ring_ctx *ctx = req->ctx;
		u16 index;

		if (unlikely(req->buf_index >= ctx->nr_user_bufs))
			return -EFAULT;
		index = array_index_nospec(req->buf_index, ctx->nr_user_bufs);
		req->imu = ctx->user_bufs[index];
		io_req_set_rsrc_node(req, ctx, 0);
	}

	ioprio = READ_ONCE(sqe->ioprio);
	if (ioprio) {
		ret = ioprio_check_cap(ioprio);
		if (ret)
			return ret;

		rw->kiocb.ki_ioprio = ioprio;
	} else {
		rw->kiocb.ki_ioprio = get_current_ioprio();
	}
	rw->kiocb.dio_complete = NULL;

	rw->addr = READ_ONCE(sqe->addr);
	rw->len = READ_ONCE(sqe->len);
	rw->flags = READ_ONCE(sqe->rw_flags);

	/* Have to do this validation here, as this is in io_read() rw->len might
	 * have chanaged due to buffer selection
	 */
	if (req->opcode == IORING_OP_READV && req->flags & REQ_F_BUFFER_SELECT) {
		ret = io_iov_buffer_select_prep(req);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Multishot read is prepared just like a normal read/write request, only
 * difference is that we set the MULTISHOT flag.
 */
int io_read_mshot_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	int ret;

	ret = io_prep_rw(req, sqe);
	if (unlikely(ret))
		return ret;

	req->flags |= REQ_F_APOLL_MULTISHOT;
	return 0;
}

void io_readv_writev_cleanup(struct io_kiocb *req)
{
	struct io_async_rw *io = req->async_data;

	kfree(io->free_iovec);
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
		kiocb->ki_complete(kiocb, ret);
	}
}

static inline loff_t *io_kiocb_update_pos(struct io_kiocb *req)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	if (rw->kiocb.ki_pos != -1)
		return &rw->kiocb.ki_pos;

	if (!(req->file->f_mode & FMODE_STREAM)) {
		req->flags |= REQ_F_CUR_POS;
		rw->kiocb.ki_pos = req->file->f_pos;
		return &rw->kiocb.ki_pos;
	}

	rw->kiocb.ki_pos = 0;
	return NULL;
}

static void io_req_task_queue_reissue(struct io_kiocb *req)
{
	req->io_task_work.func = io_queue_iowq;
	io_req_task_work_add(req);
}

#ifdef CONFIG_BLOCK
static bool io_resubmit_prep(struct io_kiocb *req)
{
	struct io_async_rw *io = req->async_data;

	if (!req_has_async_data(req))
		return !io_req_prep_async(req);
	iov_iter_restore(&io->s.iter, &io->s.iter_state);
	return true;
}

static bool io_rw_should_reissue(struct io_kiocb *req)
{
	umode_t mode = file_inode(req->file)->i_mode;
	struct io_ring_ctx *ctx = req->ctx;

	if (!S_ISBLK(mode) && !S_ISREG(mode))
		return false;
	if ((req->flags & REQ_F_NOWAIT) || (io_wq_current_is_worker() &&
	    !(ctx->flags & IORING_SETUP_IOPOLL)))
		return false;
	/*
	 * If ref is dying, we might be running poll reap from the exit work.
	 * Don't attempt to reissue from that path, just let it fail with
	 * -EAGAIN.
	 */
	if (percpu_ref_is_dying(&ctx->refs))
		return false;
	/*
	 * Play it safe and assume not safe to re-import and reissue if we're
	 * not in the original thread group (or in task context).
	 */
	if (!same_thread_group(req->task, current) || !in_task())
		return false;
	return true;
}
#else
static bool io_resubmit_prep(struct io_kiocb *req)
{
	return false;
}
static bool io_rw_should_reissue(struct io_kiocb *req)
{
	return false;
}
#endif

static void io_req_end_write(struct io_kiocb *req)
{
	if (req->flags & REQ_F_ISREG) {
		struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

		kiocb_end_write(&rw->kiocb);
	}
}

/*
 * Trigger the notifications after having done some IO, and finish the write
 * accounting, if any.
 */
static void io_req_io_end(struct io_kiocb *req)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

	if (rw->kiocb.ki_flags & IOCB_WRITE) {
		io_req_end_write(req);
		fsnotify_modify(req->file);
	} else {
		fsnotify_access(req->file);
	}
}

static bool __io_complete_rw_common(struct io_kiocb *req, long res)
{
	if (unlikely(res != req->cqe.res)) {
		if ((res == -EAGAIN || res == -EOPNOTSUPP) &&
		    io_rw_should_reissue(req)) {
			/*
			 * Reissue will start accounting again, finish the
			 * current cycle.
			 */
			io_req_io_end(req);
			req->flags |= REQ_F_REISSUE | REQ_F_PARTIAL_IO;
			return true;
		}
		req_set_fail(req);
		req->cqe.res = res;
	}
	return false;
}

static inline int io_fixup_rw_res(struct io_kiocb *req, long res)
{
	struct io_async_rw *io = req->async_data;

	/* add previously done IO, if any */
	if (req_has_async_data(req) && io->bytes_done > 0) {
		if (res < 0)
			res = io->bytes_done;
		else
			res += io->bytes_done;
	}
	return res;
}

void io_req_rw_complete(struct io_kiocb *req, struct io_tw_state *ts)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct kiocb *kiocb = &rw->kiocb;

	if ((kiocb->ki_flags & IOCB_DIO_CALLER_COMP) && kiocb->dio_complete) {
		long res = kiocb->dio_complete(rw->kiocb.private);

		io_req_set_res(req, io_fixup_rw_res(req, res), 0);
	}

	io_req_io_end(req);

	if (req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING)) {
		unsigned issue_flags = ts->locked ? 0 : IO_URING_F_UNLOCKED;

		req->cqe.flags |= io_put_kbuf(req, issue_flags);
	}
	io_req_task_complete(req, ts);
}

static void io_complete_rw(struct kiocb *kiocb, long res)
{
	struct io_rw *rw = container_of(kiocb, struct io_rw, kiocb);
	struct io_kiocb *req = cmd_to_io_kiocb(rw);

	if (!kiocb->dio_complete || !(kiocb->ki_flags & IOCB_DIO_CALLER_COMP)) {
		if (__io_complete_rw_common(req, res))
			return;
		io_req_set_res(req, io_fixup_rw_res(req, res), 0);
	}
	req->io_task_work.func = io_req_rw_complete;
	__io_req_task_work_add(req, IOU_F_TWQ_LAZY_WAKE);
}

static void io_complete_rw_iopoll(struct kiocb *kiocb, long res)
{
	struct io_rw *rw = container_of(kiocb, struct io_rw, kiocb);
	struct io_kiocb *req = cmd_to_io_kiocb(rw);

	if (kiocb->ki_flags & IOCB_WRITE)
		io_req_end_write(req);
	if (unlikely(res != req->cqe.res)) {
		if (res == -EAGAIN && io_rw_should_reissue(req)) {
			req->flags |= REQ_F_REISSUE | REQ_F_PARTIAL_IO;
			return;
		}
		req->cqe.res = res;
	}

	/* order with io_iopoll_complete() checking ->iopoll_completed */
	smp_store_release(&req->iopoll_completed, 1);
}

static int kiocb_done(struct io_kiocb *req, ssize_t ret,
		       unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	unsigned final_ret = io_fixup_rw_res(req, ret);

	if (ret >= 0 && req->flags & REQ_F_CUR_POS)
		req->file->f_pos = rw->kiocb.ki_pos;
	if (ret >= 0 && (rw->kiocb.ki_complete == io_complete_rw)) {
		if (!__io_complete_rw_common(req, ret)) {
			/*
			 * Safe to call io_end from here as we're inline
			 * from the submission path.
			 */
			io_req_io_end(req);
			io_req_set_res(req, final_ret,
				       io_put_kbuf(req, issue_flags));
			return IOU_OK;
		}
	} else {
		io_rw_done(&rw->kiocb, ret);
	}

	if (req->flags & REQ_F_REISSUE) {
		req->flags &= ~REQ_F_REISSUE;
		if (io_resubmit_prep(req))
			io_req_task_queue_reissue(req);
		else
			io_req_task_queue_fail(req, final_ret);
	}
	return IOU_ISSUE_SKIP_COMPLETE;
}

static struct iovec *__io_import_iovec(int ddir, struct io_kiocb *req,
				       struct io_rw_state *s,
				       unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct iov_iter *iter = &s->iter;
	u8 opcode = req->opcode;
	struct iovec *iovec;
	void __user *buf;
	size_t sqe_len;
	ssize_t ret;

	if (opcode == IORING_OP_READ_FIXED || opcode == IORING_OP_WRITE_FIXED) {
		ret = io_import_fixed(ddir, iter, req->imu, rw->addr, rw->len);
		if (ret)
			return ERR_PTR(ret);
		return NULL;
	}

	buf = u64_to_user_ptr(rw->addr);
	sqe_len = rw->len;

	if (!io_issue_defs[opcode].vectored || req->flags & REQ_F_BUFFER_SELECT) {
		if (io_do_buffer_select(req)) {
			buf = io_buffer_select(req, &sqe_len, issue_flags);
			if (!buf)
				return ERR_PTR(-ENOBUFS);
			rw->addr = (unsigned long) buf;
			rw->len = sqe_len;
		}

		ret = import_ubuf(ddir, buf, sqe_len, iter);
		if (ret)
			return ERR_PTR(ret);
		return NULL;
	}

	iovec = s->fast_iov;
	ret = __import_iovec(ddir, buf, sqe_len, UIO_FASTIOV, &iovec, iter,
			      req->ctx->compat);
	if (unlikely(ret < 0))
		return ERR_PTR(ret);
	return iovec;
}

static inline int io_import_iovec(int rw, struct io_kiocb *req,
				  struct iovec **iovec, struct io_rw_state *s,
				  unsigned int issue_flags)
{
	*iovec = __io_import_iovec(rw, req, s, issue_flags);
	if (IS_ERR(*iovec))
		return PTR_ERR(*iovec);

	iov_iter_save_state(&s->iter, &s->iter_state);
	return 0;
}

static inline loff_t *io_kiocb_ppos(struct kiocb *kiocb)
{
	return (kiocb->ki_filp->f_mode & FMODE_STREAM) ? NULL : &kiocb->ki_pos;
}

/*
 * For files that don't have ->read_iter() and ->write_iter(), handle them
 * by looping over ->read() or ->write() manually.
 */
static ssize_t loop_rw_iter(int ddir, struct io_rw *rw, struct iov_iter *iter)
{
	struct kiocb *kiocb = &rw->kiocb;
	struct file *file = kiocb->ki_filp;
	ssize_t ret = 0;
	loff_t *ppos;

	/*
	 * Don't support polled IO through this interface, and we can't
	 * support non-blocking either. For the latter, this just causes
	 * the kiocb to be handled from an async context.
	 */
	if (kiocb->ki_flags & IOCB_HIPRI)
		return -EOPNOTSUPP;
	if ((kiocb->ki_flags & IOCB_NOWAIT) &&
	    !(kiocb->ki_filp->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ppos = io_kiocb_ppos(kiocb);

	while (iov_iter_count(iter)) {
		void __user *addr;
		size_t len;
		ssize_t nr;

		if (iter_is_ubuf(iter)) {
			addr = iter->ubuf + iter->iov_offset;
			len = iov_iter_count(iter);
		} else if (!iov_iter_is_bvec(iter)) {
			addr = iter_iov_addr(iter);
			len = iter_iov_len(iter);
		} else {
			addr = u64_to_user_ptr(rw->addr);
			len = rw->len;
		}

		if (ddir == READ)
			nr = file->f_op->read(file, addr, len, ppos);
		else
			nr = file->f_op->write(file, addr, len, ppos);

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (!iov_iter_is_bvec(iter)) {
			iov_iter_advance(iter, nr);
		} else {
			rw->addr += nr;
			rw->len -= nr;
			if (!rw->len)
				break;
		}
		if (nr != len)
			break;
	}

	return ret;
}

static void io_req_map_rw(struct io_kiocb *req, const struct iovec *iovec,
			  const struct iovec *fast_iov, struct iov_iter *iter)
{
	struct io_async_rw *io = req->async_data;

	memcpy(&io->s.iter, iter, sizeof(*iter));
	io->free_iovec = iovec;
	io->bytes_done = 0;
	/* can only be fixed buffers, no need to do anything */
	if (iov_iter_is_bvec(iter) || iter_is_ubuf(iter))
		return;
	if (!iovec) {
		unsigned iov_off = 0;

		io->s.iter.__iov = io->s.fast_iov;
		if (iter->__iov != fast_iov) {
			iov_off = iter_iov(iter) - fast_iov;
			io->s.iter.__iov += iov_off;
		}
		if (io->s.fast_iov != fast_iov)
			memcpy(io->s.fast_iov + iov_off, fast_iov + iov_off,
			       sizeof(struct iovec) * iter->nr_segs);
	} else {
		req->flags |= REQ_F_NEED_CLEANUP;
	}
}

static int io_setup_async_rw(struct io_kiocb *req, const struct iovec *iovec,
			     struct io_rw_state *s, bool force)
{
	if (!force && !io_cold_defs[req->opcode].prep_async)
		return 0;
	if (!req_has_async_data(req)) {
		struct io_async_rw *iorw;

		if (io_alloc_async_data(req)) {
			kfree(iovec);
			return -ENOMEM;
		}

		io_req_map_rw(req, iovec, s->fast_iov, &s->iter);
		iorw = req->async_data;
		/* we've copied and mapped the iter, ensure state is saved */
		iov_iter_save_state(&iorw->s.iter, &iorw->s.iter_state);
	}
	return 0;
}

static inline int io_rw_prep_async(struct io_kiocb *req, int rw)
{
	struct io_async_rw *iorw = req->async_data;
	struct iovec *iov;
	int ret;

	/* submission path, ->uring_lock should already be taken */
	ret = io_import_iovec(rw, req, &iov, &iorw->s, 0);
	if (unlikely(ret < 0))
		return ret;

	iorw->bytes_done = 0;
	iorw->free_iovec = iov;
	if (iov)
		req->flags |= REQ_F_NEED_CLEANUP;
	return 0;
}

int io_readv_prep_async(struct io_kiocb *req)
{
	return io_rw_prep_async(req, ITER_DEST);
}

int io_writev_prep_async(struct io_kiocb *req)
{
	return io_rw_prep_async(req, ITER_SOURCE);
}

/*
 * This is our waitqueue callback handler, registered through __folio_lock_async()
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
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct wait_page_key *key = arg;

	wpq = container_of(wait, struct wait_page_queue, wait);

	if (!wake_page_match(wpq, key))
		return 0;

	rw->kiocb.ki_flags &= ~IOCB_WAITQ;
	list_del_init(&wait->entry);
	io_req_task_queue(req);
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
	struct io_async_rw *io = req->async_data;
	struct wait_page_queue *wait = &io->wpq;
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct kiocb *kiocb = &rw->kiocb;

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

static inline int io_iter_do_read(struct io_rw *rw, struct iov_iter *iter)
{
	struct file *file = rw->kiocb.ki_filp;

	if (likely(file->f_op->read_iter))
		return call_read_iter(file, &rw->kiocb, iter);
	else if (file->f_op->read)
		return loop_rw_iter(READ, rw, iter);
	else
		return -EINVAL;
}

static bool need_complete_io(struct io_kiocb *req)
{
	return req->flags & REQ_F_ISREG ||
		S_ISBLK(file_inode(req->file)->i_mode);
}

static int io_rw_init_file(struct io_kiocb *req, fmode_t mode)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct kiocb *kiocb = &rw->kiocb;
	struct io_ring_ctx *ctx = req->ctx;
	struct file *file = req->file;
	int ret;

	if (unlikely(!file || !(file->f_mode & mode)))
		return -EBADF;

	if (!(req->flags & REQ_F_FIXED_FILE))
		req->flags |= io_file_get_flags(file);

	kiocb->ki_flags = file->f_iocb_flags;
	ret = kiocb_set_rw_flags(kiocb, rw->flags);
	if (unlikely(ret))
		return ret;
	kiocb->ki_flags |= IOCB_ALLOC_CACHE;

	/*
	 * If the file is marked O_NONBLOCK, still allow retry for it if it
	 * supports async. Otherwise it's impossible to use O_NONBLOCK files
	 * reliably. If not, or it IOCB_NOWAIT is set, don't retry.
	 */
	if ((kiocb->ki_flags & IOCB_NOWAIT) ||
	    ((file->f_flags & O_NONBLOCK) && !io_file_supports_nowait(req)))
		req->flags |= REQ_F_NOWAIT;

	if (ctx->flags & IORING_SETUP_IOPOLL) {
		if (!(kiocb->ki_flags & IOCB_DIRECT) || !file->f_op->iopoll)
			return -EOPNOTSUPP;

		kiocb->private = NULL;
		kiocb->ki_flags |= IOCB_HIPRI;
		kiocb->ki_complete = io_complete_rw_iopoll;
		req->iopoll_completed = 0;
	} else {
		if (kiocb->ki_flags & IOCB_HIPRI)
			return -EINVAL;
		kiocb->ki_complete = io_complete_rw;
	}

	return 0;
}

static int __io_read(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_rw_state __s, *s = &__s;
	struct iovec *iovec;
	struct kiocb *kiocb = &rw->kiocb;
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	struct io_async_rw *io;
	ssize_t ret, ret2;
	loff_t *ppos;

	if (!req_has_async_data(req)) {
		ret = io_import_iovec(ITER_DEST, req, &iovec, s, issue_flags);
		if (unlikely(ret < 0))
			return ret;
	} else {
		io = req->async_data;
		s = &io->s;

		/*
		 * Safe and required to re-import if we're using provided
		 * buffers, as we dropped the selected one before retry.
		 */
		if (io_do_buffer_select(req)) {
			ret = io_import_iovec(ITER_DEST, req, &iovec, s, issue_flags);
			if (unlikely(ret < 0))
				return ret;
		}

		/*
		 * We come here from an earlier attempt, restore our state to
		 * match in case it doesn't. It's cheap enough that we don't
		 * need to make this conditional.
		 */
		iov_iter_restore(&s->iter, &s->iter_state);
		iovec = NULL;
	}
	ret = io_rw_init_file(req, FMODE_READ);
	if (unlikely(ret)) {
		kfree(iovec);
		return ret;
	}
	req->cqe.res = iov_iter_count(&s->iter);

	if (force_nonblock) {
		/* If the file doesn't support async, just async punt */
		if (unlikely(!io_file_supports_nowait(req))) {
			ret = io_setup_async_rw(req, iovec, s, true);
			return ret ?: -EAGAIN;
		}
		kiocb->ki_flags |= IOCB_NOWAIT;
	} else {
		/* Ensure we clear previously set non-block flag */
		kiocb->ki_flags &= ~IOCB_NOWAIT;
	}

	ppos = io_kiocb_update_pos(req);

	ret = rw_verify_area(READ, req->file, ppos, req->cqe.res);
	if (unlikely(ret)) {
		kfree(iovec);
		return ret;
	}

	ret = io_iter_do_read(rw, &s->iter);

	if (ret == -EAGAIN || (req->flags & REQ_F_REISSUE)) {
		req->flags &= ~REQ_F_REISSUE;
		/*
		 * If we can poll, just do that. For a vectored read, we'll
		 * need to copy state first.
		 */
		if (file_can_poll(req->file) && !io_issue_defs[req->opcode].vectored)
			return -EAGAIN;
		/* IOPOLL retry should happen for io-wq threads */
		if (!force_nonblock && !(req->ctx->flags & IORING_SETUP_IOPOLL))
			goto done;
		/* no retry on NONBLOCK nor RWF_NOWAIT */
		if (req->flags & REQ_F_NOWAIT)
			goto done;
		ret = 0;
	} else if (ret == -EIOCBQUEUED) {
		if (iovec)
			kfree(iovec);
		return IOU_ISSUE_SKIP_COMPLETE;
	} else if (ret == req->cqe.res || ret <= 0 || !force_nonblock ||
		   (req->flags & REQ_F_NOWAIT) || !need_complete_io(req)) {
		/* read all, failed, already did sync or don't want to retry */
		goto done;
	}

	/*
	 * Don't depend on the iter state matching what was consumed, or being
	 * untouched in case of error. Restore it and we'll advance it
	 * manually if we need to.
	 */
	iov_iter_restore(&s->iter, &s->iter_state);

	ret2 = io_setup_async_rw(req, iovec, s, true);
	iovec = NULL;
	if (ret2) {
		ret = ret > 0 ? ret : ret2;
		goto done;
	}

	io = req->async_data;
	s = &io->s;
	/*
	 * Now use our persistent iterator and state, if we aren't already.
	 * We've restored and mapped the iter to match.
	 */

	do {
		/*
		 * We end up here because of a partial read, either from
		 * above or inside this loop. Advance the iter by the bytes
		 * that were consumed.
		 */
		iov_iter_advance(&s->iter, ret);
		if (!iov_iter_count(&s->iter))
			break;
		io->bytes_done += ret;
		iov_iter_save_state(&s->iter, &s->iter_state);

		/* if we can retry, do so with the callbacks armed */
		if (!io_rw_should_retry(req)) {
			kiocb->ki_flags &= ~IOCB_WAITQ;
			return -EAGAIN;
		}

		req->cqe.res = iov_iter_count(&s->iter);
		/*
		 * Now retry read with the IOCB_WAITQ parts set in the iocb. If
		 * we get -EIOCBQUEUED, then we'll get a notification when the
		 * desired page gets unlocked. We can also get a partial read
		 * here, and if we do, then just retry at the new offset.
		 */
		ret = io_iter_do_read(rw, &s->iter);
		if (ret == -EIOCBQUEUED)
			return IOU_ISSUE_SKIP_COMPLETE;
		/* we got some bytes, but not all. retry. */
		kiocb->ki_flags &= ~IOCB_WAITQ;
		iov_iter_restore(&s->iter, &s->iter_state);
	} while (ret > 0);
done:
	/* it's faster to check here then delegate to kfree */
	if (iovec)
		kfree(iovec);
	return ret;
}

int io_read(struct io_kiocb *req, unsigned int issue_flags)
{
	int ret;

	ret = __io_read(req, issue_flags);
	if (ret >= 0)
		return kiocb_done(req, ret, issue_flags);

	return ret;
}

int io_read_mshot(struct io_kiocb *req, unsigned int issue_flags)
{
	unsigned int cflags = 0;
	int ret;

	/*
	 * Multishot MUST be used on a pollable file
	 */
	if (!file_can_poll(req->file))
		return -EBADFD;

	ret = __io_read(req, issue_flags);

	/*
	 * If we get -EAGAIN, recycle our buffer and just let normal poll
	 * handling arm it.
	 */
	if (ret == -EAGAIN) {
		io_kbuf_recycle(req, issue_flags);
		return -EAGAIN;
	}

	/*
	 * Any successful return value will keep the multishot read armed.
	 */
	if (ret > 0) {
		/*
		 * Put our buffer and post a CQE. If we fail to post a CQE, then
		 * jump to the termination path. This request is then done.
		 */
		cflags = io_put_kbuf(req, issue_flags);

		if (io_fill_cqe_req_aux(req,
					issue_flags & IO_URING_F_COMPLETE_DEFER,
					ret, cflags | IORING_CQE_F_MORE)) {
			if (issue_flags & IO_URING_F_MULTISHOT)
				return IOU_ISSUE_SKIP_COMPLETE;
			return -EAGAIN;
		}
	}

	/*
	 * Either an error, or we've hit overflow posting the CQE. For any
	 * multishot request, hitting overflow will terminate it.
	 */
	io_req_set_res(req, ret, cflags);
	if (issue_flags & IO_URING_F_MULTISHOT)
		return IOU_STOP_MULTISHOT;
	return IOU_OK;
}

int io_write(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);
	struct io_rw_state __s, *s = &__s;
	struct iovec *iovec;
	struct kiocb *kiocb = &rw->kiocb;
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	ssize_t ret, ret2;
	loff_t *ppos;

	if (!req_has_async_data(req)) {
		ret = io_import_iovec(ITER_SOURCE, req, &iovec, s, issue_flags);
		if (unlikely(ret < 0))
			return ret;
	} else {
		struct io_async_rw *io = req->async_data;

		s = &io->s;
		iov_iter_restore(&s->iter, &s->iter_state);
		iovec = NULL;
	}
	ret = io_rw_init_file(req, FMODE_WRITE);
	if (unlikely(ret)) {
		kfree(iovec);
		return ret;
	}
	req->cqe.res = iov_iter_count(&s->iter);

	if (force_nonblock) {
		/* If the file doesn't support async, just async punt */
		if (unlikely(!io_file_supports_nowait(req)))
			goto copy_iov;

		/* File path supports NOWAIT for non-direct_IO only for block devices. */
		if (!(kiocb->ki_flags & IOCB_DIRECT) &&
			!(kiocb->ki_filp->f_mode & FMODE_BUF_WASYNC) &&
			(req->flags & REQ_F_ISREG))
			goto copy_iov;

		kiocb->ki_flags |= IOCB_NOWAIT;
	} else {
		/* Ensure we clear previously set non-block flag */
		kiocb->ki_flags &= ~IOCB_NOWAIT;
	}

	ppos = io_kiocb_update_pos(req);

	ret = rw_verify_area(WRITE, req->file, ppos, req->cqe.res);
	if (unlikely(ret)) {
		kfree(iovec);
		return ret;
	}

	if (req->flags & REQ_F_ISREG)
		kiocb_start_write(kiocb);
	kiocb->ki_flags |= IOCB_WRITE;

	if (likely(req->file->f_op->write_iter))
		ret2 = call_write_iter(req->file, kiocb, &s->iter);
	else if (req->file->f_op->write)
		ret2 = loop_rw_iter(WRITE, rw, &s->iter);
	else
		ret2 = -EINVAL;

	if (req->flags & REQ_F_REISSUE) {
		req->flags &= ~REQ_F_REISSUE;
		ret2 = -EAGAIN;
	}

	/*
	 * Raw bdev writes will return -EOPNOTSUPP for IOCB_NOWAIT. Just
	 * retry them without IOCB_NOWAIT.
	 */
	if (ret2 == -EOPNOTSUPP && (kiocb->ki_flags & IOCB_NOWAIT))
		ret2 = -EAGAIN;
	/* no retry on NONBLOCK nor RWF_NOWAIT */
	if (ret2 == -EAGAIN && (req->flags & REQ_F_NOWAIT))
		goto done;
	if (!force_nonblock || ret2 != -EAGAIN) {
		/* IOPOLL retry should happen for io-wq threads */
		if (ret2 == -EAGAIN && (req->ctx->flags & IORING_SETUP_IOPOLL))
			goto copy_iov;

		if (ret2 != req->cqe.res && ret2 >= 0 && need_complete_io(req)) {
			struct io_async_rw *io;

			trace_io_uring_short_write(req->ctx, kiocb->ki_pos - ret2,
						req->cqe.res, ret2);

			/* This is a partial write. The file pos has already been
			 * updated, setup the async struct to complete the request
			 * in the worker. Also update bytes_done to account for
			 * the bytes already written.
			 */
			iov_iter_save_state(&s->iter, &s->iter_state);
			ret = io_setup_async_rw(req, iovec, s, true);

			io = req->async_data;
			if (io)
				io->bytes_done += ret2;

			if (kiocb->ki_flags & IOCB_WRITE)
				io_req_end_write(req);
			return ret ? ret : -EAGAIN;
		}
done:
		ret = kiocb_done(req, ret2, issue_flags);
	} else {
copy_iov:
		iov_iter_restore(&s->iter, &s->iter_state);
		ret = io_setup_async_rw(req, iovec, s, false);
		if (!ret) {
			if (kiocb->ki_flags & IOCB_WRITE)
				io_req_end_write(req);
			return -EAGAIN;
		}
		return ret;
	}
	/* it's reportedly faster than delegating the null check to kfree() */
	if (iovec)
		kfree(iovec);
	return ret;
}

void io_rw_fail(struct io_kiocb *req)
{
	int res;

	res = io_fixup_rw_res(req, req->cqe.res);
	io_req_set_res(req, res, req->cqe.flags);
}

int io_do_iopoll(struct io_ring_ctx *ctx, bool force_nonspin)
{
	struct io_wq_work_node *pos, *start, *prev;
	unsigned int poll_flags = 0;
	DEFINE_IO_COMP_BATCH(iob);
	int nr_events = 0;

	/*
	 * Only spin for completions if we don't have multiple devices hanging
	 * off our complete list.
	 */
	if (ctx->poll_multi_queue || force_nonspin)
		poll_flags |= BLK_POLL_ONESHOT;

	wq_list_for_each(pos, start, &ctx->iopoll_list) {
		struct io_kiocb *req = container_of(pos, struct io_kiocb, comp_list);
		struct file *file = req->file;
		int ret;

		/*
		 * Move completed and retryable entries to our local lists.
		 * If we find a request that requires polling, break out
		 * and complete those lists first, if we have entries there.
		 */
		if (READ_ONCE(req->iopoll_completed))
			break;

		if (req->opcode == IORING_OP_URING_CMD) {
			struct io_uring_cmd *ioucmd;

			ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
			ret = file->f_op->uring_cmd_iopoll(ioucmd, &iob,
								poll_flags);
		} else {
			struct io_rw *rw = io_kiocb_to_cmd(req, struct io_rw);

			ret = file->f_op->iopoll(&rw->kiocb, &iob, poll_flags);
		}
		if (unlikely(ret < 0))
			return ret;
		else if (ret)
			poll_flags |= BLK_POLL_ONESHOT;

		/* iopoll may have completed current req */
		if (!rq_list_empty(iob.req_list) ||
		    READ_ONCE(req->iopoll_completed))
			break;
	}

	if (!rq_list_empty(iob.req_list))
		iob.complete(&iob);
	else if (!pos)
		return 0;

	prev = start;
	wq_list_for_each_resume(pos, prev) {
		struct io_kiocb *req = container_of(pos, struct io_kiocb, comp_list);

		/* order with io_complete_rw_iopoll(), e.g. ->result updates */
		if (!smp_load_acquire(&req->iopoll_completed))
			break;
		nr_events++;
		req->cqe.flags = io_put_kbuf(req, 0);
	}
	if (unlikely(!nr_events))
		return 0;

	pos = start ? start->next : ctx->iopoll_list.first;
	wq_list_cut(&ctx->iopoll_list, prev, start);

	if (WARN_ON_ONCE(!wq_list_empty(&ctx->submit_state.compl_reqs)))
		return 0;
	ctx->submit_state.compl_reqs.first = pos;
	__io_submit_flush_completions(ctx);
	return nr_events;
}
