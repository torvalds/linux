/*
 * rt3261.c  --  RT3261 ALSA SoC DSP driver
 *
 * Copyright 2011 Realtek Semiconductor Corp.
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#define RTK_IOCTL
#ifdef RTK_IOCTL
#include <linux/spi/spi.h>
#include "rt_codec_ioctl.h"
#endif

#include "rt3261.h"
#include "rt3261-dsp.h"

#define INIT_DSP_IN_PROBE

static const u16 rt3261_dsp_init[][2] = {
	{0x3fd2, 0x0038}, {0x229C, 0x0fa0}, {0x22d2, 0x8400}, {0x22ee, 0x0000},
	{0x22f2, 0x0040}, {0x22f5, 0x8000}, {0x22f6, 0x0000}, {0x22f9, 0x007f},
	{0x2310, 0x0880},
};
#define RT3261_DSP_INIT_NUM \
	(sizeof(rt3261_dsp_init) / sizeof(rt3261_dsp_init[0]))

//static const u16 rt3261_dsp_48[][2] = {
unsigned short rt3261_dsp_48[][2] = {
	{0x22c8, 0x0026}, {0x22fe, 0x0fa0}, {0x22ff, 0x3893}, {0x22fa, 0x2487},
	{0x2301, 0x0002},
};
#define RT3261_DSP_48_NUM (sizeof(rt3261_dsp_48) / sizeof(rt3261_dsp_48[0]))

//static const u16 rt3261_dsp_441[][2] = {
unsigned short rt3261_dsp_441[][2] = {
	{0x22c6, 0x0031}, {0x22c7, 0x0050}, {0x22c8, 0x0009}, {0x22fe, 0x0e5b},
	{0x22ff, 0x3883}, {0x22fa, 0x2484}, {0x2301, 0x0001},
};
#define RT3261_DSP_441_NUM (sizeof(rt3261_dsp_441) / sizeof(rt3261_dsp_441[0]))

//static const u16 rt3261_dsp_16[][2] = {
unsigned short rt3261_dsp_16[][2] = {
	{0x22c8, 0x0026}, {0x22fa, 0x2484}, {0x2301, 0x0002},
};
#define RT3261_DSP_16_NUM (sizeof(rt3261_dsp_16) / sizeof(rt3261_dsp_16[0]))

static const u16 rt3261_dsp_rate_tab[] = {
	0x22c6, 0x22c7, 0x22c8, 0x22fe, 0x22ff, 0x22fa, 0x2301,
};
#define RT3261_DSP_RATE_NUM (sizeof(rt3261_dsp_rate_tab) / sizeof(rt3261_dsp_rate_tab[0]))

static const u16 rt3261_dsp_aec_ns_fens[][2] = {
	{0x22F8, 0x8005},
	{0x2309, 0x0400},
	{0x2310, 0x0824},
	{0x2332, 0x0080},
	{0x2348, 0x1000},
	{0x2362, 0x0180},
	{0x236E, 0x1800},
	{0x2373, 0x3000},
	{0x2381, 0x4000},
	{0x2398, 0x0020},
	{0x23B4, 0x0012},
	{0x23BD, 0x0100},
	{0x23D1, 0xFF80},
	{0x23D5, 0x7FFF},
	{0x2303, 0x0931},
	{0x2333, 0x0008},
	{0x2349, 0x6800},
	{0x2363, 0x0100},
	{0x236F, 0x0A0A},
	{0x2374, 0x2400},
	{0x2382, 0x0400},
	{0x238C, 0x0400},
	{0x23A5, 0x0006},
	{0x23B7, 0x0008},
	{0x23BE, 0x2400},
	{0x23D2, 0xFF80},
	{0x23ED, 0x0300},
	{0x2304, 0xC31F},
	{0x230C, 0x0900},
	{0x2337, 0x0002},
	{0x2360, 0x0080},
	{0x2364, 0x0078},
	{0x2370, 0x0F00},
	{0x2375, 0x1800},
	{0x2383, 0x0400},
	{0x2396, 0x2000},
	{0x23A6, 0x7FFF},
	{0x23BB, 0x1000},
	{0x23CF, 0x7FFF},
	{0x2305, 0x0005},
	{0x232F, 0x0080},
	{0x2339, 0x0010},
	{0x2361, 0x1800},
	{0x2380, 0x7FFF},
	{0x2384, 0x0005},
	{0x23B3, 0x0018},
	{0x23BC, 0x0130},
	{0x2328, 0x7FFF},
	{0x233A, 0x7FFF},
	{0x233B, 0x7FFF},
	{0x233C, 0x7FFF},
	{0x2302, 0x0101},
	{0x22F2, 0x0040},
	{0x230A, 0x1B00},
	{0x22FB, 0x0000},
};

#define RT3261_DSP_AEC_NUM \
	(sizeof(rt3261_dsp_aec_ns_fens) / sizeof(rt3261_dsp_aec_ns_fens[0]))

static const u16 rt3261_dsp_hfbf[][2] = {
	{0x22f8, 0x8004}, {0x22a0, 0x1205}, {0x22a1, 0x0f00}, {0x22a2, 0x1000},
	{0x22a3, 0x1000}, {0x22a4, 0x1000}, {0x22aa, 0x0006}, {0x22ad, 0x0060},
	{0x22ae, 0x0080}, {0x22af, 0x0000}, {0x22b0, 0x000e}, {0x22b1, 0x0010},
	{0x22b2, 0x0006}, {0x22b3, 0x0001}, {0x22b4, 0x0010}, {0x22b5, 0x0001},
	{0x22b7, 0x0005}, {0x22d8, 0x0017}, {0x22f9, 0x007f}, {0x2303, 0x0971},
	{0x2304, 0x0302}, {0x2303, 0x0971}, {0x2304, 0x4302}, {0x2305, 0x102d},
	{0x2309, 0x0400}, {0x230c, 0x0400}, {0x230d, 0x0200}, {0x232f, 0x0020},
	{0x2332, 0x0100}, {0x2333, 0x0020}, {0x2337, 0xffff}, {0x2339, 0x0010},
	{0x2348, 0x1000}, {0x2349, 0x1000}, {0x236e, 0x1800}, {0x236f, 0x1006},
	{0x2370, 0x1000}, {0x2372, 0x0200}, {0x237b, 0x001e}, {0x2380, 0x7fff},
	{0x2381, 0x4000}, {0x2382, 0x0080}, {0x2383, 0x0200}, {0x2386, 0x7f80},
	{0x2387, 0x0040}, {0x238a, 0x0280}, {0x238c, 0x6000}, {0x238e, 0x5000},
	{0x2396, 0x6a00}, {0x2397, 0x6000}, {0x2398, 0x00e0}, {0x23a5, 0x0005},
	{0x23b3, 0x000f}, {0x23b4, 0x0003}, {0x23bb, 0x2000}, {0x23bc, 0x00d0},
	{0x23bd, 0x0140}, {0x23be, 0x1000}, {0x23cf, 0x0800}, {0x23d0, 0x0400},
	{0x23d1, 0x0100}, {0x23d2, 0x0100}, {0x23d5, 0x7c00}, {0x23ed, 0x0300},
	{0x23ee, 0x3000}, {0x23ef, 0x2800}, {0x22fb, 0x0000},
};
#define RT3261_DSP_HFBF_NUM \
	(sizeof(rt3261_dsp_hfbf) / sizeof(rt3261_dsp_hfbf[0]))

static const u16 rt3261_dsp_ffp[][2] = {
	{0x22f8, 0x8005}, {0x2303, 0x1971}, {0x2304, 0x8312}, {0x2305, 0x0005},
	{0x2309, 0x0200}, {0x230a, 0x1b00}, {0x230c, 0x0800}, {0x230d, 0x0400},
	{0x2325, 0x5000}, {0x2326, 0x0040}, {0x232f, 0x0080}, {0x2332, 0x0100},
	{0x2333, 0x0020}, {0x2337, 0x0001}, {0x2339, 0x0010}, {0x233c, 0x0040},
	{0x2348, 0x1000}, {0x2349, 0x1000}, {0x2360, 0x0180}, {0x2361, 0x1800},
	{0x2362, 0x0200}, {0x2363, 0x0200}, {0x2364, 0x0200}, {0x2365, 0x2000},
	{0x236e, 0x1000}, {0x236f, 0x0a05}, {0x2370, 0x0f00}, {0x2372, 0x1a00},
	{0x2373, 0x3000}, {0x2374, 0x2400}, {0x2375, 0x1800}, {0x2380, 0x7fff},
	{0x2381, 0x4000}, {0x2382, 0x0400}, {0x2383, 0x0400}, {0x2384, 0x0005},
	{0x2385, 0x0005}, {0x238e, 0x7000}, {0x2393, 0x4444}, {0x2394, 0x4444},
	{0x2395, 0x4444}, {0x2396, 0x2000}, {0x2397, 0x3000}, {0x2398, 0x0020},
	{0x23a5, 0x0006}, {0x23a6, 0x7fff}, {0x23b3, 0x000a}, {0x23b4, 0x0006},
	{0x23b7, 0x0008}, {0x23bb, 0x1000}, {0x23bc, 0x0130}, {0x23bd, 0x0160},
	{0x23be, 0x2400}, {0x23cf, 0x0800}, {0x23d0, 0x0400}, {0x23d1, 0xff80},
	{0x23d2, 0xff80}, {0x23d3, 0x2000}, {0x23d4, 0x5000}, {0x23d5, 0x5000},
	{0x23e7, 0x0c00}, {0x23e8, 0x1400}, {0x23e9, 0x6000}, {0x23ea, 0x7f00},
	{0x23ed, 0x0300}, {0x23ee, 0x2800}, {0x22fb, 0x0000},
};
#define RT3261_DSP_FFP_NUM (sizeof(rt3261_dsp_ffp) / sizeof(rt3261_dsp_ffp[0]))

static const u16 rt3261_dsp_p3_tab[][3] = {
	{0x4af0, 0x1000, 0x822b}, {0x90f0, 0x1001, 0x8393},
	{0x64f0, 0x1002, 0x822b}, {0x0ff0, 0x1003, 0x26e0},
	{0x55f0, 0x1004, 0x2200}, {0xcff0, 0x1005, 0x1a7b},
	{0x5af0, 0x1006, 0x823a}, {0x90f0, 0x1007, 0x8393},
	{0x64f0, 0x1008, 0x822b}, {0x0ff0, 0x1009, 0x26e0},
	{0x03f0, 0x100a, 0x2218}, {0x0ef0, 0x100b, 0x3400},
	{0x4ff0, 0x100c, 0x195e}, {0x00f0, 0x100d, 0x0000},
	{0xf0f0, 0x100e, 0x8143}, {0x1ff0, 0x100f, 0x2788},
	{0x0ef0, 0x1010, 0x3400}, {0xe0f0, 0x1011, 0x1a26},
	{0x2cf0, 0x1012, 0x8001}, {0x0ff0, 0x1013, 0x267c},
	{0x82f0, 0x1014, 0x1a27}, {0x3cf0, 0x1015, 0x8001},
	{0x0ff0, 0x1016, 0x267c}, {0x82f0, 0x1017, 0x1a27},
	{0xeff0, 0x1018, 0x1a26}, {0x01f0, 0x1019, 0x4ff0},
	{0x5cf0, 0x101a, 0x2b81}, {0xfaf0, 0x101b, 0x2a6a},
	{0x05f0, 0x101c, 0x4011}, {0x0ff0, 0x101d, 0x278e},
	{0x0ef0, 0x101e, 0x3400}, {0xe1f0, 0x101f, 0x1997},
	{0x1ff0, 0x1020, 0x1997}, {0x03f0, 0x1021, 0x2279},
	{0xb8f0, 0x1022, 0x8206}, {0xf8f0, 0x1023, 0x0f00},
	{0xfff0, 0x1024, 0x279e}, {0x0ff0, 0x1025, 0x2272},
	{0x0ef0, 0x1026, 0x3400}, {0x3ff0, 0x1027, 0x199a},
	{0x0ff0, 0x1028, 0x2262}, {0x0ff0, 0x1029, 0x2272},
	{0x0ef0, 0x102a, 0x3400}, {0xfff0, 0x102b, 0x199a},
	{0x7ff0, 0x102c, 0x22e2}, {0x0ef0, 0x102d, 0x3400},
	{0xfff0, 0x102e, 0x19cb}, {0xfff0, 0x102f, 0x47ff},
	{0xb1f0, 0x1030, 0x80b1}, {0x5ff0, 0x1031, 0x2261},
	{0x62f0, 0x1032, 0x1903}, {0x9af0, 0x1033, 0x0d00},
	{0xcff0, 0x1034, 0x80b1}, {0x0ff0, 0x1035, 0x0e27},
	{0x8ff0, 0x1036, 0x9229}, {0x0ef0, 0x1037, 0x3400},
	{0xaff0, 0x1038, 0x19f5}, {0x81f0, 0x1039, 0x8229},
	{0x0ef0, 0x103a, 0x3400}, {0xfff0, 0x103b, 0x19f6},
	{0x5af0, 0x103c, 0x8234}, {0xeaf0, 0x103d, 0x9113},
	{0x0ef0, 0x103e, 0x3400}, {0x7ff0, 0x103f, 0x19ea},
	{0x8af0, 0x1040, 0x924d}, {0x08f0, 0x1041, 0x3400},
	{0x3ff0, 0x1042, 0x1a74}, {0x00f0, 0x1043, 0x0000},
	{0x00f0, 0x1044, 0x0000}, {0x00f0, 0x1045, 0x0c38},
	{0x0ff0, 0x1046, 0x2618}, {0xb0f0, 0x1047, 0x8148},
	{0x01f0, 0x1048, 0x3700}, {0x02f0, 0x1049, 0x3a70},
	{0x03f0, 0x104a, 0x3a78}, {0x9af0, 0x104b, 0x8229},
	{0xd6f0, 0x104c, 0x47c4}, {0x95f0, 0x104d, 0x4361},
	{0x0ff0, 0x104e, 0x2082}, {0x76f0, 0x104f, 0x626b},
	{0x0ff0, 0x1050, 0x208a}, {0x0ff0, 0x1051, 0x204a},
	{0xc9f0, 0x1052, 0x7882}, {0x75f0, 0x1053, 0x626b},
	{0x0ff0, 0x1054, 0x208a}, {0x0ff0, 0x1055, 0x204a},
	{0xcdf0, 0x1056, 0x7882}, {0x0ff0, 0x1057, 0x2630},
	{0x8af0, 0x1058, 0x2b30}, {0xf4f0, 0x1059, 0x1904},
	{0x98f0, 0x105a, 0x9229}, {0x0ef0, 0x105b, 0x3400},
	{0xeff0, 0x105c, 0x19fd}, {0xd7f0, 0x105d, 0x40cc},
	{0x0ef0, 0x105e, 0x3400}, {0xdff0, 0x105f, 0x1a44},
	{0x00f0, 0x1060, 0x0000}, {0xcef0, 0x1061, 0x1507},
	{0x90f0, 0x1062, 0x1020}, {0x5ff0, 0x1063, 0x1006},
	{0x89f0, 0x1064, 0x608f}, {0x0ff0, 0x1065, 0x0e64},
	{0x49f0, 0x1066, 0x1044}, {0xcff0, 0x1067, 0x2b28},
	{0x93f0, 0x1068, 0x2a62}, {0x5ff0, 0x1069, 0x266a},
	{0x54f0, 0x106a, 0x22a8}, {0x0af0, 0x106b, 0x0f22},
	{0xfbf0, 0x106c, 0x0f0c}, {0x5ff0, 0x106d, 0x0d00},
	{0x90f0, 0x106e, 0x1020}, {0x4ff0, 0x106f, 0x1006},
	{0x8df0, 0x1070, 0x6087}, {0x0ff0, 0x1071, 0x0e64},
	{0xb9f0, 0x1072, 0x1044}, {0xcff0, 0x1073, 0x2a63},
	{0x5ff0, 0x1074, 0x266a}, {0x54f0, 0x1075, 0x22a8},
	{0x0af0, 0x1076, 0x0f22}, {0xfbf0, 0x1077, 0x0f0c},
	{0x93f0, 0x1078, 0x2aef}, {0x0ff0, 0x1079, 0x227a},
	{0xc2f0, 0x107a, 0x1907}, {0xf5f0, 0x107b, 0x0d00},
	{0xfdf0, 0x107c, 0x7800}, {0x0ef0, 0x107d, 0x3400},
	{0xaff0, 0x107e, 0x1899},
};
#define RT3261_DSP_PATCH3_NUM \
	(sizeof(rt3261_dsp_p3_tab) / sizeof(rt3261_dsp_p3_tab[0]))

static const u16 rt3261_dsp_p2_tab[][2] = {
	{0x3fa1, 0xe7bb}, {0x3fb1, 0x5000}, {0x3fa2, 0xa26b}, {0x3fb2, 0x500e},
	{0x3fa3, 0xa27c}, {0x3fb3, 0x2282}, {0x3fa4, 0x996e}, {0x3fb4, 0x5019},
	{0x3fa5, 0x99a2}, {0x3fb5, 0x5021}, {0x3fa6, 0x99ae}, {0x3fb6, 0x5028},
	{0x3fa7, 0x9cbb}, {0x3fb7, 0x502c}, {0x3fa8, 0x9900}, {0x3fb8, 0x1903},
	{0x3fa9, 0x9f59}, {0x3fb9, 0x502f}, {0x3faa, 0x9f6e}, {0x3fba, 0x5039},
	{0x3fab, 0x9ea2}, {0x3fbb, 0x503c}, {0x3fac, 0x9fc8}, {0x3fbc, 0x5045},
	{0x3fad, 0xa44c}, {0x3fbd, 0x505d}, {0x3fae, 0x8983}, {0x3fbe, 0x5061},
	{0x3faf, 0x95e3}, {0x3fbf, 0x5006}, {0x3fa0, 0xe742}, {0x3fb0, 0x5040},
};
#define RT3261_DSP_PATCH2_NUM \
	(sizeof(rt3261_dsp_p2_tab) / sizeof(rt3261_dsp_p2_tab[0]))

/**
 * rt3261_dsp_done - Wait until DSP is ready.
 * @codec: SoC Audio Codec device.
 *
 * To check voice DSP status and confirm it's ready for next work.
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_dsp_done(struct snd_soc_codec *codec)
{
	unsigned int count = 0, dsp_val;

	dsp_val = snd_soc_read(codec, RT3261_DSP_CTRL3);
	while(dsp_val & RT3261_DSP_BUSY_MASK) {
		if(count > 10)
			return -EBUSY;
		dsp_val = snd_soc_read(codec, RT3261_DSP_CTRL3);
		count ++;
	}

	return 0;
}


/**
 * rt3261_dsp_write - Write DSP register.
 * @codec: SoC audio codec device.
 * @param: DSP parameters.
  *
 * Modify voice DSP register for sound effect. The DSP can be controlled
 * through DSP command format (0xfc), addr (0xc4), data (0xc5) and cmd (0xc6)
 * register. It has to wait until the DSP is ready.
 *
 * Returns 0 for success or negative error code.
 */
