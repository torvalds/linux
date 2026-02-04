// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sprintf.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/sdca.h>
#include <sound/sdca_fdl.h>
#include <sound/sdca_function.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_ump.h>

/**
 * sdca_reset_function - send an SDCA function reset
 * @dev: Device pointer for error messages.
 * @function: Pointer to the SDCA Function.
 * @regmap: Pointer to the SDCA Function regmap.
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_reset_function(struct device *dev, struct sdca_function_data *function,
			struct regmap *regmap)
{
	unsigned int reg = SDW_SDCA_CTL(function->desc->adr,
					SDCA_ENTITY_TYPE_ENTITY_0,
					SDCA_CTL_ENTITY_0_FUNCTION_ACTION, 0);
	unsigned int val, poll_us;
	int ret;

	ret = regmap_write(regmap, reg, SDCA_CTL_ENTITY_0_RESET_FUNCTION_NOW);
	if (ret) // Allowed for function reset to not be implemented
		return 0;

	if (!function->reset_max_delay) {
		dev_err(dev, "No reset delay specified in DisCo\n");
		return -EINVAL;
	}

	/*
	 * Poll up to 16 times but no more than once per ms, these are just
	 * arbitrarily selected values, so may be fine tuned in future.
	 */
	poll_us = umin(function->reset_max_delay >> 4, 1000);

	ret = regmap_read_poll_timeout(regmap, reg, val, !val, poll_us,
				       function->reset_max_delay);
	if (ret) {
		dev_err(dev, "Failed waiting for function reset: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_reset_function, "SND_SOC_SDCA");

/**
 * sdca_fdl_sync - wait for a function to finish FDL
 * @dev: Device pointer for error messages.
 * @function: Pointer to the SDCA Function.
 * @info: Pointer to the SDCA interrupt info for this device.
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_fdl_sync(struct device *dev, struct sdca_function_data *function,
		  struct sdca_interrupt_info *info)
{
	static const int fdl_retries = 6;
	unsigned long begin_timeout = msecs_to_jiffies(100);
	unsigned long done_timeout = msecs_to_jiffies(4000);
	int nfdl;
	int i, j;

	for (i = 0; i < fdl_retries; i++) {
		nfdl = 0;

		for (j = 0; j < SDCA_MAX_INTERRUPTS; j++) {
			struct sdca_interrupt *interrupt = &info->irqs[j];
			struct fdl_state *fdl_state;
			unsigned long time;

			if (interrupt->function != function ||
			    !interrupt->entity || !interrupt->control ||
			    interrupt->entity->type != SDCA_ENTITY_TYPE_XU ||
			    interrupt->control->sel != SDCA_CTL_XU_FDL_CURRENTOWNER)
				continue;

			fdl_state = interrupt->priv;
			nfdl++;

			/*
			 * Looking for timeout without any new FDL requests
			 * to imply the device has completed initial
			 * firmware setup. Alas the specification doesn't
			 * have any mechanism to detect this.
			 */
			time = wait_for_completion_timeout(&fdl_state->begin,
							   begin_timeout);
			if (!time) {
				dev_dbg(dev, "no new FDL starts\n");
				nfdl--;
				continue;
			}

			time = wait_for_completion_timeout(&fdl_state->done,
							   done_timeout);
			if (!time) {
				dev_err(dev, "timed out waiting for FDL to complete\n");
				goto error;
			}
		}

		if (!nfdl)
			return 0;
	}

	dev_err(dev, "too many FDL requests\n");

error:
	for (j = 0; j < SDCA_MAX_INTERRUPTS; j++) {
		struct sdca_interrupt *interrupt = &info->irqs[j];
		struct fdl_state *fdl_state;

		if (interrupt->function != function ||
		    !interrupt->entity || !interrupt->control ||
		    interrupt->entity->type != SDCA_ENTITY_TYPE_XU ||
		    interrupt->control->sel != SDCA_CTL_XU_FDL_CURRENTOWNER)
			continue;

		disable_irq(interrupt->irq);

		fdl_state = interrupt->priv;

		sdca_ump_cancel_timeout(&fdl_state->timeout);
	}

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_NS_GPL(sdca_fdl_sync, "SND_SOC_SDCA");

