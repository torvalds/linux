
/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Network Services Protocol (Output)
 *
 * Author:      Eduardo Marcelo Serrat <emserrat@geocities.com>
 *
 * Changes:
 *
 *    Steve Whitehouse:  Split into dn_nsp_in.c and dn_nsp_out.c from
 *                       original dn_nsp.c.
 *    Steve Whitehouse:  Updated to work with my new routing architecture.
 *    Steve Whitehouse:  Added changes from Eduardo Serrat's patches.
 *    Steve Whitehouse:  Now conninits have the "return" bit set.
 *    Steve Whitehouse:  Fixes to check alloc'd skbs are non NULL!
 *                       Moved output state machine into one function
 *    Steve Whitehouse:  New output state machine
 *         Paul Koning:  Connect Confirm message fix.
 *      Eduardo Serrat:  Fix to stop dn_nsp_do_disc() sending malformed packets.
 *    Steve Whitehouse:  dn_nsp_output() and friends needed a spring clean
 *    Steve Whitehouse:  Moved dn_nsp_send() in here from route.h
 */

/******************************************************************************
    (c) 1995-1998 E.M. Serrat		emserrat@geocities.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*******************************************************************************/

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <net/sock.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/termios.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/if_packet.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/dn.h>
#include <net/dn_nsp.h>
#include <net/dn_dev.h>
#include <net/dn_route.h>


static int nsp_backoff[NSP_MAXRXTSHIFT + 1] = { 1, 2, 4, 8, 16, 32, 64, 64, 64, 64, 64, 64, 64 };

static void dn_nsp_send(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct dn_scp *scp = DN_SK(sk);
	struct dst_entry *dst;
	struct flowi fl;

	skb->h.raw = skb->data;
	scp->stamp = jiffies;

	dst = sk_dst_check(sk, 0);
	if (dst) {
try_again:
		skb->dst = dst;
		dst_output(skb);
		return;
	}

	memset(&fl, 0, sizeof(fl));
	fl.oif = sk->sk_bound_dev_if;
	fl.fld_src = dn_saddr2dn(&scp->addr);
	fl.fld_dst = dn_saddr2dn(&scp->peer);
	dn_sk_ports_copy(&fl, scp);
	fl.proto = DNPROTO_NSP;
	if (dn_route_output_sock(&sk->sk_dst_cache, &fl, sk, 0) == 0) {
		dst = sk_dst_get(sk);
		sk->sk_route_caps = dst->dev->features;
		goto try_again;
	}

	sk->sk_err = EHOSTUNREACH;
	if (!sock_flag(sk, SOCK_DEAD))
		sk->sk_state_change(sk);
}


/*
 * If sk == NULL, then we assume that we are supposed to be making
 * a routing layer skb. If sk != NULL, then we are supposed to be
 * creating an skb for the NSP layer.
 *
 * The eventual aim is for each socket to have a cached header size
 * for its outgoing packets, and to set hdr from this when sk != NULL.
 */
struct sk_buff *dn_alloc_skb(struct sock *sk, int size, gfp_t pri)
{
	struct sk_buff *skb;
	int hdr = 64;

	if ((skb = alloc_skb(size + hdr, pri)) == NULL)
		return NULL;

	skb->protocol = __constant_htons(ETH_P_DNA_RT);
	skb->pkt_type = PACKET_OUTGOING;

	if (sk)
		skb_set_owner_w(skb, sk);

	skb_reserve(skb, hdr);

	return skb;
}

/*
 * Calculate persist timer based upon the smoothed round
 * trip time and the variance. Backoff according to the
 * nsp_backoff[] array.
 */
unsigned long dn_nsp_persist(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	unsigned long t = ((scp->nsp_srtt >> 2) + scp->nsp_rttvar) >> 1;

	t *= nsp_backoff[scp->nsp_rxtshift];

	if (t < HZ) t = HZ;
	if (t > (600*HZ)) t = (600*HZ);

	if (scp->nsp_rxtshift < NSP_MAXRXTSHIFT)
		scp->nsp_rxtshift++;

	/* printk(KERN_DEBUG "rxtshift %lu, t=%lu\n", scp->nsp_rxtshift, t); */

	return t;
}

/*
 * This is called each time we get an estimate for the rtt
 * on the link.
 */
