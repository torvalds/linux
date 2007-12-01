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
#include <linux/cache.h>
#include <asm/uaccess.h>

#include "xfrm_hash.h"

struct sock *xfrm_nl;
EXPORT_SYMBOL(xfrm_nl);

u32 sysctl_xfrm_aevent_etime __read_mostly = XFRM_AE_ETIME;
EXPORT_SYMBOL(sysctl_xfrm_aevent_etime);

u32 sysctl_xfrm_aevent_rseqth __read_mostly = XFRM_AE_SEQT_SIZE;
EXPORT_SYMBOL(sysctl_xfrm_aevent_rseqth);

u32 sysctl_xfrm_acq_expires __read_mostly = 30;

/* Each xfrm_state may be linked to two tables:

   1. Hash table by (spi,daddr,ah/esp) to find SA by SPI. (input,ctl)
   2. Hash table by (daddr,family,reqid) to find what SAs exist for given
      destination/tunnel endpoint. (output)
 */

static DEFINE_SPINLOCK(xfrm_state_lock);

/* Hash table to find appropriate SA towards given target (endpoint
 * of tunnel or destination of transport mode) allowed by selector.
 *
 * Main use is finding SA after policy selected tunnel or transport mode.
 * Also, it can be used by ah/esp icmp error handler to find offending SA.
 */
static struct hlist_head *xfrm_state_bydst __read_mostly;
static struct hlist_head *xfrm_state_bysrc __read_mostly;
static struct hlist_head *xfrm_state_byspi __read_mostly;
static unsigned int xfrm_state_hmask __read_mostly;
static unsigned int xfrm_state_hashmax __read_mostly = 1 * 1024 * 1024;
static unsigned int xfrm_state_num;
static unsigned int xfrm_state_genid;

static struct xfrm_state_afinfo *xfrm_state_get_afinfo(unsigned int family);
static void xfrm_state_put_afinfo(struct xfrm_state_afinfo *afinfo);

static inline unsigned int xfrm_dst_hash(xfrm_address_t *daddr,
					 xfrm_address_t *saddr,
					 u32 reqid,
					 unsigned short family)
{
	return __xfrm_dst_hash(daddr, saddr, reqid, family, xfrm_state_hmask);
}

static inline unsigned int xfrm_src_hash(xfrm_address_t *daddr,
					 xfrm_address_t *saddr,
					 unsigned short family)
{
	return __xfrm_src_hash(daddr, saddr, family, xfrm_state_hmask);
}

static inline unsigned int
xfrm_spi_hash(xfrm_address_t *daddr, __be32 spi, u8 proto, unsigned short family)
{
	return __xfrm_spi_hash(daddr, spi, proto, family, xfrm_state_hmask);
}

static void xfrm_hash_transfer(struct hlist_head *list,
			       struct hlist_head *ndsttable,
			       struct hlist_head *nsrctable,
			       struct hlist_head *nspitable,
			       unsigned int nhashmask)
{
	struct hlist_node *entry, *tmp;
	struct xfrm_state *x;

	hlist_for_each_entry_safe(x, entry, tmp, list, bydst) {
		unsigned int h;

		h = __xfrm_dst_hash(&x->id.daddr, &x->props.saddr,
				    x->props.reqid, x->props.family,
				    nhashmask);
		hlist_add_head(&x->bydst, ndsttable+h);

		h = __xfrm_src_hash(&x->id.daddr, &x->props.saddr,
				    x->props.family,
				    nhashmask);
		hlist_add_head(&x->bysrc, nsrctable+h);

		if (x->id.spi) {
			h = __xfrm_spi_hash(&x->id.daddr, x->id.spi,
					    x->id.proto, x->props.family,
					    nhashmask);
			hlist_add_head(&x->byspi, nspitable+h);
		}
	}
}

static unsigned long xfrm_hash_new_size(void)
{
	return ((xfrm_state_hmask + 1) << 1) *
		sizeof(struct hlist_head);
}

static DEFINE_MUTEX(hash_resize_mutex);

static void xfrm_hash_resize(struct work_struct *__unused)
{
	struct hlist_head *ndst, *nsrc, *nspi, *odst, *osrc, *ospi;
	unsigned long nsize, osize;
	unsigned int nhashmask, ohashmask;
	int i;

	mutex_lock(&hash_resize_mutex);

	nsize = xfrm_hash_new_size();
	ndst = xfrm_hash_alloc(nsize);
	if (!ndst)
		goto out_unlock;
	nsrc = xfrm_hash_alloc(nsize);
	if (!nsrc) {
		xfrm_hash_free(ndst, nsize);
		goto out_unlock;
	}
	nspi = xfrm_hash_alloc(nsize);
	if (!nspi) {
		xfrm_hash_free(ndst, nsize);
		xfrm_hash_free(nsrc, nsize);
		goto out_unlock;
	}

	spin_lock_bh(&xfrm_state_lock);

	nhashmask = (nsize / sizeof(struct hlist_head)) - 1U;
	for (i = xfrm_state_hmask; i >= 0; i--)
		xfrm_hash_transfer(xfrm_state_bydst+i, ndst, nsrc, nspi,
				   nhashmask);

	odst = xfrm_state_bydst;
	osrc = xfrm_state_bysrc;
	ospi = xfrm_state_byspi;
	ohashmask = xfrm_state_hmask;

	xfrm_state_bydst = ndst;
	xfrm_state_bysrc = nsrc;
	xfrm_state_byspi = nspi;
	xfrm_state_hmask = nhashmask;

	spin_unlock_bh(&xfrm_state_lock);

	osize = (ohashmask + 1) * sizeof(struct hlist_head);
	xfrm_hash_free(odst, osize);
	xfrm_hash_free(osrc, osize);
	xfrm_hash_free(ospi, osize);

out_unlock:
	mutex_unlock(&hash_resize_mutex);
}

static DECLARE_WORK(xfrm_hash_work, xfrm_hash_resize);

DECLARE_WAIT_QUEUE_HEAD(km_waitq);
EXPORT_SYMBOL(km_waitq);

static DEFINE_RWLOCK(xfrm_state_afinfo_lock);
static struct xfrm_state_afinfo *xfrm_state_afinfo[NPROTO];

