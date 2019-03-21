/*
 * Header file for dma buffer sharing framework.
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Author: Sumit Semwal <sumit.semwal@ti.com>
 *
 * Many thanks to linaro-mm-sig list, and specially
 * Arnd Bergmann <arnd@arndb.de>, Rob Clark <rob@ti.com> and
 * Daniel Vetter <daniel@ffwll.ch> for their support in creation and
 * refining of this idea.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __DMA_BUF_H__
#define __DMA_BUF_H__

#include <linux/file.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/dma-fence.h>
#include <linux/wait.h>

struct device;
struct dma_buf;
struct dma_buf_attachment;

/**
 * struct dma_buf_ops - operations possible on struct dma_buf
 * @vmap: [optional] creates a virtual mapping for the buffer into kernel
 *	  address space. Same restrictions as for vmap and friends apply.
 * @vunmap: [optional] unmaps a vmap from the buffer
 */
struct dma_buf_ops {
	/**
	 * @attach:
	 *
	 * This is called from dma_buf_attach() to make sure that a given
	 * &dma_buf_attachment.dev can access the provided &dma_buf. Exporters
	 * which support buffer objects in special locations like VRAM or
	 * device-specific carveout areas should check whether the buffer could
	 * be move to system memory (or directly accessed by the provided
	 * device), and otherwise need to fail the attach operation.
	 *
	 * The exporter should also in general check whether the current
	 * allocation fullfills the DMA constraints of the new device. If this
	 * is not the case, and the allocation cannot be moved, it should also
	 * fail the attach operation.
	 *
	 * Any exporter-private housekeeping data can be stored in the
	 * &dma_buf_attachment.priv pointer.
	 *
	 * This callback is optional.
	 *
	 * Returns:
	 *
	 * 0 on success, negative error code on failure. It might return -EBUSY
	 * to signal that backing storage is already allocated and incompatible
	 * with the requirements of requesting device.
	 */
	int (*attach)(struct dma_buf *, struct dma_buf_attachment *);

	/**
	 * @detach:
	 *
	 * This is called by dma_buf_detach() to release a &dma_buf_attachment.
	 * Provided so that exporters can clean up any housekeeping for an
	 * &dma_buf_attachment.
	 *
	 * This callback is optional.
	 */
	void (*detach)(struct dma_buf *, struct dma_buf_attachment *);

	/**
	 * @map_dma_buf:
	 *
	 * This is called by dma_buf_map_attachment() and is used to map a
	 * shared &dma_buf into device address space, and it is mandatory. It
	 * can only be called if @attach has been called successfully. This
	 * essentially pins the DMA buffer into place, and it cannot be moved
	 * any more
	 *
	 * This call may sleep, e.g. when the backing storage first needs to be
	 * allocated, or moved to a location suitable for all currently attached
	 * devices.
	 *
	 * Note that any specific buffer attributes required for this function
	 * should get added to device_dma_parameters accessible via
	 * &device.dma_params from the &dma_buf_attachment. The @attach callback
	 * should also check these constraints.
	 *
	 * If this is being called for the first time, the exporter can now
	 * choose to scan through the list of attachments for this buffer,
	 * collate the requirements of the attached devices, and choose an
	 * appropriate backing storage for the buffer.
	 *
	 * Based on enum dma_data_direction, it might be possible to have
	 * multiple users accessing at the same time (for reading, maybe), or
	 * any other kind of sharing that the exporter might wish to make
	 * available to buffer-users.
	 *
	 * Returns:
	 *
	 * A &sg_table scatter list of or the backing storage of the DMA buffer,
	 * already mapped into the device address space of the &device attached
	 * with the provided &dma_buf_attachment.
	 *
	 * On failure, returns a negative error value wrapped into a pointer.
	 * May also return -EINTR when a signal was received while being
	 * blocked.
	 */
	struct sg_table * (*map_dma_buf)(struct dma_buf_attachment *,
					 enum dma_data_direction);
	/**
	 * @unmap_dma_buf:
	 *
	 * This is called by dma_buf_unmap_attachment() and should unmap and
	 * release the &sg_table allocated in @map_dma_buf, and it is mandatory.
	 * It should also unpin the backing storage if this is the last mapping
	 * of the DMA buffer, it the exporter supports backing storage
	 * migration.
	 */
	void (*unmap_dma_buf)(struct dma_buf_attachment *,
			      struct sg_table *,
			      enum dma_data_direction);

	/* TODO: Add try_map_dma_buf version, to return immed with -EBUSY
	 * if the call would block.
	 */

	/**
	 * @release:
	 *
	 * Called after the last dma_buf_put to release the &dma_buf, and
	 * mandatory.
	 */
	void (*release)(struct dma_buf *);

