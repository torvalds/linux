// SPDX-License-Identifier: GPL-2.0-only
//
// rt766-sdca-sdw.c -- rt766 SDCA ALSA SoC audio driver
//
// Copyright(c) 2026 Realtek Semiconductor Corp.
//
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include "rt766-sdca.h"
#include "rt766-sdca-sdw.h"

#define RT766_PROBE_TIMEOUT 5000

static bool rt766_sdca_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SDW_SCP_SDCA_INT1 ... SDW_SCP_SDCA_INTMASK4:
	case RT766_VERSION_ID ... RT766_BOND_LATCH_ID:
	case 0xc344 ... 0xc345:
	case 0xc900:
	case 0xc920:
	case 0xd540 ... 0xd542:
	case 0xf01e:
	case RT766_HP_POWER_STATE ... RT766_HP_FSM_CTL2_1:
	case 0x310100:
	case RT766_MCU_PATCH_ADDR1_START ... RT766_MCU_PATCH_ADDR1_END:
	case RT766_MCU_PATCH_ADDR2_START ... RT766_MCU_PATCH_ADDR2_END:
	case RT766_MUTE_REG(UAJ, USER_FU41, 1):
	case RT766_MUTE_REG(UAJ, USER_FU41, 2):
	case RT766_VOLUME_REG(UAJ, USER_FU41, 1):
	case RT766_VOLUME_REG(UAJ, USER_FU41, 2):
	case RT766_MUTE_REG(UAJ, USER_FU36, 1):
	case RT766_MUTE_REG(UAJ, USER_FU36, 2):
	case RT766_VOLUME_REG(UAJ, USER_FU36, 1):
	case RT766_VOLUME_REG(UAJ, USER_FU36, 2):
	case RT766_PDE_REQ_REG(UAJ, PDE47):
	case RT766_PDE_REQ_REG(UAJ, PDE34):
	case RT766_SDCA_CTL(UAJ, CS41, SDCA_CTL_CS_SAMPLERATEINDEX):
	case RT766_SDCA_CTL(UAJ, CS36, SDCA_CTL_CS_SAMPLERATEINDEX):
	case RT766_FUNC_STATUS_REG(UAJ): /* 0x40480000 */
	case RT766_PDE_ACTUAL_REG(UAJ, PDE47): /* 0x40481400 */
	case RT766_PDE_ACTUAL_REG(UAJ, PDE34): /* 0x40481480 */
	case RT766_GAIN_REG(UAJ, PLATFORM_FU33, 1):
	case RT766_GAIN_REG(UAJ, PLATFORM_FU33, 2):
	case RT766_SDCA_CTL(UAJ, GE49, SDCA_CTL_GE_SELECTED_MODE):
	case RT766_SDCA_CTL(UAJ, GE49, SDCA_CTL_GE_DETECTED_MODE): /* 0x40600490 */
	case RT766_PDE_REQ_REG(MIC, PDE11):
	case RT766_MUTE_REG(MIC, USER_FU113, 1):
	case RT766_MUTE_REG(MIC, USER_FU113, 2):
	case RT766_MUTE_REG(MIC, USER_FU113, 3):
	case RT766_MUTE_REG(MIC, USER_FU113, 4):
	case RT766_VOLUME_REG(MIC, USER_FU113, 1):
	case RT766_VOLUME_REG(MIC, USER_FU113, 2):
	case RT766_VOLUME_REG(MIC, USER_FU113, 3):
	case RT766_VOLUME_REG(MIC, USER_FU113, 4):
	case RT766_FUNC_STATUS_REG(MIC): /* 0x40880000 */
	case RT766_SDCA_CTL(MIC, CS113, SDCA_CTL_CS_SAMPLERATEINDEX):
	case RT766_PDE_ACTUAL_REG(MIC, PDE11): /* 0x40881500 */
	case RT766_FUNC_STATUS_REG(HID): /* 0x40c80000 */
	/* 0x40c80080 - 0x40c80098 */
	case RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_CURRENTOWNER) ...
		RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_MESSAGELENGTH):
	case RT766_MUTE_REG(AMP, USER_FU21, 1):
	case RT766_MUTE_REG(AMP, USER_FU21, 2):
	case RT766_VOLUME_REG(AMP, USER_FU21, 1):
	case RT766_VOLUME_REG(AMP, USER_FU21, 2):
	case RT766_PDE_REQ_REG(AMP, PDE23):
	case RT766_FUNC_STATUS_REG(AMP): /* 0x41080000 */
	case RT766_SDCA_CTL(AMP, PPU21, SDCA_CTL_PPU_POSTURENUMBER):
	case RT766_SDCA_CTL(AMP, CS21, SDCA_CTL_CS_SAMPLERATEINDEX):
	case RT766_PDE_ACTUAL_REG(AMP, PDE23): /* 0x41081980 */
	case RT766_BUF_ADDR_HID1 ... RT766_BUF_ADDR_HID2:
		return true;
	default:
		return false;
	}
}

