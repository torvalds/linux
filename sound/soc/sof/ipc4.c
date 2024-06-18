// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation
//
// Authors: Rander Wang <rander.wang@linux.intel.com>
//	    Peter Ujfalusi <peter.ujfalusi@linux.intel.com>
//
#include <linux/firmware.h>
#include <sound/sof/header.h>
#include <sound/sof/ipc4/header.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4-fw-reg.h"
#include "ipc4-priv.h"
#include "ipc4-telemetry.h"
#include "ops.h"

static const struct sof_ipc4_fw_status {
	int status;
	char *msg;
} ipc4_status[] = {
	{0, "The operation was successful"},
	{1, "Invalid parameter specified"},
	{2, "Unknown message type specified"},
	{3, "Not enough space in the IPC reply buffer to complete the request"},
	{4, "The system or resource is busy"},
	{5, "Replaced ADSP IPC PENDING (unused)"},
	{6, "Unknown error while processing the request"},
	{7, "Unsupported operation requested"},
	{8, "Reserved (ADSP_STAGE_UNINITIALIZED removed)"},
	{9, "Specified resource not found"},
	{10, "A resource's ID requested to be created is already assigned"},
	{11, "Reserved (ADSP_IPC_OUT_OF_MIPS removed)"},
	{12, "Required resource is in invalid state"},
	{13, "Requested power transition failed to complete"},
	{14, "Manifest of the library being loaded is invalid"},
	{15, "Requested service or data is unavailable on the target platform"},
	{42, "Library target address is out of storage memory range"},
	{43, "Reserved"},
	{44, "Image verification by CSE failed"},
	{100, "General module management error"},
	{101, "Module loading failed"},
	{102, "Integrity check of the loaded module content failed"},
	{103, "Attempt to unload code of the module in use"},
	{104, "Other failure of module instance initialization request"},
	{105, "Reserved (ADSP_IPC_OUT_OF_MIPS removed)"},
	{106, "Reserved (ADSP_IPC_CONFIG_GET_ERROR removed)"},
	{107, "Reserved (ADSP_IPC_CONFIG_SET_ERROR removed)"},
	{108, "Reserved (ADSP_IPC_LARGE_CONFIG_GET_ERROR removed)"},
	{109, "Reserved (ADSP_IPC_LARGE_CONFIG_SET_ERROR removed)"},
	{110, "Invalid (out of range) module ID provided"},
	{111, "Invalid module instance ID provided"},
	{112, "Invalid queue (pin) ID provided"},
	{113, "Invalid destination queue (pin) ID provided"},
	{114, "Reserved (ADSP_IPC_BIND_UNBIND_DST_SINK_UNSUPPORTED removed)"},
	{115, "Reserved (ADSP_IPC_UNLOAD_INST_EXISTS removed)"},
	{116, "Invalid target code ID provided"},
	{117, "Injection DMA buffer is too small for probing the input pin"},
	{118, "Extraction DMA buffer is too small for probing the output pin"},
	{120, "Invalid ID of configuration item provided in TLV list"},
	{121, "Invalid length of configuration item provided in TLV list"},
	{122, "Invalid structure of configuration item provided"},
	{140, "Initialization of DMA Gateway failed"},
	{141, "Invalid ID of gateway provided"},
	{142, "Setting state of DMA Gateway failed"},
	{143, "DMA_CONTROL message targeting gateway not allocated yet"},
	{150, "Attempt to configure SCLK while I2S port is running"},
	{151, "Attempt to configure MCLK while I2S port is running"},
	{152, "Attempt to stop SCLK that is not running"},
	{153, "Attempt to stop MCLK that is not running"},
	{160, "Reserved (ADSP_IPC_PIPELINE_NOT_INITIALIZED removed)"},
	{161, "Reserved (ADSP_IPC_PIPELINE_NOT_EXIST removed)"},
	{162, "Reserved (ADSP_IPC_PIPELINE_SAVE_FAILED removed)"},
	{163, "Reserved (ADSP_IPC_PIPELINE_RESTORE_FAILED removed)"},
	{165, "Reserved (ADSP_IPC_PIPELINE_ALREADY_EXISTS removed)"},
};

