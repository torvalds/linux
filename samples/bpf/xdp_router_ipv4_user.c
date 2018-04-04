/* Copyright (C) 2017 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "bpf_load.h"
#include "libbpf.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include "bpf_util.h"

int sock, sock_arp, flags = 0;
static int total_ifindex;
int *ifindex_list;
char buf[8192];

static int get_route_table(int rtm_family);
static void int_exit(int sig)
{
	int i = 0;

	for (i = 0; i < total_ifindex; i++)
		bpf_set_link_xdp_fd(ifindex_list[i], -1, flags);
	exit(0);
}

static void close_and_exit(int sig)
{
	int i = 0;

	close(sock);
	close(sock_arp);

	for (i = 0; i < total_ifindex; i++)
		bpf_set_link_xdp_fd(ifindex_list[i], -1, flags);
	exit(0);
}

/* Get the mac address of the interface given interface name */
static __be64 getmac(char *iface)
{
	struct ifreq ifr;
	__be64 mac = 0;
	int fd, i;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
		printf("ioctl failed leaving....\n");
		return -1;
	}
	for (i = 0; i < 6 ; i++)
		*((__u8 *)&mac + i) = (__u8)ifr.ifr_hwaddr.sa_data[i];
	close(fd);
	return mac;
}

static int recv_msg(struct sockaddr_nl sock_addr, int sock)
{
	struct nlmsghdr *nh;
	int len, nll = 0;
	char *buf_ptr;

	buf_ptr = buf;
	while (1) {
		len = recv(sock, buf_ptr, sizeof(buf) - nll, 0);
		if (len < 0)
			return len;

		nh = (struct nlmsghdr *)buf_ptr;

		if (nh->nlmsg_type == NLMSG_DONE)
			break;
		buf_ptr += len;
		nll += len;
		if ((sock_addr.nl_groups & RTMGRP_NEIGH) == RTMGRP_NEIGH)
			break;

		if ((sock_addr.nl_groups & RTMGRP_IPV4_ROUTE) == RTMGRP_IPV4_ROUTE)
			break;
	}
	return nll;
}

/* Function to parse the route entry returned by netlink
 * Updates the route entry related map entries
 */
