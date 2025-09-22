/*	$OpenBSD: libsa.h,v 1.10 2023/02/23 19:48:22 miod Exp $	*/

/*
 * Copyright (c) 2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lib/libsa/stand.h>

#define	PCLOCK	33333333

int readsects(int dev, uint32_t lba, void *buf, size_t size);
int blkdevopen(struct open_file *, ...);
int blkdevclose(struct open_file *);
int blkdevstrategy(void *, int, daddr_t, size_t, void *, size_t *);
int  getc(void);
void putc(int);
void cache_flush(void);
void cache_disable(void);

void	scif_cnprobe(struct consdev *);
void	scif_cninit(struct consdev *);
int	scif_cngetc(dev_t);
void	scif_cnputc(dev_t, int);
void	scif_init(unsigned int);

int	tick_init();
void	delay(int);

void	devboot(dev_t, char *);
void	machdep();
void	run_loadfile(uint64_t *, int);
