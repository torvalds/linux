// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/* isotp.c - ISO 15765-2 CAN transport protocol for protocol family CAN
 *
 * This implementation does not provide ISO-TP specific return values to the
 * userspace.
 *
 * - RX path timeout of data reception leads to -ETIMEDOUT
 * - RX path SN mismatch leads to -EILSEQ
 * - RX path data reception with wrong padding leads to -EBADMSG
 * - TX path flowcontrol reception timeout leads to -ECOMM
 * - TX path flowcontrol reception overflow leads to -EMSGSIZE
 * - TX path flowcontrol reception with wrong layout/padding leads to -EBADMSG
 * - when a transfer (tx) is on the run the next write() blocks until it's done
 * - use CAN_ISOTP_WAIT_TX_DONE flag to block the caller until the PDU is sent
 * - as we have static buffers the check whether the PDU fits into the buffer
 *   is done at FF reception time (no support for sending 'wait frames')
 *
 * Copyright (c) 2020 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/wait.h>
#include <linux/uio.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/can.h>
#include <linux/can/core.h>
#include <linux/can/skb.h>
#include <linux/can/isotp.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/net_namespace.h>

MODULE_DESCRIPTION("PF_CAN isotp 15765-2:2016 protocol");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Oliver Hartkopp <socketcan@hartkopp.net>");
MODULE_ALIAS("can-proto-6");

#define ISOTP_MIN_NAMELEN CAN_REQUIRED_SIZE(struct sockaddr_can, can_addr.tp)

#define SINGLE_MASK(id) (((id) & CAN_EFF_FLAG) ? \
			 (CAN_EFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG) : \
			 (CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG))

/* ISO 15765-2:2016 supports more than 4095 byte per ISO PDU as the FF_DL can
 * take full 32 bit values (4 Gbyte). We would need some good concept to handle
 * this between user space and kernel space. For now increase the static buffer
 * to something about 64 kbyte to be able to test this new functionality.
 */
#define MAX_MSG_LENGTH 66000

/* N_PCI type values in bits 7-4 of N_PCI bytes */
#define N_PCI_SF 0x00	/* single frame */
#define N_PCI_FF 0x10	/* first frame */
#define N_PCI_CF 0x20	/* consecutive frame */
#define N_PCI_FC 0x30	/* flow control */

#define N_PCI_SZ 1	/* size of the PCI byte #1 */
#define SF_PCI_SZ4 1	/* size of SingleFrame PCI including 4 bit SF_DL */
#define SF_PCI_SZ8 2	/* size of SingleFrame PCI including 8 bit SF_DL */
#define FF_PCI_SZ12 2	/* size of FirstFrame PCI including 12 bit FF_DL */
#define FF_PCI_SZ32 6	/* size of FirstFrame PCI including 32 bit FF_DL */
#define FC_CONTENT_SZ 3	/* flow control content size in byte (FS/BS/STmin) */

#define ISOTP_CHECK_PADDING (CAN_ISOTP_CHK_PAD_LEN | CAN_ISOTP_CHK_PAD_DATA)

/* Flow Status given in FC frame */
#define ISOTP_FC_CTS 0		/* clear to send */
#define ISOTP_FC_WT 1		/* wait */
#define ISOTP_FC_OVFLW 2	/* overflow */

enum {
	ISOTP_IDLE = 0,
	ISOTP_WAIT_FIRST_FC,
	ISOTP_WAIT_FC,
	ISOTP_WAIT_DATA,
	ISOTP_SENDING
};

struct tpcon {
	unsigned int idx;
	unsigned int len;
	u32 state;
	u8 bs;
	u8 sn;
	u8 ll_dl;
	u8 buf[MAX_MSG_LENGTH + 1];
};

struct isotp_sock {
	struct sock sk;
	int bound;
	int ifindex;
	canid_t txid;
	canid_t rxid;
	ktime_t tx_gap;
	ktime_t lastrxcf_tstamp;
	struct hrtimer rxtimer, txtimer;
	struct can_isotp_options opt;
	struct can_isotp_fc_options rxfc, txfc;
	struct can_isotp_ll_options ll;
	u32 frame_txtime;
	u32 force_tx_stmin;
	u32 force_rx_stmin;
	u32 cfecho; /* consecutive frame echo tag */
	struct tpcon rx, tx;
	struct list_head notifier;
	wait_queue_head_t wait;
	spinlock_t rx_lock; /* protect single thread state machine */
};

static LIST_HEAD(isotp_notifier_list);
static DEFINE_SPINLOCK(isotp_notifier_lock);
static struct isotp_sock *isotp_busy_notifier;

static inline struct isotp_sock *isotp_sk(const struct sock *sk)
{
	return (struct isotp_sock *)sk;
}

static enum hrtimer_restart isotp_rx_timer_handler(struct hrtimer *hrtimer)
{
	struct isotp_sock *so = container_of(hrtimer, struct isotp_sock,
					     rxtimer);
	struct sock *sk = &so->sk;

	if (so->rx.state == ISOTP_WAIT_DATA) {
		/* we did not get new data frames in time */

		/* report 'connection timed out' */
		sk->sk_err = ETIMEDOUT;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);

		/* reset rx state */
		so->rx.state = ISOTP_IDLE;
	}

	return HRTIMER_NORESTART;
}

static int isotp_send_fc(struct sock *sk, int ae, u8 flowstatus)
{
	struct net_device *dev;
	struct sk_buff *nskb;
	struct canfd_frame *ncf;
	struct isotp_sock *so = isotp_sk(sk);
	int can_send_ret;

	nskb = alloc_skb(so->ll.mtu + sizeof(struct can_skb_priv), gfp_any());
	if (!nskb)
		return 1;

	dev = dev_get_by_index(sock_net(sk), so->ifindex);
	if (!dev) {
		kfree_skb(nskb);
		return 1;
	}

	can_skb_reserve(nskb);
	can_skb_prv(nskb)->ifindex = dev->ifindex;
	can_skb_prv(nskb)->skbcnt = 0;

	nskb->dev = dev;
	can_skb_set_owner(nskb, sk);
	ncf = (struct canfd_frame *)nskb->data;
	skb_put_zero(nskb, so->ll.mtu);

	/* create & send flow control reply */
	ncf->can_id = so->txid;

	if (so->opt.flags & CAN_ISOTP_TX_PADDING) {
		memset(ncf->data, so->opt.txpad_content, CAN_MAX_DLEN);
		ncf->len = CAN_MAX_DLEN;
	} else {
		ncf->len = ae + FC_CONTENT_SZ;
	}

	ncf->data[ae] = N_PCI_FC | flowstatus;
	ncf->data[ae + 1] = so->rxfc.bs;
	ncf->data[ae + 2] = so->rxfc.stmin;

	if (ae)
		ncf->data[0] = so->opt.ext_address;

	ncf->flags = so->ll.tx_flags;

	can_send_ret = can_send(nskb, 1);
	if (can_send_ret)
		pr_notice_once("can-isotp: %s: can_send_ret %pe\n",
			       __func__, ERR_PTR(can_send_ret));

	dev_put(dev);

	/* reset blocksize counter */
	so->rx.bs = 0;

	/* reset last CF frame rx timestamp for rx stmin enforcement */
	so->lastrxcf_tstamp = ktime_set(0, 0);

	/* start rx timeout watchdog */
	hrtimer_start(&so->rxtimer, ktime_set(1, 0), HRTIMER_MODE_REL_SOFT);
	return 0;
}

