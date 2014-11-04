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
	size_t len;
	bool need_priv = false;

	len = sizeof(struct udphdr) + sizeof(struct guehdr);

	if (e->flags & TUNNEL_ENCAP_FLAG_REMCSUM) {
		len += GUE_PLEN_REMCSUM;
		need_priv = true;
	}

	len += need_priv ? GUE_LEN_PRIV : 0;

	return len;
}

#endif
