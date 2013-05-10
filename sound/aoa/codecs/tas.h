/*
 * Apple Onboard Audio driver for tas codec (header)
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * GPL v2, can be found in COPYING.
 */
#ifndef __SND_AOA_CODECTASH
#define __SND_AOA_CODECTASH

#define TAS_REG_MCS	0x01	/* main control */
#	define TAS_MCS_FASTLOAD		(1<<7)
#	define TAS_MCS_SCLK64		(1<<6)
#	define TAS_MCS_SPORT_MODE_MASK	(3<<4)
#	define TAS_MCS_SPORT_MODE_I2S	(2<<4)
#	define TAS_MCS_SPORT_MODE_RJ	(1<<4)
#	define TAS_MCS_SPORT_MODE_LJ	(0<<4)
#	define TAS_MCS_SPORT_WL_MASK	(3<<0)
#	define TAS_MCS_SPORT_WL_16BIT	(0<<0)
#	define TAS_MCS_SPORT_WL_18BIT	(1<<0)
#	define TAS_MCS_SPORT_WL_20BIT	(2<<0)
#	define TAS_MCS_SPORT_WL_24BIT	(3<<0)

#define TAS_REG_DRC	0x02
#define TAS_REG_VOL	0x04
#define TAS_REG_TREBLE	0x05
#define TAS_REG_BASS	0x06
#define TAS_REG_LMIX	0x07
#define TAS_REG_RMIX	0x08

#define TAS_REG_ACR	0x40	/* analog control */
#	define TAS_ACR_B_MONAUREAL	(1<<7)
#	define TAS_ACR_B_MON_SEL_RIGHT	(1<<6)
#	define TAS_ACR_DEEMPH_MASK	(3<<2)
#	define TAS_ACR_DEEMPH_OFF	(0<<2)
#	define TAS_ACR_DEEMPH_48KHz	(1<<2)
#	define TAS_ACR_DEEMPH_44KHz	(2<<2)
#	define TAS_ACR_INPUT_B		(1<<1)
#	define TAS_ACR_ANALOG_PDOWN	(1<<0)

#define TAS_REG_MCS2	0x43	/* main control 2 */
#	define TAS_MCS2_ALLPASS		(1<<1)

#define TAS_REG_LEFT_BIQUAD6	0x10
#define TAS_REG_RIGHT_BIQUAD6	0x19

#define TAS_REG_LEFT_LOUDNESS		0x21
#define TAS_REG_RIGHT_LOUDNESS		0x22
#define TAS_REG_LEFT_LOUDNESS_GAIN	0x23
#define TAS_REG_RIGHT_LOUDNESS_GAIN	0x24

#define TAS3001_DRC_MAX		0x5f
#define TAS3004_DRC_MAX		0xef

#endif /* __SND_AOA_CODECTASH */
