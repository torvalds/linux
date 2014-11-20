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

/* kernel only EFI/EFD definitions */

struct xfs_mount;
struct kmem_zone;

/*
 * Max number of extents in fast allocation path.
 */
#define	XFS_EFI_MAX_FAST_EXTENTS	16

/*
 * Define EFI flag bits. Manipulated by set/clear/test_bit operators.
 */
#define	XFS_EFI_RECOVERED	1

/*
 * This is the "extent free intention" log item.  It is used to log the fact
 * that some extents need to be free.  It is used in conjunction with the
 * "extent free done" log item described below.
 *
 * The EFI is reference counted so that it is not freed prior to both the EFI
 * and EFD being committed and unpinned. This ensures that when the last
 * reference goes away the EFI will always be in the AIL as it has been
 * unpinned, regardless of whether the EFD is processed before or after the EFI.
 */
typedef struct xfs_efi_log_item {
	xfs_log_item_t		efi_item;
	atomic_t		efi_refcount;
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

#endif	/* __XFS_EXTFREE_ITEM_H__ */
