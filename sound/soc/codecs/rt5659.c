// SPDX-License-Identifier: GPL-2.0-only
/*
 * rt5659.c  --  RT5659/RT5658 ALSA SoC audio codec driver
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/rt5659.h>

#include "rl6231.h"
#include "rt5659.h"

static const struct reg_default rt5659_reg[] = {
	{ 0x0000, 0x0000 },
	{ 0x0001, 0x4848 },
	{ 0x0002, 0x8080 },
	{ 0x0003, 0xc8c8 },
	{ 0x0004, 0xc80a },
	{ 0x0005, 0x0000 },
	{ 0x0006, 0x0000 },
	{ 0x0007, 0x0103 },
	{ 0x0008, 0x0080 },
	{ 0x0009, 0x0000 },
	{ 0x000a, 0x0000 },
	{ 0x000c, 0x0000 },
	{ 0x000d, 0x0000 },
	{ 0x000f, 0x0808 },
	{ 0x0010, 0x3080 },
	{ 0x0011, 0x4a00 },
	{ 0x0012, 0x4e00 },
	{ 0x0015, 0x42c1 },
	{ 0x0016, 0x0000 },
	{ 0x0018, 0x000b },
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
	{ 0x0026, 0xc060 },
	{ 0x0027, 0xd8d8 },
	{ 0x0029, 0x8080 },
	{ 0x002a, 0xaaaa },
	{ 0x002b, 0xaaaa },
	{ 0x002c, 0x00af },
	{ 0x002d, 0x0000 },
	{ 0x002f, 0x1002 },
	{ 0x0031, 0x5000 },
	{ 0x0032, 0x0000 },
	{ 0x0033, 0x0000 },
	{ 0x0034, 0x0000 },
	{ 0x0035, 0x0000 },
	{ 0x0036, 0x0000 },
	{ 0x003a, 0x0000 },
	{ 0x003b, 0x0000 },
	{ 0x003c, 0x007f },
	{ 0x003d, 0x0000 },
	{ 0x003e, 0x007f },
	{ 0x0040, 0x0808 },
	{ 0x0046, 0x001f },
	{ 0x0047, 0x001f },
	{ 0x0048, 0x0003 },
	{ 0x0049, 0xe061 },
	{ 0x004a, 0x0000 },
	{ 0x004b, 0x031f },
	{ 0x004d, 0x0000 },
	{ 0x004e, 0x001f },
	{ 0x004f, 0x0000 },
	{ 0x0050, 0x001f },
	{ 0x0052, 0xf000 },
	{ 0x0053, 0x0111 },
	{ 0x0054, 0x0064 },
	{ 0x0055, 0x0080 },
	{ 0x0056, 0xef0e },
	{ 0x0057, 0xf0f0 },
	{ 0x0058, 0xef0e },
	{ 0x0059, 0xf0f0 },
	{ 0x005a, 0xef0e },
	{ 0x005b, 0xf0f0 },
	{ 0x005c, 0xf000 },
	{ 0x005d, 0x0000 },
	{ 0x005e, 0x1f2c },
	{ 0x005f, 0x1f2c },
	{ 0x0060, 0x2717 },
	{ 0x0061, 0x0000 },
	{ 0x0062, 0x0000 },
	{ 0x0063, 0x003e },
	{ 0x0064, 0x0000 },
	{ 0x0065, 0x0000 },
	{ 0x0066, 0x0000 },
	{ 0x0067, 0x0000 },
	{ 0x006a, 0x0000 },
	{ 0x006b, 0x0000 },
	{ 0x006c, 0x0000 },
	{ 0x006e, 0x0000 },
	{ 0x006f, 0x0000 },
	{ 0x0070, 0x8000 },
	{ 0x0071, 0x8000 },
	{ 0x0072, 0x8000 },
	{ 0x0073, 0x1110 },
	{ 0x0074, 0xfe00 },
	{ 0x0075, 0x2409 },
	{ 0x0076, 0x000a },
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
	{ 0x0085, 0x0000 },
	{ 0x0086, 0x0008 },
	{ 0x0087, 0x0000 },
	{ 0x0088, 0x0000 },
	{ 0x0089, 0x0000 },
	{ 0x008a, 0x0000 },
	{ 0x008b, 0x0000 },
	{ 0x008c, 0x0003 },
	{ 0x008e, 0x0000 },
	{ 0x008f, 0x1000 },
	{ 0x0090, 0x0646 },
	{ 0x0091, 0x0c16 },
	{ 0x0092, 0x0073 },
	{ 0x0093, 0x0000 },
	{ 0x0094, 0x0080 },
	{ 0x0097, 0x0000 },
	{ 0x0098, 0x0000 },
	{ 0x0099, 0x0000 },
	{ 0x009a, 0x0000 },
	{ 0x009b, 0x0000 },
	{ 0x009c, 0x007f },
	{ 0x009d, 0x0000 },
	{ 0x009e, 0x007f },
	{ 0x009f, 0x0000 },
	{ 0x00a0, 0x0060 },
	{ 0x00a1, 0x90a1 },
	{ 0x00ae, 0x2000 },
	{ 0x00af, 0x0000 },
	{ 0x00b0, 0x2000 },
	{ 0x00b1, 0x0000 },
	{ 0x00b2, 0x0000 },
	{ 0x00b6, 0x0000 },
	{ 0x00b7, 0x0000 },
	{ 0x00b8, 0x0000 },
	{ 0x00b9, 0x0000 },
	{ 0x00ba, 0x0000 },
	{ 0x00bb, 0x0000 },
	{ 0x00be, 0x0000 },
	{ 0x00bf, 0x0000 },
	{ 0x00c0, 0x0000 },
	{ 0x00c1, 0x0000 },
	{ 0x00c2, 0x0000 },
	{ 0x00c3, 0x0000 },
	{ 0x00c4, 0x0003 },
	{ 0x00c5, 0x0000 },
	{ 0x00cb, 0xa02f },
	{ 0x00cc, 0x0000 },
	{ 0x00cd, 0x0e02 },
	{ 0x00d6, 0x0000 },
	{ 0x00d7, 0x2244 },
	{ 0x00d9, 0x0809 },
	{ 0x00da, 0x0000 },
	{ 0x00db, 0x0008 },
	{ 0x00dc, 0x00c0 },
	{ 0x00dd, 0x6724 },
	{ 0x00de, 0x3131 },
	{ 0x00df, 0x0008 },
	{ 0x00e0, 0x4000 },
	{ 0x00e1, 0x3131 },
	{ 0x00e4, 0x400c },
	{ 0x00e5, 0x8031 },
	{ 0x00ea, 0xb320 },
	{ 0x00eb, 0x0000 },
	{ 0x00ec, 0xb300 },
	{ 0x00ed, 0x0000 },
	{ 0x00f0, 0x0000 },
	{ 0x00f1, 0x0202 },
	{ 0x00f2, 0x0ddd },
	{ 0x00f3, 0x0ddd },
	{ 0x00f4, 0x0ddd },
	{ 0x00f6, 0x0000 },
	{ 0x00f7, 0x0000 },
	{ 0x00f8, 0x0000 },
	{ 0x00f9, 0x0000 },
	{ 0x00fa, 0x8000 },
	{ 0x00fb, 0x0000 },
	{ 0x00fc, 0x0000 },
	{ 0x00fd, 0x0001 },
	{ 0x00fe, 0x10ec },
	{ 0x00ff, 0x6311 },
	{ 0x0100, 0xaaaa },
	{ 0x010a, 0xaaaa },
	{ 0x010b, 0x00a0 },
	{ 0x010c, 0xaeae },
	{ 0x010d, 0xaaaa },
	{ 0x010e, 0xaaa8 },
	{ 0x010f, 0xa0aa },
	{ 0x0110, 0xe02a },
	{ 0x0111, 0xa702 },
	{ 0x0112, 0xaaaa },
	{ 0x0113, 0x2800 },
	{ 0x0116, 0x0000 },
	{ 0x0117, 0x0f00 },
	{ 0x011a, 0x0020 },
	{ 0x011b, 0x0011 },
	{ 0x011c, 0x0150 },
	{ 0x011d, 0x0000 },
	{ 0x011e, 0x0000 },
	{ 0x011f, 0x0000 },
	{ 0x0120, 0x0000 },
	{ 0x0121, 0x009b },
	{ 0x0122, 0x5014 },
	{ 0x0123, 0x0421 },
	{ 0x0124, 0x7cea },
	{ 0x0125, 0x0420 },
	{ 0x0126, 0x5550 },
	{ 0x0132, 0x0000 },
	{ 0x0133, 0x0000 },
	{ 0x0137, 0x5055 },
	{ 0x0138, 0x3700 },
	{ 0x0139, 0x79a1 },
	{ 0x013a, 0x2020 },
	{ 0x013b, 0x2020 },
	{ 0x013c, 0x2005 },
	{ 0x013e, 0x1f00 },
	{ 0x013f, 0x0000 },
	{ 0x0145, 0x0002 },
	{ 0x0146, 0x0000 },
	{ 0x0147, 0x0000 },
	{ 0x0148, 0x0000 },
	{ 0x0150, 0x1813 },
	{ 0x0151, 0x0690 },
	{ 0x0152, 0x1c17 },
	{ 0x0153, 0x6883 },
	{ 0x0154, 0xd3ce },
	{ 0x0155, 0x352d },
	{ 0x0156, 0x00eb },
	{ 0x0157, 0x3717 },
	{ 0x0158, 0x4c6a },
	{ 0x0159, 0xe41b },
	{ 0x015a, 0x2a13 },
	{ 0x015b, 0xb600 },
	{ 0x015c, 0xc730 },
	{ 0x015d, 0x35d4 },
	{ 0x015e, 0x00bf },
	{ 0x0160, 0x0ec0 },
	{ 0x0161, 0x0020 },
	{ 0x0162, 0x0080 },
	{ 0x0163, 0x0800 },
	{ 0x0164, 0x0000 },
	{ 0x0165, 0x0000 },
	{ 0x0166, 0x0000 },
	{ 0x0167, 0x001f },
	{ 0x0170, 0x4e80 },
	{ 0x0171, 0x0020 },
	{ 0x0172, 0x0080 },
	{ 0x0173, 0x0800 },
	{ 0x0174, 0x000c },
	{ 0x0175, 0x0000 },
	{ 0x0190, 0x3300 },
	{ 0x0191, 0x2200 },
	{ 0x0192, 0x0000 },
	{ 0x01b0, 0x4b38 },
	{ 0x01b1, 0x0000 },
	{ 0x01b2, 0x0000 },
	{ 0x01b3, 0x0000 },
	{ 0x01c0, 0x0045 },
	{ 0x01c1, 0x0540 },
	{ 0x01c2, 0x0000 },
	{ 0x01c3, 0x0030 },
	{ 0x01c7, 0x0000 },
	{ 0x01c8, 0x5757 },
	{ 0x01c9, 0x5757 },
	{ 0x01ca, 0x5757 },
	{ 0x01cb, 0x5757 },
	{ 0x01cc, 0x5757 },
	{ 0x01cd, 0x5757 },
	{ 0x01ce, 0x006f },
	{ 0x01da, 0x0000 },
	{ 0x01db, 0x0000 },
	{ 0x01de, 0x7d00 },
	{ 0x01df, 0x10c0 },
	{ 0x01e0, 0x06a1 },
	{ 0x01e1, 0x0000 },
	{ 0x01e2, 0x0000 },
	{ 0x01e3, 0x0000 },
	{ 0x01e4, 0x0001 },
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
	{ 0x01f6, 0x1e04 },
	{ 0x01f7, 0x01a1 },
	{ 0x01f8, 0x0000 },
	{ 0x01f9, 0x0000 },
	{ 0x01fa, 0x0002 },
	{ 0x01fb, 0x0000 },
	{ 0x01fc, 0x0000 },
	{ 0x01fd, 0x0000 },
	{ 0x01fe, 0x0000 },
	{ 0x0200, 0x066c },
	{ 0x0201, 0x7fff },
	{ 0x0202, 0x7fff },
	{ 0x0203, 0x0000 },
	{ 0x0204, 0x0000 },
	{ 0x0205, 0x0000 },
	{ 0x0206, 0x0000 },
	{ 0x0207, 0x0000 },
	{ 0x0208, 0x0000 },
	{ 0x0256, 0x0000 },
	{ 0x0257, 0x0000 },
	{ 0x0258, 0x0000 },
	{ 0x0259, 0x0000 },
	{ 0x025a, 0x0000 },
	{ 0x025b, 0x3333 },
	{ 0x025c, 0x3333 },
	{ 0x025d, 0x3333 },
	{ 0x025e, 0x0000 },
	{ 0x025f, 0x0000 },
	{ 0x0260, 0x0000 },
	{ 0x0261, 0x0022 },
	{ 0x0262, 0x0300 },
	{ 0x0265, 0x1e80 },
	{ 0x0266, 0x0131 },
	{ 0x0267, 0x0003 },
	{ 0x0268, 0x0000 },
	{ 0x0269, 0x0000 },
	{ 0x026a, 0x0000 },
	{ 0x026b, 0x0000 },
	{ 0x026c, 0x0000 },
	{ 0x026d, 0x0000 },
	{ 0x026e, 0x0000 },
	{ 0x026f, 0x0000 },
	{ 0x0270, 0x0000 },
	{ 0x0271, 0x0000 },
	{ 0x0272, 0x0000 },
	{ 0x0273, 0x0000 },
	{ 0x0280, 0x0000 },
	{ 0x0281, 0x0000 },
	{ 0x0282, 0x0418 },
	{ 0x0283, 0x7fff },
	{ 0x0284, 0x7000 },
	{ 0x0290, 0x01d0 },
	{ 0x0291, 0x0100 },
	{ 0x02fa, 0x0000 },
	{ 0x02fb, 0x0000 },
	{ 0x02fc, 0x0000 },
	{ 0x0300, 0x001f },
	{ 0x0301, 0x032c },
	{ 0x0302, 0x5f21 },
	{ 0x0303, 0x4000 },
	{ 0x0304, 0x4000 },
	{ 0x0305, 0x0600 },
	{ 0x0306, 0x8000 },
	{ 0x0307, 0x0700 },
	{ 0x0308, 0x001f },
	{ 0x0309, 0x032c },
	{ 0x030a, 0x5f21 },
	{ 0x030b, 0x4000 },
	{ 0x030c, 0x4000 },
	{ 0x030d, 0x0600 },
	{ 0x030e, 0x8000 },
	{ 0x030f, 0x0700 },
	{ 0x0310, 0x4560 },
	{ 0x0311, 0xa4a8 },
	{ 0x0312, 0x7418 },
	{ 0x0313, 0x0000 },
	{ 0x0314, 0x0006 },
	{ 0x0315, 0x00ff },
	{ 0x0316, 0xc400 },
	{ 0x0317, 0x4560 },
	{ 0x0318, 0xa4a8 },
	{ 0x0319, 0x7418 },
	{ 0x031a, 0x0000 },
	{ 0x031b, 0x0006 },
	{ 0x031c, 0x00ff },
	{ 0x031d, 0xc400 },
	{ 0x0320, 0x0f20 },
	{ 0x0321, 0x8700 },
	{ 0x0322, 0x7dc2 },
	{ 0x0323, 0xa178 },
	{ 0x0324, 0x5383 },
	{ 0x0325, 0x7dc2 },
	{ 0x0326, 0xa178 },
	{ 0x0327, 0x5383 },
	{ 0x0328, 0x003e },
	{ 0x0329, 0x02c1 },
	{ 0x032a, 0xd37d },
	{ 0x0330, 0x00a6 },
	{ 0x0331, 0x04c3 },
	{ 0x0332, 0x27c8 },
	{ 0x0333, 0xbf50 },
	{ 0x0334, 0x0045 },
	{ 0x0335, 0x2007 },
	{ 0x0336, 0x7418 },
	{ 0x0337, 0x0501 },
	{ 0x0338, 0x0000 },
	{ 0x0339, 0x0010 },
	{ 0x033a, 0x1010 },
	{ 0x0340, 0x0800 },
	{ 0x0341, 0x0800 },
	{ 0x0342, 0x0800 },
	{ 0x0343, 0x0800 },
	{ 0x0344, 0x0000 },
	{ 0x0345, 0x0000 },
	{ 0x0346, 0x0000 },
	{ 0x0347, 0x0000 },
	{ 0x0348, 0x0000 },
	{ 0x0349, 0x0000 },
	{ 0x034a, 0x0000 },
	{ 0x034b, 0x0000 },
	{ 0x034c, 0x0000 },
	{ 0x034d, 0x0000 },
	{ 0x034e, 0x0000 },
	{ 0x034f, 0x0000 },
	{ 0x0350, 0x0000 },
	{ 0x0351, 0x0000 },
	{ 0x0352, 0x0000 },
	{ 0x0353, 0x0000 },
	{ 0x0354, 0x0000 },
	{ 0x0355, 0x0000 },
	{ 0x0356, 0x0000 },
	{ 0x0357, 0x0000 },
	{ 0x0358, 0x0000 },
	{ 0x0359, 0x0000 },
	{ 0x035a, 0x0000 },
	{ 0x035b, 0x0000 },
	{ 0x035c, 0x0000 },
	{ 0x035d, 0x0000 },
	{ 0x035e, 0x2000 },
	{ 0x035f, 0x0000 },
	{ 0x0360, 0x2000 },
	{ 0x0361, 0x2000 },
	{ 0x0362, 0x0000 },
	{ 0x0363, 0x2000 },
	{ 0x0364, 0x0200 },
	{ 0x0365, 0x0000 },
	{ 0x0366, 0x0000 },
	{ 0x0367, 0x0000 },
	{ 0x0368, 0x0000 },
	{ 0x0369, 0x0000 },
	{ 0x036a, 0x0000 },
	{ 0x036b, 0x0000 },
	{ 0x036c, 0x0000 },
	{ 0x036d, 0x0000 },
	{ 0x036e, 0x0200 },
	{ 0x036f, 0x0000 },
	{ 0x0370, 0x0000 },
	{ 0x0371, 0x0000 },
	{ 0x0372, 0x0000 },
	{ 0x0373, 0x0000 },
	{ 0x0374, 0x0000 },
	{ 0x0375, 0x0000 },
	{ 0x0376, 0x0000 },
	{ 0x0377, 0x0000 },
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

static bool rt5659_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5659_RESET:
	case RT5659_EJD_CTRL_2:
	case RT5659_SILENCE_CTRL:
	case RT5659_DAC2_DIG_VOL:
	case RT5659_HP_IMP_GAIN_2:
	case RT5659_PDM_OUT_CTRL:
	case RT5659_PDM_DATA_CTRL_1:
	case RT5659_PDM_DATA_CTRL_4:
	case RT5659_HAPTIC_GEN_CTRL_1:
	case RT5659_HAPTIC_GEN_CTRL_3:
	case RT5659_HAPTIC_LPF_CTRL_3:
	case RT5659_CLK_DET:
	case RT5659_MICBIAS_1:
	case RT5659_ASRC_11:
	case RT5659_ADC_EQ_CTRL_1:
	case RT5659_DAC_EQ_CTRL_1:
	case RT5659_INT_ST_1:
	case RT5659_INT_ST_2:
	case RT5659_GPIO_STA:
	case RT5659_SINE_GEN_CTRL_1:
	case RT5659_IL_CMD_1:
	case RT5659_4BTN_IL_CMD_1:
	case RT5659_PSV_IL_CMD_1:
	case RT5659_AJD1_CTRL:
	case RT5659_AJD2_AJD3_CTRL:
	case RT5659_JD_CTRL_3:
	case RT5659_VENDOR_ID:
	case RT5659_VENDOR_ID_1:
	case RT5659_DEVICE_ID:
	case RT5659_MEMORY_TEST:
	case RT5659_SOFT_RAMP_DEPOP_DAC_CLK_CTRL:
	case RT5659_VOL_TEST:
	case RT5659_STO_NG2_CTRL_1:
	case RT5659_STO_NG2_CTRL_5:
	case RT5659_STO_NG2_CTRL_6:
	case RT5659_STO_NG2_CTRL_7:
	case RT5659_MONO_NG2_CTRL_1:
	case RT5659_MONO_NG2_CTRL_5:
	case RT5659_MONO_NG2_CTRL_6:
	case RT5659_HP_IMP_SENS_CTRL_1:
	case RT5659_HP_IMP_SENS_CTRL_3:
	case RT5659_HP_IMP_SENS_CTRL_4:
	case RT5659_HP_CALIB_CTRL_1:
	case RT5659_HP_CALIB_CTRL_9:
	case RT5659_HP_CALIB_STA_1:
	case RT5659_HP_CALIB_STA_2:
	case RT5659_HP_CALIB_STA_3:
	case RT5659_HP_CALIB_STA_4:
	case RT5659_HP_CALIB_STA_5:
	case RT5659_HP_CALIB_STA_6:
	case RT5659_HP_CALIB_STA_7:
	case RT5659_HP_CALIB_STA_8:
	case RT5659_HP_CALIB_STA_9:
	case RT5659_MONO_AMP_CALIB_CTRL_1:
	case RT5659_MONO_AMP_CALIB_CTRL_3:
	case RT5659_MONO_AMP_CALIB_STA_1:
	case RT5659_MONO_AMP_CALIB_STA_2:
	case RT5659_MONO_AMP_CALIB_STA_3:
	case RT5659_MONO_AMP_CALIB_STA_4:
	case RT5659_SPK_PWR_LMT_STA_1:
	case RT5659_SPK_PWR_LMT_STA_2:
	case RT5659_SPK_PWR_LMT_STA_3:
	case RT5659_SPK_PWR_LMT_STA_4:
	case RT5659_SPK_PWR_LMT_STA_5:
	case RT5659_SPK_PWR_LMT_STA_6:
	case RT5659_SPK_DC_CAILB_CTRL_1:
	case RT5659_SPK_DC_CAILB_STA_1:
	case RT5659_SPK_DC_CAILB_STA_2:
	case RT5659_SPK_DC_CAILB_STA_3:
	case RT5659_SPK_DC_CAILB_STA_4:
	case RT5659_SPK_DC_CAILB_STA_5:
	case RT5659_SPK_DC_CAILB_STA_6:
	case RT5659_SPK_DC_CAILB_STA_7:
	case RT5659_SPK_DC_CAILB_STA_8:
	case RT5659_SPK_DC_CAILB_STA_9:
	case RT5659_SPK_DC_CAILB_STA_10:
	case RT5659_SPK_VDD_STA_1:
	case RT5659_SPK_VDD_STA_2:
	case RT5659_SPK_DC_DET_CTRL_1:
	case RT5659_PURE_DC_DET_CTRL_1:
	case RT5659_PURE_DC_DET_CTRL_2:
	case RT5659_DRC1_PRIV_1:
	case RT5659_DRC1_PRIV_4:
	case RT5659_DRC1_PRIV_5:
	case RT5659_DRC1_PRIV_6:
	case RT5659_DRC1_PRIV_7:
	case RT5659_DRC2_PRIV_1:
	case RT5659_DRC2_PRIV_4:
	case RT5659_DRC2_PRIV_5:
	case RT5659_DRC2_PRIV_6:
	case RT5659_DRC2_PRIV_7:
	case RT5659_ALC_PGA_STA_1:
	case RT5659_ALC_PGA_STA_2:
	case RT5659_ALC_PGA_STA_3:
		return true;
	default:
		return false;
	}
}

static bool rt5659_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT5659_RESET:
	case RT5659_SPO_VOL:
	case RT5659_HP_VOL:
	case RT5659_LOUT:
	case RT5659_MONO_OUT:
	case RT5659_HPL_GAIN:
	case RT5659_HPR_GAIN:
	case RT5659_MONO_GAIN:
	case RT5659_SPDIF_CTRL_1:
	case RT5659_SPDIF_CTRL_2:
	case RT5659_CAL_BST_CTRL:
	case RT5659_IN1_IN2:
	case RT5659_IN3_IN4:
	case RT5659_INL1_INR1_VOL:
	case RT5659_EJD_CTRL_1:
	case RT5659_EJD_CTRL_2:
	case RT5659_EJD_CTRL_3:
	case RT5659_SILENCE_CTRL:
	case RT5659_PSV_CTRL:
	case RT5659_SIDETONE_CTRL:
	case RT5659_DAC1_DIG_VOL:
	case RT5659_DAC2_DIG_VOL:
	case RT5659_DAC_CTRL:
	case RT5659_STO1_ADC_DIG_VOL:
	case RT5659_MONO_ADC_DIG_VOL:
	case RT5659_STO2_ADC_DIG_VOL:
	case RT5659_STO1_BOOST:
	case RT5659_MONO_BOOST:
	case RT5659_STO2_BOOST:
	case RT5659_HP_IMP_GAIN_1:
	case RT5659_HP_IMP_GAIN_2:
	case RT5659_STO1_ADC_MIXER:
	case RT5659_MONO_ADC_MIXER:
	case RT5659_AD_DA_MIXER:
	case RT5659_STO_DAC_MIXER:
	case RT5659_MONO_DAC_MIXER:
	case RT5659_DIG_MIXER:
	case RT5659_A_DAC_MUX:
	case RT5659_DIG_INF23_DATA:
	case RT5659_PDM_OUT_CTRL:
	case RT5659_PDM_DATA_CTRL_1:
	case RT5659_PDM_DATA_CTRL_2:
	case RT5659_PDM_DATA_CTRL_3:
	case RT5659_PDM_DATA_CTRL_4:
	case RT5659_SPDIF_CTRL:
	case RT5659_REC1_GAIN:
	case RT5659_REC1_L1_MIXER:
	case RT5659_REC1_L2_MIXER:
	case RT5659_REC1_R1_MIXER:
	case RT5659_REC1_R2_MIXER:
	case RT5659_CAL_REC:
	case RT5659_REC2_L1_MIXER:
	case RT5659_REC2_L2_MIXER:
	case RT5659_REC2_R1_MIXER:
	case RT5659_REC2_R2_MIXER:
	case RT5659_SPK_L_MIXER:
	case RT5659_SPK_R_MIXER:
	case RT5659_SPO_AMP_GAIN:
	case RT5659_ALC_BACK_GAIN:
	case RT5659_MONOMIX_GAIN:
	case RT5659_MONOMIX_IN_GAIN:
	case RT5659_OUT_L_GAIN:
	case RT5659_OUT_L_MIXER:
	case RT5659_OUT_R_GAIN:
	case RT5659_OUT_R_MIXER:
	case RT5659_LOUT_MIXER:
	case RT5659_HAPTIC_GEN_CTRL_1:
	case RT5659_HAPTIC_GEN_CTRL_2:
	case RT5659_HAPTIC_GEN_CTRL_3:
	case RT5659_HAPTIC_GEN_CTRL_4:
	case RT5659_HAPTIC_GEN_CTRL_5:
	case RT5659_HAPTIC_GEN_CTRL_6:
	case RT5659_HAPTIC_GEN_CTRL_7:
	case RT5659_HAPTIC_GEN_CTRL_8:
	case RT5659_HAPTIC_GEN_CTRL_9:
	case RT5659_HAPTIC_GEN_CTRL_10:
	case RT5659_HAPTIC_GEN_CTRL_11:
	case RT5659_HAPTIC_LPF_CTRL_1:
	case RT5659_HAPTIC_LPF_CTRL_2:
	case RT5659_HAPTIC_LPF_CTRL_3:
	case RT5659_PWR_DIG_1:
	case RT5659_PWR_DIG_2:
	case RT5659_PWR_ANLG_1:
	case RT5659_PWR_ANLG_2:
	case RT5659_PWR_ANLG_3:
	case RT5659_PWR_MIXER:
	case RT5659_PWR_VOL:
	case RT5659_PRIV_INDEX:
	case RT5659_CLK_DET:
	case RT5659_PRIV_DATA:
	case RT5659_PRE_DIV_1:
	case RT5659_PRE_DIV_2:
	case RT5659_I2S1_SDP:
	case RT5659_I2S2_SDP:
	case RT5659_I2S3_SDP:
	case RT5659_ADDA_CLK_1:
	case RT5659_ADDA_CLK_2:
	case RT5659_DMIC_CTRL_1:
	case RT5659_DMIC_CTRL_2:
	case RT5659_TDM_CTRL_1:
	case RT5659_TDM_CTRL_2:
	case RT5659_TDM_CTRL_3:
	case RT5659_TDM_CTRL_4:
	case RT5659_TDM_CTRL_5:
	case RT5659_GLB_CLK:
	case RT5659_PLL_CTRL_1:
	case RT5659_PLL_CTRL_2:
	case RT5659_ASRC_1:
	case RT5659_ASRC_2:
	case RT5659_ASRC_3:
	case RT5659_ASRC_4:
	case RT5659_ASRC_5:
	case RT5659_ASRC_6:
	case RT5659_ASRC_7:
	case RT5659_ASRC_8:
	case RT5659_ASRC_9:
	case RT5659_ASRC_10:
	case RT5659_DEPOP_1:
	case RT5659_DEPOP_2:
	case RT5659_DEPOP_3:
	case RT5659_HP_CHARGE_PUMP_1:
	case RT5659_HP_CHARGE_PUMP_2:
	case RT5659_MICBIAS_1:
	case RT5659_MICBIAS_2:
	case RT5659_ASRC_11:
	case RT5659_ASRC_12:
	case RT5659_ASRC_13:
	case RT5659_REC_M1_M2_GAIN_CTRL:
	case RT5659_RC_CLK_CTRL:
	case RT5659_CLASSD_CTRL_1:
	case RT5659_CLASSD_CTRL_2:
	case RT5659_ADC_EQ_CTRL_1:
	case RT5659_ADC_EQ_CTRL_2:
	case RT5659_DAC_EQ_CTRL_1:
	case RT5659_DAC_EQ_CTRL_2:
	case RT5659_DAC_EQ_CTRL_3:
	case RT5659_IRQ_CTRL_1:
	case RT5659_IRQ_CTRL_2:
	case RT5659_IRQ_CTRL_3:
	case RT5659_IRQ_CTRL_4:
	case RT5659_IRQ_CTRL_5:
	case RT5659_IRQ_CTRL_6:
	case RT5659_INT_ST_1:
	case RT5659_INT_ST_2:
	case RT5659_GPIO_CTRL_1:
	case RT5659_GPIO_CTRL_2:
	case RT5659_GPIO_CTRL_3:
	case RT5659_GPIO_CTRL_4:
	case RT5659_GPIO_CTRL_5:
	case RT5659_GPIO_STA:
	case RT5659_SINE_GEN_CTRL_1:
	case RT5659_SINE_GEN_CTRL_2:
	case RT5659_SINE_GEN_CTRL_3:
	case RT5659_HP_AMP_DET_CTRL_1:
	case RT5659_HP_AMP_DET_CTRL_2:
	case RT5659_SV_ZCD_1:
	case RT5659_SV_ZCD_2:
	case RT5659_IL_CMD_1:
	case RT5659_IL_CMD_2:
	case RT5659_IL_CMD_3:
	case RT5659_IL_CMD_4:
	case RT5659_4BTN_IL_CMD_1:
	case RT5659_4BTN_IL_CMD_2:
	case RT5659_4BTN_IL_CMD_3:
	case RT5659_PSV_IL_CMD_1:
	case RT5659_PSV_IL_CMD_2:
	case RT5659_ADC_STO1_HP_CTRL_1:
	case RT5659_ADC_STO1_HP_CTRL_2:
	case RT5659_ADC_MONO_HP_CTRL_1:
	case RT5659_ADC_MONO_HP_CTRL_2:
	case RT5659_AJD1_CTRL:
	case RT5659_AJD2_AJD3_CTRL:
	case RT5659_JD1_THD:
	case RT5659_JD2_THD:
	case RT5659_JD3_THD:
	case RT5659_JD_CTRL_1:
	case RT5659_JD_CTRL_2:
	case RT5659_JD_CTRL_3:
	case RT5659_JD_CTRL_4:
	case RT5659_DIG_MISC:
	case RT5659_DUMMY_2:
	case RT5659_DUMMY_3:
	case RT5659_VENDOR_ID:
	case RT5659_VENDOR_ID_1:
	case RT5659_DEVICE_ID:
	case RT5659_DAC_ADC_DIG_VOL:
	case RT5659_BIAS_CUR_CTRL_1:
	case RT5659_BIAS_CUR_CTRL_2:
	case RT5659_BIAS_CUR_CTRL_3:
	case RT5659_BIAS_CUR_CTRL_4:
	case RT5659_BIAS_CUR_CTRL_5:
	case RT5659_BIAS_CUR_CTRL_6:
	case RT5659_BIAS_CUR_CTRL_7:
	case RT5659_BIAS_CUR_CTRL_8:
	case RT5659_BIAS_CUR_CTRL_9:
	case RT5659_BIAS_CUR_CTRL_10:
	case RT5659_MEMORY_TEST:
	case RT5659_VREF_REC_OP_FB_CAP_CTRL:
	case RT5659_CLASSD_0:
	case RT5659_CLASSD_1:
	case RT5659_CLASSD_2:
	case RT5659_CLASSD_3:
	case RT5659_CLASSD_4:
	case RT5659_CLASSD_5:
	case RT5659_CLASSD_6:
	case RT5659_CLASSD_7:
	case RT5659_CLASSD_8:
	case RT5659_CLASSD_9:
	case RT5659_CLASSD_10:
	case RT5659_CHARGE_PUMP_1:
	case RT5659_CHARGE_PUMP_2:
	case RT5659_DIG_IN_CTRL_1:
	case RT5659_DIG_IN_CTRL_2:
	case RT5659_PAD_DRIVING_CTRL:
	case RT5659_SOFT_RAMP_DEPOP:
	case RT5659_PLL:
	case RT5659_CHOP_DAC:
	case RT5659_CHOP_ADC:
	case RT5659_CALIB_ADC_CTRL:
	case RT5659_SOFT_RAMP_DEPOP_DAC_CLK_CTRL:
	case RT5659_VOL_TEST:
	case RT5659_TEST_MODE_CTRL_1:
	case RT5659_TEST_MODE_CTRL_2:
	case RT5659_TEST_MODE_CTRL_3:
	case RT5659_TEST_MODE_CTRL_4:
	case RT5659_BASSBACK_CTRL:
	case RT5659_MP3_PLUS_CTRL_1:
	case RT5659_MP3_PLUS_CTRL_2:
	case RT5659_MP3_HPF_A1:
	case RT5659_MP3_HPF_A2:
	case RT5659_MP3_HPF_H0:
	case RT5659_MP3_LPF_H0:
	case RT5659_3D_SPK_CTRL:
	case RT5659_3D_SPK_COEF_1:
	case RT5659_3D_SPK_COEF_2:
	case RT5659_3D_SPK_COEF_3:
	case RT5659_3D_SPK_COEF_4:
	case RT5659_3D_SPK_COEF_5:
	case RT5659_3D_SPK_COEF_6:
	case RT5659_3D_SPK_COEF_7:
	case RT5659_STO_NG2_CTRL_1:
	case RT5659_STO_NG2_CTRL_2:
	case RT5659_STO_NG2_CTRL_3:
	case RT5659_STO_NG2_CTRL_4:
	case RT5659_STO_NG2_CTRL_5:
	case RT5659_STO_NG2_CTRL_6:
	case RT5659_STO_NG2_CTRL_7:
	case RT5659_STO_NG2_CTRL_8:
	case RT5659_MONO_NG2_CTRL_1:
	case RT5659_MONO_NG2_CTRL_2:
	case RT5659_MONO_NG2_CTRL_3:
	case RT5659_MONO_NG2_CTRL_4:
	case RT5659_MONO_NG2_CTRL_5:
	case RT5659_MONO_NG2_CTRL_6:
	case RT5659_MID_HP_AMP_DET:
	case RT5659_LOW_HP_AMP_DET:
	case RT5659_LDO_CTRL:
	case RT5659_HP_DECROSS_CTRL_1:
	case RT5659_HP_DECROSS_CTRL_2:
	case RT5659_HP_DECROSS_CTRL_3:
	case RT5659_HP_DECROSS_CTRL_4:
	case RT5659_HP_IMP_SENS_CTRL_1:
	case RT5659_HP_IMP_SENS_CTRL_2:
	case RT5659_HP_IMP_SENS_CTRL_3:
	case RT5659_HP_IMP_SENS_CTRL_4:
	case RT5659_HP_IMP_SENS_MAP_1:
	case RT5659_HP_IMP_SENS_MAP_2:
	case RT5659_HP_IMP_SENS_MAP_3:
	case RT5659_HP_IMP_SENS_MAP_4:
	case RT5659_HP_IMP_SENS_MAP_5:
	case RT5659_HP_IMP_SENS_MAP_6:
	case RT5659_HP_IMP_SENS_MAP_7:
	case RT5659_HP_IMP_SENS_MAP_8:
	case RT5659_HP_LOGIC_CTRL_1:
	case RT5659_HP_LOGIC_CTRL_2:
	case RT5659_HP_CALIB_CTRL_1:
	case RT5659_HP_CALIB_CTRL_2:
	case RT5659_HP_CALIB_CTRL_3:
	case RT5659_HP_CALIB_CTRL_4:
	case RT5659_HP_CALIB_CTRL_5:
	case RT5659_HP_CALIB_CTRL_6:
	case RT5659_HP_CALIB_CTRL_7:
	case RT5659_HP_CALIB_CTRL_9:
	case RT5659_HP_CALIB_CTRL_10:
	case RT5659_HP_CALIB_CTRL_11:
	case RT5659_HP_CALIB_STA_1:
	case RT5659_HP_CALIB_STA_2:
	case RT5659_HP_CALIB_STA_3:
	case RT5659_HP_CALIB_STA_4:
	case RT5659_HP_CALIB_STA_5:
	case RT5659_HP_CALIB_STA_6:
	case RT5659_HP_CALIB_STA_7:
	case RT5659_HP_CALIB_STA_8:
	case RT5659_HP_CALIB_STA_9:
	case RT5659_MONO_AMP_CALIB_CTRL_1:
	case RT5659_MONO_AMP_CALIB_CTRL_2:
	case RT5659_MONO_AMP_CALIB_CTRL_3:
	case RT5659_MONO_AMP_CALIB_CTRL_4:
	case RT5659_MONO_AMP_CALIB_CTRL_5:
	case RT5659_MONO_AMP_CALIB_STA_1:
	case RT5659_MONO_AMP_CALIB_STA_2:
	case RT5659_MONO_AMP_CALIB_STA_3:
	case RT5659_MONO_AMP_CALIB_STA_4:
	case RT5659_SPK_PWR_LMT_CTRL_1:
	case RT5659_SPK_PWR_LMT_CTRL_2:
	case RT5659_SPK_PWR_LMT_CTRL_3:
	case RT5659_SPK_PWR_LMT_STA_1:
	case RT5659_SPK_PWR_LMT_STA_2:
	case RT5659_SPK_PWR_LMT_STA_3:
	case RT5659_SPK_PWR_LMT_STA_4:
	case RT5659_SPK_PWR_LMT_STA_5:
	case RT5659_SPK_PWR_LMT_STA_6:
	case RT5659_FLEX_SPK_BST_CTRL_1:
	case RT5659_FLEX_SPK_BST_CTRL_2:
	case RT5659_FLEX_SPK_BST_CTRL_3:
	case RT5659_FLEX_SPK_BST_CTRL_4:
	case RT5659_SPK_EX_LMT_CTRL_1:
	case RT5659_SPK_EX_LMT_CTRL_2:
	case RT5659_SPK_EX_LMT_CTRL_3:
	case RT5659_SPK_EX_LMT_CTRL_4:
	case RT5659_SPK_EX_LMT_CTRL_5:
	case RT5659_SPK_EX_LMT_CTRL_6:
	case RT5659_SPK_EX_LMT_CTRL_7:
	case RT5659_ADJ_HPF_CTRL_1:
	case RT5659_ADJ_HPF_CTRL_2:
	case RT5659_SPK_DC_CAILB_CTRL_1:
	case RT5659_SPK_DC_CAILB_CTRL_2:
	case RT5659_SPK_DC_CAILB_CTRL_3:
	case RT5659_SPK_DC_CAILB_CTRL_4:
	case RT5659_SPK_DC_CAILB_CTRL_5:
	case RT5659_SPK_DC_CAILB_STA_1:
	case RT5659_SPK_DC_CAILB_STA_2:
	case RT5659_SPK_DC_CAILB_STA_3:
	case RT5659_SPK_DC_CAILB_STA_4:
	case RT5659_SPK_DC_CAILB_STA_5:
	case RT5659_SPK_DC_CAILB_STA_6:
	case RT5659_SPK_DC_CAILB_STA_7:
	case RT5659_SPK_DC_CAILB_STA_8:
	case RT5659_SPK_DC_CAILB_STA_9:
	case RT5659_SPK_DC_CAILB_STA_10:
	case RT5659_SPK_VDD_STA_1:
	case RT5659_SPK_VDD_STA_2:
	case RT5659_SPK_DC_DET_CTRL_1:
	case RT5659_SPK_DC_DET_CTRL_2:
	case RT5659_SPK_DC_DET_CTRL_3:
	case RT5659_PURE_DC_DET_CTRL_1:
	case RT5659_PURE_DC_DET_CTRL_2:
	case RT5659_DUMMY_4:
	case RT5659_DUMMY_5:
	case RT5659_DUMMY_6:
	case RT5659_DRC1_CTRL_1:
	case RT5659_DRC1_CTRL_2:
	case RT5659_DRC1_CTRL_3:
	case RT5659_DRC1_CTRL_4:
	case RT5659_DRC1_CTRL_5:
	case RT5659_DRC1_CTRL_6:
	case RT5659_DRC1_HARD_LMT_CTRL_1:
	case RT5659_DRC1_HARD_LMT_CTRL_2:
	case RT5659_DRC2_CTRL_1:
	case RT5659_DRC2_CTRL_2:
	case RT5659_DRC2_CTRL_3:
	case RT5659_DRC2_CTRL_4:
	case RT5659_DRC2_CTRL_5:
	case RT5659_DRC2_CTRL_6:
	case RT5659_DRC2_HARD_LMT_CTRL_1:
	case RT5659_DRC2_HARD_LMT_CTRL_2:
	case RT5659_DRC1_PRIV_1:
	case RT5659_DRC1_PRIV_2:
	case RT5659_DRC1_PRIV_3:
	case RT5659_DRC1_PRIV_4:
	case RT5659_DRC1_PRIV_5:
	case RT5659_DRC1_PRIV_6:
	case RT5659_DRC1_PRIV_7:
	case RT5659_DRC2_PRIV_1:
	case RT5659_DRC2_PRIV_2:
	case RT5659_DRC2_PRIV_3:
	case RT5659_DRC2_PRIV_4:
	case RT5659_DRC2_PRIV_5:
	case RT5659_DRC2_PRIV_6:
	case RT5659_DRC2_PRIV_7:
	case RT5659_MULTI_DRC_CTRL:
	case RT5659_CROSS_OVER_1:
	case RT5659_CROSS_OVER_2:
	case RT5659_CROSS_OVER_3:
	case RT5659_CROSS_OVER_4:
	case RT5659_CROSS_OVER_5:
	case RT5659_CROSS_OVER_6:
	case RT5659_CROSS_OVER_7:
	case RT5659_CROSS_OVER_8:
	case RT5659_CROSS_OVER_9:
	case RT5659_CROSS_OVER_10:
	case RT5659_ALC_PGA_CTRL_1:
	case RT5659_ALC_PGA_CTRL_2:
	case RT5659_ALC_PGA_CTRL_3:
	case RT5659_ALC_PGA_CTRL_4:
	case RT5659_ALC_PGA_CTRL_5:
	case RT5659_ALC_PGA_CTRL_6:
	case RT5659_ALC_PGA_CTRL_7:
	case RT5659_ALC_PGA_CTRL_8:
	case RT5659_ALC_PGA_STA_1:
	case RT5659_ALC_PGA_STA_2:
	case RT5659_ALC_PGA_STA_3:
	case RT5659_DAC_L_EQ_PRE_VOL:
	case RT5659_DAC_R_EQ_PRE_VOL:
	case RT5659_DAC_L_EQ_POST_VOL:
	case RT5659_DAC_R_EQ_POST_VOL:
	case RT5659_DAC_L_EQ_LPF1_A1:
	case RT5659_DAC_L_EQ_LPF1_H0:
	case RT5659_DAC_R_EQ_LPF1_A1:
	case RT5659_DAC_R_EQ_LPF1_H0:
	case RT5659_DAC_L_EQ_BPF2_A1:
	case RT5659_DAC_L_EQ_BPF2_A2:
	case RT5659_DAC_L_EQ_BPF2_H0:
	case RT5659_DAC_R_EQ_BPF2_A1:
	case RT5659_DAC_R_EQ_BPF2_A2:
	case RT5659_DAC_R_EQ_BPF2_H0:
	case RT5659_DAC_L_EQ_BPF3_A1:
	case RT5659_DAC_L_EQ_BPF3_A2:
	case RT5659_DAC_L_EQ_BPF3_H0:
	case RT5659_DAC_R_EQ_BPF3_A1:
	case RT5659_DAC_R_EQ_BPF3_A2:
	case RT5659_DAC_R_EQ_BPF3_H0:
	case RT5659_DAC_L_EQ_BPF4_A1:
	case RT5659_DAC_L_EQ_BPF4_A2:
	case RT5659_DAC_L_EQ_BPF4_H0:
	case RT5659_DAC_R_EQ_BPF4_A1:
	case RT5659_DAC_R_EQ_BPF4_A2:
	case RT5659_DAC_R_EQ_BPF4_H0:
	case RT5659_DAC_L_EQ_HPF1_A1:
	case RT5659_DAC_L_EQ_HPF1_H0:
	case RT5659_DAC_R_EQ_HPF1_A1:
	case RT5659_DAC_R_EQ_HPF1_H0:
	case RT5659_DAC_L_EQ_HPF2_A1:
	case RT5659_DAC_L_EQ_HPF2_A2:
	case RT5659_DAC_L_EQ_HPF2_H0:
	case RT5659_DAC_R_EQ_HPF2_A1:
	case RT5659_DAC_R_EQ_HPF2_A2:
	case RT5659_DAC_R_EQ_HPF2_H0:
	case RT5659_DAC_L_BI_EQ_BPF1_H0_1:
	case RT5659_DAC_L_BI_EQ_BPF1_H0_2:
	case RT5659_DAC_L_BI_EQ_BPF1_B1_1:
	case RT5659_DAC_L_BI_EQ_BPF1_B1_2:
	case RT5659_DAC_L_BI_EQ_BPF1_B2_1:
	case RT5659_DAC_L_BI_EQ_BPF1_B2_2:
	case RT5659_DAC_L_BI_EQ_BPF1_A1_1:
	case RT5659_DAC_L_BI_EQ_BPF1_A1_2:
	case RT5659_DAC_L_BI_EQ_BPF1_A2_1:
	case RT5659_DAC_L_BI_EQ_BPF1_A2_2:
	case RT5659_DAC_R_BI_EQ_BPF1_H0_1:
	case RT5659_DAC_R_BI_EQ_BPF1_H0_2:
	case RT5659_DAC_R_BI_EQ_BPF1_B1_1:
	case RT5659_DAC_R_BI_EQ_BPF1_B1_2:
	case RT5659_DAC_R_BI_EQ_BPF1_B2_1:
	case RT5659_DAC_R_BI_EQ_BPF1_B2_2:
	case RT5659_DAC_R_BI_EQ_BPF1_A1_1:
	case RT5659_DAC_R_BI_EQ_BPF1_A1_2:
	case RT5659_DAC_R_BI_EQ_BPF1_A2_1:
	case RT5659_DAC_R_BI_EQ_BPF1_A2_2:
	case RT5659_ADC_L_EQ_LPF1_A1:
	case RT5659_ADC_R_EQ_LPF1_A1:
	case RT5659_ADC_L_EQ_LPF1_H0:
	case RT5659_ADC_R_EQ_LPF1_H0:
	case RT5659_ADC_L_EQ_BPF1_A1:
	case RT5659_ADC_R_EQ_BPF1_A1:
	case RT5659_ADC_L_EQ_BPF1_A2:
	case RT5659_ADC_R_EQ_BPF1_A2:
	case RT5659_ADC_L_EQ_BPF1_H0:
	case RT5659_ADC_R_EQ_BPF1_H0:
	case RT5659_ADC_L_EQ_BPF2_A1:
	case RT5659_ADC_R_EQ_BPF2_A1:
	case RT5659_ADC_L_EQ_BPF2_A2:
	case RT5659_ADC_R_EQ_BPF2_A2:
	case RT5659_ADC_L_EQ_BPF2_H0:
	case RT5659_ADC_R_EQ_BPF2_H0:
	case RT5659_ADC_L_EQ_BPF3_A1:
	case RT5659_ADC_R_EQ_BPF3_A1:
	case RT5659_ADC_L_EQ_BPF3_A2:
	case RT5659_ADC_R_EQ_BPF3_A2:
	case RT5659_ADC_L_EQ_BPF3_H0:
	case RT5659_ADC_R_EQ_BPF3_H0:
	case RT5659_ADC_L_EQ_BPF4_A1:
	case RT5659_ADC_R_EQ_BPF4_A1:
	case RT5659_ADC_L_EQ_BPF4_A2:
	case RT5659_ADC_R_EQ_BPF4_A2:
	case RT5659_ADC_L_EQ_BPF4_H0:
	case RT5659_ADC_R_EQ_BPF4_H0:
	case RT5659_ADC_L_EQ_HPF1_A1:
	case RT5659_ADC_R_EQ_HPF1_A1:
	case RT5659_ADC_L_EQ_HPF1_H0:
	case RT5659_ADC_R_EQ_HPF1_H0:
	case RT5659_ADC_L_EQ_PRE_VOL:
	case RT5659_ADC_R_EQ_PRE_VOL:
	case RT5659_ADC_L_EQ_POST_VOL:
	case RT5659_ADC_R_EQ_POST_VOL:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(hp_vol_tlv, -2325, 75, 0);
static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -65625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);
static const DECLARE_TLV_DB_SCALE(in_bst_tlv, -1200, 75, 0);

/* Interface data select */
static const char * const rt5659_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt5659_if1_01_adc_enum,
	RT5659_TDM_CTRL_2, RT5659_DS_ADC_SLOT01_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if1_23_adc_enum,
	RT5659_TDM_CTRL_2, RT5659_DS_ADC_SLOT23_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if1_45_adc_enum,
	RT5659_TDM_CTRL_2, RT5659_DS_ADC_SLOT45_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if1_67_adc_enum,
	RT5659_TDM_CTRL_2, RT5659_DS_ADC_SLOT67_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if2_dac_enum,
	RT5659_DIG_INF23_DATA, RT5659_IF2_DAC_SEL_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if2_adc_enum,
	RT5659_DIG_INF23_DATA, RT5659_IF2_ADC_SEL_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if3_dac_enum,
	RT5659_DIG_INF23_DATA, RT5659_IF3_DAC_SEL_SFT, rt5659_data_select);