static void isotp_rcv_skb(struct sk_buff *skb, struct sock *sk)
{
	struct sockaddr_can *addr = (struct sockaddr_can *)skb->cb;

	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct sockaddr_can));

	memset(addr, 0, sizeof(*addr));
	addr->can_family = AF_CAN;
	addr->can_ifindex = skb->dev->ifindex;

	if (sock_queue_rcv_skb(sk, skb) < 0)
		kfree_skb(skb);
}

static u8 padlen(u8 datalen)
{
	static const u8 plen[] = {
		8, 8, 8, 8, 8, 8, 8, 8, 8,	/* 0 - 8 */
		12, 12, 12, 12,			/* 9 - 12 */
		16, 16, 16, 16,			/* 13 - 16 */
		20, 20, 20, 20,			/* 17 - 20 */
		24, 24, 24, 24,			/* 21 - 24 */
		32, 32, 32, 32, 32, 32, 32, 32,	/* 25 - 32 */
		48, 48, 48, 48, 48, 48, 48, 48,	/* 33 - 40 */
		48, 48, 48, 48, 48, 48, 48, 48	/* 41 - 48 */
	};

	if (datalen > 48)
		return 64;

	return plen[datalen];
}

/* check for length optimization and return 1/true when the check fails */
static int check_optimized(struct canfd_frame *cf, int start_index)
{
	/* for CAN_DL <= 8 the start_index is equal to the CAN_DL as the
	 * padding would start at this point. E.g. if the padding would
	 * start at cf.data[7] cf->len has to be 7 to be optimal.
	 * Note: The data[] index starts with zero.
	 */
	if (cf->len <= CAN_MAX_DLEN)
		return (cf->len != start_index);

	/* This relation is also valid in the non-linear DLC range, where
	 * we need to take care of the minimal next possible CAN_DL.
	 * The correct check would be (padlen(cf->len) != padlen(start_index)).
	 * But as cf->len can only take discrete values from 12, .., 64 at this
	 * point the padlen(cf->len) is always equal to cf->len.
	 */
	return (cf->len != padlen(start_index));
}

/* check padding and return 1/true when the check fails */
static int check_pad(struct isotp_sock *so, struct canfd_frame *cf,
		     int start_index, u8 content)
{
	int i;

	/* no RX_PADDING value => check length of optimized frame length */
	if (!(so->opt.flags & CAN_ISOTP_RX_PADDING)) {
		if (so->opt.flags & CAN_ISOTP_CHK_PAD_LEN)
			return check_optimized(cf, start_index);

		/* no valid test against empty value => ignore frame */
		return 1;
	}

	/* check datalength of correctly padded CAN frame */
	if ((so->opt.flags & CAN_ISOTP_CHK_PAD_LEN) &&
	    cf->len != padlen(cf->len))
		return 1;

	/* check padding content */
	if (so->opt.flags & CAN_ISOTP_CHK_PAD_DATA) {
		for (i = start_index; i < cf->len; i++)
			if (cf->data[i] != content)
				return 1;
	}
	return 0;
}

static int isotp_rcv_fc(struct isotp_sock *so, struct canfd_frame *cf, int ae)
{
	struct sock *sk = &so->sk;

	if (so->tx.state != ISOTP_WAIT_FC &&
	    so->tx.state != ISOTP_WAIT_FIRST_FC)
		return 0;

	hrtimer_cancel(&so->txtimer);

	if ((cf->len < ae + FC_CONTENT_SZ) ||
	    ((so->opt.flags & ISOTP_CHECK_PADDING) &&
	     check_pad(so, cf, ae + FC_CONTENT_SZ, so->opt.rxpad_content))) {
		/* malformed PDU - report 'not a data message' */
		sk->sk_err = EBADMSG;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);

		so->tx.state = ISOTP_IDLE;
		wake_up_interruptible(&so->wait);
		return 1;
	}

	/* get communication parameters only from the first FC frame */
	if (so->tx.state == ISOTP_WAIT_FIRST_FC) {
		so->txfc.bs = cf->data[ae + 1];
		so->txfc.stmin = cf->data[ae + 2];

		/* fix wrong STmin values according spec */
		if (so->txfc.stmin > 0x7F &&
		    (so->txfc.stmin < 0xF1 || so->txfc.stmin > 0xF9))
			so->txfc.stmin = 0x7F;

		so->tx_gap = ktime_set(0, 0);
		/* add transmission time for CAN frame N_As */
		so->tx_gap = ktime_add_ns(so->tx_gap, so->frame_txtime);
		/* add waiting time for consecutive frames N_Cs */
		if (so->opt.flags & CAN_ISOTP_FORCE_TXSTMIN)
			so->tx_gap = ktime_add_ns(so->tx_gap,
						  so->force_tx_stmin);
		else if (so->txfc.stmin < 0x80)
			so->tx_gap = ktime_add_ns(so->tx_gap,
						  so->txfc.stmin * 1000000);
		else
			so->tx_gap = ktime_add_ns(so->tx_gap,
						  (so->txfc.stmin - 0xF0)
						  * 100000);
		so->tx.state = ISOTP_WAIT_FC;
	}

	switch (cf->data[ae] & 0x0F) {
	case ISOTP_FC_CTS:
		so->tx.bs = 0;
		so->tx.state = ISOTP_SENDING;
		/* start cyclic timer for sending CF frame */
		hrtimer_start(&so->txtimer, so->tx_gap,
			      HRTIMER_MODE_REL_SOFT);
		break;

	case ISOTP_FC_WT:
		/* start timer to wait for next FC frame */
		hrtimer_start(&so->txtimer, ktime_set(1, 0),
			      HRTIMER_MODE_REL_SOFT);
		break;

	case ISOTP_FC_OVFLW:
		/* overflow on receiver side - report 'message too long' */
		sk->sk_err = EMSGSIZE;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);
		fallthrough;

	default:
		/* stop this tx job */
		so->tx.state = ISOTP_IDLE;
		wake_up_interruptible(&so->wait);
	}
	return 0;
}

