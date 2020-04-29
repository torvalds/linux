/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) ST Ericsson SA 2011
 *
 * STE Ux500 Watchdog platform data
 */
#ifndef __UX500_WDT_H
#define __UX500_WDT_H

/**
 * struct ux500_wdt_data
 */
struct ux500_wdt_data {
	unsigned int timeout;
	bool has_28_bits_resolution;
};

#endif /* __UX500_WDT_H */
