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
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

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
#include <asm/system.h>
#include <asm/uaccess.h>
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

#ifdef CONFIG_INET
#include <net/inet_common.h>
#endif

/*
   Assumptions:
   - if device has no dev->hard_header routine, it adds and removes ll header
     inside itself. In this case ll header is invisible outside of device,
     but higher levels still should reserve dev->hard_header_len.
     Some devices are enough clever to reallocate skb, when header
     will not fit to reserved space (tunnel), another ones are silly
     (PPP).
   - packet socket receives packets with pulled ll header,
     so that SOCK_RAW should push it back.

On receive:
-----------

Incoming, dev->hard_header!=NULL
   mac_header -> ll header
   data       -> data

Outgoing, dev->hard_header!=NULL
   mac_header -> ll header
   data       -> ll header

Incoming, dev->hard_header==NULL
   mac_header -> UNKNOWN position. It is very likely, that it points to ll
		 header.  PPP makes it, that is wrong, because introduce
		 assymetry between rx and tx paths.
   data       -> data

Outgoing, dev->hard_header==NULL
   mac_header -> data. ll header is still not built!
   data       -> data

Resume
  If dev->hard_header==NULL we are unlikely to restore sensible ll header.


On transmit:
------------

dev->hard_header != NULL
   mac_header -> ll header
   data       -> ll header

dev->hard_header == NULL (ll header is added by device, we cannot control it)
   mac_header -> data
   data       -> data

   We should set nh.raw on output to correct posistion,
   packet classifier depends on it.
 */

/* Private packet socket structures. */

struct packet_mclist {
	struct packet_mclist	*next;
	int			ifindex;
	int			count;
	unsigned short		type;
	unsigned short		alen;
	unsigned char		addr[MAX_ADDR_LEN];
};
/* identical to struct packet_mreq except it has
 * a longer address field.
 */
struct packet_mreq_max {
	int		mr_ifindex;
	unsigned short	mr_type;
	unsigned short	mr_alen;
	unsigned char	mr_address[MAX_ADDR_LEN];
};

static int packet_set_ring(struct sock *sk, union tpacket_req_u *req_u,
		int closing, int tx_ring);


#define V3_ALIGNMENT	(8)

#define BLK_HDR_LEN	(ALIGN(sizeof(struct tpacket_block_desc), V3_ALIGNMENT))

#define BLK_PLUS_PRIV(sz_of_priv) \
	(BLK_HDR_LEN + ALIGN((sz_of_priv), V3_ALIGNMENT))

/* kbdq - kernel block descriptor queue */
struct tpacket_kbdq_core {
	struct pgv	*pkbdq;
	unsigned int	feature_req_word;
	unsigned int	hdrlen;
	unsigned char	reset_pending_on_curr_blk;
	unsigned char   delete_blk_timer;
	unsigned short	kactive_blk_num;
	unsigned short	blk_sizeof_priv;

	/* last_kactive_blk_num:
	 * trick to see if user-space has caught up
	 * in order to avoid refreshing timer when every single pkt arrives.
	 */
	unsigned short	last_kactive_blk_num;

	char		*pkblk_start;
	char		*pkblk_end;
	int		kblk_size;
	unsigned int	knum_blocks;
	uint64_t	knxt_seq_num;
	char		*prev;
	char		*nxt_offset;
	struct sk_buff	*skb;

	atomic_t	blk_fill_in_prog;

	/* Default is set to 8ms */
#define DEFAULT_PRB_RETIRE_TOV	(8)

	unsigned short  retire_blk_tov;
	unsigned short  version;
	unsigned long	tov_in_jiffies;

	/* timer to retire an outstanding block */
	struct timer_list retire_blk_timer;
};

#define PGV_FROM_VMALLOC 1
struct pgv {
	char *buffer;
};

struct packet_ring_buffer {
	struct pgv		*pg_vec;
	unsigned int		head;
	unsigned int		frames_per_block;
	unsigned int		frame_size;
	unsigned int		frame_max;

	unsigned int		pg_vec_order;
	unsigned int		pg_vec_pages;
	unsigned int		pg_vec_len;

	struct tpacket_kbdq_core	prb_bdqc;
	atomic_t		pending;
};

#define BLOCK_STATUS(x)	((x)->hdr.bh1.block_status)
#define BLOCK_NUM_PKTS(x)	((x)->hdr.bh1.num_pkts)
#define BLOCK_O2FP(x)		((x)->hdr.bh1.offset_to_first_pkt)
#define BLOCK_LEN(x)		((x)->hdr.bh1.blk_len)
#define BLOCK_SNUM(x)		((x)->hdr.bh1.seq_num)
#define BLOCK_O2PRIV(x)	((x)->offset_to_priv)
#define BLOCK_PRIV(x)		((void *)((char *)(x) + BLOCK_O2PRIV(x)))

struct packet_sock;
static int tpacket_snd(struct packet_sock *po, struct msghdr *msg);

static void *packet_previous_frame(struct packet_sock *po,
		struct packet_ring_buffer *rb,
		int status);
static void packet_increment_head(struct packet_ring_buffer *buff);
static int prb_curr_blk_in_use(struct tpacket_kbdq_core *,
			struct tpacket_block_desc *);
static void *prb_dispatch_next_block(struct tpacket_kbdq_core *,
			struct packet_sock *);
static void prb_retire_current_block(struct tpacket_kbdq_core *,
		struct packet_sock *, unsigned int status);
static int prb_queue_frozen(struct tpacket_kbdq_core *);
static void prb_open_block(struct tpacket_kbdq_core *,
		struct tpacket_block_desc *);
static void prb_retire_rx_blk_timer_expired(unsigned long);
static void _prb_refresh_rx_retire_blk_timer(struct tpacket_kbdq_core *);
static void prb_init_blk_timer(struct packet_sock *,
		struct tpacket_kbdq_core *,
		void (*func) (unsigned long));
static void prb_fill_rxhash(struct tpacket_kbdq_core *, struct tpacket3_hdr *);
static void prb_clear_rxhash(struct tpacket_kbdq_core *,
		struct tpacket3_hdr *);
static void prb_fill_vlan_info(struct tpacket_kbdq_core *,
		struct tpacket3_hdr *);
static void packet_flush_mclist(struct sock *sk);

struct packet_fanout;
struct packet_sock {
	/* struct sock has to be the first member of packet_sock */
	struct sock		sk;
	struct packet_fanout	*fanout;
	struct tpacket_stats	stats;
	union  tpacket_stats_u	stats_u;
	struct packet_ring_buffer	rx_ring;
	struct packet_ring_buffer	tx_ring;
	int			copy_thresh;
	spinlock_t		bind_lock;
	struct mutex		pg_vec_lock;
	unsigned int		running:1,	/* prot_hook is attached*/
				auxdata:1,
				origdev:1,
				has_vnet_hdr:1;
	int			ifindex;	/* bound device		*/
	__be16			num;
	struct packet_mclist	*mclist;
	atomic_t		mapped;
	enum tpacket_versions	tp_version;
	unsigned int		tp_hdrlen;
	unsigned int		tp_reserve;
	unsigned int		tp_loss:1;
	unsigned int		tp_tstamp;
	struct packet_type	prot_hook ____cacheline_aligned_in_smp;
};

#define PACKET_FANOUT_MAX	256

struct packet_fanout {
#ifdef CONFIG_NET_NS
	struct net		*net;
#endif
	unsigned int		num_members;
	u16			id;
	u8			type;
	u8			defrag;
	atomic_t		rr_cur;
	struct list_head	list;
	struct sock		*arr[PACKET_FANOUT_MAX];
	spinlock_t		lock;
	atomic_t		sk_ref;
	struct packet_type	prot_hook ____cacheline_aligned_in_smp;
};

struct packet_skb_cb {
	unsigned int origlen;
	union {
		struct sockaddr_pkt pkt;
		struct sockaddr_ll ll;
	} sa;
};

#define PACKET_SKB_CB(__skb)	((struct packet_skb_cb *)((__skb)->cb))

#define GET_PBDQC_FROM_RB(x)	((struct tpacket_kbdq_core *)(&(x)->prb_bdqc))
#define GET_PBLOCK_DESC(x, bid)	\
	((struct tpacket_block_desc *)((x)->pkbdq[(bid)].buffer))
#define GET_CURR_PBLOCK_DESC_FROM_CORE(x)	\
	((struct tpacket_block_desc *)((x)->pkbdq[(x)->kactive_blk_num].buffer))
#define GET_NEXT_PRB_BLK_NUM(x) \
	(((x)->kactive_blk_num < ((x)->knum_blocks-1)) ? \
	((x)->kactive_blk_num+1) : 0)

static struct packet_sock *pkt_sk(struct sock *sk)
{
	return (struct packet_sock *)sk;
}

static void __fanout_unlink(struct sock *sk, struct packet_sock *po);
static void __fanout_link(struct sock *sk, struct packet_sock *po);

/* register_prot_hook must be invoked with the po->bind_lock held,
 * or from a context in which asynchronous accesses to the packet
 * socket is not possible (packet_create()).
 */
static void register_prot_hook(struct sock *sk)
{
	struct packet_sock *po = pkt_sk(sk);
	if (!po->running) {
		if (po->fanout)
			__fanout_link(sk, po);
		else
			dev_add_pack(&po->prot_hook);
		sock_hold(sk);
		po->running = 1;
	}
}

/* {,__}unregister_prot_hook() must be invoked with the po->bind_lock
 * held.   If the sync parameter is true, we will temporarily drop
 * the po->bind_lock and do a synchronize_net to make sure no
 * asynchronous packet processing paths still refer to the elements
 * of po->prot_hook.  If the sync parameter is false, it is the
 * callers responsibility to take care of this.
 */
static void __unregister_prot_hook(struct sock *sk, bool sync)
{
	struct packet_sock *po = pkt_sk(sk);

	po->running = 0;
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

	if (po->running)
		__unregister_prot_hook(sk, sync);
}

