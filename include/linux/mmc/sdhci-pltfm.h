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

/**
 * struct sdhci_pltfm_data - SDHCI platform-specific information & hooks
 * @ops: optional pointer to the platform-provided SDHCI ops
 * @quirks: optional SDHCI quirks
 */
struct sdhci_pltfm_data {
	struct sdhci_ops *ops;
	unsigned int quirks;
};

#endif /* _SDHCI_PLTFM_H */
