/*
 * linux/fs/9p/trans_xen
 *
 * Xen transport layer.
 *
 * Copyright (C) 2017 by Stefano Stabellini <stefano@aporeto.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/9pfs.h>

#include <linux/module.h>
#include <linux/spinlock.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>

#define XEN_9PFS_NUM_RINGS 2
#define XEN_9PFS_RING_ORDER 9
#define XEN_9PFS_RING_SIZE(ring)  XEN_FLEX_RING_SIZE(ring->intf->ring_order)

struct xen_9pfs_header {
	uint32_t size;
	uint8_t id;
	uint16_t tag;

	/* uint8_t sdata[]; */
} __attribute__((packed));

/* One per ring, more than one per 9pfs share */
struct xen_9pfs_dataring {
	struct xen_9pfs_front_priv *priv;

	struct xen_9pfs_data_intf *intf;
	grant_ref_t ref;
	int evtchn;
	int irq;
	/* protect a ring from concurrent accesses */
	spinlock_t lock;

	struct xen_9pfs_data data;
	wait_queue_head_t wq;
	struct work_struct work;
};

/* One per 9pfs share */
struct xen_9pfs_front_priv {
	struct list_head list;
	struct xenbus_device *dev;
	char *tag;
	struct p9_client *client;

	int num_rings;
	struct xen_9pfs_dataring *rings;
};

static LIST_HEAD(xen_9pfs_devs);
static DEFINE_RWLOCK(xen_9pfs_lock);

/* We don't currently allow canceling of requests */
static int p9_xen_cancel(struct p9_client *client, struct p9_req_t *req)
{
	return 1;
}

static int p9_xen_create(struct p9_client *client, const char *addr, char *args)
{
	struct xen_9pfs_front_priv *priv;

	if (addr == NULL)
		return -EINVAL;

	read_lock(&xen_9pfs_lock);
	list_for_each_entry(priv, &xen_9pfs_devs, list) {
		if (!strcmp(priv->tag, addr)) {
			priv->client = client;
			read_unlock(&xen_9pfs_lock);
			return 0;
		}
	}
	read_unlock(&xen_9pfs_lock);
	return -EINVAL;
}

static void p9_xen_close(struct p9_client *client)
{
	struct xen_9pfs_front_priv *priv;

	read_lock(&xen_9pfs_lock);
	list_for_each_entry(priv, &xen_9pfs_devs, list) {
		if (priv->client == client) {
			priv->client = NULL;
			read_unlock(&xen_9pfs_lock);
			return;
		}
	}
	read_unlock(&xen_9pfs_lock);
}

static bool p9_xen_write_todo(struct xen_9pfs_dataring *ring, RING_IDX size)
{
	RING_IDX cons, prod;

	cons = ring->intf->out_cons;
	prod = ring->intf->out_prod;
	virt_mb();

	return XEN_9PFS_RING_SIZE(ring) -
		xen_9pfs_queued(prod, cons, XEN_9PFS_RING_SIZE(ring)) >= size;
}

static int p9_xen_request(struct p9_client *client, struct p9_req_t *p9_req)
{
	struct xen_9pfs_front_priv *priv = NULL;
	RING_IDX cons, prod, masked_cons, masked_prod;
	unsigned long flags;
	u32 size = p9_req->tc.size;
	struct xen_9pfs_dataring *ring;
	int num;

	read_lock(&xen_9pfs_lock);
	list_for_each_entry(priv, &xen_9pfs_devs, list) {
		if (priv->client == client)
			break;
	}
	read_unlock(&xen_9pfs_lock);
	if (!priv || priv->client != client)
		return -EINVAL;

	num = p9_req->tc.tag % priv->num_rings;
	ring = &priv->rings[num];

again:
	while (wait_event_killable(ring->wq,
				   p9_xen_write_todo(ring, size)) != 0)
		;

	spin_lock_irqsave(&ring->lock, flags);
	cons = ring->intf->out_cons;
	prod = ring->intf->out_prod;
	virt_mb();

	if (XEN_9PFS_RING_SIZE(ring) -
	    xen_9pfs_queued(prod, cons, XEN_9PFS_RING_SIZE(ring)) < size) {
		spin_unlock_irqrestore(&ring->lock, flags);
		goto again;
	}

	masked_prod = xen_9pfs_mask(prod, XEN_9PFS_RING_SIZE(ring));
	masked_cons = xen_9pfs_mask(cons, XEN_9PFS_RING_SIZE(ring));

	xen_9pfs_write_packet(ring->data.out, p9_req->tc.sdata, size,
			      &masked_prod, masked_cons,
			      XEN_9PFS_RING_SIZE(ring));

	p9_req->status = REQ_STATUS_SENT;
	virt_wmb();			/* write ring before updating pointer */
	prod += size;
	ring->intf->out_prod = prod;
	spin_unlock_irqrestore(&ring->lock, flags);
	notify_remote_via_irq(ring->irq);
	p9_req_put(p9_req);

	return 0;
}

