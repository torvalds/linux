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

MODULE_DESCRIPTION("x_tables match for TCP and UDP, supports IPv4 and IPv6");
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
static inline int
port_match(u_int16_t min, u_int16_t max, u_int16_t port, int invert)
{
	int ret;

	ret = (port >= min && port <= max) ^ invert;
	return ret;
}

static int
tcp_find_option(u_int8_t option,
		const struct sk_buff *skb,
		unsigned int protoff,
		unsigned int optlen,
		int invert,
		int *hotdrop)
{
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	u_int8_t _opt[60 - sizeof(struct tcphdr)], *op;
	unsigned int i;

	duprintf("tcp_match: finding option\n");

	if (!optlen)
		return invert;

	/* If we don't have the whole header, drop packet. */
	op = skb_header_pointer(skb, protoff + sizeof(struct tcphdr),
				optlen, _opt);
	if (op == NULL) {
		*hotdrop = 1;
		return 0;
	}

	for (i = 0; i < optlen; ) {
		if (op[i] == option) return !invert;
		if (op[i] < 2) i++;
		else i += op[i+1]?:1;
	}

	return invert;
}

static int
tcp_match(const struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  const void *matchinfo,
	  int offset,
	  unsigned int protoff,
	  int *hotdrop)
{
	struct tcphdr _tcph, *th;
	const struct xt_tcp *tcpinfo = matchinfo;

	if (offset) {
		/* To quote Alan:

		   Don't allow a fragment of TCP 8 bytes in. Nobody normal
		   causes this. Its a cracker trying to break in by doing a
		   flag overwrite to pass the direction checks.
		*/
		if (offset == 1) {
			duprintf("Dropping evil TCP offset=1 frag.\n");
			*hotdrop = 1;
		}
		/* Must not be a fragment. */
		return 0;
	}

#define FWINVTCP(bool,invflg) ((bool) ^ !!(tcpinfo->invflags & invflg))

	th = skb_header_pointer(skb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil TCP offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	if (!port_match(tcpinfo->spts[0], tcpinfo->spts[1],
			ntohs(th->source),
			!!(tcpinfo->invflags & XT_TCP_INV_SRCPT)))
		return 0;
	if (!port_match(tcpinfo->dpts[0], tcpinfo->dpts[1],
			ntohs(th->dest),
			!!(tcpinfo->invflags & XT_TCP_INV_DSTPT)))
		return 0;
	if (!FWINVTCP((((unsigned char *)th)[13] & tcpinfo->flg_mask)
		      == tcpinfo->flg_cmp,
		      XT_TCP_INV_FLAGS))
		return 0;
	if (tcpinfo->option) {
		if (th->doff * 4 < sizeof(_tcph)) {
			*hotdrop = 1;
			return 0;
		}
		if (!tcp_find_option(tcpinfo->option, skb, protoff,
				     th->doff*4 - sizeof(_tcph),
				     tcpinfo->invflags & XT_TCP_INV_OPTION,
				     hotdrop))
			return 0;
	}
	return 1;
}

/* Called when user tries to insert an entry of this type. */
static int
tcp_checkentry(const char *tablename,
	       const void *info,
	       void *matchinfo,
	       unsigned int matchsize,
	       unsigned int hook_mask)
{
	const struct ipt_ip *ip = info;
	const struct xt_tcp *tcpinfo = matchinfo;

	/* Must specify proto == TCP, and no unknown invflags */
	return ip->proto == IPPROTO_TCP
		&& !(ip->invflags & XT_INV_PROTO)
		&& matchsize == XT_ALIGN(sizeof(struct xt_tcp))
		&& !(tcpinfo->invflags & ~XT_TCP_INV_MASK);
}

/* Called when user tries to insert an entry of this type. */
static int
tcp6_checkentry(const char *tablename,
	       const void *entry,
	       void *matchinfo,
	       unsigned int matchsize,
	       unsigned int hook_mask)
{
	const struct ip6t_ip6 *ipv6 = entry;
	const struct xt_tcp *tcpinfo = matchinfo;

	/* Must specify proto == TCP, and no unknown invflags */
	return ipv6->proto == IPPROTO_TCP
		&& !(ipv6->invflags & XT_INV_PROTO)
		&& matchsize == XT_ALIGN(sizeof(struct xt_tcp))
		&& !(tcpinfo->invflags & ~XT_TCP_INV_MASK);
}


static int
udp_match(const struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  const void *matchinfo,
	  int offset,
	  unsigned int protoff,
	  int *hotdrop)
{
	struct udphdr _udph, *uh;
	const struct xt_udp *udpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	uh = skb_header_pointer(skb, protoff, sizeof(_udph), &_udph);
	if (uh == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil UDP tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return port_match(udpinfo->spts[0], udpinfo->spts[1],
			  ntohs(uh->source),
			  !!(udpinfo->invflags & XT_UDP_INV_SRCPT))
		&& port_match(udpinfo->dpts[0], udpinfo->dpts[1],
			      ntohs(uh->dest),
			      !!(udpinfo->invflags & XT_UDP_INV_DSTPT));
}

/* Called when user tries to insert an entry of this type. */
static int
udp_checkentry(const char *tablename,
	       const void *info,
	       void *matchinfo,
	       unsigned int matchinfosize,
	       unsigned int hook_mask)
{
	const struct ipt_ip *ip = info;
	const struct xt_udp *udpinfo = matchinfo;

	/* Must specify proto == UDP, and no unknown invflags */
	if (ip->proto != IPPROTO_UDP || (ip->invflags & XT_INV_PROTO)) {
		duprintf("ipt_udp: Protocol %u != %u\n", ip->proto,
			 IPPROTO_UDP);
		return 0;
	}
	if (matchinfosize != XT_ALIGN(sizeof(struct xt_udp))) {
		duprintf("ipt_udp: matchsize %u != %u\n",
			 matchinfosize, XT_ALIGN(sizeof(struct xt_udp)));
		return 0;
	}
	if (udpinfo->invflags & ~XT_UDP_INV_MASK) {
		duprintf("ipt_udp: unknown flags %X\n",
			 udpinfo->invflags);
		return 0;
	}

	return 1;
}

/* Called when user tries to insert an entry of this type. */
static int
udp6_checkentry(const char *tablename,
	       const void *entry,
	       void *matchinfo,
	       unsigned int matchinfosize,
	       unsigned int hook_mask)
{
	const struct ip6t_ip6 *ipv6 = entry;
	const struct xt_udp *udpinfo = matchinfo;

	/* Must specify proto == UDP, and no unknown invflags */
	if (ipv6->proto != IPPROTO_UDP || (ipv6->invflags & XT_INV_PROTO)) {
		duprintf("ip6t_udp: Protocol %u != %u\n", ipv6->proto,
			 IPPROTO_UDP);
		return 0;
	}
	if (matchinfosize != XT_ALIGN(sizeof(struct xt_udp))) {
		duprintf("ip6t_udp: matchsize %u != %u\n",
			 matchinfosize, XT_ALIGN(sizeof(struct xt_udp)));
		return 0;
	}
	if (udpinfo->invflags & ~XT_UDP_INV_MASK) {
		duprintf("ip6t_udp: unknown flags %X\n",
			 udpinfo->invflags);
		return 0;
	}

	return 1;
}

static struct xt_match tcp_matchstruct = {
	.name		= "tcp",
	.match		= &tcp_match,
	.checkentry	= &tcp_checkentry,
	.me		= THIS_MODULE,
};
static struct xt_match tcp6_matchstruct = {
	.name		= "tcp",
	.match		= &tcp_match,
	.checkentry	= &tcp6_checkentry,
	.me		= THIS_MODULE,
};

static struct xt_match udp_matchstruct = {
	.name		= "udp",
	.match		= &udp_match,
	.checkentry	= &udp_checkentry,
	.me		= THIS_MODULE,
};
static struct xt_match udp6_matchstruct = {
	.name		= "udp",
	.match		= &udp_match,
	.checkentry	= &udp6_checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;
	ret = xt_register_match(AF_INET, &tcp_matchstruct);
	if (ret)
		return ret;

	ret = xt_register_match(AF_INET6, &tcp6_matchstruct);
	if (ret)
		goto out_unreg_tcp;

	ret = xt_register_match(AF_INET, &udp_matchstruct);
	if (ret)
		goto out_unreg_tcp6;
	
	ret = xt_register_match(AF_INET6, &udp6_matchstruct);
	if (ret)
		goto out_unreg_udp;

	return ret;

out_unreg_udp:
	xt_unregister_match(AF_INET, &tcp_matchstruct);
out_unreg_tcp6:
	xt_unregister_match(AF_INET6, &tcp6_matchstruct);
out_unreg_tcp:
	xt_unregister_match(AF_INET, &tcp_matchstruct);
	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET6, &udp6_matchstruct);
	xt_unregister_match(AF_INET, &udp_matchstruct);
	xt_unregister_match(AF_INET6, &tcp6_matchstruct);
	xt_unregister_match(AF_INET, &tcp_matchstruct);
}

module_init(init);
module_exit(fini);
