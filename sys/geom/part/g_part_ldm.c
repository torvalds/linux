/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/diskmbr.h>
#include <sys/endian.h>
#include <sys/gpt.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/uuid.h>
#include <geom/geom.h>
#include <geom/part/g_part.h>

#include "g_part_if.h"

FEATURE(geom_part_ldm, "GEOM partitioning class for LDM support");

SYSCTL_DECL(_kern_geom_part);
static SYSCTL_NODE(_kern_geom_part, OID_AUTO, ldm, CTLFLAG_RW, 0,
    "GEOM_PART_LDM Logical Disk Manager");

static u_int ldm_debug = 0;
SYSCTL_UINT(_kern_geom_part_ldm, OID_AUTO, debug,
    CTLFLAG_RWTUN, &ldm_debug, 0, "Debug level");

/*
 * This allows access to mirrored LDM volumes. Since we do not
 * doing mirroring here, it is not enabled by default.
 */
static u_int show_mirrors = 0;
SYSCTL_UINT(_kern_geom_part_ldm, OID_AUTO, show_mirrors,
    CTLFLAG_RWTUN, &show_mirrors, 0, "Show mirrored volumes");

#define	LDM_DEBUG(lvl, fmt, ...)	do {				\
	if (ldm_debug >= (lvl)) {					\
		printf("GEOM_PART: " fmt "\n", __VA_ARGS__);		\
	}								\
} while (0)
#define	LDM_DUMP(buf, size)	do {					\
	if (ldm_debug > 1) {						\
		hexdump(buf, size, NULL, 0);				\
	}								\
} while (0)

/*
 * There are internal representations of LDM structures.
 *
 * We do not keep all fields of on-disk structures, only most useful.
 * All numbers in an on-disk structures are in big-endian format.
 */

/*
 * Private header is 512 bytes long. There are three copies on each disk.
 * Offset and sizes are in sectors. Location of each copy:
 * - the first offset is relative to the disk start;
 * - the second and third offset are relative to the LDM database start.
 *
 * On a disk partitioned with GPT, the LDM has not first private header.
 */
#define	LDM_PH_MBRINDEX		0
#define	LDM_PH_GPTINDEX		2
static const uint64_t	ldm_ph_off[] = {6, 1856, 2047};
#define	LDM_VERSION_2K		0x2000b
#define	LDM_VERSION_VISTA	0x2000c
#define	LDM_PH_VERSION_OFF	0x00c
#define	LDM_PH_DISKGUID_OFF	0x030
#define	LDM_PH_DGGUID_OFF	0x0b0
#define	LDM_PH_DGNAME_OFF	0x0f0
#define	LDM_PH_START_OFF	0x11b
#define	LDM_PH_SIZE_OFF		0x123
#define	LDM_PH_DB_OFF		0x12b
#define	LDM_PH_DBSIZE_OFF	0x133
#define	LDM_PH_TH1_OFF		0x13b
#define	LDM_PH_TH2_OFF		0x143
#define	LDM_PH_CONFSIZE_OFF	0x153
#define	LDM_PH_LOGSIZE_OFF	0x15b
#define	LDM_PH_SIGN		"PRIVHEAD"
struct ldm_privhdr {
	struct uuid	disk_guid;
	struct uuid	dg_guid;
	u_char		dg_name[32];
	uint64_t	start;		/* logical disk start */
	uint64_t	size;		/* logical disk size */
	uint64_t	db_offset;	/* LDM database start */
#define	LDM_DB_SIZE		2048
	uint64_t	db_size;	/* LDM database size */
#define	LDM_TH_COUNT		2
	uint64_t	th_offset[LDM_TH_COUNT]; /* TOC header offsets */
	uint64_t	conf_size;	/* configuration size */
	uint64_t	log_size;	/* size of log */
};

/*
 * Table of contents header is 512 bytes long.
 * There are two identical copies at offsets from the private header.
 * Offsets are relative to the LDM database start.
 */
#define	LDM_TH_SIGN		"TOCBLOCK"
#define	LDM_TH_NAME1		"config"
#define	LDM_TH_NAME2		"log"
#define	LDM_TH_NAME1_OFF	0x024
#define	LDM_TH_CONF_OFF		0x02e
#define	LDM_TH_CONFSIZE_OFF	0x036
#define	LDM_TH_NAME2_OFF	0x046
#define	LDM_TH_LOG_OFF		0x050
#define	LDM_TH_LOGSIZE_OFF	0x058
struct ldm_tochdr {
	uint64_t	conf_offset;	/* configuration offset */
	uint64_t	log_offset;	/* log offset */
};

/*
 * LDM database header is 512 bytes long.
 */
#define	LDM_VMDB_SIGN		"VMDB"
#define	LDM_DB_LASTSEQ_OFF	0x004
#define	LDM_DB_SIZE_OFF		0x008
#define	LDM_DB_STATUS_OFF	0x010
#define	LDM_DB_VERSION_OFF	0x012
#define	LDM_DB_DGNAME_OFF	0x016
#define	LDM_DB_DGGUID_OFF	0x035
struct ldm_vmdbhdr {
	uint32_t	last_seq;	/* sequence number of last VBLK */
	uint32_t	size;		/* size of VBLK */
};

/*
 * The LDM database configuration section contains VMDB header and
 * many VBLKs. Each VBLK represents a disk group, disk partition,
 * component or volume.
 *
 * The most interesting for us are volumes, they are represents
 * partitions in the GEOM_PART meaning. But volume VBLK does not
 * contain all information needed to create GEOM provider. And we
 * should get this information from the related VBLK. This is how
 * VBLK releated:
 *	Volumes <- Components <- Partitions -> Disks
 *
 * One volume can contain several components. In this case LDM
 * does mirroring of volume data to each component.
 *
 * Also each component can contain several partitions (spanned or
 * striped volumes).
 */

struct ldm_component {
	uint64_t	id;		/* object id */
	uint64_t	vol_id;		/* parent volume object id */

	int		count;
	LIST_HEAD(, ldm_partition) partitions;
	LIST_ENTRY(ldm_component) entry;
};

struct ldm_volume {
	uint64_t	id;		/* object id */
	uint64_t	size;		/* volume size */
	uint8_t		number;		/* used for ordering */
	uint8_t		part_type;	/* partition type */

	int		count;
	LIST_HEAD(, ldm_component) components;
	LIST_ENTRY(ldm_volume)	entry;
};

struct ldm_disk {
	uint64_t	id;		/* object id */
	struct uuid	guid;		/* disk guid */

	LIST_ENTRY(ldm_disk) entry;
};

