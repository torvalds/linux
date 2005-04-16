#ifndef __SOUND_CS8403_H
#define __SOUND_CS8403_H

/*
 *  Routines for Cirrus Logic CS8403/CS8404A IEC958 (S/PDIF) Transmitter
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>,
 *		     Takashi Iwai <tiwai@suse.de>
 *
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

#ifdef SND_CS8403

#ifndef SND_CS8403_DECL
#define SND_CS8403_DECL static
#endif
#ifndef SND_CS8403_DECODE
#define SND_CS8403_DECODE snd_cs8403_decode_spdif_bits
#endif
#ifndef SND_CS8403_ENCODE
#define SND_CS8403_ENCODE snd_cs8403_encode_spdif_bits
#endif


SND_CS8403_DECL void SND_CS8403_DECODE(snd_aes_iec958_t *diga, unsigned char bits)
{
	if (bits & 0x01) {	/* consumer */
		if (!(bits & 0x02))
			diga->status[0] |= IEC958_AES0_NONAUDIO;
		if (!(bits & 0x08))
			diga->status[0] |= IEC958_AES0_CON_NOT_COPYRIGHT;
		switch (bits & 0x10) {
		case 0x10: diga->status[0] |= IEC958_AES0_CON_EMPHASIS_NONE; break;
		case 0x00: diga->status[0] |= IEC958_AES0_CON_EMPHASIS_5015; break;
		}
		if (!(bits & 0x80))
			diga->status[1] |= IEC958_AES1_CON_ORIGINAL;
		switch (bits & 0x60) {
		case 0x00: diga->status[1] |= IEC958_AES1_CON_MAGNETIC_ID; break;
		case 0x20: diga->status[1] |= IEC958_AES1_CON_DIGDIGCONV_ID; break;
		case 0x40: diga->status[1] |= IEC958_AES1_CON_LASEROPT_ID; break;
		case 0x60: diga->status[1] |= IEC958_AES1_CON_GENERAL; break;
		}
		switch (bits & 0x06) {
		case 0x00: diga->status[3] |= IEC958_AES3_CON_FS_44100; break;
		case 0x02: diga->status[3] |= IEC958_AES3_CON_FS_48000; break;
		case 0x04: diga->status[3] |= IEC958_AES3_CON_FS_32000; break;
		}
	} else {
		diga->status[0] = IEC958_AES0_PROFESSIONAL;
		switch (bits & 0x18) {
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_FS_32000; break;
		case 0x10: diga->status[0] |= IEC958_AES0_PRO_FS_44100; break;
		case 0x08: diga->status[0] |= IEC958_AES0_PRO_FS_48000; break;
		case 0x18: diga->status[0] |= IEC958_AES0_PRO_FS_NOTID; break;
		}
		switch (bits & 0x60) {
		case 0x20: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NONE; break;
		case 0x40: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_5015; break;
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_CCITT; break;
		case 0x60: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NOTID; break;
		}
		if (bits & 0x80)
			diga->status[1] |= IEC958_AES1_PRO_MODE_STEREOPHONIC;
	}
}

