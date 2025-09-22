/*	$OpenBSD: softraid_sparc64.h,v 1.5 2020/12/09 18:10:19 krw Exp $	*/

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

#ifndef _SOFTRAID_SPARC64_H_
#define _SOFTRAID_SPARC64_H_

void	srprobe(void);

struct sr_boot_chunk *sr_vol_boot_chunk(struct sr_boot_volume *);
const char *sr_getdisklabel(struct sr_boot_volume *, struct disklabel *);
int	sr_strategy(struct sr_boot_volume *, int, int, daddr_t, size_t,
	    void *, size_t *);

#endif /* _SOFTRAID_SPARC64_H */
