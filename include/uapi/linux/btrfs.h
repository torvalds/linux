/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef _UAPI_LINUX_BTRFS_H
#define _UAPI_LINUX_BTRFS_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define BTRFS_IOCTL_MAGIC 0x94
#define BTRFS_VOL_NAME_MAX 255

/* this should be 4k */
#define BTRFS_PATH_NAME_MAX 4087
struct btrfs_ioctl_vol_args {
	__s64 fd;
	char name[BTRFS_PATH_NAME_MAX + 1];
};

#define BTRFS_DEVICE_PATH_NAME_MAX 1024

#define BTRFS_SUBVOL_CREATE_ASYNC	(1ULL << 0)
#define BTRFS_SUBVOL_RDONLY		(1ULL << 1)
#define BTRFS_SUBVOL_QGROUP_INHERIT	(1ULL << 2)
#define BTRFS_FSID_SIZE 16
#define BTRFS_UUID_SIZE 16

#define BTRFS_QGROUP_INHERIT_SET_LIMITS	(1ULL << 0)

struct btrfs_qgroup_limit {
	__u64	flags;
	__u64	max_rfer;
	__u64	max_excl;
	__u64	rsv_rfer;
	__u64	rsv_excl;
};

struct btrfs_qgroup_inherit {
	__u64	flags;
	__u64	num_qgroups;
	__u64	num_ref_copies;
	__u64	num_excl_copies;
	struct btrfs_qgroup_limit lim;
	__u64	qgroups[0];
};

struct btrfs_ioctl_qgroup_limit_args {
	__u64	qgroupid;
	struct btrfs_qgroup_limit lim;
};

#define BTRFS_SUBVOL_NAME_MAX 4039
struct btrfs_ioctl_vol_args_v2 {
	__s64 fd;
	__u64 transid;
	__u64 flags;
	union {
		struct {
			__u64 size;
			struct btrfs_qgroup_inherit __user *qgroup_inherit;
		};
		__u64 unused[4];
	};
	char name[BTRFS_SUBVOL_NAME_MAX + 1];
};

/*
 * structure to report errors and progress to userspace, either as a
 * result of a finished scrub, a canceled scrub or a progress inquiry
 */
struct btrfs_scrub_progress {
	__u64 data_extents_scrubbed;	/* # of data extents scrubbed */
	__u64 tree_extents_scrubbed;	/* # of tree extents scrubbed */
	__u64 data_bytes_scrubbed;	/* # of data bytes scrubbed */
	__u64 tree_bytes_scrubbed;	/* # of tree bytes scrubbed */
	__u64 read_errors;		/* # of read errors encountered (EIO) */
	__u64 csum_errors;		/* # of failed csum checks */
	__u64 verify_errors;		/* # of occurences, where the metadata
					 * of a tree block did not match the
					 * expected values, like generation or
					 * logical */
	__u64 no_csum;			/* # of 4k data block for which no csum
					 * is present, probably the result of
					 * data written with nodatasum */
	__u64 csum_discards;		/* # of csum for which no data was found
					 * in the extent tree. */
	__u64 super_errors;		/* # of bad super blocks encountered */
	__u64 malloc_errors;		/* # of internal kmalloc errors. These
					 * will likely cause an incomplete
					 * scrub */
	__u64 uncorrectable_errors;	/* # of errors where either no intact
					 * copy was found or the writeback
					 * failed */
	__u64 corrected_errors;		/* # of errors corrected */
	__u64 last_physical;		/* last physical address scrubbed. In
					 * case a scrub was aborted, this can
					 * be used to restart the scrub */
	__u64 unverified_errors;	/* # of occurences where a read for a
					 * full (64k) bio failed, but the re-
					 * check succeeded for each 4k piece.
					 * Intermittent error. */
};

#define BTRFS_SCRUB_READONLY	1
struct btrfs_ioctl_scrub_args {
	__u64 devid;				/* in */
	__u64 start;				/* in */
	__u64 end;				/* in */
	__u64 flags;				/* in */
	struct btrfs_scrub_progress progress;	/* out */
	/* pad to 1k */
	__u64 unused[(1024-32-sizeof(struct btrfs_scrub_progress))/8];
};

