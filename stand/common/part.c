/*-
 * Copyright (c) 2012 Andrey V. Elsukov <ae@FreeBSD.org>
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

#include <stand.h>
#include <sys/param.h>
#include <sys/diskmbr.h>
#include <sys/disklabel.h>
#include <sys/endian.h>
#include <sys/gpt.h>
#include <sys/stddef.h>
#include <sys/queue.h>
#include <sys/vtoc.h>

#include <fs/cd9660/iso.h>

#include <crc32.h>
#include <part.h>
#include <uuid.h>

#ifdef PART_DEBUG
#define	DPRINTF(fmt, args...) printf("%s: " fmt "\n", __func__, ## args)
#else
#define	DPRINTF(fmt, args...)
#endif

#ifdef LOADER_GPT_SUPPORT
#define	MAXTBLSZ	64
static const uuid_t gpt_uuid_unused = GPT_ENT_TYPE_UNUSED;
static const uuid_t gpt_uuid_ms_basic_data = GPT_ENT_TYPE_MS_BASIC_DATA;
static const uuid_t gpt_uuid_freebsd_ufs = GPT_ENT_TYPE_FREEBSD_UFS;
static const uuid_t gpt_uuid_efi = GPT_ENT_TYPE_EFI;
static const uuid_t gpt_uuid_freebsd = GPT_ENT_TYPE_FREEBSD;
static const uuid_t gpt_uuid_freebsd_boot = GPT_ENT_TYPE_FREEBSD_BOOT;
static const uuid_t gpt_uuid_freebsd_nandfs = GPT_ENT_TYPE_FREEBSD_NANDFS;
static const uuid_t gpt_uuid_freebsd_swap = GPT_ENT_TYPE_FREEBSD_SWAP;
static const uuid_t gpt_uuid_freebsd_zfs = GPT_ENT_TYPE_FREEBSD_ZFS;
static const uuid_t gpt_uuid_freebsd_vinum = GPT_ENT_TYPE_FREEBSD_VINUM;
#endif

struct pentry {
	struct ptable_entry	part;
	uint64_t		flags;
	union {
		uint8_t bsd;
		uint8_t	mbr;
		uuid_t	gpt;
		uint16_t vtoc8;
	} type;
	STAILQ_ENTRY(pentry)	entry;
};

struct ptable {
	enum ptable_type	type;
	uint16_t		sectorsize;
	uint64_t		sectors;

	STAILQ_HEAD(, pentry)	entries;
};

static struct parttypes {
	enum partition_type	type;
	const char		*desc;
} ptypes[] = {
	{ PART_UNKNOWN,		"Unknown" },
	{ PART_EFI,		"EFI" },
	{ PART_FREEBSD,		"FreeBSD" },
	{ PART_FREEBSD_BOOT,	"FreeBSD boot" },
	{ PART_FREEBSD_NANDFS,	"FreeBSD nandfs" },
	{ PART_FREEBSD_UFS,	"FreeBSD UFS" },
	{ PART_FREEBSD_ZFS,	"FreeBSD ZFS" },
	{ PART_FREEBSD_SWAP,	"FreeBSD swap" },
	{ PART_FREEBSD_VINUM,	"FreeBSD vinum" },
	{ PART_LINUX,		"Linux" },
	{ PART_LINUX_SWAP,	"Linux swap" },
	{ PART_DOS,		"DOS/Windows" },
	{ PART_ISO9660,		"ISO9660" },
};

const char *
parttype2str(enum partition_type type)
{
	size_t i;

	for (i = 0; i < nitems(ptypes); i++)
		if (ptypes[i].type == type)
			return (ptypes[i].desc);
	return (ptypes[0].desc);
}

#ifdef LOADER_GPT_SUPPORT
static void
uuid_letoh(uuid_t *uuid)
{

	uuid->time_low = le32toh(uuid->time_low);
	uuid->time_mid = le16toh(uuid->time_mid);
	uuid->time_hi_and_version = le16toh(uuid->time_hi_and_version);
}

static enum partition_type
gpt_parttype(uuid_t type)
{

	if (uuid_equal(&type, &gpt_uuid_efi, NULL))
		return (PART_EFI);
	else if (uuid_equal(&type, &gpt_uuid_ms_basic_data, NULL))
		return (PART_DOS);
	else if (uuid_equal(&type, &gpt_uuid_freebsd_boot, NULL))
		return (PART_FREEBSD_BOOT);
	else if (uuid_equal(&type, &gpt_uuid_freebsd_ufs, NULL))
		return (PART_FREEBSD_UFS);
	else if (uuid_equal(&type, &gpt_uuid_freebsd_zfs, NULL))
		return (PART_FREEBSD_ZFS);
	else if (uuid_equal(&type, &gpt_uuid_freebsd_swap, NULL))
		return (PART_FREEBSD_SWAP);
	else if (uuid_equal(&type, &gpt_uuid_freebsd_vinum, NULL))
		return (PART_FREEBSD_VINUM);
	else if (uuid_equal(&type, &gpt_uuid_freebsd_nandfs, NULL))
		return (PART_FREEBSD_NANDFS);
	else if (uuid_equal(&type, &gpt_uuid_freebsd, NULL))
		return (PART_FREEBSD);
	return (PART_UNKNOWN);
}

static struct gpt_hdr *
gpt_checkhdr(struct gpt_hdr *hdr, uint64_t lba_self, uint64_t lba_last,
    uint16_t sectorsize)
{
	uint32_t sz, crc;

	if (memcmp(hdr->hdr_sig, GPT_HDR_SIG, sizeof(hdr->hdr_sig)) != 0) {
		DPRINTF("no GPT signature");
		return (NULL);
	}
	sz = le32toh(hdr->hdr_size);
	if (sz < 92 || sz > sectorsize) {
		DPRINTF("invalid GPT header size: %d", sz);
		return (NULL);
	}
	crc = le32toh(hdr->hdr_crc_self);
	hdr->hdr_crc_self = 0;
	if (crc32(hdr, sz) != crc) {
		DPRINTF("GPT header's CRC doesn't match");
		return (NULL);
	}
	hdr->hdr_crc_self = crc;
	hdr->hdr_revision = le32toh(hdr->hdr_revision);
	if (hdr->hdr_revision < GPT_HDR_REVISION) {
		DPRINTF("unsupported GPT revision %d", hdr->hdr_revision);
		return (NULL);
	}
	hdr->hdr_lba_self = le64toh(hdr->hdr_lba_self);
	if (hdr->hdr_lba_self != lba_self) {
		DPRINTF("self LBA doesn't match");
		return (NULL);
	}
	hdr->hdr_lba_alt = le64toh(hdr->hdr_lba_alt);
	if (hdr->hdr_lba_alt == hdr->hdr_lba_self) {
		DPRINTF("invalid alternate LBA");
		return (NULL);
	}
	hdr->hdr_entries = le32toh(hdr->hdr_entries);
	hdr->hdr_entsz = le32toh(hdr->hdr_entsz);
	if (hdr->hdr_entries == 0 ||
	    hdr->hdr_entsz < sizeof(struct gpt_ent) ||
	    sectorsize % hdr->hdr_entsz != 0) {
		DPRINTF("invalid entry size or number of entries");
		return (NULL);
	}
	hdr->hdr_lba_start = le64toh(hdr->hdr_lba_start);
	hdr->hdr_lba_end = le64toh(hdr->hdr_lba_end);
	hdr->hdr_lba_table = le64toh(hdr->hdr_lba_table);
	hdr->hdr_crc_table = le32toh(hdr->hdr_crc_table);
	uuid_letoh(&hdr->hdr_uuid);
	return (hdr);
}

static int
gpt_checktbl(const struct gpt_hdr *hdr, uint8_t *tbl, size_t size,
    uint64_t lba_last)
{
	struct gpt_ent *ent;
	uint32_t i, cnt;

	cnt = size / hdr->hdr_entsz;
	if (hdr->hdr_entries <= cnt) {
		cnt = hdr->hdr_entries;
		/* Check CRC only when buffer size is enough for table. */
		if (hdr->hdr_crc_table !=
		    crc32(tbl, hdr->hdr_entries * hdr->hdr_entsz)) {
			DPRINTF("GPT table's CRC doesn't match");
			return (-1);
		}
	}
	for (i = 0; i < cnt; i++) {
		ent = (struct gpt_ent *)(tbl + i * hdr->hdr_entsz);
		uuid_letoh(&ent->ent_type);
		if (uuid_equal(&ent->ent_type, &gpt_uuid_unused, NULL))
			continue;
		ent->ent_lba_start = le64toh(ent->ent_lba_start);
		ent->ent_lba_end = le64toh(ent->ent_lba_end);
	}
	return (0);
}

