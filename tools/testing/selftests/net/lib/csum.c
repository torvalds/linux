// SPDX-License-Identifier: GPL-2.0

/* Test hardware checksum offload: Rx + Tx, IPv4 + IPv6, TCP + UDP.
 *
 * The test runs on two machines to exercise the NIC. For this reason it
 * is not integrated in kselftests.
 *
 *     CMD=$((./csum -[46] -[tu] -S $SADDR -D $DADDR -[RT] -r 1 $EXTRA_ARGS))
 *
 * Rx:
 *
 * The sender sends packets with a known checksum field using PF_INET(6)
 * SOCK_RAW sockets.
 *
 * good packet: $CMD [-t]
 * bad packet:  $CMD [-t] -E
 *
 * The receiver reads UDP packets with a UDP socket. This is not an
 * option for TCP packets ('-t'). Optionally insert an iptables filter
 * to avoid these entering the real protocol stack.
 *
 * The receiver also reads all packets with a PF_PACKET socket, to
 * observe whether both good and bad packets arrive on the host. And to
 * read the optional TP_STATUS_CSUM_VALID bit. This requires setting
 * option PACKET_AUXDATA, and works only for CHECKSUM_UNNECESSARY.
 *
 * Tx:
 *
 * The sender needs to build CHECKSUM_PARTIAL packets to exercise tx
 * checksum offload.
 *
 * The sender can sends packets with a UDP socket.
 *
 * Optionally crafts a packet that sums up to zero to verify that the
 * device writes negative zero 0xFFFF in this case to distinguish from
 * 0x0000 (checksum disabled), as required by RFC 768. Hit this case
 * by choosing a specific source port.
 *
 * good packet: $CMD -U
 * zero csum:   $CMD -U -Z
 *
 * The sender can also build packets with PF_PACKET with PACKET_VNET_HDR,
 * to cover more protocols. PF_PACKET requires passing src and dst mac
 * addresses.
 *
 * good packet: $CMD -s $smac -d $dmac -p [-t]
 *
 * Argument '-z' sends UDP packets with a 0x000 checksum disabled field,
 * to verify that the NIC passes these packets unmodified.
 *
 * Argument '-e' adds a transport mode encapsulation header between
 * network and transport header. This will fail for devices that parse
 *  headers. Should work on devices that implement protocol agnostic tx
 * checksum offload (NETIF_F_HW_CSUM).
 *
 * Argument '-r $SEED' optionally randomizes header, payload and length
 * to increase coverage between packets sent. SEED 1 further chooses a
 * different seed for each run (and logs this for reproducibility). It
 * is advised to enable this for extra coverage in continuous testing.
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <asm/byteorder.h>
#include <errno.h>
#include <error.h>
#include <linux/filter.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <linux/virtio_net.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <poll.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "kselftest.h"

static bool cfg_bad_csum;
static int cfg_family = PF_INET6;
static int cfg_num_pkt = 4;
static bool cfg_do_rx = true;
static bool cfg_do_tx = true;
static bool cfg_encap;
static char *cfg_ifname = "eth0";
static char *cfg_mac_dst;
static char *cfg_mac_src;
static int cfg_proto = IPPROTO_UDP;
static int cfg_payload_char = 'a';
static int cfg_payload_len = 100;
static uint16_t cfg_port_dst = 34000;
static uint16_t cfg_port_src = 33000;
static uint16_t cfg_port_src_encap = 33001;
static unsigned int cfg_random_seed;
static int cfg_rcvbuf = 1 << 22;	/* be able to queue large cfg_num_pkt */
static bool cfg_send_pfpacket;
static bool cfg_send_udp;
static int cfg_timeout_ms = 2000;
static bool cfg_zero_disable; /* skip checksum: set to zero (udp only) */
static bool cfg_zero_sum;     /* create packet that adds up to zero */

static struct sockaddr_in cfg_daddr4 = {.sin_family = AF_INET};
static struct sockaddr_in cfg_saddr4 = {.sin_family = AF_INET};
static struct sockaddr_in6 cfg_daddr6 = {.sin6_family = AF_INET6};
static struct sockaddr_in6 cfg_saddr6 = {.sin6_family = AF_INET6};

