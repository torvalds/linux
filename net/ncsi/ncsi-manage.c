// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Gavin Shan, IBM Corporation 2016.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <net/ncsi.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/addrconf.h>
#include <net/ipv6.h>
#include <net/genetlink.h>

#include "internal.h"
#include "ncsi-pkt.h"
#include "ncsi-netlink.h"

LIST_HEAD(ncsi_dev_list);
DEFINE_SPINLOCK(ncsi_dev_lock);

bool ncsi_channel_has_link(struct ncsi_channel *channel)
{
	return !!(channel->modes[NCSI_MODE_LINK].data[2] & 0x1);
}

bool ncsi_channel_is_last(struct ncsi_dev_priv *ndp,
			  struct ncsi_channel *channel)
{
	struct ncsi_package *np;
	struct ncsi_channel *nc;

	NCSI_FOR_EACH_PACKAGE(ndp, np)
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			if (nc == channel)
				continue;
			if (nc->state == NCSI_CHANNEL_ACTIVE &&
			    ncsi_channel_has_link(nc))
				return false;
		}

	return true;
}

static void ncsi_report_link(struct ncsi_dev_priv *ndp, bool force_down)
{
	struct ncsi_dev *nd = &ndp->ndev;
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	unsigned long flags;

	nd->state = ncsi_dev_state_functional;
	if (force_down) {
		nd->link_up = 0;
		goto report;
	}

	nd->link_up = 0;
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			spin_lock_irqsave(&nc->lock, flags);

			if (!list_empty(&nc->link) ||
			    nc->state != NCSI_CHANNEL_ACTIVE) {
				spin_unlock_irqrestore(&nc->lock, flags);
				continue;
			}

			if (ncsi_channel_has_link(nc)) {
				spin_unlock_irqrestore(&nc->lock, flags);
				nd->link_up = 1;
				goto report;
			}

			spin_unlock_irqrestore(&nc->lock, flags);
		}
	}

report:
	nd->handler(nd);
}

static void ncsi_channel_monitor(struct timer_list *t)
{
	struct ncsi_channel *nc = from_timer(nc, t, monitor.timer);
	struct ncsi_package *np = nc->package;
	struct ncsi_dev_priv *ndp = np->ndp;
	struct ncsi_channel_mode *ncm;
	struct ncsi_cmd_arg nca;
	bool enabled, chained;
	unsigned int monitor_state;
	unsigned long flags;
	int state, ret;

	spin_lock_irqsave(&nc->lock, flags);
	state = nc->state;
	chained = !list_empty(&nc->link);
	enabled = nc->monitor.enabled;
	monitor_state = nc->monitor.state;
	spin_unlock_irqrestore(&nc->lock, flags);

	if (!enabled)
		return;		/* expected race disabling timer */
	if (WARN_ON_ONCE(chained))
		goto bad_state;

	if (state != NCSI_CHANNEL_INACTIVE &&
	    state != NCSI_CHANNEL_ACTIVE) {
bad_state:
		netdev_warn(ndp->ndev.dev,
			    "Bad NCSI monitor state channel %d 0x%x %s queue\n",
			    nc->id, state, chained ? "on" : "off");
		spin_lock_irqsave(&nc->lock, flags);
		nc->monitor.enabled = false;
		spin_unlock_irqrestore(&nc->lock, flags);
		return;
	}

	switch (monitor_state) {
	case NCSI_CHANNEL_MONITOR_START:
	case NCSI_CHANNEL_MONITOR_RETRY:
		nca.ndp = ndp;
		nca.package = np->id;
		nca.channel = nc->id;
		nca.type = NCSI_PKT_CMD_GLS;
		nca.req_flags = 0;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			netdev_err(ndp->ndev.dev, "Error %d sending GLS\n",
				   ret);
		break;
	case NCSI_CHANNEL_MONITOR_WAIT ... NCSI_CHANNEL_MONITOR_WAIT_MAX:
		break;
	default:
		netdev_err(ndp->ndev.dev, "NCSI Channel %d timed out!\n",
			   nc->id);
		ncsi_report_link(ndp, true);
		ndp->flags |= NCSI_DEV_RESHUFFLE;

		ncm = &nc->modes[NCSI_MODE_LINK];
		spin_lock_irqsave(&nc->lock, flags);
		nc->monitor.enabled = false;
		nc->state = NCSI_CHANNEL_INVISIBLE;
		ncm->data[2] &= ~0x1;
		spin_unlock_irqrestore(&nc->lock, flags);

		spin_lock_irqsave(&ndp->lock, flags);
		nc->state = NCSI_CHANNEL_ACTIVE;
		list_add_tail_rcu(&nc->link, &ndp->channel_queue);
		spin_unlock_irqrestore(&ndp->lock, flags);
		ncsi_process_next_channel(ndp);
		return;
	}

	spin_lock_irqsave(&nc->lock, flags);
	nc->monitor.state++;
	spin_unlock_irqrestore(&nc->lock, flags);
	mod_timer(&nc->monitor.timer, jiffies + HZ);
}

void ncsi_start_channel_monitor(struct ncsi_channel *nc)
{
	unsigned long flags;

	spin_lock_irqsave(&nc->lock, flags);
	WARN_ON_ONCE(nc->monitor.enabled);
	nc->monitor.enabled = true;
	nc->monitor.state = NCSI_CHANNEL_MONITOR_START;
	spin_unlock_irqrestore(&nc->lock, flags);

	mod_timer(&nc->monitor.timer, jiffies + HZ);
}

void ncsi_stop_channel_monitor(struct ncsi_channel *nc)
{
	unsigned long flags;

	spin_lock_irqsave(&nc->lock, flags);
	if (!nc->monitor.enabled) {
		spin_unlock_irqrestore(&nc->lock, flags);
		return;
	}
	nc->monitor.enabled = false;
	spin_unlock_irqrestore(&nc->lock, flags);

	timer_delete_sync(&nc->monitor.timer);
}

struct ncsi_channel *ncsi_find_channel(struct ncsi_package *np,
				       unsigned char id)
{
	struct ncsi_channel *nc;

	NCSI_FOR_EACH_CHANNEL(np, nc) {
		if (nc->id == id)
			return nc;
	}

	return NULL;
}

struct ncsi_channel *ncsi_add_channel(struct ncsi_package *np, unsigned char id)
{
	struct ncsi_channel *nc, *tmp;
	int index;
	unsigned long flags;

	nc = kzalloc(sizeof(*nc), GFP_ATOMIC);
	if (!nc)
		return NULL;

	nc->id = id;
	nc->package = np;
	nc->state = NCSI_CHANNEL_INACTIVE;
	nc->monitor.enabled = false;
	timer_setup(&nc->monitor.timer, ncsi_channel_monitor, 0);
	spin_lock_init(&nc->lock);
	INIT_LIST_HEAD(&nc->link);
	for (index = 0; index < NCSI_CAP_MAX; index++)
		nc->caps[index].index = index;
	for (index = 0; index < NCSI_MODE_MAX; index++)
		nc->modes[index].index = index;

	spin_lock_irqsave(&np->lock, flags);
	tmp = ncsi_find_channel(np, id);
	if (tmp) {
		spin_unlock_irqrestore(&np->lock, flags);
		kfree(nc);
		return tmp;
	}

	list_add_tail_rcu(&nc->node, &np->channels);
	np->channel_num++;
	spin_unlock_irqrestore(&np->lock, flags);

	return nc;
}

static void ncsi_remove_channel(struct ncsi_channel *nc)
{
	struct ncsi_package *np = nc->package;
	unsigned long flags;

	spin_lock_irqsave(&nc->lock, flags);

	/* Release filters */
	kfree(nc->mac_filter.addrs);
	kfree(nc->vlan_filter.vids);

	nc->state = NCSI_CHANNEL_INACTIVE;
	spin_unlock_irqrestore(&nc->lock, flags);
	ncsi_stop_channel_monitor(nc);

	/* Remove and free channel */
	spin_lock_irqsave(&np->lock, flags);
	list_del_rcu(&nc->node);
	np->channel_num--;
	spin_unlock_irqrestore(&np->lock, flags);

	kfree(nc);
}

