// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2024 Intel Corporation

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#define dev_fmt(fmt) "%s: " fmt, __func__

#include <linux/acpi.h>
#include <linux/byteorder/generic.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/soundwire/sdw.h>
#include <linux/types.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_hid.h>

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
	struct sdw_slave *slave = container_of(sdca_data, struct sdw_slave, sdca_data);
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

	if (!sdca_device_quirk_match(slave, SDCA_QUIRKS_SKIP_FUNC_TYPE_PATCHING)) {
		ret = patch_sdca_function_type(sdca_data->interface_revision, &function_type);
		if (ret < 0) {
			dev_err(dev, "SDCA version %#x invalid function type %d\n",
				sdca_data->interface_revision, function_type);
			return ret;
		}
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

struct raw_init_write {
	__le32 addr;
	u8 val;
} __packed;

static int find_sdca_init_table(struct device *dev,
				struct fwnode_handle *function_node,
				struct sdca_function_data *function)
{
	struct raw_init_write *raw __free(kfree) = NULL;
	struct sdca_init_write *init_write;
	int i, num_init_writes;

	num_init_writes = fwnode_property_count_u8(function_node,
						   "mipi-sdca-function-initialization-table");
	if (!num_init_writes || num_init_writes == -EINVAL) {
		return 0;
	} else if (num_init_writes < 0) {
		dev_err(dev, "%pfwP: failed to read initialization table: %d\n",
			function_node, num_init_writes);
		return num_init_writes;
	} else if (num_init_writes % sizeof(*raw) != 0) {
		dev_err(dev, "%pfwP: init table size invalid\n", function_node);
		return -EINVAL;
	} else if ((num_init_writes / sizeof(*raw)) > SDCA_MAX_INIT_COUNT) {
		dev_err(dev, "%pfwP: maximum init table size exceeded\n", function_node);
		return -EINVAL;
	}

	raw = kzalloc(num_init_writes, GFP_KERNEL);
	if (!raw)
		return -ENOMEM;

	fwnode_property_read_u8_array(function_node,
				      "mipi-sdca-function-initialization-table",
				      (u8 *)raw, num_init_writes);

	num_init_writes /= sizeof(*raw);

	init_write = devm_kcalloc(dev, num_init_writes, sizeof(*init_write), GFP_KERNEL);
	if (!init_write)
		return -ENOMEM;

	for (i = 0; i < num_init_writes; i++) {
		init_write[i].addr = le32_to_cpu(raw[i].addr);
		init_write[i].val = raw[i].val;
	}

	function->num_init_table = num_init_writes;
	function->init_table = init_write;

	return 0;
}

static const char *find_sdca_control_label(struct device *dev,
					   const struct sdca_entity *entity,
					   const struct sdca_control *control)
{
	switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
	case SDCA_CTL_TYPE_S(IT, MIC_BIAS):
		return SDCA_CTL_MIC_BIAS_NAME;
	case SDCA_CTL_TYPE_S(IT, USAGE):
	case SDCA_CTL_TYPE_S(OT, USAGE):
		return SDCA_CTL_USAGE_NAME;
	case SDCA_CTL_TYPE_S(IT, LATENCY):
	case SDCA_CTL_TYPE_S(OT, LATENCY):
	case SDCA_CTL_TYPE_S(MU, LATENCY):
	case SDCA_CTL_TYPE_S(SU, LATENCY):
	case SDCA_CTL_TYPE_S(FU, LATENCY):
	case SDCA_CTL_TYPE_S(XU, LATENCY):
	case SDCA_CTL_TYPE_S(CRU, LATENCY):
	case SDCA_CTL_TYPE_S(UDMPU, LATENCY):
	case SDCA_CTL_TYPE_S(MFPU, LATENCY):
	case SDCA_CTL_TYPE_S(SMPU, LATENCY):
	case SDCA_CTL_TYPE_S(SAPU, LATENCY):
	case SDCA_CTL_TYPE_S(PPU, LATENCY):
		return SDCA_CTL_LATENCY_NAME;
	case SDCA_CTL_TYPE_S(IT, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(CRU, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(UDMPU, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(MFPU, CLUSTERINDEX):
		return SDCA_CTL_CLUSTERINDEX_NAME;
	case SDCA_CTL_TYPE_S(IT, DATAPORT_SELECTOR):
	case SDCA_CTL_TYPE_S(OT, DATAPORT_SELECTOR):
		return SDCA_CTL_DATAPORT_SELECTOR_NAME;
	case SDCA_CTL_TYPE_S(IT, MATCHING_GUID):
	case SDCA_CTL_TYPE_S(OT, MATCHING_GUID):
	case SDCA_CTL_TYPE_S(ENTITY_0, MATCHING_GUID):
		return SDCA_CTL_MATCHING_GUID_NAME;
	case SDCA_CTL_TYPE_S(IT, KEEP_ALIVE):
	case SDCA_CTL_TYPE_S(OT, KEEP_ALIVE):
		return SDCA_CTL_KEEP_ALIVE_NAME;
	case SDCA_CTL_TYPE_S(IT, NDAI_STREAM):
	case SDCA_CTL_TYPE_S(OT, NDAI_STREAM):
		return SDCA_CTL_NDAI_STREAM_NAME;
	case SDCA_CTL_TYPE_S(IT, NDAI_CATEGORY):
	case SDCA_CTL_TYPE_S(OT, NDAI_CATEGORY):
		return SDCA_CTL_NDAI_CATEGORY_NAME;
	case SDCA_CTL_TYPE_S(IT, NDAI_CODINGTYPE):
	case SDCA_CTL_TYPE_S(OT, NDAI_CODINGTYPE):
		return SDCA_CTL_NDAI_CODINGTYPE_NAME;
	case SDCA_CTL_TYPE_S(IT, NDAI_PACKETTYPE):
	case SDCA_CTL_TYPE_S(OT, NDAI_PACKETTYPE):
		return SDCA_CTL_NDAI_PACKETTYPE_NAME;
	case SDCA_CTL_TYPE_S(MU, MIXER):
		return SDCA_CTL_MIXER_NAME;
	case SDCA_CTL_TYPE_S(SU, SELECTOR):
		return SDCA_CTL_SELECTOR_NAME;
	case SDCA_CTL_TYPE_S(FU, MUTE):
		return SDCA_CTL_MUTE_NAME;
	case SDCA_CTL_TYPE_S(FU, CHANNEL_VOLUME):
		return SDCA_CTL_CHANNEL_VOLUME_NAME;
	case SDCA_CTL_TYPE_S(FU, AGC):
		return SDCA_CTL_AGC_NAME;
	case SDCA_CTL_TYPE_S(FU, BASS_BOOST):
		return SDCA_CTL_BASS_BOOST_NAME;
	case SDCA_CTL_TYPE_S(FU, LOUDNESS):
		return SDCA_CTL_LOUDNESS_NAME;
	case SDCA_CTL_TYPE_S(FU, GAIN):
		return SDCA_CTL_GAIN_NAME;
	case SDCA_CTL_TYPE_S(XU, BYPASS):
	case SDCA_CTL_TYPE_S(MFPU, BYPASS):
		return SDCA_CTL_BYPASS_NAME;
	case SDCA_CTL_TYPE_S(XU, XU_ID):
		return SDCA_CTL_XU_ID_NAME;
	case SDCA_CTL_TYPE_S(XU, XU_VERSION):
		return SDCA_CTL_XU_VERSION_NAME;
	case SDCA_CTL_TYPE_S(XU, FDL_CURRENTOWNER):
		return SDCA_CTL_FDL_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(XU, FDL_MESSAGEOFFSET):
		return SDCA_CTL_FDL_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(XU, FDL_MESSAGELENGTH):
		return SDCA_CTL_FDL_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(XU, FDL_STATUS):
		return SDCA_CTL_FDL_STATUS_NAME;
	case SDCA_CTL_TYPE_S(XU, FDL_SET_INDEX):
		return SDCA_CTL_FDL_SET_INDEX_NAME;
	case SDCA_CTL_TYPE_S(XU, FDL_HOST_REQUEST):
		return SDCA_CTL_FDL_HOST_REQUEST_NAME;
	case SDCA_CTL_TYPE_S(CS, CLOCK_VALID):
		return SDCA_CTL_CLOCK_VALID_NAME;
	case SDCA_CTL_TYPE_S(CS, SAMPLERATEINDEX):
		return SDCA_CTL_SAMPLERATEINDEX_NAME;
	case SDCA_CTL_TYPE_S(CX, CLOCK_SELECT):
		return SDCA_CTL_CLOCK_SELECT_NAME;
	case SDCA_CTL_TYPE_S(PDE, REQUESTED_PS):
		return SDCA_CTL_REQUESTED_PS_NAME;
	case SDCA_CTL_TYPE_S(PDE, ACTUAL_PS):
		return SDCA_CTL_ACTUAL_PS_NAME;
	case SDCA_CTL_TYPE_S(GE, SELECTED_MODE):
		return SDCA_CTL_SELECTED_MODE_NAME;
	case SDCA_CTL_TYPE_S(GE, DETECTED_MODE):
		return SDCA_CTL_DETECTED_MODE_NAME;
	case SDCA_CTL_TYPE_S(SPE, PRIVATE):
		return SDCA_CTL_PRIVATE_NAME;
	case SDCA_CTL_TYPE_S(SPE, PRIVACY_POLICY):
		return SDCA_CTL_PRIVACY_POLICY_NAME;
	case SDCA_CTL_TYPE_S(SPE, PRIVACY_LOCKSTATE):
		return SDCA_CTL_PRIVACY_LOCKSTATE_NAME;
	case SDCA_CTL_TYPE_S(SPE, PRIVACY_OWNER):
		return SDCA_CTL_PRIVACY_OWNER_NAME;
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_CURRENTOWNER):
		return SDCA_CTL_AUTHTX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_MESSAGEOFFSET):
		return SDCA_CTL_AUTHTX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_MESSAGELENGTH):
		return SDCA_CTL_AUTHTX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_CURRENTOWNER):
		return SDCA_CTL_AUTHRX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_MESSAGEOFFSET):
		return SDCA_CTL_AUTHRX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_MESSAGELENGTH):
		return SDCA_CTL_AUTHRX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, ACOUSTIC_ENERGY_LEVEL_MONITOR):
		return SDCA_CTL_ACOUSTIC_ENERGY_LEVEL_MONITOR_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, ULTRASOUND_LOOP_GAIN):
		return SDCA_CTL_ULTRASOUND_LOOP_GAIN_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_0):
		return SDCA_CTL_OPAQUESET_0_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_1):
		return SDCA_CTL_OPAQUESET_1_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_2):
		return SDCA_CTL_OPAQUESET_2_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_3):
		return SDCA_CTL_OPAQUESET_3_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_4):
		return SDCA_CTL_OPAQUESET_4_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_5):
		return SDCA_CTL_OPAQUESET_5_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_6):
		return SDCA_CTL_OPAQUESET_6_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_7):
		return SDCA_CTL_OPAQUESET_7_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_8):
		return SDCA_CTL_OPAQUESET_8_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_9):
		return SDCA_CTL_OPAQUESET_9_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_10):
		return SDCA_CTL_OPAQUESET_10_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_11):
		return SDCA_CTL_OPAQUESET_11_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_12):
		return SDCA_CTL_OPAQUESET_12_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_13):
		return SDCA_CTL_OPAQUESET_13_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_14):
		return SDCA_CTL_OPAQUESET_14_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_15):
		return SDCA_CTL_OPAQUESET_15_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_16):
		return SDCA_CTL_OPAQUESET_16_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_17):
		return SDCA_CTL_OPAQUESET_17_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_18):
		return SDCA_CTL_OPAQUESET_18_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_19):
		return SDCA_CTL_OPAQUESET_19_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_20):
		return SDCA_CTL_OPAQUESET_20_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_21):
		return SDCA_CTL_OPAQUESET_21_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_22):
		return SDCA_CTL_OPAQUESET_22_NAME;
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_23):
		return SDCA_CTL_OPAQUESET_23_NAME;
	case SDCA_CTL_TYPE_S(MFPU, ALGORITHM_READY):
		return SDCA_CTL_ALGORITHM_READY_NAME;
	case SDCA_CTL_TYPE_S(MFPU, ALGORITHM_ENABLE):
		return SDCA_CTL_ALGORITHM_ENABLE_NAME;
	case SDCA_CTL_TYPE_S(MFPU, ALGORITHM_PREPARE):
		return SDCA_CTL_ALGORITHM_PREPARE_NAME;
	case SDCA_CTL_TYPE_S(MFPU, CENTER_FREQUENCY_INDEX):
		return SDCA_CTL_CENTER_FREQUENCY_INDEX_NAME;
	case SDCA_CTL_TYPE_S(MFPU, ULTRASOUND_LEVEL):
		return SDCA_CTL_ULTRASOUND_LEVEL_NAME;
	case SDCA_CTL_TYPE_S(MFPU, AE_NUMBER):
		return SDCA_CTL_AE_NUMBER_NAME;
	case SDCA_CTL_TYPE_S(MFPU, AE_CURRENTOWNER):
		return SDCA_CTL_AE_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(MFPU, AE_MESSAGEOFFSET):
		return SDCA_CTL_AE_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(MFPU, AE_MESSAGELENGTH):
		return SDCA_CTL_AE_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_ENABLE):
		return SDCA_CTL_TRIGGER_ENABLE_NAME;
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_STATUS):
		return SDCA_CTL_TRIGGER_STATUS_NAME;
	case SDCA_CTL_TYPE_S(SMPU, HIST_BUFFER_MODE):
		return SDCA_CTL_HIST_BUFFER_MODE_NAME;
	case SDCA_CTL_TYPE_S(SMPU, HIST_BUFFER_PREAMBLE):
		return SDCA_CTL_HIST_BUFFER_PREAMBLE_NAME;
	case SDCA_CTL_TYPE_S(SMPU, HIST_ERROR):
		return SDCA_CTL_HIST_ERROR_NAME;
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_EXTENSION):
		return SDCA_CTL_TRIGGER_EXTENSION_NAME;
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_READY):
		return SDCA_CTL_TRIGGER_READY_NAME;
	case SDCA_CTL_TYPE_S(SMPU, HIST_CURRENTOWNER):
		return SDCA_CTL_HIST_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SMPU, HIST_MESSAGEOFFSET):
		return SDCA_CTL_HIST_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SMPU, HIST_MESSAGELENGTH):
		return SDCA_CTL_HIST_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_CURRENTOWNER):
		return SDCA_CTL_DTODTX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_MESSAGEOFFSET):
		return SDCA_CTL_DTODTX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_MESSAGELENGTH):
		return SDCA_CTL_DTODTX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_CURRENTOWNER):
		return SDCA_CTL_DTODRX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_MESSAGEOFFSET):
		return SDCA_CTL_DTODRX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_MESSAGELENGTH):
		return SDCA_CTL_DTODRX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(SAPU, PROTECTION_MODE):
		return SDCA_CTL_PROTECTION_MODE_NAME;
	case SDCA_CTL_TYPE_S(SAPU, PROTECTION_STATUS):
		return SDCA_CTL_PROTECTION_STATUS_NAME;
	case SDCA_CTL_TYPE_S(SAPU, OPAQUESETREQ_INDEX):
		return SDCA_CTL_OPAQUESETREQ_INDEX_NAME;
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_CURRENTOWNER):
		return SDCA_CTL_DTODTX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_MESSAGEOFFSET):
		return SDCA_CTL_DTODTX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_MESSAGELENGTH):
		return SDCA_CTL_DTODTX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_CURRENTOWNER):
		return SDCA_CTL_DTODRX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_MESSAGEOFFSET):
		return SDCA_CTL_DTODRX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_MESSAGELENGTH):
		return SDCA_CTL_DTODRX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(PPU, POSTURENUMBER):
		return SDCA_CTL_POSTURENUMBER_NAME;
	case SDCA_CTL_TYPE_S(PPU, POSTUREEXTENSION):
		return SDCA_CTL_POSTUREEXTENSION_NAME;
	case SDCA_CTL_TYPE_S(PPU, HORIZONTALBALANCE):
		return SDCA_CTL_HORIZONTALBALANCE_NAME;
	case SDCA_CTL_TYPE_S(PPU, VERTICALBALANCE):
		return SDCA_CTL_VERTICALBALANCE_NAME;
	case SDCA_CTL_TYPE_S(TG, TONE_DIVIDER):
		return SDCA_CTL_TONE_DIVIDER_NAME;
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_CURRENTOWNER):
		return SDCA_CTL_HIDTX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_MESSAGEOFFSET):
		return SDCA_CTL_HIDTX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_MESSAGELENGTH):
		return SDCA_CTL_HIDTX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_CURRENTOWNER):
		return SDCA_CTL_HIDRX_CURRENTOWNER_NAME;
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_MESSAGEOFFSET):
		return SDCA_CTL_HIDRX_MESSAGEOFFSET_NAME;
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_MESSAGELENGTH):
		return SDCA_CTL_HIDRX_MESSAGELENGTH_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, COMMIT_GROUP_MASK):
		return SDCA_CTL_COMMIT_GROUP_MASK_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_SDCA_VERSION):
		return SDCA_CTL_FUNCTION_SDCA_VERSION_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_TYPE):
		return SDCA_CTL_FUNCTION_TYPE_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_MANUFACTURER_ID):
		return SDCA_CTL_FUNCTION_MANUFACTURER_ID_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_ID):
		return SDCA_CTL_FUNCTION_ID_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_VERSION):
		return SDCA_CTL_FUNCTION_VERSION_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_EXTENSION_ID):
		return SDCA_CTL_FUNCTION_EXTENSION_ID_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_EXTENSION_VERSION):
		return SDCA_CTL_FUNCTION_EXTENSION_VERSION_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_STATUS):
		return SDCA_CTL_FUNCTION_STATUS_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_ACTION):
		return SDCA_CTL_FUNCTION_ACTION_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_MANUFACTURER_ID):
		return SDCA_CTL_DEVICE_MANUFACTURER_ID_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_PART_ID):
		return SDCA_CTL_DEVICE_PART_ID_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_VERSION):
		return SDCA_CTL_DEVICE_VERSION_NAME;
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_SDCA_VERSION):
		return SDCA_CTL_DEVICE_SDCA_VERSION_NAME;
	default:
		return devm_kasprintf(dev, GFP_KERNEL, "Imp-Def %#x", control->sel);
	}
}

