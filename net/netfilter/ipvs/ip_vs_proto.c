/*
 * ip_vs_proto.c: transport protocol load balancing support for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/gfp.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>

#include <net/ip_vs.h>


/*
 * IPVS protocols can only be registered/unregistered when the ipvs
 * module is loaded/unloaded, so no lock is needed in accessing the
 * ipvs protocol table.
 */

#define IP_VS_PROTO_TAB_SIZE		32	/* must be power of 2 */
#define IP_VS_PROTO_HASH(proto)		((proto) & (IP_VS_PROTO_TAB_SIZE-1))

static struct ip_vs_protocol *ip_vs_proto_table[IP_VS_PROTO_TAB_SIZE];


/*
 *	register an ipvs protocol
 */
static int __used __init register_ip_vs_protocol(struct ip_vs_protocol *pp)
{
	unsigned hash = IP_VS_PROTO_HASH(pp->protocol);

	pp->next = ip_vs_proto_table[hash];
	ip_vs_proto_table[hash] = pp;

	if (pp->init != NULL)
		pp->init(pp);

	return 0;
}

/*
 *	register an ipvs protocols netns related data
 */
static int
register_ip_vs_proto_netns(struct net *net, struct ip_vs_protocol *pp)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	unsigned hash = IP_VS_PROTO_HASH(pp->protocol);
	struct ip_vs_proto_data *pd =
			kzalloc(sizeof(struct ip_vs_proto_data), GFP_ATOMIC);

	if (!pd)
		return -ENOMEM;

	pd->pp = pp;	/* For speed issues */
	pd->next = ipvs->proto_data_table[hash];
	ipvs->proto_data_table[hash] = pd;
	atomic_set(&pd->appcnt, 0);	/* Init app counter */

	if (pp->init_netns != NULL) {
		int ret = pp->init_netns(net, pd);
		if (ret) {
			/* unlink an free proto data */
			ipvs->proto_data_table[hash] = pd->next;
			kfree(pd);
			return ret;
		}
	}

	return 0;
}

/*
 *	unregister an ipvs protocol
 */
static int unregister_ip_vs_protocol(struct ip_vs_protocol *pp)
{
	struct ip_vs_protocol **pp_p;
	unsigned hash = IP_VS_PROTO_HASH(pp->protocol);

	pp_p = &ip_vs_proto_table[hash];
	for (; *pp_p; pp_p = &(*pp_p)->next) {
		if (*pp_p == pp) {
			*pp_p = pp->next;
			if (pp->exit != NULL)
				pp->exit(pp);
			return 0;
		}
	}

	return -ESRCH;
}

/*
 *	unregister an ipvs protocols netns data
 */
static int
unregister_ip_vs_proto_netns(struct net *net, struct ip_vs_proto_data *pd)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_proto_data **pd_p;
	unsigned hash = IP_VS_PROTO_HASH(pd->pp->protocol);

	pd_p = &ipvs->proto_data_table[hash];
	for (; *pd_p; pd_p = &(*pd_p)->next) {
		if (*pd_p == pd) {
			*pd_p = pd->next;
			if (pd->pp->exit_netns != NULL)
				pd->pp->exit_netns(net, pd);
			kfree(pd);
			return 0;
		}
	}

	return -ESRCH;
}

/*
 *	get ip_vs_protocol object by its proto.
 */
struct ip_vs_protocol * ip_vs_proto_get(unsigned short proto)
{
	struct ip_vs_protocol *pp;
	unsigned hash = IP_VS_PROTO_HASH(proto);

	for (pp = ip_vs_proto_table[hash]; pp; pp = pp->next) {
		if (pp->protocol == proto)
			return pp;
	}

	return NULL;
}
EXPORT_SYMBOL(ip_vs_proto_get);

/*
 *	get ip_vs_protocol object data by netns and proto
 */
