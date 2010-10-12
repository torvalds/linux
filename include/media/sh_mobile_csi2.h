/*
 * Driver header for the SH-Mobile MIPI CSI-2 unit
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SH_MIPI_CSI
#define SH_MIPI_CSI

enum sh_csi2_phy {
	SH_CSI2_PHY_MAIN,
	SH_CSI2_PHY_SUB,
};

enum sh_csi2_type {
	SH_CSI2C,
	SH_CSI2I,
};

#define SH_CSI2_CRC	(1 << 0)
#define SH_CSI2_ECC	(1 << 1)

struct platform_device;

struct sh_csi2_client_config {
	enum sh_csi2_phy phy;
	unsigned char lanes;		/* bitmask[3:0] */
	unsigned char channel;		/* 0..3 */
	struct platform_device *pdev;	/* client platform device */
};

struct sh_csi2_pdata {
	enum sh_csi2_type type;
	unsigned int flags;
	struct sh_csi2_client_config *clients;
	int num_clients;
};

struct device;
struct v4l2_device;

#endif
