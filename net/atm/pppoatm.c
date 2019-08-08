// SPDX-License-Identifier: GPL-2.0-or-later
/* net/atm/pppoatm.c - RFC2364 PPP over ATM/AAL5 */

/* Copyright 1999-2000 by Mitchell Blank Jr */
/* Based on clip.c; 1995-1999 by Werner Almesberger, EPFL LRC/ICA */
/* And on ppp_async.c; Copyright 1999 Paul Mackerras */
/* And help from Jens Axboe */

/*
 *
 * This driver provides the encapsulation and framing for sending
 * and receiving PPP frames in ATM AAL5 PDUs.
 */

/*
 * One shortcoming of this driver is that it does not comply with
 * section 8 of RFC2364 - we are supposed to detect a change
 * in encapsulation and immediately abort the connection (in order
 * to avoid a black-hole being created if our peer loses state
 * and changes encapsulation unilaterally.  However, since the
 * ppp_generic layer actually does the decapsulation, we need
 * a way of notifying it when we _think_ there might be a problem)
 * There's two cases:
 *   1.	LLC-encapsulation was missing when it was enabled.  In
 *	this case, we should tell the upper layer "tear down
 *	this session if this skb looks ok to you"
 *   2.	LLC-encapsulation was present when it was disabled.  Then
 *	we need to tell the upper layer "this packet may be
 *	ok, but if its in error tear down the session"
 * These hooks are not yet available in ppp_generic
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/capability.h>
#include <linux/ppp_defs.h>
#include <linux/ppp-ioctl.h>
#include <linux/ppp_channel.h>
#include <linux/atmppp.h>

#include "common.h"

enum pppoatm_encaps {
	e_autodetect = PPPOATM_ENCAPS_AUTODETECT,
	e_vc = PPPOATM_ENCAPS_VC,
	e_llc = PPPOATM_ENCAPS_LLC,
};

struct pppoatm_vcc {
	struct atm_vcc	*atmvcc;	/* VCC descriptor */
	void (*old_push)(struct atm_vcc *, struct sk_buff *);
	void (*old_pop)(struct atm_vcc *, struct sk_buff *);
	void (*old_release_cb)(struct atm_vcc *);
	struct module *old_owner;
					/* keep old push/pop for detaching */
	enum pppoatm_encaps encaps;
	atomic_t inflight;
	unsigned long blocked;
	int flags;			/* SC_COMP_PROT - compress protocol */
	struct ppp_channel chan;	/* interface to generic ppp layer */
	struct tasklet_struct wakeup_tasklet;
};

/*
 * We want to allow two packets in the queue. The one that's currently in
 * flight, and *one* queued up ready for the ATM device to send immediately
 * from its TX done IRQ. We want to be able to use atomic_inc_not_zero(), so
 * inflight == -2 represents an empty queue, -1 one packet, and zero means
 * there are two packets in the queue.
 */
#define NONE_INFLIGHT -2

#define BLOCKED 0

/*
 * Header used for LLC Encapsulated PPP (4 bytes) followed by the LCP protocol
 * ID (0xC021) used in autodetection
 */
static const unsigned char pppllc[6] = { 0xFE, 0xFE, 0x03, 0xCF, 0xC0, 0x21 };
#define LLC_LEN		(4)

static inline struct pppoatm_vcc *atmvcc_to_pvcc(const struct atm_vcc *atmvcc)
{
	return (struct pppoatm_vcc *) (atmvcc->user_back);
}

static inline struct pppoatm_vcc *chan_to_pvcc(const struct ppp_channel *chan)
{
	return (struct pppoatm_vcc *) (chan->private);
}

/*
 * We can't do this directly from our _pop handler, since the ppp code
 * doesn't want to be called in interrupt context, so we do it from
 * a tasklet
 */
static void pppoatm_wakeup_sender(unsigned long arg)
{
	ppp_output_wakeup((struct ppp_channel *) arg);
}