static inline __pure struct page *pgv_to_page(void *addr)
{
	if (is_vmalloc_addr(addr))
		return vmalloc_to_page(addr);
	return virt_to_page(addr);
}

static void __packet_set_status(struct packet_sock *po, void *frame, int status)
{
	union {
		struct tpacket_hdr *h1;
		struct tpacket2_hdr *h2;
		void *raw;
	} h;

	h.raw = frame;
	switch (po->tp_version) {
	case TPACKET_V1:
		h.h1->tp_status = status;
		flush_dcache_page(pgv_to_page(&h.h1->tp_status));
		break;
	case TPACKET_V2:
		h.h2->tp_status = status;
		flush_dcache_page(pgv_to_page(&h.h2->tp_status));
		break;
	case TPACKET_V3:
	default:
		WARN(1, "TPACKET version not supported.\n");
		BUG();
	}

	smp_wmb();
}

static int __packet_get_status(struct packet_sock *po, void *frame)
{
	union {
		struct tpacket_hdr *h1;
		struct tpacket2_hdr *h2;
		void *raw;
	} h;

	smp_rmb();

	h.raw = frame;
	switch (po->tp_version) {
	case TPACKET_V1:
		flush_dcache_page(pgv_to_page(&h.h1->tp_status));
		return h.h1->tp_status;
	case TPACKET_V2:
		flush_dcache_page(pgv_to_page(&h.h2->tp_status));
		return h.h2->tp_status;
	case TPACKET_V3:
	default:
		WARN(1, "TPACKET version not supported.\n");
		BUG();
		return 0;
	}
}

static void *packet_lookup_frame(struct packet_sock *po,
		struct packet_ring_buffer *rb,
		unsigned int position,
		int status)
{
	unsigned int pg_vec_pos, frame_offset;
	union {
		struct tpacket_hdr *h1;
		struct tpacket2_hdr *h2;
		void *raw;
	} h;

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
		int tx_ring,
		struct sk_buff_head *rb_queue)
{
	struct tpacket_kbdq_core *pkc;

	pkc = tx_ring ? &po->tx_ring.prb_bdqc : &po->rx_ring.prb_bdqc;

	spin_lock(&rb_queue->lock);
	pkc->delete_blk_timer = 1;
	spin_unlock(&rb_queue->lock);

	prb_del_retire_blk_timer(pkc);
}

static void prb_init_blk_timer(struct packet_sock *po,
		struct tpacket_kbdq_core *pkc,
		void (*func) (unsigned long))
{
	init_timer(&pkc->retire_blk_timer);
	pkc->retire_blk_timer.data = (long)po;
	pkc->retire_blk_timer.function = func;
	pkc->retire_blk_timer.expires = jiffies;
}

static void prb_setup_retire_blk_timer(struct packet_sock *po, int tx_ring)
{
	struct tpacket_kbdq_core *pkc;

	if (tx_ring)
		BUG();

	pkc = tx_ring ? &po->tx_ring.prb_bdqc : &po->rx_ring.prb_bdqc;
	prb_init_blk_timer(po, pkc, prb_retire_rx_blk_timer_expired);
}

static int prb_calc_retire_blk_tmo(struct packet_sock *po,
				int blk_size_in_bytes)
{
	struct net_device *dev;
	unsigned int mbits = 0, msec = 0, div = 0, tmo = 0;
	struct ethtool_cmd ecmd;
	int err;

	rtnl_lock();
	dev = __dev_get_by_index(sock_net(&po->sk), po->ifindex);
	if (unlikely(!dev)) {
		rtnl_unlock();
		return DEFAULT_PRB_RETIRE_TOV;
	}
	err = __ethtool_get_settings(dev, &ecmd);
	rtnl_unlock();
	if (!err) {
		switch (ecmd.speed) {
		case SPEED_10000:
			msec = 1;
			div = 10000/1000;
			break;
		case SPEED_1000:
			msec = 1;
			div = 1000/1000;
			break;
		/*
		 * If the link speed is so slow you don't really
		 * need to worry about perf anyways
		 */
		case SPEED_100:
		case SPEED_10:
		default:
			return DEFAULT_PRB_RETIRE_TOV;
		}
	}

	mbits = (blk_size_in_bytes * 8) / (1024 * 1024);

	if (div)
		mbits /= div;

	tmo = mbits * msec;

	if (div)
		return tmo+1;
	return tmo;
}

static void prb_init_ft_ops(struct tpacket_kbdq_core *p1,
			union tpacket_req_u *req_u)
{
	p1->feature_req_word = req_u->req3.tp_feature_req_word;
}

static void init_prb_bdqc(struct packet_sock *po,
			struct packet_ring_buffer *rb,
			struct pgv *pg_vec,
			union tpacket_req_u *req_u, int tx_ring)
{
	struct tpacket_kbdq_core *p1 = &rb->prb_bdqc;
	struct tpacket_block_desc *pbd;

	memset(p1, 0x0, sizeof(*p1));

	p1->knxt_seq_num = 1;
	p1->pkbdq = pg_vec;
	pbd = (struct tpacket_block_desc *)pg_vec[0].buffer;
	p1->pkblk_start	= (char *)pg_vec[0].buffer;
	p1->kblk_size = req_u->req3.tp_block_size;
	p1->knum_blocks	= req_u->req3.tp_block_nr;
	p1->hdrlen = po->tp_hdrlen;
	p1->version = po->tp_version;
	p1->last_kactive_blk_num = 0;
	po->stats_u.stats3.tp_freeze_q_cnt = 0;
	if (req_u->req3.tp_retire_blk_tov)
		p1->retire_blk_tov = req_u->req3.tp_retire_blk_tov;
	else
		p1->retire_blk_tov = prb_calc_retire_blk_tmo(po,
						req_u->req3.tp_block_size);
	p1->tov_in_jiffies = msecs_to_jiffies(p1->retire_blk_tov);
	p1->blk_sizeof_priv = req_u->req3.tp_sizeof_priv;

	prb_init_ft_ops(p1, req_u);
	prb_setup_retire_blk_timer(po, tx_ring);
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
static void prb_retire_rx_blk_timer_expired(unsigned long data)
{
	struct packet_sock *po = (struct packet_sock *)data;
	struct tpacket_kbdq_core *pkc = &po->rx_ring.prb_bdqc;
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
		while (atomic_read(&pkc->blk_fill_in_prog)) {
			/* Waiting for skb_copy_bits to finish... */
			cpu_relax();
		}
	}

