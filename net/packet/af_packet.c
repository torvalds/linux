// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PACKET - implements raw packet sockets.
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() now used correctly
 *		Alan Cox	:	new skbuff lists, look ma no backlogs!
 *		Alan Cox	:	tidied skbuff lists.
 *		Alan Cox	:	Now uses generic datagram routines I
 *					added. Also fixed the peek/read crash
 *					from all old Linux datagram code.
 *		Alan Cox	:	Uses the improved datagram code.
 *		Alan Cox	:	Added NULL's for socket options.
 *		Alan Cox	:	Re-commented the code.
 *		Alan Cox	:	Use new kernel side addressing
 *		Rob Janssen	:	Correct MTU usage.
 *		Dave Platt	:	Counter leaks caused by incorrect
 *					interrupt locking and some slightly
 *					dubious gcc output. Can you read
 *					compiler: it said _VOLATILE_
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	New buffers. Use sk->mac.raw.
 *		Alan Cox	:	sendmsg/recvmsg support.
 *		Alan Cox	:	Protocol setting support
 *	Alexey Kuznetsov	:	Untied from IPv4 stack.
 *	Cyrus Durgin		:	Fixed kerneld for kmod.
 *	Michal Ostrowski        :       Module initialization cleanup.
 *         Ulises Alonso        :       Frame number limit removal and
 *                                      packet_set_ring memory leak.
 *		Eric Biederman	:	Allow for > 8 byte hardware addresses.
 *					The convention is that longer addresses
 *					will simply extend the hardware address
 *					byte arrays at the end of sockaddr_ll
 *					and packet_mreq.
 *		Johann Baudy	:	Added TX RING.
 *		Chetan Loke	:	Implemented TPACKET_V3 block abstraction
 *					layer.
 *					Copyright (C) 2011, <lokec@ccs.neu.edu>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ethtool.h>
#include <linux/filter.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <linux/wireless.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <asm/ioctls.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/if_vlan.h>
#include <linux/virtio_net.h>
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <linux/percpu.h>
#ifdef CONFIG_INET
#include <net/inet_common.h>
#endif
#include <linux/bpf.h>
#include <net/compat.h>
#include <linux/netfilter_netdev.h>

#include "internal.h"

/*
   Assumptions:
   - If the device has no dev->header_ops->create, there is no LL header
     visible above the device. In this case, its hard_header_len should be 0.
     The device may prepend its own header internally. In this case, its
     needed_headroom should be set to the space needed for it to add its
     internal header.
     For example, a WiFi driver pretending to be an Ethernet driver should
     set its hard_header_len to be the Ethernet header length, and set its
     needed_headroom to be (the real WiFi header length - the fake Ethernet
     header length).
   - packet socket receives packets with pulled ll header,
     so that SOCK_RAW should push it back.

On receive:
-----------

Incoming, dev_has_header(dev) == true
   mac_header -> ll header
   data       -> data

Outgoing, dev_has_header(dev) == true
   mac_header -> ll header
   data       -> ll header

Incoming, dev_has_header(dev) == false
   mac_header -> data
     However drivers often make it point to the ll header.
     This is incorrect because the ll header should be invisible to us.
   data       -> data

Outgoing, dev_has_header(dev) == false
   mac_header -> data. ll header is invisible to us.
   data       -> data

Resume
  If dev_has_header(dev) == false we are unable to restore the ll header,
    because it is invisible to us.


On transmit:
------------

dev_has_header(dev) == true
   mac_header -> ll header
   data       -> ll header

dev_has_header(dev) == false (ll header is invisible to us)
   mac_header -> data
   data       -> data

   We should set network_header on output to the correct position,
   packet classifier depends on it.
 */

/* Private packet socket structures. */

/* identical to struct packet_mreq except it has
 * a longer address field.
 */
struct packet_mreq_max {
	int		mr_ifindex;
	unsigned short	mr_type;
	unsigned short	mr_alen;
	unsigned char	mr_address[MAX_ADDR_LEN];
};

union tpacket_uhdr {
	struct tpacket_hdr  *h1;
	struct tpacket2_hdr *h2;
	struct tpacket3_hdr *h3;
	void *raw;
};

static int packet_set_ring(struct sock *sk, union tpacket_req_u *req_u,
		int closing, int tx_ring);

#define V3_ALIGNMENT	(8)

#define BLK_HDR_LEN	(ALIGN(sizeof(struct tpacket_block_desc), V3_ALIGNMENT))

#define BLK_PLUS_PRIV(sz_of_priv) \
	(BLK_HDR_LEN + ALIGN((sz_of_priv), V3_ALIGNMENT))

#define BLOCK_STATUS(x)	((x)->hdr.bh1.block_status)
#define BLOCK_NUM_PKTS(x)	((x)->hdr.bh1.num_pkts)
#define BLOCK_O2FP(x)		((x)->hdr.bh1.offset_to_first_pkt)
#define BLOCK_LEN(x)		((x)->hdr.bh1.blk_len)
#define BLOCK_SNUM(x)		((x)->hdr.bh1.seq_num)
#define BLOCK_O2PRIV(x)	((x)->offset_to_priv)

struct packet_sock;
static int tpacket_rcv(struct sk_buff *skb, struct net_device *dev,
		       struct packet_type *pt, struct net_device *orig_dev);

static void *packet_previous_frame(struct packet_sock *po,
		struct packet_ring_buffer *rb,
		int status);
static void packet_increment_head(struct packet_ring_buffer *buff);
static int prb_curr_blk_in_use(struct tpacket_block_desc *);
static void *prb_dispatch_next_block(struct tpacket_kbdq_core *,
			struct packet_sock *);
static void prb_retire_current_block(struct tpacket_kbdq_core *,
		struct packet_sock *, unsigned int status);
static int prb_queue_frozen(struct tpacket_kbdq_core *);
static void prb_open_block(struct tpacket_kbdq_core *,
		struct tpacket_block_desc *);
static void prb_retire_rx_blk_timer_expired(struct timer_list *);
static void _prb_refresh_rx_retire_blk_timer(struct tpacket_kbdq_core *);
static void prb_fill_rxhash(struct tpacket_kbdq_core *, struct tpacket3_hdr *);
static void prb_clear_rxhash(struct tpacket_kbdq_core *,
		struct tpacket3_hdr *);
static void prb_fill_vlan_info(struct tpacket_kbdq_core *,
		struct tpacket3_hdr *);
static void packet_flush_mclist(struct sock *sk);
static u16 packet_pick_tx_queue(struct sk_buff *skb);

struct packet_skb_cb {
	union {
		struct sockaddr_pkt pkt;
		union {
			/* Trick: alias skb original length with
			 * ll.sll_family and ll.protocol in order
			 * to save room.
			 */
			unsigned int origlen;
			struct sockaddr_ll ll;
		};
	} sa;
};

#define vio_le() virtio_legacy_is_little_endian()

#define PACKET_SKB_CB(__skb)	((struct packet_skb_cb *)((__skb)->cb))

#define GET_PBDQC_FROM_RB(x)	((struct tpacket_kbdq_core *)(&(x)->prb_bdqc))
#define GET_PBLOCK_DESC(x, bid)	\
	((struct tpacket_block_desc *)((x)->pkbdq[(bid)].buffer))
#define GET_CURR_PBLOCK_DESC_FROM_CORE(x)	\
	((struct tpacket_block_desc *)((x)->pkbdq[(x)->kactive_blk_num].buffer))
#define GET_NEXT_PRB_BLK_NUM(x) \
	(((x)->kactive_blk_num < ((x)->knum_blocks-1)) ? \
	((x)->kactive_blk_num+1) : 0)

static void __fanout_unlink(struct sock *sk, struct packet_sock *po);
static void __fanout_link(struct sock *sk, struct packet_sock *po);

#ifdef CONFIG_NETFILTER_EGRESS
static noinline struct sk_buff *nf_hook_direct_egress(struct sk_buff *skb)
{
	struct sk_buff *next, *head = NULL, *tail;
	int rc;

	rcu_read_lock();
	for (; skb != NULL; skb = next) {
		next = skb->next;
		skb_mark_not_on_list(skb);

		if (!nf_hook_egress(skb, &rc, skb->dev))
			continue;

		if (!head)
			head = skb;
		else
			tail->next = skb;

		tail = skb;
	}
	rcu_read_unlock();

	return head;
}
#endif

static int packet_xmit(const struct packet_sock *po, struct sk_buff *skb)
{
	if (!packet_sock_flag(po, PACKET_SOCK_QDISC_BYPASS))
		return dev_queue_xmit(skb);

#ifdef CONFIG_NETFILTER_EGRESS
	if (nf_hook_egress_active()) {
		skb = nf_hook_direct_egress(skb);
		if (!skb)
			return NET_XMIT_DROP;
	}
#endif
	return dev_direct_xmit(skb, packet_pick_tx_queue(skb));
}

static struct net_device *packet_cached_dev_get(struct packet_sock *po)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = rcu_dereference(po->cached_dev);
	dev_hold(dev);
	rcu_read_unlock();

	return dev;
}

static void packet_cached_dev_assign(struct packet_sock *po,
				     struct net_device *dev)
{
	rcu_assign_pointer(po->cached_dev, dev);
}

static void packet_cached_dev_reset(struct packet_sock *po)
{
	RCU_INIT_POINTER(po->cached_dev, NULL);
}

static u16 packet_pick_tx_queue(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	const struct net_device_ops *ops = dev->netdev_ops;
	int cpu = raw_smp_processor_id();
	u16 queue_index;

#ifdef CONFIG_XPS
	skb->sender_cpu = cpu + 1;
#endif
	skb_record_rx_queue(skb, cpu % dev->real_num_tx_queues);
	if (ops->ndo_select_queue) {
		queue_index = ops->ndo_select_queue(dev, skb, NULL);
		queue_index = netdev_cap_txqueue(dev, queue_index);
	} else {
		queue_index = netdev_pick_tx(dev, skb, NULL);
	}

	return queue_index;
}

/* __register_prot_hook must be invoked through register_prot_hook
 * or from a context in which asynchronous accesses to the packet
 * socket is not possible (packet_create()).
 */
static void __register_prot_hook(struct sock *sk)
{
	struct packet_sock *po = pkt_sk(sk);

	if (!packet_sock_flag(po, PACKET_SOCK_RUNNING)) {
		if (po->fanout)
			__fanout_link(sk, po);
		else
			dev_add_pack(&po->prot_hook);

		sock_hold(sk);
		packet_sock_flag_set(po, PACKET_SOCK_RUNNING, 1);
	}
}

static void register_prot_hook(struct sock *sk)
{
	lockdep_assert_held_once(&pkt_sk(sk)->bind_lock);
	__register_prot_hook(sk);
}

/* If the sync parameter is true, we will temporarily drop
 * the po->bind_lock and do a synchronize_net to make sure no
 * asynchronous packet processing paths still refer to the elements
 * of po->prot_hook.  If the sync parameter is false, it is the
 * callers responsibility to take care of this.
 */
static void __unregister_prot_hook(struct sock *sk, bool sync)
{
	struct packet_sock *po = pkt_sk(sk);

	lockdep_assert_held_once(&po->bind_lock);

	packet_sock_flag_set(po, PACKET_SOCK_RUNNING, 0);

	if (po->fanout)
		__fanout_unlink(sk, po);
	else
		__dev_remove_pack(&po->prot_hook);

	__sock_put(sk);

	if (sync) {
		spin_unlock(&po->bind_lock);
		synchronize_net();
		spin_lock(&po->bind_lock);
	}
}

static void unregister_prot_hook(struct sock *sk, bool sync)
{
	struct packet_sock *po = pkt_sk(sk);

	if (packet_sock_flag(po, PACKET_SOCK_RUNNING))
		__unregister_prot_hook(sk, sync);
}

static inline struct page * __pure pgv_to_page(void *addr)
{
	if (is_vmalloc_addr(addr))
		return vmalloc_to_page(addr);
	return virt_to_page(addr);
}

static void __packet_set_status(struct packet_sock *po, void *frame, int status)
{
	union tpacket_uhdr h;

	/* WRITE_ONCE() are paired with READ_ONCE() in __packet_get_status */

	h.raw = frame;
	switch (po->tp_version) {
	case TPACKET_V1:
		WRITE_ONCE(h.h1->tp_status, status);
		flush_dcache_page(pgv_to_page(&h.h1->tp_status));
		break;
	case TPACKET_V2:
		WRITE_ONCE(h.h2->tp_status, status);
		flush_dcache_page(pgv_to_page(&h.h2->tp_status));
		break;
	case TPACKET_V3:
		WRITE_ONCE(h.h3->tp_status, status);
		flush_dcache_page(pgv_to_page(&h.h3->tp_status));
		break;
	default:
		WARN(1, "TPACKET version not supported.\n");
		BUG();
	}

	smp_wmb();
}

static int __packet_get_status(const struct packet_sock *po, void *frame)
{
	union tpacket_uhdr h;

	smp_rmb();

	/* READ_ONCE() are paired with WRITE_ONCE() in __packet_set_status */

	h.raw = frame;
	switch (po->tp_version) {
	case TPACKET_V1:
		flush_dcache_page(pgv_to_page(&h.h1->tp_status));
		return READ_ONCE(h.h1->tp_status);
	case TPACKET_V2:
		flush_dcache_page(pgv_to_page(&h.h2->tp_status));
		return READ_ONCE(h.h2->tp_status);
	case TPACKET_V3:
		flush_dcache_page(pgv_to_page(&h.h3->tp_status));
		return READ_ONCE(h.h3->tp_status);
	default:
		WARN(1, "TPACKET version not supported.\n");
		BUG();
		return 0;
	}
}

static __u32 tpacket_get_timestamp(struct sk_buff *skb, struct timespec64 *ts,
				   unsigned int flags)
{
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);

	if (shhwtstamps &&
	    (flags & SOF_TIMESTAMPING_RAW_HARDWARE) &&
	    ktime_to_timespec64_cond(shhwtstamps->hwtstamp, ts))
		return TP_STATUS_TS_RAW_HARDWARE;

	if ((flags & SOF_TIMESTAMPING_SOFTWARE) &&
	    ktime_to_timespec64_cond(skb_tstamp(skb), ts))
		return TP_STATUS_TS_SOFTWARE;

	return 0;
}

static __u32 __packet_set_timestamp(struct packet_sock *po, void *frame,
				    struct sk_buff *skb)
{
	union tpacket_uhdr h;
	struct timespec64 ts;
	__u32 ts_status;

	if (!(ts_status = tpacket_get_timestamp(skb, &ts, READ_ONCE(po->tp_tstamp))))
		return 0;

	h.raw = frame;
	/*
	 * versions 1 through 3 overflow the timestamps in y2106, since they
	 * all store the seconds in a 32-bit unsigned integer.
	 * If we create a version 4, that should have a 64-bit timestamp,
	 * either 64-bit seconds + 32-bit nanoseconds, or just 64-bit
	 * nanoseconds.
	 */
	switch (po->tp_version) {
	case TPACKET_V1:
		h.h1->tp_sec = ts.tv_sec;
		h.h1->tp_usec = ts.tv_nsec / NSEC_PER_USEC;
		break;
	case TPACKET_V2:
		h.h2->tp_sec = ts.tv_sec;
		h.h2->tp_nsec = ts.tv_nsec;
		break;
	case TPACKET_V3:
		h.h3->tp_sec = ts.tv_sec;
		h.h3->tp_nsec = ts.tv_nsec;
		break;
	default:
		WARN(1, "TPACKET version not supported.\n");
		BUG();
	}

	/* one flush is safe, as both fields always lie on the same cacheline */
	flush_dcache_page(pgv_to_page(&h.h1->tp_sec));
	smp_wmb();

	return ts_status;
}

static void *packet_lookup_frame(const struct packet_sock *po,
				 const struct packet_ring_buffer *rb,
				 unsigned int position,
				 int status)
{
	unsigned int pg_vec_pos, frame_offset;
	union tpacket_uhdr h;

	pg_vec_pos = position / rb->frames_per_block;
	frame_offset = position % rb->frames_per_block;

	h.raw = rb->pg_vec[pg_vec_pos].buffer +
		(frame_offset * rb->frame_size);

	if (status != __packet_get_status(po, h.raw))
		return NULL;

	return h.raw;
}

static void *packet_current_frame(struct packet_sock *po,
		struct packet_ring_buffer *rb,
		int status)
{
	return packet_lookup_frame(po, rb, rb->head, status);
}

static void prb_del_retire_blk_timer(struct tpacket_kbdq_core *pkc)
{
	del_timer_sync(&pkc->retire_blk_timer);
}

static void prb_shutdown_retire_blk_timer(struct packet_sock *po,
		struct sk_buff_head *rb_queue)
{
	struct tpacket_kbdq_core *pkc;

	pkc = GET_PBDQC_FROM_RB(&po->rx_ring);

	spin_lock_bh(&rb_queue->lock);
	pkc->delete_blk_timer = 1;
	spin_unlock_bh(&rb_queue->lock);

	prb_del_retire_blk_timer(pkc);
}

static void prb_setup_retire_blk_timer(struct packet_sock *po)
{
	struct tpacket_kbdq_core *pkc;

	pkc = GET_PBDQC_FROM_RB(&po->rx_ring);
	timer_setup(&pkc->retire_blk_timer, prb_retire_rx_blk_timer_expired,
		    0);
	pkc->retire_blk_timer.expires = jiffies;
}

static int prb_calc_retire_blk_tmo(struct packet_sock *po,
				int blk_size_in_bytes)
{
	struct net_device *dev;
	unsigned int mbits, div;
	struct ethtool_link_ksettings ecmd;
	int err;

	rtnl_lock();
	dev = __dev_get_by_index(sock_net(&po->sk), po->ifindex);
	if (unlikely(!dev)) {
		rtnl_unlock();
		return DEFAULT_PRB_RETIRE_TOV;
	}
	err = __ethtool_get_link_ksettings(dev, &ecmd);
	rtnl_unlock();
	if (err)
		return DEFAULT_PRB_RETIRE_TOV;

	/* If the link speed is so slow you don't really
	 * need to worry about perf anyways
	 */
	if (ecmd.base.speed < SPEED_1000 ||
	    ecmd.base.speed == SPEED_UNKNOWN)
		return DEFAULT_PRB_RETIRE_TOV;

	div = ecmd.base.speed / 1000;
	mbits = (blk_size_in_bytes * 8) / (1024 * 1024);

	if (div)
		mbits /= div;

	if (div)
		return mbits + 1;
	return mbits;
}

