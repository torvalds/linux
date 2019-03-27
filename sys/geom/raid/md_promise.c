/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2000 - 2008 Søren Schmidt <sos@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <geom/geom.h>
#include "geom/raid/g_raid.h"
#include "g_raid_md_if.h"

static MALLOC_DEFINE(M_MD_PROMISE, "md_promise_data", "GEOM_RAID Promise metadata");

#define	PROMISE_MAX_DISKS	8
#define	PROMISE_MAX_SUBDISKS	2
#define	PROMISE_META_OFFSET	14

struct promise_raid_disk {
	uint8_t		flags;			/* Subdisk status. */
#define PROMISE_F_VALID		0x01
#define PROMISE_F_ONLINE	0x02
#define PROMISE_F_ASSIGNED	0x04
#define PROMISE_F_SPARE		0x08
#define PROMISE_F_DUPLICATE	0x10
#define PROMISE_F_REDIR		0x20
#define PROMISE_F_DOWN		0x40
#define PROMISE_F_READY		0x80

	uint8_t		number;			/* Position in a volume. */
	uint8_t		channel;		/* ATA channel number. */
	uint8_t		device;			/* ATA device number. */
	uint64_t	id __packed;		/* Subdisk ID. */
} __packed;

struct promise_raid_conf {
	char		promise_id[24];
#define PROMISE_MAGIC		"Promise Technology, Inc."
#define FREEBSD_MAGIC		"FreeBSD ATA driver RAID "

	uint32_t	dummy_0;
	uint64_t	magic_0;
#define PROMISE_MAGIC0(x)	(((uint64_t)(x.channel) << 48) | \
				((uint64_t)(x.device != 0) << 56))
	uint16_t	magic_1;
	uint32_t	magic_2;
	uint8_t		filler1[470];

	uint32_t	integrity;
#define PROMISE_I_VALID		0x00000080

	struct promise_raid_disk	disk;	/* This subdisk info. */
	uint32_t	disk_offset;		/* Subdisk offset. */
	uint32_t	disk_sectors;		/* Subdisk size */
	uint32_t	disk_rebuild;		/* Rebuild position. */
	uint16_t	generation;		/* Generation number. */
	uint8_t		status;			/* Volume status. */
#define PROMISE_S_VALID		0x01
#define PROMISE_S_ONLINE	0x02
#define PROMISE_S_INITED	0x04
#define PROMISE_S_READY		0x08
#define PROMISE_S_DEGRADED	0x10
#define PROMISE_S_MARKED	0x20
#define PROMISE_S_MIGRATING	0x40
#define PROMISE_S_FUNCTIONAL	0x80

	uint8_t		type;			/* Voluem type. */
#define PROMISE_T_RAID0		0x00
#define PROMISE_T_RAID1		0x01
#define PROMISE_T_RAID3		0x02
#define PROMISE_T_RAID5		0x04
#define PROMISE_T_SPAN		0x08
#define PROMISE_T_JBOD		0x10

	uint8_t		total_disks;		/* Disks in this volume. */
	uint8_t		stripe_shift;		/* Strip size. */
	uint8_t		array_width;		/* Number of RAID0 stripes. */
	uint8_t		array_number;		/* Global volume number. */
	uint32_t	total_sectors;		/* Volume size. */
	uint16_t	cylinders;		/* Volume geometry: C. */
	uint8_t		heads;			/* Volume geometry: H. */
	uint8_t		sectors;		/* Volume geometry: S. */
	uint64_t	volume_id __packed;	/* Volume ID, */
	struct promise_raid_disk	disks[PROMISE_MAX_DISKS];
						/* Subdisks in this volume. */
	char		name[32];		/* Volume label. */

	uint32_t	filler2[8];
	uint32_t	magic_3;	/* Something related to rebuild. */
	uint64_t	rebuild_lba64;	/* Per-volume rebuild position. */
	uint32_t	magic_4;
	uint32_t	magic_5;
	uint32_t	total_sectors_high;
	uint8_t		magic_6;
	uint8_t		sector_size;
	uint16_t	magic_7;
	uint32_t	magic_8[31];
	uint32_t	backup_time;
	uint16_t	magic_9;
	uint32_t	disk_offset_high;
	uint32_t	disk_sectors_high;
	uint32_t	disk_rebuild_high;
	uint16_t	magic_10;
	uint32_t	magic_11[3];
	uint32_t	filler3[284];
	uint32_t	checksum;
} __packed;

struct g_raid_md_promise_perdisk {
	int		 pd_updated;
	int		 pd_subdisks;
	struct promise_raid_conf	*pd_meta[PROMISE_MAX_SUBDISKS];
};

struct g_raid_md_promise_pervolume {
	struct promise_raid_conf	*pv_meta;
	uint64_t			 pv_id;
	uint16_t			 pv_generation;
	int				 pv_disks_present;
	int				 pv_started;
	struct callout			 pv_start_co;	/* STARTING state timer. */
};

static g_raid_md_create_t g_raid_md_create_promise;
static g_raid_md_taste_t g_raid_md_taste_promise;
static g_raid_md_event_t g_raid_md_event_promise;
static g_raid_md_volume_event_t g_raid_md_volume_event_promise;
static g_raid_md_ctl_t g_raid_md_ctl_promise;
static g_raid_md_write_t g_raid_md_write_promise;
static g_raid_md_fail_disk_t g_raid_md_fail_disk_promise;
static g_raid_md_free_disk_t g_raid_md_free_disk_promise;
static g_raid_md_free_volume_t g_raid_md_free_volume_promise;
static g_raid_md_free_t g_raid_md_free_promise;

static kobj_method_t g_raid_md_promise_methods[] = {
	KOBJMETHOD(g_raid_md_create,	g_raid_md_create_promise),
	KOBJMETHOD(g_raid_md_taste,	g_raid_md_taste_promise),
	KOBJMETHOD(g_raid_md_event,	g_raid_md_event_promise),
	KOBJMETHOD(g_raid_md_volume_event,	g_raid_md_volume_event_promise),
	KOBJMETHOD(g_raid_md_ctl,	g_raid_md_ctl_promise),
	KOBJMETHOD(g_raid_md_write,	g_raid_md_write_promise),
	KOBJMETHOD(g_raid_md_fail_disk,	g_raid_md_fail_disk_promise),
	KOBJMETHOD(g_raid_md_free_disk,	g_raid_md_free_disk_promise),
	KOBJMETHOD(g_raid_md_free_volume,	g_raid_md_free_volume_promise),
	KOBJMETHOD(g_raid_md_free,	g_raid_md_free_promise),
	{ 0, 0 }
};

static struct g_raid_md_class g_raid_md_promise_class = {
	"Promise",
	g_raid_md_promise_methods,
	sizeof(struct g_raid_md_object),
	.mdc_enable = 1,
	.mdc_priority = 100
};


static void
g_raid_md_promise_print(struct promise_raid_conf *meta)
{
	int i;

	if (g_raid_debug < 1)
		return;

	printf("********* ATA Promise Metadata *********\n");
	printf("promise_id          <%.24s>\n", meta->promise_id);
	printf("disk                %02x %02x %02x %02x %016jx\n",
	    meta->disk.flags, meta->disk.number, meta->disk.channel,
	    meta->disk.device, meta->disk.id);
	printf("disk_offset         %u\n", meta->disk_offset);
	printf("disk_sectors        %u\n", meta->disk_sectors);
	printf("disk_rebuild        %u\n", meta->disk_rebuild);
	printf("generation          %u\n", meta->generation);
	printf("status              0x%02x\n", meta->status);
	printf("type                %u\n", meta->type);
	printf("total_disks         %u\n", meta->total_disks);
	printf("stripe_shift        %u\n", meta->stripe_shift);
	printf("array_width         %u\n", meta->array_width);
	printf("array_number        %u\n", meta->array_number);
	printf("total_sectors       %u\n", meta->total_sectors);
	printf("cylinders           %u\n", meta->cylinders);
	printf("heads               %u\n", meta->heads);
	printf("sectors             %u\n", meta->sectors);
	printf("volume_id           0x%016jx\n", meta->volume_id);
	printf("disks:\n");
	for (i = 0; i < PROMISE_MAX_DISKS; i++ ) {
		printf("                    %02x %02x %02x %02x %016jx\n",
		    meta->disks[i].flags, meta->disks[i].number,
		    meta->disks[i].channel, meta->disks[i].device,
		    meta->disks[i].id);
	}
	printf("name                <%.32s>\n", meta->name);
	printf("magic_3             0x%08x\n", meta->magic_3);
	printf("rebuild_lba64       %ju\n", meta->rebuild_lba64);
	printf("magic_4             0x%08x\n", meta->magic_4);
	printf("magic_5             0x%08x\n", meta->magic_5);
	printf("total_sectors_high  0x%08x\n", meta->total_sectors_high);
	printf("sector_size         %u\n", meta->sector_size);
	printf("backup_time         %d\n", meta->backup_time);
	printf("disk_offset_high    0x%08x\n", meta->disk_offset_high);
	printf("disk_sectors_high   0x%08x\n", meta->disk_sectors_high);
	printf("disk_rebuild_high   0x%08x\n", meta->disk_rebuild_high);
	printf("=================================================\n");
}