static void p9_xen_response(struct work_struct *work)
{
	struct xen_9pfs_front_priv *priv;
	struct xen_9pfs_dataring *ring;
	RING_IDX cons, prod, masked_cons, masked_prod;
	struct xen_9pfs_header h;
	struct p9_req_t *req;
	int status;

	ring = container_of(work, struct xen_9pfs_dataring, work);
	priv = ring->priv;

	while (1) {
		cons = ring->intf->in_cons;
		prod = ring->intf->in_prod;
		virt_rmb();

		if (xen_9pfs_queued(prod, cons, XEN_9PFS_RING_SIZE(ring)) <
		    sizeof(h)) {
			notify_remote_via_irq(ring->irq);
			return;
		}

		masked_prod = xen_9pfs_mask(prod, XEN_9PFS_RING_SIZE(ring));
		masked_cons = xen_9pfs_mask(cons, XEN_9PFS_RING_SIZE(ring));

		/* First, read just the header */
		xen_9pfs_read_packet(&h, ring->data.in, sizeof(h),
				     masked_prod, &masked_cons,
				     XEN_9PFS_RING_SIZE(ring));

		req = p9_tag_lookup(priv->client, h.tag);
		if (!req || req->status != REQ_STATUS_SENT) {
			dev_warn(&priv->dev->dev, "Wrong req tag=%x\n", h.tag);
			cons += h.size;
			virt_mb();
			ring->intf->in_cons = cons;
			continue;
		}

		memcpy(&req->rc, &h, sizeof(h));
		req->rc.offset = 0;

		masked_cons = xen_9pfs_mask(cons, XEN_9PFS_RING_SIZE(ring));
		/* Then, read the whole packet (including the header) */
		xen_9pfs_read_packet(req->rc.sdata, ring->data.in, h.size,
				     masked_prod, &masked_cons,
				     XEN_9PFS_RING_SIZE(ring));

		virt_mb();
		cons += h.size;
		ring->intf->in_cons = cons;

		status = (req->status != REQ_STATUS_ERROR) ?
			REQ_STATUS_RCVD : REQ_STATUS_ERROR;

		p9_client_cb(priv->client, req, status);
	}
}

static irqreturn_t xen_9pfs_front_event_handler(int irq, void *r)
{
	struct xen_9pfs_dataring *ring = r;

	if (!ring || !ring->priv->client) {
		/* ignore spurious interrupt */
		return IRQ_HANDLED;
	}

	wake_up_interruptible(&ring->wq);
	schedule_work(&ring->work);

	return IRQ_HANDLED;
}

static struct p9_trans_module p9_xen_trans = {
	.name = "xen",
	.maxsize = 1 << (XEN_9PFS_RING_ORDER + XEN_PAGE_SHIFT - 2),
	.def = 1,
	.create = p9_xen_create,
	.close = p9_xen_close,
	.request = p9_xen_request,
	.cancel = p9_xen_cancel,
	.owner = THIS_MODULE,
};

static const struct xenbus_device_id xen_9pfs_front_ids[] = {
	{ "9pfs" },
	{ "" }
};

