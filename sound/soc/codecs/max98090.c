/*
 * max98090.c -- MAX98090 ALSA SoC Audio driver
 *
 * Copyright 2011 Maxim Integrated Products
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/switch.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include <sound/max98090.h>
#include "max98090.h"

static const u8 max98090_reg_def[M98090_REG_CNT] = {
	0x00, /* 00 */
	0x00, /* 01 */
	0x00, /* 02 */
	0x00, /* 03 */
	0x00, /* 04 */
	0x00, /* 05 */
	0x00, /* 06 */
	0x00, /* 07 */
	0x00, /* 08 */
	0x00, /* 09 */
	0x00, /* 0A */
	0x00, /* 0B */
	0x00, /* 0C */
	0x00, /* 0D */
	0x00, /* 0E */
	0x00, /* 0F */
	0x00, /* 10 */
	0x00, /* 11 */
	0x00, /* 12 */
	0x00, /* 13 */
	0x00, /* 14 */
	0x00, /* 15 */
	0x00, /* 16 */
	0x00, /* 17 */
	0x00, /* 18 */
	0x00, /* 19 */
	0x00, /* 1A */
	0x00, /* 1B */
	0x00, /* 1C */
	0x00, /* 1D */
	0x00, /* 1E */
	0x00, /* 1F */
	0x00, /* 20 */
	0x00, /* 21 */
	0x00, /* 22 */
	0x00, /* 23 */
	0x00, /* 24 */
	0x00, /* 25 */
	0x00, /* 26 */
	0x00, /* 27 */
	0x00, /* 28 */
	0x00, /* 29 */
	0x00, /* 2A */
	0x00, /* 2B */
	0x00, /* 2C */
	0x00, /* 2D */
	0x00, /* 2E */
	0x00, /* 2F */
	0x00, /* 30 */
	0x00, /* 31 */
	0x00, /* 32 */
	0x00, /* 33 */
	0x00, /* 34 */
	0x00, /* 35 */
	0x00, /* 36 */
	0x00, /* 37 */
	0x00, /* 38 */
	0x00, /* 39 */
	0x00, /* 3A */
	0x00, /* 3B */
	0x00, /* 3C */
	0x00, /* 3D */
	0x00, /* 3E */
	0x00, /* 3F */
	0x00, /* 40 */
	0x00, /* 41 */
	0x00, /* 42 */
	0x00, /* 43 */
	0x00, /* 44 */
	0x00, /* 45 */
	0x00, /* 46 */
	0x00, /* 47 */
	0x00, /* 48 */
	0x00, /* 49 */
	0x00, /* 4A */
	0x00, /* 4B */
	0x00, /* 4C */
	0x00, /* 4D */
	0x00, /* 4E */
	0x00, /* 4F */
	0x00, /* 50 */
	0x00, /* 51 */
	0x00, /* 52 */
	0x00, /* 53 */
	0x00, /* 54 */
	0x00, /* 55 */
	0x00, /* 56 */
	0x00, /* 57 */
	0x00, /* 58 */
	0x00, /* 59 */
	0x00, /* 5A */
	0x00, /* 5B */
	0x00, /* 5C */
	0x00, /* 5D */
	0x00, /* 5E */
	0x00, /* 5F */
	0x00, /* 60 */
	0x00, /* 61 */
	0x00, /* 62 */
	0x00, /* 63 */
	0x00, /* 64 */
	0x00, /* 65 */
	0x00, /* 66 */
	0x00, /* 67 */
	0x00, /* 68 */
	0x00, /* 69 */
	0x00, /* 6A */
	0x00, /* 6B */
	0x00, /* 6C */
	0x00, /* 6D */
	0x00, /* 6E */
	0x00, /* 6F */
	0x00, /* 70 */
	0x00, /* 71 */
	0x00, /* 72 */
	0x00, /* 73 */
	0x00, /* 74 */
	0x00, /* 75 */
	0x00, /* 76 */
	0x00, /* 77 */
	0x00, /* 78 */
	0x00, /* 79 */
	0x00, /* 7A */
	0x00, /* 7B */
	0x00, /* 7C */
	0x00, /* 7D */
	0x00, /* 7E */
	0x00, /* 7F */
	0x00, /* 80 */
	0x00, /* 81 */
	0x00, /* 82 */
	0x00, /* 83 */
	0x00, /* 84 */
	0x00, /* 85 */
	0x00, /* 86 */
	0x00, /* 87 */
	0x00, /* 88 */
	0x00, /* 89 */
	0x00, /* 8A */
	0x00, /* 8B */
	0x00, /* 8C */
	0x00, /* 8D */
	0x00, /* 8E */
	0x00, /* 8F */
	0x00, /* 90 */
	0x00, /* 91 */
	0x30, /* 92 */
	0xF0, /* 93 */
	0x00, /* 94 */
	0x00, /* 95 */
	0x3F, /* 96 */
	0x00, /* 97 */
	0x00, /* 98 */
	0x00, /* 99 */
	0x00, /* 9A */
	0x00, /* 9B */
	0x00, /* 9C */
	0x00, /* 9D */
	0x00, /* 9E */
	0x00, /* 9F */
	0x00, /* A0 */
	0x00, /* A1 */
	0x00, /* A2 */
	0x00, /* A3 */
	0x00, /* A4 */
	0x00, /* A5 */
	0x00, /* A6 */
	0x00, /* A7 */
	0x00, /* A8 */
	0x00, /* A9 */
	0x00, /* AA */
	0x00, /* AB */
	0x00, /* AC */
	0x00, /* AD */
	0x00, /* AE */
	0x00, /* AF */
	0x00, /* B0 */
	0x00, /* B1 */
	0x00, /* B2 */
	0x00, /* B3 */
	0x00, /* B4 */
	0x00, /* B5 */
	0x00, /* B6 */
	0x00, /* B7 */
	0x00, /* B8 */
	0x00, /* B9 */
	0x00, /* BA */
	0x00, /* BB */
	0x00, /* BC */
	0x00, /* BD */
	0x00, /* BE */
	0x00, /* BF */
	0x00, /* C0 */
	0x00, /* C1 */
	0x00, /* C2 */
	0x00, /* C3 */
	0x00, /* C4 */
	0x00, /* C5 */
	0x00, /* C6 */
	0x00, /* C7 */
	0x00, /* C8 */
	0x00, /* C9 */
	0x00, /* CA */
	0x00, /* CB */
	0x00, /* CC */
	0x00, /* CD */
	0x00, /* CE */
	0x00, /* CF */
	0x00, /* D0 */
	0x00, /* D1 */
	0x00, /* D2 */
	0x00, /* D3 */
	0x00, /* D4 */
	0x00, /* D5 */
	0x00, /* D6 */
	0x00, /* D7 */
	0x00, /* D8 */
	0x00, /* D9 */
	0x00, /* DA */
	0x00, /* DB */
	0x00, /* DC */
	0x00, /* DD */
	0x00, /* DE */
	0x00, /* DF */
	0x00, /* E0 */
	0x00, /* E1 */
	0x00, /* E2 */
	0x00, /* E3 */
	0x00, /* E4 */
	0x00, /* E5 */
	0x00, /* E6 */
	0x00, /* E7 */
	0x00, /* E8 */
	0x00, /* E9 */
	0x00, /* EA */
	0x00, /* EB */
	0x00, /* EC */
	0x00, /* ED */
	0x00, /* EE */
	0x00, /* EF */
	0x00, /* F0 */
	0x00, /* F1 */
	0x00, /* F2 */
	0x00, /* F3 */
	0x00, /* F4 */
	0x00, /* F5 */
	0x00, /* F6 */
	0x00, /* F7 */
	0x00, /* F8 */
	0x00, /* F9 */
	0x00, /* FA */
	0x00, /* FB */
	0x00, /* FC */
	0x00, /* FD */
	0x00, /* FE */
	0x00, /* FF */
};