static void read_route(struct nlmsghdr *nh, int nll)
{
	char dsts[24], gws[24], ifs[16], dsts_len[24], metrics[24];
	struct bpf_lpm_trie_key *prefix_key;
	struct rtattr *rt_attr;
	struct rtmsg *rt_msg;
	int rtm_family;
	int rtl;
	int i;
	struct route_table {
		int  dst_len, iface, metric;
		char *iface_name;
		__be32 dst, gw;
		__be64 mac;
	} route;
	struct arp_table {
		__be64 mac;
		__be32 dst;
	};

	struct direct_map {
		struct arp_table arp;
		int ifindex;
		__be64 mac;
	} direct_entry;

	if (nh->nlmsg_type == RTM_DELROUTE)
		printf("DELETING Route entry\n");
	else if (nh->nlmsg_type == RTM_GETROUTE)
		printf("READING Route entry\n");
	else if (nh->nlmsg_type == RTM_NEWROUTE)
		printf("NEW Route entry\n");
	else
		printf("%d\n", nh->nlmsg_type);

	memset(&route, 0, sizeof(route));
	printf("Destination\t\tGateway\t\tGenmask\t\tMetric\t\tIface\n");
	for (; NLMSG_OK(nh, nll); nh = NLMSG_NEXT(nh, nll)) {
		rt_msg = (struct rtmsg *)NLMSG_DATA(nh);
		rtm_family = rt_msg->rtm_family;
		if (rtm_family == AF_INET)
			if (rt_msg->rtm_table != RT_TABLE_MAIN)
				continue;
		rt_attr = (struct rtattr *)RTM_RTA(rt_msg);
		rtl = RTM_PAYLOAD(nh);

		for (; RTA_OK(rt_attr, rtl); rt_attr = RTA_NEXT(rt_attr, rtl)) {
			switch (rt_attr->rta_type) {
			case NDA_DST:
				sprintf(dsts, "%u",
					(*((__be32 *)RTA_DATA(rt_attr))));
				break;
			case RTA_GATEWAY:
				sprintf(gws, "%u",
					*((__be32 *)RTA_DATA(rt_attr)));
				break;
			case RTA_OIF:
				sprintf(ifs, "%u",
					*((int *)RTA_DATA(rt_attr)));
				break;
			case RTA_METRICS:
				sprintf(metrics, "%u",
					*((int *)RTA_DATA(rt_attr)));
			default:
				break;
			}
		}
		sprintf(dsts_len, "%d", rt_msg->rtm_dst_len);
		route.dst = atoi(dsts);
		route.dst_len = atoi(dsts_len);
		route.gw = atoi(gws);
		route.iface = atoi(ifs);
		route.metric = atoi(metrics);
		route.iface_name = alloca(sizeof(char *) * IFNAMSIZ);
		route.iface_name = if_indextoname(route.iface, route.iface_name);
		route.mac = getmac(route.iface_name);
		if (route.mac == -1) {
			int i = 0;

			for (i = 0; i < total_ifindex; i++)
				bpf_set_link_xdp_fd(ifindex_list[i], -1, flags);
			exit(0);
		}
		assert(bpf_map_update_elem(map_fd[4], &route.iface, &route.iface, 0) == 0);
		if (rtm_family == AF_INET) {
			struct trie_value {
				__u8 prefix[4];
				__be64 value;
				int ifindex;
				int metric;
				__be32 gw;
			} *prefix_value;

			prefix_key = alloca(sizeof(*prefix_key) + 3);
			prefix_value = alloca(sizeof(*prefix_value));

			prefix_key->prefixlen = 32;
			prefix_key->prefixlen = route.dst_len;
			direct_entry.mac = route.mac & 0xffffffffffff;
			direct_entry.ifindex = route.iface;
			direct_entry.arp.mac = 0;
			direct_entry.arp.dst = 0;
			if (route.dst_len == 32) {
				if (nh->nlmsg_type == RTM_DELROUTE) {
					assert(bpf_map_delete_elem(map_fd[3], &route.dst) == 0);
				} else {
					if (bpf_map_lookup_elem(map_fd[2], &route.dst, &direct_entry.arp.mac) == 0)
						direct_entry.arp.dst = route.dst;
					assert(bpf_map_update_elem(map_fd[3], &route.dst, &direct_entry, 0) == 0);
				}
			}
			for (i = 0; i < 4; i++)
				prefix_key->data[i] = (route.dst >> i * 8) & 0xff;

			printf("%3d.%d.%d.%d\t\t%3x\t\t%d\t\t%d\t\t%s\n",
			       (int)prefix_key->data[0],
			       (int)prefix_key->data[1],
			       (int)prefix_key->data[2],
			       (int)prefix_key->data[3],
			       route.gw, route.dst_len,
			       route.metric,
			       route.iface_name);
			if (bpf_map_lookup_elem(map_fd[0], prefix_key,
						prefix_value) < 0) {
				for (i = 0; i < 4; i++)
					prefix_value->prefix[i] = prefix_key->data[i];
				prefix_value->value = route.mac & 0xffffffffffff;
				prefix_value->ifindex = route.iface;
				prefix_value->gw = route.gw;
				prefix_value->metric = route.metric;

				assert(bpf_map_update_elem(map_fd[0],
							   prefix_key,
							   prefix_value, 0
							   ) == 0);
			} else {
				if (nh->nlmsg_type == RTM_DELROUTE) {
					printf("deleting entry\n");
					printf("prefix key=%d.%d.%d.%d/%d",
					       prefix_key->data[0],
					       prefix_key->data[1],
					       prefix_key->data[2],
					       prefix_key->data[3],
					       prefix_key->prefixlen);
					assert(bpf_map_delete_elem(map_fd[0],
								   prefix_key
								   ) == 0);
					/* Rereading the route table to check if
					 * there is an entry with the same
					 * prefix but a different metric as the
					 * deleted enty.
					 */
					get_route_table(AF_INET);
				} else if (prefix_key->data[0] ==
					   prefix_value->prefix[0] &&
					   prefix_key->data[1] ==
					   prefix_value->prefix[1] &&
					   prefix_key->data[2] ==
					   prefix_value->prefix[2] &&
					   prefix_key->data[3] ==
					   prefix_value->prefix[3] &&
					   route.metric >= prefix_value->metric) {
					continue;
				} else {
					for (i = 0; i < 4; i++)
						prefix_value->prefix[i] =
							prefix_key->data[i];
					prefix_value->value =
						route.mac & 0xffffffffffff;
					prefix_value->ifindex = route.iface;
					prefix_value->gw = route.gw;
					prefix_value->metric = route.metric;
					assert(bpf_map_update_elem(
								   map_fd[0],
								   prefix_key,
								   prefix_value,
								   0) == 0);
				}
			}
		}
		memset(&route, 0, sizeof(route));
		memset(dsts, 0, sizeof(dsts));
		memset(dsts_len, 0, sizeof(dsts_len));
		memset(gws, 0, sizeof(gws));
		memset(ifs, 0, sizeof(ifs));
		memset(&route, 0, sizeof(route));
	}
}

