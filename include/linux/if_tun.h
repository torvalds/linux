/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 */
#ifndef __IF_TUN_H
#define __IF_TUN_H

#include <uapi/linux/if_tun.h>
#include <uapi/linux/virtio_net.h>

#define TUN_XDP_FLAG 0x1UL

#define TUN_MSG_UBUF 1
#define TUN_MSG_PTR  2
struct tun_msg_ctl {
	unsigned short type;
	unsigned short num;
	void *ptr;
};

struct tun_xdp_hdr {
	int buflen;
	struct virtio_net_hdr gso;
};

#if defined(CONFIG_TUN) || defined(CONFIG_TUN_MODULE)
struct socket *tun_get_socket(struct file *);
struct ptr_ring *tun_get_tx_ring(struct file *file);

static inline bool tun_is_xdp_frame(void *ptr)
{
	return (unsigned long)ptr & TUN_XDP_FLAG;
}

static inline void *tun_xdp_to_ptr(struct xdp_frame *xdp)
{
	return (void *)((unsigned long)xdp | TUN_XDP_FLAG);
}

static inline struct xdp_frame *tun_ptr_to_xdp(void *ptr)
{
	return (void *)((unsigned long)ptr & ~TUN_XDP_FLAG);
}

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

static inline void *tun_xdp_to_ptr(struct xdp_frame *xdp)
{
	return NULL;
}

static inline struct xdp_frame *tun_ptr_to_xdp(void *ptr)
{
	return NULL;
}

static inline void tun_ptr_free(void *ptr)
{
}
#endif /* CONFIG_TUN */
#endif /* __IF_TUN_H */
