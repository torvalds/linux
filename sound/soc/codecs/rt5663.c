/*
 * rt5663.c  --  RT5663 ALSA SoC audio codec driver
 *
 * Copyright 2016 Realtek Semiconductor Corp.
 * Author: Jack Yu <jack.yu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rt5663.h"
#include "rl6231.h"

#define RT5663_DEVICE_ID_2 0x6451
#define RT5663_DEVICE_ID_1 0x6406

enum {
	CODEC_VER_1,
	CODEC_VER_0,
};

struct rt5663_priv {
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	struct delayed_work jack_detect_work;
	struct snd_soc_jack *hs_jack;
	struct timer_list btn_check_timer;

	int codec_ver;
	int sysclk;
	int sysclk_src;
	int lrck;

	int pll_src;
	int pll_in;
	int pll_out;

	int jack_type;
};

static const struct reg_default rt5663_v2_reg[] = {
	{ 0x0000, 0x0000 },
	{ 0x0001, 0xc8c8 },
	{ 0x0002, 0x8080 },
	{ 0x0003, 0x8000 },
	{ 0x0004, 0xc80a },
	{ 0x0005, 0x0000 },
	{ 0x0006, 0x0000 },
	{ 0x0007, 0x0000 },
	{ 0x000a, 0x0000 },
	{ 0x000b, 0x0000 },
	{ 0x000c, 0x0000 },
	{ 0x000d, 0x0000 },
	{ 0x000f, 0x0808 },
	{ 0x0010, 0x4000 },
	{ 0x0011, 0x0000 },
	{ 0x0012, 0x1404 },
	{ 0x0013, 0x1000 },
	{ 0x0014, 0xa00a },
	{ 0x0015, 0x0404 },
	{ 0x0016, 0x0404 },
	{ 0x0017, 0x0011 },
	{ 0x0018, 0xafaf },
	{ 0x0019, 0xafaf },
	{ 0x001a, 0xafaf },
	{ 0x001b, 0x0011 },
	{ 0x001c, 0x2f2f },
	{ 0x001d, 0x2f2f },
	{ 0x001e, 0x2f2f },
	{ 0x001f, 0x0000 },
	{ 0x0020, 0x0000 },
	{ 0x0021, 0x0000 },
	{ 0x0022, 0x5757 },
	{ 0x0023, 0x0039 },
	{ 0x0024, 0x000b },
	{ 0x0026, 0xc0c0 },
	{ 0x0027, 0xc0c0 },
	{ 0x0028, 0xc0c0 },
	{ 0x0029, 0x8080 },
	{ 0x002a, 0xaaaa },
	{ 0x002b, 0xaaaa },
	{ 0x002c, 0xaba8 },
	{ 0x002d, 0x0000 },
	{ 0x002e, 0x0000 },
	{ 0x002f, 0x0000 },
	{ 0x0030, 0x0000 },
	{ 0x0031, 0x5000 },
	{ 0x0032, 0x0000 },
	{ 0x0033, 0x0000 },
	{ 0x0034, 0x0000 },
	{ 0x0035, 0x0000 },
	{ 0x003a, 0x0000 },
	{ 0x003b, 0x0000 },
	{ 0x003c, 0x00ff },
	{ 0x003d, 0x0000 },
	{ 0x003e, 0x00ff },
	{ 0x003f, 0x0000 },
	{ 0x0040, 0x0000 },
	{ 0x0041, 0x00ff },
	{ 0x0042, 0x0000 },
	{ 0x0043, 0x00ff },
	{ 0x0044, 0x0c0c },
	{ 0x0049, 0xc00b },
	{ 0x004a, 0x0000 },
	{ 0x004b, 0x031f },
	{ 0x004d, 0x0000 },
	{ 0x004e, 0x001f },
	{ 0x004f, 0x0000 },
	{ 0x0050, 0x001f },
	{ 0x0052, 0xf000 },
	{ 0x0061, 0x0000 },
	{ 0x0062, 0x0000 },
	{ 0x0063, 0x003e },
	{ 0x0064, 0x0000 },
	{ 0x0065, 0x0000 },
	{ 0x0066, 0x003f },
	{ 0x0067, 0x0000 },
	{ 0x006b, 0x0000 },
	{ 0x006d, 0xff00 },
	{ 0x006e, 0x2808 },
	{ 0x006f, 0x000a },
	{ 0x0070, 0x8000 },
	{ 0x0071, 0x8000 },
	{ 0x0072, 0x8000 },
	{ 0x0073, 0x7000 },
	{ 0x0074, 0x7770 },
	{ 0x0075, 0x0002 },
	{ 0x0076, 0x0001 },
	{ 0x0078, 0x00f0 },
	{ 0x0079, 0x0000 },
	{ 0x007a, 0x0000 },
	{ 0x007b, 0x0000 },
	{ 0x007c, 0x0000 },
	{ 0x007d, 0x0123 },
	{ 0x007e, 0x4500 },
	{ 0x007f, 0x8003 },
	{ 0x0080, 0x0000 },
	{ 0x0081, 0x0000 },
	{ 0x0082, 0x0000 },
	{ 0x0083, 0x0000 },
	{ 0x0084, 0x0000 },
	{ 0x0085, 0x0000 },
	{ 0x0086, 0x0008 },
	{ 0x0087, 0x0000 },
	{ 0x0088, 0x0000 },
	{ 0x0089, 0x0000 },
	{ 0x008a, 0x0000 },
	{ 0x008b, 0x0000 },
	{ 0x008c, 0x0003 },
	{ 0x008e, 0x0060 },
	{ 0x008f, 0x1000 },
	{ 0x0091, 0x0c26 },
	{ 0x0092, 0x0073 },
	{ 0x0093, 0x0000 },
	{ 0x0094, 0x0080 },
	{ 0x0098, 0x0000 },
	{ 0x0099, 0x0000 },
	{ 0x009a, 0x0007 },
	{ 0x009f, 0x0000 },
	{ 0x00a0, 0x0000 },
	{ 0x00a1, 0x0002 },
	{ 0x00a2, 0x0001 },
	{ 0x00a3, 0x0002 },
	{ 0x00a4, 0x0001 },
	{ 0x00ae, 0x2040 },
	{ 0x00af, 0x0000 },
	{ 0x00b6, 0x0000 },
	{ 0x00b7, 0x0000 },
	{ 0x00b8, 0x0000 },
	{ 0x00b9, 0x0000 },
	{ 0x00ba, 0x0002 },
	{ 0x00bb, 0x0000 },
	{ 0x00be, 0x0000 },
	{ 0x00c0, 0x0000 },
	{ 0x00c1, 0x0aaa },
	{ 0x00c2, 0xaa80 },
	{ 0x00c3, 0x0003 },
	{ 0x00c4, 0x0000 },
	{ 0x00d0, 0x0000 },
	{ 0x00d1, 0x2244 },
	{ 0x00d2, 0x0000 },
	{ 0x00d3, 0x3300 },
	{ 0x00d4, 0x2200 },
	{ 0x00d9, 0x0809 },
	{ 0x00da, 0x0000 },
	{ 0x00db, 0x0008 },
	{ 0x00dc, 0x00c0 },
	{ 0x00dd, 0x6724 },
	{ 0x00de, 0x3131 },
	{ 0x00df, 0x0008 },
	{ 0x00e0, 0x4000 },
	{ 0x00e1, 0x3131 },
	{ 0x00e2, 0x600c },
	{ 0x00ea, 0xb320 },
	{ 0x00eb, 0x0000 },
	{ 0x00ec, 0xb300 },
	{ 0x00ed, 0x0000 },
	{ 0x00ee, 0xb320 },
	{ 0x00ef, 0x0000 },
	{ 0x00f0, 0x0201 },
	{ 0x00f1, 0x0ddd },
	{ 0x00f2, 0x0ddd },
	{ 0x00f6, 0x0000 },
	{ 0x00f7, 0x0000 },
	{ 0x00f8, 0x0000 },
	{ 0x00fa, 0x0000 },
	{ 0x00fb, 0x0000 },
	{ 0x00fc, 0x0000 },
	{ 0x00fd, 0x0000 },
	{ 0x00fe, 0x10ec },
	{ 0x00ff, 0x6451 },
	{ 0x0100, 0xaaaa },
	{ 0x0101, 0x000a },
	{ 0x010a, 0xaaaa },
	{ 0x010b, 0xa0a0 },
	{ 0x010c, 0xaeae },
	{ 0x010d, 0xaaaa },
	{ 0x010e, 0xaaaa },
	{ 0x010f, 0xaaaa },
	{ 0x0110, 0xe002 },
	{ 0x0111, 0xa602 },
	{ 0x0112, 0xaaaa },
	{ 0x0113, 0x2000 },
	{ 0x0117, 0x0f00 },
	{ 0x0125, 0x0420 },
	{ 0x0132, 0x0000 },
	{ 0x0133, 0x0000 },
	{ 0x0136, 0x5555 },
	{ 0x0137, 0x5540 },
	{ 0x0138, 0x3700 },
	{ 0x0139, 0x79a1 },
	{ 0x013a, 0x2020 },
	{ 0x013b, 0x2020 },
	{ 0x013c, 0x2005 },
	{ 0x013f, 0x0000 },
	{ 0x0145, 0x0002 },
	{ 0x0146, 0x0000 },
	{ 0x0147, 0x0000 },
	{ 0x0148, 0x0000 },
	{ 0x0160, 0x4ec0 },
	{ 0x0161, 0x0080 },
	{ 0x0162, 0x0200 },
	{ 0x0163, 0x0800 },
	{ 0x0164, 0x0000 },
	{ 0x0165, 0x0000 },
	{ 0x0166, 0x0000 },
	{ 0x0167, 0x000f },
	{ 0x0168, 0x000f },
	{ 0x0170, 0x4e80 },
	{ 0x0171, 0x0080 },
	{ 0x0172, 0x0200 },
	{ 0x0173, 0x0800 },
	{ 0x0174, 0x00ff },
	{ 0x0175, 0x0000 },
	{ 0x0190, 0x4131 },
	{ 0x0191, 0x4131 },
	{ 0x0192, 0x4131 },
	{ 0x0193, 0x4131 },
	{ 0x0194, 0x0000 },
	{ 0x0195, 0x0000 },
	{ 0x0196, 0x0000 },
	{ 0x0197, 0x0000 },
	{ 0x0198, 0x0000 },
	{ 0x0199, 0x0000 },
	{ 0x01a0, 0x1e64 },
	{ 0x01a1, 0x06a3 },
	{ 0x01a2, 0x0000 },
	{ 0x01a3, 0x0000 },
	{ 0x01a4, 0x0000 },
	{ 0x01a5, 0x0000 },
	{ 0x01a6, 0x0000 },
	{ 0x01a7, 0x0000 },
	{ 0x01a8, 0x0000 },
	{ 0x01a9, 0x0000 },
	{ 0x01aa, 0x0000 },
	{ 0x01ab, 0x0000 },
	{ 0x01b5, 0x0000 },
	{ 0x01b6, 0x01c3 },
	{ 0x01b7, 0x02a0 },
	{ 0x01b8, 0x03e9 },
	{ 0x01b9, 0x1389 },
	{ 0x01ba, 0xc351 },
	{ 0x01bb, 0x0009 },
	{ 0x01bc, 0x0018 },
	{ 0x01bd, 0x002a },
	{ 0x01be, 0x004c },
	{ 0x01bf, 0x0097 },
	{ 0x01c0, 0x433d },
	{ 0x01c1, 0x0000 },
	{ 0x01c2, 0x0000 },
	{ 0x01c3, 0x0000 },
	{ 0x01c4, 0x0000 },
	{ 0x01c5, 0x0000 },
	{ 0x01c6, 0x0000 },
	{ 0x01c7, 0x0000 },
	{ 0x01c8, 0x40af },
	{ 0x01c9, 0x0702 },
	{ 0x01ca, 0x0000 },
	{ 0x01cb, 0x0000 },
	{ 0x01cc, 0x5757 },
	{ 0x01cd, 0x5757 },
	{ 0x01ce, 0x5757 },
	{ 0x01cf, 0x5757 },
	{ 0x01d0, 0x5757 },
	{ 0x01d1, 0x5757 },
	{ 0x01d2, 0x5757 },
	{ 0x01d3, 0x5757 },
	{ 0x01d4, 0x5757 },
	{ 0x01d5, 0x5757 },
	{ 0x01d6, 0x003c },
	{ 0x01da, 0x0000 },
	{ 0x01db, 0x0000 },
	{ 0x01dc, 0x0000 },
	{ 0x01de, 0x7c00 },
	{ 0x01df, 0x0320 },
	{ 0x01e0, 0x06a1 },
	{ 0x01e1, 0x0000 },
	{ 0x01e2, 0x0000 },
	{ 0x01e3, 0x0000 },
	{ 0x01e4, 0x0000 },
	{ 0x01e5, 0x0000 },
	{ 0x01e6, 0x0001 },
	{ 0x01e7, 0x0000 },
	{ 0x01e8, 0x0000 },
	{ 0x01ea, 0x0000 },
	{ 0x01eb, 0x0000 },
	{ 0x01ec, 0x0000 },
	{ 0x01ed, 0x0000 },
	{ 0x01ee, 0x0000 },
	{ 0x01ef, 0x0000 },
	{ 0x01f0, 0x0000 },
	{ 0x01f1, 0x0000 },
	{ 0x01f2, 0x0000 },
	{ 0x01f3, 0x0000 },
	{ 0x01f4, 0x0000 },
	{ 0x0200, 0x0000 },
	{ 0x0201, 0x0000 },
	{ 0x0202, 0x0000 },
	{ 0x0203, 0x0000 },
	{ 0x0204, 0x0000 },
	{ 0x0205, 0x0000 },
	{ 0x0206, 0x0000 },
	{ 0x0207, 0x0000 },
	{ 0x0208, 0x0000 },
	{ 0x0210, 0x60b1 },
	{ 0x0211, 0xa000 },
	{ 0x0212, 0x024c },
	{ 0x0213, 0xf7ff },
	{ 0x0214, 0x024c },
	{ 0x0215, 0x0102 },
	{ 0x0216, 0x00a3 },
	{ 0x0217, 0x0048 },
	{ 0x0218, 0x92c0 },
	{ 0x0219, 0x0000 },
	{ 0x021a, 0x00c8 },
	{ 0x021b, 0x0020 },
	{ 0x02fa, 0x0000 },
	{ 0x02fb, 0x0000 },
	{ 0x02fc, 0x0000 },
	{ 0x02ff, 0x0110 },
	{ 0x0300, 0x001f },
	{ 0x0301, 0x032c },
	{ 0x0302, 0x5f21 },
	{ 0x0303, 0x4000 },
	{ 0x0304, 0x4000 },
	{ 0x0305, 0x06d5 },
	{ 0x0306, 0x8000 },
	{ 0x0307, 0x0700 },
	{ 0x0310, 0x4560 },
	{ 0x0311, 0xa4a8 },
	{ 0x0312, 0x7418 },
	{ 0x0313, 0x0000 },
	{ 0x0314, 0x0006 },
	{ 0x0315, 0xffff },
	{ 0x0316, 0xc400 },
	{ 0x0317, 0x0000 },
	{ 0x0330, 0x00a6 },
	{ 0x0331, 0x04c3 },
	{ 0x0332, 0x27c8 },
	{ 0x0333, 0xbf50 },
	{ 0x0334, 0x0045 },
	{ 0x0335, 0x0007 },
	{ 0x0336, 0x7418 },
	{ 0x0337, 0x0501 },
	{ 0x0338, 0x0000 },
	{ 0x0339, 0x0010 },
	{ 0x033a, 0x1010 },
	{ 0x03c0, 0x7e00 },
	{ 0x03c1, 0x8000 },
	{ 0x03c2, 0x8000 },
	{ 0x03c3, 0x8000 },
	{ 0x03c4, 0x8000 },
	{ 0x03c5, 0x8000 },
	{ 0x03c6, 0x8000 },
	{ 0x03c7, 0x8000 },
	{ 0x03c8, 0x8000 },
	{ 0x03c9, 0x8000 },
	{ 0x03ca, 0x8000 },
	{ 0x03cb, 0x8000 },
	{ 0x03cc, 0x8000 },
	{ 0x03d0, 0x0000 },
	{ 0x03d1, 0x0000 },
	{ 0x03d2, 0x0000 },
	{ 0x03d3, 0x0000 },
	{ 0x03d4, 0x2000 },
	{ 0x03d5, 0x2000 },
	{ 0x03d6, 0x0000 },
	{ 0x03d7, 0x0000 },
	{ 0x03d8, 0x2000 },
	{ 0x03d9, 0x2000 },
	{ 0x03da, 0x2000 },
	{ 0x03db, 0x2000 },
	{ 0x03dc, 0x0000 },
	{ 0x03dd, 0x0000 },
	{ 0x03de, 0x0000 },
	{ 0x03df, 0x2000 },
	{ 0x03e0, 0x0000 },
	{ 0x03e1, 0x0000 },
	{ 0x03e2, 0x0000 },
	{ 0x03e3, 0x0000 },
	{ 0x03e4, 0x0000 },
	{ 0x03e5, 0x0000 },
	{ 0x03e6, 0x0000 },
	{ 0x03e7, 0x0000 },
	{ 0x03e8, 0x0000 },
	{ 0x03e9, 0x0000 },
	{ 0x03ea, 0x0000 },
	{ 0x03eb, 0x0000 },
	{ 0x03ec, 0x0000 },
	{ 0x03ed, 0x0000 },
	{ 0x03ee, 0x0000 },
	{ 0x03ef, 0x0000 },
	{ 0x03f0, 0x0800 },
	{ 0x03f1, 0x0800 },
	{ 0x03f2, 0x0800 },
	{ 0x03f3, 0x0800 },
	{ 0x03fe, 0x0000 },
	{ 0x03ff, 0x0000 },
	{ 0x07f0, 0x0000 },
	{ 0x07fa, 0x0000 },
};

static const struct reg_default rt5663_reg[] = {
	{ 0x0000, 0x0000 },
	{ 0x0002, 0x0008 },
	{ 0x0005, 0x1000 },
	{ 0x0006, 0x1000 },
	{ 0x000a, 0x0000 },
	{ 0x0010, 0x000f },
	{ 0x0015, 0x42f1 },
	{ 0x0016, 0x0000 },
	{ 0x0018, 0x000b },
	{ 0x0019, 0xafaf },
	{ 0x001c, 0x2f2f },
	{ 0x001f, 0x0000 },
	{ 0x0022, 0x5757 },
	{ 0x0023, 0x0039 },
	{ 0x0026, 0xc0c0 },
	{ 0x0029, 0x8080 },
	{ 0x002a, 0xa0a0 },
	{ 0x002c, 0x000c },
	{ 0x002d, 0x0000 },
	{ 0x0040, 0x0808 },
	{ 0x0061, 0x0000 },
	{ 0x0062, 0x0000 },
	{ 0x0063, 0x003e },
	{ 0x0064, 0x0000 },
	{ 0x0065, 0x0000 },
	{ 0x0066, 0x0000 },
	{ 0x006b, 0x0000 },
	{ 0x006e, 0x0000 },
	{ 0x006f, 0x0000 },
	{ 0x0070, 0x8020 },
	{ 0x0073, 0x1000 },
	{ 0x0074, 0xe400 },
	{ 0x0075, 0x0002 },
	{ 0x0076, 0x0001 },
	{ 0x0077, 0x00f0 },
	{ 0x0078, 0x0000 },
	{ 0x0079, 0x0000 },
	{ 0x007a, 0x0123 },
	{ 0x007b, 0x8003 },
	{ 0x0080, 0x0000 },
	{ 0x0081, 0x0000 },
	{ 0x0082, 0x0000 },
	{ 0x0083, 0x0000 },
	{ 0x0084, 0x0000 },
	{ 0x0086, 0x0008 },
	{ 0x0087, 0x0000 },
	{ 0x008a, 0x0000 },
	{ 0x008b, 0x0000 },
	{ 0x008c, 0x0003 },
	{ 0x008e, 0x0008 },
	{ 0x008f, 0x1000 },
	{ 0x0090, 0x0646 },
	{ 0x0091, 0x0e3e },
	{ 0x0092, 0x1071 },
	{ 0x0093, 0x0000 },
	{ 0x0094, 0x0080 },
	{ 0x0097, 0x0000 },
	{ 0x0098, 0x0000 },
	{ 0x009a, 0x0000 },
	{ 0x009f, 0x0000 },
	{ 0x00ae, 0x6000 },
	{ 0x00af, 0x0000 },
	{ 0x00b6, 0x0000 },
	{ 0x00b7, 0x0000 },
	{ 0x00b8, 0x0000 },
	{ 0x00ba, 0x0000 },
	{ 0x00bb, 0x0000 },
	{ 0x00be, 0x0000 },
	{ 0x00bf, 0x0000 },
	{ 0x00c0, 0x0000 },
	{ 0x00c1, 0x0000 },
	{ 0x00c5, 0x0000 },
	{ 0x00cb, 0xa02f },
	{ 0x00cc, 0x0000 },
	{ 0x00cd, 0x0e02 },
	{ 0x00d9, 0x08f9 },
	{ 0x00db, 0x0008 },
	{ 0x00dc, 0x00c0 },
	{ 0x00dd, 0x6729 },
	{ 0x00de, 0x3131 },
	{ 0x00df, 0x0008 },
	{ 0x00e0, 0x4000 },
	{ 0x00e1, 0x3131 },
	{ 0x00e2, 0x0043 },
	{ 0x00e4, 0x400b },
	{ 0x00e5, 0x8031 },
	{ 0x00e6, 0x3080 },
	{ 0x00e7, 0x4100 },
	{ 0x00e8, 0x1400 },
	{ 0x00e9, 0xe00a },
	{ 0x00ea, 0x0404 },
	{ 0x00eb, 0x0404 },
	{ 0x00ec, 0xb320 },
	{ 0x00ed, 0x0000 },
	{ 0x00f4, 0x0000 },
	{ 0x00f6, 0x0000 },
	{ 0x00f8, 0x0000 },
	{ 0x00fa, 0x8000 },
	{ 0x00fd, 0x0001 },
	{ 0x00fe, 0x10ec },
	{ 0x00ff, 0x6406 },
	{ 0x0100, 0xa0a0 },
	{ 0x0108, 0x4444 },
	{ 0x0109, 0x4444 },
	{ 0x010a, 0xaaaa },
	{ 0x010b, 0x00a0 },
	{ 0x010c, 0x8aaa },
	{ 0x010d, 0xaaaa },
	{ 0x010e, 0x2aaa },
	{ 0x010f, 0x002a },
	{ 0x0110, 0xa0a4 },
	{ 0x0111, 0x4602 },
	{ 0x0112, 0x0101 },
	{ 0x0113, 0x2000 },
	{ 0x0114, 0x0000 },
	{ 0x0116, 0x0000 },
	{ 0x0117, 0x0f00 },
	{ 0x0118, 0x0006 },
	{ 0x0125, 0x2424 },
	{ 0x0126, 0x5550 },
	{ 0x0127, 0x0400 },
	{ 0x0128, 0x7711 },
	{ 0x0132, 0x0004 },
	{ 0x0137, 0x5441 },
	{ 0x0139, 0x79a1 },
	{ 0x013a, 0x30c0 },
	{ 0x013b, 0x2000 },
	{ 0x013c, 0x2005 },
	{ 0x013d, 0x30c0 },
	{ 0x013e, 0x0000 },
	{ 0x0140, 0x3700 },
	{ 0x0141, 0x1f00 },
	{ 0x0144, 0x0000 },
	{ 0x0145, 0x0002 },
	{ 0x0146, 0x0000 },
	{ 0x0160, 0x0e80 },
	{ 0x0161, 0x0080 },
	{ 0x0162, 0x0200 },
	{ 0x0163, 0x0800 },
	{ 0x0164, 0x0000 },
	{ 0x0165, 0x0000 },
	{ 0x0166, 0x0000 },
	{ 0x0167, 0x1417 },
	{ 0x0168, 0x0017 },
	{ 0x0169, 0x0017 },
	{ 0x0180, 0x2000 },
	{ 0x0181, 0x0000 },
	{ 0x0182, 0x0000 },
	{ 0x0183, 0x2000 },
	{ 0x0184, 0x0000 },
	{ 0x0185, 0x0000 },
	{ 0x01b0, 0x4b30 },
	{ 0x01b1, 0x0000 },
	{ 0x01b2, 0xd870 },
	{ 0x01b3, 0x0000 },
	{ 0x01b4, 0x0030 },
	{ 0x01b5, 0x5757 },
	{ 0x01b6, 0x5757 },
	{ 0x01b7, 0x5757 },
	{ 0x01b8, 0x5757 },
	{ 0x01c0, 0x433d },
	{ 0x01c1, 0x0540 },
	{ 0x01c2, 0x0000 },
	{ 0x01c3, 0x0000 },
	{ 0x01c4, 0x0000 },
	{ 0x01c5, 0x0009 },
	{ 0x01c6, 0x0018 },
	{ 0x01c7, 0x002a },
	{ 0x01c8, 0x004c },
	{ 0x01c9, 0x0097 },
	{ 0x01ca, 0x01c3 },
	{ 0x01cb, 0x03e9 },
	{ 0x01cc, 0x1389 },
	{ 0x01cd, 0xc351 },
	{ 0x01ce, 0x0000 },
	{ 0x01cf, 0x0000 },
	{ 0x01d0, 0x0000 },
	{ 0x01d1, 0x0000 },
	{ 0x01d2, 0x0000 },
	{ 0x01d3, 0x003c },
	{ 0x01d4, 0x5757 },
	{ 0x01d5, 0x5757 },
	{ 0x01d6, 0x5757 },
	{ 0x01d7, 0x5757 },
	{ 0x01d8, 0x5757 },
	{ 0x01d9, 0x5757 },
	{ 0x01da, 0x0000 },
	{ 0x01db, 0x0000 },
	{ 0x01dd, 0x0009 },
	{ 0x01de, 0x7f00 },
	{ 0x01df, 0x00c8 },
	{ 0x01e0, 0x0691 },
	{ 0x01e1, 0x0000 },
	{ 0x01e2, 0x0000 },
	{ 0x01e3, 0x0000 },
	{ 0x01e4, 0x0000 },
	{ 0x01e5, 0x0040 },
	{ 0x01e6, 0x0000 },
	{ 0x01e7, 0x0000 },
	{ 0x01e8, 0x0000 },
	{ 0x01ea, 0x0000 },
	{ 0x01eb, 0x0000 },
	{ 0x01ec, 0x0000 },
	{ 0x01ed, 0x0000 },
	{ 0x01ee, 0x0000 },
	{ 0x01ef, 0x0000 },
	{ 0x01f0, 0x0000 },
	{ 0x01f1, 0x0000 },
	{ 0x01f2, 0x0000 },
	{ 0x0200, 0x0000 },
	{ 0x0201, 0x2244 },
	{ 0x0202, 0xaaaa },
	{ 0x0250, 0x8010 },
	{ 0x0251, 0x0000 },
	{ 0x0252, 0x028a },
	{ 0x02fa, 0x0000 },
	{ 0x02fb, 0x00a4 },
	{ 0x02fc, 0x0300 },
	{ 0x0300, 0x0000 },
	{ 0x03d0, 0x0000 },
	{ 0x03d1, 0x0000 },
	{ 0x03d2, 0x0000 },
	{ 0x03d3, 0x0000 },
	{ 0x03d4, 0x2000 },
	{ 0x03d5, 0x2000 },
	{ 0x03d6, 0x0000 },
	{ 0x03d7, 0x0000 },
	{ 0x03d8, 0x2000 },
	{ 0x03d9, 0x2000 },
	{ 0x03da, 0x2000 },
	{ 0x03db, 0x2000 },
	{ 0x03dc, 0x0000 },
	{ 0x03dd, 0x0000 },
	{ 0x03de, 0x0000 },
	{ 0x03df, 0x2000 },
	{ 0x03e0, 0x0000 },
	{ 0x03e1, 0x0000 },
	{ 0x03e2, 0x0000 },
	{ 0x03e3, 0x0000 },
	{ 0x03e4, 0x0000 },
	{ 0x03e5, 0x0000 },
	{ 0x03e6, 0x0000 },
	{ 0x03e7, 0x0000 },
	{ 0x03e8, 0x0000 },
	{ 0x03e9, 0x0000 },
	{ 0x03ea, 0x0000 },
	{ 0x03eb, 0x0000 },
	{ 0x03ec, 0x0000 },
	{ 0x03ed, 0x0000 },
	{ 0x03ee, 0x0000 },
	{ 0x03ef, 0x0000 },
	{ 0x03f0, 0x0800 },
	{ 0x03f1, 0x0800 },
	{ 0x03f2, 0x0800 },
	{ 0x03f3, 0x0800 },
};

static bool rt5663_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5663_RESET:
	case RT5663_SIL_DET_CTL:
	case RT5663_HP_IMP_GAIN_2:
	case RT5663_AD_DA_MIXER:
	case RT5663_FRAC_DIV_2:
	case RT5663_MICBIAS_1:
	case RT5663_ASRC_11_2:
	case RT5663_ADC_EQ_1:
	case RT5663_INT_ST_1:
	case RT5663_INT_ST_2:
	case RT5663_GPIO_STA1:
	case RT5663_SIN_GEN_1:
	case RT5663_IL_CMD_1:
	case RT5663_IL_CMD_5:
	case RT5663_IL_CMD_PWRSAV1:
	case RT5663_EM_JACK_TYPE_1:
	case RT5663_EM_JACK_TYPE_2:
	case RT5663_EM_JACK_TYPE_3:
	case RT5663_JD_CTRL2:
	case RT5663_VENDOR_ID:
	case RT5663_VENDOR_ID_1:
	case RT5663_VENDOR_ID_2:
	case RT5663_PLL_INT_REG:
	case RT5663_SOFT_RAMP:
	case RT5663_STO_DRE_1:
	case RT5663_STO_DRE_5:
	case RT5663_STO_DRE_6:
	case RT5663_STO_DRE_7:
	case RT5663_MIC_DECRO_1:
	case RT5663_MIC_DECRO_4:
	case RT5663_HP_IMP_SEN_1:
	case RT5663_HP_IMP_SEN_3:
	case RT5663_HP_IMP_SEN_4:
	case RT5663_HP_IMP_SEN_5:
	case RT5663_HP_CALIB_1_1:
	case RT5663_HP_CALIB_9:
	case RT5663_HP_CALIB_ST1:
	case RT5663_HP_CALIB_ST2:
	case RT5663_HP_CALIB_ST3:
	case RT5663_HP_CALIB_ST4:
	case RT5663_HP_CALIB_ST5:
	case RT5663_HP_CALIB_ST6:
	case RT5663_HP_CALIB_ST7:
	case RT5663_HP_CALIB_ST8:
	case RT5663_HP_CALIB_ST9:
	case RT5663_ANA_JD:
		return true;
	default:
		return false;
	}
}

static bool rt5663_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5663_RESET:
	case RT5663_HP_OUT_EN:
	case RT5663_HP_LCH_DRE:
	case RT5663_HP_RCH_DRE:
	case RT5663_CALIB_BST:
	case RT5663_RECMIX:
	case RT5663_SIL_DET_CTL:
	case RT5663_PWR_SAV_SILDET:
	case RT5663_SIDETONE_CTL:
	case RT5663_STO1_DAC_DIG_VOL:
	case RT5663_STO1_ADC_DIG_VOL:
	case RT5663_STO1_BOOST:
	case RT5663_HP_IMP_GAIN_1:
	case RT5663_HP_IMP_GAIN_2:
	case RT5663_STO1_ADC_MIXER:
	case RT5663_AD_DA_MIXER:
	case RT5663_STO_DAC_MIXER:
	case RT5663_DIG_SIDE_MIXER:
	case RT5663_BYPASS_STO_DAC:
	case RT5663_CALIB_REC_MIX:
	case RT5663_PWR_DIG_1:
	case RT5663_PWR_DIG_2:
	case RT5663_PWR_ANLG_1:
	case RT5663_PWR_ANLG_2:
	case RT5663_PWR_ANLG_3:
	case RT5663_PWR_MIXER:
	case RT5663_SIG_CLK_DET:
	case RT5663_PRE_DIV_GATING_1:
	case RT5663_PRE_DIV_GATING_2:
	case RT5663_I2S1_SDP:
	case RT5663_ADDA_CLK_1:
	case RT5663_ADDA_RST:
	case RT5663_FRAC_DIV_1:
	case RT5663_FRAC_DIV_2:
	case RT5663_TDM_1:
	case RT5663_TDM_2:
	case RT5663_TDM_3:
	case RT5663_TDM_4:
	case RT5663_TDM_5:
	case RT5663_GLB_CLK:
	case RT5663_PLL_1:
	case RT5663_PLL_2:
	case RT5663_ASRC_1:
	case RT5663_ASRC_2:
	case RT5663_ASRC_4:
	case RT5663_DUMMY_REG:
	case RT5663_ASRC_8:
	case RT5663_ASRC_9:
	case RT5663_ASRC_11:
	case RT5663_DEPOP_1:
	case RT5663_DEPOP_2:
	case RT5663_DEPOP_3:
	case RT5663_HP_CHARGE_PUMP_1:
	case RT5663_HP_CHARGE_PUMP_2:
	case RT5663_MICBIAS_1:
	case RT5663_RC_CLK:
	case RT5663_ASRC_11_2:
	case RT5663_DUMMY_REG_2:
	case RT5663_REC_PATH_GAIN:
	case RT5663_AUTO_1MRC_CLK:
	case RT5663_ADC_EQ_1:
	case RT5663_ADC_EQ_2:
	case RT5663_IRQ_1:
	case RT5663_IRQ_2:
	case RT5663_IRQ_3:
	case RT5663_IRQ_4:
	case RT5663_IRQ_5:
	case RT5663_INT_ST_1:
	case RT5663_INT_ST_2:
	case RT5663_GPIO_1:
	case RT5663_GPIO_2:
	case RT5663_GPIO_STA1:
	case RT5663_SIN_GEN_1:
	case RT5663_SIN_GEN_2:
	case RT5663_SIN_GEN_3:
	case RT5663_SOF_VOL_ZC1:
	case RT5663_IL_CMD_1:
	case RT5663_IL_CMD_2:
	case RT5663_IL_CMD_3:
	case RT5663_IL_CMD_4:
	case RT5663_IL_CMD_5:
	case RT5663_IL_CMD_6:
	case RT5663_IL_CMD_7:
	case RT5663_IL_CMD_8:
	case RT5663_IL_CMD_PWRSAV1:
	case RT5663_IL_CMD_PWRSAV2:
	case RT5663_EM_JACK_TYPE_1:
	case RT5663_EM_JACK_TYPE_2:
	case RT5663_EM_JACK_TYPE_3:
	case RT5663_EM_JACK_TYPE_4:
	case RT5663_EM_JACK_TYPE_5:
	case RT5663_EM_JACK_TYPE_6:
	case RT5663_STO1_HPF_ADJ1:
	case RT5663_STO1_HPF_ADJ2:
	case RT5663_FAST_OFF_MICBIAS:
	case RT5663_JD_CTRL1:
	case RT5663_JD_CTRL2:
	case RT5663_DIG_MISC:
	case RT5663_VENDOR_ID:
	case RT5663_VENDOR_ID_1:
	case RT5663_VENDOR_ID_2:
	case RT5663_DIG_VOL_ZCD:
	case RT5663_ANA_BIAS_CUR_1:
	case RT5663_ANA_BIAS_CUR_2:
	case RT5663_ANA_BIAS_CUR_3:
	case RT5663_ANA_BIAS_CUR_4:
	case RT5663_ANA_BIAS_CUR_5:
	case RT5663_ANA_BIAS_CUR_6:
	case RT5663_BIAS_CUR_5:
	case RT5663_BIAS_CUR_6:
	case RT5663_BIAS_CUR_7:
	case RT5663_BIAS_CUR_8:
	case RT5663_DACREF_LDO:
	case RT5663_DUMMY_REG_3:
	case RT5663_BIAS_CUR_9:
	case RT5663_DUMMY_REG_4:
	case RT5663_VREFADJ_OP:
	case RT5663_VREF_RECMIX:
	case RT5663_CHARGE_PUMP_1:
	case RT5663_CHARGE_PUMP_1_2:
	case RT5663_CHARGE_PUMP_1_3:
	case RT5663_CHARGE_PUMP_2:
	case RT5663_DIG_IN_PIN1:
	case RT5663_PAD_DRV_CTL:
	case RT5663_PLL_INT_REG:
	case RT5663_CHOP_DAC_L:
	case RT5663_CHOP_ADC:
	case RT5663_CALIB_ADC:
	case RT5663_CHOP_DAC_R:
	case RT5663_DUMMY_CTL_DACLR:
	case RT5663_DUMMY_REG_5:
	case RT5663_SOFT_RAMP:
	case RT5663_TEST_MODE_1:
	case RT5663_TEST_MODE_2:
	case RT5663_TEST_MODE_3:
	case RT5663_STO_DRE_1:
	case RT5663_STO_DRE_2:
	case RT5663_STO_DRE_3:
	case RT5663_STO_DRE_4:
	case RT5663_STO_DRE_5:
	case RT5663_STO_DRE_6:
	case RT5663_STO_DRE_7:
	case RT5663_STO_DRE_8:
	case RT5663_STO_DRE_9:
	case RT5663_STO_DRE_10:
	case RT5663_MIC_DECRO_1:
	case RT5663_MIC_DECRO_2:
	case RT5663_MIC_DECRO_3:
	case RT5663_MIC_DECRO_4:
	case RT5663_MIC_DECRO_5:
	case RT5663_MIC_DECRO_6:
	case RT5663_HP_DECRO_1:
	case RT5663_HP_DECRO_2:
	case RT5663_HP_DECRO_3:
	case RT5663_HP_DECRO_4:
	case RT5663_HP_DECOUP:
	case RT5663_HP_IMP_SEN_MAP8:
	case RT5663_HP_IMP_SEN_MAP9:
	case RT5663_HP_IMP_SEN_MAP10:
	case RT5663_HP_IMP_SEN_MAP11:
	case RT5663_HP_IMP_SEN_1:
	case RT5663_HP_IMP_SEN_2:
	case RT5663_HP_IMP_SEN_3:
	case RT5663_HP_IMP_SEN_4:
	case RT5663_HP_IMP_SEN_5:
	case RT5663_HP_IMP_SEN_6:
	case RT5663_HP_IMP_SEN_7:
	case RT5663_HP_IMP_SEN_8:
	case RT5663_HP_IMP_SEN_9:
	case RT5663_HP_IMP_SEN_10:
	case RT5663_HP_IMP_SEN_11:
	case RT5663_HP_IMP_SEN_12:
	case RT5663_HP_IMP_SEN_13:
	case RT5663_HP_IMP_SEN_14:
	case RT5663_HP_IMP_SEN_15:
	case RT5663_HP_IMP_SEN_16:
	case RT5663_HP_IMP_SEN_17:
	case RT5663_HP_IMP_SEN_18:
	case RT5663_HP_IMP_SEN_19:
	case RT5663_HP_IMPSEN_DIG5:
	case RT5663_HP_IMPSEN_MAP1:
	case RT5663_HP_IMPSEN_MAP2:
	case RT5663_HP_IMPSEN_MAP3:
	case RT5663_HP_IMPSEN_MAP4:
	case RT5663_HP_IMPSEN_MAP5:
	case RT5663_HP_IMPSEN_MAP7:
	case RT5663_HP_LOGIC_1:
	case RT5663_HP_LOGIC_2:
	case RT5663_HP_CALIB_1:
	case RT5663_HP_CALIB_1_1:
	case RT5663_HP_CALIB_2:
	case RT5663_HP_CALIB_3:
	case RT5663_HP_CALIB_4:
	case RT5663_HP_CALIB_5:
	case RT5663_HP_CALIB_5_1:
	case RT5663_HP_CALIB_6:
	case RT5663_HP_CALIB_7:
	case RT5663_HP_CALIB_9:
	case RT5663_HP_CALIB_10:
	case RT5663_HP_CALIB_11:
	case RT5663_HP_CALIB_ST1:
	case RT5663_HP_CALIB_ST2:
	case RT5663_HP_CALIB_ST3:
	case RT5663_HP_CALIB_ST4:
	case RT5663_HP_CALIB_ST5:
	case RT5663_HP_CALIB_ST6:
	case RT5663_HP_CALIB_ST7:
	case RT5663_HP_CALIB_ST8:
	case RT5663_HP_CALIB_ST9:
	case RT5663_HP_AMP_DET:
	case RT5663_DUMMY_REG_6:
	case RT5663_HP_BIAS:
	case RT5663_CBJ_1:
	case RT5663_CBJ_2:
	case RT5663_CBJ_3:
	case RT5663_DUMMY_1:
	case RT5663_DUMMY_2:
	case RT5663_DUMMY_3:
	case RT5663_ANA_JD:
	case RT5663_ADC_LCH_LPF1_A1:
	case RT5663_ADC_RCH_LPF1_A1:
	case RT5663_ADC_LCH_LPF1_H0:
	case RT5663_ADC_RCH_LPF1_H0:
	case RT5663_ADC_LCH_BPF1_A1:
	case RT5663_ADC_RCH_BPF1_A1:
	case RT5663_ADC_LCH_BPF1_A2:
	case RT5663_ADC_RCH_BPF1_A2:
	case RT5663_ADC_LCH_BPF1_H0:
	case RT5663_ADC_RCH_BPF1_H0:
	case RT5663_ADC_LCH_BPF2_A1:
	case RT5663_ADC_RCH_BPF2_A1:
	case RT5663_ADC_LCH_BPF2_A2:
	case RT5663_ADC_RCH_BPF2_A2:
	case RT5663_ADC_LCH_BPF2_H0:
	case RT5663_ADC_RCH_BPF2_H0:
	case RT5663_ADC_LCH_BPF3_A1:
	case RT5663_ADC_RCH_BPF3_A1:
	case RT5663_ADC_LCH_BPF3_A2:
	case RT5663_ADC_RCH_BPF3_A2:
	case RT5663_ADC_LCH_BPF3_H0:
	case RT5663_ADC_RCH_BPF3_H0:
	case RT5663_ADC_LCH_BPF4_A1:
	case RT5663_ADC_RCH_BPF4_A1:
	case RT5663_ADC_LCH_BPF4_A2:
	case RT5663_ADC_RCH_BPF4_A2:
	case RT5663_ADC_LCH_BPF4_H0:
	case RT5663_ADC_RCH_BPF4_H0:
	case RT5663_ADC_LCH_HPF1_A1:
	case RT5663_ADC_RCH_HPF1_A1:
	case RT5663_ADC_LCH_HPF1_H0:
	case RT5663_ADC_RCH_HPF1_H0:
	case RT5663_ADC_EQ_PRE_VOL_L:
	case RT5663_ADC_EQ_PRE_VOL_R:
	case RT5663_ADC_EQ_POST_VOL_L:
	case RT5663_ADC_EQ_POST_VOL_R:
		return true;
	default:
		return false;
	}
}

static bool rt5663_v2_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5663_RESET:
	case RT5663_CBJ_TYPE_2:
	case RT5663_PDM_OUT_CTL:
	case RT5663_PDM_I2C_DATA_CTL1:
	case RT5663_PDM_I2C_DATA_CTL4:
	case RT5663_ALC_BK_GAIN:
	case RT5663_PLL_2:
	case RT5663_MICBIAS_1:
	case RT5663_ADC_EQ_1:
	case RT5663_INT_ST_1:
	case RT5663_GPIO_STA2:
	case RT5663_IL_CMD_1:
	case RT5663_IL_CMD_5:
	case RT5663_A_JD_CTRL:
	case RT5663_JD_CTRL2:
	case RT5663_VENDOR_ID:
	case RT5663_VENDOR_ID_1:
	case RT5663_VENDOR_ID_2:
	case RT5663_STO_DRE_1:
	case RT5663_STO_DRE_5:
	case RT5663_STO_DRE_6:
	case RT5663_STO_DRE_7:
	case RT5663_MONO_DYNA_6:
	case RT5663_STO1_SIL_DET:
	case RT5663_MONOL_SIL_DET:
	case RT5663_MONOR_SIL_DET:
	case RT5663_STO2_DAC_SIL:
	case RT5663_MONO_AMP_CAL_ST1:
	case RT5663_MONO_AMP_CAL_ST2:
	case RT5663_MONO_AMP_CAL_ST3:
	case RT5663_MONO_AMP_CAL_ST4:
	case RT5663_HP_IMP_SEN_2:
	case RT5663_HP_IMP_SEN_3:
	case RT5663_HP_IMP_SEN_4:
	case RT5663_HP_IMP_SEN_10:
	case RT5663_HP_CALIB_1:
	case RT5663_HP_CALIB_10:
	case RT5663_HP_CALIB_ST1:
	case RT5663_HP_CALIB_ST4:
	case RT5663_HP_CALIB_ST5:
	case RT5663_HP_CALIB_ST6:
	case RT5663_HP_CALIB_ST7:
	case RT5663_HP_CALIB_ST8:
	case RT5663_HP_CALIB_ST9:
	case RT5663_HP_CALIB_ST10:
	case RT5663_HP_CALIB_ST11:
		return true;
	default:
		return false;
	}
}

static bool rt5663_v2_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5663_LOUT_CTRL:
	case RT5663_HP_AMP_2:
	case RT5663_MONO_OUT:
	case RT5663_MONO_GAIN:
	case RT5663_AEC_BST:
	case RT5663_IN1_IN2:
	case RT5663_IN3_IN4:
	case RT5663_INL1_INR1:
	case RT5663_CBJ_TYPE_2:
	case RT5663_CBJ_TYPE_3:
	case RT5663_CBJ_TYPE_4:
	case RT5663_CBJ_TYPE_5:
	case RT5663_CBJ_TYPE_8:
	case RT5663_DAC3_DIG_VOL:
	case RT5663_DAC3_CTRL:
	case RT5663_MONO_ADC_DIG_VOL:
	case RT5663_STO2_ADC_DIG_VOL:
	case RT5663_MONO_ADC_BST_GAIN:
	case RT5663_STO2_ADC_BST_GAIN:
	case RT5663_SIDETONE_CTRL:
	case RT5663_MONO1_ADC_MIXER:
	case RT5663_STO2_ADC_MIXER:
	case RT5663_MONO_DAC_MIXER:
	case RT5663_DAC2_SRC_CTRL:
	case RT5663_IF_3_4_DATA_CTL:
	case RT5663_IF_5_DATA_CTL:
	case RT5663_PDM_OUT_CTL:
	case RT5663_PDM_I2C_DATA_CTL1:
	case RT5663_PDM_I2C_DATA_CTL2:
	case RT5663_PDM_I2C_DATA_CTL3:
	case RT5663_PDM_I2C_DATA_CTL4:
	case RT5663_RECMIX1_NEW:
	case RT5663_RECMIX1L_0:
	case RT5663_RECMIX1L:
	case RT5663_RECMIX1R_0:
	case RT5663_RECMIX1R:
	case RT5663_RECMIX2_NEW:
	case RT5663_RECMIX2_L_2:
	case RT5663_RECMIX2_R:
	case RT5663_RECMIX2_R_2:
	case RT5663_CALIB_REC_LR:
	case RT5663_ALC_BK_GAIN:
	case RT5663_MONOMIX_GAIN:
	case RT5663_MONOMIX_IN_GAIN:
	case RT5663_OUT_MIXL_GAIN:
	case RT5663_OUT_LMIX_IN_GAIN:
	case RT5663_OUT_RMIX_IN_GAIN:
	case RT5663_OUT_RMIX_IN_GAIN1:
	case RT5663_LOUT_MIXER_CTRL:
	case RT5663_PWR_VOL:
	case RT5663_ADCDAC_RST:
	case RT5663_I2S34_SDP:
	case RT5663_I2S5_SDP:
	case RT5663_TDM_6:
	case RT5663_TDM_7:
	case RT5663_TDM_8:
	case RT5663_TDM_9:
	case RT5663_ASRC_3:
	case RT5663_ASRC_6:
	case RT5663_ASRC_7:
	case RT5663_PLL_TRK_13:
	case RT5663_I2S_M_CLK_CTL:
	case RT5663_FDIV_I2S34_M_CLK:
	case RT5663_FDIV_I2S34_M_CLK2:
	case RT5663_FDIV_I2S5_M_CLK:
	case RT5663_FDIV_I2S5_M_CLK2:
	case RT5663_V2_IRQ_4:
	case RT5663_GPIO_3:
	case RT5663_GPIO_4:
	case RT5663_GPIO_STA2:
	case RT5663_HP_AMP_DET1:
	case RT5663_HP_AMP_DET2:
	case RT5663_HP_AMP_DET3:
	case RT5663_MID_BD_HP_AMP:
	case RT5663_LOW_BD_HP_AMP:
	case RT5663_SOF_VOL_ZC2:
	case RT5663_ADC_STO2_ADJ1:
	case RT5663_ADC_STO2_ADJ2:
	case RT5663_A_JD_CTRL:
	case RT5663_JD1_TRES_CTRL:
	case RT5663_JD2_TRES_CTRL:
	case RT5663_V2_JD_CTRL2:
	case RT5663_DUM_REG_2:
	case RT5663_DUM_REG_3:
	case RT5663_VENDOR_ID:
	case RT5663_VENDOR_ID_1:
	case RT5663_VENDOR_ID_2:
	case RT5663_DACADC_DIG_VOL2:
	case RT5663_DIG_IN_PIN2:
	case RT5663_PAD_DRV_CTL1:
	case RT5663_SOF_RAM_DEPOP:
	case RT5663_VOL_TEST:
	case RT5663_TEST_MODE_4:
	case RT5663_TEST_MODE_5:
	case RT5663_STO_DRE_9:
	case RT5663_MONO_DYNA_1:
	case RT5663_MONO_DYNA_2:
	case RT5663_MONO_DYNA_3:
	case RT5663_MONO_DYNA_4:
	case RT5663_MONO_DYNA_5:
	case RT5663_MONO_DYNA_6:
	case RT5663_STO1_SIL_DET:
	case RT5663_MONOL_SIL_DET:
	case RT5663_MONOR_SIL_DET:
	case RT5663_STO2_DAC_SIL:
	case RT5663_PWR_SAV_CTL1:
	case RT5663_PWR_SAV_CTL2:
	case RT5663_PWR_SAV_CTL3:
	case RT5663_PWR_SAV_CTL4:
	case RT5663_PWR_SAV_CTL5:
	case RT5663_PWR_SAV_CTL6:
	case RT5663_MONO_AMP_CAL1:
	case RT5663_MONO_AMP_CAL2:
	case RT5663_MONO_AMP_CAL3:
	case RT5663_MONO_AMP_CAL4:
	case RT5663_MONO_AMP_CAL5:
	case RT5663_MONO_AMP_CAL6:
	case RT5663_MONO_AMP_CAL7:
	case RT5663_MONO_AMP_CAL_ST1:
	case RT5663_MONO_AMP_CAL_ST2:
	case RT5663_MONO_AMP_CAL_ST3:
	case RT5663_MONO_AMP_CAL_ST4:
	case RT5663_MONO_AMP_CAL_ST5:
	case RT5663_V2_HP_IMP_SEN_13:
	case RT5663_V2_HP_IMP_SEN_14:
	case RT5663_V2_HP_IMP_SEN_6:
	case RT5663_V2_HP_IMP_SEN_7:
	case RT5663_V2_HP_IMP_SEN_8:
	case RT5663_V2_HP_IMP_SEN_9:
	case RT5663_V2_HP_IMP_SEN_10:
	case RT5663_HP_LOGIC_3:
	case RT5663_HP_CALIB_ST10:
	case RT5663_HP_CALIB_ST11:
	case RT5663_PRO_REG_TBL_4:
	case RT5663_PRO_REG_TBL_5:
	case RT5663_PRO_REG_TBL_6:
	case RT5663_PRO_REG_TBL_7:
	case RT5663_PRO_REG_TBL_8:
	case RT5663_PRO_REG_TBL_9:
	case RT5663_SAR_ADC_INL_1:
	case RT5663_SAR_ADC_INL_2:
	case RT5663_SAR_ADC_INL_3:
	case RT5663_SAR_ADC_INL_4:
	case RT5663_SAR_ADC_INL_5:
	case RT5663_SAR_ADC_INL_6:
	case RT5663_SAR_ADC_INL_7:
	case RT5663_SAR_ADC_INL_8:
	case RT5663_SAR_ADC_INL_9:
	case RT5663_SAR_ADC_INL_10:
	case RT5663_SAR_ADC_INL_11:
	case RT5663_SAR_ADC_INL_12:
	case RT5663_DRC_CTRL_1:
	case RT5663_DRC1_CTRL_2:
	case RT5663_DRC1_CTRL_3:
	case RT5663_DRC1_CTRL_4:
	case RT5663_DRC1_CTRL_5:
	case RT5663_DRC1_CTRL_6:
	case RT5663_DRC1_HD_CTRL_1:
	case RT5663_DRC1_HD_CTRL_2:
	case RT5663_DRC1_PRI_REG_1:
	case RT5663_DRC1_PRI_REG_2:
	case RT5663_DRC1_PRI_REG_3:
	case RT5663_DRC1_PRI_REG_4:
	case RT5663_DRC1_PRI_REG_5:
	case RT5663_DRC1_PRI_REG_6:
	case RT5663_DRC1_PRI_REG_7:
	case RT5663_DRC1_PRI_REG_8:
	case RT5663_ALC_PGA_CTL_1:
	case RT5663_ALC_PGA_CTL_2:
	case RT5663_ALC_PGA_CTL_3:
	case RT5663_ALC_PGA_CTL_4:
	case RT5663_ALC_PGA_CTL_5:
	case RT5663_ALC_PGA_CTL_6:
	case RT5663_ALC_PGA_CTL_7:
	case RT5663_ALC_PGA_CTL_8:
	case RT5663_ALC_PGA_REG_1:
	case RT5663_ALC_PGA_REG_2:
	case RT5663_ALC_PGA_REG_3:
	case RT5663_ADC_EQ_RECOV_1:
	case RT5663_ADC_EQ_RECOV_2:
	case RT5663_ADC_EQ_RECOV_3:
	case RT5663_ADC_EQ_RECOV_4:
	case RT5663_ADC_EQ_RECOV_5:
	case RT5663_ADC_EQ_RECOV_6:
	case RT5663_ADC_EQ_RECOV_7:
	case RT5663_ADC_EQ_RECOV_8:
	case RT5663_ADC_EQ_RECOV_9:
	case RT5663_ADC_EQ_RECOV_10:
	case RT5663_ADC_EQ_RECOV_11:
	case RT5663_ADC_EQ_RECOV_12:
	case RT5663_ADC_EQ_RECOV_13:
	case RT5663_VID_HIDDEN:
	case RT5663_VID_CUSTOMER:
	case RT5663_SCAN_MODE:
	case RT5663_I2C_BYPA:
		return true;
	case RT5663_TDM_1:
	case RT5663_DEPOP_3:
	case RT5663_ASRC_11_2:
	case RT5663_INT_ST_2:
	case RT5663_GPIO_STA1:
	case RT5663_SIN_GEN_1:
	case RT5663_SIN_GEN_2:
	case RT5663_SIN_GEN_3:
	case RT5663_IL_CMD_PWRSAV1:
	case RT5663_IL_CMD_PWRSAV2:
	case RT5663_EM_JACK_TYPE_1:
	case RT5663_EM_JACK_TYPE_2:
	case RT5663_EM_JACK_TYPE_3:
	case RT5663_EM_JACK_TYPE_4:
	case RT5663_FAST_OFF_MICBIAS:
	case RT5663_ANA_BIAS_CUR_1:
	case RT5663_ANA_BIAS_CUR_2:
	case RT5663_BIAS_CUR_9:
	case RT5663_DUMMY_REG_4:
	case RT5663_VREF_RECMIX:
	case RT5663_CHARGE_PUMP_1_2:
	case RT5663_CHARGE_PUMP_1_3:
	case RT5663_CHARGE_PUMP_2:
	case RT5663_CHOP_DAC_R:
	case RT5663_DUMMY_CTL_DACLR:
	case RT5663_DUMMY_REG_5:
	case RT5663_SOFT_RAMP:
	case RT5663_TEST_MODE_1:
	case RT5663_STO_DRE_10:
	case RT5663_MIC_DECRO_1:
	case RT5663_MIC_DECRO_2:
	case RT5663_MIC_DECRO_3:
	case RT5663_MIC_DECRO_4:
	case RT5663_MIC_DECRO_5:
	case RT5663_MIC_DECRO_6:
	case RT5663_HP_DECRO_1:
	case RT5663_HP_DECRO_2:
	case RT5663_HP_DECRO_3:
	case RT5663_HP_DECRO_4:
	case RT5663_HP_DECOUP:
	case RT5663_HP_IMPSEN_MAP4:
	case RT5663_HP_IMPSEN_MAP5:
	case RT5663_HP_IMPSEN_MAP7:
	case RT5663_HP_CALIB_1:
	case RT5663_CBJ_1:
	case RT5663_CBJ_2:
	case RT5663_CBJ_3:
		return false;
	default:
		return rt5663_readable_register(dev, reg);
	}
}

static const DECLARE_TLV_DB_SCALE(rt5663_hp_vol_tlv, -2400, 150, 0);
static const DECLARE_TLV_DB_SCALE(rt5663_v2_hp_vol_tlv, -2250, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -1725, 75, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static const DECLARE_TLV_DB_RANGE(in_bst_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0)
);

/* Interface data select */
static const char * const rt5663_if1_adc_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt5663_if1_adc_enum, RT5663_TDM_2,
	RT5663_DATA_SWAP_ADCDAT1_SHIFT, rt5663_if1_adc_data_select);

