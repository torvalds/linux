// SPDX-License-Identifier: GPL-2.0-only
//
// Components shared between ASoC and HDA CS35L56 drivers
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <kunit/static_stub.h>
#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/kstrtox.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <linux/types.h>
#include <sound/cs-amp-lib.h>

#include "cs35l56.h"

static const struct reg_sequence cs35l56_patch[] = {
	/*
	 * Firmware can change these to non-defaults to satisfy SDCA.
	 * Ensure that they are at known defaults.
	 */
	{ CS35L56_ASP1_ENABLES1,		0x00000000 },
	{ CS35L56_ASP1_CONTROL1,		0x00000028 },
	{ CS35L56_ASP1_CONTROL2,		0x18180200 },
	{ CS35L56_ASP1_CONTROL3,		0x00000002 },
	{ CS35L56_ASP1_FRAME_CONTROL1,		0x03020100 },
	{ CS35L56_ASP1_FRAME_CONTROL5,		0x00020100 },
	{ CS35L56_ASP1_DATA_CONTROL1,		0x00000018 },
	{ CS35L56_ASP1_DATA_CONTROL5,		0x00000018 },
	{ CS35L56_ASP1TX1_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX2_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX3_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX4_INPUT,		0x00000000 },
	{ CS35L56_SWIRE_DP3_CH1_INPUT,		0x00000018 },
	{ CS35L56_SWIRE_DP3_CH2_INPUT,		0x00000019 },
	{ CS35L56_SWIRE_DP3_CH3_INPUT,		0x00000029 },
	{ CS35L56_SWIRE_DP3_CH4_INPUT,		0x00000028 },
	{ CS35L56_IRQ1_MASK_18,			0x1f7df0ff },
};

static const struct reg_sequence cs35l56_patch_fw[] = {
	/* These are not reset by a soft-reset, so patch to defaults. */
	{ CS35L56_MAIN_RENDER_USER_MUTE,	0x00000000 },
	{ CS35L56_MAIN_RENDER_USER_VOLUME,	0x00000000 },
	{ CS35L56_MAIN_POSTURE_NUMBER,		0x00000000 },
};

static const struct reg_sequence cs35l63_patch_fw[] = {
	/* These are not reset by a soft-reset, so patch to defaults. */
	{ CS35L63_MAIN_RENDER_USER_MUTE,	0x00000000 },
	{ CS35L63_MAIN_RENDER_USER_VOLUME,	0x00000000 },
	{ CS35L63_MAIN_POSTURE_NUMBER,		0x00000000 },
};

int cs35l56_set_patch(struct cs35l56_base *cs35l56_base)
{
	int ret;

	ret = regmap_register_patch(cs35l56_base->regmap, cs35l56_patch,
				     ARRAY_SIZE(cs35l56_patch));
	if (ret)
		return ret;


	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		ret = regmap_register_patch(cs35l56_base->regmap, cs35l56_patch_fw,
					    ARRAY_SIZE(cs35l56_patch_fw));
		break;
	case 0x63:
		ret = regmap_register_patch(cs35l56_base->regmap, cs35l63_patch_fw,
					    ARRAY_SIZE(cs35l63_patch_fw));
		break;
	default:
		break;
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_set_patch, "SND_SOC_CS35L56_SHARED");

static const struct reg_default cs35l56_reg_defaults[] = {
	/* no defaults for OTP_MEM - first read populates cache */

	{ CS35L56_ASP1_ENABLES1,		0x00000000 },
	{ CS35L56_ASP1_CONTROL1,		0x00000028 },
	{ CS35L56_ASP1_CONTROL2,		0x18180200 },
	{ CS35L56_ASP1_CONTROL3,		0x00000002 },
	{ CS35L56_ASP1_FRAME_CONTROL1,		0x03020100 },
	{ CS35L56_ASP1_FRAME_CONTROL5,		0x00020100 },
	{ CS35L56_ASP1_DATA_CONTROL1,		0x00000018 },
	{ CS35L56_ASP1_DATA_CONTROL5,		0x00000018 },
	{ CS35L56_ASP1TX1_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX2_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX3_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX4_INPUT,		0x00000000 },
	{ CS35L56_SWIRE_DP3_CH1_INPUT,		0x00000018 },
	{ CS35L56_SWIRE_DP3_CH2_INPUT,		0x00000019 },
	{ CS35L56_SWIRE_DP3_CH3_INPUT,		0x00000029 },
	{ CS35L56_SWIRE_DP3_CH4_INPUT,		0x00000028 },
	{ CS35L56_IRQ1_MASK_1,			0x83ffffff },
	{ CS35L56_IRQ1_MASK_2,			0xffff7fff },
	{ CS35L56_IRQ1_MASK_4,			0xe0ffffff },
	{ CS35L56_IRQ1_MASK_8,			0xfc000fff },
	{ CS35L56_IRQ1_MASK_18,			0x1f7df0ff },
	{ CS35L56_IRQ1_MASK_20,			0x15c00000 },
	{ CS35L56_MAIN_RENDER_USER_MUTE,	0x00000000 },
	{ CS35L56_MAIN_RENDER_USER_VOLUME,	0x00000000 },
	{ CS35L56_MAIN_POSTURE_NUMBER,		0x00000000 },
};

static const struct reg_default cs35l63_reg_defaults[] = {
	/* no defaults for OTP_MEM - first read populates cache */

	{ CS35L56_ASP1_ENABLES1,		0x00000000 },
	{ CS35L56_ASP1_CONTROL1,		0x00000028 },
	{ CS35L56_ASP1_CONTROL2,		0x18180200 },
	{ CS35L56_ASP1_CONTROL3,		0x00000002 },
	{ CS35L56_ASP1_FRAME_CONTROL1,		0x03020100 },
	{ CS35L56_ASP1_FRAME_CONTROL5,		0x00020100 },
	{ CS35L56_ASP1_DATA_CONTROL1,		0x00000018 },
	{ CS35L56_ASP1_DATA_CONTROL5,		0x00000018 },
	{ CS35L56_ASP1TX1_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX2_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX3_INPUT,		0x00000000 },
	{ CS35L56_ASP1TX4_INPUT,		0x00000000 },
	{ CS35L56_SWIRE_DP3_CH1_INPUT,		0x00000018 },
	{ CS35L56_SWIRE_DP3_CH2_INPUT,		0x00000019 },
	{ CS35L56_SWIRE_DP3_CH3_INPUT,		0x00000029 },
	{ CS35L56_SWIRE_DP3_CH4_INPUT,		0x00000028 },
	{ CS35L56_IRQ1_MASK_1,			0x8003ffff },
	{ CS35L56_IRQ1_MASK_2,			0xffff7fff },
	{ CS35L56_IRQ1_MASK_4,			0xe0ffffff },
	{ CS35L56_IRQ1_MASK_8,			0x8c000fff },
	{ CS35L56_IRQ1_MASK_18,			0x0760f000 },
	{ CS35L56_IRQ1_MASK_20,			0x15c00000 },
	{ CS35L63_MAIN_RENDER_USER_MUTE,	0x00000000 },
	{ CS35L63_MAIN_RENDER_USER_VOLUME,	0x00000000 },
	{ CS35L63_MAIN_POSTURE_NUMBER,		0x00000000 },
};

static bool cs35l56_is_dsp_memory(unsigned int reg)
{
	switch (reg) {
	case CS35L56_DSP1_XMEM_PACKED_0 ... CS35L56_DSP1_XMEM_PACKED_6143:
	case CS35L56_DSP1_XMEM_UNPACKED32_0 ... CS35L56_DSP1_XMEM_UNPACKED32_4095:
	case CS35L56_DSP1_XMEM_UNPACKED24_0 ... CS35L56_DSP1_XMEM_UNPACKED24_8191:
	case CS35L56_DSP1_YMEM_PACKED_0 ... CS35L56_DSP1_YMEM_PACKED_4604:
	case CS35L56_DSP1_YMEM_UNPACKED32_0 ... CS35L56_DSP1_YMEM_UNPACKED32_3070:
	case CS35L56_DSP1_YMEM_UNPACKED24_0 ... CS35L56_DSP1_YMEM_UNPACKED24_6141:
	case CS35L56_DSP1_PMEM_0 ... CS35L56_DSP1_PMEM_5114:
		return true;
	default:
		return false;
	}
}

