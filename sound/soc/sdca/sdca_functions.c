// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2024 Intel Corporation

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#define dev_fmt(fmt) "%s: " fmt, __func__

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>

/*
 * Should be long enough to encompass all the MIPI DisCo properties.
 */
#define SDCA_PROPERTY_LENGTH 64

static int patch_sdca_function_type(u32 interface_revision, u32 *function_type)
{
	/*
	 * Unfortunately early SDCA specifications used different indices for Functions,
	 * for backwards compatibility we have to reorder the values found.
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
	int function_index;
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
	function_index = sdca_data->num_functions;
	sdca_data->function[function_index].adr = addr;
	sdca_data->function[function_index].type = function_type;
	sdca_data->function[function_index].name = function_name;
	sdca_data->function[function_index].node = function_node;
	sdca_data->num_functions++;

	return 0;
}

/**
 * sdca_lookup_functions - Parse sdca_device_desc for each Function
 * @slave: SoundWire slave device to be processed.
 *
 * Iterate through the available SDCA Functions and fill in a short
 * descriptor (struct sdca_function_desc) for each function, this
 * information is stored along with the SoundWire slave device and
 * used for adding drivers and quirks before the devices have fully
 * probed.
 */
void sdca_lookup_functions(struct sdw_slave *slave)
{
	struct device *dev = &slave->dev;
	struct acpi_device *adev = to_acpi_device_node(dev->fwnode);

	if (!adev) {
		dev_info(dev, "no matching ACPI device found, ignoring peripheral\n");
		return;
	}

	acpi_dev_for_each_child(adev, find_sdca_function, &slave->sdca_data);
}
EXPORT_SYMBOL_NS(sdca_lookup_functions, "SND_SOC_SDCA");

static int find_sdca_entity(struct device *dev,
			    struct fwnode_handle *function_node,
			    struct fwnode_handle *entity_node,
			    struct sdca_entity *entity)
{
	u32 tmp;
	int ret;

	ret = fwnode_property_read_string(entity_node, "mipi-sdca-entity-label",
					  &entity->label);
	if (ret) {
		dev_err(dev, "%pfwP: entity %#x: label missing: %d\n",
			function_node, entity->id, ret);
		return ret;
	}

	ret = fwnode_property_read_u32(entity_node, "mipi-sdca-entity-type", &tmp);
	if (ret) {
		dev_err(dev, "%s: type missing: %d\n", entity->label, ret);
		return ret;
	}

	entity->type = tmp;

	dev_info(dev, "%s: entity %#x type %#x\n",
		 entity->label, entity->id, entity->type);

	return 0;
}

static int find_sdca_entities(struct device *dev,
			      struct fwnode_handle *function_node,
			      struct sdca_function_data *function)
{
	struct sdca_entity *entities;
	u32 *entity_list;
	int num_entities;
	int i, ret;

	num_entities = fwnode_property_count_u32(function_node,
						 "mipi-sdca-entity-id-list");
	if (num_entities <= 0) {
		dev_err(dev, "%pfwP: entity id list missing: %d\n",
			function_node, num_entities);
		return -EINVAL;
	} else if (num_entities > SDCA_MAX_ENTITY_COUNT) {
		dev_err(dev, "%pfwP: maximum number of entities exceeded\n",
			function_node);
		return -EINVAL;
	}

	entities = devm_kcalloc(dev, num_entities, sizeof(*entities), GFP_KERNEL);
	if (!entities)
		return -ENOMEM;

	entity_list = kcalloc(num_entities, sizeof(*entity_list), GFP_KERNEL);
	if (!entity_list)
		return -ENOMEM;

	fwnode_property_read_u32_array(function_node, "mipi-sdca-entity-id-list",
				       entity_list, num_entities);

	for (i = 0; i < num_entities; i++)
		entities[i].id = entity_list[i];

	kfree(entity_list);

	/* now read subproperties */
	for (i = 0; i < num_entities; i++) {
		char entity_property[SDCA_PROPERTY_LENGTH];
		struct fwnode_handle *entity_node;

		/* DisCo uses upper-case for hex numbers */
		snprintf(entity_property, sizeof(entity_property),
			 "mipi-sdca-entity-id-0x%X-subproperties", entities[i].id);

		entity_node = fwnode_get_named_child_node(function_node, entity_property);
		if (!entity_node) {
			dev_err(dev, "%pfwP: entity node %s not found\n",
				function_node, entity_property);
			return -EINVAL;
		}

		ret = find_sdca_entity(dev, function_node, entity_node, &entities[i]);
		fwnode_handle_put(entity_node);
		if (ret)
			return ret;
	}

	function->num_entities = num_entities;
	function->entities = entities;

	return 0;
}

static struct sdca_entity *find_sdca_entity_by_label(struct sdca_function_data *function,
						     const char *entity_label)
{
	int i;

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		if (!strcmp(entity->label, entity_label))
			return entity;
	}

	return NULL;
}

