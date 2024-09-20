/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2024 Linaro Limited
 */

#ifndef __TEE_DRV_H
#define __TEE_DRV_H

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/mod_devicetable.h>
#include <linux/tee.h>
#include <linux/types.h>

/*
 * The file describes the API provided by the TEE subsystem to the
 * TEE client drivers.
 */

struct tee_device;

/**
 * struct tee_context - driver specific context on file pointer data
 * @teedev:	pointer to this drivers struct tee_device
 * @data:	driver specific context data, managed by the driver
 * @refcount:	reference counter for this structure
 * @releasing:  flag that indicates if context is being released right now.
 *		It is needed to break circular dependency on context during
 *              shared memory release.
 * @supp_nowait: flag that indicates that requests in this context should not
 *              wait for tee-supplicant daemon to be started if not present
 *              and just return with an error code. It is needed for requests
 *              that arises from TEE based kernel drivers that should be
 *              non-blocking in nature.
 * @cap_memref_null: flag indicating if the TEE Client support shared
 *                   memory buffer with a NULL pointer.
 */
struct tee_context {
	struct tee_device *teedev;
	void *data;
	struct kref refcount;
	bool releasing;
	bool supp_nowait;
	bool cap_memref_null;
};

/**
 * struct tee_shm - shared memory object
 * @ctx:	context using the object
 * @paddr:	physical address of the shared memory
 * @kaddr:	virtual address of the shared memory
 * @size:	size of shared memory
 * @offset:	offset of buffer in user space
 * @pages:	locked pages from userspace
 * @num_pages:	number of locked pages
 * @refcount:	reference counter
 * @flags:	defined by TEE_SHM_* in tee_core.h
 * @id:		unique id of a shared memory object on this device, shared
 *		with user space
 * @sec_world_id:
 *		secure world assigned id of this shared memory object, not
 *		used by all drivers
 */
struct tee_shm {
	struct tee_context *ctx;
	phys_addr_t paddr;
	void *kaddr;
	size_t size;
	unsigned int offset;
	struct page **pages;
	size_t num_pages;
	refcount_t refcount;
	u32 flags;
	int id;
	u64 sec_world_id;
};

struct tee_param_memref {
	size_t shm_offs;
	size_t size;
	struct tee_shm *shm;
};

struct tee_param_value {
	u64 a;
	u64 b;
	u64 c;
};

struct tee_param {
	u64 attr;
	union {
		struct tee_param_memref memref;
		struct tee_param_value value;
	} u;
};

/**
 * tee_shm_alloc_kernel_buf() - Allocate kernel shared memory for a
 *                              particular TEE client driver
 * @ctx:	The TEE context for shared memory allocation
 * @size:	Shared memory allocation size
 * @returns a pointer to 'struct tee_shm' on success or an ERR_PTR on failure
 */
struct tee_shm *tee_shm_alloc_kernel_buf(struct tee_context *ctx, size_t size);

/**
 * tee_shm_register_kernel_buf() - Register kernel shared memory for a
 *                                 particular TEE client driver
 * @ctx:	The TEE context for shared memory registration
 * @addr:	Kernel buffer address
 * @length:	Kernel buffer length
 * @returns a pointer to 'struct tee_shm' on success or an ERR_PTR on failure
 */
struct tee_shm *tee_shm_register_kernel_buf(struct tee_context *ctx,
					    void *addr, size_t length);

/**
 * tee_shm_free() - Free shared memory
 * @shm:	Handle to shared memory to free
 */
void tee_shm_free(struct tee_shm *shm);

/**
 * tee_shm_get_va() - Get virtual address of a shared memory plus an offset
 * @shm:	Shared memory handle
 * @offs:	Offset from start of this shared memory
 * @returns virtual address of the shared memory + offs if offs is within
 *	the bounds of this shared memory, else an ERR_PTR
 */
void *tee_shm_get_va(struct tee_shm *shm, size_t offs);

/**
 * tee_shm_get_pa() - Get physical address of a shared memory plus an offset
 * @shm:	Shared memory handle
 * @offs:	Offset from start of this shared memory
 * @pa:		Physical address to return
 * @returns 0 if offs is within the bounds of this shared memory, else an
 *	error code.
 */
int tee_shm_get_pa(struct tee_shm *shm, size_t offs, phys_addr_t *pa);

/**
 * tee_shm_get_size() - Get size of shared memory buffer
 * @shm:	Shared memory handle
 * @returns size of shared memory
 */
static inline size_t tee_shm_get_size(struct tee_shm *shm)
{
	return shm->size;
}

/**
 * tee_shm_get_pages() - Get list of pages that hold shared buffer
 * @shm:	Shared memory handle
 * @num_pages:	Number of pages will be stored there
 * @returns pointer to pages array
 */
