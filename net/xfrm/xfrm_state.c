/*
 * xfrm_state.c
 *
 * Changes:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 * 		IPv6 support
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific functions
 *	Derek Atkins <derek@ihtfp.com>
 *		Add UDP Encapsulation
 *
 */

#include <linux/workqueue.h>
#include <net/xfrm.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <linux/module.h>
#include <asm/uaccess.h>

struct sock *xfrm_nl;
EXPORT_SYMBOL(xfrm_nl);

u32 sysctl_xfrm_aevent_etime = XFRM_AE_ETIME;
EXPORT_SYMBOL(sysctl_xfrm_aevent_etime);

u32 sysctl_xfrm_aevent_rseqth = XFRM_AE_SEQT_SIZE;
EXPORT_SYMBOL(sysctl_xfrm_aevent_rseqth);

/* Each xfrm_state may be linked to two tables:

   1. Hash table by (spi,daddr,ah/esp) to find SA by SPI. (input,ctl)
   2. Hash table by daddr to find what SAs exist for given
      destination/tunnel endpoint. (output)
 */

static DEFINE_SPINLOCK(xfrm_state_lock);

/* Hash table to find appropriate SA towards given target (endpoint
 * of tunnel or destination of transport mode) allowed by selector.
 *
 * Main use is finding SA after policy selected tunnel or transport mode.
 * Also, it can be used by ah/esp icmp error handler to find offending SA.
 */
static struct list_head xfrm_state_bydst[XFRM_DST_HSIZE];
static struct list_head xfrm_state_byspi[XFRM_DST_HSIZE];

DECLARE_WAIT_QUEUE_HEAD(km_waitq);
EXPORT_SYMBOL(km_waitq);

static DEFINE_RWLOCK(xfrm_state_afinfo_lock);
static struct xfrm_state_afinfo *xfrm_state_afinfo[NPROTO];

static struct work_struct xfrm_state_gc_work;
static struct list_head xfrm_state_gc_list = LIST_HEAD_INIT(xfrm_state_gc_list);
static DEFINE_SPINLOCK(xfrm_state_gc_lock);

static int xfrm_state_gc_flush_bundles;

int __xfrm_state_delete(struct xfrm_state *x);

static struct xfrm_state_afinfo *xfrm_state_get_afinfo(unsigned short family);
static void xfrm_state_put_afinfo(struct xfrm_state_afinfo *afinfo);

int km_query(struct xfrm_state *x, struct xfrm_tmpl *t, struct xfrm_policy *pol);
void km_state_expired(struct xfrm_state *x, int hard, u32 pid);

static void xfrm_state_gc_destroy(struct xfrm_state *x)
{
	if (del_timer(&x->timer))
		BUG();
	if (del_timer(&x->rtimer))
		BUG();
	kfree(x->aalg);
	kfree(x->ealg);
	kfree(x->calg);
	kfree(x->encap);
	if (x->type) {
		x->type->destructor(x);
		xfrm_put_type(x->type);
	}
	security_xfrm_state_free(x);
	kfree(x);
}

static void xfrm_state_gc_task(void *data)
{
	struct xfrm_state *x;
	struct list_head *entry, *tmp;
	struct list_head gc_list = LIST_HEAD_INIT(gc_list);

	if (xfrm_state_gc_flush_bundles) {
		xfrm_state_gc_flush_bundles = 0;
		xfrm_flush_bundles();
	}

	spin_lock_bh(&xfrm_state_gc_lock);
	list_splice_init(&xfrm_state_gc_list, &gc_list);
	spin_unlock_bh(&xfrm_state_gc_lock);

	list_for_each_safe(entry, tmp, &gc_list) {
		x = list_entry(entry, struct xfrm_state, bydst);
		xfrm_state_gc_destroy(x);
	}
	wake_up(&km_waitq);
}

static inline unsigned long make_jiffies(long secs)
{
	if (secs >= (MAX_SCHEDULE_TIMEOUT-1)/HZ)
		return MAX_SCHEDULE_TIMEOUT-1;
	else
	        return secs*HZ;
}

