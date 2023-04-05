/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/platform_device.h>

#ifndef __ARCH_ARM_MACH_MSM_RPM_SMD_H
#define __ARCH_ARM_MACH_MSM_RPM_SMD_H

/**
 * enum msm_rpm_set - RPM enumerations for sleep/active set
 * %MSM_RPM_CTX_SET_0: Set resource parameters for active mode.
 * %MSM_RPM_CTX_SET_SLEEP: Set resource parameters for sleep.
 */
enum msm_rpm_set {
	MSM_RPM_CTX_ACTIVE_SET,
	MSM_RPM_CTX_SLEEP_SET,
};

struct msm_rpm_request;

struct msm_rpm_kvp {
	uint32_t key;
	uint32_t length;
	uint8_t *data;
};
#if IS_ENABLED(CONFIG_MSM_RPM_SMD)
/**
 * msm_rpm_request() - Creates a parent element to identify the
 * resource on the RPM, that stores the KVPs for different fields modified
 * for a hardware resource
 *
 * @set: if the device is setting the active/sleep set parameter
 * for the resource
 * @rsc_type: unsigned 32 bit integer that identifies the type of the resource
 * @rsc_id: unsigned 32 bit that uniquely identifies a resource within a type
 * @num_elements: number of KVPs pairs associated with the resource
 *
 * returns pointer to a msm_rpm_request on success, NULL on error
 */
struct msm_rpm_request *msm_rpm_create_request(
		enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, int num_elements);

/**
 * msm_rpm_request_noirq() - Creates a parent element to identify the
 * resource on the RPM, that stores the KVPs for different fields modified
 * for a hardware resource. This function is similar to msm_rpm_create_request
 * except that it has to be called with interrupts masked.
 *
 * @set: if the device is setting the active/sleep set parameter
 * for the resource
 * @rsc_type: unsigned 32 bit integer that identifies the type of the resource
 * @rsc_id: unsigned 32 bit that uniquely identifies a resource within a type
 * @num_elements: number of KVPs pairs associated with the resource
 *
 * returns pointer to a msm_rpm_request on success, NULL on error
 */
struct msm_rpm_request *msm_rpm_create_request_noirq(
		enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, int num_elements);

/**
 * msm_rpm_add_kvp_data() - Adds a Key value pair to a existing RPM resource.
 *
 * @handle: RPM resource handle to which the data should be appended
 * @key:  unsigned integer identify the parameter modified
 * @data: byte array that contains the value corresponding to key.
 * @size:   size of data in bytes.
 *
 * returns 0 on success or errno
 */
int msm_rpm_add_kvp_data(struct msm_rpm_request *handle,
		uint32_t key, const uint8_t *data, int size);

/**
 * msm_rpm_add_kvp_data_noirq() - Adds a Key value pair to a existing RPM
 * resource. This function is similar to msm_rpm_add_kvp_data except that it
 * has to be called with interrupts masked.
 *
 * @handle: RPM resource handle to which the data should be appended
 * @key:  unsigned integer identify the parameter modified
 * @data: byte array that contains the value corresponding to key.
 * @size:   size of data in bytes.
 *
 * returns 0 on success or errno
 */
int msm_rpm_add_kvp_data_noirq(struct msm_rpm_request *handle,
		uint32_t key, const uint8_t *data, int size);

/** msm_rpm_free_request() - clean up the RPM request handle created with
 * msm_rpm_create_request
 *
 * @handle: RPM resource handle to be cleared.
 */

void msm_rpm_free_request(struct msm_rpm_request *handle);

/**
 * msm_rpm_send_request() - Send the RPM messages using SMD. The function
 * assigns a message id before sending the data out to the RPM. RPM hardware
 * uses the message id to acknowledge the messages.
 *
 * @handle: pointer to the msm_rpm_request for the resource being modified.
 *
 * returns non-zero message id on success and zero on a failed transaction.
 * The drivers use message id to wait for ACK from RPM.
 */
int msm_rpm_send_request(struct msm_rpm_request *handle);

/**
 * msm_rpm_send_request_noack() - Send the RPM messages using SMD. The function
 * assigns a message id before sending the data out to the RPM. RPM hardware
 * uses the message id to acknowledge the messages, but this API does not wait
 * on the ACK for this message id and it does not add the message id to the wait
 * list.
 *
 * @handle: pointer to the msm_rpm_request for the resource being modified.
 *
 * returns NULL on success and PTR_ERR on a failed transaction.
 */
void *msm_rpm_send_request_noack(struct msm_rpm_request *handle);

/**
 * msm_rpm_send_request_noirq() - Send the RPM messages using SMD. The
 * function assigns a message id before sending the data out to the RPM.
 * RPM hardware uses the message id to acknowledge the messages. This function
 * is similar to msm_rpm_send_request except that it has to be called with
 * interrupts masked.
 *
 * @handle: pointer to the msm_rpm_request for the resource being modified.
 *
 * returns non-zero message id on success and zero on a failed transaction.
 * The drivers use message id to wait for ACK from RPM.
 */
