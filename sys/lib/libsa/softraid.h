/*	$OpenBSD: softraid.h,v 1.2 2018/08/10 16:41:35 jsing Exp $	*/

/*
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
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

#ifndef _SOFTRAID_H_
#define _SOFTRAID_H_

/* Metadata from keydisks. */
struct sr_boot_keydisk {
	struct sr_uuid	kd_uuid;
	u_int8_t	kd_key[SR_CRYPTO_MAXKEYBYTES];
	SLIST_ENTRY(sr_boot_keydisk) kd_link;
};
SLIST_HEAD(sr_boot_keydisk_head, sr_boot_keydisk);

/* List of softraid volumes. */
extern struct sr_boot_volume_head sr_volumes;

/* List of softraid keydisks. */
extern struct sr_boot_keydisk_head sr_keydisks;

void	sr_clear_keys(void);
int	sr_crypto_unlock_volume(struct sr_boot_volume *);

#endif /* _SOFTRAID_H */
