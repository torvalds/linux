/*
 * xfrm6_state.c: based on xfrm4_state.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 * 		IPv6 support
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 * 	
 */

#include <net/xfrm.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>
#include <net/ipv6.h>
#include <net/addrconf.h>

static struct xfrm_state_afinfo xfrm6_state_afinfo;

static void
__xfrm6_init_tempsel(struct xfrm_state *x, struct flowi *fl,
		     struct xfrm_tmpl *tmpl,
		     xfrm_address_t *daddr, xfrm_address_t *saddr)
{
	/* Initialize temporary selector matching only
	 * to current session. */
	ipv6_addr_copy((struct in6_addr *)&x->sel.daddr, &fl->fl6_dst);
	ipv6_addr_copy((struct in6_addr *)&x->sel.saddr, &fl->fl6_src);
	x->sel.dport = xfrm_flowi_dport(fl);
	x->sel.dport_mask = ~0;
	x->sel.sport = xfrm_flowi_sport(fl);
	x->sel.sport_mask = ~0;
	x->sel.prefixlen_d = 128;
	x->sel.prefixlen_s = 128;
	x->sel.proto = fl->proto;
	x->sel.ifindex = fl->oif;
	x->id = tmpl->id;
	if (ipv6_addr_any((struct in6_addr*)&x->id.daddr))
		memcpy(&x->id.daddr, daddr, sizeof(x->sel.daddr));
	memcpy(&x->props.saddr, &tmpl->saddr, sizeof(x->props.saddr));
	if (ipv6_addr_any((struct in6_addr*)&x->props.saddr))
		memcpy(&x->props.saddr, saddr, sizeof(x->props.saddr));
	if (tmpl->mode && ipv6_addr_any((struct in6_addr*)&x->props.saddr)) {
		struct rt6_info *rt;
		struct flowi fl_tunnel = {
			.nl_u = {
				.ip6_u = {
					.daddr = *(struct in6_addr *)daddr,
				}
			}
		};
		if (!xfrm_dst_lookup((struct xfrm_dst **)&rt,
		                     &fl_tunnel, AF_INET6)) {
			ipv6_get_saddr(&rt->u.dst, (struct in6_addr *)daddr,
			               (struct in6_addr *)&x->props.saddr);
			dst_release(&rt->u.dst);
		}
	}
	x->props.mode = tmpl->mode;
	x->props.reqid = tmpl->reqid;
	x->props.family = AF_INET6;
}

static struct xfrm_state *
__xfrm6_state_lookup(xfrm_address_t *daddr, u32 spi, u8 proto)
{
	unsigned h = __xfrm6_spi_hash(daddr, spi, proto);
	struct xfrm_state *x;

	list_for_each_entry(x, xfrm6_state_afinfo.state_byspi+h, byspi) {
		if (x->props.family == AF_INET6 &&
		    spi == x->id.spi &&
		    ipv6_addr_equal((struct in6_addr *)daddr, (struct in6_addr *)x->id.daddr.a6) &&
		    proto == x->id.proto) {
			xfrm_state_hold(x);
			return x;
		}
	}
	return NULL;
}

static struct xfrm_state *
__xfrm6_find_acq(u8 mode, u32 reqid, u8 proto, 
		 xfrm_address_t *daddr, xfrm_address_t *saddr, 
		 int create)
{
	struct xfrm_state *x, *x0;
	unsigned h = __xfrm6_dst_hash(daddr);

	x0 = NULL;

	list_for_each_entry(x, xfrm6_state_afinfo.state_bydst+h, bydst) {
		if (x->props.family == AF_INET6 &&
		    ipv6_addr_equal((struct in6_addr *)daddr, (struct in6_addr *)x->id.daddr.a6) &&
		    mode == x->props.mode &&
		    proto == x->id.proto &&
		    ipv6_addr_equal((struct in6_addr *)saddr, (struct in6_addr *)x->props.saddr.a6) &&
		    reqid == x->props.reqid &&
		    x->km.state == XFRM_STATE_ACQ &&
		    !x->id.spi) {
			    x0 = x;
			    break;
		    }
	}
	if (!x0 && create && (x0 = xfrm_state_alloc()) != NULL) {
		ipv6_addr_copy((struct in6_addr *)x0->sel.daddr.a6,
			       (struct in6_addr *)daddr);
		ipv6_addr_copy((struct in6_addr *)x0->sel.saddr.a6,
			       (struct in6_addr *)saddr);
		x0->sel.prefixlen_d = 128;
		x0->sel.prefixlen_s = 128;
		ipv6_addr_copy((struct in6_addr *)x0->props.saddr.a6,
			       (struct in6_addr *)saddr);
		x0->km.state = XFRM_STATE_ACQ;
		ipv6_addr_copy((struct in6_addr *)x0->id.daddr.a6,
			       (struct in6_addr *)daddr);
		x0->id.proto = proto;
		x0->props.family = AF_INET6;
		x0->props.mode = mode;
		x0->props.reqid = reqid;
		x0->lft.hard_add_expires_seconds = XFRM_ACQ_EXPIRES;
		xfrm_state_hold(x0);
		x0->timer.expires = jiffies + XFRM_ACQ_EXPIRES*HZ;
		add_timer(&x0->timer);
		xfrm_state_hold(x0);
		list_add_tail(&x0->bydst, xfrm6_state_afinfo.state_bydst+h);
		wake_up(&km_waitq);
	}
	if (x0)
		xfrm_state_hold(x0);
	return x0;
}

static struct xfrm_state_afinfo xfrm6_state_afinfo = {
	.family			= AF_INET6,
	.init_tempsel		= __xfrm6_init_tempsel,
	.state_lookup		= __xfrm6_state_lookup,
	.find_acq		= __xfrm6_find_acq,
};

void __init xfrm6_state_init(void)
{
	xfrm_state_register_afinfo(&xfrm6_state_afinfo);
}

void xfrm6_state_fini(void)
{
	xfrm_state_unregister_afinfo(&xfrm6_state_afinfo);
}