static void rt5663_enable_push_button_irq(struct snd_soc_codec *codec,
	bool enable)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	if (enable) {
		snd_soc_update_bits(codec, RT5663_IL_CMD_6,
			RT5663_EN_4BTN_INL_MASK, RT5663_EN_4BTN_INL_EN);
		/* reset in-line command */
		snd_soc_update_bits(codec, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_RESET);
		snd_soc_update_bits(codec, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_NOR);
		switch (rt5663->codec_ver) {
		case CODEC_VER_1:
			snd_soc_update_bits(codec, RT5663_IRQ_3,
				RT5663_V2_EN_IRQ_INLINE_MASK,
				RT5663_V2_EN_IRQ_INLINE_NOR);
			break;
		case CODEC_VER_0:
			snd_soc_update_bits(codec, RT5663_IRQ_2,
				RT5663_EN_IRQ_INLINE_MASK,
				RT5663_EN_IRQ_INLINE_NOR);
			break;
		default:
			dev_err(codec->dev, "Unknown CODEC Version\n");
		}
	} else {
		switch (rt5663->codec_ver) {
		case CODEC_VER_1:
			snd_soc_update_bits(codec, RT5663_IRQ_3,
				RT5663_V2_EN_IRQ_INLINE_MASK,
				RT5663_V2_EN_IRQ_INLINE_BYP);
			break;
		case CODEC_VER_0:
			snd_soc_update_bits(codec, RT5663_IRQ_2,
				RT5663_EN_IRQ_INLINE_MASK,
				RT5663_EN_IRQ_INLINE_BYP);
			break;
		default:
			dev_err(codec->dev, "Unknown CODEC Version\n");
		}
		snd_soc_update_bits(codec, RT5663_IL_CMD_6,
			RT5663_EN_4BTN_INL_MASK, RT5663_EN_4BTN_INL_DIS);
		/* reset in-line command */
		snd_soc_update_bits(codec, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_RESET);
		snd_soc_update_bits(codec, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_NOR);
	}
}

