// SPDX-License-Identifier: GPL-2.0-only
//
// rt721-sdca-sdw.c -- rt721 SDCA ALSA SoC audio driver
//
// Copyright(c) 2024 Realtek Semiconductor Corp.
//
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>

#include "rt721-sdca.h"
#include "rt721-sdca-sdw.h"
#include "rt-sdw-common.h"

static bool rt721_sdca_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f01 ... 0x2f0a:
	case 0x2f35:
	case 0x2f50:
	case 0x2f51:
	case 0x2f58 ... 0x2f5d:
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_XUV,
		RT721_SDCA_CTL_XUV, 0):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_GE49,
		RT721_SDCA_CTL_SELECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_GE49,
		RT721_SDCA_CTL_DETECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_HID, RT721_SDCA_ENT_HID01,
		RT721_SDCA_CTL_HIDTX_CURRENT_OWNER, 0) ... SDW_SDCA_CTL(FUNC_NUM_HID,
		RT721_SDCA_ENT_HID01, RT721_SDCA_CTL_HIDTX_MESSAGE_LENGTH, 0):
	case RT721_BUF_ADDR_HID1 ... RT721_BUF_ADDR_HID2:
		return true;
	default:
		return false;
	}
}

static bool rt721_sdca_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2f01:
	case 0x2f51:
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_GE49,
		RT721_SDCA_CTL_DETECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_XUV,
		RT721_SDCA_CTL_XUV, 0):
	case SDW_SDCA_CTL(FUNC_NUM_HID, RT721_SDCA_ENT_HID01,
		RT721_SDCA_CTL_HIDTX_CURRENT_OWNER, 0) ... SDW_SDCA_CTL(FUNC_NUM_HID,
		RT721_SDCA_ENT_HID01, RT721_SDCA_CTL_HIDTX_MESSAGE_LENGTH, 0):
	case RT721_BUF_ADDR_HID1 ... RT721_BUF_ADDR_HID2:
		return true;
	default:
		return false;
	}
}

static bool rt721_sdca_mbq_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0900007:
	case 0x0a00005:
	case 0x0c00005:
	case 0x0d00014:
	case 0x0310100:
	case 0x2000001:
	case 0x2000002:
	case 0x2000003:
	case 0x2000013:
	case 0x200003c:
	case 0x2000046:
	case 0x5810000:
	case 0x5810036:
	case 0x5810037:
	case 0x5810038:
	case 0x5810039:
	case 0x5b10018:
	case 0x5b10019:
	case 0x5f00045:
	case 0x5f00048:
	case 0x6100000:
	case 0x6100005:
	case 0x6100006:
	case 0x610000d:
	case 0x6100010:
	case 0x6100011:
	case 0x6100013:
	case 0x6100015:
	case 0x6100017:
	case 0x6100025:
	case 0x6100029:
	case 0x610002c ... 0x610002f:
	case 0x6100053 ... 0x6100055:
	case 0x6100057:
	case 0x610005a:
	case 0x610005b:
	case 0x610006a:
	case 0x610006d:
	case 0x6100092:
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05, RT721_SDCA_CTL_FU_VOLUME,
			CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU05, RT721_SDCA_CTL_FU_VOLUME,
			CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU0F, RT721_SDCA_CTL_FU_VOLUME,
			CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_USER_FU0F, RT721_SDCA_CTL_FU_VOLUME,
			CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PLATFORM_FU44,
			RT721_SDCA_CTL_FU_CH_GAIN, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT721_SDCA_ENT_PLATFORM_FU44,
			RT721_SDCA_CTL_FU_CH_GAIN, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E, RT721_SDCA_CTL_FU_VOLUME,
			CH_01):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E, RT721_SDCA_CTL_FU_VOLUME,
			CH_02):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E, RT721_SDCA_CTL_FU_VOLUME,
			CH_03):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT721_SDCA_ENT_USER_FU1E, RT721_SDCA_CTL_FU_VOLUME,
			CH_04):
	case SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06, RT721_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_AMP, RT721_SDCA_ENT_USER_FU06, RT721_SDCA_CTL_FU_VOLUME, CH_R):
		return true;
	default:
		return false;
	}
}