#define BTRFS_IOCTL_DEV_REPLACE_CONT_READING_FROM_SRCDEV_MODE_ALWAYS	0
#define BTRFS_IOCTL_DEV_REPLACE_CONT_READING_FROM_SRCDEV_MODE_AVOID	1
struct btrfs_ioctl_dev_replace_start_params {
	__u64 srcdevid;	/* in, if 0, use srcdev_name instead */
	__u64 cont_reading_from_srcdev_mode;	/* in, see #define
						 * above */
	__u8 srcdev_name[BTRFS_DEVICE_PATH_NAME_MAX + 1];	/* in */
	__u8 tgtdev_name[BTRFS_DEVICE_PATH_NAME_MAX + 1];	/* in */
};

#define BTRFS_IOCTL_DEV_REPLACE_STATE_NEVER_STARTED	0
#define BTRFS_IOCTL_DEV_REPLACE_STATE_STARTED		1
#define BTRFS_IOCTL_DEV_REPLACE_STATE_FINISHED		2
#define BTRFS_IOCTL_DEV_REPLACE_STATE_CANCELED		3
#define BTRFS_IOCTL_DEV_REPLACE_STATE_SUSPENDED		4
struct btrfs_ioctl_dev_replace_status_params {
	__u64 replace_state;	/* out, see #define above */
	__u64 progress_1000;	/* out, 0 <= x <= 1000 */
	__u64 time_started;	/* out, seconds since 1-Jan-1970 */
	__u64 time_stopped;	/* out, seconds since 1-Jan-1970 */
	__u64 num_write_errors;	/* out */
	__u64 num_uncorrectable_read_errors;	/* out */
};

#define BTRFS_IOCTL_DEV_REPLACE_CMD_START			0
#define BTRFS_IOCTL_DEV_REPLACE_CMD_STATUS			1
#define BTRFS_IOCTL_DEV_REPLACE_CMD_CANCEL			2
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_NO_ERROR			0
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_NOT_STARTED		1
#define BTRFS_IOCTL_DEV_REPLACE_RESULT_ALREADY_STARTED		2
struct btrfs_ioctl_dev_replace_args {
	__u64 cmd;	/* in */
	__u64 result;	/* out */

	union {
		struct btrfs_ioctl_dev_replace_start_params start;
		struct btrfs_ioctl_dev_replace_status_params status;
	};	/* in/out */

	__u64 spare[64];
};

struct btrfs_ioctl_dev_info_args {
	__u64 devid;				/* in/out */
	__u8 uuid[BTRFS_UUID_SIZE];		/* in/out */
	__u64 bytes_used;			/* out */
	__u64 total_bytes;			/* out */
	__u64 unused[379];			/* pad to 4k */
	__u8 path[BTRFS_DEVICE_PATH_NAME_MAX];	/* out */
};

struct btrfs_ioctl_fs_info_args {
	__u64 max_id;				/* out */
	__u64 num_devices;			/* out */
	__u8 fsid[BTRFS_FSID_SIZE];		/* out */
	__u64 reserved[124];			/* pad to 1k */
};

/* balance control ioctl modes */
#define BTRFS_BALANCE_CTL_PAUSE		1
#define BTRFS_BALANCE_CTL_CANCEL	2

/*
 * this is packed, because it should be exactly the same as its disk
 * byte order counterpart (struct btrfs_disk_balance_args)
 */
struct btrfs_balance_args {
	__u64 profiles;
	__u64 usage;
	__u64 devid;
	__u64 pstart;
	__u64 pend;
	__u64 vstart;
	__u64 vend;

	__u64 target;

	__u64 flags;

	__u64 unused[8];
} __attribute__ ((__packed__));

/* report balance progress to userspace */
struct btrfs_balance_progress {
	__u64 expected;		/* estimated # of chunks that will be
				 * relocated to fulfill the request */
	__u64 considered;	/* # of chunks we have considered so far */
	__u64 completed;	/* # of chunks relocated so far */
};

#define BTRFS_BALANCE_STATE_RUNNING	(1ULL << 0)
#define BTRFS_BALANCE_STATE_PAUSE_REQ	(1ULL << 1)
#define BTRFS_BALANCE_STATE_CANCEL_REQ	(1ULL << 2)

struct btrfs_ioctl_balance_args {
	__u64 flags;				/* in/out */
	__u64 state;				/* out */

	struct btrfs_balance_args data;		/* in/out */
	struct btrfs_balance_args meta;		/* in/out */
	struct btrfs_balance_args sys;		/* in/out */

	struct btrfs_balance_progress stat;	/* out */

	__u64 unused[72];			/* pad to 1k */
};

