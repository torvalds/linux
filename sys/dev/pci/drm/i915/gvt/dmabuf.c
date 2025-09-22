/*
 * Copyright 2017 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Xiaoguang Chen
 *    Tina Zhang <tina.zhang@intel.com>
 */

#include <linux/dma-buf.h>
#include <linux/mdev.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>

#include "gem/i915_gem_dmabuf.h"

#include "i915_drv.h"
#include "gvt.h"

#include "display/skl_universal_plane_regs.h"

#define GEN8_DECODE_PTE(pte) (pte & GENMASK_ULL(63, 12))

static int vgpu_gem_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct intel_vgpu *vgpu;
	struct sg_table *st;
	struct scatterlist *sg;
	int i, j, ret;
	gen8_pte_t __iomem *gtt_entries;
	struct intel_vgpu_fb_info *fb_info;
	unsigned int page_num; /* limited by sg_alloc_table */

	if (overflows_type(obj->base.size >> PAGE_SHIFT, page_num))
		return -E2BIG;

	page_num = obj->base.size >> PAGE_SHIFT;
	fb_info = (struct intel_vgpu_fb_info *)obj->gvt_info;
	if (drm_WARN_ON(&dev_priv->drm, !fb_info))
		return -ENODEV;

	vgpu = fb_info->obj->vgpu;
	if (drm_WARN_ON(&dev_priv->drm, !vgpu))
		return -ENODEV;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (unlikely(!st))
		return -ENOMEM;

	ret = sg_alloc_table(st, page_num, GFP_KERNEL);
	if (ret) {
		kfree(st);
		return ret;
	}
	gtt_entries = (gen8_pte_t __iomem *)to_gt(dev_priv)->ggtt->gsm +
		(fb_info->start >> PAGE_SHIFT);
	for_each_sg(st->sgl, sg, page_num, i) {
		dma_addr_t dma_addr =
			GEN8_DECODE_PTE(readq(&gtt_entries[i]));
		if (intel_gvt_dma_pin_guest_page(vgpu, dma_addr)) {
			ret = -EINVAL;
			goto out;
		}

		sg->offset = 0;
		sg->length = PAGE_SIZE;
		sg_dma_len(sg) = PAGE_SIZE;
		sg_dma_address(sg) = dma_addr;
	}

	__i915_gem_object_set_pages(obj, st);
out:
	if (ret) {
		dma_addr_t dma_addr;

		for_each_sg(st->sgl, sg, i, j) {
			dma_addr = sg_dma_address(sg);
			if (dma_addr)
				intel_gvt_dma_unmap_guest_page(vgpu, dma_addr);
		}
		sg_free_table(st);
		kfree(st);
	}

	return ret;

}

static void vgpu_gem_put_pages(struct drm_i915_gem_object *obj,
		struct sg_table *pages)
{
	struct scatterlist *sg;

	if (obj->base.dma_buf) {
		struct intel_vgpu_fb_info *fb_info = obj->gvt_info;
		struct intel_vgpu_dmabuf_obj *obj = fb_info->obj;
		struct intel_vgpu *vgpu = obj->vgpu;
		int i;

		for_each_sg(pages->sgl, sg, fb_info->size, i)
			intel_gvt_dma_unmap_guest_page(vgpu,
					       sg_dma_address(sg));
	}

	sg_free_table(pages);
	kfree(pages);
}

static void dmabuf_gem_object_free(struct kref *kref)
{
	struct intel_vgpu_dmabuf_obj *obj =
		container_of(kref, struct intel_vgpu_dmabuf_obj, kref);
	struct intel_vgpu *vgpu = obj->vgpu;
	struct list_head *pos;
	struct intel_vgpu_dmabuf_obj *dmabuf_obj;

	if (vgpu && test_bit(INTEL_VGPU_STATUS_ACTIVE, vgpu->status) &&
	    !list_empty(&vgpu->dmabuf_obj_list_head)) {
		list_for_each(pos, &vgpu->dmabuf_obj_list_head) {
			dmabuf_obj = list_entry(pos, struct intel_vgpu_dmabuf_obj, list);
			if (dmabuf_obj == obj) {
				list_del(pos);
				idr_remove(&vgpu->object_idr,
					   dmabuf_obj->dmabuf_id);
				kfree(dmabuf_obj->info);
				kfree(dmabuf_obj);
				break;
			}
		}
	} else {
		/* Free the orphan dmabuf_objs here */
		kfree(obj->info);
		kfree(obj);
	}
}


