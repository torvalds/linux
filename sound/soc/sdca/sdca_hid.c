// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/acpi.h>
#include <linux/byteorder/generic.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/hid.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_hid.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_ump.h>

static int sdwhid_parse(struct hid_device *hid)
{
	struct sdca_entity *entity = hid->driver_data;
	unsigned int rsize;
	int ret;

	rsize = le16_to_cpu(entity->hide.hid_desc.rpt_desc.wDescriptorLength);

	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dev_err(&hid->dev, "invalid size of report descriptor (%u)\n", rsize);
		return -EINVAL;
	}

	ret = hid_parse_report(hid, entity->hide.hid_report_desc, rsize);

	if (!ret)
		return 0;

	dev_err(&hid->dev, "parsing report descriptor failed\n");
	return ret;
}

static int sdwhid_start(struct hid_device *hid)
{
	return 0;
}

static void sdwhid_stop(struct hid_device *hid)
{
}

static int sdwhid_raw_request(struct hid_device *hid, unsigned char reportnum,
			      __u8 *buf, size_t len, unsigned char rtype, int reqtype)
{
	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		/* not implemented yet */
		return 0;
	case HID_REQ_SET_REPORT:
		/* not implemented yet */
		return 0;
	default:
		return -EIO;
	}
}

static int sdwhid_open(struct hid_device *hid)
{
	return 0;
}

static void sdwhid_close(struct hid_device *hid)
{
}

static const struct hid_ll_driver sdw_hid_driver = {
	.parse = sdwhid_parse,
	.start = sdwhid_start,
	.stop = sdwhid_stop,
	.open = sdwhid_open,
	.close = sdwhid_close,
	.raw_request = sdwhid_raw_request,
};

int sdca_add_hid_device(struct device *dev, struct sdw_slave *sdw,
			struct sdca_entity *entity)
{
	struct sdw_bus *bus = sdw->bus;
	struct hid_device *hid;
	int ret;

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return PTR_ERR(hid);

	hid->ll_driver = &sdw_hid_driver;

	hid->dev.parent = dev;
	hid->bus = BUS_SDW;
	hid->version = le16_to_cpu(entity->hide.hid_desc.bcdHID);

	snprintf(hid->name, sizeof(hid->name),
		 "HID sdw:%01x:%01x:%04x:%04x:%02x",
		 bus->controller_id, bus->link_id, sdw->id.mfg_id,
		 sdw->id.part_id, sdw->id.class_id);

	snprintf(hid->phys, sizeof(hid->phys), "%s", dev->bus->name);

	hid->driver_data = entity;

	ret = hid_add_device(hid);
	if (ret && ret != -ENODEV) {
		dev_err(dev, "can't add hid device: %d\n", ret);
		hid_destroy_device(hid);
		return ret;
	}

	entity->hide.hid = hid;

	return 0;
}
EXPORT_SYMBOL_NS(sdca_add_hid_device, "SND_SOC_SDCA");

/**
 * sdca_hid_process_report - read a HID event from the device and report
 * @interrupt: Pointer to the SDCA interrupt information structure.
 *
 * Return: Zero on success, and a negative error code on failure.
 */
int sdca_hid_process_report(struct sdca_interrupt *interrupt)
{
	struct device *dev = interrupt->dev;
	struct hid_device *hid = interrupt->entity->hide.hid;
	void *val __free(kfree) = NULL;
	int len, ret;

	ret = sdca_ump_get_owner_host(dev, interrupt->function_regmap,
				      interrupt->function, interrupt->entity,
				      interrupt->control);
	if (ret)
		return ret;

	len = sdca_ump_read_message(dev, interrupt->device_regmap,
				    interrupt->function_regmap,
				    interrupt->function, interrupt->entity,
				    SDCA_CTL_HIDE_HIDTX_MESSAGEOFFSET,
				    SDCA_CTL_HIDE_HIDTX_MESSAGELENGTH, &val);
	if (len < 0)
		return len;

	ret = sdca_ump_set_owner_device(dev, interrupt->function_regmap,
					interrupt->function, interrupt->entity,
					interrupt->control);
	if (ret)
		return ret;

	ret = hid_input_report(hid, HID_INPUT_REPORT, val, len, true);
	if (ret < 0) {
		dev_err(dev, "failed to report hid event: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_hid_process_report, "SND_SOC_SDCA");
