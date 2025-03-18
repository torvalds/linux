/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETWORK_HELPERS_H
#define __NETWORK_HELPERS_H
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/types.h>
typedef __u16 __sum16;
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_tun.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/err.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <bpf/bpf_endian.h>
#include <net/if.h>

#define MAGIC_VAL 0x1234
#define NUM_ITER 100000
#define VIP_NUM 5
#define MAGIC_BYTES 123

struct network_helper_opts {
	int timeout_ms;
	int proto;
	/* +ve: Passed to listen() as-is.
	 *   0: Default when the test does not set
	 *      a particular value during the struct init.
	 *      It is changed to 1 before passing to listen().
	 *      Most tests only have one on-going connection.
	 * -ve: It is changed to 0 before passing to listen().
	 *      It is useful to force syncookie without
	 *	changing the "tcp_syncookies" sysctl from 1 to 2.
	 */
	int backlog;
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
int client_socket(int family, int type,
		  const struct network_helper_opts *opts);
int connect_to_addr(int type, const struct sockaddr_storage *addr, socklen_t len,
		    const struct network_helper_opts *opts);
int connect_to_addr_str(int family, int type, const char *addr_str, __u16 port,
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

int open_tuntap(const char *dev_name, bool need_mac);

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
int make_netns(const char *name);
int remove_netns(const char *name);

static __u16 csum_fold(__u32 csum)
{
	csum = (csum & 0xffff) + (csum >> 16);
	csum = (csum & 0xffff) + (csum >> 16);

	return (__u16)~csum;
}

static __wsum csum_partial(const void *buf, int len, __wsum sum)
{
	__u16 *p = (__u16 *)buf;
	int num_u16 = len >> 1;
	int i;

	for (i = 0; i < num_u16; i++)
		sum += p[i];

	return sum;
}

static inline __sum16 build_ip_csum(struct iphdr *iph)
{
	__u32 sum = 0;
	__u16 *p;

	iph->check = 0;
	p = (void *)iph;
	sum = csum_partial(p, iph->ihl << 2, 0);

	return csum_fold(sum);
}

/**
 * csum_tcpudp_magic - compute IP pseudo-header checksum
 *
 * Compute the IPv4 pseudo header checksum. The helper can take a
 * accumulated sum from the transport layer to accumulate it and directly
 * return the transport layer
 *
 * @saddr: IP source address
 * @daddr: IP dest address
 * @len: IP data size
 * @proto: transport layer protocol
 * @csum: The accumulated partial sum to add to the computation
 *
 * Returns the folded sum
 */
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

/**
 * csum_ipv6_magic - compute IPv6 pseudo-header checksum
 *
 * Compute the ipv6 pseudo header checksum. The helper can take a
 * accumulated sum from the transport layer to accumulate it and directly
 * return the transport layer
 *
 * @saddr: IPv6 source address
 * @daddr: IPv6 dest address
 * @len: IPv6 data size
 * @proto: transport layer protocol
 * @csum: The accumulated partial sum to add to the computation
 *
 * Returns the folded sum
 */
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

/**
 * build_udp_v4_csum - compute UDP checksum for UDP over IPv4
 *
 * Compute the checksum to embed in UDP header, composed of the sum of IP
 * pseudo-header checksum, UDP header checksum and UDP data checksum
 * @iph IP header
 * @udph UDP header, which must be immediately followed by UDP data
 *
 * Returns the total checksum
 */

static inline __sum16 build_udp_v4_csum(const struct iphdr *iph,
					const struct udphdr *udph)
{
	unsigned long sum;

	sum = csum_partial(udph, ntohs(udph->len), 0);
	return csum_tcpudp_magic(iph->saddr, iph->daddr, ntohs(udph->len),
				 IPPROTO_UDP, sum);
}

/**
 * build_udp_v6_csum - compute UDP checksum for UDP over IPv6
 *
 * Compute the checksum to embed in UDP header, composed of the sum of IPv6
 * pseudo-header checksum, UDP header checksum and UDP data checksum
 * @ip6h IPv6 header
 * @udph UDP header, which must be immediately followed by UDP data
 *
 * Returns the total checksum
 */
static inline __sum16 build_udp_v6_csum(const struct ipv6hdr *ip6h,
					const struct udphdr *udph)
{
	unsigned long sum;

	sum = csum_partial(udph, ntohs(udph->len), 0);
	return csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr, ntohs(udph->len),
			       IPPROTO_UDP, sum);
}

struct tmonitor_ctx;

#ifdef TRAFFIC_MONITOR
struct tmonitor_ctx *traffic_monitor_start(const char *netns, const char *test_name,
					   const char *subtest_name);
void traffic_monitor_stop(struct tmonitor_ctx *ctx);
#else
static inline struct tmonitor_ctx *traffic_monitor_start(const char *netns, const char *test_name,
							 const char *subtest_name)
{
	return NULL;
}

static inline void traffic_monitor_stop(struct tmonitor_ctx *ctx)
{
}
#endif

#endif
