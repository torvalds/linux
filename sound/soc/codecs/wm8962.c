/*
 * wm8962.c  --  WM8962 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
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
#include <linux/gcd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm8962.h>
#include <trace/events/asoc.h>

#include "wm8962.h"

#define WM8962_NUM_SUPPLIES 8
static const char *wm8962_supply_names[WM8962_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD",
	"CPVDD",
	"MICVDD",
	"PLLVDD",
	"SPKVDD1",
	"SPKVDD2",
};

/* codec private data */
struct wm8962_priv {
	struct snd_soc_codec *codec;

	int sysclk;
	int sysclk_rate;

	int bclk;  /* Desired BCLK */
	int lrclk;

	struct completion fll_lock;
	int fll_src;
	int fll_fref;
	int fll_fout;

	struct delayed_work mic_work;
	struct snd_soc_jack *jack;

	struct regulator_bulk_data supplies[WM8962_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8962_NUM_SUPPLIES];

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
	struct input_dev *beep;
	struct work_struct beep_work;
	int beep_rate;
#endif

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

/* We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8962_REGULATOR_EVENT(n) \
static int wm8962_regulator_event_##n(struct notifier_block *nb, \
				    unsigned long event, void *data)	\
{ \
	struct wm8962_priv *wm8962 = container_of(nb, struct wm8962_priv, \
						  disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		wm8962->codec->cache_sync = 1; \
	} \
	return 0; \
}

WM8962_REGULATOR_EVENT(0)
WM8962_REGULATOR_EVENT(1)
WM8962_REGULATOR_EVENT(2)
WM8962_REGULATOR_EVENT(3)
WM8962_REGULATOR_EVENT(4)
WM8962_REGULATOR_EVENT(5)
WM8962_REGULATOR_EVENT(6)
WM8962_REGULATOR_EVENT(7)

static const u16 wm8962_reg[WM8962_MAX_REGISTER + 1] = {
	[0] = 0x009F,     /* R0     - Left Input volume */
	[1] = 0x049F,     /* R1     - Right Input volume */
	[2] = 0x0000,     /* R2     - HPOUTL volume */
	[3] = 0x0000,     /* R3     - HPOUTR volume */
	[4] = 0x0020,     /* R4     - Clocking1 */
	[5] = 0x0018,     /* R5     - ADC & DAC Control 1 */
	[6] = 0x2008,     /* R6     - ADC & DAC Control 2 */
	[7] = 0x000A,     /* R7     - Audio Interface 0 */
	[8] = 0x01E4,     /* R8     - Clocking2 */
	[9] = 0x0300,     /* R9     - Audio Interface 1 */
	[10] = 0x00C0,    /* R10    - Left DAC volume */
	[11] = 0x00C0,    /* R11    - Right DAC volume */

	[14] = 0x0040,     /* R14    - Audio Interface 2 */
	[15] = 0x6243,     /* R15    - Software Reset */

	[17] = 0x007B,     /* R17    - ALC1 */
	[18] = 0x0000,     /* R18    - ALC2 */
	[19] = 0x1C32,     /* R19    - ALC3 */
	[20] = 0x3200,     /* R20    - Noise Gate */
	[21] = 0x00C0,     /* R21    - Left ADC volume */
	[22] = 0x00C0,     /* R22    - Right ADC volume */
	[23] = 0x0160,     /* R23    - Additional control(1) */
	[24] = 0x0000,     /* R24    - Additional control(2) */
	[25] = 0x0000,     /* R25    - Pwr Mgmt (1) */
	[26] = 0x0000,     /* R26    - Pwr Mgmt (2) */
	[27] = 0x0010,     /* R27    - Additional Control (3) */
	[28] = 0x0000,     /* R28    - Anti-pop */

	[30] = 0x005E,     /* R30    - Clocking 3 */
	[31] = 0x0000,     /* R31    - Input mixer control (1) */
	[32] = 0x0145,     /* R32    - Left input mixer volume */
	[33] = 0x0145,     /* R33    - Right input mixer volume */
	[34] = 0x0009,     /* R34    - Input mixer control (2) */
	[35] = 0x0003,     /* R35    - Input bias control */
	[37] = 0x0008,     /* R37    - Left input PGA control */
	[38] = 0x0008,     /* R38    - Right input PGA control */

	[40] = 0x0000,     /* R40    - SPKOUTL volume */
	[41] = 0x0000,     /* R41    - SPKOUTR volume */

	[47] = 0x0000,     /* R47    - Thermal Shutdown Status */
	[48] = 0x8027,     /* R48    - Additional Control (4) */
	[49] = 0x0010,     /* R49    - Class D Control 1 */

	[51] = 0x0003,     /* R51    - Class D Control 2 */

	[56] = 0x0506,     /* R56    - Clocking 4 */
	[57] = 0x0000,     /* R57    - DAC DSP Mixing (1) */
	[58] = 0x0000,     /* R58    - DAC DSP Mixing (2) */

	[60] = 0x0300,     /* R60    - DC Servo 0 */
	[61] = 0x0300,     /* R61    - DC Servo 1 */

	[64] = 0x0810,     /* R64    - DC Servo 4 */

	[66] = 0x0000,     /* R66    - DC Servo 6 */

	[68] = 0x001B,     /* R68    - Analogue PGA Bias */
	[69] = 0x0000,     /* R69    - Analogue HP 0 */

	[71] = 0x01FB,     /* R71    - Analogue HP 2 */
	[72] = 0x0000,     /* R72    - Charge Pump 1 */

	[82] = 0x0004,     /* R82    - Charge Pump B */

	[87] = 0x0000,     /* R87    - Write Sequencer Control 1 */

	[90] = 0x0000,     /* R90    - Write Sequencer Control 2 */

	[93] = 0x0000,     /* R93    - Write Sequencer Control 3 */
	[94] = 0x0000,     /* R94    - Control Interface */

	[99] = 0x0000,     /* R99    - Mixer Enables */
	[100] = 0x0000,     /* R100   - Headphone Mixer (1) */
	[101] = 0x0000,     /* R101   - Headphone Mixer (2) */
	[102] = 0x013F,     /* R102   - Headphone Mixer (3) */
	[103] = 0x013F,     /* R103   - Headphone Mixer (4) */

	[105] = 0x0000,     /* R105   - Speaker Mixer (1) */
	[106] = 0x0000,     /* R106   - Speaker Mixer (2) */
	[107] = 0x013F,     /* R107   - Speaker Mixer (3) */
	[108] = 0x013F,     /* R108   - Speaker Mixer (4) */
	[109] = 0x0003,     /* R109   - Speaker Mixer (5) */
	[110] = 0x0002,     /* R110   - Beep Generator (1) */

	[115] = 0x0006,     /* R115   - Oscillator Trim (3) */
	[116] = 0x0026,     /* R116   - Oscillator Trim (4) */

	[119] = 0x0000,     /* R119   - Oscillator Trim (7) */

	[124] = 0x0011,     /* R124   - Analogue Clocking1 */
	[125] = 0x004B,     /* R125   - Analogue Clocking2 */
	[126] = 0x000D,     /* R126   - Analogue Clocking3 */
	[127] = 0x0000,     /* R127   - PLL Software Reset */

	[129] = 0x0000,     /* R129   - PLL2 */

	[131] = 0x0000,     /* R131   - PLL 4 */

	[136] = 0x0067,     /* R136   - PLL 9 */
	[137] = 0x001C,     /* R137   - PLL 10 */
	[138] = 0x0071,     /* R138   - PLL 11 */
	[139] = 0x00C7,     /* R139   - PLL 12 */
	[140] = 0x0067,     /* R140   - PLL 13 */
	[141] = 0x0048,     /* R141   - PLL 14 */
	[142] = 0x0022,     /* R142   - PLL 15 */
	[143] = 0x0097,     /* R143   - PLL 16 */

	[155] = 0x000C,     /* R155   - FLL Control (1) */
	[156] = 0x0039,     /* R156   - FLL Control (2) */
	[157] = 0x0180,     /* R157   - FLL Control (3) */

	[159] = 0x0032,     /* R159   - FLL Control (5) */
	[160] = 0x0018,     /* R160   - FLL Control (6) */
	[161] = 0x007D,     /* R161   - FLL Control (7) */
	[162] = 0x0008,     /* R162   - FLL Control (8) */

	[252] = 0x0005,     /* R252   - General test 1 */

	[256] = 0x0000,     /* R256   - DF1 */
	[257] = 0x0000,     /* R257   - DF2 */
	[258] = 0x0000,     /* R258   - DF3 */
	[259] = 0x0000,     /* R259   - DF4 */
	[260] = 0x0000,     /* R260   - DF5 */
	[261] = 0x0000,     /* R261   - DF6 */
	[262] = 0x0000,     /* R262   - DF7 */

	[264] = 0x0000,     /* R264   - LHPF1 */
	[265] = 0x0000,     /* R265   - LHPF2 */

	[268] = 0x0000,     /* R268   - THREED1 */
	[269] = 0x0000,     /* R269   - THREED2 */
	[270] = 0x0000,     /* R270   - THREED3 */
	[271] = 0x0000,     /* R271   - THREED4 */

	[276] = 0x000C,     /* R276   - DRC 1 */
	[277] = 0x0925,     /* R277   - DRC 2 */
	[278] = 0x0000,     /* R278   - DRC 3 */
	[279] = 0x0000,     /* R279   - DRC 4 */
	[280] = 0x0000,     /* R280   - DRC 5 */

	[285] = 0x0000,     /* R285   - Tloopback */

	[335] = 0x0004,     /* R335   - EQ1 */
	[336] = 0x6318,     /* R336   - EQ2 */
	[337] = 0x6300,     /* R337   - EQ3 */
	[338] = 0x0FCA,     /* R338   - EQ4 */
	[339] = 0x0400,     /* R339   - EQ5 */
	[340] = 0x00D8,     /* R340   - EQ6 */
	[341] = 0x1EB5,     /* R341   - EQ7 */
	[342] = 0xF145,     /* R342   - EQ8 */
	[343] = 0x0B75,     /* R343   - EQ9 */
	[344] = 0x01C5,     /* R344   - EQ10 */
	[345] = 0x1C58,     /* R345   - EQ11 */
	[346] = 0xF373,     /* R346   - EQ12 */
	[347] = 0x0A54,     /* R347   - EQ13 */
	[348] = 0x0558,     /* R348   - EQ14 */
	[349] = 0x168E,     /* R349   - EQ15 */
	[350] = 0xF829,     /* R350   - EQ16 */
	[351] = 0x07AD,     /* R351   - EQ17 */
	[352] = 0x1103,     /* R352   - EQ18 */
	[353] = 0x0564,     /* R353   - EQ19 */
	[354] = 0x0559,     /* R354   - EQ20 */
	[355] = 0x4000,     /* R355   - EQ21 */
	[356] = 0x6318,     /* R356   - EQ22 */
	[357] = 0x6300,     /* R357   - EQ23 */
	[358] = 0x0FCA,     /* R358   - EQ24 */
	[359] = 0x0400,     /* R359   - EQ25 */
	[360] = 0x00D8,     /* R360   - EQ26 */
	[361] = 0x1EB5,     /* R361   - EQ27 */
	[362] = 0xF145,     /* R362   - EQ28 */
	[363] = 0x0B75,     /* R363   - EQ29 */
	[364] = 0x01C5,     /* R364   - EQ30 */
	[365] = 0x1C58,     /* R365   - EQ31 */
	[366] = 0xF373,     /* R366   - EQ32 */
	[367] = 0x0A54,     /* R367   - EQ33 */
	[368] = 0x0558,     /* R368   - EQ34 */
	[369] = 0x168E,     /* R369   - EQ35 */
	[370] = 0xF829,     /* R370   - EQ36 */
	[371] = 0x07AD,     /* R371   - EQ37 */
	[372] = 0x1103,     /* R372   - EQ38 */
	[373] = 0x0564,     /* R373   - EQ39 */
	[374] = 0x0559,     /* R374   - EQ40 */
	[375] = 0x4000,     /* R375   - EQ41 */

	[513] = 0x0000,     /* R513   - GPIO 2 */
	[514] = 0x0000,     /* R514   - GPIO 3 */

	[516] = 0x8100,     /* R516   - GPIO 5 */
	[517] = 0x8100,     /* R517   - GPIO 6 */

	[560] = 0x0000,     /* R560   - Interrupt Status 1 */
	[561] = 0x0000,     /* R561   - Interrupt Status 2 */

	[568] = 0x0030,     /* R568   - Interrupt Status 1 Mask */
	[569] = 0xFFED,     /* R569   - Interrupt Status 2 Mask */

	[576] = 0x0000,     /* R576   - Interrupt Control */

	[584] = 0x002D,     /* R584   - IRQ Debounce */

	[586] = 0x0000,     /* R586   -  MICINT Source Pol */

	[768] = 0x1C00,     /* R768   - DSP2 Power Management */

	[1037] = 0x0000,     /* R1037  - DSP2_ExecControl */

	[8192] = 0x0000,     /* R8192  - DSP2 Instruction RAM 0 */

	[9216] = 0x0030,     /* R9216  - DSP2 Address RAM 2 */
	[9217] = 0x0000,     /* R9217  - DSP2 Address RAM 1 */
	[9218] = 0x0000,     /* R9218  - DSP2 Address RAM 0 */

	[12288] = 0x0000,     /* R12288 - DSP2 Data1 RAM 1 */
	[12289] = 0x0000,     /* R12289 - DSP2 Data1 RAM 0 */

	[13312] = 0x0000,     /* R13312 - DSP2 Data2 RAM 1 */
	[13313] = 0x0000,     /* R13313 - DSP2 Data2 RAM 0 */

	[14336] = 0x0000,     /* R14336 - DSP2 Data3 RAM 1 */
	[14337] = 0x0000,     /* R14337 - DSP2 Data3 RAM 0 */

	[15360] = 0x000A,     /* R15360 - DSP2 Coeff RAM 0 */

	[16384] = 0x0000,     /* R16384 - RETUNEADC_SHARED_COEFF_1 */
	[16385] = 0x0000,     /* R16385 - RETUNEADC_SHARED_COEFF_0 */
	[16386] = 0x0000,     /* R16386 - RETUNEDAC_SHARED_COEFF_1 */
	[16387] = 0x0000,     /* R16387 - RETUNEDAC_SHARED_COEFF_0 */
	[16388] = 0x0000,     /* R16388 - SOUNDSTAGE_ENABLES_1 */
	[16389] = 0x0000,     /* R16389 - SOUNDSTAGE_ENABLES_0 */

	[16896] = 0x0002,     /* R16896 - HDBASS_AI_1 */
	[16897] = 0xBD12,     /* R16897 - HDBASS_AI_0 */
	[16898] = 0x007C,     /* R16898 - HDBASS_AR_1 */
	[16899] = 0x586C,     /* R16899 - HDBASS_AR_0 */
	[16900] = 0x0053,     /* R16900 - HDBASS_B_1 */
	[16901] = 0x8121,     /* R16901 - HDBASS_B_0 */
	[16902] = 0x003F,     /* R16902 - HDBASS_K_1 */
	[16903] = 0x8BD8,     /* R16903 - HDBASS_K_0 */
	[16904] = 0x0032,     /* R16904 - HDBASS_N1_1 */
	[16905] = 0xF52D,     /* R16905 - HDBASS_N1_0 */
	[16906] = 0x0065,     /* R16906 - HDBASS_N2_1 */
	[16907] = 0xAC8C,     /* R16907 - HDBASS_N2_0 */
	[16908] = 0x006B,     /* R16908 - HDBASS_N3_1 */
	[16909] = 0xE087,     /* R16909 - HDBASS_N3_0 */
	[16910] = 0x0072,     /* R16910 - HDBASS_N4_1 */
	[16911] = 0x1483,     /* R16911 - HDBASS_N4_0 */
	[16912] = 0x0072,     /* R16912 - HDBASS_N5_1 */
	[16913] = 0x1483,     /* R16913 - HDBASS_N5_0 */
	[16914] = 0x0043,     /* R16914 - HDBASS_X1_1 */
	[16915] = 0x3525,     /* R16915 - HDBASS_X1_0 */
	[16916] = 0x0006,     /* R16916 - HDBASS_X2_1 */
	[16917] = 0x6A4A,     /* R16917 - HDBASS_X2_0 */
	[16918] = 0x0043,     /* R16918 - HDBASS_X3_1 */
	[16919] = 0x6079,     /* R16919 - HDBASS_X3_0 */
	[16920] = 0x0008,     /* R16920 - HDBASS_ATK_1 */
	[16921] = 0x0000,     /* R16921 - HDBASS_ATK_0 */
	[16922] = 0x0001,     /* R16922 - HDBASS_DCY_1 */
	[16923] = 0x0000,     /* R16923 - HDBASS_DCY_0 */
	[16924] = 0x0059,     /* R16924 - HDBASS_PG_1 */
	[16925] = 0x999A,     /* R16925 - HDBASS_PG_0 */

	[17048] = 0x0083,     /* R17408 - HPF_C_1 */
	[17049] = 0x98AD,     /* R17409 - HPF_C_0 */

	[17920] = 0x007F,     /* R17920 - ADCL_RETUNE_C1_1 */
	[17921] = 0xFFFF,     /* R17921 - ADCL_RETUNE_C1_0 */
	[17922] = 0x0000,     /* R17922 - ADCL_RETUNE_C2_1 */
	[17923] = 0x0000,     /* R17923 - ADCL_RETUNE_C2_0 */
	[17924] = 0x0000,     /* R17924 - ADCL_RETUNE_C3_1 */
	[17925] = 0x0000,     /* R17925 - ADCL_RETUNE_C3_0 */
	[17926] = 0x0000,     /* R17926 - ADCL_RETUNE_C4_1 */
	[17927] = 0x0000,     /* R17927 - ADCL_RETUNE_C4_0 */
	[17928] = 0x0000,     /* R17928 - ADCL_RETUNE_C5_1 */
	[17929] = 0x0000,     /* R17929 - ADCL_RETUNE_C5_0 */
	[17930] = 0x0000,     /* R17930 - ADCL_RETUNE_C6_1 */
	[17931] = 0x0000,     /* R17931 - ADCL_RETUNE_C6_0 */
	[17932] = 0x0000,     /* R17932 - ADCL_RETUNE_C7_1 */
	[17933] = 0x0000,     /* R17933 - ADCL_RETUNE_C7_0 */
	[17934] = 0x0000,     /* R17934 - ADCL_RETUNE_C8_1 */
	[17935] = 0x0000,     /* R17935 - ADCL_RETUNE_C8_0 */
	[17936] = 0x0000,     /* R17936 - ADCL_RETUNE_C9_1 */
	[17937] = 0x0000,     /* R17937 - ADCL_RETUNE_C9_0 */
	[17938] = 0x0000,     /* R17938 - ADCL_RETUNE_C10_1 */
	[17939] = 0x0000,     /* R17939 - ADCL_RETUNE_C10_0 */
	[17940] = 0x0000,     /* R17940 - ADCL_RETUNE_C11_1 */
	[17941] = 0x0000,     /* R17941 - ADCL_RETUNE_C11_0 */
	[17942] = 0x0000,     /* R17942 - ADCL_RETUNE_C12_1 */
	[17943] = 0x0000,     /* R17943 - ADCL_RETUNE_C12_0 */
	[17944] = 0x0000,     /* R17944 - ADCL_RETUNE_C13_1 */
	[17945] = 0x0000,     /* R17945 - ADCL_RETUNE_C13_0 */
	[17946] = 0x0000,     /* R17946 - ADCL_RETUNE_C14_1 */
	[17947] = 0x0000,     /* R17947 - ADCL_RETUNE_C14_0 */
	[17948] = 0x0000,     /* R17948 - ADCL_RETUNE_C15_1 */
	[17949] = 0x0000,     /* R17949 - ADCL_RETUNE_C15_0 */
	[17950] = 0x0000,     /* R17950 - ADCL_RETUNE_C16_1 */
	[17951] = 0x0000,     /* R17951 - ADCL_RETUNE_C16_0 */
	[17952] = 0x0000,     /* R17952 - ADCL_RETUNE_C17_1 */
	[17953] = 0x0000,     /* R17953 - ADCL_RETUNE_C17_0 */
	[17954] = 0x0000,     /* R17954 - ADCL_RETUNE_C18_1 */
	[17955] = 0x0000,     /* R17955 - ADCL_RETUNE_C18_0 */
	[17956] = 0x0000,     /* R17956 - ADCL_RETUNE_C19_1 */
	[17957] = 0x0000,     /* R17957 - ADCL_RETUNE_C19_0 */
	[17958] = 0x0000,     /* R17958 - ADCL_RETUNE_C20_1 */
	[17959] = 0x0000,     /* R17959 - ADCL_RETUNE_C20_0 */
	[17960] = 0x0000,     /* R17960 - ADCL_RETUNE_C21_1 */
	[17961] = 0x0000,     /* R17961 - ADCL_RETUNE_C21_0 */
	[17962] = 0x0000,     /* R17962 - ADCL_RETUNE_C22_1 */
	[17963] = 0x0000,     /* R17963 - ADCL_RETUNE_C22_0 */
	[17964] = 0x0000,     /* R17964 - ADCL_RETUNE_C23_1 */
	[17965] = 0x0000,     /* R17965 - ADCL_RETUNE_C23_0 */
	[17966] = 0x0000,     /* R17966 - ADCL_RETUNE_C24_1 */
	[17967] = 0x0000,     /* R17967 - ADCL_RETUNE_C24_0 */
	[17968] = 0x0000,     /* R17968 - ADCL_RETUNE_C25_1 */
	[17969] = 0x0000,     /* R17969 - ADCL_RETUNE_C25_0 */
	[17970] = 0x0000,     /* R17970 - ADCL_RETUNE_C26_1 */
	[17971] = 0x0000,     /* R17971 - ADCL_RETUNE_C26_0 */
	[17972] = 0x0000,     /* R17972 - ADCL_RETUNE_C27_1 */
	[17973] = 0x0000,     /* R17973 - ADCL_RETUNE_C27_0 */
	[17974] = 0x0000,     /* R17974 - ADCL_RETUNE_C28_1 */
	[17975] = 0x0000,     /* R17975 - ADCL_RETUNE_C28_0 */
	[17976] = 0x0000,     /* R17976 - ADCL_RETUNE_C29_1 */
	[17977] = 0x0000,     /* R17977 - ADCL_RETUNE_C29_0 */
	[17978] = 0x0000,     /* R17978 - ADCL_RETUNE_C30_1 */
	[17979] = 0x0000,     /* R17979 - ADCL_RETUNE_C30_0 */
	[17980] = 0x0000,     /* R17980 - ADCL_RETUNE_C31_1 */
	[17981] = 0x0000,     /* R17981 - ADCL_RETUNE_C31_0 */
	[17982] = 0x0000,     /* R17982 - ADCL_RETUNE_C32_1 */
	[17983] = 0x0000,     /* R17983 - ADCL_RETUNE_C32_0 */

	[18432] = 0x0020,     /* R18432 - RETUNEADC_PG2_1 */
	[18433] = 0x0000,     /* R18433 - RETUNEADC_PG2_0 */
	[18434] = 0x0040,     /* R18434 - RETUNEADC_PG_1 */
	[18435] = 0x0000,     /* R18435 - RETUNEADC_PG_0 */

	[18944] = 0x007F,     /* R18944 - ADCR_RETUNE_C1_1 */
	[18945] = 0xFFFF,     /* R18945 - ADCR_RETUNE_C1_0 */
	[18946] = 0x0000,     /* R18946 - ADCR_RETUNE_C2_1 */
	[18947] = 0x0000,     /* R18947 - ADCR_RETUNE_C2_0 */
	[18948] = 0x0000,     /* R18948 - ADCR_RETUNE_C3_1 */
	[18949] = 0x0000,     /* R18949 - ADCR_RETUNE_C3_0 */
	[18950] = 0x0000,     /* R18950 - ADCR_RETUNE_C4_1 */
	[18951] = 0x0000,     /* R18951 - ADCR_RETUNE_C4_0 */
	[18952] = 0x0000,     /* R18952 - ADCR_RETUNE_C5_1 */
	[18953] = 0x0000,     /* R18953 - ADCR_RETUNE_C5_0 */
	[18954] = 0x0000,     /* R18954 - ADCR_RETUNE_C6_1 */
	[18955] = 0x0000,     /* R18955 - ADCR_RETUNE_C6_0 */
	[18956] = 0x0000,     /* R18956 - ADCR_RETUNE_C7_1 */
	[18957] = 0x0000,     /* R18957 - ADCR_RETUNE_C7_0 */
	[18958] = 0x0000,     /* R18958 - ADCR_RETUNE_C8_1 */
	[18959] = 0x0000,     /* R18959 - ADCR_RETUNE_C8_0 */
	[18960] = 0x0000,     /* R18960 - ADCR_RETUNE_C9_1 */
	[18961] = 0x0000,     /* R18961 - ADCR_RETUNE_C9_0 */
	[18962] = 0x0000,     /* R18962 - ADCR_RETUNE_C10_1 */
	[18963] = 0x0000,     /* R18963 - ADCR_RETUNE_C10_0 */
	[18964] = 0x0000,     /* R18964 - ADCR_RETUNE_C11_1 */
	[18965] = 0x0000,     /* R18965 - ADCR_RETUNE_C11_0 */
	[18966] = 0x0000,     /* R18966 - ADCR_RETUNE_C12_1 */
	[18967] = 0x0000,     /* R18967 - ADCR_RETUNE_C12_0 */
	[18968] = 0x0000,     /* R18968 - ADCR_RETUNE_C13_1 */
	[18969] = 0x0000,     /* R18969 - ADCR_RETUNE_C13_0 */
	[18970] = 0x0000,     /* R18970 - ADCR_RETUNE_C14_1 */
	[18971] = 0x0000,     /* R18971 - ADCR_RETUNE_C14_0 */
	[18972] = 0x0000,     /* R18972 - ADCR_RETUNE_C15_1 */
	[18973] = 0x0000,     /* R18973 - ADCR_RETUNE_C15_0 */
	[18974] = 0x0000,     /* R18974 - ADCR_RETUNE_C16_1 */
	[18975] = 0x0000,     /* R18975 - ADCR_RETUNE_C16_0 */
	[18976] = 0x0000,     /* R18976 - ADCR_RETUNE_C17_1 */
	[18977] = 0x0000,     /* R18977 - ADCR_RETUNE_C17_0 */
	[18978] = 0x0000,     /* R18978 - ADCR_RETUNE_C18_1 */
	[18979] = 0x0000,     /* R18979 - ADCR_RETUNE_C18_0 */
	[18980] = 0x0000,     /* R18980 - ADCR_RETUNE_C19_1 */
	[18981] = 0x0000,     /* R18981 - ADCR_RETUNE_C19_0 */
	[18982] = 0x0000,     /* R18982 - ADCR_RETUNE_C20_1 */
	[18983] = 0x0000,     /* R18983 - ADCR_RETUNE_C20_0 */
	[18984] = 0x0000,     /* R18984 - ADCR_RETUNE_C21_1 */
	[18985] = 0x0000,     /* R18985 - ADCR_RETUNE_C21_0 */
	[18986] = 0x0000,     /* R18986 - ADCR_RETUNE_C22_1 */
	[18987] = 0x0000,     /* R18987 - ADCR_RETUNE_C22_0 */
	[18988] = 0x0000,     /* R18988 - ADCR_RETUNE_C23_1 */
	[18989] = 0x0000,     /* R18989 - ADCR_RETUNE_C23_0 */
	[18990] = 0x0000,     /* R18990 - ADCR_RETUNE_C24_1 */
	[18991] = 0x0000,     /* R18991 - ADCR_RETUNE_C24_0 */
	[18992] = 0x0000,     /* R18992 - ADCR_RETUNE_C25_1 */
	[18993] = 0x0000,     /* R18993 - ADCR_RETUNE_C25_0 */
	[18994] = 0x0000,     /* R18994 - ADCR_RETUNE_C26_1 */
	[18995] = 0x0000,     /* R18995 - ADCR_RETUNE_C26_0 */
	[18996] = 0x0000,     /* R18996 - ADCR_RETUNE_C27_1 */
	[18997] = 0x0000,     /* R18997 - ADCR_RETUNE_C27_0 */
	[18998] = 0x0000,     /* R18998 - ADCR_RETUNE_C28_1 */
	[18999] = 0x0000,     /* R18999 - ADCR_RETUNE_C28_0 */
	[19000] = 0x0000,     /* R19000 - ADCR_RETUNE_C29_1 */
	[19001] = 0x0000,     /* R19001 - ADCR_RETUNE_C29_0 */
	[19002] = 0x0000,     /* R19002 - ADCR_RETUNE_C30_1 */
	[19003] = 0x0000,     /* R19003 - ADCR_RETUNE_C30_0 */
	[19004] = 0x0000,     /* R19004 - ADCR_RETUNE_C31_1 */
	[19005] = 0x0000,     /* R19005 - ADCR_RETUNE_C31_0 */
	[19006] = 0x0000,     /* R19006 - ADCR_RETUNE_C32_1 */
	[19007] = 0x0000,     /* R19007 - ADCR_RETUNE_C32_0 */