static int find_sdca_entity_connection(struct device *dev,
				       struct sdca_function_data *function,
				       struct fwnode_handle *entity_node,
				       struct sdca_entity *entity)
{
	struct sdca_entity **pins;
	int num_pins, pin;
	u64 pin_list;
	int i, ret;

	ret = fwnode_property_read_u64(entity_node, "mipi-sdca-input-pin-list", &pin_list);
	if (ret == -EINVAL) {
		/* Allow missing pin lists, assume no pins. */
		dev_warn(dev, "%s: missing pin list\n", entity->label);
		return 0;
	} else if (ret) {
		dev_err(dev, "%s: failed to read pin list: %d\n", entity->label, ret);
		return ret;
	} else if (pin_list & BIT(0)) {
		/*
		 * Each bit set in the pin-list refers to an entity_id in this
		 * Function. Entity 0 is an illegal connection since it is used
		 * for Function-level configurations.
		 */
		dev_err(dev, "%s: pin 0 used as input\n", entity->label);
		return -EINVAL;
	} else if (!pin_list) {
		return 0;
	}

	num_pins = hweight64(pin_list);
	pins = devm_kcalloc(dev, num_pins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	i = 0;
	for_each_set_bit(pin, (unsigned long *)&pin_list, BITS_PER_TYPE(pin_list)) {
		char pin_property[SDCA_PROPERTY_LENGTH];
		struct fwnode_handle *connected_node;
		struct sdca_entity *connected_entity;
		const char *connected_label;

		snprintf(pin_property, sizeof(pin_property), "mipi-sdca-input-pin-%d", pin);

		connected_node = fwnode_get_named_child_node(entity_node, pin_property);
		if (!connected_node) {
			dev_err(dev, "%s: pin node %s not found\n",
				entity->label, pin_property);
			return -EINVAL;
		}

		ret = fwnode_property_read_string(connected_node, "mipi-sdca-entity-label",
						  &connected_label);
		if (ret) {
			dev_err(dev, "%s: pin %d label missing: %d\n",
				entity->label, pin, ret);
			fwnode_handle_put(connected_node);
			return ret;
		}

		connected_entity = find_sdca_entity_by_label(function, connected_label);
		if (!connected_entity) {
			dev_err(dev, "%s: failed to find entity with label %s\n",
				entity->label, connected_label);
			fwnode_handle_put(connected_node);
			return -EINVAL;
		}

		pins[i] = connected_entity;

		dev_info(dev, "%s -> %s\n", connected_entity->label, entity->label);

		i++;
		fwnode_handle_put(connected_node);
	}

	entity->num_sources = num_pins;
	entity->sources = pins;

	return 0;
}

static int find_sdca_connections(struct device *dev,
				 struct fwnode_handle *function_node,
				 struct sdca_function_data *function)
{
	int i;

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];
		char entity_property[SDCA_PROPERTY_LENGTH];
		struct fwnode_handle *entity_node;
		int ret;

		/* DisCo uses upper-case for hex numbers */
		snprintf(entity_property, sizeof(entity_property),
			 "mipi-sdca-entity-id-0x%X-subproperties",
			 entity->id);

		entity_node = fwnode_get_named_child_node(function_node, entity_property);
		if (!entity_node) {
			dev_err(dev, "%pfwP: entity node %s not found\n",
				function_node, entity_property);
			return -EINVAL;
		}

		ret = find_sdca_entity_connection(dev, function, entity_node, entity);
		fwnode_handle_put(entity_node);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * sdca_parse_function - parse ACPI DisCo for a Function
 * @dev: Pointer to device against which function data will be allocated.
 * @function_desc: Pointer to the Function short descriptor.
 * @function: Pointer to the Function information, to be populated.
 *
 * Return: Returns 0 for success.
 */
int sdca_parse_function(struct device *dev,
			struct sdca_function_desc *function_desc,
			struct sdca_function_data *function)
{
	u32 tmp;
	int ret;

	function->desc = function_desc;

	ret = fwnode_property_read_u32(function_desc->node,
				       "mipi-sdca-function-busy-max-delay", &tmp);
	if (!ret)
		function->busy_max_delay = tmp;

	dev_info(dev, "%pfwP: name %s delay %dus\n", function->desc->node,
		 function->desc->name, function->busy_max_delay);

	ret = find_sdca_entities(dev, function_desc->node, function);
	if (ret)
		return ret;

	ret = find_sdca_connections(dev, function_desc->node, function);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_NS(sdca_parse_function, "SND_SOC_SDCA");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SDCA library");