#define ENC_HEADER_LEN	(sizeof(struct udphdr) + sizeof(struct udp_encap_hdr))
#define MAX_HEADER_LEN	(sizeof(struct ipv6hdr) + ENC_HEADER_LEN + sizeof(struct tcphdr))
#define MAX_PAYLOAD_LEN 1024

/* Trivial demo encap. Stand-in for transport layer protocols like ESP or PSP */
struct udp_encap_hdr {
	uint8_t nexthdr;
	uint8_t padding[3];
};

/* Ipaddrs, for pseudo csum. Global var is ugly, pass through funcs was worse */
static void *iph_addr_p;

static unsigned long gettimeofday_ms(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000UL) + (tv.tv_usec / 1000UL);
}

static uint32_t checksum_nofold(char *data, size_t len, uint32_t sum)
{
	uint16_t *words = (uint16_t *)data;
	int i;

	for (i = 0; i < len / 2; i++)
		sum += words[i];

	if (len & 1)
		sum += ((unsigned char *)data)[len - 1];

	return sum;
}

static uint16_t checksum_fold(void *data, size_t len, uint32_t sum)
{
	sum = checksum_nofold(data, len, sum);

	while (sum > 0xFFFF)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

static uint16_t checksum(void *th, uint16_t proto, size_t len)
{
	uint32_t sum;
	int alen;

	alen = cfg_family == PF_INET6 ? 32 : 8;

	sum = checksum_nofold(iph_addr_p, alen, 0);
	sum += htons(proto);
	sum += htons(len);

	/* With CHECKSUM_PARTIAL kernel expects non-inverted pseudo csum */
	if (cfg_do_tx && cfg_send_pfpacket)
		return ~checksum_fold(NULL, 0, sum);
	else
		return checksum_fold(th, len, sum);
}

static void *build_packet_ipv4(void *_iph, uint8_t proto, unsigned int len)
{
	struct iphdr *iph = _iph;

	memset(iph, 0, sizeof(*iph));

	iph->version = 4;
	iph->ihl = 5;
	iph->ttl = 8;
	iph->protocol = proto;
	iph->saddr = cfg_saddr4.sin_addr.s_addr;
	iph->daddr = cfg_daddr4.sin_addr.s_addr;
	iph->tot_len = htons(sizeof(*iph) + len);
	iph->check = checksum_fold(iph, sizeof(*iph), 0);

	iph_addr_p = &iph->saddr;

	return iph + 1;
}

static void *build_packet_ipv6(void *_ip6h, uint8_t proto, unsigned int len)
{
	struct ipv6hdr *ip6h = _ip6h;

	memset(ip6h, 0, sizeof(*ip6h));

	ip6h->version = 6;
	ip6h->payload_len = htons(len);
	ip6h->nexthdr = proto;
	ip6h->hop_limit = 64;
	ip6h->saddr = cfg_saddr6.sin6_addr;
	ip6h->daddr = cfg_daddr6.sin6_addr;

	iph_addr_p = &ip6h->saddr;

	return ip6h + 1;
}

static void *build_packet_udp(void *_uh)
{
	struct udphdr *uh = _uh;

	uh->source = htons(cfg_port_src);
	uh->dest = htons(cfg_port_dst);
	uh->len = htons(sizeof(*uh) + cfg_payload_len);
	uh->check = 0;

	/* choose source port so that uh->check adds up to zero */
	if (cfg_zero_sum) {
		uh->source = 0;
		uh->source = checksum(uh, IPPROTO_UDP, sizeof(*uh) + cfg_payload_len);

		fprintf(stderr, "tx: changing sport: %hu -> %hu\n",
			cfg_port_src, ntohs(uh->source));
		cfg_port_src = ntohs(uh->source);
	}

	if (cfg_zero_disable)
		uh->check = 0;
	else
		uh->check = checksum(uh, IPPROTO_UDP, sizeof(*uh) + cfg_payload_len);

	if (cfg_bad_csum)
		uh->check = ~uh->check;

	fprintf(stderr, "tx: sending checksum: 0x%x\n", uh->check);
	return uh + 1;
}

static void *build_packet_tcp(void *_th)
{
	struct tcphdr *th = _th;

	th->source = htons(cfg_port_src);
	th->dest = htons(cfg_port_dst);
	th->doff = 5;
	th->check = 0;

	th->check = checksum(th, IPPROTO_TCP, sizeof(*th) + cfg_payload_len);

	if (cfg_bad_csum)
		th->check = ~th->check;

	fprintf(stderr, "tx: sending checksum: 0x%x\n", th->check);
	return th + 1;
}

static char *build_packet_udp_encap(void *_uh)
{
	struct udphdr *uh = _uh;
	struct udp_encap_hdr *eh = _uh + sizeof(*uh);

	/* outer dst == inner dst, to simplify BPF filter
	 * outer src != inner src, to demultiplex on recv
	 */
	uh->dest = htons(cfg_port_dst);
	uh->source = htons(cfg_port_src_encap);
	uh->check = 0;
	uh->len = htons(sizeof(*uh) +
			sizeof(*eh) +
			sizeof(struct tcphdr) +
			cfg_payload_len);

	eh->nexthdr = IPPROTO_TCP;

	return build_packet_tcp(eh + 1);
}

static char *build_packet(char *buf, int max_len, int *len)
{
	uint8_t proto;
	char *off;
	int tlen;

	if (cfg_random_seed) {
		int *buf32 = (void *)buf;
		int i;

		for (i = 0; i < (max_len / sizeof(int)); i++)
			buf32[i] = rand();
	} else {
		memset(buf, cfg_payload_char, max_len);
	}

	if (cfg_proto == IPPROTO_UDP)
		tlen = sizeof(struct udphdr) + cfg_payload_len;
	else
		tlen = sizeof(struct tcphdr) + cfg_payload_len;

	if (cfg_encap) {
		proto = IPPROTO_UDP;
		tlen += ENC_HEADER_LEN;
	} else {
		proto = cfg_proto;
	}

	if (cfg_family == PF_INET)
		off = build_packet_ipv4(buf, proto, tlen);
	else
		off = build_packet_ipv6(buf, proto, tlen);

	if (cfg_encap)
		off = build_packet_udp_encap(off);
	else if (cfg_proto == IPPROTO_UDP)
		off = build_packet_udp(off);
	else
		off = build_packet_tcp(off);

	/* only pass the payload, but still compute headers for cfg_zero_sum */
	if (cfg_send_udp) {
		*len = cfg_payload_len;
		return off;
	}

	*len = off - buf + cfg_payload_len;
	return buf;
}

static int open_inet(int ipproto, int protocol)
{
	int fd;

	fd = socket(cfg_family, ipproto, protocol);
	if (fd == -1)
		error(1, errno, "socket inet");

	if (cfg_family == PF_INET6) {
		/* may have been updated by cfg_zero_sum */
		cfg_saddr6.sin6_port = htons(cfg_port_src);

		if (bind(fd, (void *)&cfg_saddr6, sizeof(cfg_saddr6)))
			error(1, errno, "bind dgram 6");
		if (connect(fd, (void *)&cfg_daddr6, sizeof(cfg_daddr6)))
			error(1, errno, "connect dgram 6");
	} else {
		/* may have been updated by cfg_zero_sum */
		cfg_saddr4.sin_port = htons(cfg_port_src);

		if (bind(fd, (void *)&cfg_saddr4, sizeof(cfg_saddr4)))
			error(1, errno, "bind dgram 4");
		if (connect(fd, (void *)&cfg_daddr4, sizeof(cfg_daddr4)))
			error(1, errno, "connect dgram 4");
	}

	return fd;
}

static int open_packet(void)
{
	int fd, one = 1;

	fd = socket(PF_PACKET, SOCK_RAW, 0);
	if (fd == -1)
		error(1, errno, "socket packet");

	if (setsockopt(fd, SOL_PACKET, PACKET_VNET_HDR, &one, sizeof(one)))
		error(1, errno, "setsockopt packet_vnet_ndr");

	return fd;
}

static void send_inet(int fd, const char *buf, int len)
{
	int ret;

	ret = write(fd, buf, len);
	if (ret == -1)
		error(1, errno, "write");
	if (ret != len)
		error(1, 0, "write: %d", ret);
}

static void eth_str_to_addr(const char *str, unsigned char *eth)
{
	if (sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		   &eth[0], &eth[1], &eth[2], &eth[3], &eth[4], &eth[5]) != 6)
		error(1, 0, "cannot parse mac addr %s", str);
}

