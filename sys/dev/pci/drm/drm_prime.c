/*
 * Copyright © 2012 Red Hat
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *      Dave Airlie <airlied@redhat.com>
 *      Rob Clark <rob.clark@linaro.org>
 *
 */

#include <linux/export.h>
#include <linux/dma-buf.h>
#include <linux/rbtree.h>
#include <linux/module.h>

#include <drm/drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_prime.h>

#include "drm_internal.h"

MODULE_IMPORT_NS(DMA_BUF);

/**
 * DOC: overview and lifetime rules
 *
 * Similar to GEM global names, PRIME file descriptors are also used to share
 * buffer objects across processes. They offer additional security: as file
 * descriptors must be explicitly sent over UNIX domain sockets to be shared
 * between applications, they can't be guessed like the globally unique GEM
 * names.
 *
 * Drivers that support the PRIME API implement the drm_gem_object_funcs.export
 * and &drm_driver.gem_prime_import hooks. &dma_buf_ops implementations for
 * drivers are all individually exported for drivers which need to overwrite
 * or reimplement some of them.
 *
 * Reference Counting for GEM Drivers
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * On the export the &dma_buf holds a reference to the exported buffer object,
 * usually a &drm_gem_object. It takes this reference in the PRIME_HANDLE_TO_FD
 * IOCTL, when it first calls &drm_gem_object_funcs.export
 * and stores the exporting GEM object in the &dma_buf.priv field. This
 * reference needs to be released when the final reference to the &dma_buf
 * itself is dropped and its &dma_buf_ops.release function is called.  For
 * GEM-based drivers, the &dma_buf should be exported using
 * drm_gem_dmabuf_export() and then released by drm_gem_dmabuf_release().
 *
 * Thus the chain of references always flows in one direction, avoiding loops:
 * importing GEM object -> dma-buf -> exported GEM bo. A further complication
 * are the lookup caches for import and export. These are required to guarantee
 * that any given object will always have only one unique userspace handle. This
 * is required to allow userspace to detect duplicated imports, since some GEM
 * drivers do fail command submissions if a given buffer object is listed more
 * than once. These import and export caches in &drm_prime_file_private only
 * retain a weak reference, which is cleaned up when the corresponding object is
 * released.
 *
 * Self-importing: If userspace is using PRIME as a replacement for flink then
 * it will get a fd->handle request for a GEM object that it created.  Drivers
 * should detect this situation and return back the underlying object from the
 * dma-buf private. For GEM based drivers this is handled in
 * drm_gem_prime_import() already.
 */

struct drm_prime_member {
	struct dma_buf *dma_buf;
	uint32_t handle;

	struct rb_node dmabuf_rb;
	struct rb_node handle_rb;
};

static int drm_prime_add_buf_handle(struct drm_prime_file_private *prime_fpriv,
				    struct dma_buf *dma_buf, uint32_t handle)
{
	struct drm_prime_member *member;
	struct rb_node **p, *rb;

	member = kmalloc(sizeof(*member), GFP_KERNEL);
	if (!member)
		return -ENOMEM;

	get_dma_buf(dma_buf);
	member->dma_buf = dma_buf;
	member->handle = handle;

	rb = NULL;
	p = &prime_fpriv->dmabufs.rb_node;
	while (*p) {
		struct drm_prime_member *pos;

		rb = *p;
		pos = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (dma_buf > pos->dma_buf)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&member->dmabuf_rb, rb, p);
	rb_insert_color(&member->dmabuf_rb, &prime_fpriv->dmabufs);

	rb = NULL;
	p = &prime_fpriv->handles.rb_node;
	while (*p) {
		struct drm_prime_member *pos;

		rb = *p;
		pos = rb_entry(rb, struct drm_prime_member, handle_rb);
		if (handle > pos->handle)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&member->handle_rb, rb, p);
	rb_insert_color(&member->handle_rb, &prime_fpriv->handles);

	return 0;
}

static struct dma_buf *drm_prime_lookup_buf_by_handle(struct drm_prime_file_private *prime_fpriv,
						      uint32_t handle)
{
	struct rb_node *rb;

	rb = prime_fpriv->handles.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, handle_rb);
		if (member->handle == handle)
			return member->dma_buf;
		else if (member->handle < handle)
			rb = rb->rb_right;
		else
			rb = rb->rb_left;
	}

	return NULL;
}

