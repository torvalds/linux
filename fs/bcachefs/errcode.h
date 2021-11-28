/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ERRCODE_H
#define _BCACHEFS_ERRCODE_H

enum {
	/* Bucket allocator: */
	OPEN_BUCKETS_EMPTY =	2048,
	FREELIST_EMPTY,		/* Allocator thread not keeping up */
	INSUFFICIENT_DEVICES,
};

#endif /* _BCACHFES_ERRCODE_H */
