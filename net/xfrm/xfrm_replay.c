// SPDX-License-Identifier: GPL-2.0-only
/*
 * xfrm_replay.c - xfrm replay detection, derived from xfrm_state.c.
 *
 * Copyright (C) 2010 secunet Security Networks AG
 * Copyright (C) 2010 Steffen Klassert <steffen.klassert@secunet.com>
 */

#include <linux/export.h>
#include <net/xfrm.h>

u32 xfrm_replay_seqhi(struct xfrm_state *x, __be32 net_seq)
{
	u32 seq, seq_hi, bottom;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;

	if (!(x->props.flags & XFRM_STATE_ESN))
		return 0;

	seq = ntohl(net_seq);
	seq_hi = replay_esn->seq_hi;
	bottom = replay_esn->seq - replay_esn->replay_window + 1;

	if (likely(replay_esn->seq >= replay_esn->replay_window - 1)) {
		/* A. same subspace */
		if (unlikely(seq < bottom))
			seq_hi++;
	} else {
		/* B. window spans two subspaces */
		if (unlikely(seq >= bottom))
			seq_hi--;
	}

	return seq_hi;
}
EXPORT_SYMBOL(xfrm_replay_seqhi);
;
static void xfrm_replay_notify(struct xfrm_state *x, int event)
{
	struct km_event c;
	/* we send notify messages in case
	 *  1. we updated on of the sequence numbers, and the seqno difference
	 *     is at least x->replay_maxdiff, in this case we also update the
	 *     timeout of our timer function
	 *  2. if x->replay_maxage has elapsed since last update,
	 *     and there were changes
	 *
	 *  The state structure must be locked!
	 */

	switch (event) {
	case XFRM_REPLAY_UPDATE:
		if (!x->replay_maxdiff ||
		    ((x->replay.seq - x->preplay.seq < x->replay_maxdiff) &&
		    (x->replay.oseq - x->preplay.oseq < x->replay_maxdiff))) {
			if (x->xflags & XFRM_TIME_DEFER)
				event = XFRM_REPLAY_TIMEOUT;
			else
				return;
		}

		break;

	case XFRM_REPLAY_TIMEOUT:
		if (memcmp(&x->replay, &x->preplay,
			   sizeof(struct xfrm_replay_state)) == 0) {
			x->xflags |= XFRM_TIME_DEFER;
			return;
		}

		break;
	}

	memcpy(&x->preplay, &x->replay, sizeof(struct xfrm_replay_state));
	c.event = XFRM_MSG_NEWAE;
	c.data.aevent = event;
	km_state_notify(x, &c);

	if (x->replay_maxage &&
	    !mod_timer(&x->rtimer, jiffies + x->replay_maxage))
		x->xflags &= ~XFRM_TIME_DEFER;
}

static int xfrm_replay_overflow(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct net *net = xs_net(x);

	if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
		XFRM_SKB_CB(skb)->seq.output.low = ++x->replay.oseq;
		XFRM_SKB_CB(skb)->seq.output.hi = 0;
		if (unlikely(x->replay.oseq == 0) &&
		    !(x->props.extra_flags & XFRM_SA_XFLAG_OSEQ_MAY_WRAP)) {
			x->replay.oseq--;
			xfrm_audit_state_replay_overflow(x, skb);
			err = -EOVERFLOW;

			return err;
		}
		if (xfrm_aevent_is_on(net))
			x->repl->notify(x, XFRM_REPLAY_UPDATE);
	}

	return err;
}

static int xfrm_replay_check(struct xfrm_state *x,
		      struct sk_buff *skb, __be32 net_seq)
{
	u32 diff;
	u32 seq = ntohl(net_seq);

	if (!x->props.replay_window)
		return 0;

	if (unlikely(seq == 0))
		goto err;

	if (likely(seq > x->replay.seq))
		return 0;

	diff = x->replay.seq - seq;
	if (diff >= x->props.replay_window) {
		x->stats.replay_window++;
		goto err;
	}

