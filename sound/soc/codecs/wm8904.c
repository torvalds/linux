/*
 * wm8904.c  --  WM8904 ALSA SoC Audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
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
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/wm8904.h>

#include "wm8904.h"

enum wm8904_type {
	WM8904,
	WM8912,
};

#define WM8904_NUM_DCS_CHANNELS 4

#define WM8904_NUM_SUPPLIES 5
static const char *wm8904_supply_names[WM8904_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD",
	"CPVDD",
	"MICVDD",
};

/* codec private data */
struct wm8904_priv {

	enum wm8904_type devtype;
	void *control_data;

	struct regulator_bulk_data supplies[WM8904_NUM_SUPPLIES];

	struct wm8904_pdata *pdata;

	int deemph;

	/* Platform provided DRC configuration */
	const char **drc_texts;
	int drc_cfg;
	struct soc_enum drc_enum;

	/* Platform provided ReTune mobile configuration */
	int num_retune_mobile_texts;
	const char **retune_mobile_texts;
	int retune_mobile_cfg;
	struct soc_enum retune_mobile_enum;

	/* FLL setup */
	int fll_src;
	int fll_fref;
	int fll_fout;

	/* Clocking configuration */
	unsigned int mclk_rate;
	int sysclk_src;
	unsigned int sysclk_rate;

	int tdm_width;
	int tdm_slots;
	int bclk;
	int fs;

	/* DC servo configuration - cached offset values */
	int dcs_state[WM8904_NUM_DCS_CHANNELS];
};

static const u16 wm8904_reg[WM8904_MAX_REGISTER + 1] = {
	0x8904,     /* R0   - SW Reset and ID */
	0x0000,     /* R1   - Revision */
	0x0000,     /* R2 */
	0x0000,     /* R3 */
	0x0018,     /* R4   - Bias Control 0 */
	0x0000,     /* R5   - VMID Control 0 */
	0x0000,     /* R6   - Mic Bias Control 0 */
	0x0000,     /* R7   - Mic Bias Control 1 */
	0x0001,     /* R8   - Analogue DAC 0 */
	0x9696,     /* R9   - mic Filter Control */
	0x0001,     /* R10  - Analogue ADC 0 */
	0x0000,     /* R11 */
	0x0000,     /* R12  - Power Management 0 */
	0x0000,     /* R13 */
	0x0000,     /* R14  - Power Management 2 */
	0x0000,     /* R15  - Power Management 3 */
	0x0000,     /* R16 */
	0x0000,     /* R17 */
	0x0000,     /* R18  - Power Management 6 */
	0x0000,     /* R19 */
	0x945E,     /* R20  - Clock Rates 0 */
	0x0C05,     /* R21  - Clock Rates 1 */
	0x0006,     /* R22  - Clock Rates 2 */
	0x0000,     /* R23 */
	0x0050,     /* R24  - Audio Interface 0 */
	0x000A,     /* R25  - Audio Interface 1 */
	0x00E4,     /* R26  - Audio Interface 2 */
	0x0040,     /* R27  - Audio Interface 3 */
	0x0000,     /* R28 */
	0x0000,     /* R29 */
	0x00C0,     /* R30  - DAC Digital Volume Left */
	0x00C0,     /* R31  - DAC Digital Volume Right */
	0x0000,     /* R32  - DAC Digital 0 */
	0x0008,     /* R33  - DAC Digital 1 */
	0x0000,     /* R34 */
	0x0000,     /* R35 */
	0x00C0,     /* R36  - ADC Digital Volume Left */
	0x00C0,     /* R37  - ADC Digital Volume Right */
	0x0010,     /* R38  - ADC Digital 0 */
	0x0000,     /* R39  - Digital Microphone 0 */
	0x01AF,     /* R40  - DRC 0 */
	0x3248,     /* R41  - DRC 1 */
	0x0000,     /* R42  - DRC 2 */
	0x0000,     /* R43  - DRC 3 */
	0x0085,     /* R44  - Analogue Left Input 0 */
	0x0085,     /* R45  - Analogue Right Input 0 */
	0x0044,     /* R46  - Analogue Left Input 1 */
	0x0044,     /* R47  - Analogue Right Input 1 */
	0x0000,     /* R48 */
	0x0000,     /* R49 */
	0x0000,     /* R50 */
	0x0000,     /* R51 */
	0x0000,     /* R52 */
	0x0000,     /* R53 */
	0x0000,     /* R54 */
	0x0000,     /* R55 */
	0x0000,     /* R56 */
	0x002D,     /* R57  - Analogue OUT1 Left */
	0x002D,     /* R58  - Analogue OUT1 Right */
	0x0039,     /* R59  - Analogue OUT2 Left */
	0x0039,     /* R60  - Analogue OUT2 Right */
	0x0000,     /* R61  - Analogue OUT12 ZC */
	0x0000,     /* R62 */
	0x0000,     /* R63 */
	0x0000,     /* R64 */
	0x0000,     /* R65 */
	0x0000,     /* R66 */
	0x0000,     /* R67  - DC Servo 0 */
	0x0000,     /* R68  - DC Servo 1 */
	0xAAAA,     /* R69  - DC Servo 2 */
	0x0000,     /* R70 */
	0xAAAA,     /* R71  - DC Servo 4 */
	0xAAAA,     /* R72  - DC Servo 5 */
	0x0000,     /* R73  - DC Servo 6 */
	0x0000,     /* R74  - DC Servo 7 */
	0x0000,     /* R75  - DC Servo 8 */
	0x0000,     /* R76  - DC Servo 9 */
	0x0000,     /* R77  - DC Servo Readback 0 */
	0x0000,     /* R78 */
	0x0000,     /* R79 */
	0x0000,     /* R80 */
	0x0000,     /* R81 */
	0x0000,     /* R82 */
	0x0000,     /* R83 */
	0x0000,     /* R84 */
	0x0000,     /* R85 */
	0x0000,     /* R86 */
	0x0000,     /* R87 */
	0x0000,     /* R88 */
	0x0000,     /* R89 */
	0x0000,     /* R90  - Analogue HP 0 */
	0x0000,     /* R91 */
	0x0000,     /* R92 */
	0x0000,     /* R93 */
	0x0000,     /* R94  - Analogue Lineout 0 */
	0x0000,     /* R95 */
	0x0000,     /* R96 */
	0x0000,     /* R97 */
	0x0000,     /* R98  - Charge Pump 0 */
	0x0000,     /* R99 */
	0x0000,     /* R100 */
	0x0000,     /* R101 */
	0x0000,     /* R102 */
	0x0000,     /* R103 */
	0x0004,     /* R104 - Class W 0 */
	0x0000,     /* R105 */
	0x0000,     /* R106 */
	0x0000,     /* R107 */
	0x0000,     /* R108 - Write Sequencer 0 */
	0x0000,     /* R109 - Write Sequencer 1 */
	0x0000,     /* R110 - Write Sequencer 2 */
	0x0000,     /* R111 - Write Sequencer 3 */
	0x0000,     /* R112 - Write Sequencer 4 */
	0x0000,     /* R113 */
	0x0000,     /* R114 */
	0x0000,     /* R115 */
	0x0000,     /* R116 - FLL Control 1 */
	0x0007,     /* R117 - FLL Control 2 */
	0x0000,     /* R118 - FLL Control 3 */
	0x2EE0,     /* R119 - FLL Control 4 */
	0x0004,     /* R120 - FLL Control 5 */
	0x0014,     /* R121 - GPIO Control 1 */
	0x0010,     /* R122 - GPIO Control 2 */
	0x0010,     /* R123 - GPIO Control 3 */
	0x0000,     /* R124 - GPIO Control 4 */
	0x0000,     /* R125 */
	0x0000,     /* R126 - Digital Pulls */
	0x0000,     /* R127 - Interrupt Status */
	0xFFFF,     /* R128 - Interrupt Status Mask */
	0x0000,     /* R129 - Interrupt Polarity */
	0x0000,     /* R130 - Interrupt Debounce */
	0x0000,     /* R131 */
	0x0000,     /* R132 */
	0x0000,     /* R133 */
	0x0000,     /* R134 - EQ1 */
	0x000C,     /* R135 - EQ2 */
	0x000C,     /* R136 - EQ3 */
	0x000C,     /* R137 - EQ4 */
	0x000C,     /* R138 - EQ5 */
	0x000C,     /* R139 - EQ6 */
	0x0FCA,     /* R140 - EQ7 */
	0x0400,     /* R141 - EQ8 */
	0x00D8,     /* R142 - EQ9 */
	0x1EB5,     /* R143 - EQ10 */
	0xF145,     /* R144 - EQ11 */
	0x0B75,     /* R145 - EQ12 */
	0x01C5,     /* R146 - EQ13 */
	0x1C58,     /* R147 - EQ14 */
	0xF373,     /* R148 - EQ15 */
	0x0A54,     /* R149 - EQ16 */
	0x0558,     /* R150 - EQ17 */
	0x168E,     /* R151 - EQ18 */
	0xF829,     /* R152 - EQ19 */
	0x07AD,     /* R153 - EQ20 */
	0x1103,     /* R154 - EQ21 */
	0x0564,     /* R155 - EQ22 */
	0x0559,     /* R156 - EQ23 */
	0x4000,     /* R157 - EQ24 */
	0x0000,     /* R158 */
	0x0000,     /* R159 */
	0x0000,     /* R160 */
	0x0000,     /* R161 - Control Interface Test 1 */
	0x0000,     /* R162 */
	0x0000,     /* R163 */
	0x0000,     /* R164 */
	0x0000,     /* R165 */
	0x0000,     /* R166 */
	0x0000,     /* R167 */
	0x0000,     /* R168 */
	0x0000,     /* R169 */
	0x0000,     /* R170 */
	0x0000,     /* R171 */
	0x0000,     /* R172 */
	0x0000,     /* R173 */
	0x0000,     /* R174 */
	0x0000,     /* R175 */
	0x0000,     /* R176 */
	0x0000,     /* R177 */
	0x0000,     /* R178 */
	0x0000,     /* R179 */
	0x0000,     /* R180 */
	0x0000,     /* R181 */
	0x0000,     /* R182 */
	0x0000,     /* R183 */
	0x0000,     /* R184 */
	0x0000,     /* R185 */
	0x0000,     /* R186 */
	0x0000,     /* R187 */
	0x0000,     /* R188 */
	0x0000,     /* R189 */
	0x0000,     /* R190 */
	0x0000,     /* R191 */
	0x0000,     /* R192 */
	0x0000,     /* R193 */
	0x0000,     /* R194 */
	0x0000,     /* R195 */
	0x0000,     /* R196 */
	0x0000,     /* R197 */
	0x0000,     /* R198 */
	0x0000,     /* R199 */
	0x0000,     /* R200 */
	0x0000,     /* R201 */
	0x0000,     /* R202 */
	0x0000,     /* R203 */
	0x0000,     /* R204 - Analogue Output Bias 0 */
	0x0000,     /* R205 */
	0x0000,     /* R206 */
	0x0000,     /* R207 */
	0x0000,     /* R208 */
	0x0000,     /* R209 */
	0x0000,     /* R210 */
	0x0000,     /* R211 */
	0x0000,     /* R212 */
	0x0000,     /* R213 */
	0x0000,     /* R214 */
	0x0000,     /* R215 */
	0x0000,     /* R216 */
	0x0000,     /* R217 */
	0x0000,     /* R218 */
	0x0000,     /* R219 */
	0x0000,     /* R220 */
	0x0000,     /* R221 */
	0x0000,     /* R222 */
	0x0000,     /* R223 */
	0x0000,     /* R224 */
	0x0000,     /* R225 */
	0x0000,     /* R226 */
	0x0000,     /* R227 */
	0x0000,     /* R228 */
	0x0000,     /* R229 */
	0x0000,     /* R230 */
	0x0000,     /* R231 */
	0x0000,     /* R232 */
	0x0000,     /* R233 */
	0x0000,     /* R234 */
	0x0000,     /* R235 */
	0x0000,     /* R236 */
	0x0000,     /* R237 */
	0x0000,     /* R238 */
	0x0000,     /* R239 */
	0x0000,     /* R240 */
	0x0000,     /* R241 */
	0x0000,     /* R242 */
	0x0000,     /* R243 */
	0x0000,     /* R244 */
	0x0000,     /* R245 */
	0x0000,     /* R246 */
	0x0000,     /* R247 - FLL NCO Test 0 */
	0x0019,     /* R248 - FLL NCO Test 1 */
};

