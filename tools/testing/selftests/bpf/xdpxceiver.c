// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 Intel Corporation. */

/*
 * Some functions in this program are taken from
 * Linux kernel samples/bpf/xdpsock* and modified
 * for use.
 *
 * See test_xsk.sh for detailed information on test topology
 * and prerequisite network setup.
 *
 * This test program contains two threads, each thread is single socket with
 * a unique UMEM. It validates in-order packet delivery and packet content
 * by sending packets to each other.
 *
 * Tests Information:
 * ------------------
 * These selftests test AF_XDP SKB and Native/DRV modes using veth
 * Virtual Ethernet interfaces.
 *
 * The following tests are run:
 *
 * 1. AF_XDP SKB mode
 *    Generic mode XDP is driver independent, used when the driver does
 *    not have support for XDP. Works on any netdevice using sockets and
 *    generic XDP path. XDP hook from netif_receive_skb().
 *    a. nopoll - soft-irq processing
 *    b. poll - using poll() syscall
 *    c. Socket Teardown
 *       Create a Tx and a Rx socket, Tx from one socket, Rx on another. Destroy
 *       both sockets, then repeat multiple times. Only nopoll mode is used
 *    d. Bi-directional sockets
 *       Configure sockets as bi-directional tx/rx sockets, sets up fill and
 *       completion rings on each socket, tx/rx in both directions. Only nopoll
 *       mode is used
 *
 * 2. AF_XDP DRV/Native mode
 *    Works on any netdevice with XDP_REDIRECT support, driver dependent. Processes
 *    packets before SKB allocation. Provides better performance than SKB. Driver
 *    hook available just after DMA of buffer descriptor.
 *    a. nopoll
 *    b. poll
 *    c. Socket Teardown
 *    d. Bi-directional sockets
 *    - Only copy mode is supported because veth does not currently support
 *      zero-copy mode
 *
 * Total tests: 8
 *
 * Flow:
 * -----
 * - Single process spawns two threads: Tx and Rx
 * - Each of these two threads attach to a veth interface within their assigned
 *   namespaces
 * - Each thread Creates one AF_XDP socket connected to a unique umem for each
 *   veth interface
 * - Tx thread Transmits 10k packets from veth<xxxx> to veth<yyyy>
 * - Rx thread verifies if all 10k packets were received and delivered in-order,
 *   and have the right content
 *
 * Enable/disable debug mode:
 * --------------------------
 * To enable L2 - L4 headers and payload dump of each packet on STDOUT, add
 * parameter -D to params array in test_xsk.sh, i.e. params=("-S" "-D")
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <asm/barrier.h>
typedef __u16 __sum16;
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <bpf/xsk.h>
#include "xdpxceiver.h"
#include "../kselftest.h"

static void __exit_with_error(int error, const char *file, const char *func, int line)
{
	ksft_test_result_fail
	    ("[%s:%s:%i]: ERROR: %d/\"%s\"\n", file, func, line, error, strerror(error));
	ksft_exit_xfail();
}

#define exit_with_error(error) __exit_with_error(error, __FILE__, __func__, __LINE__)

#define print_ksft_result(void)\
	(ksft_test_result_pass("PASS: %s %s %s%s\n", uut ? "DRV" : "SKB", opt_poll ? "POLL" :\
			       "NOPOLL", opt_teardown ? "Socket Teardown" : "",\
			       opt_bidi ? "Bi-directional Sockets" : ""))

static void pthread_init_mutex(void)
{
	pthread_mutex_init(&sync_mutex, NULL);
	pthread_mutex_init(&sync_mutex_tx, NULL);
	pthread_cond_init(&signal_rx_condition, NULL);
	pthread_cond_init(&signal_tx_condition, NULL);
}

static void pthread_destroy_mutex(void)
{
	pthread_mutex_destroy(&sync_mutex);
	pthread_mutex_destroy(&sync_mutex_tx);
	pthread_cond_destroy(&signal_rx_condition);
	pthread_cond_destroy(&signal_tx_condition);
}

static void *memset32_htonl(void *dest, u32 val, u32 size)
{
	u32 *ptr = (u32 *)dest;
	int i;

	val = htonl(val);

	for (i = 0; i < (size & (~0x3)); i += 4)
		ptr[i >> 2] = val;

	for (; i < size; i++)
		((char *)dest)[i] = ((char *)&val)[i & 3];

	return dest;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static inline unsigned short from32to16(unsigned int x)
{
	/* add up 16-bit and 16-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

/*
 * Fold a partial checksum
 * This function code has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static inline __u16 csum_fold(__u32 csum)
{
	u32 sum = (__force u32)csum;

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (__force __u16)~sum;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static inline u32 from64to32(u64 x)
{
	/* add up 32-bit and 32-bit for 32+c bit */
	x = (x & 0xffffffff) + (x >> 32);
	/* add up carry.. */
	x = (x & 0xffffffff) + (x >> 32);
	return (u32)x;
}

