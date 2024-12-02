/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 */
#ifndef _LINUX_MEI_AUX_H
#define _LINUX_MEI_AUX_H

#include <linux/auxiliary_bus.h>

/**
 * struct mei_aux_device - mei auxiliary device
 * @aux_dev: - auxiliary device object
 * @irq: interrupt driving the mei auxiliary device
 * @bar: mmio resource bar reserved to mei auxiliary device
 * @ext_op_mem: resource for extend operational memory
 *              used in graphics PXP mode.
 * @slow_firmware: The device has slow underlying firmware.
 *                 Such firmware will require to use larger operation timeouts.
 */
struct mei_aux_device {
	struct auxiliary_device aux_dev;
	int irq;
	struct resource bar;
	struct resource ext_op_mem;
	bool slow_firmware;
};

#define auxiliary_dev_to_mei_aux_dev(auxiliary_dev) \
	container_of(auxiliary_dev, struct mei_aux_device, aux_dev)

#endif /* _LINUX_MEI_AUX_H */
