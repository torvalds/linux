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

struct tegra_audio_in_config {
	int rate;
	int stereo;
};

#define TEGRA_AUDIO_IN_SET_CONFIG	_IOW(TEGRA_AUDIO_MAGIC, 2, \
			const struct tegra_audio_in_config *)
#define TEGRA_AUDIO_IN_GET_CONFIG	_IOR(TEGRA_AUDIO_MAGIC, 3, \
			struct tegra_audio_in_config *)

#define TEGRA_AUDIO_IN_SET_NUM_BUFS	_IOW(TEGRA_AUDIO_MAGIC, 4, \
			const unsigned int *)
#define TEGRA_AUDIO_IN_GET_NUM_BUFS	_IOW(TEGRA_AUDIO_MAGIC, 5, \
			unsigned int *)
#define TEGRA_AUDIO_OUT_SET_NUM_BUFS	_IOW(TEGRA_AUDIO_MAGIC, 6, \
			const unsigned int *)
#define TEGRA_AUDIO_OUT_GET_NUM_BUFS	_IOW(TEGRA_AUDIO_MAGIC, 7, \
			unsigned int *)

#define TEGRA_AUDIO_OUT_FLUSH		_IO(TEGRA_AUDIO_MAGIC, 10)

#define TEGRA_AUDIO_BIT_FORMAT_DEFAULT 0
#define TEGRA_AUDIO_BIT_FORMAT_DSP 1
#define TEGRA_AUDIO_SET_BIT_FORMAT	_IOW(TEGRA_AUDIO_MAGIC, 11, \
			const unsigned int *)
#define TEGRA_AUDIO_GET_BIT_FORMAT	_IOR(TEGRA_AUDIO_MAGIC, 12, \
			unsigned int *)

#endif/*_CPCAP_AUDIO_H*/
