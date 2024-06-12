// SPDX-License-Identifier: GPL-2.0
/*
 * rt1011.c -- rt1011 ALSA SoC amplifier component driver
 *
 * Copyright(c) 2019 Realtek Semiconductor Corp.
 *
 * Author: Shuming Fan <shumingf@realtek.com>
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt1011.h"

static int rt1011_calibrate(struct rt1011_priv *rt1011,
	unsigned char cali_flag);

static const struct reg_sequence init_list[] = {

	{ RT1011_POWER_9, 0xa840 },

	{ RT1011_ADC_SET_5, 0x0a20 },
	{ RT1011_DAC_SET_2, 0xa032 },

	{ RT1011_SPK_PRO_DC_DET_1, 0xb00c },
	{ RT1011_SPK_PRO_DC_DET_2, 0xcccc },

	{ RT1011_A_TIMING_1, 0x6054 },

	{ RT1011_POWER_7, 0x3e55 },
	{ RT1011_POWER_8, 0x0520 },
	{ RT1011_BOOST_CON_1, 0xe188 },
	{ RT1011_POWER_4, 0x16f2 },

	{ RT1011_CROSS_BQ_SET_1, 0x0004 },
	{ RT1011_SIL_DET, 0xc313 },
	{ RT1011_SINE_GEN_REG_1, 0x0707 },

	{ RT1011_DC_CALIB_CLASSD_3, 0xcb00 },

	{ RT1011_DAC_SET_1, 0xe702 },
	{ RT1011_DAC_SET_3, 0x2004 },
};

static const struct reg_default rt1011_reg[] = {
	{0x0000, 0x0000},
	{0x0002, 0x0000},
	{0x0004, 0xa000},
	{0x0006, 0x0000},
	{0x0008, 0x0003},
	{0x000a, 0x087e},
	{0x000c, 0x0020},
	{0x000e, 0x9002},
	{0x0010, 0x0000},
	{0x0012, 0x0000},
	{0x0020, 0x0c40},
	{0x0022, 0x4313},
	{0x0076, 0x0000},
	{0x0078, 0x0000},
	{0x007a, 0x0000},
	{0x007c, 0x10ec},
	{0x007d, 0x1011},
	{0x00f0, 0x5000},
	{0x00f2, 0x0374},
	{0x00f3, 0x0000},
	{0x00f4, 0x0000},
	{0x0100, 0x0038},
	{0x0102, 0xff02},
	{0x0104, 0x0232},
	{0x0106, 0x200c},
	{0x0107, 0x0000},
	{0x0108, 0x2f2f},
	{0x010a, 0x2f2f},
	{0x010c, 0x002f},
	{0x010e, 0xe000},
	{0x0110, 0x0820},
	{0x0111, 0x4010},
	{0x0112, 0x0000},
	{0x0114, 0x0000},
	{0x0116, 0x0000},
	{0x0118, 0x0000},
	{0x011a, 0x0101},
	{0x011c, 0x4567},
	{0x011e, 0x0000},
	{0x0120, 0x0000},
	{0x0122, 0x0000},
	{0x0124, 0x0123},
	{0x0126, 0x4567},
	{0x0200, 0x0000},
	{0x0300, 0xffdd},
	{0x0302, 0x001e},
	{0x0311, 0x0000},
	{0x0313, 0x5254},
	{0x0314, 0x0062},
	{0x0316, 0x7f40},
	{0x0319, 0x000f},
	{0x031a, 0xffff},
	{0x031b, 0x0000},
	{0x031c, 0x009f},
	{0x031d, 0xffff},
	{0x031e, 0x0000},
	{0x031f, 0x0000},
	{0x0320, 0xe31c},
	{0x0321, 0x0000},
	{0x0322, 0x0000},
	{0x0324, 0x0000},
	{0x0326, 0x0002},
	{0x0328, 0x20b2},
	{0x0329, 0x0175},
	{0x032a, 0x32ad},
	{0x032b, 0x3455},
	{0x032c, 0x0528},
	{0x032d, 0xa800},
	{0x032e, 0x030e},
	{0x0330, 0x2080},
	{0x0332, 0x0034},
	{0x0334, 0x0000},
	{0x0508, 0x0010},
	{0x050a, 0x0018},
	{0x050c, 0x0000},
	{0x050d, 0xffff},
	{0x050e, 0x1f1f},
	{0x050f, 0x04ff},
	{0x0510, 0x4020},
	{0x0511, 0x01f0},
	{0x0512, 0x0702},
	{0x0516, 0xbb80},
	{0x0517, 0xffff},
	{0x0518, 0xffff},
	{0x0519, 0x307f},
	{0x051a, 0xffff},
	{0x051b, 0x0000},
	{0x051c, 0x0000},
	{0x051d, 0x2000},
	{0x051e, 0x0000},
	{0x051f, 0x0000},
	{0x0520, 0x0000},
	{0x0521, 0x1001},
	{0x0522, 0x7fff},
	{0x0524, 0x7fff},
	{0x0526, 0x0000},
	{0x0528, 0x0000},
	{0x052a, 0x0000},
	{0x0530, 0x0401},
	{0x0532, 0x3000},
	{0x0534, 0x0000},
	{0x0535, 0xffff},
	{0x0536, 0x101c},
	{0x0538, 0x1814},
	{0x053a, 0x100c},
	{0x053c, 0x0804},
	{0x053d, 0x0000},
	{0x053e, 0x0000},
	{0x053f, 0x0000},
	{0x0540, 0x0000},
	{0x0541, 0x0000},
	{0x0542, 0x0000},
	{0x0543, 0x0000},
	{0x0544, 0x001c},
	{0x0545, 0x1814},
	{0x0546, 0x100c},
	{0x0547, 0x0804},
	{0x0548, 0x0000},
	{0x0549, 0x0000},
	{0x054a, 0x0000},
	{0x054b, 0x0000},
	{0x054c, 0x0000},
	{0x054d, 0x0000},
	{0x054e, 0x0000},
	{0x054f, 0x0000},
	{0x0566, 0x0000},
	{0x0568, 0x20f1},
	{0x056a, 0x0007},
	{0x0600, 0x9d00},
	{0x0611, 0x2000},
	{0x0612, 0x505f},
	{0x0613, 0x0444},
	{0x0614, 0x4000},
	{0x0615, 0x4004},
	{0x0616, 0x0606},
	{0x0617, 0x8904},
	{0x0618, 0xe021},
	{0x0621, 0x2000},
	{0x0622, 0x505f},
	{0x0623, 0x0444},
	{0x0624, 0x4000},
	{0x0625, 0x4004},
	{0x0626, 0x0606},
	{0x0627, 0x8704},
	{0x0628, 0xe021},
	{0x0631, 0x2000},
	{0x0632, 0x517f},
	{0x0633, 0x0440},
	{0x0634, 0x4000},
	{0x0635, 0x4104},
	{0x0636, 0x0306},
	{0x0637, 0x8904},
	{0x0638, 0xe021},
	{0x0702, 0x0014},
	{0x0704, 0x0000},
	{0x0706, 0x0014},
	{0x0708, 0x0000},
	{0x070a, 0x0000},
	{0x0710, 0x0200},
	{0x0711, 0x0000},
	{0x0712, 0x0200},
	{0x0713, 0x0000},
	{0x0720, 0x0200},
	{0x0721, 0x0000},
	{0x0722, 0x0000},
	{0x0723, 0x0000},
	{0x0724, 0x0000},
	{0x0725, 0x0000},
	{0x0726, 0x0000},
	{0x0727, 0x0000},
	{0x0728, 0x0000},
	{0x0729, 0x0000},
	{0x0730, 0x0200},
	{0x0731, 0x0000},
	{0x0732, 0x0000},
	{0x0733, 0x0000},
	{0x0734, 0x0000},
	{0x0735, 0x0000},
	{0x0736, 0x0000},
	{0x0737, 0x0000},
	{0x0738, 0x0000},
	{0x0739, 0x0000},
	{0x0740, 0x0200},
	{0x0741, 0x0000},
	{0x0742, 0x0000},
	{0x0743, 0x0000},
	{0x0744, 0x0000},
	{0x0745, 0x0000},
	{0x0746, 0x0000},
	{0x0747, 0x0000},
	{0x0748, 0x0000},
	{0x0749, 0x0000},
	{0x0750, 0x0200},
	{0x0751, 0x0000},
	{0x0752, 0x0000},
	{0x0753, 0x0000},
	{0x0754, 0x0000},
	{0x0755, 0x0000},
	{0x0756, 0x0000},
	{0x0757, 0x0000},
	{0x0758, 0x0000},
	{0x0759, 0x0000},
	{0x0760, 0x0200},
	{0x0761, 0x0000},
	{0x0762, 0x0000},
	{0x0763, 0x0000},
	{0x0764, 0x0000},
	{0x0765, 0x0000},
	{0x0766, 0x0000},
	{0x0767, 0x0000},
	{0x0768, 0x0000},
	{0x0769, 0x0000},
	{0x0770, 0x0200},
	{0x0771, 0x0000},
	{0x0772, 0x0000},
	{0x0773, 0x0000},
	{0x0774, 0x0000},
	{0x0775, 0x0000},
	{0x0776, 0x0000},
	{0x0777, 0x0000},
	{0x0778, 0x0000},
	{0x0779, 0x0000},
	{0x0780, 0x0200},
	{0x0781, 0x0000},
	{0x0782, 0x0000},
	{0x0783, 0x0000},
	{0x0784, 0x0000},
	{0x0785, 0x0000},
	{0x0786, 0x0000},
	{0x0787, 0x0000},
	{0x0788, 0x0000},
	{0x0789, 0x0000},
	{0x0790, 0x0200},
	{0x0791, 0x0000},
	{0x0792, 0x0000},
	{0x0793, 0x0000},
	{0x0794, 0x0000},
	{0x0795, 0x0000},
	{0x0796, 0x0000},
	{0x0797, 0x0000},
	{0x0798, 0x0000},
	{0x0799, 0x0000},
	{0x07a0, 0x0200},
	{0x07a1, 0x0000},
	{0x07a2, 0x0000},
	{0x07a3, 0x0000},
	{0x07a4, 0x0000},
	{0x07a5, 0x0000},
	{0x07a6, 0x0000},
	{0x07a7, 0x0000},
	{0x07a8, 0x0000},
	{0x07a9, 0x0000},
	{0x07b0, 0x0200},
	{0x07b1, 0x0000},
	{0x07b2, 0x0000},
	{0x07b3, 0x0000},
	{0x07b4, 0x0000},
	{0x07b5, 0x0000},
	{0x07b6, 0x0000},
	{0x07b7, 0x0000},
	{0x07b8, 0x0000},
	{0x07b9, 0x0000},
	{0x07c0, 0x0200},
	{0x07c1, 0x0000},
	{0x07c2, 0x0000},
	{0x07c3, 0x0000},
	{0x07c4, 0x0000},
	{0x07c5, 0x0000},
	{0x07c6, 0x0000},
	{0x07c7, 0x0000},
	{0x07c8, 0x0000},
	{0x07c9, 0x0000},
	{0x1000, 0x4040},
	{0x1002, 0x6505},
	{0x1004, 0x5405},
	{0x1006, 0x5555},
	{0x1007, 0x003f},
	{0x1008, 0x7fd7},
	{0x1009, 0x770f},
	{0x100a, 0xfffe},
	{0x100b, 0xe000},
	{0x100c, 0x0000},
	{0x100d, 0x0007},
	{0x1010, 0xa433},
	{0x1020, 0x0000},
	{0x1022, 0x0000},
	{0x1024, 0x0000},
	{0x1200, 0x5a01},
	{0x1202, 0x6324},
	{0x1204, 0x0b00},
	{0x1206, 0x0000},
	{0x1208, 0x0000},
	{0x120a, 0x0024},
	{0x120c, 0x0000},
	{0x120e, 0x000e},
	{0x1210, 0x0000},
	{0x1212, 0x0000},
	{0x1300, 0x0701},
	{0x1302, 0x12f9},
	{0x1304, 0x3405},
	{0x1305, 0x0844},
	{0x1306, 0x5611},
	{0x1308, 0x555e},
	{0x130a, 0xa605},
	{0x130c, 0x2000},
	{0x130e, 0x0000},
	{0x130f, 0x0001},
	{0x1310, 0xaa48},
	{0x1312, 0x0285},
	{0x1314, 0xaaaa},
	{0x1316, 0xaaa0},
	{0x1318, 0x2aaa},
	{0x131a, 0xaa07},
	{0x1322, 0x0029},
	{0x1323, 0x4a52},
	{0x1324, 0x002c},
	{0x1325, 0x0b02},
	{0x1326, 0x002d},
	{0x1327, 0x6b5a},
	{0x1328, 0x002e},
	{0x1329, 0xcbb2},
	{0x132a, 0x0030},
	{0x132b, 0x2c0b},
	{0x1330, 0x0031},
	{0x1331, 0x8c63},
	{0x1332, 0x0032},
	{0x1333, 0xecbb},
	{0x1334, 0x0034},
	{0x1335, 0x4d13},
	{0x1336, 0x0037},
	{0x1337, 0x0dc3},
	{0x1338, 0x003d},
	{0x1339, 0xef7b},
	{0x133a, 0x0044},
	{0x133b, 0xd134},
	{0x133c, 0x0047},
	{0x133d, 0x91e4},
	{0x133e, 0x004d},
	{0x133f, 0xc370},
	{0x1340, 0x0053},
	{0x1341, 0xf4fd},
	{0x1342, 0x0060},
	{0x1343, 0x5816},
	{0x1344, 0x006c},
	{0x1345, 0xbb2e},
	{0x1346, 0x0072},
	{0x1347, 0xecbb},
	{0x1348, 0x0076},
	{0x1349, 0x5d97},
	{0x1500, 0x0702},
	{0x1502, 0x002f},
	{0x1504, 0x0000},
	{0x1510, 0x0064},
	{0x1512, 0x0000},
	{0x1514, 0xdf47},
	{0x1516, 0x079c},
	{0x1518, 0xfbf5},
	{0x151a, 0x00bc},
	{0x151c, 0x3b85},
	{0x151e, 0x02b3},
	{0x1520, 0x3333},
	{0x1522, 0x0000},
	{0x1524, 0x4000},
	{0x1528, 0x0064},
	{0x152a, 0x0000},
	{0x152c, 0x0000},
	{0x152e, 0x0000},
	{0x1530, 0x0000},
	{0x1532, 0x0000},
	{0x1534, 0x0000},
	{0x1536, 0x0000},
	{0x1538, 0x0040},
	{0x1539, 0x0000},
	{0x153a, 0x0040},
	{0x153b, 0x0000},
	{0x153c, 0x0064},
	{0x153e, 0x0bf9},
	{0x1540, 0xb2a9},
	{0x1544, 0x0200},
	{0x1546, 0x0000},
	{0x1548, 0x00ca},
	{0x1552, 0x03ff},
	{0x1554, 0x017f},
	{0x1556, 0x017f},
	{0x155a, 0x0000},
	{0x155c, 0x0000},
	{0x1560, 0x0040},
	{0x1562, 0x0000},
	{0x1570, 0x03ff},
	{0x1571, 0xdcff},
	{0x1572, 0x1e00},
	{0x1573, 0x224f},
	{0x1574, 0x0000},
	{0x1575, 0x0000},
	{0x1576, 0x1e00},
	{0x1577, 0x0000},
	{0x1578, 0x0000},
	{0x1579, 0x1128},
	{0x157a, 0x03ff},
	{0x157b, 0xdcff},
	{0x157c, 0x1e00},
	{0x157d, 0x224f},
	{0x157e, 0x0000},
	{0x157f, 0x0000},
	{0x1580, 0x1e00},
	{0x1581, 0x0000},
	{0x1582, 0x0000},
	{0x1583, 0x1128},
	{0x1590, 0x03ff},
	{0x1591, 0xdcff},
	{0x1592, 0x1e00},
	{0x1593, 0x224f},
	{0x1594, 0x0000},
	{0x1595, 0x0000},
	{0x1596, 0x1e00},
	{0x1597, 0x0000},
	{0x1598, 0x0000},
	{0x1599, 0x1128},
	{0x159a, 0x03ff},
	{0x159b, 0xdcff},
	{0x159c, 0x1e00},
	{0x159d, 0x224f},
	{0x159e, 0x0000},
	{0x159f, 0x0000},
	{0x15a0, 0x1e00},
	{0x15a1, 0x0000},
	{0x15a2, 0x0000},
	{0x15a3, 0x1128},
	{0x15b0, 0x007f},
	{0x15b1, 0xffff},
	{0x15b2, 0x007f},
	{0x15b3, 0xffff},
	{0x15b4, 0x007f},
	{0x15b5, 0xffff},
	{0x15b8, 0x007f},
	{0x15b9, 0xffff},
	{0x15bc, 0x0000},
	{0x15bd, 0x0000},
	{0x15be, 0xff00},
	{0x15bf, 0x0000},
	{0x15c0, 0xff00},
	{0x15c1, 0x0000},
	{0x15c3, 0xfc00},
	{0x15c4, 0xbb80},
	{0x15d0, 0x0000},
	{0x15d1, 0x0000},
	{0x15d2, 0x0000},
	{0x15d3, 0x0000},
	{0x15d4, 0x0000},
	{0x15d5, 0x0000},
	{0x15d6, 0x0000},
	{0x15d7, 0x0000},
	{0x15d8, 0x0200},
	{0x15d9, 0x0000},
	{0x15da, 0x0000},
	{0x15db, 0x0000},
	{0x15dc, 0x0000},
	{0x15dd, 0x0000},
	{0x15de, 0x0000},
	{0x15df, 0x0000},
	{0x15e0, 0x0000},
	{0x15e1, 0x0000},
	{0x15e2, 0x0200},
	{0x15e3, 0x0000},
	{0x15e4, 0x0000},
	{0x15e5, 0x0000},
	{0x15e6, 0x0000},
	{0x15e7, 0x0000},
	{0x15e8, 0x0000},
	{0x15e9, 0x0000},
	{0x15ea, 0x0000},
	{0x15eb, 0x0000},
	{0x15ec, 0x0200},
	{0x15ed, 0x0000},
	{0x15ee, 0x0000},
	{0x15ef, 0x0000},
	{0x15f0, 0x0000},
	{0x15f1, 0x0000},
	{0x15f2, 0x0000},
	{0x15f3, 0x0000},
	{0x15f4, 0x0000},
	{0x15f5, 0x0000},
	{0x15f6, 0x0200},
	{0x15f7, 0x0200},
	{0x15f8, 0x8200},
	{0x15f9, 0x0000},
	{0x1600, 0x007d},
	{0x1601, 0xa178},
	{0x1602, 0x00c2},
	{0x1603, 0x5383},
	{0x1604, 0x0000},
	{0x1605, 0x02c1},
	{0x1606, 0x007d},
	{0x1607, 0xa178},
	{0x1608, 0x00c2},
	{0x1609, 0x5383},
	{0x160a, 0x003e},
	{0x160b, 0xd37d},
	{0x1611, 0x3210},
	{0x1612, 0x7418},
	{0x1613, 0xc0ff},
	{0x1614, 0x0000},
	{0x1615, 0x00ff},
	{0x1616, 0x0000},
	{0x1617, 0x0000},
	{0x1621, 0x6210},
	{0x1622, 0x7418},
	{0x1623, 0xc0ff},
	{0x1624, 0x0000},
	{0x1625, 0x00ff},
	{0x1626, 0x0000},
	{0x1627, 0x0000},
	{0x1631, 0x3a14},
	{0x1632, 0x7418},
	{0x1633, 0xc3ff},
	{0x1634, 0x0000},
	{0x1635, 0x00ff},
	{0x1636, 0x0000},
	{0x1637, 0x0000},
	{0x1638, 0x0000},
	{0x163a, 0x0000},
	{0x163c, 0x0000},
	{0x163e, 0x0000},
	{0x1640, 0x0000},
	{0x1642, 0x0000},
	{0x1644, 0x0000},
	{0x1646, 0x0000},
	{0x1648, 0x0000},
	{0x1650, 0x0000},
	{0x1652, 0x0000},
	{0x1654, 0x0000},
	{0x1656, 0x0000},
	{0x1658, 0x0000},
	{0x1660, 0x0000},
	{0x1662, 0x0000},
	{0x1664, 0x0000},
	{0x1666, 0x0000},
	{0x1668, 0x0000},
	{0x1670, 0x0000},
	{0x1672, 0x0000},
	{0x1674, 0x0000},
	{0x1676, 0x0000},
	{0x1678, 0x0000},
	{0x1680, 0x0000},
	{0x1682, 0x0000},
	{0x1684, 0x0000},
	{0x1686, 0x0000},
	{0x1688, 0x0000},
	{0x1690, 0x0000},
	{0x1692, 0x0000},
	{0x1694, 0x0000},
	{0x1696, 0x0000},
	{0x1698, 0x0000},
	{0x1700, 0x0000},
	{0x1702, 0x0000},
	{0x1704, 0x0000},
	{0x1706, 0x0000},
	{0x1708, 0x0000},
	{0x1710, 0x0000},
	{0x1712, 0x0000},
	{0x1714, 0x0000},
	{0x1716, 0x0000},
	{0x1718, 0x0000},
	{0x1720, 0x0000},
	{0x1722, 0x0000},
	{0x1724, 0x0000},
	{0x1726, 0x0000},
	{0x1728, 0x0000},
	{0x1730, 0x0000},
	{0x1732, 0x0000},
	{0x1734, 0x0000},
	{0x1736, 0x0000},
	{0x1738, 0x0000},
	{0x173a, 0x0000},
	{0x173c, 0x0000},
	{0x173e, 0x0000},
	{0x17bb, 0x0500},
	{0x17bd, 0x0004},
	{0x17bf, 0x0004},
	{0x17c1, 0x0004},
	{0x17c2, 0x7fff},
	{0x17c3, 0x0000},
	{0x17c5, 0x0000},
	{0x17c7, 0x0000},
	{0x17c9, 0x0000},
	{0x17cb, 0x2010},
	{0x17cd, 0x0000},
	{0x17cf, 0x0000},
	{0x17d1, 0x0000},
	{0x17d3, 0x0000},
	{0x17d5, 0x0000},
	{0x17d7, 0x0000},
	{0x17d9, 0x0000},
	{0x17db, 0x0000},
	{0x17dd, 0x0000},
	{0x17df, 0x0000},
	{0x17e1, 0x0000},
	{0x17e3, 0x0000},
	{0x17e5, 0x0000},
	{0x17e7, 0x0000},
	{0x17e9, 0x0000},
	{0x17eb, 0x0000},
	{0x17ed, 0x0000},
	{0x17ef, 0x0000},
	{0x17f1, 0x0000},
	{0x17f3, 0x0000},
	{0x17f5, 0x0000},
	{0x17f7, 0x0000},
	{0x17f9, 0x0000},
	{0x17fb, 0x0000},
	{0x17fd, 0x0000},
	{0x17ff, 0x0000},
	{0x1801, 0x0000},
	{0x1803, 0x0000},
};

static int rt1011_reg_init(struct snd_soc_component *component)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	regmap_multi_reg_write(rt1011->regmap,
		init_list, ARRAY_SIZE(init_list));
	return 0;
}

static bool rt1011_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1011_RESET:
	case RT1011_SRC_2:
	case RT1011_CLK_DET:
	case RT1011_SIL_DET:
	case RT1011_VERSION_ID:
	case RT1011_VENDOR_ID:
	case RT1011_DEVICE_ID:
	case RT1011_DUM_RO:
	case RT1011_DAC_SET_3:
	case RT1011_PWM_CAL:
	case RT1011_SPK_VOL_TEST_OUT:
	case RT1011_VBAT_VOL_DET_1:
	case RT1011_VBAT_TEST_OUT_1:
	case RT1011_VBAT_TEST_OUT_2:
	case RT1011_VBAT_PROTECTION:
	case RT1011_VBAT_DET:
	case RT1011_BOOST_CON_1:
	case RT1011_SHORT_CIRCUIT_DET_1:
	case RT1011_SPK_TEMP_PROTECT_3:
	case RT1011_SPK_TEMP_PROTECT_6:
	case RT1011_SPK_PRO_DC_DET_3:
	case RT1011_SPK_PRO_DC_DET_7:
	case RT1011_SPK_PRO_DC_DET_8:
	case RT1011_SPL_1:
	case RT1011_SPL_4:
	case RT1011_EXCUR_PROTECT_1:
	case RT1011_CROSS_BQ_SET_1:
	case RT1011_CROSS_BQ_SET_2:
	case RT1011_BQ_SET_0:
	case RT1011_BQ_SET_1:
	case RT1011_BQ_SET_2:
	case RT1011_TEST_PAD_STATUS:
	case RT1011_DC_CALIB_CLASSD_1:
	case RT1011_DC_CALIB_CLASSD_5:
	case RT1011_DC_CALIB_CLASSD_6:
	case RT1011_DC_CALIB_CLASSD_7:
	case RT1011_DC_CALIB_CLASSD_8:
	case RT1011_SINE_GEN_REG_2:
	case RT1011_STP_CALIB_RS_TEMP:
	case RT1011_SPK_RESISTANCE_1:
	case RT1011_SPK_RESISTANCE_2:
	case RT1011_SPK_THERMAL:
	case RT1011_ALC_BK_GAIN_O:
	case RT1011_ALC_BK_GAIN_O_PRE:
	case RT1011_SPK_DC_O_23_16:
	case RT1011_SPK_DC_O_15_0:
	case RT1011_INIT_RECIPROCAL_SYN_24_16:
	case RT1011_INIT_RECIPROCAL_SYN_15_0:
	case RT1011_SPK_EXCURSION_23_16:
	case RT1011_SPK_EXCURSION_15_0:
	case RT1011_SEP_MAIN_OUT_23_16:
	case RT1011_SEP_MAIN_OUT_15_0:
	case RT1011_ALC_DRC_HB_INTERNAL_5:
	case RT1011_ALC_DRC_HB_INTERNAL_6:
	case RT1011_ALC_DRC_HB_INTERNAL_7:
	case RT1011_ALC_DRC_BB_INTERNAL_5:
	case RT1011_ALC_DRC_BB_INTERNAL_6:
	case RT1011_ALC_DRC_BB_INTERNAL_7:
	case RT1011_ALC_DRC_POS_INTERNAL_5:
	case RT1011_ALC_DRC_POS_INTERNAL_6:
	case RT1011_ALC_DRC_POS_INTERNAL_7:
	case RT1011_ALC_DRC_POS_INTERNAL_8:
	case RT1011_ALC_DRC_POS_INTERNAL_9:
	case RT1011_ALC_DRC_POS_INTERNAL_10:
	case RT1011_ALC_DRC_POS_INTERNAL_11:
	case RT1011_IRQ_1:
	case RT1011_EFUSE_CONTROL_1:
	case RT1011_EFUSE_CONTROL_2:
	case RT1011_EFUSE_MATCH_DONE ... RT1011_EFUSE_READ_R0_3_15_0:
		return true;

	default:
		return false;
	}
}

static bool rt1011_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT1011_RESET:
	case RT1011_CLK_1:
	case RT1011_CLK_2:
	case RT1011_CLK_3:
	case RT1011_CLK_4:
	case RT1011_PLL_1:
	case RT1011_PLL_2:
	case RT1011_SRC_1:
	case RT1011_SRC_2:
	case RT1011_SRC_3:
	case RT1011_CLK_DET:
	case RT1011_SIL_DET:
	case RT1011_PRIV_INDEX:
	case RT1011_PRIV_DATA:
	case RT1011_CUSTOMER_ID:
	case RT1011_FM_VER:
	case RT1011_VERSION_ID:
	case RT1011_VENDOR_ID:
	case RT1011_DEVICE_ID:
	case RT1011_DUM_RW_0:
	case RT1011_DUM_YUN:
	case RT1011_DUM_RW_1:
	case RT1011_DUM_RO:
	case RT1011_MAN_I2C_DEV:
	case RT1011_DAC_SET_1:
	case RT1011_DAC_SET_2:
	case RT1011_DAC_SET_3:
	case RT1011_ADC_SET:
	case RT1011_ADC_SET_1:
	case RT1011_ADC_SET_2:
	case RT1011_ADC_SET_3:
	case RT1011_ADC_SET_4:
	case RT1011_ADC_SET_5:
	case RT1011_TDM_TOTAL_SET:
	case RT1011_TDM1_SET_TCON:
	case RT1011_TDM1_SET_1:
	case RT1011_TDM1_SET_2:
	case RT1011_TDM1_SET_3:
	case RT1011_TDM1_SET_4:
	case RT1011_TDM1_SET_5:
	case RT1011_TDM2_SET_1:
	case RT1011_TDM2_SET_2:
	case RT1011_TDM2_SET_3:
	case RT1011_TDM2_SET_4:
	case RT1011_TDM2_SET_5:
	case RT1011_PWM_CAL:
	case RT1011_MIXER_1:
	case RT1011_MIXER_2:
	case RT1011_ADRC_LIMIT:
	case RT1011_A_PRO:
	case RT1011_A_TIMING_1:
	case RT1011_A_TIMING_2:
	case RT1011_A_TEMP_SEN:
	case RT1011_SPK_VOL_DET_1:
	case RT1011_SPK_VOL_DET_2:
	case RT1011_SPK_VOL_TEST_OUT:
	case RT1011_VBAT_VOL_DET_1:
	case RT1011_VBAT_VOL_DET_2:
	case RT1011_VBAT_TEST_OUT_1:
	case RT1011_VBAT_TEST_OUT_2:
	case RT1011_VBAT_PROTECTION:
	case RT1011_VBAT_DET:
	case RT1011_POWER_1:
	case RT1011_POWER_2:
	case RT1011_POWER_3:
	case RT1011_POWER_4:
	case RT1011_POWER_5:
	case RT1011_POWER_6:
	case RT1011_POWER_7:
	case RT1011_POWER_8:
	case RT1011_POWER_9:
	case RT1011_CLASS_D_POS:
	case RT1011_BOOST_CON_1:
	case RT1011_BOOST_CON_2:
	case RT1011_ANALOG_CTRL:
	case RT1011_POWER_SEQ:
	case RT1011_SHORT_CIRCUIT_DET_1:
	case RT1011_SHORT_CIRCUIT_DET_2:
	case RT1011_SPK_TEMP_PROTECT_0:
	case RT1011_SPK_TEMP_PROTECT_1:
	case RT1011_SPK_TEMP_PROTECT_2:
	case RT1011_SPK_TEMP_PROTECT_3:
	case RT1011_SPK_TEMP_PROTECT_4:
	case RT1011_SPK_TEMP_PROTECT_5:
	case RT1011_SPK_TEMP_PROTECT_6:
	case RT1011_SPK_TEMP_PROTECT_7:
	case RT1011_SPK_TEMP_PROTECT_8:
	case RT1011_SPK_TEMP_PROTECT_9:
	case RT1011_SPK_PRO_DC_DET_1:
	case RT1011_SPK_PRO_DC_DET_2:
	case RT1011_SPK_PRO_DC_DET_3:
	case RT1011_SPK_PRO_DC_DET_4:
	case RT1011_SPK_PRO_DC_DET_5:
	case RT1011_SPK_PRO_DC_DET_6:
	case RT1011_SPK_PRO_DC_DET_7:
	case RT1011_SPK_PRO_DC_DET_8:
	case RT1011_SPL_1:
	case RT1011_SPL_2:
	case RT1011_SPL_3:
	case RT1011_SPL_4:
	case RT1011_THER_FOLD_BACK_1:
	case RT1011_THER_FOLD_BACK_2:
	case RT1011_EXCUR_PROTECT_1:
	case RT1011_EXCUR_PROTECT_2:
	case RT1011_EXCUR_PROTECT_3:
	case RT1011_EXCUR_PROTECT_4:
	case RT1011_BAT_GAIN_1:
	case RT1011_BAT_GAIN_2:
	case RT1011_BAT_GAIN_3:
	case RT1011_BAT_GAIN_4:
	case RT1011_BAT_GAIN_5:
	case RT1011_BAT_GAIN_6:
	case RT1011_BAT_GAIN_7:
	case RT1011_BAT_GAIN_8:
	case RT1011_BAT_GAIN_9:
	case RT1011_BAT_GAIN_10:
	case RT1011_BAT_GAIN_11:
	case RT1011_BAT_RT_THMAX_1:
	case RT1011_BAT_RT_THMAX_2:
	case RT1011_BAT_RT_THMAX_3:
	case RT1011_BAT_RT_THMAX_4:
	case RT1011_BAT_RT_THMAX_5:
	case RT1011_BAT_RT_THMAX_6:
	case RT1011_BAT_RT_THMAX_7:
	case RT1011_BAT_RT_THMAX_8:
	case RT1011_BAT_RT_THMAX_9:
	case RT1011_BAT_RT_THMAX_10:
	case RT1011_BAT_RT_THMAX_11:
	case RT1011_BAT_RT_THMAX_12:
	case RT1011_SPREAD_SPECTURM:
	case RT1011_PRO_GAIN_MODE:
	case RT1011_RT_DRC_CROSS:
	case RT1011_RT_DRC_HB_1:
	case RT1011_RT_DRC_HB_2:
	case RT1011_RT_DRC_HB_3:
	case RT1011_RT_DRC_HB_4:
	case RT1011_RT_DRC_HB_5:
	case RT1011_RT_DRC_HB_6:
	case RT1011_RT_DRC_HB_7:
	case RT1011_RT_DRC_HB_8:
	case RT1011_RT_DRC_BB_1:
	case RT1011_RT_DRC_BB_2:
	case RT1011_RT_DRC_BB_3:
	case RT1011_RT_DRC_BB_4:
	case RT1011_RT_DRC_BB_5:
	case RT1011_RT_DRC_BB_6:
	case RT1011_RT_DRC_BB_7:
	case RT1011_RT_DRC_BB_8:
	case RT1011_RT_DRC_POS_1:
	case RT1011_RT_DRC_POS_2:
	case RT1011_RT_DRC_POS_3:
	case RT1011_RT_DRC_POS_4:
	case RT1011_RT_DRC_POS_5:
	case RT1011_RT_DRC_POS_6:
	case RT1011_RT_DRC_POS_7:
	case RT1011_RT_DRC_POS_8:
	case RT1011_CROSS_BQ_SET_1:
	case RT1011_CROSS_BQ_SET_2:
	case RT1011_BQ_SET_0:
	case RT1011_BQ_SET_1:
	case RT1011_BQ_SET_2:
	case RT1011_BQ_PRE_GAIN_28_16:
	case RT1011_BQ_PRE_GAIN_15_0:
	case RT1011_BQ_POST_GAIN_28_16:
	case RT1011_BQ_POST_GAIN_15_0:
	case RT1011_BQ_H0_28_16 ... RT1011_BQ_A2_15_0:
	case RT1011_BQ_1_H0_28_16 ... RT1011_BQ_1_A2_15_0:
	case RT1011_BQ_2_H0_28_16 ... RT1011_BQ_2_A2_15_0:
	case RT1011_BQ_3_H0_28_16 ... RT1011_BQ_3_A2_15_0:
	case RT1011_BQ_4_H0_28_16 ... RT1011_BQ_4_A2_15_0:
	case RT1011_BQ_5_H0_28_16 ... RT1011_BQ_5_A2_15_0:
	case RT1011_BQ_6_H0_28_16 ... RT1011_BQ_6_A2_15_0:
	case RT1011_BQ_7_H0_28_16 ... RT1011_BQ_7_A2_15_0:
	case RT1011_BQ_8_H0_28_16 ... RT1011_BQ_8_A2_15_0:
	case RT1011_BQ_9_H0_28_16 ... RT1011_BQ_9_A2_15_0:
	case RT1011_BQ_10_H0_28_16 ... RT1011_BQ_10_A2_15_0:
	case RT1011_TEST_PAD_STATUS ... RT1011_PLL_INTERNAL_SET:
	case RT1011_TEST_OUT_1 ... RT1011_TEST_OUT_3:
	case RT1011_DC_CALIB_CLASSD_1 ... RT1011_DC_CALIB_CLASSD_10:
	case RT1011_CLASSD_INTERNAL_SET_1 ... RT1011_VREF_LV_1:
	case RT1011_SMART_BOOST_TIMING_1 ... RT1011_SMART_BOOST_TIMING_36:
	case RT1011_SINE_GEN_REG_1 ... RT1011_SINE_GEN_REG_3:
	case RT1011_STP_INITIAL_RS_TEMP ... RT1011_SPK_THERMAL:
	case RT1011_STP_OTP_TH ... RT1011_INIT_RECIPROCAL_SYN_15_0:
	case RT1011_STP_BQ_1_A1_L_28_16 ... RT1011_STP_BQ_1_H0_R_15_0:
	case RT1011_STP_BQ_2_A1_L_28_16 ... RT1011_SEP_RE_REG_15_0:
	case RT1011_DRC_CF_PARAMS_1 ... RT1011_DRC_CF_PARAMS_12:
	case RT1011_ALC_DRC_HB_INTERNAL_1 ... RT1011_ALC_DRC_HB_INTERNAL_7:
	case RT1011_ALC_DRC_BB_INTERNAL_1 ... RT1011_ALC_DRC_BB_INTERNAL_7:
	case RT1011_ALC_DRC_POS_INTERNAL_1 ... RT1011_ALC_DRC_POS_INTERNAL_8:
	case RT1011_ALC_DRC_POS_INTERNAL_9 ... RT1011_BQ_1_PARAMS_CHECK_5:
	case RT1011_BQ_2_PARAMS_CHECK_1 ... RT1011_BQ_2_PARAMS_CHECK_5:
	case RT1011_BQ_3_PARAMS_CHECK_1 ... RT1011_BQ_3_PARAMS_CHECK_5:
	case RT1011_BQ_4_PARAMS_CHECK_1 ... RT1011_BQ_4_PARAMS_CHECK_5:
	case RT1011_BQ_5_PARAMS_CHECK_1 ... RT1011_BQ_5_PARAMS_CHECK_5:
	case RT1011_BQ_6_PARAMS_CHECK_1 ... RT1011_BQ_6_PARAMS_CHECK_5:
	case RT1011_BQ_7_PARAMS_CHECK_1 ... RT1011_BQ_7_PARAMS_CHECK_5:
	case RT1011_BQ_8_PARAMS_CHECK_1 ... RT1011_BQ_8_PARAMS_CHECK_5:
	case RT1011_BQ_9_PARAMS_CHECK_1 ... RT1011_BQ_9_PARAMS_CHECK_5:
	case RT1011_BQ_10_PARAMS_CHECK_1 ... RT1011_BQ_10_PARAMS_CHECK_5:
	case RT1011_IRQ_1 ... RT1011_PART_NUMBER_EFUSE:
	case RT1011_EFUSE_CONTROL_1 ... RT1011_EFUSE_READ_R0_3_15_0:
		return true;
	default:
		return false;
	}
}

static const char * const rt1011_din_source_select[] = {
	"Left",
	"Right",
	"Left + Right average",
};

static SOC_ENUM_SINGLE_DECL(rt1011_din_source_enum, RT1011_CROSS_BQ_SET_1, 5,
	rt1011_din_source_select);

static const char * const rt1011_tdm_data_out_select[] = {
	"TDM_O_LR", "BQ1", "DVOL", "BQ10", "ALC", "DMIX", "ADC_SRC_LR",
	"ADC_O_LR", "ADC_MONO", "RSPK_BPF_LR", "DMIX_ADD", "ENVELOPE_FS",
	"SEP_O_GAIN", "ALC_BK_GAIN", "STP_V_C", "DMIX_ABST"
};

static const char * const rt1011_tdm_l_ch_data_select[] = {
	"Slot0", "Slot1", "Slot2", "Slot3", "Slot4", "Slot5", "Slot6", "Slot7"
};
static SOC_ENUM_SINGLE_DECL(rt1011_tdm1_l_dac1_enum, RT1011_TDM1_SET_4, 12,
	rt1011_tdm_l_ch_data_select);
static SOC_ENUM_SINGLE_DECL(rt1011_tdm2_l_dac1_enum, RT1011_TDM2_SET_4, 12,
	rt1011_tdm_l_ch_data_select);

static SOC_ENUM_SINGLE_DECL(rt1011_tdm1_adc1_dat_enum,
	RT1011_ADCDAT_OUT_SOURCE, 0, rt1011_tdm_data_out_select);
static SOC_ENUM_SINGLE_DECL(rt1011_tdm1_adc1_loc_enum, RT1011_TDM1_SET_2, 0,
	rt1011_tdm_l_ch_data_select);

static const char * const rt1011_adc_data_mode_select[] = {
	"Stereo", "Mono"
};
static SOC_ENUM_SINGLE_DECL(rt1011_adc_dout_mode_enum, RT1011_TDM1_SET_1, 12,
	rt1011_adc_data_mode_select);

static const char * const rt1011_tdm_adc_data_len_control[] = {
	"1CH", "2CH", "3CH", "4CH", "5CH", "6CH", "7CH", "8CH"
};
static SOC_ENUM_SINGLE_DECL(rt1011_tdm1_dout_len_enum, RT1011_TDM1_SET_2, 13,
	rt1011_tdm_adc_data_len_control);
static SOC_ENUM_SINGLE_DECL(rt1011_tdm2_dout_len_enum, RT1011_TDM2_SET_2, 13,
	rt1011_tdm_adc_data_len_control);

static const char * const rt1011_tdm_adc_swap_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt1011_tdm_adc1_1_enum, RT1011_TDM1_SET_3, 6,
	rt1011_tdm_adc_swap_select);
static SOC_ENUM_SINGLE_DECL(rt1011_tdm_adc2_1_enum, RT1011_TDM1_SET_3, 4,
	rt1011_tdm_adc_swap_select);

static void rt1011_reset(struct regmap *regmap)
{
	regmap_write(regmap, RT1011_RESET, 0);
}

static int rt1011_recv_spk_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1011_priv *rt1011 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt1011->recv_spk_mode;

	return 0;
}

static int rt1011_recv_spk_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1011_priv *rt1011 =
		snd_soc_component_get_drvdata(component);

	if (ucontrol->value.integer.value[0] == rt1011->recv_spk_mode)
		return 0;

	if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
		rt1011->recv_spk_mode = ucontrol->value.integer.value[0];

		if (rt1011->recv_spk_mode) {

			/* 1: recevier mode on */
			snd_soc_component_update_bits(component,
				RT1011_CLASSD_INTERNAL_SET_3,
				RT1011_REG_GAIN_CLASSD_RI_SPK_MASK,
				RT1011_REG_GAIN_CLASSD_RI_410K);
			snd_soc_component_update_bits(component,
				RT1011_CLASSD_INTERNAL_SET_1,
				RT1011_RECV_MODE_SPK_MASK,
				RT1011_RECV_MODE);
		} else {
			/* 0: speaker mode on */
			snd_soc_component_update_bits(component,
				RT1011_CLASSD_INTERNAL_SET_3,
				RT1011_REG_GAIN_CLASSD_RI_SPK_MASK,
				RT1011_REG_GAIN_CLASSD_RI_72P5K);
			snd_soc_component_update_bits(component,
				RT1011_CLASSD_INTERNAL_SET_1,
				RT1011_RECV_MODE_SPK_MASK,
				RT1011_SPK_MODE);
		}
	}

	return 0;
}

