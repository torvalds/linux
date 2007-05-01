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

#ifndef __UBI_USER_H__
#define __UBI_USER_H__

/*
 * UBI volume creation
 * ~~~~~~~~~~~~~~~~~~~
 *
 * UBI volumes are created via the %UBI_IOCMKVOL IOCTL command of UBI character
 * device. A &struct ubi_mkvol_req object has to be properly filled and a
 * pointer to it has to be passed to the IOCTL.
 *
 * UBI volume deletion
 * ~~~~~~~~~~~~~~~~~~~
 *
 * To delete a volume, the %UBI_IOCRMVOL IOCTL command of the UBI character
 * device should be used. A pointer to the 32-bit volume ID hast to be passed
 * to the IOCTL.
 *
 * UBI volume re-size
 * ~~~~~~~~~~~~~~~~~~
 *
 * To re-size a volume, the %UBI_IOCRSVOL IOCTL command of the UBI character
 * device should be used. A &struct ubi_rsvol_req object has to be properly
 * filled and a pointer to it has to be passed to the IOCTL.
 *
 * UBI volume update
 * ~~~~~~~~~~~~~~~~~
 *
 * Volume update should be done via the %UBI_IOCVOLUP IOCTL command of the
 * corresponding UBI volume character device. A pointer to a 64-bit update
 * size should be passed to the IOCTL. After then, UBI expects user to write
 * this number of bytes to the volume character device. The update is finished
 * when the claimed number of bytes is passed. So, the volume update sequence
 * is something like:
 *
 * fd = open("/dev/my_volume");
 * ioctl(fd, UBI_IOCVOLUP, &image_size);
 * write(fd, buf, image_size);
 * close(fd);
 */

/*
 * When a new volume is created, users may either specify the volume number they
 * want to create or to let UBI automatically assign a volume number using this
 * constant.
 */
#define UBI_VOL_NUM_AUTO (-1)

/* Maximum volume name length */
#define UBI_MAX_VOLUME_NAME 127

/* IOCTL commands of UBI character devices */

#define UBI_IOC_MAGIC 'o'

/* Create an UBI volume */
#define UBI_IOCMKVOL _IOW(UBI_IOC_MAGIC, 0, struct ubi_mkvol_req)
/* Remove an UBI volume */
#define UBI_IOCRMVOL _IOW(UBI_IOC_MAGIC, 1, int32_t)
/* Re-size an UBI volume */
#define UBI_IOCRSVOL _IOW(UBI_IOC_MAGIC, 2, struct ubi_rsvol_req)

/* IOCTL commands of UBI volume character devices */

#define UBI_VOL_IOC_MAGIC 'O'

/* Start UBI volume update */
#define UBI_IOCVOLUP _IOW(UBI_VOL_IOC_MAGIC, 0, int64_t)
/* An eraseblock erasure command, used for debugging, disabled by default */
#define UBI_IOCEBER _IOW(UBI_VOL_IOC_MAGIC, 1, int32_t)

/*
 * UBI volume type constants.
 *
 * @UBI_DYNAMIC_VOLUME: dynamic volume
 * @UBI_STATIC_VOLUME:  static volume
 */
enum {
	UBI_DYNAMIC_VOLUME = 3,
	UBI_STATIC_VOLUME = 4
};

/**
 * struct ubi_mkvol_req - volume description data structure used in
 * volume creation requests.
 * @vol_id: volume number
 * @alignment: volume alignment
 * @bytes: volume size in bytes
 * @vol_type: volume type (%UBI_DYNAMIC_VOLUME or %UBI_STATIC_VOLUME)
 * @padding1: reserved for future, not used
 * @name_len: volume name length
 * @padding2: reserved for future, not used
 * @name: volume name
 *
 * This structure is used by userspace programs when creating new volumes. The
 * @used_bytes field is only necessary when creating static volumes.
 *
 * The @alignment field specifies the required alignment of the volume logical
 * eraseblock. This means, that the size of logical eraseblocks will be aligned
 * to this number, i.e.,
 *	(UBI device logical eraseblock size) mod (@alignment) = 0.
 *
 * To put it differently, the logical eraseblock of this volume may be slightly
 * shortened in order to make it properly aligned. The alignment has to be
 * multiple of the flash minimal input/output unit, or %1 to utilize the entire
 * available space of logical eraseblocks.
 *
 * The @alignment field may be useful, for example, when one wants to maintain
 * a block device on top of an UBI volume. In this case, it is desirable to fit
 * an integer number of blocks in logical eraseblocks of this UBI volume. With
 * alignment it is possible to update this volume using plane UBI volume image
 * BLOBs, without caring about how to properly align them.
 */
struct ubi_mkvol_req {
	int32_t vol_id;
	int32_t alignment;
	int64_t bytes;
	int8_t vol_type;
	int8_t padding1;
	int16_t name_len;
	int8_t padding2[4];
	char name[UBI_MAX_VOLUME_NAME+1];
} __attribute__ ((packed));

/**
 * struct ubi_rsvol_req - a data structure used in volume re-size requests.
 * @vol_id: ID of the volume to re-size
 * @bytes: new size of the volume in bytes
 *
 * Re-sizing is possible for both dynamic and static volumes. But while dynamic
 * volumes may be re-sized arbitrarily, static volumes cannot be made to be
 * smaller then the number of bytes they bear. To arbitrarily shrink a static
 * volume, it must be wiped out first (by means of volume update operation with
 * zero number of bytes).
 */
struct ubi_rsvol_req {
	int64_t bytes;
	int32_t vol_id;
} __attribute__ ((packed));

#endif /* __UBI_USER_H__ */
