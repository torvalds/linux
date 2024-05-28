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
#include <linux/swab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "cs35l56.h"

/* Register addresses are offset when sent over SoundWire */
#define CS35L56_SDW_ADDR_OFFSET		0x8000

static int cs35l56_sdw_read_one(struct sdw_slave *peripheral, unsigned int reg, void *buf)
{
	int ret;

	ret = sdw_nread_no_pm(peripheral, reg, 4, (u8 *)buf);
	if (ret != 0) {
		dev_err(&peripheral->dev, "Read failed @%#x:%d\n", reg, ret);
		return ret;
	}

	swab32s((u32 *)buf);

	return 0;
}

static int cs35l56_sdw_read(void *context, const void *reg_buf,
			    const size_t reg_size, void *val_buf,
			    size_t val_size)
{
	struct sdw_slave *peripheral = context;
	u8 *buf8 = val_buf;
	unsigned int reg, bytes;
	int ret;

	reg = le32_to_cpu(*(const __le32 *)reg_buf);
	reg += CS35L56_SDW_ADDR_OFFSET;

	if (val_size == 4)
		return cs35l56_sdw_read_one(peripheral, reg, val_buf);

	while (val_size) {
		bytes = SDW_REG_NO_PAGE - (reg & SDW_REGADDR); /* to end of page */
		if (bytes > val_size)
			bytes = val_size;

		ret = sdw_nread_no_pm(peripheral, reg, bytes, buf8);
		if (ret != 0) {
			dev_err(&peripheral->dev, "Read failed @%#x..%#x:%d\n",
				reg, reg + bytes - 1, ret);
			return ret;
		}

		swab32_array((u32 *)buf8, bytes / 4);
		val_size -= bytes;
		reg += bytes;
		buf8 += bytes;
	}

	return 0;
}

static inline void cs35l56_swab_copy(void *dest, const void *src, size_t nbytes)
{
	u32 *dest32 = dest;
	const u32 *src32 = src;

	for (; nbytes > 0; nbytes -= 4)
		*dest32++ = swab32(*src32++);
}

static int cs35l56_sdw_write_one(struct sdw_slave *peripheral, unsigned int reg, const void *buf)
{
	u32 val_le = swab32(*(u32 *)buf);
	int ret;

	ret = sdw_nwrite_no_pm(peripheral, reg, 4, (u8 *)&val_le);
	if (ret != 0) {
		dev_err(&peripheral->dev, "Write failed @%#x:%d\n", reg, ret);
		return ret;
	}

	return 0;
}

static int cs35l56_sdw_gather_write(void *context,
				    const void *reg_buf, size_t reg_size,
				    const void *val_buf, size_t val_size)
{
	struct sdw_slave *peripheral = context;
	const u8 *src_be = val_buf;
	u32 val_le_buf[64];	/* Define u32 so it is 32-bit aligned */
	unsigned int reg, bytes;
	int ret;

	reg = le32_to_cpu(*(const __le32 *)reg_buf);
	reg += CS35L56_SDW_ADDR_OFFSET;

	if (val_size == 4)
		return cs35l56_sdw_write_one(peripheral, reg, src_be);

	while (val_size) {
		bytes = SDW_REG_NO_PAGE - (reg & SDW_REGADDR); /* to end of page */
		if (bytes > val_size)
			bytes = val_size;
		if (bytes > sizeof(val_le_buf))
			bytes = sizeof(val_le_buf);

		cs35l56_swab_copy(val_le_buf, src_be, bytes);

		ret = sdw_nwrite_no_pm(peripheral, reg, bytes, (u8 *)val_le_buf);
		if (ret != 0) {
			dev_err(&peripheral->dev, "Write failed @%#x..%#x:%d\n",
				reg, reg + bytes - 1, ret);
			return ret;
		}

		val_size -= bytes;
		reg += bytes;
		src_be += bytes;
	}

	return 0;
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
static const struct regmap_bus cs35l56_regmap_bus_sdw = {
	.read = cs35l56_sdw_read,
	.write = cs35l56_sdw_write,
	.gather_write = cs35l56_sdw_gather_write,
	.reg_format_endian_default = REGMAP_ENDIAN_LITTLE,
	.val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static int cs35l56_sdw_set_cal_index(struct cs35l56_private *cs35l56)
{
	int ret;

	/* SoundWire UniqueId is used to index the calibration array */
	ret = sdw_read_no_pm(cs35l56->sdw_peripheral, SDW_SCP_DEVID_0);
	if (ret < 0)
		return ret;

	cs35l56->base.cal_index = ret & 0xf;

	return 0;
}

static void cs35l56_sdw_init(struct sdw_slave *peripheral)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	int ret;

	pm_runtime_get_noresume(cs35l56->base.dev);

	if (cs35l56->base.cal_index < 0) {
		ret = cs35l56_sdw_set_cal_index(cs35l56);
		if (ret < 0)
			goto out;
	}

	ret = cs35l56_init(cs35l56);
	if (ret < 0) {
		regcache_cache_only(cs35l56->base.regmap, true);
		goto out;
	}

	/*
	 * cs35l56_init can return with !init_done if it triggered
	 * a soft reset.
	 */
	if (cs35l56->base.init_done) {
		/* Enable SoundWire interrupts */
		sdw_write_no_pm(peripheral, CS35L56_SDW_GEN_INT_MASK_1,
				CS35L56_SDW_INT_MASK_CODEC_IRQ);
	}

out:
	pm_runtime_mark_last_busy(cs35l56->base.dev);
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
	 * Mask and clear until it has been handled. The read of GEN_INT_STAT_1
	 * is required as per the SoundWire spec for interrupt status bits
	 * to clear. GEN_INT_MASK_1 masks the _inputs_ to GEN_INT_STAT1.
	 * None of the interrupts are time-critical so use the
	 * power-efficient queue.
	 */
	sdw_write_no_pm(peripheral, CS35L56_SDW_GEN_INT_MASK_1, 0);
	sdw_read_no_pm(peripheral, CS35L56_SDW_GEN_INT_STAT_1);
	sdw_write_no_pm(peripheral, CS35L56_SDW_GEN_INT_STAT_1, 0xFF);
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
		sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_MASK_1,
				CS35L56_SDW_INT_MASK_CODEC_IRQ);

	pm_runtime_put_autosuspend(cs35l56->base.dev);
}

