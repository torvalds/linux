// SPDX-License-Identifier: GPL-2.0-only
//
// CS35L56 ALSA SoC audio driver SoundWire binding
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw_type.h>
#include <linux/string_choices.h>
#include <linux/swab.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>

#include "cs35l56.h"

/* Register addresses are offset when sent over SoundWire */
#define CS35L56_SDW_ADDR_OFFSET		0x8000

/* Cirrus bus bridge registers */
#define CS35L56_SDW_MEM_ACCESS_STATUS	0xd0
#define CS35L56_SDW_MEM_READ_DATA	0xd8

#define CS35L56_SDW_LAST_LATE		BIT(3)
#define CS35L56_SDW_CMD_IN_PROGRESS	BIT(2)
#define CS35L56_SDW_RDATA_RDY		BIT(0)

#define CS35L56_LATE_READ_POLL_US	10
#define CS35L56_LATE_READ_TIMEOUT_US	1000

static int cs35l56_sdw_poll_mem_status(struct sdw_slave *peripheral,
				       unsigned int mask,
				       unsigned int match)
{
	int ret, val;

	ret = read_poll_timeout(sdw_read_no_pm, val,
				(val < 0) || ((val & mask) == match),
				CS35L56_LATE_READ_POLL_US, CS35L56_LATE_READ_TIMEOUT_US,
				false, peripheral, CS35L56_SDW_MEM_ACCESS_STATUS);
	if (ret < 0)
		return ret;

	if (val < 0)
		return val;

	return 0;
}

static int cs35l56_sdw_slow_read(struct sdw_slave *peripheral, unsigned int reg,
				 u8 *buf, size_t val_size)
{
	int ret, i;

	for (i = 0; i < val_size; i += sizeof(u32)) {
		/* Poll for bus bridge idle */
		ret = cs35l56_sdw_poll_mem_status(peripheral,
						  CS35L56_SDW_CMD_IN_PROGRESS,
						  0);
		if (ret < 0) {
			dev_err(&peripheral->dev, "!CMD_IN_PROGRESS fail: %d\n", ret);
			return ret;
		}

		/* Reading LSByte triggers read of register to holding buffer */
		sdw_read_no_pm(peripheral, reg + i);

		/* Wait for data available */
		ret = cs35l56_sdw_poll_mem_status(peripheral,
						  CS35L56_SDW_RDATA_RDY,
						  CS35L56_SDW_RDATA_RDY);
		if (ret < 0) {
			dev_err(&peripheral->dev, "RDATA_RDY fail: %d\n", ret);
			return ret;
		}

		/* Read data from buffer */
		ret = sdw_nread_no_pm(peripheral, CS35L56_SDW_MEM_READ_DATA,
				      sizeof(u32), &buf[i]);
		if (ret) {
			dev_err(&peripheral->dev, "Late read @%#x failed: %d\n", reg + i, ret);
			return ret;
		}

		swab32s((u32 *)&buf[i]);
	}

	return 0;
}

static int cs35l56_sdw_read(void *context, const void *reg_buf,
			    const size_t reg_size, void *val_buf,
			    size_t val_size)
{
	struct sdw_slave *peripheral = context;
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	unsigned int reg_addr = get_unaligned_le32(reg_buf);
	int ret;

	if (cs35l56_is_otp_register(reg_addr - CS35L56_SDW_ADDR_OFFSET))
		return cs35l56_sdw_slow_read(peripheral, reg_addr, (u8 *)val_buf, val_size);

	ret = regmap_raw_read(cs35l56->sdw_bus_regmap, reg_addr, val_buf, val_size);
	if (ret)
		return ret;

	swab32_array((u32 *)val_buf, val_size / sizeof(u32));

	return 0;
}

static inline void cs35l56_swab_copy(void *dest, const void *src, size_t nbytes)
{
	u32 *dest32 = dest;
	const u32 *src32 = src;

	for (; nbytes > 0; nbytes -= 4)
		*dest32++ = swab32(*src32++);
}

