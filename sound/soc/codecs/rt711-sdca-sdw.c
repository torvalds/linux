// SPDX-License-Identifier: GPL-2.0-only
//
// rt711-sdw-sdca.c -- rt711 SDCA ALSA SoC audio driver
//
// Copyright(c) 2021 Realtek Semiconductor Corp.
//
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include "rt711-sdca.h"
#include "rt711-sdca-sdw.h"

static bool rt711_sdca_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x201a ... 0x2027:
	case 0x2029 ... 0x202a:
	case 0x202d ... 0x2034:
	case 0x2200 ... 0x2204:
	case 0x2206 ... 0x2212:
	case 0x2220 ... 0x2223:
	case 0x2230 ... 0x2239:
	case 0x2f01 ... 0x2f0f:
	case 0x2f30 ... 0x2f36:
	case 0x2f50 ... 0x2f5a:
	case 0x2f60:
	case 0x3200 ... 0x3212:
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49, RT711_SDCA_CTL_SELECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49, RT711_SDCA_CTL_DETECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_CURRENT_OWNER, 0) ...
		SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_MESSAGE_LENGTH, 0):
	case RT711_BUF_ADDR_HID1 ... RT711_BUF_ADDR_HID2:
		return true;
	default:
		return false;
	}
}

static bool rt711_sdca_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x201b:
	case 0x201c:
	case 0x201d:
	case 0x201f:
	case 0x2021:
	case 0x2023:
	case 0x2230:
	case 0x202d ... 0x202f: /* BRA */
	case 0x2200 ... 0x2212: /* i2c debug */
	case RT711_RC_CAL_STATUS:
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_GE49, RT711_SDCA_CTL_DETECTED_MODE, 0):
	case SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_CURRENT_OWNER, 0) ...
		SDW_SDCA_CTL(FUNC_NUM_HID, RT711_SDCA_ENT_HID01, RT711_SDCA_CTL_HIDTX_MESSAGE_LENGTH, 0):
	case RT711_BUF_ADDR_HID1 ... RT711_BUF_ADDR_HID2:
		return true;
	default:
		return false;
	}
}

static bool rt711_sdca_mbq_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2000000 ... 0x20000ff:
	case 0x5600000 ... 0x56000ff:
	case 0x5700000 ... 0x57000ff:
	case 0x5800000 ... 0x58000ff:
	case 0x5900000 ... 0x59000ff:
	case 0x5b00000 ... 0x5b000ff:
	case 0x5f00000 ... 0x5f000ff:
	case 0x6100000 ... 0x61000ff:
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05, RT711_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU05, RT711_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_USER_FU1E, RT711_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_USER_FU1E, RT711_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU0F, RT711_SDCA_CTL_FU_VOLUME, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_USER_FU0F, RT711_SDCA_CTL_FU_VOLUME, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PLATFORM_FU44, RT711_SDCA_CTL_FU_CH_GAIN, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_JACK_CODEC, RT711_SDCA_ENT_PLATFORM_FU44, RT711_SDCA_CTL_FU_CH_GAIN, CH_R):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_PLATFORM_FU15, RT711_SDCA_CTL_FU_CH_GAIN, CH_L):
	case SDW_SDCA_CTL(FUNC_NUM_MIC_ARRAY, RT711_SDCA_ENT_PLATFORM_FU15, RT711_SDCA_CTL_FU_CH_GAIN, CH_R):
		return true;
	default:
		return false;
	}
}

static bool rt711_sdca_mbq_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2000000:
	case 0x200001a:
	case 0x2000046:
	case 0x2000080:
	case 0x2000081:
	case 0x2000083:
	case 0x5800000:
	case 0x5800001:
	case 0x5f00001:
	case 0x6100008:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt711_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt711_sdca_readable_register,
	.volatile_reg = rt711_sdca_volatile_register,
	.max_register = 0x44ffffff,
	.reg_defaults = rt711_sdca_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt711_sdca_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct regmap_config rt711_sdca_mbq_regmap = {
	.name = "sdw-mbq",
	.reg_bits = 32,
	.val_bits = 16,
	.readable_reg = rt711_sdca_mbq_readable_register,
	.volatile_reg = rt711_sdca_mbq_volatile_register,
	.max_register = 0x40800f12,
	.reg_defaults = rt711_sdca_mbq_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt711_sdca_mbq_defaults),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt711_sdca_update_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	struct rt711_sdca_priv *rt711 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt711->status = status;

	if (status == SDW_SLAVE_UNATTACHED)
		rt711->hw_init = false;

	if (status == SDW_SLAVE_ATTACHED) {
		if (rt711->hs_jack) {
			/*
			 * Due to the SCP_SDCA_INTMASK will be cleared by any reset, and then
			 * if the device attached again, we will need to set the setting back.
			 * It could avoid losing the jack detection interrupt.
			 * This also could sync with the cache value as the rt711_sdca_jack_init set.
			 */
			sdw_write_no_pm(rt711->slave, SDW_SCP_SDCA_INTMASK1,
				SDW_SCP_SDCA_INTMASK_SDCA_0);
			sdw_write_no_pm(rt711->slave, SDW_SCP_SDCA_INTMASK2,
				SDW_SCP_SDCA_INTMASK_SDCA_8);
		}
	}

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt711->hw_init || rt711->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt711_sdca_io_init(&slave->dev, slave);
}

