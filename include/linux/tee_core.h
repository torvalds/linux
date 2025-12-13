/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Linaro Limited
 */

#ifndef __TEE_CORE_H
#define __TEE_CORE_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/tee.h>
#include <linux/tee_drv.h>
#include <linux/types.h>
#include <linux/uuid.h>

/*
 * The file describes the API provided by the generic TEE driver to the
 * specific TEE driver.
 */

#define TEE_SHM_DYNAMIC		BIT(0)  /* Dynamic shared memory registered */
					/* in secure world */
#define TEE_SHM_USER_MAPPED	BIT(1)  /* Memory mapped in user space */
#define TEE_SHM_POOL		BIT(2)  /* Memory allocated from pool */
#define TEE_SHM_PRIV		BIT(3)  /* Memory private to TEE driver */
#define TEE_SHM_DMA_BUF		BIT(4)	/* Memory with dma-buf handle */
#define TEE_SHM_DMA_MEM		BIT(5)	/* Memory allocated with */
					/* dma_alloc_pages() */

#define TEE_DEVICE_FLAG_REGISTERED	0x1
#define TEE_MAX_DEV_NAME_LEN		32

enum tee_dma_heap_id {
	TEE_DMA_HEAP_SECURE_VIDEO_PLAY = 1,
	TEE_DMA_HEAP_TRUSTED_UI,
	TEE_DMA_HEAP_SECURE_VIDEO_RECORD,
};

/**
 * struct tee_device - TEE Device representation
 * @name:	name of device
 * @desc:	description of device
 * @id:		unique id of device
 * @flags:	represented by TEE_DEVICE_FLAG_REGISTERED above
 * @dev:	embedded basic device structure
 * @cdev:	embedded cdev
 * @num_users:	number of active users of this device
 * @c_no_user:	completion used when unregistering the device
 * @mutex:	mutex protecting @num_users and @idr
 * @idr:	register of user space shared memory objects allocated or
 *		registered on this device
 * @pool:	shared memory pool
 */
struct tee_device {
	char name[TEE_MAX_DEV_NAME_LEN];
	const struct tee_desc *desc;
	int id;
	unsigned int flags;

	struct device dev;
	struct cdev cdev;

	size_t num_users;
	struct completion c_no_users;
	struct mutex mutex;	/* protects num_users and idr */

	struct idr idr;
	struct tee_shm_pool *pool;
};

/**
 * struct tee_driver_ops - driver operations vtable
 * @get_version:	returns version of driver
 * @open:		called for a context when the device file is opened
 * @close_context:	called when the device file is closed
 * @release:		called to release the context
 * @open_session:	open a new session
 * @close_session:	close a session
 * @system_session:	declare session as a system session
 * @invoke_func:	invoke a trusted function
 * @object_invoke_func:	invoke a TEE object
 * @cancel_req:		request cancel of an ongoing invoke or open
 * @supp_recv:		called for supplicant to get a command
 * @supp_send:		called for supplicant to send a response
 * @shm_register:	register shared memory buffer in TEE
 * @shm_unregister:	unregister shared memory buffer in TEE
 *
 * The context given to @open might last longer than the device file if it is
 * tied to other resources in the TEE driver. @close_context is called when the
 * client closes the device file, even if there are existing references to the
 * context. The TEE driver can use @close_context to start cleaning up.
 */
struct tee_driver_ops {
	void (*get_version)(struct tee_device *teedev,
			    struct tee_ioctl_version_data *vers);
	int (*open)(struct tee_context *ctx);
	void (*close_context)(struct tee_context *ctx);
	void (*release)(struct tee_context *ctx);
	int (*open_session)(struct tee_context *ctx,
			    struct tee_ioctl_open_session_arg *arg,
			    struct tee_param *param);
	int (*close_session)(struct tee_context *ctx, u32 session);
	int (*system_session)(struct tee_context *ctx, u32 session);
	int (*invoke_func)(struct tee_context *ctx,
			   struct tee_ioctl_invoke_arg *arg,
			   struct tee_param *param);
	int (*object_invoke_func)(struct tee_context *ctx,
				  struct tee_ioctl_object_invoke_arg *arg,
				  struct tee_param *param);
	int (*cancel_req)(struct tee_context *ctx, u32 cancel_id, u32 session);
	int (*supp_recv)(struct tee_context *ctx, u32 *func, u32 *num_params,
			 struct tee_param *param);
	int (*supp_send)(struct tee_context *ctx, u32 ret, u32 num_params,
			 struct tee_param *param);
	int (*shm_register)(struct tee_context *ctx, struct tee_shm *shm,
			    struct page **pages, size_t num_pages,
			    unsigned long start);
	int (*shm_unregister)(struct tee_context *ctx, struct tee_shm *shm);
};

/**
 * struct tee_desc - Describes the TEE driver to the subsystem
 * @name:	name of driver
 * @ops:	driver operations vtable
 * @owner:	module providing the driver
 * @flags:	Extra properties of driver, defined by TEE_DESC_* below
 */
