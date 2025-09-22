/*	$OpenBSD: efidev.h,v 1.4 2023/04/18 23:11:56 dlg Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/* efidev.c */
void		 efid_init(struct diskinfo *, void *handle);
int		 efiopen(struct open_file *, ...);
int		 efistrategy(void *, int, daddr_t, size_t, void *, size_t *);
int		 eficlose(struct open_file *);
int		 efiioctl(struct open_file *, u_long, void *);

int		 esp_open(char *, struct open_file *);
int		 esp_close(struct open_file *);
int		 esp_read(struct open_file *, void *, size_t, size_t *);
int		 esp_write(struct open_file *, void *, size_t, size_t *);
off_t		 esp_seek(struct open_file *, off_t, int);
int		 esp_stat(struct open_file *, struct stat *);
int		 esp_readdir(struct open_file *, char *);

int		 espopen(struct open_file *, ...);
int		 espclose(struct open_file *);
int		 espioctl(struct open_file *, u_long, void *);
int		 espstrategy(void *, int, daddr_t, size_t, void *, size_t *);