int rt3261_dsp_write(struct snd_soc_codec *codec,
		struct rt3261_dsp_param *param)
{
	unsigned int dsp_val = snd_soc_read(codec, RT3261_DSP_CTRL3);
	int ret;

	ret = rt3261_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_GEN_CTRL3, param->cmd_fmt);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write cmd format: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_DSP_CTRL1, param->addr);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_DSP_CTRL2, param->data);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP data reg: %d\n", ret);
		goto err;
	}
	dsp_val &= ~(RT3261_DSP_R_EN | RT3261_DSP_CMD_MASK);
	dsp_val |= RT3261_DSP_W_EN | param->cmd;
	ret = snd_soc_write(codec, RT3261_DSP_CTRL3, dsp_val);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}
	//mdelay(10);
	return 0;

err:
	return ret;
}

EXPORT_SYMBOL_GPL(rt3261_dsp_write);

/**
 * rt3261_dsp_read - Read DSP register.
 * @codec: SoC audio codec device.
 * @reg: DSP register index.
 *
 * Read DSP setting value from voice DSP. The DSP can be controlled
 * through DSP addr (0xc4), data (0xc5) and cmd (0xc6) register. Each
 * command has to wait until the DSP is ready.
 *
 * Returns DSP register value or negative error code.
 */