	if (x->replay.bitmap & (1U << diff)) {
		x->stats.replay++;
		goto err;
	}
	return 0;

err:
	xfrm_audit_state_replay(x, skb, net_seq);
	return -EINVAL;
}

static void xfrm_replay_advance(struct xfrm_state *x, __be32 net_seq)
{
	u32 diff;
	u32 seq = ntohl(net_seq);

	if (!x->props.replay_window)
		return;

	if (seq > x->replay.seq) {
		diff = seq - x->replay.seq;
		if (diff < x->props.replay_window)
			x->replay.bitmap = ((x->replay.bitmap) << diff) | 1;
		else
			x->replay.bitmap = 1;
		x->replay.seq = seq;
	} else {
		diff = x->replay.seq - seq;
		x->replay.bitmap |= (1U << diff);
	}

	if (xfrm_aevent_is_on(xs_net(x)))
		x->repl->notify(x, XFRM_REPLAY_UPDATE);
}

static int xfrm_replay_overflow_bmp(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	struct net *net = xs_net(x);

	if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
		XFRM_SKB_CB(skb)->seq.output.low = ++replay_esn->oseq;
		XFRM_SKB_CB(skb)->seq.output.hi = 0;
		if (unlikely(replay_esn->oseq == 0) &&
		    !(x->props.extra_flags & XFRM_SA_XFLAG_OSEQ_MAY_WRAP)) {
			replay_esn->oseq--;
			xfrm_audit_state_replay_overflow(x, skb);
			err = -EOVERFLOW;

			return err;
		}
		if (xfrm_aevent_is_on(net))
			x->repl->notify(x, XFRM_REPLAY_UPDATE);
	}

	return err;
}

static int xfrm_replay_check_bmp(struct xfrm_state *x,
				 struct sk_buff *skb, __be32 net_seq)
{
	unsigned int bitnr, nr;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	u32 pos;
	u32 seq = ntohl(net_seq);
	u32 diff =  replay_esn->seq - seq;

	if (!replay_esn->replay_window)
		return 0;

	if (unlikely(seq == 0))
		goto err;

	if (likely(seq > replay_esn->seq))
		return 0;

	if (diff >= replay_esn->replay_window) {
		x->stats.replay_window++;
		goto err;
	}

	pos = (replay_esn->seq - 1) % replay_esn->replay_window;

	if (pos >= diff)
		bitnr = (pos - diff) % replay_esn->replay_window;
	else
		bitnr = replay_esn->replay_window - (diff - pos);

	nr = bitnr >> 5;
	bitnr = bitnr & 0x1F;
	if (replay_esn->bmp[nr] & (1U << bitnr))
		goto err_replay;

	return 0;

err_replay:
	x->stats.replay++;
err:
	xfrm_audit_state_replay(x, skb, net_seq);
	return -EINVAL;
}

static void xfrm_replay_advance_bmp(struct xfrm_state *x, __be32 net_seq)
{
	unsigned int bitnr, nr, i;
	u32 diff;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	u32 seq = ntohl(net_seq);
	u32 pos;

	if (!replay_esn->replay_window)
		return;

	pos = (replay_esn->seq - 1) % replay_esn->replay_window;

	if (seq > replay_esn->seq) {
		diff = seq - replay_esn->seq;

		if (diff < replay_esn->replay_window) {
			for (i = 1; i < diff; i++) {
				bitnr = (pos + i) % replay_esn->replay_window;
				nr = bitnr >> 5;
				bitnr = bitnr & 0x1F;
				replay_esn->bmp[nr] &=  ~(1U << bitnr);
			}
		} else {
			nr = (replay_esn->replay_window - 1) >> 5;
			for (i = 0; i <= nr; i++)
				replay_esn->bmp[i] = 0;
		}

		bitnr = (pos + diff) % replay_esn->replay_window;
		replay_esn->seq = seq;
	} else {
		diff = replay_esn->seq - seq;

		if (pos >= diff)
			bitnr = (pos - diff) % replay_esn->replay_window;
		else
			bitnr = replay_esn->replay_window - (diff - pos);
	}

	nr = bitnr >> 5;
	bitnr = bitnr & 0x1F;
	replay_esn->bmp[nr] |= (1U << bitnr);

	if (xfrm_aevent_is_on(xs_net(x)))
		x->repl->notify(x, XFRM_REPLAY_UPDATE);
}