static struct promise_raid_conf *
promise_meta_copy(struct promise_raid_conf *meta)
{
	struct promise_raid_conf *nmeta;

	nmeta = malloc(sizeof(*nmeta), M_MD_PROMISE, M_WAITOK);
	memcpy(nmeta, meta, sizeof(*nmeta));
	return (nmeta);
}

static int
promise_meta_find_disk(struct promise_raid_conf *meta, uint64_t id)
{
	int pos;

	for (pos = 0; pos < meta->total_disks; pos++) {
		if (meta->disks[pos].id == id)
			return (pos);
	}
	return (-1);
}

static int
promise_meta_unused_range(struct promise_raid_conf **metaarr, int nsd,
    off_t sectors, off_t *off, off_t *size)
{
	off_t coff, csize, tmp;
	int i, j;

	sectors -= 131072;
	*off = 0;
	*size = 0;
	coff = 0;
	csize = sectors;
	i = 0;
	while (1) {
		for (j = 0; j < nsd; j++) {
			tmp = ((off_t)metaarr[j]->disk_offset_high << 32) +
			    metaarr[j]->disk_offset;
			if (tmp >= coff)
				csize = MIN(csize, tmp - coff);
		}
		if (csize > *size) {
			*off = coff;
			*size = csize;
		}
		if (i >= nsd)
			break;
		coff = ((off_t)metaarr[i]->disk_offset_high << 32) +
		     metaarr[i]->disk_offset +
		    ((off_t)metaarr[i]->disk_sectors_high << 32) +
		     metaarr[i]->disk_sectors;
		csize = sectors - coff;
		i++;
	}
	return ((*size > 0) ? 1 : 0);
}

static int
promise_meta_translate_disk(struct g_raid_volume *vol, int md_disk_pos)
{
	int disk_pos, width;

	if (md_disk_pos >= 0 && vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E) {
		width = vol->v_disks_count / 2;
		disk_pos = (md_disk_pos / width) +
		    (md_disk_pos % width) * width;
	} else
		disk_pos = md_disk_pos;
	return (disk_pos);
}

static void
promise_meta_get_name(struct promise_raid_conf *meta, char *buf)
{
	int i;

	strncpy(buf, meta->name, 32);
	buf[32] = 0;
	for (i = 31; i >= 0; i--) {
		if (buf[i] > 0x20)
			break;
		buf[i] = 0;
	}
}

static void
promise_meta_put_name(struct promise_raid_conf *meta, char *buf)
{

	memset(meta->name, 0x20, 32);
	memcpy(meta->name, buf, MIN(strlen(buf), 32));
}

static int
promise_meta_read(struct g_consumer *cp, struct promise_raid_conf **metaarr)
{
	struct g_provider *pp;
	struct promise_raid_conf *meta;
	char *buf;
	int error, i, subdisks;
	uint32_t checksum, *ptr;

	pp = cp->provider;
	subdisks = 0;

	if (pp->sectorsize * 4 > MAXPHYS) {
		G_RAID_DEBUG(1, "%s: Blocksize is too big.", pp->name);
		return (subdisks);
	}
next:
	/* Read metadata block. */
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize *
	    (63 - subdisks * PROMISE_META_OFFSET),
	    pp->sectorsize * 4, &error);
	if (buf == NULL) {
		G_RAID_DEBUG(1, "Cannot read metadata from %s (error=%d).",
		    pp->name, error);
		return (subdisks);
	}
	meta = (struct promise_raid_conf *)buf;

	/* Check if this is an Promise RAID struct */
	if (strncmp(meta->promise_id, PROMISE_MAGIC, strlen(PROMISE_MAGIC)) &&
	    strncmp(meta->promise_id, FREEBSD_MAGIC, strlen(FREEBSD_MAGIC))) {
		if (subdisks == 0)
			G_RAID_DEBUG(1,
			    "Promise signature check failed on %s", pp->name);
		g_free(buf);
		return (subdisks);
	}
	meta = malloc(sizeof(*meta), M_MD_PROMISE, M_WAITOK);
	memcpy(meta, buf, MIN(sizeof(*meta), pp->sectorsize * 4));
	g_free(buf);

	/* Check metadata checksum. */
	for (checksum = 0, ptr = (uint32_t *)meta, i = 0; i < 511; i++)
		checksum += *ptr++;
	if (checksum != meta->checksum) {
		G_RAID_DEBUG(1, "Promise checksum check failed on %s", pp->name);
		free(meta, M_MD_PROMISE);
		return (subdisks);
	}

	if ((meta->integrity & PROMISE_I_VALID) == 0) {
		G_RAID_DEBUG(1, "Promise metadata is invalid on %s", pp->name);
		free(meta, M_MD_PROMISE);
		return (subdisks);
	}

	if (meta->total_disks > PROMISE_MAX_DISKS) {
		G_RAID_DEBUG(1, "Wrong number of disks on %s (%d)",
		    pp->name, meta->total_disks);
		free(meta, M_MD_PROMISE);
		return (subdisks);
	}

	/* Remove filler garbage from fields used in newer metadata. */
	if (meta->disk_offset_high == 0x8b8c8d8e &&
	    meta->disk_sectors_high == 0x8788898a &&
	    meta->disk_rebuild_high == 0x83848586) {
		meta->disk_offset_high = 0;
		meta->disk_sectors_high = 0;
		if (meta->disk_rebuild == UINT32_MAX)
			meta->disk_rebuild_high = UINT32_MAX;
		else
			meta->disk_rebuild_high = 0;
		if (meta->total_sectors_high == 0x15161718) {
			meta->total_sectors_high = 0;
			meta->backup_time = 0;
			if (meta->rebuild_lba64 == 0x2122232425262728)
				meta->rebuild_lba64 = UINT64_MAX;
		}
	}
	if (meta->sector_size < 1 || meta->sector_size > 8)
		meta->sector_size = 1;

	/* Save this part and look for next. */
	*metaarr = meta;
	metaarr++;
	subdisks++;
	if (subdisks < PROMISE_MAX_SUBDISKS)
		goto next;

	return (subdisks);
}

static int
promise_meta_write(struct g_consumer *cp,
    struct promise_raid_conf **metaarr, int nsd)
{
	struct g_provider *pp;
	struct promise_raid_conf *meta;
	char *buf;
	off_t off, size;
	int error, i, subdisk, fake;
	uint32_t checksum, *ptr;

	pp = cp->provider;
	subdisk = 0;
	fake = 0;
next:
	buf = malloc(pp->sectorsize * 4, M_MD_PROMISE, M_WAITOK | M_ZERO);
	meta = NULL;
	if (subdisk < nsd) {
		meta = metaarr[subdisk];
	} else if (!fake && promise_meta_unused_range(metaarr, nsd,
	    cp->provider->mediasize / cp->provider->sectorsize,
	    &off, &size)) {
		/* Optionally add record for unused space. */
		meta = (struct promise_raid_conf *)buf;
		memcpy(&meta->promise_id[0], PROMISE_MAGIC,
		    sizeof(PROMISE_MAGIC) - 1);
		meta->dummy_0 = 0x00020000;
		meta->integrity = PROMISE_I_VALID;
		meta->disk.flags = PROMISE_F_ONLINE | PROMISE_F_VALID;
		meta->disk.number = 0xff;
		arc4rand(&meta->disk.id, sizeof(meta->disk.id), 0);
		meta->disk_offset_high = off >> 32;
		meta->disk_offset = (uint32_t)off;
		meta->disk_sectors_high = size >> 32;
		meta->disk_sectors = (uint32_t)size;
		meta->disk_rebuild_high = UINT32_MAX;
		meta->disk_rebuild = UINT32_MAX;
		fake = 1;
	}
	if (meta != NULL) {
		/* Recalculate checksum for case if metadata were changed. */
		meta->checksum = 0;
		for (checksum = 0, ptr = (uint32_t *)meta, i = 0; i < 511; i++)
			checksum += *ptr++;
		meta->checksum = checksum;
		memcpy(buf, meta, MIN(pp->sectorsize * 4, sizeof(*meta)));
	}
	error = g_write_data(cp, pp->mediasize - pp->sectorsize *
	    (63 - subdisk * PROMISE_META_OFFSET),
	    buf, pp->sectorsize * 4);
	if (error != 0) {
		G_RAID_DEBUG(1, "Cannot write metadata to %s (error=%d).",
		    pp->name, error);
	}
	free(buf, M_MD_PROMISE);

