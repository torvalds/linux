/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/taskqueue.h>
#include <geom/geom.h>
#include "geom/raid/g_raid.h"
#include "g_raid_md_if.h"

static MALLOC_DEFINE(M_MD_JMICRON, "md_jmicron_data", "GEOM_RAID JMicron metadata");

#define	JMICRON_MAX_DISKS	8
#define	JMICRON_MAX_SPARE	2

struct jmicron_raid_conf {
    u_int8_t		signature[2];
#define	JMICRON_MAGIC		"JM"

    u_int16_t		version;
#define	JMICRON_VERSION		0x0001

    u_int16_t		checksum;
    u_int8_t		filler_1[10];
    u_int32_t		disk_id;
    u_int32_t		offset;
    u_int32_t		disk_sectors_high;
    u_int16_t		disk_sectors_low;
    u_int8_t		filler_2[2];
    u_int8_t		name[16];
    u_int8_t		type;
#define	JMICRON_T_RAID0		0
#define	JMICRON_T_RAID1		1
#define	JMICRON_T_RAID01	2
#define	JMICRON_T_CONCAT	3
#define	JMICRON_T_RAID5		5

    u_int8_t		stripe_shift;
    u_int16_t		flags;
#define	JMICRON_F_READY		0x0001
#define	JMICRON_F_BOOTABLE	0x0002
#define	JMICRON_F_BADSEC	0x0004
#define	JMICRON_F_ACTIVE	0x0010
#define	JMICRON_F_UNSYNC	0x0020
#define	JMICRON_F_NEWEST	0x0040

    u_int8_t		filler_3[4];
    u_int32_t		spare[JMICRON_MAX_SPARE];
    u_int32_t		disks[JMICRON_MAX_DISKS];
#define	JMICRON_DISK_MASK	0xFFFFFFF0
#define	JMICRON_SEG_MASK	0x0000000F
    u_int8_t		filler_4[32];
    u_int8_t		filler_5[384];
};

struct g_raid_md_jmicron_perdisk {
	struct jmicron_raid_conf	*pd_meta;
	int				 pd_disk_pos;
	int				 pd_disk_id;
	off_t				 pd_disk_size;
};

struct g_raid_md_jmicron_object {
	struct g_raid_md_object	 mdio_base;
	uint32_t		 mdio_config_id;
	struct jmicron_raid_conf	*mdio_meta;
	struct callout		 mdio_start_co;	/* STARTING state timer. */
	int			 mdio_total_disks;
	int			 mdio_disks_present;
	int			 mdio_started;
	int			 mdio_incomplete;
	struct root_hold_token	*mdio_rootmount; /* Root mount delay token. */
};

static g_raid_md_create_t g_raid_md_create_jmicron;
static g_raid_md_taste_t g_raid_md_taste_jmicron;
static g_raid_md_event_t g_raid_md_event_jmicron;
static g_raid_md_ctl_t g_raid_md_ctl_jmicron;
static g_raid_md_write_t g_raid_md_write_jmicron;
static g_raid_md_fail_disk_t g_raid_md_fail_disk_jmicron;
static g_raid_md_free_disk_t g_raid_md_free_disk_jmicron;
static g_raid_md_free_t g_raid_md_free_jmicron;

static kobj_method_t g_raid_md_jmicron_methods[] = {
	KOBJMETHOD(g_raid_md_create,	g_raid_md_create_jmicron),
	KOBJMETHOD(g_raid_md_taste,	g_raid_md_taste_jmicron),
	KOBJMETHOD(g_raid_md_event,	g_raid_md_event_jmicron),
	KOBJMETHOD(g_raid_md_ctl,	g_raid_md_ctl_jmicron),
	KOBJMETHOD(g_raid_md_write,	g_raid_md_write_jmicron),
	KOBJMETHOD(g_raid_md_fail_disk,	g_raid_md_fail_disk_jmicron),
	KOBJMETHOD(g_raid_md_free_disk,	g_raid_md_free_disk_jmicron),
	KOBJMETHOD(g_raid_md_free,	g_raid_md_free_jmicron),
	{ 0, 0 }
};

static struct g_raid_md_class g_raid_md_jmicron_class = {
	"JMicron",
	g_raid_md_jmicron_methods,
	sizeof(struct g_raid_md_jmicron_object),
	.mdc_enable = 1,
	.mdc_priority = 100
};

static void
g_raid_md_jmicron_print(struct jmicron_raid_conf *meta)
{
	int k;

	if (g_raid_debug < 1)
		return;

	printf("********* ATA JMicron RAID Metadata *********\n");
	printf("signature           <%c%c>\n", meta->signature[0], meta->signature[1]);
	printf("version             %04x\n", meta->version);
	printf("checksum            0x%04x\n", meta->checksum);
	printf("disk_id             0x%08x\n", meta->disk_id);
	printf("offset              0x%08x\n", meta->offset);
	printf("disk_sectors_high   0x%08x\n", meta->disk_sectors_high);
	printf("disk_sectors_low    0x%04x\n", meta->disk_sectors_low);
	printf("name                <%.16s>\n", meta->name);
	printf("type                %d\n", meta->type);
	printf("stripe_shift        %d\n", meta->stripe_shift);
	printf("flags               %04x\n", meta->flags);
	printf("spare              ");
	for (k = 0; k < JMICRON_MAX_SPARE; k++)
		printf(" 0x%08x", meta->spare[k]);
	printf("\n");
	printf("disks              ");
	for (k = 0; k < JMICRON_MAX_DISKS; k++)
		printf(" 0x%08x", meta->disks[k]);
	printf("\n");
	printf("=================================================\n");
}

static struct jmicron_raid_conf *
jmicron_meta_copy(struct jmicron_raid_conf *meta)
{
	struct jmicron_raid_conf *nmeta;

	nmeta = malloc(sizeof(*meta), M_MD_JMICRON, M_WAITOK);
	memcpy(nmeta, meta, sizeof(*meta));
	return (nmeta);
}

static int
jmicron_meta_total_disks(struct jmicron_raid_conf *meta)
{
	int pos;

	for (pos = 0; pos < JMICRON_MAX_DISKS; pos++) {
		if (meta->disks[pos] == 0)
			break;
	}
	return (pos);
}