/* Function to read the existing route table  when the process is launched*/
static int get_route_table(int rtm_family)
{
	struct sockaddr_nl sa;
	struct nlmsghdr *nh;
	int sock, seq = 0;
	struct msghdr msg;
	struct iovec iov;
	int ret = 0;
	int nll;

	struct {
		struct nlmsghdr nl;
		struct rtmsg rt;
		char buf[8192];
	} req;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0) {
		printf("open netlink socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		printf("bind to netlink: %s\n", strerror(errno));
		ret = -1;
		goto cleanup;
	}
	memset(&req, 0, sizeof(req));
	req.nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nl.nlmsg_type = RTM_GETROUTE;

	req.rt.rtm_family = rtm_family;
	req.rt.rtm_table = RT_TABLE_MAIN;
	req.nl.nlmsg_pid = 0;
	req.nl.nlmsg_seq = ++seq;
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = (void *)&req.nl;
	iov.iov_len = req.nl.nlmsg_len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	ret = sendmsg(sock, &msg, 0);
	if (ret < 0) {
		printf("send to netlink: %s\n", strerror(errno));
		ret = -1;
		goto cleanup;
	}
	memset(buf, 0, sizeof(buf));
	nll = recv_msg(sa, sock);
	if (nll < 0) {
		printf("recv from netlink: %s\n", strerror(nll));
		ret = -1;
		goto cleanup;
	}
	nh = (struct nlmsghdr *)buf;
	read_route(nh, nll);
cleanup:
	close(sock);
	return ret;
}

/* Function to parse the arp entry returned by netlink
 * Updates the arp entry related map entries
 */