	subdisk++;
	if (subdisk < PROMISE_MAX_SUBDISKS)
		goto next;

	return (error);
}

static int
promise_meta_erase(struct g_consumer *cp)
{
	struct g_provider *pp;
	char *buf;
	int error, subdisk;

	pp = cp->provider;
	buf = malloc(4 * pp->sectorsize, M_MD_PROMISE, M_WAITOK | M_ZERO);
	for (subdisk = 0; subdisk < PROMISE_MAX_SUBDISKS; subdisk++) {
		error = g_write_data(cp, pp->mediasize - pp->sectorsize *
		    (63 - subdisk * PROMISE_META_OFFSET),
		    buf, 4 * pp->sectorsize);
		if (error != 0) {
			G_RAID_DEBUG(1, "Cannot erase metadata on %s (error=%d).",
			    pp->name, error);
		}
	}
	free(buf, M_MD_PROMISE);
	return (error);
}

static int
promise_meta_write_spare(struct g_consumer *cp)
{
	struct promise_raid_conf *meta;
	off_t tmp;
	int error;

	meta = malloc(sizeof(*meta), M_MD_PROMISE, M_WAITOK | M_ZERO);
	memcpy(&meta->promise_id[0], PROMISE_MAGIC, sizeof(PROMISE_MAGIC) - 1);
	meta->dummy_0 = 0x00020000;
	meta->integrity = PROMISE_I_VALID;
	meta->disk.flags = PROMISE_F_SPARE | PROMISE_F_ONLINE | PROMISE_F_VALID;
	meta->disk.number = 0xff;
	arc4rand(&meta->disk.id, sizeof(meta->disk.id), 0);
	tmp = cp->provider->mediasize / cp->provider->sectorsize - 131072;
	meta->disk_sectors_high = tmp >> 32;
	meta->disk_sectors = (uint32_t)tmp;
	meta->disk_rebuild_high = UINT32_MAX;
	meta->disk_rebuild = UINT32_MAX;
	error = promise_meta_write(cp, &meta, 1);
	free(meta, M_MD_PROMISE);
	return (error);
}

static struct g_raid_volume *
g_raid_md_promise_get_volume(struct g_raid_softc *sc, uint64_t id)
{
	struct g_raid_volume	*vol;
	struct g_raid_md_promise_pervolume *pv;

	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		pv = vol->v_md_data;
		if (pv->pv_id == id)
			break;
	}
	return (vol);
}

static int
g_raid_md_promise_purge_volumes(struct g_raid_softc *sc)
{
	struct g_raid_volume	*vol, *tvol;
	struct g_raid_md_promise_pervolume *pv;
	int i, res;

	res = 0;
	TAILQ_FOREACH_SAFE(vol, &sc->sc_volumes, v_next, tvol) {
		pv = vol->v_md_data;
		if (!pv->pv_started || vol->v_stopping)
			continue;
		for (i = 0; i < vol->v_disks_count; i++) {
			if (vol->v_subdisks[i].sd_state != G_RAID_SUBDISK_S_NONE)
				break;
		}
		if (i >= vol->v_disks_count) {
			g_raid_destroy_volume(vol);
			res = 1;
		}
	}
	return (res);
}

static int
g_raid_md_promise_purge_disks(struct g_raid_softc *sc)
{
	struct g_raid_disk	*disk, *tdisk;
	struct g_raid_volume	*vol;
	struct g_raid_md_promise_perdisk *pd;
	int i, j, res;

	res = 0;
	TAILQ_FOREACH_SAFE(disk, &sc->sc_disks, d_next, tdisk) {
		if (disk->d_state == G_RAID_DISK_S_SPARE)
			continue;
		pd = (struct g_raid_md_promise_perdisk *)disk->d_md_data;

		/* Scan for deleted volumes. */
		for (i = 0; i < pd->pd_subdisks; ) {
			vol = g_raid_md_promise_get_volume(sc,
			    pd->pd_meta[i]->volume_id);
			if (vol != NULL && !vol->v_stopping) {
				i++;
				continue;
			}
			free(pd->pd_meta[i], M_MD_PROMISE);
			for (j = i; j < pd->pd_subdisks - 1; j++)
				pd->pd_meta[j] = pd->pd_meta[j + 1];
			pd->pd_meta[pd->pd_subdisks - 1] = NULL;
			pd->pd_subdisks--;
			pd->pd_updated = 1;
		}

		/* If there is no metadata left - erase and delete disk. */
		if (pd->pd_subdisks == 0) {
			promise_meta_erase(disk->d_consumer);
			g_raid_destroy_disk(disk);
			res = 1;
		}
	}
	return (res);
}

static int
g_raid_md_promise_supported(int level, int qual, int disks, int force)
{

	if (disks > PROMISE_MAX_DISKS)
		return (0);
	switch (level) {
	case G_RAID_VOLUME_RL_RAID0:
		if (disks < 1)
			return (0);
		if (!force && disks < 2)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID1:
		if (disks < 1)
			return (0);
		if (!force && (disks != 2))
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID1E:
		if (disks < 2)
			return (0);
		if (disks % 2 != 0)
			return (0);
		if (!force && (disks != 4))
			return (0);
		break;
	case G_RAID_VOLUME_RL_SINGLE:
		if (disks != 1)
			return (0);
		break;
	case G_RAID_VOLUME_RL_CONCAT:
		if (disks < 2)
			return (0);
		break;
	case G_RAID_VOLUME_RL_RAID5:
		if (disks < 3)
			return (0);
		if (qual != G_RAID_VOLUME_RLQ_R5LA)
			return (0);
		break;
	default:
		return (0);
	}
	if (level != G_RAID_VOLUME_RL_RAID5 && qual != G_RAID_VOLUME_RLQ_NONE)
		return (0);
	return (1);
}

