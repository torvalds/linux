/* SPDX-License-Identifier: GPL-2.0-only */
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
 */
#ifndef __DMA_BUF_H__
#define __DMA_BUF_H__

#include <linux/dma-buf-map.h>
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
	  * @cache_sgt_mapping:
	  *
	  * If true the framework will cache the first mapping made for each
	  * attachment. This avoids creating mappings for attachments multiple
	  * times.
	  */
	bool cache_sgt_mapping;

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
	 * allocation fulfills the DMA constraints of the new device. If this
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
	 * @pin:
	 *
	 * This is called by dma_buf_pin() and lets the exporter know that the
	 * DMA-buf can't be moved any more. Ideally, the exporter should
	 * pin the buffer so that it is generally accessible by all
	 * devices.
	 *
	 * This is called with the &dmabuf.resv object locked and is mutual
	 * exclusive with @cache_sgt_mapping.
	 *
	 * This is called automatically for non-dynamic importers from
	 * dma_buf_attach().
	 *
	 * Note that similar to non-dynamic exporters in their @map_dma_buf
	 * callback the driver must guarantee that the memory is available for
	 * use and cleared of any old data by the time this function returns.
	 * Drivers which pipeline their buffer moves internally must wait for
	 * all moves and clears to complete.
	 *
	 * Returns:
	 *
	 * 0 on success, negative error code on failure.
	 */
	int (*pin)(struct dma_buf_attachment *attach);

	/**
	 * @unpin:
	 *
	 * This is called by dma_buf_unpin() and lets the exporter know that the
	 * DMA-buf can be moved again.
	 *
	 * This is called with the dmabuf->resv object locked and is mutual
	 * exclusive with @cache_sgt_mapping.
	 *
	 * This callback is optional.
	 */
	void (*unpin)(struct dma_buf_attachment *attach);

	/**
	 * @map_dma_buf:
	 *
	 * This is called by dma_buf_map_attachment() and is used to map a
	 * shared &dma_buf into device address space, and it is mandatory. It
	 * can only be called if @attach has been called successfully.
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
	 * This is always called with the dmabuf->resv object locked when
	 * the dynamic_mapping flag is true.
	 *
	 * Note that for non-dynamic exporters the driver must guarantee that
	 * that the memory is available for use and cleared of any old data by
	 * the time this function returns.  Drivers which pipeline their buffer
	 * moves internally must wait for all moves and clears to complete.
	 * Dynamic exporters do not need to follow this rule: For non-dynamic
	 * importers the buffer is already pinned through @pin, which has the
	 * same requirements. Dynamic importers otoh are required to obey the
	 * dma_resv fences.
	 *
	 * Returns:
	 *
	 * A &sg_table scatter list of the backing storage of the DMA buffer,
	 * already mapped into the device address space of the &device attached
	 * with the provided &dma_buf_attachment. The addresses and lengths in
	 * the scatter list are PAGE_SIZE aligned.
	 *
	 * On failure, returns a negative error value wrapped into a pointer.
	 * May also return -EINTR when a signal was received while being
	 * blocked.
	 *
	 * Note that exporters should not try to cache the scatter list, or
	 * return the same one for multiple calls. Caching is done either by the
	 * DMA-BUF code (for non-dynamic importers) or the importer. Ownership
	 * of the scatter list is transferred to the caller, and returned by
	 * @unmap_dma_buf.
	 */
	struct sg_table * (*map_dma_buf)(struct dma_buf_attachment *,
					 enum dma_data_direction);
	/**
	 * @unmap_dma_buf:
	 *
	 * This is called by dma_buf_unmap_attachment() and should unmap and
	 * release the &sg_table allocated in @map_dma_buf, and it is mandatory.
	 * For static dma_buf handling this might also unpin the backing
	 * storage if this is the last mapping of the DMA buffer.
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
	 * exporter to ensure that the memory is actually coherent for cpu
	 * access. The exporter also needs to ensure that cpu access is coherent
	 * for the access direction. The direction can be used by the exporter
	 * to optimize the cache flushing, i.e. access with a different
	 * direction (read instead of write) might return stale or even bogus
	 * data (e.g. when the exporter needs to copy the data to temporary
	 * storage).
	 *
	 * Note that this is both called through the DMA_BUF_IOCTL_SYNC IOCTL
	 * command for userspace mappings established through @mmap, and also
	 * for kernel mappings established with @vmap.
	 *
	 * This callback is optional.
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
	 * undo anything else done in @begin_cpu_access.
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
	 * to bracket CPU access using the DMA_BUF_IOCTL_SYNC interface.
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

	int (*vmap)(struct dma_buf *dmabuf, struct dma_buf_map *map);
	void (*vunmap)(struct dma_buf *dmabuf, struct dma_buf_map *map);
};

/**
 * struct dma_buf - shared buffer object
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
	/**
	 * @size:
	 *
	 * Size of the buffer; invariant over the lifetime of the buffer.
	 */
	size_t size;

	/**
	 * @file:
	 *
	 * File pointer used for sharing buffers across, and for refcounting.
	 * See dma_buf_get() and dma_buf_put().
	 */
	struct file *file;

	/**
	 * @attachments:
	 *
	 * List of dma_buf_attachment that denotes all devices attached,
	 * protected by &dma_resv lock @resv.
	 */
	struct list_head attachments;

	/** @ops: dma_buf_ops associated with this buffer object. */
	const struct dma_buf_ops *ops;

	/**
	 * @lock:
	 *
	 * Used internally to serialize list manipulation, attach/detach and
	 * vmap/unmap. Note that in many cases this is superseeded by
	 * dma_resv_lock() on @resv.
	 */
	struct mutex lock;

	/**
	 * @vmapping_counter:
	 *
	 * Used internally to refcnt the vmaps returned by dma_buf_vmap().
	 * Protected by @lock.
	 */
	unsigned vmapping_counter;

	/**
	 * @vmap_ptr:
	 * The current vmap ptr if @vmapping_counter > 0. Protected by @lock.
	 */
	struct dma_buf_map vmap_ptr;

	/**
	 * @exp_name:
	 *
	 * Name of the exporter; useful for debugging. See the
	 * DMA_BUF_SET_NAME IOCTL.
	 */
	const char *exp_name;

	/**
	 * @name:
	 *
	 * Userspace-provided name; useful for accounting and debugging,
	 * protected by dma_resv_lock() on @resv and @name_lock for read access.
	 */
	const char *name;

	/** @name_lock: Spinlock to protect name acces for read access. */
	spinlock_t name_lock;

	/**
	 * @owner:
	 *
	 * Pointer to exporter module; used for refcounting when exporter is a
	 * kernel module.
	 */
	struct module *owner;

	/** @list_node: node for dma_buf accounting and debugging. */
	struct list_head list_node;

	/** @priv: exporter specific private data for this buffer object. */
	void *priv;

	/**
	 * @resv:
	 *
	 * Reservation object linked to this dma-buf.
	 *
	 * IMPLICIT SYNCHRONIZATION RULES:
	 *
	 * Drivers which support implicit synchronization of buffer access as
	 * e.g. exposed in `Implicit Fence Poll Support`_ must follow the
	 * below rules.
	 *
	 * - Drivers must add a shared fence through dma_resv_add_shared_fence()
	 *   for anything the userspace API considers a read access. This highly
	 *   depends upon the API and window system.
	 *
	 * - Similarly drivers must set the exclusive fence through
	 *   dma_resv_add_excl_fence() for anything the userspace API considers
	 *   write access.
	 *
	 * - Drivers may just always set the exclusive fence, since that only
	 *   causes unecessarily synchronization, but no correctness issues.
	 *
	 * - Some drivers only expose a synchronous userspace API with no
	 *   pipelining across drivers. These do not set any fences for their
	 *   access. An example here is v4l.
	 *
	 * DYNAMIC IMPORTER RULES:
	 *
	 * Dynamic importers, see dma_buf_attachment_is_dynamic(), have
	 * additional constraints on how they set up fences:
	 *
	 * - Dynamic importers must obey the exclusive fence and wait for it to
	 *   signal before allowing access to the buffer's underlying storage
	 *   through the device.
	 *
	 * - Dynamic importers should set fences for any access that they can't
	 *   disable immediately from their &dma_buf_attach_ops.move_notify
	 *   callback.
	 *
	 * IMPORTANT:
	 *
	 * All drivers must obey the struct dma_resv rules, specifically the
	 * rules for updating fences, see &dma_resv.fence_excl and
	 * &dma_resv.fence. If these dependency rules are broken access tracking
	 * can be lost resulting in use after free issues.
	 */
	struct dma_resv *resv;

	/** @poll: for userspace poll support */
	wait_queue_head_t poll;

	/** @cb_excl: for userspace poll support */
	/** @cb_shared: for userspace poll support */
	struct dma_buf_poll_cb_t {
		struct dma_fence_cb cb;
		wait_queue_head_t *poll;

		__poll_t active;
	} cb_in, cb_out;