typedef void (*ipc4_notification_handler)(struct snd_sof_dev *sdev,
					  struct sof_ipc4_msg *msg);

static int sof_ipc4_check_reply_status(struct snd_sof_dev *sdev, u32 status)
{
	int i, ret;

	status &= SOF_IPC4_REPLY_STATUS;

	if (!status)
		return 0;

	for (i = 0; i < ARRAY_SIZE(ipc4_status); i++) {
		if (ipc4_status[i].status == status) {
			dev_err(sdev->dev, "FW reported error: %u - %s\n",
				status, ipc4_status[i].msg);
			goto to_errno;
		}
	}

	if (i == ARRAY_SIZE(ipc4_status))
		dev_err(sdev->dev, "FW reported error: %u - Unknown\n", status);

to_errno:
	switch (status) {
	case 2:
	case 15:
		ret = -EOPNOTSUPP;
		break;
	case 8:
	case 11:
	case 105 ... 109:
	case 114 ... 115:
	case 160 ... 163:
	case 165:
		ret = -ENOENT;
		break;
	case 4:
	case 150:
	case 151:
		ret = -EBUSY;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC)
#define DBG_IPC4_MSG_TYPE_ENTRY(type)	[SOF_IPC4_##type] = #type
static const char * const ipc4_dbg_mod_msg_type[] = {
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_INIT_INSTANCE),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_CONFIG_GET),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_CONFIG_SET),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_LARGE_CONFIG_GET),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_LARGE_CONFIG_SET),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_BIND),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_UNBIND),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_SET_DX),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_SET_D0IX),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_ENTER_MODULE_RESTORE),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_EXIT_MODULE_RESTORE),
	DBG_IPC4_MSG_TYPE_ENTRY(MOD_DELETE_INSTANCE),
};

static const char * const ipc4_dbg_glb_msg_type[] = {
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_BOOT_CONFIG),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_ROM_CONTROL),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_IPCGATEWAY_CMD),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_PERF_MEASUREMENTS_CMD),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_CHAIN_DMA),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_LOAD_MULTIPLE_MODULES),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_UNLOAD_MULTIPLE_MODULES),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_CREATE_PIPELINE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_DELETE_PIPELINE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_SET_PIPELINE_STATE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_GET_PIPELINE_STATE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_GET_PIPELINE_CONTEXT_SIZE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_SAVE_PIPELINE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_RESTORE_PIPELINE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_LOAD_LIBRARY),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_LOAD_LIBRARY_PREPARE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_INTERNAL_MESSAGE),
	DBG_IPC4_MSG_TYPE_ENTRY(GLB_NOTIFICATION),
};

#define DBG_IPC4_NOTIFICATION_TYPE_ENTRY(type)	[SOF_IPC4_NOTIFY_##type] = #type
static const char * const ipc4_dbg_notification_type[] = {
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(PHRASE_DETECTED),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(RESOURCE_EVENT),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(LOG_BUFFER_STATUS),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(TIMESTAMP_CAPTURED),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(FW_READY),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(FW_AUD_CLASS_RESULT),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(EXCEPTION_CAUGHT),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(MODULE_NOTIFICATION),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(PROBE_DATA_AVAILABLE),
	DBG_IPC4_NOTIFICATION_TYPE_ENTRY(ASYNC_MSG_SRVC_MESSAGE),
};