static int
jmicron_meta_total_spare(struct jmicron_raid_conf *meta)
{
	int pos, n;

	n = 0;
	for (pos = 0; pos < JMICRON_MAX_SPARE; pos++) {
		if (meta->spare[pos] != 0)
			n++;
	}
	return (n);
}

/*
 * Generate fake Configuration ID based on disk IDs.
 * Note: it will change after each disk set change.
 */
static uint32_t
jmicron_meta_config_id(struct jmicron_raid_conf *meta)
{
	int pos;
	uint32_t config_id;

	config_id = 0;
	for (pos = 0; pos < JMICRON_MAX_DISKS; pos++)
		config_id += meta->disks[pos] << pos;
	return (config_id);
}

static void
jmicron_meta_get_name(struct jmicron_raid_conf *meta, char *buf)
{
	int i;

	strncpy(buf, meta->name, 16);
	buf[16] = 0;
	for (i = 15; i >= 0; i--) {
		if (buf[i] > 0x20)
			break;
		buf[i] = 0;
	}
}

static void
jmicron_meta_put_name(struct jmicron_raid_conf *meta, char *buf)
{

	memset(meta->name, 0x20, 16);
	memcpy(meta->name, buf, MIN(strlen(buf), 16));
}

static int
jmicron_meta_find_disk(struct jmicron_raid_conf *meta, uint32_t id)
{
	int pos;

	id &= JMICRON_DISK_MASK;
	for (pos = 0; pos < JMICRON_MAX_DISKS; pos++) {
		if ((meta->disks[pos] & JMICRON_DISK_MASK) == id)
			return (pos);
	}
	for (pos = 0; pos < JMICRON_MAX_SPARE; pos++) {
		if ((meta->spare[pos] & JMICRON_DISK_MASK) == id)
			return (-3);
	}
	return (-1);
}

static struct jmicron_raid_conf *
jmicron_meta_read(struct g_consumer *cp)
{
	struct g_provider *pp;
	struct jmicron_raid_conf *meta;
	char *buf;
	int error, i;
	uint16_t checksum, *ptr;

	pp = cp->provider;

	/* Read the anchor sector. */
	buf = g_read_data(cp,
	    pp->mediasize - pp->sectorsize, pp->sectorsize, &error);
	if (buf == NULL) {
		G_RAID_DEBUG(1, "Cannot read metadata from %s (error=%d).",
		    pp->name, error);
		return (NULL);
	}
	meta = (struct jmicron_raid_conf *)buf;

	/* Check if this is an JMicron RAID struct */
	if (strncmp(meta->signature, JMICRON_MAGIC, strlen(JMICRON_MAGIC))) {
		G_RAID_DEBUG(1, "JMicron signature check failed on %s", pp->name);
		g_free(buf);
		return (NULL);
	}
	meta = malloc(sizeof(*meta), M_MD_JMICRON, M_WAITOK);
	memcpy(meta, buf, min(sizeof(*meta), pp->sectorsize));
	g_free(buf);

	/* Check metadata checksum. */
	for (checksum = 0, ptr = (uint16_t *)meta, i = 0; i < 64; i++)
		checksum += *ptr++;
	if (checksum != 0) {
		G_RAID_DEBUG(1, "JMicron checksum check failed on %s", pp->name);
		free(meta, M_MD_JMICRON);
		return (NULL);
	}

	return (meta);
}

static int
jmicron_meta_write(struct g_consumer *cp, struct jmicron_raid_conf *meta)
{
	struct g_provider *pp;
	char *buf;
	int error, i;
	uint16_t checksum, *ptr;

	pp = cp->provider;

	/* Recalculate checksum for case if metadata were changed. */
	meta->checksum = 0;
	for (checksum = 0, ptr = (uint16_t *)meta, i = 0; i < 64; i++)
		checksum += *ptr++;
	meta->checksum -= checksum;

	/* Create and fill buffer. */
	buf = malloc(pp->sectorsize, M_MD_JMICRON, M_WAITOK | M_ZERO);
	memcpy(buf, meta, sizeof(*meta));

	error = g_write_data(cp,
	    pp->mediasize - pp->sectorsize, buf, pp->sectorsize);
	if (error != 0) {
		G_RAID_DEBUG(1, "Cannot write metadata to %s (error=%d).",
		    pp->name, error);
	}

	free(buf, M_MD_JMICRON);
	return (error);
}

static int
jmicron_meta_erase(struct g_consumer *cp)
{
	struct g_provider *pp;
	char *buf;
	int error;

	pp = cp->provider;
	buf = malloc(pp->sectorsize, M_MD_JMICRON, M_WAITOK | M_ZERO);
	error = g_write_data(cp,
	    pp->mediasize - pp->sectorsize, buf, pp->sectorsize);
	if (error != 0) {
		G_RAID_DEBUG(1, "Cannot erase metadata on %s (error=%d).",
		    pp->name, error);
	}
	free(buf, M_MD_JMICRON);
	return (error);
}

static struct g_raid_disk *
g_raid_md_jmicron_get_disk(struct g_raid_softc *sc, int id)
{
	struct g_raid_disk	*disk;
	struct g_raid_md_jmicron_perdisk *pd;

	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
		if (pd->pd_disk_pos == id)
			break;
	}
	return (disk);
}

