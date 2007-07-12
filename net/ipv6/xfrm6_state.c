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
	x->sel.dport_mask = htons(0xffff);
	x->sel.sport = xfrm_flowi_sport(fl);
	x->sel.sport_mask = htons(0xffff);
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
	x->props.mode = tmpl->mode;
	x->props.reqid = tmpl->reqid;
	x->props.family = AF_INET6;
}

static int
__xfrm6_state_sort(struct xfrm_state **dst, struct xfrm_state **src, int n)
{
	int i;
	int j = 0;

	/* Rule 1: select IPsec transport except AH */
	for (i = 0; i < n; i++) {
		if (src[i]->props.mode == XFRM_MODE_TRANSPORT &&
		    src[i]->id.proto != IPPROTO_AH) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (j == n)
		goto end;

	/* Rule 2: select MIPv6 RO or inbound trigger */
#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
	for (i = 0; i < n; i++) {
		if (src[i] &&
		    (src[i]->props.mode == XFRM_MODE_ROUTEOPTIMIZATION ||
		     src[i]->props.mode == XFRM_MODE_IN_TRIGGER)) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (j == n)
		goto end;
#endif

	/* Rule 3: select IPsec transport AH */
	for (i = 0; i < n; i++) {
		if (src[i] &&
		    src[i]->props.mode == XFRM_MODE_TRANSPORT &&
		    src[i]->id.proto == IPPROTO_AH) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (j == n)
		goto end;

	/* Rule 4: select IPsec tunnel */
	for (i = 0; i < n; i++) {
		if (src[i] &&
		    src[i]->props.mode == XFRM_MODE_TUNNEL) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (likely(j == n))
		goto end;

	/* Final rule */
	for (i = 0; i < n; i++) {
		if (src[i]) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}

 end:
	return 0;
}

static int
__xfrm6_tmpl_sort(struct xfrm_tmpl **dst, struct xfrm_tmpl **src, int n)
{
	int i;
	int j = 0;

	/* Rule 1: select IPsec transport */
	for (i = 0; i < n; i++) {
		if (src[i]->mode == XFRM_MODE_TRANSPORT) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (j == n)
		goto end;

	/* Rule 2: select MIPv6 RO or inbound trigger */
#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
	for (i = 0; i < n; i++) {
		if (src[i] &&
		    (src[i]->mode == XFRM_MODE_ROUTEOPTIMIZATION ||
		     src[i]->mode == XFRM_MODE_IN_TRIGGER)) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (j == n)
		goto end;
#endif

	/* Rule 3: select IPsec tunnel */
	for (i = 0; i < n; i++) {
		if (src[i] &&
		    src[i]->mode == XFRM_MODE_TUNNEL) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}
	if (likely(j == n))
		goto end;

	/* Final rule */
	for (i = 0; i < n; i++) {
		if (src[i]) {
			dst[j++] = src[i];
			src[i] = NULL;
		}
	}

 end:
	return 0;
}

static struct xfrm_state_afinfo xfrm6_state_afinfo = {
	.family			= AF_INET6,
	.init_tempsel		= __xfrm6_init_tempsel,
	.tmpl_sort		= __xfrm6_tmpl_sort,
	.state_sort		= __xfrm6_state_sort,
	.output			= xfrm6_output,
};

void __init xfrm6_state_init(void)
{
	xfrm_state_register_afinfo(&xfrm6_state_afinfo);
}

void xfrm6_state_fini(void)
{
	xfrm_state_unregister_afinfo(&xfrm6_state_afinfo);
}