static int drm_prime_lookup_buf_handle(struct drm_prime_file_private *prime_fpriv,
				       struct dma_buf *dma_buf,
				       uint32_t *handle)
{
	struct rb_node *rb;

	rb = prime_fpriv->dmabufs.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (member->dma_buf == dma_buf) {
			*handle = member->handle;
			return 0;
		} else if (member->dma_buf < dma_buf) {
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}

	return -ENOENT;
}

void drm_prime_remove_buf_handle(struct drm_prime_file_private *prime_fpriv,
				 uint32_t handle)
{
	struct rb_node *rb;

	mutex_lock(&prime_fpriv->lock);

	rb = prime_fpriv->handles.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, handle_rb);
		if (member->handle == handle) {
			rb_erase(&member->handle_rb, &prime_fpriv->handles);
			rb_erase(&member->dmabuf_rb, &prime_fpriv->dmabufs);

			dma_buf_put(member->dma_buf);
			kfree(member);
			break;
		} else if (member->handle < handle) {
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}

	mutex_unlock(&prime_fpriv->lock);
}

void drm_prime_init_file_private(struct drm_prime_file_private *prime_fpriv)
{
	rw_init(&prime_fpriv->lock, "primlk");
	prime_fpriv->dmabufs = RB_ROOT;
	prime_fpriv->handles = RB_ROOT;
}

void drm_prime_destroy_file_private(struct drm_prime_file_private *prime_fpriv)
{
	/* by now drm_gem_release should've made sure the list is empty */
	WARN_ON(!RB_EMPTY_ROOT(&prime_fpriv->dmabufs));
}

/**
 * drm_gem_dmabuf_export - &dma_buf export implementation for GEM
 * @dev: parent device for the exported dmabuf
 * @exp_info: the export information used by dma_buf_export()
 *
 * This wraps dma_buf_export() for use by generic GEM drivers that are using
 * drm_gem_dmabuf_release(). In addition to calling dma_buf_export(), we take
 * a reference to the &drm_device and the exported &drm_gem_object (stored in
 * &dma_buf_export_info.priv) which is released by drm_gem_dmabuf_release().
 *
 * Returns the new dmabuf.
 */
struct dma_buf *drm_gem_dmabuf_export(struct drm_device *dev,
				      struct dma_buf_export_info *exp_info)
{
	struct drm_gem_object *obj = exp_info->priv;
	struct dma_buf *dma_buf;

	dma_buf = dma_buf_export(exp_info);
	if (IS_ERR(dma_buf))
		return dma_buf;

	drm_dev_get(dev);
	drm_gem_object_get(obj);
#ifdef __linux__
	dma_buf->file->f_mapping = obj->dev->anon_inode->i_mapping;
#endif

	return dma_buf;
}
EXPORT_SYMBOL(drm_gem_dmabuf_export);

/**
 * drm_gem_dmabuf_release - &dma_buf release implementation for GEM
 * @dma_buf: buffer to be released
 *
 * Generic release function for dma_bufs exported as PRIME buffers. GEM drivers
 * must use this in their &dma_buf_ops structure as the release callback.
 * drm_gem_dmabuf_release() should be used in conjunction with
 * drm_gem_dmabuf_export().
 */
void drm_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj = dma_buf->priv;
	struct drm_device *dev = obj->dev;

	/* drop the reference on the export fd holds */
	drm_gem_object_put(obj);

	drm_dev_put(dev);
}
EXPORT_SYMBOL(drm_gem_dmabuf_release);

/**
 * drm_gem_prime_fd_to_handle - PRIME import function for GEM drivers
 * @dev: drm_device to import into
 * @file_priv: drm file-private structure
 * @prime_fd: fd id of the dma-buf which should be imported
 * @handle: pointer to storage for the handle of the imported buffer object
 *
 * This is the PRIME import function which must be used mandatorily by GEM
 * drivers to ensure correct lifetime management of the underlying GEM object.
 * The actual importing of GEM object from the dma-buf is done through the
 * &drm_driver.gem_prime_import driver callback.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_gem_prime_fd_to_handle(struct drm_device *dev,
			       struct drm_file *file_priv, int prime_fd,
			       uint32_t *handle)
{
	struct dma_buf *dma_buf;
	struct drm_gem_object *obj;
	int ret;

	dma_buf = dma_buf_get(prime_fd);
	if (IS_ERR(dma_buf))
		return PTR_ERR(dma_buf);

	mutex_lock(&file_priv->prime.lock);

	ret = drm_prime_lookup_buf_handle(&file_priv->prime,
			dma_buf, handle);
	if (ret == 0)
		goto out_put;

	/* never seen this one, need to import */
	mutex_lock(&dev->object_name_lock);
	if (dev->driver->gem_prime_import)
		obj = dev->driver->gem_prime_import(dev, dma_buf);
	else
		obj = drm_gem_prime_import(dev, dma_buf);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto out_unlock;
	}

	if (obj->dma_buf) {
		WARN_ON(obj->dma_buf != dma_buf);
	} else {
		obj->dma_buf = dma_buf;
		get_dma_buf(dma_buf);
	}

	/* _handle_create_tail unconditionally unlocks dev->object_name_lock. */
	ret = drm_gem_handle_create_tail(file_priv, obj, handle);
	drm_gem_object_put(obj);
	if (ret)
		goto out_put;

	ret = drm_prime_add_buf_handle(&file_priv->prime,
			dma_buf, *handle);
	mutex_unlock(&file_priv->prime.lock);
	if (ret)
		goto fail;

	dma_buf_put(dma_buf);

	return 0;