static struct ptable *
ptable_gptread(struct ptable *table, void *dev, diskread_t dread)
{
	struct pentry *entry;
	struct gpt_hdr *phdr, hdr;
	struct gpt_ent *ent;
	uint8_t *buf, *tbl;
	uint64_t offset;
	int pri, sec;
	size_t size, i;

	buf = malloc(table->sectorsize);
	if (buf == NULL)
		return (NULL);
	tbl = malloc(table->sectorsize * MAXTBLSZ);
	if (tbl == NULL) {
		free(buf);
		return (NULL);
	}
	/* Read the primary GPT header. */
	if (dread(dev, buf, 1, 1) != 0) {
		ptable_close(table);
		table = NULL;
		goto out;
	}
	pri = sec = 0;
	/* Check the primary GPT header. */
	phdr = gpt_checkhdr((struct gpt_hdr *)buf, 1, table->sectors - 1,
	    table->sectorsize);
	if (phdr != NULL) {
		/* Read the primary GPT table. */
		size = MIN(MAXTBLSZ,
		    howmany(phdr->hdr_entries * phdr->hdr_entsz,
		        table->sectorsize));
		if (dread(dev, tbl, size, phdr->hdr_lba_table) == 0 &&
		    gpt_checktbl(phdr, tbl, size * table->sectorsize,
		    table->sectors - 1) == 0) {
			memcpy(&hdr, phdr, sizeof(hdr));
			pri = 1;
		}
	}
	offset = pri ? hdr.hdr_lba_alt: table->sectors - 1;
	/* Read the backup GPT header. */
	if (dread(dev, buf, 1, offset) != 0)
		phdr = NULL;
	else
		phdr = gpt_checkhdr((struct gpt_hdr *)buf, offset,
		    table->sectors - 1, table->sectorsize);
	if (phdr != NULL) {
		/*
		 * Compare primary and backup headers.
		 * If they are equal, then we do not need to read backup
		 * table. If they are different, then prefer backup header
		 * and try to read backup table.
		 */
		if (pri == 0 ||
		    uuid_equal(&hdr.hdr_uuid, &phdr->hdr_uuid, NULL) == 0 ||
		    hdr.hdr_revision != phdr->hdr_revision ||
		    hdr.hdr_size != phdr->hdr_size ||
		    hdr.hdr_lba_start != phdr->hdr_lba_start ||
		    hdr.hdr_lba_end != phdr->hdr_lba_end ||
		    hdr.hdr_entries != phdr->hdr_entries ||
		    hdr.hdr_entsz != phdr->hdr_entsz ||
		    hdr.hdr_crc_table != phdr->hdr_crc_table) {
			/* Read the backup GPT table. */
			size = MIN(MAXTBLSZ,
				   howmany(phdr->hdr_entries * phdr->hdr_entsz,
				       table->sectorsize));
			if (dread(dev, tbl, size, phdr->hdr_lba_table) == 0 &&
			    gpt_checktbl(phdr, tbl, size * table->sectorsize,
			    table->sectors - 1) == 0) {
				memcpy(&hdr, phdr, sizeof(hdr));
				sec = 1;
			}
		}
	}
	if (pri == 0 && sec == 0) {
		/* Both primary and backup tables are invalid. */
		table->type = PTABLE_NONE;
		goto out;
	}
	DPRINTF("GPT detected");
	size = MIN(hdr.hdr_entries * hdr.hdr_entsz,
	    MAXTBLSZ * table->sectorsize);

	/*
	 * If the disk's sector count is smaller than the sector count recorded
	 * in the disk's GPT table header, set the table->sectors to the value
	 * recorded in GPT tables. This is done to work around buggy firmware
	 * that returns truncated disk sizes.
	 *
	 * Note, this is still not a foolproof way to get disk's size. For
	 * example, an image file can be truncated when copied to smaller media.
	 */
	table->sectors = hdr.hdr_lba_alt + 1;

	for (i = 0; i < size / hdr.hdr_entsz; i++) {
		ent = (struct gpt_ent *)(tbl + i * hdr.hdr_entsz);
		if (uuid_equal(&ent->ent_type, &gpt_uuid_unused, NULL))
			continue;

		/* Simple sanity checks. */
		if (ent->ent_lba_start < hdr.hdr_lba_start ||
		    ent->ent_lba_end > hdr.hdr_lba_end ||
		    ent->ent_lba_start > ent->ent_lba_end)
			continue;

		entry = malloc(sizeof(*entry));
		if (entry == NULL)
			break;
		entry->part.start = ent->ent_lba_start;
		entry->part.end = ent->ent_lba_end;
		entry->part.index = i + 1;
		entry->part.type = gpt_parttype(ent->ent_type);
		entry->flags = le64toh(ent->ent_attr);
		memcpy(&entry->type.gpt, &ent->ent_type, sizeof(uuid_t));
		STAILQ_INSERT_TAIL(&table->entries, entry, entry);
		DPRINTF("new GPT partition added");
	}
out:
	free(buf);
	free(tbl);
	return (table);
}
#endif /* LOADER_GPT_SUPPORT */

