/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TEST_XSK_H_
#define TEST_XSK_H_

#include <linux/ethtool.h>
#include <linux/if_xdp.h>

#include "../kselftest.h"
#include "xsk.h"

#ifndef SO_PREFER_BUSY_POLL
#define SO_PREFER_BUSY_POLL 69
#endif

#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70
#endif

#define TEST_PASS 0
#define TEST_FAILURE -1
#define TEST_CONTINUE 1
#define TEST_SKIP 2

#define DEFAULT_PKT_CNT			(4 * 1024)
#define DEFAULT_UMEM_BUFFERS		(DEFAULT_PKT_CNT / 4)
#define HUGEPAGE_SIZE			(2 * 1024 * 1024)
#define MIN_PKT_SIZE			64
#define MAX_ETH_PKT_SIZE		1518
#define MAX_INTERFACE_NAME_CHARS	16
#define MAX_TEST_NAME_SIZE		48
#define SOCK_RECONF_CTR			10
#define USLEEP_MAX			10000

extern bool opt_verbose;
#define print_verbose(x...) do { if (opt_verbose) ksft_print_msg(x); } while (0)


static inline u32 ceil_u32(u32 a, u32 b)
{
	return (a + b - 1) / b;
}

static inline u64 ceil_u64(u64 a, u64 b)
{
	return (a + b - 1) / b;
}

/* Simple test */
enum test_mode {
	TEST_MODE_SKB,
	TEST_MODE_DRV,
	TEST_MODE_ZC,
	TEST_MODE_ALL
};

struct ifobject;
struct test_spec;
typedef int (*validation_func_t)(struct ifobject *ifobj);
typedef void *(*thread_func_t)(void *arg);
typedef int (*test_func_t)(struct test_spec *test);

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
	bool check_consumer;
};

int kick_rx(struct xsk_socket_info *xsk);
int kick_tx(struct xsk_socket_info *xsk);

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

struct set_hw_ring {
	u32 default_tx;
	u32 default_rx;
};

int hw_ring_size_reset(struct ifobject *ifobj);

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
struct ifobject *ifobject_create(void);
void ifobject_delete(struct ifobject *ifobj);
int init_iface(struct ifobject *ifobj, thread_func_t func_ptr);

int xsk_configure_umem(struct ifobject *ifobj, struct xsk_umem_info *umem, void *buffer, u64 size);
int xsk_configure_socket(struct xsk_socket_info *xsk, struct xsk_umem_info *umem,
			 struct ifobject *ifobject, bool shared);


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

static inline bool pkt_continues(u32 options)
{
	return options & XDP_PKT_CONTD;
}

struct pkt_stream *pkt_stream_generate(u32 nb_pkts, u32 pkt_len);
void pkt_stream_delete(struct pkt_stream *pkt_stream);
void pkt_stream_reset(struct pkt_stream *pkt_stream);
void pkt_stream_restore_default(struct test_spec *test);

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
	bool adjust_tail;
	bool adjust_tail_support;
	enum test_mode mode;
	char name[MAX_TEST_NAME_SIZE];
};

#define busy_poll_string(test) (test)->ifobj_tx->busy_poll ? "BUSY-POLL " : ""
static inline char *mode_string(struct test_spec *test)
{
	switch (test->mode) {
	case TEST_MODE_SKB:
		return "SKB";
	case TEST_MODE_DRV:
		return "DRV";
	case TEST_MODE_ZC:
		return "ZC";
	default:
		return "BOGUS";
	}
}

void test_init(struct test_spec *test, struct ifobject *ifobj_tx,
	       struct ifobject *ifobj_rx, enum test_mode mode,
	       const struct test_spec *test_to_run);

int testapp_adjust_tail_grow(struct test_spec *test);
int testapp_adjust_tail_grow_mb(struct test_spec *test);
int testapp_adjust_tail_shrink(struct test_spec *test);
int testapp_adjust_tail_shrink_mb(struct test_spec *test);
int testapp_aligned_inv_desc(struct test_spec *test);
int testapp_aligned_inv_desc_2k_frame(struct test_spec *test);
int testapp_aligned_inv_desc_mb(struct test_spec *test);
int testapp_bidirectional(struct test_spec *test);
int testapp_headroom(struct test_spec *test);
int testapp_hw_sw_max_ring_size(struct test_spec *test);
int testapp_hw_sw_min_ring_size(struct test_spec *test);
int testapp_poll_rx(struct test_spec *test);
int testapp_poll_rxq_tmout(struct test_spec *test);
int testapp_poll_tx(struct test_spec *test);
int testapp_poll_txq_tmout(struct test_spec *test);
int testapp_send_receive(struct test_spec *test);
int testapp_send_receive_2k_frame(struct test_spec *test);
int testapp_send_receive_mb(struct test_spec *test);
int testapp_send_receive_unaligned(struct test_spec *test);
int testapp_send_receive_unaligned_mb(struct test_spec *test);
int testapp_single_pkt(struct test_spec *test);
int testapp_stats_fill_empty(struct test_spec *test);
int testapp_stats_rx_dropped(struct test_spec *test);
int testapp_stats_tx_invalid_descs(struct test_spec *test);
int testapp_stats_rx_full(struct test_spec *test);
int testapp_teardown(struct test_spec *test);
int testapp_too_many_frags(struct test_spec *test);
int testapp_tx_queue_consumer(struct test_spec *test);
int testapp_unaligned_inv_desc(struct test_spec *test);
int testapp_unaligned_inv_desc_4001_frame(struct test_spec *test);
int testapp_unaligned_inv_desc_mb(struct test_spec *test);
int testapp_xdp_drop(struct test_spec *test);
int testapp_xdp_metadata(struct test_spec *test);
int testapp_xdp_metadata_mb(struct test_spec *test);
int testapp_xdp_prog_cleanup(struct test_spec *test);
int testapp_xdp_shared_umem(struct test_spec *test);

