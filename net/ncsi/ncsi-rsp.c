/*
 * Copyright Gavin Shan, IBM Corporation 2016.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <net/ncsi.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "internal.h"
#include "ncsi-pkt.h"

static int ncsi_validate_rsp_pkt(struct ncsi_request *nr,
				 unsigned short payload)
{
	struct ncsi_rsp_pkt_hdr *h;
	u32 checksum;
	__be32 *pchecksum;

	/* Check NCSI packet header. We don't need validate
	 * the packet type, which should have been checked
	 * before calling this function.
	 */
	h = (struct ncsi_rsp_pkt_hdr *)skb_network_header(nr->rsp);
	if (h->common.revision != NCSI_PKT_REVISION)
		return -EINVAL;
	if (ntohs(h->common.length) != payload)
		return -EINVAL;

	/* Check on code and reason */
	if (ntohs(h->code) != NCSI_PKT_RSP_C_COMPLETED ||
	    ntohs(h->reason) != NCSI_PKT_RSP_R_NO_ERROR)
		return -EINVAL;

	/* Validate checksum, which might be zeroes if the
	 * sender doesn't support checksum according to NCSI
	 * specification.
	 */
	pchecksum = (__be32 *)((void *)(h + 1) + payload - 4);
	if (ntohl(*pchecksum) == 0)
		return 0;

	checksum = ncsi_calculate_checksum((unsigned char *)h,
					   sizeof(*h) + payload - 4);
	if (*pchecksum != htonl(checksum))
		return -EINVAL;

	return 0;
}

static int ncsi_rsp_handler_cis(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	unsigned char id;

	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel, &np, &nc);
	if (!nc) {
		if (ndp->flags & NCSI_DEV_PROBED)
			return -ENXIO;

		id = NCSI_CHANNEL_INDEX(rsp->rsp.common.channel);
		nc = ncsi_add_channel(np, id);
	}

	return nc ? 0 : -ENODEV;
}

static int ncsi_rsp_handler_sp(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_package *np;
	unsigned char id;

	/* Add the package if it's not existing. Otherwise,
	 * to change the state of its child channels.
	 */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      &np, NULL);
	if (!np) {
		if (ndp->flags & NCSI_DEV_PROBED)
			return -ENXIO;

		id = NCSI_PACKAGE_INDEX(rsp->rsp.common.channel);
		np = ncsi_add_package(ndp, id);
		if (!np)
			return -ENODEV;
	}

	return 0;
}

static int ncsi_rsp_handler_dp(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_package *np;
	struct ncsi_channel *nc;
	unsigned long flags;

	/* Find the package */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      &np, NULL);
	if (!np)
		return -ENODEV;

	/* Change state of all channels attached to the package */
	NCSI_FOR_EACH_CHANNEL(np, nc) {
		spin_lock_irqsave(&nc->lock, flags);
		nc->state = NCSI_CHANNEL_INACTIVE;
		spin_unlock_irqrestore(&nc->lock, flags);
	}

	return 0;
}

static int ncsi_rsp_handler_ec(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	ncm = &nc->modes[NCSI_MODE_ENABLE];
	if (ncm->enable)
		return 0;

	ncm->enable = 1;
	return 0;
}

static int ncsi_rsp_handler_dc(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;
	int ret;

	ret = ncsi_validate_rsp_pkt(nr, 4);
	if (ret)
		return ret;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	ncm = &nc->modes[NCSI_MODE_ENABLE];
	if (!ncm->enable)
		return 0;

	ncm->enable = 0;
	return 0;
}

static int ncsi_rsp_handler_rc(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	unsigned long flags;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update state for the specified channel */
	spin_lock_irqsave(&nc->lock, flags);
	nc->state = NCSI_CHANNEL_INACTIVE;
	spin_unlock_irqrestore(&nc->lock, flags);

	return 0;
}

static int ncsi_rsp_handler_ecnt(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	ncm = &nc->modes[NCSI_MODE_TX_ENABLE];
	if (ncm->enable)
		return 0;

	ncm->enable = 1;
	return 0;
}

static int ncsi_rsp_handler_dcnt(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	ncm = &nc->modes[NCSI_MODE_TX_ENABLE];
	if (!ncm->enable)
		return 0;

	ncm->enable = 1;
	return 0;
}

