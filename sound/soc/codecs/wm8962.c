// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8962.c  --  WM8962 ALSA SoC Audio driver
 *
 * Copyright 2010-2 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/gcd.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
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
	struct wm8962_pdata pdata;
	struct regmap *regmap;
	struct snd_soc_component *component;

	int sysclk;
	int sysclk_rate;

	int bclk;  /* Desired BCLK */
	int lrclk;

	struct completion fll_lock;
	int fll_src;
	int fll_fref;
	int fll_fout;

	struct mutex dsp2_ena_lock;
	u16 dsp2_ena;

	struct delayed_work mic_work;
	struct snd_soc_jack *jack;

	struct regulator_bulk_data supplies[WM8962_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8962_NUM_SUPPLIES];

	struct input_dev *beep;
	struct work_struct beep_work;
	int beep_rate;

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif

	int irq;
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
		regcache_mark_dirty(wm8962->regmap);	\
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

static const struct reg_default wm8962_reg[] = {
	{ 0, 0x009F },   /* R0     - Left Input volume */
	{ 1, 0x049F },   /* R1     - Right Input volume */
	{ 2, 0x0000 },   /* R2     - HPOUTL volume */
	{ 3, 0x0000 },   /* R3     - HPOUTR volume */

	{ 5, 0x0018 },   /* R5     - ADC & DAC Control 1 */
	{ 6, 0x2008 },   /* R6     - ADC & DAC Control 2 */
	{ 7, 0x000A },   /* R7     - Audio Interface 0 */
	{ 8, 0x01E4 },   /* R8     - Clocking2 */
	{ 9, 0x0300 },   /* R9     - Audio Interface 1 */
	{ 10, 0x00C0 },  /* R10    - Left DAC volume */
	{ 11, 0x00C0 },  /* R11    - Right DAC volume */

	{ 14, 0x0040 },   /* R14    - Audio Interface 2 */
	{ 15, 0x6243 },   /* R15    - Software Reset */

	{ 17, 0x007B },   /* R17    - ALC1 */
	{ 18, 0x0000 },   /* R18    - ALC2 */
	{ 19, 0x1C32 },   /* R19    - ALC3 */
	{ 20, 0x3200 },   /* R20    - Noise Gate */
	{ 21, 0x00C0 },   /* R21    - Left ADC volume */
	{ 22, 0x00C0 },   /* R22    - Right ADC volume */
	{ 23, 0x0160 },   /* R23    - Additional control(1) */
	{ 24, 0x0000 },   /* R24    - Additional control(2) */
	{ 25, 0x0000 },   /* R25    - Pwr Mgmt (1) */
	{ 26, 0x0000 },   /* R26    - Pwr Mgmt (2) */
	{ 27, 0x0010 },   /* R27    - Additional Control (3) */
	{ 28, 0x0000 },   /* R28    - Anti-pop */

	{ 30, 0x005E },   /* R30    - Clocking 3 */
	{ 31, 0x0000 },   /* R31    - Input mixer control (1) */
	{ 32, 0x0145 },   /* R32    - Left input mixer volume */
	{ 33, 0x0145 },   /* R33    - Right input mixer volume */
	{ 34, 0x0009 },   /* R34    - Input mixer control (2) */
	{ 35, 0x0003 },   /* R35    - Input bias control */
	{ 37, 0x0008 },   /* R37    - Left input PGA control */
	{ 38, 0x0008 },   /* R38    - Right input PGA control */

	{ 40, 0x0000 },   /* R40    - SPKOUTL volume */
	{ 41, 0x0000 },   /* R41    - SPKOUTR volume */

	{ 49, 0x0010 },   /* R49    - Class D Control 1 */
	{ 51, 0x0003 },   /* R51    - Class D Control 2 */

	{ 56, 0x0506 },   /* R56    - Clocking 4 */
	{ 57, 0x0000 },   /* R57    - DAC DSP Mixing (1) */
	{ 58, 0x0000 },   /* R58    - DAC DSP Mixing (2) */

	{ 60, 0x0300 },   /* R60    - DC Servo 0 */
	{ 61, 0x0300 },   /* R61    - DC Servo 1 */

	{ 64, 0x0810 },   /* R64    - DC Servo 4 */

	{ 68, 0x001B },   /* R68    - Analogue PGA Bias */
	{ 69, 0x0000 },   /* R69    - Analogue HP 0 */

	{ 71, 0x01FB },   /* R71    - Analogue HP 2 */
	{ 72, 0x0000 },   /* R72    - Charge Pump 1 */

	{ 82, 0x0004 },   /* R82    - Charge Pump B */

	{ 87, 0x0000 },   /* R87    - Write Sequencer Control 1 */

	{ 90, 0x0000 },   /* R90    - Write Sequencer Control 2 */

	{ 93, 0x0000 },   /* R93    - Write Sequencer Control 3 */
	{ 94, 0x0000 },   /* R94    - Control Interface */

	{ 99, 0x0000 },   /* R99    - Mixer Enables */
	{ 100, 0x0000 },   /* R100   - Headphone Mixer (1) */
	{ 101, 0x0000 },   /* R101   - Headphone Mixer (2) */
	{ 102, 0x013F },   /* R102   - Headphone Mixer (3) */
	{ 103, 0x013F },   /* R103   - Headphone Mixer (4) */

	{ 105, 0x0000 },   /* R105   - Speaker Mixer (1) */
	{ 106, 0x0000 },   /* R106   - Speaker Mixer (2) */
	{ 107, 0x013F },   /* R107   - Speaker Mixer (3) */
	{ 108, 0x013F },   /* R108   - Speaker Mixer (4) */
	{ 109, 0x0003 },   /* R109   - Speaker Mixer (5) */
	{ 110, 0x0002 },   /* R110   - Beep Generator (1) */

	{ 115, 0x0006 },   /* R115   - Oscillator Trim (3) */
	{ 116, 0x0026 },   /* R116   - Oscillator Trim (4) */

	{ 119, 0x0000 },   /* R119   - Oscillator Trim (7) */

	{ 124, 0x0011 },   /* R124   - Analogue Clocking1 */
	{ 125, 0x004B },   /* R125   - Analogue Clocking2 */
	{ 126, 0x000D },   /* R126   - Analogue Clocking3 */
	{ 127, 0x0000 },   /* R127   - PLL Software Reset */

	{ 131, 0x0000 },   /* R131   - PLL 4 */

	{ 136, 0x0067 },   /* R136   - PLL 9 */
	{ 137, 0x001C },   /* R137   - PLL 10 */
	{ 138, 0x0071 },   /* R138   - PLL 11 */
	{ 139, 0x00C7 },   /* R139   - PLL 12 */
	{ 140, 0x0067 },   /* R140   - PLL 13 */
	{ 141, 0x0048 },   /* R141   - PLL 14 */
	{ 142, 0x0022 },   /* R142   - PLL 15 */
	{ 143, 0x0097 },   /* R143   - PLL 16 */

	{ 155, 0x000C },   /* R155   - FLL Control (1) */
	{ 156, 0x0039 },   /* R156   - FLL Control (2) */
	{ 157, 0x0180 },   /* R157   - FLL Control (3) */

	{ 159, 0x0032 },   /* R159   - FLL Control (5) */
	{ 160, 0x0018 },   /* R160   - FLL Control (6) */
	{ 161, 0x007D },   /* R161   - FLL Control (7) */
	{ 162, 0x0008 },   /* R162   - FLL Control (8) */

	{ 252, 0x0005 },   /* R252   - General test 1 */

	{ 256, 0x0000 },   /* R256   - DF1 */
	{ 257, 0x0000 },   /* R257   - DF2 */
	{ 258, 0x0000 },   /* R258   - DF3 */
	{ 259, 0x0000 },   /* R259   - DF4 */
	{ 260, 0x0000 },   /* R260   - DF5 */
	{ 261, 0x0000 },   /* R261   - DF6 */
	{ 262, 0x0000 },   /* R262   - DF7 */

	{ 264, 0x0000 },   /* R264   - LHPF1 */
	{ 265, 0x0000 },   /* R265   - LHPF2 */

	{ 268, 0x0000 },   /* R268   - THREED1 */
	{ 269, 0x0000 },   /* R269   - THREED2 */
	{ 270, 0x0000 },   /* R270   - THREED3 */
	{ 271, 0x0000 },   /* R271   - THREED4 */

	{ 276, 0x000C },   /* R276   - DRC 1 */
	{ 277, 0x0925 },   /* R277   - DRC 2 */
	{ 278, 0x0000 },   /* R278   - DRC 3 */
	{ 279, 0x0000 },   /* R279   - DRC 4 */
	{ 280, 0x0000 },   /* R280   - DRC 5 */

	{ 285, 0x0000 },   /* R285   - Tloopback */

	{ 335, 0x0004 },   /* R335   - EQ1 */
	{ 336, 0x6318 },   /* R336   - EQ2 */
	{ 337, 0x6300 },   /* R337   - EQ3 */
	{ 338, 0x0FCA },   /* R338   - EQ4 */
	{ 339, 0x0400 },   /* R339   - EQ5 */
	{ 340, 0x00D8 },   /* R340   - EQ6 */
	{ 341, 0x1EB5 },   /* R341   - EQ7 */
	{ 342, 0xF145 },   /* R342   - EQ8 */
	{ 343, 0x0B75 },   /* R343   - EQ9 */
	{ 344, 0x01C5 },   /* R344   - EQ10 */
	{ 345, 0x1C58 },   /* R345   - EQ11 */
	{ 346, 0xF373 },   /* R346   - EQ12 */
	{ 347, 0x0A54 },   /* R347   - EQ13 */
	{ 348, 0x0558 },   /* R348   - EQ14 */
	{ 349, 0x168E },   /* R349   - EQ15 */
	{ 350, 0xF829 },   /* R350   - EQ16 */
	{ 351, 0x07AD },   /* R351   - EQ17 */
	{ 352, 0x1103 },   /* R352   - EQ18 */
	{ 353, 0x0564 },   /* R353   - EQ19 */
	{ 354, 0x0559 },   /* R354   - EQ20 */
	{ 355, 0x4000 },   /* R355   - EQ21 */
	{ 356, 0x6318 },   /* R356   - EQ22 */
	{ 357, 0x6300 },   /* R357   - EQ23 */
	{ 358, 0x0FCA },   /* R358   - EQ24 */
	{ 359, 0x0400 },   /* R359   - EQ25 */
	{ 360, 0x00D8 },   /* R360   - EQ26 */
	{ 361, 0x1EB5 },   /* R361   - EQ27 */
	{ 362, 0xF145 },   /* R362   - EQ28 */
	{ 363, 0x0B75 },   /* R363   - EQ29 */
	{ 364, 0x01C5 },   /* R364   - EQ30 */
	{ 365, 0x1C58 },   /* R365   - EQ31 */
	{ 366, 0xF373 },   /* R366   - EQ32 */
	{ 367, 0x0A54 },   /* R367   - EQ33 */
	{ 368, 0x0558 },   /* R368   - EQ34 */
	{ 369, 0x168E },   /* R369   - EQ35 */
	{ 370, 0xF829 },   /* R370   - EQ36 */
	{ 371, 0x07AD },   /* R371   - EQ37 */
	{ 372, 0x1103 },   /* R372   - EQ38 */
	{ 373, 0x0564 },   /* R373   - EQ39 */
	{ 374, 0x0559 },   /* R374   - EQ40 */
	{ 375, 0x4000 },   /* R375   - EQ41 */

	{ 513, 0x0000 },   /* R513   - GPIO 2 */
	{ 514, 0x0000 },   /* R514   - GPIO 3 */

	{ 516, 0x8100 },   /* R516   - GPIO 5 */
	{ 517, 0x8100 },   /* R517   - GPIO 6 */

	{ 568, 0x0030 },   /* R568   - Interrupt Status 1 Mask */
	{ 569, 0xFFED },   /* R569   - Interrupt Status 2 Mask */

	{ 576, 0x0000 },   /* R576   - Interrupt Control */

	{ 584, 0x002D },   /* R584   - IRQ Debounce */

	{ 586, 0x0000 },   /* R586   -  MICINT Source Pol */

	{ 768, 0x1C00 },   /* R768   - DSP2 Power Management */

	{ 8192, 0x0000 },   /* R8192  - DSP2 Instruction RAM 0 */

	{ 9216, 0x0030 },   /* R9216  - DSP2 Address RAM 2 */
	{ 9217, 0x0000 },   /* R9217  - DSP2 Address RAM 1 */
	{ 9218, 0x0000 },   /* R9218  - DSP2 Address RAM 0 */

	{ 12288, 0x0000 },   /* R12288 - DSP2 Data1 RAM 1 */
	{ 12289, 0x0000 },   /* R12289 - DSP2 Data1 RAM 0 */

	{ 13312, 0x0000 },   /* R13312 - DSP2 Data2 RAM 1 */
	{ 13313, 0x0000 },   /* R13313 - DSP2 Data2 RAM 0 */

	{ 14336, 0x0000 },   /* R14336 - DSP2 Data3 RAM 1 */
	{ 14337, 0x0000 },   /* R14337 - DSP2 Data3 RAM 0 */

	{ 15360, 0x000A },   /* R15360 - DSP2 Coeff RAM 0 */

	{ 16384, 0x0000 },   /* R16384 - RETUNEADC_SHARED_COEFF_1 */
	{ 16385, 0x0000 },   /* R16385 - RETUNEADC_SHARED_COEFF_0 */
	{ 16386, 0x0000 },   /* R16386 - RETUNEDAC_SHARED_COEFF_1 */
	{ 16387, 0x0000 },   /* R16387 - RETUNEDAC_SHARED_COEFF_0 */
	{ 16388, 0x0000 },   /* R16388 - SOUNDSTAGE_ENABLES_1 */
	{ 16389, 0x0000 },   /* R16389 - SOUNDSTAGE_ENABLES_0 */

	{ 16896, 0x0002 },   /* R16896 - HDBASS_AI_1 */
	{ 16897, 0xBD12 },   /* R16897 - HDBASS_AI_0 */
	{ 16898, 0x007C },   /* R16898 - HDBASS_AR_1 */
	{ 16899, 0x586C },   /* R16899 - HDBASS_AR_0 */
	{ 16900, 0x0053 },   /* R16900 - HDBASS_B_1 */
	{ 16901, 0x8121 },   /* R16901 - HDBASS_B_0 */
	{ 16902, 0x003F },   /* R16902 - HDBASS_K_1 */
	{ 16903, 0x8BD8 },   /* R16903 - HDBASS_K_0 */
	{ 16904, 0x0032 },   /* R16904 - HDBASS_N1_1 */
	{ 16905, 0xF52D },   /* R16905 - HDBASS_N1_0 */
	{ 16906, 0x0065 },   /* R16906 - HDBASS_N2_1 */
	{ 16907, 0xAC8C },   /* R16907 - HDBASS_N2_0 */
	{ 16908, 0x006B },   /* R16908 - HDBASS_N3_1 */
	{ 16909, 0xE087 },   /* R16909 - HDBASS_N3_0 */
	{ 16910, 0x0072 },   /* R16910 - HDBASS_N4_1 */
	{ 16911, 0x1483 },   /* R16911 - HDBASS_N4_0 */
	{ 16912, 0x0072 },   /* R16912 - HDBASS_N5_1 */
	{ 16913, 0x1483 },   /* R16913 - HDBASS_N5_0 */
	{ 16914, 0x0043 },   /* R16914 - HDBASS_X1_1 */
	{ 16915, 0x3525 },   /* R16915 - HDBASS_X1_0 */
	{ 16916, 0x0006 },   /* R16916 - HDBASS_X2_1 */
	{ 16917, 0x6A4A },   /* R16917 - HDBASS_X2_0 */
	{ 16918, 0x0043 },   /* R16918 - HDBASS_X3_1 */
	{ 16919, 0x6079 },   /* R16919 - HDBASS_X3_0 */
	{ 16920, 0x0008 },   /* R16920 - HDBASS_ATK_1 */
	{ 16921, 0x0000 },   /* R16921 - HDBASS_ATK_0 */
	{ 16922, 0x0001 },   /* R16922 - HDBASS_DCY_1 */
	{ 16923, 0x0000 },   /* R16923 - HDBASS_DCY_0 */
	{ 16924, 0x0059 },   /* R16924 - HDBASS_PG_1 */
	{ 16925, 0x999A },   /* R16925 - HDBASS_PG_0 */

	{ 17408, 0x0083 },   /* R17408 - HPF_C_1 */
	{ 17409, 0x98AD },   /* R17409 - HPF_C_0 */

	{ 17920, 0x007F },   /* R17920 - ADCL_RETUNE_C1_1 */
	{ 17921, 0xFFFF },   /* R17921 - ADCL_RETUNE_C1_0 */
	{ 17922, 0x0000 },   /* R17922 - ADCL_RETUNE_C2_1 */
	{ 17923, 0x0000 },   /* R17923 - ADCL_RETUNE_C2_0 */
	{ 17924, 0x0000 },   /* R17924 - ADCL_RETUNE_C3_1 */
	{ 17925, 0x0000 },   /* R17925 - ADCL_RETUNE_C3_0 */
	{ 17926, 0x0000 },   /* R17926 - ADCL_RETUNE_C4_1 */
	{ 17927, 0x0000 },   /* R17927 - ADCL_RETUNE_C4_0 */
	{ 17928, 0x0000 },   /* R17928 - ADCL_RETUNE_C5_1 */
	{ 17929, 0x0000 },   /* R17929 - ADCL_RETUNE_C5_0 */
	{ 17930, 0x0000 },   /* R17930 - ADCL_RETUNE_C6_1 */
	{ 17931, 0x0000 },   /* R17931 - ADCL_RETUNE_C6_0 */
	{ 17932, 0x0000 },   /* R17932 - ADCL_RETUNE_C7_1 */
	{ 17933, 0x0000 },   /* R17933 - ADCL_RETUNE_C7_0 */
	{ 17934, 0x0000 },   /* R17934 - ADCL_RETUNE_C8_1 */
	{ 17935, 0x0000 },   /* R17935 - ADCL_RETUNE_C8_0 */
	{ 17936, 0x0000 },   /* R17936 - ADCL_RETUNE_C9_1 */
	{ 17937, 0x0000 },   /* R17937 - ADCL_RETUNE_C9_0 */
	{ 17938, 0x0000 },   /* R17938 - ADCL_RETUNE_C10_1 */
	{ 17939, 0x0000 },   /* R17939 - ADCL_RETUNE_C10_0 */
	{ 17940, 0x0000 },   /* R17940 - ADCL_RETUNE_C11_1 */
	{ 17941, 0x0000 },   /* R17941 - ADCL_RETUNE_C11_0 */
	{ 17942, 0x0000 },   /* R17942 - ADCL_RETUNE_C12_1 */
	{ 17943, 0x0000 },   /* R17943 - ADCL_RETUNE_C12_0 */
	{ 17944, 0x0000 },   /* R17944 - ADCL_RETUNE_C13_1 */
	{ 17945, 0x0000 },   /* R17945 - ADCL_RETUNE_C13_0 */
	{ 17946, 0x0000 },   /* R17946 - ADCL_RETUNE_C14_1 */
	{ 17947, 0x0000 },   /* R17947 - ADCL_RETUNE_C14_0 */
	{ 17948, 0x0000 },   /* R17948 - ADCL_RETUNE_C15_1 */
	{ 17949, 0x0000 },   /* R17949 - ADCL_RETUNE_C15_0 */
	{ 17950, 0x0000 },   /* R17950 - ADCL_RETUNE_C16_1 */
	{ 17951, 0x0000 },   /* R17951 - ADCL_RETUNE_C16_0 */
	{ 17952, 0x0000 },   /* R17952 - ADCL_RETUNE_C17_1 */
	{ 17953, 0x0000 },   /* R17953 - ADCL_RETUNE_C17_0 */
	{ 17954, 0x0000 },   /* R17954 - ADCL_RETUNE_C18_1 */
	{ 17955, 0x0000 },   /* R17955 - ADCL_RETUNE_C18_0 */
	{ 17956, 0x0000 },   /* R17956 - ADCL_RETUNE_C19_1 */
	{ 17957, 0x0000 },   /* R17957 - ADCL_RETUNE_C19_0 */
	{ 17958, 0x0000 },   /* R17958 - ADCL_RETUNE_C20_1 */
	{ 17959, 0x0000 },   /* R17959 - ADCL_RETUNE_C20_0 */
	{ 17960, 0x0000 },   /* R17960 - ADCL_RETUNE_C21_1 */
	{ 17961, 0x0000 },   /* R17961 - ADCL_RETUNE_C21_0 */
	{ 17962, 0x0000 },   /* R17962 - ADCL_RETUNE_C22_1 */
	{ 17963, 0x0000 },   /* R17963 - ADCL_RETUNE_C22_0 */
	{ 17964, 0x0000 },   /* R17964 - ADCL_RETUNE_C23_1 */
	{ 17965, 0x0000 },   /* R17965 - ADCL_RETUNE_C23_0 */
	{ 17966, 0x0000 },   /* R17966 - ADCL_RETUNE_C24_1 */
	{ 17967, 0x0000 },   /* R17967 - ADCL_RETUNE_C24_0 */
	{ 17968, 0x0000 },   /* R17968 - ADCL_RETUNE_C25_1 */
	{ 17969, 0x0000 },   /* R17969 - ADCL_RETUNE_C25_0 */
	{ 17970, 0x0000 },   /* R17970 - ADCL_RETUNE_C26_1 */
	{ 17971, 0x0000 },   /* R17971 - ADCL_RETUNE_C26_0 */
	{ 17972, 0x0000 },   /* R17972 - ADCL_RETUNE_C27_1 */
	{ 17973, 0x0000 },   /* R17973 - ADCL_RETUNE_C27_0 */
	{ 17974, 0x0000 },   /* R17974 - ADCL_RETUNE_C28_1 */
	{ 17975, 0x0000 },   /* R17975 - ADCL_RETUNE_C28_0 */
	{ 17976, 0x0000 },   /* R17976 - ADCL_RETUNE_C29_1 */
	{ 17977, 0x0000 },   /* R17977 - ADCL_RETUNE_C29_0 */
	{ 17978, 0x0000 },   /* R17978 - ADCL_RETUNE_C30_1 */
	{ 17979, 0x0000 },   /* R17979 - ADCL_RETUNE_C30_0 */
	{ 17980, 0x0000 },   /* R17980 - ADCL_RETUNE_C31_1 */
	{ 17981, 0x0000 },   /* R17981 - ADCL_RETUNE_C31_0 */
	{ 17982, 0x0000 },   /* R17982 - ADCL_RETUNE_C32_1 */
	{ 17983, 0x0000 },   /* R17983 - ADCL_RETUNE_C32_0 */

	{ 18432, 0x0020 },   /* R18432 - RETUNEADC_PG2_1 */
	{ 18433, 0x0000 },   /* R18433 - RETUNEADC_PG2_0 */
	{ 18434, 0x0040 },   /* R18434 - RETUNEADC_PG_1 */
	{ 18435, 0x0000 },   /* R18435 - RETUNEADC_PG_0 */

	{ 18944, 0x007F },   /* R18944 - ADCR_RETUNE_C1_1 */
	{ 18945, 0xFFFF },   /* R18945 - ADCR_RETUNE_C1_0 */
	{ 18946, 0x0000 },   /* R18946 - ADCR_RETUNE_C2_1 */
	{ 18947, 0x0000 },   /* R18947 - ADCR_RETUNE_C2_0 */
	{ 18948, 0x0000 },   /* R18948 - ADCR_RETUNE_C3_1 */
	{ 18949, 0x0000 },   /* R18949 - ADCR_RETUNE_C3_0 */
	{ 18950, 0x0000 },   /* R18950 - ADCR_RETUNE_C4_1 */
	{ 18951, 0x0000 },   /* R18951 - ADCR_RETUNE_C4_0 */
	{ 18952, 0x0000 },   /* R18952 - ADCR_RETUNE_C5_1 */
	{ 18953, 0x0000 },   /* R18953 - ADCR_RETUNE_C5_0 */
	{ 18954, 0x0000 },   /* R18954 - ADCR_RETUNE_C6_1 */
	{ 18955, 0x0000 },   /* R18955 - ADCR_RETUNE_C6_0 */
	{ 18956, 0x0000 },   /* R18956 - ADCR_RETUNE_C7_1 */
	{ 18957, 0x0000 },   /* R18957 - ADCR_RETUNE_C7_0 */
	{ 18958, 0x0000 },   /* R18958 - ADCR_RETUNE_C8_1 */
	{ 18959, 0x0000 },   /* R18959 - ADCR_RETUNE_C8_0 */
	{ 18960, 0x0000 },   /* R18960 - ADCR_RETUNE_C9_1 */
	{ 18961, 0x0000 },   /* R18961 - ADCR_RETUNE_C9_0 */
	{ 18962, 0x0000 },   /* R18962 - ADCR_RETUNE_C10_1 */
	{ 18963, 0x0000 },   /* R18963 - ADCR_RETUNE_C10_0 */
	{ 18964, 0x0000 },   /* R18964 - ADCR_RETUNE_C11_1 */
	{ 18965, 0x0000 },   /* R18965 - ADCR_RETUNE_C11_0 */
	{ 18966, 0x0000 },   /* R18966 - ADCR_RETUNE_C12_1 */
	{ 18967, 0x0000 },   /* R18967 - ADCR_RETUNE_C12_0 */
	{ 18968, 0x0000 },   /* R18968 - ADCR_RETUNE_C13_1 */
	{ 18969, 0x0000 },   /* R18969 - ADCR_RETUNE_C13_0 */
	{ 18970, 0x0000 },   /* R18970 - ADCR_RETUNE_C14_1 */
	{ 18971, 0x0000 },   /* R18971 - ADCR_RETUNE_C14_0 */
	{ 18972, 0x0000 },   /* R18972 - ADCR_RETUNE_C15_1 */
	{ 18973, 0x0000 },   /* R18973 - ADCR_RETUNE_C15_0 */
	{ 18974, 0x0000 },   /* R18974 - ADCR_RETUNE_C16_1 */
	{ 18975, 0x0000 },   /* R18975 - ADCR_RETUNE_C16_0 */
	{ 18976, 0x0000 },   /* R18976 - ADCR_RETUNE_C17_1 */
	{ 18977, 0x0000 },   /* R18977 - ADCR_RETUNE_C17_0 */
	{ 18978, 0x0000 },   /* R18978 - ADCR_RETUNE_C18_1 */
	{ 18979, 0x0000 },   /* R18979 - ADCR_RETUNE_C18_0 */
	{ 18980, 0x0000 },   /* R18980 - ADCR_RETUNE_C19_1 */
	{ 18981, 0x0000 },   /* R18981 - ADCR_RETUNE_C19_0 */
	{ 18982, 0x0000 },   /* R18982 - ADCR_RETUNE_C20_1 */
	{ 18983, 0x0000 },   /* R18983 - ADCR_RETUNE_C20_0 */
	{ 18984, 0x0000 },   /* R18984 - ADCR_RETUNE_C21_1 */
	{ 18985, 0x0000 },   /* R18985 - ADCR_RETUNE_C21_0 */
	{ 18986, 0x0000 },   /* R18986 - ADCR_RETUNE_C22_1 */
	{ 18987, 0x0000 },   /* R18987 - ADCR_RETUNE_C22_0 */
	{ 18988, 0x0000 },   /* R18988 - ADCR_RETUNE_C23_1 */
	{ 18989, 0x0000 },   /* R18989 - ADCR_RETUNE_C23_0 */
	{ 18990, 0x0000 },   /* R18990 - ADCR_RETUNE_C24_1 */
	{ 18991, 0x0000 },   /* R18991 - ADCR_RETUNE_C24_0 */
	{ 18992, 0x0000 },   /* R18992 - ADCR_RETUNE_C25_1 */
	{ 18993, 0x0000 },   /* R18993 - ADCR_RETUNE_C25_0 */
	{ 18994, 0x0000 },   /* R18994 - ADCR_RETUNE_C26_1 */
	{ 18995, 0x0000 },   /* R18995 - ADCR_RETUNE_C26_0 */
	{ 18996, 0x0000 },   /* R18996 - ADCR_RETUNE_C27_1 */
	{ 18997, 0x0000 },   /* R18997 - ADCR_RETUNE_C27_0 */
	{ 18998, 0x0000 },   /* R18998 - ADCR_RETUNE_C28_1 */
	{ 18999, 0x0000 },   /* R18999 - ADCR_RETUNE_C28_0 */
	{ 19000, 0x0000 },   /* R19000 - ADCR_RETUNE_C29_1 */
	{ 19001, 0x0000 },   /* R19001 - ADCR_RETUNE_C29_0 */
	{ 19002, 0x0000 },   /* R19002 - ADCR_RETUNE_C30_1 */
	{ 19003, 0x0000 },   /* R19003 - ADCR_RETUNE_C30_0 */
	{ 19004, 0x0000 },   /* R19004 - ADCR_RETUNE_C31_1 */
	{ 19005, 0x0000 },   /* R19005 - ADCR_RETUNE_C31_0 */
	{ 19006, 0x0000 },   /* R19006 - ADCR_RETUNE_C32_1 */
	{ 19007, 0x0000 },   /* R19007 - ADCR_RETUNE_C32_0 */

	{ 19456, 0x007F },   /* R19456 - DACL_RETUNE_C1_1 */
	{ 19457, 0xFFFF },   /* R19457 - DACL_RETUNE_C1_0 */
	{ 19458, 0x0000 },   /* R19458 - DACL_RETUNE_C2_1 */
	{ 19459, 0x0000 },   /* R19459 - DACL_RETUNE_C2_0 */
	{ 19460, 0x0000 },   /* R19460 - DACL_RETUNE_C3_1 */
	{ 19461, 0x0000 },   /* R19461 - DACL_RETUNE_C3_0 */
	{ 19462, 0x0000 },   /* R19462 - DACL_RETUNE_C4_1 */
	{ 19463, 0x0000 },   /* R19463 - DACL_RETUNE_C4_0 */
	{ 19464, 0x0000 },   /* R19464 - DACL_RETUNE_C5_1 */
	{ 19465, 0x0000 },   /* R19465 - DACL_RETUNE_C5_0 */
	{ 19466, 0x0000 },   /* R19466 - DACL_RETUNE_C6_1 */
	{ 19467, 0x0000 },   /* R19467 - DACL_RETUNE_C6_0 */
	{ 19468, 0x0000 },   /* R19468 - DACL_RETUNE_C7_1 */
	{ 19469, 0x0000 },   /* R19469 - DACL_RETUNE_C7_0 */
	{ 19470, 0x0000 },   /* R19470 - DACL_RETUNE_C8_1 */
	{ 19471, 0x0000 },   /* R19471 - DACL_RETUNE_C8_0 */
	{ 19472, 0x0000 },   /* R19472 - DACL_RETUNE_C9_1 */
	{ 19473, 0x0000 },   /* R19473 - DACL_RETUNE_C9_0 */
	{ 19474, 0x0000 },   /* R19474 - DACL_RETUNE_C10_1 */
	{ 19475, 0x0000 },   /* R19475 - DACL_RETUNE_C10_0 */
	{ 19476, 0x0000 },   /* R19476 - DACL_RETUNE_C11_1 */
	{ 19477, 0x0000 },   /* R19477 - DACL_RETUNE_C11_0 */
	{ 19478, 0x0000 },   /* R19478 - DACL_RETUNE_C12_1 */
	{ 19479, 0x0000 },   /* R19479 - DACL_RETUNE_C12_0 */
	{ 19480, 0x0000 },   /* R19480 - DACL_RETUNE_C13_1 */
	{ 19481, 0x0000 },   /* R19481 - DACL_RETUNE_C13_0 */
	{ 19482, 0x0000 },   /* R19482 - DACL_RETUNE_C14_1 */
	{ 19483, 0x0000 },   /* R19483 - DACL_RETUNE_C14_0 */
	{ 19484, 0x0000 },   /* R19484 - DACL_RETUNE_C15_1 */
	{ 19485, 0x0000 },   /* R19485 - DACL_RETUNE_C15_0 */
	{ 19486, 0x0000 },   /* R19486 - DACL_RETUNE_C16_1 */
	{ 19487, 0x0000 },   /* R19487 - DACL_RETUNE_C16_0 */
	{ 19488, 0x0000 },   /* R19488 - DACL_RETUNE_C17_1 */
	{ 19489, 0x0000 },   /* R19489 - DACL_RETUNE_C17_0 */
	{ 19490, 0x0000 },   /* R19490 - DACL_RETUNE_C18_1 */
	{ 19491, 0x0000 },   /* R19491 - DACL_RETUNE_C18_0 */
	{ 19492, 0x0000 },   /* R19492 - DACL_RETUNE_C19_1 */
	{ 19493, 0x0000 },   /* R19493 - DACL_RETUNE_C19_0 */
	{ 19494, 0x0000 },   /* R19494 - DACL_RETUNE_C20_1 */
	{ 19495, 0x0000 },   /* R19495 - DACL_RETUNE_C20_0 */
	{ 19496, 0x0000 },   /* R19496 - DACL_RETUNE_C21_1 */
	{ 19497, 0x0000 },   /* R19497 - DACL_RETUNE_C21_0 */
	{ 19498, 0x0000 },   /* R19498 - DACL_RETUNE_C22_1 */
	{ 19499, 0x0000 },   /* R19499 - DACL_RETUNE_C22_0 */
	{ 19500, 0x0000 },   /* R19500 - DACL_RETUNE_C23_1 */
	{ 19501, 0x0000 },   /* R19501 - DACL_RETUNE_C23_0 */
	{ 19502, 0x0000 },   /* R19502 - DACL_RETUNE_C24_1 */
	{ 19503, 0x0000 },   /* R19503 - DACL_RETUNE_C24_0 */
	{ 19504, 0x0000 },   /* R19504 - DACL_RETUNE_C25_1 */
	{ 19505, 0x0000 },   /* R19505 - DACL_RETUNE_C25_0 */
	{ 19506, 0x0000 },   /* R19506 - DACL_RETUNE_C26_1 */
	{ 19507, 0x0000 },   /* R19507 - DACL_RETUNE_C26_0 */
	{ 19508, 0x0000 },   /* R19508 - DACL_RETUNE_C27_1 */
	{ 19509, 0x0000 },   /* R19509 - DACL_RETUNE_C27_0 */
	{ 19510, 0x0000 },   /* R19510 - DACL_RETUNE_C28_1 */
	{ 19511, 0x0000 },   /* R19511 - DACL_RETUNE_C28_0 */
	{ 19512, 0x0000 },   /* R19512 - DACL_RETUNE_C29_1 */
	{ 19513, 0x0000 },   /* R19513 - DACL_RETUNE_C29_0 */
	{ 19514, 0x0000 },   /* R19514 - DACL_RETUNE_C30_1 */
	{ 19515, 0x0000 },   /* R19515 - DACL_RETUNE_C30_0 */
	{ 19516, 0x0000 },   /* R19516 - DACL_RETUNE_C31_1 */
	{ 19517, 0x0000 },   /* R19517 - DACL_RETUNE_C31_0 */
	{ 19518, 0x0000 },   /* R19518 - DACL_RETUNE_C32_1 */
	{ 19519, 0x0000 },   /* R19519 - DACL_RETUNE_C32_0 */

	{ 19968, 0x0020 },   /* R19968 - RETUNEDAC_PG2_1 */
	{ 19969, 0x0000 },   /* R19969 - RETUNEDAC_PG2_0 */
	{ 19970, 0x0040 },   /* R19970 - RETUNEDAC_PG_1 */
	{ 19971, 0x0000 },   /* R19971 - RETUNEDAC_PG_0 */

	{ 20480, 0x007F },   /* R20480 - DACR_RETUNE_C1_1 */
	{ 20481, 0xFFFF },   /* R20481 - DACR_RETUNE_C1_0 */
	{ 20482, 0x0000 },   /* R20482 - DACR_RETUNE_C2_1 */
	{ 20483, 0x0000 },   /* R20483 - DACR_RETUNE_C2_0 */
	{ 20484, 0x0000 },   /* R20484 - DACR_RETUNE_C3_1 */
	{ 20485, 0x0000 },   /* R20485 - DACR_RETUNE_C3_0 */
	{ 20486, 0x0000 },   /* R20486 - DACR_RETUNE_C4_1 */
	{ 20487, 0x0000 },   /* R20487 - DACR_RETUNE_C4_0 */
	{ 20488, 0x0000 },   /* R20488 - DACR_RETUNE_C5_1 */
	{ 20489, 0x0000 },   /* R20489 - DACR_RETUNE_C5_0 */
	{ 20490, 0x0000 },   /* R20490 - DACR_RETUNE_C6_1 */
	{ 20491, 0x0000 },   /* R20491 - DACR_RETUNE_C6_0 */
	{ 20492, 0x0000 },   /* R20492 - DACR_RETUNE_C7_1 */
	{ 20493, 0x0000 },   /* R20493 - DACR_RETUNE_C7_0 */
	{ 20494, 0x0000 },   /* R20494 - DACR_RETUNE_C8_1 */
	{ 20495, 0x0000 },   /* R20495 - DACR_RETUNE_C8_0 */
	{ 20496, 0x0000 },   /* R20496 - DACR_RETUNE_C9_1 */
	{ 20497, 0x0000 },   /* R20497 - DACR_RETUNE_C9_0 */
	{ 20498, 0x0000 },   /* R20498 - DACR_RETUNE_C10_1 */
	{ 20499, 0x0000 },   /* R20499 - DACR_RETUNE_C10_0 */
	{ 20500, 0x0000 },   /* R20500 - DACR_RETUNE_C11_1 */
	{ 20501, 0x0000 },   /* R20501 - DACR_RETUNE_C11_0 */
	{ 20502, 0x0000 },   /* R20502 - DACR_RETUNE_C12_1 */
	{ 20503, 0x0000 },   /* R20503 - DACR_RETUNE_C12_0 */
	{ 20504, 0x0000 },   /* R20504 - DACR_RETUNE_C13_1 */
	{ 20505, 0x0000 },   /* R20505 - DACR_RETUNE_C13_0 */
	{ 20506, 0x0000 },   /* R20506 - DACR_RETUNE_C14_1 */
	{ 20507, 0x0000 },   /* R20507 - DACR_RETUNE_C14_0 */
	{ 20508, 0x0000 },   /* R20508 - DACR_RETUNE_C15_1 */
	{ 20509, 0x0000 },   /* R20509 - DACR_RETUNE_C15_0 */
	{ 20510, 0x0000 },   /* R20510 - DACR_RETUNE_C16_1 */
	{ 20511, 0x0000 },   /* R20511 - DACR_RETUNE_C16_0 */
	{ 20512, 0x0000 },   /* R20512 - DACR_RETUNE_C17_1 */
	{ 20513, 0x0000 },   /* R20513 - DACR_RETUNE_C17_0 */
	{ 20514, 0x0000 },   /* R20514 - DACR_RETUNE_C18_1 */
	{ 20515, 0x0000 },   /* R20515 - DACR_RETUNE_C18_0 */
	{ 20516, 0x0000 },   /* R20516 - DACR_RETUNE_C19_1 */
	{ 20517, 0x0000 },   /* R20517 - DACR_RETUNE_C19_0 */
	{ 20518, 0x0000 },   /* R20518 - DACR_RETUNE_C20_1 */
	{ 20519, 0x0000 },   /* R20519 - DACR_RETUNE_C20_0 */
	{ 20520, 0x0000 },   /* R20520 - DACR_RETUNE_C21_1 */
	{ 20521, 0x0000 },   /* R20521 - DACR_RETUNE_C21_0 */
	{ 20522, 0x0000 },   /* R20522 - DACR_RETUNE_C22_1 */
	{ 20523, 0x0000 },   /* R20523 - DACR_RETUNE_C22_0 */
	{ 20524, 0x0000 },   /* R20524 - DACR_RETUNE_C23_1 */
	{ 20525, 0x0000 },   /* R20525 - DACR_RETUNE_C23_0 */
	{ 20526, 0x0000 },   /* R20526 - DACR_RETUNE_C24_1 */
	{ 20527, 0x0000 },   /* R20527 - DACR_RETUNE_C24_0 */
	{ 20528, 0x0000 },   /* R20528 - DACR_RETUNE_C25_1 */
	{ 20529, 0x0000 },   /* R20529 - DACR_RETUNE_C25_0 */
	{ 20530, 0x0000 },   /* R20530 - DACR_RETUNE_C26_1 */
	{ 20531, 0x0000 },   /* R20531 - DACR_RETUNE_C26_0 */
	{ 20532, 0x0000 },   /* R20532 - DACR_RETUNE_C27_1 */
	{ 20533, 0x0000 },   /* R20533 - DACR_RETUNE_C27_0 */
	{ 20534, 0x0000 },   /* R20534 - DACR_RETUNE_C28_1 */
	{ 20535, 0x0000 },   /* R20535 - DACR_RETUNE_C28_0 */
	{ 20536, 0x0000 },   /* R20536 - DACR_RETUNE_C29_1 */
	{ 20537, 0x0000 },   /* R20537 - DACR_RETUNE_C29_0 */
	{ 20538, 0x0000 },   /* R20538 - DACR_RETUNE_C30_1 */
	{ 20539, 0x0000 },   /* R20539 - DACR_RETUNE_C30_0 */
	{ 20540, 0x0000 },   /* R20540 - DACR_RETUNE_C31_1 */
	{ 20541, 0x0000 },   /* R20541 - DACR_RETUNE_C31_0 */
	{ 20542, 0x0000 },   /* R20542 - DACR_RETUNE_C32_1 */
	{ 20543, 0x0000 },   /* R20543 - DACR_RETUNE_C32_0 */

	{ 20992, 0x008C },   /* R20992 - VSS_XHD2_1 */
	{ 20993, 0x0200 },   /* R20993 - VSS_XHD2_0 */
	{ 20994, 0x0035 },   /* R20994 - VSS_XHD3_1 */
	{ 20995, 0x0700 },   /* R20995 - VSS_XHD3_0 */
	{ 20996, 0x003A },   /* R20996 - VSS_XHN1_1 */
	{ 20997, 0x4100 },   /* R20997 - VSS_XHN1_0 */
	{ 20998, 0x008B },   /* R20998 - VSS_XHN2_1 */
	{ 20999, 0x7D00 },   /* R20999 - VSS_XHN2_0 */
	{ 21000, 0x003A },   /* R21000 - VSS_XHN3_1 */
	{ 21001, 0x4100 },   /* R21001 - VSS_XHN3_0 */
	{ 21002, 0x008C },   /* R21002 - VSS_XLA_1 */
	{ 21003, 0xFEE8 },   /* R21003 - VSS_XLA_0 */
	{ 21004, 0x0078 },   /* R21004 - VSS_XLB_1 */
	{ 21005, 0x0000 },   /* R21005 - VSS_XLB_0 */
	{ 21006, 0x003F },   /* R21006 - VSS_XLG_1 */
	{ 21007, 0xB260 },   /* R21007 - VSS_XLG_0 */
	{ 21008, 0x002D },   /* R21008 - VSS_PG2_1 */
	{ 21009, 0x1818 },   /* R21009 - VSS_PG2_0 */
	{ 21010, 0x0020 },   /* R21010 - VSS_PG_1 */
	{ 21011, 0x0000 },   /* R21011 - VSS_PG_0 */
	{ 21012, 0x00F1 },   /* R21012 - VSS_XTD1_1 */
	{ 21013, 0x8340 },   /* R21013 - VSS_XTD1_0 */
	{ 21014, 0x00FB },   /* R21014 - VSS_XTD2_1 */
	{ 21015, 0x8300 },   /* R21015 - VSS_XTD2_0 */
	{ 21016, 0x00EE },   /* R21016 - VSS_XTD3_1 */
	{ 21017, 0xAEC0 },   /* R21017 - VSS_XTD3_0 */
	{ 21018, 0x00FB },   /* R21018 - VSS_XTD4_1 */
	{ 21019, 0xAC40 },   /* R21019 - VSS_XTD4_0 */
	{ 21020, 0x00F1 },   /* R21020 - VSS_XTD5_1 */
	{ 21021, 0x7F80 },   /* R21021 - VSS_XTD5_0 */
	{ 21022, 0x00F4 },   /* R21022 - VSS_XTD6_1 */
	{ 21023, 0x3B40 },   /* R21023 - VSS_XTD6_0 */
	{ 21024, 0x00F5 },   /* R21024 - VSS_XTD7_1 */
	{ 21025, 0xFB00 },   /* R21025 - VSS_XTD7_0 */
	{ 21026, 0x00EA },   /* R21026 - VSS_XTD8_1 */
	{ 21027, 0x10C0 },   /* R21027 - VSS_XTD8_0 */
	{ 21028, 0x00FC },   /* R21028 - VSS_XTD9_1 */
	{ 21029, 0xC580 },   /* R21029 - VSS_XTD9_0 */
	{ 21030, 0x00E2 },   /* R21030 - VSS_XTD10_1 */
	{ 21031, 0x75C0 },   /* R21031 - VSS_XTD10_0 */
	{ 21032, 0x0004 },   /* R21032 - VSS_XTD11_1 */
	{ 21033, 0xB480 },   /* R21033 - VSS_XTD11_0 */
	{ 21034, 0x00D4 },   /* R21034 - VSS_XTD12_1 */
	{ 21035, 0xF980 },   /* R21035 - VSS_XTD12_0 */
	{ 21036, 0x0004 },   /* R21036 - VSS_XTD13_1 */
	{ 21037, 0x9140 },   /* R21037 - VSS_XTD13_0 */
	{ 21038, 0x00D8 },   /* R21038 - VSS_XTD14_1 */
	{ 21039, 0xA480 },   /* R21039 - VSS_XTD14_0 */
	{ 21040, 0x0002 },   /* R21040 - VSS_XTD15_1 */
	{ 21041, 0x3DC0 },   /* R21041 - VSS_XTD15_0 */
	{ 21042, 0x00CF },   /* R21042 - VSS_XTD16_1 */
	{ 21043, 0x7A80 },   /* R21043 - VSS_XTD16_0 */
	{ 21044, 0x00DC },   /* R21044 - VSS_XTD17_1 */
	{ 21045, 0x0600 },   /* R21045 - VSS_XTD17_0 */
	{ 21046, 0x00F2 },   /* R21046 - VSS_XTD18_1 */
	{ 21047, 0xDAC0 },   /* R21047 - VSS_XTD18_0 */
	{ 21048, 0x00BA },   /* R21048 - VSS_XTD19_1 */
	{ 21049, 0xF340 },   /* R21049 - VSS_XTD19_0 */
	{ 21050, 0x000A },   /* R21050 - VSS_XTD20_1 */
	{ 21051, 0x7940 },   /* R21051 - VSS_XTD20_0 */
	{ 21052, 0x001C },   /* R21052 - VSS_XTD21_1 */
	{ 21053, 0x0680 },   /* R21053 - VSS_XTD21_0 */
	{ 21054, 0x00FD },   /* R21054 - VSS_XTD22_1 */
	{ 21055, 0x2D00 },   /* R21055 - VSS_XTD22_0 */
	{ 21056, 0x001C },   /* R21056 - VSS_XTD23_1 */
	{ 21057, 0xE840 },   /* R21057 - VSS_XTD23_0 */
	{ 21058, 0x000D },   /* R21058 - VSS_XTD24_1 */
	{ 21059, 0xDC40 },   /* R21059 - VSS_XTD24_0 */
	{ 21060, 0x00FC },   /* R21060 - VSS_XTD25_1 */
	{ 21061, 0x9D00 },   /* R21061 - VSS_XTD25_0 */
	{ 21062, 0x0009 },   /* R21062 - VSS_XTD26_1 */
	{ 21063, 0x5580 },   /* R21063 - VSS_XTD26_0 */
	{ 21064, 0x00FE },   /* R21064 - VSS_XTD27_1 */
	{ 21065, 0x7E80 },   /* R21065 - VSS_XTD27_0 */
	{ 21066, 0x000E },   /* R21066 - VSS_XTD28_1 */
	{ 21067, 0xAB40 },   /* R21067 - VSS_XTD28_0 */
	{ 21068, 0x00F9 },   /* R21068 - VSS_XTD29_1 */
	{ 21069, 0x9880 },   /* R21069 - VSS_XTD29_0 */
	{ 21070, 0x0009 },   /* R21070 - VSS_XTD30_1 */
	{ 21071, 0x87C0 },   /* R21071 - VSS_XTD30_0 */
	{ 21072, 0x00FD },   /* R21072 - VSS_XTD31_1 */
	{ 21073, 0x2C40 },   /* R21073 - VSS_XTD31_0 */
	{ 21074, 0x0009 },   /* R21074 - VSS_XTD32_1 */
	{ 21075, 0x4800 },   /* R21075 - VSS_XTD32_0 */
	{ 21076, 0x0003 },   /* R21076 - VSS_XTS1_1 */
	{ 21077, 0x5F40 },   /* R21077 - VSS_XTS1_0 */
	{ 21078, 0x0000 },   /* R21078 - VSS_XTS2_1 */
	{ 21079, 0x8700 },   /* R21079 - VSS_XTS2_0 */
	{ 21080, 0x00FA },   /* R21080 - VSS_XTS3_1 */
	{ 21081, 0xE4C0 },   /* R21081 - VSS_XTS3_0 */
	{ 21082, 0x0000 },   /* R21082 - VSS_XTS4_1 */
	{ 21083, 0x0B40 },   /* R21083 - VSS_XTS4_0 */
	{ 21084, 0x0004 },   /* R21084 - VSS_XTS5_1 */
	{ 21085, 0xE180 },   /* R21085 - VSS_XTS5_0 */
	{ 21086, 0x0001 },   /* R21086 - VSS_XTS6_1 */
	{ 21087, 0x1F40 },   /* R21087 - VSS_XTS6_0 */
	{ 21088, 0x00F8 },   /* R21088 - VSS_XTS7_1 */
	{ 21089, 0xB000 },   /* R21089 - VSS_XTS7_0 */
	{ 21090, 0x00FB },   /* R21090 - VSS_XTS8_1 */
	{ 21091, 0xCBC0 },   /* R21091 - VSS_XTS8_0 */
	{ 21092, 0x0004 },   /* R21092 - VSS_XTS9_1 */
	{ 21093, 0xF380 },   /* R21093 - VSS_XTS9_0 */
	{ 21094, 0x0007 },   /* R21094 - VSS_XTS10_1 */
	{ 21095, 0xDF40 },   /* R21095 - VSS_XTS10_0 */
	{ 21096, 0x00FF },   /* R21096 - VSS_XTS11_1 */
	{ 21097, 0x0700 },   /* R21097 - VSS_XTS11_0 */
	{ 21098, 0x00EF },   /* R21098 - VSS_XTS12_1 */
	{ 21099, 0xD700 },   /* R21099 - VSS_XTS12_0 */
	{ 21100, 0x00FB },   /* R21100 - VSS_XTS13_1 */
	{ 21101, 0xAF40 },   /* R21101 - VSS_XTS13_0 */
	{ 21102, 0x0010 },   /* R21102 - VSS_XTS14_1 */
	{ 21103, 0x8A80 },   /* R21103 - VSS_XTS14_0 */
	{ 21104, 0x0011 },   /* R21104 - VSS_XTS15_1 */
	{ 21105, 0x07C0 },   /* R21105 - VSS_XTS15_0 */
	{ 21106, 0x00E0 },   /* R21106 - VSS_XTS16_1 */
	{ 21107, 0x0800 },   /* R21107 - VSS_XTS16_0 */
	{ 21108, 0x00D2 },   /* R21108 - VSS_XTS17_1 */
	{ 21109, 0x7600 },   /* R21109 - VSS_XTS17_0 */
	{ 21110, 0x0020 },   /* R21110 - VSS_XTS18_1 */
	{ 21111, 0xCF40 },   /* R21111 - VSS_XTS18_0 */
	{ 21112, 0x0030 },   /* R21112 - VSS_XTS19_1 */
	{ 21113, 0x2340 },   /* R21113 - VSS_XTS19_0 */
	{ 21114, 0x00FD },   /* R21114 - VSS_XTS20_1 */
	{ 21115, 0x69C0 },   /* R21115 - VSS_XTS20_0 */
	{ 21116, 0x0028 },   /* R21116 - VSS_XTS21_1 */
	{ 21117, 0x3500 },   /* R21117 - VSS_XTS21_0 */
	{ 21118, 0x0006 },   /* R21118 - VSS_XTS22_1 */
	{ 21119, 0x3300 },   /* R21119 - VSS_XTS22_0 */
	{ 21120, 0x00D9 },   /* R21120 - VSS_XTS23_1 */
	{ 21121, 0xF6C0 },   /* R21121 - VSS_XTS23_0 */
	{ 21122, 0x00F3 },   /* R21122 - VSS_XTS24_1 */
	{ 21123, 0x3340 },   /* R21123 - VSS_XTS24_0 */
	{ 21124, 0x000F },   /* R21124 - VSS_XTS25_1 */
	{ 21125, 0x4200 },   /* R21125 - VSS_XTS25_0 */
	{ 21126, 0x0004 },   /* R21126 - VSS_XTS26_1 */
	{ 21127, 0x0C80 },   /* R21127 - VSS_XTS26_0 */
	{ 21128, 0x00FB },   /* R21128 - VSS_XTS27_1 */
	{ 21129, 0x3F80 },   /* R21129 - VSS_XTS27_0 */
	{ 21130, 0x00F7 },   /* R21130 - VSS_XTS28_1 */
	{ 21131, 0x57C0 },   /* R21131 - VSS_XTS28_0 */
	{ 21132, 0x0003 },   /* R21132 - VSS_XTS29_1 */
	{ 21133, 0x5400 },   /* R21133 - VSS_XTS29_0 */
	{ 21134, 0x0000 },   /* R21134 - VSS_XTS30_1 */
	{ 21135, 0xC6C0 },   /* R21135 - VSS_XTS30_0 */
	{ 21136, 0x0003 },   /* R21136 - VSS_XTS31_1 */
	{ 21137, 0x12C0 },   /* R21137 - VSS_XTS31_0 */
	{ 21138, 0x00FD },   /* R21138 - VSS_XTS32_1 */
	{ 21139, 0x8580 },   /* R21139 - VSS_XTS32_0 */
};