	if (pkc->last_kactive_blk_num == pkc->kactive_blk_num) {
		if (!frozen) {
			prb_retire_current_block(pkc, po, TP_STATUS_BLK_TMO);
			if (!prb_dispatch_next_block(pkc, po))
				goto refresh_timer;
			else
				goto out;
		} else {
			/* Case 1. Queue was frozen because user-space was
			 *	   lagging behind.
			 */
			if (prb_curr_blk_in_use(pkc, pbd)) {
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

	if (po->stats.tp_drops)
		status |= TP_STATUS_LOSING;

	last_pkt = (struct tpacket3_hdr *)pkc1->prev;
	last_pkt->tp_next_offset = 0;

	/* Get the ts of the last pkt */
	if (BLOCK_NUM_PKTS(pbd1)) {
		h1->ts_last_pkt.ts_sec = last_pkt->tp_sec;
		h1->ts_last_pkt.ts_nsec	= last_pkt->tp_nsec;
	} else {
		/* Ok, we tmo'd - so get the current time */
		struct timespec ts;
		getnstimeofday(&ts);
		h1->ts_last_pkt.ts_sec = ts.tv_sec;
		h1->ts_last_pkt.ts_nsec	= ts.tv_nsec;
	}

	smp_wmb();

	/* Flush the block */
	prb_flush_block(pkc1, pbd1, status);

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
	struct timespec ts;
	struct tpacket_hdr_v1 *h1 = &pbd1->hdr.bh1;

	smp_rmb();

	if (likely(TP_STATUS_KERNEL == BLOCK_STATUS(pbd1))) {

		/* We could have just memset this but we will lose the
		 * flexibility of making the priv area sticky
		 */
		BLOCK_SNUM(pbd1) = pkc1->knxt_seq_num++;
		BLOCK_NUM_PKTS(pbd1) = 0;
		BLOCK_LEN(pbd1) = BLK_PLUS_PRIV(pkc1->blk_sizeof_priv);
		getnstimeofday(&ts);
		h1->ts_first_pkt.ts_sec = ts.tv_sec;
		h1->ts_first_pkt.ts_nsec = ts.tv_nsec;
		pkc1->pkblk_start = (char *)pbd1;
		pkc1->nxt_offset = (char *)(pkc1->pkblk_start +
		BLK_PLUS_PRIV(pkc1->blk_sizeof_priv));
		BLOCK_O2FP(pbd1) = (__u32)BLK_PLUS_PRIV(pkc1->blk_sizeof_priv);
		BLOCK_O2PRIV(pbd1) = BLK_HDR_LEN;
		pbd1->version = pkc1->version;
		pkc1->prev = pkc1->nxt_offset;
		pkc1->pkblk_end = pkc1->pkblk_start + pkc1->kblk_size;
		prb_thaw_queue(pkc1);
		_prb_refresh_rx_retire_blk_timer(pkc1);

		smp_wmb();

		return;
	}

	WARN(1, "ERROR block:%p is NOT FREE status:%d kactive_blk_num:%d\n",
		pbd1, BLOCK_STATUS(pbd1), pkc1->kactive_blk_num);
	dump_stack();
	BUG();
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
	po->stats_u.stats3.tp_freeze_q_cnt++;
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
			while (atomic_read(&pkc->blk_fill_in_prog)) {
				/* Waiting for skb_copy_bits to finish... */
				cpu_relax();
			}
		}
		prb_close_block(pkc, pbd, po, status);
		return;
	}

	WARN(1, "ERROR-pbd[%d]:%p\n", pkc->kactive_blk_num, pbd);
	dump_stack();
	BUG();
}

static int prb_curr_blk_in_use(struct tpacket_kbdq_core *pkc,
				      struct tpacket_block_desc *pbd)
{
	return TP_STATUS_USER & BLOCK_STATUS(pbd);
}

static int prb_queue_frozen(struct tpacket_kbdq_core *pkc)
{
	return pkc->reset_pending_on_curr_blk;
}

static void prb_clear_blk_fill_status(struct packet_ring_buffer *rb)
{
	struct tpacket_kbdq_core *pkc  = GET_PBDQC_FROM_RB(rb);
	atomic_dec(&pkc->blk_fill_in_prog);
}

static void prb_fill_rxhash(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	ppd->hv1.tp_rxhash = skb_get_rxhash(pkc->skb);
}

static void prb_clear_rxhash(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	ppd->hv1.tp_rxhash = 0;
}

static void prb_fill_vlan_info(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
	if (vlan_tx_tag_present(pkc->skb)) {
		ppd->hv1.tp_vlan_tci = vlan_tx_tag_get(pkc->skb);
		ppd->tp_status = TP_STATUS_VLAN_VALID;
	} else {
		ppd->hv1.tp_vlan_tci = ppd->tp_status = 0;
	}
}

static void prb_run_all_ft_ops(struct tpacket_kbdq_core *pkc,
			struct tpacket3_hdr *ppd)
{
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
{
	struct tpacket3_hdr *ppd;

	ppd  = (struct tpacket3_hdr *)curr;
	ppd->tp_next_offset = TOTAL_PKT_LEN_INCL_ALIGN(len);
	pkc->prev = curr;
	pkc->nxt_offset += TOTAL_PKT_LEN_INCL_ALIGN(len);
	BLOCK_LEN(pbd) += TOTAL_PKT_LEN_INCL_ALIGN(len);
	BLOCK_NUM_PKTS(pbd) += 1;
	atomic_inc(&pkc->blk_fill_in_prog);
	prb_run_all_ft_ops(pkc, ppd);
}

/* Assumes caller has the sk->rx_queue.lock */
static void *__packet_lookup_frame_in_block(struct packet_sock *po,
					    struct sk_buff *skb,
						int status,
					    unsigned int len
					    )
{
	struct tpacket_kbdq_core *pkc;
	struct tpacket_block_desc *pbd;
	char *curr, *end;

	pkc = GET_PBDQC_FROM_RB(((struct packet_ring_buffer *)&po->rx_ring));
	pbd = GET_CURR_PBLOCK_DESC_FROM_CORE(pkc);

	/* Queue is frozen when user space is lagging behind */
	if (prb_queue_frozen(pkc)) {
		/*
		 * Check if that last block which caused the queue to freeze,
		 * is still in_use by user-space.
		 */
		if (prb_curr_blk_in_use(pkc, pbd)) {
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
	end = (char *) ((char *)pbd + pkc->kblk_size);

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
		return __packet_lookup_frame_in_block(po, skb, status, len);
	default:
		WARN(1, "TPACKET version not supported\n");
		BUG();
		return 0;
	}
}

static void *prb_lookup_block(struct packet_sock *po,
				     struct packet_ring_buffer *rb,
				     unsigned int previous,
				     int status)
{
	struct tpacket_kbdq_core *pkc  = GET_PBDQC_FROM_RB(rb);
	struct tpacket_block_desc *pbd = GET_PBLOCK_DESC(pkc, previous);

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

static void packet_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_error_queue);

	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(atomic_read(&sk->sk_wmem_alloc));

	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Attempt to release alive packet socket: %p\n", sk);
		return;
	}

	sk_refcnt_debug_dec(sk);
}

static int fanout_rr_next(struct packet_fanout *f, unsigned int num)
{
	int x = atomic_read(&f->rr_cur) + 1;

	if (x >= num)
		x = 0;

	return x;
}

static struct sock *fanout_demux_hash(struct packet_fanout *f, struct sk_buff *skb, unsigned int num)
{
	u32 idx, hash = skb->rxhash;

	idx = ((u64)hash * num) >> 32;

	return f->arr[idx];
}

static struct sock *fanout_demux_lb(struct packet_fanout *f, struct sk_buff *skb, unsigned int num)
{
	int cur, old;

	cur = atomic_read(&f->rr_cur);
	while ((old = atomic_cmpxchg(&f->rr_cur, cur,
				     fanout_rr_next(f, num))) != cur)
		cur = old;
	return f->arr[cur];
}

static struct sock *fanout_demux_cpu(struct packet_fanout *f, struct sk_buff *skb, unsigned int num)
{
	unsigned int cpu = smp_processor_id();

	return f->arr[cpu % num];
}

static int packet_rcv_fanout(struct sk_buff *skb, struct net_device *dev,
			     struct packet_type *pt, struct net_device *orig_dev)
{
	struct packet_fanout *f = pt->af_packet_priv;
	unsigned int num = f->num_members;
	struct packet_sock *po;
	struct sock *sk;

	if (!net_eq(dev_net(dev), read_pnet(&f->net)) ||
	    !num) {
		kfree_skb(skb);
		return 0;
	}

	switch (f->type) {
	case PACKET_FANOUT_HASH:
	default:
		if (f->defrag) {
			skb = ip_check_defrag(skb, IP_DEFRAG_AF_PACKET);
			if (!skb)
				return 0;
		}
		skb_get_rxhash(skb);
		sk = fanout_demux_hash(f, skb, num);
		break;
	case PACKET_FANOUT_LB:
		sk = fanout_demux_lb(f, skb, num);
		break;
	case PACKET_FANOUT_CPU:
		sk = fanout_demux_cpu(f, skb, num);
		break;
	}

	po = pkt_sk(sk);

	return po->prot_hook.func(skb, dev, &po->prot_hook, orig_dev);
}

static DEFINE_MUTEX(fanout_mutex);
static LIST_HEAD(fanout_list);

static void __fanout_link(struct sock *sk, struct packet_sock *po)
{
	struct packet_fanout *f = po->fanout;

	spin_lock(&f->lock);
	f->arr[f->num_members] = sk;
	smp_wmb();
	f->num_members++;
	spin_unlock(&f->lock);
}

static void __fanout_unlink(struct sock *sk, struct packet_sock *po)
{
	struct packet_fanout *f = po->fanout;
	int i;

	spin_lock(&f->lock);
	for (i = 0; i < f->num_members; i++) {
		if (f->arr[i] == sk)
			break;
	}
	BUG_ON(i >= f->num_members);
	f->arr[i] = f->arr[f->num_members - 1];
	f->num_members--;
	spin_unlock(&f->lock);
}

static int fanout_add(struct sock *sk, u16 id, u16 type_flags)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_fanout *f, *match;
	u8 type = type_flags & 0xff;
	u8 defrag = (type_flags & PACKET_FANOUT_FLAG_DEFRAG) ? 1 : 0;
	int err;

	switch (type) {
	case PACKET_FANOUT_HASH:
	case PACKET_FANOUT_LB:
	case PACKET_FANOUT_CPU:
		break;
	default:
		return -EINVAL;
	}

	if (!po->running)
		return -EINVAL;

	if (po->fanout)
		return -EALREADY;

	mutex_lock(&fanout_mutex);
	match = NULL;
	list_for_each_entry(f, &fanout_list, list) {
		if (f->id == id &&
		    read_pnet(&f->net) == sock_net(sk)) {
			match = f;
			break;
		}
	}
	err = -EINVAL;
	if (match && match->defrag != defrag)
		goto out;
	if (!match) {
		err = -ENOMEM;
		match = kzalloc(sizeof(*match), GFP_KERNEL);
		if (!match)
			goto out;
		write_pnet(&match->net, sock_net(sk));
		match->id = id;
		match->type = type;
		match->defrag = defrag;
		atomic_set(&match->rr_cur, 0);
		INIT_LIST_HEAD(&match->list);
		spin_lock_init(&match->lock);
		atomic_set(&match->sk_ref, 0);
		match->prot_hook.type = po->prot_hook.type;
		match->prot_hook.dev = po->prot_hook.dev;
		match->prot_hook.func = packet_rcv_fanout;
		match->prot_hook.af_packet_priv = match;
		dev_add_pack(&match->prot_hook);
		list_add(&match->list, &fanout_list);
	}
	err = -EINVAL;
	if (match->type == type &&
	    match->prot_hook.type == po->prot_hook.type &&
	    match->prot_hook.dev == po->prot_hook.dev) {
		err = -ENOSPC;
		if (atomic_read(&match->sk_ref) < PACKET_FANOUT_MAX) {
			__dev_remove_pack(&po->prot_hook);
			po->fanout = match;
			atomic_inc(&match->sk_ref);
			__fanout_link(sk, po);
			err = 0;
		}
	}
out:
	mutex_unlock(&fanout_mutex);
	return err;
}