static SOC_ENUM_SINGLE_DECL(rt5659_if3_adc_enum,
	RT5659_DIG_INF23_DATA, RT5659_IF3_ADC_SEL_SFT, rt5659_data_select);

static const struct snd_kcontrol_new rt5659_if1_01_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 01 ADC Swap Source", rt5659_if1_01_adc_enum);

static const struct snd_kcontrol_new rt5659_if1_23_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 23 ADC1 Swap Source", rt5659_if1_23_adc_enum);

static const struct snd_kcontrol_new rt5659_if1_45_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 45 ADC1 Swap Source", rt5659_if1_45_adc_enum);

static const struct snd_kcontrol_new rt5659_if1_67_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 67 ADC1 Swap Source", rt5659_if1_67_adc_enum);

static const struct snd_kcontrol_new rt5659_if2_dac_swap_mux =
	SOC_DAPM_ENUM("IF2 DAC Swap Source", rt5659_if2_dac_enum);

static const struct snd_kcontrol_new rt5659_if2_adc_swap_mux =
	SOC_DAPM_ENUM("IF2 ADC Swap Source", rt5659_if2_adc_enum);

static const struct snd_kcontrol_new rt5659_if3_dac_swap_mux =
	SOC_DAPM_ENUM("IF3 DAC Swap Source", rt5659_if3_dac_enum);