static int isotp_rcv_sf(struct sock *sk, struct canfd_frame *cf, int pcilen,
			struct sk_buff *skb, int len)
{
	struct isotp_sock *so = isotp_sk(sk);
	struct sk_buff *nskb;

	hrtimer_cancel(&so->rxtimer);
	so->rx.state = ISOTP_IDLE;

	if (!len || len > cf->len - pcilen)
		return 1;

	if ((so->opt.flags & ISOTP_CHECK_PADDING) &&
	    check_pad(so, cf, pcilen + len, so->opt.rxpad_content)) {
		/* malformed PDU - report 'not a data message' */
		sk->sk_err = EBADMSG;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);
		return 1;
	}

	nskb = alloc_skb(len, gfp_any());
	if (!nskb)
		return 1;

	memcpy(skb_put(nskb, len), &cf->data[pcilen], len);

	nskb->tstamp = skb->tstamp;
	nskb->dev = skb->dev;
	isotp_rcv_skb(nskb, sk);
	return 0;
}

static int isotp_rcv_ff(struct sock *sk, struct canfd_frame *cf, int ae)
{
	struct isotp_sock *so = isotp_sk(sk);
	int i;
	int off;
	int ff_pci_sz;

	hrtimer_cancel(&so->rxtimer);
	so->rx.state = ISOTP_IDLE;

	/* get the used sender LL_DL from the (first) CAN frame data length */
	so->rx.ll_dl = padlen(cf->len);

	/* the first frame has to use the entire frame up to LL_DL length */
	if (cf->len != so->rx.ll_dl)
		return 1;

	/* get the FF_DL */
	so->rx.len = (cf->data[ae] & 0x0F) << 8;
	so->rx.len += cf->data[ae + 1];

	/* Check for FF_DL escape sequence supporting 32 bit PDU length */
	if (so->rx.len) {
		ff_pci_sz = FF_PCI_SZ12;
	} else {
		/* FF_DL = 0 => get real length from next 4 bytes */
		so->rx.len = cf->data[ae + 2] << 24;
		so->rx.len += cf->data[ae + 3] << 16;
		so->rx.len += cf->data[ae + 4] << 8;
		so->rx.len += cf->data[ae + 5];
		ff_pci_sz = FF_PCI_SZ32;
	}

	/* take care of a potential SF_DL ESC offset for TX_DL > 8 */
	off = (so->rx.ll_dl > CAN_MAX_DLEN) ? 1 : 0;

	if (so->rx.len + ae + off + ff_pci_sz < so->rx.ll_dl)
		return 1;

	if (so->rx.len > MAX_MSG_LENGTH) {
		/* send FC frame with overflow status */
		isotp_send_fc(sk, ae, ISOTP_FC_OVFLW);
		return 1;
	}

	/* copy the first received data bytes */
	so->rx.idx = 0;
	for (i = ae + ff_pci_sz; i < so->rx.ll_dl; i++)
		so->rx.buf[so->rx.idx++] = cf->data[i];

	/* initial setup for this pdu reception */
	so->rx.sn = 1;
	so->rx.state = ISOTP_WAIT_DATA;

	/* no creation of flow control frames */
	if (so->opt.flags & CAN_ISOTP_LISTEN_MODE)
		return 0;

	/* send our first FC frame */
	isotp_send_fc(sk, ae, ISOTP_FC_CTS);
	return 0;
}

static int isotp_rcv_cf(struct sock *sk, struct canfd_frame *cf, int ae,
			struct sk_buff *skb)
{
	struct isotp_sock *so = isotp_sk(sk);
	struct sk_buff *nskb;
	int i;

	if (so->rx.state != ISOTP_WAIT_DATA)
		return 0;

	/* drop if timestamp gap is less than force_rx_stmin nano secs */
	if (so->opt.flags & CAN_ISOTP_FORCE_RXSTMIN) {
		if (ktime_to_ns(ktime_sub(skb->tstamp, so->lastrxcf_tstamp)) <
		    so->force_rx_stmin)
			return 0;

		so->lastrxcf_tstamp = skb->tstamp;
	}

	hrtimer_cancel(&so->rxtimer);

	/* CFs are never longer than the FF */
	if (cf->len > so->rx.ll_dl)
		return 1;

	/* CFs have usually the LL_DL length */
	if (cf->len < so->rx.ll_dl) {
		/* this is only allowed for the last CF */
		if (so->rx.len - so->rx.idx > so->rx.ll_dl - ae - N_PCI_SZ)
			return 1;
	}

	if ((cf->data[ae] & 0x0F) != so->rx.sn) {
		/* wrong sn detected - report 'illegal byte sequence' */
		sk->sk_err = EILSEQ;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);

		/* reset rx state */
		so->rx.state = ISOTP_IDLE;
		return 1;
	}
	so->rx.sn++;
	so->rx.sn %= 16;

	for (i = ae + N_PCI_SZ; i < cf->len; i++) {
		so->rx.buf[so->rx.idx++] = cf->data[i];
		if (so->rx.idx >= so->rx.len)
			break;
	}

	if (so->rx.idx >= so->rx.len) {
		/* we are done */
		so->rx.state = ISOTP_IDLE;

		if ((so->opt.flags & ISOTP_CHECK_PADDING) &&
		    check_pad(so, cf, i + 1, so->opt.rxpad_content)) {
			/* malformed PDU - report 'not a data message' */
			sk->sk_err = EBADMSG;
			if (!sock_flag(sk, SOCK_DEAD))
				sk_error_report(sk);
			return 1;
		}

		nskb = alloc_skb(so->rx.len, gfp_any());
		if (!nskb)
			return 1;

		memcpy(skb_put(nskb, so->rx.len), so->rx.buf,
		       so->rx.len);

		nskb->tstamp = skb->tstamp;
		nskb->dev = skb->dev;
		isotp_rcv_skb(nskb, sk);
		return 0;
	}

	/* perform blocksize handling, if enabled */
	if (!so->rxfc.bs || ++so->rx.bs < so->rxfc.bs) {
		/* start rx timeout watchdog */
		hrtimer_start(&so->rxtimer, ktime_set(1, 0),
			      HRTIMER_MODE_REL_SOFT);
		return 0;
	}

	/* no creation of flow control frames */
	if (so->opt.flags & CAN_ISOTP_LISTEN_MODE)
		return 0;

	/* we reached the specified blocksize so->rxfc.bs */
	isotp_send_fc(sk, ae, ISOTP_FC_CTS);
	return 0;
}

