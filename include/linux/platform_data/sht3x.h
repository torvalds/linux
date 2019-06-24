/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Sensirion AG, Switzerland
 * Author: David Frey <david.frey@sensirion.com>
 * Author: Pascal Sachs <pascal.sachs@sensirion.com>
 */

#ifndef __SHT3X_H_
#define __SHT3X_H_

struct sht3x_platform_data {
	bool blocking_io;
	bool high_precision;
};
#endif /* __SHT3X_H_ */
