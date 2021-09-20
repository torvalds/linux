// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_sfc.c - Tegra210 SFC driver
//
// Copyright (c) 2021 NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra210_sfc.h"
#include "tegra_cif.h"

#define UNSUPP_CONV ((void *)(-EOPNOTSUPP))
#define BYPASS_CONV NULL

static const struct reg_default tegra210_sfc_reg_defaults[] = {
	{ TEGRA210_SFC_RX_INT_MASK, 0x00000001},
	{ TEGRA210_SFC_RX_CIF_CTRL, 0x00007700},
	{ TEGRA210_SFC_TX_INT_MASK, 0x00000001},
	{ TEGRA210_SFC_TX_CIF_CTRL, 0x00007700},
	{ TEGRA210_SFC_CG, 0x1},
	{ TEGRA210_SFC_CFG_RAM_CTRL, 0x00004000},
};

static const int tegra210_sfc_rates[TEGRA210_SFC_NUM_RATES] = {
	8000,
	11025,
	16000,
	22050,
	24000,
	32000,
	44100,
	48000,
	88200,
	96000,
	176400,
	192000,
};

/* coeff RAM tables required for SFC */
static u32 coef_8to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00235204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0000015f,//input gain
	0x00a7909c, 0xff241c71, 0x005f5e00,
	0xffca77f4, 0xff20dd50, 0x006855eb,
	0xff86c552, 0xff18137a, 0x00773648,
	0x00000001//output gain
};

static u32 coef_8to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_8to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00230204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000001//output gain
};

static u32 coef_8to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x0000a105,//header
	0x000005e1,//input gain
	0x00dca92f, 0xff45647a, 0x0046b59c,
	0x00429d1e, 0xff4fec62, 0x00516d30,
	0xffdea779, 0xff5e08ba, 0x0060185e,
	0xffafbab2, 0xff698d5a, 0x006ce3ae,
	0xff9a82d2, 0xff704674, 0x007633c5,
	0xff923433, 0xff721128, 0x007cff42,
	0x00000003//output gain
};

static u32 coef_8to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_8to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x0156105,//interpolation + IIR filter
	0x0000d649,//input gain
	0x00e87afb, 0xff5f69d0, 0x003df3cf,
	0x007ce488, 0xff99a5c8, 0x0056a6a0,
	0x00344928, 0xffcba3e5, 0x006be470,
	0x00137aa7, 0xffe60276, 0x00773410,
	0x0005fa2a, 0xfff1ac11, 0x007c795b,
	0x00012d36, 0xfff5eca2, 0x007f10ef,
	0x00000002,//ouptut gain
	0x0021a102,//interpolation + IIR filter
	0x00000e00,//input gain
	0x00e2e000, 0xff6e1a00, 0x002aaa00,
	0x00610a00, 0xff5dda00, 0x003ccc00,
	0x00163a00, 0xff3c0400, 0x00633200,
	0x00000003,//Output gain
	0x00000204,//Farrow filter
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_8to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00156105,//interpolation + IIR Filter
	0x0000d649,//input gain
	0x00e87afb, 0xff5f69d0, 0x003df3cf,
	0x007ce488, 0xff99a5c8, 0x0056a6a0,
	0x00344928, 0xffcba3e5, 0x006be470,
	0x00137aa7, 0xffe60276, 0x00773410,
	0x0005fa2a, 0xfff1ac11, 0x007c795b,
	0x00012d36, 0xfff5eca2, 0x007f10ef,
	0x00000002,//ouptut gain
	0x0000a102,//interpolation + IIR filter
	0x00000e00,//input gain
	0x00e2e000, 0xff6e1a00, 0x002aaa00,
	0x00610a00, 0xff5dda00, 0x003ccc00,
	0x00163a00, 0xff3c0400, 0x00633200,
	0x00000003//output gain
};

static u32 coef_8to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x0024a102,//header
	0x0000007d,//input gain
	0x007d1f20, 0xff1a540e, 0x00678bf9,
	0xff916625, 0xff16b0ff, 0x006e433a,
	0xff5af660, 0xff0eb91f, 0x00797356,
	0x00000003,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_8to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x0000a102,//header
	0x0000007d,//input gain
	0x007d1f20, 0xff1a540e, 0x00678bf9,
	0xff916625, 0xff16b0ff, 0x006e433a,
	0xff5af660, 0xff0eb91f, 0x00797356,
	0x00000003//output gain
};

static u32 coef_11to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0000015f,//input gain
	0x00a7909c, 0xff241c71, 0x005f5e00,
	0xffca77f4, 0xff20dd50, 0x006855eb,
	0xff86c552, 0xff18137a, 0x00773648,
	0x00000002,//output gain
	0x00186102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000002,//output gain
	0x00239204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_11to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00009204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_11to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_11to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_11to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00009204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_11to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_11to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_11to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00006102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002//output gain
};

static u32 coef_11to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_16to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_16to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000fa103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000003,//output gain
	0x001a5204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_16to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00235204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0000015f,//input gain
	0x00a7909c, 0xff241c71, 0x005f5e00,
	0xffca77f4, 0xff20dd50, 0x006855eb,
	0xff86c552, 0xff18137a, 0x00773648,
	0x00000001//output gain
};

static u32 coef_16to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x0015a105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000003,//output gain
	0x00005105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000001//output gain
};