static struct {
	int readable;
	int writable;
} max98090_access[M98090_REG_CNT] = {
	{ 0x00, 0x80 }, /* 00 */
	{ 0xFF, 0x00 }, /* 01 */
	{ 0xFF, 0x00 }, /* 02 */
	{ 0xFF, 0xE7 }, /* 03 */
	{ 0xFF, 0xFD }, /* 04 */
	{ 0xFF, 0x3F }, /* 05 */
	{ 0xFF, 0x3F }, /* 06 */
	{ 0xFF, 0xF0 }, /* 07 */
	{ 0xFF, 0xFE }, /* 08 */
	{ 0xFF, 0xF8 }, /* 09 */
	{ 0xFF, 0xFF }, /* 0A */
	{ 0xFF, 0xFF }, /* 0B */
	{ 0xFF, 0xF0 }, /* 0C */
	{ 0xFF, 0xFF }, /* 0D */
	{ 0xFF, 0xFF }, /* 0E */
	{ 0xFF, 0xD3 }, /* 0F */
	{ 0xFF, 0x7F }, /* 10 */
	{ 0xFF, 0x7F }, /* 11 */
	{ 0xFF, 0x00 }, /* 12 */
	{ 0xFF, 0x00 }, /* 13 */
	{ 0xFF, 0x00 }, /* 14 */
	{ 0xFF, 0xFF }, /* 15 */
	{ 0xFF, 0xFF }, /* 16 */
	{ 0xFF, 0x3F }, /* 17 */
	{ 0xFF, 0x3F }, /* 18 */
	{ 0xFF, 0x0F }, /* 19 */
	{ 0xFF, 0xDF }, /* 1A */
	{ 0xFF, 0x30 }, /* 1B */
	{ 0xFF, 0xF1 }, /* 1C */
	{ 0xFF, 0x7F }, /* 1D */
	{ 0xFF, 0xFF }, /* 1E */
	{ 0xFF, 0xFF }, /* 1F */
	{ 0xFF, 0xFF }, /* 20 */
	{ 0xFF, 0x87 }, /* 21 */
	{ 0xFF, 0x3F }, /* 22 */
	{ 0xFF, 0x03 }, /* 23 */
	{ 0xFF, 0xFF }, /* 24 */
	{ 0xFF, 0x3F }, /* 25 */
	{ 0xFF, 0xF0 }, /* 26 */
	{ 0xFF, 0xBF }, /* 27 */
	{ 0xFF, 0x1F }, /* 28 */
	{ 0xFF, 0x3F }, /* 29 */
	{ 0xFF, 0x3F }, /* 2A */
	{ 0xFF, 0x3F }, /* 2B */
	{ 0xFF, 0x9F }, /* 2C */
	{ 0xFF, 0x9F }, /* 2D */
	{ 0xFF, 0x3F }, /* 2E */
	{ 0xFF, 0x3F }, /* 2F */
	{ 0xFF, 0x0F }, /* 30 */
	{ 0xFF, 0xBF }, /* 31 */
	{ 0xFF, 0xBF }, /* 32 */
	{ 0xFF, 0xF7 }, /* 33 */
	{ 0xFF, 0xFF }, /* 34 */
	{ 0xFF, 0xFF }, /* 35 */
	{ 0xFF, 0x1F }, /* 36 */
	{ 0xFF, 0x3F }, /* 37 */
	{ 0xFF, 0x03 }, /* 38 */
	{ 0xFF, 0x9F }, /* 39 */
	{ 0xFF, 0xBF }, /* 3A */
	{ 0xFF, 0x03 }, /* 3B */
	{ 0xFF, 0x9F }, /* 3C */
	{ 0xFF, 0xC3 }, /* 3D */
	{ 0xFF, 0x1F }, /* 3E */
	{ 0xFF, 0xFF }, /* 3F */
	{ 0xFF, 0x07 }, /* 40 */
	{ 0xFF, 0x0F }, /* 41 */
	{ 0xFF, 0x01 }, /* 42 */
	{ 0xFF, 0x03 }, /* 43 */
	{ 0xFF, 0x05 }, /* 44 */
	{ 0xFF, 0x80 }, /* 45 */
	{ 0x00, 0x00 }, /* 46 */
	{ 0x00, 0x00 }, /* 47 */
	{ 0x00, 0x00 }, /* 48 */
	{ 0x00, 0x00 }, /* 49 */
	{ 0x00, 0x00 }, /* 4A */
	{ 0x00, 0x00 }, /* 4B */
	{ 0x00, 0x00 }, /* 4C */
	{ 0x00, 0x00 }, /* 4D */
	{ 0x00, 0x00 }, /* 4E */
	{ 0x00, 0x00 }, /* 4F */
	{ 0x00, 0x00 }, /* 50 */
	{ 0x00, 0x00 }, /* 51 */
	{ 0x00, 0x00 }, /* 52 */
	{ 0x00, 0x00 }, /* 53 */
	{ 0x00, 0x00 }, /* 54 */
	{ 0x00, 0x00 }, /* 55 */
	{ 0x00, 0x00 }, /* 56 */
	{ 0x00, 0x00 }, /* 57 */
	{ 0x00, 0x00 }, /* 58 */
	{ 0x00, 0x00 }, /* 59 */
	{ 0x00, 0x00 }, /* 5A */
	{ 0x00, 0x00 }, /* 5B */
	{ 0x00, 0x00 }, /* 5C */
	{ 0x00, 0x00 }, /* 5D */
	{ 0x00, 0x00 }, /* 5E */
	{ 0x00, 0x00 }, /* 5F */
	{ 0x00, 0x00 }, /* 60 */
	{ 0x00, 0x00 }, /* 61 */
	{ 0x00, 0x00 }, /* 62 */
	{ 0x00, 0x00 }, /* 63 */
	{ 0x00, 0x00 }, /* 64 */
	{ 0x00, 0x00 }, /* 65 */
	{ 0x00, 0x00 }, /* 66 */
	{ 0x00, 0x00 }, /* 67 */
	{ 0x00, 0x00 }, /* 68 */
	{ 0x00, 0x00 }, /* 69 */
	{ 0x00, 0x00 }, /* 6A */
	{ 0x00, 0x00 }, /* 6B */
	{ 0x00, 0x00 }, /* 6C */
	{ 0x00, 0x00 }, /* 6D */
	{ 0x00, 0x00 }, /* 6E */
	{ 0x00, 0x00 }, /* 6F */
	{ 0x00, 0x00 }, /* 70 */
	{ 0x00, 0x00 }, /* 71 */
	{ 0x00, 0x00 }, /* 72 */
	{ 0x00, 0x00 }, /* 73 */
	{ 0x00, 0x00 }, /* 74 */
	{ 0x00, 0x00 }, /* 75 */
	{ 0x00, 0x00 }, /* 76 */
	{ 0x00, 0x00 }, /* 77 */
	{ 0x00, 0x00 }, /* 78 */
	{ 0x00, 0x00 }, /* 79 */
	{ 0x00, 0x00 }, /* 7A */
	{ 0x00, 0x00 }, /* 7B */
	{ 0x00, 0x00 }, /* 7C */
	{ 0x00, 0x00 }, /* 7D */
	{ 0x00, 0x00 }, /* 7E */
	{ 0x00, 0x00 }, /* 7F */
	{ 0x00, 0x00 }, /* 80 */
	{ 0x00, 0x00 }, /* 81 */
	{ 0x00, 0x00 }, /* 82 */
	{ 0x00, 0x00 }, /* 83 */
	{ 0x00, 0x00 }, /* 84 */
	{ 0x00, 0x00 }, /* 85 */
	{ 0x00, 0x00 }, /* 86 */
	{ 0x00, 0x00 }, /* 87 */
	{ 0x00, 0x00 }, /* 88 */
	{ 0x00, 0x00 }, /* 89 */
	{ 0x00, 0x00 }, /* 8A */
	{ 0x00, 0x00 }, /* 8B */
	{ 0x00, 0x00 }, /* 8C */
	{ 0x00, 0x00 }, /* 8D */
	{ 0x00, 0x00 }, /* 8E */
	{ 0x00, 0x00 }, /* 8F */
	{ 0x00, 0x00 }, /* 90 */
	{ 0x00, 0x00 }, /* 91 */
	{ 0x00, 0x00 }, /* 92 */
	{ 0x00, 0x00 }, /* 93 */
	{ 0x00, 0x00 }, /* 94 */
	{ 0x00, 0x00 }, /* 95 */
	{ 0x00, 0x00 }, /* 96 */
	{ 0x00, 0x00 }, /* 97 */
	{ 0x00, 0x00 }, /* 98 */
	{ 0x00, 0x00 }, /* 99 */
	{ 0x00, 0x00 }, /* 9A */
	{ 0x00, 0x00 }, /* 9B */
	{ 0x00, 0x00 }, /* 9C */
	{ 0x00, 0x00 }, /* 9D */
	{ 0x00, 0x00 }, /* 9E */
	{ 0x00, 0x00 }, /* 9F */
	{ 0x00, 0x00 }, /* A0 */
	{ 0x00, 0x00 }, /* A1 */
	{ 0x00, 0x00 }, /* A2 */
	{ 0x00, 0x00 }, /* A3 */
	{ 0x00, 0x00 }, /* A4 */
	{ 0x00, 0x00 }, /* A5 */
	{ 0x00, 0x00 }, /* A6 */
	{ 0x00, 0x00 }, /* A7 */
	{ 0x00, 0x00 }, /* A8 */
	{ 0x00, 0x00 }, /* A9 */
	{ 0x00, 0x00 }, /* AA */
	{ 0x00, 0x00 }, /* AB */
	{ 0x00, 0x00 }, /* AC */
	{ 0x00, 0x00 }, /* AD */
	{ 0x00, 0x00 }, /* AE */
	{ 0x00, 0x00 }, /* AF */
	{ 0x00, 0x00 }, /* B0 */
	{ 0x00, 0x00 }, /* B1 */
	{ 0x00, 0x00 }, /* B2 */
	{ 0x00, 0x00 }, /* B3 */
	{ 0x00, 0x00 }, /* B4 */
	{ 0x00, 0x00 }, /* B5 */
	{ 0x00, 0x00 }, /* B6 */
	{ 0x00, 0x00 }, /* B7 */
	{ 0x00, 0x00 }, /* B8 */
	{ 0x00, 0x00 }, /* B9 */
	{ 0x00, 0x00 }, /* BA */
	{ 0x00, 0x00 }, /* BB */
	{ 0x00, 0x00 }, /* BC */
	{ 0x00, 0x00 }, /* BD */
	{ 0x00, 0x00 }, /* BE */
	{ 0x00, 0x00 }, /* BF */
	{ 0x00, 0x00 }, /* C0 */
	{ 0x00, 0x00 }, /* C1 */
	{ 0x00, 0x00 }, /* C2 */
	{ 0x00, 0x00 }, /* C3 */
	{ 0x00, 0x00 }, /* C4 */
	{ 0x00, 0x00 }, /* C5 */
	{ 0x00, 0x00 }, /* C6 */
	{ 0x00, 0x00 }, /* C7 */
	{ 0x00, 0x00 }, /* C8 */
	{ 0x00, 0x00 }, /* C9 */
	{ 0x00, 0x00 }, /* CA */
	{ 0x00, 0x00 }, /* CB */
	{ 0x00, 0x00 }, /* CC */
	{ 0x00, 0x00 }, /* CD */
	{ 0x00, 0x00 }, /* CE */
	{ 0x00, 0x00 }, /* CF */
	{ 0x00, 0x00 }, /* D0 */
	{ 0x00, 0x00 }, /* D1 */
	{ 0x00, 0x00 }, /* D2 */
	{ 0x00, 0x00 }, /* D3 */
	{ 0x00, 0x00 }, /* D4 */
	{ 0x00, 0x00 }, /* D5 */
	{ 0x00, 0x00 }, /* D6 */
	{ 0x00, 0x00 }, /* D7 */
	{ 0x00, 0x00 }, /* D8 */
	{ 0x00, 0x00 }, /* D9 */
	{ 0x00, 0x00 }, /* DA */
	{ 0x00, 0x00 }, /* DB */
	{ 0x00, 0x00 }, /* DC */
	{ 0x00, 0x00 }, /* DD */
	{ 0x00, 0x00 }, /* DE */
	{ 0x00, 0x00 }, /* DF */
	{ 0x00, 0x00 }, /* E0 */
	{ 0x00, 0x00 }, /* E1 */
	{ 0x00, 0x00 }, /* E2 */
	{ 0x00, 0x00 }, /* E3 */
	{ 0x00, 0x00 }, /* E4 */
	{ 0x00, 0x00 }, /* E5 */
	{ 0x00, 0x00 }, /* E6 */
	{ 0x00, 0x00 }, /* E7 */
	{ 0x00, 0x00 }, /* E8 */
	{ 0x00, 0x00 }, /* E9 */
	{ 0x00, 0x00 }, /* EA */
	{ 0x00, 0x00 }, /* EB */
	{ 0x00, 0x00 }, /* EC */
	{ 0x00, 0x00 }, /* ED */
	{ 0x00, 0x00 }, /* EE */
	{ 0x00, 0x00 }, /* EF */
	{ 0x00, 0x00 }, /* F0 */
	{ 0x00, 0x00 }, /* F1 */
	{ 0x00, 0x00 }, /* F2 */
	{ 0x00, 0x00 }, /* F3 */
	{ 0x00, 0x00 }, /* F4 */
	{ 0x00, 0x00 }, /* F5 */
	{ 0x00, 0x00 }, /* F6 */
	{ 0x00, 0x00 }, /* F7 */
	{ 0x00, 0x00 }, /* F8 */
	{ 0x00, 0x00 }, /* F9 */
	{ 0x00, 0x00 }, /* FA */
	{ 0x00, 0x00 }, /* FB */
	{ 0x00, 0x00 }, /* FC */
	{ 0x00, 0x00 }, /* FD */
	{ 0x00, 0x00 }, /* FE */
	{ 0xFF, 0x00 }, /* FF */
};

