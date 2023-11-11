// SPDX-License-Identifier: GPL-2.0
/* Shared Memory Communications Direct over ISM devices (SMC-D)
 *
 * Functions for ISM device.
 *
 * Copyright IBM Corp. 2018
 */

#include <linux/if_vlan.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/page.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_ism.h"
#include "smc_pnet.h"
#include "smc_netlink.h"

struct smcd_dev_list smcd_dev_list = {
	.list = LIST_HEAD_INIT(smcd_dev_list.list),
	.mutex = __MUTEX_INITIALIZER(smcd_dev_list.mutex)
};

static bool smc_ism_v2_capable;
static u8 smc_ism_v2_system_eid[SMC_MAX_EID_LEN];

/* Test if an ISM communication is possible - same CPC */
int smc_ism_cantalk(u64 peer_gid, unsigned short vlan_id, struct smcd_dev *smcd)
{
	return smcd->ops->query_remote_gid(smcd, peer_gid, vlan_id ? 1 : 0,
					   vlan_id);
}

void smc_ism_get_system_eid(u8 **eid)
{
	if (!smc_ism_v2_capable)
		*eid = NULL;
	else
		*eid = smc_ism_v2_system_eid;
}

u16 smc_ism_get_chid(struct smcd_dev *smcd)
{
	return smcd->ops->get_chid(smcd);
}

/* HW supports ISM V2 and thus System EID is defined */
bool smc_ism_is_v2_capable(void)
{
	return smc_ism_v2_capable;
}

/* Set a connection using this DMBE. */
void smc_ism_set_conn(struct smc_connection *conn)
{
	unsigned long flags;

	spin_lock_irqsave(&conn->lgr->smcd->lock, flags);
	conn->lgr->smcd->conn[conn->rmb_desc->sba_idx] = conn;
	spin_unlock_irqrestore(&conn->lgr->smcd->lock, flags);
}

/* Unset a connection using this DMBE. */
void smc_ism_unset_conn(struct smc_connection *conn)
{
	unsigned long flags;

	if (!conn->rmb_desc)
		return;

	spin_lock_irqsave(&conn->lgr->smcd->lock, flags);
	conn->lgr->smcd->conn[conn->rmb_desc->sba_idx] = NULL;
	spin_unlock_irqrestore(&conn->lgr->smcd->lock, flags);
}

/* Register a VLAN identifier with the ISM device. Use a reference count
 * and add a VLAN identifier only when the first DMB using this VLAN is
 * registered.
 */
int smc_ism_get_vlan(struct smcd_dev *smcd, unsigned short vlanid)
{
	struct smc_ism_vlanid *new_vlan, *vlan;
	unsigned long flags;
	int rc = 0;

	if (!vlanid)			/* No valid vlan id */
		return -EINVAL;

	/* create new vlan entry, in case we need it */
	new_vlan = kzalloc(sizeof(*new_vlan), GFP_KERNEL);
	if (!new_vlan)
		return -ENOMEM;
	new_vlan->vlanid = vlanid;
	refcount_set(&new_vlan->refcnt, 1);

	/* if there is an existing entry, increase count and return */
	spin_lock_irqsave(&smcd->lock, flags);
	list_for_each_entry(vlan, &smcd->vlan, list) {
		if (vlan->vlanid == vlanid) {
			refcount_inc(&vlan->refcnt);
			kfree(new_vlan);
			goto out;
		}
	}

	/* no existing entry found.
	 * add new entry to device; might fail, e.g., if HW limit reached
	 */
	if (smcd->ops->add_vlan_id(smcd, vlanid)) {
		kfree(new_vlan);
		rc = -EIO;
		goto out;
	}
	list_add_tail(&new_vlan->list, &smcd->vlan);
out:
	spin_unlock_irqrestore(&smcd->lock, flags);
	return rc;
}

/* Unregister a VLAN identifier with the ISM device. Use a reference count
 * and remove a VLAN identifier only when the last DMB using this VLAN is
 * unregistered.
 */
int smc_ism_put_vlan(struct smcd_dev *smcd, unsigned short vlanid)
{
	struct smc_ism_vlanid *vlan;
	unsigned long flags;
	bool found = false;
	int rc = 0;

	if (!vlanid)			/* No valid vlan id */
		return -EINVAL;

	spin_lock_irqsave(&smcd->lock, flags);
	list_for_each_entry(vlan, &smcd->vlan, list) {
		if (vlan->vlanid == vlanid) {
			if (!refcount_dec_and_test(&vlan->refcnt))
				goto out;
			found = true;
			break;
		}
	}
	if (!found) {
		rc = -ENOENT;
		goto out;		/* VLAN id not in table */
	}

	/* Found and the last reference just gone */
	if (smcd->ops->del_vlan_id(smcd, vlanid))
		rc = -EIO;
	list_del(&vlan->list);
	kfree(vlan);
out:
	spin_unlock_irqrestore(&smcd->lock, flags);
	return rc;
}

