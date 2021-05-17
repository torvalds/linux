// SPDX-License-Identifier: GPL-2.0-only
//
// rt715-sdca-sdw.c -- rt715 ALSA SoC audio driver
//
// Copyright(c) 2020 Realtek Semiconductor Corp.
//
//

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "rt715-sdca.h"
#include "rt715-sdca-sdw.h"

static bool rt715_sdca_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x201a ... 0x2027:
	case 0x2029 ... 0x202a:
	case 0x202d ... 0x2034:
	case 0x2200 ... 0x2204:
	case 0x2206 ... 0x2212:
	case 0x2230 ... 0x2239:
	case 0x2f5b:
	case SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_SMPU_TRIG_ST_EN,
		RT715_SDCA_SMPU_TRIG_ST_CTRL, CH_00):
		return true;
	default:
		return false;
	}
}

static bool rt715_sdca_volatile_register(struct device *dev, unsigned int reg)
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
	case 0x2f07:
	case 0x2f1b ... 0x2f1e:
	case 0x2f30 ... 0x2f34:
	case 0x2f50 ... 0x2f51:
	case 0x2f53 ... 0x2f59:
	case 0x2f5c ... 0x2f5f:
	case SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_SMPU_TRIG_ST_EN,
		RT715_SDCA_SMPU_TRIG_ST_CTRL, CH_00): /* VAD Searching status */
		return true;
	default:
		return false;
	}
}

static bool rt715_sdca_mbq_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2000000:
	case 0x200002b:
	case 0x2000036:
	case 0x2000037:
	case 0x2000039:
	case 0x6100000:
		return true;
	default:
		return false;
	}
}

