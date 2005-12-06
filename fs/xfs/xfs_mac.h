/*
 * Copyright (c) 2001-2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_MAC_H__
#define __XFS_MAC_H__

/*
 * Mandatory Access Control
 *
 * Layout of a composite MAC label:
 * ml_list contains the list of categories (MSEN) followed by the list of
 * divisions (MINT). This is actually a header for the data structure which
 * will have an ml_list with more than one element.
 *
 *      -------------------------------
 *      | ml_msen_type | ml_mint_type |
 *      -------------------------------
 *      | ml_level     | ml_grade     |
 *      -------------------------------
 *      | ml_catcount                 |
 *      -------------------------------
 *      | ml_divcount                 |
 *      -------------------------------
 *      | category 1                  |
 *      | . . .                       |
 *      | category N                  | (where N = ml_catcount)
 *      -------------------------------
 *      | division 1                  |
 *      | . . .                       |
 *      | division M                  | (where M = ml_divcount)
 *      -------------------------------
 */
#define XFS_MAC_MAX_SETS	250
typedef struct xfs_mac_label {
	__uint8_t	ml_msen_type;	/* MSEN label type */
	__uint8_t	ml_mint_type;	/* MINT label type */
	__uint8_t	ml_level;	/* Hierarchical level */
	__uint8_t	ml_grade;	/* Hierarchical grade */
	__uint16_t	ml_catcount;	/* Category count */
	__uint16_t	ml_divcount;	/* Division count */
					/* Category set, then Division set */
	__uint16_t	ml_list[XFS_MAC_MAX_SETS];
} xfs_mac_label_t;

/* MSEN label type names. Choose an upper case ASCII character.  */
#define XFS_MSEN_ADMIN_LABEL	'A'	/* Admin: low<admin != tcsec<high */
#define XFS_MSEN_EQUAL_LABEL	'E'	/* Wildcard - always equal */
#define XFS_MSEN_HIGH_LABEL	'H'	/* System High - always dominates */
#define XFS_MSEN_MLD_HIGH_LABEL	'I'	/* System High, multi-level dir */
#define XFS_MSEN_LOW_LABEL	'L'	/* System Low - always dominated */
#define XFS_MSEN_MLD_LABEL	'M'	/* TCSEC label on a multi-level dir */
#define XFS_MSEN_MLD_LOW_LABEL	'N'	/* System Low, multi-level dir */
#define XFS_MSEN_TCSEC_LABEL	'T'	/* TCSEC label */
#define XFS_MSEN_UNKNOWN_LABEL	'U'	/* unknown label */

/* MINT label type names. Choose a lower case ASCII character.  */
#define XFS_MINT_BIBA_LABEL	'b'	/* Dual of a TCSEC label */
#define XFS_MINT_EQUAL_LABEL	'e'	/* Wildcard - always equal */
#define XFS_MINT_HIGH_LABEL	'h'	/* High Grade - always dominates */
#define XFS_MINT_LOW_LABEL	'l'	/* Low Grade - always dominated */

/* On-disk XFS extended attribute names */
#define SGI_MAC_FILE	"SGI_MAC_FILE"
#define SGI_MAC_FILE_SIZE	(sizeof(SGI_MAC_FILE)-1)


#ifdef __KERNEL__

#ifdef CONFIG_FS_POSIX_MAC

/* NOT YET IMPLEMENTED */

#define MACEXEC		00100
#define MACWRITE	00200
#define MACREAD		00400

struct xfs_inode;
extern int  xfs_mac_iaccess(struct xfs_inode *, mode_t, cred_t *);

#define _MAC_XFS_IACCESS(i,m,c) (xfs_mac_iaccess(i,m,c))
#define _MAC_VACCESS(v,c,m)	(xfs_mac_vaccess(v,c,m))
#define _MAC_EXISTS		xfs_mac_vhaslabel

#else
#define _MAC_XFS_IACCESS(i,m,c)	(0)
#define _MAC_VACCESS(v,c,m)	(0)
#define _MAC_EXISTS		(NULL)
#endif

#endif	/* __KERNEL__ */

#endif	/* __XFS_MAC_H__ */
