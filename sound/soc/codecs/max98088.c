/*
 * max98088.c -- MAX98088 ALSA SoC Audio driver
 *
 * Copyright 2010 Maxim Integrated Products
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
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include <sound/max98088.h>
#include "max98088.h"

enum max98088_type {
       MAX98088,
       MAX98089,
};

struct max98088_cdata {
       unsigned int rate;
       unsigned int fmt;
       int eq_sel;
};

struct max98088_priv {
	struct regmap *regmap;
	enum max98088_type devtype;
	struct max98088_pdata *pdata;
	unsigned int sysclk;
	struct max98088_cdata dai[2];
	int eq_textcnt;
	const char **eq_texts;
	struct soc_enum eq_enum;
	u8 ina_state;
	u8 inb_state;
	unsigned int ex_mode;
	unsigned int digmic;
	unsigned int mic1pre;
	unsigned int mic2pre;
	unsigned int extmic_mode;
};

static const struct reg_default max98088_reg[] = {
	{  0xf, 0x00 }, /* 0F interrupt enable */

	{ 0x10, 0x00 }, /* 10 master clock */
	{ 0x11, 0x00 }, /* 11 DAI1 clock mode */
	{ 0x12, 0x00 }, /* 12 DAI1 clock control */
	{ 0x13, 0x00 }, /* 13 DAI1 clock control */
	{ 0x14, 0x00 }, /* 14 DAI1 format */
	{ 0x15, 0x00 }, /* 15 DAI1 clock */
	{ 0x16, 0x00 }, /* 16 DAI1 config */
	{ 0x17, 0x00 }, /* 17 DAI1 TDM */
	{ 0x18, 0x00 }, /* 18 DAI1 filters */
	{ 0x19, 0x00 }, /* 19 DAI2 clock mode */
	{ 0x1a, 0x00 }, /* 1A DAI2 clock control */
	{ 0x1b, 0x00 }, /* 1B DAI2 clock control */
	{ 0x1c, 0x00 }, /* 1C DAI2 format */
	{ 0x1d, 0x00 }, /* 1D DAI2 clock */
	{ 0x1e, 0x00 }, /* 1E DAI2 config */
	{ 0x1f, 0x00 }, /* 1F DAI2 TDM */

	{ 0x20, 0x00 }, /* 20 DAI2 filters */
	{ 0x21, 0x00 }, /* 21 data config */
	{ 0x22, 0x00 }, /* 22 DAC mixer */
	{ 0x23, 0x00 }, /* 23 left ADC mixer */
	{ 0x24, 0x00 }, /* 24 right ADC mixer */
	{ 0x25, 0x00 }, /* 25 left HP mixer */
	{ 0x26, 0x00 }, /* 26 right HP mixer */
	{ 0x27, 0x00 }, /* 27 HP control */
	{ 0x28, 0x00 }, /* 28 left REC mixer */
	{ 0x29, 0x00 }, /* 29 right REC mixer */
	{ 0x2a, 0x00 }, /* 2A REC control */
	{ 0x2b, 0x00 }, /* 2B left SPK mixer */
	{ 0x2c, 0x00 }, /* 2C right SPK mixer */
	{ 0x2d, 0x00 }, /* 2D SPK control */
	{ 0x2e, 0x00 }, /* 2E sidetone */
	{ 0x2f, 0x00 }, /* 2F DAI1 playback level */

	{ 0x30, 0x00 }, /* 30 DAI1 playback level */
	{ 0x31, 0x00 }, /* 31 DAI2 playback level */
	{ 0x32, 0x00 }, /* 32 DAI2 playbakc level */
	{ 0x33, 0x00 }, /* 33 left ADC level */
	{ 0x34, 0x00 }, /* 34 right ADC level */
	{ 0x35, 0x00 }, /* 35 MIC1 level */
	{ 0x36, 0x00 }, /* 36 MIC2 level */
	{ 0x37, 0x00 }, /* 37 INA level */
	{ 0x38, 0x00 }, /* 38 INB level */
	{ 0x39, 0x00 }, /* 39 left HP volume */
	{ 0x3a, 0x00 }, /* 3A right HP volume */
	{ 0x3b, 0x00 }, /* 3B left REC volume */
	{ 0x3c, 0x00 }, /* 3C right REC volume */
	{ 0x3d, 0x00 }, /* 3D left SPK volume */
	{ 0x3e, 0x00 }, /* 3E right SPK volume */
	{ 0x3f, 0x00 }, /* 3F MIC config */

	{ 0x40, 0x00 }, /* 40 MIC threshold */
	{ 0x41, 0x00 }, /* 41 excursion limiter filter */
	{ 0x42, 0x00 }, /* 42 excursion limiter threshold */
	{ 0x43, 0x00 }, /* 43 ALC */
	{ 0x44, 0x00 }, /* 44 power limiter threshold */
	{ 0x45, 0x00 }, /* 45 power limiter config */
	{ 0x46, 0x00 }, /* 46 distortion limiter config */
	{ 0x47, 0x00 }, /* 47 audio input */
        { 0x48, 0x00 }, /* 48 microphone */
	{ 0x49, 0x00 }, /* 49 level control */
	{ 0x4a, 0x00 }, /* 4A bypass switches */
	{ 0x4b, 0x00 }, /* 4B jack detect */
	{ 0x4c, 0x00 }, /* 4C input enable */
	{ 0x4d, 0x00 }, /* 4D output enable */
	{ 0x4e, 0xF0 }, /* 4E bias control */
	{ 0x4f, 0x00 }, /* 4F DAC power */

	{ 0x50, 0x0F }, /* 50 DAC power */
	{ 0x51, 0x00 }, /* 51 system */
	{ 0x52, 0x00 }, /* 52 DAI1 EQ1 */
	{ 0x53, 0x00 }, /* 53 DAI1 EQ1 */
	{ 0x54, 0x00 }, /* 54 DAI1 EQ1 */
	{ 0x55, 0x00 }, /* 55 DAI1 EQ1 */
	{ 0x56, 0x00 }, /* 56 DAI1 EQ1 */
	{ 0x57, 0x00 }, /* 57 DAI1 EQ1 */
	{ 0x58, 0x00 }, /* 58 DAI1 EQ1 */
	{ 0x59, 0x00 }, /* 59 DAI1 EQ1 */
	{ 0x5a, 0x00 }, /* 5A DAI1 EQ1 */
	{ 0x5b, 0x00 }, /* 5B DAI1 EQ1 */
	{ 0x5c, 0x00 }, /* 5C DAI1 EQ2 */
	{ 0x5d, 0x00 }, /* 5D DAI1 EQ2 */
	{ 0x5e, 0x00 }, /* 5E DAI1 EQ2 */
	{ 0x5f, 0x00 }, /* 5F DAI1 EQ2 */

	{ 0x60, 0x00 }, /* 60 DAI1 EQ2 */
	{ 0x61, 0x00 }, /* 61 DAI1 EQ2 */
	{ 0x62, 0x00 }, /* 62 DAI1 EQ2 */
	{ 0x63, 0x00 }, /* 63 DAI1 EQ2 */
	{ 0x64, 0x00 }, /* 64 DAI1 EQ2 */
	{ 0x65, 0x00 }, /* 65 DAI1 EQ2 */
	{ 0x66, 0x00 }, /* 66 DAI1 EQ3 */
	{ 0x67, 0x00 }, /* 67 DAI1 EQ3 */
	{ 0x68, 0x00 }, /* 68 DAI1 EQ3 */
	{ 0x69, 0x00 }, /* 69 DAI1 EQ3 */
	{ 0x6a, 0x00 }, /* 6A DAI1 EQ3 */
	{ 0x6b, 0x00 }, /* 6B DAI1 EQ3 */
	{ 0x6c, 0x00 }, /* 6C DAI1 EQ3 */
	{ 0x6d, 0x00 }, /* 6D DAI1 EQ3 */
	{ 0x6e, 0x00 }, /* 6E DAI1 EQ3 */
	{ 0x6f, 0x00 }, /* 6F DAI1 EQ3 */

	{ 0x70, 0x00 }, /* 70 DAI1 EQ4 */
	{ 0x71, 0x00 }, /* 71 DAI1 EQ4 */
	{ 0x72, 0x00 }, /* 72 DAI1 EQ4 */
	{ 0x73, 0x00 }, /* 73 DAI1 EQ4 */
	{ 0x74, 0x00 }, /* 74 DAI1 EQ4 */
	{ 0x75, 0x00 }, /* 75 DAI1 EQ4 */
	{ 0x76, 0x00 }, /* 76 DAI1 EQ4 */
	{ 0x77, 0x00 }, /* 77 DAI1 EQ4 */
	{ 0x78, 0x00 }, /* 78 DAI1 EQ4 */
	{ 0x79, 0x00 }, /* 79 DAI1 EQ4 */
	{ 0x7a, 0x00 }, /* 7A DAI1 EQ5 */
	{ 0x7b, 0x00 }, /* 7B DAI1 EQ5 */
	{ 0x7c, 0x00 }, /* 7C DAI1 EQ5 */
	{ 0x7d, 0x00 }, /* 7D DAI1 EQ5 */
	{ 0x7e, 0x00 }, /* 7E DAI1 EQ5 */
	{ 0x7f, 0x00 }, /* 7F DAI1 EQ5 */

	{ 0x80, 0x00 }, /* 80 DAI1 EQ5 */
	{ 0x81, 0x00 }, /* 81 DAI1 EQ5 */
	{ 0x82, 0x00 }, /* 82 DAI1 EQ5 */
	{ 0x83, 0x00 }, /* 83 DAI1 EQ5 */
	{ 0x84, 0x00 }, /* 84 DAI2 EQ1 */
	{ 0x85, 0x00 }, /* 85 DAI2 EQ1 */
	{ 0x86, 0x00 }, /* 86 DAI2 EQ1 */
	{ 0x87, 0x00 }, /* 87 DAI2 EQ1 */
	{ 0x88, 0x00 }, /* 88 DAI2 EQ1 */
	{ 0x89, 0x00 }, /* 89 DAI2 EQ1 */
	{ 0x8a, 0x00 }, /* 8A DAI2 EQ1 */
	{ 0x8b, 0x00 }, /* 8B DAI2 EQ1 */
	{ 0x8c, 0x00 }, /* 8C DAI2 EQ1 */
	{ 0x8d, 0x00 }, /* 8D DAI2 EQ1 */
	{ 0x8e, 0x00 }, /* 8E DAI2 EQ2 */
	{ 0x8f, 0x00 }, /* 8F DAI2 EQ2 */

	{ 0x90, 0x00 }, /* 90 DAI2 EQ2 */
	{ 0x91, 0x00 }, /* 91 DAI2 EQ2 */
	{ 0x92, 0x00 }, /* 92 DAI2 EQ2 */
	{ 0x93, 0x00 }, /* 93 DAI2 EQ2 */
	{ 0x94, 0x00 }, /* 94 DAI2 EQ2 */
	{ 0x95, 0x00 }, /* 95 DAI2 EQ2 */
	{ 0x96, 0x00 }, /* 96 DAI2 EQ2 */
	{ 0x97, 0x00 }, /* 97 DAI2 EQ2 */
	{ 0x98, 0x00 }, /* 98 DAI2 EQ3 */
	{ 0x99, 0x00 }, /* 99 DAI2 EQ3 */
	{ 0x9a, 0x00 }, /* 9A DAI2 EQ3 */
        { 0x9b, 0x00 }, /* 9B DAI2 EQ3 */
	{ 0x9c, 0x00 }, /* 9C DAI2 EQ3 */
	{ 0x9d, 0x00 }, /* 9D DAI2 EQ3 */
	{ 0x9e, 0x00 }, /* 9E DAI2 EQ3 */
	{ 0x9f, 0x00 }, /* 9F DAI2 EQ3 */

	{ 0xa0, 0x00 }, /* A0 DAI2 EQ3 */
	{ 0xa1, 0x00 }, /* A1 DAI2 EQ3 */
	{ 0xa2, 0x00 }, /* A2 DAI2 EQ4 */
	{ 0xa3, 0x00 }, /* A3 DAI2 EQ4 */
	{ 0xa4, 0x00 }, /* A4 DAI2 EQ4 */
	{ 0xa5, 0x00 }, /* A5 DAI2 EQ4 */
	{ 0xa6, 0x00 }, /* A6 DAI2 EQ4 */
	{ 0xa7, 0x00 }, /* A7 DAI2 EQ4 */
	{ 0xa8, 0x00 }, /* A8 DAI2 EQ4 */
	{ 0xa9, 0x00 }, /* A9 DAI2 EQ4 */
	{ 0xaa, 0x00 }, /* AA DAI2 EQ4 */
	{ 0xab, 0x00 }, /* AB DAI2 EQ4 */
	{ 0xac, 0x00 }, /* AC DAI2 EQ5 */
	{ 0xad, 0x00 }, /* AD DAI2 EQ5 */
	{ 0xae, 0x00 }, /* AE DAI2 EQ5 */
	{ 0xaf, 0x00 }, /* AF DAI2 EQ5 */

	{ 0xb0, 0x00 }, /* B0 DAI2 EQ5 */
	{ 0xb1, 0x00 }, /* B1 DAI2 EQ5 */
	{ 0xb2, 0x00 }, /* B2 DAI2 EQ5 */
	{ 0xb3, 0x00 }, /* B3 DAI2 EQ5 */
	{ 0xb4, 0x00 }, /* B4 DAI2 EQ5 */
	{ 0xb5, 0x00 }, /* B5 DAI2 EQ5 */
	{ 0xb6, 0x00 }, /* B6 DAI1 biquad */
	{ 0xb7, 0x00 }, /* B7 DAI1 biquad */
	{ 0xb8 ,0x00 }, /* B8 DAI1 biquad */
	{ 0xb9, 0x00 }, /* B9 DAI1 biquad */
	{ 0xba, 0x00 }, /* BA DAI1 biquad */
	{ 0xbb, 0x00 }, /* BB DAI1 biquad */
	{ 0xbc, 0x00 }, /* BC DAI1 biquad */
	{ 0xbd, 0x00 }, /* BD DAI1 biquad */
	{ 0xbe, 0x00 }, /* BE DAI1 biquad */
        { 0xbf, 0x00 }, /* BF DAI1 biquad */

	{ 0xc0, 0x00 }, /* C0 DAI2 biquad */
	{ 0xc1, 0x00 }, /* C1 DAI2 biquad */
	{ 0xc2, 0x00 }, /* C2 DAI2 biquad */
	{ 0xc3, 0x00 }, /* C3 DAI2 biquad */
	{ 0xc4, 0x00 }, /* C4 DAI2 biquad */
	{ 0xc5, 0x00 }, /* C5 DAI2 biquad */
	{ 0xc6, 0x00 }, /* C6 DAI2 biquad */
	{ 0xc7, 0x00 }, /* C7 DAI2 biquad */
	{ 0xc8, 0x00 }, /* C8 DAI2 biquad */
	{ 0xc9, 0x00 }, /* C9 DAI2 biquad */
};

