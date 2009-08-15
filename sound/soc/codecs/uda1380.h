/*
 * Audio support for Philips UDA1380
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2005 Giorgio Padrin <giorgio@mandarinlogiq.org>
 */

#ifndef _UDA1380_H
#define _UDA1380_H

#define UDA1380_CLK	0x00
#define UDA1380_IFACE	0x01
#define UDA1380_PM	0x02
#define UDA1380_AMIX	0x03
#define UDA1380_HP	0x04
#define UDA1380_MVOL	0x10
#define UDA1380_MIXVOL	0x11
#define UDA1380_MODE	0x12
#define UDA1380_DEEMP	0x13
#define UDA1380_MIXER	0x14
#define UDA1380_INTSTAT	0x18
#define UDA1380_DEC	0x20
#define UDA1380_PGA	0x21
#define UDA1380_ADC	0x22
#define UDA1380_AGC	0x23
#define UDA1380_DECSTAT	0x28
#define UDA1380_RESET	0x7f

#define UDA1380_CACHEREGNUM 0x24

/* Register flags */
#define R00_EN_ADC	0x0800
#define R00_EN_DEC	0x0400
#define R00_EN_DAC	0x0200
#define R00_EN_INT	0x0100
#define R00_DAC_CLK	0x0010
#define R01_SFORI_I2S   0x0000
#define R01_SFORI_LSB16 0x0100
#define R01_SFORI_LSB18 0x0200
#define R01_SFORI_LSB20 0x0300
#define R01_SFORI_MSB   0x0500
#define R01_SFORI_MASK  0x0700
#define R01_SFORO_I2S   0x0000
#define R01_SFORO_LSB16 0x0001
#define R01_SFORO_LSB18 0x0002
#define R01_SFORO_LSB20 0x0003
#define R01_SFORO_LSB24 0x0004
#define R01_SFORO_MSB   0x0005
#define R01_SFORO_MASK  0x0007
#define R01_SEL_SOURCE  0x0040
#define R01_SIM		0x0010
#define R02_PON_PLL	0x8000
#define R02_PON_HP	0x2000
#define R02_PON_DAC	0x0400
#define R02_PON_BIAS	0x0100
#define R02_EN_AVC	0x0080
#define R02_PON_AVC	0x0040
#define R02_PON_LNA	0x0010
#define R02_PON_PGAL	0x0008
#define R02_PON_ADCL	0x0004
#define R02_PON_PGAR	0x0002
#define R02_PON_ADCR	0x0001
#define R13_MTM		0x4000
#define R14_SILENCE	0x0080
#define R14_SDET_ON	0x0040
#define R21_MT_ADC	0x8000
#define R22_SEL_LNA	0x0008
#define R22_SEL_MIC	0x0004
#define R22_SKIP_DCFIL	0x0002
#define R23_AGC_EN	0x0001

#define UDA1380_DAI_DUPLEX	0 /* playback and capture on single DAI */
#define UDA1380_DAI_PLAYBACK	1 /* playback DAI */
#define UDA1380_DAI_CAPTURE	2 /* capture DAI */

extern struct snd_soc_dai uda1380_dai[3];
extern struct snd_soc_codec_device soc_codec_dev_uda1380;

#endif /* _UDA1380_H */