#define BTRFS_INO_LOOKUP_PATH_MAX 4080
struct btrfs_ioctl_ino_lookup_args {
	__u64 treeid;
	__u64 objectid;
	char name[BTRFS_INO_LOOKUP_PATH_MAX];
};

struct btrfs_ioctl_search_key {
	/* which root are we searching.  0 is the tree of tree roots */
	__u64 tree_id;

	/* keys returned will be >= min and <= max */
	__u64 min_objectid;
	__u64 max_objectid;

	/* keys returned will be >= min and <= max */
	__u64 min_offset;
	__u64 max_offset;

	/* max and min transids to search for */
	__u64 min_transid;
	__u64 max_transid;

	/* keys returned will be >= min and <= max */
	__u32 min_type;
	__u32 max_type;

	/*
	 * how many items did userland ask for, and how many are we
	 * returning
	 */
	__u32 nr_items;

	/* align to 64 bits */
	__u32 unused;

	/* some extra for later */
	__u64 unused1;
	__u64 unused2;
	__u64 unused3;
	__u64 unused4;
};

struct btrfs_ioctl_search_header {
	__u64 transid;
	__u64 objectid;
	__u64 offset;
	__u32 type;
	__u32 len;
};

#define BTRFS_SEARCH_ARGS_BUFSIZE (4096 - sizeof(struct btrfs_ioctl_search_key))
/*
 * the buf is an array of search headers where
 * each header is followed by the actual item
 * the type field is expanded to 32 bits for alignment
 */
struct btrfs_ioctl_search_args {
	struct btrfs_ioctl_search_key key;
	char buf[BTRFS_SEARCH_ARGS_BUFSIZE];
};

struct btrfs_ioctl_clone_range_args {
  __s64 src_fd;
  __u64 src_offset, src_length;
  __u64 dest_offset;
};

/* flags for the defrag range ioctl */
#define BTRFS_DEFRAG_RANGE_COMPRESS 1
#define BTRFS_DEFRAG_RANGE_START_IO 2

#define BTRFS_SAME_DATA_DIFFERS	1
/* For extent-same ioctl */
struct btrfs_ioctl_same_extent_info {
	__s64 fd;		/* in - destination file */
	__u64 logical_offset;	/* in - start of extent in destination */
	__u64 bytes_deduped;	/* out - total # of bytes we were able
				 * to dedupe from this file */
	/* status of this dedupe operation:
	 * 0 if dedup succeeds
	 * < 0 for error
	 * == BTRFS_SAME_DATA_DIFFERS if data differs
	 */
	__s32 status;		/* out - see above description */
	__u32 reserved;
};

struct btrfs_ioctl_same_args {
	__u64 logical_offset;	/* in - start of extent in source */
	__u64 length;		/* in - length of extent */
	__u16 dest_count;	/* in - total elements in info array */
	__u16 reserved1;
	__u32 reserved2;
	struct btrfs_ioctl_same_extent_info info[0];
};

struct btrfs_ioctl_space_info {
	__u64 flags;
	__u64 total_bytes;
	__u64 used_bytes;
};

struct btrfs_ioctl_space_args {
	__u64 space_slots;
	__u64 total_spaces;
	struct btrfs_ioctl_space_info spaces[0];
};

struct btrfs_data_container {
	__u32	bytes_left;	/* out -- bytes not needed to deliver output */
	__u32	bytes_missing;	/* out -- additional bytes needed for result */
	__u32	elem_cnt;	/* out */
	__u32	elem_missed;	/* out */
	__u64	val[0];		/* out */
};

struct btrfs_ioctl_ino_path_args {
	__u64				inum;		/* in */
	__u64				size;		/* in */
	__u64				reserved[4];
	/* struct btrfs_data_container	*fspath;	   out */
	__u64				fspath;		/* out */
};

struct btrfs_ioctl_logical_ino_args {
	__u64				logical;	/* in */
	__u64				size;		/* in */
	__u64				reserved[4];
	/* struct btrfs_data_container	*inodes;	out   */
	__u64				inodes;
};

enum btrfs_dev_stat_values {
	/* disk I/O failure stats */
	BTRFS_DEV_STAT_WRITE_ERRS, /* EIO or EREMOTEIO from lower layers */
	BTRFS_DEV_STAT_READ_ERRS, /* EIO or EREMOTEIO from lower layers */
	BTRFS_DEV_STAT_FLUSH_ERRS, /* EIO or EREMOTEIO from lower layers */

