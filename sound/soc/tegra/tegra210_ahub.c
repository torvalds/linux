// SPDX-License-Identifier: GPL-2.0-only
//
// tegra210_ahub.c - Tegra210 AHUB driver
//
// Copyright (c) 2020-2024, NVIDIA CORPORATION.  All rights reserved.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "tegra210_ahub.h"

static int tegra_ahub_get_value_enum(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_value *uctl)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_kcontrol_component(kctl);
	struct tegra_ahub *ahub = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *e = (struct soc_enum *)kctl->private_value;
	unsigned int reg, i, bit_pos = 0;

	/*
	 * Find the bit position of current MUX input.
	 * If nothing is set, position would be 0 and it corresponds to 'None'.
	 */
	for (i = 0; i < ahub->soc_data->reg_count; i++) {
		unsigned int reg_val;

		reg = e->reg + (TEGRA210_XBAR_PART1_RX * i);
		reg_val = snd_soc_component_read(cmpnt, reg);
		reg_val &= ahub->soc_data->mask[i];

		if (reg_val) {
			bit_pos = ffs(reg_val) +
				  (8 * cmpnt->val_bytes * i);
			break;
		}
	}

	/* Find index related to the item in array *_ahub_mux_texts[] */
	for (i = 0; i < e->items; i++) {
		if (bit_pos == e->values[i]) {
			uctl->value.enumerated.item[0] = i;
			break;
		}
	}

	return 0;
}

static int tegra_ahub_put_value_enum(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_value *uctl)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_kcontrol_component(kctl);
	struct tegra_ahub *ahub = snd_soc_component_get_drvdata(cmpnt);
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kctl);
	struct soc_enum *e = (struct soc_enum *)kctl->private_value;
	struct snd_soc_dapm_update update[TEGRA_XBAR_UPDATE_MAX_REG] = { };
	unsigned int *item = uctl->value.enumerated.item;
	unsigned int value = e->values[item[0]];
	unsigned int i, bit_pos, reg_idx = 0, reg_val = 0;
	int change = 0;

	if (item[0] >= e->items)
		return -EINVAL;

	if (value) {
		/* Get the register index and value to set */
		reg_idx = (value - 1) / (8 * cmpnt->val_bytes);
		bit_pos = (value - 1) % (8 * cmpnt->val_bytes);
		reg_val = BIT(bit_pos);
	}

	/*
	 * Run through all parts of a MUX register to find the state changes.
	 * There will be an additional update if new MUX input value is from
	 * different part of the MUX register.
	 */
	for (i = 0; i < ahub->soc_data->reg_count; i++) {
		update[i].reg = e->reg + (TEGRA210_XBAR_PART1_RX * i);
		update[i].val = (i == reg_idx) ? reg_val : 0;
		update[i].mask = ahub->soc_data->mask[i];
		update[i].kcontrol = kctl;

		/* Update widget power if state has changed */
		if (snd_soc_component_test_bits(cmpnt, update[i].reg,
						update[i].mask,
						update[i].val))
			change |= snd_soc_dapm_mux_update_power(dapm, kctl,
								item[0], e,
								&update[i]);
	}

	return change;
}

static struct snd_soc_dai_driver tegra210_ahub_dais[] = {
	DAI(ADMAIF1),
	DAI(ADMAIF2),
	DAI(ADMAIF3),
	DAI(ADMAIF4),
	DAI(ADMAIF5),
	DAI(ADMAIF6),
	DAI(ADMAIF7),
	DAI(ADMAIF8),
	DAI(ADMAIF9),
	DAI(ADMAIF10),
	/* XBAR <-> I2S <-> Codec */
	DAI(I2S1),
	DAI(I2S2),
	DAI(I2S3),
	DAI(I2S4),
	DAI(I2S5),
	/* XBAR <- DMIC <- Codec */
	DAI(DMIC1),
	DAI(DMIC2),
	DAI(DMIC3),
	/* XBAR -> SFC -> XBAR */
	DAI(SFC1 RX),
	DAI(SFC1 TX),
	DAI(SFC2 RX),
	DAI(SFC2 TX),
	DAI(SFC3 RX),
	DAI(SFC3 TX),
	DAI(SFC4 RX),
	DAI(SFC4 TX),
	/* XBAR -> MVC -> XBAR */
	DAI(MVC1 RX),
	DAI(MVC1 TX),
	DAI(MVC2 RX),
	DAI(MVC2 TX),
	/* XBAR -> AMX(4:1) -> XBAR */
	DAI(AMX1 RX1),
	DAI(AMX1 RX2),
	DAI(AMX1 RX3),
	DAI(AMX1 RX4),
	DAI(AMX1),
	DAI(AMX2 RX1),
	DAI(AMX2 RX2),
	DAI(AMX2 RX3),
	DAI(AMX2 RX4),
	DAI(AMX2),
	/* XBAR -> ADX(1:4) -> XBAR */
	DAI(ADX1),
	DAI(ADX1 TX1),
	DAI(ADX1 TX2),
	DAI(ADX1 TX3),
	DAI(ADX1 TX4),
	DAI(ADX2),
	DAI(ADX2 TX1),
	DAI(ADX2 TX2),
	DAI(ADX2 TX3),
	DAI(ADX2 TX4),
	/* XBAR -> MIXER(10:5) -> XBAR */
	DAI(MIXER1 RX1),
	DAI(MIXER1 RX2),
	DAI(MIXER1 RX3),
	DAI(MIXER1 RX4),
	DAI(MIXER1 RX5),
	DAI(MIXER1 RX6),
	DAI(MIXER1 RX7),
	DAI(MIXER1 RX8),
	DAI(MIXER1 RX9),
	DAI(MIXER1 RX10),
	DAI(MIXER1 TX1),
	DAI(MIXER1 TX2),
	DAI(MIXER1 TX3),
	DAI(MIXER1 TX4),
	DAI(MIXER1 TX5),
	/* XBAR -> OPE -> XBAR */
	DAI(OPE1 RX),
	DAI(OPE1 TX),
	DAI(OPE2 RX),
	DAI(OPE2 TX),
};

static struct snd_soc_dai_driver tegra186_ahub_dais[] = {
	DAI(ADMAIF1),
	DAI(ADMAIF2),
	DAI(ADMAIF3),
	DAI(ADMAIF4),
	DAI(ADMAIF5),
	DAI(ADMAIF6),
	DAI(ADMAIF7),
	DAI(ADMAIF8),
	DAI(ADMAIF9),
	DAI(ADMAIF10),
	DAI(ADMAIF11),
	DAI(ADMAIF12),
	DAI(ADMAIF13),
	DAI(ADMAIF14),
	DAI(ADMAIF15),
	DAI(ADMAIF16),
	DAI(ADMAIF17),
	DAI(ADMAIF18),
	DAI(ADMAIF19),
	DAI(ADMAIF20),
	/* XBAR <-> I2S <-> Codec */
	DAI(I2S1),
	DAI(I2S2),
	DAI(I2S3),
	DAI(I2S4),
	DAI(I2S5),
	DAI(I2S6),
	/* XBAR <- DMIC <- Codec */
	DAI(DMIC1),
	DAI(DMIC2),
	DAI(DMIC3),
	DAI(DMIC4),
	/* XBAR -> DSPK -> Codec */
	DAI(DSPK1),
	DAI(DSPK2),
	/* XBAR -> SFC -> XBAR */
	DAI(SFC1 RX),
	DAI(SFC1 TX),
	DAI(SFC2 RX),
	DAI(SFC2 TX),
	DAI(SFC3 RX),
	DAI(SFC3 TX),
	DAI(SFC4 RX),
	DAI(SFC4 TX),
	/* XBAR -> MVC -> XBAR */
	DAI(MVC1 RX),
	DAI(MVC1 TX),
	DAI(MVC2 RX),
	DAI(MVC2 TX),
	/* XBAR -> AMX(4:1) -> XBAR */
	DAI(AMX1 RX1),
	DAI(AMX1 RX2),
	DAI(AMX1 RX3),
	DAI(AMX1 RX4),
	DAI(AMX1),
	DAI(AMX2 RX1),
	DAI(AMX2 RX2),
	DAI(AMX2 RX3),
	DAI(AMX2 RX4),
	DAI(AMX2),
	DAI(AMX3 RX1),
	DAI(AMX3 RX2),
	DAI(AMX3 RX3),
	DAI(AMX3 RX4),
	DAI(AMX3),
	DAI(AMX4 RX1),
	DAI(AMX4 RX2),
	DAI(AMX4 RX3),
	DAI(AMX4 RX4),
	DAI(AMX4),
	/* XBAR -> ADX(1:4) -> XBAR */
	DAI(ADX1),
	DAI(ADX1 TX1),
	DAI(ADX1 TX2),
	DAI(ADX1 TX3),
	DAI(ADX1 TX4),
	DAI(ADX2),
	DAI(ADX2 TX1),
	DAI(ADX2 TX2),
	DAI(ADX2 TX3),
	DAI(ADX2 TX4),
	DAI(ADX3),
	DAI(ADX3 TX1),
	DAI(ADX3 TX2),
	DAI(ADX3 TX3),
	DAI(ADX3 TX4),
	DAI(ADX4),
	DAI(ADX4 TX1),
	DAI(ADX4 TX2),
	DAI(ADX4 TX3),
	DAI(ADX4 TX4),
	/* XBAR -> MIXER1(10:5) -> XBAR */
	DAI(MIXER1 RX1),
	DAI(MIXER1 RX2),
	DAI(MIXER1 RX3),
	DAI(MIXER1 RX4),
	DAI(MIXER1 RX5),
	DAI(MIXER1 RX6),
	DAI(MIXER1 RX7),
	DAI(MIXER1 RX8),
	DAI(MIXER1 RX9),
	DAI(MIXER1 RX10),
	DAI(MIXER1 TX1),
	DAI(MIXER1 TX2),
	DAI(MIXER1 TX3),
	DAI(MIXER1 TX4),
	DAI(MIXER1 TX5),
	/* XBAR -> ASRC -> XBAR */
	DAI(ASRC1 RX1),
	DAI(ASRC1 TX1),
	DAI(ASRC1 RX2),
	DAI(ASRC1 TX2),
	DAI(ASRC1 RX3),
	DAI(ASRC1 TX3),
	DAI(ASRC1 RX4),
	DAI(ASRC1 TX4),
	DAI(ASRC1 RX5),
	DAI(ASRC1 TX5),
	DAI(ASRC1 RX6),
	DAI(ASRC1 TX6),
	DAI(ASRC1 RX7),
	/* XBAR -> OPE -> XBAR */
	DAI(OPE1 RX),
	DAI(OPE1 TX),
};