static void sof_ipc4_log_header(struct device *dev, u8 *text, struct sof_ipc4_msg *msg,
				bool data_size_valid)
{
	u32 val, type;
	const u8 *str2 = NULL;
	const u8 *str = NULL;

	val = msg->primary & SOF_IPC4_MSG_TARGET_MASK;
	type = SOF_IPC4_MSG_TYPE_GET(msg->primary);

	if (val == SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG)) {
		/* Module message */
		if (type < SOF_IPC4_MOD_TYPE_LAST)
			str = ipc4_dbg_mod_msg_type[type];
		if (!str)
			str = "Unknown Module message type";
	} else {
		/* Global FW message */
		if (type < SOF_IPC4_GLB_TYPE_LAST)
			str = ipc4_dbg_glb_msg_type[type];
		if (!str)
			str = "Unknown Global message type";

		if (type == SOF_IPC4_GLB_NOTIFICATION) {
			/* Notification message */
			u32 notif = SOF_IPC4_NOTIFICATION_TYPE_GET(msg->primary);

			/* Do not print log buffer notification if not desired */
			if (notif == SOF_IPC4_NOTIFY_LOG_BUFFER_STATUS &&
			    !sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS))
				return;

			if (notif < SOF_IPC4_NOTIFY_TYPE_LAST)
				str2 = ipc4_dbg_notification_type[notif];
			if (!str2)
				str2 = "Unknown Global notification";
		}
	}

	if (str2) {
		if (data_size_valid && msg->data_size)
			dev_dbg(dev, "%s: %#x|%#x: %s|%s [data size: %zu]\n",
				text, msg->primary, msg->extension, str, str2,
				msg->data_size);
		else
			dev_dbg(dev, "%s: %#x|%#x: %s|%s\n", text, msg->primary,
				msg->extension, str, str2);
	} else {
		if (data_size_valid && msg->data_size)
			dev_dbg(dev, "%s: %#x|%#x: %s [data size: %zu]\n",
				text, msg->primary, msg->extension, str,
				msg->data_size);
		else
			dev_dbg(dev, "%s: %#x|%#x: %s\n", text, msg->primary,
				msg->extension, str);
	}
}
#else /* CONFIG_SND_SOC_SOF_DEBUG_VERBOSE_IPC */
static void sof_ipc4_log_header(struct device *dev, u8 *text, struct sof_ipc4_msg *msg,
				bool data_size_valid)
{
	/* Do not print log buffer notification if not desired */
	if (!sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS) &&
	    !SOF_IPC4_MSG_IS_MODULE_MSG(msg->primary) &&
	    SOF_IPC4_MSG_TYPE_GET(msg->primary) == SOF_IPC4_GLB_NOTIFICATION &&
	    SOF_IPC4_NOTIFICATION_TYPE_GET(msg->primary) == SOF_IPC4_NOTIFY_LOG_BUFFER_STATUS)
		return;

	if (data_size_valid && msg->data_size)
		dev_dbg(dev, "%s: %#x|%#x [data size: %zu]\n", text,
			msg->primary, msg->extension, msg->data_size);
	else
		dev_dbg(dev, "%s: %#x|%#x\n", text, msg->primary, msg->extension);
}
#endif

static void sof_ipc4_dump_payload(struct snd_sof_dev *sdev,
				  void *ipc_data, size_t size)
{
	print_hex_dump_debug("Message payload: ", DUMP_PREFIX_OFFSET,
			     16, 4, ipc_data, size, false);
}

static int sof_ipc4_get_reply(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc_msg *msg = sdev->msg;
	struct sof_ipc4_msg *ipc4_reply;
	int ret;

	/* get the generic reply */
	ipc4_reply = msg->reply_data;

	sof_ipc4_log_header(sdev->dev, "ipc tx reply", ipc4_reply, false);

	ret = sof_ipc4_check_reply_status(sdev, ipc4_reply->primary);
	if (ret)
		return ret;

	/* No other information is expected for non large config get replies */
	if (!msg->reply_size || !SOF_IPC4_MSG_IS_MODULE_MSG(ipc4_reply->primary) ||
	    (SOF_IPC4_MSG_TYPE_GET(ipc4_reply->primary) != SOF_IPC4_MOD_LARGE_CONFIG_GET))
		return 0;

	/* Read the requested payload */
	snd_sof_dsp_mailbox_read(sdev, sdev->dsp_box.offset, ipc4_reply->data_ptr,
				 msg->reply_size);

	return 0;
}