static struct {
       int readable;
       int writable;
       int vol;
} max98088_access[M98088_REG_CNT] = {
       { 0xFF, 0xFF, 1 }, /* 00 IRQ status */
       { 0xFF, 0x00, 1 }, /* 01 MIC status */
       { 0xFF, 0x00, 1 }, /* 02 jack status */
       { 0x1F, 0x1F, 1 }, /* 03 battery voltage */
       { 0xFF, 0xFF, 0 }, /* 04 */
       { 0xFF, 0xFF, 0 }, /* 05 */
       { 0xFF, 0xFF, 0 }, /* 06 */
       { 0xFF, 0xFF, 0 }, /* 07 */
       { 0xFF, 0xFF, 0 }, /* 08 */
       { 0xFF, 0xFF, 0 }, /* 09 */
       { 0xFF, 0xFF, 0 }, /* 0A */
       { 0xFF, 0xFF, 0 }, /* 0B */
       { 0xFF, 0xFF, 0 }, /* 0C */
       { 0xFF, 0xFF, 0 }, /* 0D */
       { 0xFF, 0xFF, 0 }, /* 0E */
       { 0xFF, 0xFF, 0 }, /* 0F interrupt enable */

       { 0xFF, 0xFF, 0 }, /* 10 master clock */
       { 0xFF, 0xFF, 0 }, /* 11 DAI1 clock mode */
       { 0xFF, 0xFF, 0 }, /* 12 DAI1 clock control */
       { 0xFF, 0xFF, 0 }, /* 13 DAI1 clock control */
       { 0xFF, 0xFF, 0 }, /* 14 DAI1 format */
       { 0xFF, 0xFF, 0 }, /* 15 DAI1 clock */
       { 0xFF, 0xFF, 0 }, /* 16 DAI1 config */
       { 0xFF, 0xFF, 0 }, /* 17 DAI1 TDM */
       { 0xFF, 0xFF, 0 }, /* 18 DAI1 filters */
       { 0xFF, 0xFF, 0 }, /* 19 DAI2 clock mode */
       { 0xFF, 0xFF, 0 }, /* 1A DAI2 clock control */
       { 0xFF, 0xFF, 0 }, /* 1B DAI2 clock control */
       { 0xFF, 0xFF, 0 }, /* 1C DAI2 format */
       { 0xFF, 0xFF, 0 }, /* 1D DAI2 clock */
       { 0xFF, 0xFF, 0 }, /* 1E DAI2 config */
       { 0xFF, 0xFF, 0 }, /* 1F DAI2 TDM */

       { 0xFF, 0xFF, 0 }, /* 20 DAI2 filters */
       { 0xFF, 0xFF, 0 }, /* 21 data config */
       { 0xFF, 0xFF, 0 }, /* 22 DAC mixer */
       { 0xFF, 0xFF, 0 }, /* 23 left ADC mixer */
       { 0xFF, 0xFF, 0 }, /* 24 right ADC mixer */
       { 0xFF, 0xFF, 0 }, /* 25 left HP mixer */
       { 0xFF, 0xFF, 0 }, /* 26 right HP mixer */
       { 0xFF, 0xFF, 0 }, /* 27 HP control */
       { 0xFF, 0xFF, 0 }, /* 28 left REC mixer */
       { 0xFF, 0xFF, 0 }, /* 29 right REC mixer */
       { 0xFF, 0xFF, 0 }, /* 2A REC control */
       { 0xFF, 0xFF, 0 }, /* 2B left SPK mixer */
       { 0xFF, 0xFF, 0 }, /* 2C right SPK mixer */
       { 0xFF, 0xFF, 0 }, /* 2D SPK control */
       { 0xFF, 0xFF, 0 }, /* 2E sidetone */
       { 0xFF, 0xFF, 0 }, /* 2F DAI1 playback level */

       { 0xFF, 0xFF, 0 }, /* 30 DAI1 playback level */
       { 0xFF, 0xFF, 0 }, /* 31 DAI2 playback level */
       { 0xFF, 0xFF, 0 }, /* 32 DAI2 playbakc level */
       { 0xFF, 0xFF, 0 }, /* 33 left ADC level */
       { 0xFF, 0xFF, 0 }, /* 34 right ADC level */
       { 0xFF, 0xFF, 0 }, /* 35 MIC1 level */
       { 0xFF, 0xFF, 0 }, /* 36 MIC2 level */
       { 0xFF, 0xFF, 0 }, /* 37 INA level */
       { 0xFF, 0xFF, 0 }, /* 38 INB level */
       { 0xFF, 0xFF, 0 }, /* 39 left HP volume */
       { 0xFF, 0xFF, 0 }, /* 3A right HP volume */
       { 0xFF, 0xFF, 0 }, /* 3B left REC volume */
       { 0xFF, 0xFF, 0 }, /* 3C right REC volume */
       { 0xFF, 0xFF, 0 }, /* 3D left SPK volume */
       { 0xFF, 0xFF, 0 }, /* 3E right SPK volume */
       { 0xFF, 0xFF, 0 }, /* 3F MIC config */

       { 0xFF, 0xFF, 0 }, /* 40 MIC threshold */
       { 0xFF, 0xFF, 0 }, /* 41 excursion limiter filter */
       { 0xFF, 0xFF, 0 }, /* 42 excursion limiter threshold */
       { 0xFF, 0xFF, 0 }, /* 43 ALC */
       { 0xFF, 0xFF, 0 }, /* 44 power limiter threshold */
       { 0xFF, 0xFF, 0 }, /* 45 power limiter config */
       { 0xFF, 0xFF, 0 }, /* 46 distortion limiter config */
       { 0xFF, 0xFF, 0 }, /* 47 audio input */
       { 0xFF, 0xFF, 0 }, /* 48 microphone */
       { 0xFF, 0xFF, 0 }, /* 49 level control */
       { 0xFF, 0xFF, 0 }, /* 4A bypass switches */
       { 0xFF, 0xFF, 0 }, /* 4B jack detect */
       { 0xFF, 0xFF, 0 }, /* 4C input enable */
       { 0xFF, 0xFF, 0 }, /* 4D output enable */
       { 0xFF, 0xFF, 0 }, /* 4E bias control */
       { 0xFF, 0xFF, 0 }, /* 4F DAC power */

       { 0xFF, 0xFF, 0 }, /* 50 DAC power */
       { 0xFF, 0xFF, 0 }, /* 51 system */
       { 0xFF, 0xFF, 0 }, /* 52 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 53 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 54 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 55 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 56 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 57 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 58 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 59 DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 5A DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 5B DAI1 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 5C DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 5D DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 5E DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 5F DAI1 EQ2 */

       { 0xFF, 0xFF, 0 }, /* 60 DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 61 DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 62 DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 63 DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 64 DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 65 DAI1 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 66 DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 67 DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 68 DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 69 DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 6A DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 6B DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 6C DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 6D DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 6E DAI1 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 6F DAI1 EQ3 */

       { 0xFF, 0xFF, 0 }, /* 70 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 71 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 72 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 73 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 74 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 75 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 76 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 77 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 78 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 79 DAI1 EQ4 */
       { 0xFF, 0xFF, 0 }, /* 7A DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 7B DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 7C DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 7D DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 7E DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 7F DAI1 EQ5 */

       { 0xFF, 0xFF, 0 }, /* 80 DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 81 DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 82 DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 83 DAI1 EQ5 */
       { 0xFF, 0xFF, 0 }, /* 84 DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 85 DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 86 DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 87 DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 88 DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 89 DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 8A DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 8B DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 8C DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 8D DAI2 EQ1 */
       { 0xFF, 0xFF, 0 }, /* 8E DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 8F DAI2 EQ2 */

       { 0xFF, 0xFF, 0 }, /* 90 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 91 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 92 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 93 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 94 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 95 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 96 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 97 DAI2 EQ2 */
       { 0xFF, 0xFF, 0 }, /* 98 DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 99 DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 9A DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 9B DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 9C DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 9D DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 9E DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* 9F DAI2 EQ3 */

       { 0xFF, 0xFF, 0 }, /* A0 DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* A1 DAI2 EQ3 */
       { 0xFF, 0xFF, 0 }, /* A2 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A3 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A4 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A5 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A6 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A7 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A8 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* A9 DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* AA DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* AB DAI2 EQ4 */
       { 0xFF, 0xFF, 0 }, /* AC DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* AD DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* AE DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* AF DAI2 EQ5 */

       { 0xFF, 0xFF, 0 }, /* B0 DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* B1 DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* B2 DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* B3 DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* B4 DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* B5 DAI2 EQ5 */
       { 0xFF, 0xFF, 0 }, /* B6 DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* B7 DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* B8 DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* B9 DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* BA DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* BB DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* BC DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* BD DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* BE DAI1 biquad */
       { 0xFF, 0xFF, 0 }, /* BF DAI1 biquad */

       { 0xFF, 0xFF, 0 }, /* C0 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C1 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C2 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C3 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C4 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C5 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C6 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C7 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C8 DAI2 biquad */
       { 0xFF, 0xFF, 0 }, /* C9 DAI2 biquad */
       { 0x00, 0x00, 0 }, /* CA */
       { 0x00, 0x00, 0 }, /* CB */
       { 0x00, 0x00, 0 }, /* CC */
       { 0x00, 0x00, 0 }, /* CD */
       { 0x00, 0x00, 0 }, /* CE */
       { 0x00, 0x00, 0 }, /* CF */

       { 0x00, 0x00, 0 }, /* D0 */
       { 0x00, 0x00, 0 }, /* D1 */
       { 0x00, 0x00, 0 }, /* D2 */
       { 0x00, 0x00, 0 }, /* D3 */
       { 0x00, 0x00, 0 }, /* D4 */
       { 0x00, 0x00, 0 }, /* D5 */
       { 0x00, 0x00, 0 }, /* D6 */
       { 0x00, 0x00, 0 }, /* D7 */
       { 0x00, 0x00, 0 }, /* D8 */
       { 0x00, 0x00, 0 }, /* D9 */
       { 0x00, 0x00, 0 }, /* DA */
       { 0x00, 0x00, 0 }, /* DB */
       { 0x00, 0x00, 0 }, /* DC */
       { 0x00, 0x00, 0 }, /* DD */
       { 0x00, 0x00, 0 }, /* DE */
       { 0x00, 0x00, 0 }, /* DF */

       { 0x00, 0x00, 0 }, /* E0 */
       { 0x00, 0x00, 0 }, /* E1 */
       { 0x00, 0x00, 0 }, /* E2 */
       { 0x00, 0x00, 0 }, /* E3 */
       { 0x00, 0x00, 0 }, /* E4 */
       { 0x00, 0x00, 0 }, /* E5 */
       { 0x00, 0x00, 0 }, /* E6 */
       { 0x00, 0x00, 0 }, /* E7 */
       { 0x00, 0x00, 0 }, /* E8 */
       { 0x00, 0x00, 0 }, /* E9 */
       { 0x00, 0x00, 0 }, /* EA */
       { 0x00, 0x00, 0 }, /* EB */
       { 0x00, 0x00, 0 }, /* EC */
       { 0x00, 0x00, 0 }, /* ED */
       { 0x00, 0x00, 0 }, /* EE */
       { 0x00, 0x00, 0 }, /* EF */

       { 0x00, 0x00, 0 }, /* F0 */
       { 0x00, 0x00, 0 }, /* F1 */
       { 0x00, 0x00, 0 }, /* F2 */
       { 0x00, 0x00, 0 }, /* F3 */
       { 0x00, 0x00, 0 }, /* F4 */
       { 0x00, 0x00, 0 }, /* F5 */
       { 0x00, 0x00, 0 }, /* F6 */
       { 0x00, 0x00, 0 }, /* F7 */
       { 0x00, 0x00, 0 }, /* F8 */
       { 0x00, 0x00, 0 }, /* F9 */
       { 0x00, 0x00, 0 }, /* FA */
       { 0x00, 0x00, 0 }, /* FB */
       { 0x00, 0x00, 0 }, /* FC */
       { 0x00, 0x00, 0 }, /* FD */
       { 0x00, 0x00, 0 }, /* FE */
       { 0xFF, 0x00, 1 }, /* FF */
};

