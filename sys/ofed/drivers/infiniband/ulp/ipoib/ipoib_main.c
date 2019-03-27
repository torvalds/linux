/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "ipoib.h"

static	int ipoib_resolvemulti(struct ifnet *, struct sockaddr **,
		struct sockaddr *);


#include <linux/module.h>

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

#include <linux/if_arp.h>	/* For ARPHRD_xxx */
#include <linux/if_vlan.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include <rdma/ib_cache.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IP-over-InfiniBand net driver");
MODULE_LICENSE("Dual BSD/GPL");

int ipoib_sendq_size = IPOIB_TX_RING_SIZE;
int ipoib_recvq_size = IPOIB_RX_RING_SIZE;

module_param_named(send_queue_size, ipoib_sendq_size, int, 0444);
MODULE_PARM_DESC(send_queue_size, "Number of descriptors in send queue");
module_param_named(recv_queue_size, ipoib_recvq_size, int, 0444);
MODULE_PARM_DESC(recv_queue_size, "Number of descriptors in receive queue");

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
int ipoib_debug_level = 1;

module_param_named(debug_level, ipoib_debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Enable debug tracing if > 0");
#endif

struct ipoib_path_iter {
	struct ipoib_dev_priv *priv;
	struct ipoib_path  path;
};

static const u8 ipv4_bcast_addr[] = {
	0x00, 0xff, 0xff, 0xff,
	0xff, 0x12, 0x40, 0x1b,	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,	0xff, 0xff, 0xff, 0xff
};

struct workqueue_struct *ipoib_workqueue;

struct ib_sa_client ipoib_sa_client;

static void ipoib_add_one(struct ib_device *device);
static void ipoib_remove_one(struct ib_device *device, void *client_data);
static struct net_device *ipoib_get_net_dev_by_params(
		struct ib_device *dev, u8 port, u16 pkey,
		const union ib_gid *gid, const struct sockaddr *addr,
		void *client_data);
static void ipoib_start(struct ifnet *dev);
static int ipoib_output(struct ifnet *ifp, struct mbuf *m,
	    const struct sockaddr *dst, struct route *ro);
static int ipoib_ioctl(struct ifnet *ifp, u_long command, caddr_t data);
static void ipoib_input(struct ifnet *ifp, struct mbuf *m);

#define	IPOIB_MTAP(_ifp, _m)					\
do {								\
	if (bpf_peers_present((_ifp)->if_bpf)) {		\
		M_ASSERTVALID(_m);				\
		ipoib_mtap_mb((_ifp), (_m));			\
	}							\
} while (0)

static struct unrhdr *ipoib_unrhdr;

static void
ipoib_unrhdr_init(void *arg)
{

	ipoib_unrhdr = new_unrhdr(0, 65535, NULL);
}
SYSINIT(ipoib_unrhdr_init, SI_SUB_KLD - 1, SI_ORDER_ANY, ipoib_unrhdr_init, NULL);

static void
ipoib_unrhdr_uninit(void *arg)
{

	if (ipoib_unrhdr != NULL) {
		struct unrhdr *hdr;

		hdr = ipoib_unrhdr;
		ipoib_unrhdr = NULL;

		delete_unrhdr(hdr);
	}
}
SYSUNINIT(ipoib_unrhdr_uninit, SI_SUB_KLD - 1, SI_ORDER_ANY, ipoib_unrhdr_uninit, NULL);

/*
 * This is for clients that have an ipoib_header in the mbuf.
 */
static void
ipoib_mtap_mb(struct ifnet *ifp, struct mbuf *mb)
{
	struct ipoib_header *ih;
	struct ether_header eh;

	ih = mtod(mb, struct ipoib_header *);
	eh.ether_type = ih->proto;
	bcopy(ih->hwaddr, &eh.ether_dhost, ETHER_ADDR_LEN);
	bzero(&eh.ether_shost, ETHER_ADDR_LEN);
	mb->m_data += sizeof(struct ipoib_header);
	mb->m_len -= sizeof(struct ipoib_header);
	bpf_mtap2(ifp->if_bpf, &eh, sizeof(eh), mb);
	mb->m_data -= sizeof(struct ipoib_header);
	mb->m_len += sizeof(struct ipoib_header);
}

void
ipoib_mtap_proto(struct ifnet *ifp, struct mbuf *mb, uint16_t proto)
{
	struct ether_header eh;

	eh.ether_type = proto;
	bzero(&eh.ether_shost, ETHER_ADDR_LEN);
	bzero(&eh.ether_dhost, ETHER_ADDR_LEN);
	bpf_mtap2(ifp->if_bpf, &eh, sizeof(eh), mb);
}

static struct ib_client ipoib_client = {
	.name   = "ipoib",
	.add    = ipoib_add_one,
	.remove = ipoib_remove_one,
	.get_net_dev_by_params = ipoib_get_net_dev_by_params,
};

int
ipoib_open(struct ipoib_dev_priv *priv)
{
	struct ifnet *dev = priv->dev;

	ipoib_dbg(priv, "bringing up interface\n");

	set_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	if (ipoib_pkey_dev_delay_open(priv))
		return 0;

	if (ipoib_ib_dev_open(priv))
		goto err_disable;

	if (ipoib_ib_dev_up(priv))
		goto err_stop;

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring up any child interfaces too */
		mutex_lock(&priv->vlan_mutex);
		list_for_each_entry(cpriv, &priv->child_intfs, list)
			if ((cpriv->dev->if_drv_flags & IFF_DRV_RUNNING) == 0)
				ipoib_open(cpriv);
		mutex_unlock(&priv->vlan_mutex);
	}
	dev->if_drv_flags |= IFF_DRV_RUNNING;
	dev->if_drv_flags &= ~IFF_DRV_OACTIVE;

	return 0;

err_stop:
	ipoib_ib_dev_stop(priv, 1);

err_disable:
	clear_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	return -EINVAL;
}

static void
ipoib_init(void *arg)
{
	struct ifnet *dev;
	struct ipoib_dev_priv *priv;

	priv = arg;
	dev = priv->dev;
	if ((dev->if_drv_flags & IFF_DRV_RUNNING) == 0)
		ipoib_open(priv);
	queue_work(ipoib_workqueue, &priv->flush_light);
}


static int
ipoib_stop(struct ipoib_dev_priv *priv)
{
	struct ifnet *dev = priv->dev;

	ipoib_dbg(priv, "stopping interface\n");

	clear_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags);

	dev->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	ipoib_ib_dev_down(priv, 0);
	ipoib_ib_dev_stop(priv, 0);

	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		struct ipoib_dev_priv *cpriv;

		/* Bring down any child interfaces too */
		mutex_lock(&priv->vlan_mutex);
		list_for_each_entry(cpriv, &priv->child_intfs, list)
			if ((cpriv->dev->if_drv_flags & IFF_DRV_RUNNING) != 0)
				ipoib_stop(cpriv);
		mutex_unlock(&priv->vlan_mutex);
	}

	return 0;
}

