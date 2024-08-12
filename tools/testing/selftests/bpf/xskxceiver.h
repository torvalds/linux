/* SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef XSKXCEIVER_H_
#define XSKXCEIVER_H_

#include <limits.h>

#include "xsk_xdp_progs.skel.h"
#include "xsk_xdp_common.h"

#ifndef SOL_XDP
#define SOL_XDP 283
#endif

#ifndef AF_XDP
#define AF_XDP 44
#endif

#ifndef PF_XDP
#define PF_XDP AF_XDP
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif

#ifndef SO_PREFER_BUSY_POLL
#define SO_PREFER_BUSY_POLL 69
#endif

#define TEST_PASS 0
#define TEST_FAILURE -1
#define TEST_CONTINUE 1
#define TEST_SKIP 2
#define MAX_INTERFACES 2
#define MAX_INTERFACE_NAME_CHARS 16
#define MAX_TEST_NAME_SIZE 48
#define MAX_TEARDOWN_ITER 10
#define PKT_HDR_SIZE (sizeof(struct ethhdr) + 2) /* Just to align the data in the packet */
#define MIN_PKT_SIZE 64
#define MAX_ETH_PKT_SIZE 1518
#define MAX_ETH_JUMBO_SIZE 9000
#define USLEEP_MAX 10000
#define SOCK_RECONF_CTR 10
#define DEFAULT_BATCH_SIZE 64
#define POLL_TMOUT 1000
#define THREAD_TMOUT 3
#define DEFAULT_PKT_CNT (4 * 1024)
#define DEFAULT_UMEM_BUFFERS (DEFAULT_PKT_CNT / 4)
#define RX_FULL_RXQSIZE 32
#define UMEM_HEADROOM_TEST_SIZE 128
#define XSK_UMEM__INVALID_FRAME_SIZE (MAX_ETH_JUMBO_SIZE + 1)
#define XSK_UMEM__LARGE_FRAME_SIZE (3 * 1024)
#define XSK_UMEM__MAX_FRAME_SIZE (4 * 1024)
#define XSK_DESC__INVALID_OPTION (0xffff)
#define XSK_DESC__MAX_SKB_FRAGS 18
#define HUGEPAGE_SIZE (2 * 1024 * 1024)
#define PKT_DUMP_NB_TO_PRINT 16
#define RUN_ALL_TESTS UINT_MAX
#define NUM_MAC_ADDRESSES 4

#define print_verbose(x...) do { if (opt_verbose) ksft_print_msg(x); } while (0)

enum test_mode {
	TEST_MODE_SKB,
	TEST_MODE_DRV,
	TEST_MODE_ZC,
	TEST_MODE_ALL
};

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	u64 next_buffer;
	u32 num_frames;
	u32 frame_headroom;
	void *buffer;
	u32 frame_size;
	u32 base_addr;
	u32 fill_size;
	u32 comp_size;
	bool unaligned_mode;
};

struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;
	struct pkt_stream *pkt_stream;
	u32 outstanding_tx;
	u32 rxqsize;
	u32 batch_size;
	u8 dst_mac[ETH_ALEN];
	u8 src_mac[ETH_ALEN];
};

struct pkt {
	int offset;
	u32 len;
	u32 pkt_nb;
	bool valid;
	u16 options;
};

struct pkt_stream {
	u32 nb_pkts;
	u32 current_pkt_nb;
	struct pkt *pkts;
	u32 max_pkt_len;
	u32 nb_rx_pkts;
	u32 nb_valid_entries;
	bool verbatim;
};

struct set_hw_ring {
	u32 default_tx;
	u32 default_rx;
};

struct ifobject;
struct test_spec;
typedef int (*validation_func_t)(struct ifobject *ifobj);
typedef void *(*thread_func_t)(void *arg);
typedef int (*test_func_t)(struct test_spec *test);

struct ifobject {
	char ifname[MAX_INTERFACE_NAME_CHARS];
	struct xsk_socket_info *xsk;
	struct xsk_socket_info *xsk_arr;
	struct xsk_umem_info *umem;
	thread_func_t func_ptr;
	validation_func_t validation_func;
	struct xsk_xdp_progs *xdp_progs;
	struct bpf_map *xskmap;
	struct bpf_program *xdp_prog;
	struct ethtool_ringparam ring;
	struct set_hw_ring set_ring;
	enum test_mode mode;
	int ifindex;
	int mtu;
	u32 bind_flags;
	u32 xdp_zc_max_segs;
	bool tx_on;
	bool rx_on;
	bool use_poll;
	bool busy_poll;
	bool use_fill_ring;
	bool release_rx;
	bool shared_umem;
	bool use_metadata;
	bool unaligned_supp;
	bool multi_buff_supp;
	bool multi_buff_zc_supp;
	bool hw_ring_size_supp;
};

struct test_spec {
	struct ifobject *ifobj_tx;
	struct ifobject *ifobj_rx;
	struct pkt_stream *tx_pkt_stream_default;
	struct pkt_stream *rx_pkt_stream_default;
	struct bpf_program *xdp_prog_rx;
	struct bpf_program *xdp_prog_tx;
	struct bpf_map *xskmap_rx;
	struct bpf_map *xskmap_tx;
	test_func_t test_func;
	int mtu;
	u16 total_steps;
	u16 current_step;
	u16 nb_sockets;
	bool fail;
	bool set_ring;
	enum test_mode mode;
	char name[MAX_TEST_NAME_SIZE];
};

pthread_barrier_t barr;
pthread_mutex_t pacing_mutex = PTHREAD_MUTEX_INITIALIZER;

int pkts_in_flight;

static const u8 g_mac[ETH_ALEN] = {0x55, 0x44, 0x33, 0x22, 0x11, 0x00};

#endif				/* XSKXCEIVER_H_ */