static int
g_raid_md_jmicron_supported(int level, int qual, int disks, int force)
{

	if (disks > 8)
		return (0);
	switch (level) {
	case G_RAID_VOLUME_RL_RAID0:
		if (disks < 1)
			return (0);
		if (!force && (disks < 2 || disks > 6))
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
		if (!force && (disks != 4))
			return (0);
		break;
	case G_RAID_VOLUME_RL_SINGLE:
		if (disks != 1)
			return (0);
		if (!force)
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
		if (!force)
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
g_raid_md_jmicron_start_disk(struct g_raid_disk *disk)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd, *tmpsd;
	struct g_raid_disk *olddisk, *tmpdisk;
	struct g_raid_md_object *md;
	struct g_raid_md_jmicron_object *mdi;
	struct g_raid_md_jmicron_perdisk *pd, *oldpd;
	struct jmicron_raid_conf *meta;
	int disk_pos, resurrection = 0;

	sc = disk->d_softc;
	md = sc->sc_md;
	mdi = (struct g_raid_md_jmicron_object *)md;
	meta = mdi->mdio_meta;
	pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
	olddisk = NULL;

	/* Find disk position in metadata by its serial. */
	if (pd->pd_meta != NULL)
		disk_pos = jmicron_meta_find_disk(meta, pd->pd_disk_id);
	else
		disk_pos = -1;
	if (disk_pos < 0) {
		G_RAID_DEBUG1(1, sc, "Unknown, probably new or stale disk");
		/* If we are in the start process, that's all for now. */
		if (!mdi->mdio_started)
			goto nofit;
		/*
		 * If we have already started - try to get use of the disk.
		 * Try to replace OFFLINE disks first, then FAILED.
		 */
		TAILQ_FOREACH(tmpdisk, &sc->sc_disks, d_next) {
			if (tmpdisk->d_state != G_RAID_DISK_S_OFFLINE &&
			    tmpdisk->d_state != G_RAID_DISK_S_FAILED)
				continue;
			/* Make sure this disk is big enough. */
			TAILQ_FOREACH(sd, &tmpdisk->d_subdisks, sd_next) {
				if (sd->sd_offset + sd->sd_size + 512 >
				    pd->pd_disk_size) {
					G_RAID_DEBUG1(1, sc,
					    "Disk too small (%ju < %ju)",
					    pd->pd_disk_size,
					    sd->sd_offset + sd->sd_size + 512);
					break;
				}
			}
			if (sd != NULL)
				continue;
			if (tmpdisk->d_state == G_RAID_DISK_S_OFFLINE) {
				olddisk = tmpdisk;
				break;
			} else if (olddisk == NULL)
				olddisk = tmpdisk;
		}
		if (olddisk == NULL) {
nofit:
			if (disk_pos == -3 || pd->pd_disk_pos == -3) {
				g_raid_change_disk_state(disk,
				    G_RAID_DISK_S_SPARE);
				return (1);
			} else {
				g_raid_change_disk_state(disk,
				    G_RAID_DISK_S_STALE);
				return (0);
			}
		}
		oldpd = (struct g_raid_md_jmicron_perdisk *)olddisk->d_md_data;
		disk_pos = oldpd->pd_disk_pos;
		resurrection = 1;
	}

	if (olddisk == NULL) {
		/* Find placeholder by position. */
		olddisk = g_raid_md_jmicron_get_disk(sc, disk_pos);
		if (olddisk == NULL)
			panic("No disk at position %d!", disk_pos);
		if (olddisk->d_state != G_RAID_DISK_S_OFFLINE) {
			G_RAID_DEBUG1(1, sc, "More than one disk for pos %d",
			    disk_pos);
			g_raid_change_disk_state(disk, G_RAID_DISK_S_STALE);
			return (0);
		}
		oldpd = (struct g_raid_md_jmicron_perdisk *)olddisk->d_md_data;
	}

	/* Replace failed disk or placeholder with new disk. */
	TAILQ_FOREACH_SAFE(sd, &olddisk->d_subdisks, sd_next, tmpsd) {
		TAILQ_REMOVE(&olddisk->d_subdisks, sd, sd_next);
		TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);
		sd->sd_disk = disk;
	}
	oldpd->pd_disk_pos = -2;
	pd->pd_disk_pos = disk_pos;
	/* Update global metadata just in case. */
	meta->disks[disk_pos] = pd->pd_disk_id;

	/* If it was placeholder -- destroy it. */
	if (olddisk->d_state == G_RAID_DISK_S_OFFLINE) {
		g_raid_destroy_disk(olddisk);
	} else {
		/* Otherwise, make it STALE_FAILED. */
		g_raid_change_disk_state(olddisk, G_RAID_DISK_S_STALE_FAILED);
	}

	/* Welcome the new disk. */
	g_raid_change_disk_state(disk, G_RAID_DISK_S_ACTIVE);
	TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {

		/*
		 * Different disks may have different sizes/offsets,
		 * especially in concat mode. Update.
		 */
		if (!resurrection) {
			sd->sd_offset =
			    (off_t)pd->pd_meta->offset * 16 * 512; //ZZZ
			sd->sd_size =
			    (((off_t)pd->pd_meta->disk_sectors_high << 16) +
			      pd->pd_meta->disk_sectors_low) * 512;
		}

		if (resurrection) {
			/* Stale disk, almost same as new. */
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_NEW);
		} else if ((meta->flags & JMICRON_F_BADSEC) != 0 &&
		    (pd->pd_meta->flags & JMICRON_F_BADSEC) == 0) {
			/* Cold-inserted or rebuilding disk. */
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_NEW);
		} else if (pd->pd_meta->flags & JMICRON_F_UNSYNC) {
			/* Dirty or resyncing disk.. */
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_STALE);
		} else {
			/* Up to date disk. */
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_ACTIVE);
		}
		g_raid_event_send(sd, G_RAID_SUBDISK_E_NEW,
		    G_RAID_EVENT_SUBDISK);
	}

	/* Update status of our need for spare. */
	if (mdi->mdio_started) {
		mdi->mdio_incomplete =
		    (g_raid_ndisks(sc, G_RAID_DISK_S_ACTIVE) <
		     mdi->mdio_total_disks);
	}

	return (resurrection);
}

static void
g_disk_md_jmicron_retaste(void *arg, int pending)
{

	G_RAID_DEBUG(1, "Array is not complete, trying to retaste.");
	g_retaste(&g_raid_class);
	free(arg, M_MD_JMICRON);
}