/* wait for IPC message reply */
static int ipc4_wait_tx_done(struct snd_sof_ipc *ipc, void *reply_data)
{
	struct snd_sof_ipc_msg *msg = &ipc->msg;
	struct sof_ipc4_msg *ipc4_msg = msg->msg_data;
	struct snd_sof_dev *sdev = ipc->sdev;
	int ret;

	/* wait for DSP IPC completion */
	ret = wait_event_timeout(msg->waitq, msg->ipc_complete,
				 msecs_to_jiffies(sdev->ipc_timeout));
	if (ret == 0) {
		dev_err(sdev->dev, "ipc timed out for %#x|%#x\n",
			ipc4_msg->primary, ipc4_msg->extension);
		snd_sof_handle_fw_exception(ipc->sdev, "IPC timeout");
		return -ETIMEDOUT;
	}

	if (msg->reply_error) {
		dev_err(sdev->dev, "ipc error for msg %#x|%#x\n",
			ipc4_msg->primary, ipc4_msg->extension);
		ret =  msg->reply_error;
	} else {
		if (reply_data) {
			struct sof_ipc4_msg *ipc4_reply = msg->reply_data;
			struct sof_ipc4_msg *ipc4_reply_data = reply_data;

			/* Copy the header */
			ipc4_reply_data->header_u64 = ipc4_reply->header_u64;
			if (msg->reply_size && ipc4_reply_data->data_ptr) {
				/* copy the payload returned from DSP */
				memcpy(ipc4_reply_data->data_ptr, ipc4_reply->data_ptr,
				       msg->reply_size);
				ipc4_reply_data->data_size = msg->reply_size;
			}
		}

		ret = 0;
		sof_ipc4_log_header(sdev->dev, "ipc tx done ", ipc4_msg, true);
	}

	/* re-enable dumps after successful IPC tx */
	if (sdev->ipc_dump_printed) {
		sdev->dbg_dump_printed = false;
		sdev->ipc_dump_printed = false;
	}

	return ret;
}

static int ipc4_tx_msg_unlocked(struct snd_sof_ipc *ipc,
				void *msg_data, size_t msg_bytes,
				void *reply_data, size_t reply_bytes)
{
	struct sof_ipc4_msg *ipc4_msg = msg_data;
	struct snd_sof_dev *sdev = ipc->sdev;
	int ret;

	if (msg_bytes > ipc->max_payload_size || reply_bytes > ipc->max_payload_size)
		return -EINVAL;

	sof_ipc4_log_header(sdev->dev, "ipc tx      ", msg_data, true);

	ret = sof_ipc_send_msg(sdev, msg_data, msg_bytes, reply_bytes);
	if (ret) {
		dev_err_ratelimited(sdev->dev,
				    "%s: ipc message send for %#x|%#x failed: %d\n",
				    __func__, ipc4_msg->primary, ipc4_msg->extension, ret);
		return ret;
	}

	/* now wait for completion */
	return ipc4_wait_tx_done(ipc, reply_data);
}

static int sof_ipc4_tx_msg(struct snd_sof_dev *sdev, void *msg_data, size_t msg_bytes,
			   void *reply_data, size_t reply_bytes, bool no_pm)
{
	struct snd_sof_ipc *ipc = sdev->ipc;
	int ret;

	if (!msg_data)
		return -EINVAL;

	if (!no_pm) {
		const struct sof_dsp_power_state target_state = {
			.state = SOF_DSP_PM_D0,
		};

		/* ensure the DSP is in D0i0 before sending a new IPC */
		ret = snd_sof_dsp_set_power_state(sdev, &target_state);
		if (ret < 0)
			return ret;
	}

	/* Serialise IPC TX */
	mutex_lock(&ipc->tx_mutex);

	ret = ipc4_tx_msg_unlocked(ipc, msg_data, msg_bytes, reply_data, reply_bytes);

	if (sof_debug_check_flag(SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD)) {
		struct sof_ipc4_msg *msg = NULL;

		/* payload is indicated by non zero msg/reply_bytes */
		if (msg_bytes)
			msg = msg_data;
		else if (reply_bytes)
			msg = reply_data;

		if (msg)
			sof_ipc4_dump_payload(sdev, msg->data_ptr, msg->data_size);
	}

	mutex_unlock(&ipc->tx_mutex);

	return ret;
}

