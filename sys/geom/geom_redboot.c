/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <sys/errno.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>

#include <sys/sbuf.h>
#include <geom/geom.h>
#include <geom/geom_slice.h>

#define REDBOOT_CLASS_NAME "REDBOOT"

struct fis_image_desc {
	uint8_t		name[16];	/* null-terminated name */
	uint32_t	offset;		/* offset in flash */
	uint32_t	addr;		/* address in memory */
	uint32_t	size;		/* image size in bytes */
	uint32_t	entry;		/* offset in image for entry point */
	uint32_t	dsize;		/* data size in bytes */
	uint8_t		pad[256-(16+7*sizeof(uint32_t)+sizeof(void*))];
	struct fis_image_desc *next;	/* linked list (in memory) */
	uint32_t	dsum;		/* descriptor checksum */
	uint32_t	fsum;		/* checksum over image data */
};

#define	FISDIR_NAME	"FIS directory"
#define	REDBCFG_NAME	"RedBoot config"
#define	REDBOOT_NAME	"RedBoot"

#define	REDBOOT_MAXSLICE	64
#define	REDBOOT_MAXOFF \
	(REDBOOT_MAXSLICE*sizeof(struct fis_image_desc))

struct g_redboot_softc {
	uint32_t	entry[REDBOOT_MAXSLICE];
	uint32_t	dsize[REDBOOT_MAXSLICE];
	uint8_t		readonly[REDBOOT_MAXSLICE];
	g_access_t	*parent_access;
};

static void
g_redboot_print(int i, struct fis_image_desc *fd)
{

	printf("[%2d] \"%-15.15s\" %08x:%08x", i, fd->name,
	    fd->offset, fd->size);
	printf(" addr %08x entry %08x\n", fd->addr, fd->entry);
	printf("     dsize 0x%x dsum 0x%x fsum 0x%x\n", fd->dsize,
	    fd->dsum, fd->fsum);
}

static int
g_redboot_ioctl(struct g_provider *pp, u_long cmd, void *data, int fflag, struct thread *td)
{
	return (ENOIOCTL);
}

static int
g_redboot_access(struct g_provider *pp, int dread, int dwrite, int dexcl)
{
	struct g_geom *gp = pp->geom;
	struct g_slicer *gsp = gp->softc;
	struct g_redboot_softc *sc = gsp->softc;

	if (dwrite > 0 && sc->readonly[pp->index])
		return (EPERM);
	return (sc->parent_access(pp, dread, dwrite, dexcl));
}

static int
g_redboot_start(struct bio *bp)
{
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_redboot_softc *sc;
	struct g_slicer *gsp;
	int idx;

	pp = bp->bio_to;
	idx = pp->index;
	gp = pp->geom;
	gsp = gp->softc;
	sc = gsp->softc;
	if (bp->bio_cmd == BIO_GETATTR) {
		if (g_handleattr_int(bp, REDBOOT_CLASS_NAME "::entry",
		    sc->entry[idx]))
			return (1);
		if (g_handleattr_int(bp, REDBOOT_CLASS_NAME "::dsize",
		    sc->dsize[idx]))
			return (1);
	}

	return (0);
}

static void
g_redboot_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
	struct g_consumer *cp __unused, struct g_provider *pp)
{
	struct g_redboot_softc *sc;
	struct g_slicer *gsp;

	gsp = gp->softc;
	sc = gsp->softc;
	g_slice_dumpconf(sb, indent, gp, cp, pp);
	if (pp != NULL) {
		if (indent == NULL) {
			sbuf_printf(sb, " entry %d", sc->entry[pp->index]);
			sbuf_printf(sb, " dsize %d", sc->dsize[pp->index]);
		} else {
			sbuf_printf(sb, "%s<entry>%d</entry>\n", indent,
			    sc->entry[pp->index]);
			sbuf_printf(sb, "%s<dsize>%d</dsize>\n", indent,
			    sc->dsize[pp->index]);
		}
	}
}

#include <sys/ctype.h>

static int
nameok(const char name[16])
{
	int i;

	/* descriptor names are null-terminated printable ascii */
	for (i = 0; i < 15; i++)
		if (!isprint(name[i]))
			break;
	return (name[i] == '\0');
}

static struct fis_image_desc *
parse_fis_directory(u_char *buf, size_t bufsize, off_t offset, uint32_t offmask)
{
#define	match(a,b)	(bcmp(a, b, sizeof(b)-1) == 0)
	struct fis_image_desc *fd, *efd;
	struct fis_image_desc *fisdir, *redbcfg;
	struct fis_image_desc *head, **tail;
	int i;

	fd = (struct fis_image_desc *)buf;
	efd = fd + (bufsize / sizeof(struct fis_image_desc));
#if 0
	/*
	 * Find the start of the FIS table.
	 */
	while (fd < efd && fd->name[0] != 0xff)
		fd++;
	if (fd == efd)
		return (NULL);
	if (bootverbose)
		printf("RedBoot FIS table starts at 0x%jx\n",
		    offset + fd - (struct fis_image_desc *) buf);
#endif
	/*
	 * Scan forward collecting entries in a list.
	 */
	fisdir = redbcfg = NULL;
	*(tail = &head) = NULL;
	for (i = 0; fd < efd; i++, fd++) {
		if (fd->name[0] == 0xff)
			continue;
		if (match(fd->name, FISDIR_NAME))
			fisdir = fd;
		else if (match(fd->name, REDBCFG_NAME))
			redbcfg = fd;
		if (nameok(fd->name)) {
			/*
			 * NB: flash address includes platform mapping;
			 *     strip it so we have only a flash offset.
			 */
			fd->offset &= offmask;
			if (bootverbose)
				g_redboot_print(i, fd);
			*tail = fd;
			*(tail = &fd->next) = NULL;
		}
	}
	if (fisdir == NULL) {
		if (bootverbose)
			printf("No RedBoot FIS table located at %lu\n",
			    (long) offset);
		return (NULL);
	}
	if (redbcfg != NULL &&
	    fisdir->offset + fisdir->size == redbcfg->offset) {
		/*
		 * Merged FIS/RedBoot config directory.
		 */
		if (bootverbose)
			printf("FIS/RedBoot merged at 0x%jx (not yet)\n",
			    offset + fisdir->offset);
		/* XXX */
	}
	return head;
#undef match
}