static bool rt721_sdca_mbq_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0310100:
	case 0x0a00005:
	case 0x0c00005:
	case 0x0d00014:
	case 0x2000000:
	case 0x200000d:
	case 0x2000019:
	case 0x2000020:
	case 0x2000030:
	case 0x2000046:
	case 0x2000067:
	case 0x2000084:
	case 0x2000086:
	case 0x5810000:
	case 0x5810036:
	case 0x5810037:
	case 0x5810038:
	case 0x5810039:
	case 0x5b10018:
	case 0x5b10019:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt721_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt721_sdca_readable_register,
	.volatile_reg = rt721_sdca_volatile_register,
	.max_register = 0x44ffffff,
	.reg_defaults = rt721_sdca_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt721_sdca_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct regmap_config rt721_sdca_mbq_regmap = {
	.name = "sdw-mbq",
	.reg_bits = 32,
	.val_bits = 16,
	.readable_reg = rt721_sdca_mbq_readable_register,
	.volatile_reg = rt721_sdca_mbq_volatile_register,
	.max_register = 0x41000312,
	.reg_defaults = rt721_sdca_mbq_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt721_sdca_mbq_defaults),
	.cache_type = REGCACHE_MAPLE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt721_sdca_update_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	struct rt721_sdca_priv *rt721 = dev_get_drvdata(&slave->dev);

	if (status == SDW_SLAVE_UNATTACHED)
		rt721->hw_init = false;

	if (status == SDW_SLAVE_ATTACHED) {
		if (rt721->hs_jack) {
		/*
		 * Due to the SCP_SDCA_INTMASK will be cleared by any reset, and then
		 * if the device attached again, we will need to set the setting back.
		 * It could avoid losing the jack detection interrupt.
		 * This also could sync with the cache value as the rt721_sdca_jack_init set.
		 */
			sdw_write_no_pm(rt721->slave, SDW_SCP_SDCA_INTMASK1,
				SDW_SCP_SDCA_INTMASK_SDCA_0);
			sdw_write_no_pm(rt721->slave, SDW_SCP_SDCA_INTMASK2,
				SDW_SCP_SDCA_INTMASK_SDCA_8);
		}
	}

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt721->hw_init || status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt721_sdca_io_init(&slave->dev, slave);
}

static int rt721_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	sdw_slave_read_prop(slave);
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;

	prop->paging_support = true;

	/*
	 * port = 1 for headphone playback
	 * port = 2 for headset-mic capture
	 * port = 3 for speaker playback
	 * port = 6 for digital-mic capture
	 */
	prop->source_ports = BIT(6) | BIT(2); /* BITMAP: 01000100 */
	prop->sink_ports = BIT(3) | BIT(1); /* BITMAP:  00001010 */

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
		sizeof(*prop->src_dpn_prop), GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->src_dpn_prop;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* do this again for sink now */
	nval = hweight32(prop->sink_ports);
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
		sizeof(*prop->sink_dpn_prop), GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	j = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[j].num = bit;
		dpn[j].type = SDW_DPN_FULL;
		dpn[j].simple_ch_prep_sm = true;
		dpn[j].ch_prep_timeout = 10;
		j++;
	}

	/* set the timeout values */
	prop->clk_stop_timeout = 1380;

	/* wake-up event */
	prop->wake_capable = 1;

	/* Three data lanes are supported by rt721-sdca codec */
	prop->lane_control_support = true;

	return 0;
}

static int rt721_sdca_interrupt_callback(struct sdw_slave *slave,
					struct sdw_slave_intr_status *status)
{
	struct rt721_sdca_priv *rt721 = dev_get_drvdata(&slave->dev);
	int ret, stat;
	int count = 0, retry = 3;
	unsigned int sdca_cascade, scp_sdca_stat1, scp_sdca_stat2 = 0;

