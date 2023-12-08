/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_VT1720_MOBO_H
#define __SOUND_VT1720_MOBO_H

/*
 *   ALSA driver for VT1720/VT1724 (Envy24PT/Envy24HT)
 *
 *   Lowlevel functions for VT1720-based motherboards
 *
 *	Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 */      

#define VT1720_MOBO_DEVICE_DESC        "{Albatron,K8X800 Pro II},"\
				       "{Chaintech,ZNF3-150},"\
				       "{Chaintech,ZNF3-250},"\
				       "{Chaintech,9CJS},"\
				       "{Shuttle,SN25P},"

#define VT1720_SUBDEVICE_K8X800		0xf217052c
#define VT1720_SUBDEVICE_ZNF3_150	0x0f2741f6
#define VT1720_SUBDEVICE_ZNF3_250	0x0f2745f6
#define VT1720_SUBDEVICE_9CJS		0x0f272327
#define VT1720_SUBDEVICE_SN25P		0x97123650

extern struct snd_ice1712_card_info  snd_vt1720_mobo_cards[];

#endif /* __SOUND_VT1720_MOBO_H */
