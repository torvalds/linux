// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2019-2022 Intel Corporation
//
// Author: Jyri Sarha <jyri.sarha@intel.com>
//

#include <sound/soc.h>
#include <sound/sof/ipc4/header.h>
#include <uapi/sound/sof/header.h>
#include "sof-audio.h"
#include "ipc4-priv.h"
#include "sof-client.h"
#include "sof-client-probes.h"

enum sof_ipc4_dma_type {
	SOF_IPC4_DMA_HDA_HOST_OUTPUT = 0,
	SOF_IPC4_DMA_HDA_HOST_INPUT = 1,
	SOF_IPC4_DMA_HDA_LINK_OUTPUT = 8,
	SOF_IPC4_DMA_HDA_LINK_INPUT = 9,
	SOF_IPC4_DMA_DMIC_LINK_INPUT = 11,
	SOF_IPC4_DMA_I2S_LINK_OUTPUT = 12,
	SOF_IPC4_DMA_I2S_LINK_INPUT = 13,
};

enum sof_ipc4_probe_runtime_param {
	SOF_IPC4_PROBE_INJECTION_DMA = 1,
	SOF_IPC4_PROBE_INJECTION_DMA_DETACH,
	SOF_IPC4_PROBE_POINTS,
	SOF_IPC4_PROBE_POINTS_DISCONNECT,
	SOF_IPC4_PROBE_POINTS_AVAILABLE,
};

struct sof_ipc4_probe_gtw_cfg {
	u32 node_id;
	u32 dma_buffer_size;
} __packed __aligned(4);

#define SOF_IPC4_PROBE_NODE_ID_INDEX(x)		((x) & GENMASK(7, 0))
#define SOF_IPC4_PROBE_NODE_ID_TYPE(x)		(((x) << 8) & GENMASK(12, 8))

struct sof_ipc4_probe_cfg {
	struct sof_ipc4_base_module_cfg base;
	struct sof_ipc4_probe_gtw_cfg gtw_cfg;
} __packed __aligned(4);

enum sof_ipc4_probe_type {
	SOF_IPC4_PROBE_TYPE_INPUT = 0,
	SOF_IPC4_PROBE_TYPE_OUTPUT,
	SOF_IPC4_PROBE_TYPE_INTERNAL
};

#define SOF_IPC4_PROBE_TYPE_SHIFT		24
#define SOF_IPC4_PROBE_TYPE_MASK		GENMASK(25, 24)
#define SOF_IPC4_PROBE_TYPE_GET(x)		(((x) & SOF_IPC4_PROBE_TYPE_MASK) \
						 >> SOF_IPC4_PROBE_TYPE_SHIFT)
#define SOF_IPC4_PROBE_IDX_SHIFT		26
#define SOF_IPC4_PROBE_IDX_MASK			GENMASK(31, 26)
#define SOF_IPC4_PROBE_IDX_GET(x)		(((x) & SOF_IPC4_PROBE_IDX_MASK) \
						 >> SOF_IPC4_PROBE_IDX_SHIFT)

struct sof_ipc4_probe_point {
	u32 point_id;
	u32 purpose;
	u32 stream_tag;
} __packed __aligned(4);

struct sof_ipc4_probe_info {
	unsigned int num_elems;
	DECLARE_FLEX_ARRAY(struct sof_ipc4_probe_point, points);
} __packed;

#define INVALID_PIPELINE_ID      0xFF

static const char *sof_probe_ipc4_type_string(u32 type)
{
	switch (type) {
	case SOF_IPC4_PROBE_TYPE_INPUT:
		return "input";
	case SOF_IPC4_PROBE_TYPE_OUTPUT:
		return "output";
	case SOF_IPC4_PROBE_TYPE_INTERNAL:
		return "internal";
	default:
		return "UNKNOWN";
	}
}

/**
 * sof_ipc4_probe_get_module_info - Get IPC4 module info for probe module
 * @cdev:		SOF client device
 * @return:		Pointer to IPC4 probe module info
 *
 * Look up the IPC4 probe module info based on the hard coded uuid and
 * store the value for the future calls.
 */
static struct sof_man4_module *sof_ipc4_probe_get_module_info(struct sof_client_dev *cdev)
{
	struct sof_probes_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	static const guid_t probe_uuid =
		GUID_INIT(0x7CAD0808, 0xAB10, 0xCD23,
			  0xEF, 0x45, 0x12, 0xAB, 0x34, 0xCD, 0x56, 0xEF);

	if (!priv->ipc_priv) {
		struct sof_ipc4_fw_module *fw_module =
			sof_client_ipc4_find_module(cdev, &probe_uuid);

		if (!fw_module) {
			dev_err(dev, "%s: no matching uuid found", __func__);
			return NULL;
		}

		priv->ipc_priv = &fw_module->man4_module_entry;
	}

	return (struct sof_man4_module *)priv->ipc_priv;
}