	if (cancel_delayed_work_sync(&rt721->jack_detect_work)) {
		dev_warn(&slave->dev, "%s the pending delayed_work was cancelled", __func__);
		/* avoid the HID owner doesn't change to device */
		if (rt721->scp_sdca_stat2)
			scp_sdca_stat2 = rt721->scp_sdca_stat2;
	}

	/*
	 * The critical section below intentionally protects a rather large piece of code.
	 * We don't want to allow the system suspend to disable an interrupt while we are
	 * processing it, which could be problematic given the quirky SoundWire interrupt
	 * scheme. We do want however to prevent new workqueues from being scheduled if
	 * the disable_irq flag was set during system suspend.
	 */
	mutex_lock(&rt721->disable_irq_lock);

	ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT1);
	if (ret < 0)
		goto io_error;

	rt721->scp_sdca_stat1 = ret;
	ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT2);
	if (ret < 0)
		goto io_error;

	rt721->scp_sdca_stat2 = ret;
	if (scp_sdca_stat2)
		rt721->scp_sdca_stat2 |= scp_sdca_stat2;
	do {
		/* clear flag */
		ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			goto io_error;
		if (ret & SDW_SCP_SDCA_INTMASK_SDCA_0) {
			ret = sdw_update_no_pm(rt721->slave, SDW_SCP_SDCA_INT1,
				SDW_SCP_SDCA_INT_SDCA_0, SDW_SCP_SDCA_INT_SDCA_0);
			if (ret < 0)
				goto io_error;
		}
		ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT2);
		if (ret < 0)
			goto io_error;
		if (ret & SDW_SCP_SDCA_INTMASK_SDCA_8) {
			ret = sdw_write_no_pm(rt721->slave, SDW_SCP_SDCA_INT2,
						SDW_SCP_SDCA_INTMASK_SDCA_8);
			if (ret < 0)
				goto io_error;
		}

		/* check if flag clear or not */
		ret = sdw_read_no_pm(rt721->slave, SDW_DP0_INT);
		if (ret < 0)
			goto io_error;
		sdca_cascade = ret & SDW_DP0_SDCA_CASCADE;

		ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			goto io_error;
		scp_sdca_stat1 = ret & SDW_SCP_SDCA_INTMASK_SDCA_0;

		ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT2);
		if (ret < 0)
			goto io_error;
		scp_sdca_stat2 = ret & SDW_SCP_SDCA_INTMASK_SDCA_8;

		stat = scp_sdca_stat1 || scp_sdca_stat2 || sdca_cascade;

		count++;
	} while (stat != 0 && count < retry);

	if (stat)
		dev_warn(&slave->dev,
			"%s scp_sdca_stat1=0x%x, scp_sdca_stat2=0x%x\n", __func__,
			rt721->scp_sdca_stat1, rt721->scp_sdca_stat2);
	ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT1);
	ret = sdw_read_no_pm(rt721->slave, SDW_SCP_SDCA_INT2);

	if (status->sdca_cascade && !rt721->disable_irq)
		mod_delayed_work(system_power_efficient_wq,
			&rt721->jack_detect_work, msecs_to_jiffies(280));

	mutex_unlock(&rt721->disable_irq_lock);

	return 0;

io_error:
	mutex_unlock(&rt721->disable_irq_lock);
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static const struct sdw_slave_ops rt721_sdca_slave_ops = {
	.read_prop = rt721_sdca_read_prop,
	.interrupt_callback = rt721_sdca_interrupt_callback,
	.update_status = rt721_sdca_update_status,
};

static int rt721_sdca_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap, *mbq_regmap;

	/* Regmap Initialization */
	mbq_regmap = devm_regmap_init_sdw_mbq(slave, &rt721_sdca_mbq_regmap);
	if (IS_ERR(mbq_regmap))
		return PTR_ERR(mbq_regmap);

	regmap = devm_regmap_init_sdw(slave, &rt721_sdca_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt721_sdca_init(&slave->dev, regmap, mbq_regmap, slave);
}