static bool rt766_sdca_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SDW_SCP_SDCA_INT1 ... SDW_SCP_SDCA_INTMASK4:
	case RT766_VERSION_ID ... RT766_BOND_LATCH_ID:
	case 0xc344 ... 0xc345:
	case 0xc900:
	case 0xc920:
	case 0xd540 ... 0xd542:
	case 0xf01e:
	case RT766_HP_POWER_STATE ... RT766_HP_FSM_CTL2_1:
	case 0x310100:
	case RT766_MCU_PATCH_ADDR1_START ... RT766_MCU_PATCH_ADDR1_END:
	case RT766_MCU_PATCH_ADDR2_START ... RT766_MCU_PATCH_ADDR2_END:
	case RT766_FUNC_STATUS_REG(UAJ):
	case RT766_PDE_ACTUAL_REG(UAJ, PDE47):
	case RT766_PDE_ACTUAL_REG(UAJ, PDE34):
	case RT766_SDCA_CTL(UAJ, GE49, SDCA_CTL_GE_DETECTED_MODE):
	case RT766_FUNC_STATUS_REG(MIC):
	case RT766_PDE_ACTUAL_REG(MIC, PDE11):
	case RT766_FUNC_STATUS_REG(HID):
	case RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_CURRENTOWNER) ...
		RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_MESSAGELENGTH):
	case RT766_FUNC_STATUS_REG(AMP):
	case RT766_PDE_ACTUAL_REG(AMP, PDE23):
	case RT766_BUF_ADDR_HID1 ... RT766_BUF_ADDR_HID2:
		return true;
	default:
		return false;
	}
}

static int rt766_sdca_mbq_size(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT766_VOLUME_REG(UAJ, USER_FU41, 1):
	case RT766_VOLUME_REG(UAJ, USER_FU41, 2):
	case RT766_VOLUME_REG(UAJ, USER_FU36, 1):
	case RT766_VOLUME_REG(UAJ, USER_FU36, 2):
	case RT766_GAIN_REG(UAJ, PLATFORM_FU33, 1):
	case RT766_GAIN_REG(UAJ, PLATFORM_FU33, 2):
	case RT766_VOLUME_REG(MIC, USER_FU113, 1):
	case RT766_VOLUME_REG(MIC, USER_FU113, 2):
	case RT766_VOLUME_REG(MIC, USER_FU113, 3):
	case RT766_VOLUME_REG(MIC, USER_FU113, 4):
	case RT766_VOLUME_REG(AMP, USER_FU21, 1):
	case RT766_VOLUME_REG(AMP, USER_FU21, 2):
		return 2;
	default:
		return 1;
	}
}

static const struct regmap_sdw_mbq_cfg rt766_sdca_mbq_cfg = {
	.mbq_size = rt766_sdca_mbq_size,
};

static const struct regmap_config rt766_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 16,
	.readable_reg = rt766_sdca_readable_register,
	.volatile_reg = rt766_sdca_volatile_register,
	.reg_defaults = rt766_sdca_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt766_sdca_defaults),
	.max_register = SDW_SDCA_MAX_REGISTER,
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt766_sdca_update_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	struct rt766_sdca_priv *rt766 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED)
		rt766->hw_init = false;

	if (status == SDW_SLAVE_ATTACHED) {
		if (rt766->hs_jack) {
			/*
			 * Due to the SCP_SDCA_INTMASK will be cleared by any reset, and then
			 * if the device attached again, we will need to set the setting back.
			 * It could avoid losing the jack detection interrupt.
			 * This also could sync with the cache value as the rt766_sdca_jack_init set.
			 */
			sdw_write_no_pm(rt766->slave, SDW_SCP_SDCA_INTMASK3,
				SDW_SCP_SDCA_INTMASK_SDCA_16);
			sdw_write_no_pm(rt766->slave, SDW_SCP_SDCA_INTMASK4,
				SDW_SCP_SDCA_INTMASK_SDCA_24);
		}
	}

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt766->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt766_sdca_io_init(&slave->dev, slave);
}