	/* stats for indirect indications for I/O failures */
	BTRFS_DEV_STAT_CORRUPTION_ERRS, /* checksum error, bytenr error or
					 * contents is illegal: this is an
					 * indication that the block was damaged
					 * during read or write, or written to
					 * wrong location or read from wrong
					 * location */
	BTRFS_DEV_STAT_GENERATION_ERRS, /* an indication that blocks have not
					 * been written */

	BTRFS_DEV_STAT_VALUES_MAX
};

/* Reset statistics after reading; needs SYS_ADMIN capability */
#define	BTRFS_DEV_STATS_RESET		(1ULL << 0)

struct btrfs_ioctl_get_dev_stats {
	__u64 devid;				/* in */
	__u64 nr_items;				/* in/out */
	__u64 flags;				/* in/out */

	/* out values: */
	__u64 values[BTRFS_DEV_STAT_VALUES_MAX];

	__u64 unused[128 - 2 - BTRFS_DEV_STAT_VALUES_MAX]; /* pad to 1k */
};

#define BTRFS_QUOTA_CTL_ENABLE	1
#define BTRFS_QUOTA_CTL_DISABLE	2
#define BTRFS_QUOTA_CTL_RESCAN__NOTUSED	3
struct btrfs_ioctl_quota_ctl_args {
	__u64 cmd;
	__u64 status;
};

struct btrfs_ioctl_quota_rescan_args {
	__u64	flags;
	__u64   progress;
	__u64   reserved[6];
};

struct btrfs_ioctl_qgroup_assign_args {
	__u64 assign;
	__u64 src;
	__u64 dst;
};

struct btrfs_ioctl_qgroup_create_args {
	__u64 create;
	__u64 qgroupid;
};
struct btrfs_ioctl_timespec {
	__u64 sec;
	__u32 nsec;
};

struct btrfs_ioctl_received_subvol_args {
	char	uuid[BTRFS_UUID_SIZE];	/* in */
	__u64	stransid;		/* in */
	__u64	rtransid;		/* out */
	struct btrfs_ioctl_timespec stime; /* in */
	struct btrfs_ioctl_timespec rtime; /* out */
	__u64	flags;			/* in */
	__u64	reserved[16];		/* in */
};

/*
 * Caller doesn't want file data in the send stream, even if the
 * search of clone sources doesn't find an extent. UPDATE_EXTENT
 * commands will be sent instead of WRITE commands.
 */
#define BTRFS_SEND_FLAG_NO_FILE_DATA		0x1

/*
 * Do not add the leading stream header. Used when multiple snapshots
 * are sent back to back.
 */
#define BTRFS_SEND_FLAG_OMIT_STREAM_HEADER	0x2

/*
 * Omit the command at the end of the stream that indicated the end
 * of the stream. This option is used when multiple snapshots are
 * sent back to back.
 */
#define BTRFS_SEND_FLAG_OMIT_END_CMD		0x4

#define BTRFS_SEND_FLAG_MASK \
	(BTRFS_SEND_FLAG_NO_FILE_DATA | \
	 BTRFS_SEND_FLAG_OMIT_STREAM_HEADER | \
	 BTRFS_SEND_FLAG_OMIT_END_CMD)

struct btrfs_ioctl_send_args {
	__s64 send_fd;			/* in */
	__u64 clone_sources_count;	/* in */
	__u64 __user *clone_sources;	/* in */
	__u64 parent_root;		/* in */
	__u64 flags;			/* in */
	__u64 reserved[4];		/* in */
};

/* Error codes as returned by the kernel */
enum btrfs_err_code {
	notused,
	BTRFS_ERROR_DEV_RAID1_MIN_NOT_MET,
	BTRFS_ERROR_DEV_RAID10_MIN_NOT_MET,
	BTRFS_ERROR_DEV_RAID5_MIN_NOT_MET,
	BTRFS_ERROR_DEV_RAID6_MIN_NOT_MET,
	BTRFS_ERROR_DEV_TGT_REPLACE,
	BTRFS_ERROR_DEV_MISSING_NOT_FOUND,
	BTRFS_ERROR_DEV_ONLY_WRITABLE,
	BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS
};
/* An error code to error string mapping for the kernel
*  error codes
*/
static inline char *btrfs_err_str(enum btrfs_err_code err_code)
{
	switch (err_code) {
		case BTRFS_ERROR_DEV_RAID1_MIN_NOT_MET:
			return "unable to go below two devices on raid1";
		case BTRFS_ERROR_DEV_RAID10_MIN_NOT_MET:
			return "unable to go below four devices on raid10";
		case BTRFS_ERROR_DEV_RAID5_MIN_NOT_MET:
			return "unable to go below two devices on raid5";
		case BTRFS_ERROR_DEV_RAID6_MIN_NOT_MET:
			return "unable to go below three devices on raid6";
		case BTRFS_ERROR_DEV_TGT_REPLACE:
			return "unable to remove the dev_replace target dev";
		case BTRFS_ERROR_DEV_MISSING_NOT_FOUND:
			return "no missing devices found to remove";
		case BTRFS_ERROR_DEV_ONLY_WRITABLE:
			return "unable to remove the only writeable device";
		case BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS:
			return "add/delete/balance/replace/resize operation "\
				"in progress";
		default:
			return NULL;
	}
}

