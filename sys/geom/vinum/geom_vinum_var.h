/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
 * Copyright (c) 1997, 1998, 1999
 *      Nan Yang Computer Services Limited.  All rights reserved.
 *
 * Parts copyright (c) 1997, 1998 Cybernet Corporation, NetMAX project.
 * Parts written by Greg Lehey.
 *
 *  This software is distributed under the so-called ``Berkeley
 *  License'':                                                                    *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Nan Yang Computer
 *      Services Limited.
 * 4. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *  
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any               * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even if
 * advised of the possibility of such damage.
 *  
 * $FreeBSD$
 */

#ifndef	_GEOM_VINUM_VAR_H_
#define	_GEOM_VINUM_VAR_H_

/*
 * Slice header
 *
 * Vinum drives start with this structure:
 *
 *\                                            Sector
 * |--------------------------------------|
 * |   PDP-11 memorial boot block         |      0
 * |--------------------------------------|
 * |   Disk label, maybe                  |      1
 * |--------------------------------------|
 * |   Slice definition  (vinum_hdr)      |      8
 * |--------------------------------------|
 * |                                      |
 * |   Configuration info, first copy     |      9
 * |                                      |
 * |--------------------------------------|
 * |                                      |
 * |   Configuration info, second copy    |      9 + size of config
 * |                                      |
 * |--------------------------------------|
 */

/* Sizes and offsets of our information. */
#define	GV_HDR_OFFSET	4096	/* Offset of vinum header. */
#define	GV_HDR_LEN	512	/* Size of vinum header. */
#define	GV_CFG_OFFSET	4608	/* Offset of first config copy. */
#define	GV_CFG_LEN	65536	/* Size of config copy. */

/* This is where the actual data starts. */
#define	GV_DATA_START	(GV_CFG_LEN * 2 + GV_CFG_OFFSET)
/* #define GV_DATA_START	(GV_CFG_LEN * 2 + GV_HDR_LEN) */

#define	GV_MAXDRIVENAME	32	/* Maximum length of a device name. */
#define	GV_MAXSDNAME	64	/* Maximum length of a subdisk name. */
#define	GV_MAXPLEXNAME	64	/* Maximum length of a plex name. */
#define	GV_MAXVOLNAME	64	/* Maximum length of a volume name. */

/* Command line flags. */
#define	GV_FLAG_R	0x01
#define	GV_FLAG_S	0x02
#define	GV_FLAG_V	0x04
#define	GV_FLAG_VV	0x08
#define	GV_FLAG_F	0x10

/* Object types. */
#define	GV_TYPE_VOL	1
#define	GV_TYPE_PLEX	2
#define	GV_TYPE_SD	3
#define	GV_TYPE_DRIVE	4

/* State changing flags. */
#define	GV_SETSTATE_FORCE	0x1
#define	GV_SETSTATE_CONFIG	0x2

/* Subdisk state bitmaps for plexes. */
#define	GV_SD_DOWNSTATE		0x01	/* Subdisk is down. */
#define	GV_SD_STALESTATE	0x02	/* Subdisk is stale. */
#define	GV_SD_INITSTATE		0x04	/* Subdisk is initializing. */
#define	GV_SD_UPSTATE		0x08	/* Subdisk is up. */

/* Synchronization/initialization request sizes. */
#define	GV_MIN_SYNCSIZE		512
#define	GV_MAX_SYNCSIZE		MAXPHYS
#define	GV_DFLT_SYNCSIZE	65536

/* Flags for BIOs, as they are processed within vinum. */
#define	GV_BIO_GROW	0x01
#define	GV_BIO_MALLOC	0x02
#define	GV_BIO_ONHOLD	0x04
#define	GV_BIO_SYNCREQ	0x08
#define	GV_BIO_INIT	0x10
#define	GV_BIO_REBUILD	0x20
#define	GV_BIO_CHECK	0x40
#define	GV_BIO_PARITY	0x80
#define GV_BIO_INTERNAL \
    (GV_BIO_SYNCREQ | GV_BIO_INIT | GV_BIO_REBUILD | GV_BIO_CHECK | GV_BIO_GROW)