static inline void dmabuf_obj_get(struct intel_vgpu_dmabuf_obj *obj)
{
	kref_get(&obj->kref);
}

static inline void dmabuf_obj_put(struct intel_vgpu_dmabuf_obj *obj)
{
	kref_put(&obj->kref, dmabuf_gem_object_free);
}

static void vgpu_gem_release(struct drm_i915_gem_object *gem_obj)
{

	struct intel_vgpu_fb_info *fb_info = gem_obj->gvt_info;
	struct intel_vgpu_dmabuf_obj *obj = fb_info->obj;
	struct intel_vgpu *vgpu = obj->vgpu;

	if (vgpu) {
		mutex_lock(&vgpu->dmabuf_lock);
		gem_obj->base.dma_buf = NULL;
		dmabuf_obj_put(obj);
		mutex_unlock(&vgpu->dmabuf_lock);
	} else {
		/* vgpu is NULL, as it has been removed already */
		gem_obj->base.dma_buf = NULL;
		dmabuf_obj_put(obj);
	}
}

static const struct drm_i915_gem_object_ops intel_vgpu_gem_ops = {
	.name = "i915_gem_object_vgpu",
	.flags = I915_GEM_OBJECT_IS_PROXY,
	.get_pages = vgpu_gem_get_pages,
	.put_pages = vgpu_gem_put_pages,
	.release = vgpu_gem_release,
};

static struct drm_i915_gem_object *vgpu_create_gem(struct drm_device *dev,
		struct intel_vgpu_fb_info *info)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct drm_i915_gem_object *obj;

	obj = i915_gem_object_alloc();
	if (obj == NULL)
		return NULL;

	drm_gem_private_object_init(dev, &obj->base,
		roundup(info->size, PAGE_SIZE));
	i915_gem_object_init(obj, &intel_vgpu_gem_ops, &lock_class, 0);
	i915_gem_object_set_readonly(obj);

	obj->read_domains = I915_GEM_DOMAIN_GTT;
	obj->write_domain = 0;
	if (GRAPHICS_VER(dev_priv) >= 9) {
		unsigned int tiling_mode = 0;
		unsigned int stride = 0;

		switch (info->drm_format_mod) {
		case DRM_FORMAT_MOD_LINEAR:
			tiling_mode = I915_TILING_NONE;
			break;
		case I915_FORMAT_MOD_X_TILED:
			tiling_mode = I915_TILING_X;
			stride = info->stride;
			break;
		case I915_FORMAT_MOD_Y_TILED:
		case I915_FORMAT_MOD_Yf_TILED:
			tiling_mode = I915_TILING_Y;
			stride = info->stride;
			break;
		default:
			gvt_dbg_core("invalid drm_format_mod %llx for tiling\n",
				     info->drm_format_mod);
		}
		obj->tiling_and_stride = tiling_mode | stride;
	} else {
		obj->tiling_and_stride = info->drm_format_mod ?
					I915_TILING_X : 0;
	}

	return obj;
}

static bool validate_hotspot(struct intel_vgpu_cursor_plane_format *c)
{
	if (c && c->x_hot <= c->width && c->y_hot <= c->height)
		return true;
	else
		return false;
}

