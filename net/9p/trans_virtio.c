/*
 * The Virtio 9p transport driver
 *
 * This is a block based transport driver based on the lguest block driver
 * code.
 *
 *  Copyright (C) 2007, 2008 Eric Van Hensbergen, IBM Corporation
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
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include <linux/scatterlist.h>
#include <linux/swap.h>
#include <linux/virtio.h>
#include <linux/virtio_9p.h>
#include "trans_common.h"

#define VIRTQUEUE_NUM	128

/* a single mutex to manage channel initialization and attachment */
static DEFINE_MUTEX(virtio_9p_lock);
static DECLARE_WAIT_QUEUE_HEAD(vp_wq);
static atomic_t vp_pinned = ATOMIC_INIT(0);

/**
 * struct virtio_chan - per-instance transport information
 * @initialized: whether the channel is initialized
 * @inuse: whether the channel is in use
 * @lock: protects multiple elements within this structure
 * @client: client instance
 * @vdev: virtio dev associated with this channel
 * @vq: virtio queue associated with this channel
 * @sg: scatter gather list which is used to pack a request (protected?)
 *
 * We keep all per-channel information in a structure.
 * This structure is allocated within the devices dev->mem space.
 * A pointer to the structure will get put in the transport private.
 *
 */

struct virtio_chan {
	bool inuse;

	spinlock_t lock;

	struct p9_client *client;
	struct virtio_device *vdev;
	struct virtqueue *vq;
	int ring_bufs_avail;
	wait_queue_head_t *vc_wq;
	/* This is global limit. Since we don't have a global structure,
	 * will be placing it in each channel.
	 */
	int p9_max_pages;
	/* Scatterlist: can be too big for stack. */
	struct scatterlist sg[VIRTQUEUE_NUM];

	int tag_len;
	/*
	 * tag name to identify a mount Non-null terminated
	 */
	char *tag;

	struct list_head chan_list;
};

static struct list_head virtio_chan_list;

/* How many bytes left in this page. */
static unsigned int rest_of_page(void *data)
{
	return PAGE_SIZE - ((unsigned long)data % PAGE_SIZE);
}

/**
 * p9_virtio_close - reclaim resources of a channel
 * @client: client instance
 *
 * This reclaims a channel by freeing its resources and
 * reseting its inuse flag.
 *
 */

static void p9_virtio_close(struct p9_client *client)
{
	struct virtio_chan *chan = client->trans;

	mutex_lock(&virtio_9p_lock);
	if (chan)
		chan->inuse = false;
	mutex_unlock(&virtio_9p_lock);
}

/**
 * req_done - callback which signals activity from the server
 * @vq: virtio queue activity was received on
 *
 * This notifies us that the server has triggered some activity
 * on the virtio channel - most likely a response to request we
 * sent.  Figure out which requests now have responses and wake up
 * those threads.
 *
 * Bugs: could do with some additional sanity checking, but appears to work.
 *
 */

static void req_done(struct virtqueue *vq)
{
	struct virtio_chan *chan = vq->vdev->priv;
	struct p9_fcall *rc;
	unsigned int len;
	struct p9_req_t *req;
	unsigned long flags;

	P9_DPRINTK(P9_DEBUG_TRANS, ": request done\n");

	while (1) {
		spin_lock_irqsave(&chan->lock, flags);
		rc = virtqueue_get_buf(chan->vq, &len);

		if (rc == NULL) {
			spin_unlock_irqrestore(&chan->lock, flags);
			break;
		}

		chan->ring_bufs_avail = 1;
		spin_unlock_irqrestore(&chan->lock, flags);
		/* Wakeup if anyone waiting for VirtIO ring space. */
		wake_up(chan->vc_wq);
		P9_DPRINTK(P9_DEBUG_TRANS, ": rc %p\n", rc);
		P9_DPRINTK(P9_DEBUG_TRANS, ": lookup tag %d\n", rc->tag);
		req = p9_tag_lookup(chan->client, rc->tag);
		if (req->tc->private) {
			struct trans_rpage_info *rp = req->tc->private;
			int p = rp->rp_nr_pages;
			/*Release pages */
			p9_release_req_pages(rp);
			atomic_sub(p, &vp_pinned);
			wake_up(&vp_wq);
			if (rp->rp_alloc)
				kfree(rp);
			req->tc->private = NULL;
		}
		req->status = REQ_STATUS_RCVD;
		p9_client_cb(chan->client, req);
	}
}

/**
 * pack_sg_list - pack a scatter gather list from a linear buffer
 * @sg: scatter/gather list to pack into
 * @start: which segment of the sg_list to start at
 * @limit: maximum segment to pack data to
 * @data: data to pack into scatter/gather list
 * @count: amount of data to pack into the scatter/gather list
 *
 * sg_lists have multiple segments of various sizes.  This will pack
 * arbitrary data into an existing scatter gather list, segmenting the
 * data as necessary within constraints.
 *
 */