static bool wm8962_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8962_CLOCKING1:
	case WM8962_SOFTWARE_RESET:
	case WM8962_THERMAL_SHUTDOWN_STATUS:
	case WM8962_ADDITIONAL_CONTROL_4:
	case WM8962_DC_SERVO_6:
	case WM8962_INTERRUPT_STATUS_1:
	case WM8962_INTERRUPT_STATUS_2:
	case WM8962_DSP2_EXECCONTROL:
		return true;
	default:
		return false;
	}
}

static bool wm8962_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8962_LEFT_INPUT_VOLUME:
	case WM8962_RIGHT_INPUT_VOLUME:
	case WM8962_HPOUTL_VOLUME:
	case WM8962_HPOUTR_VOLUME:
	case WM8962_CLOCKING1:
	case WM8962_ADC_DAC_CONTROL_1:
	case WM8962_ADC_DAC_CONTROL_2:
	case WM8962_AUDIO_INTERFACE_0:
	case WM8962_CLOCKING2:
	case WM8962_AUDIO_INTERFACE_1:
	case WM8962_LEFT_DAC_VOLUME:
	case WM8962_RIGHT_DAC_VOLUME:
	case WM8962_AUDIO_INTERFACE_2:
	case WM8962_SOFTWARE_RESET:
	case WM8962_ALC1:
	case WM8962_ALC2:
	case WM8962_ALC3:
	case WM8962_NOISE_GATE:
	case WM8962_LEFT_ADC_VOLUME:
	case WM8962_RIGHT_ADC_VOLUME:
	case WM8962_ADDITIONAL_CONTROL_1:
	case WM8962_ADDITIONAL_CONTROL_2:
	case WM8962_PWR_MGMT_1:
	case WM8962_PWR_MGMT_2:
	case WM8962_ADDITIONAL_CONTROL_3:
	case WM8962_ANTI_POP:
	case WM8962_CLOCKING_3:
	case WM8962_INPUT_MIXER_CONTROL_1:
	case WM8962_LEFT_INPUT_MIXER_VOLUME:
	case WM8962_RIGHT_INPUT_MIXER_VOLUME:
	case WM8962_INPUT_MIXER_CONTROL_2:
	case WM8962_INPUT_BIAS_CONTROL:
	case WM8962_LEFT_INPUT_PGA_CONTROL:
	case WM8962_RIGHT_INPUT_PGA_CONTROL:
	case WM8962_SPKOUTL_VOLUME:
	case WM8962_SPKOUTR_VOLUME:
	case WM8962_THERMAL_SHUTDOWN_STATUS:
	case WM8962_ADDITIONAL_CONTROL_4:
	case WM8962_CLASS_D_CONTROL_1:
	case WM8962_CLASS_D_CONTROL_2:
	case WM8962_CLOCKING_4:
	case WM8962_DAC_DSP_MIXING_1:
	case WM8962_DAC_DSP_MIXING_2:
	case WM8962_DC_SERVO_0:
	case WM8962_DC_SERVO_1:
	case WM8962_DC_SERVO_4:
	case WM8962_DC_SERVO_6:
	case WM8962_ANALOGUE_PGA_BIAS:
	case WM8962_ANALOGUE_HP_0:
	case WM8962_ANALOGUE_HP_2:
	case WM8962_CHARGE_PUMP_1:
	case WM8962_CHARGE_PUMP_B:
	case WM8962_WRITE_SEQUENCER_CONTROL_1:
	case WM8962_WRITE_SEQUENCER_CONTROL_2:
	case WM8962_WRITE_SEQUENCER_CONTROL_3:
	case WM8962_CONTROL_INTERFACE:
	case WM8962_MIXER_ENABLES:
	case WM8962_HEADPHONE_MIXER_1:
	case WM8962_HEADPHONE_MIXER_2:
	case WM8962_HEADPHONE_MIXER_3:
	case WM8962_HEADPHONE_MIXER_4:
	case WM8962_SPEAKER_MIXER_1:
	case WM8962_SPEAKER_MIXER_2:
	case WM8962_SPEAKER_MIXER_3:
	case WM8962_SPEAKER_MIXER_4:
	case WM8962_SPEAKER_MIXER_5:
	case WM8962_BEEP_GENERATOR_1:
	case WM8962_OSCILLATOR_TRIM_3:
	case WM8962_OSCILLATOR_TRIM_4:
	case WM8962_OSCILLATOR_TRIM_7:
	case WM8962_ANALOGUE_CLOCKING1:
	case WM8962_ANALOGUE_CLOCKING2:
	case WM8962_ANALOGUE_CLOCKING3:
	case WM8962_PLL_SOFTWARE_RESET:
	case WM8962_PLL2:
	case WM8962_PLL_4:
	case WM8962_PLL_9:
	case WM8962_PLL_10:
	case WM8962_PLL_11:
	case WM8962_PLL_12:
	case WM8962_PLL_13:
	case WM8962_PLL_14:
	case WM8962_PLL_15:
	case WM8962_PLL_16:
	case WM8962_FLL_CONTROL_1:
	case WM8962_FLL_CONTROL_2:
	case WM8962_FLL_CONTROL_3:
	case WM8962_FLL_CONTROL_5:
	case WM8962_FLL_CONTROL_6:
	case WM8962_FLL_CONTROL_7:
	case WM8962_FLL_CONTROL_8:
	case WM8962_GENERAL_TEST_1:
	case WM8962_DF1:
	case WM8962_DF2:
	case WM8962_DF3:
	case WM8962_DF4:
	case WM8962_DF5:
	case WM8962_DF6:
	case WM8962_DF7:
	case WM8962_LHPF1:
	case WM8962_LHPF2:
	case WM8962_THREED1:
	case WM8962_THREED2:
	case WM8962_THREED3:
	case WM8962_THREED4:
	case WM8962_DRC_1:
	case WM8962_DRC_2:
	case WM8962_DRC_3:
	case WM8962_DRC_4:
	case WM8962_DRC_5:
	case WM8962_TLOOPBACK:
	case WM8962_EQ1:
	case WM8962_EQ2:
	case WM8962_EQ3:
	case WM8962_EQ4:
	case WM8962_EQ5:
	case WM8962_EQ6:
	case WM8962_EQ7:
	case WM8962_EQ8:
	case WM8962_EQ9:
	case WM8962_EQ10:
	case WM8962_EQ11:
	case WM8962_EQ12:
	case WM8962_EQ13:
	case WM8962_EQ14:
	case WM8962_EQ15:
	case WM8962_EQ16:
	case WM8962_EQ17:
	case WM8962_EQ18:
	case WM8962_EQ19:
	case WM8962_EQ20:
	case WM8962_EQ21:
	case WM8962_EQ22:
	case WM8962_EQ23:
	case WM8962_EQ24:
	case WM8962_EQ25:
	case WM8962_EQ26:
	case WM8962_EQ27:
	case WM8962_EQ28:
	case WM8962_EQ29:
	case WM8962_EQ30:
	case WM8962_EQ31:
	case WM8962_EQ32:
	case WM8962_EQ33:
	case WM8962_EQ34:
	case WM8962_EQ35:
	case WM8962_EQ36:
	case WM8962_EQ37:
	case WM8962_EQ38:
	case WM8962_EQ39:
	case WM8962_EQ40:
	case WM8962_EQ41:
	case WM8962_GPIO_2:
	case WM8962_GPIO_3:
	case WM8962_GPIO_5:
	case WM8962_GPIO_6:
	case WM8962_INTERRUPT_STATUS_1:
	case WM8962_INTERRUPT_STATUS_2:
	case WM8962_INTERRUPT_STATUS_1_MASK:
	case WM8962_INTERRUPT_STATUS_2_MASK:
	case WM8962_INTERRUPT_CONTROL:
	case WM8962_IRQ_DEBOUNCE:
	case WM8962_MICINT_SOURCE_POL:
	case WM8962_DSP2_POWER_MANAGEMENT:
	case WM8962_DSP2_EXECCONTROL:
	case WM8962_DSP2_INSTRUCTION_RAM_0:
	case WM8962_DSP2_ADDRESS_RAM_2:
	case WM8962_DSP2_ADDRESS_RAM_1:
	case WM8962_DSP2_ADDRESS_RAM_0:
	case WM8962_DSP2_DATA1_RAM_1:
	case WM8962_DSP2_DATA1_RAM_0:
	case WM8962_DSP2_DATA2_RAM_1:
	case WM8962_DSP2_DATA2_RAM_0:
	case WM8962_DSP2_DATA3_RAM_1:
	case WM8962_DSP2_DATA3_RAM_0:
	case WM8962_DSP2_COEFF_RAM_0:
	case WM8962_RETUNEADC_SHARED_COEFF_1:
	case WM8962_RETUNEADC_SHARED_COEFF_0:
	case WM8962_RETUNEDAC_SHARED_COEFF_1:
	case WM8962_RETUNEDAC_SHARED_COEFF_0:
	case WM8962_SOUNDSTAGE_ENABLES_1:
	case WM8962_SOUNDSTAGE_ENABLES_0:
	case WM8962_HDBASS_AI_1:
	case WM8962_HDBASS_AI_0:
	case WM8962_HDBASS_AR_1:
	case WM8962_HDBASS_AR_0:
	case WM8962_HDBASS_B_1:
	case WM8962_HDBASS_B_0:
	case WM8962_HDBASS_K_1:
	case WM8962_HDBASS_K_0:
	case WM8962_HDBASS_N1_1:
	case WM8962_HDBASS_N1_0:
	case WM8962_HDBASS_N2_1:
	case WM8962_HDBASS_N2_0:
	case WM8962_HDBASS_N3_1:
	case WM8962_HDBASS_N3_0:
	case WM8962_HDBASS_N4_1:
	case WM8962_HDBASS_N4_0:
	case WM8962_HDBASS_N5_1:
	case WM8962_HDBASS_N5_0:
	case WM8962_HDBASS_X1_1:
	case WM8962_HDBASS_X1_0:
	case WM8962_HDBASS_X2_1:
	case WM8962_HDBASS_X2_0:
	case WM8962_HDBASS_X3_1:
	case WM8962_HDBASS_X3_0:
	case WM8962_HDBASS_ATK_1:
	case WM8962_HDBASS_ATK_0:
	case WM8962_HDBASS_DCY_1:
	case WM8962_HDBASS_DCY_0:
	case WM8962_HDBASS_PG_1:
	case WM8962_HDBASS_PG_0:
	case WM8962_HPF_C_1:
	case WM8962_HPF_C_0:
	case WM8962_ADCL_RETUNE_C1_1:
	case WM8962_ADCL_RETUNE_C1_0:
	case WM8962_ADCL_RETUNE_C2_1:
	case WM8962_ADCL_RETUNE_C2_0:
	case WM8962_ADCL_RETUNE_C3_1:
	case WM8962_ADCL_RETUNE_C3_0:
	case WM8962_ADCL_RETUNE_C4_1:
	case WM8962_ADCL_RETUNE_C4_0:
	case WM8962_ADCL_RETUNE_C5_1:
	case WM8962_ADCL_RETUNE_C5_0:
	case WM8962_ADCL_RETUNE_C6_1:
	case WM8962_ADCL_RETUNE_C6_0:
	case WM8962_ADCL_RETUNE_C7_1:
	case WM8962_ADCL_RETUNE_C7_0:
	case WM8962_ADCL_RETUNE_C8_1:
	case WM8962_ADCL_RETUNE_C8_0:
	case WM8962_ADCL_RETUNE_C9_1:
	case WM8962_ADCL_RETUNE_C9_0:
	case WM8962_ADCL_RETUNE_C10_1:
	case WM8962_ADCL_RETUNE_C10_0:
	case WM8962_ADCL_RETUNE_C11_1:
	case WM8962_ADCL_RETUNE_C11_0:
	case WM8962_ADCL_RETUNE_C12_1:
	case WM8962_ADCL_RETUNE_C12_0:
	case WM8962_ADCL_RETUNE_C13_1:
	case WM8962_ADCL_RETUNE_C13_0:
	case WM8962_ADCL_RETUNE_C14_1:
	case WM8962_ADCL_RETUNE_C14_0:
	case WM8962_ADCL_RETUNE_C15_1:
	case WM8962_ADCL_RETUNE_C15_0:
	case WM8962_ADCL_RETUNE_C16_1:
	case WM8962_ADCL_RETUNE_C16_0:
	case WM8962_ADCL_RETUNE_C17_1:
	case WM8962_ADCL_RETUNE_C17_0:
	case WM8962_ADCL_RETUNE_C18_1:
	case WM8962_ADCL_RETUNE_C18_0:
	case WM8962_ADCL_RETUNE_C19_1:
	case WM8962_ADCL_RETUNE_C19_0:
	case WM8962_ADCL_RETUNE_C20_1:
	case WM8962_ADCL_RETUNE_C20_0:
	case WM8962_ADCL_RETUNE_C21_1:
	case WM8962_ADCL_RETUNE_C21_0:
	case WM8962_ADCL_RETUNE_C22_1:
	case WM8962_ADCL_RETUNE_C22_0:
	case WM8962_ADCL_RETUNE_C23_1:
	case WM8962_ADCL_RETUNE_C23_0:
	case WM8962_ADCL_RETUNE_C24_1:
	case WM8962_ADCL_RETUNE_C24_0:
	case WM8962_ADCL_RETUNE_C25_1:
	case WM8962_ADCL_RETUNE_C25_0:
	case WM8962_ADCL_RETUNE_C26_1:
	case WM8962_ADCL_RETUNE_C26_0:
	case WM8962_ADCL_RETUNE_C27_1:
	case WM8962_ADCL_RETUNE_C27_0:
	case WM8962_ADCL_RETUNE_C28_1:
	case WM8962_ADCL_RETUNE_C28_0:
	case WM8962_ADCL_RETUNE_C29_1:
	case WM8962_ADCL_RETUNE_C29_0:
	case WM8962_ADCL_RETUNE_C30_1:
	case WM8962_ADCL_RETUNE_C30_0:
	case WM8962_ADCL_RETUNE_C31_1:
	case WM8962_ADCL_RETUNE_C31_0:
	case WM8962_ADCL_RETUNE_C32_1:
	case WM8962_ADCL_RETUNE_C32_0:
	case WM8962_RETUNEADC_PG2_1:
	case WM8962_RETUNEADC_PG2_0:
	case WM8962_RETUNEADC_PG_1:
	case WM8962_RETUNEADC_PG_0:
	case WM8962_ADCR_RETUNE_C1_1:
	case WM8962_ADCR_RETUNE_C1_0:
	case WM8962_ADCR_RETUNE_C2_1:
	case WM8962_ADCR_RETUNE_C2_0:
	case WM8962_ADCR_RETUNE_C3_1:
	case WM8962_ADCR_RETUNE_C3_0:
	case WM8962_ADCR_RETUNE_C4_1:
	case WM8962_ADCR_RETUNE_C4_0:
	case WM8962_ADCR_RETUNE_C5_1:
	case WM8962_ADCR_RETUNE_C5_0:
	case WM8962_ADCR_RETUNE_C6_1:
	case WM8962_ADCR_RETUNE_C6_0:
	case WM8962_ADCR_RETUNE_C7_1:
	case WM8962_ADCR_RETUNE_C7_0:
	case WM8962_ADCR_RETUNE_C8_1:
	case WM8962_ADCR_RETUNE_C8_0:
	case WM8962_ADCR_RETUNE_C9_1:
	case WM8962_ADCR_RETUNE_C9_0:
	case WM8962_ADCR_RETUNE_C10_1:
	case WM8962_ADCR_RETUNE_C10_0:
	case WM8962_ADCR_RETUNE_C11_1:
	case WM8962_ADCR_RETUNE_C11_0:
	case WM8962_ADCR_RETUNE_C12_1:
	case WM8962_ADCR_RETUNE_C12_0:
	case WM8962_ADCR_RETUNE_C13_1:
	case WM8962_ADCR_RETUNE_C13_0:
	case WM8962_ADCR_RETUNE_C14_1:
	case WM8962_ADCR_RETUNE_C14_0:
	case WM8962_ADCR_RETUNE_C15_1:
	case WM8962_ADCR_RETUNE_C15_0:
	case WM8962_ADCR_RETUNE_C16_1:
	case WM8962_ADCR_RETUNE_C16_0:
	case WM8962_ADCR_RETUNE_C17_1:
	case WM8962_ADCR_RETUNE_C17_0:
	case WM8962_ADCR_RETUNE_C18_1:
	case WM8962_ADCR_RETUNE_C18_0:
	case WM8962_ADCR_RETUNE_C19_1:
	case WM8962_ADCR_RETUNE_C19_0:
	case WM8962_ADCR_RETUNE_C20_1:
	case WM8962_ADCR_RETUNE_C20_0:
	case WM8962_ADCR_RETUNE_C21_1:
	case WM8962_ADCR_RETUNE_C21_0:
	case WM8962_ADCR_RETUNE_C22_1:
	case WM8962_ADCR_RETUNE_C22_0:
	case WM8962_ADCR_RETUNE_C23_1:
	case WM8962_ADCR_RETUNE_C23_0:
	case WM8962_ADCR_RETUNE_C24_1:
	case WM8962_ADCR_RETUNE_C24_0:
	case WM8962_ADCR_RETUNE_C25_1:
	case WM8962_ADCR_RETUNE_C25_0:
	case WM8962_ADCR_RETUNE_C26_1:
	case WM8962_ADCR_RETUNE_C26_0:
	case WM8962_ADCR_RETUNE_C27_1:
	case WM8962_ADCR_RETUNE_C27_0:
	case WM8962_ADCR_RETUNE_C28_1:
	case WM8962_ADCR_RETUNE_C28_0:
	case WM8962_ADCR_RETUNE_C29_1:
	case WM8962_ADCR_RETUNE_C29_0:
	case WM8962_ADCR_RETUNE_C30_1:
	case WM8962_ADCR_RETUNE_C30_0:
	case WM8962_ADCR_RETUNE_C31_1:
	case WM8962_ADCR_RETUNE_C31_0:
	case WM8962_ADCR_RETUNE_C32_1:
	case WM8962_ADCR_RETUNE_C32_0:
	case WM8962_DACL_RETUNE_C1_1:
	case WM8962_DACL_RETUNE_C1_0:
	case WM8962_DACL_RETUNE_C2_1:
	case WM8962_DACL_RETUNE_C2_0:
	case WM8962_DACL_RETUNE_C3_1:
	case WM8962_DACL_RETUNE_C3_0:
	case WM8962_DACL_RETUNE_C4_1:
	case WM8962_DACL_RETUNE_C4_0:
	case WM8962_DACL_RETUNE_C5_1:
	case WM8962_DACL_RETUNE_C5_0:
	case WM8962_DACL_RETUNE_C6_1:
	case WM8962_DACL_RETUNE_C6_0:
	case WM8962_DACL_RETUNE_C7_1:
	case WM8962_DACL_RETUNE_C7_0:
	case WM8962_DACL_RETUNE_C8_1:
	case WM8962_DACL_RETUNE_C8_0:
	case WM8962_DACL_RETUNE_C9_1:
	case WM8962_DACL_RETUNE_C9_0:
	case WM8962_DACL_RETUNE_C10_1:
	case WM8962_DACL_RETUNE_C10_0:
	case WM8962_DACL_RETUNE_C11_1:
	case WM8962_DACL_RETUNE_C11_0:
	case WM8962_DACL_RETUNE_C12_1:
	case WM8962_DACL_RETUNE_C12_0:
	case WM8962_DACL_RETUNE_C13_1:
	case WM8962_DACL_RETUNE_C13_0:
	case WM8962_DACL_RETUNE_C14_1:
	case WM8962_DACL_RETUNE_C14_0:
	case WM8962_DACL_RETUNE_C15_1:
	case WM8962_DACL_RETUNE_C15_0:
	case WM8962_DACL_RETUNE_C16_1:
	case WM8962_DACL_RETUNE_C16_0:
	case WM8962_DACL_RETUNE_C17_1:
	case WM8962_DACL_RETUNE_C17_0:
	case WM8962_DACL_RETUNE_C18_1:
	case WM8962_DACL_RETUNE_C18_0:
	case WM8962_DACL_RETUNE_C19_1:
	case WM8962_DACL_RETUNE_C19_0:
	case WM8962_DACL_RETUNE_C20_1:
	case WM8962_DACL_RETUNE_C20_0:
	case WM8962_DACL_RETUNE_C21_1:
	case WM8962_DACL_RETUNE_C21_0:
	case WM8962_DACL_RETUNE_C22_1:
	case WM8962_DACL_RETUNE_C22_0:
	case WM8962_DACL_RETUNE_C23_1:
	case WM8962_DACL_RETUNE_C23_0:
	case WM8962_DACL_RETUNE_C24_1:
	case WM8962_DACL_RETUNE_C24_0:
	case WM8962_DACL_RETUNE_C25_1:
	case WM8962_DACL_RETUNE_C25_0:
	case WM8962_DACL_RETUNE_C26_1:
	case WM8962_DACL_RETUNE_C26_0:
	case WM8962_DACL_RETUNE_C27_1:
	case WM8962_DACL_RETUNE_C27_0:
	case WM8962_DACL_RETUNE_C28_1:
	case WM8962_DACL_RETUNE_C28_0:
	case WM8962_DACL_RETUNE_C29_1:
	case WM8962_DACL_RETUNE_C29_0:
	case WM8962_DACL_RETUNE_C30_1:
	case WM8962_DACL_RETUNE_C30_0:
	case WM8962_DACL_RETUNE_C31_1:
	case WM8962_DACL_RETUNE_C31_0:
	case WM8962_DACL_RETUNE_C32_1:
	case WM8962_DACL_RETUNE_C32_0:
	case WM8962_RETUNEDAC_PG2_1:
	case WM8962_RETUNEDAC_PG2_0:
	case WM8962_RETUNEDAC_PG_1:
	case WM8962_RETUNEDAC_PG_0:
	case WM8962_DACR_RETUNE_C1_1:
	case WM8962_DACR_RETUNE_C1_0:
	case WM8962_DACR_RETUNE_C2_1:
	case WM8962_DACR_RETUNE_C2_0:
	case WM8962_DACR_RETUNE_C3_1:
	case WM8962_DACR_RETUNE_C3_0:
	case WM8962_DACR_RETUNE_C4_1:
	case WM8962_DACR_RETUNE_C4_0:
	case WM8962_DACR_RETUNE_C5_1:
	case WM8962_DACR_RETUNE_C5_0:
	case WM8962_DACR_RETUNE_C6_1:
	case WM8962_DACR_RETUNE_C6_0:
	case WM8962_DACR_RETUNE_C7_1:
	case WM8962_DACR_RETUNE_C7_0:
	case WM8962_DACR_RETUNE_C8_1:
	case WM8962_DACR_RETUNE_C8_0:
	case WM8962_DACR_RETUNE_C9_1:
	case WM8962_DACR_RETUNE_C9_0:
	case WM8962_DACR_RETUNE_C10_1:
	case WM8962_DACR_RETUNE_C10_0:
	case WM8962_DACR_RETUNE_C11_1:
	case WM8962_DACR_RETUNE_C11_0:
	case WM8962_DACR_RETUNE_C12_1:
	case WM8962_DACR_RETUNE_C12_0:
	case WM8962_DACR_RETUNE_C13_1:
	case WM8962_DACR_RETUNE_C13_0:
	case WM8962_DACR_RETUNE_C14_1:
	case WM8962_DACR_RETUNE_C14_0:
	case WM8962_DACR_RETUNE_C15_1:
	case WM8962_DACR_RETUNE_C15_0:
	case WM8962_DACR_RETUNE_C16_1:
	case WM8962_DACR_RETUNE_C16_0:
	case WM8962_DACR_RETUNE_C17_1:
	case WM8962_DACR_RETUNE_C17_0:
	case WM8962_DACR_RETUNE_C18_1:
	case WM8962_DACR_RETUNE_C18_0:
	case WM8962_DACR_RETUNE_C19_1:
	case WM8962_DACR_RETUNE_C19_0:
	case WM8962_DACR_RETUNE_C20_1:
	case WM8962_DACR_RETUNE_C20_0:
	case WM8962_DACR_RETUNE_C21_1:
	case WM8962_DACR_RETUNE_C21_0:
	case WM8962_DACR_RETUNE_C22_1:
	case WM8962_DACR_RETUNE_C22_0:
	case WM8962_DACR_RETUNE_C23_1:
	case WM8962_DACR_RETUNE_C23_0:
	case WM8962_DACR_RETUNE_C24_1:
	case WM8962_DACR_RETUNE_C24_0:
	case WM8962_DACR_RETUNE_C25_1:
	case WM8962_DACR_RETUNE_C25_0:
	case WM8962_DACR_RETUNE_C26_1:
	case WM8962_DACR_RETUNE_C26_0:
	case WM8962_DACR_RETUNE_C27_1:
	case WM8962_DACR_RETUNE_C27_0:
	case WM8962_DACR_RETUNE_C28_1:
	case WM8962_DACR_RETUNE_C28_0:
	case WM8962_DACR_RETUNE_C29_1:
	case WM8962_DACR_RETUNE_C29_0:
	case WM8962_DACR_RETUNE_C30_1:
	case WM8962_DACR_RETUNE_C30_0:
	case WM8962_DACR_RETUNE_C31_1:
	case WM8962_DACR_RETUNE_C31_0:
	case WM8962_DACR_RETUNE_C32_1:
	case WM8962_DACR_RETUNE_C32_0:
	case WM8962_VSS_XHD2_1:
	case WM8962_VSS_XHD2_0:
	case WM8962_VSS_XHD3_1:
	case WM8962_VSS_XHD3_0:
	case WM8962_VSS_XHN1_1:
	case WM8962_VSS_XHN1_0:
	case WM8962_VSS_XHN2_1:
	case WM8962_VSS_XHN2_0:
	case WM8962_VSS_XHN3_1:
	case WM8962_VSS_XHN3_0:
	case WM8962_VSS_XLA_1:
	case WM8962_VSS_XLA_0:
	case WM8962_VSS_XLB_1:
	case WM8962_VSS_XLB_0:
	case WM8962_VSS_XLG_1:
	case WM8962_VSS_XLG_0:
	case WM8962_VSS_PG2_1:
	case WM8962_VSS_PG2_0:
	case WM8962_VSS_PG_1:
	case WM8962_VSS_PG_0:
	case WM8962_VSS_XTD1_1:
	case WM8962_VSS_XTD1_0:
	case WM8962_VSS_XTD2_1:
	case WM8962_VSS_XTD2_0:
	case WM8962_VSS_XTD3_1:
	case WM8962_VSS_XTD3_0:
	case WM8962_VSS_XTD4_1:
	case WM8962_VSS_XTD4_0:
	case WM8962_VSS_XTD5_1:
	case WM8962_VSS_XTD5_0:
	case WM8962_VSS_XTD6_1:
	case WM8962_VSS_XTD6_0:
	case WM8962_VSS_XTD7_1:
	case WM8962_VSS_XTD7_0:
	case WM8962_VSS_XTD8_1:
	case WM8962_VSS_XTD8_0:
	case WM8962_VSS_XTD9_1:
	case WM8962_VSS_XTD9_0:
	case WM8962_VSS_XTD10_1:
	case WM8962_VSS_XTD10_0:
	case WM8962_VSS_XTD11_1:
	case WM8962_VSS_XTD11_0:
	case WM8962_VSS_XTD12_1:
	case WM8962_VSS_XTD12_0:
	case WM8962_VSS_XTD13_1:
	case WM8962_VSS_XTD13_0:
	case WM8962_VSS_XTD14_1:
	case WM8962_VSS_XTD14_0:
	case WM8962_VSS_XTD15_1:
	case WM8962_VSS_XTD15_0:
	case WM8962_VSS_XTD16_1:
	case WM8962_VSS_XTD16_0:
	case WM8962_VSS_XTD17_1:
	case WM8962_VSS_XTD17_0:
	case WM8962_VSS_XTD18_1:
	case WM8962_VSS_XTD18_0:
	case WM8962_VSS_XTD19_1:
	case WM8962_VSS_XTD19_0:
	case WM8962_VSS_XTD20_1:
	case WM8962_VSS_XTD20_0:
	case WM8962_VSS_XTD21_1:
	case WM8962_VSS_XTD21_0:
	case WM8962_VSS_XTD22_1:
	case WM8962_VSS_XTD22_0:
	case WM8962_VSS_XTD23_1:
	case WM8962_VSS_XTD23_0:
	case WM8962_VSS_XTD24_1:
	case WM8962_VSS_XTD24_0:
	case WM8962_VSS_XTD25_1:
	case WM8962_VSS_XTD25_0:
	case WM8962_VSS_XTD26_1:
	case WM8962_VSS_XTD26_0:
	case WM8962_VSS_XTD27_1:
	case WM8962_VSS_XTD27_0:
	case WM8962_VSS_XTD28_1:
	case WM8962_VSS_XTD28_0:
	case WM8962_VSS_XTD29_1:
	case WM8962_VSS_XTD29_0:
	case WM8962_VSS_XTD30_1:
	case WM8962_VSS_XTD30_0:
	case WM8962_VSS_XTD31_1:
	case WM8962_VSS_XTD31_0:
	case WM8962_VSS_XTD32_1:
	case WM8962_VSS_XTD32_0:
	case WM8962_VSS_XTS1_1:
	case WM8962_VSS_XTS1_0:
	case WM8962_VSS_XTS2_1:
	case WM8962_VSS_XTS2_0:
	case WM8962_VSS_XTS3_1:
	case WM8962_VSS_XTS3_0:
	case WM8962_VSS_XTS4_1:
	case WM8962_VSS_XTS4_0:
	case WM8962_VSS_XTS5_1:
	case WM8962_VSS_XTS5_0:
	case WM8962_VSS_XTS6_1:
	case WM8962_VSS_XTS6_0:
	case WM8962_VSS_XTS7_1:
	case WM8962_VSS_XTS7_0:
	case WM8962_VSS_XTS8_1:
	case WM8962_VSS_XTS8_0:
	case WM8962_VSS_XTS9_1:
	case WM8962_VSS_XTS9_0:
	case WM8962_VSS_XTS10_1:
	case WM8962_VSS_XTS10_0:
	case WM8962_VSS_XTS11_1:
	case WM8962_VSS_XTS11_0:
	case WM8962_VSS_XTS12_1:
	case WM8962_VSS_XTS12_0:
	case WM8962_VSS_XTS13_1:
	case WM8962_VSS_XTS13_0:
	case WM8962_VSS_XTS14_1:
	case WM8962_VSS_XTS14_0:
	case WM8962_VSS_XTS15_1:
	case WM8962_VSS_XTS15_0:
	case WM8962_VSS_XTS16_1:
	case WM8962_VSS_XTS16_0:
	case WM8962_VSS_XTS17_1:
	case WM8962_VSS_XTS17_0:
	case WM8962_VSS_XTS18_1:
	case WM8962_VSS_XTS18_0:
	case WM8962_VSS_XTS19_1:
	case WM8962_VSS_XTS19_0:
	case WM8962_VSS_XTS20_1:
	case WM8962_VSS_XTS20_0:
	case WM8962_VSS_XTS21_1:
	case WM8962_VSS_XTS21_0:
	case WM8962_VSS_XTS22_1:
	case WM8962_VSS_XTS22_0:
	case WM8962_VSS_XTS23_1:
	case WM8962_VSS_XTS23_0:
	case WM8962_VSS_XTS24_1:
	case WM8962_VSS_XTS24_0:
	case WM8962_VSS_XTS25_1:
	case WM8962_VSS_XTS25_0:
	case WM8962_VSS_XTS26_1:
	case WM8962_VSS_XTS26_0:
	case WM8962_VSS_XTS27_1:
	case WM8962_VSS_XTS27_0:
	case WM8962_VSS_XTS28_1:
	case WM8962_VSS_XTS28_0:
	case WM8962_VSS_XTS29_1:
	case WM8962_VSS_XTS29_0:
	case WM8962_VSS_XTS30_1:
	case WM8962_VSS_XTS30_0:
	case WM8962_VSS_XTS31_1:
	case WM8962_VSS_XTS31_0:
	case WM8962_VSS_XTS32_1:
	case WM8962_VSS_XTS32_0:
		return true;
	default:
		return false;
	}
}