static void dn_nsp_rtt(struct sock *sk, long rtt)
{
	struct dn_scp *scp = DN_SK(sk);
	long srtt = (long)scp->nsp_srtt;
	long rttvar = (long)scp->nsp_rttvar;
	long delta;

	/*
	 * If the jiffies clock flips over in the middle of timestamp
	 * gathering this value might turn out negative, so we make sure
	 * that is it always positive here.
	 */
	if (rtt < 0)
		rtt = -rtt;
	/*
	 * Add new rtt to smoothed average
	 */
	delta = ((rtt << 3) - srtt);
	srtt += (delta >> 3);
	if (srtt >= 1)
		scp->nsp_srtt = (unsigned long)srtt;
	else
		scp->nsp_srtt = 1;

	/*
	 * Add new rtt varience to smoothed varience
	 */
	delta >>= 1;
	rttvar += ((((delta>0)?(delta):(-delta)) - rttvar) >> 2);
	if (rttvar >= 1)
		scp->nsp_rttvar = (unsigned long)rttvar;
	else
		scp->nsp_rttvar = 1;

	/* printk(KERN_DEBUG "srtt=%lu rttvar=%lu\n", scp->nsp_srtt, scp->nsp_rttvar); */
}

/**
 * dn_nsp_clone_and_send - Send a data packet by cloning it
 * @skb: The packet to clone and transmit
 * @gfp: memory allocation flag
 *
 * Clone a queued data or other data packet and transmit it.
 *
 * Returns: The number of times the packet has been sent previously
 */
static inline unsigned dn_nsp_clone_and_send(struct sk_buff *skb,
					     gfp_t gfp)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	struct sk_buff *skb2;
	int ret = 0;

	if ((skb2 = skb_clone(skb, gfp)) != NULL) {
		ret = cb->xmit_count;
		cb->xmit_count++;
		cb->stamp = jiffies;
		skb2->sk = skb->sk;
		dn_nsp_send(skb2);
	}

	return ret;
}

/**
 * dn_nsp_output - Try and send something from socket queues
 * @sk: The socket whose queues are to be investigated
 * @gfp: The memory allocation flags
 *
 * Try and send the packet on the end of the data and other data queues.
 * Other data gets priority over data, and if we retransmit a packet we
 * reduce the window by dividing it in two.
 *
 */
void dn_nsp_output(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);
	struct sk_buff *skb;
	unsigned reduce_win = 0;

	/*
	 * First we check for otherdata/linkservice messages
	 */
	if ((skb = skb_peek(&scp->other_xmit_queue)) != NULL)
		reduce_win = dn_nsp_clone_and_send(skb, GFP_ATOMIC);

	/*
	 * If we may not send any data, we don't.
	 * If we are still trying to get some other data down the
	 * channel, we don't try and send any data.
	 */
	if (reduce_win || (scp->flowrem_sw != DN_SEND))
		goto recalc_window;

	if ((skb = skb_peek(&scp->data_xmit_queue)) != NULL)
		reduce_win = dn_nsp_clone_and_send(skb, GFP_ATOMIC);

	/*
	 * If we've sent any frame more than once, we cut the
	 * send window size in half. There is always a minimum
	 * window size of one available.
	 */
recalc_window:
	if (reduce_win) {
		scp->snd_window >>= 1;
		if (scp->snd_window < NSP_MIN_WINDOW)
			scp->snd_window = NSP_MIN_WINDOW;
	}
}

int dn_nsp_xmit_timeout(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	dn_nsp_output(sk);

	if (!skb_queue_empty(&scp->data_xmit_queue) ||
	    !skb_queue_empty(&scp->other_xmit_queue))
		scp->persist = dn_nsp_persist(sk);

	return 0;
}

static inline __le16 *dn_mk_common_header(struct dn_scp *scp, struct sk_buff *skb, unsigned char msgflag, int len)
{
	unsigned char *ptr = skb_push(skb, len);

	BUG_ON(len < 5);

	*ptr++ = msgflag;
	*((__le16 *)ptr) = scp->addrrem;
	ptr += 2;
	*((__le16 *)ptr) = scp->addrloc;
	ptr += 2;
	return (__le16 __force *)ptr;
}

static __le16 *dn_mk_ack_header(struct sock *sk, struct sk_buff *skb, unsigned char msgflag, int hlen, int other)
{
	struct dn_scp *scp = DN_SK(sk);
	unsigned short acknum = scp->numdat_rcv & 0x0FFF;
	unsigned short ackcrs = scp->numoth_rcv & 0x0FFF;
	__le16 *ptr;

	BUG_ON(hlen < 9);

	scp->ackxmt_dat = acknum;
	scp->ackxmt_oth = ackcrs;
	acknum |= 0x8000;
	ackcrs |= 0x8000;

	/* If this is an "other data/ack" message, swap acknum and ackcrs */
	if (other) {
		unsigned short tmp = acknum;
		acknum = ackcrs;
		ackcrs = tmp;
	}

	/* Set "cross subchannel" bit in ackcrs */
	ackcrs |= 0x2000;

	ptr = (__le16 *)dn_mk_common_header(scp, skb, msgflag, hlen);

	*ptr++ = dn_htons(acknum);
	*ptr++ = dn_htons(ackcrs);

	return ptr;
}