static int rt711_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	prop->is_sdca = true;

	prop->paging_support = true;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x14; /* BITMAP: 00010100 */
	prop->sink_ports = 0x8; /* BITMAP:  00001000 */

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
	prop->clk_stop_timeout = 700;

	/* wake-up event */
	prop->wake_capable = 1;

	return 0;
}

static int rt711_sdca_interrupt_callback(struct sdw_slave *slave,
					struct sdw_slave_intr_status *status)
{
	struct rt711_sdca_priv *rt711 = dev_get_drvdata(&slave->dev);
	int ret, stat;
	int count = 0, retry = 3;
	unsigned int sdca_cascade, scp_sdca_stat1, scp_sdca_stat2 = 0;

	dev_dbg(&slave->dev,
		"%s control_port_stat=%x, sdca_cascade=%x", __func__,
		status->control_port, status->sdca_cascade);

	if (cancel_delayed_work_sync(&rt711->jack_detect_work)) {
		dev_warn(&slave->dev, "%s the pending delayed_work was cancelled", __func__);
		/* avoid the HID owner doesn't change to device */
		if (rt711->scp_sdca_stat2)
			scp_sdca_stat2 = rt711->scp_sdca_stat2;
	}

	/*
	 * The critical section below intentionally protects a rather large piece of code.
	 * We don't want to allow the system suspend to disable an interrupt while we are
	 * processing it, which could be problematic given the quirky SoundWire interrupt
	 * scheme. We do want however to prevent new workqueues from being scheduled if
	 * the disable_irq flag was set during system suspend.
	 */
	mutex_lock(&rt711->disable_irq_lock);

	ret = sdw_read_no_pm(rt711->slave, SDW_SCP_SDCA_INT1);
	if (ret < 0)
		goto io_error;
	rt711->scp_sdca_stat1 = ret;
	ret = sdw_read_no_pm(rt711->slave, SDW_SCP_SDCA_INT2);
	if (ret < 0)
		goto io_error;
	rt711->scp_sdca_stat2 = ret;
	if (scp_sdca_stat2)
		rt711->scp_sdca_stat2 |= scp_sdca_stat2;