static void prb_init_ft_ops(struct tpacket_kbdq_core *p1,
			union tpacket_req_u *req_u)
{
	p1->feature_req_word = req_u->req3.tp_feature_req_word;
}

static void init_prb_bdqc(struct packet_sock *po,
			struct packet_ring_buffer *rb,
			struct pgv *pg_vec,
			union tpacket_req_u *req_u)
{
	struct tpacket_kbdq_core *p1 = GET_PBDQC_FROM_RB(rb);
	struct tpacket_block_desc *pbd;

	memset(p1, 0x0, sizeof(*p1));

	p1->knxt_seq_num = 1;
	p1->pkbdq = pg_vec;
	pbd = (struct tpacket_block_desc *)pg_vec[0].buffer;
	p1->pkblk_start	= pg_vec[0].buffer;
	p1->kblk_size = req_u->req3.tp_block_size;
	p1->knum_blocks	= req_u->req3.tp_block_nr;
	p1->hdrlen = po->tp_hdrlen;
	p1->version = po->tp_version;
	p1->last_kactive_blk_num = 0;
	po->stats.stats3.tp_freeze_q_cnt = 0;
	if (req_u->req3.tp_retire_blk_tov)
		p1->retire_blk_tov = req_u->req3.tp_retire_blk_tov;
	else
		p1->retire_blk_tov = prb_calc_retire_blk_tmo(po,
						req_u->req3.tp_block_size);
	p1->tov_in_jiffies = msecs_to_jiffies(p1->retire_blk_tov);
	p1->blk_sizeof_priv = req_u->req3.tp_sizeof_priv;
	rwlock_init(&p1->blk_fill_in_prog_lock);

	p1->max_frame_len = p1->kblk_size - BLK_PLUS_PRIV(p1->blk_sizeof_priv);
	prb_init_ft_ops(p1, req_u);
	prb_setup_retire_blk_timer(po);
	prb_open_block(p1, pbd);
}

/*  Do NOT update the last_blk_num first.
 *  Assumes sk_buff_head lock is held.
 */
static void _prb_refresh_rx_retire_blk_timer(struct tpacket_kbdq_core *pkc)
{
	mod_timer(&pkc->retire_blk_timer,
			jiffies + pkc->tov_in_jiffies);
	pkc->last_kactive_blk_num = pkc->kactive_blk_num;
}

/*
 * Timer logic:
 * 1) We refresh the timer only when we open a block.
 *    By doing this we don't waste cycles refreshing the timer
 *	  on packet-by-packet basis.
 *
 * With a 1MB block-size, on a 1Gbps line, it will take
 * i) ~8 ms to fill a block + ii) memcpy etc.
 * In this cut we are not accounting for the memcpy time.
 *
 * So, if the user sets the 'tmo' to 10ms then the timer
 * will never fire while the block is still getting filled
 * (which is what we want). However, the user could choose
 * to close a block early and that's fine.
 *
 * But when the timer does fire, we check whether or not to refresh it.
 * Since the tmo granularity is in msecs, it is not too expensive
 * to refresh the timer, lets say every '8' msecs.
 * Either the user can set the 'tmo' or we can derive it based on
 * a) line-speed and b) block-size.
 * prb_calc_retire_blk_tmo() calculates the tmo.
 *
 */
static void prb_retire_rx_blk_timer_expired(struct timer_list *t)
{
	struct packet_sock *po =
		from_timer(po, t, rx_ring.prb_bdqc.retire_blk_timer);
	struct tpacket_kbdq_core *pkc = GET_PBDQC_FROM_RB(&po->rx_ring);
	unsigned int frozen;
	struct tpacket_block_desc *pbd;

	spin_lock(&po->sk.sk_receive_queue.lock);

	frozen = prb_queue_frozen(pkc);
	pbd = GET_CURR_PBLOCK_DESC_FROM_CORE(pkc);

	if (unlikely(pkc->delete_blk_timer))
		goto out;

	/* We only need to plug the race when the block is partially filled.
	 * tpacket_rcv:
	 *		lock(); increment BLOCK_NUM_PKTS; unlock()
	 *		copy_bits() is in progress ...
	 *		timer fires on other cpu:
	 *		we can't retire the current block because copy_bits
	 *		is in progress.
	 *
	 */
	if (BLOCK_NUM_PKTS(pbd)) {
		/* Waiting for skb_copy_bits to finish... */
		write_lock(&pkc->blk_fill_in_prog_lock);
		write_unlock(&pkc->blk_fill_in_prog_lock);
	}

	if (pkc->last_kactive_blk_num == pkc->kactive_blk_num) {
		if (!frozen) {
			if (!BLOCK_NUM_PKTS(pbd)) {
				/* An empty block. Just refresh the timer. */
				goto refresh_timer;
			}
			prb_retire_current_block(pkc, po, TP_STATUS_BLK_TMO);
			if (!prb_dispatch_next_block(pkc, po))
				goto refresh_timer;
			else
				goto out;
		} else {
			/* Case 1. Queue was frozen because user-space was
			 *	   lagging behind.
			 */
			if (prb_curr_blk_in_use(pbd)) {
				/*
				 * Ok, user-space is still behind.
				 * So just refresh the timer.
				 */
				goto refresh_timer;
			} else {
			       /* Case 2. queue was frozen,user-space caught up,
				* now the link went idle && the timer fired.
				* We don't have a block to close.So we open this
				* block and restart the timer.
				* opening a block thaws the queue,restarts timer
				* Thawing/timer-refresh is a side effect.
				*/
				prb_open_block(pkc, pbd);
				goto out;
			}
		}
	}

refresh_timer:
	_prb_refresh_rx_retire_blk_timer(pkc);

out:
	spin_unlock(&po->sk.sk_receive_queue.lock);
}

static void prb_flush_block(struct tpacket_kbdq_core *pkc1,
		struct tpacket_block_desc *pbd1, __u32 status)
{
	/* Flush everything minus the block header */

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE == 1
	u8 *start, *end;

	start = (u8 *)pbd1;

	/* Skip the block header(we know header WILL fit in 4K) */
	start += PAGE_SIZE;

	end = (u8 *)PAGE_ALIGN((unsigned long)pkc1->pkblk_end);
	for (; start < end; start += PAGE_SIZE)
		flush_dcache_page(pgv_to_page(start));

	smp_wmb();
#endif

	/* Now update the block status. */

	BLOCK_STATUS(pbd1) = status;

	/* Flush the block header */

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE == 1
	start = (u8 *)pbd1;
	flush_dcache_page(pgv_to_page(start));

	smp_wmb();
#endif
}

/*
 * Side effect:
 *
 * 1) flush the block
 * 2) Increment active_blk_num
 *
 * Note:We DONT refresh the timer on purpose.
 *	Because almost always the next block will be opened.
 */
static void prb_close_block(struct tpacket_kbdq_core *pkc1,
		struct tpacket_block_desc *pbd1,
		struct packet_sock *po, unsigned int stat)
{
	__u32 status = TP_STATUS_USER | stat;

	struct tpacket3_hdr *last_pkt;
	struct tpacket_hdr_v1 *h1 = &pbd1->hdr.bh1;
	struct sock *sk = &po->sk;

	if (atomic_read(&po->tp_drops))
		status |= TP_STATUS_LOSING;

	last_pkt = (struct tpacket3_hdr *)pkc1->prev;
	last_pkt->tp_next_offset = 0;

	/* Get the ts of the last pkt */
	if (BLOCK_NUM_PKTS(pbd1)) {
		h1->ts_last_pkt.ts_sec = last_pkt->tp_sec;
		h1->ts_last_pkt.ts_nsec	= last_pkt->tp_nsec;
	} else {
		/* Ok, we tmo'd - so get the current time.
		 *
		 * It shouldn't really happen as we don't close empty
		 * blocks. See prb_retire_rx_blk_timer_expired().
		 */
		struct timespec64 ts;
		ktime_get_real_ts64(&ts);
		h1->ts_last_pkt.ts_sec = ts.tv_sec;
		h1->ts_last_pkt.ts_nsec	= ts.tv_nsec;
	}

	smp_wmb();

	/* Flush the block */
	prb_flush_block(pkc1, pbd1, status);

	sk->sk_data_ready(sk);

	pkc1->kactive_blk_num = GET_NEXT_PRB_BLK_NUM(pkc1);
}

static void prb_thaw_queue(struct tpacket_kbdq_core *pkc)
{
	pkc->reset_pending_on_curr_blk = 0;
}

/*
 * Side effect of opening a block:
 *
 * 1) prb_queue is thawed.
 * 2) retire_blk_timer is refreshed.
 *
 */
static void prb_open_block(struct tpacket_kbdq_core *pkc1,
	struct tpacket_block_desc *pbd1)
{
	struct timespec64 ts;
	struct tpacket_hdr_v1 *h1 = &pbd1->hdr.bh1;

	smp_rmb();

	/* We could have just memset this but we will lose the
	 * flexibility of making the priv area sticky
	 */

	BLOCK_SNUM(pbd1) = pkc1->knxt_seq_num++;
	BLOCK_NUM_PKTS(pbd1) = 0;
	BLOCK_LEN(pbd1) = BLK_PLUS_PRIV(pkc1->blk_sizeof_priv);

	ktime_get_real_ts64(&ts);

	h1->ts_first_pkt.ts_sec = ts.tv_sec;
	h1->ts_first_pkt.ts_nsec = ts.tv_nsec;

	pkc1->pkblk_start = (char *)pbd1;
	pkc1->nxt_offset = pkc1->pkblk_start + BLK_PLUS_PRIV(pkc1->blk_sizeof_priv);

	BLOCK_O2FP(pbd1) = (__u32)BLK_PLUS_PRIV(pkc1->blk_sizeof_priv);
	BLOCK_O2PRIV(pbd1) = BLK_HDR_LEN;

	pbd1->version = pkc1->version;
	pkc1->prev = pkc1->nxt_offset;
	pkc1->pkblk_end = pkc1->pkblk_start + pkc1->kblk_size;

	prb_thaw_queue(pkc1);
	_prb_refresh_rx_retire_blk_timer(pkc1);

	smp_wmb();
}

/*
 * Queue freeze logic:
 * 1) Assume tp_block_nr = 8 blocks.
 * 2) At time 't0', user opens Rx ring.
 * 3) Some time past 't0', kernel starts filling blocks starting from 0 .. 7
 * 4) user-space is either sleeping or processing block '0'.
 * 5) tpacket_rcv is currently filling block '7', since there is no space left,
 *    it will close block-7,loop around and try to fill block '0'.
 *    call-flow:
 *    __packet_lookup_frame_in_block
 *      prb_retire_current_block()
 *      prb_dispatch_next_block()
 *        |->(BLOCK_STATUS == USER) evaluates to true
 *    5.1) Since block-0 is currently in-use, we just freeze the queue.
 * 6) Now there are two cases:
 *    6.1) Link goes idle right after the queue is frozen.
 *         But remember, the last open_block() refreshed the timer.
 *         When this timer expires,it will refresh itself so that we can
 *         re-open block-0 in near future.
 *    6.2) Link is busy and keeps on receiving packets. This is a simple
 *         case and __packet_lookup_frame_in_block will check if block-0
 *         is free and can now be re-used.
 */
static void prb_freeze_queue(struct tpacket_kbdq_core *pkc,
				  struct packet_sock *po)
{
	pkc->reset_pending_on_curr_blk = 1;
	po->stats.stats3.tp_freeze_q_cnt++;
}

#define TOTAL_PKT_LEN_INCL_ALIGN(length) (ALIGN((length), V3_ALIGNMENT))

/*
 * If the next block is free then we will dispatch it
 * and return a good offset.
 * Else, we will freeze the queue.
 * So, caller must check the return value.
 */
static void *prb_dispatch_next_block(struct tpacket_kbdq_core *pkc,
		struct packet_sock *po)
{
	struct tpacket_block_desc *pbd;

	smp_rmb();

	/* 1. Get current block num */
	pbd = GET_CURR_PBLOCK_DESC_FROM_CORE(pkc);

	/* 2. If this block is currently in_use then freeze the queue */
	if (TP_STATUS_USER & BLOCK_STATUS(pbd)) {
		prb_freeze_queue(pkc, po);
		return NULL;
	}

	/*
	 * 3.
	 * open this block and return the offset where the first packet
	 * needs to get stored.
	 */
	prb_open_block(pkc, pbd);
	return (void *)pkc->nxt_offset;
}

static void prb_retire_current_block(struct tpacket_kbdq_core *pkc,
		struct packet_sock *po, unsigned int status)
{
	struct tpacket_block_desc *pbd = GET_CURR_PBLOCK_DESC_FROM_CORE(pkc);

	/* retire/close the current block */
	if (likely(TP_STATUS_KERNEL == BLOCK_STATUS(pbd))) {
		/*
		 * Plug the case where copy_bits() is in progress on
		 * cpu-0 and tpacket_rcv() got invoked on cpu-1, didn't
		 * have space to copy the pkt in the current block and
		 * called prb_retire_current_block()
		 *
		 * We don't need to worry about the TMO case because
		 * the timer-handler already handled this case.
		 */
		if (!(status & TP_STATUS_BLK_TMO)) {
			/* Waiting for skb_copy_bits to finish... */
			write_lock(&pkc->blk_fill_in_prog_lock);
			write_unlock(&pkc->blk_fill_in_prog_lock);
		}
		prb_close_block(pkc, pbd, po, status);
		return;
	}
}

static int prb_curr_blk_in_use(struct tpacket_block_desc *pbd)
{
	return TP_STATUS_USER & BLOCK_STATUS(pbd);
}

static int prb_queue_frozen(struct tpacket_kbdq_core *pkc)
{
	return pkc->reset_pending_on_curr_blk;
}

static void prb_clear_blk_fill_status(struct packet_ring_buffer *rb)
	__releases(&pkc->blk_fill_in_prog_lock)
{
	struct tpacket_kbdq_core *pkc  = GET_PBDQC_FROM_RB(rb);

	read_unlock(&pkc->blk_fill_in_prog_lock);
}

static void prb_fill_rxhash(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	ppd->hv1.tp_rxhash = skb_get_hash(pkc->skb);
}

static void prb_clear_rxhash(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	ppd->hv1.tp_rxhash = 0;
}

static void prb_fill_vlan_info(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	if (skb_vlan_tag_present(pkc->skb)) {
		ppd->hv1.tp_vlan_tci = skb_vlan_tag_get(pkc->skb);
		ppd->hv1.tp_vlan_tpid = ntohs(pkc->skb->vlan_proto);
		ppd->tp_status = TP_STATUS_VLAN_VALID | TP_STATUS_VLAN_TPID_VALID;
	} else {
		ppd->hv1.tp_vlan_tci = 0;
		ppd->hv1.tp_vlan_tpid = 0;
		ppd->tp_status = TP_STATUS_AVAILABLE;
	}
}

static void prb_run_all_ft_ops(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	ppd->hv1.tp_padding = 0;
	prb_fill_vlan_info(pkc, ppd);

	if (pkc->feature_req_word & TP_FT_REQ_FILL_RXHASH)
		prb_fill_rxhash(pkc, ppd);
	else
		prb_clear_rxhash(pkc, ppd);
}

static void prb_fill_curr_block(char *curr,
				struct tpacket_kbdq_core *pkc,
				struct tpacket_block_desc *pbd,
				unsigned int len)
	__acquires(&pkc->blk_fill_in_prog_lock)
{
	struct tpacket3_hdr *ppd;

	ppd  = (struct tpacket3_hdr *)curr;
	ppd->tp_next_offset = TOTAL_PKT_LEN_INCL_ALIGN(len);
	pkc->prev = curr;
	pkc->nxt_offset += TOTAL_PKT_LEN_INCL_ALIGN(len);
	BLOCK_LEN(pbd) += TOTAL_PKT_LEN_INCL_ALIGN(len);
	BLOCK_NUM_PKTS(pbd) += 1;
	read_lock(&pkc->blk_fill_in_prog_lock);
	prb_run_all_ft_ops(pkc, ppd);
}

/* Assumes caller has the sk->rx_queue.lock */
static void *__packet_lookup_frame_in_block(struct packet_sock *po,
					    struct sk_buff *skb,
					    unsigned int len
					    )
{
	struct tpacket_kbdq_core *pkc;
	struct tpacket_block_desc *pbd;
	char *curr, *end;

	pkc = GET_PBDQC_FROM_RB(&po->rx_ring);
	pbd = GET_CURR_PBLOCK_DESC_FROM_CORE(pkc);

	/* Queue is frozen when user space is lagging behind */
	if (prb_queue_frozen(pkc)) {
		/*
		 * Check if that last block which caused the queue to freeze,
		 * is still in_use by user-space.
		 */
		if (prb_curr_blk_in_use(pbd)) {
			/* Can't record this packet */
			return NULL;
		} else {
			/*
			 * Ok, the block was released by user-space.
			 * Now let's open that block.
			 * opening a block also thaws the queue.
			 * Thawing is a side effect.
			 */
			prb_open_block(pkc, pbd);
		}
	}

	smp_mb();
	curr = pkc->nxt_offset;
	pkc->skb = skb;
	end = (char *)pbd + pkc->kblk_size;

	/* first try the current block */
	if (curr+TOTAL_PKT_LEN_INCL_ALIGN(len) < end) {
		prb_fill_curr_block(curr, pkc, pbd, len);
		return (void *)curr;
	}

	/* Ok, close the current block */
	prb_retire_current_block(pkc, po, 0);

	/* Now, try to dispatch the next block */
	curr = (char *)prb_dispatch_next_block(pkc, po);
	if (curr) {
		pbd = GET_CURR_PBLOCK_DESC_FROM_CORE(pkc);
		prb_fill_curr_block(curr, pkc, pbd, len);
		return (void *)curr;
	}

	/*
	 * No free blocks are available.user_space hasn't caught up yet.
	 * Queue was just frozen and now this packet will get dropped.
	 */
	return NULL;
}