static void xen_9pfs_front_free(struct xen_9pfs_front_priv *priv)
{
	int i, j;

	write_lock(&xen_9pfs_lock);
	list_del(&priv->list);
	write_unlock(&xen_9pfs_lock);

	for (i = 0; i < priv->num_rings; i++) {
		if (!priv->rings[i].intf)
			break;
		if (priv->rings[i].irq > 0)
			unbind_from_irqhandler(priv->rings[i].irq, priv->dev);
		if (priv->rings[i].data.in) {
			for (j = 0;
			     j < (1 << priv->rings[i].intf->ring_order);
			     j++) {
				grant_ref_t ref;

				ref = priv->rings[i].intf->ref[j];
				gnttab_end_foreign_access(ref, 0, 0);
			}
			free_pages((unsigned long)priv->rings[i].data.in,
				   priv->rings[i].intf->ring_order -
				   (PAGE_SHIFT - XEN_PAGE_SHIFT));
		}
		gnttab_end_foreign_access(priv->rings[i].ref, 0, 0);
		free_page((unsigned long)priv->rings[i].intf);
	}
	kfree(priv->rings);
	kfree(priv->tag);
	kfree(priv);
}

static int xen_9pfs_front_remove(struct xenbus_device *dev)
{
	struct xen_9pfs_front_priv *priv = dev_get_drvdata(&dev->dev);

	dev_set_drvdata(&dev->dev, NULL);
	xen_9pfs_front_free(priv);
	return 0;
}

static int xen_9pfs_front_alloc_dataring(struct xenbus_device *dev,
					 struct xen_9pfs_dataring *ring,
					 unsigned int order)
{
	int i = 0;
	int ret = -ENOMEM;
	void *bytes = NULL;

	init_waitqueue_head(&ring->wq);
	spin_lock_init(&ring->lock);
	INIT_WORK(&ring->work, p9_xen_response);

	ring->intf = (struct xen_9pfs_data_intf *)get_zeroed_page(GFP_KERNEL);
	if (!ring->intf)
		return ret;
	ret = gnttab_grant_foreign_access(dev->otherend_id,
					  virt_to_gfn(ring->intf), 0);
	if (ret < 0)
		goto out;
	ring->ref = ret;
	bytes = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
			order - (PAGE_SHIFT - XEN_PAGE_SHIFT));
	if (!bytes) {
		ret = -ENOMEM;
		goto out;
	}
	for (; i < (1 << order); i++) {
		ret = gnttab_grant_foreign_access(
				dev->otherend_id, virt_to_gfn(bytes) + i, 0);
		if (ret < 0)
			goto out;
		ring->intf->ref[i] = ret;
	}
	ring->intf->ring_order = order;
	ring->data.in = bytes;
	ring->data.out = bytes + XEN_FLEX_RING_SIZE(order);

	ret = xenbus_alloc_evtchn(dev, &ring->evtchn);
	if (ret)
		goto out;
	ring->irq = bind_evtchn_to_irqhandler(ring->evtchn,
					      xen_9pfs_front_event_handler,
					      0, "xen_9pfs-frontend", ring);
	if (ring->irq >= 0)
		return 0;

	xenbus_free_evtchn(dev, ring->evtchn);
	ret = ring->irq;
out:
	if (bytes) {
		for (i--; i >= 0; i--)
			gnttab_end_foreign_access(ring->intf->ref[i], 0, 0);
		free_pages((unsigned long)bytes,
			   ring->intf->ring_order -
			   (PAGE_SHIFT - XEN_PAGE_SHIFT));
	}
	gnttab_end_foreign_access(ring->ref, 0, 0);
	free_page((unsigned long)ring->intf);
	return ret;
}