static void isotp_rcv(struct sk_buff *skb, void *data)
{
	struct sock *sk = (struct sock *)data;
	struct isotp_sock *so = isotp_sk(sk);
	struct canfd_frame *cf;
	int ae = (so->opt.flags & CAN_ISOTP_EXTEND_ADDR) ? 1 : 0;
	u8 n_pci_type, sf_dl;

	/* Strictly receive only frames with the configured MTU size
	 * => clear separation of CAN2.0 / CAN FD transport channels
	 */
	if (skb->len != so->ll.mtu)
		return;

	cf = (struct canfd_frame *)skb->data;

	/* if enabled: check reception of my configured extended address */
	if (ae && cf->data[0] != so->opt.rx_ext_address)
		return;

	n_pci_type = cf->data[ae] & 0xF0;

	/* Make sure the state changes and data structures stay consistent at
	 * CAN frame reception time. This locking is not needed in real world
	 * use cases but the inconsistency can be triggered with syzkaller.
	 */
	spin_lock(&so->rx_lock);

	if (so->opt.flags & CAN_ISOTP_HALF_DUPLEX) {
		/* check rx/tx path half duplex expectations */
		if ((so->tx.state != ISOTP_IDLE && n_pci_type != N_PCI_FC) ||
		    (so->rx.state != ISOTP_IDLE && n_pci_type == N_PCI_FC))
			goto out_unlock;
	}

	switch (n_pci_type) {
	case N_PCI_FC:
		/* tx path: flow control frame containing the FC parameters */
		isotp_rcv_fc(so, cf, ae);
		break;

	case N_PCI_SF:
		/* rx path: single frame
		 *
		 * As we do not have a rx.ll_dl configuration, we can only test
		 * if the CAN frames payload length matches the LL_DL == 8
		 * requirements - no matter if it's CAN 2.0 or CAN FD
		 */

		/* get the SF_DL from the N_PCI byte */
		sf_dl = cf->data[ae] & 0x0F;

		if (cf->len <= CAN_MAX_DLEN) {
			isotp_rcv_sf(sk, cf, SF_PCI_SZ4 + ae, skb, sf_dl);
		} else {
			if (skb->len == CANFD_MTU) {
				/* We have a CAN FD frame and CAN_DL is greater than 8:
				 * Only frames with the SF_DL == 0 ESC value are valid.
				 *
				 * If so take care of the increased SF PCI size
				 * (SF_PCI_SZ8) to point to the message content behind
				 * the extended SF PCI info and get the real SF_DL
				 * length value from the formerly first data byte.
				 */
				if (sf_dl == 0)
					isotp_rcv_sf(sk, cf, SF_PCI_SZ8 + ae, skb,
						     cf->data[SF_PCI_SZ4 + ae]);
			}
		}
		break;

	case N_PCI_FF:
		/* rx path: first frame */
		isotp_rcv_ff(sk, cf, ae);
		break;

	case N_PCI_CF:
		/* rx path: consecutive frame */
		isotp_rcv_cf(sk, cf, ae, skb);
		break;
	}

out_unlock:
	spin_unlock(&so->rx_lock);
}

static void isotp_fill_dataframe(struct canfd_frame *cf, struct isotp_sock *so,
				 int ae, int off)
{
	int pcilen = N_PCI_SZ + ae + off;
	int space = so->tx.ll_dl - pcilen;
	int num = min_t(int, so->tx.len - so->tx.idx, space);
	int i;

	cf->can_id = so->txid;
	cf->len = num + pcilen;

	if (num < space) {
		if (so->opt.flags & CAN_ISOTP_TX_PADDING) {
			/* user requested padding */
			cf->len = padlen(cf->len);
			memset(cf->data, so->opt.txpad_content, cf->len);
		} else if (cf->len > CAN_MAX_DLEN) {
			/* mandatory padding for CAN FD frames */
			cf->len = padlen(cf->len);
			memset(cf->data, CAN_ISOTP_DEFAULT_PAD_CONTENT,
			       cf->len);
		}
	}

	for (i = 0; i < num; i++)
		cf->data[pcilen + i] = so->tx.buf[so->tx.idx++];

	if (ae)
		cf->data[0] = so->opt.ext_address;
}

static void isotp_send_cframe(struct isotp_sock *so)
{
	struct sock *sk = &so->sk;
	struct sk_buff *skb;
	struct net_device *dev;
	struct canfd_frame *cf;
	int can_send_ret;
	int ae = (so->opt.flags & CAN_ISOTP_EXTEND_ADDR) ? 1 : 0;

	dev = dev_get_by_index(sock_net(sk), so->ifindex);
	if (!dev)
		return;

	skb = alloc_skb(so->ll.mtu + sizeof(struct can_skb_priv), GFP_ATOMIC);
	if (!skb) {
		dev_put(dev);
		return;
	}

	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = dev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;

	cf = (struct canfd_frame *)skb->data;
	skb_put_zero(skb, so->ll.mtu);

	/* create consecutive frame */
	isotp_fill_dataframe(cf, so, ae, 0);

	/* place consecutive frame N_PCI in appropriate index */
	cf->data[ae] = N_PCI_CF | so->tx.sn++;
	so->tx.sn %= 16;
	so->tx.bs++;

	cf->flags = so->ll.tx_flags;

	skb->dev = dev;
	can_skb_set_owner(skb, sk);

	/* cfecho should have been zero'ed by init/isotp_rcv_echo() */
	if (so->cfecho)
		pr_notice_once("can-isotp: cfecho is %08X != 0\n", so->cfecho);

	/* set consecutive frame echo tag */
	so->cfecho = *(u32 *)cf->data;

	/* send frame with local echo enabled */
	can_send_ret = can_send(skb, 1);
	if (can_send_ret) {
		pr_notice_once("can-isotp: %s: can_send_ret %pe\n",
			       __func__, ERR_PTR(can_send_ret));
		if (can_send_ret == -ENOBUFS)
			pr_notice_once("can-isotp: tx queue is full\n");
	}
	dev_put(dev);
}