int smc_ism_unregister_dmb(struct smcd_dev *smcd, struct smc_buf_desc *dmb_desc)
{
	struct smcd_dmb dmb;
	int rc = 0;

	if (!dmb_desc->dma_addr)
		return rc;

	memset(&dmb, 0, sizeof(dmb));
	dmb.dmb_tok = dmb_desc->token;
	dmb.sba_idx = dmb_desc->sba_idx;
	dmb.cpu_addr = dmb_desc->cpu_addr;
	dmb.dma_addr = dmb_desc->dma_addr;
	dmb.dmb_len = dmb_desc->len;
	rc = smcd->ops->unregister_dmb(smcd, &dmb);
	if (!rc || rc == ISM_ERROR) {
		dmb_desc->cpu_addr = NULL;
		dmb_desc->dma_addr = 0;
	}

	return rc;
}

int smc_ism_register_dmb(struct smc_link_group *lgr, int dmb_len,
			 struct smc_buf_desc *dmb_desc)
{
	struct smcd_dmb dmb;
	int rc;

	memset(&dmb, 0, sizeof(dmb));
	dmb.dmb_len = dmb_len;
	dmb.sba_idx = dmb_desc->sba_idx;
	dmb.vlan_id = lgr->vlan_id;
	dmb.rgid = lgr->peer_gid;
	rc = lgr->smcd->ops->register_dmb(lgr->smcd, &dmb);
	if (!rc) {
		dmb_desc->sba_idx = dmb.sba_idx;
		dmb_desc->token = dmb.dmb_tok;
		dmb_desc->cpu_addr = dmb.cpu_addr;
		dmb_desc->dma_addr = dmb.dma_addr;
		dmb_desc->len = dmb.dmb_len;
	}
	return rc;
}

static int smc_nl_handle_smcd_dev(struct smcd_dev *smcd,
				  struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	char smc_pnet[SMC_MAX_PNETID_LEN + 1];
	struct smc_pci_dev smc_pci_dev;
	struct nlattr *port_attrs;
	struct nlattr *attrs;
	int use_cnt = 0;
	void *nlh;

	nlh = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &smc_gen_nl_family, NLM_F_MULTI,
			  SMC_NETLINK_GET_DEV_SMCD);
	if (!nlh)
		goto errmsg;
	attrs = nla_nest_start(skb, SMC_GEN_DEV_SMCD);
	if (!attrs)
		goto errout;
	use_cnt = atomic_read(&smcd->lgr_cnt);
	if (nla_put_u32(skb, SMC_NLA_DEV_USE_CNT, use_cnt))
		goto errattr;
	if (nla_put_u8(skb, SMC_NLA_DEV_IS_CRIT, use_cnt > 0))
		goto errattr;
	memset(&smc_pci_dev, 0, sizeof(smc_pci_dev));
	smc_set_pci_values(to_pci_dev(smcd->dev.parent), &smc_pci_dev);
	if (nla_put_u32(skb, SMC_NLA_DEV_PCI_FID, smc_pci_dev.pci_fid))
		goto errattr;
	if (nla_put_u16(skb, SMC_NLA_DEV_PCI_CHID, smc_pci_dev.pci_pchid))
		goto errattr;
	if (nla_put_u16(skb, SMC_NLA_DEV_PCI_VENDOR, smc_pci_dev.pci_vendor))
		goto errattr;
	if (nla_put_u16(skb, SMC_NLA_DEV_PCI_DEVICE, smc_pci_dev.pci_device))
		goto errattr;
	if (nla_put_string(skb, SMC_NLA_DEV_PCI_ID, smc_pci_dev.pci_id))
		goto errattr;

	port_attrs = nla_nest_start(skb, SMC_NLA_DEV_PORT);
	if (!port_attrs)
		goto errattr;
	if (nla_put_u8(skb, SMC_NLA_DEV_PORT_PNET_USR, smcd->pnetid_by_user))
		goto errportattr;
	memcpy(smc_pnet, smcd->pnetid, SMC_MAX_PNETID_LEN);
	smc_pnet[SMC_MAX_PNETID_LEN] = 0;
	if (nla_put_string(skb, SMC_NLA_DEV_PORT_PNETID, smc_pnet))
		goto errportattr;

	nla_nest_end(skb, port_attrs);
	nla_nest_end(skb, attrs);
	genlmsg_end(skb, nlh);
	return 0;

errportattr:
	nla_nest_cancel(skb, port_attrs);
errattr:
	nla_nest_cancel(skb, attrs);
errout:
	nlmsg_cancel(skb, nlh);
