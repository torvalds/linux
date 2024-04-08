// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/compat.h>
#include <net/compat.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "kbuf.h"
#include "alloc_cache.h"
#include "net.h"
#include "notif.h"
#include "rsrc.h"

#if defined(CONFIG_NET)
struct io_shutdown {
	struct file			*file;
	int				how;
};

struct io_accept {
	struct file			*file;
	struct sockaddr __user		*addr;
	int __user			*addr_len;
	int				flags;
	u32				file_slot;
	unsigned long			nofile;
};

struct io_socket {
	struct file			*file;
	int				domain;
	int				type;
	int				protocol;
	int				flags;
	u32				file_slot;
	unsigned long			nofile;
};

struct io_connect {
	struct file			*file;
	struct sockaddr __user		*addr;
	int				addr_len;
	bool				in_progress;
	bool				seen_econnaborted;
};

struct io_sr_msg {
	struct file			*file;
	union {
		struct compat_msghdr __user	*umsg_compat;
		struct user_msghdr __user	*umsg;
		void __user			*buf;
	};
	unsigned			len;
	unsigned			done_io;
	unsigned			msg_flags;
	u16				flags;
	/* initialised and used only by !msg send variants */
	u16				addr_len;
	u16				buf_group;
	void __user			*addr;
	void __user			*msg_control;
	/* used only for send zerocopy */
	struct io_kiocb 		*notif;
};

int io_shutdown_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_shutdown *shutdown = io_kiocb_to_cmd(req, struct io_shutdown);

	if (unlikely(sqe->off || sqe->addr || sqe->rw_flags ||
		     sqe->buf_index || sqe->splice_fd_in))
		return -EINVAL;

	shutdown->how = READ_ONCE(sqe->len);
	return 0;
}

int io_shutdown(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_shutdown *shutdown = io_kiocb_to_cmd(req, struct io_shutdown);
	struct socket *sock;
	int ret;

	if (issue_flags & IO_URING_F_NONBLOCK)
		return -EAGAIN;

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;

	ret = __sys_shutdown_sock(sock, shutdown->how);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

static bool io_net_retry(struct socket *sock, int flags)
{
	if (!(flags & MSG_WAITALL))
		return false;
	return sock->type == SOCK_STREAM || sock->type == SOCK_SEQPACKET;
}

static void io_netmsg_recycle(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_async_msghdr *hdr = req->async_data;

	if (!req_has_async_data(req) || issue_flags & IO_URING_F_UNLOCKED)
		return;

	/* Let normal cleanup path reap it if we fail adding to the cache */
	if (io_alloc_cache_put(&req->ctx->netmsg_cache, &hdr->cache)) {
		req->async_data = NULL;
		req->flags &= ~REQ_F_ASYNC_DATA;
	}
}

static struct io_async_msghdr *io_msg_alloc_async(struct io_kiocb *req,
						  unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_cache_entry *entry;
	struct io_async_msghdr *hdr;

	if (!(issue_flags & IO_URING_F_UNLOCKED)) {
		entry = io_alloc_cache_get(&ctx->netmsg_cache);
		if (entry) {
			hdr = container_of(entry, struct io_async_msghdr, cache);
			hdr->free_iov = NULL;
			req->flags |= REQ_F_ASYNC_DATA;
			req->async_data = hdr;
			return hdr;
		}
	}

	if (!io_alloc_async_data(req)) {
		hdr = req->async_data;
		hdr->free_iov = NULL;
		return hdr;
	}
	return NULL;
}

static inline struct io_async_msghdr *io_msg_alloc_async_prep(struct io_kiocb *req)
{
	/* ->prep_async is always called from the submission context */
	return io_msg_alloc_async(req, 0);
}

static int io_setup_async_msg(struct io_kiocb *req,
			      struct io_async_msghdr *kmsg,
			      unsigned int issue_flags)
{
	struct io_async_msghdr *async_msg;

	if (req_has_async_data(req))
		return -EAGAIN;
	async_msg = io_msg_alloc_async(req, issue_flags);
	if (!async_msg) {
		kfree(kmsg->free_iov);
		return -ENOMEM;
	}
	req->flags |= REQ_F_NEED_CLEANUP;
	memcpy(async_msg, kmsg, sizeof(*kmsg));
	if (async_msg->msg.msg_name)
		async_msg->msg.msg_name = &async_msg->addr;

	if ((req->flags & REQ_F_BUFFER_SELECT) && !async_msg->msg.msg_iter.nr_segs)
		return -EAGAIN;

	/* if were using fast_iov, set it to the new one */
	if (!kmsg->free_iov) {
		size_t fast_idx = kmsg->msg.msg_iter.iov - kmsg->fast_iov;
		async_msg->msg.msg_iter.iov = &async_msg->fast_iov[fast_idx];
	}

	return -EAGAIN;
}

#ifdef CONFIG_COMPAT
static int io_compat_msg_copy_hdr(struct io_kiocb *req,
				  struct io_async_msghdr *iomsg,
				  struct compat_msghdr *msg, int ddir)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct compat_iovec __user *uiov;
	int ret;

	if (copy_from_user(msg, sr->umsg_compat, sizeof(*msg)))
		return -EFAULT;

	uiov = compat_ptr(msg->msg_iov);
	if (req->flags & REQ_F_BUFFER_SELECT) {
		compat_ssize_t clen;

		iomsg->free_iov = NULL;
		if (msg->msg_iovlen == 0) {
			sr->len = 0;
		} else if (msg->msg_iovlen > 1) {
			return -EINVAL;
		} else {
			if (!access_ok(uiov, sizeof(*uiov)))
				return -EFAULT;
			if (__get_user(clen, &uiov->iov_len))
				return -EFAULT;
			if (clen < 0)
				return -EINVAL;
			sr->len = clen;
		}

		return 0;
	}

	iomsg->free_iov = iomsg->fast_iov;
	ret = __import_iovec(ddir, (struct iovec __user *)uiov, msg->msg_iovlen,
				UIO_FASTIOV, &iomsg->free_iov,
				&iomsg->msg.msg_iter, true);
	if (unlikely(ret < 0))
		return ret;

	return 0;
}
#endif