fail:
	/* hmm, if driver attached, we are relying on the free-object path
	 * to detach.. which seems ok..
	 */
	drm_gem_handle_delete(file_priv, *handle);
	dma_buf_put(dma_buf);
	return ret;

out_unlock:
	mutex_unlock(&dev->object_name_lock);
out_put:
	mutex_unlock(&file_priv->prime.lock);
	dma_buf_put(dma_buf);
	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_fd_to_handle);

int drm_prime_fd_to_handle_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	if (dev->driver->prime_fd_to_handle) {
		return dev->driver->prime_fd_to_handle(dev, file_priv, args->fd,
						       &args->handle);
	}

	return drm_gem_prime_fd_to_handle(dev, file_priv, args->fd, &args->handle);
}

static struct dma_buf *export_and_register_object(struct drm_device *dev,
						  struct drm_gem_object *obj,
						  uint32_t flags)
{
	struct dma_buf *dmabuf;

	/* prevent races with concurrent gem_close. */
	if (obj->handle_count == 0) {
		dmabuf = ERR_PTR(-ENOENT);
		return dmabuf;
	}

	if (obj->funcs && obj->funcs->export)
		dmabuf = obj->funcs->export(obj, flags);
	else
		dmabuf = drm_gem_prime_export(obj, flags);
	if (IS_ERR(dmabuf)) {
		/* normally the created dma-buf takes ownership of the ref,
		 * but if that fails then drop the ref
		 */
		return dmabuf;
	}

	/*
	 * Note that callers do not need to clean up the export cache
	 * since the check for obj->handle_count guarantees that someone
	 * will clean it up.
	 */
	obj->dma_buf = dmabuf;
	get_dma_buf(obj->dma_buf);

	return dmabuf;
}

/**
 * drm_gem_prime_handle_to_dmabuf - PRIME export function for GEM drivers
 * @dev: dev to export the buffer from
 * @file_priv: drm file-private structure
 * @handle: buffer handle to export
 * @flags: flags like DRM_CLOEXEC
 *
 * This is the PRIME export function which must be used mandatorily by GEM
 * drivers to ensure correct lifetime management of the underlying GEM object.
 * The actual exporting from GEM object to a dma-buf is done through the
 * &drm_gem_object_funcs.export callback.
 *
 * Unlike drm_gem_prime_handle_to_fd(), it returns the struct dma_buf it
 * has created, without attaching it to any file descriptors.  The difference
 * between those two is similar to that between anon_inode_getfile() and
 * anon_inode_getfd(); insertion into descriptor table is something you
 * can not revert if any cleanup is needed, so the descriptor-returning
 * variants should only be used when you are past the last failure exit
 * and the only thing left is passing the new file descriptor to userland.
 * When all you need is the object itself or when you need to do something
 * else that might fail, use that one instead.
 */
struct dma_buf *drm_gem_prime_handle_to_dmabuf(struct drm_device *dev,
			       struct drm_file *file_priv, uint32_t handle,
			       uint32_t flags)
{
	struct drm_gem_object *obj;
	int ret = 0;
	struct dma_buf *dmabuf;

	mutex_lock(&file_priv->prime.lock);
	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj)  {
		dmabuf = ERR_PTR(-ENOENT);
		goto out_unlock;
	}

	dmabuf = drm_prime_lookup_buf_by_handle(&file_priv->prime, handle);
	if (dmabuf) {
		get_dma_buf(dmabuf);
		goto out;
	}

	mutex_lock(&dev->object_name_lock);