#ifdef LOADER_MBR_SUPPORT
/* We do not need to support too many EBR partitions in the loader */
#define	MAXEBRENTRIES		8
static enum partition_type
mbr_parttype(uint8_t type)
{

	switch (type) {
	case DOSPTYP_386BSD:
		return (PART_FREEBSD);
	case DOSPTYP_LINSWP:
		return (PART_LINUX_SWAP);
	case DOSPTYP_LINUX:
		return (PART_LINUX);
	case 0x01:
	case 0x04:
	case 0x06:
	case 0x07:
	case 0x0b:
	case 0x0c:
	case 0x0e:
		return (PART_DOS);
	}
	return (PART_UNKNOWN);
}

static struct ptable *
ptable_ebrread(struct ptable *table, void *dev, diskread_t dread)
{
	struct dos_partition *dp;
	struct pentry *e1, *entry;
	uint32_t start, end, offset;
	u_char *buf;
	int i, index;

	STAILQ_FOREACH(e1, &table->entries, entry) {
		if (e1->type.mbr == DOSPTYP_EXT ||
		    e1->type.mbr == DOSPTYP_EXTLBA)
			break;
	}
	if (e1 == NULL)
		return (table);
	index = 5;
	offset = e1->part.start;
	buf = malloc(table->sectorsize);
	if (buf == NULL)
		return (table);
	DPRINTF("EBR detected");
	for (i = 0; i < MAXEBRENTRIES; i++) {
#if 0	/* Some BIOSes return an incorrect number of sectors */
		if (offset >= table->sectors)
			break;
#endif
		if (dread(dev, buf, 1, offset) != 0)
			break;
		dp = (struct dos_partition *)(buf + DOSPARTOFF);
		if (dp[0].dp_typ == 0)
			break;
		start = le32toh(dp[0].dp_start);
		if (dp[0].dp_typ == DOSPTYP_EXT &&
		    dp[1].dp_typ == 0) {
			offset = e1->part.start + start;
			continue;
		}
		end = le32toh(dp[0].dp_size);
		entry = malloc(sizeof(*entry));
		if (entry == NULL)
			break;
		entry->part.start = offset + start;
		entry->part.end = entry->part.start + end - 1;
		entry->part.index = index++;
		entry->part.type = mbr_parttype(dp[0].dp_typ);
		entry->flags = dp[0].dp_flag;
		entry->type.mbr = dp[0].dp_typ;
		STAILQ_INSERT_TAIL(&table->entries, entry, entry);
		DPRINTF("new EBR partition added");
		if (dp[1].dp_typ == 0)
			break;
		offset = e1->part.start + le32toh(dp[1].dp_start);
	}
	free(buf);
	return (table);
}
#endif /* LOADER_MBR_SUPPORT */