struct ncsi_package *ncsi_find_package(struct ncsi_dev_priv *ndp,
				       unsigned char id)
{
	struct ncsi_package *np;

	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (np->id == id)
			return np;
	}

	return NULL;
}

struct ncsi_package *ncsi_add_package(struct ncsi_dev_priv *ndp,
				      unsigned char id)
{
	struct ncsi_package *np, *tmp;
	unsigned long flags;

	np = kzalloc(sizeof(*np), GFP_ATOMIC);
	if (!np)
		return NULL;

	np->id = id;
	np->ndp = ndp;
	spin_lock_init(&np->lock);
	INIT_LIST_HEAD(&np->channels);
	np->channel_whitelist = UINT_MAX;

	spin_lock_irqsave(&ndp->lock, flags);
	tmp = ncsi_find_package(ndp, id);
	if (tmp) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		kfree(np);
		return tmp;
	}

	list_add_tail_rcu(&np->node, &ndp->packages);
	ndp->package_num++;
	spin_unlock_irqrestore(&ndp->lock, flags);

	return np;
}

void ncsi_remove_package(struct ncsi_package *np)
{
	struct ncsi_dev_priv *ndp = np->ndp;
	struct ncsi_channel *nc, *tmp;
	unsigned long flags;

	/* Release all child channels */
	list_for_each_entry_safe(nc, tmp, &np->channels, node)
		ncsi_remove_channel(nc);

	/* Remove and free package */
	spin_lock_irqsave(&ndp->lock, flags);
	list_del_rcu(&np->node);
	ndp->package_num--;
	spin_unlock_irqrestore(&ndp->lock, flags);

	kfree(np);
}

void ncsi_find_package_and_channel(struct ncsi_dev_priv *ndp,
				   unsigned char id,
				   struct ncsi_package **np,
				   struct ncsi_channel **nc)
{
	struct ncsi_package *p;
	struct ncsi_channel *c;

	p = ncsi_find_package(ndp, NCSI_PACKAGE_INDEX(id));
	c = p ? ncsi_find_channel(p, NCSI_CHANNEL_INDEX(id)) : NULL;

	if (np)
		*np = p;
	if (nc)
		*nc = c;
}

/* For two consecutive NCSI commands, the packet IDs shouldn't
 * be same. Otherwise, the bogus response might be replied. So
 * the available IDs are allocated in round-robin fashion.
 */
struct ncsi_request *ncsi_alloc_request(struct ncsi_dev_priv *ndp,
					unsigned int req_flags)
{
	struct ncsi_request *nr = NULL;
	int i, limit = ARRAY_SIZE(ndp->requests);
	unsigned long flags;

	/* Check if there is one available request until the ceiling */
	spin_lock_irqsave(&ndp->lock, flags);
	for (i = ndp->request_id; i < limit; i++) {
		if (ndp->requests[i].used)
			continue;

		nr = &ndp->requests[i];
		nr->used = true;
		nr->flags = req_flags;
		ndp->request_id = i + 1;
		goto found;
	}

	/* Fail back to check from the starting cursor */
	for (i = NCSI_REQ_START_IDX; i < ndp->request_id; i++) {
		if (ndp->requests[i].used)
			continue;

		nr = &ndp->requests[i];
		nr->used = true;
		nr->flags = req_flags;
		ndp->request_id = i + 1;
		goto found;
	}

found:
	spin_unlock_irqrestore(&ndp->lock, flags);
	return nr;
}

void ncsi_free_request(struct ncsi_request *nr)
{
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct sk_buff *cmd, *rsp;
	unsigned long flags;
	bool driven;

	if (nr->enabled) {
		nr->enabled = false;
		timer_delete_sync(&nr->timer);
	}

	spin_lock_irqsave(&ndp->lock, flags);
	cmd = nr->cmd;
	rsp = nr->rsp;
	nr->cmd = NULL;
	nr->rsp = NULL;
	nr->used = false;
	driven = !!(nr->flags & NCSI_REQ_FLAG_EVENT_DRIVEN);
	spin_unlock_irqrestore(&ndp->lock, flags);

	if (driven && cmd && --ndp->pending_req_num == 0)
		schedule_work(&ndp->work);

	/* Release command and response */
	consume_skb(cmd);
	consume_skb(rsp);
}

struct ncsi_dev *ncsi_find_dev(struct net_device *dev)
{
	struct ncsi_dev_priv *ndp;

	NCSI_FOR_EACH_DEV(ndp) {
		if (ndp->ndev.dev == dev)
			return &ndp->ndev;
	}

	return NULL;
}

static void ncsi_request_timeout(struct timer_list *t)
{
	struct ncsi_request *nr = from_timer(nr, t, timer);
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_cmd_pkt *cmd;
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	unsigned long flags;

	/* If the request already had associated response,
	 * let the response handler to release it.
	 */
	spin_lock_irqsave(&ndp->lock, flags);
	nr->enabled = false;
	if (nr->rsp || !nr->cmd) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ndp->lock, flags);

	if (nr->flags == NCSI_REQ_FLAG_NETLINK_DRIVEN) {
		if (nr->cmd) {
			/* Find the package */
			cmd = (struct ncsi_cmd_pkt *)
			      skb_network_header(nr->cmd);
			ncsi_find_package_and_channel(ndp,
						      cmd->cmd.common.channel,
						      &np, &nc);
			ncsi_send_netlink_timeout(nr, np, nc);
		}
	}

	/* Release the request */
	ncsi_free_request(nr);
}

static void ncsi_suspend_channel(struct ncsi_dev_priv *ndp)
{
	struct ncsi_dev *nd = &ndp->ndev;
	struct ncsi_package *np;
	struct ncsi_channel *nc, *tmp;
	struct ncsi_cmd_arg nca;
	unsigned long flags;
	int ret;

	np = ndp->active_package;
	nc = ndp->active_channel;
	nca.ndp = ndp;
	nca.req_flags = NCSI_REQ_FLAG_EVENT_DRIVEN;
	switch (nd->state) {
	case ncsi_dev_state_suspend:
		nd->state = ncsi_dev_state_suspend_select;
		fallthrough;
	case ncsi_dev_state_suspend_select:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_SP;
		nca.package = np->id;
		nca.channel = NCSI_RESERVED_CHANNEL;
		if (ndp->flags & NCSI_DEV_HWA)
			nca.bytes[0] = 0;
		else
			nca.bytes[0] = 1;

		/* To retrieve the last link states of channels in current
		 * package when current active channel needs fail over to
		 * another one. It means we will possibly select another
		 * channel as next active one. The link states of channels
		 * are most important factor of the selection. So we need
		 * accurate link states. Unfortunately, the link states on
		 * inactive channels can't be updated with LSC AEN in time.
		 */
		if (ndp->flags & NCSI_DEV_RESHUFFLE)
			nd->state = ncsi_dev_state_suspend_gls;
		else
			nd->state = ncsi_dev_state_suspend_dcnt;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;

		break;
	case ncsi_dev_state_suspend_gls:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_GLS;
		nca.package = np->id;
		nca.channel = ndp->channel_probe_id;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;
		ndp->channel_probe_id++;

		if (ndp->channel_probe_id == ndp->channel_count) {
			ndp->channel_probe_id = 0;
			nd->state = ncsi_dev_state_suspend_dcnt;
		}

		break;
	case ncsi_dev_state_suspend_dcnt:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_DCNT;
		nca.package = np->id;
		nca.channel = nc->id;

		nd->state = ncsi_dev_state_suspend_dc;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;

		break;
	case ncsi_dev_state_suspend_dc:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_DC;
		nca.package = np->id;
		nca.channel = nc->id;
		nca.bytes[0] = 1;

		nd->state = ncsi_dev_state_suspend_deselect;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;

		NCSI_FOR_EACH_CHANNEL(np, tmp) {
			/* If there is another channel active on this package
			 * do not deselect the package.
			 */
			if (tmp != nc && tmp->state == NCSI_CHANNEL_ACTIVE) {
				nd->state = ncsi_dev_state_suspend_done;
				break;
			}
		}
		break;
	case ncsi_dev_state_suspend_deselect:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_DP;
		nca.package = np->id;
		nca.channel = NCSI_RESERVED_CHANNEL;

		nd->state = ncsi_dev_state_suspend_done;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;

		break;
	case ncsi_dev_state_suspend_done:
		spin_lock_irqsave(&nc->lock, flags);
		nc->state = NCSI_CHANNEL_INACTIVE;
		spin_unlock_irqrestore(&nc->lock, flags);
		if (ndp->flags & NCSI_DEV_RESET)
			ncsi_reset_dev(nd);
		else
			ncsi_process_next_channel(ndp);
		break;
	default:
		netdev_warn(nd->dev, "Wrong NCSI state 0x%x in suspend\n",
			    nd->state);
	}

	return;
error:
	nd->state = ncsi_dev_state_functional;
}