static u32 coef_16to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_16to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00156105,//interpolation + IIR filter
	0x0000d649,//input gain
	0x00e87afb, 0xff5f69d0, 0x003df3cf,
	0x007ce488, 0xff99a5c8, 0x0056a6a0,
	0x00344928, 0xffcba3e5, 0x006be470,
	0x00137aa7, 0xffe60276, 0x00773410,
	0x0005fa2a, 0xfff1ac11, 0x007c795b,
	0x00012d36, 0xfff5eca2, 0x007f10ef,
	0x00000002,//output gain
	0x0021a102,//interpolation + IIR filter
	0x00000e00,//input gain
	0x00e2e000, 0xff6e1a00, 0x002aaa00,
	0x00610a00, 0xff5dda00, 0x003ccc00,
	0x00163a00, 0xff3c0400, 0x00633200,
	0x00000003,//output gain
	0x002c0204,//Farrow Filter
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005101,//IIR Filter + Decimator
	0x0000203c,//input gain
	0x00f52d35, 0xff2e2162, 0x005a21e0,
	0x00c6f0f0, 0xff2ecd69, 0x006fa78d,
	0x00000001//output gain
};

static u32 coef_16to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x0000a105,//interpolation + IIR Filter
	0x00000784,//input gain
	0x00cc516e, 0xff2c9639, 0x005ad5b3,
	0x0013ad0d, 0xff3d4799, 0x0063ce75,
	0xffb6f398, 0xff5138d1, 0x006e9e1f,
	0xff9186e5, 0xff5f96a4, 0x0076a86e,
	0xff82089c, 0xff676b81, 0x007b9f8a,
	0xff7c48a5, 0xff6a31e7, 0x007ebb7b,
	0x00000003//output gain
};

static u32 coef_16to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_16to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0000a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003//output gain
};

static u32 coef_16to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x0024a102,//header
	0x0000007d,//input gain
	0x007d1f20, 0xff1a540e, 0x00678bf9,
	0xff916625, 0xff16b0ff, 0x006e433a,
	0xff5af660, 0xff0eb91f, 0x00797356,
	0x00000003,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_16to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x0000a102,//header
	0x0000007d,//input gain
	0x007d1f20, 0xff1a540e, 0x00678bf9,
	0xff916625, 0xff16b0ff, 0x006e433a,
	0xff5af660, 0xff0eb91f, 0x00797356,
	0x00000003//output gain
};

static u32 coef_22to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000002,//output gain
	0x00179204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_22to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_22to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0000015f,//input gain
	0x00a7909c, 0xff241c71, 0x005f5e00,
	0xffca77f4, 0xff20dd50, 0x006855eb,
	0xff86c552, 0xff18137a, 0x00773648,
	0x00000002,//output gain
	0x00186102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000002,//output gain
	0x00239204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_22to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00235204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d029,//input gain
	0x00f2a98b, 0xff92aa71, 0x001fcd16,
	0x00ae9004, 0xffb85140, 0x0041813a,
	0x007f8ed1, 0xffd585fc, 0x006a69e6,
	0x00000001//output gain
};

static u32 coef_22to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00009204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_22to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_22to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_22to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_22to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_22to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00006102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002//output gain
};

static u32 coef_22to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_24to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00009105,//header
	0x000005e1,//input gain
	0x00dca92f, 0xff45647a, 0x0046b59c,
	0x00429d1e, 0xff4fec62, 0x00516d30,
	0xffdea779, 0xff5e08ba, 0x0060185e,
	0xffafbab2, 0xff698d5a, 0x006ce3ae,
	0xff9a82d2, 0xff704674, 0x007633c5,
	0xff923433, 0xff721128, 0x007cff42,
	0x00000001//output gain
};

static u32 coef_24to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000f6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x001a5204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_24to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00156105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000002,//output gain
	0x00009105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000001//output gain
};

static u32 coef_24to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d029,//input gain
	0x00f2a98b, 0xff92aa71, 0x001fcd16,
	0x00ae9004, 0xffb85140, 0x0041813a,
	0x007f8ed1, 0xffd585fc, 0x006a69e6,
	0x00000002,//output gain
	0x001b6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x00265204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_24to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00009102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001//output gain
};

static u32 coef_24to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00230204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x00001685,//input gain
	0x00f53ae9, 0xff52f196, 0x003e3e08,
	0x00b9f857, 0xff5d8985, 0x0050070a,
	0x008c3e86, 0xff6053f0, 0x006d98ef,
	0x00000001//output gain
};

static u32 coef_24to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_24to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x002f0204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x00000138,//input gain
	0x00d5d232, 0xff2a3bf8, 0x005a785c,
	0x0034001b, 0xff283109, 0x006462a6,
	0xffe6746a, 0xff1fb09c, 0x00758a91,
	0x00000001//output gain
};

static u32 coef_24to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_24to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_24to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00006102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002//output gain
};

static u32 coef_32to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_32to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000ca102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000003,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x0000d102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001//output gain
};

static u32 coef_32to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_32to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000fa103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000003,//output gain
	0x001a5204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_32to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000ca102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000003,//output gain
	0x0000d102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001//output gain
};

static u32 coef_32to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00235204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0000015f,//input gain
	0x00a7909c, 0xff241c71, 0x005f5e00,
	0xffca77f4, 0xff20dd50, 0x006855eb,
	0xff86c552, 0xff18137a, 0x00773648,
	0x00000001//output gain
};

