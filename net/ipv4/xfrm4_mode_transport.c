/*
 * xfrm4_mode_transport.c - Transport mode encapsulation for IPv4.
 *
 * Copyright (c) 2004-2006 Herbert Xu <herbert@gondor.apana.org.au>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/stringify.h>
#include <net/dst.h>
#include <net/ip.h>
#include <net/xfrm.h>
#include <net/protocol.h>

static struct xfrm_mode xfrm4_transport_mode = {
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_TRANSPORT,
	.family = AF_INET,
};

static int __init xfrm4_transport_init(void)
{
	return xfrm_register_mode(&xfrm4_transport_mode);
}

static void __exit xfrm4_transport_exit(void)
{
	xfrm_unregister_mode(&xfrm4_transport_mode);
}

module_init(xfrm4_transport_init);
module_exit(xfrm4_transport_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET, XFRM_MODE_TRANSPORT);
