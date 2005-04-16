#ifndef __SOUND_AMP_H
#define __SOUND_AMP_H

/*
 *   ALSA driver for VIA VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Advanced Micro Peripherals Ltd AUDIO2000
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
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

#define  AMP_AUDIO2000_DEVICE_DESC 	       "{AMP Ltd,AUDIO2000},"

#define VT1724_SUBDEVICE_AUDIO2000	0x12142417	/* Advanced Micro Peripherals Ltd AUDIO2000 */

extern struct snd_ice1712_card_info  snd_vt1724_amp_cards[];


#endif /* __SOUND_AMP_H */
