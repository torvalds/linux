// SPDX-License-Identifier: GPL-2.0
//
// rt711-sdw.c -- rt711 ALSA SoC audio driver
//
// Copyright(c) 2019 Realtek Semiconductor Corp.
//
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "rt711.h"
#include "rt711-sdw.h"

static bool rt711_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00e0:
	case 0x00f0:
	case 0x2012 ... 0x2016:
	case 0x201a ... 0x2027:
	case 0x2029 ... 0x202a:
	case 0x202d ... 0x2034:
	case 0x2201 ... 0x2204:
	case 0x2206 ... 0x2212:
	case 0x2220 ... 0x2223:
	case 0x2230 ... 0x2239:
	case 0x2f01 ... 0x2f0f:
	case 0x3000 ... 0x3fff:
	case 0x7000 ... 0x7fff:
	case 0x8300 ... 0x83ff:
	case 0x9c00 ... 0x9cff:
	case 0xb900 ... 0xb9ff:
	case 0x752009:
	case 0x752011:
	case 0x75201a:
	case 0x752045:
	case 0x752046:
	case 0x752048:
	case 0x75204a:
	case 0x75206b:
	case 0x75206f:
	case 0x752080:
	case 0x752081:
	case 0x752091:
	case 0x755800:
		return true;
	default:
		return false;
	}
}

static bool rt711_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2016:
	case 0x201b:
	case 0x201c:
	case 0x201d:
	case 0x201f:
	case 0x2021:
	case 0x2023:
	case 0x2230:
	case 0x2012 ... 0x2015: /* HD-A read */
	case 0x202d ... 0x202f: /* BRA */
	case 0x2201 ... 0x2212: /* i2c debug */
	case 0x2220 ... 0x2223: /* decoded HD-A */
	case 0x9c00 ... 0x9cff:
	case 0xb900 ... 0xb9ff:
	case 0xff01:
	case 0x75201a:
	case 0x752046:
	case 0x752080:
	case 0x752081:
	case 0x755800:
		return true;
	default:
		return false;
	}
}