#ifdef notyet
	/* re-export the original imported object */
	if (obj->import_attach) {
		dmabuf = obj->import_attach->dmabuf;
		get_dma_buf(dmabuf);
		goto out_have_obj;
	}
#endif

	if (obj->dma_buf) {
		get_dma_buf(obj->dma_buf);
		dmabuf = obj->dma_buf;
		goto out_have_obj;
	}

	dmabuf = export_and_register_object(dev, obj, flags);
	if (IS_ERR(dmabuf)) {
		/* normally the created dma-buf takes ownership of the ref,
		 * but if that fails then drop the ref
		 */
		mutex_unlock(&dev->object_name_lock);
		goto out;
	}

out_have_obj:
	/*
	 * If we've exported this buffer then cheat and add it to the import list
	 * so we get the correct handle back. We must do this under the
	 * protection of dev->object_name_lock to ensure that a racing gem close
	 * ioctl doesn't miss to remove this buffer handle from the cache.
	 */
	ret = drm_prime_add_buf_handle(&file_priv->prime,
				       dmabuf, handle);
	mutex_unlock(&dev->object_name_lock);
	if (ret) {
		dma_buf_put(dmabuf);
		dmabuf = ERR_PTR(ret);
	}
out:
	drm_gem_object_put(obj);
out_unlock:
	mutex_unlock(&file_priv->prime.lock);
	return dmabuf;
}
EXPORT_SYMBOL(drm_gem_prime_handle_to_dmabuf);

/**
 * drm_gem_prime_handle_to_fd - PRIME export function for GEM drivers
 * @dev: dev to export the buffer from
 * @file_priv: drm file-private structure
 * @handle: buffer handle to export
 * @flags: flags like DRM_CLOEXEC
 * @prime_fd: pointer to storage for the fd id of the create dma-buf
 *
 * This is the PRIME export function which must be used mandatorily by GEM
 * drivers to ensure correct lifetime management of the underlying GEM object.
 * The actual exporting from GEM object to a dma-buf is done through the
 * &drm_gem_object_funcs.export callback.
 */
int drm_gem_prime_handle_to_fd(struct drm_device *dev,
			       struct drm_file *file_priv, uint32_t handle,
			       uint32_t flags,
			       int *prime_fd)
{
	struct dma_buf *dmabuf;
	int fd = get_unused_fd_flags(flags);

	if (fd < 0)
		return fd;

	dmabuf = drm_gem_prime_handle_to_dmabuf(dev, file_priv, handle, flags);
	if (IS_ERR(dmabuf)) {
		put_unused_fd(fd);
		return PTR_ERR(dmabuf);
	}

	fd_install(fd, dmabuf->file);
	*prime_fd = fd;
	return 0;
}
EXPORT_SYMBOL(drm_gem_prime_handle_to_fd);

int drm_prime_handle_to_fd_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	/* check flags are valid */
	if (args->flags & ~(DRM_CLOEXEC | DRM_RDWR))
		return -EINVAL;

	if (dev->driver->prime_handle_to_fd) {
		return dev->driver->prime_handle_to_fd(dev, file_priv,
						       args->handle, args->flags,
						       &args->fd);
	}
	return drm_gem_prime_handle_to_fd(dev, file_priv, args->handle,
					  args->flags, &args->fd);
}

/**
 * DOC: PRIME Helpers
 *
 * Drivers can implement &drm_gem_object_funcs.export and
 * &drm_driver.gem_prime_import in terms of simpler APIs by using the helper
 * functions drm_gem_prime_export() and drm_gem_prime_import(). These functions
 * implement dma-buf support in terms of some lower-level helpers, which are
 * again exported for drivers to use individually:
 *
 * Exporting buffers
 * ~~~~~~~~~~~~~~~~~
 *
 * Optional pinning of buffers is handled at dma-buf attach and detach time in
 * drm_gem_map_attach() and drm_gem_map_detach(). Backing storage itself is
 * handled by drm_gem_map_dma_buf() and drm_gem_unmap_dma_buf(), which relies on
 * &drm_gem_object_funcs.get_sg_table. If &drm_gem_object_funcs.get_sg_table is
 * unimplemented, exports into another device are rejected.
 *
 * For kernel-internal access there's drm_gem_dmabuf_vmap() and
 * drm_gem_dmabuf_vunmap(). Userspace mmap support is provided by
 * drm_gem_dmabuf_mmap().
 *
 * Note that these export helpers can only be used if the underlying backing
 * storage is fully coherent and either permanently pinned, or it is safe to pin
 * it indefinitely.
 *
 * FIXME: The underlying helper functions are named rather inconsistently.
 *
 * Importing buffers
 * ~~~~~~~~~~~~~~~~~
 *
 * Importing dma-bufs using drm_gem_prime_import() relies on
 * &drm_driver.gem_prime_import_sg_table.
 *
 * Note that similarly to the export helpers this permanently pins the
 * underlying backing storage. Which is ok for scanout, but is not the best
 * option for sharing lots of buffers for rendering.
 */