/**
 * rt5663_v2_jack_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */

static int rt5663_v2_jack_detect(struct snd_soc_codec *codec, int jack_insert)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	int val, i = 0, sleep_time[5] = {300, 150, 100, 50, 30};

	dev_dbg(codec->dev, "%s jack_insert:%d\n", __func__, jack_insert);
	if (jack_insert) {
		snd_soc_write(codec, RT5663_CBJ_TYPE_2, 0x8040);
		snd_soc_write(codec, RT5663_CBJ_TYPE_3, 0x1484);

		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS2");
		snd_soc_dapm_force_enable_pin(dapm, "Mic Det Power");
		snd_soc_dapm_force_enable_pin(dapm, "CBJ Power");
		snd_soc_dapm_sync(dapm);
		snd_soc_update_bits(codec, RT5663_RC_CLK,
			RT5663_DIG_1M_CLK_MASK, RT5663_DIG_1M_CLK_EN);
		snd_soc_update_bits(codec, RT5663_RECMIX, 0x8, 0x8);

		while (i < 5) {
			msleep(sleep_time[i]);
			val = snd_soc_read(codec, RT5663_CBJ_TYPE_2) & 0x0003;
			if (val == 0x1 || val == 0x2 || val == 0x3)
				break;
			dev_dbg(codec->dev, "%s: MX-0011 val=%x sleep %d\n",
				__func__, val, sleep_time[i]);
			i++;
		}
		dev_dbg(codec->dev, "%s val = %d\n", __func__, val);
		switch (val) {
		case 1:
		case 2:
			rt5663->jack_type = SND_JACK_HEADSET;
			rt5663_enable_push_button_irq(codec, true);
			break;
		default:
			snd_soc_dapm_disable_pin(dapm, "MICBIAS1");
			snd_soc_dapm_disable_pin(dapm, "MICBIAS2");
			snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
			snd_soc_dapm_disable_pin(dapm, "CBJ Power");
			snd_soc_dapm_sync(dapm);
			rt5663->jack_type = SND_JACK_HEADPHONE;
			break;
		}
	} else {
		snd_soc_update_bits(codec, RT5663_RECMIX, 0x8, 0x0);

		if (rt5663->jack_type == SND_JACK_HEADSET) {
			rt5663_enable_push_button_irq(codec, false);
			snd_soc_dapm_disable_pin(dapm, "MICBIAS1");
			snd_soc_dapm_disable_pin(dapm, "MICBIAS2");
			snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
			snd_soc_dapm_disable_pin(dapm, "CBJ Power");
			snd_soc_dapm_sync(dapm);
		}
		rt5663->jack_type = 0;
	}

	dev_dbg(codec->dev, "jack_type = %d\n", rt5663->jack_type);
	return rt5663->jack_type;
}

