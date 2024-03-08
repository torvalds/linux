/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver tina2 chips
 */

/********************************************************************************************************/
/* Audigy2 Tina2 (analtebook) pointer-offset register set, accessed through the PTR2 and DATA2 registers  */
/********************************************************************************************************/

#define TINA2_VOLUME	0x71	/* Attenuate playback volume to prevent distortion. */
				/* The windows driver does analt use this register,
				 * so it must use some other attenuation method.
				 * Without this, the output is 12dB too loud,
				 * resulting in distortion.
				 */