static int wm8962_reset(struct wm8962_priv *wm8962)
{
	int ret;

	ret = regmap_write(wm8962->regmap, WM8962_SOFTWARE_RESET, 0x6243);
	if (ret != 0)
		return ret;

	return regmap_write(wm8962->regmap, WM8962_PLL_SOFTWARE_RESET, 0);
}

static const DECLARE_TLV_DB_SCALE(inpga_tlv, -2325, 75, 0);
static const DECLARE_TLV_DB_SCALE(mixin_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_RANGE(mixinpga_tlv,
	0, 1, TLV_DB_SCALE_ITEM(0, 600, 0),
	2, 2, TLV_DB_SCALE_ITEM(1300, 1300, 0),
	3, 4, TLV_DB_SCALE_ITEM(1800, 200, 0),
	5, 5, TLV_DB_SCALE_ITEM(2400, 0, 0),
	6, 7, TLV_DB_SCALE_ITEM(2700, 300, 0)
);
static const DECLARE_TLV_DB_SCALE(beep_tlv, -9600, 600, 1);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(st_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(inmix_tlv, -600, 600, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(hp_tlv, -700, 100, 0);
static const DECLARE_TLV_DB_RANGE(classd_tlv,
	0, 6, TLV_DB_SCALE_ITEM(0, 150, 0),
	7, 7, TLV_DB_SCALE_ITEM(1200, 0, 0)
);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);

static int wm8962_dsp2_write_config(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	return regcache_sync_region(wm8962->regmap,
				    WM8962_HDBASS_AI_1, WM8962_MAX_REGISTER);
}

static int wm8962_dsp2_set_enable(struct snd_soc_component *component, u16 val)
{
	u16 adcl = snd_soc_component_read(component, WM8962_LEFT_ADC_VOLUME);
	u16 adcr = snd_soc_component_read(component, WM8962_RIGHT_ADC_VOLUME);
	u16 dac = snd_soc_component_read(component, WM8962_ADC_DAC_CONTROL_1);

	/* Mute the ADCs and DACs */
	snd_soc_component_write(component, WM8962_LEFT_ADC_VOLUME, 0);
	snd_soc_component_write(component, WM8962_RIGHT_ADC_VOLUME, WM8962_ADC_VU);
	snd_soc_component_update_bits(component, WM8962_ADC_DAC_CONTROL_1,
			    WM8962_DAC_MUTE, WM8962_DAC_MUTE);

	snd_soc_component_write(component, WM8962_SOUNDSTAGE_ENABLES_0, val);

	/* Restore the ADCs and DACs */
	snd_soc_component_write(component, WM8962_LEFT_ADC_VOLUME, adcl);
	snd_soc_component_write(component, WM8962_RIGHT_ADC_VOLUME, adcr);
	snd_soc_component_update_bits(component, WM8962_ADC_DAC_CONTROL_1,
			    WM8962_DAC_MUTE, dac);

	return 0;
}

static int wm8962_dsp2_start(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	wm8962_dsp2_write_config(component);

	snd_soc_component_write(component, WM8962_DSP2_EXECCONTROL, WM8962_DSP2_RUNR);

	wm8962_dsp2_set_enable(component, wm8962->dsp2_ena);

	return 0;
}

static int wm8962_dsp2_stop(struct snd_soc_component *component)
{
	wm8962_dsp2_set_enable(component, 0);

	snd_soc_component_write(component, WM8962_DSP2_EXECCONTROL, WM8962_DSP2_STOP);

	return 0;
}

#define WM8962_DSP2_ENABLE(xname, xshift) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = wm8962_dsp2_ena_info, \
	.get = wm8962_dsp2_ena_get, .put = wm8962_dsp2_ena_put, \
	.private_value = xshift }

static int wm8962_dsp2_ena_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int wm8962_dsp2_ena_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	int shift = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !!(wm8962->dsp2_ena & 1 << shift);

	return 0;
}