static int max98090_readable(struct snd_soc_codec *codec, unsigned int reg)
{
	if (reg >= M98090_REG_CNT)
		return 0;
	return max98090_access[reg].readable != 0;
}

static int max98090_volatile(struct snd_soc_codec *codec, unsigned int reg)
{
	if (reg > M98090_REG_MAX_CACHED)
		return 1;

	switch (reg) {
	case M98090_001_INT_STS:
	case M98090_002_JACK_STS:
		return 1;
	}

	return 0;
}
//------------------------------------------------
// Implementation of I2C functions
//------------------------------------------------
inline unsigned int max98090_i2c_read(struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(codec->control_data, (u8)(reg & 0xFF));

	if (ret < 0)
		printk("DEBUG -> %s error!!! [%d]\n",__FUNCTION__,__LINE__);
	return ret;
}

/*
 * write max98090 register cache
 */
static inline void max98090_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;

	/* Reset register and reserved registers are uncached */
	if (reg == 0 || reg > ARRAY_SIZE(max98090_reg_def) - 1)
		return;

	cache[reg] = value;
}

/*
 * write to the max98090 register space
 */
static int max98090_i2c_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	max98090_write_reg_cache (codec, reg, value);

	if(i2c_smbus_write_byte_data(codec->control_data, (u8)(reg & 0xFF), (u8)(value & 0xFF))<0) {
		printk("%s error!!! [%d]\n",__FUNCTION__,__LINE__);
		return -EIO;
	}
	
	return 0;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------