static const char * const tegra210_ahub_mux_texts[] = {
	"None",
	"ADMAIF1",
	"ADMAIF2",
	"ADMAIF3",
	"ADMAIF4",
	"ADMAIF5",
	"ADMAIF6",
	"ADMAIF7",
	"ADMAIF8",
	"ADMAIF9",
	"ADMAIF10",
	"I2S1",
	"I2S2",
	"I2S3",
	"I2S4",
	"I2S5",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"SFC1",
	"SFC2",
	"SFC3",
	"SFC4",
	"MVC1",
	"MVC2",
	"AMX1",
	"AMX2",
	"ADX1 TX1",
	"ADX1 TX2",
	"ADX1 TX3",
	"ADX1 TX4",
	"ADX2 TX1",
	"ADX2 TX2",
	"ADX2 TX3",
	"ADX2 TX4",
	"MIXER1 TX1",
	"MIXER1 TX2",
	"MIXER1 TX3",
	"MIXER1 TX4",
	"MIXER1 TX5",
	"OPE1",
	"OPE2",
};

static const char * const tegra186_ahub_mux_texts[] = {
	"None",
	"ADMAIF1",
	"ADMAIF2",
	"ADMAIF3",
	"ADMAIF4",
	"ADMAIF5",
	"ADMAIF6",
	"ADMAIF7",
	"ADMAIF8",
	"ADMAIF9",
	"ADMAIF10",
	"ADMAIF11",
	"ADMAIF12",
	"ADMAIF13",
	"ADMAIF14",
	"ADMAIF15",
	"ADMAIF16",
	"I2S1",
	"I2S2",
	"I2S3",
	"I2S4",
	"I2S5",
	"I2S6",
	"ADMAIF17",
	"ADMAIF18",
	"ADMAIF19",
	"ADMAIF20",
	"DMIC1",
	"DMIC2",
	"DMIC3",
	"DMIC4",
	"SFC1",
	"SFC2",
	"SFC3",
	"SFC4",
	"MVC1",
	"MVC2",
	"AMX1",
	"AMX2",
	"AMX3",
	"AMX4",
	"ADX1 TX1",
	"ADX1 TX2",
	"ADX1 TX3",
	"ADX1 TX4",
	"ADX2 TX1",
	"ADX2 TX2",
	"ADX2 TX3",
	"ADX2 TX4",
	"ADX3 TX1",
	"ADX3 TX2",
	"ADX3 TX3",
	"ADX3 TX4",
	"ADX4 TX1",
	"ADX4 TX2",
	"ADX4 TX3",
	"ADX4 TX4",
	"MIXER1 TX1",
	"MIXER1 TX2",
	"MIXER1 TX3",
	"MIXER1 TX4",
	"MIXER1 TX5",
	"ASRC1 TX1",
	"ASRC1 TX2",
	"ASRC1 TX3",
	"ASRC1 TX4",
	"ASRC1 TX5",
	"ASRC1 TX6",
	"OPE1",
};

static const unsigned int tegra210_ahub_mux_values[] = {
	0,
	/* ADMAIF */
	MUX_VALUE(0, 0),
	MUX_VALUE(0, 1),
	MUX_VALUE(0, 2),
	MUX_VALUE(0, 3),
	MUX_VALUE(0, 4),
	MUX_VALUE(0, 5),
	MUX_VALUE(0, 6),
	MUX_VALUE(0, 7),
	MUX_VALUE(0, 8),
	MUX_VALUE(0, 9),
	/* I2S */
	MUX_VALUE(0, 16),
	MUX_VALUE(0, 17),
	MUX_VALUE(0, 18),
	MUX_VALUE(0, 19),
	MUX_VALUE(0, 20),
	/* DMIC */
	MUX_VALUE(2, 18),
	MUX_VALUE(2, 19),
	MUX_VALUE(2, 20),
	/* SFC */
	MUX_VALUE(0, 24),
	MUX_VALUE(0, 25),
	MUX_VALUE(0, 26),
	MUX_VALUE(0, 27),
	/* MVC */
	MUX_VALUE(2, 8),
	MUX_VALUE(2, 9),
	/* AMX */
	MUX_VALUE(1, 8),
	MUX_VALUE(1, 9),
	/* ADX */
	MUX_VALUE(2, 24),
	MUX_VALUE(2, 25),
	MUX_VALUE(2, 26),
	MUX_VALUE(2, 27),
	MUX_VALUE(2, 28),
	MUX_VALUE(2, 29),
	MUX_VALUE(2, 30),
	MUX_VALUE(2, 31),
	/* MIXER */
	MUX_VALUE(1, 0),
	MUX_VALUE(1, 1),
	MUX_VALUE(1, 2),
	MUX_VALUE(1, 3),
	MUX_VALUE(1, 4),
	/* OPE */
	MUX_VALUE(2, 0),
	MUX_VALUE(2, 1),
};

static const unsigned int tegra186_ahub_mux_values[] = {
	0,
	/* ADMAIF */
	MUX_VALUE(0, 0),
	MUX_VALUE(0, 1),
	MUX_VALUE(0, 2),
	MUX_VALUE(0, 3),
	MUX_VALUE(0, 4),
	MUX_VALUE(0, 5),
	MUX_VALUE(0, 6),
	MUX_VALUE(0, 7),
	MUX_VALUE(0, 8),
	MUX_VALUE(0, 9),
	MUX_VALUE(0, 10),
	MUX_VALUE(0, 11),
	MUX_VALUE(0, 12),
	MUX_VALUE(0, 13),
	MUX_VALUE(0, 14),
	MUX_VALUE(0, 15),
	/* I2S */
	MUX_VALUE(0, 16),
	MUX_VALUE(0, 17),
	MUX_VALUE(0, 18),
	MUX_VALUE(0, 19),
	MUX_VALUE(0, 20),
	MUX_VALUE(0, 21),
	/* ADMAIF */
	MUX_VALUE(3, 16),
	MUX_VALUE(3, 17),
	MUX_VALUE(3, 18),
	MUX_VALUE(3, 19),
	/* DMIC */
	MUX_VALUE(2, 18),
	MUX_VALUE(2, 19),
	MUX_VALUE(2, 20),
	MUX_VALUE(2, 21),
	/* SFC */
	MUX_VALUE(0, 24),
	MUX_VALUE(0, 25),
	MUX_VALUE(0, 26),
	MUX_VALUE(0, 27),
	/* MVC */
	MUX_VALUE(2, 8),
	MUX_VALUE(2, 9),
	/* AMX */
	MUX_VALUE(1, 8),
	MUX_VALUE(1, 9),
	MUX_VALUE(1, 10),
	MUX_VALUE(1, 11),
	/* ADX */
	MUX_VALUE(2, 24),
	MUX_VALUE(2, 25),
	MUX_VALUE(2, 26),
	MUX_VALUE(2, 27),
	MUX_VALUE(2, 28),
	MUX_VALUE(2, 29),
	MUX_VALUE(2, 30),
	MUX_VALUE(2, 31),
	MUX_VALUE(3, 0),
	MUX_VALUE(3, 1),
	MUX_VALUE(3, 2),
	MUX_VALUE(3, 3),
	MUX_VALUE(3, 4),
	MUX_VALUE(3, 5),
	MUX_VALUE(3, 6),
	MUX_VALUE(3, 7),
	/* MIXER */
	MUX_VALUE(1, 0),
	MUX_VALUE(1, 1),
	MUX_VALUE(1, 2),
	MUX_VALUE(1, 3),
	MUX_VALUE(1, 4),
	/* ASRC */
	MUX_VALUE(3, 24),
	MUX_VALUE(3, 25),
	MUX_VALUE(3, 26),
	MUX_VALUE(3, 27),
	MUX_VALUE(3, 28),
	MUX_VALUE(3, 29),
	/* OPE */
	MUX_VALUE(2, 0),
};