static void *packet_current_rx_frame(struct packet_sock *po,
					    struct sk_buff *skb,
					    int status, unsigned int len)
{
	char *curr = NULL;
	switch (po->tp_version) {
	case TPACKET_V1:
	case TPACKET_V2:
		curr = packet_lookup_frame(po, &po->rx_ring,
					po->rx_ring.head, status);
		return curr;
	case TPACKET_V3:
		return __packet_lookup_frame_in_block(po, skb, len);
	default:
		WARN(1, "TPACKET version not supported\n");
		BUG();
		return NULL;
	}
}

static void *prb_lookup_block(const struct packet_sock *po,
			      const struct packet_ring_buffer *rb,
			      unsigned int idx,
			      int status)
{
	struct tpacket_kbdq_core *pkc  = GET_PBDQC_FROM_RB(rb);
	struct tpacket_block_desc *pbd = GET_PBLOCK_DESC(pkc, idx);

	if (status != BLOCK_STATUS(pbd))
		return NULL;
	return pbd;
}

static int prb_previous_blk_num(struct packet_ring_buffer *rb)
{
	unsigned int prev;
	if (rb->prb_bdqc.kactive_blk_num)
		prev = rb->prb_bdqc.kactive_blk_num-1;
	else
		prev = rb->prb_bdqc.knum_blocks-1;
	return prev;
}

/* Assumes caller has held the rx_queue.lock */
static void *__prb_previous_block(struct packet_sock *po,
					 struct packet_ring_buffer *rb,
					 int status)
{
	unsigned int previous = prb_previous_blk_num(rb);
	return prb_lookup_block(po, rb, previous, status);
}

static void *packet_previous_rx_frame(struct packet_sock *po,
					     struct packet_ring_buffer *rb,
					     int status)
{
	if (po->tp_version <= TPACKET_V2)
		return packet_previous_frame(po, rb, status);

	return __prb_previous_block(po, rb, status);
}

static void packet_increment_rx_head(struct packet_sock *po,
					    struct packet_ring_buffer *rb)
{
	switch (po->tp_version) {
	case TPACKET_V1:
	case TPACKET_V2:
		return packet_increment_head(rb);
	case TPACKET_V3:
	default:
		WARN(1, "TPACKET version not supported.\n");
		BUG();
		return;
	}
}

static void *packet_previous_frame(struct packet_sock *po,
		struct packet_ring_buffer *rb,
		int status)
{
	unsigned int previous = rb->head ? rb->head - 1 : rb->frame_max;
	return packet_lookup_frame(po, rb, previous, status);
}

static void packet_increment_head(struct packet_ring_buffer *buff)
{
	buff->head = buff->head != buff->frame_max ? buff->head+1 : 0;
}

static void packet_inc_pending(struct packet_ring_buffer *rb)
{
	this_cpu_inc(*rb->pending_refcnt);
}

static void packet_dec_pending(struct packet_ring_buffer *rb)
{
	this_cpu_dec(*rb->pending_refcnt);
}

static unsigned int packet_read_pending(const struct packet_ring_buffer *rb)
{
	unsigned int refcnt = 0;
	int cpu;

	/* We don't use pending refcount in rx_ring. */
	if (rb->pending_refcnt == NULL)
		return 0;

	for_each_possible_cpu(cpu)
		refcnt += *per_cpu_ptr(rb->pending_refcnt, cpu);

	return refcnt;
}

static int packet_alloc_pending(struct packet_sock *po)
{
	po->rx_ring.pending_refcnt = NULL;

	po->tx_ring.pending_refcnt = alloc_percpu(unsigned int);
	if (unlikely(po->tx_ring.pending_refcnt == NULL))
		return -ENOBUFS;

	return 0;
}

static void packet_free_pending(struct packet_sock *po)
{
	free_percpu(po->tx_ring.pending_refcnt);
}

#define ROOM_POW_OFF	2
#define ROOM_NONE	0x0
#define ROOM_LOW	0x1
#define ROOM_NORMAL	0x2

static bool __tpacket_has_room(const struct packet_sock *po, int pow_off)
{
	int idx, len;

	len = READ_ONCE(po->rx_ring.frame_max) + 1;
	idx = READ_ONCE(po->rx_ring.head);
	if (pow_off)
		idx += len >> pow_off;
	if (idx >= len)
		idx -= len;
	return packet_lookup_frame(po, &po->rx_ring, idx, TP_STATUS_KERNEL);
}

static bool __tpacket_v3_has_room(const struct packet_sock *po, int pow_off)
{
	int idx, len;

	len = READ_ONCE(po->rx_ring.prb_bdqc.knum_blocks);
	idx = READ_ONCE(po->rx_ring.prb_bdqc.kactive_blk_num);
	if (pow_off)
		idx += len >> pow_off;
	if (idx >= len)
		idx -= len;
	return prb_lookup_block(po, &po->rx_ring, idx, TP_STATUS_KERNEL);
}

static int __packet_rcv_has_room(const struct packet_sock *po,
				 const struct sk_buff *skb)
{
	const struct sock *sk = &po->sk;
	int ret = ROOM_NONE;

	if (po->prot_hook.func != tpacket_rcv) {
		int rcvbuf = READ_ONCE(sk->sk_rcvbuf);
		int avail = rcvbuf - atomic_read(&sk->sk_rmem_alloc)
				   - (skb ? skb->truesize : 0);

		if (avail > (rcvbuf >> ROOM_POW_OFF))
			return ROOM_NORMAL;
		else if (avail > 0)
			return ROOM_LOW;
		else
			return ROOM_NONE;
	}

	if (po->tp_version == TPACKET_V3) {
		if (__tpacket_v3_has_room(po, ROOM_POW_OFF))
			ret = ROOM_NORMAL;
		else if (__tpacket_v3_has_room(po, 0))
			ret = ROOM_LOW;
	} else {
		if (__tpacket_has_room(po, ROOM_POW_OFF))
			ret = ROOM_NORMAL;
		else if (__tpacket_has_room(po, 0))
			ret = ROOM_LOW;
	}

	return ret;
}

static int packet_rcv_has_room(struct packet_sock *po, struct sk_buff *skb)
{
	bool pressure;
	int ret;

	ret = __packet_rcv_has_room(po, skb);
	pressure = ret != ROOM_NORMAL;

	if (packet_sock_flag(po, PACKET_SOCK_PRESSURE) != pressure)
		packet_sock_flag_set(po, PACKET_SOCK_PRESSURE, pressure);

	return ret;
}

static void packet_rcv_try_clear_pressure(struct packet_sock *po)
{
	if (packet_sock_flag(po, PACKET_SOCK_PRESSURE) &&
	    __packet_rcv_has_room(po, NULL) == ROOM_NORMAL)
		packet_sock_flag_set(po, PACKET_SOCK_PRESSURE, false);
}

static void packet_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_error_queue);

	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(refcount_read(&sk->sk_wmem_alloc));

	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Attempt to release alive packet socket: %p\n", sk);
		return;
	}
}

static bool fanout_flow_is_huge(struct packet_sock *po, struct sk_buff *skb)
{
	u32 *history = po->rollover->history;
	u32 victim, rxhash;
	int i, count = 0;

	rxhash = skb_get_hash(skb);
	for (i = 0; i < ROLLOVER_HLEN; i++)
		if (READ_ONCE(history[i]) == rxhash)
			count++;

	victim = get_random_u32_below(ROLLOVER_HLEN);

	/* Avoid dirtying the cache line if possible */
	if (READ_ONCE(history[victim]) != rxhash)
		WRITE_ONCE(history[victim], rxhash);

	return count > (ROLLOVER_HLEN >> 1);
}

static unsigned int fanout_demux_hash(struct packet_fanout *f,
				      struct sk_buff *skb,
				      unsigned int num)
{
	return reciprocal_scale(__skb_get_hash_symmetric(skb), num);
}

static unsigned int fanout_demux_lb(struct packet_fanout *f,
				    struct sk_buff *skb,
				    unsigned int num)
{
	unsigned int val = atomic_inc_return(&f->rr_cur);

	return val % num;
}

static unsigned int fanout_demux_cpu(struct packet_fanout *f,
				     struct sk_buff *skb,
				     unsigned int num)
{
	return smp_processor_id() % num;
}

static unsigned int fanout_demux_rnd(struct packet_fanout *f,
				     struct sk_buff *skb,
				     unsigned int num)
{
	return get_random_u32_below(num);
}

static unsigned int fanout_demux_rollover(struct packet_fanout *f,
					  struct sk_buff *skb,
					  unsigned int idx, bool try_self,
					  unsigned int num)
{
	struct packet_sock *po, *po_next, *po_skip = NULL;
	unsigned int i, j, room = ROOM_NONE;

	po = pkt_sk(rcu_dereference(f->arr[idx]));

	if (try_self) {
		room = packet_rcv_has_room(po, skb);
		if (room == ROOM_NORMAL ||
		    (room == ROOM_LOW && !fanout_flow_is_huge(po, skb)))
			return idx;
		po_skip = po;
	}

	i = j = min_t(int, po->rollover->sock, num - 1);
	do {
		po_next = pkt_sk(rcu_dereference(f->arr[i]));
		if (po_next != po_skip &&
		    !packet_sock_flag(po_next, PACKET_SOCK_PRESSURE) &&
		    packet_rcv_has_room(po_next, skb) == ROOM_NORMAL) {
			if (i != j)
				po->rollover->sock = i;
			atomic_long_inc(&po->rollover->num);
			if (room == ROOM_LOW)
				atomic_long_inc(&po->rollover->num_huge);
			return i;
		}

		if (++i == num)
			i = 0;
	} while (i != j);

	atomic_long_inc(&po->rollover->num_failed);
	return idx;
}

static unsigned int fanout_demux_qm(struct packet_fanout *f,
				    struct sk_buff *skb,
				    unsigned int num)
{
	return skb_get_queue_mapping(skb) % num;
}

static unsigned int fanout_demux_bpf(struct packet_fanout *f,
				     struct sk_buff *skb,
				     unsigned int num)
{
	struct bpf_prog *prog;
	unsigned int ret = 0;

	rcu_read_lock();
	prog = rcu_dereference(f->bpf_prog);
	if (prog)
		ret = bpf_prog_run_clear_cb(prog, skb) % num;
	rcu_read_unlock();

	return ret;
}

static bool fanout_has_flag(struct packet_fanout *f, u16 flag)
{
	return f->flags & (flag >> 8);
}

static int packet_rcv_fanout(struct sk_buff *skb, struct net_device *dev,
			     struct packet_type *pt, struct net_device *orig_dev)
{
	struct packet_fanout *f = pt->af_packet_priv;
	unsigned int num = READ_ONCE(f->num_members);
	struct net *net = read_pnet(&f->net);
	struct packet_sock *po;
	unsigned int idx;

	if (!net_eq(dev_net(dev), net) || !num) {
		kfree_skb(skb);
		return 0;
	}

	if (fanout_has_flag(f, PACKET_FANOUT_FLAG_DEFRAG)) {
		skb = ip_check_defrag(net, skb, IP_DEFRAG_AF_PACKET);
		if (!skb)
			return 0;
	}
	switch (f->type) {
	case PACKET_FANOUT_HASH:
	default:
		idx = fanout_demux_hash(f, skb, num);
		break;
	case PACKET_FANOUT_LB:
		idx = fanout_demux_lb(f, skb, num);
		break;
	case PACKET_FANOUT_CPU:
		idx = fanout_demux_cpu(f, skb, num);
		break;
	case PACKET_FANOUT_RND:
		idx = fanout_demux_rnd(f, skb, num);
		break;
	case PACKET_FANOUT_QM:
		idx = fanout_demux_qm(f, skb, num);
		break;
	case PACKET_FANOUT_ROLLOVER:
		idx = fanout_demux_rollover(f, skb, 0, false, num);
		break;
	case PACKET_FANOUT_CBPF:
	case PACKET_FANOUT_EBPF:
		idx = fanout_demux_bpf(f, skb, num);
		break;
	}

	if (fanout_has_flag(f, PACKET_FANOUT_FLAG_ROLLOVER))
		idx = fanout_demux_rollover(f, skb, idx, true, num);

	po = pkt_sk(rcu_dereference(f->arr[idx]));
	return po->prot_hook.func(skb, dev, &po->prot_hook, orig_dev);
}

DEFINE_MUTEX(fanout_mutex);
EXPORT_SYMBOL_GPL(fanout_mutex);
static LIST_HEAD(fanout_list);
static u16 fanout_next_id;

static void __fanout_link(struct sock *sk, struct packet_sock *po)
{
	struct packet_fanout *f = po->fanout;

	spin_lock(&f->lock);
	rcu_assign_pointer(f->arr[f->num_members], sk);
	smp_wmb();
	f->num_members++;
	if (f->num_members == 1)
		dev_add_pack(&f->prot_hook);
	spin_unlock(&f->lock);
}

static void __fanout_unlink(struct sock *sk, struct packet_sock *po)
{
	struct packet_fanout *f = po->fanout;
	int i;

	spin_lock(&f->lock);
	for (i = 0; i < f->num_members; i++) {
		if (rcu_dereference_protected(f->arr[i],
					      lockdep_is_held(&f->lock)) == sk)
			break;
	}
	BUG_ON(i >= f->num_members);
	rcu_assign_pointer(f->arr[i],
			   rcu_dereference_protected(f->arr[f->num_members - 1],
						     lockdep_is_held(&f->lock)));
	f->num_members--;
	if (f->num_members == 0)
		__dev_remove_pack(&f->prot_hook);
	spin_unlock(&f->lock);
}

static bool match_fanout_group(struct packet_type *ptype, struct sock *sk)
{
	if (sk->sk_family != PF_PACKET)
		return false;

	return ptype->af_packet_priv == pkt_sk(sk)->fanout;
}

static void fanout_init_data(struct packet_fanout *f)
{
	switch (f->type) {
	case PACKET_FANOUT_LB:
		atomic_set(&f->rr_cur, 0);
		break;
	case PACKET_FANOUT_CBPF:
	case PACKET_FANOUT_EBPF:
		RCU_INIT_POINTER(f->bpf_prog, NULL);
		break;
	}
}

static void __fanout_set_data_bpf(struct packet_fanout *f, struct bpf_prog *new)
{
	struct bpf_prog *old;

	spin_lock(&f->lock);
	old = rcu_dereference_protected(f->bpf_prog, lockdep_is_held(&f->lock));
	rcu_assign_pointer(f->bpf_prog, new);
	spin_unlock(&f->lock);

	if (old) {
		synchronize_net();
		bpf_prog_destroy(old);
	}
}

static int fanout_set_data_cbpf(struct packet_sock *po, sockptr_t data,
				unsigned int len)
{
	struct bpf_prog *new;
	struct sock_fprog fprog;
	int ret;

	if (sock_flag(&po->sk, SOCK_FILTER_LOCKED))
		return -EPERM;

	ret = copy_bpf_fprog_from_user(&fprog, data, len);
	if (ret)
		return ret;

	ret = bpf_prog_create_from_user(&new, &fprog, NULL, false);
	if (ret)
		return ret;

	__fanout_set_data_bpf(po->fanout, new);
	return 0;
}

static int fanout_set_data_ebpf(struct packet_sock *po, sockptr_t data,
				unsigned int len)
{
	struct bpf_prog *new;
	u32 fd;

	if (sock_flag(&po->sk, SOCK_FILTER_LOCKED))
		return -EPERM;
	if (len != sizeof(fd))
		return -EINVAL;
	if (copy_from_sockptr(&fd, data, len))
		return -EFAULT;

	new = bpf_prog_get_type(fd, BPF_PROG_TYPE_SOCKET_FILTER);
	if (IS_ERR(new))
		return PTR_ERR(new);

	__fanout_set_data_bpf(po->fanout, new);
	return 0;
}

static int fanout_set_data(struct packet_sock *po, sockptr_t data,
			   unsigned int len)
{
	switch (po->fanout->type) {
	case PACKET_FANOUT_CBPF:
		return fanout_set_data_cbpf(po, data, len);
	case PACKET_FANOUT_EBPF:
		return fanout_set_data_ebpf(po, data, len);
	default:
		return -EINVAL;
	}
}

static void fanout_release_data(struct packet_fanout *f)
{
	switch (f->type) {
	case PACKET_FANOUT_CBPF:
	case PACKET_FANOUT_EBPF:
		__fanout_set_data_bpf(f, NULL);
	}
}

static bool __fanout_id_is_free(struct sock *sk, u16 candidate_id)
{
	struct packet_fanout *f;

	list_for_each_entry(f, &fanout_list, list) {
		if (f->id == candidate_id &&
		    read_pnet(&f->net) == sock_net(sk)) {
			return false;
		}
	}
	return true;
}

static bool fanout_find_new_id(struct sock *sk, u16 *new_id)
{
	u16 id = fanout_next_id;

	do {
		if (__fanout_id_is_free(sk, id)) {
			*new_id = id;
			fanout_next_id = id + 1;
			return true;
		}

		id++;
	} while (id != fanout_next_id);

	return false;
}