	/**
	 * @begin_cpu_access:
	 *
	 * This is called from dma_buf_begin_cpu_access() and allows the
	 * exporter to ensure that the memory is actually available for cpu
	 * access - the exporter might need to allocate or swap-in and pin the
	 * backing storage. The exporter also needs to ensure that cpu access is
	 * coherent for the access direction. The direction can be used by the
	 * exporter to optimize the cache flushing, i.e. access with a different
	 * direction (read instead of write) might return stale or even bogus
	 * data (e.g. when the exporter needs to copy the data to temporary
	 * storage).
	 *
	 * This callback is optional.
	 *
	 * FIXME: This is both called through the DMA_BUF_IOCTL_SYNC command
	 * from userspace (where storage shouldn't be pinned to avoid handing
	 * de-factor mlock rights to userspace) and for the kernel-internal
	 * users of the various kmap interfaces, where the backing storage must
	 * be pinned to guarantee that the atomic kmap calls can succeed. Since
	 * there's no in-kernel users of the kmap interfaces yet this isn't a
	 * real problem.
	 *
	 * Returns:
	 *
	 * 0 on success or a negative error code on failure. This can for
	 * example fail when the backing storage can't be allocated. Can also
	 * return -ERESTARTSYS or -EINTR when the call has been interrupted and
	 * needs to be restarted.
	 */
	int (*begin_cpu_access)(struct dma_buf *, enum dma_data_direction);

	/**
	 * @end_cpu_access:
	 *
	 * This is called from dma_buf_end_cpu_access() when the importer is
	 * done accessing the CPU. The exporter can use this to flush caches and
	 * unpin any resources pinned in @begin_cpu_access.
	 * The result of any dma_buf kmap calls after end_cpu_access is
	 * undefined.
	 *
	 * This callback is optional.
	 *
	 * Returns:
	 *
	 * 0 on success or a negative error code on failure. Can return
	 * -ERESTARTSYS or -EINTR when the call has been interrupted and needs
	 * to be restarted.
	 */
	int (*end_cpu_access)(struct dma_buf *, enum dma_data_direction);

	/**
	 * @mmap:
	 *
	 * This callback is used by the dma_buf_mmap() function
	 *
	 * Note that the mapping needs to be incoherent, userspace is expected
	 * to braket CPU access using the DMA_BUF_IOCTL_SYNC interface.
	 *
	 * Because dma-buf buffers have invariant size over their lifetime, the
	 * dma-buf core checks whether a vma is too large and rejects such
	 * mappings. The exporter hence does not need to duplicate this check.
	 * Drivers do not need to check this themselves.
	 *
	 * If an exporter needs to manually flush caches and hence needs to fake
	 * coherency for mmap support, it needs to be able to zap all the ptes
	 * pointing at the backing storage. Now linux mm needs a struct
	 * address_space associated with the struct file stored in vma->vm_file
	 * to do that with the function unmap_mapping_range. But the dma_buf
	 * framework only backs every dma_buf fd with the anon_file struct file,
	 * i.e. all dma_bufs share the same file.
	 *
	 * Hence exporters need to setup their own file (and address_space)
	 * association by setting vma->vm_file and adjusting vma->vm_pgoff in
	 * the dma_buf mmap callback. In the specific case of a gem driver the
	 * exporter could use the shmem file already provided by gem (and set
	 * vm_pgoff = 0). Exporters can then zap ptes by unmapping the
	 * corresponding range of the struct address_space associated with their
	 * own file.
	 *
	 * This callback is optional.
	 *
	 * Returns:
	 *
	 * 0 on success or a negative error code on failure.
	 */
	int (*mmap)(struct dma_buf *, struct vm_area_struct *vma);

	/**
	 * @map:
	 *
	 * Maps a page from the buffer into kernel address space. The page is
	 * specified by offset into the buffer in PAGE_SIZE units.
	 *
	 * This callback is optional.
	 *
	 * Returns:
	 *
	 * Virtual address pointer where requested page can be accessed. NULL
	 * on error or when this function is unimplemented by the exporter.
	 */
	void *(*map)(struct dma_buf *, unsigned long);

	/**
	 * @unmap:
	 *
	 * Unmaps a page from the buffer. Page offset and address pointer should
	 * be the same as the one passed to and returned by matching call to map.
	 *
	 * This callback is optional.
	 */
	void (*unmap)(struct dma_buf *, unsigned long, void *);

	void *(*vmap)(struct dma_buf *);
	void (*vunmap)(struct dma_buf *, void *vaddr);
};

/**
 * struct dma_buf - shared buffer object
 * @size: size of the buffer
 * @file: file pointer used for sharing buffers across, and for refcounting.
 * @attachments: list of dma_buf_attachment that denotes all devices attached.
 * @ops: dma_buf_ops associated with this buffer object.
 * @lock: used internally to serialize list manipulation, attach/detach and vmap/unmap
 * @vmapping_counter: used internally to refcnt the vmaps
 * @vmap_ptr: the current vmap ptr if vmapping_counter > 0
 * @exp_name: name of the exporter; useful for debugging.
 * @owner: pointer to exporter module; used for refcounting when exporter is a
 *         kernel module.
 * @list_node: node for dma_buf accounting and debugging.
 * @priv: exporter specific private data for this buffer object.
 * @resv: reservation object linked to this dma-buf
 * @poll: for userspace poll support
 * @cb_excl: for userspace poll support
 * @cb_shared: for userspace poll support
 *
 * This represents a shared buffer, created by calling dma_buf_export(). The
 * userspace representation is a normal file descriptor, which can be created by
 * calling dma_buf_fd().
 *
 * Shared dma buffers are reference counted using dma_buf_put() and
 * get_dma_buf().
 *
 * Device DMA access is handled by the separate &struct dma_buf_attachment.
 */
