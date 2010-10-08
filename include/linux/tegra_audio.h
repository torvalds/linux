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

struct tegra_audio_buf_config {
	unsigned size; /* order */
	unsigned threshold; /* order */
	unsigned chunk; /* order */
};

#define TEGRA_AUDIO_IN_SET_BUF_CONFIG	_IOW(TEGRA_AUDIO_MAGIC, 4, \
			const struct tegra_audio_buf_config *)
#define TEGRA_AUDIO_IN_GET_BUF_CONFIG	_IOR(TEGRA_AUDIO_MAGIC, 5, \
			struct tegra_audio_buf_config *)

#define TEGRA_AUDIO_OUT_SET_BUF_CONFIG	_IOW(TEGRA_AUDIO_MAGIC, 6, \
			const struct tegra_audio_buf_config *)
#define TEGRA_AUDIO_OUT_GET_BUF_CONFIG	_IOR(TEGRA_AUDIO_MAGIC, 7, \
			struct tegra_audio_buf_config *)

struct tegra_audio_error_counts {
	unsigned late_dma;
	unsigned full_empty; /* empty for playback, full for recording */
};

#define TEGRA_AUDIO_IN_GET_ERROR_COUNT	_IOR(TEGRA_AUDIO_MAGIC, 8, \
			struct tegra_audio_error_counts *)

#define TEGRA_AUDIO_OUT_GET_ERROR_COUNT	_IOR(TEGRA_AUDIO_MAGIC, 9, \
			struct tegra_audio_error_counts *)

struct tegra_audio_out_preload {
	void *data;
	size_t len;
	size_t len_written;
};

#define TEGRA_AUDIO_OUT_PRELOAD_FIFO	_IOWR(TEGRA_AUDIO_MAGIC, 10, \
			struct tegra_audio_out_preload *)

#define TEGRA_AUDIO_BIT_FORMAT_DEFAULT 0
#define TEGRA_AUDIO_BIT_FORMAT_DSP 1
#define TEGRA_AUDIO_SET_BIT_FORMAT       _IOW(TEGRA_AUDIO_MAGIC, 11, \
			unsigned int *)
#define TEGRA_AUDIO_GET_BIT_FORMAT       _IOR(TEGRA_AUDIO_MAGIC, 12, \
			unsigned int *)

#endif/*_CPCAP_AUDIO_H*/