static struct work_struct xfrm_state_gc_work;
static HLIST_HEAD(xfrm_state_gc_list);
static DEFINE_SPINLOCK(xfrm_state_gc_lock);

int __xfrm_state_delete(struct xfrm_state *x);

int km_query(struct xfrm_state *x, struct xfrm_tmpl *t, struct xfrm_policy *pol);
void km_state_expired(struct xfrm_state *x, int hard, u32 pid);

static struct xfrm_state_afinfo *xfrm_state_lock_afinfo(unsigned int family)
{
	struct xfrm_state_afinfo *afinfo;
	if (unlikely(family >= NPROTO))
		return NULL;
	write_lock_bh(&xfrm_state_afinfo_lock);
	afinfo = xfrm_state_afinfo[family];
	if (unlikely(!afinfo))
		write_unlock_bh(&xfrm_state_afinfo_lock);
	return afinfo;
}

static void xfrm_state_unlock_afinfo(struct xfrm_state_afinfo *afinfo)
{
	write_unlock_bh(&xfrm_state_afinfo_lock);
}

int xfrm_register_type(struct xfrm_type *type, unsigned short family)
{
	struct xfrm_state_afinfo *afinfo = xfrm_state_lock_afinfo(family);
	struct xfrm_type **typemap;
	int err = 0;

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;
	typemap = afinfo->type_map;

	if (likely(typemap[type->proto] == NULL))
		typemap[type->proto] = type;
	else
		err = -EEXIST;
	xfrm_state_unlock_afinfo(afinfo);
	return err;
}
EXPORT_SYMBOL(xfrm_register_type);

int xfrm_unregister_type(struct xfrm_type *type, unsigned short family)
{
	struct xfrm_state_afinfo *afinfo = xfrm_state_lock_afinfo(family);
	struct xfrm_type **typemap;
	int err = 0;

	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;
	typemap = afinfo->type_map;

	if (unlikely(typemap[type->proto] != type))
		err = -ENOENT;
	else
		typemap[type->proto] = NULL;
	xfrm_state_unlock_afinfo(afinfo);
	return err;
}
EXPORT_SYMBOL(xfrm_unregister_type);

static struct xfrm_type *xfrm_get_type(u8 proto, unsigned short family)
{
	struct xfrm_state_afinfo *afinfo;
	struct xfrm_type **typemap;
	struct xfrm_type *type;
	int modload_attempted = 0;

retry:
	afinfo = xfrm_state_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return NULL;
	typemap = afinfo->type_map;

	type = typemap[proto];
	if (unlikely(type && !try_module_get(type->owner)))
		type = NULL;
	if (!type && !modload_attempted) {
		xfrm_state_put_afinfo(afinfo);
		request_module("xfrm-type-%d-%d", family, proto);
		modload_attempted = 1;
		goto retry;
	}

	xfrm_state_put_afinfo(afinfo);
	return type;
}

static void xfrm_put_type(struct xfrm_type *type)
{
	module_put(type->owner);
}

int xfrm_register_mode(struct xfrm_mode *mode, int family)
{
	struct xfrm_state_afinfo *afinfo;
	struct xfrm_mode **modemap;
	int err;

	if (unlikely(mode->encap >= XFRM_MODE_MAX))
		return -EINVAL;

	afinfo = xfrm_state_lock_afinfo(family);
	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	err = -EEXIST;
	modemap = afinfo->mode_map;
	if (modemap[mode->encap])
		goto out;

	err = -ENOENT;
	if (!try_module_get(afinfo->owner))
		goto out;

	mode->afinfo = afinfo;
	modemap[mode->encap] = mode;
	err = 0;

out:
	xfrm_state_unlock_afinfo(afinfo);
	return err;
}
EXPORT_SYMBOL(xfrm_register_mode);

int xfrm_unregister_mode(struct xfrm_mode *mode, int family)
{
	struct xfrm_state_afinfo *afinfo;
	struct xfrm_mode **modemap;
	int err;

	if (unlikely(mode->encap >= XFRM_MODE_MAX))
		return -EINVAL;

	afinfo = xfrm_state_lock_afinfo(family);
	if (unlikely(afinfo == NULL))
		return -EAFNOSUPPORT;

	err = -ENOENT;
	modemap = afinfo->mode_map;
	if (likely(modemap[mode->encap] == mode)) {
		modemap[mode->encap] = NULL;
		module_put(mode->afinfo->owner);
		err = 0;
	}

	xfrm_state_unlock_afinfo(afinfo);
	return err;
}
EXPORT_SYMBOL(xfrm_unregister_mode);

static struct xfrm_mode *xfrm_get_mode(unsigned int encap, int family)
{
	struct xfrm_state_afinfo *afinfo;
	struct xfrm_mode *mode;
	int modload_attempted = 0;

	if (unlikely(encap >= XFRM_MODE_MAX))
		return NULL;

retry:
	afinfo = xfrm_state_get_afinfo(family);
	if (unlikely(afinfo == NULL))
		return NULL;

	mode = afinfo->mode_map[encap];
	if (unlikely(mode && !try_module_get(mode->owner)))
		mode = NULL;
	if (!mode && !modload_attempted) {
		xfrm_state_put_afinfo(afinfo);
		request_module("xfrm-mode-%d-%d", family, encap);
		modload_attempted = 1;
		goto retry;
	}

	xfrm_state_put_afinfo(afinfo);
	return mode;
}

static void xfrm_put_mode(struct xfrm_mode *mode)
{
	module_put(mode->owner);
}

static void xfrm_state_gc_destroy(struct xfrm_state *x)
{
	del_timer_sync(&x->timer);
	del_timer_sync(&x->rtimer);
	kfree(x->aalg);
	kfree(x->ealg);
	kfree(x->calg);
	kfree(x->encap);
	kfree(x->coaddr);
	if (x->inner_mode)
		xfrm_put_mode(x->inner_mode);
	if (x->outer_mode)
		xfrm_put_mode(x->outer_mode);
	if (x->type) {
		x->type->destructor(x);
		xfrm_put_type(x->type);
	}
	security_xfrm_state_free(x);
	kfree(x);
}

