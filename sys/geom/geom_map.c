/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Aleksandr Rybalko <ray@dlink.ua>
 *   based on geom_redboot.c
 * Copyright (c) 2009 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>

#include <geom/geom.h>
#include <geom/geom_slice.h>

#define	MAP_CLASS_NAME	"MAP"
#define	MAP_MAXSLICE	64
#define	MAP_MAX_MARKER_LEN	64

struct g_map_softc {
	off_t		 offset[MAP_MAXSLICE];	/* offset in flash */
	off_t		 size[MAP_MAXSLICE];	/* image size in bytes */
	off_t		 entry[MAP_MAXSLICE];
	off_t		 dsize[MAP_MAXSLICE];
	uint8_t		 readonly[MAP_MAXSLICE];
	g_access_t	*parent_access;
};

static int
g_map_access(struct g_provider *pp, int dread, int dwrite, int dexcl)
{
	struct g_geom *gp;
	struct g_slicer *gsp;
	struct g_map_softc *sc;

	gp = pp->geom;
	gsp = gp->softc;
	sc = gsp->softc;

	if (dwrite > 0 && sc->readonly[pp->index])
		return (EPERM);

	return (sc->parent_access(pp, dread, dwrite, dexcl)); 
}

static int
g_map_start(struct bio *bp)
{
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_map_softc *sc;
	struct g_slicer *gsp;
	int idx;

	pp = bp->bio_to;
	idx = pp->index;
	gp = pp->geom;
	gsp = gp->softc;
	sc = gsp->softc;

	if (bp->bio_cmd == BIO_GETATTR) {
		if (g_handleattr_int(bp, MAP_CLASS_NAME "::entry",
		    sc->entry[idx])) {
			return (1);
		}
		if (g_handleattr_int(bp, MAP_CLASS_NAME "::dsize",
		    sc->dsize[idx])) {
			return (1);
		}
	}

	return (0);
}

static void
g_map_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp __unused, struct g_provider *pp)
{
	struct g_map_softc *sc;
	struct g_slicer *gsp;

	gsp = gp->softc;
	sc = gsp->softc;
	g_slice_dumpconf(sb, indent, gp, cp, pp);
	if (pp != NULL) {
		if (indent == NULL) {
			sbuf_printf(sb, " entry %jd", (intmax_t)sc->entry[pp->index]);
			sbuf_printf(sb, " dsize %jd", (intmax_t)sc->dsize[pp->index]);
		} else {
			sbuf_printf(sb, "%s<entry>%jd</entry>\n", indent,
			    (intmax_t)sc->entry[pp->index]);
			sbuf_printf(sb, "%s<dsize>%jd</dsize>\n", indent,
			    (intmax_t)sc->dsize[pp->index]);
		}
	}
}

static int
find_marker(struct g_consumer *cp, const char *line, off_t *offset)
{
	off_t search_start, search_offset, search_step;
	size_t sectorsize;
	uint8_t *buf;
	char *op, key[MAP_MAX_MARKER_LEN], search_key[MAP_MAX_MARKER_LEN];
	int ret, c;

	/* Try convert to numeric first */
	*offset = strtouq(line, &op, 0);
	if (*op == '\0') 
		return (0);

	bzero(search_key, MAP_MAX_MARKER_LEN);
	sectorsize = cp->provider->sectorsize;

#ifdef __LP64__
	ret = sscanf(line, "search:%li:%li:%63c",
	    &search_start, &search_step, search_key);
#else
	ret = sscanf(line, "search:%qi:%qi:%63c",
	    &search_start, &search_step, search_key);
#endif
	if (ret < 3)
		return (1);

	if (bootverbose) {
		printf("MAP: search %s for key \"%s\" from 0x%jx, step 0x%jx\n",
		    cp->geom->name, search_key, (intmax_t)search_start, (intmax_t)search_step);
	}

	/* error if search_key is empty */
	if (strlen(search_key) < 1)
		return (1);

	/* sscanf successful, and we start marker search */
	for (search_offset = search_start;
	     search_offset < cp->provider->mediasize;
	     search_offset += search_step) {

		g_topology_unlock();
		buf = g_read_data(cp, rounddown(search_offset, sectorsize),
		    roundup(strlen(search_key), sectorsize), NULL);
		g_topology_lock();

		/*
		 * Don't bother doing the rest if buf==NULL; eg derefencing
		 * to assemble 'key'.
		 */
		if (buf == NULL)
			continue;

		/* Wildcard, replace '.' with byte from data */
		/* TODO: add support wildcard escape '\.' */

		strncpy(key, search_key, MAP_MAX_MARKER_LEN);

		for (c = 0; c < MAP_MAX_MARKER_LEN && key[c]; c++) {
			if (key[c] == '.') {
				key[c] = ((char *)(buf + 
				    (search_offset % sectorsize)))[c];
			}
		}

		/* Assume buf != NULL here */
		if (memcmp(buf + search_offset % sectorsize,
		    key, strlen(search_key)) == 0) {
			g_free(buf);
			/* Marker found, so return their offset */
			*offset = search_offset;
			return (0);
		}
		g_free(buf);
	}

	/* Marker not found */
	return (1);
}

static int
g_map_parse_part(struct g_class *mp, struct g_provider *pp,
    struct g_consumer *cp, struct g_geom *gp, struct g_map_softc *sc, int i)
{
	const char *value, *name;
	char *op;
	off_t start, end, offset, size, dsize;
	int readonly, ret;