/**
 * ipc4_probes_init - initialize data probing
 * @cdev:		SOF client device
 * @stream_tag:		Extractor stream tag
 * @buffer_size:	DMA buffer size to set for extractor
 * @return:		0 on success, negative error code on error
 *
 * Host chooses whether extraction is supported or not by providing
 * valid stream tag to DSP. Once specified, stream described by that
 * tag will be tied to DSP for extraction for the entire lifetime of
 * probe.
 *
 * Probing is initialized only once and each INIT request must be
 * matched by DEINIT call.
 */
static int ipc4_probes_init(struct sof_client_dev *cdev, u32 stream_tag,
			    size_t buffer_size)
{
	struct sof_man4_module *mentry = sof_ipc4_probe_get_module_info(cdev);
	struct sof_ipc4_msg msg;
	struct sof_ipc4_probe_cfg cfg;

	if (!mentry)
		return -ENODEV;

	memset(&cfg, '\0', sizeof(cfg));
	cfg.gtw_cfg.node_id = SOF_IPC4_PROBE_NODE_ID_INDEX(stream_tag - 1) |
		SOF_IPC4_PROBE_NODE_ID_TYPE(SOF_IPC4_DMA_HDA_HOST_INPUT);

	cfg.gtw_cfg.dma_buffer_size = buffer_size;

	msg.primary = mentry->id;
	msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_INIT_INSTANCE);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.extension = SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(INVALID_PIPELINE_ID);
	msg.extension |= SOF_IPC4_MOD_EXT_CORE_ID(0);
	msg.extension |= SOF_IPC4_MOD_EXT_PARAM_SIZE(sizeof(cfg) / sizeof(uint32_t));

	msg.data_size = sizeof(cfg);
	msg.data_ptr = &cfg;

	return sof_client_ipc_tx_message_no_reply(cdev, &msg);
}

/**
 * ipc4_probes_deinit - cleanup after data probing
 * @cdev:		SOF client device
 * @return:		0 on success, negative error code on error
 *
 * Host sends DEINIT request to free previously initialized probe
 * on DSP side once it is no longer needed. DEINIT only when there
 * are no probes connected and with all injectors detached.
 */
static int ipc4_probes_deinit(struct sof_client_dev *cdev)
{
	struct sof_man4_module *mentry = sof_ipc4_probe_get_module_info(cdev);
	struct sof_ipc4_msg msg;

	if (!mentry)
		return -ENODEV;

	msg.primary = mentry->id;
	msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_DELETE_INSTANCE);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.extension = SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(INVALID_PIPELINE_ID);
	msg.extension |= SOF_IPC4_MOD_EXT_CORE_ID(0);

	msg.data_size = 0;
	msg.data_ptr = NULL;

	return sof_client_ipc_tx_message_no_reply(cdev, &msg);
}

/**
 * ipc4_probes_points_info - retrieve list of probe points
 * @cdev:	SOF client device
 * @desc:	Returned list of active probes
 * @num_desc:	Returned count of active probes
 * @type:	Either PROBES_INFO_ACTIVE_PROBES or PROBES_INFO_AVAILABE_PROBES
 * @return:	0 on success, negative error code on error
 *
 * Returns list if active probe points if type is
 * PROBES_INFO_ACTIVE_PROBES, or list of all available probe points if
 * type is PROBES_INFO_AVAILABE_PROBES.
 */
static int ipc4_probes_points_info(struct sof_client_dev *cdev,
				   struct sof_probe_point_desc **desc,
				   size_t *num_desc,
				   enum sof_probe_info_type type)
{
	struct sof_man4_module *mentry = sof_ipc4_probe_get_module_info(cdev);
	struct device *dev = &cdev->auxdev.dev;
	struct sof_ipc4_probe_info *info;
	struct sof_ipc4_msg msg;
	u32 param_id;
	int i, ret;

	if (!mentry)
		return -ENODEV;

	switch (type) {
	case PROBES_INFO_ACTIVE_PROBES:
		param_id = SOF_IPC4_PROBE_POINTS;
		break;
	case PROBES_INFO_AVAILABE_PROBES:
		param_id = SOF_IPC4_PROBE_POINTS_AVAILABLE;
		break;
	default:
		dev_err(dev, "%s: info type %u not supported", __func__, type);
		return -EOPNOTSUPP;
	}

	msg.primary = mentry->id;
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(param_id);

	msg.data_size = sof_client_get_ipc_max_payload_size(cdev);
	msg.data_ptr = kzalloc(msg.data_size, GFP_KERNEL);
	if (!msg.data_ptr)
		return -ENOMEM;

	ret = sof_client_ipc_set_get_data(cdev, &msg, false);
	if (ret) {
		kfree(msg.data_ptr);
		return ret;
	}
	info = msg.data_ptr;
	*num_desc = info->num_elems;
	dev_dbg(dev, "%s: got %zu probe points", __func__, *num_desc);

	*desc = kzalloc(*num_desc * sizeof(**desc), GFP_KERNEL);
	if (!*desc) {
		kfree(msg.data_ptr);
		return -ENOMEM;
	}

	for (i = 0; i < *num_desc; i++) {
		(*desc)[i].buffer_id = info->points[i].point_id;
		(*desc)[i].purpose = info->points[i].purpose;
		(*desc)[i].stream_tag = info->points[i].stream_tag;
	}
	kfree(msg.data_ptr);