static int
ipoib_propagate_ifnet_mtu(struct ipoib_dev_priv *priv, int new_mtu,
    bool propagate)
{
	struct ifnet *ifp;
	struct ifreq ifr;
	int error;

	ifp = priv->dev;
	if (ifp->if_mtu == new_mtu)
		return (0);
	if (propagate) {
		strlcpy(ifr.ifr_name, if_name(ifp), IFNAMSIZ);
		ifr.ifr_mtu = new_mtu;
		CURVNET_SET(ifp->if_vnet);
		error = ifhwioctl(SIOCSIFMTU, ifp, (caddr_t)&ifr, curthread);
		CURVNET_RESTORE();
	} else {
		ifp->if_mtu = new_mtu;
		error = 0;
	}
	return (error);
}

int
ipoib_change_mtu(struct ipoib_dev_priv *priv, int new_mtu, bool propagate)
{
	int error, prev_admin_mtu;

	/* dev->if_mtu > 2K ==> connected mode */
	if (ipoib_cm_admin_enabled(priv)) {
		if (new_mtu > IPOIB_CM_MTU(ipoib_cm_max_mtu(priv)))
			return -EINVAL;

		if (new_mtu > priv->mcast_mtu)
			ipoib_warn(priv, "mtu > %d will cause multicast packet drops.\n",
				   priv->mcast_mtu);

		return (ipoib_propagate_ifnet_mtu(priv, new_mtu, propagate));
	}

	if (new_mtu > IPOIB_UD_MTU(priv->max_ib_mtu))
		return -EINVAL;

	prev_admin_mtu = priv->admin_mtu;
	priv->admin_mtu = new_mtu;
	error = ipoib_propagate_ifnet_mtu(priv, min(priv->mcast_mtu,
	    priv->admin_mtu), propagate);
	if (error == 0) {
		/* check for MTU change to avoid infinite loop */
		if (prev_admin_mtu != new_mtu)
			queue_work(ipoib_workqueue, &priv->flush_light);
	} else
		priv->admin_mtu = prev_admin_mtu;
	return (error);
}

static int
ipoib_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ipoib_dev_priv *priv = ifp->if_softc;
	struct ifaddr *ifa = (struct ifaddr *) data;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	/* check if detaching */
	if (priv == NULL || priv->gone != 0)
		return (ENXIO);

	switch (command) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
				error = -ipoib_open(priv);
		} else
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				ipoib_stop(priv);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			queue_work(ipoib_workqueue, &priv->restart_task);
		break;
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;

		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			ifp->if_init(ifp->if_softc);	/* before arpwhohas */
			arp_ifinit(ifp, ifa);
			break;
#endif
		default:
			ifp->if_init(ifp->if_softc);
			break;
		}
		break;

	case SIOCGIFADDR:
			bcopy(IF_LLADDR(ifp), &ifr->ifr_addr.sa_data[0],
                            INFINIBAND_ALEN);
		break;

	case SIOCSIFMTU:
		/*
		 * Set the interface MTU.
		 */
		error = -ipoib_change_mtu(priv, ifr->ifr_mtu, false);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}


static struct ipoib_path *
__path_find(struct ipoib_dev_priv *priv, void *gid)
{
	struct rb_node *n = priv->path_tree.rb_node;
	struct ipoib_path *path;
	int ret;

	while (n) {
		path = rb_entry(n, struct ipoib_path, rb_node);

		ret = memcmp(gid, path->pathrec.dgid.raw,
			     sizeof (union ib_gid));

		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else
			return path;
	}

	return NULL;
}

static int
__path_add(struct ipoib_dev_priv *priv, struct ipoib_path *path)
{
	struct rb_node **n = &priv->path_tree.rb_node;
	struct rb_node *pn = NULL;
	struct ipoib_path *tpath;
	int ret;

	while (*n) {
		pn = *n;
		tpath = rb_entry(pn, struct ipoib_path, rb_node);

		ret = memcmp(path->pathrec.dgid.raw, tpath->pathrec.dgid.raw,
			     sizeof (union ib_gid));
		if (ret < 0)
			n = &pn->rb_left;
		else if (ret > 0)
			n = &pn->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&path->rb_node, pn, n);
	rb_insert_color(&path->rb_node, &priv->path_tree);

	list_add_tail(&path->list, &priv->path_list);

	return 0;
}

void
ipoib_path_free(struct ipoib_dev_priv *priv, struct ipoib_path *path)
{

	_IF_DRAIN(&path->queue);

	if (path->ah)
		ipoib_put_ah(path->ah);
	if (ipoib_cm_get(path))
		ipoib_cm_destroy_tx(ipoib_cm_get(path));

	kfree(path);
}

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG

