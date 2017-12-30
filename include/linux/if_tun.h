/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */
#ifndef __IF_TUN_H
#define __IF_TUN_H

#include <uapi/linux/if_tun.h>

#if defined(CONFIG_TUN) || defined(CONFIG_TUN_MODULE)
struct socket *tun_get_socket(struct file *);
struct skb_array *tun_get_skb_array(struct file *file);
#else
#include <linux/err.h>
#include <linux/errno.h>
struct file;
struct socket;
static inline struct socket *tun_get_socket(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
static inline struct skb_array *tun_get_skb_array(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
#endif /* CONFIG_TUN */
#endif /* __IF_TUN_H */