static bool rt715_sdca_mbq_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x2000000:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rt715_sdca_regmap = {
	.reg_bits = 32,
	.val_bits = 8,
	.readable_reg = rt715_sdca_readable_register,
	.volatile_reg = rt715_sdca_volatile_register,
	.max_register = 0x43ffffff,
	.reg_defaults = rt715_reg_defaults_sdca,
	.num_reg_defaults = ARRAY_SIZE(rt715_reg_defaults_sdca),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static const struct regmap_config rt715_sdca_mbq_regmap = {
	.name = "sdw-mbq",
	.reg_bits = 32,
	.val_bits = 16,
	.readable_reg = rt715_sdca_mbq_readable_register,
	.volatile_reg = rt715_sdca_mbq_volatile_register,
	.max_register = 0x43ffffff,
	.reg_defaults = rt715_mbq_reg_defaults_sdca,
	.num_reg_defaults = ARRAY_SIZE(rt715_mbq_reg_defaults_sdca),
	.cache_type = REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};

static int rt715_sdca_update_status(struct sdw_slave *slave,
				enum sdw_slave_status status)
{
	struct rt715_sdca_priv *rt715 = dev_get_drvdata(&slave->dev);

	/* Update the status */
	rt715->status = status;

	/*
	 * Perform initialization only if slave status is present and
	 * hw_init flag is false
	 */
	if (rt715->hw_init || rt715->status != SDW_SLAVE_ATTACHED)
		return 0;

	/* perform I/O transfers required for Slave initialization */
	return rt715_sdca_io_init(&slave->dev, slave);
}

static int rt715_sdca_read_prop(struct sdw_slave *slave)
{
	struct sdw_slave_prop *prop = &slave->prop;
	int nval, i;
	u32 bit;
	unsigned long addr;
	struct sdw_dpn_prop *dpn;

	prop->paging_support = true;

	/* first we need to allocate memory for set bits in port lists */
	prop->source_ports = 0x50;/* BITMAP: 01010000 */
	prop->sink_ports = 0x0;	/* BITMAP:  00000000 */

	nval = hweight32(prop->source_ports);
	prop->src_dpn_prop = devm_kcalloc(&slave->dev, nval,
					sizeof(*prop->src_dpn_prop),
					GFP_KERNEL);
	if (!prop->src_dpn_prop)
		return -ENOMEM;

	dpn = prop->src_dpn_prop;
	i = 0;
	addr = prop->source_ports;
	for_each_set_bit(bit, &addr, 32) {
		dpn[i].num = bit;
		dpn[i].simple_ch_prep_sm = true;
		dpn[i].ch_prep_timeout = 10;
		i++;
	}

	/* set the timeout values */
	prop->clk_stop_timeout = 20;

	return 0;
}

static struct sdw_slave_ops rt715_sdca_slave_ops = {
	.read_prop = rt715_sdca_read_prop,
	.update_status = rt715_sdca_update_status,
};

static int rt715_sdca_sdw_probe(struct sdw_slave *slave,
			   const struct sdw_device_id *id)
{
	struct regmap *mbq_regmap, *regmap;

	slave->ops = &rt715_sdca_slave_ops;

	/* Regmap Initialization */
	mbq_regmap = devm_regmap_init_sdw_mbq(slave, &rt715_sdca_mbq_regmap);
	if (IS_ERR(mbq_regmap))
		return PTR_ERR(mbq_regmap);

	regmap = devm_regmap_init_sdw(slave, &rt715_sdca_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rt715_sdca_init(&slave->dev, mbq_regmap, regmap, slave);
}

static const struct sdw_device_id rt715_sdca_id[] = {
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x715, 0x3, 0x1, 0),
	SDW_SLAVE_ENTRY_EXT(0x025d, 0x714, 0x3, 0x1, 0),
	{},
};
MODULE_DEVICE_TABLE(sdw, rt715_sdca_id);

static int __maybe_unused rt715_dev_suspend(struct device *dev)
{
	struct rt715_sdca_priv *rt715 = dev_get_drvdata(dev);

	if (!rt715->hw_init)
		return 0;

	regcache_cache_only(rt715->regmap, true);
	regcache_mark_dirty(rt715->regmap);
	regcache_cache_only(rt715->mbq_regmap, true);
	regcache_mark_dirty(rt715->mbq_regmap);

	return 0;
}

#define RT715_PROBE_TIMEOUT 5000

static int __maybe_unused rt715_dev_resume(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct rt715_sdca_priv *rt715 = dev_get_drvdata(dev);
	unsigned long time;

	if (!rt715->hw_init)
		return 0;

	if (!slave->unattach_request)
		goto regmap_sync;

	time = wait_for_completion_timeout(&slave->enumeration_complete,
					   msecs_to_jiffies(RT715_PROBE_TIMEOUT));
	if (!time) {
		dev_err(&slave->dev, "Enumeration not complete, timed out\n");
		return -ETIMEDOUT;
	}

regmap_sync:
	slave->unattach_request = 0;
	regcache_cache_only(rt715->regmap, false);
	regcache_sync_region(rt715->regmap,
		SDW_SDCA_CTL(FUN_JACK_CODEC, RT715_SDCA_ST_EN, RT715_SDCA_ST_CTRL,
			CH_00),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_SMPU_TRIG_ST_EN,
			RT715_SDCA_SMPU_TRIG_ST_CTRL, CH_00));
	regcache_cache_only(rt715->mbq_regmap, false);
	regcache_sync_region(rt715->mbq_regmap, 0x2000000, 0x61020ff);
	regcache_sync_region(rt715->mbq_regmap,
		SDW_SDCA_CTL(FUN_JACK_CODEC, RT715_SDCA_ST_EN, RT715_SDCA_ST_CTRL,
			CH_00),
		SDW_SDCA_CTL(FUN_MIC_ARRAY, RT715_SDCA_SMPU_TRIG_ST_EN,
			RT715_SDCA_SMPU_TRIG_ST_CTRL, CH_00));

	return 0;
}

static const struct dev_pm_ops rt715_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(rt715_dev_suspend, rt715_dev_resume)
	SET_RUNTIME_PM_OPS(rt715_dev_suspend, rt715_dev_resume, NULL)
};

static struct sdw_driver rt715_sdw_driver = {
	.driver = {
		.name = "rt715-sdca",
		.owner = THIS_MODULE,
		.pm = &rt715_pm,
	},
	.probe = rt715_sdca_sdw_probe,
	.ops = &rt715_sdca_slave_ops,
	.id_table = rt715_sdca_id,
};
module_sdw_driver(rt715_sdw_driver);

MODULE_DESCRIPTION("ASoC RT715 driver SDW SDCA");
MODULE_AUTHOR("Jack Yu <jack.yu@realtek.com>");
MODULE_LICENSE("GPL v2");
