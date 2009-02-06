/*
   md_p.h : physical layout of Linux RAID devices
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_P_H
#define _MD_P_H

#include <linux/types.h>

/*
 * RAID superblock.
 *
 * The RAID superblock maintains some statistics on each RAID configuration.
 * Each real device in the RAID set contains it near the end of the device.
 * Some of the ideas are copied from the ext2fs implementation.
 *
 * We currently use 4096 bytes as follows:
 *
 *	word offset	function
 *
 *	   0  -    31	Constant generic RAID device information.
 *        32  -    63   Generic state information.
 *	  64  -   127	Personality specific information.
 *	 128  -   511	12 32-words descriptors of the disks in the raid set.
 *	 512  -   911	Reserved.
 *	 912  -  1023	Disk specific descriptor.
 */

/*
 * If x is the real device size in bytes, we return an apparent size of:
 *
 *	y = (x & ~(MD_RESERVED_BYTES - 1)) - MD_RESERVED_BYTES
 *
 * and place the 4kB superblock at offset y.
 */
#define MD_RESERVED_BYTES		(64 * 1024)
#define MD_RESERVED_SECTORS		(MD_RESERVED_BYTES / 512)

#define MD_NEW_SIZE_SECTORS(x)		((x & ~(MD_RESERVED_SECTORS - 1)) - MD_RESERVED_SECTORS)

#define MD_SB_BYTES			4096
#define MD_SB_WORDS			(MD_SB_BYTES / 4)
#define MD_SB_SECTORS			(MD_SB_BYTES / 512)

/*
 * The following are counted in 32-bit words
 */
#define	MD_SB_GENERIC_OFFSET		0
#define MD_SB_PERSONALITY_OFFSET	64
#define MD_SB_DISKS_OFFSET		128
#define MD_SB_DESCRIPTOR_OFFSET		992

#define MD_SB_GENERIC_CONSTANT_WORDS	32
#define MD_SB_GENERIC_STATE_WORDS	32
#define MD_SB_GENERIC_WORDS		(MD_SB_GENERIC_CONSTANT_WORDS + MD_SB_GENERIC_STATE_WORDS)
#define MD_SB_PERSONALITY_WORDS		64
#define MD_SB_DESCRIPTOR_WORDS		32
#define MD_SB_DISKS			27
#define MD_SB_DISKS_WORDS		(MD_SB_DISKS*MD_SB_DESCRIPTOR_WORDS)
#define MD_SB_RESERVED_WORDS		(1024 - MD_SB_GENERIC_WORDS - MD_SB_PERSONALITY_WORDS - MD_SB_DISKS_WORDS - MD_SB_DESCRIPTOR_WORDS)
#define MD_SB_EQUAL_WORDS		(MD_SB_GENERIC_WORDS + MD_SB_PERSONALITY_WORDS + MD_SB_DISKS_WORDS)

/*
 * Device "operational" state bits
 */
#define MD_DISK_FAULTY		0 /* disk is faulty / operational */
#define MD_DISK_ACTIVE		1 /* disk is running or spare disk */
#define MD_DISK_SYNC		2 /* disk is in sync with the raid set */
#define MD_DISK_REMOVED		3 /* disk is in sync with the raid set */

#define	MD_DISK_WRITEMOSTLY	9 /* disk is "write-mostly" is RAID1 config.
				   * read requests will only be sent here in
				   * dire need
				   */

typedef struct mdp_device_descriptor_s {
	__u32 number;		/* 0 Device number in the entire set	      */
	__u32 major;		/* 1 Device major number		      */
	__u32 minor;		/* 2 Device minor number		      */
	__u32 raid_disk;	/* 3 The role of the device in the raid set   */
	__u32 state;		/* 4 Operational state			      */
	__u32 reserved[MD_SB_DESCRIPTOR_WORDS - 5];
} mdp_disk_t;

#define MD_SB_MAGIC		0xa92b4efc

/*
 * Superblock state bits
 */
#define MD_SB_CLEAN		0
#define MD_SB_ERRORS		1

#define	MD_SB_BITMAP_PRESENT	8 /* bitmap may be present nearby */