static bool rt1011_validate_bq_drc_coeff(unsigned short reg)
{
	if ((reg == RT1011_DAC_SET_1) ||
		(reg >= RT1011_ADC_SET && reg <= RT1011_ADC_SET_1) ||
		(reg == RT1011_ADC_SET_4) || (reg == RT1011_ADC_SET_5) ||
		(reg == RT1011_MIXER_1) ||
		(reg == RT1011_A_TIMING_1) ||
		(reg >= RT1011_POWER_7 && reg <= RT1011_POWER_8) ||
		(reg == RT1011_CLASS_D_POS) || (reg == RT1011_ANALOG_CTRL) ||
		(reg >= RT1011_SPK_TEMP_PROTECT_0 && reg <= RT1011_SPK_TEMP_PROTECT_6) ||
		(reg >= RT1011_SPK_PRO_DC_DET_5 && reg <= RT1011_BAT_GAIN_1) ||
		(reg >= RT1011_RT_DRC_CROSS && reg <= RT1011_RT_DRC_POS_8) ||
		(reg >= RT1011_CROSS_BQ_SET_1 && reg <= RT1011_BQ_10_A2_15_0) ||
		(reg >= RT1011_SMART_BOOST_TIMING_1 && reg <= RT1011_SMART_BOOST_TIMING_36) ||
		(reg == RT1011_SINE_GEN_REG_1) ||
		(reg >= RT1011_STP_ALPHA_RECIPROCAL_MSB && reg <= RT1011_BQ_6_PARAMS_CHECK_5) ||
		(reg >= RT1011_BQ_7_PARAMS_CHECK_1 && reg <= RT1011_BQ_10_PARAMS_CHECK_5))
		return true;

	return false;
}