	[19456] = 0x007F,     /* R19456 - DACL_RETUNE_C1_1 */
	[19457] = 0xFFFF,     /* R19457 - DACL_RETUNE_C1_0 */
	[19458] = 0x0000,     /* R19458 - DACL_RETUNE_C2_1 */
	[19459] = 0x0000,     /* R19459 - DACL_RETUNE_C2_0 */
	[19460] = 0x0000,     /* R19460 - DACL_RETUNE_C3_1 */
	[19461] = 0x0000,     /* R19461 - DACL_RETUNE_C3_0 */
	[19462] = 0x0000,     /* R19462 - DACL_RETUNE_C4_1 */
	[19463] = 0x0000,     /* R19463 - DACL_RETUNE_C4_0 */
	[19464] = 0x0000,     /* R19464 - DACL_RETUNE_C5_1 */
	[19465] = 0x0000,     /* R19465 - DACL_RETUNE_C5_0 */
	[19466] = 0x0000,     /* R19466 - DACL_RETUNE_C6_1 */
	[19467] = 0x0000,     /* R19467 - DACL_RETUNE_C6_0 */
	[19468] = 0x0000,     /* R19468 - DACL_RETUNE_C7_1 */
	[19469] = 0x0000,     /* R19469 - DACL_RETUNE_C7_0 */
	[19470] = 0x0000,     /* R19470 - DACL_RETUNE_C8_1 */
	[19471] = 0x0000,     /* R19471 - DACL_RETUNE_C8_0 */
	[19472] = 0x0000,     /* R19472 - DACL_RETUNE_C9_1 */
	[19473] = 0x0000,     /* R19473 - DACL_RETUNE_C9_0 */
	[19474] = 0x0000,     /* R19474 - DACL_RETUNE_C10_1 */
	[19475] = 0x0000,     /* R19475 - DACL_RETUNE_C10_0 */
	[19476] = 0x0000,     /* R19476 - DACL_RETUNE_C11_1 */
	[19477] = 0x0000,     /* R19477 - DACL_RETUNE_C11_0 */
	[19478] = 0x0000,     /* R19478 - DACL_RETUNE_C12_1 */
	[19479] = 0x0000,     /* R19479 - DACL_RETUNE_C12_0 */
	[19480] = 0x0000,     /* R19480 - DACL_RETUNE_C13_1 */
	[19481] = 0x0000,     /* R19481 - DACL_RETUNE_C13_0 */
	[19482] = 0x0000,     /* R19482 - DACL_RETUNE_C14_1 */
	[19483] = 0x0000,     /* R19483 - DACL_RETUNE_C14_0 */
	[19484] = 0x0000,     /* R19484 - DACL_RETUNE_C15_1 */
	[19485] = 0x0000,     /* R19485 - DACL_RETUNE_C15_0 */
	[19486] = 0x0000,     /* R19486 - DACL_RETUNE_C16_1 */
	[19487] = 0x0000,     /* R19487 - DACL_RETUNE_C16_0 */
	[19488] = 0x0000,     /* R19488 - DACL_RETUNE_C17_1 */
	[19489] = 0x0000,     /* R19489 - DACL_RETUNE_C17_0 */
	[19490] = 0x0000,     /* R19490 - DACL_RETUNE_C18_1 */
	[19491] = 0x0000,     /* R19491 - DACL_RETUNE_C18_0 */
	[19492] = 0x0000,     /* R19492 - DACL_RETUNE_C19_1 */
	[19493] = 0x0000,     /* R19493 - DACL_RETUNE_C19_0 */
	[19494] = 0x0000,     /* R19494 - DACL_RETUNE_C20_1 */
	[19495] = 0x0000,     /* R19495 - DACL_RETUNE_C20_0 */
	[19496] = 0x0000,     /* R19496 - DACL_RETUNE_C21_1 */
	[19497] = 0x0000,     /* R19497 - DACL_RETUNE_C21_0 */
	[19498] = 0x0000,     /* R19498 - DACL_RETUNE_C22_1 */
	[19499] = 0x0000,     /* R19499 - DACL_RETUNE_C22_0 */
	[19500] = 0x0000,     /* R19500 - DACL_RETUNE_C23_1 */
	[19501] = 0x0000,     /* R19501 - DACL_RETUNE_C23_0 */
	[19502] = 0x0000,     /* R19502 - DACL_RETUNE_C24_1 */
	[19503] = 0x0000,     /* R19503 - DACL_RETUNE_C24_0 */
	[19504] = 0x0000,     /* R19504 - DACL_RETUNE_C25_1 */
	[19505] = 0x0000,     /* R19505 - DACL_RETUNE_C25_0 */
	[19506] = 0x0000,     /* R19506 - DACL_RETUNE_C26_1 */
	[19507] = 0x0000,     /* R19507 - DACL_RETUNE_C26_0 */
	[19508] = 0x0000,     /* R19508 - DACL_RETUNE_C27_1 */
	[19509] = 0x0000,     /* R19509 - DACL_RETUNE_C27_0 */
	[19510] = 0x0000,     /* R19510 - DACL_RETUNE_C28_1 */
	[19511] = 0x0000,     /* R19511 - DACL_RETUNE_C28_0 */
	[19512] = 0x0000,     /* R19512 - DACL_RETUNE_C29_1 */
	[19513] = 0x0000,     /* R19513 - DACL_RETUNE_C29_0 */
	[19514] = 0x0000,     /* R19514 - DACL_RETUNE_C30_1 */
	[19515] = 0x0000,     /* R19515 - DACL_RETUNE_C30_0 */
	[19516] = 0x0000,     /* R19516 - DACL_RETUNE_C31_1 */
	[19517] = 0x0000,     /* R19517 - DACL_RETUNE_C31_0 */
	[19518] = 0x0000,     /* R19518 - DACL_RETUNE_C32_1 */
	[19519] = 0x0000,     /* R19519 - DACL_RETUNE_C32_0 */

	[19968] = 0x0020,     /* R19968 - RETUNEDAC_PG2_1 */
	[19969] = 0x0000,     /* R19969 - RETUNEDAC_PG2_0 */
	[19970] = 0x0040,     /* R19970 - RETUNEDAC_PG_1 */
	[19971] = 0x0000,     /* R19971 - RETUNEDAC_PG_0 */

	[20480] = 0x007F,     /* R20480 - DACR_RETUNE_C1_1 */
	[20481] = 0xFFFF,     /* R20481 - DACR_RETUNE_C1_0 */
	[20482] = 0x0000,     /* R20482 - DACR_RETUNE_C2_1 */
	[20483] = 0x0000,     /* R20483 - DACR_RETUNE_C2_0 */
	[20484] = 0x0000,     /* R20484 - DACR_RETUNE_C3_1 */
	[20485] = 0x0000,     /* R20485 - DACR_RETUNE_C3_0 */
	[20486] = 0x0000,     /* R20486 - DACR_RETUNE_C4_1 */
	[20487] = 0x0000,     /* R20487 - DACR_RETUNE_C4_0 */
	[20488] = 0x0000,     /* R20488 - DACR_RETUNE_C5_1 */
	[20489] = 0x0000,     /* R20489 - DACR_RETUNE_C5_0 */
	[20490] = 0x0000,     /* R20490 - DACR_RETUNE_C6_1 */
	[20491] = 0x0000,     /* R20491 - DACR_RETUNE_C6_0 */
	[20492] = 0x0000,     /* R20492 - DACR_RETUNE_C7_1 */
	[20493] = 0x0000,     /* R20493 - DACR_RETUNE_C7_0 */
	[20494] = 0x0000,     /* R20494 - DACR_RETUNE_C8_1 */
	[20495] = 0x0000,     /* R20495 - DACR_RETUNE_C8_0 */
	[20496] = 0x0000,     /* R20496 - DACR_RETUNE_C9_1 */
	[20497] = 0x0000,     /* R20497 - DACR_RETUNE_C9_0 */
	[20498] = 0x0000,     /* R20498 - DACR_RETUNE_C10_1 */
	[20499] = 0x0000,     /* R20499 - DACR_RETUNE_C10_0 */
	[20500] = 0x0000,     /* R20500 - DACR_RETUNE_C11_1 */
	[20501] = 0x0000,     /* R20501 - DACR_RETUNE_C11_0 */
	[20502] = 0x0000,     /* R20502 - DACR_RETUNE_C12_1 */
	[20503] = 0x0000,     /* R20503 - DACR_RETUNE_C12_0 */
	[20504] = 0x0000,     /* R20504 - DACR_RETUNE_C13_1 */
	[20505] = 0x0000,     /* R20505 - DACR_RETUNE_C13_0 */
	[20506] = 0x0000,     /* R20506 - DACR_RETUNE_C14_1 */
	[20507] = 0x0000,     /* R20507 - DACR_RETUNE_C14_0 */
	[20508] = 0x0000,     /* R20508 - DACR_RETUNE_C15_1 */
	[20509] = 0x0000,     /* R20509 - DACR_RETUNE_C15_0 */
	[20510] = 0x0000,     /* R20510 - DACR_RETUNE_C16_1 */
	[20511] = 0x0000,     /* R20511 - DACR_RETUNE_C16_0 */
	[20512] = 0x0000,     /* R20512 - DACR_RETUNE_C17_1 */
	[20513] = 0x0000,     /* R20513 - DACR_RETUNE_C17_0 */
	[20514] = 0x0000,     /* R20514 - DACR_RETUNE_C18_1 */
	[20515] = 0x0000,     /* R20515 - DACR_RETUNE_C18_0 */
	[20516] = 0x0000,     /* R20516 - DACR_RETUNE_C19_1 */
	[20517] = 0x0000,     /* R20517 - DACR_RETUNE_C19_0 */
	[20518] = 0x0000,     /* R20518 - DACR_RETUNE_C20_1 */
	[20519] = 0x0000,     /* R20519 - DACR_RETUNE_C20_0 */
	[20520] = 0x0000,     /* R20520 - DACR_RETUNE_C21_1 */
	[20521] = 0x0000,     /* R20521 - DACR_RETUNE_C21_0 */
	[20522] = 0x0000,     /* R20522 - DACR_RETUNE_C22_1 */
	[20523] = 0x0000,     /* R20523 - DACR_RETUNE_C22_0 */
	[20524] = 0x0000,     /* R20524 - DACR_RETUNE_C23_1 */
	[20525] = 0x0000,     /* R20525 - DACR_RETUNE_C23_0 */
	[20526] = 0x0000,     /* R20526 - DACR_RETUNE_C24_1 */
	[20527] = 0x0000,     /* R20527 - DACR_RETUNE_C24_0 */
	[20528] = 0x0000,     /* R20528 - DACR_RETUNE_C25_1 */
	[20529] = 0x0000,     /* R20529 - DACR_RETUNE_C25_0 */
	[20530] = 0x0000,     /* R20530 - DACR_RETUNE_C26_1 */
	[20531] = 0x0000,     /* R20531 - DACR_RETUNE_C26_0 */
	[20532] = 0x0000,     /* R20532 - DACR_RETUNE_C27_1 */
	[20533] = 0x0000,     /* R20533 - DACR_RETUNE_C27_0 */
	[20534] = 0x0000,     /* R20534 - DACR_RETUNE_C28_1 */
	[20535] = 0x0000,     /* R20535 - DACR_RETUNE_C28_0 */
	[20536] = 0x0000,     /* R20536 - DACR_RETUNE_C29_1 */
	[20537] = 0x0000,     /* R20537 - DACR_RETUNE_C29_0 */
	[20538] = 0x0000,     /* R20538 - DACR_RETUNE_C30_1 */
	[20539] = 0x0000,     /* R20539 - DACR_RETUNE_C30_0 */
	[20540] = 0x0000,     /* R20540 - DACR_RETUNE_C31_1 */
	[20541] = 0x0000,     /* R20541 - DACR_RETUNE_C31_0 */
	[20542] = 0x0000,     /* R20542 - DACR_RETUNE_C32_1 */
	[20543] = 0x0000,     /* R20543 - DACR_RETUNE_C32_0 */

	[20992] = 0x008C,     /* R20992 - VSS_XHD2_1 */
	[20993] = 0x0200,     /* R20993 - VSS_XHD2_0 */
	[20994] = 0x0035,     /* R20994 - VSS_XHD3_1 */
	[20995] = 0x0700,     /* R20995 - VSS_XHD3_0 */
	[20996] = 0x003A,     /* R20996 - VSS_XHN1_1 */
	[20997] = 0x4100,     /* R20997 - VSS_XHN1_0 */
	[20998] = 0x008B,     /* R20998 - VSS_XHN2_1 */
	[20999] = 0x7D00,     /* R20999 - VSS_XHN2_0 */
	[21000] = 0x003A,     /* R21000 - VSS_XHN3_1 */
	[21001] = 0x4100,     /* R21001 - VSS_XHN3_0 */
	[21002] = 0x008C,     /* R21002 - VSS_XLA_1 */
	[21003] = 0xFEE8,     /* R21003 - VSS_XLA_0 */
	[21004] = 0x0078,     /* R21004 - VSS_XLB_1 */
	[21005] = 0x0000,     /* R21005 - VSS_XLB_0 */
	[21006] = 0x003F,     /* R21006 - VSS_XLG_1 */
	[21007] = 0xB260,     /* R21007 - VSS_XLG_0 */
	[21008] = 0x002D,     /* R21008 - VSS_PG2_1 */
	[21009] = 0x1818,     /* R21009 - VSS_PG2_0 */
	[21010] = 0x0020,     /* R21010 - VSS_PG_1 */
	[21011] = 0x0000,     /* R21011 - VSS_PG_0 */
	[21012] = 0x00F1,     /* R21012 - VSS_XTD1_1 */
	[21013] = 0x8340,     /* R21013 - VSS_XTD1_0 */
	[21014] = 0x00FB,     /* R21014 - VSS_XTD2_1 */
	[21015] = 0x8300,     /* R21015 - VSS_XTD2_0 */
	[21016] = 0x00EE,     /* R21016 - VSS_XTD3_1 */
	[21017] = 0xAEC0,     /* R21017 - VSS_XTD3_0 */
	[21018] = 0x00FB,     /* R21018 - VSS_XTD4_1 */
	[21019] = 0xAC40,     /* R21019 - VSS_XTD4_0 */
	[21020] = 0x00F1,     /* R21020 - VSS_XTD5_1 */
	[21021] = 0x7F80,     /* R21021 - VSS_XTD5_0 */
	[21022] = 0x00F4,     /* R21022 - VSS_XTD6_1 */
	[21023] = 0x3B40,     /* R21023 - VSS_XTD6_0 */
	[21024] = 0x00F5,     /* R21024 - VSS_XTD7_1 */
	[21025] = 0xFB00,     /* R21025 - VSS_XTD7_0 */
	[21026] = 0x00EA,     /* R21026 - VSS_XTD8_1 */
	[21027] = 0x10C0,     /* R21027 - VSS_XTD8_0 */
	[21028] = 0x00FC,     /* R21028 - VSS_XTD9_1 */
	[21029] = 0xC580,     /* R21029 - VSS_XTD9_0 */
	[21030] = 0x00E2,     /* R21030 - VSS_XTD10_1 */
	[21031] = 0x75C0,     /* R21031 - VSS_XTD10_0 */
	[21032] = 0x0004,     /* R21032 - VSS_XTD11_1 */
	[21033] = 0xB480,     /* R21033 - VSS_XTD11_0 */
	[21034] = 0x00D4,     /* R21034 - VSS_XTD12_1 */
	[21035] = 0xF980,     /* R21035 - VSS_XTD12_0 */
	[21036] = 0x0004,     /* R21036 - VSS_XTD13_1 */
	[21037] = 0x9140,     /* R21037 - VSS_XTD13_0 */
	[21038] = 0x00D8,     /* R21038 - VSS_XTD14_1 */
	[21039] = 0xA480,     /* R21039 - VSS_XTD14_0 */
	[21040] = 0x0002,     /* R21040 - VSS_XTD15_1 */
	[21041] = 0x3DC0,     /* R21041 - VSS_XTD15_0 */
	[21042] = 0x00CF,     /* R21042 - VSS_XTD16_1 */
	[21043] = 0x7A80,     /* R21043 - VSS_XTD16_0 */
	[21044] = 0x00DC,     /* R21044 - VSS_XTD17_1 */
	[21045] = 0x0600,     /* R21045 - VSS_XTD17_0 */
	[21046] = 0x00F2,     /* R21046 - VSS_XTD18_1 */
	[21047] = 0xDAC0,     /* R21047 - VSS_XTD18_0 */
	[21048] = 0x00BA,     /* R21048 - VSS_XTD19_1 */
	[21049] = 0xF340,     /* R21049 - VSS_XTD19_0 */
	[21050] = 0x000A,     /* R21050 - VSS_XTD20_1 */
	[21051] = 0x7940,     /* R21051 - VSS_XTD20_0 */
	[21052] = 0x001C,     /* R21052 - VSS_XTD21_1 */
	[21053] = 0x0680,     /* R21053 - VSS_XTD21_0 */
	[21054] = 0x00FD,     /* R21054 - VSS_XTD22_1 */
	[21055] = 0x2D00,     /* R21055 - VSS_XTD22_0 */
	[21056] = 0x001C,     /* R21056 - VSS_XTD23_1 */
	[21057] = 0xE840,     /* R21057 - VSS_XTD23_0 */
	[21058] = 0x000D,     /* R21058 - VSS_XTD24_1 */
	[21059] = 0xDC40,     /* R21059 - VSS_XTD24_0 */
	[21060] = 0x00FC,     /* R21060 - VSS_XTD25_1 */
	[21061] = 0x9D00,     /* R21061 - VSS_XTD25_0 */
	[21062] = 0x0009,     /* R21062 - VSS_XTD26_1 */
	[21063] = 0x5580,     /* R21063 - VSS_XTD26_0 */
	[21064] = 0x00FE,     /* R21064 - VSS_XTD27_1 */
	[21065] = 0x7E80,     /* R21065 - VSS_XTD27_0 */
	[21066] = 0x000E,     /* R21066 - VSS_XTD28_1 */
	[21067] = 0xAB40,     /* R21067 - VSS_XTD28_0 */
	[21068] = 0x00F9,     /* R21068 - VSS_XTD29_1 */
	[21069] = 0x9880,     /* R21069 - VSS_XTD29_0 */
	[21070] = 0x0009,     /* R21070 - VSS_XTD30_1 */
	[21071] = 0x87C0,     /* R21071 - VSS_XTD30_0 */
	[21072] = 0x00FD,     /* R21072 - VSS_XTD31_1 */
	[21073] = 0x2C40,     /* R21073 - VSS_XTD31_0 */
	[21074] = 0x0009,     /* R21074 - VSS_XTD32_1 */
	[21075] = 0x4800,     /* R21075 - VSS_XTD32_0 */
	[21076] = 0x0003,     /* R21076 - VSS_XTS1_1 */
	[21077] = 0x5F40,     /* R21077 - VSS_XTS1_0 */
	[21078] = 0x0000,     /* R21078 - VSS_XTS2_1 */
	[21079] = 0x8700,     /* R21079 - VSS_XTS2_0 */
	[21080] = 0x00FA,     /* R21080 - VSS_XTS3_1 */
	[21081] = 0xE4C0,     /* R21081 - VSS_XTS3_0 */
	[21082] = 0x0000,     /* R21082 - VSS_XTS4_1 */
	[21083] = 0x0B40,     /* R21083 - VSS_XTS4_0 */
	[21084] = 0x0004,     /* R21084 - VSS_XTS5_1 */
	[21085] = 0xE180,     /* R21085 - VSS_XTS5_0 */
	[21086] = 0x0001,     /* R21086 - VSS_XTS6_1 */
	[21087] = 0x1F40,     /* R21087 - VSS_XTS6_0 */
	[21088] = 0x00F8,     /* R21088 - VSS_XTS7_1 */
	[21089] = 0xB000,     /* R21089 - VSS_XTS7_0 */
	[21090] = 0x00FB,     /* R21090 - VSS_XTS8_1 */
	[21091] = 0xCBC0,     /* R21091 - VSS_XTS8_0 */
	[21092] = 0x0004,     /* R21092 - VSS_XTS9_1 */
	[21093] = 0xF380,     /* R21093 - VSS_XTS9_0 */
	[21094] = 0x0007,     /* R21094 - VSS_XTS10_1 */
	[21095] = 0xDF40,     /* R21095 - VSS_XTS10_0 */
	[21096] = 0x00FF,     /* R21096 - VSS_XTS11_1 */
	[21097] = 0x0700,     /* R21097 - VSS_XTS11_0 */
	[21098] = 0x00EF,     /* R21098 - VSS_XTS12_1 */
	[21099] = 0xD700,     /* R21099 - VSS_XTS12_0 */
	[21100] = 0x00FB,     /* R21100 - VSS_XTS13_1 */
	[21101] = 0xAF40,     /* R21101 - VSS_XTS13_0 */
	[21102] = 0x0010,     /* R21102 - VSS_XTS14_1 */
	[21103] = 0x8A80,     /* R21103 - VSS_XTS14_0 */
	[21104] = 0x0011,     /* R21104 - VSS_XTS15_1 */
	[21105] = 0x07C0,     /* R21105 - VSS_XTS15_0 */
	[21106] = 0x00E0,     /* R21106 - VSS_XTS16_1 */
	[21107] = 0x0800,     /* R21107 - VSS_XTS16_0 */
	[21108] = 0x00D2,     /* R21108 - VSS_XTS17_1 */
	[21109] = 0x7600,     /* R21109 - VSS_XTS17_0 */
	[21110] = 0x0020,     /* R21110 - VSS_XTS18_1 */
	[21111] = 0xCF40,     /* R21111 - VSS_XTS18_0 */
	[21112] = 0x0030,     /* R21112 - VSS_XTS19_1 */
	[21113] = 0x2340,     /* R21113 - VSS_XTS19_0 */
	[21114] = 0x00FD,     /* R21114 - VSS_XTS20_1 */
	[21115] = 0x69C0,     /* R21115 - VSS_XTS20_0 */
	[21116] = 0x0028,     /* R21116 - VSS_XTS21_1 */
	[21117] = 0x3500,     /* R21117 - VSS_XTS21_0 */
	[21118] = 0x0006,     /* R21118 - VSS_XTS22_1 */
	[21119] = 0x3300,     /* R21119 - VSS_XTS22_0 */
	[21120] = 0x00D9,     /* R21120 - VSS_XTS23_1 */
	[21121] = 0xF6C0,     /* R21121 - VSS_XTS23_0 */
	[21122] = 0x00F3,     /* R21122 - VSS_XTS24_1 */
	[21123] = 0x3340,     /* R21123 - VSS_XTS24_0 */
	[21124] = 0x000F,     /* R21124 - VSS_XTS25_1 */
	[21125] = 0x4200,     /* R21125 - VSS_XTS25_0 */
	[21126] = 0x0004,     /* R21126 - VSS_XTS26_1 */
	[21127] = 0x0C80,     /* R21127 - VSS_XTS26_0 */
	[21128] = 0x00FB,     /* R21128 - VSS_XTS27_1 */
	[21129] = 0x3F80,     /* R21129 - VSS_XTS27_0 */
	[21130] = 0x00F7,     /* R21130 - VSS_XTS28_1 */
	[21131] = 0x57C0,     /* R21131 - VSS_XTS28_0 */
	[21132] = 0x0003,     /* R21132 - VSS_XTS29_1 */
	[21133] = 0x5400,     /* R21133 - VSS_XTS29_0 */
	[21134] = 0x0000,     /* R21134 - VSS_XTS30_1 */
	[21135] = 0xC6C0,     /* R21135 - VSS_XTS30_0 */
	[21136] = 0x0003,     /* R21136 - VSS_XTS31_1 */
	[21137] = 0x12C0,     /* R21137 - VSS_XTS31_0 */
	[21138] = 0x00FD,     /* R21138 - VSS_XTS32_1 */
	[21139] = 0x8580,     /* R21139 - VSS_XTS32_0 */
};