static bool max98088_readable_register(struct device *dev, unsigned int reg)
{
       return max98088_access[reg].readable;
}

static bool max98088_volatile_register(struct device *dev, unsigned int reg)
{
       return max98088_access[reg].vol;
}

static const struct regmap_config max98088_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = max98088_readable_register,
	.volatile_reg = max98088_volatile_register,
	.max_register = 0xff,

	.reg_defaults = max98088_reg,
	.num_reg_defaults = ARRAY_SIZE(max98088_reg),
	.cache_type = REGCACHE_RBTREE,
};

/*
 * Load equalizer DSP coefficient configurations registers
 */
static void m98088_eq_band(struct snd_soc_codec *codec, unsigned int dai,
                   unsigned int band, u16 *coefs)
{
       unsigned int eq_reg;
       unsigned int i;

	if (WARN_ON(band > 4) ||
	    WARN_ON(dai > 1))
		return;

       /* Load the base register address */
       eq_reg = dai ? M98088_REG_84_DAI2_EQ_BASE : M98088_REG_52_DAI1_EQ_BASE;

       /* Add the band address offset, note adjustment for word address */
       eq_reg += band * (M98088_COEFS_PER_BAND << 1);

       /* Step through the registers and coefs */
       for (i = 0; i < M98088_COEFS_PER_BAND; i++) {
               snd_soc_write(codec, eq_reg++, M98088_BYTE1(coefs[i]));
               snd_soc_write(codec, eq_reg++, M98088_BYTE0(coefs[i]));
       }
}

/*
 * Excursion limiter modes
 */
static const char *max98088_exmode_texts[] = {
       "Off", "100Hz", "400Hz", "600Hz", "800Hz", "1000Hz", "200-400Hz",
       "400-600Hz", "400-800Hz",
};

static const unsigned int max98088_exmode_values[] = {
       0x00, 0x43, 0x10, 0x20, 0x30, 0x40, 0x11, 0x22, 0x32
};

static SOC_VALUE_ENUM_SINGLE_DECL(max98088_exmode_enum,
				  M98088_REG_41_SPKDHP, 0, 127,
				  max98088_exmode_texts,
				  max98088_exmode_values);

static const char *max98088_ex_thresh[] = { /* volts PP */
       "0.6", "1.2", "1.8", "2.4", "3.0", "3.6", "4.2", "4.8"};
static SOC_ENUM_SINGLE_DECL(max98088_ex_thresh_enum,
			    M98088_REG_42_SPKDHP_THRESH, 0,
			    max98088_ex_thresh);

static const char *max98088_fltr_mode[] = {"Voice", "Music" };
static SOC_ENUM_SINGLE_DECL(max98088_filter_mode_enum,
			    M98088_REG_18_DAI1_FILTERS, 7,
			    max98088_fltr_mode);

static const char *max98088_extmic_text[] = { "None", "MIC1", "MIC2" };

static SOC_ENUM_SINGLE_DECL(max98088_extmic_enum,
			    M98088_REG_48_CFG_MIC, 0,
			    max98088_extmic_text);

static const struct snd_kcontrol_new max98088_extmic_mux =
       SOC_DAPM_ENUM("External MIC Mux", max98088_extmic_enum);

static const char *max98088_dai1_fltr[] = {
       "Off", "fc=258/fs=16k", "fc=500/fs=16k",
       "fc=258/fs=8k", "fc=500/fs=8k", "fc=200"};
static SOC_ENUM_SINGLE_DECL(max98088_dai1_dac_filter_enum,
			    M98088_REG_18_DAI1_FILTERS, 0,
			    max98088_dai1_fltr);
static SOC_ENUM_SINGLE_DECL(max98088_dai1_adc_filter_enum,
			    M98088_REG_18_DAI1_FILTERS, 4,
			    max98088_dai1_fltr);

static int max98088_mic1pre_set(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
{
       struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       unsigned int sel = ucontrol->value.integer.value[0];

       max98088->mic1pre = sel;
       snd_soc_update_bits(codec, M98088_REG_35_LVL_MIC1, M98088_MICPRE_MASK,
               (1+sel)<<M98088_MICPRE_SHIFT);

       return 0;
}

static int max98088_mic1pre_get(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
{
       struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);

       ucontrol->value.integer.value[0] = max98088->mic1pre;
       return 0;
}

static int max98088_mic2pre_set(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
{
       struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       unsigned int sel = ucontrol->value.integer.value[0];

       max98088->mic2pre = sel;
       snd_soc_update_bits(codec, M98088_REG_36_LVL_MIC2, M98088_MICPRE_MASK,
               (1+sel)<<M98088_MICPRE_SHIFT);

       return 0;
}

static int max98088_mic2pre_get(struct snd_kcontrol *kcontrol,
                               struct snd_ctl_elem_value *ucontrol)
{
       struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);

       ucontrol->value.integer.value[0] = max98088->mic2pre;
       return 0;
}

static const unsigned int max98088_micboost_tlv[] = {
       TLV_DB_RANGE_HEAD(2),
       0, 1, TLV_DB_SCALE_ITEM(0, 2000, 0),
       2, 2, TLV_DB_SCALE_ITEM(3000, 0, 0),
};

static const unsigned int max98088_hp_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0, 6, TLV_DB_SCALE_ITEM(-6700, 400, 0),
	7, 14, TLV_DB_SCALE_ITEM(-4000, 300, 0),
	15, 21, TLV_DB_SCALE_ITEM(-1700, 200, 0),
	22, 27, TLV_DB_SCALE_ITEM(-400, 100, 0),
	28, 31, TLV_DB_SCALE_ITEM(150, 50, 0),
};