__u32 csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum);

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
__u32 csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum)
{
	unsigned long long s = (__force u32)sum;

	s += (__force u32)saddr;
	s += (__force u32)daddr;
#ifdef __BIG_ENDIAN__
	s += proto + len;
#else
	s += (proto + len) << 8;
#endif
	return (__force __u32)from64to32(s);
}

/*
 * This function has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static inline __u16
csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

static inline u16 udp_csum(u32 saddr, u32 daddr, u32 len, u8 proto, u16 *udp_pkt)
{
	u32 csum = 0;
	u32 cnt = 0;

	/* udp hdr and data */
	for (; cnt < len; cnt += 2)
		csum += udp_pkt[cnt >> 1];

	return csum_tcpudp_magic(saddr, daddr, len, proto, csum);
}

static void gen_eth_hdr(void *data, struct ethhdr *eth_hdr)
{
	memcpy(eth_hdr->h_dest, ((struct ifobject *)data)->dst_mac, ETH_ALEN);
	memcpy(eth_hdr->h_source, ((struct ifobject *)data)->src_mac, ETH_ALEN);
	eth_hdr->h_proto = htons(ETH_P_IP);
}

static void gen_ip_hdr(void *data, struct iphdr *ip_hdr)
{
	ip_hdr->version = IP_PKT_VER;
	ip_hdr->ihl = 0x5;
	ip_hdr->tos = IP_PKT_TOS;
	ip_hdr->tot_len = htons(IP_PKT_SIZE);
	ip_hdr->id = 0;
	ip_hdr->frag_off = 0;
	ip_hdr->ttl = IPDEFTTL;
	ip_hdr->protocol = IPPROTO_UDP;
	ip_hdr->saddr = ((struct ifobject *)data)->src_ip;
	ip_hdr->daddr = ((struct ifobject *)data)->dst_ip;
	ip_hdr->check = 0;
}

static void gen_udp_hdr(void *data, void *arg, struct udphdr *udp_hdr)
{
	udp_hdr->source = htons(((struct ifobject *)arg)->src_port);
	udp_hdr->dest = htons(((struct ifobject *)arg)->dst_port);
	udp_hdr->len = htons(UDP_PKT_SIZE);
	memset32_htonl(pkt_data + PKT_HDR_SIZE,
		       htonl(((struct generic_data *)data)->seqnum), UDP_PKT_DATA_SIZE);
}

static void gen_udp_csum(struct udphdr *udp_hdr, struct iphdr *ip_hdr)
{
	udp_hdr->check = 0;
	udp_hdr->check =
	    udp_csum(ip_hdr->saddr, ip_hdr->daddr, UDP_PKT_SIZE, IPPROTO_UDP, (u16 *)udp_hdr);
}

static void gen_eth_frame(struct xsk_umem_info *umem, u64 addr)
{
	memcpy(xsk_umem__get_data(umem->buffer, addr), pkt_data, PKT_SIZE);
}

static void xsk_configure_umem(struct ifobject *data, void *buffer, u64 size)
{
	int ret;

	data->umem = calloc(1, sizeof(struct xsk_umem_info));
	if (!data->umem)
		exit_with_error(errno);

	ret = xsk_umem__create(&data->umem->umem, buffer, size,
			       &data->umem->fq, &data->umem->cq, NULL);
	if (ret)
		exit_with_error(ret);

	data->umem->buffer = buffer;
}

static void xsk_populate_fill_ring(struct xsk_umem_info *umem)
{
	int ret, i;
	u32 idx;

	ret = xsk_ring_prod__reserve(&umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);
	if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
		exit_with_error(ret);
	for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
		*xsk_ring_prod__fill_addr(&umem->fq, idx++) = i * XSK_UMEM__DEFAULT_FRAME_SIZE;
	xsk_ring_prod__submit(&umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);
}

static int xsk_configure_socket(struct ifobject *ifobject)
{
	struct xsk_socket_config cfg;
	struct xsk_ring_cons *rxr;
	struct xsk_ring_prod *txr;
	int ret;

	ifobject->xsk = calloc(1, sizeof(struct xsk_socket_info));
	if (!ifobject->xsk)
		exit_with_error(errno);

	ifobject->xsk->umem = ifobject->umem;
	cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
	cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	cfg.libbpf_flags = 0;
	cfg.xdp_flags = opt_xdp_flags;
	cfg.bind_flags = opt_xdp_bind_flags;

	if (!opt_bidi) {
		rxr = (ifobject->fv.vector == rx) ? &ifobject->xsk->rx : NULL;
		txr = (ifobject->fv.vector == tx) ? &ifobject->xsk->tx : NULL;
	} else {
		rxr = &ifobject->xsk->rx;
		txr = &ifobject->xsk->tx;
	}

	ret = xsk_socket__create(&ifobject->xsk->xsk, ifobject->ifname,
				 opt_queue, ifobject->umem->umem, rxr, txr, &cfg);

	if (ret)
		return 1;

	return 0;
}

