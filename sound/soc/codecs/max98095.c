/*
 * max98095.c -- MAX98095 ALSA SoC Audio driver
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include <sound/max98095.h>
#include <sound/jack.h>
#include "max98095.h"

enum max98095_type {
	MAX98095,
};

struct max98095_cdata {
	unsigned int rate;
	unsigned int fmt;
	int eq_sel;
	int bq_sel;
};

struct max98095_priv {
	enum max98095_type devtype;
	struct max98095_pdata *pdata;
	unsigned int sysclk;
	struct max98095_cdata dai[3];
	const char **eq_texts;
	const char **bq_texts;
	struct soc_enum eq_enum;
	struct soc_enum bq_enum;
	int eq_textcnt;
	int bq_textcnt;
	u8 lin_state;
	unsigned int mic1pre;
	unsigned int mic2pre;
	struct snd_soc_jack *headphone_jack;
	struct snd_soc_jack *mic_jack;
};

static const u8 max98095_reg_def[M98095_REG_CNT] = {
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
} max98095_access[M98095_REG_CNT] = {
	{ 0x00, 0x00 }, /* 00 */
	{ 0xFF, 0x00 }, /* 01 */
	{ 0xFF, 0x00 }, /* 02 */
	{ 0xFF, 0x00 }, /* 03 */
	{ 0xFF, 0x00 }, /* 04 */
	{ 0xFF, 0x00 }, /* 05 */
	{ 0xFF, 0x00 }, /* 06 */
	{ 0xFF, 0x00 }, /* 07 */
	{ 0xFF, 0x00 }, /* 08 */
	{ 0xFF, 0x00 }, /* 09 */
	{ 0xFF, 0x00 }, /* 0A */
	{ 0xFF, 0x00 }, /* 0B */
	{ 0xFF, 0x00 }, /* 0C */
	{ 0xFF, 0x00 }, /* 0D */
	{ 0xFF, 0x00 }, /* 0E */
	{ 0xFF, 0x9F }, /* 0F */
	{ 0xFF, 0xFF }, /* 10 */
	{ 0xFF, 0xFF }, /* 11 */
	{ 0xFF, 0xFF }, /* 12 */
	{ 0xFF, 0xFF }, /* 13 */
	{ 0xFF, 0xFF }, /* 14 */
	{ 0xFF, 0xFF }, /* 15 */
	{ 0xFF, 0xFF }, /* 16 */
	{ 0xFF, 0xFF }, /* 17 */
	{ 0xFF, 0xFF }, /* 18 */
	{ 0xFF, 0xFF }, /* 19 */
	{ 0xFF, 0xFF }, /* 1A */
	{ 0xFF, 0xFF }, /* 1B */
	{ 0xFF, 0xFF }, /* 1C */
	{ 0xFF, 0xFF }, /* 1D */
	{ 0xFF, 0x77 }, /* 1E */
	{ 0xFF, 0x77 }, /* 1F */
	{ 0xFF, 0x77 }, /* 20 */
	{ 0xFF, 0x77 }, /* 21 */
	{ 0xFF, 0x77 }, /* 22 */
	{ 0xFF, 0x77 }, /* 23 */
	{ 0xFF, 0xFF }, /* 24 */
	{ 0xFF, 0x7F }, /* 25 */
	{ 0xFF, 0x31 }, /* 26 */
	{ 0xFF, 0xFF }, /* 27 */
	{ 0xFF, 0xFF }, /* 28 */
	{ 0xFF, 0xFF }, /* 29 */
	{ 0xFF, 0xF7 }, /* 2A */
	{ 0xFF, 0x2F }, /* 2B */
	{ 0xFF, 0xEF }, /* 2C */
	{ 0xFF, 0xFF }, /* 2D */
	{ 0xFF, 0xFF }, /* 2E */
	{ 0xFF, 0xFF }, /* 2F */
	{ 0xFF, 0xFF }, /* 30 */
	{ 0xFF, 0xFF }, /* 31 */
	{ 0xFF, 0xFF }, /* 32 */
	{ 0xFF, 0xFF }, /* 33 */
	{ 0xFF, 0xF7 }, /* 34 */
	{ 0xFF, 0x2F }, /* 35 */
	{ 0xFF, 0xCF }, /* 36 */
	{ 0xFF, 0xFF }, /* 37 */
	{ 0xFF, 0xFF }, /* 38 */
	{ 0xFF, 0xFF }, /* 39 */
	{ 0xFF, 0xFF }, /* 3A */
	{ 0xFF, 0xFF }, /* 3B */
	{ 0xFF, 0xFF }, /* 3C */
	{ 0xFF, 0xFF }, /* 3D */
	{ 0xFF, 0xF7 }, /* 3E */
	{ 0xFF, 0x2F }, /* 3F */
	{ 0xFF, 0xCF }, /* 40 */
	{ 0xFF, 0xFF }, /* 41 */
	{ 0xFF, 0x77 }, /* 42 */
	{ 0xFF, 0xFF }, /* 43 */
	{ 0xFF, 0xFF }, /* 44 */
	{ 0xFF, 0xFF }, /* 45 */
	{ 0xFF, 0xFF }, /* 46 */
	{ 0xFF, 0xFF }, /* 47 */
	{ 0xFF, 0xFF }, /* 48 */
	{ 0xFF, 0x0F }, /* 49 */
	{ 0xFF, 0xFF }, /* 4A */
	{ 0xFF, 0xFF }, /* 4B */
	{ 0xFF, 0x3F }, /* 4C */
	{ 0xFF, 0x3F }, /* 4D */
	{ 0xFF, 0x3F }, /* 4E */
	{ 0xFF, 0xFF }, /* 4F */
	{ 0xFF, 0x7F }, /* 50 */
	{ 0xFF, 0x7F }, /* 51 */
	{ 0xFF, 0x0F }, /* 52 */
	{ 0xFF, 0x3F }, /* 53 */
	{ 0xFF, 0x3F }, /* 54 */
	{ 0xFF, 0x3F }, /* 55 */
	{ 0xFF, 0xFF }, /* 56 */
	{ 0xFF, 0xFF }, /* 57 */
	{ 0xFF, 0xBF }, /* 58 */
	{ 0xFF, 0x1F }, /* 59 */
	{ 0xFF, 0xBF }, /* 5A */
	{ 0xFF, 0x1F }, /* 5B */
	{ 0xFF, 0xBF }, /* 5C */
	{ 0xFF, 0x3F }, /* 5D */
	{ 0xFF, 0x3F }, /* 5E */
	{ 0xFF, 0x7F }, /* 5F */
	{ 0xFF, 0x7F }, /* 60 */
	{ 0xFF, 0x47 }, /* 61 */
	{ 0xFF, 0x9F }, /* 62 */
	{ 0xFF, 0x9F }, /* 63 */
	{ 0xFF, 0x9F }, /* 64 */
	{ 0xFF, 0x9F }, /* 65 */
	{ 0xFF, 0x9F }, /* 66 */
	{ 0xFF, 0xBF }, /* 67 */
	{ 0xFF, 0xBF }, /* 68 */
	{ 0xFF, 0xFF }, /* 69 */
	{ 0xFF, 0xFF }, /* 6A */
	{ 0xFF, 0x7F }, /* 6B */
	{ 0xFF, 0xF7 }, /* 6C */
	{ 0xFF, 0xFF }, /* 6D */
	{ 0xFF, 0xFF }, /* 6E */
	{ 0xFF, 0x1F }, /* 6F */
	{ 0xFF, 0xF7 }, /* 70 */
	{ 0xFF, 0xFF }, /* 71 */
	{ 0xFF, 0xFF }, /* 72 */
	{ 0xFF, 0x1F }, /* 73 */
	{ 0xFF, 0xF7 }, /* 74 */
	{ 0xFF, 0xFF }, /* 75 */
	{ 0xFF, 0xFF }, /* 76 */
	{ 0xFF, 0x1F }, /* 77 */
	{ 0xFF, 0xF7 }, /* 78 */
	{ 0xFF, 0xFF }, /* 79 */
	{ 0xFF, 0xFF }, /* 7A */
	{ 0xFF, 0x1F }, /* 7B */
	{ 0xFF, 0xF7 }, /* 7C */
	{ 0xFF, 0xFF }, /* 7D */
	{ 0xFF, 0xFF }, /* 7E */
	{ 0xFF, 0x1F }, /* 7F */
	{ 0xFF, 0xF7 }, /* 80 */
	{ 0xFF, 0xFF }, /* 81 */
	{ 0xFF, 0xFF }, /* 82 */
	{ 0xFF, 0x1F }, /* 83 */
	{ 0xFF, 0x7F }, /* 84 */
	{ 0xFF, 0x0F }, /* 85 */
	{ 0xFF, 0xD8 }, /* 86 */
	{ 0xFF, 0xFF }, /* 87 */
	{ 0xFF, 0xEF }, /* 88 */
	{ 0xFF, 0xFE }, /* 89 */
	{ 0xFF, 0xFE }, /* 8A */
	{ 0xFF, 0xFF }, /* 8B */
	{ 0xFF, 0xFF }, /* 8C */
	{ 0xFF, 0x3F }, /* 8D */
	{ 0xFF, 0xFF }, /* 8E */
	{ 0xFF, 0x3F }, /* 8F */
	{ 0xFF, 0x8F }, /* 90 */
	{ 0xFF, 0xFF }, /* 91 */
	{ 0xFF, 0x3F }, /* 92 */
	{ 0xFF, 0xFF }, /* 93 */
	{ 0xFF, 0xFF }, /* 94 */
	{ 0xFF, 0x0F }, /* 95 */
	{ 0xFF, 0x3F }, /* 96 */
	{ 0xFF, 0x8C }, /* 97 */
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

