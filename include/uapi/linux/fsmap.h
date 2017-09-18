/*
 * FS_IOC_GETFSMAP ioctl infrastructure.
 *
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#ifndef _LINUX_FSMAP_H
#define _LINUX_FSMAP_H

#include <linux/types.h>

/*
 *	Structure for FS_IOC_GETFSMAP.
 *
 *	The memory layout for this call are the scalar values defined in
 *	struct fsmap_head, followed by two struct fsmap that describe
 *	the lower and upper bound of mappings to return, followed by an
 *	array of struct fsmap mappings.
 *
 *	fmh_iflags control the output of the call, whereas fmh_oflags report
 *	on the overall record output.  fmh_count should be set to the
 *	length of the fmh_recs array, and fmh_entries will be set to the
 *	number of entries filled out during each call.  If fmh_count is
 *	zero, the number of reverse mappings will be returned in
 *	fmh_entries, though no mappings will be returned.  fmh_reserved
 *	must be set to zero.
 *
 *	The two elements in the fmh_keys array are used to constrain the
 *	output.  The first element in the array should represent the
 *	lowest disk mapping ("low key") that the user wants to learn
 *	about.  If this value is all zeroes, the filesystem will return
 *	the first entry it knows about.  For a subsequent call, the
 *	contents of fsmap_head.fmh_recs[fsmap_head.fmh_count - 1] should be
 *	copied into fmh_keys[0] to have the kernel start where it left off.
 *
 *	The second element in the fmh_keys array should represent the
 *	highest disk mapping ("high key") that the user wants to learn
 *	about.  If this value is all ones, the filesystem will not stop
 *	until it runs out of mapping to return or runs out of space in
 *	fmh_recs.
 *
 *	fmr_device can be either a 32-bit cookie representing a device, or
 *	a 32-bit dev_t if the FMH_OF_DEV_T flag is set.  fmr_physical,
 *	fmr_offset, and fmr_length are expressed in units of bytes.
 *	fmr_owner is either an inode number, or a special value if
 *	FMR_OF_SPECIAL_OWNER is set in fmr_flags.
 */
struct fsmap {
	__u32		fmr_device;	/* device id */
	__u32		fmr_flags;	/* mapping flags */
	__u64		fmr_physical;	/* device offset of segment */
	__u64		fmr_owner;	/* owner id */
	__u64		fmr_offset;	/* file offset of segment */
	__u64		fmr_length;	/* length of segment */
	__u64		fmr_reserved[3];	/* must be zero */
};

struct fsmap_head {
	__u32		fmh_iflags;	/* control flags */
	__u32		fmh_oflags;	/* output flags */
	__u32		fmh_count;	/* # of entries in array incl. input */
	__u32		fmh_entries;	/* # of entries filled in (output). */
	__u64		fmh_reserved[6];	/* must be zero */

	struct fsmap	fmh_keys[2];	/* low and high keys for the mapping search */
	struct fsmap	fmh_recs[];	/* returned records */
};

/* Size of an fsmap_head with room for nr records. */
static inline size_t
fsmap_sizeof(
	unsigned int	nr)
{
	return sizeof(struct fsmap_head) + nr * sizeof(struct fsmap);
}

/* Start the next fsmap query at the end of the current query results. */
static inline void
fsmap_advance(
	struct fsmap_head	*head)
{
	head->fmh_keys[0] = head->fmh_recs[head->fmh_entries - 1];
}

/*	fmh_iflags values - set by FS_IOC_GETFSMAP caller in the header. */
/* no flags defined yet */
#define FMH_IF_VALID		0

/*	fmh_oflags values - returned in the header segment only. */
#define FMH_OF_DEV_T		0x1	/* fmr_device values will be dev_t */

/*	fmr_flags values - returned for each non-header segment */
#define FMR_OF_PREALLOC		0x1	/* segment = unwritten pre-allocation */
#define FMR_OF_ATTR_FORK	0x2	/* segment = attribute fork */
#define FMR_OF_EXTENT_MAP	0x4	/* segment = extent map */
#define FMR_OF_SHARED		0x8	/* segment = shared with another file */
#define FMR_OF_SPECIAL_OWNER	0x10	/* owner is a special value */
#define FMR_OF_LAST		0x20	/* segment is the last in the dataset */

/* Each FS gets to define its own special owner codes. */
#define FMR_OWNER(type, code)	(((__u64)type << 32) | \
				 ((__u64)code & 0xFFFFFFFFULL))
#define FMR_OWNER_TYPE(owner)	((__u32)((__u64)owner >> 32))
#define FMR_OWNER_CODE(owner)	((__u32)(((__u64)owner & 0xFFFFFFFFULL)))
#define FMR_OWN_FREE		FMR_OWNER(0, 1) /* free space */
#define FMR_OWN_UNKNOWN		FMR_OWNER(0, 2) /* unknown owner */
#define FMR_OWN_METADATA	FMR_OWNER(0, 3) /* metadata */

#define FS_IOC_GETFSMAP		_IOWR('X', 59, struct fsmap_head)

#endif /* _LINUX_FSMAP_H */