SND_CS8403_DECL unsigned char SND_CS8403_ENCODE(snd_aes_iec958_t *diga)
{
	unsigned char bits;

	if (!(diga->status[0] & IEC958_AES0_PROFESSIONAL)) {
		bits = 0x01;	/* consumer mode */
		if (diga->status[0] & IEC958_AES0_NONAUDIO)
			bits &= ~0x02;
		else
			bits |= 0x02;
		if (diga->status[0] & IEC958_AES0_CON_NOT_COPYRIGHT)
			bits &= ~0x08;
		else
			bits |= 0x08;
		switch (diga->status[0] & IEC958_AES0_CON_EMPHASIS) {
		default:
		case IEC958_AES0_CON_EMPHASIS_NONE: bits |= 0x10; break;
		case IEC958_AES0_CON_EMPHASIS_5015: bits |= 0x00; break;
		}
		if (diga->status[1] & IEC958_AES1_CON_ORIGINAL)
			bits &= ~0x80;
		else
			bits |= 0x80;
		if ((diga->status[1] & IEC958_AES1_CON_CATEGORY) == IEC958_AES1_CON_GENERAL)
			bits |= 0x60;
		else {
			switch(diga->status[1] & IEC958_AES1_CON_MAGNETIC_MASK) {
			case IEC958_AES1_CON_MAGNETIC_ID:
				bits |= 0x00; break;
			case IEC958_AES1_CON_DIGDIGCONV_ID:
				bits |= 0x20; break;
			default:
			case IEC958_AES1_CON_LASEROPT_ID:
				bits |= 0x40; break;
			}
		}
		switch (diga->status[3] & IEC958_AES3_CON_FS) {
		default:
		case IEC958_AES3_CON_FS_44100: bits |= 0x00; break;
		case IEC958_AES3_CON_FS_48000: bits |= 0x02; break;
		case IEC958_AES3_CON_FS_32000: bits |= 0x04; break;
		}
	} else {
		bits = 0x00;	/* professional mode */
		if (diga->status[0] & IEC958_AES0_NONAUDIO)
			bits &= ~0x02;
		else
			bits |= 0x02;
		/* CHECKME: I'm not sure about the bit order in val here */
		switch (diga->status[0] & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_32000:	bits |= 0x00; break;
		case IEC958_AES0_PRO_FS_44100:	bits |= 0x10; break;	/* 44.1kHz */
		case IEC958_AES0_PRO_FS_48000:	bits |= 0x08; break;	/* 48kHz */
		default:
		case IEC958_AES0_PRO_FS_NOTID: bits |= 0x18; break;
		}
		switch (diga->status[0] & IEC958_AES0_PRO_EMPHASIS) {
		case IEC958_AES0_PRO_EMPHASIS_NONE: bits |= 0x20; break;
		case IEC958_AES0_PRO_EMPHASIS_5015: bits |= 0x40; break;
		case IEC958_AES0_PRO_EMPHASIS_CCITT: bits |= 0x00; break;
		default:
		case IEC958_AES0_PRO_EMPHASIS_NOTID: bits |= 0x60; break;
		}
		switch (diga->status[1] & IEC958_AES1_PRO_MODE) {
		case IEC958_AES1_PRO_MODE_TWO:
		case IEC958_AES1_PRO_MODE_STEREOPHONIC: bits |= 0x00; break;
		default: bits |= 0x80; break;
		}
	}
	return bits;
}

#endif /* SND_CS8403 */

#ifdef SND_CS8404

#ifndef SND_CS8404_DECL
#define SND_CS8404_DECL static
#endif
#ifndef SND_CS8404_DECODE
#define SND_CS8404_DECODE snd_cs8404_decode_spdif_bits
#endif
#ifndef SND_CS8404_ENCODE
#define SND_CS8404_ENCODE snd_cs8404_encode_spdif_bits
#endif


