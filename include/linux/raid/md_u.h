/*
   md_u.h : user <=> kernel API between Linux raidtools and RAID drivers
          Copyright (C) 1998 Ingo Molnar
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_U_H
#define _MD_U_H

/*
 * Different major versions are not compatible.
 * Different minor versions are only downward compatible.
 * Different patchlevel versions are downward and upward compatible.
 */
#define MD_MAJOR_VERSION                0
#define MD_MINOR_VERSION                90
/*
 * MD_PATCHLEVEL_VERSION indicates kernel functionality.
 * >=1 means different superblock formats are selectable using SET_ARRAY_INFO
 *     and major_version/minor_version accordingly
 * >=2 means that Internal bitmaps are supported by setting MD_SB_BITMAP_PRESENT
 *     in the super status byte
 * >=3 means that bitmap superblock version 4 is supported, which uses
 *     little-ending representation rather than host-endian
 */
#define MD_PATCHLEVEL_VERSION           3

/* ioctls */

/* status */
#define RAID_VERSION		_IOR (MD_MAJOR, 0x10, mdu_version_t)
#define GET_ARRAY_INFO		_IOR (MD_MAJOR, 0x11, mdu_array_info_t)
#define GET_DISK_INFO		_IOR (MD_MAJOR, 0x12, mdu_disk_info_t)
#define PRINT_RAID_DEBUG	_IO (MD_MAJOR, 0x13)
#define RAID_AUTORUN		_IO (MD_MAJOR, 0x14)
#define GET_BITMAP_FILE		_IOR (MD_MAJOR, 0x15, mdu_bitmap_file_t)

/* configuration */
#define CLEAR_ARRAY		_IO (MD_MAJOR, 0x20)
#define ADD_NEW_DISK		_IOW (MD_MAJOR, 0x21, mdu_disk_info_t)
#define HOT_REMOVE_DISK		_IO (MD_MAJOR, 0x22)
#define SET_ARRAY_INFO		_IOW (MD_MAJOR, 0x23, mdu_array_info_t)
#define SET_DISK_INFO		_IO (MD_MAJOR, 0x24)
#define WRITE_RAID_INFO		_IO (MD_MAJOR, 0x25)
#define UNPROTECT_ARRAY		_IO (MD_MAJOR, 0x26)
#define PROTECT_ARRAY		_IO (MD_MAJOR, 0x27)
#define HOT_ADD_DISK		_IO (MD_MAJOR, 0x28)
#define SET_DISK_FAULTY		_IO (MD_MAJOR, 0x29)
#define HOT_GENERATE_ERROR	_IO (MD_MAJOR, 0x2a)
#define SET_BITMAP_FILE		_IOW (MD_MAJOR, 0x2b, int)

/* usage */
#define RUN_ARRAY		_IOW (MD_MAJOR, 0x30, mdu_param_t)
/*  0x31 was START_ARRAY  */
#define STOP_ARRAY		_IO (MD_MAJOR, 0x32)
#define STOP_ARRAY_RO		_IO (MD_MAJOR, 0x33)
#define RESTART_ARRAY_RW	_IO (MD_MAJOR, 0x34)

/* 63 partitions with the alternate major number (mdp) */
#define MdpMinorShift 6
#ifdef __KERNEL__
extern int mdp_major;
#endif

typedef struct mdu_version_s {
	int major;
	int minor;
	int patchlevel;
} mdu_version_t;

typedef struct mdu_array_info_s {
	/*
	 * Generic constant information
	 */
	int major_version;
	int minor_version;
	int patch_version;
	int ctime;
	int level;
	int size;
	int nr_disks;
	int raid_disks;
	int md_minor;
	int not_persistent;

	/*
	 * Generic state information
	 */
	int utime;		/*  0 Superblock update time		      */
	int state;		/*  1 State bits (clean, ...)		      */
	int active_disks;	/*  2 Number of currently active disks	      */
	int working_disks;	/*  3 Number of working disks		      */
	int failed_disks;	/*  4 Number of failed disks		      */
	int spare_disks;	/*  5 Number of spare disks		      */

	/*
	 * Personality information
	 */
	int layout;		/*  0 the array's physical layout	      */
	int chunk_size;	/*  1 chunk size in bytes		      */

} mdu_array_info_t;

/* non-obvious values for 'level' */
#define	LEVEL_MULTIPATH		(-4)
#define	LEVEL_LINEAR		(-1)
#define	LEVEL_FAULTY		(-5)

/* we need a value for 'no level specified' and 0
 * means 'raid0', so we need something else.  This is
 * for internal use only
 */
#define	LEVEL_NONE		(-1000000)

typedef struct mdu_disk_info_s {
	/*
	 * configuration/status of one particular disk
	 */
	int number;
	int major;
	int minor;
	int raid_disk;
	int state;

} mdu_disk_info_t;

typedef struct mdu_start_info_s {
	/*
	 * configuration/status of one particular disk
	 */
	int major;
	int minor;
	int raid_disk;
	int state;

} mdu_start_info_t;

typedef struct mdu_bitmap_file_s
{
	char pathname[4096];
} mdu_bitmap_file_t;

typedef struct mdu_param_s
{
	int			personality;	/* 1,2,3,4 */
	int			chunk_size;	/* in bytes */
	int			max_fault;	/* unused for now */
} mdu_param_t;

#endif 