/**
 * rt5663_jack_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */
static int rt5663_jack_detect(struct snd_soc_codec *codec, int jack_insert)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	int val, i = 0, sleep_time[5] = {300, 150, 100, 50, 30};

	dev_dbg(codec->dev, "%s jack_insert:%d\n", __func__, jack_insert);

	if (jack_insert) {
		snd_soc_update_bits(codec, RT5663_DIG_MISC,
			RT5663_DIG_GATE_CTRL_MASK, RT5663_DIG_GATE_CTRL_EN);
		snd_soc_update_bits(codec, RT5663_HP_CHARGE_PUMP_1,
			RT5663_SI_HP_MASK | RT5663_OSW_HP_L_MASK |
			RT5663_OSW_HP_R_MASK, RT5663_SI_HP_EN |
			RT5663_OSW_HP_L_DIS | RT5663_OSW_HP_R_DIS);
		snd_soc_update_bits(codec, RT5663_DUMMY_1,
			RT5663_EMB_CLK_MASK | RT5663_HPA_CPL_BIAS_MASK |
			RT5663_HPA_CPR_BIAS_MASK, RT5663_EMB_CLK_EN |
			RT5663_HPA_CPL_BIAS_1 | RT5663_HPA_CPR_BIAS_1);
		snd_soc_update_bits(codec, RT5663_CBJ_1,
			RT5663_INBUF_CBJ_BST1_MASK | RT5663_CBJ_SENSE_BST1_MASK,
			RT5663_INBUF_CBJ_BST1_ON | RT5663_CBJ_SENSE_BST1_L);
		snd_soc_update_bits(codec, RT5663_IL_CMD_2,
			RT5663_PWR_MIC_DET_MASK, RT5663_PWR_MIC_DET_ON);
		/* BST1 power on for JD */
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_2,
			RT5663_PWR_BST1_MASK, RT5663_PWR_BST1_ON);
		snd_soc_update_bits(codec, RT5663_EM_JACK_TYPE_1,
			RT5663_CBJ_DET_MASK | RT5663_EXT_JD_MASK |
			RT5663_POL_EXT_JD_MASK, RT5663_CBJ_DET_EN |
			RT5663_EXT_JD_EN | RT5663_POL_EXT_JD_EN);
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_1,
			RT5663_PWR_MB_MASK | RT5663_LDO1_DVO_MASK |
			RT5663_AMP_HP_MASK, RT5663_PWR_MB |
			RT5663_LDO1_DVO_0_9V | RT5663_AMP_HP_3X);
		snd_soc_update_bits(codec, RT5663_AUTO_1MRC_CLK,
			RT5663_IRQ_POW_SAV_MASK, RT5663_IRQ_POW_SAV_EN);
		snd_soc_update_bits(codec, RT5663_IRQ_1,
			RT5663_EN_IRQ_JD1_MASK, RT5663_EN_IRQ_JD1_EN);
		while (i < 5) {
			msleep(sleep_time[i]);
			val = snd_soc_read(codec, RT5663_EM_JACK_TYPE_2) &
				0x0003;
			dev_dbg(codec->dev, "%s: MX-00e7 val=%x sleep %d\n",
				__func__, val, sleep_time[i]);
			i++;
			if (val == 0x1 || val == 0x2 || val == 0x3)
				break;
		}
		dev_dbg(codec->dev, "%s val = %d\n", __func__, val);
		switch (val) {
		case 1:
		case 2:
			rt5663->jack_type = SND_JACK_HEADSET;
			rt5663_enable_push_button_irq(codec, true);
			break;
		default:
			rt5663->jack_type = SND_JACK_HEADPHONE;
			break;
		}
	} else {
		if (rt5663->jack_type == SND_JACK_HEADSET)
			rt5663_enable_push_button_irq(codec, false);
		rt5663->jack_type = 0;
	}

	dev_dbg(codec->dev, "jack_type = %d\n", rt5663->jack_type);
	return rt5663->jack_type;
}

