/*-
 * Copyright (c) 2002 by Thomas Moestl <tmm@FreeBSD.org>.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef OFW_UTIL_H
#define	OFW_UTIL_H

#include <dev/ofw/openfirm.h>

int		ofw_open(int);
void		ofw_close(int);

phandle_t	ofw_root(int);
phandle_t	ofw_optnode(int);
phandle_t	ofw_peer(int, phandle_t);
phandle_t	ofw_child(int, phandle_t);
phandle_t	ofw_finddevice(int, const char *);

int		ofw_firstprop(int, phandle_t, char *, int);
int		ofw_nextprop(int, phandle_t, const char *, char *, int);
int		ofw_getprop(int, phandle_t, const char *, void *, int);
int		ofw_setprop(int, phandle_t, const char *, const void *, int);
int		ofw_getproplen(int, phandle_t, const char *);
int		ofw_getprop_alloc(int, phandle_t, const char *, void **, int *,
    int);

#endif /* OFW_UTIL_H */
