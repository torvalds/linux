/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2013 Philipp Zabel, Pengutronix
 */
#ifndef PLATFORM_CODA_H
#define PLATFORM_CODA_H

struct device;

struct coda_platform_data {
	struct device *iram_dev;
};

#endif
