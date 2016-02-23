/*
 * Driver for Digigram pcxhr compatible soundcards
 *
 * low level interface with interrupt ans message handling
 *
 * Copyright (c) 2004 by Digigram <alsa@digigram.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SOUND_PCXHR_MIX22_H
#define __SOUND_PCXHR_MIX22_H

struct pcxhr_mgr;

int hr222_sub_init(struct pcxhr_mgr *mgr);
int hr222_sub_set_clock(struct pcxhr_mgr *mgr, unsigned int rate,
			int *changed);
int hr222_get_external_clock(struct pcxhr_mgr *mgr,
			     enum pcxhr_clock_type clock_type,
			     int *sample_rate);

int hr222_read_gpio(struct pcxhr_mgr *mgr, int is_gpi, int *value);
int hr222_write_gpo(struct pcxhr_mgr *mgr, int value);
int hr222_manage_timecode(struct pcxhr_mgr *mgr, int enable);

#define HR222_LINE_PLAYBACK_LEVEL_MIN		0	/* -25.5 dB */
#define HR222_LINE_PLAYBACK_ZERO_LEVEL		51	/* 0.0 dB */
#define HR222_LINE_PLAYBACK_LEVEL_MAX		99	/* +24.0 dB */

#define HR222_LINE_CAPTURE_LEVEL_MIN		0	/* -111.5 dB */
#define HR222_LINE_CAPTURE_ZERO_LEVEL		223	/* 0.0 dB */
#define HR222_LINE_CAPTURE_LEVEL_MAX		255	/* +16 dB */
#define HR222_MICRO_CAPTURE_LEVEL_MIN		0	/* -98.5 dB */
#define HR222_MICRO_CAPTURE_LEVEL_MAX		210	/* +6.5 dB */

int hr222_update_analog_audio_level(struct snd_pcxhr *chip,
				    int is_capture,
				    int channel);
int hr222_set_audio_source(struct snd_pcxhr *chip);
int hr222_iec958_capture_byte(struct snd_pcxhr *chip, int aes_idx,
			      unsigned char *aes_bits);
int hr222_iec958_update_byte(struct snd_pcxhr *chip, int aes_idx,
			     unsigned char aes_bits);

int hr222_add_mic_controls(struct snd_pcxhr *chip);

#endif /* __SOUND_PCXHR_MIX22_H */
