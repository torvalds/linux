// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "xdp_metadata.skel.h"
#include "xdp_metadata2.skel.h"
#include "xdp_metadata.h"
#include "xsk.h"

#include <bpf/btf.h>
#include <linux/errqueue.h>
#include <linux/if_link.h>
#include <linux/net_tstamp.h>
#include <linux/udp.h>
#include <sys/mman.h>
#include <net/if.h>
#include <poll.h>

#define TX_NAME "veTX"
#define RX_NAME "veRX"

#define UDP_PAYLOAD_BYTES 4

#define AF_XDP_SOURCE_PORT 1234
#define AF_XDP_CONSUMER_PORT 8080

#define UMEM_NUM 16
#define UMEM_FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define UMEM_SIZE (UMEM_FRAME_SIZE * UMEM_NUM)
#define XDP_FLAGS XDP_FLAGS_DRV_MODE
#define QUEUE_ID 0

#define TX_ADDR "10.0.0.1"
#define RX_ADDR "10.0.0.2"
#define PREFIX_LEN "8"
#define FAMILY AF_INET

struct xsk {
	void *umem_area;
	struct xsk_umem *umem;
	struct xsk_ring_prod fill;
	struct xsk_ring_cons comp;
	struct xsk_ring_prod tx;
	struct xsk_ring_cons rx;
	struct xsk_socket *socket;
};