unsigned int rt3261_dsp_read(
	struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int val_h, val_l, value;
	unsigned int dsp_val = snd_soc_read(codec, RT3261_DSP_CTRL3);
	int ret = 0;

	ret = rt3261_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_GEN_CTRL3, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write fc = 0: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_DSP_CTRL1, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	dsp_val &= ~(RT3261_DSP_W_EN | RT3261_DSP_CMD_MASK);
	dsp_val |= RT3261_DSP_R_EN | RT3261_DSP_CMD_MR;
	ret = snd_soc_write(codec, RT3261_DSP_CTRL3, dsp_val);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}

	/* Read DSP high byte data */
	ret = rt3261_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_DSP_CTRL1, RT3261_DSP_REG_DATHI);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	dsp_val &= ~(RT3261_DSP_W_EN | RT3261_DSP_CMD_MASK);
	dsp_val |= RT3261_DSP_R_EN | RT3261_DSP_CMD_RR;
	ret = snd_soc_write(codec, RT3261_DSP_CTRL3, dsp_val);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}
	ret = rt3261_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_read(codec, RT3261_DSP_CTRL2);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read DSP data reg: %d\n", ret);
		goto err;
	}
	val_h = ret;

	/* Read DSP low byte data */
	ret = snd_soc_write(codec, RT3261_DSP_CTRL1, RT3261_DSP_REG_DATLO);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP addr reg: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT3261_DSP_CTRL3, dsp_val);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to write DSP cmd reg: %d\n", ret);
		goto err;
	}
	ret = rt3261_dsp_done(codec);
	if (ret < 0) {
		dev_err(codec->dev, "DSP is busy: %d\n", ret);
		goto err;
	}
	ret = snd_soc_read(codec, RT3261_DSP_CTRL2);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read DSP data reg: %d\n", ret);
		goto err;
	}
	val_l = ret;

	value = ((val_h & 0xff) << 8) |(val_l & 0xff);
	return value;

