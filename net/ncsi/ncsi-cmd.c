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
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include <net/ncsi.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "internal.h"
#include "ncsi-pkt.h"

u32 ncsi_calculate_checksum(unsigned char *data, int len)
{
	u32 checksum = 0;
	int i;

	for (i = 0; i < len; i += 2)
		checksum += (((u32)data[i] << 8) | data[i + 1]);

	checksum = (~checksum + 1);
	return checksum;
}

/* This function should be called after the data area has been
 * populated completely.
 */
static void ncsi_cmd_build_header(struct ncsi_pkt_hdr *h,
				  struct ncsi_cmd_arg *nca)
{
	u32 checksum;
	__be32 *pchecksum;

	h->mc_id        = 0;
	h->revision     = NCSI_PKT_REVISION;
	h->reserved     = 0;
	h->id           = nca->id;
	h->type         = nca->type;
	h->channel      = NCSI_TO_CHANNEL(nca->package,
					  nca->channel);
	h->length       = htons(nca->payload);
	h->reserved1[0] = 0;
	h->reserved1[1] = 0;

	/* Fill with calculated checksum */
	checksum = ncsi_calculate_checksum((unsigned char *)h,
					   sizeof(*h) + nca->payload);
	pchecksum = (__be32 *)((void *)h + sizeof(struct ncsi_pkt_hdr) +
		    nca->payload);
	*pchecksum = htonl(checksum);
}