static int open_xsk(int ifindex, struct xsk *xsk)
{
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	const struct xsk_socket_config socket_config = {
		.rx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.bind_flags = XDP_COPY,
	};
	const struct xsk_umem_config umem_config = {
		.fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE,
		.flags = XDP_UMEM_UNALIGNED_CHUNK_FLAG,
	};
	__u32 idx;
	u64 addr;
	int ret;
	int i;

	xsk->umem_area = mmap(NULL, UMEM_SIZE, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (!ASSERT_NEQ(xsk->umem_area, MAP_FAILED, "mmap"))
		return -1;

	ret = xsk_umem__create(&xsk->umem,
			       xsk->umem_area, UMEM_SIZE,
			       &xsk->fill,
			       &xsk->comp,
			       &umem_config);
	if (!ASSERT_OK(ret, "xsk_umem__create"))
		return ret;

	ret = xsk_socket__create(&xsk->socket, ifindex, QUEUE_ID,
				 xsk->umem,
				 &xsk->rx,
				 &xsk->tx,
				 &socket_config);
	if (!ASSERT_OK(ret, "xsk_socket__create"))
		return ret;

	/* First half of umem is for TX. This way address matches 1-to-1
	 * to the completion queue index.
	 */

	for (i = 0; i < UMEM_NUM / 2; i++) {
		addr = i * UMEM_FRAME_SIZE;
		printf("%p: tx_desc[%d] -> %lx\n", xsk, i, addr);
	}

	/* Second half of umem is for RX. */

	ret = xsk_ring_prod__reserve(&xsk->fill, UMEM_NUM / 2, &idx);
	if (!ASSERT_EQ(UMEM_NUM / 2, ret, "xsk_ring_prod__reserve"))
		return ret;
	if (!ASSERT_EQ(idx, 0, "fill idx != 0"))
		return -1;

	for (i = 0; i < UMEM_NUM / 2; i++) {
		addr = (UMEM_NUM / 2 + i) * UMEM_FRAME_SIZE;
		printf("%p: rx_desc[%d] -> %lx\n", xsk, i, addr);
		*xsk_ring_prod__fill_addr(&xsk->fill, i) = addr;
	}
	xsk_ring_prod__submit(&xsk->fill, ret);

	return 0;
}

static void close_xsk(struct xsk *xsk)
{
	if (xsk->umem)
		xsk_umem__delete(xsk->umem);
	if (xsk->socket)
		xsk_socket__delete(xsk->socket);
	munmap(xsk->umem_area, UMEM_SIZE);
}

static void ip_csum(struct iphdr *iph)
{
	__u32 sum = 0;
	__u16 *p;
	int i;

	iph->check = 0;
	p = (void *)iph;
	for (i = 0; i < sizeof(*iph) / sizeof(*p); i++)
		sum += p[i];

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	iph->check = ~sum;
}

static int generate_packet(struct xsk *xsk, __u16 dst_port)
{
	struct xdp_desc *tx_desc;
	struct udphdr *udph;
	struct ethhdr *eth;
	struct iphdr *iph;
	void *data;
	__u32 idx;
	int ret;

	ret = xsk_ring_prod__reserve(&xsk->tx, 1, &idx);
	if (!ASSERT_EQ(ret, 1, "xsk_ring_prod__reserve"))
		return -1;

	tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, idx);
	tx_desc->addr = idx % (UMEM_NUM / 2) * UMEM_FRAME_SIZE;
	printf("%p: tx_desc[%u]->addr=%llx\n", xsk, idx, tx_desc->addr);
	data = xsk_umem__get_data(xsk->umem_area, tx_desc->addr);

	eth = data;
	iph = (void *)(eth + 1);
	udph = (void *)(iph + 1);

	memcpy(eth->h_dest, "\x00\x00\x00\x00\x00\x02", ETH_ALEN);
	memcpy(eth->h_source, "\x00\x00\x00\x00\x00\x01", ETH_ALEN);
	eth->h_proto = htons(ETH_P_IP);

	iph->version = 0x4;
	iph->ihl = 0x5;
	iph->tos = 0x9;
	iph->tot_len = htons(sizeof(*iph) + sizeof(*udph) + UDP_PAYLOAD_BYTES);
	iph->id = 0;
	iph->frag_off = 0;
	iph->ttl = 0;
	iph->protocol = IPPROTO_UDP;
	ASSERT_EQ(inet_pton(FAMILY, TX_ADDR, &iph->saddr), 1, "inet_pton(TX_ADDR)");
	ASSERT_EQ(inet_pton(FAMILY, RX_ADDR, &iph->daddr), 1, "inet_pton(RX_ADDR)");
	ip_csum(iph);

	udph->source = htons(AF_XDP_SOURCE_PORT);
	udph->dest = htons(dst_port);
	udph->len = htons(sizeof(*udph) + UDP_PAYLOAD_BYTES);
	udph->check = 0;

	memset(udph + 1, 0xAA, UDP_PAYLOAD_BYTES);

	tx_desc->len = sizeof(*eth) + sizeof(*iph) + sizeof(*udph) + UDP_PAYLOAD_BYTES;
	xsk_ring_prod__submit(&xsk->tx, 1);

	ret = sendto(xsk_socket__fd(xsk->socket), NULL, 0, MSG_DONTWAIT, NULL, 0);
	if (!ASSERT_GE(ret, 0, "sendto"))
		return ret;

	return 0;
}

static void complete_tx(struct xsk *xsk)
{
	__u32 idx;
	__u64 addr;

	if (ASSERT_EQ(xsk_ring_cons__peek(&xsk->comp, 1, &idx), 1, "xsk_ring_cons__peek")) {
		addr = *xsk_ring_cons__comp_addr(&xsk->comp, idx);

		printf("%p: complete tx idx=%u addr=%llx\n", xsk, idx, addr);
		xsk_ring_cons__release(&xsk->comp, 1);
	}
}

static void refill_rx(struct xsk *xsk, __u64 addr)
{
	__u32 idx;

	if (ASSERT_EQ(xsk_ring_prod__reserve(&xsk->fill, 1, &idx), 1, "xsk_ring_prod__reserve")) {
		printf("%p: complete idx=%u addr=%llx\n", xsk, idx, addr);
		*xsk_ring_prod__fill_addr(&xsk->fill, idx) = addr;
		xsk_ring_prod__submit(&xsk->fill, 1);
	}
}