static void xfrm_timer_handler(unsigned long data)
{
	struct xfrm_state *x = (struct xfrm_state*)data;
	unsigned long now = (unsigned long)xtime.tv_sec;
	long next = LONG_MAX;
	int warn = 0;

	spin_lock(&x->lock);
	if (x->km.state == XFRM_STATE_DEAD)
		goto out;
	if (x->km.state == XFRM_STATE_EXPIRED)
		goto expired;
	if (x->lft.hard_add_expires_seconds) {
		long tmo = x->lft.hard_add_expires_seconds +
			x->curlft.add_time - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (x->lft.hard_use_expires_seconds) {
		long tmo = x->lft.hard_use_expires_seconds +
			(x->curlft.use_time ? : now) - now;
		if (tmo <= 0)
			goto expired;
		if (tmo < next)
			next = tmo;
	}
	if (x->km.dying)
		goto resched;
	if (x->lft.soft_add_expires_seconds) {
		long tmo = x->lft.soft_add_expires_seconds +
			x->curlft.add_time - now;
		if (tmo <= 0)
			warn = 1;
		else if (tmo < next)
			next = tmo;
	}
	if (x->lft.soft_use_expires_seconds) {
		long tmo = x->lft.soft_use_expires_seconds +
			(x->curlft.use_time ? : now) - now;
		if (tmo <= 0)
			warn = 1;
		else if (tmo < next)
			next = tmo;
	}

	x->km.dying = warn;
	if (warn)
		km_state_expired(x, 0, 0);
resched:
	if (next != LONG_MAX &&
	    !mod_timer(&x->timer, jiffies + make_jiffies(next)))
		xfrm_state_hold(x);
	goto out;

expired:
	if (x->km.state == XFRM_STATE_ACQ && x->id.spi == 0) {
		x->km.state = XFRM_STATE_EXPIRED;
		wake_up(&km_waitq);
		next = 2;
		goto resched;
	}
	if (!__xfrm_state_delete(x) && x->id.spi)
		km_state_expired(x, 1, 0);

out:
	spin_unlock(&x->lock);
	xfrm_state_put(x);
}

static void xfrm_replay_timer_handler(unsigned long data);

struct xfrm_state *xfrm_state_alloc(void)
{
	struct xfrm_state *x;

	x = kmalloc(sizeof(struct xfrm_state), GFP_ATOMIC);

	if (x) {
		memset(x, 0, sizeof(struct xfrm_state));
		atomic_set(&x->refcnt, 1);
		atomic_set(&x->tunnel_users, 0);
		INIT_LIST_HEAD(&x->bydst);
		INIT_LIST_HEAD(&x->byspi);
		init_timer(&x->timer);
		x->timer.function = xfrm_timer_handler;
		x->timer.data	  = (unsigned long)x;
		init_timer(&x->rtimer);
		x->rtimer.function = xfrm_replay_timer_handler;
		x->rtimer.data     = (unsigned long)x;
		x->curlft.add_time = (unsigned long)xtime.tv_sec;
		x->lft.soft_byte_limit = XFRM_INF;
		x->lft.soft_packet_limit = XFRM_INF;
		x->lft.hard_byte_limit = XFRM_INF;
		x->lft.hard_packet_limit = XFRM_INF;
		x->replay_maxage = 0;
		x->replay_maxdiff = 0;
		spin_lock_init(&x->lock);
	}
	return x;
}
EXPORT_SYMBOL(xfrm_state_alloc);

void __xfrm_state_destroy(struct xfrm_state *x)
{
	BUG_TRAP(x->km.state == XFRM_STATE_DEAD);

	spin_lock_bh(&xfrm_state_gc_lock);
	list_add(&x->bydst, &xfrm_state_gc_list);
	spin_unlock_bh(&xfrm_state_gc_lock);
	schedule_work(&xfrm_state_gc_work);
}
EXPORT_SYMBOL(__xfrm_state_destroy);

int __xfrm_state_delete(struct xfrm_state *x)
{
	int err = -ESRCH;

	if (x->km.state != XFRM_STATE_DEAD) {
		x->km.state = XFRM_STATE_DEAD;
		spin_lock(&xfrm_state_lock);
		list_del(&x->bydst);
		__xfrm_state_put(x);
		if (x->id.spi) {
			list_del(&x->byspi);
			__xfrm_state_put(x);
		}
		spin_unlock(&xfrm_state_lock);
		if (del_timer(&x->timer))
			__xfrm_state_put(x);
		if (del_timer(&x->rtimer))
			__xfrm_state_put(x);

		/* The number two in this test is the reference
		 * mentioned in the comment below plus the reference
		 * our caller holds.  A larger value means that
		 * there are DSTs attached to this xfrm_state.
		 */
		if (atomic_read(&x->refcnt) > 2) {
			xfrm_state_gc_flush_bundles = 1;
			schedule_work(&xfrm_state_gc_work);
		}

		/* All xfrm_state objects are created by xfrm_state_alloc.
		 * The xfrm_state_alloc call gives a reference, and that
		 * is what we are dropping here.
		 */
		__xfrm_state_put(x);
		err = 0;
	}

	return err;
}
EXPORT_SYMBOL(__xfrm_state_delete);

int xfrm_state_delete(struct xfrm_state *x)
{
	int err;

	spin_lock_bh(&x->lock);
	err = __xfrm_state_delete(x);
	spin_unlock_bh(&x->lock);

	return err;
}
EXPORT_SYMBOL(xfrm_state_delete);

void xfrm_state_flush(u8 proto)
{
	int i;
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	for (i = 0; i < XFRM_DST_HSIZE; i++) {
restart:
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (!xfrm_state_kern(x) &&
			    (proto == IPSEC_PROTO_ANY || x->id.proto == proto)) {
				xfrm_state_hold(x);
				spin_unlock_bh(&xfrm_state_lock);

				xfrm_state_delete(x);
				xfrm_state_put(x);

				spin_lock_bh(&xfrm_state_lock);
				goto restart;
			}
		}
	}
	spin_unlock_bh(&xfrm_state_lock);
	wake_up(&km_waitq);
}
EXPORT_SYMBOL(xfrm_state_flush);