static int sof_ipc4_set_get_data(struct snd_sof_dev *sdev, void *data,
				 size_t payload_bytes, bool set)
{
	const struct sof_dsp_power_state target_state = {
			.state = SOF_DSP_PM_D0,
	};
	size_t payload_limit = sdev->ipc->max_payload_size;
	struct sof_ipc4_msg *ipc4_msg = data;
	struct sof_ipc4_msg tx = {{ 0 }};
	struct sof_ipc4_msg rx = {{ 0 }};
	size_t remaining = payload_bytes;
	size_t offset = 0;
	size_t chunk_size;
	int ret;

	if (!data)
		return -EINVAL;

	if ((ipc4_msg->primary & SOF_IPC4_MSG_TARGET_MASK) !=
	    SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG))
		return -EINVAL;

	ipc4_msg->primary &= ~SOF_IPC4_MSG_TYPE_MASK;
	tx.primary = ipc4_msg->primary;
	tx.extension = ipc4_msg->extension;

	if (set)
		tx.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_LARGE_CONFIG_SET);
	else
		tx.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_LARGE_CONFIG_GET);

	tx.extension &= ~SOF_IPC4_MOD_EXT_MSG_SIZE_MASK;
	tx.extension |= SOF_IPC4_MOD_EXT_MSG_SIZE(payload_bytes);

	tx.extension |= SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK(1);

	/* ensure the DSP is in D0i0 before sending IPC */
	ret = snd_sof_dsp_set_power_state(sdev, &target_state);
	if (ret < 0)
		return ret;

	/* Serialise IPC TX */
	mutex_lock(&sdev->ipc->tx_mutex);

	do {
		size_t tx_size, rx_size;

		if (remaining > payload_limit) {
			chunk_size = payload_limit;
		} else {
			chunk_size = remaining;
			if (set)
				tx.extension |= SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK(1);
		}

		if (offset) {
			tx.extension &= ~SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_MASK;
			tx.extension &= ~SOF_IPC4_MOD_EXT_MSG_SIZE_MASK;
			tx.extension |= SOF_IPC4_MOD_EXT_MSG_SIZE(offset);
		}

		if (set) {
			tx.data_size = chunk_size;
			tx.data_ptr = ipc4_msg->data_ptr + offset;

			tx_size = chunk_size;
			rx_size = 0;
		} else {
			rx.primary = 0;
			rx.extension = 0;
			rx.data_size = chunk_size;
			rx.data_ptr = ipc4_msg->data_ptr + offset;

			tx_size = 0;
			rx_size = chunk_size;
		}

		/* Send the message for the current chunk */
		ret = ipc4_tx_msg_unlocked(sdev->ipc, &tx, tx_size, &rx, rx_size);
		if (ret < 0) {
			dev_err(sdev->dev,
				"%s: large config %s failed at offset %zu: %d\n",
				__func__, set ? "set" : "get", offset, ret);
			goto out;
		}

		if (!set && rx.extension & SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_MASK) {
			/* Verify the firmware reported total payload size */
			rx_size = rx.extension & SOF_IPC4_MOD_EXT_MSG_SIZE_MASK;

			if (rx_size > payload_bytes) {
				dev_err(sdev->dev,
					"%s: Receive buffer (%zu) is too small for %zu\n",
					__func__, payload_bytes, rx_size);
				ret = -ENOMEM;
				goto out;
			}

			if (rx_size < chunk_size) {
				chunk_size = rx_size;
				remaining = rx_size;
			} else if (rx_size < payload_bytes) {
				remaining = rx_size;
			}
		}

		offset += chunk_size;
		remaining -= chunk_size;
	} while (remaining);

	/* Adjust the received data size if needed */
	if (!set && payload_bytes != offset)
		ipc4_msg->data_size = offset;

out:
	if (sof_debug_check_flag(SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD))
		sof_ipc4_dump_payload(sdev, ipc4_msg->data_ptr, ipc4_msg->data_size);

	mutex_unlock(&sdev->ipc->tx_mutex);

	return ret;
}