err:
	return ret;
}
EXPORT_SYMBOL_GPL(rt3261_dsp_read);

static int rt3261_dsp_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = rt3261->dsp_sw;

	return 0;
}

static int rt3261_dsp_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	if (rt3261->dsp_sw != ucontrol->value.integer.value[0])
		rt3261->dsp_sw = ucontrol->value.integer.value[0];

	return 0;
}

/* DSP Path Control 1 */
static const char *rt3261_src_rxdp_mode[] = {
	"Normal", "Divided by 3"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_src_rxdp_enum, RT3261_DSP_PATH1,
	RT3261_RXDP_SRC_SFT, rt3261_src_rxdp_mode);

static const char *rt3261_src_txdp_mode[] = {
	"Normal", "Multiplied by 3"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_src_txdp_enum, RT3261_DSP_PATH1,
	RT3261_TXDP_SRC_SFT, rt3261_src_txdp_mode);

/* DSP data select */
static const char *rt3261_dsp_data_select[] = {
	"Normal", "left copy to right", "right copy to left", "Swap"};

static const SOC_ENUM_SINGLE_DECL(rt3261_rxdc_data_enum, RT3261_DSP_PATH2,
				RT3261_RXDC_SEL_SFT, rt3261_dsp_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_rxdp_data_enum, RT3261_DSP_PATH2,
				RT3261_RXDP_SEL_SFT, rt3261_dsp_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_txdc_data_enum, RT3261_DSP_PATH2,
				RT3261_TXDC_SEL_SFT, rt3261_dsp_data_select);

static const SOC_ENUM_SINGLE_DECL(rt3261_txdp_data_enum, RT3261_DSP_PATH2,
				RT3261_TXDP_SEL_SFT, rt3261_dsp_data_select);

/* Sound Effect */
static const char *rt3261_dsp_mode[] = {
	"Disable", "AEC+NS+FENS", "HFBF", "Far Field Pick-up"};

static const SOC_ENUM_SINGLE_DECL(rt3261_dsp_enum, 0, 0, rt3261_dsp_mode);

static const char *rt3261_rxdp2_src[] =
	{"IF2_DAC", "Stereo_ADC"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_rxdp2_enum, RT3261_GEN_CTRL2,
	RT3261_RXDP2_SEL_SFT, rt3261_rxdp2_src);

static const struct snd_kcontrol_new rt3261_rxdp2_mux =
	SOC_DAPM_ENUM("RxDP2 sel", rt3261_rxdp2_enum);

static const char *rt3261_rxdp_src[] =
	{"RxDP2", "RxDP1"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_rxdp_enum, RT3261_DUMMY_PR3F,
	10, rt3261_rxdp_src);

static const struct snd_kcontrol_new rt3261_rxdp_mux =
	SOC_DAPM_ENUM("RxDP sel", rt3261_rxdp_enum);

static const char *rt3261_rxdc_src[] =
	{"Mono_ADC", "Stereo_ADC"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_rxdc_enum, RT3261_GEN_CTRL2,
	RT3261_RXDC_SRC_SFT, rt3261_rxdc_src);

static const struct snd_kcontrol_new rt3261_rxdc_mux =
	SOC_DAPM_ENUM("RxDC sel", rt3261_rxdc_enum);

