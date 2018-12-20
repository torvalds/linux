// SPDX-License-Identifier: GPL-2.0
/*
 * Inject packets with all sorts of encapsulation into the kernel.
 *
 * IPv4/IPv6	outer layer 3
 * GRE/GUE/BARE outer layer 4, where bare is IPIP/SIT/IPv4-in-IPv6/..
 * IPv4/IPv6    inner layer 3
 */

#define _GNU_SOURCE

#include <stddef.h>
#include <arpa/inet.h>
#include <asm/byteorder.h>
#include <error.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ipv6.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CFG_PORT_INNER	8000

/* Add some protocol definitions that do not exist in userspace */

struct grehdr {
	uint16_t unused;
	uint16_t protocol;
} __attribute__((packed));

struct guehdr {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8	hlen:5,
				control:1,
				version:2;
#elif defined (__BIG_ENDIAN_BITFIELD)
			__u8	version:2,
				control:1,
				hlen:5;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
			__u8	proto_ctype;
			__be16	flags;
		};
		__be32	word;
	};
};

static uint8_t	cfg_dsfield_inner;
static uint8_t	cfg_dsfield_outer;
static uint8_t	cfg_encap_proto;
static bool	cfg_expect_failure = false;
static int	cfg_l3_extra = AF_UNSPEC;	/* optional SIT prefix */
static int	cfg_l3_inner = AF_UNSPEC;
static int	cfg_l3_outer = AF_UNSPEC;
static int	cfg_num_pkt = 10;
static int	cfg_num_secs = 0;
static char	cfg_payload_char = 'a';
static int	cfg_payload_len = 100;
static int	cfg_port_gue = 6080;
static bool	cfg_only_rx;
static bool	cfg_only_tx;
static int	cfg_src_port = 9;

static char	buf[ETH_DATA_LEN];

#define INIT_ADDR4(name, addr4, port)				\
	static struct sockaddr_in name = {			\
		.sin_family = AF_INET,				\
		.sin_port = __constant_htons(port),		\
		.sin_addr.s_addr = __constant_htonl(addr4),	\
	};

#define INIT_ADDR6(name, addr6, port)				\
	static struct sockaddr_in6 name = {			\
		.sin6_family = AF_INET6,			\
		.sin6_port = __constant_htons(port),		\
		.sin6_addr = addr6,				\
	};

INIT_ADDR4(in_daddr4, INADDR_LOOPBACK, CFG_PORT_INNER)
INIT_ADDR4(in_saddr4, INADDR_LOOPBACK + 2, 0)
INIT_ADDR4(out_daddr4, INADDR_LOOPBACK, 0)
INIT_ADDR4(out_saddr4, INADDR_LOOPBACK + 1, 0)
INIT_ADDR4(extra_daddr4, INADDR_LOOPBACK, 0)
INIT_ADDR4(extra_saddr4, INADDR_LOOPBACK + 1, 0)

INIT_ADDR6(in_daddr6, IN6ADDR_LOOPBACK_INIT, CFG_PORT_INNER)
INIT_ADDR6(in_saddr6, IN6ADDR_LOOPBACK_INIT, 0)
INIT_ADDR6(out_daddr6, IN6ADDR_LOOPBACK_INIT, 0)
INIT_ADDR6(out_saddr6, IN6ADDR_LOOPBACK_INIT, 0)
INIT_ADDR6(extra_daddr6, IN6ADDR_LOOPBACK_INIT, 0)
INIT_ADDR6(extra_saddr6, IN6ADDR_LOOPBACK_INIT, 0)