void *worker_testapp_validate_rx(void *arg);
void *worker_testapp_validate_tx(void *arg);

static const struct test_spec tests[] = {
	{.name = "SEND_RECEIVE", .test_func = testapp_send_receive},
	{.name = "SEND_RECEIVE_2K_FRAME", .test_func = testapp_send_receive_2k_frame},
	{.name = "SEND_RECEIVE_SINGLE_PKT", .test_func = testapp_single_pkt},
	{.name = "POLL_RX", .test_func = testapp_poll_rx},
	{.name = "POLL_TX", .test_func = testapp_poll_tx},
	{.name = "POLL_RXQ_FULL", .test_func = testapp_poll_rxq_tmout},
	{.name = "POLL_TXQ_FULL", .test_func = testapp_poll_txq_tmout},
	{.name = "ALIGNED_INV_DESC", .test_func = testapp_aligned_inv_desc},
	{.name = "ALIGNED_INV_DESC_2K_FRAME_SIZE", .test_func = testapp_aligned_inv_desc_2k_frame},
	{.name = "UMEM_HEADROOM", .test_func = testapp_headroom},
	{.name = "BIDIRECTIONAL", .test_func = testapp_bidirectional},
	{.name = "STAT_RX_DROPPED", .test_func = testapp_stats_rx_dropped},
	{.name = "STAT_TX_INVALID", .test_func = testapp_stats_tx_invalid_descs},
	{.name = "STAT_RX_FULL", .test_func = testapp_stats_rx_full},
	{.name = "STAT_FILL_EMPTY", .test_func = testapp_stats_fill_empty},
	{.name = "XDP_PROG_CLEANUP", .test_func = testapp_xdp_prog_cleanup},
	{.name = "XDP_DROP_HALF", .test_func = testapp_xdp_drop},
	{.name = "XDP_SHARED_UMEM", .test_func = testapp_xdp_shared_umem},
	{.name = "XDP_METADATA_COPY", .test_func = testapp_xdp_metadata},
	{.name = "XDP_METADATA_COPY_MULTI_BUFF", .test_func = testapp_xdp_metadata_mb},
	{.name = "ALIGNED_INV_DESC_MULTI_BUFF", .test_func = testapp_aligned_inv_desc_mb},
	{.name = "TOO_MANY_FRAGS", .test_func = testapp_too_many_frags},
	{.name = "XDP_ADJUST_TAIL_SHRINK", .test_func = testapp_adjust_tail_shrink},
	{.name = "TX_QUEUE_CONSUMER", .test_func = testapp_tx_queue_consumer},
	};

static const struct test_spec ci_skip_tests[] = {
	/* Flaky tests */
	{.name = "XDP_ADJUST_TAIL_SHRINK_MULTI_BUFF", .test_func = testapp_adjust_tail_shrink_mb},
	{.name = "XDP_ADJUST_TAIL_GROW", .test_func = testapp_adjust_tail_grow},
	{.name = "XDP_ADJUST_TAIL_GROW_MULTI_BUFF", .test_func = testapp_adjust_tail_grow_mb},
	{.name = "SEND_RECEIVE_9K_PACKETS", .test_func = testapp_send_receive_mb},
	/* Tests with huge page dependency */
	{.name = "SEND_RECEIVE_UNALIGNED", .test_func = testapp_send_receive_unaligned},
	{.name = "UNALIGNED_INV_DESC", .test_func = testapp_unaligned_inv_desc},
	{.name = "UNALIGNED_INV_DESC_4001_FRAME_SIZE",
	 .test_func = testapp_unaligned_inv_desc_4001_frame},
	{.name = "SEND_RECEIVE_UNALIGNED_9K_PACKETS",
	 .test_func = testapp_send_receive_unaligned_mb},
	{.name = "UNALIGNED_INV_DESC_MULTI_BUFF", .test_func = testapp_unaligned_inv_desc_mb},
	/* Test with HW ring size dependency */
	{.name = "HW_SW_MIN_RING_SIZE", .test_func = testapp_hw_sw_min_ring_size},
	{.name = "HW_SW_MAX_RING_SIZE", .test_func = testapp_hw_sw_max_ring_size},
	/* Too long test */
	{.name = "TEARDOWN", .test_func = testapp_teardown},
};


#endif				/* TEST_XSK_H_ */
