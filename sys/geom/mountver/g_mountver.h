/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Edward Tomasz Napierala <trasz@FreeBSD.org>
 * Copyright (c) 2004-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef	_G_MOUNTVER_H_
#define	_G_MOUNTVER_H_

#define	G_MOUNTVER_CLASS_NAME	"MOUNTVER"
#define	G_MOUNTVER_VERSION	4
#define	G_MOUNTVER_SUFFIX	".mountver"

#ifdef _KERNEL

#define	G_MOUNTVER_DEBUG(lvl, ...)	do {				\
	if (g_mountver_debug >= (lvl)) {				\
		printf("GEOM_MOUNTVER");				\
		if (g_mountver_debug > 0)				\
			printf("[%u]", lvl);				\
		printf(": ");						\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)
#define	G_MOUNTVER_LOGREQ(bp, ...)	do {				\
	if (g_mountver_debug >= 2) {					\
		printf("GEOM_MOUNTVER[2]: ");				\
		printf(__VA_ARGS__);					\
		printf(" ");						\
		g_print_bio(bp);					\
		printf("\n");						\
	}								\
} while (0)

struct g_mountver_softc {
	TAILQ_HEAD(, bio)		sc_queue;
	struct mtx			sc_mtx;
	char				*sc_provider_name;
	char				sc_ident[DISK_IDENT_SIZE];
	int				sc_orphaned;
	int				sc_shutting_down;
	int				sc_access_r;
	int				sc_access_w;
	int				sc_access_e;
};
#endif	/* _KERNEL */

#endif	/* _G_MOUNTVER_H_ */