static u32 coef_32to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x0015a105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000003,//output gain
	0x00005105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000001//output gain
};

static u32 coef_32to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00230204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000001//output gain
};

static u32 coef_32to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x0000a105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000003//output gain
};

static u32 coef_32to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0018a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003,//output gain
	0x00000204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_32to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x0000a102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000003//output gain
};

static u32 coef_44to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00120104,//IIR Filter
	0x00000af2,//input gain
	0x0057eebe, 0xff1e9863, 0x00652604,
	0xff7206ea, 0xff22ad7e, 0x006d47e1,
	0xff42a4d7, 0xff26e722, 0x0075fd83,
	0xff352f66, 0xff29312b, 0x007b986b,
	0xff310a07, 0xff296f51, 0x007eca7c,
	0x00000001,//output gain
	0x001d9204,//Farrow Filter + decimation
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005105,//IIR Filter + Decimator
	0x0000d649,//input gain
	0x00e87afb, 0xff5f69d0, 0x003df3cf,
	0x007ce488, 0xff99a5c8, 0x0056a6a0,
	0x00344928, 0xffcba3e5, 0x006be470,
	0x00137aa7, 0xffe60276, 0x00773410,
	0x0005fa2a, 0xfff1ac11, 0x007c795b,
	0x00012d36, 0xfff5eca2, 0x007f10ef,
	0x00000001//output gain
};

static u32 coef_44to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_44to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00126104,//IIR Filter + interpolation
	0x00000af2,//input gain
	0x0057eebe, 0xff1e9863, 0x00652604,
	0xff7206ea, 0xff22ad7e, 0x006d47e1,
	0xff42a4d7, 0xff26e722, 0x0075fd83,
	0xff352f66, 0xff29312b, 0x007b986b,
	0xff310a07, 0xff296f51, 0x007eca7c,
	0x00000002,//output gain
	0x001d9204,//Farrow Filter + decimation
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005105,//IIR Filter + Decimator
	0x0000d649,//input gain
	0x00e87afb, 0xff5f69d0, 0x003df3cf,
	0x007ce488, 0xff99a5c8, 0x0056a6a0,
	0x00344928, 0xffcba3e5, 0x006be470,
	0x00137aa7, 0xffe60276, 0x00773410,
	0x0005fa2a, 0xfff1ac11, 0x007c795b,
	0x00012d36, 0xfff5eca2, 0x007f10ef,
	0x00000001//output gain
};

static u32 coef_44to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_44to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x00001685,//input gain
	0x00f53ae9, 0xff52f196, 0x003e3e08,
	0x00b9f857, 0xff5d8985, 0x0050070a,
	0x008c3e86, 0xff6053f0, 0x006d98ef,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_44to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0000015f,//input gain
	0x00a7909c, 0xff241c71, 0x005f5e00,
	0xffca77f4, 0xff20dd50, 0x006855eb,
	0xff86c552, 0xff18137a, 0x00773648,
	0x00000002,//output gain
	0x00186102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000002,//output gain
	0x00239204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_44to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00235204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d029,//input gain
	0x00f2a98b, 0xff92aa71, 0x001fcd16,
	0x00ae9004, 0xffb85140, 0x0041813a,
	0x007f8ed1, 0xffd585fc, 0x006a69e6,
	0x00000001//output gain
};

static u32 coef_44to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_44to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_44to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_44to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_48to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c9102,//IIR Filter + Decimator
	0x00000e00,//input gain
	0x00e2e000, 0xff6e1a00, 0x002aaa00,
	0x00610a00, 0xff5dda00, 0x003ccc00,
	0x00163a00, 0xff3c0400, 0x00633200,
	0x00000001,//output gain
	0x00005105,//IIR Filter + Decimator
	0x0000d649,//input gain
	0x00e87afb, 0xff5f69d0, 0x003df3cf,
	0x007ce488, 0xff99a5c8, 0x0056a6a0,
	0x00344928, 0xffcba3e5, 0x006be470,
	0x00137aa7, 0xffe60276, 0x00773410,
	0x0005fa2a, 0xfff1ac11, 0x007c795b,
	0x00012d36, 0xfff5eca2, 0x007f10ef,
	0x00000001//output gain
};

static u32 coef_48to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_48to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00009105,//IIR Filter + Decimator
	0x00000784,//input gain
	0x00cc516e, 0xff2c9639, 0x005ad5b3,
	0x0013ad0d, 0xff3d4799, 0x0063ce75,
	0xffb6f398, 0xff5138d1, 0x006e9e1f,
	0xff9186e5, 0xff5f96a4, 0x0076a86e,
	0xff82089c, 0xff676b81, 0x007b9f8a,
	0xff7c48a5, 0xff6a31e7, 0x007ebb7b,
	0x00000001//output gain
};

static u32 coef_48to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000f6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x001a5204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_48to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_48to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00156105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000002,//output gain
	0x00009105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000001//output gain
};

static u32 coef_48to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d029,//input gain
	0x00f2a98b, 0xff92aa71, 0x001fcd16,
	0x00ae9004, 0xffb85140, 0x0041813a,
	0x007f8ed1, 0xffd585fc, 0x006a69e6,
	0x00000002,//output gain
	0x001b6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x00265204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_48to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00230204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x00001685,//input gain
	0x00f53ae9, 0xff52f196, 0x003e3e08,
	0x00b9f857, 0xff5d8985, 0x0050070a,
	0x008c3e86, 0xff6053f0, 0x006d98ef,
	0x00000001//output gain
};

