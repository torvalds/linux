/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __Q6DSP_COMMON_H__
#define __Q6DSP_COMMON_H__

#include <linux/kernel.h>

#define PCM_MAX_NUM_CHANNEL  8
#define PCM_CHANNEL_NULL 0

#define PCM_CHANNEL_FL    1	/* Front left channel. */
#define PCM_CHANNEL_FR    2	/* Front right channel. */
#define PCM_CHANNEL_FC    3	/* Front center channel. */
#define PCM_CHANNEL_LS   4	/* Left surround channel. */
#define PCM_CHANNEL_RS   5	/* Right surround channel. */
#define PCM_CHANNEL_LFE  6	/* Low frequency effect channel. */
#define PCM_CHANNEL_CS   7	/* Center surround channel; Rear center ch */
#define PCM_CHANNEL_LB   8	/* Left back channel; Rear left channel. */
#define PCM_CHANNEL_RB   9	/* Right back channel; Rear right channel. */
#define PCM_CHANNELS   10	/* Top surround channel. */

int q6dsp_map_channels(u8 ch_map[PCM_MAX_NUM_CHANNEL], int ch);
int q6dsp_get_channel_allocation(int channels);

#endif /* __Q6DSP_COMMON_H__ */
