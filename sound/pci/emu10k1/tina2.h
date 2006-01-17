/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver p16v chips
 *  Version: 0.21
 *
 *
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
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

/********************************************************************************************************/
/* Audigy2 Tina2 (notebook) pointer-offset register set, accessed through the PTR2 and DATA2 registers  */
/********************************************************************************************************/

#define TINA2_VOLUME	0x71	/* Attenuate playback volume to prevent distortion. */
				/* The windows driver does not use this register,
				 * so it must use some other attenuation method.
				 * Without this, the output is 12dB too loud,
				 * resulting in distortion.
				 */