static const struct snd_kcontrol_new rt5659_if3_adc_swap_mux =
	SOC_DAPM_ENUM("IF3 ADC Swap Source", rt5659_if3_adc_enum);

static int rt5659_hp_vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int ret = snd_soc_put_volsw(kcontrol, ucontrol);

	if (snd_soc_component_read(component, RT5659_STO_NG2_CTRL_1) & RT5659_NG2_EN) {
		snd_soc_component_update_bits(component, RT5659_STO_NG2_CTRL_1,
			RT5659_NG2_EN_MASK, RT5659_NG2_DIS);
		snd_soc_component_update_bits(component, RT5659_STO_NG2_CTRL_1,
			RT5659_NG2_EN_MASK, RT5659_NG2_EN);
	}

	return ret;
}

static void rt5659_enable_push_button_irq(struct snd_soc_component *component,
	bool enable)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	if (enable) {
		snd_soc_component_write(component, RT5659_4BTN_IL_CMD_1, 0x000b);

		/* MICBIAS1 and Mic Det Power for button detect*/
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS1");
		snd_soc_dapm_force_enable_pin(dapm,
			"Mic Det Power");
		snd_soc_dapm_sync(dapm);

		snd_soc_component_update_bits(component, RT5659_PWR_ANLG_2,
			RT5659_PWR_MB1, RT5659_PWR_MB1);
		snd_soc_component_update_bits(component, RT5659_PWR_VOL,
			RT5659_PWR_MIC_DET, RT5659_PWR_MIC_DET);

		snd_soc_component_update_bits(component, RT5659_IRQ_CTRL_2,
				RT5659_IL_IRQ_MASK, RT5659_IL_IRQ_EN);
		snd_soc_component_update_bits(component, RT5659_4BTN_IL_CMD_2,
				RT5659_4BTN_IL_MASK, RT5659_4BTN_IL_EN);
	} else {
		snd_soc_component_update_bits(component, RT5659_4BTN_IL_CMD_2,
				RT5659_4BTN_IL_MASK, RT5659_4BTN_IL_DIS);
		snd_soc_component_update_bits(component, RT5659_IRQ_CTRL_2,
				RT5659_IL_IRQ_MASK, RT5659_IL_IRQ_DIS);
		/* MICBIAS1 and Mic Det Power for button detect*/
		snd_soc_dapm_disable_pin(dapm, "MICBIAS1");
		snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
		snd_soc_dapm_sync(dapm);
	}
}

/**
 * rt5659_headset_detect - Detect headset.
 * @component: SoC audio component device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */

static int rt5659_headset_detect(struct snd_soc_component *component, int jack_insert)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int val, i = 0, sleep_time[5] = {300, 150, 100, 50, 30};
	int reg_63;

	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	if (jack_insert) {
		snd_soc_dapm_force_enable_pin(dapm,
			"Mic Det Power");
		snd_soc_dapm_sync(dapm);
		reg_63 = snd_soc_component_read(component, RT5659_PWR_ANLG_1);

		snd_soc_component_update_bits(component, RT5659_PWR_ANLG_1,
			RT5659_PWR_VREF2 | RT5659_PWR_MB,
			RT5659_PWR_VREF2 | RT5659_PWR_MB);
		msleep(20);
		snd_soc_component_update_bits(component, RT5659_PWR_ANLG_1,
			RT5659_PWR_FV2, RT5659_PWR_FV2);

		snd_soc_component_write(component, RT5659_EJD_CTRL_2, 0x4160);
		snd_soc_component_update_bits(component, RT5659_EJD_CTRL_1,
			0x20, 0x0);
		msleep(20);
		snd_soc_component_update_bits(component, RT5659_EJD_CTRL_1,
			0x20, 0x20);

		while (i < 5) {
			msleep(sleep_time[i]);
			val = snd_soc_component_read(component, RT5659_EJD_CTRL_2) & 0x0003;
			i++;
			if (val == 0x1 || val == 0x2 || val == 0x3)
				break;
		}

		switch (val) {
		case 1:
			rt5659->jack_type = SND_JACK_HEADSET;
			rt5659_enable_push_button_irq(component, true);
			break;
		default:
			snd_soc_component_write(component, RT5659_PWR_ANLG_1, reg_63);
			rt5659->jack_type = SND_JACK_HEADPHONE;
			snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
			snd_soc_dapm_sync(dapm);
			break;
		}
	} else {
		snd_soc_dapm_disable_pin(dapm, "Mic Det Power");
		snd_soc_dapm_sync(dapm);
		if (rt5659->jack_type == SND_JACK_HEADSET)
			rt5659_enable_push_button_irq(component, false);
		rt5659->jack_type = 0;
	}

	dev_dbg(component->dev, "jack_type = %d\n", rt5659->jack_type);
	return rt5659->jack_type;
}

static int rt5659_button_detect(struct snd_soc_component *component)
{
	int btn_type, val;

	val = snd_soc_component_read(component, RT5659_4BTN_IL_CMD_1);
	btn_type = val & 0xfff0;
	snd_soc_component_write(component, RT5659_4BTN_IL_CMD_1, val);

	return btn_type;
}

static irqreturn_t rt5659_irq(int irq, void *data)
{
	struct rt5659_priv *rt5659 = data;

	queue_delayed_work(system_power_efficient_wq,
			   &rt5659->jack_detect_work, msecs_to_jiffies(250));

	return IRQ_HANDLED;
}

int rt5659_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	rt5659->hs_jack = hs_jack;

	rt5659_irq(0, rt5659);

	return 0;
}
EXPORT_SYMBOL_GPL(rt5659_set_jack_detect);

static void rt5659_jack_detect_work(struct work_struct *work)
{
	struct rt5659_priv *rt5659 =
		container_of(work, struct rt5659_priv, jack_detect_work.work);
	int val, btn_type, report = 0;

	if (!rt5659->component)
		return;

	val = snd_soc_component_read(rt5659->component, RT5659_INT_ST_1) & 0x0080;
	if (!val) {
		/* jack in */
		if (rt5659->jack_type == 0) {
			/* jack was out, report jack type */
			report = rt5659_headset_detect(rt5659->component, 1);
		} else {
			/* jack is already in, report button event */
			report = SND_JACK_HEADSET;
			btn_type = rt5659_button_detect(rt5659->component);
			/**
			 * rt5659 can report three kinds of button behavior,
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
				dev_err(rt5659->component->dev,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}

			/* button release or spurious interrput*/
			if (btn_type == 0)
				report =  rt5659->jack_type;
		}
	} else {
		/* jack out */
		report = rt5659_headset_detect(rt5659->component, 0);
	}

	snd_soc_jack_report(rt5659->hs_jack, report, SND_JACK_HEADSET |
			    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
			    SND_JACK_BTN_2 | SND_JACK_BTN_3);
}

static void rt5659_jack_detect_intel_hd_header(struct work_struct *work)
{
	struct rt5659_priv *rt5659 =
		container_of(work, struct rt5659_priv, jack_detect_work.work);
	unsigned int value;
	bool hp_flag, mic_flag;

	if (!rt5659->hs_jack)
		return;

	/* headphone jack */
	regmap_read(rt5659->regmap, RT5659_GPIO_STA, &value);
	hp_flag = (!(value & 0x8)) ? true : false;

	if (hp_flag != rt5659->hda_hp_plugged) {
		rt5659->hda_hp_plugged = hp_flag;

		if (hp_flag) {
			regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_1,
				0x10, 0x0);
			rt5659->jack_type |= SND_JACK_HEADPHONE;
		} else {
			regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_1,
				0x10, 0x10);
			rt5659->jack_type = rt5659->jack_type &
				(~SND_JACK_HEADPHONE);
		}

		snd_soc_jack_report(rt5659->hs_jack, rt5659->jack_type,
			SND_JACK_HEADPHONE);
	}

	/* mic jack */
	regmap_read(rt5659->regmap, RT5659_4BTN_IL_CMD_1, &value);
	regmap_write(rt5659->regmap, RT5659_4BTN_IL_CMD_1, value);
	mic_flag = (value & 0x2000) ? true : false;

	if (mic_flag != rt5659->hda_mic_plugged) {
		rt5659->hda_mic_plugged = mic_flag;
		if (mic_flag) {
			regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_2,
				0x2, 0x2);
			rt5659->jack_type |= SND_JACK_MICROPHONE;
		} else {
			regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_2,
				0x2, 0x0);
			rt5659->jack_type = rt5659->jack_type
				& (~SND_JACK_MICROPHONE);
		}

		snd_soc_jack_report(rt5659->hs_jack, rt5659->jack_type,
			SND_JACK_MICROPHONE);
	}
}