#define BTRFS_IOC_SNAP_CREATE _IOW(BTRFS_IOCTL_MAGIC, 1, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_DEFRAG _IOW(BTRFS_IOCTL_MAGIC, 2, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_RESIZE _IOW(BTRFS_IOCTL_MAGIC, 3, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_SCAN_DEV _IOW(BTRFS_IOCTL_MAGIC, 4, \
				   struct btrfs_ioctl_vol_args)
/* trans start and trans end are dangerous, and only for
 * use by applications that know how to avoid the
 * resulting deadlocks
 */
#define BTRFS_IOC_TRANS_START  _IO(BTRFS_IOCTL_MAGIC, 6)
#define BTRFS_IOC_TRANS_END    _IO(BTRFS_IOCTL_MAGIC, 7)
#define BTRFS_IOC_SYNC         _IO(BTRFS_IOCTL_MAGIC, 8)

#define BTRFS_IOC_CLONE        _IOW(BTRFS_IOCTL_MAGIC, 9, int)
#define BTRFS_IOC_ADD_DEV _IOW(BTRFS_IOCTL_MAGIC, 10, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_RM_DEV _IOW(BTRFS_IOCTL_MAGIC, 11, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_BALANCE _IOW(BTRFS_IOCTL_MAGIC, 12, \
				   struct btrfs_ioctl_vol_args)

#define BTRFS_IOC_CLONE_RANGE _IOW(BTRFS_IOCTL_MAGIC, 13, \
				  struct btrfs_ioctl_clone_range_args)

#define BTRFS_IOC_SUBVOL_CREATE _IOW(BTRFS_IOCTL_MAGIC, 14, \
				   struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_SNAP_DESTROY _IOW(BTRFS_IOCTL_MAGIC, 15, \
				struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_DEFRAG_RANGE _IOW(BTRFS_IOCTL_MAGIC, 16, \
				struct btrfs_ioctl_defrag_range_args)
#define BTRFS_IOC_TREE_SEARCH _IOWR(BTRFS_IOCTL_MAGIC, 17, \
				   struct btrfs_ioctl_search_args)
#define BTRFS_IOC_INO_LOOKUP _IOWR(BTRFS_IOCTL_MAGIC, 18, \
				   struct btrfs_ioctl_ino_lookup_args)
#define BTRFS_IOC_DEFAULT_SUBVOL _IOW(BTRFS_IOCTL_MAGIC, 19, __u64)
#define BTRFS_IOC_SPACE_INFO _IOWR(BTRFS_IOCTL_MAGIC, 20, \
				    struct btrfs_ioctl_space_args)
#define BTRFS_IOC_START_SYNC _IOR(BTRFS_IOCTL_MAGIC, 24, __u64)
#define BTRFS_IOC_WAIT_SYNC  _IOW(BTRFS_IOCTL_MAGIC, 22, __u64)
#define BTRFS_IOC_SNAP_CREATE_V2 _IOW(BTRFS_IOCTL_MAGIC, 23, \
				   struct btrfs_ioctl_vol_args_v2)
#define BTRFS_IOC_SUBVOL_CREATE_V2 _IOW(BTRFS_IOCTL_MAGIC, 24, \
				   struct btrfs_ioctl_vol_args_v2)
#define BTRFS_IOC_SUBVOL_GETFLAGS _IOR(BTRFS_IOCTL_MAGIC, 25, __u64)
#define BTRFS_IOC_SUBVOL_SETFLAGS _IOW(BTRFS_IOCTL_MAGIC, 26, __u64)
#define BTRFS_IOC_SCRUB _IOWR(BTRFS_IOCTL_MAGIC, 27, \
			      struct btrfs_ioctl_scrub_args)