static struct option long_options[] = {
	{"interface", required_argument, 0, 'i'},
	{"queue", optional_argument, 0, 'q'},
	{"poll", no_argument, 0, 'p'},
	{"xdp-skb", no_argument, 0, 'S'},
	{"xdp-native", no_argument, 0, 'N'},
	{"copy", no_argument, 0, 'c'},
	{"tear-down", no_argument, 0, 'T'},
	{"bidi", optional_argument, 0, 'B'},
	{"debug", optional_argument, 0, 'D'},
	{"tx-pkt-count", optional_argument, 0, 'C'},
	{0, 0, 0, 0}
};

static void usage(const char *prog)
{
	const char *str =
	    "  Usage: %s [OPTIONS]\n"
	    "  Options:\n"
	    "  -i, --interface      Use interface\n"
	    "  -q, --queue=n        Use queue n (default 0)\n"
	    "  -p, --poll           Use poll syscall\n"
	    "  -S, --xdp-skb=n      Use XDP SKB mode\n"
	    "  -N, --xdp-native=n   Enforce XDP DRV (native) mode\n"
	    "  -c, --copy           Force copy mode\n"
	    "  -T, --tear-down      Tear down sockets by repeatedly recreating them\n"
	    "  -B, --bidi           Bi-directional sockets test\n"
	    "  -D, --debug          Debug mode - dump packets L2 - L5\n"
	    "  -C, --tx-pkt-count=n Number of packets to send\n";
	ksft_print_msg(str, prog);
}

static bool switch_namespace(int idx)
{
	char fqns[26] = "/var/run/netns/";
	int nsfd;

	strncat(fqns, ifdict[idx]->nsname, sizeof(fqns) - strlen(fqns) - 1);
	nsfd = open(fqns, O_RDONLY);

	if (nsfd == -1)
		exit_with_error(errno);

	if (setns(nsfd, 0) == -1)
		exit_with_error(errno);

	return true;
}

static void *nsswitchthread(void *args)
{
	if (switch_namespace(((struct targs *)args)->idx)) {
		ifdict[((struct targs *)args)->idx]->ifindex =
		    if_nametoindex(ifdict[((struct targs *)args)->idx]->ifname);
		if (!ifdict[((struct targs *)args)->idx]->ifindex) {
			ksft_test_result_fail
			    ("ERROR: [%s] interface \"%s\" does not exist\n",
			     __func__, ifdict[((struct targs *)args)->idx]->ifname);
			((struct targs *)args)->retptr = false;
		} else {
			ksft_print_msg("Interface found: %s\n",
				       ifdict[((struct targs *)args)->idx]->ifname);
			((struct targs *)args)->retptr = true;
		}
	} else {
		((struct targs *)args)->retptr = false;
	}
	pthread_exit(NULL);
}

static int validate_interfaces(void)
{
	bool ret = true;

	for (int i = 0; i < MAX_INTERFACES; i++) {
		if (!strcmp(ifdict[i]->ifname, "")) {
			ret = false;
			ksft_test_result_fail("ERROR: interfaces: -i <int>,<ns> -i <int>,<ns>.");
		}
		if (strcmp(ifdict[i]->nsname, "")) {
			struct targs *targs;

			targs = (struct targs *)malloc(sizeof(struct targs));
			if (!targs)
				exit_with_error(errno);

			targs->idx = i;
			if (pthread_create(&ns_thread, NULL, nsswitchthread, (void *)targs))
				exit_with_error(errno);

			pthread_join(ns_thread, NULL);

			if (targs->retptr)
				ksft_print_msg("NS switched: %s\n", ifdict[i]->nsname);

			free(targs);
		} else {
			ifdict[i]->ifindex = if_nametoindex(ifdict[i]->ifname);
			if (!ifdict[i]->ifindex) {
				ksft_test_result_fail
				    ("ERROR: interface \"%s\" does not exist\n", ifdict[i]->ifname);
				ret = false;
			} else {
				ksft_print_msg("Interface found: %s\n", ifdict[i]->ifname);
			}
		}
	}
	return ret;
}