static int vgpu_get_plane_info(struct drm_device *dev,
		struct intel_vgpu *vgpu,
		struct intel_vgpu_fb_info *info,
		int plane_id)
{
	struct intel_vgpu_primary_plane_format p;
	struct intel_vgpu_cursor_plane_format c;
	int ret, tile_height = 1;

	memset(info, 0, sizeof(*info));

	if (plane_id == DRM_PLANE_TYPE_PRIMARY) {
		ret = intel_vgpu_decode_primary_plane(vgpu, &p);
		if (ret)
			return ret;
		info->start = p.base;
		info->start_gpa = p.base_gpa;
		info->width = p.width;
		info->height = p.height;
		info->stride = p.stride;
		info->drm_format = p.drm_format;

		switch (p.tiled) {
		case PLANE_CTL_TILED_LINEAR:
			info->drm_format_mod = DRM_FORMAT_MOD_LINEAR;
			break;
		case PLANE_CTL_TILED_X:
			info->drm_format_mod = I915_FORMAT_MOD_X_TILED;
			tile_height = 8;
			break;
		case PLANE_CTL_TILED_Y:
			info->drm_format_mod = I915_FORMAT_MOD_Y_TILED;
			tile_height = 32;
			break;
		case PLANE_CTL_TILED_YF:
			info->drm_format_mod = I915_FORMAT_MOD_Yf_TILED;
			tile_height = 32;
			break;
		default:
			gvt_vgpu_err("invalid tiling mode: %x\n", p.tiled);
		}
	} else if (plane_id == DRM_PLANE_TYPE_CURSOR) {
		ret = intel_vgpu_decode_cursor_plane(vgpu, &c);
		if (ret)
			return ret;
		info->start = c.base;
		info->start_gpa = c.base_gpa;
		info->width = c.width;
		info->height = c.height;
		info->stride = c.width * (c.bpp / 8);
		info->drm_format = c.drm_format;
		info->drm_format_mod = 0;
		info->x_pos = c.x_pos;
		info->y_pos = c.y_pos;

		if (validate_hotspot(&c)) {
			info->x_hot = c.x_hot;
			info->y_hot = c.y_hot;
		} else {
			info->x_hot = UINT_MAX;
			info->y_hot = UINT_MAX;
		}
	} else {
		gvt_vgpu_err("invalid plane id:%d\n", plane_id);
		return -EINVAL;
	}

	info->size = info->stride * roundup(info->height, tile_height);
	if (info->size == 0) {
		gvt_vgpu_err("fb size is zero\n");
		return -EINVAL;
	}

	if (info->start & (PAGE_SIZE - 1)) {
		gvt_vgpu_err("Not aligned fb address:0x%llx\n", info->start);
		return -EFAULT;
	}

	if (!intel_gvt_ggtt_validate_range(vgpu, info->start, info->size)) {
		gvt_vgpu_err("invalid gma addr\n");
		return -EFAULT;
	}

	return 0;
}

static struct intel_vgpu_dmabuf_obj *
pick_dmabuf_by_info(struct intel_vgpu *vgpu,
		    struct intel_vgpu_fb_info *latest_info)
{
	struct list_head *pos;
	struct intel_vgpu_fb_info *fb_info;
	struct intel_vgpu_dmabuf_obj *dmabuf_obj = NULL;
	struct intel_vgpu_dmabuf_obj *ret = NULL;

	list_for_each(pos, &vgpu->dmabuf_obj_list_head) {
		dmabuf_obj = list_entry(pos, struct intel_vgpu_dmabuf_obj, list);
		if (!dmabuf_obj->info)
			continue;

		fb_info = (struct intel_vgpu_fb_info *)dmabuf_obj->info;
		if ((fb_info->start == latest_info->start) &&
		    (fb_info->start_gpa == latest_info->start_gpa) &&
		    (fb_info->size == latest_info->size) &&
		    (fb_info->drm_format_mod == latest_info->drm_format_mod) &&
		    (fb_info->drm_format == latest_info->drm_format) &&
		    (fb_info->width == latest_info->width) &&
		    (fb_info->height == latest_info->height)) {
			ret = dmabuf_obj;
			break;
		}
	}

	return ret;
}

static struct intel_vgpu_dmabuf_obj *
pick_dmabuf_by_num(struct intel_vgpu *vgpu, u32 id)
{
	struct list_head *pos;
	struct intel_vgpu_dmabuf_obj *dmabuf_obj = NULL;
	struct intel_vgpu_dmabuf_obj *ret = NULL;

	list_for_each(pos, &vgpu->dmabuf_obj_list_head) {
		dmabuf_obj = list_entry(pos, struct intel_vgpu_dmabuf_obj, list);
		if (dmabuf_obj->dmabuf_id == id) {
			ret = dmabuf_obj;
			break;
		}
	}

	return ret;
}