static int rt5663_button_detect(struct snd_soc_codec *codec)
{
	int btn_type, val;

	val = snd_soc_read(codec, RT5663_IL_CMD_5);
	dev_dbg(codec->dev, "%s: val=0x%x\n", __func__, val);
	btn_type = val & 0xfff0;
	snd_soc_write(codec, RT5663_IL_CMD_5, val);

	return btn_type;
}

static irqreturn_t rt5663_irq(int irq, void *data)
{
	struct rt5663_priv *rt5663 = data;

	dev_dbg(rt5663->codec->dev, "%s IRQ queue work\n", __func__);

	queue_delayed_work(system_wq, &rt5663->jack_detect_work,
		msecs_to_jiffies(250));

	return IRQ_HANDLED;
}

int rt5663_set_jack_detect(struct snd_soc_codec *codec,
	struct snd_soc_jack *hs_jack)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	rt5663->hs_jack = hs_jack;

	rt5663_irq(0, rt5663);

	return 0;
}
EXPORT_SYMBOL_GPL(rt5663_set_jack_detect);

static bool rt5663_check_jd_status(struct snd_soc_codec *codec)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	int val = snd_soc_read(codec, RT5663_INT_ST_1);

	dev_dbg(codec->dev, "%s val=%x\n", __func__, val);

	/* JD1 */
	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		return !(val & 0x2000);
	case CODEC_VER_0:
		return !(val & 0x1000);
	default:
		dev_err(codec->dev, "Unknown CODEC Version\n");
	}

	return false;
}

static void rt5663_jack_detect_work(struct work_struct *work)
{
	struct rt5663_priv *rt5663 =
		container_of(work, struct rt5663_priv, jack_detect_work.work);
	struct snd_soc_codec *codec = rt5663->codec;
	int btn_type, report = 0;

	if (!codec)
		return;

	if (rt5663_check_jd_status(codec)) {
		/* jack in */
		if (rt5663->jack_type == 0) {
			/* jack was out, report jack type */
			switch (rt5663->codec_ver) {
			case CODEC_VER_1:
				report = rt5663_v2_jack_detect(
						rt5663->codec, 1);
				break;
			case CODEC_VER_0:
				report = rt5663_jack_detect(rt5663->codec, 1);
				break;
			default:
				dev_err(codec->dev, "Unknown CODEC Version\n");
			}
		} else {
			/* jack is already in, report button event */
			report = SND_JACK_HEADSET;
			btn_type = rt5663_button_detect(rt5663->codec);
			/**
			 * rt5663 can report three kinds of button behavior,
			 * one click, double click and hold. However,
			 * currently we will report button pressed/released
			 * event. So all the three button behaviors are
			 * treated as button pressed.
			 */
			switch (btn_type) {
			case 0x8000:
			case 0x4000:
			case 0x2000:
				report |= SND_JACK_BTN_0;
				break;
			case 0x1000:
			case 0x0800:
			case 0x0400:
				report |= SND_JACK_BTN_1;
				break;
			case 0x0200:
			case 0x0100:
			case 0x0080:
				report |= SND_JACK_BTN_2;
				break;
			case 0x0040:
			case 0x0020:
			case 0x0010:
				report |= SND_JACK_BTN_3;
				break;
			case 0x0000: /* unpressed */
				break;
			default:
				btn_type = 0;
				dev_err(rt5663->codec->dev,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}
			/* button release or spurious interrput*/
			if (btn_type == 0)
				report =  rt5663->jack_type;
		}
	} else {
		/* jack out */
		switch (rt5663->codec_ver) {
		case CODEC_VER_1:
			report = rt5663_v2_jack_detect(rt5663->codec, 0);
			break;
		case CODEC_VER_0:
			report = rt5663_jack_detect(rt5663->codec, 0);
			break;
		default:
			dev_err(codec->dev, "Unknown CODEC Version\n");
		}
	}
	dev_dbg(codec->dev, "%s jack report: 0x%04x\n", __func__, report);
	snd_soc_jack_report(rt5663->hs_jack, report, SND_JACK_HEADSET |
			    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			    SND_JACK_BTN_2 | SND_JACK_BTN_3);
}

static const struct snd_kcontrol_new rt5663_snd_controls[] = {
	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC Playback Volume", RT5663_STO1_DAC_DIG_VOL,
		RT5663_DAC_L1_VOL_SHIFT + 1, RT5663_DAC_R1_VOL_SHIFT + 1,
		87, 0, dac_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5663_STO1_ADC_DIG_VOL,
		RT5663_ADC_L_MUTE_SHIFT, RT5663_ADC_R_MUTE_SHIFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5663_STO1_ADC_DIG_VOL,
		RT5663_ADC_L_VOL_SHIFT + 1, RT5663_ADC_R_VOL_SHIFT + 1,
		63, 0, adc_vol_tlv),
};

static const struct snd_kcontrol_new rt5663_v2_specific_controls[] = {
	/* Headphone Output Volume */
	SOC_DOUBLE_R_TLV("Headphone Playback Volume", RT5663_HP_LCH_DRE,
		RT5663_HP_RCH_DRE, RT5663_GAIN_HP_SHIFT, 15, 1,
		rt5663_v2_hp_vol_tlv),
	/* Mic Boost Volume */
	SOC_SINGLE_TLV("IN1 Capture Volume", RT5663_AEC_BST,
		RT5663_GAIN_CBJ_SHIFT, 8, 0, in_bst_tlv),
};

static const struct snd_kcontrol_new rt5663_specific_controls[] = {
	/* Headphone Output Volume */
	SOC_DOUBLE_R_TLV("Headphone Playback Volume", RT5663_STO_DRE_9,
		RT5663_STO_DRE_10, RT5663_DRE_GAIN_HP_SHIFT, 23, 1,
		rt5663_hp_vol_tlv),
	/* Mic Boost Volume*/
	SOC_SINGLE_TLV("IN1 Capture Volume", RT5663_CBJ_2,
		RT5663_GAIN_BST1_SHIFT, 8, 0, in_bst_tlv),
	/* Data Swap for Slot0/1 in ADCDAT1 */
	SOC_ENUM("IF1 ADC Data Swap", rt5663_if1_adc_enum),
};

static int rt5663_is_sys_clk_from_pll(struct snd_soc_dapm_widget *w,
	struct snd_soc_dapm_widget *sink)
{
	unsigned int val;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	val = snd_soc_read(codec, RT5663_GLB_CLK);
	val &= RT5663_SCLK_SRC_MASK;
	if (val == RT5663_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

static int rt5663_is_using_asrc(struct snd_soc_dapm_widget *w,
	struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, shift, val;
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	if (rt5663->codec_ver == CODEC_VER_1) {
		switch (w->shift) {
		case RT5663_ADC_STO1_ASRC_SHIFT:
			reg = RT5663_ASRC_3;
			shift = RT5663_V2_AD_STO1_TRACK_SHIFT;
			break;
		case RT5663_DAC_STO1_ASRC_SHIFT:
			reg = RT5663_ASRC_2;
			shift = RT5663_DA_STO1_TRACK_SHIFT;
			break;
		default:
			return 0;
		}
	} else {
		switch (w->shift) {
		case RT5663_ADC_STO1_ASRC_SHIFT:
			reg = RT5663_ASRC_2;
			shift = RT5663_AD_STO1_TRACK_SHIFT;
			break;
		case RT5663_DAC_STO1_ASRC_SHIFT:
			reg = RT5663_ASRC_2;
			shift = RT5663_DA_STO1_TRACK_SHIFT;
			break;
		default:
			return 0;
		}
	}

	val = (snd_soc_read(codec, reg) >> shift) & 0x7;

	if (val)
		return 1;

	return 0;
}

static int rt5663_i2s_use_asrc(struct snd_soc_dapm_widget *source,
	struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(source->dapm);
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	int da_asrc_en, ad_asrc_en;

	da_asrc_en = (snd_soc_read(codec, RT5663_ASRC_2) &
		RT5663_DA_STO1_TRACK_MASK) ? 1 : 0;
	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		ad_asrc_en = (snd_soc_read(codec, RT5663_ASRC_3) &
			RT5663_V2_AD_STO1_TRACK_MASK) ? 1 : 0;
		break;
	case CODEC_VER_0:
		ad_asrc_en = (snd_soc_read(codec, RT5663_ASRC_2) &
			RT5663_AD_STO1_TRACK_MASK) ? 1 : 0;
		break;
	default:
		dev_err(codec->dev, "Unknown CODEC Version\n");
		return 1;
	}

	if (da_asrc_en || ad_asrc_en)
		if (rt5663->sysclk > rt5663->lrck * 384)
			return 1;

	dev_err(codec->dev, "sysclk < 384 x fs, disable i2s asrc\n");

	return 0;
}

/**
 * rt5663_sel_asrc_clk_src - select ASRC clock source for a set of filters
 * @codec: SoC audio codec device.
 * @filter_mask: mask of filters.
 * @clk_src: clock source
 *
 * The ASRC function is for asynchronous MCLK and LRCK. Also, since RT5663 can
 * only support standard 32fs or 64fs i2s format, ASRC should be enabled to
 * support special i2s clock format such as Intel's 100fs(100 * sampling rate).
 * ASRC function will track i2s clock and generate a corresponding system clock
 * for codec. This function provides an API to select the clock source for a
 * set of filters specified by the mask. And the codec driver will turn on ASRC
 * for these filters if ASRC is selected as their clock source.
 */
int rt5663_sel_asrc_clk_src(struct snd_soc_codec *codec,
		unsigned int filter_mask, unsigned int clk_src)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	unsigned int asrc2_mask = 0;
	unsigned int asrc2_value = 0;
	unsigned int asrc3_mask = 0;
	unsigned int asrc3_value = 0;

	switch (clk_src) {
	case RT5663_CLK_SEL_SYS:
	case RT5663_CLK_SEL_I2S1_ASRC:
		break;

	default:
		return -EINVAL;
	}

	if (filter_mask & RT5663_DA_STEREO_FILTER) {
		asrc2_mask |= RT5663_DA_STO1_TRACK_MASK;
		asrc2_value |= clk_src << RT5663_DA_STO1_TRACK_SHIFT;
	}

	if (filter_mask & RT5663_AD_STEREO_FILTER) {
		switch (rt5663->codec_ver) {
		case CODEC_VER_1:
			asrc3_mask |= RT5663_V2_AD_STO1_TRACK_MASK;
			asrc3_value |= clk_src << RT5663_V2_AD_STO1_TRACK_SHIFT;
			break;
		case CODEC_VER_0:
			asrc2_mask |= RT5663_AD_STO1_TRACK_MASK;
			asrc2_value |= clk_src << RT5663_AD_STO1_TRACK_SHIFT;
			break;
		default:
			dev_err(codec->dev, "Unknown CODEC Version\n");
		}
	}

	if (asrc2_mask)
		snd_soc_update_bits(codec, RT5663_ASRC_2, asrc2_mask,
			asrc2_value);

	if (asrc3_mask)
		snd_soc_update_bits(codec, RT5663_ASRC_3, asrc3_mask,
			asrc3_value);

	return 0;
}
EXPORT_SYMBOL_GPL(rt5663_sel_asrc_clk_src);

