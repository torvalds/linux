/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors	Mitsuru KANDA  <mk@linux-ipv6.org>
 * 		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 *
 * Based on net/ipv4/xfrm4_tunnel.c
 *
 */
#include <linux/module.h>
#include <linux/xfrm.h>
#include <linux/list.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/ipv6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/mutex.h>

/*
 * xfrm_tunnel_spi things are for allocating unique id ("spi") 
 * per xfrm_address_t.
 */
struct xfrm6_tunnel_spi {
	struct hlist_node list_byaddr;
	struct hlist_node list_byspi;
	xfrm_address_t addr;
	u32 spi;
	atomic_t refcnt;
};

static DEFINE_RWLOCK(xfrm6_tunnel_spi_lock);

static u32 xfrm6_tunnel_spi;

#define XFRM6_TUNNEL_SPI_MIN	1
#define XFRM6_TUNNEL_SPI_MAX	0xffffffff

static kmem_cache_t *xfrm6_tunnel_spi_kmem __read_mostly;

#define XFRM6_TUNNEL_SPI_BYADDR_HSIZE 256
#define XFRM6_TUNNEL_SPI_BYSPI_HSIZE 256

static struct hlist_head xfrm6_tunnel_spi_byaddr[XFRM6_TUNNEL_SPI_BYADDR_HSIZE];
static struct hlist_head xfrm6_tunnel_spi_byspi[XFRM6_TUNNEL_SPI_BYSPI_HSIZE];

static unsigned inline xfrm6_tunnel_spi_hash_byaddr(xfrm_address_t *addr)
{
	unsigned h;

	h = addr->a6[0] ^ addr->a6[1] ^ addr->a6[2] ^ addr->a6[3];
	h ^= h >> 16;
	h ^= h >> 8;
	h &= XFRM6_TUNNEL_SPI_BYADDR_HSIZE - 1;

	return h;
}

static unsigned inline xfrm6_tunnel_spi_hash_byspi(u32 spi)
{
	return spi % XFRM6_TUNNEL_SPI_BYSPI_HSIZE;
}


static int xfrm6_tunnel_spi_init(void)
{
	int i;

	xfrm6_tunnel_spi = 0;
	xfrm6_tunnel_spi_kmem = kmem_cache_create("xfrm6_tunnel_spi",
						  sizeof(struct xfrm6_tunnel_spi),
						  0, SLAB_HWCACHE_ALIGN,
						  NULL, NULL);
	if (!xfrm6_tunnel_spi_kmem)
		return -ENOMEM;

	for (i = 0; i < XFRM6_TUNNEL_SPI_BYADDR_HSIZE; i++)
		INIT_HLIST_HEAD(&xfrm6_tunnel_spi_byaddr[i]);
	for (i = 0; i < XFRM6_TUNNEL_SPI_BYSPI_HSIZE; i++)
		INIT_HLIST_HEAD(&xfrm6_tunnel_spi_byspi[i]);
	return 0;
}

static void xfrm6_tunnel_spi_fini(void)
{
	int i;

	for (i = 0; i < XFRM6_TUNNEL_SPI_BYADDR_HSIZE; i++) {
		if (!hlist_empty(&xfrm6_tunnel_spi_byaddr[i]))
			return;
	}
	for (i = 0; i < XFRM6_TUNNEL_SPI_BYSPI_HSIZE; i++) {
		if (!hlist_empty(&xfrm6_tunnel_spi_byspi[i]))
			return;
	}
	kmem_cache_destroy(xfrm6_tunnel_spi_kmem);
	xfrm6_tunnel_spi_kmem = NULL;
}

static struct xfrm6_tunnel_spi *__xfrm6_tunnel_spi_lookup(xfrm_address_t *saddr)
{
	struct xfrm6_tunnel_spi *x6spi;
	struct hlist_node *pos;

	hlist_for_each_entry(x6spi, pos,
			     &xfrm6_tunnel_spi_byaddr[xfrm6_tunnel_spi_hash_byaddr(saddr)],
			     list_byaddr) {
		if (memcmp(&x6spi->addr, saddr, sizeof(x6spi->addr)) == 0)
			return x6spi;
	}

	return NULL;
}