static void send_packet(int fd, const char *buf, int len)
{
	struct virtio_net_hdr vh = {0};
	struct sockaddr_ll addr = {0};
	struct msghdr msg = {0};
	struct ethhdr eth;
	struct iovec iov[3];
	int ret;

	addr.sll_family = AF_PACKET;
	addr.sll_halen = ETH_ALEN;
	addr.sll_ifindex = if_nametoindex(cfg_ifname);
	if (!addr.sll_ifindex)
		error(1, errno, "if_nametoindex %s", cfg_ifname);

	vh.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
	if (cfg_family == PF_INET6) {
		vh.csum_start = sizeof(struct ethhdr) + sizeof(struct ipv6hdr);
		addr.sll_protocol = htons(ETH_P_IPV6);
	} else {
		vh.csum_start = sizeof(struct ethhdr) + sizeof(struct iphdr);
		addr.sll_protocol = htons(ETH_P_IP);
	}

	if (cfg_encap)
		vh.csum_start += ENC_HEADER_LEN;

	if (cfg_proto == IPPROTO_TCP) {
		vh.csum_offset = __builtin_offsetof(struct tcphdr, check);
		vh.hdr_len = vh.csum_start + sizeof(struct tcphdr);
	} else {
		vh.csum_offset = __builtin_offsetof(struct udphdr, check);
		vh.hdr_len = vh.csum_start + sizeof(struct udphdr);
	}

	eth_str_to_addr(cfg_mac_src, eth.h_source);
	eth_str_to_addr(cfg_mac_dst, eth.h_dest);
	eth.h_proto = addr.sll_protocol;

	iov[0].iov_base = &vh;
	iov[0].iov_len = sizeof(vh);

	iov[1].iov_base = &eth;
	iov[1].iov_len = sizeof(eth);

	iov[2].iov_base = (void *)buf;
	iov[2].iov_len = len;

	msg.msg_iov = iov;
	msg.msg_iovlen = ARRAY_SIZE(iov);

	msg.msg_name = &addr;
	msg.msg_namelen = sizeof(addr);

	ret = sendmsg(fd, &msg, 0);
	if (ret == -1)
		error(1, errno, "sendmsg packet");
	if (ret != sizeof(vh) + sizeof(eth) + len)
		error(1, errno, "sendmsg packet: %u", ret);
}