static void xfrm_state_gc_task(struct work_struct *data)
{
	struct xfrm_state *x;
	struct hlist_node *entry, *tmp;
	struct hlist_head gc_list;

	spin_lock_bh(&xfrm_state_gc_lock);
	gc_list.first = xfrm_state_gc_list.first;
	INIT_HLIST_HEAD(&xfrm_state_gc_list);
	spin_unlock_bh(&xfrm_state_gc_lock);

	hlist_for_each_entry_safe(x, entry, tmp, &gc_list, bydst)
		xfrm_state_gc_destroy(x);

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
	unsigned long now = get_seconds();
	long next = LONG_MAX;
	int warn = 0;
	int err = 0;

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
	if (next != LONG_MAX)
		mod_timer(&x->timer, jiffies + make_jiffies(next));

	goto out;

expired:
	if (x->km.state == XFRM_STATE_ACQ && x->id.spi == 0) {
		x->km.state = XFRM_STATE_EXPIRED;
		wake_up(&km_waitq);
		next = 2;
		goto resched;
	}

	err = __xfrm_state_delete(x);
	if (!err && x->id.spi)
		km_state_expired(x, 1, 0);

	xfrm_audit_state_delete(x, err ? 0 : 1,
				audit_get_loginuid(current->audit_context), 0);

out:
	spin_unlock(&x->lock);
}

static void xfrm_replay_timer_handler(unsigned long data);

struct xfrm_state *xfrm_state_alloc(void)
{
	struct xfrm_state *x;

	x = kzalloc(sizeof(struct xfrm_state), GFP_ATOMIC);

