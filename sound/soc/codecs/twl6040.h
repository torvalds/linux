/*
 * ALSA SoC TWL6040 codec driver
 *
 * Author:	Misael Lopez Cruz <x0052729@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __TWL6040_H__
#define __TWL6040_H__

#define TWL6040_REG_ASICID		0x01
#define TWL6040_REG_ASICREV		0x02
#define TWL6040_REG_INTID		0x03
#define TWL6040_REG_INTMR		0x04
#define TWL6040_REG_NCPCTL		0x05
#define TWL6040_REG_LDOCTL		0x06
#define TWL6040_REG_HPPLLCTL		0x07
#define TWL6040_REG_LPPLLCTL		0x08
#define TWL6040_REG_LPPLLDIV		0x09
#define TWL6040_REG_AMICBCTL		0x0A
#define TWL6040_REG_DMICBCTL		0x0B
#define TWL6040_REG_MICLCTL		0x0C
#define TWL6040_REG_MICRCTL		0x0D
#define TWL6040_REG_MICGAIN		0x0E
#define TWL6040_REG_LINEGAIN		0x0F
#define TWL6040_REG_HSLCTL		0x10
#define TWL6040_REG_HSRCTL		0x11
#define TWL6040_REG_HSGAIN		0x12
#define TWL6040_REG_EARCTL		0x13
#define TWL6040_REG_HFLCTL		0x14
#define TWL6040_REG_HFLGAIN		0x15
#define TWL6040_REG_HFRCTL		0x16
#define TWL6040_REG_HFRGAIN		0x17
#define TWL6040_REG_VIBCTLL		0x18
#define TWL6040_REG_VIBDATL		0x19
#define TWL6040_REG_VIBCTLR		0x1A
#define TWL6040_REG_VIBDATR		0x1B
#define TWL6040_REG_HKCTL1		0x1C
#define TWL6040_REG_HKCTL2		0x1D
#define TWL6040_REG_GPOCTL		0x1E
#define TWL6040_REG_ALB			0x1F
#define TWL6040_REG_DLB			0x20
#define TWL6040_REG_TRIM1		0x28
#define TWL6040_REG_TRIM2		0x29
#define TWL6040_REG_TRIM3		0x2A
#define TWL6040_REG_HSOTRIM		0x2B
#define TWL6040_REG_HFOTRIM		0x2C
#define TWL6040_REG_ACCCTL		0x2D
#define TWL6040_REG_STATUS		0x2E

#define TWL6040_CACHEREGNUM		(TWL6040_REG_STATUS + 1)

#define TWL6040_VIOREGNUM		18
#define TWL6040_VDDREGNUM		21

/* INTID (0x03) fields */

#define TWL6040_THINT			0x01
#define TWL6040_PLUGINT			0x02
#define TWL6040_UNPLUGINT		0x04
#define TWL6040_HOOKINT			0x08
#define TWL6040_HFINT			0x10
#define TWL6040_VIBINT			0x20
#define TWL6040_READYINT		0x40

/* INTMR (0x04) fields */

#define TWL6040_READYMSK		0x40
#define TWL6040_ALLINT_MSK		0x7B

/* NCPCTL (0x05) fields */

#define TWL6040_NCPENA			0x01
#define TWL6040_NCPOPEN			0x40

/* LDOCTL (0x06) fields */

#define TWL6040_LSLDOENA		0x01
#define TWL6040_HSLDOENA		0x04
#define TWL6040_REFENA			0x40
#define TWL6040_OSCENA			0x80

/* HPPLLCTL (0x07) fields */

#define TWL6040_HPLLENA			0x01
#define TWL6040_HPLLRST			0x02
#define TWL6040_HPLLBP			0x04
#define TWL6040_HPLLSQRENA		0x08
#define TWL6040_HPLLSQRBP		0x10
#define TWL6040_MCLK_12000KHZ		(0 << 5)
#define TWL6040_MCLK_19200KHZ		(1 << 5)
#define TWL6040_MCLK_26000KHZ		(2 << 5)
#define TWL6040_MCLK_38400KHZ		(3 << 5)
#define TWL6040_MCLK_MSK		0x60

/* LPPLLCTL (0x08) fields */

#define TWL6040_LPLLENA			0x01
#define TWL6040_LPLLRST			0x02
#define TWL6040_LPLLSEL			0x04
#define TWL6040_LPLLFIN			0x08
#define TWL6040_HPLLSEL			0x10

/* HSLCTL (0x10) fields */

#define TWL6040_HSDACMODEL		0x02
#define TWL6040_HSDRVMODEL		0x08

/* HSRCTL (0x11) fields */

#define TWL6040_HSDACMODER		0x02
#define TWL6040_HSDRVMODER		0x08

/* ACCCTL (0x2D) fields */

#define TWL6040_RESETSPLIT		0x04

#define TWL6040_SYSCLK_SEL_LPPLL	1
#define TWL6040_SYSCLK_SEL_HPPLL	2

#define TWL6040_HPPLL_ID		1
#define TWL6040_LPPLL_ID		2

extern struct snd_soc_dai twl6040_dai;
extern struct snd_soc_codec_device soc_codec_dev_twl6040;

#endif /* End of __TWL6040_H__ */
