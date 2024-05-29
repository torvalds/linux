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
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/err.h>
#include <netinet/tcp.h>
#include <bpf/bpf_endian.h>
#include <net/if.h>

#define MAGIC_VAL 0x1234
#define NUM_ITER 100000
#define VIP_NUM 5
#define MAGIC_BYTES 123

struct network_helper_opts {
	int timeout_ms;
	bool must_fail;
	bool noconnect;
	int type;
	int proto;
	int (*post_socket_cb)(int fd, void *opts);
	void *cb_opts;
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
int start_server_str(int family, int type, const char *addr_str, __u16 port,
		     const struct network_helper_opts *opts);
int start_server(int family, int type, const char *addr, __u16 port,
		 int timeout_ms);
int *start_reuseport_server(int family, int type, const char *addr_str,
			    __u16 port, int timeout_ms,
			    unsigned int nr_listens);
int start_server_addr(int type, const struct sockaddr_storage *addr, socklen_t len,
		      const struct network_helper_opts *opts);
void free_fds(int *fds, unsigned int nr_close_fds);
int connect_to_addr(int type, const struct sockaddr_storage *addr, socklen_t len,
		    const struct network_helper_opts *opts);
int connect_to_fd(int server_fd, int timeout_ms);
int connect_to_fd_opts(int server_fd, const struct network_helper_opts *opts);
int connect_fd_to_fd(int client_fd, int server_fd, int timeout_ms);
int fastopen_connect(int server_fd, const char *data, unsigned int data_len,
		     int timeout_ms);
int make_sockaddr(int family, const char *addr_str, __u16 port,
		  struct sockaddr_storage *addr, socklen_t *len);
char *ping_command(int family);
int get_socket_local_port(int sock_fd);
int get_hw_ring_size(char *ifname, struct ethtool_ringparam *ring_param);
int set_hw_ring_size(char *ifname, struct ethtool_ringparam *ring_param);

struct nstoken;
/**
 * open_netns() - Switch to specified network namespace by name.
 *
 * Returns token with which to restore the original namespace
 * using close_netns().
 */
struct nstoken *open_netns(const char *name);
void close_netns(struct nstoken *token);
int send_recv_data(int lfd, int fd, uint32_t total_bytes);

static __u16 csum_fold(__u32 csum)
{
	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);

	return (__u16)~csum;
}

static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
					__u32 len, __u8 proto,
					__wsum csum)
{
	__u64 s = csum;

	s += (__u32)saddr;
	s += (__u32)daddr;
	s += htons(proto + len);
	s = (s & 0xffffffff) + (s >> 32);
	s = (s & 0xffffffff) + (s >> 32);

	return csum_fold((__u32)s);
}

static inline __sum16 csum_ipv6_magic(const struct in6_addr *saddr,
				      const struct in6_addr *daddr,
					__u32 len, __u8 proto,
					__wsum csum)
{
	__u64 s = csum;
	int i;

	for (i = 0; i < 4; i++)
		s += (__u32)saddr->s6_addr32[i];
	for (i = 0; i < 4; i++)
		s += (__u32)daddr->s6_addr32[i];
	s += htons(proto + len);
	s = (s & 0xffffffff) + (s >> 32);
	s = (s & 0xffffffff) + (s >> 32);

	return csum_fold((__u32)s);
}

#endif
