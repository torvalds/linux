/*	$OpenBSD: iapp.h,v 1.4 2015/11/03 12:21:50 mpi Exp $	*/

/*
 * Copyright (c) 2005 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 */

#ifndef _IAPP_H
#define _IAPP_H

#define IEEE80211_IAPP_VERSION	0

/*
 * IAPP (Inter Access Point Protocol)
 */

struct ieee80211_iapp_frame {
	u_int8_t	i_version;
	u_int8_t	i_command;
	u_int16_t	i_identifier;
	u_int16_t	i_length;
} __packed;

enum ieee80211_iapp_frame_type {
	IEEE80211_IAPP_FRAME_ADD_NOTIFY			= 0,
	IEEE80211_IAPP_FRAME_MOVE_NOTIFY		= 1,
	IEEE80211_IAPP_FRAME_MOVE_RESPONSE		= 2,
	IEEE80211_IAPP_FRAME_SEND_SECURITY_BLOCK	= 3,
	IEEE80211_IAPP_FRAME_ACK_SECURITY_BLOCK		= 4,
	IEEE80211_IAPP_FRAME_CACHE_NOTIFY		= 5,
	IEEE80211_IAPP_FRAME_CACHE_RESPONSE		= 6,
	IEEE80211_IAPP_FRAME_HOSTAPD_RADIOTAP		= 12,
	IEEE80211_IAPP_FRAME_HOSTAPD_PCAP		= 13
};

#define IEEE80211_IAPP_FRAME_TYPE_NAME	{				\
	"add notify",							\
	"move notify",							\
	"move response",						\
	"send security block",						\
	"ack security block",						\
	"cache notify",							\
	"cache response",						\
	"reserved#07",							\
	"reserved#08",							\
	"reserved#09",							\
	"reserved#10",							\
	"reserved#11",							\
	"hostapd radiotap",						\
	"hostapd pcap",							\
	"reserved#14",							\
	"reserved#15",							\
}

struct ieee80211_iapp_add_notify {
	u_int8_t	a_length;
	u_int8_t	a_reserved;
	u_int8_t	a_macaddr[IEEE80211_ADDR_LEN];
	u_int16_t	a_seqnum;
} __packed;

#define IAPP_PORT	3517
#define IAPP_OLD_PORT	2313
#define IAPP_MCASTADDR	"224.0.1.178"
#define IAPP_MAXSIZE	512

#endif /* _IAPP_H */