static void isotp_create_fframe(struct canfd_frame *cf, struct isotp_sock *so,
				int ae)
{
	int i;
	int ff_pci_sz;

	cf->can_id = so->txid;
	cf->len = so->tx.ll_dl;
	if (ae)
		cf->data[0] = so->opt.ext_address;

	/* create N_PCI bytes with 12/32 bit FF_DL data length */
	if (so->tx.len > 4095) {
		/* use 32 bit FF_DL notation */
		cf->data[ae] = N_PCI_FF;
		cf->data[ae + 1] = 0;
		cf->data[ae + 2] = (u8)(so->tx.len >> 24) & 0xFFU;
		cf->data[ae + 3] = (u8)(so->tx.len >> 16) & 0xFFU;
		cf->data[ae + 4] = (u8)(so->tx.len >> 8) & 0xFFU;
		cf->data[ae + 5] = (u8)so->tx.len & 0xFFU;
		ff_pci_sz = FF_PCI_SZ32;
	} else {
		/* use 12 bit FF_DL notation */
		cf->data[ae] = (u8)(so->tx.len >> 8) | N_PCI_FF;
		cf->data[ae + 1] = (u8)so->tx.len & 0xFFU;
		ff_pci_sz = FF_PCI_SZ12;
	}

	/* add first data bytes depending on ae */
	for (i = ae + ff_pci_sz; i < so->tx.ll_dl; i++)
		cf->data[i] = so->tx.buf[so->tx.idx++];

	so->tx.sn = 1;
	so->tx.state = ISOTP_WAIT_FIRST_FC;
}

static void isotp_rcv_echo(struct sk_buff *skb, void *data)
{
	struct sock *sk = (struct sock *)data;
	struct isotp_sock *so = isotp_sk(sk);
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;

	/* only handle my own local echo skb's */
	if (skb->sk != sk || so->cfecho != *(u32 *)cf->data)
		return;

	/* cancel local echo timeout */
	hrtimer_cancel(&so->txtimer);

	/* local echo skb with consecutive frame has been consumed */
	so->cfecho = 0;

	if (so->tx.idx >= so->tx.len) {
		/* we are done */
		so->tx.state = ISOTP_IDLE;
		wake_up_interruptible(&so->wait);
		return;
	}

	if (so->txfc.bs && so->tx.bs >= so->txfc.bs) {
		/* stop and wait for FC with timeout */
		so->tx.state = ISOTP_WAIT_FC;
		hrtimer_start(&so->txtimer, ktime_set(1, 0),
			      HRTIMER_MODE_REL_SOFT);
		return;
	}

	/* no gap between data frames needed => use burst mode */
	if (!so->tx_gap) {
		isotp_send_cframe(so);
		return;
	}

	/* start timer to send next consecutive frame with correct delay */
	hrtimer_start(&so->txtimer, so->tx_gap, HRTIMER_MODE_REL_SOFT);
}

static enum hrtimer_restart isotp_tx_timer_handler(struct hrtimer *hrtimer)
{
	struct isotp_sock *so = container_of(hrtimer, struct isotp_sock,
					     txtimer);
	struct sock *sk = &so->sk;
	enum hrtimer_restart restart = HRTIMER_NORESTART;

	switch (so->tx.state) {
	case ISOTP_SENDING:

		/* cfecho should be consumed by isotp_rcv_echo() here */
		if (!so->cfecho) {
			/* start timeout for unlikely lost echo skb */
			hrtimer_set_expires(&so->txtimer,
					    ktime_add(ktime_get(),
						      ktime_set(2, 0)));
			restart = HRTIMER_RESTART;

			/* push out the next consecutive frame */
			isotp_send_cframe(so);
			break;
		}

		/* cfecho has not been cleared in isotp_rcv_echo() */
		pr_notice_once("can-isotp: cfecho %08X timeout\n", so->cfecho);
		fallthrough;

	case ISOTP_WAIT_FC:
	case ISOTP_WAIT_FIRST_FC:

		/* we did not get any flow control frame in time */

		/* report 'communication error on send' */
		sk->sk_err = ECOMM;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);

		/* reset tx state */
		so->tx.state = ISOTP_IDLE;
		wake_up_interruptible(&so->wait);
		break;

	default:
		WARN_ON_ONCE(1);
	}

	return restart;
}