	/* hint.map.0.at="cfid0" - bind to cfid0 media */
	if (resource_string_value("map", i, "at", &value) != 0)
		return (1);

	/* Check if this correct provider */
	if (strcmp(pp->name, value) != 0)
		return (1);

	/*
	 * hint.map.0.name="uboot" - name of partition, will be available
	 * as "/dev/map/uboot"
	 */
	if (resource_string_value("map", i, "name", &name) != 0) {
		if (bootverbose)
			printf("MAP: hint.map.%d has no name\n", i);
		return (1);
	}

	/*
	 * hint.map.0.start="0x00010000" - partition start at 0x00010000
	 * or hint.map.0.start="search:0x00010000:0x200:marker text" -
	 * search for text "marker text", begin at 0x10000, step 0x200
	 * until we found marker or end of media reached
	 */ 
	if (resource_string_value("map", i, "start", &value) != 0) {
		if (bootverbose)
			printf("MAP: \"%s\" has no start value\n", name);
		return (1);
	}
	if (find_marker(cp, value, &start) != 0) {
		if (bootverbose) {
			printf("MAP: \"%s\" can't parse/use start value\n",
			    name);
		}
		return (1);
	}

	/* like "start" */
	if (resource_string_value("map", i, "end", &value) != 0) {
		if (bootverbose)
			printf("MAP: \"%s\" has no end value\n", name);
		return (1);
	}
	if (find_marker(cp, value, &end) != 0) {
		if (bootverbose) {
			printf("MAP: \"%s\" can't parse/use end value\n",
			    name);
		}
		return (1);
	}

	/* variable readonly optional, disable write access */
	if (resource_int_value("map", i, "readonly", &readonly) != 0)
		readonly = 0;

	/* offset of partition data, from partition begin */
	if (resource_string_value("map", i, "offset", &value) == 0) {
		offset = strtouq(value, &op, 0);
		if (*op != '\0') {
			if (bootverbose) {
				printf("MAP: \"%s\" can't parse offset\n",
				    name);
			}
			return (1);
		}
	} else {
		offset = 0;
	}

	/* partition data size */
	if (resource_string_value("map", i, "dsize", &value) == 0) {
		dsize = strtouq(value, &op, 0);
		if (*op != '\0') {
			if (bootverbose) {
				printf("MAP: \"%s\" can't parse dsize\n", 
				    name);
			}
			return (1);
		}
	} else {
		dsize = 0;
	}

	size = end - start;
	if (dsize == 0)
		dsize = size - offset;

	/* end is 0 or size is 0, No MAP - so next */
	if (end < start) {
		if (bootverbose) {
			printf("MAP: \"%s\", \"end\" less than "
			    "\"start\"\n", name);
		}
		return (1);
	}

	if (offset + dsize > size) {
		if (bootverbose) {
			printf("MAP: \"%s\", \"dsize\" bigger than "
			    "partition - offset\n", name);
		}
		return (1);
	}

	ret = g_slice_config(gp, i, G_SLICE_CONFIG_SET, start + offset,
	    dsize, cp->provider->sectorsize, "map/%s", name);
	if (ret != 0) {
		if (bootverbose) {
			printf("MAP: g_slice_config returns %d for \"%s\"\n", 
			    ret, name);
		}
		return (1);
	}

	if (bootverbose) {
		printf("MAP: %s: %jxx%jx, data=%jxx%jx "
		    "\"/dev/map/%s\"\n",
		    cp->geom->name, (intmax_t)start, (intmax_t)size, (intmax_t)offset,
		    (intmax_t)dsize, name);
	}

	sc->offset[i] = start;
	sc->size[i] = size;
	sc->entry[i] = offset;
	sc->dsize[i] = dsize;
	sc->readonly[i] = readonly ? 1 : 0;

	return (0);
}

static struct g_geom *
g_map_taste(struct g_class *mp, struct g_provider *pp, int insist __unused)
{
	struct g_map_softc *sc;
	struct g_consumer *cp;
	struct g_geom *gp;
	int i;

	g_trace(G_T_TOPOLOGY, "map_taste(%s,%s)", mp->name, pp->name);
	g_topology_assert();
	if (strcmp(pp->geom->class->name, MAP_CLASS_NAME) == 0)
		return (NULL);

	gp = g_slice_new(mp, MAP_MAXSLICE, pp, &cp, &sc, sizeof(*sc),
	    g_map_start);
	if (gp == NULL)
		return (NULL);

	/* interpose our access method */
	sc->parent_access = gp->access;
	gp->access = g_map_access;

	for (i = 0; i < MAP_MAXSLICE; i++)
		g_map_parse_part(mp, pp, cp, gp, sc, i);


	g_access(cp, -1, 0, 0);
	if (LIST_EMPTY(&gp->provider)) {
		if (bootverbose)
			printf("MAP: No valid partition found at %s\n", pp->name);
		g_slice_spoiled(cp);
		return (NULL);
	}
	return (gp);
}

static void
g_map_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	struct g_geom *gp;

	g_topology_assert();
	gp = gctl_get_geom(req, mp, "geom");
	if (gp == NULL)
		return;
	gctl_error(req, "Unknown verb");
}

static struct g_class g_map_class = {
	.name = MAP_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_map_taste,
	.dumpconf = g_map_dumpconf,
	.ctlreq = g_map_config,
};
DECLARE_GEOM_CLASS(g_map_class, g_map);
MODULE_VERSION(geom_map, 0);