static int max98095_readable(struct snd_soc_codec *codec, unsigned int reg)
{
	if (reg >= M98095_REG_CNT)
		return 0;
	return max98095_access[reg].readable != 0;
}

static int max98095_volatile(struct snd_soc_codec *codec, unsigned int reg)
{
	if (reg > M98095_REG_MAX_CACHED)
		return 1;

	switch (reg) {
	case M98095_000_HOST_DATA:
	case M98095_001_HOST_INT_STS:
	case M98095_002_HOST_RSP_STS:
	case M98095_003_HOST_CMD_STS:
	case M98095_004_CODEC_STS:
	case M98095_005_DAI1_ALC_STS:
	case M98095_006_DAI2_ALC_STS:
	case M98095_007_JACK_AUTO_STS:
	case M98095_008_JACK_MANUAL_STS:
	case M98095_009_JACK_VBAT_STS:
	case M98095_00A_ACC_ADC_STS:
	case M98095_00B_MIC_NG_AGC_STS:
	case M98095_00C_SPK_L_VOLT_STS:
	case M98095_00D_SPK_R_VOLT_STS:
	case M98095_00E_TEMP_SENSOR_STS:
		return 1;
	}

	return 0;
}

/*
 * Filter coefficients are in a separate register segment
 * and they share the address space of the normal registers.
 * The coefficient registers do not need or share the cache.
 */
static int max98095_hw_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	int ret;

	codec->cache_bypass = 1;
	ret = snd_soc_write(codec, reg, value);
	codec->cache_bypass = 0;

	return ret ? -EIO : 0;
}

/*
 * Load equalizer DSP coefficient configurations registers
 */
static void m98095_eq_band(struct snd_soc_codec *codec, unsigned int dai,
		    unsigned int band, u16 *coefs)
{
	unsigned int eq_reg;
	unsigned int i;

	BUG_ON(band > 4);
	BUG_ON(dai > 1);

	/* Load the base register address */
	eq_reg = dai ? M98095_142_DAI2_EQ_BASE : M98095_110_DAI1_EQ_BASE;

	/* Add the band address offset, note adjustment for word address */
	eq_reg += band * (M98095_COEFS_PER_BAND << 1);

	/* Step through the registers and coefs */
	for (i = 0; i < M98095_COEFS_PER_BAND; i++) {
		max98095_hw_write(codec, eq_reg++, M98095_BYTE1(coefs[i]));
		max98095_hw_write(codec, eq_reg++, M98095_BYTE0(coefs[i]));
	}
}

/*
 * Load biquad filter coefficient configurations registers
 */
static void m98095_biquad_band(struct snd_soc_codec *codec, unsigned int dai,
		    unsigned int band, u16 *coefs)
{
	unsigned int bq_reg;
	unsigned int i;

	BUG_ON(band > 1);
	BUG_ON(dai > 1);

	/* Load the base register address */
	bq_reg = dai ? M98095_17E_DAI2_BQ_BASE : M98095_174_DAI1_BQ_BASE;

	/* Add the band address offset, note adjustment for word address */
	bq_reg += band * (M98095_COEFS_PER_BAND << 1);

	/* Step through the registers and coefs */
	for (i = 0; i < M98095_COEFS_PER_BAND; i++) {
		max98095_hw_write(codec, bq_reg++, M98095_BYTE1(coefs[i]));
		max98095_hw_write(codec, bq_reg++, M98095_BYTE0(coefs[i]));
	}
}

static const char * const max98095_fltr_mode[] = { "Voice", "Music" };
static const struct soc_enum max98095_dai1_filter_mode_enum[] = {
	SOC_ENUM_SINGLE(M98095_02E_DAI1_FILTERS, 7, 2, max98095_fltr_mode),
};
static const struct soc_enum max98095_dai2_filter_mode_enum[] = {
	SOC_ENUM_SINGLE(M98095_038_DAI2_FILTERS, 7, 2, max98095_fltr_mode),
};

static const char * const max98095_extmic_text[] = { "None", "MIC1", "MIC2" };

static const struct soc_enum max98095_extmic_enum =
	SOC_ENUM_SINGLE(M98095_087_CFG_MIC, 0, 3, max98095_extmic_text);

static const struct snd_kcontrol_new max98095_extmic_mux =
	SOC_DAPM_ENUM("External MIC Mux", max98095_extmic_enum);

static const char * const max98095_linein_text[] = { "INA", "INB" };

static const struct soc_enum max98095_linein_enum =
	SOC_ENUM_SINGLE(M98095_086_CFG_LINE, 6, 2, max98095_linein_text);

static const struct snd_kcontrol_new max98095_linein_mux =
	SOC_DAPM_ENUM("Linein Input Mux", max98095_linein_enum);

static const char * const max98095_line_mode_text[] = {
	"Stereo", "Differential"};

static const struct soc_enum max98095_linein_mode_enum =
	SOC_ENUM_SINGLE(M98095_086_CFG_LINE, 7, 2, max98095_line_mode_text);

static const struct soc_enum max98095_lineout_mode_enum =
	SOC_ENUM_SINGLE(M98095_086_CFG_LINE, 4, 2, max98095_line_mode_text);

static const char * const max98095_dai_fltr[] = {
	"Off", "Elliptical-HPF-16k", "Butterworth-HPF-16k",
	"Elliptical-HPF-8k", "Butterworth-HPF-8k", "Butterworth-HPF-Fs/240"};
static const struct soc_enum max98095_dai1_dac_filter_enum[] = {
	SOC_ENUM_SINGLE(M98095_02E_DAI1_FILTERS, 0, 6, max98095_dai_fltr),
};
static const struct soc_enum max98095_dai2_dac_filter_enum[] = {
	SOC_ENUM_SINGLE(M98095_038_DAI2_FILTERS, 0, 6, max98095_dai_fltr),
};
static const struct soc_enum max98095_dai3_dac_filter_enum[] = {
	SOC_ENUM_SINGLE(M98095_042_DAI3_FILTERS, 0, 6, max98095_dai_fltr),
};

static int max98095_mic1pre_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	unsigned int sel = ucontrol->value.integer.value[0];

	max98095->mic1pre = sel;
	snd_soc_update_bits(codec, M98095_05F_LVL_MIC1, M98095_MICPRE_MASK,
		(1+sel)<<M98095_MICPRE_SHIFT);

	return 0;
}

static int max98095_mic1pre_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = max98095->mic1pre;
	return 0;
}

static int max98095_mic2pre_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	unsigned int sel = ucontrol->value.integer.value[0];

	max98095->mic2pre = sel;
	snd_soc_update_bits(codec, M98095_060_LVL_MIC2, M98095_MICPRE_MASK,
		(1+sel)<<M98095_MICPRE_SHIFT);

	return 0;
}

static int max98095_mic2pre_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = max98095->mic2pre;
	return 0;
}

static const unsigned int max98095_micboost_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 1, TLV_DB_SCALE_ITEM(0, 2000, 0),
	2, 2, TLV_DB_SCALE_ITEM(3000, 0, 0),
};

static const DECLARE_TLV_DB_SCALE(max98095_mic_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(max98095_adc_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(max98095_adcboost_tlv, 0, 600, 0);

static const unsigned int max98095_hp_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0, 6, TLV_DB_SCALE_ITEM(-6700, 400, 0),
	7, 14, TLV_DB_SCALE_ITEM(-4000, 300, 0),
	15, 21, TLV_DB_SCALE_ITEM(-1700, 200, 0),
	22, 27, TLV_DB_SCALE_ITEM(-400, 100, 0),
	28, 31, TLV_DB_SCALE_ITEM(150, 50, 0),
};

static const unsigned int max98095_spk_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 10, TLV_DB_SCALE_ITEM(-5900, 400, 0),
	11, 18, TLV_DB_SCALE_ITEM(-1700, 200, 0),
	19, 27, TLV_DB_SCALE_ITEM(-200, 100, 0),
	28, 39, TLV_DB_SCALE_ITEM(650, 50, 0),
};

static const unsigned int max98095_rcv_lout_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0, 6, TLV_DB_SCALE_ITEM(-6200, 400, 0),
	7, 14, TLV_DB_SCALE_ITEM(-3500, 300, 0),
	15, 21, TLV_DB_SCALE_ITEM(-1200, 200, 0),
	22, 27, TLV_DB_SCALE_ITEM(100, 100, 0),
	28, 31, TLV_DB_SCALE_ITEM(650, 50, 0),
};