	if (x) {
		atomic_set(&x->refcnt, 1);
		atomic_set(&x->tunnel_users, 0);
		INIT_HLIST_NODE(&x->bydst);
		INIT_HLIST_NODE(&x->bysrc);
		INIT_HLIST_NODE(&x->byspi);
		init_timer(&x->timer);
		x->timer.function = xfrm_timer_handler;
		x->timer.data	  = (unsigned long)x;
		init_timer(&x->rtimer);
		x->rtimer.function = xfrm_replay_timer_handler;
		x->rtimer.data     = (unsigned long)x;
		x->curlft.add_time = get_seconds();
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
	hlist_add_head(&x->bydst, &xfrm_state_gc_list);
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
		hlist_del(&x->bydst);
		hlist_del(&x->bysrc);
		if (x->id.spi)
			hlist_del(&x->byspi);
		xfrm_state_num--;
		spin_unlock(&xfrm_state_lock);

		/* All xfrm_state objects are created by xfrm_state_alloc.
		 * The xfrm_state_alloc call gives a reference, and that
		 * is what we are dropping here.
		 */
		xfrm_state_put(x);
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

#ifdef CONFIG_SECURITY_NETWORK_XFRM
static inline int
xfrm_state_flush_secctx_check(u8 proto, struct xfrm_audit *audit_info)
{
	int i, err = 0;

	for (i = 0; i <= xfrm_state_hmask; i++) {
		struct hlist_node *entry;
		struct xfrm_state *x;

		hlist_for_each_entry(x, entry, xfrm_state_bydst+i, bydst) {
			if (xfrm_id_proto_match(x->id.proto, proto) &&
			   (err = security_xfrm_state_delete(x)) != 0) {
				xfrm_audit_state_delete(x, 0,
							audit_info->loginuid,
							audit_info->secid);
				return err;
			}
		}
	}

	return err;
}
#else
static inline int
xfrm_state_flush_secctx_check(u8 proto, struct xfrm_audit *audit_info)
{
	return 0;
}
#endif

int xfrm_state_flush(u8 proto, struct xfrm_audit *audit_info)
{
	int i, err = 0;

	spin_lock_bh(&xfrm_state_lock);
	err = xfrm_state_flush_secctx_check(proto, audit_info);
	if (err)
		goto out;

	for (i = 0; i <= xfrm_state_hmask; i++) {
		struct hlist_node *entry;
		struct xfrm_state *x;
restart:
		hlist_for_each_entry(x, entry, xfrm_state_bydst+i, bydst) {
			if (!xfrm_state_kern(x) &&
			    xfrm_id_proto_match(x->id.proto, proto)) {
				xfrm_state_hold(x);
				spin_unlock_bh(&xfrm_state_lock);

				err = xfrm_state_delete(x);
				xfrm_audit_state_delete(x, err ? 0 : 1,
							audit_info->loginuid,
							audit_info->secid);
				xfrm_state_put(x);

				spin_lock_bh(&xfrm_state_lock);
				goto restart;
			}
		}
	}
	err = 0;

out:
	spin_unlock_bh(&xfrm_state_lock);
	wake_up(&km_waitq);
	return err;
}
EXPORT_SYMBOL(xfrm_state_flush);

void xfrm_sad_getinfo(struct xfrmk_sadinfo *si)
{
	spin_lock_bh(&xfrm_state_lock);
	si->sadcnt = xfrm_state_num;
	si->sadhcnt = xfrm_state_hmask;
	si->sadhmcnt = xfrm_state_hashmax;
	spin_unlock_bh(&xfrm_state_lock);
}
EXPORT_SYMBOL(xfrm_sad_getinfo);

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

static struct xfrm_state *__xfrm_state_lookup(xfrm_address_t *daddr, __be32 spi, u8 proto, unsigned short family)
{
	unsigned int h = xfrm_spi_hash(daddr, spi, proto, family);
	struct xfrm_state *x;
	struct hlist_node *entry;

	hlist_for_each_entry(x, entry, xfrm_state_byspi+h, byspi) {
		if (x->props.family != family ||
		    x->id.spi       != spi ||
		    x->id.proto     != proto)
			continue;

		switch (family) {
		case AF_INET:
			if (x->id.daddr.a4 != daddr->a4)
				continue;
			break;
		case AF_INET6:
			if (!ipv6_addr_equal((struct in6_addr *)daddr,
					     (struct in6_addr *)
					     x->id.daddr.a6))
				continue;
			break;
		}

		xfrm_state_hold(x);
		return x;
	}

	return NULL;
}

static struct xfrm_state *__xfrm_state_lookup_byaddr(xfrm_address_t *daddr, xfrm_address_t *saddr, u8 proto, unsigned short family)
{
	unsigned int h = xfrm_src_hash(daddr, saddr, family);
	struct xfrm_state *x;
	struct hlist_node *entry;

	hlist_for_each_entry(x, entry, xfrm_state_bysrc+h, bysrc) {
		if (x->props.family != family ||
		    x->id.proto     != proto)
			continue;

		switch (family) {
		case AF_INET:
			if (x->id.daddr.a4 != daddr->a4 ||
			    x->props.saddr.a4 != saddr->a4)
				continue;
			break;
		case AF_INET6:
			if (!ipv6_addr_equal((struct in6_addr *)daddr,
					     (struct in6_addr *)
					     x->id.daddr.a6) ||
			    !ipv6_addr_equal((struct in6_addr *)saddr,
					     (struct in6_addr *)
					     x->props.saddr.a6))
				continue;
			break;
		}

		xfrm_state_hold(x);
		return x;
	}

	return NULL;
}

static inline struct xfrm_state *
__xfrm_state_locate(struct xfrm_state *x, int use_spi, int family)
{
	if (use_spi)
		return __xfrm_state_lookup(&x->id.daddr, x->id.spi,
					   x->id.proto, family);
	else
		return __xfrm_state_lookup_byaddr(&x->id.daddr,
						  &x->props.saddr,
						  x->id.proto, family);
}

static void xfrm_hash_grow_check(int have_hash_collision)
{
	if (have_hash_collision &&
	    (xfrm_state_hmask + 1) < xfrm_state_hashmax &&
	    xfrm_state_num > xfrm_state_hmask)
		schedule_work(&xfrm_hash_work);
}

struct xfrm_state *
xfrm_state_find(xfrm_address_t *daddr, xfrm_address_t *saddr,
		struct flowi *fl, struct xfrm_tmpl *tmpl,
		struct xfrm_policy *pol, int *err,
		unsigned short family)
{
	unsigned int h = xfrm_dst_hash(daddr, saddr, tmpl->reqid, family);
	struct hlist_node *entry;
	struct xfrm_state *x, *x0;
	int acquire_in_progress = 0;
	int error = 0;
	struct xfrm_state *best = NULL;

	spin_lock_bh(&xfrm_state_lock);
	hlist_for_each_entry(x, entry, xfrm_state_bydst+h, bydst) {
		if (x->props.family == family &&
		    x->props.reqid == tmpl->reqid &&
		    !(x->props.flags & XFRM_STATE_WILDRECV) &&
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
				if (!xfrm_selector_match(&x->sel, fl, x->sel.family) ||
				    !security_xfrm_state_pol_flow_match(x, pol, fl))
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
				if (xfrm_selector_match(&x->sel, fl, x->sel.family) &&
				    security_xfrm_state_pol_flow_match(x, pol, fl))
					error = -ESRCH;
			}
		}
	}

	x = best;
	if (!x && !error && !acquire_in_progress) {
		if (tmpl->id.spi &&
		    (x0 = __xfrm_state_lookup(daddr, tmpl->id.spi,
					      tmpl->id.proto, family)) != NULL) {
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

		error = security_xfrm_state_alloc_acquire(x, pol->security, fl->secid);
		if (error) {
			x->km.state = XFRM_STATE_DEAD;
			xfrm_state_put(x);
			x = NULL;
			goto out;
		}

		if (km_query(x, tmpl, pol) == 0) {
			x->km.state = XFRM_STATE_ACQ;
			hlist_add_head(&x->bydst, xfrm_state_bydst+h);
			h = xfrm_src_hash(daddr, saddr, family);
			hlist_add_head(&x->bysrc, xfrm_state_bysrc+h);
			if (x->id.spi) {
				h = xfrm_spi_hash(&x->id.daddr, x->id.spi, x->id.proto, family);
				hlist_add_head(&x->byspi, xfrm_state_byspi+h);
			}
			x->lft.hard_add_expires_seconds = sysctl_xfrm_acq_expires;
			x->timer.expires = jiffies + sysctl_xfrm_acq_expires*HZ;
			add_timer(&x->timer);
			xfrm_state_num++;
			xfrm_hash_grow_check(x->bydst.next != NULL);
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
	return x;
}

struct xfrm_state *
xfrm_stateonly_find(xfrm_address_t *daddr, xfrm_address_t *saddr,
		    unsigned short family, u8 mode, u8 proto, u32 reqid)
{
	unsigned int h = xfrm_dst_hash(daddr, saddr, reqid, family);
	struct xfrm_state *rx = NULL, *x = NULL;
	struct hlist_node *entry;

	spin_lock(&xfrm_state_lock);
	hlist_for_each_entry(x, entry, xfrm_state_bydst+h, bydst) {
		if (x->props.family == family &&
		    x->props.reqid == reqid &&
		    !(x->props.flags & XFRM_STATE_WILDRECV) &&
		    xfrm_state_addr_check(x, daddr, saddr, family) &&
		    mode == x->props.mode &&
		    proto == x->id.proto &&
		    x->km.state == XFRM_STATE_VALID) {
			rx = x;
			break;
		}
	}

	if (rx)
		xfrm_state_hold(rx);
	spin_unlock(&xfrm_state_lock);


	return rx;
}
EXPORT_SYMBOL(xfrm_stateonly_find);

static void __xfrm_state_insert(struct xfrm_state *x)
{
	unsigned int h;

	x->genid = ++xfrm_state_genid;

	h = xfrm_dst_hash(&x->id.daddr, &x->props.saddr,
			  x->props.reqid, x->props.family);
	hlist_add_head(&x->bydst, xfrm_state_bydst+h);

	h = xfrm_src_hash(&x->id.daddr, &x->props.saddr, x->props.family);
	hlist_add_head(&x->bysrc, xfrm_state_bysrc+h);

	if (x->id.spi) {
		h = xfrm_spi_hash(&x->id.daddr, x->id.spi, x->id.proto,
				  x->props.family);

		hlist_add_head(&x->byspi, xfrm_state_byspi+h);
	}

	mod_timer(&x->timer, jiffies + HZ);
	if (x->replay_maxage)
		mod_timer(&x->rtimer, jiffies + x->replay_maxage);

	wake_up(&km_waitq);

	xfrm_state_num++;

	xfrm_hash_grow_check(x->bydst.next != NULL);
}

/* xfrm_state_lock is held */
static void __xfrm_state_bump_genids(struct xfrm_state *xnew)
{
	unsigned short family = xnew->props.family;
	u32 reqid = xnew->props.reqid;
	struct xfrm_state *x;
	struct hlist_node *entry;
	unsigned int h;

	h = xfrm_dst_hash(&xnew->id.daddr, &xnew->props.saddr, reqid, family);
	hlist_for_each_entry(x, entry, xfrm_state_bydst+h, bydst) {
		if (x->props.family	== family &&
		    x->props.reqid	== reqid &&
		    !xfrm_addr_cmp(&x->id.daddr, &xnew->id.daddr, family) &&
		    !xfrm_addr_cmp(&x->props.saddr, &xnew->props.saddr, family))
			x->genid = xfrm_state_genid;
	}
}

void xfrm_state_insert(struct xfrm_state *x)
{
	spin_lock_bh(&xfrm_state_lock);
	__xfrm_state_bump_genids(x);
	__xfrm_state_insert(x);
	spin_unlock_bh(&xfrm_state_lock);
}
EXPORT_SYMBOL(xfrm_state_insert);

/* xfrm_state_lock is held */
static struct xfrm_state *__find_acq_core(unsigned short family, u8 mode, u32 reqid, u8 proto, xfrm_address_t *daddr, xfrm_address_t *saddr, int create)
{
	unsigned int h = xfrm_dst_hash(daddr, saddr, reqid, family);
	struct hlist_node *entry;
	struct xfrm_state *x;

	hlist_for_each_entry(x, entry, xfrm_state_bydst+h, bydst) {
		if (x->props.reqid  != reqid ||
		    x->props.mode   != mode ||
		    x->props.family != family ||
		    x->km.state     != XFRM_STATE_ACQ ||
		    x->id.spi       != 0 ||
		    x->id.proto	    != proto)
			continue;

		switch (family) {
		case AF_INET:
			if (x->id.daddr.a4    != daddr->a4 ||
			    x->props.saddr.a4 != saddr->a4)
				continue;
			break;
		case AF_INET6:
			if (!ipv6_addr_equal((struct in6_addr *)x->id.daddr.a6,
					     (struct in6_addr *)daddr) ||
			    !ipv6_addr_equal((struct in6_addr *)
					     x->props.saddr.a6,
					     (struct in6_addr *)saddr))
				continue;
			break;
		}

		xfrm_state_hold(x);
		return x;
	}

	if (!create)
		return NULL;

	x = xfrm_state_alloc();
	if (likely(x)) {
		switch (family) {
		case AF_INET:
			x->sel.daddr.a4 = daddr->a4;
			x->sel.saddr.a4 = saddr->a4;
			x->sel.prefixlen_d = 32;
			x->sel.prefixlen_s = 32;
			x->props.saddr.a4 = saddr->a4;
			x->id.daddr.a4 = daddr->a4;
			break;

		case AF_INET6:
			ipv6_addr_copy((struct in6_addr *)x->sel.daddr.a6,
				       (struct in6_addr *)daddr);
			ipv6_addr_copy((struct in6_addr *)x->sel.saddr.a6,
				       (struct in6_addr *)saddr);
			x->sel.prefixlen_d = 128;
			x->sel.prefixlen_s = 128;
			ipv6_addr_copy((struct in6_addr *)x->props.saddr.a6,
				       (struct in6_addr *)saddr);
			ipv6_addr_copy((struct in6_addr *)x->id.daddr.a6,
				       (struct in6_addr *)daddr);
			break;
		}

		x->km.state = XFRM_STATE_ACQ;
		x->id.proto = proto;
		x->props.family = family;
		x->props.mode = mode;
		x->props.reqid = reqid;
		x->lft.hard_add_expires_seconds = sysctl_xfrm_acq_expires;
		xfrm_state_hold(x);
		x->timer.expires = jiffies + sysctl_xfrm_acq_expires*HZ;
		add_timer(&x->timer);
		hlist_add_head(&x->bydst, xfrm_state_bydst+h);
		h = xfrm_src_hash(daddr, saddr, family);
		hlist_add_head(&x->bysrc, xfrm_state_bysrc+h);

		xfrm_state_num++;

		xfrm_hash_grow_check(x->bydst.next != NULL);
	}

	return x;
}

static struct xfrm_state *__xfrm_find_acq_byseq(u32 seq);

int xfrm_state_add(struct xfrm_state *x)
{
	struct xfrm_state *x1;
	int family;
	int err;
	int use_spi = xfrm_id_proto_match(x->id.proto, IPSEC_PROTO_ANY);

	family = x->props.family;

	spin_lock_bh(&xfrm_state_lock);

	x1 = __xfrm_state_locate(x, use_spi, family);
	if (x1) {
		xfrm_state_put(x1);
		x1 = NULL;
		err = -EEXIST;
		goto out;
	}

	if (use_spi && x->km.seq) {
		x1 = __xfrm_find_acq_byseq(x->km.seq);
		if (x1 && ((x1->id.proto != x->id.proto) ||
		    xfrm_addr_cmp(&x1->id.daddr, &x->id.daddr, family))) {
			xfrm_state_put(x1);
			x1 = NULL;
		}
	}

	if (use_spi && !x1)
		x1 = __find_acq_core(family, x->props.mode, x->props.reqid,
				     x->id.proto,
				     &x->id.daddr, &x->props.saddr, 0);

	__xfrm_state_bump_genids(x);
	__xfrm_state_insert(x);
	err = 0;

out:
	spin_unlock_bh(&xfrm_state_lock);

	if (x1) {
		xfrm_state_delete(x1);
		xfrm_state_put(x1);
	}

	return err;
}
EXPORT_SYMBOL(xfrm_state_add);

#ifdef CONFIG_XFRM_MIGRATE
struct xfrm_state *xfrm_state_clone(struct xfrm_state *orig, int *errp)
{
	int err = -ENOMEM;
	struct xfrm_state *x = xfrm_state_alloc();
	if (!x)
		goto error;

	memcpy(&x->id, &orig->id, sizeof(x->id));
	memcpy(&x->sel, &orig->sel, sizeof(x->sel));
	memcpy(&x->lft, &orig->lft, sizeof(x->lft));
	x->props.mode = orig->props.mode;
	x->props.replay_window = orig->props.replay_window;
	x->props.reqid = orig->props.reqid;
	x->props.family = orig->props.family;
	x->props.saddr = orig->props.saddr;

	if (orig->aalg) {
		x->aalg = xfrm_algo_clone(orig->aalg);
		if (!x->aalg)
			goto error;
	}
	x->props.aalgo = orig->props.aalgo;

	if (orig->ealg) {
		x->ealg = xfrm_algo_clone(orig->ealg);
		if (!x->ealg)
			goto error;
	}
	x->props.ealgo = orig->props.ealgo;

	if (orig->calg) {
		x->calg = xfrm_algo_clone(orig->calg);
		if (!x->calg)
			goto error;
	}
	x->props.calgo = orig->props.calgo;

	if (orig->encap) {
		x->encap = kmemdup(orig->encap, sizeof(*x->encap), GFP_KERNEL);
		if (!x->encap)
			goto error;
	}

	if (orig->coaddr) {
		x->coaddr = kmemdup(orig->coaddr, sizeof(*x->coaddr),
				    GFP_KERNEL);
		if (!x->coaddr)
			goto error;
	}

	err = xfrm_init_state(x);
	if (err)
		goto error;

	x->props.flags = orig->props.flags;

	x->curlft.add_time = orig->curlft.add_time;
	x->km.state = orig->km.state;
	x->km.seq = orig->km.seq;

	return x;

 error:
	if (errp)
		*errp = err;
	if (x) {
		kfree(x->aalg);
		kfree(x->ealg);
		kfree(x->calg);
		kfree(x->encap);
		kfree(x->coaddr);
	}
	kfree(x);
	return NULL;
}
EXPORT_SYMBOL(xfrm_state_clone);

/* xfrm_state_lock is held */
struct xfrm_state * xfrm_migrate_state_find(struct xfrm_migrate *m)
{
	unsigned int h;
	struct xfrm_state *x;
	struct hlist_node *entry;

	if (m->reqid) {
		h = xfrm_dst_hash(&m->old_daddr, &m->old_saddr,
				  m->reqid, m->old_family);
		hlist_for_each_entry(x, entry, xfrm_state_bydst+h, bydst) {
			if (x->props.mode != m->mode ||
			    x->id.proto != m->proto)
				continue;
			if (m->reqid && x->props.reqid != m->reqid)
				continue;
			if (xfrm_addr_cmp(&x->id.daddr, &m->old_daddr,
					  m->old_family) ||
			    xfrm_addr_cmp(&x->props.saddr, &m->old_saddr,
					  m->old_family))
				continue;
			xfrm_state_hold(x);
			return x;
		}
	} else {
		h = xfrm_src_hash(&m->old_daddr, &m->old_saddr,
				  m->old_family);
		hlist_for_each_entry(x, entry, xfrm_state_bysrc+h, bysrc) {
			if (x->props.mode != m->mode ||
			    x->id.proto != m->proto)
				continue;
			if (xfrm_addr_cmp(&x->id.daddr, &m->old_daddr,
					  m->old_family) ||
			    xfrm_addr_cmp(&x->props.saddr, &m->old_saddr,
					  m->old_family))
				continue;
			xfrm_state_hold(x);
			return x;
		}
	}

	return NULL;
}
EXPORT_SYMBOL(xfrm_migrate_state_find);

struct xfrm_state * xfrm_state_migrate(struct xfrm_state *x,
				       struct xfrm_migrate *m)
{
	struct xfrm_state *xc;
	int err;

	xc = xfrm_state_clone(x, &err);
	if (!xc)
		return NULL;

	memcpy(&xc->id.daddr, &m->new_daddr, sizeof(xc->id.daddr));
	memcpy(&xc->props.saddr, &m->new_saddr, sizeof(xc->props.saddr));

	/* add state */
	if (!xfrm_addr_cmp(&x->id.daddr, &m->new_daddr, m->new_family)) {
		/* a care is needed when the destination address of the
		   state is to be updated as it is a part of triplet */
		xfrm_state_insert(xc);
	} else {
		if ((err = xfrm_state_add(xc)) < 0)
			goto error;
	}

	return xc;
error:
	kfree(xc);
	return NULL;
}
EXPORT_SYMBOL(xfrm_state_migrate);
#endif

int xfrm_state_update(struct xfrm_state *x)
{
	struct xfrm_state *x1;
	int err;
	int use_spi = xfrm_id_proto_match(x->id.proto, IPSEC_PROTO_ANY);

	spin_lock_bh(&xfrm_state_lock);
	x1 = __xfrm_state_locate(x, use_spi, x->props.family);

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
		if (x->coaddr && x1->coaddr) {
			memcpy(x1->coaddr, x->coaddr, sizeof(*x1->coaddr));
		}
		if (!use_spi && memcmp(&x1->sel, &x->sel, sizeof(x1->sel)))
			memcpy(&x1->sel, &x->sel, sizeof(x1->sel));
		memcpy(&x1->lft, &x->lft, sizeof(x1->lft));
		x1->km.dying = 0;

		mod_timer(&x1->timer, jiffies + HZ);
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
		x->curlft.use_time = get_seconds();

	if (x->km.state != XFRM_STATE_VALID)
		return -EINVAL;

	if (x->curlft.bytes >= x->lft.hard_byte_limit ||
	    x->curlft.packets >= x->lft.hard_packet_limit) {
		x->km.state = XFRM_STATE_EXPIRED;
		mod_timer(&x->timer, jiffies);
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

struct xfrm_state *
xfrm_state_lookup(xfrm_address_t *daddr, __be32 spi, u8 proto,
		  unsigned short family)
{
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	x = __xfrm_state_lookup(daddr, spi, proto, family);
	spin_unlock_bh(&xfrm_state_lock);
	return x;
}
EXPORT_SYMBOL(xfrm_state_lookup);

struct xfrm_state *
xfrm_state_lookup_byaddr(xfrm_address_t *daddr, xfrm_address_t *saddr,
			 u8 proto, unsigned short family)
{
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	x = __xfrm_state_lookup_byaddr(daddr, saddr, proto, family);
	spin_unlock_bh(&xfrm_state_lock);
	return x;
}
EXPORT_SYMBOL(xfrm_state_lookup_byaddr);

struct xfrm_state *
xfrm_find_acq(u8 mode, u32 reqid, u8 proto,
	      xfrm_address_t *daddr, xfrm_address_t *saddr,
	      int create, unsigned short family)
{
	struct xfrm_state *x;

	spin_lock_bh(&xfrm_state_lock);
	x = __find_acq_core(family, mode, reqid, proto, daddr, saddr, create);
	spin_unlock_bh(&xfrm_state_lock);

	return x;
}
EXPORT_SYMBOL(xfrm_find_acq);

#ifdef CONFIG_XFRM_SUB_POLICY
int
xfrm_tmpl_sort(struct xfrm_tmpl **dst, struct xfrm_tmpl **src, int n,
	       unsigned short family)
{
	int err = 0;
	struct xfrm_state_afinfo *afinfo = xfrm_state_get_afinfo(family);
	if (!afinfo)
		return -EAFNOSUPPORT;

	spin_lock_bh(&xfrm_state_lock);
	if (afinfo->tmpl_sort)
		err = afinfo->tmpl_sort(dst, src, n);
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);
	return err;
}
EXPORT_SYMBOL(xfrm_tmpl_sort);

int
xfrm_state_sort(struct xfrm_state **dst, struct xfrm_state **src, int n,
		unsigned short family)
{
	int err = 0;
	struct xfrm_state_afinfo *afinfo = xfrm_state_get_afinfo(family);
	if (!afinfo)
		return -EAFNOSUPPORT;

	spin_lock_bh(&xfrm_state_lock);
	if (afinfo->state_sort)
		err = afinfo->state_sort(dst, src, n);
	spin_unlock_bh(&xfrm_state_lock);
	xfrm_state_put_afinfo(afinfo);
	return err;
}
EXPORT_SYMBOL(xfrm_state_sort);
#endif

/* Silly enough, but I'm lazy to build resolution list */

static struct xfrm_state *__xfrm_find_acq_byseq(u32 seq)
{
	int i;

	for (i = 0; i <= xfrm_state_hmask; i++) {
		struct hlist_node *entry;
		struct xfrm_state *x;

		hlist_for_each_entry(x, entry, xfrm_state_bydst+i, bydst) {
			if (x->km.seq == seq &&
			    x->km.state == XFRM_STATE_ACQ) {
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

int xfrm_alloc_spi(struct xfrm_state *x, u32 low, u32 high)
{
	unsigned int h;
	struct xfrm_state *x0;
	int err = -ENOENT;
	__be32 minspi = htonl(low);
	__be32 maxspi = htonl(high);

	spin_lock_bh(&x->lock);
	if (x->km.state == XFRM_STATE_DEAD)
		goto unlock;

	err = 0;
	if (x->id.spi)
		goto unlock;

	err = -ENOENT;

	if (minspi == maxspi) {
		x0 = xfrm_state_lookup(&x->id.daddr, minspi, x->id.proto, x->props.family);
		if (x0) {
			xfrm_state_put(x0);
			goto unlock;
		}
		x->id.spi = minspi;
	} else {
		u32 spi = 0;
		for (h=0; h<high-low+1; h++) {
			spi = low + net_random()%(high-low+1);
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
		hlist_add_head(&x->byspi, xfrm_state_byspi+h);
		spin_unlock_bh(&xfrm_state_lock);

		err = 0;
	}

unlock:
	spin_unlock_bh(&x->lock);

	return err;
}
EXPORT_SYMBOL(xfrm_alloc_spi);

int xfrm_state_walk(u8 proto, int (*func)(struct xfrm_state *, int, void*),
		    void *data)
{
	int i;
	struct xfrm_state *x, *last = NULL;
	struct hlist_node *entry;
	int count = 0;
	int err = 0;

	spin_lock_bh(&xfrm_state_lock);
	for (i = 0; i <= xfrm_state_hmask; i++) {
		hlist_for_each_entry(x, entry, xfrm_state_bydst+i, bydst) {
			if (!xfrm_id_proto_match(x->id.proto, proto))
				continue;
			if (last) {
				err = func(last, count, data);
				if (err)
					goto out;
			}
			last = x;
			count++;
		}
	}
	if (count == 0) {
		err = -ENOENT;
		goto out;
	}
	err = func(last, 0, data);
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
		    (x->replay.oseq - x->preplay.oseq < x->replay_maxdiff)) {
			if (x->xflags & XFRM_TIME_DEFER)
				event = XFRM_REPLAY_TIMEOUT;
			else
				return;
		}

		break;

	case XFRM_REPLAY_TIMEOUT:
		if ((x->replay.seq == x->preplay.seq) &&
		    (x->replay.bitmap == x->preplay.bitmap) &&
		    (x->replay.oseq == x->preplay.oseq)) {
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

static void xfrm_replay_timer_handler(unsigned long data)
{
	struct xfrm_state *x = (struct xfrm_state*)data;

	spin_lock(&x->lock);

	if (x->km.state == XFRM_STATE_VALID) {
		if (xfrm_aevent_is_on())
			xfrm_replay_notify(x, XFRM_REPLAY_TIMEOUT);
		else
			x->xflags |= XFRM_TIME_DEFER;
	}

	spin_unlock(&x->lock);
}

int xfrm_replay_check(struct xfrm_state *x, __be32 net_seq)
{
	u32 diff;
	u32 seq = ntohl(net_seq);

	if (unlikely(seq == 0))
		return -EINVAL;

	if (likely(seq > x->replay.seq))
		return 0;

	diff = x->replay.seq - seq;
	if (diff >= min_t(unsigned int, x->props.replay_window,
			  sizeof(x->replay.bitmap) * 8)) {
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

void xfrm_replay_advance(struct xfrm_state *x, __be32 net_seq)
{
	u32 diff;
	u32 seq = ntohl(net_seq);

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

int km_new_mapping(struct xfrm_state *x, xfrm_address_t *ipaddr, __be16 sport)
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

int km_migrate(struct xfrm_selector *sel, u8 dir, u8 type,
	       struct xfrm_migrate *m, int num_migrate)
{
	int err = -EINVAL;
	int ret;
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		if (km->migrate) {
			ret = km->migrate(sel, dir, type, m, num_migrate);
			if (!ret)
				err = ret;
		}
	}
	read_unlock(&xfrm_km_lock);
	return err;
}
EXPORT_SYMBOL(km_migrate);

int km_report(u8 proto, struct xfrm_selector *sel, xfrm_address_t *addr)
{
	int err = -EINVAL;
	int ret;
	struct xfrm_mgr *km;

	read_lock(&xfrm_km_lock);
	list_for_each_entry(km, &xfrm_km_list, list) {
		if (km->report) {
			ret = km->report(proto, sel, addr);
			if (!ret)
				err = ret;
		}
	}
	read_unlock(&xfrm_km_lock);
	return err;
}
EXPORT_SYMBOL(km_report);

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
		pol = km->compile_policy(sk, optname, data,
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
	write_lock_bh(&xfrm_state_afinfo_lock);
	if (unlikely(xfrm_state_afinfo[afinfo->family] != NULL))
		err = -ENOBUFS;
	else
		xfrm_state_afinfo[afinfo->family] = afinfo;
	write_unlock_bh(&xfrm_state_afinfo_lock);
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
	write_lock_bh(&xfrm_state_afinfo_lock);
	if (likely(xfrm_state_afinfo[afinfo->family] != NULL)) {
		if (unlikely(xfrm_state_afinfo[afinfo->family] != afinfo))
			err = -EINVAL;
		else
			xfrm_state_afinfo[afinfo->family] = NULL;
	}
	write_unlock_bh(&xfrm_state_afinfo_lock);
	return err;
}
EXPORT_SYMBOL(xfrm_state_unregister_afinfo);

static struct xfrm_state_afinfo *xfrm_state_get_afinfo(unsigned int family)
{
	struct xfrm_state_afinfo *afinfo;
	if (unlikely(family >= NPROTO))
		return NULL;
	read_lock(&xfrm_state_afinfo_lock);
	afinfo = xfrm_state_afinfo[family];
	if (unlikely(!afinfo))
		read_unlock(&xfrm_state_afinfo_lock);
	return afinfo;
}

static void xfrm_state_put_afinfo(struct xfrm_state_afinfo *afinfo)
{
	read_unlock(&xfrm_state_afinfo_lock);
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

int xfrm_state_mtu(struct xfrm_state *x, int mtu)
{
	int res;

	spin_lock_bh(&x->lock);
	if (x->km.state == XFRM_STATE_VALID &&
	    x->type && x->type->get_mtu)
		res = x->type->get_mtu(x, mtu);
	else
		res = mtu - x->props.header_len;
	spin_unlock_bh(&x->lock);
	return res;
}

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
	x->inner_mode = xfrm_get_mode(x->props.mode, x->sel.family);
	if (x->inner_mode == NULL)
		goto error;

	if (!(x->inner_mode->flags & XFRM_MODE_FLAG_TUNNEL) &&
	    family != x->sel.family)
		goto error;

	x->type = xfrm_get_type(x->id.proto, family);
	if (x->type == NULL)
		goto error;

	err = x->type->init_state(x);
	if (err)
		goto error;

	x->outer_mode = xfrm_get_mode(x->props.mode, family);
	if (x->outer_mode == NULL)
		goto error;

	x->km.state = XFRM_STATE_VALID;

error:
	return err;
}

EXPORT_SYMBOL(xfrm_init_state);

void __init xfrm_state_init(void)
{
	unsigned int sz;

	sz = sizeof(struct hlist_head) * 8;

	xfrm_state_bydst = xfrm_hash_alloc(sz);
	xfrm_state_bysrc = xfrm_hash_alloc(sz);
	xfrm_state_byspi = xfrm_hash_alloc(sz);
	if (!xfrm_state_bydst || !xfrm_state_bysrc || !xfrm_state_byspi)
		panic("XFRM: Cannot allocate bydst/bysrc/byspi hashes.");
	xfrm_state_hmask = ((sz / sizeof(struct hlist_head)) - 1);

	INIT_WORK(&xfrm_state_gc_work, xfrm_state_gc_task);
}

#ifdef CONFIG_AUDITSYSCALL
static inline void xfrm_audit_common_stateinfo(struct xfrm_state *x,
					       struct audit_buffer *audit_buf)
{
	if (x->security)
		audit_log_format(audit_buf, " sec_alg=%u sec_doi=%u sec_obj=%s",
				 x->security->ctx_alg, x->security->ctx_doi,
				 x->security->ctx_str);

	switch(x->props.family) {
	case AF_INET:
		audit_log_format(audit_buf, " src=%u.%u.%u.%u dst=%u.%u.%u.%u",
				 NIPQUAD(x->props.saddr.a4),
				 NIPQUAD(x->id.daddr.a4));
		break;
	case AF_INET6:
		{
			struct in6_addr saddr6, daddr6;

			memcpy(&saddr6, x->props.saddr.a6,
				sizeof(struct in6_addr));
			memcpy(&daddr6, x->id.daddr.a6,
				sizeof(struct in6_addr));
			audit_log_format(audit_buf,
					 " src=" NIP6_FMT " dst=" NIP6_FMT,
					 NIP6(saddr6), NIP6(daddr6));
		}
		break;
	}
}

void
xfrm_audit_state_add(struct xfrm_state *x, int result, u32 auid, u32 sid)
{
	struct audit_buffer *audit_buf;
	extern int audit_enabled;

	if (audit_enabled == 0)
		return;
	audit_buf = xfrm_audit_start(sid, auid);
	if (audit_buf == NULL)
		return;
	audit_log_format(audit_buf, " op=SAD-add res=%u",result);
	xfrm_audit_common_stateinfo(x, audit_buf);
	audit_log_format(audit_buf, " spi=%lu(0x%lx)",
			 (unsigned long)x->id.spi, (unsigned long)x->id.spi);
	audit_log_end(audit_buf);
}
EXPORT_SYMBOL_GPL(xfrm_audit_state_add);

void
xfrm_audit_state_delete(struct xfrm_state *x, int result, u32 auid, u32 sid)
{
	struct audit_buffer *audit_buf;
	extern int audit_enabled;

	if (audit_enabled == 0)
		return;
	audit_buf = xfrm_audit_start(sid, auid);
	if (audit_buf == NULL)
		return;
	audit_log_format(audit_buf, " op=SAD-delete res=%u",result);
	xfrm_audit_common_stateinfo(x, audit_buf);
	audit_log_format(audit_buf, " spi=%lu(0x%lx)",
			 (unsigned long)x->id.spi, (unsigned long)x->id.spi);
	audit_log_end(audit_buf);
}
EXPORT_SYMBOL_GPL(xfrm_audit_state_delete);
#endif /* CONFIG_AUDITSYSCALL */