static const unsigned int max98088_spk_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0, 6, TLV_DB_SCALE_ITEM(-6200, 400, 0),
	7, 14, TLV_DB_SCALE_ITEM(-3500, 300, 0),
	15, 21, TLV_DB_SCALE_ITEM(-1200, 200, 0),
	22, 27, TLV_DB_SCALE_ITEM(100, 100, 0),
	28, 31, TLV_DB_SCALE_ITEM(650, 50, 0),
};

static const struct snd_kcontrol_new max98088_snd_controls[] = {

	SOC_DOUBLE_R_TLV("Headphone Volume", M98088_REG_39_LVL_HP_L,
			 M98088_REG_3A_LVL_HP_R, 0, 31, 0, max98088_hp_tlv),
	SOC_DOUBLE_R_TLV("Speaker Volume", M98088_REG_3D_LVL_SPK_L,
			 M98088_REG_3E_LVL_SPK_R, 0, 31, 0, max98088_spk_tlv),
	SOC_DOUBLE_R_TLV("Receiver Volume", M98088_REG_3B_LVL_REC_L,
			 M98088_REG_3C_LVL_REC_R, 0, 31, 0, max98088_spk_tlv),

       SOC_DOUBLE_R("Headphone Switch", M98088_REG_39_LVL_HP_L,
               M98088_REG_3A_LVL_HP_R, 7, 1, 1),
       SOC_DOUBLE_R("Speaker Switch", M98088_REG_3D_LVL_SPK_L,
               M98088_REG_3E_LVL_SPK_R, 7, 1, 1),
       SOC_DOUBLE_R("Receiver Switch", M98088_REG_3B_LVL_REC_L,
               M98088_REG_3C_LVL_REC_R, 7, 1, 1),

       SOC_SINGLE("MIC1 Volume", M98088_REG_35_LVL_MIC1, 0, 31, 1),
       SOC_SINGLE("MIC2 Volume", M98088_REG_36_LVL_MIC2, 0, 31, 1),

       SOC_SINGLE_EXT_TLV("MIC1 Boost Volume",
                       M98088_REG_35_LVL_MIC1, 5, 2, 0,
                       max98088_mic1pre_get, max98088_mic1pre_set,
                       max98088_micboost_tlv),
       SOC_SINGLE_EXT_TLV("MIC2 Boost Volume",
                       M98088_REG_36_LVL_MIC2, 5, 2, 0,
                       max98088_mic2pre_get, max98088_mic2pre_set,
                       max98088_micboost_tlv),

       SOC_SINGLE("INA Volume", M98088_REG_37_LVL_INA, 0, 7, 1),
       SOC_SINGLE("INB Volume", M98088_REG_38_LVL_INB, 0, 7, 1),

       SOC_SINGLE("ADCL Volume", M98088_REG_33_LVL_ADC_L, 0, 15, 0),
       SOC_SINGLE("ADCR Volume", M98088_REG_34_LVL_ADC_R, 0, 15, 0),

       SOC_SINGLE("ADCL Boost Volume", M98088_REG_33_LVL_ADC_L, 4, 3, 0),
       SOC_SINGLE("ADCR Boost Volume", M98088_REG_34_LVL_ADC_R, 4, 3, 0),

       SOC_SINGLE("EQ1 Switch", M98088_REG_49_CFG_LEVEL, 0, 1, 0),
       SOC_SINGLE("EQ2 Switch", M98088_REG_49_CFG_LEVEL, 1, 1, 0),

       SOC_ENUM("EX Limiter Mode", max98088_exmode_enum),
       SOC_ENUM("EX Limiter Threshold", max98088_ex_thresh_enum),

       SOC_ENUM("DAI1 Filter Mode", max98088_filter_mode_enum),
       SOC_ENUM("DAI1 DAC Filter", max98088_dai1_dac_filter_enum),
       SOC_ENUM("DAI1 ADC Filter", max98088_dai1_adc_filter_enum),
       SOC_SINGLE("DAI2 DC Block Switch", M98088_REG_20_DAI2_FILTERS,
               0, 1, 0),

       SOC_SINGLE("ALC Switch", M98088_REG_43_SPKALC_COMP, 7, 1, 0),
       SOC_SINGLE("ALC Threshold", M98088_REG_43_SPKALC_COMP, 0, 7, 0),
       SOC_SINGLE("ALC Multiband", M98088_REG_43_SPKALC_COMP, 3, 1, 0),
       SOC_SINGLE("ALC Release Time", M98088_REG_43_SPKALC_COMP, 4, 7, 0),

       SOC_SINGLE("PWR Limiter Threshold", M98088_REG_44_PWRLMT_CFG,
               4, 15, 0),
       SOC_SINGLE("PWR Limiter Weight", M98088_REG_44_PWRLMT_CFG, 0, 7, 0),
       SOC_SINGLE("PWR Limiter Time1", M98088_REG_45_PWRLMT_TIME, 0, 15, 0),
       SOC_SINGLE("PWR Limiter Time2", M98088_REG_45_PWRLMT_TIME, 4, 15, 0),

       SOC_SINGLE("THD Limiter Threshold", M98088_REG_46_THDLMT_CFG, 4, 15, 0),
       SOC_SINGLE("THD Limiter Time", M98088_REG_46_THDLMT_CFG, 0, 7, 0),
};

/* Left speaker mixer switch */
static const struct snd_kcontrol_new max98088_left_speaker_mixer_controls[] = {
       SOC_DAPM_SINGLE("Left DAC1 Switch", M98088_REG_2B_MIX_SPK_LEFT, 0, 1, 0),
       SOC_DAPM_SINGLE("Right DAC1 Switch", M98088_REG_2B_MIX_SPK_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("Left DAC2 Switch", M98088_REG_2B_MIX_SPK_LEFT, 0, 1, 0),
       SOC_DAPM_SINGLE("Right DAC2 Switch", M98088_REG_2B_MIX_SPK_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_2B_MIX_SPK_LEFT, 5, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_2B_MIX_SPK_LEFT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_2B_MIX_SPK_LEFT, 1, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_2B_MIX_SPK_LEFT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_2B_MIX_SPK_LEFT, 3, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_2B_MIX_SPK_LEFT, 4, 1, 0),
};

/* Right speaker mixer switch */
static const struct snd_kcontrol_new max98088_right_speaker_mixer_controls[] = {
       SOC_DAPM_SINGLE("Left DAC1 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("Right DAC1 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 0, 1, 0),
       SOC_DAPM_SINGLE("Left DAC2 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("Right DAC2 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 0, 1, 0),
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 5, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 1, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 3, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_2C_MIX_SPK_RIGHT, 4, 1, 0),
};

