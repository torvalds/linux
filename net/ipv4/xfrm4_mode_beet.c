/*
 * xfrm4_mode_beet.c - BEET mode encapsulation for IPv4.
 *
 * Copyright (c) 2006 Diego Beltrami <diego.beltrami@gmail.com>
 *                    Miika Komu     <miika@iki.fi>
 *                    Herbert Xu     <herbert@gondor.apana.org.au>
 *                    Abhinav Pathak <abhinav.pathak@hiit.fi>
 *                    Jeff Ahrenholz <ahrenholz@gmail.com>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>


static struct xfrm_mode xfrm4_beet_mode = {
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_BEET,
	.flags = XFRM_MODE_FLAG_TUNNEL,
	.family = AF_INET,
};

static int __init xfrm4_beet_init(void)
{
	return xfrm_register_mode(&xfrm4_beet_mode);
}

static void __exit xfrm4_beet_exit(void)
{
	xfrm_unregister_mode(&xfrm4_beet_mode);
}

module_init(xfrm4_beet_init);
module_exit(xfrm4_beet_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_BEET);