static struct {
	int readable;
	int writable;
	int vol;
} wm8904_access[] = {
	{ 0xFFFF, 0xFFFF, 1 }, /* R0   - SW Reset and ID */
	{ 0x0000, 0x0000, 0 }, /* R1   - Revision */
	{ 0x0000, 0x0000, 0 }, /* R2 */
	{ 0x0000, 0x0000, 0 }, /* R3 */
	{ 0x001F, 0x001F, 0 }, /* R4   - Bias Control 0 */
	{ 0x0047, 0x0047, 0 }, /* R5   - VMID Control 0 */
	{ 0x007F, 0x007F, 0 }, /* R6   - Mic Bias Control 0 */
	{ 0xC007, 0xC007, 0 }, /* R7   - Mic Bias Control 1 */
	{ 0x001E, 0x001E, 0 }, /* R8   - Analogue DAC 0 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R9   - mic Filter Control */
	{ 0x0001, 0x0001, 0 }, /* R10  - Analogue ADC 0 */
	{ 0x0000, 0x0000, 0 }, /* R11 */
	{ 0x0003, 0x0003, 0 }, /* R12  - Power Management 0 */
	{ 0x0000, 0x0000, 0 }, /* R13 */
	{ 0x0003, 0x0003, 0 }, /* R14  - Power Management 2 */
	{ 0x0003, 0x0003, 0 }, /* R15  - Power Management 3 */
	{ 0x0000, 0x0000, 0 }, /* R16 */
	{ 0x0000, 0x0000, 0 }, /* R17 */
	{ 0x000F, 0x000F, 0 }, /* R18  - Power Management 6 */
	{ 0x0000, 0x0000, 0 }, /* R19 */
	{ 0x7001, 0x7001, 0 }, /* R20  - Clock Rates 0 */
	{ 0x3C07, 0x3C07, 0 }, /* R21  - Clock Rates 1 */
	{ 0xD00F, 0xD00F, 0 }, /* R22  - Clock Rates 2 */
	{ 0x0000, 0x0000, 0 }, /* R23 */
	{ 0x1FFF, 0x1FFF, 0 }, /* R24  - Audio Interface 0 */
	{ 0x3DDF, 0x3DDF, 0 }, /* R25  - Audio Interface 1 */
	{ 0x0F1F, 0x0F1F, 0 }, /* R26  - Audio Interface 2 */
	{ 0x0FFF, 0x0FFF, 0 }, /* R27  - Audio Interface 3 */
	{ 0x0000, 0x0000, 0 }, /* R28 */
	{ 0x0000, 0x0000, 0 }, /* R29 */
	{ 0x00FF, 0x01FF, 0 }, /* R30  - DAC Digital Volume Left */
	{ 0x00FF, 0x01FF, 0 }, /* R31  - DAC Digital Volume Right */
	{ 0x0FFF, 0x0FFF, 0 }, /* R32  - DAC Digital 0 */
	{ 0x1E4E, 0x1E4E, 0 }, /* R33  - DAC Digital 1 */
	{ 0x0000, 0x0000, 0 }, /* R34 */
	{ 0x0000, 0x0000, 0 }, /* R35 */
	{ 0x00FF, 0x01FF, 0 }, /* R36  - ADC Digital Volume Left */
	{ 0x00FF, 0x01FF, 0 }, /* R37  - ADC Digital Volume Right */
	{ 0x0073, 0x0073, 0 }, /* R38  - ADC Digital 0 */
	{ 0x1800, 0x1800, 0 }, /* R39  - Digital Microphone 0 */
	{ 0xDFEF, 0xDFEF, 0 }, /* R40  - DRC 0 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R41  - DRC 1 */
	{ 0x003F, 0x003F, 0 }, /* R42  - DRC 2 */
	{ 0x07FF, 0x07FF, 0 }, /* R43  - DRC 3 */
	{ 0x009F, 0x009F, 0 }, /* R44  - Analogue Left Input 0 */
	{ 0x009F, 0x009F, 0 }, /* R45  - Analogue Right Input 0 */
	{ 0x007F, 0x007F, 0 }, /* R46  - Analogue Left Input 1 */
	{ 0x007F, 0x007F, 0 }, /* R47  - Analogue Right Input 1 */
	{ 0x0000, 0x0000, 0 }, /* R48 */
	{ 0x0000, 0x0000, 0 }, /* R49 */
	{ 0x0000, 0x0000, 0 }, /* R50 */
	{ 0x0000, 0x0000, 0 }, /* R51 */
	{ 0x0000, 0x0000, 0 }, /* R52 */
	{ 0x0000, 0x0000, 0 }, /* R53 */
	{ 0x0000, 0x0000, 0 }, /* R54 */
	{ 0x0000, 0x0000, 0 }, /* R55 */
	{ 0x0000, 0x0000, 0 }, /* R56 */
	{ 0x017F, 0x01FF, 0 }, /* R57  - Analogue OUT1 Left */
	{ 0x017F, 0x01FF, 0 }, /* R58  - Analogue OUT1 Right */
	{ 0x017F, 0x01FF, 0 }, /* R59  - Analogue OUT2 Left */
	{ 0x017F, 0x01FF, 0 }, /* R60  - Analogue OUT2 Right */
	{ 0x000F, 0x000F, 0 }, /* R61  - Analogue OUT12 ZC */
	{ 0x0000, 0x0000, 0 }, /* R62 */
	{ 0x0000, 0x0000, 0 }, /* R63 */
	{ 0x0000, 0x0000, 0 }, /* R64 */
	{ 0x0000, 0x0000, 0 }, /* R65 */
	{ 0x0000, 0x0000, 0 }, /* R66 */
	{ 0x000F, 0x000F, 0 }, /* R67  - DC Servo 0 */
	{ 0xFFFF, 0xFFFF, 1 }, /* R68  - DC Servo 1 */
	{ 0x0F0F, 0x0F0F, 0 }, /* R69  - DC Servo 2 */
	{ 0x0000, 0x0000, 0 }, /* R70 */
	{ 0x007F, 0x007F, 0 }, /* R71  - DC Servo 4 */
	{ 0x007F, 0x007F, 0 }, /* R72  - DC Servo 5 */
	{ 0x00FF, 0x00FF, 1 }, /* R73  - DC Servo 6 */
	{ 0x00FF, 0x00FF, 1 }, /* R74  - DC Servo 7 */
	{ 0x00FF, 0x00FF, 1 }, /* R75  - DC Servo 8 */
	{ 0x00FF, 0x00FF, 1 }, /* R76  - DC Servo 9 */
	{ 0x0FFF, 0x0000, 1 }, /* R77  - DC Servo Readback 0 */
	{ 0x0000, 0x0000, 0 }, /* R78 */
	{ 0x0000, 0x0000, 0 }, /* R79 */
	{ 0x0000, 0x0000, 0 }, /* R80 */
	{ 0x0000, 0x0000, 0 }, /* R81 */
	{ 0x0000, 0x0000, 0 }, /* R82 */
	{ 0x0000, 0x0000, 0 }, /* R83 */
	{ 0x0000, 0x0000, 0 }, /* R84 */
	{ 0x0000, 0x0000, 0 }, /* R85 */
	{ 0x0000, 0x0000, 0 }, /* R86 */
	{ 0x0000, 0x0000, 0 }, /* R87 */
	{ 0x0000, 0x0000, 0 }, /* R88 */
	{ 0x0000, 0x0000, 0 }, /* R89 */
	{ 0x00FF, 0x00FF, 0 }, /* R90  - Analogue HP 0 */
	{ 0x0000, 0x0000, 0 }, /* R91 */
	{ 0x0000, 0x0000, 0 }, /* R92 */
	{ 0x0000, 0x0000, 0 }, /* R93 */
	{ 0x00FF, 0x00FF, 0 }, /* R94  - Analogue Lineout 0 */
	{ 0x0000, 0x0000, 0 }, /* R95 */
	{ 0x0000, 0x0000, 0 }, /* R96 */
	{ 0x0000, 0x0000, 0 }, /* R97 */
	{ 0x0001, 0x0001, 0 }, /* R98  - Charge Pump 0 */
	{ 0x0000, 0x0000, 0 }, /* R99 */
	{ 0x0000, 0x0000, 0 }, /* R100 */
	{ 0x0000, 0x0000, 0 }, /* R101 */
	{ 0x0000, 0x0000, 0 }, /* R102 */
	{ 0x0000, 0x0000, 0 }, /* R103 */
	{ 0x0001, 0x0001, 0 }, /* R104 - Class W 0 */
	{ 0x0000, 0x0000, 0 }, /* R105 */
	{ 0x0000, 0x0000, 0 }, /* R106 */
	{ 0x0000, 0x0000, 0 }, /* R107 */
	{ 0x011F, 0x011F, 0 }, /* R108 - Write Sequencer 0 */
	{ 0x7FFF, 0x7FFF, 0 }, /* R109 - Write Sequencer 1 */
	{ 0x4FFF, 0x4FFF, 0 }, /* R110 - Write Sequencer 2 */
	{ 0x003F, 0x033F, 0 }, /* R111 - Write Sequencer 3 */
	{ 0x03F1, 0x0000, 0 }, /* R112 - Write Sequencer 4 */
	{ 0x0000, 0x0000, 0 }, /* R113 */
	{ 0x0000, 0x0000, 0 }, /* R114 */
	{ 0x0000, 0x0000, 0 }, /* R115 */
	{ 0x0007, 0x0007, 0 }, /* R116 - FLL Control 1 */
	{ 0x3F77, 0x3F77, 0 }, /* R117 - FLL Control 2 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R118 - FLL Control 3 */
	{ 0x7FEF, 0x7FEF, 0 }, /* R119 - FLL Control 4 */
	{ 0x001B, 0x001B, 0 }, /* R120 - FLL Control 5 */
	{ 0x003F, 0x003F, 0 }, /* R121 - GPIO Control 1 */
	{ 0x003F, 0x003F, 0 }, /* R122 - GPIO Control 2 */
	{ 0x003F, 0x003F, 0 }, /* R123 - GPIO Control 3 */
	{ 0x038F, 0x038F, 0 }, /* R124 - GPIO Control 4 */
	{ 0x0000, 0x0000, 0 }, /* R125 */
	{ 0x00FF, 0x00FF, 0 }, /* R126 - Digital Pulls */
	{ 0x07FF, 0x03FF, 1 }, /* R127 - Interrupt Status */
	{ 0x03FF, 0x03FF, 0 }, /* R128 - Interrupt Status Mask */
	{ 0x03FF, 0x03FF, 0 }, /* R129 - Interrupt Polarity */
	{ 0x03FF, 0x03FF, 0 }, /* R130 - Interrupt Debounce */
	{ 0x0000, 0x0000, 0 }, /* R131 */
	{ 0x0000, 0x0000, 0 }, /* R132 */
	{ 0x0000, 0x0000, 0 }, /* R133 */
	{ 0x0001, 0x0001, 0 }, /* R134 - EQ1 */
	{ 0x001F, 0x001F, 0 }, /* R135 - EQ2 */
	{ 0x001F, 0x001F, 0 }, /* R136 - EQ3 */
	{ 0x001F, 0x001F, 0 }, /* R137 - EQ4 */
	{ 0x001F, 0x001F, 0 }, /* R138 - EQ5 */
	{ 0x001F, 0x001F, 0 }, /* R139 - EQ6 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R140 - EQ7 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R141 - EQ8 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R142 - EQ9 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R143 - EQ10 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R144 - EQ11 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R145 - EQ12 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R146 - EQ13 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R147 - EQ14 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R148 - EQ15 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R149 - EQ16 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R150 - EQ17 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R151wm8523_dai - EQ18 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R152 - EQ19 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R153 - EQ20 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R154 - EQ21 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R155 - EQ22 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R156 - EQ23 */
	{ 0xFFFF, 0xFFFF, 0 }, /* R157 - EQ24 */
	{ 0x0000, 0x0000, 0 }, /* R158 */
	{ 0x0000, 0x0000, 0 }, /* R159 */
	{ 0x0000, 0x0000, 0 }, /* R160 */
	{ 0x0002, 0x0002, 0 }, /* R161 - Control Interface Test 1 */
	{ 0x0000, 0x0000, 0 }, /* R162 */
	{ 0x0000, 0x0000, 0 }, /* R163 */
	{ 0x0000, 0x0000, 0 }, /* R164 */
	{ 0x0000, 0x0000, 0 }, /* R165 */
	{ 0x0000, 0x0000, 0 }, /* R166 */
	{ 0x0000, 0x0000, 0 }, /* R167 */
	{ 0x0000, 0x0000, 0 }, /* R168 */
	{ 0x0000, 0x0000, 0 }, /* R169 */
	{ 0x0000, 0x0000, 0 }, /* R170 */
	{ 0x0000, 0x0000, 0 }, /* R171 */
	{ 0x0000, 0x0000, 0 }, /* R172 */
	{ 0x0000, 0x0000, 0 }, /* R173 */
	{ 0x0000, 0x0000, 0 }, /* R174 */
	{ 0x0000, 0x0000, 0 }, /* R175 */
	{ 0x0000, 0x0000, 0 }, /* R176 */
	{ 0x0000, 0x0000, 0 }, /* R177 */
	{ 0x0000, 0x0000, 0 }, /* R178 */
	{ 0x0000, 0x0000, 0 }, /* R179 */
	{ 0x0000, 0x0000, 0 }, /* R180 */
	{ 0x0000, 0x0000, 0 }, /* R181 */
	{ 0x0000, 0x0000, 0 }, /* R182 */
	{ 0x0000, 0x0000, 0 }, /* R183 */
	{ 0x0000, 0x0000, 0 }, /* R184 */
	{ 0x0000, 0x0000, 0 }, /* R185 */
	{ 0x0000, 0x0000, 0 }, /* R186 */
	{ 0x0000, 0x0000, 0 }, /* R187 */
	{ 0x0000, 0x0000, 0 }, /* R188 */
	{ 0x0000, 0x0000, 0 }, /* R189 */
	{ 0x0000, 0x0000, 0 }, /* R190 */
	{ 0x0000, 0x0000, 0 }, /* R191 */
	{ 0x0000, 0x0000, 0 }, /* R192 */
	{ 0x0000, 0x0000, 0 }, /* R193 */
	{ 0x0000, 0x0000, 0 }, /* R194 */
	{ 0x0000, 0x0000, 0 }, /* R195 */
	{ 0x0000, 0x0000, 0 }, /* R196 */
	{ 0x0000, 0x0000, 0 }, /* R197 */
	{ 0x0000, 0x0000, 0 }, /* R198 */
	{ 0x0000, 0x0000, 0 }, /* R199 */
	{ 0x0000, 0x0000, 0 }, /* R200 */
	{ 0x0000, 0x0000, 0 }, /* R201 */
	{ 0x0000, 0x0000, 0 }, /* R202 */
	{ 0x0000, 0x0000, 0 }, /* R203 */
	{ 0x0070, 0x0070, 0 }, /* R204 - Analogue Output Bias 0 */
	{ 0x0000, 0x0000, 0 }, /* R205 */
	{ 0x0000, 0x0000, 0 }, /* R206 */
	{ 0x0000, 0x0000, 0 }, /* R207 */
	{ 0x0000, 0x0000, 0 }, /* R208 */
	{ 0x0000, 0x0000, 0 }, /* R209 */
	{ 0x0000, 0x0000, 0 }, /* R210 */
	{ 0x0000, 0x0000, 0 }, /* R211 */
	{ 0x0000, 0x0000, 0 }, /* R212 */
	{ 0x0000, 0x0000, 0 }, /* R213 */
	{ 0x0000, 0x0000, 0 }, /* R214 */
	{ 0x0000, 0x0000, 0 }, /* R215 */
	{ 0x0000, 0x0000, 0 }, /* R216 */
	{ 0x0000, 0x0000, 0 }, /* R217 */
	{ 0x0000, 0x0000, 0 }, /* R218 */
	{ 0x0000, 0x0000, 0 }, /* R219 */
	{ 0x0000, 0x0000, 0 }, /* R220 */
	{ 0x0000, 0x0000, 0 }, /* R221 */
	{ 0x0000, 0x0000, 0 }, /* R222 */
	{ 0x0000, 0x0000, 0 }, /* R223 */
	{ 0x0000, 0x0000, 0 }, /* R224 */
	{ 0x0000, 0x0000, 0 }, /* R225 */
	{ 0x0000, 0x0000, 0 }, /* R226 */
	{ 0x0000, 0x0000, 0 }, /* R227 */
	{ 0x0000, 0x0000, 0 }, /* R228 */
	{ 0x0000, 0x0000, 0 }, /* R229 */
	{ 0x0000, 0x0000, 0 }, /* R230 */
	{ 0x0000, 0x0000, 0 }, /* R231 */
	{ 0x0000, 0x0000, 0 }, /* R232 */
	{ 0x0000, 0x0000, 0 }, /* R233 */
	{ 0x0000, 0x0000, 0 }, /* R234 */
	{ 0x0000, 0x0000, 0 }, /* R235 */
	{ 0x0000, 0x0000, 0 }, /* R236 */
	{ 0x0000, 0x0000, 0 }, /* R237 */
	{ 0x0000, 0x0000, 0 }, /* R238 */
	{ 0x0000, 0x0000, 0 }, /* R239 */
	{ 0x0000, 0x0000, 0 }, /* R240 */
	{ 0x0000, 0x0000, 0 }, /* R241 */
	{ 0x0000, 0x0000, 0 }, /* R242 */
	{ 0x0000, 0x0000, 0 }, /* R243 */
	{ 0x0000, 0x0000, 0 }, /* R244 */
	{ 0x0000, 0x0000, 0 }, /* R245 */
	{ 0x0000, 0x0000, 0 }, /* R246 */
	{ 0x0001, 0x0001, 0 }, /* R247 - FLL NCO Test 0 */
	{ 0x003F, 0x003F, 0 }, /* R248 - FLL NCO Test 1 */
};