static void read_arp(struct nlmsghdr *nh, int nll)
{
	struct rtattr *rt_attr;
	char dsts[24], mac[24];
	struct ndmsg *rt_msg;
	int rtl, ndm_family;

	struct arp_table {
		__be64 mac;
		__be32 dst;
	} arp_entry;
	struct direct_map {
		struct arp_table arp;
		int ifindex;
		__be64 mac;
	} direct_entry;

	if (nh->nlmsg_type == RTM_GETNEIGH)
		printf("READING arp entry\n");
	printf("Address\tHwAddress\n");
	for (; NLMSG_OK(nh, nll); nh = NLMSG_NEXT(nh, nll)) {
		rt_msg = (struct ndmsg *)NLMSG_DATA(nh);
		rt_attr = (struct rtattr *)RTM_RTA(rt_msg);
		ndm_family = rt_msg->ndm_family;
		rtl = RTM_PAYLOAD(nh);
		for (; RTA_OK(rt_attr, rtl); rt_attr = RTA_NEXT(rt_attr, rtl)) {
			switch (rt_attr->rta_type) {
			case NDA_DST:
				sprintf(dsts, "%u",
					*((__be32 *)RTA_DATA(rt_attr)));
				break;
			case NDA_LLADDR:
				sprintf(mac, "%lld",
					*((__be64 *)RTA_DATA(rt_attr)));
				break;
			default:
				break;
			}
		}
		arp_entry.dst = atoi(dsts);
		arp_entry.mac = atol(mac);
		printf("%x\t\t%llx\n", arp_entry.dst, arp_entry.mac);
		if (ndm_family == AF_INET) {
			if (bpf_map_lookup_elem(map_fd[3], &arp_entry.dst,
						&direct_entry) == 0) {
				if (nh->nlmsg_type == RTM_DELNEIGH) {
					direct_entry.arp.dst = 0;
					direct_entry.arp.mac = 0;
				} else if (nh->nlmsg_type == RTM_NEWNEIGH) {
					direct_entry.arp.dst = arp_entry.dst;
					direct_entry.arp.mac = arp_entry.mac;
				}
				assert(bpf_map_update_elem(map_fd[3],
							   &arp_entry.dst,
							   &direct_entry, 0
							   ) == 0);
				memset(&direct_entry, 0, sizeof(direct_entry));
			}
			if (nh->nlmsg_type == RTM_DELNEIGH) {
				assert(bpf_map_delete_elem(map_fd[2], &arp_entry.dst) == 0);
			} else if (nh->nlmsg_type == RTM_NEWNEIGH) {
				assert(bpf_map_update_elem(map_fd[2],
							   &arp_entry.dst,
							   &arp_entry.mac, 0
							   ) == 0);
			}
		}
		memset(&arp_entry, 0, sizeof(arp_entry));
		memset(dsts, 0, sizeof(dsts));
	}
}

/* Function to read the existing arp table  when the process is launched*/
static int get_arp_table(int rtm_family)
{
	struct sockaddr_nl sa;
	struct nlmsghdr *nh;
	int sock, seq = 0;
	struct msghdr msg;
	struct iovec iov;
	int ret = 0;
	int nll;
	struct {
		struct nlmsghdr nl;
		struct ndmsg rt;
		char buf[8192];
	} req;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0) {
		printf("open netlink socket: %s\n", strerror(errno));
		return -1;
	}
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		printf("bind to netlink: %s\n", strerror(errno));
		ret = -1;
		goto cleanup;
	}
	memset(&req, 0, sizeof(req));
	req.nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
	req.nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nl.nlmsg_type = RTM_GETNEIGH;
	req.rt.ndm_state = NUD_REACHABLE;
	req.rt.ndm_family = rtm_family;
	req.nl.nlmsg_pid = 0;
	req.nl.nlmsg_seq = ++seq;
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = (void *)&req.nl;
	iov.iov_len = req.nl.nlmsg_len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	ret = sendmsg(sock, &msg, 0);
	if (ret < 0) {
		printf("send to netlink: %s\n", strerror(errno));
		ret = -1;
		goto cleanup;
	}
	memset(buf, 0, sizeof(buf));
	nll = recv_msg(sa, sock);
	if (nll < 0) {
		printf("recv from netlink: %s\n", strerror(nll));
		ret = -1;
		goto cleanup;
	}
	nh = (struct nlmsghdr *)buf;
	read_arp(nh, nll);
cleanup:
	close(sock);
	return ret;
}

/* Function to keep track and update changes in route and arp table
 * Give regular statistics of packets forwarded
 */