static int
g_raid_md_promise_start_disk(struct g_raid_disk *disk, int sdn,
    struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd;
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_md_promise_pervolume *pv;
	struct promise_raid_conf *meta;
	off_t eoff, esize, size;
	int disk_pos, md_disk_pos, i, resurrection = 0;

	sc = disk->d_softc;
	pd = (struct g_raid_md_promise_perdisk *)disk->d_md_data;

	pv = vol->v_md_data;
	meta = pv->pv_meta;

	if (sdn >= 0) {
		/* Find disk position in metadata by its serial. */
		md_disk_pos = promise_meta_find_disk(meta, pd->pd_meta[sdn]->disk.id);
		/* For RAID0+1 we need to translate order. */
		disk_pos = promise_meta_translate_disk(vol, md_disk_pos);
	} else {
		md_disk_pos = -1;
		disk_pos = -1;
	}
	if (disk_pos < 0) {
		G_RAID_DEBUG1(1, sc, "Disk %s is not part of the volume %s",
		    g_raid_get_diskname(disk), vol->v_name);
		/* Failed stale disk is useless for us. */
		if (sdn >= 0 &&
		    pd->pd_meta[sdn]->disk.flags & PROMISE_F_DOWN) {
			g_raid_change_disk_state(disk, G_RAID_DISK_S_STALE_FAILED);
			return (0);
		}
		/* If we were given specific metadata subdisk - erase it. */
		if (sdn >= 0) {
			free(pd->pd_meta[sdn], M_MD_PROMISE);
			for (i = sdn; i < pd->pd_subdisks - 1; i++)
				pd->pd_meta[i] = pd->pd_meta[i + 1];
			pd->pd_meta[pd->pd_subdisks - 1] = NULL;
			pd->pd_subdisks--;
		}
		/* If we are in the start process, that's all for now. */
		if (!pv->pv_started)
			goto nofit;
		/*
		 * If we have already started - try to get use of the disk.
		 * Try to replace OFFLINE disks first, then FAILED.
		 */
		promise_meta_unused_range(pd->pd_meta, pd->pd_subdisks,
		    disk->d_consumer->provider->mediasize /
		    disk->d_consumer->provider->sectorsize,
		    &eoff, &esize);
		if (esize == 0) {
			G_RAID_DEBUG1(1, sc, "No free space on disk %s",
			    g_raid_get_diskname(disk));
			goto nofit;
		}
		size = INT64_MAX;
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			if (sd->sd_state != G_RAID_SUBDISK_S_NONE)
				size = sd->sd_size;
			if (sd->sd_state <= G_RAID_SUBDISK_S_FAILED &&
			    (disk_pos < 0 ||
			     vol->v_subdisks[i].sd_state < sd->sd_state))
				disk_pos = i;
		}
		if (disk_pos >= 0 &&
		    vol->v_raid_level != G_RAID_VOLUME_RL_CONCAT &&
		    (off_t)esize * 512 < size) {
			G_RAID_DEBUG1(1, sc, "Disk %s free space "
			    "is too small (%ju < %ju)",
			    g_raid_get_diskname(disk),
			    (off_t)esize * 512, size);
			disk_pos = -1;
		}
		if (disk_pos >= 0) {
			if (vol->v_raid_level != G_RAID_VOLUME_RL_CONCAT)
				esize = size / 512;
			/* For RAID0+1 we need to translate order. */
			md_disk_pos = promise_meta_translate_disk(vol, disk_pos);
		} else {
nofit:
			if (pd->pd_subdisks == 0) {
				g_raid_change_disk_state(disk,
				    G_RAID_DISK_S_SPARE);
			}
			return (0);
		}
		G_RAID_DEBUG1(1, sc, "Disk %s takes pos %d in the volume %s",
		    g_raid_get_diskname(disk), disk_pos, vol->v_name);
		resurrection = 1;
	}

	sd = &vol->v_subdisks[disk_pos];

	if (resurrection && sd->sd_disk != NULL) {
		g_raid_change_disk_state(sd->sd_disk,
		    G_RAID_DISK_S_STALE_FAILED);
		TAILQ_REMOVE(&sd->sd_disk->d_subdisks,
		    sd, sd_next);
	}
	vol->v_subdisks[disk_pos].sd_disk = disk;
	TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);

	/* Welcome the new disk. */
	if (resurrection)
		g_raid_change_disk_state(disk, G_RAID_DISK_S_ACTIVE);
	else if (meta->disks[md_disk_pos].flags & PROMISE_F_DOWN)
		g_raid_change_disk_state(disk, G_RAID_DISK_S_FAILED);
	else
		g_raid_change_disk_state(disk, G_RAID_DISK_S_ACTIVE);

	if (resurrection) {
		sd->sd_offset = (off_t)eoff * 512;
		sd->sd_size = (off_t)esize * 512;
	} else {
		sd->sd_offset = (((off_t)pd->pd_meta[sdn]->disk_offset_high
		    << 32) + pd->pd_meta[sdn]->disk_offset) * 512;
		sd->sd_size = (((off_t)pd->pd_meta[sdn]->disk_sectors_high
		    << 32) + pd->pd_meta[sdn]->disk_sectors) * 512;
	}

	if (resurrection) {
		/* Stale disk, almost same as new. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_NEW);
	} else if (meta->disks[md_disk_pos].flags & PROMISE_F_DOWN) {
		/* Failed disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_FAILED);
	} else if (meta->disks[md_disk_pos].flags & PROMISE_F_REDIR) {
		/* Rebuilding disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_REBUILD);
		if (pd->pd_meta[sdn]->generation != meta->generation)
			sd->sd_rebuild_pos = 0;
		else {
			sd->sd_rebuild_pos =
			    (((off_t)pd->pd_meta[sdn]->disk_rebuild_high << 32) +
			     pd->pd_meta[sdn]->disk_rebuild) * 512;
		}
	} else if (!(meta->disks[md_disk_pos].flags & PROMISE_F_ONLINE)) {
		/* Rebuilding disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_NEW);
	} else if (pd->pd_meta[sdn]->generation != meta->generation ||
	    (meta->status & PROMISE_S_MARKED)) {
		/* Stale disk or dirty volume (unclean shutdown). */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_STALE);
	} else {
		/* Up to date disk. */
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_ACTIVE);
	}
	g_raid_event_send(sd, G_RAID_SUBDISK_E_NEW,
	    G_RAID_EVENT_SUBDISK);

	return (resurrection);
}

static void
g_raid_md_promise_refill(struct g_raid_softc *sc)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_object *md;
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_md_promise_pervolume *pv;
	int update, updated, i, bad;

	md = sc->sc_md;
restart:
	updated = 0;
	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		pv = vol->v_md_data;
		if (!pv->pv_started || vol->v_stopping)
			continue;

		/* Search for subdisk that needs replacement. */
		bad = 0;
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			if (sd->sd_state == G_RAID_SUBDISK_S_NONE ||
			    sd->sd_state == G_RAID_SUBDISK_S_FAILED)
			        bad = 1;
		}
		if (!bad)
			continue;

		G_RAID_DEBUG1(1, sc, "Volume %s is not complete, "
		    "trying to refill.", vol->v_name);

		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			/* Skip failed. */
			if (disk->d_state < G_RAID_DISK_S_SPARE)
				continue;
			/* Skip already used by this volume. */
			for (i = 0; i < vol->v_disks_count; i++) {
				sd = &vol->v_subdisks[i];
				if (sd->sd_disk == disk)
					break;
			}
			if (i < vol->v_disks_count)
				continue;

			/* Try to use disk if it has empty extents. */
			pd = disk->d_md_data;
			if (pd->pd_subdisks < PROMISE_MAX_SUBDISKS) {
				update =
				    g_raid_md_promise_start_disk(disk, -1, vol);
			} else
				update = 0;
			if (update) {
				updated = 1;
				g_raid_md_write_promise(md, vol, NULL, disk);
				break;
			}
		}
	}
	if (updated)
		goto restart;
}

static void
g_raid_md_promise_start(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_object *md;
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_md_promise_pervolume *pv;
	struct promise_raid_conf *meta;
	u_int i;

	sc = vol->v_softc;
	md = sc->sc_md;
	pv = vol->v_md_data;
	meta = pv->pv_meta;

	vol->v_raid_level_qualifier = G_RAID_VOLUME_RLQ_NONE;
	if (meta->type == PROMISE_T_RAID0)
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID0;
	else if (meta->type == PROMISE_T_RAID1) {
		if (meta->array_width == 1)
			vol->v_raid_level = G_RAID_VOLUME_RL_RAID1;
		else
			vol->v_raid_level = G_RAID_VOLUME_RL_RAID1E;
	} else if (meta->type == PROMISE_T_RAID3)
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID3;
	else if (meta->type == PROMISE_T_RAID5) {
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID5;
		vol->v_raid_level_qualifier = G_RAID_VOLUME_RLQ_R5LA;
	} else if (meta->type == PROMISE_T_SPAN)
		vol->v_raid_level = G_RAID_VOLUME_RL_CONCAT;
	else if (meta->type == PROMISE_T_JBOD)
		vol->v_raid_level = G_RAID_VOLUME_RL_SINGLE;
	else
		vol->v_raid_level = G_RAID_VOLUME_RL_UNKNOWN;
	vol->v_strip_size = 512 << meta->stripe_shift; //ZZZ
	vol->v_disks_count = meta->total_disks;
	vol->v_mediasize = (off_t)meta->total_sectors * 512; //ZZZ
	if (meta->total_sectors_high < 256) /* If value looks sane. */
		vol->v_mediasize +=
		    ((off_t)meta->total_sectors_high << 32) * 512; //ZZZ
	vol->v_sectorsize = 512 * meta->sector_size;
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		sd->sd_offset = (((off_t)meta->disk_offset_high << 32) +
		    meta->disk_offset) * 512;
		sd->sd_size = (((off_t)meta->disk_sectors_high << 32) +
		    meta->disk_sectors) * 512;
	}
	g_raid_start_volume(vol);

	/* Make all disks found till the moment take their places. */
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = disk->d_md_data;
		for (i = 0; i < pd->pd_subdisks; i++) {
			if (pd->pd_meta[i]->volume_id == meta->volume_id)
				g_raid_md_promise_start_disk(disk, i, vol);
		}
	}

	pv->pv_started = 1;
	callout_stop(&pv->pv_start_co);
	G_RAID_DEBUG1(0, sc, "Volume started.");
	g_raid_md_write_promise(md, vol, NULL, NULL);

	/* Pickup any STALE/SPARE disks to refill array if needed. */
	g_raid_md_promise_refill(sc);

	g_raid_event_send(vol, G_RAID_VOLUME_E_START, G_RAID_EVENT_VOLUME);
}

static void
g_raid_promise_go(void *arg)
{
	struct g_raid_volume *vol;
	struct g_raid_softc *sc;
	struct g_raid_md_promise_pervolume *pv;

	vol = arg;
	pv = vol->v_md_data;
	sc = vol->v_softc;
	if (!pv->pv_started) {
		G_RAID_DEBUG1(0, sc, "Force volume start due to timeout.");
		g_raid_event_send(vol, G_RAID_VOLUME_E_STARTMD,
		    G_RAID_EVENT_VOLUME);
	}
}