/**
 * drm_gem_map_attach - dma_buf attach implementation for GEM
 * @dma_buf: buffer to attach device to
 * @attach: buffer attachment data
 *
 * Calls &drm_gem_object_funcs.pin for device specific handling. This can be
 * used as the &dma_buf_ops.attach callback. Must be used together with
 * drm_gem_map_detach().
 *
 * Returns 0 on success, negative error code on failure.
 */
int drm_gem_map_attach(struct dma_buf *dma_buf,
		       struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dma_buf->priv;

	/*
	 * drm_gem_map_dma_buf() requires obj->get_sg_table(), but drivers
	 * that implement their own ->map_dma_buf() do not.
	 */
#ifdef notyet
	if (dma_buf->ops->map_dma_buf == drm_gem_map_dma_buf &&
	    !obj->funcs->get_sg_table)
#else
	if (!obj->funcs->get_sg_table)
#endif
		return -ENOSYS;

	return drm_gem_pin(obj);
}
EXPORT_SYMBOL(drm_gem_map_attach);

/**
 * drm_gem_map_detach - dma_buf detach implementation for GEM
 * @dma_buf: buffer to detach from
 * @attach: attachment to be detached
 *
 * Calls &drm_gem_object_funcs.pin for device specific handling.  Cleans up
 * &dma_buf_attachment from drm_gem_map_attach(). This can be used as the
 * &dma_buf_ops.detach callback.
 */
void drm_gem_map_detach(struct dma_buf *dma_buf,
			struct dma_buf_attachment *attach)
{
	struct drm_gem_object *obj = dma_buf->priv;

	drm_gem_unpin(obj);
}
EXPORT_SYMBOL(drm_gem_map_detach);

#ifdef notyet

/**
 * drm_gem_map_dma_buf - map_dma_buf implementation for GEM
 * @attach: attachment whose scatterlist is to be returned
 * @dir: direction of DMA transfer
 *
 * Calls &drm_gem_object_funcs.get_sg_table and then maps the scatterlist. This
 * can be used as the &dma_buf_ops.map_dma_buf callback. Should be used together
 * with drm_gem_unmap_dma_buf().
 *
 * Returns:sg_table containing the scatterlist to be returned; returns ERR_PTR
 * on error. May return -EINTR if it is interrupted by a signal.
 */
struct sg_table *drm_gem_map_dma_buf(struct dma_buf_attachment *attach,
				     enum dma_data_direction dir)
{
	struct drm_gem_object *obj = attach->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	if (WARN_ON(dir == DMA_NONE))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(!obj->funcs->get_sg_table))
		return ERR_PTR(-ENOSYS);

	sgt = obj->funcs->get_sg_table(obj);
	if (IS_ERR(sgt))
		return sgt;

	ret = dma_map_sgtable(attach->dev, sgt, dir,
			      DMA_ATTR_SKIP_CPU_SYNC);
	if (ret) {
		sg_free_table(sgt);
		kfree(sgt);
		sgt = ERR_PTR(ret);
	}

	return sgt;
}
EXPORT_SYMBOL(drm_gem_map_dma_buf);

/**
 * drm_gem_unmap_dma_buf - unmap_dma_buf implementation for GEM
 * @attach: attachment to unmap buffer from
 * @sgt: scatterlist info of the buffer to unmap
 * @dir: direction of DMA transfer
 *
 * This can be used as the &dma_buf_ops.unmap_dma_buf callback.
 */
void drm_gem_unmap_dma_buf(struct dma_buf_attachment *attach,
			   struct sg_table *sgt,
			   enum dma_data_direction dir)
{
	if (!sgt)
		return;