static void parse_command_line(int argc, char **argv)
{
	int option_index, interface_index = 0, c;

	opterr = 0;

	for (;;) {
		c = getopt_long(argc, argv, "i:q:pSNcTBDC:", long_options, &option_index);

		if (c == -1)
			break;

		switch (c) {
		case 'i':
			if (interface_index == MAX_INTERFACES)
				break;
			char *sptr, *token;

			sptr = strndupa(optarg, strlen(optarg));
			memcpy(ifdict[interface_index]->ifname,
			       strsep(&sptr, ","), MAX_INTERFACE_NAME_CHARS);
			token = strsep(&sptr, ",");
			if (token)
				memcpy(ifdict[interface_index]->nsname, token,
				       MAX_INTERFACES_NAMESPACE_CHARS);
			interface_index++;
			break;
		case 'q':
			opt_queue = atoi(optarg);
			break;
		case 'p':
			opt_poll = 1;
			break;
		case 'S':
			opt_xdp_flags |= XDP_FLAGS_SKB_MODE;
			opt_xdp_bind_flags |= XDP_COPY;
			uut = ORDER_CONTENT_VALIDATE_XDP_SKB;
			break;
		case 'N':
			opt_xdp_flags |= XDP_FLAGS_DRV_MODE;
			opt_xdp_bind_flags |= XDP_COPY;
			uut = ORDER_CONTENT_VALIDATE_XDP_DRV;
			break;
		case 'c':
			opt_xdp_bind_flags |= XDP_COPY;
			break;
		case 'T':
			opt_teardown = 1;
			break;
		case 'B':
			opt_bidi = 1;
			break;
		case 'D':
			debug_pkt_dump = 1;
			break;
		case 'C':
			opt_pkt_count = atoi(optarg);
			break;
		default:
			usage(basename(argv[0]));
			ksft_exit_xfail();
		}
	}

	if (!validate_interfaces()) {
		usage(basename(argv[0]));
		ksft_exit_xfail();
	}
}

static void kick_tx(struct xsk_socket_info *xsk)
{
	int ret;

	ret = sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
	if (ret >= 0 || errno == ENOBUFS || errno == EAGAIN || errno == EBUSY || errno == ENETDOWN)
		return;
	exit_with_error(errno);
}

static inline void complete_tx_only(struct xsk_socket_info *xsk, int batch_size)
{
	unsigned int rcvd;
	u32 idx;

	if (!xsk->outstanding_tx)
		return;

	if (!NEED_WAKEUP || xsk_ring_prod__needs_wakeup(&xsk->tx))
		kick_tx(xsk);

	rcvd = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx);
	if (rcvd) {
		xsk_ring_cons__release(&xsk->umem->cq, rcvd);
		xsk->outstanding_tx -= rcvd;
		xsk->tx_npkts += rcvd;
	}
}

static void rx_pkt(struct xsk_socket_info *xsk, struct pollfd *fds)
{
	unsigned int rcvd, i;
	u32 idx_rx = 0, idx_fq = 0;
	int ret;

	rcvd = xsk_ring_cons__peek(&xsk->rx, BATCH_SIZE, &idx_rx);
	if (!rcvd) {
		if (xsk_ring_prod__needs_wakeup(&xsk->umem->fq)) {
			ret = poll(fds, 1, POLL_TMOUT);
			if (ret < 0)
				exit_with_error(ret);
		}
		return;
	}

	ret = xsk_ring_prod__reserve(&xsk->umem->fq, rcvd, &idx_fq);
	while (ret != rcvd) {
		if (ret < 0)
			exit_with_error(ret);
		if (xsk_ring_prod__needs_wakeup(&xsk->umem->fq)) {
			ret = poll(fds, 1, POLL_TMOUT);
			if (ret < 0)
				exit_with_error(ret);
		}
		ret = xsk_ring_prod__reserve(&xsk->umem->fq, rcvd, &idx_fq);
	}

	for (i = 0; i < rcvd; i++) {
		u64 addr = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx)->addr;
		(void)xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++)->len;
		u64 orig = xsk_umem__extract_addr(addr);

		addr = xsk_umem__add_offset_to_addr(addr);
		pkt_node_rx = malloc(sizeof(struct pkt) + PKT_SIZE);
		if (!pkt_node_rx)
			exit_with_error(errno);

		pkt_node_rx->pkt_frame = (char *)malloc(PKT_SIZE);
		if (!pkt_node_rx->pkt_frame)
			exit_with_error(errno);

		memcpy(pkt_node_rx->pkt_frame, xsk_umem__get_data(xsk->umem->buffer, addr),
		       PKT_SIZE);

		TAILQ_INSERT_HEAD(&head, pkt_node_rx, pkt_nodes);

		*xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) = orig;
	}

	xsk_ring_prod__submit(&xsk->umem->fq, rcvd);
	xsk_ring_cons__release(&xsk->rx, rcvd);
	xsk->rx_npkts += rcvd;
}