u32 xfrm6_tunnel_spi_lookup(xfrm_address_t *saddr)
{
	struct xfrm6_tunnel_spi *x6spi;
	u32 spi;

	read_lock_bh(&xfrm6_tunnel_spi_lock);
	x6spi = __xfrm6_tunnel_spi_lookup(saddr);
	spi = x6spi ? x6spi->spi : 0;
	read_unlock_bh(&xfrm6_tunnel_spi_lock);
	return htonl(spi);
}

EXPORT_SYMBOL(xfrm6_tunnel_spi_lookup);

static u32 __xfrm6_tunnel_alloc_spi(xfrm_address_t *saddr)
{
	u32 spi;
	struct xfrm6_tunnel_spi *x6spi;
	struct hlist_node *pos;
	unsigned index;

	if (xfrm6_tunnel_spi < XFRM6_TUNNEL_SPI_MIN ||
	    xfrm6_tunnel_spi >= XFRM6_TUNNEL_SPI_MAX)
		xfrm6_tunnel_spi = XFRM6_TUNNEL_SPI_MIN;
	else
		xfrm6_tunnel_spi++;

	for (spi = xfrm6_tunnel_spi; spi <= XFRM6_TUNNEL_SPI_MAX; spi++) {
		index = xfrm6_tunnel_spi_hash_byspi(spi);
		hlist_for_each_entry(x6spi, pos, 
				     &xfrm6_tunnel_spi_byspi[index], 
				     list_byspi) {
			if (x6spi->spi == spi)
				goto try_next_1;
		}
		xfrm6_tunnel_spi = spi;
		goto alloc_spi;
try_next_1:;
	}
	for (spi = XFRM6_TUNNEL_SPI_MIN; spi < xfrm6_tunnel_spi; spi++) {
		index = xfrm6_tunnel_spi_hash_byspi(spi);
		hlist_for_each_entry(x6spi, pos, 
				     &xfrm6_tunnel_spi_byspi[index], 
				     list_byspi) {
			if (x6spi->spi == spi)
				goto try_next_2;
		}
		xfrm6_tunnel_spi = spi;
		goto alloc_spi;
try_next_2:;
	}
	spi = 0;
	goto out;
alloc_spi:
	x6spi = kmem_cache_alloc(xfrm6_tunnel_spi_kmem, SLAB_ATOMIC);
	if (!x6spi)
		goto out;

	memcpy(&x6spi->addr, saddr, sizeof(x6spi->addr));
	x6spi->spi = spi;
	atomic_set(&x6spi->refcnt, 1);

	hlist_add_head(&x6spi->list_byspi, &xfrm6_tunnel_spi_byspi[index]);

	index = xfrm6_tunnel_spi_hash_byaddr(saddr);
	hlist_add_head(&x6spi->list_byaddr, &xfrm6_tunnel_spi_byaddr[index]);
out:
	return spi;
}

u32 xfrm6_tunnel_alloc_spi(xfrm_address_t *saddr)
{
	struct xfrm6_tunnel_spi *x6spi;
	u32 spi;

	write_lock_bh(&xfrm6_tunnel_spi_lock);
	x6spi = __xfrm6_tunnel_spi_lookup(saddr);
	if (x6spi) {
		atomic_inc(&x6spi->refcnt);
		spi = x6spi->spi;
	} else
		spi = __xfrm6_tunnel_alloc_spi(saddr);
	write_unlock_bh(&xfrm6_tunnel_spi_lock);

	return htonl(spi);
}

EXPORT_SYMBOL(xfrm6_tunnel_alloc_spi);

void xfrm6_tunnel_free_spi(xfrm_address_t *saddr)
{
	struct xfrm6_tunnel_spi *x6spi;
	struct hlist_node *pos, *n;

	write_lock_bh(&xfrm6_tunnel_spi_lock);

	hlist_for_each_entry_safe(x6spi, pos, n, 
				  &xfrm6_tunnel_spi_byaddr[xfrm6_tunnel_spi_hash_byaddr(saddr)],
				  list_byaddr)
	{
		if (memcmp(&x6spi->addr, saddr, sizeof(x6spi->addr)) == 0) {
			if (atomic_dec_and_test(&x6spi->refcnt)) {
				hlist_del(&x6spi->list_byaddr);
				hlist_del(&x6spi->list_byspi);
				kmem_cache_free(xfrm6_tunnel_spi_kmem, x6spi);
				break;
			}
		}
	}
	write_unlock_bh(&xfrm6_tunnel_spi_lock);
}

