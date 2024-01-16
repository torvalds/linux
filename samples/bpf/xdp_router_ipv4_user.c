// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2017 Cavium, Inc.
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
#include <bpf/bpf.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include "bpf_util.h"
#include <bpf/libbpf.h>
#include <libgen.h>
#include <getopt.h>
#include <pthread.h>
#include "xdp_sample_user.h"
#include "xdp_router_ipv4.skel.h"

static const char *__doc__ =
"XDP IPv4 router implementation\n"
"Usage: xdp_router_ipv4 <IFNAME-0> ... <IFNAME-N>\n";

static char buf[8192];
static int lpm_map_fd;
static int arp_table_map_fd;
static int exact_match_map_fd;
static int tx_port_map_fd;

static bool routes_thread_exit;
static int interval = 5;

static int mask = SAMPLE_RX_CNT | SAMPLE_REDIRECT_ERR_MAP_CNT |
		  SAMPLE_DEVMAP_XMIT_CNT_MULTI | SAMPLE_EXCEPTION_CNT;

DEFINE_SAMPLE_INIT(xdp_router_ipv4);

static const struct option long_options[] = {
	{ "help", no_argument, NULL, 'h' },
	{ "skb-mode", no_argument, NULL, 'S' },
	{ "force", no_argument, NULL, 'F' },
	{ "interval", required_argument, NULL, 'i' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "stats", no_argument, NULL, 's' },
	{}
};

