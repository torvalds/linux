/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __MSM_HW_FENCE_H
#define __MSM_HW_FENCE_H

#include <linux/types.h>
#include <linux/dma-fence.h>

/**
 * MSM_HW_FENCE_FLAG_ENABLED_BIT - Hw-fence is enabled for the dma_fence.
 *
 * Drivers set this flag in the dma_fence 'flags' to fences that
 * are backed up by a hw-fence.
 */
#define MSM_HW_FENCE_FLAG_ENABLED_BIT    31

/**
 * MSM_HW_FENCE_FLAG_SIGNALED_BIT - Hw-fence is signaled for the dma_fence.
 *
 * This flag is set by hw-fence driver when a client wants to add itself as
 * a waiter for this hw-fence. The client uses this flag to avoid adding itself
 * as a waiter for a fence that is already retired.
 */
#define MSM_HW_FENCE_FLAG_SIGNALED_BIT    30

/**
 * MSM_HW_FENCE_ERROR_RESET - Hw-fence flagged as error due to forced reset from producer.
 */
#define MSM_HW_FENCE_ERROR_RESET    BIT(0)

/**
 * MSM_HW_FENCE_RESET_WITHOUT_ERROR: Resets client and its hw-fences, signaling them without error.
 * MSM_HW_FENCE_RESET_WITHOUT_DESTROY: Resets client and its hw-fences, signaling without
 *                                      destroying the fences.
 */
#define MSM_HW_FENCE_RESET_WITHOUT_ERROR    BIT(0)
#define MSM_HW_FENCE_RESET_WITHOUT_DESTROY  BIT(1)

/**
 * MSM_HW_FENCE_UPDATE_ERROR_WITH_MOVE: Updates client tx queue error by moving fence with error to
 *                                      beginning of queue.
 */
#define MSM_HW_FENCE_UPDATE_ERROR_WITH_MOVE      BIT(0)

/**
 * MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT - Maximum number of signals per client
 */
#define MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT 64

/**
 * MSM_HW_FENCE_DBG_DUMP_QUEUES: Dumps queues information
 * MSM_HW_FENCE_DBG_DUMP_TABLE: Dumps hwfence table
 * MSM_HW_FENCE_DBG_DUMP_EVENTS: Dumps hwfence ctl events
 */
#define MSM_HW_FENCE_DBG_DUMP_QUEUES        BIT(0)
#define MSM_HW_FENCE_DBG_DUMP_TABLE         BIT(1)
#define MSM_HW_FENCE_DBG_DUMP_EVENTS        BIT(2)

/**
 * struct msm_hw_fence_create_params - Creation parameters.
 *
 * @name : Optional parameter associating a name with the object for debug purposes.
 *         Only first 64 bytes are accepted, rest will be ignored.
 * @handle : Pointer to fence handle (filled by function).
 * @fence : Pointer to fence.
 * @flags : flags for customization.
 */
struct msm_hw_fence_create_params {
	const char *name;
	u64 *handle;
	void *fence;
	u32 flags;
};

/**
 * struct msm_hw_fence_hfi_queue_table_header - HFI queue table structure.
 * @version: HFI protocol version.
 * @size: Queue table size in dwords.
 * @qhdr0_offset: First queue header offset (dwords) in this table.
 * @qhdr_size: Queue header size.
 * @num_q: Number of queues defined in this table.
 * @num_active_q: Number of active queues.
 */
struct msm_hw_fence_hfi_queue_table_header {
	u32 version;
	u32 size;
	u32 qhdr0_offset;
	u32 qhdr_size;
	u32 num_q;
	u32 num_active_q;
};