static unsigned int find_sdca_control_bits(const struct sdca_entity *entity,
					   const struct sdca_control *control)
{
	switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
	case SDCA_CTL_TYPE_S(IT, LATENCY):
	case SDCA_CTL_TYPE_S(OT, LATENCY):
	case SDCA_CTL_TYPE_S(MU, LATENCY):
	case SDCA_CTL_TYPE_S(SU, LATENCY):
	case SDCA_CTL_TYPE_S(FU, LATENCY):
	case SDCA_CTL_TYPE_S(XU, LATENCY):
	case SDCA_CTL_TYPE_S(XU, FDL_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(XU, FDL_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(CRU, LATENCY):
	case SDCA_CTL_TYPE_S(UDMPU, LATENCY):
	case SDCA_CTL_TYPE_S(MFPU, LATENCY):
	case SDCA_CTL_TYPE_S(MFPU, AE_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(MFPU, AE_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SMPU, LATENCY):
	case SDCA_CTL_TYPE_S(SMPU, HIST_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SMPU, HIST_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SAPU, LATENCY):
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(PPU, LATENCY):
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_MESSAGELENGTH):
		return 32;
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_MANUFACTURER_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_EXTENSION_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_MANUFACTURER_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_PART_ID):
	case SDCA_CTL_TYPE_S(IT, DATAPORT_SELECTOR):
	case SDCA_CTL_TYPE_S(OT, DATAPORT_SELECTOR):
	case SDCA_CTL_TYPE_S(MU, MIXER):
	case SDCA_CTL_TYPE_S(FU, CHANNEL_VOLUME):
	case SDCA_CTL_TYPE_S(FU, GAIN):
	case SDCA_CTL_TYPE_S(XU, XU_ID):
	case SDCA_CTL_TYPE_S(UDMPU, ACOUSTIC_ENERGY_LEVEL_MONITOR):
	case SDCA_CTL_TYPE_S(UDMPU, ULTRASOUND_LOOP_GAIN):
	case SDCA_CTL_TYPE_S(MFPU, ULTRASOUND_LEVEL):
	case SDCA_CTL_TYPE_S(PPU, HORIZONTALBALANCE):
	case SDCA_CTL_TYPE_S(PPU, VERTICALBALANCE):
		return 16;
	case SDCA_CTL_TYPE_S(FU, MUTE):
	case SDCA_CTL_TYPE_S(FU, AGC):
	case SDCA_CTL_TYPE_S(FU, BASS_BOOST):
	case SDCA_CTL_TYPE_S(FU, LOUDNESS):
	case SDCA_CTL_TYPE_S(XU, BYPASS):
	case SDCA_CTL_TYPE_S(MFPU, BYPASS):
		return 1;
	default:
		return 8;
	}
}

