/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MEDIA_MT9V032_H
#define _MEDIA_MT9V032_H

struct mt9v032_platform_data {
	unsigned int clk_pol:1;

	const s64 *link_freqs;
	s64 link_def_freq;
};

#endif