#ifdef CONFIG_DMABUF_SYSFS_STATS
	/**
	 * @sysfs_entry:
	 *
	 * For exposing information about this buffer in sysfs. See also
	 * `DMA-BUF statistics`_ for the uapi this enables.
	 */
	struct dma_buf_sysfs_entry {
		struct kobject kobj;
		struct dma_buf *dmabuf;
	} *sysfs_entry;
#endif
};

/**
 * struct dma_buf_attach_ops - importer operations for an attachment
 *
 * Attachment operations implemented by the importer.
 */
struct dma_buf_attach_ops {
	/**
	 * @allow_peer2peer:
	 *
	 * If this is set to true the importer must be able to handle peer
	 * resources without struct pages.
	 */
	bool allow_peer2peer;

	/**
	 * @move_notify: [optional] notification that the DMA-buf is moving
	 *
	 * If this callback is provided the framework can avoid pinning the
	 * backing store while mappings exists.
	 *
	 * This callback is called with the lock of the reservation object
	 * associated with the dma_buf held and the mapping function must be
	 * called with this lock held as well. This makes sure that no mapping
	 * is created concurrently with an ongoing move operation.
	 *
	 * Mappings stay valid and are not directly affected by this callback.
	 * But the DMA-buf can now be in a different physical location, so all
	 * mappings should be destroyed and re-created as soon as possible.
	 *
	 * New mappings can be created after this callback returns, and will
	 * point to the new location of the DMA-buf.
	 */
	void (*move_notify)(struct dma_buf_attachment *attach);
};