/* Error codes to be used within gvinum. */
#define	GV_ERR_SETSTATE		(-1)	/* Error setting state. */
#define	GV_ERR_BADSIZE		(-2)	/* Object has wrong size. */
#define	GV_ERR_INVTYPE		(-3)    /* Invalid object type. */
#define	GV_ERR_CREATE		(-4)	/* Error creating gvinum object. */
#define	GV_ERR_ISBUSY		(-5)	/* Object is busy. */
#define	GV_ERR_ISATTACHED	(-6)	/* Object is attached to another. */
#define	GV_ERR_INVFLAG		(-7)	/* Invalid flag passed. */
#define	GV_ERR_INVSTATE		(-8)	/* Invalid state. */
#define	GV_ERR_NOTFOUND		(-9)	/* Object not found. */
#define	GV_ERR_NAMETAKEN	(-10)	/* Object name is taken. */
#define	GV_ERR_NOSPACE		(-11)	/* No space left on drive/subdisk. */
#define	GV_ERR_BADOFFSET	(-12)	/* Invalid offset specified. */
#define	GV_ERR_INVNAME		(-13)	/* Invalid object name. */
#define	GV_ERR_PLEXORG		(-14)	/* Invalid plex organization. */

/*
 * hostname is 256 bytes long, but we don't need to shlep multiple copies in
 * vinum.  We use the host name just to identify this system, and 32 bytes
 * should be ample for that purpose.
 */

#define	GV_HOSTNAME_LEN	32
struct gv_label {
	char	sysname[GV_HOSTNAME_LEN]; /* System name at creation time. */
	char	name[GV_MAXDRIVENAME];	/* Our name of the drive. */
	struct timeval	date_of_birth;	/* The time it was created ... */
	struct timeval	last_update;	/* ... and the time of last update. */
	off_t		drive_size;	/* Total size incl. headers. */
};

/* The 'header' of each valid vinum drive. */
struct gv_hdr {
	uint64_t	magic;
#define GV_OLD_MAGIC	0x494E2056494E4F00LL
#define GV_OLD_NOMAGIC	0x4E4F2056494E4F00LL
#define GV_MAGIC	0x56494E554D2D3100LL
#define GV_NOMAGIC	0x56494E554D2D2D00LL

	uint64_t	config_length;
	struct gv_label	label;
};

/* A single freelist entry of a drive. */
struct gv_freelist {
	off_t size;				/* Size of this free slot. */
	off_t offset;				/* Offset on the drive. */
	LIST_ENTRY(gv_freelist) freelist;
};

/*
 * Since we share structures between userland and kernel, we need this helper
 * struct instead of struct bio_queue_head and friends.  Maybe I find a proper
 * solution some day.
 */
struct gv_bioq {
	struct bio *bp;
	TAILQ_ENTRY(gv_bioq)	queue;
};

#define	GV_EVENT_DRIVE_TASTED		1
#define	GV_EVENT_DRIVE_LOST		2
#define	GV_EVENT_THREAD_EXIT		3
#define	GV_EVENT_CREATE_DRIVE		4
#define	GV_EVENT_CREATE_VOLUME		5
#define	GV_EVENT_CREATE_PLEX		6
#define	GV_EVENT_CREATE_SD		7
#define	GV_EVENT_SAVE_CONFIG		8
#define	GV_EVENT_RM_VOLUME		9
#define	GV_EVENT_RM_PLEX		10
#define	GV_EVENT_RM_SD			11
#define	GV_EVENT_RM_DRIVE		12
#define	GV_EVENT_SET_SD_STATE		13
#define	GV_EVENT_SET_DRIVE_STATE	14
#define	GV_EVENT_SET_VOL_STATE		15
#define	GV_EVENT_SET_PLEX_STATE		16
#define	GV_EVENT_RESET_CONFIG		17
#define	GV_EVENT_PARITY_REBUILD		18
#define	GV_EVENT_PARITY_CHECK		19
#define	GV_EVENT_START_PLEX		20
#define	GV_EVENT_START_VOLUME		21
#define	GV_EVENT_ATTACH_PLEX		22
#define	GV_EVENT_ATTACH_SD		23
#define	GV_EVENT_DETACH_PLEX		24
#define	GV_EVENT_DETACH_SD		25
#define	GV_EVENT_RENAME_VOL		26
#define	GV_EVENT_RENAME_PLEX		27
#define	GV_EVENT_RENAME_SD		28
#define	GV_EVENT_RENAME_DRIVE		29
#define	GV_EVENT_MOVE_SD		30
#define	GV_EVENT_SETUP_OBJECTS		31