static const struct snd_kcontrol_new rt5659_snd_controls[] = {
	/* Speaker Output Volume */
	SOC_DOUBLE_TLV("Speaker Playback Volume", RT5659_SPO_VOL,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 39, 1, out_vol_tlv),

	/* Headphone Output Volume */
	SOC_DOUBLE_R_EXT_TLV("Headphone Playback Volume", RT5659_HPL_GAIN,
		RT5659_HPR_GAIN, RT5659_G_HP_SFT, 31, 1, snd_soc_get_volsw,
		rt5659_hp_vol_put, hp_vol_tlv),

	/* Mono Output Volume */
	SOC_SINGLE_TLV("Mono Playback Volume", RT5659_MONO_OUT,
		RT5659_L_VOL_SFT, 39, 1, out_vol_tlv),

	/* Output Volume */
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5659_LOUT,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 39, 1, out_vol_tlv),

	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5659_DAC1_DIG_VOL,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 175, 0, dac_vol_tlv),
	SOC_DOUBLE("DAC1 Playback Switch", RT5659_AD_DA_MIXER,
		RT5659_M_DAC1_L_SFT, RT5659_M_DAC1_R_SFT, 1, 1),

	SOC_DOUBLE_TLV("DAC2 Playback Volume", RT5659_DAC2_DIG_VOL,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 175, 0, dac_vol_tlv),
	SOC_DOUBLE("DAC2 Playback Switch", RT5659_DAC_CTRL,
		RT5659_M_DAC2_L_VOL_SFT, RT5659_M_DAC2_R_VOL_SFT, 1, 1),

	/* IN1/IN2/IN3/IN4 Volume */
	SOC_SINGLE_TLV("IN1 Boost Volume", RT5659_IN1_IN2,
		RT5659_BST1_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost Volume", RT5659_IN1_IN2,
		RT5659_BST2_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("IN3 Boost Volume", RT5659_IN3_IN4,
		RT5659_BST3_SFT, 69, 0, in_bst_tlv),
	SOC_SINGLE_TLV("IN4 Boost Volume", RT5659_IN3_IN4,
		RT5659_BST4_SFT, 69, 0, in_bst_tlv),

	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5659_INL1_INR1_VOL,
		RT5659_INL_VOL_SFT, RT5659_INR_VOL_SFT, 31, 1, in_vol_tlv),

	/* ADC Digital Volume Control */
	SOC_DOUBLE("STO1 ADC Capture Switch", RT5659_STO1_ADC_DIG_VOL,
		RT5659_L_MUTE_SFT, RT5659_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("STO1 ADC Capture Volume", RT5659_STO1_ADC_DIG_VOL,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 127, 0, adc_vol_tlv),
	SOC_DOUBLE("Mono ADC Capture Switch", RT5659_MONO_ADC_DIG_VOL,
		RT5659_L_MUTE_SFT, RT5659_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5659_MONO_ADC_DIG_VOL,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 127, 0, adc_vol_tlv),
	SOC_DOUBLE("STO2 ADC Capture Switch", RT5659_STO2_ADC_DIG_VOL,
		RT5659_L_MUTE_SFT, RT5659_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("STO2 ADC Capture Volume", RT5659_STO2_ADC_DIG_VOL,
		RT5659_L_VOL_SFT, RT5659_R_VOL_SFT, 127, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain Volume", RT5659_STO1_BOOST,
		RT5659_STO1_ADC_L_BST_SFT, RT5659_STO1_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),

	SOC_DOUBLE_TLV("Mono ADC Boost Gain Volume", RT5659_MONO_BOOST,
		RT5659_MONO_ADC_L_BST_SFT, RT5659_MONO_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),

	SOC_DOUBLE_TLV("STO2 ADC Boost Gain Volume", RT5659_STO2_BOOST,
		RT5659_STO2_ADC_L_BST_SFT, RT5659_STO2_ADC_R_BST_SFT,
		3, 0, adc_bst_tlv),

	SOC_SINGLE("DAC IF1 DAC1 L Data Switch", RT5659_TDM_CTRL_4, 12, 7, 0),
	SOC_SINGLE("DAC IF1 DAC1 R Data Switch", RT5659_TDM_CTRL_4, 8, 7, 0),
	SOC_SINGLE("DAC IF1 DAC2 L Data Switch", RT5659_TDM_CTRL_4, 4, 7, 0),
	SOC_SINGLE("DAC IF1 DAC2 R Data Switch", RT5659_TDM_CTRL_4, 0, 7, 0),
};

/**
 * set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 * Choose dmic clock between 1MHz and 3MHz.
 * It is better for clock to approximate 3MHz.
 */
static int set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);
	int pd, idx;

	pd = rl6231_get_pre_div(rt5659->regmap,
		RT5659_ADDA_CLK_1, RT5659_I2S_PD1_SFT);
	idx = rl6231_calc_dmic_clk(rt5659->sysclk / pd);

	if (idx < 0)
		dev_err(component->dev, "Failed to set DMIC clock\n");
	else {
		snd_soc_component_update_bits(component, RT5659_DMIC_CTRL_1,
			RT5659_DMIC_CLK_MASK, idx << RT5659_DMIC_CLK_SFT);
	}
	return idx;
}

static int set_adc1_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5659_CHOP_ADC,
			RT5659_CKXEN_ADC1_MASK | RT5659_CKGEN_ADC1_MASK,
			RT5659_CKXEN_ADC1_MASK | RT5659_CKGEN_ADC1_MASK);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5659_CHOP_ADC,
			RT5659_CKXEN_ADC1_MASK | RT5659_CKGEN_ADC1_MASK, 0);
		break;

	default:
		return 0;
	}

	return 0;

}

static int set_adc2_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5659_CHOP_ADC,
			RT5659_CKXEN_ADC2_MASK | RT5659_CKGEN_ADC2_MASK,
			RT5659_CKXEN_ADC2_MASK | RT5659_CKGEN_ADC2_MASK);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5659_CHOP_ADC,
			RT5659_CKXEN_ADC2_MASK | RT5659_CKGEN_ADC2_MASK, 0);
		break;

	default:
		return 0;
	}

	return 0;

}

static int rt5659_charge_pump_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Depop */
		snd_soc_component_write(component, RT5659_DEPOP_1, 0x0009);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write(component, RT5659_HP_CHARGE_PUMP_1, 0x0c16);
		break;
	default:
		return 0;
	}

	return 0;
}

static int is_sys_clk_from_pll(struct snd_soc_dapm_widget *w,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int val;
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	val = snd_soc_component_read(component, RT5659_GLB_CLK);
	val &= RT5659_SCLK_SRC_MASK;
	if (val == RT5659_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

static int is_using_asrc(struct snd_soc_dapm_widget *w,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int reg, shift, val;
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (w->shift) {
	case RT5659_ADC_MONO_R_ASRC_SFT:
		reg = RT5659_ASRC_3;
		shift = RT5659_AD_MONO_R_T_SFT;
		break;
	case RT5659_ADC_MONO_L_ASRC_SFT:
		reg = RT5659_ASRC_3;
		shift = RT5659_AD_MONO_L_T_SFT;
		break;
	case RT5659_ADC_STO1_ASRC_SFT:
		reg = RT5659_ASRC_2;
		shift = RT5659_AD_STO1_T_SFT;
		break;
	case RT5659_DAC_MONO_R_ASRC_SFT:
		reg = RT5659_ASRC_2;
		shift = RT5659_DA_MONO_R_T_SFT;
		break;
	case RT5659_DAC_MONO_L_ASRC_SFT:
		reg = RT5659_ASRC_2;
		shift = RT5659_DA_MONO_L_T_SFT;
		break;
	case RT5659_DAC_STO_ASRC_SFT:
		reg = RT5659_ASRC_2;
		shift = RT5659_DA_STO_T_SFT;
		break;
	default:
		return 0;
	}

	val = (snd_soc_component_read(component, reg) >> shift) & 0xf;
	switch (val) {
	case 1:
	case 2:
	case 3:
		/* I2S_Pre_Div1 should be 1 in asrc mode */
		snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1,
			RT5659_I2S_PD1_MASK, RT5659_I2S_PD1_2);
		return 1;
	default:
		return 0;
	}

}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5659_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5659_STO1_ADC_MIXER,
			RT5659_M_STO1_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5659_STO1_ADC_MIXER,
			RT5659_M_STO1_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5659_STO1_ADC_MIXER,
			RT5659_M_STO1_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5659_STO1_ADC_MIXER,
			RT5659_M_STO1_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5659_MONO_ADC_MIXER,
			RT5659_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5659_MONO_ADC_MIXER,
			RT5659_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5659_MONO_ADC_MIXER,
			RT5659_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5659_MONO_ADC_MIXER,
			RT5659_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5659_AD_DA_MIXER,
			RT5659_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5659_AD_DA_MIXER,
			RT5659_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5659_AD_DA_MIXER,
			RT5659_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5659_AD_DA_MIXER,
			RT5659_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_L1_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_R1_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_L2_STO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_R2_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_L1_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_R1_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_L2_STO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_STO_DAC_MIXER,
			RT5659_M_DAC_R2_STO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_L1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_R1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_L2_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_R2_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_L1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_R1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_L2_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_MONO_DAC_MIXER,
			RT5659_M_DAC_R2_MONO_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5659_rec1_l_mix[] = {
	SOC_DAPM_SINGLE("SPKVOLL Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_SPKVOLL_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_INL_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_BST4_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_BST3_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_BST2_RM1_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_BST1_RM1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_rec1_r_mix[] = {
	SOC_DAPM_SINGLE("HPOVOLR Switch", RT5659_REC1_L2_MIXER,
			RT5659_M_HPOVOLR_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5659_REC1_R2_MIXER,
			RT5659_M_INR_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5659_REC1_R2_MIXER,
			RT5659_M_BST4_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_REC1_R2_MIXER,
			RT5659_M_BST3_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_REC1_R2_MIXER,
			RT5659_M_BST2_RM1_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_REC1_R2_MIXER,
			RT5659_M_BST1_RM1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_rec2_l_mix[] = {
	SOC_DAPM_SINGLE("SPKVOLL Switch", RT5659_REC2_L2_MIXER,
			RT5659_M_SPKVOL_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOLL Switch", RT5659_REC2_L2_MIXER,
			RT5659_M_OUTVOLL_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5659_REC2_L2_MIXER,
			RT5659_M_BST4_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_REC2_L2_MIXER,
			RT5659_M_BST3_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_REC2_L2_MIXER,
			RT5659_M_BST2_RM2_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_REC2_L2_MIXER,
			RT5659_M_BST1_RM2_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_rec2_r_mix[] = {
	SOC_DAPM_SINGLE("MONOVOL Switch", RT5659_REC2_R2_MIXER,
			RT5659_M_MONOVOL_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOLR Switch", RT5659_REC2_R2_MIXER,
			RT5659_M_OUTVOLR_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5659_REC2_R2_MIXER,
			RT5659_M_BST4_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_REC2_R2_MIXER,
			RT5659_M_BST3_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_REC2_R2_MIXER,
			RT5659_M_BST2_RM2_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_REC2_R2_MIXER,
			RT5659_M_BST1_RM2_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_spk_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_SPK_L_MIXER,
			RT5659_M_DAC_L2_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_SPK_L_MIXER,
			RT5659_M_BST1_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5659_SPK_L_MIXER,
			RT5659_M_IN_L_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5659_SPK_L_MIXER,
			RT5659_M_IN_R_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_SPK_L_MIXER,
			RT5659_M_BST3_SM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_spk_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_SPK_R_MIXER,
			RT5659_M_DAC_R2_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5659_SPK_R_MIXER,
			RT5659_M_BST4_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5659_SPK_R_MIXER,
			RT5659_M_IN_L_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5659_SPK_R_MIXER,
			RT5659_M_IN_R_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_SPK_R_MIXER,
			RT5659_M_BST3_SM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_monovol_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_DAC_L2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_DAC_R2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_BST1_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_BST2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_BST3_MM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_out_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_OUT_L_MIXER,
			RT5659_M_DAC_L2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5659_OUT_L_MIXER,
			RT5659_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5659_OUT_L_MIXER,
			RT5659_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_OUT_L_MIXER,
			RT5659_M_BST2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_OUT_L_MIXER,
			RT5659_M_BST3_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_out_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_OUT_R_MIXER,
			RT5659_M_DAC_R2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5659_OUT_R_MIXER,
			RT5659_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5659_OUT_R_MIXER,
			RT5659_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5659_OUT_R_MIXER,
			RT5659_M_BST3_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST4 Switch", RT5659_OUT_R_MIXER,
			RT5659_M_BST4_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_spo_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_SPO_AMP_GAIN,
			RT5659_M_DAC_L2_SPKOMIX_SFT, 1, 0),
	SOC_DAPM_SINGLE("SPKVOL L Switch", RT5659_SPO_AMP_GAIN,
			RT5659_M_SPKVOLL_SPKOMIX_SFT, 1, 0),
};

static const struct snd_kcontrol_new rt5659_spo_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_SPO_AMP_GAIN,
			RT5659_M_DAC_R2_SPKOMIX_SFT, 1, 0),
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT5659_SPO_AMP_GAIN,
			RT5659_M_SPKVOLR_SPKOMIX_SFT, 1, 0),
};

static const struct snd_kcontrol_new rt5659_mono_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_DAC_L2_MA_SFT, 1, 1),
	SOC_DAPM_SINGLE("MONOVOL Switch", RT5659_MONOMIX_IN_GAIN,
			RT5659_M_MONOVOL_MA_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_lout_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5659_LOUT_MIXER,
			RT5659_M_DAC_L2_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT5659_LOUT_MIXER,
			RT5659_M_OV_L_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5659_lout_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5659_LOUT_MIXER,
			RT5659_M_DAC_R2_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT5659_LOUT_MIXER,
			RT5659_M_OV_R_LM_SFT, 1, 1),
};

/*DAC L2, DAC R2*/
/*MX-1B [6:4], MX-1B [2:0]*/
static const char * const rt5659_dac2_src[] = {
	"IF1 DAC2", "IF2 DAC", "IF3 DAC", "Mono ADC MIX"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_dac_l2_enum, RT5659_DAC_CTRL,
	RT5659_DAC_L2_SEL_SFT, rt5659_dac2_src);

static const struct snd_kcontrol_new rt5659_dac_l2_mux =
	SOC_DAPM_ENUM("DAC L2 Source", rt5659_dac_l2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_dac_r2_enum, RT5659_DAC_CTRL,
	RT5659_DAC_R2_SEL_SFT, rt5659_dac2_src);

static const struct snd_kcontrol_new rt5659_dac_r2_mux =
	SOC_DAPM_ENUM("DAC R2 Source", rt5659_dac_r2_enum);


/* STO1 ADC1 Source */
/* MX-26 [13] */
static const char * const rt5659_sto1_adc1_src[] = {
	"DAC MIX", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_sto1_adc1_enum, RT5659_STO1_ADC_MIXER,
	RT5659_STO1_ADC1_SRC_SFT, rt5659_sto1_adc1_src);

static const struct snd_kcontrol_new rt5659_sto1_adc1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC1 Source", rt5659_sto1_adc1_enum);

/* STO1 ADC Source */
/* MX-26 [12] */
static const char * const rt5659_sto1_adc_src[] = {
	"ADC1", "ADC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_sto1_adc_enum, RT5659_STO1_ADC_MIXER,
	RT5659_STO1_ADC_SRC_SFT, rt5659_sto1_adc_src);

static const struct snd_kcontrol_new rt5659_sto1_adc_mux =
	SOC_DAPM_ENUM("Stereo1 ADC Source", rt5659_sto1_adc_enum);

/* STO1 ADC2 Source */
/* MX-26 [11] */
static const char * const rt5659_sto1_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_sto1_adc2_enum, RT5659_STO1_ADC_MIXER,
	RT5659_STO1_ADC2_SRC_SFT, rt5659_sto1_adc2_src);

static const struct snd_kcontrol_new rt5659_sto1_adc2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC2 Source", rt5659_sto1_adc2_enum);

/* STO1 DMIC Source */
/* MX-26 [8] */
static const char * const rt5659_sto1_dmic_src[] = {
	"DMIC1", "DMIC2"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_sto1_dmic_enum, RT5659_STO1_ADC_MIXER,
	RT5659_STO1_DMIC_SRC_SFT, rt5659_sto1_dmic_src);

static const struct snd_kcontrol_new rt5659_sto1_dmic_mux =
	SOC_DAPM_ENUM("Stereo1 DMIC Source", rt5659_sto1_dmic_enum);


/* MONO ADC L2 Source */
/* MX-27 [12] */
static const char * const rt5659_mono_adc_l2_src[] = {
	"Mono DAC MIXL", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_adc_l2_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_ADC_L2_SRC_SFT, rt5659_mono_adc_l2_src);

static const struct snd_kcontrol_new rt5659_mono_adc_l2_mux =
	SOC_DAPM_ENUM("Mono ADC L2 Source", rt5659_mono_adc_l2_enum);


/* MONO ADC L1 Source */
/* MX-27 [11] */
static const char * const rt5659_mono_adc_l1_src[] = {
	"Mono DAC MIXL", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_adc_l1_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_ADC_L1_SRC_SFT, rt5659_mono_adc_l1_src);

static const struct snd_kcontrol_new rt5659_mono_adc_l1_mux =
	SOC_DAPM_ENUM("Mono ADC L1 Source", rt5659_mono_adc_l1_enum);

/* MONO ADC L Source, MONO ADC R Source*/
/* MX-27 [10:9], MX-27 [2:1] */
static const char * const rt5659_mono_adc_src[] = {
	"ADC1 L", "ADC1 R", "ADC2 L", "ADC2 R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_adc_l_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_ADC_L_SRC_SFT, rt5659_mono_adc_src);

static const struct snd_kcontrol_new rt5659_mono_adc_l_mux =
	SOC_DAPM_ENUM("Mono ADC L Source", rt5659_mono_adc_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_adcr_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_ADC_R_SRC_SFT, rt5659_mono_adc_src);

static const struct snd_kcontrol_new rt5659_mono_adc_r_mux =
	SOC_DAPM_ENUM("Mono ADC R Source", rt5659_mono_adcr_enum);

/* MONO DMIC L Source */
/* MX-27 [8] */
static const char * const rt5659_mono_dmic_l_src[] = {
	"DMIC1 L", "DMIC2 L"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_dmic_l_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_DMIC_L_SRC_SFT, rt5659_mono_dmic_l_src);

static const struct snd_kcontrol_new rt5659_mono_dmic_l_mux =
	SOC_DAPM_ENUM("Mono DMIC L Source", rt5659_mono_dmic_l_enum);

/* MONO ADC R2 Source */
/* MX-27 [4] */
static const char * const rt5659_mono_adc_r2_src[] = {
	"Mono DAC MIXR", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_adc_r2_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_ADC_R2_SRC_SFT, rt5659_mono_adc_r2_src);

static const struct snd_kcontrol_new rt5659_mono_adc_r2_mux =
	SOC_DAPM_ENUM("Mono ADC R2 Source", rt5659_mono_adc_r2_enum);

/* MONO ADC R1 Source */
/* MX-27 [3] */
static const char * const rt5659_mono_adc_r1_src[] = {
	"Mono DAC MIXR", "ADC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_adc_r1_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_ADC_R1_SRC_SFT, rt5659_mono_adc_r1_src);

static const struct snd_kcontrol_new rt5659_mono_adc_r1_mux =
	SOC_DAPM_ENUM("Mono ADC R1 Source", rt5659_mono_adc_r1_enum);

/* MONO DMIC R Source */
/* MX-27 [0] */
static const char * const rt5659_mono_dmic_r_src[] = {
	"DMIC1 R", "DMIC2 R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_mono_dmic_r_enum, RT5659_MONO_ADC_MIXER,
	RT5659_MONO_DMIC_R_SRC_SFT, rt5659_mono_dmic_r_src);

static const struct snd_kcontrol_new rt5659_mono_dmic_r_mux =
	SOC_DAPM_ENUM("Mono DMIC R Source", rt5659_mono_dmic_r_enum);


/* DAC R1 Source, DAC L1 Source*/
/* MX-29 [11:10], MX-29 [9:8]*/
static const char * const rt5659_dac1_src[] = {
	"IF1 DAC1", "IF2 DAC", "IF3 DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_dac_r1_enum, RT5659_AD_DA_MIXER,
	RT5659_DAC1_R_SEL_SFT, rt5659_dac1_src);

static const struct snd_kcontrol_new rt5659_dac_r1_mux =
	SOC_DAPM_ENUM("DAC R1 Source", rt5659_dac_r1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_dac_l1_enum, RT5659_AD_DA_MIXER,
	RT5659_DAC1_L_SEL_SFT, rt5659_dac1_src);

static const struct snd_kcontrol_new rt5659_dac_l1_mux =
	SOC_DAPM_ENUM("DAC L1 Source", rt5659_dac_l1_enum);

/* DAC Digital Mixer L Source, DAC Digital Mixer R Source*/
/* MX-2C [6], MX-2C [4]*/
static const char * const rt5659_dig_dac_mix_src[] = {
	"Stereo DAC Mixer", "Mono DAC Mixer"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_dig_dac_mixl_enum, RT5659_DIG_MIXER,
	RT5659_DAC_MIX_L_SFT, rt5659_dig_dac_mix_src);

static const struct snd_kcontrol_new rt5659_dig_dac_mixl_mux =
	SOC_DAPM_ENUM("DAC Digital Mixer L Source", rt5659_dig_dac_mixl_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_dig_dac_mixr_enum, RT5659_DIG_MIXER,
	RT5659_DAC_MIX_R_SFT, rt5659_dig_dac_mix_src);

static const struct snd_kcontrol_new rt5659_dig_dac_mixr_mux =
	SOC_DAPM_ENUM("DAC Digital Mixer R Source", rt5659_dig_dac_mixr_enum);

/* Analog DAC L1 Source, Analog DAC R1 Source*/
/* MX-2D [3], MX-2D [2]*/
static const char * const rt5659_alg_dac1_src[] = {
	"DAC", "Stereo DAC Mixer"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_alg_dac_l1_enum, RT5659_A_DAC_MUX,
	RT5659_A_DACL1_SFT, rt5659_alg_dac1_src);

static const struct snd_kcontrol_new rt5659_alg_dac_l1_mux =
	SOC_DAPM_ENUM("Analog DACL1 Source", rt5659_alg_dac_l1_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_alg_dac_r1_enum, RT5659_A_DAC_MUX,
	RT5659_A_DACR1_SFT, rt5659_alg_dac1_src);

static const struct snd_kcontrol_new rt5659_alg_dac_r1_mux =
	SOC_DAPM_ENUM("Analog DACR1 Source", rt5659_alg_dac_r1_enum);

/* Analog DAC LR Source, Analog DAC R2 Source*/
/* MX-2D [1], MX-2D [0]*/
static const char * const rt5659_alg_dac2_src[] = {
	"Stereo DAC Mixer", "Mono DAC Mixer"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_alg_dac_l2_enum, RT5659_A_DAC_MUX,
	RT5659_A_DACL2_SFT, rt5659_alg_dac2_src);

static const struct snd_kcontrol_new rt5659_alg_dac_l2_mux =
	SOC_DAPM_ENUM("Analog DAC L2 Source", rt5659_alg_dac_l2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_alg_dac_r2_enum, RT5659_A_DAC_MUX,
	RT5659_A_DACR2_SFT, rt5659_alg_dac2_src);

static const struct snd_kcontrol_new rt5659_alg_dac_r2_mux =
	SOC_DAPM_ENUM("Analog DAC R2 Source", rt5659_alg_dac_r2_enum);

/* Interface2 ADC Data Input*/
/* MX-2F [13:12] */
static const char * const rt5659_if2_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "DAC_REF", "IF_ADC3"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_if2_adc_in_enum, RT5659_DIG_INF23_DATA,
	RT5659_IF2_ADC_IN_SFT, rt5659_if2_adc_in_src);

static const struct snd_kcontrol_new rt5659_if2_adc_in_mux =
	SOC_DAPM_ENUM("IF2 ADC IN Source", rt5659_if2_adc_in_enum);

/* Interface3 ADC Data Input*/
/* MX-2F [1:0] */
static const char * const rt5659_if3_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "DAC_REF", "Stereo2_ADC_L/R"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_if3_adc_in_enum, RT5659_DIG_INF23_DATA,
	RT5659_IF3_ADC_IN_SFT, rt5659_if3_adc_in_src);

