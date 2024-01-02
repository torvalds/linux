// SPDX-License-Identifier: GPL-2.0
/* Original from tools/testing/selftests/net/ipsec.c */
#include <linux/netlink.h>
#include <linux/random.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include "aolib.h"

#define MAX_PAYLOAD		2048

static int netlink_sock(int *sock, uint32_t *seq_nr, int proto)
{
	if (*sock > 0) {
		seq_nr++;
		return 0;
	}

	*sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, proto);
	if (*sock < 0) {
		test_print("socket(AF_NETLINK)");
		return -1;
	}

	randomize_buffer(seq_nr, sizeof(*seq_nr));

	return 0;
}

static int netlink_check_answer(int sock, bool quite)
{
	struct nlmsgerror {
		struct nlmsghdr hdr;
		int error;
		struct nlmsghdr orig_msg;
	} answer;

	if (recv(sock, &answer, sizeof(answer), 0) < 0) {
		test_print("recv()");
		return -1;
	} else if (answer.hdr.nlmsg_type != NLMSG_ERROR) {
		test_print("expected NLMSG_ERROR, got %d",
			   (int)answer.hdr.nlmsg_type);
		return -1;
	} else if (answer.error) {
		if (!quite) {
			test_print("NLMSG_ERROR: %d: %s",
				answer.error, strerror(-answer.error));
		}
		return answer.error;
	}

	return 0;
}

static inline struct rtattr *rtattr_hdr(struct nlmsghdr *nh)
{
	return (struct rtattr *)((char *)(nh) + RTA_ALIGN((nh)->nlmsg_len));
}

static int rtattr_pack(struct nlmsghdr *nh, size_t req_sz,
		unsigned short rta_type, const void *payload, size_t size)
{
	/* NLMSG_ALIGNTO == RTA_ALIGNTO, nlmsg_len already aligned */
	struct rtattr *attr = rtattr_hdr(nh);
	size_t nl_size = RTA_ALIGN(nh->nlmsg_len) + RTA_LENGTH(size);

	if (req_sz < nl_size) {
		test_print("req buf is too small: %zu < %zu", req_sz, nl_size);
		return -1;
	}
	nh->nlmsg_len = nl_size;

	attr->rta_len = RTA_LENGTH(size);
	attr->rta_type = rta_type;
	memcpy(RTA_DATA(attr), payload, size);

	return 0;
}

static struct rtattr *_rtattr_begin(struct nlmsghdr *nh, size_t req_sz,
		unsigned short rta_type, const void *payload, size_t size)
{
	struct rtattr *ret = rtattr_hdr(nh);

	if (rtattr_pack(nh, req_sz, rta_type, payload, size))
		return 0;

	return ret;
}

static inline struct rtattr *rtattr_begin(struct nlmsghdr *nh, size_t req_sz,
		unsigned short rta_type)
{
	return _rtattr_begin(nh, req_sz, rta_type, 0, 0);
}

static inline void rtattr_end(struct nlmsghdr *nh, struct rtattr *attr)
{
	char *nlmsg_end = (char *)nh + nh->nlmsg_len;

	attr->rta_len = nlmsg_end - (char *)attr;
}

static int veth_pack_peerb(struct nlmsghdr *nh, size_t req_sz,
		const char *peer, int ns)
{
	struct ifinfomsg pi;
	struct rtattr *peer_attr;

	memset(&pi, 0, sizeof(pi));
	pi.ifi_family	= AF_UNSPEC;
	pi.ifi_change	= 0xFFFFFFFF;

	peer_attr = _rtattr_begin(nh, req_sz, VETH_INFO_PEER, &pi, sizeof(pi));
	if (!peer_attr)
		return -1;

	if (rtattr_pack(nh, req_sz, IFLA_IFNAME, peer, strlen(peer)))
		return -1;

	if (rtattr_pack(nh, req_sz, IFLA_NET_NS_FD, &ns, sizeof(ns)))
		return -1;

	rtattr_end(nh, peer_attr);

	return 0;
}

