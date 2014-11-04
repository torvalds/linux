#ifndef __NET_FOU_H
#define __NET_FOU_H

#include <linux/skbuff.h>

#include <net/flow.h>
#include <net/gue.h>
#include <net/ip_tunnels.h>
#include <net/udp.h>

int fou_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		     u8 *protocol, struct flowi4 *fl4);
int gue_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		     u8 *protocol, struct flowi4 *fl4);

static size_t fou_encap_hlen(struct ip_tunnel_encap *e)
{
	return sizeof(struct udphdr);
}

static size_t gue_encap_hlen(struct ip_tunnel_encap *e)
{
	return sizeof(struct udphdr) + sizeof(struct guehdr);
}

#endif
