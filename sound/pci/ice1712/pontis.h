/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_PONTIS_H
#define __SOUND_PONTIS_H

/*
 *   ALSA driver for VIA VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Pontis MS300 boards
 *
 *	Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 */      

#define PONTIS_DEVICE_DESC 	       "{Pontis,MS300},"

#define VT1720_SUBDEVICE_PONTIS_MS300	0x00020002	/* a dummy id for MS300 */

extern struct snd_ice1712_card_info  snd_vt1720_pontis_cards[];

#endif /* __SOUND_PONTIS_H */