static bool cs35l56_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L56_DEVID:
	case CS35L56_REVID:
	case CS35L56_RELID:
	case CS35L56_OTPID:
	case CS35L56_SFT_RESET:
	case CS35L56_GLOBAL_ENABLES:
	case CS35L56_BLOCK_ENABLES:
	case CS35L56_BLOCK_ENABLES2:
	case CS35L56_REFCLK_INPUT:
	case CS35L56_GLOBAL_SAMPLE_RATE:
	case CS35L56_OTP_MEM_53:
	case CS35L56_OTP_MEM_54:
	case CS35L56_OTP_MEM_55:
	case CS35L56_SYNC_GPIO1_CFG ... CS35L56_ASP2_DIO_GPIO13_CFG:
	case CS35L56_UPDATE_REGS:
	case CS35L56_ASP1_ENABLES1:
	case CS35L56_ASP1_CONTROL1:
	case CS35L56_ASP1_CONTROL2:
	case CS35L56_ASP1_CONTROL3:
	case CS35L56_ASP1_FRAME_CONTROL1:
	case CS35L56_ASP1_FRAME_CONTROL5:
	case CS35L56_ASP1_DATA_CONTROL1:
	case CS35L56_ASP1_DATA_CONTROL5:
	case CS35L56_DACPCM1_INPUT:
	case CS35L56_DACPCM2_INPUT:
	case CS35L56_ASP1TX1_INPUT:
	case CS35L56_ASP1TX2_INPUT:
	case CS35L56_ASP1TX3_INPUT:
	case CS35L56_ASP1TX4_INPUT:
	case CS35L56_DSP1RX1_INPUT:
	case CS35L56_DSP1RX2_INPUT:
	case CS35L56_SWIRE_DP3_CH1_INPUT:
	case CS35L56_SWIRE_DP3_CH2_INPUT:
	case CS35L56_SWIRE_DP3_CH3_INPUT:
	case CS35L56_SWIRE_DP3_CH4_INPUT:
	case CS35L56_IRQ1_CFG:
	case CS35L56_IRQ1_STATUS:
	case CS35L56_IRQ1_EINT_1 ... CS35L56_IRQ1_EINT_8:
	case CS35L56_IRQ1_EINT_18:
	case CS35L56_IRQ1_EINT_20:
	case CS35L56_IRQ1_MASK_1:
	case CS35L56_IRQ1_MASK_2:
	case CS35L56_IRQ1_MASK_4:
	case CS35L56_IRQ1_MASK_8:
	case CS35L56_IRQ1_MASK_18:
	case CS35L56_IRQ1_MASK_20:
	case CS35L56_GPIO_STATUS1 ... CS35L56_GPIO13_CTRL1:
	case CS35L56_MIXER_NGATE_CH1_CFG:
	case CS35L56_MIXER_NGATE_CH2_CFG:
	case CS35L56_DSP_VIRTUAL1_MBOX_1:
	case CS35L56_DSP_VIRTUAL1_MBOX_2:
	case CS35L56_DSP_VIRTUAL1_MBOX_3:
	case CS35L56_DSP_VIRTUAL1_MBOX_4:
	case CS35L56_DSP_VIRTUAL1_MBOX_5:
	case CS35L56_DSP_VIRTUAL1_MBOX_6:
	case CS35L56_DSP_VIRTUAL1_MBOX_7:
	case CS35L56_DSP_VIRTUAL1_MBOX_8:
	case CS35L56_DIE_STS1:
	case CS35L56_DIE_STS2:
	case CS35L56_DSP_RESTRICT_STS1:
	case CS35L56_DSP1_SYS_INFO_ID ... CS35L56_DSP1_SYS_INFO_END:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_0:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_1:
	case CS35L56_DSP1_SCRATCH1:
	case CS35L56_DSP1_SCRATCH2:
	case CS35L56_DSP1_SCRATCH3:
	case CS35L56_DSP1_SCRATCH4:
		return true;
	default:
		return cs35l56_is_dsp_memory(reg);
	}
}

static bool cs35l56_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L56_DSP1_XMEM_PACKED_0 ... CS35L56_DSP1_XMEM_PACKED_6143:
	case CS35L56_DSP1_YMEM_PACKED_0 ... CS35L56_DSP1_YMEM_PACKED_4604:
	case CS35L56_DSP1_PMEM_0 ... CS35L56_DSP1_PMEM_5114:
		return true;
	default:
		return false;
	}
}

static bool cs35l56_common_volatile_reg(unsigned int reg)
{
	switch (reg) {
	case CS35L56_DEVID:
	case CS35L56_REVID:
	case CS35L56_RELID:
	case CS35L56_OTPID:
	case CS35L56_SFT_RESET:
	case CS35L56_GLOBAL_ENABLES:		   /* owned by firmware */
	case CS35L56_BLOCK_ENABLES:		   /* owned by firmware */
	case CS35L56_BLOCK_ENABLES2:		   /* owned by firmware */
	case CS35L56_SYNC_GPIO1_CFG ... CS35L56_ASP2_DIO_GPIO13_CFG:
	case CS35L56_UPDATE_REGS:
	case CS35L56_REFCLK_INPUT:		   /* owned by firmware */
	case CS35L56_GLOBAL_SAMPLE_RATE:	   /* owned by firmware */
	case CS35L56_DACPCM1_INPUT:		   /* owned by firmware */
	case CS35L56_DACPCM2_INPUT:		   /* owned by firmware */
	case CS35L56_DSP1RX1_INPUT:		   /* owned by firmware */
	case CS35L56_DSP1RX2_INPUT:		   /* owned by firmware */
	case CS35L56_IRQ1_STATUS:
	case CS35L56_IRQ1_EINT_1 ... CS35L56_IRQ1_EINT_8:
	case CS35L56_IRQ1_EINT_18:
	case CS35L56_IRQ1_EINT_20:
	case CS35L56_GPIO_STATUS1 ... CS35L56_GPIO13_CTRL1:
	case CS35L56_MIXER_NGATE_CH1_CFG:
	case CS35L56_MIXER_NGATE_CH2_CFG:
	case CS35L56_DSP_VIRTUAL1_MBOX_1:
	case CS35L56_DSP_VIRTUAL1_MBOX_2:
	case CS35L56_DSP_VIRTUAL1_MBOX_3:
	case CS35L56_DSP_VIRTUAL1_MBOX_4:
	case CS35L56_DSP_VIRTUAL1_MBOX_5:
	case CS35L56_DSP_VIRTUAL1_MBOX_6:
	case CS35L56_DSP_VIRTUAL1_MBOX_7:
	case CS35L56_DSP_VIRTUAL1_MBOX_8:
	case CS35L56_DSP_RESTRICT_STS1:
	case CS35L56_DSP1_SYS_INFO_ID ... CS35L56_DSP1_SYS_INFO_END:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_0:
	case CS35L56_DSP1_AHBM_WINDOW_DEBUG_1:
	case CS35L56_DSP1_SCRATCH1:
	case CS35L56_DSP1_SCRATCH2:
	case CS35L56_DSP1_SCRATCH3:
	case CS35L56_DSP1_SCRATCH4:
		return true;
	default:
		return cs35l56_is_dsp_memory(reg);
	}
}

static bool cs35l56_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L56_MAIN_RENDER_USER_MUTE:
	case CS35L56_MAIN_RENDER_USER_VOLUME:
	case CS35L56_MAIN_POSTURE_NUMBER:
		return false;
	default:
		return cs35l56_common_volatile_reg(reg);
	}
}

static bool cs35l63_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS35L63_MAIN_RENDER_USER_MUTE:
	case CS35L63_MAIN_RENDER_USER_VOLUME:
	case CS35L63_MAIN_POSTURE_NUMBER:
		return false;
	default:
		return cs35l56_common_volatile_reg(reg);
	}
}

static const struct cs35l56_fw_reg cs35l56_fw_reg = {
	.fw_ver = CS35L56_DSP1_FW_VER,
	.halo_state = CS35L56_DSP1_HALO_STATE,
	.pm_cur_stat = CS35L56_DSP1_PM_CUR_STATE,
	.prot_sts = CS35L56_PROTECTION_STATUS,
	.transducer_actual_ps = CS35L56_TRANSDUCER_ACTUAL_PS,
	.user_mute = CS35L56_MAIN_RENDER_USER_MUTE,
	.user_volume = CS35L56_MAIN_RENDER_USER_VOLUME,
	.posture_number = CS35L56_MAIN_POSTURE_NUMBER,
};

static const struct cs35l56_fw_reg cs35l56_b2_fw_reg = {
	.fw_ver = CS35L56_DSP1_FW_VER,
	.halo_state = CS35L56_B2_DSP1_HALO_STATE,
	.pm_cur_stat = CS35L56_B2_DSP1_PM_CUR_STATE,
	.prot_sts = CS35L56_PROTECTION_STATUS,
	.transducer_actual_ps = CS35L56_TRANSDUCER_ACTUAL_PS,
	.user_mute = CS35L56_MAIN_RENDER_USER_MUTE,
	.user_volume = CS35L56_MAIN_RENDER_USER_VOLUME,
	.posture_number = CS35L56_MAIN_POSTURE_NUMBER,
};

static const struct cs35l56_fw_reg cs35l63_fw_reg = {
	.fw_ver = CS35L63_DSP1_FW_VER,
	.halo_state = CS35L63_DSP1_HALO_STATE,
	.pm_cur_stat = CS35L63_DSP1_PM_CUR_STATE,
	.prot_sts = CS35L63_PROTECTION_STATUS,
	.transducer_actual_ps = CS35L63_TRANSDUCER_ACTUAL_PS,
	.user_mute = CS35L63_MAIN_RENDER_USER_MUTE,
	.user_volume = CS35L63_MAIN_RENDER_USER_VOLUME,
	.posture_number = CS35L63_MAIN_POSTURE_NUMBER,
};

static void cs35l56_set_fw_reg_table(struct cs35l56_base *cs35l56_base)
{
	switch (cs35l56_base->type) {
	default:
		switch (cs35l56_base->rev) {
		case 0xb0:
			cs35l56_base->fw_reg = &cs35l56_fw_reg;
			break;
		default:
			cs35l56_base->fw_reg = &cs35l56_b2_fw_reg;
			break;
		}
		break;
	case 0x63:
		cs35l56_base->fw_reg = &cs35l63_fw_reg;
		break;
	}
}

