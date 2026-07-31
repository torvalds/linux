/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_MHI_H
#define LINUX_DEVICE_ID_MHI_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

#define MHI_DEVICE_MODALIAS_FMT "mhi:%s"
#define MHI_NAME_SIZE 32

#define MHI_EP_DEVICE_MODALIAS_FMT "mhi_ep:%s"

/**
 * struct mhi_device_id - MHI device identification
 * @chan: MHI channel name
 * @driver_data: driver data;
 */
struct mhi_device_id {
	const char chan[MHI_NAME_SIZE];
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_MHI_H */
