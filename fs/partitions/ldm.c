/**
 * ldm - Support for Windows Logical Disk Manager (Dynamic Disks)
 *
 * Copyright (C) 2001,2002 Richard Russon <ldm@flatcap.org>
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (C) 2001,2002 Jakob Kemi <jakob.kemi@telia.com>
 *
 * Documentation is available at http://www.linux-ntfs.org/content/view/19/37/
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program (in the main directory of the source in the file COPYING); if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA  02111-1307  USA
 */

#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/stringify.h>
#include "ldm.h"
#include "check.h"
#include "msdos.h"

/**
 * ldm_debug/info/error/crit - Output an error message
 * @f:    A printf format string containing the message
 * @...:  Variables to substitute into @f
 *
 * ldm_debug() writes a DEBUG level message to the syslog but only if the
 * driver was compiled with debug enabled. Otherwise, the call turns into a NOP.
 */
#ifndef CONFIG_LDM_DEBUG
#define ldm_debug(...)	do {} while (0)
#else
#define ldm_debug(f, a...) _ldm_printk (KERN_DEBUG, __func__, f, ##a)
#endif

#define ldm_crit(f, a...)  _ldm_printk (KERN_CRIT,  __func__, f, ##a)
#define ldm_error(f, a...) _ldm_printk (KERN_ERR,   __func__, f, ##a)
#define ldm_info(f, a...)  _ldm_printk (KERN_INFO,  __func__, f, ##a)

__attribute__ ((format (printf, 3, 4)))
static void _ldm_printk (const char *level, const char *function,
			 const char *fmt, ...)
{
	static char buf[128];
	va_list args;

	va_start (args, fmt);
	vsnprintf (buf, sizeof (buf), fmt, args);
	va_end (args);

	printk ("%s%s(): %s\n", level, function, buf);
}

/**
 * ldm_parse_hexbyte - Convert a ASCII hex number to a byte
 * @src:  Pointer to at least 2 characters to convert.
 *
 * Convert a two character ASCII hex string to a number.
 *
 * Return:  0-255  Success, the byte was parsed correctly
 *          -1     Error, an invalid character was supplied
 */
static int ldm_parse_hexbyte (const u8 *src)
{
	unsigned int x;		/* For correct wrapping */
	int h;

	/* high part */
	if      ((x = src[0] - '0') <= '9'-'0') h = x;
	else if ((x = src[0] - 'a') <= 'f'-'a') h = x+10;
	else if ((x = src[0] - 'A') <= 'F'-'A') h = x+10;
	else return -1;
	h <<= 4;

	/* low part */
	if ((x = src[1] - '0') <= '9'-'0') return h | x;
	if ((x = src[1] - 'a') <= 'f'-'a') return h | (x+10);
	if ((x = src[1] - 'A') <= 'F'-'A') return h | (x+10);
	return -1;
}

/**
 * ldm_parse_guid - Convert GUID from ASCII to binary
 * @src:   36 char string of the form fa50ff2b-f2e8-45de-83fa-65417f2f49ba
 * @dest:  Memory block to hold binary GUID (16 bytes)
 *
 * N.B. The GUID need not be NULL terminated.
 *
 * Return:  'true'   @dest contains binary GUID
 *          'false'  @dest contents are undefined
 */
static bool ldm_parse_guid (const u8 *src, u8 *dest)
{
	static const int size[] = { 4, 2, 2, 2, 6 };
	int i, j, v;

	if (src[8]  != '-' || src[13] != '-' ||
	    src[18] != '-' || src[23] != '-')
		return false;

	for (j = 0; j < 5; j++, src++)
		for (i = 0; i < size[j]; i++, src+=2, *dest++ = v)
			if ((v = ldm_parse_hexbyte (src)) < 0)
				return false;

	return true;
}

/**
 * ldm_parse_privhead - Read the LDM Database PRIVHEAD structure
 * @data:  Raw database PRIVHEAD structure loaded from the device
 * @ph:    In-memory privhead structure in which to return parsed information
 *
 * This parses the LDM database PRIVHEAD structure supplied in @data and
 * sets up the in-memory privhead structure @ph with the obtained information.
 *
 * Return:  'true'   @ph contains the PRIVHEAD data
 *          'false'  @ph contents are undefined
 */
static bool ldm_parse_privhead(const u8 *data, struct privhead *ph)
{
	bool is_vista = false;

	BUG_ON(!data || !ph);
	if (MAGIC_PRIVHEAD != get_unaligned_be64(data)) {
		ldm_error("Cannot find PRIVHEAD structure. LDM database is"
			" corrupt. Aborting.");
		return false;
	}
	ph->ver_major = get_unaligned_be16(data + 0x000C);
	ph->ver_minor = get_unaligned_be16(data + 0x000E);
	ph->logical_disk_start = get_unaligned_be64(data + 0x011B);
	ph->logical_disk_size = get_unaligned_be64(data + 0x0123);
	ph->config_start = get_unaligned_be64(data + 0x012B);
	ph->config_size = get_unaligned_be64(data + 0x0133);
	/* Version 2.11 is Win2k/XP and version 2.12 is Vista. */
	if (ph->ver_major == 2 && ph->ver_minor == 12)
		is_vista = true;
	if (!is_vista && (ph->ver_major != 2 || ph->ver_minor != 11)) {
		ldm_error("Expected PRIVHEAD version 2.11 or 2.12, got %d.%d."
			" Aborting.", ph->ver_major, ph->ver_minor);
		return false;
	}
	ldm_debug("PRIVHEAD version %d.%d (Windows %s).", ph->ver_major,
			ph->ver_minor, is_vista ? "Vista" : "2000/XP");
	if (ph->config_size != LDM_DB_SIZE) {	/* 1 MiB in sectors. */
		/* Warn the user and continue, carefully. */
		ldm_info("Database is normally %u bytes, it claims to "
			"be %llu bytes.", LDM_DB_SIZE,
			(unsigned long long)ph->config_size);
	}
	if ((ph->logical_disk_size == 0) || (ph->logical_disk_start +
			ph->logical_disk_size > ph->config_start)) {
		ldm_error("PRIVHEAD disk size doesn't match real disk size");
		return false;
	}
	if (!ldm_parse_guid(data + 0x0030, ph->disk_id)) {
		ldm_error("PRIVHEAD contains an invalid GUID.");
		return false;
	}
	ldm_debug("Parsed PRIVHEAD successfully.");
	return true;
}