/* Analog Mixer */
static const struct snd_kcontrol_new rt5663_recmix1l[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5663_RECMIX1L,
		RT5663_RECMIX1L_BST2_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 CBJ Switch", RT5663_RECMIX1L,
		RT5663_RECMIX1L_BST1_CBJ_SHIFT, 1, 1),
};

static const struct snd_kcontrol_new rt5663_recmix1r[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5663_RECMIX1R,
		RT5663_RECMIX1R_BST2_SHIFT, 1, 1),
};

/* Digital Mixer */
static const struct snd_kcontrol_new rt5663_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5663_STO1_ADC_MIXER,
			RT5663_M_STO1_ADC_L1_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5663_STO1_ADC_MIXER,
			RT5663_M_STO1_ADC_L2_SHIFT, 1, 1),
};

static const struct snd_kcontrol_new rt5663_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5663_STO1_ADC_MIXER,
			RT5663_M_STO1_ADC_R1_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5663_STO1_ADC_MIXER,
			RT5663_M_STO1_ADC_R2_SHIFT, 1, 1),
};

static const struct snd_kcontrol_new rt5663_adda_l_mix[] = {
	SOC_DAPM_SINGLE("ADC L Switch", RT5663_AD_DA_MIXER,
			RT5663_M_ADCMIX_L_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L Switch", RT5663_AD_DA_MIXER,
			RT5663_M_DAC1_L_SHIFT, 1, 1),
};

static const struct snd_kcontrol_new rt5663_adda_r_mix[] = {
	SOC_DAPM_SINGLE("ADC R Switch", RT5663_AD_DA_MIXER,
			RT5663_M_ADCMIX_R_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R Switch", RT5663_AD_DA_MIXER,
			RT5663_M_DAC1_R_SHIFT, 1, 1),
};

static const struct snd_kcontrol_new rt5663_sto1_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L Switch", RT5663_STO_DAC_MIXER,
			RT5663_M_DAC_L1_STO_L_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R Switch", RT5663_STO_DAC_MIXER,
			RT5663_M_DAC_R1_STO_L_SHIFT, 1, 1),
};

static const struct snd_kcontrol_new rt5663_sto1_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L Switch", RT5663_STO_DAC_MIXER,
			RT5663_M_DAC_L1_STO_R_SHIFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R Switch", RT5663_STO_DAC_MIXER,
			RT5663_M_DAC_R1_STO_R_SHIFT, 1, 1),
};

/* Out Switch */
static const struct snd_kcontrol_new rt5663_hpo_switch =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5663_HP_AMP_2,
		RT5663_EN_DAC_HPO_SHIFT, 1, 0);

/* Stereo ADC source */
static const char * const rt5663_sto1_adc_src[] = {
	"ADC L", "ADC R"
};

static SOC_ENUM_SINGLE_DECL(rt5663_sto1_adcl_enum, RT5663_STO1_ADC_MIXER,
	RT5663_STO1_ADC_L_SRC_SHIFT, rt5663_sto1_adc_src);

static const struct snd_kcontrol_new rt5663_sto1_adcl_mux =
	SOC_DAPM_ENUM("STO1 ADC L Mux", rt5663_sto1_adcl_enum);

static SOC_ENUM_SINGLE_DECL(rt5663_sto1_adcr_enum, RT5663_STO1_ADC_MIXER,
	RT5663_STO1_ADC_R_SRC_SHIFT, rt5663_sto1_adc_src);

static const struct snd_kcontrol_new rt5663_sto1_adcr_mux =
	SOC_DAPM_ENUM("STO1 ADC R Mux", rt5663_sto1_adcr_enum);

/* RT5663: Analog DACL1 input source */
static const char * const rt5663_alg_dacl_src[] = {
	"DAC L", "STO DAC MIXL"
};

static SOC_ENUM_SINGLE_DECL(rt5663_alg_dacl_enum, RT5663_BYPASS_STO_DAC,
	RT5663_DACL1_SRC_SHIFT, rt5663_alg_dacl_src);

static const struct snd_kcontrol_new rt5663_alg_dacl_mux =
	SOC_DAPM_ENUM("DAC L Mux", rt5663_alg_dacl_enum);

/* RT5663: Analog DACR1 input source */
static const char * const rt5663_alg_dacr_src[] = {
	"DAC R", "STO DAC MIXR"
};

static SOC_ENUM_SINGLE_DECL(rt5663_alg_dacr_enum, RT5663_BYPASS_STO_DAC,
	RT5663_DACR1_SRC_SHIFT, rt5663_alg_dacr_src);

static const struct snd_kcontrol_new rt5663_alg_dacr_mux =
	SOC_DAPM_ENUM("DAC R Mux", rt5663_alg_dacr_enum);

