/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2005, 2007 Lukas Ertl
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>

#define GV_LEGACY_I386	0
#define GV_LEGACY_AMD64 1
#define GV_LEGACY_SPARC64 2
#define GV_LEGACY_POWERPC 3

static int	gv_legacy_header_type(uint8_t *, int);

/*
 * Here are the "offset (size)" for the various struct gv_hdr fields,
 * for the legacy i386 (or 32-bit powerpc), legacy amd64 (or sparc64), and
 * current (cpu & endian agnostic) versions of the on-disk format of the vinum
 * header structure:
 *
 *       i386    amd64   current   field
 *     -------- -------- --------  -----
 *       0 ( 8)   0 ( 8)   0 ( 8)  magic
 *       8 ( 4)   8 ( 8)   8 ( 8)  config_length
 *      12 (32)  16 (32)  16 (32)  label.sysname
 *      44 (32)  48 (32)  48 (32)  label.name
 *      76 ( 4)  80 ( 8)  80 ( 8)  label.date_of_birth.tv_sec
 *      80 ( 4)  88 ( 8)  88 ( 8)  label.date_of_birth.tv_usec
 *      84 ( 4)  96 ( 8)  96 ( 8)  label.last_update.tv_sec
 *      88 ( 4) 104 ( 8) 104 ( 8)  label.last_update.tv_usec
 *      92 ( 8) 112 ( 8) 112 ( 8)  label.drive_size
 *     ======== ======== ========
 *     100      120      120       total size
 *
 * NOTE: i386 and amd64 formats are stored as little-endian; the current
 * format uses big-endian (network order).
 */


/* Checks for legacy format depending on platform. */
static int
gv_legacy_header_type(uint8_t *hdr, int bigendian)
{
	uint32_t *i32;
	int arch_32, arch_64, i;

	/* Set arch according to endianness. */
	if (bigendian) {
		arch_32 = GV_LEGACY_POWERPC;
		arch_64 = GV_LEGACY_SPARC64;
	} else {
		arch_32 = GV_LEGACY_I386;
		arch_64 = GV_LEGACY_AMD64;
	}

	/* if non-empty hostname overlaps 64-bit config_length */
	i32 = (uint32_t *)(hdr + 12);
	if (*i32 != 0)
		return (arch_32);
	/* check for non-empty hostname */
	if (hdr[16] != 0)
		return (arch_64);
	/* check bytes past 32-bit structure */
	for (i = 100; i < 120; i++)
		if (hdr[i] != 0)
			return (arch_32);
	/* check for overlapping timestamp */
	i32 = (uint32_t *)(hdr + 84);

	if (*i32 == 0)
		return (arch_64);
	return (arch_32);
}

/*
 * Read the header while taking magic number into account, and write it to
 * destination pointer.
 */