static const struct wm8962_reg_access {
	u16 read;
	u16 write;
	u16 vol;
} wm8962_reg_access[WM8962_MAX_REGISTER + 1] = {
	[0] = { 0x00FF, 0x01FF, 0x0000 }, /* R0     - Left Input volume */
	[1] = { 0xFEFF, 0x01FF, 0xFFFF }, /* R1     - Right Input volume */
	[2] = { 0x00FF, 0x01FF, 0x0000 }, /* R2     - HPOUTL volume */
	[3] = { 0x00FF, 0x01FF, 0x0000 }, /* R3     - HPOUTR volume */
	[4] = { 0x07FE, 0x07FE, 0xFFFF }, /* R4     - Clocking1 */
	[5] = { 0x007F, 0x007F, 0x0000 }, /* R5     - ADC & DAC Control 1 */
	[6] = { 0x37ED, 0x37ED, 0x0000 }, /* R6     - ADC & DAC Control 2 */
	[7] = { 0x1FFF, 0x1FFF, 0x0000 }, /* R7     - Audio Interface 0 */
	[8] = { 0x0FEF, 0x0FEF, 0xFFFF }, /* R8     - Clocking2 */
	[9] = { 0x0B9F, 0x039F, 0x0000 }, /* R9     - Audio Interface 1 */
	[10] = { 0x00FF, 0x01FF, 0x0000 }, /* R10    - Left DAC volume */
	[11] = { 0x00FF, 0x01FF, 0x0000 }, /* R11    - Right DAC volume */
	[14] = { 0x07FF, 0x07FF, 0x0000 }, /* R14    - Audio Interface 2 */
	[15] = { 0xFFFF, 0xFFFF, 0xFFFF }, /* R15    - Software Reset */
	[17] = { 0x07FF, 0x07FF, 0x0000 }, /* R17    - ALC1 */
	[18] = { 0xF8FF, 0x00FF, 0xFFFF }, /* R18    - ALC2 */
	[19] = { 0x1DFF, 0x1DFF, 0x0000 }, /* R19    - ALC3 */
	[20] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20    - Noise Gate */
	[21] = { 0x00FF, 0x01FF, 0x0000 }, /* R21    - Left ADC volume */
	[22] = { 0x00FF, 0x01FF, 0x0000 }, /* R22    - Right ADC volume */
	[23] = { 0x0161, 0x0161, 0x0000 }, /* R23    - Additional control(1) */
	[24] = { 0x0008, 0x0008, 0x0000 }, /* R24    - Additional control(2) */
	[25] = { 0x07FE, 0x07FE, 0x0000 }, /* R25    - Pwr Mgmt (1) */
	[26] = { 0x01FB, 0x01FB, 0x0000 }, /* R26    - Pwr Mgmt (2) */
	[27] = { 0x0017, 0x0017, 0x0000 }, /* R27    - Additional Control (3) */
	[28] = { 0x001C, 0x001C, 0x0000 }, /* R28    - Anti-pop */

	[30] = { 0xFFFE, 0xFFFE, 0x0000 }, /* R30    - Clocking 3 */
	[31] = { 0x000F, 0x000F, 0x0000 }, /* R31    - Input mixer control (1) */
	[32] = { 0x01FF, 0x01FF, 0x0000 }, /* R32    - Left input mixer volume */
	[33] = { 0x01FF, 0x01FF, 0x0000 }, /* R33    - Right input mixer volume */
	[34] = { 0x003F, 0x003F, 0x0000 }, /* R34    - Input mixer control (2) */
	[35] = { 0x003F, 0x003F, 0x0000 }, /* R35    - Input bias control */
	[37] = { 0x001F, 0x001F, 0x0000 }, /* R37    - Left input PGA control */
	[38] = { 0x001F, 0x001F, 0x0000 }, /* R38    - Right input PGA control */
	[40] = { 0x00FF, 0x01FF, 0x0000 }, /* R40    - SPKOUTL volume */
	[41] = { 0x00FF, 0x01FF, 0x0000 }, /* R41    - SPKOUTR volume */

	[47] = { 0x000F, 0x0000, 0x0000 }, /* R47    - Thermal Shutdown Status */
	[48] = { 0x7EC7, 0x7E07, 0xFFFF }, /* R48    - Additional Control (4) */
	[49] = { 0x00D3, 0x00D7, 0xFFFF }, /* R49    - Class D Control 1 */
	[51] = { 0x0047, 0x0047, 0x0000 }, /* R51    - Class D Control 2 */
	[56] = { 0x001E, 0x001E, 0x0000 }, /* R56    - Clocking 4 */
	[57] = { 0x02FC, 0x02FC, 0x0000 }, /* R57    - DAC DSP Mixing (1) */
	[58] = { 0x00FC, 0x00FC, 0x0000 }, /* R58    - DAC DSP Mixing (2) */
	[60] = { 0x00CC, 0x00CC, 0x0000 }, /* R60    - DC Servo 0 */
	[61] = { 0x00DD, 0x00DD, 0x0000 }, /* R61    - DC Servo 1 */
	[64] = { 0x3F80, 0x3F80, 0x0000 }, /* R64    - DC Servo 4 */
	[66] = { 0x0780, 0x0000, 0xFFFF }, /* R66    - DC Servo 6 */
	[68] = { 0x0007, 0x0007, 0x0000 }, /* R68    - Analogue PGA Bias */
	[69] = { 0x00FF, 0x00FF, 0x0000 }, /* R69    - Analogue HP 0 */
	[71] = { 0x01FF, 0x01FF, 0x0000 }, /* R71    - Analogue HP 2 */
	[72] = { 0x0001, 0x0001, 0x0000 }, /* R72    - Charge Pump 1 */
	[82] = { 0x0001, 0x0001, 0x0000 }, /* R82    - Charge Pump B */
	[87] = { 0x00A0, 0x00A0, 0x0000 }, /* R87    - Write Sequencer Control 1 */
	[90] = { 0x007F, 0x01FF, 0x0000 }, /* R90    - Write Sequencer Control 2 */
	[93] = { 0x03F9, 0x0000, 0x0000 }, /* R93    - Write Sequencer Control 3 */
	[94] = { 0x0070, 0x0070, 0x0000 }, /* R94    - Control Interface */
	[99] = { 0x000F, 0x000F, 0x0000 }, /* R99    - Mixer Enables */
	[100] = { 0x00BF, 0x00BF, 0x0000 }, /* R100   - Headphone Mixer (1) */
	[101] = { 0x00BF, 0x00BF, 0x0000 }, /* R101   - Headphone Mixer (2) */
	[102] = { 0x01FF, 0x01FF, 0x0000 }, /* R102   - Headphone Mixer (3) */
	[103] = { 0x01FF, 0x01FF, 0x0000 }, /* R103   - Headphone Mixer (4) */
	[105] = { 0x00BF, 0x00BF, 0x0000 }, /* R105   - Speaker Mixer (1) */
	[106] = { 0x00BF, 0x00BF, 0x0000 }, /* R106   - Speaker Mixer (2) */
	[107] = { 0x01FF, 0x01FF, 0x0000 }, /* R107   - Speaker Mixer (3) */
	[108] = { 0x01FF, 0x01FF, 0x0000 }, /* R108   - Speaker Mixer (4) */
	[109] = { 0x00F0, 0x00F0, 0x0000 }, /* R109   - Speaker Mixer (5) */
	[110] = { 0x00F7, 0x00F7, 0x0000 }, /* R110   - Beep Generator (1) */
	[115] = { 0x001F, 0x001F, 0x0000 }, /* R115   - Oscillator Trim (3) */
	[116] = { 0x001F, 0x001F, 0x0000 }, /* R116   - Oscillator Trim (4) */
	[119] = { 0x00FF, 0x00FF, 0x0000 }, /* R119   - Oscillator Trim (7) */
	[124] = { 0x0079, 0x0079, 0x0000 }, /* R124   - Analogue Clocking1 */
	[125] = { 0x00DF, 0x00DF, 0x0000 }, /* R125   - Analogue Clocking2 */
	[126] = { 0x000D, 0x000D, 0x0000 }, /* R126   - Analogue Clocking3 */
	[127] = { 0x0000, 0xFFFF, 0x0000 }, /* R127   - PLL Software Reset */
	[129] = { 0x00B0, 0x00B0, 0x0000 }, /* R129   - PLL2 */
	[131] = { 0x0003, 0x0003, 0x0000 }, /* R131   - PLL 4 */
	[136] = { 0x005F, 0x005F, 0x0000 }, /* R136   - PLL 9 */
	[137] = { 0x00FF, 0x00FF, 0x0000 }, /* R137   - PLL 10 */
	[138] = { 0x00FF, 0x00FF, 0x0000 }, /* R138   - PLL 11 */
	[139] = { 0x00FF, 0x00FF, 0x0000 }, /* R139   - PLL 12 */
	[140] = { 0x005F, 0x005F, 0x0000 }, /* R140   - PLL 13 */
	[141] = { 0x00FF, 0x00FF, 0x0000 }, /* R141   - PLL 14 */
	[142] = { 0x00FF, 0x00FF, 0x0000 }, /* R142   - PLL 15 */
	[143] = { 0x00FF, 0x00FF, 0x0000 }, /* R143   - PLL 16 */
	[155] = { 0x0067, 0x0067, 0x0000 }, /* R155   - FLL Control (1) */
	[156] = { 0x01FB, 0x01FB, 0x0000 }, /* R156   - FLL Control (2) */
	[157] = { 0x0007, 0x0007, 0x0000 }, /* R157   - FLL Control (3) */
	[159] = { 0x007F, 0x007F, 0x0000 }, /* R159   - FLL Control (5) */
	[160] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R160   - FLL Control (6) */
	[161] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R161   - FLL Control (7) */
	[162] = { 0x03FF, 0x03FF, 0x0000 }, /* R162   - FLL Control (8) */
	[252] = { 0x0005, 0x0005, 0x0000 }, /* R252   - General test 1 */
	[256] = { 0x000F, 0x000F, 0x0000 }, /* R256   - DF1 */
	[257] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R257   - DF2 */
	[258] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R258   - DF3 */
	[259] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R259   - DF4 */
	[260] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R260   - DF5 */
	[261] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R261   - DF6 */
	[262] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R262   - DF7 */
	[264] = { 0x0003, 0x0003, 0x0000 }, /* R264   - LHPF1 */
	[265] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R265   - LHPF2 */
	[268] = { 0x0077, 0x0077, 0x0000 }, /* R268   - THREED1 */
	[269] = { 0xFFFC, 0xFFFC, 0x0000 }, /* R269   - THREED2 */
	[270] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R270   - THREED3 */
	[271] = { 0xFFFC, 0xFFFC, 0x0000 }, /* R271   - THREED4 */
	[276] = { 0x7FFF, 0x7FFF, 0x0000 }, /* R276   - DRC 1 */
	[277] = { 0x1FFF, 0x1FFF, 0x0000 }, /* R277   - DRC 2 */
	[278] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R278   - DRC 3 */
	[279] = { 0x07FF, 0x07FF, 0x0000 }, /* R279   - DRC 4 */
	[280] = { 0x03FF, 0x03FF, 0x0000 }, /* R280   - DRC 5 */
	[285] = { 0x0003, 0x0003, 0x0000 }, /* R285   - Tloopback */
	[335] = { 0x0007, 0x0007, 0x0000 }, /* R335   - EQ1 */
	[336] = { 0xFFFE, 0xFFFE, 0x0000 }, /* R336   - EQ2 */
	[337] = { 0xFFC0, 0xFFC0, 0x0000 }, /* R337   - EQ3 */
	[338] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R338   - EQ4 */
	[339] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R339   - EQ5 */
	[340] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R340   - EQ6 */
	[341] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R341   - EQ7 */
	[342] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R342   - EQ8 */
	[343] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R343   - EQ9 */
	[344] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R344   - EQ10 */
	[345] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R345   - EQ11 */
	[346] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R346   - EQ12 */
	[347] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R347   - EQ13 */
	[348] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R348   - EQ14 */
	[349] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R349   - EQ15 */
	[350] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R350   - EQ16 */
	[351] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R351   - EQ17 */
	[352] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R352   - EQ18 */
	[353] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R353   - EQ19 */
	[354] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R354   - EQ20 */
	[355] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R355   - EQ21 */
	[356] = { 0xFFFE, 0xFFFE, 0x0000 }, /* R356   - EQ22 */
	[357] = { 0xFFC0, 0xFFC0, 0x0000 }, /* R357   - EQ23 */
	[358] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R358   - EQ24 */
	[359] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R359   - EQ25 */
	[360] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R360   - EQ26 */
	[361] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R361   - EQ27 */
	[362] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R362   - EQ28 */
	[363] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R363   - EQ29 */
	[364] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R364   - EQ30 */
	[365] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R365   - EQ31 */
	[366] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R366   - EQ32 */
	[367] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R367   - EQ33 */
	[368] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R368   - EQ34 */
	[369] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R369   - EQ35 */
	[370] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R370   - EQ36 */
	[371] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R371   - EQ37 */
	[372] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R372   - EQ38 */
	[373] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R373   - EQ39 */
	[374] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R374   - EQ40 */
	[375] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R375   - EQ41 */
	[513] = { 0x045F, 0x045F, 0x0000 }, /* R513   - GPIO 2 */
	[514] = { 0x045F, 0x045F, 0x0000 }, /* R514   - GPIO 3 */
	[516] = { 0xE75F, 0xE75F, 0x0000 }, /* R516   - GPIO 5 */
	[517] = { 0xE75F, 0xE75F, 0x0000 }, /* R517   - GPIO 6 */
	[560] = { 0x0030, 0x0030, 0xFFFF }, /* R560   - Interrupt Status 1 */
	[561] = { 0xFFED, 0xFFED, 0xFFFF }, /* R561   - Interrupt Status 2 */
	[568] = { 0x0030, 0x0030, 0x0000 }, /* R568   - Interrupt Status 1 Mask */
	[569] = { 0xFFED, 0xFFED, 0x0000 }, /* R569   - Interrupt Status 2 Mask */
	[576] = { 0x0001, 0x0001, 0x0000 }, /* R576   - Interrupt Control */
	[584] = { 0x002D, 0x002D, 0x0000 }, /* R584   - IRQ Debounce */
	[586] = { 0xC000, 0xC000, 0x0000 }, /* R586   -  MICINT Source Pol */
	[768] = { 0x0001, 0x0001, 0x0000 }, /* R768   - DSP2 Power Management */
	[1037] = { 0x0000, 0x003F, 0x0000 }, /* R1037  - DSP2_ExecControl */
	[4096] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4096  - Write Sequencer 0 */
	[4097] = { 0x00FF, 0x00FF, 0x0000 }, /* R4097  - Write Sequencer 1 */
	[4098] = { 0x070F, 0x070F, 0x0000 }, /* R4098  - Write Sequencer 2 */
	[4099] = { 0x010F, 0x010F, 0x0000 }, /* R4099  - Write Sequencer 3 */
	[4100] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4100  - Write Sequencer 4 */
	[4101] = { 0x00FF, 0x00FF, 0x0000 }, /* R4101  - Write Sequencer 5 */
	[4102] = { 0x070F, 0x070F, 0x0000 }, /* R4102  - Write Sequencer 6 */
	[4103] = { 0x010F, 0x010F, 0x0000 }, /* R4103  - Write Sequencer 7 */
	[4104] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4104  - Write Sequencer 8 */
	[4105] = { 0x00FF, 0x00FF, 0x0000 }, /* R4105  - Write Sequencer 9 */
	[4106] = { 0x070F, 0x070F, 0x0000 }, /* R4106  - Write Sequencer 10 */
	[4107] = { 0x010F, 0x010F, 0x0000 }, /* R4107  - Write Sequencer 11 */
	[4108] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4108  - Write Sequencer 12 */
	[4109] = { 0x00FF, 0x00FF, 0x0000 }, /* R4109  - Write Sequencer 13 */
	[4110] = { 0x070F, 0x070F, 0x0000 }, /* R4110  - Write Sequencer 14 */
	[4111] = { 0x010F, 0x010F, 0x0000 }, /* R4111  - Write Sequencer 15 */
	[4112] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4112  - Write Sequencer 16 */
	[4113] = { 0x00FF, 0x00FF, 0x0000 }, /* R4113  - Write Sequencer 17 */
	[4114] = { 0x070F, 0x070F, 0x0000 }, /* R4114  - Write Sequencer 18 */
	[4115] = { 0x010F, 0x010F, 0x0000 }, /* R4115  - Write Sequencer 19 */
	[4116] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4116  - Write Sequencer 20 */
	[4117] = { 0x00FF, 0x00FF, 0x0000 }, /* R4117  - Write Sequencer 21 */
	[4118] = { 0x070F, 0x070F, 0x0000 }, /* R4118  - Write Sequencer 22 */
	[4119] = { 0x010F, 0x010F, 0x0000 }, /* R4119  - Write Sequencer 23 */
	[4120] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4120  - Write Sequencer 24 */
	[4121] = { 0x00FF, 0x00FF, 0x0000 }, /* R4121  - Write Sequencer 25 */
	[4122] = { 0x070F, 0x070F, 0x0000 }, /* R4122  - Write Sequencer 26 */
	[4123] = { 0x010F, 0x010F, 0x0000 }, /* R4123  - Write Sequencer 27 */
	[4124] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4124  - Write Sequencer 28 */
	[4125] = { 0x00FF, 0x00FF, 0x0000 }, /* R4125  - Write Sequencer 29 */
	[4126] = { 0x070F, 0x070F, 0x0000 }, /* R4126  - Write Sequencer 30 */
	[4127] = { 0x010F, 0x010F, 0x0000 }, /* R4127  - Write Sequencer 31 */
	[4128] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4128  - Write Sequencer 32 */
	[4129] = { 0x00FF, 0x00FF, 0x0000 }, /* R4129  - Write Sequencer 33 */
	[4130] = { 0x070F, 0x070F, 0x0000 }, /* R4130  - Write Sequencer 34 */
	[4131] = { 0x010F, 0x010F, 0x0000 }, /* R4131  - Write Sequencer 35 */
	[4132] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4132  - Write Sequencer 36 */
	[4133] = { 0x00FF, 0x00FF, 0x0000 }, /* R4133  - Write Sequencer 37 */
	[4134] = { 0x070F, 0x070F, 0x0000 }, /* R4134  - Write Sequencer 38 */
	[4135] = { 0x010F, 0x010F, 0x0000 }, /* R4135  - Write Sequencer 39 */
	[4136] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4136  - Write Sequencer 40 */
	[4137] = { 0x00FF, 0x00FF, 0x0000 }, /* R4137  - Write Sequencer 41 */
	[4138] = { 0x070F, 0x070F, 0x0000 }, /* R4138  - Write Sequencer 42 */
	[4139] = { 0x010F, 0x010F, 0x0000 }, /* R4139  - Write Sequencer 43 */
	[4140] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4140  - Write Sequencer 44 */
	[4141] = { 0x00FF, 0x00FF, 0x0000 }, /* R4141  - Write Sequencer 45 */
	[4142] = { 0x070F, 0x070F, 0x0000 }, /* R4142  - Write Sequencer 46 */
	[4143] = { 0x010F, 0x010F, 0x0000 }, /* R4143  - Write Sequencer 47 */
	[4144] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4144  - Write Sequencer 48 */
	[4145] = { 0x00FF, 0x00FF, 0x0000 }, /* R4145  - Write Sequencer 49 */
	[4146] = { 0x070F, 0x070F, 0x0000 }, /* R4146  - Write Sequencer 50 */
	[4147] = { 0x010F, 0x010F, 0x0000 }, /* R4147  - Write Sequencer 51 */
	[4148] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4148  - Write Sequencer 52 */
	[4149] = { 0x00FF, 0x00FF, 0x0000 }, /* R4149  - Write Sequencer 53 */
	[4150] = { 0x070F, 0x070F, 0x0000 }, /* R4150  - Write Sequencer 54 */
	[4151] = { 0x010F, 0x010F, 0x0000 }, /* R4151  - Write Sequencer 55 */
	[4152] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4152  - Write Sequencer 56 */
	[4153] = { 0x00FF, 0x00FF, 0x0000 }, /* R4153  - Write Sequencer 57 */
	[4154] = { 0x070F, 0x070F, 0x0000 }, /* R4154  - Write Sequencer 58 */
	[4155] = { 0x010F, 0x010F, 0x0000 }, /* R4155  - Write Sequencer 59 */
	[4156] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4156  - Write Sequencer 60 */
	[4157] = { 0x00FF, 0x00FF, 0x0000 }, /* R4157  - Write Sequencer 61 */
	[4158] = { 0x070F, 0x070F, 0x0000 }, /* R4158  - Write Sequencer 62 */
	[4159] = { 0x010F, 0x010F, 0x0000 }, /* R4159  - Write Sequencer 63 */
	[4160] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4160  - Write Sequencer 64 */
	[4161] = { 0x00FF, 0x00FF, 0x0000 }, /* R4161  - Write Sequencer 65 */
	[4162] = { 0x070F, 0x070F, 0x0000 }, /* R4162  - Write Sequencer 66 */
	[4163] = { 0x010F, 0x010F, 0x0000 }, /* R4163  - Write Sequencer 67 */
	[4164] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4164  - Write Sequencer 68 */
	[4165] = { 0x00FF, 0x00FF, 0x0000 }, /* R4165  - Write Sequencer 69 */
	[4166] = { 0x070F, 0x070F, 0x0000 }, /* R4166  - Write Sequencer 70 */
	[4167] = { 0x010F, 0x010F, 0x0000 }, /* R4167  - Write Sequencer 71 */
	[4168] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4168  - Write Sequencer 72 */
	[4169] = { 0x00FF, 0x00FF, 0x0000 }, /* R4169  - Write Sequencer 73 */
	[4170] = { 0x070F, 0x070F, 0x0000 }, /* R4170  - Write Sequencer 74 */
	[4171] = { 0x010F, 0x010F, 0x0000 }, /* R4171  - Write Sequencer 75 */
	[4172] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4172  - Write Sequencer 76 */
	[4173] = { 0x00FF, 0x00FF, 0x0000 }, /* R4173  - Write Sequencer 77 */
	[4174] = { 0x070F, 0x070F, 0x0000 }, /* R4174  - Write Sequencer 78 */
	[4175] = { 0x010F, 0x010F, 0x0000 }, /* R4175  - Write Sequencer 79 */
	[4176] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4176  - Write Sequencer 80 */
	[4177] = { 0x00FF, 0x00FF, 0x0000 }, /* R4177  - Write Sequencer 81 */
	[4178] = { 0x070F, 0x070F, 0x0000 }, /* R4178  - Write Sequencer 82 */
	[4179] = { 0x010F, 0x010F, 0x0000 }, /* R4179  - Write Sequencer 83 */
	[4180] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4180  - Write Sequencer 84 */
	[4181] = { 0x00FF, 0x00FF, 0x0000 }, /* R4181  - Write Sequencer 85 */
	[4182] = { 0x070F, 0x070F, 0x0000 }, /* R4182  - Write Sequencer 86 */
	[4183] = { 0x010F, 0x010F, 0x0000 }, /* R4183  - Write Sequencer 87 */
	[4184] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4184  - Write Sequencer 88 */
	[4185] = { 0x00FF, 0x00FF, 0x0000 }, /* R4185  - Write Sequencer 89 */
	[4186] = { 0x070F, 0x070F, 0x0000 }, /* R4186  - Write Sequencer 90 */
	[4187] = { 0x010F, 0x010F, 0x0000 }, /* R4187  - Write Sequencer 91 */
	[4188] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4188  - Write Sequencer 92 */
	[4189] = { 0x00FF, 0x00FF, 0x0000 }, /* R4189  - Write Sequencer 93 */
	[4190] = { 0x070F, 0x070F, 0x0000 }, /* R4190  - Write Sequencer 94 */
	[4191] = { 0x010F, 0x010F, 0x0000 }, /* R4191  - Write Sequencer 95 */
	[4192] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4192  - Write Sequencer 96 */
	[4193] = { 0x00FF, 0x00FF, 0x0000 }, /* R4193  - Write Sequencer 97 */
	[4194] = { 0x070F, 0x070F, 0x0000 }, /* R4194  - Write Sequencer 98 */
	[4195] = { 0x010F, 0x010F, 0x0000 }, /* R4195  - Write Sequencer 99 */
	[4196] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4196  - Write Sequencer 100 */
	[4197] = { 0x00FF, 0x00FF, 0x0000 }, /* R4197  - Write Sequencer 101 */
	[4198] = { 0x070F, 0x070F, 0x0000 }, /* R4198  - Write Sequencer 102 */
	[4199] = { 0x010F, 0x010F, 0x0000 }, /* R4199  - Write Sequencer 103 */
	[4200] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4200  - Write Sequencer 104 */
	[4201] = { 0x00FF, 0x00FF, 0x0000 }, /* R4201  - Write Sequencer 105 */
	[4202] = { 0x070F, 0x070F, 0x0000 }, /* R4202  - Write Sequencer 106 */
	[4203] = { 0x010F, 0x010F, 0x0000 }, /* R4203  - Write Sequencer 107 */
	[4204] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4204  - Write Sequencer 108 */
	[4205] = { 0x00FF, 0x00FF, 0x0000 }, /* R4205  - Write Sequencer 109 */
	[4206] = { 0x070F, 0x070F, 0x0000 }, /* R4206  - Write Sequencer 110 */
	[4207] = { 0x010F, 0x010F, 0x0000 }, /* R4207  - Write Sequencer 111 */
	[4208] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4208  - Write Sequencer 112 */
	[4209] = { 0x00FF, 0x00FF, 0x0000 }, /* R4209  - Write Sequencer 113 */
	[4210] = { 0x070F, 0x070F, 0x0000 }, /* R4210  - Write Sequencer 114 */
	[4211] = { 0x010F, 0x010F, 0x0000 }, /* R4211  - Write Sequencer 115 */
	[4212] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4212  - Write Sequencer 116 */
	[4213] = { 0x00FF, 0x00FF, 0x0000 }, /* R4213  - Write Sequencer 117 */
	[4214] = { 0x070F, 0x070F, 0x0000 }, /* R4214  - Write Sequencer 118 */
	[4215] = { 0x010F, 0x010F, 0x0000 }, /* R4215  - Write Sequencer 119 */
	[4216] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4216  - Write Sequencer 120 */
	[4217] = { 0x00FF, 0x00FF, 0x0000 }, /* R4217  - Write Sequencer 121 */
	[4218] = { 0x070F, 0x070F, 0x0000 }, /* R4218  - Write Sequencer 122 */
	[4219] = { 0x010F, 0x010F, 0x0000 }, /* R4219  - Write Sequencer 123 */
	[4220] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4220  - Write Sequencer 124 */
	[4221] = { 0x00FF, 0x00FF, 0x0000 }, /* R4221  - Write Sequencer 125 */
	[4222] = { 0x070F, 0x070F, 0x0000 }, /* R4222  - Write Sequencer 126 */
	[4223] = { 0x010F, 0x010F, 0x0000 }, /* R4223  - Write Sequencer 127 */
	[4224] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4224  - Write Sequencer 128 */
	[4225] = { 0x00FF, 0x00FF, 0x0000 }, /* R4225  - Write Sequencer 129 */
	[4226] = { 0x070F, 0x070F, 0x0000 }, /* R4226  - Write Sequencer 130 */
	[4227] = { 0x010F, 0x010F, 0x0000 }, /* R4227  - Write Sequencer 131 */
	[4228] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4228  - Write Sequencer 132 */
	[4229] = { 0x00FF, 0x00FF, 0x0000 }, /* R4229  - Write Sequencer 133 */
	[4230] = { 0x070F, 0x070F, 0x0000 }, /* R4230  - Write Sequencer 134 */
	[4231] = { 0x010F, 0x010F, 0x0000 }, /* R4231  - Write Sequencer 135 */
	[4232] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4232  - Write Sequencer 136 */
	[4233] = { 0x00FF, 0x00FF, 0x0000 }, /* R4233  - Write Sequencer 137 */
	[4234] = { 0x070F, 0x070F, 0x0000 }, /* R4234  - Write Sequencer 138 */
	[4235] = { 0x010F, 0x010F, 0x0000 }, /* R4235  - Write Sequencer 139 */
	[4236] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4236  - Write Sequencer 140 */
	[4237] = { 0x00FF, 0x00FF, 0x0000 }, /* R4237  - Write Sequencer 141 */
	[4238] = { 0x070F, 0x070F, 0x0000 }, /* R4238  - Write Sequencer 142 */
	[4239] = { 0x010F, 0x010F, 0x0000 }, /* R4239  - Write Sequencer 143 */
	[4240] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4240  - Write Sequencer 144 */
	[4241] = { 0x00FF, 0x00FF, 0x0000 }, /* R4241  - Write Sequencer 145 */
	[4242] = { 0x070F, 0x070F, 0x0000 }, /* R4242  - Write Sequencer 146 */
	[4243] = { 0x010F, 0x010F, 0x0000 }, /* R4243  - Write Sequencer 147 */
	[4244] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4244  - Write Sequencer 148 */
	[4245] = { 0x00FF, 0x00FF, 0x0000 }, /* R4245  - Write Sequencer 149 */
	[4246] = { 0x070F, 0x070F, 0x0000 }, /* R4246  - Write Sequencer 150 */
	[4247] = { 0x010F, 0x010F, 0x0000 }, /* R4247  - Write Sequencer 151 */
	[4248] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4248  - Write Sequencer 152 */
	[4249] = { 0x00FF, 0x00FF, 0x0000 }, /* R4249  - Write Sequencer 153 */
	[4250] = { 0x070F, 0x070F, 0x0000 }, /* R4250  - Write Sequencer 154 */
	[4251] = { 0x010F, 0x010F, 0x0000 }, /* R4251  - Write Sequencer 155 */
	[4252] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4252  - Write Sequencer 156 */
	[4253] = { 0x00FF, 0x00FF, 0x0000 }, /* R4253  - Write Sequencer 157 */
	[4254] = { 0x070F, 0x070F, 0x0000 }, /* R4254  - Write Sequencer 158 */
	[4255] = { 0x010F, 0x010F, 0x0000 }, /* R4255  - Write Sequencer 159 */
	[4256] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4256  - Write Sequencer 160 */
	[4257] = { 0x00FF, 0x00FF, 0x0000 }, /* R4257  - Write Sequencer 161 */
	[4258] = { 0x070F, 0x070F, 0x0000 }, /* R4258  - Write Sequencer 162 */
	[4259] = { 0x010F, 0x010F, 0x0000 }, /* R4259  - Write Sequencer 163 */
	[4260] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4260  - Write Sequencer 164 */
	[4261] = { 0x00FF, 0x00FF, 0x0000 }, /* R4261  - Write Sequencer 165 */
	[4262] = { 0x070F, 0x070F, 0x0000 }, /* R4262  - Write Sequencer 166 */
	[4263] = { 0x010F, 0x010F, 0x0000 }, /* R4263  - Write Sequencer 167 */
	[4264] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4264  - Write Sequencer 168 */
	[4265] = { 0x00FF, 0x00FF, 0x0000 }, /* R4265  - Write Sequencer 169 */
	[4266] = { 0x070F, 0x070F, 0x0000 }, /* R4266  - Write Sequencer 170 */
	[4267] = { 0x010F, 0x010F, 0x0000 }, /* R4267  - Write Sequencer 171 */
	[4268] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4268  - Write Sequencer 172 */
	[4269] = { 0x00FF, 0x00FF, 0x0000 }, /* R4269  - Write Sequencer 173 */
	[4270] = { 0x070F, 0x070F, 0x0000 }, /* R4270  - Write Sequencer 174 */
	[4271] = { 0x010F, 0x010F, 0x0000 }, /* R4271  - Write Sequencer 175 */
	[4272] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4272  - Write Sequencer 176 */
	[4273] = { 0x00FF, 0x00FF, 0x0000 }, /* R4273  - Write Sequencer 177 */
	[4274] = { 0x070F, 0x070F, 0x0000 }, /* R4274  - Write Sequencer 178 */
	[4275] = { 0x010F, 0x010F, 0x0000 }, /* R4275  - Write Sequencer 179 */
	[4276] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4276  - Write Sequencer 180 */
	[4277] = { 0x00FF, 0x00FF, 0x0000 }, /* R4277  - Write Sequencer 181 */
	[4278] = { 0x070F, 0x070F, 0x0000 }, /* R4278  - Write Sequencer 182 */
	[4279] = { 0x010F, 0x010F, 0x0000 }, /* R4279  - Write Sequencer 183 */
	[4280] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4280  - Write Sequencer 184 */
	[4281] = { 0x00FF, 0x00FF, 0x0000 }, /* R4281  - Write Sequencer 185 */
	[4282] = { 0x070F, 0x070F, 0x0000 }, /* R4282  - Write Sequencer 186 */
	[4283] = { 0x010F, 0x010F, 0x0000 }, /* R4283  - Write Sequencer 187 */
	[4284] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4284  - Write Sequencer 188 */
	[4285] = { 0x00FF, 0x00FF, 0x0000 }, /* R4285  - Write Sequencer 189 */
	[4286] = { 0x070F, 0x070F, 0x0000 }, /* R4286  - Write Sequencer 190 */
	[4287] = { 0x010F, 0x010F, 0x0000 }, /* R4287  - Write Sequencer 191 */
	[4288] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4288  - Write Sequencer 192 */
	[4289] = { 0x00FF, 0x00FF, 0x0000 }, /* R4289  - Write Sequencer 193 */
	[4290] = { 0x070F, 0x070F, 0x0000 }, /* R4290  - Write Sequencer 194 */
	[4291] = { 0x010F, 0x010F, 0x0000 }, /* R4291  - Write Sequencer 195 */
	[4292] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4292  - Write Sequencer 196 */
	[4293] = { 0x00FF, 0x00FF, 0x0000 }, /* R4293  - Write Sequencer 197 */
	[4294] = { 0x070F, 0x070F, 0x0000 }, /* R4294  - Write Sequencer 198 */
	[4295] = { 0x010F, 0x010F, 0x0000 }, /* R4295  - Write Sequencer 199 */
	[4296] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4296  - Write Sequencer 200 */
	[4297] = { 0x00FF, 0x00FF, 0x0000 }, /* R4297  - Write Sequencer 201 */
	[4298] = { 0x070F, 0x070F, 0x0000 }, /* R4298  - Write Sequencer 202 */
	[4299] = { 0x010F, 0x010F, 0x0000 }, /* R4299  - Write Sequencer 203 */
	[4300] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4300  - Write Sequencer 204 */
	[4301] = { 0x00FF, 0x00FF, 0x0000 }, /* R4301  - Write Sequencer 205 */
	[4302] = { 0x070F, 0x070F, 0x0000 }, /* R4302  - Write Sequencer 206 */
	[4303] = { 0x010F, 0x010F, 0x0000 }, /* R4303  - Write Sequencer 207 */
	[4304] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4304  - Write Sequencer 208 */
	[4305] = { 0x00FF, 0x00FF, 0x0000 }, /* R4305  - Write Sequencer 209 */
	[4306] = { 0x070F, 0x070F, 0x0000 }, /* R4306  - Write Sequencer 210 */
	[4307] = { 0x010F, 0x010F, 0x0000 }, /* R4307  - Write Sequencer 211 */
	[4308] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4308  - Write Sequencer 212 */
	[4309] = { 0x00FF, 0x00FF, 0x0000 }, /* R4309  - Write Sequencer 213 */
	[4310] = { 0x070F, 0x070F, 0x0000 }, /* R4310  - Write Sequencer 214 */
	[4311] = { 0x010F, 0x010F, 0x0000 }, /* R4311  - Write Sequencer 215 */
	[4312] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4312  - Write Sequencer 216 */
	[4313] = { 0x00FF, 0x00FF, 0x0000 }, /* R4313  - Write Sequencer 217 */
	[4314] = { 0x070F, 0x070F, 0x0000 }, /* R4314  - Write Sequencer 218 */
	[4315] = { 0x010F, 0x010F, 0x0000 }, /* R4315  - Write Sequencer 219 */
	[4316] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4316  - Write Sequencer 220 */
	[4317] = { 0x00FF, 0x00FF, 0x0000 }, /* R4317  - Write Sequencer 221 */
	[4318] = { 0x070F, 0x070F, 0x0000 }, /* R4318  - Write Sequencer 222 */
	[4319] = { 0x010F, 0x010F, 0x0000 }, /* R4319  - Write Sequencer 223 */
	[4320] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4320  - Write Sequencer 224 */
	[4321] = { 0x00FF, 0x00FF, 0x0000 }, /* R4321  - Write Sequencer 225 */
	[4322] = { 0x070F, 0x070F, 0x0000 }, /* R4322  - Write Sequencer 226 */
	[4323] = { 0x010F, 0x010F, 0x0000 }, /* R4323  - Write Sequencer 227 */
	[4324] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4324  - Write Sequencer 228 */
	[4325] = { 0x00FF, 0x00FF, 0x0000 }, /* R4325  - Write Sequencer 229 */
	[4326] = { 0x070F, 0x070F, 0x0000 }, /* R4326  - Write Sequencer 230 */
	[4327] = { 0x010F, 0x010F, 0x0000 }, /* R4327  - Write Sequencer 231 */
	[4328] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4328  - Write Sequencer 232 */
	[4329] = { 0x00FF, 0x00FF, 0x0000 }, /* R4329  - Write Sequencer 233 */
	[4330] = { 0x070F, 0x070F, 0x0000 }, /* R4330  - Write Sequencer 234 */
	[4331] = { 0x010F, 0x010F, 0x0000 }, /* R4331  - Write Sequencer 235 */
	[4332] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4332  - Write Sequencer 236 */
	[4333] = { 0x00FF, 0x00FF, 0x0000 }, /* R4333  - Write Sequencer 237 */
	[4334] = { 0x070F, 0x070F, 0x0000 }, /* R4334  - Write Sequencer 238 */
	[4335] = { 0x010F, 0x010F, 0x0000 }, /* R4335  - Write Sequencer 239 */
	[4336] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4336  - Write Sequencer 240 */
	[4337] = { 0x00FF, 0x00FF, 0x0000 }, /* R4337  - Write Sequencer 241 */
	[4338] = { 0x070F, 0x070F, 0x0000 }, /* R4338  - Write Sequencer 242 */
	[4339] = { 0x010F, 0x010F, 0x0000 }, /* R4339  - Write Sequencer 243 */
	[4340] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4340  - Write Sequencer 244 */
	[4341] = { 0x00FF, 0x00FF, 0x0000 }, /* R4341  - Write Sequencer 245 */
	[4342] = { 0x070F, 0x070F, 0x0000 }, /* R4342  - Write Sequencer 246 */
	[4343] = { 0x010F, 0x010F, 0x0000 }, /* R4343  - Write Sequencer 247 */
	[4344] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4344  - Write Sequencer 248 */
	[4345] = { 0x00FF, 0x00FF, 0x0000 }, /* R4345  - Write Sequencer 249 */
	[4346] = { 0x070F, 0x070F, 0x0000 }, /* R4346  - Write Sequencer 250 */
	[4347] = { 0x010F, 0x010F, 0x0000 }, /* R4347  - Write Sequencer 251 */
	[4348] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4348  - Write Sequencer 252 */
	[4349] = { 0x00FF, 0x00FF, 0x0000 }, /* R4349  - Write Sequencer 253 */
	[4350] = { 0x070F, 0x070F, 0x0000 }, /* R4350  - Write Sequencer 254 */
	[4351] = { 0x010F, 0x010F, 0x0000 }, /* R4351  - Write Sequencer 255 */
	[4352] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4352  - Write Sequencer 256 */
	[4353] = { 0x00FF, 0x00FF, 0x0000 }, /* R4353  - Write Sequencer 257 */
	[4354] = { 0x070F, 0x070F, 0x0000 }, /* R4354  - Write Sequencer 258 */
	[4355] = { 0x010F, 0x010F, 0x0000 }, /* R4355  - Write Sequencer 259 */
	[4356] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4356  - Write Sequencer 260 */
	[4357] = { 0x00FF, 0x00FF, 0x0000 }, /* R4357  - Write Sequencer 261 */
	[4358] = { 0x070F, 0x070F, 0x0000 }, /* R4358  - Write Sequencer 262 */
	[4359] = { 0x010F, 0x010F, 0x0000 }, /* R4359  - Write Sequencer 263 */
	[4360] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4360  - Write Sequencer 264 */
	[4361] = { 0x00FF, 0x00FF, 0x0000 }, /* R4361  - Write Sequencer 265 */
	[4362] = { 0x070F, 0x070F, 0x0000 }, /* R4362  - Write Sequencer 266 */
	[4363] = { 0x010F, 0x010F, 0x0000 }, /* R4363  - Write Sequencer 267 */
	[4364] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4364  - Write Sequencer 268 */
	[4365] = { 0x00FF, 0x00FF, 0x0000 }, /* R4365  - Write Sequencer 269 */
	[4366] = { 0x070F, 0x070F, 0x0000 }, /* R4366  - Write Sequencer 270 */
	[4367] = { 0x010F, 0x010F, 0x0000 }, /* R4367  - Write Sequencer 271 */
	[4368] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4368  - Write Sequencer 272 */
	[4369] = { 0x00FF, 0x00FF, 0x0000 }, /* R4369  - Write Sequencer 273 */
	[4370] = { 0x070F, 0x070F, 0x0000 }, /* R4370  - Write Sequencer 274 */
	[4371] = { 0x010F, 0x010F, 0x0000 }, /* R4371  - Write Sequencer 275 */
	[4372] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4372  - Write Sequencer 276 */
	[4373] = { 0x00FF, 0x00FF, 0x0000 }, /* R4373  - Write Sequencer 277 */
	[4374] = { 0x070F, 0x070F, 0x0000 }, /* R4374  - Write Sequencer 278 */
	[4375] = { 0x010F, 0x010F, 0x0000 }, /* R4375  - Write Sequencer 279 */
	[4376] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4376  - Write Sequencer 280 */
	[4377] = { 0x00FF, 0x00FF, 0x0000 }, /* R4377  - Write Sequencer 281 */
	[4378] = { 0x070F, 0x070F, 0x0000 }, /* R4378  - Write Sequencer 282 */
	[4379] = { 0x010F, 0x010F, 0x0000 }, /* R4379  - Write Sequencer 283 */
	[4380] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4380  - Write Sequencer 284 */
	[4381] = { 0x00FF, 0x00FF, 0x0000 }, /* R4381  - Write Sequencer 285 */
	[4382] = { 0x070F, 0x070F, 0x0000 }, /* R4382  - Write Sequencer 286 */
	[4383] = { 0x010F, 0x010F, 0x0000 }, /* R4383  - Write Sequencer 287 */
	[4384] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4384  - Write Sequencer 288 */
	[4385] = { 0x00FF, 0x00FF, 0x0000 }, /* R4385  - Write Sequencer 289 */
	[4386] = { 0x070F, 0x070F, 0x0000 }, /* R4386  - Write Sequencer 290 */
	[4387] = { 0x010F, 0x010F, 0x0000 }, /* R4387  - Write Sequencer 291 */
	[4388] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4388  - Write Sequencer 292 */
	[4389] = { 0x00FF, 0x00FF, 0x0000 }, /* R4389  - Write Sequencer 293 */
	[4390] = { 0x070F, 0x070F, 0x0000 }, /* R4390  - Write Sequencer 294 */
	[4391] = { 0x010F, 0x010F, 0x0000 }, /* R4391  - Write Sequencer 295 */
	[4392] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4392  - Write Sequencer 296 */
	[4393] = { 0x00FF, 0x00FF, 0x0000 }, /* R4393  - Write Sequencer 297 */
	[4394] = { 0x070F, 0x070F, 0x0000 }, /* R4394  - Write Sequencer 298 */
	[4395] = { 0x010F, 0x010F, 0x0000 }, /* R4395  - Write Sequencer 299 */
	[4396] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4396  - Write Sequencer 300 */
	[4397] = { 0x00FF, 0x00FF, 0x0000 }, /* R4397  - Write Sequencer 301 */
	[4398] = { 0x070F, 0x070F, 0x0000 }, /* R4398  - Write Sequencer 302 */
	[4399] = { 0x010F, 0x010F, 0x0000 }, /* R4399  - Write Sequencer 303 */
	[4400] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4400  - Write Sequencer 304 */
	[4401] = { 0x00FF, 0x00FF, 0x0000 }, /* R4401  - Write Sequencer 305 */
	[4402] = { 0x070F, 0x070F, 0x0000 }, /* R4402  - Write Sequencer 306 */
	[4403] = { 0x010F, 0x010F, 0x0000 }, /* R4403  - Write Sequencer 307 */
	[4404] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4404  - Write Sequencer 308 */
	[4405] = { 0x00FF, 0x00FF, 0x0000 }, /* R4405  - Write Sequencer 309 */
	[4406] = { 0x070F, 0x070F, 0x0000 }, /* R4406  - Write Sequencer 310 */
	[4407] = { 0x010F, 0x010F, 0x0000 }, /* R4407  - Write Sequencer 311 */
	[4408] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4408  - Write Sequencer 312 */
	[4409] = { 0x00FF, 0x00FF, 0x0000 }, /* R4409  - Write Sequencer 313 */
	[4410] = { 0x070F, 0x070F, 0x0000 }, /* R4410  - Write Sequencer 314 */
	[4411] = { 0x010F, 0x010F, 0x0000 }, /* R4411  - Write Sequencer 315 */
	[4412] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4412  - Write Sequencer 316 */
	[4413] = { 0x00FF, 0x00FF, 0x0000 }, /* R4413  - Write Sequencer 317 */
	[4414] = { 0x070F, 0x070F, 0x0000 }, /* R4414  - Write Sequencer 318 */
	[4415] = { 0x010F, 0x010F, 0x0000 }, /* R4415  - Write Sequencer 319 */
	[4416] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4416  - Write Sequencer 320 */
	[4417] = { 0x00FF, 0x00FF, 0x0000 }, /* R4417  - Write Sequencer 321 */
	[4418] = { 0x070F, 0x070F, 0x0000 }, /* R4418  - Write Sequencer 322 */
	[4419] = { 0x010F, 0x010F, 0x0000 }, /* R4419  - Write Sequencer 323 */
	[4420] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4420  - Write Sequencer 324 */
	[4421] = { 0x00FF, 0x00FF, 0x0000 }, /* R4421  - Write Sequencer 325 */
	[4422] = { 0x070F, 0x070F, 0x0000 }, /* R4422  - Write Sequencer 326 */
	[4423] = { 0x010F, 0x010F, 0x0000 }, /* R4423  - Write Sequencer 327 */
	[4424] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4424  - Write Sequencer 328 */
	[4425] = { 0x00FF, 0x00FF, 0x0000 }, /* R4425  - Write Sequencer 329 */
	[4426] = { 0x070F, 0x070F, 0x0000 }, /* R4426  - Write Sequencer 330 */
	[4427] = { 0x010F, 0x010F, 0x0000 }, /* R4427  - Write Sequencer 331 */
	[4428] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4428  - Write Sequencer 332 */
	[4429] = { 0x00FF, 0x00FF, 0x0000 }, /* R4429  - Write Sequencer 333 */
	[4430] = { 0x070F, 0x070F, 0x0000 }, /* R4430  - Write Sequencer 334 */
	[4431] = { 0x010F, 0x010F, 0x0000 }, /* R4431  - Write Sequencer 335 */
	[4432] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4432  - Write Sequencer 336 */
	[4433] = { 0x00FF, 0x00FF, 0x0000 }, /* R4433  - Write Sequencer 337 */
	[4434] = { 0x070F, 0x070F, 0x0000 }, /* R4434  - Write Sequencer 338 */
	[4435] = { 0x010F, 0x010F, 0x0000 }, /* R4435  - Write Sequencer 339 */
	[4436] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4436  - Write Sequencer 340 */
	[4437] = { 0x00FF, 0x00FF, 0x0000 }, /* R4437  - Write Sequencer 341 */
	[4438] = { 0x070F, 0x070F, 0x0000 }, /* R4438  - Write Sequencer 342 */
	[4439] = { 0x010F, 0x010F, 0x0000 }, /* R4439  - Write Sequencer 343 */
	[4440] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4440  - Write Sequencer 344 */
	[4441] = { 0x00FF, 0x00FF, 0x0000 }, /* R4441  - Write Sequencer 345 */
	[4442] = { 0x070F, 0x070F, 0x0000 }, /* R4442  - Write Sequencer 346 */
	[4443] = { 0x010F, 0x010F, 0x0000 }, /* R4443  - Write Sequencer 347 */
	[4444] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4444  - Write Sequencer 348 */
	[4445] = { 0x00FF, 0x00FF, 0x0000 }, /* R4445  - Write Sequencer 349 */
	[4446] = { 0x070F, 0x070F, 0x0000 }, /* R4446  - Write Sequencer 350 */
	[4447] = { 0x010F, 0x010F, 0x0000 }, /* R4447  - Write Sequencer 351 */
	[4448] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4448  - Write Sequencer 352 */
	[4449] = { 0x00FF, 0x00FF, 0x0000 }, /* R4449  - Write Sequencer 353 */
	[4450] = { 0x070F, 0x070F, 0x0000 }, /* R4450  - Write Sequencer 354 */
	[4451] = { 0x010F, 0x010F, 0x0000 }, /* R4451  - Write Sequencer 355 */
	[4452] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4452  - Write Sequencer 356 */
	[4453] = { 0x00FF, 0x00FF, 0x0000 }, /* R4453  - Write Sequencer 357 */
	[4454] = { 0x070F, 0x070F, 0x0000 }, /* R4454  - Write Sequencer 358 */
	[4455] = { 0x010F, 0x010F, 0x0000 }, /* R4455  - Write Sequencer 359 */
	[4456] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4456  - Write Sequencer 360 */
	[4457] = { 0x00FF, 0x00FF, 0x0000 }, /* R4457  - Write Sequencer 361 */
	[4458] = { 0x070F, 0x070F, 0x0000 }, /* R4458  - Write Sequencer 362 */
	[4459] = { 0x010F, 0x010F, 0x0000 }, /* R4459  - Write Sequencer 363 */
	[4460] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4460  - Write Sequencer 364 */
	[4461] = { 0x00FF, 0x00FF, 0x0000 }, /* R4461  - Write Sequencer 365 */
	[4462] = { 0x070F, 0x070F, 0x0000 }, /* R4462  - Write Sequencer 366 */
	[4463] = { 0x010F, 0x010F, 0x0000 }, /* R4463  - Write Sequencer 367 */
	[4464] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4464  - Write Sequencer 368 */
	[4465] = { 0x00FF, 0x00FF, 0x0000 }, /* R4465  - Write Sequencer 369 */
	[4466] = { 0x070F, 0x070F, 0x0000 }, /* R4466  - Write Sequencer 370 */
	[4467] = { 0x010F, 0x010F, 0x0000 }, /* R4467  - Write Sequencer 371 */
	[4468] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4468  - Write Sequencer 372 */
	[4469] = { 0x00FF, 0x00FF, 0x0000 }, /* R4469  - Write Sequencer 373 */
	[4470] = { 0x070F, 0x070F, 0x0000 }, /* R4470  - Write Sequencer 374 */
	[4471] = { 0x010F, 0x010F, 0x0000 }, /* R4471  - Write Sequencer 375 */
	[4472] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4472  - Write Sequencer 376 */
	[4473] = { 0x00FF, 0x00FF, 0x0000 }, /* R4473  - Write Sequencer 377 */
	[4474] = { 0x070F, 0x070F, 0x0000 }, /* R4474  - Write Sequencer 378 */
	[4475] = { 0x010F, 0x010F, 0x0000 }, /* R4475  - Write Sequencer 379 */
	[4476] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4476  - Write Sequencer 380 */
	[4477] = { 0x00FF, 0x00FF, 0x0000 }, /* R4477  - Write Sequencer 381 */
	[4478] = { 0x070F, 0x070F, 0x0000 }, /* R4478  - Write Sequencer 382 */
	[4479] = { 0x010F, 0x010F, 0x0000 }, /* R4479  - Write Sequencer 383 */
	[4480] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4480  - Write Sequencer 384 */
	[4481] = { 0x00FF, 0x00FF, 0x0000 }, /* R4481  - Write Sequencer 385 */
	[4482] = { 0x070F, 0x070F, 0x0000 }, /* R4482  - Write Sequencer 386 */
	[4483] = { 0x010F, 0x010F, 0x0000 }, /* R4483  - Write Sequencer 387 */
	[4484] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4484  - Write Sequencer 388 */
	[4485] = { 0x00FF, 0x00FF, 0x0000 }, /* R4485  - Write Sequencer 389 */
	[4486] = { 0x070F, 0x070F, 0x0000 }, /* R4486  - Write Sequencer 390 */
	[4487] = { 0x010F, 0x010F, 0x0000 }, /* R4487  - Write Sequencer 391 */
	[4488] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4488  - Write Sequencer 392 */
	[4489] = { 0x00FF, 0x00FF, 0x0000 }, /* R4489  - Write Sequencer 393 */
	[4490] = { 0x070F, 0x070F, 0x0000 }, /* R4490  - Write Sequencer 394 */
	[4491] = { 0x010F, 0x010F, 0x0000 }, /* R4491  - Write Sequencer 395 */
	[4492] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4492  - Write Sequencer 396 */
	[4493] = { 0x00FF, 0x00FF, 0x0000 }, /* R4493  - Write Sequencer 397 */
	[4494] = { 0x070F, 0x070F, 0x0000 }, /* R4494  - Write Sequencer 398 */
	[4495] = { 0x010F, 0x010F, 0x0000 }, /* R4495  - Write Sequencer 399 */
	[4496] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4496  - Write Sequencer 400 */
	[4497] = { 0x00FF, 0x00FF, 0x0000 }, /* R4497  - Write Sequencer 401 */
	[4498] = { 0x070F, 0x070F, 0x0000 }, /* R4498  - Write Sequencer 402 */
	[4499] = { 0x010F, 0x010F, 0x0000 }, /* R4499  - Write Sequencer 403 */
	[4500] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4500  - Write Sequencer 404 */
	[4501] = { 0x00FF, 0x00FF, 0x0000 }, /* R4501  - Write Sequencer 405 */
	[4502] = { 0x070F, 0x070F, 0x0000 }, /* R4502  - Write Sequencer 406 */
	[4503] = { 0x010F, 0x010F, 0x0000 }, /* R4503  - Write Sequencer 407 */
	[4504] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4504  - Write Sequencer 408 */
	[4505] = { 0x00FF, 0x00FF, 0x0000 }, /* R4505  - Write Sequencer 409 */
	[4506] = { 0x070F, 0x070F, 0x0000 }, /* R4506  - Write Sequencer 410 */
	[4507] = { 0x010F, 0x010F, 0x0000 }, /* R4507  - Write Sequencer 411 */
	[4508] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4508  - Write Sequencer 412 */
	[4509] = { 0x00FF, 0x00FF, 0x0000 }, /* R4509  - Write Sequencer 413 */
	[4510] = { 0x070F, 0x070F, 0x0000 }, /* R4510  - Write Sequencer 414 */
	[4511] = { 0x010F, 0x010F, 0x0000 }, /* R4511  - Write Sequencer 415 */
	[4512] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4512  - Write Sequencer 416 */
	[4513] = { 0x00FF, 0x00FF, 0x0000 }, /* R4513  - Write Sequencer 417 */
	[4514] = { 0x070F, 0x070F, 0x0000 }, /* R4514  - Write Sequencer 418 */
	[4515] = { 0x010F, 0x010F, 0x0000 }, /* R4515  - Write Sequencer 419 */
	[4516] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4516  - Write Sequencer 420 */
	[4517] = { 0x00FF, 0x00FF, 0x0000 }, /* R4517  - Write Sequencer 421 */
	[4518] = { 0x070F, 0x070F, 0x0000 }, /* R4518  - Write Sequencer 422 */
	[4519] = { 0x010F, 0x010F, 0x0000 }, /* R4519  - Write Sequencer 423 */
	[4520] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4520  - Write Sequencer 424 */
	[4521] = { 0x00FF, 0x00FF, 0x0000 }, /* R4521  - Write Sequencer 425 */
	[4522] = { 0x070F, 0x070F, 0x0000 }, /* R4522  - Write Sequencer 426 */
	[4523] = { 0x010F, 0x010F, 0x0000 }, /* R4523  - Write Sequencer 427 */
	[4524] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4524  - Write Sequencer 428 */
	[4525] = { 0x00FF, 0x00FF, 0x0000 }, /* R4525  - Write Sequencer 429 */
	[4526] = { 0x070F, 0x070F, 0x0000 }, /* R4526  - Write Sequencer 430 */
	[4527] = { 0x010F, 0x010F, 0x0000 }, /* R4527  - Write Sequencer 431 */
	[4528] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4528  - Write Sequencer 432 */
	[4529] = { 0x00FF, 0x00FF, 0x0000 }, /* R4529  - Write Sequencer 433 */
	[4530] = { 0x070F, 0x070F, 0x0000 }, /* R4530  - Write Sequencer 434 */
	[4531] = { 0x010F, 0x010F, 0x0000 }, /* R4531  - Write Sequencer 435 */
	[4532] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4532  - Write Sequencer 436 */
	[4533] = { 0x00FF, 0x00FF, 0x0000 }, /* R4533  - Write Sequencer 437 */
	[4534] = { 0x070F, 0x070F, 0x0000 }, /* R4534  - Write Sequencer 438 */
	[4535] = { 0x010F, 0x010F, 0x0000 }, /* R4535  - Write Sequencer 439 */
	[4536] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4536  - Write Sequencer 440 */
	[4537] = { 0x00FF, 0x00FF, 0x0000 }, /* R4537  - Write Sequencer 441 */
	[4538] = { 0x070F, 0x070F, 0x0000 }, /* R4538  - Write Sequencer 442 */
	[4539] = { 0x010F, 0x010F, 0x0000 }, /* R4539  - Write Sequencer 443 */
	[4540] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4540  - Write Sequencer 444 */
	[4541] = { 0x00FF, 0x00FF, 0x0000 }, /* R4541  - Write Sequencer 445 */
	[4542] = { 0x070F, 0x070F, 0x0000 }, /* R4542  - Write Sequencer 446 */
	[4543] = { 0x010F, 0x010F, 0x0000 }, /* R4543  - Write Sequencer 447 */
	[4544] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4544  - Write Sequencer 448 */
	[4545] = { 0x00FF, 0x00FF, 0x0000 }, /* R4545  - Write Sequencer 449 */
	[4546] = { 0x070F, 0x070F, 0x0000 }, /* R4546  - Write Sequencer 450 */
	[4547] = { 0x010F, 0x010F, 0x0000 }, /* R4547  - Write Sequencer 451 */
	[4548] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4548  - Write Sequencer 452 */
	[4549] = { 0x00FF, 0x00FF, 0x0000 }, /* R4549  - Write Sequencer 453 */
	[4550] = { 0x070F, 0x070F, 0x0000 }, /* R4550  - Write Sequencer 454 */
	[4551] = { 0x010F, 0x010F, 0x0000 }, /* R4551  - Write Sequencer 455 */
	[4552] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4552  - Write Sequencer 456 */
	[4553] = { 0x00FF, 0x00FF, 0x0000 }, /* R4553  - Write Sequencer 457 */
	[4554] = { 0x070F, 0x070F, 0x0000 }, /* R4554  - Write Sequencer 458 */
	[4555] = { 0x010F, 0x010F, 0x0000 }, /* R4555  - Write Sequencer 459 */
	[4556] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4556  - Write Sequencer 460 */
	[4557] = { 0x00FF, 0x00FF, 0x0000 }, /* R4557  - Write Sequencer 461 */
	[4558] = { 0x070F, 0x070F, 0x0000 }, /* R4558  - Write Sequencer 462 */
	[4559] = { 0x010F, 0x010F, 0x0000 }, /* R4559  - Write Sequencer 463 */
	[4560] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4560  - Write Sequencer 464 */
	[4561] = { 0x00FF, 0x00FF, 0x0000 }, /* R4561  - Write Sequencer 465 */
	[4562] = { 0x070F, 0x070F, 0x0000 }, /* R4562  - Write Sequencer 466 */
	[4563] = { 0x010F, 0x010F, 0x0000 }, /* R4563  - Write Sequencer 467 */
	[4564] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4564  - Write Sequencer 468 */
	[4565] = { 0x00FF, 0x00FF, 0x0000 }, /* R4565  - Write Sequencer 469 */
	[4566] = { 0x070F, 0x070F, 0x0000 }, /* R4566  - Write Sequencer 470 */
	[4567] = { 0x010F, 0x010F, 0x0000 }, /* R4567  - Write Sequencer 471 */
	[4568] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4568  - Write Sequencer 472 */
	[4569] = { 0x00FF, 0x00FF, 0x0000 }, /* R4569  - Write Sequencer 473 */
	[4570] = { 0x070F, 0x070F, 0x0000 }, /* R4570  - Write Sequencer 474 */
	[4571] = { 0x010F, 0x010F, 0x0000 }, /* R4571  - Write Sequencer 475 */
	[4572] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4572  - Write Sequencer 476 */
	[4573] = { 0x00FF, 0x00FF, 0x0000 }, /* R4573  - Write Sequencer 477 */
	[4574] = { 0x070F, 0x070F, 0x0000 }, /* R4574  - Write Sequencer 478 */
	[4575] = { 0x010F, 0x010F, 0x0000 }, /* R4575  - Write Sequencer 479 */
	[4576] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4576  - Write Sequencer 480 */
	[4577] = { 0x00FF, 0x00FF, 0x0000 }, /* R4577  - Write Sequencer 481 */
	[4578] = { 0x070F, 0x070F, 0x0000 }, /* R4578  - Write Sequencer 482 */
	[4579] = { 0x010F, 0x010F, 0x0000 }, /* R4579  - Write Sequencer 483 */
	[4580] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4580  - Write Sequencer 484 */
	[4581] = { 0x00FF, 0x00FF, 0x0000 }, /* R4581  - Write Sequencer 485 */
	[4582] = { 0x070F, 0x070F, 0x0000 }, /* R4582  - Write Sequencer 486 */
	[4583] = { 0x010F, 0x010F, 0x0000 }, /* R4583  - Write Sequencer 487 */
	[4584] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4584  - Write Sequencer 488 */
	[4585] = { 0x00FF, 0x00FF, 0x0000 }, /* R4585  - Write Sequencer 489 */
	[4586] = { 0x070F, 0x070F, 0x0000 }, /* R4586  - Write Sequencer 490 */
	[4587] = { 0x010F, 0x010F, 0x0000 }, /* R4587  - Write Sequencer 491 */
	[4588] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4588  - Write Sequencer 492 */
	[4589] = { 0x00FF, 0x00FF, 0x0000 }, /* R4589  - Write Sequencer 493 */
	[4590] = { 0x070F, 0x070F, 0x0000 }, /* R4590  - Write Sequencer 494 */
	[4591] = { 0x010F, 0x010F, 0x0000 }, /* R4591  - Write Sequencer 495 */
	[4592] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4592  - Write Sequencer 496 */
	[4593] = { 0x00FF, 0x00FF, 0x0000 }, /* R4593  - Write Sequencer 497 */
	[4594] = { 0x070F, 0x070F, 0x0000 }, /* R4594  - Write Sequencer 498 */
	[4595] = { 0x010F, 0x010F, 0x0000 }, /* R4595  - Write Sequencer 499 */
	[4596] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4596  - Write Sequencer 500 */
	[4597] = { 0x00FF, 0x00FF, 0x0000 }, /* R4597  - Write Sequencer 501 */
	[4598] = { 0x070F, 0x070F, 0x0000 }, /* R4598  - Write Sequencer 502 */
	[4599] = { 0x010F, 0x010F, 0x0000 }, /* R4599  - Write Sequencer 503 */
	[4600] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4600  - Write Sequencer 504 */
	[4601] = { 0x00FF, 0x00FF, 0x0000 }, /* R4601  - Write Sequencer 505 */
	[4602] = { 0x070F, 0x070F, 0x0000 }, /* R4602  - Write Sequencer 506 */
	[4603] = { 0x010F, 0x010F, 0x0000 }, /* R4603  - Write Sequencer 507 */
	[4604] = { 0x3FFF, 0x3FFF, 0x0000 }, /* R4604  - Write Sequencer 508 */
	[4605] = { 0x00FF, 0x00FF, 0x0000 }, /* R4605  - Write Sequencer 509 */
	[4606] = { 0x070F, 0x070F, 0x0000 }, /* R4606  - Write Sequencer 510 */
	[4607] = { 0x010F, 0x010F, 0x0000 }, /* R4607  - Write Sequencer 511 */
	[8192] = { 0x03FF, 0x03FF, 0x0000 }, /* R8192  - DSP2 Instruction RAM 0 */
	[9216] = { 0x003F, 0x003F, 0x0000 }, /* R9216  - DSP2 Address RAM 2 */
	[9217] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R9217  - DSP2 Address RAM 1 */
	[9218] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R9218  - DSP2 Address RAM 0 */
	[12288] = { 0x00FF, 0x00FF, 0x0000 }, /* R12288 - DSP2 Data1 RAM 1 */
	[12289] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R12289 - DSP2 Data1 RAM 0 */
	[13312] = { 0x00FF, 0x00FF, 0x0000 }, /* R13312 - DSP2 Data2 RAM 1 */
	[13313] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R13313 - DSP2 Data2 RAM 0 */
	[14336] = { 0x00FF, 0x00FF, 0x0000 }, /* R14336 - DSP2 Data3 RAM 1 */
	[14337] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R14337 - DSP2 Data3 RAM 0 */
	[15360] = { 0x07FF, 0x07FF, 0x0000 }, /* R15360 - DSP2 Coeff RAM 0 */
	[16384] = { 0x00FF, 0x00FF, 0x0000 }, /* R16384 - RETUNEADC_SHARED_COEFF_1 */
	[16385] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16385 - RETUNEADC_SHARED_COEFF_0 */
	[16386] = { 0x00FF, 0x00FF, 0x0000 }, /* R16386 - RETUNEDAC_SHARED_COEFF_1 */
	[16387] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16387 - RETUNEDAC_SHARED_COEFF_0 */
	[16388] = { 0x00FF, 0x00FF, 0x0000 }, /* R16388 - SOUNDSTAGE_ENABLES_1 */
	[16389] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16389 - SOUNDSTAGE_ENABLES_0 */
	[16896] = { 0x00FF, 0x00FF, 0x0000 }, /* R16896 - HDBASS_AI_1 */
	[16897] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16897 - HDBASS_AI_0 */
	[16898] = { 0x00FF, 0x00FF, 0x0000 }, /* R16898 - HDBASS_AR_1 */
	[16899] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16899 - HDBASS_AR_0 */
	[16900] = { 0x00FF, 0x00FF, 0x0000 }, /* R16900 - HDBASS_B_1 */
	[16901] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16901 - HDBASS_B_0 */
	[16902] = { 0x00FF, 0x00FF, 0x0000 }, /* R16902 - HDBASS_K_1 */
	[16903] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16903 - HDBASS_K_0 */
	[16904] = { 0x00FF, 0x00FF, 0x0000 }, /* R16904 - HDBASS_N1_1 */
	[16905] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16905 - HDBASS_N1_0 */
	[16906] = { 0x00FF, 0x00FF, 0x0000 }, /* R16906 - HDBASS_N2_1 */
	[16907] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16907 - HDBASS_N2_0 */
	[16908] = { 0x00FF, 0x00FF, 0x0000 }, /* R16908 - HDBASS_N3_1 */
	[16909] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16909 - HDBASS_N3_0 */
	[16910] = { 0x00FF, 0x00FF, 0x0000 }, /* R16910 - HDBASS_N4_1 */
	[16911] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16911 - HDBASS_N4_0 */
	[16912] = { 0x00FF, 0x00FF, 0x0000 }, /* R16912 - HDBASS_N5_1 */
	[16913] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16913 - HDBASS_N5_0 */
	[16914] = { 0x00FF, 0x00FF, 0x0000 }, /* R16914 - HDBASS_X1_1 */
	[16915] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16915 - HDBASS_X1_0 */
	[16916] = { 0x00FF, 0x00FF, 0x0000 }, /* R16916 - HDBASS_X2_1 */
	[16917] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16917 - HDBASS_X2_0 */
	[16918] = { 0x00FF, 0x00FF, 0x0000 }, /* R16918 - HDBASS_X3_1 */
	[16919] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16919 - HDBASS_X3_0 */
	[16920] = { 0x00FF, 0x00FF, 0x0000 }, /* R16920 - HDBASS_ATK_1 */
	[16921] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16921 - HDBASS_ATK_0 */
	[16922] = { 0x00FF, 0x00FF, 0x0000 }, /* R16922 - HDBASS_DCY_1 */
	[16923] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16923 - HDBASS_DCY_0 */
	[16924] = { 0x00FF, 0x00FF, 0x0000 }, /* R16924 - HDBASS_PG_1 */
	[16925] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R16925 - HDBASS_PG_0 */
	[17408] = { 0x00FF, 0x00FF, 0x0000 }, /* R17408 - HPF_C_1 */
	[17409] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17409 - HPF_C_0 */
	[17920] = { 0x00FF, 0x00FF, 0x0000 }, /* R17920 - ADCL_RETUNE_C1_1 */
	[17921] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17921 - ADCL_RETUNE_C1_0 */
	[17922] = { 0x00FF, 0x00FF, 0x0000 }, /* R17922 - ADCL_RETUNE_C2_1 */
	[17923] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17923 - ADCL_RETUNE_C2_0 */
	[17924] = { 0x00FF, 0x00FF, 0x0000 }, /* R17924 - ADCL_RETUNE_C3_1 */
	[17925] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17925 - ADCL_RETUNE_C3_0 */
	[17926] = { 0x00FF, 0x00FF, 0x0000 }, /* R17926 - ADCL_RETUNE_C4_1 */
	[17927] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17927 - ADCL_RETUNE_C4_0 */
	[17928] = { 0x00FF, 0x00FF, 0x0000 }, /* R17928 - ADCL_RETUNE_C5_1 */
	[17929] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17929 - ADCL_RETUNE_C5_0 */
	[17930] = { 0x00FF, 0x00FF, 0x0000 }, /* R17930 - ADCL_RETUNE_C6_1 */
	[17931] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17931 - ADCL_RETUNE_C6_0 */
	[17932] = { 0x00FF, 0x00FF, 0x0000 }, /* R17932 - ADCL_RETUNE_C7_1 */
	[17933] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17933 - ADCL_RETUNE_C7_0 */
	[17934] = { 0x00FF, 0x00FF, 0x0000 }, /* R17934 - ADCL_RETUNE_C8_1 */
	[17935] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17935 - ADCL_RETUNE_C8_0 */
	[17936] = { 0x00FF, 0x00FF, 0x0000 }, /* R17936 - ADCL_RETUNE_C9_1 */
	[17937] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17937 - ADCL_RETUNE_C9_0 */
	[17938] = { 0x00FF, 0x00FF, 0x0000 }, /* R17938 - ADCL_RETUNE_C10_1 */
	[17939] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17939 - ADCL_RETUNE_C10_0 */
	[17940] = { 0x00FF, 0x00FF, 0x0000 }, /* R17940 - ADCL_RETUNE_C11_1 */
	[17941] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17941 - ADCL_RETUNE_C11_0 */
	[17942] = { 0x00FF, 0x00FF, 0x0000 }, /* R17942 - ADCL_RETUNE_C12_1 */
	[17943] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17943 - ADCL_RETUNE_C12_0 */
	[17944] = { 0x00FF, 0x00FF, 0x0000 }, /* R17944 - ADCL_RETUNE_C13_1 */
	[17945] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17945 - ADCL_RETUNE_C13_0 */
	[17946] = { 0x00FF, 0x00FF, 0x0000 }, /* R17946 - ADCL_RETUNE_C14_1 */
	[17947] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17947 - ADCL_RETUNE_C14_0 */
	[17948] = { 0x00FF, 0x00FF, 0x0000 }, /* R17948 - ADCL_RETUNE_C15_1 */
	[17949] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17949 - ADCL_RETUNE_C15_0 */
	[17950] = { 0x00FF, 0x00FF, 0x0000 }, /* R17950 - ADCL_RETUNE_C16_1 */
	[17951] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17951 - ADCL_RETUNE_C16_0 */
	[17952] = { 0x00FF, 0x00FF, 0x0000 }, /* R17952 - ADCL_RETUNE_C17_1 */
	[17953] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17953 - ADCL_RETUNE_C17_0 */
	[17954] = { 0x00FF, 0x00FF, 0x0000 }, /* R17954 - ADCL_RETUNE_C18_1 */
	[17955] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17955 - ADCL_RETUNE_C18_0 */
	[17956] = { 0x00FF, 0x00FF, 0x0000 }, /* R17956 - ADCL_RETUNE_C19_1 */
	[17957] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17957 - ADCL_RETUNE_C19_0 */
	[17958] = { 0x00FF, 0x00FF, 0x0000 }, /* R17958 - ADCL_RETUNE_C20_1 */
	[17959] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17959 - ADCL_RETUNE_C20_0 */
	[17960] = { 0x00FF, 0x00FF, 0x0000 }, /* R17960 - ADCL_RETUNE_C21_1 */
	[17961] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17961 - ADCL_RETUNE_C21_0 */
	[17962] = { 0x00FF, 0x00FF, 0x0000 }, /* R17962 - ADCL_RETUNE_C22_1 */
	[17963] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17963 - ADCL_RETUNE_C22_0 */
	[17964] = { 0x00FF, 0x00FF, 0x0000 }, /* R17964 - ADCL_RETUNE_C23_1 */
	[17965] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17965 - ADCL_RETUNE_C23_0 */
	[17966] = { 0x00FF, 0x00FF, 0x0000 }, /* R17966 - ADCL_RETUNE_C24_1 */
	[17967] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17967 - ADCL_RETUNE_C24_0 */
	[17968] = { 0x00FF, 0x00FF, 0x0000 }, /* R17968 - ADCL_RETUNE_C25_1 */
	[17969] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17969 - ADCL_RETUNE_C25_0 */
	[17970] = { 0x00FF, 0x00FF, 0x0000 }, /* R17970 - ADCL_RETUNE_C26_1 */
	[17971] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17971 - ADCL_RETUNE_C26_0 */
	[17972] = { 0x00FF, 0x00FF, 0x0000 }, /* R17972 - ADCL_RETUNE_C27_1 */
	[17973] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17973 - ADCL_RETUNE_C27_0 */
	[17974] = { 0x00FF, 0x00FF, 0x0000 }, /* R17974 - ADCL_RETUNE_C28_1 */
	[17975] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17975 - ADCL_RETUNE_C28_0 */
	[17976] = { 0x00FF, 0x00FF, 0x0000 }, /* R17976 - ADCL_RETUNE_C29_1 */
	[17977] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17977 - ADCL_RETUNE_C29_0 */
	[17978] = { 0x00FF, 0x00FF, 0x0000 }, /* R17978 - ADCL_RETUNE_C30_1 */
	[17979] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17979 - ADCL_RETUNE_C30_0 */
	[17980] = { 0x00FF, 0x00FF, 0x0000 }, /* R17980 - ADCL_RETUNE_C31_1 */
	[17981] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17981 - ADCL_RETUNE_C31_0 */
	[17982] = { 0x00FF, 0x00FF, 0x0000 }, /* R17982 - ADCL_RETUNE_C32_1 */
	[17983] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R17983 - ADCL_RETUNE_C32_0 */
	[18432] = { 0x00FF, 0x00FF, 0x0000 }, /* R18432 - RETUNEADC_PG2_1 */
	[18433] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18433 - RETUNEADC_PG2_0 */
	[18434] = { 0x00FF, 0x00FF, 0x0000 }, /* R18434 - RETUNEADC_PG_1 */
	[18435] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18435 - RETUNEADC_PG_0 */
	[18944] = { 0x00FF, 0x00FF, 0x0000 }, /* R18944 - ADCR_RETUNE_C1_1 */
	[18945] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18945 - ADCR_RETUNE_C1_0 */
	[18946] = { 0x00FF, 0x00FF, 0x0000 }, /* R18946 - ADCR_RETUNE_C2_1 */
	[18947] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18947 - ADCR_RETUNE_C2_0 */
	[18948] = { 0x00FF, 0x00FF, 0x0000 }, /* R18948 - ADCR_RETUNE_C3_1 */
	[18949] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18949 - ADCR_RETUNE_C3_0 */
	[18950] = { 0x00FF, 0x00FF, 0x0000 }, /* R18950 - ADCR_RETUNE_C4_1 */
	[18951] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18951 - ADCR_RETUNE_C4_0 */
	[18952] = { 0x00FF, 0x00FF, 0x0000 }, /* R18952 - ADCR_RETUNE_C5_1 */
	[18953] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18953 - ADCR_RETUNE_C5_0 */
	[18954] = { 0x00FF, 0x00FF, 0x0000 }, /* R18954 - ADCR_RETUNE_C6_1 */
	[18955] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18955 - ADCR_RETUNE_C6_0 */
	[18956] = { 0x00FF, 0x00FF, 0x0000 }, /* R18956 - ADCR_RETUNE_C7_1 */
	[18957] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18957 - ADCR_RETUNE_C7_0 */
	[18958] = { 0x00FF, 0x00FF, 0x0000 }, /* R18958 - ADCR_RETUNE_C8_1 */
	[18959] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18959 - ADCR_RETUNE_C8_0 */
	[18960] = { 0x00FF, 0x00FF, 0x0000 }, /* R18960 - ADCR_RETUNE_C9_1 */
	[18961] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18961 - ADCR_RETUNE_C9_0 */
	[18962] = { 0x00FF, 0x00FF, 0x0000 }, /* R18962 - ADCR_RETUNE_C10_1 */
	[18963] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18963 - ADCR_RETUNE_C10_0 */
	[18964] = { 0x00FF, 0x00FF, 0x0000 }, /* R18964 - ADCR_RETUNE_C11_1 */
	[18965] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18965 - ADCR_RETUNE_C11_0 */
	[18966] = { 0x00FF, 0x00FF, 0x0000 }, /* R18966 - ADCR_RETUNE_C12_1 */
	[18967] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18967 - ADCR_RETUNE_C12_0 */
	[18968] = { 0x00FF, 0x00FF, 0x0000 }, /* R18968 - ADCR_RETUNE_C13_1 */
	[18969] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18969 - ADCR_RETUNE_C13_0 */
	[18970] = { 0x00FF, 0x00FF, 0x0000 }, /* R18970 - ADCR_RETUNE_C14_1 */
	[18971] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18971 - ADCR_RETUNE_C14_0 */
	[18972] = { 0x00FF, 0x00FF, 0x0000 }, /* R18972 - ADCR_RETUNE_C15_1 */
	[18973] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18973 - ADCR_RETUNE_C15_0 */
	[18974] = { 0x00FF, 0x00FF, 0x0000 }, /* R18974 - ADCR_RETUNE_C16_1 */
	[18975] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18975 - ADCR_RETUNE_C16_0 */
	[18976] = { 0x00FF, 0x00FF, 0x0000 }, /* R18976 - ADCR_RETUNE_C17_1 */
	[18977] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18977 - ADCR_RETUNE_C17_0 */
	[18978] = { 0x00FF, 0x00FF, 0x0000 }, /* R18978 - ADCR_RETUNE_C18_1 */
	[18979] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18979 - ADCR_RETUNE_C18_0 */
	[18980] = { 0x00FF, 0x00FF, 0x0000 }, /* R18980 - ADCR_RETUNE_C19_1 */
	[18981] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18981 - ADCR_RETUNE_C19_0 */
	[18982] = { 0x00FF, 0x00FF, 0x0000 }, /* R18982 - ADCR_RETUNE_C20_1 */
	[18983] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18983 - ADCR_RETUNE_C20_0 */
	[18984] = { 0x00FF, 0x00FF, 0x0000 }, /* R18984 - ADCR_RETUNE_C21_1 */
	[18985] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18985 - ADCR_RETUNE_C21_0 */
	[18986] = { 0x00FF, 0x00FF, 0x0000 }, /* R18986 - ADCR_RETUNE_C22_1 */
	[18987] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18987 - ADCR_RETUNE_C22_0 */
	[18988] = { 0x00FF, 0x00FF, 0x0000 }, /* R18988 - ADCR_RETUNE_C23_1 */
	[18989] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18989 - ADCR_RETUNE_C23_0 */
	[18990] = { 0x00FF, 0x00FF, 0x0000 }, /* R18990 - ADCR_RETUNE_C24_1 */
	[18991] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18991 - ADCR_RETUNE_C24_0 */
	[18992] = { 0x00FF, 0x00FF, 0x0000 }, /* R18992 - ADCR_RETUNE_C25_1 */
	[18993] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18993 - ADCR_RETUNE_C25_0 */
	[18994] = { 0x00FF, 0x00FF, 0x0000 }, /* R18994 - ADCR_RETUNE_C26_1 */
	[18995] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18995 - ADCR_RETUNE_C26_0 */
	[18996] = { 0x00FF, 0x00FF, 0x0000 }, /* R18996 - ADCR_RETUNE_C27_1 */
	[18997] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18997 - ADCR_RETUNE_C27_0 */
	[18998] = { 0x00FF, 0x00FF, 0x0000 }, /* R18998 - ADCR_RETUNE_C28_1 */
	[18999] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R18999 - ADCR_RETUNE_C28_0 */
	[19000] = { 0x00FF, 0x00FF, 0x0000 }, /* R19000 - ADCR_RETUNE_C29_1 */
	[19001] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19001 - ADCR_RETUNE_C29_0 */
	[19002] = { 0x00FF, 0x00FF, 0x0000 }, /* R19002 - ADCR_RETUNE_C30_1 */
	[19003] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19003 - ADCR_RETUNE_C30_0 */
	[19004] = { 0x00FF, 0x00FF, 0x0000 }, /* R19004 - ADCR_RETUNE_C31_1 */
	[19005] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19005 - ADCR_RETUNE_C31_0 */
	[19006] = { 0x00FF, 0x00FF, 0x0000 }, /* R19006 - ADCR_RETUNE_C32_1 */
	[19007] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19007 - ADCR_RETUNE_C32_0 */
	[19456] = { 0x00FF, 0x00FF, 0x0000 }, /* R19456 - DACL_RETUNE_C1_1 */
	[19457] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19457 - DACL_RETUNE_C1_0 */
	[19458] = { 0x00FF, 0x00FF, 0x0000 }, /* R19458 - DACL_RETUNE_C2_1 */
	[19459] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19459 - DACL_RETUNE_C2_0 */
	[19460] = { 0x00FF, 0x00FF, 0x0000 }, /* R19460 - DACL_RETUNE_C3_1 */
	[19461] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19461 - DACL_RETUNE_C3_0 */
	[19462] = { 0x00FF, 0x00FF, 0x0000 }, /* R19462 - DACL_RETUNE_C4_1 */
	[19463] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19463 - DACL_RETUNE_C4_0 */
	[19464] = { 0x00FF, 0x00FF, 0x0000 }, /* R19464 - DACL_RETUNE_C5_1 */
	[19465] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19465 - DACL_RETUNE_C5_0 */
	[19466] = { 0x00FF, 0x00FF, 0x0000 }, /* R19466 - DACL_RETUNE_C6_1 */
	[19467] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19467 - DACL_RETUNE_C6_0 */
	[19468] = { 0x00FF, 0x00FF, 0x0000 }, /* R19468 - DACL_RETUNE_C7_1 */
	[19469] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19469 - DACL_RETUNE_C7_0 */
	[19470] = { 0x00FF, 0x00FF, 0x0000 }, /* R19470 - DACL_RETUNE_C8_1 */
	[19471] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19471 - DACL_RETUNE_C8_0 */
	[19472] = { 0x00FF, 0x00FF, 0x0000 }, /* R19472 - DACL_RETUNE_C9_1 */
	[19473] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19473 - DACL_RETUNE_C9_0 */
	[19474] = { 0x00FF, 0x00FF, 0x0000 }, /* R19474 - DACL_RETUNE_C10_1 */
	[19475] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19475 - DACL_RETUNE_C10_0 */
	[19476] = { 0x00FF, 0x00FF, 0x0000 }, /* R19476 - DACL_RETUNE_C11_1 */
	[19477] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19477 - DACL_RETUNE_C11_0 */
	[19478] = { 0x00FF, 0x00FF, 0x0000 }, /* R19478 - DACL_RETUNE_C12_1 */
	[19479] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19479 - DACL_RETUNE_C12_0 */
	[19480] = { 0x00FF, 0x00FF, 0x0000 }, /* R19480 - DACL_RETUNE_C13_1 */
	[19481] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19481 - DACL_RETUNE_C13_0 */
	[19482] = { 0x00FF, 0x00FF, 0x0000 }, /* R19482 - DACL_RETUNE_C14_1 */
	[19483] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19483 - DACL_RETUNE_C14_0 */
	[19484] = { 0x00FF, 0x00FF, 0x0000 }, /* R19484 - DACL_RETUNE_C15_1 */
	[19485] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19485 - DACL_RETUNE_C15_0 */
	[19486] = { 0x00FF, 0x00FF, 0x0000 }, /* R19486 - DACL_RETUNE_C16_1 */
	[19487] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19487 - DACL_RETUNE_C16_0 */
	[19488] = { 0x00FF, 0x00FF, 0x0000 }, /* R19488 - DACL_RETUNE_C17_1 */
	[19489] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19489 - DACL_RETUNE_C17_0 */
	[19490] = { 0x00FF, 0x00FF, 0x0000 }, /* R19490 - DACL_RETUNE_C18_1 */
	[19491] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19491 - DACL_RETUNE_C18_0 */
	[19492] = { 0x00FF, 0x00FF, 0x0000 }, /* R19492 - DACL_RETUNE_C19_1 */
	[19493] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19493 - DACL_RETUNE_C19_0 */
	[19494] = { 0x00FF, 0x00FF, 0x0000 }, /* R19494 - DACL_RETUNE_C20_1 */
	[19495] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19495 - DACL_RETUNE_C20_0 */
	[19496] = { 0x00FF, 0x00FF, 0x0000 }, /* R19496 - DACL_RETUNE_C21_1 */
	[19497] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19497 - DACL_RETUNE_C21_0 */
	[19498] = { 0x00FF, 0x00FF, 0x0000 }, /* R19498 - DACL_RETUNE_C22_1 */
	[19499] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19499 - DACL_RETUNE_C22_0 */
	[19500] = { 0x00FF, 0x00FF, 0x0000 }, /* R19500 - DACL_RETUNE_C23_1 */
	[19501] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19501 - DACL_RETUNE_C23_0 */
	[19502] = { 0x00FF, 0x00FF, 0x0000 }, /* R19502 - DACL_RETUNE_C24_1 */
	[19503] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19503 - DACL_RETUNE_C24_0 */
	[19504] = { 0x00FF, 0x00FF, 0x0000 }, /* R19504 - DACL_RETUNE_C25_1 */
	[19505] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19505 - DACL_RETUNE_C25_0 */
	[19506] = { 0x00FF, 0x00FF, 0x0000 }, /* R19506 - DACL_RETUNE_C26_1 */
	[19507] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19507 - DACL_RETUNE_C26_0 */
	[19508] = { 0x00FF, 0x00FF, 0x0000 }, /* R19508 - DACL_RETUNE_C27_1 */
	[19509] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19509 - DACL_RETUNE_C27_0 */
	[19510] = { 0x00FF, 0x00FF, 0x0000 }, /* R19510 - DACL_RETUNE_C28_1 */
	[19511] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19511 - DACL_RETUNE_C28_0 */
	[19512] = { 0x00FF, 0x00FF, 0x0000 }, /* R19512 - DACL_RETUNE_C29_1 */
	[19513] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19513 - DACL_RETUNE_C29_0 */
	[19514] = { 0x00FF, 0x00FF, 0x0000 }, /* R19514 - DACL_RETUNE_C30_1 */
	[19515] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19515 - DACL_RETUNE_C30_0 */
	[19516] = { 0x00FF, 0x00FF, 0x0000 }, /* R19516 - DACL_RETUNE_C31_1 */
	[19517] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19517 - DACL_RETUNE_C31_0 */
	[19518] = { 0x00FF, 0x00FF, 0x0000 }, /* R19518 - DACL_RETUNE_C32_1 */
	[19519] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19519 - DACL_RETUNE_C32_0 */
	[19968] = { 0x00FF, 0x00FF, 0x0000 }, /* R19968 - RETUNEDAC_PG2_1 */
	[19969] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19969 - RETUNEDAC_PG2_0 */
	[19970] = { 0x00FF, 0x00FF, 0x0000 }, /* R19970 - RETUNEDAC_PG_1 */
	[19971] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R19971 - RETUNEDAC_PG_0 */
	[20480] = { 0x00FF, 0x00FF, 0x0000 }, /* R20480 - DACR_RETUNE_C1_1 */
	[20481] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20481 - DACR_RETUNE_C1_0 */
	[20482] = { 0x00FF, 0x00FF, 0x0000 }, /* R20482 - DACR_RETUNE_C2_1 */
	[20483] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20483 - DACR_RETUNE_C2_0 */
	[20484] = { 0x00FF, 0x00FF, 0x0000 }, /* R20484 - DACR_RETUNE_C3_1 */
	[20485] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20485 - DACR_RETUNE_C3_0 */
	[20486] = { 0x00FF, 0x00FF, 0x0000 }, /* R20486 - DACR_RETUNE_C4_1 */
	[20487] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20487 - DACR_RETUNE_C4_0 */
	[20488] = { 0x00FF, 0x00FF, 0x0000 }, /* R20488 - DACR_RETUNE_C5_1 */
	[20489] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20489 - DACR_RETUNE_C5_0 */
	[20490] = { 0x00FF, 0x00FF, 0x0000 }, /* R20490 - DACR_RETUNE_C6_1 */
	[20491] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20491 - DACR_RETUNE_C6_0 */
	[20492] = { 0x00FF, 0x00FF, 0x0000 }, /* R20492 - DACR_RETUNE_C7_1 */
	[20493] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20493 - DACR_RETUNE_C7_0 */
	[20494] = { 0x00FF, 0x00FF, 0x0000 }, /* R20494 - DACR_RETUNE_C8_1 */
	[20495] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20495 - DACR_RETUNE_C8_0 */
	[20496] = { 0x00FF, 0x00FF, 0x0000 }, /* R20496 - DACR_RETUNE_C9_1 */
	[20497] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20497 - DACR_RETUNE_C9_0 */
	[20498] = { 0x00FF, 0x00FF, 0x0000 }, /* R20498 - DACR_RETUNE_C10_1 */
	[20499] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20499 - DACR_RETUNE_C10_0 */
	[20500] = { 0x00FF, 0x00FF, 0x0000 }, /* R20500 - DACR_RETUNE_C11_1 */
	[20501] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20501 - DACR_RETUNE_C11_0 */
	[20502] = { 0x00FF, 0x00FF, 0x0000 }, /* R20502 - DACR_RETUNE_C12_1 */
	[20503] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20503 - DACR_RETUNE_C12_0 */
	[20504] = { 0x00FF, 0x00FF, 0x0000 }, /* R20504 - DACR_RETUNE_C13_1 */
	[20505] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20505 - DACR_RETUNE_C13_0 */
	[20506] = { 0x00FF, 0x00FF, 0x0000 }, /* R20506 - DACR_RETUNE_C14_1 */
	[20507] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20507 - DACR_RETUNE_C14_0 */
	[20508] = { 0x00FF, 0x00FF, 0x0000 }, /* R20508 - DACR_RETUNE_C15_1 */
	[20509] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20509 - DACR_RETUNE_C15_0 */
	[20510] = { 0x00FF, 0x00FF, 0x0000 }, /* R20510 - DACR_RETUNE_C16_1 */
	[20511] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20511 - DACR_RETUNE_C16_0 */
	[20512] = { 0x00FF, 0x00FF, 0x0000 }, /* R20512 - DACR_RETUNE_C17_1 */
	[20513] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20513 - DACR_RETUNE_C17_0 */
	[20514] = { 0x00FF, 0x00FF, 0x0000 }, /* R20514 - DACR_RETUNE_C18_1 */
	[20515] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20515 - DACR_RETUNE_C18_0 */
	[20516] = { 0x00FF, 0x00FF, 0x0000 }, /* R20516 - DACR_RETUNE_C19_1 */
	[20517] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20517 - DACR_RETUNE_C19_0 */
	[20518] = { 0x00FF, 0x00FF, 0x0000 }, /* R20518 - DACR_RETUNE_C20_1 */
	[20519] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20519 - DACR_RETUNE_C20_0 */
	[20520] = { 0x00FF, 0x00FF, 0x0000 }, /* R20520 - DACR_RETUNE_C21_1 */
	[20521] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20521 - DACR_RETUNE_C21_0 */
	[20522] = { 0x00FF, 0x00FF, 0x0000 }, /* R20522 - DACR_RETUNE_C22_1 */
	[20523] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20523 - DACR_RETUNE_C22_0 */
	[20524] = { 0x00FF, 0x00FF, 0x0000 }, /* R20524 - DACR_RETUNE_C23_1 */
	[20525] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20525 - DACR_RETUNE_C23_0 */
	[20526] = { 0x00FF, 0x00FF, 0x0000 }, /* R20526 - DACR_RETUNE_C24_1 */
	[20527] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20527 - DACR_RETUNE_C24_0 */
	[20528] = { 0x00FF, 0x00FF, 0x0000 }, /* R20528 - DACR_RETUNE_C25_1 */
	[20529] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20529 - DACR_RETUNE_C25_0 */
	[20530] = { 0x00FF, 0x00FF, 0x0000 }, /* R20530 - DACR_RETUNE_C26_1 */
	[20531] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20531 - DACR_RETUNE_C26_0 */
	[20532] = { 0x00FF, 0x00FF, 0x0000 }, /* R20532 - DACR_RETUNE_C27_1 */
	[20533] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20533 - DACR_RETUNE_C27_0 */
	[20534] = { 0x00FF, 0x00FF, 0x0000 }, /* R20534 - DACR_RETUNE_C28_1 */
	[20535] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20535 - DACR_RETUNE_C28_0 */
	[20536] = { 0x00FF, 0x00FF, 0x0000 }, /* R20536 - DACR_RETUNE_C29_1 */
	[20537] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20537 - DACR_RETUNE_C29_0 */
	[20538] = { 0x00FF, 0x00FF, 0x0000 }, /* R20538 - DACR_RETUNE_C30_1 */
	[20539] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20539 - DACR_RETUNE_C30_0 */
	[20540] = { 0x00FF, 0x00FF, 0x0000 }, /* R20540 - DACR_RETUNE_C31_1 */
	[20541] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20541 - DACR_RETUNE_C31_0 */
	[20542] = { 0x00FF, 0x00FF, 0x0000 }, /* R20542 - DACR_RETUNE_C32_1 */
	[20543] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20543 - DACR_RETUNE_C32_0 */
	[20992] = { 0x00FF, 0x00FF, 0x0000 }, /* R20992 - VSS_XHD2_1 */
	[20993] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20993 - VSS_XHD2_0 */
	[20994] = { 0x00FF, 0x00FF, 0x0000 }, /* R20994 - VSS_XHD3_1 */
	[20995] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20995 - VSS_XHD3_0 */
	[20996] = { 0x00FF, 0x00FF, 0x0000 }, /* R20996 - VSS_XHN1_1 */
	[20997] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20997 - VSS_XHN1_0 */
	[20998] = { 0x00FF, 0x00FF, 0x0000 }, /* R20998 - VSS_XHN2_1 */
	[20999] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R20999 - VSS_XHN2_0 */
	[21000] = { 0x00FF, 0x00FF, 0x0000 }, /* R21000 - VSS_XHN3_1 */
	[21001] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21001 - VSS_XHN3_0 */
	[21002] = { 0x00FF, 0x00FF, 0x0000 }, /* R21002 - VSS_XLA_1 */
	[21003] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21003 - VSS_XLA_0 */
	[21004] = { 0x00FF, 0x00FF, 0x0000 }, /* R21004 - VSS_XLB_1 */
	[21005] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21005 - VSS_XLB_0 */
	[21006] = { 0x00FF, 0x00FF, 0x0000 }, /* R21006 - VSS_XLG_1 */
	[21007] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21007 - VSS_XLG_0 */
	[21008] = { 0x00FF, 0x00FF, 0x0000 }, /* R21008 - VSS_PG2_1 */
	[21009] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21009 - VSS_PG2_0 */
	[21010] = { 0x00FF, 0x00FF, 0x0000 }, /* R21010 - VSS_PG_1 */
	[21011] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21011 - VSS_PG_0 */
	[21012] = { 0x00FF, 0x00FF, 0x0000 }, /* R21012 - VSS_XTD1_1 */
	[21013] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21013 - VSS_XTD1_0 */
	[21014] = { 0x00FF, 0x00FF, 0x0000 }, /* R21014 - VSS_XTD2_1 */
	[21015] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21015 - VSS_XTD2_0 */
	[21016] = { 0x00FF, 0x00FF, 0x0000 }, /* R21016 - VSS_XTD3_1 */
	[21017] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21017 - VSS_XTD3_0 */
	[21018] = { 0x00FF, 0x00FF, 0x0000 }, /* R21018 - VSS_XTD4_1 */
	[21019] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21019 - VSS_XTD4_0 */
	[21020] = { 0x00FF, 0x00FF, 0x0000 }, /* R21020 - VSS_XTD5_1 */
	[21021] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21021 - VSS_XTD5_0 */
	[21022] = { 0x00FF, 0x00FF, 0x0000 }, /* R21022 - VSS_XTD6_1 */
	[21023] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21023 - VSS_XTD6_0 */
	[21024] = { 0x00FF, 0x00FF, 0x0000 }, /* R21024 - VSS_XTD7_1 */
	[21025] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21025 - VSS_XTD7_0 */
	[21026] = { 0x00FF, 0x00FF, 0x0000 }, /* R21026 - VSS_XTD8_1 */
	[21027] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21027 - VSS_XTD8_0 */
	[21028] = { 0x00FF, 0x00FF, 0x0000 }, /* R21028 - VSS_XTD9_1 */
	[21029] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21029 - VSS_XTD9_0 */
	[21030] = { 0x00FF, 0x00FF, 0x0000 }, /* R21030 - VSS_XTD10_1 */
	[21031] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21031 - VSS_XTD10_0 */
	[21032] = { 0x00FF, 0x00FF, 0x0000 }, /* R21032 - VSS_XTD11_1 */
	[21033] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21033 - VSS_XTD11_0 */
	[21034] = { 0x00FF, 0x00FF, 0x0000 }, /* R21034 - VSS_XTD12_1 */
	[21035] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21035 - VSS_XTD12_0 */
	[21036] = { 0x00FF, 0x00FF, 0x0000 }, /* R21036 - VSS_XTD13_1 */
	[21037] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21037 - VSS_XTD13_0 */
	[21038] = { 0x00FF, 0x00FF, 0x0000 }, /* R21038 - VSS_XTD14_1 */
	[21039] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21039 - VSS_XTD14_0 */
	[21040] = { 0x00FF, 0x00FF, 0x0000 }, /* R21040 - VSS_XTD15_1 */
	[21041] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21041 - VSS_XTD15_0 */
	[21042] = { 0x00FF, 0x00FF, 0x0000 }, /* R21042 - VSS_XTD16_1 */
	[21043] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21043 - VSS_XTD16_0 */
	[21044] = { 0x00FF, 0x00FF, 0x0000 }, /* R21044 - VSS_XTD17_1 */
	[21045] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21045 - VSS_XTD17_0 */
	[21046] = { 0x00FF, 0x00FF, 0x0000 }, /* R21046 - VSS_XTD18_1 */
	[21047] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21047 - VSS_XTD18_0 */
	[21048] = { 0x00FF, 0x00FF, 0x0000 }, /* R21048 - VSS_XTD19_1 */
	[21049] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21049 - VSS_XTD19_0 */
	[21050] = { 0x00FF, 0x00FF, 0x0000 }, /* R21050 - VSS_XTD20_1 */
	[21051] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21051 - VSS_XTD20_0 */
	[21052] = { 0x00FF, 0x00FF, 0x0000 }, /* R21052 - VSS_XTD21_1 */
	[21053] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21053 - VSS_XTD21_0 */
	[21054] = { 0x00FF, 0x00FF, 0x0000 }, /* R21054 - VSS_XTD22_1 */
	[21055] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21055 - VSS_XTD22_0 */
	[21056] = { 0x00FF, 0x00FF, 0x0000 }, /* R21056 - VSS_XTD23_1 */
	[21057] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21057 - VSS_XTD23_0 */
	[21058] = { 0x00FF, 0x00FF, 0x0000 }, /* R21058 - VSS_XTD24_1 */
	[21059] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21059 - VSS_XTD24_0 */
	[21060] = { 0x00FF, 0x00FF, 0x0000 }, /* R21060 - VSS_XTD25_1 */
	[21061] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21061 - VSS_XTD25_0 */
	[21062] = { 0x00FF, 0x00FF, 0x0000 }, /* R21062 - VSS_XTD26_1 */
	[21063] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21063 - VSS_XTD26_0 */
	[21064] = { 0x00FF, 0x00FF, 0x0000 }, /* R21064 - VSS_XTD27_1 */
	[21065] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21065 - VSS_XTD27_0 */
	[21066] = { 0x00FF, 0x00FF, 0x0000 }, /* R21066 - VSS_XTD28_1 */
	[21067] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21067 - VSS_XTD28_0 */
	[21068] = { 0x00FF, 0x00FF, 0x0000 }, /* R21068 - VSS_XTD29_1 */
	[21069] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21069 - VSS_XTD29_0 */
	[21070] = { 0x00FF, 0x00FF, 0x0000 }, /* R21070 - VSS_XTD30_1 */
	[21071] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21071 - VSS_XTD30_0 */
	[21072] = { 0x00FF, 0x00FF, 0x0000 }, /* R21072 - VSS_XTD31_1 */
	[21073] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21073 - VSS_XTD31_0 */
	[21074] = { 0x00FF, 0x00FF, 0x0000 }, /* R21074 - VSS_XTD32_1 */
	[21075] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21075 - VSS_XTD32_0 */
	[21076] = { 0x00FF, 0x00FF, 0x0000 }, /* R21076 - VSS_XTS1_1 */
	[21077] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21077 - VSS_XTS1_0 */
	[21078] = { 0x00FF, 0x00FF, 0x0000 }, /* R21078 - VSS_XTS2_1 */
	[21079] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21079 - VSS_XTS2_0 */
	[21080] = { 0x00FF, 0x00FF, 0x0000 }, /* R21080 - VSS_XTS3_1 */
	[21081] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21081 - VSS_XTS3_0 */
	[21082] = { 0x00FF, 0x00FF, 0x0000 }, /* R21082 - VSS_XTS4_1 */
	[21083] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21083 - VSS_XTS4_0 */
	[21084] = { 0x00FF, 0x00FF, 0x0000 }, /* R21084 - VSS_XTS5_1 */
	[21085] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21085 - VSS_XTS5_0 */
	[21086] = { 0x00FF, 0x00FF, 0x0000 }, /* R21086 - VSS_XTS6_1 */
	[21087] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21087 - VSS_XTS6_0 */
	[21088] = { 0x00FF, 0x00FF, 0x0000 }, /* R21088 - VSS_XTS7_1 */
	[21089] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21089 - VSS_XTS7_0 */
	[21090] = { 0x00FF, 0x00FF, 0x0000 }, /* R21090 - VSS_XTS8_1 */
	[21091] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21091 - VSS_XTS8_0 */
	[21092] = { 0x00FF, 0x00FF, 0x0000 }, /* R21092 - VSS_XTS9_1 */
	[21093] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21093 - VSS_XTS9_0 */
	[21094] = { 0x00FF, 0x00FF, 0x0000 }, /* R21094 - VSS_XTS10_1 */
	[21095] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21095 - VSS_XTS10_0 */
	[21096] = { 0x00FF, 0x00FF, 0x0000 }, /* R21096 - VSS_XTS11_1 */
	[21097] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21097 - VSS_XTS11_0 */
	[21098] = { 0x00FF, 0x00FF, 0x0000 }, /* R21098 - VSS_XTS12_1 */
	[21099] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21099 - VSS_XTS12_0 */
	[21100] = { 0x00FF, 0x00FF, 0x0000 }, /* R21100 - VSS_XTS13_1 */
	[21101] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21101 - VSS_XTS13_0 */
	[21102] = { 0x00FF, 0x00FF, 0x0000 }, /* R21102 - VSS_XTS14_1 */
	[21103] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21103 - VSS_XTS14_0 */
	[21104] = { 0x00FF, 0x00FF, 0x0000 }, /* R21104 - VSS_XTS15_1 */
	[21105] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21105 - VSS_XTS15_0 */
	[21106] = { 0x00FF, 0x00FF, 0x0000 }, /* R21106 - VSS_XTS16_1 */
	[21107] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21107 - VSS_XTS16_0 */
	[21108] = { 0x00FF, 0x00FF, 0x0000 }, /* R21108 - VSS_XTS17_1 */
	[21109] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21109 - VSS_XTS17_0 */
	[21110] = { 0x00FF, 0x00FF, 0x0000 }, /* R21110 - VSS_XTS18_1 */
	[21111] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21111 - VSS_XTS18_0 */
	[21112] = { 0x00FF, 0x00FF, 0x0000 }, /* R21112 - VSS_XTS19_1 */
	[21113] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21113 - VSS_XTS19_0 */
	[21114] = { 0x00FF, 0x00FF, 0x0000 }, /* R21114 - VSS_XTS20_1 */
	[21115] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21115 - VSS_XTS20_0 */
	[21116] = { 0x00FF, 0x00FF, 0x0000 }, /* R21116 - VSS_XTS21_1 */
	[21117] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21117 - VSS_XTS21_0 */
	[21118] = { 0x00FF, 0x00FF, 0x0000 }, /* R21118 - VSS_XTS22_1 */
	[21119] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21119 - VSS_XTS22_0 */
	[21120] = { 0x00FF, 0x00FF, 0x0000 }, /* R21120 - VSS_XTS23_1 */
	[21121] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21121 - VSS_XTS23_0 */
	[21122] = { 0x00FF, 0x00FF, 0x0000 }, /* R21122 - VSS_XTS24_1 */
	[21123] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21123 - VSS_XTS24_0 */
	[21124] = { 0x00FF, 0x00FF, 0x0000 }, /* R21124 - VSS_XTS25_1 */
	[21125] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21125 - VSS_XTS25_0 */
	[21126] = { 0x00FF, 0x00FF, 0x0000 }, /* R21126 - VSS_XTS26_1 */
	[21127] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21127 - VSS_XTS26_0 */
	[21128] = { 0x00FF, 0x00FF, 0x0000 }, /* R21128 - VSS_XTS27_1 */
	[21129] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21129 - VSS_XTS27_0 */
	[21130] = { 0x00FF, 0x00FF, 0x0000 }, /* R21130 - VSS_XTS28_1 */
	[21131] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21131 - VSS_XTS28_0 */
	[21132] = { 0x00FF, 0x00FF, 0x0000 }, /* R21132 - VSS_XTS29_1 */
	[21133] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21133 - VSS_XTS29_0 */
	[21134] = { 0x00FF, 0x00FF, 0x0000 }, /* R21134 - VSS_XTS30_1 */
	[21135] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21135 - VSS_XTS30_0 */
	[21136] = { 0x00FF, 0x00FF, 0x0000 }, /* R21136 - VSS_XTS31_1 */
	[21137] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21137 - VSS_XTS31_0 */
	[21138] = { 0x00FF, 0x00FF, 0x0000 }, /* R21138 - VSS_XTS32_1 */
	[21139] = { 0xFFFF, 0xFFFF, 0x0000 }, /* R21139 - VSS_XTS32_0 */
};