struct ip_vs_proto_data *
__ipvs_proto_data_get(struct netns_ipvs *ipvs, unsigned short proto)
{
	struct ip_vs_proto_data *pd;
	unsigned hash = IP_VS_PROTO_HASH(proto);

	for (pd = ipvs->proto_data_table[hash]; pd; pd = pd->next) {
		if (pd->pp->protocol == proto)
			return pd;
	}

	return NULL;
}

struct ip_vs_proto_data *
ip_vs_proto_data_get(struct net *net, unsigned short proto)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	return __ipvs_proto_data_get(ipvs, proto);
}
EXPORT_SYMBOL(ip_vs_proto_data_get);

/*
 *	Propagate event for state change to all protocols
 */
void ip_vs_protocol_timeout_change(struct netns_ipvs *ipvs, int flags)
{
	struct ip_vs_proto_data *pd;
	int i;

	for (i = 0; i < IP_VS_PROTO_TAB_SIZE; i++) {
		for (pd = ipvs->proto_data_table[i]; pd; pd = pd->next) {
			if (pd->pp->timeout_change)
				pd->pp->timeout_change(pd, flags);
		}
	}
}


int *
ip_vs_create_timeout_table(int *table, int size)
{
	return kmemdup(table, size, GFP_ATOMIC);
}


/*
 *	Set timeout value for state specified by name
 */
int
ip_vs_set_state_timeout(int *table, int num, const char *const *names,
			const char *name, int to)
{
	int i;

	if (!table || !name || !to)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		if (strcmp(names[i], name))
			continue;
		table[i] = to * HZ;
		return 0;
	}
	return -ENOENT;
}


const char * ip_vs_state_name(__u16 proto, int state)
{
	struct ip_vs_protocol *pp = ip_vs_proto_get(proto);

	if (pp == NULL || pp->state_name == NULL)
		return (IPPROTO_IP == proto) ? "NONE" : "ERR!";
	return pp->state_name(state);
}


static void
ip_vs_tcpudp_debug_packet_v4(struct ip_vs_protocol *pp,
			     const struct sk_buff *skb,
			     int offset,
			     const char *msg)
{
	char buf[128];
	struct iphdr _iph, *ih;

	ih = skb_header_pointer(skb, offset, sizeof(_iph), &_iph);
	if (ih == NULL)
		sprintf(buf, "TRUNCATED");
	else if (ih->frag_off & htons(IP_OFFSET))
		sprintf(buf, "%pI4->%pI4 frag", &ih->saddr, &ih->daddr);
	else {
		__be16 _ports[2], *pptr;

		pptr = skb_header_pointer(skb, offset + ih->ihl*4,
					  sizeof(_ports), _ports);
		if (pptr == NULL)
			sprintf(buf, "TRUNCATED %pI4->%pI4",
				&ih->saddr, &ih->daddr);
		else
			sprintf(buf, "%pI4:%u->%pI4:%u",
				&ih->saddr, ntohs(pptr[0]),
				&ih->daddr, ntohs(pptr[1]));
	}

	pr_debug("%s: %s %s\n", msg, pp->name, buf);
}

#ifdef CONFIG_IP_VS_IPV6
static void
ip_vs_tcpudp_debug_packet_v6(struct ip_vs_protocol *pp,
			     const struct sk_buff *skb,
			     int offset,
			     const char *msg)
{
	char buf[192];
	struct ipv6hdr _iph, *ih;

	ih = skb_header_pointer(skb, offset, sizeof(_iph), &_iph);
	if (ih == NULL)
		sprintf(buf, "TRUNCATED");
	else if (ih->nexthdr == IPPROTO_FRAGMENT)
		sprintf(buf, "%pI6->%pI6 frag",	&ih->saddr, &ih->daddr);
	else {
		__be16 _ports[2], *pptr;

		pptr = skb_header_pointer(skb, offset + sizeof(struct ipv6hdr),
					  sizeof(_ports), _ports);
		if (pptr == NULL)
			sprintf(buf, "TRUNCATED %pI6->%pI6",
				&ih->saddr, &ih->daddr);
		else
			sprintf(buf, "%pI6:%u->%pI6:%u",
				&ih->saddr, ntohs(pptr[0]),
				&ih->daddr, ntohs(pptr[1]));
	}

	pr_debug("%s: %s %s\n", msg, pp->name, buf);
}
#endif


