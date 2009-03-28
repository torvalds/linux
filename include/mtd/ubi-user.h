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

#include <linux/types.h>

/*
 * UBI device creation (the same as MTD device attachment)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * MTD devices may be attached using %UBI_IOCATT ioctl command of the UBI
 * control device. The caller has to properly fill and pass
 * &struct ubi_attach_req object - UBI will attach the MTD device specified in
 * the request and return the newly created UBI device number as the ioctl
 * return value.
 *
 * UBI device deletion (the same as MTD device detachment)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * An UBI device maybe deleted with %UBI_IOCDET ioctl command of the UBI
 * control device.
 *
 * UBI volume creation
 * ~~~~~~~~~~~~~~~~~~~
 *
 * UBI volumes are created via the %UBI_IOCMKVOL ioctl command of UBI character
 * device. A &struct ubi_mkvol_req object has to be properly filled and a
 * pointer to it has to be passed to the ioctl.
 *
 * UBI volume deletion
 * ~~~~~~~~~~~~~~~~~~~
 *
 * To delete a volume, the %UBI_IOCRMVOL ioctl command of the UBI character
 * device should be used. A pointer to the 32-bit volume ID hast to be passed
 * to the ioctl.
 *
 * UBI volume re-size
 * ~~~~~~~~~~~~~~~~~~
 *
 * To re-size a volume, the %UBI_IOCRSVOL ioctl command of the UBI character
 * device should be used. A &struct ubi_rsvol_req object has to be properly
 * filled and a pointer to it has to be passed to the ioctl.
 *
 * UBI volumes re-name
 * ~~~~~~~~~~~~~~~~~~~
 *
 * To re-name several volumes atomically at one go, the %UBI_IOCRNVOL command
 * of the UBI character device should be used. A &struct ubi_rnvol_req object
 * has to be properly filled and a pointer to it has to be passed to the ioctl.
 *
 * UBI volume update
 * ~~~~~~~~~~~~~~~~~
 *
 * Volume update should be done via the %UBI_IOCVOLUP ioctl command of the
 * corresponding UBI volume character device. A pointer to a 64-bit update
 * size should be passed to the ioctl. After this, UBI expects user to write
 * this number of bytes to the volume character device. The update is finished
 * when the claimed number of bytes is passed. So, the volume update sequence
 * is something like:
 *
 * fd = open("/dev/my_volume");
 * ioctl(fd, UBI_IOCVOLUP, &image_size);
 * write(fd, buf, image_size);
 * close(fd);
 *
 * Logical eraseblock erase
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * To erase a logical eraseblock, the %UBI_IOCEBER ioctl command of the
 * corresponding UBI volume character device should be used. This command
 * unmaps the requested logical eraseblock, makes sure the corresponding
 * physical eraseblock is successfully erased, and returns.
 *
 * Atomic logical eraseblock change
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Atomic logical eraseblock change operation is called using the %UBI_IOCEBCH
 * ioctl command of the corresponding UBI volume character device. A pointer to
 * a &struct ubi_leb_change_req object has to be passed to the ioctl. Then the
 * user is expected to write the requested amount of bytes (similarly to what
 * should be done in case of the "volume update" ioctl).
 *
 * Logical eraseblock map
 * ~~~~~~~~~~~~~~~~~~~~~
 *
 * To map a logical eraseblock to a physical eraseblock, the %UBI_IOCEBMAP
 * ioctl command should be used. A pointer to a &struct ubi_map_req object is
 * expected to be passed. The ioctl maps the requested logical eraseblock to
 * a physical eraseblock and returns.  Only non-mapped logical eraseblocks can
 * be mapped. If the logical eraseblock specified in the request is already
 * mapped to a physical eraseblock, the ioctl fails and returns error.
 *
 * Logical eraseblock unmap
 * ~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * To unmap a logical eraseblock to a physical eraseblock, the %UBI_IOCEBUNMAP
 * ioctl command should be used. The ioctl unmaps the logical eraseblocks,
 * schedules corresponding physical eraseblock for erasure, and returns. Unlike
 * the "LEB erase" command, it does not wait for the physical eraseblock being
 * erased. Note, the side effect of this is that if an unclean reboot happens
 * after the unmap ioctl returns, you may find the LEB mapped again to the same
 * physical eraseblock after the UBI is run again.
 *
 * Check if logical eraseblock is mapped
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * To check if a logical eraseblock is mapped to a physical eraseblock, the
 * %UBI_IOCEBISMAP ioctl command should be used. It returns %0 if the LEB is
 * not mapped, and %1 if it is mapped.
 *
 * Set an UBI volume property
 * ~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * To set an UBI volume property the %UBI_IOCSETPROP ioctl command should be
 * used. A pointer to a &struct ubi_set_prop_req object is expected to be
 * passed. The object describes which property should be set, and to which value
 * it should be set.
 */

/*
 * When a new UBI volume or UBI device is created, users may either specify the
 * volume/device number they want to create or to let UBI automatically assign
 * the number using these constants.
 */
#define UBI_VOL_NUM_AUTO (-1)
#define UBI_DEV_NUM_AUTO (-1)

