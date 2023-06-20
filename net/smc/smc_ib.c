// SPDX-License-Identifier: GPL-2.0
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  IB infrastructure:
 *  Establish SMC-R as an Infiniband Client to be notified about added and
 *  removed IB devices of type RDMA.
 *  Determine device and port characteristics for these IB devices.
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/random.h>
#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/inetdevice.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>

#include "smc_pnet.h"
#include "smc_ib.h"
#include "smc_core.h"
#include "smc_wr.h"
#include "smc.h"
#include "smc_netlink.h"

#define SMC_MAX_CQE 32766	/* max. # of completion queue elements */

#define SMC_QP_MIN_RNR_TIMER		5
#define SMC_QP_TIMEOUT			15 /* 4096 * 2 ** timeout usec */
#define SMC_QP_RETRY_CNT			7 /* 7: infinite */
#define SMC_QP_RNR_RETRY			7 /* 7: infinite */

struct smc_ib_devices smc_ib_devices = {	/* smc-registered ib devices */
	.mutex = __MUTEX_INITIALIZER(smc_ib_devices.mutex),
	.list = LIST_HEAD_INIT(smc_ib_devices.list),
};

u8 local_systemid[SMC_SYSTEMID_LEN];		/* unique system identifier */

static int smc_ib_modify_qp_init(struct smc_link *lnk)
{
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.pkey_index = 0;
	qp_attr.port_num = lnk->ibport;
	qp_attr.qp_access_flags = IB_ACCESS_LOCAL_WRITE
				| IB_ACCESS_REMOTE_WRITE;
	return ib_modify_qp(lnk->roce_qp, &qp_attr,
			    IB_QP_STATE | IB_QP_PKEY_INDEX |
			    IB_QP_ACCESS_FLAGS | IB_QP_PORT);
}

static int smc_ib_modify_qp_rtr(struct smc_link *lnk)
{
	enum ib_qp_attr_mask qp_attr_mask =
		IB_QP_STATE | IB_QP_AV | IB_QP_PATH_MTU | IB_QP_DEST_QPN |
		IB_QP_RQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_MIN_RNR_TIMER;
	struct ib_qp_attr qp_attr;
	u8 hop_lim = 1;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RTR;
	qp_attr.path_mtu = min(lnk->path_mtu, lnk->peer_mtu);
	qp_attr.ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;
	rdma_ah_set_port_num(&qp_attr.ah_attr, lnk->ibport);
	if (lnk->lgr->smc_version == SMC_V2 && lnk->lgr->uses_gateway)
		hop_lim = IPV6_DEFAULT_HOPLIMIT;
	rdma_ah_set_grh(&qp_attr.ah_attr, NULL, 0, lnk->sgid_index, hop_lim, 0);
	rdma_ah_set_dgid_raw(&qp_attr.ah_attr, lnk->peer_gid);
	if (lnk->lgr->smc_version == SMC_V2 && lnk->lgr->uses_gateway)
		memcpy(&qp_attr.ah_attr.roce.dmac, lnk->lgr->nexthop_mac,
		       sizeof(lnk->lgr->nexthop_mac));
	else
		memcpy(&qp_attr.ah_attr.roce.dmac, lnk->peer_mac,
		       sizeof(lnk->peer_mac));
	qp_attr.dest_qp_num = lnk->peer_qpn;
	qp_attr.rq_psn = lnk->peer_psn; /* starting receive packet seq # */
	qp_attr.max_dest_rd_atomic = 1; /* max # of resources for incoming
					 * requests
					 */
	qp_attr.min_rnr_timer = SMC_QP_MIN_RNR_TIMER;

	return ib_modify_qp(lnk->roce_qp, &qp_attr, qp_attr_mask);
}

int smc_ib_modify_qp_rts(struct smc_link *lnk)
{
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RTS;
	qp_attr.timeout = SMC_QP_TIMEOUT;	/* local ack timeout */
	qp_attr.retry_cnt = SMC_QP_RETRY_CNT;	/* retry count */
	qp_attr.rnr_retry = SMC_QP_RNR_RETRY;	/* RNR retries, 7=infinite */
	qp_attr.sq_psn = lnk->psn_initial;	/* starting send packet seq # */
	qp_attr.max_rd_atomic = 1;	/* # of outstanding RDMA reads and
					 * atomic ops allowed
					 */
	return ib_modify_qp(lnk->roce_qp, &qp_attr,
			    IB_QP_STATE | IB_QP_TIMEOUT | IB_QP_RETRY_CNT |
			    IB_QP_SQ_PSN | IB_QP_RNR_RETRY |
			    IB_QP_MAX_QP_RD_ATOMIC);
}