errmsg:
	return -EMSGSIZE;
}

static void smc_nl_prep_smcd_dev(struct smcd_dev_list *dev_list,
				 struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct smc_nl_dmp_ctx *cb_ctx = smc_nl_dmp_ctx(cb);
	int snum = cb_ctx->pos[0];
	struct smcd_dev *smcd;
	int num = 0;

	mutex_lock(&dev_list->mutex);
	list_for_each_entry(smcd, &dev_list->list, list) {
		if (num < snum)
			goto next;
		if (smc_nl_handle_smcd_dev(smcd, skb, cb))
			goto errout;
next:
		num++;
	}
errout:
	mutex_unlock(&dev_list->mutex);
	cb_ctx->pos[0] = num;
}

int smcd_nl_get_device(struct sk_buff *skb, struct netlink_callback *cb)
{
	smc_nl_prep_smcd_dev(&smcd_dev_list, skb, cb);
	return skb->len;
}

struct smc_ism_event_work {
	struct work_struct work;
	struct smcd_dev *smcd;
	struct smcd_event event;
};

#define ISM_EVENT_REQUEST		0x0001
#define ISM_EVENT_RESPONSE		0x0002
#define ISM_EVENT_REQUEST_IR		0x00000001
#define ISM_EVENT_CODE_SHUTDOWN		0x80
#define ISM_EVENT_CODE_TESTLINK		0x83

union smcd_sw_event_info {
	u64	info;
	struct {
		u8		uid[SMC_LGR_ID_SIZE];
		unsigned short	vlan_id;
		u16		code;
	};
};

static void smcd_handle_sw_event(struct smc_ism_event_work *wrk)
{
	union smcd_sw_event_info ev_info;

	ev_info.info = wrk->event.info;
	switch (wrk->event.code) {
	case ISM_EVENT_CODE_SHUTDOWN:	/* Peer shut down DMBs */
		smc_smcd_terminate(wrk->smcd, wrk->event.tok, ev_info.vlan_id);
		break;
	case ISM_EVENT_CODE_TESTLINK:	/* Activity timer */
		if (ev_info.code == ISM_EVENT_REQUEST) {
			ev_info.code = ISM_EVENT_RESPONSE;
			wrk->smcd->ops->signal_event(wrk->smcd,
						     wrk->event.tok,
						     ISM_EVENT_REQUEST_IR,
						     ISM_EVENT_CODE_TESTLINK,
						     ev_info.info);
			}
		break;
	}
}

int smc_ism_signal_shutdown(struct smc_link_group *lgr)
{
	int rc;
	union smcd_sw_event_info ev_info;

	if (lgr->peer_shutdown)
		return 0;

	memcpy(ev_info.uid, lgr->id, SMC_LGR_ID_SIZE);
	ev_info.vlan_id = lgr->vlan_id;
	ev_info.code = ISM_EVENT_REQUEST;
	rc = lgr->smcd->ops->signal_event(lgr->smcd, lgr->peer_gid,
					  ISM_EVENT_REQUEST_IR,
					  ISM_EVENT_CODE_SHUTDOWN,
					  ev_info.info);
	return rc;
}

/* worker for SMC-D events */
static void smc_ism_event_work(struct work_struct *work)
{
	struct smc_ism_event_work *wrk =
		container_of(work, struct smc_ism_event_work, work);

	switch (wrk->event.type) {
	case ISM_EVENT_GID:	/* GID event, token is peer GID */
		smc_smcd_terminate(wrk->smcd, wrk->event.tok, VLAN_VID_MASK);
		break;
	case ISM_EVENT_DMB:
		break;
	case ISM_EVENT_SWR:	/* Software defined event */
		smcd_handle_sw_event(wrk);
		break;
	}
	kfree(wrk);
}

static void smcd_release(struct device *dev)
{
	struct smcd_dev *smcd = container_of(dev, struct smcd_dev, dev);

	kfree(smcd->conn);
	kfree(smcd);
}

struct smcd_dev *smcd_alloc_dev(struct device *parent, const char *name,
				const struct smcd_ops *ops, int max_dmbs)
{
	struct smcd_dev *smcd;

	smcd = kzalloc(sizeof(*smcd), GFP_KERNEL);
	if (!smcd)
		return NULL;
	smcd->conn = kcalloc(max_dmbs, sizeof(struct smc_connection *),
			     GFP_KERNEL);
	if (!smcd->conn) {
		kfree(smcd);
		return NULL;
	}

	smcd->event_wq = alloc_ordered_workqueue("ism_evt_wq-%s)",
						 WQ_MEM_RECLAIM, name);
	if (!smcd->event_wq) {
		kfree(smcd->conn);
		kfree(smcd);
		return NULL;
	}

	smcd->dev.parent = parent;
	smcd->dev.release = smcd_release;
	device_initialize(&smcd->dev);
	dev_set_name(&smcd->dev, name);
	smcd->ops = ops;
	if (smc_pnetid_by_dev_port(parent, 0, smcd->pnetid))
		smc_pnetid_by_table_smcd(smcd);

	spin_lock_init(&smcd->lock);
	spin_lock_init(&smcd->lgr_lock);
	INIT_LIST_HEAD(&smcd->vlan);
	INIT_LIST_HEAD(&smcd->lgr_list);
	init_waitqueue_head(&smcd->lgrs_deleted);
	return smcd;
}
EXPORT_SYMBOL_GPL(smcd_alloc_dev);

