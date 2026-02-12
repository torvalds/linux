/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _TUNTAP_HELPERS_H
#define _TUNTAP_HELPERS_H

#include <errno.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/virtio_net.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ynl.h>

#include "rt-route-user.h"
#include "rt-addr-user.h"
#include "rt-neigh-user.h"
#include "rt-link-user.h"

#define GENEVE_HLEN 8
#define PKT_DATA 0xCB
#define TUNTAP_DEFAULT_TTL 8
#define TUNTAP_DEFAULT_IPID 1337

unsigned int if_nametoindex(const char *ifname);

static inline int ip_addr_len(int family)
{
	return (family == AF_INET) ? sizeof(struct in_addr) :
				     sizeof(struct in6_addr);
}

static inline void fill_ifaddr_msg(struct ifaddrmsg *ifam, int family,
				   int prefix, int flags, const char *dev)
{
	ifam->ifa_family = family;
	ifam->ifa_prefixlen = prefix;
	ifam->ifa_index = if_nametoindex(dev);
	ifam->ifa_flags = flags;
	ifam->ifa_scope = RT_SCOPE_UNIVERSE;
}

static inline int ip_addr_add(const char *dev, int family, void *addr,
			      uint8_t prefix)
{
	int nl_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	int ifa_flags = IFA_F_PERMANENT | IFA_F_NODAD;
	int ret = -1, ipalen = ip_addr_len(family);
	struct rt_addr_newaddr_req *req;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_rt_addr_family, NULL);
	if (!ys)
		return -1;

	req = rt_addr_newaddr_req_alloc();
	if (!req)
		goto err_req_alloc;

	fill_ifaddr_msg(&req->_hdr, family, prefix, ifa_flags, dev);
	rt_addr_newaddr_req_set_nlflags(req, nl_flags);
	rt_addr_newaddr_req_set_local(req, addr, ipalen);

	ret = rt_addr_newaddr(ys, req);
	rt_addr_newaddr_req_free(req);
err_req_alloc:
	ynl_sock_destroy(ys);
	return ret;
}

static inline void fill_neigh_req_header(struct ndmsg *ndm, int family,
					 int state, const char *dev)
{
	ndm->ndm_family = family;
	ndm->ndm_ifindex = if_nametoindex(dev);
	ndm->ndm_state = state;
	ndm->ndm_flags = 0;
	ndm->ndm_type = RTN_UNICAST;
}

static inline int ip_neigh_add(const char *dev, int family, void *addr,
			       unsigned char *lladdr)
{
	int nl_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	int ret = -1, ipalen = ip_addr_len(family);
	struct rt_neigh_newneigh_req *req;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_rt_neigh_family, NULL);
	if (!ys)
		return -1;

	req = rt_neigh_newneigh_req_alloc();
	if (!req)
		goto err_req_alloc;

	fill_neigh_req_header(&req->_hdr, family, NUD_PERMANENT, dev);
	rt_neigh_newneigh_req_set_nlflags(req, nl_flags);
	rt_neigh_newneigh_req_set_dst(req, addr, ipalen);
	rt_neigh_newneigh_req_set_lladdr(req, lladdr, ETH_ALEN);
	rt_neigh_newneigh_req_set_ifindex(req, if_nametoindex(dev));

	ret = rt_neigh_newneigh(ys, req);
	rt_neigh_newneigh_req_free(req);
err_req_alloc:
	ynl_sock_destroy(ys);
	return ret;
}

static inline void fill_route_req_header(struct rtmsg *rtm, int family,
					 int table)
{
	rtm->rtm_family = family;
	rtm->rtm_table = table;
}

static inline int
ip_route_get(const char *dev, int family, int table, void *dst,
	     void (*parse_rsp)(struct rt_route_getroute_rsp *rsp, void *out),
	     void *out)
{
	int ret = -1, ipalen = ip_addr_len(family);
	struct rt_route_getroute_req *req;
	struct rt_route_getroute_rsp *rsp;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_rt_route_family, NULL);
	if (!ys)
		return -1;

	req = rt_route_getroute_req_alloc();
	if (!req)
		goto err_req_alloc;

	fill_route_req_header(&req->_hdr, family, table);
	rt_route_getroute_req_set_nlflags(req, NLM_F_REQUEST);
	rt_route_getroute_req_set_dst(req, dst, ipalen);
	rt_route_getroute_req_set_oif(req, if_nametoindex(dev));

	rsp = rt_route_getroute(ys, req);
	if (!rsp)
		goto err_rsp_get;

	ret = 0;
	if (parse_rsp)
		parse_rsp(rsp, out);

	rt_route_getroute_rsp_free(rsp);
