/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022, Intel Corporation. All rights reserved.
 */
#ifndef _LINUX_MEI_AUX_H
#define _LINUX_MEI_AUX_H

#include <linux/auxiliary_bus.h>

struct mei_aux_device {
	struct auxiliary_device aux_dev;
	int irq;
	struct resource bar;
};

#define auxiliary_dev_to_mei_aux_dev(auxiliary_dev) \
	container_of(auxiliary_dev, struct mei_aux_device, aux_dev)

#endif /* _LINUX_MEI_AUX_H */