static int io_msg_copy_hdr(struct io_kiocb *req, struct io_async_msghdr *iomsg,
			   struct user_msghdr *msg, int ddir)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	int ret;

	if (copy_from_user(msg, sr->umsg, sizeof(*sr->umsg)))
		return -EFAULT;

	if (req->flags & REQ_F_BUFFER_SELECT) {
		if (msg->msg_iovlen == 0) {
			sr->len = iomsg->fast_iov[0].iov_len = 0;
			iomsg->fast_iov[0].iov_base = NULL;
			iomsg->free_iov = NULL;
		} else if (msg->msg_iovlen > 1) {
			return -EINVAL;
		} else {
			if (copy_from_user(iomsg->fast_iov, msg->msg_iov,
					   sizeof(*msg->msg_iov)))
				return -EFAULT;
			sr->len = iomsg->fast_iov[0].iov_len;
			iomsg->free_iov = NULL;
		}

		return 0;
	}

	iomsg->free_iov = iomsg->fast_iov;
	ret = __import_iovec(ddir, msg->msg_iov, msg->msg_iovlen, UIO_FASTIOV,
				&iomsg->free_iov, &iomsg->msg.msg_iter, false);
	if (unlikely(ret < 0))
		return ret;

	return 0;
}

static int io_sendmsg_copy_hdr(struct io_kiocb *req,
			       struct io_async_msghdr *iomsg)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct user_msghdr msg;
	int ret;

	iomsg->msg.msg_name = &iomsg->addr;
	iomsg->msg.msg_iter.nr_segs = 0;

#ifdef CONFIG_COMPAT
	if (unlikely(req->ctx->compat)) {
		struct compat_msghdr cmsg;

		ret = io_compat_msg_copy_hdr(req, iomsg, &cmsg, ITER_SOURCE);
		if (unlikely(ret))
			return ret;

		return __get_compat_msghdr(&iomsg->msg, &cmsg, NULL);
	}
#endif

	ret = io_msg_copy_hdr(req, iomsg, &msg, ITER_SOURCE);
	if (unlikely(ret))
		return ret;

	ret = __copy_msghdr(&iomsg->msg, &msg, NULL);

	/* save msg_control as sys_sendmsg() overwrites it */
	sr->msg_control = iomsg->msg.msg_control_user;
	return ret;
}

int io_send_prep_async(struct io_kiocb *req)
{
	struct io_sr_msg *zc = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_async_msghdr *io;
	int ret;

	if (!zc->addr || req_has_async_data(req))
		return 0;
	io = io_msg_alloc_async_prep(req);
	if (!io)
		return -ENOMEM;
	ret = move_addr_to_kernel(zc->addr, zc->addr_len, &io->addr);
	return ret;
}

static int io_setup_async_addr(struct io_kiocb *req,
			      struct sockaddr_storage *addr_storage,
			      unsigned int issue_flags)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_async_msghdr *io;

	if (!sr->addr || req_has_async_data(req))
		return -EAGAIN;
	io = io_msg_alloc_async(req, issue_flags);
	if (!io)
		return -ENOMEM;
	memcpy(&io->addr, addr_storage, sizeof(io->addr));
	return -EAGAIN;
}

int io_sendmsg_prep_async(struct io_kiocb *req)
{
	int ret;

	if (!io_msg_alloc_async_prep(req))
		return -ENOMEM;
	ret = io_sendmsg_copy_hdr(req, req->async_data);
	if (!ret)
		req->flags |= REQ_F_NEED_CLEANUP;
	return ret;
}

void io_sendmsg_recvmsg_cleanup(struct io_kiocb *req)
{
	struct io_async_msghdr *io = req->async_data;

	kfree(io->free_iov);
}

int io_sendmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);

	if (req->opcode == IORING_OP_SEND) {
		if (READ_ONCE(sqe->__pad3[0]))
			return -EINVAL;
		sr->addr = u64_to_user_ptr(READ_ONCE(sqe->addr2));
		sr->addr_len = READ_ONCE(sqe->addr_len);
	} else if (sqe->addr2 || sqe->file_index) {
		return -EINVAL;
	}

	sr->umsg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sr->len = READ_ONCE(sqe->len);
	sr->flags = READ_ONCE(sqe->ioprio);
	if (sr->flags & ~IORING_RECVSEND_POLL_FIRST)
		return -EINVAL;
	sr->msg_flags = READ_ONCE(sqe->msg_flags) | MSG_NOSIGNAL;
	if (sr->msg_flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		sr->msg_flags |= MSG_CMSG_COMPAT;
#endif
	sr->done_io = 0;
	return 0;
}