int smc_ib_modify_qp_error(struct smc_link *lnk)
{
	struct ib_qp_attr qp_attr;

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_ERR;
	return ib_modify_qp(lnk->roce_qp, &qp_attr, IB_QP_STATE);
}

int smc_ib_ready_link(struct smc_link *lnk)
{
	struct smc_link_group *lgr = smc_get_lgr(lnk);
	int rc = 0;

	rc = smc_ib_modify_qp_init(lnk);
	if (rc)
		goto out;

	rc = smc_ib_modify_qp_rtr(lnk);
	if (rc)
		goto out;
	smc_wr_remember_qp_attr(lnk);
	rc = ib_req_notify_cq(lnk->smcibdev->roce_cq_recv,
			      IB_CQ_SOLICITED_MASK);
	if (rc)
		goto out;
	rc = smc_wr_rx_post_init(lnk);
	if (rc)
		goto out;
	smc_wr_remember_qp_attr(lnk);

	if (lgr->role == SMC_SERV) {
		rc = smc_ib_modify_qp_rts(lnk);
		if (rc)
			goto out;
		smc_wr_remember_qp_attr(lnk);
	}
out:
	return rc;
}

static int smc_ib_fill_mac(struct smc_ib_device *smcibdev, u8 ibport)
{
	const struct ib_gid_attr *attr;
	int rc;

	attr = rdma_get_gid_attr(smcibdev->ibdev, ibport, 0);
	if (IS_ERR(attr))
		return -ENODEV;

	rc = rdma_read_gid_l2_fields(attr, NULL, smcibdev->mac[ibport - 1]);
	rdma_put_gid_attr(attr);
	return rc;
}

/* Create an identifier unique for this instance of SMC-R.
 * The MAC-address of the first active registered IB device
 * plus a random 2-byte number is used to create this identifier.
 * This name is delivered to the peer during connection initialization.
 */
static inline void smc_ib_define_local_systemid(struct smc_ib_device *smcibdev,
						u8 ibport)
{
	memcpy(&local_systemid[2], &smcibdev->mac[ibport - 1],
	       sizeof(smcibdev->mac[ibport - 1]));
}

bool smc_ib_is_valid_local_systemid(void)
{
	return !is_zero_ether_addr(&local_systemid[2]);
}

static void smc_ib_init_local_systemid(void)
{
	get_random_bytes(&local_systemid[0], 2);
}

bool smc_ib_port_active(struct smc_ib_device *smcibdev, u8 ibport)
{
	return smcibdev->pattr[ibport - 1].state == IB_PORT_ACTIVE;
}

int smc_ib_find_route(__be32 saddr, __be32 daddr,
		      u8 nexthop_mac[], u8 *uses_gateway)
{
	struct neighbour *neigh = NULL;
	struct rtable *rt = NULL;
	struct flowi4 fl4 = {
		.saddr = saddr,
		.daddr = daddr
	};

	if (daddr == cpu_to_be32(INADDR_NONE))
		goto out;
	rt = ip_route_output_flow(&init_net, &fl4, NULL);
	if (IS_ERR(rt))
		goto out;
	if (rt->rt_uses_gateway && rt->rt_gw_family != AF_INET)
		goto out;
	neigh = rt->dst.ops->neigh_lookup(&rt->dst, NULL, &fl4.daddr);
	if (neigh) {
		memcpy(nexthop_mac, neigh->ha, ETH_ALEN);
		*uses_gateway = rt->rt_uses_gateway;
		return 0;
	}
out:
	return -ENOENT;
}

static int smc_ib_determine_gid_rcu(const struct net_device *ndev,
				    const struct ib_gid_attr *attr,
				    u8 gid[], u8 *sgid_index,
				    struct smc_init_info_smcrv2 *smcrv2)
{
	if (!smcrv2 && attr->gid_type == IB_GID_TYPE_ROCE) {
		if (gid)
			memcpy(gid, &attr->gid, SMC_GID_SIZE);
		if (sgid_index)
			*sgid_index = attr->index;
		return 0;
	}
	if (smcrv2 && attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP &&
	    smc_ib_gid_to_ipv4((u8 *)&attr->gid) != cpu_to_be32(INADDR_NONE)) {
		struct in_device *in_dev = __in_dev_get_rcu(ndev);
		const struct in_ifaddr *ifa;
		bool subnet_match = false;

		if (!in_dev)
			goto out;
		in_dev_for_each_ifa_rcu(ifa, in_dev) {
			if (!inet_ifa_match(smcrv2->saddr, ifa))
				continue;
			subnet_match = true;
			break;
		}
		if (!subnet_match)
			goto out;
		if (smcrv2->daddr && smc_ib_find_route(smcrv2->saddr,
						       smcrv2->daddr,
						       smcrv2->nexthop_mac,
						       &smcrv2->uses_gateway))
			goto out;

		if (gid)
			memcpy(gid, &attr->gid, SMC_GID_SIZE);
		if (sgid_index)
			*sgid_index = attr->index;
		return 0;
	}
out:
	return -ENODEV;
}

