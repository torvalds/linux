/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/bpf.h>
#include <linux/if_link.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <netinet/ether.h>
#include <unistd.h>
#include <time.h>
#include "bpf/libbpf.h"
#include <bpf/bpf.h>
#include "bpf_util.h"
#include "xdp_tx_iptunnel_common.h"

#define STATS_INTERVAL_S 2U

static int ifindex = -1;
static __u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static int rxcnt_map_fd;
static __u32 prog_id;

static void int_exit(int sig)
{
	__u32 curr_prog_id = 0;

	if (ifindex > -1) {
		if (bpf_get_link_xdp_id(ifindex, &curr_prog_id, xdp_flags)) {
			printf("bpf_get_link_xdp_id failed\n");
			exit(1);
		}
		if (prog_id == curr_prog_id)
			bpf_set_link_xdp_fd(ifindex, -1, xdp_flags);
		else if (!curr_prog_id)
			printf("couldn't find a prog id on a given iface\n");
		else
			printf("program on interface changed, not removing\n");
	}
	exit(0);
}

/* simple per-protocol drop counter
 */
static void poll_stats(unsigned int kill_after_s)
{
	const unsigned int nr_protos = 256;
	unsigned int nr_cpus = bpf_num_possible_cpus();
	time_t started_at = time(NULL);
	__u64 values[nr_cpus], prev[nr_protos][nr_cpus];
	__u32 proto;
	int i;

	memset(prev, 0, sizeof(prev));

	while (!kill_after_s || time(NULL) - started_at <= kill_after_s) {
		sleep(STATS_INTERVAL_S);

		for (proto = 0; proto < nr_protos; proto++) {
			__u64 sum = 0;

			assert(bpf_map_lookup_elem(rxcnt_map_fd, &proto,
						   values) == 0);
			for (i = 0; i < nr_cpus; i++)
				sum += (values[i] - prev[proto][i]);

			if (sum)
				printf("proto %u: sum:%10llu pkts, rate:%10llu pkts/s\n",
				       proto, sum, sum / STATS_INTERVAL_S);
			memcpy(prev[proto], values, sizeof(values));
		}
	}
}

static void usage(const char *cmd)
{
	printf("Start a XDP prog which encapsulates incoming packets\n"
	       "in an IPv4/v6 header and XDP_TX it out.  The dst <VIP:PORT>\n"
	       "is used to select packets to encapsulate\n\n");
	printf("Usage: %s [...]\n", cmd);
	printf("    -i <ifindex> Interface Index\n");
	printf("    -a <vip-service-address> IPv4 or IPv6\n");
	printf("    -p <vip-service-port> A port range (e.g. 433-444) is also allowed\n");
	printf("    -s <source-ip> Used in the IPTunnel header\n");
	printf("    -d <dest-ip> Used in the IPTunnel header\n");
	printf("    -m <dest-MAC> Used in sending the IP Tunneled pkt\n");
	printf("    -T <stop-after-X-seconds> Default: 0 (forever)\n");
	printf("    -P <IP-Protocol> Default is TCP\n");
	printf("    -S use skb-mode\n");
	printf("    -N enforce native mode\n");
	printf("    -F Force loading the XDP prog\n");
	printf("    -h Display this help\n");
}

static int parse_ipstr(const char *ipstr, unsigned int *addr)
{
	if (inet_pton(AF_INET6, ipstr, addr) == 1) {
		return AF_INET6;
	} else if (inet_pton(AF_INET, ipstr, addr) == 1) {
		addr[1] = addr[2] = addr[3] = 0;
		return AF_INET;
	}

	fprintf(stderr, "%s is an invalid IP\n", ipstr);
	return AF_UNSPEC;
}

static int parse_ports(const char *port_str, int *min_port, int *max_port)
{
	char *end;
	long tmp_min_port;
	long tmp_max_port;

	tmp_min_port = strtol(optarg, &end, 10);
	if (tmp_min_port < 1 || tmp_min_port > 65535) {
		fprintf(stderr, "Invalid port(s):%s\n", optarg);
		return 1;
	}

	if (*end == '-') {
		end++;
		tmp_max_port = strtol(end, NULL, 10);
		if (tmp_max_port < 1 || tmp_max_port > 65535) {
			fprintf(stderr, "Invalid port(s):%s\n", optarg);
			return 1;
		}
	} else {
		tmp_max_port = tmp_min_port;
	}

	if (tmp_min_port > tmp_max_port) {
		fprintf(stderr, "Invalid port(s):%s\n", optarg);
		return 1;
	}

	if (tmp_max_port - tmp_min_port + 1 > MAX_IPTNL_ENTRIES) {
		fprintf(stderr, "Port range (%s) is larger than %u\n",
			port_str, MAX_IPTNL_ENTRIES);
		return 1;
	}
	*min_port = tmp_min_port;
	*max_port = tmp_max_port;

	return 0;
}