static int ncsi_rsp_handler_ae(struct ncsi_request *nr)
{
	struct ncsi_cmd_ae_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if the AEN has been enabled */
	ncm = &nc->modes[NCSI_MODE_AEN];
	if (ncm->enable)
		return 0;

	/* Update to AEN configuration */
	cmd = (struct ncsi_cmd_ae_pkt *)skb_network_header(nr->cmd);
	ncm->enable = 1;
	ncm->data[0] = cmd->mc_id;
	ncm->data[1] = ntohl(cmd->mode);

	return 0;
}

static int ncsi_rsp_handler_sl(struct ncsi_request *nr)
{
	struct ncsi_cmd_sl_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	cmd = (struct ncsi_cmd_sl_pkt *)skb_network_header(nr->cmd);
	ncm = &nc->modes[NCSI_MODE_LINK];
	ncm->data[0] = ntohl(cmd->mode);
	ncm->data[1] = ntohl(cmd->oem_mode);

	return 0;
}

static int ncsi_rsp_handler_gls(struct ncsi_request *nr)
{
	struct ncsi_rsp_gls_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;
	unsigned long flags;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_gls_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	ncm = &nc->modes[NCSI_MODE_LINK];
	ncm->data[2] = ntohl(rsp->status);
	ncm->data[3] = ntohl(rsp->other);
	ncm->data[4] = ntohl(rsp->oem_status);

	if (nr->flags & NCSI_REQ_FLAG_EVENT_DRIVEN)
		return 0;

	/* Reset the channel monitor if it has been enabled */
	spin_lock_irqsave(&nc->lock, flags);
	nc->monitor.state = NCSI_CHANNEL_MONITOR_START;
	spin_unlock_irqrestore(&nc->lock, flags);

	return 0;
}

static int ncsi_rsp_handler_svf(struct ncsi_request *nr)
{
	struct ncsi_cmd_svf_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_filter *ncf;
	unsigned short vlan;
	int ret;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	cmd = (struct ncsi_cmd_svf_pkt *)skb_network_header(nr->cmd);
	ncf = nc->filters[NCSI_FILTER_VLAN];
	if (!ncf)
		return -ENOENT;
	if (cmd->index >= ncf->total)
		return -ERANGE;

	/* Add or remove the VLAN filter */
	if (!(cmd->enable & 0x1)) {
		/* HW indexes from 1 */
		ret = ncsi_remove_filter(nc, NCSI_FILTER_VLAN, cmd->index - 1);
	} else {
		vlan = ntohs(cmd->vlan);
		ret = ncsi_add_filter(nc, NCSI_FILTER_VLAN, &vlan);
	}

	return ret;
}

static int ncsi_rsp_handler_ev(struct ncsi_request *nr)
{
	struct ncsi_cmd_ev_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if VLAN mode has been enabled */
	ncm = &nc->modes[NCSI_MODE_VLAN];
	if (ncm->enable)
		return 0;

	/* Update to VLAN mode */
	cmd = (struct ncsi_cmd_ev_pkt *)skb_network_header(nr->cmd);
	ncm->enable = 1;
	ncm->data[0] = ntohl(cmd->mode);

	return 0;
}

static int ncsi_rsp_handler_dv(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if VLAN mode has been enabled */
	ncm = &nc->modes[NCSI_MODE_VLAN];
	if (!ncm->enable)
		return 0;

	/* Update to VLAN mode */
	ncm->enable = 0;
	return 0;
}

static int ncsi_rsp_handler_sma(struct ncsi_request *nr)
{
	struct ncsi_cmd_sma_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_filter *ncf;
	void *bitmap;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* According to NCSI spec 1.01, the mixed filter table
	 * isn't supported yet.
	 */
	cmd = (struct ncsi_cmd_sma_pkt *)skb_network_header(nr->cmd);
	switch (cmd->at_e >> 5) {
	case 0x0:	/* UC address */
		ncf = nc->filters[NCSI_FILTER_UC];
		break;
	case 0x1:	/* MC address */
		ncf = nc->filters[NCSI_FILTER_MC];
		break;
	default:
		return -EINVAL;
	}

	/* Sanity check on the filter */
	if (!ncf)
		return -ENOENT;
	else if (cmd->index >= ncf->total)
		return -ERANGE;

	bitmap = &ncf->bitmap;
	if (cmd->at_e & 0x1) {
		set_bit(cmd->index, bitmap);
		memcpy(ncf->data + 6 * cmd->index, cmd->mac, 6);
	} else {
		clear_bit(cmd->index, bitmap);
		memset(ncf->data + 6 * cmd->index, 0, 6);
	}

	return 0;
}

