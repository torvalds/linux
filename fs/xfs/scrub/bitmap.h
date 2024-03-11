// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_BITMAP_H__
#define __XFS_SCRUB_BITMAP_H__

/* u64 bitmap */

struct xbitmap64 {
	struct rb_root_cached	xb_root;
};

void xbitmap64_init(struct xbitmap64 *bitmap);
void xbitmap64_destroy(struct xbitmap64 *bitmap);

int xbitmap64_clear(struct xbitmap64 *bitmap, uint64_t start, uint64_t len);
int xbitmap64_set(struct xbitmap64 *bitmap, uint64_t start, uint64_t len);
int xbitmap64_disunion(struct xbitmap64 *bitmap, struct xbitmap64 *sub);
uint64_t xbitmap64_hweight(struct xbitmap64 *bitmap);

/*
 * Return codes for the bitmap iterator functions are 0 to continue iterating,
 * and non-zero to stop iterating.  Any non-zero value will be passed up to the
 * iteration caller.  The special value -ECANCELED can be used to stop
 * iteration, because neither bitmap iterator ever generates that error code on
 * its own.  Callers must not modify the bitmap while walking it.
 */
typedef int (*xbitmap64_walk_fn)(uint64_t start, uint64_t len, void *priv);
int xbitmap64_walk(struct xbitmap64 *bitmap, xbitmap64_walk_fn fn,
		void *priv);

bool xbitmap64_empty(struct xbitmap64 *bitmap);
bool xbitmap64_test(struct xbitmap64 *bitmap, uint64_t start, uint64_t *len);

/* u32 bitmap */

struct xbitmap32 {
	struct rb_root_cached	xb_root;
};

void xbitmap32_init(struct xbitmap32 *bitmap);
void xbitmap32_destroy(struct xbitmap32 *bitmap);

int xbitmap32_clear(struct xbitmap32 *bitmap, uint32_t start, uint32_t len);
int xbitmap32_set(struct xbitmap32 *bitmap, uint32_t start, uint32_t len);
int xbitmap32_disunion(struct xbitmap32 *bitmap, struct xbitmap32 *sub);
uint32_t xbitmap32_hweight(struct xbitmap32 *bitmap);

/*
 * Return codes for the bitmap iterator functions are 0 to continue iterating,
 * and non-zero to stop iterating.  Any non-zero value will be passed up to the
 * iteration caller.  The special value -ECANCELED can be used to stop
 * iteration, because neither bitmap iterator ever generates that error code on
 * its own.  Callers must not modify the bitmap while walking it.
 */
typedef int (*xbitmap32_walk_fn)(uint32_t start, uint32_t len, void *priv);
int xbitmap32_walk(struct xbitmap32 *bitmap, xbitmap32_walk_fn fn,
		void *priv);

bool xbitmap32_empty(struct xbitmap32 *bitmap);
bool xbitmap32_test(struct xbitmap32 *bitmap, uint32_t start, uint32_t *len);

#endif	/* __XFS_SCRUB_BITMAP_H__ */