	dma_unmap_sgtable(attach->dev, sgt, dir, DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}
EXPORT_SYMBOL(drm_gem_unmap_dma_buf);

#endif /* notyet */

/**
 * drm_gem_dmabuf_vmap - dma_buf vmap implementation for GEM
 * @dma_buf: buffer to be mapped
 * @map: the virtual address of the buffer
 *
 * Sets up a kernel virtual mapping. This can be used as the &dma_buf_ops.vmap
 * callback. Calls into &drm_gem_object_funcs.vmap for device specific handling.
 * The kernel virtual address is returned in map.
 *
 * Returns 0 on success or a negative errno code otherwise.
 */
int drm_gem_dmabuf_vmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return drm_gem_vmap(obj, map);
}
EXPORT_SYMBOL(drm_gem_dmabuf_vmap);

/**
 * drm_gem_dmabuf_vunmap - dma_buf vunmap implementation for GEM
 * @dma_buf: buffer to be unmapped
 * @map: the virtual address of the buffer
 *
 * Releases a kernel virtual mapping. This can be used as the
 * &dma_buf_ops.vunmap callback. Calls into &drm_gem_object_funcs.vunmap for device specific handling.
 */
void drm_gem_dmabuf_vunmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct drm_gem_object *obj = dma_buf->priv;

	drm_gem_vunmap(obj, map);
}
EXPORT_SYMBOL(drm_gem_dmabuf_vunmap);

#ifdef __linux__
/**
 * drm_gem_prime_mmap - PRIME mmap function for GEM drivers
 * @obj: GEM object
 * @vma: Virtual address range
 *
 * This function sets up a userspace mapping for PRIME exported buffers using
 * the same codepath that is used for regular GEM buffer mapping on the DRM fd.
 * The fake GEM offset is added to vma->vm_pgoff and &drm_driver->fops->mmap is
 * called to set up the mapping.
 */
int drm_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct drm_file *priv;
	struct file *fil;
	int ret;

	/* Add the fake offset */
	vma->vm_pgoff += drm_vma_node_start(&obj->vma_node);

	if (obj->funcs && obj->funcs->mmap) {
		vma->vm_ops = obj->funcs->vm_ops;

		drm_gem_object_get(obj);
		ret = obj->funcs->mmap(obj, vma);
		if (ret) {
			drm_gem_object_put(obj);
			return ret;
		}
		vma->vm_private_data = obj;
		return 0;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	fil = kzalloc(sizeof(*fil), GFP_KERNEL);
	if (!priv || !fil) {
		ret = -ENOMEM;
		goto out;
	}

	/* Used by drm_gem_mmap() to lookup the GEM object */
	priv->minor = obj->dev->primary;
	fil->private_data = priv;

	ret = drm_vma_node_allow(&obj->vma_node, priv);
	if (ret)
		goto out;

	ret = obj->dev->driver->fops->mmap(fil, vma);

	drm_vma_node_revoke(&obj->vma_node, priv);
out:
	kfree(priv);
	kfree(fil);

	return ret;
}
EXPORT_SYMBOL(drm_gem_prime_mmap);
#else
struct uvm_object *
drm_gem_prime_mmap(struct file *filp, vm_prot_t accessprot, voff_t off,
    vsize_t size)
{
	STUB();
	return NULL;
}
#endif

#ifdef notyet

/**
 * drm_gem_dmabuf_mmap - dma_buf mmap implementation for GEM
 * @dma_buf: buffer to be mapped
 * @vma: virtual address range
 *
 * Provides memory mapping for the buffer. This can be used as the
 * &dma_buf_ops.mmap callback. It just forwards to drm_gem_prime_mmap().
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_gem_dmabuf_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = dma_buf->priv;

	return drm_gem_prime_mmap(obj, vma);
}
EXPORT_SYMBOL(drm_gem_dmabuf_mmap);

#endif /* notyet */

static const struct dma_buf_ops drm_gem_prime_dmabuf_ops =  {
#ifdef notyet
	.cache_sgt_mapping = true,
	.attach = drm_gem_map_attach,
	.detach = drm_gem_map_detach,
	.map_dma_buf = drm_gem_map_dma_buf,
	.unmap_dma_buf = drm_gem_unmap_dma_buf,
#endif
	.release = drm_gem_dmabuf_release,
#ifdef notyet
	.mmap = drm_gem_dmabuf_mmap,
	.vmap = drm_gem_dmabuf_vmap,
	.vunmap = drm_gem_dmabuf_vunmap,
#endif
};