static enum sdca_control_datatype
find_sdca_control_datatype(const struct sdca_entity *entity,
			   const struct sdca_control *control)
{
	switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
	case SDCA_CTL_TYPE_S(XU, BYPASS):
	case SDCA_CTL_TYPE_S(MFPU, BYPASS):
	case SDCA_CTL_TYPE_S(FU, MUTE):
	case SDCA_CTL_TYPE_S(FU, AGC):
	case SDCA_CTL_TYPE_S(FU, BASS_BOOST):
	case SDCA_CTL_TYPE_S(FU, LOUDNESS):
		return SDCA_CTL_DATATYPE_ONEBIT;
	case SDCA_CTL_TYPE_S(IT, LATENCY):
	case SDCA_CTL_TYPE_S(OT, LATENCY):
	case SDCA_CTL_TYPE_S(MU, LATENCY):
	case SDCA_CTL_TYPE_S(SU, LATENCY):
	case SDCA_CTL_TYPE_S(FU, LATENCY):
	case SDCA_CTL_TYPE_S(XU, LATENCY):
	case SDCA_CTL_TYPE_S(CRU, LATENCY):
	case SDCA_CTL_TYPE_S(UDMPU, LATENCY):
	case SDCA_CTL_TYPE_S(MFPU, LATENCY):
	case SDCA_CTL_TYPE_S(SMPU, LATENCY):
	case SDCA_CTL_TYPE_S(SAPU, LATENCY):
	case SDCA_CTL_TYPE_S(PPU, LATENCY):
	case SDCA_CTL_TYPE_S(SU, SELECTOR):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_0):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_1):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_2):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_3):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_4):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_5):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_6):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_7):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_8):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_9):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_10):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_11):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_12):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_13):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_14):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_15):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_16):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_17):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_18):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_19):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_20):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_21):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_22):
	case SDCA_CTL_TYPE_S(UDMPU, OPAQUESET_23):
	case SDCA_CTL_TYPE_S(SAPU, PROTECTION_MODE):
	case SDCA_CTL_TYPE_S(SMPU, HIST_BUFFER_PREAMBLE):
	case SDCA_CTL_TYPE_S(XU, FDL_HOST_REQUEST):
	case SDCA_CTL_TYPE_S(XU, XU_ID):
	case SDCA_CTL_TYPE_S(CX, CLOCK_SELECT):
	case SDCA_CTL_TYPE_S(TG, TONE_DIVIDER):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_MANUFACTURER_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_EXTENSION_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_MANUFACTURER_ID):
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_PART_ID):
	case SDCA_CTL_TYPE_S(XU, FDL_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(XU, FDL_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(MFPU, AE_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(MFPU, AE_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SMPU, HIST_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SMPU, HIST_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_MESSAGELENGTH):
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_MESSAGEOFFSET):
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_MESSAGELENGTH):
		return SDCA_CTL_DATATYPE_INTEGER;
	case SDCA_CTL_TYPE_S(IT, MIC_BIAS):
	case SDCA_CTL_TYPE_S(SMPU, HIST_BUFFER_MODE):
	case SDCA_CTL_TYPE_S(PDE, REQUESTED_PS):
	case SDCA_CTL_TYPE_S(PDE, ACTUAL_PS):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_TYPE):
		return SDCA_CTL_DATATYPE_SPEC_ENCODED_VALUE;
	case SDCA_CTL_TYPE_S(XU, XU_VERSION):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_SDCA_VERSION):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_VERSION):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_EXTENSION_VERSION):
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_VERSION):
	case SDCA_CTL_TYPE_S(ENTITY_0, DEVICE_SDCA_VERSION):
		return SDCA_CTL_DATATYPE_BCD;
	case SDCA_CTL_TYPE_S(FU, CHANNEL_VOLUME):
	case SDCA_CTL_TYPE_S(FU, GAIN):
	case SDCA_CTL_TYPE_S(MU, MIXER):
	case SDCA_CTL_TYPE_S(PPU, HORIZONTALBALANCE):
	case SDCA_CTL_TYPE_S(PPU, VERTICALBALANCE):
	case SDCA_CTL_TYPE_S(MFPU, ULTRASOUND_LEVEL):
	case SDCA_CTL_TYPE_S(UDMPU, ACOUSTIC_ENERGY_LEVEL_MONITOR):
	case SDCA_CTL_TYPE_S(UDMPU, ULTRASOUND_LOOP_GAIN):
		return SDCA_CTL_DATATYPE_Q7P8DB;
	case SDCA_CTL_TYPE_S(IT, USAGE):
	case SDCA_CTL_TYPE_S(OT, USAGE):
	case SDCA_CTL_TYPE_S(IT, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(CRU, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(UDMPU, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(MFPU, CLUSTERINDEX):
	case SDCA_CTL_TYPE_S(MFPU, CENTER_FREQUENCY_INDEX):
	case SDCA_CTL_TYPE_S(MFPU, AE_NUMBER):
	case SDCA_CTL_TYPE_S(SAPU, OPAQUESETREQ_INDEX):
	case SDCA_CTL_TYPE_S(XU, FDL_SET_INDEX):
	case SDCA_CTL_TYPE_S(CS, SAMPLERATEINDEX):
	case SDCA_CTL_TYPE_S(GE, SELECTED_MODE):
	case SDCA_CTL_TYPE_S(GE, DETECTED_MODE):
		return SDCA_CTL_DATATYPE_BYTEINDEX;
	case SDCA_CTL_TYPE_S(PPU, POSTURENUMBER):
		return SDCA_CTL_DATATYPE_POSTURENUMBER;
	case SDCA_CTL_TYPE_S(IT, DATAPORT_SELECTOR):
	case SDCA_CTL_TYPE_S(OT, DATAPORT_SELECTOR):
		return SDCA_CTL_DATATYPE_DP_INDEX;
	case SDCA_CTL_TYPE_S(MFPU, ALGORITHM_READY):
	case SDCA_CTL_TYPE_S(MFPU, ALGORITHM_ENABLE):
	case SDCA_CTL_TYPE_S(MFPU, ALGORITHM_PREPARE):
	case SDCA_CTL_TYPE_S(SAPU, PROTECTION_STATUS):
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_ENABLE):
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_STATUS):
	case SDCA_CTL_TYPE_S(SMPU, TRIGGER_READY):
	case SDCA_CTL_TYPE_S(SPE, PRIVACY_POLICY):
	case SDCA_CTL_TYPE_S(SPE, PRIVACY_OWNER):
		return SDCA_CTL_DATATYPE_BITINDEX;
	case SDCA_CTL_TYPE_S(IT, KEEP_ALIVE):
	case SDCA_CTL_TYPE_S(OT, KEEP_ALIVE):
	case SDCA_CTL_TYPE_S(IT, NDAI_STREAM):
	case SDCA_CTL_TYPE_S(OT, NDAI_STREAM):
	case SDCA_CTL_TYPE_S(IT, NDAI_CATEGORY):
	case SDCA_CTL_TYPE_S(OT, NDAI_CATEGORY):
	case SDCA_CTL_TYPE_S(IT, NDAI_CODINGTYPE):
	case SDCA_CTL_TYPE_S(OT, NDAI_CODINGTYPE):
	case SDCA_CTL_TYPE_S(IT, NDAI_PACKETTYPE):
	case SDCA_CTL_TYPE_S(OT, NDAI_PACKETTYPE):
	case SDCA_CTL_TYPE_S(SMPU, HIST_ERROR):
	case SDCA_CTL_TYPE_S(XU, FDL_STATUS):
	case SDCA_CTL_TYPE_S(CS, CLOCK_VALID):
	case SDCA_CTL_TYPE_S(SPE, PRIVACY_LOCKSTATE):
	case SDCA_CTL_TYPE_S(ENTITY_0, COMMIT_GROUP_MASK):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_STATUS):
	case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_ACTION):
	case SDCA_CTL_TYPE_S(XU, FDL_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SPE, AUTHTX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SPE, AUTHRX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(MFPU, AE_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SMPU, HIST_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SMPU, DTODTX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SMPU, DTODRX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SAPU, DTODTX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(SAPU, DTODRX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(HIDE, HIDTX_CURRENTOWNER):
	case SDCA_CTL_TYPE_S(HIDE, HIDRX_CURRENTOWNER):
		return SDCA_CTL_DATATYPE_BITMAP;
	case SDCA_CTL_TYPE_S(IT, MATCHING_GUID):
	case SDCA_CTL_TYPE_S(OT, MATCHING_GUID):
	case SDCA_CTL_TYPE_S(ENTITY_0, MATCHING_GUID):
		return SDCA_CTL_DATATYPE_GUID;
	default:
		return SDCA_CTL_DATATYPE_IMPDEF;
	}
}

static int find_sdca_control_range(struct device *dev,
				   struct fwnode_handle *control_node,
				   struct sdca_control_range *range)
{
	u8 *range_list;
	int num_range;
	u16 *limits;
	int i;

	num_range = fwnode_property_count_u8(control_node, "mipi-sdca-control-range");
	if (!num_range || num_range == -EINVAL)
		return 0;
	else if (num_range < 0)
		return num_range;

	range_list = devm_kcalloc(dev, num_range, sizeof(*range_list), GFP_KERNEL);
	if (!range_list)
		return -ENOMEM;

	fwnode_property_read_u8_array(control_node, "mipi-sdca-control-range",
				      range_list, num_range);

	limits = (u16 *)range_list;

	range->cols = le16_to_cpu(limits[0]);
	range->rows = le16_to_cpu(limits[1]);
	range->data = (u32 *)&limits[2];

	num_range = (num_range - (2 * sizeof(*limits))) / sizeof(*range->data);
	if (num_range != range->cols * range->rows)
		return -EINVAL;

	for (i = 0; i < num_range; i++)
		range->data[i] = le32_to_cpu(range->data[i]);

	return 0;
}

static int find_sdca_control_value(struct device *dev, struct sdca_entity *entity,
				   struct fwnode_handle *control_node,
				   struct sdca_control *control,
				   const char * const label)
{
	char property[SDCA_PROPERTY_LENGTH];
	bool global = true;
	int ret, cn, i;
	u32 tmp;

	snprintf(property, sizeof(property), "mipi-sdca-control-%s", label);

	ret = fwnode_property_read_u32(control_node, property, &tmp);
	if (ret == -EINVAL)
		global = false;
	else if (ret)
		return ret;

	i = 0;
	for_each_set_bit(cn, (unsigned long *)&control->cn_list,
			 BITS_PER_TYPE(control->cn_list)) {
		if (!global) {
			snprintf(property, sizeof(property),
				 "mipi-sdca-control-cn-%d-%s", cn, label);

			ret = fwnode_property_read_u32(control_node, property, &tmp);
			if (ret)
				return ret;
		}

		control->values[i] = tmp;
		i++;
	}

	return 0;
}

/*
 * TODO: Add support for -cn- properties, allowing different channels to have
 * different defaults etc.
 */
static int find_sdca_entity_control(struct device *dev, struct sdca_entity *entity,
				    struct fwnode_handle *control_node,
				    struct sdca_control *control)
{
	u32 tmp;
	int ret;

	ret = fwnode_property_read_u32(control_node, "mipi-sdca-control-access-mode", &tmp);
	if (ret) {
		dev_err(dev, "%s: control %#x: access mode missing: %d\n",
			entity->label, control->sel, ret);
		return ret;
	}

	control->mode = tmp;

	ret = fwnode_property_read_u32(control_node, "mipi-sdca-control-access-layer", &tmp);
	if (ret) {
		dev_err(dev, "%s: control %#x: access layer missing: %d\n",
			entity->label, control->sel, ret);
		return ret;
	}

	control->layers = tmp;

	ret = fwnode_property_read_u64(control_node, "mipi-sdca-control-cn-list",
				       &control->cn_list);
	if (ret == -EINVAL) {
		/* Spec allows not specifying cn-list if only the first number is used */
		control->cn_list = 0x1;
	} else if (ret || !control->cn_list) {
		dev_err(dev, "%s: control %#x: cn list missing: %d\n",
			entity->label, control->sel, ret);
		return ret;
	}

	control->values = devm_kcalloc(dev, hweight64(control->cn_list),
				       sizeof(int), GFP_KERNEL);
	if (!control->values)
		return -ENOMEM;

	switch (control->mode) {
	case SDCA_ACCESS_MODE_DC:
		ret = find_sdca_control_value(dev, entity, control_node, control,
					      "dc-value");
		if (ret) {
			dev_err(dev, "%s: control %#x: dc value missing: %d\n",
				entity->label, control->sel, ret);
			return ret;
		}

		control->has_fixed = true;
		break;
	case SDCA_ACCESS_MODE_RW:
	case SDCA_ACCESS_MODE_DUAL:
		ret = find_sdca_control_value(dev, entity, control_node, control,
					      "default-value");
		if (!ret)
			control->has_default = true;

		ret = find_sdca_control_value(dev, entity, control_node, control,
					      "fixed-value");
		if (!ret)
			control->has_fixed = true;
		fallthrough;
	case SDCA_ACCESS_MODE_RO:
		control->deferrable = fwnode_property_read_bool(control_node,
								"mipi-sdca-control-deferrable");
		break;
	default:
		break;
	}

	ret = find_sdca_control_range(dev, control_node, &control->range);
	if (ret) {
		dev_err(dev, "%s: control %#x: range missing: %d\n",
			entity->label, control->sel, ret);
		return ret;
	}

	ret = fwnode_property_read_u32(control_node,
				       "mipi-sdca-control-interrupt-position",
				       &tmp);
	if (!ret)
		control->interrupt_position = tmp;
	else
		control->interrupt_position = SDCA_NO_INTERRUPT;

	control->label = find_sdca_control_label(dev, entity, control);
	if (!control->label)
		return -ENOMEM;

	control->type = find_sdca_control_datatype(entity, control);
	control->nbits = find_sdca_control_bits(entity, control);

	dev_info(dev, "%s: %s: control %#x mode %#x layers %#x cn %#llx int %d %s\n",
		 entity->label, control->label, control->sel,
		 control->mode, control->layers, control->cn_list,
		 control->interrupt_position, control->deferrable ? "deferrable" : "");

	return 0;
}

static int find_sdca_entity_controls(struct device *dev,
				     struct fwnode_handle *entity_node,
				     struct sdca_entity *entity)
{
	struct sdca_control *controls;
	int num_controls;
	u64 control_list;
	int control_sel;
	int i, ret;

	ret = fwnode_property_read_u64(entity_node, "mipi-sdca-control-list", &control_list);
	if (ret == -EINVAL) {
		/* Allow missing control lists, assume no controls. */
		dev_warn(dev, "%s: missing control list\n", entity->label);
		return 0;
	} else if (ret) {
		dev_err(dev, "%s: failed to read control list: %d\n", entity->label, ret);
		return ret;
	} else if (!control_list) {
		return 0;
	}

	num_controls = hweight64(control_list);
	controls = devm_kcalloc(dev, num_controls, sizeof(*controls), GFP_KERNEL);
	if (!controls)
		return -ENOMEM;

	i = 0;
	for_each_set_bit(control_sel, (unsigned long *)&control_list,
			 BITS_PER_TYPE(control_list)) {
		struct fwnode_handle *control_node;
		char control_property[SDCA_PROPERTY_LENGTH];

		/* DisCo uses upper-case for hex numbers */
		snprintf(control_property, sizeof(control_property),
			 "mipi-sdca-control-0x%X-subproperties", control_sel);

		control_node = fwnode_get_named_child_node(entity_node, control_property);
		if (!control_node) {
			dev_err(dev, "%s: control node %s not found\n",
				entity->label, control_property);
			return -EINVAL;
		}

		controls[i].sel = control_sel;

		ret = find_sdca_entity_control(dev, entity, control_node, &controls[i]);
		fwnode_handle_put(control_node);
		if (ret)
			return ret;

		i++;
	}

	entity->num_controls = num_controls;
	entity->controls = controls;

	return 0;
}

static bool find_sdca_iot_dataport(struct sdca_entity_iot *terminal)
{
	switch (terminal->type) {
	case SDCA_TERM_TYPE_GENERIC:
	case SDCA_TERM_TYPE_ULTRASOUND:
	case SDCA_TERM_TYPE_CAPTURE_DIRECT_PCM_MIC:
	case SDCA_TERM_TYPE_RAW_PDM_MIC:
	case SDCA_TERM_TYPE_SPEECH:
	case SDCA_TERM_TYPE_VOICE:
	case SDCA_TERM_TYPE_SECONDARY_PCM_MIC:
	case SDCA_TERM_TYPE_ACOUSTIC_CONTEXT_AWARENESS:
	case SDCA_TERM_TYPE_DTOD_STREAM:
	case SDCA_TERM_TYPE_REFERENCE_STREAM:
	case SDCA_TERM_TYPE_SENSE_CAPTURE:
	case SDCA_TERM_TYPE_STREAMING_MIC:
	case SDCA_TERM_TYPE_OPTIMIZATION_STREAM:
	case SDCA_TERM_TYPE_PDM_RENDER_STREAM:
	case SDCA_TERM_TYPE_COMPANION_DATA:
		return true;
	default:
		return false;
	}
}

static int find_sdca_entity_iot(struct device *dev,
				struct fwnode_handle *entity_node,
				struct sdca_entity *entity)
{
	struct sdca_entity_iot *terminal = &entity->iot;
	u32 tmp;
	int ret;

	ret = fwnode_property_read_u32(entity_node, "mipi-sdca-terminal-type", &tmp);
	if (ret) {
		dev_err(dev, "%s: terminal type missing: %d\n", entity->label, ret);
		return ret;
	}

	terminal->type = tmp;
	terminal->is_dataport = find_sdca_iot_dataport(terminal);

	ret = fwnode_property_read_u32(entity_node,
				       "mipi-sdca-terminal-reference-number", &tmp);
	if (!ret)
		terminal->reference = tmp;

	ret = fwnode_property_read_u32(entity_node,
				       "mipi-sdca-terminal-connector-type", &tmp);
	if (!ret)
		terminal->connector = tmp;

	ret = fwnode_property_read_u32(entity_node,
				       "mipi-sdca-terminal-transducer-count", &tmp);
	if (!ret)
		terminal->num_transducer = tmp;

	dev_info(dev, "%s: terminal type %#x ref %#x conn %#x count %d\n",
		 entity->label, terminal->type, terminal->reference,
		 terminal->connector, terminal->num_transducer);

	return 0;
}

static int find_sdca_entity_cs(struct device *dev,
			       struct fwnode_handle *entity_node,
			       struct sdca_entity *entity)
{
	struct sdca_entity_cs *clock = &entity->cs;
	u32 tmp;
	int ret;

	ret = fwnode_property_read_u32(entity_node, "mipi-sdca-cs-type", &tmp);
	if (ret) {
		dev_err(dev, "%s: clock type missing: %d\n", entity->label, ret);
		return ret;
	}

	clock->type = tmp;

	ret = fwnode_property_read_u32(entity_node,
				       "mipi-sdca-clock-valid-max-delay", &tmp);
	if (!ret)
		clock->max_delay = tmp;

	dev_info(dev, "%s: clock type %#x delay %d\n", entity->label,
		 clock->type, clock->max_delay);

	return 0;
}

static int find_sdca_entity_pde(struct device *dev,
				struct fwnode_handle *entity_node,
				struct sdca_entity *entity)
{
	static const int mult_delay = 3;
	struct sdca_entity_pde *power = &entity->pde;
	u32 *delay_list __free(kfree) = NULL;
	struct sdca_pde_delay *delays;
	int num_delays;
	int i, j;

	num_delays = fwnode_property_count_u32(entity_node,
					       "mipi-sdca-powerdomain-transition-max-delay");
	if (num_delays <= 0) {
		dev_err(dev, "%s: max delay list missing: %d\n",
			entity->label, num_delays);
		return -EINVAL;
	} else if (num_delays % mult_delay != 0) {
		dev_err(dev, "%s: delays not multiple of %d\n",
			entity->label, mult_delay);
		return -EINVAL;
	} else if (num_delays > SDCA_MAX_DELAY_COUNT) {
		dev_err(dev, "%s: maximum number of transition delays exceeded\n",
			entity->label);
		return -EINVAL;
	}

	delay_list = kcalloc(num_delays, sizeof(*delay_list), GFP_KERNEL);
	if (!delay_list)
		return -ENOMEM;

	fwnode_property_read_u32_array(entity_node,
				       "mipi-sdca-powerdomain-transition-max-delay",
				       delay_list, num_delays);

	num_delays /= mult_delay;

	delays = devm_kcalloc(dev, num_delays, sizeof(*delays), GFP_KERNEL);
	if (!delays)
		return -ENOMEM;

	for (i = 0, j = 0; i < num_delays; i++) {
		delays[i].from_ps = delay_list[j++];
		delays[i].to_ps = delay_list[j++];
		delays[i].us = delay_list[j++];

		dev_info(dev, "%s: from %#x to %#x delay %dus\n", entity->label,
			 delays[i].from_ps, delays[i].to_ps, delays[i].us);
	}

	power->num_max_delay = num_delays;
	power->max_delay = delays;

	return 0;
}

struct raw_ge_mode {
	u8 val;
	u8 num_controls;
	struct {
		u8 id;
		u8 sel;
		u8 cn;
		__le32 val;
	} __packed controls[] __counted_by(num_controls);
} __packed;

static int find_sdca_entity_ge(struct device *dev,
			       struct fwnode_handle *entity_node,
			       struct sdca_entity *entity)
{
	struct sdca_entity_ge *group = &entity->ge;
	u8 *affected_list __free(kfree) = NULL;
	u8 *affected_iter;
	int num_affected;
	int i, j;

	num_affected = fwnode_property_count_u8(entity_node,
						"mipi-sdca-ge-selectedmode-controls-affected");
	if (!num_affected) {
		return 0;
	} else if (num_affected < 0) {
		dev_err(dev, "%s: failed to read affected controls: %d\n",
			entity->label, num_affected);
		return num_affected;
	} else if (num_affected > SDCA_MAX_AFFECTED_COUNT) {
		dev_err(dev, "%s: maximum affected controls size exceeded\n",
			entity->label);
		return -EINVAL;
	}

	affected_list = kcalloc(num_affected, sizeof(*affected_list), GFP_KERNEL);
	if (!affected_list)
		return -ENOMEM;

	fwnode_property_read_u8_array(entity_node,
				      "mipi-sdca-ge-selectedmode-controls-affected",
				      affected_list, num_affected);

	group->num_modes = *affected_list;
	affected_iter = affected_list + 1;

	group->modes = devm_kcalloc(dev, group->num_modes, sizeof(*group->modes),
				    GFP_KERNEL);
	if (!group->modes)
		return -ENOMEM;

	for (i = 0; i < group->num_modes; i++) {
		struct raw_ge_mode *raw = (struct raw_ge_mode *)affected_iter;
		struct sdca_ge_mode *mode = &group->modes[i];

		affected_iter += sizeof(*raw);
		if (affected_iter > affected_list + num_affected)
			goto bad_list;

		mode->val = raw->val;
		mode->num_controls = raw->num_controls;

		affected_iter += mode->num_controls * sizeof(raw->controls[0]);
		if (affected_iter > affected_list + num_affected)
			goto bad_list;

		mode->controls = devm_kcalloc(dev, mode->num_controls,
					      sizeof(*mode->controls), GFP_KERNEL);
		if (!mode->controls)
			return -ENOMEM;

		for (j = 0; j < mode->num_controls; j++) {
			mode->controls[j].id = raw->controls[j].id;
			mode->controls[j].sel = raw->controls[j].sel;
			mode->controls[j].cn = raw->controls[j].cn;
			mode->controls[j].val = le32_to_cpu(raw->controls[j].val);
		}
	}

	return 0;

bad_list:
	dev_err(dev, "%s: malformed affected controls list\n", entity->label);
	return -EINVAL;
}

static int
find_sdca_entity_hide(struct device *dev, struct fwnode_handle *function_node,
		      struct fwnode_handle *entity_node, struct sdca_entity *entity)
{
	struct sdca_entity_hide *hide = &entity->hide;
	unsigned int delay, *af_list = hide->af_number_list;
	int nval, ret;
	unsigned char *report_desc = NULL;

	ret = fwnode_property_read_u32(entity_node,
				       "mipi-sdca-RxUMP-ownership-transition-maxdelay", &delay);
	if (!ret)
		hide->max_delay = delay;

	nval = fwnode_property_count_u32(entity_node, "mipi-sdca-HIDTx-supported-report-ids");
	if (nval > 0) {
		hide->num_hidtx_ids = nval;
		hide->hidtx_ids = devm_kcalloc(dev, hide->num_hidtx_ids,
					       sizeof(*hide->hidtx_ids), GFP_KERNEL);
		if (!hide->hidtx_ids)
			return -ENOMEM;

		ret = fwnode_property_read_u32_array(entity_node,
						     "mipi-sdca-HIDTx-supported-report-ids",
						     hide->hidtx_ids,
						     hide->num_hidtx_ids);
		if (ret < 0)
			return ret;
	}

	nval = fwnode_property_count_u32(entity_node, "mipi-sdca-HIDRx-supported-report-ids");
	if (nval > 0) {
		hide->num_hidrx_ids = nval;
		hide->hidrx_ids = devm_kcalloc(dev, hide->num_hidrx_ids,
					       sizeof(*hide->hidrx_ids), GFP_KERNEL);
		if (!hide->hidrx_ids)
			return -ENOMEM;

		ret = fwnode_property_read_u32_array(entity_node,
						     "mipi-sdca-HIDRx-supported-report-ids",
						     hide->hidrx_ids,
						     hide->num_hidrx_ids);
		if (ret < 0)
			return ret;
	}

	nval = fwnode_property_count_u32(entity_node, "mipi-sdca-hide-related-audio-function-list");
	if (nval <= 0) {
		dev_err(dev, "%pfwP: audio function numbers list missing: %d\n",
			entity_node, nval);
		return -EINVAL;
	} else if (nval > SDCA_MAX_FUNCTION_COUNT) {
		dev_err(dev, "%pfwP: maximum number of audio function exceeded\n", entity_node);
		return -EINVAL;
	}

	hide->hide_reside_function_num = nval;
	fwnode_property_read_u32_array(entity_node,
				       "mipi-sdca-hide-related-audio-function-list", af_list, nval);

	nval = fwnode_property_count_u8(function_node, "mipi-sdca-hid-descriptor");
	if (nval)
		fwnode_property_read_u8_array(function_node, "mipi-sdca-hid-descriptor",
					      (u8 *)&hide->hid_desc, nval);

	if (hide->hid_desc.bNumDescriptors) {
		nval = fwnode_property_count_u8(function_node, "mipi-sdca-report-descriptor");
		if (nval) {
			report_desc = devm_kzalloc(dev, nval, GFP_KERNEL);
			if (!report_desc)
				return -ENOMEM;
			hide->hid_report_desc = report_desc;
			fwnode_property_read_u8_array(function_node, "mipi-sdca-report-descriptor",
						      report_desc, nval);

			/* add HID device */
			ret = sdca_add_hid_device(dev, entity);
			if (ret) {
				dev_err(dev, "%pfwP: failed to add HID device: %d\n", entity_node, ret);
				return ret;
			}
		}
	}

	return 0;
}

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

	switch (entity->type) {
	case SDCA_ENTITY_TYPE_IT:
	case SDCA_ENTITY_TYPE_OT:
		ret = find_sdca_entity_iot(dev, entity_node, entity);
		break;
	case SDCA_ENTITY_TYPE_CS:
		ret = find_sdca_entity_cs(dev, entity_node, entity);
		break;
	case SDCA_ENTITY_TYPE_PDE:
		ret = find_sdca_entity_pde(dev, entity_node, entity);
		break;
	case SDCA_ENTITY_TYPE_GE:
		ret = find_sdca_entity_ge(dev, entity_node, entity);
		break;
	case SDCA_ENTITY_TYPE_HIDE:
		ret = find_sdca_entity_hide(dev, function_node, entity_node, entity);
		break;
	default:
		break;
	}
	if (ret)
		return ret;

	ret = find_sdca_entity_controls(dev, entity_node, entity);
	if (ret)
		return ret;

	return 0;
}