static int isotp_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;
	struct isotp_sock *so = isotp_sk(sk);
	u32 old_state = so->tx.state;
	struct sk_buff *skb;
	struct net_device *dev;
	struct canfd_frame *cf;
	int ae = (so->opt.flags & CAN_ISOTP_EXTEND_ADDR) ? 1 : 0;
	int wait_tx_done = (so->opt.flags & CAN_ISOTP_WAIT_TX_DONE) ? 1 : 0;
	s64 hrtimer_sec = 0;
	int off;
	int err;

	if (!so->bound)
		return -EADDRNOTAVAIL;

	/* we do not support multiple buffers - for now */
	if (cmpxchg(&so->tx.state, ISOTP_IDLE, ISOTP_SENDING) != ISOTP_IDLE ||
	    wq_has_sleeper(&so->wait)) {
		if (msg->msg_flags & MSG_DONTWAIT) {
			err = -EAGAIN;
			goto err_out;
		}

		/* wait for complete transmission of current pdu */
		err = wait_event_interruptible(so->wait, so->tx.state == ISOTP_IDLE);
		if (err)
			goto err_out;
	}

	if (!size || size > MAX_MSG_LENGTH) {
		err = -EINVAL;
		goto err_out_drop;
	}

	/* take care of a potential SF_DL ESC offset for TX_DL > 8 */
	off = (so->tx.ll_dl > CAN_MAX_DLEN) ? 1 : 0;

	/* does the given data fit into a single frame for SF_BROADCAST? */
	if ((so->opt.flags & CAN_ISOTP_SF_BROADCAST) &&
	    (size > so->tx.ll_dl - SF_PCI_SZ4 - ae - off)) {
		err = -EINVAL;
		goto err_out_drop;
	}

	err = memcpy_from_msg(so->tx.buf, msg, size);
	if (err < 0)
		goto err_out_drop;

	dev = dev_get_by_index(sock_net(sk), so->ifindex);
	if (!dev) {
		err = -ENXIO;
		goto err_out_drop;
	}

	skb = sock_alloc_send_skb(sk, so->ll.mtu + sizeof(struct can_skb_priv),
				  msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb) {
		dev_put(dev);
		goto err_out_drop;
	}

	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = dev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;

	so->tx.len = size;
	so->tx.idx = 0;

	cf = (struct canfd_frame *)skb->data;
	skb_put_zero(skb, so->ll.mtu);

	/* check for single frame transmission depending on TX_DL */
	if (size <= so->tx.ll_dl - SF_PCI_SZ4 - ae - off) {
		/* The message size generally fits into a SingleFrame - good.
		 *
		 * SF_DL ESC offset optimization:
		 *
		 * When TX_DL is greater 8 but the message would still fit
		 * into a 8 byte CAN frame, we can omit the offset.
		 * This prevents a protocol caused length extension from
		 * CAN_DL = 8 to CAN_DL = 12 due to the SF_SL ESC handling.
		 */
		if (size <= CAN_MAX_DLEN - SF_PCI_SZ4 - ae)
			off = 0;

		isotp_fill_dataframe(cf, so, ae, off);

		/* place single frame N_PCI w/o length in appropriate index */
		cf->data[ae] = N_PCI_SF;

		/* place SF_DL size value depending on the SF_DL ESC offset */
		if (off)
			cf->data[SF_PCI_SZ4 + ae] = size;
		else
			cf->data[ae] |= size;

		so->tx.state = ISOTP_IDLE;
		wake_up_interruptible(&so->wait);

		/* don't enable wait queue for a single frame transmission */
		wait_tx_done = 0;
	} else {
		/* send first frame and wait for FC */

		isotp_create_fframe(cf, so, ae);

		/* start timeout for FC */
		hrtimer_sec = 1;
		hrtimer_start(&so->txtimer, ktime_set(hrtimer_sec, 0),
			      HRTIMER_MODE_REL_SOFT);
	}

	/* send the first or only CAN frame */
	cf->flags = so->ll.tx_flags;

	skb->dev = dev;
	skb->sk = sk;
	err = can_send(skb, 1);
	dev_put(dev);
	if (err) {
		pr_notice_once("can-isotp: %s: can_send_ret %pe\n",
			       __func__, ERR_PTR(err));

		/* no transmission -> no timeout monitoring */
		if (hrtimer_sec)
			hrtimer_cancel(&so->txtimer);

		goto err_out_drop;
	}

	if (wait_tx_done) {
		/* wait for complete transmission of current pdu */
		wait_event_interruptible(so->wait, so->tx.state == ISOTP_IDLE);

		if (sk->sk_err)
			return -sk->sk_err;
	}

	return size;

err_out_drop:
	/* drop this PDU and unlock a potential wait queue */
	old_state = ISOTP_IDLE;
err_out:
	so->tx.state = old_state;
	if (so->tx.state == ISOTP_IDLE)
		wake_up_interruptible(&so->wait);

	return err;
}

static int isotp_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
			 int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	struct isotp_sock *so = isotp_sk(sk);
	int noblock = flags & MSG_DONTWAIT;
	int ret = 0;

	if (flags & ~(MSG_DONTWAIT | MSG_TRUNC | MSG_PEEK))
		return -EINVAL;

	if (!so->bound)
		return -EADDRNOTAVAIL;

	flags &= ~MSG_DONTWAIT;
	skb = skb_recv_datagram(sk, flags, noblock, &ret);
	if (!skb)
		return ret;

	if (size < skb->len)
		msg->msg_flags |= MSG_TRUNC;
	else
		size = skb->len;

	ret = memcpy_to_msg(msg, skb->data, size);
	if (ret < 0)
		goto out_err;

	sock_recv_timestamp(msg, sk, skb);

	if (msg->msg_name) {
		__sockaddr_check_size(ISOTP_MIN_NAMELEN);
		msg->msg_namelen = ISOTP_MIN_NAMELEN;
		memcpy(msg->msg_name, skb->cb, msg->msg_namelen);
	}

	/* set length of return value */
	ret = (flags & MSG_TRUNC) ? skb->len : size;

out_err:
	skb_free_datagram(sk, skb);

	return ret;
}

static int isotp_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct isotp_sock *so;
	struct net *net;

	if (!sk)
		return 0;

	so = isotp_sk(sk);
	net = sock_net(sk);

	/* wait for complete transmission of current pdu */
	wait_event_interruptible(so->wait, so->tx.state == ISOTP_IDLE);

	spin_lock(&isotp_notifier_lock);
	while (isotp_busy_notifier == so) {
		spin_unlock(&isotp_notifier_lock);
		schedule_timeout_uninterruptible(1);
		spin_lock(&isotp_notifier_lock);
	}
	list_del(&so->notifier);
	spin_unlock(&isotp_notifier_lock);

	lock_sock(sk);

	/* remove current filters & unregister */
	if (so->bound && (!(so->opt.flags & CAN_ISOTP_SF_BROADCAST))) {
		if (so->ifindex) {
			struct net_device *dev;

			dev = dev_get_by_index(net, so->ifindex);
			if (dev) {
				can_rx_unregister(net, dev, so->rxid,
						  SINGLE_MASK(so->rxid),
						  isotp_rcv, sk);
				can_rx_unregister(net, dev, so->txid,
						  SINGLE_MASK(so->txid),
						  isotp_rcv_echo, sk);
				dev_put(dev);
				synchronize_rcu();
			}
		}
	}

	hrtimer_cancel(&so->txtimer);
	hrtimer_cancel(&so->rxtimer);

	so->ifindex = 0;
	so->bound = 0;

	sock_orphan(sk);
	sock->sk = NULL;

	release_sock(sk);
	sock_put(sk);

	return 0;
}