int cs35l56_mbox_send(struct cs35l56_base *cs35l56_base, unsigned int command)
{
	unsigned int val;
	int ret;

	regmap_write(cs35l56_base->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1, command);
	ret = regmap_read_poll_timeout(cs35l56_base->regmap, CS35L56_DSP_VIRTUAL1_MBOX_1,
				       val, (val == 0),
				       CS35L56_MBOX_POLL_US, CS35L56_MBOX_TIMEOUT_US);
	if (ret) {
		dev_warn(cs35l56_base->dev, "MBOX command %#x failed: %d\n", command, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_mbox_send, "SND_SOC_CS35L56_SHARED");

int cs35l56_firmware_shutdown(struct cs35l56_base *cs35l56_base)
{
	int ret;
	unsigned int val;

	ret = cs35l56_mbox_send(cs35l56_base, CS35L56_MBOX_CMD_SHUTDOWN);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(cs35l56_base->regmap,
				       cs35l56_base->fw_reg->pm_cur_stat,
				       val, (val == CS35L56_HALO_STATE_SHUTDOWN),
				       CS35L56_HALO_STATE_POLL_US,
				       CS35L56_HALO_STATE_TIMEOUT_US);
	if (ret < 0)
		dev_err(cs35l56_base->dev, "Failed to poll PM_CUR_STATE to 1 is %d (ret %d)\n",
			val, ret);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_firmware_shutdown, "SND_SOC_CS35L56_SHARED");

int cs35l56_wait_for_firmware_boot(struct cs35l56_base *cs35l56_base)
{
	unsigned int val = 0;
	int read_ret, poll_ret;

	/*
	 * The regmap must remain in cache-only until the chip has
	 * booted, so use a bypassed read of the status register.
	 */
	poll_ret = read_poll_timeout(regmap_read_bypassed, read_ret,
				     (val < 0xFFFF) && (val >= CS35L56_HALO_STATE_BOOT_DONE),
				     CS35L56_HALO_STATE_POLL_US,
				     CS35L56_HALO_STATE_TIMEOUT_US,
				     false,
				     cs35l56_base->regmap,
				     cs35l56_base->fw_reg->halo_state,
				     &val);

	if (poll_ret) {
		dev_err(cs35l56_base->dev, "Firmware boot timed out(%d): HALO_STATE=%#x\n",
			read_ret, val);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_wait_for_firmware_boot, "SND_SOC_CS35L56_SHARED");

void cs35l56_wait_control_port_ready(void)
{
	/* Wait for control port to be ready (datasheet tIRS). */
	usleep_range(CS35L56_CONTROL_PORT_READY_US, 2 * CS35L56_CONTROL_PORT_READY_US);
}
EXPORT_SYMBOL_NS_GPL(cs35l56_wait_control_port_ready, "SND_SOC_CS35L56_SHARED");

void cs35l56_wait_min_reset_pulse(void)
{
	/* Satisfy minimum reset pulse width spec */
	usleep_range(CS35L56_RESET_PULSE_MIN_US, 2 * CS35L56_RESET_PULSE_MIN_US);
}
EXPORT_SYMBOL_NS_GPL(cs35l56_wait_min_reset_pulse, "SND_SOC_CS35L56_SHARED");

static const struct {
	u32 addr;
	u32 value;
} cs35l56_spi_system_reset_stages[] = {
	{ .addr = CS35L56_DSP_VIRTUAL1_MBOX_1, .value = CS35L56_MBOX_CMD_SYSTEM_RESET },
	/* The next write is necessary to delimit the soft reset */
	{ .addr = CS35L56_DSP_MBOX_1_RAW, .value = CS35L56_MBOX_CMD_PING },
};

static void cs35l56_spi_issue_bus_locked_reset(struct cs35l56_base *cs35l56_base,
					       struct spi_device *spi)
{
	struct cs35l56_spi_payload *buf = cs35l56_base->spi_payload_buf;
	struct spi_transfer t = {
		.tx_buf		= buf,
		.len		= sizeof(*buf),
	};
	struct spi_message m;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(cs35l56_spi_system_reset_stages); i++) {
		buf->addr = cpu_to_be32(cs35l56_spi_system_reset_stages[i].addr);
		buf->value = cpu_to_be32(cs35l56_spi_system_reset_stages[i].value);
		spi_message_init_with_transfers(&m, &t, 1);
		ret = spi_sync_locked(spi, &m);
		if (ret)
			dev_warn(cs35l56_base->dev, "spi_sync failed: %d\n", ret);

		usleep_range(CS35L56_SPI_RESET_TO_PORT_READY_US,
			     2 * CS35L56_SPI_RESET_TO_PORT_READY_US);
	}
}

static void cs35l56_spi_system_reset(struct cs35l56_base *cs35l56_base)
{
	struct spi_device *spi = to_spi_device(cs35l56_base->dev);
	unsigned int val;
	int read_ret, ret;

	/*
	 * There must not be any other SPI bus activity while the amp is
	 * soft-resetting.
	 */
	ret = spi_bus_lock(spi->controller);
	if (ret) {
		dev_warn(cs35l56_base->dev, "spi_bus_lock failed: %d\n", ret);
		return;
	}

	cs35l56_spi_issue_bus_locked_reset(cs35l56_base, spi);
	spi_bus_unlock(spi->controller);

	/*
	 * Check firmware boot by testing for a response in MBOX_2.
	 * HALO_STATE cannot be trusted yet because the reset sequence
	 * can leave it with stale state. But MBOX is reset.
	 * The regmap must remain in cache-only until the chip has
	 * booted, so use a bypassed read.
	 */
	ret = read_poll_timeout(regmap_read_bypassed, read_ret,
				(val > 0) && (val < 0xffffffff),
				CS35L56_HALO_STATE_POLL_US,
				CS35L56_HALO_STATE_TIMEOUT_US,
				false,
				cs35l56_base->regmap,
				CS35L56_DSP_VIRTUAL1_MBOX_2,
				&val);
	if (ret) {
		dev_err(cs35l56_base->dev, "SPI reboot timed out(%d): MBOX2=%#x\n",
			read_ret, val);
	}
}

static const struct reg_sequence cs35l56_system_reset_seq[] = {
	REG_SEQ0(CS35L56_DSP1_HALO_STATE, 0),
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_SYSTEM_RESET),
};

static const struct reg_sequence cs35l56_b2_system_reset_seq[] = {
	REG_SEQ0(CS35L56_B2_DSP1_HALO_STATE, 0),
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_SYSTEM_RESET),
};

static const struct reg_sequence cs35l63_system_reset_seq[] = {
	REG_SEQ0(CS35L63_DSP1_HALO_STATE, 0),
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_SYSTEM_RESET),
};

void cs35l56_system_reset(struct cs35l56_base *cs35l56_base, bool is_soundwire)
{
	/*
	 * Must enter cache-only first so there can't be any more register
	 * accesses other than the controlled system reset sequence below.
	 */
	regcache_cache_only(cs35l56_base->regmap, true);

	if (cs35l56_is_spi(cs35l56_base)) {
		cs35l56_spi_system_reset(cs35l56_base);
		return;
	}

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		switch (cs35l56_base->rev) {
		case 0xb0:
			regmap_multi_reg_write_bypassed(cs35l56_base->regmap,
							cs35l56_system_reset_seq,
							ARRAY_SIZE(cs35l56_system_reset_seq));
			break;
		default:
			regmap_multi_reg_write_bypassed(cs35l56_base->regmap,
							cs35l56_b2_system_reset_seq,
							ARRAY_SIZE(cs35l56_b2_system_reset_seq));
			break;
		}
		break;
	case 0x63:
		regmap_multi_reg_write_bypassed(cs35l56_base->regmap,
						cs35l63_system_reset_seq,
						ARRAY_SIZE(cs35l63_system_reset_seq));
		break;
	default:
		break;
	}

	/* On SoundWire the registers won't be accessible until it re-enumerates. */
	if (is_soundwire)
		return;

	cs35l56_wait_control_port_ready();

	/* Leave in cache-only. This will be revoked when the chip has rebooted. */
}
EXPORT_SYMBOL_NS_GPL(cs35l56_system_reset, "SND_SOC_CS35L56_SHARED");