static u32 coef_48to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002//output gain
};

static u32 coef_48to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00186102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00246102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x002f0204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x00000138,//input gain
	0x00d5d232, 0xff2a3bf8, 0x005a785c,
	0x0034001b, 0xff283109, 0x006462a6,
	0xffe6746a, 0xff1fb09c, 0x00758a91,
	0x00000001//output gain
};

static u32 coef_48to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000002,//output gain
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_88to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x00000057,//input gain
	0x00a8e717, 0xff1c748d, 0x0065b976,
	0xffcbccab, 0xff190aff, 0x006cc1cf,
	0xff871ce1, 0xff10d878, 0x0078cfc5,
	0x00000001,//output gain
	0x00179204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000001,//output gain
	0x00185102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000001,//output gain
	0x00179204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x00001685,//input gain
	0x00f53ae9, 0xff52f196, 0x003e3e08,
	0x00b9f857, 0xff5d8985, 0x0050070a,
	0x008c3e86, 0xff6053f0, 0x006d98ef,
	0x00000001,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000002,//output gain
	0x00179204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x00001685,//input gain
	0x00f53ae9, 0xff52f196, 0x003e3e08,
	0x00b9f857, 0xff5d8985, 0x0050070a,
	0x008c3e86, 0xff6053f0, 0x006d98ef,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_88to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_88to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002//output gain
};

static u32 coef_88to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000002,//output gain
	0x00186102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_96to8[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c9102,//header
	0x0000007d,//input gain
	0x007d1f20, 0xff1a540e, 0x00678bf9,
	0xff916625, 0xff16b0ff, 0x006e433a,
	0xff5af660, 0xff0eb91f, 0x00797356,
	0x00000001,//output gain
	0x00185102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to11[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000001,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c9102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00009105,//header
	0x00000292,//input gain
	0x00e4320a, 0xff41d2d9, 0x004911ac,
	0x005dd9e3, 0xff4c7d80, 0x0052103e,
	0xfff8ebef, 0xff5b6fab, 0x005f0a0d,
	0xffc4b414, 0xff68582c, 0x006b38e5,
	0xffabb861, 0xff704bec, 0x0074de52,
	0xffa19f4c, 0xff729059, 0x007c7e90,
	0x00000001//output gain
};

static u32 coef_96to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000f6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x001a5204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_96to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000f6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x001a0204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001//output gain
};

static u32 coef_96to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000f6103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002,//output gain
	0x001b6102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000002,//output gain
	0x00260204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000001//output gain
};

static u32 coef_96to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00006103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000002//output gain
};

static u32 coef_176to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x00000057,//input gain
	0x00a8e717, 0xff1c748d, 0x0065b976,
	0xffcbccab, 0xff190aff, 0x006cc1cf,
	0xff871ce1, 0xff10d878, 0x0078cfc5,
	0x00000001,//output gain
	0x00179204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_176to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000001,//output gain
	0x00185102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_176to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x00000138,//input gain
	0x00d5d232, 0xff2a3bf8, 0x005a785c,
	0x0034001b, 0xff283109, 0x006462a6,
	0xffe6746a, 0xff1fb09c, 0x00758a91,
	0x00000001,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_176to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x000005f3,//input gain
	0x00d816d6, 0xff385383, 0x004fe566,
	0x003c548d, 0xff38c23d, 0x005d0b1c,
	0xfff02f7d, 0xff31e983, 0x0072d65d,
	0x00000001,//output gain
	0x00179204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_176to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_176to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x00001685,//input gain
	0x00f53ae9, 0xff52f196, 0x003e3e08,
	0x00b9f857, 0xff5d8985, 0x0050070a,
	0x008c3e86, 0xff6053f0, 0x006d98ef,
	0x00000001,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_176to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001//output gain
};

static u32 coef_176to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000001//output gain
};

static u32 coef_176to192[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000002,//output gain
	0x00005204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000
};