static const struct snd_kcontrol_new rt5659_if3_adc_in_mux =
	SOC_DAPM_ENUM("IF3 ADC IN Source", rt5659_if3_adc_in_enum);

/* PDM 1 L/R*/
/* MX-31 [15] [13] */
static const char * const rt5659_pdm_src[] = {
	"Mono DAC", "Stereo DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_pdm_l_enum, RT5659_PDM_OUT_CTRL,
	RT5659_PDM1_L_SFT, rt5659_pdm_src);

static const struct snd_kcontrol_new rt5659_pdm_l_mux =
	SOC_DAPM_ENUM("PDM L Source", rt5659_pdm_l_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5659_pdm_r_enum, RT5659_PDM_OUT_CTRL,
	RT5659_PDM1_R_SFT, rt5659_pdm_src);

static const struct snd_kcontrol_new rt5659_pdm_r_mux =
	SOC_DAPM_ENUM("PDM R Source", rt5659_pdm_r_enum);

/* SPDIF Output source*/
/* MX-36 [1:0] */
static const char * const rt5659_spdif_src[] = {
	"IF1_DAC1", "IF1_DAC2", "IF2_DAC", "IF3_DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_spdif_enum, RT5659_SPDIF_CTRL,
	RT5659_SPDIF_SEL_SFT, rt5659_spdif_src);

static const struct snd_kcontrol_new rt5659_spdif_mux =
	SOC_DAPM_ENUM("SPDIF Source", rt5659_spdif_enum);

/* I2S1 TDM ADCDAT Source */
/* MX-78[4:0] */
static const char * const rt5659_rx_adc_data_src[] = {
	"AD1:AD2:DAC:NUL", "AD1:AD2:NUL:DAC", "AD1:DAC:AD2:NUL",
	"AD1:DAC:NUL:AD2", "AD1:NUL:DAC:AD2", "AD1:NUL:AD2:DAC",
	"AD2:AD1:DAC:NUL", "AD2:AD1:NUL:DAC", "AD2:DAC:AD1:NUL",
	"AD2:DAC:NUL:AD1", "AD2:NUL:DAC:AD1", "AD1:NUL:AD1:DAC",
	"DAC:AD1:AD2:NUL", "DAC:AD1:NUL:AD2", "DAC:AD2:AD1:NUL",
	"DAC:AD2:NUL:AD1", "DAC:NUL:DAC:AD2", "DAC:NUL:AD2:DAC",
	"NUL:AD1:AD2:DAC", "NUL:AD1:DAC:AD2", "NUL:AD2:AD1:DAC",
	"NUL:AD2:DAC:AD1", "NUL:DAC:DAC:AD2", "NUL:DAC:AD2:DAC"
};

static SOC_ENUM_SINGLE_DECL(
	rt5659_rx_adc_data_enum, RT5659_TDM_CTRL_2,
	RT5659_ADCDAT_SRC_SFT, rt5659_rx_adc_data_src);

static const struct snd_kcontrol_new rt5659_rx_adc_dac_mux =
	SOC_DAPM_ENUM("TDM ADCDAT Source", rt5659_rx_adc_data_enum);

/* Out Volume Switch */
static const struct snd_kcontrol_new spkvol_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_SPO_VOL, RT5659_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new spkvol_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_SPO_VOL, RT5659_VOL_R_SFT, 1, 1);

static const struct snd_kcontrol_new monovol_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_MONO_OUT, RT5659_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new outvol_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_LOUT, RT5659_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new outvol_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_LOUT, RT5659_VOL_R_SFT, 1, 1);

/* Out Switch */
static const struct snd_kcontrol_new spo_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_CLASSD_2, RT5659_M_RF_DIG_SFT, 1, 1);

static const struct snd_kcontrol_new mono_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_MONO_OUT, RT5659_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpo_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_HP_VOL, RT5659_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpo_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_HP_VOL, RT5659_R_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new lout_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_LOUT, RT5659_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new lout_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_LOUT, RT5659_R_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new pdm_l_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_PDM_OUT_CTRL, RT5659_M_PDM1_L_SFT, 1,
		1);

static const struct snd_kcontrol_new pdm_r_switch =
	SOC_DAPM_SINGLE("Switch", RT5659_PDM_OUT_CTRL, RT5659_M_PDM1_R_SFT, 1,
		1);

static int rt5659_spk_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_update_bits(component, RT5659_CLASSD_CTRL_1,
			RT5659_POW_CLSD_DB_MASK, RT5659_POW_CLSD_DB_EN);
		snd_soc_component_update_bits(component, RT5659_CLASSD_2,
			RT5659_M_RI_DIG, RT5659_M_RI_DIG);
		snd_soc_component_write(component, RT5659_CLASSD_1, 0x0803);
		snd_soc_component_write(component, RT5659_SPK_DC_CAILB_CTRL_3, 0x0000);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write(component, RT5659_CLASSD_1, 0x0011);
		snd_soc_component_update_bits(component, RT5659_CLASSD_2,
			RT5659_M_RI_DIG, 0x0);
		snd_soc_component_write(component, RT5659_SPK_DC_CAILB_CTRL_3, 0x0003);
		snd_soc_component_update_bits(component, RT5659_CLASSD_CTRL_1,
			RT5659_POW_CLSD_DB_MASK, RT5659_POW_CLSD_DB_DIS);
		break;

	default:
		return 0;
	}

	return 0;

}

static int rt5659_mono_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write(component, RT5659_MONO_AMP_CALIB_CTRL_1, 0x1e00);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write(component, RT5659_MONO_AMP_CALIB_CTRL_1, 0x1e04);
		break;

	default:
		return 0;
	}

	return 0;

}

static int rt5659_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_write(component, RT5659_HP_CHARGE_PUMP_1, 0x0e1e);
		snd_soc_component_update_bits(component, RT5659_DEPOP_1, 0x0010, 0x0010);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_write(component, RT5659_DEPOP_1, 0x0000);
		break;

	default:
		return 0;
	}

	return 0;
}

