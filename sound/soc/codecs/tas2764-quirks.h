/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __TAS2764_QUIRKS__
#define __TAS2764_QUIRKS__

#include <linux/regmap.h>

#include "tas2764.h"

/* Bitmask of enabled Apple quirks */
#define ENABLED_APPLE_QUIRKS	0x3f

/*
 * Disable noise gate and flip down reserved bit in NS_CFG0
 */
#define TAS2764_NOISE_GATE_DISABLE	BIT(0)

static const struct reg_sequence tas2764_noise_gate_dis_seq[] = {
	REG_SEQ0(TAS2764_REG(0x0, 0x35), 0xb0)
};

/*
 * CONV_VBAT_PVDD_MODE=1
 */
#define TAS2764_CONV_VBAT_PVDD_MODE	BIT(1)

static const struct reg_sequence tas2764_conv_vbat_pvdd_mode_seq[] = {
	REG_SEQ0(TAS2764_REG(0x0, 0x6b), 0x41)
};

/*
 * Reset of DAC modulator when DSP is OFF
 */
#define TAS2764_DMOD_RST		BIT(2)

static const struct reg_sequence tas2764_dmod_rst_seq[] = {
	REG_SEQ0(TAS2764_REG(0x0, 0x76), 0x0)
};

/*
 * Unknown 0x133/0x137 writes (maybe TDM related)
 */
#define TAS2764_UNK_SEQ0		BIT(3)

static const struct reg_sequence tas2764_unk_seq0[] = {
	REG_SEQ0(TAS2764_REG(0x1, 0x33), 0x80),
	REG_SEQ0(TAS2764_REG(0x1, 0x37), 0x3a),
};

/*
 * Unknown 0x614 - 0x61f writes
 */
#define TAS2764_APPLE_UNK_SEQ1		BIT(4)

static const struct reg_sequence tas2764_unk_seq1[] = {
	REG_SEQ0(TAS2764_REG(0x6, 0x14), 0x0),
	REG_SEQ0(TAS2764_REG(0x6, 0x15), 0x13),
	REG_SEQ0(TAS2764_REG(0x6, 0x16), 0x52),
	REG_SEQ0(TAS2764_REG(0x6, 0x17), 0x0),
	REG_SEQ0(TAS2764_REG(0x6, 0x18), 0xe4),
	REG_SEQ0(TAS2764_REG(0x6, 0x19), 0xc),
	REG_SEQ0(TAS2764_REG(0x6, 0x16), 0xaa),
	REG_SEQ0(TAS2764_REG(0x6, 0x1b), 0x0),
	REG_SEQ0(TAS2764_REG(0x6, 0x1c), 0x12),
	REG_SEQ0(TAS2764_REG(0x6, 0x1d), 0xa0),
	REG_SEQ0(TAS2764_REG(0x6, 0x1e), 0xd8),
	REG_SEQ0(TAS2764_REG(0x6, 0x1f), 0x0),
};

/*
 * Unknown writes in the 0xfd page (with secondary paging inside)
 */
#define TAS2764_APPLE_UNK_SEQ2		BIT(5)

static const struct reg_sequence tas2764_unk_seq2[] = {
	REG_SEQ0(TAS2764_REG(0xfd, 0x0d), 0xd),
	REG_SEQ0(TAS2764_REG(0xfd, 0x6c), 0x2),
	REG_SEQ0(TAS2764_REG(0xfd, 0x6d), 0xf),
	REG_SEQ0(TAS2764_REG(0xfd, 0x0d), 0x0),
};

/*
 * Disable 'Thermal Threshold 1'
 */
#define TAS2764_THERMAL_TH1_DISABLE	BIT(6)

static const struct reg_sequence tas2764_thermal_th1_dis_seq[] = {
	REG_SEQ0(TAS2764_REG(0x1, 0x47), 0x2),
};

/*
 * Imitate Apple's shutdown dance
 */
#define TAS2764_SHUTDOWN_DANCE		BIT(7)