/* Check the VLAN filter bitmap for a set filter, and construct a
 * "Set VLAN Filter - Disable" packet if found.
 */
static int clear_one_vid(struct ncsi_dev_priv *ndp, struct ncsi_channel *nc,
			 struct ncsi_cmd_arg *nca)
{
	struct ncsi_channel_vlan_filter *ncf;
	unsigned long flags;
	void *bitmap;
	int index;
	u16 vid;

	ncf = &nc->vlan_filter;
	bitmap = &ncf->bitmap;

	spin_lock_irqsave(&nc->lock, flags);
	index = find_first_bit(bitmap, ncf->n_vids);
	if (index >= ncf->n_vids) {
		spin_unlock_irqrestore(&nc->lock, flags);
		return -1;
	}
	vid = ncf->vids[index];

	clear_bit(index, bitmap);
	ncf->vids[index] = 0;
	spin_unlock_irqrestore(&nc->lock, flags);

	nca->type = NCSI_PKT_CMD_SVF;
	nca->words[1] = vid;
	/* HW filter index starts at 1 */
	nca->bytes[6] = index + 1;
	nca->bytes[7] = 0x00;
	return 0;
}

/* Find an outstanding VLAN tag and construct a "Set VLAN Filter - Enable"
 * packet.
 */
static int set_one_vid(struct ncsi_dev_priv *ndp, struct ncsi_channel *nc,
		       struct ncsi_cmd_arg *nca)
{
	struct ncsi_channel_vlan_filter *ncf;
	struct vlan_vid *vlan = NULL;
	unsigned long flags;
	int i, index;
	void *bitmap;
	u16 vid;

	if (list_empty(&ndp->vlan_vids))
		return -1;

	ncf = &nc->vlan_filter;
	bitmap = &ncf->bitmap;

	spin_lock_irqsave(&nc->lock, flags);

	rcu_read_lock();
	list_for_each_entry_rcu(vlan, &ndp->vlan_vids, list) {
		vid = vlan->vid;
		for (i = 0; i < ncf->n_vids; i++)
			if (ncf->vids[i] == vid) {
				vid = 0;
				break;
			}
		if (vid)
			break;
	}
	rcu_read_unlock();

	if (!vid) {
		/* No VLAN ID is not set */
		spin_unlock_irqrestore(&nc->lock, flags);
		return -1;
	}

	index = find_first_zero_bit(bitmap, ncf->n_vids);
	if (index < 0 || index >= ncf->n_vids) {
		netdev_err(ndp->ndev.dev,
			   "Channel %u already has all VLAN filters set\n",
			   nc->id);
		spin_unlock_irqrestore(&nc->lock, flags);
		return -1;
	}

	ncf->vids[index] = vid;
	set_bit(index, bitmap);
	spin_unlock_irqrestore(&nc->lock, flags);

	nca->type = NCSI_PKT_CMD_SVF;
	nca->words[1] = vid;
	/* HW filter index starts at 1 */
	nca->bytes[6] = index + 1;
	nca->bytes[7] = 0x01;

	return 0;
}

static int ncsi_oem_keep_phy_intel(struct ncsi_cmd_arg *nca)
{
	unsigned char data[NCSI_OEM_INTEL_CMD_KEEP_PHY_LEN];
	int ret = 0;

	nca->payload = NCSI_OEM_INTEL_CMD_KEEP_PHY_LEN;

	memset(data, 0, NCSI_OEM_INTEL_CMD_KEEP_PHY_LEN);
	*(unsigned int *)data = ntohl((__force __be32)NCSI_OEM_MFR_INTEL_ID);

	data[4] = NCSI_OEM_INTEL_CMD_KEEP_PHY;

	/* PHY Link up attribute */
	data[6] = 0x1;

	nca->data = data;

	ret = ncsi_xmit_cmd(nca);
	if (ret)
		netdev_err(nca->ndp->ndev.dev,
			   "NCSI: Failed to transmit cmd 0x%x during configure\n",
			   nca->type);
	return ret;
}

/* NCSI OEM Command APIs */
static int ncsi_oem_gma_handler_bcm(struct ncsi_cmd_arg *nca)
{
	unsigned char data[NCSI_OEM_BCM_CMD_GMA_LEN];
	int ret = 0;

	nca->payload = NCSI_OEM_BCM_CMD_GMA_LEN;

	memset(data, 0, NCSI_OEM_BCM_CMD_GMA_LEN);
	*(unsigned int *)data = ntohl((__force __be32)NCSI_OEM_MFR_BCM_ID);
	data[5] = NCSI_OEM_BCM_CMD_GMA;

	nca->data = data;

	ret = ncsi_xmit_cmd(nca);
	if (ret)
		netdev_err(nca->ndp->ndev.dev,
			   "NCSI: Failed to transmit cmd 0x%x during configure\n",
			   nca->type);
	return ret;
}

static int ncsi_oem_gma_handler_mlx(struct ncsi_cmd_arg *nca)
{
	union {
		u8 data_u8[NCSI_OEM_MLX_CMD_GMA_LEN];
		u32 data_u32[NCSI_OEM_MLX_CMD_GMA_LEN / sizeof(u32)];
	} u;
	int ret = 0;

	nca->payload = NCSI_OEM_MLX_CMD_GMA_LEN;

	memset(&u, 0, sizeof(u));
	u.data_u32[0] = ntohl((__force __be32)NCSI_OEM_MFR_MLX_ID);
	u.data_u8[5] = NCSI_OEM_MLX_CMD_GMA;
	u.data_u8[6] = NCSI_OEM_MLX_CMD_GMA_PARAM;

	nca->data = u.data_u8;

	ret = ncsi_xmit_cmd(nca);
	if (ret)
		netdev_err(nca->ndp->ndev.dev,
			   "NCSI: Failed to transmit cmd 0x%x during configure\n",
			   nca->type);
	return ret;
}

static int ncsi_oem_smaf_mlx(struct ncsi_cmd_arg *nca)
{
	union {
		u8 data_u8[NCSI_OEM_MLX_CMD_SMAF_LEN];
		u32 data_u32[NCSI_OEM_MLX_CMD_SMAF_LEN / sizeof(u32)];
	} u;
	int ret = 0;

	memset(&u, 0, sizeof(u));
	u.data_u32[0] = ntohl((__force __be32)NCSI_OEM_MFR_MLX_ID);
	u.data_u8[5] = NCSI_OEM_MLX_CMD_SMAF;
	u.data_u8[6] = NCSI_OEM_MLX_CMD_SMAF_PARAM;
	memcpy(&u.data_u8[MLX_SMAF_MAC_ADDR_OFFSET],
	       nca->ndp->ndev.dev->dev_addr,	ETH_ALEN);
	u.data_u8[MLX_SMAF_MED_SUPPORT_OFFSET] =
		(MLX_MC_RBT_AVL | MLX_MC_RBT_SUPPORT);

	nca->payload = NCSI_OEM_MLX_CMD_SMAF_LEN;
	nca->data = u.data_u8;

	ret = ncsi_xmit_cmd(nca);
	if (ret)
		netdev_err(nca->ndp->ndev.dev,
			   "NCSI: Failed to transmit cmd 0x%x during probe\n",
			   nca->type);
	return ret;
}