static int find_sdca_entities(struct device *dev,
			      struct fwnode_handle *function_node,
			      struct sdca_function_data *function)
{
	u32 *entity_list __free(kfree) = NULL;
	struct sdca_entity *entities;
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

	/* Add 1 to make space for Entity 0 */
	entities = devm_kcalloc(dev, num_entities + 1, sizeof(*entities), GFP_KERNEL);
	if (!entities)
		return -ENOMEM;

	entity_list = kcalloc(num_entities, sizeof(*entity_list), GFP_KERNEL);
	if (!entity_list)
		return -ENOMEM;

	fwnode_property_read_u32_array(function_node, "mipi-sdca-entity-id-list",
				       entity_list, num_entities);

	for (i = 0; i < num_entities; i++)
		entities[i].id = entity_list[i];

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

	/*
	 * Add Entity 0 at end of the array, makes it easy to skip during
	 * all the Entity searches involved in creating connections.
	 */
	entities[num_entities].label = "entity0";

	ret = find_sdca_entity_controls(dev, function_node, &entities[num_entities]);
	if (ret)
		return ret;

	function->num_entities = num_entities + 1;
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

static struct sdca_entity *find_sdca_entity_by_id(struct sdca_function_data *function,
						  const int id)
{
	int i;

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		if (entity->id == id)
			return entity;
	}