static int cs35l56_sdw_read_prop(struct sdw_slave *peripheral)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	struct sdw_slave_prop *prop = &peripheral->prop;
	struct sdw_dpn_prop *ports;

	ports = devm_kcalloc(cs35l56->base.dev, 2, sizeof(*ports), GFP_KERNEL);
	if (!ports)
		return -ENOMEM;

	prop->source_ports = BIT(CS35L56_SDW1_CAPTURE_PORT);
	prop->sink_ports = BIT(CS35L56_SDW1_PLAYBACK_PORT);
	prop->paging_support = true;
	prop->clk_stop_mode1 = false;
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

	return 0;
}

static int cs35l56_sdw_update_status(struct sdw_slave *peripheral,
				     enum sdw_slave_status status)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);

	switch (status) {
	case SDW_SLAVE_ATTACHED:
		dev_dbg(cs35l56->base.dev, "%s: ATTACHED\n", __func__);
		if (cs35l56->sdw_attached)
			break;

		if (!cs35l56->base.init_done || cs35l56->soft_resetting)
			cs35l56_sdw_init(peripheral);

		cs35l56->sdw_attached = true;
		break;
	case SDW_SLAVE_UNATTACHED:
		dev_dbg(cs35l56->base.dev, "%s: UNATTACHED\n", __func__);
		cs35l56->sdw_attached = false;
		break;
	default:
		break;
	}

	return 0;
}

static int cs35l56_a1_kick_divider(struct cs35l56_private *cs35l56,
				   struct sdw_slave *peripheral)
{
	unsigned int curr_scale_reg, next_scale_reg;
	int curr_scale, next_scale, ret;

	if (!cs35l56->base.init_done)
		return 0;

	if (peripheral->bus->params.curr_bank) {
		curr_scale_reg = SDW_SCP_BUSCLOCK_SCALE_B1;
		next_scale_reg = SDW_SCP_BUSCLOCK_SCALE_B0;
	} else {
		curr_scale_reg = SDW_SCP_BUSCLOCK_SCALE_B0;
		next_scale_reg = SDW_SCP_BUSCLOCK_SCALE_B1;
	}

	/*
	 * Current clock scale value must be different to new value.
	 * Modify current to guarantee this. If next still has the dummy
	 * value we wrote when it was current, the core code has not set
	 * a new scale so restore its original good value
	 */
	curr_scale = sdw_read_no_pm(peripheral, curr_scale_reg);
	if (curr_scale < 0) {
		dev_err(cs35l56->base.dev, "Failed to read current clock scale: %d\n", curr_scale);
		return curr_scale;
	}

	next_scale = sdw_read_no_pm(peripheral, next_scale_reg);
	if (next_scale < 0) {
		dev_err(cs35l56->base.dev, "Failed to read next clock scale: %d\n", next_scale);
		return next_scale;
	}

	if (next_scale == CS35L56_SDW_INVALID_BUS_SCALE) {
		next_scale = cs35l56->old_sdw_clock_scale;
		ret = sdw_write_no_pm(peripheral, next_scale_reg, next_scale);
		if (ret < 0) {
			dev_err(cs35l56->base.dev, "Failed to modify current clock scale: %d\n",
				ret);
			return ret;
		}
	}

	cs35l56->old_sdw_clock_scale = curr_scale;
	ret = sdw_write_no_pm(peripheral, curr_scale_reg, CS35L56_SDW_INVALID_BUS_SCALE);
	if (ret < 0) {
		dev_err(cs35l56->base.dev, "Failed to modify current clock scale: %d\n", ret);
		return ret;
	}

	dev_dbg(cs35l56->base.dev, "Next bus scale: %#x\n", next_scale);

	return 0;
}