static void xfrm_replay_notify_bmp(struct xfrm_state *x, int event)
{
	struct km_event c;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	struct xfrm_replay_state_esn *preplay_esn = x->preplay_esn;

	/* we send notify messages in case
	 *  1. we updated on of the sequence numbers, and the seqno difference
	 *     is at least x->replay_maxdiff, in this case we also update the
	 *     timeout of our timer function
	 *  2. if x->replay_maxage has elapsed since last update,
	 *     and there were changes
	 *
	 *  The state structure must be locked!
	 */

	switch (event) {
	case XFRM_REPLAY_UPDATE:
		if (!x->replay_maxdiff ||
		    ((replay_esn->seq - preplay_esn->seq < x->replay_maxdiff) &&
		    (replay_esn->oseq - preplay_esn->oseq
		     < x->replay_maxdiff))) {
			if (x->xflags & XFRM_TIME_DEFER)
				event = XFRM_REPLAY_TIMEOUT;
			else
				return;
		}

		break;

	case XFRM_REPLAY_TIMEOUT:
		if (memcmp(x->replay_esn, x->preplay_esn,
			   xfrm_replay_state_esn_len(replay_esn)) == 0) {
			x->xflags |= XFRM_TIME_DEFER;
			return;
		}

		break;
	}

	memcpy(x->preplay_esn, x->replay_esn,
	       xfrm_replay_state_esn_len(replay_esn));
	c.event = XFRM_MSG_NEWAE;
	c.data.aevent = event;
	km_state_notify(x, &c);

	if (x->replay_maxage &&
	    !mod_timer(&x->rtimer, jiffies + x->replay_maxage))
		x->xflags &= ~XFRM_TIME_DEFER;
}

static void xfrm_replay_notify_esn(struct xfrm_state *x, int event)
{
	u32 seq_diff, oseq_diff;
	struct km_event c;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	struct xfrm_replay_state_esn *preplay_esn = x->preplay_esn;

	/* we send notify messages in case
	 *  1. we updated on of the sequence numbers, and the seqno difference
	 *     is at least x->replay_maxdiff, in this case we also update the
	 *     timeout of our timer function
	 *  2. if x->replay_maxage has elapsed since last update,
	 *     and there were changes
	 *
	 *  The state structure must be locked!
	 */

	switch (event) {
	case XFRM_REPLAY_UPDATE:
		if (x->replay_maxdiff) {
			if (replay_esn->seq_hi == preplay_esn->seq_hi)
				seq_diff = replay_esn->seq - preplay_esn->seq;
			else
				seq_diff = ~preplay_esn->seq + replay_esn->seq
					   + 1;

			if (replay_esn->oseq_hi == preplay_esn->oseq_hi)
				oseq_diff = replay_esn->oseq
					    - preplay_esn->oseq;
			else
				oseq_diff = ~preplay_esn->oseq
					    + replay_esn->oseq + 1;

			if (seq_diff >= x->replay_maxdiff ||
			    oseq_diff >= x->replay_maxdiff)
				break;
		}

		if (x->xflags & XFRM_TIME_DEFER)
			event = XFRM_REPLAY_TIMEOUT;
		else
			return;

		break;

	case XFRM_REPLAY_TIMEOUT:
		if (memcmp(x->replay_esn, x->preplay_esn,
			   xfrm_replay_state_esn_len(replay_esn)) == 0) {
			x->xflags |= XFRM_TIME_DEFER;
			return;
		}

		break;
	}

	memcpy(x->preplay_esn, x->replay_esn,
	       xfrm_replay_state_esn_len(replay_esn));
	c.event = XFRM_MSG_NEWAE;
	c.data.aevent = event;
	km_state_notify(x, &c);

	if (x->replay_maxage &&
	    !mod_timer(&x->rtimer, jiffies + x->replay_maxage))
		x->xflags &= ~XFRM_TIME_DEFER;
}