static void
g_raid_md_jmicron_refill(struct g_raid_softc *sc)
{
	struct g_raid_md_object *md;
	struct g_raid_md_jmicron_object *mdi;
	struct g_raid_disk *disk;
	struct task *task;
	int update, na;

	md = sc->sc_md;
	mdi = (struct g_raid_md_jmicron_object *)md;
	update = 0;
	do {
		/* Make sure we miss anything. */
		na = g_raid_ndisks(sc, G_RAID_DISK_S_ACTIVE);
		if (na == mdi->mdio_total_disks)
			break;

		G_RAID_DEBUG1(1, md->mdo_softc,
		    "Array is not complete (%d of %d), "
		    "trying to refill.", na, mdi->mdio_total_disks);

		/* Try to get use some of STALE disks. */
		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			if (disk->d_state == G_RAID_DISK_S_STALE) {
				update += g_raid_md_jmicron_start_disk(disk);
				if (disk->d_state == G_RAID_DISK_S_ACTIVE)
					break;
			}
		}
		if (disk != NULL)
			continue;

		/* Try to get use some of SPARE disks. */
		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			if (disk->d_state == G_RAID_DISK_S_SPARE) {
				update += g_raid_md_jmicron_start_disk(disk);
				if (disk->d_state == G_RAID_DISK_S_ACTIVE)
					break;
			}
		}
	} while (disk != NULL);

	/* Write new metadata if we changed something. */
	if (update)
		g_raid_md_write_jmicron(md, NULL, NULL, NULL);

	/* Update status of our need for spare. */
	mdi->mdio_incomplete = (g_raid_ndisks(sc, G_RAID_DISK_S_ACTIVE) <
	    mdi->mdio_total_disks);

	/* Request retaste hoping to find spare. */
	if (mdi->mdio_incomplete) {
		task = malloc(sizeof(struct task),
		    M_MD_JMICRON, M_WAITOK | M_ZERO);
		TASK_INIT(task, 0, g_disk_md_jmicron_retaste, task);
		taskqueue_enqueue(taskqueue_swi, task);
	}
}

static void
g_raid_md_jmicron_start(struct g_raid_softc *sc)
{
	struct g_raid_md_object *md;
	struct g_raid_md_jmicron_object *mdi;
	struct g_raid_md_jmicron_perdisk *pd;
	struct jmicron_raid_conf *meta;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	off_t size;
	int j, disk_pos;
	char buf[17];

	md = sc->sc_md;
	mdi = (struct g_raid_md_jmicron_object *)md;
	meta = mdi->mdio_meta;

	/* Create volumes and subdisks. */
	jmicron_meta_get_name(meta, buf);
	vol = g_raid_create_volume(sc, buf, -1);
	size = ((off_t)meta->disk_sectors_high << 16) + meta->disk_sectors_low;
	size *= 512; //ZZZ
	vol->v_raid_level_qualifier = G_RAID_VOLUME_RLQ_NONE;
	if (meta->type == JMICRON_T_RAID0) {
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID0;
		vol->v_mediasize = size * mdi->mdio_total_disks;
	} else if (meta->type == JMICRON_T_RAID1) {
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID1;
		vol->v_mediasize = size;
	} else if (meta->type == JMICRON_T_RAID01) {
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID1E;
		vol->v_mediasize = size * mdi->mdio_total_disks / 2;
	} else if (meta->type == JMICRON_T_CONCAT) {
		if (mdi->mdio_total_disks == 1)
			vol->v_raid_level = G_RAID_VOLUME_RL_SINGLE;
		else
			vol->v_raid_level = G_RAID_VOLUME_RL_CONCAT;
		vol->v_mediasize = 0;
	} else if (meta->type == JMICRON_T_RAID5) {
		vol->v_raid_level = G_RAID_VOLUME_RL_RAID5;
		vol->v_raid_level_qualifier = G_RAID_VOLUME_RLQ_R5LA;
		vol->v_mediasize = size * (mdi->mdio_total_disks - 1);
	} else {
		vol->v_raid_level = G_RAID_VOLUME_RL_UNKNOWN;
		vol->v_mediasize = 0;
	}
	vol->v_strip_size = 1024 << meta->stripe_shift; //ZZZ
	vol->v_disks_count = mdi->mdio_total_disks;
	vol->v_sectorsize = 512; //ZZZ
	for (j = 0; j < vol->v_disks_count; j++) {
		sd = &vol->v_subdisks[j];
		sd->sd_offset = (off_t)meta->offset * 16 * 512; //ZZZ
		sd->sd_size = size;
	}
	g_raid_start_volume(vol);

	/* Create disk placeholders to store data for later writing. */
	for (disk_pos = 0; disk_pos < mdi->mdio_total_disks; disk_pos++) {
		pd = malloc(sizeof(*pd), M_MD_JMICRON, M_WAITOK | M_ZERO);
		pd->pd_disk_pos = disk_pos;
		pd->pd_disk_id = meta->disks[disk_pos];
		disk = g_raid_create_disk(sc);
		disk->d_md_data = (void *)pd;
		disk->d_state = G_RAID_DISK_S_OFFLINE;
		sd = &vol->v_subdisks[disk_pos];
		sd->sd_disk = disk;
		TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);
	}

	/* Make all disks found till the moment take their places. */
	do {
		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			if (disk->d_state == G_RAID_DISK_S_NONE) {
				g_raid_md_jmicron_start_disk(disk);
				break;
			}
		}
	} while (disk != NULL);

	mdi->mdio_started = 1;
	G_RAID_DEBUG1(0, sc, "Array started.");
	g_raid_md_write_jmicron(md, NULL, NULL, NULL);

	/* Pickup any STALE/SPARE disks to refill array if needed. */
	g_raid_md_jmicron_refill(sc);

	g_raid_event_send(vol, G_RAID_VOLUME_E_START, G_RAID_EVENT_VOLUME);

	callout_stop(&mdi->mdio_start_co);
	G_RAID_DEBUG1(1, sc, "root_mount_rel %p", mdi->mdio_rootmount);
	root_mount_rel(mdi->mdio_rootmount);
	mdi->mdio_rootmount = NULL;
}

