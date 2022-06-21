/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 */

#ifndef __MSM_MMRM_H__
#define __MSM_MMRM_H__

#include <linux/types.h>

#define MMRM_CLK_CLIENT_NAME_SIZE  128

/*
 * MMRM Client data flags
 * MMRM_CLIENT_DATA_FLAG_RESERVE_ONLY : Clients can use this flag
 * to reserve the actual required resource to MMRM (as some clients
 * have a differnt path to validate the resource availability).
 * With this flag set for mmrm_set_value, MMRM will skip setting rate
 * to clk driver & only check if requested clk resource is available.
 * Client need to call mmrm_set_value again (without this flag) to complete
 * the rate setting to clk driver. If MMRM driver will not receive
 * set_value call from client within stipulated time eg: 100ms,
 * resource will be returned back to pool.
 */
#define MMRM_CLIENT_DATA_FLAG_RESERVE_ONLY  0x0001

/**
 * mmrm_client_domain : MMRM client domain
 * Clients need to configure this in mmrm_clk_client_desc
 */
enum mmrm_client_domain {
	MMRM_CLIENT_DOMAIN_CAMERA = 0x1,
	MMRM_CLIENT_DOMAIN_CVP = 0x2,
	MMRM_CLIENT_DOMAIN_DISPLAY = 0x3,
	MMRM_CLIENT_DOMAIN_VIDEO = 0x4,
};

/**
 * mmrm_client_type : MMRM Client type
 */
enum mmrm_client_type {
	MMRM_CLIENT_CLOCK,
};

/**
 * mmrm_client_priority: Allowed mmrm client priorities configured by client.
 * These priorities are used by mmrm clients when regitering client.
 * Clients registered with low priority can be throttled to a lower
 * value (if needed to accommodate high priority client
 * or if current set value exceeds peak threshold).
 * High priority clients cannot be throttled & if set value cannot
 * be accommodated (even after throttling low priority clients),
 * mmrm driver will return error -EDQUOT.
 * Order of throttling registered low priority clients will be
 * dependent on chipset and will be decided by MMRM driver.
 */
enum mmrm_client_priority {
	MMRM_CLIENT_PRIOR_HIGH = 0x1,
	MMRM_CLIENT_PRIOR_LOW  = 0x2
};

/*
 * mmrm_cb_type: Callback type for the mmrm client notifier
 * MMRM_CLIENT_RESOURCE_VALUE_CHANGE: Indicate a change in resource value
 */
enum mmrm_cb_type {
	MMRM_CLIENT_RESOURCE_VALUE_CHANGE = 0x1,
};

/**
 * mmrm_res_val_chng: Change in resource value for mmrm cb type
 *                    MMRM_CLIENT_RESOURCE_VALUE_CHANGE
 * @old_val: Previously configured resource value by client
 * @new_val: New resource value for this client (throttled value) by mmrm
 */
struct mmrm_res_val_chng {
	unsigned long old_val;
	unsigned long new_val;
};

/**
 * mmrm_client_notifier_data: Callback used to pass resource data to client
 * @cb_type: Type of notifier callback (eg: MMRM_CLIENT_RESOURCE_VALUE_CHANGE)
 * @cb_data: Data corresponding to notifier cb type
 * @pvt_data: Private data provided by client while registering
 */
struct mmrm_client_notifier_data {
	enum mmrm_cb_type cb_type;
	union {
		struct mmrm_res_val_chng val_chng;
	} cb_data;
	void *pvt_data;
};

/**
 * mmrm_client_res_value - Resource values for MMRM client
 * For mmrm_client_set_value_in_range, client will configure min & cur
 * For mmrm_client_get_value, mmrm driver will return all 3 values.
 * @min: Min resource value of this client
 * @cur: Current resource value of this client
 * @max: Max possible resource value of this client
 * min and cur are previously set by client, while max is computed by mmrm
 */
struct mmrm_client_res_value {
	unsigned long min;
	unsigned long cur;
	unsigned long max;
};

/**
 * mmrm_client : MMRM client structure allocated & returned to client
 * in register call. This will ensure that right client is configuring
 * set/get value for its resources.
 * @client_type: Type of client resource
 * @client_uid: Unique id of the resource created by MMRM
 */
struct mmrm_client {
	enum mmrm_client_type client_type;
	u32 client_uid;
};

/**
 * mmrm_clk_client_desc : MMRM clock client descriptor used
 *                        for registering client
 * @client_domain: Client provides MMRM_CLIENT_DOMAIN_XXXX
 * @client_id: Client provides CLK_SRC_XXXX id
 * @name     : Client name
 * @clk      : Pointer to clock struct
 */
struct mmrm_clk_client_desc {
	u32 client_domain;
	u32 client_id;
	const char name[MMRM_CLK_CLIENT_NAME_SIZE];
	struct clk *clk;
};

