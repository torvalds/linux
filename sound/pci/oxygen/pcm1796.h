/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PCM1796_H_INCLUDED
#define PCM1796_H_INCLUDED

/* register 16 */
#define PCM1796_ATL_MASK	0xff
/* register 17 */
#define PCM1796_ATR_MASK	0xff
/* register 18 */
#define PCM1796_MUTE		0x01
#define PCM1796_DME		0x02
#define PCM1796_DMF_MASK	0x0c
#define PCM1796_DMF_DISABLED	0x00
#define PCM1796_DMF_48		0x04
#define PCM1796_DMF_441		0x08
#define PCM1796_DMF_32		0x0c
#define PCM1796_FMT_MASK	0x70
#define PCM1796_FMT_16_RJUST	0x00
#define PCM1796_FMT_20_RJUST	0x10
#define PCM1796_FMT_24_RJUST	0x20
#define PCM1796_FMT_24_LJUST	0x30
#define PCM1796_FMT_16_I2S	0x40
#define PCM1796_FMT_24_I2S	0x50
#define PCM1796_ATLD		0x80
/* register 19 */
#define PCM1796_INZD		0x01
#define PCM1796_FLT_MASK	0x02
#define PCM1796_FLT_SHARP	0x00
#define PCM1796_FLT_SLOW	0x02
#define PCM1796_DFMS		0x04
#define PCM1796_OPE		0x10
#define PCM1796_ATS_MASK	0x60
#define PCM1796_ATS_1		0x00
#define PCM1796_ATS_2		0x20
#define PCM1796_ATS_4		0x40
#define PCM1796_ATS_8		0x60
#define PCM1796_REV		0x80
/* register 20 */
#define PCM1796_OS_MASK		0x03
#define PCM1796_OS_64		0x00
#define PCM1796_OS_32		0x01
#define PCM1796_OS_128		0x02
#define PCM1796_CHSL_MASK	0x04
#define PCM1796_CHSL_LEFT	0x00
#define PCM1796_CHSL_RIGHT	0x04
#define PCM1796_MONO		0x08
#define PCM1796_DFTH		0x10
#define PCM1796_DSD		0x20
#define PCM1796_SRST		0x40
/* register 21 */
#define PCM1796_PCMZ		0x01
#define PCM1796_DZ_MASK		0x06
/* register 22 */
#define PCM1796_ZFGL		0x01
#define PCM1796_ZFGR		0x02
/* register 23 */
#define PCM1796_ID_MASK		0x1f

#endif