static void tx_only(struct xsk_socket_info *xsk, u32 *frameptr, int batch_size)
{
	u32 idx;
	unsigned int i;

	while (xsk_ring_prod__reserve(&xsk->tx, batch_size, &idx) < batch_size)
		complete_tx_only(xsk, batch_size);

	for (i = 0; i < batch_size; i++) {
		struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, idx + i);

		tx_desc->addr = (*frameptr + i) << XSK_UMEM__DEFAULT_FRAME_SHIFT;
		tx_desc->len = PKT_SIZE;
	}

	xsk_ring_prod__submit(&xsk->tx, batch_size);
	xsk->outstanding_tx += batch_size;
	*frameptr += batch_size;
	*frameptr %= num_frames;
	complete_tx_only(xsk, batch_size);
}

static inline int get_batch_size(int pkt_cnt)
{
	if (!opt_pkt_count)
		return BATCH_SIZE;

	if (pkt_cnt + BATCH_SIZE <= opt_pkt_count)
		return BATCH_SIZE;

	return opt_pkt_count - pkt_cnt;
}

static void complete_tx_only_all(void *arg)
{
	bool pending;

	do {
		pending = false;
		if (((struct ifobject *)arg)->xsk->outstanding_tx) {
			complete_tx_only(((struct ifobject *)
					  arg)->xsk, BATCH_SIZE);
			pending = !!((struct ifobject *)arg)->xsk->outstanding_tx;
		}
	} while (pending);
}

static void tx_only_all(void *arg)
{
	struct pollfd fds[MAX_SOCKS] = { };
	u32 frame_nb = 0;
	int pkt_cnt = 0;
	int ret;

	fds[0].fd = xsk_socket__fd(((struct ifobject *)arg)->xsk->xsk);
	fds[0].events = POLLOUT;

	while ((opt_pkt_count && pkt_cnt < opt_pkt_count) || !opt_pkt_count) {
		int batch_size = get_batch_size(pkt_cnt);

		if (opt_poll) {
			ret = poll(fds, 1, POLL_TMOUT);
			if (ret <= 0)
				continue;

			if (!(fds[0].revents & POLLOUT))
				continue;
		}

		tx_only(((struct ifobject *)arg)->xsk, &frame_nb, batch_size);
		pkt_cnt += batch_size;
	}

	if (opt_pkt_count)
		complete_tx_only_all(arg);
}

static void worker_pkt_dump(void)
{
	struct in_addr ipaddr;

	fprintf(stdout, "---------------------------------------\n");
	for (int iter = 0; iter < num_frames - 1; iter++) {
		/*extract L2 frame */
		fprintf(stdout, "DEBUG>> L2: dst mac: ");
		for (int i = 0; i < ETH_ALEN; i++)
			fprintf(stdout, "%02X", ((struct ethhdr *)
						 pkt_buf[iter]->payload)->h_dest[i]);

		fprintf(stdout, "\nDEBUG>> L2: src mac: ");
		for (int i = 0; i < ETH_ALEN; i++)
			fprintf(stdout, "%02X", ((struct ethhdr *)
						 pkt_buf[iter]->payload)->h_source[i]);

		/*extract L3 frame */
		fprintf(stdout, "\nDEBUG>> L3: ip_hdr->ihl: %02X\n",
			((struct iphdr *)(pkt_buf[iter]->payload + sizeof(struct ethhdr)))->ihl);

		ipaddr.s_addr =
		    ((struct iphdr *)(pkt_buf[iter]->payload + sizeof(struct ethhdr)))->saddr;
		fprintf(stdout, "DEBUG>> L3: ip_hdr->saddr: %s\n", inet_ntoa(ipaddr));

		ipaddr.s_addr =
		    ((struct iphdr *)(pkt_buf[iter]->payload + sizeof(struct ethhdr)))->daddr;
		fprintf(stdout, "DEBUG>> L3: ip_hdr->daddr: %s\n", inet_ntoa(ipaddr));

		/*extract L4 frame */
		fprintf(stdout, "DEBUG>> L4: udp_hdr->src: %d\n",
			ntohs(((struct udphdr *)(pkt_buf[iter]->payload +
						 sizeof(struct ethhdr) +
						 sizeof(struct iphdr)))->source));

		fprintf(stdout, "DEBUG>> L4: udp_hdr->dst: %d\n",
			ntohs(((struct udphdr *)(pkt_buf[iter]->payload +
						 sizeof(struct ethhdr) +
						 sizeof(struct iphdr)))->dest));
		/*extract L5 frame */
		int payload = *((uint32_t *)(pkt_buf[iter]->payload + PKT_HDR_SIZE));

		if (payload == EOT) {
			ksft_print_msg("End-of-tranmission frame received\n");
			fprintf(stdout, "---------------------------------------\n");
			break;
		}
		fprintf(stdout, "DEBUG>> L5: payload: %d\n", payload);
		fprintf(stdout, "---------------------------------------\n");
	}
}

