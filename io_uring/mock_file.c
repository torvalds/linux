#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/anon_inodes.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/poll.h>

#include <linux/io_uring/cmd.h>
#include <linux/io_uring_types.h>
#include <uapi/linux/io_uring/mock_file.h>

struct io_mock_iocb {
	struct kiocb		*iocb;
	struct hrtimer		timer;
	int			res;
};

struct io_mock_file {
	size_t			size;
	u64			rw_delay_ns;
	bool			pollable;
	struct wait_queue_head	poll_wq;
};

#define IO_VALID_COPY_CMD_FLAGS		IORING_MOCK_COPY_FROM

static int io_copy_regbuf(struct iov_iter *reg_iter, void __user *ubuf)
{
	size_t ret, copied = 0;
	size_t buflen = PAGE_SIZE;
	void *tmp_buf;

	tmp_buf = kzalloc(buflen, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	while (iov_iter_count(reg_iter)) {
		size_t len = min(iov_iter_count(reg_iter), buflen);

		if (iov_iter_rw(reg_iter) == ITER_SOURCE) {
			ret = copy_from_iter(tmp_buf, len, reg_iter);
			if (ret <= 0)
				break;
			if (copy_to_user(ubuf, tmp_buf, ret))
				break;
		} else {
			if (copy_from_user(tmp_buf, ubuf, len))
				break;
			ret = copy_to_iter(tmp_buf, len, reg_iter);
			if (ret <= 0)
				break;
		}
		ubuf += ret;
		copied += ret;
	}

	kfree(tmp_buf);
	return copied;
}

static int io_cmd_copy_regbuf(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	const struct io_uring_sqe *sqe = cmd->sqe;
	const struct iovec __user *iovec;
	unsigned flags, iovec_len;
	struct iov_iter iter;
	void __user *ubuf;
	int dir, ret;

	ubuf = u64_to_user_ptr(READ_ONCE(sqe->addr3));
	iovec = u64_to_user_ptr(READ_ONCE(sqe->addr));
	iovec_len = READ_ONCE(sqe->len);
	flags = READ_ONCE(sqe->file_index);

	if (unlikely(sqe->ioprio || sqe->__pad1))
		return -EINVAL;
	if (flags & ~IO_VALID_COPY_CMD_FLAGS)
		return -EINVAL;

	dir = (flags & IORING_MOCK_COPY_FROM) ? ITER_SOURCE : ITER_DEST;
	ret = io_uring_cmd_import_fixed_vec(cmd, iovec, iovec_len, dir, &iter,
					    issue_flags);
	if (ret)
		return ret;
	ret = io_copy_regbuf(&iter, ubuf);
	return ret ? ret : -EFAULT;
}

static int io_mock_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	switch (cmd->cmd_op) {
	case IORING_MOCK_CMD_COPY_REGBUF:
		return io_cmd_copy_regbuf(cmd, issue_flags);
	}
	return -ENOTSUPP;
}

static enum hrtimer_restart io_mock_rw_timer_expired(struct hrtimer *timer)
{
	struct io_mock_iocb *mio = container_of(timer, struct io_mock_iocb, timer);
	struct kiocb *iocb = mio->iocb;

	WRITE_ONCE(iocb->private, NULL);
	iocb->ki_complete(iocb, mio->res);
	kfree(mio);
	return HRTIMER_NORESTART;
}

static ssize_t io_mock_delay_rw(struct kiocb *iocb, size_t len)
{
	struct io_mock_file *mf = iocb->ki_filp->private_data;
	struct io_mock_iocb *mio;

	mio = kzalloc(sizeof(*mio), GFP_KERNEL);
	if (!mio)
		return -ENOMEM;

	mio->iocb = iocb;
	mio->res = len;
	hrtimer_setup(&mio->timer, io_mock_rw_timer_expired,
		      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_start(&mio->timer, ns_to_ktime(mf->rw_delay_ns),
		      HRTIMER_MODE_REL);
	return -EIOCBQUEUED;
}

static ssize_t io_mock_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct io_mock_file *mf = iocb->ki_filp->private_data;
	size_t len = iov_iter_count(to);
	size_t nr_zeroed;

	if (iocb->ki_pos + len > mf->size)
		return -EINVAL;
	nr_zeroed = iov_iter_zero(len, to);
	if (!mf->rw_delay_ns || nr_zeroed != len)
		return nr_zeroed;

	return io_mock_delay_rw(iocb, len);
}

static ssize_t io_mock_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct io_mock_file *mf = iocb->ki_filp->private_data;
	size_t len = iov_iter_count(from);

	if (iocb->ki_pos + len > mf->size)
		return -EINVAL;
	if (!mf->rw_delay_ns) {
		iov_iter_advance(from, len);
		return len;
	}

	return io_mock_delay_rw(iocb, len);
}

static loff_t io_mock_llseek(struct file *file, loff_t offset, int whence)
{
	struct io_mock_file *mf = file->private_data;

	return fixed_size_llseek(file, offset, whence, mf->size);
}

static __poll_t io_mock_poll(struct file *file, struct poll_table_struct *pt)
{
	struct io_mock_file *mf = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &mf->poll_wq, pt);

	mask |= EPOLLOUT | EPOLLWRNORM;
	mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static int io_mock_release(struct inode *inode, struct file *file)
{
	struct io_mock_file *mf = file->private_data;

	kfree(mf);
	return 0;
}

static const struct file_operations io_mock_fops = {
	.owner		= THIS_MODULE,
	.release	= io_mock_release,
	.uring_cmd	= io_mock_cmd,
	.read_iter	= io_mock_read_iter,
	.write_iter	= io_mock_write_iter,
	.llseek		= io_mock_llseek,
};