static void fanout_release(struct sock *sk)
{
	struct packet_sock *po = pkt_sk(sk);
	struct packet_fanout *f;

	f = po->fanout;
	if (!f)
		return;

	po->fanout = NULL;

	mutex_lock(&fanout_mutex);
	if (atomic_dec_and_test(&f->sk_ref)) {
		list_del(&f->list);
		dev_remove_pack(&f->prot_hook);
		kfree(f);
	}
	mutex_unlock(&fanout_mutex);
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
	nf_reset(skb);

	spkt = &PACKET_SKB_CB(skb)->sa.pkt;

	skb_push(skb, skb->data - skb_mac_header(skb));

	/*
	 *	The SOCK_PACKET socket receives _all_ frames.
	 */

	spkt->spkt_family = dev->type;
	strlcpy(spkt->spkt_device, dev->name, sizeof(spkt->spkt_device));
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


/*
 *	Output a raw packet to a device layer. This bypasses all the other
 *	protocol layers and you must therefore supply it with a complete frame
 */

static int packet_sendmsg_spkt(struct kiocb *iocb, struct socket *sock,
			       struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_pkt *saddr = (struct sockaddr_pkt *)msg->msg_name;
	struct sk_buff *skb = NULL;
	struct net_device *dev;
	__be16 proto = 0;
	int err;

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

	saddr->spkt_device[13] = 0;
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

	err = -EMSGSIZE;
	if (len > dev->mtu + dev->hard_header_len + VLAN_HLEN)
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
		err = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
		if (err)
			goto out_free;
		goto retry;
	}

	if (len > (dev->mtu + dev->hard_header_len)) {
		/* Earlier code assumed this would be a VLAN pkt,
		 * double-check this now that we have the actual
		 * packet in hand.
		 */
		struct ethhdr *ehdr;
		skb_reset_mac_header(skb);
		ehdr = eth_hdr(skb);
		if (ehdr->h_proto != htons(ETH_P_8021Q)) {
			err = -EMSGSIZE;
			goto out_unlock;
		}
	}

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;
	err = sock_tx_timestamp(sk, &skb_shinfo(skb)->tx_flags);
	if (err < 0)
		goto out_unlock;

	dev_queue_xmit(skb);
	rcu_read_unlock();
	return len;

out_unlock:
	rcu_read_unlock();
out_free:
	kfree_skb(skb);
	return err;
}

static unsigned int run_filter(const struct sk_buff *skb,
				      const struct sock *sk,
				      unsigned int res)
{
	struct sk_filter *filter;

	rcu_read_lock();
	filter = rcu_dereference(sk->sk_filter);
	if (filter != NULL)
		res = SK_RUN_FILTER(filter, skb);
	rcu_read_unlock();

	return res;
}

/*
 * This function makes lazy skb cloning in hope that most of packets
 * are discarded by BPF.
 *
 * Note tricky part: we DO mangle shared skb! skb->data, skb->len
 * and skb->cb are mangled. It works because (and until) packets
 * falling here are owned by current CPU. Output packets are cloned
 * by dev_queue_xmit_nit(), input packets are processed by net_bh
 * sequencially, so that if we return skb to original state on exit,
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

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto drop;

	sk = pt->af_packet_priv;
	po = pkt_sk(sk);

	if (!net_eq(dev_net(dev), sock_net(sk)))
		goto drop;

	skb->dev = dev;

	if (dev->header_ops) {
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
		kfree_skb(skb);
		skb = nskb;
	}

	BUILD_BUG_ON(sizeof(*PACKET_SKB_CB(skb)) + MAX_ADDR_LEN - 8 >
		     sizeof(skb->cb));

	sll = &PACKET_SKB_CB(skb)->sa.ll;
	sll->sll_family = AF_PACKET;
	sll->sll_hatype = dev->type;
	sll->sll_protocol = skb->protocol;
	sll->sll_pkttype = skb->pkt_type;
	if (unlikely(po->origdev))
		sll->sll_ifindex = orig_dev->ifindex;
	else
		sll->sll_ifindex = dev->ifindex;

	sll->sll_halen = dev_parse_header(skb, sll->sll_addr);

	PACKET_SKB_CB(skb)->origlen = skb->len;

	if (pskb_trim(skb, snaplen))
		goto drop_n_acct;

	skb_set_owner_r(skb, sk);
	skb->dev = NULL;
	skb_dst_drop(skb);

	/* drop conntrack reference */
	nf_reset(skb);

	spin_lock(&sk->sk_receive_queue.lock);
	po->stats.tp_packets++;
	skb->dropcount = atomic_read(&sk->sk_drops);
	__skb_queue_tail(&sk->sk_receive_queue, skb);
	spin_unlock(&sk->sk_receive_queue.lock);
	sk->sk_data_ready(sk, skb->len);
	return 0;

drop_n_acct:
	spin_lock(&sk->sk_receive_queue.lock);
	po->stats.tp_drops++;
	atomic_inc(&sk->sk_drops);
	spin_unlock(&sk->sk_receive_queue.lock);

drop_n_restore:
	if (skb_head != skb->data && skb_shared(skb)) {
		skb->data = skb_head;
		skb->len = skb_len;
	}
drop:
	consume_skb(skb);
	return 0;
}

static int tpacket_rcv(struct sk_buff *skb, struct net_device *dev,
		       struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct packet_sock *po;
	struct sockaddr_ll *sll;
	union {
		struct tpacket_hdr *h1;
		struct tpacket2_hdr *h2;
		struct tpacket3_hdr *h3;
		void *raw;
	} h;
	u8 *skb_head = skb->data;
	int skb_len = skb->len;
	unsigned int snaplen, res;
	unsigned long status = TP_STATUS_USER;
	unsigned short macoff, netoff, hdrlen;
	struct sk_buff *copy_skb = NULL;
	struct timeval tv;
	struct timespec ts;
	struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);

	if (skb->pkt_type == PACKET_LOOPBACK)
		goto drop;

	sk = pt->af_packet_priv;
	po = pkt_sk(sk);

	if (!net_eq(dev_net(dev), sock_net(sk)))
		goto drop;

	if (dev->header_ops) {
		if (sk->sk_type != SOCK_DGRAM)
			skb_push(skb, skb->data - skb_mac_header(skb));
		else if (skb->pkt_type == PACKET_OUTGOING) {
			/* Special case: outgoing packets have ll header at head */
			skb_pull(skb, skb_network_offset(skb));
		}
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		status |= TP_STATUS_CSUMNOTREADY;

	snaplen = skb->len;

	res = run_filter(skb, sk, snaplen);
	if (!res)
		goto drop_n_restore;
	if (snaplen > res)
		snaplen = res;

	if (sk->sk_type == SOCK_DGRAM) {
		macoff = netoff = TPACKET_ALIGN(po->tp_hdrlen) + 16 +
				  po->tp_reserve;
	} else {
		unsigned maclen = skb_network_offset(skb);
		netoff = TPACKET_ALIGN(po->tp_hdrlen +
				       (maclen < 16 ? 16 : maclen)) +
			po->tp_reserve;
		macoff = netoff - maclen;
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
				if (copy_skb)
					skb_set_owner_r(copy_skb, sk);
			}
			snaplen = po->rx_ring.frame_size - macoff;
			if ((int)snaplen < 0)
				snaplen = 0;
		}
	}
	spin_lock(&sk->sk_receive_queue.lock);
	h.raw = packet_current_rx_frame(po, skb,
					TP_STATUS_KERNEL, (macoff+snaplen));
	if (!h.raw)
		goto ring_is_full;
	if (po->tp_version <= TPACKET_V2) {
		packet_increment_rx_head(po, &po->rx_ring);
	/*
	 * LOSING will be reported till you read the stats,
	 * because it's COR - Clear On Read.
	 * Anyways, moving it for V1/V2 only as V3 doesn't need this
	 * at packet level.
	 */
		if (po->stats.tp_drops)
			status |= TP_STATUS_LOSING;
	}
	po->stats.tp_packets++;
	if (copy_skb) {
		status |= TP_STATUS_COPY;
		__skb_queue_tail(&sk->sk_receive_queue, copy_skb);
	}
	spin_unlock(&sk->sk_receive_queue.lock);

	skb_copy_bits(skb, 0, h.raw + macoff, snaplen);

	switch (po->tp_version) {
	case TPACKET_V1:
		h.h1->tp_len = skb->len;
		h.h1->tp_snaplen = snaplen;
		h.h1->tp_mac = macoff;
		h.h1->tp_net = netoff;
		if ((po->tp_tstamp & SOF_TIMESTAMPING_SYS_HARDWARE)
				&& shhwtstamps->syststamp.tv64)
			tv = ktime_to_timeval(shhwtstamps->syststamp);
		else if ((po->tp_tstamp & SOF_TIMESTAMPING_RAW_HARDWARE)
				&& shhwtstamps->hwtstamp.tv64)
			tv = ktime_to_timeval(shhwtstamps->hwtstamp);
		else if (skb->tstamp.tv64)
			tv = ktime_to_timeval(skb->tstamp);
		else
			do_gettimeofday(&tv);
		h.h1->tp_sec = tv.tv_sec;
		h.h1->tp_usec = tv.tv_usec;
		hdrlen = sizeof(*h.h1);
		break;
	case TPACKET_V2:
		h.h2->tp_len = skb->len;
		h.h2->tp_snaplen = snaplen;
		h.h2->tp_mac = macoff;
		h.h2->tp_net = netoff;
		if ((po->tp_tstamp & SOF_TIMESTAMPING_SYS_HARDWARE)
				&& shhwtstamps->syststamp.tv64)
			ts = ktime_to_timespec(shhwtstamps->syststamp);
		else if ((po->tp_tstamp & SOF_TIMESTAMPING_RAW_HARDWARE)
				&& shhwtstamps->hwtstamp.tv64)
			ts = ktime_to_timespec(shhwtstamps->hwtstamp);
		else if (skb->tstamp.tv64)
			ts = ktime_to_timespec(skb->tstamp);
		else
			getnstimeofday(&ts);
		h.h2->tp_sec = ts.tv_sec;
		h.h2->tp_nsec = ts.tv_nsec;
		if (vlan_tx_tag_present(skb)) {
			h.h2->tp_vlan_tci = vlan_tx_tag_get(skb);
			status |= TP_STATUS_VLAN_VALID;
		} else {
			h.h2->tp_vlan_tci = 0;
		}
		h.h2->tp_padding = 0;
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
		if ((po->tp_tstamp & SOF_TIMESTAMPING_SYS_HARDWARE)
				&& shhwtstamps->syststamp.tv64)
			ts = ktime_to_timespec(shhwtstamps->syststamp);
		else if ((po->tp_tstamp & SOF_TIMESTAMPING_RAW_HARDWARE)
				&& shhwtstamps->hwtstamp.tv64)
			ts = ktime_to_timespec(shhwtstamps->hwtstamp);
		else if (skb->tstamp.tv64)
			ts = ktime_to_timespec(skb->tstamp);
		else
			getnstimeofday(&ts);
		h.h3->tp_sec  = ts.tv_sec;
		h.h3->tp_nsec = ts.tv_nsec;
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
	if (unlikely(po->origdev))
		sll->sll_ifindex = orig_dev->ifindex;
	else
		sll->sll_ifindex = dev->ifindex;

	smp_mb();
#if ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE == 1
	{
		u8 *start, *end;

		if (po->tp_version <= TPACKET_V2) {
			end = (u8 *)PAGE_ALIGN((unsigned long)h.raw
				+ macoff + snaplen);
			for (start = h.raw; start < end; start += PAGE_SIZE)
				flush_dcache_page(pgv_to_page(start));
		}
		smp_wmb();
	}
#endif
	if (po->tp_version <= TPACKET_V2)
		__packet_set_status(po, h.raw, status);
	else
		prb_clear_blk_fill_status(&po->rx_ring);

	sk->sk_data_ready(sk, 0);

drop_n_restore:
	if (skb_head != skb->data && skb_shared(skb)) {
		skb->data = skb_head;
		skb->len = skb_len;
	}
drop:
	kfree_skb(skb);
	return 0;

ring_is_full:
	po->stats.tp_drops++;
	spin_unlock(&sk->sk_receive_queue.lock);

	sk->sk_data_ready(sk, 0);
	kfree_skb(copy_skb);
	goto drop_n_restore;
}

