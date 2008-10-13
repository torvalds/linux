/*
 * OpenFirmware SPI support routines
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 *
 * Support routines for deriving SPI device attachments from the device
 * tree.
 */

#ifndef __LINUX_OF_SPI_H
#define __LINUX_OF_SPI_H

#include <linux/of.h>
#include <linux/spi/spi.h>

extern void of_register_spi_devices(struct spi_master *master,
				    struct device_node *np);

#endif /* __LINUX_OF_SPI */
