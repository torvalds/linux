/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef XDPXCEIVER_H_
#define XDPXCEIVER_H_

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef PF_XDP
#define PF_XDP AF_XDP
#endif

#define MAX_INTERFACES 2
#define MAX_INTERFACE_NAME_CHARS 7
#define MAX_INTERFACES_NAMESPACE_CHARS 10
#define MAX_SOCKS 1
#define MAX_SOCKETS 2
#define MAX_TEARDOWN_ITER 10
#define MAX_BIDI_ITER 2
#define MAX_BPF_ITER 2
#define PKT_HDR_SIZE (sizeof(struct ethhdr) + sizeof(struct iphdr) + \
			sizeof(struct udphdr))
#define MIN_PKT_SIZE 64
#define ETH_FCS_SIZE 4
#define PKT_SIZE (MIN_PKT_SIZE - ETH_FCS_SIZE)
#define IP_PKT_SIZE (PKT_SIZE - sizeof(struct ethhdr))
#define IP_PKT_VER 0x4
#define IP_PKT_TOS 0x9
#define UDP_PKT_SIZE (IP_PKT_SIZE - sizeof(struct iphdr))
#define UDP_PKT_DATA_SIZE (UDP_PKT_SIZE - sizeof(struct udphdr))
#define USLEEP_MAX 10000
#define SOCK_RECONF_CTR 10
#define BATCH_SIZE 8
#define POLL_TMOUT 1000
#define DEFAULT_PKT_CNT (4 * 1024)
#define RX_FULL_RXQSIZE 32
#define XSK_UMEM__INVALID_FRAME_SIZE (XSK_UMEM__DEFAULT_FRAME_SIZE + 1)

#define print_verbose(x...) do { if (opt_verbose) ksft_print_msg(x); } while (0)

enum test_mode {
	TEST_MODE_SKB,
	TEST_MODE_DRV,
	TEST_MODE_MAX
};

enum test_type {
	TEST_TYPE_NOPOLL,
	TEST_TYPE_POLL,
	TEST_TYPE_TEARDOWN,
	TEST_TYPE_BIDI,
	TEST_TYPE_STATS,
	TEST_TYPE_BPF_RES,
	TEST_TYPE_MAX
};

enum stat_test_type {
	STAT_TEST_RX_DROPPED,
	STAT_TEST_TX_INVALID,
	STAT_TEST_RX_FULL,
	STAT_TEST_RX_FILL_EMPTY,
	STAT_TEST_TYPE_MAX
};

static int configured_mode;
static bool opt_pkt_dump;
static bool second_step;
static int test_type;

static bool opt_verbose;

static u32 xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
static u32 xdp_bind_flags = XDP_USE_NEED_WAKEUP | XDP_COPY;
static int stat_test_type;

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	u32 num_frames;
	u32 frame_headroom;
	void *buffer;
	u32 frame_size;
};

struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;
	u32 outstanding_tx;
	u32 rxqsize;
};

struct flow_vector {
	enum fvector {
		tx,
		rx,
	} vector;
};

struct pkt {
	u64 addr;
	u32 len;
	u32 payload;
};

struct pkt_stream {
	u32 nb_pkts;
	struct pkt *pkts;
};

typedef void *(*thread_func_t)(void *arg);

struct ifobject {
	char ifname[MAX_INTERFACE_NAME_CHARS];
	char nsname[MAX_INTERFACES_NAMESPACE_CHARS];
	struct xsk_socket_info *xsk;
	struct xsk_socket_info *xsk_arr;
	struct xsk_umem_info *umem;
	struct xsk_umem_info *umem_arr;
	struct flow_vector fv;
	thread_func_t func_ptr;
	struct pkt_stream *pkt_stream;
	int ns_fd;
	u32 dst_ip;
	u32 src_ip;
	u16 src_port;
	u16 dst_port;
	u8 dst_mac[ETH_ALEN];
	u8 src_mac[ETH_ALEN];
};

struct test_spec {
	struct ifobject *ifobj_tx;
	struct ifobject *ifobj_rx;
};

/*threads*/
pthread_barrier_t barr;
pthread_t t0, t1;

#endif				/* XDPXCEIVER_H */