static const char *rt3261_rxdp1_src[] =
	{"DAC1", "IF1_DAC"};

static const SOC_ENUM_SINGLE_DECL(
	rt3261_rxdp1_enum, RT3261_DUMMY_PR3F,
	9, rt3261_rxdp1_src);

static const struct snd_kcontrol_new rt3261_rxdp1_mux =
	SOC_DAPM_ENUM("RxDP1 sel", rt3261_rxdp1_enum);
	
static const struct snd_kcontrol_new rt3261_dsp_snd_controls[] = {
	SOC_ENUM("RxDC input data", rt3261_rxdc_data_enum),
	SOC_ENUM("RxDP input data", rt3261_rxdp_data_enum),
	SOC_ENUM("TxDC input data", rt3261_txdc_data_enum),
	SOC_ENUM("TxDP input data", rt3261_txdp_data_enum),
	SOC_ENUM("SRC for RxDP", rt3261_src_rxdp_enum),
	SOC_ENUM("SRC for TxDP", rt3261_src_txdp_enum),
	/* AEC */
	SOC_ENUM_EXT("DSP Function Switch", rt3261_dsp_enum,
		rt3261_dsp_get, rt3261_dsp_put),
};

/*static int rt3261_dsp_patch_3(struct snd_soc_codec *codec)
{
	struct rt3261_dsp_param param;
	int ret, i;

	param.cmd_fmt = 0x0090;
	param.addr = 0x0064;
	param.data = 0x0004;
	param.cmd = RT3261_DSP_CMD_RW;
	ret = rt3261_dsp_write(codec, &param);
	if (ret < 0) {
		dev_err(codec->dev,
			"Fail to set DSP 3 bytes patch entrance: %d\n", ret);
		goto patch_err;
	}

	param.cmd = RT3261_DSP_CMD_PE;
	for(i = 0; i < RT3261_DSP_PATCH3_NUM; i++) {
		param.cmd_fmt = rt3261_dsp_p3_tab[i][0];
		param.addr = rt3261_dsp_p3_tab[i][1];
		param.data = rt3261_dsp_p3_tab[i][2];
		ret = rt3261_dsp_write(codec, &param);
		if (ret < 0) {
			dev_err(codec->dev, "Fail to patch Dsp: %d\n", ret);
			goto patch_err;
		}
	}

	return 0;

patch_err:

	return ret;
}

static int rt3261_dsp_patch_2(struct snd_soc_codec *codec)
{
	struct rt3261_dsp_param param;
	int ret, i;

	param.cmd_fmt = 0x0090;
	param.addr = 0x0064;
	param.data = 0x0000;
	param.cmd = RT3261_DSP_CMD_RW;
	ret = rt3261_dsp_write(codec, &param);
	if (ret < 0) {
		dev_err(codec->dev,
			"Fail to set DSP 2 bytes patch entrance: %d\n", ret);
		goto patch_err;
	}

	param.cmd_fmt = 0x00e0;
	param.cmd = RT3261_DSP_CMD_MW;
	for(i = 0; i < RT3261_DSP_PATCH2_NUM; i++) {
		param.addr = rt3261_dsp_p2_tab[i][0];
		param.data = rt3261_dsp_p2_tab[i][1];
		ret = rt3261_dsp_write(codec, &param);
		if (ret < 0) {
			dev_err(codec->dev, "Fail to patch Dsp: %d\n", ret);
			goto patch_err;
		}
	}

	return 0;

patch_err:

	return ret;
}*/

/**
 * rt3261_dsp_patch - Write DSP patch code.
 *
 * @codec: SoC audio codec device.
 *
 * Write patch codes to DSP including 3 and 2 bytes data.
 *
 * Returns 0 for success or negative error code.
 */
/*static int rt3261_dsp_patch(struct snd_soc_codec *codec)
{
	int ret;

	dev_dbg(codec->dev, "\n DSP Patch Start ......\n");

	ret = snd_soc_update_bits(codec, RT3261_MICBIAS,
		RT3261_PWR_CLK25M_MASK, RT3261_PWR_CLK25M_PU);
	if (ret < 0)
		goto patch_err;

	ret = snd_soc_update_bits(codec, RT3261_GLB_CLK,
		RT3261_SCLK_SRC_MASK, RT3261_SCLK_SRC_RCCLK);
	if (ret < 0)
		goto patch_err;

	ret = snd_soc_update_bits(codec, RT3261_PWR_DIG2,
		RT3261_PWR_I2S_DSP, RT3261_PWR_I2S_DSP);
	if (ret < 0)
		goto patch_err;

	ret = snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_PD_PIN_MASK, RT3261_DSP_PD_PIN_HI);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to power up DSP: %d\n", ret);
		goto patch_err;
	}

	ret = snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_RST_PIN_MASK, RT3261_DSP_RST_PIN_LO);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset DSP: %d\n", ret);
		goto patch_err;
	}

	mdelay(10);

	ret = snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_RST_PIN_MASK, RT3261_DSP_RST_PIN_HI);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to recover DSP: %d\n", ret);
		goto patch_err;
	}

	ret = rt3261_dsp_patch_3(codec);
	if (ret < 0)
		goto patch_err;

	ret = rt3261_dsp_patch_2(codec);
	if (ret < 0)
		goto patch_err;

	return 0;

patch_err:

	return ret;
}

static void rt3261_do_dsp_patch(struct work_struct *work)
{
	struct rt3261_priv *rt3261 =
		container_of(work, struct rt3261_priv, patch_work.work);
	struct snd_soc_codec *codec = rt3261->codec;

	if (rt3261_dsp_patch(codec) < 0)
		dev_err(codec->dev, "Patch DSP rom code Fail !!!\n");
}*/


