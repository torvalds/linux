/*
 * Intel PCH/PCU SPI flash driver.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef INTEL_SPI_PDATA_H
#define INTEL_SPI_PDATA_H

enum intel_spi_type {
	INTEL_SPI_BYT = 1,
	INTEL_SPI_LPT,
	INTEL_SPI_BXT,
};

/**
 * struct intel_spi_boardinfo - Board specific data for Intel SPI driver
 * @type: Type which this controller is compatible with
 * @writeable: The chip is writeable
 */
struct intel_spi_boardinfo {
	enum intel_spi_type type;
	bool writeable;
};

#endif /* INTEL_SPI_PDATA_H */
