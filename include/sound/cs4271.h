/*
 * Definitions for CS4271 ASoC codec driver
 *
 * Copyright (c) 2010 Alexander Sverdlin <subaparts@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __CS4271_H
#define __CS4271_H

struct cs4271_platform_data {
	int gpio_nreset;	/* GPIO driving Reset pin, if any */
};

#endif /* __CS4271_H */
