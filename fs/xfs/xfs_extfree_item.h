/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef	__XFS_EXTFREE_ITEM_H__
#define	__XFS_EXTFREE_ITEM_H__

struct xfs_mount;
struct kmem_zone;

typedef struct xfs_extent {
	xfs_dfsbno_t	ext_start;
	xfs_extlen_t	ext_len;
} xfs_extent_t;

/*
 * Since an xfs_extent_t has types (start:64, len: 32)
 * there are different alignments on 32 bit and 64 bit kernels.
 * So we provide the different variants for use by a
 * conversion routine.
 */

typedef struct xfs_extent_32 {
	__uint64_t	ext_start;
	__uint32_t	ext_len;
} __attribute__((packed)) xfs_extent_32_t;

typedef struct xfs_extent_64 {
	__uint64_t	ext_start;
	__uint32_t	ext_len;
	__uint32_t	ext_pad;
} xfs_extent_64_t;

/*
 * This is the structure used to lay out an efi log item in the
 * log.  The efi_extents field is a variable size array whose
 * size is given by efi_nextents.
 */
typedef struct xfs_efi_log_format {
	__uint16_t		efi_type;	/* efi log item type */
	__uint16_t		efi_size;	/* size of this item */
	__uint32_t		efi_nextents;	/* # extents to free */
	__uint64_t		efi_id;		/* efi identifier */
	xfs_extent_t		efi_extents[1];	/* array of extents to free */
} xfs_efi_log_format_t;

typedef struct xfs_efi_log_format_32 {
	__uint16_t		efi_type;	/* efi log item type */
	__uint16_t		efi_size;	/* size of this item */
	__uint32_t		efi_nextents;	/* # extents to free */
	__uint64_t		efi_id;		/* efi identifier */
	xfs_extent_32_t		efi_extents[1];	/* array of extents to free */
} __attribute__((packed)) xfs_efi_log_format_32_t;

typedef struct xfs_efi_log_format_64 {
	__uint16_t		efi_type;	/* efi log item type */
	__uint16_t		efi_size;	/* size of this item */
	__uint32_t		efi_nextents;	/* # extents to free */
	__uint64_t		efi_id;		/* efi identifier */
	xfs_extent_64_t		efi_extents[1];	/* array of extents to free */
} xfs_efi_log_format_64_t;

/*
 * This is the structure used to lay out an efd log item in the
 * log.  The efd_extents array is a variable size array whose
 * size is given by efd_nextents;
 */
typedef struct xfs_efd_log_format {
	__uint16_t		efd_type;	/* efd log item type */
	__uint16_t		efd_size;	/* size of this item */
	__uint32_t		efd_nextents;	/* # of extents freed */
	__uint64_t		efd_efi_id;	/* id of corresponding efi */
	xfs_extent_t		efd_extents[1];	/* array of extents freed */
} xfs_efd_log_format_t;

typedef struct xfs_efd_log_format_32 {
	__uint16_t		efd_type;	/* efd log item type */
	__uint16_t		efd_size;	/* size of this item */
	__uint32_t		efd_nextents;	/* # of extents freed */
	__uint64_t		efd_efi_id;	/* id of corresponding efi */
	xfs_extent_32_t		efd_extents[1];	/* array of extents freed */
} __attribute__((packed)) xfs_efd_log_format_32_t;

typedef struct xfs_efd_log_format_64 {
	__uint16_t		efd_type;	/* efd log item type */
	__uint16_t		efd_size;	/* size of this item */
	__uint32_t		efd_nextents;	/* # of extents freed */
	__uint64_t		efd_efi_id;	/* id of corresponding efi */
	xfs_extent_64_t		efd_extents[1];	/* array of extents freed */
} xfs_efd_log_format_64_t;


#ifdef __KERNEL__

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_EFI_MAX_FAST_EXTENTS	16

/*
 * Define EFI flag bits. Manipulated by set/clear/test_bit operators.
 */
#define	XFS_EFI_RECOVERED	1
#define	XFS_EFI_COMMITTED	2

/*
 * This is the "extent free intention" log item.  It is used
 * to log the fact that some extents need to be free.  It is
 * used in conjunction with the "extent free done" log item
 * described below.
 */
typedef struct xfs_efi_log_item {
	xfs_log_item_t		efi_item;
	atomic_t		efi_next_extent;
	unsigned long		efi_flags;	/* misc flags */
	xfs_efi_log_format_t	efi_format;
} xfs_efi_log_item_t;

/*
 * This is the "extent free done" log item.  It is used to log
 * the fact that some extents earlier mentioned in an efi item
 * have been freed.
 */
typedef struct xfs_efd_log_item {
	xfs_log_item_t		efd_item;
	xfs_efi_log_item_t	*efd_efip;
	uint			efd_next_extent;
	xfs_efd_log_format_t	efd_format;
} xfs_efd_log_item_t;

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_EFD_MAX_FAST_EXTENTS	16

extern struct kmem_zone	*xfs_efi_zone;
extern struct kmem_zone	*xfs_efd_zone;

xfs_efi_log_item_t	*xfs_efi_init(struct xfs_mount *, uint);
xfs_efd_log_item_t	*xfs_efd_init(struct xfs_mount *, xfs_efi_log_item_t *,
				      uint);
int			xfs_efi_copy_format(xfs_log_iovec_t *buf,
					    xfs_efi_log_format_t *dst_efi_fmt);
void			xfs_efi_item_free(xfs_efi_log_item_t *);

#endif	/* __KERNEL__ */

#endif	/* __XFS_EXTFREE_ITEM_H__ */