static int xfrm_replay_overflow_esn(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	struct net *net = xs_net(x);

	if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
		XFRM_SKB_CB(skb)->seq.output.low = ++replay_esn->oseq;
		XFRM_SKB_CB(skb)->seq.output.hi = replay_esn->oseq_hi;

		if (unlikely(replay_esn->oseq == 0)) {
			XFRM_SKB_CB(skb)->seq.output.hi = ++replay_esn->oseq_hi;

			if (replay_esn->oseq_hi == 0) {
				replay_esn->oseq--;
				replay_esn->oseq_hi--;
				xfrm_audit_state_replay_overflow(x, skb);
				err = -EOVERFLOW;

				return err;
			}
		}
		if (xfrm_aevent_is_on(net))
			x->repl->notify(x, XFRM_REPLAY_UPDATE);
	}

	return err;
}

static int xfrm_replay_check_esn(struct xfrm_state *x,
				 struct sk_buff *skb, __be32 net_seq)
{
	unsigned int bitnr, nr;
	u32 diff;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	u32 pos;
	u32 seq = ntohl(net_seq);
	u32 wsize = replay_esn->replay_window;
	u32 top = replay_esn->seq;
	u32 bottom = top - wsize + 1;

	if (!wsize)
		return 0;

	if (unlikely(seq == 0 && replay_esn->seq_hi == 0 &&
		     (replay_esn->seq < replay_esn->replay_window - 1)))
		goto err;

	diff = top - seq;

	if (likely(top >= wsize - 1)) {
		/* A. same subspace */
		if (likely(seq > top) || seq < bottom)
			return 0;
	} else {
		/* B. window spans two subspaces */
		if (likely(seq > top && seq < bottom))
			return 0;
		if (seq >= bottom)
			diff = ~seq + top + 1;
	}

	if (diff >= replay_esn->replay_window) {
		x->stats.replay_window++;
		goto err;
	}

	pos = (replay_esn->seq - 1) % replay_esn->replay_window;

	if (pos >= diff)
		bitnr = (pos - diff) % replay_esn->replay_window;
	else
		bitnr = replay_esn->replay_window - (diff - pos);

	nr = bitnr >> 5;
	bitnr = bitnr & 0x1F;
	if (replay_esn->bmp[nr] & (1U << bitnr))
		goto err_replay;

	return 0;

err_replay:
	x->stats.replay++;
err:
	xfrm_audit_state_replay(x, skb, net_seq);
	return -EINVAL;
}

static int xfrm_replay_recheck_esn(struct xfrm_state *x,
				   struct sk_buff *skb, __be32 net_seq)
{
	if (unlikely(XFRM_SKB_CB(skb)->seq.input.hi !=
		     htonl(xfrm_replay_seqhi(x, net_seq)))) {
			x->stats.replay_window++;
			return -EINVAL;
	}

	return xfrm_replay_check_esn(x, skb, net_seq);
}

