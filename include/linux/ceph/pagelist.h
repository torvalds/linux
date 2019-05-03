/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_CEPH_PAGELIST_H
#define __FS_CEPH_PAGELIST_H

#include <asm/byteorder.h>
#include <linux/refcount.h>
#include <linux/list.h>
#include <linux/types.h>

struct ceph_pagelist {
	struct list_head head;
	void *mapped_tail;
	size_t length;
	size_t room;
	struct list_head free_list;
	size_t num_pages_free;
	refcount_t refcnt;
};

struct ceph_pagelist_cursor {
	struct ceph_pagelist *pl;   /* pagelist, for error checking */
	struct list_head *page_lru; /* page in list */
	size_t room;		    /* room remaining to reset to */
};

struct ceph_pagelist *ceph_pagelist_alloc(gfp_t gfp_flags);

extern void ceph_pagelist_release(struct ceph_pagelist *pl);

extern int ceph_pagelist_append(struct ceph_pagelist *pl, const void *d, size_t l);

extern int ceph_pagelist_reserve(struct ceph_pagelist *pl, size_t space);

extern int ceph_pagelist_free_reserve(struct ceph_pagelist *pl);

extern void ceph_pagelist_set_cursor(struct ceph_pagelist *pl,
				     struct ceph_pagelist_cursor *c);

extern int ceph_pagelist_truncate(struct ceph_pagelist *pl,
				  struct ceph_pagelist_cursor *c);

static inline int ceph_pagelist_encode_64(struct ceph_pagelist *pl, u64 v)
{
	__le64 ev = cpu_to_le64(v);
	return ceph_pagelist_append(pl, &ev, sizeof(ev));
}
static inline int ceph_pagelist_encode_32(struct ceph_pagelist *pl, u32 v)
{
	__le32 ev = cpu_to_le32(v);
	return ceph_pagelist_append(pl, &ev, sizeof(ev));
}
static inline int ceph_pagelist_encode_16(struct ceph_pagelist *pl, u16 v)
{
	__le16 ev = cpu_to_le16(v);
	return ceph_pagelist_append(pl, &ev, sizeof(ev));
}
static inline int ceph_pagelist_encode_8(struct ceph_pagelist *pl, u8 v)
{
	return ceph_pagelist_append(pl, &v, 1);
}
static inline int ceph_pagelist_encode_string(struct ceph_pagelist *pl,
					      char *s, u32 len)
{
	int ret = ceph_pagelist_encode_32(pl, len);
	if (ret)
		return ret;
	if (len)
		return ceph_pagelist_append(pl, s, len);
	return 0;
}

#endif