static char *fdl_get_sku_filename(struct device *dev,
				  struct sdca_fdl_file *fdl_file)
{
	struct device *parent = dev;
	const char *product_vendor;
	const char *product_sku;

	/*
	 * Try to find pci_dev manually because the card may not be ready to be
	 * used for snd_soc_card_get_pci_ssid yet
	 */
	while (parent) {
		if (dev_is_pci(parent)) {
			struct pci_dev *pci_dev = to_pci_dev(parent);

			return kasprintf(GFP_KERNEL, "sdca/%x/%x/%x/%x.bin",
					 fdl_file->vendor_id,
					 pci_dev->subsystem_vendor,
					 pci_dev->subsystem_device,
					 fdl_file->file_id);
		} else {
			parent = parent->parent;
		}
	}

	product_vendor = dmi_get_system_info(DMI_SYS_VENDOR);
	if (!product_vendor || !strcmp(product_vendor, "Default string"))
		product_vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
	if (!product_vendor || !strcmp(product_vendor, "Default string"))
		product_vendor = dmi_get_system_info(DMI_CHASSIS_VENDOR);
	if (!product_vendor)
		product_vendor = "unknown";

	product_sku = dmi_get_system_info(DMI_PRODUCT_SKU);
	if (!product_sku || !strcmp(product_sku, "Default string"))
		product_sku = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (!product_sku)
		product_sku = "unknown";

	return kasprintf(GFP_KERNEL, "sdca/%x/%s/%s/%x.bin", fdl_file->vendor_id,
			 product_vendor, product_sku, fdl_file->file_id);
}

static int fdl_load_file(struct sdca_interrupt *interrupt,
			 struct sdca_fdl_set *set, int file_index)
{
	struct device *dev = interrupt->dev;
	struct sdca_fdl_data *fdl_data = &interrupt->function->fdl_data;
	const struct firmware *firmware = NULL;
	struct acpi_sw_file *swf = NULL, *tmp;
	struct sdca_fdl_file *fdl_file;
	char *disk_filename;
	int ret;
	int i;

	if (!set) {
		dev_err(dev, "request to load SWF with no set\n");
		return -EINVAL;
	}

	fdl_file = &set->files[file_index];

	if (fdl_data->swft) {
		tmp = fdl_data->swft->files;
		for (i = 0; i < fdl_data->swft->header.length; i += tmp->file_length,
		     tmp = ACPI_ADD_PTR(struct acpi_sw_file, tmp, tmp->file_length)) {
			if (tmp->vendor_id == fdl_file->vendor_id &&
			    tmp->file_id == fdl_file->file_id) {
				dev_dbg(dev, "located SWF in ACPI: %x-%x-%x\n",
					tmp->vendor_id, tmp->file_id,
					tmp->file_version);
				swf = tmp;
				break;
			}
		}
	}

	disk_filename = fdl_get_sku_filename(dev, fdl_file);
	if (!disk_filename)
		return -ENOMEM;

	dev_dbg(dev, "FDL disk filename: %s\n", disk_filename);

	ret = firmware_request_nowarn(&firmware, disk_filename, dev);
	kfree(disk_filename);
	if (ret) {
		disk_filename = kasprintf(GFP_KERNEL, "sdca/%x/%x.bin",
					  fdl_file->vendor_id, fdl_file->file_id);
		if (!disk_filename)
			return -ENOMEM;

		dev_dbg(dev, "FDL disk filename: %s\n", disk_filename);

		ret = firmware_request_nowarn(&firmware, disk_filename, dev);
		kfree(disk_filename);
	}

	if (!ret) {
		tmp = (struct acpi_sw_file *)&firmware->data[0];

		if (firmware->size < sizeof(*tmp) ||
		    tmp->file_length != firmware->size) {
			dev_err(dev, "bad disk SWF size\n");
		} else if (!swf || swf->file_version <= tmp->file_version) {
			dev_dbg(dev, "using SWF from disk\n");
			swf = tmp;
		}
	}

	if (!swf) {
		dev_err(dev, "failed to locate SWF\n");
		return -ENOENT;
	}