static const unsigned int max98095_lin_tlv[] = {
	TLV_DB_RANGE_HEAD(3),
	0, 2, TLV_DB_SCALE_ITEM(-600, 300, 0),
	3, 3, TLV_DB_SCALE_ITEM(300, 1100, 0),
	4, 5, TLV_DB_SCALE_ITEM(1400, 600, 0),
};

static const struct snd_kcontrol_new max98095_snd_controls[] = {

	SOC_DOUBLE_R_TLV("Headphone Volume", M98095_064_LVL_HP_L,
		M98095_065_LVL_HP_R, 0, 31, 0, max98095_hp_tlv),

	SOC_DOUBLE_R_TLV("Speaker Volume", M98095_067_LVL_SPK_L,
		M98095_068_LVL_SPK_R, 0, 39, 0, max98095_spk_tlv),

	SOC_SINGLE_TLV("Receiver Volume", M98095_066_LVL_RCV,
		0, 31, 0, max98095_rcv_lout_tlv),

	SOC_DOUBLE_R_TLV("Lineout Volume", M98095_062_LVL_LINEOUT1,
		M98095_063_LVL_LINEOUT2, 0, 31, 0, max98095_rcv_lout_tlv),

	SOC_DOUBLE_R("Headphone Switch", M98095_064_LVL_HP_L,
		M98095_065_LVL_HP_R, 7, 1, 1),

	SOC_DOUBLE_R("Speaker Switch", M98095_067_LVL_SPK_L,
		M98095_068_LVL_SPK_R, 7, 1, 1),

	SOC_SINGLE("Receiver Switch", M98095_066_LVL_RCV, 7, 1, 1),

	SOC_DOUBLE_R("Lineout Switch", M98095_062_LVL_LINEOUT1,
		M98095_063_LVL_LINEOUT2, 7, 1, 1),

	SOC_SINGLE_TLV("MIC1 Volume", M98095_05F_LVL_MIC1, 0, 20, 1,
		max98095_mic_tlv),

	SOC_SINGLE_TLV("MIC2 Volume", M98095_060_LVL_MIC2, 0, 20, 1,
		max98095_mic_tlv),

	SOC_SINGLE_EXT_TLV("MIC1 Boost Volume",
			M98095_05F_LVL_MIC1, 5, 2, 0,
			max98095_mic1pre_get, max98095_mic1pre_set,
			max98095_micboost_tlv),
	SOC_SINGLE_EXT_TLV("MIC2 Boost Volume",
			M98095_060_LVL_MIC2, 5, 2, 0,
			max98095_mic2pre_get, max98095_mic2pre_set,
			max98095_micboost_tlv),

	SOC_SINGLE_TLV("Linein Volume", M98095_061_LVL_LINEIN, 0, 5, 1,
		max98095_lin_tlv),

	SOC_SINGLE_TLV("ADCL Volume", M98095_05D_LVL_ADC_L, 0, 15, 1,
		max98095_adc_tlv),
	SOC_SINGLE_TLV("ADCR Volume", M98095_05E_LVL_ADC_R, 0, 15, 1,
		max98095_adc_tlv),

	SOC_SINGLE_TLV("ADCL Boost Volume", M98095_05D_LVL_ADC_L, 4, 3, 0,
		max98095_adcboost_tlv),
	SOC_SINGLE_TLV("ADCR Boost Volume", M98095_05E_LVL_ADC_R, 4, 3, 0,
		max98095_adcboost_tlv),

	SOC_SINGLE("EQ1 Switch", M98095_088_CFG_LEVEL, 0, 1, 0),
	SOC_SINGLE("EQ2 Switch", M98095_088_CFG_LEVEL, 1, 1, 0),

	SOC_SINGLE("Biquad1 Switch", M98095_088_CFG_LEVEL, 2, 1, 0),
	SOC_SINGLE("Biquad2 Switch", M98095_088_CFG_LEVEL, 3, 1, 0),

	SOC_ENUM("DAI1 Filter Mode", max98095_dai1_filter_mode_enum),
	SOC_ENUM("DAI2 Filter Mode", max98095_dai2_filter_mode_enum),
	SOC_ENUM("DAI1 DAC Filter", max98095_dai1_dac_filter_enum),
	SOC_ENUM("DAI2 DAC Filter", max98095_dai2_dac_filter_enum),
	SOC_ENUM("DAI3 DAC Filter", max98095_dai3_dac_filter_enum),

	SOC_ENUM("Linein Mode", max98095_linein_mode_enum),
	SOC_ENUM("Lineout Mode", max98095_lineout_mode_enum),
};

/* Left speaker mixer switch */
static const struct snd_kcontrol_new max98095_left_speaker_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_050_MIX_SPK_LEFT, 0, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_050_MIX_SPK_LEFT, 6, 1, 0),
	SOC_DAPM_SINGLE("Mono DAC2 Switch", M98095_050_MIX_SPK_LEFT, 3, 1, 0),
	SOC_DAPM_SINGLE("Mono DAC3 Switch", M98095_050_MIX_SPK_LEFT, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_050_MIX_SPK_LEFT, 4, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_050_MIX_SPK_LEFT, 5, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_050_MIX_SPK_LEFT, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_050_MIX_SPK_LEFT, 2, 1, 0),
};

/* Right speaker mixer switch */
static const struct snd_kcontrol_new max98095_right_speaker_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_051_MIX_SPK_RIGHT, 6, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_051_MIX_SPK_RIGHT, 0, 1, 0),
	SOC_DAPM_SINGLE("Mono DAC2 Switch", M98095_051_MIX_SPK_RIGHT, 3, 1, 0),
	SOC_DAPM_SINGLE("Mono DAC3 Switch", M98095_051_MIX_SPK_RIGHT, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_051_MIX_SPK_RIGHT, 5, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_051_MIX_SPK_RIGHT, 4, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_051_MIX_SPK_RIGHT, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_051_MIX_SPK_RIGHT, 2, 1, 0),
};

/* Left headphone mixer switch */
static const struct snd_kcontrol_new max98095_left_hp_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_04C_MIX_HP_LEFT, 0, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_04C_MIX_HP_LEFT, 5, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_04C_MIX_HP_LEFT, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_04C_MIX_HP_LEFT, 4, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_04C_MIX_HP_LEFT, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_04C_MIX_HP_LEFT, 2, 1, 0),
};

/* Right headphone mixer switch */
static const struct snd_kcontrol_new max98095_right_hp_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_04D_MIX_HP_RIGHT, 5, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_04D_MIX_HP_RIGHT, 0, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_04D_MIX_HP_RIGHT, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_04D_MIX_HP_RIGHT, 4, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_04D_MIX_HP_RIGHT, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_04D_MIX_HP_RIGHT, 2, 1, 0),
};

/* Receiver earpiece mixer switch */
static const struct snd_kcontrol_new max98095_mono_rcv_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_04F_MIX_RCV, 0, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_04F_MIX_RCV, 5, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_04F_MIX_RCV, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_04F_MIX_RCV, 4, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_04F_MIX_RCV, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_04F_MIX_RCV, 2, 1, 0),
};

/* Left lineout mixer switch */
static const struct snd_kcontrol_new max98095_left_lineout_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_053_MIX_LINEOUT1, 5, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_053_MIX_LINEOUT1, 0, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_053_MIX_LINEOUT1, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_053_MIX_LINEOUT1, 4, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_053_MIX_LINEOUT1, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_053_MIX_LINEOUT1, 2, 1, 0),
};

/* Right lineout mixer switch */
static const struct snd_kcontrol_new max98095_right_lineout_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left DAC1 Switch", M98095_054_MIX_LINEOUT2, 0, 1, 0),
	SOC_DAPM_SINGLE("Right DAC1 Switch", M98095_054_MIX_LINEOUT2, 5, 1, 0),
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_054_MIX_LINEOUT2, 3, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_054_MIX_LINEOUT2, 4, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_054_MIX_LINEOUT2, 1, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_054_MIX_LINEOUT2, 2, 1, 0),
};

/* Left ADC mixer switch */
static const struct snd_kcontrol_new max98095_left_ADC_mixer_controls[] = {
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_04A_MIX_ADC_LEFT, 7, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_04A_MIX_ADC_LEFT, 6, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_04A_MIX_ADC_LEFT, 3, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_04A_MIX_ADC_LEFT, 2, 1, 0),
};

/* Right ADC mixer switch */
static const struct snd_kcontrol_new max98095_right_ADC_mixer_controls[] = {
	SOC_DAPM_SINGLE("MIC1 Switch", M98095_04B_MIX_ADC_RIGHT, 7, 1, 0),
	SOC_DAPM_SINGLE("MIC2 Switch", M98095_04B_MIX_ADC_RIGHT, 6, 1, 0),
	SOC_DAPM_SINGLE("IN1 Switch", M98095_04B_MIX_ADC_RIGHT, 3, 1, 0),
	SOC_DAPM_SINGLE("IN2 Switch", M98095_04B_MIX_ADC_RIGHT, 2, 1, 0),
};