/**
 * drm_prime_pages_to_sg - converts a page array into an sg list
 * @dev: DRM device
 * @pages: pointer to the array of page pointers to convert
 * @nr_pages: length of the page vector
 *
 * This helper creates an sg table object from a set of pages
 * the driver is responsible for mapping the pages into the
 * importers address space for use with dma_buf itself.
 *
 * This is useful for implementing &drm_gem_object_funcs.get_sg_table.
 */
struct sg_table *drm_prime_pages_to_sg(struct drm_device *dev,
				       struct vm_page **pages, unsigned int nr_pages)
{
	STUB();
	return NULL;
#ifdef notyet
	struct sg_table *sg;
	size_t max_segment = 0;
	int err;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg)
		return ERR_PTR(-ENOMEM);

	if (dev)
		max_segment = dma_max_mapping_size(dev->dev);
	if (max_segment == 0)
		max_segment = UINT_MAX;
	err = sg_alloc_table_from_pages_segment(sg, pages, nr_pages, 0,
						(unsigned long)nr_pages << PAGE_SHIFT,
						max_segment, GFP_KERNEL);
	if (err) {
		kfree(sg);
		sg = ERR_PTR(err);
	}
	return sg;
#endif
}
EXPORT_SYMBOL(drm_prime_pages_to_sg);

/**
 * drm_prime_get_contiguous_size - returns the contiguous size of the buffer
 * @sgt: sg_table describing the buffer to check
 *
 * This helper calculates the contiguous size in the DMA address space
 * of the buffer described by the provided sg_table.
 *
 * This is useful for implementing
 * &drm_gem_object_funcs.gem_prime_import_sg_table.
 */
unsigned long drm_prime_get_contiguous_size(struct sg_table *sgt)
{
	STUB();
	return 0;
#ifdef notyet
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	struct scatterlist *sg;
	unsigned long size = 0;
	int i;

	for_each_sgtable_dma_sg(sgt, sg, i) {
		unsigned int len = sg_dma_len(sg);

		if (!len)
			break;
		if (sg_dma_address(sg) != expected)
			break;
		expected += len;
		size += len;
	}
	return size;
#endif
}
EXPORT_SYMBOL(drm_prime_get_contiguous_size);

/**
 * drm_gem_prime_export - helper library implementation of the export callback
 * @obj: GEM object to export
 * @flags: flags like DRM_CLOEXEC and DRM_RDWR
 *
 * This is the implementation of the &drm_gem_object_funcs.export functions for GEM drivers
 * using the PRIME helpers. It is used as the default in
 * drm_gem_prime_handle_to_fd().
 */
struct dma_buf *drm_gem_prime_export(struct drm_gem_object *obj,
				     int flags)
{
	struct drm_device *dev = obj->dev;
	struct dma_buf_export_info exp_info = {
#ifdef __linux__
		.exp_name = KBUILD_MODNAME, /* white lie for debug */
		.owner = dev->driver->fops->owner,
#endif
		.ops = &drm_gem_prime_dmabuf_ops,
		.size = obj->size,
		.flags = flags,
		.priv = obj,
		.resv = obj->resv,
	};

	return drm_gem_dmabuf_export(dev, &exp_info);
}
EXPORT_SYMBOL(drm_gem_prime_export);

/**
 * drm_gem_prime_import_dev - core implementation of the import callback
 * @dev: drm_device to import into
 * @dma_buf: dma-buf object to import
 * @attach_dev: struct device to dma_buf attach
 *
 * This is the core of drm_gem_prime_import(). It's designed to be called by
 * drivers who want to use a different device structure than &drm_device.dev for
 * attaching via dma_buf. This function calls
 * &drm_driver.gem_prime_import_sg_table internally.
 *
 * Drivers must arrange to call drm_prime_gem_destroy() from their
 * &drm_gem_object_funcs.free hook when using this function.
 */
struct drm_gem_object *drm_gem_prime_import_dev(struct drm_device *dev,
					    struct dma_buf *dma_buf,
					    struct device *attach_dev)
{
	struct dma_buf_attachment *attach;
#ifdef notyet
	struct sg_table *sgt;
#endif
	struct drm_gem_object *obj;
	int ret;

	if (dma_buf->ops == &drm_gem_prime_dmabuf_ops) {
		obj = dma_buf->priv;
		if (obj->dev == dev) {
			/*
			 * Importing dmabuf exported from our own gem increases
			 * refcount on gem itself instead of f_count of dmabuf.
			 */
			drm_gem_object_get(obj);
			return obj;
		}
	}

#ifdef notyet
	if (!dev->driver->gem_prime_import_sg_table)
		return ERR_PTR(-EINVAL);
#endif

