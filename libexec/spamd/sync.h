/*	$OpenBSD: sync.h,v 1.3 2008/05/22 19:54:11 deraadt Exp $	*/

/*
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

#ifndef _SPAMD_SYNC
#define _SPAMD_SYNC

/*
 * spamd(8) synchronisation protocol.
 *
 * This protocol has been designed for realtime synchronisation between
 * multiple machines running spamd(8), ie. in front of a MX and a backup MX.
 * It is a simple Type-Length-Value based protocol, it allows easy
 * extension with future subtypes and bulk transfers by sending multiple
 * entries at once. The unencrypted messages will be authenticated using
 * HMAC-SHA1.
 *
 * the spamd(8) synchronisation protocol is not intended to be used as
 * a public SPAM sender database or distribution between vendors.
 */

#define SPAM_SYNC_VERSION	2
#define SPAM_SYNC_MCASTADDR	"224.0.1.240"	/* XXX choose valid address */
#define SPAM_SYNC_MCASTTTL	IP_DEFAULT_MULTICAST_TTL
#define SPAM_SYNC_HMAC_LEN	20	/* SHA1 */
#define SPAM_SYNC_MAXSIZE	1408
#define SPAM_SYNC_KEY		"/etc/mail/spamd.key"

#define SPAM_ALIGNBYTES      (15)
#define SPAM_ALIGN(p)        (((u_int)(p) + SPAM_ALIGNBYTES) &~ SPAM_ALIGNBYTES)

struct spam_synchdr {
	u_int8_t	sh_version;
	u_int8_t	sh_af;
	u_int16_t	sh_length;
	u_int32_t	sh_counter;
	u_int8_t	sh_hmac[SPAM_SYNC_HMAC_LEN];
	u_int8_t	sh_pad[4];
} __packed;

struct spam_synctlv_hdr {
	u_int16_t	st_type;
	u_int16_t	st_length;
} __packed;

struct spam_synctlv_grey {
	u_int16_t	sg_type;
	u_int16_t	sg_length;
	u_int32_t	sg_timestamp;
	u_int32_t	sg_ip;
	u_int16_t	sg_from_length;
	u_int16_t	sg_to_length;
	u_int16_t	sg_helo_length;
	/* strings go here, then packet code re-aligns packet */
} __packed;

struct spam_synctlv_addr {
	u_int16_t	sd_type;
	u_int16_t	sd_length;
	u_int32_t	sd_timestamp;
	u_int32_t	sd_expire;
	u_int32_t	sd_ip;
} __packed;

#define SPAM_SYNC_END		0x0000
#define SPAM_SYNC_GREY		0x0001
#define SPAM_SYNC_WHITE		0x0002
#define SPAM_SYNC_TRAPPED	0x0003

extern int	 sync_init(const char *, const char *, u_short);
extern int	 sync_addhost(const char *, u_short);
extern void	 sync_recv(void);
extern void	 sync_update(time_t, char *, char *, char *, char *);
extern void	 sync_white(time_t, time_t, char *);
extern void	 sync_trapped(time_t, time_t, char *);

#endif /* _SPAMD_SYNC */