static unsigned long util_gettime(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static void util_printaddr(const char *msg, struct sockaddr *addr)
{
	unsigned long off = 0;
	char nbuf[INET6_ADDRSTRLEN];

	switch (addr->sa_family) {
	case PF_INET:
		off = __builtin_offsetof(struct sockaddr_in, sin_addr);
		break;
	case PF_INET6:
		off = __builtin_offsetof(struct sockaddr_in6, sin6_addr);
		break;
	default:
		error(1, 0, "printaddr: unsupported family %u\n",
		      addr->sa_family);
	}

	if (!inet_ntop(addr->sa_family, ((void *) addr) + off, nbuf,
		       sizeof(nbuf)))
		error(1, errno, "inet_ntop");

	fprintf(stderr, "%s: %s\n", msg, nbuf);
}

static unsigned long add_csum_hword(const uint16_t *start, int num_u16)
{
	unsigned long sum = 0;
	int i;

	for (i = 0; i < num_u16; i++)
		sum += start[i];

	return sum;
}

static uint16_t build_ip_csum(const uint16_t *start, int num_u16,
			      unsigned long sum)
{
	sum += add_csum_hword(start, num_u16);

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

static void build_ipv4_header(void *header, uint8_t proto,
			      uint32_t src, uint32_t dst,
			      int payload_len, uint8_t tos)
{
	struct iphdr *iph = header;

	iph->ihl = 5;
	iph->version = 4;
	iph->tos = tos;
	iph->ttl = 8;
	iph->tot_len = htons(sizeof(*iph) + payload_len);
	iph->id = htons(1337);
	iph->protocol = proto;
	iph->saddr = src;
	iph->daddr = dst;
	iph->check = build_ip_csum((void *) iph, iph->ihl << 1, 0);
}

static void ipv6_set_dsfield(struct ipv6hdr *ip6h, uint8_t dsfield)
{
	uint16_t val, *ptr = (uint16_t *)ip6h;

	val = ntohs(*ptr);
	val &= 0xF00F;
	val |= ((uint16_t) dsfield) << 4;
	*ptr = htons(val);
}

static void build_ipv6_header(void *header, uint8_t proto,
			      struct sockaddr_in6 *src,
			      struct sockaddr_in6 *dst,
			      int payload_len, uint8_t dsfield)
{
	struct ipv6hdr *ip6h = header;

	ip6h->version = 6;
	ip6h->payload_len = htons(payload_len);
	ip6h->nexthdr = proto;
	ip6h->hop_limit = 8;
	ipv6_set_dsfield(ip6h, dsfield);

	memcpy(&ip6h->saddr, &src->sin6_addr, sizeof(ip6h->saddr));
	memcpy(&ip6h->daddr, &dst->sin6_addr, sizeof(ip6h->daddr));
}

static uint16_t build_udp_v4_csum(const struct iphdr *iph,
				  const struct udphdr *udph,
				  int num_words)
{
	unsigned long pseudo_sum;
	int num_u16 = sizeof(iph->saddr);	/* halfwords: twice byte len */

	pseudo_sum = add_csum_hword((void *) &iph->saddr, num_u16);
	pseudo_sum += htons(IPPROTO_UDP);
	pseudo_sum += udph->len;
	return build_ip_csum((void *) udph, num_words, pseudo_sum);
}

static uint16_t build_udp_v6_csum(const struct ipv6hdr *ip6h,
				  const struct udphdr *udph,
				  int num_words)
{
	unsigned long pseudo_sum;
	int num_u16 = sizeof(ip6h->saddr);	/* halfwords: twice byte len */

	pseudo_sum = add_csum_hword((void *) &ip6h->saddr, num_u16);
	pseudo_sum += htons(ip6h->nexthdr);
	pseudo_sum += ip6h->payload_len;
	return build_ip_csum((void *) udph, num_words, pseudo_sum);
}

static void build_udp_header(void *header, int payload_len,
			     uint16_t dport, int family)
{
	struct udphdr *udph = header;
	int len = sizeof(*udph) + payload_len;

	udph->source = htons(cfg_src_port);
	udph->dest = htons(dport);
	udph->len = htons(len);
	udph->check = 0;
	if (family == AF_INET)
		udph->check = build_udp_v4_csum(header - sizeof(struct iphdr),
						udph, len >> 1);
	else
		udph->check = build_udp_v6_csum(header - sizeof(struct ipv6hdr),
						udph, len >> 1);
}

static void build_gue_header(void *header, uint8_t proto)
{
	struct guehdr *gueh = header;

	gueh->proto_ctype = proto;
}

static void build_gre_header(void *header, uint16_t proto)
{
	struct grehdr *greh = header;

	greh->protocol = htons(proto);
}

static int l3_length(int family)
{
	if (family == AF_INET)
		return sizeof(struct iphdr);
	else
		return sizeof(struct ipv6hdr);
}

static int build_packet(void)
{
	int ol3_len = 0, ol4_len = 0, il3_len = 0, il4_len = 0;
	int el3_len = 0;

	if (cfg_l3_extra)
		el3_len = l3_length(cfg_l3_extra);

	/* calculate header offsets */
	if (cfg_encap_proto) {
		ol3_len = l3_length(cfg_l3_outer);

		if (cfg_encap_proto == IPPROTO_GRE)
			ol4_len = sizeof(struct grehdr);
		else if (cfg_encap_proto == IPPROTO_UDP)
			ol4_len = sizeof(struct udphdr) + sizeof(struct guehdr);
	}

	il3_len = l3_length(cfg_l3_inner);
	il4_len = sizeof(struct udphdr);

	if (el3_len + ol3_len + ol4_len + il3_len + il4_len + cfg_payload_len >=
	    sizeof(buf))
		error(1, 0, "packet too large\n");

	/*
	 * Fill packet from inside out, to calculate correct checksums.
	 * But create ip before udp headers, as udp uses ip for pseudo-sum.
	 */
	memset(buf + el3_len + ol3_len + ol4_len + il3_len + il4_len,
	       cfg_payload_char, cfg_payload_len);

	/* add zero byte for udp csum padding */
	buf[el3_len + ol3_len + ol4_len + il3_len + il4_len + cfg_payload_len] = 0;

	switch (cfg_l3_inner) {
	case PF_INET:
		build_ipv4_header(buf + el3_len + ol3_len + ol4_len,
				  IPPROTO_UDP,
				  in_saddr4.sin_addr.s_addr,
				  in_daddr4.sin_addr.s_addr,
				  il4_len + cfg_payload_len,
				  cfg_dsfield_inner);
		break;
	case PF_INET6:
		build_ipv6_header(buf + el3_len + ol3_len + ol4_len,
				  IPPROTO_UDP,
				  &in_saddr6, &in_daddr6,
				  il4_len + cfg_payload_len,
				  cfg_dsfield_inner);
		break;
	}

	build_udp_header(buf + el3_len + ol3_len + ol4_len + il3_len,
			 cfg_payload_len, CFG_PORT_INNER, cfg_l3_inner);

	if (!cfg_encap_proto)
		return il3_len + il4_len + cfg_payload_len;

	switch (cfg_l3_outer) {
	case PF_INET:
		build_ipv4_header(buf + el3_len, cfg_encap_proto,
				  out_saddr4.sin_addr.s_addr,
				  out_daddr4.sin_addr.s_addr,
				  ol4_len + il3_len + il4_len + cfg_payload_len,
				  cfg_dsfield_outer);
		break;
	case PF_INET6:
		build_ipv6_header(buf + el3_len, cfg_encap_proto,
				  &out_saddr6, &out_daddr6,
				  ol4_len + il3_len + il4_len + cfg_payload_len,
				  cfg_dsfield_outer);
		break;
	}

	switch (cfg_encap_proto) {
	case IPPROTO_UDP:
		build_gue_header(buf + el3_len + ol3_len + ol4_len -
				 sizeof(struct guehdr),
				 cfg_l3_inner == PF_INET ? IPPROTO_IPIP
							 : IPPROTO_IPV6);
		build_udp_header(buf + el3_len + ol3_len,
				 sizeof(struct guehdr) + il3_len + il4_len +
				 cfg_payload_len,
				 cfg_port_gue, cfg_l3_outer);
		break;
	case IPPROTO_GRE:
		build_gre_header(buf + el3_len + ol3_len,
				 cfg_l3_inner == PF_INET ? ETH_P_IP
							 : ETH_P_IPV6);
		break;
	}

	switch (cfg_l3_extra) {
	case PF_INET:
		build_ipv4_header(buf,
				  cfg_l3_outer == PF_INET ? IPPROTO_IPIP
							  : IPPROTO_IPV6,
				  extra_saddr4.sin_addr.s_addr,
				  extra_daddr4.sin_addr.s_addr,
				  ol3_len + ol4_len + il3_len + il4_len +
				  cfg_payload_len, 0);
		break;
	case PF_INET6:
		build_ipv6_header(buf,
				  cfg_l3_outer == PF_INET ? IPPROTO_IPIP
							  : IPPROTO_IPV6,
				  &extra_saddr6, &extra_daddr6,
				  ol3_len + ol4_len + il3_len + il4_len +
				  cfg_payload_len, 0);
		break;
	}

	return el3_len + ol3_len + ol4_len + il3_len + il4_len +
	       cfg_payload_len;
}

/* sender transmits encapsulated over RAW or unencap'd over UDP */
static int setup_tx(void)
{
	int family, fd, ret;

	if (cfg_l3_extra)
		family = cfg_l3_extra;
	else if (cfg_l3_outer)
		family = cfg_l3_outer;
	else
		family = cfg_l3_inner;

	fd = socket(family, SOCK_RAW, IPPROTO_RAW);
	if (fd == -1)
		error(1, errno, "socket tx");

	if (cfg_l3_extra) {
		if (cfg_l3_extra == PF_INET)
			ret = connect(fd, (void *) &extra_daddr4,
				      sizeof(extra_daddr4));
		else
			ret = connect(fd, (void *) &extra_daddr6,
				      sizeof(extra_daddr6));
		if (ret)
			error(1, errno, "connect tx");
	} else if (cfg_l3_outer) {
		/* connect to destination if not encapsulated */
		if (cfg_l3_outer == PF_INET)
			ret = connect(fd, (void *) &out_daddr4,
				      sizeof(out_daddr4));
		else
			ret = connect(fd, (void *) &out_daddr6,
				      sizeof(out_daddr6));
		if (ret)
			error(1, errno, "connect tx");
	} else {
		/* otherwise using loopback */
		if (cfg_l3_inner == PF_INET)
			ret = connect(fd, (void *) &in_daddr4,
				      sizeof(in_daddr4));
		else
			ret = connect(fd, (void *) &in_daddr6,
				      sizeof(in_daddr6));
		if (ret)
			error(1, errno, "connect tx");
	}

	return fd;
}

/* receiver reads unencapsulated UDP */
static int setup_rx(void)
{
	int fd, ret;

	fd = socket(cfg_l3_inner, SOCK_DGRAM, 0);
	if (fd == -1)
		error(1, errno, "socket rx");

	if (cfg_l3_inner == PF_INET)
		ret = bind(fd, (void *) &in_daddr4, sizeof(in_daddr4));
	else
		ret = bind(fd, (void *) &in_daddr6, sizeof(in_daddr6));
	if (ret)
		error(1, errno, "bind rx");

	return fd;
}

static int do_tx(int fd, const char *pkt, int len)
{
	int ret;

	ret = write(fd, pkt, len);
	if (ret == -1)
		error(1, errno, "send");
	if (ret != len)
		error(1, errno, "send: len (%d < %d)\n", ret, len);

	return 1;
}

static int do_poll(int fd, short events, int timeout)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = fd;
	pfd.events = events;

	ret = poll(&pfd, 1, timeout);
	if (ret == -1)
		error(1, errno, "poll");
	if (ret && !(pfd.revents & POLLIN))
		error(1, errno, "poll: unexpected event 0x%x\n", pfd.revents);

	return ret;
}

static int do_rx(int fd)
{
	char rbuf;
	int ret, num = 0;

	while (1) {
		ret = recv(fd, &rbuf, 1, MSG_DONTWAIT);
		if (ret == -1 && errno == EAGAIN)
			break;
		if (ret == -1)
			error(1, errno, "recv");
		if (rbuf != cfg_payload_char)
			error(1, 0, "recv: payload mismatch");
		num++;
	};

	return num;
}

static int do_main(void)
{
	unsigned long tstop, treport, tcur;
	int fdt = -1, fdr = -1, len, tx = 0, rx = 0;

	if (!cfg_only_tx)
		fdr = setup_rx();
	if (!cfg_only_rx)
		fdt = setup_tx();

	len = build_packet();

	tcur = util_gettime();
	treport = tcur + 1000;
	tstop = tcur + (cfg_num_secs * 1000);

	while (1) {
		if (!cfg_only_rx)
			tx += do_tx(fdt, buf, len);

		if (!cfg_only_tx)
			rx += do_rx(fdr);

		if (cfg_num_secs) {
			tcur = util_gettime();
			if (tcur >= tstop)
				break;
			if (tcur >= treport) {
				fprintf(stderr, "pkts: tx=%u rx=%u\n", tx, rx);
				tx = 0;
				rx = 0;
				treport = tcur + 1000;
			}
		} else {
			if (tx == cfg_num_pkt)
				break;
		}
	}

	/* read straggler packets, if any */
	if (rx < tx) {
		tstop = util_gettime() + 100;
		while (rx < tx) {
			tcur = util_gettime();
			if (tcur >= tstop)
				break;

			do_poll(fdr, POLLIN, tstop - tcur);
			rx += do_rx(fdr);
		}
	}

	fprintf(stderr, "pkts: tx=%u rx=%u\n", tx, rx);

	if (fdr != -1 && close(fdr))
		error(1, errno, "close rx");
	if (fdt != -1 && close(fdt))
		error(1, errno, "close tx");

	/*
	 * success (== 0) only if received all packets
	 * unless failure is expected, in which case none must arrive.
	 */
	if (cfg_expect_failure)
		return rx != 0;
	else
		return rx != tx;
}


static void __attribute__((noreturn)) usage(const char *filepath)
{
	fprintf(stderr, "Usage: %s [-e gre|gue|bare|none] [-i 4|6] [-l len] "
			"[-O 4|6] [-o 4|6] [-n num] [-t secs] [-R] [-T] "
			"[-s <osrc> [-d <odst>] [-S <isrc>] [-D <idst>] "
			"[-x <otos>] [-X <itos>] [-f <isport>] [-F]\n",
		filepath);
	exit(1);
}

static void parse_addr(int family, void *addr, const char *optarg)
{
	int ret;

	ret = inet_pton(family, optarg, addr);
	if (ret == -1)
		error(1, errno, "inet_pton");
	if (ret == 0)
		error(1, 0, "inet_pton: bad string");
}

static void parse_addr4(struct sockaddr_in *addr, const char *optarg)
{
	parse_addr(AF_INET, &addr->sin_addr, optarg);
}

static void parse_addr6(struct sockaddr_in6 *addr, const char *optarg)
{
	parse_addr(AF_INET6, &addr->sin6_addr, optarg);
}

static int parse_protocol_family(const char *filepath, const char *optarg)
{
	if (!strcmp(optarg, "4"))
		return PF_INET;
	if (!strcmp(optarg, "6"))
		return PF_INET6;

	usage(filepath);
}

static void parse_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "d:D:e:f:Fhi:l:n:o:O:Rs:S:t:Tx:X:")) != -1) {
		switch (c) {
		case 'd':
			if (cfg_l3_outer == AF_UNSPEC)
				error(1, 0, "-d must be preceded by -o");
			if (cfg_l3_outer == AF_INET)
				parse_addr4(&out_daddr4, optarg);
			else
				parse_addr6(&out_daddr6, optarg);
			break;
		case 'D':
			if (cfg_l3_inner == AF_UNSPEC)
				error(1, 0, "-D must be preceded by -i");
			if (cfg_l3_inner == AF_INET)
				parse_addr4(&in_daddr4, optarg);
			else
				parse_addr6(&in_daddr6, optarg);
			break;
		case 'e':
			if (!strcmp(optarg, "gre"))
				cfg_encap_proto = IPPROTO_GRE;
			else if (!strcmp(optarg, "gue"))
				cfg_encap_proto = IPPROTO_UDP;
			else if (!strcmp(optarg, "bare"))
				cfg_encap_proto = IPPROTO_IPIP;
			else if (!strcmp(optarg, "none"))
				cfg_encap_proto = IPPROTO_IP;	/* == 0 */
			else
				usage(argv[0]);
			break;
		case 'f':
			cfg_src_port = strtol(optarg, NULL, 0);
			break;
		case 'F':
			cfg_expect_failure = true;
			break;
		case 'h':
			usage(argv[0]);
			break;
		case 'i':
			if (!strcmp(optarg, "4"))
				cfg_l3_inner = PF_INET;
			else if (!strcmp(optarg, "6"))
				cfg_l3_inner = PF_INET6;
			else
				usage(argv[0]);
			break;
		case 'l':
			cfg_payload_len = strtol(optarg, NULL, 0);
			break;
		case 'n':
			cfg_num_pkt = strtol(optarg, NULL, 0);
			break;
		case 'o':
			cfg_l3_outer = parse_protocol_family(argv[0], optarg);
			break;
		case 'O':
			cfg_l3_extra = parse_protocol_family(argv[0], optarg);
			break;
		case 'R':
			cfg_only_rx = true;
			break;
		case 's':
			if (cfg_l3_outer == AF_INET)
				parse_addr4(&out_saddr4, optarg);
			else
				parse_addr6(&out_saddr6, optarg);
			break;
		case 'S':
			if (cfg_l3_inner == AF_INET)
				parse_addr4(&in_saddr4, optarg);
			else
				parse_addr6(&in_saddr6, optarg);
			break;
		case 't':
			cfg_num_secs = strtol(optarg, NULL, 0);
			break;
		case 'T':
			cfg_only_tx = true;
			break;
		case 'x':
			cfg_dsfield_outer = strtol(optarg, NULL, 0);
			break;
		case 'X':
			cfg_dsfield_inner = strtol(optarg, NULL, 0);
			break;
		}
	}

	if (cfg_only_rx && cfg_only_tx)
		error(1, 0, "options: cannot combine rx-only and tx-only");

	if (cfg_encap_proto && cfg_l3_outer == AF_UNSPEC)
		error(1, 0, "options: must specify outer with encap");
	else if ((!cfg_encap_proto) && cfg_l3_outer != AF_UNSPEC)
		error(1, 0, "options: cannot combine no-encap and outer");
	else if ((!cfg_encap_proto) && cfg_l3_extra != AF_UNSPEC)
		error(1, 0, "options: cannot combine no-encap and extra");

	if (cfg_l3_inner == AF_UNSPEC)
		cfg_l3_inner = AF_INET6;
	if (cfg_l3_inner == AF_INET6 && cfg_encap_proto == IPPROTO_IPIP)
		cfg_encap_proto = IPPROTO_IPV6;

	/* RFC 6040 4.2:
	 *   on decap, if outer encountered congestion (CE == 0x3),
	 *   but inner cannot encode ECN (NoECT == 0x0), then drop packet.
	 */
	if (((cfg_dsfield_outer & 0x3) == 0x3) &&
	    ((cfg_dsfield_inner & 0x3) == 0x0))
		cfg_expect_failure = true;
}