int cs35l56_irq_request(struct cs35l56_base *cs35l56_base, int irq)
{
	int ret;

	if (irq < 1)
		return 0;

	ret = devm_request_threaded_irq(cs35l56_base->dev, irq, NULL, cs35l56_irq,
					IRQF_ONESHOT | IRQF_SHARED | IRQF_TRIGGER_LOW,
					"cs35l56", cs35l56_base);
	if (!ret)
		cs35l56_base->irq = irq;
	else
		dev_err(cs35l56_base->dev, "Failed to get IRQ: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_irq_request, "SND_SOC_CS35L56_SHARED");

irqreturn_t cs35l56_irq(int irq, void *data)
{
	struct cs35l56_base *cs35l56_base = data;
	unsigned int status1 = 0, status8 = 0, status20 = 0;
	unsigned int mask1, mask8, mask20;
	unsigned int val;
	int rv;

	irqreturn_t ret = IRQ_NONE;

	if (!cs35l56_base->init_done)
		return IRQ_NONE;

	mutex_lock(&cs35l56_base->irq_lock);

	rv = pm_runtime_resume_and_get(cs35l56_base->dev);
	if (rv < 0) {
		dev_err(cs35l56_base->dev, "irq: failed to get pm_runtime: %d\n", rv);
		goto err_unlock;
	}

	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_STATUS, &val);
	if ((val & CS35L56_IRQ1_STS_MASK) == 0) {
		dev_dbg(cs35l56_base->dev, "Spurious IRQ: no pending interrupt\n");
		goto err;
	}

	/* Ack interrupts */
	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_EINT_1, &status1);
	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_MASK_1, &mask1);
	status1 &= ~mask1;
	regmap_write(cs35l56_base->regmap, CS35L56_IRQ1_EINT_1, status1);

	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_EINT_8, &status8);
	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_MASK_8, &mask8);
	status8 &= ~mask8;
	regmap_write(cs35l56_base->regmap, CS35L56_IRQ1_EINT_8, status8);

	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_EINT_20, &status20);
	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_MASK_20, &mask20);
	status20 &= ~mask20;
	/* We don't want EINT20 but they default to unmasked: force mask */
	regmap_write(cs35l56_base->regmap, CS35L56_IRQ1_MASK_20, 0xffffffff);

	dev_dbg(cs35l56_base->dev, "%s: %#x %#x\n", __func__, status1, status8);

	/* Check to see if unmasked bits are active */
	if (!status1 && !status8 && !status20)
		goto err;

	if (status1 & CS35L56_AMP_SHORT_ERR_EINT1_MASK)
		dev_crit(cs35l56_base->dev, "Amp short error\n");

	if (status8 & CS35L56_TEMP_ERR_EINT1_MASK)
		dev_crit(cs35l56_base->dev, "Overtemp error\n");

	ret = IRQ_HANDLED;

err:
	pm_runtime_put(cs35l56_base->dev);
err_unlock:
	mutex_unlock(&cs35l56_base->irq_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_irq, "SND_SOC_CS35L56_SHARED");

int cs35l56_is_fw_reload_needed(struct cs35l56_base *cs35l56_base)
{
	unsigned int val;
	int ret;

	/*
	 * In secure mode FIRMWARE_MISSING is cleared by the BIOS loader so
	 * can't be used here to test for memory retention.
	 * Assume that tuning must be re-loaded.
	 */
	if (cs35l56_base->secured)
		return true;

	ret = pm_runtime_resume_and_get(cs35l56_base->dev);
	if (ret) {
		dev_err(cs35l56_base->dev, "Failed to runtime_get: %d\n", ret);
		return ret;
	}

	ret = regmap_read(cs35l56_base->regmap,
			  cs35l56_base->fw_reg->prot_sts,
			  &val);
	if (ret)
		dev_err(cs35l56_base->dev, "Failed to read PROTECTION_STATUS: %d\n", ret);
	else
		ret = !!(val & CS35L56_FIRMWARE_MISSING);

	pm_runtime_put_autosuspend(cs35l56_base->dev);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_is_fw_reload_needed, "SND_SOC_CS35L56_SHARED");

static const struct reg_sequence cs35l56_hibernate_seq[] = {
	/* This must be the last register access */
	REG_SEQ0(CS35L56_DSP_VIRTUAL1_MBOX_1, CS35L56_MBOX_CMD_ALLOW_AUTO_HIBERNATE),
};

static void cs35l56_issue_wake_event(struct cs35l56_base *cs35l56_base)
{
	unsigned int val;

	/*
	 * Dummy transactions to trigger I2C/SPI auto-wake. Issue two
	 * transactions to meet the minimum required time from the rising edge
	 * to the last falling edge of wake.
	 *
	 * It uses bypassed read because we must wake the chip before
	 * disabling regmap cache-only.
	 */
	regmap_read_bypassed(cs35l56_base->regmap, CS35L56_IRQ1_STATUS, &val);

	usleep_range(CS35L56_WAKE_HOLD_TIME_US, 2 * CS35L56_WAKE_HOLD_TIME_US);

	regmap_read_bypassed(cs35l56_base->regmap, CS35L56_IRQ1_STATUS, &val);

	cs35l56_wait_control_port_ready();
}

static int cs35l56_wait_for_ps3(struct cs35l56_base *cs35l56_base)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(cs35l56_base->regmap,
				       cs35l56_base->fw_reg->transducer_actual_ps,
				       val, (val >= CS35L56_PS3),
				       CS35L56_PS3_POLL_US,
				       CS35L56_PS3_TIMEOUT_US);
	if (ret)
		dev_warn(cs35l56_base->dev, "PS3 wait failed: %d\n", ret);

	return ret;
}

int cs35l56_runtime_suspend_common(struct cs35l56_base *cs35l56_base)
{
	if (!cs35l56_base->init_done)
		return 0;

	/* Firmware must have entered a power-save state */
	cs35l56_wait_for_ps3(cs35l56_base);

	/* Clear BOOT_DONE so it can be used to detect a reboot */
	regmap_write(cs35l56_base->regmap, CS35L56_IRQ1_EINT_4, CS35L56_OTP_BOOT_DONE_MASK);

	if (!cs35l56_base->can_hibernate) {
		regcache_cache_only(cs35l56_base->regmap, true);
		dev_dbg(cs35l56_base->dev, "Suspended: no hibernate");

		return 0;
	}

	/*
	 * Must enter cache-only first so there can't be any more register
	 * accesses other than the controlled hibernate sequence below.
	 */
	regcache_cache_only(cs35l56_base->regmap, true);

	regmap_multi_reg_write_bypassed(cs35l56_base->regmap,
					cs35l56_hibernate_seq,
					ARRAY_SIZE(cs35l56_hibernate_seq));

	dev_dbg(cs35l56_base->dev, "Suspended: hibernate");

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_runtime_suspend_common, "SND_SOC_CS35L56_SHARED");

int cs35l56_runtime_resume_common(struct cs35l56_base *cs35l56_base, bool is_soundwire)
{
	unsigned int val;
	int ret;

	if (!cs35l56_base->init_done)
		return 0;

	if (!cs35l56_base->can_hibernate)
		goto out_sync;

	/* Must be done before releasing cache-only */
	if (!is_soundwire)
		cs35l56_issue_wake_event(cs35l56_base);

out_sync:
	ret = cs35l56_wait_for_firmware_boot(cs35l56_base);
	if (ret) {
		dev_err(cs35l56_base->dev, "Hibernate wake failed: %d\n", ret);
		goto err;
	}

	regcache_cache_only(cs35l56_base->regmap, false);

	ret = cs35l56_mbox_send(cs35l56_base, CS35L56_MBOX_CMD_PREVENT_AUTO_HIBERNATE);
	if (ret)
		goto err;

	/* BOOT_DONE will be 1 if the amp reset */
	regmap_read(cs35l56_base->regmap, CS35L56_IRQ1_EINT_4, &val);
	if (val & CS35L56_OTP_BOOT_DONE_MASK) {
		dev_dbg(cs35l56_base->dev, "Registers reset in suspend\n");
		regcache_mark_dirty(cs35l56_base->regmap);
	}

	regcache_sync(cs35l56_base->regmap);

	dev_dbg(cs35l56_base->dev, "Resumed");

	return 0;

err:
	regcache_cache_only(cs35l56_base->regmap, true);

	regmap_multi_reg_write_bypassed(cs35l56_base->regmap,
					cs35l56_hibernate_seq,
					ARRAY_SIZE(cs35l56_hibernate_seq));

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_runtime_resume_common, "SND_SOC_CS35L56_SHARED");

static const struct cs_dsp_region cs35l56_dsp1_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED,	.base = CS35L56_DSP1_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED,	.base = CS35L56_DSP1_XMEM_PACKED_0 },
	{ .type = WMFW_HALO_YM_PACKED,	.base = CS35L56_DSP1_YMEM_PACKED_0 },
	{ .type = WMFW_ADSP2_XM,	.base = CS35L56_DSP1_XMEM_UNPACKED24_0 },
	{ .type = WMFW_ADSP2_YM,	.base = CS35L56_DSP1_YMEM_UNPACKED24_0 },
};

void cs35l56_init_cs_dsp(struct cs35l56_base *cs35l56_base, struct cs_dsp *cs_dsp)
{
	cs_dsp->num = 1;
	cs_dsp->type = WMFW_HALO;
	cs_dsp->rev = 0;
	cs_dsp->dev = cs35l56_base->dev;
	cs_dsp->regmap = cs35l56_base->regmap;
	cs_dsp->base = CS35L56_DSP1_CORE_BASE;
	cs_dsp->base_sysinfo = CS35L56_DSP1_SYS_INFO_ID;
	cs_dsp->mem = cs35l56_dsp1_regions;
	cs_dsp->num_mems = ARRAY_SIZE(cs35l56_dsp1_regions);
	cs_dsp->no_core_startstop = true;

	cs35l56_base->dsp = cs_dsp;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_init_cs_dsp, "SND_SOC_CS35L56_SHARED");

