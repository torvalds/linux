/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_REPLICAS_TYPES_H
#define _BCACHEFS_REPLICAS_TYPES_H

struct bch_replicas_cpu {
	unsigned		nr;
	unsigned		entry_size;
	struct bch_replicas_entry *entries;
};

#endif /* _BCACHEFS_REPLICAS_TYPES_H */