static int cs35l56_sdw_gather_write(void *context,
				    const void *reg_buf, size_t reg_size,
				    const void *val_buf, size_t val_size)
{
	struct sdw_slave *peripheral = context;
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	unsigned int reg_addr = get_unaligned_le32(reg_buf);
	u32 swab_buf[64];	/* Define u32 so it is 32-bit aligned */
	int ret;

	while (val_size > sizeof(swab_buf)) {
		cs35l56_swab_copy(swab_buf, val_buf, sizeof(swab_buf));
		ret = regmap_raw_write(cs35l56->sdw_bus_regmap, reg_addr,
				       swab_buf, sizeof(swab_buf));
		if (ret)
			return ret;

		val_size -= sizeof(swab_buf);
		reg_addr += sizeof(swab_buf);
		val_buf += sizeof(swab_buf);
	}

	if (val_size == 0)
		return 0;

	cs35l56_swab_copy(swab_buf, val_buf, val_size);

	return regmap_raw_write(cs35l56->sdw_bus_regmap, reg_addr, swab_buf, val_size);
}

static int cs35l56_sdw_write(void *context, const void *val_buf, size_t val_size)
{
	const u8 *src_buf = val_buf;

	/* First word of val_buf contains the destination address */
	return cs35l56_sdw_gather_write(context, &src_buf[0], 4, &src_buf[4], val_size - 4);
}

/*
 * Registers are big-endian on I2C and SPI but little-endian on SoundWire.
 * Exported firmware controls are big-endian on I2C/SPI but little-endian on
 * SoundWire. Firmware files are always big-endian and are opaque blobs.
 * Present a big-endian regmap and hide the endianness swap, so that the ALSA
 * byte controls always have the same byte order, and firmware file blobs
 * can be written verbatim.
 */
static const struct regmap_bus cs35l56_regmap_swab_bus_sdw = {
	.read = cs35l56_sdw_read,
	.write = cs35l56_sdw_write,
	.gather_write = cs35l56_sdw_gather_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

/* Low-level SoundWire regmap to transfer the data over the bus */
static const struct regmap_config cs35l56_sdw_bus_regmap = {
	.name = "sdw-le32",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = CS35L56_DSP1_PMEM_5114 + 0x8000,
	.cache_type = REGCACHE_NONE,
};

static int cs35l56_sdw_get_unique_id(struct cs35l56_private *cs35l56)
{
	int ret;

	ret = sdw_read_no_pm(cs35l56->sdw_peripheral, SDW_SCP_DEVID_0);
	if (ret < 0)
		return ret;

	cs35l56->sdw_unique_id = ret & 0xf;

	return 0;
}

static void cs35l56_sdw_init(struct sdw_slave *peripheral)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	int ret;

	pm_runtime_get_noresume(cs35l56->base.dev);

	ret = cs35l56_sdw_get_unique_id(cs35l56);
	if (ret)
		goto out;

	/* SoundWire UniqueId is used to index the calibration array */
	if (cs35l56->base.cal_index < 0)
		cs35l56->base.cal_index = cs35l56->sdw_unique_id;

	ret = cs35l56_init(cs35l56);
	if (ret < 0) {
		regcache_cache_only(cs35l56->base.regmap, true);
		goto out;
	}

	/*
	 * cs35l56_init can return with !init_done if it triggered
	 * a soft reset.
	 */
	if (cs35l56->base.init_done)
		cs35l56_unmask_soundwire_interrupts(cs35l56->sdw_peripheral);

out:
	pm_runtime_put_autosuspend(cs35l56->base.dev);
}

static int cs35l56_sdw_interrupt(struct sdw_slave *peripheral,
				 struct sdw_slave_intr_status *status)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);

	/* SoundWire core holds our pm_runtime when calling this function. */

	dev_dbg(cs35l56->base.dev, "int control_port=%#x\n", status->control_port);

	if ((status->control_port & SDW_SCP_INT1_IMPL_DEF) == 0)
		return 0;

	/*
	 * Prevent bus manager suspending and possibly issuing a
	 * bus-reset before the queued work has run.
	 */
	pm_runtime_get_noresume(cs35l56->base.dev);

	/*
	 * Mask and clear until it has been handled.
	 * None of the interrupts are time-critical so use the
	 * power-efficient queue.
	 */
	cs35l56_mask_soundwire_interrupts(peripheral);
	queue_work(system_power_efficient_wq, &cs35l56->sdw_irq_work);

	return 0;
}

static void cs35l56_sdw_irq_work(struct work_struct *work)
{
	struct cs35l56_private *cs35l56 = container_of(work,
						       struct cs35l56_private,
						       sdw_irq_work);

	cs35l56_irq(-1, &cs35l56->base);

	/* unmask interrupts */
	if (!cs35l56->sdw_irq_no_unmask)
		cs35l56_unmask_soundwire_interrupts(cs35l56->sdw_peripheral);

	pm_runtime_put_autosuspend(cs35l56->base.dev);
}

