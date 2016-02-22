/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ARCH_ARM_MACH_MSM_RPM_NOTIF_H
#define __ARCH_ARM_MACH_MSM_RPM_NOTIF_H

struct msm_rpm_notifier_data {
	uint32_t rsc_type;
	uint32_t rsc_id;
	uint32_t key;
	uint32_t size;
	uint8_t *value;
};
/**
 * msm_rpm_register_notifier - Register for sleep set notifications
 *
 * @nb - notifier block to register
 *
 * return 0 on success, errno on failure.
 */
int msm_rpm_register_notifier(struct notifier_block *nb);

/**
 * msm_rpm_unregister_notifier - Unregister previously registered notifications
 *
 * @nb - notifier block to unregister
 *
 * return 0 on success, errno on failure.
 */
int msm_rpm_unregister_notifier(struct notifier_block *nb);

/**
 * msm_rpm_enter_sleep - Notify RPM driver to prepare for entering sleep
 *
 * @bool - flag to enable print contents of sleep buffer.
 * @cpumask - cpumask of next wakeup cpu
 *
 * return 0 on success errno on failure.
 */
int msm_rpm_enter_sleep(bool print, const struct cpumask *cpumask);

/**
 * msm_rpm_exit_sleep - Notify RPM driver about resuming from power collapse
 */
void msm_rpm_exit_sleep(void);

/**
 * msm_rpm_waiting_for_ack - Indicate if there is RPM message
 *				pending acknowledgement.
 * returns true for pending messages and false otherwise
 */
bool msm_rpm_waiting_for_ack(void);

#endif /*__ARCH_ARM_MACH_MSM_RPM_NOTIF_H */
