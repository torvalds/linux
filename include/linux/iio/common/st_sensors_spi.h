/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics sensors spi library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#ifndef ST_SENSORS_SPI_H
#define ST_SENSORS_SPI_H

#include <linux/spi/spi.h>
#include <linux/iio/common/st_sensors.h>

int st_sensors_spi_configure(struct iio_dev *indio_dev,
			     struct spi_device *spi);

#endif /* ST_SENSORS_SPI_H */