static int rt1011_bq_drc_coeff_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1011_priv *rt1011 =
		snd_soc_component_get_drvdata(component);
	struct rt1011_bq_drc_params *bq_drc_info;
	struct rt1011_bq_drc_params *params =
		(struct rt1011_bq_drc_params *)ucontrol->value.integer.value;
	unsigned int i, mode_idx = 0;

	if (strstr(ucontrol->id.name, "AdvanceMode Initial Set"))
		mode_idx = RT1011_ADVMODE_INITIAL_SET;
	else if (strstr(ucontrol->id.name, "AdvanceMode SEP BQ Coeff"))
		mode_idx = RT1011_ADVMODE_SEP_BQ_COEFF;
	else if (strstr(ucontrol->id.name, "AdvanceMode EQ BQ Coeff"))
		mode_idx = RT1011_ADVMODE_EQ_BQ_COEFF;
	else if (strstr(ucontrol->id.name, "AdvanceMode BQ UI Coeff"))
		mode_idx = RT1011_ADVMODE_BQ_UI_COEFF;
	else if (strstr(ucontrol->id.name, "AdvanceMode SmartBoost Coeff"))
		mode_idx = RT1011_ADVMODE_SMARTBOOST_COEFF;
	else
		return -EINVAL;

	pr_info("%s, id.name=%s, mode_idx=%d\n", __func__,
		ucontrol->id.name, mode_idx);
	bq_drc_info = rt1011->bq_drc_params[mode_idx];

	for (i = 0; i < RT1011_BQ_DRC_NUM; i++) {
		params[i].reg = bq_drc_info[i].reg;
		params[i].val = bq_drc_info[i].val;
	}

	return 0;
}