/* determine the gid for an ib-device port and vlan id */
int smc_ib_determine_gid(struct smc_ib_device *smcibdev, u8 ibport,
			 unsigned short vlan_id, u8 gid[], u8 *sgid_index,
			 struct smc_init_info_smcrv2 *smcrv2)
{
	const struct ib_gid_attr *attr;
	const struct net_device *ndev;
	int i;

	for (i = 0; i < smcibdev->pattr[ibport - 1].gid_tbl_len; i++) {
		attr = rdma_get_gid_attr(smcibdev->ibdev, ibport, i);
		if (IS_ERR(attr))
			continue;

		rcu_read_lock();
		ndev = rdma_read_gid_attr_ndev_rcu(attr);
		if (!IS_ERR(ndev) &&
		    ((!vlan_id && !is_vlan_dev(ndev)) ||
		     (vlan_id && is_vlan_dev(ndev) &&
		      vlan_dev_vlan_id(ndev) == vlan_id))) {
			if (!smc_ib_determine_gid_rcu(ndev, attr, gid,
						      sgid_index, smcrv2)) {
				rcu_read_unlock();
				rdma_put_gid_attr(attr);
				return 0;
			}
		}
		rcu_read_unlock();
		rdma_put_gid_attr(attr);
	}
	return -ENODEV;
}

/* check if gid is still defined on smcibdev */
static bool smc_ib_check_link_gid(u8 gid[SMC_GID_SIZE], bool smcrv2,
				  struct smc_ib_device *smcibdev, u8 ibport)
{
	const struct ib_gid_attr *attr;
	bool rc = false;
	int i;

	for (i = 0; !rc && i < smcibdev->pattr[ibport - 1].gid_tbl_len; i++) {
		attr = rdma_get_gid_attr(smcibdev->ibdev, ibport, i);
		if (IS_ERR(attr))
			continue;

		rcu_read_lock();
		if ((!smcrv2 && attr->gid_type == IB_GID_TYPE_ROCE) ||
		    (smcrv2 && attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP &&
		     !(ipv6_addr_type((const struct in6_addr *)&attr->gid)
				     & IPV6_ADDR_LINKLOCAL)))
			if (!memcmp(gid, &attr->gid, SMC_GID_SIZE))
				rc = true;
		rcu_read_unlock();
		rdma_put_gid_attr(attr);
	}
	return rc;
}

/* check all links if the gid is still defined on smcibdev */
static void smc_ib_gid_check(struct smc_ib_device *smcibdev, u8 ibport)
{
	struct smc_link_group *lgr;
	int i;

	spin_lock_bh(&smc_lgr_list.lock);
	list_for_each_entry(lgr, &smc_lgr_list.list, list) {
		if (strncmp(smcibdev->pnetid[ibport - 1], lgr->pnet_id,
			    SMC_MAX_PNETID_LEN))
			continue; /* lgr is not affected */
		if (list_empty(&lgr->list))
			continue;
		for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
			if (lgr->lnk[i].state == SMC_LNK_UNUSED ||
			    lgr->lnk[i].smcibdev != smcibdev)
				continue;
			if (!smc_ib_check_link_gid(lgr->lnk[i].gid,
						   lgr->smc_version == SMC_V2,
						   smcibdev, ibport))
				smcr_port_err(smcibdev, ibport);
		}
	}
	spin_unlock_bh(&smc_lgr_list.lock);
}

static int smc_ib_remember_port_attr(struct smc_ib_device *smcibdev, u8 ibport)
{
	int rc;

	memset(&smcibdev->pattr[ibport - 1], 0,
	       sizeof(smcibdev->pattr[ibport - 1]));
	rc = ib_query_port(smcibdev->ibdev, ibport,
			   &smcibdev->pattr[ibport - 1]);
	if (rc)
		goto out;
	/* the SMC protocol requires specification of the RoCE MAC address */
	rc = smc_ib_fill_mac(smcibdev, ibport);
	if (rc)
		goto out;
	if (!smc_ib_is_valid_local_systemid() &&
	    smc_ib_port_active(smcibdev, ibport))
		/* create unique system identifier */
		smc_ib_define_local_systemid(smcibdev, ibport);
out:
	return rc;
}