int io_sendmsg(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_async_msghdr iomsg, *kmsg;
	struct socket *sock;
	unsigned flags;
	int min_ret = 0;
	int ret;

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;

	if (req_has_async_data(req)) {
		kmsg = req->async_data;
		kmsg->msg.msg_control_user = sr->msg_control;
	} else {
		ret = io_sendmsg_copy_hdr(req, &iomsg);
		if (ret)
			return ret;
		kmsg = &iomsg;
	}

	if (!(req->flags & REQ_F_POLLED) &&
	    (sr->flags & IORING_RECVSEND_POLL_FIRST))
		return io_setup_async_msg(req, kmsg, issue_flags);

	flags = sr->msg_flags;
	if (issue_flags & IO_URING_F_NONBLOCK)
		flags |= MSG_DONTWAIT;
	if (flags & MSG_WAITALL)
		min_ret = iov_iter_count(&kmsg->msg.msg_iter);

	ret = __sys_sendmsg_sock(sock, &kmsg->msg, flags);

	if (ret < min_ret) {
		if (ret == -EAGAIN && (issue_flags & IO_URING_F_NONBLOCK))
			return io_setup_async_msg(req, kmsg, issue_flags);
		if (ret > 0 && io_net_retry(sock, flags)) {
			kmsg->msg.msg_controllen = 0;
			kmsg->msg.msg_control = NULL;
			sr->done_io += ret;
			req->flags |= REQ_F_PARTIAL_IO;
			return io_setup_async_msg(req, kmsg, issue_flags);
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	}
	/* fast path, check for non-NULL to avoid function call */
	if (kmsg->free_iov)
		kfree(kmsg->free_iov);
	req->flags &= ~REQ_F_NEED_CLEANUP;
	io_netmsg_recycle(req, issue_flags);
	if (ret >= 0)
		ret += sr->done_io;
	else if (sr->done_io)
		ret = sr->done_io;
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

int io_send(struct io_kiocb *req, unsigned int issue_flags)
{
	struct sockaddr_storage __address;
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct msghdr msg;
	struct iovec iov;
	struct socket *sock;
	unsigned flags;
	int min_ret = 0;
	int ret;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	msg.msg_ubuf = NULL;

	if (sr->addr) {
		if (req_has_async_data(req)) {
			struct io_async_msghdr *io = req->async_data;

			msg.msg_name = &io->addr;
		} else {
			ret = move_addr_to_kernel(sr->addr, sr->addr_len, &__address);
			if (unlikely(ret < 0))
				return ret;
			msg.msg_name = (struct sockaddr *)&__address;
		}
		msg.msg_namelen = sr->addr_len;
	}

	if (!(req->flags & REQ_F_POLLED) &&
	    (sr->flags & IORING_RECVSEND_POLL_FIRST))
		return io_setup_async_addr(req, &__address, issue_flags);

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;

	ret = import_single_range(ITER_SOURCE, sr->buf, sr->len, &iov, &msg.msg_iter);
	if (unlikely(ret))
		return ret;

	flags = sr->msg_flags;
	if (issue_flags & IO_URING_F_NONBLOCK)
		flags |= MSG_DONTWAIT;
	if (flags & MSG_WAITALL)
		min_ret = iov_iter_count(&msg.msg_iter);

	flags &= ~MSG_INTERNAL_SENDMSG_FLAGS;
	msg.msg_flags = flags;
	ret = sock_sendmsg(sock, &msg);
	if (ret < min_ret) {
		if (ret == -EAGAIN && (issue_flags & IO_URING_F_NONBLOCK))
			return io_setup_async_addr(req, &__address, issue_flags);

		if (ret > 0 && io_net_retry(sock, flags)) {
			sr->len -= ret;
			sr->buf += ret;
			sr->done_io += ret;
			req->flags |= REQ_F_PARTIAL_IO;
			return io_setup_async_addr(req, &__address, issue_flags);
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	}
	if (ret >= 0)
		ret += sr->done_io;
	else if (sr->done_io)
		ret = sr->done_io;
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

static int io_recvmsg_mshot_prep(struct io_kiocb *req,
				 struct io_async_msghdr *iomsg,
				 int namelen, size_t controllen)
{
	if ((req->flags & (REQ_F_APOLL_MULTISHOT|REQ_F_BUFFER_SELECT)) ==
			  (REQ_F_APOLL_MULTISHOT|REQ_F_BUFFER_SELECT)) {
		int hdr;

		if (unlikely(namelen < 0))
			return -EOVERFLOW;
		if (check_add_overflow(sizeof(struct io_uring_recvmsg_out),
					namelen, &hdr))
			return -EOVERFLOW;
		if (check_add_overflow(hdr, controllen, &hdr))
			return -EOVERFLOW;

		iomsg->namelen = namelen;
		iomsg->controllen = controllen;
		return 0;
	}

	return 0;
}

static int io_recvmsg_copy_hdr(struct io_kiocb *req,
			       struct io_async_msghdr *iomsg)
{
	struct user_msghdr msg;
	int ret;

	iomsg->msg.msg_name = &iomsg->addr;
	iomsg->msg.msg_iter.nr_segs = 0;

#ifdef CONFIG_COMPAT
	if (unlikely(req->ctx->compat)) {
		struct compat_msghdr cmsg;

		ret = io_compat_msg_copy_hdr(req, iomsg, &cmsg, ITER_DEST);
		if (unlikely(ret))
			return ret;

		ret = __get_compat_msghdr(&iomsg->msg, &cmsg, &iomsg->uaddr);
		if (unlikely(ret))
			return ret;

		return io_recvmsg_mshot_prep(req, iomsg, cmsg.msg_namelen,
						cmsg.msg_controllen);
	}
#endif

	ret = io_msg_copy_hdr(req, iomsg, &msg, ITER_DEST);
	if (unlikely(ret))
		return ret;

	ret = __copy_msghdr(&iomsg->msg, &msg, &iomsg->uaddr);
	if (unlikely(ret))
		return ret;

	return io_recvmsg_mshot_prep(req, iomsg, msg.msg_namelen,
					msg.msg_controllen);
}

int io_recvmsg_prep_async(struct io_kiocb *req)
{
	struct io_async_msghdr *iomsg;
	int ret;

	if (!io_msg_alloc_async_prep(req))
		return -ENOMEM;
	iomsg = req->async_data;
	ret = io_recvmsg_copy_hdr(req, iomsg);
	if (!ret)
		req->flags |= REQ_F_NEED_CLEANUP;
	return ret;
}

#define RECVMSG_FLAGS (IORING_RECVSEND_POLL_FIRST | IORING_RECV_MULTISHOT)

int io_recvmsg_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);

	if (unlikely(sqe->file_index || sqe->addr2))
		return -EINVAL;

	sr->umsg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	sr->len = READ_ONCE(sqe->len);
	sr->flags = READ_ONCE(sqe->ioprio);
	if (sr->flags & ~(RECVMSG_FLAGS))
		return -EINVAL;
	sr->msg_flags = READ_ONCE(sqe->msg_flags);
	if (sr->msg_flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;
	if (sr->msg_flags & MSG_ERRQUEUE)
		req->flags |= REQ_F_CLEAR_POLLIN;
	if (sr->flags & IORING_RECV_MULTISHOT) {
		if (!(req->flags & REQ_F_BUFFER_SELECT))
			return -EINVAL;
		if (sr->msg_flags & MSG_WAITALL)
			return -EINVAL;
		if (req->opcode == IORING_OP_RECV && sr->len)
			return -EINVAL;
		req->flags |= REQ_F_APOLL_MULTISHOT;
		/*
		 * Store the buffer group for this multishot receive separately,
		 * as if we end up doing an io-wq based issue that selects a
		 * buffer, it has to be committed immediately and that will
		 * clear ->buf_list. This means we lose the link to the buffer
		 * list, and the eventual buffer put on completion then cannot
		 * restore it.
		 */
		sr->buf_group = req->buf_index;
	}

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		sr->msg_flags |= MSG_CMSG_COMPAT;
#endif
	sr->done_io = 0;
	return 0;
}

static inline void io_recv_prep_retry(struct io_kiocb *req)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);

	sr->done_io = 0;
	sr->len = 0; /* get from the provided buffer */
	req->buf_index = sr->buf_group;
}

/*
 * Finishes io_recv and io_recvmsg.
 *
 * Returns true if it is actually finished, or false if it should run
 * again (for multishot).
 */
static inline bool io_recv_finish(struct io_kiocb *req, int *ret,
				  unsigned int cflags, bool mshot_finished,
				  unsigned issue_flags)
{
	if (!(req->flags & REQ_F_APOLL_MULTISHOT)) {
		io_req_set_res(req, *ret, cflags);
		*ret = IOU_OK;
		return true;
	}

	if (!mshot_finished) {
		if (io_post_aux_cqe(req->ctx, req->cqe.user_data, *ret,
				    cflags | IORING_CQE_F_MORE, false)) {
			io_recv_prep_retry(req);
			return false;
		}
		/*
		 * Otherwise stop multishot but use the current result.
		 * Probably will end up going into overflow, but this means
		 * we cannot trust the ordering anymore
		 */
	}

	io_req_set_res(req, *ret, cflags);

	if (issue_flags & IO_URING_F_MULTISHOT)
		*ret = IOU_STOP_MULTISHOT;
	else
		*ret = IOU_OK;
	return true;
}

static int io_recvmsg_prep_multishot(struct io_async_msghdr *kmsg,
				     struct io_sr_msg *sr, void __user **buf,
				     size_t *len)
{
	unsigned long ubuf = (unsigned long) *buf;
	unsigned long hdr;

	hdr = sizeof(struct io_uring_recvmsg_out) + kmsg->namelen +
		kmsg->controllen;
	if (*len < hdr)
		return -EFAULT;

	if (kmsg->controllen) {
		unsigned long control = ubuf + hdr - kmsg->controllen;

		kmsg->msg.msg_control_user = (void __user *) control;
		kmsg->msg.msg_controllen = kmsg->controllen;
	}

	sr->buf = *buf; /* stash for later copy */
	*buf = (void __user *) (ubuf + hdr);
	kmsg->payloadlen = *len = *len - hdr;
	return 0;
}

struct io_recvmsg_multishot_hdr {
	struct io_uring_recvmsg_out msg;
	struct sockaddr_storage addr;
};

static int io_recvmsg_multishot(struct socket *sock, struct io_sr_msg *io,
				struct io_async_msghdr *kmsg,
				unsigned int flags, bool *finished)
{
	int err;
	int copy_len;
	struct io_recvmsg_multishot_hdr hdr;

	if (kmsg->namelen)
		kmsg->msg.msg_name = &hdr.addr;
	kmsg->msg.msg_flags = flags & (MSG_CMSG_CLOEXEC|MSG_CMSG_COMPAT);
	kmsg->msg.msg_namelen = 0;

	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;

	err = sock_recvmsg(sock, &kmsg->msg, flags);
	*finished = err <= 0;
	if (err < 0)
		return err;

	hdr.msg = (struct io_uring_recvmsg_out) {
		.controllen = kmsg->controllen - kmsg->msg.msg_controllen,
		.flags = kmsg->msg.msg_flags & ~MSG_CMSG_COMPAT
	};

	hdr.msg.payloadlen = err;
	if (err > kmsg->payloadlen)
		err = kmsg->payloadlen;

	copy_len = sizeof(struct io_uring_recvmsg_out);
	if (kmsg->msg.msg_namelen > kmsg->namelen)
		copy_len += kmsg->namelen;
	else
		copy_len += kmsg->msg.msg_namelen;

	/*
	 *      "fromlen shall refer to the value before truncation.."
	 *                      1003.1g
	 */
	hdr.msg.namelen = kmsg->msg.msg_namelen;

	/* ensure that there is no gap between hdr and sockaddr_storage */
	BUILD_BUG_ON(offsetof(struct io_recvmsg_multishot_hdr, addr) !=
		     sizeof(struct io_uring_recvmsg_out));
	if (copy_to_user(io->buf, &hdr, copy_len)) {
		*finished = true;
		return -EFAULT;
	}

	return sizeof(struct io_uring_recvmsg_out) + kmsg->namelen +
			kmsg->controllen + err;
}

int io_recvmsg(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_async_msghdr iomsg, *kmsg;
	struct socket *sock;
	unsigned int cflags;
	unsigned flags;
	int ret, min_ret = 0;
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	bool mshot_finished = true;

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;

	if (req_has_async_data(req)) {
		kmsg = req->async_data;
	} else {
		ret = io_recvmsg_copy_hdr(req, &iomsg);
		if (ret)
			return ret;
		kmsg = &iomsg;
	}

	if (!(req->flags & REQ_F_POLLED) &&
	    (sr->flags & IORING_RECVSEND_POLL_FIRST))
		return io_setup_async_msg(req, kmsg, issue_flags);

retry_multishot:
	if (io_do_buffer_select(req)) {
		void __user *buf;
		size_t len = sr->len;

		buf = io_buffer_select(req, &len, issue_flags);
		if (!buf)
			return -ENOBUFS;

		if (req->flags & REQ_F_APOLL_MULTISHOT) {
			ret = io_recvmsg_prep_multishot(kmsg, sr, &buf, &len);
			if (ret) {
				io_kbuf_recycle(req, issue_flags);
				return ret;
			}
		}

		kmsg->fast_iov[0].iov_base = buf;
		kmsg->fast_iov[0].iov_len = len;
		iov_iter_init(&kmsg->msg.msg_iter, ITER_DEST, kmsg->fast_iov, 1,
				len);
	}

	flags = sr->msg_flags;
	if (force_nonblock)
		flags |= MSG_DONTWAIT;

	kmsg->msg.msg_get_inq = 1;
	if (req->flags & REQ_F_APOLL_MULTISHOT) {
		ret = io_recvmsg_multishot(sock, sr, kmsg, flags,
					   &mshot_finished);
	} else {
		/* disable partial retry for recvmsg with cmsg attached */
		if (flags & MSG_WAITALL && !kmsg->msg.msg_controllen)
			min_ret = iov_iter_count(&kmsg->msg.msg_iter);

		ret = __sys_recvmsg_sock(sock, &kmsg->msg, sr->umsg,
					 kmsg->uaddr, flags);
	}

	if (ret < min_ret) {
		if (ret == -EAGAIN && force_nonblock) {
			ret = io_setup_async_msg(req, kmsg, issue_flags);
			if (ret == -EAGAIN && (issue_flags & IO_URING_F_MULTISHOT)) {
				io_kbuf_recycle(req, issue_flags);
				return IOU_ISSUE_SKIP_COMPLETE;
			}
			return ret;
		}
		if (ret > 0 && io_net_retry(sock, flags)) {
			sr->done_io += ret;
			req->flags |= REQ_F_PARTIAL_IO;
			return io_setup_async_msg(req, kmsg, issue_flags);
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	} else if ((flags & MSG_WAITALL) && (kmsg->msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
		req_set_fail(req);
	}

	if (ret > 0)
		ret += sr->done_io;
	else if (sr->done_io)
		ret = sr->done_io;
	else
		io_kbuf_recycle(req, issue_flags);

	cflags = io_put_kbuf(req, issue_flags);
	if (kmsg->msg.msg_inq)
		cflags |= IORING_CQE_F_SOCK_NONEMPTY;

	if (!io_recv_finish(req, &ret, cflags, mshot_finished, issue_flags))
		goto retry_multishot;

	if (mshot_finished) {
		/* fast path, check for non-NULL to avoid function call */
		if (kmsg->free_iov)
			kfree(kmsg->free_iov);
		io_netmsg_recycle(req, issue_flags);
		req->flags &= ~REQ_F_NEED_CLEANUP;
	} else if (ret == -EAGAIN)
		return io_setup_async_msg(req, kmsg, issue_flags);

	return ret;
}

int io_recv(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct msghdr msg;
	struct socket *sock;
	struct iovec iov;
	unsigned int cflags;
	unsigned flags;
	int ret, min_ret = 0;
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	size_t len = sr->len;

	if (!(req->flags & REQ_F_POLLED) &&
	    (sr->flags & IORING_RECVSEND_POLL_FIRST))
		return -EAGAIN;

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;

retry_multishot:
	if (io_do_buffer_select(req)) {
		void __user *buf;

		buf = io_buffer_select(req, &len, issue_flags);
		if (!buf)
			return -ENOBUFS;
		sr->buf = buf;
		sr->len = len;
	}

	ret = import_single_range(ITER_DEST, sr->buf, len, &iov, &msg.msg_iter);
	if (unlikely(ret))
		goto out_free;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_get_inq = 1;
	msg.msg_flags = 0;
	msg.msg_controllen = 0;
	msg.msg_iocb = NULL;
	msg.msg_ubuf = NULL;

	flags = sr->msg_flags;
	if (force_nonblock)
		flags |= MSG_DONTWAIT;
	if (flags & MSG_WAITALL)
		min_ret = iov_iter_count(&msg.msg_iter);

	ret = sock_recvmsg(sock, &msg, flags);
	if (ret < min_ret) {
		if (ret == -EAGAIN && force_nonblock) {
			if (issue_flags & IO_URING_F_MULTISHOT) {
				io_kbuf_recycle(req, issue_flags);
				return IOU_ISSUE_SKIP_COMPLETE;
			}

			return -EAGAIN;
		}
		if (ret > 0 && io_net_retry(sock, flags)) {
			sr->len -= ret;
			sr->buf += ret;
			sr->done_io += ret;
			req->flags |= REQ_F_PARTIAL_IO;
			return -EAGAIN;
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	} else if ((flags & MSG_WAITALL) && (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
out_free:
		req_set_fail(req);
	}

	if (ret > 0)
		ret += sr->done_io;
	else if (sr->done_io)
		ret = sr->done_io;
	else
		io_kbuf_recycle(req, issue_flags);

	cflags = io_put_kbuf(req, issue_flags);
	if (msg.msg_inq)
		cflags |= IORING_CQE_F_SOCK_NONEMPTY;

	if (!io_recv_finish(req, &ret, cflags, ret <= 0, issue_flags))
		goto retry_multishot;

	return ret;
}

void io_send_zc_cleanup(struct io_kiocb *req)
{
	struct io_sr_msg *zc = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_async_msghdr *io;

	if (req_has_async_data(req)) {
		io = req->async_data;
		/* might be ->fast_iov if *msg_copy_hdr failed */
		if (io->free_iov != io->fast_iov)
			kfree(io->free_iov);
	}
	if (zc->notif) {
		io_notif_flush(zc->notif);
		zc->notif = NULL;
	}
}

int io_send_zc_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_sr_msg *zc = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_ring_ctx *ctx = req->ctx;
	struct io_kiocb *notif;

	if (unlikely(READ_ONCE(sqe->__pad2[0]) || READ_ONCE(sqe->addr3)))
		return -EINVAL;
	/* we don't support IOSQE_CQE_SKIP_SUCCESS just yet */
	if (req->flags & REQ_F_CQE_SKIP)
		return -EINVAL;

	zc->flags = READ_ONCE(sqe->ioprio);
	if (zc->flags & ~(IORING_RECVSEND_POLL_FIRST |
			  IORING_RECVSEND_FIXED_BUF |
			  IORING_SEND_ZC_REPORT_USAGE))
		return -EINVAL;
	notif = zc->notif = io_alloc_notif(ctx);
	if (!notif)
		return -ENOMEM;
	notif->cqe.user_data = req->cqe.user_data;
	notif->cqe.res = 0;
	notif->cqe.flags = IORING_CQE_F_NOTIF;
	req->flags |= REQ_F_NEED_CLEANUP;
	if (zc->flags & IORING_RECVSEND_FIXED_BUF) {
		unsigned idx = READ_ONCE(sqe->buf_index);

		if (unlikely(idx >= ctx->nr_user_bufs))
			return -EFAULT;
		idx = array_index_nospec(idx, ctx->nr_user_bufs);
		req->imu = READ_ONCE(ctx->user_bufs[idx]);
		io_req_set_rsrc_node(notif, ctx, 0);
	}
	if (zc->flags & IORING_SEND_ZC_REPORT_USAGE) {
		io_notif_to_data(notif)->zc_report = true;
	}

	if (req->opcode == IORING_OP_SEND_ZC) {
		if (READ_ONCE(sqe->__pad3[0]))
			return -EINVAL;
		zc->addr = u64_to_user_ptr(READ_ONCE(sqe->addr2));
		zc->addr_len = READ_ONCE(sqe->addr_len);
	} else {
		if (unlikely(sqe->addr2 || sqe->file_index))
			return -EINVAL;
		if (unlikely(zc->flags & IORING_RECVSEND_FIXED_BUF))
			return -EINVAL;
	}

	zc->buf = u64_to_user_ptr(READ_ONCE(sqe->addr));
	zc->len = READ_ONCE(sqe->len);
	zc->msg_flags = READ_ONCE(sqe->msg_flags) | MSG_NOSIGNAL;
	if (zc->msg_flags & MSG_DONTWAIT)
		req->flags |= REQ_F_NOWAIT;

	zc->done_io = 0;

#ifdef CONFIG_COMPAT
	if (req->ctx->compat)
		zc->msg_flags |= MSG_CMSG_COMPAT;
#endif
	return 0;
}

static int io_sg_from_iter_iovec(struct sock *sk, struct sk_buff *skb,
				 struct iov_iter *from, size_t length)
{
	skb_zcopy_downgrade_managed(skb);
	return __zerocopy_sg_from_iter(NULL, sk, skb, from, length);
}

static int io_sg_from_iter(struct sock *sk, struct sk_buff *skb,
			   struct iov_iter *from, size_t length)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	int frag = shinfo->nr_frags;
	int ret = 0;
	struct bvec_iter bi;
	ssize_t copied = 0;
	unsigned long truesize = 0;

	if (!frag)
		shinfo->flags |= SKBFL_MANAGED_FRAG_REFS;
	else if (unlikely(!skb_zcopy_managed(skb)))
		return __zerocopy_sg_from_iter(NULL, sk, skb, from, length);

	bi.bi_size = min(from->count, length);
	bi.bi_bvec_done = from->iov_offset;
	bi.bi_idx = 0;

	while (bi.bi_size && frag < MAX_SKB_FRAGS) {
		struct bio_vec v = mp_bvec_iter_bvec(from->bvec, bi);

		copied += v.bv_len;
		truesize += PAGE_ALIGN(v.bv_len + v.bv_offset);
		__skb_fill_page_desc_noacc(shinfo, frag++, v.bv_page,
					   v.bv_offset, v.bv_len);
		bvec_iter_advance_single(from->bvec, &bi, v.bv_len);
	}
	if (bi.bi_size)
		ret = -EMSGSIZE;

	shinfo->nr_frags = frag;
	from->bvec += bi.bi_idx;
	from->nr_segs -= bi.bi_idx;
	from->count -= copied;
	from->iov_offset = bi.bi_bvec_done;

	skb->data_len += copied;
	skb->len += copied;
	skb->truesize += truesize;

	if (sk && sk->sk_type == SOCK_STREAM) {
		sk_wmem_queued_add(sk, truesize);
		if (!skb_zcopy_pure(skb))
			sk_mem_charge(sk, truesize);
	} else {
		refcount_add(truesize, &skb->sk->sk_wmem_alloc);
	}
	return ret;
}

int io_send_zc(struct io_kiocb *req, unsigned int issue_flags)
{
	struct sockaddr_storage __address;
	struct io_sr_msg *zc = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct msghdr msg;
	struct iovec iov;
	struct socket *sock;
	unsigned msg_flags;
	int ret, min_ret = 0;

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;
	if (!test_bit(SOCK_SUPPORT_ZC, &sock->flags))
		return -EOPNOTSUPP;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;

	if (zc->addr) {
		if (req_has_async_data(req)) {
			struct io_async_msghdr *io = req->async_data;

			msg.msg_name = &io->addr;
		} else {
			ret = move_addr_to_kernel(zc->addr, zc->addr_len, &__address);
			if (unlikely(ret < 0))
				return ret;
			msg.msg_name = (struct sockaddr *)&__address;
		}
		msg.msg_namelen = zc->addr_len;
	}

	if (!(req->flags & REQ_F_POLLED) &&
	    (zc->flags & IORING_RECVSEND_POLL_FIRST))
		return io_setup_async_addr(req, &__address, issue_flags);

	if (zc->flags & IORING_RECVSEND_FIXED_BUF) {
		ret = io_import_fixed(ITER_SOURCE, &msg.msg_iter, req->imu,
					(u64)(uintptr_t)zc->buf, zc->len);
		if (unlikely(ret))
			return ret;
		msg.sg_from_iter = io_sg_from_iter;
	} else {
		ret = import_single_range(ITER_SOURCE, zc->buf, zc->len, &iov,
					  &msg.msg_iter);
		if (unlikely(ret))
			return ret;
		ret = io_notif_account_mem(zc->notif, zc->len);
		if (unlikely(ret))
			return ret;
		msg.sg_from_iter = io_sg_from_iter_iovec;
	}

	msg_flags = zc->msg_flags | MSG_ZEROCOPY;
	if (issue_flags & IO_URING_F_NONBLOCK)
		msg_flags |= MSG_DONTWAIT;
	if (msg_flags & MSG_WAITALL)
		min_ret = iov_iter_count(&msg.msg_iter);
	msg_flags &= ~MSG_INTERNAL_SENDMSG_FLAGS;

	msg.msg_flags = msg_flags;
	msg.msg_ubuf = &io_notif_to_data(zc->notif)->uarg;
	ret = sock_sendmsg(sock, &msg);

	if (unlikely(ret < min_ret)) {
		if (ret == -EAGAIN && (issue_flags & IO_URING_F_NONBLOCK))
			return io_setup_async_addr(req, &__address, issue_flags);

		if (ret > 0 && io_net_retry(sock, msg.msg_flags)) {
			zc->len -= ret;
			zc->buf += ret;
			zc->done_io += ret;
			req->flags |= REQ_F_PARTIAL_IO;
			return io_setup_async_addr(req, &__address, issue_flags);
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	}

	if (ret >= 0)
		ret += zc->done_io;
	else if (zc->done_io)
		ret = zc->done_io;

	/*
	 * If we're in io-wq we can't rely on tw ordering guarantees, defer
	 * flushing notif to io_send_zc_cleanup()
	 */
	if (!(issue_flags & IO_URING_F_UNLOCKED)) {
		io_notif_flush(zc->notif);
		req->flags &= ~REQ_F_NEED_CLEANUP;
	}
	io_req_set_res(req, ret, IORING_CQE_F_MORE);
	return IOU_OK;
}

int io_sendmsg_zc(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);
	struct io_async_msghdr iomsg, *kmsg;
	struct socket *sock;
	unsigned flags;
	int ret, min_ret = 0;

	sock = sock_from_file(req->file);
	if (unlikely(!sock))
		return -ENOTSOCK;
	if (!test_bit(SOCK_SUPPORT_ZC, &sock->flags))
		return -EOPNOTSUPP;

	if (req_has_async_data(req)) {
		kmsg = req->async_data;
		kmsg->msg.msg_control_user = sr->msg_control;
	} else {
		ret = io_sendmsg_copy_hdr(req, &iomsg);
		if (ret)
			return ret;
		kmsg = &iomsg;
	}

	if (!(req->flags & REQ_F_POLLED) &&
	    (sr->flags & IORING_RECVSEND_POLL_FIRST))
		return io_setup_async_msg(req, kmsg, issue_flags);

	flags = sr->msg_flags | MSG_ZEROCOPY;
	if (issue_flags & IO_URING_F_NONBLOCK)
		flags |= MSG_DONTWAIT;
	if (flags & MSG_WAITALL)
		min_ret = iov_iter_count(&kmsg->msg.msg_iter);

	kmsg->msg.msg_ubuf = &io_notif_to_data(sr->notif)->uarg;
	kmsg->msg.sg_from_iter = io_sg_from_iter_iovec;
	ret = __sys_sendmsg_sock(sock, &kmsg->msg, flags);

	if (unlikely(ret < min_ret)) {
		if (ret == -EAGAIN && (issue_flags & IO_URING_F_NONBLOCK))
			return io_setup_async_msg(req, kmsg, issue_flags);

		if (ret > 0 && io_net_retry(sock, flags)) {
			sr->done_io += ret;
			req->flags |= REQ_F_PARTIAL_IO;
			return io_setup_async_msg(req, kmsg, issue_flags);
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	}
	/* fast path, check for non-NULL to avoid function call */
	if (kmsg->free_iov) {
		kfree(kmsg->free_iov);
		kmsg->free_iov = NULL;
	}

	io_netmsg_recycle(req, issue_flags);
	if (ret >= 0)
		ret += sr->done_io;
	else if (sr->done_io)
		ret = sr->done_io;

	/*
	 * If we're in io-wq we can't rely on tw ordering guarantees, defer
	 * flushing notif to io_send_zc_cleanup()
	 */
	if (!(issue_flags & IO_URING_F_UNLOCKED)) {
		io_notif_flush(sr->notif);
		req->flags &= ~REQ_F_NEED_CLEANUP;
	}
	io_req_set_res(req, ret, IORING_CQE_F_MORE);
	return IOU_OK;
}

void io_sendrecv_fail(struct io_kiocb *req)
{
	struct io_sr_msg *sr = io_kiocb_to_cmd(req, struct io_sr_msg);

	if (req->flags & REQ_F_PARTIAL_IO)
		req->cqe.res = sr->done_io;

	if ((req->flags & REQ_F_NEED_CLEANUP) &&
	    (req->opcode == IORING_OP_SEND_ZC || req->opcode == IORING_OP_SENDMSG_ZC))
		req->cqe.flags |= IORING_CQE_F_MORE;
}

int io_accept_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_accept *accept = io_kiocb_to_cmd(req, struct io_accept);
	unsigned flags;

	if (sqe->len || sqe->buf_index)
		return -EINVAL;

	accept->addr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	accept->addr_len = u64_to_user_ptr(READ_ONCE(sqe->addr2));
	accept->flags = READ_ONCE(sqe->accept_flags);
	accept->nofile = rlimit(RLIMIT_NOFILE);
	flags = READ_ONCE(sqe->ioprio);
	if (flags & ~IORING_ACCEPT_MULTISHOT)
		return -EINVAL;

	accept->file_slot = READ_ONCE(sqe->file_index);
	if (accept->file_slot) {
		if (accept->flags & SOCK_CLOEXEC)
			return -EINVAL;
		if (flags & IORING_ACCEPT_MULTISHOT &&
		    accept->file_slot != IORING_FILE_INDEX_ALLOC)
			return -EINVAL;
	}
	if (accept->flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	if (SOCK_NONBLOCK != O_NONBLOCK && (accept->flags & SOCK_NONBLOCK))
		accept->flags = (accept->flags & ~SOCK_NONBLOCK) | O_NONBLOCK;
	if (flags & IORING_ACCEPT_MULTISHOT)
		req->flags |= REQ_F_APOLL_MULTISHOT;
	return 0;
}

int io_accept(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_accept *accept = io_kiocb_to_cmd(req, struct io_accept);
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;
	unsigned int file_flags = force_nonblock ? O_NONBLOCK : 0;
	bool fixed = !!accept->file_slot;
	struct file *file;
	int ret, fd;

retry:
	if (!fixed) {
		fd = __get_unused_fd_flags(accept->flags, accept->nofile);
		if (unlikely(fd < 0))
			return fd;
	}
	file = do_accept(req->file, file_flags, accept->addr, accept->addr_len,
			 accept->flags);
	if (IS_ERR(file)) {
		if (!fixed)
			put_unused_fd(fd);
		ret = PTR_ERR(file);
		if (ret == -EAGAIN && force_nonblock) {
			/*
			 * if it's multishot and polled, we don't need to
			 * return EAGAIN to arm the poll infra since it
			 * has already been done
			 */
			if (issue_flags & IO_URING_F_MULTISHOT)
				return IOU_ISSUE_SKIP_COMPLETE;
			return ret;
		}
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	} else if (!fixed) {
		fd_install(fd, file);
		ret = fd;
	} else {
		ret = io_fixed_fd_install(req, issue_flags, file,
						accept->file_slot);
	}

	if (!(req->flags & REQ_F_APOLL_MULTISHOT)) {
		io_req_set_res(req, ret, 0);
		return IOU_OK;
	}

	if (ret < 0)
		return ret;
	if (io_post_aux_cqe(ctx, req->cqe.user_data, ret, IORING_CQE_F_MORE, false))
		goto retry;

	io_req_set_res(req, ret, 0);
	return IOU_STOP_MULTISHOT;
}

int io_socket_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_socket *sock = io_kiocb_to_cmd(req, struct io_socket);

	if (sqe->addr || sqe->rw_flags || sqe->buf_index)
		return -EINVAL;

	sock->domain = READ_ONCE(sqe->fd);
	sock->type = READ_ONCE(sqe->off);
	sock->protocol = READ_ONCE(sqe->len);
	sock->file_slot = READ_ONCE(sqe->file_index);
	sock->nofile = rlimit(RLIMIT_NOFILE);

	sock->flags = sock->type & ~SOCK_TYPE_MASK;
	if (sock->file_slot && (sock->flags & SOCK_CLOEXEC))
		return -EINVAL;
	if (sock->flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	return 0;
}

int io_socket(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_socket *sock = io_kiocb_to_cmd(req, struct io_socket);
	bool fixed = !!sock->file_slot;
	struct file *file;
	int ret, fd;

	if (!fixed) {
		fd = __get_unused_fd_flags(sock->flags, sock->nofile);
		if (unlikely(fd < 0))
			return fd;
	}
	file = __sys_socket_file(sock->domain, sock->type, sock->protocol);
	if (IS_ERR(file)) {
		if (!fixed)
			put_unused_fd(fd);
		ret = PTR_ERR(file);
		if (ret == -EAGAIN && (issue_flags & IO_URING_F_NONBLOCK))
			return -EAGAIN;
		if (ret == -ERESTARTSYS)
			ret = -EINTR;
		req_set_fail(req);
	} else if (!fixed) {
		fd_install(fd, file);
		ret = fd;
	} else {
		ret = io_fixed_fd_install(req, issue_flags, file,
					    sock->file_slot);
	}
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

int io_connect_prep_async(struct io_kiocb *req)
{
	struct io_async_connect *io = req->async_data;
	struct io_connect *conn = io_kiocb_to_cmd(req, struct io_connect);

	return move_addr_to_kernel(conn->addr, conn->addr_len, &io->address);
}

int io_connect_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_connect *conn = io_kiocb_to_cmd(req, struct io_connect);

	if (sqe->len || sqe->buf_index || sqe->rw_flags || sqe->splice_fd_in)
		return -EINVAL;

	conn->addr = u64_to_user_ptr(READ_ONCE(sqe->addr));
	conn->addr_len =  READ_ONCE(sqe->addr2);
	conn->in_progress = conn->seen_econnaborted = false;
	return 0;
}

int io_connect(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_connect *connect = io_kiocb_to_cmd(req, struct io_connect);
	struct io_async_connect __io, *io;
	unsigned file_flags;
	int ret;
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;

	if (req_has_async_data(req)) {
		io = req->async_data;
	} else {
		ret = move_addr_to_kernel(connect->addr,
						connect->addr_len,
						&__io.address);
		if (ret)
			goto out;
		io = &__io;
	}

	file_flags = force_nonblock ? O_NONBLOCK : 0;

	ret = __sys_connect_file(req->file, &io->address,
					connect->addr_len, file_flags);
	if ((ret == -EAGAIN || ret == -EINPROGRESS || ret == -ECONNABORTED)
	    && force_nonblock) {
		if (ret == -EINPROGRESS) {
			connect->in_progress = true;
		} else if (ret == -ECONNABORTED) {
			if (connect->seen_econnaborted)
				goto out;
			connect->seen_econnaborted = true;
		}
		if (req_has_async_data(req))
			return -EAGAIN;
		if (io_alloc_async_data(req)) {
			ret = -ENOMEM;
			goto out;
		}
		memcpy(req->async_data, &__io, sizeof(__io));
		return -EAGAIN;
	}
	if (connect->in_progress) {
		/*
		 * At least bluetooth will return -EBADFD on a re-connect
		 * attempt, and it's (supposedly) also valid to get -EISCONN
		 * which means the previous result is good. For both of these,
		 * grab the sock_error() and use that for the completion.
		 */
		if (ret == -EBADFD || ret == -EISCONN)
			ret = sock_error(sock_from_file(req->file)->sk);
	}
	if (ret == -ERESTARTSYS)
		ret = -EINTR;
out:
	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

void io_netmsg_cache_free(struct io_cache_entry *entry)
{
	kfree(container_of(entry, struct io_async_msghdr, cache));
}
#endif