static int verify_xsk_metadata(struct xsk *xsk)
{
	const struct xdp_desc *rx_desc;
	struct pollfd fds = {};
	struct xdp_meta *meta;
	struct ethhdr *eth;
	struct iphdr *iph;
	__u64 comp_addr;
	void *data;
	__u64 addr;
	__u32 idx = 0;
	int ret;

	ret = recvfrom(xsk_socket__fd(xsk->socket), NULL, 0, MSG_DONTWAIT, NULL, NULL);
	if (!ASSERT_EQ(ret, 0, "recvfrom"))
		return -1;

	fds.fd = xsk_socket__fd(xsk->socket);
	fds.events = POLLIN;

	ret = poll(&fds, 1, 1000);
	if (!ASSERT_GT(ret, 0, "poll"))
		return -1;

	ret = xsk_ring_cons__peek(&xsk->rx, 1, &idx);
	if (!ASSERT_EQ(ret, 1, "xsk_ring_cons__peek"))
		return -2;

	rx_desc = xsk_ring_cons__rx_desc(&xsk->rx, idx);
	comp_addr = xsk_umem__extract_addr(rx_desc->addr);
	addr = xsk_umem__add_offset_to_addr(rx_desc->addr);
	printf("%p: rx_desc[%u]->addr=%llx addr=%llx comp_addr=%llx\n",
	       xsk, idx, rx_desc->addr, addr, comp_addr);
	data = xsk_umem__get_data(xsk->umem_area, addr);

	/* Make sure we got the packet offset correctly. */

	eth = data;
	ASSERT_EQ(eth->h_proto, htons(ETH_P_IP), "eth->h_proto");
	iph = (void *)(eth + 1);
	ASSERT_EQ((int)iph->version, 4, "iph->version");

	/* custom metadata */

	meta = data - sizeof(struct xdp_meta);

	if (!ASSERT_NEQ(meta->rx_timestamp, 0, "rx_timestamp"))
		return -1;

	if (!ASSERT_NEQ(meta->rx_hash, 0, "rx_hash"))
		return -1;

	ASSERT_EQ(meta->rx_hash_type, 0, "rx_hash_type");

	xsk_ring_cons__release(&xsk->rx, 1);
	refill_rx(xsk, comp_addr);

	return 0;
}