static int wm8904_volatile_register(struct snd_soc_codec *codec, unsigned int reg)
{
	return wm8904_access[reg].vol;
}

static int wm8904_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8904_SW_RESET_AND_ID, 0);
}

static int wm8904_configure_clocking(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	unsigned int clock0, clock2, rate;

	/* Gate the clock while we're updating to avoid misclocking */
	clock2 = snd_soc_read(codec, WM8904_CLOCK_RATES_2);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_SYSCLK_SRC, 0);

	/* This should be done on init() for bypass paths */
	switch (wm8904->sysclk_src) {
	case WM8904_CLK_MCLK:
		dev_dbg(codec->dev, "Using %dHz MCLK\n", wm8904->mclk_rate);

		clock2 &= ~WM8904_SYSCLK_SRC;
		rate = wm8904->mclk_rate;

		/* Ensure the FLL is stopped */
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
				    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);
		break;

	case WM8904_CLK_FLL:
		dev_dbg(codec->dev, "Using %dHz FLL clock\n",
			wm8904->fll_fout);

		clock2 |= WM8904_SYSCLK_SRC;
		rate = wm8904->fll_fout;
		break;

	default:
		dev_err(codec->dev, "System clock not configured\n");
		return -EINVAL;
	}

	/* SYSCLK shouldn't be over 13.5MHz */
	if (rate > 13500000) {
		clock0 = WM8904_MCLK_DIV;
		wm8904->sysclk_rate = rate / 2;
	} else {
		clock0 = 0;
		wm8904->sysclk_rate = rate;
	}

	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_0, WM8904_MCLK_DIV,
			    clock0);

	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_CLK_SYS_ENA | WM8904_SYSCLK_SRC, clock2);

	dev_dbg(codec->dev, "CLK_SYS is %dHz\n", wm8904->sysclk_rate);

	return 0;
}

static void wm8904_set_drc(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int save, i;

	/* Save any enables; the configuration should clear them. */
	save = snd_soc_read(codec, WM8904_DRC_0);

	for (i = 0; i < WM8904_DRC_REGS; i++)
		snd_soc_update_bits(codec, WM8904_DRC_0 + i, 0xffff,
				    pdata->drc_cfgs[wm8904->drc_cfg].regs[i]);

	/* Reenable the DRC */
	snd_soc_update_bits(codec, WM8904_DRC_0,
			    WM8904_DRC_ENA | WM8904_DRC_DAC_PATH, save);
}

static int wm8904_put_drc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int value = ucontrol->value.integer.value[0];

	if (value >= pdata->num_drc_cfgs)
		return -EINVAL;

	wm8904->drc_cfg = value;

	wm8904_set_drc(codec);

	return 0;
}

