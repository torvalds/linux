/*
 * IEEE 802.2 User Interface SAPs for Linux, data structures and indicators.
 *
 * Copyright (c) 2001 by Jay Schulist <jschlst@samba.org>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#ifndef __LINUX_LLC_H
#define __LINUX_LLC_H

#include <uapi/linux/llc.h>

#define LLC_SAP_DYN_START	0xC0
#define LLC_SAP_DYN_STOP	0xDE
#define LLC_SAP_DYN_TRIES	4

#define llc_ui_skb_cb(__skb) ((struct sockaddr_llc *)&((__skb)->cb[0]))
#endif /* __LINUX_LLC_H */
