/* include/linux/tegra_spdif.h
 *
 * SPDIF audio driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _TEGRA_SPDIF_H
#define _TEGRA_SPDIF_H

#include <linux/ioctl.h>

#define TEGRA_SPDIF_MAGIC 's'



struct tegra_audio_buf_config {
	unsigned size; /* order */
	unsigned threshold; /* order */
	unsigned chunk; /* order */
};



#define TEGRA_AUDIO_OUT_SET_BUF_CONFIG	_IOW(TEGRA_SPDIF_MAGIC, 0, \
			const struct tegra_audio_buf_config *)
#define TEGRA_AUDIO_OUT_GET_BUF_CONFIG	_IOR(TEGRA_SPDIF_MAGIC, 1, \
			struct tegra_audio_buf_config *)

#define TEGRA_AUDIO_OUT_GET_ERROR_COUNT	_IOR(TEGRA_SPDIF_MAGIC, 2, \
			unsigned *)

struct tegra_audio_out_preload {
	void *data;
	size_t len;
	size_t len_written;
};

#define TEGRA_AUDIO_OUT_PRELOAD_FIFO	_IOWR(TEGRA_SPDIF_MAGIC, 3, \
			struct tegra_audio_out_preload *)

#endif/*_TEGRA_SPDIF_H*/