static int cs35l56_sdw_read_prop(struct sdw_slave *peripheral)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	struct sdw_slave_prop *prop = &peripheral->prop;
	struct sdw_dpn_prop *ports;
	u8 clock_stop_1 = false;
	int ret;

	ret = fwnode_property_read_u8(dev_fwnode(cs35l56->base.dev),
				      "mipi-sdw-clock-stop-mode1-supported",
				      &clock_stop_1);
	if (ret == 0)
		prop->clk_stop_mode1 = !!clock_stop_1;

	ports = devm_kcalloc(cs35l56->base.dev, 2, sizeof(*ports), GFP_KERNEL);
	if (!ports)
		return -ENOMEM;

	prop->source_ports = BIT(CS35L56_SDW1_CAPTURE_PORT);
	prop->sink_ports = BIT(CS35L56_SDW1_PLAYBACK_PORT);
	prop->paging_support = true;
	prop->quirks = SDW_SLAVE_QUIRKS_INVALID_INITIAL_PARITY;
	prop->scp_int1_mask = SDW_SCP_INT1_BUS_CLASH | SDW_SCP_INT1_PARITY | SDW_SCP_INT1_IMPL_DEF;

	/* DP1 - playback */
	ports[0].num = CS35L56_SDW1_PLAYBACK_PORT;
	ports[0].type = SDW_DPN_FULL;
	ports[0].ch_prep_timeout = 10;
	prop->sink_dpn_prop = &ports[0];

	/* DP3 - capture */
	ports[1].num = CS35L56_SDW1_CAPTURE_PORT;
	ports[1].type = SDW_DPN_FULL;
	ports[1].ch_prep_timeout = 10;
	prop->src_dpn_prop = &ports[1];

	dev_dbg(&peripheral->dev, "clock stop mode 1 supported: %s\n",
		str_yes_no(prop->clk_stop_mode1));

	return 0;
}

static int cs35l56_sdw_update_status(struct sdw_slave *peripheral,
				     enum sdw_slave_status status)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);

	switch (status) {
	case SDW_SLAVE_ATTACHED:
		if (cs35l56->sdw_attached)
			break;

		dev_dbg(cs35l56->base.dev, "%s: ATTACHED\n", __func__);
		if (!cs35l56->base.init_done || cs35l56->soft_resetting)
			cs35l56_sdw_init(peripheral);

		cs35l56->sdw_attached = true;
		break;
	case SDW_SLAVE_UNATTACHED:
		if (cs35l56->sdw_attached)
			dev_dbg(cs35l56->base.dev, "%s: UNATTACHED\n", __func__);
		cs35l56->sdw_attached = false;
		break;
	default:
		break;
	}

	return 0;
}

static const struct sdw_slave_ops cs35l56_sdw_ops = {
	.read_prop = cs35l56_sdw_read_prop,
	.interrupt_callback = cs35l56_sdw_interrupt,
	.update_status = cs35l56_sdw_update_status,
};

static int __maybe_unused cs35l56_sdw_handle_unattach(struct cs35l56_private *cs35l56)
{
	struct sdw_slave *peripheral = cs35l56->sdw_peripheral;
	int ret;

	dev_dbg(cs35l56->base.dev, "attached:%u unattach_request:%u\n",
		cs35l56->sdw_attached, peripheral->unattach_request);

	/* Cannot access registers until bus is re-initialized. */
	ret = sdw_slave_wait_for_init(peripheral, 5000);
	if (ret)
		return ret;

	/*
	 * Don't call regcache_mark_dirty(), we can't be sure that the
	 * Manager really did issue a Bus Reset.
	 */

	return 0;
}

static int __maybe_unused cs35l56_sdw_runtime_suspend(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	if (!cs35l56->base.init_done)
		return 0;

	return cs35l56_runtime_suspend_common(&cs35l56->base);
}

static int __maybe_unused cs35l56_sdw_runtime_resume(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "Runtime resume\n");

	if (!cs35l56->base.init_done)
		return 0;

	ret = cs35l56_sdw_handle_unattach(cs35l56);
	if (ret < 0)
		return ret;

	ret = cs35l56_runtime_resume_common(&cs35l56->base, true);
	if (ret)
		return ret;

	cs35l56_unmask_soundwire_interrupts(cs35l56->sdw_peripheral);

	return 0;
}