static int __add_veth(int sock, uint32_t seq, const char *name,
		      int ns_a, int ns_b)
{
	uint16_t flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
	struct {
		struct nlmsghdr		nh;
		struct ifinfomsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;
	static const char veth_type[] = "veth";
	struct rtattr *link_info, *info_data;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWLINK;
	req.nh.nlmsg_flags	= flags;
	req.nh.nlmsg_seq	= seq;
	req.info.ifi_family	= AF_UNSPEC;
	req.info.ifi_change	= 0xFFFFFFFF;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_IFNAME, name, strlen(name)))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_NET_NS_FD, &ns_a, sizeof(ns_a)))
		return -1;

	link_info = rtattr_begin(&req.nh, sizeof(req), IFLA_LINKINFO);
	if (!link_info)
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_INFO_KIND, veth_type, sizeof(veth_type)))
		return -1;

	info_data = rtattr_begin(&req.nh, sizeof(req), IFLA_INFO_DATA);
	if (!info_data)
		return -1;

	if (veth_pack_peerb(&req.nh, sizeof(req), name, ns_b))
		return -1;

	rtattr_end(&req.nh, info_data);
	rtattr_end(&req.nh, link_info);

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		test_print("send()");
		return -1;
	}
	return netlink_check_answer(sock, false);
}

int add_veth(const char *name, int nsfda, int nsfdb)
{
	int route_sock = -1, ret;
	uint32_t route_seq;

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE))
		test_error("Failed to open netlink route socket\n");

	ret = __add_veth(route_sock, route_seq++, name, nsfda, nsfdb);
	close(route_sock);
	return ret;
}

static int __ip_addr_add(int sock, uint32_t seq, const char *intf,
			 int family, union tcp_addr addr, uint8_t prefix)
{
	uint16_t flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
	struct {
		struct nlmsghdr		nh;
		struct ifaddrmsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;
	size_t addr_len = (family == AF_INET) ? sizeof(struct in_addr) :
						sizeof(struct in6_addr);

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWADDR;
	req.nh.nlmsg_flags	= flags;
	req.nh.nlmsg_seq	= seq;
	req.info.ifa_family	= family;
	req.info.ifa_prefixlen	= prefix;
	req.info.ifa_index	= if_nametoindex(intf);
	req.info.ifa_flags	= IFA_F_NODAD;

	if (rtattr_pack(&req.nh, sizeof(req), IFA_LOCAL, &addr, addr_len))
		return -1;

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		test_print("send()");
		return -1;
	}
	return netlink_check_answer(sock, true);
}

int ip_addr_add(const char *intf, int family,
		union tcp_addr addr, uint8_t prefix)
{
	int route_sock = -1, ret;
	uint32_t route_seq;

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE))
		test_error("Failed to open netlink route socket\n");

	ret = __ip_addr_add(route_sock, route_seq++, intf,
			    family, addr, prefix);

	close(route_sock);
	return ret;
}

static int __ip_route_add(int sock, uint32_t seq, const char *intf, int family,
			  union tcp_addr src, union tcp_addr dst, uint8_t vrf)
{
	struct {
		struct nlmsghdr	nh;
		struct rtmsg	rt;
		char		attrbuf[MAX_PAYLOAD];
	} req;
	unsigned int index = if_nametoindex(intf);
	size_t addr_len = (family == AF_INET) ? sizeof(struct in_addr) :
						sizeof(struct in6_addr);

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.rt));
	req.nh.nlmsg_type	= RTM_NEWROUTE;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE;
	req.nh.nlmsg_seq	= seq;
	req.rt.rtm_family	= family;
	req.rt.rtm_dst_len	= (family == AF_INET) ? 32 : 128;
	req.rt.rtm_table	= vrf;
	req.rt.rtm_protocol	= RTPROT_BOOT;
	req.rt.rtm_scope	= RT_SCOPE_UNIVERSE;
	req.rt.rtm_type		= RTN_UNICAST;

	if (rtattr_pack(&req.nh, sizeof(req), RTA_DST, &dst, addr_len))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), RTA_PREFSRC, &src, addr_len))
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), RTA_OIF, &index, sizeof(index)))
		return -1;

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		test_print("send()");
		return -1;
	}

	return netlink_check_answer(sock, true);
}