static const char *playback_path[] = {"OFF", "RCV", "SPK", "HP", "SPK_HP", "TV_OUT", };
static const char *record_path[] = {"Main Mic", "Headset Mic", };

static const struct soc_enum path_control_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(playback_path),playback_path),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(record_path),record_path),
};

#if defined(CONFIG_MAX98090_HEADSET)
enum {
	JACK_NO_DEVICE			= 0x0,
	HEADSET_4_POLE_DEVICE	= 0x01 << 0,
	HEADSET_3_POLE_DEVICE	= 0x01 << 1,

	TVOUT_DEVICE			= 0x01 << 2,
	UNKNOWN_DEVICE			= 0x01 << 3,
};

enum {
	JACK_DETACHED		= 0x0,
	JACK_ATTACHED		= 0x1,
};

struct switch_dev switch_jack_detection = {
	.name = "h2w",
};
static unsigned int current_jack_type_status = UNKNOWN_DEVICE;
#endif
//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
static int max98090_set_playback_path(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);

	int path_num = ucontrol->value.integer.value[0];

	max98090->cur_path = path_num;
	
	switch(path_num) {
		case 0 : // headset
		case 1 : // earpiece
//			max98090_set_playback_headset(codec);
			max98090_set_playback_speaker_headset(codec);
			break;
		case 2 : // speaker
//			max98090_set_playback_speaker(codec);
			max98090_set_playback_speaker_headset(codec);
			break;
		case 3 :
			max98090_set_playback_speaker_headset(codec);
			break;
		case 4 :
			break;
		default :
			break;
	}