#ifdef _KERNEL
struct gv_event {
	int	type;
	void	*arg1;
	void	*arg2;
	intmax_t arg3;
	intmax_t arg4;
	TAILQ_ENTRY(gv_event)	events;
};

/* This struct contains the main vinum config. */
struct gv_softc {
	/* Linked lists of all objects in our setup. */
	LIST_HEAD(,gv_drive)	drives;		/* All drives. */
	LIST_HEAD(,gv_plex)	plexes;		/* All plexes. */
	LIST_HEAD(,gv_sd)	subdisks;	/* All subdisks. */
	LIST_HEAD(,gv_volume)	volumes;	/* All volumes. */

	TAILQ_HEAD(,gv_event)	equeue;		/* Event queue. */
	struct mtx		equeue_mtx;	/* Event queue lock. */
	struct mtx		bqueue_mtx;	/* BIO queue lock. */
	struct mtx		config_mtx;	/* Configuration lock. */
	struct bio_queue_head	*bqueue_down;	/* BIO queue incoming
						   requests. */
	struct bio_queue_head	*bqueue_up;	/* BIO queue for completed
						   requests. */
	struct g_geom		*geom;		/* Pointer to our VINUM geom. */
	struct proc		*worker;	/* Worker process. */
};
#endif

/* softc for a drive. */
struct gv_drive {
	char	name[GV_MAXDRIVENAME];		/* The name of this drive. */
	char	device[GV_MAXDRIVENAME];	/* Associated device. */
	int	state;				/* The state of this drive. */
#define	GV_DRIVE_DOWN	0
#define	GV_DRIVE_UP	1

	off_t	size;				/* Size of this drive. */
	off_t	avail;				/* Available space. */
	int	sdcount;			/* Number of subdisks. */

	int	flags;
#define	GV_DRIVE_REFERENCED	0x01	/* The drive isn't really existing,
					   but was referenced by a subdisk
					   during taste. */

	struct gv_hdr	*hdr;		/* The drive header. */

	struct g_consumer *consumer;	/* Consumer attached to this drive. */

	int freelist_entries;			/* Count of freelist entries. */
	LIST_HEAD(,gv_freelist)	freelist;	/* List of freelist entries. */
	LIST_HEAD(,gv_sd)	subdisks;	/* Subdisks on this drive. */
	LIST_ENTRY(gv_drive)	drive;		/* Entry in the vinum config. */

	struct gv_softc	*vinumconf;		/* Pointer to the vinum conf. */
};

/* softc for a subdisk. */
struct gv_sd {
	char	name[GV_MAXSDNAME];	/* The name of this subdisk. */
	off_t	size;			/* The size of this subdisk. */
	off_t	drive_offset;		/* Offset in the underlying drive. */
	off_t	plex_offset;		/* Offset in the associated plex. */
	int	state;			/* The state of this subdisk. */
#define	GV_SD_DOWN		0
#define	GV_SD_STALE		1
#define	GV_SD_INITIALIZING	2
#define	GV_SD_REVIVING		3
#define	GV_SD_UP		4

	off_t	initialized;		/* Count of initialized bytes. */

	int	init_size;		/* Initialization read/write size. */
	int	init_error;		/* Flag error on initialization. */

	int	flags;
#define	GV_SD_NEWBORN		0x01	/* Subdisk is created by user. */
#define	GV_SD_TASTED		0x02	/* Subdisk is created during taste. */
#define	GV_SD_CANGOUP		0x04	/* Subdisk can go up immediately. */
#define GV_SD_GROW		0x08	/* Subdisk is added to striped plex. */