static int ncsi_oem_gma_handler_intel(struct ncsi_cmd_arg *nca)
{
	unsigned char data[NCSI_OEM_INTEL_CMD_GMA_LEN];
	int ret = 0;

	nca->payload = NCSI_OEM_INTEL_CMD_GMA_LEN;

	memset(data, 0, NCSI_OEM_INTEL_CMD_GMA_LEN);
	*(unsigned int *)data = ntohl((__force __be32)NCSI_OEM_MFR_INTEL_ID);
	data[4] = NCSI_OEM_INTEL_CMD_GMA;

	nca->data = data;

	ret = ncsi_xmit_cmd(nca);
	if (ret)
		netdev_err(nca->ndp->ndev.dev,
			   "NCSI: Failed to transmit cmd 0x%x during configure\n",
			   nca->type);

	return ret;
}

/* OEM Command handlers initialization */
static struct ncsi_oem_gma_handler {
	unsigned int	mfr_id;
	int		(*handler)(struct ncsi_cmd_arg *nca);
} ncsi_oem_gma_handlers[] = {
	{ NCSI_OEM_MFR_BCM_ID, ncsi_oem_gma_handler_bcm },
	{ NCSI_OEM_MFR_MLX_ID, ncsi_oem_gma_handler_mlx },
	{ NCSI_OEM_MFR_INTEL_ID, ncsi_oem_gma_handler_intel }
};

static int ncsi_gma_handler(struct ncsi_cmd_arg *nca, unsigned int mf_id)
{
	struct ncsi_oem_gma_handler *nch = NULL;
	int i;

	/* This function should only be called once, return if flag set */
	if (nca->ndp->gma_flag == 1)
		return -1;

	/* Find gma handler for given manufacturer id */
	for (i = 0; i < ARRAY_SIZE(ncsi_oem_gma_handlers); i++) {
		if (ncsi_oem_gma_handlers[i].mfr_id == mf_id) {
			if (ncsi_oem_gma_handlers[i].handler)
				nch = &ncsi_oem_gma_handlers[i];
			break;
			}
	}

	if (!nch) {
		netdev_err(nca->ndp->ndev.dev,
			   "NCSI: No GMA handler available for MFR-ID (0x%x)\n",
			   mf_id);
		return -1;
	}

	/* Get Mac address from NCSI device */
	return nch->handler(nca);
}

/* Determine if a given channel from the channel_queue should be used for Tx */
static bool ncsi_channel_is_tx(struct ncsi_dev_priv *ndp,
			       struct ncsi_channel *nc)
{
	struct ncsi_channel_mode *ncm;
	struct ncsi_channel *channel;
	struct ncsi_package *np;

	/* Check if any other channel has Tx enabled; a channel may have already
	 * been configured and removed from the channel queue.
	 */
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (!ndp->multi_package && np != nc->package)
			continue;
		NCSI_FOR_EACH_CHANNEL(np, channel) {
			ncm = &channel->modes[NCSI_MODE_TX_ENABLE];
			if (ncm->enable)
				return false;
		}
	}

	/* This channel is the preferred channel and has link */
	list_for_each_entry_rcu(channel, &ndp->channel_queue, link) {
		np = channel->package;
		if (np->preferred_channel &&
		    ncsi_channel_has_link(np->preferred_channel)) {
			return np->preferred_channel == nc;
		}
	}

	/* This channel has link */
	if (ncsi_channel_has_link(nc))
		return true;

	list_for_each_entry_rcu(channel, &ndp->channel_queue, link)
		if (ncsi_channel_has_link(channel))
			return false;

	/* No other channel has link; default to this one */
	return true;
}

/* Change the active Tx channel in a multi-channel setup */
int ncsi_update_tx_channel(struct ncsi_dev_priv *ndp,
			   struct ncsi_package *package,
			   struct ncsi_channel *disable,
			   struct ncsi_channel *enable)
{
	struct ncsi_cmd_arg nca;
	struct ncsi_channel *nc;
	struct ncsi_package *np;
	int ret = 0;

	if (!package->multi_channel && !ndp->multi_package)
		netdev_warn(ndp->ndev.dev,
			    "NCSI: Trying to update Tx channel in single-channel mode\n");
	nca.ndp = ndp;
	nca.req_flags = 0;

	/* Find current channel with Tx enabled */
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (disable)
			break;
		if (!ndp->multi_package && np != package)
			continue;

		NCSI_FOR_EACH_CHANNEL(np, nc)
			if (nc->modes[NCSI_MODE_TX_ENABLE].enable) {
				disable = nc;
				break;
			}
	}

	/* Find a suitable channel for Tx */
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (enable)
			break;
		if (!ndp->multi_package && np != package)
			continue;
		if (!(ndp->package_whitelist & (0x1 << np->id)))
			continue;

		if (np->preferred_channel &&
		    ncsi_channel_has_link(np->preferred_channel)) {
			enable = np->preferred_channel;
			break;
		}

		NCSI_FOR_EACH_CHANNEL(np, nc) {
			if (!(np->channel_whitelist & 0x1 << nc->id))
				continue;
			if (nc->state != NCSI_CHANNEL_ACTIVE)
				continue;
			if (ncsi_channel_has_link(nc)) {
				enable = nc;
				break;
			}
		}
	}

	if (disable == enable)
		return -1;

	if (!enable)
		return -1;

	if (disable) {
		nca.channel = disable->id;
		nca.package = disable->package->id;
		nca.type = NCSI_PKT_CMD_DCNT;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			netdev_err(ndp->ndev.dev,
				   "Error %d sending DCNT\n",
				   ret);
	}

	netdev_info(ndp->ndev.dev, "NCSI: channel %u enables Tx\n", enable->id);

	nca.channel = enable->id;
	nca.package = enable->package->id;
	nca.type = NCSI_PKT_CMD_ECNT;
	ret = ncsi_xmit_cmd(&nca);
	if (ret)
		netdev_err(ndp->ndev.dev,
			   "Error %d sending ECNT\n",
			   ret);

	return ret;
}