static int set_dmic_power(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/*Add delay to avoid pop noise*/
		msleep(450);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5659_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO2", RT5659_PWR_ANLG_3, RT5659_PWR_LDO2_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL", RT5659_PWR_ANLG_3, RT5659_PWR_PLL_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Det Power", RT5659_PWR_VOL,
		RT5659_PWR_MIC_DET_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mono Vref", RT5659_PWR_ANLG_1,
		RT5659_PWR_VREF3_BIT, 0, NULL, 0),

	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("I2S1 ASRC", 1, RT5659_ASRC_1,
		RT5659_I2S1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S2 ASRC", 1, RT5659_ASRC_1,
		RT5659_I2S2_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S3 ASRC", 1, RT5659_ASRC_1,
		RT5659_I2S3_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC STO ASRC", 1, RT5659_ASRC_1,
		RT5659_DAC_STO_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC Mono L ASRC", 1, RT5659_ASRC_1,
		RT5659_DAC_MONO_L_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC Mono R ASRC", 1, RT5659_ASRC_1,
		RT5659_DAC_MONO_R_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC STO1 ASRC", 1, RT5659_ASRC_1,
		RT5659_ADC_STO1_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC Mono L ASRC", 1, RT5659_ASRC_1,
		RT5659_ADC_MONO_L_ASRC_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC Mono R ASRC", 1, RT5659_ASRC_1,
		RT5659_ADC_MONO_R_ASRC_SFT, 0, NULL, 0),

	/* Input Side */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5659_PWR_ANLG_2, RT5659_PWR_MB1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", RT5659_PWR_ANLG_2, RT5659_PWR_MB2_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS3", RT5659_PWR_ANLG_2, RT5659_PWR_MB3_BIT,
		0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_INPUT("DMIC L2"),
	SND_SOC_DAPM_INPUT("DMIC R2"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),
	SND_SOC_DAPM_INPUT("IN3P"),
	SND_SOC_DAPM_INPUT("IN3N"),
	SND_SOC_DAPM_INPUT("IN4P"),
	SND_SOC_DAPM_INPUT("IN4N"),

	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5659_DMIC_CTRL_1,
		RT5659_DMIC_1_EN_SFT, 0, set_dmic_power, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC2 Power", RT5659_DMIC_CTRL_1,
		RT5659_DMIC_2_EN_SFT, 0, set_dmic_power, SND_SOC_DAPM_POST_PMU),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST1_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST2", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST2_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST3", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST3_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST4", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST4_P_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST1 Power", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST2 Power", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST3 Power", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST3_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BST4 Power", RT5659_PWR_ANLG_2,
		RT5659_PWR_BST4_BIT, 0, NULL, 0),


	/* Input Volume */
	SND_SOC_DAPM_PGA("INL VOL", RT5659_PWR_VOL, RT5659_PWR_IN_L_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA("INR VOL", RT5659_PWR_VOL, RT5659_PWR_IN_R_BIT,
		0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX1L", RT5659_PWR_MIXER, RT5659_PWR_RM1_L_BIT,
		0, rt5659_rec1_l_mix, ARRAY_SIZE(rt5659_rec1_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIX1R", RT5659_PWR_MIXER, RT5659_PWR_RM1_R_BIT,
		0, rt5659_rec1_r_mix, ARRAY_SIZE(rt5659_rec1_r_mix)),
	SND_SOC_DAPM_MIXER("RECMIX2L", RT5659_PWR_MIXER, RT5659_PWR_RM2_L_BIT,
		0, rt5659_rec2_l_mix, ARRAY_SIZE(rt5659_rec2_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIX2R", RT5659_PWR_MIXER, RT5659_PWR_RM2_R_BIT,
		0, rt5659_rec2_r_mix, ARRAY_SIZE(rt5659_rec2_r_mix)),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1 L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC1 R", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC2 L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC2 R", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("ADC1 L Power", RT5659_PWR_DIG_1,
		RT5659_PWR_ADC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 R Power", RT5659_PWR_DIG_1,
		RT5659_PWR_ADC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 L Power", RT5659_PWR_DIG_1,
		RT5659_PWR_ADC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC2 R Power", RT5659_PWR_DIG_1,
		RT5659_PWR_ADC_R2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC1 clock", SND_SOC_NOPM, 0, 0, set_adc1_clk,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("ADC2 clock", SND_SOC_NOPM, 0, 0, set_adc2_clk,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 DMIC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 DMIC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_adc1_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_adc1_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_adc2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_adc2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_adc_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_sto1_adc_mux),
	SND_SOC_DAPM_MUX("Mono ADC L2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_adc_l2_mux),
	SND_SOC_DAPM_MUX("Mono ADC R2 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_adc_r2_mux),
	SND_SOC_DAPM_MUX("Mono ADC L1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_adc_l1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R1 Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono DMIC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_dmic_l_mux),
	SND_SOC_DAPM_MUX("Mono DMIC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_dmic_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC L Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_adc_l_mux),
	SND_SOC_DAPM_MUX("Mono ADC R Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_mono_adc_r_mux),
	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("ADC Stereo1 Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_ADC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Stereo2 Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_ADC_S2F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXL", SND_SOC_NOPM,
		0, 0, rt5659_sto1_adc_l_mix,
		ARRAY_SIZE(rt5659_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXR", SND_SOC_NOPM,
		0, 0, rt5659_sto1_adc_r_mix,
		ARRAY_SIZE(rt5659_sto1_adc_r_mix)),
	SND_SOC_DAPM_SUPPLY("ADC Mono Left Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXL", RT5659_MONO_ADC_DIG_VOL,
		RT5659_L_MUTE_SFT, 1, rt5659_mono_adc_l_mix,
		ARRAY_SIZE(rt5659_mono_adc_l_mix)),
	SND_SOC_DAPM_SUPPLY("ADC Mono Right Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXR", RT5659_MONO_ADC_DIG_VOL,
		RT5659_R_MUTE_SFT, 1, rt5659_mono_adc_r_mix,
		ARRAY_SIZE(rt5659_mono_adc_r_mix)),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("IF_ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo2 ADC LR", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Stereo1 ADC Volume L", RT5659_STO1_ADC_DIG_VOL,
		RT5659_L_MUTE_SFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo1 ADC Volume R", RT5659_STO1_ADC_DIG_VOL,
		RT5659_R_MUTE_SFT, 1, NULL, 0),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5659_PWR_DIG_1, RT5659_PWR_I2S1_BIT,
		0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT5659_PWR_DIG_1, RT5659_PWR_I2S2_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S3", RT5659_PWR_DIG_1, RT5659_PWR_I2S3_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF3 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_PGA("TDM AD1:AD2:DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("TDM AD2:DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX("TDM Data Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_rx_adc_dac_mux),
	SND_SOC_DAPM_MUX("IF2 ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_if2_adc_in_mux),
	SND_SOC_DAPM_MUX("IF3 ADC Mux", SND_SOC_NOPM, 0, 0,
		&rt5659_if3_adc_in_mux),
	SND_SOC_DAPM_MUX("IF1 01 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if1_01_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 23 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if1_23_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 45 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if1_45_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF1 67 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if1_67_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF2 DAC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if2_dac_swap_mux),
	SND_SOC_DAPM_MUX("IF2 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if2_adc_swap_mux),
	SND_SOC_DAPM_MUX("IF3 DAC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if3_dac_swap_mux),
	SND_SOC_DAPM_MUX("IF3 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5659_if3_adc_swap_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF3RX", "AIF3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF3TX", "AIF3 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5659_dac_l_mix, ARRAY_SIZE(rt5659_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5659_dac_r_mix, ARRAY_SIZE(rt5659_dac_r_mix)),

	/* DAC channel Mux */
	SND_SOC_DAPM_MUX("DAC L1 Mux", SND_SOC_NOPM, 0, 0, &rt5659_dac_l1_mux),
	SND_SOC_DAPM_MUX("DAC R1 Mux", SND_SOC_NOPM, 0, 0, &rt5659_dac_r1_mux),
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0, &rt5659_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0, &rt5659_dac_r2_mux),

	SND_SOC_DAPM_MUX("DAC L1 Source", SND_SOC_NOPM, 0, 0,
		&rt5659_alg_dac_l1_mux),
	SND_SOC_DAPM_MUX("DAC R1 Source", SND_SOC_NOPM, 0, 0,
		&rt5659_alg_dac_r1_mux),
	SND_SOC_DAPM_MUX("DAC L2 Source", SND_SOC_NOPM, 0, 0,
		&rt5659_alg_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Source", SND_SOC_NOPM, 0, 0,
		&rt5659_alg_dac_r2_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("DAC Stereo1 Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_DAC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mono Left Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_DAC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Mono Right Filter", RT5659_PWR_DIG_2,
		RT5659_PWR_DAC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5659_sto_dac_l_mix, ARRAY_SIZE(rt5659_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5659_sto_dac_r_mix, ARRAY_SIZE(rt5659_sto_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5659_mono_dac_l_mix, ARRAY_SIZE(rt5659_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5659_mono_dac_r_mix, ARRAY_SIZE(rt5659_mono_dac_r_mix)),
	SND_SOC_DAPM_MUX("DAC MIXL", SND_SOC_NOPM, 0, 0,
		&rt5659_dig_dac_mixl_mux),
	SND_SOC_DAPM_MUX("DAC MIXR", SND_SOC_NOPM, 0, 0,
		&rt5659_dig_dac_mixr_mux),

	/* DACs */
	SND_SOC_DAPM_SUPPLY_S("DAC L1 Power", 1, RT5659_PWR_DIG_1,
		RT5659_PWR_DAC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DAC R1 Power", 1, RT5659_PWR_DIG_1,
		RT5659_PWR_DAC_R1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC L1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("DAC L2 Power", RT5659_PWR_DIG_1,
		RT5659_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R2 Power", RT5659_PWR_DIG_1,
		RT5659_PWR_DAC_R2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R2", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_PGA("DAC_REF", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* OUT Mixer */
	SND_SOC_DAPM_MIXER("SPK MIXL", RT5659_PWR_MIXER, RT5659_PWR_SM_L_BIT,
		0, rt5659_spk_l_mix, ARRAY_SIZE(rt5659_spk_l_mix)),
	SND_SOC_DAPM_MIXER("SPK MIXR", RT5659_PWR_MIXER, RT5659_PWR_SM_R_BIT,
		0, rt5659_spk_r_mix, ARRAY_SIZE(rt5659_spk_r_mix)),
	SND_SOC_DAPM_MIXER("MONOVOL MIX", RT5659_PWR_MIXER, RT5659_PWR_MM_BIT,
		0, rt5659_monovol_mix, ARRAY_SIZE(rt5659_monovol_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXL", RT5659_PWR_MIXER, RT5659_PWR_OM_L_BIT,
		0, rt5659_out_l_mix, ARRAY_SIZE(rt5659_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5659_PWR_MIXER, RT5659_PWR_OM_R_BIT,
		0, rt5659_out_r_mix, ARRAY_SIZE(rt5659_out_r_mix)),

	/* Output Volume */
	SND_SOC_DAPM_SWITCH("SPKVOL L", RT5659_PWR_VOL, RT5659_PWR_SV_L_BIT, 0,
		&spkvol_l_switch),
	SND_SOC_DAPM_SWITCH("SPKVOL R", RT5659_PWR_VOL, RT5659_PWR_SV_R_BIT, 0,
		&spkvol_r_switch),
	SND_SOC_DAPM_SWITCH("MONOVOL", RT5659_PWR_VOL, RT5659_PWR_MV_BIT, 0,
		&monovol_switch),
	SND_SOC_DAPM_SWITCH("OUTVOL L", RT5659_PWR_VOL, RT5659_PWR_OV_L_BIT, 0,
		&outvol_l_switch),
	SND_SOC_DAPM_SWITCH("OUTVOL R", RT5659_PWR_VOL, RT5659_PWR_OV_R_BIT, 0,
		&outvol_r_switch),

	/* SPO/MONO/HPO/LOUT */
	SND_SOC_DAPM_MIXER("SPO L MIX", SND_SOC_NOPM, 0, 0, rt5659_spo_l_mix,
		ARRAY_SIZE(rt5659_spo_l_mix)),
	SND_SOC_DAPM_MIXER("SPO R MIX", SND_SOC_NOPM, 0, 0, rt5659_spo_r_mix,
		ARRAY_SIZE(rt5659_spo_r_mix)),
	SND_SOC_DAPM_MIXER("Mono MIX", SND_SOC_NOPM, 0,	0, rt5659_mono_mix,
		ARRAY_SIZE(rt5659_mono_mix)),
	SND_SOC_DAPM_MIXER("LOUT L MIX", SND_SOC_NOPM, 0, 0, rt5659_lout_l_mix,
		ARRAY_SIZE(rt5659_lout_l_mix)),
	SND_SOC_DAPM_MIXER("LOUT R MIX", SND_SOC_NOPM, 0, 0, rt5659_lout_r_mix,
		ARRAY_SIZE(rt5659_lout_r_mix)),

	SND_SOC_DAPM_PGA_S("SPK Amp", 1, RT5659_PWR_DIG_1, RT5659_PWR_CLS_D_BIT,
		0, rt5659_spk_event, SND_SOC_DAPM_POST_PMD |
		SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_PGA_S("Mono Amp", 1, RT5659_PWR_ANLG_1, RT5659_PWR_MA_BIT,
		0, rt5659_mono_event, SND_SOC_DAPM_POST_PMD |
		SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0, rt5659_hp_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("LOUT Amp", 1,  RT5659_PWR_ANLG_1, RT5659_PWR_LM_BIT,
		0,  NULL, 0),

	SND_SOC_DAPM_SUPPLY("Charge Pump", SND_SOC_NOPM, 0, 0,
		rt5659_charge_pump_event, SND_SOC_DAPM_PRE_PMU |
		SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SWITCH("SPO Playback", SND_SOC_NOPM, 0, 0, &spo_switch),
	SND_SOC_DAPM_SWITCH("Mono Playback", SND_SOC_NOPM, 0, 0,
		&mono_switch),
	SND_SOC_DAPM_SWITCH("HPO L Playback", SND_SOC_NOPM, 0, 0,
		&hpo_l_switch),
	SND_SOC_DAPM_SWITCH("HPO R Playback", SND_SOC_NOPM, 0, 0,
		&hpo_r_switch),
	SND_SOC_DAPM_SWITCH("LOUT L Playback", SND_SOC_NOPM, 0, 0,
		&lout_l_switch),
	SND_SOC_DAPM_SWITCH("LOUT R Playback", SND_SOC_NOPM, 0, 0,
		&lout_r_switch),
	SND_SOC_DAPM_SWITCH("PDM L Playback", SND_SOC_NOPM, 0, 0,
		&pdm_l_switch),
	SND_SOC_DAPM_SWITCH("PDM R Playback", SND_SOC_NOPM, 0, 0,
		&pdm_r_switch),

	/* PDM */
	SND_SOC_DAPM_SUPPLY("PDM Power", RT5659_PWR_DIG_2,
		RT5659_PWR_PDM1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MUX("PDM L Mux", RT5659_PDM_OUT_CTRL,
		RT5659_M_PDM1_L_SFT, 1, &rt5659_pdm_l_mux),
	SND_SOC_DAPM_MUX("PDM R Mux", RT5659_PDM_OUT_CTRL,
		RT5659_M_PDM1_R_SFT, 1, &rt5659_pdm_r_mux),

	/* SPDIF */
	SND_SOC_DAPM_MUX("SPDIF Mux", SND_SOC_NOPM, 0, 0, &rt5659_spdif_mux),

	SND_SOC_DAPM_SUPPLY("SYS CLK DET", RT5659_CLK_DET, 3, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLKDET", RT5659_CLK_DET, 0, 0, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("MONOOUT"),
	SND_SOC_DAPM_OUTPUT("PDML"),
	SND_SOC_DAPM_OUTPUT("PDMR"),
	SND_SOC_DAPM_OUTPUT("SPDIF"),
};

static const struct snd_soc_dapm_route rt5659_dapm_routes[] = {
	/*PLL*/
	{ "ADC Stereo1 Filter", NULL, "PLL", is_sys_clk_from_pll },
	{ "ADC Stereo2 Filter", NULL, "PLL", is_sys_clk_from_pll },
	{ "ADC Mono Left Filter", NULL, "PLL", is_sys_clk_from_pll },
	{ "ADC Mono Right Filter", NULL, "PLL", is_sys_clk_from_pll },
	{ "DAC Stereo1 Filter", NULL, "PLL", is_sys_clk_from_pll },
	{ "DAC Mono Left Filter", NULL, "PLL", is_sys_clk_from_pll },
	{ "DAC Mono Right Filter", NULL, "PLL", is_sys_clk_from_pll },

	/*ASRC*/
	{ "ADC Stereo1 Filter", NULL, "ADC STO1 ASRC", is_using_asrc },
	{ "ADC Mono Left Filter", NULL, "ADC Mono L ASRC", is_using_asrc },
	{ "ADC Mono Right Filter", NULL, "ADC Mono R ASRC", is_using_asrc },
	{ "DAC Mono Left Filter", NULL, "DAC Mono L ASRC", is_using_asrc },
	{ "DAC Mono Right Filter", NULL, "DAC Mono R ASRC", is_using_asrc },
	{ "DAC Stereo1 Filter", NULL, "DAC STO ASRC", is_using_asrc },

	{ "SYS CLK DET", NULL, "CLKDET" },

	{ "I2S1", NULL, "I2S1 ASRC" },
	{ "I2S2", NULL, "I2S2 ASRC" },
	{ "I2S3", NULL, "I2S3 ASRC" },

	{ "DMIC1", NULL, "DMIC L1" },
	{ "DMIC1", NULL, "DMIC R1" },
	{ "DMIC2", NULL, "DMIC L2" },
	{ "DMIC2", NULL, "DMIC R2" },

	{ "BST1", NULL, "IN1P" },
	{ "BST1", NULL, "IN1N" },
	{ "BST1", NULL, "BST1 Power" },
	{ "BST2", NULL, "IN2P" },
	{ "BST2", NULL, "IN2N" },
	{ "BST2", NULL, "BST2 Power" },
	{ "BST3", NULL, "IN3P" },
	{ "BST3", NULL, "IN3N" },
	{ "BST3", NULL, "BST3 Power" },
	{ "BST4", NULL, "IN4P" },
	{ "BST4", NULL, "IN4N" },
	{ "BST4", NULL, "BST4 Power" },

	{ "INL VOL", NULL, "IN2P" },
	{ "INR VOL", NULL, "IN2N" },

	{ "RECMIX1L", "SPKVOLL Switch", "SPKVOL L" },
	{ "RECMIX1L", "INL Switch", "INL VOL" },
	{ "RECMIX1L", "BST4 Switch", "BST4" },
	{ "RECMIX1L", "BST3 Switch", "BST3" },
	{ "RECMIX1L", "BST2 Switch", "BST2" },
	{ "RECMIX1L", "BST1 Switch", "BST1" },

	{ "RECMIX1R", "HPOVOLR Switch", "HPO R Playback" },
	{ "RECMIX1R", "INR Switch", "INR VOL" },
	{ "RECMIX1R", "BST4 Switch", "BST4" },
	{ "RECMIX1R", "BST3 Switch", "BST3" },
	{ "RECMIX1R", "BST2 Switch", "BST2" },
	{ "RECMIX1R", "BST1 Switch", "BST1" },

	{ "RECMIX2L", "SPKVOLL Switch", "SPKVOL L" },
	{ "RECMIX2L", "OUTVOLL Switch", "OUTVOL L" },
	{ "RECMIX2L", "BST4 Switch", "BST4" },
	{ "RECMIX2L", "BST3 Switch", "BST3" },
	{ "RECMIX2L", "BST2 Switch", "BST2" },
	{ "RECMIX2L", "BST1 Switch", "BST1" },

	{ "RECMIX2R", "MONOVOL Switch", "MONOVOL" },
	{ "RECMIX2R", "OUTVOLR Switch", "OUTVOL R" },
	{ "RECMIX2R", "BST4 Switch", "BST4" },
	{ "RECMIX2R", "BST3 Switch", "BST3" },
	{ "RECMIX2R", "BST2 Switch", "BST2" },
	{ "RECMIX2R", "BST1 Switch", "BST1" },

	{ "ADC1 L", NULL, "RECMIX1L" },
	{ "ADC1 L", NULL, "ADC1 L Power" },
	{ "ADC1 L", NULL, "ADC1 clock" },
	{ "ADC1 R", NULL, "RECMIX1R" },
	{ "ADC1 R", NULL, "ADC1 R Power" },
	{ "ADC1 R", NULL, "ADC1 clock" },

	{ "ADC2 L", NULL, "RECMIX2L" },
	{ "ADC2 L", NULL, "ADC2 L Power" },
	{ "ADC2 L", NULL, "ADC2 clock" },
	{ "ADC2 R", NULL, "RECMIX2R" },
	{ "ADC2 R", NULL, "ADC2 R Power" },
	{ "ADC2 R", NULL, "ADC2 clock" },

	{ "DMIC L1", NULL, "DMIC CLK" },
	{ "DMIC L1", NULL, "DMIC1 Power" },
	{ "DMIC R1", NULL, "DMIC CLK" },
	{ "DMIC R1", NULL, "DMIC1 Power" },
	{ "DMIC L2", NULL, "DMIC CLK" },
	{ "DMIC L2", NULL, "DMIC2 Power" },
	{ "DMIC R2", NULL, "DMIC CLK" },
	{ "DMIC R2", NULL, "DMIC2 Power" },

	{ "Stereo1 DMIC L Mux", "DMIC1", "DMIC L1" },
	{ "Stereo1 DMIC L Mux", "DMIC2", "DMIC L2" },

	{ "Stereo1 DMIC R Mux", "DMIC1", "DMIC R1" },
	{ "Stereo1 DMIC R Mux", "DMIC2", "DMIC R2" },

	{ "Mono DMIC L Mux", "DMIC1 L", "DMIC L1" },
	{ "Mono DMIC L Mux", "DMIC2 L", "DMIC L2" },

	{ "Mono DMIC R Mux", "DMIC1 R", "DMIC R1" },
	{ "Mono DMIC R Mux", "DMIC2 R", "DMIC R2" },

	{ "Stereo1 ADC L Mux", "ADC1", "ADC1 L" },
	{ "Stereo1 ADC L Mux", "ADC2", "ADC2 L" },
	{ "Stereo1 ADC R Mux", "ADC1", "ADC1 R" },
	{ "Stereo1 ADC R Mux", "ADC2", "ADC2 R" },

	{ "Stereo1 ADC L1 Mux", "ADC", "Stereo1 ADC L Mux" },
	{ "Stereo1 ADC L1 Mux", "DAC MIX", "DAC MIXL" },
	{ "Stereo1 ADC L2 Mux", "DMIC", "Stereo1 DMIC L Mux" },
	{ "Stereo1 ADC L2 Mux", "DAC MIX", "DAC MIXL" },

	{ "Stereo1 ADC R1 Mux", "ADC", "Stereo1 ADC R Mux" },
	{ "Stereo1 ADC R1 Mux", "DAC MIX", "DAC MIXR" },
	{ "Stereo1 ADC R2 Mux", "DMIC", "Stereo1 DMIC R Mux" },
	{ "Stereo1 ADC R2 Mux", "DAC MIX", "DAC MIXR" },

	{ "Mono ADC L Mux", "ADC1 L", "ADC1 L" },
	{ "Mono ADC L Mux", "ADC1 R", "ADC1 R" },
	{ "Mono ADC L Mux", "ADC2 L", "ADC2 L" },
	{ "Mono ADC L Mux", "ADC2 R", "ADC2 R" },

	{ "Mono ADC R Mux", "ADC1 L", "ADC1 L" },
	{ "Mono ADC R Mux", "ADC1 R", "ADC1 R" },
	{ "Mono ADC R Mux", "ADC2 L", "ADC2 L" },
	{ "Mono ADC R Mux", "ADC2 R", "ADC2 R" },

	{ "Mono ADC L2 Mux", "DMIC", "Mono DMIC L Mux" },
	{ "Mono ADC L2 Mux", "Mono DAC MIXL", "Mono DAC MIXL" },
	{ "Mono ADC L1 Mux", "Mono DAC MIXL", "Mono DAC MIXL" },
	{ "Mono ADC L1 Mux", "ADC",  "Mono ADC L Mux" },

	{ "Mono ADC R1 Mux", "Mono DAC MIXR", "Mono DAC MIXR" },
	{ "Mono ADC R1 Mux", "ADC", "Mono ADC R Mux" },
	{ "Mono ADC R2 Mux", "DMIC", "Mono DMIC R Mux" },
	{ "Mono ADC R2 Mux", "Mono DAC MIXR", "Mono DAC MIXR" },

	{ "Stereo1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC L1 Mux" },
	{ "Stereo1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC L2 Mux" },
	{ "Stereo1 ADC MIXL", NULL, "ADC Stereo1 Filter" },

	{ "Stereo1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC R1 Mux" },
	{ "Stereo1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC R2 Mux" },
	{ "Stereo1 ADC MIXR", NULL, "ADC Stereo1 Filter" },

	{ "Mono ADC MIXL", "ADC1 Switch", "Mono ADC L1 Mux" },
	{ "Mono ADC MIXL", "ADC2 Switch", "Mono ADC L2 Mux" },
	{ "Mono ADC MIXL", NULL, "ADC Mono Left Filter" },

	{ "Mono ADC MIXR", "ADC1 Switch", "Mono ADC R1 Mux" },
	{ "Mono ADC MIXR", "ADC2 Switch", "Mono ADC R2 Mux" },
	{ "Mono ADC MIXR", NULL, "ADC Mono Right Filter" },

	{ "Stereo1 ADC Volume L", NULL, "Stereo1 ADC MIXL" },
	{ "Stereo1 ADC Volume R", NULL, "Stereo1 ADC MIXR" },

	{ "IF_ADC1", NULL, "Stereo1 ADC Volume L" },
	{ "IF_ADC1", NULL, "Stereo1 ADC Volume R" },
	{ "IF_ADC2", NULL, "Mono ADC MIXL" },
	{ "IF_ADC2", NULL, "Mono ADC MIXR" },

	{ "TDM AD1:AD2:DAC", NULL, "IF_ADC1" },
	{ "TDM AD1:AD2:DAC", NULL, "IF_ADC2" },
	{ "TDM AD1:AD2:DAC", NULL, "DAC_REF" },
	{ "TDM AD2:DAC", NULL, "IF_ADC2" },
	{ "TDM AD2:DAC", NULL, "DAC_REF" },
	{ "TDM Data Mux", "AD1:AD2:DAC:NUL", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD1:AD2:NUL:DAC", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD1:DAC:AD2:NUL", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD1:DAC:NUL:AD2", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD1:NUL:DAC:AD2", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD1:NUL:AD2:DAC", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD2:AD1:DAC:NUL", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD2:AD1:NUL:DAC", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD2:DAC:AD1:NUL", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD2:DAC:NUL:AD1", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD2:NUL:DAC:AD1", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "AD1:NUL:AD1:DAC", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "DAC:AD1:AD2:NUL", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "DAC:AD1:NUL:AD2", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "DAC:AD2:AD1:NUL", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "DAC:AD2:NUL:AD1", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "DAC:NUL:DAC:AD2", "TDM AD2:DAC" },
	{ "TDM Data Mux", "DAC:NUL:AD2:DAC", "TDM AD2:DAC" },
	{ "TDM Data Mux", "NUL:AD1:AD2:DAC", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "NUL:AD1:DAC:AD2", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "NUL:AD2:AD1:DAC", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "NUL:AD2:DAC:AD1", "TDM AD1:AD2:DAC" },
	{ "TDM Data Mux", "NUL:DAC:DAC:AD2", "TDM AD2:DAC" },
	{ "TDM Data Mux", "NUL:DAC:AD2:DAC", "TDM AD2:DAC" },
	{ "IF1 01 ADC Swap Mux", "L/R", "TDM Data Mux" },
	{ "IF1 01 ADC Swap Mux", "R/L", "TDM Data Mux" },
	{ "IF1 01 ADC Swap Mux", "L/L", "TDM Data Mux" },
	{ "IF1 01 ADC Swap Mux", "R/R", "TDM Data Mux" },
	{ "IF1 23 ADC Swap Mux", "L/R", "TDM Data Mux" },
	{ "IF1 23 ADC Swap Mux", "R/L", "TDM Data Mux" },
	{ "IF1 23 ADC Swap Mux", "L/L", "TDM Data Mux" },
	{ "IF1 23 ADC Swap Mux", "R/R", "TDM Data Mux" },
	{ "IF1 45 ADC Swap Mux", "L/R", "TDM Data Mux" },
	{ "IF1 45 ADC Swap Mux", "R/L", "TDM Data Mux" },
	{ "IF1 45 ADC Swap Mux", "L/L", "TDM Data Mux" },
	{ "IF1 45 ADC Swap Mux", "R/R", "TDM Data Mux" },
	{ "IF1 67 ADC Swap Mux", "L/R", "TDM Data Mux" },
	{ "IF1 67 ADC Swap Mux", "R/L", "TDM Data Mux" },
	{ "IF1 67 ADC Swap Mux", "L/L", "TDM Data Mux" },
	{ "IF1 67 ADC Swap Mux", "R/R", "TDM Data Mux" },
	{ "IF1 ADC", NULL, "IF1 01 ADC Swap Mux" },
	{ "IF1 ADC", NULL, "IF1 23 ADC Swap Mux" },
	{ "IF1 ADC", NULL, "IF1 45 ADC Swap Mux" },
	{ "IF1 ADC", NULL, "IF1 67 ADC Swap Mux" },
	{ "IF1 ADC", NULL, "I2S1" },

	{ "IF2 ADC Mux", "IF_ADC1", "IF_ADC1" },
	{ "IF2 ADC Mux", "IF_ADC2", "IF_ADC2" },
	{ "IF2 ADC Mux", "IF_ADC3", "IF_ADC3" },
	{ "IF2 ADC Mux", "DAC_REF", "DAC_REF" },
	{ "IF2 ADC", NULL, "IF2 ADC Mux"},
	{ "IF2 ADC", NULL, "I2S2" },

	{ "IF3 ADC Mux", "IF_ADC1", "IF_ADC1" },
	{ "IF3 ADC Mux", "IF_ADC2", "IF_ADC2" },
	{ "IF3 ADC Mux", "Stereo2_ADC_L/R", "Stereo2 ADC LR" },
	{ "IF3 ADC Mux", "DAC_REF", "DAC_REF" },
	{ "IF3 ADC", NULL, "IF3 ADC Mux"},
	{ "IF3 ADC", NULL, "I2S3" },

	{ "AIF1TX", NULL, "IF1 ADC" },
	{ "IF2 ADC Swap Mux", "L/R", "IF2 ADC" },
	{ "IF2 ADC Swap Mux", "R/L", "IF2 ADC" },
	{ "IF2 ADC Swap Mux", "L/L", "IF2 ADC" },
	{ "IF2 ADC Swap Mux", "R/R", "IF2 ADC" },
	{ "AIF2TX", NULL, "IF2 ADC Swap Mux" },
	{ "IF3 ADC Swap Mux", "L/R", "IF3 ADC" },
	{ "IF3 ADC Swap Mux", "R/L", "IF3 ADC" },
	{ "IF3 ADC Swap Mux", "L/L", "IF3 ADC" },
	{ "IF3 ADC Swap Mux", "R/R", "IF3 ADC" },
	{ "AIF3TX", NULL, "IF3 ADC Swap Mux" },

	{ "IF1 DAC1", NULL, "AIF1RX" },
	{ "IF1 DAC2", NULL, "AIF1RX" },
	{ "IF2 DAC Swap Mux", "L/R", "AIF2RX" },
	{ "IF2 DAC Swap Mux", "R/L", "AIF2RX" },
	{ "IF2 DAC Swap Mux", "L/L", "AIF2RX" },
	{ "IF2 DAC Swap Mux", "R/R", "AIF2RX" },
	{ "IF2 DAC", NULL, "IF2 DAC Swap Mux" },
	{ "IF3 DAC Swap Mux", "L/R", "AIF3RX" },
	{ "IF3 DAC Swap Mux", "R/L", "AIF3RX" },
	{ "IF3 DAC Swap Mux", "L/L", "AIF3RX" },
	{ "IF3 DAC Swap Mux", "R/R", "AIF3RX" },
	{ "IF3 DAC", NULL, "IF3 DAC Swap Mux" },

	{ "IF1 DAC1", NULL, "I2S1" },
	{ "IF1 DAC2", NULL, "I2S1" },
	{ "IF2 DAC", NULL, "I2S2" },
	{ "IF3 DAC", NULL, "I2S3" },

	{ "IF1 DAC2 L", NULL, "IF1 DAC2" },
	{ "IF1 DAC2 R", NULL, "IF1 DAC2" },
	{ "IF1 DAC1 L", NULL, "IF1 DAC1" },
	{ "IF1 DAC1 R", NULL, "IF1 DAC1" },
	{ "IF2 DAC L", NULL, "IF2 DAC" },
	{ "IF2 DAC R", NULL, "IF2 DAC" },
	{ "IF3 DAC L", NULL, "IF3 DAC" },
	{ "IF3 DAC R", NULL, "IF3 DAC" },

	{ "DAC L1 Mux", "IF1 DAC1", "IF1 DAC1 L" },
	{ "DAC L1 Mux", "IF2 DAC", "IF2 DAC L" },
	{ "DAC L1 Mux", "IF3 DAC", "IF3 DAC L" },
	{ "DAC L1 Mux", NULL, "DAC Stereo1 Filter" },

	{ "DAC R1 Mux", "IF1 DAC1", "IF1 DAC1 R" },
	{ "DAC R1 Mux", "IF2 DAC", "IF2 DAC R" },
	{ "DAC R1 Mux", "IF3 DAC", "IF3 DAC R" },
	{ "DAC R1 Mux", NULL, "DAC Stereo1 Filter" },

	{ "DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC Volume L" },
	{ "DAC1 MIXL", "DAC1 Switch", "DAC L1 Mux" },
	{ "DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC Volume R" },
	{ "DAC1 MIXR", "DAC1 Switch", "DAC R1 Mux" },

	{ "DAC_REF", NULL, "DAC1 MIXL" },
	{ "DAC_REF", NULL, "DAC1 MIXR" },

	{ "DAC L2 Mux", "IF1 DAC2", "IF1 DAC2 L" },
	{ "DAC L2 Mux", "IF2 DAC", "IF2 DAC L" },
	{ "DAC L2 Mux", "IF3 DAC", "IF3 DAC L" },
	{ "DAC L2 Mux", "Mono ADC MIX", "Mono ADC MIXL" },
	{ "DAC L2 Mux", NULL, "DAC Mono Left Filter" },

	{ "DAC R2 Mux", "IF1 DAC2", "IF1 DAC2 R" },
	{ "DAC R2 Mux", "IF2 DAC", "IF2 DAC R" },
	{ "DAC R2 Mux", "IF3 DAC", "IF3 DAC R" },
	{ "DAC R2 Mux", "Mono ADC MIX", "Mono ADC MIXR" },
	{ "DAC R2 Mux", NULL, "DAC Mono Right Filter" },

	{ "Stereo DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXL", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXL", "DAC L2 Switch", "DAC L2 Mux" },
	{ "Stereo DAC MIXL", "DAC R2 Switch", "DAC R2 Mux" },

	{ "Stereo DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXR", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXR", "DAC L2 Switch", "DAC L2 Mux" },
	{ "Stereo DAC MIXR", "DAC R2 Switch", "DAC R2 Mux" },

	{ "Mono DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Mono DAC MIXL", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Mono DAC MIXL", "DAC L2 Switch", "DAC L2 Mux" },
	{ "Mono DAC MIXL", "DAC R2 Switch", "DAC R2 Mux" },
	{ "Mono DAC MIXR", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Mono DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Mono DAC MIXR", "DAC R2 Switch", "DAC R2 Mux" },
	{ "Mono DAC MIXR", "DAC L2 Switch", "DAC L2 Mux" },

	{ "DAC MIXL", "Stereo DAC Mixer", "Stereo DAC MIXL" },
	{ "DAC MIXL", "Mono DAC Mixer", "Mono DAC MIXL" },
	{ "DAC MIXR", "Stereo DAC Mixer", "Stereo DAC MIXR" },
	{ "DAC MIXR", "Mono DAC Mixer", "Mono DAC MIXR" },

	{ "DAC L1 Source", NULL, "DAC L1 Power" },
	{ "DAC L1 Source", "DAC", "DAC1 MIXL" },
	{ "DAC L1 Source", "Stereo DAC Mixer", "Stereo DAC MIXL" },
	{ "DAC R1 Source", NULL, "DAC R1 Power" },
	{ "DAC R1 Source", "DAC", "DAC1 MIXR" },
	{ "DAC R1 Source", "Stereo DAC Mixer", "Stereo DAC MIXR" },
	{ "DAC L2 Source", "Stereo DAC Mixer", "Stereo DAC MIXL" },
	{ "DAC L2 Source", "Mono DAC Mixer", "Mono DAC MIXL" },
	{ "DAC L2 Source", NULL, "DAC L2 Power" },
	{ "DAC R2 Source", "Stereo DAC Mixer", "Stereo DAC MIXR" },
	{ "DAC R2 Source", "Mono DAC Mixer", "Mono DAC MIXR" },
	{ "DAC R2 Source", NULL, "DAC R2 Power" },

	{ "DAC L1", NULL, "DAC L1 Source" },
	{ "DAC R1", NULL, "DAC R1 Source" },
	{ "DAC L2", NULL, "DAC L2 Source" },
	{ "DAC R2", NULL, "DAC R2 Source" },

	{ "SPK MIXL", "DAC L2 Switch", "DAC L2" },
	{ "SPK MIXL", "BST1 Switch", "BST1" },
	{ "SPK MIXL", "INL Switch", "INL VOL" },
	{ "SPK MIXL", "INR Switch", "INR VOL" },
	{ "SPK MIXL", "BST3 Switch", "BST3" },
	{ "SPK MIXR", "DAC R2 Switch", "DAC R2" },
	{ "SPK MIXR", "BST4 Switch", "BST4" },
	{ "SPK MIXR", "INL Switch", "INL VOL" },
	{ "SPK MIXR", "INR Switch", "INR VOL" },
	{ "SPK MIXR", "BST3 Switch", "BST3" },

	{ "MONOVOL MIX", "DAC L2 Switch", "DAC L2" },
	{ "MONOVOL MIX", "DAC R2 Switch", "DAC R2" },
	{ "MONOVOL MIX", "BST1 Switch", "BST1" },
	{ "MONOVOL MIX", "BST2 Switch", "BST2" },
	{ "MONOVOL MIX", "BST3 Switch", "BST3" },

	{ "OUT MIXL", "DAC L2 Switch", "DAC L2" },
	{ "OUT MIXL", "INL Switch", "INL VOL" },
	{ "OUT MIXL", "BST1 Switch", "BST1" },
	{ "OUT MIXL", "BST2 Switch", "BST2" },
	{ "OUT MIXL", "BST3 Switch", "BST3" },
	{ "OUT MIXR", "DAC R2 Switch", "DAC R2" },
	{ "OUT MIXR", "INR Switch", "INR VOL" },
	{ "OUT MIXR", "BST2 Switch", "BST2" },
	{ "OUT MIXR", "BST3 Switch", "BST3" },
	{ "OUT MIXR", "BST4 Switch", "BST4" },

	{ "SPKVOL L", "Switch", "SPK MIXL" },
	{ "SPKVOL R", "Switch", "SPK MIXR" },
	{ "SPO L MIX", "DAC L2 Switch", "DAC L2" },
	{ "SPO L MIX", "SPKVOL L Switch", "SPKVOL L" },
	{ "SPO R MIX", "DAC R2 Switch", "DAC R2" },
	{ "SPO R MIX", "SPKVOL R Switch", "SPKVOL R" },
	{ "SPK Amp", NULL, "SPO L MIX" },
	{ "SPK Amp", NULL, "SPO R MIX" },
	{ "SPK Amp", NULL, "SYS CLK DET" },
	{ "SPO Playback", "Switch", "SPK Amp" },
	{ "SPOL", NULL, "SPO Playback" },
	{ "SPOR", NULL, "SPO Playback" },

	{ "MONOVOL", "Switch", "MONOVOL MIX" },
	{ "Mono MIX", "DAC L2 Switch", "DAC L2" },
	{ "Mono MIX", "MONOVOL Switch", "MONOVOL" },
	{ "Mono Amp", NULL, "Mono MIX" },
	{ "Mono Amp", NULL, "Mono Vref" },
	{ "Mono Amp", NULL, "SYS CLK DET" },
	{ "Mono Playback", "Switch", "Mono Amp" },
	{ "MONOOUT", NULL, "Mono Playback" },

	{ "HP Amp", NULL, "DAC L1" },
	{ "HP Amp", NULL, "DAC R1" },
	{ "HP Amp", NULL, "Charge Pump" },
	{ "HP Amp", NULL, "SYS CLK DET" },
	{ "HPO L Playback", "Switch", "HP Amp"},
	{ "HPO R Playback", "Switch", "HP Amp"},
	{ "HPOL", NULL, "HPO L Playback" },
	{ "HPOR", NULL, "HPO R Playback" },

	{ "OUTVOL L", "Switch", "OUT MIXL" },
	{ "OUTVOL R", "Switch", "OUT MIXR" },
	{ "LOUT L MIX", "DAC L2 Switch", "DAC L2" },
	{ "LOUT L MIX", "OUTVOL L Switch", "OUTVOL L" },
	{ "LOUT R MIX", "DAC R2 Switch", "DAC R2" },
	{ "LOUT R MIX", "OUTVOL R Switch", "OUTVOL R" },
	{ "LOUT Amp", NULL, "LOUT L MIX" },
	{ "LOUT Amp", NULL, "LOUT R MIX" },
	{ "LOUT Amp", NULL, "Charge Pump" },
	{ "LOUT Amp", NULL, "SYS CLK DET" },
	{ "LOUT L Playback", "Switch", "LOUT Amp" },
	{ "LOUT R Playback", "Switch", "LOUT Amp" },
	{ "LOUTL", NULL, "LOUT L Playback" },
	{ "LOUTR", NULL, "LOUT R Playback" },

	{ "PDM L Mux", "Mono DAC", "Mono DAC MIXL" },
	{ "PDM L Mux", "Stereo DAC", "Stereo DAC MIXL" },
	{ "PDM L Mux", NULL, "PDM Power" },
	{ "PDM R Mux", "Mono DAC", "Mono DAC MIXR" },
	{ "PDM R Mux", "Stereo DAC", "Stereo DAC MIXR" },
	{ "PDM R Mux", NULL, "PDM Power" },
	{ "PDM L Playback", "Switch", "PDM L Mux" },
	{ "PDM R Playback", "Switch", "PDM R Mux" },
	{ "PDML", NULL, "PDM L Playback" },
	{ "PDMR", NULL, "PDM R Playback" },

	{ "SPDIF Mux", "IF3_DAC", "IF3 DAC" },
	{ "SPDIF Mux", "IF2_DAC", "IF2 DAC" },
	{ "SPDIF Mux", "IF1_DAC2", "IF1 DAC2" },
	{ "SPDIF Mux", "IF1_DAC1", "IF1 DAC1" },
	{ "SPDIF", NULL, "SPDIF Mux" },
};

static int rt5659_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, frame_size;

	rt5659->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5659->sysclk, rt5659->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock setting %d for DAI %d\n",
			rt5659->lrck[dai->id], dai->id);
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}

	dev_dbg(dai->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				rt5659->lrck[dai->id], pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len |= RT5659_I2S_DL_20;
		break;
	case 24:
		val_len |= RT5659_I2S_DL_24;
		break;
	case 8:
		val_len |= RT5659_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5659_AIF1:
		mask_clk = RT5659_I2S_PD1_MASK;
		val_clk = pre_div << RT5659_I2S_PD1_SFT;
		snd_soc_component_update_bits(component, RT5659_I2S1_SDP,
			RT5659_I2S_DL_MASK, val_len);
		break;
	case RT5659_AIF2:
		mask_clk = RT5659_I2S_PD2_MASK;
		val_clk = pre_div << RT5659_I2S_PD2_SFT;
		snd_soc_component_update_bits(component, RT5659_I2S2_SDP,
			RT5659_I2S_DL_MASK, val_len);
		break;
	case RT5659_AIF3:
		mask_clk = RT5659_I2S_PD3_MASK;
		val_clk = pre_div << RT5659_I2S_PD3_SFT;
		snd_soc_component_update_bits(component, RT5659_I2S3_SDP,
			RT5659_I2S_DL_MASK, val_len);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1, mask_clk, val_clk);

	switch (rt5659->lrck[dai->id]) {
	case 192000:
		snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1,
			RT5659_DAC_OSR_MASK, RT5659_DAC_OSR_32);
		break;
	case 96000:
		snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1,
			RT5659_DAC_OSR_MASK, RT5659_DAC_OSR_64);
		break;
	default:
		snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1,
			RT5659_DAC_OSR_MASK, RT5659_DAC_OSR_128);
		break;
	}

	return 0;
}

static int rt5659_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5659->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5659_I2S_MS_S;
		rt5659->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5659_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5659_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5659_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5659_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5659_AIF1:
		snd_soc_component_update_bits(component, RT5659_I2S1_SDP,
			RT5659_I2S_MS_MASK | RT5659_I2S_BP_MASK |
			RT5659_I2S_DF_MASK, reg_val);
		break;
	case RT5659_AIF2:
		snd_soc_component_update_bits(component, RT5659_I2S2_SDP,
			RT5659_I2S_MS_MASK | RT5659_I2S_BP_MASK |
			RT5659_I2S_DF_MASK, reg_val);
		break;
	case RT5659_AIF3:
		snd_soc_component_update_bits(component, RT5659_I2S3_SDP,
			RT5659_I2S_MS_MASK | RT5659_I2S_BP_MASK |
			RT5659_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5659_set_component_sysclk(struct snd_soc_component *component, int clk_id,
				   int source, unsigned int freq, int dir)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt5659->sysclk && clk_id == rt5659->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5659_SCLK_S_MCLK:
		reg_val |= RT5659_SCLK_SRC_MCLK;
		break;
	case RT5659_SCLK_S_PLL1:
		reg_val |= RT5659_SCLK_SRC_PLL1;
		break;
	case RT5659_SCLK_S_RCCLK:
		reg_val |= RT5659_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, RT5659_GLB_CLK,
		RT5659_SCLK_SRC_MASK, reg_val);
	rt5659->sysclk = freq;
	rt5659->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static int rt5659_set_component_pll(struct snd_soc_component *component, int pll_id,
				int source, unsigned int freq_in,
				unsigned int freq_out)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5659->pll_src && freq_in == rt5659->pll_in &&
	    freq_out == rt5659->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt5659->pll_in = 0;
		rt5659->pll_out = 0;
		snd_soc_component_update_bits(component, RT5659_GLB_CLK,
			RT5659_SCLK_SRC_MASK, RT5659_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5659_PLL1_S_MCLK:
		snd_soc_component_update_bits(component, RT5659_GLB_CLK,
			RT5659_PLL1_SRC_MASK, RT5659_PLL1_SRC_MCLK);
		break;
	case RT5659_PLL1_S_BCLK1:
		snd_soc_component_update_bits(component, RT5659_GLB_CLK,
				RT5659_PLL1_SRC_MASK, RT5659_PLL1_SRC_BCLK1);
		break;
	case RT5659_PLL1_S_BCLK2:
		snd_soc_component_update_bits(component, RT5659_GLB_CLK,
				RT5659_PLL1_SRC_MASK, RT5659_PLL1_SRC_BCLK2);
		break;
	case RT5659_PLL1_S_BCLK3:
		snd_soc_component_update_bits(component, RT5659_GLB_CLK,
				RT5659_PLL1_SRC_MASK, RT5659_PLL1_SRC_BCLK3);
		break;
	default:
		dev_err(component->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_component_write(component, RT5659_PLL_CTRL_1,
		pll_code.n_code << RT5659_PLL_N_SFT | pll_code.k_code);
	snd_soc_component_write(component, RT5659_PLL_CTRL_2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5659_PLL_M_SFT |
		pll_code.m_bp << RT5659_PLL_M_BP_SFT);

	rt5659->pll_in = freq_in;
	rt5659->pll_out = freq_out;
	rt5659->pll_src = source;

	return 0;
}

static int rt5659_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	unsigned int val = 0;

	if (rx_mask || tx_mask)
		val |= (1 << 15);

	switch (slots) {
	case 4:
		val |= (1 << 10);
		val |= (1 << 8);
		break;
	case 6:
		val |= (2 << 10);
		val |= (2 << 8);
		break;
	case 8:
		val |= (3 << 10);
		val |= (3 << 8);
		break;
	case 2:
		break;
	default:
		return -EINVAL;
	}

	switch (slot_width) {
	case 20:
		val |= (1 << 6);
		val |= (1 << 4);
		break;
	case 24:
		val |= (2 << 6);
		val |= (2 << 4);
		break;
	case 32:
		val |= (3 << 6);
		val |= (3 << 4);
		break;
	case 16:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5659_TDM_CTRL_1, 0x8ff0, val);

	return 0;
}

static int rt5659_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_component *component = dai->component;
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s ratio=%d\n", __func__, ratio);

	rt5659->bclk[dai->id] = ratio;

	if (ratio == 64) {
		switch (dai->id) {
		case RT5659_AIF2:
			snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1,
				RT5659_I2S_BCLK_MS2_MASK,
				RT5659_I2S_BCLK_MS2_64);
			break;
		case RT5659_AIF3:
			snd_soc_component_update_bits(component, RT5659_ADDA_CLK_1,
				RT5659_I2S_BCLK_MS3_MASK,
				RT5659_I2S_BCLK_MS3_64);
			break;
		}
	}

	return 0;
}

static int rt5659_set_bias_level(struct snd_soc_component *component,
			enum snd_soc_bias_level level)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		regmap_update_bits(rt5659->regmap, RT5659_DIG_MISC,
			RT5659_DIG_GATE_CTRL, RT5659_DIG_GATE_CTRL);
		regmap_update_bits(rt5659->regmap, RT5659_PWR_DIG_1,
			RT5659_PWR_LDO,	RT5659_PWR_LDO);
		regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_1,
			RT5659_PWR_MB | RT5659_PWR_VREF1 | RT5659_PWR_VREF2,
			RT5659_PWR_MB | RT5659_PWR_VREF1 | RT5659_PWR_VREF2);
		msleep(20);
		regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_1,
			RT5659_PWR_FV1 | RT5659_PWR_FV2,
			RT5659_PWR_FV1 | RT5659_PWR_FV2);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (dapm->bias_level == SND_SOC_BIAS_OFF) {
			ret = clk_prepare_enable(rt5659->mclk);
			if (ret) {
				dev_err(component->dev,
					"failed to enable MCLK: %d\n", ret);
				return ret;
			}
		}
		break;

	case SND_SOC_BIAS_OFF:
		regmap_update_bits(rt5659->regmap, RT5659_PWR_DIG_1,
			RT5659_PWR_LDO, 0);
		regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_1,
			RT5659_PWR_MB | RT5659_PWR_VREF1 | RT5659_PWR_VREF2
			| RT5659_PWR_FV1 | RT5659_PWR_FV2,
			RT5659_PWR_MB | RT5659_PWR_VREF2);
		regmap_update_bits(rt5659->regmap, RT5659_DIG_MISC,
			RT5659_DIG_GATE_CTRL, 0);
		clk_disable_unprepare(rt5659->mclk);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5659_probe(struct snd_soc_component *component)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	rt5659->component = component;

	return 0;
}

static void rt5659_remove(struct snd_soc_component *component)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	regmap_write(rt5659->regmap, RT5659_RESET, 0);
}

#ifdef CONFIG_PM
static int rt5659_suspend(struct snd_soc_component *component)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt5659->regmap, true);
	regcache_mark_dirty(rt5659->regmap);
	return 0;
}

static int rt5659_resume(struct snd_soc_component *component)
{
	struct rt5659_priv *rt5659 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt5659->regmap, false);
	regcache_sync(rt5659->regmap);

	return 0;
}
#else
#define rt5659_suspend NULL
#define rt5659_resume NULL
#endif

#define RT5659_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5659_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5659_aif_dai_ops = {
	.hw_params = rt5659_hw_params,
	.set_fmt = rt5659_set_dai_fmt,
	.set_tdm_slot = rt5659_set_tdm_slot,
	.set_bclk_ratio = rt5659_set_bclk_ratio,
};

static struct snd_soc_dai_driver rt5659_dai[] = {
	{
		.name = "rt5659-aif1",
		.id = RT5659_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5659_STEREO_RATES,
			.formats = RT5659_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5659_STEREO_RATES,
			.formats = RT5659_FORMATS,
		},
		.ops = &rt5659_aif_dai_ops,
	},
	{
		.name = "rt5659-aif2",
		.id = RT5659_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5659_STEREO_RATES,
			.formats = RT5659_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5659_STEREO_RATES,
			.formats = RT5659_FORMATS,
		},
		.ops = &rt5659_aif_dai_ops,
	},
	{
		.name = "rt5659-aif3",
		.id = RT5659_AIF3,
		.playback = {
			.stream_name = "AIF3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5659_STEREO_RATES,
			.formats = RT5659_FORMATS,
		},
		.capture = {
			.stream_name = "AIF3 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5659_STEREO_RATES,
			.formats = RT5659_FORMATS,
		},
		.ops = &rt5659_aif_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_rt5659 = {
	.probe			= rt5659_probe,
	.remove			= rt5659_remove,
	.suspend		= rt5659_suspend,
	.resume			= rt5659_resume,
	.set_bias_level		= rt5659_set_bias_level,
	.controls		= rt5659_snd_controls,
	.num_controls		= ARRAY_SIZE(rt5659_snd_controls),
	.dapm_widgets		= rt5659_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rt5659_dapm_widgets),
	.dapm_routes		= rt5659_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rt5659_dapm_routes),
	.set_sysclk		= rt5659_set_component_sysclk,
	.set_pll		= rt5659_set_component_pll,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};


static const struct regmap_config rt5659_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = 0x0400,
	.volatile_reg = rt5659_volatile_register,
	.readable_reg = rt5659_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5659_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5659_reg),
};

static const struct i2c_device_id rt5659_i2c_id[] = {
	{ "rt5658", 0 },
	{ "rt5659", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5659_i2c_id);

static int rt5659_parse_dt(struct rt5659_priv *rt5659, struct device *dev)
{
	rt5659->pdata.in1_diff = device_property_read_bool(dev,
					"realtek,in1-differential");
	rt5659->pdata.in3_diff = device_property_read_bool(dev,
					"realtek,in3-differential");
	rt5659->pdata.in4_diff = device_property_read_bool(dev,
					"realtek,in4-differential");


	device_property_read_u32(dev, "realtek,dmic1-data-pin",
		&rt5659->pdata.dmic1_data_pin);
	device_property_read_u32(dev, "realtek,dmic2-data-pin",
		&rt5659->pdata.dmic2_data_pin);
	device_property_read_u32(dev, "realtek,jd-src",
		&rt5659->pdata.jd_src);

	return 0;
}

static void rt5659_calibrate(struct rt5659_priv *rt5659)
{
	int value, count;

	/* Calibrate HPO Start */
	/* Fine tune HP Performance */
	regmap_write(rt5659->regmap, RT5659_BIAS_CUR_CTRL_8, 0xa502);
	regmap_write(rt5659->regmap, RT5659_CHOP_DAC, 0x3030);

	regmap_write(rt5659->regmap, RT5659_PRE_DIV_1, 0xef00);
	regmap_write(rt5659->regmap, RT5659_PRE_DIV_2, 0xeffc);
	regmap_write(rt5659->regmap, RT5659_MICBIAS_2, 0x0280);
	regmap_write(rt5659->regmap, RT5659_DIG_MISC, 0x0001);
	regmap_write(rt5659->regmap, RT5659_GLB_CLK, 0x8000);

	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_1, 0xaa7e);
	msleep(60);
	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_1, 0xfe7e);
	msleep(50);
	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_3, 0x0004);
	regmap_write(rt5659->regmap, RT5659_PWR_DIG_2, 0x0400);
	msleep(50);
	regmap_write(rt5659->regmap, RT5659_PWR_DIG_1, 0x0080);
	usleep_range(10000, 10005);
	regmap_write(rt5659->regmap, RT5659_DEPOP_1, 0x0009);
	msleep(50);
	regmap_write(rt5659->regmap, RT5659_PWR_DIG_1, 0x0f80);
	msleep(50);
	regmap_write(rt5659->regmap, RT5659_HP_CHARGE_PUMP_1, 0x0e16);
	msleep(50);

	/* Enalbe K ADC Power And Clock */
	regmap_write(rt5659->regmap, RT5659_CAL_REC, 0x0505);
	msleep(50);
	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_3, 0x0184);
	regmap_write(rt5659->regmap, RT5659_CALIB_ADC_CTRL, 0x3c05);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x20c1);

	/* K Headphone */
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x2cc1);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_1, 0x5100);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_7, 0x0014);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_1, 0xd100);
	msleep(60);

	/* Manual K ADC Offset */
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x2cc1);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_1, 0x4900);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_7, 0x0016);
	regmap_update_bits(rt5659->regmap, RT5659_HP_CALIB_CTRL_1,
		0x8000, 0x8000);

	count = 0;
	while (true) {
		regmap_read(rt5659->regmap, RT5659_HP_CALIB_CTRL_1, &value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 30) {
			dev_err(rt5659->component->dev,
				"HP Calibration 1 Failure\n");
			return;
		}

		count++;
	}

	/* Manual K Internal Path Offset */
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x2cc1);
	regmap_write(rt5659->regmap, RT5659_HP_VOL, 0x0000);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_1, 0x4500);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_7, 0x001f);
	regmap_update_bits(rt5659->regmap, RT5659_HP_CALIB_CTRL_1,
		0x8000, 0x8000);

	count = 0;
	while (true) {
		regmap_read(rt5659->regmap, RT5659_HP_CALIB_CTRL_1, &value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 85) {
			dev_err(rt5659->component->dev,
				"HP Calibration 2 Failure\n");
			return;
		}

		count++;
	}

	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_7, 0x0000);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x20c0);
	/* Calibrate HPO End */

	/* Calibrate SPO Start */
	regmap_write(rt5659->regmap, RT5659_CLASSD_0, 0x2021);
	regmap_write(rt5659->regmap, RT5659_CLASSD_CTRL_1, 0x0260);
	regmap_write(rt5659->regmap, RT5659_PWR_MIXER, 0x3000);
	regmap_write(rt5659->regmap, RT5659_PWR_VOL, 0xc000);
	regmap_write(rt5659->regmap, RT5659_A_DAC_MUX, 0x000c);
	regmap_write(rt5659->regmap, RT5659_DIG_MISC, 0x8000);
	regmap_write(rt5659->regmap, RT5659_SPO_VOL, 0x0808);
	regmap_write(rt5659->regmap, RT5659_SPK_L_MIXER, 0x001e);
	regmap_write(rt5659->regmap, RT5659_SPK_R_MIXER, 0x001e);
	regmap_write(rt5659->regmap, RT5659_CLASSD_1, 0x0803);
	regmap_write(rt5659->regmap, RT5659_CLASSD_2, 0x0554);
	regmap_write(rt5659->regmap, RT5659_SPO_AMP_GAIN, 0x1103);

	/* Enalbe K ADC Power And Clock */
	regmap_write(rt5659->regmap, RT5659_CAL_REC, 0x0909);
	regmap_update_bits(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x0001,
		0x0001);

	/* Start Calibration */
	regmap_write(rt5659->regmap, RT5659_SPK_DC_CAILB_CTRL_3, 0x0000);
	regmap_write(rt5659->regmap, RT5659_CLASSD_0, 0x0021);
	regmap_write(rt5659->regmap, RT5659_SPK_DC_CAILB_CTRL_1, 0x3e80);
	regmap_update_bits(rt5659->regmap, RT5659_SPK_DC_CAILB_CTRL_1,
		0x8000, 0x8000);

	count = 0;
	while (true) {
		regmap_read(rt5659->regmap,
				RT5659_SPK_DC_CAILB_CTRL_1, &value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 10) {
			dev_err(rt5659->component->dev,
				"SPK Calibration Failure\n");
			return;
		}

		count++;
	}
	/* Calibrate SPO End */

	/* Calibrate MONO Start */
	regmap_write(rt5659->regmap, RT5659_DIG_MISC, 0x0000);
	regmap_write(rt5659->regmap, RT5659_MONOMIX_IN_GAIN, 0x021f);
	regmap_write(rt5659->regmap, RT5659_MONO_OUT, 0x480a);
	/* MONO NG2 GAIN 5dB */
	regmap_write(rt5659->regmap, RT5659_MONO_GAIN, 0x0003);
	regmap_write(rt5659->regmap, RT5659_MONO_NG2_CTRL_5, 0x0009);

	/* Start Calibration */
	regmap_write(rt5659->regmap, RT5659_SPK_DC_CAILB_CTRL_3, 0x000f);
	regmap_write(rt5659->regmap, RT5659_MONO_AMP_CALIB_CTRL_1, 0x1e00);
	regmap_update_bits(rt5659->regmap, RT5659_MONO_AMP_CALIB_CTRL_1,
		0x8000, 0x8000);

	count = 0;
	while (true) {
		regmap_read(rt5659->regmap, RT5659_MONO_AMP_CALIB_CTRL_1,
			&value);
		if (value & 0x8000)
			usleep_range(10000, 10005);
		else
			break;

		if (count > 35) {
			dev_err(rt5659->component->dev,
				"Mono Calibration Failure\n");
			return;
		}

		count++;
	}

	regmap_write(rt5659->regmap, RT5659_SPK_DC_CAILB_CTRL_3, 0x0003);
	/* Calibrate MONO End */

	/* Power Off */
	regmap_write(rt5659->regmap, RT5659_CAL_REC, 0x0808);
	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_3, 0x0000);
	regmap_write(rt5659->regmap, RT5659_CALIB_ADC_CTRL, 0x2005);
	regmap_write(rt5659->regmap, RT5659_HP_CALIB_CTRL_2, 0x20c0);
	regmap_write(rt5659->regmap, RT5659_DEPOP_1, 0x0000);
	regmap_write(rt5659->regmap, RT5659_CLASSD_1, 0x0011);
	regmap_write(rt5659->regmap, RT5659_CLASSD_2, 0x0150);
	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_1, 0xfe3e);
	regmap_write(rt5659->regmap, RT5659_MONO_OUT, 0xc80a);
	regmap_write(rt5659->regmap, RT5659_MONO_AMP_CALIB_CTRL_1, 0x1e04);
	regmap_write(rt5659->regmap, RT5659_PWR_MIXER, 0x0000);
	regmap_write(rt5659->regmap, RT5659_PWR_VOL, 0x0000);
	regmap_write(rt5659->regmap, RT5659_PWR_DIG_1, 0x0000);
	regmap_write(rt5659->regmap, RT5659_PWR_DIG_2, 0x0000);
	regmap_write(rt5659->regmap, RT5659_PWR_ANLG_1, 0x003e);
	regmap_write(rt5659->regmap, RT5659_CLASSD_CTRL_1, 0x0060);
	regmap_write(rt5659->regmap, RT5659_CLASSD_0, 0x2021);
	regmap_write(rt5659->regmap, RT5659_GLB_CLK, 0x0000);
	regmap_write(rt5659->regmap, RT5659_MICBIAS_2, 0x0080);
	regmap_write(rt5659->regmap, RT5659_HP_VOL, 0x8080);
	regmap_write(rt5659->regmap, RT5659_HP_CHARGE_PUMP_1, 0x0c16);
}