static int recv_prepare_udp(void)
{
	int fd;

	fd = socket(cfg_family, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket r");

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
		       &cfg_rcvbuf, sizeof(cfg_rcvbuf)))
		error(1, errno, "setsockopt SO_RCVBUF r");

	if (cfg_family == PF_INET6) {
		if (bind(fd, (void *)&cfg_daddr6, sizeof(cfg_daddr6)))
			error(1, errno, "bind r");
	} else {
		if (bind(fd, (void *)&cfg_daddr4, sizeof(cfg_daddr4)))
			error(1, errno, "bind r");
	}

	return fd;
}

/* Filter out all traffic that is not cfg_proto with our destination port.
 *
 * Otherwise background noise may cause PF_PACKET receive queue overflow,
 * dropping the expected packets and failing the test.
 */
static void __recv_prepare_packet_filter(int fd, int off_nexthdr, int off_dport)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD + BPF_B + BPF_ABS, SKF_AD_OFF + SKF_AD_PKTTYPE),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, PACKET_HOST, 0, 4),
		BPF_STMT(BPF_LD + BPF_B + BPF_ABS, off_nexthdr),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, cfg_encap ? IPPROTO_UDP : cfg_proto, 0, 2),
		BPF_STMT(BPF_LD + BPF_H + BPF_ABS, off_dport),
		BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, cfg_port_dst, 1, 0),
		BPF_STMT(BPF_RET + BPF_K, 0),
		BPF_STMT(BPF_RET + BPF_K, 0xFFFF),
	};
	struct sock_fprog prog = {};

	prog.filter = filter;
	prog.len = ARRAY_SIZE(filter);
	if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog)))
		error(1, errno, "setsockopt filter");
}