static int __maybe_unused cs35l56_sdw_system_suspend(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	if (!cs35l56->base.init_done)
		return 0;

	cs35l56_disable_sdw_interrupts(cs35l56);

	return cs35l56_system_suspend(dev);
}

static int __maybe_unused cs35l56_sdw_system_resume(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	cs35l56->sdw_irq_no_unmask = false;
	/* runtime_resume re-enables the interrupt */

	return cs35l56_system_resume(dev);
}

static int cs35l56_sdw_probe(struct sdw_slave *peripheral, const struct sdw_device_id *id)
{
	struct device *dev = &peripheral->dev;
	struct cs35l56_private *cs35l56;
	const struct regmap_config *regmap_config;
	int ret;

	cs35l56 = devm_kzalloc(dev, sizeof(*cs35l56), GFP_KERNEL);
	if (!cs35l56)
		return -ENOMEM;

	cs35l56->base.dev = dev;
	cs35l56->sdw_peripheral = peripheral;
	cs35l56->sdw_link_num = peripheral->bus->link_id;
	INIT_WORK(&cs35l56->sdw_irq_work, cs35l56_sdw_irq_work);

	dev_set_drvdata(dev, cs35l56);

	switch ((unsigned int)id->driver_data) {
	case 0x3556:
	case 0x3557:
		regmap_config = &cs35l56_regmap_sdw;
		break;
	case 0x3563:
		regmap_config = &cs35l63_regmap_sdw;
		break;
	default:
		return -ENODEV;
	}

	cs35l56->base.type = ((unsigned int)id->driver_data) & 0xff;

	/* Low-level regmap to transfer read/writes over SoundWire bus */
	cs35l56->sdw_bus_regmap = devm_regmap_init_sdw(peripheral, &cs35l56_sdw_bus_regmap);
	if (IS_ERR(cs35l56->sdw_bus_regmap)) {
		ret = PTR_ERR(cs35l56->sdw_bus_regmap);
		return dev_err_probe(dev, ret, "Failed to allocate bus register map\n");
	}

	/* Wrapper regmap to simulate big-endian ordering */
	cs35l56->base.regmap = devm_regmap_init(dev, &cs35l56_regmap_swab_bus_sdw,
						peripheral, regmap_config);
	if (IS_ERR(cs35l56->base.regmap)) {
		ret = PTR_ERR(cs35l56->base.regmap);
		return dev_err_probe(dev, ret, "Failed to allocate register map\n");
	}

	/* Start in cache-only until device is enumerated */
	regcache_cache_only(cs35l56->base.regmap, true);

	ret = cs35l56_common_probe(cs35l56);
	if (ret != 0)
		return ret;

	return 0;
}

static void cs35l56_sdw_remove(struct sdw_slave *peripheral)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);

	cs35l56_disable_sdw_interrupts(cs35l56);

	cs35l56_remove(cs35l56);
}

static const struct dev_pm_ops cs35l56_sdw_pm = {
	SET_RUNTIME_PM_OPS(cs35l56_sdw_runtime_suspend, cs35l56_sdw_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(cs35l56_sdw_system_suspend, cs35l56_sdw_system_resume)
	LATE_SYSTEM_SLEEP_PM_OPS(cs35l56_system_suspend_late, cs35l56_system_resume_early)
	/* NOIRQ stage not needed, SoundWire doesn't use a hard IRQ */
};

static const struct sdw_device_id cs35l56_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x01FA, 0x3556, 0x3556),
	SDW_SLAVE_ENTRY(0x01FA, 0x3557, 0x3557),
	SDW_SLAVE_ENTRY(0x01FA, 0x3563, 0x3563),
	{},
};
MODULE_DEVICE_TABLE(sdw, cs35l56_sdw_id);

static struct sdw_driver cs35l56_sdw_driver = {
	.driver = {
		.name = "cs35l56",
		.pm = pm_ptr(&cs35l56_sdw_pm),
	},
	.probe = cs35l56_sdw_probe,
	.remove = cs35l56_sdw_remove,
	.ops = &cs35l56_sdw_ops,
	.id_table = cs35l56_sdw_id,
};

module_sdw_driver(cs35l56_sdw_driver);

MODULE_DESCRIPTION("ASoC CS35L56 SoundWire driver");
MODULE_IMPORT_NS("SND_SOC_CS35L56_CORE");
MODULE_IMPORT_NS("SND_SOC_CS35L56_SHARED");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