static int rt1011_bq_drc_coeff_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1011_priv *rt1011 =
		snd_soc_component_get_drvdata(component);
	struct rt1011_bq_drc_params *bq_drc_info;
	struct rt1011_bq_drc_params *params =
		(struct rt1011_bq_drc_params *)ucontrol->value.integer.value;
	unsigned int i, mode_idx = 0;

	if (strstr(ucontrol->id.name, "AdvanceMode Initial Set"))
		mode_idx = RT1011_ADVMODE_INITIAL_SET;
	else if (strstr(ucontrol->id.name, "AdvanceMode SEP BQ Coeff"))
		mode_idx = RT1011_ADVMODE_SEP_BQ_COEFF;
	else if (strstr(ucontrol->id.name, "AdvanceMode EQ BQ Coeff"))
		mode_idx = RT1011_ADVMODE_EQ_BQ_COEFF;
	else if (strstr(ucontrol->id.name, "AdvanceMode BQ UI Coeff"))
		mode_idx = RT1011_ADVMODE_BQ_UI_COEFF;
	else if (strstr(ucontrol->id.name, "AdvanceMode SmartBoost Coeff"))
		mode_idx = RT1011_ADVMODE_SMARTBOOST_COEFF;
	else
		return -EINVAL;

	bq_drc_info = rt1011->bq_drc_params[mode_idx];
	memset(bq_drc_info, 0,
		sizeof(struct rt1011_bq_drc_params) * RT1011_BQ_DRC_NUM);

	pr_info("%s, id.name=%s, mode_idx=%d\n", __func__,
		ucontrol->id.name, mode_idx);
	for (i = 0; i < RT1011_BQ_DRC_NUM; i++) {
		bq_drc_info[i].reg = params[i].reg;
		bq_drc_info[i].val = params[i].val;
	}

	for (i = 0; i < RT1011_BQ_DRC_NUM; i++) {
		if (bq_drc_info[i].reg == 0)
			break;
		else if (rt1011_validate_bq_drc_coeff(bq_drc_info[i].reg)) {
			snd_soc_component_write(component, bq_drc_info[i].reg,
					bq_drc_info[i].val);
		}
	}

	return 0;
}