/* Controls for t210 */
MUX_ENUM_CTRL_DECL(t210_admaif1_tx, 0x00);
MUX_ENUM_CTRL_DECL(t210_admaif2_tx, 0x01);
MUX_ENUM_CTRL_DECL(t210_admaif3_tx, 0x02);
MUX_ENUM_CTRL_DECL(t210_admaif4_tx, 0x03);
MUX_ENUM_CTRL_DECL(t210_admaif5_tx, 0x04);
MUX_ENUM_CTRL_DECL(t210_admaif6_tx, 0x05);
MUX_ENUM_CTRL_DECL(t210_admaif7_tx, 0x06);
MUX_ENUM_CTRL_DECL(t210_admaif8_tx, 0x07);
MUX_ENUM_CTRL_DECL(t210_admaif9_tx, 0x08);
MUX_ENUM_CTRL_DECL(t210_admaif10_tx, 0x09);
MUX_ENUM_CTRL_DECL(t210_i2s1_tx, 0x10);
MUX_ENUM_CTRL_DECL(t210_i2s2_tx, 0x11);
MUX_ENUM_CTRL_DECL(t210_i2s3_tx, 0x12);
MUX_ENUM_CTRL_DECL(t210_i2s4_tx, 0x13);
MUX_ENUM_CTRL_DECL(t210_i2s5_tx, 0x14);
MUX_ENUM_CTRL_DECL(t210_sfc1_tx, 0x18);
MUX_ENUM_CTRL_DECL(t210_sfc2_tx, 0x19);
MUX_ENUM_CTRL_DECL(t210_sfc3_tx, 0x1a);
MUX_ENUM_CTRL_DECL(t210_sfc4_tx, 0x1b);
MUX_ENUM_CTRL_DECL(t210_mvc1_tx, 0x48);
MUX_ENUM_CTRL_DECL(t210_mvc2_tx, 0x49);
MUX_ENUM_CTRL_DECL(t210_amx11_tx, 0x50);
MUX_ENUM_CTRL_DECL(t210_amx12_tx, 0x51);
MUX_ENUM_CTRL_DECL(t210_amx13_tx, 0x52);
MUX_ENUM_CTRL_DECL(t210_amx14_tx, 0x53);
MUX_ENUM_CTRL_DECL(t210_amx21_tx, 0x54);
MUX_ENUM_CTRL_DECL(t210_amx22_tx, 0x55);
MUX_ENUM_CTRL_DECL(t210_amx23_tx, 0x56);
MUX_ENUM_CTRL_DECL(t210_amx24_tx, 0x57);
MUX_ENUM_CTRL_DECL(t210_adx1_tx, 0x58);
MUX_ENUM_CTRL_DECL(t210_adx2_tx, 0x59);
MUX_ENUM_CTRL_DECL(t210_mixer11_tx, 0x20);
MUX_ENUM_CTRL_DECL(t210_mixer12_tx, 0x21);
MUX_ENUM_CTRL_DECL(t210_mixer13_tx, 0x22);
MUX_ENUM_CTRL_DECL(t210_mixer14_tx, 0x23);
MUX_ENUM_CTRL_DECL(t210_mixer15_tx, 0x24);
MUX_ENUM_CTRL_DECL(t210_mixer16_tx, 0x25);
MUX_ENUM_CTRL_DECL(t210_mixer17_tx, 0x26);
MUX_ENUM_CTRL_DECL(t210_mixer18_tx, 0x27);
MUX_ENUM_CTRL_DECL(t210_mixer19_tx, 0x28);
MUX_ENUM_CTRL_DECL(t210_mixer110_tx, 0x29);
MUX_ENUM_CTRL_DECL(t210_ope1_tx, 0x40);
MUX_ENUM_CTRL_DECL(t210_ope2_tx, 0x41);

/* Controls for t186 */
MUX_ENUM_CTRL_DECL_186(t186_admaif1_tx, 0x00);
MUX_ENUM_CTRL_DECL_186(t186_admaif2_tx, 0x01);
MUX_ENUM_CTRL_DECL_186(t186_admaif3_tx, 0x02);
MUX_ENUM_CTRL_DECL_186(t186_admaif4_tx, 0x03);
MUX_ENUM_CTRL_DECL_186(t186_admaif5_tx, 0x04);
MUX_ENUM_CTRL_DECL_186(t186_admaif6_tx, 0x05);
MUX_ENUM_CTRL_DECL_186(t186_admaif7_tx, 0x06);
MUX_ENUM_CTRL_DECL_186(t186_admaif8_tx, 0x07);
MUX_ENUM_CTRL_DECL_186(t186_admaif9_tx, 0x08);
MUX_ENUM_CTRL_DECL_186(t186_admaif10_tx, 0x09);
MUX_ENUM_CTRL_DECL_186(t186_i2s1_tx, 0x10);
MUX_ENUM_CTRL_DECL_186(t186_i2s2_tx, 0x11);
MUX_ENUM_CTRL_DECL_186(t186_i2s3_tx, 0x12);
MUX_ENUM_CTRL_DECL_186(t186_i2s4_tx, 0x13);
MUX_ENUM_CTRL_DECL_186(t186_i2s5_tx, 0x14);
MUX_ENUM_CTRL_DECL_186(t186_admaif11_tx, 0x0a);
MUX_ENUM_CTRL_DECL_186(t186_admaif12_tx, 0x0b);
MUX_ENUM_CTRL_DECL_186(t186_admaif13_tx, 0x0c);
MUX_ENUM_CTRL_DECL_186(t186_admaif14_tx, 0x0d);
MUX_ENUM_CTRL_DECL_186(t186_admaif15_tx, 0x0e);
MUX_ENUM_CTRL_DECL_186(t186_admaif16_tx, 0x0f);
MUX_ENUM_CTRL_DECL_186(t186_i2s6_tx, 0x15);
MUX_ENUM_CTRL_DECL_186(t186_dspk1_tx, 0x30);
MUX_ENUM_CTRL_DECL_186(t186_dspk2_tx, 0x31);
MUX_ENUM_CTRL_DECL_186(t186_admaif17_tx, 0x68);
MUX_ENUM_CTRL_DECL_186(t186_admaif18_tx, 0x69);
MUX_ENUM_CTRL_DECL_186(t186_admaif19_tx, 0x6a);
MUX_ENUM_CTRL_DECL_186(t186_admaif20_tx, 0x6b);
MUX_ENUM_CTRL_DECL_186(t186_sfc1_tx, 0x18);
MUX_ENUM_CTRL_DECL_186(t186_sfc2_tx, 0x19);
MUX_ENUM_CTRL_DECL_186(t186_sfc3_tx, 0x1a);
MUX_ENUM_CTRL_DECL_186(t186_sfc4_tx, 0x1b);
MUX_ENUM_CTRL_DECL_186(t186_mvc1_tx, 0x48);
MUX_ENUM_CTRL_DECL_186(t186_mvc2_tx, 0x49);
MUX_ENUM_CTRL_DECL_186(t186_amx11_tx, 0x50);
MUX_ENUM_CTRL_DECL_186(t186_amx12_tx, 0x51);
MUX_ENUM_CTRL_DECL_186(t186_amx13_tx, 0x52);
MUX_ENUM_CTRL_DECL_186(t186_amx14_tx, 0x53);
MUX_ENUM_CTRL_DECL_186(t186_amx21_tx, 0x54);
MUX_ENUM_CTRL_DECL_186(t186_amx22_tx, 0x55);
MUX_ENUM_CTRL_DECL_186(t186_amx23_tx, 0x56);
MUX_ENUM_CTRL_DECL_186(t186_amx24_tx, 0x57);
MUX_ENUM_CTRL_DECL_186(t186_amx31_tx, 0x58);
MUX_ENUM_CTRL_DECL_186(t186_amx32_tx, 0x59);
MUX_ENUM_CTRL_DECL_186(t186_amx33_tx, 0x5a);
MUX_ENUM_CTRL_DECL_186(t186_amx34_tx, 0x5b);
MUX_ENUM_CTRL_DECL_186(t186_amx41_tx, 0x64);
MUX_ENUM_CTRL_DECL_186(t186_amx42_tx, 0x65);
MUX_ENUM_CTRL_DECL_186(t186_amx43_tx, 0x66);
MUX_ENUM_CTRL_DECL_186(t186_amx44_tx, 0x67);
MUX_ENUM_CTRL_DECL_186(t186_adx1_tx, 0x60);
MUX_ENUM_CTRL_DECL_186(t186_adx2_tx, 0x61);
MUX_ENUM_CTRL_DECL_186(t186_adx3_tx, 0x62);
MUX_ENUM_CTRL_DECL_186(t186_adx4_tx, 0x63);
MUX_ENUM_CTRL_DECL_186(t186_mixer11_tx, 0x20);
MUX_ENUM_CTRL_DECL_186(t186_mixer12_tx, 0x21);
MUX_ENUM_CTRL_DECL_186(t186_mixer13_tx, 0x22);
MUX_ENUM_CTRL_DECL_186(t186_mixer14_tx, 0x23);
MUX_ENUM_CTRL_DECL_186(t186_mixer15_tx, 0x24);
MUX_ENUM_CTRL_DECL_186(t186_mixer16_tx, 0x25);
MUX_ENUM_CTRL_DECL_186(t186_mixer17_tx, 0x26);
MUX_ENUM_CTRL_DECL_186(t186_mixer18_tx, 0x27);
MUX_ENUM_CTRL_DECL_186(t186_mixer19_tx, 0x28);
MUX_ENUM_CTRL_DECL_186(t186_mixer110_tx, 0x29);
MUX_ENUM_CTRL_DECL_186(t186_asrc11_tx, 0x6c);
MUX_ENUM_CTRL_DECL_186(t186_asrc12_tx, 0x6d);
MUX_ENUM_CTRL_DECL_186(t186_asrc13_tx, 0x6e);
MUX_ENUM_CTRL_DECL_186(t186_asrc14_tx, 0x6f);
MUX_ENUM_CTRL_DECL_186(t186_asrc15_tx, 0x70);
MUX_ENUM_CTRL_DECL_186(t186_asrc16_tx, 0x71);
MUX_ENUM_CTRL_DECL_186(t186_asrc17_tx, 0x72);
MUX_ENUM_CTRL_DECL_186(t186_ope1_tx, 0x40);