static int
pack_sg_list(struct scatterlist *sg, int start, int limit, char *data,
								int count)
{
	int s;
	int index = start;

	while (count) {
		s = rest_of_page(data);
		if (s > count)
			s = count;
		sg_set_buf(&sg[index++], data, s);
		count -= s;
		data += s;
		BUG_ON(index > limit);
	}

	return index-start;
}

/* We don't currently allow canceling of virtio requests */
static int p9_virtio_cancel(struct p9_client *client, struct p9_req_t *req)
{
	return 1;
}

/**
 * pack_sg_list_p - Just like pack_sg_list. Instead of taking a buffer,
 * this takes a list of pages.
 * @sg: scatter/gather list to pack into
 * @start: which segment of the sg_list to start at
 * @pdata_off: Offset into the first page
 * @**pdata: a list of pages to add into sg.
 * @count: amount of data to pack into the scatter/gather list
 */
static int
pack_sg_list_p(struct scatterlist *sg, int start, int limit, size_t pdata_off,
		struct page **pdata, int count)
{
	int s;
	int i = 0;
	int index = start;

	if (pdata_off) {
		s = min((int)(PAGE_SIZE - pdata_off), count);
		sg_set_page(&sg[index++], pdata[i++], s, pdata_off);
		count -= s;
	}

	while (count) {
		BUG_ON(index > limit);
		s = min((int)PAGE_SIZE, count);
		sg_set_page(&sg[index++], pdata[i++], s, 0);
		count -= s;
	}
	return index-start;
}

/**
 * p9_virtio_request - issue a request
 * @client: client instance issuing the request
 * @req: request to be issued
 *
 */

static int
p9_virtio_request(struct p9_client *client, struct p9_req_t *req)
{
	int in, out, inp, outp;
	struct virtio_chan *chan = client->trans;
	unsigned long flags;
	size_t pdata_off = 0;
	struct trans_rpage_info *rpinfo = NULL;
	int err, pdata_len = 0;

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio request\n");

	req->status = REQ_STATUS_SENT;

	if (req->tc->pbuf_size && (req->tc->pubuf && P9_IS_USER_CONTEXT)) {
		int nr_pages = p9_nr_pages(req);
		int rpinfo_size = sizeof(struct trans_rpage_info) +
			sizeof(struct page *) * nr_pages;

		if (atomic_read(&vp_pinned) >= chan->p9_max_pages) {
			err = wait_event_interruptible(vp_wq,
				atomic_read(&vp_pinned) < chan->p9_max_pages);
			if (err  == -ERESTARTSYS)
				return err;
			P9_DPRINTK(P9_DEBUG_TRANS, "9p: May gup pages now.\n");
		}

		if (rpinfo_size <= (req->tc->capacity - req->tc->size)) {
			/* We can use sdata */
			req->tc->private = req->tc->sdata + req->tc->size;
			rpinfo = (struct trans_rpage_info *)req->tc->private;
			rpinfo->rp_alloc = 0;
		} else {
			req->tc->private = kmalloc(rpinfo_size, GFP_NOFS);
			if (!req->tc->private) {
				P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: "
					"private kmalloc returned NULL");
				return -ENOMEM;
			}
			rpinfo = (struct trans_rpage_info *)req->tc->private;
			rpinfo->rp_alloc = 1;
		}

		err = p9_payload_gup(req, &pdata_off, &pdata_len, nr_pages,
				req->tc->id == P9_TREAD ? 1 : 0);
		if (err < 0) {
			if (rpinfo->rp_alloc)
				kfree(rpinfo);
			return err;
		} else {
			atomic_add(rpinfo->rp_nr_pages, &vp_pinned);
		}
	}