static void worker_pkt_validate(void)
{
	u32 payloadseqnum = -2;

	while (1) {
		pkt_node_rx_q = malloc(sizeof(struct pkt));
		pkt_node_rx_q = TAILQ_LAST(&head, head_s);
		if (!pkt_node_rx_q)
			break;
		/*do not increment pktcounter if !(tos=0x9 and ipv4) */
		if ((((struct iphdr *)(pkt_node_rx_q->pkt_frame +
				       sizeof(struct ethhdr)))->version == IP_PKT_VER)
		    && (((struct iphdr *)(pkt_node_rx_q->pkt_frame + sizeof(struct ethhdr)))->tos ==
			IP_PKT_TOS)) {
			payloadseqnum = *((uint32_t *) (pkt_node_rx_q->pkt_frame + PKT_HDR_SIZE));
			if (debug_pkt_dump && payloadseqnum != EOT) {
				pkt_obj = (struct pkt_frame *)malloc(sizeof(struct pkt_frame));
				pkt_obj->payload = (char *)malloc(PKT_SIZE);
				memcpy(pkt_obj->payload, pkt_node_rx_q->pkt_frame, PKT_SIZE);
				pkt_buf[payloadseqnum] = pkt_obj;
			}

			if (payloadseqnum == EOT) {
				ksft_print_msg("End-of-tranmission frame received: PASS\n");
				sigvar = 1;
				break;
			}

			if (prev_pkt + 1 != payloadseqnum) {
				ksft_test_result_fail
				    ("ERROR: [%s] prev_pkt [%d], payloadseqnum [%d]\n",
				     __func__, prev_pkt, payloadseqnum);
				ksft_exit_xfail();
			}

			TAILQ_REMOVE(&head, pkt_node_rx_q, pkt_nodes);
			free(pkt_node_rx_q->pkt_frame);
			free(pkt_node_rx_q);
			pkt_node_rx_q = NULL;
			prev_pkt = payloadseqnum;
			pkt_counter++;
		} else {
			ksft_print_msg("Invalid frame received: ");
			ksft_print_msg("[IP_PKT_VER: %02X], [IP_PKT_TOS: %02X]\n",
				((struct iphdr *)(pkt_node_rx_q->pkt_frame +
				       sizeof(struct ethhdr)))->version,
				((struct iphdr *)(pkt_node_rx_q->pkt_frame +
				       sizeof(struct ethhdr)))->tos);
			TAILQ_REMOVE(&head, pkt_node_rx_q, pkt_nodes);
			free(pkt_node_rx_q->pkt_frame);
			free(pkt_node_rx_q);
			pkt_node_rx_q = NULL;
		}
	}
}

static void thread_common_ops(void *arg, void *bufs, pthread_mutex_t *mutexptr,
			      atomic_int *spinningptr)
{
	int ctr = 0;
	int ret;

	xsk_configure_umem((struct ifobject *)arg, bufs, num_frames * XSK_UMEM__DEFAULT_FRAME_SIZE);
	ret = xsk_configure_socket((struct ifobject *)arg);

	/* Retry Create Socket if it fails as xsk_socket__create()
	 * is asynchronous
	 *
	 * Essential to lock Mutex here to prevent Tx thread from
	 * entering before Rx and causing a deadlock
	 */
	pthread_mutex_lock(mutexptr);
	while (ret && ctr < SOCK_RECONF_CTR) {
		atomic_store(spinningptr, 1);
		xsk_configure_umem((struct ifobject *)arg,
				   bufs, num_frames * XSK_UMEM__DEFAULT_FRAME_SIZE);
		ret = xsk_configure_socket((struct ifobject *)arg);
		usleep(USLEEP_MAX);
		ctr++;
	}
	atomic_store(spinningptr, 0);
	pthread_mutex_unlock(mutexptr);

	if (ctr >= SOCK_RECONF_CTR)
		exit_with_error(ret);
}