/* Controls for t234 */
MUX_ENUM_CTRL_DECL_234(t234_mvc1_tx, 0x44);
MUX_ENUM_CTRL_DECL_234(t234_mvc2_tx, 0x45);
MUX_ENUM_CTRL_DECL_234(t234_amx11_tx, 0x48);
MUX_ENUM_CTRL_DECL_234(t234_amx12_tx, 0x49);
MUX_ENUM_CTRL_DECL_234(t234_amx13_tx, 0x4a);
MUX_ENUM_CTRL_DECL_234(t234_amx14_tx, 0x4b);
MUX_ENUM_CTRL_DECL_234(t234_amx21_tx, 0x4c);
MUX_ENUM_CTRL_DECL_234(t234_amx22_tx, 0x4d);
MUX_ENUM_CTRL_DECL_234(t234_amx23_tx, 0x4e);
MUX_ENUM_CTRL_DECL_234(t234_amx24_tx, 0x4f);
MUX_ENUM_CTRL_DECL_234(t234_amx31_tx, 0x50);
MUX_ENUM_CTRL_DECL_234(t234_amx32_tx, 0x51);
MUX_ENUM_CTRL_DECL_234(t234_amx33_tx, 0x52);
MUX_ENUM_CTRL_DECL_234(t234_amx34_tx, 0x53);
MUX_ENUM_CTRL_DECL_234(t234_adx1_tx, 0x58);
MUX_ENUM_CTRL_DECL_234(t234_adx2_tx, 0x59);
MUX_ENUM_CTRL_DECL_234(t234_adx3_tx, 0x5a);
MUX_ENUM_CTRL_DECL_234(t234_adx4_tx, 0x5b);
MUX_ENUM_CTRL_DECL_234(t234_amx41_tx, 0x5c);
MUX_ENUM_CTRL_DECL_234(t234_amx42_tx, 0x5d);
MUX_ENUM_CTRL_DECL_234(t234_amx43_tx, 0x5e);
MUX_ENUM_CTRL_DECL_234(t234_amx44_tx, 0x5f);
MUX_ENUM_CTRL_DECL_234(t234_admaif17_tx, 0x60);
MUX_ENUM_CTRL_DECL_234(t234_admaif18_tx, 0x61);
MUX_ENUM_CTRL_DECL_234(t234_admaif19_tx, 0x62);
MUX_ENUM_CTRL_DECL_234(t234_admaif20_tx, 0x63);
MUX_ENUM_CTRL_DECL_234(t234_asrc11_tx, 0x64);
MUX_ENUM_CTRL_DECL_234(t234_asrc12_tx, 0x65);
MUX_ENUM_CTRL_DECL_234(t234_asrc13_tx, 0x66);
MUX_ENUM_CTRL_DECL_234(t234_asrc14_tx, 0x67);
MUX_ENUM_CTRL_DECL_234(t234_asrc15_tx, 0x68);
MUX_ENUM_CTRL_DECL_234(t234_asrc16_tx, 0x69);
MUX_ENUM_CTRL_DECL_234(t234_asrc17_tx, 0x6a);

static const struct snd_soc_dapm_widget tegra210_ahub_widgets[] = {
	WIDGETS("ADMAIF1", t210_admaif1_tx),
	WIDGETS("ADMAIF2", t210_admaif2_tx),
	WIDGETS("ADMAIF3", t210_admaif3_tx),
	WIDGETS("ADMAIF4", t210_admaif4_tx),
	WIDGETS("ADMAIF5", t210_admaif5_tx),
	WIDGETS("ADMAIF6", t210_admaif6_tx),
	WIDGETS("ADMAIF7", t210_admaif7_tx),
	WIDGETS("ADMAIF8", t210_admaif8_tx),
	WIDGETS("ADMAIF9", t210_admaif9_tx),
	WIDGETS("ADMAIF10", t210_admaif10_tx),
	WIDGETS("I2S1", t210_i2s1_tx),
	WIDGETS("I2S2", t210_i2s2_tx),
	WIDGETS("I2S3", t210_i2s3_tx),
	WIDGETS("I2S4", t210_i2s4_tx),
	WIDGETS("I2S5", t210_i2s5_tx),
	TX_WIDGETS("DMIC1"),
	TX_WIDGETS("DMIC2"),
	TX_WIDGETS("DMIC3"),
	WIDGETS("SFC1", t210_sfc1_tx),
	WIDGETS("SFC2", t210_sfc2_tx),
	WIDGETS("SFC3", t210_sfc3_tx),
	WIDGETS("SFC4", t210_sfc4_tx),
	WIDGETS("MVC1", t210_mvc1_tx),
	WIDGETS("MVC2", t210_mvc2_tx),
	WIDGETS("AMX1 RX1", t210_amx11_tx),
	WIDGETS("AMX1 RX2", t210_amx12_tx),
	WIDGETS("AMX1 RX3", t210_amx13_tx),
	WIDGETS("AMX1 RX4", t210_amx14_tx),
	WIDGETS("AMX2 RX1", t210_amx21_tx),
	WIDGETS("AMX2 RX2", t210_amx22_tx),
	WIDGETS("AMX2 RX3", t210_amx23_tx),
	WIDGETS("AMX2 RX4", t210_amx24_tx),
	TX_WIDGETS("AMX1"),
	TX_WIDGETS("AMX2"),
	WIDGETS("ADX1", t210_adx1_tx),
	WIDGETS("ADX2", t210_adx2_tx),
	TX_WIDGETS("ADX1 TX1"),
	TX_WIDGETS("ADX1 TX2"),
	TX_WIDGETS("ADX1 TX3"),
	TX_WIDGETS("ADX1 TX4"),
	TX_WIDGETS("ADX2 TX1"),
	TX_WIDGETS("ADX2 TX2"),
	TX_WIDGETS("ADX2 TX3"),
	TX_WIDGETS("ADX2 TX4"),
	WIDGETS("MIXER1 RX1", t210_mixer11_tx),
	WIDGETS("MIXER1 RX2", t210_mixer12_tx),
	WIDGETS("MIXER1 RX3", t210_mixer13_tx),
	WIDGETS("MIXER1 RX4", t210_mixer14_tx),
	WIDGETS("MIXER1 RX5", t210_mixer15_tx),
	WIDGETS("MIXER1 RX6", t210_mixer16_tx),
	WIDGETS("MIXER1 RX7", t210_mixer17_tx),
	WIDGETS("MIXER1 RX8", t210_mixer18_tx),
	WIDGETS("MIXER1 RX9", t210_mixer19_tx),
	WIDGETS("MIXER1 RX10", t210_mixer110_tx),
	TX_WIDGETS("MIXER1 TX1"),
	TX_WIDGETS("MIXER1 TX2"),
	TX_WIDGETS("MIXER1 TX3"),
	TX_WIDGETS("MIXER1 TX4"),
	TX_WIDGETS("MIXER1 TX5"),
	WIDGETS("OPE1", t210_ope1_tx),
	WIDGETS("OPE2", t210_ope2_tx),
};

