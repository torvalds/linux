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
#include <net/9p/9p.h>
#include <linux/parser.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>
#include <linux/scatterlist.h>
#include <linux/virtio.h>
#include <linux/virtio_9p.h>

#define VIRTQUEUE_NUM	128

/* a single mutex to manage channel initialization and attachment */
static DEFINE_MUTEX(virtio_9p_lock);
/* global which tracks highest initialized channel */
static int chan_index;

/**
 * struct virtio_chan - per-instance transport information
 * @initialized: whether the channel is initialized
 * @inuse: whether the channel is in use
 * @lock: protects multiple elements within this structure
 * @vdev: virtio dev associated with this channel
 * @vq: virtio queue associated with this channel
 * @tagpool: accounting for tag ids (and request slots)
 * @reqs: array of request slots
 * @max_tag: current number of request_slots allocated
 * @sg: scatter gather list which is used to pack a request (protected?)
 *
 * We keep all per-channel information in a structure.
 * This structure is allocated within the devices dev->mem space.
 * A pointer to the structure will get put in the transport private.
 *
 */

static struct virtio_chan {
	bool initialized;
	bool inuse;

	spinlock_t lock;

	struct p9_client *client;
	struct virtio_device *vdev;
	struct virtqueue *vq;

	/* Scatterlist: can be too big for stack. */
	struct scatterlist sg[VIRTQUEUE_NUM];
} channels[MAX_9P_CHAN];

/* How many bytes left in this page. */
static unsigned int rest_of_page(void *data)
{
	return PAGE_SIZE - ((unsigned long)data % PAGE_SIZE);
}

/**
 * p9_virtio_close - reclaim resources of a channel
 * @trans: transport state
 *
 * This reclaims a channel by freeing its resources and
 * reseting its inuse flag.
 *
 */

static void p9_virtio_close(struct p9_client *client)
{
	struct virtio_chan *chan = client->trans;

	mutex_lock(&virtio_9p_lock);
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

	P9_DPRINTK(P9_DEBUG_TRANS, ": request done\n");

	while ((rc = chan->vq->vq_ops->get_buf(chan->vq, &len)) != NULL) {
		P9_DPRINTK(P9_DEBUG_TRANS, ": rc %p\n", rc);
		P9_DPRINTK(P9_DEBUG_TRANS, ": lookup tag %d\n", rc->tag);
		req = p9_tag_lookup(chan->client, rc->tag);
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
 * p9_virtio_request - issue a request
 * @t: transport state
 * @tc: &p9_fcall request to transmit
 * @rc: &p9_fcall to put reponse into
 *
 */

static int
p9_virtio_request(struct p9_client *client, struct p9_req_t *req)
{
	int in, out;
	struct virtio_chan *chan = client->trans;
	char *rdata = (char *)req->rc+sizeof(struct p9_fcall);

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio request\n");

	out = pack_sg_list(chan->sg, 0, VIRTQUEUE_NUM, req->tc->sdata,
								req->tc->size);
	in = pack_sg_list(chan->sg, out, VIRTQUEUE_NUM-out, rdata,
								client->msize);

	req->status = REQ_STATUS_SENT;

	if (chan->vq->vq_ops->add_buf(chan->vq, chan->sg, out, in, req->tc)) {
		P9_DPRINTK(P9_DEBUG_TRANS,
			"9p debug: virtio rpc add_buf returned failure");
		return -EIO;
	}

	chan->vq->vq_ops->kick(chan->vq);

	P9_DPRINTK(P9_DEBUG_TRANS, "9p debug: virtio request kicked\n");
	return 0;
}

/**
 * p9_virtio_probe - probe for existence of 9P virtio channels
 * @vdev: virtio device to probe
 *
 * This probes for existing virtio channels.  At present only
 * a single channel is in use, so in the future more work may need
 * to be done here.
 *
 */

static int p9_virtio_probe(struct virtio_device *vdev)
{
	int err;
	struct virtio_chan *chan;
	int index;

	mutex_lock(&virtio_9p_lock);
	index = chan_index++;
	chan = &channels[index];
	mutex_unlock(&virtio_9p_lock);

	if (chan_index > MAX_9P_CHAN) {
		printk(KERN_ERR "9p: virtio: Maximum channels exceeded\n");
		BUG();
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
	chan->initialized = true;
	return 0;

out_free_vq:
	vdev->config->del_vqs(vdev);
fail:
	mutex_lock(&virtio_9p_lock);
	chan_index--;
	mutex_unlock(&virtio_9p_lock);
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
 * Bugs: doesn't allow identification of a specific channel
 * to allocate, channels are allocated sequentially. This was
 * a pragmatic decision to get things rolling, but ideally some
 * way of identifying the channel to attach to would be nice
 * if we are going to support multiple channels.
 *
 */

static int
p9_virtio_create(struct p9_client *client, const char *devname, char *args)
{
	struct virtio_chan *chan = channels;
	int index = 0;

	mutex_lock(&virtio_9p_lock);
	while (index < MAX_9P_CHAN) {
		if (chan->initialized && !chan->inuse) {
			chan->inuse = true;
			break;
		} else {
			index++;
			chan = &channels[index];
		}
	}
	mutex_unlock(&virtio_9p_lock);

	if (index >= MAX_9P_CHAN) {
		printk(KERN_ERR "9p: no channels available\n");
		return -ENODEV;
	}

	client->trans = (void *)chan;
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

	if (chan->initialized) {
		vdev->config->del_vqs(vdev);
		chan->initialized = false;
	}
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
	.remove =	p9_virtio_remove,
};

static struct p9_trans_module p9_virtio_trans = {
	.name = "virtio",
	.create = p9_virtio_create,
	.close = p9_virtio_close,
	.request = p9_virtio_request,
	.cancel = p9_virtio_cancel,
	.maxsize = PAGE_SIZE*16,
	.def = 0,
	.owner = THIS_MODULE,
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