static int wm8962_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	if (wm8962_reg_access[reg].vol)
		return 1;
	else
		return 0;
}

static int wm8962_readable_register(struct snd_soc_codec *codec, unsigned int reg)
{
	if (wm8962_reg_access[reg].read)
		return 1;
	else
		return 0;
}

static int wm8962_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8962_SOFTWARE_RESET, 0x6243);
}

static const DECLARE_TLV_DB_SCALE(inpga_tlv, -2325, 75, 0);
static const DECLARE_TLV_DB_SCALE(mixin_tlv, -1500, 300, 0);
static const unsigned int mixinpga_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 1, TLV_DB_SCALE_ITEM(0, 600, 0),
	2, 2, TLV_DB_SCALE_ITEM(1300, 1300, 0),
	3, 4, TLV_DB_SCALE_ITEM(1800, 200, 0),
	5, 5, TLV_DB_SCALE_ITEM(2400, 0, 0),
	6, 7, TLV_DB_SCALE_ITEM(2700, 300, 0),
};
static const DECLARE_TLV_DB_SCALE(beep_tlv, -9600, 600, 1);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(st_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(inmix_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(hp_tlv, -700, 100, 0);
static const unsigned int classd_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 6, TLV_DB_SCALE_ITEM(0, 150, 0),
	7, 7, TLV_DB_SCALE_ITEM(1200, 0, 0),
};

/* The VU bits for the headphones are in a different register to the mute
 * bits and only take effect on the PGA if it is actually powered.
 */
static int wm8962_put_hp_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 *reg_cache = codec->reg_cache;
	int ret;

	/* Apply the update (if any) */
        ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret == 0)
		return 0;

	/* If the left PGA is enabled hit that VU bit... */
	if (snd_soc_read(codec, WM8962_PWR_MGMT_2) & WM8962_HPOUTL_PGA_ENA)
		return snd_soc_write(codec, WM8962_HPOUTL_VOLUME,
				     reg_cache[WM8962_HPOUTL_VOLUME]);

	/* ...otherwise the right.  The VU is stereo. */
	if (snd_soc_read(codec, WM8962_PWR_MGMT_2) & WM8962_HPOUTR_PGA_ENA)
		return snd_soc_write(codec, WM8962_HPOUTR_VOLUME,
				     reg_cache[WM8962_HPOUTR_VOLUME]);

	return 0;
}