static const struct snd_soc_dapm_widget tegra186_ahub_widgets[] = {
	WIDGETS("ADMAIF1", t186_admaif1_tx),
	WIDGETS("ADMAIF2", t186_admaif2_tx),
	WIDGETS("ADMAIF3", t186_admaif3_tx),
	WIDGETS("ADMAIF4", t186_admaif4_tx),
	WIDGETS("ADMAIF5", t186_admaif5_tx),
	WIDGETS("ADMAIF6", t186_admaif6_tx),
	WIDGETS("ADMAIF7", t186_admaif7_tx),
	WIDGETS("ADMAIF8", t186_admaif8_tx),
	WIDGETS("ADMAIF9", t186_admaif9_tx),
	WIDGETS("ADMAIF10", t186_admaif10_tx),
	WIDGETS("ADMAIF11", t186_admaif11_tx),
	WIDGETS("ADMAIF12", t186_admaif12_tx),
	WIDGETS("ADMAIF13", t186_admaif13_tx),
	WIDGETS("ADMAIF14", t186_admaif14_tx),
	WIDGETS("ADMAIF15", t186_admaif15_tx),
	WIDGETS("ADMAIF16", t186_admaif16_tx),
	WIDGETS("ADMAIF17", t186_admaif17_tx),
	WIDGETS("ADMAIF18", t186_admaif18_tx),
	WIDGETS("ADMAIF19", t186_admaif19_tx),
	WIDGETS("ADMAIF20", t186_admaif20_tx),
	WIDGETS("I2S1", t186_i2s1_tx),
	WIDGETS("I2S2", t186_i2s2_tx),
	WIDGETS("I2S3", t186_i2s3_tx),
	WIDGETS("I2S4", t186_i2s4_tx),
	WIDGETS("I2S5", t186_i2s5_tx),
	WIDGETS("I2S6", t186_i2s6_tx),
	TX_WIDGETS("DMIC1"),
	TX_WIDGETS("DMIC2"),
	TX_WIDGETS("DMIC3"),
	TX_WIDGETS("DMIC4"),
	WIDGETS("DSPK1", t186_dspk1_tx),
	WIDGETS("DSPK2", t186_dspk2_tx),
	WIDGETS("SFC1", t186_sfc1_tx),
	WIDGETS("SFC2", t186_sfc2_tx),
	WIDGETS("SFC3", t186_sfc3_tx),
	WIDGETS("SFC4", t186_sfc4_tx),
	WIDGETS("MVC1", t186_mvc1_tx),
	WIDGETS("MVC2", t186_mvc2_tx),
	WIDGETS("AMX1 RX1", t186_amx11_tx),
	WIDGETS("AMX1 RX2", t186_amx12_tx),
	WIDGETS("AMX1 RX3", t186_amx13_tx),
	WIDGETS("AMX1 RX4", t186_amx14_tx),
	WIDGETS("AMX2 RX1", t186_amx21_tx),
	WIDGETS("AMX2 RX2", t186_amx22_tx),
	WIDGETS("AMX2 RX3", t186_amx23_tx),
	WIDGETS("AMX2 RX4", t186_amx24_tx),
	WIDGETS("AMX3 RX1", t186_amx31_tx),
	WIDGETS("AMX3 RX2", t186_amx32_tx),
	WIDGETS("AMX3 RX3", t186_amx33_tx),
	WIDGETS("AMX3 RX4", t186_amx34_tx),
	WIDGETS("AMX4 RX1", t186_amx41_tx),
	WIDGETS("AMX4 RX2", t186_amx42_tx),
	WIDGETS("AMX4 RX3", t186_amx43_tx),
	WIDGETS("AMX4 RX4", t186_amx44_tx),
	TX_WIDGETS("AMX1"),
	TX_WIDGETS("AMX2"),
	TX_WIDGETS("AMX3"),
	TX_WIDGETS("AMX4"),
	WIDGETS("ADX1", t186_adx1_tx),
	WIDGETS("ADX2", t186_adx2_tx),
	WIDGETS("ADX3", t186_adx3_tx),
	WIDGETS("ADX4", t186_adx4_tx),
	TX_WIDGETS("ADX1 TX1"),
	TX_WIDGETS("ADX1 TX2"),
	TX_WIDGETS("ADX1 TX3"),
	TX_WIDGETS("ADX1 TX4"),
	TX_WIDGETS("ADX2 TX1"),
	TX_WIDGETS("ADX2 TX2"),
	TX_WIDGETS("ADX2 TX3"),
	TX_WIDGETS("ADX2 TX4"),
	TX_WIDGETS("ADX3 TX1"),
	TX_WIDGETS("ADX3 TX2"),
	TX_WIDGETS("ADX3 TX3"),
	TX_WIDGETS("ADX3 TX4"),
	TX_WIDGETS("ADX4 TX1"),
	TX_WIDGETS("ADX4 TX2"),
	TX_WIDGETS("ADX4 TX3"),
	TX_WIDGETS("ADX4 TX4"),
	WIDGETS("MIXER1 RX1", t186_mixer11_tx),
	WIDGETS("MIXER1 RX2", t186_mixer12_tx),
	WIDGETS("MIXER1 RX3", t186_mixer13_tx),
	WIDGETS("MIXER1 RX4", t186_mixer14_tx),
	WIDGETS("MIXER1 RX5", t186_mixer15_tx),
	WIDGETS("MIXER1 RX6", t186_mixer16_tx),
	WIDGETS("MIXER1 RX7", t186_mixer17_tx),
	WIDGETS("MIXER1 RX8", t186_mixer18_tx),
	WIDGETS("MIXER1 RX9", t186_mixer19_tx),
	WIDGETS("MIXER1 RX10", t186_mixer110_tx),
	TX_WIDGETS("MIXER1 TX1"),
	TX_WIDGETS("MIXER1 TX2"),
	TX_WIDGETS("MIXER1 TX3"),
	TX_WIDGETS("MIXER1 TX4"),
	TX_WIDGETS("MIXER1 TX5"),
	WIDGETS("ASRC1 RX1", t186_asrc11_tx),
	WIDGETS("ASRC1 RX2", t186_asrc12_tx),
	WIDGETS("ASRC1 RX3", t186_asrc13_tx),
	WIDGETS("ASRC1 RX4", t186_asrc14_tx),
	WIDGETS("ASRC1 RX5", t186_asrc15_tx),
	WIDGETS("ASRC1 RX6", t186_asrc16_tx),
	WIDGETS("ASRC1 RX7", t186_asrc17_tx),
	TX_WIDGETS("ASRC1 TX1"),
	TX_WIDGETS("ASRC1 TX2"),
	TX_WIDGETS("ASRC1 TX3"),
	TX_WIDGETS("ASRC1 TX4"),
	TX_WIDGETS("ASRC1 TX5"),
	TX_WIDGETS("ASRC1 TX6"),
	WIDGETS("OPE1", t186_ope1_tx),
};