/* process context wrapper for might_sleep smc_ib_remember_port_attr */
static void smc_ib_port_event_work(struct work_struct *work)
{
	struct smc_ib_device *smcibdev = container_of(
		work, struct smc_ib_device, port_event_work);
	u8 port_idx;

	for_each_set_bit(port_idx, &smcibdev->port_event_mask, SMC_MAX_PORTS) {
		smc_ib_remember_port_attr(smcibdev, port_idx + 1);
		clear_bit(port_idx, &smcibdev->port_event_mask);
		if (!smc_ib_port_active(smcibdev, port_idx + 1)) {
			set_bit(port_idx, smcibdev->ports_going_away);
			smcr_port_err(smcibdev, port_idx + 1);
		} else {
			clear_bit(port_idx, smcibdev->ports_going_away);
			smcr_port_add(smcibdev, port_idx + 1);
			smc_ib_gid_check(smcibdev, port_idx + 1);
		}
	}
}

/* can be called in IRQ context */
static void smc_ib_global_event_handler(struct ib_event_handler *handler,
					struct ib_event *ibevent)
{
	struct smc_ib_device *smcibdev;
	bool schedule = false;
	u8 port_idx;

	smcibdev = container_of(handler, struct smc_ib_device, event_handler);

	switch (ibevent->event) {
	case IB_EVENT_DEVICE_FATAL:
		/* terminate all ports on device */
		for (port_idx = 0; port_idx < SMC_MAX_PORTS; port_idx++) {
			set_bit(port_idx, &smcibdev->port_event_mask);
			if (!test_and_set_bit(port_idx,
					      smcibdev->ports_going_away))
				schedule = true;
		}
		if (schedule)
			schedule_work(&smcibdev->port_event_work);
		break;
	case IB_EVENT_PORT_ACTIVE:
		port_idx = ibevent->element.port_num - 1;
		if (port_idx >= SMC_MAX_PORTS)
			break;
		set_bit(port_idx, &smcibdev->port_event_mask);
		if (test_and_clear_bit(port_idx, smcibdev->ports_going_away))
			schedule_work(&smcibdev->port_event_work);
		break;
	case IB_EVENT_PORT_ERR:
		port_idx = ibevent->element.port_num - 1;
		if (port_idx >= SMC_MAX_PORTS)
			break;
		set_bit(port_idx, &smcibdev->port_event_mask);
		if (!test_and_set_bit(port_idx, smcibdev->ports_going_away))
			schedule_work(&smcibdev->port_event_work);
		break;
	case IB_EVENT_GID_CHANGE:
		port_idx = ibevent->element.port_num - 1;
		if (port_idx >= SMC_MAX_PORTS)
			break;
		set_bit(port_idx, &smcibdev->port_event_mask);
		schedule_work(&smcibdev->port_event_work);
		break;
	default:
		break;
	}
}

void smc_ib_dealloc_protection_domain(struct smc_link *lnk)
{
	if (lnk->roce_pd)
		ib_dealloc_pd(lnk->roce_pd);
	lnk->roce_pd = NULL;
}

int smc_ib_create_protection_domain(struct smc_link *lnk)
{
	int rc;

	lnk->roce_pd = ib_alloc_pd(lnk->smcibdev->ibdev, 0);
	rc = PTR_ERR_OR_ZERO(lnk->roce_pd);
	if (IS_ERR(lnk->roce_pd))
		lnk->roce_pd = NULL;
	return rc;
}

static bool smcr_diag_is_dev_critical(struct smc_lgr_list *smc_lgr,
				      struct smc_ib_device *smcibdev)
{
	struct smc_link_group *lgr;
	bool rc = false;
	int i;

	spin_lock_bh(&smc_lgr->lock);
	list_for_each_entry(lgr, &smc_lgr->list, list) {
		if (lgr->is_smcd)
			continue;
		for (i = 0; i < SMC_LINKS_PER_LGR_MAX; i++) {
			if (lgr->lnk[i].state == SMC_LNK_UNUSED ||
			    lgr->lnk[i].smcibdev != smcibdev)
				continue;
			if (lgr->type == SMC_LGR_SINGLE ||
			    lgr->type == SMC_LGR_ASYMMETRIC_LOCAL) {
				rc = true;
				goto out;
			}
		}
	}
out:
	spin_unlock_bh(&smc_lgr->lock);
	return rc;
}