#define TEE_DESC_PRIVILEGED	0x1
struct tee_desc {
	const char *name;
	const struct tee_driver_ops *ops;
	struct module *owner;
	u32 flags;
};

/**
 * struct tee_protmem_pool - protected memory pool
 * @ops:		operations
 *
 * This is an abstract interface where this struct is expected to be
 * embedded in another struct specific to the implementation.
 */
struct tee_protmem_pool {
	const struct tee_protmem_pool_ops *ops;
};

/**
 * struct tee_protmem_pool_ops - protected memory pool operations
 * @alloc:		called when allocating protected memory
 * @free:		called when freeing protected memory
 * @update_shm:		called when registering a dma-buf to update the @shm
 *			with physical address of the buffer or to return the
 *			@parent_shm of the memory pool
 * @destroy_pool:	called when destroying the pool
 */
struct tee_protmem_pool_ops {
	int (*alloc)(struct tee_protmem_pool *pool, struct sg_table *sgt,
		     size_t size, size_t *offs);
	void (*free)(struct tee_protmem_pool *pool, struct sg_table *sgt);
	int (*update_shm)(struct tee_protmem_pool *pool, struct sg_table *sgt,
			  size_t offs, struct tee_shm *shm,
			  struct tee_shm **parent_shm);
	void (*destroy_pool)(struct tee_protmem_pool *pool);
};

/**
 * tee_device_alloc() - Allocate a new struct tee_device instance
 * @teedesc:	Descriptor for this driver
 * @dev:	Parent device for this device
 * @pool:	Shared memory pool, NULL if not used
 * @driver_data: Private driver data for this device
 *
 * Allocates a new struct tee_device instance. The device is
 * removed by tee_device_unregister().
 *
 * @returns a pointer to a 'struct tee_device' or an ERR_PTR on failure
 */
struct tee_device *tee_device_alloc(const struct tee_desc *teedesc,
				    struct device *dev,
				    struct tee_shm_pool *pool,
				    void *driver_data);

/**
 * tee_device_register() - Registers a TEE device
 * @teedev:	Device to register
 *
 * tee_device_unregister() need to be called to remove the @teedev if
 * this function fails.
 *
 * @returns < 0 on failure
 */
int tee_device_register(struct tee_device *teedev);

/**
 * tee_device_unregister() - Removes a TEE device
 * @teedev:	Device to unregister
 *
 * This function should be called to remove the @teedev even if
 * tee_device_register() hasn't been called yet. Does nothing if
 * @teedev is NULL.
 */
void tee_device_unregister(struct tee_device *teedev);

int tee_device_register_dma_heap(struct tee_device *teedev,
				 enum tee_dma_heap_id id,
				 struct tee_protmem_pool *pool);
void tee_device_put_all_dma_heaps(struct tee_device *teedev);

/**
 * tee_device_get() - Increment the user count for a tee_device
 * @teedev: Pointer to the tee_device
 *
 * If tee_device_unregister() has been called and the final user of @teedev
 * has already released the device, this function will fail to prevent new users
 * from accessing the device during the unregistration process.
 *
 * Returns: true if @teedev remains valid, otherwise false
 */
bool tee_device_get(struct tee_device *teedev);

/**
 * tee_device_put() - Decrease the user count for a tee_device
 * @teedev: pointer to the tee_device
 */
void tee_device_put(struct tee_device *teedev);

/**
 * tee_device_set_dev_groups() - Set device attribute groups
 * @teedev:	Device to register
 * @dev_groups: Attribute groups
 *
 * Assigns the provided @dev_groups to the @teedev to be registered later
 * with tee_device_register(). Calling this function is optional, but if
 * it's called it must be called before tee_device_register().
 */
void tee_device_set_dev_groups(struct tee_device *teedev,
			       const struct attribute_group **dev_groups);

/**
 * tee_session_calc_client_uuid() - Calculates client UUID for session
 * @uuid:		Resulting UUID
 * @connection_method:	Connection method for session (TEE_IOCTL_LOGIN_*)
 * @connectuon_data:	Connection data for opening session
 *
 * Based on connection method calculates UUIDv5 based client UUID.
 *
 * For group based logins verifies that calling process has specified
 * credentials.
 *
 * @return < 0 on failure
 */
int tee_session_calc_client_uuid(uuid_t *uuid, u32 connection_method,
				 const u8 connection_data[TEE_IOCTL_UUID_LEN]);

/**
 * struct tee_shm_pool - shared memory pool
 * @ops:		operations
 * @private_data:	private data for the shared memory manager
 */
struct tee_shm_pool {
	const struct tee_shm_pool_ops *ops;
	void *private_data;
};

/**
 * struct tee_shm_pool_ops - shared memory pool operations
 * @alloc:		called when allocating shared memory
 * @free:		called when freeing shared memory
 * @destroy_pool:	called when destroying the pool
 */
struct tee_shm_pool_ops {
	int (*alloc)(struct tee_shm_pool *pool, struct tee_shm *shm,
		     size_t size, size_t align);
	void (*free)(struct tee_shm_pool *pool, struct tee_shm *shm);
	void (*destroy_pool)(struct tee_shm_pool *pool);
};