/* Maximum volume name length */
#define UBI_MAX_VOLUME_NAME 127

/* ioctl commands of UBI character devices */

#define UBI_IOC_MAGIC 'o'

/* Create an UBI volume */
#define UBI_IOCMKVOL _IOW(UBI_IOC_MAGIC, 0, struct ubi_mkvol_req)
/* Remove an UBI volume */
#define UBI_IOCRMVOL _IOW(UBI_IOC_MAGIC, 1, __s32)
/* Re-size an UBI volume */
#define UBI_IOCRSVOL _IOW(UBI_IOC_MAGIC, 2, struct ubi_rsvol_req)
/* Re-name volumes */
#define UBI_IOCRNVOL _IOW(UBI_IOC_MAGIC, 3, struct ubi_rnvol_req)

/* ioctl commands of the UBI control character device */

#define UBI_CTRL_IOC_MAGIC 'o'

/* Attach an MTD device */
#define UBI_IOCATT _IOW(UBI_CTRL_IOC_MAGIC, 64, struct ubi_attach_req)
/* Detach an MTD device */
#define UBI_IOCDET _IOW(UBI_CTRL_IOC_MAGIC, 65, __s32)

/* ioctl commands of UBI volume character devices */

#define UBI_VOL_IOC_MAGIC 'O'

/* Start UBI volume update */
#define UBI_IOCVOLUP _IOW(UBI_VOL_IOC_MAGIC, 0, __s64)
/* LEB erasure command, used for debugging, disabled by default */
#define UBI_IOCEBER _IOW(UBI_VOL_IOC_MAGIC, 1, __s32)
/* Atomic LEB change command */
#define UBI_IOCEBCH _IOW(UBI_VOL_IOC_MAGIC, 2, __s32)
/* Map LEB command */
#define UBI_IOCEBMAP _IOW(UBI_VOL_IOC_MAGIC, 3, struct ubi_map_req)
/* Unmap LEB command */
#define UBI_IOCEBUNMAP _IOW(UBI_VOL_IOC_MAGIC, 4, __s32)
/* Check if LEB is mapped command */
#define UBI_IOCEBISMAP _IOR(UBI_VOL_IOC_MAGIC, 5, __s32)
/* Set an UBI volume property */
#define UBI_IOCSETPROP _IOW(UBI_VOL_IOC_MAGIC, 6, struct ubi_set_prop_req)

/* Maximum MTD device name length supported by UBI */
#define MAX_UBI_MTD_NAME_LEN 127

/* Maximum amount of UBI volumes that can be re-named at one go */
#define UBI_MAX_RNVOL 32

/*
 * UBI data type hint constants.
 *
 * UBI_LONGTERM: long-term data
 * UBI_SHORTTERM: short-term data
 * UBI_UNKNOWN: data persistence is unknown
 *
 * These constants are used when data is written to UBI volumes in order to
 * help the UBI wear-leveling unit to find more appropriate physical
 * eraseblocks.
 */
enum {
	UBI_LONGTERM  = 1,
	UBI_SHORTTERM = 2,
	UBI_UNKNOWN   = 3,
};

/*
 * UBI volume type constants.
 *
 * @UBI_DYNAMIC_VOLUME: dynamic volume
 * @UBI_STATIC_VOLUME:  static volume
 */
enum {
	UBI_DYNAMIC_VOLUME = 3,
	UBI_STATIC_VOLUME  = 4,
};

/*
 * UBI set property ioctl constants
 *
 * @UBI_PROP_DIRECT_WRITE: allow / disallow user to directly write and
 *                         erase individual eraseblocks on dynamic volumes
 */
enum {
       UBI_PROP_DIRECT_WRITE = 1,
};

/**
 * struct ubi_attach_req - attach MTD device request.
 * @ubi_num: UBI device number to create
 * @mtd_num: MTD device number to attach
 * @vid_hdr_offset: VID header offset (use defaults if %0)
 * @padding: reserved for future, not used, has to be zeroed
 *
 * This data structure is used to specify MTD device UBI has to attach and the
 * parameters it has to use. The number which should be assigned to the new UBI
 * device is passed in @ubi_num. UBI may automatically assign the number if
 * @UBI_DEV_NUM_AUTO is passed. In this case, the device number is returned in
 * @ubi_num.
 *
 * Most applications should pass %0 in @vid_hdr_offset to make UBI use default
 * offset of the VID header within physical eraseblocks. The default offset is
 * the next min. I/O unit after the EC header. For example, it will be offset
 * 512 in case of a 512 bytes page NAND flash with no sub-page support. Or
 * it will be 512 in case of a 2KiB page NAND flash with 4 512-byte sub-pages.
 *
 * But in rare cases, if this optimizes things, the VID header may be placed to
 * a different offset. For example, the boot-loader might do things faster if
 * the VID header sits at the end of the first 2KiB NAND page with 4 sub-pages.
 * As the boot-loader would not normally need to read EC headers (unless it
 * needs UBI in RW mode), it might be faster to calculate ECC. This is weird
 * example, but it real-life example. So, in this example, @vid_hdr_offer would
 * be 2KiB-64 bytes = 1984. Note, that this position is not even 512-bytes
 * aligned, which is OK, as UBI is clever enough to realize this is 4th
 * sub-page of the first page and add needed padding.
 */