req_retry_pinned:
	spin_lock_irqsave(&chan->lock, flags);

	/* Handle out VirtIO ring buffers */
	out = pack_sg_list(chan->sg, 0, VIRTQUEUE_NUM, req->tc->sdata,
			req->tc->size);

	if (req->tc->pbuf_size && (req->tc->id == P9_TWRITE)) {
		/* We have additional write payload buffer to take care */
		if (req->tc->pubuf && P9_IS_USER_CONTEXT) {
			outp = pack_sg_list_p(chan->sg, out, VIRTQUEUE_NUM,
					pdata_off, rpinfo->rp_data, pdata_len);
		} else {
			char *pbuf;
			if (req->tc->pubuf)
				pbuf = (__force char *) req->tc->pubuf;
			else
				pbuf = req->tc->pkbuf;
			outp = pack_sg_list(chan->sg, out, VIRTQUEUE_NUM, pbuf,
					req->tc->pbuf_size);
		}
		out += outp;
	}

	/* Handle in VirtIO ring buffers */
	if (req->tc->pbuf_size &&
		((req->tc->id == P9_TREAD) || (req->tc->id == P9_TREADDIR))) {
		/*
		 * Take care of additional Read payload.
		 * 11 is the read/write header = PDU Header(7) + IO Size (4).
		 * Arrange in such a way that server places header in the
		 * alloced memory and payload onto the user buffer.
		 */
		inp = pack_sg_list(chan->sg, out,
				   VIRTQUEUE_NUM, req->rc->sdata, 11);
		/*
		 * Running executables in the filesystem may result in
		 * a read request with kernel buffer as opposed to user buffer.
		 */
		if (req->tc->pubuf && P9_IS_USER_CONTEXT) {
			in = pack_sg_list_p(chan->sg, out+inp, VIRTQUEUE_NUM,
					pdata_off, rpinfo->rp_data, pdata_len);
		} else {
			char *pbuf;
			if (req->tc->pubuf)
				pbuf = (__force char *) req->tc->pubuf;
			else
				pbuf = req->tc->pkbuf;

			in = pack_sg_list(chan->sg, out+inp, VIRTQUEUE_NUM,
					pbuf, req->tc->pbuf_size);
		}
		in += inp;
	} else {
		in = pack_sg_list(chan->sg, out, VIRTQUEUE_NUM,
				  req->rc->sdata, req->rc->capacity);
	}

	err = virtqueue_add_buf(chan->vq, chan->sg, out, in, req->tc);
	if (err < 0) {
		if (err == -ENOSPC) {
			chan->ring_bufs_avail = 0;
			spin_unlock_irqrestore(&chan->lock, flags);
			err = wait_event_interruptible(*chan->vc_wq,
							chan->ring_bufs_avail);
			if (err  == -ERESTARTSYS)
				return err;

			P9_DPRINTK(P9_DEBUG_TRANS, "9p:Retry virtio request\n");
			goto req_retry_pinned;
		} else {
			spin_unlock_irqrestore(&chan->lock, flags);
			P9_DPRINTK(P9_DEBUG_TRANS,
					"9p debug: "
					"virtio rpc add_buf returned failure");
			if (rpinfo && rpinfo->rp_alloc)
				kfree(rpinfo);
			return -EIO;
		}
	}

	virtqueue_kick(chan->vq);
	spin_unlock_irqrestore(&chan->lock, flags);

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio request kicked\n");
	return 0;
}

static ssize_t p9_mount_tag_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct virtio_chan *chan;
	struct virtio_device *vdev;

	vdev = dev_to_virtio(dev);
	chan = vdev->priv;

	return snprintf(buf, chan->tag_len + 1, "%s", chan->tag);
}

static DEVICE_ATTR(mount_tag, 0444, p9_mount_tag_show, NULL);

/**
 * p9_virtio_probe - probe for existence of 9P virtio channels
 * @vdev: virtio device to probe
 *
 * This probes for existing virtio channels.
 *
 */

static int p9_virtio_probe(struct virtio_device *vdev)
{
	__u16 tag_len;
	char *tag;
	int err;
	struct virtio_chan *chan;

	chan = kmalloc(sizeof(struct virtio_chan), GFP_KERNEL);
	if (!chan) {
		printk(KERN_ERR "9p: Failed to allocate virtio 9P channel\n");
		err = -ENOMEM;
		goto fail;
	}

	chan->vdev = vdev;

	/* We expect one virtqueue, for requests. */
	chan->vq = virtio_find_single_vq(vdev, req_done, "requests");
	if (IS_ERR(chan->vq)) {
		err = PTR_ERR(chan->vq);
		goto out_free_vq;
	}
	chan->vq->vdev->priv = chan;
	spin_lock_init(&chan->lock);

	sg_init_table(chan->sg, VIRTQUEUE_NUM);

	chan->inuse = false;
	if (virtio_has_feature(vdev, VIRTIO_9P_MOUNT_TAG)) {
		vdev->config->get(vdev,
				offsetof(struct virtio_9p_config, tag_len),
				&tag_len, sizeof(tag_len));
	} else {
		err = -EINVAL;
		goto out_free_vq;
	}
	tag = kmalloc(tag_len, GFP_KERNEL);
	if (!tag) {
		err = -ENOMEM;
		goto out_free_vq;
	}
	vdev->config->get(vdev, offsetof(struct virtio_9p_config, tag),
			tag, tag_len);
	chan->tag = tag;
	chan->tag_len = tag_len;
	err = sysfs_create_file(&(vdev->dev.kobj), &dev_attr_mount_tag.attr);
	if (err) {
		goto out_free_tag;
	}
	chan->vc_wq = kmalloc(sizeof(wait_queue_head_t), GFP_KERNEL);
	if (!chan->vc_wq) {
		err = -ENOMEM;
		goto out_free_tag;
	}
	init_waitqueue_head(chan->vc_wq);
	chan->ring_bufs_avail = 1;
	/* Ceiling limit to avoid denial of service attacks */
	chan->p9_max_pages = nr_free_buffer_pages()/4;

	mutex_lock(&virtio_9p_lock);
	list_add_tail(&chan->chan_list, &virtio_chan_list);
	mutex_unlock(&virtio_9p_lock);
	return 0;

out_free_tag:
	kfree(tag);
out_free_vq:
	vdev->config->del_vqs(vdev);
	kfree(chan);
fail:
	return err;
}


