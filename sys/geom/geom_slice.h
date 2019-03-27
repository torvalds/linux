/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

#ifndef _GEOM_GEOM_SLICE_H_
#define _GEOM_GEOM_SLICE_H_

struct g_slice {
	off_t	offset;
	off_t	length;
	u_int	sectorsize;
	struct	g_provider *provider;
};

struct g_slice_hot {
	off_t	offset;
	off_t	length;
	int	ract;
	int	dact;
	int	wact;
};

typedef int g_slice_start_t (struct bio *bp);

struct g_slicer {
	u_int			nslice;
	u_int			nprovider;
	struct g_slice		*slices;

	u_int			nhotspot;
	struct g_slice_hot	*hotspot;

	void			*softc;
	g_slice_start_t		*start;
	g_event_t		*hot;
};

g_dumpconf_t g_slice_dumpconf;
int g_slice_config(struct g_geom *gp, u_int idx, int how, off_t offset, off_t length, u_int sectorsize, const char *fmt, ...) __printflike(7, 8);
void g_slice_spoiled(struct g_consumer *cp);
void g_slice_orphan(struct g_consumer *cp);
#define G_SLICE_CONFIG_CHECK	0
#define G_SLICE_CONFIG_SET	1
#define G_SLICE_CONFIG_FORCE	2
struct g_geom * g_slice_new(struct g_class *mp, u_int slices, struct g_provider *pp, struct g_consumer **cpp, void *extrap, int extra, g_slice_start_t *start);

int g_slice_conf_hot(struct g_geom *gp, u_int idx, off_t offset, off_t length, int ract, int dact, int wact);
#define G_SLICE_HOT_ALLOW	1
#define G_SLICE_HOT_DENY	2
#define G_SLICE_HOT_START	4
#define G_SLICE_HOT_CALL	8

int g_slice_destroy_geom(struct gctl_req *req, struct g_class *mp, struct g_geom *gp);

void g_slice_finish_hot(struct bio *bp);

#endif /* _GEOM_GEOM_SLICE_H_ */