static void update_fb_info(struct vfio_device_gfx_plane_info *gvt_dmabuf,
		      struct intel_vgpu_fb_info *fb_info)
{
	gvt_dmabuf->drm_format = fb_info->drm_format;
	gvt_dmabuf->drm_format_mod = fb_info->drm_format_mod;
	gvt_dmabuf->width = fb_info->width;
	gvt_dmabuf->height = fb_info->height;
	gvt_dmabuf->stride = fb_info->stride;
	gvt_dmabuf->size = fb_info->size;
	gvt_dmabuf->x_pos = fb_info->x_pos;
	gvt_dmabuf->y_pos = fb_info->y_pos;
	gvt_dmabuf->x_hot = fb_info->x_hot;
	gvt_dmabuf->y_hot = fb_info->y_hot;
}

int intel_vgpu_query_plane(struct intel_vgpu *vgpu, void *args)
{
	struct drm_device *dev = &vgpu->gvt->gt->i915->drm;
	struct vfio_device_gfx_plane_info *gfx_plane_info = args;
	struct intel_vgpu_dmabuf_obj *dmabuf_obj;
	struct intel_vgpu_fb_info fb_info;
	int ret = 0;

	if (gfx_plane_info->flags == (VFIO_GFX_PLANE_TYPE_DMABUF |
				       VFIO_GFX_PLANE_TYPE_PROBE))
		return ret;
	else if ((gfx_plane_info->flags & ~VFIO_GFX_PLANE_TYPE_DMABUF) ||
			(!gfx_plane_info->flags))
		return -EINVAL;

	ret = vgpu_get_plane_info(dev, vgpu, &fb_info,
					gfx_plane_info->drm_plane_type);
	if (ret != 0)
		goto out;

	mutex_lock(&vgpu->dmabuf_lock);
	/* If exists, pick up the exposed dmabuf_obj */
	dmabuf_obj = pick_dmabuf_by_info(vgpu, &fb_info);
	if (dmabuf_obj) {
		update_fb_info(gfx_plane_info, &fb_info);
		gfx_plane_info->dmabuf_id = dmabuf_obj->dmabuf_id;

		/* This buffer may be released between query_plane ioctl and
		 * get_dmabuf ioctl. Add the refcount to make sure it won't
		 * be released between the two ioctls.
		 */
		if (!dmabuf_obj->initref) {
			dmabuf_obj->initref = true;
			dmabuf_obj_get(dmabuf_obj);
		}
		ret = 0;
		gvt_dbg_dpy("vgpu%d: re-use dmabuf_obj ref %d, id %d\n",
			    vgpu->id, kref_read(&dmabuf_obj->kref),
			    gfx_plane_info->dmabuf_id);
		mutex_unlock(&vgpu->dmabuf_lock);
		goto out;
	}

	mutex_unlock(&vgpu->dmabuf_lock);

	/* Need to allocate a new one*/
	dmabuf_obj = kmalloc(sizeof(struct intel_vgpu_dmabuf_obj), GFP_KERNEL);
	if (unlikely(!dmabuf_obj)) {
		gvt_vgpu_err("alloc dmabuf_obj failed\n");
		ret = -ENOMEM;
		goto out;
	}

	dmabuf_obj->info = kmalloc(sizeof(struct intel_vgpu_fb_info),
				   GFP_KERNEL);
	if (unlikely(!dmabuf_obj->info)) {
		gvt_vgpu_err("allocate intel vgpu fb info failed\n");
		ret = -ENOMEM;
		goto out_free_dmabuf;
	}
	memcpy(dmabuf_obj->info, &fb_info, sizeof(struct intel_vgpu_fb_info));

	((struct intel_vgpu_fb_info *)dmabuf_obj->info)->obj = dmabuf_obj;

	dmabuf_obj->vgpu = vgpu;

	ret = idr_alloc(&vgpu->object_idr, dmabuf_obj, 1, 0, GFP_NOWAIT);
	if (ret < 0)
		goto out_free_info;
	gfx_plane_info->dmabuf_id = ret;
	dmabuf_obj->dmabuf_id = ret;

	dmabuf_obj->initref = true;

	kref_init(&dmabuf_obj->kref);

	update_fb_info(gfx_plane_info, &fb_info);

	INIT_LIST_HEAD(&dmabuf_obj->list);
	mutex_lock(&vgpu->dmabuf_lock);
	list_add_tail(&dmabuf_obj->list, &vgpu->dmabuf_obj_list_head);
	mutex_unlock(&vgpu->dmabuf_lock);

	gvt_dbg_dpy("vgpu%d: %s new dmabuf_obj ref %d, id %d\n", vgpu->id,
		    __func__, kref_read(&dmabuf_obj->kref), ret);

	return 0;

out_free_info:
	kfree(dmabuf_obj->info);
out_free_dmabuf:
	kfree(dmabuf_obj);
out:
	/* ENODEV means plane isn't ready, which might be a normal case. */
	return (ret == -ENODEV) ? 0 : ret;
}