/**
 * p9_virtio_create - allocate a new virtio channel
 * @client: client instance invoking this transport
 * @devname: string identifying the channel to connect to (unused)
 * @args: args passed from sys_mount() for per-transport options (unused)
 *
 * This sets up a transport channel for 9p communication.  Right now
 * we only match the first available channel, but eventually we couldlook up
 * alternate channels by matching devname versus a virtio_config entry.
 * We use a simple reference count mechanism to ensure that only a single
 * mount has a channel open at a time.
 *
 */

static int
p9_virtio_create(struct p9_client *client, const char *devname, char *args)
{
	struct virtio_chan *chan;
	int ret = -ENOENT;
	int found = 0;

	mutex_lock(&virtio_9p_lock);
	list_for_each_entry(chan, &virtio_chan_list, chan_list) {
		if (!strncmp(devname, chan->tag, chan->tag_len) &&
		    strlen(devname) == chan->tag_len) {
			if (!chan->inuse) {
				chan->inuse = true;
				found = 1;
				break;
			}
			ret = -EBUSY;
		}
	}
	mutex_unlock(&virtio_9p_lock);

	if (!found) {
		printk(KERN_ERR "9p: no channels available\n");
		return ret;
	}

	client->trans = (void *)chan;
	client->status = Connected;
	chan->client = client;

	return 0;
}

/**
 * p9_virtio_remove - clean up resources associated with a virtio device
 * @vdev: virtio device to remove
 *
 */

static void p9_virtio_remove(struct virtio_device *vdev)
{
	struct virtio_chan *chan = vdev->priv;

	BUG_ON(chan->inuse);
	vdev->config->del_vqs(vdev);

	mutex_lock(&virtio_9p_lock);
	list_del(&chan->chan_list);
	mutex_unlock(&virtio_9p_lock);
	sysfs_remove_file(&(vdev->dev.kobj), &dev_attr_mount_tag.attr);
	kfree(chan->tag);
	kfree(chan->vc_wq);
	kfree(chan);

}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_9P, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_9P_MOUNT_TAG,
};

/* The standard "struct lguest_driver": */
static struct virtio_driver p9_virtio_drv = {
	.feature_table  = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name    = KBUILD_MODNAME,
	.driver.owner	= THIS_MODULE,
	.id_table	= id_table,
	.probe		= p9_virtio_probe,
	.remove		= p9_virtio_remove,
};

static struct p9_trans_module p9_virtio_trans = {
	.name = "virtio",
	.create = p9_virtio_create,
	.close = p9_virtio_close,
	.request = p9_virtio_request,
	.cancel = p9_virtio_cancel,

	/*
	 * We leave one entry for input and one entry for response
	 * headers. We also skip one more entry to accomodate, address
	 * that are not at page boundary, that can result in an extra
	 * page in zero copy.
	 */
	.maxsize = PAGE_SIZE * (VIRTQUEUE_NUM - 3),
	.pref = P9_TRANS_PREF_PAYLOAD_SEP,
	.def = 0,
	.owner = THIS_MODULE,
};

/* The standard init function */
static int __init p9_virtio_init(void)
{
	INIT_LIST_HEAD(&virtio_chan_list);

	v9fs_register_trans(&p9_virtio_trans);
	return register_virtio_driver(&p9_virtio_drv);
}

static void __exit p9_virtio_cleanup(void)
{
	unregister_virtio_driver(&p9_virtio_drv);
	v9fs_unregister_trans(&p9_virtio_trans);
}

module_init(p9_virtio_init);
module_exit(p9_virtio_cleanup);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_AUTHOR("Eric Van Hensbergen <ericvh@gmail.com>");
MODULE_DESCRIPTION("Virtio 9p Transport");
MODULE_LICENSE("GPL");