static int smc_nl_handle_dev_port(struct sk_buff *skb,
				  struct ib_device *ibdev,
				  struct smc_ib_device *smcibdev,
				  int port)
{
	char smc_pnet[SMC_MAX_PNETID_LEN + 1];
	struct nlattr *port_attrs;
	unsigned char port_state;
	int lnk_count = 0;

	port_attrs = nla_nest_start(skb, SMC_NLA_DEV_PORT + port);
	if (!port_attrs)
		goto errout;

	if (nla_put_u8(skb, SMC_NLA_DEV_PORT_PNET_USR,
		       smcibdev->pnetid_by_user[port]))
		goto errattr;
	memcpy(smc_pnet, &smcibdev->pnetid[port], SMC_MAX_PNETID_LEN);
	smc_pnet[SMC_MAX_PNETID_LEN] = 0;
	if (nla_put_string(skb, SMC_NLA_DEV_PORT_PNETID, smc_pnet))
		goto errattr;
	if (nla_put_u32(skb, SMC_NLA_DEV_PORT_NETDEV,
			smcibdev->ndev_ifidx[port]))
		goto errattr;
	if (nla_put_u8(skb, SMC_NLA_DEV_PORT_VALID, 1))
		goto errattr;
	port_state = smc_ib_port_active(smcibdev, port + 1);
	if (nla_put_u8(skb, SMC_NLA_DEV_PORT_STATE, port_state))
		goto errattr;
	lnk_count = atomic_read(&smcibdev->lnk_cnt_by_port[port]);
	if (nla_put_u32(skb, SMC_NLA_DEV_PORT_LNK_CNT, lnk_count))
		goto errattr;
	nla_nest_end(skb, port_attrs);
	return 0;
errattr:
	nla_nest_cancel(skb, port_attrs);
errout:
	return -EMSGSIZE;
}

static bool smc_nl_handle_pci_values(const struct smc_pci_dev *smc_pci_dev,
				     struct sk_buff *skb)
{
	if (nla_put_u32(skb, SMC_NLA_DEV_PCI_FID, smc_pci_dev->pci_fid))
		return false;
	if (nla_put_u16(skb, SMC_NLA_DEV_PCI_CHID, smc_pci_dev->pci_pchid))
		return false;
	if (nla_put_u16(skb, SMC_NLA_DEV_PCI_VENDOR, smc_pci_dev->pci_vendor))
		return false;
	if (nla_put_u16(skb, SMC_NLA_DEV_PCI_DEVICE, smc_pci_dev->pci_device))
		return false;
	if (nla_put_string(skb, SMC_NLA_DEV_PCI_ID, smc_pci_dev->pci_id))
		return false;
	return true;
}

static int smc_nl_handle_smcr_dev(struct smc_ib_device *smcibdev,
				  struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	char smc_ibname[IB_DEVICE_NAME_MAX];
	struct smc_pci_dev smc_pci_dev;
	struct pci_dev *pci_dev;
	unsigned char is_crit;
	struct nlattr *attrs;
	void *nlh;
	int i;

	nlh = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &smc_gen_nl_family, NLM_F_MULTI,
			  SMC_NETLINK_GET_DEV_SMCR);
	if (!nlh)
		goto errmsg;
	attrs = nla_nest_start(skb, SMC_GEN_DEV_SMCR);
	if (!attrs)
		goto errout;
	is_crit = smcr_diag_is_dev_critical(&smc_lgr_list, smcibdev);
	if (nla_put_u8(skb, SMC_NLA_DEV_IS_CRIT, is_crit))
		goto errattr;
	if (smcibdev->ibdev->dev.parent) {
		memset(&smc_pci_dev, 0, sizeof(smc_pci_dev));
		pci_dev = to_pci_dev(smcibdev->ibdev->dev.parent);
		smc_set_pci_values(pci_dev, &smc_pci_dev);
		if (!smc_nl_handle_pci_values(&smc_pci_dev, skb))
			goto errattr;
	}
	snprintf(smc_ibname, sizeof(smc_ibname), "%s", smcibdev->ibdev->name);
	if (nla_put_string(skb, SMC_NLA_DEV_IB_NAME, smc_ibname))
		goto errattr;
	for (i = 1; i <= SMC_MAX_PORTS; i++) {
		if (!rdma_is_port_valid(smcibdev->ibdev, i))
			continue;
		if (smc_nl_handle_dev_port(skb, smcibdev->ibdev,
					   smcibdev, i - 1))
			goto errattr;
	}

	nla_nest_end(skb, attrs);
	genlmsg_end(skb, nlh);
	return 0;

errattr:
	nla_nest_cancel(skb, attrs);
errout:
	genlmsg_cancel(skb, nlh);
errmsg:
	return -EMSGSIZE;
}

static void smc_nl_prep_smcr_dev(struct smc_ib_devices *dev_list,
				 struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct smc_nl_dmp_ctx *cb_ctx = smc_nl_dmp_ctx(cb);
	struct smc_ib_device *smcibdev;
	int snum = cb_ctx->pos[0];
	int num = 0;

	mutex_lock(&dev_list->mutex);
	list_for_each_entry(smcibdev, &dev_list->list, list) {
		if (num < snum)
			goto next;
		if (smc_nl_handle_smcr_dev(smcibdev, skb, cb))
			goto errout;
next:
		num++;
	}
errout:
	mutex_unlock(&dev_list->mutex);
	cb_ctx->pos[0] = num;
}