static int wm8962_dsp2_ena_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	int shift = kcontrol->private_value;
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	int old = wm8962->dsp2_ena;
	int ret = 0;
	int dsp2_running = snd_soc_component_read(component, WM8962_DSP2_POWER_MANAGEMENT) &
		WM8962_DSP2_ENA;

	mutex_lock(&wm8962->dsp2_ena_lock);

	if (ucontrol->value.integer.value[0])
		wm8962->dsp2_ena |= 1 << shift;
	else
		wm8962->dsp2_ena &= ~(1 << shift);

	if (wm8962->dsp2_ena == old)
		goto out;

	ret = 1;

	if (dsp2_running) {
		if (wm8962->dsp2_ena)
			wm8962_dsp2_set_enable(component, wm8962->dsp2_ena);
		else
			wm8962_dsp2_stop(component);
	}

out:
	mutex_unlock(&wm8962->dsp2_ena_lock);

	return ret;
}

/* The VU bits for the headphones are in a different register to the mute
 * bits and only take effect on the PGA if it is actually powered.
 */
static int wm8962_put_hp_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int ret;

	/* Apply the update (if any) */
        ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret == 0)
		return 0;

	/* If the left PGA is enabled hit that VU bit... */
	ret = snd_soc_component_read(component, WM8962_PWR_MGMT_2);
	if (ret & WM8962_HPOUTL_PGA_ENA) {
		snd_soc_component_write(component, WM8962_HPOUTL_VOLUME,
			      snd_soc_component_read(component, WM8962_HPOUTL_VOLUME));
		return 1;
	}

	/* ...otherwise the right.  The VU is stereo. */
	if (ret & WM8962_HPOUTR_PGA_ENA)
		snd_soc_component_write(component, WM8962_HPOUTR_VOLUME,
			      snd_soc_component_read(component, WM8962_HPOUTR_VOLUME));

	return 1;
}