//	max98090_set_record_main_mic(codec);
	return 0;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
static int max98090_get_playback_path(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);
	
	printk(" %s [%d] current playback path %d\n",__FUNCTION__,__LINE__, max98090->cur_path);

	ucontrol->value.integer.value[0] = max98090->cur_path;

	return 0;
}

//--------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------
static int max98090_set_record_path(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);

	int path_num = ucontrol->value.integer.value[0];
	
	max98090->rec_path = path_num;
	printk(" %s [%d] param %d\n",__FUNCTION__,__LINE__, path_num);

	switch(path_num) {
		case 0 :
			max98090_set_record_main_mic(codec);
			break;
		case 1 :
			max98090_set_record_main_mic(codec);
			break;
		case 2 :
			max98090_set_record_main_mic(codec);
			break;
		default :
			break;
	}

	return 0;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
static int max98090_get_record_path(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);

	printk(" %s [%d] current record path %d\n",__FUNCTION__,__LINE__, max98090->rec_path);

	ucontrol->value.integer.value[0] = max98090->rec_path;

	return 0;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
static const struct snd_kcontrol_new max98090_snd_controls[] = {
	/* Path Control */
	SOC_ENUM_EXT("Playback Path", path_control_enum[0],
                max98090_get_playback_path, max98090_set_playback_path),

	SOC_ENUM_EXT("MIC Path", path_control_enum[1],
                max98090_get_record_path, max98090_set_record_path),
};

