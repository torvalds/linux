/*
 *  MMP Platform AUDIO Management
 *
 *  Copyright (c) 2011 Marvell Semiconductors Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#ifndef MMP_AUDIO_H
#define MMP_AUDIO_H

struct mmp_audio_platdata {
	u32 period_max_capture;
	u32 buffer_max_capture;
	u32 period_max_playback;
	u32 buffer_max_playback;
};

#endif /* MMP_AUDIO_H */
