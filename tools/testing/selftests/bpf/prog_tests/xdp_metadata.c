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
#include <netinet/udp.h>
#include <sys/mman.h>
#include <net/if.h>
#include <poll.h>

#define TX_NAME "veTX"
#define RX_NAME "veRX"

#define UDP_PAYLOAD_BYTES 4

#define UDP_SOURCE_PORT 1234
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
#define TX_NETNS_NAME "xdp_metadata_tx"
#define RX_NETNS_NAME "xdp_metadata_rx"
#define TX_MAC "00:00:00:00:00:01"
#define RX_MAC "00:00:00:00:00:02"

#define VLAN_ID 59
#define VLAN_PROTO "802.1Q"
#define VLAN_PID htons(ETH_P_8021Q)
#define TX_NAME_VLAN TX_NAME "." TO_STR(VLAN_ID)

#define XDP_RSS_TYPE_L4 BIT(3)
#define VLAN_VID_MASK 0xfff

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
		.flags = XDP_UMEM_UNALIGNED_CHUNK_FLAG | XDP_UMEM_TX_SW_CSUM |
			 XDP_UMEM_TX_METADATA_LEN,
		.tx_metadata_len = sizeof(struct xsk_tx_metadata),
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

static int generate_packet(struct xsk *xsk, __u16 dst_port)
{
	struct xsk_tx_metadata *meta;
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
	tx_desc->addr = idx % (UMEM_NUM / 2) * UMEM_FRAME_SIZE + sizeof(struct xsk_tx_metadata);
	printf("%p: tx_desc[%u]->addr=%llx\n", xsk, idx, tx_desc->addr);
	data = xsk_umem__get_data(xsk->umem_area, tx_desc->addr);

	meta = data - sizeof(struct xsk_tx_metadata);
	memset(meta, 0, sizeof(*meta));
	meta->flags = XDP_TXMD_FLAGS_TIMESTAMP;

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
	iph->check = build_ip_csum(iph);

	udph->source = htons(UDP_SOURCE_PORT);
	udph->dest = htons(dst_port);
	udph->len = htons(sizeof(*udph) + UDP_PAYLOAD_BYTES);
	udph->check = ~csum_tcpudp_magic(iph->saddr, iph->daddr,
					 ntohs(udph->len), IPPROTO_UDP, 0);

	memset(udph + 1, 0xAA, UDP_PAYLOAD_BYTES);

	meta->flags |= XDP_TXMD_FLAGS_CHECKSUM;
	meta->request.csum_start = sizeof(*eth) + sizeof(*iph);
	meta->request.csum_offset = offsetof(struct udphdr, check);

	tx_desc->len = sizeof(*eth) + sizeof(*iph) + sizeof(*udph) + UDP_PAYLOAD_BYTES;
	tx_desc->options |= XDP_TX_METADATA;
	xsk_ring_prod__submit(&xsk->tx, 1);

	ret = sendto(xsk_socket__fd(xsk->socket), NULL, 0, MSG_DONTWAIT, NULL, 0);
	if (!ASSERT_GE(ret, 0, "sendto"))
		return ret;

	return 0;
}

static int generate_packet_inet(void)
{
	char udp_payload[UDP_PAYLOAD_BYTES];
	struct sockaddr_in rx_addr;
	int sock_fd, err = 0;

	/* Build a packet */
	memset(udp_payload, 0xAA, UDP_PAYLOAD_BYTES);
	rx_addr.sin_addr.s_addr = inet_addr(RX_ADDR);
	rx_addr.sin_family = AF_INET;
	rx_addr.sin_port = htons(AF_XDP_CONSUMER_PORT);

	sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!ASSERT_GE(sock_fd, 0, "socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)"))
		return sock_fd;

	err = sendto(sock_fd, udp_payload, UDP_PAYLOAD_BYTES, MSG_DONTWAIT,
		     (void *)&rx_addr, sizeof(rx_addr));
	ASSERT_GE(err, 0, "sendto");

	close(sock_fd);
	return err;
}