static void tpacket_destruct_skb(struct sk_buff *skb)
{
	struct packet_sock *po = pkt_sk(skb->sk);
	void *ph;

	if (likely(po->tx_ring.pg_vec)) {
		ph = skb_shinfo(skb)->destructor_arg;
		BUG_ON(__packet_get_status(po, ph) != TP_STATUS_SENDING);
		BUG_ON(atomic_read(&po->tx_ring.pending) == 0);
		atomic_dec(&po->tx_ring.pending);
		__packet_set_status(po, ph, TP_STATUS_AVAILABLE);
	}

	sock_wfree(skb);
}

static int tpacket_fill_skb(struct packet_sock *po, struct sk_buff *skb,
		void *frame, struct net_device *dev, int size_max,
		__be16 proto, unsigned char *addr, int hlen)
{
	union {
		struct tpacket_hdr *h1;
		struct tpacket2_hdr *h2;
		void *raw;
	} ph;
	int to_write, offset, len, tp_len, nr_frags, len_max;
	struct socket *sock = po->sk.sk_socket;
	struct page *page;
	void *data;
	int err;

	ph.raw = frame;

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = po->sk.sk_priority;
	skb->mark = po->sk.sk_mark;
	skb_shinfo(skb)->destructor_arg = ph.raw;

	switch (po->tp_version) {
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

	skb_reserve(skb, hlen);
	skb_reset_network_header(skb);

	data = ph.raw + po->tp_hdrlen - sizeof(struct sockaddr_ll);
	to_write = tp_len;

	if (sock->type == SOCK_DGRAM) {
		err = dev_hard_header(skb, dev, ntohs(proto), addr,
				NULL, tp_len);
		if (unlikely(err < 0))
			return -EINVAL;
	} else if (dev->hard_header_len) {
		/* net device doesn't like empty head */
		if (unlikely(tp_len <= dev->hard_header_len)) {
			pr_err("packet size is too short (%d < %d)\n",
			       tp_len, dev->hard_header_len);
			return -EINVAL;
		}

		skb_push(skb, dev->hard_header_len);
		err = skb_store_bits(skb, 0, data,
				dev->hard_header_len);
		if (unlikely(err))
			return err;

		data += dev->hard_header_len;
		to_write -= dev->hard_header_len;
	}

	err = -EFAULT;
	offset = offset_in_page(data);
	len_max = PAGE_SIZE - offset;
	len = ((to_write > len_max) ? len_max : to_write);

	skb->data_len = to_write;
	skb->len += to_write;
	skb->truesize += to_write;
	atomic_add(to_write, &po->sk.sk_wmem_alloc);

	while (likely(to_write)) {
		nr_frags = skb_shinfo(skb)->nr_frags;

		if (unlikely(nr_frags >= MAX_SKB_FRAGS)) {
			pr_err("Packet exceed the number of skb frags(%lu)\n",
			       MAX_SKB_FRAGS);
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

	return tp_len;
}

static int tpacket_snd(struct packet_sock *po, struct msghdr *msg)
{
	struct sk_buff *skb;
	struct net_device *dev;
	__be16 proto;
	bool need_rls_dev = false;
	int err, reserve = 0;
	void *ph;
	struct sockaddr_ll *saddr = (struct sockaddr_ll *)msg->msg_name;
	int tp_len, size_max;
	unsigned char *addr;
	int len_sum = 0;
	int status = 0;
	int hlen, tlen;

	mutex_lock(&po->pg_vec_lock);

	err = -EBUSY;
	if (saddr == NULL) {
		dev = po->prot_hook.dev;
		proto	= po->num;
		addr	= NULL;
	} else {
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(struct sockaddr_ll))
			goto out;
		if (msg->msg_namelen < (saddr->sll_halen
					+ offsetof(struct sockaddr_ll,
						sll_addr)))
			goto out;
		proto	= saddr->sll_protocol;
		addr	= saddr->sll_addr;
		dev = dev_get_by_index(sock_net(&po->sk), saddr->sll_ifindex);
		need_rls_dev = true;
	}

	err = -ENXIO;
	if (unlikely(dev == NULL))
		goto out;

	reserve = dev->hard_header_len;

	err = -ENETDOWN;
	if (unlikely(!(dev->flags & IFF_UP)))
		goto out_put;

	size_max = po->tx_ring.frame_size
		- (po->tp_hdrlen - sizeof(struct sockaddr_ll));

	if (size_max > dev->mtu + reserve)
		size_max = dev->mtu + reserve;

	do {
		ph = packet_current_frame(po, &po->tx_ring,
				TP_STATUS_SEND_REQUEST);

		if (unlikely(ph == NULL)) {
			schedule();
			continue;
		}

		status = TP_STATUS_SEND_REQUEST;
		hlen = LL_RESERVED_SPACE(dev);
		tlen = dev->needed_tailroom;
		skb = sock_alloc_send_skb(&po->sk,
				hlen + tlen + sizeof(struct sockaddr_ll),
				0, &err);

		if (unlikely(skb == NULL))
			goto out_status;

		tp_len = tpacket_fill_skb(po, skb, ph, dev, size_max, proto,
				addr, hlen);

		if (unlikely(tp_len < 0)) {
			if (po->tp_loss) {
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

		skb->destructor = tpacket_destruct_skb;
		__packet_set_status(po, ph, TP_STATUS_SENDING);
		atomic_inc(&po->tx_ring.pending);

		status = TP_STATUS_SEND_REQUEST;
		err = dev_queue_xmit(skb);
		if (unlikely(err > 0)) {
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
			((!(msg->msg_flags & MSG_DONTWAIT)) &&
			 (atomic_read(&po->tx_ring.pending))))
		);

	err = len_sum;
	goto out_put;

out_status:
	__packet_set_status(po, ph, status);
	kfree_skb(skb);
out_put:
	if (need_rls_dev)
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

	skb = sock_alloc_send_pskb(sk, prepad + linear, len - linear, noblock,
				   err);
	if (!skb)
		return NULL;

	skb_reserve(skb, reserve);
	skb_put(skb, linear);
	skb->data_len = len - linear;
	skb->len += len - linear;

	return skb;
}

static int packet_snd(struct socket *sock,
			  struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ll *saddr = (struct sockaddr_ll *)msg->msg_name;
	struct sk_buff *skb;
	struct net_device *dev;
	__be16 proto;
	bool need_rls_dev = false;
	unsigned char *addr;
	int err, reserve = 0;
	struct virtio_net_hdr vnet_hdr = { 0 };
	int offset = 0;
	int vnet_hdr_len;
	struct packet_sock *po = pkt_sk(sk);
	unsigned short gso_type = 0;
	int hlen, tlen;

	/*
	 *	Get and verify the address.
	 */

	if (saddr == NULL) {
		dev = po->prot_hook.dev;
		proto	= po->num;
		addr	= NULL;
	} else {
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(struct sockaddr_ll))
			goto out;
		if (msg->msg_namelen < (saddr->sll_halen + offsetof(struct sockaddr_ll, sll_addr)))
			goto out;
		proto	= saddr->sll_protocol;
		addr	= saddr->sll_addr;
		dev = dev_get_by_index(sock_net(sk), saddr->sll_ifindex);
		need_rls_dev = true;
	}

	err = -ENXIO;
	if (dev == NULL)
		goto out_unlock;
	if (sock->type == SOCK_RAW)
		reserve = dev->hard_header_len;

	err = -ENETDOWN;
	if (!(dev->flags & IFF_UP))
		goto out_unlock;

	if (po->has_vnet_hdr) {
		vnet_hdr_len = sizeof(vnet_hdr);

		err = -EINVAL;
		if (len < vnet_hdr_len)
			goto out_unlock;

		len -= vnet_hdr_len;

		err = memcpy_fromiovec((void *)&vnet_hdr, msg->msg_iov,
				       vnet_hdr_len);
		if (err < 0)
			goto out_unlock;

		if ((vnet_hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) &&
		    (vnet_hdr.csum_start + vnet_hdr.csum_offset + 2 >
		      vnet_hdr.hdr_len))
			vnet_hdr.hdr_len = vnet_hdr.csum_start +
						 vnet_hdr.csum_offset + 2;

		err = -EINVAL;
		if (vnet_hdr.hdr_len > len)
			goto out_unlock;

		if (vnet_hdr.gso_type != VIRTIO_NET_HDR_GSO_NONE) {
			switch (vnet_hdr.gso_type & ~VIRTIO_NET_HDR_GSO_ECN) {
			case VIRTIO_NET_HDR_GSO_TCPV4:
				gso_type = SKB_GSO_TCPV4;
				break;
			case VIRTIO_NET_HDR_GSO_TCPV6:
				gso_type = SKB_GSO_TCPV6;
				break;
			case VIRTIO_NET_HDR_GSO_UDP:
				gso_type = SKB_GSO_UDP;
				break;
			default:
				goto out_unlock;
			}

			if (vnet_hdr.gso_type & VIRTIO_NET_HDR_GSO_ECN)
				gso_type |= SKB_GSO_TCP_ECN;

			if (vnet_hdr.gso_size == 0)
				goto out_unlock;

		}
	}

	err = -EMSGSIZE;
	if (!gso_type && (len > dev->mtu + reserve + VLAN_HLEN))
		goto out_unlock;

	err = -ENOBUFS;
	hlen = LL_RESERVED_SPACE(dev);
	tlen = dev->needed_tailroom;
	skb = packet_alloc_skb(sk, hlen + tlen, hlen, len, vnet_hdr.hdr_len,
			       msg->msg_flags & MSG_DONTWAIT, &err);
	if (skb == NULL)
		goto out_unlock;

	skb_set_network_header(skb, reserve);

	err = -EINVAL;
	if (sock->type == SOCK_DGRAM &&
	    (offset = dev_hard_header(skb, dev, ntohs(proto), addr, NULL, len)) < 0)
		goto out_free;

	/* Returns -EFAULT on error */
	err = skb_copy_datagram_from_iovec(skb, offset, msg->msg_iov, 0, len);
	if (err)
		goto out_free;
	err = sock_tx_timestamp(sk, &skb_shinfo(skb)->tx_flags);
	if (err < 0)
		goto out_free;

	if (!gso_type && (len > dev->mtu + reserve)) {
		/* Earlier code assumed this would be a VLAN pkt,
		 * double-check this now that we have the actual
		 * packet in hand.
		 */
		struct ethhdr *ehdr;
		skb_reset_mac_header(skb);
		ehdr = eth_hdr(skb);
		if (ehdr->h_proto != htons(ETH_P_8021Q)) {
			err = -EMSGSIZE;
			goto out_free;
		}
	}

	skb->protocol = proto;
	skb->dev = dev;
	skb->priority = sk->sk_priority;
	skb->mark = sk->sk_mark;

	if (po->has_vnet_hdr) {
		if (vnet_hdr.flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) {
			if (!skb_partial_csum_set(skb, vnet_hdr.csum_start,
						  vnet_hdr.csum_offset)) {
				err = -EINVAL;
				goto out_free;
			}
		}

		skb_shinfo(skb)->gso_size = vnet_hdr.gso_size;
		skb_shinfo(skb)->gso_type = gso_type;

		/* Header must be checked, and gso_segs computed. */
		skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
		skb_shinfo(skb)->gso_segs = 0;

		len += vnet_hdr_len;
	}

	/*
	 *	Now send it
	 */

	err = dev_queue_xmit(skb);
	if (err > 0 && (err = net_xmit_errno(err)) != 0)
		goto out_unlock;

	if (need_rls_dev)
		dev_put(dev);

	return len;

out_free:
	kfree_skb(skb);
out_unlock:
	if (dev && need_rls_dev)
		dev_put(dev);
out:
	return err;
}

static int packet_sendmsg(struct kiocb *iocb, struct socket *sock,
		struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	if (po->tx_ring.pg_vec)
		return tpacket_snd(po, msg);
	else
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
	struct net *net;
	union tpacket_req_u req_u;

	if (!sk)
		return 0;

	net = sock_net(sk);
	po = pkt_sk(sk);

	spin_lock_bh(&net->packet.sklist_lock);
	sk_del_node_init_rcu(sk);
	sock_prot_inuse_add(net, sk->sk_prot, -1);
	spin_unlock_bh(&net->packet.sklist_lock);

	spin_lock(&po->bind_lock);
	unregister_prot_hook(sk, false);
	if (po->prot_hook.dev) {
		dev_put(po->prot_hook.dev);
		po->prot_hook.dev = NULL;
	}
	spin_unlock(&po->bind_lock);

	packet_flush_mclist(sk);

	memset(&req_u, 0, sizeof(req_u));

	if (po->rx_ring.pg_vec)
		packet_set_ring(sk, &req_u, 1, 0);

	if (po->tx_ring.pg_vec)
		packet_set_ring(sk, &req_u, 1, 1);

	fanout_release(sk);

	synchronize_net();
	/*
	 *	Now the socket is dead. No more input will appear.
	 */
	sock_orphan(sk);
	sock->sk = NULL;

	/* Purge queues */

	skb_queue_purge(&sk->sk_receive_queue);
	sk_refcnt_debug_release(sk);

	sock_put(sk);
	return 0;
}

/*
 *	Attach a packet hook.
 */

static int packet_do_bind(struct sock *sk, struct net_device *dev, __be16 protocol)
{
	struct packet_sock *po = pkt_sk(sk);

	if (po->fanout)
		return -EINVAL;

	lock_sock(sk);

	spin_lock(&po->bind_lock);
	unregister_prot_hook(sk, true);
	po->num = protocol;
	po->prot_hook.type = protocol;
	if (po->prot_hook.dev)
		dev_put(po->prot_hook.dev);
	po->prot_hook.dev = dev;

	po->ifindex = dev ? dev->ifindex : 0;

	if (protocol == 0)
		goto out_unlock;

	if (!dev || (dev->flags & IFF_UP)) {
		register_prot_hook(sk);
	} else {
		sk->sk_err = ENETDOWN;
		if (!sock_flag(sk, SOCK_DEAD))
			sk->sk_error_report(sk);
	}

out_unlock:
	spin_unlock(&po->bind_lock);
	release_sock(sk);
	return 0;
}

/*
 *	Bind a packet socket to a device
 */

static int packet_bind_spkt(struct socket *sock, struct sockaddr *uaddr,
			    int addr_len)
{
	struct sock *sk = sock->sk;
	char name[15];
	struct net_device *dev;
	int err = -ENODEV;

	/*
	 *	Check legality
	 */

	if (addr_len != sizeof(struct sockaddr))
		return -EINVAL;
	strlcpy(name, uaddr->sa_data, sizeof(name));

	dev = dev_get_by_name(sock_net(sk), name);
	if (dev)
		err = packet_do_bind(sk, dev, pkt_sk(sk)->num);
	return err;
}

static int packet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_ll *sll = (struct sockaddr_ll *)uaddr;
	struct sock *sk = sock->sk;
	struct net_device *dev = NULL;
	int err;


	/*
	 *	Check legality
	 */

	if (addr_len < sizeof(struct sockaddr_ll))
		return -EINVAL;
	if (sll->sll_family != AF_PACKET)
		return -EINVAL;

	if (sll->sll_ifindex) {
		err = -ENODEV;
		dev = dev_get_by_index(sock_net(sk), sll->sll_ifindex);
		if (dev == NULL)
			goto out;
	}
	err = packet_do_bind(sk, dev, sll->sll_protocol ? : pkt_sk(sk)->num);

out:
	return err;
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

	if (!capable(CAP_NET_RAW))
		return -EPERM;
	if (sock->type != SOCK_DGRAM && sock->type != SOCK_RAW &&
	    sock->type != SOCK_PACKET)
		return -ESOCKTNOSUPPORT;

	sock->state = SS_UNCONNECTED;

	err = -ENOBUFS;
	sk = sk_alloc(net, PF_PACKET, GFP_KERNEL, &packet_proto);
	if (sk == NULL)
		goto out;

	sock->ops = &packet_ops;
	if (sock->type == SOCK_PACKET)
		sock->ops = &packet_ops_spkt;

	sock_init_data(sock, sk);

	po = pkt_sk(sk);
	sk->sk_family = PF_PACKET;
	po->num = proto;

	sk->sk_destruct = packet_sock_destruct;
	sk_refcnt_debug_inc(sk);

	/*
	 *	Attach a protocol block
	 */

	spin_lock_init(&po->bind_lock);
	mutex_init(&po->pg_vec_lock);
	po->prot_hook.func = packet_rcv;

	if (sock->type == SOCK_PACKET)
		po->prot_hook.func = packet_rcv_spkt;

	po->prot_hook.af_packet_priv = sk;

	if (proto) {
		po->prot_hook.type = proto;
		register_prot_hook(sk);
	}

	spin_lock_bh(&net->packet.sklist_lock);
	sk_add_node_rcu(sk, &net->packet.sklist);
	sock_prot_inuse_add(net, &packet_proto, 1);
	spin_unlock_bh(&net->packet.sklist_lock);

	return 0;
out:
	return err;
}

static int packet_recv_error(struct sock *sk, struct msghdr *msg, int len)
{
	struct sock_exterr_skb *serr;
	struct sk_buff *skb, *skb2;
	int copied, err;

	err = -EAGAIN;
	skb = skb_dequeue(&sk->sk_error_queue);
	if (skb == NULL)
		goto out;

	copied = skb->len;
	if (copied > len) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}
	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto out_free_skb;

	sock_recv_timestamp(msg, sk, skb);

	serr = SKB_EXT_ERR(skb);
	put_cmsg(msg, SOL_PACKET, PACKET_TX_TIMESTAMP,
		 sizeof(serr->ee), &serr->ee);

	msg->msg_flags |= MSG_ERRQUEUE;
	err = copied;

	/* Reset and regenerate socket error */
	spin_lock_bh(&sk->sk_error_queue.lock);
	sk->sk_err = 0;
	if ((skb2 = skb_peek(&sk->sk_error_queue)) != NULL) {
		sk->sk_err = SKB_EXT_ERR(skb2)->ee.ee_errno;
		spin_unlock_bh(&sk->sk_error_queue.lock);
		sk->sk_error_report(sk);
	} else
		spin_unlock_bh(&sk->sk_error_queue.lock);

out_free_skb:
	kfree_skb(skb);
out:
	return err;
}

/*
 *	Pull a packet from our receive queue and hand it to the user.
 *	If necessary we block.
 */

static int packet_recvmsg(struct kiocb *iocb, struct socket *sock,
			  struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int copied, err;
	struct sockaddr_ll *sll;
	int vnet_hdr_len = 0;

	err = -EINVAL;
	if (flags & ~(MSG_PEEK|MSG_DONTWAIT|MSG_TRUNC|MSG_CMSG_COMPAT|MSG_ERRQUEUE))
		goto out;

#if 0
	/* What error should we return now? EUNATTACH? */
	if (pkt_sk(sk)->ifindex < 0)
		return -ENODEV;
#endif

	if (flags & MSG_ERRQUEUE) {
		err = packet_recv_error(sk, msg, len);
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

	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &err);

	/*
	 *	An error occurred so return it. Because skb_recv_datagram()
	 *	handles the blocking we don't see and worry about blocking
	 *	retries.
	 */

	if (skb == NULL)
		goto out;

	if (pkt_sk(sk)->has_vnet_hdr) {
		struct virtio_net_hdr vnet_hdr = { 0 };

		err = -EINVAL;
		vnet_hdr_len = sizeof(vnet_hdr);
		if (len < vnet_hdr_len)
			goto out_free;

		len -= vnet_hdr_len;

		if (skb_is_gso(skb)) {
			struct skb_shared_info *sinfo = skb_shinfo(skb);

			/* This is a hint as to how much should be linear. */
			vnet_hdr.hdr_len = skb_headlen(skb);
			vnet_hdr.gso_size = sinfo->gso_size;
			if (sinfo->gso_type & SKB_GSO_TCPV4)
				vnet_hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
			else if (sinfo->gso_type & SKB_GSO_TCPV6)
				vnet_hdr.gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
			else if (sinfo->gso_type & SKB_GSO_UDP)
				vnet_hdr.gso_type = VIRTIO_NET_HDR_GSO_UDP;
			else if (sinfo->gso_type & SKB_GSO_FCOE)
				goto out_free;
			else
				BUG();
			if (sinfo->gso_type & SKB_GSO_TCP_ECN)
				vnet_hdr.gso_type |= VIRTIO_NET_HDR_GSO_ECN;
		} else
			vnet_hdr.gso_type = VIRTIO_NET_HDR_GSO_NONE;

		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			vnet_hdr.flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
			vnet_hdr.csum_start = skb_checksum_start_offset(skb);
			vnet_hdr.csum_offset = skb->csum_offset;
		} else if (skb->ip_summed == CHECKSUM_UNNECESSARY) {
			vnet_hdr.flags = VIRTIO_NET_HDR_F_DATA_VALID;
		} /* else everything is zero */

		err = memcpy_toiovec(msg->msg_iov, (void *)&vnet_hdr,
				     vnet_hdr_len);
		if (err < 0)
			goto out_free;
	}

	/*
	 *	If the address length field is there to be filled in, we fill
	 *	it in now.
	 */

	sll = &PACKET_SKB_CB(skb)->sa.ll;
	if (sock->type == SOCK_PACKET)
		msg->msg_namelen = sizeof(struct sockaddr_pkt);
	else
		msg->msg_namelen = sll->sll_halen + offsetof(struct sockaddr_ll, sll_addr);

	/*
	 *	You lose any data beyond the buffer you gave. If it worries a
	 *	user program they can ask the device for its MTU anyway.
	 */

	copied = skb->len;
	if (copied > len) {
		copied = len;
		msg->msg_flags |= MSG_TRUNC;
	}

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto out_free;

	sock_recv_ts_and_drops(msg, sk, skb);

	if (msg->msg_name)
		memcpy(msg->msg_name, &PACKET_SKB_CB(skb)->sa,
		       msg->msg_namelen);

	if (pkt_sk(sk)->auxdata) {
		struct tpacket_auxdata aux;

		aux.tp_status = TP_STATUS_USER;
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			aux.tp_status |= TP_STATUS_CSUMNOTREADY;
		aux.tp_len = PACKET_SKB_CB(skb)->origlen;
		aux.tp_snaplen = skb->len;
		aux.tp_mac = 0;
		aux.tp_net = skb_network_offset(skb);
		if (vlan_tx_tag_present(skb)) {
			aux.tp_vlan_tci = vlan_tx_tag_get(skb);
			aux.tp_status |= TP_STATUS_VLAN_VALID;
		} else {
			aux.tp_vlan_tci = 0;
		}
		aux.tp_padding = 0;
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
			       int *uaddr_len, int peer)
{
	struct net_device *dev;
	struct sock *sk	= sock->sk;

	if (peer)
		return -EOPNOTSUPP;

	uaddr->sa_family = AF_PACKET;
	rcu_read_lock();
	dev = dev_get_by_index_rcu(sock_net(sk), pkt_sk(sk)->ifindex);
	if (dev)
		strncpy(uaddr->sa_data, dev->name, 14);
	else
		memset(uaddr->sa_data, 0, 14);
	rcu_read_unlock();
	*uaddr_len = sizeof(*uaddr);

	return 0;
}

static int packet_getname(struct socket *sock, struct sockaddr *uaddr,
			  int *uaddr_len, int peer)
{
	struct net_device *dev;
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_ll *, sll, uaddr);

	if (peer)
		return -EOPNOTSUPP;

	sll->sll_family = AF_PACKET;
	sll->sll_ifindex = po->ifindex;
	sll->sll_protocol = po->num;
	sll->sll_pkttype = 0;
	rcu_read_lock();
	dev = dev_get_by_index_rcu(sock_net(sk), po->ifindex);
	if (dev) {
		sll->sll_hatype = dev->type;
		sll->sll_halen = dev->addr_len;
		memcpy(sll->sll_addr, dev->dev_addr, dev->addr_len);
	} else {
		sll->sll_hatype = 0;	/* Bad: we have no ARPHRD_UNSPEC */
		sll->sll_halen = 0;
	}
	rcu_read_unlock();
	*uaddr_len = offsetof(struct sockaddr_ll, sll_addr) + sll->sll_halen;

	return 0;
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
		break;
	case PACKET_MR_ALLMULTI:
		return dev_set_allmulti(dev, what);
		break;
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

static void packet_dev_mclist(struct net_device *dev, struct packet_mclist *i, int what)
{
	for ( ; i; i = i->next) {
		if (i->ifindex == dev->ifindex)
			packet_dev_mc(dev, i, what);
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
			rtnl_unlock();
			return 0;
		}
	}
	rtnl_unlock();
	return -EADDRNOTAVAIL;
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
packet_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int optlen)
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
		if (copy_from_user(&mreq, optval, len))
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
		if (optlen < len)
			return -EINVAL;
		if (pkt_sk(sk)->has_vnet_hdr)
			return -EINVAL;
		if (copy_from_user(&req_u.req, optval, len))
			return -EFAULT;
		return packet_set_ring(sk, &req_u, 0,
			optname == PACKET_TX_RING);
	}
	case PACKET_COPY_THRESH:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;

		pkt_sk(sk)->copy_thresh = val;
		return 0;
	}
	case PACKET_VERSION:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec)
			return -EBUSY;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;
		switch (val) {
		case TPACKET_V1:
		case TPACKET_V2:
		case TPACKET_V3:
			po->tp_version = val;
			return 0;
		default:
			return -EINVAL;
		}
	}
	case PACKET_RESERVE:
	{
		unsigned int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec)
			return -EBUSY;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;
		po->tp_reserve = val;
		return 0;
	}
	case PACKET_LOSS:
	{
		unsigned int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec)
			return -EBUSY;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;
		po->tp_loss = !!val;
		return 0;
	}
	case PACKET_AUXDATA:
	{
		int val;

		if (optlen < sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;

		po->auxdata = !!val;
		return 0;
	}
	case PACKET_ORIGDEV:
	{
		int val;

		if (optlen < sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;

		po->origdev = !!val;
		return 0;
	}
	case PACKET_VNET_HDR:
	{
		int val;

		if (sock->type != SOCK_RAW)
			return -EINVAL;
		if (po->rx_ring.pg_vec || po->tx_ring.pg_vec)
			return -EBUSY;
		if (optlen < sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;

		po->has_vnet_hdr = !!val;
		return 0;
	}
	case PACKET_TIMESTAMP:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;

		po->tp_tstamp = val;
		return 0;
	}
	case PACKET_FANOUT:
	{
		int val;

		if (optlen != sizeof(val))
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)))
			return -EFAULT;

		return fanout_add(sk, val & 0xffff, val >> 16);
	}
	default:
		return -ENOPROTOOPT;
	}
}

