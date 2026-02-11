// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_ump.h>
#include <sound/soc-component.h>
#include <linux/soundwire/sdw_registers.h>

/**
 * sdca_ump_get_owner_host - check a UMP buffer is owned by the host
 * @dev: Pointer to the struct device used for error messages.
 * @function_regmap: Pointer to the regmap for the SDCA Function.
 * @function: Pointer to the Function information.
 * @entity: Pointer to the SDCA Entity.
 * @control: Pointer to the SDCA Control for the UMP Owner.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_ump_get_owner_host(struct device *dev,
			    struct regmap *function_regmap,
			    struct sdca_function_data *function,
			    struct sdca_entity *entity,
			    struct sdca_control *control)
{
	unsigned int reg, owner;
	int ret;

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, control->sel, 0);
	ret = regmap_read(function_regmap, reg, &owner);
	if (ret < 0) {
		dev_err(dev, "%s: failed to read UMP owner: %d\n",
			entity->label, ret);
		return ret;
	}

	if (owner != SDCA_UMP_OWNER_HOST) {
		dev_err(dev, "%s: host is not the UMP owner\n", entity->label);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_ump_get_owner_host, "SND_SOC_SDCA");

/**
 * sdca_ump_set_owner_device - set a UMP buffer's ownership back to the device
 * @dev: Pointer to the struct device used for error messages.
 * @function_regmap: Pointer to the regmap for the SDCA Function.
 * @function: Pointer to the Function information.
 * @entity: Pointer to the SDCA Entity.
 * @control: Pointer to the SDCA Control for the UMP Owner.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_ump_set_owner_device(struct device *dev,
			      struct regmap *function_regmap,
			      struct sdca_function_data *function,
			      struct sdca_entity *entity,
			      struct sdca_control *control)
{
	unsigned int reg;
	int ret;

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, control->sel, 0);
	ret = regmap_write(function_regmap, reg, SDCA_UMP_OWNER_DEVICE);
	if (ret < 0)
		dev_err(dev, "%s: failed to write UMP owner: %d\n",
			entity->label, ret);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(sdca_ump_set_owner_device, "SND_SOC_SDCA");

/**
 * sdca_ump_read_message - read a UMP message from the device
 * @dev: Pointer to the struct device used for error messages.
 * @device_regmap: Pointer to the Device register map.
 * @function_regmap: Pointer to the regmap for the SDCA Function.
 * @function: Pointer to the Function information.
 * @entity: Pointer to the SDCA Entity.
 * @offset_sel: Control Selector for the UMP Offset Control.
 * @length_sel: Control Selector for the UMP Length Control.
 * @msg: Pointer that will be populated with an dynamically buffer
 * containing the UMP message. Note this needs to be freed by the
 * caller.
 *
 * The caller should first call sdca_ump_get_owner_host() to ensure the host
 * currently owns the UMP buffer, and then this function can be used to
 * retrieve a message. It is the callers responsibility to free the
 * message once it is finished with it. Finally sdca_ump_set_owner_device()
 * should be called to return the buffer to the device.
 *
 * Return: Returns the message length on success, and a negative error
 * code on failure.
 */
int sdca_ump_read_message(struct device *dev,
			  struct regmap *device_regmap,
			  struct regmap *function_regmap,
			  struct sdca_function_data *function,
			  struct sdca_entity *entity,
			  unsigned int offset_sel, unsigned int length_sel,
			  void **msg)
{
	struct sdca_control_range *range;
	unsigned int msg_offset, msg_len;
	unsigned int buf_addr, buf_len;
	unsigned int reg;
	int ret;

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, offset_sel, 0);
	ret = regmap_read(function_regmap, reg, &msg_offset);
	if (ret < 0) {
		dev_err(dev, "%s: failed to read UMP offset: %d\n",
			entity->label, ret);
		return ret;
	}

	range = sdca_selector_find_range(dev, entity, offset_sel,
					 SDCA_MESSAGEOFFSET_NCOLS, 1);
	if (!range)
		return -ENOENT;

	buf_addr = sdca_range(range, SDCA_MESSAGEOFFSET_BUFFER_START_ADDRESS, 0);
	buf_len = sdca_range(range, SDCA_MESSAGEOFFSET_BUFFER_LENGTH, 0);

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, length_sel, 0);
	ret = regmap_read(function_regmap, reg, &msg_len);
	if (ret < 0) {
		dev_err(dev, "%s: failed to read UMP length: %d\n",
			entity->label, ret);
		return ret;
	}

	if (msg_len > buf_len - msg_offset) {
		dev_err(dev, "%s: message too big for UMP buffer: %d\n",
			entity->label, msg_len);
		return -EINVAL;
	}

	*msg = kmalloc(msg_len, GFP_KERNEL);
	if (!*msg)
		return -ENOMEM;

	ret = regmap_raw_read(device_regmap, buf_addr + msg_offset, *msg, msg_len);
	if (ret < 0) {
		dev_err(dev, "%s: failed to read UMP message: %d\n",
			entity->label, ret);
		return ret;
	}

	return msg_len;
}
EXPORT_SYMBOL_NS_GPL(sdca_ump_read_message, "SND_SOC_SDCA");