static int rt766_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int ret;

	ret = sdw_slave_read_prop(slave);
	if (ret < 0)
		return ret;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	/*
	 * SDCA interrupts are routed through SoundWire domain IRQ.
	 */
	prop->use_domain_irq = true;

	return 0;
}

static const struct sdw_slave_ops rt766_sdca_slave_ops = {
	.read_prop = rt766_sdca_read_prop,
	.update_status = rt766_sdca_update_status,
};

static int rt766_sdca_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap;

	/* Regmap Initialization */
	regmap = devm_regmap_init_sdw_mbq_cfg(&slave->dev, slave,
					      &rt766_sdca_regmap, &rt766_sdca_mbq_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt766_sdca_init(&slave->dev, regmap, slave);
}

static void rt766_sdca_sdw_remove(struct sdw_slave *slave)
{
	pm_runtime_disable(&slave->dev);
}

static const struct sdw_device_id rt766_sdca_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x766, 0x3, 0x1, 0),
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x767, 0x3, 0x1, 0),
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x766, 0x4, 0x1, 0),
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x767, 0x4, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt766_sdca_id);

static int rt766_sdca_dev_suspend(struct device *dev)
{
	struct rt766_sdca_priv *rt766 = dev_get_drvdata(dev);

	if (!rt766->hw_init)
		return 0;

	regcache_cache_only(rt766->regmap, true);
	return 0;
}

static int rt766_sdca_dev_system_suspend(struct device *dev)
{
	struct rt766_sdca_priv *rt766 = dev_get_drvdata(dev);
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret1, ret2;

	if (!rt766->hw_init)
		return 0;

	/*
	 * prevent new interrupts from being handled after the
	 * deferred work completes and before the parent disables
	 * interrupts on the link
	 */
	mutex_lock(&rt766->disable_irq_lock);
	rt766->disable_irq = true;
	ret1 = sdw_update_no_pm(slave, SDW_SCP_SDCA_INTMASK3,
				SDW_SCP_SDCA_INTMASK_SDCA_16, 0);
	ret2 = sdw_update_no_pm(slave, SDW_SCP_SDCA_INTMASK4,
				SDW_SCP_SDCA_INTMASK_SDCA_24, 0);
	mutex_unlock(&rt766->disable_irq_lock);

	if (ret1 < 0 || ret2 < 0) {
		/* log but don't prevent suspend from happening */
		dev_dbg(&slave->dev, "%s: could not disable SDCA interrupts\n:", __func__);
	}

	return rt766_sdca_dev_suspend(dev);
}

static int rt766_sdca_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt766_sdca_priv *rt766 = dev_get_drvdata(dev);
	int ret;

	if (!rt766->first_hw_init)
		return 0;

	if (!slave->unattach_request) {
		mutex_lock(&rt766->disable_irq_lock);
		if (rt766->disable_irq == true) {
			sdw_write_no_pm(slave, SDW_SCP_SDCA_INTMASK3, SDW_SCP_SDCA_INTMASK_SDCA_16);
			sdw_write_no_pm(slave, SDW_SCP_SDCA_INTMASK4, SDW_SCP_SDCA_INTMASK_SDCA_24);
			rt766->disable_irq = false;
		}
		mutex_unlock(&rt766->disable_irq_lock);
		goto regmap_sync;
	}

	ret = sdw_slave_wait_for_init(slave, RT766_PROBE_TIMEOUT);
	if (ret) {
		sdw_show_ping_status(slave->bus, true);
		return ret;
	}

regmap_sync:
	regcache_cache_only(rt766->regmap, false);
	ret = regcache_sync(rt766->regmap);
	if (ret) {
		regcache_cache_only(rt766->regmap, true);
		regcache_mark_dirty(rt766->regmap);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops rt766_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(rt766_sdca_dev_system_suspend, rt766_sdca_dev_resume)
	RUNTIME_PM_OPS(rt766_sdca_dev_suspend, rt766_sdca_dev_resume, NULL)
};

static struct sdw_driver rt766_sdca_sdw_driver = {
	.driver = {
		.name = "rt766-sdca",
		.pm = pm_ptr(&rt766_sdca_pm),
	},
	.probe = rt766_sdca_sdw_probe,
	.remove = rt766_sdca_sdw_remove,
	.ops = &rt766_sdca_slave_ops,
	.id_table = rt766_sdca_id,
};
module_sdw_driver(rt766_sdca_sdw_driver);

MODULE_DESCRIPTION("ASoC RT766 SDCA SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_SDCA");