struct cs35l56_pte {
	u8 x;
	u8 wafer_id;
	u8 pte[2];
	u8 lot[3];
	u8 y;
	u8 unused[3];
	u8 dvs;
} __packed;
static_assert((sizeof(struct cs35l56_pte) % sizeof(u32)) == 0);

static int cs35l56_read_silicon_uid(struct cs35l56_base *cs35l56_base)
{
	struct cs35l56_pte pte;
	u64 unique_id;
	int ret;

	ret = regmap_raw_read(cs35l56_base->regmap, CS35L56_OTP_MEM_53, &pte, sizeof(pte));
	if (ret) {
		dev_err(cs35l56_base->dev, "Failed to read OTP: %d\n", ret);
		return ret;
	}

	unique_id = (u32)pte.lot[2] | ((u32)pte.lot[1] << 8) | ((u32)pte.lot[0] << 16);
	unique_id <<= 32;
	unique_id |= (u32)pte.x | ((u32)pte.y << 8) | ((u32)pte.wafer_id << 16) |
		     ((u32)pte.dvs << 24);

	cs35l56_base->silicon_uid = unique_id;

	return 0;
}

static int cs35l63_read_silicon_uid(struct cs35l56_base *cs35l56_base)
{
	u32 tmp[2];
	u64 unique_id;
	int ret;

	ret = regmap_bulk_read(cs35l56_base->regmap, CS35L56_DIE_STS1, tmp, ARRAY_SIZE(tmp));
	if (ret) {
		dev_err(cs35l56_base->dev, "Cannot obtain CS35L56_DIE_STS: %d\n", ret);
		return ret;
	}

	unique_id = tmp[1];
	unique_id <<= 32;
	unique_id |= tmp[0];

	cs35l56_base->silicon_uid = unique_id;

	return 0;
}

/* Firmware calibration controls */
const struct cirrus_amp_cal_controls cs35l56_calibration_controls = {
	.alg_id =	0x9f210,
	.mem_region =	WMFW_ADSP2_YM,
	.ambient =	"CAL_AMBIENT",
	.calr =		"CAL_R",
	.status =	"CAL_STATUS",
	.checksum =	"CAL_CHECKSUM",
};
EXPORT_SYMBOL_NS_GPL(cs35l56_calibration_controls, "SND_SOC_CS35L56_SHARED");

static const struct cirrus_amp_cal_controls cs35l63_calibration_controls = {
	.alg_id =	0xbf210,
	.mem_region =	WMFW_ADSP2_YM,
	.ambient =	"CAL_AMBIENT",
	.calr =		"CAL_R",
	.status =	"CAL_STATUS",
	.checksum =	"CAL_CHECKSUM",
};

int cs35l56_get_calibration(struct cs35l56_base *cs35l56_base)
{
	int ret;

	/* Driver can't apply calibration to a secured part, so skip */
	if (cs35l56_base->secured)
		return 0;

	ret = cs_amp_get_efi_calibration_data(cs35l56_base->dev,
					      cs35l56_base->silicon_uid,
					      cs35l56_base->cal_index,
					      &cs35l56_base->cal_data);

	/* Only return an error status if probe should be aborted */
	if ((ret == -ENOENT) || (ret == -EOVERFLOW))
		return 0;

	if (ret < 0)
		return ret;

	cs35l56_base->cal_data_valid = true;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_get_calibration, "SND_SOC_CS35L56_SHARED");

int cs35l56_stash_calibration(struct cs35l56_base *cs35l56_base,
			      const struct cirrus_amp_cal_data *data)
{

	/* Ignore if it is empty */
	if (!data->calTime[0] && !data->calTime[1])
		return -ENODATA;

	if (cs_amp_cal_target_u64(data) != cs35l56_base->silicon_uid) {
		dev_err(cs35l56_base->dev, "cal_data not for this silicon ID\n");
		return -EINVAL;
	}

	cs35l56_base->cal_data = *data;
	cs35l56_base->cal_data_valid = true;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_stash_calibration, "SND_SOC_CS35L56_SHARED");

static int cs35l56_perform_calibration(struct cs35l56_base *cs35l56_base)
{
	const struct cirrus_amp_cal_controls *calibration_controls =
		cs35l56_base->calibration_controls;
	struct cs_dsp *dsp = cs35l56_base->dsp;
	struct cirrus_amp_cal_data cal_data;
	struct cs_dsp_coeff_ctl *ctl;
	bool ngate_ch1_was_enabled = false;
	bool ngate_ch2_was_enabled = false;
	int cali_norm_en_alg_id, cali_norm_en_mem;
	int ret;
	__be32 val;

	if (cs35l56_base->silicon_uid == 0) {
		dev_err(cs35l56_base->dev, "Cannot calibrate: no silicon UID\n");
		return -ENXIO;
	}

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		if (cs35l56_base->rev < 0xb2) {
			cali_norm_en_alg_id = 0x9f22f;
			cali_norm_en_mem = WMFW_ADSP2_YM;
		} else {
			cali_norm_en_alg_id = 0x9f210;
			cali_norm_en_mem = WMFW_ADSP2_XM;
		}
		break;
	default:
		cali_norm_en_alg_id = 0xbf210;
		cali_norm_en_mem = WMFW_ADSP2_XM;
		break;
	}

	ret = pm_runtime_resume_and_get(cs35l56_base->dev);
	if (ret)
		return ret;

	ret = cs35l56_wait_for_ps3(cs35l56_base);
	if (ret) {
		ret = -EBUSY;
		goto err_pm_put;
	}

	regmap_update_bits_check(cs35l56_base->regmap, CS35L56_MIXER_NGATE_CH1_CFG,
				 CS35L56_AUX_NGATE_CHn_EN, 0, &ngate_ch1_was_enabled);
	regmap_update_bits_check(cs35l56_base->regmap, CS35L56_MIXER_NGATE_CH2_CFG,
				 CS35L56_AUX_NGATE_CHn_EN, 0, &ngate_ch2_was_enabled);

	scoped_guard(mutex, &dsp->pwr_lock) {
		ctl = cs_dsp_get_ctl(dsp,
				     calibration_controls->status,
				     calibration_controls->mem_region,
				     calibration_controls->alg_id);
		if (!ctl) {
			dev_err(cs35l56_base->dev, "Could not get %s control\n",
				calibration_controls->status);
			ret = -EIO;
			goto err;
		}

		val = cpu_to_be32(0);
		ret = cs_dsp_coeff_write_ctrl(cs_dsp_get_ctl(dsp,
					      "CALI_NORM_EN",
					      cali_norm_en_mem,
					      cali_norm_en_alg_id),
					      0, &val, sizeof(val));
		if (ret < 0) {
			dev_err(cs35l56_base->dev, "Could not write %s: %d\n", "CALI_NORM_EN", ret);
			ret = -EIO;
			goto err;
		}

		ret = cs35l56_mbox_send(cs35l56_base, CS35L56_MBOX_CMD_AUDIO_CALIBRATION);
		if (ret) {
			ret = -EIO;
			goto err;
		}

		if (read_poll_timeout(cs_dsp_coeff_read_ctrl, ret,
				      (val == cpu_to_be32(1)),
				      CS35L56_CALIBRATION_POLL_US,
				      CS35L56_CALIBRATION_TIMEOUT_US,
				      true,
				      ctl, 0, &val, sizeof(val))) {
			dev_err(cs35l56_base->dev, "Calibration timed out (CAL_STATUS: %u)\n",
				be32_to_cpu(val));
			switch (be32_to_cpu(val)) {
			case CS35L56_CAL_STATUS_OUT_OF_RANGE:
				ret = -ERANGE;
				goto err;
			default:
				ret = -ETIMEDOUT;
				goto err;
			}
		}
	}

	cs35l56_base->cal_data_valid = false;
	memset(&cal_data, 0, sizeof(cal_data));
	ret = cs_amp_read_cal_coeffs(dsp, calibration_controls, &cal_data);
	if (ret) {
		ret = -EIO;
		goto err;
	}

	dev_info(cs35l56_base->dev, "Cal status:%d calR:%d ambient:%d\n",
		 cal_data.calStatus, cal_data.calR, cal_data.calAmbient);

	cal_data.calTarget[0] = (u32)cs35l56_base->silicon_uid;
	cal_data.calTarget[1] = (u32)(cs35l56_base->silicon_uid >> 32);
	cs35l56_base->cal_data = cal_data;
	cs35l56_base->cal_data_valid = true;

	ret = 0;

err:
	if (ngate_ch1_was_enabled) {
		regmap_set_bits(cs35l56_base->regmap, CS35L56_MIXER_NGATE_CH1_CFG,
				CS35L56_AUX_NGATE_CHn_EN);
	}
	if (ngate_ch2_was_enabled) {
		regmap_set_bits(cs35l56_base->regmap, CS35L56_MIXER_NGATE_CH2_CFG,
				CS35L56_AUX_NGATE_CHn_EN);
	}
err_pm_put:
	pm_runtime_put(cs35l56_base->dev);

	return ret;
}