static void
g_raid_md_promise_new_disk(struct g_raid_disk *disk)
{
	struct g_raid_softc *sc;
	struct g_raid_md_object *md;
	struct promise_raid_conf *pdmeta;
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_md_promise_pervolume *pv;
	struct g_raid_volume *vol;
	int i;
	char buf[33];

	sc = disk->d_softc;
	md = sc->sc_md;
	pd = (struct g_raid_md_promise_perdisk *)disk->d_md_data;

	if (pd->pd_subdisks == 0) {
		g_raid_change_disk_state(disk, G_RAID_DISK_S_SPARE);
		g_raid_md_promise_refill(sc);
		return;
	}

	for (i = 0; i < pd->pd_subdisks; i++) {
		pdmeta = pd->pd_meta[i];

		/* Look for volume with matching ID. */
		vol = g_raid_md_promise_get_volume(sc, pdmeta->volume_id);
		if (vol == NULL) {
			promise_meta_get_name(pdmeta, buf);
			vol = g_raid_create_volume(sc, buf, pdmeta->array_number);
			pv = malloc(sizeof(*pv), M_MD_PROMISE, M_WAITOK | M_ZERO);
			pv->pv_id = pdmeta->volume_id;
			vol->v_md_data = pv;
			callout_init(&pv->pv_start_co, 1);
			callout_reset(&pv->pv_start_co,
			    g_raid_start_timeout * hz,
			    g_raid_promise_go, vol);
		} else
			pv = vol->v_md_data;

		/* If we haven't started yet - check metadata freshness. */
		if (pv->pv_meta == NULL || !pv->pv_started) {
			if (pv->pv_meta == NULL ||
			    ((int16_t)(pdmeta->generation - pv->pv_generation)) > 0) {
				G_RAID_DEBUG1(1, sc, "Newer disk");
				if (pv->pv_meta != NULL)
					free(pv->pv_meta, M_MD_PROMISE);
				pv->pv_meta = promise_meta_copy(pdmeta);
				pv->pv_generation = pv->pv_meta->generation;
				pv->pv_disks_present = 1;
			} else if (pdmeta->generation == pv->pv_generation) {
				pv->pv_disks_present++;
				G_RAID_DEBUG1(1, sc, "Matching disk (%d of %d up)",
				    pv->pv_disks_present,
				    pv->pv_meta->total_disks);
			} else {
				G_RAID_DEBUG1(1, sc, "Older disk");
			}
		}
	}

	for (i = 0; i < pd->pd_subdisks; i++) {
		pdmeta = pd->pd_meta[i];

		/* Look for volume with matching ID. */
		vol = g_raid_md_promise_get_volume(sc, pdmeta->volume_id);
		if (vol == NULL)
			continue;
		pv = vol->v_md_data;

		if (pv->pv_started) {
			if (g_raid_md_promise_start_disk(disk, i, vol))
				g_raid_md_write_promise(md, vol, NULL, NULL);
		} else {
			/* If we collected all needed disks - start array. */
			if (pv->pv_disks_present == pv->pv_meta->total_disks)
				g_raid_md_promise_start(vol);
		}
	}
}

static int
g_raid_md_create_promise(struct g_raid_md_object *md, struct g_class *mp,
    struct g_geom **gp)
{
	struct g_geom *geom;
	struct g_raid_softc *sc;

	/* Search for existing node. */
	LIST_FOREACH(geom, &mp->geom, geom) {
		sc = geom->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		if (sc->sc_md->mdo_class != md->mdo_class)
			continue;
		break;
	}
	if (geom != NULL) {
		*gp = geom;
		return (G_RAID_MD_TASTE_EXISTING);
	}

	/* Create new one if not found. */
	sc = g_raid_create_node(mp, "Promise", md);
	if (sc == NULL)
		return (G_RAID_MD_TASTE_FAIL);
	md->mdo_softc = sc;
	*gp = sc->sc_geom;
	return (G_RAID_MD_TASTE_NEW);
}

static int
g_raid_md_taste_promise(struct g_raid_md_object *md, struct g_class *mp,
                              struct g_consumer *cp, struct g_geom **gp)
{
	struct g_consumer *rcp;
	struct g_provider *pp;
	struct g_raid_softc *sc;
	struct g_raid_disk *disk;
	struct promise_raid_conf *metaarr[4];
	struct g_raid_md_promise_perdisk *pd;
	struct g_geom *geom;
	int i, j, result, len, subdisks;
	char name[16];
	uint16_t vendor;

	G_RAID_DEBUG(1, "Tasting Promise on %s", cp->provider->name);
	pp = cp->provider;

	/* Read metadata from device. */
	g_topology_unlock();
	vendor = 0xffff;
	len = sizeof(vendor);
	if (pp->geom->rank == 1)
		g_io_getattr("GEOM::hba_vendor", cp, &len, &vendor);
	subdisks = promise_meta_read(cp, metaarr);
	g_topology_lock();
	if (subdisks == 0) {
		if (g_raid_aggressive_spare) {
			if (vendor == 0x105a || vendor == 0x1002) {
				G_RAID_DEBUG(1,
				    "No Promise metadata, forcing spare.");
				goto search;
			} else {
				G_RAID_DEBUG(1,
				    "Promise/ATI vendor mismatch "
				    "0x%04x != 0x105a/0x1002",
				    vendor);
			}
		}
		return (G_RAID_MD_TASTE_FAIL);
	}

	/* Metadata valid. Print it. */
	for (i = 0; i < subdisks; i++)
		g_raid_md_promise_print(metaarr[i]);

	/* Purge meaningless (empty/spare) records. */
	for (i = 0; i < subdisks; ) {
		if (metaarr[i]->disk.flags & PROMISE_F_ASSIGNED) {
			i++;
			continue;
		}
		free(metaarr[i], M_MD_PROMISE);
		for (j = i; j < subdisks - 1; j++)
			metaarr[i] = metaarr[j + 1];
		metaarr[subdisks - 1] = NULL;
		subdisks--;
	}

search:
	/* Search for matching node. */
	sc = NULL;
	LIST_FOREACH(geom, &mp->geom, geom) {
		sc = geom->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		if (sc->sc_md->mdo_class != md->mdo_class)
			continue;
		break;
	}

	/* Found matching node. */
	if (geom != NULL) {
		G_RAID_DEBUG(1, "Found matching array %s", sc->sc_name);
		result = G_RAID_MD_TASTE_EXISTING;

	} else { /* Not found matching node -- create one. */
		result = G_RAID_MD_TASTE_NEW;
		snprintf(name, sizeof(name), "Promise");
		sc = g_raid_create_node(mp, name, md);
		md->mdo_softc = sc;
		geom = sc->sc_geom;
	}

	/* There is no return after this point, so we close passed consumer. */
	g_access(cp, -1, 0, 0);

	rcp = g_new_consumer(geom);
	rcp->flags |= G_CF_DIRECT_RECEIVE;
	g_attach(rcp, pp);
	if (g_access(rcp, 1, 1, 1) != 0)
		; //goto fail1;

	g_topology_unlock();
	sx_xlock(&sc->sc_lock);

	pd = malloc(sizeof(*pd), M_MD_PROMISE, M_WAITOK | M_ZERO);
	pd->pd_subdisks = subdisks;
	for (i = 0; i < subdisks; i++)
		pd->pd_meta[i] = metaarr[i];
	disk = g_raid_create_disk(sc);
	disk->d_md_data = (void *)pd;
	disk->d_consumer = rcp;
	rcp->private = disk;

	g_raid_get_disk_info(disk);

	g_raid_md_promise_new_disk(disk);

	sx_xunlock(&sc->sc_lock);
	g_topology_lock();
	*gp = geom;
	return (result);
}

static int
g_raid_md_event_promise(struct g_raid_md_object *md,
    struct g_raid_disk *disk, u_int event)
{
	struct g_raid_softc *sc;

	sc = md->mdo_softc;
	if (disk == NULL)
		return (-1);
	switch (event) {
	case G_RAID_DISK_E_DISCONNECTED:
		/* Delete disk. */
		g_raid_change_disk_state(disk, G_RAID_DISK_S_NONE);
		g_raid_destroy_disk(disk);
		g_raid_md_promise_purge_volumes(sc);

		/* Write updated metadata to all disks. */
		g_raid_md_write_promise(md, NULL, NULL, NULL);

		/* Check if anything left. */
		if (g_raid_ndisks(sc, -1) == 0)
			g_raid_destroy_node(sc, 0);
		else
			g_raid_md_promise_refill(sc);
		return (0);
	}
	return (-2);
}

static int
g_raid_md_volume_event_promise(struct g_raid_md_object *md,
    struct g_raid_volume *vol, u_int event)
{
	struct g_raid_md_promise_pervolume *pv;