/* The VU bits for the speakers are in a different register to the mute
 * bits and only take effect on the PGA if it is actually powered.
 */
static int wm8962_put_spk_sw(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int ret;

	/* Apply the update (if any) */
        ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret == 0)
		return 0;

	/* If the left PGA is enabled hit that VU bit... */
	ret = snd_soc_component_read(component, WM8962_PWR_MGMT_2);
	if (ret & WM8962_SPKOUTL_PGA_ENA) {
		snd_soc_component_write(component, WM8962_SPKOUTL_VOLUME,
			      snd_soc_component_read(component, WM8962_SPKOUTL_VOLUME));
		return 1;
	}

	/* ...otherwise the right.  The VU is stereo. */
	if (ret & WM8962_SPKOUTR_PGA_ENA)
		snd_soc_component_write(component, WM8962_SPKOUTR_VOLUME,
			      snd_soc_component_read(component, WM8962_SPKOUTR_VOLUME));

	return 1;
}

static const char *cap_hpf_mode_text[] = {
	"Hi-fi", "Application"
};

static SOC_ENUM_SINGLE_DECL(cap_hpf_mode,
			    WM8962_ADC_DAC_CONTROL_2, 10, cap_hpf_mode_text);


static const char *cap_lhpf_mode_text[] = {
	"LPF", "HPF"
};

