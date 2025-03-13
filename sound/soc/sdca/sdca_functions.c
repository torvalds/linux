// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2024 Intel Corporation

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#define dev_fmt(fmt) "%s: " fmt, __func__

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>

static int patch_sdca_function_type(u32 interface_revision, u32 *function_type)
{
	/*
	 * Unfortunately early SDCA specifications used different indices for Functions,
	 * for backwards compatibility we have to reorder the values found
	 */
	if (interface_revision < 0x0801) {
		switch (*function_type) {
		case 1:
			*function_type = SDCA_FUNCTION_TYPE_SMART_AMP;
			break;
		case 2:
			*function_type = SDCA_FUNCTION_TYPE_SMART_MIC;
			break;
		case 3:
			*function_type = SDCA_FUNCTION_TYPE_SPEAKER_MIC;
			break;
		case 4:
			*function_type = SDCA_FUNCTION_TYPE_UAJ;
			break;
		case 5:
			*function_type = SDCA_FUNCTION_TYPE_RJ;
			break;
		case 6:
			*function_type = SDCA_FUNCTION_TYPE_HID;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static const char *get_sdca_function_name(u32 function_type)
{
	switch (function_type) {
	case SDCA_FUNCTION_TYPE_SMART_AMP:
		return SDCA_FUNCTION_TYPE_SMART_AMP_NAME;
	case SDCA_FUNCTION_TYPE_SMART_MIC:
		return SDCA_FUNCTION_TYPE_SMART_MIC_NAME;
	case SDCA_FUNCTION_TYPE_UAJ:
		return SDCA_FUNCTION_TYPE_UAJ_NAME;
	case SDCA_FUNCTION_TYPE_HID:
		return SDCA_FUNCTION_TYPE_HID_NAME;
	case SDCA_FUNCTION_TYPE_SIMPLE_AMP:
		return SDCA_FUNCTION_TYPE_SIMPLE_AMP_NAME;
	case SDCA_FUNCTION_TYPE_SIMPLE_MIC:
		return SDCA_FUNCTION_TYPE_SIMPLE_MIC_NAME;
	case SDCA_FUNCTION_TYPE_SPEAKER_MIC:
		return SDCA_FUNCTION_TYPE_SPEAKER_MIC_NAME;
	case SDCA_FUNCTION_TYPE_RJ:
		return SDCA_FUNCTION_TYPE_RJ_NAME;
	case SDCA_FUNCTION_TYPE_IMP_DEF:
		return SDCA_FUNCTION_TYPE_IMP_DEF_NAME;
	default:
		return NULL;
	}
}

static int find_sdca_function(struct acpi_device *adev, void *data)
{
	struct fwnode_handle *function_node = acpi_fwnode_handle(adev);
	struct sdca_device_data *sdca_data = data;
	struct device *dev = &adev->dev;
	struct fwnode_handle *control5; /* used to identify function type */
	const char *function_name;
	u32 function_type;
	int func_index;
	u64 addr;
	int ret;

	if (sdca_data->num_functions >= SDCA_MAX_FUNCTION_COUNT) {
		dev_err(dev, "maximum number of functions exceeded\n");
		return -EINVAL;
	}

	ret = acpi_get_local_u64_address(adev->handle, &addr);
	if (ret < 0)
		return ret;

	if (!addr || addr > 0x7) {
		dev_err(dev, "invalid addr: 0x%llx\n", addr);
		return -ENODEV;
	}

	/*
	 * Extracting the topology type for an SDCA function is a
	 * convoluted process.
	 * The Function type is only visible as a result of a read
	 * from a control. In theory this would mean reading from the hardware,
	 * but the SDCA/DisCo specs defined the notion of "DC value" - a constant
	 * represented with a DSD subproperty.
	 * Drivers have to query the properties for the control
	 * SDCA_CONTROL_ENTITY_0_FUNCTION_TOPOLOGY (0x05)
	 */
	control5 = fwnode_get_named_child_node(function_node,
					       "mipi-sdca-control-0x5-subproperties");
	if (!control5)
		return -ENODEV;

	ret = fwnode_property_read_u32(control5, "mipi-sdca-control-dc-value",
				       &function_type);

	fwnode_handle_put(control5);

	if (ret < 0) {
		dev_err(dev, "function type only supported as DisCo constant\n");
		return ret;
	}

	ret = patch_sdca_function_type(sdca_data->interface_revision, &function_type);
	if (ret < 0) {
		dev_err(dev, "SDCA version %#x invalid function type %d\n",
			sdca_data->interface_revision, function_type);
		return ret;
	}

	function_name = get_sdca_function_name(function_type);
	if (!function_name) {
		dev_err(dev, "invalid SDCA function type %d\n", function_type);
		return -EINVAL;
	}

	dev_info(dev, "SDCA function %s (type %d) at 0x%llx\n",
		 function_name, function_type, addr);

	/* store results */
	func_index = sdca_data->num_functions;
	sdca_data->sdca_func[func_index].adr = addr;
	sdca_data->sdca_func[func_index].type = function_type;
	sdca_data->sdca_func[func_index].name = function_name;
	sdca_data->num_functions++;

	return 0;
}

void sdca_lookup_functions(struct sdw_slave *slave)
{
	struct device *dev = &slave->dev;
	struct acpi_device *adev = to_acpi_device_node(dev->fwnode);

	if (!adev) {
		dev_info(dev, "No matching ACPI device found, ignoring peripheral\n");
		return;
	}
	acpi_dev_for_each_child(adev, find_sdca_function, &slave->sdca_data);
}
EXPORT_SYMBOL_NS(sdca_lookup_functions, "SND_SOC_SDCA");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SDCA library");
