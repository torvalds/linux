/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __LINUX_UBI_H__
#define __LINUX_UBI_H__

#include <asm/ioctl.h>
#include <linux/types.h>
#include <mtd/ubi-user.h>

/*
 * enum ubi_open_mode - UBI volume open mode constants.
 *
 * UBI_READONLY: read-only mode
 * UBI_READWRITE: read-write mode
 * UBI_EXCLUSIVE: exclusive mode
 */
enum {
	UBI_READONLY = 1,
	UBI_READWRITE,
	UBI_EXCLUSIVE
};

/**
 * struct ubi_volume_info - UBI volume description data structure.
 * @vol_id: volume ID
 * @ubi_num: UBI device number this volume belongs to
 * @size: how many physical eraseblocks are reserved for this volume
 * @used_bytes: how many bytes of data this volume contains
 * @used_ebs: how many physical eraseblocks of this volume actually contain any
 *            data
 * @vol_type: volume type (%UBI_DYNAMIC_VOLUME or %UBI_STATIC_VOLUME)
 * @corrupted: non-zero if the volume is corrupted (static volumes only)
 * @upd_marker: non-zero if the volume has update marker set
 * @alignment: volume alignment
 * @usable_leb_size: how many bytes are available in logical eraseblocks of
 *                   this volume
 * @name_len: volume name length
 * @name: volume name
 * @cdev: UBI volume character device major and minor numbers
 *
 * The @corrupted flag is only relevant to static volumes and is always zero
 * for dynamic ones. This is because UBI does not care about dynamic volume
 * data protection and only cares about protecting static volume data.
 *
 * The @upd_marker flag is set if the volume update operation was interrupted.
 * Before touching the volume data during the update operation, UBI first sets
 * the update marker flag for this volume. If the volume update operation was
 * further interrupted, the update marker indicates this. If the update marker
 * is set, the contents of the volume is certainly damaged and a new volume
 * update operation has to be started.
 *
 * To put it differently, @corrupted and @upd_marker fields have different
 * semantics:
 *     o the @corrupted flag means that this static volume is corrupted for some
 *       reasons, but not because an interrupted volume update
 *     o the @upd_marker field means that the volume is damaged because of an
 *       interrupted update operation.
 *
 * I.e., the @corrupted flag is never set if the @upd_marker flag is set.
 *
 * The @used_bytes and @used_ebs fields are only really needed for static
 * volumes and contain the number of bytes stored in this static volume and how
 * many eraseblock this data occupies. In case of dynamic volumes, the
 * @used_bytes field is equivalent to @size*@usable_leb_size, and the @used_ebs
 * field is equivalent to @size.
 *
 * In general, logical eraseblock size is a property of the UBI device, not
 * of the UBI volume. Indeed, the logical eraseblock size depends on the
 * physical eraseblock size and on how much bytes UBI headers consume. But
 * because of the volume alignment (@alignment), the usable size of logical
 * eraseblocks if a volume may be less. The following equation is true:
 * 	@usable_leb_size = LEB size - (LEB size mod @alignment),
 * where LEB size is the logical eraseblock size defined by the UBI device.
 *
 * The alignment is multiple to the minimal flash input/output unit size or %1
 * if all the available space is used.
 *
 * To put this differently, alignment may be considered is a way to change
 * volume logical eraseblock sizes.
 */
struct ubi_volume_info {
	int ubi_num;
	int vol_id;
	int size;
	long long used_bytes;
	int used_ebs;
	int vol_type;
	int corrupted;
	int upd_marker;
	int alignment;
	int usable_leb_size;
	int name_len;
	const char *name;
	dev_t cdev;
};

/**
 * struct ubi_device_info - UBI device description data structure.
 * @ubi_num: ubi device number
 * @leb_size: logical eraseblock size on this UBI device
 * @min_io_size: minimal I/O unit size
 * @ro_mode: if this device is in read-only mode
 * @cdev: UBI character device major and minor numbers
 *
 * Note, @leb_size is the logical eraseblock size offered by the UBI device.
 * Volumes of this UBI device may have smaller logical eraseblock size if their
 * alignment is not equivalent to %1.
 */
struct ubi_device_info {
	int ubi_num;
	int leb_size;
	int min_io_size;
	int ro_mode;
	dev_t cdev;
};

/* UBI descriptor given to users when they open UBI volumes */
struct ubi_volume_desc;

int ubi_get_device_info(int ubi_num, struct ubi_device_info *di);
void ubi_get_volume_info(struct ubi_volume_desc *desc,
			 struct ubi_volume_info *vi);
struct ubi_volume_desc *ubi_open_volume(int ubi_num, int vol_id, int mode);
struct ubi_volume_desc *ubi_open_volume_nm(int ubi_num, const char *name,
					   int mode);
void ubi_close_volume(struct ubi_volume_desc *desc);
int ubi_leb_read(struct ubi_volume_desc *desc, int lnum, char *buf, int offset,
		 int len, int check);
int ubi_leb_write(struct ubi_volume_desc *desc, int lnum, const void *buf,
		  int offset, int len, int dtype);
int ubi_leb_change(struct ubi_volume_desc *desc, int lnum, const void *buf,
		   int len, int dtype);
int ubi_leb_erase(struct ubi_volume_desc *desc, int lnum);
int ubi_leb_unmap(struct ubi_volume_desc *desc, int lnum);
int ubi_leb_map(struct ubi_volume_desc *desc, int lnum, int dtype);
int ubi_is_mapped(struct ubi_volume_desc *desc, int lnum);
int ubi_sync(int ubi_num);

/*
 * This function is the same as the 'ubi_leb_read()' function, but it does not
 * provide the checking capability.
 */
static inline int ubi_read(struct ubi_volume_desc *desc, int lnum, char *buf,
			   int offset, int len)
{
	return ubi_leb_read(desc, lnum, buf, offset, len, 0);
}

/*
 * This function is the same as the 'ubi_leb_write()' functions, but it does
 * not have the data type argument.
 */
static inline int ubi_write(struct ubi_volume_desc *desc, int lnum,
			    const void *buf, int offset, int len)
{
	return ubi_leb_write(desc, lnum, buf, offset, len, UBI_UNKNOWN);
}

/*
 * This function is the same as the 'ubi_leb_change()' functions, but it does
 * not have the data type argument.
 */
static inline int ubi_change(struct ubi_volume_desc *desc, int lnum,
				    const void *buf, int len)
{
	return ubi_leb_change(desc, lnum, buf, len, UBI_UNKNOWN);
}

#endif /* !__LINUX_UBI_H__ */