static void complete_tx(struct xsk *xsk)
{
	struct xsk_tx_metadata *meta;
	__u64 addr;
	void *data;
	__u32 idx;

	if (ASSERT_EQ(xsk_ring_cons__peek(&xsk->comp, 1, &idx), 1, "xsk_ring_cons__peek")) {
		addr = *xsk_ring_cons__comp_addr(&xsk->comp, idx);

		printf("%p: complete tx idx=%u addr=%llx\n", xsk, idx, addr);

		data = xsk_umem__get_data(xsk->umem_area, addr);
		meta = data - sizeof(struct xsk_tx_metadata);

		ASSERT_NEQ(meta->completion.tx_timestamp, 0, "tx_timestamp");

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

static int verify_xsk_metadata(struct xsk *xsk, bool sent_from_af_xdp)
{
	const struct xdp_desc *rx_desc;
	struct pollfd fds = {};
	struct xdp_meta *meta;
	struct udphdr *udph;
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
	udph = (void *)(iph + 1);

	/* custom metadata */

	meta = data - sizeof(struct xdp_meta);

	if (!ASSERT_NEQ(meta->rx_timestamp, 0, "rx_timestamp"))
		return -1;

	if (!ASSERT_NEQ(meta->rx_hash, 0, "rx_hash"))
		return -1;

	if (!sent_from_af_xdp) {
		if (!ASSERT_NEQ(meta->rx_hash_type & XDP_RSS_TYPE_L4, 0, "rx_hash_type"))
			return -1;

		if (!ASSERT_EQ(meta->rx_vlan_tci & VLAN_VID_MASK, VLAN_ID, "rx_vlan_tci"))
			return -1;

		if (!ASSERT_EQ(meta->rx_vlan_proto, VLAN_PID, "rx_vlan_proto"))
			return -1;
		goto done;
	}

	ASSERT_EQ(meta->rx_hash_type, 0, "rx_hash_type");

	/* checksum offload */
	ASSERT_EQ(udph->check, htons(0x721c), "csum");

done:
	xsk_ring_cons__release(&xsk->rx, 1);
	refill_rx(xsk, comp_addr);

	return 0;
}

static void switch_ns_to_rx(struct nstoken **tok)
{
	close_netns(*tok);
	*tok = open_netns(RX_NETNS_NAME);
}

static void switch_ns_to_tx(struct nstoken **tok)
{
	close_netns(*tok);
	*tok = open_netns(TX_NETNS_NAME);
}

void test_xdp_metadata(void)
{
	struct xdp_metadata2 *bpf_obj2 = NULL;
	struct xdp_metadata *bpf_obj = NULL;
	struct bpf_program *new_prog, *prog;
	struct bpf_devmap_val devmap_e = {};
	struct bpf_map *prog_arr, *devmap;
	struct nstoken *tok = NULL;
	__u32 queue_id = QUEUE_ID;
	struct xsk tx_xsk = {};
	struct xsk rx_xsk = {};
	__u32 val, key = 0;
	int retries = 10;
	int rx_ifindex;
	int tx_ifindex;
	int sock_fd;
	int ret;

	/* Setup new networking namespaces, with a veth pair. */
	SYS(out, "ip netns add " TX_NETNS_NAME);
	SYS(out, "ip netns add " RX_NETNS_NAME);

	tok = open_netns(TX_NETNS_NAME);
	if (!ASSERT_OK_PTR(tok, "setns"))
		goto out;
	SYS(out, "ip link add numtxqueues 1 numrxqueues 1 " TX_NAME
	    " type veth peer " RX_NAME " numtxqueues 1 numrxqueues 1");
	SYS(out, "ip link set " RX_NAME " netns " RX_NETNS_NAME);

	SYS(out, "ip link set dev " TX_NAME " address " TX_MAC);
	SYS(out, "ip link set dev " TX_NAME " up");

	SYS(out, "ip link add link " TX_NAME " " TX_NAME_VLAN
		 " type vlan proto " VLAN_PROTO " id " TO_STR(VLAN_ID));
	SYS(out, "ip link set dev " TX_NAME_VLAN " up");
	SYS(out, "ip addr add " TX_ADDR "/" PREFIX_LEN " dev " TX_NAME_VLAN);

	/* Avoid ARP calls */
	SYS(out, "ip -4 neigh add " RX_ADDR " lladdr " RX_MAC " dev " TX_NAME_VLAN);

	switch_ns_to_rx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns rx"))
		goto out;

	SYS(out, "ip link set dev " RX_NAME " address " RX_MAC);
	SYS(out, "ip link set dev " RX_NAME " up");
	SYS(out, "ip addr add " RX_ADDR "/" PREFIX_LEN " dev " RX_NAME);

	rx_ifindex = if_nametoindex(RX_NAME);

	/* Setup separate AF_XDP for RX interface. */

	ret = open_xsk(rx_ifindex, &rx_xsk);
	if (!ASSERT_OK(ret, "open_xsk(RX_NAME)"))
		goto out;

	bpf_obj = xdp_metadata__open();
	if (!ASSERT_OK_PTR(bpf_obj, "open skeleton"))
		goto out;

	prog = bpf_object__find_program_by_name(bpf_obj->obj, "rx");
	bpf_program__set_ifindex(prog, rx_ifindex);
	bpf_program__set_flags(prog, BPF_F_XDP_DEV_BOUND_ONLY);

	/* Make sure we can load a dev-bound program that performs
	 * XDP_REDIRECT into a devmap.
	 */
	new_prog = bpf_object__find_program_by_name(bpf_obj->obj, "redirect");
	bpf_program__set_ifindex(new_prog, rx_ifindex);
	bpf_program__set_flags(new_prog, BPF_F_XDP_DEV_BOUND_ONLY);

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

	/* Make sure we can't add dev-bound programs to devmaps. */
	devmap = bpf_object__find_map_by_name(bpf_obj->obj, "dev_map");
	if (!ASSERT_OK_PTR(devmap, "no dev_map found"))
		goto out;

	devmap_e.bpf_prog.fd = val;
	if (!ASSERT_ERR(bpf_map__update_elem(devmap, &key, sizeof(key),
					     &devmap_e, sizeof(devmap_e),
					     BPF_ANY),
			"update dev_map"))
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

	switch_ns_to_tx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns tx"))
		goto out;

	/* Setup separate AF_XDP for TX interface nad send packet to the RX socket. */
	tx_ifindex = if_nametoindex(TX_NAME);
	ret = open_xsk(tx_ifindex, &tx_xsk);
	if (!ASSERT_OK(ret, "open_xsk(TX_NAME)"))
		goto out;

	if (!ASSERT_GE(generate_packet(&tx_xsk, AF_XDP_CONSUMER_PORT), 0,
		       "generate AF_XDP_CONSUMER_PORT"))
		goto out;

	switch_ns_to_rx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns rx"))
		goto out;

	/* Verify packet sent from AF_XDP has proper metadata. */
	if (!ASSERT_GE(verify_xsk_metadata(&rx_xsk, true), 0,
		       "verify_xsk_metadata"))
		goto out;

	switch_ns_to_tx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns tx"))
		goto out;
	complete_tx(&tx_xsk);

	/* Now check metadata of packet, generated with network stack */
	if (!ASSERT_GE(generate_packet_inet(), 0, "generate UDP packet"))
		goto out;

	switch_ns_to_rx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns rx"))
		goto out;

	if (!ASSERT_GE(verify_xsk_metadata(&rx_xsk, false), 0,
		       "verify_xsk_metadata"))
		goto out;

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

	switch_ns_to_tx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns tx"))
		goto out;

	/* Send packet to trigger . */
	if (!ASSERT_GE(generate_packet(&tx_xsk, AF_XDP_CONSUMER_PORT), 0,
		       "generate freplace packet"))
		goto out;

	switch_ns_to_rx(&tok);
	if (!ASSERT_OK_PTR(tok, "setns rx"))
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
	SYS_NOFAIL("ip netns del " RX_NETNS_NAME);
	SYS_NOFAIL("ip netns del " TX_NETNS_NAME);
}