int smcr_nl_get_device(struct sk_buff *skb, struct netlink_callback *cb)
{
	smc_nl_prep_smcr_dev(&smc_ib_devices, skb, cb);
	return skb->len;
}

static void smc_ib_qp_event_handler(struct ib_event *ibevent, void *priv)
{
	struct smc_link *lnk = (struct smc_link *)priv;
	struct smc_ib_device *smcibdev = lnk->smcibdev;
	u8 port_idx;

	switch (ibevent->event) {
	case IB_EVENT_QP_FATAL:
	case IB_EVENT_QP_ACCESS_ERR:
		port_idx = ibevent->element.qp->port - 1;
		if (port_idx >= SMC_MAX_PORTS)
			break;
		set_bit(port_idx, &smcibdev->port_event_mask);
		if (!test_and_set_bit(port_idx, smcibdev->ports_going_away))
			schedule_work(&smcibdev->port_event_work);
		break;
	default:
		break;
	}
}

void smc_ib_destroy_queue_pair(struct smc_link *lnk)
{
	if (lnk->roce_qp)
		ib_destroy_qp(lnk->roce_qp);
	lnk->roce_qp = NULL;
}

/* create a queue pair within the protection domain for a link */
int smc_ib_create_queue_pair(struct smc_link *lnk)
{
	int sges_per_buf = (lnk->lgr->smc_version == SMC_V2) ? 2 : 1;
	struct ib_qp_init_attr qp_attr = {
		.event_handler = smc_ib_qp_event_handler,
		.qp_context = lnk,
		.send_cq = lnk->smcibdev->roce_cq_send,
		.recv_cq = lnk->smcibdev->roce_cq_recv,
		.srq = NULL,
		.cap = {
				/* include unsolicited rdma_writes as well,
				 * there are max. 2 RDMA_WRITE per 1 WR_SEND
				 */
			.max_send_wr = SMC_WR_BUF_CNT * 3,
			.max_recv_wr = SMC_WR_BUF_CNT * 3,
			.max_send_sge = SMC_IB_MAX_SEND_SGE,
			.max_recv_sge = sges_per_buf,
			.max_inline_data = 0,
		},
		.sq_sig_type = IB_SIGNAL_REQ_WR,
		.qp_type = IB_QPT_RC,
	};
	int rc;

	lnk->roce_qp = ib_create_qp(lnk->roce_pd, &qp_attr);
	rc = PTR_ERR_OR_ZERO(lnk->roce_qp);
	if (IS_ERR(lnk->roce_qp))
		lnk->roce_qp = NULL;
	else
		smc_wr_remember_qp_attr(lnk);
	return rc;
}

void smc_ib_put_memory_region(struct ib_mr *mr)
{
	ib_dereg_mr(mr);
}

static int smc_ib_map_mr_sg(struct smc_buf_desc *buf_slot, u8 link_idx)
{
	unsigned int offset = 0;
	int sg_num;

	/* map the largest prefix of a dma mapped SG list */
	sg_num = ib_map_mr_sg(buf_slot->mr_rx[link_idx],
			      buf_slot->sgt[link_idx].sgl,
			      buf_slot->sgt[link_idx].orig_nents,
			      &offset, PAGE_SIZE);

	return sg_num;
}

/* Allocate a memory region and map the dma mapped SG list of buf_slot */
int smc_ib_get_memory_region(struct ib_pd *pd, int access_flags,
			     struct smc_buf_desc *buf_slot, u8 link_idx)
{
	if (buf_slot->mr_rx[link_idx])
		return 0; /* already done */

	buf_slot->mr_rx[link_idx] =
		ib_alloc_mr(pd, IB_MR_TYPE_MEM_REG, 1 << buf_slot->order);
	if (IS_ERR(buf_slot->mr_rx[link_idx])) {
		int rc;

		rc = PTR_ERR(buf_slot->mr_rx[link_idx]);
		buf_slot->mr_rx[link_idx] = NULL;
		return rc;
	}

	if (smc_ib_map_mr_sg(buf_slot, link_idx) != 1)
		return -EINVAL;

	return 0;
}

/* synchronize buffer usage for cpu access */
void smc_ib_sync_sg_for_cpu(struct smc_link *lnk,
			    struct smc_buf_desc *buf_slot,
			    enum dma_data_direction data_direction)
{
	struct scatterlist *sg;
	unsigned int i;

	/* for now there is just one DMA address */
	for_each_sg(buf_slot->sgt[lnk->link_idx].sgl, sg,
		    buf_slot->sgt[lnk->link_idx].nents, i) {
		if (!sg_dma_len(sg))
			break;
		ib_dma_sync_single_for_cpu(lnk->smcibdev->ibdev,
					   sg_dma_address(sg),
					   sg_dma_len(sg),
					   data_direction);
	}
}

