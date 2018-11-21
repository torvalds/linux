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

#define TUN_XDP_FLAG 0x1UL

#if defined(CONFIG_TUN) || defined(CONFIG_TUN_MODULE)
struct socket *tun_get_socket(struct file *);
struct ptr_ring *tun_get_tx_ring(struct file *file);
bool tun_is_xdp_frame(void *ptr);
void *tun_xdp_to_ptr(void *ptr);
void *tun_ptr_to_xdp(void *ptr);
void tun_ptr_free(void *ptr);
#else
#include <linux/err.h>
#include <linux/errno.h>
struct file;
struct socket;
static inline struct socket *tun_get_socket(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
static inline struct ptr_ring *tun_get_tx_ring(struct file *f)
{
	return ERR_PTR(-EINVAL);
}
static inline bool tun_is_xdp_frame(void *ptr)
{
	return false;
}
static inline void *tun_xdp_to_ptr(void *ptr)
{
	return NULL;
}
static inline void *tun_ptr_to_xdp(void *ptr)
{
	return NULL;
}
static inline void tun_ptr_free(void *ptr)
{
}
#endif /* CONFIG_TUN */
#endif /* __IF_TUN_H */