static enum partition_type
bsd_parttype(uint8_t type)
{

	switch (type) {
	case FS_NANDFS:
		return (PART_FREEBSD_NANDFS);
	case FS_SWAP:
		return (PART_FREEBSD_SWAP);
	case FS_BSDFFS:
		return (PART_FREEBSD_UFS);
	case FS_VINUM:
		return (PART_FREEBSD_VINUM);
	case FS_ZFS:
		return (PART_FREEBSD_ZFS);
	}
	return (PART_UNKNOWN);
}

static struct ptable *
ptable_bsdread(struct ptable *table, void *dev, diskread_t dread)
{
	struct disklabel *dl;
	struct partition *part;
	struct pentry *entry;
	uint8_t *buf;
	uint32_t raw_offset;
	int i;

	if (table->sectorsize < sizeof(struct disklabel)) {
		DPRINTF("Too small sectorsize");
		return (table);
	}
	buf = malloc(table->sectorsize);
	if (buf == NULL)
		return (table);
	if (dread(dev, buf, 1, 1) != 0) {
		DPRINTF("read failed");
		ptable_close(table);
		table = NULL;
		goto out;
	}
	dl = (struct disklabel *)buf;
	if (le32toh(dl->d_magic) != DISKMAGIC &&
	    le32toh(dl->d_magic2) != DISKMAGIC)
		goto out;
	if (le32toh(dl->d_secsize) != table->sectorsize) {
		DPRINTF("unsupported sector size");
		goto out;
	}
	dl->d_npartitions = le16toh(dl->d_npartitions);
	if (dl->d_npartitions > 20 || dl->d_npartitions < 8) {
		DPRINTF("invalid number of partitions");
		goto out;
	}
	DPRINTF("BSD detected");
	part = &dl->d_partitions[0];
	raw_offset = le32toh(part[RAW_PART].p_offset);
	for (i = 0; i < dl->d_npartitions; i++, part++) {
		if (i == RAW_PART)
			continue;
		if (part->p_size == 0)
			continue;
		entry = malloc(sizeof(*entry));
		if (entry == NULL)
			break;
		entry->part.start = le32toh(part->p_offset) - raw_offset;
		entry->part.end = entry->part.start +
		    le32toh(part->p_size) - 1;
		entry->part.type = bsd_parttype(part->p_fstype);
		entry->part.index = i; /* starts from zero */
		entry->type.bsd = part->p_fstype;
		STAILQ_INSERT_TAIL(&table->entries, entry, entry);
		DPRINTF("new BSD partition added");
	}
	table->type = PTABLE_BSD;
out:
	free(buf);
	return (table);
}

