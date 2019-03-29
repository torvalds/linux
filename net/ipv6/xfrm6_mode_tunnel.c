/*
 * xfrm6_mode_tunnel.c - Tunnel mode encapsulation for IPv6.
 *
 * Copyright (C) 2002 USAGI/WIDE Project
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dsfield.h>
#include <net/dst.h>
#include <net/inet_ecn.h>
#include <net/ip6_route.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

/* Add encapsulation header.
 *
 * The top IP header will be constructed per RFC 2401.
 */
static struct xfrm_mode xfrm6_tunnel_mode = {
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TUNNEL,
	.flags = XFRM_MODE_FLAG_TUNNEL,
	.family = AF_INET6,
};

static int __init xfrm6_mode_tunnel_init(void)
{
	return xfrm_register_mode(&xfrm6_tunnel_mode);
}

static void __exit xfrm6_mode_tunnel_exit(void)
{
	xfrm_unregister_mode(&xfrm6_tunnel_mode);
}

module_init(xfrm6_mode_tunnel_init);
module_exit(xfrm6_mode_tunnel_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_TUNNEL);