int main(int argc, char **argv)
{
	struct bpf_prog_load_attr prog_load_attr = {
		.prog_type	= BPF_PROG_TYPE_XDP,
	};
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	int min_port = 0, max_port = 0, vip2tnl_map_fd;
	const char *optstr = "i:a:p:s:d:m:T:P:FSNh";
	unsigned char opt_flags[256] = {};
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	unsigned int kill_after_s = 0;
	struct iptnl_info tnl = {};
	struct bpf_object *obj;
	struct vip vip = {};
	char filename[256];
	int opt, prog_fd;
	int i, err;

	tnl.family = AF_UNSPEC;
	vip.protocol = IPPROTO_TCP;

	for (i = 0; i < strlen(optstr); i++)
		if (optstr[i] != 'h' && 'a' <= optstr[i] && optstr[i] <= 'z')
			opt_flags[(unsigned char)optstr[i]] = 1;

	while ((opt = getopt(argc, argv, optstr)) != -1) {
		unsigned short family;
		unsigned int *v6;

		switch (opt) {
		case 'i':
			ifindex = atoi(optarg);
			break;
		case 'a':
			vip.family = parse_ipstr(optarg, vip.daddr.v6);
			if (vip.family == AF_UNSPEC)
				return 1;
			break;
		case 'p':
			if (parse_ports(optarg, &min_port, &max_port))
				return 1;
			break;
		case 'P':
			vip.protocol = atoi(optarg);
			break;
		case 's':
		case 'd':
			if (opt == 's')
				v6 = tnl.saddr.v6;
			else
				v6 = tnl.daddr.v6;

			family = parse_ipstr(optarg, v6);
			if (family == AF_UNSPEC)
				return 1;
			if (tnl.family == AF_UNSPEC) {
				tnl.family = family;
			} else if (tnl.family != family) {
				fprintf(stderr,
					"The IP version of the src and dst addresses used in the IP encapsulation does not match\n");
				return 1;
			}
			break;
		case 'm':
			if (!ether_aton_r(optarg,
					  (struct ether_addr *)tnl.dmac)) {
				fprintf(stderr, "Invalid mac address:%s\n",
					optarg);
				return 1;
			}
			break;
		case 'T':
			kill_after_s = atoi(optarg);
			break;
		case 'S':
			xdp_flags |= XDP_FLAGS_SKB_MODE;
			break;
		case 'N':
			xdp_flags |= XDP_FLAGS_DRV_MODE;
			break;
		case 'F':
			xdp_flags &= ~XDP_FLAGS_UPDATE_IF_NOEXIST;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
		opt_flags[opt] = 0;
	}

	for (i = 0; i < strlen(optstr); i++) {
		if (opt_flags[(unsigned int)optstr[i]]) {
			fprintf(stderr, "Missing argument -%c\n", optstr[i]);
			usage(argv[0]);
			return 1;
		}
	}

	if (setrlimit(RLIMIT_MEMLOCK, &r)) {
		perror("setrlimit(RLIMIT_MEMLOCK, RLIM_INFINITY)");
		return 1;
	}

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	prog_load_attr.file = filename;

	if (bpf_prog_load_xattr(&prog_load_attr, &obj, &prog_fd))
		return 1;

	if (!prog_fd) {
		printf("load_bpf_file: %s\n", strerror(errno));
		return 1;
	}

	rxcnt_map_fd = bpf_object__find_map_fd_by_name(obj, "rxcnt");
	vip2tnl_map_fd = bpf_object__find_map_fd_by_name(obj, "vip2tnl");
	if (vip2tnl_map_fd < 0 || rxcnt_map_fd < 0) {
		printf("bpf_object__find_map_fd_by_name failed\n");
		return 1;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);

	while (min_port <= max_port) {
		vip.dport = htons(min_port++);
		if (bpf_map_update_elem(vip2tnl_map_fd, &vip, &tnl,
					BPF_NOEXIST)) {
			perror("bpf_map_update_elem(&vip2tnl)");
			return 1;
		}
	}

	if (bpf_set_link_xdp_fd(ifindex, prog_fd, xdp_flags) < 0) {
		printf("link set xdp fd failed\n");
		return 1;
	}

	err = bpf_obj_get_info_by_fd(prog_fd, &info, &info_len);
	if (err) {
		printf("can't get prog info - %s\n", strerror(errno));
		return err;
	}
	prog_id = info.id;

	poll_stats(kill_after_s);

	bpf_set_link_xdp_fd(ifindex, -1, xdp_flags);

	return 0;
}
