/*
 * xfrm_replay.c - xfrm replay detection, derived from xfrm_state.c.
 */

#include <net/xfrm.h>

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
		if (x->replay_maxdiff &&
		    (x->replay.seq - x->preplay.seq < x->replay_maxdiff) &&
		    (x->replay.oseq - x->preplay.oseq < x->replay_maxdiff)) {
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
		if (unlikely(x->replay.oseq == 0)) {
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

	if (unlikely(seq == 0))
		goto err;

	if (likely(seq > x->replay.seq))
		return 0;

	diff = x->replay.seq - seq;
	if (diff >= min_t(unsigned int, x->props.replay_window,
			  sizeof(x->replay.bitmap) * 8)) {
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
		xfrm_replay_notify(x, XFRM_REPLAY_UPDATE);
}

static struct xfrm_replay xfrm_replay_legacy = {
	.advance	= xfrm_replay_advance,
	.check		= xfrm_replay_check,
	.notify		= xfrm_replay_notify,
	.overflow	= xfrm_replay_overflow,
};

int xfrm_init_replay(struct xfrm_state *x)
{
	x->repl = &xfrm_replay_legacy;

	return 0;
}
EXPORT_SYMBOL(xfrm_init_replay);