static int max98095_mic_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (w->reg == M98095_05F_LVL_MIC1) {
			snd_soc_update_bits(codec, w->reg, M98095_MICPRE_MASK,
				(1+max98095->mic1pre)<<M98095_MICPRE_SHIFT);
		} else {
			snd_soc_update_bits(codec, w->reg, M98095_MICPRE_MASK,
				(1+max98095->mic2pre)<<M98095_MICPRE_SHIFT);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, w->reg, M98095_MICPRE_MASK, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * The line inputs are stereo inputs with the left and right
 * channels sharing a common PGA power control signal.
 */
static int max98095_line_pga(struct snd_soc_dapm_widget *w,
			     int event, u8 channel)
{
	struct snd_soc_codec *codec = w->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	u8 *state;

	BUG_ON(!((channel == 1) || (channel == 2)));

	state = &max98095->lin_state;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		*state |= channel;
		snd_soc_update_bits(codec, w->reg,
			(1 << w->shift), (1 << w->shift));
		break;
	case SND_SOC_DAPM_POST_PMD:
		*state &= ~channel;
		if (*state == 0) {
			snd_soc_update_bits(codec, w->reg,
				(1 << w->shift), 0);
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max98095_pga_in1_event(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *k, int event)
{
	return max98095_line_pga(w, event, 1);
}

static int max98095_pga_in2_event(struct snd_soc_dapm_widget *w,
				   struct snd_kcontrol *k, int event)
{
	return max98095_line_pga(w, event, 2);
}

/*
 * The stereo line out mixer outputs to two stereo line outs.
 * The 2nd pair has a separate set of enables.
 */
static int max98095_lineout_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, w->reg,
			(1 << (w->shift+2)), (1 << (w->shift+2)));
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, w->reg,
			(1 << (w->shift+2)), 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dapm_widget max98095_dapm_widgets[] = {

	SND_SOC_DAPM_ADC("ADCL", "HiFi Capture", M98095_090_PWR_EN_IN, 0, 0),
	SND_SOC_DAPM_ADC("ADCR", "HiFi Capture", M98095_090_PWR_EN_IN, 1, 0),

	SND_SOC_DAPM_DAC("DACL1", "HiFi Playback",
		M98095_091_PWR_EN_OUT, 0, 0),
	SND_SOC_DAPM_DAC("DACR1", "HiFi Playback",
		M98095_091_PWR_EN_OUT, 1, 0),
	SND_SOC_DAPM_DAC("DACM2", "Aux Playback",
		M98095_091_PWR_EN_OUT, 2, 0),
	SND_SOC_DAPM_DAC("DACM3", "Voice Playback",
		M98095_091_PWR_EN_OUT, 2, 0),

	SND_SOC_DAPM_PGA("HP Left Out", M98095_091_PWR_EN_OUT,
		6, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HP Right Out", M98095_091_PWR_EN_OUT,
		7, 0, NULL, 0),

	SND_SOC_DAPM_PGA("SPK Left Out", M98095_091_PWR_EN_OUT,
		4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPK Right Out", M98095_091_PWR_EN_OUT,
		5, 0, NULL, 0),

	SND_SOC_DAPM_PGA("RCV Mono Out", M98095_091_PWR_EN_OUT,
		3, 0, NULL, 0),

	SND_SOC_DAPM_PGA_E("LINE Left Out", M98095_092_PWR_EN_OUT,
		0, 0, NULL, 0, max98095_lineout_event, SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_PGA_E("LINE Right Out", M98095_092_PWR_EN_OUT,
		1, 0, NULL, 0, max98095_lineout_event, SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MUX("External MIC", SND_SOC_NOPM, 0, 0,
		&max98095_extmic_mux),

	SND_SOC_DAPM_MUX("Linein Mux", SND_SOC_NOPM, 0, 0,
		&max98095_linein_mux),

	SND_SOC_DAPM_MIXER("Left Headphone Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_left_hp_mixer_controls[0],
		ARRAY_SIZE(max98095_left_hp_mixer_controls)),

	SND_SOC_DAPM_MIXER("Right Headphone Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_right_hp_mixer_controls[0],
		ARRAY_SIZE(max98095_right_hp_mixer_controls)),

	SND_SOC_DAPM_MIXER("Left Speaker Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_left_speaker_mixer_controls[0],
		ARRAY_SIZE(max98095_left_speaker_mixer_controls)),

	SND_SOC_DAPM_MIXER("Right Speaker Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_right_speaker_mixer_controls[0],
		ARRAY_SIZE(max98095_right_speaker_mixer_controls)),

	SND_SOC_DAPM_MIXER("Receiver Mixer", SND_SOC_NOPM, 0, 0,
	  &max98095_mono_rcv_mixer_controls[0],
		ARRAY_SIZE(max98095_mono_rcv_mixer_controls)),

	SND_SOC_DAPM_MIXER("Left Lineout Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_left_lineout_mixer_controls[0],
		ARRAY_SIZE(max98095_left_lineout_mixer_controls)),

	SND_SOC_DAPM_MIXER("Right Lineout Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_right_lineout_mixer_controls[0],
		ARRAY_SIZE(max98095_right_lineout_mixer_controls)),

	SND_SOC_DAPM_MIXER("Left ADC Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_left_ADC_mixer_controls[0],
		ARRAY_SIZE(max98095_left_ADC_mixer_controls)),

	SND_SOC_DAPM_MIXER("Right ADC Mixer", SND_SOC_NOPM, 0, 0,
		&max98095_right_ADC_mixer_controls[0],
		ARRAY_SIZE(max98095_right_ADC_mixer_controls)),

	SND_SOC_DAPM_PGA_E("MIC1 Input", M98095_05F_LVL_MIC1,
		5, 0, NULL, 0, max98095_mic_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA_E("MIC2 Input", M98095_060_LVL_MIC2,
		5, 0, NULL, 0, max98095_mic_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA_E("IN1 Input", M98095_090_PWR_EN_IN,
		7, 0, NULL, 0, max98095_pga_in1_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_PGA_E("IN2 Input", M98095_090_PWR_EN_IN,
		7, 0, NULL, 0, max98095_pga_in2_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MICBIAS("MICBIAS1", M98095_090_PWR_EN_IN, 2, 0),
	SND_SOC_DAPM_MICBIAS("MICBIAS2", M98095_090_PWR_EN_IN, 3, 0),

	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("SPKL"),
	SND_SOC_DAPM_OUTPUT("SPKR"),
	SND_SOC_DAPM_OUTPUT("RCV"),
	SND_SOC_DAPM_OUTPUT("OUT1"),
	SND_SOC_DAPM_OUTPUT("OUT2"),
	SND_SOC_DAPM_OUTPUT("OUT3"),
	SND_SOC_DAPM_OUTPUT("OUT4"),

	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("INA1"),
	SND_SOC_DAPM_INPUT("INA2"),
	SND_SOC_DAPM_INPUT("INB1"),
	SND_SOC_DAPM_INPUT("INB2"),
};

static const struct snd_soc_dapm_route max98095_audio_map[] = {
	/* Left headphone output mixer */
	{"Left Headphone Mixer", "Left DAC1 Switch", "DACL1"},
	{"Left Headphone Mixer", "Right DAC1 Switch", "DACR1"},
	{"Left Headphone Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Left Headphone Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Left Headphone Mixer", "IN1 Switch", "IN1 Input"},
	{"Left Headphone Mixer", "IN2 Switch", "IN2 Input"},

	/* Right headphone output mixer */
	{"Right Headphone Mixer", "Left DAC1 Switch", "DACL1"},
	{"Right Headphone Mixer", "Right DAC1 Switch", "DACR1"},
	{"Right Headphone Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Right Headphone Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Right Headphone Mixer", "IN1 Switch", "IN1 Input"},
	{"Right Headphone Mixer", "IN2 Switch", "IN2 Input"},

	/* Left speaker output mixer */
	{"Left Speaker Mixer", "Left DAC1 Switch", "DACL1"},
	{"Left Speaker Mixer", "Right DAC1 Switch", "DACR1"},
	{"Left Speaker Mixer", "Mono DAC2 Switch", "DACM2"},
	{"Left Speaker Mixer", "Mono DAC3 Switch", "DACM3"},
	{"Left Speaker Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Left Speaker Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Left Speaker Mixer", "IN1 Switch", "IN1 Input"},
	{"Left Speaker Mixer", "IN2 Switch", "IN2 Input"},

	/* Right speaker output mixer */
	{"Right Speaker Mixer", "Left DAC1 Switch", "DACL1"},
	{"Right Speaker Mixer", "Right DAC1 Switch", "DACR1"},
	{"Right Speaker Mixer", "Mono DAC2 Switch", "DACM2"},
	{"Right Speaker Mixer", "Mono DAC3 Switch", "DACM3"},
	{"Right Speaker Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Right Speaker Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Right Speaker Mixer", "IN1 Switch", "IN1 Input"},
	{"Right Speaker Mixer", "IN2 Switch", "IN2 Input"},

	/* Earpiece/Receiver output mixer */
	{"Receiver Mixer", "Left DAC1 Switch", "DACL1"},
	{"Receiver Mixer", "Right DAC1 Switch", "DACR1"},
	{"Receiver Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Receiver Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Receiver Mixer", "IN1 Switch", "IN1 Input"},
	{"Receiver Mixer", "IN2 Switch", "IN2 Input"},

	/* Left Lineout output mixer */
	{"Left Lineout Mixer", "Left DAC1 Switch", "DACL1"},
	{"Left Lineout Mixer", "Right DAC1 Switch", "DACR1"},
	{"Left Lineout Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Left Lineout Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Left Lineout Mixer", "IN1 Switch", "IN1 Input"},
	{"Left Lineout Mixer", "IN2 Switch", "IN2 Input"},

	/* Right lineout output mixer */
	{"Right Lineout Mixer", "Left DAC1 Switch", "DACL1"},
	{"Right Lineout Mixer", "Right DAC1 Switch", "DACR1"},
	{"Right Lineout Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Right Lineout Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Right Lineout Mixer", "IN1 Switch", "IN1 Input"},
	{"Right Lineout Mixer", "IN2 Switch", "IN2 Input"},

	{"HP Left Out", NULL, "Left Headphone Mixer"},
	{"HP Right Out", NULL, "Right Headphone Mixer"},
	{"SPK Left Out", NULL, "Left Speaker Mixer"},
	{"SPK Right Out", NULL, "Right Speaker Mixer"},
	{"RCV Mono Out", NULL, "Receiver Mixer"},
	{"LINE Left Out", NULL, "Left Lineout Mixer"},
	{"LINE Right Out", NULL, "Right Lineout Mixer"},

	{"HPL", NULL, "HP Left Out"},
	{"HPR", NULL, "HP Right Out"},
	{"SPKL", NULL, "SPK Left Out"},
	{"SPKR", NULL, "SPK Right Out"},
	{"RCV", NULL, "RCV Mono Out"},
	{"OUT1", NULL, "LINE Left Out"},
	{"OUT2", NULL, "LINE Right Out"},
	{"OUT3", NULL, "LINE Left Out"},
	{"OUT4", NULL, "LINE Right Out"},

	/* Left ADC input mixer */
	{"Left ADC Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Left ADC Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Left ADC Mixer", "IN1 Switch", "IN1 Input"},
	{"Left ADC Mixer", "IN2 Switch", "IN2 Input"},

	/* Right ADC input mixer */
	{"Right ADC Mixer", "MIC1 Switch", "MIC1 Input"},
	{"Right ADC Mixer", "MIC2 Switch", "MIC2 Input"},
	{"Right ADC Mixer", "IN1 Switch", "IN1 Input"},
	{"Right ADC Mixer", "IN2 Switch", "IN2 Input"},

	/* Inputs */
	{"ADCL", NULL, "Left ADC Mixer"},
	{"ADCR", NULL, "Right ADC Mixer"},

	{"IN1 Input", NULL, "INA1"},
	{"IN2 Input", NULL, "INA2"},

	{"MIC1 Input", NULL, "MIC1"},
	{"MIC2 Input", NULL, "MIC2"},
};

static int max98095_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_add_codec_controls(codec, max98095_snd_controls,
			     ARRAY_SIZE(max98095_snd_controls));

	return 0;
}

/* codec mclk clock divider coefficients */
static const struct {
	u32 rate;
	u8  sr;
} rate_table[] = {
	{8000,  0x01},
	{11025, 0x02},
	{16000, 0x03},
	{22050, 0x04},
	{24000, 0x05},
	{32000, 0x06},
	{44100, 0x07},
	{48000, 0x08},
	{88200, 0x09},
	{96000, 0x0A},
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

static int max98095_dai1_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	unsigned long long ni;
	unsigned int rate;
	u8 regval;

	cdata = &max98095->dai[0];

	rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_update_bits(codec, M98095_02A_DAI1_FORMAT,
			M98095_DAI_WS, 0);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		snd_soc_update_bits(codec, M98095_02A_DAI1_FORMAT,
			M98095_DAI_WS, M98095_DAI_WS);
		break;
	default:
		return -EINVAL;
	}

	if (rate_value(rate, &regval))
		return -EINVAL;

	snd_soc_update_bits(codec, M98095_027_DAI1_CLKMODE,
		M98095_CLKMODE_MASK, regval);
	cdata->rate = rate;

	/* Configure NI when operating as master */
	if (snd_soc_read(codec, M98095_02A_DAI1_FORMAT) & M98095_DAI_MAS) {
		if (max98095->sysclk == 0) {
			dev_err(codec->dev, "Invalid system clock frequency\n");
			return -EINVAL;
		}
		ni = 65536ULL * (rate < 50000 ? 96ULL : 48ULL)
				* (unsigned long long int)rate;
		do_div(ni, (unsigned long long int)max98095->sysclk);
		snd_soc_write(codec, M98095_028_DAI1_CLKCFG_HI,
			(ni >> 8) & 0x7F);
		snd_soc_write(codec, M98095_029_DAI1_CLKCFG_LO,
			ni & 0xFF);
	}

	/* Update sample rate mode */
	if (rate < 50000)
		snd_soc_update_bits(codec, M98095_02E_DAI1_FILTERS,
			M98095_DAI_DHF, 0);
	else
		snd_soc_update_bits(codec, M98095_02E_DAI1_FILTERS,
			M98095_DAI_DHF, M98095_DAI_DHF);

	return 0;
}

static int max98095_dai2_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	unsigned long long ni;
	unsigned int rate;
	u8 regval;

	cdata = &max98095->dai[1];

	rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_update_bits(codec, M98095_034_DAI2_FORMAT,
			M98095_DAI_WS, 0);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		snd_soc_update_bits(codec, M98095_034_DAI2_FORMAT,
			M98095_DAI_WS, M98095_DAI_WS);
		break;
	default:
		return -EINVAL;
	}

	if (rate_value(rate, &regval))
		return -EINVAL;

	snd_soc_update_bits(codec, M98095_031_DAI2_CLKMODE,
		M98095_CLKMODE_MASK, regval);
	cdata->rate = rate;

	/* Configure NI when operating as master */
	if (snd_soc_read(codec, M98095_034_DAI2_FORMAT) & M98095_DAI_MAS) {
		if (max98095->sysclk == 0) {
			dev_err(codec->dev, "Invalid system clock frequency\n");
			return -EINVAL;
		}
		ni = 65536ULL * (rate < 50000 ? 96ULL : 48ULL)
				* (unsigned long long int)rate;
		do_div(ni, (unsigned long long int)max98095->sysclk);
		snd_soc_write(codec, M98095_032_DAI2_CLKCFG_HI,
			(ni >> 8) & 0x7F);
		snd_soc_write(codec, M98095_033_DAI2_CLKCFG_LO,
			ni & 0xFF);
	}

	/* Update sample rate mode */
	if (rate < 50000)
		snd_soc_update_bits(codec, M98095_038_DAI2_FILTERS,
			M98095_DAI_DHF, 0);
	else
		snd_soc_update_bits(codec, M98095_038_DAI2_FILTERS,
			M98095_DAI_DHF, M98095_DAI_DHF);

	return 0;
}

static int max98095_dai3_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	unsigned long long ni;
	unsigned int rate;
	u8 regval;

	cdata = &max98095->dai[2];

	rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		snd_soc_update_bits(codec, M98095_03E_DAI3_FORMAT,
			M98095_DAI_WS, 0);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		snd_soc_update_bits(codec, M98095_03E_DAI3_FORMAT,
			M98095_DAI_WS, M98095_DAI_WS);
		break;
	default:
		return -EINVAL;
	}

	if (rate_value(rate, &regval))
		return -EINVAL;

	snd_soc_update_bits(codec, M98095_03B_DAI3_CLKMODE,
		M98095_CLKMODE_MASK, regval);
	cdata->rate = rate;

	/* Configure NI when operating as master */
	if (snd_soc_read(codec, M98095_03E_DAI3_FORMAT) & M98095_DAI_MAS) {
		if (max98095->sysclk == 0) {
			dev_err(codec->dev, "Invalid system clock frequency\n");
			return -EINVAL;
		}
		ni = 65536ULL * (rate < 50000 ? 96ULL : 48ULL)
				* (unsigned long long int)rate;
		do_div(ni, (unsigned long long int)max98095->sysclk);
		snd_soc_write(codec, M98095_03C_DAI3_CLKCFG_HI,
			(ni >> 8) & 0x7F);
		snd_soc_write(codec, M98095_03D_DAI3_CLKCFG_LO,
			ni & 0xFF);
	}

	/* Update sample rate mode */
	if (rate < 50000)
		snd_soc_update_bits(codec, M98095_042_DAI3_FILTERS,
			M98095_DAI_DHF, 0);
	else
		snd_soc_update_bits(codec, M98095_042_DAI3_FILTERS,
			M98095_DAI_DHF, M98095_DAI_DHF);

	return 0;
}

static int max98095_dai_set_sysclk(struct snd_soc_dai *dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);

	/* Requested clock frequency is already setup */
	if (freq == max98095->sysclk)
		return 0;

	/* Setup clocks for slave mode, and using the PLL
	 * PSCLK = 0x01 (when master clk is 10MHz to 20MHz)
	 *         0x02 (when master clk is 20MHz to 40MHz)..
	 *         0x03 (when master clk is 40MHz to 60MHz)..
	 */
	if ((freq >= 10000000) && (freq < 20000000)) {
		snd_soc_write(codec, M98095_026_SYS_CLK, 0x10);
	} else if ((freq >= 20000000) && (freq < 40000000)) {
		snd_soc_write(codec, M98095_026_SYS_CLK, 0x20);
	} else if ((freq >= 40000000) && (freq < 60000000)) {
		snd_soc_write(codec, M98095_026_SYS_CLK, 0x30);
	} else {
		dev_err(codec->dev, "Invalid master clock frequency\n");
		return -EINVAL;
	}

	dev_dbg(dai->dev, "Clock source is %d at %uHz\n", clk_id, freq);

	max98095->sysclk = freq;
	return 0;
}

static int max98095_dai1_set_fmt(struct snd_soc_dai *codec_dai,
				 unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	u8 regval = 0;

	cdata = &max98095->dai[0];

	if (fmt != cdata->fmt) {
		cdata->fmt = fmt;

		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS:
			/* Slave mode PLL */
			snd_soc_write(codec, M98095_028_DAI1_CLKCFG_HI,
				0x80);
			snd_soc_write(codec, M98095_029_DAI1_CLKCFG_LO,
				0x00);
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
			/* Set to master mode */
			regval |= M98095_DAI_MAS;
			break;
		case SND_SOC_DAIFMT_CBS_CFM:
		case SND_SOC_DAIFMT_CBM_CFS:
		default:
			dev_err(codec->dev, "Clock mode unsupported");
			return -EINVAL;
		}

		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			regval |= M98095_DAI_DLY;
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
			regval |= M98095_DAI_WCI;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			regval |= M98095_DAI_BCI;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			regval |= M98095_DAI_BCI|M98095_DAI_WCI;
			break;
		default:
			return -EINVAL;
		}

		snd_soc_update_bits(codec, M98095_02A_DAI1_FORMAT,
			M98095_DAI_MAS | M98095_DAI_DLY | M98095_DAI_BCI |
			M98095_DAI_WCI, regval);

		snd_soc_write(codec, M98095_02B_DAI1_CLOCK, M98095_DAI_BSEL64);
	}

	return 0;
}