/**
 * struct msm_hw_fence_hfi_queue_header - HFI queue header structure.
 * @status: Active = 1, Inactive = 0.
 * @start_addr: Starting address of the queue.
 * @type: Queue type (rx/tx).
 * @queue_size: Size of the queue.
 * @pkt_size: Size of the queue packet entries,
 *            0 - means variable size of message in the queue,
 *            non-zero - size of the packet, fixed.
 * @pkt_drop_cnt: Number of packets drop by sender.
 * @rx_wm: Receiver watermark, applicable in event driven mode.
 * @tx_wm: Sender watermark, applicable in event driven mode.
 * @rx_req: Receiver sets this bit if queue is empty.
 * @tx_req: Sender sets this bit if queue is full.
 * @rx_irq_status: Receiver sets this bit and triggers an interrupt to the
 *                 sender after packets are dequeued. Sender clears this bit.
 * @tx_irq_status: Sender sets this bit and triggers an interrupt to the
 *                 receiver after packets are queued. Receiver clears this bit.
 * @read_index: read index of the queue.
 * @write_index: write index of the queue.
 */
struct msm_hw_fence_hfi_queue_header {
	u32 status;
	u32 start_addr;
	u32 type;
	u32 queue_size;
	u32 pkt_size;
	u32 pkt_drop_cnt;
	u32 rx_wm;
	u32 tx_wm;
	u32 rx_req;
	u32 tx_req;
	u32 rx_irq_status;
	u32 tx_irq_status;
	u32 read_index;
	u32 write_index;
};

/**
 * struct msm_hw_fence_mem_addr - Memory descriptor of the queue allocated by
 *                           the fence driver for each client during
 *                           register.
 * @virtual_addr: Kernel virtual address of the queue.
 * @device_addr: Physical address of the memory object.
 * @size: Size of the memory.
 * @mem_data: Internal pointer with the attributes of the allocation.
 */
struct msm_hw_fence_mem_addr {
	void *virtual_addr;
	phys_addr_t device_addr;
	u64 size;
	void *mem_data;
};

/**
 * struct msm_hw_fence_cb_data - Data passed back in fence error callback.
 * @data: data registered with callback
 * @fence: fence signaled with error
 */
struct msm_hw_fence_cb_data {
	void *data;
	struct dma_fence *fence;
};

/**
 * msm_hw_fence_error_cb: Callback function registered by waiting clients.
 *                        Dispatched when client is waiting on a fence
 *                        signaled with error.
 *
 * @handle: handle of fence signaled with error
 * @error: error signed for fence
 * @cb_data: pointer to struct containing opaque pointer registered with callback
 *           and fence information
 */
typedef void (*msm_hw_fence_error_cb_t)(u32 handle, int error, void *cb_data);

/**
 * enum hw_fence_client_id - Unique identifier of the supported clients.
 * @HW_FENCE_CLIENT_ID_CTX0: GFX Client.
 * @HW_FENCE_CLIENT_ID_CTL0: DPU Client 0.
 * @HW_FENCE_CLIENT_ID_CTL1: DPU Client 1.
 * @HW_FENCE_CLIENT_ID_CTL2: DPU Client 2.
 * @HW_FENCE_CLIENT_ID_CTL3: DPU Client 3.
 * @HW_FENCE_CLIENT_ID_CTL4: DPU Client 4.
 * @HW_FENCE_CLIENT_ID_CTL5: DPU Client 5.
 * @HW_FENCE_CLIENT_ID_VAL0: debug Validation client 0.
 * @HW_FENCE_CLIENT_ID_VAL1: debug Validation client 1.
 * @HW_FENCE_CLIENT_ID_VAL2: debug Validation client 2.
 * @HW_FENCE_CLIENT_ID_VAL3: debug Validation client 3.
 * @HW_FENCE_CLIENT_ID_VAL4: debug Validation client 4.
 * @HW_FENCE_CLIENT_ID_VAL5: debug Validation client 5.
 * @HW_FENCE_CLIENT_ID_VAL6: debug Validation client 6.
 * @HW_FENCE_CLIENT_ID_IPE: IPE Client.
 * @HW_FENCE_CLIENT_ID_VPU: VPU Client.
 * @HW_FENCE_CLIENT_ID_IFE0: IFE0 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE1: IFE1 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE2: IFE2 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE3: IFE3 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE4: IFE4 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE5: IFE5 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE6: IFE6 Client 0.
 * @HW_FENCE_CLIENT_ID_IFE7: IFE7 Client 0.
 * @HW_FENCE_CLIENT_MAX: Max number of clients, any client must be added
 *                       before this enum.
 */
