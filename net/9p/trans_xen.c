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
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <net/9p/transport.h>

static int p9_xen_cancel(struct p9_client *client, struct p9_req_t *req)
{
	return 0;
}

static int p9_xen_create(struct p9_client *client, const char *addr, char *args)
{
	return 0;
}

static void p9_xen_close(struct p9_client *client)
{
}

static int p9_xen_request(struct p9_client *client, struct p9_req_t *p9_req)
{
	return 0;
}

static struct p9_trans_module p9_xen_trans = {
	.name = "xen",
	.maxsize = 1 << (XEN_9PFS_RING_ORDER + XEN_PAGE_SHIFT),
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

static int xen_9pfs_front_remove(struct xenbus_device *dev)
{
	return 0;
}

static int xen_9pfs_front_probe(struct xenbus_device *dev,
				const struct xenbus_device_id *id)
{
	return 0;
}

static int xen_9pfs_front_resume(struct xenbus_device *dev)
{
	return 0;
}

static void xen_9pfs_front_changed(struct xenbus_device *dev,
				   enum xenbus_state backend_state)
{
}

static struct xenbus_driver xen_9pfs_front_driver = {
	.ids = xen_9pfs_front_ids,
	.probe = xen_9pfs_front_probe,
	.remove = xen_9pfs_front_remove,
	.resume = xen_9pfs_front_resume,
	.otherend_changed = xen_9pfs_front_changed,
};

int p9_trans_xen_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising Xen transport for 9pfs\n");

	v9fs_register_trans(&p9_xen_trans);
	return xenbus_register_frontend(&xen_9pfs_front_driver);
}
module_init(p9_trans_xen_init);

void p9_trans_xen_exit(void)
{
	v9fs_unregister_trans(&p9_xen_trans);
	return xenbus_unregister_driver(&xen_9pfs_front_driver);
}
module_exit(p9_trans_xen_exit);