static void
g_raid_md_jmicron_new_disk(struct g_raid_disk *disk)
{
	struct g_raid_softc *sc;
	struct g_raid_md_object *md;
	struct g_raid_md_jmicron_object *mdi;
	struct jmicron_raid_conf *pdmeta;
	struct g_raid_md_jmicron_perdisk *pd;

	sc = disk->d_softc;
	md = sc->sc_md;
	mdi = (struct g_raid_md_jmicron_object *)md;
	pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
	pdmeta = pd->pd_meta;

	if (mdi->mdio_started) {
		if (g_raid_md_jmicron_start_disk(disk))
			g_raid_md_write_jmicron(md, NULL, NULL, NULL);
	} else {
		/*
		 * If we haven't started yet - update common metadata
		 * to get subdisks details, avoiding data from spare disks.
		 */
		if (mdi->mdio_meta == NULL ||
		    jmicron_meta_find_disk(mdi->mdio_meta,
		     mdi->mdio_meta->disk_id) == -3) {
			if (mdi->mdio_meta != NULL)
				free(mdi->mdio_meta, M_MD_JMICRON);
			mdi->mdio_meta = jmicron_meta_copy(pdmeta);
			mdi->mdio_total_disks = jmicron_meta_total_disks(pdmeta);
		}
		mdi->mdio_meta->flags |= pdmeta->flags & JMICRON_F_BADSEC;

		mdi->mdio_disks_present++;
		G_RAID_DEBUG1(1, sc, "Matching disk (%d of %d+%d up)",
		    mdi->mdio_disks_present,
		    mdi->mdio_total_disks,
		    jmicron_meta_total_spare(mdi->mdio_meta));

		/* If we collected all needed disks - start array. */
		if (mdi->mdio_disks_present == mdi->mdio_total_disks +
		    jmicron_meta_total_spare(mdi->mdio_meta))
			g_raid_md_jmicron_start(sc);
	}
}

static void
g_raid_jmicron_go(void *arg)
{
	struct g_raid_softc *sc;
	struct g_raid_md_object *md;
	struct g_raid_md_jmicron_object *mdi;

	sc = arg;
	md = sc->sc_md;
	mdi = (struct g_raid_md_jmicron_object *)md;
	if (!mdi->mdio_started) {
		G_RAID_DEBUG1(0, sc, "Force array start due to timeout.");
		g_raid_event_send(sc, G_RAID_NODE_E_START, 0);
	}
}

static int
g_raid_md_create_jmicron(struct g_raid_md_object *md, struct g_class *mp,
    struct g_geom **gp)
{
	struct g_raid_softc *sc;
	struct g_raid_md_jmicron_object *mdi;
	char name[16];

	mdi = (struct g_raid_md_jmicron_object *)md;
	mdi->mdio_config_id = arc4random();
	snprintf(name, sizeof(name), "JMicron-%08x", mdi->mdio_config_id);
	sc = g_raid_create_node(mp, name, md);
	if (sc == NULL)
		return (G_RAID_MD_TASTE_FAIL);
	md->mdo_softc = sc;
	*gp = sc->sc_geom;
	return (G_RAID_MD_TASTE_NEW);
}

static int
g_raid_md_taste_jmicron(struct g_raid_md_object *md, struct g_class *mp,
                              struct g_consumer *cp, struct g_geom **gp)
{
	struct g_consumer *rcp;
	struct g_provider *pp;
	struct g_raid_md_jmicron_object *mdi, *mdi1;
	struct g_raid_softc *sc;
	struct g_raid_disk *disk;
	struct jmicron_raid_conf *meta;
	struct g_raid_md_jmicron_perdisk *pd;
	struct g_geom *geom;
	int disk_pos, result, spare, len;
	char name[16];
	uint16_t vendor;

	G_RAID_DEBUG(1, "Tasting JMicron on %s", cp->provider->name);
	mdi = (struct g_raid_md_jmicron_object *)md;
	pp = cp->provider;

	/* Read metadata from device. */
	meta = NULL;
	g_topology_unlock();
	vendor = 0xffff;
	len = sizeof(vendor);
	if (pp->geom->rank == 1)
		g_io_getattr("GEOM::hba_vendor", cp, &len, &vendor);
	meta = jmicron_meta_read(cp);
	g_topology_lock();
	if (meta == NULL) {
		if (g_raid_aggressive_spare) {
			if (vendor == 0x197b) {
				G_RAID_DEBUG(1,
				    "No JMicron metadata, forcing spare.");
				spare = 2;
				goto search;
			} else {
				G_RAID_DEBUG(1,
				    "JMicron vendor mismatch 0x%04x != 0x197b",
				    vendor);
			}
		}
		return (G_RAID_MD_TASTE_FAIL);
	}

	/* Check this disk position in obtained metadata. */
	disk_pos = jmicron_meta_find_disk(meta, meta->disk_id);
	if (disk_pos == -1) {
		G_RAID_DEBUG(1, "JMicron disk_id %08x not found",
		    meta->disk_id);
		goto fail1;
	}

	/* Metadata valid. Print it. */
	g_raid_md_jmicron_print(meta);
	G_RAID_DEBUG(1, "JMicron disk position %d", disk_pos);
	spare = (disk_pos == -2) ? 1 : 0;

search:
	/* Search for matching node. */
	sc = NULL;
	mdi1 = NULL;
	LIST_FOREACH(geom, &mp->geom, geom) {
		sc = geom->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		if (sc->sc_md->mdo_class != md->mdo_class)
			continue;
		mdi1 = (struct g_raid_md_jmicron_object *)sc->sc_md;
		if (spare == 2) {
			if (mdi1->mdio_incomplete)
				break;
		} else {
			if (mdi1->mdio_config_id ==
			    jmicron_meta_config_id(meta))
				break;
		}
	}