static void rt5659_intel_hd_header_probe_setup(struct rt5659_priv *rt5659)
{
	int value;

	regmap_read(rt5659->regmap, RT5659_GPIO_STA, &value);
	if (!(value & 0x8)) {
		rt5659->hda_hp_plugged = true;
		regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_1,
			0x10, 0x0);
	} else {
		regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_1,
			0x10, 0x10);
	}

	regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_1,
		RT5659_PWR_VREF2 | RT5659_PWR_MB,
		RT5659_PWR_VREF2 | RT5659_PWR_MB);
	msleep(20);
	regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_1,
		RT5659_PWR_FV2, RT5659_PWR_FV2);

	regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_3, RT5659_PWR_LDO2,
		RT5659_PWR_LDO2);
	regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_2, RT5659_PWR_MB1,
		RT5659_PWR_MB1);
	regmap_update_bits(rt5659->regmap, RT5659_PWR_VOL, RT5659_PWR_MIC_DET,
		RT5659_PWR_MIC_DET);
	msleep(20);

	regmap_update_bits(rt5659->regmap, RT5659_4BTN_IL_CMD_2,
		RT5659_4BTN_IL_MASK, RT5659_4BTN_IL_EN);
	regmap_read(rt5659->regmap, RT5659_4BTN_IL_CMD_1, &value);
	regmap_write(rt5659->regmap, RT5659_4BTN_IL_CMD_1, value);
	regmap_read(rt5659->regmap, RT5659_4BTN_IL_CMD_1, &value);

	if (value & 0x2000) {
		rt5659->hda_mic_plugged = true;
		regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_2,
			0x2, 0x2);
	} else {
		regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_2,
			0x2, 0x0);
	}

	regmap_update_bits(rt5659->regmap, RT5659_IRQ_CTRL_2,
		RT5659_IL_IRQ_MASK, RT5659_IL_IRQ_EN);
}