err_rsp_get:
	rt_route_getroute_req_free(req);
err_req_alloc:
	ynl_sock_destroy(ys);
	return ret;
}

static inline int
ip_link_add(const char *dev, char *link_type,
	    int (*fill_link_attr)(struct rt_link_newlink_req *req, void *data),
	    void *data)
{
	int nl_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	struct rt_link_newlink_req *req;
	struct ynl_sock *ys;
	int ret = -1;

	ys = ynl_sock_create(&ynl_rt_link_family, NULL);
	if (!ys)
		return -1;

	req = rt_link_newlink_req_alloc();
	if (!req)
		goto err_req_alloc;

	req->_hdr.ifi_flags = IFF_UP;
	rt_link_newlink_req_set_nlflags(req, nl_flags);
	rt_link_newlink_req_set_ifname(req, dev);
	rt_link_newlink_req_set_linkinfo_kind(req, link_type);

	if (fill_link_attr && fill_link_attr(req, data) < 0)
		goto err_attr_fill;

	ret = rt_link_newlink(ys, req);
err_attr_fill:
	rt_link_newlink_req_free(req);
err_req_alloc:
	ynl_sock_destroy(ys);
	return ret;
}

static inline int ip_link_del(const char *dev)
{
	struct rt_link_dellink_req *req;
	struct ynl_sock *ys;
	int ret = -1;

	ys = ynl_sock_create(&ynl_rt_link_family, NULL);
	if (!ys)
		return -1;

	req = rt_link_dellink_req_alloc();
	if (!req)
		goto err_req_alloc;

	rt_link_dellink_req_set_nlflags(req, NLM_F_REQUEST);
	rt_link_dellink_req_set_ifname(req, dev);

	ret = rt_link_dellink(ys, req);
	rt_link_dellink_req_free(req);
err_req_alloc:
	ynl_sock_destroy(ys);
	return ret;
}

static inline size_t build_eth(uint8_t *buf, uint16_t proto, unsigned char *src,
			       unsigned char *dest)
{
	struct ethhdr *eth = (struct ethhdr *)buf;

	eth->h_proto = htons(proto);
	memcpy(eth->h_source, src, ETH_ALEN);
	memcpy(eth->h_dest, dest, ETH_ALEN);

	return ETH_HLEN;
}

static inline uint32_t add_csum(const uint8_t *buf, int len)
{
	uint16_t *sbuf = (uint16_t *)buf;
	uint32_t sum = 0;

	while (len > 1) {
		sum += *sbuf++;
		len -= 2;
	}

	if (len)
		sum += *(uint8_t *)sbuf;

	return sum;
}

static inline uint16_t finish_ip_csum(uint32_t sum)
{
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return ~((uint16_t)sum);
}

static inline uint16_t build_ip_csum(const uint8_t *buf, int len, uint32_t sum)
{
	sum += add_csum(buf, len);
	return finish_ip_csum(sum);
}

static inline int build_ipv4_header(uint8_t *buf, uint8_t proto,
				    int payload_len, struct in_addr *src,
				    struct in_addr *dst)
{
	struct iphdr *iph = (struct iphdr *)buf;

	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = TUNTAP_DEFAULT_TTL;
	iph->tot_len = htons(sizeof(*iph) + payload_len);
	iph->id = htons(TUNTAP_DEFAULT_IPID);
	iph->protocol = proto;
	iph->saddr = src->s_addr;
	iph->daddr = dst->s_addr;
	iph->check = build_ip_csum(buf, iph->ihl << 2, 0);

	return iph->ihl << 2;
}

static inline void ipv6_set_dsfield(struct ipv6hdr *ip6h, uint8_t dsfield)
{
	uint16_t val, *ptr = (uint16_t *)ip6h;

	val = ntohs(*ptr);
	val &= 0xF00F;
	val |= ((uint16_t)dsfield) << 4;
	*ptr = htons(val);
}

