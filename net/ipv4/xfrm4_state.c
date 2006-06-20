/*
 * xfrm4_state.c
 *
 * Changes:
 * 	YOSHIFUJI Hideaki @USAGI
 * 		Split up af-specific portion
 *
 */

#include <net/ip.h>
#include <net/xfrm.h>
#include <linux/pfkeyv2.h>
#include <linux/ipsec.h>

static struct xfrm_state_afinfo xfrm4_state_afinfo;

static int xfrm4_init_flags(struct xfrm_state *x)
{
	if (ipv4_config.no_pmtu_disc)
		x->props.flags |= XFRM_STATE_NOPMTUDISC;
	return 0;
}

static void
__xfrm4_init_tempsel(struct xfrm_state *x, struct flowi *fl,
		     struct xfrm_tmpl *tmpl,
		     xfrm_address_t *daddr, xfrm_address_t *saddr)
{
	x->sel.daddr.a4 = fl->fl4_dst;
	x->sel.saddr.a4 = fl->fl4_src;
	x->sel.dport = xfrm_flowi_dport(fl);
	x->sel.dport_mask = ~0;
	x->sel.sport = xfrm_flowi_sport(fl);
	x->sel.sport_mask = ~0;
	x->sel.prefixlen_d = 32;
	x->sel.prefixlen_s = 32;
	x->sel.proto = fl->proto;
	x->sel.ifindex = fl->oif;
	x->id = tmpl->id;
	if (x->id.daddr.a4 == 0)
		x->id.daddr.a4 = daddr->a4;
	x->props.saddr = tmpl->saddr;
	if (x->props.saddr.a4 == 0)
		x->props.saddr.a4 = saddr->a4;
	if (tmpl->mode && x->props.saddr.a4 == 0) {
		struct rtable *rt;
	        struct flowi fl_tunnel = {
        	        .nl_u = {
        			.ip4_u = {
					.daddr = x->id.daddr.a4,
				}
			}
		};
		if (!xfrm_dst_lookup((struct xfrm_dst **)&rt,
		                     &fl_tunnel, AF_INET)) {
			x->props.saddr.a4 = rt->rt_src;
			dst_release(&rt->u.dst);
		}
	}
	x->props.mode = tmpl->mode;
	x->props.reqid = tmpl->reqid;
	x->props.family = AF_INET;
}

static struct xfrm_state *
__xfrm4_state_lookup(xfrm_address_t *daddr, u32 spi, u8 proto)
{
	unsigned h = __xfrm4_spi_hash(daddr, spi, proto);
	struct xfrm_state *x;

	list_for_each_entry(x, xfrm4_state_afinfo.state_byspi+h, byspi) {
		if (x->props.family == AF_INET &&
		    spi == x->id.spi &&
		    daddr->a4 == x->id.daddr.a4 &&
		    proto == x->id.proto) {
			xfrm_state_hold(x);
			return x;
		}
	}
	return NULL;
}

static struct xfrm_state *
__xfrm4_find_acq(u8 mode, u32 reqid, u8 proto, 
		 xfrm_address_t *daddr, xfrm_address_t *saddr, 
		 int create)
{
	struct xfrm_state *x, *x0;
	unsigned h = __xfrm4_dst_hash(daddr);

	x0 = NULL;

	list_for_each_entry(x, xfrm4_state_afinfo.state_bydst+h, bydst) {
		if (x->props.family == AF_INET &&
		    daddr->a4 == x->id.daddr.a4 &&
		    mode == x->props.mode &&
		    proto == x->id.proto &&
		    saddr->a4 == x->props.saddr.a4 &&
		    reqid == x->props.reqid &&
		    x->km.state == XFRM_STATE_ACQ &&
		    !x->id.spi) {
			    x0 = x;
			    break;
		    }
	}
	if (!x0 && create && (x0 = xfrm_state_alloc()) != NULL) {
		x0->sel.daddr.a4 = daddr->a4;
		x0->sel.saddr.a4 = saddr->a4;
		x0->sel.prefixlen_d = 32;
		x0->sel.prefixlen_s = 32;
		x0->props.saddr.a4 = saddr->a4;
		x0->km.state = XFRM_STATE_ACQ;
		x0->id.daddr.a4 = daddr->a4;
		x0->id.proto = proto;
		x0->props.family = AF_INET;
		x0->props.mode = mode;
		x0->props.reqid = reqid;
		x0->props.family = AF_INET;
		x0->lft.hard_add_expires_seconds = XFRM_ACQ_EXPIRES;
		xfrm_state_hold(x0);
		x0->timer.expires = jiffies + XFRM_ACQ_EXPIRES*HZ;
		add_timer(&x0->timer);
		xfrm_state_hold(x0);
		list_add_tail(&x0->bydst, xfrm4_state_afinfo.state_bydst+h);
		wake_up(&km_waitq);
	}
	if (x0)
		xfrm_state_hold(x0);
	return x0;
}

static struct xfrm_state_afinfo xfrm4_state_afinfo = {
	.family			= AF_INET,
	.init_flags		= xfrm4_init_flags,
	.init_tempsel		= __xfrm4_init_tempsel,
	.state_lookup		= __xfrm4_state_lookup,
	.find_acq		= __xfrm4_find_acq,
};

void __init xfrm4_state_init(void)
{
	xfrm_state_register_afinfo(&xfrm4_state_afinfo);
}

#if 0
void __exit xfrm4_state_fini(void)
{
	xfrm_state_unregister_afinfo(&xfrm4_state_afinfo);
}
#endif  /*  0  */

