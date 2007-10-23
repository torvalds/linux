/*
 * The Guest 9p transport driver
 *
 * This is a trivial pipe-based transport driver based on the lguest console
 * code: we use lguest's DMA mechanism to send bytes out, and register a
 * DMA buffer to receive bytes in.  It is assumed to be present and available
 * from the very beginning of boot.
 *
 * This may be have been done by just instaniating another HVC console,
 * but HVC's blocksize of 16 bytes is annoying and painful to performance.
 *
 * A more efficient transport could be built based on the virtio block driver
 * but it requires some changes in the 9p transport model (which are in
 * progress)
 *
 */
/*
 *  Copyright (C) 2007 Eric Van Hensbergen, IBM Corporation
 *
 *  Based on virtio console driver
 *  Copyright (C) 2006, 2007 Rusty Russell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/in.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/ipv6.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/un.h>
#include <linux/uaccess.h>
#include <linux/inet.h>
#include <linux/idr.h>
#include <linux/file.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/transport.h>
#include <linux/scatterlist.h>
#include <linux/virtio.h>
#include <linux/virtio_9p.h>

/* a single mutex to manage channel initialization and attachment */
static DECLARE_MUTEX(virtio_9p_lock);
/* global which tracks highest initialized channel */
static int chan_index;

/* We keep all per-channel information in a structure.
 * This structure is allocated within the devices dev->mem space.
 * A pointer to the structure will get put in the transport private.
 */
static struct virtio_chan {
	bool initialized;		/* channel is initialized */
	bool inuse;			/* channel is in use */

	struct virtqueue *in_vq, *out_vq;
	struct virtio_device *vdev;

	/* This is our input buffer, and how much data is left in it. */
	unsigned int in_len;
	char *in, *inbuf;

	wait_queue_head_t wq;		/* waitq for buffer */
} channels[MAX_9P_CHAN];

/* How many bytes left in this page. */
static unsigned int rest_of_page(void *data)
{
	return PAGE_SIZE - ((unsigned long)data % PAGE_SIZE);
}

static int p9_virtio_write(struct p9_trans *trans, void *buf, int count)
{
	struct virtio_chan *chan = (struct virtio_chan *) trans->priv;
	struct virtqueue *out_vq = chan->out_vq;
	struct scatterlist sg[1];
	unsigned int len;

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio write (%d)\n", count);

	/* keep it simple - make sure we don't overflow a page */
	if (rest_of_page(buf) < count)
		count = rest_of_page(buf);

	sg_init_one(sg, buf, count);

	/* add_buf wants a token to identify this buffer: we hand it any
	 * non-NULL pointer, since there's only ever one buffer. */
	if (out_vq->vq_ops->add_buf(out_vq, sg, 1, 0, (void *)1) == 0) {
		/* Tell Host to go! */
		out_vq->vq_ops->kick(out_vq);
		/* Chill out until it's done with the buffer. */
		while (!out_vq->vq_ops->get_buf(out_vq, &len))
			cpu_relax();
	}

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio wrote (%d)\n", count);

	/* We're expected to return the amount of data we wrote: all of it. */
	return count;
}

/* Create a scatter-gather list representing our input buffer and put it in the
 * queue. */
static void add_inbuf(struct virtio_chan *chan)
{
	struct scatterlist sg[1];

	sg_init_one(sg, chan->inbuf, PAGE_SIZE);

	/* We should always be able to add one buffer to an empty queue. */
	if (chan->in_vq->vq_ops->add_buf(chan->in_vq, sg, 0, 1, chan->inbuf))
		BUG();
	chan->in_vq->vq_ops->kick(chan->in_vq);
}

static int p9_virtio_read(struct p9_trans *trans, void *buf, int count)
{
	struct virtio_chan *chan = (struct virtio_chan *) trans->priv;
	struct virtqueue *in_vq = chan->in_vq;

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio read (%d)\n", count);

	/* If we don't have an input queue yet, we can't get input. */
	BUG_ON(!in_vq);

	/* No buffer?  Try to get one. */
	if (!chan->in_len) {
		chan->in = in_vq->vq_ops->get_buf(in_vq, &chan->in_len);
		if (!chan->in)
			return 0;
	}

	/* You want more than we have to give?  Well, try wanting less! */
	if (chan->in_len < count)
		count = chan->in_len;

	/* Copy across to their buffer and increment offset. */
	memcpy(buf, chan->in, count);
	chan->in += count;
	chan->in_len -= count;

	/* Finished?  Re-register buffer so Host will use it again. */
	if (chan->in_len == 0)
		add_inbuf(chan);

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio finished read (%d)\n",
									count);

	return count;
}

/* The poll function is used by 9p transports to determine if there
 * is there is activity available on a particular channel.  In our case
 * we use it to wait for a callback from the input routines.
 */