static int fanout_add(struct sock *sk, struct fanout_args *args)
{
	struct packet_rollover *rollover = NULL;
	struct packet_sock *po = pkt_sk(sk);
	u16 type_flags = args->type_flags;
	struct packet_fanout *f, *match;
	u8 type = type_flags & 0xff;
	u8 flags = type_flags >> 8;
	u16 id = args->id;
	int err;

	switch (type) {
	case PACKET_FANOUT_ROLLOVER:
		if (type_flags & PACKET_FANOUT_FLAG_ROLLOVER)
			return -EINVAL;
		break;
	case PACKET_FANOUT_HASH:
	case PACKET_FANOUT_LB:
	case PACKET_FANOUT_CPU:
	case PACKET_FANOUT_RND:
	case PACKET_FANOUT_QM:
	case PACKET_FANOUT_CBPF:
	case PACKET_FANOUT_EBPF:
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&fanout_mutex);

	err = -EALREADY;
	if (po->fanout)
		goto out;

	if (type == PACKET_FANOUT_ROLLOVER ||
	    (type_flags & PACKET_FANOUT_FLAG_ROLLOVER)) {
		err = -ENOMEM;
		rollover = kzalloc(sizeof(*rollover), GFP_KERNEL);
		if (!rollover)
			goto out;
		atomic_long_set(&rollover->num, 0);
		atomic_long_set(&rollover->num_huge, 0);
		atomic_long_set(&rollover->num_failed, 0);
	}

	if (type_flags & PACKET_FANOUT_FLAG_UNIQUEID) {
		if (id != 0) {
			err = -EINVAL;
			goto out;
		}
		if (!fanout_find_new_id(sk, &id)) {
			err = -ENOMEM;
			goto out;
		}
		/* ephemeral flag for the first socket in the group: drop it */
		flags &= ~(PACKET_FANOUT_FLAG_UNIQUEID >> 8);
	}

	match = NULL;
	list_for_each_entry(f, &fanout_list, list) {
		if (f->id == id &&
		    read_pnet(&f->net) == sock_net(sk)) {
			match = f;
			break;
		}
	}
	err = -EINVAL;
	if (match) {
		if (match->flags != flags)
			goto out;
		if (args->max_num_members &&
		    args->max_num_members != match->max_num_members)
			goto out;
	} else {
		if (args->max_num_members > PACKET_FANOUT_MAX)
			goto out;
		if (!args->max_num_members)
			/* legacy PACKET_FANOUT_MAX */
			args->max_num_members = 256;
		err = -ENOMEM;
		match = kvzalloc(struct_size(match, arr, args->max_num_members),
				 GFP_KERNEL);
		if (!match)
			goto out;
		write_pnet(&match->net, sock_net(sk));
		match->id = id;
		match->type = type;
		match->flags = flags;
		INIT_LIST_HEAD(&match->list);
		spin_lock_init(&match->lock);
		refcount_set(&match->sk_ref, 0);
		fanout_init_data(match);
		match->prot_hook.type = po->prot_hook.type;
		match->prot_hook.dev = po->prot_hook.dev;
		match->prot_hook.func = packet_rcv_fanout;
		match->prot_hook.af_packet_priv = match;
		match->prot_hook.af_packet_net = read_pnet(&match->net);
		match->prot_hook.id_match = match_fanout_group;
		match->max_num_members = args->max_num_members;
		match->prot_hook.ignore_outgoing = type_flags & PACKET_FANOUT_FLAG_IGNORE_OUTGOING;
		list_add(&match->list, &fanout_list);
	}
	err = -EINVAL;

	spin_lock(&po->bind_lock);
	if (packet_sock_flag(po, PACKET_SOCK_RUNNING) &&
	    match->type == type &&
	    match->prot_hook.type == po->prot_hook.type &&
	    match->prot_hook.dev == po->prot_hook.dev) {
		err = -ENOSPC;
		if (refcount_read(&match->sk_ref) < match->max_num_members) {
			__dev_remove_pack(&po->prot_hook);

			/* Paired with packet_setsockopt(PACKET_FANOUT_DATA) */
			WRITE_ONCE(po->fanout, match);

			po->rollover = rollover;
			rollover = NULL;
			refcount_set(&match->sk_ref, refcount_read(&match->sk_ref) + 1);
			__fanout_link(sk, po);
			err = 0;
		}
	}
	spin_unlock(&po->bind_lock);

	if (err && !refcount_read(&match->sk_ref)) {
		list_del(&match->list);
		kvfree(match);
	}

out:
	kfree(rollover);
	mutex_unlock(&fanout_mutex);
	return err;
}

/* If pkt_sk(sk)->fanout->sk_ref is zero, this function removes
 * pkt_sk(sk)->fanout from fanout_list and returns pkt_sk(sk)->fanout.
 * It is the responsibility of the caller to call fanout_release_data() and
 * free the returned packet_fanout (after synchronize_net())
 */
static struct packet_fanout *fanout_release(struct sock *sk)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_fanout *f;

	mutex_lock(&fanout_mutex);
	f = po->fanout;
	if (f) {
		po->fanout = NULL;

		if (refcount_dec_and_test(&f->sk_ref))
			list_del(&f->list);
		else
			f = NULL;
	}
	mutex_unlock(&fanout_mutex);

	return f;
}

static bool packet_extra_vlan_len_allowed(const struct net_device *dev,
					  struct sk_buff *skb)
{
	/* Earlier code assumed this would be a VLAN pkt, double-check
	 * this now that we have the actual packet in hand. We can only
	 * do this check on Ethernet devices.
	 */
	if (unlikely(dev->type != ARPHRD_ETHER))
		return false;

	skb_reset_mac_header(skb);
	return likely(eth_hdr(skb)->h_proto == htons(ETH_P_8021Q));
}

static const struct proto_ops packet_ops;

static const struct proto_ops packet_ops_spkt;

static int packet_rcv_spkt(struct sk_buff *skb, struct net_device *dev,
			   struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct sockaddr_pkt *spkt;

	/*
	 *	When we registered the protocol we saved the socket in the data
	 *	field for just this event.
	 */

	sk = pt->af_packet_priv;

	/*
	 *	Yank back the headers [hope the device set this
	 *	right or kerboom...]
	 *
	 *	Incoming packets have ll header pulled,
	 *	push it back.
	 *
	 *	For outgoing ones skb->data == skb_mac_header(skb)
	 *	so that this procedure is noop.
	 */

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto out;

	if (!net_eq(dev_net(dev), sock_net(sk)))
		goto out;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		goto oom;

	/* drop any routing info */
	skb_dst_drop(skb);

	/* drop conntrack reference */
	nf_reset_ct(skb);

	spkt = &PACKET_SKB_CB(skb)->sa.pkt;

	skb_push(skb, skb->data - skb_mac_header(skb));

	/*
	 *	The SOCK_PACKET socket receives _all_ frames.
	 */

	spkt->spkt_family = dev->type;
	strscpy(spkt->spkt_device, dev->name, sizeof(spkt->spkt_device));
	spkt->spkt_protocol = skb->protocol;

	/*
	 *	Charge the memory to the socket. This is done specifically
	 *	to prevent sockets using all the memory up.
	 */

	if (sock_queue_rcv_skb(sk, skb) == 0)
		return 0;

out:
	kfree_skb(skb);
oom:
	return 0;
}

static void packet_parse_headers(struct sk_buff *skb, struct socket *sock)
{
	int depth;

	if ((!skb->protocol || skb->protocol == htons(ETH_P_ALL)) &&
	    sock->type == SOCK_RAW) {
		skb_reset_mac_header(skb);
		skb->protocol = dev_parse_header_protocol(skb);
	}

	/* Move network header to the right position for VLAN tagged packets */
	if (likely(skb->dev->type == ARPHRD_ETHER) &&
	    eth_type_vlan(skb->protocol) &&
	    vlan_get_protocol_and_depth(skb, skb->protocol, &depth) != 0)
		skb_set_network_header(skb, depth);

	skb_probe_transport_header(skb);
}

/*
 *	Output a raw packet to a device layer. This bypasses all the other
 *	protocol layers and you must therefore supply it with a complete frame
 */

static int packet_sendmsg_spkt(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	struct sock *sk = sock->sk;
	DECLARE_SOCKADDR(struct sockaddr_pkt *, saddr, msg->msg_name);
	struct sk_buff *skb = NULL;
	struct net_device *dev;
	struct sockcm_cookie sockc;
	__be16 proto = 0;
	int err;
	int extra_len = 0;

	/*
	 *	Get and verify the address.
	 */

	if (saddr) {
		if (msg->msg_namelen < sizeof(struct sockaddr))
			return -EINVAL;
		if (msg->msg_namelen == sizeof(struct sockaddr_pkt))
			proto = saddr->spkt_protocol;
	} else
		return -ENOTCONN;	/* SOCK_PACKET must be sent giving an address */

	/*
	 *	Find the device first to size check it
	 */

	saddr->spkt_device[sizeof(saddr->spkt_device) - 1] = 0;
retry:
	rcu_read_lock();
	dev = dev_get_by_name_rcu(sock_net(sk), saddr->spkt_device);
	err = -ENODEV;
	if (dev == NULL)
		goto out_unlock;

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_unlock;

	/*
	 * You may not queue a frame bigger than the mtu. This is the lowest level
	 * raw protocol and you must do your own fragmentation at this level.
	 */

	if (unlikely(sock_flag(sk, SOCK_NOFCS))) {
		if (!netif_supports_nofcs(dev)) {
			err = -EPROTONOSUPPORT;
			goto out_unlock;
		}
		extra_len = 4; /* We're doing our own CRC */
	}

	err = -EMSGSIZE;
	if (len > dev->mtu + dev->hard_header_len + VLAN_HLEN + extra_len)
		goto out_unlock;

	if (!skb) {
		size_t reserved = LL_RESERVED_SPACE(dev);
		int tlen = dev->needed_tailroom;
		unsigned int hhlen = dev->header_ops ? dev->hard_header_len : 0;

		rcu_read_unlock();
		skb = sock_wmalloc(sk, len + reserved + tlen, 0, GFP_KERNEL);
		if (skb == NULL)
			return -ENOBUFS;
		/* FIXME: Save some space for broken drivers that write a hard
		 * header at transmission time by themselves. PPP is the notable
		 * one here. This should really be fixed at the driver level.
		 */
		skb_reserve(skb, reserved);
		skb_reset_network_header(skb);

		/* Try to align data part correctly */
		if (hhlen) {
			skb->data -= hhlen;
			skb->tail -= hhlen;
			if (len < hhlen)
				skb_reset_network_header(skb);
		}
		err = memcpy_from_msg(skb_put(skb, len), msg, len);
		if (err)
			goto out_free;
		goto retry;
	}

	if (!dev_validate_header(dev, skb->data, len) || !skb->len) {
		err = -EINVAL;
		goto out_unlock;
	}
	if (len > (dev->mtu + dev->hard_header_len + extra_len) &&
	    !packet_extra_vlan_len_allowed(dev, skb)) {
		err = -EMSGSIZE;
		goto out_unlock;
	}

	sockcm_init(&sockc, sk);
	if (msg->msg_controllen) {
		err = sock_cmsg_send(sk, msg, &sockc);
		if (unlikely(err))
			goto out_unlock;
	}

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = READ_ONCE(sk->sk_priority);
	skb->mark = READ_ONCE(sk->sk_mark);
	skb->tstamp = sockc.transmit_time;

	skb_setup_tx_timestamp(skb, sockc.tsflags);

	if (unlikely(extra_len == 4))
		skb->no_fcs = 1;

	packet_parse_headers(skb, sock);

	dev_queue_xmit(skb);
	rcu_read_unlock();
	return len;

out_unlock:
	rcu_read_unlock();
out_free:
	kfree_skb(skb);
	return err;
}

static unsigned int run_filter(struct sk_buff *skb,
			       const struct sock *sk,
			       unsigned int res)
{
	struct sk_filter *filter;

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter != NULL)
		res = bpf_prog_run_clear_cb(filter->prog, skb);
	rcu_read_unlock();

	return res;
}

static int packet_rcv_vnet(struct msghdr *msg, const struct sk_buff *skb,
			   size_t *len, int vnet_hdr_sz)
{
	struct virtio_net_hdr_mrg_rxbuf vnet_hdr = { .num_buffers = 0 };

	if (*len < vnet_hdr_sz)
		return -EINVAL;
	*len -= vnet_hdr_sz;

	if (virtio_net_hdr_from_skb(skb, (struct virtio_net_hdr *)&vnet_hdr, vio_le(), true, 0))
		return -EINVAL;

	return memcpy_to_msg(msg, (void *)&vnet_hdr, vnet_hdr_sz);
}

/*
 * This function makes lazy skb cloning in hope that most of packets
 * are discarded by BPF.
 *
 * Note tricky part: we DO mangle shared skb! skb->data, skb->len
 * and skb->cb are mangled. It works because (and until) packets
 * falling here are owned by current CPU. Output packets are cloned
 * by dev_queue_xmit_nit(), input packets are processed by net_bh
 * sequentially, so that if we return skb to original state on exit,
 * we will not harm anyone.
 */

static int packet_rcv(struct sk_buff *skb, struct net_device *dev,
		      struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct sockaddr_ll *sll;
	struct packet_sock *po;
	u8 *skb_head = skb->data;
	int skb_len = skb->len;
	unsigned int snaplen, res;
	bool is_drop_n_account = false;

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto drop;

	sk = pt->af_packet_priv;
	po = pkt_sk(sk);

	if (!net_eq(dev_net(dev), sock_net(sk)))
		goto drop;

	skb->dev = dev;

	if (dev_has_header(dev)) {
		/* The device has an explicit notion of ll header,
		 * exported to higher levels.
		 *
		 * Otherwise, the device hides details of its frame
		 * structure, so that corresponding packet head is
		 * never delivered to user.
		 */
		if (sk->sk_type != SOCK_DGRAM)
			skb_push(skb, skb->data - skb_mac_header(skb));
		else if (skb->pkt_type == PACKET_OUTGOING) {
			/* Special case: outgoing packets have ll header at head */
			skb_pull(skb, skb_network_offset(skb));
		}
	}

	snaplen = skb->len;

	res = run_filter(skb, sk, snaplen);
	if (!res)
		goto drop_n_restore;
	if (snaplen > res)
		snaplen = res;

	if (atomic_read(&sk->sk_rmem_alloc) >= sk->sk_rcvbuf)
		goto drop_n_acct;

	if (skb_shared(skb)) {
		struct sk_buff *nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb == NULL)
			goto drop_n_acct;

		if (skb_head != skb->data) {
			skb->data = skb_head;
			skb->len = skb_len;
		}
		consume_skb(skb);
		skb = nskb;
	}

	sock_skb_cb_check_size(sizeof(*PACKET_SKB_CB(skb)) + MAX_ADDR_LEN - 8);

	sll = &PACKET_SKB_CB(skb)->sa.ll;
	sll->sll_hatype = dev->type;
	sll->sll_pkttype = skb->pkt_type;
	if (unlikely(packet_sock_flag(po, PACKET_SOCK_ORIGDEV)))
		sll->sll_ifindex = orig_dev->ifindex;
	else
		sll->sll_ifindex = dev->ifindex;

	sll->sll_halen = dev_parse_header(skb, sll->sll_addr);

	/* sll->sll_family and sll->sll_protocol are set in packet_recvmsg().
	 * Use their space for storing the original skb length.
	 */
	PACKET_SKB_CB(skb)->sa.origlen = skb->len;

	if (pskb_trim(skb, snaplen))
		goto drop_n_acct;

	skb_set_owner_r(skb, sk);
	skb->dev = NULL;
	skb_dst_drop(skb);

	/* drop conntrack reference */
	nf_reset_ct(skb);

	spin_lock(&sk->sk_receive_queue.lock);
	po->stats.stats1.tp_packets++;
	sock_skb_set_dropcount(sk, skb);
	skb_clear_delivery_time(skb);
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	spin_unlock(&sk->sk_receive_queue.lock);
	sk->sk_data_ready(sk);
	return 0;

drop_n_acct:
	is_drop_n_account = true;
	atomic_inc(&po->tp_drops);
	atomic_inc(&sk->sk_drops);

drop_n_restore:
	if (skb_head != skb->data && skb_shared(skb)) {
		skb->data = skb_head;
		skb->len = skb_len;
	}
drop:
	if (!is_drop_n_account)
		consume_skb(skb);
	else
		kfree_skb(skb);
	return 0;
}