int ip_route_add_vrf(const char *intf, int family,
		 union tcp_addr src, union tcp_addr dst, uint8_t vrf)
{
	int route_sock = -1, ret;
	uint32_t route_seq;

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE))
		test_error("Failed to open netlink route socket\n");

	ret = __ip_route_add(route_sock, route_seq++, intf,
			     family, src, dst, vrf);

	close(route_sock);
	return ret;
}

int ip_route_add(const char *intf, int family,
		 union tcp_addr src, union tcp_addr dst)
{
	return ip_route_add_vrf(intf, family, src, dst, RT_TABLE_MAIN);
}

static int __link_set_up(int sock, uint32_t seq, const char *intf)
{
	struct {
		struct nlmsghdr		nh;
		struct ifinfomsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWLINK;
	req.nh.nlmsg_flags	= NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_seq	= seq;
	req.info.ifi_family	= AF_UNSPEC;
	req.info.ifi_change	= 0xFFFFFFFF;
	req.info.ifi_index	= if_nametoindex(intf);
	req.info.ifi_flags	= IFF_UP;
	req.info.ifi_change	= IFF_UP;

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		test_print("send()");
		return -1;
	}
	return netlink_check_answer(sock, false);
}

int link_set_up(const char *intf)
{
	int route_sock = -1, ret;
	uint32_t route_seq;

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE))
		test_error("Failed to open netlink route socket\n");

	ret = __link_set_up(route_sock, route_seq++, intf);

	close(route_sock);
	return ret;
}

static int __add_vrf(int sock, uint32_t seq, const char *name,
		     uint32_t tabid, int ifindex, int nsfd)
{
	uint16_t flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_EXCL | NLM_F_CREATE;
	struct {
		struct nlmsghdr		nh;
		struct ifinfomsg	info;
		char			attrbuf[MAX_PAYLOAD];
	} req;
	static const char vrf_type[] = "vrf";
	struct rtattr *link_info, *info_data;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len	= NLMSG_LENGTH(sizeof(req.info));
	req.nh.nlmsg_type	= RTM_NEWLINK;
	req.nh.nlmsg_flags	= flags;
	req.nh.nlmsg_seq	= seq;
	req.info.ifi_family	= AF_UNSPEC;
	req.info.ifi_change	= 0xFFFFFFFF;
	req.info.ifi_index	= ifindex;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_IFNAME, name, strlen(name)))
		return -1;

	if (nsfd >= 0)
		if (rtattr_pack(&req.nh, sizeof(req), IFLA_NET_NS_FD,
				&nsfd, sizeof(nsfd)))
			return -1;

	link_info = rtattr_begin(&req.nh, sizeof(req), IFLA_LINKINFO);
	if (!link_info)
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_INFO_KIND, vrf_type, sizeof(vrf_type)))
		return -1;

	info_data = rtattr_begin(&req.nh, sizeof(req), IFLA_INFO_DATA);
	if (!info_data)
		return -1;

	if (rtattr_pack(&req.nh, sizeof(req), IFLA_VRF_TABLE,
			&tabid, sizeof(tabid)))
		return -1;

	rtattr_end(&req.nh, info_data);
	rtattr_end(&req.nh, link_info);

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		test_print("send()");
		return -1;
	}
	return netlink_check_answer(sock, true);
}

int add_vrf(const char *name, uint32_t tabid, int ifindex, int nsfd)
{
	int route_sock = -1, ret;
	uint32_t route_seq;

	if (netlink_sock(&route_sock, &route_seq, NETLINK_ROUTE))
		test_error("Failed to open netlink route socket\n");

	ret = __add_vrf(route_sock, route_seq++, name, tabid, ifindex, nsfd);
	close(route_sock);
	return ret;
}
