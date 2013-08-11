/* include/linux/wifi_tiwlan.h
 *
 * Copyright (C) 2008 Google, Inc.
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
#ifndef _LINUX_WIFI_TIWLAN_H_
#define _LINUX_WIFI_TIWLAN_H_

#include <linux/wlan_plat.h>

#define WMPA_NUMBER_OF_SECTIONS	3
#define WMPA_NUMBER_OF_BUFFERS	160
#define WMPA_SECTION_HEADER	24
#define WMPA_SECTION_SIZE_0	(WMPA_NUMBER_OF_BUFFERS * 64)
#define WMPA_SECTION_SIZE_1	(WMPA_NUMBER_OF_BUFFERS * 256)
#define WMPA_SECTION_SIZE_2	(WMPA_NUMBER_OF_BUFFERS * 2048)

#endif