ssize_t cs35l56_calibrate_debugfs_write(struct cs35l56_base *cs35l56_base,
					const char __user *from, size_t count,
					loff_t *ppos)
{
	static const char * const options[] = { "factory", "store_uefi" };
	char buf[11] = { 0 };
	int num_amps, ret;

	if (!IS_ENABLED(CONFIG_SND_SOC_CS35L56_CAL_DEBUGFS_COMMON))
		return -ENXIO;

	if (*ppos)
		return -EINVAL;

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, from, count);
	if (ret < 0)
		return ret;

	switch (sysfs_match_string(options, buf)) {
	case 0:
		ret = cs35l56_perform_calibration(cs35l56_base);
		if (ret < 0)
			return ret;
		break;
	case 1:
		if (!cs35l56_base->cal_data_valid)
			return -ENODATA;

		num_amps = cs35l56_base->num_amps;
		if (num_amps == 0)
			num_amps = -1;

		ret = cs_amp_set_efi_calibration_data(cs35l56_base->dev,
						      cs35l56_base->cal_index,
						      num_amps,
						      &cs35l56_base->cal_data);
		if (ret < 0)
			return ret;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return count;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_calibrate_debugfs_write, "SND_SOC_CS35L56_SHARED");

ssize_t cs35l56_cal_ambient_debugfs_write(struct cs35l56_base *cs35l56_base,
					  const char __user *from, size_t count,
					  loff_t *ppos)
{
	unsigned long val;
	int ret;

	if (!IS_ENABLED(CONFIG_SND_SOC_CS35L56_CAL_DEBUGFS_COMMON))
		return -ENXIO;

	if (*ppos)
		return -EINVAL;

	ret = pm_runtime_resume_and_get(cs35l56_base->dev);
	if (ret)
		return ret;

	ret = kstrtoul_from_user(from, count, 10, &val);
	if (ret < 0)
		goto out;

	ret = cs_amp_write_ambient_temp(cs35l56_base->dsp, cs35l56_base->calibration_controls, val);
	if (ret)
		ret = -EIO;
out:
	pm_runtime_put(cs35l56_base->dev);

	if (ret < 0)
		return ret;

	return count;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_cal_ambient_debugfs_write, "SND_SOC_CS35L56_SHARED");

ssize_t cs35l56_cal_data_debugfs_read(struct cs35l56_base *cs35l56_base,
				      char __user *to, size_t count,
				      loff_t *ppos)
{
	if (!IS_ENABLED(CONFIG_SND_SOC_CS35L56_CAL_DEBUGFS_COMMON))
		return -ENXIO;

	if (!cs35l56_base->cal_data_valid)
		return 0;

	return simple_read_from_buffer(to, count, ppos, &cs35l56_base->cal_data,
				       sizeof(cs35l56_base->cal_data));
}
EXPORT_SYMBOL_NS_GPL(cs35l56_cal_data_debugfs_read, "SND_SOC_CS35L56_SHARED");

ssize_t cs35l56_cal_data_debugfs_write(struct cs35l56_base *cs35l56_base,
				       const char __user *from, size_t count,
				       loff_t *ppos)
{
	struct cirrus_amp_cal_data cal_data;
	int ret;

	if (!IS_ENABLED(CONFIG_SND_SOC_CS35L56_CAL_DEBUGFS_COMMON))
		return -ENXIO;

	/* Only allow a full blob to be written */
	if (*ppos || (count != sizeof(cal_data)))
		return -EMSGSIZE;

	ret = simple_write_to_buffer(&cal_data, sizeof(cal_data), ppos, from, count);
	if (ret)
		return ret;

	ret = cs35l56_stash_calibration(cs35l56_base, &cal_data);
	if (ret)
		return ret;

	return count;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_cal_data_debugfs_write, "SND_SOC_CS35L56_SHARED");

void cs35l56_create_cal_debugfs(struct cs35l56_base *cs35l56_base,
				const struct cs35l56_cal_debugfs_fops *fops)
{
	if (!IS_ENABLED(CONFIG_SND_SOC_CS35L56_CAL_DEBUGFS_COMMON))
		return;

	cs35l56_base->debugfs = cs_amp_create_debugfs(cs35l56_base->dev);

	debugfs_create_file("calibrate",
			    0200, cs35l56_base->debugfs, cs35l56_base,
			    &fops->calibrate);
	debugfs_create_file("cal_temperature",
			    0200, cs35l56_base->debugfs, cs35l56_base,
			    &fops->cal_temperature);
	debugfs_create_file("cal_data",
			    0644, cs35l56_base->debugfs, cs35l56_base,
			    &fops->cal_data);
}
EXPORT_SYMBOL_NS_GPL(cs35l56_create_cal_debugfs, "SND_SOC_CS35L56_SHARED");

void cs35l56_remove_cal_debugfs(struct cs35l56_base *cs35l56_base)
{
	debugfs_remove_recursive(cs35l56_base->debugfs);
}
EXPORT_SYMBOL_NS_GPL(cs35l56_remove_cal_debugfs, "SND_SOC_CS35L56_SHARED");

const char * const cs35l56_cal_set_status_text[] = {
	"Unknown", "Default", "Set",
};
EXPORT_SYMBOL_NS_GPL(cs35l56_cal_set_status_text, "SND_SOC_CS35L56_SHARED");

int cs35l56_cal_set_status_get(struct cs35l56_base *cs35l56_base,
			       struct snd_ctl_elem_value *uvalue)
{
	struct cs_dsp *dsp = cs35l56_base->dsp;
	__be32 cal_set_status_be;
	int alg_id;
	int ret;

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		alg_id = 0x9f210;
		break;
	default:
		alg_id = 0xbf210;
		break;
	}

	scoped_guard(mutex, &dsp->pwr_lock) {
		ret = cs_dsp_coeff_read_ctrl(cs_dsp_get_ctl(dsp,
							    "CAL_SET_STATUS",
							    WMFW_ADSP2_YM, alg_id),
					      0, &cal_set_status_be,
					      sizeof(cal_set_status_be));
	}
	if (ret) {
		uvalue->value.enumerated.item[0] = CS35L56_CAL_SET_STATUS_UNKNOWN;
		return 0;
	}

	switch (be32_to_cpu(cal_set_status_be)) {
	case CS35L56_CAL_SET_STATUS_DEFAULT:
	case CS35L56_CAL_SET_STATUS_SET:
		uvalue->value.enumerated.item[0] = be32_to_cpu(cal_set_status_be);
		return 0;
	default:
		uvalue->value.enumerated.item[0] = CS35L56_CAL_SET_STATUS_UNKNOWN;
		return 0;
	}
}
EXPORT_SYMBOL_NS_GPL(cs35l56_cal_set_status_get, "SND_SOC_CS35L56_SHARED");

