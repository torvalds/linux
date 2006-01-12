/*
 *  ebt_stp
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *	Stephen Hemminger <shemminger@osdl.org>
 *
 *  July, 2003
 */

#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_stp.h>
#include <linux/etherdevice.h>
#include <linux/module.h>

#define BPDU_TYPE_CONFIG 0
#define BPDU_TYPE_TCN 0x80

struct stp_header {
	uint8_t dsap;
	uint8_t ssap;
	uint8_t ctrl;
	uint8_t pid;
	uint8_t vers;
	uint8_t type;
};

struct stp_config_pdu {
	uint8_t flags;
	uint8_t root[8];
	uint8_t root_cost[4];
	uint8_t sender[8];
	uint8_t port[2];
	uint8_t msg_age[2];
	uint8_t max_age[2];
	uint8_t hello_time[2];
	uint8_t forward_delay[2];
};

#define NR16(p) (p[0] << 8 | p[1])
#define NR32(p) ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3])

static int ebt_filter_config(struct ebt_stp_info *info,
   struct stp_config_pdu *stpc)
{
	struct ebt_stp_config_info *c;
	uint16_t v16;
	uint32_t v32;
	int verdict, i;

	c = &info->config;
	if ((info->bitmask & EBT_STP_FLAGS) &&
	    FWINV(c->flags != stpc->flags, EBT_STP_FLAGS))
		return EBT_NOMATCH;
	if (info->bitmask & EBT_STP_ROOTPRIO) {
		v16 = NR16(stpc->root);
		if (FWINV(v16 < c->root_priol ||
		    v16 > c->root_priou, EBT_STP_ROOTPRIO))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_ROOTADDR) {
		verdict = 0;
		for (i = 0; i < 6; i++)
			verdict |= (stpc->root[2+i] ^ c->root_addr[i]) &
			           c->root_addrmsk[i];
		if (FWINV(verdict != 0, EBT_STP_ROOTADDR))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_ROOTCOST) {
		v32 = NR32(stpc->root_cost);
		if (FWINV(v32 < c->root_costl ||
		    v32 > c->root_costu, EBT_STP_ROOTCOST))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_SENDERPRIO) {
		v16 = NR16(stpc->sender);
		if (FWINV(v16 < c->sender_priol ||
		    v16 > c->sender_priou, EBT_STP_SENDERPRIO))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_SENDERADDR) {
		verdict = 0;
		for (i = 0; i < 6; i++)
			verdict |= (stpc->sender[2+i] ^ c->sender_addr[i]) &
			           c->sender_addrmsk[i];
		if (FWINV(verdict != 0, EBT_STP_SENDERADDR))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_PORT) {
		v16 = NR16(stpc->port);
		if (FWINV(v16 < c->portl ||
		    v16 > c->portu, EBT_STP_PORT))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_MSGAGE) {
		v16 = NR16(stpc->msg_age);
		if (FWINV(v16 < c->msg_agel ||
		    v16 > c->msg_ageu, EBT_STP_MSGAGE))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_MAXAGE) {
		v16 = NR16(stpc->max_age);
		if (FWINV(v16 < c->max_agel ||
		    v16 > c->max_ageu, EBT_STP_MAXAGE))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_HELLOTIME) {
		v16 = NR16(stpc->hello_time);
		if (FWINV(v16 < c->hello_timel ||
		    v16 > c->hello_timeu, EBT_STP_HELLOTIME))
			return EBT_NOMATCH;
	}
	if (info->bitmask & EBT_STP_FWDD) {
		v16 = NR16(stpc->forward_delay);
		if (FWINV(v16 < c->forward_delayl ||
		    v16 > c->forward_delayu, EBT_STP_FWDD))
			return EBT_NOMATCH;
	}
	return EBT_MATCH;
}

static int ebt_filter_stp(const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const void *data, unsigned int datalen)
{
	struct ebt_stp_info *info = (struct ebt_stp_info *)data;
	struct stp_header _stph, *sp;
	uint8_t header[6] = {0x42, 0x42, 0x03, 0x00, 0x00, 0x00};

	sp = skb_header_pointer(skb, 0, sizeof(_stph), &_stph);
	if (sp == NULL)
		return EBT_NOMATCH;

	/* The stp code only considers these */
	if (memcmp(sp, header, sizeof(header)))
		return EBT_NOMATCH;

	if (info->bitmask & EBT_STP_TYPE
	    && FWINV(info->type != sp->type, EBT_STP_TYPE))
		return EBT_NOMATCH;

	if (sp->type == BPDU_TYPE_CONFIG &&
	    info->bitmask & EBT_STP_CONFIG_MASK) {
		struct stp_config_pdu _stpc, *st;

		st = skb_header_pointer(skb, sizeof(_stph),
					sizeof(_stpc), &_stpc);
		if (st == NULL)
			return EBT_NOMATCH;
		return ebt_filter_config(info, st);
	}
	return EBT_MATCH;
}

static int ebt_stp_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_stp_info *info = (struct ebt_stp_info *)data;
	int len = EBT_ALIGN(sizeof(struct ebt_stp_info));
	uint8_t bridge_ula[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
	uint8_t msk[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	if (info->bitmask & ~EBT_STP_MASK || info->invflags & ~EBT_STP_MASK ||
	    !(info->bitmask & EBT_STP_MASK))
		return -EINVAL;
	if (datalen != len)
		return -EINVAL;
	/* Make sure the match only receives stp frames */
	if (compare_ether_addr(e->destmac, bridge_ula) ||
	    compare_ether_addr(e->destmsk, msk) || !(e->bitmask & EBT_DESTMAC))
		return -EINVAL;

	return 0;
}

static struct ebt_match filter_stp =
{
	.name		= EBT_STP_MATCH,
	.match		= ebt_filter_stp,
	.check		= ebt_stp_check,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ebt_register_match(&filter_stp);
}

static void __exit fini(void)
{
	ebt_unregister_match(&filter_stp);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
