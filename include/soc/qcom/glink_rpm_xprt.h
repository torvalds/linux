/* Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _SOC_QCOM_GLINK_RPM_XPRT_H_
#define _SOC_QCOM_GLINK_RPM_XPRT_H_

#include <linux/types.h>

#ifdef CONFIG_MSM_GLINK

/**
 * glink_rpm_rx_poll() - Poll and receive any available events
 * @handle:	Channel handle in which this operation is performed.
 *
 * This function is used to poll and receive events and packets while the
 * receive interrupt from RPM is disabled.
 *
 * Note that even if a return value > 0 is returned indicating that some events
 * were processed, clients should only use the notification functions passed
 * into glink_open() to determine if an entire packet has been received since
 * some events may be internal details that are not visible to clients.
 *
 * Return: 0 for no packets available; > 0 for events available; standard
 * Linux error codes on failure.
 */
int glink_rpm_rx_poll(void *handle);

/**
 * glink_rpm_mask_rx_interrupt() - Mask or unmask the RPM receive interrupt
 * @handle:	Channel handle in which this operation is performed.
 * @mask:	Flag to mask or unmask the interrupt.
 * @pstruct:	Pointer to any platform specific data.
 *
 * This function is used to mask or unmask the receive interrupt from RPM.
 * "mask" set to true indicates masking the interrupt and when set to false
 * indicates unmasking the interrupt.
 *
 * Return: 0 on success, standard Linux error codes on failure.
 */
int glink_rpm_mask_rx_interrupt(void *handle, bool mask, void *pstruct);

/**
 * glink_wait_link_down() - Return whether read/write indices in FIFO are all 0.
 * @handle:	Channel handle in which this operation is performed.
 *
 * This function returns the status of the read/write indices in the FIFO.
 *
 * Return: 1 if the indices are all 0, 0 otherwise.
 */
int glink_wait_link_down(void *handle);

#else
static inline int glink_rpm_rx_poll(void *handle)
{
	return -ENODEV;
}

static inline int glink_rpm_mask_rx_interrupt(void *handle, bool mask,
		void *pstruct)
{
	return -ENODEV;
}
static inline int glink_wait_link_down(void *handle)
{
	return -ENODEV;
}

#endif /* CONFIG_MSM_GLINK */

#endif /* _SOC_QCOM_GLINK_RPM_XPRT_H_ */