static SOC_ENUM_SINGLE_DECL(cap_lhpf_mode,
			    WM8962_LHPF1, 1, cap_lhpf_mode_text);

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
SOC_SINGLE("Capture LHPF Switch", WM8962_LHPF1, 0, 1, 0),
SOC_ENUM("Capture LHPF Mode", cap_lhpf_mode),

SOC_DOUBLE_R_TLV("Sidetone Volume", WM8962_DAC_DSP_MIXING_1,
		 WM8962_DAC_DSP_MIXING_2, 4, 12, 0, st_tlv),

SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8962_LEFT_DAC_VOLUME,
		 WM8962_RIGHT_DAC_VOLUME, 1, 127, 0, digital_tlv),
SOC_SINGLE("DAC High Performance Switch", WM8962_ADC_DAC_CONTROL_2, 0, 1, 0),
SOC_SINGLE("DAC L/R Swap Switch", WM8962_AUDIO_INTERFACE_0, 5, 1, 0),
SOC_SINGLE("ADC L/R Swap Switch", WM8962_AUDIO_INTERFACE_0, 8, 1, 0),
SOC_SINGLE("DAC Monomix Switch", WM8962_DAC_DSP_MIXING_1, WM8962_DAC_MONOMIX_SHIFT, 1, 0),
SOC_SINGLE("ADC Monomix Switch", WM8962_THREED1, WM8962_ADC_MONOMIX_SHIFT, 1, 0),

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

SOC_SINGLE("EQ Switch", WM8962_EQ1, WM8962_EQ_ENA_SHIFT, 1, 0),
SOC_DOUBLE_R_TLV("EQ1 Volume", WM8962_EQ2, WM8962_EQ22,
		 WM8962_EQL_B1_GAIN_SHIFT, 31, 0, eq_tlv),
SOC_DOUBLE_R_TLV("EQ2 Volume", WM8962_EQ2, WM8962_EQ22,
		 WM8962_EQL_B2_GAIN_SHIFT, 31, 0, eq_tlv),
SOC_DOUBLE_R_TLV("EQ3 Volume", WM8962_EQ2, WM8962_EQ22,
		 WM8962_EQL_B3_GAIN_SHIFT, 31, 0, eq_tlv),
SOC_DOUBLE_R_TLV("EQ4 Volume", WM8962_EQ3, WM8962_EQ23,
		 WM8962_EQL_B4_GAIN_SHIFT, 31, 0, eq_tlv),
SOC_DOUBLE_R_TLV("EQ5 Volume", WM8962_EQ3, WM8962_EQ23,
		 WM8962_EQL_B5_GAIN_SHIFT, 31, 0, eq_tlv),
SND_SOC_BYTES("EQL Coefficients", WM8962_EQ4, 18),
SND_SOC_BYTES("EQR Coefficients", WM8962_EQ24, 18),


SOC_SINGLE("3D Switch", WM8962_THREED1, 0, 1, 0),
SND_SOC_BYTES_MASK("3D Coefficients", WM8962_THREED1, 4, WM8962_THREED_ENA),

SOC_SINGLE("DF1 Switch", WM8962_DF1, 0, 1, 0),
SND_SOC_BYTES_MASK("DF1 Coefficients", WM8962_DF1, 7, WM8962_DF1_ENA),

SOC_SINGLE("DRC Switch", WM8962_DRC_1, 0, 1, 0),
SND_SOC_BYTES_MASK("DRC Coefficients", WM8962_DRC_1, 5, WM8962_DRC_ENA),

WM8962_DSP2_ENABLE("VSS Switch", WM8962_VSS_ENA_SHIFT),
SND_SOC_BYTES("VSS Coefficients", WM8962_VSS_XHD2_1, 148),
WM8962_DSP2_ENABLE("HPF1 Switch", WM8962_HPF1_ENA_SHIFT),
WM8962_DSP2_ENABLE("HPF2 Switch", WM8962_HPF2_ENA_SHIFT),
SND_SOC_BYTES("HPF Coefficients", WM8962_LHPF2, 1),
WM8962_DSP2_ENABLE("HD Bass Switch", WM8962_HDBASS_ENA_SHIFT),
SND_SOC_BYTES("HD Bass Coefficients", WM8962_HDBASS_AI_1, 30),

SOC_DOUBLE("ALC Switch", WM8962_ALC1, WM8962_ALCL_ENA_SHIFT,
		WM8962_ALCR_ENA_SHIFT, 1, 0),
SND_SOC_BYTES_MASK("ALC Coefficients", WM8962_ALC1, 4,
		WM8962_ALCL_ENA_MASK | WM8962_ALCR_ENA_MASK),
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

static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(5);
		break;

	default:
		WARN(1, "Invalid event %d\n", event);
		return -EINVAL;
	}

	return 0;
}

static int hp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	int timeout;
	int reg;
	int expected = (WM8962_DCS_STARTUP_DONE_HP1L |
			WM8962_DCS_STARTUP_DONE_HP1R);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA);
		udelay(20);

		snd_soc_component_update_bits(component, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY,
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY);

		/* Start the DC servo */
		snd_soc_component_update_bits(component, WM8962_DC_SERVO_1,
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
			reg = snd_soc_component_read(component, WM8962_DC_SERVO_6);
			if (reg < 0) {
				dev_err(component->dev,
					"Failed to read DCS status: %d\n",
					reg);
				continue;
			}
			dev_dbg(component->dev, "DCS status: %x\n", reg);
		} while (++timeout < 200 && (reg & expected) != expected);

		if ((reg & expected) != expected)
			dev_err(component->dev, "DC servo timed out\n");
		else
			dev_dbg(component->dev, "DC servo complete after %dms\n",
				timeout);

		snd_soc_component_update_bits(component, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP,
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP);
		udelay(20);

		snd_soc_component_update_bits(component, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_RMV_SHORT |
				    WM8962_HP1R_RMV_SHORT, 0);

		udelay(20);

		snd_soc_component_update_bits(component, WM8962_DC_SERVO_1,
				    WM8962_HP1L_DCS_ENA | WM8962_HP1R_DCS_ENA |
				    WM8962_HP1L_DCS_STARTUP |
				    WM8962_HP1R_DCS_STARTUP,
				    0);

		snd_soc_component_update_bits(component, WM8962_ANALOGUE_HP_0,
				    WM8962_HP1L_ENA | WM8962_HP1R_ENA |
				    WM8962_HP1L_ENA_DLY | WM8962_HP1R_ENA_DLY |
				    WM8962_HP1L_ENA_OUTP |
				    WM8962_HP1R_ENA_OUTP, 0);
				    
		break;

	default:
		WARN(1, "Invalid event %d\n", event);
		return -EINVAL;
	
	}

	return 0;
}

/* VU bits for the output PGAs only take effect while the PGA is powered */
static int out_pga_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
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
		WARN(1, "Invalid shift %d\n", w->shift);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		return snd_soc_component_write(component, reg,
			snd_soc_component_read(component, reg));
	default:
		WARN(1, "Invalid event %d\n", event);
		return -EINVAL;
	}
}

static int dsp2_event(struct snd_soc_dapm_widget *w,
		      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (wm8962->dsp2_ena)
			wm8962_dsp2_start(component);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		if (wm8962->dsp2_ena)
			wm8962_dsp2_stop(component);
		break;

	default:
		WARN(1, "Invalid event %d\n", event);
		return -EINVAL;
	}

	return 0;
}

static const char *st_text[] = { "None", "Left", "Right" };

static SOC_ENUM_SINGLE_DECL(str_enum,
			    WM8962_DAC_DSP_MIXING_1, 2, st_text);

static const struct snd_kcontrol_new str_mux =
	SOC_DAPM_ENUM("Right Sidetone", str_enum);

static SOC_ENUM_SINGLE_DECL(stl_enum,
			    WM8962_DAC_DSP_MIXING_2, 2, st_text);

static const struct snd_kcontrol_new stl_mux =
	SOC_DAPM_ENUM("Left Sidetone", stl_enum);

static const char *outmux_text[] = { "DAC", "Mixer" };

static SOC_ENUM_SINGLE_DECL(spkoutr_enum,
			    WM8962_SPEAKER_MIXER_2, 7, outmux_text);

static const struct snd_kcontrol_new spkoutr_mux =
	SOC_DAPM_ENUM("SPKOUTR Mux", spkoutr_enum);

static SOC_ENUM_SINGLE_DECL(spkoutl_enum,
			    WM8962_SPEAKER_MIXER_1, 7, outmux_text);

static const struct snd_kcontrol_new spkoutl_mux =
	SOC_DAPM_ENUM("SPKOUTL Mux", spkoutl_enum);

static SOC_ENUM_SINGLE_DECL(hpoutr_enum,
			    WM8962_HEADPHONE_MIXER_2, 7, outmux_text);

static const struct snd_kcontrol_new hpoutr_mux =
	SOC_DAPM_ENUM("HPOUTR Mux", hpoutr_enum);

static SOC_ENUM_SINGLE_DECL(hpoutl_enum,
			    WM8962_HEADPHONE_MIXER_1, 7, outmux_text);

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
SND_SOC_DAPM_SIGGEN("Beep"),
SND_SOC_DAPM_INPUT("DMICDAT"),

SND_SOC_DAPM_SUPPLY("MICBIAS", WM8962_PWR_MGMT_1, 1, 0, NULL, 0),

SND_SOC_DAPM_SUPPLY("Class G", WM8962_CHARGE_PUMP_B, 0, 1, NULL, 0),
SND_SOC_DAPM_SUPPLY("SYSCLK", WM8962_CLOCKING2, 5, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("Charge Pump", WM8962_CHARGE_PUMP_1, 0, 0, cp_event,
		    SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_SUPPLY("TOCLK", WM8962_ADDITIONAL_CONTROL_1, 0, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY_S("DSP2", 1, WM8962_DSP2_POWER_MANAGEMENT,
		      WM8962_DSP2_ENA_SHIFT, 0, dsp2_event,
		      SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_SUPPLY("TEMP_HP", WM8962_ADDITIONAL_CONTROL_4, 2, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("TEMP_SPK", WM8962_ADDITIONAL_CONTROL_4, 1, 0, NULL, 0),

SND_SOC_DAPM_MIXER("INPGAL", WM8962_LEFT_INPUT_PGA_CONTROL, 4, 0,
		   inpgal, ARRAY_SIZE(inpgal)),
SND_SOC_DAPM_MIXER("INPGAR", WM8962_RIGHT_INPUT_PGA_CONTROL, 4, 0,
		   inpgar, ARRAY_SIZE(inpgar)),
SND_SOC_DAPM_MIXER("MIXINL", WM8962_PWR_MGMT_1, 5, 0,
		   mixinl, ARRAY_SIZE(mixinl)),
SND_SOC_DAPM_MIXER("MIXINR", WM8962_PWR_MGMT_1, 4, 0,
		   mixinr, ARRAY_SIZE(mixinr)),

SND_SOC_DAPM_AIF_IN("DMIC_ENA", NULL, 0, WM8962_PWR_MGMT_1, 10, 0),

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

	{ "DMIC_ENA", NULL, "DMICDAT" },

	{ "ADCL", NULL, "SYSCLK" },
	{ "ADCL", NULL, "TOCLK" },
	{ "ADCL", NULL, "MIXINL" },
	{ "ADCL", NULL, "DMIC_ENA" },
	{ "ADCL", NULL, "DSP2" },

	{ "ADCR", NULL, "SYSCLK" },
	{ "ADCR", NULL, "TOCLK" },
	{ "ADCR", NULL, "MIXINR" },
	{ "ADCR", NULL, "DMIC_ENA" },
	{ "ADCR", NULL, "DSP2" },

	{ "STL", "Left", "ADCL" },
	{ "STL", "Right", "ADCR" },
	{ "STL", NULL, "Class G" },

	{ "STR", "Left", "ADCL" },
	{ "STR", "Right", "ADCR" },
	{ "STR", NULL, "Class G" },

	{ "DACL", NULL, "SYSCLK" },
	{ "DACL", NULL, "TOCLK" },
	{ "DACL", NULL, "Beep" },
	{ "DACL", NULL, "STL" },
	{ "DACL", NULL, "DSP2" },

	{ "DACR", NULL, "SYSCLK" },
	{ "DACR", NULL, "TOCLK" },
	{ "DACR", NULL, "Beep" },
	{ "DACR", NULL, "STR" },
	{ "DACR", NULL, "DSP2" },

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

	{ "HPOUTL", NULL, "TEMP_HP" },
	{ "HPOUTR", NULL, "TEMP_HP" },
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
	{ "Speaker Output", NULL, "TEMP_SPK" },

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
	{ "SPKOUTL Output", NULL, "TEMP_SPK" },

	{ "SPKOUTR Output", NULL, "SPKOUTR PGA" },
	{ "SPKOUTR Output", NULL, "SYSCLK" },
	{ "SPKOUTR Output", NULL, "TOCLK" },
	{ "SPKOUTR Output", NULL, "TEMP_SPK" },

	{ "SPKOUTL", NULL, "SPKOUTL Output" },
	{ "SPKOUTR", NULL, "SPKOUTR Output" },
};

static int wm8962_add_widgets(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	struct wm8962_pdata *pdata = &wm8962->pdata;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	snd_soc_add_component_controls(component, wm8962_snd_controls,
			     ARRAY_SIZE(wm8962_snd_controls));
	if (pdata->spk_mono)
		snd_soc_add_component_controls(component, wm8962_spk_mono_controls,
				     ARRAY_SIZE(wm8962_spk_mono_controls));
	else
		snd_soc_add_component_controls(component, wm8962_spk_stereo_controls,
				     ARRAY_SIZE(wm8962_spk_stereo_controls));


	snd_soc_dapm_new_controls(dapm, wm8962_dapm_widgets,
				  ARRAY_SIZE(wm8962_dapm_widgets));
	if (pdata->spk_mono)
		snd_soc_dapm_new_controls(dapm, wm8962_dapm_spk_mono_widgets,
					  ARRAY_SIZE(wm8962_dapm_spk_mono_widgets));
	else
		snd_soc_dapm_new_controls(dapm, wm8962_dapm_spk_stereo_widgets,
					  ARRAY_SIZE(wm8962_dapm_spk_stereo_widgets));

	snd_soc_dapm_add_routes(dapm, wm8962_intercon,
				ARRAY_SIZE(wm8962_intercon));
	if (pdata->spk_mono)
		snd_soc_dapm_add_routes(dapm, wm8962_spk_mono_intercon,
					ARRAY_SIZE(wm8962_spk_mono_intercon));
	else
		snd_soc_dapm_add_routes(dapm, wm8962_spk_stereo_intercon,
					ARRAY_SIZE(wm8962_spk_stereo_intercon));


	snd_soc_dapm_disable_pin(dapm, "Beep");

	return 0;
}

/* -1 for reserved values */
static const int bclk_divs[] = {
	1, -1, 2, 3, 4, -1, 6, 8, -1, 12, 16, 24, -1, 32, 32, 32
};

static const int sysclk_rates[] = {
	64, 128, 192, 256, 384, 512, 768, 1024, 1408, 1536, 3072, 6144
};

static void wm8962_configure_bclk(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	int best, min_diff, diff;
	int dspclk, i;
	int clocking2 = 0;
	int clocking4 = 0;
	int aif2 = 0;

	if (!wm8962->sysclk_rate) {
		dev_dbg(component->dev, "No SYSCLK configured\n");
		return;
	}

	if (!wm8962->bclk || !wm8962->lrclk) {
		dev_dbg(component->dev, "No audio clocks configured\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(sysclk_rates); i++) {
		if (sysclk_rates[i] == wm8962->sysclk_rate / wm8962->lrclk) {
			clocking4 |= i << WM8962_SYSCLK_RATE_SHIFT;
			break;
		}
	}

	if (i == ARRAY_SIZE(sysclk_rates)) {
		dev_err(component->dev, "Unsupported sysclk ratio %d\n",
			wm8962->sysclk_rate / wm8962->lrclk);
		return;
	}

	dev_dbg(component->dev, "Selected sysclk ratio %d\n", sysclk_rates[i]);

	snd_soc_component_update_bits(component, WM8962_CLOCKING_4,
			    WM8962_SYSCLK_RATE_MASK, clocking4);

	/* DSPCLK_DIV can be only generated correctly after enabling SYSCLK.
	 * So we here provisionally enable it and then disable it afterward
	 * if current bias_level hasn't reached SND_SOC_BIAS_ON.
	 */
	if (snd_soc_component_get_bias_level(component) != SND_SOC_BIAS_ON)
		snd_soc_component_update_bits(component, WM8962_CLOCKING2,
				WM8962_SYSCLK_ENA_MASK, WM8962_SYSCLK_ENA);

	dspclk = snd_soc_component_read(component, WM8962_CLOCKING1);

	if (snd_soc_component_get_bias_level(component) != SND_SOC_BIAS_ON)
		snd_soc_component_update_bits(component, WM8962_CLOCKING2,
				WM8962_SYSCLK_ENA_MASK, 0);

	if (dspclk < 0) {
		dev_err(component->dev, "Failed to read DSPCLK: %d\n", dspclk);
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
		dev_warn(component->dev, "Unknown DSPCLK divisor read back\n");
		dspclk = wm8962->sysclk_rate;
	}

	dev_dbg(component->dev, "DSPCLK is %dHz, BCLK %d\n", dspclk, wm8962->bclk);

	/* Search a proper bclk, not exact match. */
	best = 0;
	min_diff = INT_MAX;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		if (bclk_divs[i] < 0)
			continue;

		diff = (dspclk / bclk_divs[i]) - wm8962->bclk;
		if (diff < 0) /* Table is sorted */
			break;
		if (diff < min_diff) {
			best = i;
			min_diff = diff;
		}
	}
	wm8962->bclk = dspclk / bclk_divs[best];
	clocking2 |= best;
	dev_dbg(component->dev, "Selected BCLK_DIV %d for %dHz\n",
		bclk_divs[best], wm8962->bclk);

	aif2 |= wm8962->bclk / wm8962->lrclk;
	dev_dbg(component->dev, "Selected LRCLK divisor %d for %dHz\n",
		wm8962->bclk / wm8962->lrclk, wm8962->lrclk);

	snd_soc_component_update_bits(component, WM8962_CLOCKING2,
			    WM8962_BCLK_DIV_MASK, clocking2);
	snd_soc_component_update_bits(component, WM8962_AUDIO_INTERFACE_2,
			    WM8962_AIF_RATE_MASK, aif2);
}

static int wm8962_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID 2*50k */
		snd_soc_component_update_bits(component, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK, 0x80);

		wm8962_configure_bclk(component);
		break;

	case SND_SOC_BIAS_STANDBY:
		/* VMID 2*250k */
		snd_soc_component_update_bits(component, WM8962_PWR_MGMT_1,
				    WM8962_VMID_SEL_MASK, 0x100);

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF)
			msleep(100);
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}

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

static int wm8962_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	int i;
	int aif0 = 0;
	int adctl3 = 0;

	wm8962->bclk = snd_soc_params_to_bclk(params);
	if (params_channels(params) == 1)
		wm8962->bclk *= 2;

	wm8962->lrclk = params_rate(params);

	for (i = 0; i < ARRAY_SIZE(sr_vals); i++) {
		if (sr_vals[i].rate == wm8962->lrclk) {
			adctl3 |= sr_vals[i].reg;
			break;
		}
	}
	if (i == ARRAY_SIZE(sr_vals)) {
		dev_err(component->dev, "Unsupported rate %dHz\n", wm8962->lrclk);
		return -EINVAL;
	}

	if (wm8962->lrclk % 8000 == 0)
		adctl3 |= WM8962_SAMPLE_RATE_INT_MODE;

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		aif0 |= 0x4;
		break;
	case 24:
		aif0 |= 0x8;
		break;
	case 32:
		aif0 |= 0xc;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, WM8962_AUDIO_INTERFACE_0,
			    WM8962_WL_MASK, aif0);
	snd_soc_component_update_bits(component, WM8962_ADDITIONAL_CONTROL_3,
			    WM8962_SAMPLE_RATE_INT_MODE |
			    WM8962_SAMPLE_RATE_MASK, adctl3);

	dev_dbg(component->dev, "hw_params set BCLK %dHz LRCLK %dHz\n",
		wm8962->bclk, wm8962->lrclk);

	if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_ON)
		wm8962_configure_bclk(component);

	return 0;
}

