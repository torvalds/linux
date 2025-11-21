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
#include "linux/dibs.h"

struct smcd_dev_list smcd_dev_list = {
	.list = LIST_HEAD_INIT(smcd_dev_list.list),
	.mutex = __MUTEX_INITIALIZER(smcd_dev_list.mutex)
};

static bool smc_ism_v2_capable;
static u8 smc_ism_v2_system_eid[SMC_MAX_EID_LEN];

static void smcd_register_dev(struct dibs_dev *dibs);
static void smcd_unregister_dev(struct dibs_dev *dibs);
static void smcd_handle_event(struct dibs_dev *dibs,
			      const struct dibs_event *event);
static void smcd_handle_irq(struct dibs_dev *dibs, unsigned int dmbno,
			    u16 dmbemask);

static struct dibs_client_ops smc_client_ops = {
	.add_dev = smcd_register_dev,
	.del_dev = smcd_unregister_dev,
	.handle_event = smcd_handle_event,
	.handle_irq = smcd_handle_irq,
};

static struct dibs_client smc_dibs_client = {
	.name = "SMC-D",
	.ops = &smc_client_ops,
};

static void smc_ism_create_system_eid(void)
{
	struct smc_ism_seid *seid =
		(struct smc_ism_seid *)smc_ism_v2_system_eid;
#if IS_ENABLED(CONFIG_S390)
	struct cpuid id;
	u16 ident_tail;
	char tmp[5];

	memcpy(seid->seid_string, "IBM-SYSZ-ISMSEID00000000", 24);
	get_cpu_id(&id);
	ident_tail = (u16)(id.ident & SMC_ISM_IDENT_MASK);
	snprintf(tmp, 5, "%04X", ident_tail);
	memcpy(seid->serial_number, tmp, 4);
	snprintf(tmp, 5, "%04X", id.machine);
	memcpy(seid->type, tmp, 4);
#else
	memset(seid, 0, SMC_MAX_EID_LEN);
#endif
}

/* Test if an ISM communication is possible - same CPC */
int smc_ism_cantalk(struct smcd_gid *peer_gid, unsigned short vlan_id,
		    struct smcd_dev *smcd)
{
	struct dibs_dev *dibs = smcd->dibs;
	uuid_t ism_rgid;

