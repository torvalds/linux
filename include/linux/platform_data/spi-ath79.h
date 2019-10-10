/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Platform data definition for Atheros AR71XX/AR724X/AR913X SPI controller
 *
 *  Copyright (C) 2008-2010 Gabor Juhos <juhosg@openwrt.org>
 */

#ifndef _ATH79_SPI_PLATFORM_H
#define _ATH79_SPI_PLATFORM_H

struct ath79_spi_platform_data {
	unsigned	bus_num;
	unsigned	num_chipselect;
};

#endif /* _ATH79_SPI_PLATFORM_H */