EXPORT_SYMBOL(xfrm6_tunnel_free_spi);

static int xfrm6_tunnel_output(struct xfrm_state *x, struct sk_buff *skb)
{
	struct ipv6hdr *top_iph;

	top_iph = (struct ipv6hdr *)skb->data;
	top_iph->payload_len = htons(skb->len - sizeof(struct ipv6hdr));

	return 0;
}

static int xfrm6_tunnel_input(struct xfrm_state *x, struct sk_buff *skb)
{
	return 0;
}

static int xfrm6_tunnel_rcv(struct sk_buff *skb)
{
	struct ipv6hdr *iph = skb->nh.ipv6h;
	__be32 spi;

	spi = xfrm6_tunnel_spi_lookup((xfrm_address_t *)&iph->saddr);
	return xfrm6_rcv_spi(skb, spi);
}

static int xfrm6_tunnel_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
			    int type, int code, int offset, __u32 info)
{
	/* xfrm6_tunnel native err handling */
	switch (type) {
	case ICMPV6_DEST_UNREACH: 
		switch (code) {
		case ICMPV6_NOROUTE: 
		case ICMPV6_ADM_PROHIBITED:
		case ICMPV6_NOT_NEIGHBOUR:
		case ICMPV6_ADDR_UNREACH:
		case ICMPV6_PORT_UNREACH:
		default:
			break;
		}
		break;
	case ICMPV6_PKT_TOOBIG:
		break;
	case ICMPV6_TIME_EXCEED:
		switch (code) {
		case ICMPV6_EXC_HOPLIMIT:
			break;
		case ICMPV6_EXC_FRAGTIME:
		default: 
			break;
		}
		break;
	case ICMPV6_PARAMPROB:
		switch (code) {
		case ICMPV6_HDR_FIELD: break;
		case ICMPV6_UNK_NEXTHDR: break;
		case ICMPV6_UNK_OPTION: break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int xfrm6_tunnel_init_state(struct xfrm_state *x)
{
	if (x->props.mode != XFRM_MODE_TUNNEL)
		return -EINVAL;

	if (x->encap)
		return -EINVAL;

	x->props.header_len = sizeof(struct ipv6hdr);

	return 0;
}

static void xfrm6_tunnel_destroy(struct xfrm_state *x)
{
	xfrm6_tunnel_free_spi((xfrm_address_t *)&x->props.saddr);
}

static struct xfrm_type xfrm6_tunnel_type = {
	.description	= "IP6IP6",
	.owner          = THIS_MODULE,
	.proto		= IPPROTO_IPV6,
	.init_state	= xfrm6_tunnel_init_state,
	.destructor	= xfrm6_tunnel_destroy,
	.input		= xfrm6_tunnel_input,
	.output		= xfrm6_tunnel_output,
};

static struct xfrm6_tunnel xfrm6_tunnel_handler = {
	.handler	= xfrm6_tunnel_rcv,
	.err_handler	= xfrm6_tunnel_err,
	.priority	= 2,
};

static int __init xfrm6_tunnel_init(void)
{
	if (xfrm_register_type(&xfrm6_tunnel_type, AF_INET6) < 0)
		return -EAGAIN;

	if (xfrm6_tunnel_register(&xfrm6_tunnel_handler)) {
		xfrm_unregister_type(&xfrm6_tunnel_type, AF_INET6);
		return -EAGAIN;
	}
	if (xfrm6_tunnel_spi_init() < 0) {
		xfrm6_tunnel_deregister(&xfrm6_tunnel_handler);
		xfrm_unregister_type(&xfrm6_tunnel_type, AF_INET6);
		return -EAGAIN;
	}
	return 0;
}

static void __exit xfrm6_tunnel_fini(void)
{
	xfrm6_tunnel_spi_fini();
	xfrm6_tunnel_deregister(&xfrm6_tunnel_handler);
	xfrm_unregister_type(&xfrm6_tunnel_type, AF_INET6);
}

module_init(xfrm6_tunnel_init);
module_exit(xfrm6_tunnel_fini);
MODULE_LICENSE("GPL");