/* The VU bits for the speakers are in a different register to the mute
 * bits and only take effect on the PGA if it is actually powered.
 */
static int wm8962_put_spk_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 *reg_cache = codec->reg_cache;
	int ret;

	/* Apply the update (if any) */
        ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret == 0)
		return 0;

	/* If the left PGA is enabled hit that VU bit... */
	if (reg_cache[WM8962_PWR_MGMT_2] & WM8962_SPKOUTL_PGA_ENA)
		return snd_soc_write(codec, WM8962_SPKOUTL_VOLUME,
				     reg_cache[WM8962_SPKOUTL_VOLUME]);

	/* ...otherwise the right.  The VU is stereo. */
	if (reg_cache[WM8962_PWR_MGMT_2] & WM8962_SPKOUTR_PGA_ENA)
		return snd_soc_write(codec, WM8962_SPKOUTR_VOLUME,
				     reg_cache[WM8962_SPKOUTR_VOLUME]);

	return 0;
}

static const char *cap_hpf_mode_text[] = {
	"Hi-fi", "Application"
};

static const struct soc_enum cap_hpf_mode =
	SOC_ENUM_SINGLE(WM8962_ADC_DAC_CONTROL_2, 10, 2, cap_hpf_mode_text);

static const struct snd_kcontrol_new wm8962_snd_controls[] = {
SOC_DOUBLE("Input Mixer Switch", WM8962_INPUT_MIXER_CONTROL_1, 3, 2, 1, 1),

SOC_SINGLE_TLV("MIXINL IN2L Volume", WM8962_LEFT_INPUT_MIXER_VOLUME, 6, 7, 0,
	       mixin_tlv),
SOC_SINGLE_TLV("MIXINL PGA Volume", WM8962_LEFT_INPUT_MIXER_VOLUME, 3, 7, 0,
	       mixinpga_tlv),
SOC_SINGLE_TLV("MIXINL IN3L Volume", WM8962_LEFT_INPUT_MIXER_VOLUME, 0, 7, 0,
	       mixin_tlv),

SOC_SINGLE_TLV("MIXINR IN2R Volume", WM8962_RIGHT_INPUT_MIXER_VOLUME, 6, 7, 0,
	       mixin_tlv),
SOC_SINGLE_TLV("MIXINR PGA Volume", WM8962_RIGHT_INPUT_MIXER_VOLUME, 3, 7, 0,
	       mixinpga_tlv),
SOC_SINGLE_TLV("MIXINR IN3R Volume", WM8962_RIGHT_INPUT_MIXER_VOLUME, 0, 7, 0,
	       mixin_tlv),

SOC_DOUBLE_R_TLV("Digital Capture Volume", WM8962_LEFT_ADC_VOLUME,
		 WM8962_RIGHT_ADC_VOLUME, 1, 127, 0, digital_tlv),
SOC_DOUBLE_R_TLV("Capture Volume", WM8962_LEFT_INPUT_VOLUME,
		 WM8962_RIGHT_INPUT_VOLUME, 0, 63, 0, inpga_tlv),
SOC_DOUBLE_R("Capture Switch", WM8962_LEFT_INPUT_VOLUME,
	     WM8962_RIGHT_INPUT_VOLUME, 7, 1, 1),
SOC_DOUBLE_R("Capture ZC Switch", WM8962_LEFT_INPUT_VOLUME,
	     WM8962_RIGHT_INPUT_VOLUME, 6, 1, 1),
SOC_SINGLE("Capture HPF Switch", WM8962_ADC_DAC_CONTROL_1, 0, 1, 1),
SOC_ENUM("Capture HPF Mode", cap_hpf_mode),
SOC_SINGLE("Capture HPF Cutoff", WM8962_ADC_DAC_CONTROL_2, 7, 7, 0),

SOC_DOUBLE_R_TLV("Sidetone Volume", WM8962_DAC_DSP_MIXING_1,
		 WM8962_DAC_DSP_MIXING_2, 4, 12, 0, st_tlv),

SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8962_LEFT_DAC_VOLUME,
		 WM8962_RIGHT_DAC_VOLUME, 1, 127, 0, digital_tlv),
