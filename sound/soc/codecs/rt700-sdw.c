// SPDX-License-Identifier: GPL-2.0
//
// rt700-sdw.c -- rt700 ALSA SoC audio driver
//
// Copyright(c) 2019 Realtek Semiconductor Corp.
//
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "rt700.h"
#include "rt700-sdw.h"

static bool rt700_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x00e0:
	case 0x00f0:
	case 0x2000 ... 0x200e:
	case 0x2012 ... 0x2016:
	case 0x201a ... 0x2027:
	case 0x2029 ... 0x202a:
	case 0x202d ... 0x2034:
	case 0x2200 ... 0x2204:
	case 0x2206 ... 0x2212:
	case 0x2220 ... 0x2223:
	case 0x2230 ... 0x2231:
	case 0x3000 ... 0x3fff:
	case 0x7000 ... 0x7fff:
	case 0x8300 ... 0x83ff:
	case 0x9c00 ... 0x9cff:
	case 0xb900 ... 0xb9ff:
	case 0x75201a:
	case 0x752045:
	case 0x752046:
	case 0x752048:
	case 0x75204a:
	case 0x75206b:
	case 0x752080:
	case 0x752081:
		return true;
	default:
		return false;
	}
}

static bool rt700_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2009:
	case 0x2016:
	case 0x201b:
	case 0x201c:
	case 0x201d:
	case 0x201f:
	case 0x2021:
	case 0x2023:
	case 0x2230:
	case 0x200b ... 0x200e: /* i2c read */
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
		return true;
	default:
		return false;
	}
}

