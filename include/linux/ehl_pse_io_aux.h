/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Elkhart Lake PSE I/O Auxiliary Device
 *
 * Copyright (c) 2025 Intel Corporation.
 *
 * Author: Raag Jadav <raag.jadav@intel.com>
 */

#ifndef _EHL_PSE_IO_AUX_H_
#define _EHL_PSE_IO_AUX_H_

#include <linux/ioport.h>

#define EHL_PSE_IO_NAME		"ehl_pse_io"
#define EHL_PSE_GPIO_NAME	"gpio"
#define EHL_PSE_TIO_NAME	"pps_tio"

struct ehl_pse_io_data {
	struct resource mem;
	int irq;
};

#endif /* _EHL_PSE_IO_AUX_H_ */