static u32 coef_192to16[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c9102,//header
	0x0000007d,//input gain
	0x007d1f20, 0xff1a540e, 0x00678bf9,
	0xff916625, 0xff16b0ff, 0x006e433a,
	0xff5af660, 0xff0eb91f, 0x00797356,
	0x00000001,//output gain
	0x00185102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_192to22[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c0102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000001,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_192to24[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000001,//output gain
	0x00185102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_192to32[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c9102,//header
	0x000005d6,//input gain
	0x00c6543e, 0xff342935, 0x0052f116,
	0x000a1d78, 0xff3330c0, 0x005f88a3,
	0xffbee7c0, 0xff2b5ba5, 0x0073eb26,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_192to44[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00235102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_192to48[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c5102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001,//output gain
	0x00005102,//header
	0x0001d727,//input gain
	0x00fc2fc7, 0xff9bb27b, 0x001c564c,
	0x00e55557, 0xffcadd5b, 0x003d80ba,
	0x00d13397, 0xfff232f8, 0x00683337,
	0x00000001//output gain
};

static u32 coef_192to88[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000002,//output gain
	0x00175204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x000013d9,//input gain
	0x00ebd477, 0xff4ce383, 0x0042049d,
	0x0089c278, 0xff54414d, 0x00531ded,
	0x004a5e07, 0xff53cf41, 0x006efbdc,
	0x00000001//output gain
};

static u32 coef_192to96[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x00005103,//header
	0x000001e0,//input gain
	0x00de44c0, 0xff380b7f, 0x004ffc73,
	0x00494b44, 0xff3d493a, 0x005908bf,
	0xffe9a3c8, 0xff425647, 0x006745f7,
	0xffc42d61, 0xff40a6c7, 0x00776709,
	0x00000001//output gain
};

static u32 coef_192to176[TEGRA210_SFC_COEF_RAM_DEPTH] = {
	0x000c6102,//header
	0x000000af,//input gain
	0x00c65663, 0xff23d2ce, 0x005f97d6,
	0x00086ad6, 0xff20ec4f, 0x00683201,
	0xffbbbef6, 0xff184447, 0x00770963,
	0x00000002,//output gain
	0x00170204,//farrow
	0x000aaaab,
	0xffaaaaab,
	0xfffaaaab,
	0x00555555,
	0xff600000,
	0xfff55555,
	0x00155555,
	0x00055555,
	0xffeaaaab,
	0x00200000,
	0x00005102,//header
	0x0000010a,//input gain
	0x00c93dc4, 0xff26f5f6, 0x005d1041,
	0x001002c4, 0xff245b76, 0x00666002,
	0xffc30a45, 0xff1baecd, 0x00765921,
	0x00000001//output gain
};

/*
 * Coefficient table for various sample rate conversions. The sample
 * rates available are as per tegra210_sfc_rates[].
 */
static s32 *coef_addr_table[TEGRA210_SFC_NUM_RATES][TEGRA210_SFC_NUM_RATES] = {
	/* Convertions from 8 kHz */
	{
		BYPASS_CONV,
		coef_8to11,
		coef_8to16,
		coef_8to22,
		coef_8to24,
		coef_8to32,
		coef_8to44,
		coef_8to48,
		coef_8to88,
		coef_8to96,
		UNSUPP_CONV,
		UNSUPP_CONV,
	},
	/* Convertions from 11.025 kHz */
	{
		coef_11to8,
		BYPASS_CONV,
		coef_11to16,
		coef_11to22,
		coef_11to24,
		coef_11to32,
		coef_11to44,
		coef_11to48,
		coef_11to88,
		coef_11to96,
		UNSUPP_CONV,
		UNSUPP_CONV,
	},
	/* Convertions from 16 kHz */
	{
		coef_16to8,
		coef_16to11,
		BYPASS_CONV,
		coef_16to22,
		coef_16to24,
		coef_16to32,
		coef_16to44,
		coef_16to48,
		coef_16to88,
		coef_16to96,
		coef_16to176,
		coef_16to192,
	},
	/* Convertions from 22.05 kHz */
	{
		coef_22to8,
		coef_22to11,
		coef_22to16,
		BYPASS_CONV,
		coef_22to24,
		coef_22to32,
		coef_22to44,
		coef_22to48,
		coef_22to88,
		coef_22to96,
		coef_22to176,
		coef_22to192,
	},
	/* Convertions from 24 kHz */
	{
		coef_24to8,
		coef_24to11,
		coef_24to16,
		coef_24to22,
		BYPASS_CONV,
		coef_24to32,
		coef_24to44,
		coef_24to48,
		coef_24to88,
		coef_24to96,
		coef_24to176,
		coef_24to192,
	},
	/* Convertions from 32 kHz */
	{
		coef_32to8,
		coef_32to11,
		coef_32to16,
		coef_32to22,
		coef_32to24,
		BYPASS_CONV,
		coef_32to44,
		coef_32to48,
		coef_32to88,
		coef_32to96,
		coef_32to176,
		coef_32to192,
	},
	/* Convertions from 44.1 kHz */
	{
		coef_44to8,
		coef_44to11,
		coef_44to16,
		coef_44to22,
		coef_44to24,
		coef_44to32,
		BYPASS_CONV,
		coef_44to48,
		coef_44to88,
		coef_44to96,
		coef_44to176,
		coef_44to192,
	},
	/* Convertions from 48 kHz */
	{
		coef_48to8,
		coef_48to11,
		coef_48to16,
		coef_48to22,
		coef_48to24,
		coef_48to32,
		coef_48to44,
		BYPASS_CONV,
		coef_48to88,
		coef_48to96,
		coef_48to176,
		coef_48to192,
	},
	/* Convertions from 88.2 kHz */
	{
		coef_88to8,
		coef_88to11,
		coef_88to16,
		coef_88to22,
		coef_88to24,
		coef_88to32,
		coef_88to44,
		coef_88to48,
		BYPASS_CONV,
		coef_88to96,
		coef_88to176,
		coef_88to192,
	},
	/* Convertions from 96 kHz */
	{	coef_96to8,
		coef_96to11,
		coef_96to16,
		coef_96to22,
		coef_96to24,
		coef_96to32,
		coef_96to44,
		coef_96to48,
		coef_96to88,
		BYPASS_CONV,
		coef_96to176,
		coef_96to192,
	},
	/* Convertions from 176.4 kHz */
	{
		UNSUPP_CONV,
		UNSUPP_CONV,
		coef_176to16,
		coef_176to22,
		coef_176to24,
		coef_176to32,
		coef_176to44,
		coef_176to48,
		coef_176to88,
		coef_176to96,
		BYPASS_CONV,
		coef_176to192,
	},
	/* Convertions from 192 kHz */
	{
		UNSUPP_CONV,
		UNSUPP_CONV,
		coef_192to16,
		coef_192to22,
		coef_192to24,
		coef_192to32,
		coef_192to44,
		coef_192to48,
		coef_192to88,
		coef_192to96,
		coef_192to176,
		BYPASS_CONV,
	},
};

static int __maybe_unused tegra210_sfc_runtime_suspend(struct device *dev)
{
	struct tegra210_sfc *sfc = dev_get_drvdata(dev);

	regcache_cache_only(sfc->regmap, true);
	regcache_mark_dirty(sfc->regmap);

	return 0;
}

static int __maybe_unused tegra210_sfc_runtime_resume(struct device *dev)
{
	struct tegra210_sfc *sfc = dev_get_drvdata(dev);

	regcache_cache_only(sfc->regmap, false);
	regcache_sync(sfc->regmap);

	return 0;
}

static inline void tegra210_sfc_write_ram(struct regmap *regmap,
					  s32 *data)
{
	int i;

	regmap_write(regmap, TEGRA210_SFC_CFG_RAM_CTRL,
		     TEGRA210_SFC_RAM_CTRL_SEQ_ACCESS_EN |
		     TEGRA210_SFC_RAM_CTRL_ADDR_INIT_EN |
		     TEGRA210_SFC_RAM_CTRL_RW_WRITE);

	for (i = 0; i < TEGRA210_SFC_COEF_RAM_DEPTH; i++)
		regmap_write(regmap, TEGRA210_SFC_CFG_RAM_DATA, data[i]);
}

static int tegra210_sfc_write_coeff_ram(struct snd_soc_component *cmpnt)
{
	struct tegra210_sfc *sfc = dev_get_drvdata(cmpnt->dev);
	s32 *coeff_ram;

	/* Bypass */
	if (sfc->srate_in == sfc->srate_out)
		return 0;

	coeff_ram = coef_addr_table[sfc->srate_in][sfc->srate_out];
	if (IS_ERR_OR_NULL(coeff_ram)) {
		dev_err(cmpnt->dev,
			"Conversion from %d to %d Hz is not supported\n",
			sfc->srate_in, sfc->srate_out);

		return PTR_ERR_OR_ZERO(coeff_ram);
	}

	tegra210_sfc_write_ram(sfc->regmap, coeff_ram);

	regmap_update_bits(sfc->regmap,
			   TEGRA210_SFC_COEF_RAM,
			   TEGRA210_SFC_COEF_RAM_EN,
			   TEGRA210_SFC_COEF_RAM_EN);

	return 0;
}

static int tegra210_sfc_set_audio_cif(struct tegra210_sfc *sfc,
				      struct snd_pcm_hw_params *params,
				      unsigned int reg)
{
	unsigned int channels, audio_bits, path;
	struct tegra_cif_conf cif_conf;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	channels = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		audio_bits = TEGRA_ACIF_BITS_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		audio_bits = TEGRA_ACIF_BITS_32;
		break;
	default:
		return -EOPNOTSUPP;
	}

	cif_conf.audio_ch = channels;
	cif_conf.client_ch = channels;
	cif_conf.audio_bits = audio_bits;
	cif_conf.client_bits = TEGRA_ACIF_BITS_32;

	if (reg == TEGRA210_SFC_RX_CIF_CTRL)
		path = SFC_RX_PATH;
	else
		path = SFC_TX_PATH;

	cif_conf.stereo_conv = sfc->stereo_to_mono[path];
	cif_conf.mono_conv = sfc->mono_to_stereo[path];

	tegra_set_cif(sfc->regmap, reg, &cif_conf);

	return 0;
}

static int tegra210_sfc_soft_reset(struct tegra210_sfc *sfc)
{
	u32 val;

	/*
	 * Soft Reset: Below performs module soft reset which clears
	 * all FSM logic, flushes flow control of FIFO and resets the
	 * state register. It also brings module back to disabled
	 * state (without flushing the data in the pipe).
	 */
	regmap_update_bits(sfc->regmap, TEGRA210_SFC_SOFT_RESET,
			   TEGRA210_SFC_SOFT_RESET_EN, 1);

	return regmap_read_poll_timeout(sfc->regmap,
					TEGRA210_SFC_SOFT_RESET,
					val,
					!(val & TEGRA210_SFC_SOFT_RESET_EN),
					10, 10000);
}

static int tegra210_sfc_rate_to_idx(struct device *dev, int rate,
				    int *rate_idx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tegra210_sfc_rates); i++) {
		if (rate == tegra210_sfc_rates[i]) {
			*rate_idx = i;

			return 0;
		}
	}

	dev_err(dev, "Sample rate %d Hz is not supported\n", rate);

	return -EOPNOTSUPP;
}

static int tegra210_sfc_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra210_sfc *sfc = snd_soc_dai_get_drvdata(dai);
	int err;

	regmap_update_bits(sfc->regmap, TEGRA210_SFC_COEF_RAM,
			   TEGRA210_SFC_COEF_RAM_EN, 0);

	err = tegra210_sfc_soft_reset(sfc);
	if (err < 0) {
		dev_err(dai->dev, "Failed to reset SFC in %s, err = %d\n",
			__func__, err);

		return err;
	}

	return 0;
}

static int tegra210_sfc_in_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct tegra210_sfc *sfc = snd_soc_dai_get_drvdata(dai);
	struct device *dev = dai->dev;
	int err;

	err = tegra210_sfc_rate_to_idx(dev, params_rate(params),
				       &sfc->srate_in);
	if (err < 0)
		return err;

	err = tegra210_sfc_set_audio_cif(sfc, params, TEGRA210_SFC_RX_CIF_CTRL);
	if (err < 0) {
		dev_err(dev, "Can't set SFC RX CIF: %d\n", err);
		return err;
	}

	regmap_write(sfc->regmap, TEGRA210_SFC_RX_FREQ, sfc->srate_in);

	return err;
}