	/* Found matching node. */
	if (geom != NULL) {
		G_RAID_DEBUG(1, "Found matching array %s", sc->sc_name);
		result = G_RAID_MD_TASTE_EXISTING;

	} else if (spare) { /* Not found needy node -- left for later. */
		G_RAID_DEBUG(1, "Spare is not needed at this time");
		goto fail1;

	} else { /* Not found matching node -- create one. */
		result = G_RAID_MD_TASTE_NEW;
		mdi->mdio_config_id = jmicron_meta_config_id(meta);
		snprintf(name, sizeof(name), "JMicron-%08x",
		    mdi->mdio_config_id);
		sc = g_raid_create_node(mp, name, md);
		md->mdo_softc = sc;
		geom = sc->sc_geom;
		callout_init(&mdi->mdio_start_co, 1);
		callout_reset(&mdi->mdio_start_co, g_raid_start_timeout * hz,
		    g_raid_jmicron_go, sc);
		mdi->mdio_rootmount = root_mount_hold("GRAID-JMicron");
		G_RAID_DEBUG1(1, sc, "root_mount_hold %p", mdi->mdio_rootmount);
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

	pd = malloc(sizeof(*pd), M_MD_JMICRON, M_WAITOK | M_ZERO);
	pd->pd_meta = meta;
	if (spare == 2) {
		pd->pd_disk_pos = -3;
		pd->pd_disk_id = arc4random() & JMICRON_DISK_MASK;
	} else {
		pd->pd_disk_pos = -1;
		pd->pd_disk_id = meta->disk_id;
	}
	pd->pd_disk_size = pp->mediasize;
	disk = g_raid_create_disk(sc);
	disk->d_md_data = (void *)pd;
	disk->d_consumer = rcp;
	rcp->private = disk;

	g_raid_get_disk_info(disk);

	g_raid_md_jmicron_new_disk(disk);

	sx_xunlock(&sc->sc_lock);
	g_topology_lock();
	*gp = geom;
	return (result);
fail1:
	free(meta, M_MD_JMICRON);
	return (G_RAID_MD_TASTE_FAIL);
}

static int
g_raid_md_event_jmicron(struct g_raid_md_object *md,
    struct g_raid_disk *disk, u_int event)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd;
	struct g_raid_md_jmicron_object *mdi;
	struct g_raid_md_jmicron_perdisk *pd;

	sc = md->mdo_softc;
	mdi = (struct g_raid_md_jmicron_object *)md;
	if (disk == NULL) {
		switch (event) {
		case G_RAID_NODE_E_START:
			if (!mdi->mdio_started)
				g_raid_md_jmicron_start(sc);
			return (0);
		}
		return (-1);
	}
	pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
	switch (event) {
	case G_RAID_DISK_E_DISCONNECTED:
		/* If disk was assigned, just update statuses. */
		if (pd->pd_disk_pos >= 0) {
			g_raid_change_disk_state(disk, G_RAID_DISK_S_OFFLINE);
			if (disk->d_consumer) {
				g_raid_kill_consumer(sc, disk->d_consumer);
				disk->d_consumer = NULL;
			}
			TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {
				g_raid_change_subdisk_state(sd,
				    G_RAID_SUBDISK_S_NONE);
				g_raid_event_send(sd, G_RAID_SUBDISK_E_DISCONNECTED,
				    G_RAID_EVENT_SUBDISK);
			}
		} else {
			/* Otherwise -- delete. */
			g_raid_change_disk_state(disk, G_RAID_DISK_S_NONE);
			g_raid_destroy_disk(disk);
		}

		/* Write updated metadata to all disks. */
		g_raid_md_write_jmicron(md, NULL, NULL, NULL);

		/* Check if anything left except placeholders. */
		if (g_raid_ndisks(sc, -1) ==
		    g_raid_ndisks(sc, G_RAID_DISK_S_OFFLINE))
			g_raid_destroy_node(sc, 0);
		else
			g_raid_md_jmicron_refill(sc);
		return (0);
	}
	return (-2);
}