static int rt711_sdw_read(void *context, unsigned int reg, unsigned int *val)
{
	struct device *dev = context;
	struct rt711_priv *rt711 = dev_get_drvdata(dev);
	unsigned int sdw_data_3, sdw_data_2, sdw_data_1, sdw_data_0;
	unsigned int reg2 = 0, reg3 = 0, reg4 = 0, mask, nid, val2;
	unsigned int is_hda_reg = 1, is_index_reg = 0;
	int ret;

	if (reg > 0xffff)
		is_index_reg = 1;

	mask = reg & 0xf000;

	if (is_index_reg) { /* index registers */
		val2 = reg & 0xff;
		reg = reg >> 8;
		nid = reg & 0xff;
		ret = regmap_write(rt711->sdw_regmap, reg, 0);
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg2, val2);
		if (ret < 0)
			return ret;

		reg3 = RT711_PRIV_DATA_R_H | nid;
		ret = regmap_write(rt711->sdw_regmap,
			reg3, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg4 = reg3 + 0x1000;
		reg4 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg4, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if (mask   == 0x3000) {
		reg += 0x8000;
		ret = regmap_write(rt711->sdw_regmap, reg, *val);
		if (ret < 0)
			return ret;
	} else if (mask == 0x7000) {
		reg += 0x2000;
		reg |= 0x800;
		ret = regmap_write(rt711->sdw_regmap,
			reg, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg2, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if ((reg & 0xff00) == 0x8300) { /* for R channel */
		reg2 = reg - 0x1000;
		reg2 &= ~0x80;
		ret = regmap_write(rt711->sdw_regmap,
			reg2, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		ret = regmap_write(rt711->sdw_regmap, reg, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if (mask == 0x9000) {
		ret = regmap_write(rt711->sdw_regmap,
			reg, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg2, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if (mask == 0xb000) {
		ret = regmap_write(rt711->sdw_regmap, reg, *val);
		if (ret < 0)
			return ret;
	} else {
		ret = regmap_read(rt711->sdw_regmap, reg, val);
		if (ret < 0)
			return ret;
		is_hda_reg = 0;
	}

	if (is_hda_reg || is_index_reg) {
		sdw_data_3 = 0;
		sdw_data_2 = 0;
		sdw_data_1 = 0;
		sdw_data_0 = 0;
		ret = regmap_read(rt711->sdw_regmap,
			RT711_READ_HDA_3, &sdw_data_3);
		if (ret < 0)
			return ret;
		ret = regmap_read(rt711->sdw_regmap,
			RT711_READ_HDA_2, &sdw_data_2);
		if (ret < 0)
			return ret;
		ret = regmap_read(rt711->sdw_regmap,
			RT711_READ_HDA_1, &sdw_data_1);
		if (ret < 0)
			return ret;
		ret = regmap_read(rt711->sdw_regmap,
			RT711_READ_HDA_0, &sdw_data_0);
		if (ret < 0)
			return ret;
		*val = ((sdw_data_3 & 0xff) << 24) |
			((sdw_data_2 & 0xff) << 16) |
			((sdw_data_1 & 0xff) << 8) | (sdw_data_0 & 0xff);
	}

	if (is_hda_reg == 0)
		dev_dbg(dev, "[%s] %04x => %08x\n", __func__, reg, *val);
	else if (is_index_reg)
		dev_dbg(dev, "[%s] %04x %04x %04x %04x => %08x\n",
			__func__, reg, reg2, reg3, reg4, *val);
	else
		dev_dbg(dev, "[%s] %04x %04x => %08x\n",
			__func__, reg, reg2, *val);

	return 0;
}

static int rt711_sdw_write(void *context, unsigned int reg, unsigned int val)
{
	struct device *dev = context;
	struct rt711_priv *rt711 = dev_get_drvdata(dev);
	unsigned int reg2 = 0, reg3, reg4, nid, mask, val2;
	unsigned int is_index_reg = 0;
	int ret;

	if (reg > 0xffff)
		is_index_reg = 1;

	mask = reg & 0xf000;

	if (is_index_reg) { /* index registers */
		val2 = reg & 0xff;
		reg = reg >> 8;
		nid = reg & 0xff;
		ret = regmap_write(rt711->sdw_regmap, reg, 0);
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg2, val2);
		if (ret < 0)
			return ret;

		reg3 = RT711_PRIV_DATA_W_H | nid;
		ret = regmap_write(rt711->sdw_regmap,
			reg3, ((val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg4 = reg3 + 0x1000;
		reg4 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg4, (val & 0xff));
		if (ret < 0)
			return ret;
		is_index_reg = 1;
	} else if (reg < 0x4fff) {
		ret = regmap_write(rt711->sdw_regmap, reg, val);
		if (ret < 0)
			return ret;
	} else if (reg == RT711_FUNC_RESET) {
		ret = regmap_write(rt711->sdw_regmap, reg, val);
		if (ret < 0)
			return ret;
	} else if (mask == 0x7000) {
		ret = regmap_write(rt711->sdw_regmap,
			reg, ((val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt711->sdw_regmap, reg2, (val & 0xff));
		if (ret < 0)
			return ret;
	} else if ((reg & 0xff00) == 0x8300) {  /* for R channel */
		reg2 = reg - 0x1000;
		reg2 &= ~0x80;
		ret = regmap_write(rt711->sdw_regmap,
			reg2, ((val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		ret = regmap_write(rt711->sdw_regmap, reg, (val & 0xff));
		if (ret < 0)
			return ret;
	}

	if (reg2 == 0)
		dev_dbg(dev, "[%s] %04x <= %04x\n", __func__, reg, val);
	else if (is_index_reg)
		dev_dbg(dev, "[%s] %04x %04x %04x %04x <= %04x %04x\n",
			__func__, reg, reg2, reg3, reg4, val2, val);
	else
		dev_dbg(dev, "[%s] %04x %04x <= %04x\n",
			__func__, reg, reg2, val);

	return 0;
}

static const struct regmap_config rt711_regmap = {
	.reg_bits = 24,
	.val_bits = 32,
	.readable_reg = rt711_readable_register,
	.volatile_reg = rt711_volatile_register,
	.max_register = 0x755800,
	.reg_defaults = rt711_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt711_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
	.reg_read = rt711_sdw_read,
	.reg_write = rt711_sdw_write,
};

static const struct regmap_config rt711_sdw_regmap = {
	.name = "sdw",
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt711_readable_register,
	.max_register = 0xff01,
	.cache_type = REGCACHE_NONE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt711_update_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	struct rt711_priv *rt711 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt711->status = status;

	if (status == SDW_SLAVE_UNATTACHED)
		rt711->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt711->hw_init || rt711->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt711_io_init(&slave->dev, slave);
}

static int rt711_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval;
	int i, j;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->scp_int1_mask = SDW_SCP_INT1_IMPL_DEF | SDW_SCP_INT1_BUS_CLASH |
		SDW_SCP_INT1_PARITY;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;

	prop->paging_support = false;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x14; /* BITMAP: 00010100 */
	prop->sink_ports = 0x8; /* BITMAP:  00001000 */

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
						sizeof(*prop->src_dpn_prop),
						GFP_KERNEL);
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
						sizeof(*prop->sink_dpn_prop),
						GFP_KERNEL);
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
	prop->clk_stop_timeout = 20;

	/* wake-up event */
	prop->wake_capable = 1;

	return 0;
}

static int rt711_bus_config(struct sdw_slave *slave,
				struct sdw_bus_params *params)
{
	struct rt711_priv *rt711 = dev_get_drvdata(&slave->dev);
	int ret;

	memcpy(&rt711->params, params, sizeof(*params));

	ret = rt711_clock_config(&slave->dev);
	if (ret < 0)
		dev_err(&slave->dev, "Invalid clk config");

	return ret;
}

static int rt711_interrupt_callback(struct sdw_slave *slave,
					struct sdw_slave_intr_status *status)
{
	struct rt711_priv *rt711 = dev_get_drvdata(&slave->dev);

	dev_dbg(&slave->dev,
		"%s control_port_stat=%x", __func__, status->control_port);

	mutex_lock(&rt711->disable_irq_lock);
	if (status->control_port & 0x4 && !rt711->disable_irq) {
		mod_delayed_work(system_power_efficient_wq,
			&rt711->jack_detect_work, msecs_to_jiffies(250));
	}
	mutex_unlock(&rt711->disable_irq_lock);

	return 0;
}

static const struct sdw_slave_ops rt711_slave_ops = {
	.read_prop = rt711_read_prop,
	.interrupt_callback = rt711_interrupt_callback,
	.update_status = rt711_update_status,
	.bus_config = rt711_bus_config,
};

static int rt711_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *sdw_regmap, *regmap;

	/* Regmap Initialization */
	sdw_regmap = devm_regmap_init_sdw(slave, &rt711_sdw_regmap);
	if (IS_ERR(sdw_regmap))
		return PTR_ERR(sdw_regmap);

	regmap = devm_regmap_init(&slave->dev, NULL,
		&slave->dev, &rt711_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	rt711_init(&slave->dev, sdw_regmap, regmap, slave);

	return 0;
}

static int rt711_sdw_remove(struct sdw_slave *slave)
{
	struct rt711_priv *rt711 = dev_get_drvdata(&slave->dev);

	if (rt711->hw_init) {
		cancel_delayed_work_sync(&rt711->jack_detect_work);
		cancel_delayed_work_sync(&rt711->jack_btn_check_work);
		cancel_work_sync(&rt711->calibration_work);
	}

	if (rt711->first_hw_init)
		pm_runtime_disable(&slave->dev);

	mutex_destroy(&rt711->calibrate_mutex);
	mutex_destroy(&rt711->disable_irq_lock);

	return 0;
}

static const struct sdw_device_id rt711_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x711, 0x2, 0, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt711_id);

static int __maybe_unused rt711_dev_suspend(struct device *dev)
{
	struct rt711_priv *rt711 = dev_get_drvdata(dev);

	if (!rt711->hw_init)
		return 0;

	cancel_delayed_work_sync(&rt711->jack_detect_work);
	cancel_delayed_work_sync(&rt711->jack_btn_check_work);
	cancel_work_sync(&rt711->calibration_work);

	regcache_cache_only(rt711->regmap, true);

	return 0;
}

static int __maybe_unused rt711_dev_system_suspend(struct device *dev)
{
	struct rt711_priv *rt711 = dev_get_drvdata(dev);
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret;

	if (!rt711->hw_init)
		return 0;

	/*
	 * prevent new interrupts from being handled after the
	 * deferred work completes and before the parent disables
	 * interrupts on the link
	 */
	mutex_lock(&rt711->disable_irq_lock);
	rt711->disable_irq = true;
	ret = sdw_update_no_pm(slave, SDW_SCP_INTMASK1,
			       SDW_SCP_INT1_IMPL_DEF, 0);
	mutex_unlock(&rt711->disable_irq_lock);

	if (ret < 0) {
		/* log but don't prevent suspend from happening */
		dev_dbg(&slave->dev, "%s: could not disable imp-def interrupts\n:", __func__);
	}

	return rt711_dev_suspend(dev);
}

#define RT711_PROBE_TIMEOUT 5000

static int __maybe_unused rt711_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt711_priv *rt711 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt711->first_hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT711_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt711->regmap, false);
	regcache_sync_region(rt711->regmap, 0x3000, 0x8fff);
	regcache_sync_region(rt711->regmap, 0x752009, 0x752091);

	return 0;
}

static const struct dev_pm_ops rt711_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt711_dev_system_suspend, rt711_dev_resume)
	SET_RUNTIME_PM_OPS(rt711_dev_suspend, rt711_dev_resume, NULL)
};

static struct sdw_driver rt711_sdw_driver = {
	.driver = {
		.name = "rt711",
		.owner = THIS_MODULE,
		.pm = &rt711_pm,
	},
	.probe = rt711_sdw_probe,
	.remove = rt711_sdw_remove,
	.ops = &rt711_slave_ops,
	.id_table = rt711_id,
};
module_sdw_driver(rt711_sdw_driver);

MODULE_DESCRIPTION("ASoC RT711 SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
