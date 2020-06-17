/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETWORK_HELPERS_H
#define __NETWORK_HELPERS_H
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/types.h>
typedef __u16 __sum16;
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <netinet/tcp.h>
#include <bpf/bpf_endian.h>

#define MAGIC_VAL 0x1234
#define NUM_ITER 100000
#define VIP_NUM 5
#define MAGIC_BYTES 123

/* ipv4 test vector */
struct ipv4_packet {
	struct ethhdr eth;
	struct iphdr iph;
	struct tcphdr tcp;
} __packed;
extern struct ipv4_packet pkt_v4;

/* ipv6 test vector */
struct ipv6_packet {
	struct ethhdr eth;
	struct ipv6hdr iph;
	struct tcphdr tcp;
} __packed;
extern struct ipv6_packet pkt_v6;

int start_server(int family, int type);
int start_server_with_port(int family, int type, __u16 port);
int connect_to_fd(int family, int type, int server_fd);
int connect_fd_to_fd(int client_fd, int server_fd);
int connect_wait(int client_fd);

#endif
