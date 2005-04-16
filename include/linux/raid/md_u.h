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

/* ioctls */

/* status */
#define RAID_VERSION		_IOR (MD_MAJOR, 0x10, mdu_version_t)
#define GET_ARRAY_INFO		_IOR (MD_MAJOR, 0x11, mdu_array_info_t)
#define GET_DISK_INFO		_IOR (MD_MAJOR, 0x12, mdu_disk_info_t)
#define PRINT_RAID_DEBUG	_IO (MD_MAJOR, 0x13)
#define RAID_AUTORUN		_IO (MD_MAJOR, 0x14)

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

/* usage */
#define RUN_ARRAY		_IOW (MD_MAJOR, 0x30, mdu_param_t)
#define START_ARRAY		_IO (MD_MAJOR, 0x31)
#define STOP_ARRAY		_IO (MD_MAJOR, 0x32)
#define STOP_ARRAY_RO		_IO (MD_MAJOR, 0x33)
#define RESTART_ARRAY_RW	_IO (MD_MAJOR, 0x34)

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

typedef struct mdu_param_s
{
	int			personality;	/* 1,2,3,4 */
	int			chunk_size;	/* in bytes */
	int			max_fault;	/* unused for now */
} mdu_param_t;

#endif 