static int max98095_dai2_set_fmt(struct snd_soc_dai *codec_dai,
				 unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	u8 regval = 0;

	cdata = &max98095->dai[1];

	if (fmt != cdata->fmt) {
		cdata->fmt = fmt;

		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS:
			/* Slave mode PLL */
			snd_soc_write(codec, M98095_032_DAI2_CLKCFG_HI,
				0x80);
			snd_soc_write(codec, M98095_033_DAI2_CLKCFG_LO,
				0x00);
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
			/* Set to master mode */
			regval |= M98095_DAI_MAS;
			break;
		case SND_SOC_DAIFMT_CBS_CFM:
		case SND_SOC_DAIFMT_CBM_CFS:
		default:
			dev_err(codec->dev, "Clock mode unsupported");
			return -EINVAL;
		}

		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			regval |= M98095_DAI_DLY;
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
			regval |= M98095_DAI_WCI;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			regval |= M98095_DAI_BCI;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			regval |= M98095_DAI_BCI|M98095_DAI_WCI;
			break;
		default:
			return -EINVAL;
		}

		snd_soc_update_bits(codec, M98095_034_DAI2_FORMAT,
			M98095_DAI_MAS | M98095_DAI_DLY | M98095_DAI_BCI |
			M98095_DAI_WCI, regval);

		snd_soc_write(codec, M98095_035_DAI2_CLOCK,
			M98095_DAI_BSEL64);
	}

	return 0;
}

