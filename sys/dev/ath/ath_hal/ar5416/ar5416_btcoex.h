/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2011 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef	__ATH_AR5416_BTCOEX_H__
#define	__ATH_AR5416_BTCOEX_H__
/*
 * Weight table configurations.
 */
#define AR5416_BT_WGHT                     0xff55
#define AR5416_STOMP_ALL_WLAN_WGHT         0xfcfc
#define AR5416_STOMP_LOW_WLAN_WGHT         0xa8a8
#define AR5416_STOMP_NONE_WLAN_WGHT        0x0000
#define AR5416_STOMP_ALL_FORCE_WLAN_WGHT   0xffff       // Stomp BT even when WLAN is idle
#define AR5416_STOMP_LOW_FORCE_WLAN_WGHT   0xaaaa       // Stomp BT even when WLAN is idle

#endif	/* __ATH_AR5416_BTCOEX_H__ */
