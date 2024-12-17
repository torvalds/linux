/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Linaro Limited
 */

#ifndef __TEE_CORE_H
#define __TEE_CORE_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/list.h>
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

#define TEE_DEVICE_FLAG_REGISTERED	0x1
#define TEE_MAX_DEV_NAME_LEN		32

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
 * @open:		called when the device file is opened
 * @release:		release this open file
 * @open_session:	open a new session
 * @close_session:	close a session
 * @system_session:	declare session as a system session
 * @invoke_func:	invoke a trusted function
 * @cancel_req:		request cancel of an ongoing invoke or open
 * @supp_recv:		called for supplicant to get a command
 * @supp_send:		called for supplicant to send a response
 * @shm_register:	register shared memory buffer in TEE
 * @shm_unregister:	unregister shared memory buffer in TEE
 */
struct tee_driver_ops {
	void (*get_version)(struct tee_device *teedev,
			    struct tee_ioctl_version_data *vers);
	int (*open)(struct tee_context *ctx);
	void (*release)(struct tee_context *ctx);
	int (*open_session)(struct tee_context *ctx,
			    struct tee_ioctl_open_session_arg *arg,
			    struct tee_param *param);
	int (*close_session)(struct tee_context *ctx, u32 session);
	int (*system_session)(struct tee_context *ctx, u32 session);
	int (*invoke_func)(struct tee_context *ctx,
			   struct tee_ioctl_invoke_arg *arg,
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

#endif /*__TEE_CORE_H*/