static int max98095_dai3_set_fmt(struct snd_soc_dai *codec_dai,
				 unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	u8 regval = 0;

	cdata = &max98095->dai[2];

	if (fmt != cdata->fmt) {
		cdata->fmt = fmt;

		switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
		case SND_SOC_DAIFMT_CBS_CFS:
			/* Slave mode PLL */
			snd_soc_write(codec, M98095_03C_DAI3_CLKCFG_HI,
				0x80);
			snd_soc_write(codec, M98095_03D_DAI3_CLKCFG_LO,
				0x00);
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
			/* Set to master mode */
			regval |= M98095_DAI_MAS;
			break;
		case SND_SOC_DAIFMT_CBS_CFM:
		case SND_SOC_DAIFMT_CBM_CFS:
		default:
			dev_err(codec->dev, "Clock mode unsupported");
			return -EINVAL;
		}

		switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
		case SND_SOC_DAIFMT_I2S:
			regval |= M98095_DAI_DLY;
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
			regval |= M98095_DAI_WCI;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			regval |= M98095_DAI_BCI;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			regval |= M98095_DAI_BCI|M98095_DAI_WCI;
			break;
		default:
			return -EINVAL;
		}

		snd_soc_update_bits(codec, M98095_03E_DAI3_FORMAT,
			M98095_DAI_MAS | M98095_DAI_DLY | M98095_DAI_BCI |
			M98095_DAI_WCI, regval);

		snd_soc_write(codec, M98095_03F_DAI3_CLOCK,
			M98095_DAI_BSEL64);
	}

	return 0;
}

static int max98095_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = snd_soc_cache_sync(codec);

			if (ret != 0) {
				dev_err(codec->dev, "Failed to sync cache: %d\n", ret);
				return ret;
			}
		}

		snd_soc_update_bits(codec, M98095_090_PWR_EN_IN,
				M98095_MBEN, M98095_MBEN);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, M98095_090_PWR_EN_IN,
				M98095_MBEN, 0);
		codec->cache_sync = 1;
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define MAX98095_RATES SNDRV_PCM_RATE_8000_96000
#define MAX98095_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops max98095_dai1_ops = {
	.set_sysclk = max98095_dai_set_sysclk,
	.set_fmt = max98095_dai1_set_fmt,
	.hw_params = max98095_dai1_hw_params,
};

static const struct snd_soc_dai_ops max98095_dai2_ops = {
	.set_sysclk = max98095_dai_set_sysclk,
	.set_fmt = max98095_dai2_set_fmt,
	.hw_params = max98095_dai2_hw_params,
};

static const struct snd_soc_dai_ops max98095_dai3_ops = {
	.set_sysclk = max98095_dai_set_sysclk,
	.set_fmt = max98095_dai3_set_fmt,
	.hw_params = max98095_dai3_hw_params,
};

static struct snd_soc_dai_driver max98095_dai[] = {
{
	.name = "HiFi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = MAX98095_RATES,
		.formats = MAX98095_FORMATS,
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = MAX98095_RATES,
		.formats = MAX98095_FORMATS,
	},
	 .ops = &max98095_dai1_ops,
},
{
	.name = "Aux",
	.playback = {
		.stream_name = "Aux Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = MAX98095_RATES,
		.formats = MAX98095_FORMATS,
	},
	.ops = &max98095_dai2_ops,
},
{
	.name = "Voice",
	.playback = {
		.stream_name = "Voice Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = MAX98095_RATES,
		.formats = MAX98095_FORMATS,
	},
	.ops = &max98095_dai3_ops,
}

};

static int max98095_get_eq_channel(const char *name)
{
	if (strcmp(name, "EQ1 Mode") == 0)
		return 0;
	if (strcmp(name, "EQ2 Mode") == 0)
		return 1;
	return -EINVAL;
}

static int max98095_put_eq_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_pdata *pdata = max98095->pdata;
	int channel = max98095_get_eq_channel(kcontrol->id.name);
	struct max98095_cdata *cdata;
	int sel = ucontrol->value.integer.value[0];
	struct max98095_eq_cfg *coef_set;
	int fs, best, best_val, i;
	int regmask, regsave;

	BUG_ON(channel > 1);

	if (!pdata || !max98095->eq_textcnt)
		return 0;

	if (sel >= pdata->eq_cfgcnt)
		return -EINVAL;

	cdata = &max98095->dai[channel];
	cdata->eq_sel = sel;
	fs = cdata->rate;

	/* Find the selected configuration with nearest sample rate */
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < pdata->eq_cfgcnt; i++) {
		if (strcmp(pdata->eq_cfg[i].name, max98095->eq_texts[sel]) == 0 &&
			abs(pdata->eq_cfg[i].rate - fs) < best_val) {
			best = i;
			best_val = abs(pdata->eq_cfg[i].rate - fs);
		}
	}

	dev_dbg(codec->dev, "Selected %s/%dHz for %dHz sample rate\n",
		pdata->eq_cfg[best].name,
		pdata->eq_cfg[best].rate, fs);

	coef_set = &pdata->eq_cfg[best];

	regmask = (channel == 0) ? M98095_EQ1EN : M98095_EQ2EN;

	/* Disable filter while configuring, and save current on/off state */
	regsave = snd_soc_read(codec, M98095_088_CFG_LEVEL);
	snd_soc_update_bits(codec, M98095_088_CFG_LEVEL, regmask, 0);

	mutex_lock(&codec->mutex);
	snd_soc_update_bits(codec, M98095_00F_HOST_CFG, M98095_SEG, M98095_SEG);
	m98095_eq_band(codec, channel, 0, coef_set->band1);
	m98095_eq_band(codec, channel, 1, coef_set->band2);
	m98095_eq_band(codec, channel, 2, coef_set->band3);
	m98095_eq_band(codec, channel, 3, coef_set->band4);
	m98095_eq_band(codec, channel, 4, coef_set->band5);
	snd_soc_update_bits(codec, M98095_00F_HOST_CFG, M98095_SEG, 0);
	mutex_unlock(&codec->mutex);

	/* Restore the original on/off state */
	snd_soc_update_bits(codec, M98095_088_CFG_LEVEL, regmask, regsave);
	return 0;
}