struct ipoib_path_iter *
ipoib_path_iter_init(struct ipoib_dev_priv *priv)
{
	struct ipoib_path_iter *iter;

	iter = kmalloc(sizeof *iter, GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->priv = priv;
	memset(iter->path.pathrec.dgid.raw, 0, 16);

	if (ipoib_path_iter_next(iter)) {
		kfree(iter);
		return NULL;
	}

	return iter;
}

int
ipoib_path_iter_next(struct ipoib_path_iter *iter)
{
	struct ipoib_dev_priv *priv = iter->priv;
	struct rb_node *n;
	struct ipoib_path *path;
	int ret = 1;

	spin_lock_irq(&priv->lock);

	n = rb_first(&priv->path_tree);

	while (n) {
		path = rb_entry(n, struct ipoib_path, rb_node);

		if (memcmp(iter->path.pathrec.dgid.raw, path->pathrec.dgid.raw,
			   sizeof (union ib_gid)) < 0) {
			iter->path = *path;
			ret = 0;
			break;
		}

		n = rb_next(n);
	}

	spin_unlock_irq(&priv->lock);

	return ret;
}

void
ipoib_path_iter_read(struct ipoib_path_iter *iter, struct ipoib_path *path)
{
	*path = iter->path;
}

#endif /* CONFIG_INFINIBAND_IPOIB_DEBUG */

void
ipoib_mark_paths_invalid(struct ipoib_dev_priv *priv)
{
	struct ipoib_path *path, *tp;

	spin_lock_irq(&priv->lock);

	list_for_each_entry_safe(path, tp, &priv->path_list, list) {
		ipoib_dbg(priv, "mark path LID 0x%04x GID %16D invalid\n",
			be16_to_cpu(path->pathrec.dlid),
			path->pathrec.dgid.raw, ":");
		path->valid =  0;
	}

	spin_unlock_irq(&priv->lock);
}

void
ipoib_flush_paths(struct ipoib_dev_priv *priv)
{
	struct ipoib_path *path, *tp;
	LIST_HEAD(remove_list);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	list_splice_init(&priv->path_list, &remove_list);

	list_for_each_entry(path, &remove_list, list)
		rb_erase(&path->rb_node, &priv->path_tree);

	list_for_each_entry_safe(path, tp, &remove_list, list) {
		if (path->query)
			ib_sa_cancel_query(path->query_id, path->query);
		spin_unlock_irqrestore(&priv->lock, flags);
		wait_for_completion(&path->done);
		ipoib_path_free(priv, path);
		spin_lock_irqsave(&priv->lock, flags);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void
path_rec_completion(int status, struct ib_sa_path_rec *pathrec, void *path_ptr)
{
	struct ipoib_path *path = path_ptr;
	struct ipoib_dev_priv *priv = path->priv;
	struct ifnet *dev = priv->dev;
	struct ipoib_ah *ah = NULL;
	struct ipoib_ah *old_ah = NULL;
	struct ifqueue mbqueue;
	struct mbuf *mb;
	unsigned long flags;

	if (!status)
		ipoib_dbg(priv, "PathRec LID 0x%04x for GID %16D\n",
			  be16_to_cpu(pathrec->dlid), pathrec->dgid.raw, ":");
	else
		ipoib_dbg(priv, "PathRec status %d for GID %16D\n",
			  status, path->pathrec.dgid.raw, ":");

	bzero(&mbqueue, sizeof(mbqueue));

	if (!status) {
		struct ib_ah_attr av;

		if (!ib_init_ah_from_path(priv->ca, priv->port, pathrec, &av))
			ah = ipoib_create_ah(priv, priv->pd, &av);
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (ah) {
		path->pathrec = *pathrec;

		old_ah   = path->ah;
		path->ah = ah;

		ipoib_dbg(priv, "created address handle %p for LID 0x%04x, SL %d\n",
			  ah, be16_to_cpu(pathrec->dlid), pathrec->sl);

		for (;;) {
			_IF_DEQUEUE(&path->queue, mb);
			if (mb == NULL)
				break;
			_IF_ENQUEUE(&mbqueue, mb);
		}

#ifdef CONFIG_INFINIBAND_IPOIB_CM
		if (ipoib_cm_enabled(priv, path->hwaddr) && !ipoib_cm_get(path))
			ipoib_cm_set(path, ipoib_cm_create_tx(priv, path));
#endif

		path->valid = 1;
	}

	path->query = NULL;
	complete(&path->done);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (old_ah)
		ipoib_put_ah(old_ah);

	for (;;) {
		_IF_DEQUEUE(&mbqueue, mb);
		if (mb == NULL)
			break;
		mb->m_pkthdr.rcvif = dev;
		if (dev->if_transmit(dev, mb))
			ipoib_warn(priv, "dev_queue_xmit failed "
				   "to requeue packet\n");
	}
}

static struct ipoib_path *
path_rec_create(struct ipoib_dev_priv *priv, uint8_t *hwaddr)
{
	struct ipoib_path *path;

	if (!priv->broadcast)
		return NULL;

	path = kzalloc(sizeof *path, GFP_ATOMIC);
	if (!path)
		return NULL;

	path->priv = priv;

	bzero(&path->queue, sizeof(path->queue));

#ifdef CONFIG_INFINIBAND_IPOIB_CM
	memcpy(&path->hwaddr, hwaddr, INFINIBAND_ALEN);
#endif
	memcpy(path->pathrec.dgid.raw, &hwaddr[4], sizeof (union ib_gid));
	path->pathrec.sgid	    = priv->local_gid;
	path->pathrec.pkey	    = cpu_to_be16(priv->pkey);
	path->pathrec.numb_path     = 1;
	path->pathrec.traffic_class = priv->broadcast->mcmember.traffic_class;

	return path;
}

static int
path_rec_start(struct ipoib_dev_priv *priv, struct ipoib_path *path)
{
	struct ifnet *dev = priv->dev;

	ib_sa_comp_mask comp_mask = IB_SA_PATH_REC_MTU_SELECTOR | IB_SA_PATH_REC_MTU;
	struct ib_sa_path_rec p_rec;

	p_rec = path->pathrec;
	p_rec.mtu_selector = IB_SA_GT;

	switch (roundup_pow_of_two(dev->if_mtu + IPOIB_ENCAP_LEN)) {
	case 512:
		p_rec.mtu = IB_MTU_256;
		break;
	case 1024:
		p_rec.mtu = IB_MTU_512;
		break;
	case 2048:
		p_rec.mtu = IB_MTU_1024;
		break;
	case 4096:
		p_rec.mtu = IB_MTU_2048;
		break;
	default:
		/* Wildcard everything */
		comp_mask = 0;
		p_rec.mtu = 0;
		p_rec.mtu_selector = 0;
	}

	ipoib_dbg(priv, "Start path record lookup for %16D MTU > %d\n",
		  p_rec.dgid.raw, ":",
		  comp_mask ? ib_mtu_enum_to_int(p_rec.mtu) : 0);

	init_completion(&path->done);

	path->query_id =
		ib_sa_path_rec_get(&ipoib_sa_client, priv->ca, priv->port,
				   &p_rec, comp_mask		|
				   IB_SA_PATH_REC_DGID		|
				   IB_SA_PATH_REC_SGID		|
				   IB_SA_PATH_REC_NUMB_PATH	|
				   IB_SA_PATH_REC_TRAFFIC_CLASS |
				   IB_SA_PATH_REC_PKEY,
				   1000, GFP_ATOMIC,
				   path_rec_completion,
				   path, &path->query);
	if (path->query_id < 0) {
		ipoib_warn(priv, "ib_sa_path_rec_get failed: %d\n", path->query_id);
		path->query = NULL;
		complete(&path->done);
		return path->query_id;
	}

	return 0;
}

static void
ipoib_unicast_send(struct mbuf *mb, struct ipoib_dev_priv *priv, struct ipoib_header *eh)
{
	struct ipoib_path *path;

	path = __path_find(priv, eh->hwaddr + 4);
	if (!path || !path->valid) {
		int new_path = 0;

		if (!path) {
			path = path_rec_create(priv, eh->hwaddr);
			new_path = 1;
		}
		if (path) {
			if (_IF_QLEN(&path->queue) < IPOIB_MAX_PATH_REC_QUEUE)
				_IF_ENQUEUE(&path->queue, mb);
			else {
				if_inc_counter(priv->dev, IFCOUNTER_OERRORS, 1);
				m_freem(mb);
			}

			if (!path->query && path_rec_start(priv, path)) {
				spin_unlock_irqrestore(&priv->lock, flags);
				if (new_path)
					ipoib_path_free(priv, path);
				return;
			} else
				__path_add(priv, path);
		} else {
			if_inc_counter(priv->dev, IFCOUNTER_OERRORS, 1);
			m_freem(mb);
		}

		return;
	}

	if (ipoib_cm_get(path) && ipoib_cm_up(path)) {
		ipoib_cm_send(priv, mb, ipoib_cm_get(path));
	} else if (path->ah) {
		ipoib_send(priv, mb, path->ah, IPOIB_QPN(eh->hwaddr));
	} else if ((path->query || !path_rec_start(priv, path)) &&
		    path->queue.ifq_len < IPOIB_MAX_PATH_REC_QUEUE) {
		_IF_ENQUEUE(&path->queue, mb);
	} else {
		if_inc_counter(priv->dev, IFCOUNTER_OERRORS, 1);
		m_freem(mb);
	}
}

static int
ipoib_send_one(struct ipoib_dev_priv *priv, struct mbuf *mb)
{
	struct ipoib_header *eh;

	eh = mtod(mb, struct ipoib_header *);
	if (IPOIB_IS_MULTICAST(eh->hwaddr)) {
		/* Add in the P_Key for multicast*/
		eh->hwaddr[8] = (priv->pkey >> 8) & 0xff;
		eh->hwaddr[9] = priv->pkey & 0xff;

		ipoib_mcast_send(priv, eh->hwaddr + 4, mb);
	} else
		ipoib_unicast_send(mb, priv, eh);

	return 0;
}


static void
_ipoib_start(struct ifnet *dev, struct ipoib_dev_priv *priv)
{
	struct mbuf *mb;

	if ((dev->if_drv_flags & (IFF_DRV_RUNNING|IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	spin_lock(&priv->lock);
	while (!IFQ_DRV_IS_EMPTY(&dev->if_snd) &&
	    (dev->if_drv_flags & IFF_DRV_OACTIVE) == 0) {
		IFQ_DRV_DEQUEUE(&dev->if_snd, mb);
		if (mb == NULL)
			break;
		IPOIB_MTAP(dev, mb);
		ipoib_send_one(priv, mb);
	}
	spin_unlock(&priv->lock);
}

static void
ipoib_start(struct ifnet *dev)
{
	_ipoib_start(dev, dev->if_softc);
}

static void
ipoib_vlan_start(struct ifnet *dev)
{
	struct ipoib_dev_priv *priv;
	struct mbuf *mb;

	priv = VLAN_COOKIE(dev);
	if (priv != NULL)
		return _ipoib_start(dev, priv);
	while (!IFQ_DRV_IS_EMPTY(&dev->if_snd)) {
		IFQ_DRV_DEQUEUE(&dev->if_snd, mb);
		if (mb == NULL)
			break;
		m_freem(mb);
		if_inc_counter(dev, IFCOUNTER_OERRORS, 1);
	}
}

int
ipoib_dev_init(struct ipoib_dev_priv *priv, struct ib_device *ca, int port)
{

	/* Allocate RX/TX "rings" to hold queued mbs */
	priv->rx_ring =	kzalloc(ipoib_recvq_size * sizeof *priv->rx_ring,
				GFP_KERNEL);
	if (!priv->rx_ring) {
		printk(KERN_WARNING "%s: failed to allocate RX ring (%d entries)\n",
		       ca->name, ipoib_recvq_size);
		goto out;
	}

	priv->tx_ring = kzalloc(ipoib_sendq_size * sizeof *priv->tx_ring, GFP_KERNEL);
	if (!priv->tx_ring) {
		printk(KERN_WARNING "%s: failed to allocate TX ring (%d entries)\n",
		       ca->name, ipoib_sendq_size);
		goto out_rx_ring_cleanup;
	}
	memset(priv->tx_ring, 0, ipoib_sendq_size * sizeof *priv->tx_ring);

	/* priv->tx_head, tx_tail & tx_outstanding are already 0 */

	if (ipoib_ib_dev_init(priv, ca, port))
		goto out_tx_ring_cleanup;

	return 0;

out_tx_ring_cleanup:
	kfree(priv->tx_ring);

out_rx_ring_cleanup:
	kfree(priv->rx_ring);

out:
	return -ENOMEM;
}

static void
ipoib_detach(struct ipoib_dev_priv *priv)
{
	struct ifnet *dev;

	dev = priv->dev;
	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		priv->gone = 1;
		bpfdetach(dev);
		if_detach(dev);
		if_free(dev);
		free_unr(ipoib_unrhdr, priv->unit);
	} else
		VLAN_SETCOOKIE(priv->dev, NULL);

	free(priv, M_TEMP);
}

void
ipoib_dev_cleanup(struct ipoib_dev_priv *priv)
{
	struct ipoib_dev_priv *cpriv, *tcpriv;

	/* Delete any child interfaces first */
	list_for_each_entry_safe(cpriv, tcpriv, &priv->child_intfs, list) {
		ipoib_dev_cleanup(cpriv);
		ipoib_detach(cpriv);
	}

	ipoib_ib_dev_cleanup(priv);

	kfree(priv->rx_ring);
	kfree(priv->tx_ring);

	priv->rx_ring = NULL;
	priv->tx_ring = NULL;
}

static struct ipoib_dev_priv *
ipoib_priv_alloc(void)
{
	struct ipoib_dev_priv *priv;

	priv = malloc(sizeof(struct ipoib_dev_priv), M_TEMP, M_ZERO|M_WAITOK);
	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->drain_lock);
	mutex_init(&priv->vlan_mutex);
	INIT_LIST_HEAD(&priv->path_list);
	INIT_LIST_HEAD(&priv->child_intfs);
	INIT_LIST_HEAD(&priv->dead_ahs);
	INIT_LIST_HEAD(&priv->multicast_list);
	INIT_DELAYED_WORK(&priv->pkey_poll_task, ipoib_pkey_poll);
	INIT_DELAYED_WORK(&priv->mcast_task,   ipoib_mcast_join_task);
	INIT_WORK(&priv->carrier_on_task, ipoib_mcast_carrier_on_task);
	INIT_WORK(&priv->flush_light,   ipoib_ib_dev_flush_light);
	INIT_WORK(&priv->flush_normal,   ipoib_ib_dev_flush_normal);
	INIT_WORK(&priv->flush_heavy,   ipoib_ib_dev_flush_heavy);
	INIT_WORK(&priv->restart_task, ipoib_mcast_restart_task);
	INIT_DELAYED_WORK(&priv->ah_reap_task, ipoib_reap_ah);
	memcpy(priv->broadcastaddr, ipv4_bcast_addr, INFINIBAND_ALEN);

	return (priv);
}

struct ipoib_dev_priv *
ipoib_intf_alloc(const char *name)
{
	struct ipoib_dev_priv *priv;
	struct sockaddr_dl *sdl;
	struct ifnet *dev;

	priv = ipoib_priv_alloc();
	dev = priv->dev = if_alloc(IFT_INFINIBAND);
	if (!dev) {
		free(priv, M_TEMP);
		return NULL;
	}
	dev->if_softc = priv;
	priv->unit = alloc_unr(ipoib_unrhdr);
	if (priv->unit == -1) {
		if_free(dev);
		free(priv, M_TEMP);
		return NULL;
	}
	if_initname(dev, name, priv->unit);
	dev->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	dev->if_addrlen = INFINIBAND_ALEN;
	dev->if_hdrlen = IPOIB_HEADER_LEN;
	if_attach(dev);
	dev->if_init = ipoib_init;
	dev->if_ioctl = ipoib_ioctl;
	dev->if_start = ipoib_start;
	dev->if_output = ipoib_output;
	dev->if_input = ipoib_input;
	dev->if_resolvemulti = ipoib_resolvemulti;
	dev->if_baudrate = IF_Gbps(10);
	dev->if_broadcastaddr = priv->broadcastaddr;
	dev->if_snd.ifq_maxlen = ipoib_sendq_size * 2;
	sdl = (struct sockaddr_dl *)dev->if_addr->ifa_addr;
	sdl->sdl_type = IFT_INFINIBAND;
	sdl->sdl_alen = dev->if_addrlen;
	priv->dev = dev;
	if_link_state_change(dev, LINK_STATE_DOWN);
	bpfattach(dev, DLT_EN10MB, ETHER_HDR_LEN);

	return dev->if_softc;
}

int
ipoib_set_dev_features(struct ipoib_dev_priv *priv, struct ib_device *hca)
{
	struct ib_device_attr *device_attr = &hca->attrs;

	priv->hca_caps = device_attr->device_cap_flags;

	priv->dev->if_hwassist = 0;
	priv->dev->if_capabilities = 0;

#ifndef CONFIG_INFINIBAND_IPOIB_CM
	if (priv->hca_caps & IB_DEVICE_UD_IP_CSUM) {
		set_bit(IPOIB_FLAG_CSUM, &priv->flags);
		priv->dev->if_hwassist = CSUM_IP | CSUM_TCP | CSUM_UDP;
		priv->dev->if_capabilities = IFCAP_HWCSUM | IFCAP_VLAN_HWCSUM;
	}

#if 0
	if (priv->dev->features & NETIF_F_SG && priv->hca_caps & IB_DEVICE_UD_TSO) {
		priv->dev->if_capabilities |= IFCAP_TSO4;
		priv->dev->if_hwassist |= CSUM_TSO;
	}
#endif
#endif
	priv->dev->if_capabilities |=
	    IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_MTU | IFCAP_LINKSTATE;
	priv->dev->if_capenable = priv->dev->if_capabilities;

	return 0;
}


static struct ifnet *
ipoib_add_port(const char *format, struct ib_device *hca, u8 port)
{
	struct ipoib_dev_priv *priv;
	struct ib_port_attr attr;
	int result = -ENOMEM;

	priv = ipoib_intf_alloc(format);
	if (!priv)
		goto alloc_mem_failed;

	if (!ib_query_port(hca, port, &attr))
		priv->max_ib_mtu = ib_mtu_enum_to_int(attr.max_mtu);
	else {
		printk(KERN_WARNING "%s: ib_query_port %d failed\n",
		       hca->name, port);
		goto device_init_failed;
	}

	/* MTU will be reset when mcast join happens */
	priv->dev->if_mtu = IPOIB_UD_MTU(priv->max_ib_mtu);
	priv->mcast_mtu = priv->admin_mtu = priv->dev->if_mtu;

	result = ib_query_pkey(hca, port, 0, &priv->pkey);
	if (result) {
		printk(KERN_WARNING "%s: ib_query_pkey port %d failed (ret = %d)\n",
		       hca->name, port, result);
		goto device_init_failed;
	}

	if (ipoib_set_dev_features(priv, hca))
		goto device_init_failed;

	/*
	 * Set the full membership bit, so that we join the right
	 * broadcast group, etc.
	 */
	priv->pkey |= 0x8000;

	priv->broadcastaddr[8] = priv->pkey >> 8;
	priv->broadcastaddr[9] = priv->pkey & 0xff;

	result = ib_query_gid(hca, port, 0, &priv->local_gid, NULL);
	if (result) {
		printk(KERN_WARNING "%s: ib_query_gid port %d failed (ret = %d)\n",
		       hca->name, port, result);
		goto device_init_failed;
	}
	memcpy(IF_LLADDR(priv->dev) + 4, priv->local_gid.raw, sizeof (union ib_gid));

	result = ipoib_dev_init(priv, hca, port);
	if (result < 0) {
		printk(KERN_WARNING "%s: failed to initialize port %d (ret = %d)\n",
		       hca->name, port, result);
		goto device_init_failed;
	}
	if (ipoib_cm_admin_enabled(priv))
		priv->dev->if_mtu = IPOIB_CM_MTU(ipoib_cm_max_mtu(priv));

	INIT_IB_EVENT_HANDLER(&priv->event_handler,
			      priv->ca, ipoib_event);
	result = ib_register_event_handler(&priv->event_handler);
	if (result < 0) {
		printk(KERN_WARNING "%s: ib_register_event_handler failed for "
		       "port %d (ret = %d)\n",
		       hca->name, port, result);
		goto event_failed;
	}
	if_printf(priv->dev, "Attached to %s port %d\n", hca->name, port);

	return priv->dev;

event_failed:
	ipoib_dev_cleanup(priv);

device_init_failed:
	ipoib_detach(priv);

alloc_mem_failed:
	return ERR_PTR(result);
}

static void
ipoib_add_one(struct ib_device *device)
{
	struct list_head *dev_list;
	struct ifnet *dev;
	struct ipoib_dev_priv *priv;
	int s, e, p;

	if (rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	dev_list = kmalloc(sizeof *dev_list, GFP_KERNEL);
	if (!dev_list)
		return;

	INIT_LIST_HEAD(dev_list);

	if (device->node_type == RDMA_NODE_IB_SWITCH) {
		s = 0;
		e = 0;
	} else {
		s = 1;
		e = device->phys_port_cnt;
	}

	for (p = s; p <= e; ++p) {
		if (rdma_port_get_link_layer(device, p) != IB_LINK_LAYER_INFINIBAND)
			continue;
		dev = ipoib_add_port("ib", device, p);
		if (!IS_ERR(dev)) {
			priv = dev->if_softc;
			list_add_tail(&priv->list, dev_list);
		}
	}

	ib_set_client_data(device, &ipoib_client, dev_list);
}

static void
ipoib_remove_one(struct ib_device *device, void *client_data)
{
	struct ipoib_dev_priv *priv, *tmp;
	struct list_head *dev_list = client_data;

	if (!dev_list)
		return;

	if (rdma_node_get_transport(device->node_type) != RDMA_TRANSPORT_IB)
		return;

	list_for_each_entry_safe(priv, tmp, dev_list, list) {
		if (rdma_port_get_link_layer(device, priv->port) != IB_LINK_LAYER_INFINIBAND)
			continue;

		ipoib_stop(priv);

		ib_unregister_event_handler(&priv->event_handler);

		/* dev_change_flags(priv->dev, priv->dev->flags & ~IFF_UP); */

		flush_workqueue(ipoib_workqueue);

		ipoib_dev_cleanup(priv);
		ipoib_detach(priv);
	}

	kfree(dev_list);
}

static int
ipoib_match_dev_addr(const struct sockaddr *addr, struct net_device *dev)
{
	struct epoch_tracker et;
	struct ifaddr *ifa;
	int retval = 0;

	CURVNET_SET(dev->if_vnet);
	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ifa, &dev->if_addrhead, ifa_link) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != addr->sa_family ||
		    ifa->ifa_addr->sa_len != addr->sa_len) {
			continue;
		}
		if (memcmp(ifa->ifa_addr, addr, addr->sa_len) == 0) {
			retval = 1;
			break;
		}
	}
	NET_EPOCH_EXIT(et);
	CURVNET_RESTORE();

	return (retval);
}

/*
 * ipoib_match_gid_pkey_addr - returns the number of IPoIB netdevs on
 * top a given ipoib device matching a pkey_index and address, if one
 * exists.
 *
 * @found_net_dev: contains a matching net_device if the return value
 * >= 1, with a reference held.
 */
static int
ipoib_match_gid_pkey_addr(struct ipoib_dev_priv *priv,
    const union ib_gid *gid, u16 pkey_index, const struct sockaddr *addr,
    struct net_device **found_net_dev)
{
	struct ipoib_dev_priv *child_priv;
	int matches = 0;

	if (priv->pkey_index == pkey_index &&
	    (!gid || !memcmp(gid, &priv->local_gid, sizeof(*gid)))) {
		if (addr == NULL || ipoib_match_dev_addr(addr, priv->dev) != 0) {
			if (*found_net_dev == NULL) {
				struct net_device *net_dev;

				if (priv->parent != NULL)
					net_dev = priv->parent;
				else
					net_dev = priv->dev;
				*found_net_dev = net_dev;
				dev_hold(net_dev);
			}
			matches++;
		}
	}

	/* Check child interfaces */
	mutex_lock(&priv->vlan_mutex);
	list_for_each_entry(child_priv, &priv->child_intfs, list) {
		matches += ipoib_match_gid_pkey_addr(child_priv, gid,
		    pkey_index, addr, found_net_dev);
		if (matches > 1)
			break;
	}
	mutex_unlock(&priv->vlan_mutex);

	return matches;
}

/*
 * __ipoib_get_net_dev_by_params - returns the number of matching
 * net_devs found (between 0 and 2). Also return the matching
 * net_device in the @net_dev parameter, holding a reference to the
 * net_device, if the number of matches >= 1
 */
static int
__ipoib_get_net_dev_by_params(struct list_head *dev_list, u8 port,
    u16 pkey_index, const union ib_gid *gid,
    const struct sockaddr *addr, struct net_device **net_dev)
{
	struct ipoib_dev_priv *priv;
	int matches = 0;

	*net_dev = NULL;

	list_for_each_entry(priv, dev_list, list) {
		if (priv->port != port)
			continue;

		matches += ipoib_match_gid_pkey_addr(priv, gid, pkey_index,
		    addr, net_dev);

		if (matches > 1)
			break;
	}

	return matches;
}

static struct net_device *
ipoib_get_net_dev_by_params(struct ib_device *dev, u8 port, u16 pkey,
    const union ib_gid *gid, const struct sockaddr *addr, void *client_data)
{
	struct net_device *net_dev;
	struct list_head *dev_list = client_data;
	u16 pkey_index;
	int matches;
	int ret;

	if (!rdma_protocol_ib(dev, port))
		return NULL;

	ret = ib_find_cached_pkey(dev, port, pkey, &pkey_index);
	if (ret)
		return NULL;

	if (!dev_list)
		return NULL;

	/* See if we can find a unique device matching the L2 parameters */
	matches = __ipoib_get_net_dev_by_params(dev_list, port, pkey_index,
						gid, NULL, &net_dev);

	switch (matches) {
	case 0:
		return NULL;
	case 1:
		return net_dev;
	}

	dev_put(net_dev);

	/* Couldn't find a unique device with L2 parameters only. Use L3
	 * address to uniquely match the net device */
	matches = __ipoib_get_net_dev_by_params(dev_list, port, pkey_index,
						gid, addr, &net_dev);
	switch (matches) {
	case 0:
		return NULL;
	default:
		dev_warn_ratelimited(&dev->dev,
				     "duplicate IP address detected\n");
		/* Fall through */
	case 1:
		return net_dev;
	}
}

static void
ipoib_config_vlan(void *arg, struct ifnet *ifp, u_int16_t vtag)
{
	struct ipoib_dev_priv *parent;
	struct ipoib_dev_priv *priv;
	struct ifnet *dev;
	uint16_t pkey;
	int error;

	if (ifp->if_type != IFT_INFINIBAND)
		return;
	dev = VLAN_DEVAT(ifp, vtag);
	if (dev == NULL)
		return;
	priv = NULL;
	error = 0;
	parent = ifp->if_softc;
	/* We only support 15 bits of pkey. */
	if (vtag & 0x8000)
		return;
	pkey = vtag | 0x8000;	/* Set full membership bit. */
	if (pkey == parent->pkey)
		return;
	/* Check for dups */
	mutex_lock(&parent->vlan_mutex);
	list_for_each_entry(priv, &parent->child_intfs, list) {
		if (priv->pkey == pkey) {
			priv = NULL;
			error = EBUSY;
			goto out;
		}
	}
	priv = ipoib_priv_alloc();
	priv->dev = dev;
	priv->max_ib_mtu = parent->max_ib_mtu;
	priv->mcast_mtu = priv->admin_mtu = parent->dev->if_mtu;
	set_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags);
	error = ipoib_set_dev_features(priv, parent->ca);
	if (error)
		goto out;
	priv->pkey = pkey;
	priv->broadcastaddr[8] = pkey >> 8;
	priv->broadcastaddr[9] = pkey & 0xff;
	dev->if_broadcastaddr = priv->broadcastaddr;
	error = ipoib_dev_init(priv, parent->ca, parent->port);
	if (error)
		goto out;
	priv->parent = parent->dev;
	list_add_tail(&priv->list, &parent->child_intfs);
	VLAN_SETCOOKIE(dev, priv);
	dev->if_start = ipoib_vlan_start;
	dev->if_drv_flags &= ~IFF_DRV_RUNNING;
	dev->if_hdrlen = IPOIB_HEADER_LEN;
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		ipoib_open(priv);
	mutex_unlock(&parent->vlan_mutex);
	return;
out:
	mutex_unlock(&parent->vlan_mutex);
	if (priv)
		free(priv, M_TEMP);
	if (error)
		ipoib_warn(parent,
		    "failed to initialize subinterface: device %s, port %d vtag 0x%X",
		    parent->ca->name, parent->port, vtag);
	return;
}

static void
ipoib_unconfig_vlan(void *arg, struct ifnet *ifp, u_int16_t vtag)
{
	struct ipoib_dev_priv *parent;
	struct ipoib_dev_priv *priv;
	struct ifnet *dev;
	uint16_t pkey;

	if (ifp->if_type != IFT_INFINIBAND)
		return;

	dev = VLAN_DEVAT(ifp, vtag);
	if (dev)
		VLAN_SETCOOKIE(dev, NULL);
	pkey = vtag | 0x8000;
	parent = ifp->if_softc;
	mutex_lock(&parent->vlan_mutex);
	list_for_each_entry(priv, &parent->child_intfs, list) {
		if (priv->pkey == pkey) {
			ipoib_dev_cleanup(priv);
			list_del(&priv->list);
			break;
		}
	}
	mutex_unlock(&parent->vlan_mutex);
}

eventhandler_tag ipoib_vlan_attach;
eventhandler_tag ipoib_vlan_detach;

static int __init
ipoib_init_module(void)
{
	int ret;

	ipoib_recvq_size = roundup_pow_of_two(ipoib_recvq_size);
	ipoib_recvq_size = min(ipoib_recvq_size, IPOIB_MAX_QUEUE_SIZE);
	ipoib_recvq_size = max(ipoib_recvq_size, IPOIB_MIN_QUEUE_SIZE);

	ipoib_sendq_size = roundup_pow_of_two(ipoib_sendq_size);
	ipoib_sendq_size = min(ipoib_sendq_size, IPOIB_MAX_QUEUE_SIZE);
	ipoib_sendq_size = max(ipoib_sendq_size, max(2 * MAX_SEND_CQE,
						     IPOIB_MIN_QUEUE_SIZE));
#ifdef CONFIG_INFINIBAND_IPOIB_CM
	ipoib_max_conn_qp = min(ipoib_max_conn_qp, IPOIB_CM_MAX_CONN_QP);
#endif

	ipoib_vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
		ipoib_config_vlan, NULL, EVENTHANDLER_PRI_FIRST);
	ipoib_vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
		ipoib_unconfig_vlan, NULL, EVENTHANDLER_PRI_FIRST);

	/*
	 * We create our own workqueue mainly because we want to be
	 * able to flush it when devices are being removed.  We can't
	 * use schedule_work()/flush_scheduled_work() because both
	 * unregister_netdev() and linkwatch_event take the rtnl lock,
	 * so flush_scheduled_work() can deadlock during device
	 * removal.
	 */
	ipoib_workqueue = create_singlethread_workqueue("ipoib");
	if (!ipoib_workqueue) {
		ret = -ENOMEM;
		goto err_fs;
	}

	ib_sa_register_client(&ipoib_sa_client);

	ret = ib_register_client(&ipoib_client);
	if (ret)
		goto err_sa;

	return 0;

err_sa:
	ib_sa_unregister_client(&ipoib_sa_client);
	destroy_workqueue(ipoib_workqueue);

err_fs:
	return ret;
}

