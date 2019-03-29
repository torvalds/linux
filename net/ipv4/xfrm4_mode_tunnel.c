/*
 * xfrm4_mode_tunnel.c - Tunnel mode encapsulation for IPv4.
 *
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/inet_ecn.h>
#include <net/ip.h>
#include <net/xfrm.h>

static struct xfrm_mode xfrm4_tunnel_mode = {
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TUNNEL,
	.flags = XFRM_MODE_FLAG_TUNNEL,
	.family = AF_INET,
};

static int __init xfrm4_mode_tunnel_init(void)
{
	return xfrm_register_mode(&xfrm4_tunnel_mode);
}

static void __exit xfrm4_mode_tunnel_exit(void)
{
	xfrm_unregister_mode(&xfrm4_tunnel_mode);
}

module_init(xfrm4_mode_tunnel_init);
module_exit(xfrm4_mode_tunnel_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_TUNNEL);