/* Left headphone mixer switch */
static const struct snd_kcontrol_new max98088_left_hp_mixer_controls[] = {
       SOC_DAPM_SINGLE("Left DAC1 Switch", M98088_REG_25_MIX_HP_LEFT, 0, 1, 0),
       SOC_DAPM_SINGLE("Right DAC1 Switch", M98088_REG_25_MIX_HP_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("Left DAC2 Switch", M98088_REG_25_MIX_HP_LEFT, 0, 1, 0),
       SOC_DAPM_SINGLE("Right DAC2 Switch", M98088_REG_25_MIX_HP_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_25_MIX_HP_LEFT, 5, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_25_MIX_HP_LEFT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_25_MIX_HP_LEFT, 1, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_25_MIX_HP_LEFT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_25_MIX_HP_LEFT, 3, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_25_MIX_HP_LEFT, 4, 1, 0),
};

/* Right headphone mixer switch */
static const struct snd_kcontrol_new max98088_right_hp_mixer_controls[] = {
       SOC_DAPM_SINGLE("Left DAC1 Switch", M98088_REG_26_MIX_HP_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("Right DAC1 Switch", M98088_REG_26_MIX_HP_RIGHT, 0, 1, 0),
       SOC_DAPM_SINGLE("Left DAC2 Switch", M98088_REG_26_MIX_HP_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("Right DAC2 Switch", M98088_REG_26_MIX_HP_RIGHT, 0, 1, 0),
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_26_MIX_HP_RIGHT, 5, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_26_MIX_HP_RIGHT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_26_MIX_HP_RIGHT, 1, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_26_MIX_HP_RIGHT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_26_MIX_HP_RIGHT, 3, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_26_MIX_HP_RIGHT, 4, 1, 0),
};

/* Left earpiece/receiver mixer switch */
static const struct snd_kcontrol_new max98088_left_rec_mixer_controls[] = {
       SOC_DAPM_SINGLE("Left DAC1 Switch", M98088_REG_28_MIX_REC_LEFT, 0, 1, 0),
       SOC_DAPM_SINGLE("Right DAC1 Switch", M98088_REG_28_MIX_REC_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("Left DAC2 Switch", M98088_REG_28_MIX_REC_LEFT, 0, 1, 0),
       SOC_DAPM_SINGLE("Right DAC2 Switch", M98088_REG_28_MIX_REC_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_28_MIX_REC_LEFT, 5, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_28_MIX_REC_LEFT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_28_MIX_REC_LEFT, 1, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_28_MIX_REC_LEFT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_28_MIX_REC_LEFT, 3, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_28_MIX_REC_LEFT, 4, 1, 0),
};

/* Right earpiece/receiver mixer switch */
static const struct snd_kcontrol_new max98088_right_rec_mixer_controls[] = {
       SOC_DAPM_SINGLE("Left DAC1 Switch", M98088_REG_29_MIX_REC_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("Right DAC1 Switch", M98088_REG_29_MIX_REC_RIGHT, 0, 1, 0),
       SOC_DAPM_SINGLE("Left DAC2 Switch", M98088_REG_29_MIX_REC_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("Right DAC2 Switch", M98088_REG_29_MIX_REC_RIGHT, 0, 1, 0),
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_29_MIX_REC_RIGHT, 5, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_29_MIX_REC_RIGHT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_29_MIX_REC_RIGHT, 1, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_29_MIX_REC_RIGHT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_29_MIX_REC_RIGHT, 3, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_29_MIX_REC_RIGHT, 4, 1, 0),
};

/* Left ADC mixer switch */
static const struct snd_kcontrol_new max98088_left_ADC_mixer_controls[] = {
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_23_MIX_ADC_LEFT, 7, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_23_MIX_ADC_LEFT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_23_MIX_ADC_LEFT, 3, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_23_MIX_ADC_LEFT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_23_MIX_ADC_LEFT, 1, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_23_MIX_ADC_LEFT, 0, 1, 0),
};

/* Right ADC mixer switch */
static const struct snd_kcontrol_new max98088_right_ADC_mixer_controls[] = {
       SOC_DAPM_SINGLE("MIC1 Switch", M98088_REG_24_MIX_ADC_RIGHT, 7, 1, 0),
       SOC_DAPM_SINGLE("MIC2 Switch", M98088_REG_24_MIX_ADC_RIGHT, 6, 1, 0),
       SOC_DAPM_SINGLE("INA1 Switch", M98088_REG_24_MIX_ADC_RIGHT, 3, 1, 0),
       SOC_DAPM_SINGLE("INA2 Switch", M98088_REG_24_MIX_ADC_RIGHT, 2, 1, 0),
       SOC_DAPM_SINGLE("INB1 Switch", M98088_REG_24_MIX_ADC_RIGHT, 1, 1, 0),
       SOC_DAPM_SINGLE("INB2 Switch", M98088_REG_24_MIX_ADC_RIGHT, 0, 1, 0),
};

static int max98088_mic_event(struct snd_soc_dapm_widget *w,
                            struct snd_kcontrol *kcontrol, int event)
{
       struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);

       switch (event) {
       case SND_SOC_DAPM_POST_PMU:
               if (w->reg == M98088_REG_35_LVL_MIC1) {
                       snd_soc_update_bits(codec, w->reg, M98088_MICPRE_MASK,
                               (1+max98088->mic1pre)<<M98088_MICPRE_SHIFT);
               } else {
                       snd_soc_update_bits(codec, w->reg, M98088_MICPRE_MASK,
                               (1+max98088->mic2pre)<<M98088_MICPRE_SHIFT);
               }
               break;
       case SND_SOC_DAPM_POST_PMD:
               snd_soc_update_bits(codec, w->reg, M98088_MICPRE_MASK, 0);
               break;
       default:
               return -EINVAL;
       }

       return 0;
}

/*
 * The line inputs are 2-channel stereo inputs with the left
 * and right channels sharing a common PGA power control signal.
 */
static int max98088_line_pga(struct snd_soc_dapm_widget *w,
                            int event, int line, u8 channel)
{
       struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       u8 *state;

	if (WARN_ON(!(channel == 1 || channel == 2)))
		return -EINVAL;

       switch (line) {
       case LINE_INA:
               state = &max98088->ina_state;
               break;
       case LINE_INB:
               state = &max98088->inb_state;
               break;
       default:
               return -EINVAL;
       }

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

static int max98088_pga_ina1_event(struct snd_soc_dapm_widget *w,
                                  struct snd_kcontrol *k, int event)
{
       return max98088_line_pga(w, event, LINE_INA, 1);
}

static int max98088_pga_ina2_event(struct snd_soc_dapm_widget *w,
                                  struct snd_kcontrol *k, int event)
{
       return max98088_line_pga(w, event, LINE_INA, 2);
}

static int max98088_pga_inb1_event(struct snd_soc_dapm_widget *w,
                                  struct snd_kcontrol *k, int event)
{
       return max98088_line_pga(w, event, LINE_INB, 1);
}

static int max98088_pga_inb2_event(struct snd_soc_dapm_widget *w,
                                  struct snd_kcontrol *k, int event)
{
       return max98088_line_pga(w, event, LINE_INB, 2);
}

static const struct snd_soc_dapm_widget max98088_dapm_widgets[] = {

       SND_SOC_DAPM_ADC("ADCL", "HiFi Capture", M98088_REG_4C_PWR_EN_IN, 1, 0),
       SND_SOC_DAPM_ADC("ADCR", "HiFi Capture", M98088_REG_4C_PWR_EN_IN, 0, 0),

       SND_SOC_DAPM_DAC("DACL1", "HiFi Playback",
               M98088_REG_4D_PWR_EN_OUT, 1, 0),
       SND_SOC_DAPM_DAC("DACR1", "HiFi Playback",
               M98088_REG_4D_PWR_EN_OUT, 0, 0),
       SND_SOC_DAPM_DAC("DACL2", "Aux Playback",
               M98088_REG_4D_PWR_EN_OUT, 1, 0),
       SND_SOC_DAPM_DAC("DACR2", "Aux Playback",
               M98088_REG_4D_PWR_EN_OUT, 0, 0),

       SND_SOC_DAPM_PGA("HP Left Out", M98088_REG_4D_PWR_EN_OUT,
               7, 0, NULL, 0),
       SND_SOC_DAPM_PGA("HP Right Out", M98088_REG_4D_PWR_EN_OUT,
               6, 0, NULL, 0),

       SND_SOC_DAPM_PGA("SPK Left Out", M98088_REG_4D_PWR_EN_OUT,
               5, 0, NULL, 0),
       SND_SOC_DAPM_PGA("SPK Right Out", M98088_REG_4D_PWR_EN_OUT,
               4, 0, NULL, 0),

       SND_SOC_DAPM_PGA("REC Left Out", M98088_REG_4D_PWR_EN_OUT,
               3, 0, NULL, 0),
       SND_SOC_DAPM_PGA("REC Right Out", M98088_REG_4D_PWR_EN_OUT,
               2, 0, NULL, 0),

       SND_SOC_DAPM_MUX("External MIC", SND_SOC_NOPM, 0, 0,
               &max98088_extmic_mux),

       SND_SOC_DAPM_MIXER("Left HP Mixer", SND_SOC_NOPM, 0, 0,
               &max98088_left_hp_mixer_controls[0],
               ARRAY_SIZE(max98088_left_hp_mixer_controls)),

       SND_SOC_DAPM_MIXER("Right HP Mixer", SND_SOC_NOPM, 0, 0,
               &max98088_right_hp_mixer_controls[0],
               ARRAY_SIZE(max98088_right_hp_mixer_controls)),

       SND_SOC_DAPM_MIXER("Left SPK Mixer", SND_SOC_NOPM, 0, 0,
               &max98088_left_speaker_mixer_controls[0],
               ARRAY_SIZE(max98088_left_speaker_mixer_controls)),

       SND_SOC_DAPM_MIXER("Right SPK Mixer", SND_SOC_NOPM, 0, 0,
               &max98088_right_speaker_mixer_controls[0],
               ARRAY_SIZE(max98088_right_speaker_mixer_controls)),

       SND_SOC_DAPM_MIXER("Left REC Mixer", SND_SOC_NOPM, 0, 0,
         &max98088_left_rec_mixer_controls[0],
               ARRAY_SIZE(max98088_left_rec_mixer_controls)),

       SND_SOC_DAPM_MIXER("Right REC Mixer", SND_SOC_NOPM, 0, 0,
         &max98088_right_rec_mixer_controls[0],
               ARRAY_SIZE(max98088_right_rec_mixer_controls)),

       SND_SOC_DAPM_MIXER("Left ADC Mixer", SND_SOC_NOPM, 0, 0,
               &max98088_left_ADC_mixer_controls[0],
               ARRAY_SIZE(max98088_left_ADC_mixer_controls)),

       SND_SOC_DAPM_MIXER("Right ADC Mixer", SND_SOC_NOPM, 0, 0,
               &max98088_right_ADC_mixer_controls[0],
               ARRAY_SIZE(max98088_right_ADC_mixer_controls)),

       SND_SOC_DAPM_PGA_E("MIC1 Input", M98088_REG_35_LVL_MIC1,
               5, 0, NULL, 0, max98088_mic_event,
               SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

       SND_SOC_DAPM_PGA_E("MIC2 Input", M98088_REG_36_LVL_MIC2,
               5, 0, NULL, 0, max98088_mic_event,
               SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

       SND_SOC_DAPM_PGA_E("INA1 Input", M98088_REG_4C_PWR_EN_IN,
               7, 0, NULL, 0, max98088_pga_ina1_event,
               SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

       SND_SOC_DAPM_PGA_E("INA2 Input", M98088_REG_4C_PWR_EN_IN,
               7, 0, NULL, 0, max98088_pga_ina2_event,
               SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

       SND_SOC_DAPM_PGA_E("INB1 Input", M98088_REG_4C_PWR_EN_IN,
               6, 0, NULL, 0, max98088_pga_inb1_event,
               SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

       SND_SOC_DAPM_PGA_E("INB2 Input", M98088_REG_4C_PWR_EN_IN,
               6, 0, NULL, 0, max98088_pga_inb2_event,
               SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

       SND_SOC_DAPM_MICBIAS("MICBIAS", M98088_REG_4C_PWR_EN_IN, 3, 0),

       SND_SOC_DAPM_OUTPUT("HPL"),
       SND_SOC_DAPM_OUTPUT("HPR"),
       SND_SOC_DAPM_OUTPUT("SPKL"),
       SND_SOC_DAPM_OUTPUT("SPKR"),
       SND_SOC_DAPM_OUTPUT("RECL"),
       SND_SOC_DAPM_OUTPUT("RECR"),

       SND_SOC_DAPM_INPUT("MIC1"),
       SND_SOC_DAPM_INPUT("MIC2"),
       SND_SOC_DAPM_INPUT("INA1"),
       SND_SOC_DAPM_INPUT("INA2"),
       SND_SOC_DAPM_INPUT("INB1"),
       SND_SOC_DAPM_INPUT("INB2"),
};

static const struct snd_soc_dapm_route max98088_audio_map[] = {
       /* Left headphone output mixer */
       {"Left HP Mixer", "Left DAC1 Switch", "DACL1"},
       {"Left HP Mixer", "Left DAC2 Switch", "DACL2"},
       {"Left HP Mixer", "Right DAC1 Switch", "DACR1"},
       {"Left HP Mixer", "Right DAC2 Switch", "DACR2"},
       {"Left HP Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Left HP Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Left HP Mixer", "INA1 Switch", "INA1 Input"},
       {"Left HP Mixer", "INA2 Switch", "INA2 Input"},
       {"Left HP Mixer", "INB1 Switch", "INB1 Input"},
       {"Left HP Mixer", "INB2 Switch", "INB2 Input"},

       /* Right headphone output mixer */
       {"Right HP Mixer", "Left DAC1 Switch", "DACL1"},
       {"Right HP Mixer", "Left DAC2 Switch", "DACL2"  },
       {"Right HP Mixer", "Right DAC1 Switch", "DACR1"},
       {"Right HP Mixer", "Right DAC2 Switch", "DACR2"},
       {"Right HP Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Right HP Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Right HP Mixer", "INA1 Switch", "INA1 Input"},
       {"Right HP Mixer", "INA2 Switch", "INA2 Input"},
       {"Right HP Mixer", "INB1 Switch", "INB1 Input"},
       {"Right HP Mixer", "INB2 Switch", "INB2 Input"},

       /* Left speaker output mixer */
       {"Left SPK Mixer", "Left DAC1 Switch", "DACL1"},
       {"Left SPK Mixer", "Left DAC2 Switch", "DACL2"},
       {"Left SPK Mixer", "Right DAC1 Switch", "DACR1"},
       {"Left SPK Mixer", "Right DAC2 Switch", "DACR2"},
       {"Left SPK Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Left SPK Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Left SPK Mixer", "INA1 Switch", "INA1 Input"},
       {"Left SPK Mixer", "INA2 Switch", "INA2 Input"},
       {"Left SPK Mixer", "INB1 Switch", "INB1 Input"},
       {"Left SPK Mixer", "INB2 Switch", "INB2 Input"},

       /* Right speaker output mixer */
       {"Right SPK Mixer", "Left DAC1 Switch", "DACL1"},
       {"Right SPK Mixer", "Left DAC2 Switch", "DACL2"},
       {"Right SPK Mixer", "Right DAC1 Switch", "DACR1"},
       {"Right SPK Mixer", "Right DAC2 Switch", "DACR2"},
       {"Right SPK Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Right SPK Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Right SPK Mixer", "INA1 Switch", "INA1 Input"},
       {"Right SPK Mixer", "INA2 Switch", "INA2 Input"},
       {"Right SPK Mixer", "INB1 Switch", "INB1 Input"},
       {"Right SPK Mixer", "INB2 Switch", "INB2 Input"},

       /* Earpiece/Receiver output mixer */
       {"Left REC Mixer", "Left DAC1 Switch", "DACL1"},
       {"Left REC Mixer", "Left DAC2 Switch", "DACL2"},
       {"Left REC Mixer", "Right DAC1 Switch", "DACR1"},
       {"Left REC Mixer", "Right DAC2 Switch", "DACR2"},
       {"Left REC Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Left REC Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Left REC Mixer", "INA1 Switch", "INA1 Input"},
       {"Left REC Mixer", "INA2 Switch", "INA2 Input"},
       {"Left REC Mixer", "INB1 Switch", "INB1 Input"},
       {"Left REC Mixer", "INB2 Switch", "INB2 Input"},

       /* Earpiece/Receiver output mixer */
       {"Right REC Mixer", "Left DAC1 Switch", "DACL1"},
       {"Right REC Mixer", "Left DAC2 Switch", "DACL2"},
       {"Right REC Mixer", "Right DAC1 Switch", "DACR1"},
       {"Right REC Mixer", "Right DAC2 Switch", "DACR2"},
       {"Right REC Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Right REC Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Right REC Mixer", "INA1 Switch", "INA1 Input"},
       {"Right REC Mixer", "INA2 Switch", "INA2 Input"},
       {"Right REC Mixer", "INB1 Switch", "INB1 Input"},
       {"Right REC Mixer", "INB2 Switch", "INB2 Input"},

       {"HP Left Out", NULL, "Left HP Mixer"},
       {"HP Right Out", NULL, "Right HP Mixer"},
       {"SPK Left Out", NULL, "Left SPK Mixer"},
       {"SPK Right Out", NULL, "Right SPK Mixer"},
       {"REC Left Out", NULL, "Left REC Mixer"},
       {"REC Right Out", NULL, "Right REC Mixer"},

       {"HPL", NULL, "HP Left Out"},
       {"HPR", NULL, "HP Right Out"},
       {"SPKL", NULL, "SPK Left Out"},
       {"SPKR", NULL, "SPK Right Out"},
       {"RECL", NULL, "REC Left Out"},
       {"RECR", NULL, "REC Right Out"},

       /* Left ADC input mixer */
       {"Left ADC Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Left ADC Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Left ADC Mixer", "INA1 Switch", "INA1 Input"},
       {"Left ADC Mixer", "INA2 Switch", "INA2 Input"},
       {"Left ADC Mixer", "INB1 Switch", "INB1 Input"},
       {"Left ADC Mixer", "INB2 Switch", "INB2 Input"},

       /* Right ADC input mixer */
       {"Right ADC Mixer", "MIC1 Switch", "MIC1 Input"},
       {"Right ADC Mixer", "MIC2 Switch", "MIC2 Input"},
       {"Right ADC Mixer", "INA1 Switch", "INA1 Input"},
       {"Right ADC Mixer", "INA2 Switch", "INA2 Input"},
       {"Right ADC Mixer", "INB1 Switch", "INB1 Input"},
       {"Right ADC Mixer", "INB2 Switch", "INB2 Input"},

       /* Inputs */
       {"ADCL", NULL, "Left ADC Mixer"},
       {"ADCR", NULL, "Right ADC Mixer"},
       {"INA1 Input", NULL, "INA1"},
       {"INA2 Input", NULL, "INA2"},
       {"INB1 Input", NULL, "INB1"},
       {"INB2 Input", NULL, "INB2"},
       {"MIC1 Input", NULL, "MIC1"},
       {"MIC2 Input", NULL, "MIC2"},
};

/* codec mclk clock divider coefficients */
static const struct {
       u32 rate;
       u8  sr;
} rate_table[] = {
       {8000,  0x10},
       {11025, 0x20},
       {16000, 0x30},
       {22050, 0x40},
       {24000, 0x50},
       {32000, 0x60},
       {44100, 0x70},
       {48000, 0x80},
       {88200, 0x90},
       {96000, 0xA0},
};

static inline int rate_value(int rate, u8 *value)
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

static int max98088_dai1_hw_params(struct snd_pcm_substream *substream,
                                  struct snd_pcm_hw_params *params,
                                  struct snd_soc_dai *dai)
{
       struct snd_soc_codec *codec = dai->codec;
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_cdata *cdata;
       unsigned long long ni;
       unsigned int rate;
       u8 regval;

       cdata = &max98088->dai[0];

       rate = params_rate(params);

       switch (params_width(params)) {
       case 16:
               snd_soc_update_bits(codec, M98088_REG_14_DAI1_FORMAT,
                       M98088_DAI_WS, 0);
               break;
       case 24:
               snd_soc_update_bits(codec, M98088_REG_14_DAI1_FORMAT,
                       M98088_DAI_WS, M98088_DAI_WS);
               break;
       default:
               return -EINVAL;
       }

       snd_soc_update_bits(codec, M98088_REG_51_PWR_SYS, M98088_SHDNRUN, 0);

       if (rate_value(rate, &regval))
               return -EINVAL;

       snd_soc_update_bits(codec, M98088_REG_11_DAI1_CLKMODE,
               M98088_CLKMODE_MASK, regval);
       cdata->rate = rate;

       /* Configure NI when operating as master */
       if (snd_soc_read(codec, M98088_REG_14_DAI1_FORMAT)
               & M98088_DAI_MAS) {
               if (max98088->sysclk == 0) {
                       dev_err(codec->dev, "Invalid system clock frequency\n");
                       return -EINVAL;
               }
               ni = 65536ULL * (rate < 50000 ? 96ULL : 48ULL)
                               * (unsigned long long int)rate;
               do_div(ni, (unsigned long long int)max98088->sysclk);
               snd_soc_write(codec, M98088_REG_12_DAI1_CLKCFG_HI,
                       (ni >> 8) & 0x7F);
               snd_soc_write(codec, M98088_REG_13_DAI1_CLKCFG_LO,
                       ni & 0xFF);
       }

       /* Update sample rate mode */
       if (rate < 50000)
               snd_soc_update_bits(codec, M98088_REG_18_DAI1_FILTERS,
                       M98088_DAI_DHF, 0);
       else
               snd_soc_update_bits(codec, M98088_REG_18_DAI1_FILTERS,
                       M98088_DAI_DHF, M98088_DAI_DHF);

       snd_soc_update_bits(codec, M98088_REG_51_PWR_SYS, M98088_SHDNRUN,
               M98088_SHDNRUN);

       return 0;
}

static int max98088_dai2_hw_params(struct snd_pcm_substream *substream,
                                  struct snd_pcm_hw_params *params,
                                  struct snd_soc_dai *dai)
{
       struct snd_soc_codec *codec = dai->codec;
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_cdata *cdata;
       unsigned long long ni;
       unsigned int rate;
       u8 regval;

       cdata = &max98088->dai[1];

       rate = params_rate(params);

       switch (params_width(params)) {
       case 16:
               snd_soc_update_bits(codec, M98088_REG_1C_DAI2_FORMAT,
                       M98088_DAI_WS, 0);
               break;
       case 24:
               snd_soc_update_bits(codec, M98088_REG_1C_DAI2_FORMAT,
                       M98088_DAI_WS, M98088_DAI_WS);
               break;
       default:
               return -EINVAL;
       }

       snd_soc_update_bits(codec, M98088_REG_51_PWR_SYS, M98088_SHDNRUN, 0);

       if (rate_value(rate, &regval))
               return -EINVAL;

       snd_soc_update_bits(codec, M98088_REG_19_DAI2_CLKMODE,
               M98088_CLKMODE_MASK, regval);
       cdata->rate = rate;

       /* Configure NI when operating as master */
       if (snd_soc_read(codec, M98088_REG_1C_DAI2_FORMAT)
               & M98088_DAI_MAS) {
               if (max98088->sysclk == 0) {
                       dev_err(codec->dev, "Invalid system clock frequency\n");
                       return -EINVAL;
               }
               ni = 65536ULL * (rate < 50000 ? 96ULL : 48ULL)
                               * (unsigned long long int)rate;
               do_div(ni, (unsigned long long int)max98088->sysclk);
               snd_soc_write(codec, M98088_REG_1A_DAI2_CLKCFG_HI,
                       (ni >> 8) & 0x7F);
               snd_soc_write(codec, M98088_REG_1B_DAI2_CLKCFG_LO,
                       ni & 0xFF);
       }

       /* Update sample rate mode */
       if (rate < 50000)
               snd_soc_update_bits(codec, M98088_REG_20_DAI2_FILTERS,
                       M98088_DAI_DHF, 0);
       else
               snd_soc_update_bits(codec, M98088_REG_20_DAI2_FILTERS,
                       M98088_DAI_DHF, M98088_DAI_DHF);

       snd_soc_update_bits(codec, M98088_REG_51_PWR_SYS, M98088_SHDNRUN,
               M98088_SHDNRUN);

       return 0;
}

static int max98088_dai_set_sysclk(struct snd_soc_dai *dai,
                                  int clk_id, unsigned int freq, int dir)
{
       struct snd_soc_codec *codec = dai->codec;
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);

       /* Requested clock frequency is already setup */
       if (freq == max98088->sysclk)
               return 0;

       /* Setup clocks for slave mode, and using the PLL
        * PSCLK = 0x01 (when master clk is 10MHz to 20MHz)
        *         0x02 (when master clk is 20MHz to 30MHz)..
        */
       if ((freq >= 10000000) && (freq < 20000000)) {
               snd_soc_write(codec, M98088_REG_10_SYS_CLK, 0x10);
       } else if ((freq >= 20000000) && (freq < 30000000)) {
               snd_soc_write(codec, M98088_REG_10_SYS_CLK, 0x20);
       } else {
               dev_err(codec->dev, "Invalid master clock frequency\n");
               return -EINVAL;
       }

       if (snd_soc_read(codec, M98088_REG_51_PWR_SYS)  & M98088_SHDNRUN) {
               snd_soc_update_bits(codec, M98088_REG_51_PWR_SYS,
                       M98088_SHDNRUN, 0);
               snd_soc_update_bits(codec, M98088_REG_51_PWR_SYS,
                       M98088_SHDNRUN, M98088_SHDNRUN);
       }

       dev_dbg(dai->dev, "Clock source is %d at %uHz\n", clk_id, freq);

       max98088->sysclk = freq;
       return 0;
}

static int max98088_dai1_set_fmt(struct snd_soc_dai *codec_dai,
                                unsigned int fmt)
{
       struct snd_soc_codec *codec = codec_dai->codec;
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_cdata *cdata;
       u8 reg15val;
       u8 reg14val = 0;

       cdata = &max98088->dai[0];

       if (fmt != cdata->fmt) {
               cdata->fmt = fmt;

               switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
               case SND_SOC_DAIFMT_CBS_CFS:
                       /* Slave mode PLL */
                       snd_soc_write(codec, M98088_REG_12_DAI1_CLKCFG_HI,
                               0x80);
                       snd_soc_write(codec, M98088_REG_13_DAI1_CLKCFG_LO,
                               0x00);
                       break;
               case SND_SOC_DAIFMT_CBM_CFM:
                       /* Set to master mode */
                       reg14val |= M98088_DAI_MAS;
                       break;
               case SND_SOC_DAIFMT_CBS_CFM:
               case SND_SOC_DAIFMT_CBM_CFS:
               default:
                       dev_err(codec->dev, "Clock mode unsupported");
                       return -EINVAL;
               }

               switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
               case SND_SOC_DAIFMT_I2S:
                       reg14val |= M98088_DAI_DLY;
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
                       reg14val |= M98088_DAI_WCI;
                       break;
               case SND_SOC_DAIFMT_IB_NF:
                       reg14val |= M98088_DAI_BCI;
                       break;
               case SND_SOC_DAIFMT_IB_IF:
                       reg14val |= M98088_DAI_BCI|M98088_DAI_WCI;
                       break;
               default:
                       return -EINVAL;
               }

               snd_soc_update_bits(codec, M98088_REG_14_DAI1_FORMAT,
                       M98088_DAI_MAS | M98088_DAI_DLY | M98088_DAI_BCI |
                       M98088_DAI_WCI, reg14val);

               reg15val = M98088_DAI_BSEL64;
               if (max98088->digmic)
                       reg15val |= M98088_DAI_OSR64;
               snd_soc_write(codec, M98088_REG_15_DAI1_CLOCK, reg15val);
       }

       return 0;
}

static int max98088_dai2_set_fmt(struct snd_soc_dai *codec_dai,
                                unsigned int fmt)
{
       struct snd_soc_codec *codec = codec_dai->codec;
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_cdata *cdata;
       u8 reg1Cval = 0;

       cdata = &max98088->dai[1];

       if (fmt != cdata->fmt) {
               cdata->fmt = fmt;

               switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
               case SND_SOC_DAIFMT_CBS_CFS:
                       /* Slave mode PLL */
                       snd_soc_write(codec, M98088_REG_1A_DAI2_CLKCFG_HI,
                               0x80);
                       snd_soc_write(codec, M98088_REG_1B_DAI2_CLKCFG_LO,
                               0x00);
                       break;
               case SND_SOC_DAIFMT_CBM_CFM:
                       /* Set to master mode */
                       reg1Cval |= M98088_DAI_MAS;
                       break;
               case SND_SOC_DAIFMT_CBS_CFM:
               case SND_SOC_DAIFMT_CBM_CFS:
               default:
                       dev_err(codec->dev, "Clock mode unsupported");
                       return -EINVAL;
               }

               switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
               case SND_SOC_DAIFMT_I2S:
                       reg1Cval |= M98088_DAI_DLY;
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
                       reg1Cval |= M98088_DAI_WCI;
                       break;
               case SND_SOC_DAIFMT_IB_NF:
                       reg1Cval |= M98088_DAI_BCI;
                       break;
               case SND_SOC_DAIFMT_IB_IF:
                       reg1Cval |= M98088_DAI_BCI|M98088_DAI_WCI;
                       break;
               default:
                       return -EINVAL;
               }

               snd_soc_update_bits(codec, M98088_REG_1C_DAI2_FORMAT,
                       M98088_DAI_MAS | M98088_DAI_DLY | M98088_DAI_BCI |
                       M98088_DAI_WCI, reg1Cval);

               snd_soc_write(codec, M98088_REG_1D_DAI2_CLOCK,
                       M98088_DAI_BSEL64);
       }

       return 0;
}

static int max98088_dai1_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
       struct snd_soc_codec *codec = codec_dai->codec;
       int reg;

       if (mute)
               reg = M98088_DAI_MUTE;
       else
               reg = 0;

       snd_soc_update_bits(codec, M98088_REG_2F_LVL_DAI1_PLAY,
                           M98088_DAI_MUTE_MASK, reg);
       return 0;
}

static int max98088_dai2_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
       struct snd_soc_codec *codec = codec_dai->codec;
       int reg;

       if (mute)
               reg = M98088_DAI_MUTE;
       else
               reg = 0;

       snd_soc_update_bits(codec, M98088_REG_31_LVL_DAI2_PLAY,
                           M98088_DAI_MUTE_MASK, reg);
       return 0;
}

static int max98088_set_bias_level(struct snd_soc_codec *codec,
                                  enum snd_soc_bias_level level)
{
	struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF)
			regcache_sync(max98088->regmap);

		snd_soc_update_bits(codec, M98088_REG_4C_PWR_EN_IN,
				   M98088_MBEN, M98088_MBEN);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, M98088_REG_4C_PWR_EN_IN,
				    M98088_MBEN, 0);
		regcache_mark_dirty(max98088->regmap);
		break;
	}
	return 0;
}

