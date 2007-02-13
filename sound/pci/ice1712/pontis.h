#ifndef __SOUND_PONTIS_H
#define __SOUND_PONTIS_H

/*
 *   ALSA driver for VIA VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Pontis MS300 boards
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

#define PONTIS_DEVICE_DESC 	       "{Pontis,MS300},"

#define VT1720_SUBDEVICE_PONTIS_MS300	0x00020002	/* a dummy id for MS300 */

extern const struct snd_ice1712_card_info  snd_vt1720_pontis_cards[];

#endif /* __SOUND_PONTIS_H */