static void pppoatm_release_cb(struct atm_vcc *atmvcc)
{
	struct pppoatm_vcc *pvcc = atmvcc_to_pvcc(atmvcc);

	/*
	 * As in pppoatm_pop(), it's safe to clear the BLOCKED bit here because
	 * the wakeup *can't* race with pppoatm_send(). They both hold the PPP
	 * channel's ->downl lock. And the potential race with *setting* it,
	 * which leads to the double-check dance in pppoatm_may_send(), doesn't
	 * exist here. In the sock_owned_by_user() case in pppoatm_send(), we
	 * set the BLOCKED bit while the socket is still locked. We know that
	 * ->release_cb() can't be called until that's done.
	 */
	if (test_and_clear_bit(BLOCKED, &pvcc->blocked))
		tasklet_schedule(&pvcc->wakeup_tasklet);
	if (pvcc->old_release_cb)
		pvcc->old_release_cb(atmvcc);
}
/*
 * This gets called every time the ATM card has finished sending our
 * skb.  The ->old_pop will take care up normal atm flow control,
 * but we also need to wake up the device if we blocked it
 */
static void pppoatm_pop(struct atm_vcc *atmvcc, struct sk_buff *skb)
{
	struct pppoatm_vcc *pvcc = atmvcc_to_pvcc(atmvcc);

	pvcc->old_pop(atmvcc, skb);
	atomic_dec(&pvcc->inflight);

	/*
	 * We always used to run the wakeup tasklet unconditionally here, for
	 * fear of race conditions where we clear the BLOCKED flag just as we
	 * refuse another packet in pppoatm_send(). This was quite inefficient.
	 *
	 * In fact it's OK. The PPP core will only ever call pppoatm_send()
	 * while holding the channel->downl lock. And ppp_output_wakeup() as
	 * called by the tasklet will *also* grab that lock. So even if another
	 * CPU is in pppoatm_send() right now, the tasklet isn't going to race
	 * with it. The wakeup *will* happen after the other CPU is safely out
	 * of pppoatm_send() again.
	 *
	 * So if the CPU in pppoatm_send() has already set the BLOCKED bit and
	 * it about to return, that's fine. We trigger a wakeup which will
	 * happen later. And if the CPU in pppoatm_send() *hasn't* set the
	 * BLOCKED bit yet, that's fine too because of the double check in
	 * pppoatm_may_send() which is commented there.
	 */
	if (test_and_clear_bit(BLOCKED, &pvcc->blocked))
		tasklet_schedule(&pvcc->wakeup_tasklet);
}

/*
 * Unbind from PPP - currently we only do this when closing the socket,
 * but we could put this into an ioctl if need be
 */
static void pppoatm_unassign_vcc(struct atm_vcc *atmvcc)
{
	struct pppoatm_vcc *pvcc;
	pvcc = atmvcc_to_pvcc(atmvcc);
	atmvcc->push = pvcc->old_push;
	atmvcc->pop = pvcc->old_pop;
	atmvcc->release_cb = pvcc->old_release_cb;
	tasklet_kill(&pvcc->wakeup_tasklet);
	ppp_unregister_channel(&pvcc->chan);
	atmvcc->user_back = NULL;
	kfree(pvcc);
}