/**
 * ldm_parse_tocblock - Read the LDM Database TOCBLOCK structure
 * @data:  Raw database TOCBLOCK structure loaded from the device
 * @toc:   In-memory toc structure in which to return parsed information
 *
 * This parses the LDM Database TOCBLOCK (table of contents) structure supplied
 * in @data and sets up the in-memory tocblock structure @toc with the obtained
 * information.
 *
 * N.B.  The *_start and *_size values returned in @toc are not range-checked.
 *
 * Return:  'true'   @toc contains the TOCBLOCK data
 *          'false'  @toc contents are undefined
 */
static bool ldm_parse_tocblock (const u8 *data, struct tocblock *toc)
{
	BUG_ON (!data || !toc);

	if (MAGIC_TOCBLOCK != get_unaligned_be64(data)) {
		ldm_crit ("Cannot find TOCBLOCK, database may be corrupt.");
		return false;
	}
	strncpy (toc->bitmap1_name, data + 0x24, sizeof (toc->bitmap1_name));
	toc->bitmap1_name[sizeof (toc->bitmap1_name) - 1] = 0;
	toc->bitmap1_start = get_unaligned_be64(data + 0x2E);
	toc->bitmap1_size  = get_unaligned_be64(data + 0x36);

	if (strncmp (toc->bitmap1_name, TOC_BITMAP1,
			sizeof (toc->bitmap1_name)) != 0) {
		ldm_crit ("TOCBLOCK's first bitmap is '%s', should be '%s'.",
				TOC_BITMAP1, toc->bitmap1_name);
		return false;
	}
	strncpy (toc->bitmap2_name, data + 0x46, sizeof (toc->bitmap2_name));
	toc->bitmap2_name[sizeof (toc->bitmap2_name) - 1] = 0;
	toc->bitmap2_start = get_unaligned_be64(data + 0x50);
	toc->bitmap2_size  = get_unaligned_be64(data + 0x58);
	if (strncmp (toc->bitmap2_name, TOC_BITMAP2,
			sizeof (toc->bitmap2_name)) != 0) {
		ldm_crit ("TOCBLOCK's second bitmap is '%s', should be '%s'.",
				TOC_BITMAP2, toc->bitmap2_name);
		return false;
	}
	ldm_debug ("Parsed TOCBLOCK successfully.");
	return true;
}

/**
 * ldm_parse_vmdb - Read the LDM Database VMDB structure
 * @data:  Raw database VMDB structure loaded from the device
 * @vm:    In-memory vmdb structure in which to return parsed information
 *
 * This parses the LDM Database VMDB structure supplied in @data and sets up
 * the in-memory vmdb structure @vm with the obtained information.
 *
 * N.B.  The *_start, *_size and *_seq values will be range-checked later.
 *
 * Return:  'true'   @vm contains VMDB info
 *          'false'  @vm contents are undefined
 */
static bool ldm_parse_vmdb (const u8 *data, struct vmdb *vm)
{
	BUG_ON (!data || !vm);

	if (MAGIC_VMDB != get_unaligned_be32(data)) {
		ldm_crit ("Cannot find the VMDB, database may be corrupt.");
		return false;
	}

	vm->ver_major = get_unaligned_be16(data + 0x12);
	vm->ver_minor = get_unaligned_be16(data + 0x14);
	if ((vm->ver_major != 4) || (vm->ver_minor != 10)) {
		ldm_error ("Expected VMDB version %d.%d, got %d.%d. "
			"Aborting.", 4, 10, vm->ver_major, vm->ver_minor);
		return false;
	}

	vm->vblk_size     = get_unaligned_be32(data + 0x08);
	if (vm->vblk_size == 0) {
		ldm_error ("Illegal VBLK size");
		return false;
	}

	vm->vblk_offset   = get_unaligned_be32(data + 0x0C);
	vm->last_vblk_seq = get_unaligned_be32(data + 0x04);

	ldm_debug ("Parsed VMDB successfully.");
	return true;
}

/**
 * ldm_compare_privheads - Compare two privhead objects
 * @ph1:  First privhead
 * @ph2:  Second privhead
 *
 * This compares the two privhead structures @ph1 and @ph2.
 *
 * Return:  'true'   Identical
 *          'false'  Different
 */
static bool ldm_compare_privheads (const struct privhead *ph1,
				   const struct privhead *ph2)
{
	BUG_ON (!ph1 || !ph2);

	return ((ph1->ver_major          == ph2->ver_major)		&&
		(ph1->ver_minor          == ph2->ver_minor)		&&
		(ph1->logical_disk_start == ph2->logical_disk_start)	&&
		(ph1->logical_disk_size  == ph2->logical_disk_size)	&&
		(ph1->config_start       == ph2->config_start)		&&
		(ph1->config_size        == ph2->config_size)		&&
		!memcmp (ph1->disk_id, ph2->disk_id, GUID_SIZE));
}

/**
 * ldm_compare_tocblocks - Compare two tocblock objects
 * @toc1:  First toc
 * @toc2:  Second toc
 *
 * This compares the two tocblock structures @toc1 and @toc2.
 *
 * Return:  'true'   Identical
 *          'false'  Different
 */
static bool ldm_compare_tocblocks (const struct tocblock *toc1,
				   const struct tocblock *toc2)
{
	BUG_ON (!toc1 || !toc2);

	return ((toc1->bitmap1_start == toc2->bitmap1_start)	&&
		(toc1->bitmap1_size  == toc2->bitmap1_size)	&&
		(toc1->bitmap2_start == toc2->bitmap2_start)	&&
		(toc1->bitmap2_size  == toc2->bitmap2_size)	&&
		!strncmp (toc1->bitmap1_name, toc2->bitmap1_name,
			sizeof (toc1->bitmap1_name))		&&
		!strncmp (toc1->bitmap2_name, toc2->bitmap2_name,
			sizeof (toc1->bitmap2_name)));
}

/**
 * ldm_validate_privheads - Compare the primary privhead with its backups
 * @bdev:  Device holding the LDM Database
 * @ph1:   Memory struct to fill with ph contents
 *
 * Read and compare all three privheads from disk.
 *
 * The privheads on disk show the size and location of the main disk area and
 * the configuration area (the database).  The values are range-checked against
 * @hd, which contains the real size of the disk.
 *
 * Return:  'true'   Success
 *          'false'  Error
 */
static bool ldm_validate_privheads (struct block_device *bdev,
				    struct privhead *ph1)
{
	static const int off[3] = { OFF_PRIV1, OFF_PRIV2, OFF_PRIV3 };
	struct privhead *ph[3] = { ph1 };
	Sector sect;
	u8 *data;
	bool result = false;
	long num_sects;
	int i;

	BUG_ON (!bdev || !ph1);

	ph[1] = kmalloc (sizeof (*ph[1]), GFP_KERNEL);
	ph[2] = kmalloc (sizeof (*ph[2]), GFP_KERNEL);
	if (!ph[1] || !ph[2]) {
		ldm_crit ("Out of memory.");
		goto out;
	}

