/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_arp.h>
#include <linux/delay.h>

#include "rds.h"
#include "iw.h"

unsigned int fastreg_pool_size = RDS_FASTREG_POOL_SIZE;
unsigned int fastreg_message_size = RDS_FASTREG_SIZE + 1; /* +1 allows for unaligned MRs */

module_param(fastreg_pool_size, int, 0444);
MODULE_PARM_DESC(fastreg_pool_size, " Max number of fastreg MRs per device");
module_param(fastreg_message_size, int, 0444);
MODULE_PARM_DESC(fastreg_message_size, " Max size of a RDMA transfer (fastreg MRs)");

struct list_head rds_iw_devices;

/* NOTE: if also grabbing iwdev lock, grab this first */
DEFINE_SPINLOCK(iw_nodev_conns_lock);
LIST_HEAD(iw_nodev_conns);

void rds_iw_add_one(struct ib_device *device)
{
	struct rds_iw_device *rds_iwdev;
	struct ib_device_attr *dev_attr;

	/* Only handle iwarp devices */
	if (device->node_type != RDMA_NODE_RNIC)
		return;

	dev_attr = kmalloc(sizeof *dev_attr, GFP_KERNEL);
	if (!dev_attr)
		return;

	if (ib_query_device(device, dev_attr)) {
		rdsdebug("Query device failed for %s\n", device->name);
		goto free_attr;
	}

	rds_iwdev = kmalloc(sizeof *rds_iwdev, GFP_KERNEL);
	if (!rds_iwdev)
		goto free_attr;

	spin_lock_init(&rds_iwdev->spinlock);

	rds_iwdev->dma_local_lkey = !!(dev_attr->device_cap_flags & IB_DEVICE_LOCAL_DMA_LKEY);
	rds_iwdev->max_wrs = dev_attr->max_qp_wr;
	rds_iwdev->max_sge = min(dev_attr->max_sge, RDS_IW_MAX_SGE);

	rds_iwdev->dev = device;
	rds_iwdev->pd = ib_alloc_pd(device);
	if (IS_ERR(rds_iwdev->pd))
		goto free_dev;

	if (!rds_iwdev->dma_local_lkey) {
		rds_iwdev->mr = ib_get_dma_mr(rds_iwdev->pd,
					IB_ACCESS_REMOTE_READ |
					IB_ACCESS_REMOTE_WRITE |
					IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(rds_iwdev->mr))
			goto err_pd;
	} else
		rds_iwdev->mr = NULL;

	rds_iwdev->mr_pool = rds_iw_create_mr_pool(rds_iwdev);
	if (IS_ERR(rds_iwdev->mr_pool)) {
		rds_iwdev->mr_pool = NULL;
		goto err_mr;
	}

	INIT_LIST_HEAD(&rds_iwdev->cm_id_list);
	INIT_LIST_HEAD(&rds_iwdev->conn_list);
	list_add_tail(&rds_iwdev->list, &rds_iw_devices);

	ib_set_client_data(device, &rds_iw_client, rds_iwdev);

	goto free_attr;

err_mr:
	if (rds_iwdev->mr)
		ib_dereg_mr(rds_iwdev->mr);
err_pd:
	ib_dealloc_pd(rds_iwdev->pd);
free_dev:
	kfree(rds_iwdev);
free_attr:
	kfree(dev_attr);
}

void rds_iw_remove_one(struct ib_device *device)
{
	struct rds_iw_device *rds_iwdev;
	struct rds_iw_cm_id *i_cm_id, *next;

	rds_iwdev = ib_get_client_data(device, &rds_iw_client);
	if (!rds_iwdev)
		return;

	spin_lock_irq(&rds_iwdev->spinlock);
	list_for_each_entry_safe(i_cm_id, next, &rds_iwdev->cm_id_list, list) {
		list_del(&i_cm_id->list);
		kfree(i_cm_id);
	}
	spin_unlock_irq(&rds_iwdev->spinlock);

	rds_iw_destroy_conns(rds_iwdev);

	if (rds_iwdev->mr_pool)
		rds_iw_destroy_mr_pool(rds_iwdev->mr_pool);

	if (rds_iwdev->mr)
		ib_dereg_mr(rds_iwdev->mr);

	while (ib_dealloc_pd(rds_iwdev->pd)) {
		rdsdebug("Failed to dealloc pd %p\n", rds_iwdev->pd);
		msleep(1);
	}

	list_del(&rds_iwdev->list);
	kfree(rds_iwdev);
}

struct ib_client rds_iw_client = {
	.name   = "rds_iw",
	.add    = rds_iw_add_one,
	.remove = rds_iw_remove_one
};

