/*	$OpenBSD: installboot.h,v 1.17 2025/02/19 21:30:46 kettenis Exp $	*/
/*
 * Copyright (c) 2012, 2013 Joel Sing <jsing@openbsd.org>
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

#include <dev/biovar.h>

#include <stdlib.h>

extern int config;
extern int nowrite;
extern int stages;
extern int verbose;

extern char *root;
extern char *stage1;
extern char *stage2;

#ifdef BOOTSTRAP
void	bootstrap(int, char *, char *);
#endif

int	filecopy(const char *, const char *);
char	*fileprefix(const char *, const char *);
int	fileprintf(const char *, const char *, ...)
	    __attribute__((format(printf, 2, 3)));
u_int32_t crc32(const u_char *, const u_int32_t);

void	md_init(void);
void	md_loadboot(void);
void	md_prepareboot(int, char *);
void	md_installboot(int, char *);

#ifdef SOFTRAID
int	sr_open_chunk(int, int, int, struct bioc_disk *, char **, char *);
void	sr_prepareboot(int, char *);
void	sr_installboot(int, char *);
void	sr_install_bootblk(int, int, int);
void	sr_install_bootldr(int, char *);
void	sr_status(struct bio_status *);
#endif

#ifdef EFIBOOTMGR
struct gpt_partition;
void	efi_bootmgr_setup(int, struct gpt_partition *, const char *);
#endif