static int max98090_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_add_codec_controls(codec, max98090_snd_controls,
									     ARRAY_SIZE(max98090_snd_controls));
	return 0;
}

#if defined(CONFIG_MAX98090_HEADSET) 
//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
#define MAX98090_DELAY	msecs_to_jiffies(1000)
static void max98090_work(struct work_struct *work)
{
	struct max98090_priv *max98090 = container_of(work, struct max98090_priv, work.work);
	struct snd_soc_codec *codec= max98090->codec;
	int read_value=0;
//	int jack_auto_sts=0;

//	jack_auto_sts = snd_soc_read(codec, M98090_002_JACK_STS);

//printk("CKKIM -> %s[%d] : jack_auto_sts = 0x%02x \n\n",__func__,__LINE__, jack_auto_sts);
//	if(jack_man_sts&0x18){
		read_value = JACK_NO_DEVICE;
//	}
//	else if(jack_auto_sts&0x08){
//		read_value = HEADSET_4_POLE_DEVICE;
//	}
//	else {
//		read_value = JACK_NO_DEVICE;
//	}
	if(read_value != current_jack_type_status)
	{
		current_jack_type_status=read_value;
		switch_set_state(&switch_jack_detection, current_jack_type_status);
		mdelay(500);
		switch(current_jack_type_status)
		{
			case HEADSET_3_POLE_DEVICE :
			case HEADSET_4_POLE_DEVICE :
				max98090_disable_playback_path(codec, SPK);
				max98090_set_playback_speaker_headset(codec);
				max98090->cur_path = HP;
				break;
			case JACK_NO_DEVICE :
				max98090_disable_playback_path(codec, HP);
				max98090_set_playback_speaker_headset(codec);
				max98090->cur_path = SPK;
				break;
			default :
				max98090_disable_playback_path(codec, SPK);
				max98090_set_playback_speaker_headset(codec);
				max98090->cur_path = HP;
				break;
		}
		schedule_delayed_work(&max98090->work, msecs_to_jiffies(2000));
	}
	else schedule_delayed_work(&max98090->work, msecs_to_jiffies(1000));

}

void	odroid_audio_tvout(bool tvout)
{
	return;
}
#endif

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------
/* codec mclk clock divider coefficients */
static const struct {
	u32 rate;
	u8  sr;
} rate_table[] = {
	{8000,  M98090_QS_SR_8K},
	{16000, M98090_QS_SR_16K},
	{32000, M98090_QS_SR_32K},
	{44100, M98090_QS_SR_44K1},
	{48000, M98090_QS_SR_48K},
	{96000, M98090_QS_SR_96K},
};

