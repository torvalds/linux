// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io_uring.h>
#include <linux/eventpoll.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "epoll.h"

struct io_epoll {
	struct file			*file;
	int				epfd;
	int				op;
	int				fd;
	struct epoll_event		event;
};

struct io_epoll_wait {
	struct file			*file;
	int				maxevents;
	struct epoll_event __user	*events;
};

int io_epoll_ctl_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_epoll *epoll = io_kiocb_to_cmd(req, struct io_epoll);

	if (sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;

	epoll->epfd = READ_ONCE(sqe->fd);
	epoll->op = READ_ONCE(sqe->len);
	epoll->fd = READ_ONCE(sqe->off);

	if (ep_op_has_event(epoll->op)) {
		struct epoll_event __user *ev;

		ev = u64_to_user_ptr(READ_ONCE(sqe->addr));
		if (copy_from_user(&epoll->event, ev, sizeof(*ev)))
			return -EFAULT;
	}

	return 0;
}

int io_epoll_ctl(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_epoll *ie = io_kiocb_to_cmd(req, struct io_epoll);
	int ret;
	bool force_nonblock = issue_flags & IO_URING_F_NONBLOCK;

	ret = do_epoll_ctl(ie->epfd, ie->op, ie->fd, &ie->event, force_nonblock);
	if (force_nonblock && ret == -EAGAIN)
		return -EAGAIN;

	if (ret < 0)
		req_set_fail(req);
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

int io_epoll_wait_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_epoll_wait *iew = io_kiocb_to_cmd(req, struct io_epoll_wait);

	if (sqe->off || sqe->rw_flags || sqe->buf_index || sqe->splice_fd_in)
		return -EINVAL;

	iew->maxevents = READ_ONCE(sqe->len);
	iew->events = u64_to_user_ptr(READ_ONCE(sqe->addr));
	return 0;
}

int io_epoll_wait(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_epoll_wait *iew = io_kiocb_to_cmd(req, struct io_epoll_wait);
	int ret;

	ret = epoll_sendevents(req->file, iew->events, iew->maxevents);
	if (ret == 0)
		return -EAGAIN;
	if (ret < 0)
		req_set_fail(req);

	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}