struct ubi_attach_req {
	__s32 ubi_num;
	__s32 mtd_num;
	__s32 vid_hdr_offset;
	__s8 padding[12];
};

/**
 * struct ubi_mkvol_req - volume description data structure used in
 *                        volume creation requests.
 * @vol_id: volume number
 * @alignment: volume alignment
 * @bytes: volume size in bytes
 * @vol_type: volume type (%UBI_DYNAMIC_VOLUME or %UBI_STATIC_VOLUME)
 * @padding1: reserved for future, not used, has to be zeroed
 * @name_len: volume name length
 * @padding2: reserved for future, not used, has to be zeroed
 * @name: volume name
 *
 * This structure is used by user-space programs when creating new volumes. The
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
	__s32 vol_id;
	__s32 alignment;
	__s64 bytes;
	__s8 vol_type;
	__s8 padding1;
	__s16 name_len;
	__s8 padding2[4];
	char name[UBI_MAX_VOLUME_NAME + 1];
} __attribute__ ((packed));

/**
 * struct ubi_rsvol_req - a data structure used in volume re-size requests.
 * @vol_id: ID of the volume to re-size
 * @bytes: new size of the volume in bytes
 *
 * Re-sizing is possible for both dynamic and static volumes. But while dynamic
 * volumes may be re-sized arbitrarily, static volumes cannot be made to be
 * smaller than the number of bytes they bear. To arbitrarily shrink a static
 * volume, it must be wiped out first (by means of volume update operation with
 * zero number of bytes).
 */
struct ubi_rsvol_req {
	__s64 bytes;
	__s32 vol_id;
} __attribute__ ((packed));

/**
 * struct ubi_rnvol_req - volumes re-name request.
 * @count: count of volumes to re-name
 * @padding1:  reserved for future, not used, has to be zeroed
 * @vol_id: ID of the volume to re-name
 * @name_len: name length
 * @padding2:  reserved for future, not used, has to be zeroed
 * @name: new volume name
 *
 * UBI allows to re-name up to %32 volumes at one go. The count of volumes to
 * re-name is specified in the @count field. The ID of the volumes to re-name
 * and the new names are specified in the @vol_id and @name fields.
 *
 * The UBI volume re-name operation is atomic, which means that should power cut
 * happen, the volumes will have either old name or new name. So the possible
 * use-cases of this command is atomic upgrade. Indeed, to upgrade, say, volumes
 * A and B one may create temporary volumes %A1 and %B1 with the new contents,
 * then atomically re-name A1->A and B1->B, in which case old %A and %B will
 * be removed.
 *
 * If it is not desirable to remove old A and B, the re-name request has to
 * contain 4 entries: A1->A, A->A1, B1->B, B->B1, in which case old A1 and B1
 * become A and B, and old A and B will become A1 and B1.
 *
 * It is also OK to request: A1->A, A1->X, B1->B, B->Y, in which case old A1
 * and B1 become A and B, and old A and B become X and Y.
 *
 * In other words, in case of re-naming into an existing volume name, the
 * existing volume is removed, unless it is re-named as well at the same
 * re-name request.
 */
struct ubi_rnvol_req {
	__s32 count;
	__s8 padding1[12];
	struct {
		__s32 vol_id;
		__s16 name_len;
		__s8  padding2[2];
		char    name[UBI_MAX_VOLUME_NAME + 1];
	} ents[UBI_MAX_RNVOL];
} __attribute__ ((packed));

/**
 * struct ubi_leb_change_req - a data structure used in atomic LEB change
 *                             requests.
 * @lnum: logical eraseblock number to change
 * @bytes: how many bytes will be written to the logical eraseblock
 * @dtype: data type (%UBI_LONGTERM, %UBI_SHORTTERM, %UBI_UNKNOWN)
 * @padding: reserved for future, not used, has to be zeroed
 */
struct ubi_leb_change_req {
	__s32 lnum;
	__s32 bytes;
	__s8  dtype;
	__s8  padding[7];
} __attribute__ ((packed));

/**
 * struct ubi_map_req - a data structure used in map LEB requests.
 * @lnum: logical eraseblock number to unmap
 * @dtype: data type (%UBI_LONGTERM, %UBI_SHORTTERM, %UBI_UNKNOWN)
 * @padding: reserved for future, not used, has to be zeroed
 */
struct ubi_map_req {
	__s32 lnum;
	__s8  dtype;
	__s8  padding[3];
} __attribute__ ((packed));


/**
 * struct ubi_set_prop_req - a data structure used to set an ubi volume
 *                           property.
 * @property: property to set (%UBI_PROP_DIRECT_WRITE)
 * @padding: reserved for future, not used, has to be zeroed
 * @value: value to set
 */
struct ubi_set_prop_req {
       __u8  property;
       __u8  padding[7];
       __u64 value;
}  __attribute__ ((packed));

#endif /* __UBI_USER_H__ */