	/* off[1 & 2] are relative to ph[0]->config_start */
	ph[0]->config_start = 0;

	/* Read and parse privheads */
	for (i = 0; i < 3; i++) {
		data = read_dev_sector (bdev,
			ph[0]->config_start + off[i], &sect);
		if (!data) {
			ldm_crit ("Disk read failed.");
			goto out;
		}
		result = ldm_parse_privhead (data, ph[i]);
		put_dev_sector (sect);
		if (!result) {
			ldm_error ("Cannot find PRIVHEAD %d.", i+1); /* Log again */
			if (i < 2)
				goto out;	/* Already logged */
			else
				break;	/* FIXME ignore for now, 3rd PH can fail on odd-sized disks */
		}
	}

	num_sects = bdev->bd_inode->i_size >> 9;

	if ((ph[0]->config_start > num_sects) ||
	   ((ph[0]->config_start + ph[0]->config_size) > num_sects)) {
		ldm_crit ("Database extends beyond the end of the disk.");
		goto out;
	}

	if ((ph[0]->logical_disk_start > ph[0]->config_start) ||
	   ((ph[0]->logical_disk_start + ph[0]->logical_disk_size)
		    > ph[0]->config_start)) {
		ldm_crit ("Disk and database overlap.");
		goto out;
	}

	if (!ldm_compare_privheads (ph[0], ph[1])) {
		ldm_crit ("Primary and backup PRIVHEADs don't match.");
		goto out;
	}
	/* FIXME ignore this for now
	if (!ldm_compare_privheads (ph[0], ph[2])) {
		ldm_crit ("Primary and backup PRIVHEADs don't match.");
		goto out;
	}*/
	ldm_debug ("Validated PRIVHEADs successfully.");
	result = true;
out:
	kfree (ph[1]);
	kfree (ph[2]);
	return result;
}

/**
 * ldm_validate_tocblocks - Validate the table of contents and its backups
 * @bdev:  Device holding the LDM Database
 * @base:  Offset, into @bdev, of the database
 * @ldb:   Cache of the database structures
 *
 * Find and compare the four tables of contents of the LDM Database stored on
 * @bdev and return the parsed information into @toc1.
 *
 * The offsets and sizes of the configs are range-checked against a privhead.
 *
 * Return:  'true'   @toc1 contains validated TOCBLOCK info
 *          'false'  @toc1 contents are undefined
 */
static bool ldm_validate_tocblocks(struct block_device *bdev,
	unsigned long base, struct ldmdb *ldb)
{
	static const int off[4] = { OFF_TOCB1, OFF_TOCB2, OFF_TOCB3, OFF_TOCB4};
	struct tocblock *tb[4];
	struct privhead *ph;
	Sector sect;
	u8 *data;
	int i, nr_tbs;
	bool result = false;

	BUG_ON(!bdev || !ldb);
	ph = &ldb->ph;
	tb[0] = &ldb->toc;
	tb[1] = kmalloc(sizeof(*tb[1]) * 3, GFP_KERNEL);
	if (!tb[1]) {
		ldm_crit("Out of memory.");
		goto err;
	}
	tb[2] = (struct tocblock*)((u8*)tb[1] + sizeof(*tb[1]));
	tb[3] = (struct tocblock*)((u8*)tb[2] + sizeof(*tb[2]));
	/*
	 * Try to read and parse all four TOCBLOCKs.
	 *
	 * Windows Vista LDM v2.12 does not always have all four TOCBLOCKs so
	 * skip any that fail as long as we get at least one valid TOCBLOCK.
	 */
	for (nr_tbs = i = 0; i < 4; i++) {
		data = read_dev_sector(bdev, base + off[i], &sect);
		if (!data) {
			ldm_error("Disk read failed for TOCBLOCK %d.", i);
			continue;
		}
		if (ldm_parse_tocblock(data, tb[nr_tbs]))
			nr_tbs++;
		put_dev_sector(sect);
	}
	if (!nr_tbs) {
		ldm_crit("Failed to find a valid TOCBLOCK.");
		goto err;
	}
	/* Range check the TOCBLOCK against a privhead. */
	if (((tb[0]->bitmap1_start + tb[0]->bitmap1_size) > ph->config_size) ||
			((tb[0]->bitmap2_start + tb[0]->bitmap2_size) >
			ph->config_size)) {
		ldm_crit("The bitmaps are out of range.  Giving up.");
		goto err;
	}
	/* Compare all loaded TOCBLOCKs. */
	for (i = 1; i < nr_tbs; i++) {
		if (!ldm_compare_tocblocks(tb[0], tb[i])) {
			ldm_crit("TOCBLOCKs 0 and %d do not match.", i);
			goto err;
		}
	}
	ldm_debug("Validated %d TOCBLOCKs successfully.", nr_tbs);
	result = true;
err:
	kfree(tb[1]);
	return result;
}

/**
 * ldm_validate_vmdb - Read the VMDB and validate it
 * @bdev:  Device holding the LDM Database
 * @base:  Offset, into @bdev, of the database
 * @ldb:   Cache of the database structures
 *
 * Find the vmdb of the LDM Database stored on @bdev and return the parsed
 * information in @ldb.
 *
 * Return:  'true'   @ldb contains validated VBDB info
 *          'false'  @ldb contents are undefined
 */
static bool ldm_validate_vmdb (struct block_device *bdev, unsigned long base,
			       struct ldmdb *ldb)
{
	Sector sect;
	u8 *data;
	bool result = false;
	struct vmdb *vm;
	struct tocblock *toc;

	BUG_ON (!bdev || !ldb);

	vm  = &ldb->vm;
	toc = &ldb->toc;

	data = read_dev_sector (bdev, base + OFF_VMDB, &sect);
	if (!data) {
		ldm_crit ("Disk read failed.");
		return false;
	}

	if (!ldm_parse_vmdb (data, vm))
		goto out;				/* Already logged */

	/* Are there uncommitted transactions? */
	if (get_unaligned_be16(data + 0x10) != 0x01) {
		ldm_crit ("Database is not in a consistent state.  Aborting.");
		goto out;
	}

	if (vm->vblk_offset != 512)
		ldm_info ("VBLKs start at offset 0x%04x.", vm->vblk_offset);

	/*
	 * The last_vblkd_seq can be before the end of the vmdb, just make sure
	 * it is not out of bounds.
	 */
	if ((vm->vblk_size * vm->last_vblk_seq) > (toc->bitmap1_size << 9)) {
		ldm_crit ("VMDB exceeds allowed size specified by TOCBLOCK.  "
				"Database is corrupt.  Aborting.");
		goto out;
	}