	return NULL;
}

static int find_sdca_entity_connection_iot(struct device *dev,
					   struct sdca_function_data *function,
					   struct fwnode_handle *entity_node,
					   struct sdca_entity *entity)
{
	struct sdca_entity_iot *terminal = &entity->iot;
	struct fwnode_handle *clock_node;
	struct sdca_entity *clock_entity;
	const char *clock_label;
	int ret;

	clock_node = fwnode_get_named_child_node(entity_node,
						 "mipi-sdca-terminal-clock-connection");
	if (!clock_node)
		return 0;

	ret = fwnode_property_read_string(clock_node, "mipi-sdca-entity-label",
					  &clock_label);
	if (ret) {
		dev_err(dev, "%s: clock label missing: %d\n", entity->label, ret);
		fwnode_handle_put(clock_node);
		return ret;
	}

	clock_entity = find_sdca_entity_by_label(function, clock_label);
	if (!clock_entity) {
		dev_err(dev, "%s: failed to find clock with label %s\n",
			entity->label, clock_label);
		fwnode_handle_put(clock_node);
		return -EINVAL;
	}

	terminal->clock = clock_entity;

	dev_info(dev, "%s -> %s\n", clock_entity->label, entity->label);

	fwnode_handle_put(clock_node);
	return 0;
}