static int sof_ipc4_init_msg_memory(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_msg *ipc4_msg;
	struct snd_sof_ipc_msg *msg = &sdev->ipc->msg;

	/* TODO: get max_payload_size from firmware */
	sdev->ipc->max_payload_size = SOF_IPC4_MSG_MAX_SIZE;

	/* Allocate memory for the ipc4 container and the maximum payload */
	msg->reply_data = devm_kzalloc(sdev->dev, sdev->ipc->max_payload_size +
				       sizeof(struct sof_ipc4_msg), GFP_KERNEL);
	if (!msg->reply_data)
		return -ENOMEM;

	ipc4_msg = msg->reply_data;
	ipc4_msg->data_ptr = msg->reply_data + sizeof(struct sof_ipc4_msg);

	return 0;
}

size_t sof_ipc4_find_debug_slot_offset_by_type(struct snd_sof_dev *sdev,
					       u32 slot_type)
{
	size_t slot_desc_type_offset;
	u32 type;
	int i;

	/* The type is the second u32 in the slot descriptor */
	slot_desc_type_offset = sdev->debug_box.offset + sizeof(u32);
	for (i = 0; i < SOF_IPC4_MAX_DEBUG_SLOTS; i++) {
		sof_mailbox_read(sdev, slot_desc_type_offset, &type, sizeof(type));

		if (type == slot_type)
			return sdev->debug_box.offset + (i + 1) * SOF_IPC4_DEBUG_SLOT_SIZE;

		slot_desc_type_offset += SOF_IPC4_DEBUG_DESCRIPTOR_SIZE;
	}

	dev_dbg(sdev->dev, "Slot type %#x is not available in debug window\n", slot_type);
	return 0;
}
EXPORT_SYMBOL(sof_ipc4_find_debug_slot_offset_by_type);

static int ipc4_fw_ready(struct snd_sof_dev *sdev, struct sof_ipc4_msg *ipc4_msg)
{
	/* no need to re-check version/ABI for subsequent boots */
	if (!sdev->first_boot)
		return 0;

	sof_ipc4_create_exception_debugfs_node(sdev);

	return sof_ipc4_init_msg_memory(sdev);
}

static void sof_ipc4_module_notification_handler(struct snd_sof_dev *sdev,
						 struct sof_ipc4_msg *ipc4_msg)
{
	struct sof_ipc4_notify_module_data *data = ipc4_msg->data_ptr;

	/*
	 * If the notification includes additional, module specific data, then
	 * we need to re-allocate the buffer and re-read the whole payload,
	 * including the event_data
	 */
	if (data->event_data_size) {
		void *new;
		int ret;

		ipc4_msg->data_size += data->event_data_size;

		new = krealloc(ipc4_msg->data_ptr, ipc4_msg->data_size, GFP_KERNEL);
		if (!new) {
			ipc4_msg->data_size -= data->event_data_size;
			return;
		}

		/* re-read the whole payload */
		ipc4_msg->data_ptr = new;
		ret = snd_sof_ipc_msg_data(sdev, NULL, ipc4_msg->data_ptr,
					   ipc4_msg->data_size);
		if (ret < 0) {
			dev_err(sdev->dev,
				"Failed to read the full module notification: %d\n",
				ret);
			return;
		}
		data = ipc4_msg->data_ptr;
	}

	/* Handle ALSA kcontrol notification */
	if ((data->event_id & SOF_IPC4_NOTIFY_MODULE_EVENTID_ALSA_MAGIC_MASK) ==
	    SOF_IPC4_NOTIFY_MODULE_EVENTID_ALSA_MAGIC_VAL) {
		const struct sof_ipc_tplg_ops *tplg_ops = sdev->ipc->ops->tplg;

		if (tplg_ops->control->update)
			tplg_ops->control->update(sdev, ipc4_msg);
	}
}