int smcd_register_dev(struct smcd_dev *smcd)
{
	int rc;

	mutex_lock(&smcd_dev_list.mutex);
	if (list_empty(&smcd_dev_list.list)) {
		u8 *system_eid = NULL;

		system_eid = smcd->ops->get_system_eid();
		if (system_eid[24] != '0' || system_eid[28] != '0') {
			smc_ism_v2_capable = true;
			memcpy(smc_ism_v2_system_eid, system_eid,
			       SMC_MAX_EID_LEN);
		}
	}
	/* sort list: devices without pnetid before devices with pnetid */
	if (smcd->pnetid[0])
		list_add_tail(&smcd->list, &smcd_dev_list.list);
	else
		list_add(&smcd->list, &smcd_dev_list.list);
	mutex_unlock(&smcd_dev_list.mutex);

	pr_warn_ratelimited("smc: adding smcd device %s with pnetid %.16s%s\n",
			    dev_name(&smcd->dev), smcd->pnetid,
			    smcd->pnetid_by_user ? " (user defined)" : "");

	rc = device_add(&smcd->dev);
	if (rc) {
		mutex_lock(&smcd_dev_list.mutex);
		list_del(&smcd->list);
		mutex_unlock(&smcd_dev_list.mutex);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(smcd_register_dev);

void smcd_unregister_dev(struct smcd_dev *smcd)
{
	pr_warn_ratelimited("smc: removing smcd device %s\n",
			    dev_name(&smcd->dev));
	mutex_lock(&smcd_dev_list.mutex);
	list_del_init(&smcd->list);
	mutex_unlock(&smcd_dev_list.mutex);
	smcd->going_away = 1;
	smc_smcd_terminate_all(smcd);
	destroy_workqueue(smcd->event_wq);

	device_del(&smcd->dev);
}
EXPORT_SYMBOL_GPL(smcd_unregister_dev);

void smcd_free_dev(struct smcd_dev *smcd)
{
	put_device(&smcd->dev);
}
EXPORT_SYMBOL_GPL(smcd_free_dev);

/* SMCD Device event handler. Called from ISM device interrupt handler.
 * Parameters are smcd device pointer,
 * - event->type (0 --> DMB, 1 --> GID),
 * - event->code (event code),
 * - event->tok (either DMB token when event type 0, or GID when event type 1)
 * - event->time (time of day)
 * - event->info (debug info).
 *
 * Context:
 * - Function called in IRQ context from ISM device driver event handler.
 */
void smcd_handle_event(struct smcd_dev *smcd, struct smcd_event *event)
{
	struct smc_ism_event_work *wrk;

	if (smcd->going_away)
		return;
	/* copy event to event work queue, and let it be handled there */
	wrk = kmalloc(sizeof(*wrk), GFP_ATOMIC);
	if (!wrk)
		return;
	INIT_WORK(&wrk->work, smc_ism_event_work);
	wrk->smcd = smcd;
	wrk->event = *event;
	queue_work(smcd->event_wq, &wrk->work);
}
EXPORT_SYMBOL_GPL(smcd_handle_event);

/* SMCD Device interrupt handler. Called from ISM device interrupt handler.
 * Parameters are smcd device pointer, DMB number, and the DMBE bitmask.
 * Find the connection and schedule the tasklet for this connection.
 *
 * Context:
 * - Function called in IRQ context from ISM device driver IRQ handler.
 */
void smcd_handle_irq(struct smcd_dev *smcd, unsigned int dmbno, u16 dmbemask)
{
	struct smc_connection *conn = NULL;
	unsigned long flags;

	spin_lock_irqsave(&smcd->lock, flags);
	conn = smcd->conn[dmbno];
	if (conn && !conn->killed)
		tasklet_schedule(&conn->rx_tsklet);
	spin_unlock_irqrestore(&smcd->lock, flags);
}
EXPORT_SYMBOL_GPL(smcd_handle_irq);

void __init smc_ism_init(void)
{
	smc_ism_v2_capable = false;
	memset(smc_ism_v2_system_eid, 0, SMC_MAX_EID_LEN);
}
