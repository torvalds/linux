/* include/linux/tegra_audio.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *     Iliyan Malchev <malchev@google.com>
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

#ifndef _TEGRA_AUDIO_H
#define _TEGRA_AUDIO_H

#include <linux/ioctl.h>

#define TEGRA_AUDIO_MAGIC 't'

#define TEGRA_AUDIO_IN_START _IO(TEGRA_AUDIO_MAGIC, 0)
#define TEGRA_AUDIO_IN_STOP  _IO(TEGRA_AUDIO_MAGIC, 1)

#endif/*_CPCAP_AUDIO_H*/