static int isotp_bind(struct socket *sock, struct sockaddr *uaddr, int len)
{
	struct sockaddr_can *addr = (struct sockaddr_can *)uaddr;
	struct sock *sk = sock->sk;
	struct isotp_sock *so = isotp_sk(sk);
	struct net *net = sock_net(sk);
	int ifindex;
	struct net_device *dev;
	canid_t tx_id, rx_id;
	int err = 0;
	int notify_enetdown = 0;
	int do_rx_reg = 1;

	if (len < ISOTP_MIN_NAMELEN)
		return -EINVAL;

	/* sanitize tx/rx CAN identifiers */
	tx_id = addr->can_addr.tp.tx_id;
	if (tx_id & CAN_EFF_FLAG)
		tx_id &= (CAN_EFF_FLAG | CAN_EFF_MASK);
	else
		tx_id &= CAN_SFF_MASK;

	rx_id = addr->can_addr.tp.rx_id;
	if (rx_id & CAN_EFF_FLAG)
		rx_id &= (CAN_EFF_FLAG | CAN_EFF_MASK);
	else
		rx_id &= CAN_SFF_MASK;

	if (!addr->can_ifindex)
		return -ENODEV;

	lock_sock(sk);

	if (so->bound) {
		err = -EINVAL;
		goto out;
	}

	/* do not register frame reception for functional addressing */
	if (so->opt.flags & CAN_ISOTP_SF_BROADCAST)
		do_rx_reg = 0;

	/* do not validate rx address for functional addressing */
	if (do_rx_reg && rx_id == tx_id) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	dev = dev_get_by_index(net, addr->can_ifindex);
	if (!dev) {
		err = -ENODEV;
		goto out;
	}
	if (dev->type != ARPHRD_CAN) {
		dev_put(dev);
		err = -ENODEV;
		goto out;
	}
	if (dev->mtu < so->ll.mtu) {
		dev_put(dev);
		err = -EINVAL;
		goto out;
	}
	if (!(dev->flags & IFF_UP))
		notify_enetdown = 1;

	ifindex = dev->ifindex;

	if (do_rx_reg) {
		can_rx_register(net, dev, rx_id, SINGLE_MASK(rx_id),
				isotp_rcv, sk, "isotp", sk);

		/* no consecutive frame echo skb in flight */
		so->cfecho = 0;

		/* register for echo skb's */
		can_rx_register(net, dev, tx_id, SINGLE_MASK(tx_id),
				isotp_rcv_echo, sk, "isotpe", sk);
	}

	dev_put(dev);

	/* switch to new settings */
	so->ifindex = ifindex;
	so->rxid = rx_id;
	so->txid = tx_id;
	so->bound = 1;

out:
	release_sock(sk);

	if (notify_enetdown) {
		sk->sk_err = ENETDOWN;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);
	}

	return err;
}

static int isotp_getname(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	struct sockaddr_can *addr = (struct sockaddr_can *)uaddr;
	struct sock *sk = sock->sk;
	struct isotp_sock *so = isotp_sk(sk);

	if (peer)
		return -EOPNOTSUPP;

	memset(addr, 0, ISOTP_MIN_NAMELEN);
	addr->can_family = AF_CAN;
	addr->can_ifindex = so->ifindex;
	addr->can_addr.tp.rx_id = so->rxid;
	addr->can_addr.tp.tx_id = so->txid;

	return ISOTP_MIN_NAMELEN;
}

static int isotp_setsockopt_locked(struct socket *sock, int level, int optname,
			    sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct isotp_sock *so = isotp_sk(sk);
	int ret = 0;

	if (so->bound)
		return -EISCONN;

	switch (optname) {
	case CAN_ISOTP_OPTS:
		if (optlen != sizeof(struct can_isotp_options))
			return -EINVAL;

		if (copy_from_sockptr(&so->opt, optval, optlen))
			return -EFAULT;

		/* no separate rx_ext_address is given => use ext_address */
		if (!(so->opt.flags & CAN_ISOTP_RX_EXT_ADDR))
			so->opt.rx_ext_address = so->opt.ext_address;

		/* check for frame_txtime changes (0 => no changes) */
		if (so->opt.frame_txtime) {
			if (so->opt.frame_txtime == CAN_ISOTP_FRAME_TXTIME_ZERO)
				so->frame_txtime = 0;
			else
				so->frame_txtime = so->opt.frame_txtime;
		}
		break;

	case CAN_ISOTP_RECV_FC:
		if (optlen != sizeof(struct can_isotp_fc_options))
			return -EINVAL;

		if (copy_from_sockptr(&so->rxfc, optval, optlen))
			return -EFAULT;
		break;

	case CAN_ISOTP_TX_STMIN:
		if (optlen != sizeof(u32))
			return -EINVAL;

		if (copy_from_sockptr(&so->force_tx_stmin, optval, optlen))
			return -EFAULT;
		break;

	case CAN_ISOTP_RX_STMIN:
		if (optlen != sizeof(u32))
			return -EINVAL;

		if (copy_from_sockptr(&so->force_rx_stmin, optval, optlen))
			return -EFAULT;
		break;

	case CAN_ISOTP_LL_OPTS:
		if (optlen == sizeof(struct can_isotp_ll_options)) {
			struct can_isotp_ll_options ll;

			if (copy_from_sockptr(&ll, optval, optlen))
				return -EFAULT;

			/* check for correct ISO 11898-1 DLC data length */
			if (ll.tx_dl != padlen(ll.tx_dl))
				return -EINVAL;

			if (ll.mtu != CAN_MTU && ll.mtu != CANFD_MTU)
				return -EINVAL;

			if (ll.mtu == CAN_MTU &&
			    (ll.tx_dl > CAN_MAX_DLEN || ll.tx_flags != 0))
				return -EINVAL;

			memcpy(&so->ll, &ll, sizeof(ll));

			/* set ll_dl for tx path to similar place as for rx */
			so->tx.ll_dl = ll.tx_dl;
		} else {
			return -EINVAL;
		}
		break;

	default:
		ret = -ENOPROTOOPT;
	}

	return ret;
}

static int isotp_setsockopt(struct socket *sock, int level, int optname,
			    sockptr_t optval, unsigned int optlen)

{
	struct sock *sk = sock->sk;
	int ret;

	if (level != SOL_CAN_ISOTP)
		return -EINVAL;

	lock_sock(sk);
	ret = isotp_setsockopt_locked(sock, level, optname, optval, optlen);
	release_sock(sk);
	return ret;
}

static int isotp_getsockopt(struct socket *sock, int level, int optname,
			    char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct isotp_sock *so = isotp_sk(sk);
	int len;
	void *val;

	if (level != SOL_CAN_ISOTP)
		return -EINVAL;
	if (get_user(len, optlen))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;

	switch (optname) {
	case CAN_ISOTP_OPTS:
		len = min_t(int, len, sizeof(struct can_isotp_options));
		val = &so->opt;
		break;

	case CAN_ISOTP_RECV_FC:
		len = min_t(int, len, sizeof(struct can_isotp_fc_options));
		val = &so->rxfc;
		break;

	case CAN_ISOTP_TX_STMIN:
		len = min_t(int, len, sizeof(u32));
		val = &so->force_tx_stmin;
		break;

	case CAN_ISOTP_RX_STMIN:
		len = min_t(int, len, sizeof(u32));
		val = &so->force_rx_stmin;
		break;

	case CAN_ISOTP_LL_OPTS:
		len = min_t(int, len, sizeof(struct can_isotp_ll_options));
		val = &so->ll;
		break;

	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, val, len))
		return -EFAULT;
	return 0;
}