static void ncsi_configure_channel(struct ncsi_dev_priv *ndp)
{
	struct ncsi_package *np = ndp->active_package;
	struct ncsi_channel *nc = ndp->active_channel;
	struct ncsi_channel *hot_nc = NULL;
	struct ncsi_dev *nd = &ndp->ndev;
	struct net_device *dev = nd->dev;
	struct ncsi_cmd_arg nca;
	unsigned char index;
	unsigned long flags;
	int ret;

	nca.ndp = ndp;
	nca.req_flags = NCSI_REQ_FLAG_EVENT_DRIVEN;
	switch (nd->state) {
	case ncsi_dev_state_config:
	case ncsi_dev_state_config_sp:
		ndp->pending_req_num = 1;

		/* Select the specific package */
		nca.type = NCSI_PKT_CMD_SP;
		if (ndp->flags & NCSI_DEV_HWA)
			nca.bytes[0] = 0;
		else
			nca.bytes[0] = 1;
		nca.package = np->id;
		nca.channel = NCSI_RESERVED_CHANNEL;
		ret = ncsi_xmit_cmd(&nca);
		if (ret) {
			netdev_err(ndp->ndev.dev,
				   "NCSI: Failed to transmit CMD_SP\n");
			goto error;
		}

		nd->state = ncsi_dev_state_config_cis;
		break;
	case ncsi_dev_state_config_cis:
		ndp->pending_req_num = 1;

		/* Clear initial state */
		nca.type = NCSI_PKT_CMD_CIS;
		nca.package = np->id;
		nca.channel = nc->id;
		ret = ncsi_xmit_cmd(&nca);
		if (ret) {
			netdev_err(ndp->ndev.dev,
				   "NCSI: Failed to transmit CMD_CIS\n");
			goto error;
		}

		nd->state = IS_ENABLED(CONFIG_NCSI_OEM_CMD_GET_MAC)
			  ? ncsi_dev_state_config_oem_gma
			  : ncsi_dev_state_config_clear_vids;
		break;
	case ncsi_dev_state_config_oem_gma:
		nd->state = ncsi_dev_state_config_apply_mac;

		nca.package = np->id;
		nca.channel = nc->id;
		ndp->pending_req_num = 1;
		if (nc->version.major >= 1 && nc->version.minor >= 2) {
			nca.type = NCSI_PKT_CMD_GMCMA;
			ret = ncsi_xmit_cmd(&nca);
		} else {
			nca.type = NCSI_PKT_CMD_OEM;
			ret = ncsi_gma_handler(&nca, nc->version.mf_id);
		}
		if (ret < 0) {
			nd->state = ncsi_dev_state_config_clear_vids;
			schedule_work(&ndp->work);
		}

		break;
	case ncsi_dev_state_config_apply_mac:
		rtnl_lock();
		ret = dev_set_mac_address(dev, &ndp->pending_mac, NULL);
		rtnl_unlock();
		if (ret < 0)
			netdev_warn(dev, "NCSI: 'Writing MAC address to device failed\n");

		nd->state = ncsi_dev_state_config_clear_vids;

		fallthrough;
	case ncsi_dev_state_config_clear_vids:
	case ncsi_dev_state_config_svf:
	case ncsi_dev_state_config_ev:
	case ncsi_dev_state_config_sma:
	case ncsi_dev_state_config_ebf:
	case ncsi_dev_state_config_dgmf:
	case ncsi_dev_state_config_ecnt:
	case ncsi_dev_state_config_ec:
	case ncsi_dev_state_config_ae:
	case ncsi_dev_state_config_gls:
		ndp->pending_req_num = 1;

		nca.package = np->id;
		nca.channel = nc->id;

		/* Clear any active filters on the channel before setting */
		if (nd->state == ncsi_dev_state_config_clear_vids) {
			ret = clear_one_vid(ndp, nc, &nca);
			if (ret) {
				nd->state = ncsi_dev_state_config_svf;
				schedule_work(&ndp->work);
				break;
			}
			/* Repeat */
			nd->state = ncsi_dev_state_config_clear_vids;
		/* Add known VLAN tags to the filter */
		} else if (nd->state == ncsi_dev_state_config_svf) {
			ret = set_one_vid(ndp, nc, &nca);
			if (ret) {
				nd->state = ncsi_dev_state_config_ev;
				schedule_work(&ndp->work);
				break;
			}
			/* Repeat */
			nd->state = ncsi_dev_state_config_svf;
		/* Enable/Disable the VLAN filter */
		} else if (nd->state == ncsi_dev_state_config_ev) {
			if (list_empty(&ndp->vlan_vids)) {
				nca.type = NCSI_PKT_CMD_DV;
			} else {
				nca.type = NCSI_PKT_CMD_EV;
				nca.bytes[3] = NCSI_CAP_VLAN_NO;
			}
			nd->state = ncsi_dev_state_config_sma;
		} else if (nd->state == ncsi_dev_state_config_sma) {
		/* Use first entry in unicast filter table. Note that
		 * the MAC filter table starts from entry 1 instead of
		 * 0.
		 */
			nca.type = NCSI_PKT_CMD_SMA;
			for (index = 0; index < 6; index++)
				nca.bytes[index] = dev->dev_addr[index];
			nca.bytes[6] = 0x1;
			nca.bytes[7] = 0x1;
			nd->state = ncsi_dev_state_config_ebf;
		} else if (nd->state == ncsi_dev_state_config_ebf) {
			nca.type = NCSI_PKT_CMD_EBF;
			nca.dwords[0] = nc->caps[NCSI_CAP_BC].cap;
			/* if multicast global filtering is supported then
			 * disable it so that all multicast packet will be
			 * forwarded to management controller
			 */
			if (nc->caps[NCSI_CAP_GENERIC].cap &
			    NCSI_CAP_GENERIC_MC)
				nd->state = ncsi_dev_state_config_dgmf;
			else if (ncsi_channel_is_tx(ndp, nc))
				nd->state = ncsi_dev_state_config_ecnt;
			else
				nd->state = ncsi_dev_state_config_ec;
		} else if (nd->state == ncsi_dev_state_config_dgmf) {
			nca.type = NCSI_PKT_CMD_DGMF;
			if (ncsi_channel_is_tx(ndp, nc))
				nd->state = ncsi_dev_state_config_ecnt;
			else
				nd->state = ncsi_dev_state_config_ec;
		} else if (nd->state == ncsi_dev_state_config_ecnt) {
			if (np->preferred_channel &&
			    nc != np->preferred_channel)
				netdev_info(ndp->ndev.dev,
					    "NCSI: Tx failed over to channel %u\n",
					    nc->id);
			nca.type = NCSI_PKT_CMD_ECNT;
			nd->state = ncsi_dev_state_config_ec;
		} else if (nd->state == ncsi_dev_state_config_ec) {
			/* Enable AEN if it's supported */
			nca.type = NCSI_PKT_CMD_EC;
			nd->state = ncsi_dev_state_config_ae;
			if (!(nc->caps[NCSI_CAP_AEN].cap & NCSI_CAP_AEN_MASK))
				nd->state = ncsi_dev_state_config_gls;
		} else if (nd->state == ncsi_dev_state_config_ae) {
			nca.type = NCSI_PKT_CMD_AE;
			nca.bytes[0] = 0;
			nca.dwords[1] = nc->caps[NCSI_CAP_AEN].cap;
			nd->state = ncsi_dev_state_config_gls;
		} else if (nd->state == ncsi_dev_state_config_gls) {
			nca.type = NCSI_PKT_CMD_GLS;
			nd->state = ncsi_dev_state_config_done;
		}

		ret = ncsi_xmit_cmd(&nca);
		if (ret) {
			netdev_err(ndp->ndev.dev,
				   "NCSI: Failed to transmit CMD %x\n",
				   nca.type);
			goto error;
		}
		break;
	case ncsi_dev_state_config_done:
		netdev_dbg(ndp->ndev.dev, "NCSI: channel %u config done\n",
			   nc->id);
		spin_lock_irqsave(&nc->lock, flags);
		nc->state = NCSI_CHANNEL_ACTIVE;

		if (ndp->flags & NCSI_DEV_RESET) {
			/* A reset event happened during config, start it now */
			nc->reconfigure_needed = false;
			spin_unlock_irqrestore(&nc->lock, flags);
			ncsi_reset_dev(nd);
			break;
		}

		if (nc->reconfigure_needed) {
			/* This channel's configuration has been updated
			 * part-way during the config state - start the
			 * channel configuration over
			 */
			nc->reconfigure_needed = false;
			nc->state = NCSI_CHANNEL_INACTIVE;
			spin_unlock_irqrestore(&nc->lock, flags);

			spin_lock_irqsave(&ndp->lock, flags);
			list_add_tail_rcu(&nc->link, &ndp->channel_queue);
			spin_unlock_irqrestore(&ndp->lock, flags);

			netdev_dbg(dev, "Dirty NCSI channel state reset\n");
			ncsi_process_next_channel(ndp);
			break;
		}

		if (nc->modes[NCSI_MODE_LINK].data[2] & 0x1) {
			hot_nc = nc;
		} else {
			hot_nc = NULL;
			netdev_dbg(ndp->ndev.dev,
				   "NCSI: channel %u link down after config\n",
				   nc->id);
		}
		spin_unlock_irqrestore(&nc->lock, flags);

		/* Update the hot channel */
		spin_lock_irqsave(&ndp->lock, flags);
		ndp->hot_channel = hot_nc;
		spin_unlock_irqrestore(&ndp->lock, flags);

		ncsi_start_channel_monitor(nc);
		ncsi_process_next_channel(ndp);
		break;
	default:
		netdev_alert(dev, "Wrong NCSI state 0x%x in config\n",
			     nd->state);
	}

	return;

error:
	ncsi_report_link(ndp, true);
}