static int tegra210_sfc_out_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct tegra210_sfc *sfc = snd_soc_dai_get_drvdata(dai);
	struct device *dev = dai->dev;
	int err;

	err = tegra210_sfc_rate_to_idx(dev, params_rate(params),
				       &sfc->srate_out);
	if (err < 0)
		return err;

	err = tegra210_sfc_set_audio_cif(sfc, params, TEGRA210_SFC_TX_CIF_CTRL);
	if (err < 0) {
		dev_err(dev, "Can't set SFC TX CIF: %d\n", err);
		return err;
	}

	regmap_write(sfc->regmap, TEGRA210_SFC_TX_FREQ, sfc->srate_out);

	return 0;
}

static int tegra210_sfc_init(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);

	return tegra210_sfc_write_coeff_ram(cmpnt);
}

static int tegra210_sfc_get_control(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_sfc *sfc = snd_soc_component_get_drvdata(cmpnt);

	if (strstr(kcontrol->id.name, "Input Stereo To Mono"))
		ucontrol->value.integer.value[0] =
			sfc->stereo_to_mono[SFC_RX_PATH];
	else if (strstr(kcontrol->id.name, "Input Mono To Stereo"))
		ucontrol->value.integer.value[0] =
			sfc->mono_to_stereo[SFC_RX_PATH];
	else if (strstr(kcontrol->id.name, "Output Stereo To Mono"))
		ucontrol->value.integer.value[0] =
			sfc->stereo_to_mono[SFC_TX_PATH];
	else if (strstr(kcontrol->id.name, "Output Mono To Stereo"))
		ucontrol->value.integer.value[0] =
			sfc->mono_to_stereo[SFC_TX_PATH];

	return 0;
}