int msm_rpm_send_request_noirq(struct msm_rpm_request *handle);

/**
 * msm_rpm_wait_for_ack() - A blocking call that waits for acknowledgment of
 * a message from RPM.
 *
 * @msg_id: the return from msm_rpm_send_requests
 *
 * returns 0 on success or errno
 */
int msm_rpm_wait_for_ack(uint32_t msg_id);

/**
 * msm_rpm_wait_for_ack_noirq() - A blocking call that waits for acknowledgment
 * of a message from RPM. This function is similar to msm_rpm_wait_for_ack
 * except that it has to be called with interrupts masked.
 *
 * @msg_id: the return from msm_rpm_send_request
 *
 * returns 0 on success or errno
 */
int msm_rpm_wait_for_ack_noirq(uint32_t msg_id);

/**
 * msm_rpm_send_message() -Wrapper function for clients to send data given an
 * array of key value pairs.
 *
 * @set: if the device is setting the active/sleep set parameter
 * for the resource
 * @rsc_type: unsigned 32 bit integer that identifies the type of the resource
 * @rsc_id: unsigned 32 bit that uniquely identifies a resource within a type
 * @kvp: array of KVP data.
 * @nelem: number of KVPs pairs associated with the message.
 *
 * returns  0 on success and errno on failure.
 */
int msm_rpm_send_message(enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems);

/**
 * msm_rpm_send_message_noack() -Wrapper function for clients to send data
 * given an array of key value pairs without waiting for ack.
 *
 * @set: if the device is setting the active/sleep set parameter
 * for the resource
 * @rsc_type: unsigned 32 bit integer that identifies the type of the resource
 * @rsc_id: unsigned 32 bit that uniquely identifies a resource within a type
 * @kvp: array of KVP data.
 * @nelem: number of KVPs pairs associated with the message.
 *
 * returns  NULL on success and PTR_ERR(errno) on failure.
 */
void *msm_rpm_send_message_noack(enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems);

/**
 * msm_rpm_send_message_noirq() -Wrapper function for clients to send data
 * given an array of key value pairs. This function is similar to the
 * msm_rpm_send_message() except that it has to be called with interrupts
 * disabled. Clients should choose the irq version when possible for system
 * performance.
 *
 * @set: if the device is setting the active/sleep set parameter
 * for the resource
 * @rsc_type: unsigned 32 bit integer that identifies the type of the resource
 * @rsc_id: unsigned 32 bit that uniquely identifies a resource within a type
 * @kvp: array of KVP data.
 * @nelem: number of KVPs pairs associated with the message.
 *
 * returns  0 on success and errno on failure.
 */
int msm_rpm_send_message_noirq(enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems);

/**
 * msm_rpm_driver_init() - Initialization function that registers for a
 * rpm platform driver.
 *
 * returns 0 on success.
 */
int __init msm_rpm_driver_init(void);

#else

static inline struct msm_rpm_request *msm_rpm_create_request(
		enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, int num_elements)
{
	return NULL;
}

static inline struct msm_rpm_request *msm_rpm_create_request_noirq(
		enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, int num_elements)
{
	return NULL;

}
static inline uint32_t msm_rpm_add_kvp_data(struct msm_rpm_request *handle,
		uint32_t key, const uint8_t *data, int count)
{
	return 0;
}
static inline uint32_t msm_rpm_add_kvp_data_noirq(
		struct msm_rpm_request *handle, uint32_t key,
		const uint8_t *data, int count)
{
	return 0;
}

static inline void msm_rpm_free_request(struct msm_rpm_request *handle)
{
}

static inline int msm_rpm_send_request(struct msm_rpm_request *handle)
{
	return 0;
}

static inline int msm_rpm_send_request_noirq(struct msm_rpm_request *handle)
{
	return 0;

}

static inline void *msm_rpm_send_request_noack(struct msm_rpm_request *handle)
{
	return NULL;
}

static inline int msm_rpm_send_message(enum msm_rpm_set set, uint32_t rsc_type,
		uint32_t rsc_id, struct msm_rpm_kvp *kvp, int nelems)
{
	return 0;
}

static inline int msm_rpm_send_message_noirq(enum msm_rpm_set set,
		uint32_t rsc_type, uint32_t rsc_id, struct msm_rpm_kvp *kvp,
		int nelems)
{
	return 0;
}

static inline void *msm_rpm_send_message_noack(enum msm_rpm_set set,
		uint32_t rsc_type, uint32_t rsc_id, struct msm_rpm_kvp *kvp,
		int nelems)
{
	return NULL;
}

static inline int msm_rpm_wait_for_ack(uint32_t msg_id)
{
	return 0;

}
static inline int msm_rpm_wait_for_ack_noirq(uint32_t msg_id)
{
	return 0;
}

static inline int __init msm_rpm_driver_init(void)
{
	return 0;
}
#endif

#endif /*__ARCH_ARM_MACH_MSM_RPM_SMD_H*/