#ifdef LOADER_VTOC8_SUPPORT
static enum partition_type
vtoc8_parttype(uint16_t type)
{

	switch (type) {
	case VTOC_TAG_FREEBSD_NANDFS:
		return (PART_FREEBSD_NANDFS);
	case VTOC_TAG_FREEBSD_SWAP:
		return (PART_FREEBSD_SWAP);
	case VTOC_TAG_FREEBSD_UFS:
		return (PART_FREEBSD_UFS);
	case VTOC_TAG_FREEBSD_VINUM:
		return (PART_FREEBSD_VINUM);
	case VTOC_TAG_FREEBSD_ZFS:
		return (PART_FREEBSD_ZFS);
	}
	return (PART_UNKNOWN);
}

static struct ptable *
ptable_vtoc8read(struct ptable *table, void *dev, diskread_t dread)
{
	struct pentry *entry;
	struct vtoc8 *dl;
	uint8_t *buf;
	uint16_t sum, heads, sectors;
	int i;

	if (table->sectorsize != sizeof(struct vtoc8))
		return (table);
	buf = malloc(table->sectorsize);
	if (buf == NULL)
		return (table);
	if (dread(dev, buf, 1, 0) != 0) {
		DPRINTF("read failed");
		ptable_close(table);
		table = NULL;
		goto out;
	}
	dl = (struct vtoc8 *)buf;
	/* Check the sum */
	for (i = sum = 0; i < sizeof(struct vtoc8); i += sizeof(sum))
		sum ^= be16dec(buf + i);
	if (sum != 0) {
		DPRINTF("incorrect checksum");
		goto out;
	}
	if (be16toh(dl->nparts) != VTOC8_NPARTS) {
		DPRINTF("invalid number of entries");
		goto out;
	}
	sectors = be16toh(dl->nsecs);
	heads = be16toh(dl->nheads);
	if (sectors * heads == 0) {
		DPRINTF("invalid geometry");
		goto out;
	}
	DPRINTF("VTOC8 detected");
	for (i = 0; i < VTOC8_NPARTS; i++) {
		dl->part[i].tag = be16toh(dl->part[i].tag);
		if (i == VTOC_RAW_PART ||
		    dl->part[i].tag == VTOC_TAG_UNASSIGNED)
			continue;
		entry = malloc(sizeof(*entry));
		if (entry == NULL)
			break;
		entry->part.start = be32toh(dl->map[i].cyl) * heads * sectors;
		entry->part.end = be32toh(dl->map[i].nblks) +
		    entry->part.start - 1;
		entry->part.type = vtoc8_parttype(dl->part[i].tag);
		entry->part.index = i; /* starts from zero */
		entry->type.vtoc8 = dl->part[i].tag;
		STAILQ_INSERT_TAIL(&table->entries, entry, entry);
		DPRINTF("new VTOC8 partition added");
	}
	table->type = PTABLE_VTOC8;
out:
	free(buf);
	return (table);

}
#endif /* LOADER_VTOC8_SUPPORT */