static int tpacket_rcv(struct sk_buff *skb, struct net_device *dev,
		       struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct packet_sock *po;
	struct sockaddr_ll *sll;
	union tpacket_uhdr h;
	u8 *skb_head = skb->data;
	int skb_len = skb->len;
	unsigned int snaplen, res;
	unsigned long status = TP_STATUS_USER;
	unsigned short macoff, hdrlen;
	unsigned int netoff;
	struct sk_buff *copy_skb = NULL;
	struct timespec64 ts;
	__u32 ts_status;
	bool is_drop_n_account = false;
	unsigned int slot_id = 0;
	int vnet_hdr_sz = 0;

	/* struct tpacket{2,3}_hdr is aligned to a multiple of TPACKET_ALIGNMENT.
	 * We may add members to them until current aligned size without forcing
	 * userspace to call getsockopt(..., PACKET_HDRLEN, ...).
	 */
	BUILD_BUG_ON(TPACKET_ALIGN(sizeof(*h.h2)) != 32);
	BUILD_BUG_ON(TPACKET_ALIGN(sizeof(*h.h3)) != 48);

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto drop;

	sk = pt->af_packet_priv;
	po = pkt_sk(sk);

	if (!net_eq(dev_net(dev), sock_net(sk)))
		goto drop;

	if (dev_has_header(dev)) {
		if (sk->sk_type != SOCK_DGRAM)
			skb_push(skb, skb->data - skb_mac_header(skb));
		else if (skb->pkt_type == PACKET_OUTGOING) {
			/* Special case: outgoing packets have ll header at head */
			skb_pull(skb, skb_network_offset(skb));
		}
	}

	snaplen = skb->len;

	res = run_filter(skb, sk, snaplen);
	if (!res)
		goto drop_n_restore;

	/* If we are flooded, just give up */
	if (__packet_rcv_has_room(po, skb) == ROOM_NONE) {
		atomic_inc(&po->tp_drops);
		goto drop_n_restore;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		status |= TP_STATUS_CSUMNOTREADY;
	else if (skb->pkt_type != PACKET_OUTGOING &&
		 skb_csum_unnecessary(skb))
		status |= TP_STATUS_CSUM_VALID;
	if (skb_is_gso(skb) && skb_is_gso_tcp(skb))
		status |= TP_STATUS_GSO_TCP;

	if (snaplen > res)
		snaplen = res;

	if (sk->sk_type == SOCK_DGRAM) {
		macoff = netoff = TPACKET_ALIGN(po->tp_hdrlen) + 16 +
				  po->tp_reserve;
	} else {
		unsigned int maclen = skb_network_offset(skb);
		netoff = TPACKET_ALIGN(po->tp_hdrlen +
				       (maclen < 16 ? 16 : maclen)) +
				       po->tp_reserve;
		vnet_hdr_sz = READ_ONCE(po->vnet_hdr_sz);
		if (vnet_hdr_sz)
			netoff += vnet_hdr_sz;
		macoff = netoff - maclen;
	}
	if (netoff > USHRT_MAX) {
		atomic_inc(&po->tp_drops);
		goto drop_n_restore;
	}
	if (po->tp_version <= TPACKET_V2) {
		if (macoff + snaplen > po->rx_ring.frame_size) {
			if (po->copy_thresh &&
			    atomic_read(&sk->sk_rmem_alloc) < sk->sk_rcvbuf) {
				if (skb_shared(skb)) {
					copy_skb = skb_clone(skb, GFP_ATOMIC);
				} else {
					copy_skb = skb_get(skb);
					skb_head = skb->data;
				}
				if (copy_skb) {
					memset(&PACKET_SKB_CB(copy_skb)->sa.ll, 0,
					       sizeof(PACKET_SKB_CB(copy_skb)->sa.ll));
					skb_set_owner_r(copy_skb, sk);
				}
			}
			snaplen = po->rx_ring.frame_size - macoff;
			if ((int)snaplen < 0) {
				snaplen = 0;
				vnet_hdr_sz = 0;
			}
		}
	} else if (unlikely(macoff + snaplen >
			    GET_PBDQC_FROM_RB(&po->rx_ring)->max_frame_len)) {
		u32 nval;

		nval = GET_PBDQC_FROM_RB(&po->rx_ring)->max_frame_len - macoff;
		pr_err_once("tpacket_rcv: packet too big, clamped from %u to %u. macoff=%u\n",
			    snaplen, nval, macoff);
		snaplen = nval;
		if (unlikely((int)snaplen < 0)) {
			snaplen = 0;
			macoff = GET_PBDQC_FROM_RB(&po->rx_ring)->max_frame_len;
			vnet_hdr_sz = 0;
		}
	}
	spin_lock(&sk->sk_receive_queue.lock);
	h.raw = packet_current_rx_frame(po, skb,
					TP_STATUS_KERNEL, (macoff+snaplen));
	if (!h.raw)
		goto drop_n_account;

	if (po->tp_version <= TPACKET_V2) {
		slot_id = po->rx_ring.head;
		if (test_bit(slot_id, po->rx_ring.rx_owner_map))
			goto drop_n_account;
		__set_bit(slot_id, po->rx_ring.rx_owner_map);
	}

	if (vnet_hdr_sz &&
	    virtio_net_hdr_from_skb(skb, h.raw + macoff -
				    sizeof(struct virtio_net_hdr),
				    vio_le(), true, 0)) {
		if (po->tp_version == TPACKET_V3)
			prb_clear_blk_fill_status(&po->rx_ring);
		goto drop_n_account;
	}

	if (po->tp_version <= TPACKET_V2) {
		packet_increment_rx_head(po, &po->rx_ring);
	/*
	 * LOSING will be reported till you read the stats,
	 * because it's COR - Clear On Read.
	 * Anyways, moving it for V1/V2 only as V3 doesn't need this
	 * at packet level.
	 */
		if (atomic_read(&po->tp_drops))
			status |= TP_STATUS_LOSING;
	}

	po->stats.stats1.tp_packets++;
	if (copy_skb) {
		status |= TP_STATUS_COPY;
		skb_clear_delivery_time(copy_skb);
		__skb_queue_tail(&sk->sk_receive_queue, copy_skb);
	}
	spin_unlock(&sk->sk_receive_queue.lock);

	skb_copy_bits(skb, 0, h.raw + macoff, snaplen);

	/* Always timestamp; prefer an existing software timestamp taken
	 * closer to the time of capture.
	 */
	ts_status = tpacket_get_timestamp(skb, &ts,
					  READ_ONCE(po->tp_tstamp) |
					  SOF_TIMESTAMPING_SOFTWARE);
	if (!ts_status)
		ktime_get_real_ts64(&ts);

	status |= ts_status;

	switch (po->tp_version) {
	case TPACKET_V1:
		h.h1->tp_len = skb->len;
		h.h1->tp_snaplen = snaplen;
		h.h1->tp_mac = macoff;
		h.h1->tp_net = netoff;
		h.h1->tp_sec = ts.tv_sec;
		h.h1->tp_usec = ts.tv_nsec / NSEC_PER_USEC;
		hdrlen = sizeof(*h.h1);
		break;
	case TPACKET_V2:
		h.h2->tp_len = skb->len;
		h.h2->tp_snaplen = snaplen;
		h.h2->tp_mac = macoff;
		h.h2->tp_net = netoff;
		h.h2->tp_sec = ts.tv_sec;
		h.h2->tp_nsec = ts.tv_nsec;
		if (skb_vlan_tag_present(skb)) {
			h.h2->tp_vlan_tci = skb_vlan_tag_get(skb);
			h.h2->tp_vlan_tpid = ntohs(skb->vlan_proto);
			status |= TP_STATUS_VLAN_VALID | TP_STATUS_VLAN_TPID_VALID;
		} else {
			h.h2->tp_vlan_tci = 0;
			h.h2->tp_vlan_tpid = 0;
		}
		memset(h.h2->tp_padding, 0, sizeof(h.h2->tp_padding));
		hdrlen = sizeof(*h.h2);
		break;
	case TPACKET_V3:
		/* tp_nxt_offset,vlan are already populated above.
		 * So DONT clear those fields here
		 */
		h.h3->tp_status |= status;
		h.h3->tp_len = skb->len;
		h.h3->tp_snaplen = snaplen;
		h.h3->tp_mac = macoff;
		h.h3->tp_net = netoff;
		h.h3->tp_sec  = ts.tv_sec;
		h.h3->tp_nsec = ts.tv_nsec;
		memset(h.h3->tp_padding, 0, sizeof(h.h3->tp_padding));
		hdrlen = sizeof(*h.h3);
		break;
	default:
		BUG();
	}

	sll = h.raw + TPACKET_ALIGN(hdrlen);
	sll->sll_halen = dev_parse_header(skb, sll->sll_addr);
	sll->sll_family = AF_PACKET;
	sll->sll_hatype = dev->type;
	sll->sll_protocol = skb->protocol;
	sll->sll_pkttype = skb->pkt_type;
	if (unlikely(packet_sock_flag(po, PACKET_SOCK_ORIGDEV)))
		sll->sll_ifindex = orig_dev->ifindex;
	else
		sll->sll_ifindex = dev->ifindex;

	smp_mb();

#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE == 1
	if (po->tp_version <= TPACKET_V2) {
		u8 *start, *end;

		end = (u8 *) PAGE_ALIGN((unsigned long) h.raw +
					macoff + snaplen);

		for (start = h.raw; start < end; start += PAGE_SIZE)
			flush_dcache_page(pgv_to_page(start));
	}
	smp_wmb();
#endif

	if (po->tp_version <= TPACKET_V2) {
		spin_lock(&sk->sk_receive_queue.lock);
		__packet_set_status(po, h.raw, status);
		__clear_bit(slot_id, po->rx_ring.rx_owner_map);
		spin_unlock(&sk->sk_receive_queue.lock);
		sk->sk_data_ready(sk);
	} else if (po->tp_version == TPACKET_V3) {
		prb_clear_blk_fill_status(&po->rx_ring);
	}

drop_n_restore:
	if (skb_head != skb->data && skb_shared(skb)) {
		skb->data = skb_head;
		skb->len = skb_len;
	}
drop:
	if (!is_drop_n_account)
		consume_skb(skb);
	else
		kfree_skb(skb);
	return 0;

drop_n_account:
	spin_unlock(&sk->sk_receive_queue.lock);
	atomic_inc(&po->tp_drops);
	is_drop_n_account = true;

	sk->sk_data_ready(sk);
	kfree_skb(copy_skb);
	goto drop_n_restore;
}

static void tpacket_destruct_skb(struct sk_buff *skb)
{
	struct packet_sock *po = pkt_sk(skb->sk);

	if (likely(po->tx_ring.pg_vec)) {
		void *ph;
		__u32 ts;

		ph = skb_zcopy_get_nouarg(skb);
		packet_dec_pending(&po->tx_ring);

		ts = __packet_set_timestamp(po, ph, skb);
		__packet_set_status(po, ph, TP_STATUS_AVAILABLE | ts);

		if (!packet_read_pending(&po->tx_ring))
			complete(&po->skb_completion);
	}

	sock_wfree(skb);
}

static int __packet_snd_vnet_parse(struct virtio_net_hdr *vnet_hdr, size_t len)
{
	if ((vnet_hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) &&
	    (__virtio16_to_cpu(vio_le(), vnet_hdr->csum_start) +
	     __virtio16_to_cpu(vio_le(), vnet_hdr->csum_offset) + 2 >
	      __virtio16_to_cpu(vio_le(), vnet_hdr->hdr_len)))
		vnet_hdr->hdr_len = __cpu_to_virtio16(vio_le(),
			 __virtio16_to_cpu(vio_le(), vnet_hdr->csum_start) +
			__virtio16_to_cpu(vio_le(), vnet_hdr->csum_offset) + 2);

	if (__virtio16_to_cpu(vio_le(), vnet_hdr->hdr_len) > len)
		return -EINVAL;

	return 0;
}

static int packet_snd_vnet_parse(struct msghdr *msg, size_t *len,
				 struct virtio_net_hdr *vnet_hdr, int vnet_hdr_sz)
{
	int ret;

	if (*len < vnet_hdr_sz)
		return -EINVAL;
	*len -= vnet_hdr_sz;

	if (!copy_from_iter_full(vnet_hdr, sizeof(*vnet_hdr), &msg->msg_iter))
		return -EFAULT;

	ret = __packet_snd_vnet_parse(vnet_hdr, *len);
	if (ret)
		return ret;

	/* move iter to point to the start of mac header */
	if (vnet_hdr_sz != sizeof(struct virtio_net_hdr))
		iov_iter_advance(&msg->msg_iter, vnet_hdr_sz - sizeof(struct virtio_net_hdr));

	return 0;
}

static int tpacket_fill_skb(struct packet_sock *po, struct sk_buff *skb,
		void *frame, struct net_device *dev, void *data, int tp_len,
		__be16 proto, unsigned char *addr, int hlen, int copylen,
		const struct sockcm_cookie *sockc)
{
	union tpacket_uhdr ph;
	int to_write, offset, len, nr_frags, len_max;
	struct socket *sock = po->sk.sk_socket;
	struct page *page;
	int err;

	ph.raw = frame;

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = READ_ONCE(po->sk.sk_priority);
	skb->mark = READ_ONCE(po->sk.sk_mark);
	skb->tstamp = sockc->transmit_time;
	skb_setup_tx_timestamp(skb, sockc->tsflags);
	skb_zcopy_set_nouarg(skb, ph.raw);

	skb_reserve(skb, hlen);
	skb_reset_network_header(skb);

	to_write = tp_len;

	if (sock->type == SOCK_DGRAM) {
		err = dev_hard_header(skb, dev, ntohs(proto), addr,
				NULL, tp_len);
		if (unlikely(err < 0))
			return -EINVAL;
	} else if (copylen) {
		int hdrlen = min_t(int, copylen, tp_len);

		skb_push(skb, dev->hard_header_len);
		skb_put(skb, copylen - dev->hard_header_len);
		err = skb_store_bits(skb, 0, data, hdrlen);
		if (unlikely(err))
			return err;
		if (!dev_validate_header(dev, skb->data, hdrlen))
			return -EINVAL;

		data += hdrlen;
		to_write -= hdrlen;
	}

	offset = offset_in_page(data);
	len_max = PAGE_SIZE - offset;
	len = ((to_write > len_max) ? len_max : to_write);

	skb->data_len = to_write;
	skb->len += to_write;
	skb->truesize += to_write;
	refcount_add(to_write, &po->sk.sk_wmem_alloc);

	while (likely(to_write)) {
		nr_frags = skb_shinfo(skb)->nr_frags;

		if (unlikely(nr_frags >= MAX_SKB_FRAGS)) {
			pr_err("Packet exceed the number of skb frags(%u)\n",
			       (unsigned int)MAX_SKB_FRAGS);
			return -EFAULT;
		}

		page = pgv_to_page(data);
		data += len;
		flush_dcache_page(page);
		get_page(page);
		skb_fill_page_desc(skb, nr_frags, page, offset, len);
		to_write -= len;
		offset = 0;
		len_max = PAGE_SIZE;
		len = ((to_write > len_max) ? len_max : to_write);
	}

	packet_parse_headers(skb, sock);

	return tp_len;
}

static int tpacket_parse_header(struct packet_sock *po, void *frame,
				int size_max, void **data)
{
	union tpacket_uhdr ph;
	int tp_len, off;

	ph.raw = frame;

	switch (po->tp_version) {
	case TPACKET_V3:
		if (ph.h3->tp_next_offset != 0) {
			pr_warn_once("variable sized slot not supported");
			return -EINVAL;
		}
		tp_len = ph.h3->tp_len;
		break;
	case TPACKET_V2:
		tp_len = ph.h2->tp_len;
		break;
	default:
		tp_len = ph.h1->tp_len;
		break;
	}
	if (unlikely(tp_len > size_max)) {
		pr_err("packet size is too long (%d > %d)\n", tp_len, size_max);
		return -EMSGSIZE;
	}

	if (unlikely(packet_sock_flag(po, PACKET_SOCK_TX_HAS_OFF))) {
		int off_min, off_max;

		off_min = po->tp_hdrlen - sizeof(struct sockaddr_ll);
		off_max = po->tx_ring.frame_size - tp_len;
		if (po->sk.sk_type == SOCK_DGRAM) {
			switch (po->tp_version) {
			case TPACKET_V3:
				off = ph.h3->tp_net;
				break;
			case TPACKET_V2:
				off = ph.h2->tp_net;
				break;
			default:
				off = ph.h1->tp_net;
				break;
			}
		} else {
			switch (po->tp_version) {
			case TPACKET_V3:
				off = ph.h3->tp_mac;
				break;
			case TPACKET_V2:
				off = ph.h2->tp_mac;
				break;
			default:
				off = ph.h1->tp_mac;
				break;
			}
		}
		if (unlikely((off < off_min) || (off_max < off)))
			return -EINVAL;
	} else {
		off = po->tp_hdrlen - sizeof(struct sockaddr_ll);
	}

	*data = frame + off;
	return tp_len;
}

static int tpacket_snd(struct packet_sock *po, struct msghdr *msg)
{
	struct sk_buff *skb = NULL;
	struct net_device *dev;
	struct virtio_net_hdr *vnet_hdr = NULL;
	struct sockcm_cookie sockc;
	__be16 proto;
	int err, reserve = 0;
	void *ph;
	DECLARE_SOCKADDR(struct sockaddr_ll *, saddr, msg->msg_name);
	bool need_wait = !(msg->msg_flags & MSG_DONTWAIT);
	int vnet_hdr_sz = READ_ONCE(po->vnet_hdr_sz);
	unsigned char *addr = NULL;
	int tp_len, size_max;
	void *data;
	int len_sum = 0;
	int status = TP_STATUS_AVAILABLE;
	int hlen, tlen, copylen = 0;
	long timeo = 0;

	mutex_lock(&po->pg_vec_lock);

	/* packet_sendmsg() check on tx_ring.pg_vec was lockless,
	 * we need to confirm it under protection of pg_vec_lock.
	 */
	if (unlikely(!po->tx_ring.pg_vec)) {
		err = -EBUSY;
		goto out;
	}
	if (likely(saddr == NULL)) {
		dev	= packet_cached_dev_get(po);
		proto	= READ_ONCE(po->num);
	} else {
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(struct sockaddr_ll))
			goto out;
		if (msg->msg_namelen < (saddr->sll_halen
					+ offsetof(struct sockaddr_ll,
						sll_addr)))
			goto out;
		proto	= saddr->sll_protocol;
		dev = dev_get_by_index(sock_net(&po->sk), saddr->sll_ifindex);
		if (po->sk.sk_socket->type == SOCK_DGRAM) {
			if (dev && msg->msg_namelen < dev->addr_len +
				   offsetof(struct sockaddr_ll, sll_addr))
				goto out_put;
			addr = saddr->sll_addr;
		}
	}

	err = -ENXIO;
	if (unlikely(dev == NULL))
		goto out;
	err = -ENETDOWN;
	if (unlikely(!(dev->flags & IFF_UP)))
		goto out_put;

	sockcm_init(&sockc, &po->sk);
	if (msg->msg_controllen) {
		err = sock_cmsg_send(&po->sk, msg, &sockc);
		if (unlikely(err))
			goto out_put;
	}

	if (po->sk.sk_socket->type == SOCK_RAW)
		reserve = dev->hard_header_len;
	size_max = po->tx_ring.frame_size
		- (po->tp_hdrlen - sizeof(struct sockaddr_ll));

	if ((size_max > dev->mtu + reserve + VLAN_HLEN) && !vnet_hdr_sz)
		size_max = dev->mtu + reserve + VLAN_HLEN;

	reinit_completion(&po->skb_completion);

	do {
		ph = packet_current_frame(po, &po->tx_ring,
					  TP_STATUS_SEND_REQUEST);
		if (unlikely(ph == NULL)) {
			if (need_wait && skb) {
				timeo = sock_sndtimeo(&po->sk, msg->msg_flags & MSG_DONTWAIT);
				timeo = wait_for_completion_interruptible_timeout(&po->skb_completion, timeo);
				if (timeo <= 0) {
					err = !timeo ? -ETIMEDOUT : -ERESTARTSYS;
					goto out_put;
				}
			}
			/* check for additional frames */
			continue;
		}

		skb = NULL;
		tp_len = tpacket_parse_header(po, ph, size_max, &data);
		if (tp_len < 0)
			goto tpacket_error;

		status = TP_STATUS_SEND_REQUEST;
		hlen = LL_RESERVED_SPACE(dev);
		tlen = dev->needed_tailroom;
		if (vnet_hdr_sz) {
			vnet_hdr = data;
			data += vnet_hdr_sz;
			tp_len -= vnet_hdr_sz;
			if (tp_len < 0 ||
			    __packet_snd_vnet_parse(vnet_hdr, tp_len)) {
				tp_len = -EINVAL;
				goto tpacket_error;
			}
			copylen = __virtio16_to_cpu(vio_le(),
						    vnet_hdr->hdr_len);
		}
		copylen = max_t(int, copylen, dev->hard_header_len);
		skb = sock_alloc_send_skb(&po->sk,
				hlen + tlen + sizeof(struct sockaddr_ll) +
				(copylen - dev->hard_header_len),
				!need_wait, &err);

		if (unlikely(skb == NULL)) {
			/* we assume the socket was initially writeable ... */
			if (likely(len_sum > 0))
				err = len_sum;
			goto out_status;
		}
		tp_len = tpacket_fill_skb(po, skb, ph, dev, data, tp_len, proto,
					  addr, hlen, copylen, &sockc);
		if (likely(tp_len >= 0) &&
		    tp_len > dev->mtu + reserve &&
		    !vnet_hdr_sz &&
		    !packet_extra_vlan_len_allowed(dev, skb))
			tp_len = -EMSGSIZE;

		if (unlikely(tp_len < 0)) {
tpacket_error:
			if (packet_sock_flag(po, PACKET_SOCK_TP_LOSS)) {
				__packet_set_status(po, ph,
						TP_STATUS_AVAILABLE);
				packet_increment_head(&po->tx_ring);
				kfree_skb(skb);
				continue;
			} else {
				status = TP_STATUS_WRONG_FORMAT;
				err = tp_len;
				goto out_status;
			}
		}

		if (vnet_hdr_sz) {
			if (virtio_net_hdr_to_skb(skb, vnet_hdr, vio_le())) {
				tp_len = -EINVAL;
				goto tpacket_error;
			}
			virtio_net_hdr_set_proto(skb, vnet_hdr);
		}

		skb->destructor = tpacket_destruct_skb;
		__packet_set_status(po, ph, TP_STATUS_SENDING);
		packet_inc_pending(&po->tx_ring);

		status = TP_STATUS_SEND_REQUEST;
		err = packet_xmit(po, skb);
		if (unlikely(err != 0)) {
			if (err > 0)
				err = net_xmit_errno(err);
			if (err && __packet_get_status(po, ph) ==
				   TP_STATUS_AVAILABLE) {
				/* skb was destructed already */
				skb = NULL;
				goto out_status;
			}
			/*
			 * skb was dropped but not destructed yet;
			 * let's treat it like congestion or err < 0
			 */
			err = 0;
		}
		packet_increment_head(&po->tx_ring);
		len_sum += tp_len;
	} while (likely((ph != NULL) ||
		/* Note: packet_read_pending() might be slow if we have
		 * to call it as it's per_cpu variable, but in fast-path
		 * we already short-circuit the loop with the first
		 * condition, and luckily don't have to go that path
		 * anyway.
		 */
		 (need_wait && packet_read_pending(&po->tx_ring))));

	err = len_sum;
	goto out_put;

out_status:
	__packet_set_status(po, ph, status);
	kfree_skb(skb);
out_put:
	dev_put(dev);
out:
	mutex_unlock(&po->pg_vec_lock);
	return err;
}

