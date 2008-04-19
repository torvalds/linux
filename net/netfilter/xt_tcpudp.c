#include <linux/types.h>
#include <linux/module.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_DESCRIPTION("Xtables: TCP, UDP and UDP-Lite match");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xt_tcp");
MODULE_ALIAS("xt_udp");
MODULE_ALIAS("ipt_udp");
MODULE_ALIAS("ipt_tcp");
MODULE_ALIAS("ip6t_udp");
MODULE_ALIAS("ip6t_tcp");

#ifdef DEBUG_IP_FIREWALL_USER
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif


/* Returns 1 if the port is matched by the range, 0 otherwise */
static inline bool
port_match(u_int16_t min, u_int16_t max, u_int16_t port, bool invert)
{
	return (port >= min && port <= max) ^ invert;
}

static bool
tcp_find_option(u_int8_t option,
		const struct sk_buff *skb,
		unsigned int protoff,
		unsigned int optlen,
		bool invert,
		bool *hotdrop)
{
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	const u_int8_t *op;
	u_int8_t _opt[60 - sizeof(struct tcphdr)];
	unsigned int i;

	duprintf("tcp_match: finding option\n");

	if (!optlen)
		return invert;

	/* If we don't have the whole header, drop packet. */
	op = skb_header_pointer(skb, protoff + sizeof(struct tcphdr),
				optlen, _opt);
	if (op == NULL) {
		*hotdrop = true;
		return false;
	}

	for (i = 0; i < optlen; ) {
		if (op[i] == option) return !invert;
		if (op[i] < 2) i++;
		else i += op[i+1]?:1;
	}

	return invert;
}

static bool
tcp_mt(const struct sk_buff *skb, const struct net_device *in,
       const struct net_device *out, const struct xt_match *match,
       const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct tcphdr *th;
	struct tcphdr _tcph;
	const struct xt_tcp *tcpinfo = matchinfo;

	if (offset) {
		/* To quote Alan:

		   Don't allow a fragment of TCP 8 bytes in. Nobody normal
		   causes this. Its a cracker trying to break in by doing a
		   flag overwrite to pass the direction checks.
		*/
		if (offset == 1) {
			duprintf("Dropping evil TCP offset=1 frag.\n");
			*hotdrop = true;
		}
		/* Must not be a fragment. */
		return false;
	}

#define FWINVTCP(bool, invflg) ((bool) ^ !!(tcpinfo->invflags & (invflg)))

	th = skb_header_pointer(skb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil TCP offset=0 tinygram.\n");
		*hotdrop = true;
		return false;
	}

	if (!port_match(tcpinfo->spts[0], tcpinfo->spts[1],
			ntohs(th->source),
			!!(tcpinfo->invflags & XT_TCP_INV_SRCPT)))
		return false;
	if (!port_match(tcpinfo->dpts[0], tcpinfo->dpts[1],
			ntohs(th->dest),
			!!(tcpinfo->invflags & XT_TCP_INV_DSTPT)))
		return false;
	if (!FWINVTCP((((unsigned char *)th)[13] & tcpinfo->flg_mask)
		      == tcpinfo->flg_cmp,
		      XT_TCP_INV_FLAGS))
		return false;
	if (tcpinfo->option) {
		if (th->doff * 4 < sizeof(_tcph)) {
			*hotdrop = true;
			return false;
		}
		if (!tcp_find_option(tcpinfo->option, skb, protoff,
				     th->doff*4 - sizeof(_tcph),
				     tcpinfo->invflags & XT_TCP_INV_OPTION,
				     hotdrop))
			return false;
	}
	return true;
}

/* Called when user tries to insert an entry of this type. */
static bool
tcp_mt_check(const char *tablename, const void *info,
             const struct xt_match *match, void *matchinfo,
             unsigned int hook_mask)
{
	const struct xt_tcp *tcpinfo = matchinfo;

	/* Must specify no unknown invflags */
	return !(tcpinfo->invflags & ~XT_TCP_INV_MASK);
}

static bool
udp_mt(const struct sk_buff *skb, const struct net_device *in,
       const struct net_device *out, const struct xt_match *match,
       const void *matchinfo, int offset, unsigned int protoff, bool *hotdrop)
{
	const struct udphdr *uh;
	struct udphdr _udph;
	const struct xt_udp *udpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return false;

	uh = skb_header_pointer(skb, protoff, sizeof(_udph), &_udph);
	if (uh == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil UDP tinygram.\n");
		*hotdrop = true;
		return false;
	}

	return port_match(udpinfo->spts[0], udpinfo->spts[1],
			  ntohs(uh->source),
			  !!(udpinfo->invflags & XT_UDP_INV_SRCPT))
		&& port_match(udpinfo->dpts[0], udpinfo->dpts[1],
			      ntohs(uh->dest),
			      !!(udpinfo->invflags & XT_UDP_INV_DSTPT));
}

/* Called when user tries to insert an entry of this type. */
static bool
udp_mt_check(const char *tablename, const void *info,
             const struct xt_match *match, void *matchinfo,
             unsigned int hook_mask)
{
	const struct xt_udp *udpinfo = matchinfo;

	/* Must specify no unknown invflags */
	return !(udpinfo->invflags & ~XT_UDP_INV_MASK);
}

static struct xt_match tcpudp_mt_reg[] __read_mostly = {
	{
		.name		= "tcp",
		.family		= AF_INET,
		.checkentry	= tcp_mt_check,
		.match		= tcp_mt,
		.matchsize	= sizeof(struct xt_tcp),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
	{
		.name		= "tcp",
		.family		= AF_INET6,
		.checkentry	= tcp_mt_check,
		.match		= tcp_mt,
		.matchsize	= sizeof(struct xt_tcp),
		.proto		= IPPROTO_TCP,
		.me		= THIS_MODULE,
	},
	{
		.name		= "udp",
		.family		= AF_INET,
		.checkentry	= udp_mt_check,
		.match		= udp_mt,
		.matchsize	= sizeof(struct xt_udp),
		.proto		= IPPROTO_UDP,
		.me		= THIS_MODULE,
	},
	{
		.name		= "udp",
		.family		= AF_INET6,
		.checkentry	= udp_mt_check,
		.match		= udp_mt,
		.matchsize	= sizeof(struct xt_udp),
		.proto		= IPPROTO_UDP,
		.me		= THIS_MODULE,
	},
	{
		.name		= "udplite",
		.family		= AF_INET,
		.checkentry	= udp_mt_check,
		.match		= udp_mt,
		.matchsize	= sizeof(struct xt_udp),
		.proto		= IPPROTO_UDPLITE,
		.me		= THIS_MODULE,
	},
	{
		.name		= "udplite",
		.family		= AF_INET6,
		.checkentry	= udp_mt_check,
		.match		= udp_mt,
		.matchsize	= sizeof(struct xt_udp),
		.proto		= IPPROTO_UDPLITE,
		.me		= THIS_MODULE,
	},
};

static int __init tcpudp_mt_init(void)
{
	return xt_register_matches(tcpudp_mt_reg, ARRAY_SIZE(tcpudp_mt_reg));
}

static void __exit tcpudp_mt_exit(void)
{
	xt_unregister_matches(tcpudp_mt_reg, ARRAY_SIZE(tcpudp_mt_reg));
}

module_init(tcpudp_mt_init);
module_exit(tcpudp_mt_exit);
