/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VHOST_IOTLB_H
#define _LINUX_VHOST_IOTLB_H

#include <linux/interval_tree_generic.h>

struct vhost_iotlb_map {
	struct rb_node rb;
	struct list_head link;
	u64 start;
	u64 last;
	u64 size;
	u64 addr;
#define VHOST_MAP_RO 0x1
#define VHOST_MAP_WO 0x2
#define VHOST_MAP_RW 0x3
	u32 perm;
	u32 flags_padding;
	u64 __subtree_last;
	void *opaque;
};

#define VHOST_IOTLB_FLAG_RETIRE 0x1

struct vhost_iotlb {
	struct rb_root_cached root;
	struct list_head list;
	unsigned int limit;
	unsigned int nmaps;
	unsigned int flags;
};

int vhost_iotlb_add_range_ctx(struct vhost_iotlb *iotlb, u64 start, u64 last,
			      u64 addr, unsigned int perm, void *opaque);
int vhost_iotlb_add_range(struct vhost_iotlb *iotlb, u64 start, u64 last,
			  u64 addr, unsigned int perm);
void vhost_iotlb_del_range(struct vhost_iotlb *iotlb, u64 start, u64 last);

struct vhost_iotlb *vhost_iotlb_alloc(unsigned int limit, unsigned int flags);
void vhost_iotlb_free(struct vhost_iotlb *iotlb);
void vhost_iotlb_reset(struct vhost_iotlb *iotlb);

struct vhost_iotlb_map *
vhost_iotlb_itree_first(struct vhost_iotlb *iotlb, u64 start, u64 last);
struct vhost_iotlb_map *
vhost_iotlb_itree_next(struct vhost_iotlb_map *map, u64 start, u64 last);

void vhost_iotlb_map_free(struct vhost_iotlb *iotlb,
			  struct vhost_iotlb_map *map);
#endif