/**
 * struct dma_buf_attachment - holds device-buffer attachment data
 * @dmabuf: buffer for this attachment.
 * @dev: device attached to the buffer.
 * @node: list of dma_buf_attachment, protected by dma_resv lock of the dmabuf.
 * @sgt: cached mapping.
 * @dir: direction of cached mapping.
 * @peer2peer: true if the importer can handle peer resources without pages.
 * @priv: exporter specific attachment data.
 * @importer_ops: importer operations for this attachment, if provided
 * dma_buf_map/unmap_attachment() must be called with the dma_resv lock held.
 * @importer_priv: importer specific attachment data.
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
	struct sg_table *sgt;
	enum dma_data_direction dir;
	bool peer2peer;
	const struct dma_buf_attach_ops *importer_ops;
	void *importer_priv;
	void *priv;
};

/**
 * struct dma_buf_export_info - holds information needed to export a dma_buf
 * @exp_name:	name of the exporter - useful for debugging.
 * @owner:	pointer to exporter module - used for refcounting kernel module
 * @ops:	Attach allocator-defined dma buf ops to the new buffer
 * @size:	Size of the buffer - invariant over the lifetime of the buffer
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
	struct dma_resv *resv;
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

/**
 * dma_buf_is_dynamic - check if a DMA-buf uses dynamic mappings.
 * @dmabuf: the DMA-buf to check
 *
 * Returns true if a DMA-buf exporter wants to be called with the dma_resv
 * locked for the map/unmap callbacks, false if it doesn't wants to be called
 * with the lock held.
 */
static inline bool dma_buf_is_dynamic(struct dma_buf *dmabuf)
{
	return !!dmabuf->ops->pin;
}

/**
 * dma_buf_attachment_is_dynamic - check if a DMA-buf attachment uses dynamic
 * mappings
 * @attach: the DMA-buf attachment to check
 *
 * Returns true if a DMA-buf importer wants to call the map/unmap functions with
 * the dma_resv lock held.
 */
static inline bool
dma_buf_attachment_is_dynamic(struct dma_buf_attachment *attach)
{
	return !!attach->importer_ops;
}

struct dma_buf_attachment *dma_buf_attach(struct dma_buf *dmabuf,
					  struct device *dev);
struct dma_buf_attachment *
dma_buf_dynamic_attach(struct dma_buf *dmabuf, struct device *dev,
		       const struct dma_buf_attach_ops *importer_ops,
		       void *importer_priv);
void dma_buf_detach(struct dma_buf *dmabuf,
		    struct dma_buf_attachment *attach);
int dma_buf_pin(struct dma_buf_attachment *attach);
void dma_buf_unpin(struct dma_buf_attachment *attach);

struct dma_buf *dma_buf_export(const struct dma_buf_export_info *exp_info);

int dma_buf_fd(struct dma_buf *dmabuf, int flags);
struct dma_buf *dma_buf_get(int fd);
void dma_buf_put(struct dma_buf *dmabuf);

struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *,
					enum dma_data_direction);
void dma_buf_unmap_attachment(struct dma_buf_attachment *, struct sg_table *,
				enum dma_data_direction);
void dma_buf_move_notify(struct dma_buf *dma_buf);
int dma_buf_begin_cpu_access(struct dma_buf *dma_buf,
			     enum dma_data_direction dir);
int dma_buf_end_cpu_access(struct dma_buf *dma_buf,
			   enum dma_data_direction dir);

int dma_buf_mmap(struct dma_buf *, struct vm_area_struct *,
		 unsigned long);
int dma_buf_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map);
void dma_buf_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map);
#endif /* __DMA_BUF_H__ */