int
gv_read_header(struct g_consumer *cp, struct gv_hdr *m_hdr)
{
	struct g_provider *pp;
	uint64_t magic_machdep;
	uint8_t *d_hdr;
	int be, off;

#define GV_GET32(endian)					\
		endian##32toh(*((uint32_t *)&d_hdr[off]));	\
		off += 4
#define GV_GET64(endian)					\
		endian##64toh(*((uint64_t *)&d_hdr[off]));	\
		off += 8

	KASSERT(m_hdr != NULL, ("gv_read_header: null m_hdr"));
	KASSERT(cp != NULL, ("gv_read_header: null cp"));
	pp = cp->provider;
	KASSERT(pp != NULL, ("gv_read_header: null pp"));

	if ((GV_HDR_OFFSET % pp->sectorsize) != 0 ||
	    (GV_HDR_LEN % pp->sectorsize) != 0)
		return (ENODEV);

	d_hdr = g_read_data(cp, GV_HDR_OFFSET, pp->sectorsize, NULL);
	if (d_hdr == NULL)
		return (-1);
	off = 0;
	m_hdr->magic = GV_GET64(be);
	magic_machdep = *((uint64_t *)&d_hdr[0]);
	/*
	 * The big endian machines will have a reverse of GV_OLD_MAGIC, so we
	 * need to decide if we are running on a big endian machine as well as
	 * checking the magic against the reverse of GV_OLD_MAGIC.
	 */
	be = (m_hdr->magic == magic_machdep);
	if (m_hdr->magic == GV_MAGIC) {
		m_hdr->config_length = GV_GET64(be);
		off = 16;
		bcopy(d_hdr + off, m_hdr->label.sysname, GV_HOSTNAME_LEN);
		off += GV_HOSTNAME_LEN;
		bcopy(d_hdr + off, m_hdr->label.name, GV_MAXDRIVENAME);
		off += GV_MAXDRIVENAME;
		m_hdr->label.date_of_birth.tv_sec = GV_GET64(be);
		m_hdr->label.date_of_birth.tv_usec = GV_GET64(be);
		m_hdr->label.last_update.tv_sec = GV_GET64(be);
		m_hdr->label.last_update.tv_usec = GV_GET64(be);
		m_hdr->label.drive_size = GV_GET64(be);
	} else if (m_hdr->magic != GV_OLD_MAGIC &&
	    m_hdr->magic != le64toh(GV_OLD_MAGIC)) {
		/* Not a gvinum drive. */
		g_free(d_hdr);
		return (-1);
	} else if (gv_legacy_header_type(d_hdr, be) == GV_LEGACY_SPARC64) {
		G_VINUM_DEBUG(1, "detected legacy sparc64 header");
		m_hdr->magic = GV_MAGIC;
		/* Legacy sparc64 on-disk header */
		m_hdr->config_length = GV_GET64(be);
		bcopy(d_hdr + 16, m_hdr->label.sysname, GV_HOSTNAME_LEN);
		off += GV_HOSTNAME_LEN;
		bcopy(d_hdr + 48, m_hdr->label.name, GV_MAXDRIVENAME);
		off += GV_MAXDRIVENAME;
		m_hdr->label.date_of_birth.tv_sec = GV_GET64(be);
		m_hdr->label.date_of_birth.tv_usec = GV_GET64(be);
		m_hdr->label.last_update.tv_sec = GV_GET64(be);
		m_hdr->label.last_update.tv_usec = GV_GET64(be);
		m_hdr->label.drive_size = GV_GET64(be);
	} else if (gv_legacy_header_type(d_hdr, be) == GV_LEGACY_POWERPC) {
		G_VINUM_DEBUG(1, "detected legacy PowerPC header");
		m_hdr->magic = GV_MAGIC;
		/* legacy 32-bit big endian on-disk header */
		m_hdr->config_length = GV_GET32(be);
		bcopy(d_hdr + off, m_hdr->label.sysname, GV_HOSTNAME_LEN);
		off += GV_HOSTNAME_LEN;
		bcopy(d_hdr + off, m_hdr->label.name, GV_MAXDRIVENAME);
		off += GV_MAXDRIVENAME;
		m_hdr->label.date_of_birth.tv_sec = GV_GET32(be);
		m_hdr->label.date_of_birth.tv_usec = GV_GET32(be);
		m_hdr->label.last_update.tv_sec = GV_GET32(be);
		m_hdr->label.last_update.tv_usec = GV_GET32(be);
		m_hdr->label.drive_size = GV_GET64(be);
	} else if (gv_legacy_header_type(d_hdr, be) == GV_LEGACY_I386) {
		G_VINUM_DEBUG(1, "detected legacy i386 header");
		m_hdr->magic = GV_MAGIC;
		/* legacy i386 on-disk header */
		m_hdr->config_length = GV_GET32(le);
		bcopy(d_hdr + off, m_hdr->label.sysname, GV_HOSTNAME_LEN);
		off += GV_HOSTNAME_LEN;
		bcopy(d_hdr + off, m_hdr->label.name, GV_MAXDRIVENAME);
		off += GV_MAXDRIVENAME;
		m_hdr->label.date_of_birth.tv_sec = GV_GET32(le);
		m_hdr->label.date_of_birth.tv_usec = GV_GET32(le);
		m_hdr->label.last_update.tv_sec = GV_GET32(le);
		m_hdr->label.last_update.tv_usec = GV_GET32(le);
		m_hdr->label.drive_size = GV_GET64(le);
	} else {
		G_VINUM_DEBUG(1, "detected legacy amd64 header");
		m_hdr->magic = GV_MAGIC;
		/* legacy amd64 on-disk header */
		m_hdr->config_length = GV_GET64(le);
		bcopy(d_hdr + 16, m_hdr->label.sysname, GV_HOSTNAME_LEN);
		off += GV_HOSTNAME_LEN;
		bcopy(d_hdr + 48, m_hdr->label.name, GV_MAXDRIVENAME);
		off += GV_MAXDRIVENAME;
		m_hdr->label.date_of_birth.tv_sec = GV_GET64(le);
		m_hdr->label.date_of_birth.tv_usec = GV_GET64(le);
		m_hdr->label.last_update.tv_sec = GV_GET64(le);
		m_hdr->label.last_update.tv_usec = GV_GET64(le);
		m_hdr->label.drive_size = GV_GET64(le);
	}

	g_free(d_hdr);
	return (0);
}