static void sof_ipc4_rx_msg(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_msg *ipc4_msg = sdev->ipc->msg.rx_data;
	ipc4_notification_handler handler_func = NULL;
	size_t data_size = 0;
	int err;

	if (!ipc4_msg || !SOF_IPC4_MSG_IS_NOTIFICATION(ipc4_msg->primary))
		return;

	ipc4_msg->data_ptr = NULL;
	ipc4_msg->data_size = 0;

	sof_ipc4_log_header(sdev->dev, "ipc rx      ", ipc4_msg, false);

	switch (SOF_IPC4_NOTIFICATION_TYPE_GET(ipc4_msg->primary)) {
	case SOF_IPC4_NOTIFY_FW_READY:
		/* check for FW boot completion */
		if (sdev->fw_state == SOF_FW_BOOT_IN_PROGRESS) {
			err = ipc4_fw_ready(sdev, ipc4_msg);
			if (err < 0)
				sof_set_fw_state(sdev, SOF_FW_BOOT_READY_FAILED);
			else
				sof_set_fw_state(sdev, SOF_FW_BOOT_READY_OK);

			/* wake up firmware loader */
			wake_up(&sdev->boot_wait);
		}

		break;
	case SOF_IPC4_NOTIFY_RESOURCE_EVENT:
		data_size = sizeof(struct sof_ipc4_notify_resource_data);
		break;
	case SOF_IPC4_NOTIFY_LOG_BUFFER_STATUS:
		sof_ipc4_mtrace_update_pos(sdev, SOF_IPC4_LOG_CORE_GET(ipc4_msg->primary));
		break;
	case SOF_IPC4_NOTIFY_EXCEPTION_CAUGHT:
		snd_sof_dsp_panic(sdev, 0, true);
		break;
	case SOF_IPC4_NOTIFY_MODULE_NOTIFICATION:
		data_size = sizeof(struct sof_ipc4_notify_module_data);
		handler_func = sof_ipc4_module_notification_handler;
		break;
	default:
		dev_dbg(sdev->dev, "Unhandled DSP message: %#x|%#x\n",
			ipc4_msg->primary, ipc4_msg->extension);
		break;
	}

	if (data_size) {
		ipc4_msg->data_ptr = kmalloc(data_size, GFP_KERNEL);
		if (!ipc4_msg->data_ptr)
			return;

		ipc4_msg->data_size = data_size;
		err = snd_sof_ipc_msg_data(sdev, NULL, ipc4_msg->data_ptr, ipc4_msg->data_size);
		if (err < 0) {
			dev_err(sdev->dev, "failed to read IPC notification data: %d\n", err);
			kfree(ipc4_msg->data_ptr);
			ipc4_msg->data_ptr = NULL;
			ipc4_msg->data_size = 0;
			return;
		}
	}

	/* Handle notifications with payload */
	if (handler_func)
		handler_func(sdev, ipc4_msg);

	sof_ipc4_log_header(sdev->dev, "ipc rx done ", ipc4_msg, true);

	if (data_size) {
		if (sof_debug_check_flag(SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD))
			sof_ipc4_dump_payload(sdev, ipc4_msg->data_ptr,
					      ipc4_msg->data_size);

		kfree(ipc4_msg->data_ptr);
		ipc4_msg->data_ptr = NULL;
		ipc4_msg->data_size = 0;
	}
}

static int sof_ipc4_set_core_state(struct snd_sof_dev *sdev, int core_idx, bool on)
{
	struct sof_ipc4_dx_state_info dx_state;
	struct sof_ipc4_msg msg;

	dx_state.core_mask = BIT(core_idx);
	if (on)
		dx_state.dx_mask = BIT(core_idx);
	else
		dx_state.dx_mask = 0;

	msg.primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_SET_DX);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.extension = 0;
	msg.data_ptr = &dx_state;
	msg.data_size = sizeof(dx_state);

	return sof_ipc4_tx_msg(sdev, &msg, msg.data_size, NULL, 0, false);
}

/*
 * The context save callback is used to send a message to the firmware notifying
 * it that the primary core is going to be turned off, which is used as an
 * indication to prepare for a full power down, thus preparing for IMR boot
 * (when supported)
 *
 * Note: in IPC4 there is no message used to restore context, thus no context
 * restore callback is implemented
 */