#if 0
struct ldm_disk_group {
	uint64_t	id;		/* object id */
	struct uuid	guid;		/* disk group guid */
	u_char		name[32];	/* disk group name */

	LIST_ENTRY(ldm_disk_group) entry;
};
#endif

struct ldm_partition {
	uint64_t	id;		/* object id */
	uint64_t	disk_id;	/* disk object id */
	uint64_t	comp_id;	/* parent component object id */
	uint64_t	start;		/* offset relative to disk start */
	uint64_t	offset;		/* offset for spanned volumes */
	uint64_t	size;		/* partition size */

	LIST_ENTRY(ldm_partition) entry;
};

/*
 * Each VBLK is 128 bytes long and has standard 16 bytes header.
 * Some of VBLK's fields are fixed size, but others has variable size.
 * Fields with variable size are prefixed with one byte length marker.
 * Some fields are strings and also can have fixed size and variable.
 * Strings with fixed size are NULL-terminated, others are not.
 * All VBLKs have same several first fields:
 *	Offset		Size		Description
 *	---------------+---------------+--------------------------
 *	0x00		16		standard VBLK header
 *	0x10		2		update status
 *	0x13		1		VBLK type
 *	0x18		PS		object id
 *	0x18+		PN		object name
 *
 *  o Offset 0x18+ means '0x18 + length of all variable-width fields'
 *  o 'P' in size column means 'prefixed' (variable-width),
 *    'S' - string, 'N' - number.
 */
#define	LDM_VBLK_SIGN		"VBLK"
#define	LDM_VBLK_SEQ_OFF	0x04
#define	LDM_VBLK_GROUP_OFF	0x08
#define	LDM_VBLK_INDEX_OFF	0x0c
#define	LDM_VBLK_COUNT_OFF	0x0e
#define	LDM_VBLK_TYPE_OFF	0x13
#define	LDM_VBLK_OID_OFF	0x18
struct ldm_vblkhdr {
	uint32_t	seq;		/* sequence number */
	uint32_t	group;		/* group number */
	uint16_t	index;		/* index in the group */
	uint16_t	count;		/* number of entries in the group */
};

#define	LDM_VBLK_T_COMPONENT	0x32
#define	LDM_VBLK_T_PARTITION	0x33
#define	LDM_VBLK_T_DISK		0x34
#define	LDM_VBLK_T_DISKGROUP	0x35
#define	LDM_VBLK_T_DISK4	0x44
#define	LDM_VBLK_T_DISKGROUP4	0x45
#define	LDM_VBLK_T_VOLUME	0x51
struct ldm_vblk {
	uint8_t		type;		/* VBLK type */
	union {
		uint64_t		id;
		struct ldm_volume	vol;
		struct ldm_component	comp;
		struct ldm_disk		disk;
		struct ldm_partition	part;
#if 0
		struct ldm_disk_group	disk_group;
#endif
	} u;
	LIST_ENTRY(ldm_vblk) entry;
};

/*
 * Some VBLKs contains a bit more data than can fit into 128 bytes. These
 * VBLKs are called eXtended VBLK. Before parsing, the data from these VBLK
 * should be placed into continuous memory buffer. We can determine xVBLK
 * by the count field in the standard VBLK header (count > 1).
 */
struct ldm_xvblk {
	uint32_t	group;		/* xVBLK group number */
	uint32_t	size;		/* the total size of xVBLK */
	uint8_t		map;		/* bitmask of currently saved VBLKs */
	u_char		*data;		/* xVBLK data */

	LIST_ENTRY(ldm_xvblk)	entry;
};

/* The internal representation of LDM database. */
struct ldm_db {
	struct ldm_privhdr		ph;	/* private header */
	struct ldm_tochdr		th;	/* TOC header */
	struct ldm_vmdbhdr		dh;	/* VMDB header */

	LIST_HEAD(, ldm_volume)		volumes;
	LIST_HEAD(, ldm_disk)		disks;
	LIST_HEAD(, ldm_vblk)		vblks;
	LIST_HEAD(, ldm_xvblk)		xvblks;
};

static struct uuid gpt_uuid_ms_ldm_metadata = GPT_ENT_TYPE_MS_LDM_METADATA;

struct g_part_ldm_table {
	struct g_part_table	base;
	uint64_t		db_offset;
	int			is_gpt;
};
struct g_part_ldm_entry {
	struct g_part_entry	base;
	uint8_t			type;
};