	copy_to_dibsgid(&ism_rgid, peer_gid);
	return dibs->ops->query_remote_gid(dibs, &ism_rgid, vlan_id ? 1 : 0,
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
	return smcd->dibs->ops->get_fabric_id(smcd->dibs);
}

/* HW supports ISM V2 and thus System EID is defined */
bool smc_ism_is_v2_capable(void)
{
	return smc_ism_v2_capable;
}

void smc_ism_set_v2_capable(void)
{
	smc_ism_v2_capable = true;
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
	if (!smcd->dibs->ops->add_vlan_id)
		return -EOPNOTSUPP;

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
	if (smcd->dibs->ops->add_vlan_id(smcd->dibs, vlanid)) {
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
	if (!smcd->dibs->ops->del_vlan_id)
		return -EOPNOTSUPP;

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
	if (smcd->dibs->ops->del_vlan_id(smcd->dibs, vlanid))
		rc = -EIO;
	list_del(&vlan->list);
	kfree(vlan);
out:
	spin_unlock_irqrestore(&smcd->lock, flags);
	return rc;
}

void smc_ism_unregister_dmb(struct smcd_dev *smcd,
			    struct smc_buf_desc *dmb_desc)
{
	struct dibs_dmb dmb;

	if (!dmb_desc->dma_addr)
		return;

	memset(&dmb, 0, sizeof(dmb));
	dmb.dmb_tok = dmb_desc->token;
	dmb.idx = dmb_desc->sba_idx;
	dmb.cpu_addr = dmb_desc->cpu_addr;
	dmb.dma_addr = dmb_desc->dma_addr;
	dmb.dmb_len = dmb_desc->len;

	smcd->dibs->ops->unregister_dmb(smcd->dibs, &dmb);

	return;
}

int smc_ism_register_dmb(struct smc_link_group *lgr, int dmb_len,
			 struct smc_buf_desc *dmb_desc)
{
	struct dibs_dev *dibs;
	struct dibs_dmb dmb;
	int rc;

	memset(&dmb, 0, sizeof(dmb));
	dmb.dmb_len = dmb_len;
	dmb.idx = dmb_desc->sba_idx;
	dmb.vlan_id = lgr->vlan_id;
	copy_to_dibsgid(&dmb.rgid, &lgr->peer_gid);

	dibs = lgr->smcd->dibs;
	rc = dibs->ops->register_dmb(dibs, &dmb, &smc_dibs_client);
	if (!rc) {
		dmb_desc->sba_idx = dmb.idx;
		dmb_desc->token = dmb.dmb_tok;
		dmb_desc->cpu_addr = dmb.cpu_addr;
		dmb_desc->dma_addr = dmb.dma_addr;
		dmb_desc->len = dmb.dmb_len;
	}
	return rc;
}

bool smc_ism_support_dmb_nocopy(struct smcd_dev *smcd)
{
	/* for now only loopback-ism supports
	 * merging sndbuf with peer DMB to avoid
	 * data copies between them.
	 */
	return (smcd->dibs->ops->support_mmapped_rdmb &&
		smcd->dibs->ops->support_mmapped_rdmb(smcd->dibs));
}

int smc_ism_attach_dmb(struct smcd_dev *dev, u64 token,
		       struct smc_buf_desc *dmb_desc)
{
	struct dibs_dmb dmb;
	int rc = 0;

	if (!dev->dibs->ops->attach_dmb)
		return -EINVAL;

	memset(&dmb, 0, sizeof(dmb));
	dmb.dmb_tok = token;
	rc = dev->dibs->ops->attach_dmb(dev->dibs, &dmb);
	if (!rc) {
		dmb_desc->sba_idx = dmb.idx;
		dmb_desc->token = dmb.dmb_tok;
		dmb_desc->cpu_addr = dmb.cpu_addr;
		dmb_desc->dma_addr = dmb.dma_addr;
		dmb_desc->len = dmb.dmb_len;
		dmb_desc->is_attached = true;
	}
	return rc;
}

int smc_ism_detach_dmb(struct smcd_dev *dev, u64 token)
{
	if (!dev->dibs->ops->detach_dmb)
		return -EINVAL;

	return dev->dibs->ops->detach_dmb(dev->dibs, token);
}

static int smc_nl_handle_smcd_dev(struct smcd_dev *smcd,
				  struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	char smc_pnet[SMC_MAX_PNETID_LEN + 1];
	struct smc_pci_dev smc_pci_dev;
	struct nlattr *port_attrs;
	struct dibs_dev *dibs;
	struct nlattr *attrs;
	int use_cnt = 0;
	void *nlh;

	dibs = smcd->dibs;
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
	smc_set_pci_values(to_pci_dev(dibs->dev.parent), &smc_pci_dev);
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
		if (smc_ism_is_loopback(smcd->dibs))
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
	struct dibs_event event;
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
	struct dibs_dev *dibs = wrk->smcd->dibs;
	union smcd_sw_event_info ev_info;
	struct smcd_gid peer_gid;
	uuid_t ism_rgid;

	copy_to_smcdgid(&peer_gid, &wrk->event.gid);
	ev_info.info = wrk->event.data;
	switch (wrk->event.subtype) {
	case ISM_EVENT_CODE_SHUTDOWN:	/* Peer shut down DMBs */
		smc_smcd_terminate(wrk->smcd, &peer_gid, ev_info.vlan_id);
		break;
	case ISM_EVENT_CODE_TESTLINK:	/* Activity timer */
		if (ev_info.code == ISM_EVENT_REQUEST &&
		    dibs->ops->signal_event) {
			ev_info.code = ISM_EVENT_RESPONSE;
			copy_to_dibsgid(&ism_rgid, &peer_gid);
			dibs->ops->signal_event(dibs, &ism_rgid,
					       ISM_EVENT_REQUEST_IR,
					       ISM_EVENT_CODE_TESTLINK,
					       ev_info.info);
		}
		break;
	}
}

/* worker for SMC-D events */
static void smc_ism_event_work(struct work_struct *work)
{
	struct smc_ism_event_work *wrk =
		container_of(work, struct smc_ism_event_work, work);
	struct smcd_gid smcd_gid;

	copy_to_smcdgid(&smcd_gid, &wrk->event.gid);

	switch (wrk->event.type) {
	case DIBS_DEV_EVENT: /* GID event, token is peer GID */
		smc_smcd_terminate(wrk->smcd, &smcd_gid, VLAN_VID_MASK);
		break;
	case DIBS_BUF_EVENT:
		break;
	case DIBS_SW_EVENT: /* Software defined event */
		smcd_handle_sw_event(wrk);
		break;
	}
	kfree(wrk);
}

static struct smcd_dev *smcd_alloc_dev(const char *name, int max_dmbs)
{
	struct smcd_dev *smcd;

	smcd = kzalloc(sizeof(*smcd), GFP_KERNEL);
	if (!smcd)
		return NULL;
	smcd->conn = kcalloc(max_dmbs, sizeof(struct smc_connection *),
			     GFP_KERNEL);
	if (!smcd->conn)
		goto free_smcd;

	smcd->event_wq = alloc_ordered_workqueue("ism_evt_wq-%s)",
						 WQ_MEM_RECLAIM, name);
	if (!smcd->event_wq)
		goto free_conn;

	spin_lock_init(&smcd->lock);
	spin_lock_init(&smcd->lgr_lock);
	INIT_LIST_HEAD(&smcd->vlan);
	INIT_LIST_HEAD(&smcd->lgr_list);
	init_waitqueue_head(&smcd->lgrs_deleted);
	return smcd;

free_conn:
	kfree(smcd->conn);
free_smcd:
	kfree(smcd);
	return NULL;
}

static void smcd_register_dev(struct dibs_dev *dibs)
{
	struct smcd_dev *smcd, *fentry;
	int max_dmbs;

	max_dmbs = dibs->ops->max_dmbs();

	smcd = smcd_alloc_dev(dev_name(&dibs->dev), max_dmbs);
	if (!smcd)
		return;

	smcd->dibs = dibs;
	dibs_set_priv(dibs, &smc_dibs_client, smcd);

	if (smc_pnetid_by_dev_port(dibs->dev.parent, 0, smcd->pnetid))
		smc_pnetid_by_table_smcd(smcd);

	if (smc_ism_is_loopback(dibs) ||
	    (dibs->ops->add_vlan_id &&
	     !dibs->ops->add_vlan_id(dibs, ISM_RESERVED_VLANID))) {
		smc_ism_set_v2_capable();
	}

	mutex_lock(&smcd_dev_list.mutex);
	/* sort list:
	 * - devices without pnetid before devices with pnetid;
	 * - loopback-ism always at the very beginning;
	 */
	if (!smcd->pnetid[0]) {
		fentry = list_first_entry_or_null(&smcd_dev_list.list,
						  struct smcd_dev, list);
		if (fentry && smc_ism_is_loopback(fentry->dibs))
			list_add(&smcd->list, &fentry->list);
		else
			list_add(&smcd->list, &smcd_dev_list.list);
	} else {
		list_add_tail(&smcd->list, &smcd_dev_list.list);
	}
	mutex_unlock(&smcd_dev_list.mutex);

	if (smc_pnet_is_pnetid_set(smcd->pnetid))
		pr_warn_ratelimited("smc: adding smcd device %s with pnetid %.16s%s\n",
				    dev_name(&dibs->dev), smcd->pnetid,
				    smcd->pnetid_by_user ?
					" (user defined)" :
					"");
	else
		pr_warn_ratelimited("smc: adding smcd device %s without pnetid\n",
				    dev_name(&dibs->dev));
	return;
}

static void smcd_unregister_dev(struct dibs_dev *dibs)
{
	struct smcd_dev *smcd = dibs_get_priv(dibs, &smc_dibs_client);

	pr_warn_ratelimited("smc: removing smcd device %s\n",
			    dev_name(&dibs->dev));
	smcd->going_away = 1;
	smc_smcd_terminate_all(smcd);
	mutex_lock(&smcd_dev_list.mutex);
	list_del_init(&smcd->list);
	mutex_unlock(&smcd_dev_list.mutex);
	destroy_workqueue(smcd->event_wq);
	kfree(smcd->conn);
	kfree(smcd);
}

/* SMCD Device event handler. Called from ISM device interrupt handler.
 * Parameters are ism device pointer,
 * - event->type (0 --> DMB, 1 --> GID),
 * - event->code (event code),
 * - event->tok (either DMB token when event type 0, or GID when event type 1)
 * - event->time (time of day)
 * - event->info (debug info).
 *
 * Context:
 * - Function called in IRQ context from ISM device driver event handler.
 */
static void smcd_handle_event(struct dibs_dev *dibs,
			      const struct dibs_event *event)
{
	struct smcd_dev *smcd = dibs_get_priv(dibs, &smc_dibs_client);
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

/* SMCD Device interrupt handler. Called from ISM device interrupt handler.
 * Parameters are the ism device pointer, DMB number, and the DMBE bitmask.
 * Find the connection and schedule the tasklet for this connection.
 *
 * Context:
 * - Function called in IRQ context from ISM device driver IRQ handler.
 */
static void smcd_handle_irq(struct dibs_dev *dibs, unsigned int dmbno,
			    u16 dmbemask)
{
	struct smcd_dev *smcd = dibs_get_priv(dibs, &smc_dibs_client);
	struct smc_connection *conn = NULL;
	unsigned long flags;

	spin_lock_irqsave(&smcd->lock, flags);
	conn = smcd->conn[dmbno];
	if (conn && !conn->killed)
		tasklet_schedule(&conn->rx_tsklet);
	spin_unlock_irqrestore(&smcd->lock, flags);
}

int smc_ism_signal_shutdown(struct smc_link_group *lgr)
{
	int rc = 0;
	union smcd_sw_event_info ev_info;
	uuid_t ism_rgid;

	if (lgr->peer_shutdown)
		return 0;
	if (!lgr->smcd->dibs->ops->signal_event)
		return 0;

	memcpy(ev_info.uid, lgr->id, SMC_LGR_ID_SIZE);
	ev_info.vlan_id = lgr->vlan_id;
	ev_info.code = ISM_EVENT_REQUEST;
	copy_to_dibsgid(&ism_rgid, &lgr->peer_gid);
	rc = lgr->smcd->dibs->ops->signal_event(lgr->smcd->dibs, &ism_rgid,
					  ISM_EVENT_REQUEST_IR,
					  ISM_EVENT_CODE_SHUTDOWN,
					  ev_info.info);
	return rc;
}

int smc_ism_init(void)
{
	int rc = 0;

	smc_ism_v2_capable = false;
	smc_ism_create_system_eid();

	rc = dibs_register_client(&smc_dibs_client);
	return rc;
}

void smc_ism_exit(void)
{
	dibs_unregister_client(&smc_dibs_client);
}