static void print_opts(void)
{
	if (cfg_l3_inner == PF_INET6) {
		util_printaddr("inner.dest6", (void *) &in_daddr6);
		util_printaddr("inner.source6", (void *) &in_saddr6);
	} else {
		util_printaddr("inner.dest4", (void *) &in_daddr4);
		util_printaddr("inner.source4", (void *) &in_saddr4);
	}

	if (!cfg_l3_outer)
		return;

	fprintf(stderr, "encap proto:   %u\n", cfg_encap_proto);

	if (cfg_l3_outer == PF_INET6) {
		util_printaddr("outer.dest6", (void *) &out_daddr6);
		util_printaddr("outer.source6", (void *) &out_saddr6);
	} else {
		util_printaddr("outer.dest4", (void *) &out_daddr4);
		util_printaddr("outer.source4", (void *) &out_saddr4);
	}

	if (!cfg_l3_extra)
		return;

	if (cfg_l3_outer == PF_INET6) {
		util_printaddr("extra.dest6", (void *) &extra_daddr6);
		util_printaddr("extra.source6", (void *) &extra_saddr6);
	} else {
		util_printaddr("extra.dest4", (void *) &extra_daddr4);
		util_printaddr("extra.source4", (void *) &extra_saddr4);
	}

}

int main(int argc, char **argv)
{
	parse_opts(argc, argv);
	print_opts();
	return do_main();
}