static int
g_raid_md_ctl_jmicron(struct g_raid_md_object *md,
    struct gctl_req *req)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_jmicron_object *mdi;
	struct g_raid_md_jmicron_perdisk *pd;
	struct g_consumer *cp;
	struct g_provider *pp;
	char arg[16];
	const char *verb, *volname, *levelname, *diskname;
	int *nargs, *force;
	off_t size, sectorsize, strip;
	intmax_t *sizearg, *striparg;
	int numdisks, i, len, level, qual, update;
	int error;

	sc = md->mdo_softc;
	mdi = (struct g_raid_md_jmicron_object *)md;
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
		if (!g_raid_md_jmicron_supported(level, qual, numdisks,
		    force ? *force : 0)) {
			gctl_error(req, "Unsupported RAID level "
			    "(0x%02x/0x%02x), or number of disks (%d).",
			    level, qual, numdisks);
			return (-5);
		}

		/* Search for disks, connect them and probe. */
		size = 0x7fffffffffffffffllu;
		sectorsize = 0;
		for (i = 0; i < numdisks; i++) {
			snprintf(arg, sizeof(arg), "arg%d", i + 3);
			diskname = gctl_get_asciiparam(req, arg);
			if (diskname == NULL) {
				gctl_error(req, "No disk name (%s).", arg);
				error = -6;
				break;
			}
			if (strcmp(diskname, "NONE") == 0) {
				cp = NULL;
				pp = NULL;
			} else {
				g_topology_lock();
				cp = g_raid_open_consumer(sc, diskname);
				if (cp == NULL) {
					gctl_error(req, "Can't open '%s'.",
					    diskname);
					g_topology_unlock();
					error = -7;
					break;
				}
				pp = cp->provider;
			}
			pd = malloc(sizeof(*pd), M_MD_JMICRON, M_WAITOK | M_ZERO);
			pd->pd_disk_pos = i;
			pd->pd_disk_id = arc4random() & JMICRON_DISK_MASK;
			disk = g_raid_create_disk(sc);
			disk->d_md_data = (void *)pd;
			disk->d_consumer = cp;
			if (cp == NULL)
				continue;
			cp->private = disk;
			g_topology_unlock();

			g_raid_get_disk_info(disk);

			pd->pd_disk_size = pp->mediasize;
			if (size > pp->mediasize)
				size = pp->mediasize;
			if (sectorsize < pp->sectorsize)
				sectorsize = pp->sectorsize;
		}
		if (error != 0)
			return (error);

		if (sectorsize <= 0) {
			gctl_error(req, "Can't get sector size.");
			return (-8);
		}

		/* Reserve space for metadata. */
		size -= sectorsize;

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
			if (strip > 65535 * sectorsize) {
				gctl_error(req, "Strip size too big.");
				return (-12);
			}
			strip = *striparg;
		}

		/* Round size down to strip or sector. */
		if (level == G_RAID_VOLUME_RL_RAID1)
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
		if (size > 0xffffffffffffllu * sectorsize) {
			gctl_error(req, "Size too big.");
			return (-14);
		}

		/* We have all we need, create things: volume, ... */
		mdi->mdio_total_disks = numdisks;
		mdi->mdio_started = 1;
		vol = g_raid_create_volume(sc, volname, -1);
		vol->v_md_data = (void *)(intptr_t)0;
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
		else if (level == G_RAID_VOLUME_RL_RAID5)
			vol->v_mediasize = size * (numdisks - 1);
		else { /* RAID1E */
			vol->v_mediasize = ((size * numdisks) / strip / 2) *
			    strip;
		}
		vol->v_sectorsize = sectorsize;
		g_raid_start_volume(vol);

		/* , and subdisks. */
		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
			sd = &vol->v_subdisks[pd->pd_disk_pos];
			sd->sd_disk = disk;
			sd->sd_offset = 0;
			sd->sd_size = size;
			TAILQ_INSERT_TAIL(&disk->d_subdisks, sd, sd_next);
			if (sd->sd_disk->d_consumer != NULL) {
				g_raid_change_disk_state(disk,
				    G_RAID_DISK_S_ACTIVE);
				g_raid_change_subdisk_state(sd,
				    G_RAID_SUBDISK_S_ACTIVE);
				g_raid_event_send(sd, G_RAID_SUBDISK_E_NEW,
				    G_RAID_EVENT_SUBDISK);
			} else {
				g_raid_change_disk_state(disk, G_RAID_DISK_S_OFFLINE);
			}
		}

		/* Write metadata based on created entities. */
		G_RAID_DEBUG1(0, sc, "Array started.");
		g_raid_md_write_jmicron(md, NULL, NULL, NULL);

		/* Pickup any STALE/SPARE disks to refill array if needed. */
		g_raid_md_jmicron_refill(sc);

		g_raid_event_send(vol, G_RAID_VOLUME_E_START,
		    G_RAID_EVENT_VOLUME);
		return (0);
	}
	if (strcmp(verb, "delete") == 0) {

		/* Check if some volume is still open. */
		force = gctl_get_paraml(req, "force", sizeof(*force));
		if (force != NULL && *force == 0 &&
		    g_raid_nopens(sc) != 0) {
			gctl_error(req, "Some volume is still open.");
			return (-4);
		}

		TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
			if (disk->d_consumer)
				jmicron_meta_erase(disk->d_consumer);
		}
		g_raid_destroy_node(sc, 0);
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
				g_raid_md_fail_disk_jmicron(md, NULL, disk);
				continue;
			}

			pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;

			/* Erase metadata on deleting disk. */
			jmicron_meta_erase(disk->d_consumer);

			/* If disk was assigned, just update statuses. */
			if (pd->pd_disk_pos >= 0) {
				g_raid_change_disk_state(disk, G_RAID_DISK_S_OFFLINE);
				g_raid_kill_consumer(sc, disk->d_consumer);
				disk->d_consumer = NULL;
				TAILQ_FOREACH(sd, &disk->d_subdisks, sd_next) {
					g_raid_change_subdisk_state(sd,
					    G_RAID_SUBDISK_S_NONE);
					g_raid_event_send(sd, G_RAID_SUBDISK_E_DISCONNECTED,
					    G_RAID_EVENT_SUBDISK);
				}
			} else {
				/* Otherwise -- delete. */
				g_raid_change_disk_state(disk, G_RAID_DISK_S_NONE);
				g_raid_destroy_disk(disk);
			}
		}

		/* Write updated metadata to remaining disks. */
		g_raid_md_write_jmicron(md, NULL, NULL, NULL);

		/* Check if anything left except placeholders. */
		if (g_raid_ndisks(sc, -1) ==
		    g_raid_ndisks(sc, G_RAID_DISK_S_OFFLINE))
			g_raid_destroy_node(sc, 0);
		else
			g_raid_md_jmicron_refill(sc);
		return (error);
	}
	if (strcmp(verb, "insert") == 0) {
		if (*nargs < 2) {
			gctl_error(req, "Invalid number of arguments.");
			return (-1);
		}
		update = 0;
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

			pd = malloc(sizeof(*pd), M_MD_JMICRON, M_WAITOK | M_ZERO);
			pd->pd_disk_pos = -3;
			pd->pd_disk_id = arc4random() & JMICRON_DISK_MASK;
			pd->pd_disk_size = pp->mediasize;

			disk = g_raid_create_disk(sc);
			disk->d_consumer = cp;
			disk->d_md_data = (void *)pd;
			cp->private = disk;
			g_topology_unlock();

			g_raid_get_disk_info(disk);

			/* Welcome the "new" disk. */
			update += g_raid_md_jmicron_start_disk(disk);
			if (disk->d_state != G_RAID_DISK_S_ACTIVE &&
			    disk->d_state != G_RAID_DISK_S_SPARE) {
				gctl_error(req, "Disk '%s' doesn't fit.",
				    diskname);
				g_raid_destroy_disk(disk);
				error = -8;
				break;
			}
		}

		/* Write new metadata if we changed something. */
		if (update)
			g_raid_md_write_jmicron(md, NULL, NULL, NULL);
		return (error);
	}
	gctl_error(req, "Command '%s' is not supported.", verb);
	return (-100);
}

static int
g_raid_md_write_jmicron(struct g_raid_md_object *md, struct g_raid_volume *tvol,
    struct g_raid_subdisk *tsd, struct g_raid_disk *tdisk)
{
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
	struct g_raid_md_jmicron_object *mdi;
	struct g_raid_md_jmicron_perdisk *pd;
	struct jmicron_raid_conf *meta;
	int i, spares;

	sc = md->mdo_softc;
	mdi = (struct g_raid_md_jmicron_object *)md;

	if (sc->sc_stopping == G_RAID_DESTROY_HARD)
		return (0);

	/* There is only one volume. */
	vol = TAILQ_FIRST(&sc->sc_volumes);