/**
 * rt3261_dsp_conf - Set DSP basic setting.
 *
 * @codec: SoC audio codec device.
 *
 * Set parameters of basic setting to DSP.
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_dsp_conf(struct snd_soc_codec *codec)
{
	struct rt3261_dsp_param param;
	int ret, i;

	ret = snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_PD_PIN_MASK, RT3261_DSP_PD_PIN_HI);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to power up DSP: %d\n", ret);
		goto conf_err;
	}

	ret = snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_RST_PIN_MASK, RT3261_DSP_RST_PIN_LO);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to reset DSP: %d\n", ret);
		goto conf_err;
	}

	mdelay(10);

	ret = snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_RST_PIN_MASK | RT3261_DSP_CLK_MASK,
		RT3261_DSP_RST_PIN_HI | RT3261_DSP_CLK_384K);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to recover DSP: %d\n", ret);
		goto conf_err;
	}

	param.cmd_fmt = 0x00e0;
	param.cmd = RT3261_DSP_CMD_MW;
	for(i = 0; i < RT3261_DSP_INIT_NUM; i++) {
		param.addr = rt3261_dsp_init[i][0];
		param.data = rt3261_dsp_init[i][1];
		ret = rt3261_dsp_write(codec, &param);
		if (ret < 0) {
			dev_err(codec->dev, "Fail to config Dsp: %d\n", ret);
			goto conf_err;
		}
	}

	return 0;

conf_err:

	return ret;
}

/**
 * rt3261_dsp_rate - Set DSP rate setting.
 *
 * @codec: SoC audio codec device.
 * @rate: Sampling rate.
 *
 * Set parameters of sampling rate to DSP.
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_dsp_rate(struct snd_soc_codec *codec, int rate)
{
	struct rt3261_dsp_param param;
	int ret, i, tab_num;
	unsigned short (*rate_tab)[2];

	if (rate != 48000 &&  rate != 44100 && rate != 16000)
		return -EINVAL;

	if (rate > 44100) {
		rate_tab = rt3261_dsp_48;
		tab_num = RT3261_DSP_48_NUM;
	} else {
		if (rate > 16000) {
			rate_tab = rt3261_dsp_441;
			tab_num = RT3261_DSP_441_NUM;
		} else {
			rate_tab = rt3261_dsp_16;
			tab_num = RT3261_DSP_16_NUM;
		}
	}

	param.cmd_fmt = 0x00e0;
	param.cmd = RT3261_DSP_CMD_MW;
	for (i = 0; i < tab_num; i++) {
		param.addr = rate_tab[i][0];
		param.data = rate_tab[i][1];
		ret = rt3261_dsp_write(codec, &param);
		if (ret < 0)
			goto rate_err;
	}

	return 0;

rate_err:

	dev_err(codec->dev, "Fail to set rate %d parameters: %d\n", rate, ret);
	return ret;
}

/**
 * rt3261_dsp_set_mode - Set DSP mode parameters.
 *
 * @codec: SoC audio codec device.
 * @mode: DSP mode.
 *
 * Set parameters of mode to DSP.
 * There are three modes which includes " mic AEC + NS + FENS",
 * "HFBF" and "Far-field pickup".
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_dsp_set_mode(struct snd_soc_codec *codec, int mode)
{
	struct rt3261_dsp_param param;
	int ret, i;
/*
	unsigned short (*mode_tab)[2];

	switch (mode) {
	case RT3261_DSP_AEC_NS_FENS:
		dev_info(codec->dev, "AEC\n");
		mode_tab = rt3261_dsp_aec_ns_fens;
		tab_num = RT3261_DSP_AEC_NUM;
		break;

	case RT3261_DSP_HFBF:
		dev_info(codec->dev, "Beamforming\n");
		mode_tab = rt3261_dsp_hfbf;
		tab_num = RT3261_DSP_HFBF_NUM;
		break;

	case RT3261_DSP_FFP:
		dev_info(codec->dev, "Far Field Pick-up\n");
		mode_tab = rt3261_dsp_ffp;
		tab_num = RT3261_DSP_FFP_NUM;
		break;

	case RT3261_DSP_DIS:
	default:
		dev_info(codec->dev, "Disable\n");
		return 0;
	}
*/
	param.cmd_fmt = 0x00e0;
	param.cmd = RT3261_DSP_CMD_MW;
	for (i = 0; i < RT3261_DSP_AEC_NUM; i++) {
		param.addr = rt3261_dsp_aec_ns_fens[i][0];
		param.data = rt3261_dsp_aec_ns_fens[i][1];
		ret = rt3261_dsp_write(codec, &param);
		if (ret < 0)
			goto mode_err;
	}

	return 0;

mode_err:

	dev_err(codec->dev, "Fail to set mode %d parameters: %d\n", mode, ret);
	return ret;
}

/**
 * rt3261_dsp_snd_effect - Set DSP sound effect.
 *
 * Set parameters of sound effect to DSP.
 *
 * Returns 0 for success or negative error code.
 */
static int rt3261_dsp_snd_effect(struct snd_soc_codec *codec)
{
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = rt3261_dsp_conf(codec);
	if (ret < 0)
		goto effect_err;

	ret = rt3261_dsp_rate(codec, rt3261->lrck[rt3261->aif_pu] ?
		rt3261->lrck[rt3261->aif_pu] : 44100);
	if (ret < 0)
		goto effect_err;

	ret = rt3261_dsp_set_mode(codec, rt3261->dsp_sw);
	if (ret < 0)
		goto effect_err;

	mdelay(20);

	return 0;

effect_err:

	return ret;
}

static int rt3261_dsp_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);
	static unsigned int power_on;

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		pr_info("%s(): PMD\n", __func__);
		if (!power_on)
			return 0;

		power_on--;
		if (!power_on) {
			snd_soc_update_bits(codec, RT3261_PWR_DIG2,
				RT3261_PWR_I2S_DSP, 0);
			snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
				RT3261_DSP_PD_PIN_MASK, RT3261_DSP_PD_PIN_LO);
		}
		break;

	case SND_SOC_DAPM_POST_PMU:
		pr_info("%s(): PMU\n", __func__);
		if (rt3261->dsp_sw == RT3261_DSP_DIS || 2 <= power_on)
			return 0;

		if (!power_on) {
			snd_soc_update_bits(codec, RT3261_PWR_DIG2,
				RT3261_PWR_I2S_DSP, RT3261_PWR_I2S_DSP);
#ifdef INIT_DSP_IN_PROBE
			snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
				RT3261_DSP_PD_PIN_MASK, RT3261_DSP_PD_PIN_HI);
#else
			rt3261_dsp_snd_effect(codec);