static int rds_iw_conn_info_visitor(struct rds_connection *conn,
				    void *buffer)
{
	struct rds_info_rdma_connection *iinfo = buffer;
	struct rds_iw_connection *ic;

	/* We will only ever look at IB transports */
	if (conn->c_trans != &rds_iw_transport)
		return 0;

	iinfo->src_addr = conn->c_laddr;
	iinfo->dst_addr = conn->c_faddr;

	memset(&iinfo->src_gid, 0, sizeof(iinfo->src_gid));
	memset(&iinfo->dst_gid, 0, sizeof(iinfo->dst_gid));
	if (rds_conn_state(conn) == RDS_CONN_UP) {
		struct rds_iw_device *rds_iwdev;
		struct rdma_dev_addr *dev_addr;

		ic = conn->c_transport_data;
		dev_addr = &ic->i_cm_id->route.addr.dev_addr;

		ib_addr_get_sgid(dev_addr, (union ib_gid *) &iinfo->src_gid);
		ib_addr_get_dgid(dev_addr, (union ib_gid *) &iinfo->dst_gid);

		rds_iwdev = ib_get_client_data(ic->i_cm_id->device, &rds_iw_client);
		iinfo->max_send_wr = ic->i_send_ring.w_nr;
		iinfo->max_recv_wr = ic->i_recv_ring.w_nr;
		iinfo->max_send_sge = rds_iwdev->max_sge;
		rds_iw_get_mr_info(rds_iwdev, iinfo);
	}
	return 1;
}

static void rds_iw_ic_info(struct socket *sock, unsigned int len,
			   struct rds_info_iterator *iter,
			   struct rds_info_lengths *lens)
{
	rds_for_each_conn_info(sock, len, iter, lens,
				rds_iw_conn_info_visitor,
				sizeof(struct rds_info_rdma_connection));
}


/*
 * Early RDS/IB was built to only bind to an address if there is an IPoIB
 * device with that address set.
 *
 * If it were me, I'd advocate for something more flexible.  Sending and
 * receiving should be device-agnostic.  Transports would try and maintain
 * connections between peers who have messages queued.  Userspace would be
 * allowed to influence which paths have priority.  We could call userspace
 * asserting this policy "routing".
 */
static int rds_iw_laddr_check(__be32 addr)
{
	int ret;
	struct rdma_cm_id *cm_id;
	struct sockaddr_in sin;

	/* Create a CMA ID and try to bind it. This catches both
	 * IB and iWARP capable NICs.
	 */
	cm_id = rdma_create_id(NULL, NULL, RDMA_PS_TCP);
	if (IS_ERR(cm_id))
		return PTR_ERR(cm_id);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = addr;

	/* rdma_bind_addr will only succeed for IB & iWARP devices */
	ret = rdma_bind_addr(cm_id, (struct sockaddr *)&sin);
	/* due to this, we will claim to support IB devices unless we
	   check node_type. */
	if (ret || cm_id->device->node_type != RDMA_NODE_RNIC)
		ret = -EADDRNOTAVAIL;

	rdsdebug("addr %pI4 ret %d node type %d\n",
		&addr, ret,
		cm_id->device ? cm_id->device->node_type : -1);

	rdma_destroy_id(cm_id);

	return ret;
}

void rds_iw_exit(void)
{
	rds_info_deregister_func(RDS_INFO_IWARP_CONNECTIONS, rds_iw_ic_info);
	rds_iw_destroy_nodev_conns();
	ib_unregister_client(&rds_iw_client);
	rds_iw_sysctl_exit();
	rds_iw_recv_exit();
	rds_trans_unregister(&rds_iw_transport);
}

struct rds_transport rds_iw_transport = {
	.laddr_check		= rds_iw_laddr_check,
	.xmit_complete		= rds_iw_xmit_complete,
	.xmit			= rds_iw_xmit,
	.xmit_cong_map		= NULL,
	.xmit_rdma		= rds_iw_xmit_rdma,
	.recv			= rds_iw_recv,
	.conn_alloc		= rds_iw_conn_alloc,
	.conn_free		= rds_iw_conn_free,
	.conn_connect		= rds_iw_conn_connect,
	.conn_shutdown		= rds_iw_conn_shutdown,
	.inc_copy_to_user	= rds_iw_inc_copy_to_user,
	.inc_purge		= rds_iw_inc_purge,
	.inc_free		= rds_iw_inc_free,
	.cm_initiate_connect	= rds_iw_cm_initiate_connect,
	.cm_handle_connect	= rds_iw_cm_handle_connect,
	.cm_connect_complete	= rds_iw_cm_connect_complete,
	.stats_info_copy	= rds_iw_stats_info_copy,
	.exit			= rds_iw_exit,
	.get_mr			= rds_iw_get_mr,
	.sync_mr		= rds_iw_sync_mr,
	.free_mr		= rds_iw_free_mr,
	.flush_mrs		= rds_iw_flush_mrs,
	.t_owner		= THIS_MODULE,
	.t_name			= "iwarp",
	.t_type			= RDS_TRANS_IWARP,
	.t_prefer_loopback	= 1,
};

int __init rds_iw_init(void)
{
	int ret;

	INIT_LIST_HEAD(&rds_iw_devices);

	ret = ib_register_client(&rds_iw_client);
	if (ret)
		goto out;

	ret = rds_iw_sysctl_init();
	if (ret)
		goto out_ibreg;

	ret = rds_iw_recv_init();
	if (ret)
		goto out_sysctl;

	ret = rds_trans_register(&rds_iw_transport);
	if (ret)
		goto out_recv;

	rds_info_register_func(RDS_INFO_IWARP_CONNECTIONS, rds_iw_ic_info);

	goto out;

out_recv:
	rds_iw_recv_exit();
out_sysctl:
	rds_iw_sysctl_exit();
out_ibreg:
	ib_unregister_client(&rds_iw_client);
out:
	return ret;
}

MODULE_LICENSE("GPL");