	pv = (struct g_raid_md_promise_pervolume *)vol->v_md_data;
	switch (event) {
	case G_RAID_VOLUME_E_STARTMD:
		if (!pv->pv_started)
			g_raid_md_promise_start(vol);
		return (0);
	}
	return (-2);
}

static int
g_raid_md_ctl_promise(struct g_raid_md_object *md,
    struct gctl_req *req)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol, *vol1;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk, *disks[PROMISE_MAX_DISKS];
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_md_promise_pervolume *pv;
	struct g_consumer *cp;
	struct g_provider *pp;
	char arg[16];
	const char *nodename, *verb, *volname, *levelname, *diskname;
	char *tmp;
	int *nargs, *force;
	off_t esize, offs[PROMISE_MAX_DISKS], size, sectorsize, strip;
	intmax_t *sizearg, *striparg;
	int numdisks, i, len, level, qual;
	int error;

	sc = md->mdo_softc;
	verb = gctl_get_param(req, "verb", NULL);
	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	error = 0;
	if (strcmp(verb, "label") == 0) {

		if (*nargs < 4) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		volname = gctl_get_asciiparam(req, "arg1");
		if (volname == NULL) {
			gctl_error(req, "No volume name.");
			return (-2);
		}
		levelname = gctl_get_asciiparam(req, "arg2");
		if (levelname == NULL) {
			gctl_error(req, "No RAID level.");
			return (-3);
		}
		if (strcasecmp(levelname, "RAID5") == 0)
			levelname = "RAID5-LA";
		if (g_raid_volume_str2level(levelname, &level, &qual)) {
			gctl_error(req, "Unknown RAID level '%s'.", levelname);
			return (-4);
		}
		numdisks = *nargs - 3;
		force = gctl_get_paraml(req, "force", sizeof(*force));
		if (!g_raid_md_promise_supported(level, qual, numdisks,
		    force ? *force : 0)) {
			gctl_error(req, "Unsupported RAID level "
			    "(0x%02x/0x%02x), or number of disks (%d).",
			    level, qual, numdisks);
			return (-5);
		}

		/* Search for disks, connect them and probe. */
		size = INT64_MAX;
		sectorsize = 0;
		bzero(disks, sizeof(disks));
		bzero(offs, sizeof(offs));
		for (i = 0; i < numdisks; i++) {
			snprintf(arg, sizeof(arg), "arg%d", i + 3);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -6;
				break;
			}
			if (strcmp(diskname, "NONE") == 0)
				continue;

			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer != NULL && 
				    disk->d_consumer->provider != NULL &&
				    strcmp(disk->d_consumer->provider->name,
				     diskname) == 0)
					break;
			}
			if (disk != NULL) {
				if (disk->d_state != G_RAID_DISK_S_ACTIVE) {
					gctl_error(req, "Disk '%s' is in a "
					    "wrong state (%s).", diskname,
					    g_raid_disk_state2str(disk->d_state));
					error = -7;
					break;
				}
				pd = disk->d_md_data;
				if (pd->pd_subdisks >= PROMISE_MAX_SUBDISKS) {
					gctl_error(req, "Disk '%s' already "
					    "used by %d volumes.",
					    diskname, pd->pd_subdisks);
					error = -7;
					break;
				}
				pp = disk->d_consumer->provider;
				disks[i] = disk;
				promise_meta_unused_range(pd->pd_meta,
				    pd->pd_subdisks,
				    pp->mediasize / pp->sectorsize,
				    &offs[i], &esize);
				size = MIN(size, (off_t)esize * pp->sectorsize);
				sectorsize = MAX(sectorsize, pp->sectorsize);
				continue;
			}

			g_topology_lock();
			cp = g_raid_open_consumer(sc, diskname);
			if (cp == NULL) {
				gctl_error(req, "Can't open disk '%s'.",
				    diskname);
				g_topology_unlock();
				error = -8;
				break;
			}
			pp = cp->provider;
			pd = malloc(sizeof(*pd), M_MD_PROMISE, M_WAITOK | M_ZERO);
			disk = g_raid_create_disk(sc);
			disk->d_md_data = (void *)pd;
			disk->d_consumer = cp;
			disks[i] = disk;
			cp->private = disk;
			g_topology_unlock();

			g_raid_get_disk_info(disk);

			/* Reserve some space for metadata. */
			size = MIN(size, pp->mediasize - 131072llu * pp->sectorsize);
			sectorsize = MAX(sectorsize, pp->sectorsize);
		}
		if (error != 0) {
			for (i = 0; i < numdisks; i++) {
				if (disks[i] != NULL &&
				    disks[i]->d_state == G_RAID_DISK_S_NONE)
					g_raid_destroy_disk(disks[i]);
			}
			return (error);
		}

		if (sectorsize <= 0) {
			gctl_error(req, "Can't get sector size.");
			return (-8);
		}

		/* Handle size argument. */
		len = sizeof(*sizearg);
		sizearg = gctl_get_param(req, "size", &len);
		if (sizearg != NULL && len == sizeof(*sizearg) &&
		    *sizearg > 0) {
			if (*sizearg > size) {
				gctl_error(req, "Size too big %lld > %lld.",
				    (long long)*sizearg, (long long)size);
				return (-9);
			}
			size = *sizearg;
		}

		/* Handle strip argument. */
		strip = 131072;
		len = sizeof(*striparg);
		striparg = gctl_get_param(req, "strip", &len);
		if (striparg != NULL && len == sizeof(*striparg) &&
		    *striparg > 0) {
			if (*striparg < sectorsize) {
				gctl_error(req, "Strip size too small.");
				return (-10);
			}
			if (*striparg % sectorsize != 0) {
				gctl_error(req, "Incorrect strip size.");
				return (-11);
			}
			strip = *striparg;
		}

		/* Round size down to strip or sector. */
		if (level == G_RAID_VOLUME_RL_RAID1 ||
		    level == G_RAID_VOLUME_RL_SINGLE ||
		    level == G_RAID_VOLUME_RL_CONCAT)
			size -= (size % sectorsize);
		else if (level == G_RAID_VOLUME_RL_RAID1E &&
		    (numdisks & 1) != 0)
			size -= (size % (2 * strip));
		else
			size -= (size % strip);
		if (size <= 0) {
			gctl_error(req, "Size too small.");
			return (-13);
		}

		/* We have all we need, create things: volume, ... */
		pv = malloc(sizeof(*pv), M_MD_PROMISE, M_WAITOK | M_ZERO);
		arc4rand(&pv->pv_id, sizeof(pv->pv_id), 0);
		pv->pv_generation = 0;
		pv->pv_started = 1;
		vol = g_raid_create_volume(sc, volname, -1);
		vol->v_md_data = pv;
		vol->v_raid_level = level;
		vol->v_raid_level_qualifier = qual;
		vol->v_strip_size = strip;
		vol->v_disks_count = numdisks;
		if (level == G_RAID_VOLUME_RL_RAID0 ||
		    level == G_RAID_VOLUME_RL_CONCAT ||
		    level == G_RAID_VOLUME_RL_SINGLE)
			vol->v_mediasize = size * numdisks;
		else if (level == G_RAID_VOLUME_RL_RAID1)
			vol->v_mediasize = size;
		else if (level == G_RAID_VOLUME_RL_RAID3 ||
		    level == G_RAID_VOLUME_RL_RAID5)
			vol->v_mediasize = size * (numdisks - 1);
		else { /* RAID1E */
			vol->v_mediasize = ((size * numdisks) / strip / 2) *
			    strip;
		}
		vol->v_sectorsize = sectorsize;
		g_raid_start_volume(vol);

		/* , and subdisks. */
		for (i = 0; i < numdisks; i++) {
			disk = disks[i];
			sd = &vol->v_subdisks[i];
			sd->sd_disk = disk;
			sd->sd_offset = (off_t)offs[i] * 512;
			sd->sd_size = size;
			if (disk == NULL)
				continue;
			TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);
			g_raid_change_disk_state(disk,
			    G_RAID_DISK_S_ACTIVE);
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_ACTIVE);
			g_raid_event_send(sd, G_RAID_SUBDISK_E_NEW,
			    G_RAID_EVENT_SUBDISK);
		}

		/* Write metadata based on created entities. */
		G_RAID_DEBUG1(0, sc, "Array started.");
		g_raid_md_write_promise(md, vol, NULL, NULL);

		/* Pickup any STALE/SPARE disks to refill array if needed. */
		g_raid_md_promise_refill(sc);

		g_raid_event_send(vol, G_RAID_VOLUME_E_START,
		    G_RAID_EVENT_VOLUME);
		return (0);
	}
	if (strcmp(verb, "add") == 0) {

		gctl_error(req, "`add` command is not applicable, "
		    "use `label` instead.");
		return (-99);
	}
	if (strcmp(verb, "delete") == 0) {

		nodename = gctl_get_asciiparam(req, "arg0");
		if (nodename != NULL && strcasecmp(sc->sc_name, nodename) != 0)
			nodename = NULL;

		/* Full node destruction. */
		if (*nargs == 1 && nodename != NULL) {
			/* Check if some volume is still open. */
			force = gctl_get_paraml(req, "force", sizeof(*force));
			if (force != NULL && *force == 0 &&
			    g_raid_nopens(sc) != 0) {
				gctl_error(req, "Some volume is still open.");
				return (-4);
			}

			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer)
					promise_meta_erase(disk->d_consumer);
			}
			g_raid_destroy_node(sc, 0);
			return (0);
		}

		/* Destroy specified volume. If it was last - all node. */
		if (*nargs > 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		volname = gctl_get_asciiparam(req,
		    nodename != NULL ? "arg1" : "arg0");
		if (volname == NULL) {
			gctl_error(req, "No volume name.");
			return (-2);
		}

		/* Search for volume. */
		TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
			if (strcmp(vol->v_name, volname) == 0)
				break;
			pp = vol->v_provider;
			if (pp == NULL)
				continue;
			if (strcmp(pp->name, volname) == 0)
				break;
			if (strncmp(pp->name, "raid/", 5) == 0 &&
			    strcmp(pp->name + 5, volname) == 0)
				break;
		}
		if (vol == NULL) {
			i = strtol(volname, &tmp, 10);
			if (verb != volname && tmp[0] == 0) {
				TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
					if (vol->v_global_id == i)
						break;
				}
			}
		}
		if (vol == NULL) {
			gctl_error(req, "Volume '%s' not found.", volname);
			return (-3);
		}

		/* Check if volume is still open. */
		force = gctl_get_paraml(req, "force", sizeof(*force));
		if (force != NULL && *force == 0 &&
		    vol->v_provider_open != 0) {
			gctl_error(req, "Volume is still open.");
			return (-4);
		}

		/* Destroy volume and potentially node. */
		i = 0;
		TAILQ_FOREACH(vol1, &sc->sc_volumes, v_next)
			i++;
		if (i >= 2) {
			g_raid_destroy_volume(vol);
			g_raid_md_promise_purge_disks(sc);
			g_raid_md_write_promise(md, NULL, NULL, NULL);
		} else {
			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer)
					promise_meta_erase(disk->d_consumer);
			}
			g_raid_destroy_node(sc, 0);
		}
		return (0);
	}
	if (strcmp(verb, "remove") == 0 ||
	    strcmp(verb, "fail") == 0) {
		if (*nargs < 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		for (i = 1; i < *nargs; i++) {
			snprintf(arg, sizeof(arg), "arg%d", i);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -2;
				break;
			}
			if (strncmp(diskname, "/dev/", 5) == 0)
				diskname += 5;

			TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
				if (disk->d_consumer != NULL && 
				    disk->d_consumer->provider != NULL &&
				    strcmp(disk->d_consumer->provider->name,
				     diskname) == 0)
					break;
			}
			if (disk == NULL) {
				gctl_error(req, "Disk '%s' not found.",
				    diskname);
				error = -3;
				break;
			}

			if (strcmp(verb, "fail") == 0) {
				g_raid_md_fail_disk_promise(md, NULL, disk);
				continue;
			}

			/* Erase metadata on deleting disk and destroy it. */
			promise_meta_erase(disk->d_consumer);
			g_raid_destroy_disk(disk);
		}
		g_raid_md_promise_purge_volumes(sc);

		/* Write updated metadata to remaining disks. */
		g_raid_md_write_promise(md, NULL, NULL, NULL);

		/* Check if anything left. */
		if (g_raid_ndisks(sc, -1) == 0)
			g_raid_destroy_node(sc, 0);
		else
			g_raid_md_promise_refill(sc);
		return (error);
	}
	if (strcmp(verb, "insert") == 0) {
		if (*nargs < 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		for (i = 1; i < *nargs; i++) {
			/* Get disk name. */
			snprintf(arg, sizeof(arg), "arg%d", i);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -3;
				break;
			}

			/* Try to find provider with specified name. */
			g_topology_lock();
			cp = g_raid_open_consumer(sc, diskname);
			if (cp == NULL) {
				gctl_error(req, "Can't open disk '%s'.",
				    diskname);
				g_topology_unlock();
				error = -4;
				break;
			}
			pp = cp->provider;
			g_topology_unlock();

			pd = malloc(sizeof(*pd), M_MD_PROMISE, M_WAITOK | M_ZERO);

			disk = g_raid_create_disk(sc);
			disk->d_consumer = cp;
			disk->d_md_data = (void *)pd;
			cp->private = disk;

			g_raid_get_disk_info(disk);

			/* Welcome the "new" disk. */
			g_raid_change_disk_state(disk, G_RAID_DISK_S_SPARE);
			promise_meta_write_spare(cp);
			g_raid_md_promise_refill(sc);
		}
		return (error);
	}
	return (-100);
}

