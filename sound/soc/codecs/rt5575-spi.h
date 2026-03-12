/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * rt5575-spi.h  --  ALC5575 SPI driver
 *
 * Copyright(c) 2025 Realtek Semiconductor Corp.
 *
 */

#ifndef __RT5575_SPI_H__
#define __RT5575_SPI_H__

#if IS_ENABLED(CONFIG_SND_SOC_RT5575_SPI)
struct spi_device *rt5575_spi_get_device(struct device *dev);
int rt5575_spi_fw_load(struct spi_device *spi);
#else
static inline struct spi_device *rt5575_spi_get_device(struct device *dev)
{
	return NULL;
}

static inline int rt5575_spi_fw_load(struct spi_device *spi)
{
	return -EINVAL;
}
#endif

#endif /* __RT5575_SPI_H__ */