static int rate_value(int rate, u8 *value)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].rate >= rate) {
			*value = rate_table[i].sr;
			return 0;
		}
	}
	*value = rate_table[0].sr;
	return -EINVAL;
}

static int max98090_dai1_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);
	struct max98090_cdata *cdata;
	unsigned int rate;
	u8 regval;

	cdata = &max98090->dai;
	rate = params_rate(params);

	cdata->rate = rate;

	rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_update_bits(codec, M98090_022_DAI_IF_FORMAT,
			M98090_DAI_WS, 0);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		snd_soc_update_bits(codec, M98090_022_DAI_IF_FORMAT,
			M98090_DAI_WS, 2);
		break;
	default:
		return -EINVAL;
	}

	if (rate_value(rate, &regval))
		return -EINVAL;

	snd_soc_update_bits(codec, M98090_005_SAMPLERATE_QS,
		M98090_QS_SR_MASK, regval);

	cdata->rate = rate;

	/* Update sample rate mode */
	if (rate < 50000)
		snd_soc_update_bits(codec, M98090_026_FILTER_CONFIG,
			M98090_FILTER_DHF, 0);
	else
		snd_soc_update_bits(codec, M98090_026_FILTER_CONFIG,
			M98090_FILTER_DHF, M98090_FILTER_DHF);

	snd_soc_update_bits(codec, M98090_026_FILTER_CONFIG,
		M98090_FILTER_MODE, 0);

	return 0;
}

static int max98090_dai_set_sysclk(struct snd_soc_dai *dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);

	/* Requested clock frequency is already setup */
	if (freq == max98090->sysclk)
		return 0;

	max98090->sysclk = freq; /* remember current sysclk */

	/* Setup clocks for slave mode, and using the PLL
	 * PSCLK = 0x01 (when master clk is 10MHz to 20MHz)
	 *         0x02 (when master clk is 20MHz to 40MHz)..
	 *         0x03 (when master clk is 40MHz to 60MHz)..
	 */
	if ((freq >= 10000000) && (freq < 20000000)) {
		snd_soc_write(codec, M98090_01B_SYS_CLK, 0x10);
	} else if ((freq >= 20000000) && (freq < 40000000)) {
		snd_soc_write(codec, M98090_01B_SYS_CLK, 0x20);
	} else if ((freq >= 40000000) && (freq < 60000000)) {
		snd_soc_write(codec, M98090_01B_SYS_CLK, 0x30);
	} else {
		dev_err(codec->dev, "Invalid master clock frequency\n");
		return -EINVAL;
	}
	dev_dbg(dai->dev, "Clock source is %d at %uHz\n", clk_id, freq);

	return 0;
}

static int max98090_dai1_set_fmt(struct snd_soc_dai *codec_dai,
				 unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);
	struct max98090_cdata *cdata;
	u8 regval = 0;
	u8 master = 0;

	cdata = &max98090->dai;

	if (fmt != cdata->fmt) {
		cdata->fmt = fmt;
		
		// slave mode
		snd_soc_write(codec, M98090_021_MASTER_MODE_CLK, master);

		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			regval |= M98090_DAI_DLY;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			break;
		default:
			return -EINVAL;
		}

		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_NB_IF:
			regval |= M98090_DAI_WCI;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			regval |= M98090_DAI_BCI;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			regval |= M98090_DAI_BCI|M98090_DAI_WCI;
			break;
		default:
			return -EINVAL;
		}
		snd_soc_update_bits(codec, M98090_022_DAI_IF_FORMAT,
			M98090_DAI_DLY | M98090_DAI_BCI | M98090_DAI_RJ |
			M98090_DAI_WCI, regval);
	}

	return 0;
}

static int max98090_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
//		snd_soc_update_bits(codec, M98090_03E_IPUT_ENABLE,
//				M98090_MBEN, M98090_MBEN);
		break;

	case SND_SOC_BIAS_OFF:
//		snd_soc_update_bits(codec, M98090_03E_IPUT_ENABLE,
//				M98090_MBEN, 0);
//		codec->cache_sync = 1;
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define MAX98090_RATES SNDRV_PCM_RATE_8000_96000
#define MAX98090_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops max98090_dai1_ops = {
	.set_sysclk = max98090_dai_set_sysclk,
	.set_fmt = max98090_dai1_set_fmt,
	.hw_params = max98090_dai1_hw_params,
};

struct snd_soc_dai_driver max98090_dai[] = {
	{
		.name = "max98090-aif1",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98090_RATES,
			.formats = MAX98090_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MAX98090_RATES,
			.formats = MAX98090_FORMATS,
		},
		.ops = &max98090_dai1_ops,
		.symmetric_rates = 1,
	},
};
EXPORT_SYMBOL_GPL(max98090_dai);

