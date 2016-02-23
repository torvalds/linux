/*
 * Copyright (C) 2013 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef PLATFORM_CODA_H
#define PLATFORM_CODA_H

struct device;

struct coda_platform_data {
	struct device *iram_dev;
};

#endif