static void *worker_testapp_validate(void *arg)
{
	struct udphdr *udp_hdr =
	    (struct udphdr *)(pkt_data + sizeof(struct ethhdr) + sizeof(struct iphdr));
	struct generic_data *data = (struct generic_data *)malloc(sizeof(struct generic_data));
	struct iphdr *ip_hdr = (struct iphdr *)(pkt_data + sizeof(struct ethhdr));
	struct ethhdr *eth_hdr = (struct ethhdr *)pkt_data;
	void *bufs = NULL;

	pthread_attr_setstacksize(&attr, THREAD_STACK);

	if (!bidi_pass) {
		bufs = mmap(NULL, num_frames * XSK_UMEM__DEFAULT_FRAME_SIZE,
			    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (bufs == MAP_FAILED)
			exit_with_error(errno);

		if (strcmp(((struct ifobject *)arg)->nsname, ""))
			switch_namespace(((struct ifobject *)arg)->ifdict_index);
	}

	if (((struct ifobject *)arg)->fv.vector == tx) {
		int spinningrxctr = 0;

		if (!bidi_pass)
			thread_common_ops(arg, bufs, &sync_mutex_tx, &spinning_tx);

		while (atomic_load(&spinning_rx) && spinningrxctr < SOCK_RECONF_CTR) {
			spinningrxctr++;
			usleep(USLEEP_MAX);
		}

		ksft_print_msg("Interface [%s] vector [Tx]\n", ((struct ifobject *)arg)->ifname);
		for (int i = 0; i < num_frames; i++) {
			/*send EOT frame */
			if (i == (num_frames - 1))
				data->seqnum = -1;
			else
				data->seqnum = i;
			gen_udp_hdr((void *)data, (void *)arg, udp_hdr);
			gen_ip_hdr((void *)arg, ip_hdr);
			gen_udp_csum(udp_hdr, ip_hdr);
			gen_eth_hdr((void *)arg, eth_hdr);
			gen_eth_frame(((struct ifobject *)arg)->umem,
				      i * XSK_UMEM__DEFAULT_FRAME_SIZE);
		}

		free(data);
		ksft_print_msg("Sending %d packets on interface %s\n",
			       (opt_pkt_count - 1), ((struct ifobject *)arg)->ifname);
		tx_only_all(arg);
	} else if (((struct ifobject *)arg)->fv.vector == rx) {
		struct pollfd fds[MAX_SOCKS] = { };
		int ret;

		if (!bidi_pass)
			thread_common_ops(arg, bufs, &sync_mutex_tx, &spinning_rx);

		ksft_print_msg("Interface [%s] vector [Rx]\n", ((struct ifobject *)arg)->ifname);
		xsk_populate_fill_ring(((struct ifobject *)arg)->umem);

		TAILQ_INIT(&head);
		if (debug_pkt_dump) {
			pkt_buf = malloc(sizeof(struct pkt_frame **) * num_frames);
			if (!pkt_buf)
				exit_with_error(errno);
		}

		fds[0].fd = xsk_socket__fd(((struct ifobject *)arg)->xsk->xsk);
		fds[0].events = POLLIN;

		pthread_mutex_lock(&sync_mutex);
		pthread_cond_signal(&signal_rx_condition);
		pthread_mutex_unlock(&sync_mutex);

		while (1) {
			if (opt_poll) {
				ret = poll(fds, 1, POLL_TMOUT);
				if (ret <= 0)
					continue;
			}
			rx_pkt(((struct ifobject *)arg)->xsk, fds);
			worker_pkt_validate();

			if (sigvar)
				break;
		}

		ksft_print_msg("Received %d packets on interface %s\n",
			       pkt_counter, ((struct ifobject *)arg)->ifname);

		if (opt_teardown)
			ksft_print_msg("Destroying socket\n");
	}

	if (!opt_bidi || (opt_bidi && bidi_pass)) {
		xsk_socket__delete(((struct ifobject *)arg)->xsk->xsk);
		(void)xsk_umem__delete(((struct ifobject *)arg)->umem->umem);
	}
	pthread_exit(NULL);
}

static void testapp_validate(void)
{
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, THREAD_STACK);

	if (opt_bidi && bidi_pass) {
		pthread_init_mutex();
		if (!switching_notify) {
			ksft_print_msg("Switching Tx/Rx vectors\n");
			switching_notify++;
		}
	}

	pthread_mutex_lock(&sync_mutex);

	/*Spawn RX thread */
	if (!opt_bidi || (opt_bidi && !bidi_pass)) {
		if (pthread_create(&t0, &attr, worker_testapp_validate, (void *)ifdict[1]))
			exit_with_error(errno);
	} else if (opt_bidi && bidi_pass) {
		/*switch Tx/Rx vectors */
		ifdict[0]->fv.vector = rx;
		if (pthread_create(&t0, &attr, worker_testapp_validate, (void *)ifdict[0]))
			exit_with_error(errno);
	}

	struct timespec max_wait = { 0, 0 };

	if (clock_gettime(CLOCK_REALTIME, &max_wait))
		exit_with_error(errno);
	max_wait.tv_sec += TMOUT_SEC;

	if (pthread_cond_timedwait(&signal_rx_condition, &sync_mutex, &max_wait) == ETIMEDOUT)
		exit_with_error(errno);

	pthread_mutex_unlock(&sync_mutex);

	/*Spawn TX thread */
	if (!opt_bidi || (opt_bidi && !bidi_pass)) {
		if (pthread_create(&t1, &attr, worker_testapp_validate, (void *)ifdict[0]))
			exit_with_error(errno);
	} else if (opt_bidi && bidi_pass) {
		/*switch Tx/Rx vectors */
		ifdict[1]->fv.vector = tx;
		if (pthread_create(&t1, &attr, worker_testapp_validate, (void *)ifdict[1]))
			exit_with_error(errno);
	}

	pthread_join(t1, NULL);
	pthread_join(t0, NULL);

	if (debug_pkt_dump) {
		worker_pkt_dump();
		for (int iter = 0; iter < num_frames - 1; iter++) {
			free(pkt_buf[iter]->payload);
			free(pkt_buf[iter]);
		}
		free(pkt_buf);
	}

	if (!opt_teardown && !opt_bidi)
		print_ksft_result();
}

