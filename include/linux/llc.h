/* SPDX-License-Identifier: GPL-2.0 */
/*
 * IEEE 802.2 User Interface SAPs for Linux, data structures and indicators.
 *
 * Copyright (c) 2001 by Jay Schulist <jschlst@samba.org>
 */
#ifndef __LINUX_LLC_H
#define __LINUX_LLC_H

#include <uapi/linux/llc.h>

#define LLC_SAP_DYN_START	0xC0
#define LLC_SAP_DYN_STOP	0xDE
#define LLC_SAP_DYN_TRIES	4

#define llc_ui_skb_cb(__skb) ((struct sockaddr_llc *)&((__skb)->cb[0]))
#endif /* __LINUX_LLC_H */
