/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Freescale DSPI controller driver
 *
 * Copyright (c) 2017 Angelo Dureghello <angelo@sysam.it>
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