static int rt5659_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5659_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5659_priv *rt5659;
	int ret;
	unsigned int val;

	rt5659 = devm_kzalloc(&i2c->dev, sizeof(struct rt5659_priv),
		GFP_KERNEL);

	if (rt5659 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5659);

	if (pdata)
		rt5659->pdata = *pdata;
	else
		rt5659_parse_dt(rt5659, &i2c->dev);

	rt5659->gpiod_ldo1_en = devm_gpiod_get_optional(&i2c->dev, "ldo1-en",
							GPIOD_OUT_HIGH);
	if (IS_ERR(rt5659->gpiod_ldo1_en))
		dev_warn(&i2c->dev, "Request ldo1-en GPIO failed\n");

	rt5659->gpiod_reset = devm_gpiod_get_optional(&i2c->dev, "reset",
							GPIOD_OUT_HIGH);

	/* Sleep for 300 ms miniumum */
	msleep(300);

	rt5659->regmap = devm_regmap_init_i2c(i2c, &rt5659_regmap);
	if (IS_ERR(rt5659->regmap)) {
		ret = PTR_ERR(rt5659->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt5659->regmap, RT5659_DEVICE_ID, &val);
	if (val != DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt5659\n", val);
		return -ENODEV;
	}

	regmap_write(rt5659->regmap, RT5659_RESET, 0);

	/* Check if MCLK provided */
	rt5659->mclk = devm_clk_get(&i2c->dev, "mclk");
	if (IS_ERR(rt5659->mclk)) {
		if (PTR_ERR(rt5659->mclk) != -ENOENT)
			return PTR_ERR(rt5659->mclk);
		/* Otherwise mark the mclk pointer to NULL */
		rt5659->mclk = NULL;
	}

	rt5659_calibrate(rt5659);

	/* line in diff mode*/
	if (rt5659->pdata.in1_diff)
		regmap_update_bits(rt5659->regmap, RT5659_IN1_IN2,
			RT5659_IN1_DF_MASK, RT5659_IN1_DF_MASK);
	if (rt5659->pdata.in3_diff)
		regmap_update_bits(rt5659->regmap, RT5659_IN3_IN4,
			RT5659_IN3_DF_MASK, RT5659_IN3_DF_MASK);
	if (rt5659->pdata.in4_diff)
		regmap_update_bits(rt5659->regmap, RT5659_IN3_IN4,
			RT5659_IN4_DF_MASK, RT5659_IN4_DF_MASK);

	/* DMIC pin*/
	if (rt5659->pdata.dmic1_data_pin != RT5659_DMIC1_NULL ||
		rt5659->pdata.dmic2_data_pin != RT5659_DMIC2_NULL) {
		regmap_update_bits(rt5659->regmap, RT5659_GPIO_CTRL_1,
			RT5659_GP2_PIN_MASK, RT5659_GP2_PIN_DMIC1_SCL);

		switch (rt5659->pdata.dmic1_data_pin) {
		case RT5659_DMIC1_DATA_IN2N:
			regmap_update_bits(rt5659->regmap, RT5659_DMIC_CTRL_1,
				RT5659_DMIC_1_DP_MASK, RT5659_DMIC_1_DP_IN2N);
			break;

		case RT5659_DMIC1_DATA_GPIO5:
			regmap_update_bits(rt5659->regmap,
				RT5659_GPIO_CTRL_3,
				RT5659_I2S2_PIN_MASK,
				RT5659_I2S2_PIN_GPIO);
			regmap_update_bits(rt5659->regmap, RT5659_DMIC_CTRL_1,
				RT5659_DMIC_1_DP_MASK, RT5659_DMIC_1_DP_GPIO5);
			regmap_update_bits(rt5659->regmap, RT5659_GPIO_CTRL_1,
				RT5659_GP5_PIN_MASK, RT5659_GP5_PIN_DMIC1_SDA);
			break;

		case RT5659_DMIC1_DATA_GPIO9:
			regmap_update_bits(rt5659->regmap, RT5659_DMIC_CTRL_1,
				RT5659_DMIC_1_DP_MASK, RT5659_DMIC_1_DP_GPIO9);
			regmap_update_bits(rt5659->regmap, RT5659_GPIO_CTRL_1,
				RT5659_GP9_PIN_MASK, RT5659_GP9_PIN_DMIC1_SDA);
			break;

		case RT5659_DMIC1_DATA_GPIO11:
			regmap_update_bits(rt5659->regmap, RT5659_DMIC_CTRL_1,
				RT5659_DMIC_1_DP_MASK, RT5659_DMIC_1_DP_GPIO11);
			regmap_update_bits(rt5659->regmap, RT5659_GPIO_CTRL_1,
				RT5659_GP11_PIN_MASK,
				RT5659_GP11_PIN_DMIC1_SDA);
			break;

		default:
			dev_dbg(&i2c->dev, "no DMIC1\n");
			break;
		}

		switch (rt5659->pdata.dmic2_data_pin) {
		case RT5659_DMIC2_DATA_IN2P:
			regmap_update_bits(rt5659->regmap,
				RT5659_DMIC_CTRL_1,
				RT5659_DMIC_2_DP_MASK,
				RT5659_DMIC_2_DP_IN2P);
			break;

		case RT5659_DMIC2_DATA_GPIO6:
			regmap_update_bits(rt5659->regmap,
				RT5659_DMIC_CTRL_1,
				RT5659_DMIC_2_DP_MASK,
				RT5659_DMIC_2_DP_GPIO6);
			regmap_update_bits(rt5659->regmap,
				RT5659_GPIO_CTRL_1,
				RT5659_GP6_PIN_MASK,
				RT5659_GP6_PIN_DMIC2_SDA);
			break;

		case RT5659_DMIC2_DATA_GPIO10:
			regmap_update_bits(rt5659->regmap,
				RT5659_DMIC_CTRL_1,
				RT5659_DMIC_2_DP_MASK,
				RT5659_DMIC_2_DP_GPIO10);
			regmap_update_bits(rt5659->regmap,
				RT5659_GPIO_CTRL_1,
				RT5659_GP10_PIN_MASK,
				RT5659_GP10_PIN_DMIC2_SDA);
			break;

		case RT5659_DMIC2_DATA_GPIO12:
			regmap_update_bits(rt5659->regmap,
				RT5659_DMIC_CTRL_1,
				RT5659_DMIC_2_DP_MASK,
				RT5659_DMIC_2_DP_GPIO12);
			regmap_update_bits(rt5659->regmap,
				RT5659_GPIO_CTRL_1,
				RT5659_GP12_PIN_MASK,
				RT5659_GP12_PIN_DMIC2_SDA);
			break;

		default:
			dev_dbg(&i2c->dev, "no DMIC2\n");
			break;

		}
	} else {
		regmap_update_bits(rt5659->regmap, RT5659_GPIO_CTRL_1,
			RT5659_GP2_PIN_MASK | RT5659_GP5_PIN_MASK |
			RT5659_GP9_PIN_MASK | RT5659_GP11_PIN_MASK |
			RT5659_GP6_PIN_MASK | RT5659_GP10_PIN_MASK |
			RT5659_GP12_PIN_MASK,
			RT5659_GP2_PIN_GPIO2 | RT5659_GP5_PIN_GPIO5 |
			RT5659_GP9_PIN_GPIO9 | RT5659_GP11_PIN_GPIO11 |
			RT5659_GP6_PIN_GPIO6 | RT5659_GP10_PIN_GPIO10 |
			RT5659_GP12_PIN_GPIO12);
		regmap_update_bits(rt5659->regmap, RT5659_DMIC_CTRL_1,
			RT5659_DMIC_1_DP_MASK | RT5659_DMIC_2_DP_MASK,
			RT5659_DMIC_1_DP_IN2N | RT5659_DMIC_2_DP_IN2P);
	}

	switch (rt5659->pdata.jd_src) {
	case RT5659_JD3:
		regmap_write(rt5659->regmap, RT5659_EJD_CTRL_1, 0xa880);
		regmap_write(rt5659->regmap, RT5659_RC_CLK_CTRL, 0x9000);
		regmap_write(rt5659->regmap, RT5659_GPIO_CTRL_1, 0xc800);
		regmap_update_bits(rt5659->regmap, RT5659_PWR_ANLG_1,
				RT5659_PWR_MB, RT5659_PWR_MB);
		regmap_write(rt5659->regmap, RT5659_PWR_ANLG_2, 0x0001);
		regmap_write(rt5659->regmap, RT5659_IRQ_CTRL_2, 0x0040);
		INIT_DELAYED_WORK(&rt5659->jack_detect_work,
			rt5659_jack_detect_work);
		break;
	case RT5659_JD_HDA_HEADER:
		regmap_write(rt5659->regmap, RT5659_GPIO_CTRL_3, 0x8000);
		regmap_write(rt5659->regmap, RT5659_RC_CLK_CTRL, 0x0900);
		regmap_write(rt5659->regmap, RT5659_EJD_CTRL_1,  0x70c0);
		regmap_write(rt5659->regmap, RT5659_JD_CTRL_1,   0x2000);
		regmap_write(rt5659->regmap, RT5659_IRQ_CTRL_1,  0x0040);
		INIT_DELAYED_WORK(&rt5659->jack_detect_work,
			rt5659_jack_detect_intel_hd_header);
		rt5659_intel_hd_header_probe_setup(rt5659);
		break;
	default:
		break;
	}

	if (i2c->irq) {
		ret = devm_request_threaded_irq(&i2c->dev, i2c->irq, NULL,
			rt5659_irq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			| IRQF_ONESHOT, "rt5659", rt5659);
		if (ret)
			dev_err(&i2c->dev, "Failed to reguest IRQ: %d\n", ret);

		/* Enable IRQ output for GPIO1 pin any way */
		regmap_update_bits(rt5659->regmap, RT5659_GPIO_CTRL_1,
				   RT5659_GP1_PIN_MASK, RT5659_GP1_PIN_IRQ);
	}

	return devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_rt5659,
			rt5659_dai, ARRAY_SIZE(rt5659_dai));
}

static void rt5659_i2c_shutdown(struct i2c_client *client)
{
	struct rt5659_priv *rt5659 = i2c_get_clientdata(client);

	regmap_write(rt5659->regmap, RT5659_RESET, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id rt5659_of_match[] = {
	{ .compatible = "realtek,rt5658", },
	{ .compatible = "realtek,rt5659", },
	{ },
};
MODULE_DEVICE_TABLE(of, rt5659_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt5659_acpi_match[] = {
	{ "10EC5658", 0, },
	{ "10EC5659", 0, },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt5659_acpi_match);
#endif

static struct i2c_driver rt5659_i2c_driver = {
	.driver = {
		.name = "rt5659",
		.of_match_table = of_match_ptr(rt5659_of_match),
		.acpi_match_table = ACPI_PTR(rt5659_acpi_match),
	},
	.probe = rt5659_i2c_probe,
	.shutdown = rt5659_i2c_shutdown,
	.id_table = rt5659_i2c_id,
};
module_i2c_driver(rt5659_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5659 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
