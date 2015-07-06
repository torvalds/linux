/* drivers/input/touchscreen/zforce.c
 *
 * Copyright (C) 2012-2013 MundoReader S.L.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _LINUX_INPUT_ZFORCE_TS_H
#define _LINUX_INPUT_ZFORCE_TS_H

struct zforce_ts_platdata {
	unsigned int x_max;
	unsigned int y_max;
};

#endif /* _LINUX_INPUT_ZFORCE_TS_H */
