/*
 *  sn95031.h - TI sn95031 Codec driver
 *
 *  Copyright (C) 2010 Intel Corp
 *  Author: Vinod Koul <vinod.koul@intel.com>
 *  Author: Harsha Priya <priya.harsha@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *
 */
#ifndef _SN95031_H
#define _SN95031_H

/*register map*/
#define SN95031_VAUD			0xDB
#define SN95031_VHSP			0xDC
#define SN95031_VHSN			0xDD
#define SN95031_VIHF			0xC9

#define SN95031_AUDPLLCTRL		0x240
#define SN95031_DMICBUF0123		0x241
#define SN95031_DMICBUF45		0x242
#define SN95031_DMICGPO			0x244
#define SN95031_DMICMUX			0x245
#define SN95031_DMICLK			0x246
#define SN95031_MICBIAS			0x247
#define SN95031_ADCCONFIG		0x248
#define SN95031_MICAMP1			0x249
#define SN95031_MICAMP2			0x24A
#define SN95031_NOISEMUX		0x24B
#define SN95031_AUDIOMUX12		0x24C
#define SN95031_AUDIOMUX34		0x24D
#define SN95031_AUDIOSINC		0x24E
#define SN95031_AUDIOTXEN		0x24F
#define SN95031_HSEPRXCTRL		0x250
#define SN95031_IHFRXCTRL		0x251
#define SN95031_HSMIXER			0x256
#define SN95031_DACCONFIG		0x257
#define SN95031_SOFTMUTE		0x258
#define SN95031_HSLVOLCTRL		0x259
#define SN95031_HSRVOLCTRL		0x25A
#define SN95031_IHFLVOLCTRL		0x25B
#define SN95031_IHFRVOLCTRL		0x25C
#define SN95031_DRIVEREN		0x25D
#define SN95031_LOCTL			0x25E
#define SN95031_VIB1C1			0x25F
#define SN95031_VIB1C2			0x260
#define SN95031_VIB1C3			0x261
#define SN95031_VIB1SPIPCM1		0x262
#define SN95031_VIB1SPIPCM2		0x263
#define SN95031_VIB1C5			0x264
#define SN95031_VIB2C1			0x265
#define SN95031_VIB2C2			0x266
#define SN95031_VIB2C3			0x267
#define SN95031_VIB2SPIPCM1		0x268
#define SN95031_VIB2SPIPCM2		0x269
#define SN95031_VIB2C5			0x26A
#define SN95031_BTNCTRL1		0x26B
#define SN95031_BTNCTRL2		0x26C
#define SN95031_PCM1TXSLOT01		0x26D
#define SN95031_PCM1TXSLOT23		0x26E
#define SN95031_PCM1TXSLOT45		0x26F
#define SN95031_PCM1RXSLOT0_3		0x270
#define SN95031_PCM1RXSLOT45		0x271
#define SN95031_PCM2TXSLOT01		0x272
#define SN95031_PCM2TXSLOT23		0x273
#define SN95031_PCM2TXSLOT45		0x274
#define SN95031_PCM2RXSLOT01		0x275
#define SN95031_PCM2RXSLOT23		0x276
#define SN95031_PCM2RXSLOT45		0x277
#define SN95031_PCM1C1			0x278
#define SN95031_PCM1C2			0x279
#define SN95031_PCM1C3			0x27A
#define SN95031_PCM2C1			0x27B
#define SN95031_PCM2C2			0x27C
/*end codec register defn*/

/*vendor defn these are not part of avp*/
#define SN95031_SSR2			0x381
#define SN95031_SSR3			0x382
#define SN95031_SSR5			0x384
#define SN95031_SSR6			0x385

#endif
