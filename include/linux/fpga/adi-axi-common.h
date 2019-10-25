/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Analog Devices AXI common registers & definitions
 *
 * Copyright 2019 Analog Devices Inc.
 *
 * https://wiki.analog.com/resources/fpga/docs/axi_ip
 * https://wiki.analog.com/resources/fpga/docs/hdl/regmap
 */

#ifndef ADI_AXI_COMMON_H_
#define ADI_AXI_COMMON_H_

#define	ADI_AXI_REG_VERSION			0x0000

#define ADI_AXI_PCORE_VER(major, minor, patch)	\
	(((major) << 16) | ((minor) << 8) | (patch))

#endif /* ADI_AXI_COMMON_H_ */
