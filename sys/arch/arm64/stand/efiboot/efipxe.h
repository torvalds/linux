/*	$OpenBSD: efipxe.h,v 1.3 2020/12/09 18:10:18 krw Exp $	*/
/*
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
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

extern struct netif_driver efinet_driver;

int	 efitftp_open(char *path, struct open_file *f);

int	 mtftp_open(char *, struct open_file *);
int	 mtftp_close(struct open_file *);
int	 mtftp_read(struct open_file *, void *, size_t, size_t *);
int	 mtftp_write(struct open_file *, void *, size_t, size_t *);
off_t	 mtftp_seek(struct open_file *, off_t, int);
int	 mtftp_stat(struct open_file *, struct stat *);
int	 mtftp_readdir(struct open_file *, char *);

int	 tftpopen(struct open_file *, ...);
int	 tftpclose(struct open_file *);
int	 tftpioctl(struct open_file *, u_long, void *);
int	 tftpstrategy(void *, int, daddr_t, size_t, void *, size_t *);