static int rt5663_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (rt5663->codec_ver == CODEC_VER_1) {
			snd_soc_update_bits(codec, RT5663_HP_CHARGE_PUMP_1,
				RT5663_SEL_PM_HP_SHIFT, RT5663_SEL_PM_HP_HIGH);
			snd_soc_update_bits(codec, RT5663_HP_LOGIC_2,
				RT5663_HP_SIG_SRC1_MASK,
				RT5663_HP_SIG_SRC1_SILENCE);
		} else {
			snd_soc_write(codec, RT5663_DEPOP_2, 0x3003);
			snd_soc_update_bits(codec, RT5663_DEPOP_1, 0x000b,
				0x000b);
			snd_soc_update_bits(codec, RT5663_DEPOP_1, 0x0030,
				0x0030);
			snd_soc_update_bits(codec, RT5663_HP_CHARGE_PUMP_1,
				RT5663_OVCD_HP_MASK, RT5663_OVCD_HP_DIS);
			snd_soc_write(codec, RT5663_HP_CHARGE_PUMP_2, 0x1371);
			snd_soc_write(codec, RT5663_HP_BIAS, 0xabba);
			snd_soc_write(codec, RT5663_CHARGE_PUMP_1, 0x2224);
			snd_soc_write(codec, RT5663_ANA_BIAS_CUR_1, 0x7766);
			snd_soc_write(codec, RT5663_HP_BIAS, 0xafaa);
			snd_soc_write(codec, RT5663_CHARGE_PUMP_2, 0x7777);
			snd_soc_update_bits(codec, RT5663_DEPOP_1, 0x3000,
				0x3000);
		}
		break;

	case SND_SOC_DAPM_PRE_PMD:
		if (rt5663->codec_ver == CODEC_VER_1) {
			snd_soc_update_bits(codec, RT5663_HP_LOGIC_2,
				RT5663_HP_SIG_SRC1_MASK,
				RT5663_HP_SIG_SRC1_REG);
		} else {
			snd_soc_update_bits(codec, RT5663_DEPOP_1, 0x3000, 0x0);
			snd_soc_update_bits(codec, RT5663_HP_CHARGE_PUMP_1,
				RT5663_OVCD_HP_MASK, RT5663_OVCD_HP_EN);
			snd_soc_update_bits(codec, RT5663_DEPOP_1, 0x0030, 0x0);
			snd_soc_update_bits(codec, RT5663_DEPOP_1, 0x000b,
				0x000b);
		}
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5663_bst2_power(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_2,
			RT5663_PWR_BST2_MASK | RT5663_PWR_BST2_OP_MASK,
			RT5663_PWR_BST2 | RT5663_PWR_BST2_OP);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_2,
			RT5663_PWR_BST2_MASK | RT5663_PWR_BST2_OP_MASK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5663_pre_div_power(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_write(codec, RT5663_PRE_DIV_GATING_1, 0xff00);
		snd_soc_write(codec, RT5663_PRE_DIV_GATING_2, 0xfffc);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_write(codec, RT5663_PRE_DIV_GATING_1, 0x0000);
		snd_soc_write(codec, RT5663_PRE_DIV_GATING_2, 0x0000);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5663_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL", RT5663_PWR_ANLG_3, RT5663_PWR_PLL_SHIFT, 0,
		NULL, 0),

	/* micbias */
	SND_SOC_DAPM_MICBIAS("MICBIAS1", RT5663_PWR_ANLG_2,
		RT5663_PWR_MB1_SHIFT, 0),
	SND_SOC_DAPM_MICBIAS("MICBIAS2", RT5663_PWR_ANLG_2,
		RT5663_PWR_MB2_SHIFT, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),

	/* REC Mixer Power */
	SND_SOC_DAPM_SUPPLY("RECMIX1L Power", RT5663_PWR_ANLG_2,
		RT5663_PWR_RECMIX1_SHIFT, 0, NULL, 0),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("ADC L Power", RT5663_PWR_DIG_1,
		RT5663_PWR_ADC_L1_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Clock", RT5663_CHOP_ADC,
		RT5663_CKGEN_ADCC_SHIFT, 0, NULL, 0),

	/* ADC Mixer */
	SND_SOC_DAPM_MIXER("STO1 ADC MIXL", SND_SOC_NOPM,
		0, 0, rt5663_sto1_adc_l_mix,
		ARRAY_SIZE(rt5663_sto1_adc_l_mix)),

	/* ADC Filter Power */
	SND_SOC_DAPM_SUPPLY("STO1 ADC Filter", RT5663_PWR_DIG_2,
		RT5663_PWR_ADC_S1F_SHIFT, 0, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S", RT5663_PWR_DIG_1, RT5663_PWR_I2S1_SHIFT, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA("IF DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF ADC", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIFRX", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIFTX", "AIF Capture", 0, SND_SOC_NOPM, 0, 0),

	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("ADDA MIXL", SND_SOC_NOPM, 0, 0, rt5663_adda_l_mix,
		ARRAY_SIZE(rt5663_adda_l_mix)),
	SND_SOC_DAPM_MIXER("ADDA MIXR", SND_SOC_NOPM, 0, 0, rt5663_adda_r_mix,
		ARRAY_SIZE(rt5663_adda_r_mix)),
	SND_SOC_DAPM_PGA("DAC L1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC R1", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("STO1 DAC Filter", RT5663_PWR_DIG_2,
		RT5663_PWR_DAC_S1F_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("STO1 DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5663_sto1_dac_l_mix, ARRAY_SIZE(rt5663_sto1_dac_l_mix)),
	SND_SOC_DAPM_MIXER("STO1 DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5663_sto1_dac_r_mix, ARRAY_SIZE(rt5663_sto1_dac_r_mix)),

	/* DACs */
	SND_SOC_DAPM_SUPPLY("STO1 DAC L Power", RT5663_PWR_DIG_1,
		RT5663_PWR_DAC_L1_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("STO1 DAC R Power", RT5663_PWR_DIG_1,
		RT5663_PWR_DAC_R1_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R", NULL, SND_SOC_NOPM, 0, 0),

	/* Headphone*/
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0, rt5663_hp_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_widget rt5663_v2_specific_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO2", RT5663_PWR_ANLG_3,
		RT5663_PWR_LDO2_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Det Power", RT5663_PWR_VOL,
		RT5663_V2_PWR_MIC_DET_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("LDO DAC", RT5663_PWR_DIG_1,
		RT5663_PWR_LDO_DACREF_SHIFT, 0, NULL, 0),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY("I2S ASRC", RT5663_ASRC_1,
		RT5663_I2S1_ASRC_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC ASRC", RT5663_ASRC_1,
		RT5663_DAC_STO1_ASRC_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC ASRC", RT5663_ASRC_1,
		RT5663_ADC_STO1_ASRC_SHIFT, 0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1 CBJ", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CBJ Power", RT5663_PWR_ANLG_3,
		RT5663_PWR_CBJ_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST2 Power", SND_SOC_NOPM, 0, 0,
		rt5663_bst2_power, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX1L", SND_SOC_NOPM, 0, 0, rt5663_recmix1l,
		ARRAY_SIZE(rt5663_recmix1l)),
	SND_SOC_DAPM_MIXER("RECMIX1R", SND_SOC_NOPM, 0, 0, rt5663_recmix1r,
		ARRAY_SIZE(rt5663_recmix1r)),
	SND_SOC_DAPM_SUPPLY("RECMIX1R Power", RT5663_PWR_ANLG_2,
		RT5663_PWR_RECMIX2_SHIFT, 0, NULL, 0),

	/* ADC */
	SND_SOC_DAPM_ADC("ADC R", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("ADC R Power", RT5663_PWR_DIG_1,
		RT5663_PWR_ADC_R1_SHIFT, 0, NULL, 0),

	/* ADC Mux */
	SND_SOC_DAPM_PGA("STO1 ADC L1", RT5663_STO1_ADC_MIXER,
		RT5663_STO1_ADC_L1_SRC_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("STO1 ADC R1", RT5663_STO1_ADC_MIXER,
		RT5663_STO1_ADC_R1_SRC_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("STO1 ADC L2", RT5663_STO1_ADC_MIXER,
		RT5663_STO1_ADC_L2_SRC_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("STO1 ADC R2", RT5663_STO1_ADC_MIXER,
		RT5663_STO1_ADC_R2_SRC_SHIFT, 1, NULL, 0),

	SND_SOC_DAPM_MUX("STO1 ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5663_sto1_adcl_mux),
	SND_SOC_DAPM_MUX("STO1 ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5663_sto1_adcr_mux),

	/* ADC Mix */
	SND_SOC_DAPM_MIXER("STO1 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5663_sto1_adc_r_mix, ARRAY_SIZE(rt5663_sto1_adc_r_mix)),

	/* Analog DAC Clock */
	SND_SOC_DAPM_SUPPLY("DAC Clock", RT5663_CHOP_DAC_L,
		RT5663_CKGEN_DAC1_SHIFT, 0, NULL, 0),

	/* Headphone out */
	SND_SOC_DAPM_SWITCH("HPO Playback", SND_SOC_NOPM, 0, 0,
		&rt5663_hpo_switch),
};

static const struct snd_soc_dapm_widget rt5663_specific_dapm_widgets[] = {
	/* System Clock Pre Divider Gating */
	SND_SOC_DAPM_SUPPLY("Pre Div Power", SND_SOC_NOPM, 0, 0,
		rt5663_pre_div_power, SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD),

	/* LDO */
	SND_SOC_DAPM_SUPPLY("LDO ADC", RT5663_PWR_DIG_1,
		RT5663_PWR_LDO_DACREF_SHIFT, 0, NULL, 0),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY("I2S ASRC", RT5663_ASRC_1,
		RT5663_I2S1_ASRC_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC ASRC", RT5663_ASRC_1,
		RT5663_DAC_STO1_ASRC_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC ASRC", RT5663_ASRC_1,
		RT5663_ADC_STO1_ASRC_SHIFT, 0, NULL, 0),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* STO ADC */
	SND_SOC_DAPM_PGA("STO1 ADC L1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("STO1 ADC L2", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Analog DAC source */
	SND_SOC_DAPM_MUX("DAC L Mux", SND_SOC_NOPM, 0, 0, &rt5663_alg_dacl_mux),
	SND_SOC_DAPM_MUX("DAC R Mux", SND_SOC_NOPM, 0, 0, &rt5663_alg_dacr_mux),
};

static const struct snd_soc_dapm_route rt5663_dapm_routes[] = {
	/* PLL */
	{ "I2S", NULL, "PLL", rt5663_is_sys_clk_from_pll },

	/* ASRC */
	{ "STO1 ADC Filter", NULL, "ADC ASRC", rt5663_is_using_asrc },
	{ "STO1 DAC Filter", NULL, "DAC ASRC", rt5663_is_using_asrc },
	{ "I2S", NULL, "I2S ASRC", rt5663_i2s_use_asrc },

	{ "ADC L", NULL, "ADC L Power" },
	{ "ADC L", NULL, "ADC Clock" },

	{ "STO1 ADC L2", NULL, "STO1 DAC MIXL" },

	{ "STO1 ADC MIXL", "ADC1 Switch", "STO1 ADC L1" },
	{ "STO1 ADC MIXL", "ADC2 Switch", "STO1 ADC L2" },
	{ "STO1 ADC MIXL", NULL, "STO1 ADC Filter" },

	{ "IF1 ADC1", NULL, "STO1 ADC MIXL" },
	{ "IF ADC", NULL, "IF1 ADC1" },
	{ "AIFTX", NULL, "IF ADC" },
	{ "AIFTX", NULL, "I2S" },

	{ "AIFRX", NULL, "I2S" },
	{ "IF DAC", NULL, "AIFRX" },
	{ "IF1 DAC1 L", NULL, "IF DAC" },
	{ "IF1 DAC1 R", NULL, "IF DAC" },

	{ "ADDA MIXL", "ADC L Switch", "STO1 ADC MIXL" },
	{ "ADDA MIXL", "DAC L Switch", "IF1 DAC1 L" },
	{ "ADDA MIXL", NULL, "STO1 DAC Filter" },
	{ "ADDA MIXL", NULL, "STO1 DAC L Power" },
	{ "ADDA MIXR", "DAC R Switch", "IF1 DAC1 R" },
	{ "ADDA MIXR", NULL, "STO1 DAC Filter" },
	{ "ADDA MIXR", NULL, "STO1 DAC R Power" },

	{ "DAC L1", NULL, "ADDA MIXL" },
	{ "DAC R1", NULL, "ADDA MIXR" },

	{ "STO1 DAC MIXL", "DAC L Switch", "DAC L1" },
	{ "STO1 DAC MIXL", "DAC R Switch", "DAC R1" },
	{ "STO1 DAC MIXL", NULL, "STO1 DAC L Power" },
	{ "STO1 DAC MIXL", NULL, "STO1 DAC Filter" },
	{ "STO1 DAC MIXR", "DAC R Switch", "DAC R1" },
	{ "STO1 DAC MIXR", "DAC L Switch", "DAC L1" },
	{ "STO1 DAC MIXR", NULL, "STO1 DAC R Power" },
	{ "STO1 DAC MIXR", NULL, "STO1 DAC Filter" },

	{ "HP Amp", NULL, "DAC L" },
	{ "HP Amp", NULL, "DAC R" },
};

static const struct snd_soc_dapm_route rt5663_v2_specific_dapm_routes[] = {
	{ "MICBIAS1", NULL, "LDO2" },
	{ "MICBIAS2", NULL, "LDO2" },

	{ "BST1 CBJ", NULL, "IN1P" },
	{ "BST1 CBJ", NULL, "IN1N" },
	{ "BST1 CBJ", NULL, "CBJ Power" },

	{ "BST2", NULL, "IN2P" },
	{ "BST2", NULL, "IN2N" },
	{ "BST2", NULL, "BST2 Power" },

	{ "RECMIX1L", "BST2 Switch", "BST2" },
	{ "RECMIX1L", "BST1 CBJ Switch", "BST1 CBJ" },
	{ "RECMIX1L", NULL, "RECMIX1L Power" },
	{ "RECMIX1R", "BST2 Switch", "BST2" },
	{ "RECMIX1R", NULL, "RECMIX1R Power" },

	{ "ADC L", NULL, "RECMIX1L" },
	{ "ADC R", NULL, "RECMIX1R" },
	{ "ADC R", NULL, "ADC R Power" },
	{ "ADC R", NULL, "ADC Clock" },

	{ "STO1 ADC L Mux", "ADC L", "ADC L" },
	{ "STO1 ADC L Mux", "ADC R", "ADC R" },
	{ "STO1 ADC L1", NULL, "STO1 ADC L Mux" },

	{ "STO1 ADC R Mux", "ADC L", "ADC L" },
	{ "STO1 ADC R Mux", "ADC R", "ADC R" },
	{ "STO1 ADC R1", NULL, "STO1 ADC R Mux" },
	{ "STO1 ADC R2", NULL, "STO1 DAC MIXR" },

	{ "STO1 ADC MIXR", "ADC1 Switch", "STO1 ADC R1" },
	{ "STO1 ADC MIXR", "ADC2 Switch", "STO1 ADC R2" },
	{ "STO1 ADC MIXR", NULL, "STO1 ADC Filter" },

	{ "IF1 ADC1", NULL, "STO1 ADC MIXR" },

	{ "ADDA MIXR", "ADC R Switch", "STO1 ADC MIXR" },

	{ "DAC L", NULL, "STO1 DAC MIXL" },
	{ "DAC L", NULL, "LDO DAC" },
	{ "DAC L", NULL, "DAC Clock" },
	{ "DAC R", NULL, "STO1 DAC MIXR" },
	{ "DAC R", NULL, "LDO DAC" },
	{ "DAC R", NULL, "DAC Clock" },

	{ "HPO Playback", "Switch", "HP Amp" },
	{ "HPOL", NULL, "HPO Playback" },
	{ "HPOR", NULL, "HPO Playback" },
};

static const struct snd_soc_dapm_route rt5663_specific_dapm_routes[] = {
	{ "I2S", NULL, "Pre Div Power" },

	{ "BST1", NULL, "IN1P" },
	{ "BST1", NULL, "IN1N" },
	{ "BST1", NULL, "RECMIX1L Power" },

	{ "ADC L", NULL, "BST1" },

	{ "STO1 ADC L1", NULL, "ADC L" },

	{ "DAC L Mux", "DAC L",  "DAC L1" },
	{ "DAC L Mux", "STO DAC MIXL", "STO1 DAC MIXL" },
	{ "DAC R Mux", "DAC R",  "DAC R1"},
	{ "DAC R Mux", "STO DAC MIXR", "STO1 DAC MIXR" },

	{ "DAC L", NULL, "DAC L Mux" },
	{ "DAC R", NULL, "DAC R Mux" },

	{ "HPOL", NULL, "HP Amp" },
	{ "HPOR", NULL, "HP Amp" },
};

static int rt5663_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0;
	int pre_div;

	rt5663->lrck = params_rate(params);

	dev_dbg(dai->dev, "bclk is %dHz and sysclk is %dHz\n",
		rt5663->lrck, rt5663->sysclk);

	pre_div = rl6231_get_clk_info(rt5663->sysclk, rt5663->lrck);
	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting %d for DAI %d\n",
			rt5663->lrck, dai->id);
		return -EINVAL;
	}

	dev_dbg(dai->dev, "pre_div is %d for iis %d\n", pre_div, dai->id);

	switch (params_width(params)) {
	case 8:
		val_len = RT5663_I2S_DL_8;
		break;
	case 16:
		val_len = RT5663_I2S_DL_16;
		break;
	case 20:
		val_len = RT5663_I2S_DL_20;
		break;
	case 24:
		val_len = RT5663_I2S_DL_24;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RT5663_I2S1_SDP,
		RT5663_I2S_DL_MASK, val_len);

	snd_soc_update_bits(codec, RT5663_ADDA_CLK_1,
		RT5663_I2S_PD1_MASK, pre_div << RT5663_I2S_PD1_SHIFT);

	return 0;
}

static int rt5663_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5663_I2S_MS_S;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5663_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5663_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5663_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5663_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RT5663_I2S1_SDP, RT5663_I2S_MS_MASK |
		RT5663_I2S_BP_MASK | RT5663_I2S_DF_MASK, reg_val);

	return 0;
}

static int rt5663_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
	unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	if (freq == rt5663->sysclk && clk_id == rt5663->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5663_SCLK_S_MCLK:
		reg_val |= RT5663_SCLK_SRC_MCLK;
		break;
	case RT5663_SCLK_S_PLL1:
		reg_val |= RT5663_SCLK_SRC_PLL1;
		break;
	case RT5663_SCLK_S_RCCLK:
		reg_val |= RT5663_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5663_GLB_CLK, RT5663_SCLK_SRC_MASK,
		reg_val);
	rt5663->sysclk = freq;
	rt5663->sysclk_src = clk_id;

	dev_dbg(codec->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static int rt5663_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	struct rl6231_pll_code pll_code;
	int ret;
	int mask, shift, val;

	if (source == rt5663->pll_src && freq_in == rt5663->pll_in &&
	    freq_out == rt5663->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5663->pll_in = 0;
		rt5663->pll_out = 0;
		snd_soc_update_bits(codec, RT5663_GLB_CLK,
			RT5663_SCLK_SRC_MASK, RT5663_SCLK_SRC_MCLK);
		return 0;
	}

	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		mask = RT5663_V2_PLL1_SRC_MASK;
		shift = RT5663_V2_PLL1_SRC_SHIFT;
		break;
	case CODEC_VER_0:
		mask = RT5663_PLL1_SRC_MASK;
		shift = RT5663_PLL1_SRC_SHIFT;
		break;
	default:
		dev_err(codec->dev, "Unknown CODEC Version\n");
		return -EINVAL;
	}

	switch (source) {
	case RT5663_PLL1_S_MCLK:
		val = 0x0;
		break;
	case RT5663_PLL1_S_BCLK1:
		val = 0x1;
		break;
	default:
		dev_err(codec->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5663_GLB_CLK, mask, (val << shift));

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "bypass=%d m=%d n=%d k=%d\n", pll_code.m_bp,
		(pll_code.m_bp ? 0 : pll_code.m_code), pll_code.n_code,
		pll_code.k_code);

	snd_soc_write(codec, RT5663_PLL_1,
		pll_code.n_code << RT5663_PLL_N_SHIFT | pll_code.k_code);
	snd_soc_write(codec, RT5663_PLL_2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5663_PLL_M_SHIFT |
		pll_code.m_bp << RT5663_PLL_M_BP_SHIFT);

	rt5663->pll_in = freq_in;
	rt5663->pll_out = freq_out;
	rt5663->pll_src = source;

	return 0;
}

static int rt5663_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
	unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0, reg;

	if (rx_mask || tx_mask)
		val |= RT5663_TDM_MODE_TDM;

	switch (slots) {
	case 4:
		val |= RT5663_TDM_IN_CH_4;
		val |= RT5663_TDM_OUT_CH_4;
		break;
	case 6:
		val |= RT5663_TDM_IN_CH_6;
		val |= RT5663_TDM_OUT_CH_6;
		break;
	case 8:
		val |= RT5663_TDM_IN_CH_8;
		val |= RT5663_TDM_OUT_CH_8;
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	switch (slot_width) {
	case 20:
		val |= RT5663_TDM_IN_LEN_20;
		val |= RT5663_TDM_OUT_LEN_20;
		break;
	case 24:
		val |= RT5663_TDM_IN_LEN_24;
		val |= RT5663_TDM_OUT_LEN_24;
		break;
	case 32:
		val |= RT5663_TDM_IN_LEN_32;
		val |= RT5663_TDM_OUT_LEN_32;
		break;
	case 16:
		break;
	default:
		return -EINVAL;
	}

	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		reg = RT5663_TDM_2;
		break;
	case CODEC_VER_0:
		reg = RT5663_TDM_1;
		break;
	default:
		dev_err(codec->dev, "Unknown CODEC Version\n");
		return -EINVAL;
	}

	snd_soc_update_bits(codec, reg, RT5663_TDM_MODE_MASK |
		RT5663_TDM_IN_CH_MASK | RT5663_TDM_OUT_CH_MASK |
		RT5663_TDM_IN_LEN_MASK | RT5663_TDM_OUT_LEN_MASK, val);

	return 0;
}

static int rt5663_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg;

	dev_dbg(codec->dev, "%s ratio = %d\n", __func__, ratio);

	if (rt5663->codec_ver == CODEC_VER_1)
		reg = RT5663_TDM_9;
	else
		reg = RT5663_TDM_5;

	switch (ratio) {
	case 32:
		snd_soc_update_bits(codec, reg,
			RT5663_TDM_LENGTN_MASK,
			RT5663_TDM_LENGTN_16);
		break;
	case 40:
		snd_soc_update_bits(codec, reg,
			RT5663_TDM_LENGTN_MASK,
			RT5663_TDM_LENGTN_20);
		break;
	case 48:
		snd_soc_update_bits(codec, reg,
			RT5663_TDM_LENGTN_MASK,
			RT5663_TDM_LENGTN_24);
		break;
	case 64:
		snd_soc_update_bits(codec, reg,
			RT5663_TDM_LENGTN_MASK,
			RT5663_TDM_LENGTN_32);
		break;
	default:
		dev_err(codec->dev, "Invalid ratio!\n");
		return -EINVAL;
	}

	return 0;
}

static int rt5663_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_1,
			RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK,
			RT5663_PWR_FV1 | RT5663_PWR_FV2);
		break;

	case SND_SOC_BIAS_PREPARE:
		if (rt5663->codec_ver == CODEC_VER_1) {
			snd_soc_update_bits(codec, RT5663_DIG_MISC,
				RT5663_DIG_GATE_CTRL_MASK,
				RT5663_DIG_GATE_CTRL_EN);
			snd_soc_update_bits(codec, RT5663_SIG_CLK_DET,
				RT5663_EN_ANA_CLK_DET_MASK |
				RT5663_PWR_CLK_DET_MASK,
				RT5663_EN_ANA_CLK_DET_AUTO |
				RT5663_PWR_CLK_DET_EN);
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (rt5663->codec_ver == CODEC_VER_1)
			snd_soc_update_bits(codec, RT5663_DIG_MISC,
				RT5663_DIG_GATE_CTRL_MASK,
				RT5663_DIG_GATE_CTRL_DIS);
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_1,
			RT5663_PWR_VREF1_MASK | RT5663_PWR_VREF2_MASK |
			RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK |
			RT5663_PWR_MB_MASK, RT5663_PWR_VREF1 |
			RT5663_PWR_VREF2 | RT5663_PWR_MB);
		usleep_range(10000, 10005);
		if (rt5663->codec_ver == CODEC_VER_1) {
			snd_soc_update_bits(codec, RT5663_SIG_CLK_DET,
				RT5663_EN_ANA_CLK_DET_MASK |
				RT5663_PWR_CLK_DET_MASK,
				RT5663_EN_ANA_CLK_DET_DIS |
				RT5663_PWR_CLK_DET_DIS);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, RT5663_PWR_ANLG_1,
			RT5663_PWR_VREF1_MASK | RT5663_PWR_VREF2_MASK |
			RT5663_PWR_FV1 | RT5663_PWR_FV2, 0x0);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5663_probe(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	rt5663->codec = codec;

	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		snd_soc_dapm_new_controls(dapm,
			rt5663_v2_specific_dapm_widgets,
			ARRAY_SIZE(rt5663_v2_specific_dapm_widgets));
		snd_soc_dapm_add_routes(dapm,
			rt5663_v2_specific_dapm_routes,
			ARRAY_SIZE(rt5663_v2_specific_dapm_routes));
		snd_soc_add_codec_controls(codec, rt5663_v2_specific_controls,
			ARRAY_SIZE(rt5663_v2_specific_controls));
		break;
	case CODEC_VER_0:
		snd_soc_dapm_new_controls(dapm,
			rt5663_specific_dapm_widgets,
			ARRAY_SIZE(rt5663_specific_dapm_widgets));
		snd_soc_dapm_add_routes(dapm,
			rt5663_specific_dapm_routes,
			ARRAY_SIZE(rt5663_specific_dapm_routes));
		snd_soc_add_codec_controls(codec, rt5663_specific_controls,
			ARRAY_SIZE(rt5663_specific_controls));
		break;
	}

	return 0;
}

static int rt5663_remove(struct snd_soc_codec *codec)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	regmap_write(rt5663->regmap, RT5663_RESET, 0);

	return 0;
}

#ifdef CONFIG_PM
static int rt5663_suspend(struct snd_soc_codec *codec)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5663->regmap, true);
	regcache_mark_dirty(rt5663->regmap);

	return 0;
}

static int rt5663_resume(struct snd_soc_codec *codec)
{
	struct rt5663_priv *rt5663 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5663->regmap, false);
	regcache_sync(rt5663->regmap);

	rt5663_irq(0, rt5663);

	return 0;
}
#else
#define rt5663_suspend NULL
#define rt5663_resume NULL
#endif