	do {
		/* clear flag */
		ret = sdw_read_no_pm(rt711->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			goto io_error;
		if (ret & SDW_SCP_SDCA_INTMASK_SDCA_0) {
			ret = sdw_write_no_pm(rt711->slave, SDW_SCP_SDCA_INT1,
						SDW_SCP_SDCA_INTMASK_SDCA_0);
			if (ret < 0)
				goto io_error;
		}
		ret = sdw_read_no_pm(rt711->slave, SDW_SCP_SDCA_INT2);
		if (ret < 0)
			goto io_error;
		if (ret & SDW_SCP_SDCA_INTMASK_SDCA_8) {
			ret = sdw_write_no_pm(rt711->slave, SDW_SCP_SDCA_INT2,
						SDW_SCP_SDCA_INTMASK_SDCA_8);
			if (ret < 0)
				goto io_error;
		}

		/* check if flag clear or not */
		ret = sdw_read_no_pm(rt711->slave, SDW_DP0_INT);
		if (ret < 0)
			goto io_error;
		sdca_cascade = ret & SDW_DP0_SDCA_CASCADE;

		ret = sdw_read_no_pm(rt711->slave, SDW_SCP_SDCA_INT1);
		if (ret < 0)
			goto io_error;
		scp_sdca_stat1 = ret & SDW_SCP_SDCA_INTMASK_SDCA_0;

		ret = sdw_read_no_pm(rt711->slave, SDW_SCP_SDCA_INT2);
		if (ret < 0)
			goto io_error;
		scp_sdca_stat2 = ret & SDW_SCP_SDCA_INTMASK_SDCA_8;

		stat = scp_sdca_stat1 || scp_sdca_stat2 || sdca_cascade;

		count++;
	} while (stat != 0 && count < retry);

	if (stat)
		dev_warn(&slave->dev,
			"%s scp_sdca_stat1=0x%x, scp_sdca_stat2=0x%x\n", __func__,
			rt711->scp_sdca_stat1, rt711->scp_sdca_stat2);

	if (status->sdca_cascade && !rt711->disable_irq)
		mod_delayed_work(system_power_efficient_wq,
			&rt711->jack_detect_work, msecs_to_jiffies(30));

	mutex_unlock(&rt711->disable_irq_lock);

	return 0;

io_error:
	mutex_unlock(&rt711->disable_irq_lock);
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static struct sdw_slave_ops rt711_sdca_slave_ops = {
	.read_prop = rt711_sdca_read_prop,
	.interrupt_callback = rt711_sdca_interrupt_callback,
	.update_status = rt711_sdca_update_status,
};

static int rt711_sdca_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *regmap, *mbq_regmap;

	/* Regmap Initialization */
	mbq_regmap = devm_regmap_init_sdw_mbq(slave, &rt711_sdca_mbq_regmap);
	if (IS_ERR(mbq_regmap))
		return PTR_ERR(mbq_regmap);

	regmap = devm_regmap_init_sdw(slave, &rt711_sdca_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt711_sdca_init(&slave->dev, regmap, mbq_regmap, slave);
}

static int rt711_sdca_sdw_remove(struct sdw_slave *slave)
{
	struct rt711_sdca_priv *rt711 = dev_get_drvdata(&slave->dev);

	if (rt711->hw_init) {
		cancel_delayed_work_sync(&rt711->jack_detect_work);
		cancel_delayed_work_sync(&rt711->jack_btn_check_work);
	}

	if (rt711->first_hw_init)
		pm_runtime_disable(&slave->dev);

	mutex_destroy(&rt711->calibrate_mutex);
	mutex_destroy(&rt711->disable_irq_lock);

	return 0;
}

static const struct sdw_device_id rt711_sdca_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x711, 0x3, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt711_sdca_id);

static int __maybe_unused rt711_sdca_dev_suspend(struct device *dev)
{
	struct rt711_sdca_priv *rt711 = dev_get_drvdata(dev);

	if (!rt711->hw_init)
		return 0;

	cancel_delayed_work_sync(&rt711->jack_detect_work);
	cancel_delayed_work_sync(&rt711->jack_btn_check_work);

	regcache_cache_only(rt711->regmap, true);
	regcache_cache_only(rt711->mbq_regmap, true);

	return 0;
}

static int __maybe_unused rt711_sdca_dev_system_suspend(struct device *dev)
{
	struct rt711_sdca_priv *rt711_sdca = dev_get_drvdata(dev);
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret1, ret2;

	if (!rt711_sdca->hw_init)
		return 0;

	/*
	 * prevent new interrupts from being handled after the
	 * deferred work completes and before the parent disables
	 * interrupts on the link
	 */
	mutex_lock(&rt711_sdca->disable_irq_lock);
	rt711_sdca->disable_irq = true;
	ret1 = sdw_update_no_pm(slave, SDW_SCP_SDCA_INTMASK1,
				SDW_SCP_SDCA_INTMASK_SDCA_0, 0);
	ret2 = sdw_update_no_pm(slave, SDW_SCP_SDCA_INTMASK2,
				SDW_SCP_SDCA_INTMASK_SDCA_8, 0);
	mutex_unlock(&rt711_sdca->disable_irq_lock);

	if (ret1 < 0 || ret2 < 0) {
		/* log but don't prevent suspend from happening */
		dev_dbg(&slave->dev, "%s: could not disable SDCA interrupts\n:", __func__);
	}

	return rt711_sdca_dev_suspend(dev);
}

#define RT711_PROBE_TIMEOUT 5000

static int __maybe_unused rt711_sdca_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt711_sdca_priv *rt711 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt711->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT711_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		sdw_show_ping_status(slave->bus, true);

		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt711->regmap, false);
	regcache_sync(rt711->regmap);
	regcache_cache_only(rt711->mbq_regmap, false);
	regcache_sync(rt711->mbq_regmap);
	return 0;
}

static const struct dev_pm_ops rt711_sdca_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt711_sdca_dev_system_suspend, rt711_sdca_dev_resume)
	SET_RUNTIME_PM_OPS(rt711_sdca_dev_suspend, rt711_sdca_dev_resume, NULL)
};

static struct sdw_driver rt711_sdca_sdw_driver = {
	.driver = {
		.name = "rt711-sdca",
		.owner = THIS_MODULE,
		.pm = &rt711_sdca_pm,
	},
	.probe = rt711_sdca_sdw_probe,
	.remove = rt711_sdca_sdw_remove,
	.ops = &rt711_sdca_slave_ops,
	.id_table = rt711_sdca_id,
};
module_sdw_driver(rt711_sdca_sdw_driver);

MODULE_DESCRIPTION("ASoC RT711 SDCA SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