static int rt721_sdca_sdw_remove(struct sdw_slave *slave)
{
	struct rt721_sdca_priv *rt721 = dev_get_drvdata(&slave->dev);

	if (rt721->hw_init) {
		cancel_delayed_work_sync(&rt721->jack_detect_work);
		cancel_delayed_work_sync(&rt721->jack_btn_check_work);
	}

	if (rt721->first_hw_init)
		pm_runtime_disable(&slave->dev);

	mutex_destroy(&rt721->calibrate_mutex);
	mutex_destroy(&rt721->disable_irq_lock);

	return 0;
}

static const struct sdw_device_id rt721_sdca_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x721, 0x3, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt721_sdca_id);

static int rt721_sdca_dev_suspend(struct device *dev)
{
	struct rt721_sdca_priv *rt721 = dev_get_drvdata(dev);

	if (!rt721->hw_init)
		return 0;

	cancel_delayed_work_sync(&rt721->jack_detect_work);
	cancel_delayed_work_sync(&rt721->jack_btn_check_work);

	regcache_cache_only(rt721->regmap, true);
	regcache_cache_only(rt721->mbq_regmap, true);

	return 0;
}

static int rt721_sdca_dev_system_suspend(struct device *dev)
{
	struct rt721_sdca_priv *rt721_sdca = dev_get_drvdata(dev);
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret1, ret2;

	if (!rt721_sdca->hw_init)
		return 0;

	/*
	 * prevent new interrupts from being handled after the
	 * deferred work completes and before the parent disables
	 * interrupts on the link
	 */
	mutex_lock(&rt721_sdca->disable_irq_lock);
	rt721_sdca->disable_irq = true;
	ret1 = sdw_update_no_pm(slave, SDW_SCP_SDCA_INTMASK1,
				SDW_SCP_SDCA_INTMASK_SDCA_0, 0);
	ret2 = sdw_update_no_pm(slave, SDW_SCP_SDCA_INTMASK2,
				SDW_SCP_SDCA_INTMASK_SDCA_8, 0);
	mutex_unlock(&rt721_sdca->disable_irq_lock);

	if (ret1 < 0 || ret2 < 0) {
		/* log but don't prevent suspend from happening */
		dev_dbg(&slave->dev, "%s: could not disable SDCA interrupts\n:", __func__);
	}

	return rt721_sdca_dev_suspend(dev);
}

#define RT721_PROBE_TIMEOUT 5000

static int rt721_sdca_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt721_sdca_priv *rt721 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt721->first_hw_init)
		return 0;

	if (!slave->unattach_request) {
		mutex_lock(&rt721->disable_irq_lock);
		if (rt721->disable_irq == true) {
			sdw_write_no_pm(slave, SDW_SCP_SDCA_INTMASK1, SDW_SCP_SDCA_INTMASK_SDCA_0);
			sdw_write_no_pm(slave, SDW_SCP_SDCA_INTMASK2, SDW_SCP_SDCA_INTMASK_SDCA_8);
			rt721->disable_irq = false;
		}
		mutex_unlock(&rt721->disable_irq_lock);
		goto regmap_sync;
	}

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT721_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt721->regmap, false);
	regcache_sync(rt721->regmap);
	regcache_cache_only(rt721->mbq_regmap, false);
	regcache_sync(rt721->mbq_regmap);
	return 0;
}

static const struct dev_pm_ops rt721_sdca_pm = {
	SYSTEM_SLEEP_PM_OPS(rt721_sdca_dev_system_suspend, rt721_sdca_dev_resume)
	RUNTIME_PM_OPS(rt721_sdca_dev_suspend, rt721_sdca_dev_resume, NULL)
};

static struct sdw_driver rt721_sdca_sdw_driver = {
	.driver = {
		.name = "rt721-sdca",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&rt721_sdca_pm),
	},
	.probe = rt721_sdca_sdw_probe,
	.remove = rt721_sdca_sdw_remove,
	.ops = &rt721_sdca_slave_ops,
	.id_table = rt721_sdca_id,
};
module_sdw_driver(rt721_sdca_sdw_driver);

MODULE_DESCRIPTION("ASoC RT721 SDCA SDW driver");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL");