static int wm8962_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
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

	snd_soc_component_update_bits(component, WM8962_CLOCKING2, WM8962_SYSCLK_SRC_MASK,
			    src);

	wm8962->sysclk_rate = freq;

	return 0;
}

static int wm8962_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	int aif0 = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif0 |= WM8962_LRCLK_INV | 3;
		fallthrough;
	case SND_SOC_DAIFMT_DSP_A:
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

	snd_soc_component_update_bits(component, WM8962_AUDIO_INTERFACE_0,
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
		fll_div->lambda = 1;
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

static int wm8962_set_fll(struct snd_soc_component *component, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	struct _fll_div fll_div;
	unsigned long timeout;
	int ret;
	int fll1 = 0;

	/* Any change? */
	if (source == wm8962->fll_src && Fref == wm8962->fll_fref &&
	    Fout == wm8962->fll_fout)
		return 0;

	if (Fout == 0) {
		dev_dbg(component->dev, "FLL disabled\n");

		wm8962->fll_fref = 0;
		wm8962->fll_fout = 0;

		snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_ENA, 0);

		pm_runtime_put(component->dev);

		return 0;
	}

	ret = fll_factors(&fll_div, Fref, Fout);
	if (ret != 0)
		return ret;

	/* Parameters good, disable so we can reprogram */
	snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_1, WM8962_FLL_ENA, 0);

	switch (fll_id) {
	case WM8962_FLL_MCLK:
	case WM8962_FLL_BCLK:
	case WM8962_FLL_OSC:
		fll1 |= (fll_id - 1) << WM8962_FLL_REFCLK_SRC_SHIFT;
		break;
	case WM8962_FLL_INT:
		snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_OSC_ENA, WM8962_FLL_OSC_ENA);
		snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_5,
				    WM8962_FLL_FRC_NCO, WM8962_FLL_FRC_NCO);
		break;
	default:
		dev_err(component->dev, "Unknown FLL source %d\n", ret);
		return -EINVAL;
	}

	if (fll_div.theta)
		fll1 |= WM8962_FLL_FRAC;

	/* Stop the FLL while we reconfigure */
	snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_1, WM8962_FLL_ENA, 0);

	snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_2,
			    WM8962_FLL_OUTDIV_MASK |
			    WM8962_FLL_REFCLK_DIV_MASK,
			    (fll_div.fll_outdiv << WM8962_FLL_OUTDIV_SHIFT) |
			    (fll_div.fll_refclk_div));

	snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_3,
			    WM8962_FLL_FRATIO_MASK, fll_div.fll_fratio);

	snd_soc_component_write(component, WM8962_FLL_CONTROL_6, fll_div.theta);
	snd_soc_component_write(component, WM8962_FLL_CONTROL_7, fll_div.lambda);
	snd_soc_component_write(component, WM8962_FLL_CONTROL_8, fll_div.n);

	reinit_completion(&wm8962->fll_lock);

	ret = pm_runtime_get_sync(component->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(component->dev);
		dev_err(component->dev, "Failed to resume device: %d\n", ret);
		return ret;
	}

	snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_1,
			    WM8962_FLL_FRAC | WM8962_FLL_REFCLK_SRC_MASK |
			    WM8962_FLL_ENA, fll1 | WM8962_FLL_ENA);

	dev_dbg(component->dev, "FLL configured for %dHz->%dHz\n", Fref, Fout);

	/* This should be a massive overestimate but go even
	 * higher if we'll error out
	 */
	if (wm8962->irq)
		timeout = msecs_to_jiffies(5);
	else
		timeout = msecs_to_jiffies(1);

	timeout = wait_for_completion_timeout(&wm8962->fll_lock,
					      timeout);

	if (timeout == 0 && wm8962->irq) {
		dev_err(component->dev, "FLL lock timed out");
		snd_soc_component_update_bits(component, WM8962_FLL_CONTROL_1,
				    WM8962_FLL_ENA, 0);
		pm_runtime_put(component->dev);
		return -ETIMEDOUT;
	}

	wm8962->fll_fref = Fref;
	wm8962->fll_fout = Fout;
	wm8962->fll_src = source;

	return 0;
}

static int wm8962_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	int val, ret;

	if (mute)
		val = WM8962_DAC_MUTE | WM8962_DAC_MUTE_ALT;
	else
		val = 0;

	/**
	 * The DAC mute bit is mirrored in two registers, update both to keep
	 * the register cache consistent.
	 */
	ret = snd_soc_component_update_bits(component, WM8962_CLASS_D_CONTROL_1,
				  WM8962_DAC_MUTE_ALT, val);
	if (ret < 0)
		return ret;

	return snd_soc_component_update_bits(component, WM8962_ADC_DAC_CONTROL_1,
				   WM8962_DAC_MUTE, val);
}

#define WM8962_RATES (SNDRV_PCM_RATE_8000_48000 |\
		SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define WM8962_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8962_dai_ops = {
	.hw_params = wm8962_hw_params,
	.set_sysclk = wm8962_set_dai_sysclk,
	.set_fmt = wm8962_set_dai_fmt,
	.mute_stream = wm8962_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8962_dai = {
	.name = "wm8962",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8962_RATES,
		.formats = WM8962_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8962_RATES,
		.formats = WM8962_FORMATS,
	},
	.ops = &wm8962_dai_ops,
	.symmetric_rate = 1,
};

static void wm8962_mic_work(struct work_struct *work)
{
	struct wm8962_priv *wm8962 = container_of(work,
						  struct wm8962_priv,
						  mic_work.work);
	struct snd_soc_component *component = wm8962->component;
	int status = 0;
	int irq_pol = 0;
	int reg;

	reg = snd_soc_component_read(component, WM8962_ADDITIONAL_CONTROL_4);

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

	snd_soc_component_update_bits(component, WM8962_MICINT_SOURCE_POL,
			    WM8962_MICSCD_IRQ_POL |
			    WM8962_MICD_IRQ_POL, irq_pol);
}

static irqreturn_t wm8962_irq(int irq, void *data)
{
	struct device *dev = data;
	struct wm8962_priv *wm8962 = dev_get_drvdata(dev);
	unsigned int mask;
	unsigned int active;
	int reg, ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		dev_err(dev, "Failed to resume: %d\n", ret);
		return IRQ_NONE;
	}

	ret = regmap_read(wm8962->regmap, WM8962_INTERRUPT_STATUS_2_MASK,
			  &mask);
	if (ret != 0) {
		pm_runtime_put(dev);
		dev_err(dev, "Failed to read interrupt mask: %d\n",
			ret);
		return IRQ_NONE;
	}

	ret = regmap_read(wm8962->regmap, WM8962_INTERRUPT_STATUS_2, &active);
	if (ret != 0) {
		pm_runtime_put(dev);
		dev_err(dev, "Failed to read interrupt: %d\n", ret);
		return IRQ_NONE;
	}

	active &= ~mask;

	if (!active) {
		pm_runtime_put(dev);
		return IRQ_NONE;
	}

	/* Acknowledge the interrupts */
	ret = regmap_write(wm8962->regmap, WM8962_INTERRUPT_STATUS_2, active);
	if (ret != 0)
		dev_warn(dev, "Failed to ack interrupt: %d\n", ret);

	if (active & WM8962_FLL_LOCK_EINT) {
		dev_dbg(dev, "FLL locked\n");
		complete(&wm8962->fll_lock);
	}

	if (active & WM8962_FIFOS_ERR_EINT)
		dev_err(dev, "FIFO error\n");

	if (active & WM8962_TEMP_SHUT_EINT) {
		dev_crit(dev, "Thermal shutdown\n");

		ret = regmap_read(wm8962->regmap,
				  WM8962_THERMAL_SHUTDOWN_STATUS,  &reg);
		if (ret != 0) {
			dev_warn(dev, "Failed to read thermal status: %d\n",
				 ret);
			reg = 0;
		}

		if (reg & WM8962_TEMP_ERR_HP)
			dev_crit(dev, "Headphone thermal error\n");
		if (reg & WM8962_TEMP_WARN_HP)
			dev_crit(dev, "Headphone thermal warning\n");
		if (reg & WM8962_TEMP_ERR_SPK)
			dev_crit(dev, "Speaker thermal error\n");
		if (reg & WM8962_TEMP_WARN_SPK)
			dev_crit(dev, "Speaker thermal warning\n");
	}

	if (active & (WM8962_MICSCD_EINT | WM8962_MICD_EINT)) {
		dev_dbg(dev, "Microphone event detected\n");

#ifndef CONFIG_SND_SOC_WM8962_MODULE
		trace_snd_soc_jack_irq(dev_name(dev));
#endif

		pm_wakeup_event(dev, 300);

		queue_delayed_work(system_power_efficient_wq,
				   &wm8962->mic_work,
				   msecs_to_jiffies(250));
	}

	pm_runtime_put(dev);

	return IRQ_HANDLED;
}

/**
 * wm8962_mic_detect - Enable microphone detection via the WM8962 IRQ
 *
 * @component:  WM8962 component
 * @jack:   jack to report detection events on
 *
 * Enable microphone detection via IRQ on the WM8962.  If GPIOs are
 * being used to bring out signals to the processor then only platform
 * data configuration is needed for WM8962 and processor GPIOs should
 * be configured using snd_soc_jack_add_gpios() instead.
 *
 * If no jack is supplied detection will be disabled.
 */
int wm8962_mic_detect(struct snd_soc_component *component, struct snd_soc_jack *jack)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int irq_mask, enable;

	wm8962->jack = jack;
	if (jack) {
		irq_mask = 0;
		enable = WM8962_MICDET_ENA;
	} else {
		irq_mask = WM8962_MICD_EINT | WM8962_MICSCD_EINT;
		enable = 0;
	}

	snd_soc_component_update_bits(component, WM8962_INTERRUPT_STATUS_2_MASK,
			    WM8962_MICD_EINT | WM8962_MICSCD_EINT, irq_mask);
	snd_soc_component_update_bits(component, WM8962_ADDITIONAL_CONTROL_4,
			    WM8962_MICDET_ENA, enable);

	/* Send an initial empty report */
	snd_soc_jack_report(wm8962->jack, 0,
			    SND_JACK_MICROPHONE | SND_JACK_BTN_0);

	snd_soc_dapm_mutex_lock(dapm);

	if (jack) {
		snd_soc_dapm_force_enable_pin_unlocked(dapm, "SYSCLK");
		snd_soc_dapm_force_enable_pin_unlocked(dapm, "MICBIAS");
	} else {
		snd_soc_dapm_disable_pin_unlocked(dapm, "SYSCLK");
		snd_soc_dapm_disable_pin_unlocked(dapm, "MICBIAS");
	}

	snd_soc_dapm_mutex_unlock(dapm);

	return 0;
}
EXPORT_SYMBOL_GPL(wm8962_mic_detect);

static int beep_rates[] = {
	500, 1000, 2000, 4000,
};

static void wm8962_beep_work(struct work_struct *work)
{
	struct wm8962_priv *wm8962 =
		container_of(work, struct wm8962_priv, beep_work);
	struct snd_soc_component *component = wm8962->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int i;
	int reg = 0;
	int best = 0;

	if (wm8962->beep_rate) {
		for (i = 0; i < ARRAY_SIZE(beep_rates); i++) {
			if (abs(wm8962->beep_rate - beep_rates[i]) <
			    abs(wm8962->beep_rate - beep_rates[best]))
				best = i;
		}

		dev_dbg(component->dev, "Set beep rate %dHz for requested %dHz\n",
			beep_rates[best], wm8962->beep_rate);

		reg = WM8962_BEEP_ENA | (best << WM8962_BEEP_RATE_SHIFT);

		snd_soc_dapm_enable_pin(dapm, "Beep");
	} else {
		dev_dbg(component->dev, "Disabling beep\n");
		snd_soc_dapm_disable_pin(dapm, "Beep");
	}

	snd_soc_component_update_bits(component, WM8962_BEEP_GENERATOR_1,
			    WM8962_BEEP_ENA | WM8962_BEEP_RATE_MASK, reg);

	snd_soc_dapm_sync(dapm);
}

/* For usability define a way of injecting beep events for the device -
 * many systems will not have a keyboard.
 */
static int wm8962_beep_event(struct input_dev *dev, unsigned int type,
			     unsigned int code, int hz)
{
	struct snd_soc_component *component = input_get_drvdata(dev);
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "Beep event %x %x\n", code, hz);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 1000;
		fallthrough;
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

static ssize_t beep_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct wm8962_priv *wm8962 = dev_get_drvdata(dev);
	long int time;
	int ret;

	ret = kstrtol(buf, 10, &time);
	if (ret != 0)
		return ret;

	input_event(wm8962->beep, EV_SND, SND_TONE, time);

	return count;
}

static DEVICE_ATTR_WO(beep);

static void wm8962_init_beep(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	int ret;

	wm8962->beep = devm_input_allocate_device(component->dev);
	if (!wm8962->beep) {
		dev_err(component->dev, "Failed to allocate beep device\n");
		return;
	}

	INIT_WORK(&wm8962->beep_work, wm8962_beep_work);
	wm8962->beep_rate = 0;

	wm8962->beep->name = "WM8962 Beep Generator";
	wm8962->beep->phys = dev_name(component->dev);
	wm8962->beep->id.bustype = BUS_I2C;

	wm8962->beep->evbit[0] = BIT_MASK(EV_SND);
	wm8962->beep->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	wm8962->beep->event = wm8962_beep_event;
	wm8962->beep->dev.parent = component->dev;
	input_set_drvdata(wm8962->beep, component);

	ret = input_register_device(wm8962->beep);
	if (ret != 0) {
		wm8962->beep = NULL;
		dev_err(component->dev, "Failed to register beep device\n");
	}

	ret = device_create_file(component->dev, &dev_attr_beep);
	if (ret != 0) {
		dev_err(component->dev, "Failed to create keyclick file: %d\n",
			ret);
	}
}

