/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ULTRASOUND_H_
#define _ULTRASOUND_H_
/*
 *	ultrasound.h - Macros for programming the Gravis Ultrasound
 *			These macros are extremely device dependent
 *			and not portable.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


/*
 *	Private events for Gravis Ultrasound (GUS)
 *
 *	Format:
 *		byte 0 		- SEQ_PRIVATE (0xfe)
 *		byte 1 		- Synthesizer device number (0-N)
 *		byte 2 		- Command (see below)
 *		byte 3 		- Voice number (0-31)
 *		bytes 4 and 5	- parameter P1 (unsigned short)
 *		bytes 6 and 7	- parameter P2 (unsigned short)
 *
 *	Commands:
 *		Each command affects one voice defined in byte 3.
 *		Unused parameters (P1 and/or P2 *MUST* be initialized to zero).
 *		_GUS_NUMVOICES	- Sets max. number of concurrent voices (P1=14-31, default 16)
 *		_GUS_VOICESAMPLE- ************ OBSOLETE *************
 *		_GUS_VOICEON	- Starts voice (P1=voice mode)
 *		_GUS_VOICEOFF	- Stops voice (no parameters)
 *		_GUS_VOICEFADE	- Stops the voice smoothly.
 *		_GUS_VOICEMODE	- Alters the voice mode, don't start or stop voice (P1=voice mode)
 *		_GUS_VOICEBALA	- Sets voice balance (P1, 0=left, 7=middle and 15=right, default 7)
 *		_GUS_VOICEFREQ	- Sets voice (sample) playback frequency (P1=Hz)
 *		_GUS_VOICEVOL	- Sets voice volume (P1=volume, 0xfff=max, 0xeff=half, 0x000=off)
 *		_GUS_VOICEVOL2	- Sets voice volume (P1=volume, 0xfff=max, 0xeff=half, 0x000=off)
 *				  (Like GUS_VOICEVOL but doesn't change the hw
 *				  volume. It just updates volume in the voice table).
 *
 *		_GUS_RAMPRANGE	- Sets limits for volume ramping (P1=low volume, P2=high volume)
 *		_GUS_RAMPRATE	- Sets the speed for volume ramping (P1=scale, P2=rate)
 *		_GUS_RAMPMODE	- Sets the volume ramping mode (P1=ramping mode)
 *		_GUS_RAMPON	- Starts volume ramping (no parameters)
 *		_GUS_RAMPOFF	- Stops volume ramping (no parameters)
 *		_GUS_VOLUME_SCALE - Changes the volume calculation constants
 *				  for all voices.
 */

#define _GUS_NUMVOICES		0x00
#define _GUS_VOICESAMPLE	0x01	/* OBSOLETE */
#define _GUS_VOICEON		0x02
#define _GUS_VOICEOFF		0x03
#define _GUS_VOICEMODE		0x04
#define _GUS_VOICEBALA		0x05
#define _GUS_VOICEFREQ		0x06
#define _GUS_VOICEVOL		0x07
#define _GUS_RAMPRANGE		0x08
#define _GUS_RAMPRATE		0x09
#define _GUS_RAMPMODE		0x0a
#define _GUS_RAMPON		0x0b
#define _GUS_RAMPOFF		0x0c
#define _GUS_VOICEFADE		0x0d
#define _GUS_VOLUME_SCALE	0x0e
#define _GUS_VOICEVOL2		0x0f
#define _GUS_VOICE_POS		0x10

/*
 *	GUS API macros
 */

#define _GUS_CMD(chn, voice, cmd, p1, p2) \
					{_SEQ_NEEDBUF(8); _seqbuf[_seqbufptr] = SEQ_PRIVATE;\
					_seqbuf[_seqbufptr+1] = (chn); _seqbuf[_seqbufptr+2] = cmd;\
					_seqbuf[_seqbufptr+3] = voice;\
					*(unsigned short*)&_seqbuf[_seqbufptr+4] = p1;\
					*(unsigned short*)&_seqbuf[_seqbufptr+6] = p2;\
					_SEQ_ADVBUF(8);}

#define GUS_NUMVOICES(chn, p1)			_GUS_CMD(chn, 0, _GUS_NUMVOICES, (p1), 0)
#define GUS_VOICESAMPLE(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_VOICESAMPLE, (p1), 0)	/* OBSOLETE */
#define GUS_VOICEON(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_VOICEON, (p1), 0)
#define GUS_VOICEOFF(chn, voice)		_GUS_CMD(chn, voice, _GUS_VOICEOFF, 0, 0)
#define GUS_VOICEFADE(chn, voice)		_GUS_CMD(chn, voice, _GUS_VOICEFADE, 0, 0)
#define GUS_VOICEMODE(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_VOICEMODE, (p1), 0)
#define GUS_VOICEBALA(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_VOICEBALA, (p1), 0)
#define GUS_VOICEFREQ(chn, voice, p)		_GUS_CMD(chn, voice, _GUS_VOICEFREQ, \
							(p) & 0xffff, ((p) >> 16) & 0xffff)
#define GUS_VOICEVOL(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_VOICEVOL, (p1), 0)
#define GUS_VOICEVOL2(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_VOICEVOL2, (p1), 0)
#define GUS_RAMPRANGE(chn, voice, low, high)	_GUS_CMD(chn, voice, _GUS_RAMPRANGE, (low), (high))
#define GUS_RAMPRATE(chn, voice, p1, p2)	_GUS_CMD(chn, voice, _GUS_RAMPRATE, (p1), (p2))
#define GUS_RAMPMODE(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_RAMPMODE, (p1), 0)
#define GUS_RAMPON(chn, voice, p1)		_GUS_CMD(chn, voice, _GUS_RAMPON, (p1), 0)
#define GUS_RAMPOFF(chn, voice)			_GUS_CMD(chn, voice, _GUS_RAMPOFF, 0, 0)
#define GUS_VOLUME_SCALE(chn, voice, p1, p2)	_GUS_CMD(chn, voice, _GUS_VOLUME_SCALE, (p1), (p2))
#define GUS_VOICE_POS(chn, voice, p)		_GUS_CMD(chn, voice, _GUS_VOICE_POS, \
							(p) & 0xffff, ((p) >> 16) & 0xffff)

#endif