static int packet_getsockopt(struct socket *sock, int level, int optname,
			     char __user *optval, int __user *optlen)
{
	int len;
	int val;
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	void *data;
	struct tpacket_stats st;
	union tpacket_stats_u st_u;

	if (level != SOL_PACKET)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case PACKET_STATISTICS:
		if (po->tp_version == TPACKET_V3) {
			len = sizeof(struct tpacket_stats_v3);
		} else {
			if (len > sizeof(struct tpacket_stats))
				len = sizeof(struct tpacket_stats);
		}
		spin_lock_bh(&sk->sk_receive_queue.lock);
		if (po->tp_version == TPACKET_V3) {
			memcpy(&st_u.stats3, &po->stats,
			sizeof(struct tpacket_stats));
			st_u.stats3.tp_freeze_q_cnt =
			po->stats_u.stats3.tp_freeze_q_cnt;
			st_u.stats3.tp_packets += po->stats.tp_drops;
			data = &st_u.stats3;
		} else {
			st = po->stats;
			st.tp_packets += st.tp_drops;
			data = &st;
		}
		memset(&po->stats, 0, sizeof(st));
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		break;
	case PACKET_AUXDATA:
		if (len > sizeof(int))
			len = sizeof(int);
		val = po->auxdata;

		data = &val;
		break;
	case PACKET_ORIGDEV:
		if (len > sizeof(int))
			len = sizeof(int);
		val = po->origdev;

		data = &val;
		break;
	case PACKET_VNET_HDR:
		if (len > sizeof(int))
			len = sizeof(int);
		val = po->has_vnet_hdr;

		data = &val;
		break;
	case PACKET_VERSION:
		if (len > sizeof(int))
			len = sizeof(int);
		val = po->tp_version;
		data = &val;
		break;
	case PACKET_HDRLEN:
		if (len > sizeof(int))
			len = sizeof(int);
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
		data = &val;
		break;
	case PACKET_RESERVE:
		if (len > sizeof(unsigned int))
			len = sizeof(unsigned int);
		val = po->tp_reserve;
		data = &val;
		break;
	case PACKET_LOSS:
		if (len > sizeof(unsigned int))
			len = sizeof(unsigned int);
		val = po->tp_loss;
		data = &val;
		break;
	case PACKET_TIMESTAMP:
		if (len > sizeof(int))
			len = sizeof(int);
		val = po->tp_tstamp;
		data = &val;
		break;
	case PACKET_FANOUT:
		if (len > sizeof(int))
			len = sizeof(int);
		val = (po->fanout ?
		       ((u32)po->fanout->id |
			((u32)po->fanout->type << 16)) :
		       0);
		data = &val;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, data, len))
		return -EFAULT;
	return 0;
}