static const struct snd_soc_dapm_widget tegra234_ahub_widgets[] = {
	WIDGETS("ADMAIF1", t186_admaif1_tx),
	WIDGETS("ADMAIF2", t186_admaif2_tx),
	WIDGETS("ADMAIF3", t186_admaif3_tx),
	WIDGETS("ADMAIF4", t186_admaif4_tx),
	WIDGETS("ADMAIF5", t186_admaif5_tx),
	WIDGETS("ADMAIF6", t186_admaif6_tx),
	WIDGETS("ADMAIF7", t186_admaif7_tx),
	WIDGETS("ADMAIF8", t186_admaif8_tx),
	WIDGETS("ADMAIF9", t186_admaif9_tx),
	WIDGETS("ADMAIF10", t186_admaif10_tx),
	WIDGETS("ADMAIF11", t186_admaif11_tx),
	WIDGETS("ADMAIF12", t186_admaif12_tx),
	WIDGETS("ADMAIF13", t186_admaif13_tx),
	WIDGETS("ADMAIF14", t186_admaif14_tx),
	WIDGETS("ADMAIF15", t186_admaif15_tx),
	WIDGETS("ADMAIF16", t186_admaif16_tx),
	WIDGETS("ADMAIF17", t234_admaif17_tx),
	WIDGETS("ADMAIF18", t234_admaif18_tx),
	WIDGETS("ADMAIF19", t234_admaif19_tx),
	WIDGETS("ADMAIF20", t234_admaif20_tx),
	WIDGETS("I2S1", t186_i2s1_tx),
	WIDGETS("I2S2", t186_i2s2_tx),
	WIDGETS("I2S3", t186_i2s3_tx),
	WIDGETS("I2S4", t186_i2s4_tx),
	WIDGETS("I2S5", t186_i2s5_tx),
	WIDGETS("I2S6", t186_i2s6_tx),
	TX_WIDGETS("DMIC1"),
	TX_WIDGETS("DMIC2"),
	TX_WIDGETS("DMIC3"),
	TX_WIDGETS("DMIC4"),
	WIDGETS("DSPK1", t186_dspk1_tx),
	WIDGETS("DSPK2", t186_dspk2_tx),
	WIDGETS("SFC1", t186_sfc1_tx),
	WIDGETS("SFC2", t186_sfc2_tx),
	WIDGETS("SFC3", t186_sfc3_tx),
	WIDGETS("SFC4", t186_sfc4_tx),
	WIDGETS("MVC1", t234_mvc1_tx),
	WIDGETS("MVC2", t234_mvc2_tx),
	WIDGETS("AMX1 RX1", t234_amx11_tx),
	WIDGETS("AMX1 RX2", t234_amx12_tx),
	WIDGETS("AMX1 RX3", t234_amx13_tx),
	WIDGETS("AMX1 RX4", t234_amx14_tx),
	WIDGETS("AMX2 RX1", t234_amx21_tx),
	WIDGETS("AMX2 RX2", t234_amx22_tx),
	WIDGETS("AMX2 RX3", t234_amx23_tx),
	WIDGETS("AMX2 RX4", t234_amx24_tx),
	WIDGETS("AMX3 RX1", t234_amx31_tx),
	WIDGETS("AMX3 RX2", t234_amx32_tx),
	WIDGETS("AMX3 RX3", t234_amx33_tx),
	WIDGETS("AMX3 RX4", t234_amx34_tx),
	WIDGETS("AMX4 RX1", t234_amx41_tx),
	WIDGETS("AMX4 RX2", t234_amx42_tx),
	WIDGETS("AMX4 RX3", t234_amx43_tx),
	WIDGETS("AMX4 RX4", t234_amx44_tx),
	TX_WIDGETS("AMX1"),
	TX_WIDGETS("AMX2"),
	TX_WIDGETS("AMX3"),
	TX_WIDGETS("AMX4"),
	WIDGETS("ADX1", t234_adx1_tx),
	WIDGETS("ADX2", t234_adx2_tx),
	WIDGETS("ADX3", t234_adx3_tx),
	WIDGETS("ADX4", t234_adx4_tx),
	TX_WIDGETS("ADX1 TX1"),
	TX_WIDGETS("ADX1 TX2"),
	TX_WIDGETS("ADX1 TX3"),
	TX_WIDGETS("ADX1 TX4"),
	TX_WIDGETS("ADX2 TX1"),
	TX_WIDGETS("ADX2 TX2"),
	TX_WIDGETS("ADX2 TX3"),
	TX_WIDGETS("ADX2 TX4"),
	TX_WIDGETS("ADX3 TX1"),
	TX_WIDGETS("ADX3 TX2"),
	TX_WIDGETS("ADX3 TX3"),
	TX_WIDGETS("ADX3 TX4"),
	TX_WIDGETS("ADX4 TX1"),
	TX_WIDGETS("ADX4 TX2"),
	TX_WIDGETS("ADX4 TX3"),
	TX_WIDGETS("ADX4 TX4"),
	WIDGETS("MIXER1 RX1", t186_mixer11_tx),
	WIDGETS("MIXER1 RX2", t186_mixer12_tx),
	WIDGETS("MIXER1 RX3", t186_mixer13_tx),
	WIDGETS("MIXER1 RX4", t186_mixer14_tx),
	WIDGETS("MIXER1 RX5", t186_mixer15_tx),
	WIDGETS("MIXER1 RX6", t186_mixer16_tx),
	WIDGETS("MIXER1 RX7", t186_mixer17_tx),
	WIDGETS("MIXER1 RX8", t186_mixer18_tx),
	WIDGETS("MIXER1 RX9", t186_mixer19_tx),
	WIDGETS("MIXER1 RX10", t186_mixer110_tx),
	TX_WIDGETS("MIXER1 TX1"),
	TX_WIDGETS("MIXER1 TX2"),
	TX_WIDGETS("MIXER1 TX3"),
	TX_WIDGETS("MIXER1 TX4"),
	TX_WIDGETS("MIXER1 TX5"),
	WIDGETS("ASRC1 RX1", t234_asrc11_tx),
	WIDGETS("ASRC1 RX2", t234_asrc12_tx),
	WIDGETS("ASRC1 RX3", t234_asrc13_tx),
	WIDGETS("ASRC1 RX4", t234_asrc14_tx),
	WIDGETS("ASRC1 RX5", t234_asrc15_tx),
	WIDGETS("ASRC1 RX6", t234_asrc16_tx),
	WIDGETS("ASRC1 RX7", t234_asrc17_tx),
	TX_WIDGETS("ASRC1 TX1"),
	TX_WIDGETS("ASRC1 TX2"),
	TX_WIDGETS("ASRC1 TX3"),
	TX_WIDGETS("ASRC1 TX4"),
	TX_WIDGETS("ASRC1 TX5"),
	TX_WIDGETS("ASRC1 TX6"),
	WIDGETS("OPE1", t186_ope1_tx),
};

#define TEGRA_COMMON_MUX_ROUTES(name)					\
	{ name " XBAR-TX",	 NULL,		name " Mux" },		\
	{ name " Mux",		"ADMAIF1",	"ADMAIF1 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF2",	"ADMAIF2 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF3",	"ADMAIF3 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF4",	"ADMAIF4 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF5",	"ADMAIF5 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF6",	"ADMAIF6 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF7",	"ADMAIF7 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF8",	"ADMAIF8 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF9",	"ADMAIF9 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF10",	"ADMAIF10 XBAR-RX" },	\
	{ name " Mux",		"I2S1",		"I2S1 XBAR-RX" },	\
	{ name " Mux",		"I2S2",		"I2S2 XBAR-RX" },	\
	{ name " Mux",		"I2S3",		"I2S3 XBAR-RX" },	\
	{ name " Mux",		"I2S4",		"I2S4 XBAR-RX" },	\
	{ name " Mux",		"I2S5",		"I2S5 XBAR-RX" },	\
	{ name " Mux",		"DMIC1",	"DMIC1 XBAR-RX" },	\
	{ name " Mux",		"DMIC2",	"DMIC2 XBAR-RX" },	\
	{ name " Mux",		"DMIC3",	"DMIC3 XBAR-RX" },	\
	{ name " Mux",		"SFC1",		"SFC1 XBAR-RX" },	\
	{ name " Mux",		"SFC2",		"SFC2 XBAR-RX" },	\
	{ name " Mux",		"SFC3",		"SFC3 XBAR-RX" },	\
	{ name " Mux",		"SFC4",		"SFC4 XBAR-RX" },	\
	{ name " Mux",		"MVC1",		"MVC1 XBAR-RX" },	\
	{ name " Mux",		"MVC2",		"MVC2 XBAR-RX" },	\
	{ name " Mux",		"AMX1",		"AMX1 XBAR-RX" },	\
	{ name " Mux",		"AMX2",		"AMX2 XBAR-RX" },	\
	{ name " Mux",		"ADX1 TX1",	"ADX1 TX1 XBAR-RX" },	\
	{ name " Mux",		"ADX1 TX2",	"ADX1 TX2 XBAR-RX" },	\
	{ name " Mux",		"ADX1 TX3",	"ADX1 TX3 XBAR-RX" },	\
	{ name " Mux",		"ADX1 TX4",	"ADX1 TX4 XBAR-RX" },	\
	{ name " Mux",		"ADX2 TX1",	"ADX2 TX1 XBAR-RX" },	\
	{ name " Mux",		"ADX2 TX2",	"ADX2 TX2 XBAR-RX" },	\
	{ name " Mux",		"ADX2 TX3",	"ADX2 TX3 XBAR-RX" },	\
	{ name " Mux",		"ADX2 TX4",	"ADX2 TX4 XBAR-RX" },	\
	{ name " Mux",		"MIXER1 TX1",	"MIXER1 TX1 XBAR-RX" },	\
	{ name " Mux",		"MIXER1 TX2",	"MIXER1 TX2 XBAR-RX" },	\
	{ name " Mux",		"MIXER1 TX3",	"MIXER1 TX3 XBAR-RX" },	\
	{ name " Mux",		"MIXER1 TX4",	"MIXER1 TX4 XBAR-RX" },	\
	{ name " Mux",		"MIXER1 TX5",	"MIXER1 TX5 XBAR-RX" }, \
	{ name " Mux",		"OPE1",		"OPE1 XBAR-RX" },

#define TEGRA210_ONLY_MUX_ROUTES(name)					\
	{ name " Mux",          "OPE2",         "OPE2 XBAR-RX" },

#define TEGRA186_ONLY_MUX_ROUTES(name)					\
	{ name " Mux",		"ADMAIF11",	"ADMAIF11 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF12",	"ADMAIF12 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF13",	"ADMAIF13 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF14",	"ADMAIF14 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF15",	"ADMAIF15 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF16",	"ADMAIF16 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF17",	"ADMAIF17 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF18",	"ADMAIF18 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF19",	"ADMAIF19 XBAR-RX" },	\
	{ name " Mux",		"ADMAIF20",	"ADMAIF20 XBAR-RX" },	\
	{ name " Mux",		"I2S6",		"I2S6 XBAR-RX" },	\
	{ name " Mux",		"DMIC4",	"DMIC4 XBAR-RX" },	\
	{ name " Mux",		"AMX3",		"AMX3 XBAR-RX" },	\
	{ name " Mux",		"AMX4",		"AMX4 XBAR-RX" },	\
	{ name " Mux",		"ADX3 TX1",	"ADX3 TX1 XBAR-RX" },	\
	{ name " Mux",		"ADX3 TX2",	"ADX3 TX2 XBAR-RX" },	\
	{ name " Mux",		"ADX3 TX3",	"ADX3 TX3 XBAR-RX" },	\
	{ name " Mux",		"ADX3 TX4",	"ADX3 TX4 XBAR-RX" },	\
	{ name " Mux",		"ADX4 TX1",	"ADX4 TX1 XBAR-RX" },	\
	{ name " Mux",		"ADX4 TX2",	"ADX4 TX2 XBAR-RX" },	\
	{ name " Mux",		"ADX4 TX3",	"ADX4 TX3 XBAR-RX" },	\
	{ name " Mux",		"ADX4 TX4",	"ADX4 TX4 XBAR-RX" },	\
	{ name " Mux",		"ASRC1 TX1",	"ASRC1 TX1 XBAR-RX" },	\
	{ name " Mux",		"ASRC1 TX2",	"ASRC1 TX2 XBAR-RX" },	\
	{ name " Mux",		"ASRC1 TX3",	"ASRC1 TX3 XBAR-RX" },	\
	{ name " Mux",		"ASRC1 TX4",	"ASRC1 TX4 XBAR-RX" },	\
	{ name " Mux",		"ASRC1 TX5",	"ASRC1 TX5 XBAR-RX" },	\
	{ name " Mux",		"ASRC1 TX6",	"ASRC1 TX6 XBAR-RX" },