#endif
		}
		power_on++;
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt3261_dsp_dapm_widgets[] = {
	SND_SOC_DAPM_PGA_E("DSP Downstream", SND_SOC_NOPM,
		0, 0, NULL, 0, rt3261_dsp_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("DSP Upstream", SND_SOC_NOPM,
		0, 0, NULL, 0, rt3261_dsp_event,
		SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX("RxDP Mux", SND_SOC_NOPM, 0, 0,
		&rt3261_rxdp_mux),
	SND_SOC_DAPM_MUX("RxDP2 Mux", SND_SOC_NOPM, 0, 0,
		&rt3261_rxdp2_mux),
	SND_SOC_DAPM_MUX("RxDP1 Mux", SND_SOC_NOPM, 0, 0,
		&rt3261_rxdp1_mux),
	SND_SOC_DAPM_MUX("RxDC Mux", SND_SOC_NOPM, 0, 0,
		&rt3261_rxdc_mux),
	SND_SOC_DAPM_PGA("RxDP", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RxDC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TxDC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TxDP", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route rt3261_dsp_dapm_routes[] = {
	{"RxDC", NULL, "RxDC Mux"},
	{"RxDC Mux", "Mono_ADC", "Mono ADC MIXL"},
	{"RxDC Mux", "Mono_ADC", "Mono ADC MIXR"},
	{"RxDC Mux", "Stereo_ADC", "Stereo ADC MIXL"},
	{"RxDC Mux", "Stereo_ADC", "Stereo ADC MIXR"},
	{"RxDP", NULL, "RxDP Mux"},
	{"RxDP Mux", "RxDP2", "RxDP2 Mux"},
	{"RxDP Mux", "RxDP1", "RxDP1 Mux"},
	{"RxDP2 Mux", "IF2_DAC", "IF2 DAC L"},
	{"RxDP2 Mux", "IF2_DAC", "IF2 DAC R"},
	{"RxDP2 Mux", "Stereo_ADC", "Stereo ADC MIXL"},
	{"RxDP2 Mux", "Stereo_ADC", "Stereo ADC MIXR"},
	{"RxDP1 Mux", "DAC1", "Stereo DAC MIXL"},
	{"RxDP1 Mux", "DAC1", "Stereo DAC MIXR"},
	{"RxDP1 Mux", "IF1_DAC", "IF1 DAC L"},
	{"RxDP1 Mux", "IF1_DAC", "IF1 DAC R"},

	{"DSP Downstream", NULL, "RxDP"},
	{"TxDC", NULL, "DSP Downstream"},
	{"DSP Upstream", NULL, "RxDP"},
	{"DSP Upstream", NULL, "RxDC"},
	{"TxDP", NULL, "DSP Upstream"},

	{"IF2 ADC L Mux", "TxDP", "TxDP"},
	{"IF2 ADC R Mux", "TxDP", "TxDP"},
	{"DAC L2 Mux", "TxDC", "TxDC"},
	{"DAC R2 Mux", "TxDC", "TxDC"},
};

/**
 * rt3261_dsp_show - Dump DSP registers.
 * @dev: codec device.
 * @attr: device attribute.
 * @buf: buffer for display.
 *
 * To show non-zero values of all DSP registers.
 *
 * Returns buffer length.
 */
static ssize_t rt3261_dsp_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt3261_priv *rt3261 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt3261->codec;
	unsigned short (*rt3261_dsp_tab)[2];
	unsigned int val;
	int cnt = 0, i, tab_num;

	switch (rt3261->dsp_sw) {
	case RT3261_DSP_AEC_NS_FENS:
		cnt += sprintf(buf, "[ RT3261 DSP 'AEC' ]\n");
		rt3261_dsp_tab = rt3261_dsp_aec_ns_fens;
		tab_num = RT3261_DSP_AEC_NUM;
		break;

	case RT3261_DSP_HFBF:
		cnt += sprintf(buf, "[ RT3261 DSP 'Beamforming' ]\n");
		rt3261_dsp_tab = rt3261_dsp_hfbf;
		tab_num = RT3261_DSP_HFBF_NUM;
		break;

	case RT3261_DSP_FFP:
		cnt += sprintf(buf, "[ RT3261 DSP 'Far Field Pick-up' ]\n");
		rt3261_dsp_tab = rt3261_dsp_ffp;
		tab_num = RT3261_DSP_FFP_NUM;
		break;

	case RT3261_DSP_DIS:
	default:
		cnt += sprintf(buf, "RT3261 DSP Disabled\n");
		goto dsp_done;
	}

	for (i = 0; i < tab_num; i++) {
		if (cnt + RT3261_DSP_REG_DISP_LEN >= PAGE_SIZE)
			break;
		val = rt3261_dsp_read(codec, rt3261_dsp_tab[i][0]);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, RT3261_DSP_REG_DISP_LEN,
			"#rnv%04x  #rv%04x  #rd0\n\n", rt3261_dsp_tab[i][0], val);
	}

	tab_num = RT3261_DSP_INIT_NUM;
	for (i = 0; i < tab_num; i++) {
		if (cnt + RT3261_DSP_REG_DISP_LEN >= PAGE_SIZE)
			break;
		val = rt3261_dsp_read(codec, rt3261_dsp_init[i][0]);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, RT3261_DSP_REG_DISP_LEN,
			"#rnv%04x  #rv%04x  #rd0\n", rt3261_dsp_init[i][0], val);
	}
	for (i = 0; i < RT3261_DSP_RATE_NUM; i++) {
		if (cnt + RT3261_DSP_REG_DISP_LEN >= PAGE_SIZE)
			break;
		val = rt3261_dsp_read(codec, rt3261_dsp_rate_tab[i]);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, RT3261_DSP_REG_DISP_LEN,
			"#rnv%04x  #rv%04x  #rd0\n", rt3261_dsp_rate_tab[i], val);
	}

dsp_done:

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	return cnt;
}

static ssize_t dsp_reg_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt3261_priv *rt3261 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt3261->codec;
	struct rt3261_dsp_param param;
	unsigned int val=0,addr=0;
	int i;

	printk("register \"%s\" count=%d\n",buf,count);

	for(i=0;i<count;i++) //address
	{
		if(*(buf+i) <= '9' && *(buf+i)>='0')
		{
			addr = (addr << 4) | (*(buf+i)-'0');
		}
		else if(*(buf+i) <= 'f' && *(buf+i)>='a')
		{
			addr = (addr << 4) | ((*(buf+i)-'a')+0xa);
		}
		else if(*(buf+i) <= 'A' && *(buf+i)>='A')
		{
			addr = (addr << 4) | ((*(buf+i)-'A')+0xa);
		}
		else
		{
			break;
		}
	}
	 
	for(i=i+1 ;i<count;i++) //val
	{
		if(*(buf+i) <= '9' && *(buf+i)>='0')
		{
			val = (val << 4) | (*(buf+i)-'0');
		}
		else if(*(buf+i) <= 'f' && *(buf+i)>='a')
		{
			val = (val << 4) | ((*(buf+i)-'a')+0xa);
		}
		else if(*(buf+i) <= 'F' && *(buf+i)>='A')
		{
			val = (val << 4) | ((*(buf+i)-'A')+0xa);
			
		}
		else
		{
			break;
		}
	}
	printk("addr=0x%x val=0x%x\n",addr,val);
	if(i==count)
	{
		printk("0x%04x = 0x%04x\n",addr,rt3261_dsp_read(codec, addr));
	}
	else
	{
		param.cmd_fmt = 0x00e0;
		param.cmd = RT3261_DSP_CMD_MW;
		param.addr = addr;
		param.data = val;
		rt3261_dsp_write(codec, &param);
	}

	return count;
}