static int wm8904_get_drc_enum(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8904->drc_cfg;

	return 0;
}

static void wm8904_set_retune_mobile(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int best, best_val, save, i, cfg;

	if (!pdata || !wm8904->num_retune_mobile_texts)
		return;

	/* Find the version of the currently selected configuration
	 * with the nearest sample rate. */
	cfg = wm8904->retune_mobile_cfg;
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		if (strcmp(pdata->retune_mobile_cfgs[i].name,
			   wm8904->retune_mobile_texts[cfg]) == 0 &&
		    abs(pdata->retune_mobile_cfgs[i].rate
			- wm8904->fs) < best_val) {
			best = i;
			best_val = abs(pdata->retune_mobile_cfgs[i].rate
				       - wm8904->fs);
		}
	}

	dev_dbg(codec->dev, "ReTune Mobile %s/%dHz for %dHz sample rate\n",
		pdata->retune_mobile_cfgs[best].name,
		pdata->retune_mobile_cfgs[best].rate,
		wm8904->fs);

	/* The EQ will be disabled while reconfiguring it, remember the
	 * current configuration. 
	 */
	save = snd_soc_read(codec, WM8904_EQ1);

	for (i = 0; i < WM8904_EQ_REGS; i++)
		snd_soc_update_bits(codec, WM8904_EQ1 + i, 0xffff,
				pdata->retune_mobile_cfgs[best].regs[i]);

	snd_soc_update_bits(codec, WM8904_EQ1, WM8904_EQ_ENA, save);
}

static int wm8904_put_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int value = ucontrol->value.integer.value[0];

	if (value >= pdata->num_retune_mobile_cfgs)
		return -EINVAL;

	wm8904->retune_mobile_cfg = value;

	wm8904_set_retune_mobile(codec);

	return 0;
}

static int wm8904_get_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8904->retune_mobile_cfg;

	return 0;
}

static int deemph_settings[] = { 0, 32000, 44100, 48000 };

static int wm8904_set_deemph(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample 
	 * rate.
	 */
	if (wm8904->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(deemph_settings); i++) {
			if (abs(deemph_settings[i] - wm8904->fs) <
			    abs(deemph_settings[best] - wm8904->fs))
				best = i;
		}

		val = best << WM8904_DEEMPH_SHIFT;
	} else {
		val = 0;
	}

	dev_dbg(codec->dev, "Set deemphasis %d\n", val);

	return snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_1,
				   WM8904_DEEMPH_MASK, val);
}

static int wm8904_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8904->deemph;
	return 0;
}

static int wm8904_put_deemph(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int deemph = ucontrol->value.enumerated.item[0];

	if (deemph > 1)
		return -EINVAL;

	wm8904->deemph = deemph;

	return wm8904_set_deemph(codec);
}

static const DECLARE_TLV_DB_SCALE(dac_boost_tlv, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);

static const char *input_mode_text[] = {
	"Single-Ended", "Differential Line", "Differential Mic"
};

static const struct soc_enum lin_mode =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_LEFT_INPUT_1, 0, 3, input_mode_text);

static const struct soc_enum rin_mode =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_RIGHT_INPUT_1, 0, 3, input_mode_text);

static const char *hpf_mode_text[] = {
	"Hi-fi", "Voice 1", "Voice 2", "Voice 3"
};

static const struct soc_enum hpf_mode =
	SOC_ENUM_SINGLE(WM8904_ADC_DIGITAL_0, 5, 4, hpf_mode_text);

static const struct snd_kcontrol_new wm8904_adc_snd_controls[] = {
SOC_DOUBLE_R_TLV("Digital Capture Volume", WM8904_ADC_DIGITAL_VOLUME_LEFT,
		 WM8904_ADC_DIGITAL_VOLUME_RIGHT, 1, 119, 0, digital_tlv),

SOC_ENUM("Left Caputure Mode", lin_mode),
SOC_ENUM("Right Capture Mode", rin_mode),

/* No TLV since it depends on mode */
SOC_DOUBLE_R("Capture Volume", WM8904_ANALOGUE_LEFT_INPUT_0,
	     WM8904_ANALOGUE_RIGHT_INPUT_0, 0, 31, 0),
SOC_DOUBLE_R("Capture Switch", WM8904_ANALOGUE_LEFT_INPUT_0,
	     WM8904_ANALOGUE_RIGHT_INPUT_0, 7, 1, 0),

SOC_SINGLE("High Pass Filter Switch", WM8904_ADC_DIGITAL_0, 4, 1, 0),
SOC_ENUM("High Pass Filter Mode", hpf_mode),

SOC_SINGLE("ADC 128x OSR Switch", WM8904_ANALOGUE_ADC_0, 0, 1, 0),
};

static const char *drc_path_text[] = {
	"ADC", "DAC"
};

static const struct soc_enum drc_path =
	SOC_ENUM_SINGLE(WM8904_DRC_0, 14, 2, drc_path_text);

static const struct snd_kcontrol_new wm8904_dac_snd_controls[] = {
SOC_SINGLE_TLV("Digital Playback Boost Volume", 
	       WM8904_AUDIO_INTERFACE_0, 9, 3, 0, dac_boost_tlv),
SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8904_DAC_DIGITAL_VOLUME_LEFT,
		 WM8904_DAC_DIGITAL_VOLUME_RIGHT, 1, 96, 0, digital_tlv),

SOC_DOUBLE_R_TLV("Headphone Volume", WM8904_ANALOGUE_OUT1_LEFT,
		 WM8904_ANALOGUE_OUT1_RIGHT, 0, 63, 0, out_tlv),
SOC_DOUBLE_R("Headphone Switch", WM8904_ANALOGUE_OUT1_LEFT,
	     WM8904_ANALOGUE_OUT1_RIGHT, 8, 1, 1),
SOC_DOUBLE_R("Headphone ZC Switch", WM8904_ANALOGUE_OUT1_LEFT,
	     WM8904_ANALOGUE_OUT1_RIGHT, 6, 1, 0),

SOC_DOUBLE_R_TLV("Line Output Volume", WM8904_ANALOGUE_OUT2_LEFT,
		 WM8904_ANALOGUE_OUT2_RIGHT, 0, 63, 0, out_tlv),
SOC_DOUBLE_R("Line Output Switch", WM8904_ANALOGUE_OUT2_LEFT,
	     WM8904_ANALOGUE_OUT2_RIGHT, 8, 1, 1),
SOC_DOUBLE_R("Line Output ZC Switch", WM8904_ANALOGUE_OUT2_LEFT,
	     WM8904_ANALOGUE_OUT2_RIGHT, 6, 1, 0),

SOC_SINGLE("EQ Switch", WM8904_EQ1, 0, 1, 0),
SOC_SINGLE("DRC Switch", WM8904_DRC_0, 15, 1, 0),
SOC_ENUM("DRC Path", drc_path),
SOC_SINGLE("DAC OSRx2 Switch", WM8904_DAC_DIGITAL_1, 6, 1, 0),
SOC_SINGLE_BOOL_EXT("DAC Deemphasis Switch", 0,
		    wm8904_get_deemph, wm8904_put_deemph),
};

static const struct snd_kcontrol_new wm8904_snd_controls[] = {
SOC_DOUBLE_TLV("Digital Sidetone Volume", WM8904_DAC_DIGITAL_0, 4, 8, 15, 0,
	       sidetone_tlv),
};

static const struct snd_kcontrol_new wm8904_eq_controls[] = {
SOC_SINGLE_TLV("EQ1 Volume", WM8904_EQ2, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ2 Volume", WM8904_EQ3, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ3 Volume", WM8904_EQ4, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ4 Volume", WM8904_EQ5, 0, 24, 0, eq_tlv),
SOC_SINGLE_TLV("EQ5 Volume", WM8904_EQ6, 0, 24, 0, eq_tlv),
};

static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	BUG_ON(event != SND_SOC_DAPM_POST_PMU);

	/* Maximum startup time */
	udelay(500);

	return 0;
}

static int sysclk_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* If we're using the FLL then we only start it when
		 * required; we assume that the configuration has been
		 * done previously and all we need to do is kick it
		 * off.
		 */
		switch (wm8904->sysclk_src) {
		case WM8904_CLK_FLL:
			snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
					    WM8904_FLL_OSC_ENA,
					    WM8904_FLL_OSC_ENA);

			snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
					    WM8904_FLL_ENA,
					    WM8904_FLL_ENA);
			break;

		default:
			break;
		}
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
				    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);
		break;
	}

	return 0;
}