static int ncsi_rsp_handler_ebf(struct ncsi_request *nr)
{
	struct ncsi_cmd_ebf_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the package and channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel, NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if broadcast filter has been enabled */
	ncm = &nc->modes[NCSI_MODE_BC];
	if (ncm->enable)
		return 0;

	/* Update to broadcast filter mode */
	cmd = (struct ncsi_cmd_ebf_pkt *)skb_network_header(nr->cmd);
	ncm->enable = 1;
	ncm->data[0] = ntohl(cmd->mode);

	return 0;
}

static int ncsi_rsp_handler_dbf(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if broadcast filter isn't enabled */
	ncm = &nc->modes[NCSI_MODE_BC];
	if (!ncm->enable)
		return 0;

	/* Update to broadcast filter mode */
	ncm->enable = 0;
	ncm->data[0] = 0;

	return 0;
}

static int ncsi_rsp_handler_egmf(struct ncsi_request *nr)
{
	struct ncsi_cmd_egmf_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if multicast filter has been enabled */
	ncm = &nc->modes[NCSI_MODE_MC];
	if (ncm->enable)
		return 0;

	/* Update to multicast filter mode */
	cmd = (struct ncsi_cmd_egmf_pkt *)skb_network_header(nr->cmd);
	ncm->enable = 1;
	ncm->data[0] = ntohl(cmd->mode);

	return 0;
}

static int ncsi_rsp_handler_dgmf(struct ncsi_request *nr)
{
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if multicast filter has been enabled */
	ncm = &nc->modes[NCSI_MODE_MC];
	if (!ncm->enable)
		return 0;

	/* Update to multicast filter mode */
	ncm->enable = 0;
	ncm->data[0] = 0;

	return 0;
}