static int ncsi_choose_active_channel(struct ncsi_dev_priv *ndp)
{
	struct ncsi_channel *nc, *found, *hot_nc;
	struct ncsi_channel_mode *ncm;
	unsigned long flags, cflags;
	struct ncsi_package *np;
	bool with_link;

	spin_lock_irqsave(&ndp->lock, flags);
	hot_nc = ndp->hot_channel;
	spin_unlock_irqrestore(&ndp->lock, flags);

	/* By default the search is done once an inactive channel with up
	 * link is found, unless a preferred channel is set.
	 * If multi_package or multi_channel are configured all channels in the
	 * whitelist are added to the channel queue.
	 */
	found = NULL;
	with_link = false;
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		if (!(ndp->package_whitelist & (0x1 << np->id)))
			continue;
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			if (!(np->channel_whitelist & (0x1 << nc->id)))
				continue;

			spin_lock_irqsave(&nc->lock, cflags);

			if (!list_empty(&nc->link) ||
			    nc->state != NCSI_CHANNEL_INACTIVE) {
				spin_unlock_irqrestore(&nc->lock, cflags);
				continue;
			}

			if (!found)
				found = nc;

			if (nc == hot_nc)
				found = nc;

			ncm = &nc->modes[NCSI_MODE_LINK];
			if (ncm->data[2] & 0x1) {
				found = nc;
				with_link = true;
			}

			/* If multi_channel is enabled configure all valid
			 * channels whether or not they currently have link
			 * so they will have AENs enabled.
			 */
			if (with_link || np->multi_channel) {
				spin_lock_irqsave(&ndp->lock, flags);
				list_add_tail_rcu(&nc->link,
						  &ndp->channel_queue);
				spin_unlock_irqrestore(&ndp->lock, flags);

				netdev_dbg(ndp->ndev.dev,
					   "NCSI: Channel %u added to queue (link %s)\n",
					   nc->id,
					   ncm->data[2] & 0x1 ? "up" : "down");
			}

			spin_unlock_irqrestore(&nc->lock, cflags);

			if (with_link && !np->multi_channel)
				break;
		}
		if (with_link && !ndp->multi_package)
			break;
	}

	if (list_empty(&ndp->channel_queue) && found) {
		netdev_info(ndp->ndev.dev,
			    "NCSI: No channel with link found, configuring channel %u\n",
			    found->id);
		spin_lock_irqsave(&ndp->lock, flags);
		list_add_tail_rcu(&found->link, &ndp->channel_queue);
		spin_unlock_irqrestore(&ndp->lock, flags);
	} else if (!found) {
		netdev_warn(ndp->ndev.dev,
			    "NCSI: No channel found to configure!\n");
		ncsi_report_link(ndp, true);
		return -ENODEV;
	}

	return ncsi_process_next_channel(ndp);
}

static bool ncsi_check_hwa(struct ncsi_dev_priv *ndp)
{
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	unsigned int cap;
	bool has_channel = false;

	/* The hardware arbitration is disabled if any one channel
	 * doesn't support explicitly.
	 */
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			has_channel = true;

			cap = nc->caps[NCSI_CAP_GENERIC].cap;
			if (!(cap & NCSI_CAP_GENERIC_HWA) ||
			    (cap & NCSI_CAP_GENERIC_HWA_MASK) !=
			    NCSI_CAP_GENERIC_HWA_SUPPORT) {
				ndp->flags &= ~NCSI_DEV_HWA;
				return false;
			}
		}
	}

	if (has_channel) {
		ndp->flags |= NCSI_DEV_HWA;
		return true;
	}

	ndp->flags &= ~NCSI_DEV_HWA;
	return false;
}

static void ncsi_probe_channel(struct ncsi_dev_priv *ndp)
{
	struct ncsi_dev *nd = &ndp->ndev;
	struct ncsi_package *np;
	struct ncsi_cmd_arg nca;
	unsigned char index;
	int ret;

	nca.ndp = ndp;
	nca.req_flags = NCSI_REQ_FLAG_EVENT_DRIVEN;
	switch (nd->state) {
	case ncsi_dev_state_probe:
		nd->state = ncsi_dev_state_probe_deselect;
		fallthrough;
	case ncsi_dev_state_probe_deselect:
		ndp->pending_req_num = 8;

		/* Deselect all possible packages */
		nca.type = NCSI_PKT_CMD_DP;
		nca.channel = NCSI_RESERVED_CHANNEL;
		for (index = 0; index < 8; index++) {
			nca.package = index;
			ret = ncsi_xmit_cmd(&nca);
			if (ret)
				goto error;
		}

		nd->state = ncsi_dev_state_probe_package;
		break;
	case ncsi_dev_state_probe_package:
		if (ndp->package_probe_id >= 8) {
			/* Last package probed, finishing */
			ndp->flags |= NCSI_DEV_PROBED;
			break;
		}

		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_SP;
		nca.bytes[0] = 1;
		nca.package = ndp->package_probe_id;
		nca.channel = NCSI_RESERVED_CHANNEL;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;
		nd->state = ncsi_dev_state_probe_channel;
		break;
	case ncsi_dev_state_probe_channel:
		ndp->active_package = ncsi_find_package(ndp,
							ndp->package_probe_id);
		if (!ndp->active_package) {
			/* No response */
			nd->state = ncsi_dev_state_probe_dp;
			schedule_work(&ndp->work);
			break;
		}
		nd->state = ncsi_dev_state_probe_cis;
		if (IS_ENABLED(CONFIG_NCSI_OEM_CMD_GET_MAC) &&
		    ndp->mlx_multi_host)
			nd->state = ncsi_dev_state_probe_mlx_gma;

		schedule_work(&ndp->work);
		break;
	case ncsi_dev_state_probe_mlx_gma:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_OEM;
		nca.package = ndp->active_package->id;
		nca.channel = 0;
		ret = ncsi_oem_gma_handler_mlx(&nca);
		if (ret)
			goto error;

		nd->state = ncsi_dev_state_probe_mlx_smaf;
		break;
	case ncsi_dev_state_probe_mlx_smaf:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_OEM;
		nca.package = ndp->active_package->id;
		nca.channel = 0;
		ret = ncsi_oem_smaf_mlx(&nca);
		if (ret)
			goto error;

		nd->state = ncsi_dev_state_probe_cis;
		break;
	case ncsi_dev_state_probe_keep_phy:
		ndp->pending_req_num = 1;

		nca.type = NCSI_PKT_CMD_OEM;
		nca.package = ndp->active_package->id;
		nca.channel = 0;
		ret = ncsi_oem_keep_phy_intel(&nca);
		if (ret)
			goto error;

		nd->state = ncsi_dev_state_probe_gvi;
		break;
	case ncsi_dev_state_probe_cis:
	case ncsi_dev_state_probe_gvi:
	case ncsi_dev_state_probe_gc:
	case ncsi_dev_state_probe_gls:
		np = ndp->active_package;
		ndp->pending_req_num = 1;

		/* Clear initial state Retrieve version, capability or link status */
		if (nd->state == ncsi_dev_state_probe_cis)
			nca.type = NCSI_PKT_CMD_CIS;
		else if (nd->state == ncsi_dev_state_probe_gvi)
			nca.type = NCSI_PKT_CMD_GVI;
		else if (nd->state == ncsi_dev_state_probe_gc)
			nca.type = NCSI_PKT_CMD_GC;
		else
			nca.type = NCSI_PKT_CMD_GLS;

		nca.package = np->id;
		nca.channel = ndp->channel_probe_id;

		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;

		if (nd->state == ncsi_dev_state_probe_cis) {
			nd->state = ncsi_dev_state_probe_gvi;
			if (IS_ENABLED(CONFIG_NCSI_OEM_CMD_KEEP_PHY) && ndp->channel_probe_id == 0)
				nd->state = ncsi_dev_state_probe_keep_phy;
		} else if (nd->state == ncsi_dev_state_probe_gvi) {
			nd->state = ncsi_dev_state_probe_gc;
		} else if (nd->state == ncsi_dev_state_probe_gc) {
			nd->state = ncsi_dev_state_probe_gls;
		} else {
			nd->state = ncsi_dev_state_probe_cis;
			ndp->channel_probe_id++;
		}

		if (ndp->channel_probe_id == ndp->channel_count) {
			ndp->channel_probe_id = 0;
			nd->state = ncsi_dev_state_probe_dp;
		}
		break;
	case ncsi_dev_state_probe_dp:
		ndp->pending_req_num = 1;

		/* Deselect the current package */
		nca.type = NCSI_PKT_CMD_DP;
		nca.package = ndp->package_probe_id;
		nca.channel = NCSI_RESERVED_CHANNEL;
		ret = ncsi_xmit_cmd(&nca);
		if (ret)
			goto error;

		/* Probe next package after receiving response */
		ndp->package_probe_id++;
		nd->state = ncsi_dev_state_probe_package;
		ndp->active_package = NULL;
		break;
	default:
		netdev_warn(nd->dev, "Wrong NCSI state 0x%0x in enumeration\n",
			    nd->state);
	}

	if (ndp->flags & NCSI_DEV_PROBED) {
		/* Check if all packages have HWA support */
		ncsi_check_hwa(ndp);
		ncsi_choose_active_channel(ndp);
	}

	return;
error:
	netdev_err(ndp->ndev.dev,
		   "NCSI: Failed to transmit cmd 0x%x during probe\n",
		   nca.type);
	ncsi_report_link(ndp, true);
}