static int monitor_route(void)
{
	unsigned int nr_cpus = bpf_num_possible_cpus();
	const unsigned int nr_keys = 256;
	struct pollfd fds_route, fds_arp;
	__u64 prev[nr_keys][nr_cpus];
	struct sockaddr_nl la, lr;
	__u64 values[nr_cpus];
	struct nlmsghdr *nh;
	int nll, ret = 0;
	int interval = 5;
	__u32 key;
	int i;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0) {
		printf("open netlink socket: %s\n", strerror(errno));
		return -1;
	}

	fcntl(sock, F_SETFL, O_NONBLOCK);
	memset(&lr, 0, sizeof(lr));
	lr.nl_family = AF_NETLINK;
	lr.nl_groups = RTMGRP_IPV6_ROUTE | RTMGRP_IPV4_ROUTE | RTMGRP_NOTIFY;
	if (bind(sock, (struct sockaddr *)&lr, sizeof(lr)) < 0) {
		printf("bind to netlink: %s\n", strerror(errno));
		ret = -1;
		goto cleanup;
	}
	fds_route.fd = sock;
	fds_route.events = POLL_IN;

	sock_arp = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock_arp < 0) {
		printf("open netlink socket: %s\n", strerror(errno));
		return -1;
	}

	fcntl(sock_arp, F_SETFL, O_NONBLOCK);
	memset(&la, 0, sizeof(la));
	la.nl_family = AF_NETLINK;
	la.nl_groups = RTMGRP_NEIGH | RTMGRP_NOTIFY;
	if (bind(sock_arp, (struct sockaddr *)&la, sizeof(la)) < 0) {
		printf("bind to netlink: %s\n", strerror(errno));
		ret = -1;
		goto cleanup;
	}
	fds_arp.fd = sock_arp;
	fds_arp.events = POLL_IN;

	memset(prev, 0, sizeof(prev));
	do {
		signal(SIGINT, close_and_exit);
		signal(SIGTERM, close_and_exit);

		sleep(interval);
		for (key = 0; key < nr_keys; key++) {
			__u64 sum = 0;

			assert(bpf_map_lookup_elem(map_fd[1], &key, values) == 0);
			for (i = 0; i < nr_cpus; i++)
				sum += (values[i] - prev[key][i]);
			if (sum)
				printf("proto %u: %10llu pkt/s\n",
				       key, sum / interval);
			memcpy(prev[key], values, sizeof(values));
		}

		memset(buf, 0, sizeof(buf));
		if (poll(&fds_route, 1, 3) == POLL_IN) {
			nll = recv_msg(lr, sock);
			if (nll < 0) {
				printf("recv from netlink: %s\n", strerror(nll));
				ret = -1;
				goto cleanup;
			}

			nh = (struct nlmsghdr *)buf;
			printf("Routing table updated.\n");
			read_route(nh, nll);
		}
		memset(buf, 0, sizeof(buf));
		if (poll(&fds_arp, 1, 3) == POLL_IN) {
			nll = recv_msg(la, sock_arp);
			if (nll < 0) {
				printf("recv from netlink: %s\n", strerror(nll));
				ret = -1;
				goto cleanup;
			}

			nh = (struct nlmsghdr *)buf;
			read_arp(nh, nll);
		}

	} while (1);
cleanup:
	close(sock);
	return ret;
}

int main(int ac, char **argv)
{
	char filename[256];
	char **ifname_list;
	int i = 1;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	if (ac < 2) {
		printf("usage: %s [-S] Interface name list\n", argv[0]);
		return 1;
	}
	if (!strcmp(argv[1], "-S")) {
		flags = XDP_FLAGS_SKB_MODE;
		total_ifindex = ac - 2;
		ifname_list = (argv + 2);
	} else {
		flags = 0;
		total_ifindex = ac - 1;
		ifname_list = (argv + 1);
	}
	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}
	printf("\n**************loading bpf file*********************\n\n\n");
	if (!prog_fd[0]) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}
	ifindex_list = (int *)malloc(total_ifindex * sizeof(int *));
	for (i = 0; i < total_ifindex; i++) {
		ifindex_list[i] = if_nametoindex(ifname_list[i]);
		if (!ifindex_list[i]) {
			printf("Couldn't translate interface name: %s",
			       strerror(errno));
			return 1;
		}
	}
	for (i = 0; i < total_ifindex; i++) {
		if (bpf_set_link_xdp_fd(ifindex_list[i], prog_fd[0], flags) < 0) {
			printf("link set xdp fd failed\n");
			int recovery_index = i;

			for (i = 0; i < recovery_index; i++)
				bpf_set_link_xdp_fd(ifindex_list[i], -1, flags);

			return 1;
		}
		printf("Attached to %d\n", ifindex_list[i]);
	}
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	printf("*******************ROUTE TABLE*************************\n\n\n");
	get_route_table(AF_INET);
	printf("*******************ARP TABLE***************************\n\n\n");
	get_arp_table(AF_INET);
	if (monitor_route() < 0) {
		printf("Error in receiving route update");
		return 1;
	}

	return 0;
}