	dev_info(dev, "loading SWF: %x-%x-%x\n",
		 swf->vendor_id, swf->file_id, swf->file_version);

	ret = sdca_ump_write_message(dev, interrupt->device_regmap,
				     interrupt->function_regmap,
				     interrupt->function, interrupt->entity,
				     SDCA_CTL_XU_FDL_MESSAGEOFFSET, fdl_file->fdl_offset,
				     SDCA_CTL_XU_FDL_MESSAGELENGTH, swf->data,
				     swf->file_length - offsetof(struct acpi_sw_file, data));
	release_firmware(firmware);
	return ret;
}

static struct sdca_fdl_set *fdl_get_set(struct sdca_interrupt *interrupt)
{
	struct device *dev = interrupt->dev;
	struct sdca_fdl_data *fdl_data = &interrupt->function->fdl_data;
	struct sdca_entity *xu = interrupt->entity;
	struct sdca_control_range *range;
	unsigned int val;
	int i, ret;

	ret = regmap_read(interrupt->function_regmap,
			  SDW_SDCA_CTL(interrupt->function->desc->adr, xu->id,
				       SDCA_CTL_XU_FDL_SET_INDEX, 0),
			  &val);
	if (ret < 0) {
		dev_err(dev, "failed to read FDL set index: %d\n", ret);
		return NULL;
	}

	range = sdca_selector_find_range(dev, xu, SDCA_CTL_XU_FDL_SET_INDEX,
					 SDCA_FDL_SET_INDEX_NCOLS, 0);

	val = sdca_range_search(range, SDCA_FDL_SET_INDEX_SET_NUMBER,
				val, SDCA_FDL_SET_INDEX_FILE_SET_ID);

	for (i = 0; i < fdl_data->num_sets; i++) {
		if (fdl_data->sets[i].id == val)
			return &fdl_data->sets[i];
	}

	dev_err(dev, "invalid fileset id: %d\n", val);
	return NULL;
}

static void fdl_end(struct sdca_interrupt *interrupt)
{
	struct fdl_state *fdl_state = interrupt->priv;

	if (!fdl_state->set)
		return;

	fdl_state->set = NULL;

	pm_runtime_put(interrupt->dev);
	complete(&fdl_state->done);

	dev_dbg(interrupt->dev, "completed FDL process\n");
}

static void sdca_fdl_timeout_work(struct work_struct *work)
{
	struct fdl_state *fdl_state = container_of(work, struct fdl_state,
						   timeout.work);
	struct sdca_interrupt *interrupt = fdl_state->interrupt;
	struct device *dev = interrupt->dev;

	dev_err(dev, "FDL transaction timed out\n");

	guard(mutex)(&fdl_state->lock);

	fdl_end(interrupt);
	sdca_reset_function(dev, interrupt->function, interrupt->function_regmap);
}

static int fdl_status_process(struct sdca_interrupt *interrupt, unsigned int status)
{
	struct fdl_state *fdl_state = interrupt->priv;
	int ret;

	switch (status) {
	case SDCA_CTL_XU_FDLD_NEEDS_SET:
		dev_dbg(interrupt->dev, "starting FDL process...\n");

		pm_runtime_get(interrupt->dev);
		complete(&fdl_state->begin);

		fdl_state->file_index = 0;
		fdl_state->set = fdl_get_set(interrupt);
		fallthrough;
	case SDCA_CTL_XU_FDLD_MORE_FILES_OK:
		ret = fdl_load_file(interrupt, fdl_state->set, fdl_state->file_index);
		if (ret) {
			fdl_end(interrupt);
			return SDCA_CTL_XU_FDLH_REQ_ABORT;
		}

		return SDCA_CTL_XU_FDLH_FILE_AVAILABLE;
	case SDCA_CTL_XU_FDLD_FILE_OK:
		if (!fdl_state->set) {
			fdl_end(interrupt);
			return SDCA_CTL_XU_FDLH_REQ_ABORT;
		}

		fdl_state->file_index++;

		if (fdl_state->file_index < fdl_state->set->num_files)
			return SDCA_CTL_XU_FDLH_MORE_FILES;
		fallthrough;
	case SDCA_CTL_XU_FDLD_COMPLETE:
		fdl_end(interrupt);
		return SDCA_CTL_XU_FDLH_COMPLETE;
	default:
		fdl_end(interrupt);

		if (status & SDCA_CTL_XU_FDLD_REQ_RESET)
			return SDCA_CTL_XU_FDLH_RESET_ACK;
		else if (status & SDCA_CTL_XU_FDLD_REQ_ABORT)
			return SDCA_CTL_XU_FDLH_COMPLETE;

		dev_err(interrupt->dev, "invalid FDL status: %x\n", status);
		return -EINVAL;
	}
}