static int out_pga_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int reg, val;
	int dcs_mask;
	int dcs_l, dcs_r;
	int dcs_l_reg, dcs_r_reg;
	int timeout;
	int pwr_reg;

	/* This code is shared between HP and LINEOUT; we do all our
	 * power management in stereo pairs to avoid latency issues so
	 * we reuse shift to identify which rather than strcmp() the
	 * name. */
	reg = w->shift;

	switch (reg) {
	case WM8904_ANALOGUE_HP_0:
		pwr_reg = WM8904_POWER_MANAGEMENT_2;
		dcs_mask = WM8904_DCS_ENA_CHAN_0 | WM8904_DCS_ENA_CHAN_1;
		dcs_r_reg = WM8904_DC_SERVO_8;
		dcs_l_reg = WM8904_DC_SERVO_9;
		dcs_l = 0;
		dcs_r = 1;
		break;
	case WM8904_ANALOGUE_LINEOUT_0:
		pwr_reg = WM8904_POWER_MANAGEMENT_3;
		dcs_mask = WM8904_DCS_ENA_CHAN_2 | WM8904_DCS_ENA_CHAN_3;
		dcs_r_reg = WM8904_DC_SERVO_6;
		dcs_l_reg = WM8904_DC_SERVO_7;
		dcs_l = 2;
		dcs_r = 3;
		break;
	default:
		BUG();
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* Power on the PGAs */
		snd_soc_update_bits(codec, pwr_reg,
				    WM8904_HPL_PGA_ENA | WM8904_HPR_PGA_ENA,
				    WM8904_HPL_PGA_ENA | WM8904_HPR_PGA_ENA);

		/* Power on the amplifier */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA | WM8904_HPR_ENA,
				    WM8904_HPL_ENA | WM8904_HPR_ENA);


		/* Enable the first stage */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA_DLY | WM8904_HPR_ENA_DLY,
				    WM8904_HPL_ENA_DLY | WM8904_HPR_ENA_DLY);

		/* Power up the DC servo */
		snd_soc_update_bits(codec, WM8904_DC_SERVO_0,
				    dcs_mask, dcs_mask);

		/* Either calibrate the DC servo or restore cached state
		 * if we have that.
		 */
		if (wm8904->dcs_state[dcs_l] || wm8904->dcs_state[dcs_r]) {
			dev_dbg(codec->dev, "Restoring DC servo state\n");

			snd_soc_write(codec, dcs_l_reg,
				      wm8904->dcs_state[dcs_l]);
			snd_soc_write(codec, dcs_r_reg,
				      wm8904->dcs_state[dcs_r]);

			snd_soc_write(codec, WM8904_DC_SERVO_1, dcs_mask);

			timeout = 20;
		} else {
			dev_dbg(codec->dev, "Calibrating DC servo\n");

			snd_soc_write(codec, WM8904_DC_SERVO_1,
				dcs_mask << WM8904_DCS_TRIG_STARTUP_0_SHIFT);

			timeout = 500;
		}

		/* Wait for DC servo to complete */
		dcs_mask <<= WM8904_DCS_CAL_COMPLETE_SHIFT;
		do {
			val = snd_soc_read(codec, WM8904_DC_SERVO_READBACK_0);
			if ((val & dcs_mask) == dcs_mask)
				break;

			msleep(1);
		} while (--timeout);

		if ((val & dcs_mask) != dcs_mask)
			dev_warn(codec->dev, "DC servo timed out\n");
		else
			dev_dbg(codec->dev, "DC servo ready\n");

		/* Enable the output stage */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA_OUTP | WM8904_HPR_ENA_OUTP,
				    WM8904_HPL_ENA_OUTP | WM8904_HPR_ENA_OUTP);
		break;

	case SND_SOC_DAPM_POST_PMU:
		/* Unshort the output itself */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_RMV_SHORT |
				    WM8904_HPR_RMV_SHORT,
				    WM8904_HPL_RMV_SHORT |
				    WM8904_HPR_RMV_SHORT);

		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* Short the output */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_RMV_SHORT |
				    WM8904_HPR_RMV_SHORT, 0);
		break;

	case SND_SOC_DAPM_POST_PMD:
		/* Cache the DC servo configuration; this will be
		 * invalidated if we change the configuration. */
		wm8904->dcs_state[dcs_l] = snd_soc_read(codec, dcs_l_reg);
		wm8904->dcs_state[dcs_r] = snd_soc_read(codec, dcs_r_reg);

		snd_soc_update_bits(codec, WM8904_DC_SERVO_0,
				    dcs_mask, 0);

		/* Disable the amplifier input and output stages */
		snd_soc_update_bits(codec, reg,
				    WM8904_HPL_ENA | WM8904_HPR_ENA |
				    WM8904_HPL_ENA_DLY | WM8904_HPR_ENA_DLY |
				    WM8904_HPL_ENA_OUTP | WM8904_HPR_ENA_OUTP,
				    0);

		/* PGAs too */
		snd_soc_update_bits(codec, pwr_reg,
				    WM8904_HPL_PGA_ENA | WM8904_HPR_PGA_ENA,
				    0);
		break;
	}

	return 0;
}

static const char *lin_text[] = {
	"IN1L", "IN2L", "IN3L"
};

static const struct soc_enum lin_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_LEFT_INPUT_1, 2, 3, lin_text);

static const struct snd_kcontrol_new lin_mux =
	SOC_DAPM_ENUM("Left Capture Mux", lin_enum);

static const struct soc_enum lin_inv_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_LEFT_INPUT_1, 4, 3, lin_text);

static const struct snd_kcontrol_new lin_inv_mux =
	SOC_DAPM_ENUM("Left Capture Inveting Mux", lin_inv_enum);

static const char *rin_text[] = {
	"IN1R", "IN2R", "IN3R"
};

static const struct soc_enum rin_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_RIGHT_INPUT_1, 2, 3, rin_text);

static const struct snd_kcontrol_new rin_mux =
	SOC_DAPM_ENUM("Right Capture Mux", rin_enum);

static const struct soc_enum rin_inv_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_RIGHT_INPUT_1, 4, 3, rin_text);

static const struct snd_kcontrol_new rin_inv_mux =
	SOC_DAPM_ENUM("Right Capture Inveting Mux", rin_inv_enum);

static const char *aif_text[] = {
	"Left", "Right"
};

static const struct soc_enum aifoutl_enum =
	SOC_ENUM_SINGLE(WM8904_AUDIO_INTERFACE_0, 7, 2, aif_text);

static const struct snd_kcontrol_new aifoutl_mux =
	SOC_DAPM_ENUM("AIFOUTL Mux", aifoutl_enum);

static const struct soc_enum aifoutr_enum =
	SOC_ENUM_SINGLE(WM8904_AUDIO_INTERFACE_0, 6, 2, aif_text);

static const struct snd_kcontrol_new aifoutr_mux =
	SOC_DAPM_ENUM("AIFOUTR Mux", aifoutr_enum);

static const struct soc_enum aifinl_enum =
	SOC_ENUM_SINGLE(WM8904_AUDIO_INTERFACE_0, 5, 2, aif_text);

static const struct snd_kcontrol_new aifinl_mux =
	SOC_DAPM_ENUM("AIFINL Mux", aifinl_enum);

static const struct soc_enum aifinr_enum =
	SOC_ENUM_SINGLE(WM8904_AUDIO_INTERFACE_0, 4, 2, aif_text);

static const struct snd_kcontrol_new aifinr_mux =
	SOC_DAPM_ENUM("AIFINR Mux", aifinr_enum);