static const struct reg_sequence tas2764_shutdown_dance_init_seq[] = {
	/*
	 * SDZ_MODE=01 (immediate)
	 *
	 * We want the shutdown to happen under the influence of
	 * the magic writes in the 0xfdXX region, so make sure
	 * the shutdown is immediate and there's no grace period
	 * followed by the codec part.
	 */
	REG_SEQ0(TAS2764_REG(0x0, 0x7), 0x60),
};

static const struct reg_sequence tas2764_pre_shutdown_seq[] = {
	REG_SEQ0(TAS2764_REG(0xfd, 0x0d), 0xd), /* switch hidden page */
	REG_SEQ0(TAS2764_REG(0xfd, 0x64), 0x4), /* do write (unknown semantics) */
	REG_SEQ0(TAS2764_REG(0xfd, 0x0d), 0x0), /* switch hidden page back */
};

static const struct reg_sequence tas2764_post_shutdown_seq[] = {
	REG_SEQ0(TAS2764_REG(0xfd, 0x0d), 0xd),
	REG_SEQ0(TAS2764_REG(0xfd, 0x64), 0x0), /* revert write from pre sequence */
	REG_SEQ0(TAS2764_REG(0xfd, 0x0d), 0x0),
};

static int tas2764_do_quirky_pwr_ctrl_change(struct tas2764_priv *tas2764,
					     unsigned int target)
{
	unsigned int curr;
	int ret;

	curr = snd_soc_component_read_field(tas2764->component,
					       TAS2764_PWR_CTRL,
					       TAS2764_PWR_CTRL_MASK);

	if (target == curr)
		return 0;

	/* Handle power state transition to shutdown */
	if (target == TAS2764_PWR_CTRL_SHUTDOWN &&
	   (curr == TAS2764_PWR_CTRL_MUTE || curr == TAS2764_PWR_CTRL_ACTIVE)) {
		ret = regmap_multi_reg_write(tas2764->regmap, tas2764_pre_shutdown_seq,
					ARRAY_SIZE(tas2764_pre_shutdown_seq));
		if (!ret)
			ret = snd_soc_component_update_bits(tas2764->component,
							TAS2764_PWR_CTRL,
							TAS2764_PWR_CTRL_MASK,
							TAS2764_PWR_CTRL_SHUTDOWN);
		if (!ret)
			ret = regmap_multi_reg_write(tas2764->regmap,
						tas2764_post_shutdown_seq,
						ARRAY_SIZE(tas2764_post_shutdown_seq));
	}

	ret = snd_soc_component_update_bits(tas2764->component, TAS2764_PWR_CTRL,
						    TAS2764_PWR_CTRL_MASK, target);

	return ret;
}

/*
 * Via devicetree (TODO):
 *  - switch from spread spectrum to class-D switching
 *  - disable edge control
 *  - set BOP settings (the BOP config bits *and* BOP_SRC)
 */

/*
 * Other setup TODOs:
 *  - DVC ramp rate
 */

static const struct tas2764_quirk_init_sequence {
	const struct reg_sequence *seq;
	int len;
} tas2764_quirk_init_sequences[] = {
	{ tas2764_noise_gate_dis_seq, ARRAY_SIZE(tas2764_noise_gate_dis_seq) },
	{ tas2764_dmod_rst_seq, ARRAY_SIZE(tas2764_dmod_rst_seq) },
	{ tas2764_conv_vbat_pvdd_mode_seq, ARRAY_SIZE(tas2764_conv_vbat_pvdd_mode_seq) },
	{ tas2764_unk_seq0, ARRAY_SIZE(tas2764_unk_seq0) },
	{ tas2764_unk_seq1, ARRAY_SIZE(tas2764_unk_seq1) },
	{ tas2764_unk_seq2, ARRAY_SIZE(tas2764_unk_seq2) },
	{ tas2764_thermal_th1_dis_seq, ARRAY_SIZE(tas2764_thermal_th1_dis_seq) },
	{ tas2764_shutdown_dance_init_seq, ARRAY_SIZE(tas2764_shutdown_dance_init_seq) },
};

#endif /* __TAS2764_QUIRKS__ */