static struct sk_buff *packet_alloc_skb(struct sock *sk, size_t prepad,
				        size_t reserve, size_t len,
				        size_t linear, int noblock,
				        int *err)
{
	struct sk_buff *skb;

	/* Under a page?  Don't bother with paged skb. */
	if (prepad + len < PAGE_SIZE || !linear)
		linear = len;

	if (len - linear > MAX_SKB_FRAGS * (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER))
		linear = len - MAX_SKB_FRAGS * (PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER);
	skb = sock_alloc_send_pskb(sk, prepad + linear, len - linear, noblock,
				   err, PAGE_ALLOC_COSTLY_ORDER);
	if (!skb)
		return NULL;

	skb_reserve(skb, reserve);
	skb_put(skb, linear);
	skb->data_len = len - linear;
	skb->len += len - linear;

	return skb;
}

static int packet_snd(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	DECLARE_SOCKADDR(struct sockaddr_ll *, saddr, msg->msg_name);
	struct sk_buff *skb;
	struct net_device *dev;
	__be16 proto;
	unsigned char *addr = NULL;
	int err, reserve = 0;
	struct sockcm_cookie sockc;
	struct virtio_net_hdr vnet_hdr = { 0 };
	int offset = 0;
	struct packet_sock *po = pkt_sk(sk);
	int vnet_hdr_sz = READ_ONCE(po->vnet_hdr_sz);
	int hlen, tlen, linear;
	int extra_len = 0;

	/*
	 *	Get and verify the address.
	 */

	if (likely(saddr == NULL)) {
		dev	= packet_cached_dev_get(po);
		proto	= READ_ONCE(po->num);
	} else {
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(struct sockaddr_ll))
			goto out;
		if (msg->msg_namelen < (saddr->sll_halen + offsetof(struct sockaddr_ll, sll_addr)))
			goto out;
		proto	= saddr->sll_protocol;
		dev = dev_get_by_index(sock_net(sk), saddr->sll_ifindex);
		if (sock->type == SOCK_DGRAM) {
			if (dev && msg->msg_namelen < dev->addr_len +
				   offsetof(struct sockaddr_ll, sll_addr))
				goto out_unlock;
			addr = saddr->sll_addr;
		}
	}

	err = -ENXIO;
	if (unlikely(dev == NULL))
		goto out_unlock;
	err = -ENETDOWN;
	if (unlikely(!(dev->flags & IFF_UP)))
		goto out_unlock;

	sockcm_init(&sockc, sk);
	sockc.mark = READ_ONCE(sk->sk_mark);
	if (msg->msg_controllen) {
		err = sock_cmsg_send(sk, msg, &sockc);
		if (unlikely(err))
			goto out_unlock;
	}

	if (sock->type == SOCK_RAW)
		reserve = dev->hard_header_len;
	if (vnet_hdr_sz) {
		err = packet_snd_vnet_parse(msg, &len, &vnet_hdr, vnet_hdr_sz);
		if (err)
			goto out_unlock;
	}

	if (unlikely(sock_flag(sk, SOCK_NOFCS))) {
		if (!netif_supports_nofcs(dev)) {
			err = -EPROTONOSUPPORT;
			goto out_unlock;
		}
		extra_len = 4; /* We're doing our own CRC */
	}

	err = -EMSGSIZE;
	if (!vnet_hdr.gso_type &&
	    (len > dev->mtu + reserve + VLAN_HLEN + extra_len))
		goto out_unlock;

	err = -ENOBUFS;
	hlen = LL_RESERVED_SPACE(dev);
	tlen = dev->needed_tailroom;
	linear = __virtio16_to_cpu(vio_le(), vnet_hdr.hdr_len);
	linear = max(linear, min_t(int, len, dev->hard_header_len));
	skb = packet_alloc_skb(sk, hlen + tlen, hlen, len, linear,
			       msg->msg_flags & MSG_DONTWAIT, &err);
	if (skb == NULL)
		goto out_unlock;

	skb_reset_network_header(skb);

	err = -EINVAL;
	if (sock->type == SOCK_DGRAM) {
		offset = dev_hard_header(skb, dev, ntohs(proto), addr, NULL, len);
		if (unlikely(offset < 0))
			goto out_free;
	} else if (reserve) {
		skb_reserve(skb, -reserve);
		if (len < reserve + sizeof(struct ipv6hdr) &&
		    dev->min_header_len != dev->hard_header_len)
			skb_reset_network_header(skb);
	}

	/* Returns -EFAULT on error */
	err = skb_copy_datagram_from_iter(skb, offset, &msg->msg_iter, len);
	if (err)
		goto out_free;

	if ((sock->type == SOCK_RAW &&
	     !dev_validate_header(dev, skb->data, len)) || !skb->len) {
		err = -EINVAL;
		goto out_free;
	}

	skb_setup_tx_timestamp(skb, sockc.tsflags);

	if (!vnet_hdr.gso_type && (len > dev->mtu + reserve + extra_len) &&
	    !packet_extra_vlan_len_allowed(dev, skb)) {
		err = -EMSGSIZE;
		goto out_free;
	}

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = READ_ONCE(sk->sk_priority);
	skb->mark = sockc.mark;
	skb->tstamp = sockc.transmit_time;

	if (unlikely(extra_len == 4))
		skb->no_fcs = 1;

	packet_parse_headers(skb, sock);

	if (vnet_hdr_sz) {
		err = virtio_net_hdr_to_skb(skb, &vnet_hdr, vio_le());
		if (err)
			goto out_free;
		len += vnet_hdr_sz;
		virtio_net_hdr_set_proto(skb, &vnet_hdr);
	}

	err = packet_xmit(po, skb);

	if (unlikely(err != 0)) {
		if (err > 0)
			err = net_xmit_errno(err);
		if (err)
			goto out_unlock;
	}

	dev_put(dev);

	return len;

out_free:
	kfree_skb(skb);
out_unlock:
	dev_put(dev);
out:
	return err;
}

static int packet_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);

	/* Reading tx_ring.pg_vec without holding pg_vec_lock is racy.
	 * tpacket_snd() will redo the check safely.
	 */
	if (data_race(po->tx_ring.pg_vec))
		return tpacket_snd(po, msg);

	return packet_snd(sock, msg, len);
}

/*
 *	Close a PACKET socket. This is fairly simple. We immediately go
 *	to 'closed' state and remove our protocol entry in the device list.
 */

static int packet_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po;
	struct packet_fanout *f;
	struct net *net;
	union tpacket_req_u req_u;

	if (!sk)
		return 0;

	net = sock_net(sk);
	po = pkt_sk(sk);

	mutex_lock(&net->packet.sklist_lock);
	sk_del_node_init_rcu(sk);
	mutex_unlock(&net->packet.sklist_lock);

	sock_prot_inuse_add(net, sk->sk_prot, -1);

	spin_lock(&po->bind_lock);
	unregister_prot_hook(sk, false);
	packet_cached_dev_reset(po);

	if (po->prot_hook.dev) {
		netdev_put(po->prot_hook.dev, &po->prot_hook.dev_tracker);
		po->prot_hook.dev = NULL;
	}
	spin_unlock(&po->bind_lock);

	packet_flush_mclist(sk);

	lock_sock(sk);
	if (po->rx_ring.pg_vec) {
		memset(&req_u, 0, sizeof(req_u));
		packet_set_ring(sk, &req_u, 1, 0);
	}

	if (po->tx_ring.pg_vec) {
		memset(&req_u, 0, sizeof(req_u));
		packet_set_ring(sk, &req_u, 1, 1);
	}
	release_sock(sk);

	f = fanout_release(sk);

	synchronize_net();

	kfree(po->rollover);
	if (f) {
		fanout_release_data(f);
		kvfree(f);
	}
	/*
	 *	Now the socket is dead. No more input will appear.
	 */
	sock_orphan(sk);
	sock->sk = NULL;

	/* Purge queues */

	skb_queue_purge(&sk->sk_receive_queue);
	packet_free_pending(po);

	sock_put(sk);
	return 0;
}

/*
 *	Attach a packet hook.
 */

static int packet_do_bind(struct sock *sk, const char *name, int ifindex,
			  __be16 proto)
{
	struct packet_sock *po = pkt_sk(sk);
	struct net_device *dev = NULL;
	bool unlisted = false;
	bool need_rehook;
	int ret = 0;

	lock_sock(sk);
	spin_lock(&po->bind_lock);
	if (!proto)
		proto = po->num;

	rcu_read_lock();

	if (po->fanout) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (name) {
		dev = dev_get_by_name_rcu(sock_net(sk), name);
		if (!dev) {
			ret = -ENODEV;
			goto out_unlock;
		}
	} else if (ifindex) {
		dev = dev_get_by_index_rcu(sock_net(sk), ifindex);
		if (!dev) {
			ret = -ENODEV;
			goto out_unlock;
		}
	}

	need_rehook = po->prot_hook.type != proto || po->prot_hook.dev != dev;

	if (need_rehook) {
		dev_hold(dev);
		if (packet_sock_flag(po, PACKET_SOCK_RUNNING)) {
			rcu_read_unlock();
			/* prevents packet_notifier() from calling
			 * register_prot_hook()
			 */
			WRITE_ONCE(po->num, 0);
			__unregister_prot_hook(sk, true);
			rcu_read_lock();
			if (dev)
				unlisted = !dev_get_by_index_rcu(sock_net(sk),
								 dev->ifindex);
		}

		BUG_ON(packet_sock_flag(po, PACKET_SOCK_RUNNING));
		WRITE_ONCE(po->num, proto);
		po->prot_hook.type = proto;

		netdev_put(po->prot_hook.dev, &po->prot_hook.dev_tracker);

		if (unlikely(unlisted)) {
			po->prot_hook.dev = NULL;
			WRITE_ONCE(po->ifindex, -1);
			packet_cached_dev_reset(po);
		} else {
			netdev_hold(dev, &po->prot_hook.dev_tracker,
				    GFP_ATOMIC);
			po->prot_hook.dev = dev;
			WRITE_ONCE(po->ifindex, dev ? dev->ifindex : 0);
			packet_cached_dev_assign(po, dev);
		}
		dev_put(dev);
	}

	if (proto == 0 || !need_rehook)
		goto out_unlock;

	if (!unlisted && (!dev || (dev->flags & IFF_UP))) {
		register_prot_hook(sk);
	} else {
		sk->sk_err = ENETDOWN;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);
	}

out_unlock:
	rcu_read_unlock();
	spin_unlock(&po->bind_lock);
	release_sock(sk);
	return ret;
}

/*
 *	Bind a packet socket to a device
 */

static int packet_bind_spkt(struct socket *sock, struct sockaddr *uaddr,
			    int addr_len)
{
	struct sock *sk = sock->sk;
	char name[sizeof(uaddr->sa_data_min) + 1];

	/*
	 *	Check legality
	 */

	if (addr_len != sizeof(struct sockaddr))
		return -EINVAL;
	/* uaddr->sa_data comes from the userspace, it's not guaranteed to be
	 * zero-terminated.
	 */
	memcpy(name, uaddr->sa_data, sizeof(uaddr->sa_data_min));
	name[sizeof(uaddr->sa_data_min)] = 0;

	return packet_do_bind(sk, name, 0, 0);
}

static int packet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_ll *sll = (struct sockaddr_ll *)uaddr;
	struct sock *sk = sock->sk;

	/*
	 *	Check legality
	 */

	if (addr_len < sizeof(struct sockaddr_ll))
		return -EINVAL;
	if (sll->sll_family != AF_PACKET)
		return -EINVAL;

	return packet_do_bind(sk, NULL, sll->sll_ifindex, sll->sll_protocol);
}

static struct proto packet_proto = {
	.name	  = "PACKET",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct packet_sock),
};

/*
 *	Create a packet of type SOCK_PACKET.
 */

static int packet_create(struct net *net, struct socket *sock, int protocol,
			 int kern)
{
	struct sock *sk;
	struct packet_sock *po;
	__be16 proto = (__force __be16)protocol; /* weird, but documented */
	int err;

	if (!ns_capable(net->user_ns, CAP_NET_RAW))
		return -EPERM;
	if (sock->type != SOCK_DGRAM && sock->type != SOCK_RAW &&
	    sock->type != SOCK_PACKET)
		return -ESOCKTNOSUPPORT;

	sock->state = SS_UNCONNECTED;

	err = -ENOBUFS;
	sk = sk_alloc(net, PF_PACKET, GFP_KERNEL, &packet_proto, kern);
	if (sk == NULL)
		goto out;

	sock->ops = &packet_ops;
	if (sock->type == SOCK_PACKET)
		sock->ops = &packet_ops_spkt;

	sock_init_data(sock, sk);

	po = pkt_sk(sk);
	init_completion(&po->skb_completion);
	sk->sk_family = PF_PACKET;
	po->num = proto;

	err = packet_alloc_pending(po);
	if (err)
		goto out2;

	packet_cached_dev_reset(po);

	sk->sk_destruct = packet_sock_destruct;

	/*
	 *	Attach a protocol block
	 */

	spin_lock_init(&po->bind_lock);
	mutex_init(&po->pg_vec_lock);
	po->rollover = NULL;
	po->prot_hook.func = packet_rcv;

	if (sock->type == SOCK_PACKET)
		po->prot_hook.func = packet_rcv_spkt;

	po->prot_hook.af_packet_priv = sk;
	po->prot_hook.af_packet_net = sock_net(sk);

	if (proto) {
		po->prot_hook.type = proto;
		__register_prot_hook(sk);
	}

	mutex_lock(&net->packet.sklist_lock);
	sk_add_node_tail_rcu(sk, &net->packet.sklist);
	mutex_unlock(&net->packet.sklist_lock);

	sock_prot_inuse_add(net, &packet_proto, 1);

	return 0;
out2:
	sk_free(sk);
out:
	return err;
}

/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */

static int packet_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			  int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;
	int vnet_hdr_len = READ_ONCE(pkt_sk(sk)->vnet_hdr_sz);
	unsigned int origlen = 0;

	err = -EINVAL;
	if (flags & ~(MSG_PEEK|MSG_DONTWAIT|MSG_TRUNC|MSG_CMSG_COMPAT|MSG_ERRQUEUE))
		goto out;

#if 0
	/* What error should we return now? EUNATTACH? */
	if (pkt_sk(sk)->ifindex < 0)
		return -ENODEV;