static int xen_9pfs_front_probe(struct xenbus_device *dev,
				const struct xenbus_device_id *id)
{
	int ret, i;
	struct xenbus_transaction xbt;
	struct xen_9pfs_front_priv *priv = NULL;
	char *versions;
	unsigned int max_rings, max_ring_order, len = 0;

	versions = xenbus_read(XBT_NIL, dev->otherend, "versions", &len);
	if (IS_ERR(versions))
		return PTR_ERR(versions);
	if (strcmp(versions, "1")) {
		kfree(versions);
		return -EINVAL;
	}
	kfree(versions);
	max_rings = xenbus_read_unsigned(dev->otherend, "max-rings", 0);
	if (max_rings < XEN_9PFS_NUM_RINGS)
		return -EINVAL;
	max_ring_order = xenbus_read_unsigned(dev->otherend,
					      "max-ring-page-order", 0);
	if (max_ring_order > XEN_9PFS_RING_ORDER)
		max_ring_order = XEN_9PFS_RING_ORDER;
	if (p9_xen_trans.maxsize > XEN_FLEX_RING_SIZE(max_ring_order))
		p9_xen_trans.maxsize = XEN_FLEX_RING_SIZE(max_ring_order) / 2;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->num_rings = XEN_9PFS_NUM_RINGS;
	priv->rings = kcalloc(priv->num_rings, sizeof(*priv->rings),
			      GFP_KERNEL);
	if (!priv->rings) {
		kfree(priv);
		return -ENOMEM;
	}

	for (i = 0; i < priv->num_rings; i++) {
		priv->rings[i].priv = priv;
		ret = xen_9pfs_front_alloc_dataring(dev, &priv->rings[i],
						    max_ring_order);
		if (ret < 0)
			goto error;
	}

 again:
	ret = xenbus_transaction_start(&xbt);
	if (ret) {
		xenbus_dev_fatal(dev, ret, "starting transaction");
		goto error;
	}
	ret = xenbus_printf(xbt, dev->nodename, "version", "%u", 1);
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "num-rings", "%u",
			    priv->num_rings);
	if (ret)
		goto error_xenbus;
	for (i = 0; i < priv->num_rings; i++) {
		char str[16];

		BUILD_BUG_ON(XEN_9PFS_NUM_RINGS > 9);
		sprintf(str, "ring-ref%d", i);
		ret = xenbus_printf(xbt, dev->nodename, str, "%d",
				    priv->rings[i].ref);
		if (ret)
			goto error_xenbus;

		sprintf(str, "event-channel-%d", i);
		ret = xenbus_printf(xbt, dev->nodename, str, "%u",
				    priv->rings[i].evtchn);
		if (ret)
			goto error_xenbus;
	}
	priv->tag = xenbus_read(xbt, dev->nodename, "tag", NULL);
	if (IS_ERR(priv->tag)) {
		ret = PTR_ERR(priv->tag);
		goto error_xenbus;
	}
	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, ret, "completing transaction");
		goto error;
	}

	write_lock(&xen_9pfs_lock);
	list_add_tail(&priv->list, &xen_9pfs_devs);
	write_unlock(&xen_9pfs_lock);
	dev_set_drvdata(&dev->dev, priv);
	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

 error_xenbus:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, ret, "writing xenstore");
 error:
	dev_set_drvdata(&dev->dev, NULL);
	xen_9pfs_front_free(priv);
	return ret;
}

static int xen_9pfs_front_resume(struct xenbus_device *dev)
{
	dev_warn(&dev->dev, "suspend/resume unsupported\n");
	return 0;
}

static void xen_9pfs_front_changed(struct xenbus_device *dev,
				   enum xenbus_state backend_state)
{
	switch (backend_state) {
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateUnknown:
		break;

	case XenbusStateInitWait:
		break;

	case XenbusStateConnected:
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		fallthrough;	/* Missed the backend's CLOSING state */
	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
		break;
	}
}

static struct xenbus_driver xen_9pfs_front_driver = {
	.ids = xen_9pfs_front_ids,
	.probe = xen_9pfs_front_probe,
	.remove = xen_9pfs_front_remove,
	.resume = xen_9pfs_front_resume,
	.otherend_changed = xen_9pfs_front_changed,
};

static int p9_trans_xen_init(void)
{
	int rc;

	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising Xen transport for 9pfs\n");

	v9fs_register_trans(&p9_xen_trans);
	rc = xenbus_register_frontend(&xen_9pfs_front_driver);
	if (rc)
		v9fs_unregister_trans(&p9_xen_trans);

	return rc;
}
module_init(p9_trans_xen_init);

static void p9_trans_xen_exit(void)
{
	v9fs_unregister_trans(&p9_xen_trans);
	return xenbus_unregister_driver(&xen_9pfs_front_driver);
}
module_exit(p9_trans_xen_exit);

MODULE_AUTHOR("Stefano Stabellini <stefano@aporeto.com>");
MODULE_DESCRIPTION("Xen Transport for 9P");
MODULE_LICENSE("GPL");
