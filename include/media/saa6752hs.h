/*
    saa6752hs.h - definition for saa6752hs MPEG encoder

    Copyright (C) 2003 Andrew de Quincey <adq@lidskialf.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#if 0 /* ndef _SAA6752HS_H */
#define _SAA6752HS_H

enum mpeg_video_bitrate_mode {
	MPEG_VIDEO_BITRATE_MODE_VBR = 0, /* Variable bitrate */
	MPEG_VIDEO_BITRATE_MODE_CBR = 1, /* Constant bitrate */

	MPEG_VIDEO_BITRATE_MODE_MAX
};

enum mpeg_audio_bitrate {
	MPEG_AUDIO_BITRATE_256 = 0, /* 256 kBit/sec */
	MPEG_AUDIO_BITRATE_384 = 1, /* 384 kBit/sec */

	MPEG_AUDIO_BITRATE_MAX
};

enum mpeg_video_format {
	MPEG_VIDEO_FORMAT_D1 = 0,
	MPEG_VIDEO_FORMAT_2_3_D1 = 1,
	MPEG_VIDEO_FORMAT_1_2_D1 = 2,
	MPEG_VIDEO_FORMAT_SIF = 3,

	MPEG_VIDEO_FORMAT_MAX
};

#define MPEG_VIDEO_TARGET_BITRATE_MAX 27000
#define MPEG_VIDEO_MAX_BITRATE_MAX 27000
#define MPEG_TOTAL_BITRATE_MAX 27000
#define MPEG_PID_MAX ((1 << 14) - 1)

struct mpeg_params {
	enum mpeg_video_bitrate_mode video_bitrate_mode;
	unsigned int video_target_bitrate;
	unsigned int video_max_bitrate; // only used for VBR
	enum mpeg_audio_bitrate audio_bitrate;
	unsigned int total_bitrate;

   	unsigned int pmt_pid;
	unsigned int video_pid;
	unsigned int audio_pid;
	unsigned int pcr_pid;

	enum mpeg_video_format video_format;
};

#define MPEG_SETPARAMS             _IOW('6',100,struct mpeg_params)

#endif // _SAA6752HS_H

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
