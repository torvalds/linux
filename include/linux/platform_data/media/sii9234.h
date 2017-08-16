/*
 * Driver header for SII9234 MHL converter chip.
 *
 * Copyright (c) 2011 Samsung Electronics, Co. Ltd
 * Contact: Tomasz Stanislawski <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef SII9234_H
#define SII9234_H

/**
 * @gpio_n_reset: GPIO driving nRESET pin
 */

struct sii9234_platform_data {
	int gpio_n_reset;
};

#endif /* SII9234_H */
