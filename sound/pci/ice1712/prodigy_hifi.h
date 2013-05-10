#ifndef __SOUND_PRODIGY_HIFI_H
#define __SOUND_PRODIGY_HIFI_H

/*
 *   ALSA driver for VIA VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Audiotrak Prodigy Hifi
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

#define PRODIGY_HIFI_DEVICE_DESC 	       "{Audiotrak,Prodigy 7.1 HIFI},"\
                                           "{Audiotrak Prodigy HD2},"\
                                           "{Hercules Fortissimo IV},"

#define VT1724_SUBDEVICE_PRODIGY_HIFI	0x38315441	/* PRODIGY 7.1 HIFI */
#define VT1724_SUBDEVICE_PRODIGY_HD2	0x37315441	/* PRODIGY HD2 */
#define VT1724_SUBDEVICE_FORTISSIMO4	0x81160100	/* Fortissimo IV */


extern struct snd_ice1712_card_info  snd_vt1724_prodigy_hifi_cards[];

#endif /* __SOUND_PRODIGY_HIFI_H */
