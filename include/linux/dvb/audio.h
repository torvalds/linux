/*
 * audio.h
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBAUDIO_H_
#define _DVBAUDIO_H_

#include <linux/types.h>

typedef enum {
	AUDIO_SOURCE_DEMUX, /* Select the demux as the main source */
	AUDIO_SOURCE_MEMORY /* Select internal memory as the main source */
} audio_stream_source_t;


typedef enum {
	AUDIO_STOPPED,      /* Device is stopped */
	AUDIO_PLAYING,      /* Device is currently playing */
	AUDIO_PAUSED        /* Device is paused */
} audio_play_state_t;


typedef enum {
	AUDIO_STEREO,
	AUDIO_MONO_LEFT,
	AUDIO_MONO_RIGHT,
	AUDIO_MONO,
	AUDIO_STEREO_SWAPPED
} audio_channel_select_t;


typedef struct audio_mixer {
	unsigned int volume_left;
	unsigned int volume_right;
  // what else do we need? bass, pass-through, ...
} audio_mixer_t;


typedef struct audio_status {
	int                    AV_sync_state;  /* sync audio and video? */
	int                    mute_state;     /* audio is muted */
	audio_play_state_t     play_state;     /* current playback state */
	audio_stream_source_t  stream_source;  /* current stream source */
	audio_channel_select_t channel_select; /* currently selected channel */
	int                    bypass_mode;    /* pass on audio data to */
	audio_mixer_t	       mixer_state;    /* current mixer state */
} audio_status_t;                              /* separate decoder hardware */


typedef
struct audio_karaoke{  /* if Vocal1 or Vocal2 are non-zero, they get mixed  */
	int vocal1;    /* into left and right t at 70% each */
	int vocal2;    /* if both, Vocal1 and Vocal2 are non-zero, Vocal1 gets*/
	int melody;    /* mixed into the left channel and */
		       /* Vocal2 into the right channel at 100% each. */
		       /* if Melody is non-zero, the melody channel gets mixed*/
} audio_karaoke_t;     /* into left and right  */


typedef __u16 audio_attributes_t;
/*   bits: descr. */
/*   15-13 audio coding mode (0=ac3, 2=mpeg1, 3=mpeg2ext, 4=LPCM, 6=DTS, */
/*   12    multichannel extension */
/*   11-10 audio type (0=not spec, 1=language included) */
/*    9- 8 audio application mode (0=not spec, 1=karaoke, 2=surround) */
/*    7- 6 Quantization / DRC (mpeg audio: 1=DRC exists)(lpcm: 0=16bit,  */
/*    5- 4 Sample frequency fs (0=48kHz, 1=96kHz) */
/*    2- 0 number of audio channels (n+1 channels) */


/* for GET_CAPABILITIES and SET_FORMAT, the latter should only set one bit */
#define AUDIO_CAP_DTS    1
#define AUDIO_CAP_LPCM   2
#define AUDIO_CAP_MP1    4
#define AUDIO_CAP_MP2    8
#define AUDIO_CAP_MP3   16
#define AUDIO_CAP_AAC   32
#define AUDIO_CAP_OGG   64
#define AUDIO_CAP_SDDS 128
#define AUDIO_CAP_AC3  256

#define AUDIO_STOP                 _IO('o', 1)
#define AUDIO_PLAY                 _IO('o', 2)
#define AUDIO_PAUSE                _IO('o', 3)
#define AUDIO_CONTINUE             _IO('o', 4)
#define AUDIO_SELECT_SOURCE        _IO('o', 5)
#define AUDIO_SET_MUTE             _IO('o', 6)
#define AUDIO_SET_AV_SYNC          _IO('o', 7)
#define AUDIO_SET_BYPASS_MODE      _IO('o', 8)
#define AUDIO_CHANNEL_SELECT       _IO('o', 9)
#define AUDIO_GET_STATUS           _IOR('o', 10, audio_status_t)

#define AUDIO_GET_CAPABILITIES     _IOR('o', 11, unsigned int)
#define AUDIO_CLEAR_BUFFER         _IO('o',  12)
#define AUDIO_SET_ID               _IO('o', 13)
#define AUDIO_SET_MIXER            _IOW('o', 14, audio_mixer_t)
#define AUDIO_SET_STREAMTYPE       _IO('o', 15)
#define AUDIO_SET_EXT_ID           _IO('o', 16)
#define AUDIO_SET_ATTRIBUTES       _IOW('o', 17, audio_attributes_t)
#define AUDIO_SET_KARAOKE          _IOW('o', 18, audio_karaoke_t)

/**
 * AUDIO_GET_PTS
 *
 * Read the 33 bit presentation time stamp as defined
 * in ITU T-REC-H.222.0 / ISO/IEC 13818-1.
 *
 * The PTS should belong to the currently played
 * frame if possible, but may also be a value close to it
 * like the PTS of the last decoded frame or the last PTS
 * extracted by the PES parser.
 */
#define AUDIO_GET_PTS              _IOR('o', 19, __u64)
#define AUDIO_BILINGUAL_CHANNEL_SELECT _IO('o', 20)

#endif /* _DVBAUDIO_H_ */
