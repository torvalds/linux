/*	$OpenBSD: gpt.h,v 1.23 2025/06/26 13:33:44 krw Exp $	*/
/*
 * Copyright (c) 2015 Markus Muller <mmu@grummel.net>
 * Copyright (c) 2015 Kenneth R Westerback <krw@openbsd.org>
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

int		GPT_read(const int);
int		GPT_recover_partition(const char *, const char *, const char *);
int		GPT_get_lba_start(const unsigned int);
int		GPT_get_lba_end(const unsigned int);
int		GPT_get_name(const unsigned int);

int		GPT_init(const int);
int		GPT_write(void);
void		GPT_zap_headers(void);
void		GPT_print(const char *);
void		GPT_print_part(const unsigned int, const char *);
void		GPT_print_parthdr(void);

extern struct mbr		gmbr;
extern struct gpt_header	gh;
extern struct gpt_partition	gp[NGPTPARTITIONS];

#define	ANYGPT		0
#define	PRIMARYGPT	1
#define	SECONDARYGPT	2

#define	TERSE		0
#define	VERBOSE		1

#define	GHANDGP		0
#define	GPONLY		1