#define BTRFS_IOC_SCRUB_CANCEL _IO(BTRFS_IOCTL_MAGIC, 28)
#define BTRFS_IOC_SCRUB_PROGRESS _IOWR(BTRFS_IOCTL_MAGIC, 29, \
				       struct btrfs_ioctl_scrub_args)
#define BTRFS_IOC_DEV_INFO _IOWR(BTRFS_IOCTL_MAGIC, 30, \
				 struct btrfs_ioctl_dev_info_args)
#define BTRFS_IOC_FS_INFO _IOR(BTRFS_IOCTL_MAGIC, 31, \
			       struct btrfs_ioctl_fs_info_args)
#define BTRFS_IOC_BALANCE_V2 _IOWR(BTRFS_IOCTL_MAGIC, 32, \
				   struct btrfs_ioctl_balance_args)
#define BTRFS_IOC_BALANCE_CTL _IOW(BTRFS_IOCTL_MAGIC, 33, int)
#define BTRFS_IOC_BALANCE_PROGRESS _IOR(BTRFS_IOCTL_MAGIC, 34, \
					struct btrfs_ioctl_balance_args)
#define BTRFS_IOC_INO_PATHS _IOWR(BTRFS_IOCTL_MAGIC, 35, \
					struct btrfs_ioctl_ino_path_args)
#define BTRFS_IOC_LOGICAL_INO _IOWR(BTRFS_IOCTL_MAGIC, 36, \
					struct btrfs_ioctl_ino_path_args)
#define BTRFS_IOC_SET_RECEIVED_SUBVOL _IOWR(BTRFS_IOCTL_MAGIC, 37, \
				struct btrfs_ioctl_received_subvol_args)
#define BTRFS_IOC_SEND _IOW(BTRFS_IOCTL_MAGIC, 38, struct btrfs_ioctl_send_args)
#define BTRFS_IOC_DEVICES_READY _IOR(BTRFS_IOCTL_MAGIC, 39, \
				     struct btrfs_ioctl_vol_args)
#define BTRFS_IOC_QUOTA_CTL _IOWR(BTRFS_IOCTL_MAGIC, 40, \
			       struct btrfs_ioctl_quota_ctl_args)
#define BTRFS_IOC_QGROUP_ASSIGN _IOW(BTRFS_IOCTL_MAGIC, 41, \
			       struct btrfs_ioctl_qgroup_assign_args)
#define BTRFS_IOC_QGROUP_CREATE _IOW(BTRFS_IOCTL_MAGIC, 42, \
			       struct btrfs_ioctl_qgroup_create_args)
#define BTRFS_IOC_QGROUP_LIMIT _IOR(BTRFS_IOCTL_MAGIC, 43, \
			       struct btrfs_ioctl_qgroup_limit_args)
#define BTRFS_IOC_QUOTA_RESCAN _IOW(BTRFS_IOCTL_MAGIC, 44, \
			       struct btrfs_ioctl_quota_rescan_args)
#define BTRFS_IOC_QUOTA_RESCAN_STATUS _IOR(BTRFS_IOCTL_MAGIC, 45, \
			       struct btrfs_ioctl_quota_rescan_args)
#define BTRFS_IOC_QUOTA_RESCAN_WAIT _IO(BTRFS_IOCTL_MAGIC, 46)
#define BTRFS_IOC_GET_FSLABEL _IOR(BTRFS_IOCTL_MAGIC, 49, \
				   char[BTRFS_LABEL_SIZE])
#define BTRFS_IOC_SET_FSLABEL _IOW(BTRFS_IOCTL_MAGIC, 50, \
				   char[BTRFS_LABEL_SIZE])
#define BTRFS_IOC_GET_DEV_STATS _IOWR(BTRFS_IOCTL_MAGIC, 52, \
				      struct btrfs_ioctl_get_dev_stats)
#define BTRFS_IOC_DEV_REPLACE _IOWR(BTRFS_IOCTL_MAGIC, 53, \
				    struct btrfs_ioctl_dev_replace_args)
#define BTRFS_IOC_FILE_EXTENT_SAME _IOWR(BTRFS_IOCTL_MAGIC, 54, \
					 struct btrfs_ioctl_same_args)

#endif /* _UAPI_LINUX_BTRFS_H */
