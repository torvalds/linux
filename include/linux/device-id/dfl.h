/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_DFL_H
#define LINUX_DEVICE_ID_DFL_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/*
 * DFL (Device Feature List)
 *
 * DFL defines a linked list of feature headers within the device MMIO space to
 * provide an extensible way of adding features. Software can walk through these
 * predefined data structures to enumerate features. It is now used in the FPGA.
 * See Documentation/fpga/dfl.rst for more information.
 *
 * The dfl bus type is introduced to match the individual feature devices (dfl
 * devices) for specific dfl drivers.
 */

/**
 * struct dfl_device_id -  dfl device identifier
 * @type: DFL FIU type of the device. See enum dfl_id_type.
 * @feature_id: feature identifier local to its DFL FIU type.
 * @driver_data: driver specific data.
 */
struct dfl_device_id {
	__u16 type;
	__u16 feature_id;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_DFL_H */