/* Write out the gvinum header. */
int
gv_write_header(struct g_consumer *cp, struct gv_hdr *m_hdr)
{
	uint8_t d_hdr[GV_HDR_LEN];
	int off, ret;

#define GV_SET64BE(field)					\
	do {							\
		*((uint64_t *)&d_hdr[off]) = htobe64(field);	\
		off += 8;					\
	} while (0)

	KASSERT(m_hdr != NULL, ("gv_write_header: null m_hdr"));

	off = 0;
	memset(d_hdr, 0, GV_HDR_LEN);
	GV_SET64BE(m_hdr->magic);
	GV_SET64BE(m_hdr->config_length);
	off = 16;
	bcopy(m_hdr->label.sysname, d_hdr + off, GV_HOSTNAME_LEN);
	off += GV_HOSTNAME_LEN;
	bcopy(m_hdr->label.name, d_hdr + off, GV_MAXDRIVENAME);
	off += GV_MAXDRIVENAME;
	GV_SET64BE(m_hdr->label.date_of_birth.tv_sec);
	GV_SET64BE(m_hdr->label.date_of_birth.tv_usec);
	GV_SET64BE(m_hdr->label.last_update.tv_sec);
	GV_SET64BE(m_hdr->label.last_update.tv_usec);
	GV_SET64BE(m_hdr->label.drive_size);

	ret = g_write_data(cp, GV_HDR_OFFSET, d_hdr, GV_HDR_LEN);
	return (ret);
}

/* Save the vinum configuration back to each involved disk. */
void
gv_save_config(struct gv_softc *sc)
{
	struct g_consumer *cp;
	struct gv_drive *d;
	struct gv_hdr *vhdr, *hdr;
	struct sbuf *sb;
	struct timeval last_update;
	int error;

	KASSERT(sc != NULL, ("gv_save_config: null sc"));

	vhdr = g_malloc(GV_HDR_LEN, M_WAITOK | M_ZERO);
	vhdr->magic = GV_MAGIC;
	vhdr->config_length = GV_CFG_LEN;
	microtime(&last_update);

	sb = sbuf_new(NULL, NULL, GV_CFG_LEN, SBUF_FIXEDLEN);
	gv_format_config(sc, sb, 1, NULL);
	sbuf_finish(sb);

	LIST_FOREACH(d, &sc->drives, drive) {
		/*
		 * We can't save the config on a drive that isn't up, but
		 * drives that were just created aren't officially up yet, so
		 * we check a special flag.
		 */
		if (d->state != GV_DRIVE_UP)
			continue;

		cp = d->consumer;
		if (cp == NULL) {
			G_VINUM_DEBUG(0, "drive '%s' has no consumer!",
			    d->name);
			continue;
		}

		hdr = d->hdr;
		if (hdr == NULL) {
			G_VINUM_DEBUG(0, "drive '%s' has no header",
			    d->name);
			g_free(vhdr);
			continue;
		}
		bcopy(&last_update, &hdr->label.last_update,
		    sizeof(struct timeval));
		bcopy(&hdr->label, &vhdr->label, sizeof(struct gv_label));
		g_topology_lock();
		error = g_access(cp, 0, 1, 0);
		if (error) {
			G_VINUM_DEBUG(0, "g_access failed on "
			    "drive %s, errno %d", d->name, error);
			g_topology_unlock();
			continue;
		}
		g_topology_unlock();

		error = gv_write_header(cp, vhdr);
		if (error) {
			G_VINUM_DEBUG(0, "writing vhdr failed on drive %s, "
			    "errno %d", d->name, error);
			g_topology_lock();
			g_access(cp, 0, -1, 0);
			g_topology_unlock();
			continue;
		}
		/* First config copy. */
		error = g_write_data(cp, GV_CFG_OFFSET, sbuf_data(sb),
		    GV_CFG_LEN);
		if (error) {
			G_VINUM_DEBUG(0, "writing first config copy failed on "
			    "drive %s, errno %d", d->name, error);
			g_topology_lock();
			g_access(cp, 0, -1, 0);
			g_topology_unlock();
			continue;
		}
		/* Second config copy. */
		error = g_write_data(cp, GV_CFG_OFFSET + GV_CFG_LEN,
		    sbuf_data(sb), GV_CFG_LEN);
		if (error)
			G_VINUM_DEBUG(0, "writing second config copy failed on "
			    "drive %s, errno %d", d->name, error);

		g_topology_lock();
		g_access(cp, 0, -1, 0);
		g_topology_unlock();
	}

	sbuf_delete(sb);
	g_free(vhdr);
}