static int
g_raid_md_write_promise(struct g_raid_md_object *md, struct g_raid_volume *tvol,
    struct g_raid_subdisk *tsd, struct g_raid_disk *tdisk)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_md_promise_pervolume *pv;
	struct promise_raid_conf *meta;
	off_t rebuild_lba64;
	int i, j, pos, rebuild;

	sc = md->mdo_softc;

	if (sc->sc_stopping == G_RAID_DESTROY_HARD)
		return (0);

	/* Generate new per-volume metadata for affected volumes. */
	TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
		if (vol->v_stopping)
			continue;

		/* Skip volumes not related to specified targets. */
		if (tvol != NULL && vol != tvol)
			continue;
		if (tsd != NULL && vol != tsd->sd_volume)
			continue;
		if (tdisk != NULL) {
			for (i = 0; i < vol->v_disks_count; i++) {
				if (vol->v_subdisks[i].sd_disk == tdisk)
					break;
			}
			if (i >= vol->v_disks_count)
				continue;
		}

		pv = (struct g_raid_md_promise_pervolume *)vol->v_md_data;
		pv->pv_generation++;

		meta = malloc(sizeof(*meta), M_MD_PROMISE, M_WAITOK | M_ZERO);
		if (pv->pv_meta != NULL)
			memcpy(meta, pv->pv_meta, sizeof(*meta));
		memcpy(meta->promise_id, PROMISE_MAGIC,
		    sizeof(PROMISE_MAGIC) - 1);
		meta->dummy_0 = 0x00020000;
		meta->integrity = PROMISE_I_VALID;

		meta->generation = pv->pv_generation;
		meta->status = PROMISE_S_VALID | PROMISE_S_ONLINE |
		    PROMISE_S_INITED | PROMISE_S_READY;
		if (vol->v_state <= G_RAID_VOLUME_S_DEGRADED)
			meta->status |= PROMISE_S_DEGRADED;
		if (vol->v_dirty)
			meta->status |= PROMISE_S_MARKED; /* XXX: INVENTED! */
		if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID0 ||
		    vol->v_raid_level == G_RAID_VOLUME_RL_SINGLE)
			meta->type = PROMISE_T_RAID0;
		else if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1 ||
		    vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E)
			meta->type = PROMISE_T_RAID1;
		else if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID3)
			meta->type = PROMISE_T_RAID3;
		else if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID5)
			meta->type = PROMISE_T_RAID5;
		else if (vol->v_raid_level == G_RAID_VOLUME_RL_CONCAT)
			meta->type = PROMISE_T_SPAN;
		else
			meta->type = PROMISE_T_JBOD;
		meta->total_disks = vol->v_disks_count;
		meta->stripe_shift = ffs(vol->v_strip_size / 1024);
		meta->array_width = vol->v_disks_count;
		if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1 ||
		    vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E)
			meta->array_width /= 2;
		meta->array_number = vol->v_global_id;
		meta->total_sectors = vol->v_mediasize / 512;
		meta->total_sectors_high = (vol->v_mediasize / 512) >> 32;
		meta->sector_size = vol->v_sectorsize / 512;
		meta->cylinders = meta->total_sectors / (255 * 63) - 1;
		meta->heads = 254;
		meta->sectors = 63;
		meta->volume_id = pv->pv_id;
		rebuild_lba64 = UINT64_MAX;
		rebuild = 0;
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			/* For RAID0+1 we need to translate order. */
			pos = promise_meta_translate_disk(vol, i);
			meta->disks[pos].flags = PROMISE_F_VALID |
			    PROMISE_F_ASSIGNED;
			if (sd->sd_state == G_RAID_SUBDISK_S_NONE) {
				meta->disks[pos].flags |= 0;
			} else if (sd->sd_state == G_RAID_SUBDISK_S_FAILED) {
				meta->disks[pos].flags |=
				    PROMISE_F_DOWN | PROMISE_F_REDIR;
			} else if (sd->sd_state <= G_RAID_SUBDISK_S_REBUILD) {
				meta->disks[pos].flags |=
				    PROMISE_F_ONLINE | PROMISE_F_REDIR;
				if (sd->sd_state == G_RAID_SUBDISK_S_REBUILD) {
					rebuild_lba64 = MIN(rebuild_lba64,
					    sd->sd_rebuild_pos / 512);
				} else
					rebuild_lba64 = 0;
				rebuild = 1;
			} else {
				meta->disks[pos].flags |= PROMISE_F_ONLINE;
				if (sd->sd_state < G_RAID_SUBDISK_S_ACTIVE) {
					meta->status |= PROMISE_S_MARKED;
					if (sd->sd_state == G_RAID_SUBDISK_S_RESYNC) {
						rebuild_lba64 = MIN(rebuild_lba64,
						    sd->sd_rebuild_pos / 512);
					} else
						rebuild_lba64 = 0;
				}
			}
			if (pv->pv_meta != NULL) {
				meta->disks[pos].id = pv->pv_meta->disks[pos].id;
			} else {
				meta->disks[pos].number = i * 2;
				arc4rand(&meta->disks[pos].id,
				    sizeof(meta->disks[pos].id), 0);
			}
		}
		promise_meta_put_name(meta, vol->v_name);

		/* Try to mimic AMD BIOS rebuild/resync behavior. */
		if (rebuild_lba64 != UINT64_MAX) {
			if (rebuild)
				meta->magic_3 = 0x03040010UL; /* Rebuild? */
			else
				meta->magic_3 = 0x03040008UL; /* Resync? */
			/* Translate from per-disk to per-volume LBA. */
			if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1 ||
			    vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E) {
				rebuild_lba64 *= meta->array_width;
			} else if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID3 ||
			    vol->v_raid_level == G_RAID_VOLUME_RL_RAID5) {
				rebuild_lba64 *= meta->array_width - 1;
			} else
				rebuild_lba64 = 0;
		} else
			meta->magic_3 = 0x03000000UL;
		meta->rebuild_lba64 = rebuild_lba64;
		meta->magic_4 = 0x04010101UL;

		/* Replace per-volume metadata with new. */
		if (pv->pv_meta != NULL)
			free(pv->pv_meta, M_MD_PROMISE);
		pv->pv_meta = meta;

		/* Copy new metadata to the disks, adding or replacing old. */
		for (i = 0; i < vol->v_disks_count; i++) {
			sd = &vol->v_subdisks[i];
			disk = sd->sd_disk;
			if (disk == NULL)
				continue;
			/* For RAID0+1 we need to translate order. */
			pos = promise_meta_translate_disk(vol, i);
			pd = (struct g_raid_md_promise_perdisk *)disk->d_md_data;
			for (j = 0; j < pd->pd_subdisks; j++) {
				if (pd->pd_meta[j]->volume_id == meta->volume_id)
					break;
			}
			if (j == pd->pd_subdisks)
				pd->pd_subdisks++;
			if (pd->pd_meta[j] != NULL)
				free(pd->pd_meta[j], M_MD_PROMISE);
			pd->pd_meta[j] = promise_meta_copy(meta);
			pd->pd_meta[j]->disk = meta->disks[pos];
			pd->pd_meta[j]->disk.number = pos;
			pd->pd_meta[j]->disk_offset_high =
			    (sd->sd_offset / 512) >> 32;
			pd->pd_meta[j]->disk_offset = sd->sd_offset / 512;
			pd->pd_meta[j]->disk_sectors_high =
			    (sd->sd_size / 512) >> 32;
			pd->pd_meta[j]->disk_sectors = sd->sd_size / 512;
			if (sd->sd_state == G_RAID_SUBDISK_S_REBUILD) {
				pd->pd_meta[j]->disk_rebuild_high =
				    (sd->sd_rebuild_pos / 512) >> 32;
				pd->pd_meta[j]->disk_rebuild =
				    sd->sd_rebuild_pos / 512;
			} else if (sd->sd_state < G_RAID_SUBDISK_S_REBUILD) {
				pd->pd_meta[j]->disk_rebuild_high = 0;
				pd->pd_meta[j]->disk_rebuild = 0;
			} else {
				pd->pd_meta[j]->disk_rebuild_high = UINT32_MAX;
				pd->pd_meta[j]->disk_rebuild = UINT32_MAX;
			}
			pd->pd_updated = 1;
		}
	}

	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_promise_perdisk *)disk->d_md_data;
		if (disk->d_state != G_RAID_DISK_S_ACTIVE)
			continue;
		if (!pd->pd_updated)
			continue;
		G_RAID_DEBUG(1, "Writing Promise metadata to %s",
		    g_raid_get_diskname(disk));
		for (i = 0; i < pd->pd_subdisks; i++)
			g_raid_md_promise_print(pd->pd_meta[i]);
		promise_meta_write(disk->d_consumer,
		    pd->pd_meta, pd->pd_subdisks);
		pd->pd_updated = 0;
	}

	return (0);
}