static int get_route_table(int rtm_family);

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

	memset(&route, 0, sizeof(route));
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
		assert(get_mac_addr(route.iface, &route.mac) == 0);
		assert(bpf_map_update_elem(tx_port_map_fd,
					   &route.iface, &route.iface, 0) == 0);
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
					assert(bpf_map_delete_elem(exact_match_map_fd,
								   &route.dst) == 0);
				} else {
					if (bpf_map_lookup_elem(arp_table_map_fd,
								&route.dst,
								&direct_entry.arp.mac) == 0)
						direct_entry.arp.dst = route.dst;
					assert(bpf_map_update_elem(exact_match_map_fd,
								   &route.dst,
								   &direct_entry, 0) == 0);
				}
			}
			for (i = 0; i < 4; i++)
				prefix_key->data[i] = (route.dst >> i * 8) & 0xff;

			if (bpf_map_lookup_elem(lpm_map_fd, prefix_key,
						prefix_value) < 0) {
				for (i = 0; i < 4; i++)
					prefix_value->prefix[i] = prefix_key->data[i];
				prefix_value->value = route.mac & 0xffffffffffff;
				prefix_value->ifindex = route.iface;
				prefix_value->gw = route.gw;
				prefix_value->metric = route.metric;

				assert(bpf_map_update_elem(lpm_map_fd,
							   prefix_key,
							   prefix_value, 0
							   ) == 0);
			} else {
				if (nh->nlmsg_type == RTM_DELROUTE) {
					assert(bpf_map_delete_elem(lpm_map_fd,
								   prefix_key
								   ) == 0);
					/* Rereading the route table to check if
					 * there is an entry with the same
					 * prefix but a different metric as the
					 * deleted entry.
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
					assert(bpf_map_update_elem(lpm_map_fd,
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
		fprintf(stderr, "open netlink socket: %s\n", strerror(errno));
		return -errno;
	}
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(stderr, "bind netlink socket: %s\n", strerror(errno));
		ret = -errno;
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
		fprintf(stderr, "send to netlink: %s\n", strerror(errno));
		ret = -errno;
		goto cleanup;
	}
	memset(buf, 0, sizeof(buf));
	nll = recv_msg(sa, sock);
	if (nll < 0) {
		fprintf(stderr, "recv from netlink: %s\n", strerror(nll));
		ret = nll;
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

		if (ndm_family == AF_INET) {
			if (bpf_map_lookup_elem(exact_match_map_fd,
						&arp_entry.dst,
						&direct_entry) == 0) {
				if (nh->nlmsg_type == RTM_DELNEIGH) {
					direct_entry.arp.dst = 0;
					direct_entry.arp.mac = 0;
				} else if (nh->nlmsg_type == RTM_NEWNEIGH) {
					direct_entry.arp.dst = arp_entry.dst;
					direct_entry.arp.mac = arp_entry.mac;
				}
				assert(bpf_map_update_elem(exact_match_map_fd,
							   &arp_entry.dst,
							   &direct_entry, 0
							   ) == 0);
				memset(&direct_entry, 0, sizeof(direct_entry));
			}
			if (nh->nlmsg_type == RTM_DELNEIGH) {
				assert(bpf_map_delete_elem(arp_table_map_fd,
							   &arp_entry.dst) == 0);
			} else if (nh->nlmsg_type == RTM_NEWNEIGH) {
				assert(bpf_map_update_elem(arp_table_map_fd,
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
		fprintf(stderr, "open netlink socket: %s\n", strerror(errno));
		return -errno;
	}
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(stderr, "bind netlink socket: %s\n", strerror(errno));
		ret = -errno;
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
		fprintf(stderr, "send to netlink: %s\n", strerror(errno));
		ret = -errno;
		goto cleanup;
	}
	memset(buf, 0, sizeof(buf));
	nll = recv_msg(sa, sock);
	if (nll < 0) {
		fprintf(stderr, "recv from netlink: %s\n", strerror(nll));
		ret = nll;
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
static void *monitor_routes_thread(void *arg)
{
	struct pollfd fds_route, fds_arp;
	struct sockaddr_nl la, lr;
	int sock, sock_arp, nll;
	struct nlmsghdr *nh;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0) {
		fprintf(stderr, "open netlink socket: %s\n", strerror(errno));
		return NULL;
	}

	fcntl(sock, F_SETFL, O_NONBLOCK);
	memset(&lr, 0, sizeof(lr));
	lr.nl_family = AF_NETLINK;
	lr.nl_groups = RTMGRP_IPV6_ROUTE | RTMGRP_IPV4_ROUTE | RTMGRP_NOTIFY;
	if (bind(sock, (struct sockaddr *)&lr, sizeof(lr)) < 0) {
		fprintf(stderr, "bind netlink socket: %s\n", strerror(errno));
		close(sock);
		return NULL;
	}

	fds_route.fd = sock;
	fds_route.events = POLL_IN;

	sock_arp = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock_arp < 0) {
		fprintf(stderr, "open netlink socket: %s\n", strerror(errno));
		close(sock);
		return NULL;
	}

	fcntl(sock_arp, F_SETFL, O_NONBLOCK);
	memset(&la, 0, sizeof(la));
	la.nl_family = AF_NETLINK;
	la.nl_groups = RTMGRP_NEIGH | RTMGRP_NOTIFY;
	if (bind(sock_arp, (struct sockaddr *)&la, sizeof(la)) < 0) {
		fprintf(stderr, "bind netlink socket: %s\n", strerror(errno));
		goto cleanup;
	}

	fds_arp.fd = sock_arp;
	fds_arp.events = POLL_IN;

	/* dump route and arp tables */
	if (get_arp_table(AF_INET) < 0) {
		fprintf(stderr, "Failed reading arp table\n");
		goto cleanup;
	}

	if (get_route_table(AF_INET) < 0) {
		fprintf(stderr, "Failed reading route table\n");
		goto cleanup;
	}

	while (!routes_thread_exit) {
		memset(buf, 0, sizeof(buf));
		if (poll(&fds_route, 1, 3) == POLL_IN) {
			nll = recv_msg(lr, sock);
			if (nll < 0) {
				fprintf(stderr, "recv from netlink: %s\n",
					strerror(nll));
				goto cleanup;
			}

			nh = (struct nlmsghdr *)buf;
			read_route(nh, nll);
		}

		memset(buf, 0, sizeof(buf));
		if (poll(&fds_arp, 1, 3) == POLL_IN) {
			nll = recv_msg(la, sock_arp);
			if (nll < 0) {
				fprintf(stderr, "recv from netlink: %s\n",
					strerror(nll));
				goto cleanup;
			}

			nh = (struct nlmsghdr *)buf;
			read_arp(nh, nll);
		}

		sleep(interval);
	}

cleanup:
	close(sock_arp);
	close(sock);
	return NULL;
}

static void usage(char *argv[], const struct option *long_options,
		  const char *doc, int mask, bool error,
		  struct bpf_object *obj)
{
	sample_usage(argv, long_options, doc, mask, error);
}

int main(int argc, char **argv)
{
	bool error = true, generic = false, force = false;
	int opt, ret = EXIT_FAIL_BPF;
	struct xdp_router_ipv4 *skel;
	int i, total_ifindex = argc - 1;
	char **ifname_list = argv + 1;
	pthread_t routes_thread;
	int longindex = 0;

	if (libbpf_set_strict_mode(LIBBPF_STRICT_ALL) < 0) {
		fprintf(stderr, "Failed to set libbpf strict mode: %s\n",
			strerror(errno));
		goto end;
	}

	skel = xdp_router_ipv4__open();
	if (!skel) {
		fprintf(stderr, "Failed to xdp_router_ipv4__open: %s\n",
			strerror(errno));
		goto end;
	}

	ret = sample_init_pre_load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to sample_init_pre_load: %s\n",
			strerror(-ret));
		ret = EXIT_FAIL_BPF;
		goto end_destroy;
	}

	ret = xdp_router_ipv4__load(skel);
	if (ret < 0) {
		fprintf(stderr, "Failed to xdp_router_ipv4__load: %s\n",
			strerror(errno));
		goto end_destroy;
	}

	ret = sample_init(skel, mask);
	if (ret < 0) {
		fprintf(stderr, "Failed to initialize sample: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}

	while ((opt = getopt_long(argc, argv, "si:SFvh",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 's':
			mask |= SAMPLE_REDIRECT_MAP_CNT;
			total_ifindex--;
			ifname_list++;
			break;
		case 'i':
			interval = strtoul(optarg, NULL, 0);
			total_ifindex -= 2;
			ifname_list += 2;
			break;
		case 'S':
			generic = true;
			total_ifindex--;
			ifname_list++;
			break;
		case 'F':
			force = true;
			total_ifindex--;
			ifname_list++;
			break;
		case 'v':
			sample_switch_mode();
			total_ifindex--;
			ifname_list++;
			break;
		case 'h':
			error = false;
		default:
			usage(argv, long_options, __doc__, mask, error, skel->obj);
			goto end_destroy;
		}
	}

	ret = EXIT_FAIL_OPTION;
	if (optind == argc) {
		usage(argv, long_options, __doc__, mask, true, skel->obj);
		goto end_destroy;
	}

	lpm_map_fd = bpf_map__fd(skel->maps.lpm_map);
	if (lpm_map_fd < 0) {
		fprintf(stderr, "Failed loading lpm_map %s\n",
			strerror(-lpm_map_fd));
		goto end_destroy;
	}
	arp_table_map_fd = bpf_map__fd(skel->maps.arp_table);
	if (arp_table_map_fd < 0) {
		fprintf(stderr, "Failed loading arp_table_map_fd %s\n",
			strerror(-arp_table_map_fd));
		goto end_destroy;
	}
	exact_match_map_fd = bpf_map__fd(skel->maps.exact_match);
	if (exact_match_map_fd < 0) {
		fprintf(stderr, "Failed loading exact_match_map_fd %s\n",
			strerror(-exact_match_map_fd));
		goto end_destroy;
	}
	tx_port_map_fd = bpf_map__fd(skel->maps.tx_port);
	if (tx_port_map_fd < 0) {
		fprintf(stderr, "Failed loading tx_port_map_fd %s\n",
			strerror(-tx_port_map_fd));
		goto end_destroy;
	}

	ret = EXIT_FAIL_XDP;
	for (i = 0; i < total_ifindex; i++) {
		int index = if_nametoindex(ifname_list[i]);

		if (!index) {
			fprintf(stderr, "Interface %s not found %s\n",
				ifname_list[i], strerror(-tx_port_map_fd));
			goto end_destroy;
		}
		if (sample_install_xdp(skel->progs.xdp_router_ipv4_prog,
				       index, generic, force) < 0)
			goto end_destroy;
	}

	ret = pthread_create(&routes_thread, NULL, monitor_routes_thread, NULL);
	if (ret) {
		fprintf(stderr, "Failed creating routes_thread: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_destroy;
	}

	ret = sample_run(interval, NULL, NULL);
	routes_thread_exit = true;

	if (ret < 0) {
		fprintf(stderr, "Failed during sample run: %s\n", strerror(-ret));
		ret = EXIT_FAIL;
		goto end_thread_wait;
	}
	ret = EXIT_OK;

end_thread_wait:
	pthread_join(routes_thread, NULL);
end_destroy:
	xdp_router_ipv4__destroy(skel);
end:
	sample_exit(ret);
}