static int
xfrm_init_tempsel(struct xfrm_state *x, struct flowi *fl,
		  struct xfrm_tmpl *tmpl,
		  xfrm_address_t *daddr, xfrm_address_t *saddr,
		  unsigned short family)
{
	struct xfrm_state_afinfo *afinfo = xfrm_state_get_afinfo(family);
	if (!afinfo)
		return -1;
	afinfo->init_tempsel(x, fl, tmpl, daddr, saddr);
	xfrm_state_put_afinfo(afinfo);
	return 0;
}

struct xfrm_state *
xfrm_state_find(xfrm_address_t *daddr, xfrm_address_t *saddr, 
		struct flowi *fl, struct xfrm_tmpl *tmpl,
		struct xfrm_policy *pol, int *err,
		unsigned short family)
{
	unsigned h = xfrm_dst_hash(daddr, family);
	struct xfrm_state *x, *x0;
	int acquire_in_progress = 0;
	int error = 0;
	struct xfrm_state *best = NULL;
	struct xfrm_state_afinfo *afinfo;
	
	afinfo = xfrm_state_get_afinfo(family);
	if (afinfo == NULL) {
		*err = -EAFNOSUPPORT;
		return NULL;
	}

	spin_lock_bh(&xfrm_state_lock);
	list_for_each_entry(x, xfrm_state_bydst+h, bydst) {
		if (x->props.family == family &&
		    x->props.reqid == tmpl->reqid &&
		    xfrm_state_addr_check(x, daddr, saddr, family) &&
		    tmpl->mode == x->props.mode &&
		    tmpl->id.proto == x->id.proto &&
		    (tmpl->id.spi == x->id.spi || !tmpl->id.spi)) {
			/* Resolution logic:
			   1. There is a valid state with matching selector.
			      Done.
			   2. Valid state with inappropriate selector. Skip.

			   Entering area of "sysdeps".

			   3. If state is not valid, selector is temporary,
			      it selects only session which triggered
			      previous resolution. Key manager will do
			      something to install a state with proper
			      selector.
			 */
			if (x->km.state == XFRM_STATE_VALID) {
				if (!xfrm_selector_match(&x->sel, fl, family) ||
				    !xfrm_sec_ctx_match(pol->security, x->security))
					continue;
				if (!best ||
				    best->km.dying > x->km.dying ||
				    (best->km.dying == x->km.dying &&
				     best->curlft.add_time < x->curlft.add_time))
					best = x;
			} else if (x->km.state == XFRM_STATE_ACQ) {
				acquire_in_progress = 1;
			} else if (x->km.state == XFRM_STATE_ERROR ||
				   x->km.state == XFRM_STATE_EXPIRED) {
 				if (xfrm_selector_match(&x->sel, fl, family) &&
				    xfrm_sec_ctx_match(pol->security, x->security))
					error = -ESRCH;
			}
		}
	}

	x = best;
	if (!x && !error && !acquire_in_progress) {
		if (tmpl->id.spi &&
		    (x0 = afinfo->state_lookup(daddr, tmpl->id.spi,
		                               tmpl->id.proto)) != NULL) {
			xfrm_state_put(x0);
			error = -EEXIST;
			goto out;
		}
		x = xfrm_state_alloc();
		if (x == NULL) {
			error = -ENOMEM;
			goto out;
		}
		/* Initialize temporary selector matching only
		 * to current session. */
		xfrm_init_tempsel(x, fl, tmpl, daddr, saddr, family);

		if (km_query(x, tmpl, pol) == 0) {
			x->km.state = XFRM_STATE_ACQ;
			list_add_tail(&x->bydst, xfrm_state_bydst+h);
			xfrm_state_hold(x);
			if (x->id.spi) {
				h = xfrm_spi_hash(&x->id.daddr, x->id.spi, x->id.proto, family);
				list_add(&x->byspi, xfrm_state_byspi+h);
				xfrm_state_hold(x);
			}
			x->lft.hard_add_expires_seconds = XFRM_ACQ_EXPIRES;
			xfrm_state_hold(x);
			x->timer.expires = jiffies + XFRM_ACQ_EXPIRES*HZ;
			add_timer(&x->timer);
		} else {
			x->km.state = XFRM_STATE_DEAD;
			xfrm_state_put(x);
			x = NULL;
			error = -ESRCH;
		}
	}
out:
	if (x)
		xfrm_state_hold(x);
	else
		*err = acquire_in_progress ? -EAGAIN : error;
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);
	return x;
}