static int g_part_ldm_add(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static int g_part_ldm_bootcode(struct g_part_table *, struct g_part_parms *);
static int g_part_ldm_create(struct g_part_table *, struct g_part_parms *);
static int g_part_ldm_destroy(struct g_part_table *, struct g_part_parms *);
static void g_part_ldm_dumpconf(struct g_part_table *, struct g_part_entry *,
    struct sbuf *, const char *);
static int g_part_ldm_dumpto(struct g_part_table *, struct g_part_entry *);
static int g_part_ldm_modify(struct g_part_table *, struct g_part_entry *,
    struct g_part_parms *);
static const char *g_part_ldm_name(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_ldm_probe(struct g_part_table *, struct g_consumer *);
static int g_part_ldm_read(struct g_part_table *, struct g_consumer *);
static const char *g_part_ldm_type(struct g_part_table *, struct g_part_entry *,
    char *, size_t);
static int g_part_ldm_write(struct g_part_table *, struct g_consumer *);

static kobj_method_t g_part_ldm_methods[] = {
	KOBJMETHOD(g_part_add,		g_part_ldm_add),
	KOBJMETHOD(g_part_bootcode,	g_part_ldm_bootcode),
	KOBJMETHOD(g_part_create,	g_part_ldm_create),
	KOBJMETHOD(g_part_destroy,	g_part_ldm_destroy),
	KOBJMETHOD(g_part_dumpconf,	g_part_ldm_dumpconf),
	KOBJMETHOD(g_part_dumpto,	g_part_ldm_dumpto),
	KOBJMETHOD(g_part_modify,	g_part_ldm_modify),
	KOBJMETHOD(g_part_name,		g_part_ldm_name),
	KOBJMETHOD(g_part_probe,	g_part_ldm_probe),
	KOBJMETHOD(g_part_read,		g_part_ldm_read),
	KOBJMETHOD(g_part_type,		g_part_ldm_type),
	KOBJMETHOD(g_part_write,	g_part_ldm_write),
	{ 0, 0 }
};

static struct g_part_scheme g_part_ldm_scheme = {
	"LDM",
	g_part_ldm_methods,
	sizeof(struct g_part_ldm_table),
	.gps_entrysz = sizeof(struct g_part_ldm_entry)
};
G_PART_SCHEME_DECLARE(g_part_ldm);
MODULE_VERSION(geom_part_ldm, 0);

static struct g_part_ldm_alias {
	u_char		typ;
	int		alias;
} ldm_alias_match[] = {
	{ DOSPTYP_386BSD,	G_PART_ALIAS_FREEBSD },
	{ DOSPTYP_FAT32,	G_PART_ALIAS_MS_FAT32 },
	{ DOSPTYP_FAT32LBA,	G_PART_ALIAS_MS_FAT32LBA },
	{ DOSPTYP_LDM,		G_PART_ALIAS_MS_LDM_DATA },
	{ DOSPTYP_LINLVM,	G_PART_ALIAS_LINUX_LVM },
	{ DOSPTYP_LINRAID,	G_PART_ALIAS_LINUX_RAID },
	{ DOSPTYP_LINSWP,	G_PART_ALIAS_LINUX_SWAP },
	{ DOSPTYP_LINUX,	G_PART_ALIAS_LINUX_DATA },
	{ DOSPTYP_NTFS,		G_PART_ALIAS_MS_NTFS },
};

static u_char*
ldm_privhdr_read(struct g_consumer *cp, uint64_t off, int *error)
{
	struct g_provider *pp;
	u_char *buf;

	pp = cp->provider;
	buf = g_read_data(cp, off, pp->sectorsize, error);
	if (buf == NULL)
		return (NULL);

	if (memcmp(buf, LDM_PH_SIGN, strlen(LDM_PH_SIGN)) != 0) {
		LDM_DEBUG(1, "%s: invalid LDM private header signature",
		    pp->name);
		g_free(buf);
		buf = NULL;
		*error = EINVAL;
	}
	return (buf);
}

static int
ldm_privhdr_parse(struct g_consumer *cp, struct ldm_privhdr *hdr,
    const u_char *buf)
{
	uint32_t version;
	int error;

	memset(hdr, 0, sizeof(*hdr));
	version = be32dec(buf + LDM_PH_VERSION_OFF);
	if (version != LDM_VERSION_2K &&
	    version != LDM_VERSION_VISTA) {
		LDM_DEBUG(0, "%s: unsupported LDM version %u.%u",
		    cp->provider->name, version >> 16,
		    version & 0xFFFF);
		return (ENXIO);
	}
	error = parse_uuid(buf + LDM_PH_DISKGUID_OFF, &hdr->disk_guid);
	if (error != 0)
		return (error);
	error = parse_uuid(buf + LDM_PH_DGGUID_OFF, &hdr->dg_guid);
	if (error != 0)
		return (error);
	strncpy(hdr->dg_name, buf + LDM_PH_DGNAME_OFF, sizeof(hdr->dg_name));
	hdr->start = be64dec(buf + LDM_PH_START_OFF);
	hdr->size = be64dec(buf + LDM_PH_SIZE_OFF);
	hdr->db_offset = be64dec(buf + LDM_PH_DB_OFF);
	hdr->db_size = be64dec(buf + LDM_PH_DBSIZE_OFF);
	hdr->th_offset[0] = be64dec(buf + LDM_PH_TH1_OFF);
	hdr->th_offset[1] = be64dec(buf + LDM_PH_TH2_OFF);
	hdr->conf_size = be64dec(buf + LDM_PH_CONFSIZE_OFF);
	hdr->log_size = be64dec(buf + LDM_PH_LOGSIZE_OFF);
	return (0);
}

static int
ldm_privhdr_check(struct ldm_db *db, struct g_consumer *cp, int is_gpt)
{
	struct g_consumer *cp2;
	struct g_provider *pp;
	struct ldm_privhdr hdr;
	uint64_t offset, last;
	int error, found, i;
	u_char *buf;

	pp = cp->provider;
	if (is_gpt) {
		/*
		 * The last LBA is used in several checks below, for the
		 * GPT case it should be calculated relative to the whole
		 * disk.
		 */
		cp2 = LIST_FIRST(&pp->geom->consumer);
		last =
		    cp2->provider->mediasize / cp2->provider->sectorsize - 1;
	} else
		last = pp->mediasize / pp->sectorsize - 1;
	for (found = 0, i = is_gpt; i < nitems(ldm_ph_off); i++) {
		offset = ldm_ph_off[i];
		/*
		 * In the GPT case consumer is attached to the LDM metadata
		 * partition and we don't need add db_offset.
		 */
		if (!is_gpt)
			offset += db->ph.db_offset;
		if (i == LDM_PH_MBRINDEX) {
			/*
			 * Prepare to errors and setup new base offset
			 * to read backup private headers. Assume that LDM
			 * database is in the last 1Mbyte area.
			 */
			db->ph.db_offset = last - LDM_DB_SIZE;
		}
		buf = ldm_privhdr_read(cp, offset * pp->sectorsize, &error);
		if (buf == NULL) {
			LDM_DEBUG(1, "%s: failed to read private header "
			    "%d at LBA %ju", pp->name, i, (uintmax_t)offset);
			continue;
		}
		error = ldm_privhdr_parse(cp, &hdr, buf);
		if (error != 0) {
			LDM_DEBUG(1, "%s: failed to parse private "
			    "header %d", pp->name, i);
			LDM_DUMP(buf, pp->sectorsize);
			g_free(buf);
			continue;
		}
		g_free(buf);
		if (hdr.start > last ||
		    hdr.start + hdr.size - 1 > last ||
		    (hdr.start + hdr.size - 1 > hdr.db_offset && !is_gpt) ||
		    hdr.db_size != LDM_DB_SIZE ||
		    hdr.db_offset + LDM_DB_SIZE - 1 > last ||
		    hdr.th_offset[0] >= LDM_DB_SIZE ||
		    hdr.th_offset[1] >= LDM_DB_SIZE ||
		    hdr.conf_size + hdr.log_size >= LDM_DB_SIZE) {
			LDM_DEBUG(1, "%s: invalid values in the "
			    "private header %d", pp->name, i);
			LDM_DEBUG(2, "%s: start: %jd, size: %jd, "
			    "db_offset: %jd, db_size: %jd, th_offset0: %jd, "
			    "th_offset1: %jd, conf_size: %jd, log_size: %jd, "
			    "last: %jd", pp->name, hdr.start, hdr.size,
			    hdr.db_offset, hdr.db_size, hdr.th_offset[0],
			    hdr.th_offset[1], hdr.conf_size, hdr.log_size,
			    last);
			continue;
		}
		if (found != 0 && memcmp(&db->ph, &hdr, sizeof(hdr)) != 0) {
			LDM_DEBUG(0, "%s: private headers are not equal",
			    pp->name);
			if (i > 1) {
				/*
				 * We have different headers in the LDM.
				 * We can not trust this metadata.
				 */
				LDM_DEBUG(0, "%s: refuse LDM metadata",
				    pp->name);
				return (EINVAL);
			}
			/*
			 * We already have read primary private header
			 * and it differs from this backup one.
			 * Prefer the backup header and save it.
			 */
			found = 0;
		}
		if (found == 0)
			memcpy(&db->ph, &hdr, sizeof(hdr));
		found = 1;
	}
	if (found == 0) {
		LDM_DEBUG(1, "%s: valid LDM private header not found",
		    pp->name);
		return (ENXIO);
	}
	return (0);
}

static int
ldm_gpt_check(struct ldm_db *db, struct g_consumer *cp)
{
	struct g_part_table *gpt;
	struct g_part_entry *e;
	struct g_consumer *cp2;
	int error;

	cp2 = LIST_NEXT(cp, consumer);
	g_topology_lock();
	gpt = cp->provider->geom->softc;
	error = 0;
	LIST_FOREACH(e, &gpt->gpt_entry, gpe_entry) {
		if (cp->provider == e->gpe_pp) {
			/* ms-ldm-metadata partition */
			if (e->gpe_start != db->ph.db_offset ||
			    e->gpe_end != db->ph.db_offset + LDM_DB_SIZE - 1)
				error++;
		} else if (cp2->provider == e->gpe_pp) {
			/* ms-ldm-data partition */
			if (e->gpe_start != db->ph.start ||
			    e->gpe_end != db->ph.start + db->ph.size - 1)
				error++;
		}
		if (error != 0) {
			LDM_DEBUG(0, "%s: GPT partition %d boundaries "
			    "do not match with the LDM metadata",
			    e->gpe_pp->name, e->gpe_index);
			error = ENXIO;
			break;
		}
	}
	g_topology_unlock();
	return (error);
}

static int
ldm_tochdr_check(struct ldm_db *db, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct ldm_tochdr hdr;
	uint64_t offset, conf_size, log_size;
	int error, found, i;
	u_char *buf;

	pp = cp->provider;
	for (i = 0, found = 0; i < LDM_TH_COUNT; i++) {
		offset = db->ph.db_offset + db->ph.th_offset[i];
		buf = g_read_data(cp,
		    offset * pp->sectorsize, pp->sectorsize, &error);
		if (buf == NULL) {
			LDM_DEBUG(1, "%s: failed to read TOC header "
			    "at LBA %ju", pp->name, (uintmax_t)offset);
			continue;
		}
		if (memcmp(buf, LDM_TH_SIGN, strlen(LDM_TH_SIGN)) != 0 ||
		    memcmp(buf + LDM_TH_NAME1_OFF, LDM_TH_NAME1,
		    strlen(LDM_TH_NAME1)) != 0 ||
		    memcmp(buf + LDM_TH_NAME2_OFF, LDM_TH_NAME2,
		    strlen(LDM_TH_NAME2)) != 0) {
			LDM_DEBUG(1, "%s: failed to parse TOC header "
			    "at LBA %ju", pp->name, (uintmax_t)offset);
			LDM_DUMP(buf, pp->sectorsize);
			g_free(buf);
			continue;
		}
		hdr.conf_offset = be64dec(buf + LDM_TH_CONF_OFF);
		hdr.log_offset = be64dec(buf + LDM_TH_LOG_OFF);
		conf_size = be64dec(buf + LDM_TH_CONFSIZE_OFF);
		log_size = be64dec(buf + LDM_TH_LOGSIZE_OFF);
		if (conf_size != db->ph.conf_size ||
		    hdr.conf_offset + conf_size >= LDM_DB_SIZE ||
		    log_size != db->ph.log_size ||
		    hdr.log_offset + log_size >= LDM_DB_SIZE) {
			LDM_DEBUG(1, "%s: invalid values in the "
			    "TOC header at LBA %ju", pp->name,
			    (uintmax_t)offset);
			LDM_DUMP(buf, pp->sectorsize);
			g_free(buf);
			continue;
		}
		g_free(buf);
		if (found == 0)
			memcpy(&db->th, &hdr, sizeof(hdr));
		found = 1;
	}
	if (found == 0) {
		LDM_DEBUG(0, "%s: valid LDM TOC header not found.",
		    pp->name);
		return (ENXIO);
	}
	return (0);
}

static int
ldm_vmdbhdr_check(struct ldm_db *db, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct uuid dg_guid;
	uint64_t offset;
	uint32_t version;
	int error;
	u_char *buf;

	pp = cp->provider;
	offset = db->ph.db_offset + db->th.conf_offset;
	buf = g_read_data(cp, offset * pp->sectorsize, pp->sectorsize,
	    &error);
	if (buf == NULL) {
		LDM_DEBUG(0, "%s: failed to read VMDB header at "
		    "LBA %ju", pp->name, (uintmax_t)offset);
		return (error);
	}
	if (memcmp(buf, LDM_VMDB_SIGN, strlen(LDM_VMDB_SIGN)) != 0) {
		g_free(buf);
		LDM_DEBUG(0, "%s: failed to parse VMDB header at "
		    "LBA %ju", pp->name, (uintmax_t)offset);
		return (ENXIO);
	}
	/* Check version. */
	version = be32dec(buf + LDM_DB_VERSION_OFF);
	if (version != 0x4000A) {
		g_free(buf);
		LDM_DEBUG(0, "%s: unsupported VMDB version %u.%u",
		    pp->name, version >> 16, version & 0xFFFF);
		return (ENXIO);
	}
	/*
	 * Check VMDB update status:
	 *	1 - in a consistent state;
	 *	2 - in a creation phase;
	 *	3 - in a deletion phase;
	 */
	if (be16dec(buf + LDM_DB_STATUS_OFF) != 1) {
		g_free(buf);
		LDM_DEBUG(0, "%s: VMDB is not in a consistent state",
		    pp->name);
		return (ENXIO);
	}
	db->dh.last_seq = be32dec(buf + LDM_DB_LASTSEQ_OFF);
	db->dh.size = be32dec(buf + LDM_DB_SIZE_OFF);
	error = parse_uuid(buf + LDM_DB_DGGUID_OFF, &dg_guid);
	/* Compare disk group name and guid from VMDB and private headers */
	if (error != 0 || db->dh.size == 0 ||
	    pp->sectorsize % db->dh.size != 0 ||
	    strncmp(buf + LDM_DB_DGNAME_OFF, db->ph.dg_name, 31) != 0 ||
	    memcmp(&dg_guid, &db->ph.dg_guid, sizeof(dg_guid)) != 0 ||
	    db->dh.size * db->dh.last_seq >
	    db->ph.conf_size * pp->sectorsize) {
		LDM_DEBUG(0, "%s: invalid values in the VMDB header",
		    pp->name);
		LDM_DUMP(buf, pp->sectorsize);
		g_free(buf);
		return (EINVAL);
	}
	g_free(buf);
	return (0);
}

static int
ldm_xvblk_handle(struct ldm_db *db, struct ldm_vblkhdr *vh, const u_char *p)
{
	struct ldm_xvblk *blk;
	size_t size;

	size = db->dh.size - 16;
	LIST_FOREACH(blk, &db->xvblks, entry)
		if (blk->group == vh->group)
			break;
	if (blk == NULL) {
		blk = g_malloc(sizeof(*blk), M_WAITOK | M_ZERO);
		blk->group = vh->group;
		blk->size = size * vh->count + 16;
		blk->data = g_malloc(blk->size, M_WAITOK | M_ZERO);
		blk->map = 0xFF << vh->count;
		LIST_INSERT_HEAD(&db->xvblks, blk, entry);
	}
	if ((blk->map & (1 << vh->index)) != 0) {
		/* Block with given index has been already saved. */
		return (EINVAL);
	}
	/* Copy the data block to the place related to index. */
	memcpy(blk->data + size * vh->index + 16, p + 16, size);
	blk->map |= 1 << vh->index;
	return (0);
}

/* Read the variable-width numeric field and return new offset */
static int
ldm_vnum_get(const u_char *buf, int offset, uint64_t *result, size_t range)
{
	uint64_t num;
	uint8_t len;

	len = buf[offset++];
	if (len > sizeof(uint64_t) || len + offset >= range)
		return (-1);
	for (num = 0; len > 0; len--)
		num = (num << 8) | buf[offset++];
	*result = num;
	return (offset);
}

/* Read the variable-width string and return new offset */
static int
ldm_vstr_get(const u_char *buf, int offset, u_char *result,
    size_t maxlen, size_t range)
{
	uint8_t len;

	len = buf[offset++];
	if (len >= maxlen || len + offset >= range)
		return (-1);
	memcpy(result, buf + offset, len);
	result[len] = '\0';
	return (offset + len);
}

/* Just skip the variable-width variable and return new offset */
static int
ldm_vparm_skip(const u_char *buf, int offset, size_t range)
{
	uint8_t len;

	len = buf[offset++];
	if (offset + len >= range)
		return (-1);

	return (offset + len);
}

static int
ldm_vblk_handle(struct ldm_db *db, const u_char *p, size_t size)
{
	struct ldm_vblk *blk;
	struct ldm_volume *volume, *last;
	const char *errstr;
	u_char vstr[64];
	int error, offset;

	blk = g_malloc(sizeof(*blk), M_WAITOK | M_ZERO);
	blk->type = p[LDM_VBLK_TYPE_OFF];
	offset = ldm_vnum_get(p, LDM_VBLK_OID_OFF, &blk->u.id, size);
	if (offset < 0) {
		errstr = "object id";
		goto fail;
	}
	offset = ldm_vstr_get(p, offset, vstr, sizeof(vstr), size);
	if (offset < 0) {
		errstr = "object name";
		goto fail;
	}
	switch (blk->type) {
	/*
	 * Component VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+	PS	volume state
	 *  0x18+5	PN	component children count
	 *  0x1D+16	PN	parent's volume object id
	 *  0x2D+1	PN	stripe size
	 */
	case LDM_VBLK_T_COMPONENT:
		offset = ldm_vparm_skip(p, offset, size);
		if (offset < 0) {
			errstr = "volume state";
			goto fail;
		}
		offset = ldm_vparm_skip(p, offset + 5, size);
		if (offset < 0) {
			errstr = "children count";
			goto fail;
		}
		offset = ldm_vnum_get(p, offset + 16,
		    &blk->u.comp.vol_id, size);
		if (offset < 0) {
			errstr = "volume id";
			goto fail;
		}
		break;
	/*
	 * Partition VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+12	8	partition start offset
	 *  0x18+20	8	volume offset
	 *  0x18+28	PN	partition size
	 *  0x34+	PN	parent's component object id
	 *  0x34+	PN	disk's object id
	 */
	case LDM_VBLK_T_PARTITION:
		if (offset + 28 >= size) {
			errstr = "too small buffer";
			goto fail;
		}
		blk->u.part.start = be64dec(p + offset + 12);
		blk->u.part.offset = be64dec(p + offset + 20);
		offset = ldm_vnum_get(p, offset + 28, &blk->u.part.size, size);
		if (offset < 0) {
			errstr = "partition size";
			goto fail;
		}
		offset = ldm_vnum_get(p, offset, &blk->u.part.comp_id, size);
		if (offset < 0) {
			errstr = "component id";
			goto fail;
		}
		offset = ldm_vnum_get(p, offset, &blk->u.part.disk_id, size);
		if (offset < 0) {
			errstr = "disk id";
			goto fail;
		}
		break;
	/*
	 * Disk VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+	PS	disk GUID
	 */
	case LDM_VBLK_T_DISK:
		errstr = "disk guid";
		offset = ldm_vstr_get(p, offset, vstr, sizeof(vstr), size);
		if (offset < 0)
			goto fail;
		error = parse_uuid(vstr, &blk->u.disk.guid);
		if (error != 0)
			goto fail;
		LIST_INSERT_HEAD(&db->disks, &blk->u.disk, entry);
		break;
	/*
	 * Disk group VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+	PS	disk group GUID
	 */
	case LDM_VBLK_T_DISKGROUP:
#if 0
		strncpy(blk->u.disk_group.name, vstr,
		    sizeof(blk->u.disk_group.name));
		offset = ldm_vstr_get(p, offset, vstr, sizeof(vstr), size);
		if (offset < 0) {
			errstr = "disk group guid";
			goto fail;
		}
		error = parse_uuid(name, &blk->u.disk_group.guid);
		if (error != 0) {
			errstr = "disk group guid";
			goto fail;
		}
		LIST_INSERT_HEAD(&db->groups, &blk->u.disk_group, entry);
#endif
		break;
	/*
	 * Disk VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+	16	disk GUID
	 */
	case LDM_VBLK_T_DISK4:
		be_uuid_dec(p + offset, &blk->u.disk.guid);
		LIST_INSERT_HEAD(&db->disks, &blk->u.disk, entry);
		break;
	/*
	 * Disk group VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+	16	disk GUID
	 */
	case LDM_VBLK_T_DISKGROUP4:
#if 0
		strncpy(blk->u.disk_group.name, vstr,
		    sizeof(blk->u.disk_group.name));
		be_uuid_dec(p + offset, &blk->u.disk.guid);
		LIST_INSERT_HEAD(&db->groups, &blk->u.disk_group, entry);
#endif
		break;
	/*
	 * Volume VBLK fields:
	 * Offset	Size	Description
	 * ------------+-------+------------------------
	 *  0x18+	PS	volume type
	 *  0x18+	PS	unknown
	 *  0x18+	14(S)	volume state
	 *  0x18+16	1	volume number
	 *  0x18+21	PN	volume children count
	 *  0x2D+16	PN	volume size
	 *  0x3D+4	1	partition type
	 */
	case LDM_VBLK_T_VOLUME:
		offset = ldm_vparm_skip(p, offset, size);
		if (offset < 0) {
			errstr = "volume type";
			goto fail;
		}
		offset = ldm_vparm_skip(p, offset, size);
		if (offset < 0) {
			errstr = "unknown param";
			goto fail;
		}
		if (offset + 21 >= size) {
			errstr = "too small buffer";
			goto fail;
		}
		blk->u.vol.number = p[offset + 16];
		offset = ldm_vparm_skip(p, offset + 21, size);
		if (offset < 0) {
			errstr = "children count";
			goto fail;
		}
		offset = ldm_vnum_get(p, offset + 16, &blk->u.vol.size, size);
		if (offset < 0) {
			errstr = "volume size";
			goto fail;
		}
		if (offset + 4 >= size) {
			errstr = "too small buffer";
			goto fail;
		}
		blk->u.vol.part_type = p[offset + 4];
		/* keep volumes ordered by volume number */
		last = NULL;
		LIST_FOREACH(volume, &db->volumes, entry) {
			if (volume->number > blk->u.vol.number)
				break;
			last = volume;
		}
		if (last != NULL)
			LIST_INSERT_AFTER(last, &blk->u.vol, entry);
		else
			LIST_INSERT_HEAD(&db->volumes, &blk->u.vol, entry);
		break;
	default:
		LDM_DEBUG(1, "unknown VBLK type 0x%02x\n", blk->type);
		LDM_DUMP(p, size);
	}
	LIST_INSERT_HEAD(&db->vblks, blk, entry);
	return (0);
fail:
	LDM_DEBUG(0, "failed to parse '%s' in VBLK of type 0x%02x\n",
	    errstr, blk->type);
	LDM_DUMP(p, size);
	g_free(blk);
	return (EINVAL);
}

static void
ldm_vmdb_free(struct ldm_db *db)
{
	struct ldm_vblk *vblk;
	struct ldm_xvblk *xvblk;

	while (!LIST_EMPTY(&db->xvblks)) {
		xvblk = LIST_FIRST(&db->xvblks);
		LIST_REMOVE(xvblk, entry);
		g_free(xvblk->data);
		g_free(xvblk);
	}
	while (!LIST_EMPTY(&db->vblks)) {
		vblk = LIST_FIRST(&db->vblks);
		LIST_REMOVE(vblk, entry);
		g_free(vblk);
	}
}

static int
ldm_vmdb_parse(struct ldm_db *db, struct g_consumer *cp)
{
	struct g_provider *pp;
	struct ldm_vblk *vblk;
	struct ldm_xvblk *xvblk;
	struct ldm_volume *volume;
	struct ldm_component *comp;
	struct ldm_vblkhdr vh;
	u_char *buf, *p;
	size_t size, n, sectors;
	uint64_t offset;
	int error;

	pp = cp->provider;
	size = howmany(db->dh.last_seq * db->dh.size, pp->sectorsize);
	size -= 1; /* one sector takes vmdb header */
	for (n = 0; n < size; n += MAXPHYS / pp->sectorsize) {
		offset = db->ph.db_offset + db->th.conf_offset + n + 1;
		sectors = (size - n) > (MAXPHYS / pp->sectorsize) ?
		    MAXPHYS / pp->sectorsize: size - n;
		/* read VBLKs */
		buf = g_read_data(cp, offset * pp->sectorsize,
		    sectors * pp->sectorsize, &error);
		if (buf == NULL) {
			LDM_DEBUG(0, "%s: failed to read VBLK\n",
			    pp->name);
			goto fail;
		}
		for (p = buf; p < buf + sectors * pp->sectorsize;
		    p += db->dh.size) {
			if (memcmp(p, LDM_VBLK_SIGN,
			    strlen(LDM_VBLK_SIGN)) != 0) {
				LDM_DEBUG(0, "%s: no VBLK signature\n",
				    pp->name);
				LDM_DUMP(p, db->dh.size);
				goto fail;
			}
			vh.seq = be32dec(p + LDM_VBLK_SEQ_OFF);
			vh.group = be32dec(p + LDM_VBLK_GROUP_OFF);
			/* skip empty blocks */
			if (vh.seq == 0 || vh.group == 0)
				continue;
			vh.index = be16dec(p + LDM_VBLK_INDEX_OFF);
			vh.count = be16dec(p + LDM_VBLK_COUNT_OFF);
			if (vh.count == 0 || vh.count > 4 ||
			    vh.seq > db->dh.last_seq) {
				LDM_DEBUG(0, "%s: invalid values "
				    "in the VBLK header\n", pp->name);
				LDM_DUMP(p, db->dh.size);
				goto fail;
			}
			if (vh.count > 1) {
				error = ldm_xvblk_handle(db, &vh, p);
				if (error != 0) {
					LDM_DEBUG(0, "%s: xVBLK "
					    "is corrupted\n", pp->name);
					LDM_DUMP(p, db->dh.size);
					goto fail;
				}
				continue;
			}
			if (be16dec(p + 16) != 0)
				LDM_DEBUG(1, "%s: VBLK update"
				    " status is %u\n", pp->name,
				    be16dec(p + 16));
			error = ldm_vblk_handle(db, p, db->dh.size);
			if (error != 0)
				goto fail;
		}
		g_free(buf);
		buf = NULL;
	}
	/* Parse xVBLKs */
	while (!LIST_EMPTY(&db->xvblks)) {
		xvblk = LIST_FIRST(&db->xvblks);
		if (xvblk->map == 0xFF) {
			error = ldm_vblk_handle(db, xvblk->data, xvblk->size);
			if (error != 0)
				goto fail;
		} else {
			LDM_DEBUG(0, "%s: incomplete or corrupt "
			    "xVBLK found\n", pp->name);
			goto fail;
		}
		LIST_REMOVE(xvblk, entry);
		g_free(xvblk->data);
		g_free(xvblk);
	}
	/* construct all VBLKs relations */
	LIST_FOREACH(volume, &db->volumes, entry) {
		LIST_FOREACH(vblk, &db->vblks, entry)
			if (vblk->type == LDM_VBLK_T_COMPONENT &&
			    vblk->u.comp.vol_id == volume->id) {
				LIST_INSERT_HEAD(&volume->components,
				    &vblk->u.comp, entry);
				volume->count++;
			}
		LIST_FOREACH(comp, &volume->components, entry)
			LIST_FOREACH(vblk, &db->vblks, entry)
				if (vblk->type == LDM_VBLK_T_PARTITION &&
				    vblk->u.part.comp_id == comp->id) {
					LIST_INSERT_HEAD(&comp->partitions,
					    &vblk->u.part, entry);
					comp->count++;
				}
	}
	return (0);
fail:
	ldm_vmdb_free(db);
	g_free(buf);
	return (ENXIO);
}

static int
g_part_ldm_add(struct g_part_table *basetable, struct g_part_entry *baseentry,
    struct g_part_parms *gpp)
{

	return (ENOSYS);
}

static int
g_part_ldm_bootcode(struct g_part_table *basetable, struct g_part_parms *gpp)
{

	return (ENOSYS);
}

static int
g_part_ldm_create(struct g_part_table *basetable, struct g_part_parms *gpp)
{

	return (ENOSYS);
}

static int
g_part_ldm_destroy(struct g_part_table *basetable, struct g_part_parms *gpp)
{
	struct g_part_ldm_table *table;
	struct g_provider *pp;

	table = (struct g_part_ldm_table *)basetable;
	/*
	 * To destroy LDM on a disk partitioned with GPT we should delete
	 * ms-ldm-metadata partition, but we can't do this via standard
	 * GEOM_PART method.
	 */
	if (table->is_gpt)
		return (ENOSYS);
	pp = LIST_FIRST(&basetable->gpt_gp->consumer)->provider;
	/*
	 * To destroy LDM we should wipe MBR, first private header and
	 * backup private headers.
	 */
	basetable->gpt_smhead = (1 << ldm_ph_off[0]) | 1;
	/*
	 * Don't touch last backup private header when LDM database is
	 * not located in the last 1MByte area.
	 * XXX: can't remove all blocks.
	 */
	if (table->db_offset + LDM_DB_SIZE ==
	    pp->mediasize / pp->sectorsize)
		basetable->gpt_smtail = 1;
	return (0);
}

static void
g_part_ldm_dumpconf(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct sbuf *sb, const char *indent)
{
	struct g_part_ldm_entry *entry;

	entry = (struct g_part_ldm_entry *)baseentry;
	if (indent == NULL) {
		/* conftxt: libdisk compatibility */
		sbuf_printf(sb, " xs LDM xt %u", entry->type);
	} else if (entry != NULL) {
		/* confxml: partition entry information */
		sbuf_printf(sb, "%s<rawtype>%u</rawtype>\n", indent,
		    entry->type);
	} else {
		/* confxml: scheme information */
	}
}

static int
g_part_ldm_dumpto(struct g_part_table *table, struct g_part_entry *baseentry)
{

	return (0);
}

static int
g_part_ldm_modify(struct g_part_table *basetable,
    struct g_part_entry *baseentry, struct g_part_parms *gpp)
{

	return (ENOSYS);
}

static const char *
g_part_ldm_name(struct g_part_table *table, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{

	snprintf(buf, bufsz, "s%d", baseentry->gpe_index);
	return (buf);
}

static int
ldm_gpt_probe(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_part_ldm_table *table;
	struct g_part_table *gpt;
	struct g_part_entry *entry;
	struct g_consumer *cp2;
	struct gpt_ent *part;
	u_char *buf;
	int error;

	/*
	 * XXX: We use some knowledge about GEOM_PART_GPT internal
	 * structures, but it is easier than parse GPT by himself.
	 */
	g_topology_lock();
	gpt = cp->provider->geom->softc;
	LIST_FOREACH(entry, &gpt->gpt_entry, gpe_entry) {
		part = (struct gpt_ent *)(entry + 1);
		/* Search ms-ldm-metadata partition */
		if (memcmp(&part->ent_type,
		    &gpt_uuid_ms_ldm_metadata, sizeof(struct uuid)) != 0 ||
		    entry->gpe_end - entry->gpe_start < LDM_DB_SIZE - 1)
			continue;

		/* Create new consumer and attach it to metadata partition */
		cp2 = g_new_consumer(cp->geom);
		error = g_attach(cp2, entry->gpe_pp);
		if (error != 0) {
			g_destroy_consumer(cp2);
			g_topology_unlock();
			return (ENXIO);
		}
		error = g_access(cp2, 1, 0, 0);
		if (error != 0) {
			g_detach(cp2);
			g_destroy_consumer(cp2);
			g_topology_unlock();
			return (ENXIO);
		}
		g_topology_unlock();

		LDM_DEBUG(2, "%s: LDM metadata partition %s found in the GPT",
		    cp->provider->name, cp2->provider->name);
		/* Read the LDM private header */
		buf = ldm_privhdr_read(cp2,
		    ldm_ph_off[LDM_PH_GPTINDEX] * cp2->provider->sectorsize,
		    &error);
		if (buf != NULL) {
			table = (struct g_part_ldm_table *)basetable;
			table->is_gpt = 1;
			g_free(buf);
			return (G_PART_PROBE_PRI_HIGH);
		}

		/* second consumer is no longer needed. */
		g_topology_lock();
		g_access(cp2, -1, 0, 0);
		g_detach(cp2);
		g_destroy_consumer(cp2);
		break;
	}
	g_topology_unlock();
	return (ENXIO);
}

static int
g_part_ldm_probe(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_provider *pp;
	u_char *buf, type[64];
	int error, idx;


	pp = cp->provider;
	if (pp->sectorsize != 512)
		return (ENXIO);

	error = g_getattr("PART::scheme", cp, &type);
	if (error == 0 && strcmp(type, "GPT") == 0) {
		if (g_getattr("PART::type", cp, &type) != 0 ||
		    strcmp(type, "ms-ldm-data") != 0)
			return (ENXIO);
		error = ldm_gpt_probe(basetable, cp);
		return (error);
	}

	if (basetable->gpt_depth != 0)
		return (ENXIO);

	/* LDM has 1M metadata area */
	if (pp->mediasize <= 1024 * 1024)
		return (ENOSPC);

	/* Check that there's a MBR */
	buf = g_read_data(cp, 0, pp->sectorsize, &error);
	if (buf == NULL)
		return (error);

	if (le16dec(buf + DOSMAGICOFFSET) != DOSMAGIC) {
		g_free(buf);
		return (ENXIO);
	}
	error = ENXIO;
	/* Check that we have LDM partitions in the MBR */
	for (idx = 0; idx < NDOSPART && error != 0; idx++) {
		if (buf[DOSPARTOFF + idx * DOSPARTSIZE + 4] == DOSPTYP_LDM)
			error = 0;
	}
	g_free(buf);
	if (error == 0) {
		LDM_DEBUG(2, "%s: LDM data partitions found in MBR",
		    pp->name);
		/* Read the LDM private header */
		buf = ldm_privhdr_read(cp,
		    ldm_ph_off[LDM_PH_MBRINDEX] * pp->sectorsize, &error);
		if (buf == NULL)
			return (error);
		g_free(buf);
		return (G_PART_PROBE_PRI_HIGH);
	}
	return (error);
}

static int
g_part_ldm_read(struct g_part_table *basetable, struct g_consumer *cp)
{
	struct g_part_ldm_table *table;
	struct g_part_ldm_entry *entry;
	struct g_consumer *cp2;
	struct ldm_component *comp;
	struct ldm_partition *part;
	struct ldm_volume *vol;
	struct ldm_disk *disk;
	struct ldm_db db;
	int error, index, skipped;

	table = (struct g_part_ldm_table *)basetable;
	memset(&db, 0, sizeof(db));
	cp2 = cp;					/* ms-ldm-data */
	if (table->is_gpt)
		cp = LIST_FIRST(&cp->geom->consumer);	/* ms-ldm-metadata */
	/* Read and parse LDM private headers. */
	error = ldm_privhdr_check(&db, cp, table->is_gpt);
	if (error != 0)
		goto gpt_cleanup;
	basetable->gpt_first = table->is_gpt ? 0: db.ph.start;
	basetable->gpt_last = basetable->gpt_first + db.ph.size - 1;
	table->db_offset = db.ph.db_offset;
	/* Make additional checks for GPT */
	if (table->is_gpt) {
		error = ldm_gpt_check(&db, cp);
		if (error != 0)
			goto gpt_cleanup;
		/*
		 * Now we should reset database offset to zero, because our
		 * consumer cp is attached to the ms-ldm-metadata partition
		 * and we don't need add db_offset to read from it.
		 */
		db.ph.db_offset = 0;
	}
	/* Read and parse LDM TOC headers. */
	error = ldm_tochdr_check(&db, cp);
	if (error != 0)
		goto gpt_cleanup;
	/* Read and parse LDM VMDB header. */
	error = ldm_vmdbhdr_check(&db, cp);
	if (error != 0)
		goto gpt_cleanup;
	error = ldm_vmdb_parse(&db, cp);
	/*
	 * For the GPT case we must detach and destroy
	 * second consumer before return.
	 */
gpt_cleanup:
	if (table->is_gpt) {
		g_topology_lock();
		g_access(cp, -1, 0, 0);
		g_detach(cp);
		g_destroy_consumer(cp);
		g_topology_unlock();
		cp = cp2;
	}
	if (error != 0)
		return (error);
	/* Search current disk in the disk list. */
	LIST_FOREACH(disk, &db.disks, entry)
	    if (memcmp(&disk->guid, &db.ph.disk_guid,
		sizeof(struct uuid)) == 0)
		    break;
	if (disk == NULL) {
		LDM_DEBUG(1, "%s: no LDM volumes on this disk",
		    cp->provider->name);
		ldm_vmdb_free(&db);
		return (ENXIO);
	}
	index = 1;
	LIST_FOREACH(vol, &db.volumes, entry) {
		LIST_FOREACH(comp, &vol->components, entry) {
			/* Skip volumes from different disks. */
			part = LIST_FIRST(&comp->partitions);
			if (part->disk_id != disk->id)
				continue;
			skipped = 0;
			/* We don't support spanned and striped volumes. */
			if (comp->count > 1 || part->offset != 0) {
				LDM_DEBUG(1, "%s: LDM volume component "
				    "%ju has %u partitions. Skipped",
				    cp->provider->name, (uintmax_t)comp->id,
				    comp->count);
				skipped = 1;
			}
			/*
			 * Allow mirrored volumes only when they are explicitly
			 * allowed with kern.geom.part.ldm.show_mirrors=1.
			 */
			if (vol->count > 1 && show_mirrors == 0) {
				LDM_DEBUG(1, "%s: LDM volume %ju has %u "
				    "components. Skipped",
				    cp->provider->name, (uintmax_t)vol->id,
				    vol->count);
				skipped = 1;
			}
			entry = (struct g_part_ldm_entry *)g_part_new_entry(
			    basetable, index++,
			    basetable->gpt_first + part->start,
			    basetable->gpt_first + part->start +
			    part->size - 1);
			/*
			 * Mark skipped partition as ms-ldm-data partition.
			 * We do not support them, but it is better to show
			 * that we have something there, than just show
			 * free space.
			 */
			if (skipped == 0)
				entry->type = vol->part_type;
			else
				entry->type = DOSPTYP_LDM;
			LDM_DEBUG(1, "%s: new volume id: %ju, start: %ju,"
			    " end: %ju, type: 0x%02x\n", cp->provider->name,
			    (uintmax_t)part->id,(uintmax_t)part->start +
			    basetable->gpt_first, (uintmax_t)part->start +
			    part->size + basetable->gpt_first - 1,
			    vol->part_type);
		}
	}
	ldm_vmdb_free(&db);
	return (error);
}

static const char *
g_part_ldm_type(struct g_part_table *basetable, struct g_part_entry *baseentry,
    char *buf, size_t bufsz)
{
	struct g_part_ldm_entry *entry;
	int i;

	entry = (struct g_part_ldm_entry *)baseentry;
	for (i = 0; i < nitems(ldm_alias_match); i++) {
		if (ldm_alias_match[i].typ == entry->type)
			return (g_part_alias_name(ldm_alias_match[i].alias));
	}
	snprintf(buf, bufsz, "!%d", entry->type);
	return (buf);
}

static int
g_part_ldm_write(struct g_part_table *basetable, struct g_consumer *cp)
{

	return (ENOSYS);
}