static int packet_notifier(struct notifier_block *this, unsigned long msg, void *data)
{
	struct sock *sk;
	struct hlist_node *node;
	struct net_device *dev = data;
	struct net *net = dev_net(dev);

	rcu_read_lock();
	sk_for_each_rcu(sk, node, &net->packet.sklist) {
		struct packet_sock *po = pkt_sk(sk);

		switch (msg) {
		case NETDEV_UNREGISTER:
			if (po->mclist)
				packet_dev_mclist(dev, po->mclist, -1);
			/* fallthrough */

		case NETDEV_DOWN:
			if (dev->ifindex == po->ifindex) {
				spin_lock(&po->bind_lock);
				if (po->running) {
					__unregister_prot_hook(sk, false);
					sk->sk_err = ENETDOWN;
					if (!sock_flag(sk, SOCK_DEAD))
						sk->sk_error_report(sk);
				}
				if (msg == NETDEV_UNREGISTER) {
					po->ifindex = -1;
					if (po->prot_hook.dev)
						dev_put(po->prot_hook.dev);
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
	case SIOCGSTAMP:
		return sock_get_timestamp(sk, (struct timeval __user *)arg);
	case SIOCGSTAMPNS:
		return sock_get_timestampns(sk, (struct timespec __user *)arg);

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

static unsigned int packet_poll(struct file *file, struct socket *sock,
				poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct packet_sock *po = pkt_sk(sk);
	unsigned int mask = datagram_poll(file, sock, wait);

	spin_lock_bh(&sk->sk_receive_queue.lock);
	if (po->rx_ring.pg_vec) {
		if (!packet_previous_rx_frame(po, &po->rx_ring,
			TP_STATUS_KERNEL))
			mask |= POLLIN | POLLRDNORM;
	}
	spin_unlock_bh(&sk->sk_receive_queue.lock);
	spin_lock_bh(&sk->sk_write_queue.lock);
	if (po->tx_ring.pg_vec) {
		if (packet_current_frame(po, &po->tx_ring, TP_STATUS_AVAILABLE))
			mask |= POLLOUT | POLLWRNORM;
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
	char *buffer = NULL;
	gfp_t gfp_flags = GFP_KERNEL | __GFP_COMP |
			  __GFP_ZERO | __GFP_NOWARN | __GFP_NORETRY;

	buffer = (char *) __get_free_pages(gfp_flags, order);

	if (buffer)
		return buffer;

	/*
	 * __get_free_pages failed, fall back to vmalloc
	 */
	buffer = vzalloc((1 << order) * PAGE_SIZE);

	if (buffer)
		return buffer;

	/*
	 * vmalloc failed, lets dig into swap here
	 */
	gfp_flags &= ~__GFP_NORETRY;
	buffer = (char *)__get_free_pages(gfp_flags, order);
	if (buffer)
		return buffer;

	/*
	 * complete and utter failure
	 */
	return NULL;
}

static struct pgv *alloc_pg_vec(struct tpacket_req *req, int order)
{
	unsigned int block_nr = req->tp_block_nr;
	struct pgv *pg_vec;
	int i;

	pg_vec = kcalloc(block_nr, sizeof(struct pgv), GFP_KERNEL);
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
	int was_running, order = 0;
	struct packet_ring_buffer *rb;
	struct sk_buff_head *rb_queue;
	__be16 num;
	int err = -EINVAL;
	/* Added to avoid minimal code churn */
	struct tpacket_req *req = &req_u->req;

	/* Opening a Tx-ring is NOT supported in TPACKET_V3 */
	if (!closing && tx_ring && (po->tp_version > TPACKET_V2)) {
		WARN(1, "Tx-ring is not supported.\n");
		goto out;
	}

	rb = tx_ring ? &po->tx_ring : &po->rx_ring;
	rb_queue = tx_ring ? &sk->sk_write_queue : &sk->sk_receive_queue;

	err = -EBUSY;
	if (!closing) {
		if (atomic_read(&po->mapped))
			goto out;
		if (atomic_read(&rb->pending))
			goto out;
	}

	if (req->tp_block_nr) {
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
		if (unlikely(req->tp_block_size & (PAGE_SIZE - 1)))
			goto out;
		if (unlikely(req->tp_frame_size < po->tp_hdrlen +
					po->tp_reserve))
			goto out;
		if (unlikely(req->tp_frame_size & (TPACKET_ALIGNMENT - 1)))
			goto out;

		rb->frames_per_block = req->tp_block_size/req->tp_frame_size;
		if (unlikely(rb->frames_per_block <= 0))
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
		/* Transmit path is not supported. We checked
		 * it above but just being paranoid
		 */
			if (!tx_ring)
				init_prb_bdqc(po, rb, pg_vec, req_u, tx_ring);
				break;
		default:
			break;
		}
	}
	/* Done */
	else {
		err = -EINVAL;
		if (unlikely(req->tp_frame_nr))
			goto out;
	}

	lock_sock(sk);

	/* Detach socket from network */
	spin_lock(&po->bind_lock);
	was_running = po->running;
	num = po->num;
	if (was_running) {
		po->num = 0;
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
		po->num = num;
		register_prot_hook(sk);
	}
	spin_unlock(&po->bind_lock);
	if (closing && (po->tp_version > TPACKET_V2)) {
		/* Because we don't support block-based V3 on tx-ring */
		if (!tx_ring)
			prb_shutdown_retire_blk_timer(po, tx_ring, rb_queue);
	}
	release_sock(sk);

	if (pg_vec)
		free_pg_vec(pg_vec, order, req->tp_block_nr);
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
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	sock_no_setsockopt,
	.getsockopt =	sock_no_getsockopt,
	.sendmsg =	packet_sendmsg_spkt,
	.recvmsg =	packet_recvmsg,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
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
	.listen =	sock_no_listen,
	.shutdown =	sock_no_shutdown,
	.setsockopt =	packet_setsockopt,
	.getsockopt =	packet_getsockopt,
	.sendmsg =	packet_sendmsg,
	.recvmsg =	packet_recvmsg,
	.mmap =		packet_mmap,
	.sendpage =	sock_no_sendpage,
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
		seq_puts(seq, "sk       RefCnt Type Proto  Iface R Rmem   User   Inode\n");
	else {
		struct sock *s = sk_entry(v);
		const struct packet_sock *po = pkt_sk(s);

		seq_printf(seq,
			   "%pK %-6d %-4d %04x   %-5d %1d %-6u %-6u %-6lu\n",
			   s,
			   atomic_read(&s->sk_refcnt),
			   s->sk_type,
			   ntohs(po->num),
			   po->ifindex,
			   po->running,
			   atomic_read(&s->sk_rmem_alloc),
			   sock_i_uid(s),
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

static int packet_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &packet_seq_ops,
			    sizeof(struct seq_net_private));
}

static const struct file_operations packet_seq_fops = {
	.owner		= THIS_MODULE,
	.open		= packet_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
};

#endif

static int __net_init packet_net_init(struct net *net)
{
	spin_lock_init(&net->packet.sklist_lock);
	INIT_HLIST_HEAD(&net->packet.sklist);

	if (!proc_net_fops_create(net, "packet", 0, &packet_seq_fops))
		return -ENOMEM;

	return 0;
}

static void __net_exit packet_net_exit(struct net *net)
{
	proc_net_remove(net, "packet");
}

static struct pernet_operations packet_net_ops = {
	.init = packet_net_init,
	.exit = packet_net_exit,
};


static void __exit packet_exit(void)
{
	unregister_netdevice_notifier(&packet_netdev_notifier);
	unregister_pernet_subsys(&packet_net_ops);
	sock_unregister(PF_PACKET);
	proto_unregister(&packet_proto);
}

static int __init packet_init(void)
{
	int rc = proto_register(&packet_proto, 0);

	if (rc != 0)
		goto out;

	sock_register(&packet_family_ops);
	register_pernet_subsys(&packet_net_ops);
	register_netdevice_notifier(&packet_netdev_notifier);
out:
	return rc;
}

module_init(packet_init);
module_exit(packet_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_PACKET);