	attach = dma_buf_attach(dma_buf, attach_dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

#ifdef notyet
	get_dma_buf(dma_buf);

	sgt = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

	obj = dev->driver->gem_prime_import_sg_table(dev, attach, sgt);
	if (IS_ERR(obj)) {
		ret = PTR_ERR(obj);
		goto fail_unmap;
	}

	obj->import_attach = attach;
	obj->resv = dma_buf->resv;

	return obj;

fail_unmap:
	dma_buf_unmap_attachment_unlocked(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
#else
	ret = 0;
	panic(__func__);
#endif
}
EXPORT_SYMBOL(drm_gem_prime_import_dev);

/**
 * drm_gem_prime_import - helper library implementation of the import callback
 * @dev: drm_device to import into
 * @dma_buf: dma-buf object to import
 *
 * This is the implementation of the gem_prime_import functions for GEM drivers
 * using the PRIME helpers. Drivers can use this as their
 * &drm_driver.gem_prime_import implementation. It is used as the default
 * implementation in drm_gem_prime_fd_to_handle().
 *
 * Drivers must arrange to call drm_prime_gem_destroy() from their
 * &drm_gem_object_funcs.free hook when using this function.
 */
struct drm_gem_object *drm_gem_prime_import(struct drm_device *dev,
					    struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}
EXPORT_SYMBOL(drm_gem_prime_import);

/**
 * drm_prime_sg_to_page_array - convert an sg table into a page array
 * @sgt: scatter-gather table to convert
 * @pages: array of page pointers to store the pages in
 * @max_entries: size of the passed-in array
 *
 * Exports an sg table into an array of pages.
 *
 * This function is deprecated and strongly discouraged to be used.
 * The page array is only useful for page faults and those can corrupt fields
 * in the struct page if they are not handled by the exporting driver.
 */
int __deprecated drm_prime_sg_to_page_array(struct sg_table *sgt,
					    struct vm_page **pages,
					    int max_entries)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct sg_page_iter page_iter;
	struct vm_page **p = pages;

	for_each_sgtable_page(sgt, &page_iter, 0) {
		if (WARN_ON(p - pages >= max_entries))
			return -1;
		*p++ = sg_page_iter_page(&page_iter);
	}
	return 0;
#endif
}
EXPORT_SYMBOL(drm_prime_sg_to_page_array);

/**
 * drm_prime_sg_to_dma_addr_array - convert an sg table into a dma addr array
 * @sgt: scatter-gather table to convert
 * @addrs: array to store the dma bus address of each page
 * @max_entries: size of both the passed-in arrays
 *
 * Exports an sg table into an array of addresses.
 *
 * Drivers should use this in their &drm_driver.gem_prime_import_sg_table
 * implementation.
 */
int drm_prime_sg_to_dma_addr_array(struct sg_table *sgt, dma_addr_t *addrs,
				   int max_entries)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct sg_dma_page_iter dma_iter;
	dma_addr_t *a = addrs;

	for_each_sgtable_dma_page(sgt, &dma_iter, 0) {
		if (WARN_ON(a - addrs >= max_entries))
			return -1;
		*a++ = sg_page_iter_dma_address(&dma_iter);
	}
	return 0;
#endif
}
EXPORT_SYMBOL(drm_prime_sg_to_dma_addr_array);

/**
 * drm_prime_gem_destroy - helper to clean up a PRIME-imported GEM object
 * @obj: GEM object which was created from a dma-buf
 * @sg: the sg-table which was pinned at import time
 *
 * This is the cleanup functions which GEM drivers need to call when they use
 * drm_gem_prime_import() or drm_gem_prime_import_dev() to import dma-bufs.
 */
void drm_prime_gem_destroy(struct drm_gem_object *obj, struct sg_table *sg)
{
	STUB();
#ifdef notyet
	struct dma_buf_attachment *attach;
	struct dma_buf *dma_buf;

	attach = obj->import_attach;
	if (sg)
		dma_buf_unmap_attachment_unlocked(attach, sg, DMA_BIDIRECTIONAL);
	dma_buf = attach->dmabuf;
	dma_buf_detach(attach->dmabuf, attach);
	/* remove the reference */
	dma_buf_put(dma_buf);
#endif
}
EXPORT_SYMBOL(drm_prime_gem_destroy);