/**
 * sdca_fdl_process - Process the FDL state machine
 * @interrupt: SDCA interrupt structure
 *
 * Based on section 13.2.5 Flow Diagram for File Download, Host side.
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_fdl_process(struct sdca_interrupt *interrupt)
{
	struct device *dev = interrupt->dev;
	struct sdca_entity_xu *xu = &interrupt->entity->xu;
	struct fdl_state *fdl_state = interrupt->priv;
	unsigned int reg, status;
	int response, ret;

	ret = sdca_ump_get_owner_host(dev, interrupt->function_regmap,
				      interrupt->function, interrupt->entity,
				      interrupt->control);
	if (ret)
		goto reset_function;

	sdca_ump_cancel_timeout(&fdl_state->timeout);

	scoped_guard(mutex, &fdl_state->lock) {
		reg = SDW_SDCA_CTL(interrupt->function->desc->adr,
				   interrupt->entity->id, SDCA_CTL_XU_FDL_STATUS, 0);
		ret = regmap_read(interrupt->function_regmap, reg, &status);
		if (ret < 0) {
			dev_err(dev, "failed to read FDL status: %d\n", ret);
			return ret;
		}

		dev_dbg(dev, "FDL status: %#x\n", status);

		ret = fdl_status_process(interrupt, status);
		if (ret < 0)
			goto reset_function;

		response = ret;

		dev_dbg(dev, "FDL response: %#x\n", response);

		ret = regmap_write(interrupt->function_regmap, reg,
				   response | (status & ~SDCA_CTL_XU_FDLH_MASK));
		if (ret < 0) {
			dev_err(dev, "failed to set FDL status signal: %d\n", ret);
			return ret;
		}

		ret = sdca_ump_set_owner_device(dev, interrupt->function_regmap,
						interrupt->function,
						interrupt->entity,
						interrupt->control);
		if (ret)
			return ret;

		switch (response) {
		case SDCA_CTL_XU_FDLH_RESET_ACK:
			dev_dbg(dev, "FDL request reset\n");

			switch (xu->reset_mechanism) {
			default:
				dev_warn(dev, "Requested reset mechanism not implemented\n");
				fallthrough;
			case SDCA_XU_RESET_FUNCTION:
				goto reset_function;
			}
		case SDCA_CTL_XU_FDLH_COMPLETE:
			if (status & SDCA_CTL_XU_FDLD_REQ_ABORT ||
			    status == SDCA_CTL_XU_FDLD_COMPLETE)
				return 0;
			fallthrough;
		default:
			sdca_ump_schedule_timeout(&fdl_state->timeout, xu->max_delay);
			return 0;
		}
	}

reset_function:
	sdca_reset_function(dev, interrupt->function, interrupt->function_regmap);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(sdca_fdl_process, "SND_SOC_SDCA");

/**
 * sdca_fdl_alloc_state - allocate state for an FDL interrupt
 * @interrupt: SDCA interrupt structure.
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_fdl_alloc_state(struct sdca_interrupt *interrupt)
{
	struct device *dev = interrupt->dev;
	struct fdl_state *fdl_state;

	fdl_state = devm_kzalloc(dev, sizeof(*fdl_state), GFP_KERNEL);
	if (!fdl_state)
		return -ENOMEM;

	INIT_DELAYED_WORK(&fdl_state->timeout, sdca_fdl_timeout_work);
	init_completion(&fdl_state->begin);
	init_completion(&fdl_state->done);
	mutex_init(&fdl_state->lock);
	fdl_state->interrupt = interrupt;

	interrupt->priv = fdl_state;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_fdl_alloc_state, "SND_SOC_SDCA");
