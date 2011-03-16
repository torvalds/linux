#ifndef _IP_SET_GETPORT_H
#define _IP_SET_GETPORT_H

extern bool ip_set_get_ip4_port(const struct sk_buff *skb, bool src,
				__be16 *port, u8 *proto);

#if defined(CONFIG_IP6_NF_IPTABLES) || defined(CONFIG_IP6_NF_IPTABLES_MODULE)
extern bool ip_set_get_ip6_port(const struct sk_buff *skb, bool src,
				__be16 *port, u8 *proto);
#else
static inline bool ip_set_get_ip6_port(const struct sk_buff *skb, bool src,
				       __be16 *port, u8 *proto)
{
	return false;
}
#endif

extern bool ip_set_get_ip_port(const struct sk_buff *skb, u8 pf, bool src,
				__be16 *port);

#endif /*_IP_SET_GETPORT_H*/