static int find_sdca_entity_connection_pde(struct device *dev,
					   struct sdca_function_data *function,
					   struct fwnode_handle *entity_node,
					   struct sdca_entity *entity)
{
	struct sdca_entity_pde *power = &entity->pde;
	u32 *managed_list __free(kfree) = NULL;
	struct sdca_entity **managed;
	int num_managed;
	int i;

	num_managed = fwnode_property_count_u32(entity_node,
						"mipi-sdca-powerdomain-managed-list");
	if (!num_managed) {
		return 0;
	} else if (num_managed < 0) {
		dev_err(dev, "%s: managed list missing: %d\n", entity->label, num_managed);
		return num_managed;
	} else if (num_managed > SDCA_MAX_ENTITY_COUNT) {
		dev_err(dev, "%s: maximum number of managed entities exceeded\n",
			entity->label);
		return -EINVAL;
	}

	managed = devm_kcalloc(dev, num_managed, sizeof(*managed), GFP_KERNEL);
	if (!managed)
		return -ENOMEM;

	managed_list = kcalloc(num_managed, sizeof(*managed_list), GFP_KERNEL);
	if (!managed_list)
		return -ENOMEM;

	fwnode_property_read_u32_array(entity_node,
				       "mipi-sdca-powerdomain-managed-list",
				       managed_list, num_managed);

