/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023, STMicroelectronics - All Rights Reserved
 */

#ifndef STM32_FIREWALL_DEVICE_H
#define STM32_FIREWALL_DEVICE_H

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define STM32_FIREWALL_MAX_EXTRA_ARGS		5

/* Opaque reference to stm32_firewall_controller */
struct stm32_firewall_controller;

/**
 * struct stm32_firewall - Information on a device's firewall. Each device can have more than one
 *			   firewall.
 *
 * @firewall_ctrl:		Pointer referencing a firewall controller of the device. It is
 *				opaque so a device cannot manipulate the controller's ops or access
 *				the controller's data
 * @extra_args:			Extra arguments that are implementation dependent
 * @entry:			Name of the firewall entry
 * @extra_args_size:		Number of extra arguments
 * @firewall_id:		Firewall ID associated the device for this firewall controller
 */
struct stm32_firewall {
	struct stm32_firewall_controller *firewall_ctrl;
	u32 extra_args[STM32_FIREWALL_MAX_EXTRA_ARGS];
	const char *entry;
	size_t extra_args_size;
	u32 firewall_id;
};

#if IS_ENABLED(CONFIG_STM32_FIREWALL)
/**
 * stm32_firewall_get_firewall - Get the firewall(s) associated to given device.
 *				 The firewall controller reference is always the first argument
 *				 of each of the access-controller property entries.
 *				 The firewall ID is always the second argument of each of the
 *				 access-controller  property entries.
 *				 If there's no argument linked to the phandle, then the firewall ID
 *				 field is set to U32_MAX, which is an invalid ID.
 *
 * @np:				Device node to parse
 * @firewall:			Array of firewall references
 * @nb_firewall:		Number of firewall references to get. Must be at least 1.
 *
 * Returns 0 on success, -ENODEV if there's no match with a firewall controller or appropriate errno
 * code if error occurred.
 */
int stm32_firewall_get_firewall(struct device_node *np, struct stm32_firewall *firewall,
				unsigned int nb_firewall);

/**
 * stm32_firewall_grant_access - Request firewall access rights and grant access.
 *
 * @firewall:			Firewall reference containing the ID to check against its firewall
 *				controller
 *
 * Returns 0 if access is granted, -EACCES if access is denied, -ENODEV if firewall is null or
 * appropriate errno code if error occurred
 */
int stm32_firewall_grant_access(struct stm32_firewall *firewall);

/**
 * stm32_firewall_release_access - Release access granted from a call to
 *				   stm32_firewall_grant_access().
 *
 * @firewall:			Firewall reference containing the ID to check against its firewall
 *				controller
 */
void stm32_firewall_release_access(struct stm32_firewall *firewall);

/**
 * stm32_firewall_grant_access_by_id - Request firewall access rights of a given device
 *				       based on a specific firewall ID
 *
 * Warnings:
 * There is no way to ensure that the given ID will correspond to the firewall referenced in the
 * device node if the ID did not come from stm32_firewall_get_firewall(). In that case, this
 * function must be used with caution.
 * This function should be used for subsystem resources that do not have the same firewall ID
 * as their parent.
 * U32_MAX is an invalid ID.
 *
 * @firewall:			Firewall reference containing the firewall controller
 * @subsystem_id:		Firewall ID of the subsystem resource
 *
 * Returns 0 if access is granted, -EACCES if access is denied, -ENODEV if firewall is null or
 * appropriate errno code if error occurred
 */
int stm32_firewall_grant_access_by_id(struct stm32_firewall *firewall, u32 subsystem_id);

/**
 * stm32_firewall_release_access_by_id - Release access granted from a call to
 *					 stm32_firewall_grant_access_by_id().
 *
 * Warnings:
 * There is no way to ensure that the given ID will correspond to the firewall referenced in the
 * device node if the ID did not come from stm32_firewall_get_firewall(). In that case, this
 * function must be used with caution.
 * This function should be used for subsystem resources that do not have the same firewall ID
 * as their parent.
 * U32_MAX is an invalid ID.
 *
 * @firewall:			Firewall reference containing the firewall controller
 * @subsystem_id:		Firewall ID of the subsystem resource
 */
void stm32_firewall_release_access_by_id(struct stm32_firewall *firewall, u32 subsystem_id);

#else /* CONFIG_STM32_FIREWALL */

static inline int stm32_firewall_get_firewall(struct device_node *np,
					      struct stm32_firewall *firewall,
					      unsigned int nb_firewall)
{
	return -ENODEV;
}

static inline int stm32_firewall_grant_access(struct stm32_firewall *firewall)
{
	return -ENODEV;
}

static inline void stm32_firewall_release_access(struct stm32_firewall *firewall)
{
}

static inline int stm32_firewall_grant_access_by_id(struct stm32_firewall *firewall,
						    u32 subsystem_id)
{
	return -ENODEV;
}

static inline void stm32_firewall_release_access_by_id(struct stm32_firewall *firewall,
						       u32 subsystem_id)
{
}

#endif /* CONFIG_STM32_FIREWALL */
#endif /* STM32_FIREWALL_DEVICE_H */