static int ncsi_cmd_handler_default(struct sk_buff *skb,
				    struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_sp(struct sk_buff *skb,
			       struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_sp_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->hw_arbitration = nca->bytes[0];
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_dc(struct sk_buff *skb,
			       struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_dc_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->ald = nca->bytes[0];
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_rc(struct sk_buff *skb,
			       struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_rc_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_ae(struct sk_buff *skb,
			       struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_ae_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->mc_id = nca->bytes[0];
	cmd->mode = htonl(nca->dwords[1]);
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_sl(struct sk_buff *skb,
			       struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_sl_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->mode = htonl(nca->dwords[0]);
	cmd->oem_mode = htonl(nca->dwords[1]);
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_svf(struct sk_buff *skb,
				struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_svf_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->vlan = htons(nca->words[0]);
	cmd->index = nca->bytes[2];
	cmd->enable = nca->bytes[3];
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_ev(struct sk_buff *skb,
			       struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_ev_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->mode = nca->bytes[0];
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_sma(struct sk_buff *skb,
				struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_sma_pkt *cmd;
	int i;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	for (i = 0; i < 6; i++)
		cmd->mac[i] = nca->bytes[i];
	cmd->index = nca->bytes[6];
	cmd->at_e = nca->bytes[7];
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_ebf(struct sk_buff *skb,
				struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_ebf_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->mode = htonl(nca->dwords[0]);
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_egmf(struct sk_buff *skb,
				 struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_egmf_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->mode = htonl(nca->dwords[0]);
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static int ncsi_cmd_handler_snfc(struct sk_buff *skb,
				 struct ncsi_cmd_arg *nca)
{
	struct ncsi_cmd_snfc_pkt *cmd;

	cmd = skb_put_zero(skb, sizeof(*cmd));
	cmd->mode = nca->bytes[0];
	ncsi_cmd_build_header(&cmd->cmd.common, nca);

	return 0;
}

static struct ncsi_cmd_handler {
	unsigned char type;
	int           payload;
	int           (*handler)(struct sk_buff *skb,
				 struct ncsi_cmd_arg *nca);
} ncsi_cmd_handlers[] = {
	{ NCSI_PKT_CMD_CIS,    0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_SP,     4, ncsi_cmd_handler_sp      },
	{ NCSI_PKT_CMD_DP,     0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_EC,     0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_DC,     4, ncsi_cmd_handler_dc      },
	{ NCSI_PKT_CMD_RC,     4, ncsi_cmd_handler_rc      },
	{ NCSI_PKT_CMD_ECNT,   0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_DCNT,   0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_AE,     8, ncsi_cmd_handler_ae      },
	{ NCSI_PKT_CMD_SL,     8, ncsi_cmd_handler_sl      },
	{ NCSI_PKT_CMD_GLS,    0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_SVF,    4, ncsi_cmd_handler_svf     },
	{ NCSI_PKT_CMD_EV,     4, ncsi_cmd_handler_ev      },
	{ NCSI_PKT_CMD_DV,     0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_SMA,    8, ncsi_cmd_handler_sma     },
	{ NCSI_PKT_CMD_EBF,    4, ncsi_cmd_handler_ebf     },
	{ NCSI_PKT_CMD_DBF,    0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_EGMF,   4, ncsi_cmd_handler_egmf    },
	{ NCSI_PKT_CMD_DGMF,   0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_SNFC,   4, ncsi_cmd_handler_snfc    },
	{ NCSI_PKT_CMD_GVI,    0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_GC,     0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_GP,     0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_GCPS,   0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_GNS,    0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_GNPTS,  0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_GPS,    0, ncsi_cmd_handler_default },
	{ NCSI_PKT_CMD_OEM,    0, NULL                     },
	{ NCSI_PKT_CMD_PLDM,   0, NULL                     },
	{ NCSI_PKT_CMD_GPUUID, 0, ncsi_cmd_handler_default }
};

static struct ncsi_request *ncsi_alloc_command(struct ncsi_cmd_arg *nca)
{
	struct ncsi_dev_priv *ndp = nca->ndp;
	struct ncsi_dev *nd = &ndp->ndev;
	struct net_device *dev = nd->dev;
	int hlen = LL_RESERVED_SPACE(dev);
	int tlen = dev->needed_tailroom;
	int len = hlen + tlen;
	struct sk_buff *skb;
	struct ncsi_request *nr;

	nr = ncsi_alloc_request(ndp, nca->req_flags);
	if (!nr)
		return NULL;

	/* NCSI command packet has 16-bytes header, payload, 4 bytes checksum.
	 * The packet needs padding if its payload is less than 26 bytes to
	 * meet 64 bytes minimal ethernet frame length.
	 */
	len += sizeof(struct ncsi_cmd_pkt_hdr) + 4;
	if (nca->payload < 26)
		len += 26;
	else
		len += nca->payload;

	/* Allocate skb */
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		ncsi_free_request(nr);
		return NULL;
	}

	nr->cmd = skb;
	skb_reserve(skb, hlen);
	skb_reset_network_header(skb);

	skb->dev = dev;
	skb->protocol = htons(ETH_P_NCSI);

	return nr;
}

int ncsi_xmit_cmd(struct ncsi_cmd_arg *nca)
{
	struct ncsi_request *nr;
	struct ethhdr *eh;
	struct ncsi_cmd_handler *nch = NULL;
	int i, ret;

	/* Search for the handler */
	for (i = 0; i < ARRAY_SIZE(ncsi_cmd_handlers); i++) {
		if (ncsi_cmd_handlers[i].type == nca->type) {
			if (ncsi_cmd_handlers[i].handler)
				nch = &ncsi_cmd_handlers[i];
			else
				nch = NULL;

			break;
		}
	}

	if (!nch) {
		netdev_err(nca->ndp->ndev.dev,
			   "Cannot send packet with type 0x%02x\n", nca->type);
		return -ENOENT;
	}

	/* Get packet payload length and allocate the request */
	nca->payload = nch->payload;
	nr = ncsi_alloc_command(nca);
	if (!nr)
		return -ENOMEM;

	/* Prepare the packet */
	nca->id = nr->id;
	ret = nch->handler(nr->cmd, nca);
	if (ret) {
		ncsi_free_request(nr);
		return ret;
	}

	/* Fill the ethernet header */
	eh = skb_push(nr->cmd, sizeof(*eh));
	eh->h_proto = htons(ETH_P_NCSI);
	eth_broadcast_addr(eh->h_dest);
	eth_broadcast_addr(eh->h_source);

	/* Start the timer for the request that might not have
	 * corresponding response. Given NCSI is an internal
	 * connection a 1 second delay should be sufficient.
	 */
	nr->enabled = true;
	mod_timer(&nr->timer, jiffies + 1 * HZ);

	/* Send NCSI packet */
	skb_get(nr->cmd);
	ret = dev_queue_xmit(nr->cmd);
	if (ret < 0) {
		ncsi_free_request(nr);
		return ret;
	}

	return 0;
}