SND_CS8404_DECL void SND_CS8404_DECODE(snd_aes_iec958_t *diga, unsigned char bits)
{
	if (bits & 0x10) {	/* consumer */
		if (!(bits & 0x20))
			diga->status[0] |= IEC958_AES0_CON_NOT_COPYRIGHT;
		if (!(bits & 0x40))
			diga->status[0] |= IEC958_AES0_CON_EMPHASIS_5015;
		if (!(bits & 0x80))
			diga->status[1] |= IEC958_AES1_CON_ORIGINAL;
		switch (bits & 0x03) {
		case 0x00: diga->status[1] |= IEC958_AES1_CON_DAT; break;
		case 0x03: diga->status[1] |= IEC958_AES1_CON_GENERAL; break;
		}
		switch (bits & 0x06) {
		case 0x02: diga->status[3] |= IEC958_AES3_CON_FS_32000; break;
		case 0x04: diga->status[3] |= IEC958_AES3_CON_FS_48000; break;
		case 0x06: diga->status[3] |= IEC958_AES3_CON_FS_44100; break;
		}
	} else {
		diga->status[0] = IEC958_AES0_PROFESSIONAL;
		if (!(bits & 0x04))
			diga->status[0] |= IEC958_AES0_NONAUDIO;
		switch (bits & 0x60) {
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_FS_32000; break;
		case 0x40: diga->status[0] |= IEC958_AES0_PRO_FS_44100; break;
		case 0x20: diga->status[0] |= IEC958_AES0_PRO_FS_48000; break;
		case 0x60: diga->status[0] |= IEC958_AES0_PRO_FS_NOTID; break;
		}
		switch (bits & 0x03) {
		case 0x02: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NONE; break;
		case 0x01: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_5015; break;
		case 0x00: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_CCITT; break;
		case 0x03: diga->status[0] |= IEC958_AES0_PRO_EMPHASIS_NOTID; break;
		}
		if (!(bits & 0x80))
			diga->status[1] |= IEC958_AES1_PRO_MODE_STEREOPHONIC;
	}
}

SND_CS8404_DECL unsigned char SND_CS8404_ENCODE(snd_aes_iec958_t *diga)
{
	unsigned char bits;

	if (!(diga->status[0] & IEC958_AES0_PROFESSIONAL)) {
		bits = 0x10;	/* consumer mode */
		if (!(diga->status[0] & IEC958_AES0_CON_NOT_COPYRIGHT))
			bits |= 0x20;
		if ((diga->status[0] & IEC958_AES0_CON_EMPHASIS) == IEC958_AES0_CON_EMPHASIS_NONE)
			bits |= 0x40;
		if (!(diga->status[1] & IEC958_AES1_CON_ORIGINAL))
			bits |= 0x80;
		if ((diga->status[1] & IEC958_AES1_CON_CATEGORY) == IEC958_AES1_CON_GENERAL)
			bits |= 0x03;
		switch (diga->status[3] & IEC958_AES3_CON_FS) {
		default:
		case IEC958_AES3_CON_FS_44100: bits |= 0x06; break;
		case IEC958_AES3_CON_FS_48000: bits |= 0x04; break;
		case IEC958_AES3_CON_FS_32000: bits |= 0x02; break;
		}
	} else {
		bits = 0x00;	/* professional mode */
		if (!(diga->status[0] & IEC958_AES0_NONAUDIO))
			bits |= 0x04;
		switch (diga->status[0] & IEC958_AES0_PRO_FS) {
		case IEC958_AES0_PRO_FS_32000:	bits |= 0x00; break;
		case IEC958_AES0_PRO_FS_44100:	bits |= 0x40; break;	/* 44.1kHz */
		case IEC958_AES0_PRO_FS_48000:	bits |= 0x20; break;	/* 48kHz */
		default:
		case IEC958_AES0_PRO_FS_NOTID:	bits |= 0x00; break;
		}
		switch (diga->status[0] & IEC958_AES0_PRO_EMPHASIS) {
		case IEC958_AES0_PRO_EMPHASIS_NONE: bits |= 0x02; break;
		case IEC958_AES0_PRO_EMPHASIS_5015: bits |= 0x01; break;
		case IEC958_AES0_PRO_EMPHASIS_CCITT: bits |= 0x00; break;
		default:
		case IEC958_AES0_PRO_EMPHASIS_NOTID: bits |= 0x03; break;
		}
		switch (diga->status[1] & IEC958_AES1_PRO_MODE) {
		case IEC958_AES1_PRO_MODE_TWO:
		case IEC958_AES1_PRO_MODE_STEREOPHONIC: bits |= 0x00; break;
		default: bits |= 0x80; break;
		}
	}
	return bits;
}

#endif /* SND_CS8404 */

#endif /* __SOUND_CS8403_H */