static int max98095_get_eq_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	int channel = max98095_get_eq_channel(kcontrol->id.name);
	struct max98095_cdata *cdata;

	cdata = &max98095->dai[channel];
	ucontrol->value.enumerated.item[0] = cdata->eq_sel;

	return 0;
}

static void max98095_handle_eq_pdata(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_pdata *pdata = max98095->pdata;
	struct max98095_eq_cfg *cfg;
	unsigned int cfgcnt;
	int i, j;
	const char **t;
	int ret;

	struct snd_kcontrol_new controls[] = {
		SOC_ENUM_EXT("EQ1 Mode",
			max98095->eq_enum,
			max98095_get_eq_enum,
			max98095_put_eq_enum),
		SOC_ENUM_EXT("EQ2 Mode",
			max98095->eq_enum,
			max98095_get_eq_enum,
			max98095_put_eq_enum),
	};

	cfg = pdata->eq_cfg;
	cfgcnt = pdata->eq_cfgcnt;

	/* Setup an array of texts for the equalizer enum.
	 * This is based on Mark Brown's equalizer driver code.
	 */
	max98095->eq_textcnt = 0;
	max98095->eq_texts = NULL;
	for (i = 0; i < cfgcnt; i++) {
		for (j = 0; j < max98095->eq_textcnt; j++) {
			if (strcmp(cfg[i].name, max98095->eq_texts[j]) == 0)
				break;
		}

		if (j != max98095->eq_textcnt)
			continue;

		/* Expand the array */
		t = krealloc(max98095->eq_texts,
			     sizeof(char *) * (max98095->eq_textcnt + 1),
			     GFP_KERNEL);
		if (t == NULL)
			continue;

		/* Store the new entry */
		t[max98095->eq_textcnt] = cfg[i].name;
		max98095->eq_textcnt++;
		max98095->eq_texts = t;
	}

	/* Now point the soc_enum to .texts array items */
	max98095->eq_enum.texts = max98095->eq_texts;
	max98095->eq_enum.max = max98095->eq_textcnt;

	ret = snd_soc_add_codec_controls(codec, controls, ARRAY_SIZE(controls));
	if (ret != 0)
		dev_err(codec->dev, "Failed to add EQ control: %d\n", ret);
}

static const char *bq_mode_name[] = {"Biquad1 Mode", "Biquad2 Mode"};

static int max98095_get_bq_channel(struct snd_soc_codec *codec,
				   const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bq_mode_name); i++)
		if (strcmp(name, bq_mode_name[i]) == 0)
			return i;

	/* Shouldn't happen */
	dev_err(codec->dev, "Bad biquad channel name '%s'\n", name);
	return -EINVAL;
}

static int max98095_put_bq_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_pdata *pdata = max98095->pdata;
	int channel = max98095_get_bq_channel(codec, kcontrol->id.name);
	struct max98095_cdata *cdata;
	int sel = ucontrol->value.integer.value[0];
	struct max98095_biquad_cfg *coef_set;
	int fs, best, best_val, i;
	int regmask, regsave;

	if (channel < 0)
		return channel;

	if (!pdata || !max98095->bq_textcnt)
		return 0;

	if (sel >= pdata->bq_cfgcnt)
		return -EINVAL;

	cdata = &max98095->dai[channel];
	cdata->bq_sel = sel;
	fs = cdata->rate;

	/* Find the selected configuration with nearest sample rate */
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < pdata->bq_cfgcnt; i++) {
		if (strcmp(pdata->bq_cfg[i].name, max98095->bq_texts[sel]) == 0 &&
			abs(pdata->bq_cfg[i].rate - fs) < best_val) {
			best = i;
			best_val = abs(pdata->bq_cfg[i].rate - fs);
		}
	}

	dev_dbg(codec->dev, "Selected %s/%dHz for %dHz sample rate\n",
		pdata->bq_cfg[best].name,
		pdata->bq_cfg[best].rate, fs);

	coef_set = &pdata->bq_cfg[best];

	regmask = (channel == 0) ? M98095_BQ1EN : M98095_BQ2EN;

	/* Disable filter while configuring, and save current on/off state */
	regsave = snd_soc_read(codec, M98095_088_CFG_LEVEL);
	snd_soc_update_bits(codec, M98095_088_CFG_LEVEL, regmask, 0);

	mutex_lock(&codec->mutex);
	snd_soc_update_bits(codec, M98095_00F_HOST_CFG, M98095_SEG, M98095_SEG);
	m98095_biquad_band(codec, channel, 0, coef_set->band1);
	m98095_biquad_band(codec, channel, 1, coef_set->band2);
	snd_soc_update_bits(codec, M98095_00F_HOST_CFG, M98095_SEG, 0);
	mutex_unlock(&codec->mutex);

	/* Restore the original on/off state */
	snd_soc_update_bits(codec, M98095_088_CFG_LEVEL, regmask, regsave);
	return 0;
}

static int max98095_get_bq_enum(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	int channel = max98095_get_bq_channel(codec, kcontrol->id.name);
	struct max98095_cdata *cdata;

	if (channel < 0)
		return channel;

	cdata = &max98095->dai[channel];
	ucontrol->value.enumerated.item[0] = cdata->bq_sel;

	return 0;
}

static void max98095_handle_bq_pdata(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_pdata *pdata = max98095->pdata;
	struct max98095_biquad_cfg *cfg;
	unsigned int cfgcnt;
	int i, j;
	const char **t;
	int ret;

	struct snd_kcontrol_new controls[] = {
		SOC_ENUM_EXT((char *)bq_mode_name[0],
			max98095->bq_enum,
			max98095_get_bq_enum,
			max98095_put_bq_enum),
		SOC_ENUM_EXT((char *)bq_mode_name[1],
			max98095->bq_enum,
			max98095_get_bq_enum,
			max98095_put_bq_enum),
	};
	BUILD_BUG_ON(ARRAY_SIZE(controls) != ARRAY_SIZE(bq_mode_name));

	cfg = pdata->bq_cfg;
	cfgcnt = pdata->bq_cfgcnt;

	/* Setup an array of texts for the biquad enum.
	 * This is based on Mark Brown's equalizer driver code.
	 */
	max98095->bq_textcnt = 0;
	max98095->bq_texts = NULL;
	for (i = 0; i < cfgcnt; i++) {
		for (j = 0; j < max98095->bq_textcnt; j++) {
			if (strcmp(cfg[i].name, max98095->bq_texts[j]) == 0)
				break;
		}

		if (j != max98095->bq_textcnt)
			continue;

		/* Expand the array */
		t = krealloc(max98095->bq_texts,
			     sizeof(char *) * (max98095->bq_textcnt + 1),
			     GFP_KERNEL);
		if (t == NULL)
			continue;

		/* Store the new entry */
		t[max98095->bq_textcnt] = cfg[i].name;
		max98095->bq_textcnt++;
		max98095->bq_texts = t;
	}

	/* Now point the soc_enum to .texts array items */
	max98095->bq_enum.texts = max98095->bq_texts;
	max98095->bq_enum.max = max98095->bq_textcnt;

	ret = snd_soc_add_codec_controls(codec, controls, ARRAY_SIZE(controls));
	if (ret != 0)
		dev_err(codec->dev, "Failed to add Biquad control: %d\n", ret);
}

static void max98095_handle_pdata(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_pdata *pdata = max98095->pdata;
	u8 regval = 0;

	if (!pdata) {
		dev_dbg(codec->dev, "No platform data\n");
		return;
	}

	/* Configure mic for analog/digital mic mode */
	if (pdata->digmic_left_mode)
		regval |= M98095_DIGMIC_L;

	if (pdata->digmic_right_mode)
		regval |= M98095_DIGMIC_R;

	snd_soc_write(codec, M98095_087_CFG_MIC, regval);

	/* Configure equalizers */
	if (pdata->eq_cfgcnt)
		max98095_handle_eq_pdata(codec);

	/* Configure bi-quad filters */
	if (pdata->bq_cfgcnt)
		max98095_handle_bq_pdata(codec);
}

static irqreturn_t max98095_report_jack(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	unsigned int value;
	int hp_report = 0;
	int mic_report = 0;

	/* Read the Jack Status Register */
	value = snd_soc_read(codec, M98095_007_JACK_AUTO_STS);

	/* If ddone is not set, then detection isn't finished yet */
	if ((value & M98095_DDONE) == 0)
		return IRQ_NONE;

	/* if hp, check its bit, and if set, clear it */
	if ((value & M98095_HP_IN || value & M98095_LO_IN) &&
		max98095->headphone_jack)
		hp_report |= SND_JACK_HEADPHONE;

	/* if mic, check its bit, and if set, clear it */
	if ((value & M98095_MIC_IN) && max98095->mic_jack)
		mic_report |= SND_JACK_MICROPHONE;

	if (max98095->headphone_jack == max98095->mic_jack) {
		snd_soc_jack_report(max98095->headphone_jack,
					hp_report | mic_report,
					SND_JACK_HEADSET);
	} else {
		if (max98095->headphone_jack)
			snd_soc_jack_report(max98095->headphone_jack,
					hp_report, SND_JACK_HEADPHONE);
		if (max98095->mic_jack)
			snd_soc_jack_report(max98095->mic_jack,
					mic_report, SND_JACK_MICROPHONE);
	}

	return IRQ_HANDLED;
}