static void ncsi_dev_work(struct work_struct *work)
{
	struct ncsi_dev_priv *ndp = container_of(work,
			struct ncsi_dev_priv, work);
	struct ncsi_dev *nd = &ndp->ndev;

	switch (nd->state & ncsi_dev_state_major) {
	case ncsi_dev_state_probe:
		ncsi_probe_channel(ndp);
		break;
	case ncsi_dev_state_suspend:
		ncsi_suspend_channel(ndp);
		break;
	case ncsi_dev_state_config:
		ncsi_configure_channel(ndp);
		break;
	default:
		netdev_warn(nd->dev, "Wrong NCSI state 0x%x in workqueue\n",
			    nd->state);
	}
}

int ncsi_process_next_channel(struct ncsi_dev_priv *ndp)
{
	struct ncsi_channel *nc;
	int old_state;
	unsigned long flags;

	spin_lock_irqsave(&ndp->lock, flags);
	nc = list_first_or_null_rcu(&ndp->channel_queue,
				    struct ncsi_channel, link);
	if (!nc) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		goto out;
	}

	list_del_init(&nc->link);
	spin_unlock_irqrestore(&ndp->lock, flags);

	spin_lock_irqsave(&nc->lock, flags);
	old_state = nc->state;
	nc->state = NCSI_CHANNEL_INVISIBLE;
	spin_unlock_irqrestore(&nc->lock, flags);

	ndp->active_channel = nc;
	ndp->active_package = nc->package;

	switch (old_state) {
	case NCSI_CHANNEL_INACTIVE:
		ndp->ndev.state = ncsi_dev_state_config;
		netdev_dbg(ndp->ndev.dev, "NCSI: configuring channel %u\n",
	                   nc->id);
		ncsi_configure_channel(ndp);
		break;
	case NCSI_CHANNEL_ACTIVE:
		ndp->ndev.state = ncsi_dev_state_suspend;
		netdev_dbg(ndp->ndev.dev, "NCSI: suspending channel %u\n",
			   nc->id);
		ncsi_suspend_channel(ndp);
		break;
	default:
		netdev_err(ndp->ndev.dev, "Invalid state 0x%x on %d:%d\n",
			   old_state, nc->package->id, nc->id);
		ncsi_report_link(ndp, false);
		return -EINVAL;
	}

	return 0;

out:
	ndp->active_channel = NULL;
	ndp->active_package = NULL;
	if (ndp->flags & NCSI_DEV_RESHUFFLE) {
		ndp->flags &= ~NCSI_DEV_RESHUFFLE;
		return ncsi_choose_active_channel(ndp);
	}

	ncsi_report_link(ndp, false);
	return -ENODEV;
}

static int ncsi_kick_channels(struct ncsi_dev_priv *ndp)
{
	struct ncsi_dev *nd = &ndp->ndev;
	struct ncsi_channel *nc;
	struct ncsi_package *np;
	unsigned long flags;
	unsigned int n = 0;

	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			spin_lock_irqsave(&nc->lock, flags);

			/* Channels may be busy, mark dirty instead of
			 * kicking if;
			 * a) not ACTIVE (configured)
			 * b) in the channel_queue (to be configured)
			 * c) it's ndev is in the config state
			 */
			if (nc->state != NCSI_CHANNEL_ACTIVE) {
				if ((ndp->ndev.state & 0xff00) ==
						ncsi_dev_state_config ||
						!list_empty(&nc->link)) {
					netdev_dbg(nd->dev,
						   "NCSI: channel %p marked dirty\n",
						   nc);
					nc->reconfigure_needed = true;
				}
				spin_unlock_irqrestore(&nc->lock, flags);
				continue;
			}

			spin_unlock_irqrestore(&nc->lock, flags);

			ncsi_stop_channel_monitor(nc);
			spin_lock_irqsave(&nc->lock, flags);
			nc->state = NCSI_CHANNEL_INACTIVE;
			spin_unlock_irqrestore(&nc->lock, flags);

			spin_lock_irqsave(&ndp->lock, flags);
			list_add_tail_rcu(&nc->link, &ndp->channel_queue);
			spin_unlock_irqrestore(&ndp->lock, flags);

			netdev_dbg(nd->dev, "NCSI: kicked channel %p\n", nc);
			n++;
		}
	}

	return n;
}

int ncsi_vlan_rx_add_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct ncsi_dev_priv *ndp;
	unsigned int n_vids = 0;
	struct vlan_vid *vlan;
	struct ncsi_dev *nd;
	bool found = false;

	if (vid == 0)
		return 0;

	nd = ncsi_find_dev(dev);
	if (!nd) {
		netdev_warn(dev, "NCSI: No net_device?\n");
		return 0;
	}

	ndp = TO_NCSI_DEV_PRIV(nd);

	/* Add the VLAN id to our internal list */
	list_for_each_entry_rcu(vlan, &ndp->vlan_vids, list) {
		n_vids++;
		if (vlan->vid == vid) {
			netdev_dbg(dev, "NCSI: vid %u already registered\n",
				   vid);
			return 0;
		}
	}
	if (n_vids >= NCSI_MAX_VLAN_VIDS) {
		netdev_warn(dev,
			    "tried to add vlan id %u but NCSI max already registered (%u)\n",
			    vid, NCSI_MAX_VLAN_VIDS);
		return -ENOSPC;
	}

	vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);
	if (!vlan)
		return -ENOMEM;

	vlan->proto = proto;
	vlan->vid = vid;
	list_add_rcu(&vlan->list, &ndp->vlan_vids);

	netdev_dbg(dev, "NCSI: Added new vid %u\n", vid);

	found = ncsi_kick_channels(ndp) != 0;

	return found ? ncsi_process_next_channel(ndp) : 0;
}
EXPORT_SYMBOL_GPL(ncsi_vlan_rx_add_vid);

int ncsi_vlan_rx_kill_vid(struct net_device *dev, __be16 proto, u16 vid)
{
	struct vlan_vid *vlan, *tmp;
	struct ncsi_dev_priv *ndp;
	struct ncsi_dev *nd;
	bool found = false;

	if (vid == 0)
		return 0;

	nd = ncsi_find_dev(dev);
	if (!nd) {
		netdev_warn(dev, "NCSI: no net_device?\n");
		return 0;
	}

	ndp = TO_NCSI_DEV_PRIV(nd);

	/* Remove the VLAN id from our internal list */
	list_for_each_entry_safe(vlan, tmp, &ndp->vlan_vids, list)
		if (vlan->vid == vid) {
			netdev_dbg(dev, "NCSI: vid %u found, removing\n", vid);
			list_del_rcu(&vlan->list);
			found = true;
			kfree(vlan);
		}

	if (!found) {
		netdev_err(dev, "NCSI: vid %u wasn't registered!\n", vid);
		return -EINVAL;
	}

	found = ncsi_kick_channels(ndp) != 0;

	return found ? ncsi_process_next_channel(ndp) : 0;
}
EXPORT_SYMBOL_GPL(ncsi_vlan_rx_kill_vid);