#define cdb2devb(bno)   ((bno) * ISO_DEFAULT_BLOCK_SIZE / table->sectorsize)

static struct ptable *
ptable_iso9660read(struct ptable *table, void *dev, diskread_t dread)
{
	uint8_t *buf;
	struct iso_primary_descriptor *vd;
	struct pentry *entry;

	buf = malloc(table->sectorsize);
	if (buf == NULL)
		return (table);
		
	if (dread(dev, buf, 1, cdb2devb(16)) != 0) {
		DPRINTF("read failed");
		ptable_close(table);
		table = NULL;
		goto out;
	}
	vd = (struct iso_primary_descriptor *)buf;
	if (bcmp(vd->id, ISO_STANDARD_ID, sizeof vd->id) != 0)
		goto out;

	entry = malloc(sizeof(*entry));
	if (entry == NULL)
		goto out;
	entry->part.start = 0;
	entry->part.end = table->sectors;
	entry->part.type = PART_ISO9660;
	entry->part.index = 0;
	STAILQ_INSERT_TAIL(&table->entries, entry, entry);

	table->type = PTABLE_ISO9660;

out:
	free(buf);
	return (table);
}

struct ptable *
ptable_open(void *dev, uint64_t sectors, uint16_t sectorsize,
    diskread_t *dread)
{
	struct dos_partition *dp;
	struct ptable *table;
	uint8_t *buf;
	int i, count;
#ifdef LOADER_MBR_SUPPORT
	struct pentry *entry;
	uint32_t start, end;
	int has_ext;
#endif
	table = NULL;
	buf = malloc(sectorsize);
	if (buf == NULL)
		return (NULL);
	/* First, read the MBR. */
	if (dread(dev, buf, 1, DOSBBSECTOR) != 0) {
		DPRINTF("read failed");
		goto out;
	}

	table = malloc(sizeof(*table));
	if (table == NULL)
		goto out;
	table->sectors = sectors;
	table->sectorsize = sectorsize;
	table->type = PTABLE_NONE;
	STAILQ_INIT(&table->entries);

	if (ptable_iso9660read(table, dev, dread) == NULL) {
		/* Read error. */
		table = NULL;
		goto out;
	} else if (table->type == PTABLE_ISO9660)
		goto out;

#ifdef LOADER_VTOC8_SUPPORT
	if (be16dec(buf + offsetof(struct vtoc8, magic)) == VTOC_MAGIC) {
		if (ptable_vtoc8read(table, dev, dread) == NULL) {
			/* Read error. */
			table = NULL;
			goto out;
		} else if (table->type == PTABLE_VTOC8)
			goto out;
	}
#endif
	/* Check the BSD label. */
	if (ptable_bsdread(table, dev, dread) == NULL) { /* Read error. */
		table = NULL;
		goto out;
	} else if (table->type == PTABLE_BSD)
		goto out;

#if defined(LOADER_GPT_SUPPORT) || defined(LOADER_MBR_SUPPORT)
	/* Check the MBR magic. */
	if (buf[DOSMAGICOFFSET] != 0x55 ||
	    buf[DOSMAGICOFFSET + 1] != 0xaa) {
		DPRINTF("magic sequence not found");
#if defined(LOADER_GPT_SUPPORT)
		/* There is no PMBR, check that we have backup GPT */
		table->type = PTABLE_GPT;
		table = ptable_gptread(table, dev, dread);
#endif
		goto out;
	}
	/* Check that we have PMBR. Also do some validation. */
	dp = (struct dos_partition *)(buf + DOSPARTOFF);
	for (i = 0, count = 0; i < NDOSPART; i++) {
		if (dp[i].dp_flag != 0 && dp[i].dp_flag != 0x80) {
			DPRINTF("invalid partition flag %x", dp[i].dp_flag);
			goto out;
		}
#ifdef LOADER_GPT_SUPPORT
		if (dp[i].dp_typ == DOSPTYP_PMBR) {
			table->type = PTABLE_GPT;
			DPRINTF("PMBR detected");
		}
#endif
		if (dp[i].dp_typ != 0)
			count++;
	}
	/* Do we have some invalid values? */
	if (table->type == PTABLE_GPT && count > 1) {
		if (dp[1].dp_typ != DOSPTYP_HFS) {
			table->type = PTABLE_NONE;
			DPRINTF("Incorrect PMBR, ignore it");
		} else {
			DPRINTF("Bootcamp detected");
		}
	}
#ifdef LOADER_GPT_SUPPORT
	if (table->type == PTABLE_GPT) {
		table = ptable_gptread(table, dev, dread);
		goto out;
	}
#endif
#ifdef LOADER_MBR_SUPPORT
	/* Read MBR. */
	DPRINTF("MBR detected");
	table->type = PTABLE_MBR;
	for (i = has_ext = 0; i < NDOSPART; i++) {
		if (dp[i].dp_typ == 0)
			continue;
		start = le32dec(&(dp[i].dp_start));
		end = le32dec(&(dp[i].dp_size));
		if (start == 0 || end == 0)
			continue;
#if 0	/* Some BIOSes return an incorrect number of sectors */
		if (start + end - 1 >= sectors)
			continue;	/* XXX: ignore */
#endif
		if (dp[i].dp_typ == DOSPTYP_EXT ||
		    dp[i].dp_typ == DOSPTYP_EXTLBA)
			has_ext = 1;
		entry = malloc(sizeof(*entry));
		if (entry == NULL)
			break;
		entry->part.start = start;
		entry->part.end = start + end - 1;
		entry->part.index = i + 1;
		entry->part.type = mbr_parttype(dp[i].dp_typ);
		entry->flags = dp[i].dp_flag;
		entry->type.mbr = dp[i].dp_typ;
		STAILQ_INSERT_TAIL(&table->entries, entry, entry);
		DPRINTF("new MBR partition added");
	}
	if (has_ext) {
		table = ptable_ebrread(table, dev, dread);
		/* FALLTHROUGH */
	}
#endif /* LOADER_MBR_SUPPORT */
#endif /* LOADER_MBR_SUPPORT || LOADER_GPT_SUPPORT */
out:
	free(buf);
	return (table);
}