#define TEGRA210_MUX_ROUTES(name)					\
	TEGRA_COMMON_MUX_ROUTES(name)					\
	TEGRA210_ONLY_MUX_ROUTES(name)

#define TEGRA186_MUX_ROUTES(name)					\
	TEGRA_COMMON_MUX_ROUTES(name)					\
	TEGRA186_ONLY_MUX_ROUTES(name)

/* Connect FEs with XBAR */
#define TEGRA_FE_ROUTES(name) \
	{ name " XBAR-Playback",	NULL,	name " Playback" },	\
	{ name " XBAR-RX",		NULL,	name " XBAR-Playback"}, \
	{ name " XBAR-Capture",		NULL,	name " XBAR-TX" },      \
	{ name " Capture",		NULL,	name " XBAR-Capture" },

static const struct snd_soc_dapm_route tegra210_ahub_routes[] = {
	TEGRA_FE_ROUTES("ADMAIF1")
	TEGRA_FE_ROUTES("ADMAIF2")
	TEGRA_FE_ROUTES("ADMAIF3")
	TEGRA_FE_ROUTES("ADMAIF4")
	TEGRA_FE_ROUTES("ADMAIF5")
	TEGRA_FE_ROUTES("ADMAIF6")
	TEGRA_FE_ROUTES("ADMAIF7")
	TEGRA_FE_ROUTES("ADMAIF8")
	TEGRA_FE_ROUTES("ADMAIF9")
	TEGRA_FE_ROUTES("ADMAIF10")
	TEGRA210_MUX_ROUTES("ADMAIF1")
	TEGRA210_MUX_ROUTES("ADMAIF2")
	TEGRA210_MUX_ROUTES("ADMAIF3")
	TEGRA210_MUX_ROUTES("ADMAIF4")
	TEGRA210_MUX_ROUTES("ADMAIF5")
	TEGRA210_MUX_ROUTES("ADMAIF6")
	TEGRA210_MUX_ROUTES("ADMAIF7")
	TEGRA210_MUX_ROUTES("ADMAIF8")
	TEGRA210_MUX_ROUTES("ADMAIF9")
	TEGRA210_MUX_ROUTES("ADMAIF10")
	TEGRA210_MUX_ROUTES("I2S1")
	TEGRA210_MUX_ROUTES("I2S2")
	TEGRA210_MUX_ROUTES("I2S3")
	TEGRA210_MUX_ROUTES("I2S4")
	TEGRA210_MUX_ROUTES("I2S5")
	TEGRA210_MUX_ROUTES("SFC1")
	TEGRA210_MUX_ROUTES("SFC2")
	TEGRA210_MUX_ROUTES("SFC3")
	TEGRA210_MUX_ROUTES("SFC4")
	TEGRA210_MUX_ROUTES("MVC1")
	TEGRA210_MUX_ROUTES("MVC2")
	TEGRA210_MUX_ROUTES("AMX1 RX1")
	TEGRA210_MUX_ROUTES("AMX1 RX2")
	TEGRA210_MUX_ROUTES("AMX1 RX3")
	TEGRA210_MUX_ROUTES("AMX1 RX4")
	TEGRA210_MUX_ROUTES("AMX2 RX1")
	TEGRA210_MUX_ROUTES("AMX2 RX2")
	TEGRA210_MUX_ROUTES("AMX2 RX3")
	TEGRA210_MUX_ROUTES("AMX2 RX4")
	TEGRA210_MUX_ROUTES("ADX1")
	TEGRA210_MUX_ROUTES("ADX2")
	TEGRA210_MUX_ROUTES("MIXER1 RX1")
	TEGRA210_MUX_ROUTES("MIXER1 RX2")
	TEGRA210_MUX_ROUTES("MIXER1 RX3")
	TEGRA210_MUX_ROUTES("MIXER1 RX4")
	TEGRA210_MUX_ROUTES("MIXER1 RX5")
	TEGRA210_MUX_ROUTES("MIXER1 RX6")
	TEGRA210_MUX_ROUTES("MIXER1 RX7")
	TEGRA210_MUX_ROUTES("MIXER1 RX8")
	TEGRA210_MUX_ROUTES("MIXER1 RX9")
	TEGRA210_MUX_ROUTES("MIXER1 RX10")
	TEGRA210_MUX_ROUTES("OPE1")
	TEGRA210_MUX_ROUTES("OPE2")
};

static const struct snd_soc_dapm_route tegra186_ahub_routes[] = {
	TEGRA_FE_ROUTES("ADMAIF1")
	TEGRA_FE_ROUTES("ADMAIF2")
	TEGRA_FE_ROUTES("ADMAIF3")
	TEGRA_FE_ROUTES("ADMAIF4")
	TEGRA_FE_ROUTES("ADMAIF5")
	TEGRA_FE_ROUTES("ADMAIF6")
	TEGRA_FE_ROUTES("ADMAIF7")
	TEGRA_FE_ROUTES("ADMAIF8")
	TEGRA_FE_ROUTES("ADMAIF9")
	TEGRA_FE_ROUTES("ADMAIF10")
	TEGRA_FE_ROUTES("ADMAIF11")
	TEGRA_FE_ROUTES("ADMAIF12")
	TEGRA_FE_ROUTES("ADMAIF13")
	TEGRA_FE_ROUTES("ADMAIF14")
	TEGRA_FE_ROUTES("ADMAIF15")
	TEGRA_FE_ROUTES("ADMAIF16")
	TEGRA_FE_ROUTES("ADMAIF17")
	TEGRA_FE_ROUTES("ADMAIF18")
	TEGRA_FE_ROUTES("ADMAIF19")
	TEGRA_FE_ROUTES("ADMAIF20")
	TEGRA186_MUX_ROUTES("ADMAIF1")
	TEGRA186_MUX_ROUTES("ADMAIF2")
	TEGRA186_MUX_ROUTES("ADMAIF3")
	TEGRA186_MUX_ROUTES("ADMAIF4")
	TEGRA186_MUX_ROUTES("ADMAIF5")
	TEGRA186_MUX_ROUTES("ADMAIF6")
	TEGRA186_MUX_ROUTES("ADMAIF7")
	TEGRA186_MUX_ROUTES("ADMAIF8")
	TEGRA186_MUX_ROUTES("ADMAIF9")
	TEGRA186_MUX_ROUTES("ADMAIF10")
	TEGRA186_MUX_ROUTES("ADMAIF11")
	TEGRA186_MUX_ROUTES("ADMAIF12")
	TEGRA186_MUX_ROUTES("ADMAIF13")
	TEGRA186_MUX_ROUTES("ADMAIF14")
	TEGRA186_MUX_ROUTES("ADMAIF15")
	TEGRA186_MUX_ROUTES("ADMAIF16")
	TEGRA186_MUX_ROUTES("ADMAIF17")
	TEGRA186_MUX_ROUTES("ADMAIF18")
	TEGRA186_MUX_ROUTES("ADMAIF19")
	TEGRA186_MUX_ROUTES("ADMAIF20")
	TEGRA186_MUX_ROUTES("I2S1")
	TEGRA186_MUX_ROUTES("I2S2")
	TEGRA186_MUX_ROUTES("I2S3")
	TEGRA186_MUX_ROUTES("I2S4")
	TEGRA186_MUX_ROUTES("I2S5")
	TEGRA186_MUX_ROUTES("I2S6")
	TEGRA186_MUX_ROUTES("DSPK1")
	TEGRA186_MUX_ROUTES("DSPK2")
	TEGRA186_MUX_ROUTES("SFC1")
	TEGRA186_MUX_ROUTES("SFC2")
	TEGRA186_MUX_ROUTES("SFC3")
	TEGRA186_MUX_ROUTES("SFC4")
	TEGRA186_MUX_ROUTES("MVC1")
	TEGRA186_MUX_ROUTES("MVC2")
	TEGRA186_MUX_ROUTES("AMX1 RX1")
	TEGRA186_MUX_ROUTES("AMX1 RX2")
	TEGRA186_MUX_ROUTES("AMX1 RX3")
	TEGRA186_MUX_ROUTES("AMX1 RX4")
	TEGRA186_MUX_ROUTES("AMX2 RX1")
	TEGRA186_MUX_ROUTES("AMX2 RX2")
	TEGRA186_MUX_ROUTES("AMX2 RX3")
	TEGRA186_MUX_ROUTES("AMX2 RX4")
	TEGRA186_MUX_ROUTES("AMX3 RX1")
	TEGRA186_MUX_ROUTES("AMX3 RX2")
	TEGRA186_MUX_ROUTES("AMX3 RX3")
	TEGRA186_MUX_ROUTES("AMX3 RX4")
	TEGRA186_MUX_ROUTES("AMX4 RX1")
	TEGRA186_MUX_ROUTES("AMX4 RX2")
	TEGRA186_MUX_ROUTES("AMX4 RX3")
	TEGRA186_MUX_ROUTES("AMX4 RX4")
	TEGRA186_MUX_ROUTES("ADX1")
	TEGRA186_MUX_ROUTES("ADX2")
	TEGRA186_MUX_ROUTES("ADX3")
	TEGRA186_MUX_ROUTES("ADX4")
	TEGRA186_MUX_ROUTES("MIXER1 RX1")
	TEGRA186_MUX_ROUTES("MIXER1 RX2")
	TEGRA186_MUX_ROUTES("MIXER1 RX3")
	TEGRA186_MUX_ROUTES("MIXER1 RX4")
	TEGRA186_MUX_ROUTES("MIXER1 RX5")
	TEGRA186_MUX_ROUTES("MIXER1 RX6")
	TEGRA186_MUX_ROUTES("MIXER1 RX7")
	TEGRA186_MUX_ROUTES("MIXER1 RX8")
	TEGRA186_MUX_ROUTES("MIXER1 RX9")
	TEGRA186_MUX_ROUTES("MIXER1 RX10")
	TEGRA186_MUX_ROUTES("ASRC1 RX1")
	TEGRA186_MUX_ROUTES("ASRC1 RX2")
	TEGRA186_MUX_ROUTES("ASRC1 RX3")
	TEGRA186_MUX_ROUTES("ASRC1 RX4")
	TEGRA186_MUX_ROUTES("ASRC1 RX5")
	TEGRA186_MUX_ROUTES("ASRC1 RX6")
	TEGRA186_MUX_ROUTES("ASRC1 RX7")
	TEGRA186_MUX_ROUTES("OPE1")
};

