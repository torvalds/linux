/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUBVOLUME_TYPES_H
#define _BCACHEFS_SUBVOLUME_TYPES_H

typedef struct {
	/* we can't have padding in this struct: */
	u64		subvol;
	u64		inum;
} subvol_inum;

#endif /* _BCACHEFS_SUBVOLUME_TYPES_H */