static unsigned int
p9_virtio_poll(struct p9_trans *trans, struct poll_table_struct *pt)
{
	struct virtio_chan *chan = (struct virtio_chan *)trans->priv;
	struct virtqueue *in_vq = chan->in_vq;
	int ret = POLLOUT; /* we can always handle more output */

	poll_wait(NULL, &chan->wq, pt);

	/* No buffer?  Try to get one. */
	if (!chan->in_len)
		chan->in = in_vq->vq_ops->get_buf(in_vq, &chan->in_len);

	if (chan->in_len)
		ret |= POLLIN;

	return ret;
}

static void p9_virtio_close(struct p9_trans *trans)
{
	struct virtio_chan *chan = trans->priv;

	down(&virtio_9p_lock);
	chan->inuse = false;
	up(&virtio_9p_lock);

	kfree(trans);
}

static bool p9_virtio_intr(struct virtqueue *q)
{
	struct virtio_chan *chan = q->vdev->priv;

	P9_DPRINTK(P9_DEBUG_TRANS, "9p poll_wakeup: %p\n", &chan->wq);
	wake_up_interruptible(&chan->wq);

	return true;
}

static int p9_virtio_probe(struct virtio_device *dev)
{
	int err;
	struct virtio_chan *chan;
	int index;

	down(&virtio_9p_lock);
	index = chan_index++;
	chan = &channels[index];
	up(&virtio_9p_lock);

	if (chan_index > MAX_9P_CHAN) {
		printk(KERN_ERR "9p: virtio: Maximum channels exceeded\n");
		BUG();
	}

	chan->vdev = dev;

	/* This is the scratch page we use to receive console input */
	chan->inbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!chan->inbuf) {
		err = -ENOMEM;
		goto fail;
	}

	/* Find the input queue. */
	dev->priv = chan;
	chan->in_vq = dev->config->find_vq(dev, p9_virtio_intr);
	if (IS_ERR(chan->in_vq)) {
		err = PTR_ERR(chan->in_vq);
		goto free;
	}

	chan->out_vq = dev->config->find_vq(dev, NULL);
	if (IS_ERR(chan->out_vq)) {
		err = PTR_ERR(chan->out_vq);
		goto free_in_vq;
	}

	init_waitqueue_head(&chan->wq);

	/* Register the input buffer the first time. */
	add_inbuf(chan);
	chan->inuse = false;
	chan->initialized = true;

	return 0;

free_in_vq:
	dev->config->del_vq(chan->in_vq);
free:
	kfree(chan->inbuf);
fail:
	down(&virtio_9p_lock);
	chan_index--;
	up(&virtio_9p_lock);
	return err;
}

/* This sets up a transport channel for 9p communication.  Right now
 * we only match the first available channel, but eventually we couldlook up
 * alternate channels by matching devname versus a virtio_config entry.
 * We use a simple reference count mechanism to ensure that only a single
 * mount has a channel open at a time. */
static struct p9_trans *p9_virtio_create(const char *devname, char *args)
{
	struct p9_trans *trans;
	int index = 0;
	struct virtio_chan *chan = channels;

	down(&virtio_9p_lock);
	while (index < MAX_9P_CHAN) {
		if (chan->initialized && !chan->inuse) {
			chan->inuse = true;
			break;
		} else {
			index++;
			chan = &channels[index];
		}
	}
	up(&virtio_9p_lock);

	if (index >= MAX_9P_CHAN) {
		printk(KERN_ERR "9p: virtio: couldn't find a free channel\n");
		return NULL;
	}

	trans = kmalloc(sizeof(struct p9_trans), GFP_KERNEL);
	if (!trans) {
		printk(KERN_ERR "9p: couldn't allocate transport\n");
		return ERR_PTR(-ENOMEM);
	}

	trans->write = p9_virtio_write;
	trans->read = p9_virtio_read;
	trans->close = p9_virtio_close;
	trans->poll = p9_virtio_poll;
	trans->priv = chan;

	return trans;
}

#define VIRTIO_ID_9P 9

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_9P, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* The standard "struct lguest_driver": */
static struct virtio_driver p9_virtio_drv = {
	.driver.name = 	KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table =	id_table,
	.probe = 	p9_virtio_probe,
};

static struct p9_trans_module p9_virtio_trans = {
	.name = "virtio",
	.create = p9_virtio_create,
	.maxsize = PAGE_SIZE,
	.def = 0,
};

/* The standard init function */
static int __init p9_virtio_init(void)
{
	int count;

	for (count = 0; count < MAX_9P_CHAN; count++)
		channels[count].initialized = false;

	v9fs_register_trans(&p9_virtio_trans);
	return register_virtio_driver(&p9_virtio_drv);
}

module_init(p9_virtio_init);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_DESCRIPTION("Virtio 9p Transport");
MODULE_LICENSE("GPL");