static const struct snd_soc_component_driver tegra210_ahub_component = {
	.dapm_widgets		= tegra210_ahub_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra210_ahub_widgets),
	.dapm_routes		= tegra210_ahub_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra210_ahub_routes),
};

static const struct snd_soc_component_driver tegra186_ahub_component = {
	.dapm_widgets = tegra186_ahub_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tegra186_ahub_widgets),
	.dapm_routes = tegra186_ahub_routes,
	.num_dapm_routes = ARRAY_SIZE(tegra186_ahub_routes),
};

static const struct snd_soc_component_driver tegra234_ahub_component = {
	.dapm_widgets		= tegra234_ahub_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tegra234_ahub_widgets),
	.dapm_routes		= tegra186_ahub_routes,
	.num_dapm_routes	= ARRAY_SIZE(tegra186_ahub_routes),
};

static const struct regmap_config tegra210_ahub_regmap_config = {
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_stride		= 4,
	.max_register		= TEGRA210_MAX_REGISTER_ADDR,
	.cache_type		= REGCACHE_FLAT,
};

static const struct regmap_config tegra186_ahub_regmap_config = {
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_stride		= 4,
	.max_register		= TEGRA186_MAX_REGISTER_ADDR,
	.cache_type		= REGCACHE_FLAT,
};

static const struct tegra_ahub_soc_data soc_data_tegra210 = {
	.cmpnt_drv	= &tegra210_ahub_component,
	.dai_drv	= tegra210_ahub_dais,
	.num_dais	= ARRAY_SIZE(tegra210_ahub_dais),
	.regmap_config	= &tegra210_ahub_regmap_config,
	.mask[0]	= TEGRA210_XBAR_REG_MASK_0,
	.mask[1]	= TEGRA210_XBAR_REG_MASK_1,
	.mask[2]	= TEGRA210_XBAR_REG_MASK_2,
	.mask[3]	= TEGRA210_XBAR_REG_MASK_3,
	.reg_count	= TEGRA210_XBAR_UPDATE_MAX_REG,
};

static const struct tegra_ahub_soc_data soc_data_tegra186 = {
	.cmpnt_drv	= &tegra186_ahub_component,
	.dai_drv	= tegra186_ahub_dais,
	.num_dais	= ARRAY_SIZE(tegra186_ahub_dais),
	.regmap_config	= &tegra186_ahub_regmap_config,
	.mask[0]	= TEGRA186_XBAR_REG_MASK_0,
	.mask[1]	= TEGRA186_XBAR_REG_MASK_1,
	.mask[2]	= TEGRA186_XBAR_REG_MASK_2,
	.mask[3]	= TEGRA186_XBAR_REG_MASK_3,
	.reg_count	= TEGRA186_XBAR_UPDATE_MAX_REG,
};

static const struct tegra_ahub_soc_data soc_data_tegra234 = {
	.cmpnt_drv	= &tegra234_ahub_component,
	.dai_drv	= tegra186_ahub_dais,
	.num_dais	= ARRAY_SIZE(tegra186_ahub_dais),
	.regmap_config	= &tegra186_ahub_regmap_config,
	.mask[0]	= TEGRA186_XBAR_REG_MASK_0,
	.mask[1]	= TEGRA186_XBAR_REG_MASK_1,
	.mask[2]	= TEGRA186_XBAR_REG_MASK_2,
	.mask[3]	= TEGRA186_XBAR_REG_MASK_3,
	.reg_count	= TEGRA186_XBAR_UPDATE_MAX_REG,
};

static const struct of_device_id tegra_ahub_of_match[] = {
	{ .compatible = "nvidia,tegra210-ahub", .data = &soc_data_tegra210 },
	{ .compatible = "nvidia,tegra186-ahub", .data = &soc_data_tegra186 },
	{ .compatible = "nvidia,tegra234-ahub", .data = &soc_data_tegra234 },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_ahub_of_match);

static int __maybe_unused tegra_ahub_runtime_suspend(struct device *dev)
{
	struct tegra_ahub *ahub = dev_get_drvdata(dev);

	regcache_cache_only(ahub->regmap, true);
	regcache_mark_dirty(ahub->regmap);

	clk_disable_unprepare(ahub->clk);

	return 0;
}

static int __maybe_unused tegra_ahub_runtime_resume(struct device *dev)
{
	struct tegra_ahub *ahub = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(ahub->clk);
	if (err) {
		dev_err(dev, "failed to enable AHUB clock, err: %d\n", err);
		return err;
	}

	regcache_cache_only(ahub->regmap, false);
	regcache_sync(ahub->regmap);

	return 0;
}

static int tegra_ahub_probe(struct platform_device *pdev)
{
	struct tegra_ahub *ahub;
	void __iomem *regs;
	int err;

	ahub = devm_kzalloc(&pdev->dev, sizeof(*ahub), GFP_KERNEL);
	if (!ahub)
		return -ENOMEM;

	ahub->soc_data = of_device_get_match_data(&pdev->dev);

	platform_set_drvdata(pdev, ahub);

	ahub->clk = devm_clk_get(&pdev->dev, "ahub");
	if (IS_ERR(ahub->clk)) {
		dev_err(&pdev->dev, "can't retrieve AHUB clock\n");
		return PTR_ERR(ahub->clk);
	}

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ahub->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					     ahub->soc_data->regmap_config);
	if (IS_ERR(ahub->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(ahub->regmap);
	}

	regcache_cache_only(ahub->regmap, true);

	err = devm_snd_soc_register_component(&pdev->dev,
					      ahub->soc_data->cmpnt_drv,
					      ahub->soc_data->dai_drv,
					      ahub->soc_data->num_dais);
	if (err) {
		dev_err(&pdev->dev, "can't register AHUB component, err: %d\n",
			err);
		return err;
	}

	pm_runtime_enable(&pdev->dev);

	err = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (err) {
		pm_runtime_disable(&pdev->dev);
		return err;
	}

	return 0;
}

static void tegra_ahub_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
}

static const struct dev_pm_ops tegra_ahub_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_ahub_runtime_suspend,
			   tegra_ahub_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver tegra_ahub_driver = {
	.probe = tegra_ahub_probe,
	.remove = tegra_ahub_remove,
	.driver = {
		.name = "tegra210-ahub",
		.of_match_table = tegra_ahub_of_match,
		.pm = &tegra_ahub_pm_ops,
	},
};
module_platform_driver(tegra_ahub_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_AUTHOR("Mohan Kumar <mkumard@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 ASoC AHUB driver");
MODULE_LICENSE("GPL v2");
