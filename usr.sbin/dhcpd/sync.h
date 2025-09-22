/*	$OpenBSD: sync.h,v 1.6 2025/06/04 21:16:25 dlg Exp $	*/

/*
 * Copyright (c) 2008, Bob Beck <beck@openbsd.org>
 * Copyright (c) 2006, 2007 Reyk Floeter <reyk@openbsd.org>
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

#ifndef _DHCPD_SYNC
#define _DHCPD_SYNC

/*
 * dhcpd(8) synchronisation protocol.
 *
 * This protocol has been designed for realtime synchronisation between
 * multiple machines running dhcpd(8), running the same config.
 * It is a simple Type-Length-Value based protocol, it allows easy
 * extension with future subtypes and bulk transfers by sending multiple
 * entries at once. The unencrypted messages will be authenticated using
 * HMAC-SHA1.
 *
 */

#define DHCP_SYNC_VERSION	1
#define DHCP_SYNC_MCASTADDR	"224.0.1.240"	/* XXX choose valid address */
#define DHCP_SYNC_MCASTTTL	IP_DEFAULT_MULTICAST_TTL
#define DHCP_SYNC_HMAC_LEN	20	/* SHA1 */
#define DHCP_SYNC_MAXSIZE	1408
#define DHCP_SYNC_KEY		"/var/db/dhcpd.key"

#define DHCP_ALIGNBYTES		(15)
#define DHCP_ALIGN(p)		(((u_int)(p) + DHCP_ALIGNBYTES) &~ DHCP_ALIGNBYTES)

struct dhcp_synchdr {
	u_int8_t	sh_version;
	u_int8_t	sh_af;
	u_int16_t	sh_length;
	u_int32_t	sh_counter;
	u_int8_t	sh_hmac[DHCP_SYNC_HMAC_LEN];
	u_int8_t	sh_pad[4];
} __packed;

struct dhcp_synctlv_hdr {
	u_int16_t	st_type;
	u_int16_t	st_length;
} __packed;

struct dhcp_synctlv_lease {
	u_int16_t	lv_type;
	u_int16_t	lv_length;
	u_int32_t	lv_starts, lv_ends, lv_timestamp;
	struct iaddr	lv_ip_addr;
	struct hardware	lv_hardware_addr;
} __packed;

#define DHCP_SYNC_END		0x0000
#define DHCP_SYNC_LEASE		0x0001

extern int	 sync_init(const char *, const char *, u_short);
extern int	 sync_addhost(const char *, u_short);
extern void	 sync_lease(struct lease *);
#endif /* _DHCPD_SYNC */