static void __exit
ipoib_cleanup_module(void)
{

	EVENTHANDLER_DEREGISTER(vlan_config, ipoib_vlan_attach);
	EVENTHANDLER_DEREGISTER(vlan_unconfig, ipoib_vlan_detach);
	ib_unregister_client(&ipoib_client);
	ib_sa_unregister_client(&ipoib_sa_client);
	destroy_workqueue(ipoib_workqueue);
}

/*
 * Infiniband output routine.
 */
static int
ipoib_output(struct ifnet *ifp, struct mbuf *m,
	const struct sockaddr *dst, struct route *ro)
{
	u_char edst[INFINIBAND_ALEN];
#if defined(INET) || defined(INET6)
	struct llentry *lle = NULL;
#endif
	struct ipoib_header *eh;
	int error = 0, is_gw = 0;
	short type;

	if (ro != NULL)
		is_gw = (ro->ro_flags & RT_HAS_GW) != 0;
#ifdef MAC
	error = mac_ifnet_check_transmit(ifp, m);
	if (error)
		goto bad;
#endif

	M_PROFILE(m);
	if (ifp->if_flags & IFF_MONITOR) {
		error = ENETDOWN;
		goto bad;
	}
	if (!((ifp->if_flags & IFF_UP) &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING))) {
		error = ENETDOWN;
		goto bad;
	}

	switch (dst->sa_family) {
#ifdef INET
	case AF_INET:
		if (lle != NULL && (lle->la_flags & LLE_VALID))
			memcpy(edst, lle->ll_addr, sizeof(edst));
		else if (m->m_flags & M_MCAST)
			ip_ib_mc_map(((struct sockaddr_in *)dst)->sin_addr.s_addr, ifp->if_broadcastaddr, edst);
		else
			error = arpresolve(ifp, is_gw, m, dst, edst, NULL, NULL);
		if (error)
			return (error == EWOULDBLOCK ? 0 : error);
		type = htons(ETHERTYPE_IP);
		break;
	case AF_ARP:
	{
		struct arphdr *ah;
		ah = mtod(m, struct arphdr *);
		ah->ar_hrd = htons(ARPHRD_INFINIBAND);

		switch(ntohs(ah->ar_op)) {
		case ARPOP_REVREQUEST:
		case ARPOP_REVREPLY:
			type = htons(ETHERTYPE_REVARP);
			break;
		case ARPOP_REQUEST:
		case ARPOP_REPLY:
		default:
			type = htons(ETHERTYPE_ARP);
			break;
		}

		if (m->m_flags & M_BCAST)
			bcopy(ifp->if_broadcastaddr, edst, INFINIBAND_ALEN);
		else
			bcopy(ar_tha(ah), edst, INFINIBAND_ALEN);

	}
	break;
#endif
#ifdef INET6
	case AF_INET6:
		if (lle != NULL && (lle->la_flags & LLE_VALID))
			memcpy(edst, lle->ll_addr, sizeof(edst));
		else if (m->m_flags & M_MCAST)
			ipv6_ib_mc_map(&((struct sockaddr_in6 *)dst)->sin6_addr, ifp->if_broadcastaddr, edst);
		else
			error = nd6_resolve(ifp, is_gw, m, dst, edst, NULL, NULL);
		if (error)
			return error;
		type = htons(ETHERTYPE_IPV6);
		break;
#endif

	default:
		if_printf(ifp, "can't handle af%d\n", dst->sa_family);
		error = EAFNOSUPPORT;
		goto bad;
	}

	/*
	 * Add local net header.  If no space in first mbuf,
	 * allocate another.
	 */
	M_PREPEND(m, IPOIB_HEADER_LEN, M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto bad;
	}
	eh = mtod(m, struct ipoib_header *);
	(void)memcpy(&eh->proto, &type, sizeof(eh->proto));
	(void)memcpy(&eh->hwaddr, edst, sizeof (edst));

	/*
	 * Queue message on interface, update output statistics if
	 * successful, and start output if interface not yet active.
	 */
	return ((ifp->if_transmit)(ifp, m));