/* synchronize buffer usage for device access */
void smc_ib_sync_sg_for_device(struct smc_link *lnk,
			       struct smc_buf_desc *buf_slot,
			       enum dma_data_direction data_direction)
{
	struct scatterlist *sg;
	unsigned int i;

	/* for now there is just one DMA address */
	for_each_sg(buf_slot->sgt[lnk->link_idx].sgl, sg,
		    buf_slot->sgt[lnk->link_idx].nents, i) {
		if (!sg_dma_len(sg))
			break;
		ib_dma_sync_single_for_device(lnk->smcibdev->ibdev,
					      sg_dma_address(sg),
					      sg_dma_len(sg),
					      data_direction);
	}
}

/* Map a new TX or RX buffer SG-table to DMA */
int smc_ib_buf_map_sg(struct smc_link *lnk,
		      struct smc_buf_desc *buf_slot,
		      enum dma_data_direction data_direction)
{
	int mapped_nents;

	mapped_nents = ib_dma_map_sg(lnk->smcibdev->ibdev,
				     buf_slot->sgt[lnk->link_idx].sgl,
				     buf_slot->sgt[lnk->link_idx].orig_nents,
				     data_direction);
	if (!mapped_nents)
		return -ENOMEM;

	return mapped_nents;
}

void smc_ib_buf_unmap_sg(struct smc_link *lnk,
			 struct smc_buf_desc *buf_slot,
			 enum dma_data_direction data_direction)
{
	if (!buf_slot->sgt[lnk->link_idx].sgl->dma_address)
		return; /* already unmapped */

	ib_dma_unmap_sg(lnk->smcibdev->ibdev,
			buf_slot->sgt[lnk->link_idx].sgl,
			buf_slot->sgt[lnk->link_idx].orig_nents,
			data_direction);
	buf_slot->sgt[lnk->link_idx].sgl->dma_address = 0;
}

long smc_ib_setup_per_ibdev(struct smc_ib_device *smcibdev)
{
	struct ib_cq_init_attr cqattr =	{
		.cqe = SMC_MAX_CQE, .comp_vector = 0 };
	int cqe_size_order, smc_order;
	long rc;

	mutex_lock(&smcibdev->mutex);
	rc = 0;
	if (smcibdev->initialized)
		goto out;
	/* the calculated number of cq entries fits to mlx5 cq allocation */
	cqe_size_order = cache_line_size() == 128 ? 7 : 6;
	smc_order = MAX_ORDER - cqe_size_order - 1;
	if (SMC_MAX_CQE + 2 > (0x00000001 << smc_order) * PAGE_SIZE)
		cqattr.cqe = (0x00000001 << smc_order) * PAGE_SIZE - 2;
	smcibdev->roce_cq_send = ib_create_cq(smcibdev->ibdev,
					      smc_wr_tx_cq_handler, NULL,
					      smcibdev, &cqattr);
	rc = PTR_ERR_OR_ZERO(smcibdev->roce_cq_send);
	if (IS_ERR(smcibdev->roce_cq_send)) {
		smcibdev->roce_cq_send = NULL;
		goto out;
	}
	smcibdev->roce_cq_recv = ib_create_cq(smcibdev->ibdev,
					      smc_wr_rx_cq_handler, NULL,
					      smcibdev, &cqattr);
	rc = PTR_ERR_OR_ZERO(smcibdev->roce_cq_recv);
	if (IS_ERR(smcibdev->roce_cq_recv)) {
		smcibdev->roce_cq_recv = NULL;
		goto err;
	}
	smc_wr_add_dev(smcibdev);
	smcibdev->initialized = 1;
	goto out;

err:
	ib_destroy_cq(smcibdev->roce_cq_send);
out:
	mutex_unlock(&smcibdev->mutex);
	return rc;
}

static void smc_ib_cleanup_per_ibdev(struct smc_ib_device *smcibdev)
{
	mutex_lock(&smcibdev->mutex);
	if (!smcibdev->initialized)
		goto out;
	smcibdev->initialized = 0;
	ib_destroy_cq(smcibdev->roce_cq_recv);
	ib_destroy_cq(smcibdev->roce_cq_send);
	smc_wr_remove_dev(smcibdev);
out:
	mutex_unlock(&smcibdev->mutex);
}

static struct ib_client smc_ib_client;

static void smc_copy_netdev_ifindex(struct smc_ib_device *smcibdev, int port)
{
	struct ib_device *ibdev = smcibdev->ibdev;
	struct net_device *ndev;

	if (!ibdev->ops.get_netdev)
		return;
	ndev = ibdev->ops.get_netdev(ibdev, port + 1);
	if (ndev) {
		smcibdev->ndev_ifidx[port] = ndev->ifindex;
		dev_put(ndev);
	}
}