/**
 * mmrm_client_desc : MMRM client descriptor used for registering client
 * @client_type : Type of client (eg: clock)
 * @client_info : Description of client (eg: clock)
 * @priority    : Client priority
 * @pvt_data    : Client data to be used by clients when mmrm driver
 *                calls notifier function after throttling.
 * @notifier_callback_fn : Callback function used by mmrm to notify clients
 *                         asynchronously. These callbacks are allowed only
 *                         when client is actively registered.
 */
struct mmrm_client_desc {
	enum mmrm_client_type client_type;
	union {
		struct mmrm_clk_client_desc desc;
	} client_info;
	enum mmrm_client_priority priority;
	void *pvt_data;
	int (*notifier_callback_fn)(
		struct mmrm_client_notifier_data *notifier_data);
};

/**
 * mmrm_client_data : Additional data MMRM client data needed to configure
 * set value.
 * @num_hw_blocks: Client hw blocks enabled for resource allocation estimation
 *                 Default 1 for each client
 * @flags: Client flags used to provide additional client info
 *         Refer flags MMRM_CLIENT_DATA_FLAG_XXXX
 */
struct mmrm_client_data {
	u32 num_hw_blocks;
	u32 flags;
};

#if IS_ENABLED(CONFIG_MSM_MMRM)
/**
 * mmrm_client_check_scaling_supported - check if mmrm client type (clk, bw)
 * scaling is supported for a client domain (camera, cvp, video, display)
 * @client_type: Type of mmrm client (clk, bw)
 * @client_domain: client domain (camera, cvp, display, video)
 *
 * Returns true : mmrm scaling is supported for a client type & domain
 *         false: mmrm scaling is not supported for a client type & domain
 */
bool mmrm_client_check_scaling_supported(enum mmrm_client_type client_type,
	u32 client_domain);

/**
 * mmrm_client_register - register an mmrm client
 * This call not configure any rate, use set rate to configure desired rate.
 * @desc: Client description
 *
 * Returns pointer to mmrm_client for success or NULL during failure
 */
struct mmrm_client *mmrm_client_register(struct mmrm_client_desc *desc);

/**
 * mmrm_client_deregister - de-register a previously registered mmrm client
 * @client: Pointer to mmrm client, returned after client registration
 *
 * Returns 0 on success or negative errno (-EINVAL)
 */
int mmrm_client_deregister(struct mmrm_client *client);

/**
 * mmrm_client_set_value - set mmrm client resource value to the given value.
 * MMRM driver will check client priority & configure this value accordingly.
 * For high priority clients, mmrm driver will try to set the value & will
 * throttle low priority clients to accommodate the request. If set rate value
 * cannot be accommodated, then mmrm driver will return errno (-EDQUOT).
 * For low priority clients, mmrm driver will return error if value cannot
 * be accommodated.
 * For clock resource, this will replace clk_set_rate() in client driver.
 *
 * @client: Pointer to mmrm client returned after client registration
 * @client_data : Additional client data provided by mmrm client for set value
 * @val: Desired resource value
 *
 * Returns 0 on success or negative errno (-EDQUOT / -EINVAL)
 */
int mmrm_client_set_value(struct mmrm_client *client,
	struct mmrm_client_data *client_data, unsigned long val);

/**
 * mmrm_client_set_value_in_range - set mmrm client resource value to the
 * allowed value in the given range.
 * This api is primarily for low priority clients, if current value (cur)
 * cannot be configured then MMRM driver will identify a suitable lower value
 * using intermediate corners for the client upto minmum value (min).
 * In case set value cannot be accommodated mmrm driver will return -EDQUOT.
 * MMRM will store the min value if this client needs to be throttled.
 * @client: mmrm client returned after client registration
 * @client_data : additional client data provided by mmrm client for set value
 * @val : setting resource value triplet
 *
 * Returns 0 on success or negative errno (-EDQUOT/ -EINVAL)
 */
int mmrm_client_set_value_in_range(struct mmrm_client *client,
	struct mmrm_client_data *client_data,
	struct mmrm_client_res_value *val);

/**
 * mmrm_client_get_value - get mmrm client resource values {min, cur, max}
 * @client: mmrm client returned after client registration
 * @val: resource value triplet set by mmrm
 *
 * Returns 0 on success or negative errno (-ENOTSUPP)
 */
int mmrm_client_get_value(struct mmrm_client *client,
	struct mmrm_client_res_value *val);

#else
static inline bool mmrm_client_check_scaling_supported(
	enum mmrm_client_type client_type, u32 client_domain)
{
	return false;
}

static inline struct mmrm_client *mmrm_client_register(
	struct mmrm_client_desc *desc)
{
	return NULL;
}

static inline int mmrm_client_deregister(struct mmrm_client *client)
{
	return 0;
}

static inline int mmrm_client_set_value(struct mmrm_client *client,
	struct mmrm_client_data *client_data, unsigned long val)
{
	return -EINVAL;
}

static inline int mmrm_client_set_value_in_range(struct mmrm_client *client,
	struct mmrm_client_data *client_data,
	struct mmrm_client_res_value *val)
{
	return -EINVAL;
}

static inline int mmrm_client_get_value(struct mmrm_client *client,
	struct mmrm_client_res_value *val)
{
	return 0;
}

#endif

#endif
