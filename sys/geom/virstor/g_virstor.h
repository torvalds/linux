/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2007 Ivan Voras <ivoras@freebsd.org>
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

#ifndef _G_VIRSTOR_H_
#define _G_VIRSTOR_H_

#define	G_VIRSTOR_CLASS_NAME "VIRSTOR"


#define VIRSTOR_MAP_ALLOCATED 1
struct virstor_map_entry {
	uint16_t	flags;
	uint16_t	provider_no;
	uint32_t	provider_chunk;
};

#define	VIRSTOR_MAP_ENTRY_SIZE (sizeof(struct virstor_map_entry))
#define	VIRSTOR_MAP_BLOCK_ENTRIES (MAXPHYS / VIRSTOR_MAP_ENTRY_SIZE)
/* Struct size is guarded by CTASSERT in main source */

#ifdef _KERNEL

#define	LOG_MSG(lvl, ...)       do {					\
        if (g_virstor_debug >= (lvl)) {					\
                printf("GEOM_" G_VIRSTOR_CLASS_NAME);			\
                if ((lvl) > 0)						\
                        printf("[%u]", (lvl));				\
                printf(": ");						\
                printf(__VA_ARGS__);					\
                printf("\n");						\
        }								\
} while (0)
#define	LOG_MESSAGE LOG_MSG

#define	LOG_REQ(lvl, bp, ...)  do {					\
        if (g_virstor_debug >= (lvl)) {					\
                printf("GEOM_" G_VIRSTOR_CLASS_NAME);			\
                if ((lvl) > 0)						\
                        printf("[%u]", (lvl));				\
                printf(": ");						\
                printf(__VA_ARGS__);					\
                printf(" ");						\
                g_print_bio(bp);					\
                printf("\n");						\
        }								\
} while (0)
#define	LOG_REQUEST LOG_REQ

/* "critical" system announcements (e.g. "geom is up") */
#define	LVL_ANNOUNCE	0
/* errors */
#define	LVL_ERROR	1
/* warnings */
#define	LVL_WARNING	2
/* info, noncritical for system operation (user doesn't have to see it */
#define	LVL_INFO	5
/* debug info */
#define	LVL_DEBUG	10
/* more debug info */
#define	LVL_DEBUG2	12
/* superfluous debug info (large volumes of data) */
#define	LVL_MOREDEBUG	15


/* Component data */
struct g_virstor_component {
	struct g_consumer	*gcons;
	struct g_virstor_softc	*sc;
	unsigned int		 index;		/* Component index in array */
	unsigned int		 chunk_count;
	unsigned int		 chunk_next;
	unsigned int		 chunk_reserved;
	unsigned int		 flags;
};


/* Internal geom instance data */
struct g_virstor_softc {
	struct g_geom		*geom;
	struct g_provider	*provider;
	struct g_virstor_component *components;
	u_int			 n_components;
	u_int			 curr_component; /* Component currently used */
	uint32_t		 id;		/* Unique ID of this geom */
	off_t			 virsize;	/* Total size of virstor */
	off_t			 sectorsize;
	size_t			 chunk_size;
	size_t			 chunk_count;	/* governs map_size */
	struct virstor_map_entry *map;
	size_t			 map_size;	/* (in bytes) */
	size_t			 map_sectors;	/* Size of map in sectors */
	size_t			 me_per_sector;	/* # map entries in a sector */
	STAILQ_HEAD(, g_virstor_bio_q)	 delayed_bio_q;	/* Queue of delayed BIOs */
	struct mtx		 delayed_bio_q_mtx;
};

/* "delayed BIOs" Queue element */
struct g_virstor_bio_q {
	struct bio		*bio;
	STAILQ_ENTRY(g_virstor_bio_q) linkage;
};


#endif	/* _KERNEL */

#ifndef _PATH_DEV
#define _PATH_DEV "/dev/"
#endif

#endif	/* !_G_VIRSTOR_H_ */
