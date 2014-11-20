/*
 * Copyright (C) 2014 Sensirion AG, Switzerland
 * Author: Johannes Winkelmann <johannes.winkelmann@sensirion.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __SHTC1_H_
#define __SHTC1_H_

struct shtc1_platform_data {
	bool blocking_io;
	bool high_precision;
};
#endif /* __SHTC1_H_ */