#define MAX98088_RATES SNDRV_PCM_RATE_8000_96000
#define MAX98088_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops max98088_dai1_ops = {
       .set_sysclk = max98088_dai_set_sysclk,
       .set_fmt = max98088_dai1_set_fmt,
       .hw_params = max98088_dai1_hw_params,
       .digital_mute = max98088_dai1_digital_mute,
};

static const struct snd_soc_dai_ops max98088_dai2_ops = {
       .set_sysclk = max98088_dai_set_sysclk,
       .set_fmt = max98088_dai2_set_fmt,
       .hw_params = max98088_dai2_hw_params,
       .digital_mute = max98088_dai2_digital_mute,
};

static struct snd_soc_dai_driver max98088_dai[] = {
{
       .name = "HiFi",
       .playback = {
               .stream_name = "HiFi Playback",
               .channels_min = 1,
               .channels_max = 2,
               .rates = MAX98088_RATES,
               .formats = MAX98088_FORMATS,
       },
       .capture = {
               .stream_name = "HiFi Capture",
               .channels_min = 1,
               .channels_max = 2,
               .rates = MAX98088_RATES,
               .formats = MAX98088_FORMATS,
       },
        .ops = &max98088_dai1_ops,
},
{
       .name = "Aux",
       .playback = {
               .stream_name = "Aux Playback",
               .channels_min = 1,
               .channels_max = 2,
               .rates = MAX98088_RATES,
               .formats = MAX98088_FORMATS,
       },
       .ops = &max98088_dai2_ops,
}
};