static int rt1011_bq_drc_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 128;
	uinfo->value.integer.max = 0x17ffffff;

	return 0;
}

#define RT1011_BQ_DRC(xname) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = rt1011_bq_drc_info, \
	.get = rt1011_bq_drc_coeff_get, \
	.put = rt1011_bq_drc_coeff_put \
}

static int rt1011_r0_cali_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt1011->cali_done;

	return 0;
}

static int rt1011_r0_cali_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	rt1011->cali_done = 0;
	if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF &&
		ucontrol->value.integer.value[0])
		rt1011_calibrate(rt1011, 1);

	return 0;
}

static int rt1011_r0_load(struct rt1011_priv *rt1011)
{
	if (!rt1011->r0_reg)
		return -EINVAL;

	/* write R0 to register */
	regmap_write(rt1011->regmap, RT1011_INIT_RECIPROCAL_REG_24_16,
		((rt1011->r0_reg>>16) & 0x1ff));
	regmap_write(rt1011->regmap, RT1011_INIT_RECIPROCAL_REG_15_0,
		(rt1011->r0_reg & 0xffff));
	regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_4, 0x4080);

	return 0;
}

static int rt1011_r0_load_mode_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = rt1011->r0_reg;

	return 0;
}

static int rt1011_r0_load_mode_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);
	struct device *dev;
	unsigned int r0_integer, r0_factor, format;

	if (ucontrol->value.integer.value[0] == rt1011->r0_reg)
		return 0;

	if (ucontrol->value.integer.value[0] == 0)
		return -EINVAL;

	dev = regmap_get_device(rt1011->regmap);
	if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
		rt1011->r0_reg = ucontrol->value.integer.value[0];

		format = 2147483648U; /* 2^24 * 128 */
		r0_integer = format / rt1011->r0_reg / 128;
		r0_factor = ((format / rt1011->r0_reg * 100) / 128)
						- (r0_integer * 100);
		dev_info(dev, "New r0 resistance about %d.%02d ohm, reg=0x%X\n",
			r0_integer, r0_factor, rt1011->r0_reg);

		if (rt1011->r0_reg)
			rt1011_r0_load(rt1011);
	}

	return 0;
}

static int rt1011_r0_load_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.max = 0x1ffffff;

	return 0;
}

#define RT1011_R0_LOAD(xname) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = rt1011_r0_load_info, \
	.get = rt1011_r0_load_mode_get, \
	.put = rt1011_r0_load_mode_put \
}

static const char * const rt1011_i2s_ref[] = {
	"None", "Left Channel", "Right Channel"
};

static SOC_ENUM_SINGLE_DECL(rt1011_i2s_ref_enum, 0, 0,
	rt1011_i2s_ref);

static int rt1011_i2s_ref_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1011_priv *rt1011 =
		snd_soc_component_get_drvdata(component);

	rt1011->i2s_ref = ucontrol->value.enumerated.item[0];
	switch (rt1011->i2s_ref) {
	case RT1011_I2S_REF_LEFT_CH:
		regmap_write(rt1011->regmap, RT1011_TDM_TOTAL_SET, 0x0240);
		regmap_write(rt1011->regmap, RT1011_TDM1_SET_2, 0x8);
		regmap_write(rt1011->regmap, RT1011_TDM1_SET_1, 0x1022);
		regmap_write(rt1011->regmap, RT1011_ADCDAT_OUT_SOURCE, 0x4);
		break;
	case RT1011_I2S_REF_RIGHT_CH:
		regmap_write(rt1011->regmap, RT1011_TDM_TOTAL_SET, 0x0240);
		regmap_write(rt1011->regmap, RT1011_TDM1_SET_2, 0x8);
		regmap_write(rt1011->regmap, RT1011_TDM1_SET_1, 0x10a2);
		regmap_write(rt1011->regmap, RT1011_ADCDAT_OUT_SOURCE, 0x4);
		break;
	default:
		dev_info(component->dev, "I2S Reference: Do nothing\n");
	}

	return 0;
}

static int rt1011_i2s_ref_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct rt1011_priv *rt1011 =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.enumerated.item[0] = rt1011->i2s_ref;

	return 0;
}

static const struct snd_kcontrol_new rt1011_snd_controls[] = {
	/* I2S Data In Selection */
	SOC_ENUM("DIN Source", rt1011_din_source_enum),

	/* TDM Data In Selection */
	SOC_ENUM("TDM1 DIN Source", rt1011_tdm1_l_dac1_enum),
	SOC_ENUM("TDM2 DIN Source", rt1011_tdm2_l_dac1_enum),

	/* TDM1 Data Out Selection */
	SOC_ENUM("TDM1 DOUT Source", rt1011_tdm1_adc1_dat_enum),
	SOC_ENUM("TDM1 DOUT Location", rt1011_tdm1_adc1_loc_enum),
	SOC_ENUM("TDM1 ADC1DAT Swap Select", rt1011_tdm_adc1_1_enum),
	SOC_ENUM("TDM1 ADC2DAT Swap Select", rt1011_tdm_adc2_1_enum),

	/* Data Out Mode */
	SOC_ENUM("I2S ADC DOUT Mode", rt1011_adc_dout_mode_enum),
	SOC_ENUM("TDM1 DOUT Length", rt1011_tdm1_dout_len_enum),
	SOC_ENUM("TDM2 DOUT Length", rt1011_tdm2_dout_len_enum),

	/* Speaker/Receiver Mode */
	SOC_SINGLE_EXT("RECV SPK Mode", SND_SOC_NOPM, 0, 1, 0,
		rt1011_recv_spk_mode_get, rt1011_recv_spk_mode_put),

	/* BiQuad/DRC/SmartBoost Settings */
	RT1011_BQ_DRC("AdvanceMode Initial Set"),
	RT1011_BQ_DRC("AdvanceMode SEP BQ Coeff"),
	RT1011_BQ_DRC("AdvanceMode EQ BQ Coeff"),
	RT1011_BQ_DRC("AdvanceMode BQ UI Coeff"),
	RT1011_BQ_DRC("AdvanceMode SmartBoost Coeff"),

	/* R0 */
	SOC_SINGLE_EXT("R0 Calibration", SND_SOC_NOPM, 0, 1, 0,
		rt1011_r0_cali_get, rt1011_r0_cali_put),
	RT1011_R0_LOAD("R0 Load Mode"),

	/* R0 temperature */
	SOC_SINGLE("R0 Temperature", RT1011_STP_INITIAL_RESISTANCE_TEMP,
		2, 255, 0),
	/* I2S Reference */
	SOC_ENUM_EXT("I2S Reference", rt1011_i2s_ref_enum,
		rt1011_i2s_ref_get, rt1011_i2s_ref_put),
};

static int rt1011_is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(source->dapm);
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	if (rt1011->sysclk_src == RT1011_FS_SYS_PRE_S_PLL1)
		return 1;
	else
		return 0;
}

static int rt1011_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component,
			RT1011_SPK_TEMP_PROTECT_0,
			RT1011_STP_EN_MASK | RT1011_STP_RS_CLB_EN_MASK,
			RT1011_STP_EN | RT1011_STP_RS_CLB_EN);
		snd_soc_component_update_bits(component, RT1011_POWER_9,
			RT1011_POW_MNL_SDB_MASK, RT1011_POW_MNL_SDB);
		msleep(50);
		snd_soc_component_update_bits(component,
			RT1011_CLASSD_INTERNAL_SET_1,
			RT1011_DRIVER_READY_SPK, RT1011_DRIVER_READY_SPK);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT1011_POWER_9,
			RT1011_POW_MNL_SDB_MASK, 0);
		snd_soc_component_update_bits(component,
			RT1011_SPK_TEMP_PROTECT_0,
			RT1011_STP_EN_MASK | RT1011_STP_RS_CLB_EN_MASK, 0);
		msleep(200);
		snd_soc_component_update_bits(component,
			RT1011_CLASSD_INTERNAL_SET_1,
			RT1011_DRIVER_READY_SPK, 0);
		break;

	default:
		return 0;
	}

	return 0;
}