	result = true;
out:
	put_dev_sector (sect);
	return result;
}


/**
 * ldm_validate_partition_table - Determine whether bdev might be a dynamic disk
 * @bdev:  Device holding the LDM Database
 *
 * This function provides a weak test to decide whether the device is a dynamic
 * disk or not.  It looks for an MS-DOS-style partition table containing at
 * least one partition of type 0x42 (formerly SFS, now used by Windows for
 * dynamic disks).
 *
 * N.B.  The only possible error can come from the read_dev_sector and that is
 *       only likely to happen if the underlying device is strange.  If that IS
 *       the case we should return zero to let someone else try.
 *
 * Return:  'true'   @bdev is a dynamic disk
 *          'false'  @bdev is not a dynamic disk, or an error occurred
 */
static bool ldm_validate_partition_table (struct block_device *bdev)
{
	Sector sect;
	u8 *data;
	struct partition *p;
	int i;
	bool result = false;

	BUG_ON (!bdev);

	data = read_dev_sector (bdev, 0, &sect);
	if (!data) {
		ldm_crit ("Disk read failed.");
		return false;
	}

	if (*(__le16*) (data + 0x01FE) != cpu_to_le16 (MSDOS_LABEL_MAGIC))
		goto out;

	p = (struct partition*)(data + 0x01BE);
	for (i = 0; i < 4; i++, p++)
		if (SYS_IND (p) == LDM_PARTITION) {
			result = true;
			break;
		}

	if (result)
		ldm_debug ("Found W2K dynamic disk partition type.");

out:
	put_dev_sector (sect);
	return result;
}

/**
 * ldm_get_disk_objid - Search a linked list of vblk's for a given Disk Id
 * @ldb:  Cache of the database structures
 *
 * The LDM Database contains a list of all partitions on all dynamic disks.
 * The primary PRIVHEAD, at the beginning of the physical disk, tells us
 * the GUID of this disk.  This function searches for the GUID in a linked
 * list of vblk's.
 *
 * Return:  Pointer, A matching vblk was found
 *          NULL,    No match, or an error
 */
static struct vblk * ldm_get_disk_objid (const struct ldmdb *ldb)
{
	struct list_head *item;

	BUG_ON (!ldb);

	list_for_each (item, &ldb->v_disk) {
		struct vblk *v = list_entry (item, struct vblk, list);
		if (!memcmp (v->vblk.disk.disk_id, ldb->ph.disk_id, GUID_SIZE))
			return v;
	}

	return NULL;
}

/**
 * ldm_create_data_partitions - Create data partitions for this device
 * @pp:   List of the partitions parsed so far
 * @ldb:  Cache of the database structures
 *
 * The database contains ALL the partitions for ALL disk groups, so we need to
 * filter out this specific disk. Using the disk's object id, we can find all
 * the partitions in the database that belong to this disk.
 *
 * Add each partition in our database, to the parsed_partitions structure.
 *
 * N.B.  This function creates the partitions in the order it finds partition
 *       objects in the linked list.
 *
 * Return:  'true'   Partition created
 *          'false'  Error, probably a range checking problem
 */
static bool ldm_create_data_partitions (struct parsed_partitions *pp,
					const struct ldmdb *ldb)
{
	struct list_head *item;
	struct vblk *vb;
	struct vblk *disk;
	struct vblk_part *part;
	int part_num = 1;

	BUG_ON (!pp || !ldb);

	disk = ldm_get_disk_objid (ldb);
	if (!disk) {
		ldm_crit ("Can't find the ID of this disk in the database.");
		return false;
	}

	printk (" [LDM]");

	/* Create the data partitions */
	list_for_each (item, &ldb->v_part) {
		vb = list_entry (item, struct vblk, list);
		part = &vb->vblk.part;

		if (part->disk_id != disk->obj_id)
			continue;

		put_partition (pp, part_num, ldb->ph.logical_disk_start +
				part->start, part->size);
		part_num++;
	}

	printk ("\n");
	return true;
}


/**
 * ldm_relative - Calculate the next relative offset
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @base:    Size of the previous fixed width fields
 * @offset:  Cumulative size of the previous variable-width fields
 *
 * Because many of the VBLK fields are variable-width, it's necessary
 * to calculate each offset based on the previous one and the length
 * of the field it pointed to.
 *
 * Return:  -1 Error, the calculated offset exceeded the size of the buffer
 *           n OK, a range-checked offset into buffer
 */
static int ldm_relative(const u8 *buffer, int buflen, int base, int offset)
{

	base += offset;
	if (!buffer || offset < 0 || base > buflen) {
		if (!buffer)
			ldm_error("!buffer");
		if (offset < 0)
			ldm_error("offset (%d) < 0", offset);
		if (base > buflen)
			ldm_error("base (%d) > buflen (%d)", base, buflen);
		return -1;
	}
	if (base + buffer[base] >= buflen) {
		ldm_error("base (%d) + buffer[base] (%d) >= buflen (%d)", base,
				buffer[base], buflen);
		return -1;
	}
	return buffer[base] + offset + 1;
}

/**
 * ldm_get_vnum - Convert a variable-width, big endian number, into cpu order
 * @block:  Pointer to the variable-width number to convert
 *
 * Large numbers in the LDM Database are often stored in a packed format.  Each
 * number is prefixed by a one byte width marker.  All numbers in the database
 * are stored in big-endian byte order.  This function reads one of these
 * numbers and returns the result
 *
 * N.B.  This function DOES NOT perform any range checking, though the most
 *       it will read is eight bytes.
 *
 * Return:  n A number
 *          0 Zero, or an error occurred
 */
static u64 ldm_get_vnum (const u8 *block)
{
	u64 tmp = 0;
	u8 length;

	BUG_ON (!block);

	length = *block++;

	if (length && length <= 8)
		while (length--)
			tmp = (tmp << 8) | *block++;
	else
		ldm_error ("Illegal length %d.", length);

	return tmp;
}

/**
 * ldm_get_vstr - Read a length-prefixed string into a buffer
 * @block:   Pointer to the length marker
 * @buffer:  Location to copy string to
 * @buflen:  Size of the output buffer
 *
 * Many of the strings in the LDM Database are not NULL terminated.  Instead
 * they are prefixed by a one byte length marker.  This function copies one of
 * these strings into a buffer.
 *
 * N.B.  This function DOES NOT perform any range checking on the input.
 *       If the buffer is too small, the output will be truncated.
 *
 * Return:  0, Error and @buffer contents are undefined
 *          n, String length in characters (excluding NULL)
 *          buflen-1, String was truncated.
 */