struct ncsi_dev *ncsi_register_dev(struct net_device *dev,
				   void (*handler)(struct ncsi_dev *ndev))
{
	struct ncsi_dev_priv *ndp;
	struct ncsi_dev *nd;
	struct platform_device *pdev;
	struct device_node *np;
	unsigned long flags;
	int i;

	/* Check if the device has been registered or not */
	nd = ncsi_find_dev(dev);
	if (nd)
		return nd;

	/* Create NCSI device */
	ndp = kzalloc(sizeof(*ndp), GFP_ATOMIC);
	if (!ndp)
		return NULL;

	nd = &ndp->ndev;
	nd->state = ncsi_dev_state_registered;
	nd->dev = dev;
	nd->handler = handler;
	ndp->pending_req_num = 0;
	INIT_LIST_HEAD(&ndp->channel_queue);
	INIT_LIST_HEAD(&ndp->vlan_vids);
	INIT_WORK(&ndp->work, ncsi_dev_work);
	ndp->package_whitelist = UINT_MAX;

	/* Initialize private NCSI device */
	spin_lock_init(&ndp->lock);
	INIT_LIST_HEAD(&ndp->packages);
	ndp->request_id = NCSI_REQ_START_IDX;
	for (i = 0; i < ARRAY_SIZE(ndp->requests); i++) {
		ndp->requests[i].id = i;
		ndp->requests[i].ndp = ndp;
		timer_setup(&ndp->requests[i].timer, ncsi_request_timeout, 0);
	}
	ndp->channel_count = NCSI_RESERVED_CHANNEL;

	spin_lock_irqsave(&ncsi_dev_lock, flags);
	list_add_tail_rcu(&ndp->node, &ncsi_dev_list);
	spin_unlock_irqrestore(&ncsi_dev_lock, flags);

	/* Register NCSI packet Rx handler */
	ndp->ptype.type = cpu_to_be16(ETH_P_NCSI);
	ndp->ptype.func = ncsi_rcv_rsp;
	ndp->ptype.dev = dev;
	dev_add_pack(&ndp->ptype);

	pdev = to_platform_device(dev->dev.parent);
	if (pdev) {
		np = pdev->dev.of_node;
		if (np && (of_property_read_bool(np, "mellanox,multi-host") ||
			   of_property_read_bool(np, "mlx,multi-host")))
			ndp->mlx_multi_host = true;
	}

	return nd;
}
EXPORT_SYMBOL_GPL(ncsi_register_dev);

int ncsi_start_dev(struct ncsi_dev *nd)
{
	struct ncsi_dev_priv *ndp = TO_NCSI_DEV_PRIV(nd);

	if (nd->state != ncsi_dev_state_registered &&
	    nd->state != ncsi_dev_state_functional)
		return -ENOTTY;

	if (!(ndp->flags & NCSI_DEV_PROBED)) {
		ndp->package_probe_id = 0;
		ndp->channel_probe_id = 0;
		nd->state = ncsi_dev_state_probe;
		schedule_work(&ndp->work);
		return 0;
	}

	return ncsi_reset_dev(nd);
}
EXPORT_SYMBOL_GPL(ncsi_start_dev);

void ncsi_stop_dev(struct ncsi_dev *nd)
{
	struct ncsi_dev_priv *ndp = TO_NCSI_DEV_PRIV(nd);
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	bool chained;
	int old_state;
	unsigned long flags;

	/* Stop the channel monitor on any active channels. Don't reset the
	 * channel state so we know which were active when ncsi_start_dev()
	 * is next called.
	 */
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			ncsi_stop_channel_monitor(nc);

			spin_lock_irqsave(&nc->lock, flags);
			chained = !list_empty(&nc->link);
			old_state = nc->state;
			spin_unlock_irqrestore(&nc->lock, flags);

			WARN_ON_ONCE(chained ||
				     old_state == NCSI_CHANNEL_INVISIBLE);
		}
	}

	netdev_dbg(ndp->ndev.dev, "NCSI: Stopping device\n");
	ncsi_report_link(ndp, true);
}
EXPORT_SYMBOL_GPL(ncsi_stop_dev);

int ncsi_reset_dev(struct ncsi_dev *nd)
{
	struct ncsi_dev_priv *ndp = TO_NCSI_DEV_PRIV(nd);
	struct ncsi_channel *nc, *active, *tmp;
	struct ncsi_package *np;
	unsigned long flags;

	spin_lock_irqsave(&ndp->lock, flags);

	if (!(ndp->flags & NCSI_DEV_RESET)) {
		/* Haven't been called yet, check states */
		switch (nd->state & ncsi_dev_state_major) {
		case ncsi_dev_state_registered:
		case ncsi_dev_state_probe:
			/* Not even probed yet - do nothing */
			spin_unlock_irqrestore(&ndp->lock, flags);
			return 0;
		case ncsi_dev_state_suspend:
		case ncsi_dev_state_config:
			/* Wait for the channel to finish its suspend/config
			 * operation; once it finishes it will check for
			 * NCSI_DEV_RESET and reset the state.
			 */
			ndp->flags |= NCSI_DEV_RESET;
			spin_unlock_irqrestore(&ndp->lock, flags);
			return 0;
		}
	} else {
		switch (nd->state) {
		case ncsi_dev_state_suspend_done:
		case ncsi_dev_state_config_done:
		case ncsi_dev_state_functional:
			/* Ok */
			break;
		default:
			/* Current reset operation happening */
			spin_unlock_irqrestore(&ndp->lock, flags);
			return 0;
		}
	}

	if (!list_empty(&ndp->channel_queue)) {
		/* Clear any channel queue we may have interrupted */
		list_for_each_entry_safe(nc, tmp, &ndp->channel_queue, link)
			list_del_init(&nc->link);
	}
	spin_unlock_irqrestore(&ndp->lock, flags);

	active = NULL;
	NCSI_FOR_EACH_PACKAGE(ndp, np) {
		NCSI_FOR_EACH_CHANNEL(np, nc) {
			spin_lock_irqsave(&nc->lock, flags);

			if (nc->state == NCSI_CHANNEL_ACTIVE) {
				active = nc;
				nc->state = NCSI_CHANNEL_INVISIBLE;
				spin_unlock_irqrestore(&nc->lock, flags);
				ncsi_stop_channel_monitor(nc);
				break;
			}

			spin_unlock_irqrestore(&nc->lock, flags);
		}
		if (active)
			break;
	}

	if (!active) {
		/* Done */
		spin_lock_irqsave(&ndp->lock, flags);
		ndp->flags &= ~NCSI_DEV_RESET;
		spin_unlock_irqrestore(&ndp->lock, flags);
		return ncsi_choose_active_channel(ndp);
	}

	spin_lock_irqsave(&ndp->lock, flags);
	ndp->flags |= NCSI_DEV_RESET;
	ndp->active_channel = active;
	ndp->active_package = active->package;
	spin_unlock_irqrestore(&ndp->lock, flags);

	nd->state = ncsi_dev_state_suspend;
	schedule_work(&ndp->work);
	return 0;
}

void ncsi_unregister_dev(struct ncsi_dev *nd)
{
	struct ncsi_dev_priv *ndp = TO_NCSI_DEV_PRIV(nd);
	struct ncsi_package *np, *tmp;
	unsigned long flags;

	dev_remove_pack(&ndp->ptype);

	list_for_each_entry_safe(np, tmp, &ndp->packages, node)
		ncsi_remove_package(np);

	spin_lock_irqsave(&ncsi_dev_lock, flags);
	list_del_rcu(&ndp->node);
	spin_unlock_irqrestore(&ncsi_dev_lock, flags);

	disable_work_sync(&ndp->work);

	kfree(ndp);
}
EXPORT_SYMBOL_GPL(ncsi_unregister_dev);