enum hw_fence_client_id {
	HW_FENCE_CLIENT_ID_CTX0 = 0x1,
	HW_FENCE_CLIENT_ID_CTL0,
	HW_FENCE_CLIENT_ID_CTL1,
	HW_FENCE_CLIENT_ID_CTL2,
	HW_FENCE_CLIENT_ID_CTL3,
	HW_FENCE_CLIENT_ID_CTL4,
	HW_FENCE_CLIENT_ID_CTL5,
	HW_FENCE_CLIENT_ID_VAL0,
	HW_FENCE_CLIENT_ID_VAL1,
	HW_FENCE_CLIENT_ID_VAL2,
	HW_FENCE_CLIENT_ID_VAL3,
	HW_FENCE_CLIENT_ID_VAL4,
	HW_FENCE_CLIENT_ID_VAL5,
	HW_FENCE_CLIENT_ID_VAL6,
	HW_FENCE_CLIENT_ID_IPE,
	HW_FENCE_CLIENT_ID_VPU = HW_FENCE_CLIENT_ID_IPE + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE0 = HW_FENCE_CLIENT_ID_VPU + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE1 = HW_FENCE_CLIENT_ID_IFE0 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE2 = HW_FENCE_CLIENT_ID_IFE1 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE3 = HW_FENCE_CLIENT_ID_IFE2 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE4 = HW_FENCE_CLIENT_ID_IFE3 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE5 = HW_FENCE_CLIENT_ID_IFE4 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE6 = HW_FENCE_CLIENT_ID_IFE5 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_ID_IFE7 = HW_FENCE_CLIENT_ID_IFE6 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT,
	HW_FENCE_CLIENT_MAX = HW_FENCE_CLIENT_ID_IFE7 + MSM_HW_FENCE_MAX_SIGNAL_PER_CLIENT
};

#if IS_ENABLED(CONFIG_QTI_HW_FENCE)
/**
 * msm_hw_fence_register() - Registers a client with the HW Fence Driver.
 * @client_id: ID of the client that is being registered.
 * @mem_descriptor: Pointer to fill the memory descriptor. Fence
 *                  controller driver fills this pointer with the
 *                  memory descriptor for the rx/tx queues.
 *
 * This call initializes any shared memory region for the tables/queues
 * required for the HW Fence Driver to communicate with Fence Controller
 * for this client_id and fills the memory descriptor for the queues
 * that the client hw cores need to manage.
 *
 * Return: Handle to the client object that must be used for further calls
 *         to the fence controller driver or NULL in case of error.
 *
 * The returned handle is used internally by the fence controller driver
 * in further calls to identify the client and access any resources
 * allocated for this client.
 */
void *msm_hw_fence_register(
	enum hw_fence_client_id client_id,
	struct msm_hw_fence_mem_addr *mem_descriptor);