void
ptable_close(struct ptable *table)
{
	struct pentry *entry;

	if (table == NULL)
		return;

	while (!STAILQ_EMPTY(&table->entries)) {
		entry = STAILQ_FIRST(&table->entries);
		STAILQ_REMOVE_HEAD(&table->entries, entry);
		free(entry);
	}
	free(table);
}

enum ptable_type
ptable_gettype(const struct ptable *table)
{

	return (table->type);
}

int
ptable_getsize(const struct ptable *table, uint64_t *sizep)
{
	uint64_t tmp = table->sectors * table->sectorsize;

	if (tmp < table->sectors)
		return (EOVERFLOW);

	if (sizep != NULL)
		*sizep = tmp;
	return (0);
}

int
ptable_getpart(const struct ptable *table, struct ptable_entry *part, int index)
{
	struct pentry *entry;

	if (part == NULL || table == NULL)
		return (EINVAL);

	STAILQ_FOREACH(entry, &table->entries, entry) {
		if (entry->part.index != index)
			continue;
		memcpy(part, &entry->part, sizeof(*part));
		return (0);
	}
	return (ENOENT);
}

/*
 * Search for a slice with the following preferences:
 *
 * 1: Active FreeBSD slice
 * 2: Non-active FreeBSD slice
 * 3: Active Linux slice
 * 4: non-active Linux slice
 * 5: Active FAT/FAT32 slice
 * 6: non-active FAT/FAT32 slice
 */