void
ip_vs_tcpudp_debug_packet(int af, struct ip_vs_protocol *pp,
			  const struct sk_buff *skb,
			  int offset,
			  const char *msg)
{
#ifdef CONFIG_IP_VS_IPV6
	if (af == AF_INET6)
		ip_vs_tcpudp_debug_packet_v6(pp, skb, offset, msg);
	else
#endif
		ip_vs_tcpudp_debug_packet_v4(pp, skb, offset, msg);
}

/*
 * per network name-space init
 */
int __net_init ip_vs_protocol_net_init(struct net *net)
{
	int i, ret;
	static struct ip_vs_protocol *protos[] = {
#ifdef CONFIG_IP_VS_PROTO_TCP
        &ip_vs_protocol_tcp,
#endif
#ifdef CONFIG_IP_VS_PROTO_UDP
	&ip_vs_protocol_udp,
#endif
#ifdef CONFIG_IP_VS_PROTO_SCTP
	&ip_vs_protocol_sctp,
#endif
#ifdef CONFIG_IP_VS_PROTO_AH
	&ip_vs_protocol_ah,
#endif
#ifdef CONFIG_IP_VS_PROTO_ESP
	&ip_vs_protocol_esp,
#endif
	};

	for (i = 0; i < ARRAY_SIZE(protos); i++) {
		ret = register_ip_vs_proto_netns(net, protos[i]);
		if (ret < 0)
			goto cleanup;
	}
	return 0;

cleanup:
	ip_vs_protocol_net_cleanup(net);
	return ret;
}

void __net_exit ip_vs_protocol_net_cleanup(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);
	struct ip_vs_proto_data *pd;
	int i;

	/* unregister all the ipvs proto data for this netns */
	for (i = 0; i < IP_VS_PROTO_TAB_SIZE; i++) {
		while ((pd = ipvs->proto_data_table[i]) != NULL)
			unregister_ip_vs_proto_netns(net, pd);
	}
}

int __init ip_vs_protocol_init(void)
{
	char protocols[64];
#define REGISTER_PROTOCOL(p)			\
	do {					\
		register_ip_vs_protocol(p);	\
		strcat(protocols, ", ");	\
		strcat(protocols, (p)->name);	\
	} while (0)

	protocols[0] = '\0';
	protocols[2] = '\0';
#ifdef CONFIG_IP_VS_PROTO_TCP
	REGISTER_PROTOCOL(&ip_vs_protocol_tcp);
#endif
#ifdef CONFIG_IP_VS_PROTO_UDP
	REGISTER_PROTOCOL(&ip_vs_protocol_udp);
#endif
#ifdef CONFIG_IP_VS_PROTO_SCTP
	REGISTER_PROTOCOL(&ip_vs_protocol_sctp);
#endif
#ifdef CONFIG_IP_VS_PROTO_AH
	REGISTER_PROTOCOL(&ip_vs_protocol_ah);
#endif
#ifdef CONFIG_IP_VS_PROTO_ESP
	REGISTER_PROTOCOL(&ip_vs_protocol_esp);
#endif
	pr_info("Registered protocols (%s)\n", &protocols[2]);

	return 0;
}


void ip_vs_protocol_cleanup(void)
{
	struct ip_vs_protocol *pp;
	int i;

	/* unregister all the ipvs protocols */
	for (i = 0; i < IP_VS_PROTO_TAB_SIZE; i++) {
		while ((pp = ip_vs_proto_table[i]) != NULL)
			unregister_ip_vs_protocol(pp);
	}
}