bad:
	if (m != NULL)
		m_freem(m);
	return (error);
}

/*
 * Upper layer processing for a received Infiniband packet.
 */
void
ipoib_demux(struct ifnet *ifp, struct mbuf *m, u_short proto)
{
	int isr;

#ifdef MAC
	/*
	 * Tag the mbuf with an appropriate MAC label before any other
	 * consumers can get to it.
	 */
	mac_ifnet_create_mbuf(ifp, m);
#endif
	/* Allow monitor mode to claim this frame, after stats are updated. */
	if (ifp->if_flags & IFF_MONITOR) {
		if_printf(ifp, "discard frame at IFF_MONITOR\n");
		m_freem(m);
		return;
	}
	/*
	 * Dispatch frame to upper layer.
	 */
	switch (proto) {
#ifdef INET
	case ETHERTYPE_IP:
		isr = NETISR_IP;
		break;

	case ETHERTYPE_ARP:
		if (ifp->if_flags & IFF_NOARP) {
			/* Discard packet if ARP is disabled on interface */
			m_freem(m);
			return;
		}
		isr = NETISR_ARP;
		break;
#endif
#ifdef INET6
	case ETHERTYPE_IPV6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		goto discard;
	}
	netisr_dispatch(isr, m);
	return;

discard:
	m_freem(m);
}