/**
 * msm_hw_fence_deregister() - Deregisters a client that was previously
 *                             registered with the HW Fence Driver.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_deregister(void *client_handle);

/**
 * msm_hw_fence_create() - Creates a new hw fence.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @params: Hw fence creation parameters containing dma fence
 *                 to create its associated hw-fence.
 *
 * This call creates the hw fence and registers it with the fence
 * controller. After the creation of this fence, it is a Client Driver
 * responsibility to 'destroy' this fence to prevent any leakage of
 * hw-fence resources.
 * To destroy a fence, 'msm_hw_fence_destroy' must be called, once the
 * fence is not required anymore, which is when all the references to
 * the dma-fence are released.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_create(void *client_handle,
	struct msm_hw_fence_create_params *params);

/**
 * msm_hw_fence_destroy() - Destroys a hw fence.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @fence: Sw dma-fence to destroy its associated hw-fence.
 *
 * The fence destroyed by this function, is a fence that must have been
 * created by the hw fence driver through 'msm_hw_fence_create' call.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_destroy(void *client_handle,
	struct dma_fence *fence);

/**
 * msm_hw_fence_destroy_with_handle() - Destroys a hw fence through its handle.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @handle: handle for hw-fence to destroy
 *
 * The fence destroyed by this function, is a fence that must have been
 * created by the hw fence driver through 'msm_hw_fence_create' call.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_destroy_with_handle(void *client_handle, u64 handle);

/**
 * msm_hw_fence_wait_update_v2() - Register or unregister the Client with the
 *                           Fence Controller as a waiting-client of the
 *                           list of fences received as parameter.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @fences: Pointer to an array of pointers containing the fences to
 *          'wait-on' for this client. If a 'fence-array' fence is passed,
 *          driver will iterate through the individual 'fences' which are
 *          part of the 'fence-array' and will register to wait-for-all the
 *          individual fences of the fence-array.
 *          A 'fence-array' passed as parameter can only have 'individual'
 *          fences and cannot have another nested 'fence-array',
 *          otherwise this API will return failure.
 *          Also, all the 'fences' in this list must have a corresponding
 *          hw-fence that was registered by the producer of the fence,
 *          otherwise, this API will return failure.
 * @handles: Optional pointer to an array of handles of 'fences'.
 *           If non-null, these handles are filled by the function.
 *           This list must have the same size as 'fences' if present.
 * @client_data_list: Optional pointer to an array of u64 client_data
 *                    values for each fence in 'fences'.
 *                    If non-null, this list must have the same size as
 *                    the 'fences' list. This client registers each fence
 *                    with the client_data value at the same index so that
 *                    this value is returned to the client upon signaling
 *                    of the fence.
 *                    If a null pointer is provided, a default value of
 *                    zero is registered as the client_data of each fence.
 * @num_fences: Number of elements in the 'fences' list (and 'handles' and
 *              'client_data_list' if either or both are present).
 * @reg: Boolean to indicate if register or unregister for waiting on
 *            the hw-fence.
 *
 * If the 'register' boolean is set as true, this API will register with
 * the Fence Controller the Client as a consumer (i.e. 'wait-client') of
 * the fences received as parameter.
 * Function will return immediately after the client was registered
 * (i.e this function does not wait for the fences to be signaled).
 * When any of the Fences received as parameter is signaled (or all the
 * fences in case of a fence-array), Fence controller will trigger the hw
 * signal to notify the Client hw-core about the signaled fence (or fences
 * in case of a fence array). i.e. signalization of the hw fence it is a
 * hw to hw communication between Fence Controller and the Client hw-core,
 * and this API is only the interface to allow the Client Driver to
 * register its Client hw-core for the hw-to-hw notification.
 * If the 'register' boolean is set as false, this API will unregister
 * with the Fence Controller the Client as a consumer, this is used for
 * cases where a Timeout waiting for a fence occurs and client drivers want
 * to unregister for signal.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_wait_update_v2(void *client_handle,
	struct dma_fence **fences, u64 *handles, u64 *client_data_list, u32 num_fences, bool reg);

/**
 * msm_hw_fence_wait_update() - Register or unregister the Client with the
 *                           Fence Controller as a waiting-client of the
 *                           list of fences received as parameter.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @fences: Pointer to an array of pointers containing the fences to
 *          'wait-on' for this client. If a 'fence-array' fence is passed,
 *          driver will iterate through the individual 'fences' which are
 *          part of the 'fence-array' and will register to wait-for-all the
 *          individual fences of the fence-array.
 *          A 'fence-array' passed as parameter can only have 'individual'
 *          fences and cannot have another nested 'fence-array',
 *          otherwise this API will return failure.
 *          Also, all the 'fences' in this list must have a corresponding
 *          hw-fence that was registered by the producer of the fence,
 *          otherwise, this API will return failure.
 * @num_fences: Number of elements in the 'fences' list.
 * @reg: Boolean to indicate if register or unregister for waiting on
 *            the hw-fence.
 *
 * If the 'register' boolean is set as true, this API will register with
 * the Fence Controller the Client as a consumer (i.e. 'wait-client') of
 * the fences received as parameter.
 * Function will return immediately after the client was registered
 * (i.e this function does not wait for the fences to be signaled).
 * When any of the Fences received as parameter is signaled (or all the
 * fences in case of a fence-array), Fence controller will trigger the hw
 * signal to notify the Client hw-core about the signaled fence (or fences
 * in case of a fence array). i.e. signalization of the hw fence it is a
 * hw to hw communication between Fence Controller and the Client hw-core,
 * and this API is only the interface to allow the Client Driver to
 * register its Client hw-core for the hw-to-hw notification.
 * If the 'register' boolean is set as false, this API will unregister
 * with the Fence Controller the Client as a consumer, this is used for
 * cases where a Timeout waiting for a fence occurs and client drivers want
 * to unregister for signal.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_wait_update(void *client_handle,
	struct dma_fence **fences, u32 num_fences, bool reg);

/**
 * msm_hw_fence_reset_client() - Resets the HW Fence Client.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @reset_flags: Flags to choose the reset type. See MSM_HW_FENCE_RESET_*
 *               definitions.
 *
 * This function iterates through the HW Fences and removes the client
 * from the waiting-client mask in any of the HW Fences and signal the
 * fences owned by that client.
 * This function should only be called by clients upon error, when clients
 * did a HW reset, to make sure any HW Fence where the client was register
 * for wait are removed, and any Fence owned by the client are signaled.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_reset_client(void *client_handle, u32 reset_flags);

/**
 * msm_hw_fence_reset_client_by_id() - Resets the HW Fence Client through
 *                                     its id.
 * @client_id: id of client to reset
 * @reset_flags: Flags to choose the reset type. See MSM_HW_FENCE_RESET_*
 *               definitions.
 *
 * This function iterates through the HW Fences and removes the client
 * from the waiting-client mask in any of the HW Fences and signal the
 * fences owned by that client.
 * This function should only be called by clients upon error, when clients
 * did a HW reset, to make sure any HW Fence where the client was register
 * for wait are removed, and any Fence owned by the client are signaled.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_reset_client_by_id(enum hw_fence_client_id client_id, u32 reset_flags);

/**
 * msm_hw_fence_update_txq() - Updates Client Tx Queue with the Fence info.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @handle: handle for fence to update in the Tx Queue.
 * @flags: flags to set in the queue for the fence.
 * @error: error to set in the queue for the fence.
 *
 * This function should only be used by clients that cannot have the Tx Queue
 * updated by the Firmware or the HW Core.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_update_txq(void *client_handle, u64 handle, u64 flags, u32 error);

/**
 * msm_hw_fence_update_txq_error() - Updates error field for fence already in Tx Queue.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @handle: handle for existing fence in Tx Queue to update.
 * @error: error to set in the queue for the fence.
 * @update_flags: flags to choose the update type. See MSM_HW_FENCE_UPDATE_ERROR_*
 *                definitions.
 *
 * This function should only be used by clients that cannot have the Tx Queue
 * updated by the Firmware or the HW Core.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_update_txq_error(void *client_handle, u64 handle, u32 error, u32 update_flags);

/**
 * msm_hw_fence_trigger_signal() - Triggers signal for the tx/rx signal pair
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @ tx_client_id: id of the client triggering the signal.
 * @ rx_client_id: id of the client receiving the signal.
 * @ signal_id: id of the signal to trigger
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_trigger_signal(void *client_handle, u32 tx_client_id, u32 rx_client_id,
	u32 signal_id);

/**
 * msm_hw_fence_register_error_cb() - Register callback to be dispatched when
 *                                    HW Fence Client is waiting for a fence
 *                                    that is signaled with error.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 * @cb: pointer to callback function to be invoked
 * @data: opaque pointer passed back with callback
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_register_error_cb(void *client_handle, msm_hw_fence_error_cb_t cb, void *data);

/**
 * msm_hw_fence_deregister_error_cb() - Deregister callback to be dispatched when
 *                                      HW Fence Client is waiting for a fence
 *                                      that is signaled with error.
 * @client_handle: Hw fence driver client handle, this handle was returned
 *                 during the call 'msm_hw_fence_register' to register the
 *                 client.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_deregister_error_cb(void *client_handle);

#else
static inline void *msm_hw_fence_register(enum hw_fence_client_id client_id,
	struct msm_hw_fence_mem_addr *mem_descriptor)
{
	return NULL;
}

static inline int msm_hw_fence_deregister(void *client_handle)
{
	return -EINVAL;
}

static inline int msm_hw_fence_create(void *client_handle,
	struct msm_hw_fence_create_params *params)
{
	return -EINVAL;
}

static inline int msm_hw_fence_destroy(void *client_handle, struct dma_fence *fence)
{
	return -EINVAL;
}

static inline int msm_hw_fence_destroy_with_handle(void *client_handle, u64 handle)
{
	return -EINVAL;
}

static inline int msm_hw_fence_wait_update_v2(void *client_handle,
	struct dma_fence **fences, u64 *handles, u64 *client_data_list, u32 num_fences, bool reg)
{
	return -EINVAL;
}

static inline int msm_hw_fence_wait_update(void *client_handle,
	struct dma_fence **fences, u32 num_fences, bool reg)
{
	return -EINVAL;
}

static inline int msm_hw_fence_reset_client(void *client_handle, u32 reset_flags)
{
	return -EINVAL;
}

static inline int msm_hw_fence_reset_client_by_id(enum hw_fence_client_id client_id,
	u32 reset_flags)
{
	return -EINVAL;
}

static inline int msm_hw_fence_update_txq(void *client_handle, u64 handle, u64 flags, u32 error)
{
	return -EINVAL;
}

static inline int msm_hw_fence_update_txq_error(void *client_handle, u64 handle, u32 error,
	u32 update_flags)
{
	return -EINVAL;
}

static inline int msm_hw_fence_trigger_signal(void *client_handle, u32 tx_client_id,
	u32 rx_client_id, u32 signal_id)
{
	return -EINVAL;
}

static inline int msm_hw_fence_register_error_cb(void *client_handle, msm_hw_fence_error_cb_t cb,
	void *data)
{
	return -EINVAL;
}

static inline int msm_hw_fence_deregister_error_cb(void *client_handle)
{
	return -EINVAL;
}
#endif

#if IS_ENABLED(CONFIG_DEBUG_FS) && IS_ENABLED(CONFIG_QTI_HW_FENCE)
/**
 * msm_hw_fence_dump_debug_data() - Dumps debug data information
 * @client_handle: Hw fence driver client handle returned during 'msm_hw_fence_register'.
 * @dump_flags: Flags to indicate which info to dump, see MSM_HW_FENCE_DBG_DUMP_** flags.
 * @dump_clients_mask: Optional bitmask to indicate along with the caller of the api, which other
 *                     clients to dump data from. E.g. a client like display might want to dump
 *                     info of any all other clients from which it can receive fences, like gfx.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_dump_debug_data(void *client_handle, u32 dump_flags, u32 dump_clients_mask);

/**
 * msm_hw_fence_dump_debug_data() - Dumps hw-fence information for dma-fence
 * @client_handle: Hw fence driver client handle returned during 'msm_hw_fence_register'.
 * @fence: dma_fence to dump hw-fence information
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int msm_hw_fence_dump_fence(void *client_handle, struct dma_fence *fence);

#else
static inline int msm_hw_fence_dump_debug_data(void *client_handle, u32 dump_flags,
	u32 dump_clients_mask)
{
	return -EINVAL;
}

static inline int msm_hw_fence_dump_fence(void *client_handle, struct dma_fence *fence)
{
	return -EINVAL;
}
#endif

#endif