SOC_SINGLE("DAC High Performance Switch", WM8962_ADC_DAC_CONTROL_2, 0, 1, 0),

SOC_SINGLE("ADC High Performance Switch", WM8962_ADDITIONAL_CONTROL_1,
	   5, 1, 0),

SOC_SINGLE_TLV("Beep Volume", WM8962_BEEP_GENERATOR_1, 4, 15, 0, beep_tlv),

SOC_DOUBLE_R_TLV("Headphone Volume", WM8962_HPOUTL_VOLUME,
		 WM8962_HPOUTR_VOLUME, 0, 127, 0, out_tlv),
SOC_DOUBLE_EXT("Headphone Switch", WM8962_PWR_MGMT_2, 1, 0, 1, 1,
	       snd_soc_get_volsw, wm8962_put_hp_sw),
SOC_DOUBLE_R("Headphone ZC Switch", WM8962_HPOUTL_VOLUME, WM8962_HPOUTR_VOLUME,
	     7, 1, 0),
SOC_DOUBLE_TLV("Headphone Aux Volume", WM8962_ANALOGUE_HP_2, 3, 6, 7, 0,
	       hp_tlv),

SOC_DOUBLE_R("Headphone Mixer Switch", WM8962_HEADPHONE_MIXER_3,
	     WM8962_HEADPHONE_MIXER_4, 8, 1, 1),

SOC_SINGLE_TLV("HPMIXL IN4L Volume", WM8962_HEADPHONE_MIXER_3,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXL IN4R Volume", WM8962_HEADPHONE_MIXER_3,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXL MIXINL Volume", WM8962_HEADPHONE_MIXER_3,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("HPMIXL MIXINR Volume", WM8962_HEADPHONE_MIXER_3,
	       6, 1, 1, inmix_tlv),

SOC_SINGLE_TLV("HPMIXR IN4L Volume", WM8962_HEADPHONE_MIXER_4,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXR IN4R Volume", WM8962_HEADPHONE_MIXER_4,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("HPMIXR MIXINL Volume", WM8962_HEADPHONE_MIXER_4,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("HPMIXR MIXINR Volume", WM8962_HEADPHONE_MIXER_4,
	       6, 1, 1, inmix_tlv),

SOC_SINGLE_TLV("Speaker Boost Volume", WM8962_CLASS_D_CONTROL_2, 0, 7, 0,
	       classd_tlv),
};