#endif

	if (flags & MSG_ERRQUEUE) {
		err = sock_recv_errqueue(sk, msg, len,
					 SOL_PACKET, PACKET_TX_TIMESTAMP);
		goto out;
	}

	/*
	 *	Call the generic datagram receiver. This handles all sorts
	 *	of horrible races and re-entrancy so we can forget about it
	 *	in the protocol layers.
	 *
	 *	Now it will return ENETDOWN, if device have just gone down,
	 *	but then it will block.
	 */

	skb = skb_recv_datagram(sk, flags, &err);

	/*
	 *	An error occurred so return it. Because skb_recv_datagram()
	 *	handles the blocking we don't see and worry about blocking
	 *	retries.
	 */

	if (skb == NULL)
		goto out;

	packet_rcv_try_clear_pressure(pkt_sk(sk));

	if (vnet_hdr_len) {
		err = packet_rcv_vnet(msg, skb, &len, vnet_hdr_len);
		if (err)
			goto out_free;
	}

	/* You lose any data beyond the buffer you gave. If it worries
	 * a user program they can ask the device for its MTU
	 * anyway.
	 */
	copied = skb->len;
	if (copied > len) {
		copied = len;
		msg->msg_flags |= MSG_TRUNC;
	}

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto out_free;

	if (sock->type != SOCK_PACKET) {
		struct sockaddr_ll *sll = &PACKET_SKB_CB(skb)->sa.ll;

		/* Original length was stored in sockaddr_ll fields */
		origlen = PACKET_SKB_CB(skb)->sa.origlen;
		sll->sll_family = AF_PACKET;
		sll->sll_protocol = skb->protocol;
	}

	sock_recv_cmsgs(msg, sk, skb);

	if (msg->msg_name) {
		const size_t max_len = min(sizeof(skb->cb),
					   sizeof(struct sockaddr_storage));
		int copy_len;

		/* If the address length field is there to be filled
		 * in, we fill it in now.
		 */
		if (sock->type == SOCK_PACKET) {
			__sockaddr_check_size(sizeof(struct sockaddr_pkt));
			msg->msg_namelen = sizeof(struct sockaddr_pkt);
			copy_len = msg->msg_namelen;
		} else {
			struct sockaddr_ll *sll = &PACKET_SKB_CB(skb)->sa.ll;

			msg->msg_namelen = sll->sll_halen +
				offsetof(struct sockaddr_ll, sll_addr);
			copy_len = msg->msg_namelen;
			if (msg->msg_namelen < sizeof(struct sockaddr_ll)) {
				memset(msg->msg_name +
				       offsetof(struct sockaddr_ll, sll_addr),
				       0, sizeof(sll->sll_addr));
				msg->msg_namelen = sizeof(struct sockaddr_ll);
			}
		}
		if (WARN_ON_ONCE(copy_len > max_len)) {
			copy_len = max_len;
			msg->msg_namelen = copy_len;
		}
		memcpy(msg->msg_name, &PACKET_SKB_CB(skb)->sa, copy_len);
	}

	if (packet_sock_flag(pkt_sk(sk), PACKET_SOCK_AUXDATA)) {
		struct tpacket_auxdata aux;

		aux.tp_status = TP_STATUS_USER;
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			aux.tp_status |= TP_STATUS_CSUMNOTREADY;
		else if (skb->pkt_type != PACKET_OUTGOING &&
			 skb_csum_unnecessary(skb))
			aux.tp_status |= TP_STATUS_CSUM_VALID;
		if (skb_is_gso(skb) && skb_is_gso_tcp(skb))
			aux.tp_status |= TP_STATUS_GSO_TCP;

		aux.tp_len = origlen;
		aux.tp_snaplen = skb->len;
		aux.tp_mac = 0;
		aux.tp_net = skb_network_offset(skb);
		if (skb_vlan_tag_present(skb)) {
			aux.tp_vlan_tci = skb_vlan_tag_get(skb);
			aux.tp_vlan_tpid = ntohs(skb->vlan_proto);
			aux.tp_status |= TP_STATUS_VLAN_VALID | TP_STATUS_VLAN_TPID_VALID;
		} else {
			aux.tp_vlan_tci = 0;
			aux.tp_vlan_tpid = 0;
		}
		put_cmsg(msg, SOL_PACKET, PACKET_AUXDATA, sizeof(aux), &aux);
	}

	/*
	 *	Free or return the buffer as appropriate. Again this
	 *	hides all the races and re-entrancy issues from us.
	 */
	err = vnet_hdr_len + ((flags&MSG_TRUNC) ? skb->len : copied);

out_free:
	skb_free_datagram(sk, skb);
out:
	return err;
}

static int packet_getname_spkt(struct socket *sock, struct sockaddr *uaddr,
			       int peer)
{
	struct net_device *dev;
	struct sock *sk	= sock->sk;

	if (peer)
		return -EOPNOTSUPP;

	uaddr->sa_family = AF_PACKET;
	memset(uaddr->sa_data, 0, sizeof(uaddr->sa_data_min));
	rcu_read_lock();
	dev = dev_get_by_index_rcu(sock_net(sk), READ_ONCE(pkt_sk(sk)->ifindex));
	if (dev)
		strscpy(uaddr->sa_data, dev->name, sizeof(uaddr->sa_data_min));
	rcu_read_unlock();

	return sizeof(*uaddr);
}

static int packet_getname(struct socket *sock, struct sockaddr *uaddr,
			  int peer)
{
	struct net_device *dev;
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_ll *, sll, uaddr);
	int ifindex;

	if (peer)
		return -EOPNOTSUPP;

	ifindex = READ_ONCE(po->ifindex);
	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = ifindex;
	sll->sll_protocol = READ_ONCE(po->num);
	sll->sll_pkttype = 0;
	rcu_read_lock();
	dev = dev_get_by_index_rcu(sock_net(sk), ifindex);
	if (dev) {
		sll->sll_hatype = dev->type;
		sll->sll_halen = dev->addr_len;
		memcpy(sll->sll_addr_flex, dev->dev_addr, dev->addr_len);
	} else {
		sll->sll_hatype = 0;	/* Bad: we have no ARPHRD_UNSPEC */
		sll->sll_halen = 0;
	}
	rcu_read_unlock();

	return offsetof(struct sockaddr_ll, sll_addr) + sll->sll_halen;
}

static int packet_dev_mc(struct net_device *dev, struct packet_mclist *i,
			 int what)
{
	switch (i->type) {
	case PACKET_MR_MULTICAST:
		if (i->alen != dev->addr_len)
			return -EINVAL;
		if (what > 0)
			return dev_mc_add(dev, i->addr);
		else
			return dev_mc_del(dev, i->addr);
		break;
	case PACKET_MR_PROMISC:
		return dev_set_promiscuity(dev, what);
	case PACKET_MR_ALLMULTI:
		return dev_set_allmulti(dev, what);
	case PACKET_MR_UNICAST:
		if (i->alen != dev->addr_len)
			return -EINVAL;
		if (what > 0)
			return dev_uc_add(dev, i->addr);
		else
			return dev_uc_del(dev, i->addr);
		break;
	default:
		break;
	}
	return 0;
}

static void packet_dev_mclist_delete(struct net_device *dev,
				     struct packet_mclist **mlp)
{
	struct packet_mclist *ml;

	while ((ml = *mlp) != NULL) {
		if (ml->ifindex == dev->ifindex) {
			packet_dev_mc(dev, ml, -1);
			*mlp = ml->next;
			kfree(ml);
		} else
			mlp = &ml->next;
	}
}

static int packet_mc_add(struct sock *sk, struct packet_mreq_max *mreq)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_mclist *ml, *i;
	struct net_device *dev;
	int err;

	rtnl_lock();

	err = -ENODEV;
	dev = __dev_get_by_index(sock_net(sk), mreq->mr_ifindex);
	if (!dev)
		goto done;

	err = -EINVAL;
	if (mreq->mr_alen > dev->addr_len)
		goto done;

	err = -ENOBUFS;
	i = kmalloc(sizeof(*i), GFP_KERNEL);
	if (i == NULL)
		goto done;

	err = 0;
	for (ml = po->mclist; ml; ml = ml->next) {
		if (ml->ifindex == mreq->mr_ifindex &&
		    ml->type == mreq->mr_type &&
		    ml->alen == mreq->mr_alen &&
		    memcmp(ml->addr, mreq->mr_address, ml->alen) == 0) {
			ml->count++;
			/* Free the new element ... */
			kfree(i);
			goto done;
		}
	}

	i->type = mreq->mr_type;
	i->ifindex = mreq->mr_ifindex;
	i->alen = mreq->mr_alen;
	memcpy(i->addr, mreq->mr_address, i->alen);
	memset(i->addr + i->alen, 0, sizeof(i->addr) - i->alen);
	i->count = 1;
	i->next = po->mclist;
	po->mclist = i;
	err = packet_dev_mc(dev, i, 1);
	if (err) {
		po->mclist = i->next;
		kfree(i);
	}

done:
	rtnl_unlock();
	return err;
}

static int packet_mc_drop(struct sock *sk, struct packet_mreq_max *mreq)
{
	struct packet_mclist *ml, **mlp;

	rtnl_lock();

	for (mlp = &pkt_sk(sk)->mclist; (ml = *mlp) != NULL; mlp = &ml->next) {
		if (ml->ifindex == mreq->mr_ifindex &&
		    ml->type == mreq->mr_type &&
		    ml->alen == mreq->mr_alen &&
		    memcmp(ml->addr, mreq->mr_address, ml->alen) == 0) {
			if (--ml->count == 0) {
				struct net_device *dev;
				*mlp = ml->next;
				dev = __dev_get_by_index(sock_net(sk), ml->ifindex);
				if (dev)
					packet_dev_mc(dev, ml, -1);
				kfree(ml);
			}
			break;
		}
	}
	rtnl_unlock();
	return 0;
}

static void packet_flush_mclist(struct sock *sk)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_mclist *ml;

	if (!po->mclist)
		return;

	rtnl_lock();
	while ((ml = po->mclist) != NULL) {
		struct net_device *dev;

		po->mclist = ml->next;
		dev = __dev_get_by_index(sock_net(sk), ml->ifindex);
		if (dev != NULL)
			packet_dev_mc(dev, ml, -1);
		kfree(ml);
	}
	rtnl_unlock();
}

static int
packet_setsockopt(struct socket *sock, int level, int optname, sockptr_t optval,
		  unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	int ret;

	if (level != SOL_PACKET)
		return -ENOPROTOOPT;

	switch (optname) {
	case PACKET_ADD_MEMBERSHIP:
	case PACKET_DROP_MEMBERSHIP:
	{
		struct packet_mreq_max mreq;
		int len = optlen;
		memset(&mreq, 0, sizeof(mreq));
		if (len < sizeof(struct packet_mreq))
			return -EINVAL;
		if (len > sizeof(mreq))
			len = sizeof(mreq);
		if (copy_from_sockptr(&mreq, optval, len))
			return -EFAULT;
		if (len < (mreq.mr_alen + offsetof(struct packet_mreq, mr_address)))
			return -EINVAL;
		if (optname == PACKET_ADD_MEMBERSHIP)
			ret = packet_mc_add(sk, &mreq);
		else
			ret = packet_mc_drop(sk, &mreq);
		return ret;
	}

	case PACKET_RX_RING:
	case PACKET_TX_RING:
	{
		union tpacket_req_u req_u;
		int len;

		lock_sock(sk);
		switch (po->tp_version) {
		case TPACKET_V1:
		case TPACKET_V2:
			len = sizeof(req_u.req);
			break;
		case TPACKET_V3:
		default:
			len = sizeof(req_u.req3);
			break;
		}
		if (optlen < len) {
			ret = -EINVAL;
		} else {
			if (copy_from_sockptr(&req_u.req, optval, len))
				ret = -EFAULT;
			else
				ret = packet_set_ring(sk, &req_u, 0,
						    optname == PACKET_TX_RING);
		}
		release_sock(sk);
		return ret;
	}
	case PACKET_COPY_THRESH:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		pkt_sk(sk)->copy_thresh = val;
		return 0;
	}
	case PACKET_VERSION:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;
		switch (val) {
		case TPACKET_V1:
		case TPACKET_V2:
		case TPACKET_V3:
			break;
		default:
			return -EINVAL;
		}
		lock_sock(sk);
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec) {
			ret = -EBUSY;
		} else {
			po->tp_version = val;
			ret = 0;
		}
		release_sock(sk);
		return ret;
	}
	case PACKET_RESERVE:
	{
		unsigned int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;
		if (val > INT_MAX)
			return -EINVAL;
		lock_sock(sk);
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec) {
			ret = -EBUSY;
		} else {
			po->tp_reserve = val;
			ret = 0;
		}
		release_sock(sk);
		return ret;
	}
	case PACKET_LOSS:
	{
		unsigned int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		lock_sock(sk);
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec) {
			ret = -EBUSY;
		} else {
			packet_sock_flag_set(po, PACKET_SOCK_TP_LOSS, val);
			ret = 0;
		}
		release_sock(sk);
		return ret;
	}
	case PACKET_AUXDATA:
	{
		int val;

		if (optlen < sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		packet_sock_flag_set(po, PACKET_SOCK_AUXDATA, val);
		return 0;
	}
	case PACKET_ORIGDEV:
	{
		int val;

		if (optlen < sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		packet_sock_flag_set(po, PACKET_SOCK_ORIGDEV, val);
		return 0;
	}
	case PACKET_VNET_HDR:
	case PACKET_VNET_HDR_SZ:
	{
		int val, hdr_len;

		if (sock->type != SOCK_RAW)
			return -EINVAL;
		if (optlen < sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		if (optname == PACKET_VNET_HDR_SZ) {
			if (val && val != sizeof(struct virtio_net_hdr) &&
			    val != sizeof(struct virtio_net_hdr_mrg_rxbuf))
				return -EINVAL;
			hdr_len = val;
		} else {
			hdr_len = val ? sizeof(struct virtio_net_hdr) : 0;
		}
		lock_sock(sk);
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec) {
			ret = -EBUSY;
		} else {
			WRITE_ONCE(po->vnet_hdr_sz, hdr_len);
			ret = 0;
		}
		release_sock(sk);
		return ret;
	}
	case PACKET_TIMESTAMP:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		WRITE_ONCE(po->tp_tstamp, val);
		return 0;
	}
	case PACKET_FANOUT:
	{
		struct fanout_args args = { 0 };

		if (optlen != sizeof(int) && optlen != sizeof(args))
			return -EINVAL;
		if (copy_from_sockptr(&args, optval, optlen))
			return -EFAULT;

		return fanout_add(sk, &args);
	}
	case PACKET_FANOUT_DATA:
	{
		/* Paired with the WRITE_ONCE() in fanout_add() */
		if (!READ_ONCE(po->fanout))
			return -EINVAL;

		return fanout_set_data(po, optval, optlen);
	}
	case PACKET_IGNORE_OUTGOING:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;
		if (val < 0 || val > 1)
			return -EINVAL;

		po->prot_hook.ignore_outgoing = !!val;
		return 0;
	}
	case PACKET_TX_HAS_OFF:
	{
		unsigned int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		lock_sock(sk);
		if (!po->rx_ring.pg_vec && !po->tx_ring.pg_vec)
			packet_sock_flag_set(po, PACKET_SOCK_TX_HAS_OFF, val);

		release_sock(sk);
		return 0;
	}
	case PACKET_QDISC_BYPASS:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(val)))
			return -EFAULT;

		packet_sock_flag_set(po, PACKET_SOCK_QDISC_BYPASS, val);
		return 0;
	}
	default:
		return -ENOPROTOOPT;
	}
}

static int packet_getsockopt(struct socket *sock, int level, int optname,
			     char __user *optval, int __user *optlen)
{
	int len;
	int val, lv = sizeof(val);
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	void *data = &val;
	union tpacket_stats_u st;
	struct tpacket_rollover_stats rstats;
	int drops;

	if (level != SOL_PACKET)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case PACKET_STATISTICS:
		spin_lock_bh(&sk->sk_receive_queue.lock);
		memcpy(&st, &po->stats, sizeof(st));
		memset(&po->stats, 0, sizeof(po->stats));
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		drops = atomic_xchg(&po->tp_drops, 0);

		if (po->tp_version == TPACKET_V3) {
			lv = sizeof(struct tpacket_stats_v3);
			st.stats3.tp_drops = drops;
			st.stats3.tp_packets += drops;
			data = &st.stats3;
		} else {
			lv = sizeof(struct tpacket_stats);
			st.stats1.tp_drops = drops;
			st.stats1.tp_packets += drops;
			data = &st.stats1;
		}

		break;
	case PACKET_AUXDATA:
		val = packet_sock_flag(po, PACKET_SOCK_AUXDATA);
		break;
	case PACKET_ORIGDEV:
		val = packet_sock_flag(po, PACKET_SOCK_ORIGDEV);
		break;
	case PACKET_VNET_HDR:
		val = !!READ_ONCE(po->vnet_hdr_sz);
		break;
	case PACKET_VNET_HDR_SZ:
		val = READ_ONCE(po->vnet_hdr_sz);
		break;
	case PACKET_VERSION:
		val = po->tp_version;
		break;
	case PACKET_HDRLEN:
		if (len > sizeof(int))
			len = sizeof(int);
		if (len < sizeof(int))
			return -EINVAL;
		if (copy_from_user(&val, optval, len))
			return -EFAULT;
		switch (val) {
		case TPACKET_V1:
			val = sizeof(struct tpacket_hdr);
			break;
		case TPACKET_V2:
			val = sizeof(struct tpacket2_hdr);
			break;
		case TPACKET_V3:
			val = sizeof(struct tpacket3_hdr);
			break;
		default:
			return -EINVAL;
		}
		break;
	case PACKET_RESERVE:
		val = po->tp_reserve;
		break;
	case PACKET_LOSS:
		val = packet_sock_flag(po, PACKET_SOCK_TP_LOSS);
		break;
	case PACKET_TIMESTAMP:
		val = READ_ONCE(po->tp_tstamp);
		break;
	case PACKET_FANOUT:
		val = (po->fanout ?
		       ((u32)po->fanout->id |
			((u32)po->fanout->type << 16) |
			((u32)po->fanout->flags << 24)) :
		       0);
		break;
	case PACKET_IGNORE_OUTGOING:
		val = po->prot_hook.ignore_outgoing;
		break;
	case PACKET_ROLLOVER_STATS:
		if (!po->rollover)
			return -EINVAL;
		rstats.tp_all = atomic_long_read(&po->rollover->num);
		rstats.tp_huge = atomic_long_read(&po->rollover->num_huge);
		rstats.tp_failed = atomic_long_read(&po->rollover->num_failed);
		data = &rstats;
		lv = sizeof(rstats);
		break;
	case PACKET_TX_HAS_OFF:
		val = packet_sock_flag(po, PACKET_SOCK_TX_HAS_OFF);
		break;
	case PACKET_QDISC_BYPASS:
		val = packet_sock_flag(po, PACKET_SOCK_QDISC_BYPASS);
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (len > lv)
		len = lv;
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, data, len))
		return -EFAULT;
	return 0;
}