struct dma_buf {
	size_t size;
	struct file *file;
	struct list_head attachments;
	const struct dma_buf_ops *ops;
	struct mutex lock;
	unsigned vmapping_counter;
	void *vmap_ptr;
	const char *exp_name;
	struct module *owner;
	struct list_head list_node;
	void *priv;
	struct reservation_object *resv;

	/* poll support */
	wait_queue_head_t poll;

	struct dma_buf_poll_cb_t {
		struct dma_fence_cb cb;
		wait_queue_head_t *poll;

		__poll_t active;
	} cb_excl, cb_shared;
};

/**
 * struct dma_buf_attachment - holds device-buffer attachment data
 * @dmabuf: buffer for this attachment.
 * @dev: device attached to the buffer.
 * @node: list of dma_buf_attachment.
 * @priv: exporter specific attachment data.
 *
 * This structure holds the attachment information between the dma_buf buffer
 * and its user device(s). The list contains one attachment struct per device
 * attached to the buffer.
 *
 * An attachment is created by calling dma_buf_attach(), and released again by
 * calling dma_buf_detach(). The DMA mapping itself needed to initiate a
 * transfer is created by dma_buf_map_attachment() and freed again by calling
 * dma_buf_unmap_attachment().
 */
struct dma_buf_attachment {
	struct dma_buf *dmabuf;
	struct device *dev;
	struct list_head node;
	void *priv;
};

/**
 * struct dma_buf_export_info - holds information needed to export a dma_buf
 * @exp_name:	name of the exporter - useful for debugging.
 * @owner:	pointer to exporter module - used for refcounting kernel module
 * @ops:	Attach allocator-defined dma buf ops to the new buffer
 * @size:	Size of the buffer
 * @flags:	mode flags for the file
 * @resv:	reservation-object, NULL to allocate default one
 * @priv:	Attach private data of allocator to this buffer
 *
 * This structure holds the information required to export the buffer. Used
 * with dma_buf_export() only.
 */
struct dma_buf_export_info {
	const char *exp_name;
	struct module *owner;
	const struct dma_buf_ops *ops;
	size_t size;
	int flags;
	struct reservation_object *resv;
	void *priv;
};

/**
 * DEFINE_DMA_BUF_EXPORT_INFO - helper macro for exporters
 * @name: export-info name
 *
 * DEFINE_DMA_BUF_EXPORT_INFO macro defines the &struct dma_buf_export_info,
 * zeroes it out and pre-populates exp_name in it.
 */
#define DEFINE_DMA_BUF_EXPORT_INFO(name)	\
	struct dma_buf_export_info name = { .exp_name = KBUILD_MODNAME, \
					 .owner = THIS_MODULE }

/**
 * get_dma_buf - convenience wrapper for get_file.
 * @dmabuf:	[in]	pointer to dma_buf
 *
 * Increments the reference count on the dma-buf, needed in case of drivers
 * that either need to create additional references to the dmabuf on the
 * kernel side.  For example, an exporter that needs to keep a dmabuf ptr
 * so that subsequent exports don't create a new dmabuf.
 */
static inline void get_dma_buf(struct dma_buf *dmabuf)
{
	get_file(dmabuf->file);
}

struct dma_buf_attachment *dma_buf_attach(struct dma_buf *dmabuf,
							struct device *dev);
void dma_buf_detach(struct dma_buf *dmabuf,
				struct dma_buf_attachment *dmabuf_attach);

struct dma_buf *dma_buf_export(const struct dma_buf_export_info *exp_info);

int dma_buf_fd(struct dma_buf *dmabuf, int flags);
struct dma_buf *dma_buf_get(int fd);
void dma_buf_put(struct dma_buf *dmabuf);

struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *,
					enum dma_data_direction);
void dma_buf_unmap_attachment(struct dma_buf_attachment *, struct sg_table *,
				enum dma_data_direction);
int dma_buf_begin_cpu_access(struct dma_buf *dma_buf,
			     enum dma_data_direction dir);
int dma_buf_end_cpu_access(struct dma_buf *dma_buf,
			   enum dma_data_direction dir);
void *dma_buf_kmap(struct dma_buf *, unsigned long);
void dma_buf_kunmap(struct dma_buf *, unsigned long, void *);

int dma_buf_mmap(struct dma_buf *, struct vm_area_struct *,
		 unsigned long);
void *dma_buf_vmap(struct dma_buf *);
void dma_buf_vunmap(struct dma_buf *, void *vaddr);
#endif /* __DMA_BUF_H__ */