static const struct snd_kcontrol_new wm8962_spk_mono_controls[] = {
SOC_SINGLE_TLV("Speaker Volume", WM8962_SPKOUTL_VOLUME, 0, 127, 0, out_tlv),
SOC_SINGLE_EXT("Speaker Switch", WM8962_CLASS_D_CONTROL_1, 1, 1, 1,
	       snd_soc_get_volsw, wm8962_put_spk_sw),
SOC_SINGLE("Speaker ZC Switch", WM8962_SPKOUTL_VOLUME, 7, 1, 0),

SOC_SINGLE("Speaker Mixer Switch", WM8962_SPEAKER_MIXER_3, 8, 1, 1),
SOC_SINGLE_TLV("Speaker Mixer IN4L Volume", WM8962_SPEAKER_MIXER_3,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("Speaker Mixer IN4R Volume", WM8962_SPEAKER_MIXER_3,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("Speaker Mixer MIXINL Volume", WM8962_SPEAKER_MIXER_3,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("Speaker Mixer MIXINR Volume", WM8962_SPEAKER_MIXER_3,
	       6, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("Speaker Mixer DACL Volume", WM8962_SPEAKER_MIXER_5,
	       7, 1, 0, inmix_tlv),
SOC_SINGLE_TLV("Speaker Mixer DACR Volume", WM8962_SPEAKER_MIXER_5,
	       6, 1, 0, inmix_tlv),
};

static const struct snd_kcontrol_new wm8962_spk_stereo_controls[] = {
SOC_DOUBLE_R_TLV("Speaker Volume", WM8962_SPKOUTL_VOLUME,
		 WM8962_SPKOUTR_VOLUME, 0, 127, 0, out_tlv),
SOC_DOUBLE_EXT("Speaker Switch", WM8962_CLASS_D_CONTROL_1, 1, 0, 1, 1,
	       snd_soc_get_volsw, wm8962_put_spk_sw),
SOC_DOUBLE_R("Speaker ZC Switch", WM8962_SPKOUTL_VOLUME, WM8962_SPKOUTR_VOLUME,
	     7, 1, 0),

SOC_DOUBLE_R("Speaker Mixer Switch", WM8962_SPEAKER_MIXER_3,
	     WM8962_SPEAKER_MIXER_4, 8, 1, 1),

SOC_SINGLE_TLV("SPKOUTL Mixer IN4L Volume", WM8962_SPEAKER_MIXER_3,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer IN4R Volume", WM8962_SPEAKER_MIXER_3,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer MIXINL Volume", WM8962_SPEAKER_MIXER_3,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer MIXINR Volume", WM8962_SPEAKER_MIXER_3,
	       6, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer DACL Volume", WM8962_SPEAKER_MIXER_5,
	       7, 1, 0, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTL Mixer DACR Volume", WM8962_SPEAKER_MIXER_5,
	       6, 1, 0, inmix_tlv),

SOC_SINGLE_TLV("SPKOUTR Mixer IN4L Volume", WM8962_SPEAKER_MIXER_4,
	       3, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer IN4R Volume", WM8962_SPEAKER_MIXER_4,
	       0, 7, 0, bypass_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer MIXINL Volume", WM8962_SPEAKER_MIXER_4,
	       7, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer MIXINR Volume", WM8962_SPEAKER_MIXER_4,
	       6, 1, 1, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer DACL Volume", WM8962_SPEAKER_MIXER_5,
	       5, 1, 0, inmix_tlv),
SOC_SINGLE_TLV("SPKOUTR Mixer DACR Volume", WM8962_SPEAKER_MIXER_5,
	       4, 1, 0, inmix_tlv),
};

static int sysclk_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int src;
	int fll;

	src = snd_soc_read(codec, WM8962_CLOCKING2) & WM8962_SYSCLK_SRC_MASK;

	switch (src) {
	case 0:      /* MCLK */
		fll = 0;
		break;
	case 0x200:  /* FLL */
		fll = 1;
		break;
	default:
		dev_err(codec->dev, "Unknown SYSCLK source %x\n", src);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (fll)
			snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
					    WM8962_FLL_ENA, WM8962_FLL_ENA);
		break;

	case SND_SOC_DAPM_POST_PMD:
		if (fll)
			snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
					    WM8962_FLL_ENA, 0);
		break;

	default:
		BUG();
		return -EINVAL;
	}

	return 0;
}

static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(5);
		break;

	default:
		BUG();
		return -EINVAL;
	}

	return 0;
}

static int hp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int timeout;
	int reg;
	int expected = (WM8962_DCS_STARTUP_DONE_HP1L |
			WM8962_DCS_STARTUP_DONE_HP1R);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA);
		udelay(20);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY,
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY);

		/* Start the DC servo */
		snd_soc_update_bits(codec, WM8962_DC_SERVO_1,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP);

		/* Wait for it to complete, should be well under 100ms */
		timeout = 0;
		do {
			msleep(1);
			reg = snd_soc_read(codec, WM8962_DC_SERVO_6);
			if (reg < 0) {
				dev_err(codec->dev,
					"Failed to read DCS status: %d\n",
					reg);
				continue;
			}
			dev_dbg(codec->dev, "DCS status: %x\n", reg);
		} while (++timeout < 200 && (reg & expected) != expected);

		if ((reg & expected) != expected)
			dev_err(codec->dev, "DC servo timed out\n");
		else
			dev_dbg(codec->dev, "DC servo complete after %dms\n",
				timeout);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP,
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP);
		udelay(20);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT, 0);

		udelay(20);

		snd_soc_update_bits(codec, WM8962_DC_SERVO_1,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP,
				    0);

		snd_soc_update_bits(codec, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA |
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY |
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP, 0);
				    
		break;

	default:
		BUG();
		return -EINVAL;
	
	}

	return 0;
}

/* VU bits for the output PGAs only take effect while the PGA is powered */
static int out_pga_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	u16 *reg_cache = codec->reg_cache;
	int reg;

	switch (w->shift) {
	case WM8962_HPOUTR_PGA_ENA_SHIFT:
		reg = WM8962_HPOUTR_VOLUME;
		break;
	case WM8962_HPOUTL_PGA_ENA_SHIFT:
		reg = WM8962_HPOUTL_VOLUME;
		break;
	case WM8962_SPKOUTR_PGA_ENA_SHIFT:
		reg = WM8962_SPKOUTR_VOLUME;
		break;
	case WM8962_SPKOUTL_PGA_ENA_SHIFT:
		reg = WM8962_SPKOUTL_VOLUME;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return snd_soc_write(codec, reg, reg_cache[reg]);
	default:
		BUG();
		return -EINVAL;
	}
}

static const char *st_text[] = { "None", "Right", "Left" };

static const struct soc_enum str_enum =
	SOC_ENUM_SINGLE(WM8962_DAC_DSP_MIXING_1, 2, 3, st_text);

static const struct snd_kcontrol_new str_mux =
	SOC_DAPM_ENUM("Right Sidetone", str_enum);

static const struct soc_enum stl_enum =
	SOC_ENUM_SINGLE(WM8962_DAC_DSP_MIXING_2, 2, 3, st_text);

static const struct snd_kcontrol_new stl_mux =
	SOC_DAPM_ENUM("Left Sidetone", stl_enum);

static const char *outmux_text[] = { "DAC", "Mixer" };

static const struct soc_enum spkoutr_enum =
	SOC_ENUM_SINGLE(WM8962_SPEAKER_MIXER_2, 7, 2, outmux_text);

static const struct snd_kcontrol_new spkoutr_mux =
	SOC_DAPM_ENUM("SPKOUTR Mux", spkoutr_enum);

static const struct soc_enum spkoutl_enum =
	SOC_ENUM_SINGLE(WM8962_SPEAKER_MIXER_1, 7, 2, outmux_text);

static const struct snd_kcontrol_new spkoutl_mux =
	SOC_DAPM_ENUM("SPKOUTL Mux", spkoutl_enum);

static const struct soc_enum hpoutr_enum =
	SOC_ENUM_SINGLE(WM8962_HEADPHONE_MIXER_2, 7, 2, outmux_text);

static const struct snd_kcontrol_new hpoutr_mux =
	SOC_DAPM_ENUM("HPOUTR Mux", hpoutr_enum);

static const struct soc_enum hpoutl_enum =
	SOC_ENUM_SINGLE(WM8962_HEADPHONE_MIXER_1, 7, 2, outmux_text);

static const struct snd_kcontrol_new hpoutl_mux =
	SOC_DAPM_ENUM("HPOUTL Mux", hpoutl_enum);

static const struct snd_kcontrol_new inpgal[] = {
SOC_DAPM_SINGLE("IN1L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 3, 1, 0),
SOC_DAPM_SINGLE("IN2L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 2, 1, 0),
SOC_DAPM_SINGLE("IN3L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 1, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_LEFT_INPUT_PGA_CONTROL, 0, 1, 0),
};

static const struct snd_kcontrol_new inpgar[] = {
SOC_DAPM_SINGLE("IN1R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 3, 1, 0),
SOC_DAPM_SINGLE("IN2R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 2, 1, 0),
SOC_DAPM_SINGLE("IN3R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_RIGHT_INPUT_PGA_CONTROL, 0, 1, 0),
};

static const struct snd_kcontrol_new mixinl[] = {
SOC_DAPM_SINGLE("IN2L Switch", WM8962_INPUT_MIXER_CONTROL_2, 5, 1, 0),
SOC_DAPM_SINGLE("IN3L Switch", WM8962_INPUT_MIXER_CONTROL_2, 4, 1, 0),
SOC_DAPM_SINGLE("PGA Switch", WM8962_INPUT_MIXER_CONTROL_2, 3, 1, 0),
};

static const struct snd_kcontrol_new mixinr[] = {
SOC_DAPM_SINGLE("IN2R Switch", WM8962_INPUT_MIXER_CONTROL_2, 2, 1, 0),
SOC_DAPM_SINGLE("IN3R Switch", WM8962_INPUT_MIXER_CONTROL_2, 1, 1, 0),
SOC_DAPM_SINGLE("PGA Switch", WM8962_INPUT_MIXER_CONTROL_2, 0, 1, 0),
};

static const struct snd_kcontrol_new hpmixl[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_HEADPHONE_MIXER_1, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_HEADPHONE_MIXER_1, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_HEADPHONE_MIXER_1, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_HEADPHONE_MIXER_1, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_HEADPHONE_MIXER_1, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_HEADPHONE_MIXER_1, 0, 1, 0),
};

static const struct snd_kcontrol_new hpmixr[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_HEADPHONE_MIXER_2, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_HEADPHONE_MIXER_2, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_HEADPHONE_MIXER_2, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_HEADPHONE_MIXER_2, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_HEADPHONE_MIXER_2, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_HEADPHONE_MIXER_2, 0, 1, 0),
};

static const struct snd_kcontrol_new spkmixl[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_SPEAKER_MIXER_1, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_SPEAKER_MIXER_1, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_SPEAKER_MIXER_1, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_SPEAKER_MIXER_1, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_SPEAKER_MIXER_1, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_SPEAKER_MIXER_1, 0, 1, 0),
};

static const struct snd_kcontrol_new spkmixr[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8962_SPEAKER_MIXER_2, 5, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8962_SPEAKER_MIXER_2, 4, 1, 0),
SOC_DAPM_SINGLE("MIXINL Switch", WM8962_SPEAKER_MIXER_2, 3, 1, 0),
SOC_DAPM_SINGLE("MIXINR Switch", WM8962_SPEAKER_MIXER_2, 2, 1, 0),
SOC_DAPM_SINGLE("IN4L Switch", WM8962_SPEAKER_MIXER_2, 1, 1, 0),
SOC_DAPM_SINGLE("IN4R Switch", WM8962_SPEAKER_MIXER_2, 0, 1, 0),
};

static const struct snd_soc_dapm_widget wm8962_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),
SND_SOC_DAPM_INPUT("IN4L"),
SND_SOC_DAPM_INPUT("IN4R"),
SND_SOC_DAPM_INPUT("Beep"),
SND_SOC_DAPM_INPUT("DMICDAT"),

SND_SOC_DAPM_MICBIAS("MICBIAS", WM8962_PWR_MGMT_1, 1, 0),

SND_SOC_DAPM_SUPPLY("Class G", WM8962_CHARGE_PUMP_B, 0, 1, NULL, 0),
SND_SOC_DAPM_SUPPLY("SYSCLK", WM8962_CLOCKING2, 5, 0, sysclk_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("Charge Pump", WM8962_CHARGE_PUMP_1, 0, 0, cp_event,
		    SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_SUPPLY("TOCLK", WM8962_ADDITIONAL_CONTROL_1, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("INPGAL", WM8962_LEFT_INPUT_PGA_CONTROL, 4, 0,
		   inpgal, ARRAY_SIZE(inpgal)),
SND_SOC_DAPM_MIXER("INPGAR", WM8962_RIGHT_INPUT_PGA_CONTROL, 4, 0,
		   inpgar, ARRAY_SIZE(inpgar)),
SND_SOC_DAPM_MIXER("MIXINL", WM8962_PWR_MGMT_1, 5, 0,
		   mixinl, ARRAY_SIZE(mixinl)),
SND_SOC_DAPM_MIXER("MIXINR", WM8962_PWR_MGMT_1, 4, 0,
		   mixinr, ARRAY_SIZE(mixinr)),

SND_SOC_DAPM_AIF_IN("DMIC", NULL, 0, WM8962_PWR_MGMT_1, 10, 0),

SND_SOC_DAPM_ADC("ADCL", "Capture", WM8962_PWR_MGMT_1, 3, 0),
SND_SOC_DAPM_ADC("ADCR", "Capture", WM8962_PWR_MGMT_1, 2, 0),

SND_SOC_DAPM_MUX("STL", SND_SOC_NOPM, 0, 0, &stl_mux),
SND_SOC_DAPM_MUX("STR", SND_SOC_NOPM, 0, 0, &str_mux),

SND_SOC_DAPM_DAC("DACL", "Playback", WM8962_PWR_MGMT_2, 8, 0),
SND_SOC_DAPM_DAC("DACR", "Playback", WM8962_PWR_MGMT_2, 7, 0),

SND_SOC_DAPM_PGA("Left Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("HPMIXL", WM8962_MIXER_ENABLES, 3, 0,
		   hpmixl, ARRAY_SIZE(hpmixl)),
SND_SOC_DAPM_MIXER("HPMIXR", WM8962_MIXER_ENABLES, 2, 0,
		   hpmixr, ARRAY_SIZE(hpmixr)),

SND_SOC_DAPM_MUX_E("HPOUTL PGA", WM8962_PWR_MGMT_2, 6, 0, &hpoutl_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_MUX_E("HPOUTR PGA", WM8962_PWR_MGMT_2, 5, 0, &hpoutr_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA_E("HPOUT", SND_SOC_NOPM, 0, 0, NULL, 0, hp_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
};

static const struct snd_soc_dapm_widget wm8962_dapm_spk_mono_widgets[] = {
SND_SOC_DAPM_MIXER("Speaker Mixer", WM8962_MIXER_ENABLES, 1, 0,
		   spkmixl, ARRAY_SIZE(spkmixl)),
SND_SOC_DAPM_MUX_E("Speaker PGA", WM8962_PWR_MGMT_2, 4, 0, &spkoutl_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA("Speaker Output", WM8962_CLASS_D_CONTROL_1, 7, 0, NULL, 0),
SND_SOC_DAPM_OUTPUT("SPKOUT"),
};

static const struct snd_soc_dapm_widget wm8962_dapm_spk_stereo_widgets[] = {
SND_SOC_DAPM_MIXER("SPKOUTL Mixer", WM8962_MIXER_ENABLES, 1, 0,
		   spkmixl, ARRAY_SIZE(spkmixl)),
SND_SOC_DAPM_MIXER("SPKOUTR Mixer", WM8962_MIXER_ENABLES, 0, 0,
		   spkmixr, ARRAY_SIZE(spkmixr)),

SND_SOC_DAPM_MUX_E("SPKOUTL PGA", WM8962_PWR_MGMT_2, 4, 0, &spkoutl_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_MUX_E("SPKOUTR PGA", WM8962_PWR_MGMT_2, 3, 0, &spkoutr_mux,
		   out_pga_event, SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("SPKOUTR Output", WM8962_CLASS_D_CONTROL_1, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("SPKOUTL Output", WM8962_CLASS_D_CONTROL_1, 6, 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("SPKOUTL"),
SND_SOC_DAPM_OUTPUT("SPKOUTR"),
};

static const struct snd_soc_dapm_route wm8962_intercon[] = {
	{ "INPGAL", "IN1L Switch", "IN1L" },
	{ "INPGAL", "IN2L Switch", "IN2L" },
	{ "INPGAL", "IN3L Switch", "IN3L" },
	{ "INPGAL", "IN4L Switch", "IN4L" },

	{ "INPGAR", "IN1R Switch", "IN1R" },
	{ "INPGAR", "IN2R Switch", "IN2R" },
	{ "INPGAR", "IN3R Switch", "IN3R" },
	{ "INPGAR", "IN4R Switch", "IN4R" },

	{ "MIXINL", "IN2L Switch", "IN2L" },
	{ "MIXINL", "IN3L Switch", "IN3L" },
	{ "MIXINL", "PGA Switch", "INPGAL" },

	{ "MIXINR", "IN2R Switch", "IN2R" },
	{ "MIXINR", "IN3R Switch", "IN3R" },
	{ "MIXINR", "PGA Switch", "INPGAR" },

	{ "MICBIAS", NULL, "SYSCLK" },

	{ "DMIC", NULL, "DMICDAT" },

	{ "ADCL", NULL, "SYSCLK" },
	{ "ADCL", NULL, "TOCLK" },
	{ "ADCL", NULL, "MIXINL" },
	{ "ADCL", NULL, "DMIC" },

	{ "ADCR", NULL, "SYSCLK" },
	{ "ADCR", NULL, "TOCLK" },
	{ "ADCR", NULL, "MIXINR" },
	{ "ADCR", NULL, "DMIC" },

	{ "STL", "Left", "ADCL" },
	{ "STL", "Right", "ADCR" },

	{ "STR", "Left", "ADCL" },
	{ "STR", "Right", "ADCR" },

	{ "DACL", NULL, "SYSCLK" },
	{ "DACL", NULL, "TOCLK" },
	{ "DACL", NULL, "Beep" },
	{ "DACL", NULL, "STL" },

	{ "DACR", NULL, "SYSCLK" },
	{ "DACR", NULL, "TOCLK" },
	{ "DACR", NULL, "Beep" },
	{ "DACR", NULL, "STR" },

	{ "HPMIXL", "IN4L Switch", "IN4L" },
	{ "HPMIXL", "IN4R Switch", "IN4R" },
	{ "HPMIXL", "DACL Switch", "DACL" },
	{ "HPMIXL", "DACR Switch", "DACR" },
	{ "HPMIXL", "MIXINL Switch", "MIXINL" },
	{ "HPMIXL", "MIXINR Switch", "MIXINR" },

	{ "HPMIXR", "IN4L Switch", "IN4L" },
	{ "HPMIXR", "IN4R Switch", "IN4R" },
	{ "HPMIXR", "DACL Switch", "DACL" },
	{ "HPMIXR", "DACR Switch", "DACR" },
	{ "HPMIXR", "MIXINL Switch", "MIXINL" },
	{ "HPMIXR", "MIXINR Switch", "MIXINR" },

	{ "Left Bypass", NULL, "HPMIXL" },
	{ "Left Bypass", NULL, "Class G" },

	{ "Right Bypass", NULL, "HPMIXR" },
	{ "Right Bypass", NULL, "Class G" },

	{ "HPOUTL PGA", "Mixer", "Left Bypass" },
	{ "HPOUTL PGA", "DAC", "DACL" },

	{ "HPOUTR PGA", "Mixer", "Right Bypass" },
	{ "HPOUTR PGA", "DAC", "DACR" },

	{ "HPOUT", NULL, "HPOUTL PGA" },
	{ "HPOUT", NULL, "HPOUTR PGA" },
	{ "HPOUT", NULL, "Charge Pump" },
	{ "HPOUT", NULL, "SYSCLK" },
	{ "HPOUT", NULL, "TOCLK" },

	{ "HPOUTL", NULL, "HPOUT" },
	{ "HPOUTR", NULL, "HPOUT" },
};

static const struct snd_soc_dapm_route wm8962_spk_mono_intercon[] = {
	{ "Speaker Mixer", "IN4L Switch", "IN4L" },
	{ "Speaker Mixer", "IN4R Switch", "IN4R" },
	{ "Speaker Mixer", "DACL Switch", "DACL" },
	{ "Speaker Mixer", "DACR Switch", "DACR" },
	{ "Speaker Mixer", "MIXINL Switch", "MIXINL" },
	{ "Speaker Mixer", "MIXINR Switch", "MIXINR" },

	{ "Speaker PGA", "Mixer", "Speaker Mixer" },
	{ "Speaker PGA", "DAC", "DACL" },

	{ "Speaker Output", NULL, "Speaker PGA" },
	{ "Speaker Output", NULL, "SYSCLK" },
	{ "Speaker Output", NULL, "TOCLK" },

	{ "SPKOUT", NULL, "Speaker Output" },
};

static const struct snd_soc_dapm_route wm8962_spk_stereo_intercon[] = {
	{ "SPKOUTL Mixer", "IN4L Switch", "IN4L" },
	{ "SPKOUTL Mixer", "IN4R Switch", "IN4R" },
	{ "SPKOUTL Mixer", "DACL Switch", "DACL" },
	{ "SPKOUTL Mixer", "DACR Switch", "DACR" },
	{ "SPKOUTL Mixer", "MIXINL Switch", "MIXINL" },
	{ "SPKOUTL Mixer", "MIXINR Switch", "MIXINR" },

	{ "SPKOUTR Mixer", "IN4L Switch", "IN4L" },
	{ "SPKOUTR Mixer", "IN4R Switch", "IN4R" },
	{ "SPKOUTR Mixer", "DACL Switch", "DACL" },
	{ "SPKOUTR Mixer", "DACR Switch", "DACR" },
	{ "SPKOUTR Mixer", "MIXINL Switch", "MIXINL" },
	{ "SPKOUTR Mixer", "MIXINR Switch", "MIXINR" },

	{ "SPKOUTL PGA", "Mixer", "SPKOUTL Mixer" },
	{ "SPKOUTL PGA", "DAC", "DACL" },

	{ "SPKOUTR PGA", "Mixer", "SPKOUTR Mixer" },
	{ "SPKOUTR PGA", "DAC", "DACR" },

	{ "SPKOUTL Output", NULL, "SPKOUTL PGA" },
	{ "SPKOUTL Output", NULL, "SYSCLK" },
	{ "SPKOUTL Output", NULL, "TOCLK" },

	{ "SPKOUTR Output", NULL, "SPKOUTR PGA" },
	{ "SPKOUTR Output", NULL, "SYSCLK" },
	{ "SPKOUTR Output", NULL, "TOCLK" },

	{ "SPKOUTL", NULL, "SPKOUTL Output" },
	{ "SPKOUTR", NULL, "SPKOUTR Output" },
};

static int wm8962_add_widgets(struct snd_soc_codec *codec)
{
	struct wm8962_pdata *pdata = dev_get_platdata(codec->dev);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_add_controls(codec, wm8962_snd_controls,
			     ARRAY_SIZE(wm8962_snd_controls));
	if (pdata && pdata->spk_mono)
		snd_soc_add_controls(codec, wm8962_spk_mono_controls,
				     ARRAY_SIZE(wm8962_spk_mono_controls));
	else
		snd_soc_add_controls(codec, wm8962_spk_stereo_controls,
				     ARRAY_SIZE(wm8962_spk_stereo_controls));


	snd_soc_dapm_new_controls(dapm, wm8962_dapm_widgets,
				  ARRAY_SIZE(wm8962_dapm_widgets));
	if (pdata && pdata->spk_mono)
		snd_soc_dapm_new_controls(dapm, wm8962_dapm_spk_mono_widgets,
					  ARRAY_SIZE(wm8962_dapm_spk_mono_widgets));
	else
		snd_soc_dapm_new_controls(dapm, wm8962_dapm_spk_stereo_widgets,
					  ARRAY_SIZE(wm8962_dapm_spk_stereo_widgets));

	snd_soc_dapm_add_routes(dapm, wm8962_intercon,
				ARRAY_SIZE(wm8962_intercon));
	if (pdata && pdata->spk_mono)
		snd_soc_dapm_add_routes(dapm, wm8962_spk_mono_intercon,
					ARRAY_SIZE(wm8962_spk_mono_intercon));
	else
		snd_soc_dapm_add_routes(dapm, wm8962_spk_stereo_intercon,
					ARRAY_SIZE(wm8962_spk_stereo_intercon));


	snd_soc_dapm_disable_pin(dapm, "Beep");

	return 0;
}

static void wm8962_sync_cache(struct snd_soc_codec *codec)
{
	u16 *reg_cache = codec->reg_cache;
	int i;

	if (!codec->cache_sync)
		return;

	dev_dbg(codec->dev, "Syncing cache\n");

	codec->cache_only = 0;

	/* Sync back cached values if they're different from the
	 * hardware default.
	 */
	for (i = 1; i < codec->driver->reg_cache_size; i++) {
		if (i == WM8962_SOFTWARE_RESET)
			continue;
		if (reg_cache[i] == wm8962_reg[i])
			continue;

		snd_soc_write(codec, i, reg_cache[i]);
	}

	codec->cache_sync = 0;
}

/* -1 for reserved values */
static const int bclk_divs[] = {
	1, -1, 2, 3, 4, -1, 6, 8, -1, 12, 16, 24, -1, 32, 32, 32
};

static void wm8962_configure_bclk(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int dspclk, i;
	int clocking2 = 0;
	int aif2 = 0;

	if (!wm8962->bclk) {
		dev_dbg(codec->dev, "No BCLK rate configured\n");
		return;
	}

	dspclk = snd_soc_read(codec, WM8962_CLOCKING1);
	if (dspclk < 0) {
		dev_err(codec->dev, "Failed to read DSPCLK: %d\n", dspclk);
		return;
	}

	dspclk = (dspclk & WM8962_DSPCLK_DIV_MASK) >> WM8962_DSPCLK_DIV_SHIFT;
	switch (dspclk) {
	case 0:
		dspclk = wm8962->sysclk_rate;
		break;
	case 1:
		dspclk = wm8962->sysclk_rate / 2;
		break;
	case 2:
		dspclk = wm8962->sysclk_rate / 4;
		break;
	default:
		dev_warn(codec->dev, "Unknown DSPCLK divisor read back\n");
		dspclk = wm8962->sysclk;
	}

	dev_dbg(codec->dev, "DSPCLK is %dHz, BCLK %d\n", dspclk, wm8962->bclk);

	/* We're expecting an exact match */
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		if (bclk_divs[i] < 0)
			continue;

		if (dspclk / bclk_divs[i] == wm8962->bclk) {
			dev_dbg(codec->dev, "Selected BCLK_DIV %d for %dHz\n",
				bclk_divs[i], wm8962->bclk);
			clocking2 |= i;
			break;
		}
	}
	if (i == ARRAY_SIZE(bclk_divs)) {
		dev_err(codec->dev, "Unsupported BCLK ratio %d\n",
			dspclk / wm8962->bclk);
		return;
	}

	aif2 |= wm8962->bclk / wm8962->lrclk;
	dev_dbg(codec->dev, "Selected LRCLK divisor %d for %dHz\n",
		wm8962->bclk / wm8962->lrclk, wm8962->lrclk);

	snd_soc_update_bits(codec, WM8962_CLOCKING2,
			    WM8962_BCLK_DIV_MASK, clocking2);
	snd_soc_update_bits(codec, WM8962_AUDIO_INTERFACE_2,
			    WM8962_AIF_RATE_MASK, aif2);
}

static int wm8962_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int ret;

	if (level == codec->dapm.bias_level)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID 2*50k */
		snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK, 0x80);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8962->supplies),
						    wm8962->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			wm8962_sync_cache(codec);

			snd_soc_update_bits(codec, WM8962_ANTI_POP,
					    WM8962_STARTUP_BIAS_ENA |
					    WM8962_VMID_BUF_ENA,
					    WM8962_STARTUP_BIAS_ENA |
					    WM8962_VMID_BUF_ENA);

			/* Bias enable at 2*50k for ramp */
			snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
					    WM8962_VMID_SEL_MASK |
					    WM8962_BIAS_ENA,
					    WM8962_BIAS_ENA | 0x180);

			msleep(5);

			snd_soc_update_bits(codec, WM8962_CLOCKING2,
					    WM8962_CLKREG_OVD,
					    WM8962_CLKREG_OVD);

			wm8962_configure_bclk(codec);
		}

		/* VMID 2*250k */
		snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK, 0x100);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK | WM8962_BIAS_ENA, 0);

		snd_soc_update_bits(codec, WM8962_ANTI_POP,
				    WM8962_STARTUP_BIAS_ENA |
				    WM8962_VMID_BUF_ENA, 0);

		regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies),
				       wm8962->supplies);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static const struct {
	int rate;
	int reg;
} sr_vals[] = {
	{ 48000, 0 },
	{ 44100, 0 },
	{ 32000, 1 },
	{ 22050, 2 },
	{ 24000, 2 },
	{ 16000, 3 },
	{ 11025, 4 },
	{ 12000, 4 },
	{ 8000,  5 },
	{ 88200, 6 },
	{ 96000, 6 },
};

static const int sysclk_rates[] = {
	64, 128, 192, 256, 384, 512, 768, 1024, 1408, 1536,
};

static int wm8962_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int rate = params_rate(params);
	int i;
	int aif0 = 0;
	int adctl3 = 0;
	int clocking4 = 0;

	wm8962->bclk = snd_soc_params_to_bclk(params);
	wm8962->lrclk = params_rate(params);

	for (i = 0; i < ARRAY_SIZE(sr_vals); i++) {
		if (sr_vals[i].rate == rate) {
			adctl3 |= sr_vals[i].reg;
			break;
		}
	}
	if (i == ARRAY_SIZE(sr_vals)) {
		dev_err(codec->dev, "Unsupported rate %dHz\n", rate);
		return -EINVAL;
	}

	if (rate % 8000 == 0)
		adctl3 |= WM8962_SAMPLE_RATE_INT_MODE;

	for (i = 0; i < ARRAY_SIZE(sysclk_rates); i++) {
		if (sysclk_rates[i] == wm8962->sysclk_rate / rate) {
			clocking4 |= i << WM8962_SYSCLK_RATE_SHIFT;
			break;
		}
	}
	if (i == ARRAY_SIZE(sysclk_rates)) {
		dev_err(codec->dev, "Unsupported sysclk ratio %d\n",
			wm8962->sysclk_rate / rate);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		aif0 |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		aif0 |= 0x80;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		aif0 |= 0xc0;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8962_AUDIO_INTERFACE_0,
			    WM8962_WL_MASK, aif0);
	snd_soc_update_bits(codec, WM8962_ADDITIONAL_CONTROL_3,
			    WM8962_SAMPLE_RATE_INT_MODE |
			    WM8962_SAMPLE_RATE_MASK, adctl3);
	snd_soc_update_bits(codec, WM8962_CLOCKING_4,
			    WM8962_SYSCLK_RATE_MASK, clocking4);

	wm8962_configure_bclk(codec);

	return 0;
}

static int wm8962_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int src;

	switch (clk_id) {
	case WM8962_SYSCLK_MCLK:
		wm8962->sysclk = WM8962_SYSCLK_MCLK;
		src = 0;
		break;
	case WM8962_SYSCLK_FLL:
		wm8962->sysclk = WM8962_SYSCLK_FLL;
		src = 1 << WM8962_SYSCLK_SRC_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8962_CLOCKING2, WM8962_SYSCLK_SRC_MASK,
			    src);

	wm8962->sysclk_rate = freq;

	return 0;
}