#ifdef CONFIG_PM
static int max98090_suspend(struct snd_soc_codec *codec)
{
	max98090_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int max98090_resume(struct snd_soc_codec *codec)
{
	max98090_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define max98090_suspend NULL
#define max98090_resume NULL
#endif

static int max98090_reset(struct snd_soc_codec *codec)
{
	int ret;

	ret = snd_soc_write(codec, M98090_045_PWR_SYS, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset codec: %d\n", ret);
		return ret;
	}
	
	ret = snd_soc_write(codec, M98090_000_SW_RESET, M98090_SWRST);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset codec: %d\n", ret);
		return ret;
	}

	return ret;
}

static int max98090_probe(struct snd_soc_codec *codec)
{
	struct max98090_priv *max98090 = snd_soc_codec_get_drvdata(codec);
	struct max98090_cdata *cdata;
	int ret = 0;

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}
	codec->control_data = max98090->control_data;
	codec->cache_sync = 1;
	codec->write = max98090_i2c_write;
	codec->read = max98090_i2c_read;
	max98090->codec = codec;

	/* reset the codec, the DSP core, and disable all interrupts */
	max98090_reset(codec);

	/* initialize private data */
	max98090->sysclk = (unsigned)-1;
	max98090->eq_textcnt = 0;
	max98090->bq_textcnt = 0;

	cdata = &max98090->dai;
	cdata->rate = (unsigned)-1;
	cdata->fmt  = (unsigned)-1;
	cdata->eq_sel = 0;
	cdata->bq_sel = 0;

	max98090->lin_state = 0;
	max98090->mic1pre = 0;
	max98090->mic2pre = 0;

	ret = snd_soc_read(codec, M98090_0FF_REV_ID);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_access;
	}
	dev_info(codec->dev, "revision 0x%02x\n", ret);

	snd_soc_write(codec, M98090_045_PWR_SYS, M98090_SHDNRUN);

	/* initialize registers cache to hardware default */
	max98090_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	snd_soc_write(codec, M98090_042_BIAS_CNTL, 0xF0);
	snd_soc_write(codec, M98090_025_DAI_IOCFG,	0x07);

	max98090_set_record_main_mic(codec);
	snd_soc_update_bits(codec, M98090_045_PWR_SYS, M98090_SHDNRUN, M98090_SHDNRUN);

	max98090_add_widgets(codec);

#if defined(CONFIG_MAX98090_HEADSET)
	switch_dev_register(&switch_jack_detection);

	INIT_DELAYED_WORK_DEFERRABLE(&max98090->work, max98090_work);
	schedule_delayed_work(&max98090->work, msecs_to_jiffies(6000));
#endif

err_access:
	return ret;
}

static int max98090_remove(struct snd_soc_codec *codec)
{
	max98090_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

struct snd_soc_codec_driver soc_codec_dev_max98090 = {
	.probe   = max98090_probe,
	.remove  = max98090_remove,
	.suspend = max98090_suspend,
	.resume  = max98090_resume,

	.reg_cache_size = ARRAY_SIZE(max98090_reg_def),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = max98090_reg_def,
	.readable_register = max98090_readable,
	.volatile_register = max98090_volatile,
};

static int max98090_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct max98090_priv *max98090;
	int ret;

	max98090 = devm_kzalloc(&i2c->dev, sizeof(struct max98090_priv), GFP_KERNEL);
	if (max98090 == NULL)
		return -ENOMEM;
	
	max98090->control_data = i2c;
	i2c_set_clientdata(i2c, max98090);
	max98090->pdata = i2c->dev.platform_data;

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_max98090, &max98090_dai[0], 1);
	if (ret < 0){
		kfree(max98090);
	}

	return ret;
}

static int __devexit max98090_i2c_remove(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);

#if defined(CONFIG_MAX98090_HEADSET)
	switch_dev_unregister(&switch_jack_detection);
#endif
	kfree(codec->reg_cache);
	return 0;
}

static const struct i2c_device_id max98090_i2c_id[] = {
	{ "max98090", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max98090_i2c_id);

static struct i2c_driver max98090_i2c_driver = {
	.driver = {
		.name = "max98090",
		.owner = THIS_MODULE,
	},
	.probe  = max98090_i2c_probe,
	.remove = __devexit_p(max98090_i2c_remove),
	.id_table = max98090_i2c_id,
};

static int __init max98090_init(void)
{
	int ret;

	ret = i2c_add_driver(&max98090_i2c_driver);
	if (ret)
		pr_err("Failed to register max98090 I2C driver: %d\n", ret);

	return ret;
}
module_init(max98090_init);

static void __exit max98090_exit(void)
{
	i2c_del_driver(&max98090_i2c_driver);
}
module_exit(max98090_exit);

MODULE_DESCRIPTION("ALSA SoC MAX98090 driver");
MODULE_AUTHOR("Peter Hsiang");
MODULE_LICENSE("GPL");
