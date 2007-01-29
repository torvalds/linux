#ifndef __SOUND_VT1720_MOBO_H
#define __SOUND_VT1720_MOBO_H

/*
 *   ALSA driver for VT1720/VT1724 (Envy24PT/Envy24HT)
 *
 *   Lowlevel functions for VT1720-based motherboards
 *
 *	Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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
 *
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

extern const struct snd_ice1712_card_info  snd_vt1720_mobo_cards[];

#endif /* __SOUND_VT1720_MOBO_H */
