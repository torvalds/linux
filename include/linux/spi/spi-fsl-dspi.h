/*
 * Freescale DSPI controller driver
 *
 * Copyright (c) 2017 Angelo Dureghello <angelo@sysam.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef SPI_FSL_DSPI_HEADER_H
#define SPI_FSL_DSPI_HEADER_H

/**
 * struct fsl_dspi_platform_data - platform data for the Freescale DSPI driver
 * @bus_num: board specific identifier for this DSPI driver.
 * @cs_num: number of chip selects supported by this DSPI driver.
 */
struct fsl_dspi_platform_data {
	u32 cs_num;
	u32 bus_num;
	u32 sck_cs_delay;
	u32 cs_sck_delay;
};

#endif /* SPI_FSL_DSPI_HEADER_H */