static int sof_ipc4_ctx_save(struct snd_sof_dev *sdev)
{
	return sof_ipc4_set_core_state(sdev, SOF_DSP_PRIMARY_CORE, false);
}

static int sof_ipc4_set_pm_gate(struct snd_sof_dev *sdev, u32 flags)
{
	struct sof_ipc4_msg msg = {{0}};

	msg.primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_SET_D0IX);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.extension = flags;

	return sof_ipc4_tx_msg(sdev, &msg, 0, NULL, 0, true);
}

static const struct sof_ipc_pm_ops ipc4_pm_ops = {
	.ctx_save = sof_ipc4_ctx_save,
	.set_core_state = sof_ipc4_set_core_state,
	.set_pm_gate = sof_ipc4_set_pm_gate,
};

static int sof_ipc4_init(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	int inbox_offset;

	mutex_init(&ipc4_data->pipeline_state_mutex);

	xa_init_flags(&ipc4_data->fw_lib_xa, XA_FLAGS_ALLOC);

	/* Set up the windows for IPC communication */
	inbox_offset = snd_sof_dsp_get_mailbox_offset(sdev);
	if (inbox_offset < 0) {
		dev_err(sdev->dev, "%s: No mailbox offset\n", __func__);
		return inbox_offset;
	}

	sdev->dsp_box.offset = inbox_offset;
	sdev->dsp_box.size = SOF_IPC4_MSG_MAX_SIZE;
	sdev->host_box.offset = snd_sof_dsp_get_window_offset(sdev,
							SOF_IPC4_OUTBOX_WINDOW_IDX);
	sdev->host_box.size = SOF_IPC4_MSG_MAX_SIZE;

	sdev->debug_box.offset = snd_sof_dsp_get_window_offset(sdev,
							SOF_IPC4_DEBUG_WINDOW_IDX);

	sdev->fw_info_box.offset = snd_sof_dsp_get_window_offset(sdev,
							SOF_IPC4_INBOX_WINDOW_IDX);
	sdev->fw_info_box.size = sizeof(struct sof_ipc4_fw_registers);

	dev_dbg(sdev->dev, "mailbox upstream %#x - size %#x\n",
		sdev->dsp_box.offset, SOF_IPC4_MSG_MAX_SIZE);
	dev_dbg(sdev->dev, "mailbox downstream %#x - size %#x\n",
		sdev->host_box.offset, SOF_IPC4_MSG_MAX_SIZE);
	dev_dbg(sdev->dev, "debug box %#x\n", sdev->debug_box.offset);

	return 0;
}

static void sof_ipc4_exit(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_fw_library *fw_lib;
	unsigned long lib_id;

	xa_for_each(&ipc4_data->fw_lib_xa, lib_id, fw_lib) {
		/*
		 * The basefw (ID == 0) is handled by generic code, it is not
		 * loaded by IPC4 code.
		 */
		if (lib_id != 0)
			release_firmware(fw_lib->sof_fw.fw);

		fw_lib->sof_fw.fw = NULL;
	}

	xa_destroy(&ipc4_data->fw_lib_xa);
}

static int sof_ipc4_post_boot(struct snd_sof_dev *sdev)
{
	if (sdev->first_boot)
		return sof_ipc4_query_fw_configuration(sdev);

	return sof_ipc4_reload_fw_libraries(sdev);
}

const struct sof_ipc_ops ipc4_ops = {
	.init = sof_ipc4_init,
	.exit = sof_ipc4_exit,
	.post_fw_boot = sof_ipc4_post_boot,
	.tx_msg = sof_ipc4_tx_msg,
	.rx_msg = sof_ipc4_rx_msg,
	.set_get_data = sof_ipc4_set_get_data,
	.get_reply = sof_ipc4_get_reply,
	.pm = &ipc4_pm_ops,
	.fw_loader = &ipc4_loader_ops,
	.tplg = &ipc4_tplg_ops,
	.pcm = &ipc4_pcm_ops,
	.fw_tracing = &ipc4_mtrace_ops,
};