	char drive[GV_MAXDRIVENAME];	/* Name of underlying drive. */
	char plex[GV_MAXPLEXNAME];	/* Name of associated plex. */

	struct gv_drive	*drive_sc;	/* Pointer to underlying drive. */
	struct gv_plex	*plex_sc;	/* Pointer to associated plex. */

	LIST_ENTRY(gv_sd) from_drive;	/* Subdisk list of underlying drive. */
	LIST_ENTRY(gv_sd) in_plex;	/* Subdisk list of associated plex. */
	LIST_ENTRY(gv_sd) sd;		/* Entry in the vinum config. */

	struct gv_softc	*vinumconf;	/* Pointer to the vinum config. */
};

/* softc for a plex. */
struct gv_plex {
	char	name[GV_MAXPLEXNAME];	/* The name of the plex. */
	off_t	size;			/* The size of the plex. */
	int	state;			/* The plex state. */
#define	GV_PLEX_DOWN		0
#define	GV_PLEX_INITIALIZING	1
#define	GV_PLEX_DEGRADED	2
#define GV_PLEX_GROWABLE	3
#define	GV_PLEX_UP		4

	int	org;			/* The plex organisation. */
#define	GV_PLEX_DISORG	0
#define	GV_PLEX_CONCAT	1
#define	GV_PLEX_STRIPED	2
#define	GV_PLEX_RAID5	4

	int	stripesize;		/* The stripe size of the plex. */

	char	volume[GV_MAXVOLNAME];	/* Name of associated volume. */
	struct gv_volume *vol_sc;	/* Pointer to associated volume. */

	int	sddetached;		/* Number of detached subdisks. */
	int	sdcount;		/* Number of subdisks in this plex. */
	int	sddown;			/* Number of subdisks that are down. */
	int	flags;
#define	GV_PLEX_ADDED		0x01	/* Added to an existing volume. */
#define	GV_PLEX_SYNCING		0x02	/* Plex is syncing from another plex. */
#define	GV_PLEX_NEWBORN		0x20	/* The plex was just created. */
#define GV_PLEX_REBUILDING	0x40	/* The plex is rebuilding. */
#define GV_PLEX_GROWING		0x80	/* The plex is growing. */

	off_t	synced;			/* Count of synced bytes. */

	TAILQ_HEAD(,gv_raid5_packet)	packets; /* RAID5 sub-requests. */

	LIST_HEAD(,gv_sd)   subdisks;	/* List of attached subdisks. */
	LIST_ENTRY(gv_plex) in_volume;	/* Plex list of associated volume. */
	LIST_ENTRY(gv_plex) plex;	/* Entry in the vinum config. */

#ifdef	_KERNEL
	struct bio_queue_head	*bqueue;	/* BIO queue. */
	struct bio_queue_head	*wqueue;	/* Waiting BIO queue. */
	struct bio_queue_head	*rqueue;	/* Rebuild waiting BIO queue. */
#else
	char			*bpad, *wpad, *rpad; /* Padding for userland. */
#endif

	struct gv_softc	*vinumconf;	/* Pointer to the vinum config. */
};

/* softc for a volume. */
struct gv_volume {
	char	name[GV_MAXVOLNAME];	/* The name of the volume. */
	off_t	size;			/* The size of the volume. */
	int	plexcount;		/* Number of plexes. */
	int	state;			/* The state of the volume. */
#define	GV_VOL_DOWN	0
#define	GV_VOL_UP	1

	int	flags;
#define GV_VOL_NEWBORN		0x08	/* The volume was just created. */

	LIST_HEAD(,gv_plex)	plexes;		/* List of attached plexes. */
	LIST_ENTRY(gv_volume)	volume;		/* Entry in vinum config. */

	struct g_provider	*provider;	/* Provider of this volume. */

#ifdef	_KERNEL
	struct bio_queue_head	*wqueue;	/* BIO delayed request queue. */
#else
	char			*wpad; /* Padding for userland. */
#endif

	struct gv_plex	*last_read_plex;
	struct gv_softc	*vinumconf;	/* Pointer to the vinum config. */
};

#endif /* !_GEOM_VINUM_VAR_H */
