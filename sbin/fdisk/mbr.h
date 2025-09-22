/*	$OpenBSD: mbr.h,v 1.45 2025/05/23 00:20:02 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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

struct mbr {
	uint64_t	mbr_lba_firstembr;
	uint64_t	mbr_lba_self;
	unsigned char	mbr_code[DOSPARTOFF];
	struct prt	mbr_prt[NDOSPART];
	uint16_t	mbr_signature;
	unsigned int	mbr_dmbrzeros;
};

extern struct dos_mbr	default_dmbr;

void		MBR_print(const struct mbr *, const char *);
void		MBR_init(struct mbr *);
int		MBR_read(const uint64_t, const uint64_t, struct mbr *);
int		MBR_recover_partition(char *, struct mbr *);
int		MBR_write(const struct mbr *);
int		MBR_valid_prt(const struct mbr *);