/* Called when an AAL5 PDU comes in */
static void pppoatm_push(struct atm_vcc *atmvcc, struct sk_buff *skb)
{
	struct pppoatm_vcc *pvcc = atmvcc_to_pvcc(atmvcc);
	pr_debug("\n");
	if (skb == NULL) {			/* VCC was closed */
		struct module *module;

		pr_debug("removing ATMPPP VCC %p\n", pvcc);
		module = pvcc->old_owner;
		pppoatm_unassign_vcc(atmvcc);
		atmvcc->push(atmvcc, NULL);	/* Pass along bad news */
		module_put(module);
		return;
	}
	atm_return(atmvcc, skb->truesize);
	switch (pvcc->encaps) {
	case e_llc:
		if (skb->len < LLC_LEN ||
		    memcmp(skb->data, pppllc, LLC_LEN))
			goto error;
		skb_pull(skb, LLC_LEN);
		break;
	case e_autodetect:
		if (pvcc->chan.ppp == NULL) {	/* Not bound yet! */
			kfree_skb(skb);
			return;
		}
		if (skb->len >= sizeof(pppllc) &&
		    !memcmp(skb->data, pppllc, sizeof(pppllc))) {
			pvcc->encaps = e_llc;
			skb_pull(skb, LLC_LEN);
			break;
		}
		if (skb->len >= (sizeof(pppllc) - LLC_LEN) &&
		    !memcmp(skb->data, &pppllc[LLC_LEN],
		    sizeof(pppllc) - LLC_LEN)) {
			pvcc->encaps = e_vc;
			pvcc->chan.mtu += LLC_LEN;
			break;
		}
		pr_debug("Couldn't autodetect yet (skb: %02X %02X %02X %02X %02X %02X)\n",
			 skb->data[0], skb->data[1], skb->data[2],
			 skb->data[3], skb->data[4], skb->data[5]);
		goto error;
	case e_vc:
		break;
	}
	ppp_input(&pvcc->chan, skb);
	return;

error:
	kfree_skb(skb);
	ppp_input_error(&pvcc->chan, 0);
}

static int pppoatm_may_send(struct pppoatm_vcc *pvcc, int size)
{
	/*
	 * It's not clear that we need to bother with using atm_may_send()
	 * to check we don't exceed sk->sk_sndbuf. If userspace sets a
	 * value of sk_sndbuf which is lower than the MTU, we're going to
	 * block for ever. But the code always did that before we introduced
	 * the packet count limit, so...
	 */
	if (atm_may_send(pvcc->atmvcc, size) &&
	    atomic_inc_not_zero(&pvcc->inflight))
		return 1;

	/*
	 * We use test_and_set_bit() rather than set_bit() here because
	 * we need to ensure there's a memory barrier after it. The bit
	 * *must* be set before we do the atomic_inc() on pvcc->inflight.
	 * There's no smp_mb__after_set_bit(), so it's this or abuse
	 * smp_mb__after_atomic().
	 */
	test_and_set_bit(BLOCKED, &pvcc->blocked);

	/*
	 * We may have raced with pppoatm_pop(). If it ran for the
	 * last packet in the queue, *just* before we set the BLOCKED
	 * bit, then it might never run again and the channel could
	 * remain permanently blocked. Cope with that race by checking
	 * *again*. If it did run in that window, we'll have space on
	 * the queue now and can return success. It's harmless to leave
	 * the BLOCKED flag set, since it's only used as a trigger to
	 * run the wakeup tasklet. Another wakeup will never hurt.
	 * If pppoatm_pop() is running but hasn't got as far as making
	 * space on the queue yet, then it hasn't checked the BLOCKED
	 * flag yet either, so we're safe in that case too. It'll issue
	 * an "immediate" wakeup... where "immediate" actually involves
	 * taking the PPP channel's ->downl lock, which is held by the
	 * code path that calls pppoatm_send(), and is thus going to
	 * wait for us to finish.
	 */
	if (atm_may_send(pvcc->atmvcc, size) &&
	    atomic_inc_not_zero(&pvcc->inflight))
		return 1;

	return 0;
}
/*
 * Called by the ppp_generic.c to send a packet - returns true if packet
 * was accepted.  If we return false, then it's our job to call
 * ppp_output_wakeup(chan) when we're feeling more up to it.
 * Note that in the ENOMEM case (as opposed to the !atm_may_send case)
 * we should really drop the packet, but the generic layer doesn't
 * support this yet.  We just return 'DROP_PACKET' which we actually define
 * as success, just to be clear what we're really doing.
 */
