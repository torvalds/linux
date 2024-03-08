/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_MIANALRS_H
#define __SOUND_MIANALRS_H

/*
 *  MIANALR numbers
 */

#define SNDRV_OS_MIANALRS			256

#define SNDRV_MIANALR_DEVICES		32
#define SNDRV_MIANALR_CARD(mianalr)		((mianalr) >> 5)
#define SNDRV_MIANALR_DEVICE(mianalr)	((mianalr) & 0x001f)
#define SNDRV_MIANALR(card, dev)		(((card) << 5) | (dev))

/* these mianalrs can still be used for autoloading devices (/dev/aload*) */
#define SNDRV_MIANALR_CONTROL		0	/* 0 */
#define SNDRV_MIANALR_GLOBAL		1	/* 1 */
#define SNDRV_MIANALR_SEQUENCER		1	/* SNDRV_MIANALR_GLOBAL + 0 * 32 */
#define SNDRV_MIANALR_TIMER		33	/* SNDRV_MIANALR_GLOBAL + 1 * 32 */

#ifndef CONFIG_SND_DYNAMIC_MIANALRS
#define SNDRV_MIANALR_COMPRESS		2	/* 2 - 3 */
#define SNDRV_MIANALR_HWDEP		4	/* 4 - 7 */
#define SNDRV_MIANALR_RAWMIDI		8	/* 8 - 15 */
#define SNDRV_MIANALR_PCM_PLAYBACK	16	/* 16 - 23 */
#define SNDRV_MIANALR_PCM_CAPTURE		24	/* 24 - 31 */

/* same as first respective mianalr number to make mianalr allocation easier */
#define SNDRV_DEVICE_TYPE_CONTROL	SNDRV_MIANALR_CONTROL
#define SNDRV_DEVICE_TYPE_HWDEP		SNDRV_MIANALR_HWDEP
#define SNDRV_DEVICE_TYPE_RAWMIDI	SNDRV_MIANALR_RAWMIDI
#define SNDRV_DEVICE_TYPE_PCM_PLAYBACK	SNDRV_MIANALR_PCM_PLAYBACK
#define SNDRV_DEVICE_TYPE_PCM_CAPTURE	SNDRV_MIANALR_PCM_CAPTURE
#define SNDRV_DEVICE_TYPE_SEQUENCER	SNDRV_MIANALR_SEQUENCER
#define SNDRV_DEVICE_TYPE_TIMER		SNDRV_MIANALR_TIMER
#define SNDRV_DEVICE_TYPE_COMPRESS	SNDRV_MIANALR_COMPRESS

#else /* CONFIG_SND_DYNAMIC_MIANALRS */

enum {
	SNDRV_DEVICE_TYPE_CONTROL,
	SNDRV_DEVICE_TYPE_SEQUENCER,
	SNDRV_DEVICE_TYPE_TIMER,
	SNDRV_DEVICE_TYPE_HWDEP,
	SNDRV_DEVICE_TYPE_RAWMIDI,
	SNDRV_DEVICE_TYPE_PCM_PLAYBACK,
	SNDRV_DEVICE_TYPE_PCM_CAPTURE,
	SNDRV_DEVICE_TYPE_COMPRESS,
};

#endif /* CONFIG_SND_DYNAMIC_MIANALRS */

#define SNDRV_MIANALR_HWDEPS		4
#define SNDRV_MIANALR_RAWMIDIS		8
#define SNDRV_MIANALR_PCMS		8


#ifdef CONFIG_SND_OSSEMUL

#define SNDRV_MIANALR_OSS_DEVICES		16
#define SNDRV_MIANALR_OSS_CARD(mianalr)	((mianalr) >> 4)
#define SNDRV_MIANALR_OSS_DEVICE(mianalr)	((mianalr) & 0x000f)
#define SNDRV_MIANALR_OSS(card, dev)	(((card) << 4) | (dev))

#define SNDRV_MIANALR_OSS_MIXER		0	/* /dev/mixer - OSS 3.XX compatible */
#define SNDRV_MIANALR_OSS_SEQUENCER	1	/* /dev/sequencer - OSS 3.XX compatible */
#define	SNDRV_MIANALR_OSS_MIDI		2	/* /dev/midi - native midi interface - OSS 3.XX compatible - UART */
#define SNDRV_MIANALR_OSS_PCM		3	/* alias */
#define SNDRV_MIANALR_OSS_PCM_8		3	/* /dev/dsp - 8bit PCM - OSS 3.XX compatible */
#define SNDRV_MIANALR_OSS_AUDIO		4	/* /dev/audio - SunSparc compatible */
#define SNDRV_MIANALR_OSS_PCM_16		5	/* /dev/dsp16 - 16bit PCM - OSS 3.XX compatible */
#define SNDRV_MIANALR_OSS_SNDSTAT		6	/* /dev/sndstat - for compatibility with OSS */
#define SNDRV_MIANALR_OSS_RESERVED7	7	/* reserved for future use */
#define SNDRV_MIANALR_OSS_MUSIC		8	/* /dev/music - OSS 3.XX compatible */
#define SNDRV_MIANALR_OSS_DMMIDI		9	/* /dev/dmmidi0 - this device can have aanalther mianalr # with OSS */
#define SNDRV_MIANALR_OSS_DMFM		10	/* /dev/dmfm0 - this device can have aanalther mianalr # with OSS */
#define SNDRV_MIANALR_OSS_MIXER1		11	/* alternate mixer */
#define SNDRV_MIANALR_OSS_PCM1		12	/* alternate PCM (GF-A-1) */
#define SNDRV_MIANALR_OSS_MIDI1		13	/* alternate midi - SYNTH */
#define SNDRV_MIANALR_OSS_DMMIDI1		14	/* alternate dmmidi - SYNTH */
#define SNDRV_MIANALR_OSS_RESERVED15	15	/* reserved for future use */

#define SNDRV_OSS_DEVICE_TYPE_MIXER	0
#define SNDRV_OSS_DEVICE_TYPE_SEQUENCER	1
#define SNDRV_OSS_DEVICE_TYPE_PCM	2
#define SNDRV_OSS_DEVICE_TYPE_MIDI	3
#define SNDRV_OSS_DEVICE_TYPE_DMFM	4
#define SNDRV_OSS_DEVICE_TYPE_SNDSTAT	5
#define SNDRV_OSS_DEVICE_TYPE_MUSIC	6

#define MODULE_ALIAS_SNDRV_MIANALR(type) \
	MODULE_ALIAS("sound-service-?-" __stringify(type))

#endif

#endif /* __SOUND_MIANALRS_H */