static void __xfrm_state_insert(struct xfrm_state *x)
{
	unsigned h = xfrm_dst_hash(&x->id.daddr, x->props.family);

	list_add(&x->bydst, xfrm_state_bydst+h);
	xfrm_state_hold(x);

	h = xfrm_spi_hash(&x->id.daddr, x->id.spi, x->id.proto, x->props.family);

	list_add(&x->byspi, xfrm_state_byspi+h);
	xfrm_state_hold(x);

	if (!mod_timer(&x->timer, jiffies + HZ))
		xfrm_state_hold(x);

	if (x->replay_maxage &&
	    !mod_timer(&x->rtimer, jiffies + x->replay_maxage))
		xfrm_state_hold(x);

	wake_up(&km_waitq);
}

void xfrm_state_insert(struct xfrm_state *x)
{
	spin_lock_bh(&xfrm_state_lock);
	__xfrm_state_insert(x);
	spin_unlock_bh(&xfrm_state_lock);

	xfrm_flush_all_bundles();
}
EXPORT_SYMBOL(xfrm_state_insert);

static struct xfrm_state *__xfrm_find_acq_byseq(u32 seq);

int xfrm_state_add(struct xfrm_state *x)
{
	struct xfrm_state_afinfo *afinfo;
	struct xfrm_state *x1;
	int family;
	int err;

	family = x->props.family;
	afinfo = xfrm_state_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	spin_lock_bh(&xfrm_state_lock);

	x1 = afinfo->state_lookup(&x->id.daddr, x->id.spi, x->id.proto);
	if (x1) {
		xfrm_state_put(x1);
		x1 = NULL;
		err = -EEXIST;
		goto out;
	}

	if (x->km.seq) {
		x1 = __xfrm_find_acq_byseq(x->km.seq);
		if (x1 && xfrm_addr_cmp(&x1->id.daddr, &x->id.daddr, family)) {
			xfrm_state_put(x1);
			x1 = NULL;
		}
	}

	if (!x1)
		x1 = afinfo->find_acq(
			x->props.mode, x->props.reqid, x->id.proto,
			&x->id.daddr, &x->props.saddr, 0);

	__xfrm_state_insert(x);
	err = 0;

out:
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);

	if (!err)
		xfrm_flush_all_bundles();

	if (x1) {
		xfrm_state_delete(x1);
		xfrm_state_put(x1);
	}

	return err;
}
EXPORT_SYMBOL(xfrm_state_add);