static int wm8962_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int aif0 = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		aif0 |= WM8962_LRCLK_INV;
	case SND_SOC_DAIFMT_DSP_B:
		aif0 |= 3;

		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
		case SND_SOC_DAIFMT_IB_NF:
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif0 |= 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif0 |= 2;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		aif0 |= WM8962_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		aif0 |= WM8962_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		aif0 |= WM8962_BCLK_INV | WM8962_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aif0 |= WM8962_MSTR;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8962_AUDIO_INTERFACE_0,
			    WM8962_FMT_MASK | WM8962_BCLK_INV | WM8962_MSTR |
			    WM8962_LRCLK_INV, aif0);

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_refclk_div;
	u16 n;
	u16 theta;
	u16 lambda;
};

/* The size in bits of the FLL divide multiplied by 10
 * to allow rounding later */
#define FIXED_FLL_SIZE ((1 << 16) * 10)

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	unsigned int target;
	unsigned int div;
	unsigned int fratio, gcd_fll;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_refclk_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_refclk_div++;

		if (div > 4) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("FLL Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 2;
	while (Fout * div < 90000000) {
		div++;
		if (div > 64) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	target = Fout * div;
	fll_div->fll_outdiv = div - 1;

	pr_debug("FLL Fvco=%dHz\n", target);

	/* Find an appropriate FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			fratio = fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	fll_div->n = target / (fratio * Fref);

	if (target % Fref == 0) {
		fll_div->theta = 0;
		fll_div->lambda = 0;
	} else {
		gcd_fll = gcd(target, fratio * Fref);

		fll_div->theta = (target - (fll_div->n * fratio * Fref))
			/ gcd_fll;
		fll_div->lambda = (fratio * Fref) / gcd_fll;
	}

	pr_debug("FLL N=%x THETA=%x LAMBDA=%x\n",
		 fll_div->n, fll_div->theta, fll_div->lambda);
	pr_debug("FLL_FRATIO=%x FLL_OUTDIV=%x FLL_REFCLK_DIV=%x\n",
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_refclk_div);

	return 0;
}

static int wm8962_set_fll(struct snd_soc_codec *codec, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div fll_div;
	unsigned long timeout;
	int ret;
	int fll1 = snd_soc_read(codec, WM8962_FLL_CONTROL_1) & WM8962_FLL_ENA;

	/* Any change? */
	if (source == wm8962->fll_src && Fref == wm8962->fll_fref &&
	    Fout == wm8962->fll_fout)
		return 0;

	if (Fout == 0) {
		dev_dbg(codec->dev, "FLL disabled\n");

		wm8962->fll_fref = 0;
		wm8962->fll_fout = 0;

		snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_ENA, 0);

		return 0;
	}

	ret = fll_factors(&fll_div, Fref, Fout);
	if (ret != 0)
		return ret;

	switch (fll_id) {
	case WM8962_FLL_MCLK:
	case WM8962_FLL_BCLK:
	case WM8962_FLL_OSC:
		fll1 |= (fll_id - 1) << WM8962_FLL_REFCLK_SRC_SHIFT;
		break;
	case WM8962_FLL_INT:
		snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_OSC_ENA, WM8962_FLL_OSC_ENA);
		snd_soc_update_bits(codec, WM8962_FLL_CONTROL_5,
				    WM8962_FLL_FRC_NCO, WM8962_FLL_FRC_NCO);
		break;
	default:
		dev_err(codec->dev, "Unknown FLL source %d\n", ret);
		return -EINVAL;
	}

	if (fll_div.theta || fll_div.lambda)
		fll1 |= WM8962_FLL_FRAC;

	/* Stop the FLL while we reconfigure */
	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1, WM8962_FLL_ENA, 0);

	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_2,
			    WM8962_FLL_OUTDIV_MASK |
			    WM8962_FLL_REFCLK_DIV_MASK,
			    (fll_div.fll_outdiv << WM8962_FLL_OUTDIV_SHIFT) |
			    (fll_div.fll_refclk_div));

	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_3,
			    WM8962_FLL_FRATIO_MASK, fll_div.fll_fratio);

	snd_soc_write(codec, WM8962_FLL_CONTROL_6, fll_div.theta);
	snd_soc_write(codec, WM8962_FLL_CONTROL_7, fll_div.lambda);
	snd_soc_write(codec, WM8962_FLL_CONTROL_8, fll_div.n);

	snd_soc_update_bits(codec, WM8962_FLL_CONTROL_1,
			    WM8962_FLL_FRAC | WM8962_FLL_REFCLK_SRC_MASK |
			    WM8962_FLL_ENA, fll1);

	dev_dbg(codec->dev, "FLL configured for %dHz->%dHz\n", Fref, Fout);

	/* This should be a massive overestimate */
	timeout = msecs_to_jiffies(1);

	wait_for_completion_timeout(&wm8962->fll_lock, timeout);

	wm8962->fll_fref = Fref;
	wm8962->fll_fout = Fout;
	wm8962->fll_src = source;

	return 0;
}

static int wm8962_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	int val;

	if (mute)
		val = WM8962_DAC_MUTE;
	else
		val = 0;

	return snd_soc_update_bits(codec, WM8962_ADC_DAC_CONTROL_1,
				   WM8962_DAC_MUTE, val);
}

#define WM8962_RATES SNDRV_PCM_RATE_8000_96000

#define WM8962_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8962_dai_ops = {
	.hw_params = wm8962_hw_params,
	.set_sysclk = wm8962_set_dai_sysclk,
	.set_fmt = wm8962_set_dai_fmt,
	.digital_mute = wm8962_mute,
};

static struct snd_soc_dai_driver wm8962_dai = {
	.name = "wm8962",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8962_RATES,
		.formats = WM8962_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8962_RATES,
		.formats = WM8962_FORMATS,
	},
	.ops = &wm8962_dai_ops,
	.symmetric_rates = 1,
};

static void wm8962_mic_work(struct work_struct *work)
{
	struct wm8962_priv *wm8962 = container_of(work,
						  struct wm8962_priv,
						  mic_work.work);
	struct snd_soc_codec *codec = wm8962->codec;
	int status = 0;
	int irq_pol = 0;
	int reg;

	reg = snd_soc_read(codec, WM8962_ADDITIONAL_CONTROL_4);

	if (reg & WM8962_MICDET_STS) {
		status |= SND_JACK_MICROPHONE;
		irq_pol |= WM8962_MICD_IRQ_POL;
	}

	if (reg & WM8962_MICSHORT_STS) {
		status |= SND_JACK_BTN_0;
		irq_pol |= WM8962_MICSCD_IRQ_POL;
	}

	snd_soc_jack_report(wm8962->jack, status,
			    SND_JACK_MICROPHONE | SND_JACK_BTN_0);

	snd_soc_update_bits(codec, WM8962_MICINT_SOURCE_POL,
			    WM8962_MICSCD_IRQ_POL |
			    WM8962_MICD_IRQ_POL, irq_pol);
}

static irqreturn_t wm8962_irq(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int mask;
	int active;

	mask = snd_soc_read(codec, WM8962_INTERRUPT_STATUS_2_MASK);

	active = snd_soc_read(codec, WM8962_INTERRUPT_STATUS_2);
	active &= ~mask;

	if (active & WM8962_FLL_LOCK_EINT) {
		dev_dbg(codec->dev, "FLL locked\n");
		complete(&wm8962->fll_lock);
	}

	if (active & WM8962_FIFOS_ERR_EINT)
		dev_err(codec->dev, "FIFO error\n");

	if (active & WM8962_TEMP_SHUT_EINT)
		dev_crit(codec->dev, "Thermal shutdown\n");

	if (active & (WM8962_MICSCD_EINT | WM8962_MICD_EINT)) {
		dev_dbg(codec->dev, "Microphone event detected\n");

#ifndef CONFIG_SND_SOC_WM8962_MODULE
		trace_snd_soc_jack_irq(dev_name(codec->dev));
#endif

		pm_wakeup_event(codec->dev, 300);

		schedule_delayed_work(&wm8962->mic_work,
				      msecs_to_jiffies(250));
	}

	/* Acknowledge the interrupts */
	snd_soc_write(codec, WM8962_INTERRUPT_STATUS_2, active);

	return IRQ_HANDLED;
}

/**
 * wm8962_mic_detect - Enable microphone detection via the WM8962 IRQ
 *
 * @codec:  WM8962 codec
 * @jack:   jack to report detection events on
 *
 * Enable microphone detection via IRQ on the WM8962.  If GPIOs are
 * being used to bring out signals to the processor then only platform
 * data configuration is needed for WM8962 and processor GPIOs should
 * be configured using snd_soc_jack_add_gpios() instead.
 *
 * If no jack is supplied detection will be disabled.
 */
int wm8962_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int irq_mask, enable;

	wm8962->jack = jack;
	if (jack) {
		irq_mask = 0;
		enable = WM8962_MICDET_ENA;
	} else {
		irq_mask = WM8962_MICD_EINT | WM8962_MICSCD_EINT;
		enable = 0;
	}

	snd_soc_update_bits(codec, WM8962_INTERRUPT_STATUS_2_MASK,
			    WM8962_MICD_EINT | WM8962_MICSCD_EINT, irq_mask);
	snd_soc_update_bits(codec, WM8962_ADDITIONAL_CONTROL_4,
			    WM8962_MICDET_ENA, enable);

	/* Send an initial empty report */
	snd_soc_jack_report(wm8962->jack, 0,
			    SND_JACK_MICROPHONE | SND_JACK_BTN_0);

	return 0;
}
EXPORT_SYMBOL_GPL(wm8962_mic_detect);

#ifdef CONFIG_PM
static int wm8962_resume(struct snd_soc_codec *codec)
{
	u16 *reg_cache = codec->reg_cache;
	int i;

	/* Restore the registers */
	for (i = 1; i < codec->driver->reg_cache_size; i++) {
		switch (i) {
		case WM8962_SOFTWARE_RESET:
			continue;
		default:
			break;
		}

		if (reg_cache[i] != wm8962_reg[i])
			snd_soc_write(codec, i, reg_cache[i]);
	}

	return 0;
}
#else
#define wm8962_resume NULL
#endif

#if defined(CONFIG_INPUT) || defined(CONFIG_INPUT_MODULE)
static int beep_rates[] = {
	500, 1000, 2000, 4000,
};

static void wm8962_beep_work(struct work_struct *work)
{
	struct wm8962_priv *wm8962 =
		container_of(work, struct wm8962_priv, beep_work);
	struct snd_soc_codec *codec = wm8962->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int i;
	int reg = 0;
	int best = 0;

	if (wm8962->beep_rate) {
		for (i = 0; i < ARRAY_SIZE(beep_rates); i++) {
			if (abs(wm8962->beep_rate - beep_rates[i]) <
			    abs(wm8962->beep_rate - beep_rates[best]))
				best = i;
		}

		dev_dbg(codec->dev, "Set beep rate %dHz for requested %dHz\n",
			beep_rates[best], wm8962->beep_rate);

		reg = WM8962_BEEP_ENA | (best << WM8962_BEEP_RATE_SHIFT);

		snd_soc_dapm_enable_pin(dapm, "Beep");
	} else {
		dev_dbg(codec->dev, "Disabling beep\n");
		snd_soc_dapm_disable_pin(dapm, "Beep");
	}

	snd_soc_update_bits(codec, WM8962_BEEP_GENERATOR_1,
			    WM8962_BEEP_ENA | WM8962_BEEP_RATE_MASK, reg);

	snd_soc_dapm_sync(dapm);
}

/* For usability define a way of injecting beep events for the device -
 * many systems will not have a keyboard.
 */
static int wm8962_beep_event(struct input_dev *dev, unsigned int type,
			     unsigned int code, int hz)
{
	struct snd_soc_codec *codec = input_get_drvdata(dev);
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "Beep event %x %x\n", code, hz);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 1000;
	case SND_TONE:
		break;
	default:
		return -1;
	}

	/* Kick the beep from a workqueue */
	wm8962->beep_rate = hz;
	schedule_work(&wm8962->beep_work);
	return 0;
}

static ssize_t wm8962_beep_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct wm8962_priv *wm8962 = dev_get_drvdata(dev);
	long int time;
	int ret;

	ret = strict_strtol(buf, 10, &time);
	if (ret != 0)
		return ret;

	input_event(wm8962->beep, EV_SND, SND_TONE, time);

	return count;
}

static DEVICE_ATTR(beep, 0200, NULL, wm8962_beep_set);

static void wm8962_init_beep(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int ret;

	wm8962->beep = input_allocate_device();
	if (!wm8962->beep) {
		dev_err(codec->dev, "Failed to allocate beep device\n");
		return;
	}

	INIT_WORK(&wm8962->beep_work, wm8962_beep_work);
	wm8962->beep_rate = 0;

	wm8962->beep->name = "WM8962 Beep Generator";
	wm8962->beep->phys = dev_name(codec->dev);
	wm8962->beep->id.bustype = BUS_I2C;

	wm8962->beep->evbit[0] = BIT_MASK(EV_SND);
	wm8962->beep->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	wm8962->beep->event = wm8962_beep_event;
	wm8962->beep->dev.parent = codec->dev;
	input_set_drvdata(wm8962->beep, codec);

	ret = input_register_device(wm8962->beep);
	if (ret != 0) {
		input_free_device(wm8962->beep);
		wm8962->beep = NULL;
		dev_err(codec->dev, "Failed to register beep device\n");
	}

	ret = device_create_file(codec->dev, &dev_attr_beep);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to create keyclick file: %d\n",
			ret);
	}
}

static void wm8962_free_beep(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);

	device_remove_file(codec->dev, &dev_attr_beep);
	input_unregister_device(wm8962->beep);
	cancel_work_sync(&wm8962->beep_work);
	wm8962->beep = NULL;

	snd_soc_update_bits(codec, WM8962_BEEP_GENERATOR_1, WM8962_BEEP_ENA,0);
}
#else
static void wm8962_init_beep(struct snd_soc_codec *codec)
{
}

static void wm8962_free_beep(struct snd_soc_codec *codec)
{
}
#endif

static void wm8962_set_gpio_mode(struct snd_soc_codec *codec, int gpio)
{
	int mask = 0;
	int val = 0;

	/* Some of the GPIOs are behind MFP configuration and need to
	 * be put into GPIO mode. */
	switch (gpio) {
	case 2:
		mask = WM8962_CLKOUT2_SEL_MASK;
		val = 1 << WM8962_CLKOUT2_SEL_SHIFT;
		break;
	case 3:
		mask = WM8962_CLKOUT3_SEL_MASK;
		val = 1 << WM8962_CLKOUT3_SEL_SHIFT;
		break;
	default:
		break;
	}

	if (mask)
		snd_soc_update_bits(codec, WM8962_ANALOGUE_CLOCKING1,
				    mask, val);
}

#ifdef CONFIG_GPIOLIB
static inline struct wm8962_priv *gpio_to_wm8962(struct gpio_chip *chip)
{
	return container_of(chip, struct wm8962_priv, gpio_chip);
}

static int wm8962_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct wm8962_priv *wm8962 = gpio_to_wm8962(chip);
	struct snd_soc_codec *codec = wm8962->codec;

	/* The WM8962 GPIOs aren't linearly numbered.  For simplicity
	 * we export linear numbers and error out if the unsupported
	 * ones are requsted.
	 */
	switch (offset + 1) {
	case 2:
	case 3:
	case 5:
	case 6:
		break;
	default:
		return -EINVAL;
	}

	wm8962_set_gpio_mode(codec, offset + 1);

	return 0;
}

static void wm8962_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8962_priv *wm8962 = gpio_to_wm8962(chip);
	struct snd_soc_codec *codec = wm8962->codec;

	snd_soc_update_bits(codec, WM8962_GPIO_BASE + offset,
			    WM8962_GP2_LVL, !!value << WM8962_GP2_LVL_SHIFT);
}

static int wm8962_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8962_priv *wm8962 = gpio_to_wm8962(chip);
	struct snd_soc_codec *codec = wm8962->codec;
	int val;

	/* Force function 1 (logic output) */
	val = (1 << WM8962_GP2_FN_SHIFT) | (value << WM8962_GP2_LVL_SHIFT);

	return snd_soc_update_bits(codec, WM8962_GPIO_BASE + offset,
				   WM8962_GP2_FN_MASK | WM8962_GP2_LVL, val);
}

static struct gpio_chip wm8962_template_chip = {
	.label			= "wm8962",
	.owner			= THIS_MODULE,
	.request		= wm8962_gpio_request,
	.direction_output	= wm8962_gpio_direction_out,
	.set			= wm8962_gpio_set,
	.can_sleep		= 1,
};

static void wm8962_init_gpio(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct wm8962_pdata *pdata = dev_get_platdata(codec->dev);
	int ret;

	wm8962->gpio_chip = wm8962_template_chip;
	wm8962->gpio_chip.ngpio = WM8962_MAX_GPIO;
	wm8962->gpio_chip.dev = codec->dev;

	if (pdata && pdata->gpio_base)
		wm8962->gpio_chip.base = pdata->gpio_base;
	else
		wm8962->gpio_chip.base = -1;

	ret = gpiochip_add(&wm8962->gpio_chip);
	if (ret != 0)
		dev_err(codec->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm8962_free_gpio(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = gpiochip_remove(&wm8962->gpio_chip);
	if (ret != 0)
		dev_err(codec->dev, "Failed to remove GPIOs: %d\n", ret);
}
#else
static void wm8962_init_gpio(struct snd_soc_codec *codec)
{
}

static void wm8962_free_gpio(struct snd_soc_codec *codec)
{
}
#endif

static int wm8962_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct wm8962_pdata *pdata = dev_get_platdata(codec->dev);
	struct i2c_client *i2c = container_of(codec->dev, struct i2c_client,
					      dev);
	u16 *reg_cache = codec->reg_cache;
	int i, trigger, irq_pol;
	bool dmicclk, dmicdat;

	wm8962->codec = codec;
	INIT_DELAYED_WORK(&wm8962->mic_work, wm8962_mic_work);
	init_completion(&wm8962->fll_lock);

	codec->cache_sync = 1;
	codec->dapm.idle_bias_off = 1;

	ret = snd_soc_codec_set_cache_io(codec, 16, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++)
		wm8962->supplies[i].supply = wm8962_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8962->supplies),
				 wm8962->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		goto err;
	}

	wm8962->disable_nb[0].notifier_call = wm8962_regulator_event_0;
	wm8962->disable_nb[1].notifier_call = wm8962_regulator_event_1;
	wm8962->disable_nb[2].notifier_call = wm8962_regulator_event_2;
	wm8962->disable_nb[3].notifier_call = wm8962_regulator_event_3;
	wm8962->disable_nb[4].notifier_call = wm8962_regulator_event_4;
	wm8962->disable_nb[5].notifier_call = wm8962_regulator_event_5;
	wm8962->disable_nb[6].notifier_call = wm8962_regulator_event_6;
	wm8962->disable_nb[7].notifier_call = wm8962_regulator_event_7;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++) {
		ret = regulator_register_notifier(wm8962->supplies[i].consumer,
						  &wm8962->disable_nb[i]);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8962->supplies),
				    wm8962->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = snd_soc_read(codec, WM8962_SOFTWARE_RESET);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (ret != wm8962_reg[WM8962_SOFTWARE_RESET]) {
		dev_err(codec->dev, "Device is not a WM8962, ID %x != %x\n",
			ret, wm8962_reg[WM8962_SOFTWARE_RESET]);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = snd_soc_read(codec, WM8962_RIGHT_INPUT_VOLUME);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_enable;
	}
	
	dev_info(codec->dev, "customer id %x revision %c\n",
		 (ret & WM8962_CUST_ID_MASK) >> WM8962_CUST_ID_SHIFT,
		 ((ret & WM8962_CHIP_REV_MASK) >> WM8962_CHIP_REV_SHIFT)
		 + 'A');

	ret = wm8962_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	/* SYSCLK defaults to on; make sure it is off so we can safely
	 * write to registers if the device is declocked.
	 */
	snd_soc_update_bits(codec, WM8962_CLOCKING2, WM8962_SYSCLK_ENA, 0);

	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);

	if (pdata) {
		/* Apply static configuration for GPIOs */
		for (i = 0; i < ARRAY_SIZE(pdata->gpio_init); i++)
			if (pdata->gpio_init[i]) {
				wm8962_set_gpio_mode(codec, i + 1);
				snd_soc_write(codec, 0x200 + i,
					      pdata->gpio_init[i] & 0xffff);
			}

		/* Put the speakers into mono mode? */
		if (pdata->spk_mono)
			reg_cache[WM8962_CLASS_D_CONTROL_2]
				|= WM8962_SPK_MONO;

		/* Micbias setup, detection enable and detection
		 * threasholds. */
		if (pdata->mic_cfg)
			snd_soc_update_bits(codec, WM8962_ADDITIONAL_CONTROL_4,
					    WM8962_MICDET_ENA |
					    WM8962_MICDET_THR_MASK |
					    WM8962_MICSHORT_THR_MASK |
					    WM8962_MICBIAS_LVL,
					    pdata->mic_cfg);
	}

	/* Latch volume update bits */
	snd_soc_update_bits(codec, WM8962_LEFT_INPUT_VOLUME,
			    WM8962_IN_VU, WM8962_IN_VU);
	snd_soc_update_bits(codec, WM8962_RIGHT_INPUT_VOLUME,
			    WM8962_IN_VU, WM8962_IN_VU);
	snd_soc_update_bits(codec, WM8962_LEFT_ADC_VOLUME,
			    WM8962_ADC_VU, WM8962_ADC_VU);
	snd_soc_update_bits(codec, WM8962_RIGHT_ADC_VOLUME,
			    WM8962_ADC_VU, WM8962_ADC_VU);
	snd_soc_update_bits(codec, WM8962_LEFT_DAC_VOLUME,
			    WM8962_DAC_VU, WM8962_DAC_VU);
	snd_soc_update_bits(codec, WM8962_RIGHT_DAC_VOLUME,
			    WM8962_DAC_VU, WM8962_DAC_VU);
	snd_soc_update_bits(codec, WM8962_SPKOUTL_VOLUME,
			    WM8962_SPKOUT_VU, WM8962_SPKOUT_VU);
	snd_soc_update_bits(codec, WM8962_SPKOUTR_VOLUME,
			    WM8962_SPKOUT_VU, WM8962_SPKOUT_VU);
	snd_soc_update_bits(codec, WM8962_HPOUTL_VOLUME,
			    WM8962_HPOUT_VU, WM8962_HPOUT_VU);
	snd_soc_update_bits(codec, WM8962_HPOUTR_VOLUME,
			    WM8962_HPOUT_VU, WM8962_HPOUT_VU);

	wm8962_add_widgets(codec);

	/* Save boards having to disable DMIC when not in use */
	dmicclk = false;
	dmicdat = false;
	for (i = 0; i < WM8962_MAX_GPIO; i++) {
		switch (snd_soc_read(codec, WM8962_GPIO_BASE + i)
			& WM8962_GP2_FN_MASK) {
		case WM8962_GPIO_FN_DMICCLK:
			dmicclk = true;
			break;
		case WM8962_GPIO_FN_DMICDAT:
			dmicdat = true;
			break;
		default:
			break;
		}
	}
	if (!dmicclk || !dmicdat) {
		dev_dbg(codec->dev, "DMIC not in use, disabling\n");
		snd_soc_dapm_nc_pin(&codec->dapm, "DMICDAT");
	}
	if (dmicclk != dmicdat)
		dev_warn(codec->dev, "DMIC GPIOs partially configured\n");

	wm8962_init_beep(codec);
	wm8962_init_gpio(codec);

	if (i2c->irq) {
		if (pdata && pdata->irq_active_low) {
			trigger = IRQF_TRIGGER_LOW;
			irq_pol = WM8962_IRQ_POL;
		} else {
			trigger = IRQF_TRIGGER_HIGH;
			irq_pol = 0;
		}

		snd_soc_update_bits(codec, WM8962_INTERRUPT_CONTROL,
				    WM8962_IRQ_POL, irq_pol);

		ret = request_threaded_irq(i2c->irq, NULL, wm8962_irq,
					   trigger | IRQF_ONESHOT,
					   "wm8962", codec);
		if (ret != 0) {
			dev_err(codec->dev, "Failed to request IRQ %d: %d\n",
				i2c->irq, ret);
			/* Non-fatal */
		} else {
			/* Enable some IRQs by default */
			snd_soc_update_bits(codec,
					    WM8962_INTERRUPT_STATUS_2_MASK,
					    WM8962_FLL_LOCK_EINT |
					    WM8962_TEMP_SHUT_EINT |
					    WM8962_FIFOS_ERR_EINT, 0);
		}
	}

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);
err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);
err:
	return ret;
}

static int wm8962_remove(struct snd_soc_codec *codec)
{
	struct wm8962_priv *wm8962 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *i2c = container_of(codec->dev, struct i2c_client,
					      dev);
	int i;

	if (i2c->irq)
		free_irq(i2c->irq, codec);

	cancel_delayed_work_sync(&wm8962->mic_work);

	wm8962_free_gpio(codec);
	wm8962_free_beep(codec);
	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++)
		regulator_unregister_notifier(wm8962->supplies[i].consumer,
					      &wm8962->disable_nb[i]);
	regulator_bulk_free(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8962 = {
	.probe =	wm8962_probe,
	.remove =	wm8962_remove,
	.resume =	wm8962_resume,
	.set_bias_level = wm8962_set_bias_level,
	.reg_cache_size = WM8962_MAX_REGISTER + 1,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8962_reg,
	.volatile_register = wm8962_volatile_register,
	.readable_register = wm8962_readable_register,
	.set_pll = wm8962_set_fll,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8962_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8962_priv *wm8962;
	int ret;

	wm8962 = kzalloc(sizeof(struct wm8962_priv), GFP_KERNEL);
	if (wm8962 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8962);

	ret = snd_soc_register_codec(&i2c->dev,
				     &soc_codec_dev_wm8962, &wm8962_dai, 1);
	if (ret < 0)
		kfree(wm8962);

	return ret;
}

static __devexit int wm8962_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8962_i2c_id[] = {
	{ "wm8962", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8962_i2c_id);

static struct i2c_driver wm8962_i2c_driver = {
	.driver = {
		.name = "wm8962",
		.owner = THIS_MODULE,
	},
	.probe =    wm8962_i2c_probe,
	.remove =   __devexit_p(wm8962_i2c_remove),
	.id_table = wm8962_i2c_id,
};
#endif

static int __init wm8962_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8962_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8962 I2C driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8962_modinit);

static void __exit wm8962_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8962_i2c_driver);
#endif
}
module_exit(wm8962_exit);

MODULE_DESCRIPTION("ASoC WM8962 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
