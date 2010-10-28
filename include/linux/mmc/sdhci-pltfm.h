/*
 * Platform data declarations for the sdhci-pltfm driver.
 *
 * Copyright (c) 2010 MontaVista Software, LLC.
 *
 * Author: Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _SDHCI_PLTFM_H
#define _SDHCI_PLTFM_H

struct sdhci_ops;
struct sdhci_host;

/**
 * struct sdhci_pltfm_data - SDHCI platform-specific information & hooks
 * @ops: optional pointer to the platform-provided SDHCI ops
 * @quirks: optional SDHCI quirks
 * @init: optional hook that is called during device probe, before the
 *        driver tries to access any SDHCI registers
 * @exit: optional hook that is called during device removal
 */
struct sdhci_pltfm_data {
	struct sdhci_ops *ops;
	unsigned int quirks;
	int (*init)(struct sdhci_host *host, struct sdhci_pltfm_data *pdata);
	void (*exit)(struct sdhci_host *host);
};

#endif /* _SDHCI_PLTFM_H */
