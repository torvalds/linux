// SPDX-License-Identifier: GPL-2.0
/*
 * XFRM compat layer
 * Author: Dmitry Safonov <dima@arista.com>
 * Based on code and translator idea by: Florian Westphal <fw@strlen.de>
 */
#include <linux/compat.h>
#include <linux/xfrm.h>
#include <net/xfrm.h>

static struct xfrm_translator xfrm_translator = {
	.owner				= THIS_MODULE,
};

static int __init xfrm_compat_init(void)
{
	return xfrm_register_translator(&xfrm_translator);
}

static void __exit xfrm_compat_exit(void)
{
	xfrm_unregister_translator(&xfrm_translator);
}

module_init(xfrm_compat_init);
module_exit(xfrm_compat_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Safonov");
MODULE_DESCRIPTION("XFRM 32-bit compatibility layer");