static void wm8962_free_beep(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	device_remove_file(component->dev, &dev_attr_beep);
	cancel_work_sync(&wm8962->beep_work);
	wm8962->beep = NULL;

	snd_soc_component_update_bits(component, WM8962_BEEP_GENERATOR_1, WM8962_BEEP_ENA,0);
}

static void wm8962_set_gpio_mode(struct wm8962_priv *wm8962, int gpio)
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
		regmap_update_bits(wm8962->regmap, WM8962_ANALOGUE_CLOCKING1,
				   mask, val);
}

#ifdef CONFIG_GPIOLIB
static int wm8962_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct wm8962_priv *wm8962 = gpiochip_get_data(chip);

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

	wm8962_set_gpio_mode(wm8962, offset + 1);

	return 0;
}

static void wm8962_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8962_priv *wm8962 = gpiochip_get_data(chip);
	struct snd_soc_component *component = wm8962->component;

	snd_soc_component_update_bits(component, WM8962_GPIO_BASE + offset,
			    WM8962_GP2_LVL, !!value << WM8962_GP2_LVL_SHIFT);
}

static int wm8962_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8962_priv *wm8962 = gpiochip_get_data(chip);
	struct snd_soc_component *component = wm8962->component;
	int ret, val;

	/* Force function 1 (logic output) */
	val = (1 << WM8962_GP2_FN_SHIFT) | (value << WM8962_GP2_LVL_SHIFT);

	ret = snd_soc_component_update_bits(component, WM8962_GPIO_BASE + offset,
				  WM8962_GP2_FN_MASK | WM8962_GP2_LVL, val);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct gpio_chip wm8962_template_chip = {
	.label			= "wm8962",
	.owner			= THIS_MODULE,
	.request		= wm8962_gpio_request,
	.direction_output	= wm8962_gpio_direction_out,
	.set			= wm8962_gpio_set,
	.can_sleep		= 1,
};

static void wm8962_init_gpio(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	struct wm8962_pdata *pdata = &wm8962->pdata;
	int ret;

	wm8962->gpio_chip = wm8962_template_chip;
	wm8962->gpio_chip.ngpio = WM8962_MAX_GPIO;
	wm8962->gpio_chip.parent = component->dev;

	if (pdata->gpio_base)
		wm8962->gpio_chip.base = pdata->gpio_base;
	else
		wm8962->gpio_chip.base = -1;

	ret = gpiochip_add_data(&wm8962->gpio_chip, wm8962);
	if (ret != 0)
		dev_err(component->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm8962_free_gpio(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	gpiochip_remove(&wm8962->gpio_chip);
}
#else
static void wm8962_init_gpio(struct snd_soc_component *component)
{
}

static void wm8962_free_gpio(struct snd_soc_component *component)
{
}
#endif

static int wm8962_probe(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	int ret;
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);
	int i;
	bool dmicclk, dmicdat;

	wm8962->component = component;

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
		ret = devm_regulator_register_notifier(
						wm8962->supplies[i].consumer,
						&wm8962->disable_nb[i]);
		if (ret != 0) {
			dev_err(component->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	wm8962_add_widgets(component);

	/* Save boards having to disable DMIC when not in use */
	dmicclk = false;
	dmicdat = false;
	for (i = 1; i < WM8962_MAX_GPIO; i++) {
		/*
		 * Register 515 (WM8962_GPIO_BASE + 3) does not exist,
		 * so skip its access
		 */
		if (i == 3)
			continue;
		switch (snd_soc_component_read(component, WM8962_GPIO_BASE + i)
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
		dev_dbg(component->dev, "DMIC not in use, disabling\n");
		snd_soc_dapm_nc_pin(dapm, "DMICDAT");
	}
	if (dmicclk != dmicdat)
		dev_warn(component->dev, "DMIC GPIOs partially configured\n");

	wm8962_init_beep(component);
	wm8962_init_gpio(component);

	return 0;
}

static void wm8962_remove(struct snd_soc_component *component)
{
	struct wm8962_priv *wm8962 = snd_soc_component_get_drvdata(component);

	cancel_delayed_work_sync(&wm8962->mic_work);

	wm8962_free_gpio(component);
	wm8962_free_beep(component);
}

static const struct snd_soc_component_driver soc_component_dev_wm8962 = {
	.probe			= wm8962_probe,
	.remove			= wm8962_remove,
	.set_bias_level		= wm8962_set_bias_level,
	.set_pll		= wm8962_set_fll,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

/* Improve power consumption for IN4 DC measurement mode */
static const struct reg_sequence wm8962_dc_measure[] = {
	{ 0xfd, 0x1 },
	{ 0xcc, 0x40 },
	{ 0xfd, 0 },
};

static const struct regmap_config wm8962_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = WM8962_MAX_REGISTER,
	.reg_defaults = wm8962_reg,
	.num_reg_defaults = ARRAY_SIZE(wm8962_reg),
	.volatile_reg = wm8962_volatile_register,
	.readable_reg = wm8962_readable_register,
	.cache_type = REGCACHE_RBTREE,
};

static int wm8962_set_pdata_from_of(struct i2c_client *i2c,
				    struct wm8962_pdata *pdata)
{
	const struct device_node *np = i2c->dev.of_node;
	u32 val32;
	int i;

	if (of_property_read_bool(np, "spk-mono"))
		pdata->spk_mono = true;

	if (of_property_read_u32(np, "mic-cfg", &val32) >= 0)
		pdata->mic_cfg = val32;

	if (of_property_read_u32_array(np, "gpio-cfg", pdata->gpio_init,
				       ARRAY_SIZE(pdata->gpio_init)) >= 0)
		for (i = 0; i < ARRAY_SIZE(pdata->gpio_init); i++) {
			/*
			 * The range of GPIO register value is [0x0, 0xffff]
			 * While the default value of each register is 0x0
			 * Any other value will be regarded as default value
			 */
			if (pdata->gpio_init[i] > 0xffff)
				pdata->gpio_init[i] = 0x0;
		}

	pdata->mclk = devm_clk_get_optional(&i2c->dev, NULL);
	return PTR_ERR_OR_ZERO(pdata->mclk);
}

static int wm8962_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8962_pdata *pdata = dev_get_platdata(&i2c->dev);
	struct wm8962_priv *wm8962;
	unsigned int reg;
	int ret, i, irq_pol, trigger;

	wm8962 = devm_kzalloc(&i2c->dev, sizeof(*wm8962), GFP_KERNEL);
	if (wm8962 == NULL)
		return -ENOMEM;

	mutex_init(&wm8962->dsp2_ena_lock);

	i2c_set_clientdata(i2c, wm8962);

	INIT_DELAYED_WORK(&wm8962->mic_work, wm8962_mic_work);
	init_completion(&wm8962->fll_lock);
	wm8962->irq = i2c->irq;

	/* If platform data was supplied, update the default data in priv */
	if (pdata) {
		memcpy(&wm8962->pdata, pdata, sizeof(struct wm8962_pdata));
	} else if (i2c->dev.of_node) {
		ret = wm8962_set_pdata_from_of(i2c, &wm8962->pdata);
		if (ret != 0)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8962->supplies); i++)
		wm8962->supplies[i].supply = wm8962_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8962->supplies),
				 wm8962->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		goto err;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8962->supplies),
				    wm8962->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	wm8962->regmap = devm_regmap_init_i2c(i2c, &wm8962_regmap);
	if (IS_ERR(wm8962->regmap)) {
		ret = PTR_ERR(wm8962->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap: %d\n", ret);
		goto err_enable;
	}

	/*
	 * We haven't marked the chip revision as volatile due to
	 * sharing a register with the right input volume; explicitly
	 * bypass the cache to read it.
	 */
	regcache_cache_bypass(wm8962->regmap, true);

	ret = regmap_read(wm8962->regmap, WM8962_SOFTWARE_RESET, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (reg != 0x6243) {
		dev_err(&i2c->dev,
			"Device is not a WM8962, ID %x != 0x6243\n", reg);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = regmap_read(wm8962->regmap, WM8962_RIGHT_INPUT_VOLUME, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_enable;
	}

	dev_info(&i2c->dev, "customer id %x revision %c\n",
		 (reg & WM8962_CUST_ID_MASK) >> WM8962_CUST_ID_SHIFT,
		 ((reg & WM8962_CHIP_REV_MASK) >> WM8962_CHIP_REV_SHIFT)
		 + 'A');

	regcache_cache_bypass(wm8962->regmap, false);

	ret = wm8962_reset(wm8962);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	/* SYSCLK defaults to on; make sure it is off so we can safely
	 * write to registers if the device is declocked.
	 */
	regmap_update_bits(wm8962->regmap, WM8962_CLOCKING2,
			   WM8962_SYSCLK_ENA, 0);

	/* Ensure we have soft control over all registers */
	regmap_update_bits(wm8962->regmap, WM8962_CLOCKING2,
			   WM8962_CLKREG_OVD, WM8962_CLKREG_OVD);

	/* Ensure that the oscillator and PLLs are disabled */
	regmap_update_bits(wm8962->regmap, WM8962_PLL2,
			   WM8962_OSC_ENA | WM8962_PLL2_ENA | WM8962_PLL3_ENA,
			   0);

	/* Apply static configuration for GPIOs */
	for (i = 0; i < ARRAY_SIZE(wm8962->pdata.gpio_init); i++)
		if (wm8962->pdata.gpio_init[i]) {
			wm8962_set_gpio_mode(wm8962, i + 1);
			regmap_write(wm8962->regmap, 0x200 + i,
				     wm8962->pdata.gpio_init[i] & 0xffff);
		}


	/* Put the speakers into mono mode? */
	if (wm8962->pdata.spk_mono)
		regmap_update_bits(wm8962->regmap, WM8962_CLASS_D_CONTROL_2,
				   WM8962_SPK_MONO_MASK, WM8962_SPK_MONO);

	/* Micbias setup, detection enable and detection
	 * threasholds. */
	if (wm8962->pdata.mic_cfg)
		regmap_update_bits(wm8962->regmap, WM8962_ADDITIONAL_CONTROL_4,
				   WM8962_MICDET_ENA |
				   WM8962_MICDET_THR_MASK |
				   WM8962_MICSHORT_THR_MASK |
				   WM8962_MICBIAS_LVL,
				   wm8962->pdata.mic_cfg);

	/* Latch volume update bits */
	regmap_update_bits(wm8962->regmap, WM8962_LEFT_INPUT_VOLUME,
			   WM8962_IN_VU, WM8962_IN_VU);
	regmap_update_bits(wm8962->regmap, WM8962_RIGHT_INPUT_VOLUME,
			   WM8962_IN_VU, WM8962_IN_VU);
	regmap_update_bits(wm8962->regmap, WM8962_LEFT_ADC_VOLUME,
			   WM8962_ADC_VU, WM8962_ADC_VU);
	regmap_update_bits(wm8962->regmap, WM8962_RIGHT_ADC_VOLUME,
			   WM8962_ADC_VU, WM8962_ADC_VU);
	regmap_update_bits(wm8962->regmap, WM8962_LEFT_DAC_VOLUME,
			   WM8962_DAC_VU, WM8962_DAC_VU);
	regmap_update_bits(wm8962->regmap, WM8962_RIGHT_DAC_VOLUME,
			   WM8962_DAC_VU, WM8962_DAC_VU);
	regmap_update_bits(wm8962->regmap, WM8962_SPKOUTL_VOLUME,
			   WM8962_SPKOUT_VU, WM8962_SPKOUT_VU);
	regmap_update_bits(wm8962->regmap, WM8962_SPKOUTR_VOLUME,
			   WM8962_SPKOUT_VU, WM8962_SPKOUT_VU);
	regmap_update_bits(wm8962->regmap, WM8962_HPOUTL_VOLUME,
			   WM8962_HPOUT_VU, WM8962_HPOUT_VU);
	regmap_update_bits(wm8962->regmap, WM8962_HPOUTR_VOLUME,
			   WM8962_HPOUT_VU, WM8962_HPOUT_VU);

	/* Stereo control for EQ */
	regmap_update_bits(wm8962->regmap, WM8962_EQ1,
			   WM8962_EQ_SHARED_COEFF, 0);

	/* Don't debouce interrupts so we don't need SYSCLK */
	regmap_update_bits(wm8962->regmap, WM8962_IRQ_DEBOUNCE,
			   WM8962_FLL_LOCK_DB | WM8962_PLL3_LOCK_DB |
			   WM8962_PLL2_LOCK_DB | WM8962_TEMP_SHUT_DB,
			   0);

	if (wm8962->pdata.in4_dc_measure) {
		ret = regmap_register_patch(wm8962->regmap,
					    wm8962_dc_measure,
					    ARRAY_SIZE(wm8962_dc_measure));
		if (ret != 0)
			dev_err(&i2c->dev,
				"Failed to configure for DC measurement: %d\n",
				ret);
	}

	if (wm8962->irq) {
		if (wm8962->pdata.irq_active_low) {
			trigger = IRQF_TRIGGER_LOW;
			irq_pol = WM8962_IRQ_POL;
		} else {
			trigger = IRQF_TRIGGER_HIGH;
			irq_pol = 0;
		}

		regmap_update_bits(wm8962->regmap, WM8962_INTERRUPT_CONTROL,
				   WM8962_IRQ_POL, irq_pol);

		ret = devm_request_threaded_irq(&i2c->dev, wm8962->irq, NULL,
						wm8962_irq,
						trigger | IRQF_ONESHOT,
						"wm8962", &i2c->dev);
		if (ret != 0) {
			dev_err(&i2c->dev, "Failed to request IRQ %d: %d\n",
				wm8962->irq, ret);
			wm8962->irq = 0;
			/* Non-fatal */
		} else {
			/* Enable some IRQs by default */
			regmap_update_bits(wm8962->regmap,
					   WM8962_INTERRUPT_STATUS_2_MASK,
					   WM8962_FLL_LOCK_EINT |
					   WM8962_TEMP_SHUT_EINT |
					   WM8962_FIFOS_ERR_EINT, 0);
		}
	}

	pm_runtime_enable(&i2c->dev);
	pm_request_idle(&i2c->dev);

	ret = devm_snd_soc_register_component(&i2c->dev,
				     &soc_component_dev_wm8962, &wm8962_dai, 1);
	if (ret < 0)
		goto err_pm_runtime;

	regcache_cache_only(wm8962->regmap, true);

	/* The drivers should power up as needed */
	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);

	return 0;

err_pm_runtime:
	pm_runtime_disable(&i2c->dev);
err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies), wm8962->supplies);
err:
	return ret;
}

static int wm8962_i2c_remove(struct i2c_client *client)
{
	pm_runtime_disable(&client->dev);
	return 0;
}

#ifdef CONFIG_PM
static int wm8962_runtime_resume(struct device *dev)
{
	struct wm8962_priv *wm8962 = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(wm8962->pdata.mclk);
	if (ret) {
		dev_err(dev, "Failed to enable MCLK: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8962->supplies),
				    wm8962->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		goto disable_clock;
	}

	regcache_cache_only(wm8962->regmap, false);

	wm8962_reset(wm8962);

	regcache_mark_dirty(wm8962->regmap);

	/* SYSCLK defaults to on; make sure it is off so we can safely
	 * write to registers if the device is declocked.
	 */
	regmap_write_bits(wm8962->regmap, WM8962_CLOCKING2,
			  WM8962_SYSCLK_ENA, 0);

	/* Ensure we have soft control over all registers */
	regmap_update_bits(wm8962->regmap, WM8962_CLOCKING2,
			   WM8962_CLKREG_OVD, WM8962_CLKREG_OVD);

	/* Ensure that the oscillator and PLLs are disabled */
	regmap_update_bits(wm8962->regmap, WM8962_PLL2,
			   WM8962_OSC_ENA | WM8962_PLL2_ENA | WM8962_PLL3_ENA,
			   0);

	regcache_sync(wm8962->regmap);

	regmap_update_bits(wm8962->regmap, WM8962_ANTI_POP,
			   WM8962_STARTUP_BIAS_ENA | WM8962_VMID_BUF_ENA,
			   WM8962_STARTUP_BIAS_ENA | WM8962_VMID_BUF_ENA);

	/* Bias enable at 2*5k (fast start-up) */
	regmap_update_bits(wm8962->regmap, WM8962_PWR_MGMT_1,
			   WM8962_BIAS_ENA | WM8962_VMID_SEL_MASK,
			   WM8962_BIAS_ENA | 0x180);

	msleep(5);

	return 0;

disable_clock:
	clk_disable_unprepare(wm8962->pdata.mclk);
	return ret;
}

static int wm8962_runtime_suspend(struct device *dev)
{
	struct wm8962_priv *wm8962 = dev_get_drvdata(dev);

	regmap_update_bits(wm8962->regmap, WM8962_PWR_MGMT_1,
			   WM8962_VMID_SEL_MASK | WM8962_BIAS_ENA, 0);

	regmap_update_bits(wm8962->regmap, WM8962_ANTI_POP,
			   WM8962_STARTUP_BIAS_ENA |
			   WM8962_VMID_BUF_ENA, 0);

	regcache_cache_only(wm8962->regmap, true);

	regulator_bulk_disable(ARRAY_SIZE(wm8962->supplies),
			       wm8962->supplies);

	clk_disable_unprepare(wm8962->pdata.mclk);

	return 0;
}
#endif

static const struct dev_pm_ops wm8962_pm = {
	SET_RUNTIME_PM_OPS(wm8962_runtime_suspend, wm8962_runtime_resume, NULL)
};

static const struct i2c_device_id wm8962_i2c_id[] = {
	{ "wm8962", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8962_i2c_id);

static const struct of_device_id wm8962_of_match[] = {
	{ .compatible = "wlf,wm8962", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8962_of_match);

static struct i2c_driver wm8962_i2c_driver = {
	.driver = {
		.name = "wm8962",
		.of_match_table = wm8962_of_match,
		.pm = &wm8962_pm,
	},
	.probe =    wm8962_i2c_probe,
	.remove =   wm8962_i2c_remove,
	.id_table = wm8962_i2c_id,
};

module_i2c_driver(wm8962_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8962 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
