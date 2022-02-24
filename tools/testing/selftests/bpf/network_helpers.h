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

struct network_helper_opts {
	const char *cc;
	int timeout_ms;
	bool must_fail;
};

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

int settimeo(int fd, int timeout_ms);
int start_server(int family, int type, const char *addr, __u16 port,
		 int timeout_ms);
int *start_reuseport_server(int family, int type, const char *addr_str,
			    __u16 port, int timeout_ms,
			    unsigned int nr_listens);
void free_fds(int *fds, unsigned int nr_close_fds);
int connect_to_fd(int server_fd, int timeout_ms);
int connect_to_fd_opts(int server_fd, const struct network_helper_opts *opts);
int connect_fd_to_fd(int client_fd, int server_fd, int timeout_ms);
int fastopen_connect(int server_fd, const char *data, unsigned int data_len,
		     int timeout_ms);
int make_sockaddr(int family, const char *addr_str, __u16 port,
		  struct sockaddr_storage *addr, socklen_t *len);
char *ping_command(int family);

#endif
