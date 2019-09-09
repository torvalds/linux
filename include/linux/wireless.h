/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file define a set of standard wireless extensions
 *
 * Version :	22	16.3.07
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2007 Jean Tourrilhes, All Rights Reserved.
 */
#ifndef _LINUX_WIRELESS_H
#define _LINUX_WIRELESS_H

#include <uapi/linux/wireless.h>

#ifdef CONFIG_COMPAT

#include <linux/compat.h>

struct compat_iw_point {
	compat_caddr_t pointer;
	__u16 length;
	__u16 flags;
};
#endif
#ifdef CONFIG_COMPAT
struct __compat_iw_event {
	__u16		len;			/* Real length of this stuff */
	__u16		cmd;			/* Wireless IOCTL */
	compat_caddr_t	pointer;
};
#define IW_EV_COMPAT_LCP_LEN offsetof(struct __compat_iw_event, pointer)
#define IW_EV_COMPAT_POINT_OFF offsetof(struct compat_iw_point, length)

/* Size of the various events for compat */
#define IW_EV_COMPAT_CHAR_LEN	(IW_EV_COMPAT_LCP_LEN + IFNAMSIZ)
#define IW_EV_COMPAT_UINT_LEN	(IW_EV_COMPAT_LCP_LEN + sizeof(__u32))
#define IW_EV_COMPAT_FREQ_LEN	(IW_EV_COMPAT_LCP_LEN + sizeof(struct iw_freq))
#define IW_EV_COMPAT_PARAM_LEN	(IW_EV_COMPAT_LCP_LEN + sizeof(struct iw_param))
#define IW_EV_COMPAT_ADDR_LEN	(IW_EV_COMPAT_LCP_LEN + sizeof(struct sockaddr))
#define IW_EV_COMPAT_QUAL_LEN	(IW_EV_COMPAT_LCP_LEN + sizeof(struct iw_quality))
#define IW_EV_COMPAT_POINT_LEN	\
	(IW_EV_COMPAT_LCP_LEN + sizeof(struct compat_iw_point) - \
	 IW_EV_COMPAT_POINT_OFF)
#endif
#endif	/* _LINUX_WIRELESS_H */