static const char *eq_mode_name[] = {"EQ1 Mode", "EQ2 Mode"};

static int max98088_get_channel(struct snd_soc_codec *codec, const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(eq_mode_name); i++)
		if (strcmp(name, eq_mode_name[i]) == 0)
			return i;

	/* Shouldn't happen */
	dev_err(codec->dev, "Bad EQ channel name '%s'\n", name);
	return -EINVAL;
}

static void max98088_setup_eq1(struct snd_soc_codec *codec)
{
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_pdata *pdata = max98088->pdata;
       struct max98088_eq_cfg *coef_set;
       int best, best_val, save, i, sel, fs;
       struct max98088_cdata *cdata;

       cdata = &max98088->dai[0];

       if (!pdata || !max98088->eq_textcnt)
               return;

       /* Find the selected configuration with nearest sample rate */
       fs = cdata->rate;
       sel = cdata->eq_sel;

       best = 0;
       best_val = INT_MAX;
       for (i = 0; i < pdata->eq_cfgcnt; i++) {
               if (strcmp(pdata->eq_cfg[i].name, max98088->eq_texts[sel]) == 0 &&
                   abs(pdata->eq_cfg[i].rate - fs) < best_val) {
                       best = i;
                       best_val = abs(pdata->eq_cfg[i].rate - fs);
               }
       }

       dev_dbg(codec->dev, "Selected %s/%dHz for %dHz sample rate\n",
               pdata->eq_cfg[best].name,
               pdata->eq_cfg[best].rate, fs);

       /* Disable EQ while configuring, and save current on/off state */
       save = snd_soc_read(codec, M98088_REG_49_CFG_LEVEL);
       snd_soc_update_bits(codec, M98088_REG_49_CFG_LEVEL, M98088_EQ1EN, 0);

       coef_set = &pdata->eq_cfg[sel];

       m98088_eq_band(codec, 0, 0, coef_set->band1);
       m98088_eq_band(codec, 0, 1, coef_set->band2);
       m98088_eq_band(codec, 0, 2, coef_set->band3);
       m98088_eq_band(codec, 0, 3, coef_set->band4);
       m98088_eq_band(codec, 0, 4, coef_set->band5);

       /* Restore the original on/off state */
       snd_soc_update_bits(codec, M98088_REG_49_CFG_LEVEL, M98088_EQ1EN, save);
}