static DEVICE_ATTR(dsp_reg, 0666, rt3261_dsp_show, dsp_reg_store);

/**
 * rt3261_dsp_probe - register DSP for rt3261
 * @codec: audio codec
 *
 * To register DSP function for rt3261.
 *
 * Returns 0 for success or negative error code.
 */
int rt3261_dsp_probe(struct snd_soc_codec *codec)
{
	//struct rt3261_priv *rt3261;
	int ret;

	if (codec == NULL)
		return -EINVAL;

	snd_soc_add_controls(codec, rt3261_dsp_snd_controls,
			ARRAY_SIZE(rt3261_dsp_snd_controls));
	snd_soc_dapm_new_controls(&codec->dapm, rt3261_dsp_dapm_widgets,
			ARRAY_SIZE(rt3261_dsp_dapm_widgets));
	snd_soc_dapm_add_routes(&codec->dapm, rt3261_dsp_dapm_routes,
			ARRAY_SIZE(rt3261_dsp_dapm_routes));

	/* Patch DSP rom code if IC version is larger than C version */

	ret = snd_soc_update_bits(codec, RT3261_PWR_DIG2,
		RT3261_PWR_I2S_DSP, RT3261_PWR_I2S_DSP);
	if (ret < 0) {
		dev_err(codec->dev,
			"Failed to power up DSP IIS interface: %d\n", ret);
	}

#ifdef INIT_DSP_IN_PROBE
	rt3261_dsp_snd_effect(codec);
	ret = rt3261_dsp_read(codec, 0x22fb);
	if(ret == 0x5a5a)
		pr_info("DSP init success\n");
	else
		pr_info("DSP init failed\n");
#else
	rt3261_dsp_conf(codec);
#endif
	ret = rt3261_dsp_read(codec, 0x3800);
	pr_info("DSP version code = 0x%04x\n",ret);
	/*if(ret != 0x501a) {
		rt3261 = snd_soc_codec_get_drvdata(codec);
		INIT_DELAYED_WORK(&rt3261->patch_work, rt3261_do_dsp_patch);
		schedule_delayed_work(&rt3261->patch_work,
				msecs_to_jiffies(100));
	}*/
#ifdef INIT_DSP_IN_PROBE
	snd_soc_update_bits(codec, RT3261_DSP_CTRL3,
		RT3261_DSP_PD_PIN_MASK, RT3261_DSP_PD_PIN_LO);
#endif
	snd_soc_update_bits(codec, RT3261_PWR_DIG2,
		RT3261_PWR_I2S_DSP, 0);

	ret = device_create_file(codec->dev, &dev_attr_dsp_reg);
	if (ret != 0) {
		dev_err(codec->dev,
			"Failed to create index_reg sysfs files: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rt3261_dsp_probe);

#ifdef RTK_IOCTL
int rt_codec_dsp_ioctl_common(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rt_codec_cmd rt_codec;
	int *buf;
	int *p;
	int ret;
	struct rt3261_dsp_param param;

	//int mask1 = 0, mask2 = 0;

	struct rt_codec_cmd __user *_rt_codec = (struct rt_codec_cmd *)arg;
	struct snd_soc_codec *codec = hw->private_data;
	struct rt3261_priv *rt3261 = snd_soc_codec_get_drvdata(codec);

	if (copy_from_user(&rt_codec, _rt_codec, sizeof(rt_codec))) {
		dev_err(codec->dev, "copy_from_user faild\n");
		return -EFAULT;
	}
	dev_dbg(codec->dev, "rt_codec.number=%d\n",rt_codec.number);
	buf = kmalloc(sizeof(*buf) * rt_codec.number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	if (copy_from_user(buf, rt_codec.buf, sizeof(*buf) * rt_codec.number)) {
		goto err;
	}

	ret = snd_soc_update_bits(codec, RT3261_PWR_DIG2,
		RT3261_PWR_I2S_DSP, RT3261_PWR_I2S_DSP);
	if (ret < 0) {
		dev_err(codec->dev,
			"Failed to power up DSP IIS interface: %d\n", ret);
		goto err;
	}

	switch (cmd) {
	case RT_READ_CODEC_DSP_IOCTL:
		for (p = buf; p < buf + rt_codec.number/2; p++)
			*(p+rt_codec.number/2) = rt3261_dsp_read(codec, *p);
		if (copy_to_user(rt_codec.buf, buf, sizeof(*buf) * rt_codec.number))
			goto err;
		break;

	case RT_WRITE_CODEC_DSP_IOCTL:
		param.cmd_fmt = 0x00e0;
		param.cmd = RT3261_DSP_CMD_MW;
		p = buf;
		param.addr = *p;
		param.data = *(p+rt_codec.number/2);
		if(codec == NULL) {
			dev_dbg(codec->dev, "codec is null\n");
			break;
		}
		for (p = buf; p < buf + rt_codec.number/2; p++)
			rt3261_dsp_write(codec, &param);
		break;

	case RT_GET_CODEC_DSP_MODE_IOCTL:
		*buf = rt3261->dsp_sw;
		if (copy_to_user(rt_codec.buf, buf, sizeof(*buf) * rt_codec.number))
			goto err;
		break;

	default:
		dev_info(codec->dev, "unsported dsp command\n");
		break;
	}

	kfree(buf);
	return 0;

err:
	kfree(buf);
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(rt_codec_dsp_ioctl_common);
#endif

#ifdef CONFIG_PM
int rt3261_dsp_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	return 0;
}
EXPORT_SYMBOL_GPL(rt3261_dsp_suspend);

int rt3261_dsp_resume(struct snd_soc_codec *codec)
{
	return 0;
}
EXPORT_SYMBOL_GPL(rt3261_dsp_resume);
#endif

