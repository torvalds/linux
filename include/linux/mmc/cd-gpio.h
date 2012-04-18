/*
 * Generic GPIO card-detect helper header
 *
 * Copyright (C) 2011, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MMC_CD_GPIO_H
#define MMC_CD_GPIO_H

struct mmc_host;
int mmc_cd_gpio_request(struct mmc_host *host, unsigned int gpio);
void mmc_cd_gpio_free(struct mmc_host *host);

#endif