static __le16 *dn_nsp_mk_data_header(struct sock *sk, struct sk_buff *skb, int oth)
{
	struct dn_scp *scp = DN_SK(sk);
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	__le16 *ptr = dn_mk_ack_header(sk, skb, cb->nsp_flags, 11, oth);

	if (unlikely(oth)) {
		cb->segnum = scp->numoth;
		seq_add(&scp->numoth, 1);
	} else {
		cb->segnum = scp->numdat;
		seq_add(&scp->numdat, 1);
	}
	*(ptr++) = dn_htons(cb->segnum);

	return ptr;
}

void dn_nsp_queue_xmit(struct sock *sk, struct sk_buff *skb,
			gfp_t gfp, int oth)
{
	struct dn_scp *scp = DN_SK(sk);
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	unsigned long t = ((scp->nsp_srtt >> 2) + scp->nsp_rttvar) >> 1;

	cb->xmit_count = 0;
	dn_nsp_mk_data_header(sk, skb, oth);

	/*
	 * Slow start: If we have been idle for more than
	 * one RTT, then reset window to min size.
	 */
	if ((jiffies - scp->stamp) > t)
		scp->snd_window = NSP_MIN_WINDOW;

	if (oth)
		skb_queue_tail(&scp->other_xmit_queue, skb);
	else
		skb_queue_tail(&scp->data_xmit_queue, skb);

	if (scp->flowrem_sw != DN_SEND)
		return;

	dn_nsp_clone_and_send(skb, gfp);
}


int dn_nsp_check_xmit_queue(struct sock *sk, struct sk_buff *skb, struct sk_buff_head *q, unsigned short acknum)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	struct dn_scp *scp = DN_SK(sk);
	struct sk_buff *skb2, *list, *ack = NULL;
	int wakeup = 0;
	int try_retrans = 0;
	unsigned long reftime = cb->stamp;
	unsigned long pkttime;
	unsigned short xmit_count;
	unsigned short segnum;

	skb2 = q->next;
	list = (struct sk_buff *)q;
	while(list != skb2) {
		struct dn_skb_cb *cb2 = DN_SKB_CB(skb2);

		if (dn_before_or_equal(cb2->segnum, acknum))
			ack = skb2;

		/* printk(KERN_DEBUG "ack: %s %04x %04x\n", ack ? "ACK" : "SKIP", (int)cb2->segnum, (int)acknum); */

		skb2 = skb2->next;

		if (ack == NULL)
			continue;

		/* printk(KERN_DEBUG "check_xmit_queue: %04x, %d\n", acknum, cb2->xmit_count); */

		/* Does _last_ packet acked have xmit_count > 1 */
		try_retrans = 0;
		/* Remember to wake up the sending process */
		wakeup = 1;
		/* Keep various statistics */
		pkttime = cb2->stamp;
		xmit_count = cb2->xmit_count;
		segnum = cb2->segnum;
		/* Remove and drop ack'ed packet */
		skb_unlink(ack, q);
		kfree_skb(ack);
		ack = NULL;

		/*
		 * We don't expect to see acknowledgements for packets we
		 * haven't sent yet.
		 */
		WARN_ON(xmit_count == 0);

		/*
		 * If the packet has only been sent once, we can use it
		 * to calculate the RTT and also open the window a little
		 * further.
		 */
		if (xmit_count == 1) {
			if (dn_equal(segnum, acknum))
				dn_nsp_rtt(sk, (long)(pkttime - reftime));

			if (scp->snd_window < scp->max_window)
				scp->snd_window++;
		}

		/*
		 * Packet has been sent more than once. If this is the last
		 * packet to be acknowledged then we want to send the next
		 * packet in the send queue again (assumes the remote host does
		 * go-back-N error control).
		 */
		if (xmit_count > 1)
			try_retrans = 1;
	}

	if (try_retrans)
		dn_nsp_output(sk);

	return wakeup;
}