static struct g_geom *
g_redboot_taste(struct g_class *mp, struct g_provider *pp, int insist)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_redboot_softc *sc;
	int error, sectorsize, i;
	struct fis_image_desc *fd, *head;
	uint32_t offmask;
	off_t blksize;		/* NB: flash block size stored as stripesize */
	u_char *buf;
	off_t offset;
	const char *value;
	char *op;

	offset = 0;
	if (resource_string_value("redboot", 0, "fisoffset", &value) == 0) {
		offset = strtouq(value, &op, 0);
		if (*op != '\0') {
			offset = 0;
		}
	}

	g_trace(G_T_TOPOLOGY, "redboot_taste(%s,%s)", mp->name, pp->name);
	g_topology_assert();
	if (!strcmp(pp->geom->class->name, REDBOOT_CLASS_NAME))
		return (NULL);
	/* XXX only taste flash providers */
	if (strncmp(pp->name, "cfi", 3) && 
	    strncmp(pp->name, "flash/spi", 9))
		return (NULL);
	gp = g_slice_new(mp, REDBOOT_MAXSLICE, pp, &cp, &sc, sizeof(*sc),
	    g_redboot_start);
	if (gp == NULL)
		return (NULL);
	/* interpose our access method */
	sc->parent_access = gp->access;
	gp->access = g_redboot_access;

	sectorsize = cp->provider->sectorsize;
	blksize = cp->provider->stripesize;
	if (powerof2(cp->provider->mediasize))
		offmask = cp->provider->mediasize-1;
	else
		offmask = 0xffffffff;		/* XXX */
	if (bootverbose)
		printf("%s: mediasize %ld secsize %d blksize %ju offmask 0x%x\n",
		    __func__, (long) cp->provider->mediasize, sectorsize,
		    (uintmax_t)blksize, offmask);
	if (sectorsize < sizeof(struct fis_image_desc) ||
	    (sectorsize % sizeof(struct fis_image_desc)))
		return (NULL);
	g_topology_unlock();
	head = NULL;
	if(offset == 0)
		offset = cp->provider->mediasize - blksize;
again:
	buf = g_read_data(cp, offset, blksize, NULL);
	if (buf != NULL)
		head = parse_fis_directory(buf, blksize, offset, offmask);
	if (head == NULL && offset != 0) {
		if (buf != NULL)
			g_free(buf);
		offset = 0;			/* check the front */
		goto again;
	}
	g_topology_lock();
	if (head == NULL) {
		if (buf != NULL)
			g_free(buf);
		return NULL;
	}
	/*
	 * Craft a slice for each entry.
	 */
	for (fd = head, i = 0; fd != NULL; fd = fd->next) {
		if (fd->name[0] == '\0')
			continue;
		error = g_slice_config(gp, i, G_SLICE_CONFIG_SET,
		    fd->offset, fd->size, sectorsize, "redboot/%s", fd->name);
		if (error)
			printf("%s: g_slice_config returns %d for \"%s\"\n",
			    __func__, error, fd->name);
		sc->entry[i] = fd->entry;
		sc->dsize[i] = fd->dsize;
		/* disallow writing hard-to-recover entries */
		sc->readonly[i] = (strcmp(fd->name, FISDIR_NAME) == 0) ||
				  (strcmp(fd->name, REDBOOT_NAME) == 0);
		i++;
	}
	g_free(buf);
	g_access(cp, -1, 0, 0);
	if (LIST_EMPTY(&gp->provider)) {
		g_slice_spoiled(cp);
		return (NULL);
	}
	return (gp);
}

static void
g_redboot_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	struct g_geom *gp;

	g_topology_assert();
	gp = gctl_get_geom(req, mp, "geom");
	if (gp == NULL)
		return;
	gctl_error(req, "Unknown verb");
}

static struct g_class g_redboot_class	= {
	.name		= REDBOOT_CLASS_NAME,
	.version	= G_VERSION,
	.taste		= g_redboot_taste,
	.dumpconf	= g_redboot_dumpconf,
	.ctlreq		= g_redboot_config,
	.ioctl		= g_redboot_ioctl,
};
DECLARE_GEOM_CLASS(g_redboot_class, g_redboot);
MODULE_VERSION(geom_redboot, 0);