static int ldm_get_vstr (const u8 *block, u8 *buffer, int buflen)
{
	int length;

	BUG_ON (!block || !buffer);

	length = block[0];
	if (length >= buflen) {
		ldm_error ("Truncating string %d -> %d.", length, buflen);
		length = buflen - 1;
	}
	memcpy (buffer, block + 1, length);
	buffer[length] = 0;
	return length;
}


/**
 * ldm_parse_cmp3 - Read a raw VBLK Component object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Component object (version 3) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Component VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_cmp3 (const u8 *buffer, int buflen, struct vblk *vb)
{
	int r_objid, r_name, r_vstate, r_child, r_parent, r_stripe, r_cols, len;
	struct vblk_comp *comp;

	BUG_ON (!buffer || !vb);

	r_objid  = ldm_relative (buffer, buflen, 0x18, 0);
	r_name   = ldm_relative (buffer, buflen, 0x18, r_objid);
	r_vstate = ldm_relative (buffer, buflen, 0x18, r_name);
	r_child  = ldm_relative (buffer, buflen, 0x1D, r_vstate);
	r_parent = ldm_relative (buffer, buflen, 0x2D, r_child);

	if (buffer[0x12] & VBLK_FLAG_COMP_STRIPE) {
		r_stripe = ldm_relative (buffer, buflen, 0x2E, r_parent);
		r_cols   = ldm_relative (buffer, buflen, 0x2E, r_stripe);
		len = r_cols;
	} else {
		r_stripe = 0;
		r_cols   = 0;
		len = r_parent;
	}
	if (len < 0)
		return false;

	len += VBLK_SIZE_CMP3;
	if (len != get_unaligned_be32(buffer + 0x14))
		return false;

	comp = &vb->vblk.comp;
	ldm_get_vstr (buffer + 0x18 + r_name, comp->state,
		sizeof (comp->state));
	comp->type      = buffer[0x18 + r_vstate];
	comp->children  = ldm_get_vnum (buffer + 0x1D + r_vstate);
	comp->parent_id = ldm_get_vnum (buffer + 0x2D + r_child);
	comp->chunksize = r_stripe ? ldm_get_vnum (buffer+r_parent+0x2E) : 0;

	return true;
}

/**
 * ldm_parse_dgr3 - Read a raw VBLK Disk Group object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Disk Group object (version 3) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Disk Group VBLK
 *          'false'  @vb contents are not defined
 */
static int ldm_parse_dgr3 (const u8 *buffer, int buflen, struct vblk *vb)
{
	int r_objid, r_name, r_diskid, r_id1, r_id2, len;
	struct vblk_dgrp *dgrp;

	BUG_ON (!buffer || !vb);

	r_objid  = ldm_relative (buffer, buflen, 0x18, 0);
	r_name   = ldm_relative (buffer, buflen, 0x18, r_objid);
	r_diskid = ldm_relative (buffer, buflen, 0x18, r_name);

	if (buffer[0x12] & VBLK_FLAG_DGR3_IDS) {
		r_id1 = ldm_relative (buffer, buflen, 0x24, r_diskid);
		r_id2 = ldm_relative (buffer, buflen, 0x24, r_id1);
		len = r_id2;
	} else {
		r_id1 = 0;
		r_id2 = 0;
		len = r_diskid;
	}
	if (len < 0)
		return false;

	len += VBLK_SIZE_DGR3;
	if (len != get_unaligned_be32(buffer + 0x14))
		return false;

	dgrp = &vb->vblk.dgrp;
	ldm_get_vstr (buffer + 0x18 + r_name, dgrp->disk_id,
		sizeof (dgrp->disk_id));
	return true;
}