/**
 * sdca_ump_write_message - write a UMP message to the device
 * @dev: Pointer to the struct device used for error messages.
 * @device_regmap: Pointer to the Device register map.
 * @function_regmap: Pointer to the regmap for the SDCA Function.
 * @function: Pointer to the Function information.
 * @entity: Pointer to the SDCA Entity.
 * @offset_sel: Control Selector for the UMP Offset Control.
 * @msg_offset: Offset within the UMP buffer at which the message should
 * be written.
 * @length_sel: Control Selector for the UMP Length Control.
 * @msg: Pointer to the data that should be written to the UMP buffer.
 * @msg_len: Length of the message data in bytes.
 *
 * The caller should first call sdca_ump_get_owner_host() to ensure the host
 * currently owns the UMP buffer, and then this function can be used to
 * write a message. Finally sdca_ump_set_owner_device() should be called to
 * return the buffer to the device, allowing the device to access the
 * message.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_ump_write_message(struct device *dev,
			   struct regmap *device_regmap,
			   struct regmap *function_regmap,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   unsigned int offset_sel, unsigned int msg_offset,
			   unsigned int length_sel,
			   void *msg, int msg_len)
{
	struct sdca_control_range *range;
	unsigned int buf_addr, buf_len, ump_mode;
	unsigned int reg;
	int ret;

	range = sdca_selector_find_range(dev, entity, offset_sel,
					 SDCA_MESSAGEOFFSET_NCOLS, 1);
	if (!range)
		return -ENOENT;

	buf_addr = sdca_range(range, SDCA_MESSAGEOFFSET_BUFFER_START_ADDRESS, 0);
	buf_len = sdca_range(range, SDCA_MESSAGEOFFSET_BUFFER_LENGTH, 0);
	ump_mode = sdca_range(range, SDCA_MESSAGEOFFSET_UMP_MODE, 0);

	if (msg_len > buf_len - msg_offset) {
		dev_err(dev, "%s: message too big for UMP buffer: %d\n",
			entity->label, msg_len);
		return -EINVAL;
	}

	if (ump_mode != SDCA_UMP_MODE_DIRECT) {
		dev_err(dev, "%s: only direct mode currently supported\n",
			entity->label);
		return -EINVAL;
	}

	ret = regmap_raw_write(device_regmap, buf_addr + msg_offset, msg, msg_len);
	if (ret) {
		dev_err(dev, "%s: failed to write UMP message: %d\n",
			entity->label, ret);
		return ret;
	}

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, offset_sel, 0);
	ret = regmap_write(function_regmap, reg, msg_offset);
	if (ret < 0) {
		dev_err(dev, "%s: failed to write UMP offset: %d\n",
			entity->label, ret);
		return ret;
	}

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, length_sel, 0);
	ret = regmap_write(function_regmap, reg, msg_len);
	if (ret < 0) {
		dev_err(dev, "%s: failed to write UMP length: %d\n",
			entity->label, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_ump_write_message, "SND_SOC_SDCA");

void sdca_ump_cancel_timeout(struct delayed_work *work)
{
	cancel_delayed_work_sync(work);
}
EXPORT_SYMBOL_NS_GPL(sdca_ump_cancel_timeout, "SND_SOC_SDCA");

void sdca_ump_schedule_timeout(struct delayed_work *work, unsigned int timeout_us)
{
	if (!timeout_us)
		return;

	queue_delayed_work(system_dfl_wq, work, usecs_to_jiffies(timeout_us));
}
EXPORT_SYMBOL_NS_GPL(sdca_ump_schedule_timeout, "SND_SOC_SDCA");