static const struct snd_soc_dapm_widget wm8904_core_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("SYSCLK", WM8904_CLOCK_RATES_2, 2, 0, sysclk_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("CLK_DSP", WM8904_CLOCK_RATES_2, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("TOCLK", WM8904_CLOCK_RATES_2, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_widget wm8904_adc_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),

SND_SOC_DAPM_MICBIAS("MICBIAS", WM8904_MIC_BIAS_CONTROL_0, 0, 0),

SND_SOC_DAPM_MUX("Left Capture Mux", SND_SOC_NOPM, 0, 0, &lin_mux),
SND_SOC_DAPM_MUX("Left Capture Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &lin_inv_mux),
SND_SOC_DAPM_MUX("Right Capture Mux", SND_SOC_NOPM, 0, 0, &rin_mux),
SND_SOC_DAPM_MUX("Right Capture Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &rin_inv_mux),

SND_SOC_DAPM_PGA("Left Capture PGA", WM8904_POWER_MANAGEMENT_0, 1, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("Right Capture PGA", WM8904_POWER_MANAGEMENT_0, 0, 0,
		 NULL, 0),

SND_SOC_DAPM_ADC("ADCL", NULL, WM8904_POWER_MANAGEMENT_6, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, WM8904_POWER_MANAGEMENT_6, 0, 0),

SND_SOC_DAPM_MUX("AIFOUTL Mux", SND_SOC_NOPM, 0, 0, &aifoutl_mux),
SND_SOC_DAPM_MUX("AIFOUTR Mux", SND_SOC_NOPM, 0, 0, &aifoutr_mux),

SND_SOC_DAPM_AIF_OUT("AIFOUTL", "Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFOUTR", "Capture", 1, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_widget wm8904_dac_dapm_widgets[] = {
SND_SOC_DAPM_AIF_IN("AIFINL", "Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFINR", "Playback", 1, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("DACL Mux", SND_SOC_NOPM, 0, 0, &aifinl_mux),
SND_SOC_DAPM_MUX("DACR Mux", SND_SOC_NOPM, 0, 0, &aifinr_mux),

SND_SOC_DAPM_DAC("DACL", NULL, WM8904_POWER_MANAGEMENT_6, 3, 0),
SND_SOC_DAPM_DAC("DACR", NULL, WM8904_POWER_MANAGEMENT_6, 2, 0),

SND_SOC_DAPM_SUPPLY("Charge pump", WM8904_CHARGE_PUMP_0, 0, 0, cp_event,
		    SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("HPL PGA", SND_SOC_NOPM, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("HPR PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA("LINEL PGA", SND_SOC_NOPM, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("LINER PGA", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("Headphone Output", SND_SOC_NOPM, WM8904_ANALOGUE_HP_0,
		   0, NULL, 0, out_pga_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_PGA_E("Line Output", SND_SOC_NOPM, WM8904_ANALOGUE_LINEOUT_0,
		   0, NULL, 0, out_pga_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
SND_SOC_DAPM_OUTPUT("LINEOUTL"),
SND_SOC_DAPM_OUTPUT("LINEOUTR"),
};

static const char *out_mux_text[] = {
	"DAC", "Bypass"
};

static const struct soc_enum hpl_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_OUT12_ZC, 3, 2, out_mux_text);

static const struct snd_kcontrol_new hpl_mux =
	SOC_DAPM_ENUM("HPL Mux", hpl_enum);

static const struct soc_enum hpr_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_OUT12_ZC, 2, 2, out_mux_text);

static const struct snd_kcontrol_new hpr_mux =
	SOC_DAPM_ENUM("HPR Mux", hpr_enum);

static const struct soc_enum linel_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_OUT12_ZC, 1, 2, out_mux_text);

static const struct snd_kcontrol_new linel_mux =
	SOC_DAPM_ENUM("LINEL Mux", linel_enum);

static const struct soc_enum liner_enum =
	SOC_ENUM_SINGLE(WM8904_ANALOGUE_OUT12_ZC, 0, 2, out_mux_text);

static const struct snd_kcontrol_new liner_mux =
	SOC_DAPM_ENUM("LINEL Mux", liner_enum);

static const char *sidetone_text[] = {
	"None", "Left", "Right"
};

static const struct soc_enum dacl_sidetone_enum =
	SOC_ENUM_SINGLE(WM8904_DAC_DIGITAL_0, 2, 3, sidetone_text);

static const struct snd_kcontrol_new dacl_sidetone_mux =
	SOC_DAPM_ENUM("Left Sidetone Mux", dacl_sidetone_enum);

static const struct soc_enum dacr_sidetone_enum =
	SOC_ENUM_SINGLE(WM8904_DAC_DIGITAL_0, 0, 3, sidetone_text);

static const struct snd_kcontrol_new dacr_sidetone_mux =
	SOC_DAPM_ENUM("Right Sidetone Mux", dacr_sidetone_enum);

static const struct snd_soc_dapm_widget wm8904_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("Class G", WM8904_CLASS_W_0, 0, 1, NULL, 0),
SND_SOC_DAPM_PGA("Left Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Bypass", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("Left Sidetone", SND_SOC_NOPM, 0, 0, &dacl_sidetone_mux),
SND_SOC_DAPM_MUX("Right Sidetone", SND_SOC_NOPM, 0, 0, &dacr_sidetone_mux),

SND_SOC_DAPM_MUX("HPL Mux", SND_SOC_NOPM, 0, 0, &hpl_mux),
SND_SOC_DAPM_MUX("HPR Mux", SND_SOC_NOPM, 0, 0, &hpr_mux),
SND_SOC_DAPM_MUX("LINEL Mux", SND_SOC_NOPM, 0, 0, &linel_mux),
SND_SOC_DAPM_MUX("LINER Mux", SND_SOC_NOPM, 0, 0, &liner_mux),
};

static const struct snd_soc_dapm_route core_intercon[] = {
	{ "CLK_DSP", NULL, "SYSCLK" },
	{ "TOCLK", NULL, "SYSCLK" },
};

static const struct snd_soc_dapm_route adc_intercon[] = {
	{ "Left Capture Mux", "IN1L", "IN1L" },
	{ "Left Capture Mux", "IN2L", "IN2L" },
	{ "Left Capture Mux", "IN3L", "IN3L" },

	{ "Left Capture Inverting Mux", "IN1L", "IN1L" },
	{ "Left Capture Inverting Mux", "IN2L", "IN2L" },
	{ "Left Capture Inverting Mux", "IN3L", "IN3L" },

	{ "Right Capture Mux", "IN1R", "IN1R" },
	{ "Right Capture Mux", "IN2R", "IN2R" },
	{ "Right Capture Mux", "IN3R", "IN3R" },

	{ "Right Capture Inverting Mux", "IN1R", "IN1R" },
	{ "Right Capture Inverting Mux", "IN2R", "IN2R" },
	{ "Right Capture Inverting Mux", "IN3R", "IN3R" },

	{ "Left Capture PGA", NULL, "Left Capture Mux" },
	{ "Left Capture PGA", NULL, "Left Capture Inverting Mux" },

	{ "Right Capture PGA", NULL, "Right Capture Mux" },
	{ "Right Capture PGA", NULL, "Right Capture Inverting Mux" },

	{ "AIFOUTL", "Left",  "ADCL" },
	{ "AIFOUTL", "Right", "ADCR" },
	{ "AIFOUTR", "Left",  "ADCL" },
	{ "AIFOUTR", "Right", "ADCR" },

	{ "ADCL", NULL, "CLK_DSP" },
	{ "ADCL", NULL, "Left Capture PGA" },

	{ "ADCR", NULL, "CLK_DSP" },
	{ "ADCR", NULL, "Right Capture PGA" },
};

static const struct snd_soc_dapm_route dac_intercon[] = {
	{ "DACL", "Right", "AIFINR" },
	{ "DACL", "Left",  "AIFINL" },
	{ "DACL", NULL, "CLK_DSP" },

	{ "DACR", "Right", "AIFINR" },
	{ "DACR", "Left",  "AIFINL" },
	{ "DACR", NULL, "CLK_DSP" },

	{ "Charge pump", NULL, "SYSCLK" },

	{ "Headphone Output", NULL, "HPL PGA" },
	{ "Headphone Output", NULL, "HPR PGA" },
	{ "Headphone Output", NULL, "Charge pump" },
	{ "Headphone Output", NULL, "TOCLK" },

	{ "Line Output", NULL, "LINEL PGA" },
	{ "Line Output", NULL, "LINER PGA" },
	{ "Line Output", NULL, "Charge pump" },
	{ "Line Output", NULL, "TOCLK" },

	{ "HPOUTL", NULL, "Headphone Output" },
	{ "HPOUTR", NULL, "Headphone Output" },

	{ "LINEOUTL", NULL, "Line Output" },
	{ "LINEOUTR", NULL, "Line Output" },
};

static const struct snd_soc_dapm_route wm8904_intercon[] = {
	{ "Left Sidetone", "Left", "ADCL" },
	{ "Left Sidetone", "Right", "ADCR" },
	{ "DACL", NULL, "Left Sidetone" },
	
	{ "Right Sidetone", "Left", "ADCL" },
	{ "Right Sidetone", "Right", "ADCR" },
	{ "DACR", NULL, "Right Sidetone" },

	{ "Left Bypass", NULL, "Class G" },
	{ "Left Bypass", NULL, "Left Capture PGA" },

	{ "Right Bypass", NULL, "Class G" },
	{ "Right Bypass", NULL, "Right Capture PGA" },

	{ "HPL Mux", "DAC", "DACL" },
	{ "HPL Mux", "Bypass", "Left Bypass" },

	{ "HPR Mux", "DAC", "DACR" },
	{ "HPR Mux", "Bypass", "Right Bypass" },

	{ "LINEL Mux", "DAC", "DACL" },
	{ "LINEL Mux", "Bypass", "Left Bypass" },

	{ "LINER Mux", "DAC", "DACR" },
	{ "LINER Mux", "Bypass", "Right Bypass" },

	{ "HPL PGA", NULL, "HPL Mux" },
	{ "HPR PGA", NULL, "HPR Mux" },

	{ "LINEL PGA", NULL, "LINEL Mux" },
	{ "LINER PGA", NULL, "LINER Mux" },
};

static const struct snd_soc_dapm_route wm8912_intercon[] = {
	{ "HPL PGA", NULL, "DACL" },
	{ "HPR PGA", NULL, "DACR" },

	{ "LINEL PGA", NULL, "DACL" },
	{ "LINER PGA", NULL, "DACR" },
};

static int wm8904_add_widgets(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	snd_soc_dapm_new_controls(dapm, wm8904_core_dapm_widgets,
				  ARRAY_SIZE(wm8904_core_dapm_widgets));
	snd_soc_dapm_add_routes(dapm, core_intercon,
				ARRAY_SIZE(core_intercon));

	switch (wm8904->devtype) {
	case WM8904:
		snd_soc_add_controls(codec, wm8904_adc_snd_controls,
				     ARRAY_SIZE(wm8904_adc_snd_controls));
		snd_soc_add_controls(codec, wm8904_dac_snd_controls,
				     ARRAY_SIZE(wm8904_dac_snd_controls));
		snd_soc_add_controls(codec, wm8904_snd_controls,
				     ARRAY_SIZE(wm8904_snd_controls));

		snd_soc_dapm_new_controls(dapm, wm8904_adc_dapm_widgets,
					  ARRAY_SIZE(wm8904_adc_dapm_widgets));
		snd_soc_dapm_new_controls(dapm, wm8904_dac_dapm_widgets,
					  ARRAY_SIZE(wm8904_dac_dapm_widgets));
		snd_soc_dapm_new_controls(dapm, wm8904_dapm_widgets,
					  ARRAY_SIZE(wm8904_dapm_widgets));

		snd_soc_dapm_add_routes(dapm, core_intercon,
					ARRAY_SIZE(core_intercon));
		snd_soc_dapm_add_routes(dapm, adc_intercon,
					ARRAY_SIZE(adc_intercon));
		snd_soc_dapm_add_routes(dapm, dac_intercon,
					ARRAY_SIZE(dac_intercon));
		snd_soc_dapm_add_routes(dapm, wm8904_intercon,
					ARRAY_SIZE(wm8904_intercon));
		break;

	case WM8912:
		snd_soc_add_controls(codec, wm8904_dac_snd_controls,
				     ARRAY_SIZE(wm8904_dac_snd_controls));

		snd_soc_dapm_new_controls(dapm, wm8904_dac_dapm_widgets,
					  ARRAY_SIZE(wm8904_dac_dapm_widgets));

		snd_soc_dapm_add_routes(dapm, dac_intercon,
					ARRAY_SIZE(dac_intercon));
		snd_soc_dapm_add_routes(dapm, wm8912_intercon,
					ARRAY_SIZE(wm8912_intercon));
		break;
	}

	snd_soc_dapm_new_widgets(dapm);
	return 0;
}

static struct {
	int ratio;
	unsigned int clk_sys_rate;
} clk_sys_rates[] = {
	{   64,  0 },
	{  128,  1 },
	{  192,  2 },
	{  256,  3 },
	{  384,  4 },
	{  512,  5 },
	{  786,  6 },
	{ 1024,  7 },
	{ 1408,  8 },
	{ 1536,  9 },
};

static struct {
	int rate;
	int sample_rate;
} sample_rates[] = {
	{ 8000,  0  },
	{ 11025, 1  },
	{ 12000, 1  },
	{ 16000, 2  },
	{ 22050, 3  },
	{ 24000, 3  },
	{ 32000, 4  },
	{ 44100, 5  },
	{ 48000, 5  },
};

static struct {
	int div; /* *10 due to .5s */
	int bclk_div;
} bclk_divs[] = {
	{ 10,  0  },
	{ 15,  1  },
	{ 20,  2  },
	{ 30,  3  },
	{ 40,  4  },
	{ 50,  5  },
	{ 55,  6  },
	{ 60,  7  },
	{ 80,  8  },
	{ 100, 9  },
	{ 110, 10 },
	{ 120, 11 },
	{ 160, 12 },
	{ 200, 13 },
	{ 220, 14 },
	{ 240, 16 },
	{ 200, 17 },
	{ 320, 18 },
	{ 440, 19 },
	{ 480, 20 },
};


static int wm8904_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int ret, i, best, best_val, cur_val;
	unsigned int aif1 = 0;
	unsigned int aif2 = 0;
	unsigned int aif3 = 0;
	unsigned int clock1 = 0;
	unsigned int dac_digital1 = 0;

	/* What BCLK do we need? */
	wm8904->fs = params_rate(params);
	if (wm8904->tdm_slots) {
		dev_dbg(codec->dev, "Configuring for %d %d bit TDM slots\n",
			wm8904->tdm_slots, wm8904->tdm_width);
		wm8904->bclk = snd_soc_calc_bclk(wm8904->fs,
						 wm8904->tdm_width, 2,
						 wm8904->tdm_slots);
	} else {
		wm8904->bclk = snd_soc_params_to_bclk(params);
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		aif1 |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		aif1 |= 0x80;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		aif1 |= 0xc0;
		break;
	default:
		return -EINVAL;
	}


	dev_dbg(codec->dev, "Target BCLK is %dHz\n", wm8904->bclk);

	ret = wm8904_configure_clocking(codec);
	if (ret != 0)
		return ret;

	/* Select nearest CLK_SYS_RATE */
	best = 0;
	best_val = abs((wm8904->sysclk_rate / clk_sys_rates[0].ratio)
		       - wm8904->fs);
	for (i = 1; i < ARRAY_SIZE(clk_sys_rates); i++) {
		cur_val = abs((wm8904->sysclk_rate /
			       clk_sys_rates[i].ratio) - wm8904->fs);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(codec->dev, "Selected CLK_SYS_RATIO of %d\n",
		clk_sys_rates[best].ratio);
	clock1 |= (clk_sys_rates[best].clk_sys_rate
		   << WM8904_CLK_SYS_RATE_SHIFT);

	/* SAMPLE_RATE */
	best = 0;
	best_val = abs(wm8904->fs - sample_rates[0].rate);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		/* Closest match */
		cur_val = abs(wm8904->fs - sample_rates[i].rate);
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	dev_dbg(codec->dev, "Selected SAMPLE_RATE of %dHz\n",
		sample_rates[best].rate);
	clock1 |= (sample_rates[best].sample_rate
		   << WM8904_SAMPLE_RATE_SHIFT);

	/* Enable sloping stopband filter for low sample rates */
	if (wm8904->fs <= 24000)
		dac_digital1 |= WM8904_DAC_SB_FILT;

	/* BCLK_DIV */
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
		cur_val = ((wm8904->sysclk_rate * 10) / bclk_divs[i].div)
			- wm8904->bclk;
		if (cur_val < 0) /* Table is sorted */
			break;
		if (cur_val < best_val) {
			best = i;
			best_val = cur_val;
		}
	}
	wm8904->bclk = (wm8904->sysclk_rate * 10) / bclk_divs[best].div;
	dev_dbg(codec->dev, "Selected BCLK_DIV of %d for %dHz BCLK\n",
		bclk_divs[best].div, wm8904->bclk);
	aif2 |= bclk_divs[best].bclk_div;

	/* LRCLK is a simple fraction of BCLK */
	dev_dbg(codec->dev, "LRCLK_RATE is %d\n", wm8904->bclk / wm8904->fs);
	aif3 |= wm8904->bclk / wm8904->fs;

	/* Apply the settings */
	snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_1,
			    WM8904_DAC_SB_FILT, dac_digital1);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_1,
			    WM8904_AIF_WL_MASK, aif1);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_2,
			    WM8904_BCLK_DIV_MASK, aif2);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_3,
			    WM8904_LRCLK_RATE_MASK, aif3);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_1,
			    WM8904_SAMPLE_RATE_MASK |
			    WM8904_CLK_SYS_RATE_MASK, clock1);

	/* Update filters for the new settings */
	wm8904_set_retune_mobile(codec);
	wm8904_set_deemph(codec);

	return 0;
}


static int wm8904_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			     unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *priv = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case WM8904_CLK_MCLK:
		priv->sysclk_src = clk_id;
		priv->mclk_rate = freq;
		break;

	case WM8904_CLK_FLL:
		priv->sysclk_src = clk_id;
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(dai->dev, "Clock source is %d at %uHz\n", clk_id, freq);

	wm8904_configure_clocking(codec);

	return 0;
}

static int wm8904_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int aif1 = 0;
	unsigned int aif3 = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif3 |= WM8904_LRCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif1 |= WM8904_BCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif1 |= WM8904_BCLK_DIR;
		aif3 |= WM8904_LRCLK_DIR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= WM8904_AIF_LRCLK_INV;
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= 0x3;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif1 |= 0x1;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8904_AIF_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;

	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8904_AIF_BCLK_INV | WM8904_AIF_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8904_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 |= WM8904_AIF_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_1,
			    WM8904_AIF_BCLK_INV | WM8904_AIF_LRCLK_INV |
			    WM8904_AIF_FMT_MASK | WM8904_BCLK_DIR, aif1);
	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_3,
			    WM8904_LRCLK_DIR, aif3);

	return 0;
}