static int tegra210_sfc_put_control(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra210_sfc *sfc = snd_soc_component_get_drvdata(cmpnt);
	int value = ucontrol->value.integer.value[0];

	if (strstr(kcontrol->id.name, "Input Stereo To Mono"))
		sfc->stereo_to_mono[SFC_RX_PATH] = value;
	else if (strstr(kcontrol->id.name, "Input Mono To Stereo"))
		sfc->mono_to_stereo[SFC_RX_PATH] = value;
	else if (strstr(kcontrol->id.name, "Output Stereo To Mono"))
		sfc->stereo_to_mono[SFC_TX_PATH] = value;
	else if (strstr(kcontrol->id.name, "Output Mono To Stereo"))
		sfc->mono_to_stereo[SFC_TX_PATH] = value;
	else
		return 0;

	return 1;
}

static struct snd_soc_dai_ops tegra210_sfc_in_dai_ops = {
	.hw_params	= tegra210_sfc_in_hw_params,
	.startup	= tegra210_sfc_startup,
};

static struct snd_soc_dai_ops tegra210_sfc_out_dai_ops = {
	.hw_params	= tegra210_sfc_out_hw_params,
};

static struct snd_soc_dai_driver tegra210_sfc_dais[] = {
	{
		.name = "SFC-RX-CIF",
		.playback = {
			.stream_name = "RX-CIF-Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "RX-CIF-Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &tegra210_sfc_in_dai_ops,
	},
	{
		.name = "SFC-TX-CIF",
		.playback = {
			.stream_name = "TX-CIF-Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "TX-CIF-Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &tegra210_sfc_out_dai_ops,
	},
};

static const struct snd_soc_dapm_widget tegra210_sfc_widgets[] = {
	SND_SOC_DAPM_AIF_IN("RX", NULL, 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT_E("TX", NULL, 0, TEGRA210_SFC_ENABLE,
			       TEGRA210_SFC_EN_SHIFT, 0,
			       tegra210_sfc_init, SND_SOC_DAPM_PRE_PMU),
};

#define RESAMPLE_ROUTE(sname)					\
	{ "RX XBAR-" sname,	NULL,	"XBAR-TX" },		\
	{ "RX-CIF-" sname,	NULL,	"RX XBAR-" sname },	\
	{ "RX",			NULL,	"RX-CIF-" sname },	\
	{ "TX-CIF-" sname,	NULL,	"TX" },			\
	{ "TX XBAR-" sname,	NULL,	"TX-CIF-" sname },	\
	{ "XBAR-RX",		NULL,	"TX XBAR-" sname }

static const struct snd_soc_dapm_route tegra210_sfc_routes[] = {
	{ "TX", NULL, "RX" },
	RESAMPLE_ROUTE("Playback"),
	RESAMPLE_ROUTE("Capture"),
};

static const char * const tegra210_sfc_stereo_conv_text[] = {
	"CH0", "CH1", "AVG",
};

static const char * const tegra210_sfc_mono_conv_text[] = {
	"Zero", "Copy",
};

static const struct soc_enum tegra210_sfc_stereo_conv_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(tegra210_sfc_stereo_conv_text),
			tegra210_sfc_stereo_conv_text);

static const struct soc_enum tegra210_sfc_mono_conv_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(tegra210_sfc_mono_conv_text),
			tegra210_sfc_mono_conv_text);

static const struct snd_kcontrol_new tegra210_sfc_controls[] = {
	SOC_ENUM_EXT("Input Stereo To Mono", tegra210_sfc_stereo_conv_enum,
		tegra210_sfc_get_control, tegra210_sfc_put_control),
	SOC_ENUM_EXT("Input Mono To Stereo", tegra210_sfc_mono_conv_enum,
		tegra210_sfc_get_control, tegra210_sfc_put_control),
	SOC_ENUM_EXT("Output Stereo To Mono", tegra210_sfc_stereo_conv_enum,
		tegra210_sfc_get_control, tegra210_sfc_put_control),
	SOC_ENUM_EXT("Output Mono To Stereo", tegra210_sfc_mono_conv_enum,
		tegra210_sfc_get_control, tegra210_sfc_put_control),
};

static const struct snd_soc_component_driver tegra210_sfc_cmpnt = {
	.dapm_widgets		= tegra210_sfc_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_sfc_widgets),
	.dapm_routes		= tegra210_sfc_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_sfc_routes),
	.controls		= tegra210_sfc_controls,
	.num_controls		= ARRAY_SIZE(tegra210_sfc_controls),
};