/**
 * ldm_parse_dgr4 - Read a raw VBLK Disk Group object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Disk Group object (version 4) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Disk Group VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_dgr4 (const u8 *buffer, int buflen, struct vblk *vb)
{
	char buf[64];
	int r_objid, r_name, r_id1, r_id2, len;
	struct vblk_dgrp *dgrp;

	BUG_ON (!buffer || !vb);

	r_objid  = ldm_relative (buffer, buflen, 0x18, 0);
	r_name   = ldm_relative (buffer, buflen, 0x18, r_objid);

	if (buffer[0x12] & VBLK_FLAG_DGR4_IDS) {
		r_id1 = ldm_relative (buffer, buflen, 0x44, r_name);
		r_id2 = ldm_relative (buffer, buflen, 0x44, r_id1);
		len = r_id2;
	} else {
		r_id1 = 0;
		r_id2 = 0;
		len = r_name;
	}
	if (len < 0)
		return false;

	len += VBLK_SIZE_DGR4;
	if (len != get_unaligned_be32(buffer + 0x14))
		return false;

	dgrp = &vb->vblk.dgrp;

	ldm_get_vstr (buffer + 0x18 + r_objid, buf, sizeof (buf));
	return true;
}

/**
 * ldm_parse_dsk3 - Read a raw VBLK Disk object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Disk object (version 3) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Disk VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_dsk3 (const u8 *buffer, int buflen, struct vblk *vb)
{
	int r_objid, r_name, r_diskid, r_altname, len;
	struct vblk_disk *disk;

	BUG_ON (!buffer || !vb);

	r_objid   = ldm_relative (buffer, buflen, 0x18, 0);
	r_name    = ldm_relative (buffer, buflen, 0x18, r_objid);
	r_diskid  = ldm_relative (buffer, buflen, 0x18, r_name);
	r_altname = ldm_relative (buffer, buflen, 0x18, r_diskid);
	len = r_altname;
	if (len < 0)
		return false;

	len += VBLK_SIZE_DSK3;
	if (len != get_unaligned_be32(buffer + 0x14))
		return false;

	disk = &vb->vblk.disk;
	ldm_get_vstr (buffer + 0x18 + r_diskid, disk->alt_name,
		sizeof (disk->alt_name));
	if (!ldm_parse_guid (buffer + 0x19 + r_name, disk->disk_id))
		return false;

	return true;
}

/**
 * ldm_parse_dsk4 - Read a raw VBLK Disk object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Disk object (version 4) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Disk VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_dsk4 (const u8 *buffer, int buflen, struct vblk *vb)
{
	int r_objid, r_name, len;
	struct vblk_disk *disk;

	BUG_ON (!buffer || !vb);

	r_objid = ldm_relative (buffer, buflen, 0x18, 0);
	r_name  = ldm_relative (buffer, buflen, 0x18, r_objid);
	len     = r_name;
	if (len < 0)
		return false;

	len += VBLK_SIZE_DSK4;
	if (len != get_unaligned_be32(buffer + 0x14))
		return false;

	disk = &vb->vblk.disk;
	memcpy (disk->disk_id, buffer + 0x18 + r_name, GUID_SIZE);
	return true;
}

/**
 * ldm_parse_prt3 - Read a raw VBLK Partition object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Partition object (version 3) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Partition VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_prt3(const u8 *buffer, int buflen, struct vblk *vb)
{
	int r_objid, r_name, r_size, r_parent, r_diskid, r_index, len;
	struct vblk_part *part;

	BUG_ON(!buffer || !vb);
	r_objid = ldm_relative(buffer, buflen, 0x18, 0);
	if (r_objid < 0) {
		ldm_error("r_objid %d < 0", r_objid);
		return false;
	}
	r_name = ldm_relative(buffer, buflen, 0x18, r_objid);
	if (r_name < 0) {
		ldm_error("r_name %d < 0", r_name);
		return false;
	}
	r_size = ldm_relative(buffer, buflen, 0x34, r_name);
	if (r_size < 0) {
		ldm_error("r_size %d < 0", r_size);
		return false;
	}
	r_parent = ldm_relative(buffer, buflen, 0x34, r_size);
	if (r_parent < 0) {
		ldm_error("r_parent %d < 0", r_parent);
		return false;
	}
	r_diskid = ldm_relative(buffer, buflen, 0x34, r_parent);
	if (r_diskid < 0) {
		ldm_error("r_diskid %d < 0", r_diskid);
		return false;
	}
	if (buffer[0x12] & VBLK_FLAG_PART_INDEX) {
		r_index = ldm_relative(buffer, buflen, 0x34, r_diskid);
		if (r_index < 0) {
			ldm_error("r_index %d < 0", r_index);
			return false;
		}
		len = r_index;
	} else {
		r_index = 0;
		len = r_diskid;
	}
	if (len < 0) {
		ldm_error("len %d < 0", len);
		return false;
	}
	len += VBLK_SIZE_PRT3;
	if (len > get_unaligned_be32(buffer + 0x14)) {
		ldm_error("len %d > BE32(buffer + 0x14) %d", len,
				get_unaligned_be32(buffer + 0x14));
		return false;
	}
	part = &vb->vblk.part;
	part->start = get_unaligned_be64(buffer + 0x24 + r_name);
	part->volume_offset = get_unaligned_be64(buffer + 0x2C + r_name);
	part->size = ldm_get_vnum(buffer + 0x34 + r_name);
	part->parent_id = ldm_get_vnum(buffer + 0x34 + r_size);
	part->disk_id = ldm_get_vnum(buffer + 0x34 + r_parent);
	if (vb->flags & VBLK_FLAG_PART_INDEX)
		part->partnum = buffer[0x35 + r_diskid];
	else
		part->partnum = 0;
	return true;
}

/**
 * ldm_parse_vol5 - Read a raw VBLK Volume object into a vblk structure
 * @buffer:  Block of data being worked on
 * @buflen:  Size of the block of data
 * @vb:      In-memory vblk in which to return information
 *
 * Read a raw VBLK Volume object (version 5) into a vblk structure.
 *
 * Return:  'true'   @vb contains a Volume VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_vol5(const u8 *buffer, int buflen, struct vblk *vb)
{
	int r_objid, r_name, r_vtype, r_disable_drive_letter, r_child, r_size;
	int r_id1, r_id2, r_size2, r_drive, len;
	struct vblk_volu *volu;

	BUG_ON(!buffer || !vb);
	r_objid = ldm_relative(buffer, buflen, 0x18, 0);
	if (r_objid < 0) {
		ldm_error("r_objid %d < 0", r_objid);
		return false;
	}
	r_name = ldm_relative(buffer, buflen, 0x18, r_objid);
	if (r_name < 0) {
		ldm_error("r_name %d < 0", r_name);
		return false;
	}
	r_vtype = ldm_relative(buffer, buflen, 0x18, r_name);
	if (r_vtype < 0) {
		ldm_error("r_vtype %d < 0", r_vtype);
		return false;
	}
	r_disable_drive_letter = ldm_relative(buffer, buflen, 0x18, r_vtype);
	if (r_disable_drive_letter < 0) {
		ldm_error("r_disable_drive_letter %d < 0",
				r_disable_drive_letter);
		return false;
	}
	r_child = ldm_relative(buffer, buflen, 0x2D, r_disable_drive_letter);
	if (r_child < 0) {
		ldm_error("r_child %d < 0", r_child);
		return false;
	}
	r_size = ldm_relative(buffer, buflen, 0x3D, r_child);
	if (r_size < 0) {
		ldm_error("r_size %d < 0", r_size);
		return false;
	}
	if (buffer[0x12] & VBLK_FLAG_VOLU_ID1) {
		r_id1 = ldm_relative(buffer, buflen, 0x52, r_size);
		if (r_id1 < 0) {
			ldm_error("r_id1 %d < 0", r_id1);
			return false;
		}
	} else
		r_id1 = r_size;
	if (buffer[0x12] & VBLK_FLAG_VOLU_ID2) {
		r_id2 = ldm_relative(buffer, buflen, 0x52, r_id1);
		if (r_id2 < 0) {
			ldm_error("r_id2 %d < 0", r_id2);
			return false;
		}
	} else
		r_id2 = r_id1;
	if (buffer[0x12] & VBLK_FLAG_VOLU_SIZE) {
		r_size2 = ldm_relative(buffer, buflen, 0x52, r_id2);
		if (r_size2 < 0) {
			ldm_error("r_size2 %d < 0", r_size2);
			return false;
		}
	} else
		r_size2 = r_id2;
	if (buffer[0x12] & VBLK_FLAG_VOLU_DRIVE) {
		r_drive = ldm_relative(buffer, buflen, 0x52, r_size2);
		if (r_drive < 0) {
			ldm_error("r_drive %d < 0", r_drive);
			return false;
		}
	} else
		r_drive = r_size2;
	len = r_drive;
	if (len < 0) {
		ldm_error("len %d < 0", len);
		return false;
	}
	len += VBLK_SIZE_VOL5;
	if (len > get_unaligned_be32(buffer + 0x14)) {
		ldm_error("len %d > BE32(buffer + 0x14) %d", len,
				get_unaligned_be32(buffer + 0x14));
		return false;
	}
	volu = &vb->vblk.volu;
	ldm_get_vstr(buffer + 0x18 + r_name, volu->volume_type,
			sizeof(volu->volume_type));
	memcpy(volu->volume_state, buffer + 0x18 + r_disable_drive_letter,
			sizeof(volu->volume_state));
	volu->size = ldm_get_vnum(buffer + 0x3D + r_child);
	volu->partition_type = buffer[0x41 + r_size];
	memcpy(volu->guid, buffer + 0x42 + r_size, sizeof(volu->guid));
	if (buffer[0x12] & VBLK_FLAG_VOLU_DRIVE) {
		ldm_get_vstr(buffer + 0x52 + r_size, volu->drive_hint,
				sizeof(volu->drive_hint));
	}
	return true;
}

/**
 * ldm_parse_vblk - Read a raw VBLK object into a vblk structure
 * @buf:  Block of data being worked on
 * @len:  Size of the block of data
 * @vb:   In-memory vblk in which to return information
 *
 * Read a raw VBLK object into a vblk structure.  This function just reads the
 * information common to all VBLK types, then delegates the rest of the work to
 * helper functions: ldm_parse_*.
 *
 * Return:  'true'   @vb contains a VBLK
 *          'false'  @vb contents are not defined
 */