#define	PREF_RAWDISK	0
#define	PREF_FBSD_ACT	1
#define	PREF_FBSD	2
#define	PREF_LINUX_ACT	3
#define	PREF_LINUX	4
#define	PREF_DOS_ACT	5
#define	PREF_DOS	6
#define	PREF_NONE	7
int
ptable_getbestpart(const struct ptable *table, struct ptable_entry *part)
{
	struct pentry *entry, *best;
	int pref, preflevel;

	if (part == NULL || table == NULL)
		return (EINVAL);

	best = NULL;
	preflevel = pref = PREF_NONE;
	STAILQ_FOREACH(entry, &table->entries, entry) {
#ifdef LOADER_MBR_SUPPORT
		if (table->type == PTABLE_MBR) {
			switch (entry->type.mbr) {
			case DOSPTYP_386BSD:
				pref = entry->flags & 0x80 ? PREF_FBSD_ACT:
				    PREF_FBSD;
				break;
			case DOSPTYP_LINUX:
				pref = entry->flags & 0x80 ? PREF_LINUX_ACT:
				    PREF_LINUX;
				break;
			case 0x01:		/* DOS/Windows */
			case 0x04:
			case 0x06:
			case 0x0c:
			case 0x0e:
			case DOSPTYP_FAT32:
				pref = entry->flags & 0x80 ? PREF_DOS_ACT:
				    PREF_DOS;
				break;
			default:
				pref = PREF_NONE;
			}
		}
#endif /* LOADER_MBR_SUPPORT */
#ifdef LOADER_GPT_SUPPORT
		if (table->type == PTABLE_GPT) {
			if (entry->part.type == PART_DOS)
				pref = PREF_DOS;
			else if (entry->part.type == PART_FREEBSD_UFS ||
			    entry->part.type == PART_FREEBSD_ZFS)
				pref = PREF_FBSD;
			else
				pref = PREF_NONE;
		}
#endif /* LOADER_GPT_SUPPORT */
		if (pref < preflevel) {
			preflevel = pref;
			best = entry;
		}
	}
	if (best != NULL) {
		memcpy(part, &best->part, sizeof(*part));
		return (0);
	}
	return (ENOENT);
}

int
ptable_iterate(const struct ptable *table, void *arg, ptable_iterate_t *iter)
{
	struct pentry *entry;
	char name[32];
	int ret = 0;

	name[0] = '\0';
	STAILQ_FOREACH(entry, &table->entries, entry) {
#ifdef LOADER_MBR_SUPPORT
		if (table->type == PTABLE_MBR)
			sprintf(name, "s%d", entry->part.index);
		else
#endif
#ifdef LOADER_GPT_SUPPORT
		if (table->type == PTABLE_GPT)
			sprintf(name, "p%d", entry->part.index);
		else
#endif
#ifdef LOADER_VTOC8_SUPPORT
		if (table->type == PTABLE_VTOC8)
			sprintf(name, "%c", (uint8_t) 'a' +
			    entry->part.index);
		else
#endif
		if (table->type == PTABLE_BSD)
			sprintf(name, "%c", (uint8_t) 'a' +
			    entry->part.index);
		if ((ret = iter(arg, name, &entry->part)) != 0)
			return (ret);
	}
	return (ret);
}