#define RT5663_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5663_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt5663_aif_dai_ops = {
	.hw_params = rt5663_hw_params,
	.set_fmt = rt5663_set_dai_fmt,
	.set_sysclk = rt5663_set_dai_sysclk,
	.set_pll = rt5663_set_dai_pll,
	.set_tdm_slot = rt5663_set_tdm_slot,
	.set_bclk_ratio = rt5663_set_bclk_ratio,
};

static struct snd_soc_dai_driver rt5663_dai[] = {
	{
		.name = "rt5663-aif",
		.id = RT5663_AIF,
		.playback = {
			.stream_name = "AIF Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5663_STEREO_RATES,
			.formats = RT5663_FORMATS,
		},
		.capture = {
			.stream_name = "AIF Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5663_STEREO_RATES,
			.formats = RT5663_FORMATS,
		},
		.ops = &rt5663_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5663 = {
	.probe = rt5663_probe,
	.remove = rt5663_remove,
	.suspend = rt5663_suspend,
	.resume = rt5663_resume,
	.set_bias_level = rt5663_set_bias_level,
	.idle_bias_off = true,
	.component_driver = {
		.controls = rt5663_snd_controls,
		.num_controls = ARRAY_SIZE(rt5663_snd_controls),
		.dapm_widgets = rt5663_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(rt5663_dapm_widgets),
		.dapm_routes = rt5663_dapm_routes,
		.num_dapm_routes = ARRAY_SIZE(rt5663_dapm_routes),
	}
};

static const struct regmap_config rt5663_v2_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.use_single_rw = true,
	.max_register = 0x07fa,
	.volatile_reg = rt5663_v2_volatile_register,
	.readable_reg = rt5663_v2_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5663_v2_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5663_v2_reg),
};

static const struct regmap_config rt5663_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.use_single_rw = true,
	.max_register = 0x03f3,
	.volatile_reg = rt5663_volatile_register,
	.readable_reg = rt5663_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5663_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5663_reg),
};

static const struct regmap_config temp_regmap = {
	.name = "nocache",
	.reg_bits = 16,
	.val_bits = 16,
	.use_single_rw = true,
	.max_register = 0x03f3,
	.cache_type = REGCACHE_NONE,
};

static const struct i2c_device_id rt5663_i2c_id[] = {
	{ "rt5663", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, rt5663_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rt5663_of_match[] = {
	{ .compatible = "realtek,rt5663", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5663_of_match);
#endif

#ifdef CONFIG_ACPI
static struct acpi_device_id rt5663_acpi_match[] = {
	{ "10EC5663", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt5663_acpi_match);
#endif

static void rt5663_v2_calibrate(struct rt5663_priv *rt5663)
{
	regmap_write(rt5663->regmap, RT5663_BIAS_CUR_8, 0xa402);
	regmap_write(rt5663->regmap, RT5663_PWR_DIG_1, 0x0100);
	regmap_write(rt5663->regmap, RT5663_RECMIX, 0x4040);
	regmap_write(rt5663->regmap, RT5663_DIG_MISC, 0x0001);
	regmap_write(rt5663->regmap, RT5663_RC_CLK, 0x0380);
	regmap_write(rt5663->regmap, RT5663_GLB_CLK, 0x8000);
	regmap_write(rt5663->regmap, RT5663_ADDA_CLK_1, 0x1000);
	regmap_write(rt5663->regmap, RT5663_CHOP_DAC_L, 0x3030);
	regmap_write(rt5663->regmap, RT5663_CALIB_ADC, 0x3c05);
	regmap_write(rt5663->regmap, RT5663_PWR_ANLG_1, 0xa23e);
	msleep(40);
	regmap_write(rt5663->regmap, RT5663_PWR_ANLG_1, 0xf23e);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_2, 0x0321);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_1, 0xfc00);
	msleep(500);
}

static void rt5663_calibrate(struct rt5663_priv *rt5663)
{
	int value, count;

	regmap_write(rt5663->regmap, RT5663_RC_CLK, 0x0280);
	regmap_write(rt5663->regmap, RT5663_GLB_CLK, 0x8000);
	regmap_write(rt5663->regmap, RT5663_DIG_MISC, 0x8001);
	regmap_write(rt5663->regmap, RT5663_VREF_RECMIX, 0x0032);
	regmap_write(rt5663->regmap, RT5663_PWR_ANLG_1, 0xa2be);
	msleep(20);
	regmap_write(rt5663->regmap, RT5663_PWR_ANLG_1, 0xf2be);
	regmap_write(rt5663->regmap, RT5663_PWR_DIG_2, 0x8400);
	regmap_write(rt5663->regmap, RT5663_CHOP_ADC, 0x3000);
	regmap_write(rt5663->regmap, RT5663_DEPOP_1, 0x003b);
	regmap_write(rt5663->regmap, RT5663_PWR_DIG_1, 0x8df8);
	regmap_write(rt5663->regmap, RT5663_PWR_ANLG_2, 0x0003);
	regmap_write(rt5663->regmap, RT5663_PWR_ANLG_3, 0x018c);
	regmap_write(rt5663->regmap, RT5663_ADDA_CLK_1, 0x1111);
	regmap_write(rt5663->regmap, RT5663_PRE_DIV_GATING_1, 0xffff);
	regmap_write(rt5663->regmap, RT5663_PRE_DIV_GATING_2, 0xffff);
	regmap_write(rt5663->regmap, RT5663_DEPOP_2, 0x3003);
	regmap_write(rt5663->regmap, RT5663_DEPOP_1, 0x003b);
	regmap_write(rt5663->regmap, RT5663_HP_CHARGE_PUMP_1, 0x1e32);
	regmap_write(rt5663->regmap, RT5663_HP_CHARGE_PUMP_2, 0x1371);
	regmap_write(rt5663->regmap, RT5663_DACREF_LDO, 0x3b0b);
	regmap_write(rt5663->regmap, RT5663_STO_DAC_MIXER, 0x2080);
	regmap_write(rt5663->regmap, RT5663_BYPASS_STO_DAC, 0x000c);
	regmap_write(rt5663->regmap, RT5663_HP_BIAS, 0xabba);
	regmap_write(rt5663->regmap, RT5663_CHARGE_PUMP_1, 0x2224);
	regmap_write(rt5663->regmap, RT5663_HP_OUT_EN, 0x8088);
	regmap_write(rt5663->regmap, RT5663_STO_DRE_9, 0x0017);
	regmap_write(rt5663->regmap, RT5663_STO_DRE_10, 0x0017);
	regmap_write(rt5663->regmap, RT5663_STO1_ADC_MIXER, 0x4040);
	regmap_write(rt5663->regmap, RT5663_RECMIX, 0x0005);
	regmap_write(rt5663->regmap, RT5663_ADDA_RST, 0xc000);
	regmap_write(rt5663->regmap, RT5663_STO1_HPF_ADJ1, 0x3320);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_2, 0x00c9);
	regmap_write(rt5663->regmap, RT5663_DUMMY_1, 0x004c);
	regmap_write(rt5663->regmap, RT5663_ANA_BIAS_CUR_1, 0x7766);
	regmap_write(rt5663->regmap, RT5663_BIAS_CUR_8, 0x4702);
	msleep(200);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_1, 0x0069);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_3, 0x06c2);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_1_1, 0x7b00);
	regmap_write(rt5663->regmap, RT5663_HP_CALIB_1_1, 0xfb00);
	count = 0;
	while (true) {
		regmap_read(rt5663->regmap, RT5663_HP_CALIB_1_1, &value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 200)
			return;
		count++;
	}
}

static int rt5663_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5663_priv *rt5663;
	int ret;
	unsigned int val;
	struct regmap *regmap;

	rt5663 = devm_kzalloc(&i2c->dev, sizeof(struct rt5663_priv),
		GFP_KERNEL);

	if (rt5663 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5663);

	regmap = devm_regmap_init_i2c(i2c, &temp_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "Failed to allocate temp register map: %d\n",
			ret);
		return ret;
	}
	regmap_read(regmap, RT5663_VENDOR_ID_2, &val);
	switch (val) {
	case RT5663_DEVICE_ID_2:
		rt5663->regmap = devm_regmap_init_i2c(i2c, &rt5663_v2_regmap);
		rt5663->codec_ver = CODEC_VER_1;
		break;
	case RT5663_DEVICE_ID_1:
		rt5663->regmap = devm_regmap_init_i2c(i2c, &rt5663_regmap);
		rt5663->codec_ver = CODEC_VER_0;
		break;
	default:
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt5663\n",
			val);
		return -ENODEV;
	}

	if (IS_ERR(rt5663->regmap)) {
		ret = PTR_ERR(rt5663->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	/* reset and calibrate */
	regmap_write(rt5663->regmap, RT5663_RESET, 0);
	regcache_cache_bypass(rt5663->regmap, true);
	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		rt5663_v2_calibrate(rt5663);
		break;
	case CODEC_VER_0:
		rt5663_calibrate(rt5663);
		break;
	default:
		dev_err(&i2c->dev, "%s:Unknown codec type\n", __func__);
	}
	regcache_cache_bypass(rt5663->regmap, false);
	regmap_write(rt5663->regmap, RT5663_RESET, 0);
	dev_dbg(&i2c->dev, "calibrate done\n");

	/* GPIO1 as IRQ */
	regmap_update_bits(rt5663->regmap, RT5663_GPIO_1, RT5663_GP1_PIN_MASK,
		RT5663_GP1_PIN_IRQ);
	/* 4btn inline command debounce */
	regmap_update_bits(rt5663->regmap, RT5663_IL_CMD_5,
		RT5663_4BTN_CLK_DEB_MASK, RT5663_4BTN_CLK_DEB_65MS);

	switch (rt5663->codec_ver) {
	case CODEC_VER_1:
		regmap_write(rt5663->regmap, RT5663_BIAS_CUR_8, 0xa402);
		/* JD1 */
		regmap_update_bits(rt5663->regmap, RT5663_AUTO_1MRC_CLK,
			RT5663_IRQ_POW_SAV_MASK | RT5663_IRQ_POW_SAV_JD1_MASK,
			RT5663_IRQ_POW_SAV_EN | RT5663_IRQ_POW_SAV_JD1_EN);
		regmap_update_bits(rt5663->regmap, RT5663_PWR_ANLG_2,
			RT5663_PWR_JD1_MASK, RT5663_PWR_JD1);
		regmap_update_bits(rt5663->regmap, RT5663_IRQ_1,
			RT5663_EN_CB_JD_MASK, RT5663_EN_CB_JD_EN);

		regmap_update_bits(rt5663->regmap, RT5663_HP_LOGIC_2,
			RT5663_HP_SIG_SRC1_MASK, RT5663_HP_SIG_SRC1_REG);
		regmap_update_bits(rt5663->regmap, RT5663_RECMIX,
			RT5663_VREF_BIAS_MASK | RT5663_CBJ_DET_MASK |
			RT5663_DET_TYPE_MASK, RT5663_VREF_BIAS_REG |
			RT5663_CBJ_DET_EN | RT5663_DET_TYPE_QFN);
		/* Set GPIO4 and GPIO8 as input for combo jack */
		regmap_update_bits(rt5663->regmap, RT5663_GPIO_2,
			RT5663_GP4_PIN_CONF_MASK, RT5663_GP4_PIN_CONF_INPUT);
		regmap_update_bits(rt5663->regmap, RT5663_GPIO_3,
			RT5663_GP8_PIN_CONF_MASK, RT5663_GP8_PIN_CONF_INPUT);
		regmap_update_bits(rt5663->regmap, RT5663_PWR_ANLG_1,
			RT5663_LDO1_DVO_MASK | RT5663_AMP_HP_MASK,
			RT5663_LDO1_DVO_0_9V | RT5663_AMP_HP_3X);
			break;
	case CODEC_VER_0:
		regmap_update_bits(rt5663->regmap, RT5663_DIG_MISC,
			RT5663_DIG_GATE_CTRL_MASK, RT5663_DIG_GATE_CTRL_EN);
		regmap_update_bits(rt5663->regmap, RT5663_AUTO_1MRC_CLK,
			RT5663_IRQ_MANUAL_MASK, RT5663_IRQ_MANUAL_EN);
		regmap_update_bits(rt5663->regmap, RT5663_IRQ_1,
			RT5663_EN_IRQ_JD1_MASK, RT5663_EN_IRQ_JD1_EN);
		regmap_update_bits(rt5663->regmap, RT5663_GPIO_1,
			RT5663_GPIO1_TYPE_MASK, RT5663_GPIO1_TYPE_EN);
		regmap_write(rt5663->regmap, RT5663_VREF_RECMIX, 0x0032);
		regmap_write(rt5663->regmap, RT5663_PWR_ANLG_1, 0xa2be);
		msleep(20);
		regmap_write(rt5663->regmap, RT5663_PWR_ANLG_1, 0xf2be);
		regmap_update_bits(rt5663->regmap, RT5663_GPIO_2,
			RT5663_GP1_PIN_CONF_MASK | RT5663_SEL_GPIO1_MASK,
			RT5663_GP1_PIN_CONF_OUTPUT | RT5663_SEL_GPIO1_EN);
		/* DACREF LDO control */
		regmap_update_bits(rt5663->regmap, RT5663_DACREF_LDO, 0x3e0e,
			0x3a0a);
		regmap_update_bits(rt5663->regmap, RT5663_RECMIX,
			RT5663_RECMIX1_BST1_MASK, RT5663_RECMIX1_BST1_ON);
		regmap_update_bits(rt5663->regmap, RT5663_TDM_2,
			RT5663_DATA_SWAP_ADCDAT1_MASK,
			RT5663_DATA_SWAP_ADCDAT1_LL);
			break;
	default:
		dev_err(&i2c->dev, "%s:Unknown codec type\n", __func__);
	}

	INIT_DELAYED_WORK(&rt5663->jack_detect_work, rt5663_jack_detect_work);

	if (i2c->irq) {
		ret = request_irq(i2c->irq, rt5663_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, "rt5663", rt5663);
		if (ret)
			dev_err(&i2c->dev, "%s Failed to reguest IRQ: %d\n",
				__func__, ret);
	}

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5663,
			rt5663_dai, ARRAY_SIZE(rt5663_dai));

	if (ret) {
		if (i2c->irq)
			free_irq(i2c->irq, rt5663);
	}

	return ret;
}

static int rt5663_i2c_remove(struct i2c_client *i2c)
{
	struct rt5663_priv *rt5663 = i2c_get_clientdata(i2c);

	if (i2c->irq)
		free_irq(i2c->irq, rt5663);

	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static void rt5663_i2c_shutdown(struct i2c_client *client)
{
	struct rt5663_priv *rt5663 = i2c_get_clientdata(client);

	regmap_write(rt5663->regmap, RT5663_RESET, 0);
}

static struct i2c_driver rt5663_i2c_driver = {
	.driver = {
		.name = "rt5663",
		.acpi_match_table = ACPI_PTR(rt5663_acpi_match),
		.of_match_table = of_match_ptr(rt5663_of_match),
	},
	.probe = rt5663_i2c_probe,
	.remove = rt5663_i2c_remove,
	.shutdown = rt5663_i2c_shutdown,
	.id_table = rt5663_i2c_id,
};
module_i2c_driver(rt5663_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5663 driver");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL v2");