static bool tegra210_sfc_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_SFC_RX_INT_MASK ... TEGRA210_SFC_RX_FREQ:
	case TEGRA210_SFC_TX_INT_MASK ... TEGRA210_SFC_TX_FREQ:
	case TEGRA210_SFC_ENABLE ... TEGRA210_SFC_CG:
	case TEGRA210_SFC_COEF_RAM ... TEGRA210_SFC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_sfc_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_SFC_RX_STATUS ... TEGRA210_SFC_RX_FREQ:
	case TEGRA210_SFC_TX_STATUS ... TEGRA210_SFC_TX_FREQ:
	case TEGRA210_SFC_ENABLE ... TEGRA210_SFC_INT_STATUS:
	case TEGRA210_SFC_COEF_RAM ... TEGRA210_SFC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_sfc_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_SFC_RX_STATUS:
	case TEGRA210_SFC_RX_INT_STATUS:
	case TEGRA210_SFC_RX_INT_SET:

	case TEGRA210_SFC_TX_STATUS:
	case TEGRA210_SFC_TX_INT_STATUS:
	case TEGRA210_SFC_TX_INT_SET:

	case TEGRA210_SFC_SOFT_RESET:
	case TEGRA210_SFC_STATUS:
	case TEGRA210_SFC_INT_STATUS:
	case TEGRA210_SFC_CFG_RAM_CTRL:
	case TEGRA210_SFC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static bool tegra210_sfc_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA210_SFC_CFG_RAM_DATA:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra210_sfc_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_SFC_CFG_RAM_DATA,
	.writeable_reg		= tegra210_sfc_wr_reg,
	.readable_reg		= tegra210_sfc_rd_reg,
	.volatile_reg		= tegra210_sfc_volatile_reg,
	.precious_reg		= tegra210_sfc_precious_reg,
	.reg_defaults		= tegra210_sfc_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(tegra210_sfc_reg_defaults),
	.cache_type		= REGCACHE_FLAT,
};

static const struct of_device_id tegra210_sfc_of_match[] = {
	{ .compatible = "nvidia,tegra210-sfc" },
	{},
};
MODULE_DEVICE_TABLE(of, tegra210_sfc_of_match);

static int tegra210_sfc_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra210_sfc *sfc;
	void __iomem *regs;
	int err;

	sfc = devm_kzalloc(dev, sizeof(*sfc), GFP_KERNEL);
	if (!sfc)
		return -ENOMEM;

	dev_set_drvdata(dev, sfc);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	sfc->regmap = devm_regmap_init_mmio(dev, regs,
					    &tegra210_sfc_regmap_config);
	if (IS_ERR(sfc->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(sfc->regmap);
	}

	regcache_cache_only(sfc->regmap, true);

	err = devm_snd_soc_register_component(dev, &tegra210_sfc_cmpnt,
					      tegra210_sfc_dais,
					      ARRAY_SIZE(tegra210_sfc_dais));
	if (err) {
		dev_err(dev, "can't register SFC component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int tegra210_sfc_platform_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tegra210_sfc_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra210_sfc_runtime_suspend,
			   tegra210_sfc_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
};

static struct platform_driver tegra210_sfc_driver = {
	.driver = {
		.name = "tegra210-sfc",
		.of_match_table = tegra210_sfc_of_match,
		.pm = &tegra210_sfc_pm_ops,
	},
	.probe = tegra210_sfc_platform_probe,
	.remove = tegra210_sfc_platform_remove,
};
module_platform_driver(tegra210_sfc_driver)

MODULE_AUTHOR("Arun Shamanna Lakshmi <aruns@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 SFC ASoC driver");
MODULE_LICENSE("GPL v2");