static void xfrm_replay_advance_esn(struct xfrm_state *x, __be32 net_seq)
{
	unsigned int bitnr, nr, i;
	int wrap;
	u32 diff, pos, seq, seq_hi;
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;

	if (!replay_esn->replay_window)
		return;

	seq = ntohl(net_seq);
	pos = (replay_esn->seq - 1) % replay_esn->replay_window;
	seq_hi = xfrm_replay_seqhi(x, net_seq);
	wrap = seq_hi - replay_esn->seq_hi;

	if ((!wrap && seq > replay_esn->seq) || wrap > 0) {
		if (likely(!wrap))
			diff = seq - replay_esn->seq;
		else
			diff = ~replay_esn->seq + seq + 1;

		if (diff < replay_esn->replay_window) {
			for (i = 1; i < diff; i++) {
				bitnr = (pos + i) % replay_esn->replay_window;
				nr = bitnr >> 5;
				bitnr = bitnr & 0x1F;
				replay_esn->bmp[nr] &=  ~(1U << bitnr);
			}
		} else {
			nr = (replay_esn->replay_window - 1) >> 5;
			for (i = 0; i <= nr; i++)
				replay_esn->bmp[i] = 0;
		}

		bitnr = (pos + diff) % replay_esn->replay_window;
		replay_esn->seq = seq;

		if (unlikely(wrap > 0))
			replay_esn->seq_hi++;
	} else {
		diff = replay_esn->seq - seq;

		if (pos >= diff)
			bitnr = (pos - diff) % replay_esn->replay_window;
		else
			bitnr = replay_esn->replay_window - (diff - pos);
	}

	xfrm_dev_state_advance_esn(x);

	nr = bitnr >> 5;
	bitnr = bitnr & 0x1F;
	replay_esn->bmp[nr] |= (1U << bitnr);

	if (xfrm_aevent_is_on(xs_net(x)))
		x->repl->notify(x, XFRM_REPLAY_UPDATE);
}

#ifdef CONFIG_XFRM_OFFLOAD
static int xfrm_replay_overflow_offload(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct net *net = xs_net(x);
	struct xfrm_offload *xo = xfrm_offload(skb);
	__u32 oseq = x->replay.oseq;

	if (!xo)
		return xfrm_replay_overflow(x, skb);

	if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
		if (!skb_is_gso(skb)) {
			XFRM_SKB_CB(skb)->seq.output.low = ++oseq;
			xo->seq.low = oseq;
		} else {
			XFRM_SKB_CB(skb)->seq.output.low = oseq + 1;
			xo->seq.low = oseq + 1;
			oseq += skb_shinfo(skb)->gso_segs;
		}

		XFRM_SKB_CB(skb)->seq.output.hi = 0;
		xo->seq.hi = 0;
		if (unlikely(oseq < x->replay.oseq) &&
		    !(x->props.extra_flags & XFRM_SA_XFLAG_OSEQ_MAY_WRAP)) {
			xfrm_audit_state_replay_overflow(x, skb);
			err = -EOVERFLOW;

			return err;
		}

		x->replay.oseq = oseq;

		if (xfrm_aevent_is_on(net))
			x->repl->notify(x, XFRM_REPLAY_UPDATE);
	}

	return err;
}

static int xfrm_replay_overflow_offload_bmp(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	struct net *net = xs_net(x);
	__u32 oseq = replay_esn->oseq;

	if (!xo)
		return xfrm_replay_overflow_bmp(x, skb);

	if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
		if (!skb_is_gso(skb)) {
			XFRM_SKB_CB(skb)->seq.output.low = ++oseq;
			xo->seq.low = oseq;
		} else {
			XFRM_SKB_CB(skb)->seq.output.low = oseq + 1;
			xo->seq.low = oseq + 1;
			oseq += skb_shinfo(skb)->gso_segs;
		}

		XFRM_SKB_CB(skb)->seq.output.hi = 0;
		xo->seq.hi = 0;
		if (unlikely(oseq < replay_esn->oseq) &&
		    !(x->props.extra_flags & XFRM_SA_XFLAG_OSEQ_MAY_WRAP)) {
			xfrm_audit_state_replay_overflow(x, skb);
			err = -EOVERFLOW;

			return err;
		} else {
			replay_esn->oseq = oseq;
		}

		if (xfrm_aevent_is_on(net))
			x->repl->notify(x, XFRM_REPLAY_UPDATE);
	}

	return err;
}