static const struct snd_soc_dapm_widget rt1011_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO2", RT1011_POWER_1,
		RT1011_POW_LDO2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ISENSE SPK", RT1011_POWER_1,
		RT1011_POW_ISENSE_SPK_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VSENSE SPK", RT1011_POWER_1,
		RT1011_POW_VSENSE_SPK_BIT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("PLL", RT1011_POWER_2,
		RT1011_PLLEN_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG", RT1011_POWER_2,
		RT1011_POW_BG_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BG MBIAS", RT1011_POWER_2,
		RT1011_POW_BG_MBIAS_LV_BIT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DET VBAT", RT1011_POWER_3,
		RT1011_POW_DET_VBAT_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MBIAS", RT1011_POWER_3,
		RT1011_POW_MBIAS_LV_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC I", RT1011_POWER_3,
		RT1011_POW_ADC_I_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC V", RT1011_POWER_3,
		RT1011_POW_ADC_V_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC T", RT1011_POWER_3,
		RT1011_POW_ADC_T_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DITHER ADC T", RT1011_POWER_3,
		RT1011_POWD_ADC_T_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIX I", RT1011_POWER_3,
		RT1011_POW_MIX_I_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIX V", RT1011_POWER_3,
		RT1011_POW_MIX_V_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SUM I", RT1011_POWER_3,
		RT1011_POW_SUM_I_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("SUM V", RT1011_POWER_3,
		RT1011_POW_SUM_V_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MIX T", RT1011_POWER_3,
		RT1011_POW_MIX_T_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VREF", RT1011_POWER_3,
		RT1011_POW_VREF_LV_BIT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("BOOST SWR", RT1011_POWER_4,
		RT1011_POW_EN_SWR_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("BGOK SWR", RT1011_POWER_4,
		RT1011_POW_EN_PASS_BGOK_SWR_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("VPOK SWR", RT1011_POWER_4,
		RT1011_POW_EN_PASS_VPOK_SWR_BIT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("TEMP REG", RT1011_A_TEMP_SEN,
		RT1011_POW_TEMP_REG_BIT, 0, NULL, 0),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("DAC Power", RT1011_POWER_1,
		RT1011_POW_DAC_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("CLK12M", RT1011_POWER_1,
		RT1011_POW_CLK12M_BIT, 0, NULL, 0),
	SND_SOC_DAPM_DAC_E("DAC", NULL, RT1011_DAC_SET_3,
		RT1011_DA_MUTE_EN_SFT, 1, rt1011_dac_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("SPO"),
};

static const struct snd_soc_dapm_route rt1011_dapm_routes[] = {

	{ "DAC", NULL, "AIF1RX" },
	{ "DAC", NULL, "DAC Power" },
	{ "DAC", NULL, "LDO2" },
	{ "DAC", NULL, "ISENSE SPK" },
	{ "DAC", NULL, "VSENSE SPK" },
	{ "DAC", NULL, "CLK12M" },

	{ "DAC", NULL, "PLL", rt1011_is_sys_clk_from_pll },
	{ "DAC", NULL, "BG" },
	{ "DAC", NULL, "BG MBIAS" },

	{ "DAC", NULL, "BOOST SWR" },
	{ "DAC", NULL, "BGOK SWR" },
	{ "DAC", NULL, "VPOK SWR" },

	{ "DAC", NULL, "DET VBAT" },
	{ "DAC", NULL, "MBIAS" },
	{ "DAC", NULL, "VREF" },
	{ "DAC", NULL, "ADC I" },
	{ "DAC", NULL, "ADC V" },
	{ "DAC", NULL, "ADC T" },
	{ "DAC", NULL, "DITHER ADC T" },
	{ "DAC", NULL, "MIX I" },
	{ "DAC", NULL, "MIX V" },
	{ "DAC", NULL, "SUM I" },
	{ "DAC", NULL, "SUM V" },
	{ "DAC", NULL, "MIX T" },

	{ "DAC", NULL, "TEMP REG" },

	{ "SPO", NULL, "DAC" },
};

static int rt1011_get_clk_info(int sclk, int rate)
{
	int i;
	static const int pd[] = {1, 2, 3, 4, 6, 8, 12, 16};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}

static int rt1011_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, ch_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt1011->lrck = params_rate(params);
	pre_div = rt1011_get_clk_info(rt1011->sysclk, rt1011->lrck);
	if (pre_div < 0) {
		dev_warn(component->dev, "Force using PLL ");
		snd_soc_dai_set_pll(dai, 0, RT1011_PLL1_S_BCLK,
			rt1011->lrck * 64, rt1011->lrck * 256);
		snd_soc_dai_set_sysclk(dai, RT1011_FS_SYS_PRE_S_PLL1,
			rt1011->lrck * 256, SND_SOC_CLOCK_IN);
		pre_div = 0;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n",
			frame_size);
		return -EINVAL;
	}

	bclk_ms = frame_size > 32;
	rt1011->bclk = rt1011->lrck * (32 << bclk_ms);

	dev_dbg(component->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	dev_dbg(component->dev, "lrck is %dHz and pre_div is %d for iis %d\n",
				rt1011->lrck, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		val_len |= RT1011_I2S_TX_DL_16B;
		val_len |= RT1011_I2S_RX_DL_16B;
		ch_len |= RT1011_I2S_CH_TX_LEN_16B;
		ch_len |= RT1011_I2S_CH_RX_LEN_16B;
		break;
	case 20:
		val_len |= RT1011_I2S_TX_DL_20B;
		val_len |= RT1011_I2S_RX_DL_20B;
		ch_len |= RT1011_I2S_CH_TX_LEN_20B;
		ch_len |= RT1011_I2S_CH_RX_LEN_20B;
		break;
	case 24:
		val_len |= RT1011_I2S_TX_DL_24B;
		val_len |= RT1011_I2S_RX_DL_24B;
		ch_len |= RT1011_I2S_CH_TX_LEN_24B;
		ch_len |= RT1011_I2S_CH_RX_LEN_24B;
		break;
	case 32:
		val_len |= RT1011_I2S_TX_DL_32B;
		val_len |= RT1011_I2S_RX_DL_32B;
		ch_len |= RT1011_I2S_CH_TX_LEN_32B;
		ch_len |= RT1011_I2S_CH_RX_LEN_32B;
		break;
	case 8:
		val_len |= RT1011_I2S_TX_DL_8B;
		val_len |= RT1011_I2S_RX_DL_8B;
		ch_len |= RT1011_I2S_CH_TX_LEN_8B;
		ch_len |= RT1011_I2S_CH_RX_LEN_8B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT1011_AIF1:
		mask_clk = RT1011_FS_SYS_DIV_MASK;
		val_clk = pre_div << RT1011_FS_SYS_DIV_SFT;
		snd_soc_component_update_bits(component, RT1011_TDM_TOTAL_SET,
			RT1011_I2S_TX_DL_MASK | RT1011_I2S_RX_DL_MASK,
			val_len);
		snd_soc_component_update_bits(component, RT1011_TDM1_SET_1,
			RT1011_I2S_CH_TX_LEN_MASK |
			RT1011_I2S_CH_RX_LEN_MASK,
			ch_len);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component,
		RT1011_CLK_2, mask_clk, val_clk);

	return 0;
}

static int rt1011_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	unsigned int reg_val = 0, reg_bclk_inv = 0;
	int ret = 0;

	snd_soc_dapm_mutex_lock(dapm);
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT1011_I2S_TDM_MS_S;
		break;
	default:
		ret = -EINVAL;
		goto _set_fmt_err_;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_bclk_inv |= RT1011_TDM_INV_BCLK;
		break;
	default:
		ret = -EINVAL;
		goto _set_fmt_err_;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT1011_I2S_TDM_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT1011_I2S_TDM_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT1011_I2S_TDM_DF_PCM_B;
		break;
	default:
		ret = -EINVAL;
		goto _set_fmt_err_;
	}

	switch (dai->id) {
	case RT1011_AIF1:
		snd_soc_component_update_bits(component, RT1011_TDM_TOTAL_SET,
			RT1011_I2S_TDM_MS_MASK | RT1011_I2S_TDM_DF_MASK,
			reg_val);
		snd_soc_component_update_bits(component, RT1011_TDM1_SET_1,
			RT1011_TDM_INV_BCLK_MASK, reg_bclk_inv);
		snd_soc_component_update_bits(component, RT1011_TDM2_SET_1,
			RT1011_TDM_INV_BCLK_MASK, reg_bclk_inv);
		break;
	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		ret = -EINVAL;
	}

_set_fmt_err_:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}

static int rt1011_set_component_sysclk(struct snd_soc_component *component,
		int clk_id, int source, unsigned int freq, int dir)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt1011->sysclk && clk_id == rt1011->sysclk_src)
		return 0;

	/* disable MCLK detect in default */
	snd_soc_component_update_bits(component, RT1011_CLK_DET,
			RT1011_EN_MCLK_DET_MASK, 0);

	switch (clk_id) {
	case RT1011_FS_SYS_PRE_S_MCLK:
		reg_val |= RT1011_FS_SYS_PRE_MCLK;
		snd_soc_component_update_bits(component, RT1011_CLK_DET,
			RT1011_EN_MCLK_DET_MASK, RT1011_EN_MCLK_DET);
		break;
	case RT1011_FS_SYS_PRE_S_BCLK:
		reg_val |= RT1011_FS_SYS_PRE_BCLK;
		break;
	case RT1011_FS_SYS_PRE_S_PLL1:
		reg_val |= RT1011_FS_SYS_PRE_PLL1;
		break;
	case RT1011_FS_SYS_PRE_S_RCCLK:
		reg_val |= RT1011_FS_SYS_PRE_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, RT1011_CLK_2,
		RT1011_FS_SYS_PRE_MASK, reg_val);
	rt1011->sysclk = freq;
	rt1011->sysclk_src = clk_id;

	dev_dbg(component->dev, "Sysclk is %dHz and clock id is %d\n",
		freq, clk_id);

	return 0;
}

static int rt1011_set_component_pll(struct snd_soc_component *component,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt1011->pll_src && freq_in == rt1011->pll_in &&
	    freq_out == rt1011->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt1011->pll_in = 0;
		rt1011->pll_out = 0;
		snd_soc_component_update_bits(component, RT1011_CLK_2,
			RT1011_FS_SYS_PRE_MASK, RT1011_FS_SYS_PRE_BCLK);
		return 0;
	}

	switch (source) {
	case RT1011_PLL2_S_MCLK:
		snd_soc_component_update_bits(component, RT1011_CLK_2,
			RT1011_PLL2_SRC_MASK, RT1011_PLL2_SRC_MCLK);
		snd_soc_component_update_bits(component, RT1011_CLK_2,
			RT1011_PLL1_SRC_MASK, RT1011_PLL1_SRC_PLL2);
		snd_soc_component_update_bits(component, RT1011_CLK_DET,
			RT1011_EN_MCLK_DET_MASK, RT1011_EN_MCLK_DET);
		break;
	case RT1011_PLL1_S_BCLK:
		snd_soc_component_update_bits(component, RT1011_CLK_2,
				RT1011_PLL1_SRC_MASK, RT1011_PLL1_SRC_BCLK);
		break;
	case RT1011_PLL2_S_RCCLK:
		snd_soc_component_update_bits(component, RT1011_CLK_2,
			RT1011_PLL2_SRC_MASK, RT1011_PLL2_SRC_RCCLK);
		snd_soc_component_update_bits(component, RT1011_CLK_2,
			RT1011_PLL1_SRC_MASK, RT1011_PLL1_SRC_PLL2);
		break;
	default:
		dev_err(component->dev, "Unknown PLL Source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupported input clock %d\n",
			freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_component_write(component, RT1011_PLL_1,
		((pll_code.m_bp ? 0 : pll_code.m_code) << RT1011_PLL1_QM_SFT) |
		(pll_code.m_bp << RT1011_PLL1_BPM_SFT) |
		pll_code.n_code);
	snd_soc_component_write(component, RT1011_PLL_2,
		pll_code.k_code);

	rt1011->pll_in = freq_in;
	rt1011->pll_out = freq_out;
	rt1011->pll_src = source;

	return 0;
}

static int rt1011_set_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(component);
	unsigned int val = 0, tdm_en = 0, rx_slotnum, tx_slotnum;
	int ret = 0, first_bit, last_bit;

	snd_soc_dapm_mutex_lock(dapm);
	if (rx_mask || tx_mask)
		tdm_en = RT1011_TDM_I2S_DOCK_EN_1;

	switch (slots) {
	case 4:
		val |= RT1011_I2S_TX_4CH;
		val |= RT1011_I2S_RX_4CH;
		break;
	case 6:
		val |= RT1011_I2S_TX_6CH;
		val |= RT1011_I2S_RX_6CH;
		break;
	case 8:
		val |= RT1011_I2S_TX_8CH;
		val |= RT1011_I2S_RX_8CH;
		break;
	case 2:
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	switch (slot_width) {
	case 20:
		val |= RT1011_I2S_CH_TX_LEN_20B;
		val |= RT1011_I2S_CH_RX_LEN_20B;
		break;
	case 24:
		val |= RT1011_I2S_CH_TX_LEN_24B;
		val |= RT1011_I2S_CH_RX_LEN_24B;
		break;
	case 32:
		val |= RT1011_I2S_CH_TX_LEN_32B;
		val |= RT1011_I2S_CH_RX_LEN_32B;
		break;
	case 16:
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	/* Rx slot configuration */
	rx_slotnum = hweight_long(rx_mask);
	if (rx_slotnum > 1 || !rx_slotnum) {
		ret = -EINVAL;
		dev_err(component->dev, "too many rx slots or zero slot\n");
		goto _set_tdm_err_;
	}

	first_bit = __ffs(rx_mask);
	switch (first_bit) {
	case 0:
	case 2:
	case 4:
	case 6:
		snd_soc_component_update_bits(component,
			RT1011_CROSS_BQ_SET_1, RT1011_MONO_LR_SEL_MASK,
			RT1011_MONO_L_CHANNEL);
		snd_soc_component_update_bits(component,
			RT1011_TDM1_SET_4,
			RT1011_TDM_I2S_TX_L_DAC1_1_MASK |
			RT1011_TDM_I2S_TX_R_DAC1_1_MASK,
			(first_bit << RT1011_TDM_I2S_TX_L_DAC1_1_SFT) |
			((first_bit+1) << RT1011_TDM_I2S_TX_R_DAC1_1_SFT));
		break;
	case 1:
	case 3:
	case 5:
	case 7:
		snd_soc_component_update_bits(component,
			RT1011_CROSS_BQ_SET_1, RT1011_MONO_LR_SEL_MASK,
			RT1011_MONO_R_CHANNEL);
		snd_soc_component_update_bits(component,
			RT1011_TDM1_SET_4,
			RT1011_TDM_I2S_TX_L_DAC1_1_MASK |
			RT1011_TDM_I2S_TX_R_DAC1_1_MASK,
			((first_bit-1) << RT1011_TDM_I2S_TX_L_DAC1_1_SFT) |
			(first_bit << RT1011_TDM_I2S_TX_R_DAC1_1_SFT));
		break;
	default:
		ret = -EINVAL;
		goto _set_tdm_err_;
	}

	/* Tx slot configuration */
	tx_slotnum = hweight_long(tx_mask);
	if (tx_slotnum > 2 || !tx_slotnum) {
		ret = -EINVAL;
		dev_err(component->dev, "too many tx slots or zero slot\n");
		goto _set_tdm_err_;
	}

	first_bit = __ffs(tx_mask);
	last_bit = __fls(tx_mask);
	if (last_bit - first_bit > 1) {
		ret = -EINVAL;
		dev_err(component->dev, "tx slot location error\n");
		goto _set_tdm_err_;
	}

	if (tx_slotnum == 1) {
		snd_soc_component_update_bits(component, RT1011_TDM1_SET_2,
			RT1011_TDM_I2S_DOCK_ADCDAT_LEN_1_MASK |
			RT1011_TDM_ADCDAT1_DATA_LOCATION, first_bit);
		switch (first_bit) {
		case 1:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC1_1_MASK,
				RT1011_TDM_I2S_RX_ADC1_1_LL);
			break;
		case 3:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC2_1_MASK,
				RT1011_TDM_I2S_RX_ADC2_1_LL);
			break;
		case 5:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC3_1_MASK,
				RT1011_TDM_I2S_RX_ADC3_1_LL);
			break;
		case 7:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC4_1_MASK,
				RT1011_TDM_I2S_RX_ADC4_1_LL);
			break;
		case 0:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC1_1_MASK, 0);
			break;
		case 2:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC2_1_MASK, 0);
			break;
		case 4:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC3_1_MASK, 0);
			break;
		case 6:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_3,
				RT1011_TDM_I2S_RX_ADC4_1_MASK, 0);
			break;
		default:
			ret = -EINVAL;
			dev_dbg(component->dev,
				"tx slot location error\n");
			goto _set_tdm_err_;
		}
	} else if (tx_slotnum == 2) {
		switch (first_bit) {
		case 0:
		case 2:
		case 4:
		case 6:
			snd_soc_component_update_bits(component,
				RT1011_TDM1_SET_2,
				RT1011_TDM_I2S_DOCK_ADCDAT_LEN_1_MASK |
				RT1011_TDM_ADCDAT1_DATA_LOCATION,
				RT1011_TDM_I2S_DOCK_ADCDAT_2CH | first_bit);
			break;
		default:
			ret = -EINVAL;
			dev_dbg(component->dev,
				"tx slot location should be paired and start from slot0/2/4/6\n");
			goto _set_tdm_err_;
		}
	}

	snd_soc_component_update_bits(component, RT1011_TDM1_SET_1,
		RT1011_I2S_CH_TX_MASK | RT1011_I2S_CH_RX_MASK |
		RT1011_I2S_CH_TX_LEN_MASK | RT1011_I2S_CH_RX_LEN_MASK, val);
	snd_soc_component_update_bits(component, RT1011_TDM2_SET_1,
		RT1011_I2S_CH_TX_MASK | RT1011_I2S_CH_RX_MASK |
		RT1011_I2S_CH_TX_LEN_MASK | RT1011_I2S_CH_RX_LEN_MASK, val);
	snd_soc_component_update_bits(component, RT1011_TDM1_SET_2,
		RT1011_TDM_I2S_DOCK_EN_1_MASK, tdm_en);
	snd_soc_component_update_bits(component, RT1011_TDM2_SET_2,
		RT1011_TDM_I2S_DOCK_EN_2_MASK, tdm_en);

	snd_soc_component_update_bits(component, RT1011_TDM_TOTAL_SET,
		RT1011_ADCDAT1_PIN_CONFIG | RT1011_ADCDAT2_PIN_CONFIG,
		RT1011_ADCDAT1_OUTPUT | RT1011_ADCDAT2_OUTPUT);