	return 0;
}

/**
 * ipc4_probes_point_print - Human readable print of probe point descriptor
 * @cdev:	SOF client device
 * @buf:	Buffer to print to
 * @size:	Available bytes in buffer
 * @desc:	Describes the probe point to print
 * @return:	Number of bytes printed or an error code (snprintf return value)
 */
static int ipc4_probes_point_print(struct sof_client_dev *cdev, char *buf, size_t size,
				   struct sof_probe_point_desc *desc)
{
	struct device *dev = &cdev->auxdev.dev;
	struct snd_sof_widget *swidget;
	int ret;

	swidget = sof_client_ipc4_find_swidget_by_id(cdev, SOF_IPC4_MOD_ID_GET(desc->buffer_id),
						     SOF_IPC4_MOD_INSTANCE_GET(desc->buffer_id));
	if (!swidget)
		dev_err(dev, "%s: Failed to find widget for module %lu.%lu\n",
			__func__, SOF_IPC4_MOD_ID_GET(desc->buffer_id),
			SOF_IPC4_MOD_INSTANCE_GET(desc->buffer_id));

	ret = snprintf(buf, size, "%#x,%#x,%#x\t%s %s buf idx %lu %s\n",
		       desc->buffer_id, desc->purpose, desc->stream_tag,
		       swidget ? swidget->widget->name : "<unknown>",
		       sof_probe_ipc4_type_string(SOF_IPC4_PROBE_TYPE_GET(desc->buffer_id)),
		       SOF_IPC4_PROBE_IDX_GET(desc->buffer_id),
		       desc->stream_tag ? "(connected)" : "");

	return ret;
}

/**
 * ipc4_probes_points_add - connect specified probes
 * @cdev:	SOF client device
 * @desc:	List of probe points to connect
 * @num_desc:	Number of elements in @desc
 * @return:	0 on success, negative error code on error
 *
 * Translates the generic probe point presentation to an IPC4
 * message to dynamically connect the provided set of endpoints.
 */
static int ipc4_probes_points_add(struct sof_client_dev *cdev,
				  struct sof_probe_point_desc *desc,
				  size_t num_desc)
{
	struct sof_man4_module *mentry = sof_ipc4_probe_get_module_info(cdev);
	struct sof_ipc4_probe_point *points;
	struct sof_ipc4_msg msg;
	int i, ret;

	if (!mentry)
		return -EOPNOTSUPP;

	/* The sof_probe_point_desc and sof_ipc4_probe_point structs
	 * are of same size and even the integers are the same in the
	 * same order, and similar meaning, but since there is no
	 * performance issue I wrote the conversion explicitly open for
	 * future development.
	 */
	points = kcalloc(num_desc, sizeof(*points), GFP_KERNEL);
	if (!points)
		return -ENOMEM;

	for (i = 0; i < num_desc; i++) {
		points[i].point_id = desc[i].buffer_id;
		points[i].purpose = desc[i].purpose;
		points[i].stream_tag = desc[i].stream_tag;
	}

	msg.primary = mentry->id;
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_PROBE_POINTS);

	msg.data_size = sizeof(*points) * num_desc;
	msg.data_ptr = points;

	ret = sof_client_ipc_set_get_data(cdev, &msg, true);

	kfree(points);

	return ret;
}

/**
 * ipc4_probes_points_remove - disconnect specified probes
 * @cdev:		SOF client device
 * @buffer_id:		List of probe points to disconnect
 * @num_buffer_id:	Number of elements in @desc
 * @return:		0 on success, negative error code on error
 *
 * Converts the generic buffer_id to IPC4 probe_point_id and remove
 * the probe points with an IPC4 for message.
 */
static int ipc4_probes_points_remove(struct sof_client_dev *cdev,
				     unsigned int *buffer_id, size_t num_buffer_id)
{
	struct sof_man4_module *mentry = sof_ipc4_probe_get_module_info(cdev);
	struct sof_ipc4_msg msg;
	u32 *probe_point_ids;
	int i, ret;

	if (!mentry)
		return -ENODEV;

	probe_point_ids = kcalloc(num_buffer_id, sizeof(*probe_point_ids),
				  GFP_KERNEL);
	if (!probe_point_ids)
		return -ENOMEM;

	for (i = 0; i < num_buffer_id; i++)
		probe_point_ids[i] = buffer_id[i];

	msg.primary = mentry->id;
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg.extension =
		SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_PROBE_POINTS_DISCONNECT);

	msg.data_size = num_buffer_id * sizeof(*probe_point_ids);
	msg.data_ptr = probe_point_ids;

	ret = sof_client_ipc_set_get_data(cdev, &msg, true);

	kfree(probe_point_ids);

	return ret;
}

const struct sof_probes_ipc_ops ipc4_probe_ops =  {
	.init = ipc4_probes_init,
	.deinit = ipc4_probes_deinit,
	.points_info = ipc4_probes_points_info,
	.point_print = ipc4_probes_point_print,
	.points_add = ipc4_probes_points_add,
	.points_remove = ipc4_probes_points_remove,
};