static int xfrm_replay_overflow_offload_esn(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = 0;
	struct xfrm_offload *xo = xfrm_offload(skb);
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;
	struct net *net = xs_net(x);
	__u32 oseq = replay_esn->oseq;
	__u32 oseq_hi = replay_esn->oseq_hi;

	if (!xo)
		return xfrm_replay_overflow_esn(x, skb);

	if (x->type->flags & XFRM_TYPE_REPLAY_PROT) {
		if (!skb_is_gso(skb)) {
			XFRM_SKB_CB(skb)->seq.output.low = ++oseq;
			XFRM_SKB_CB(skb)->seq.output.hi = oseq_hi;
			xo->seq.low = oseq;
			xo->seq.hi = oseq_hi;
		} else {
			XFRM_SKB_CB(skb)->seq.output.low = oseq + 1;
			XFRM_SKB_CB(skb)->seq.output.hi = oseq_hi;
			xo->seq.low = oseq + 1;
			xo->seq.hi = oseq_hi;
			oseq += skb_shinfo(skb)->gso_segs;
		}

		if (unlikely(oseq < replay_esn->oseq)) {
			XFRM_SKB_CB(skb)->seq.output.hi = ++oseq_hi;
			xo->seq.hi = oseq_hi;
			replay_esn->oseq_hi = oseq_hi;
			if (replay_esn->oseq_hi == 0) {
				replay_esn->oseq--;
				replay_esn->oseq_hi--;
				xfrm_audit_state_replay_overflow(x, skb);
				err = -EOVERFLOW;

				return err;
			}
		}

		replay_esn->oseq = oseq;

		if (xfrm_aevent_is_on(net))
			x->repl->notify(x, XFRM_REPLAY_UPDATE);
	}

	return err;
}

static const struct xfrm_replay xfrm_replay_legacy = {
	.advance	= xfrm_replay_advance,
	.check		= xfrm_replay_check,
	.recheck	= xfrm_replay_check,
	.notify		= xfrm_replay_notify,
	.overflow	= xfrm_replay_overflow_offload,
};

static const struct xfrm_replay xfrm_replay_bmp = {
	.advance	= xfrm_replay_advance_bmp,
	.check		= xfrm_replay_check_bmp,
	.recheck	= xfrm_replay_check_bmp,
	.notify		= xfrm_replay_notify_bmp,
	.overflow	= xfrm_replay_overflow_offload_bmp,
};

static const struct xfrm_replay xfrm_replay_esn = {
	.advance	= xfrm_replay_advance_esn,
	.check		= xfrm_replay_check_esn,
	.recheck	= xfrm_replay_recheck_esn,
	.notify		= xfrm_replay_notify_esn,
	.overflow	= xfrm_replay_overflow_offload_esn,
};
#else
static const struct xfrm_replay xfrm_replay_legacy = {
	.advance	= xfrm_replay_advance,
	.check		= xfrm_replay_check,
	.recheck	= xfrm_replay_check,
	.notify		= xfrm_replay_notify,
	.overflow	= xfrm_replay_overflow,
};

static const struct xfrm_replay xfrm_replay_bmp = {
	.advance	= xfrm_replay_advance_bmp,
	.check		= xfrm_replay_check_bmp,
	.recheck	= xfrm_replay_check_bmp,
	.notify		= xfrm_replay_notify_bmp,
	.overflow	= xfrm_replay_overflow_bmp,
};

static const struct xfrm_replay xfrm_replay_esn = {
	.advance	= xfrm_replay_advance_esn,
	.check		= xfrm_replay_check_esn,
	.recheck	= xfrm_replay_recheck_esn,
	.notify		= xfrm_replay_notify_esn,
	.overflow	= xfrm_replay_overflow_esn,
};
#endif

int xfrm_init_replay(struct xfrm_state *x)
{
	struct xfrm_replay_state_esn *replay_esn = x->replay_esn;

	if (replay_esn) {
		if (replay_esn->replay_window >
		    replay_esn->bmp_len * sizeof(__u32) * 8)
			return -EINVAL;

		if (x->props.flags & XFRM_STATE_ESN) {
			if (replay_esn->replay_window == 0)
				return -EINVAL;
			x->repl = &xfrm_replay_esn;
		} else {
			x->repl = &xfrm_replay_bmp;
		}
	} else {
		x->repl = &xfrm_replay_legacy;
	}

	return 0;
}
EXPORT_SYMBOL(xfrm_init_replay);