int xfrm_state_update(struct xfrm_state *x)
{
	struct xfrm_state_afinfo *afinfo;
	struct xfrm_state *x1;
	int err;

	afinfo = xfrm_state_get_afinfo(x->props.family);
	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	spin_lock_bh(&xfrm_state_lock);
	x1 = afinfo->state_lookup(&x->id.daddr, x->id.spi, x->id.proto);

	err = -ESRCH;
	if (!x1)
		goto out;

	if (xfrm_state_kern(x1)) {
		xfrm_state_put(x1);
		err = -EEXIST;
		goto out;
	}

	if (x1->km.state == XFRM_STATE_ACQ) {
		__xfrm_state_insert(x);
		x = NULL;
	}
	err = 0;

out:
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);

	if (err)
		return err;

	if (!x) {
		xfrm_state_delete(x1);
		xfrm_state_put(x1);
		return 0;
	}

	err = -EINVAL;
	spin_lock_bh(&x1->lock);
	if (likely(x1->km.state == XFRM_STATE_VALID)) {
		if (x->encap && x1->encap)
			memcpy(x1->encap, x->encap, sizeof(*x1->encap));
		memcpy(&x1->lft, &x->lft, sizeof(x1->lft));
		x1->km.dying = 0;

		if (!mod_timer(&x1->timer, jiffies + HZ))
			xfrm_state_hold(x1);
		if (x1->curlft.use_time)
			xfrm_state_check_expire(x1);

		err = 0;
	}
	spin_unlock_bh(&x1->lock);

	xfrm_state_put(x1);

	return err;
}
EXPORT_SYMBOL(xfrm_state_update);

int xfrm_state_check_expire(struct xfrm_state *x)
{
	if (!x->curlft.use_time)
		x->curlft.use_time = (unsigned long)xtime.tv_sec;

	if (x->km.state != XFRM_STATE_VALID)
		return -EINVAL;

	if (x->curlft.bytes >= x->lft.hard_byte_limit ||
	    x->curlft.packets >= x->lft.hard_packet_limit) {
		x->km.state = XFRM_STATE_EXPIRED;
		if (!mod_timer(&x->timer, jiffies))
			xfrm_state_hold(x);
		return -EINVAL;
	}

	if (!x->km.dying &&
	    (x->curlft.bytes >= x->lft.soft_byte_limit ||
	     x->curlft.packets >= x->lft.soft_packet_limit)) {
		x->km.dying = 1;
		km_state_expired(x, 0, 0);
	}
	return 0;
}
EXPORT_SYMBOL(xfrm_state_check_expire);

static int xfrm_state_check_space(struct xfrm_state *x, struct sk_buff *skb)
{
	int nhead = x->props.header_len + LL_RESERVED_SPACE(skb->dst->dev)
		- skb_headroom(skb);

	if (nhead > 0)
		return pskb_expand_head(skb, nhead, 0, GFP_ATOMIC);

	/* Check tail too... */
	return 0;
}

int xfrm_state_check(struct xfrm_state *x, struct sk_buff *skb)
{
	int err = xfrm_state_check_expire(x);
	if (err < 0)
		goto err;
	err = xfrm_state_check_space(x, skb);
err:
	return err;
}
EXPORT_SYMBOL(xfrm_state_check);

struct xfrm_state *
xfrm_state_lookup(xfrm_address_t *daddr, u32 spi, u8 proto,
		  unsigned short family)
{
	struct xfrm_state *x;
	struct xfrm_state_afinfo *afinfo = xfrm_state_get_afinfo(family);
	if (!afinfo)
		return NULL;

	spin_lock_bh(&xfrm_state_lock);
	x = afinfo->state_lookup(daddr, spi, proto);
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);
	return x;
}
EXPORT_SYMBOL(xfrm_state_lookup);

struct xfrm_state *
xfrm_find_acq(u8 mode, u32 reqid, u8 proto, 
	      xfrm_address_t *daddr, xfrm_address_t *saddr, 
	      int create, unsigned short family)
{
	struct xfrm_state *x;
	struct xfrm_state_afinfo *afinfo = xfrm_state_get_afinfo(family);
	if (!afinfo)
		return NULL;

	spin_lock_bh(&xfrm_state_lock);
	x = afinfo->find_acq(mode, reqid, proto, daddr, saddr, create);
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);
	return x;
}
EXPORT_SYMBOL(xfrm_find_acq);

/* Silly enough, but I'm lazy to build resolution list */

static struct xfrm_state *__xfrm_find_acq_byseq(u32 seq)
{
	int i;
	struct xfrm_state *x;

	for (i = 0; i < XFRM_DST_HSIZE; i++) {
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (x->km.seq == seq && x->km.state == XFRM_STATE_ACQ) {
				xfrm_state_hold(x);
				return x;
			}
		}
	}
	return NULL;
}

struct xfrm_state *xfrm_find_acq_byseq(u32 seq)
{
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	x = __xfrm_find_acq_byseq(seq);
	spin_unlock_bh(&xfrm_state_lock);
	return x;
}
EXPORT_SYMBOL(xfrm_find_acq_byseq);