/*
 * tee_shm_pool_alloc_res_mem() - Create a shm manager for reserved memory
 * @vaddr:	Virtual address of start of pool
 * @paddr:	Physical address of start of pool
 * @size:	Size in bytes of the pool
 *
 * @returns pointer to a 'struct tee_shm_pool' or an ERR_PTR on failure.
 */
struct tee_shm_pool *tee_shm_pool_alloc_res_mem(unsigned long vaddr,
						phys_addr_t paddr, size_t size,
						int min_alloc_order);

/**
 * tee_shm_pool_free() - Free a shared memory pool
 * @pool:	The shared memory pool to free
 *
 * The must be no remaining shared memory allocated from this pool when
 * this function is called.
 */
static inline void tee_shm_pool_free(struct tee_shm_pool *pool)
{
	pool->ops->destroy_pool(pool);
}

/**
 * tee_protmem_static_pool_alloc() - Create a protected memory manager
 * @paddr:	Physical address of start of pool
 * @size:	Size in bytes of the pool
 *
 * @returns pointer to a 'struct tee_protmem_pool' or an ERR_PTR on failure.
 */
struct tee_protmem_pool *tee_protmem_static_pool_alloc(phys_addr_t paddr,
						       size_t size);

/**
 * tee_get_drvdata() - Return driver_data pointer
 * @returns the driver_data pointer supplied to tee_register().
 */
void *tee_get_drvdata(struct tee_device *teedev);

/**
 * tee_shm_alloc_priv_buf() - Allocate shared memory for private use by specific
 *                            TEE driver
 * @ctx:	The TEE context for shared memory allocation
 * @size:	Shared memory allocation size
 * @returns a pointer to 'struct tee_shm' on success or an ERR_PTR on failure
 */
struct tee_shm *tee_shm_alloc_priv_buf(struct tee_context *ctx, size_t size);

struct tee_shm *tee_shm_alloc_dma_mem(struct tee_context *ctx,
				      size_t page_count);

int tee_dyn_shm_alloc_helper(struct tee_shm *shm, size_t size, size_t align,
			     int (*shm_register)(struct tee_context *ctx,
						 struct tee_shm *shm,
						 struct page **pages,
						 size_t num_pages,
						 unsigned long start));
void tee_dyn_shm_free_helper(struct tee_shm *shm,
			     int (*shm_unregister)(struct tee_context *ctx,
						   struct tee_shm *shm));

/**
 * tee_shm_is_dynamic() - Check if shared memory object is of the dynamic kind
 * @shm:	Shared memory handle
 * @returns true if object is dynamic shared memory
 */
static inline bool tee_shm_is_dynamic(struct tee_shm *shm)
{
	return shm && (shm->flags & TEE_SHM_DYNAMIC);
}

/**
 * tee_shm_put() - Decrease reference count on a shared memory handle
 * @shm:	Shared memory handle
 */
void tee_shm_put(struct tee_shm *shm);

/**
 * tee_shm_get_id() - Get id of a shared memory object
 * @shm:	Shared memory handle
 * @returns id
 */
static inline int tee_shm_get_id(struct tee_shm *shm)
{
	return shm->id;
}

/**
 * tee_shm_get_from_id() - Find shared memory object and increase reference
 * count
 * @ctx:	Context owning the shared memory
 * @id:		Id of shared memory object
 * @returns a pointer to 'struct tee_shm' on success or an ERR_PTR on failure
 */
struct tee_shm *tee_shm_get_from_id(struct tee_context *ctx, int id);

static inline bool tee_param_is_memref(struct tee_param *param)
{
	switch (param->attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK) {
	case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
	case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
	case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
		return true;
	default:
		return false;
	}
}

/**
 * teedev_open() - Open a struct tee_device
 * @teedev:	Device to open
 *
 * @return a pointer to struct tee_context on success or an ERR_PTR on failure.
 */
struct tee_context *teedev_open(struct tee_device *teedev);

/**
 * teedev_close_context() - closes a struct tee_context
 * @ctx:	The struct tee_context to close
 */
void teedev_close_context(struct tee_context *ctx);

/**
 * teedev_ctx_get() - Increment the reference count of a context
 * @ctx: Pointer to the context
 *
 * This function increases the refcount of the context, which is tied to
 * resources shared by the same tee_device. During the unregistration process,
 * the context may remain valid even after tee_device_unregister() has returned.
 *
 * Users should ensure that the context's refcount is properly decreased before
 * calling tee_device_put(), typically within the context's release() function.
 * Alternatively, users can call tee_device_get() and teedev_ctx_get() together
 * and release them simultaneously (see shm_alloc_helper()).
 */
void teedev_ctx_get(struct tee_context *ctx);

/**
 * teedev_ctx_put() - Decrease reference count on a context
 * @ctx: pointer to the context
 */
void teedev_ctx_put(struct tee_context *ctx);

#endif /*__TEE_CORE_H*/
