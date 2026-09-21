// SPDX-License-Identifier: GPL-2.0-only

#include "fuse_i.h"
#include <linux/rbtree.h>
#include <linux/pagemap.h>

struct fuse_iext {
	struct rb_node rb;
	u64 start;
	u64 end; /* exclusive */
	struct fuse_backing *backing;
	u64 backing_offset;
};

void fuse_ext_map_destroy(struct rb_root *extents)
{
	struct fuse_iext *fie, *tmp;

	rbtree_postorder_for_each_entry_safe(fie, tmp, extents, rb) {
		fuse_backing_put(fie->backing);
		kfree(fie);
	}
}

static int fuse_add_extent(struct fuse_conn *fc, struct rb_root *extents,
			   struct fuse_extent *ext)
{
	struct fuse_iext *new_fie __free(kfree) = kzalloc_obj(*new_fie);
	struct rb_node *parent = NULL, **link = &extents->rb_node;
	loff_t end;
	u64 tmp;

	if (!new_fie)
		return -ENOMEM;

	if (ext->reserved[0] || ext->reserved[1])
		return fuse_EIO("reserved fields set");

	if (!PAGE_ALIGNED(ext->offset) || !PAGE_ALIGNED(ext->length) || !PAGE_ALIGNED(ext->addr))
		return fuse_EIO("not page aligned");

	if (!ext->length)
		return fuse_EIO("zero sized extent");

	if (overflows_type(ext->offset, loff_t) ||
	    check_add_overflow(ext->offset, ext->length, &end) ||
	    check_add_overflow(ext->addr, ext->length, &tmp))
		return fuse_EIO("offset overflow");

	new_fie->start = ext->offset;
	new_fie->end = end;
	new_fie->backing_offset = ext->addr;

	while (*link) {
		struct fuse_iext *fie = rb_entry(*link, typeof(*fie), rb);

		parent = *link;
		if (new_fie->end <= fie->start)
			link = &parent->rb_left;
		else if (new_fie->start >= fie->end)
			link = &parent->rb_right;
		else
			return fuse_EIO("overlap");
	}

	new_fie->backing = fuse_backing_lookup(fc, ext->backing_id);
	if (!new_fie->backing)
		return fuse_EIO("backing not found");

	if (new_fie->backing->type != FUSE_BACKING_DAXDEV) {
		fuse_backing_put(new_fie->backing);
		return fuse_EIO("backing is not dax device");
	}

	rb_link_node(&new_fie->rb, parent, link);
	rb_insert_color(&no_free_ptr(new_fie)->rb, extents);

	return 0;
}

bool fuse_ext_map_is_dax(struct fuse_backing *fb)
{
	struct fuse_iext *fie;

	if (WARN_ON(fb->type != FUSE_BACKING_EXTMAP || RB_EMPTY_ROOT(&fb->extents)))
		return false;

	fie = rb_entry(fb->extents.rb_node, typeof(*fie), rb);
	return fie->backing->type == FUSE_BACKING_DAXDEV;
}

int fuse_ext_map_populate(struct fuse_conn *fc, struct fuse_notify_backing_map_out *arg,
			  struct fuse_extent *ext)
{
	struct fuse_backing *fb;
	unsigned int i;
	int err;

	if (!arg->num_extents)
		return fuse_EIO("no extents");

	if (arg->flags & FUSE_BACKING_MAP_CREATE) {
		fb = kzalloc_obj(*fb);
		if (!fb)
			return -ENOMEM;

		refcount_set(&fb->count, 1);
		fb->type = FUSE_BACKING_EXTMAP;
		fb->backing_id = arg->backing_id;
	} else {
		/* Adding extents to an existing extmap backing is not yet supported */
		return -EINVAL;
	}

	for (i = 0; i < arg->num_extents; i++) {
		err = fuse_add_extent(fc, &fb->extents, &ext[i]);
		if (err)
			goto err_put;
	}

	err = 0;
	if (arg->flags & FUSE_BACKING_MAP_CREATE) {
		err = fuse_backing_add_64(fc, fb);
		if (!err)
			return 0;
	}

err_put:
	fuse_backing_put(fb);
	return err;
}