static void recv_prepare_packet_filter(int fd)
{
	const int off_dport = offsetof(struct tcphdr, dest); /* same for udp */

	if (cfg_family == AF_INET)
		__recv_prepare_packet_filter(fd, offsetof(struct iphdr, protocol),
					     sizeof(struct iphdr) + off_dport);
	else
		__recv_prepare_packet_filter(fd, offsetof(struct ipv6hdr, nexthdr),
					     sizeof(struct ipv6hdr) + off_dport);
}

static void recv_prepare_packet_bind(int fd)
{
	struct sockaddr_ll laddr = {0};

	laddr.sll_family = AF_PACKET;

	if (cfg_family == PF_INET)
		laddr.sll_protocol = htons(ETH_P_IP);
	else
		laddr.sll_protocol = htons(ETH_P_IPV6);

	laddr.sll_ifindex = if_nametoindex(cfg_ifname);
	if (!laddr.sll_ifindex)
		error(1, 0, "if_nametoindex %s", cfg_ifname);

	if (bind(fd, (void *)&laddr, sizeof(laddr)))
		error(1, errno, "bind pf_packet");
}

static int recv_prepare_packet(void)
{
	int fd, one = 1;

	fd = socket(PF_PACKET, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket p");

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
		       &cfg_rcvbuf, sizeof(cfg_rcvbuf)))
		error(1, errno, "setsockopt SO_RCVBUF p");

	/* enable auxdata to recv checksum status (valid vs unknown) */
	if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &one, sizeof(one)))
		error(1, errno, "setsockopt auxdata");

	/* install filter to restrict packet flow to match */
	recv_prepare_packet_filter(fd);

	/* bind to address family to start packet flow */
	recv_prepare_packet_bind(fd);

	return fd;
}

static int recv_udp(int fd)
{
	static char buf[MAX_PAYLOAD_LEN];
	int ret, count = 0;

	while (1) {
		ret = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
		if (ret == -1 && errno == EAGAIN)
			break;
		if (ret == -1)
			error(1, errno, "recv r");

		fprintf(stderr, "rx: udp: len=%u\n", ret);
		count++;
	}

	return count;
}

static int recv_verify_csum(void *th, int len, uint16_t sport, uint16_t csum_field)
{
	uint16_t csum;

	csum = checksum(th, cfg_proto, len);

	fprintf(stderr, "rx: pkt: sport=%hu len=%u csum=0x%hx verify=0x%hx\n",
		sport, len, csum_field, csum);

	/* csum must be zero unless cfg_bad_csum indicates bad csum */
	if (csum && !cfg_bad_csum) {
		fprintf(stderr, "pkt: bad csum\n");
		return 1;
	} else if (cfg_bad_csum && !csum) {
		fprintf(stderr, "pkt: good csum, while bad expected\n");
		return 1;
	}

	if (cfg_zero_sum && csum_field != 0xFFFF) {
		fprintf(stderr, "pkt: zero csum: field should be 0xFFFF, is 0x%hx\n", csum_field);
		return 1;
	}

	return 0;
}

static int recv_verify_packet_tcp(void *th, int len)
{
	struct tcphdr *tcph = th;

	if (len < sizeof(*tcph) || tcph->dest != htons(cfg_port_dst))
		return -1;

	return recv_verify_csum(th, len, ntohs(tcph->source), tcph->check);
}

