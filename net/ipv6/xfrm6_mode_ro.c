/*
 * xfrm6_mode_ro.c - Route optimization mode for IPv6.
 *
 * Copyright (C)2003-2006 Helsinki University of Technology
 * Copyright (C)2003-2006 USAGI/WIDE Project
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Authors:
 *	Noriaki TAKAMIYA @USAGI
 *	Masahide NAKAMURA @USAGI
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/stringify.h>
#include <linux/time.h>
#include <net/ipv6.h>
#include <net/xfrm.h>

static struct xfrm_mode xfrm6_ro_mode = {
	.owner = THIS_MODULE,
	.encap = XFRM_MODE_ROUTEOPTIMIZATION,
	.family = AF_INET6,
};

static int __init xfrm6_ro_init(void)
{
	return xfrm_register_mode(&xfrm6_ro_mode);
}

static void __exit xfrm6_ro_exit(void)
{
	xfrm_unregister_mode(&xfrm6_ro_mode);
}

module_init(xfrm6_ro_init);
module_exit(xfrm6_ro_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_XFRM_MODE(AF_INET6, XFRM_MODE_ROUTEOPTIMIZATION);