static inline struct page **tee_shm_get_pages(struct tee_shm *shm,
					      size_t *num_pages)
{
	*num_pages = shm->num_pages;
	return shm->pages;
}

/**
 * tee_shm_get_page_offset() - Get shared buffer offset from page start
 * @shm:	Shared memory handle
 * @returns page offset of shared buffer
 */
static inline size_t tee_shm_get_page_offset(struct tee_shm *shm)
{
	return shm->offset;
}

/**
 * tee_client_open_context() - Open a TEE context
 * @start:	if not NULL, continue search after this context
 * @match:	function to check TEE device
 * @data:	data for match function
 * @vers:	if not NULL, version data of TEE device of the context returned
 *
 * This function does an operation similar to open("/dev/teeX") in user space.
 * A returned context must be released with tee_client_close_context().
 *
 * Returns a TEE context of the first TEE device matched by the match()
 * callback or an ERR_PTR.
 */
struct tee_context *
tee_client_open_context(struct tee_context *start,
			int (*match)(struct tee_ioctl_version_data *,
				     const void *),
			const void *data, struct tee_ioctl_version_data *vers);

/**
 * tee_client_close_context() - Close a TEE context
 * @ctx:	TEE context to close
 *
 * Note that all sessions previously opened with this context will be
 * closed when this function is called.
 */
void tee_client_close_context(struct tee_context *ctx);

/**
 * tee_client_get_version() - Query version of TEE
 * @ctx:	TEE context to TEE to query
 * @vers:	Pointer to version data
 */
void tee_client_get_version(struct tee_context *ctx,
			    struct tee_ioctl_version_data *vers);

/**
 * tee_client_open_session() - Open a session to a Trusted Application
 * @ctx:	TEE context
 * @arg:	Open session arguments, see description of
 *		struct tee_ioctl_open_session_arg
 * @param:	Parameters passed to the Trusted Application
 *
 * Returns < 0 on error else see @arg->ret for result. If @arg->ret
 * is TEEC_SUCCESS the session identifier is available in @arg->session.
 */
int tee_client_open_session(struct tee_context *ctx,
			    struct tee_ioctl_open_session_arg *arg,
			    struct tee_param *param);

/**
 * tee_client_close_session() - Close a session to a Trusted Application
 * @ctx:	TEE Context
 * @session:	Session id
 *
 * Return < 0 on error else 0, regardless the session will not be
 * valid after this function has returned.
 */
int tee_client_close_session(struct tee_context *ctx, u32 session);

/**
 * tee_client_system_session() - Declare session as a system session
 * @ctx:	TEE Context
 * @session:	Session id
 *
 * This function requests TEE to provision an entry context ready to use for
 * that session only. The provisioned entry context is used for command
 * invocation and session closure, not for command cancelling requests.
 * TEE releases the provisioned context upon session closure.
 *
 * Return < 0 on error else 0 if an entry context has been provisioned.
 */
int tee_client_system_session(struct tee_context *ctx, u32 session);

/**
 * tee_client_invoke_func() - Invoke a function in a Trusted Application
 * @ctx:	TEE Context
 * @arg:	Invoke arguments, see description of
 *		struct tee_ioctl_invoke_arg
 * @param:	Parameters passed to the Trusted Application
 *
 * Returns < 0 on error else see @arg->ret for result.
 */
int tee_client_invoke_func(struct tee_context *ctx,
			   struct tee_ioctl_invoke_arg *arg,
			   struct tee_param *param);

/**
 * tee_client_cancel_req() - Request cancellation of the previous open-session
 * or invoke-command operations in a Trusted Application
 * @ctx:       TEE Context
 * @arg:       Cancellation arguments, see description of
 *             struct tee_ioctl_cancel_arg
 *
 * Returns < 0 on error else 0 if the cancellation was successfully requested.
 */
int tee_client_cancel_req(struct tee_context *ctx,
			  struct tee_ioctl_cancel_arg *arg);

extern const struct bus_type tee_bus_type;

/**
 * struct tee_client_device - tee based device
 * @id:			device identifier
 * @dev:		device structure
 */
struct tee_client_device {
	struct tee_client_device_id id;
	struct device dev;
};

#define to_tee_client_device(d) container_of(d, struct tee_client_device, dev)

/**
 * struct tee_client_driver - tee client driver
 * @id_table:		device id table supported by this driver
 * @driver:		driver structure
 */
struct tee_client_driver {
	const struct tee_client_device_id *id_table;
	struct device_driver driver;
};

#define to_tee_client_driver(d) \
		container_of_const(d, struct tee_client_driver, driver)

#endif /*__TEE_DRV_H*/
