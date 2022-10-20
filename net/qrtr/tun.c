// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Linaro Ltd */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>

#include "qrtr.h"

struct qrtr_tun {
	struct qrtr_endpoint ep;

	struct sk_buff_head queue;
	wait_queue_head_t readq;
};

static int qrtr_tun_send(struct qrtr_endpoint *ep, struct sk_buff *skb)
{
	struct qrtr_tun *tun = container_of(ep, struct qrtr_tun, ep);

	skb_queue_tail(&tun->queue, skb);

	/* wake up any blocking processes, waiting for new data */
	wake_up_interruptible(&tun->readq);

	return 0;
}

static int qrtr_tun_open(struct inode *inode, struct file *filp)
{
	struct qrtr_tun *tun;
	int ret;

	tun = kzalloc(sizeof(*tun), GFP_KERNEL);
	if (!tun)
		return -ENOMEM;

	skb_queue_head_init(&tun->queue);
	init_waitqueue_head(&tun->readq);

	tun->ep.xmit = qrtr_tun_send;

	filp->private_data = tun;

	ret = qrtr_endpoint_register(&tun->ep, QRTR_EP_NET_ID_AUTO, 0);
	if (ret)
		goto out;

	return 0;

out:
	filp->private_data = NULL;
	kfree(tun);
	return ret;
}

static ssize_t qrtr_tun_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *filp = iocb->ki_filp;
	struct qrtr_tun *tun = filp->private_data;
	struct sk_buff *skb;
	int count;

	while (!(skb = skb_dequeue(&tun->queue))) {
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Wait until we get data or the endpoint goes away */
		if (wait_event_interruptible(tun->readq,
					     !skb_queue_empty(&tun->queue)))
			return -ERESTARTSYS;
	}

	count = min_t(size_t, iov_iter_count(to), skb->len);
	if (copy_to_iter(skb->data, count, to) != count)
		count = -EFAULT;

	kfree_skb(skb);

	return count;
}

static ssize_t qrtr_tun_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *filp = iocb->ki_filp;
	struct qrtr_tun *tun = filp->private_data;
	size_t len = iov_iter_count(from);
	ssize_t ret;
	void *kbuf;

	if (!len)
		return -EINVAL;

	if (len > KMALLOC_MAX_SIZE)
		return -ENOMEM;

	kbuf = kzalloc(len, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (!copy_from_iter_full(kbuf, len, from)) {
		kfree(kbuf);
		return -EFAULT;
	}

	ret = qrtr_endpoint_post(&tun->ep, kbuf, len);

	kfree(kbuf);
	return ret < 0 ? ret : len;
}

static __poll_t qrtr_tun_poll(struct file *filp, poll_table *wait)
{
	struct qrtr_tun *tun = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &tun->readq, wait);

	if (!skb_queue_empty(&tun->queue))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static int qrtr_tun_release(struct inode *inode, struct file *filp)
{
	struct qrtr_tun *tun = filp->private_data;

	qrtr_endpoint_unregister(&tun->ep);

	/* Discard all SKBs */
	skb_queue_purge(&tun->queue);

	kfree(tun);

	return 0;
}

static const struct file_operations qrtr_tun_ops = {
	.owner = THIS_MODULE,
	.open = qrtr_tun_open,
	.poll = qrtr_tun_poll,
	.read_iter = qrtr_tun_read_iter,
	.write_iter = qrtr_tun_write_iter,
	.release = qrtr_tun_release,
};

static struct miscdevice qrtr_tun_miscdev = {
	MISC_DYNAMIC_MINOR,
	"qrtr-tun",
	&qrtr_tun_ops,
};

static int __init qrtr_tun_init(void)
{
	int ret;

	ret = misc_register(&qrtr_tun_miscdev);
	if (ret)
		pr_err("failed to register Qualcomm IPC Router tun device\n");

	return ret;
}

static void __exit qrtr_tun_exit(void)
{
	misc_deregister(&qrtr_tun_miscdev);
}

module_init(qrtr_tun_init);
module_exit(qrtr_tun_exit);

MODULE_DESCRIPTION("Qualcomm IPC Router TUN device");
MODULE_LICENSE("GPL v2");