/* To associate an exposed dmabuf with the dmabuf_obj */
int intel_vgpu_get_dmabuf(struct intel_vgpu *vgpu, unsigned int dmabuf_id)
{
	struct drm_device *dev = &vgpu->gvt->gt->i915->drm;
	struct intel_vgpu_dmabuf_obj *dmabuf_obj;
	struct drm_i915_gem_object *obj;
	struct dma_buf *dmabuf;
	int dmabuf_fd;
	int ret = 0;

	mutex_lock(&vgpu->dmabuf_lock);

	dmabuf_obj = pick_dmabuf_by_num(vgpu, dmabuf_id);
	if (dmabuf_obj == NULL) {
		gvt_vgpu_err("invalid dmabuf id:%d\n", dmabuf_id);
		ret = -EINVAL;
		goto out;
	}

	obj = vgpu_create_gem(dev, dmabuf_obj->info);
	if (obj == NULL) {
		gvt_vgpu_err("create gvt gem obj failed\n");
		ret = -ENOMEM;
		goto out;
	}

	obj->gvt_info = dmabuf_obj->info;

	dmabuf = i915_gem_prime_export(&obj->base, DRM_CLOEXEC | DRM_RDWR);
	if (IS_ERR(dmabuf)) {
		gvt_vgpu_err("export dma-buf failed\n");
		ret = PTR_ERR(dmabuf);
		goto out_free_gem;
	}

	ret = dma_buf_fd(dmabuf, DRM_CLOEXEC | DRM_RDWR);
	if (ret < 0) {
		gvt_vgpu_err("create dma-buf fd failed ret:%d\n", ret);
		goto out_free_dmabuf;
	}
	dmabuf_fd = ret;

	dmabuf_obj_get(dmabuf_obj);

	if (dmabuf_obj->initref) {
		dmabuf_obj->initref = false;
		dmabuf_obj_put(dmabuf_obj);
	}

	mutex_unlock(&vgpu->dmabuf_lock);

	gvt_dbg_dpy("vgpu%d: dmabuf:%d, dmabuf ref %d, fd:%d\n"
		    "        file count: %ld, GEM ref: %d\n",
		    vgpu->id, dmabuf_obj->dmabuf_id,
		    kref_read(&dmabuf_obj->kref),
		    dmabuf_fd,
		    file_count(dmabuf->file),
		    kref_read(&obj->base.refcount));

	i915_gem_object_put(obj);

	return dmabuf_fd;

out_free_dmabuf:
	dma_buf_put(dmabuf);
out_free_gem:
	i915_gem_object_put(obj);
out:
	mutex_unlock(&vgpu->dmabuf_lock);
	return ret;
}

void intel_vgpu_dmabuf_cleanup(struct intel_vgpu *vgpu)
{
	struct list_head *pos, *n;
	struct intel_vgpu_dmabuf_obj *dmabuf_obj;

	mutex_lock(&vgpu->dmabuf_lock);
	list_for_each_safe(pos, n, &vgpu->dmabuf_obj_list_head) {
		dmabuf_obj = list_entry(pos, struct intel_vgpu_dmabuf_obj, list);
		dmabuf_obj->vgpu = NULL;

		idr_remove(&vgpu->object_idr, dmabuf_obj->dmabuf_id);
		list_del(pos);

		/* dmabuf_obj might be freed in dmabuf_obj_put */
		if (dmabuf_obj->initref) {
			dmabuf_obj->initref = false;
			dmabuf_obj_put(dmabuf_obj);
		}

	}
	mutex_unlock(&vgpu->dmabuf_lock);
}