static int rt700_sdw_read(void *context, unsigned int reg, unsigned int *val)
{
	struct device *dev = context;
	struct rt700_priv *rt700 = dev_get_drvdata(dev);
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
		ret = regmap_write(rt700->sdw_regmap, reg, 0);
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg2, val2);
		if (ret < 0)
			return ret;

		reg3 = RT700_PRIV_DATA_R_H | nid;
		ret = regmap_write(rt700->sdw_regmap,
			reg3, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg4 = reg3 + 0x1000;
		reg4 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg4, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if (mask   == 0x3000) {
		reg += 0x8000;
		ret = regmap_write(rt700->sdw_regmap, reg, *val);
		if (ret < 0)
			return ret;
	} else if (mask == 0x7000) {
		reg += 0x2000;
		reg |= 0x800;
		ret = regmap_write(rt700->sdw_regmap,
			reg, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg2, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if ((reg & 0xff00) == 0x8300) { /* for R channel */
		reg2 = reg - 0x1000;
		reg2 &= ~0x80;
		ret = regmap_write(rt700->sdw_regmap,
			reg2, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		ret = regmap_write(rt700->sdw_regmap, reg, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if (mask == 0x9000) {
		ret = regmap_write(rt700->sdw_regmap,
			reg, ((*val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg2, (*val & 0xff));
		if (ret < 0)
			return ret;
	} else if (mask == 0xb000) {
		ret = regmap_write(rt700->sdw_regmap, reg, *val);
		if (ret < 0)
			return ret;
	} else {
		ret = regmap_read(rt700->sdw_regmap, reg, val);
		if (ret < 0)
			return ret;
		is_hda_reg = 0;
	}

	if (is_hda_reg || is_index_reg) {
		sdw_data_3 = 0;
		sdw_data_2 = 0;
		sdw_data_1 = 0;
		sdw_data_0 = 0;
		ret = regmap_read(rt700->sdw_regmap,
			RT700_READ_HDA_3, &sdw_data_3);
		if (ret < 0)
			return ret;
		ret = regmap_read(rt700->sdw_regmap,
			RT700_READ_HDA_2, &sdw_data_2);
		if (ret < 0)
			return ret;
		ret = regmap_read(rt700->sdw_regmap,
			RT700_READ_HDA_1, &sdw_data_1);
		if (ret < 0)
			return ret;
		ret = regmap_read(rt700->sdw_regmap,
			RT700_READ_HDA_0, &sdw_data_0);
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

static int rt700_sdw_write(void *context, unsigned int reg, unsigned int val)
{
	struct device *dev = context;
	struct rt700_priv *rt700 = dev_get_drvdata(dev);
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
		ret = regmap_write(rt700->sdw_regmap, reg, 0);
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg2, val2);
		if (ret < 0)
			return ret;

		reg3 = RT700_PRIV_DATA_W_H | nid;
		ret = regmap_write(rt700->sdw_regmap,
			reg3, ((val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg4 = reg3 + 0x1000;
		reg4 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg4, (val & 0xff));
		if (ret < 0)
			return ret;
		is_index_reg = 1;
	} else if (reg < 0x4fff) {
		ret = regmap_write(rt700->sdw_regmap, reg, val);
		if (ret < 0)
			return ret;
	} else if (reg == 0xff01) {
		ret = regmap_write(rt700->sdw_regmap, reg, val);
		if (ret < 0)
			return ret;
	} else if (mask == 0x7000) {
		ret = regmap_write(rt700->sdw_regmap,
			reg, ((val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		reg2 = reg + 0x1000;
		reg2 |= 0x80;
		ret = regmap_write(rt700->sdw_regmap, reg2, (val & 0xff));
		if (ret < 0)
			return ret;
	} else if ((reg & 0xff00) == 0x8300) {  /* for R channel */
		reg2 = reg - 0x1000;
		reg2 &= ~0x80;
		ret = regmap_write(rt700->sdw_regmap,
			reg2, ((val >> 8) & 0xff));
		if (ret < 0)
			return ret;
		ret = regmap_write(rt700->sdw_regmap, reg, (val & 0xff));
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

static const struct regmap_config rt700_regmap = {
	.reg_bits = 24,
	.val_bits = 32,
	.readable_reg = rt700_readable_register,
	.volatile_reg = rt700_volatile_register,
	.max_register = 0x755800,
	.reg_defaults = rt700_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rt700_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
	.reg_read = rt700_sdw_read,
	.reg_write = rt700_sdw_write,
};

static const struct regmap_config rt700_sdw_regmap = {
	.name = "sdw",
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt700_readable_register,
	.max_register = 0xff01,
	.cache_type = REGCACHE_NONE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt700_update_status(struct sdw_slave *slave,
					enum sdw_slave_status status)
{
	struct rt700_priv *rt700 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt700->status = status;

	if (status == SDW_SLAVE_UNATTACHED)
		rt700->hw_init = false;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt700->hw_init || rt700->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt700_io_init(&slave->dev, slave);
}

static int rt700_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval, i, num_of_ports = 1;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->paging_support = false;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x14; /* BITMAP: 00010100 */
	prop->sink_ports = 0xA; /* BITMAP:  00001010 */

	nval = hweight32(prop->source_ports);
	num_of_ports += nval;
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
	num_of_ports += nval;
	prop->sink_dpn_prop = devm_kcalloc(&slave->dev, nval,
						sizeof(*prop->sink_dpn_prop),
						GFP_KERNEL);
	if (!prop->sink_dpn_prop)
		return -ENOMEM;

	i = 0;
	dpn = prop->sink_dpn_prop;
	addr = prop->sink_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].type = SDW_DPN_FULL;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* Allocate port_ready based on num_of_ports */
	slave->port_ready = devm_kcalloc(&slave->dev, num_of_ports,
					sizeof(*slave->port_ready),
					GFP_KERNEL);
	if (!slave->port_ready)
		return -ENOMEM;

	/* Initialize completion */
	for (i = 0; i < num_of_ports; i++)
		init_completion(&slave->port_ready[i]);

	/* set the timeout values */
	prop->clk_stop_timeout = 20;

	/* wake-up event */
	prop->wake_capable = 1;

	return 0;
}

static int rt700_bus_config(struct sdw_slave *slave,
				struct sdw_bus_params *params)
{
	struct rt700_priv *rt700 = dev_get_drvdata(&slave->dev);
	int ret;

	memcpy(&rt700->params, params, sizeof(*params));

	ret = rt700_clock_config(&slave->dev);
	if (ret < 0)
		dev_err(&slave->dev, "Invalid clk config");

	return ret;
}

static int rt700_interrupt_callback(struct sdw_slave *slave,
					struct sdw_slave_intr_status *status)
{
	struct rt700_priv *rt700 = dev_get_drvdata(&slave->dev);

	dev_dbg(&slave->dev,
		"%s control_port_stat=%x", __func__, status->control_port);

	if (status->control_port & 0x4) {
		mod_delayed_work(system_power_efficient_wq,
			&rt700->jack_detect_work, msecs_to_jiffies(250));
	}

	return 0;
}

/*
 * slave_ops: callbacks for get_clock_stop_mode, clock_stop and
 * port_prep are not defined for now
 */
static struct sdw_slave_ops rt700_slave_ops = {
	.read_prop = rt700_read_prop,
	.interrupt_callback = rt700_interrupt_callback,
	.update_status = rt700_update_status,
	.bus_config = rt700_bus_config,
};

static int rt700_sdw_probe(struct sdw_slave *slave,
				const struct sdw_device_id *id)
{
	struct regmap *sdw_regmap, *regmap;

	/* Assign ops */
	slave->ops = &rt700_slave_ops;

	/* Regmap Initialization */
	sdw_regmap = devm_regmap_init_sdw(slave, &rt700_sdw_regmap);
	if (!sdw_regmap)
		return -EINVAL;

	regmap = devm_regmap_init(&slave->dev, NULL,
		&slave->dev, &rt700_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	rt700_init(&slave->dev, sdw_regmap, regmap, slave);

	return 0;
}

static int rt700_sdw_remove(struct sdw_slave *slave)
{
	struct rt700_priv *rt700 = dev_get_drvdata(&slave->dev);

	if (rt700 && rt700->hw_init) {
		cancel_delayed_work(&rt700->jack_detect_work);
		cancel_delayed_work(&rt700->jack_btn_check_work);
	}

	return 0;
}

static const struct sdw_device_id rt700_id[] = {
	SDW_SLAVE_ENTRY(0x025d, 0x700, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt700_id);

static int rt700_dev_suspend(struct device *dev)
{
	struct rt700_priv *rt700 = dev_get_drvdata(dev);

	if (!rt700->hw_init)
		return 0;

	regcache_cache_only(rt700->regmap, true);

	return 0;
}

#define RT700_PROBE_TIMEOUT 2000

static int rt700_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt700_priv *rt700 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt700->hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->initialization_complete,
				msecs_to_jiffies(RT700_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Initialization not complete, timed out\n");
		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt700->regmap, false);
	regcache_sync_region(rt700->regmap, 0x3000, 0x8fff);
	regcache_sync_region(rt700->regmap, 0x752010, 0x75206b);

	return 0;
}

static const struct dev_pm_ops rt700_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt700_dev_suspend, rt700_dev_resume)
	SET_RUNTIME_PM_OPS(rt700_dev_suspend, rt700_dev_resume, NULL)
};

static struct sdw_driver rt700_sdw_driver = {
	.driver = {
		.name = "rt700",
		.owner = THIS_MODULE,
		.pm = &rt700_pm,
	},
	.probe = rt700_sdw_probe,
	.remove = rt700_sdw_remove,
	.ops = &rt700_slave_ops,
	.id_table = rt700_id,
};
module_sdw_driver(rt700_sdw_driver);

MODULE_DESCRIPTION("ASoC RT700 driver SDW");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL v2");