	for (i = 0; i < num_managed; i++) {
		managed[i] = find_sdca_entity_by_id(function, managed_list[i]);
		if (!managed[i]) {
			dev_err(dev, "%s: failed to find entity with id %#x\n",
				entity->label, managed_list[i]);
			return -EINVAL;
		}

		dev_info(dev, "%s -> %s\n", managed[i]->label, entity->label);
	}

	power->num_managed = num_managed;
	power->managed = managed;

	return 0;
}

static int find_sdca_entity_connection_ge(struct device *dev,
					  struct sdca_function_data *function,
					  struct fwnode_handle *entity_node,
					  struct sdca_entity *entity)
{
	int i, j;

	for (i = 0; i < entity->ge.num_modes; i++) {
		struct sdca_ge_mode *mode = &entity->ge.modes[i];

		for (j = 0; j < mode->num_controls; j++) {
			struct sdca_ge_control *affected = &mode->controls[j];
			struct sdca_entity *managed;

			managed = find_sdca_entity_by_id(function, affected->id);
			if (!managed) {
				dev_err(dev, "%s: failed to find entity with id %#x\n",
					entity->label, affected->id);
				return -EINVAL;
			}

			if (managed->group && managed->group != entity) {
				dev_err(dev,
					"%s: entity controlled by two groups %s, %s\n",
					managed->label, managed->group->label,
					entity->label);
				return -EINVAL;
			}

			managed->group = entity;
		}
	}

	return 0;
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

	switch (entity->type) {
	case SDCA_ENTITY_TYPE_IT:
	case SDCA_ENTITY_TYPE_OT:
		ret = find_sdca_entity_connection_iot(dev, function,
						      entity_node, entity);
		break;
	case SDCA_ENTITY_TYPE_PDE:
		ret = find_sdca_entity_connection_pde(dev, function,
						      entity_node, entity);
		break;
	case SDCA_ENTITY_TYPE_GE:
		ret = find_sdca_entity_connection_ge(dev, function,
						     entity_node, entity);
		break;
	default:
		ret = 0;
		break;
	}
	if (ret)
		return ret;