/*
 * Notes:
 * - if an array is being reshaped (restriped) in order to change the
 *   the number of active devices in the array, 'raid_disks' will be
 *   the larger of the old and new numbers.  'delta_disks' will
 *   be the "new - old".  So if +ve, raid_disks is the new value, and
 *   "raid_disks-delta_disks" is the old.  If -ve, raid_disks is the
 *   old value and "raid_disks+delta_disks" is the new (smaller) value.
 */


typedef struct mdp_superblock_s {
	/*
	 * Constant generic information
	 */
	__u32 md_magic;		/*  0 MD identifier 			      */
	__u32 major_version;	/*  1 major version to which the set conforms */
	__u32 minor_version;	/*  2 minor version ...			      */
	__u32 patch_version;	/*  3 patchlevel version ...		      */
	__u32 gvalid_words;	/*  4 Number of used words in this section    */
	__u32 set_uuid0;	/*  5 Raid set identifier		      */
	__u32 ctime;		/*  6 Creation time			      */
	__u32 level;		/*  7 Raid personality			      */
	__u32 size;		/*  8 Apparent size of each individual disk   */
	__u32 nr_disks;		/*  9 total disks in the raid set	      */
	__u32 raid_disks;	/* 10 disks in a fully functional raid set    */
	__u32 md_minor;		/* 11 preferred MD minor device number	      */
	__u32 not_persistent;	/* 12 does it have a persistent superblock    */
	__u32 set_uuid1;	/* 13 Raid set identifier #2		      */
	__u32 set_uuid2;	/* 14 Raid set identifier #3		      */
	__u32 set_uuid3;	/* 15 Raid set identifier #4		      */
	__u32 gstate_creserved[MD_SB_GENERIC_CONSTANT_WORDS - 16];

	/*
	 * Generic state information
	 */
	__u32 utime;		/*  0 Superblock update time		      */
	__u32 state;		/*  1 State bits (clean, ...)		      */
	__u32 active_disks;	/*  2 Number of currently active disks	      */
	__u32 working_disks;	/*  3 Number of working disks		      */
	__u32 failed_disks;	/*  4 Number of failed disks		      */
	__u32 spare_disks;	/*  5 Number of spare disks		      */
	__u32 sb_csum;		/*  6 checksum of the whole superblock        */
#ifdef __BIG_ENDIAN
	__u32 events_hi;	/*  7 high-order of superblock update count   */
	__u32 events_lo;	/*  8 low-order of superblock update count    */
	__u32 cp_events_hi;	/*  9 high-order of checkpoint update count   */
	__u32 cp_events_lo;	/* 10 low-order of checkpoint update count    */
#else
	__u32 events_lo;	/*  7 low-order of superblock update count    */
	__u32 events_hi;	/*  8 high-order of superblock update count   */
	__u32 cp_events_lo;	/*  9 low-order of checkpoint update count    */
	__u32 cp_events_hi;	/* 10 high-order of checkpoint update count   */
#endif
	__u32 recovery_cp;	/* 11 recovery checkpoint sector count	      */
	/* There are only valid for minor_version > 90 */
	__u64 reshape_position;	/* 12,13 next address in array-space for reshape */
	__u32 new_level;	/* 14 new level we are reshaping to	      */
	__u32 delta_disks;	/* 15 change in number of raid_disks	      */
	__u32 new_layout;	/* 16 new layout			      */
	__u32 new_chunk;	/* 17 new chunk size (bytes)		      */
	__u32 gstate_sreserved[MD_SB_GENERIC_STATE_WORDS - 18];

	/*
	 * Personality information
	 */
	__u32 layout;		/*  0 the array's physical layout	      */
	__u32 chunk_size;	/*  1 chunk size in bytes		      */
	__u32 root_pv;		/*  2 LV root PV */
	__u32 root_block;	/*  3 LV root block */
	__u32 pstate_reserved[MD_SB_PERSONALITY_WORDS - 4];

	/*
	 * Disks information
	 */
	mdp_disk_t disks[MD_SB_DISKS];

	/*
	 * Reserved
	 */
	__u32 reserved[MD_SB_RESERVED_WORDS];

	/*
	 * Active descriptor
	 */
	mdp_disk_t this_disk;

} mdp_super_t;