static int cs35l56_sdw_bus_config(struct sdw_slave *peripheral,
				  struct sdw_bus_params *params)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);
	int sclk;

	sclk = params->curr_dr_freq / 2;
	dev_dbg(cs35l56->base.dev, "%s: sclk=%u c=%u r=%u\n",
		__func__, sclk, params->col, params->row);

	if ((cs35l56->base.type == 0x56) && (cs35l56->base.rev < 0xb0))
		return cs35l56_a1_kick_divider(cs35l56, peripheral);

	return 0;
}

static int __maybe_unused cs35l56_sdw_clk_stop(struct sdw_slave *peripheral,
					       enum sdw_clk_stop_mode mode,
					       enum sdw_clk_stop_type type)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);

	dev_dbg(cs35l56->base.dev, "%s: mode:%d type:%d\n", __func__, mode, type);

	return 0;
}

static const struct sdw_slave_ops cs35l56_sdw_ops = {
	.read_prop = cs35l56_sdw_read_prop,
	.interrupt_callback = cs35l56_sdw_interrupt,
	.update_status = cs35l56_sdw_update_status,
	.bus_config = cs35l56_sdw_bus_config,
#ifdef DEBUG
	.clk_stop = cs35l56_sdw_clk_stop,
#endif
};

static int __maybe_unused cs35l56_sdw_handle_unattach(struct cs35l56_private *cs35l56)
{
	struct sdw_slave *peripheral = cs35l56->sdw_peripheral;

	if (peripheral->unattach_request) {
		/* Cannot access registers until bus is re-initialized. */
		dev_dbg(cs35l56->base.dev, "Wait for initialization_complete\n");
		if (!wait_for_completion_timeout(&peripheral->initialization_complete,
						 msecs_to_jiffies(5000))) {
			dev_err(cs35l56->base.dev, "initialization_complete timed out\n");
			return -ETIMEDOUT;
		}

		peripheral->unattach_request = 0;

		/*
		 * Don't call regcache_mark_dirty(), we can't be sure that the
		 * Manager really did issue a Bus Reset.
		 */
	}

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

	/* Re-enable SoundWire interrupts */
	sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_MASK_1,
			CS35L56_SDW_INT_MASK_CODEC_IRQ);

	return 0;
}

static int __maybe_unused cs35l56_sdw_system_suspend(struct device *dev)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(dev);

	if (!cs35l56->base.init_done)
		return 0;

	/*
	 * Disable SoundWire interrupts.
	 * Flush - don't cancel because that could leave an unbalanced pm_runtime_get.
	 */
	cs35l56->sdw_irq_no_unmask = true;
	flush_work(&cs35l56->sdw_irq_work);

	/* Mask interrupts and flush in case sdw_irq_work was queued again */
	sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_MASK_1, 0);
	sdw_read_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_STAT_1);
	sdw_write_no_pm(cs35l56->sdw_peripheral, CS35L56_SDW_GEN_INT_STAT_1, 0xFF);
	flush_work(&cs35l56->sdw_irq_work);

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
	int ret;

	cs35l56 = devm_kzalloc(dev, sizeof(*cs35l56), GFP_KERNEL);
	if (!cs35l56)
		return -ENOMEM;

	cs35l56->base.dev = dev;
	cs35l56->sdw_peripheral = peripheral;
	INIT_WORK(&cs35l56->sdw_irq_work, cs35l56_sdw_irq_work);

	dev_set_drvdata(dev, cs35l56);

	cs35l56->base.regmap = devm_regmap_init(dev, &cs35l56_regmap_bus_sdw,
					   peripheral, &cs35l56_regmap_sdw);
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

static int cs35l56_sdw_remove(struct sdw_slave *peripheral)
{
	struct cs35l56_private *cs35l56 = dev_get_drvdata(&peripheral->dev);

	/* Disable SoundWire interrupts */
	cs35l56->sdw_irq_no_unmask = true;
	cancel_work_sync(&cs35l56->sdw_irq_work);
	sdw_write_no_pm(peripheral, CS35L56_SDW_GEN_INT_MASK_1, 0);
	sdw_read_no_pm(peripheral, CS35L56_SDW_GEN_INT_STAT_1);
	sdw_write_no_pm(peripheral, CS35L56_SDW_GEN_INT_STAT_1, 0xFF);

	cs35l56_remove(cs35l56);

	return 0;
}

static const struct dev_pm_ops cs35l56_sdw_pm = {
	SET_RUNTIME_PM_OPS(cs35l56_sdw_runtime_suspend, cs35l56_sdw_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(cs35l56_sdw_system_suspend, cs35l56_sdw_system_resume)
	LATE_SYSTEM_SLEEP_PM_OPS(cs35l56_system_suspend_late, cs35l56_system_resume_early)
	/* NOIRQ stage not needed, SoundWire doesn't use a hard IRQ */
};

static const struct sdw_device_id cs35l56_sdw_id[] = {
	SDW_SLAVE_ENTRY(0x01FA, 0x3556, 0),
	SDW_SLAVE_ENTRY(0x01FA, 0x3557, 0),
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
MODULE_IMPORT_NS(SND_SOC_CS35L56_CORE);
MODULE_IMPORT_NS(SND_SOC_CS35L56_SHARED);
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
