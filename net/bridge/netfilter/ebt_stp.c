/*
 *  ebt_stp
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *	Stephen Hemminger <shemminger@osdl.org>
 *
 *  July, 2003
 */
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_stp.h>

#define BPDU_TYPE_CONFIG 0
#define BPDU_TYPE_TCN 0x80

struct stp_header {
	u8 dsap;
	u8 ssap;
	u8 ctrl;
	u8 pid;
	u8 vers;
	u8 type;
};

struct stp_config_pdu {
	u8 flags;
	u8 root[8];
	u8 root_cost[4];
	u8 sender[8];
	u8 port[2];
	u8 msg_age[2];
	u8 max_age[2];
	u8 hello_time[2];
	u8 forward_delay[2];
};

#define NR16(p) (p[0] << 8 | p[1])
#define NR32(p) ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3])

static bool ebt_filter_config(const struct ebt_stp_info *info,
			      const struct stp_config_pdu *stpc)
{
	const struct ebt_stp_config_info *c;
	u16 v16;
	u32 v32;

	c = &info->config;
	if ((info->bitmask & EBT_STP_FLAGS) &&
	    NF_INVF(info, EBT_STP_FLAGS, c->flags != stpc->flags))
		return false;
	if (info->bitmask & EBT_STP_ROOTPRIO) {
		v16 = NR16(stpc->root);
		if (NF_INVF(info, EBT_STP_ROOTPRIO,
			    v16 < c->root_priol || v16 > c->root_priou))
			return false;
	}
	if (info->bitmask & EBT_STP_ROOTADDR) {
		if (NF_INVF(info, EBT_STP_ROOTADDR,
			    !ether_addr_equal_masked(&stpc->root[2],
						     c->root_addr,
						     c->root_addrmsk)))
			return false;
	}
	if (info->bitmask & EBT_STP_ROOTCOST) {
		v32 = NR32(stpc->root_cost);
		if (NF_INVF(info, EBT_STP_ROOTCOST,
			    v32 < c->root_costl || v32 > c->root_costu))
			return false;
	}
	if (info->bitmask & EBT_STP_SENDERPRIO) {
		v16 = NR16(stpc->sender);
		if (NF_INVF(info, EBT_STP_SENDERPRIO,
			    v16 < c->sender_priol || v16 > c->sender_priou))
			return false;
	}
	if (info->bitmask & EBT_STP_SENDERADDR) {
		if (NF_INVF(info, EBT_STP_SENDERADDR,
			    !ether_addr_equal_masked(&stpc->sender[2],
						     c->sender_addr,
						     c->sender_addrmsk)))
			return false;
	}
	if (info->bitmask & EBT_STP_PORT) {
		v16 = NR16(stpc->port);
		if (NF_INVF(info, EBT_STP_PORT,
			    v16 < c->portl || v16 > c->portu))
			return false;
	}
	if (info->bitmask & EBT_STP_MSGAGE) {
		v16 = NR16(stpc->msg_age);
		if (NF_INVF(info, EBT_STP_MSGAGE,
			    v16 < c->msg_agel || v16 > c->msg_ageu))
			return false;
	}
	if (info->bitmask & EBT_STP_MAXAGE) {
		v16 = NR16(stpc->max_age);
		if (NF_INVF(info, EBT_STP_MAXAGE,
			    v16 < c->max_agel || v16 > c->max_ageu))
			return false;
	}
	if (info->bitmask & EBT_STP_HELLOTIME) {
		v16 = NR16(stpc->hello_time);
		if (NF_INVF(info, EBT_STP_HELLOTIME,
			    v16 < c->hello_timel || v16 > c->hello_timeu))
			return false;
	}
	if (info->bitmask & EBT_STP_FWDD) {
		v16 = NR16(stpc->forward_delay);
		if (NF_INVF(info, EBT_STP_FWDD,
			    v16 < c->forward_delayl || v16 > c->forward_delayu))
			return false;
	}
	return true;
}

static bool
ebt_stp_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct ebt_stp_info *info = par->matchinfo;
	const struct stp_header *sp;
	struct stp_header _stph;
	const u8 header[6] = {0x42, 0x42, 0x03, 0x00, 0x00, 0x00};

	sp = skb_header_pointer(skb, 0, sizeof(_stph), &_stph);
	if (sp == NULL)
		return false;

	/* The stp code only considers these */
	if (memcmp(sp, header, sizeof(header)))
		return false;

	if ((info->bitmask & EBT_STP_TYPE) &&
	    NF_INVF(info, EBT_STP_TYPE, info->type != sp->type))
		return false;

	if (sp->type == BPDU_TYPE_CONFIG &&
	    info->bitmask & EBT_STP_CONFIG_MASK) {
		const struct stp_config_pdu *st;
		struct stp_config_pdu _stpc;

		st = skb_header_pointer(skb, sizeof(_stph),
					sizeof(_stpc), &_stpc);
		if (st == NULL)
			return false;
		return ebt_filter_config(info, st);
	}
	return true;
}

static int ebt_stp_mt_check(const struct xt_mtchk_param *par)
{
	const struct ebt_stp_info *info = par->matchinfo;
	const struct ebt_entry *e = par->entryinfo;

	if (info->bitmask & ~EBT_STP_MASK || info->invflags & ~EBT_STP_MASK ||
	    !(info->bitmask & EBT_STP_MASK))
		return -EINVAL;
	/* Make sure the match only receives stp frames */
	if (!par->nft_compat &&
	    (!ether_addr_equal(e->destmac, eth_stp_addr) ||
	     !(e->bitmask & EBT_DESTMAC) ||
	     !is_broadcast_ether_addr(e->destmsk)))
		return -EINVAL;

	return 0;
}

static struct xt_match ebt_stp_mt_reg __read_mostly = {
	.name		= "stp",
	.revision	= 0,
	.family		= NFPROTO_BRIDGE,
	.match		= ebt_stp_mt,
	.checkentry	= ebt_stp_mt_check,
	.matchsize	= sizeof(struct ebt_stp_info),
	.me		= THIS_MODULE,
};

static int __init ebt_stp_init(void)
{
	return xt_register_match(&ebt_stp_mt_reg);
}

static void __exit ebt_stp_fini(void)
{
	xt_unregister_match(&ebt_stp_mt_reg);
}

module_init(ebt_stp_init);
module_exit(ebt_stp_fini);
MODULE_DESCRIPTION("Ebtables: Spanning Tree Protocol packet match");
MODULE_LICENSE("GPL");