void test_xdp_metadata(void)
{
	struct xdp_metadata2 *bpf_obj2 = NULL;
	struct xdp_metadata *bpf_obj = NULL;
	struct bpf_program *new_prog, *prog;
	struct nstoken *tok = NULL;
	__u32 queue_id = QUEUE_ID;
	struct bpf_map *prog_arr;
	struct xsk tx_xsk = {};
	struct xsk rx_xsk = {};
	__u32 val, key = 0;
	int retries = 10;
	int rx_ifindex;
	int tx_ifindex;
	int sock_fd;
	int ret;

	/* Setup new networking namespace, with a veth pair. */

	SYS(out, "ip netns add xdp_metadata");
	tok = open_netns("xdp_metadata");
	SYS(out, "ip link add numtxqueues 1 numrxqueues 1 " TX_NAME
	    " type veth peer " RX_NAME " numtxqueues 1 numrxqueues 1");
	SYS(out, "ip link set dev " TX_NAME " address 00:00:00:00:00:01");
	SYS(out, "ip link set dev " RX_NAME " address 00:00:00:00:00:02");
	SYS(out, "ip link set dev " TX_NAME " up");
	SYS(out, "ip link set dev " RX_NAME " up");
	SYS(out, "ip addr add " TX_ADDR "/" PREFIX_LEN " dev " TX_NAME);
	SYS(out, "ip addr add " RX_ADDR "/" PREFIX_LEN " dev " RX_NAME);

	rx_ifindex = if_nametoindex(RX_NAME);
	tx_ifindex = if_nametoindex(TX_NAME);

	/* Setup separate AF_XDP for TX and RX interfaces. */

	ret = open_xsk(tx_ifindex, &tx_xsk);
	if (!ASSERT_OK(ret, "open_xsk(TX_NAME)"))
		goto out;

	ret = open_xsk(rx_ifindex, &rx_xsk);
	if (!ASSERT_OK(ret, "open_xsk(RX_NAME)"))
		goto out;

	bpf_obj = xdp_metadata__open();
	if (!ASSERT_OK_PTR(bpf_obj, "open skeleton"))
		goto out;

	prog = bpf_object__find_program_by_name(bpf_obj->obj, "rx");
	bpf_program__set_ifindex(prog, rx_ifindex);
	bpf_program__set_flags(prog, BPF_F_XDP_DEV_BOUND_ONLY);

	if (!ASSERT_OK(xdp_metadata__load(bpf_obj), "load skeleton"))
		goto out;

	/* Make sure we can't add dev-bound programs to prog maps. */
	prog_arr = bpf_object__find_map_by_name(bpf_obj->obj, "prog_arr");
	if (!ASSERT_OK_PTR(prog_arr, "no prog_arr map"))
		goto out;

	val = bpf_program__fd(prog);
	if (!ASSERT_ERR(bpf_map__update_elem(prog_arr, &key, sizeof(key),
					     &val, sizeof(val), BPF_ANY),
			"update prog_arr"))
		goto out;

	/* Attach BPF program to RX interface. */

	ret = bpf_xdp_attach(rx_ifindex,
			     bpf_program__fd(bpf_obj->progs.rx),
			     XDP_FLAGS, NULL);
	if (!ASSERT_GE(ret, 0, "bpf_xdp_attach"))
		goto out;

	sock_fd = xsk_socket__fd(rx_xsk.socket);
	ret = bpf_map_update_elem(bpf_map__fd(bpf_obj->maps.xsk), &queue_id, &sock_fd, 0);
	if (!ASSERT_GE(ret, 0, "bpf_map_update_elem"))
		goto out;

	/* Send packet destined to RX AF_XDP socket. */
	if (!ASSERT_GE(generate_packet(&tx_xsk, AF_XDP_CONSUMER_PORT), 0,
		       "generate AF_XDP_CONSUMER_PORT"))
		goto out;

	/* Verify AF_XDP RX packet has proper metadata. */
	if (!ASSERT_GE(verify_xsk_metadata(&rx_xsk), 0,
		       "verify_xsk_metadata"))
		goto out;

	complete_tx(&tx_xsk);

	/* Make sure freplace correctly picks up original bound device
	 * and doesn't crash.
	 */

	bpf_obj2 = xdp_metadata2__open();
	if (!ASSERT_OK_PTR(bpf_obj2, "open skeleton"))
		goto out;

	new_prog = bpf_object__find_program_by_name(bpf_obj2->obj, "freplace_rx");
	bpf_program__set_attach_target(new_prog, bpf_program__fd(prog), "rx");

	if (!ASSERT_OK(xdp_metadata2__load(bpf_obj2), "load freplace skeleton"))
		goto out;

	if (!ASSERT_OK(xdp_metadata2__attach(bpf_obj2), "attach freplace"))
		goto out;

	/* Send packet to trigger . */
	if (!ASSERT_GE(generate_packet(&tx_xsk, AF_XDP_CONSUMER_PORT), 0,
		       "generate freplace packet"))
		goto out;

	while (!retries--) {
		if (bpf_obj2->bss->called)
			break;
		usleep(10);
	}
	ASSERT_GT(bpf_obj2->bss->called, 0, "not called");

out:
	close_xsk(&rx_xsk);
	close_xsk(&tx_xsk);
	xdp_metadata2__destroy(bpf_obj2);
	xdp_metadata__destroy(bpf_obj);
	if (tok)
		close_netns(tok);
	SYS_NOFAIL("ip netns del xdp_metadata");
}