static inline __u64 md_event(mdp_super_t *sb) {
	__u64 ev = sb->events_hi;
	return (ev<<32)| sb->events_lo;
}

#define MD_SUPERBLOCK_1_TIME_SEC_MASK ((1ULL<<40) - 1)

/*
 * The version-1 superblock :
 * All numeric fields are little-endian.
 *
 * total size: 256 bytes plus 2 per device.
 *  1K allows 384 devices.
 */
struct mdp_superblock_1 {
	/* constant array information - 128 bytes */
	__le32	magic;		/* MD_SB_MAGIC: 0xa92b4efc - little endian */
	__le32	major_version;	/* 1 */
	__le32	feature_map;	/* bit 0 set if 'bitmap_offset' is meaningful */
	__le32	pad0;		/* always set to 0 when writing */

	__u8	set_uuid[16];	/* user-space generated. */
	char	set_name[32];	/* set and interpreted by user-space */

	__le64	ctime;		/* lo 40 bits are seconds, top 24 are microseconds or 0*/
	__le32	level;		/* -4 (multipath), -1 (linear), 0,1,4,5 */
	__le32	layout;		/* only for raid5 and raid10 currently */
	__le64	size;		/* used size of component devices, in 512byte sectors */

	__le32	chunksize;	/* in 512byte sectors */
	__le32	raid_disks;
	__le32	bitmap_offset;	/* sectors after start of superblock that bitmap starts
				 * NOTE: signed, so bitmap can be before superblock
				 * only meaningful of feature_map[0] is set.
				 */

	/* These are only valid with feature bit '4' */
	__le32	new_level;	/* new level we are reshaping to		*/
	__le64	reshape_position;	/* next address in array-space for reshape */
	__le32	delta_disks;	/* change in number of raid_disks		*/
	__le32	new_layout;	/* new layout					*/
	__le32	new_chunk;	/* new chunk size (bytes)			*/
	__u8	pad1[128-124];	/* set to 0 when written */

	/* constant this-device information - 64 bytes */
	__le64	data_offset;	/* sector start of data, often 0 */
	__le64	data_size;	/* sectors in this device that can be used for data */
	__le64	super_offset;	/* sector start of this superblock */
	__le64	recovery_offset;/* sectors before this offset (from data_offset) have been recovered */
	__le32	dev_number;	/* permanent identifier of this  device - not role in raid */
	__le32	cnt_corrected_read; /* number of read errors that were corrected by re-writing */
	__u8	device_uuid[16]; /* user-space setable, ignored by kernel */
	__u8	devflags;	/* per-device flags.  Only one defined...*/
#define	WriteMostly1	1	/* mask for writemostly flag in above */
	__u8	pad2[64-57];	/* set to 0 when writing */

	/* array state information - 64 bytes */
	__le64	utime;		/* 40 bits second, 24 btes microseconds */
	__le64	events;		/* incremented when superblock updated */
	__le64	resync_offset;	/* data before this offset (from data_offset) known to be in sync */
	__le32	sb_csum;	/* checksum upto devs[max_dev] */
	__le32	max_dev;	/* size of devs[] array to consider */
	__u8	pad3[64-32];	/* set to 0 when writing */

	/* device state information. Indexed by dev_number.
	 * 2 bytes per device
	 * Note there are no per-device state flags. State information is rolled
	 * into the 'roles' value.  If a device is spare or faulty, then it doesn't
	 * have a meaningful role.
	 */
	__le16	dev_roles[0];	/* role in array, or 0xffff for a spare, or 0xfffe for faulty */
};

/* feature_map bits */
#define MD_FEATURE_BITMAP_OFFSET	1
#define	MD_FEATURE_RECOVERY_OFFSET	2 /* recovery_offset is present and
					   * must be honoured
					   */
#define	MD_FEATURE_RESHAPE_ACTIVE	4

#define	MD_FEATURE_ALL			(1|2|4)

#endif 