static int recv_verify_packet_udp_encap(void *th, int len)
{
	struct udp_encap_hdr *eh = th;

	if (len < sizeof(*eh) || eh->nexthdr != IPPROTO_TCP)
		return -1;

	return recv_verify_packet_tcp(eh + 1, len - sizeof(*eh));
}

static int recv_verify_packet_udp(void *th, int len)
{
	struct udphdr *udph = th;

	if (len < sizeof(*udph))
		return -1;

	if (udph->dest != htons(cfg_port_dst))
		return -1;

	if (udph->source == htons(cfg_port_src_encap))
		return recv_verify_packet_udp_encap(udph + 1,
						    len - sizeof(*udph));

	return recv_verify_csum(th, len, ntohs(udph->source), udph->check);
}

static int recv_verify_packet_ipv4(void *nh, int len)
{
	struct iphdr *iph = nh;
	uint16_t proto = cfg_encap ? IPPROTO_UDP : cfg_proto;

	if (len < sizeof(*iph) || iph->protocol != proto)
		return -1;

	iph_addr_p = &iph->saddr;
	if (proto == IPPROTO_TCP)
		return recv_verify_packet_tcp(iph + 1, len - sizeof(*iph));
	else
		return recv_verify_packet_udp(iph + 1, len - sizeof(*iph));
}

static int recv_verify_packet_ipv6(void *nh, int len)
{
	struct ipv6hdr *ip6h = nh;
	uint16_t proto = cfg_encap ? IPPROTO_UDP : cfg_proto;

	if (len < sizeof(*ip6h) || ip6h->nexthdr != proto)
		return -1;

	iph_addr_p = &ip6h->saddr;

	if (proto == IPPROTO_TCP)
		return recv_verify_packet_tcp(ip6h + 1, len - sizeof(*ip6h));
	else
		return recv_verify_packet_udp(ip6h + 1, len - sizeof(*ip6h));
}

/* return whether auxdata includes TP_STATUS_CSUM_VALID */
static uint32_t recv_get_packet_csum_status(struct msghdr *msg)
{
	struct tpacket_auxdata *aux = NULL;
	struct cmsghdr *cm;

	if (msg->msg_flags & MSG_CTRUNC)
		error(1, 0, "cmsg: truncated");

	for (cm = CMSG_FIRSTHDR(msg); cm; cm = CMSG_NXTHDR(msg, cm)) {
		if (cm->cmsg_level != SOL_PACKET ||
		    cm->cmsg_type != PACKET_AUXDATA)
			error(1, 0, "cmsg: level=%d type=%d\n",
			      cm->cmsg_level, cm->cmsg_type);

		if (cm->cmsg_len != CMSG_LEN(sizeof(struct tpacket_auxdata)))
			error(1, 0, "cmsg: len=%lu expected=%lu",
			      cm->cmsg_len, CMSG_LEN(sizeof(struct tpacket_auxdata)));

		aux = (void *)CMSG_DATA(cm);
	}

	if (!aux)
		error(1, 0, "cmsg: no auxdata");

	return aux->tp_status;
}

static int recv_packet(int fd)
{
	static char _buf[MAX_HEADER_LEN + MAX_PAYLOAD_LEN];
	unsigned long total = 0, bad_csums = 0, bad_validations = 0;
	char ctrl[CMSG_SPACE(sizeof(struct tpacket_auxdata))];
	struct pkt *buf = (void *)_buf;
	struct msghdr msg = {0};
	uint32_t tp_status;
	struct iovec iov;
	int len, ret;

	iov.iov_base = _buf;
	iov.iov_len = sizeof(_buf);

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	while (1) {
		msg.msg_flags = 0;

		len = recvmsg(fd, &msg, MSG_DONTWAIT);
		if (len == -1 && errno == EAGAIN)
			break;
		if (len == -1)
			error(1, errno, "recv p");

		tp_status = recv_get_packet_csum_status(&msg);

		/* GRO might coalesce randomized packets. Such GSO packets are
		 * then reinitialized for csum offload (CHECKSUM_PARTIAL), with
		 * a pseudo csum. Do not try to validate these checksums.
		 */
		if (tp_status & TP_STATUS_CSUMNOTREADY) {
			fprintf(stderr, "cmsg: GSO packet has partial csum: skip\n");
			continue;
		}

		if (cfg_family == PF_INET6)
			ret = recv_verify_packet_ipv6(buf, len);
		else
			ret = recv_verify_packet_ipv4(buf, len);

		if (ret == -1 /* skip: non-matching */)
			continue;

		total++;
		if (ret == 1)
			bad_csums++;

		/* Fail if kernel returns valid for known bad csum.
		 * Do not fail if kernel does not validate a good csum:
		 * Absence of validation does not imply invalid.
		 */
		if (tp_status & TP_STATUS_CSUM_VALID && cfg_bad_csum) {
			fprintf(stderr, "cmsg: expected bad csum, pf_packet returns valid\n");
			bad_validations++;
		}
	}

	if (bad_csums || bad_validations)
		error(1, 0, "rx: errors at pf_packet: total=%lu bad_csums=%lu bad_valids=%lu\n",
		      total, bad_csums, bad_validations);

	return total;
}