static int wm8904_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int aif1 = 0;

	/* Don't need to validate anything if we're turning off TDM */
	if (slots == 0)
		goto out;

	/* Note that we allow configurations we can't handle ourselves - 
	 * for example, we can generate clocks for slots 2 and up even if
	 * we can't use those slots ourselves.
	 */
	aif1 |= WM8904_AIFADC_TDM | WM8904_AIFDAC_TDM;

	switch (rx_mask) {
	case 3:
		break;
	case 0xc:
		aif1 |= WM8904_AIFADC_TDM_CHAN;
		break;
	default:
		return -EINVAL;
	}


	switch (tx_mask) {
	case 3:
		break;
	case 0xc:
		aif1 |= WM8904_AIFDAC_TDM_CHAN;
		break;
	default:
		return -EINVAL;
	}

out:
	wm8904->tdm_width = slot_width;
	wm8904->tdm_slots = slots / 2;

	snd_soc_update_bits(codec, WM8904_AUDIO_INTERFACE_1,
			    WM8904_AIFADC_TDM | WM8904_AIFADC_TDM_CHAN |
			    WM8904_AIFDAC_TDM | WM8904_AIFDAC_TDM_CHAN, aif1);

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_clk_ref_div;
	u16 n;
	u16 k;
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
	u64 Kpart;
	unsigned int K, Ndiv, Nmod, target;
	unsigned int div;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_clk_ref_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_clk_ref_div++;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 4;
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

	pr_debug("Fvco=%dHz\n", target);

	/* Find an appropraite FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			target /= fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	/* Now, calculate N.K */
	Ndiv = target / Fref;

	fll_div->n = Ndiv;
	Nmod = target % Fref;
	pr_debug("Nmod=%d\n", Nmod);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, Fref);

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	fll_div->k = K / 10;

	pr_debug("N=%x K=%x FLL_FRATIO=%x FLL_OUTDIV=%x FLL_CLK_REF_DIV=%x\n",
		 fll_div->n, fll_div->k,
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_clk_ref_div);

	return 0;
}

static int wm8904_set_fll(struct snd_soc_dai *dai, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct _fll_div fll_div;
	int ret, val;
	int clock2, fll1;

	/* Any change? */
	if (source == wm8904->fll_src && Fref == wm8904->fll_fref &&
	    Fout == wm8904->fll_fout)
		return 0;

	clock2 = snd_soc_read(codec, WM8904_CLOCK_RATES_2);

	if (Fout == 0) {
		dev_dbg(codec->dev, "FLL disabled\n");

		wm8904->fll_fref = 0;
		wm8904->fll_fout = 0;

		/* Gate SYSCLK to avoid glitches */
		snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
				    WM8904_CLK_SYS_ENA, 0);

		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
				    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);

		goto out;
	}

	/* Validate the FLL ID */
	switch (source) {
	case WM8904_FLL_MCLK:
	case WM8904_FLL_LRCLK:
	case WM8904_FLL_BCLK:
		ret = fll_factors(&fll_div, Fref, Fout);
		if (ret != 0)
			return ret;
		break;

	case WM8904_FLL_FREE_RUNNING:
		dev_dbg(codec->dev, "Using free running FLL\n");
		/* Force 12MHz and output/4 for now */
		Fout = 12000000;
		Fref = 12000000;

		memset(&fll_div, 0, sizeof(fll_div));
		fll_div.fll_outdiv = 3;
		break;

	default:
		dev_err(codec->dev, "Unknown FLL ID %d\n", fll_id);
		return -EINVAL;
	}

	/* Save current state then disable the FLL and SYSCLK to avoid
	 * misclocking */
	fll1 = snd_soc_read(codec, WM8904_FLL_CONTROL_1);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_CLK_SYS_ENA, 0);
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_OSC_ENA | WM8904_FLL_ENA, 0);

	/* Unlock forced oscilator control to switch it on/off */
	snd_soc_update_bits(codec, WM8904_CONTROL_INTERFACE_TEST_1,
			    WM8904_USER_KEY, WM8904_USER_KEY);

	if (fll_id == WM8904_FLL_FREE_RUNNING) {
		val = WM8904_FLL_FRC_NCO;
	} else {
		val = 0;
	}

	snd_soc_update_bits(codec, WM8904_FLL_NCO_TEST_1, WM8904_FLL_FRC_NCO,
			    val);
	snd_soc_update_bits(codec, WM8904_CONTROL_INTERFACE_TEST_1,
			    WM8904_USER_KEY, 0);

	switch (fll_id) {
	case WM8904_FLL_MCLK:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
				    WM8904_FLL_CLK_REF_SRC_MASK, 0);
		break;

	case WM8904_FLL_LRCLK:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
				    WM8904_FLL_CLK_REF_SRC_MASK, 1);
		break;

	case WM8904_FLL_BCLK:
		snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
				    WM8904_FLL_CLK_REF_SRC_MASK, 2);
		break;
	}

	if (fll_div.k)
		val = WM8904_FLL_FRACN_ENA;
	else
		val = 0;
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_FRACN_ENA, val);

	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_2,
			    WM8904_FLL_OUTDIV_MASK | WM8904_FLL_FRATIO_MASK,
			    (fll_div.fll_outdiv << WM8904_FLL_OUTDIV_SHIFT) |
			    (fll_div.fll_fratio << WM8904_FLL_FRATIO_SHIFT));

	snd_soc_write(codec, WM8904_FLL_CONTROL_3, fll_div.k);

	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_4, WM8904_FLL_N_MASK,
			    fll_div.n << WM8904_FLL_N_SHIFT);

	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_5,
			    WM8904_FLL_CLK_REF_DIV_MASK,
			    fll_div.fll_clk_ref_div 
			    << WM8904_FLL_CLK_REF_DIV_SHIFT);

	dev_dbg(codec->dev, "FLL configured for %dHz->%dHz\n", Fref, Fout);

	wm8904->fll_fref = Fref;
	wm8904->fll_fout = Fout;
	wm8904->fll_src = source;

	/* Enable the FLL if it was previously active */
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_OSC_ENA, fll1);
	snd_soc_update_bits(codec, WM8904_FLL_CONTROL_1,
			    WM8904_FLL_ENA, fll1);

out:
	/* Reenable SYSCLK if it was previously active */
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_2,
			    WM8904_CLK_SYS_ENA, clock2);

	return 0;
}

static int wm8904_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int val;

	if (mute)
		val = WM8904_DAC_MUTE;
	else
		val = 0;

	snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_1, WM8904_DAC_MUTE, val);

	return 0;
}

static void wm8904_sync_cache(struct snd_soc_codec *codec)
{
	u16 *reg_cache = codec->reg_cache;
	int i;

	if (!codec->cache_sync)
		return;

	codec->cache_only = 0;

	/* Sync back cached values if they're different from the
	 * hardware default.
	 */
	for (i = 1; i < codec->driver->reg_cache_size; i++) {
		if (!wm8904_access[i].writable)
			continue;

		if (reg_cache[i] == wm8904_reg[i])
			continue;

		snd_soc_write(codec, i, reg_cache[i]);
	}

	codec->cache_sync = 0;
}

static int wm8904_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID resistance 2*50k */
		snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
				    WM8904_VMID_RES_MASK,
				    0x1 << WM8904_VMID_RES_SHIFT);

		/* Normal bias current */
		snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
				    WM8904_ISEL_MASK, 2 << WM8904_ISEL_SHIFT);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8904->supplies),
						    wm8904->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			wm8904_sync_cache(codec);

			/* Enable bias */
			snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
					    WM8904_BIAS_ENA, WM8904_BIAS_ENA);

			/* Enable VMID, VMID buffering, 2*5k resistance */
			snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
					    WM8904_VMID_ENA |
					    WM8904_VMID_RES_MASK,
					    WM8904_VMID_ENA |
					    0x3 << WM8904_VMID_RES_SHIFT);

			/* Let VMID ramp */
			msleep(1);
		}

		/* Maintain VMID with 2*250k */
		snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
				    WM8904_VMID_RES_MASK,
				    0x2 << WM8904_VMID_RES_SHIFT);

		/* Bias current *0.5 */
		snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
				    WM8904_ISEL_MASK, 0);
		break;

	case SND_SOC_BIAS_OFF:
		/* Turn off VMID */
		snd_soc_update_bits(codec, WM8904_VMID_CONTROL_0,
				    WM8904_VMID_RES_MASK | WM8904_VMID_ENA, 0);

		/* Stop bias generation */
		snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
				    WM8904_BIAS_ENA, 0);

#ifdef CONFIG_REGULATOR
		/* Post 2.6.34 we will be able to get a callback when
		 * the regulators are disabled which we can use but
		 * for now just assume that the power will be cut if
		 * the regulator API is in use.
		 */
		codec->cache_sync = 1;
#endif

		regulator_bulk_disable(ARRAY_SIZE(wm8904->supplies),
				       wm8904->supplies);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define WM8904_RATES SNDRV_PCM_RATE_8000_96000