static inline int build_ipv6_header(uint8_t *buf, uint8_t proto,
				    uint8_t dsfield, int payload_len,
				    struct in6_addr *src, struct in6_addr *dst)
{
	struct ipv6hdr *ip6h = (struct ipv6hdr *)buf;

	ip6h->version = 6;
	ip6h->payload_len = htons(payload_len);
	ip6h->nexthdr = proto;
	ip6h->hop_limit = TUNTAP_DEFAULT_TTL;
	ipv6_set_dsfield(ip6h, dsfield);
	memcpy(&ip6h->saddr, src, sizeof(ip6h->saddr));
	memcpy(&ip6h->daddr, dst, sizeof(ip6h->daddr));

	return sizeof(struct ipv6hdr);
}

static inline int build_geneve_header(uint8_t *buf, uint32_t vni)
{
	uint16_t protocol = htons(ETH_P_TEB);
	uint32_t geneve_vni = htonl((vni << 8) & 0xffffff00);

	memcpy(buf + 2, &protocol, 2);
	memcpy(buf + 4, &geneve_vni, 4);
	return GENEVE_HLEN;
}

static inline int build_udp_header(uint8_t *buf, uint16_t sport, uint16_t dport,
				   int payload_len)
{
	struct udphdr *udph = (struct udphdr *)buf;

	udph->source = htons(sport);
	udph->dest = htons(dport);
	udph->len = htons(sizeof(*udph) + payload_len);
	return sizeof(*udph);
}

static inline void build_udp_packet_csum(uint8_t *buf, int family,
					 bool csum_off)
{
	struct udphdr *udph = (struct udphdr *)buf;
	size_t ipalen = ip_addr_len(family);
	uint32_t sum;

	/* No extension IPv4 and IPv6 headers addresses are the last fields */
	sum = add_csum(buf - 2 * ipalen, 2 * ipalen);
	sum += htons(IPPROTO_UDP) + udph->len;

	if (!csum_off)
		sum += add_csum(buf, udph->len);

	udph->check = finish_ip_csum(sum);
}

static inline int build_udp_packet(uint8_t *buf, uint16_t sport, uint16_t dport,
				   int payload_len, int family, bool csum_off)
{
	struct udphdr *udph = (struct udphdr *)buf;

	build_udp_header(buf, sport, dport, payload_len);
	memset(buf + sizeof(*udph), PKT_DATA, payload_len);
	build_udp_packet_csum(buf, family, csum_off);

	return sizeof(*udph) + payload_len;
}

static inline int build_virtio_net_hdr_v1_hash_tunnel(uint8_t *buf, bool is_tap,
						      int hdr_len, int gso_size,
						      int outer_family,
						      int inner_family)
{
	struct virtio_net_hdr_v1_hash_tunnel *vh_tunnel = (void *)buf;
	struct virtio_net_hdr_v1 *vh = &vh_tunnel->hash_hdr.hdr;
	int outer_iphlen, inner_iphlen, eth_hlen, gso_type;

	eth_hlen = is_tap ? ETH_HLEN : 0;
	outer_iphlen = (outer_family == AF_INET) ? sizeof(struct iphdr) :
						   sizeof(struct ipv6hdr);
	inner_iphlen = (inner_family == AF_INET) ? sizeof(struct iphdr) :
						   sizeof(struct ipv6hdr);

	vh_tunnel->outer_th_offset = eth_hlen + outer_iphlen;
	vh_tunnel->inner_nh_offset = vh_tunnel->outer_th_offset + ETH_HLEN +
				     GENEVE_HLEN + sizeof(struct udphdr);

	vh->csum_start = vh_tunnel->inner_nh_offset + inner_iphlen;
	vh->csum_offset = __builtin_offsetof(struct udphdr, check);
	vh->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
	vh->hdr_len = hdr_len;
	vh->gso_size = gso_size;

	if (gso_size) {
		gso_type = outer_family == AF_INET ?
				   VIRTIO_NET_HDR_GSO_UDP_TUNNEL_IPV4 :
				   VIRTIO_NET_HDR_GSO_UDP_TUNNEL_IPV6;
		vh->gso_type = VIRTIO_NET_HDR_GSO_UDP_L4 | gso_type;
	}

	return sizeof(struct virtio_net_hdr_v1_hash_tunnel);
}

#endif /* _TUNTAP_HELPERS_H */
