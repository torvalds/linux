/*
 * ip_vs_proto_esp.c:	ESP IPSec load balancing support for IPVS
 *
 * Authors:	Julian Anastasov <ja@ssi.bg>, February 2002
 *		Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		version 2 as published by the Free Software Foundation;
 *
 */

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


/* TODO:

struct isakmp_hdr {
	__u8		icookie[8];
	__u8		rcookie[8];
	__u8		np;
	__u8		version;
	__u8		xchgtype;
	__u8		flags;
	__u32		msgid;
	__u32		length;
};

*/

#define PORT_ISAKMP	500


static struct ip_vs_conn *
esp_conn_in_get(const struct sk_buff *skb,
		struct ip_vs_protocol *pp,
		const struct iphdr *iph,
		unsigned int proto_off,
		int inverse)
{
	struct ip_vs_conn *cp;

	if (likely(!inverse)) {
		cp = ip_vs_conn_in_get(IPPROTO_UDP,
				       iph->saddr,
				       htons(PORT_ISAKMP),
				       iph->daddr,
				       htons(PORT_ISAKMP));
	} else {
		cp = ip_vs_conn_in_get(IPPROTO_UDP,
				       iph->daddr,
				       htons(PORT_ISAKMP),
				       iph->saddr,
				       htons(PORT_ISAKMP));
	}

	if (!cp) {
		/*
		 * We are not sure if the packet is from our
		 * service, so our conn_schedule hook should return NF_ACCEPT
		 */
		IP_VS_DBG(12, "Unknown ISAKMP entry for outin packet "
			  "%s%s %u.%u.%u.%u->%u.%u.%u.%u\n",
			  inverse ? "ICMP+" : "",
			  pp->name,
			  NIPQUAD(iph->saddr),
			  NIPQUAD(iph->daddr));
	}

	return cp;
}


static struct ip_vs_conn *
esp_conn_out_get(const struct sk_buff *skb, struct ip_vs_protocol *pp,
		 const struct iphdr *iph, unsigned int proto_off, int inverse)
{
	struct ip_vs_conn *cp;

	if (likely(!inverse)) {
		cp = ip_vs_conn_out_get(IPPROTO_UDP,
					iph->saddr,
					htons(PORT_ISAKMP),
					iph->daddr,
					htons(PORT_ISAKMP));
	} else {
		cp = ip_vs_conn_out_get(IPPROTO_UDP,
					iph->daddr,
					htons(PORT_ISAKMP),
					iph->saddr,
					htons(PORT_ISAKMP));
	}

	if (!cp) {
		IP_VS_DBG(12, "Unknown ISAKMP entry for inout packet "
			  "%s%s %u.%u.%u.%u->%u.%u.%u.%u\n",
			  inverse ? "ICMP+" : "",
			  pp->name,
			  NIPQUAD(iph->saddr),
			  NIPQUAD(iph->daddr));
	}

	return cp;
}


static int
esp_conn_schedule(struct sk_buff *skb, struct ip_vs_protocol *pp,
		  int *verdict, struct ip_vs_conn **cpp)
{
	/*
	 * ESP is only related traffic. Pass the packet to IP stack.
	 */
	*verdict = NF_ACCEPT;
	return 0;
}


static void
esp_debug_packet(struct ip_vs_protocol *pp, const struct sk_buff *skb,
		 int offset, const char *msg)
{
	char buf[256];
	struct iphdr _iph, *ih;

	ih = skb_header_pointer(skb, offset, sizeof(_iph), &_iph);
	if (ih == NULL)
		sprintf(buf, "%s TRUNCATED", pp->name);
	else
		sprintf(buf, "%s %u.%u.%u.%u->%u.%u.%u.%u",
			pp->name, NIPQUAD(ih->saddr),
			NIPQUAD(ih->daddr));

	printk(KERN_DEBUG "IPVS: %s: %s\n", msg, buf);
}


static void esp_init(struct ip_vs_protocol *pp)
{
	/* nothing to do now */
}


static void esp_exit(struct ip_vs_protocol *pp)
{
	/* nothing to do now */
}


struct ip_vs_protocol ip_vs_protocol_esp = {
	.name =			"ESP",
	.protocol =		IPPROTO_ESP,
	.num_states =		1,
	.dont_defrag =		1,
	.init =			esp_init,
	.exit =			esp_exit,
	.conn_schedule =	esp_conn_schedule,
	.conn_in_get =		esp_conn_in_get,
	.conn_out_get =		esp_conn_out_get,
	.snat_handler =		NULL,
	.dnat_handler =		NULL,
	.csum_check =		NULL,
	.state_transition =	NULL,
	.register_app =		NULL,
	.unregister_app =	NULL,
	.app_conn_bind =	NULL,
	.debug_packet =		esp_debug_packet,
	.timeout_change =	NULL,		/* ISAKMP */
};