/*
 * Process a received Infiniband packet.
 */
static void
ipoib_input(struct ifnet *ifp, struct mbuf *m)
{
	struct ipoib_header *eh;

	if ((ifp->if_flags & IFF_UP) == 0) {
		m_freem(m);
		return;
	}
	CURVNET_SET_QUIET(ifp->if_vnet);

	/* Let BPF have it before we strip the header. */
	IPOIB_MTAP(ifp, m);
	eh = mtod(m, struct ipoib_header *);
	/*
	 * Reset layer specific mbuf flags to avoid confusing upper layers.
	 * Strip off Infiniband header.
	 */
	m->m_flags &= ~M_VLANTAG;
	m_clrprotoflags(m);
	m_adj(m, IPOIB_HEADER_LEN);

	if (IPOIB_IS_MULTICAST(eh->hwaddr)) {
		if (memcmp(eh->hwaddr, ifp->if_broadcastaddr,
		    ifp->if_addrlen) == 0)
			m->m_flags |= M_BCAST;
		else
			m->m_flags |= M_MCAST;
		if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);
	}

	ipoib_demux(ifp, m, ntohs(eh->proto));
	CURVNET_RESTORE();
}

static int
ipoib_resolvemulti(struct ifnet *ifp, struct sockaddr **llsa,
	struct sockaddr *sa)
{
	struct sockaddr_dl *sdl;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	u_char *e_addr;

