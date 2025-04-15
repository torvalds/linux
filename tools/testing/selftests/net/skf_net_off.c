// SPDX-License-Identifier: GPL-2.0

/* Open a tun device.
 *
 * [modifications: use IFF_NAPI_FRAGS, add sk filter]
 *
 * Expects the device to have been configured previously, e.g.:
 *   sudo ip tuntap add name tap1 mode tap
 *   sudo ip link set tap1 up
 *   sudo ip link set dev tap1 addr 02:00:00:00:00:01
 *   sudo ip -6 addr add fdab::1 peer fdab::2 dev tap1 nodad
 *
 * And to avoid premature pskb_may_pull:
 *
 *   sudo ethtool -K tap1 gro off
 *   sudo bash -c 'echo 0 > /proc/sys/net/ipv4/ip_early_demux'
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/filter.h>
#include <linux/if.h>
#include <linux/if_packet.h>
#include <linux/if_tun.h>
#include <linux/ipv6.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/udp.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

static bool cfg_do_filter;
static bool cfg_do_frags;
static int cfg_dst_port = 8000;
static char *cfg_ifname;

static int tun_open(const char *tun_name)
{
	struct ifreq ifr = {0};
	int fd, ret;

	fd = open("/dev/net/tun", O_RDWR);
	if (fd == -1)
		error(1, errno, "open /dev/net/tun");

	ifr.ifr_flags = IFF_TAP;
	if (cfg_do_frags)
		ifr.ifr_flags |= IFF_NAPI | IFF_NAPI_FRAGS;

	strncpy(ifr.ifr_name, tun_name, IFNAMSIZ - 1);

	ret = ioctl(fd, TUNSETIFF, &ifr);
	if (ret)
		error(1, ret, "ioctl TUNSETIFF");

	return fd;
}

static void sk_set_filter(int fd)
{
	const int offset_proto = offsetof(struct ip6_hdr, ip6_nxt);
	const int offset_dport = sizeof(struct ip6_hdr) + offsetof(struct udphdr, dest);

	/* Filter UDP packets with destination port cfg_dst_port */
	struct sock_filter filter_code[] = {
		BPF_STMT(BPF_LD  + BPF_B   + BPF_ABS, SKF_AD_OFF + SKF_AD_PKTTYPE),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, PACKET_HOST, 0, 4),
		BPF_STMT(BPF_LD  + BPF_B   + BPF_ABS, SKF_NET_OFF + offset_proto),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, IPPROTO_UDP, 0, 2),
		BPF_STMT(BPF_LD  + BPF_H   + BPF_ABS, SKF_NET_OFF + offset_dport),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, cfg_dst_port, 1, 0),
		BPF_STMT(BPF_RET + BPF_K, 0),
		BPF_STMT(BPF_RET + BPF_K, 0xFFFF),
	};

	struct sock_fprog filter = {
		sizeof(filter_code) / sizeof(filter_code[0]),
		filter_code,
	};

	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &filter, sizeof(filter)))
		error(1, errno, "setsockopt attach filter");
}

static int raw_open(void)
{
	int fd;

	fd = socket(PF_INET6, SOCK_RAW, IPPROTO_UDP);
	if (fd == -1)
		error(1, errno, "socket raw (udp)");

	if (cfg_do_filter)
		sk_set_filter(fd);

	return fd;
}

static void tun_write(int fd)
{
	const char eth_src[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };
	const char eth_dst[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
	struct tun_pi pi = {0};
	struct ipv6hdr ip6h = {0};
	struct udphdr uh = {0};
	struct ethhdr eth = {0};
	uint32_t payload;
	struct iovec iov[5];
	int ret;

	pi.proto = htons(ETH_P_IPV6);

	memcpy(eth.h_source, eth_src, sizeof(eth_src));
	memcpy(eth.h_dest, eth_dst, sizeof(eth_dst));
	eth.h_proto = htons(ETH_P_IPV6);

	ip6h.version = 6;
	ip6h.payload_len = htons(sizeof(uh) + sizeof(uint32_t));
	ip6h.nexthdr = IPPROTO_UDP;
	ip6h.hop_limit = 8;
	if (inet_pton(AF_INET6, "fdab::2", &ip6h.saddr) != 1)
		error(1, errno, "inet_pton src");
	if (inet_pton(AF_INET6, "fdab::1", &ip6h.daddr) != 1)
		error(1, errno, "inet_pton src");

	uh.source = htons(8000);
	uh.dest = htons(cfg_dst_port);
	uh.len = ip6h.payload_len;
	uh.check = 0;

	payload = htonl(0xABABABAB);		/* Covered in IPv6 length */

	iov[0].iov_base = &pi;
	iov[0].iov_len  = sizeof(pi);
	iov[1].iov_base = &eth;
	iov[1].iov_len  = sizeof(eth);
	iov[2].iov_base = &ip6h;
	iov[2].iov_len  = sizeof(ip6h);
	iov[3].iov_base = &uh;
	iov[3].iov_len  = sizeof(uh);
	iov[4].iov_base = &payload;
	iov[4].iov_len  = sizeof(payload);

	ret = writev(fd, iov, sizeof(iov) / sizeof(iov[0]));
	if (ret <= 0)
		error(1, errno, "writev");
}

static void raw_read(int fd)
{
	struct timeval tv = { .tv_usec = 100 * 1000 };
	struct msghdr msg = {0};
	struct iovec iov[2];
	struct udphdr uh;
	uint32_t payload[2];
	int ret;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
		error(1, errno, "setsockopt rcvtimeo udp");

	iov[0].iov_base = &uh;
	iov[0].iov_len = sizeof(uh);

	iov[1].iov_base = payload;
	iov[1].iov_len = sizeof(payload);

	msg.msg_iov = iov;
	msg.msg_iovlen = sizeof(iov) / sizeof(iov[0]);

	ret = recvmsg(fd, &msg, 0);
	if (ret <= 0)
		error(1, errno, "read raw");
	if (ret != sizeof(uh) + sizeof(payload[0]))
		error(1, errno, "read raw: len=%d\n", ret);

	fprintf(stderr, "raw recv: 0x%x\n", payload[0]);
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "fFi:")) != -1) {
		switch (c) {
		case 'f':
			cfg_do_filter = true;
			printf("bpf filter enabled\n");
			break;
		case 'F':
			cfg_do_frags = true;
			printf("napi frags mode enabled\n");
			break;
		case 'i':
			cfg_ifname = optarg;
			break;
		default:
			error(1, 0, "unknown option %c", optopt);
			break;
		}
	}

	if (!cfg_ifname)
		error(1, 0, "must specify tap interface name (-i)");
}

int main(int argc, char **argv)
{
	int fdt, fdr;

	parse_opts(argc, argv);

	fdr = raw_open();
	fdt = tun_open(cfg_ifname);

	tun_write(fdt);
	raw_read(fdr);

	if (close(fdt))
		error(1, errno, "close tun");
	if (close(fdr))
		error(1, errno, "close udp");

	fprintf(stderr, "OK\n");
	return 0;
}