static void isotp_notify(struct isotp_sock *so, unsigned long msg,
			 struct net_device *dev)
{
	struct sock *sk = &so->sk;

	if (!net_eq(dev_net(dev), sock_net(sk)))
		return;

	if (so->ifindex != dev->ifindex)
		return;

	switch (msg) {
	case NETDEV_UNREGISTER:
		lock_sock(sk);
		/* remove current filters & unregister */
		if (so->bound && (!(so->opt.flags & CAN_ISOTP_SF_BROADCAST))) {
			can_rx_unregister(dev_net(dev), dev, so->rxid,
					  SINGLE_MASK(so->rxid),
					  isotp_rcv, sk);
			can_rx_unregister(dev_net(dev), dev, so->txid,
					  SINGLE_MASK(so->txid),
					  isotp_rcv_echo, sk);
		}

		so->ifindex = 0;
		so->bound  = 0;
		release_sock(sk);

		sk->sk_err = ENODEV;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);
		break;

	case NETDEV_DOWN:
		sk->sk_err = ENETDOWN;
		if (!sock_flag(sk, SOCK_DEAD))
			sk_error_report(sk);
		break;
	}
}

static int isotp_notifier(struct notifier_block *nb, unsigned long msg,
			  void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (dev->type != ARPHRD_CAN)
		return NOTIFY_DONE;
	if (msg != NETDEV_UNREGISTER && msg != NETDEV_DOWN)
		return NOTIFY_DONE;
	if (unlikely(isotp_busy_notifier)) /* Check for reentrant bug. */
		return NOTIFY_DONE;

	spin_lock(&isotp_notifier_lock);
	list_for_each_entry(isotp_busy_notifier, &isotp_notifier_list, notifier) {
		spin_unlock(&isotp_notifier_lock);
		isotp_notify(isotp_busy_notifier, msg, dev);
		spin_lock(&isotp_notifier_lock);
	}
	isotp_busy_notifier = NULL;
	spin_unlock(&isotp_notifier_lock);
	return NOTIFY_DONE;
}

static int isotp_init(struct sock *sk)
{
	struct isotp_sock *so = isotp_sk(sk);

	so->ifindex = 0;
	so->bound = 0;

	so->opt.flags = CAN_ISOTP_DEFAULT_FLAGS;
	so->opt.ext_address = CAN_ISOTP_DEFAULT_EXT_ADDRESS;
	so->opt.rx_ext_address = CAN_ISOTP_DEFAULT_EXT_ADDRESS;
	so->opt.rxpad_content = CAN_ISOTP_DEFAULT_PAD_CONTENT;
	so->opt.txpad_content = CAN_ISOTP_DEFAULT_PAD_CONTENT;
	so->opt.frame_txtime = CAN_ISOTP_DEFAULT_FRAME_TXTIME;
	so->frame_txtime = CAN_ISOTP_DEFAULT_FRAME_TXTIME;
	so->rxfc.bs = CAN_ISOTP_DEFAULT_RECV_BS;
	so->rxfc.stmin = CAN_ISOTP_DEFAULT_RECV_STMIN;
	so->rxfc.wftmax = CAN_ISOTP_DEFAULT_RECV_WFTMAX;
	so->ll.mtu = CAN_ISOTP_DEFAULT_LL_MTU;
	so->ll.tx_dl = CAN_ISOTP_DEFAULT_LL_TX_DL;
	so->ll.tx_flags = CAN_ISOTP_DEFAULT_LL_TX_FLAGS;

	/* set ll_dl for tx path to similar place as for rx */
	so->tx.ll_dl = so->ll.tx_dl;

	so->rx.state = ISOTP_IDLE;
	so->tx.state = ISOTP_IDLE;

	hrtimer_init(&so->rxtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	so->rxtimer.function = isotp_rx_timer_handler;
	hrtimer_init(&so->txtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	so->txtimer.function = isotp_tx_timer_handler;

	init_waitqueue_head(&so->wait);
	spin_lock_init(&so->rx_lock);

	spin_lock(&isotp_notifier_lock);
	list_add_tail(&so->notifier, &isotp_notifier_list);
	spin_unlock(&isotp_notifier_lock);

	return 0;
}

static int isotp_sock_no_ioctlcmd(struct socket *sock, unsigned int cmd,
				  unsigned long arg)
{
	/* no ioctls for socket layer -> hand it down to NIC layer */
	return -ENOIOCTLCMD;
}

static const struct proto_ops isotp_ops = {
	.family = PF_CAN,
	.release = isotp_release,
	.bind = isotp_bind,
	.connect = sock_no_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = isotp_getname,
	.poll = datagram_poll,
	.ioctl = isotp_sock_no_ioctlcmd,
	.gettstamp = sock_gettstamp,
	.listen = sock_no_listen,
	.shutdown = sock_no_shutdown,
	.setsockopt = isotp_setsockopt,
	.getsockopt = isotp_getsockopt,
	.sendmsg = isotp_sendmsg,
	.recvmsg = isotp_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

static struct proto isotp_proto __read_mostly = {
	.name = "CAN_ISOTP",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct isotp_sock),
	.init = isotp_init,
};

static const struct can_proto isotp_can_proto = {
	.type = SOCK_DGRAM,
	.protocol = CAN_ISOTP,
	.ops = &isotp_ops,
	.prot = &isotp_proto,
};

static struct notifier_block canisotp_notifier = {
	.notifier_call = isotp_notifier
};

static __init int isotp_module_init(void)
{
	int err;

	pr_info("can: isotp protocol\n");

	err = can_proto_register(&isotp_can_proto);
	if (err < 0)
		pr_err("can: registration of isotp protocol failed %pe\n", ERR_PTR(err));
	else
		register_netdevice_notifier(&canisotp_notifier);

	return err;
}

static __exit void isotp_module_exit(void)
{
	can_proto_unregister(&isotp_can_proto);
	unregister_netdevice_notifier(&canisotp_notifier);
}

module_init(isotp_module_init);
module_exit(isotp_module_exit);