static void parse_args(int argc, char *const argv[])
{
	const char *daddr = NULL, *saddr = NULL;
	int c;

	while ((c = getopt(argc, argv, "46d:D:eEi:l:L:n:r:PRs:S:tTuUzZ")) != -1) {
		switch (c) {
		case '4':
			cfg_family = PF_INET;
			break;
		case '6':
			cfg_family = PF_INET6;
			break;
		case 'd':
			cfg_mac_dst = optarg;
			break;
		case 'D':
			daddr = optarg;
			break;
		case 'e':
			cfg_encap = true;
			break;
		case 'E':
			cfg_bad_csum = true;
			break;
		case 'i':
			cfg_ifname = optarg;
			break;
		case 'l':
			cfg_payload_len = strtol(optarg, NULL, 0);
			break;
		case 'L':
			cfg_timeout_ms = strtol(optarg, NULL, 0) * 1000;
			break;
		case 'n':
			cfg_num_pkt = strtol(optarg, NULL, 0);
			break;
		case 'r':
			cfg_random_seed = strtol(optarg, NULL, 0);
			break;
		case 'P':
			cfg_send_pfpacket = true;
			break;
		case 'R':
			/* only Rx: used with two machine tests */
			cfg_do_tx = false;
			break;
		case 's':
			cfg_mac_src = optarg;
			break;
		case 'S':
			saddr = optarg;
			break;
		case 't':
			cfg_proto = IPPROTO_TCP;
			break;
		case 'T':
			/* only Tx: used with two machine tests */
			cfg_do_rx = false;
			break;
		case 'u':
			cfg_proto = IPPROTO_UDP;
			break;
		case 'U':
			/* send using real udp socket,
			 * to exercise tx checksum offload
			 */
			cfg_send_udp = true;
			break;
		case 'z':
			cfg_zero_disable = true;
			break;
		case 'Z':
			cfg_zero_sum = true;
			break;
		default:
			error(1, 0, "unknown arg %c", c);
		}
	}

	if (!daddr || !saddr)
		error(1, 0, "Must pass -D <daddr> and -S <saddr>");

	if (cfg_do_tx && cfg_send_pfpacket && (!cfg_mac_src || !cfg_mac_dst))
		error(1, 0, "Transmit with pf_packet requires mac addresses");

	if (cfg_payload_len > MAX_PAYLOAD_LEN)
		error(1, 0, "Payload length exceeds max");

	if (cfg_proto != IPPROTO_UDP && (cfg_zero_sum || cfg_zero_disable))
		error(1, 0, "Only UDP supports zero csum");

	if (cfg_zero_sum && !cfg_send_udp)
		error(1, 0, "Zero checksum conversion requires -U for tx csum offload");
	if (cfg_zero_sum && cfg_bad_csum)
		error(1, 0, "Cannot combine zero checksum conversion and invalid checksum");
	if (cfg_zero_sum && cfg_random_seed)
		error(1, 0, "Cannot combine zero checksum conversion with randomization");

	if (cfg_family == PF_INET6) {
		cfg_saddr6.sin6_port = htons(cfg_port_src);
		cfg_daddr6.sin6_port = htons(cfg_port_dst);

		if (inet_pton(cfg_family, daddr, &cfg_daddr6.sin6_addr) != 1)
			error(1, errno, "Cannot parse ipv6 -D");
		if (inet_pton(cfg_family, saddr, &cfg_saddr6.sin6_addr) != 1)
			error(1, errno, "Cannot parse ipv6 -S");
	} else {
		cfg_saddr4.sin_port = htons(cfg_port_src);
		cfg_daddr4.sin_port = htons(cfg_port_dst);

		if (inet_pton(cfg_family, daddr, &cfg_daddr4.sin_addr) != 1)
			error(1, errno, "Cannot parse ipv4 -D");
		if (inet_pton(cfg_family, saddr, &cfg_saddr4.sin_addr) != 1)
			error(1, errno, "Cannot parse ipv4 -S");
	}

	if (cfg_do_tx && cfg_random_seed) {
		/* special case: time-based seed */
		if (cfg_random_seed == 1)
			cfg_random_seed = (unsigned int)gettimeofday_ms();
		srand(cfg_random_seed);
		fprintf(stderr, "randomization seed: %u\n", cfg_random_seed);
	}
}