static int packet_notifier(struct notifier_block *this,
			   unsigned long msg, void *ptr)
{
	struct sock *sk;
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	rcu_read_lock();
	sk_for_each_rcu(sk, &net->packet.sklist) {
		struct packet_sock *po = pkt_sk(sk);

		switch (msg) {
		case NETDEV_UNREGISTER:
			if (po->mclist)
				packet_dev_mclist_delete(dev, &po->mclist);
			fallthrough;

		case NETDEV_DOWN:
			if (dev->ifindex == po->ifindex) {
				spin_lock(&po->bind_lock);
				if (packet_sock_flag(po, PACKET_SOCK_RUNNING)) {
					__unregister_prot_hook(sk, false);
					sk->sk_err = ENETDOWN;
					if (!sock_flag(sk, SOCK_DEAD))
						sk_error_report(sk);
				}
				if (msg == NETDEV_UNREGISTER) {
					packet_cached_dev_reset(po);
					WRITE_ONCE(po->ifindex, -1);
					netdev_put(po->prot_hook.dev,
						   &po->prot_hook.dev_tracker);
					po->prot_hook.dev = NULL;
				}
				spin_unlock(&po->bind_lock);
			}
			break;
		case NETDEV_UP:
			if (dev->ifindex == po->ifindex) {
				spin_lock(&po->bind_lock);
				if (po->num)
					register_prot_hook(sk);
				spin_unlock(&po->bind_lock);
			}
			break;
		}
	}
	rcu_read_unlock();
	return NOTIFY_DONE;
}


static int packet_ioctl(struct socket *sock, unsigned int cmd,
			unsigned long arg)
{
	struct sock *sk = sock->sk;

	switch (cmd) {
	case SIOCOUTQ:
	{
		int amount = sk_wmem_alloc_get(sk);

		return put_user(amount, (int __user *)arg);
	}
	case SIOCINQ:
	{
		struct sk_buff *skb;
		int amount = 0;

		spin_lock_bh(&sk->sk_receive_queue.lock);
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb)
			amount = skb->len;
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		return put_user(amount, (int __user *)arg);
	}
#ifdef CONFIG_INET
	case SIOCADDRT:
	case SIOCDELRT:
	case SIOCDARP:
	case SIOCGARP:
	case SIOCSARP:
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
		return inet_dgram_ops.ioctl(sock, cmd, arg);
#endif

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static __poll_t packet_poll(struct file *file, struct socket *sock,
				poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	__poll_t mask = datagram_poll(file, sock, wait);

	spin_lock_bh(&sk->sk_receive_queue.lock);
	if (po->rx_ring.pg_vec) {
		if (!packet_previous_rx_frame(po, &po->rx_ring,
			TP_STATUS_KERNEL))
			mask |= EPOLLIN | EPOLLRDNORM;
	}
	packet_rcv_try_clear_pressure(po);
	spin_unlock_bh(&sk->sk_receive_queue.lock);
	spin_lock_bh(&sk->sk_write_queue.lock);
	if (po->tx_ring.pg_vec) {
		if (packet_current_frame(po, &po->tx_ring, TP_STATUS_AVAILABLE))
			mask |= EPOLLOUT | EPOLLWRNORM;
	}
	spin_unlock_bh(&sk->sk_write_queue.lock);
	return mask;
}


/* Dirty? Well, I still did not learn better way to account
 * for user mmaps.
 */

static void packet_mm_open(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct socket *sock = file->private_data;
	struct sock *sk = sock->sk;

	if (sk)
		atomic_inc(&pkt_sk(sk)->mapped);
}

static void packet_mm_close(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct socket *sock = file->private_data;
	struct sock *sk = sock->sk;

	if (sk)
		atomic_dec(&pkt_sk(sk)->mapped);
}

static const struct vm_operations_struct packet_mmap_ops = {
	.open	=	packet_mm_open,
	.close	=	packet_mm_close,
};

static void free_pg_vec(struct pgv *pg_vec, unsigned int order,
			unsigned int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (likely(pg_vec[i].buffer)) {
			if (is_vmalloc_addr(pg_vec[i].buffer))
				vfree(pg_vec[i].buffer);
			else
				free_pages((unsigned long)pg_vec[i].buffer,
					   order);
			pg_vec[i].buffer = NULL;
		}
	}
	kfree(pg_vec);
}

static char *alloc_one_pg_vec_page(unsigned long order)
{
	char *buffer;
	gfp_t gfp_flags = GFP_KERNEL | __GFP_COMP |
			  __GFP_ZERO | __GFP_NOWARN | __GFP_NORETRY;

	buffer = (char *) __get_free_pages(gfp_flags, order);
	if (buffer)
		return buffer;

	/* __get_free_pages failed, fall back to vmalloc */
	buffer = vzalloc(array_size((1 << order), PAGE_SIZE));
	if (buffer)
		return buffer;

	/* vmalloc failed, lets dig into swap here */
	gfp_flags &= ~__GFP_NORETRY;
	buffer = (char *) __get_free_pages(gfp_flags, order);
	if (buffer)
		return buffer;

	/* complete and utter failure */
	return NULL;
}

static struct pgv *alloc_pg_vec(struct tpacket_req *req, int order)
{
	unsigned int block_nr = req->tp_block_nr;
	struct pgv *pg_vec;
	int i;

	pg_vec = kcalloc(block_nr, sizeof(struct pgv), GFP_KERNEL | __GFP_NOWARN);
	if (unlikely(!pg_vec))
		goto out;

	for (i = 0; i < block_nr; i++) {
		pg_vec[i].buffer = alloc_one_pg_vec_page(order);
		if (unlikely(!pg_vec[i].buffer))
			goto out_free_pgvec;
	}

out:
	return pg_vec;

out_free_pgvec:
	free_pg_vec(pg_vec, order, block_nr);
	pg_vec = NULL;
	goto out;
}

static int packet_set_ring(struct sock *sk, union tpacket_req_u *req_u,
		int closing, int tx_ring)
{
	struct pgv *pg_vec = NULL;
	struct packet_sock *po = pkt_sk(sk);
	unsigned long *rx_owner_map = NULL;
	int was_running, order = 0;
	struct packet_ring_buffer *rb;
	struct sk_buff_head *rb_queue;
	__be16 num;
	int err;
	/* Added to avoid minimal code churn */
	struct tpacket_req *req = &req_u->req;

	rb = tx_ring ? &po->tx_ring : &po->rx_ring;
	rb_queue = tx_ring ? &sk->sk_write_queue : &sk->sk_receive_queue;

	err = -EBUSY;
	if (!closing) {
		if (atomic_read(&po->mapped))
			goto out;
		if (packet_read_pending(rb))
			goto out;
	}

	if (req->tp_block_nr) {
		unsigned int min_frame_size;

		/* Sanity tests and some calculations */
		err = -EBUSY;
		if (unlikely(rb->pg_vec))
			goto out;

		switch (po->tp_version) {
		case TPACKET_V1:
			po->tp_hdrlen = TPACKET_HDRLEN;
			break;
		case TPACKET_V2:
			po->tp_hdrlen = TPACKET2_HDRLEN;
			break;
		case TPACKET_V3:
			po->tp_hdrlen = TPACKET3_HDRLEN;
			break;
		}

		err = -EINVAL;
		if (unlikely((int)req->tp_block_size <= 0))
			goto out;
		if (unlikely(!PAGE_ALIGNED(req->tp_block_size)))
			goto out;
		min_frame_size = po->tp_hdrlen + po->tp_reserve;
		if (po->tp_version >= TPACKET_V3 &&
		    req->tp_block_size <
		    BLK_PLUS_PRIV((u64)req_u->req3.tp_sizeof_priv) + min_frame_size)
			goto out;
		if (unlikely(req->tp_frame_size < min_frame_size))
			goto out;
		if (unlikely(req->tp_frame_size & (TPACKET_ALIGNMENT - 1)))
			goto out;

		rb->frames_per_block = req->tp_block_size / req->tp_frame_size;
		if (unlikely(rb->frames_per_block == 0))
			goto out;
		if (unlikely(rb->frames_per_block > UINT_MAX / req->tp_block_nr))
			goto out;
		if (unlikely((rb->frames_per_block * req->tp_block_nr) !=
					req->tp_frame_nr))
			goto out;

		err = -ENOMEM;
		order = get_order(req->tp_block_size);
		pg_vec = alloc_pg_vec(req, order);
		if (unlikely(!pg_vec))
			goto out;
		switch (po->tp_version) {
		case TPACKET_V3:
			/* Block transmit is not supported yet */
			if (!tx_ring) {
				init_prb_bdqc(po, rb, pg_vec, req_u);
			} else {
				struct tpacket_req3 *req3 = &req_u->req3;

				if (req3->tp_retire_blk_tov ||
				    req3->tp_sizeof_priv ||
				    req3->tp_feature_req_word) {
					err = -EINVAL;
					goto out_free_pg_vec;
				}
			}
			break;
		default:
			if (!tx_ring) {
				rx_owner_map = bitmap_alloc(req->tp_frame_nr,
					GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
				if (!rx_owner_map)
					goto out_free_pg_vec;
			}
			break;
		}
	}
	/* Done */
	else {
		err = -EINVAL;
		if (unlikely(req->tp_frame_nr))
			goto out;
	}


	/* Detach socket from network */
	spin_lock(&po->bind_lock);
	was_running = packet_sock_flag(po, PACKET_SOCK_RUNNING);
	num = po->num;
	if (was_running) {
		WRITE_ONCE(po->num, 0);
		__unregister_prot_hook(sk, false);
	}
	spin_unlock(&po->bind_lock);

	synchronize_net();

	err = -EBUSY;
	mutex_lock(&po->pg_vec_lock);
	if (closing || atomic_read(&po->mapped) == 0) {
		err = 0;
		spin_lock_bh(&rb_queue->lock);
		swap(rb->pg_vec, pg_vec);
		if (po->tp_version <= TPACKET_V2)
			swap(rb->rx_owner_map, rx_owner_map);
		rb->frame_max = (req->tp_frame_nr - 1);
		rb->head = 0;
		rb->frame_size = req->tp_frame_size;
		spin_unlock_bh(&rb_queue->lock);

		swap(rb->pg_vec_order, order);
		swap(rb->pg_vec_len, req->tp_block_nr);

		rb->pg_vec_pages = req->tp_block_size/PAGE_SIZE;
		po->prot_hook.func = (po->rx_ring.pg_vec) ?
						tpacket_rcv : packet_rcv;
		skb_queue_purge(rb_queue);
		if (atomic_read(&po->mapped))
			pr_err("packet_mmap: vma is busy: %d\n",
			       atomic_read(&po->mapped));
	}
	mutex_unlock(&po->pg_vec_lock);

	spin_lock(&po->bind_lock);
	if (was_running) {
		WRITE_ONCE(po->num, num);
		register_prot_hook(sk);
	}
	spin_unlock(&po->bind_lock);
	if (pg_vec && (po->tp_version > TPACKET_V2)) {
		/* Because we don't support block-based V3 on tx-ring */
		if (!tx_ring)
			prb_shutdown_retire_blk_timer(po, rb_queue);
	}

out_free_pg_vec:
	if (pg_vec) {
		bitmap_free(rx_owner_map);
		free_pg_vec(pg_vec, order, req->tp_block_nr);
	}
out:
	return err;
}

static int packet_mmap(struct file *file, struct socket *sock,
		struct vm_area_struct *vma)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	unsigned long size, expected_size;
	struct packet_ring_buffer *rb;
	unsigned long start;
	int err = -EINVAL;
	int i;

	if (vma->vm_pgoff)
		return -EINVAL;

	mutex_lock(&po->pg_vec_lock);

	expected_size = 0;
	for (rb = &po->rx_ring; rb <= &po->tx_ring; rb++) {
		if (rb->pg_vec) {
			expected_size += rb->pg_vec_len
						* rb->pg_vec_pages
						* PAGE_SIZE;
		}
	}

	if (expected_size == 0)
		goto out;

	size = vma->vm_end - vma->vm_start;
	if (size != expected_size)
		goto out;

	start = vma->vm_start;
	for (rb = &po->rx_ring; rb <= &po->tx_ring; rb++) {
		if (rb->pg_vec == NULL)
			continue;

		for (i = 0; i < rb->pg_vec_len; i++) {
			struct page *page;
			void *kaddr = rb->pg_vec[i].buffer;
			int pg_num;

			for (pg_num = 0; pg_num < rb->pg_vec_pages; pg_num++) {
				page = pgv_to_page(kaddr);
				err = vm_insert_page(vma, start, page);
				if (unlikely(err))
					goto out;
				start += PAGE_SIZE;
				kaddr += PAGE_SIZE;
			}
		}
	}

	atomic_inc(&po->mapped);
	vma->vm_ops = &packet_mmap_ops;
	err = 0;

out:
	mutex_unlock(&po->pg_vec_lock);
	return err;
}

static const struct proto_ops packet_ops_spkt = {
	.family =	PF_PACKET,
	.owner =	THIS_MODULE,
	.release =	packet_release,
	.bind =		packet_bind_spkt,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	packet_getname_spkt,
	.poll =		datagram_poll,
	.ioctl =	packet_ioctl,
	.gettstamp =	sock_gettstamp,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.sendmsg =	packet_sendmsg_spkt,
	.recvmsg =	packet_recvmsg,
	.mmap =		sock_no_mmap,
};

static const struct proto_ops packet_ops = {
	.family =	PF_PACKET,
	.owner =	THIS_MODULE,
	.release =	packet_release,
	.bind =		packet_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	packet_getname,
	.poll =		packet_poll,
	.ioctl =	packet_ioctl,
	.gettstamp =	sock_gettstamp,
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	packet_setsockopt,
	.getsockopt =	packet_getsockopt,
	.sendmsg =	packet_sendmsg,
	.recvmsg =	packet_recvmsg,
	.mmap =		packet_mmap,
};

static const struct net_proto_family packet_family_ops = {
	.family =	PF_PACKET,
	.create =	packet_create,
	.owner	=	THIS_MODULE,
};

static struct notifier_block packet_netdev_notifier = {
	.notifier_call =	packet_notifier,
};

#ifdef CONFIG_PROC_FS

static void *packet_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct net *net = seq_file_net(seq);

	rcu_read_lock();
	return seq_hlist_start_head_rcu(&net->packet.sklist, *pos);
}

static void *packet_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	return seq_hlist_next_rcu(v, &net->packet.sklist, pos);
}

static void packet_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int packet_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq,
			   "%*sRefCnt Type Proto  Iface R Rmem   User   Inode\n",
			   IS_ENABLED(CONFIG_64BIT) ? -17 : -9, "sk");
	else {
		struct sock *s = sk_entry(v);
		const struct packet_sock *po = pkt_sk(s);

		seq_printf(seq,
			   "%pK %-6d %-4d %04x   %-5d %1d %-6u %-6u %-6lu\n",
			   s,
			   refcount_read(&s->sk_refcnt),
			   s->sk_type,
			   ntohs(READ_ONCE(po->num)),
			   READ_ONCE(po->ifindex),
			   packet_sock_flag(po, PACKET_SOCK_RUNNING),
			   atomic_read(&s->sk_rmem_alloc),
			   from_kuid_munged(seq_user_ns(seq), sock_i_uid(s)),
			   sock_i_ino(s));
	}

	return 0;
}

static const struct seq_operations packet_seq_ops = {
	.start	= packet_seq_start,
	.next	= packet_seq_next,
	.stop	= packet_seq_stop,
	.show	= packet_seq_show,
};
#endif

static int __net_init packet_net_init(struct net *net)
{
	mutex_init(&net->packet.sklist_lock);
	INIT_HLIST_HEAD(&net->packet.sklist);

#ifdef CONFIG_PROC_FS
	if (!proc_create_net("packet", 0, net->proc_net, &packet_seq_ops,
			sizeof(struct seq_net_private)))
		return -ENOMEM;
#endif /* CONFIG_PROC_FS */

	return 0;
}

static void __net_exit packet_net_exit(struct net *net)
{
	remove_proc_entry("packet", net->proc_net);
	WARN_ON_ONCE(!hlist_empty(&net->packet.sklist));
}

static struct pernet_operations packet_net_ops = {
	.init = packet_net_init,
	.exit = packet_net_exit,
};


static void __exit packet_exit(void)
{
	sock_unregister(PF_PACKET);
	proto_unregister(&packet_proto);
	unregister_netdevice_notifier(&packet_netdev_notifier);
	unregister_pernet_subsys(&packet_net_ops);
}

static int __init packet_init(void)
{
	int rc;

	rc = register_pernet_subsys(&packet_net_ops);
	if (rc)
		goto out;
	rc = register_netdevice_notifier(&packet_netdev_notifier);
	if (rc)
		goto out_pernet;
	rc = proto_register(&packet_proto, 0);
	if (rc)
		goto out_notifier;
	rc = sock_register(&packet_family_ops);
	if (rc)
		goto out_proto;

	return 0;

out_proto:
	proto_unregister(&packet_proto);
out_notifier:
	unregister_netdevice_notifier(&packet_netdev_notifier);
out_pernet:
	unregister_pernet_subsys(&packet_net_ops);
out:
	return rc;
}

module_init(packet_init);
module_exit(packet_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_PACKET);