void smc_ib_ndev_change(struct net_device *ndev, unsigned long event)
{
	struct smc_ib_device *smcibdev;
	struct ib_device *libdev;
	struct net_device *lndev;
	u8 port_cnt;
	int i;

	mutex_lock(&smc_ib_devices.mutex);
	list_for_each_entry(smcibdev, &smc_ib_devices.list, list) {
		port_cnt = smcibdev->ibdev->phys_port_cnt;
		for (i = 0; i < min_t(size_t, port_cnt, SMC_MAX_PORTS); i++) {
			libdev = smcibdev->ibdev;
			if (!libdev->ops.get_netdev)
				continue;
			lndev = libdev->ops.get_netdev(libdev, i + 1);
			dev_put(lndev);
			if (lndev != ndev)
				continue;
			if (event == NETDEV_REGISTER)
				smcibdev->ndev_ifidx[i] = ndev->ifindex;
			if (event == NETDEV_UNREGISTER)
				smcibdev->ndev_ifidx[i] = 0;
		}
	}
	mutex_unlock(&smc_ib_devices.mutex);
}

/* callback function for ib_register_client() */
static int smc_ib_add_dev(struct ib_device *ibdev)
{
	struct smc_ib_device *smcibdev;
	u8 port_cnt;
	int i;

	if (ibdev->node_type != RDMA_NODE_IB_CA)
		return -EOPNOTSUPP;

	smcibdev = kzalloc(sizeof(*smcibdev), GFP_KERNEL);
	if (!smcibdev)
		return -ENOMEM;

	smcibdev->ibdev = ibdev;
	INIT_WORK(&smcibdev->port_event_work, smc_ib_port_event_work);
	atomic_set(&smcibdev->lnk_cnt, 0);
	init_waitqueue_head(&smcibdev->lnks_deleted);
	mutex_init(&smcibdev->mutex);
	mutex_lock(&smc_ib_devices.mutex);
	list_add_tail(&smcibdev->list, &smc_ib_devices.list);
	mutex_unlock(&smc_ib_devices.mutex);
	ib_set_client_data(ibdev, &smc_ib_client, smcibdev);
	INIT_IB_EVENT_HANDLER(&smcibdev->event_handler, smcibdev->ibdev,
			      smc_ib_global_event_handler);
	ib_register_event_handler(&smcibdev->event_handler);

	/* trigger reading of the port attributes */
	port_cnt = smcibdev->ibdev->phys_port_cnt;
	pr_warn_ratelimited("smc: adding ib device %s with port count %d\n",
			    smcibdev->ibdev->name, port_cnt);
	for (i = 0;
	     i < min_t(size_t, port_cnt, SMC_MAX_PORTS);
	     i++) {
		set_bit(i, &smcibdev->port_event_mask);
		/* determine pnetids of the port */
		if (smc_pnetid_by_dev_port(ibdev->dev.parent, i,
					   smcibdev->pnetid[i]))
			smc_pnetid_by_table_ib(smcibdev, i + 1);
		smc_copy_netdev_ifindex(smcibdev, i);
		pr_warn_ratelimited("smc:    ib device %s port %d has pnetid "
				    "%.16s%s\n",
				    smcibdev->ibdev->name, i + 1,
				    smcibdev->pnetid[i],
				    smcibdev->pnetid_by_user[i] ?
				     " (user defined)" :
				     "");
	}
	schedule_work(&smcibdev->port_event_work);
	return 0;
}

/* callback function for ib_unregister_client() */
static void smc_ib_remove_dev(struct ib_device *ibdev, void *client_data)
{
	struct smc_ib_device *smcibdev = client_data;

	mutex_lock(&smc_ib_devices.mutex);
	list_del_init(&smcibdev->list); /* remove from smc_ib_devices */
	mutex_unlock(&smc_ib_devices.mutex);
	pr_warn_ratelimited("smc: removing ib device %s\n",
			    smcibdev->ibdev->name);
	smc_smcr_terminate_all(smcibdev);
	smc_ib_cleanup_per_ibdev(smcibdev);
	ib_unregister_event_handler(&smcibdev->event_handler);
	cancel_work_sync(&smcibdev->port_event_work);
	kfree(smcibdev);
}

static struct ib_client smc_ib_client = {
	.name	= "smc_ib",
	.add	= smc_ib_add_dev,
	.remove = smc_ib_remove_dev,
};

int __init smc_ib_register_client(void)
{
	smc_ib_init_local_systemid();
	return ib_register_client(&smc_ib_client);
}

void smc_ib_unregister_client(void)
{
	ib_unregister_client(&smc_ib_client);
}