#define DROP_PACKET 1
static int pppoatm_send(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct pppoatm_vcc *pvcc = chan_to_pvcc(chan);
	struct atm_vcc *vcc;
	int ret;

	ATM_SKB(skb)->vcc = pvcc->atmvcc;
	pr_debug("(skb=0x%p, vcc=0x%p)\n", skb, pvcc->atmvcc);
	if (skb->data[0] == '\0' && (pvcc->flags & SC_COMP_PROT))
		(void) skb_pull(skb, 1);

	vcc = ATM_SKB(skb)->vcc;
	bh_lock_sock(sk_atm(vcc));
	if (sock_owned_by_user(sk_atm(vcc))) {
		/*
		 * Needs to happen (and be flushed, hence test_and_) before we unlock
		 * the socket. It needs to be seen by the time our ->release_cb gets
		 * called.
		 */
		test_and_set_bit(BLOCKED, &pvcc->blocked);
		goto nospace;
	}
	if (test_bit(ATM_VF_RELEASED, &vcc->flags) ||
	    test_bit(ATM_VF_CLOSE, &vcc->flags) ||
	    !test_bit(ATM_VF_READY, &vcc->flags)) {
		bh_unlock_sock(sk_atm(vcc));
		kfree_skb(skb);
		return DROP_PACKET;
	}

	switch (pvcc->encaps) {		/* LLC encapsulation needed */
	case e_llc:
		if (skb_headroom(skb) < LLC_LEN) {
			struct sk_buff *n;
			n = skb_realloc_headroom(skb, LLC_LEN);
			if (n != NULL &&
			    !pppoatm_may_send(pvcc, n->truesize)) {
				kfree_skb(n);
				goto nospace;
			}
			consume_skb(skb);
			skb = n;
			if (skb == NULL) {
				bh_unlock_sock(sk_atm(vcc));
				return DROP_PACKET;
			}
		} else if (!pppoatm_may_send(pvcc, skb->truesize))
			goto nospace;
		memcpy(skb_push(skb, LLC_LEN), pppllc, LLC_LEN);
		break;
	case e_vc:
		if (!pppoatm_may_send(pvcc, skb->truesize))
			goto nospace;
		break;
	case e_autodetect:
		bh_unlock_sock(sk_atm(vcc));
		pr_debug("Trying to send without setting encaps!\n");
		kfree_skb(skb);
		return 1;
	}

	atm_account_tx(vcc, skb);
	pr_debug("atm_skb(%p)->vcc(%p)->dev(%p)\n",
		 skb, ATM_SKB(skb)->vcc, ATM_SKB(skb)->vcc->dev);
	ret = ATM_SKB(skb)->vcc->send(ATM_SKB(skb)->vcc, skb)
	    ? DROP_PACKET : 1;
	bh_unlock_sock(sk_atm(vcc));
	return ret;
nospace:
	bh_unlock_sock(sk_atm(vcc));
	/*
	 * We don't have space to send this SKB now, but we might have
	 * already applied SC_COMP_PROT compression, so may need to undo
	 */
	if ((pvcc->flags & SC_COMP_PROT) && skb_headroom(skb) > 0 &&
	    skb->data[-1] == '\0')
		(void) skb_push(skb, 1);
	return 0;
}

/* This handles ioctls sent to the /dev/ppp interface */
static int pppoatm_devppp_ioctl(struct ppp_channel *chan, unsigned int cmd,
	unsigned long arg)
{
	switch (cmd) {
	case PPPIOCGFLAGS:
		return put_user(chan_to_pvcc(chan)->flags, (int __user *) arg)
		    ? -EFAULT : 0;
	case PPPIOCSFLAGS:
		return get_user(chan_to_pvcc(chan)->flags, (int __user *) arg)
		    ? -EFAULT : 0;
	}
	return -ENOTTY;
}

static const struct ppp_channel_ops pppoatm_ops = {
	.start_xmit = pppoatm_send,
	.ioctl = pppoatm_devppp_ioctl,
};