u32 xfrm_get_acqseq(void)
{
	u32 res;
	static u32 acqseq;
	static DEFINE_SPINLOCK(acqseq_lock);

	spin_lock_bh(&acqseq_lock);
	res = (++acqseq ? : ++acqseq);
	spin_unlock_bh(&acqseq_lock);
	return res;
}
EXPORT_SYMBOL(xfrm_get_acqseq);

void
xfrm_alloc_spi(struct xfrm_state *x, u32 minspi, u32 maxspi)
{
	u32 h;
	struct xfrm_state *x0;

	if (x->id.spi)
		return;

	if (minspi == maxspi) {
		x0 = xfrm_state_lookup(&x->id.daddr, minspi, x->id.proto, x->props.family);
		if (x0) {
			xfrm_state_put(x0);
			return;
		}
		x->id.spi = minspi;
	} else {
		u32 spi = 0;
		minspi = ntohl(minspi);
		maxspi = ntohl(maxspi);
		for (h=0; h<maxspi-minspi+1; h++) {
			spi = minspi + net_random()%(maxspi-minspi+1);
			x0 = xfrm_state_lookup(&x->id.daddr, htonl(spi), x->id.proto, x->props.family);
			if (x0 == NULL) {
				x->id.spi = htonl(spi);
				break;
			}
			xfrm_state_put(x0);
		}
	}
	if (x->id.spi) {
		spin_lock_bh(&xfrm_state_lock);
		h = xfrm_spi_hash(&x->id.daddr, x->id.spi, x->id.proto, x->props.family);
		list_add(&x->byspi, xfrm_state_byspi+h);
		xfrm_state_hold(x);
		spin_unlock_bh(&xfrm_state_lock);
		wake_up(&km_waitq);
	}
}
EXPORT_SYMBOL(xfrm_alloc_spi);

int xfrm_state_walk(u8 proto, int (*func)(struct xfrm_state *, int, void*),
		    void *data)
{
	int i;
	struct xfrm_state *x;
	int count = 0;
	int err = 0;

	spin_lock_bh(&xfrm_state_lock);
	for (i = 0; i < XFRM_DST_HSIZE; i++) {
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (proto == IPSEC_PROTO_ANY || x->id.proto == proto)
				count++;
		}
	}
	if (count == 0) {
		err = -ENOENT;
		goto out;
	}

	for (i = 0; i < XFRM_DST_HSIZE; i++) {
		list_for_each_entry(x, xfrm_state_bydst+i, bydst) {
			if (proto != IPSEC_PROTO_ANY && x->id.proto != proto)
				continue;
			err = func(x, --count, data);
			if (err)
				goto out;
		}
	}
out:
	spin_unlock_bh(&xfrm_state_lock);
	return err;
}
EXPORT_SYMBOL(xfrm_state_walk);


void xfrm_replay_notify(struct xfrm_state *x, int event)
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
		    (x->replay.oseq - x->preplay.oseq < x->replay_maxdiff))
			return;

		break;

	case XFRM_REPLAY_TIMEOUT:
		if ((x->replay.seq == x->preplay.seq) &&
		    (x->replay.bitmap == x->preplay.bitmap) &&
		    (x->replay.oseq == x->preplay.oseq))
			return;

		break;
	}

	memcpy(&x->preplay, &x->replay, sizeof(struct xfrm_replay_state));
	c.event = XFRM_MSG_NEWAE;
	c.data.aevent = event;
	km_state_notify(x, &c);

	if (x->replay_maxage &&
	    !mod_timer(&x->rtimer, jiffies + x->replay_maxage))
		xfrm_state_hold(x);
}
EXPORT_SYMBOL(xfrm_replay_notify);

static void xfrm_replay_timer_handler(unsigned long data)
{
	struct xfrm_state *x = (struct xfrm_state*)data;

	spin_lock(&x->lock);

	if (xfrm_aevent_is_on() && x->km.state == XFRM_STATE_VALID)
		xfrm_replay_notify(x, XFRM_REPLAY_TIMEOUT);

	spin_unlock(&x->lock);
}