static int ncsi_rsp_handler_snfc(struct ncsi_request *nr)
{
	struct ncsi_cmd_snfc_pkt *cmd;
	struct ncsi_rsp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_mode *ncm;

	/* Find the channel */
	rsp = (struct ncsi_rsp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Check if flow control has been enabled */
	ncm = &nc->modes[NCSI_MODE_FC];
	if (ncm->enable)
		return 0;

	/* Update to flow control mode */
	cmd = (struct ncsi_cmd_snfc_pkt *)skb_network_header(nr->cmd);
	ncm->enable = 1;
	ncm->data[0] = cmd->mode;

	return 0;
}

static int ncsi_rsp_handler_gvi(struct ncsi_request *nr)
{
	struct ncsi_rsp_gvi_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_version *ncv;
	int i;

	/* Find the channel */
	rsp = (struct ncsi_rsp_gvi_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update to channel's version info */
	ncv = &nc->version;
	ncv->version = ntohl(rsp->ncsi_version);
	ncv->alpha2 = rsp->alpha2;
	memcpy(ncv->fw_name, rsp->fw_name, 12);
	ncv->fw_version = ntohl(rsp->fw_version);
	for (i = 0; i < ARRAY_SIZE(ncv->pci_ids); i++)
		ncv->pci_ids[i] = ntohs(rsp->pci_ids[i]);
	ncv->mf_id = ntohl(rsp->mf_id);

	return 0;
}

static int ncsi_rsp_handler_gc(struct ncsi_request *nr)
{
	struct ncsi_rsp_gc_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_filter *ncf;
	size_t size, entry_size;
	int cnt, i;

	/* Find the channel */
	rsp = (struct ncsi_rsp_gc_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update channel's capabilities */
	nc->caps[NCSI_CAP_GENERIC].cap = ntohl(rsp->cap) &
					 NCSI_CAP_GENERIC_MASK;
	nc->caps[NCSI_CAP_BC].cap = ntohl(rsp->bc_cap) &
				    NCSI_CAP_BC_MASK;
	nc->caps[NCSI_CAP_MC].cap = ntohl(rsp->mc_cap) &
				    NCSI_CAP_MC_MASK;
	nc->caps[NCSI_CAP_BUFFER].cap = ntohl(rsp->buf_cap);
	nc->caps[NCSI_CAP_AEN].cap = ntohl(rsp->aen_cap) &
				     NCSI_CAP_AEN_MASK;
	nc->caps[NCSI_CAP_VLAN].cap = rsp->vlan_mode &
				      NCSI_CAP_VLAN_MASK;

	/* Build filters */
	for (i = 0; i < NCSI_FILTER_MAX; i++) {
		switch (i) {
		case NCSI_FILTER_VLAN:
			cnt = rsp->vlan_cnt;
			entry_size = 2;
			break;
		case NCSI_FILTER_MIXED:
			cnt = rsp->mixed_cnt;
			entry_size = 6;
			break;
		case NCSI_FILTER_MC:
			cnt = rsp->mc_cnt;
			entry_size = 6;
			break;
		case NCSI_FILTER_UC:
			cnt = rsp->uc_cnt;
			entry_size = 6;
			break;
		default:
			continue;
		}

		if (!cnt || nc->filters[i])
			continue;

		size = sizeof(*ncf) + cnt * entry_size;
		ncf = kzalloc(size, GFP_ATOMIC);
		if (!ncf) {
			pr_warn("%s: Cannot alloc filter table (%d)\n",
				__func__, i);
			return -ENOMEM;
		}

		ncf->index = i;
		ncf->total = cnt;
		if (i == NCSI_FILTER_VLAN) {
			/* Set VLAN filters active so they are cleared in
			 * first configuration state
			 */
			ncf->bitmap = U64_MAX;
		} else {
			ncf->bitmap = 0x0ul;
		}
		nc->filters[i] = ncf;
	}

	return 0;
}

static int ncsi_rsp_handler_gp(struct ncsi_request *nr)
{
	struct ncsi_rsp_gp_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	unsigned short enable, vlan;
	unsigned char *pdata;
	int table, i;

	/* Find the channel */
	rsp = (struct ncsi_rsp_gp_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Modes with explicit enabled indications */
	if (ntohl(rsp->valid_modes) & 0x1) {	/* BC filter mode */
		nc->modes[NCSI_MODE_BC].enable = 1;
		nc->modes[NCSI_MODE_BC].data[0] = ntohl(rsp->bc_mode);
	}
	if (ntohl(rsp->valid_modes) & 0x2)	/* Channel enabled */
		nc->modes[NCSI_MODE_ENABLE].enable = 1;
	if (ntohl(rsp->valid_modes) & 0x4)	/* Channel Tx enabled */
		nc->modes[NCSI_MODE_TX_ENABLE].enable = 1;
	if (ntohl(rsp->valid_modes) & 0x8)	/* MC filter mode */
		nc->modes[NCSI_MODE_MC].enable = 1;

	/* Modes without explicit enabled indications */
	nc->modes[NCSI_MODE_LINK].enable = 1;
	nc->modes[NCSI_MODE_LINK].data[0] = ntohl(rsp->link_mode);
	nc->modes[NCSI_MODE_VLAN].enable = 1;
	nc->modes[NCSI_MODE_VLAN].data[0] = rsp->vlan_mode;
	nc->modes[NCSI_MODE_FC].enable = 1;
	nc->modes[NCSI_MODE_FC].data[0] = rsp->fc_mode;
	nc->modes[NCSI_MODE_AEN].enable = 1;
	nc->modes[NCSI_MODE_AEN].data[0] = ntohl(rsp->aen_mode);

	/* MAC addresses filter table */
	pdata = (unsigned char *)rsp + 48;
	enable = rsp->mac_enable;
	for (i = 0; i < rsp->mac_cnt; i++, pdata += 6) {
		if (i >= (nc->filters[NCSI_FILTER_UC]->total +
			  nc->filters[NCSI_FILTER_MC]->total))
			table = NCSI_FILTER_MIXED;
		else if (i >= nc->filters[NCSI_FILTER_UC]->total)
			table = NCSI_FILTER_MC;
		else
			table = NCSI_FILTER_UC;

		if (!(enable & (0x1 << i)))
			continue;

		if (ncsi_find_filter(nc, table, pdata) >= 0)
			continue;

		ncsi_add_filter(nc, table, pdata);
	}

	/* VLAN filter table */
	enable = ntohs(rsp->vlan_enable);
	for (i = 0; i < rsp->vlan_cnt; i++, pdata += 2) {
		if (!(enable & (0x1 << i)))
			continue;

		vlan = ntohs(*(__be16 *)pdata);
		if (ncsi_find_filter(nc, NCSI_FILTER_VLAN, &vlan) >= 0)
			continue;

		ncsi_add_filter(nc, NCSI_FILTER_VLAN, &vlan);
	}

	return 0;
}

static int ncsi_rsp_handler_gcps(struct ncsi_request *nr)
{
	struct ncsi_rsp_gcps_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_stats *ncs;

	/* Find the channel */
	rsp = (struct ncsi_rsp_gcps_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update HNC's statistics */
	ncs = &nc->stats;
	ncs->hnc_cnt_hi         = ntohl(rsp->cnt_hi);
	ncs->hnc_cnt_lo         = ntohl(rsp->cnt_lo);
	ncs->hnc_rx_bytes       = ntohl(rsp->rx_bytes);
	ncs->hnc_tx_bytes       = ntohl(rsp->tx_bytes);
	ncs->hnc_rx_uc_pkts     = ntohl(rsp->rx_uc_pkts);
	ncs->hnc_rx_mc_pkts     = ntohl(rsp->rx_mc_pkts);
	ncs->hnc_rx_bc_pkts     = ntohl(rsp->rx_bc_pkts);
	ncs->hnc_tx_uc_pkts     = ntohl(rsp->tx_uc_pkts);
	ncs->hnc_tx_mc_pkts     = ntohl(rsp->tx_mc_pkts);
	ncs->hnc_tx_bc_pkts     = ntohl(rsp->tx_bc_pkts);
	ncs->hnc_fcs_err        = ntohl(rsp->fcs_err);
	ncs->hnc_align_err      = ntohl(rsp->align_err);
	ncs->hnc_false_carrier  = ntohl(rsp->false_carrier);
	ncs->hnc_runt_pkts      = ntohl(rsp->runt_pkts);
	ncs->hnc_jabber_pkts    = ntohl(rsp->jabber_pkts);
	ncs->hnc_rx_pause_xon   = ntohl(rsp->rx_pause_xon);
	ncs->hnc_rx_pause_xoff  = ntohl(rsp->rx_pause_xoff);
	ncs->hnc_tx_pause_xon   = ntohl(rsp->tx_pause_xon);
	ncs->hnc_tx_pause_xoff  = ntohl(rsp->tx_pause_xoff);
	ncs->hnc_tx_s_collision = ntohl(rsp->tx_s_collision);
	ncs->hnc_tx_m_collision = ntohl(rsp->tx_m_collision);
	ncs->hnc_l_collision    = ntohl(rsp->l_collision);
	ncs->hnc_e_collision    = ntohl(rsp->e_collision);
	ncs->hnc_rx_ctl_frames  = ntohl(rsp->rx_ctl_frames);
	ncs->hnc_rx_64_frames   = ntohl(rsp->rx_64_frames);
	ncs->hnc_rx_127_frames  = ntohl(rsp->rx_127_frames);
	ncs->hnc_rx_255_frames  = ntohl(rsp->rx_255_frames);
	ncs->hnc_rx_511_frames  = ntohl(rsp->rx_511_frames);
	ncs->hnc_rx_1023_frames = ntohl(rsp->rx_1023_frames);
	ncs->hnc_rx_1522_frames = ntohl(rsp->rx_1522_frames);
	ncs->hnc_rx_9022_frames = ntohl(rsp->rx_9022_frames);
	ncs->hnc_tx_64_frames   = ntohl(rsp->tx_64_frames);
	ncs->hnc_tx_127_frames  = ntohl(rsp->tx_127_frames);
	ncs->hnc_tx_255_frames  = ntohl(rsp->tx_255_frames);
	ncs->hnc_tx_511_frames  = ntohl(rsp->tx_511_frames);
	ncs->hnc_tx_1023_frames = ntohl(rsp->tx_1023_frames);
	ncs->hnc_tx_1522_frames = ntohl(rsp->tx_1522_frames);
	ncs->hnc_tx_9022_frames = ntohl(rsp->tx_9022_frames);
	ncs->hnc_rx_valid_bytes = ntohl(rsp->rx_valid_bytes);
	ncs->hnc_rx_runt_pkts   = ntohl(rsp->rx_runt_pkts);
	ncs->hnc_rx_jabber_pkts = ntohl(rsp->rx_jabber_pkts);

	return 0;
}

static int ncsi_rsp_handler_gns(struct ncsi_request *nr)
{
	struct ncsi_rsp_gns_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_stats *ncs;

	/* Find the channel */
	rsp = (struct ncsi_rsp_gns_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update HNC's statistics */
	ncs = &nc->stats;
	ncs->ncsi_rx_cmds       = ntohl(rsp->rx_cmds);
	ncs->ncsi_dropped_cmds  = ntohl(rsp->dropped_cmds);
	ncs->ncsi_cmd_type_errs = ntohl(rsp->cmd_type_errs);
	ncs->ncsi_cmd_csum_errs = ntohl(rsp->cmd_csum_errs);
	ncs->ncsi_rx_pkts       = ntohl(rsp->rx_pkts);
	ncs->ncsi_tx_pkts       = ntohl(rsp->tx_pkts);
	ncs->ncsi_tx_aen_pkts   = ntohl(rsp->tx_aen_pkts);

	return 0;
}

static int ncsi_rsp_handler_gnpts(struct ncsi_request *nr)
{
	struct ncsi_rsp_gnpts_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_channel *nc;
	struct ncsi_channel_stats *ncs;

	/* Find the channel */
	rsp = (struct ncsi_rsp_gnpts_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      NULL, &nc);
	if (!nc)
		return -ENODEV;

	/* Update HNC's statistics */
	ncs = &nc->stats;
	ncs->pt_tx_pkts        = ntohl(rsp->tx_pkts);
	ncs->pt_tx_dropped     = ntohl(rsp->tx_dropped);
	ncs->pt_tx_channel_err = ntohl(rsp->tx_channel_err);
	ncs->pt_tx_us_err      = ntohl(rsp->tx_us_err);
	ncs->pt_rx_pkts        = ntohl(rsp->rx_pkts);
	ncs->pt_rx_dropped     = ntohl(rsp->rx_dropped);
	ncs->pt_rx_channel_err = ntohl(rsp->rx_channel_err);
	ncs->pt_rx_us_err      = ntohl(rsp->rx_us_err);
	ncs->pt_rx_os_err      = ntohl(rsp->rx_os_err);

	return 0;
}

static int ncsi_rsp_handler_gps(struct ncsi_request *nr)
{
	struct ncsi_rsp_gps_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_package *np;

	/* Find the package */
	rsp = (struct ncsi_rsp_gps_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      &np, NULL);
	if (!np)
		return -ENODEV;

	return 0;
}

static int ncsi_rsp_handler_gpuuid(struct ncsi_request *nr)
{
	struct ncsi_rsp_gpuuid_pkt *rsp;
	struct ncsi_dev_priv *ndp = nr->ndp;
	struct ncsi_package *np;

	/* Find the package */
	rsp = (struct ncsi_rsp_gpuuid_pkt *)skb_network_header(nr->rsp);
	ncsi_find_package_and_channel(ndp, rsp->rsp.common.channel,
				      &np, NULL);
	if (!np)
		return -ENODEV;

	memcpy(np->uuid, rsp->uuid, sizeof(rsp->uuid));

	return 0;
}

static struct ncsi_rsp_handler {
	unsigned char	type;
	int             payload;
	int		(*handler)(struct ncsi_request *nr);
} ncsi_rsp_handlers[] = {
	{ NCSI_PKT_RSP_CIS,     4, ncsi_rsp_handler_cis     },
	{ NCSI_PKT_RSP_SP,      4, ncsi_rsp_handler_sp      },
	{ NCSI_PKT_RSP_DP,      4, ncsi_rsp_handler_dp      },
	{ NCSI_PKT_RSP_EC,      4, ncsi_rsp_handler_ec      },
	{ NCSI_PKT_RSP_DC,      4, ncsi_rsp_handler_dc      },
	{ NCSI_PKT_RSP_RC,      4, ncsi_rsp_handler_rc      },
	{ NCSI_PKT_RSP_ECNT,    4, ncsi_rsp_handler_ecnt    },
	{ NCSI_PKT_RSP_DCNT,    4, ncsi_rsp_handler_dcnt    },
	{ NCSI_PKT_RSP_AE,      4, ncsi_rsp_handler_ae      },
	{ NCSI_PKT_RSP_SL,      4, ncsi_rsp_handler_sl      },
	{ NCSI_PKT_RSP_GLS,    16, ncsi_rsp_handler_gls     },
	{ NCSI_PKT_RSP_SVF,     4, ncsi_rsp_handler_svf     },
	{ NCSI_PKT_RSP_EV,      4, ncsi_rsp_handler_ev      },
	{ NCSI_PKT_RSP_DV,      4, ncsi_rsp_handler_dv      },
	{ NCSI_PKT_RSP_SMA,     4, ncsi_rsp_handler_sma     },
	{ NCSI_PKT_RSP_EBF,     4, ncsi_rsp_handler_ebf     },
	{ NCSI_PKT_RSP_DBF,     4, ncsi_rsp_handler_dbf     },
	{ NCSI_PKT_RSP_EGMF,    4, ncsi_rsp_handler_egmf    },
	{ NCSI_PKT_RSP_DGMF,    4, ncsi_rsp_handler_dgmf    },
	{ NCSI_PKT_RSP_SNFC,    4, ncsi_rsp_handler_snfc    },
	{ NCSI_PKT_RSP_GVI,    40, ncsi_rsp_handler_gvi     },
	{ NCSI_PKT_RSP_GC,     32, ncsi_rsp_handler_gc      },
	{ NCSI_PKT_RSP_GP,     -1, ncsi_rsp_handler_gp      },
	{ NCSI_PKT_RSP_GCPS,  172, ncsi_rsp_handler_gcps    },
	{ NCSI_PKT_RSP_GNS,   172, ncsi_rsp_handler_gns     },
	{ NCSI_PKT_RSP_GNPTS, 172, ncsi_rsp_handler_gnpts   },
	{ NCSI_PKT_RSP_GPS,     8, ncsi_rsp_handler_gps     },
	{ NCSI_PKT_RSP_OEM,     0, NULL                     },
	{ NCSI_PKT_RSP_PLDM,    0, NULL                     },
	{ NCSI_PKT_RSP_GPUUID, 20, ncsi_rsp_handler_gpuuid  }
};

int ncsi_rcv_rsp(struct sk_buff *skb, struct net_device *dev,
		 struct packet_type *pt, struct net_device *orig_dev)
{
	struct ncsi_rsp_handler *nrh = NULL;
	struct ncsi_dev *nd;
	struct ncsi_dev_priv *ndp;
	struct ncsi_request *nr;
	struct ncsi_pkt_hdr *hdr;
	unsigned long flags;
	int payload, i, ret;

	/* Find the NCSI device */
	nd = ncsi_find_dev(dev);
	ndp = nd ? TO_NCSI_DEV_PRIV(nd) : NULL;
	if (!ndp)
		return -ENODEV;

	/* Check if it is AEN packet */
	hdr = (struct ncsi_pkt_hdr *)skb_network_header(skb);
	if (hdr->type == NCSI_PKT_AEN)
		return ncsi_aen_handler(ndp, skb);

	/* Find the handler */
	for (i = 0; i < ARRAY_SIZE(ncsi_rsp_handlers); i++) {
		if (ncsi_rsp_handlers[i].type == hdr->type) {
			if (ncsi_rsp_handlers[i].handler)
				nrh = &ncsi_rsp_handlers[i];
			else
				nrh = NULL;

			break;
		}
	}

	if (!nrh) {
		netdev_err(nd->dev, "Received unrecognized packet (0x%x)\n",
			   hdr->type);
		return -ENOENT;
	}

	/* Associate with the request */
	spin_lock_irqsave(&ndp->lock, flags);
	nr = &ndp->requests[hdr->id];
	if (!nr->used) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		return -ENODEV;
	}

	nr->rsp = skb;
	if (!nr->enabled) {
		spin_unlock_irqrestore(&ndp->lock, flags);
		ret = -ENOENT;
		goto out;
	}

	/* Validate the packet */
	spin_unlock_irqrestore(&ndp->lock, flags);
	payload = nrh->payload;
	if (payload < 0)
		payload = ntohs(hdr->length);
	ret = ncsi_validate_rsp_pkt(nr, payload);
	if (ret) {
		netdev_warn(ndp->ndev.dev,
			    "NCSI: 'bad' packet ignored for type 0x%x\n",
			    hdr->type);
		goto out;
	}

	/* Process the packet */
	ret = nrh->handler(nr);
	if (ret)
		netdev_err(ndp->ndev.dev,
			   "NCSI: Handler for packet type 0x%x returned %d\n",
			   hdr->type, ret);
out:
	ncsi_free_request(nr);
	return ret;
}