	/* Fill global fields. */
	meta = malloc(sizeof(*meta), M_MD_JMICRON, M_WAITOK | M_ZERO);
	strncpy(meta->signature, JMICRON_MAGIC, 2);
	meta->version = JMICRON_VERSION;
	jmicron_meta_put_name(meta, vol->v_name);
	if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID0)
		meta->type = JMICRON_T_RAID0;
	else if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1)
		meta->type = JMICRON_T_RAID1;
	else if (vol->v_raid_level == G_RAID_VOLUME_RL_RAID1E)
		meta->type = JMICRON_T_RAID01;
	else if (vol->v_raid_level == G_RAID_VOLUME_RL_CONCAT ||
	    vol->v_raid_level == G_RAID_VOLUME_RL_SINGLE)
		meta->type = JMICRON_T_CONCAT;
	else
		meta->type = JMICRON_T_RAID5;
	meta->stripe_shift = fls(vol->v_strip_size / 2048);
	meta->flags = JMICRON_F_READY | JMICRON_F_BOOTABLE;
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		if (sd->sd_disk == NULL || sd->sd_disk->d_md_data == NULL)
			meta->disks[i] = 0xffffffff;
		else {
			pd = (struct g_raid_md_jmicron_perdisk *)
			    sd->sd_disk->d_md_data;
			meta->disks[i] = pd->pd_disk_id;
		}
		if (sd->sd_state < G_RAID_SUBDISK_S_STALE)
			meta->flags |= JMICRON_F_BADSEC;
		if (vol->v_dirty)
			meta->flags |= JMICRON_F_UNSYNC;
	}

	/* Put spares to their slots. */
	spares = 0;
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
		if (disk->d_state != G_RAID_DISK_S_SPARE)
			continue;
		meta->spare[spares] = pd->pd_disk_id;
		if (++spares >= 2)
			break;
	}

	/* We are done. Print meta data and store them to disks. */
	if (mdi->mdio_meta != NULL)
		free(mdi->mdio_meta, M_MD_JMICRON);
	mdi->mdio_meta = meta;
	TAILQ_FOREACH(disk, &sc->sc_disks, d_next) {
		pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
		if (disk->d_state != G_RAID_DISK_S_ACTIVE &&
		    disk->d_state != G_RAID_DISK_S_SPARE)
			continue;
		if (pd->pd_meta != NULL) {
			free(pd->pd_meta, M_MD_JMICRON);
			pd->pd_meta = NULL;
		}
		pd->pd_meta = jmicron_meta_copy(meta);
		pd->pd_meta->disk_id = pd->pd_disk_id;
		if ((sd = TAILQ_FIRST(&disk->d_subdisks)) != NULL) {
			pd->pd_meta->offset =
			    (sd->sd_offset / 512) / 16;
			pd->pd_meta->disk_sectors_high =
			    (sd->sd_size / 512) >> 16;
			pd->pd_meta->disk_sectors_low =
			    (sd->sd_size / 512) & 0xffff;
			if (sd->sd_state < G_RAID_SUBDISK_S_STALE)
				pd->pd_meta->flags &= ~JMICRON_F_BADSEC;
			else if (sd->sd_state < G_RAID_SUBDISK_S_ACTIVE)
				pd->pd_meta->flags |= JMICRON_F_UNSYNC;
		}
		G_RAID_DEBUG(1, "Writing JMicron metadata to %s",
		    g_raid_get_diskname(disk));
		g_raid_md_jmicron_print(pd->pd_meta);
		jmicron_meta_write(disk->d_consumer, pd->pd_meta);
	}
	return (0);
}

static int
g_raid_md_fail_disk_jmicron(struct g_raid_md_object *md,
    struct g_raid_subdisk *tsd, struct g_raid_disk *tdisk)
{
	struct g_raid_softc *sc;
	struct g_raid_md_jmicron_perdisk *pd;
	struct g_raid_subdisk *sd;

	sc = md->mdo_softc;
	pd = (struct g_raid_md_jmicron_perdisk *)tdisk->d_md_data;

	/* We can't fail disk that is not a part of array now. */
	if (pd->pd_disk_pos < 0)
		return (-1);

	if (tdisk->d_consumer)
		jmicron_meta_erase(tdisk->d_consumer);

	/* Change states. */
	g_raid_change_disk_state(tdisk, G_RAID_DISK_S_FAILED);
	TAILQ_FOREACH(sd, &tdisk->d_subdisks, sd_next) {
		g_raid_change_subdisk_state(sd,
		    G_RAID_SUBDISK_S_FAILED);
		g_raid_event_send(sd, G_RAID_SUBDISK_E_FAILED,
		    G_RAID_EVENT_SUBDISK);
	}

	/* Write updated metadata to remaining disks. */
	g_raid_md_write_jmicron(md, NULL, NULL, tdisk);

	/* Check if anything left except placeholders. */
	if (g_raid_ndisks(sc, -1) ==
	    g_raid_ndisks(sc, G_RAID_DISK_S_OFFLINE))
		g_raid_destroy_node(sc, 0);
	else
		g_raid_md_jmicron_refill(sc);
	return (0);
}

static int
g_raid_md_free_disk_jmicron(struct g_raid_md_object *md,
    struct g_raid_disk *disk)
{
	struct g_raid_md_jmicron_perdisk *pd;

	pd = (struct g_raid_md_jmicron_perdisk *)disk->d_md_data;
	if (pd->pd_meta != NULL) {
		free(pd->pd_meta, M_MD_JMICRON);
		pd->pd_meta = NULL;
	}
	free(pd, M_MD_JMICRON);
	disk->d_md_data = NULL;
	return (0);
}

static int
g_raid_md_free_jmicron(struct g_raid_md_object *md)
{
	struct g_raid_md_jmicron_object *mdi;

	mdi = (struct g_raid_md_jmicron_object *)md;
	if (!mdi->mdio_started) {
		mdi->mdio_started = 0;
		callout_stop(&mdi->mdio_start_co);
		G_RAID_DEBUG1(1, md->mdo_softc,
		    "root_mount_rel %p", mdi->mdio_rootmount);
		root_mount_rel(mdi->mdio_rootmount);
		mdi->mdio_rootmount = NULL;
	}
	if (mdi->mdio_meta != NULL) {
		free(mdi->mdio_meta, M_MD_JMICRON);
		mdi->mdio_meta = NULL;
	}
	return (0);
}

G_RAID_MD_DECLARE(jmicron, "JMicron");