static int max98095_jack_detect_enable(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;
	int detect_enable = M98095_JDEN;
	unsigned int slew = M98095_DEFAULT_SLEW_DELAY;

	if (max98095->pdata->jack_detect_pin5en)
		detect_enable |= M98095_PIN5EN;

	if (max98095->pdata->jack_detect_delay)
		slew = max98095->pdata->jack_detect_delay;

	ret = snd_soc_write(codec, M98095_08E_JACK_DC_SLEW, slew);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to cfg auto detect %d\n", ret);
		return ret;
	}

	/* configure auto detection to be enabled */
	ret = snd_soc_write(codec, M98095_089_JACK_DET_AUTO, detect_enable);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to cfg auto detect %d\n", ret);
		return ret;
	}

	return ret;
}

static int max98095_jack_detect_disable(struct snd_soc_codec *codec)
{
	int ret = 0;

	/* configure auto detection to be disabled */
	ret = snd_soc_write(codec, M98095_089_JACK_DET_AUTO, 0x0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to cfg auto detect %d\n", ret);
		return ret;
	}

	return ret;
}

int max98095_jack_detect(struct snd_soc_codec *codec,
	struct snd_soc_jack *hp_jack, struct snd_soc_jack *mic_jack)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *client = to_i2c_client(codec->dev);
	int ret = 0;

	max98095->headphone_jack = hp_jack;
	max98095->mic_jack = mic_jack;

	/* only progress if we have at least 1 jack pointer */
	if (!hp_jack && !mic_jack)
		return -EINVAL;

	max98095_jack_detect_enable(codec);

	/* enable interrupts for headphone jack detection */
	ret = snd_soc_update_bits(codec, M98095_013_JACK_INT_EN,
		M98095_IDDONE, M98095_IDDONE);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to cfg jack irqs %d\n", ret);
		return ret;
	}

	max98095_report_jack(client->irq, codec);
	return 0;
}
EXPORT_SYMBOL_GPL(max98095_jack_detect);

#ifdef CONFIG_PM
static int max98095_suspend(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);

	if (max98095->headphone_jack || max98095->mic_jack)
		max98095_jack_detect_disable(codec);

	max98095_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int max98095_resume(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *client = to_i2c_client(codec->dev);

	max98095_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	if (max98095->headphone_jack || max98095->mic_jack) {
		max98095_jack_detect_enable(codec);
		max98095_report_jack(client->irq, codec);
	}

	return 0;
}
#else
#define max98095_suspend NULL
#define max98095_resume NULL
#endif

static int max98095_reset(struct snd_soc_codec *codec)
{
	int i, ret;

	/* Gracefully reset the DSP core and the codec hardware
	 * in a proper sequence */
	ret = snd_soc_write(codec, M98095_00F_HOST_CFG, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset DSP: %d\n", ret);
		return ret;
	}

	ret = snd_soc_write(codec, M98095_097_PWR_SYS, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset codec: %d\n", ret);
		return ret;
	}

	/* Reset to hardware default for registers, as there is not
	 * a soft reset hardware control register */
	for (i = M98095_010_HOST_INT_CFG; i < M98095_REG_MAX_CACHED; i++) {
		ret = snd_soc_write(codec, i, max98095_reg_def[i]);
		if (ret < 0) {
			dev_err(codec->dev, "Failed to reset: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int max98095_probe(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct max98095_cdata *cdata;
	struct i2c_client *client;
	int ret = 0;

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/* reset the codec, the DSP core, and disable all interrupts */
	max98095_reset(codec);

	client = to_i2c_client(codec->dev);

	/* initialize private data */

	max98095->sysclk = (unsigned)-1;
	max98095->eq_textcnt = 0;
	max98095->bq_textcnt = 0;

	cdata = &max98095->dai[0];
	cdata->rate = (unsigned)-1;
	cdata->fmt  = (unsigned)-1;
	cdata->eq_sel = 0;
	cdata->bq_sel = 0;

	cdata = &max98095->dai[1];
	cdata->rate = (unsigned)-1;
	cdata->fmt  = (unsigned)-1;
	cdata->eq_sel = 0;
	cdata->bq_sel = 0;

	cdata = &max98095->dai[2];
	cdata->rate = (unsigned)-1;
	cdata->fmt  = (unsigned)-1;
	cdata->eq_sel = 0;
	cdata->bq_sel = 0;

	max98095->lin_state = 0;
	max98095->mic1pre = 0;
	max98095->mic2pre = 0;

	if (client->irq) {
		/* register an audio interrupt */
		ret = request_threaded_irq(client->irq, NULL,
			max98095_report_jack,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"max98095", codec);
		if (ret) {
			dev_err(codec->dev, "Failed to request IRQ: %d\n", ret);
			goto err_access;
		}
	}

	ret = snd_soc_read(codec, M98095_0FF_REV_ID);
	if (ret < 0) {
		dev_err(codec->dev, "Failure reading hardware revision: %d\n",
			ret);
		goto err_irq;
	}
	dev_info(codec->dev, "Hardware revision: %c\n", ret - 0x40 + 'A');

	snd_soc_write(codec, M98095_097_PWR_SYS, M98095_PWRSV);

	/* initialize registers cache to hardware default */
	max98095_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	snd_soc_write(codec, M98095_048_MIX_DAC_LR,
		M98095_DAI1L_TO_DACL|M98095_DAI1R_TO_DACR);

	snd_soc_write(codec, M98095_049_MIX_DAC_M,
		M98095_DAI2M_TO_DACM|M98095_DAI3M_TO_DACM);

	snd_soc_write(codec, M98095_092_PWR_EN_OUT, M98095_SPK_SPREADSPECTRUM);
	snd_soc_write(codec, M98095_045_CFG_DSP, M98095_DSPNORMAL);
	snd_soc_write(codec, M98095_04E_CFG_HP, M98095_HPNORMAL);

	snd_soc_write(codec, M98095_02C_DAI1_IOCFG,
		M98095_S1NORMAL|M98095_SDATA);

	snd_soc_write(codec, M98095_036_DAI2_IOCFG,
		M98095_S2NORMAL|M98095_SDATA);

	snd_soc_write(codec, M98095_040_DAI3_IOCFG,
		M98095_S3NORMAL|M98095_SDATA);

	max98095_handle_pdata(codec);

	/* take the codec out of the shut down */
	snd_soc_update_bits(codec, M98095_097_PWR_SYS, M98095_SHDNRUN,
		M98095_SHDNRUN);

	max98095_add_widgets(codec);

	return 0;

err_irq:
	if (client->irq)
		free_irq(client->irq, codec);
err_access:
	return ret;
}

static int max98095_remove(struct snd_soc_codec *codec)
{
	struct max98095_priv *max98095 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *client = to_i2c_client(codec->dev);

	max98095_set_bias_level(codec, SND_SOC_BIAS_OFF);

	if (max98095->headphone_jack || max98095->mic_jack)
		max98095_jack_detect_disable(codec);

	if (client->irq)
		free_irq(client->irq, codec);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_max98095 = {
	.probe   = max98095_probe,
	.remove  = max98095_remove,
	.suspend = max98095_suspend,
	.resume  = max98095_resume,
	.set_bias_level = max98095_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(max98095_reg_def),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = max98095_reg_def,
	.readable_register = max98095_readable,
	.volatile_register = max98095_volatile,
	.dapm_widgets	  = max98095_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max98095_dapm_widgets),
	.dapm_routes     = max98095_audio_map,
	.num_dapm_routes = ARRAY_SIZE(max98095_audio_map),
};

static int max98095_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct max98095_priv *max98095;
	int ret;

	max98095 = devm_kzalloc(&i2c->dev, sizeof(struct max98095_priv),
				GFP_KERNEL);
	if (max98095 == NULL)
		return -ENOMEM;

	max98095->devtype = id->driver_data;
	i2c_set_clientdata(i2c, max98095);
	max98095->pdata = i2c->dev.platform_data;

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_max98095,
				     max98095_dai, ARRAY_SIZE(max98095_dai));
	return ret;
}

static int max98095_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id max98095_i2c_id[] = {
	{ "max98095", MAX98095 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max98095_i2c_id);

static struct i2c_driver max98095_i2c_driver = {
	.driver = {
		.name = "max98095",
		.owner = THIS_MODULE,
	},
	.probe  = max98095_i2c_probe,
	.remove = max98095_i2c_remove,
	.id_table = max98095_i2c_id,
};

module_i2c_driver(max98095_i2c_driver);

MODULE_DESCRIPTION("ALSA SoC MAX98095 driver");
MODULE_AUTHOR("Peter Hsiang");
MODULE_LICENSE("GPL");
