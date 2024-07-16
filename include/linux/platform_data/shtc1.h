/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Sensirion AG, Switzerland
 * Author: Johannes Winkelmann <johannes.winkelmann@sensirion.com>
 */

#ifndef __SHTC1_H_
#define __SHTC1_H_

struct shtc1_platform_data {
	bool blocking_io;
	bool high_precision;
};
#endif /* __SHTC1_H_ */
