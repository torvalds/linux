/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI ADS7828 A/D Converter platform data definition
 *
 * Copyright (c) 2012 Savoir-faire Linux Inc.
 *          Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * For further information, see the Documentation/hwmon/ads7828.rst file.
 */

#ifndef _PDATA_ADS7828_H
#define _PDATA_ADS7828_H

/**
 * struct ads7828_platform_data - optional ADS7828 connectivity info
 * @diff_input:		Differential input mode.
 * @ext_vref:		Use an external voltage reference.
 * @vref_mv:		Voltage reference value, if external.
 */
struct ads7828_platform_data {
	bool diff_input;
	bool ext_vref;
	unsigned int vref_mv;
};

#endif /* _PDATA_ADS7828_H */