static int pppoatm_assign_vcc(struct atm_vcc *atmvcc, void __user *arg)
{
	struct atm_backend_ppp be;
	struct pppoatm_vcc *pvcc;
	int err;
	/*
	 * Each PPPoATM instance has its own tasklet - this is just a
	 * prototypical one used to initialize them
	 */
	static const DECLARE_TASKLET(tasklet_proto, pppoatm_wakeup_sender, 0);
	if (copy_from_user(&be, arg, sizeof be))
		return -EFAULT;
	if (be.encaps != PPPOATM_ENCAPS_AUTODETECT &&
	    be.encaps != PPPOATM_ENCAPS_VC && be.encaps != PPPOATM_ENCAPS_LLC)
		return -EINVAL;
	pvcc = kzalloc(sizeof(*pvcc), GFP_KERNEL);
	if (pvcc == NULL)
		return -ENOMEM;
	pvcc->atmvcc = atmvcc;

	/* Maximum is zero, so that we can use atomic_inc_not_zero() */
	atomic_set(&pvcc->inflight, NONE_INFLIGHT);
	pvcc->old_push = atmvcc->push;
	pvcc->old_pop = atmvcc->pop;
	pvcc->old_owner = atmvcc->owner;
	pvcc->old_release_cb = atmvcc->release_cb;
	pvcc->encaps = (enum pppoatm_encaps) be.encaps;
	pvcc->chan.private = pvcc;
	pvcc->chan.ops = &pppoatm_ops;
	pvcc->chan.mtu = atmvcc->qos.txtp.max_sdu - PPP_HDRLEN -
	    (be.encaps == e_vc ? 0 : LLC_LEN);
	pvcc->wakeup_tasklet = tasklet_proto;
	pvcc->wakeup_tasklet.data = (unsigned long) &pvcc->chan;
	err = ppp_register_channel(&pvcc->chan);
	if (err != 0) {
		kfree(pvcc);
		return err;
	}
	atmvcc->user_back = pvcc;
	atmvcc->push = pppoatm_push;
	atmvcc->pop = pppoatm_pop;
	atmvcc->release_cb = pppoatm_release_cb;
	__module_get(THIS_MODULE);
	atmvcc->owner = THIS_MODULE;

	/* re-process everything received between connection setup and
	   backend setup */
	vcc_process_recv_queue(atmvcc);
	return 0;
}

/*
 * This handles ioctls actually performed on our vcc - we must return
 * -ENOIOCTLCMD for any unrecognized ioctl
 */
static int pppoatm_ioctl(struct socket *sock, unsigned int cmd,
	unsigned long arg)
{
	struct atm_vcc *atmvcc = ATM_SD(sock);
	void __user *argp = (void __user *)arg;

	if (cmd != ATM_SETBACKEND && atmvcc->push != pppoatm_push)
		return -ENOIOCTLCMD;
	switch (cmd) {
	case ATM_SETBACKEND: {
		atm_backend_t b;
		if (get_user(b, (atm_backend_t __user *) argp))
			return -EFAULT;
		if (b != ATM_BACKEND_PPP)
			return -ENOIOCTLCMD;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (sock->state != SS_CONNECTED)
			return -EINVAL;
		return pppoatm_assign_vcc(atmvcc, argp);
		}
	case PPPIOCGCHAN:
		return put_user(ppp_channel_index(&atmvcc_to_pvcc(atmvcc)->
		    chan), (int __user *) argp) ? -EFAULT : 0;
	case PPPIOCGUNIT:
		return put_user(ppp_unit_number(&atmvcc_to_pvcc(atmvcc)->
		    chan), (int __user *) argp) ? -EFAULT : 0;
	}
	return -ENOIOCTLCMD;
}

static struct atm_ioctl pppoatm_ioctl_ops = {
	.owner	= THIS_MODULE,
	.ioctl	= pppoatm_ioctl,
};

static int __init pppoatm_init(void)
{
	register_atm_ioctl(&pppoatm_ioctl_ops);
	return 0;
}

static void __exit pppoatm_exit(void)
{
	deregister_atm_ioctl(&pppoatm_ioctl_ops);
}

module_init(pppoatm_init);
module_exit(pppoatm_exit);

MODULE_AUTHOR("Mitchell Blank Jr <mitch@sfgoth.com>");
MODULE_DESCRIPTION("RFC2364 PPP over ATM/AAL5");
MODULE_LICENSE("GPL");