	switch(sa->sa_family) {
	case AF_LINK:
		/*
		 * No mapping needed. Just check that it's a valid MC address.
		 */
		sdl = (struct sockaddr_dl *)sa;
		e_addr = LLADDR(sdl);
		if (!IPOIB_IS_MULTICAST(e_addr))
			return EADDRNOTAVAIL;
		*llsa = NULL;
		return 0;

#ifdef INET
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		if (!IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return EADDRNOTAVAIL;
		sdl = link_init_sdl(ifp, *llsa, IFT_INFINIBAND);
		sdl->sdl_alen = INFINIBAND_ALEN;
		e_addr = LLADDR(sdl);
		ip_ib_mc_map(sin->sin_addr.s_addr, ifp->if_broadcastaddr,
		    e_addr);
		*llsa = (struct sockaddr *)sdl;
		return 0;
#endif
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		/*
		 * An IP6 address of 0 means listen to all
		 * of the multicast address used for IP6.  
		 * This has no meaning in ipoib.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
			return EADDRNOTAVAIL;
		if (!IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return EADDRNOTAVAIL;
		sdl = link_init_sdl(ifp, *llsa, IFT_INFINIBAND);
		sdl->sdl_alen = INFINIBAND_ALEN;
		e_addr = LLADDR(sdl);
		ipv6_ib_mc_map(&sin6->sin6_addr, ifp->if_broadcastaddr, e_addr);
		*llsa = (struct sockaddr *)sdl;
		return 0;
#endif

	default:
		return EAFNOSUPPORT;
	}
}

module_init(ipoib_init_module);
module_exit(ipoib_cleanup_module);

static int
ipoib_evhand(module_t mod, int event, void *arg)
{
	                return (0);
}

static moduledata_t ipoib_mod = {
	                .name = "ipoib",
			                .evhand = ipoib_evhand,
};

DECLARE_MODULE(ipoib, ipoib_mod, SI_SUB_LAST, SI_ORDER_ANY);
MODULE_DEPEND(ipoib, ibcore, 1, 1, 1);
MODULE_DEPEND(ipoib, linuxkpi, 1, 1, 1);