static bool ldm_parse_vblk (const u8 *buf, int len, struct vblk *vb)
{
	bool result = false;
	int r_objid;

	BUG_ON (!buf || !vb);

	r_objid = ldm_relative (buf, len, 0x18, 0);
	if (r_objid < 0) {
		ldm_error ("VBLK header is corrupt.");
		return false;
	}

	vb->flags  = buf[0x12];
	vb->type   = buf[0x13];
	vb->obj_id = ldm_get_vnum (buf + 0x18);
	ldm_get_vstr (buf+0x18+r_objid, vb->name, sizeof (vb->name));

	switch (vb->type) {
		case VBLK_CMP3:  result = ldm_parse_cmp3 (buf, len, vb); break;
		case VBLK_DSK3:  result = ldm_parse_dsk3 (buf, len, vb); break;
		case VBLK_DSK4:  result = ldm_parse_dsk4 (buf, len, vb); break;
		case VBLK_DGR3:  result = ldm_parse_dgr3 (buf, len, vb); break;
		case VBLK_DGR4:  result = ldm_parse_dgr4 (buf, len, vb); break;
		case VBLK_PRT3:  result = ldm_parse_prt3 (buf, len, vb); break;
		case VBLK_VOL5:  result = ldm_parse_vol5 (buf, len, vb); break;
	}

	if (result)
		ldm_debug ("Parsed VBLK 0x%llx (type: 0x%02x) ok.",
			 (unsigned long long) vb->obj_id, vb->type);
	else
		ldm_error ("Failed to parse VBLK 0x%llx (type: 0x%02x).",
			(unsigned long long) vb->obj_id, vb->type);

	return result;
}


/**
 * ldm_ldmdb_add - Adds a raw VBLK entry to the ldmdb database
 * @data:  Raw VBLK to add to the database
 * @len:   Size of the raw VBLK
 * @ldb:   Cache of the database structures
 *
 * The VBLKs are sorted into categories.  Partitions are also sorted by offset.
 *
 * N.B.  This function does not check the validity of the VBLKs.
 *
 * Return:  'true'   The VBLK was added
 *          'false'  An error occurred
 */
static bool ldm_ldmdb_add (u8 *data, int len, struct ldmdb *ldb)
{
	struct vblk *vb;
	struct list_head *item;

	BUG_ON (!data || !ldb);

	vb = kmalloc (sizeof (*vb), GFP_KERNEL);
	if (!vb) {
		ldm_crit ("Out of memory.");
		return false;
	}

	if (!ldm_parse_vblk (data, len, vb)) {
		kfree(vb);
		return false;			/* Already logged */
	}

	/* Put vblk into the correct list. */
	switch (vb->type) {
	case VBLK_DGR3:
	case VBLK_DGR4:
		list_add (&vb->list, &ldb->v_dgrp);
		break;
	case VBLK_DSK3:
	case VBLK_DSK4:
		list_add (&vb->list, &ldb->v_disk);
		break;
	case VBLK_VOL5:
		list_add (&vb->list, &ldb->v_volu);
		break;
	case VBLK_CMP3:
		list_add (&vb->list, &ldb->v_comp);
		break;
	case VBLK_PRT3:
		/* Sort by the partition's start sector. */
		list_for_each (item, &ldb->v_part) {
			struct vblk *v = list_entry (item, struct vblk, list);
			if ((v->vblk.part.disk_id == vb->vblk.part.disk_id) &&
			    (v->vblk.part.start > vb->vblk.part.start)) {
				list_add_tail (&vb->list, &v->list);
				return true;
			}
		}
		list_add_tail (&vb->list, &ldb->v_part);
		break;
	}
	return true;
}

/**
 * ldm_frag_add - Add a VBLK fragment to a list
 * @data:   Raw fragment to be added to the list
 * @size:   Size of the raw fragment
 * @frags:  Linked list of VBLK fragments
 *
 * Fragmented VBLKs may not be consecutive in the database, so they are placed
 * in a list so they can be pieced together later.
 *
 * Return:  'true'   Success, the VBLK was added to the list
 *          'false'  Error, a problem occurred
 */
static bool ldm_frag_add (const u8 *data, int size, struct list_head *frags)
{
	struct frag *f;
	struct list_head *item;
	int rec, num, group;

	BUG_ON (!data || !frags);

	if (size < 2 * VBLK_SIZE_HEAD) {
		ldm_error("Value of size is to small.");
		return false;
	}

	group = get_unaligned_be32(data + 0x08);
	rec   = get_unaligned_be16(data + 0x0C);
	num   = get_unaligned_be16(data + 0x0E);
	if ((num < 1) || (num > 4)) {
		ldm_error ("A VBLK claims to have %d parts.", num);
		return false;
	}
	if (rec >= num) {
		ldm_error("REC value (%d) exceeds NUM value (%d)", rec, num);
		return false;
	}

	list_for_each (item, frags) {
		f = list_entry (item, struct frag, list);
		if (f->group == group)
			goto found;
	}

	f = kmalloc (sizeof (*f) + size*num, GFP_KERNEL);
	if (!f) {
		ldm_crit ("Out of memory.");
		return false;
	}

	f->group = group;
	f->num   = num;
	f->rec   = rec;
	f->map   = 0xFF << num;

	list_add_tail (&f->list, frags);
found:
	if (rec >= f->num) {
		ldm_error("REC value (%d) exceeds NUM value (%d)", rec, f->num);
		return false;
	}

	if (f->map & (1 << rec)) {
		ldm_error ("Duplicate VBLK, part %d.", rec);
		f->map &= 0x7F;			/* Mark the group as broken */
		return false;
	}

	f->map |= (1 << rec);

	data += VBLK_SIZE_HEAD;
	size -= VBLK_SIZE_HEAD;

	memcpy (f->data+rec*(size-VBLK_SIZE_HEAD)+VBLK_SIZE_HEAD, data, size);

	return true;
}

/**
 * ldm_frag_free - Free a linked list of VBLK fragments
 * @list:  Linked list of fragments
 *
 * Free a linked list of VBLK fragments
 *
 * Return:  none
 */