static void do_tx(void)
{
	static char _buf[MAX_HEADER_LEN + MAX_PAYLOAD_LEN];
	char *buf;
	int fd, len, i;

	buf = build_packet(_buf, sizeof(_buf), &len);

	if (cfg_send_pfpacket)
		fd = open_packet();
	else if (cfg_send_udp)
		fd = open_inet(SOCK_DGRAM, 0);
	else
		fd = open_inet(SOCK_RAW, IPPROTO_RAW);

	for (i = 0; i < cfg_num_pkt; i++) {
		if (cfg_send_pfpacket)
			send_packet(fd, buf, len);
		else
			send_inet(fd, buf, len);

		/* randomize each packet individually to increase coverage */
		if (cfg_random_seed) {
			cfg_payload_len = rand() % MAX_PAYLOAD_LEN;
			buf = build_packet(_buf, sizeof(_buf), &len);
		}
	}

	if (close(fd))
		error(1, errno, "close tx");
}

static void do_rx(int fdp, int fdr)
{
	unsigned long count_udp = 0, count_pkt = 0;
	long tleft, tstop;
	struct pollfd pfd;

	tstop = gettimeofday_ms() + cfg_timeout_ms;
	tleft = cfg_timeout_ms;

	do {
		pfd.events = POLLIN;
		pfd.fd = fdp;
		if (poll(&pfd, 1, tleft) == -1)
			error(1, errno, "poll");

		if (pfd.revents & POLLIN)
			count_pkt += recv_packet(fdp);

		if (cfg_proto == IPPROTO_UDP)
			count_udp += recv_udp(fdr);

		tleft = tstop - gettimeofday_ms();
	} while (tleft > 0);

	if (close(fdr))
		error(1, errno, "close r");
	if (close(fdp))
		error(1, errno, "close p");

	if (count_pkt < cfg_num_pkt)
		error(1, 0, "rx: missing packets at pf_packet: %lu < %u",
		      count_pkt, cfg_num_pkt);

	if (cfg_proto == IPPROTO_UDP) {
		if (cfg_bad_csum && count_udp)
			error(1, 0, "rx: unexpected packets at udp");
		if (!cfg_bad_csum && !count_udp)
			error(1, 0, "rx: missing packets at udp");
	}
}

int main(int argc, char *const argv[])
{
	int fdp = -1, fdr = -1;		/* -1 to silence -Wmaybe-uninitialized */

	parse_args(argc, argv);

	/* open receive sockets before transmitting */
	if (cfg_do_rx) {
		fdp = recv_prepare_packet();
		fdr = recv_prepare_udp();
	}

	if (cfg_do_tx)
		do_tx();

	if (cfg_do_rx)
		do_rx(fdp, fdr);

	fprintf(stderr, "OK\n");
	return 0;
}