int xfrm_replay_check(struct xfrm_state *x, u32 seq)
{
	u32 diff;

	seq = ntohl(seq);

	if (unlikely(seq == 0))
		return -EINVAL;

	if (likely(seq > x->replay.seq))
		return 0;

	diff = x->replay.seq - seq;
	if (diff >= x->props.replay_window) {
		x->stats.replay_window++;
		return -EINVAL;
	}

	if (x->replay.bitmap & (1U << diff)) {
		x->stats.replay++;
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(xfrm_replay_check);

void xfrm_replay_advance(struct xfrm_state *x, u32 seq)
{
	u32 diff;

	seq = ntohl(seq);

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

	if (xfrm_aevent_is_on())
		xfrm_replay_notify(x, XFRM_REPLAY_UPDATE);
}
EXPORT_SYMBOL(xfrm_replay_advance);

static struct list_head xfrm_km_list = LIST_HEAD_INIT(xfrm_km_list);
static DEFINE_RWLOCK(xfrm_km_lock);

void km_policy_notify(struct xfrm_policy *xp, int dir, struct km_event *c)
{
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list)
		if (km->notify_policy)
			km->notify_policy(xp, dir, c);
	read_unlock(&xfrm_km_lock);
}

void km_state_notify(struct xfrm_state *x, struct km_event *c)
{
	struct xfrm_mgr *km;
	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list)
		if (km->notify)
			km->notify(x, c);
	read_unlock(&xfrm_km_lock);
}

EXPORT_SYMBOL(km_policy_notify);
EXPORT_SYMBOL(km_state_notify);

void km_state_expired(struct xfrm_state *x, int hard, u32 pid)
{
	struct km_event c;

	c.data.hard = hard;
	c.pid = pid;
	c.event = XFRM_MSG_EXPIRE;
	km_state_notify(x, &c);

	if (hard)
		wake_up(&km_waitq);
}

EXPORT_SYMBOL(km_state_expired);
/*
 * We send to all registered managers regardless of failure
 * We are happy with one success
*/
int km_query(struct xfrm_state *x, struct xfrm_tmpl *t, struct xfrm_policy *pol)
{
	int err = -EINVAL, acqret;
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		acqret = km->acquire(x, t, pol, XFRM_POLICY_OUT);
		if (!acqret)
			err = acqret;
	}
	read_unlock(&xfrm_km_lock);
	return err;
}
EXPORT_SYMBOL(km_query);

int km_new_mapping(struct xfrm_state *x, xfrm_address_t *ipaddr, u16 sport)
{
	int err = -EINVAL;
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		if (km->new_mapping)
			err = km->new_mapping(x, ipaddr, sport);
		if (!err)
			break;
	}
	read_unlock(&xfrm_km_lock);
	return err;
}
EXPORT_SYMBOL(km_new_mapping);

void km_policy_expired(struct xfrm_policy *pol, int dir, int hard, u32 pid)
{
	struct km_event c;

	c.data.hard = hard;
	c.pid = pid;
	c.event = XFRM_MSG_POLEXPIRE;
	km_policy_notify(pol, dir, &c);

	if (hard)
		wake_up(&km_waitq);
}
EXPORT_SYMBOL(km_policy_expired);

int xfrm_user_policy(struct sock *sk, int optname, u8 __user *optval, int optlen)
{
	int err;
	u8 *data;
	struct xfrm_mgr *km;
	struct xfrm_policy *pol = NULL;

	if (optlen <= 0 || optlen > PAGE_SIZE)
		return -EMSGSIZE;

	data = kmalloc(optlen, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(data, optval, optlen))
		goto out;

	err = -EINVAL;
	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		pol = km->compile_policy(sk->sk_family, optname, data,
					 optlen, &err);
		if (err >= 0)
			break;
	}
	read_unlock(&xfrm_km_lock);

	if (err >= 0) {
		xfrm_sk_policy_insert(sk, err, pol);
		xfrm_pol_put(pol);
		err = 0;
	}

out:
	kfree(data);
	return err;
}
EXPORT_SYMBOL(xfrm_user_policy);

int xfrm_register_km(struct xfrm_mgr *km)
{
	write_lock_bh(&xfrm_km_lock);
	list_add_tail(&km->list, &xfrm_km_list);
	write_unlock_bh(&xfrm_km_lock);
	return 0;
}
EXPORT_SYMBOL(xfrm_register_km);

int xfrm_unregister_km(struct xfrm_mgr *km)
{
	write_lock_bh(&xfrm_km_lock);
	list_del(&km->list);
	write_unlock_bh(&xfrm_km_lock);
	return 0;
}
EXPORT_SYMBOL(xfrm_unregister_km);

int xfrm_state_register_afinfo(struct xfrm_state_afinfo *afinfo)
{
	int err = 0;
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	if (unlikely(afinfo->family >= NPROTO))
		return -EAFNOSUPPORT;
	write_lock(&xfrm_state_afinfo_lock);
	if (unlikely(xfrm_state_afinfo[afinfo->family] != NULL))
		err = -ENOBUFS;
	else {
		afinfo->state_bydst = xfrm_state_bydst;
		afinfo->state_byspi = xfrm_state_byspi;
		xfrm_state_afinfo[afinfo->family] = afinfo;
	}
	write_unlock(&xfrm_state_afinfo_lock);
	return err;
}
EXPORT_SYMBOL(xfrm_state_register_afinfo);