static void testapp_sockets(void)
{
	for (int i = 0; i < (opt_teardown ? MAX_TEARDOWN_ITER : MAX_BIDI_ITER); i++) {
		pkt_counter = 0;
		prev_pkt = -1;
		sigvar = 0;
		ksft_print_msg("Creating socket\n");
		testapp_validate();
		opt_bidi ? bidi_pass++ : bidi_pass;
	}

	print_ksft_result();
}

static void init_iface_config(void *ifaceconfig)
{
	/*Init interface0 */
	ifdict[0]->fv.vector = tx;
	memcpy(ifdict[0]->dst_mac, ((struct ifaceconfigobj *)ifaceconfig)->dst_mac, ETH_ALEN);
	memcpy(ifdict[0]->src_mac, ((struct ifaceconfigobj *)ifaceconfig)->src_mac, ETH_ALEN);
	ifdict[0]->dst_ip = ((struct ifaceconfigobj *)ifaceconfig)->dst_ip.s_addr;
	ifdict[0]->src_ip = ((struct ifaceconfigobj *)ifaceconfig)->src_ip.s_addr;
	ifdict[0]->dst_port = ((struct ifaceconfigobj *)ifaceconfig)->dst_port;
	ifdict[0]->src_port = ((struct ifaceconfigobj *)ifaceconfig)->src_port;

	/*Init interface1 */
	ifdict[1]->fv.vector = rx;
	memcpy(ifdict[1]->dst_mac, ((struct ifaceconfigobj *)ifaceconfig)->src_mac, ETH_ALEN);
	memcpy(ifdict[1]->src_mac, ((struct ifaceconfigobj *)ifaceconfig)->dst_mac, ETH_ALEN);
	ifdict[1]->dst_ip = ((struct ifaceconfigobj *)ifaceconfig)->src_ip.s_addr;
	ifdict[1]->src_ip = ((struct ifaceconfigobj *)ifaceconfig)->dst_ip.s_addr;
	ifdict[1]->dst_port = ((struct ifaceconfigobj *)ifaceconfig)->src_port;
	ifdict[1]->src_port = ((struct ifaceconfigobj *)ifaceconfig)->dst_port;
}

int main(int argc, char **argv)
{
	struct rlimit _rlim = { RLIM_INFINITY, RLIM_INFINITY };

	if (setrlimit(RLIMIT_MEMLOCK, &_rlim))
		exit_with_error(errno);

	const char *MAC1 = "\x00\x0A\x56\x9E\xEE\x62";
	const char *MAC2 = "\x00\x0A\x56\x9E\xEE\x61";
	const char *IP1 = "192.168.100.162";
	const char *IP2 = "192.168.100.161";
	u16 UDP_DST_PORT = 2020;
	u16 UDP_SRC_PORT = 2121;

	ifaceconfig = (struct ifaceconfigobj *)malloc(sizeof(struct ifaceconfigobj));
	memcpy(ifaceconfig->dst_mac, MAC1, ETH_ALEN);
	memcpy(ifaceconfig->src_mac, MAC2, ETH_ALEN);
	inet_aton(IP1, &ifaceconfig->dst_ip);
	inet_aton(IP2, &ifaceconfig->src_ip);
	ifaceconfig->dst_port = UDP_DST_PORT;
	ifaceconfig->src_port = UDP_SRC_PORT;

	for (int i = 0; i < MAX_INTERFACES; i++) {
		ifdict[i] = (struct ifobject *)malloc(sizeof(struct ifobject));
		if (!ifdict[i])
			exit_with_error(errno);

		ifdict[i]->ifdict_index = i;
	}

	setlocale(LC_ALL, "");

	parse_command_line(argc, argv);

	num_frames = ++opt_pkt_count;

	init_iface_config((void *)ifaceconfig);

	pthread_init_mutex();

	ksft_set_plan(1);

	if (!opt_teardown && !opt_bidi) {
		testapp_validate();
	} else if (opt_teardown && opt_bidi) {
		ksft_test_result_fail("ERROR: parameters -T and -B cannot be used together\n");
		ksft_exit_xfail();
	} else {
		testapp_sockets();
	}

	for (int i = 0; i < MAX_INTERFACES; i++)
		free(ifdict[i]);

	pthread_destroy_mutex();

	ksft_exit_pass();

	return 0;
}