static const struct file_operations io_mock_poll_fops = {
	.owner		= THIS_MODULE,
	.release	= io_mock_release,
	.uring_cmd	= io_mock_cmd,
	.read_iter	= io_mock_read_iter,
	.write_iter	= io_mock_write_iter,
	.llseek		= io_mock_llseek,
	.poll		= io_mock_poll,
};

#define IO_VALID_CREATE_FLAGS (IORING_MOCK_CREATE_F_SUPPORT_NOWAIT | \
				IORING_MOCK_CREATE_F_POLL)

static int io_create_mock_file(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	const struct file_operations *fops = &io_mock_fops;
	const struct io_uring_sqe *sqe = cmd->sqe;
	struct io_uring_mock_create mc, __user *uarg;
	struct io_mock_file *mf = NULL;
	struct file *file = NULL;
	size_t uarg_size;
	int fd = -1, ret;

	/*
	 * It's a testing only driver that allows exercising edge cases
	 * that wouldn't be possible to hit otherwise.
	 */
	add_taint(TAINT_TEST, LOCKDEP_STILL_OK);

	uarg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	uarg_size = READ_ONCE(sqe->len);

	if (sqe->ioprio || sqe->__pad1 || sqe->addr3 || sqe->file_index)
		return -EINVAL;
	if (uarg_size != sizeof(mc))
		return -EINVAL;

	memset(&mc, 0, sizeof(mc));
	if (copy_from_user(&mc, uarg, uarg_size))
		return -EFAULT;
	if (!mem_is_zero(mc.__resv, sizeof(mc.__resv)))
		return -EINVAL;
	if (mc.flags & ~IO_VALID_CREATE_FLAGS)
		return -EINVAL;
	if (mc.file_size > SZ_1G)
		return -EINVAL;
	if (mc.rw_delay_ns > NSEC_PER_SEC)
		return -EINVAL;

	mf = kzalloc(sizeof(*mf), GFP_KERNEL_ACCOUNT);
	if (!mf)
		return -ENOMEM;

	ret = fd = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (fd < 0)
		goto fail;

	init_waitqueue_head(&mf->poll_wq);
	mf->size = mc.file_size;
	mf->rw_delay_ns = mc.rw_delay_ns;
	if (mc.flags & IORING_MOCK_CREATE_F_POLL) {
		fops = &io_mock_poll_fops;
		mf->pollable = true;
	}

	file = anon_inode_create_getfile("[io_uring_mock]", fops,
					 mf, O_RDWR | O_CLOEXEC, NULL);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail;
	}

	file->f_mode |= FMODE_READ | FMODE_CAN_READ |
			FMODE_WRITE | FMODE_CAN_WRITE |
			FMODE_LSEEK;
	if (mc.flags & IORING_MOCK_CREATE_F_SUPPORT_NOWAIT)
		file->f_mode |= FMODE_NOWAIT;

	mc.out_fd = fd;
	if (copy_to_user(uarg, &mc, uarg_size)) {
		fput(file);
		ret = -EFAULT;
		goto fail;
	}

	fd_install(fd, file);
	return 0;
fail:
	if (fd >= 0)
		put_unused_fd(fd);
	kfree(mf);
	return ret;
}

static int io_probe_mock(struct io_uring_cmd *cmd)
{
	const struct io_uring_sqe *sqe = cmd->sqe;
	struct io_uring_mock_probe mp, __user *uarg;
	size_t uarg_size;

	uarg = u64_to_user_ptr(READ_ONCE(sqe->addr));
	uarg_size = READ_ONCE(sqe->len);

	if (sqe->ioprio || sqe->__pad1 || sqe->addr3 || sqe->file_index ||
	    uarg_size != sizeof(mp))
		return -EINVAL;

	memset(&mp, 0, sizeof(mp));
	if (copy_from_user(&mp, uarg, uarg_size))
		return -EFAULT;
	if (!mem_is_zero(&mp, sizeof(mp)))
		return -EINVAL;

	mp.features = IORING_MOCK_FEAT_END;

	if (copy_to_user(uarg, &mp, uarg_size))
		return -EFAULT;
	return 0;
}

static int iou_mock_mgr_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd->cmd_op) {
	case IORING_MOCK_MGR_CMD_PROBE:
		return io_probe_mock(cmd);
	case IORING_MOCK_MGR_CMD_CREATE:
		return io_create_mock_file(cmd, issue_flags);
	}
	return -EOPNOTSUPP;
}

static const struct file_operations iou_mock_dev_fops = {
	.owner		= THIS_MODULE,
	.uring_cmd	= iou_mock_mgr_cmd,
};

static struct miscdevice iou_mock_miscdev = {
	.minor			= MISC_DYNAMIC_MINOR,
	.name			= "io_uring_mock",
	.fops			= &iou_mock_dev_fops,
};

static int __init io_mock_init(void)
{
	int ret;

	ret = misc_register(&iou_mock_miscdev);
	if (ret < 0) {
		pr_err("Could not initialize io_uring mock device\n");
		return ret;
	}
	return 0;
}

static void __exit io_mock_exit(void)
{
	misc_deregister(&iou_mock_miscdev);
}

module_init(io_mock_init)
module_exit(io_mock_exit)

MODULE_AUTHOR("Pavel Begunkov <asml.silence@gmail.com>");
MODULE_DESCRIPTION("io_uring mock file");
MODULE_LICENSE("GPL");