static int
g_raid_md_fail_disk_promise(struct g_raid_md_object *md,
    struct g_raid_subdisk *tsd, struct g_raid_disk *tdisk)
{
	struct g_raid_softc *sc;
	struct g_raid_md_promise_perdisk *pd;
	struct g_raid_subdisk *sd;
	int i, pos;

	sc = md->mdo_softc;
	pd = (struct g_raid_md_promise_perdisk *)tdisk->d_md_data;

	/* We can't fail disk that is not a part of array now. */
	if (tdisk->d_state != G_RAID_DISK_S_ACTIVE)
		return (-1);

	/*
	 * Mark disk as failed in metadata and try to write that metadata
	 * to the disk itself to prevent it's later resurrection as STALE.
	 */
	if (pd->pd_subdisks > 0 && tdisk->d_consumer != NULL)
		G_RAID_DEBUG(1, "Writing Promise metadata to %s",
		    g_raid_get_diskname(tdisk));
	for (i = 0; i < pd->pd_subdisks; i++) {
		pd->pd_meta[i]->disk.flags |=
		    PROMISE_F_DOWN | PROMISE_F_REDIR;
		pos = pd->pd_meta[i]->disk.number;
		if (pos >= 0 && pos < PROMISE_MAX_DISKS) {
			pd->pd_meta[i]->disks[pos].flags |=
			    PROMISE_F_DOWN | PROMISE_F_REDIR;
		}
		g_raid_md_promise_print(pd->pd_meta[i]);
	}
	if (tdisk->d_consumer != NULL)
		promise_meta_write(tdisk->d_consumer,
		    pd->pd_meta, pd->pd_subdisks);

	/* Change states. */
	g_raid_change_disk_state(tdisk, G_RAID_DISK_S_FAILED);
	TAILQ_FOREACH(sd, &tdisk->d_subdisks, sd_next) {
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_FAILED);
		g_raid_event_send(sd, G_RAID_SUBDISK_E_FAILED,
		    G_RAID_EVENT_SUBDISK);
	}

	/* Write updated metadata to remaining disks. */
	g_raid_md_write_promise(md, NULL, NULL, tdisk);

	g_raid_md_promise_refill(sc);
	return (0);
}

static int
g_raid_md_free_disk_promise(struct g_raid_md_object *md,
    struct g_raid_disk *disk)
{
	struct g_raid_md_promise_perdisk *pd;
	int i;

	pd = (struct g_raid_md_promise_perdisk *)disk->d_md_data;
	for (i = 0; i < pd->pd_subdisks; i++) {
		if (pd->pd_meta[i] != NULL) {
			free(pd->pd_meta[i], M_MD_PROMISE);
			pd->pd_meta[i] = NULL;
		}
	}
	free(pd, M_MD_PROMISE);
	disk->d_md_data = NULL;
	return (0);
}

static int
g_raid_md_free_volume_promise(struct g_raid_md_object *md,
    struct g_raid_volume *vol)
{
	struct g_raid_md_promise_pervolume *pv;

	pv = (struct g_raid_md_promise_pervolume *)vol->v_md_data;
	if (pv && pv->pv_meta != NULL) {
		free(pv->pv_meta, M_MD_PROMISE);
		pv->pv_meta = NULL;
	}
	if (pv && !pv->pv_started) {
		pv->pv_started = 1;
		callout_stop(&pv->pv_start_co);
	}
	free(pv, M_MD_PROMISE);
	vol->v_md_data = NULL;
	return (0);
}

static int
g_raid_md_free_promise(struct g_raid_md_object *md)
{

	return (0);
}

G_RAID_MD_DECLARE(promise, "Promise");
