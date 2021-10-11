/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUBVOLUME_TYPES_H
#define _BCACHEFS_SUBVOLUME_TYPES_H

struct snapshot_id_list {
	u32		nr;
	u32		size;
	u32		*d;
};

#endif /* _BCACHEFS_SUBVOLUME_TYPES_H */