int cs35l56_read_prot_status(struct cs35l56_base *cs35l56_base,
			     bool *fw_missing, unsigned int *fw_version)
{
	unsigned int prot_status;
	int ret;

	ret = regmap_read(cs35l56_base->regmap,
			  cs35l56_base->fw_reg->prot_sts, &prot_status);
	if (ret) {
		dev_err(cs35l56_base->dev, "Get PROTECTION_STATUS failed: %d\n", ret);
		return ret;
	}

	*fw_missing = !!(prot_status & CS35L56_FIRMWARE_MISSING);

	ret = regmap_read(cs35l56_base->regmap,
			  cs35l56_base->fw_reg->fw_ver, fw_version);
	if (ret) {
		dev_err(cs35l56_base->dev, "Get FW VER failed: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_read_prot_status, "SND_SOC_CS35L56_SHARED");

void cs35l56_warn_if_firmware_missing(struct cs35l56_base *cs35l56_base)
{
	unsigned int firmware_version;
	bool firmware_missing;
	int ret;

	ret = cs35l56_read_prot_status(cs35l56_base, &firmware_missing, &firmware_version);
	if (ret)
		return;

	if (!firmware_missing)
		return;

	dev_warn(cs35l56_base->dev, "FIRMWARE_MISSING\n");
}
EXPORT_SYMBOL_NS_GPL(cs35l56_warn_if_firmware_missing, "SND_SOC_CS35L56_SHARED");

void cs35l56_log_tuning(struct cs35l56_base *cs35l56_base, struct cs_dsp *cs_dsp)
{
	__be32 pid, sid, tid;
	unsigned int alg_id;
	int ret;

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		alg_id = 0x9f212;
		break;
	default:
		alg_id = 0xbf212;
		break;
	}

	scoped_guard(mutex, &cs_dsp->pwr_lock) {
		ret = cs_dsp_coeff_read_ctrl(cs_dsp_get_ctl(cs_dsp, "AS_PRJCT_ID",
							    WMFW_ADSP2_XM, alg_id),
					     0, &pid, sizeof(pid));
		if (!ret)
			ret = cs_dsp_coeff_read_ctrl(cs_dsp_get_ctl(cs_dsp, "AS_CHNNL_ID",
								    WMFW_ADSP2_XM, alg_id),
						     0, &sid, sizeof(sid));
		if (!ret)
			ret = cs_dsp_coeff_read_ctrl(cs_dsp_get_ctl(cs_dsp, "AS_SNPSHT_ID",
								    WMFW_ADSP2_XM, alg_id),
						     0, &tid, sizeof(tid));
	}

	if (ret)
		dev_warn(cs35l56_base->dev, "Can't read tuning IDs");
	else
		dev_info(cs35l56_base->dev, "Tuning PID: %#x, SID: %#x, TID: %#x\n",
			 be32_to_cpu(pid), be32_to_cpu(sid), be32_to_cpu(tid));
}
EXPORT_SYMBOL_NS_GPL(cs35l56_log_tuning, "SND_SOC_CS35L56_SHARED");

int cs35l56_hw_init(struct cs35l56_base *cs35l56_base)
{
	int ret;
	unsigned int devid, revid, otpid, secured, fw_ver;
	bool fw_missing;

	/*
	 * When the system is not using a reset_gpio ensure the device is
	 * awake, otherwise the device has just been released from reset and
	 * the driver must wait for the control port to become usable.
	 */
	if (!cs35l56_base->reset_gpio)
		cs35l56_issue_wake_event(cs35l56_base);
	else
		cs35l56_wait_control_port_ready();

	ret = regmap_read_bypassed(cs35l56_base->regmap, CS35L56_REVID, &revid);
	if (ret < 0) {
		dev_err(cs35l56_base->dev, "Get Revision ID failed\n");
		return ret;
	}
	cs35l56_base->rev = revid & (CS35L56_AREVID_MASK | CS35L56_MTLREVID_MASK);
	cs35l56_set_fw_reg_table(cs35l56_base);

	ret = cs35l56_wait_for_firmware_boot(cs35l56_base);
	if (ret)
		return ret;

	ret = regmap_read_bypassed(cs35l56_base->regmap, CS35L56_DEVID, &devid);
	if (ret < 0) {
		dev_err(cs35l56_base->dev, "Get Device ID failed\n");
		return ret;
	}
	devid &= CS35L56_DEVID_MASK;

	switch (devid) {
	case 0x35A54:
	case 0x35A56:
	case 0x35A57:
		cs35l56_base->calibration_controls = &cs35l56_calibration_controls;
		break;
	case 0x35A630:
		cs35l56_base->calibration_controls = &cs35l63_calibration_controls;
		devid = devid >> 4;
		break;
	default:
		dev_err(cs35l56_base->dev, "Unknown device %x\n", devid);
		return -ENODEV;
	}

	cs35l56_base->type = devid & 0xFF;

	/* Silicon is now identified and booted so exit cache-only */
	regcache_cache_only(cs35l56_base->regmap, false);

	ret = regmap_read(cs35l56_base->regmap, CS35L56_DSP_RESTRICT_STS1, &secured);
	if (ret) {
		dev_err(cs35l56_base->dev, "Get Secure status failed\n");
		return ret;
	}

	/* When any bus is restricted treat the device as secured */
	if (secured & CS35L56_RESTRICTED_MASK)
		cs35l56_base->secured = true;

	ret = regmap_read(cs35l56_base->regmap, CS35L56_OTPID, &otpid);
	if (ret < 0) {
		dev_err(cs35l56_base->dev, "Get OTP ID failed\n");
		return ret;
	}

	ret = cs35l56_read_prot_status(cs35l56_base, &fw_missing, &fw_ver);
	if (ret)
		return ret;

	dev_info(cs35l56_base->dev, "Cirrus Logic CS35L%02X%s Rev %02X OTP%d fw:%d.%d.%d (patched=%u)\n",
		 cs35l56_base->type, cs35l56_base->secured ? "s" : "", cs35l56_base->rev, otpid,
		 fw_ver >> 16, (fw_ver >> 8) & 0xff, fw_ver & 0xff, !fw_missing);

	/* Wake source and *_BLOCKED interrupts default to unmasked, so mask them */
	regmap_write(cs35l56_base->regmap, CS35L56_IRQ1_MASK_20, 0xffffffff);
	regmap_update_bits(cs35l56_base->regmap, CS35L56_IRQ1_MASK_1,
			   CS35L56_AMP_SHORT_ERR_EINT1_MASK,
			   0);
	regmap_update_bits(cs35l56_base->regmap, CS35L56_IRQ1_MASK_8,
			   CS35L56_TEMP_ERR_EINT1_MASK,
			   0);

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		ret = cs35l56_read_silicon_uid(cs35l56_base);
		break;
	default:
		ret = cs35l63_read_silicon_uid(cs35l56_base);
		break;
	}
	if (ret)
		return ret;

	dev_dbg(cs35l56_base->dev, "SiliconID = %#llx\n", cs35l56_base->silicon_uid);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_hw_init, "SND_SOC_CS35L56_SHARED");

int cs35l56_get_speaker_id(struct cs35l56_base *cs35l56_base)
{
	struct gpio_descs *descs;
	u32 speaker_id;
	int i, ret;

	/* Check for vendor-specific speaker ID method */
	ret = cs_amp_get_vendor_spkid(cs35l56_base->dev);
	if (ret >= 0) {
		dev_dbg(cs35l56_base->dev, "Vendor Speaker ID = %d\n", ret);
		return ret;
	} else if (ret != -ENOENT) {
		dev_err(cs35l56_base->dev, "Error getting vendor Speaker ID: %d\n", ret);
		return ret;
	}

	/* Attempt to read the speaker type from a device property */
	ret = device_property_read_u32(cs35l56_base->dev, "cirrus,speaker-id", &speaker_id);
	if (!ret) {
		dev_dbg(cs35l56_base->dev, "Speaker ID = %d\n", speaker_id);
		return speaker_id;
	}

	/* Read the speaker type qualifier from the motherboard GPIOs */
	descs = gpiod_get_array_optional(cs35l56_base->dev, "spk-id", GPIOD_IN);
	if (!descs) {
		return -ENOENT;
	} else if (IS_ERR(descs)) {
		ret = PTR_ERR(descs);
		return dev_err_probe(cs35l56_base->dev, ret, "Failed to get spk-id-gpios\n");
	}

	speaker_id = 0;
	for (i = 0; i < descs->ndescs; i++) {
		ret = gpiod_get_value_cansleep(descs->desc[i]);
		if (ret < 0) {
			dev_err_probe(cs35l56_base->dev, ret, "Failed to read spk-id[%d]\n", i);
			goto err;
		}

		speaker_id |= (ret << i);
	}

	dev_dbg(cs35l56_base->dev, "Speaker ID = %d\n", speaker_id);
	ret = speaker_id;
err:
	gpiod_put_array(descs);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_get_speaker_id, "SND_SOC_CS35L56_SHARED");

int cs35l56_check_and_save_onchip_spkid_gpios(struct cs35l56_base *cs35l56_base,
					      const u32 *gpios, int num_gpios,
					      const u32 *pulls, int num_pulls)
{
	int max_gpio;
	int ret = 0;
	int i;

	if ((num_gpios > ARRAY_SIZE(cs35l56_base->onchip_spkid_gpios)) ||
	    (num_pulls > ARRAY_SIZE(cs35l56_base->onchip_spkid_pulls)))
		return -EOVERFLOW;

	switch (cs35l56_base->type) {
	case 0x54:
	case 0x56:
	case 0x57:
		max_gpio = CS35L56_MAX_GPIO;
		break;
	default:
		max_gpio = CS35L63_MAX_GPIO;
		break;
	}

	for (i = 0; i < num_gpios; i++) {
		if (gpios[i] < 1 || gpios[i] > max_gpio) {
			dev_err(cs35l56_base->dev, "Invalid spkid GPIO %d\n", gpios[i]);
			/* Keep going so we log all bad values */
			ret = -EINVAL;
		}

		/* Change to zero-based */
		cs35l56_base->onchip_spkid_gpios[i] = gpios[i] - 1;
	}

	for (i = 0; i < num_pulls; i++) {
		switch (pulls[i]) {
		case 0:
			cs35l56_base->onchip_spkid_pulls[i] = CS35L56_PAD_PULL_NONE;
			break;
		case 1:
			cs35l56_base->onchip_spkid_pulls[i] = CS35L56_PAD_PULL_UP;
			break;
		case 2:
			cs35l56_base->onchip_spkid_pulls[i] = CS35L56_PAD_PULL_DOWN;
			break;
		default:
			dev_err(cs35l56_base->dev, "Invalid spkid pull %d\n", pulls[i]);
			/* Keep going so we log all bad values */
			ret = -EINVAL;
			break;
		}
	}
	if (ret)
		return ret;

	cs35l56_base->num_onchip_spkid_gpios = num_gpios;
	cs35l56_base->num_onchip_spkid_pulls = num_pulls;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_check_and_save_onchip_spkid_gpios, "SND_SOC_CS35L56_SHARED");

/* Caller must pm_runtime resume before calling this function */
int cs35l56_configure_onchip_spkid_pads(struct cs35l56_base *cs35l56_base)
{
	struct regmap *regmap = cs35l56_base->regmap;
	unsigned int addr_offset, val;
	int num_gpios, num_pulls;
	int i, ret;

	KUNIT_STATIC_STUB_REDIRECT(cs35l56_configure_onchip_spkid_pads, cs35l56_base);

	if (cs35l56_base->num_onchip_spkid_gpios == 0)
		return 0;

	num_gpios = min(cs35l56_base->num_onchip_spkid_gpios,
			ARRAY_SIZE(cs35l56_base->onchip_spkid_gpios));
	num_pulls = min(cs35l56_base->num_onchip_spkid_pulls,
			ARRAY_SIZE(cs35l56_base->onchip_spkid_pulls));

	for (i = 0; i < num_gpios; i++) {
		addr_offset = cs35l56_base->onchip_spkid_gpios[i] * sizeof(u32);

		/* Set unspecified pulls to NONE */
		if (i < num_pulls) {
			val = FIELD_PREP(CS35L56_PAD_GPIO_PULL_MASK,
					 cs35l56_base->onchip_spkid_pulls[i]);
		} else {
			val = FIELD_PREP(CS35L56_PAD_GPIO_PULL_MASK, CS35L56_PAD_PULL_NONE);
		}

		ret = regmap_update_bits(regmap, CS35L56_SYNC_GPIO1_CFG + addr_offset,
					 CS35L56_PAD_GPIO_PULL_MASK | CS35L56_PAD_GPIO_IE,
					 val | CS35L56_PAD_GPIO_IE);
		if (ret) {
			dev_err(cs35l56_base->dev, "GPIO%d set pad fail: %d\n",
				cs35l56_base->onchip_spkid_gpios[i] + 1, ret);
			return ret;
		}
	}

	ret = regmap_write(regmap, CS35L56_UPDATE_REGS, CS35L56_UPDT_GPIO_PRES);
	if (ret) {
		dev_err(cs35l56_base->dev, "UPDT_GPIO_PRES failed:%d\n", ret);
		return ret;
	}

	usleep_range(CS35L56_PAD_PULL_SETTLE_US, CS35L56_PAD_PULL_SETTLE_US * 2);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_configure_onchip_spkid_pads, "SND_SOC_CS35L56_SHARED");

/* Caller must pm_runtime resume before calling this function */
int cs35l56_read_onchip_spkid(struct cs35l56_base *cs35l56_base)
{
	struct regmap *regmap = cs35l56_base->regmap;
	unsigned int addr_offset, val;
	int num_gpios;
	int speaker_id = 0;
	int i, ret;

	KUNIT_STATIC_STUB_REDIRECT(cs35l56_read_onchip_spkid, cs35l56_base);

	if (cs35l56_base->num_onchip_spkid_gpios == 0)
		return -ENOENT;

	num_gpios = min(cs35l56_base->num_onchip_spkid_gpios,
			ARRAY_SIZE(cs35l56_base->onchip_spkid_gpios));

	for (i = 0; i < num_gpios; i++) {
		addr_offset = cs35l56_base->onchip_spkid_gpios[i] * sizeof(u32);

		ret = regmap_update_bits(regmap, CS35L56_GPIO1_CTRL1 + addr_offset,
					 CS35L56_GPIO_DIR_MASK | CS35L56_GPIO_FN_MASK,
					 CS35L56_GPIO_DIR_MASK | CS35L56_GPIO_FN_GPIO);
		if (ret) {
			dev_err(cs35l56_base->dev, "GPIO%u set func fail: %d\n",
				cs35l56_base->onchip_spkid_gpios[i] + 1, ret);
			return ret;
		}
	}

	ret = regmap_read(regmap, CS35L56_GPIO_STATUS1, &val);
	if (ret) {
		dev_err(cs35l56_base->dev, "GPIO%d status read failed: %d\n",
			cs35l56_base->onchip_spkid_gpios[i] + 1, ret);
		return ret;
	}

	for (i = 0; i < num_gpios; i++) {
		speaker_id <<= 1;

		if (val & BIT(cs35l56_base->onchip_spkid_gpios[i]))
			speaker_id |= 1;
	}

	dev_dbg(cs35l56_base->dev, "Onchip GPIO Speaker ID = %d\n", speaker_id);

	return speaker_id;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_read_onchip_spkid, "SND_SOC_CS35L56_SHARED");

static const u32 cs35l56_bclk_valid_for_pll_freq_table[] = {
	[0x0C] = 128000,
	[0x0F] = 256000,
	[0x11] = 384000,
	[0x12] = 512000,
	[0x15] = 768000,
	[0x17] = 1024000,
	[0x1A] = 1500000,
	[0x1B] = 1536000,
	[0x1C] = 2000000,
	[0x1D] = 2048000,
	[0x1E] = 2400000,
	[0x20] = 3000000,
	[0x21] = 3072000,
	[0x23] = 4000000,
	[0x24] = 4096000,
	[0x25] = 4800000,
	[0x27] = 6000000,
	[0x28] = 6144000,
	[0x29] = 6250000,
	[0x2A] = 6400000,
	[0x2E] = 8000000,
	[0x2F] = 8192000,
	[0x30] = 9600000,
	[0x32] = 12000000,
	[0x33] = 12288000,
	[0x37] = 13500000,
	[0x38] = 19200000,
	[0x39] = 22579200,
	[0x3B] = 24576000,
};

int cs35l56_get_bclk_freq_id(unsigned int freq)
{
	int i;

	if (freq == 0)
		return -EINVAL;

	/* The BCLK frequency must be a valid PLL REFCLK */
	for (i = 0; i < ARRAY_SIZE(cs35l56_bclk_valid_for_pll_freq_table); ++i) {
		if (cs35l56_bclk_valid_for_pll_freq_table[i] == freq)
			return i;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(cs35l56_get_bclk_freq_id, "SND_SOC_CS35L56_SHARED");

static const char * const cs35l56_supplies[/* auto-sized */] = {
	"VDD_P",
	"VDD_IO",
	"VDD_A",
};

void cs35l56_fill_supply_names(struct regulator_bulk_data *data)
{
	int i;

	BUILD_BUG_ON(ARRAY_SIZE(cs35l56_supplies) != CS35L56_NUM_BULK_SUPPLIES);
	for (i = 0; i < ARRAY_SIZE(cs35l56_supplies); i++)
		data[i].supply = cs35l56_supplies[i];
}
EXPORT_SYMBOL_NS_GPL(cs35l56_fill_supply_names, "SND_SOC_CS35L56_SHARED");

const char * const cs35l56_tx_input_texts[] = {
	"None", "ASP1RX1", "ASP1RX2", "VMON", "IMON", "ERRVOL", "CLASSH",
	"VDDBMON", "VBSTMON", "DSP1TX1", "DSP1TX2", "DSP1TX3", "DSP1TX4",
	"DSP1TX5", "DSP1TX6", "DSP1TX7", "DSP1TX8", "TEMPMON",
	"INTERPOLATOR", "SDW1RX1", "SDW1RX2",
};
EXPORT_SYMBOL_NS_GPL(cs35l56_tx_input_texts, "SND_SOC_CS35L56_SHARED");

const unsigned int cs35l56_tx_input_values[] = {
	CS35L56_INPUT_SRC_NONE,
	CS35L56_INPUT_SRC_ASP1RX1,
	CS35L56_INPUT_SRC_ASP1RX2,
	CS35L56_INPUT_SRC_VMON,
	CS35L56_INPUT_SRC_IMON,
	CS35L56_INPUT_SRC_ERR_VOL,
	CS35L56_INPUT_SRC_CLASSH,
	CS35L56_INPUT_SRC_VDDBMON,
	CS35L56_INPUT_SRC_VBSTMON,
	CS35L56_INPUT_SRC_DSP1TX1,
	CS35L56_INPUT_SRC_DSP1TX2,
	CS35L56_INPUT_SRC_DSP1TX3,
	CS35L56_INPUT_SRC_DSP1TX4,
	CS35L56_INPUT_SRC_DSP1TX5,
	CS35L56_INPUT_SRC_DSP1TX6,
	CS35L56_INPUT_SRC_DSP1TX7,
	CS35L56_INPUT_SRC_DSP1TX8,
	CS35L56_INPUT_SRC_TEMPMON,
	CS35L56_INPUT_SRC_INTERPOLATOR,
	CS35L56_INPUT_SRC_SWIRE_DP1_CHANNEL1,
	CS35L56_INPUT_SRC_SWIRE_DP1_CHANNEL2,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_tx_input_values, "SND_SOC_CS35L56_SHARED");

const struct regmap_config cs35l56_regmap_i2c = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l56_reg_defaults),
	.volatile_reg = cs35l56_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_regmap_i2c, "SND_SOC_CS35L56_SHARED");

const struct regmap_config cs35l56_regmap_spi = {
	.reg_bits = 32,
	.val_bits = 32,
	.pad_bits = 16,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l56_reg_defaults),
	.volatile_reg = cs35l56_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_regmap_spi, "SND_SOC_CS35L56_SHARED");

const struct regmap_config cs35l56_regmap_sdw = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l56_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l56_reg_defaults),
	.volatile_reg = cs35l56_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_NS_GPL(cs35l56_regmap_sdw, "SND_SOC_CS35L56_SHARED");

const struct regmap_config cs35l63_regmap_i2c = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_base = 0x8000,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l63_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l63_reg_defaults),
	.volatile_reg = cs35l63_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_NS_GPL(cs35l63_regmap_i2c, "SND_SOC_CS35L56_SHARED");

const struct regmap_config cs35l63_regmap_sdw = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_BIG,
	.max_register = CS35L56_DSP1_PMEM_5114,
	.reg_defaults = cs35l63_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs35l63_reg_defaults),
	.volatile_reg = cs35l63_volatile_reg,
	.readable_reg = cs35l56_readable_reg,
	.precious_reg = cs35l56_precious_reg,
	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_NS_GPL(cs35l63_regmap_sdw, "SND_SOC_CS35L56_SHARED");

MODULE_DESCRIPTION("ASoC CS35L56 Shared");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_CS_AMP_LIB");
MODULE_IMPORT_NS("FW_CS_DSP");