static void ldm_frag_free (struct list_head *list)
{
	struct list_head *item, *tmp;

	BUG_ON (!list);

	list_for_each_safe (item, tmp, list)
		kfree (list_entry (item, struct frag, list));
}

/**
 * ldm_frag_commit - Validate fragmented VBLKs and add them to the database
 * @frags:  Linked list of VBLK fragments
 * @ldb:    Cache of the database structures
 *
 * Now that all the fragmented VBLKs have been collected, they must be added to
 * the database for later use.
 *
 * Return:  'true'   All the fragments we added successfully
 *          'false'  One or more of the fragments we invalid
 */
static bool ldm_frag_commit (struct list_head *frags, struct ldmdb *ldb)
{
	struct frag *f;
	struct list_head *item;

	BUG_ON (!frags || !ldb);

	list_for_each (item, frags) {
		f = list_entry (item, struct frag, list);

		if (f->map != 0xFF) {
			ldm_error ("VBLK group %d is incomplete (0x%02x).",
				f->group, f->map);
			return false;
		}

		if (!ldm_ldmdb_add (f->data, f->num*ldb->vm.vblk_size, ldb))
			return false;		/* Already logged */
	}
	return true;
}

/**
 * ldm_get_vblks - Read the on-disk database of VBLKs into memory
 * @bdev:  Device holding the LDM Database
 * @base:  Offset, into @bdev, of the database
 * @ldb:   Cache of the database structures
 *
 * To use the information from the VBLKs, they need to be read from the disk,
 * unpacked and validated.  We cache them in @ldb according to their type.
 *
 * Return:  'true'   All the VBLKs were read successfully
 *          'false'  An error occurred
 */
static bool ldm_get_vblks (struct block_device *bdev, unsigned long base,
			   struct ldmdb *ldb)
{
	int size, perbuf, skip, finish, s, v, recs;
	u8 *data = NULL;
	Sector sect;
	bool result = false;
	LIST_HEAD (frags);

	BUG_ON (!bdev || !ldb);

	size   = ldb->vm.vblk_size;
	perbuf = 512 / size;
	skip   = ldb->vm.vblk_offset >> 9;		/* Bytes to sectors */
	finish = (size * ldb->vm.last_vblk_seq) >> 9;

	for (s = skip; s < finish; s++) {		/* For each sector */
		data = read_dev_sector (bdev, base + OFF_VMDB + s, &sect);
		if (!data) {
			ldm_crit ("Disk read failed.");
			goto out;
		}

		for (v = 0; v < perbuf; v++, data+=size) {  /* For each vblk */
			if (MAGIC_VBLK != get_unaligned_be32(data)) {
				ldm_error ("Expected to find a VBLK.");
				goto out;
			}

			recs = get_unaligned_be16(data + 0x0E);	/* Number of records */
			if (recs == 1) {
				if (!ldm_ldmdb_add (data, size, ldb))
					goto out;	/* Already logged */
			} else if (recs > 1) {
				if (!ldm_frag_add (data, size, &frags))
					goto out;	/* Already logged */
			}
			/* else Record is not in use, ignore it. */
		}
		put_dev_sector (sect);
		data = NULL;
	}

	result = ldm_frag_commit (&frags, ldb);	/* Failures, already logged */
out:
	if (data)
		put_dev_sector (sect);
	ldm_frag_free (&frags);

	return result;
}

/**
 * ldm_free_vblks - Free a linked list of vblk's
 * @lh:  Head of a linked list of struct vblk
 *
 * Free a list of vblk's and free the memory used to maintain the list.
 *
 * Return:  none
 */
static void ldm_free_vblks (struct list_head *lh)
{
	struct list_head *item, *tmp;

	BUG_ON (!lh);

	list_for_each_safe (item, tmp, lh)
		kfree (list_entry (item, struct vblk, list));
}


/**
 * ldm_partition - Find out whether a device is a dynamic disk and handle it
 * @pp:    List of the partitions parsed so far
 * @bdev:  Device holding the LDM Database
 *
 * This determines whether the device @bdev is a dynamic disk and if so creates
 * the partitions necessary in the gendisk structure pointed to by @hd.
 *
 * We create a dummy device 1, which contains the LDM database, and then create
 * each partition described by the LDM database in sequence as devices 2+. For
 * example, if the device is hda, we would have: hda1: LDM database, hda2, hda3,
 * and so on: the actual data containing partitions.
 *
 * Return:  1 Success, @bdev is a dynamic disk and we handled it
 *          0 Success, @bdev is not a dynamic disk
 *         -1 An error occurred before enough information had been read
 *            Or @bdev is a dynamic disk, but it may be corrupted
 */
int ldm_partition (struct parsed_partitions *pp, struct block_device *bdev)
{
	struct ldmdb  *ldb;
	unsigned long base;
	int result = -1;

	BUG_ON (!pp || !bdev);

	/* Look for signs of a Dynamic Disk */
	if (!ldm_validate_partition_table (bdev))
		return 0;

	ldb = kmalloc (sizeof (*ldb), GFP_KERNEL);
	if (!ldb) {
		ldm_crit ("Out of memory.");
		goto out;
	}

	/* Parse and check privheads. */
	if (!ldm_validate_privheads (bdev, &ldb->ph))
		goto out;		/* Already logged */

	/* All further references are relative to base (database start). */
	base = ldb->ph.config_start;

	/* Parse and check tocs and vmdb. */
	if (!ldm_validate_tocblocks (bdev, base, ldb) ||
	    !ldm_validate_vmdb      (bdev, base, ldb))
	    	goto out;		/* Already logged */

	/* Initialize vblk lists in ldmdb struct */
	INIT_LIST_HEAD (&ldb->v_dgrp);
	INIT_LIST_HEAD (&ldb->v_disk);
	INIT_LIST_HEAD (&ldb->v_volu);
	INIT_LIST_HEAD (&ldb->v_comp);
	INIT_LIST_HEAD (&ldb->v_part);

	if (!ldm_get_vblks (bdev, base, ldb)) {
		ldm_crit ("Failed to read the VBLKs from the database.");
		goto cleanup;
	}

	/* Finally, create the data partition devices. */
	if (ldm_create_data_partitions (pp, ldb)) {
		ldm_debug ("Parsed LDM database successfully.");
		result = 1;
	}
	/* else Already logged */

cleanup:
	ldm_free_vblks (&ldb->v_dgrp);
	ldm_free_vblks (&ldb->v_disk);
	ldm_free_vblks (&ldb->v_volu);
	ldm_free_vblks (&ldb->v_comp);
	ldm_free_vblks (&ldb->v_part);
out:
	kfree (ldb);
	return result;
}