#define WM8904_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8904_dai_ops = {
	.set_sysclk = wm8904_set_sysclk,
	.set_fmt = wm8904_set_fmt,
	.set_tdm_slot = wm8904_set_tdm_slot,
	.set_pll = wm8904_set_fll,
	.hw_params = wm8904_hw_params,
	.digital_mute = wm8904_digital_mute,
};

static struct snd_soc_dai_driver wm8904_dai = {
	.name = "wm8904-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8904_RATES,
		.formats = WM8904_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8904_RATES,
		.formats = WM8904_FORMATS,
	},
	.ops = &wm8904_dai_ops,
	.symmetric_rates = 1,
};

#ifdef CONFIG_PM
static int wm8904_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	wm8904_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8904_resume(struct snd_soc_codec *codec)
{
	wm8904_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define wm8904_suspend NULL
#define wm8904_resume NULL
#endif

static void wm8904_handle_retune_mobile_pdata(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	struct snd_kcontrol_new control =
		SOC_ENUM_EXT("EQ Mode",
			     wm8904->retune_mobile_enum,
			     wm8904_get_retune_mobile_enum,
			     wm8904_put_retune_mobile_enum);
	int ret, i, j;
	const char **t;

	/* We need an array of texts for the enum API but the number
	 * of texts is likely to be less than the number of
	 * configurations due to the sample rate dependency of the
	 * configurations. */
	wm8904->num_retune_mobile_texts = 0;
	wm8904->retune_mobile_texts = NULL;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		for (j = 0; j < wm8904->num_retune_mobile_texts; j++) {
			if (strcmp(pdata->retune_mobile_cfgs[i].name,
				   wm8904->retune_mobile_texts[j]) == 0)
				break;
		}

		if (j != wm8904->num_retune_mobile_texts)
			continue;

		/* Expand the array... */
		t = krealloc(wm8904->retune_mobile_texts,
			     sizeof(char *) * 
			     (wm8904->num_retune_mobile_texts + 1),
			     GFP_KERNEL);
		if (t == NULL)
			continue;

		/* ...store the new entry... */
		t[wm8904->num_retune_mobile_texts] = 
			pdata->retune_mobile_cfgs[i].name;

		/* ...and remember the new version. */
		wm8904->num_retune_mobile_texts++;
		wm8904->retune_mobile_texts = t;
	}

	dev_dbg(codec->dev, "Allocated %d unique ReTune Mobile names\n",
		wm8904->num_retune_mobile_texts);

	wm8904->retune_mobile_enum.max = wm8904->num_retune_mobile_texts;
	wm8904->retune_mobile_enum.texts = wm8904->retune_mobile_texts;

	ret = snd_soc_add_controls(codec, &control, 1);
	if (ret != 0)
		dev_err(codec->dev,
			"Failed to add ReTune Mobile control: %d\n", ret);
}

static void wm8904_handle_pdata(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	int ret, i;

	if (!pdata) {
		snd_soc_add_controls(codec, wm8904_eq_controls,
				     ARRAY_SIZE(wm8904_eq_controls));
		return;
	}

	dev_dbg(codec->dev, "%d DRC configurations\n", pdata->num_drc_cfgs);

	if (pdata->num_drc_cfgs) {
		struct snd_kcontrol_new control =
			SOC_ENUM_EXT("DRC Mode", wm8904->drc_enum,
				     wm8904_get_drc_enum, wm8904_put_drc_enum);

		/* We need an array of texts for the enum API */
		wm8904->drc_texts = kmalloc(sizeof(char *)
					    * pdata->num_drc_cfgs, GFP_KERNEL);
		if (!wm8904->drc_texts) {
			dev_err(codec->dev,
				"Failed to allocate %d DRC config texts\n",
				pdata->num_drc_cfgs);
			return;
		}

		for (i = 0; i < pdata->num_drc_cfgs; i++)
			wm8904->drc_texts[i] = pdata->drc_cfgs[i].name;

		wm8904->drc_enum.max = pdata->num_drc_cfgs;
		wm8904->drc_enum.texts = wm8904->drc_texts;

		ret = snd_soc_add_controls(codec, &control, 1);
		if (ret != 0)
			dev_err(codec->dev,
				"Failed to add DRC mode control: %d\n", ret);

		wm8904_set_drc(codec);
	}

	dev_dbg(codec->dev, "%d ReTune Mobile configurations\n",
		pdata->num_retune_mobile_cfgs);

	if (pdata->num_retune_mobile_cfgs)
		wm8904_handle_retune_mobile_pdata(codec);
	else
		snd_soc_add_controls(codec, wm8904_eq_controls,
				     ARRAY_SIZE(wm8904_eq_controls));
}


static int wm8904_probe(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);
	struct wm8904_pdata *pdata = wm8904->pdata;
	u16 *reg_cache = codec->reg_cache;
	int ret, i;

	codec->cache_sync = 1;
	codec->dapm.idle_bias_off = 1;

	switch (wm8904->devtype) {
	case WM8904:
		break;
	case WM8912:
		memset(&wm8904_dai.capture, 0, sizeof(wm8904_dai.capture));
		break;
	default:
		dev_err(codec->dev, "Unknown device type %d\n",
			wm8904->devtype);
		return -EINVAL;
	}

	ret = snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_I2C);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8904->supplies); i++)
		wm8904->supplies[i].supply = wm8904_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8904->supplies),
				 wm8904->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8904->supplies),
				    wm8904->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_get;
	}

	ret = snd_soc_read(codec, WM8904_SW_RESET_AND_ID);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (ret != wm8904_reg[WM8904_SW_RESET_AND_ID]) {
		dev_err(codec->dev, "Device is not a WM8904, ID is %x\n", ret);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = snd_soc_read(codec, WM8904_REVISION);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_enable;
	}
	dev_info(codec->dev, "revision %c\n", ret + 'A');

	ret = wm8904_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err_enable;
	}

	/* Change some default settings - latch VU and enable ZC */
	snd_soc_update_bits(codec, WM8904_ADC_DIGITAL_VOLUME_LEFT,
			    WM8904_ADC_VU, WM8904_ADC_VU);
	snd_soc_update_bits(codec, WM8904_ADC_DIGITAL_VOLUME_RIGHT,
			    WM8904_ADC_VU, WM8904_ADC_VU);
	snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_VOLUME_LEFT,
			    WM8904_DAC_VU, WM8904_DAC_VU);
	snd_soc_update_bits(codec, WM8904_DAC_DIGITAL_VOLUME_RIGHT,
			    WM8904_DAC_VU, WM8904_DAC_VU);
	snd_soc_update_bits(codec, WM8904_ANALOGUE_OUT1_LEFT,
			    WM8904_HPOUT_VU | WM8904_HPOUTLZC,
			    WM8904_HPOUT_VU | WM8904_HPOUTLZC);
	snd_soc_update_bits(codec, WM8904_ANALOGUE_OUT1_RIGHT,
			    WM8904_HPOUT_VU | WM8904_HPOUTRZC,
			    WM8904_HPOUT_VU | WM8904_HPOUTRZC);
	snd_soc_update_bits(codec, WM8904_ANALOGUE_OUT2_LEFT,
			    WM8904_LINEOUT_VU | WM8904_LINEOUTLZC,
			    WM8904_LINEOUT_VU | WM8904_LINEOUTLZC);
	snd_soc_update_bits(codec, WM8904_ANALOGUE_OUT2_RIGHT,
			    WM8904_LINEOUT_VU | WM8904_LINEOUTRZC,
			    WM8904_LINEOUT_VU | WM8904_LINEOUTRZC);
	snd_soc_update_bits(codec, WM8904_CLOCK_RATES_0,
			    WM8904_SR_MODE, 0);

	/* Apply configuration from the platform data. */
	if (wm8904->pdata) {
		for (i = 0; i < WM8904_GPIO_REGS; i++) {
			if (!pdata->gpio_cfg[i])
				continue;

			reg_cache[WM8904_GPIO_CONTROL_1 + i]
				= pdata->gpio_cfg[i] & 0xffff;
		}

		/* Zero is the default value for these anyway */
		for (i = 0; i < WM8904_MIC_REGS; i++)
			reg_cache[WM8904_MIC_BIAS_CONTROL_0 + i]
				= pdata->mic_cfg[i];
	}

	/* Set Class W by default - this will be managed by the Class
	 * G widget at runtime where bypass paths are available.
	 */
	snd_soc_update_bits(codec, WM8904_CLASS_W_0,
			    WM8904_CP_DYN_PWR, WM8904_CP_DYN_PWR);

	/* Use normal bias source */
	snd_soc_update_bits(codec, WM8904_BIAS_CONTROL_0,
			    WM8904_POBCTRL, 0);

	wm8904_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Bias level configuration will have done an extra enable */
	regulator_bulk_disable(ARRAY_SIZE(wm8904->supplies), wm8904->supplies);

	wm8904_handle_pdata(codec);

	wm8904_add_widgets(codec);

	return 0;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8904->supplies), wm8904->supplies);
err_get:
	regulator_bulk_free(ARRAY_SIZE(wm8904->supplies), wm8904->supplies);
	return ret;
}

static int wm8904_remove(struct snd_soc_codec *codec)
{
	struct wm8904_priv *wm8904 = snd_soc_codec_get_drvdata(codec);

	wm8904_set_bias_level(codec, SND_SOC_BIAS_OFF);
	regulator_bulk_free(ARRAY_SIZE(wm8904->supplies), wm8904->supplies);
	kfree(wm8904->retune_mobile_texts);
	kfree(wm8904->drc_texts);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8904 = {
	.probe =	wm8904_probe,
	.remove =	wm8904_remove,
	.suspend =	wm8904_suspend,
	.resume =	wm8904_resume,
	.set_bias_level = wm8904_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(wm8904_reg),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8904_reg,
	.volatile_register = wm8904_volatile_register,
};

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8904_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8904_priv *wm8904;
	int ret;

	wm8904 = kzalloc(sizeof(struct wm8904_priv), GFP_KERNEL);
	if (wm8904 == NULL)
		return -ENOMEM;

	wm8904->devtype = id->driver_data;
	i2c_set_clientdata(i2c, wm8904);
	wm8904->control_data = i2c;
	wm8904->pdata = i2c->dev.platform_data;

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8904, &wm8904_dai, 1);
	if (ret < 0)
		kfree(wm8904);
	return ret;
}

static __devexit int wm8904_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8904_i2c_id[] = {
	{ "wm8904", WM8904 },
	{ "wm8912", WM8912 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8904_i2c_id);

static struct i2c_driver wm8904_i2c_driver = {
	.driver = {
		.name = "wm8904-codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8904_i2c_probe,
	.remove =   __devexit_p(wm8904_i2c_remove),
	.id_table = wm8904_i2c_id,
};
#endif

static int __init wm8904_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8904_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8904 I2C driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8904_modinit);

static void __exit wm8904_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8904_i2c_driver);
#endif
}
module_exit(wm8904_exit);

MODULE_DESCRIPTION("ASoC WM8904 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
