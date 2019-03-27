/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _GEOM_VINUM_SHARE_H_
#define	_GEOM_VINUM_SHARE_H_

/* Maximum number of arguments for a single command. */
#define	GV_MAXARGS	64

enum {
    KILOBYTE = 1024,
    MEGABYTE = 1048576,
    GIGABYTE = 1073741824
};

off_t	gv_sizespec(char *);
int	gv_tokenize(char *, char **, int);

struct gv_sd	 *gv_alloc_sd(void);
struct gv_volume *gv_alloc_volume(void);
struct gv_plex	 *gv_alloc_plex(void);
struct gv_drive	 *gv_alloc_drive(void);
struct gv_drive	 *gv_new_drive(int, char **);
struct gv_plex	 *gv_new_plex(int, char **);
struct gv_sd	 *gv_new_sd(int, char **);
struct gv_volume *gv_new_volume(int, char **);

int	gv_drivestatei(char *);
int	gv_plexorgi(char *);
int	gv_plexstatei(char *);
int	gv_sdstatei(char *);
int	gv_volstatei(char *);

const char	*gv_drivestate(int);
const char	*gv_plexorg(int);
const char	*gv_plexorg_short(int);
const char	*gv_plexstate(int);
const char	*gv_sdstate(int);
const char	*gv_volstate(int);
const char	*gv_roughlength(off_t, int);

#endif /* _GEOM_VINUM_SHARE_H_ */