_set_tdm_err_:
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}

static int rt1011_probe(struct snd_soc_component *component)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);
	int i;

	rt1011->component = component;

	schedule_work(&rt1011->cali_work);

	rt1011->i2s_ref = 0;
	rt1011->bq_drc_params = devm_kcalloc(component->dev,
		RT1011_ADVMODE_NUM, sizeof(struct rt1011_bq_drc_params *),
		GFP_KERNEL);
	if (!rt1011->bq_drc_params)
		return -ENOMEM;

	for (i = 0; i < RT1011_ADVMODE_NUM; i++) {
		rt1011->bq_drc_params[i] = devm_kcalloc(component->dev,
			RT1011_BQ_DRC_NUM, sizeof(struct rt1011_bq_drc_params),
			GFP_KERNEL);
		if (!rt1011->bq_drc_params[i])
			return -ENOMEM;
	}

	return 0;
}

static void rt1011_remove(struct snd_soc_component *component)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	cancel_work_sync(&rt1011->cali_work);
	rt1011_reset(rt1011->regmap);
}

#ifdef CONFIG_PM
static int rt1011_suspend(struct snd_soc_component *component)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1011->regmap, true);
	regcache_mark_dirty(rt1011->regmap);

	return 0;
}

static int rt1011_resume(struct snd_soc_component *component)
{
	struct rt1011_priv *rt1011 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt1011->regmap, false);
	regcache_sync(rt1011->regmap);

	return 0;
}
#else
#define rt1011_suspend NULL
#define rt1011_resume NULL
#endif

static int rt1011_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_OFF:
		snd_soc_component_write(component,
			RT1011_SYSTEM_RESET_1, 0x0000);
		snd_soc_component_write(component,
			RT1011_SYSTEM_RESET_2, 0x0000);
		snd_soc_component_write(component,
			RT1011_SYSTEM_RESET_3, 0x0001);
		snd_soc_component_write(component,
			RT1011_SYSTEM_RESET_1, 0x003f);
		snd_soc_component_write(component,
			RT1011_SYSTEM_RESET_2, 0x7fd7);
		snd_soc_component_write(component,
			RT1011_SYSTEM_RESET_3, 0x770f);
		break;
	default:
		break;
	}

	return 0;
}

#define RT1011_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT1011_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops rt1011_aif_dai_ops = {
	.hw_params = rt1011_hw_params,
	.set_fmt = rt1011_set_dai_fmt,
	.set_tdm_slot = rt1011_set_tdm_slot,
};

static struct snd_soc_dai_driver rt1011_dai[] = {
	{
		.name = "rt1011-aif",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT1011_STEREO_RATES,
			.formats = RT1011_FORMATS,
		},
		.ops = &rt1011_aif_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_rt1011 = {
	.probe = rt1011_probe,
	.remove = rt1011_remove,
	.suspend = rt1011_suspend,
	.resume = rt1011_resume,
	.set_bias_level = rt1011_set_bias_level,
	.controls = rt1011_snd_controls,
	.num_controls = ARRAY_SIZE(rt1011_snd_controls),
	.dapm_widgets = rt1011_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt1011_dapm_widgets),
	.dapm_routes = rt1011_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt1011_dapm_routes),
	.set_sysclk = rt1011_set_component_sysclk,
	.set_pll = rt1011_set_component_pll,
	.use_pmdown_time = 1,
	.endianness = 1,
};

static const struct regmap_config rt1011_regmap = {
	.reg_bits = 16,
	.val_bits = 16,
	.max_register = RT1011_MAX_REG + 1,
	.volatile_reg = rt1011_volatile_register,
	.readable_reg = rt1011_readable_register,
	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = rt1011_reg,
	.num_reg_defaults = ARRAY_SIZE(rt1011_reg),
	.use_single_read = true,
	.use_single_write = true,
};