static void max98088_setup_eq2(struct snd_soc_codec *codec)
{
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_pdata *pdata = max98088->pdata;
       struct max98088_eq_cfg *coef_set;
       int best, best_val, save, i, sel, fs;
       struct max98088_cdata *cdata;

       cdata = &max98088->dai[1];

       if (!pdata || !max98088->eq_textcnt)
               return;

       /* Find the selected configuration with nearest sample rate */
       fs = cdata->rate;

       sel = cdata->eq_sel;
       best = 0;
       best_val = INT_MAX;
       for (i = 0; i < pdata->eq_cfgcnt; i++) {
               if (strcmp(pdata->eq_cfg[i].name, max98088->eq_texts[sel]) == 0 &&
                   abs(pdata->eq_cfg[i].rate - fs) < best_val) {
                       best = i;
                       best_val = abs(pdata->eq_cfg[i].rate - fs);
               }
       }

       dev_dbg(codec->dev, "Selected %s/%dHz for %dHz sample rate\n",
               pdata->eq_cfg[best].name,
               pdata->eq_cfg[best].rate, fs);

       /* Disable EQ while configuring, and save current on/off state */
       save = snd_soc_read(codec, M98088_REG_49_CFG_LEVEL);
       snd_soc_update_bits(codec, M98088_REG_49_CFG_LEVEL, M98088_EQ2EN, 0);

       coef_set = &pdata->eq_cfg[sel];

       m98088_eq_band(codec, 1, 0, coef_set->band1);
       m98088_eq_band(codec, 1, 1, coef_set->band2);
       m98088_eq_band(codec, 1, 2, coef_set->band3);
       m98088_eq_band(codec, 1, 3, coef_set->band4);
       m98088_eq_band(codec, 1, 4, coef_set->band5);

       /* Restore the original on/off state */
       snd_soc_update_bits(codec, M98088_REG_49_CFG_LEVEL, M98088_EQ2EN,
               save);
}

static int max98088_put_eq_enum(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
       struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_pdata *pdata = max98088->pdata;
       int channel = max98088_get_channel(codec, kcontrol->id.name);
       struct max98088_cdata *cdata;
       int sel = ucontrol->value.integer.value[0];

       if (channel < 0)
	       return channel;

       cdata = &max98088->dai[channel];

       if (sel >= pdata->eq_cfgcnt)
               return -EINVAL;

       cdata->eq_sel = sel;

       switch (channel) {
       case 0:
               max98088_setup_eq1(codec);
               break;
       case 1:
               max98088_setup_eq2(codec);
               break;
       }

       return 0;
}

static int max98088_get_eq_enum(struct snd_kcontrol *kcontrol,
                                struct snd_ctl_elem_value *ucontrol)
{
       struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       int channel = max98088_get_channel(codec, kcontrol->id.name);
       struct max98088_cdata *cdata;

       if (channel < 0)
	       return channel;

       cdata = &max98088->dai[channel];
       ucontrol->value.enumerated.item[0] = cdata->eq_sel;
       return 0;
}

static void max98088_handle_eq_pdata(struct snd_soc_codec *codec)
{
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_pdata *pdata = max98088->pdata;
       struct max98088_eq_cfg *cfg;
       unsigned int cfgcnt;
       int i, j;
       const char **t;
       int ret;
       struct snd_kcontrol_new controls[] = {
               SOC_ENUM_EXT((char *)eq_mode_name[0],
                       max98088->eq_enum,
                       max98088_get_eq_enum,
                       max98088_put_eq_enum),
               SOC_ENUM_EXT((char *)eq_mode_name[1],
                       max98088->eq_enum,
                       max98088_get_eq_enum,
                       max98088_put_eq_enum),
       };
       BUILD_BUG_ON(ARRAY_SIZE(controls) != ARRAY_SIZE(eq_mode_name));

       cfg = pdata->eq_cfg;
       cfgcnt = pdata->eq_cfgcnt;

       /* Setup an array of texts for the equalizer enum.
        * This is based on Mark Brown's equalizer driver code.
        */
       max98088->eq_textcnt = 0;
       max98088->eq_texts = NULL;
       for (i = 0; i < cfgcnt; i++) {
               for (j = 0; j < max98088->eq_textcnt; j++) {
                       if (strcmp(cfg[i].name, max98088->eq_texts[j]) == 0)
                               break;
               }

               if (j != max98088->eq_textcnt)
                       continue;

               /* Expand the array */
               t = krealloc(max98088->eq_texts,
                            sizeof(char *) * (max98088->eq_textcnt + 1),
                            GFP_KERNEL);
               if (t == NULL)
                       continue;

               /* Store the new entry */
               t[max98088->eq_textcnt] = cfg[i].name;
               max98088->eq_textcnt++;
               max98088->eq_texts = t;
       }

       /* Now point the soc_enum to .texts array items */
       max98088->eq_enum.texts = max98088->eq_texts;
       max98088->eq_enum.items = max98088->eq_textcnt;

       ret = snd_soc_add_codec_controls(codec, controls, ARRAY_SIZE(controls));
       if (ret != 0)
               dev_err(codec->dev, "Failed to add EQ control: %d\n", ret);
}

static void max98088_handle_pdata(struct snd_soc_codec *codec)
{
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_pdata *pdata = max98088->pdata;
       u8 regval = 0;

       if (!pdata) {
               dev_dbg(codec->dev, "No platform data\n");
               return;
       }

       /* Configure mic for analog/digital mic mode */
       if (pdata->digmic_left_mode)
               regval |= M98088_DIGMIC_L;

       if (pdata->digmic_right_mode)
               regval |= M98088_DIGMIC_R;

       max98088->digmic = (regval ? 1 : 0);

       snd_soc_write(codec, M98088_REG_48_CFG_MIC, regval);

       /* Configure receiver output */
       regval = ((pdata->receiver_mode) ? M98088_REC_LINEMODE : 0);
       snd_soc_update_bits(codec, M98088_REG_2A_MIC_REC_CNTL,
               M98088_REC_LINEMODE_MASK, regval);

       /* Configure equalizers */
       if (pdata->eq_cfgcnt)
               max98088_handle_eq_pdata(codec);
}

static int max98088_probe(struct snd_soc_codec *codec)
{
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);
       struct max98088_cdata *cdata;
       int ret = 0;

       regcache_mark_dirty(max98088->regmap);

       /* initialize private data */

       max98088->sysclk = (unsigned)-1;
       max98088->eq_textcnt = 0;

       cdata = &max98088->dai[0];
       cdata->rate = (unsigned)-1;
       cdata->fmt  = (unsigned)-1;
       cdata->eq_sel = 0;

       cdata = &max98088->dai[1];
       cdata->rate = (unsigned)-1;
       cdata->fmt  = (unsigned)-1;
       cdata->eq_sel = 0;

       max98088->ina_state = 0;
       max98088->inb_state = 0;
       max98088->ex_mode = 0;
       max98088->digmic = 0;
       max98088->mic1pre = 0;
       max98088->mic2pre = 0;

       ret = snd_soc_read(codec, M98088_REG_FF_REV_ID);
       if (ret < 0) {
               dev_err(codec->dev, "Failed to read device revision: %d\n",
                       ret);
               goto err_access;
       }
       dev_info(codec->dev, "revision %c\n", ret - 0x40 + 'A');

       snd_soc_write(codec, M98088_REG_51_PWR_SYS, M98088_PWRSV);

       snd_soc_write(codec, M98088_REG_0F_IRQ_ENABLE, 0x00);

       snd_soc_write(codec, M98088_REG_22_MIX_DAC,
               M98088_DAI1L_TO_DACL|M98088_DAI2L_TO_DACL|
               M98088_DAI1R_TO_DACR|M98088_DAI2R_TO_DACR);

       snd_soc_write(codec, M98088_REG_4E_BIAS_CNTL, 0xF0);
       snd_soc_write(codec, M98088_REG_50_DAC_BIAS2, 0x0F);

       snd_soc_write(codec, M98088_REG_16_DAI1_IOCFG,
               M98088_S1NORMAL|M98088_SDATA);

       snd_soc_write(codec, M98088_REG_1E_DAI2_IOCFG,
               M98088_S2NORMAL|M98088_SDATA);

       max98088_handle_pdata(codec);

err_access:
       return ret;
}

static int max98088_remove(struct snd_soc_codec *codec)
{
       struct max98088_priv *max98088 = snd_soc_codec_get_drvdata(codec);

       kfree(max98088->eq_texts);

       return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_max98088 = {
	.probe   = max98088_probe,
	.remove  = max98088_remove,
	.set_bias_level = max98088_set_bias_level,
	.suspend_bias_off = true,

	.controls = max98088_snd_controls,
	.num_controls = ARRAY_SIZE(max98088_snd_controls),
	.dapm_widgets = max98088_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max98088_dapm_widgets),
	.dapm_routes = max98088_audio_map,
	.num_dapm_routes = ARRAY_SIZE(max98088_audio_map),
};

static int max98088_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
       struct max98088_priv *max98088;
       int ret;

       max98088 = devm_kzalloc(&i2c->dev, sizeof(struct max98088_priv),
			       GFP_KERNEL);
       if (max98088 == NULL)
               return -ENOMEM;

       max98088->regmap = devm_regmap_init_i2c(i2c, &max98088_regmap);
       if (IS_ERR(max98088->regmap))
	       return PTR_ERR(max98088->regmap);

       max98088->devtype = id->driver_data;

       i2c_set_clientdata(i2c, max98088);
       max98088->pdata = i2c->dev.platform_data;

       ret = snd_soc_register_codec(&i2c->dev,
                       &soc_codec_dev_max98088, &max98088_dai[0], 2);
       return ret;
}

static int max98088_i2c_remove(struct i2c_client *client)
{
       snd_soc_unregister_codec(&client->dev);
       return 0;
}

static const struct i2c_device_id max98088_i2c_id[] = {
       { "max98088", MAX98088 },
       { "max98089", MAX98089 },
       { }
};
MODULE_DEVICE_TABLE(i2c, max98088_i2c_id);

static struct i2c_driver max98088_i2c_driver = {
	.driver = {
		.name = "max98088",
	},
	.probe  = max98088_i2c_probe,
	.remove = max98088_i2c_remove,
	.id_table = max98088_i2c_id,
};

module_i2c_driver(max98088_i2c_driver);

MODULE_DESCRIPTION("ALSA SoC MAX98088 driver");
MODULE_AUTHOR("Peter Hsiang, Jesse Marroquin");
MODULE_LICENSE("GPL");