void dn_nsp_send_data_ack(struct sock *sk)
{
	struct sk_buff *skb = NULL;

	if ((skb = dn_alloc_skb(sk, 9, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, 9);
	dn_mk_ack_header(sk, skb, 0x04, 9, 0);
	dn_nsp_send(skb);
}

void dn_nsp_send_oth_ack(struct sock *sk)
{
	struct sk_buff *skb = NULL;

	if ((skb = dn_alloc_skb(sk, 9, GFP_ATOMIC)) == NULL)
		return;

	skb_reserve(skb, 9);
	dn_mk_ack_header(sk, skb, 0x14, 9, 1);
	dn_nsp_send(skb);
}


void dn_send_conn_ack (struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);
	struct sk_buff *skb = NULL;
	struct nsp_conn_ack_msg *msg;

	if ((skb = dn_alloc_skb(sk, 3, sk->sk_allocation)) == NULL)
		return;

	msg = (struct nsp_conn_ack_msg *)skb_put(skb, 3);
	msg->msgflg = 0x24;
	msg->dstaddr = scp->addrrem;

	dn_nsp_send(skb);
}

void dn_nsp_delayed_ack(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	if (scp->ackxmt_oth != scp->numoth_rcv)
		dn_nsp_send_oth_ack(sk);

	if (scp->ackxmt_dat != scp->numdat_rcv)
		dn_nsp_send_data_ack(sk);
}

static int dn_nsp_retrans_conn_conf(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	if (scp->state == DN_CC)
		dn_send_conn_conf(sk, GFP_ATOMIC);

	return 0;
}

void dn_send_conn_conf(struct sock *sk, gfp_t gfp)
{
	struct dn_scp *scp = DN_SK(sk);
	struct sk_buff *skb = NULL;
	struct nsp_conn_init_msg *msg;
	__u8 len = (__u8)dn_ntohs(scp->conndata_out.opt_optl);

	if ((skb = dn_alloc_skb(sk, 50 + len, gfp)) == NULL)
		return;

	msg = (struct nsp_conn_init_msg *)skb_put(skb, sizeof(*msg));
	msg->msgflg = 0x28;
	msg->dstaddr = scp->addrrem;
	msg->srcaddr = scp->addrloc;
	msg->services = scp->services_loc;
	msg->info = scp->info_loc;
	msg->segsize = dn_htons(scp->segsize_loc);

	*skb_put(skb,1) = len;

	if (len > 0)
		memcpy(skb_put(skb, len), scp->conndata_out.opt_data, len);


	dn_nsp_send(skb);

	scp->persist = dn_nsp_persist(sk);
	scp->persist_fxn = dn_nsp_retrans_conn_conf;
}


static __inline__ void dn_nsp_do_disc(struct sock *sk, unsigned char msgflg,
			unsigned short reason, gfp_t gfp,
			struct dst_entry *dst,
			int ddl, unsigned char *dd, __le16 rem, __le16 loc)
{
	struct sk_buff *skb = NULL;
	int size = 7 + ddl + ((msgflg == NSP_DISCINIT) ? 1 : 0);
	unsigned char *msg;

	if ((dst == NULL) || (rem == 0)) {
		if (net_ratelimit())
			printk(KERN_DEBUG "DECnet: dn_nsp_do_disc: BUG! Please report this to SteveW@ACM.org rem=%u dst=%p\n", dn_ntohs(rem), dst);
		return;
	}

	if ((skb = dn_alloc_skb(sk, size, gfp)) == NULL)
		return;

	msg = skb_put(skb, size);
	*msg++ = msgflg;
	*(__le16 *)msg = rem;
	msg += 2;
	*(__le16 *)msg = loc;
	msg += 2;
	*(__le16 *)msg = dn_htons(reason);
	msg += 2;
	if (msgflg == NSP_DISCINIT)
		*msg++ = ddl;

	if (ddl) {
		memcpy(msg, dd, ddl);
	}

	/*
	 * This doesn't go via the dn_nsp_send() function since we need
	 * to be able to send disc packets out which have no socket
	 * associations.
	 */
	skb->dst = dst_clone(dst);
	dst_output(skb);
}


void dn_nsp_send_disc(struct sock *sk, unsigned char msgflg,
			unsigned short reason, gfp_t gfp)
{
	struct dn_scp *scp = DN_SK(sk);
	int ddl = 0;

	if (msgflg == NSP_DISCINIT)
		ddl = dn_ntohs(scp->discdata_out.opt_optl);

	if (reason == 0)
		reason = dn_ntohs(scp->discdata_out.opt_status);

	dn_nsp_do_disc(sk, msgflg, reason, gfp, sk->sk_dst_cache, ddl,
		scp->discdata_out.opt_data, scp->addrrem, scp->addrloc);
}


void dn_nsp_return_disc(struct sk_buff *skb, unsigned char msgflg,
			unsigned short reason)
{
	struct dn_skb_cb *cb = DN_SKB_CB(skb);
	int ddl = 0;
	gfp_t gfp = GFP_ATOMIC;

	dn_nsp_do_disc(NULL, msgflg, reason, gfp, skb->dst, ddl,
			NULL, cb->src_port, cb->dst_port);
}


void dn_nsp_send_link(struct sock *sk, unsigned char lsflags, char fcval)
{
	struct dn_scp *scp = DN_SK(sk);
	struct sk_buff *skb;
	unsigned char *ptr;
	gfp_t gfp = GFP_ATOMIC;

	if ((skb = dn_alloc_skb(sk, DN_MAX_NSP_DATA_HEADER + 2, gfp)) == NULL)
		return;

	skb_reserve(skb, DN_MAX_NSP_DATA_HEADER);
	ptr = skb_put(skb, 2);
	DN_SKB_CB(skb)->nsp_flags = 0x10;
	*ptr++ = lsflags;
	*ptr = fcval;

	dn_nsp_queue_xmit(sk, skb, gfp, 1);

	scp->persist = dn_nsp_persist(sk);
	scp->persist_fxn = dn_nsp_xmit_timeout;
}

static int dn_nsp_retrans_conninit(struct sock *sk)
{
	struct dn_scp *scp = DN_SK(sk);

	if (scp->state == DN_CI)
		dn_nsp_send_conninit(sk, NSP_RCI);

	return 0;
}

void dn_nsp_send_conninit(struct sock *sk, unsigned char msgflg)
{
	struct dn_scp *scp = DN_SK(sk);
	struct nsp_conn_init_msg *msg;
	unsigned char aux;
	unsigned char menuver;
	struct dn_skb_cb *cb;
	unsigned char type = 1;
	gfp_t allocation = (msgflg == NSP_CI) ? sk->sk_allocation : GFP_ATOMIC;
	struct sk_buff *skb = dn_alloc_skb(sk, 200, allocation);

	if (!skb)
		return;

	cb  = DN_SKB_CB(skb);
	msg = (struct nsp_conn_init_msg *)skb_put(skb,sizeof(*msg));

	msg->msgflg	= msgflg;
	msg->dstaddr	= 0x0000;		/* Remote Node will assign it*/

	msg->srcaddr	= scp->addrloc;
	msg->services	= scp->services_loc;	/* Requested flow control    */
	msg->info	= scp->info_loc;	/* Version Number            */
	msg->segsize	= dn_htons(scp->segsize_loc);	/* Max segment size  */

	if (scp->peer.sdn_objnum)
		type = 0;

	skb_put(skb, dn_sockaddr2username(&scp->peer, skb->tail, type));
	skb_put(skb, dn_sockaddr2username(&scp->addr, skb->tail, 2));

	menuver = DN_MENUVER_ACC | DN_MENUVER_USR;
	if (scp->peer.sdn_flags & SDF_PROXY)
		menuver |= DN_MENUVER_PRX;
	if (scp->peer.sdn_flags & SDF_UICPROXY)
		menuver |= DN_MENUVER_UIC;

	*skb_put(skb, 1) = menuver;	/* Menu Version		*/

	aux = scp->accessdata.acc_userl;
	*skb_put(skb, 1) = aux;
	if (aux > 0)
	memcpy(skb_put(skb, aux), scp->accessdata.acc_user, aux);

	aux = scp->accessdata.acc_passl;
	*skb_put(skb, 1) = aux;
	if (aux > 0)
	memcpy(skb_put(skb, aux), scp->accessdata.acc_pass, aux);

	aux = scp->accessdata.acc_accl;
	*skb_put(skb, 1) = aux;
	if (aux > 0)
	memcpy(skb_put(skb, aux), scp->accessdata.acc_acc, aux);

	aux = (__u8)dn_ntohs(scp->conndata_out.opt_optl);
	*skb_put(skb, 1) = aux;
	if (aux > 0)
	memcpy(skb_put(skb,aux), scp->conndata_out.opt_data, aux);

	scp->persist = dn_nsp_persist(sk);
	scp->persist_fxn = dn_nsp_retrans_conninit;

	cb->rt_flags = DN_RT_F_RQR;

	dn_nsp_send(skb);
}