#if defined(CONFIG_OF)
static const struct of_device_id rt1011_of_match[] = {
	{ .compatible = "realtek,rt1011", },
	{},
};
MODULE_DEVICE_TABLE(of, rt1011_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt1011_acpi_match[] = {
	{"10EC1011", 0,},
	{},
};
MODULE_DEVICE_TABLE(acpi, rt1011_acpi_match);
#endif

static const struct i2c_device_id rt1011_i2c_id[] = {
	{ "rt1011" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt1011_i2c_id);

static int rt1011_calibrate(struct rt1011_priv *rt1011, unsigned char cali_flag)
{
	unsigned int value, count = 0, r0[3];
	unsigned int chk_cnt = 50; /* DONT change this */
	unsigned int dc_offset;
	unsigned int r0_integer, r0_factor, format;
	struct device *dev = regmap_get_device(rt1011->regmap);
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(rt1011->component);
	int ret = 0;

	snd_soc_dapm_mutex_lock(dapm);
	regcache_cache_bypass(rt1011->regmap, true);

	regmap_write(rt1011->regmap, RT1011_RESET, 0x0000);
	regmap_write(rt1011->regmap, RT1011_SYSTEM_RESET_3, 0x740f);
	regmap_write(rt1011->regmap, RT1011_SYSTEM_RESET_3, 0x770f);

	/* RC clock */
	regmap_write(rt1011->regmap, RT1011_CLK_2, 0x9400);
	regmap_write(rt1011->regmap, RT1011_PLL_1, 0x0800);
	regmap_write(rt1011->regmap, RT1011_PLL_2, 0x0020);
	regmap_write(rt1011->regmap, RT1011_CLK_DET, 0x0800);

	/* ADC/DAC setting */
	regmap_write(rt1011->regmap, RT1011_ADC_SET_5, 0x0a20);
	regmap_write(rt1011->regmap, RT1011_DAC_SET_2, 0xe232);
	regmap_write(rt1011->regmap, RT1011_ADC_SET_4, 0xc000);

	/* DC detection */
	regmap_write(rt1011->regmap, RT1011_SPK_PRO_DC_DET_1, 0xb00c);
	regmap_write(rt1011->regmap, RT1011_SPK_PRO_DC_DET_2, 0xcccc);

	/* Power */
	regmap_write(rt1011->regmap, RT1011_POWER_1, 0xe0e0);
	regmap_write(rt1011->regmap, RT1011_POWER_3, 0x5003);
	regmap_write(rt1011->regmap, RT1011_POWER_9, 0xa860);
	regmap_write(rt1011->regmap, RT1011_DAC_SET_2, 0xa032);

	/* POW_PLL / POW_BG / POW_BG_MBIAS_LV / POW_V/I */
	regmap_write(rt1011->regmap, RT1011_POWER_2, 0x0007);
	regmap_write(rt1011->regmap, RT1011_POWER_3, 0x5ff7);
	regmap_write(rt1011->regmap, RT1011_A_TEMP_SEN, 0x7f44);
	regmap_write(rt1011->regmap, RT1011_A_TIMING_1, 0x4054);
	regmap_write(rt1011->regmap, RT1011_BAT_GAIN_1, 0x309c);

	/* DC offset from EFUSE */
	regmap_write(rt1011->regmap, RT1011_DC_CALIB_CLASSD_3, 0xcb00);
	regmap_write(rt1011->regmap, RT1011_BOOST_CON_1, 0xe080);
	regmap_write(rt1011->regmap, RT1011_POWER_4, 0x16f2);
	regmap_write(rt1011->regmap, RT1011_POWER_6, 0x36ad);

	/* mixer */
	regmap_write(rt1011->regmap, RT1011_MIXER_1, 0x3f1d);

	/* EFUSE read */
	regmap_write(rt1011->regmap, RT1011_EFUSE_CONTROL_1, 0x0d0a);
	msleep(30);

	regmap_read(rt1011->regmap, RT1011_EFUSE_ADC_OFFSET_18_16, &value);
	dc_offset = value << 16;
	regmap_read(rt1011->regmap, RT1011_EFUSE_ADC_OFFSET_15_0, &value);
	dc_offset |= (value & 0xffff);
	dev_info(dev, "ADC offset=0x%x\n", dc_offset);
	regmap_read(rt1011->regmap, RT1011_EFUSE_DAC_OFFSET_G0_20_16, &value);
	dc_offset = value << 16;
	regmap_read(rt1011->regmap, RT1011_EFUSE_DAC_OFFSET_G0_15_0, &value);
	dc_offset |= (value & 0xffff);
	dev_info(dev, "Gain0 offset=0x%x\n", dc_offset);
	regmap_read(rt1011->regmap, RT1011_EFUSE_DAC_OFFSET_G1_20_16, &value);
	dc_offset = value << 16;
	regmap_read(rt1011->regmap, RT1011_EFUSE_DAC_OFFSET_G1_15_0, &value);
	dc_offset |= (value & 0xffff);
	dev_info(dev, "Gain1 offset=0x%x\n", dc_offset);

	if (cali_flag) {

		regmap_write(rt1011->regmap, RT1011_ADC_SET_1, 0x2925);
		/* Class D on */
		regmap_write(rt1011->regmap, RT1011_CLASS_D_POS, 0x010e);
		regmap_write(rt1011->regmap,
			RT1011_CLASSD_INTERNAL_SET_1, 0x1701);

		/* STP enable */
		regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_0, 0x8000);
		regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_7, 0xf000);
		regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_4, 0x4040);
		regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_0, 0xc000);
		regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_6, 0x07c2);

		r0[0] = r0[1] = r0[2] = count = 0;
		while (count < chk_cnt) {
			msleep(100);
			regmap_read(rt1011->regmap,
				RT1011_INIT_RECIPROCAL_SYN_24_16, &value);
			r0[count%3] = value << 16;
			regmap_read(rt1011->regmap,
				RT1011_INIT_RECIPROCAL_SYN_15_0, &value);
			r0[count%3] |= value;

			if (r0[count%3] == 0)
				continue;

			count++;

			if (r0[0] == r0[1] && r0[1] == r0[2])
				break;
		}
		if (count > chk_cnt) {
			dev_err(dev, "Calibrate R0 Failure\n");
			ret = -EAGAIN;
		} else {
			format = 2147483648U; /* 2^24 * 128 */
			r0_integer = format / r0[0] / 128;
			r0_factor = ((format / r0[0] * 100) / 128)
							- (r0_integer * 100);
			rt1011->r0_reg = r0[0];
			rt1011->cali_done = 1;
			dev_info(dev, "r0 resistance about %d.%02d ohm, reg=0x%X\n",
				r0_integer, r0_factor, r0[0]);
		}
	}

	/* depop */
	regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_0, 0x0000);
	msleep(400);
	regmap_write(rt1011->regmap, RT1011_POWER_9, 0xa840);
	regmap_write(rt1011->regmap, RT1011_SPK_TEMP_PROTECT_6, 0x0702);
	regmap_write(rt1011->regmap, RT1011_MIXER_1, 0xffdd);
	regmap_write(rt1011->regmap, RT1011_CLASSD_INTERNAL_SET_1, 0x0701);
	regmap_write(rt1011->regmap, RT1011_DAC_SET_3, 0xe004);
	regmap_write(rt1011->regmap, RT1011_A_TEMP_SEN, 0x7f40);
	regmap_write(rt1011->regmap, RT1011_POWER_1, 0x0000);
	regmap_write(rt1011->regmap, RT1011_POWER_2, 0x0000);
	regmap_write(rt1011->regmap, RT1011_POWER_3, 0x0002);
	regmap_write(rt1011->regmap, RT1011_POWER_4, 0x00f2);

	regmap_write(rt1011->regmap, RT1011_RESET, 0x0000);

	if (cali_flag) {
		if (count <= chk_cnt) {
			regmap_write(rt1011->regmap,
				RT1011_INIT_RECIPROCAL_REG_24_16,
				((r0[0]>>16) & 0x1ff));
			regmap_write(rt1011->regmap,
				RT1011_INIT_RECIPROCAL_REG_15_0,
				(r0[0] & 0xffff));
			regmap_write(rt1011->regmap,
				RT1011_SPK_TEMP_PROTECT_4, 0x4080);
		}
	}

	regcache_cache_bypass(rt1011->regmap, false);
	regcache_mark_dirty(rt1011->regmap);
	regcache_sync(rt1011->regmap);
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}

static void rt1011_calibration_work(struct work_struct *work)
{
	struct rt1011_priv *rt1011 =
		container_of(work, struct rt1011_priv, cali_work);
	struct snd_soc_component *component = rt1011->component;
	unsigned int r0_integer, r0_factor, format;

	if (rt1011->r0_calib)
		rt1011_calibrate(rt1011, 0);
	else
		rt1011_calibrate(rt1011, 1);

	/*
	 * This flag should reset after booting.
	 * The factory test will do calibration again and use this flag to check
	 * whether the calibration completed
	 */
	rt1011->cali_done = 0;

	/* initial */
	rt1011_reg_init(component);

	/* Apply temperature and calibration data from device property */
	if (rt1011->temperature_calib <= 0xff &&
		rt1011->temperature_calib > 0) {
		snd_soc_component_update_bits(component,
			RT1011_STP_INITIAL_RESISTANCE_TEMP, 0x3ff,
			(rt1011->temperature_calib << 2));
	}

	if (rt1011->r0_calib) {
		rt1011->r0_reg = rt1011->r0_calib;

		format = 2147483648U; /* 2^24 * 128 */
		r0_integer = format / rt1011->r0_reg / 128;
		r0_factor = ((format / rt1011->r0_reg * 100) / 128)
						- (r0_integer * 100);
		dev_info(component->dev, "DP r0 resistance about %d.%02d ohm, reg=0x%X\n",
			r0_integer, r0_factor, rt1011->r0_reg);

		rt1011_r0_load(rt1011);
	}

	snd_soc_component_write(component, RT1011_ADC_SET_1, 0x2925);
}

static int rt1011_parse_dp(struct rt1011_priv *rt1011, struct device *dev)
{
	device_property_read_u32(dev, "realtek,temperature_calib",
		&rt1011->temperature_calib);
	device_property_read_u32(dev, "realtek,r0_calib",
		&rt1011->r0_calib);

	dev_dbg(dev, "%s: r0_calib: 0x%x, temperature_calib: 0x%x",
		__func__, rt1011->r0_calib, rt1011->temperature_calib);

	return 0;
}

static int rt1011_i2c_probe(struct i2c_client *i2c)
{
	struct rt1011_priv *rt1011;
	int ret;
	unsigned int val;

	rt1011 = devm_kzalloc(&i2c->dev, sizeof(struct rt1011_priv),
				GFP_KERNEL);
	if (!rt1011)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt1011);

	rt1011_parse_dp(rt1011, &i2c->dev);

	rt1011->regmap = devm_regmap_init_i2c(i2c, &rt1011_regmap);
	if (IS_ERR(rt1011->regmap)) {
		ret = PTR_ERR(rt1011->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt1011->regmap, RT1011_DEVICE_ID, &val);
	if (val != RT1011_DEVICE_ID_NUM) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt1011\n", val);
		return -ENODEV;
	}

	INIT_WORK(&rt1011->cali_work, rt1011_calibration_work);

	return devm_snd_soc_register_component(&i2c->dev,
		&soc_component_dev_rt1011,
		rt1011_dai, ARRAY_SIZE(rt1011_dai));

}

static void rt1011_i2c_shutdown(struct i2c_client *client)
{
	struct rt1011_priv *rt1011 = i2c_get_clientdata(client);

	rt1011_reset(rt1011->regmap);
}

static struct i2c_driver rt1011_i2c_driver = {
	.driver = {
		.name = "rt1011",
		.of_match_table = of_match_ptr(rt1011_of_match),
		.acpi_match_table = ACPI_PTR(rt1011_acpi_match)
	},
	.probe = rt1011_i2c_probe,
	.shutdown = rt1011_i2c_shutdown,
	.id_table = rt1011_i2c_id,
};
module_i2c_driver(rt1011_i2c_driver);

MODULE_DESCRIPTION("ASoC RT1011 amplifier driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
