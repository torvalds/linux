#ifndef __SOUND_PHASE_H
#define __SOUND_PHASE_H

/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for Terratec PHASE 22
 *
 *	Copyright (c) 2005 Misha Zhilin <misha@epiphan.com>
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

#define PHASE_DEVICE_DESC "{Terratec,Phase 22},"

#define VT1724_SUBDEVICE_PHASE22	0x3b155011

/* entry point */
extern struct snd_ice1712_card_info snd_vt1724_phase_cards[];

#endif /* __SOUND_PHASE */