	ret = fwnode_property_read_u64(entity_node, "mipi-sdca-input-pin-list", &pin_list);
	if (ret == -EINVAL) {
		/* Allow missing pin lists, assume no pins. */
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

	/* Entity 0 cannot have connections */
	for (i = 0; i < function->num_entities - 1; i++) {
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

static int find_sdca_cluster_channel(struct device *dev,
				     struct sdca_cluster *cluster,
				     struct fwnode_handle *channel_node,
				     struct sdca_channel *channel)
{
	u32 tmp;
	int ret;

	ret = fwnode_property_read_u32(channel_node, "mipi-sdca-cluster-channel-id", &tmp);
	if (ret) {
		dev_err(dev, "cluster %#x: missing channel id: %d\n",
			cluster->id, ret);
		return ret;
	}

	channel->id = tmp;

	ret = fwnode_property_read_u32(channel_node,
				       "mipi-sdca-cluster-channel-purpose",
				       &tmp);
	if (ret) {
		dev_err(dev, "cluster %#x: channel %#x: missing purpose: %d\n",
			cluster->id, channel->id, ret);
		return ret;
	}

	channel->purpose = tmp;

	ret = fwnode_property_read_u32(channel_node,
				       "mipi-sdca-cluster-channel-relationship",
				       &tmp);
	if (ret) {
		dev_err(dev, "cluster %#x: channel %#x: missing relationship: %d\n",
			cluster->id, channel->id, ret);
		return ret;
	}

	channel->relationship = tmp;

	dev_info(dev, "cluster %#x: channel id %#x purpose %#x relationship %#x\n",
		 cluster->id, channel->id, channel->purpose, channel->relationship);

	return 0;
}

static int find_sdca_cluster_channels(struct device *dev,
				      struct fwnode_handle *cluster_node,
				      struct sdca_cluster *cluster)
{
	struct sdca_channel *channels;
	u32 num_channels;
	int i, ret;

	ret = fwnode_property_read_u32(cluster_node, "mipi-sdca-channel-count",
				       &num_channels);
	if (ret < 0) {
		dev_err(dev, "cluster %#x: failed to read channel list: %d\n",
			cluster->id, ret);
		return ret;
	} else if (num_channels > SDCA_MAX_CHANNEL_COUNT) {
		dev_err(dev, "cluster %#x: maximum number of channels exceeded\n",
			cluster->id);
		return -EINVAL;
	}

	channels = devm_kcalloc(dev, num_channels, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	for (i = 0; i < num_channels; i++) {
		char channel_property[SDCA_PROPERTY_LENGTH];
		struct fwnode_handle *channel_node;

		/* DisCo uses upper-case for hex numbers */
		snprintf(channel_property, sizeof(channel_property),
			 "mipi-sdca-channel-%d-subproperties", i + 1);

		channel_node = fwnode_get_named_child_node(cluster_node, channel_property);
		if (!channel_node) {
			dev_err(dev, "cluster %#x: channel node %s not found\n",
				cluster->id, channel_property);
			return -EINVAL;
		}

		ret = find_sdca_cluster_channel(dev, cluster, channel_node, &channels[i]);
		fwnode_handle_put(channel_node);
		if (ret)
			return ret;
	}

	cluster->num_channels = num_channels;
	cluster->channels = channels;

	return 0;
}

static int find_sdca_clusters(struct device *dev,
			      struct fwnode_handle *function_node,
			      struct sdca_function_data *function)
{
	u32 *cluster_list __free(kfree) = NULL;
	struct sdca_cluster *clusters;
	int num_clusters;
	int i, ret;

	num_clusters = fwnode_property_count_u32(function_node, "mipi-sdca-cluster-id-list");
	if (!num_clusters || num_clusters == -EINVAL) {
		return 0;
	} else if (num_clusters < 0) {
		dev_err(dev, "%pfwP: failed to read cluster id list: %d\n",
			function_node, num_clusters);
		return num_clusters;
	} else if (num_clusters > SDCA_MAX_CLUSTER_COUNT) {
		dev_err(dev, "%pfwP: maximum number of clusters exceeded\n", function_node);
		return -EINVAL;
	}

	clusters = devm_kcalloc(dev, num_clusters, sizeof(*clusters), GFP_KERNEL);
	if (!clusters)
		return -ENOMEM;

	cluster_list = kcalloc(num_clusters, sizeof(*cluster_list), GFP_KERNEL);
	if (!cluster_list)
		return -ENOMEM;

	fwnode_property_read_u32_array(function_node, "mipi-sdca-cluster-id-list",
				       cluster_list, num_clusters);

	for (i = 0; i < num_clusters; i++)
		clusters[i].id = cluster_list[i];

	/* now read subproperties */
	for (i = 0; i < num_clusters; i++) {
		char cluster_property[SDCA_PROPERTY_LENGTH];
		struct fwnode_handle *cluster_node;

		/* DisCo uses upper-case for hex numbers */
		snprintf(cluster_property, sizeof(cluster_property),
			 "mipi-sdca-cluster-id-0x%X-subproperties", clusters[i].id);

		cluster_node = fwnode_get_named_child_node(function_node, cluster_property);
		if (!cluster_node) {
			dev_err(dev, "%pfwP: cluster node %s not found\n",
				function_node, cluster_property);
			return -EINVAL;
		}

		ret = find_sdca_cluster_channels(dev, cluster_node, &clusters[i]);
		fwnode_handle_put(cluster_node);
		if (ret)
			return ret;
	}

	function->num_clusters = num_clusters;
	function->clusters = clusters;

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

	ret = find_sdca_init_table(dev, function_desc->node, function);
	if (ret)
		return ret;

	ret = find_sdca_entities(dev, function_desc->node, function);
	if (ret)
		return ret;

	ret = find_sdca_connections(dev, function_desc->node, function);
	if (ret)
		return ret;

	ret = find_sdca_clusters(dev, function_desc->node, function);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_NS(sdca_parse_function, "SND_SOC_SDCA");

struct sdca_control *sdca_selector_find_control(struct device *dev,
						struct sdca_entity *entity,
						const int sel)
{
	int i;

	for (i = 0; i < entity->num_controls; i++) {
		struct sdca_control *control = &entity->controls[i];

		if (control->sel == sel)
			return control;
	}

	dev_err(dev, "%s: control %#x: missing\n", entity->label, sel);
	return NULL;
}
EXPORT_SYMBOL_NS(sdca_selector_find_control, "SND_SOC_SDCA");

struct sdca_control_range *sdca_control_find_range(struct device *dev,
						   struct sdca_entity *entity,
						   struct sdca_control *control,
						   int cols, int rows)
{
	struct sdca_control_range *range = &control->range;

	if ((cols && range->cols != cols) || (rows && range->rows != rows) ||
	    !range->data) {
		dev_err(dev, "%s: control %#x: ranges invalid (%d,%d)\n",
			entity->label, control->sel, range->cols, range->rows);
		return NULL;
	}

	return range;
}
EXPORT_SYMBOL_NS(sdca_control_find_range, "SND_SOC_SDCA");

struct sdca_control_range *sdca_selector_find_range(struct device *dev,
						    struct sdca_entity *entity,
						    int sel, int cols, int rows)
{
	struct sdca_control *control;

	control = sdca_selector_find_control(dev, entity, sel);
	if (!control)
		return NULL;

	return sdca_control_find_range(dev, entity, control, cols, rows);
}
EXPORT_SYMBOL_NS(sdca_selector_find_range, "SND_SOC_SDCA");

struct sdca_cluster *sdca_id_find_cluster(struct device *dev,
					  struct sdca_function_data *function,
					  const int id)
{
	int i;

	for (i = 0; i < function->num_clusters; i++) {
		struct sdca_cluster *cluster = &function->clusters[i];

		if (cluster->id == id)
			return cluster;
	}

	dev_err(dev, "%s: cluster %#x: missing\n", function->desc->name, id);
	return NULL;
}
EXPORT_SYMBOL_NS(sdca_id_find_cluster, "SND_SOC_SDCA");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("SDCA library");