int xfrm_state_unregister_afinfo(struct xfrm_state_afinfo *afinfo)
{
	int err = 0;
	if (unlikely(afinfo == NULL))
		return -EINVAL;
	if (unlikely(afinfo->family >= NPROTO))
		return -EAFNOSUPPORT;
	write_lock(&xfrm_state_afinfo_lock);
	if (likely(xfrm_state_afinfo[afinfo->family] != NULL)) {
		if (unlikely(xfrm_state_afinfo[afinfo->family] != afinfo))
			err = -EINVAL;
		else {
			xfrm_state_afinfo[afinfo->family] = NULL;
			afinfo->state_byspi = NULL;
			afinfo->state_bydst = NULL;
		}
	}
	write_unlock(&xfrm_state_afinfo_lock);
	return err;
}
EXPORT_SYMBOL(xfrm_state_unregister_afinfo);

static struct xfrm_state_afinfo *xfrm_state_get_afinfo(unsigned short family)
{
	struct xfrm_state_afinfo *afinfo;
	if (unlikely(family >= NPROTO))
		return NULL;
	read_lock(&xfrm_state_afinfo_lock);
	afinfo = xfrm_state_afinfo[family];
	if (likely(afinfo != NULL))
		read_lock(&afinfo->lock);
	read_unlock(&xfrm_state_afinfo_lock);
	return afinfo;
}

static void xfrm_state_put_afinfo(struct xfrm_state_afinfo *afinfo)
{
	if (unlikely(afinfo == NULL))
		return;
	read_unlock(&afinfo->lock);
}

/* Temporarily located here until net/xfrm/xfrm_tunnel.c is created */
void xfrm_state_delete_tunnel(struct xfrm_state *x)
{
	if (x->tunnel) {
		struct xfrm_state *t = x->tunnel;

		if (atomic_read(&t->tunnel_users) == 2)
			xfrm_state_delete(t);
		atomic_dec(&t->tunnel_users);
		xfrm_state_put(t);
		x->tunnel = NULL;
	}
}
EXPORT_SYMBOL(xfrm_state_delete_tunnel);

/*
 * This function is NOT optimal.  For example, with ESP it will give an
 * MTU that's usually two bytes short of being optimal.  However, it will
 * usually give an answer that's a multiple of 4 provided the input is
 * also a multiple of 4.
 */
int xfrm_state_mtu(struct xfrm_state *x, int mtu)
{
	int res = mtu;

	res -= x->props.header_len;

	for (;;) {
		int m = res;

		if (m < 68)
			return 68;

		spin_lock_bh(&x->lock);
		if (x->km.state == XFRM_STATE_VALID &&
		    x->type && x->type->get_max_size)
			m = x->type->get_max_size(x, m);
		else
			m += x->props.header_len;
		spin_unlock_bh(&x->lock);

		if (m <= mtu)
			break;
		res -= (m - mtu);
	}

	return res;
}

EXPORT_SYMBOL(xfrm_state_mtu);

int xfrm_init_state(struct xfrm_state *x)
{
	struct xfrm_state_afinfo *afinfo;
	int family = x->props.family;
	int err;

	err = -EAFNOSUPPORT;
	afinfo = xfrm_state_get_afinfo(family);
	if (!afinfo)
		goto error;

	err = 0;
	if (afinfo->init_flags)
		err = afinfo->init_flags(x);

	xfrm_state_put_afinfo(afinfo);

	if (err)
		goto error;

	err = -EPROTONOSUPPORT;
	x->type = xfrm_get_type(x->id.proto, family);
	if (x->type == NULL)
		goto error;

	err = x->type->init_state(x);
	if (err)
		goto error;

	x->km.state = XFRM_STATE_VALID;

error:
	return err;
}

EXPORT_SYMBOL(xfrm_init_state);
 
void __init xfrm_state_init(void)
{
	int i;

	for (i=0; i<XFRM_DST_HSIZE; i++) {
		INIT_LIST_HEAD(&xfrm_state_bydst[i]);
		INIT_LIST_HEAD(&xfrm_state_byspi[i]);
	}
	INIT_WORK(&xfrm_state_gc_work, xfrm_state_gc_task, NULL);
}

