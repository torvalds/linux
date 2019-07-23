// SPDX-License-Identifier: GPL-2.0-only
/*
 * skl-sst-ipc.c - Intel skl IPC Support
 *
 * Copyright (C) 2014-15, Intel Corporation.
 */
#include <linux/device.h>

#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "skl.h"
#include "skl-sst-dsp.h"
#include "skl-sst-ipc.h"
#include "sound/hdaudio_ext.h"


#define IPC_IXC_STATUS_BITS		24

/* Global Message - Generic */
#define IPC_GLB_TYPE_SHIFT		24
#define IPC_GLB_TYPE_MASK		(0xf << IPC_GLB_TYPE_SHIFT)
#define IPC_GLB_TYPE(x)			((x) << IPC_GLB_TYPE_SHIFT)

/* Global Message - Reply */
#define IPC_GLB_REPLY_STATUS_SHIFT	24
#define IPC_GLB_REPLY_STATUS_MASK	((0x1 << IPC_GLB_REPLY_STATUS_SHIFT) - 1)
#define IPC_GLB_REPLY_STATUS(x)		((x) << IPC_GLB_REPLY_STATUS_SHIFT)

#define IPC_GLB_REPLY_TYPE_SHIFT	29
#define IPC_GLB_REPLY_TYPE_MASK		0x1F
#define IPC_GLB_REPLY_TYPE(x)		(((x) >> IPC_GLB_REPLY_TYPE_SHIFT) \
					& IPC_GLB_RPLY_TYPE_MASK)

#define IPC_TIMEOUT_MSECS		3000

#define IPC_EMPTY_LIST_SIZE		8

#define IPC_MSG_TARGET_SHIFT		30
#define IPC_MSG_TARGET_MASK		0x1
#define IPC_MSG_TARGET(x)		(((x) & IPC_MSG_TARGET_MASK) \
					<< IPC_MSG_TARGET_SHIFT)

#define IPC_MSG_DIR_SHIFT		29
#define IPC_MSG_DIR_MASK		0x1
#define IPC_MSG_DIR(x)			(((x) & IPC_MSG_DIR_MASK) \
					<< IPC_MSG_DIR_SHIFT)
/* Global Notification Message */
#define IPC_GLB_NOTIFY_TYPE_SHIFT	16
#define IPC_GLB_NOTIFY_TYPE_MASK	0xFF
#define IPC_GLB_NOTIFY_TYPE(x)		(((x) >> IPC_GLB_NOTIFY_TYPE_SHIFT) \
					& IPC_GLB_NOTIFY_TYPE_MASK)

#define IPC_GLB_NOTIFY_MSG_TYPE_SHIFT	24
#define IPC_GLB_NOTIFY_MSG_TYPE_MASK	0x1F
#define IPC_GLB_NOTIFY_MSG_TYPE(x)	(((x) >> IPC_GLB_NOTIFY_MSG_TYPE_SHIFT)	\
						& IPC_GLB_NOTIFY_MSG_TYPE_MASK)

#define IPC_GLB_NOTIFY_RSP_SHIFT	29
#define IPC_GLB_NOTIFY_RSP_MASK		0x1
#define IPC_GLB_NOTIFY_RSP_TYPE(x)	(((x) >> IPC_GLB_NOTIFY_RSP_SHIFT) \
					& IPC_GLB_NOTIFY_RSP_MASK)

/* Pipeline operations */

/* Create pipeline message */
#define IPC_PPL_MEM_SIZE_SHIFT		0
#define IPC_PPL_MEM_SIZE_MASK		0x7FF
#define IPC_PPL_MEM_SIZE(x)		(((x) & IPC_PPL_MEM_SIZE_MASK) \
					<< IPC_PPL_MEM_SIZE_SHIFT)

#define IPC_PPL_TYPE_SHIFT		11
#define IPC_PPL_TYPE_MASK		0x1F
#define IPC_PPL_TYPE(x)			(((x) & IPC_PPL_TYPE_MASK) \
					<< IPC_PPL_TYPE_SHIFT)

#define IPC_INSTANCE_ID_SHIFT		16
#define IPC_INSTANCE_ID_MASK		0xFF
#define IPC_INSTANCE_ID(x)		(((x) & IPC_INSTANCE_ID_MASK) \
					<< IPC_INSTANCE_ID_SHIFT)

#define IPC_PPL_LP_MODE_SHIFT           0
#define IPC_PPL_LP_MODE_MASK            0x1
#define IPC_PPL_LP_MODE(x)              (((x) & IPC_PPL_LP_MODE_MASK) \
					<< IPC_PPL_LP_MODE_SHIFT)

/* Set pipeline state message */
#define IPC_PPL_STATE_SHIFT		0
#define IPC_PPL_STATE_MASK		0x1F
#define IPC_PPL_STATE(x)		(((x) & IPC_PPL_STATE_MASK) \
					<< IPC_PPL_STATE_SHIFT)

/* Module operations primary register */
#define IPC_MOD_ID_SHIFT		0
#define IPC_MOD_ID_MASK		0xFFFF
#define IPC_MOD_ID(x)		(((x) & IPC_MOD_ID_MASK) \
					<< IPC_MOD_ID_SHIFT)

#define IPC_MOD_INSTANCE_ID_SHIFT	16
#define IPC_MOD_INSTANCE_ID_MASK	0xFF
#define IPC_MOD_INSTANCE_ID(x)	(((x) & IPC_MOD_INSTANCE_ID_MASK) \
					<< IPC_MOD_INSTANCE_ID_SHIFT)

/* Init instance message extension register */
#define IPC_PARAM_BLOCK_SIZE_SHIFT	0
#define IPC_PARAM_BLOCK_SIZE_MASK	0xFFFF
#define IPC_PARAM_BLOCK_SIZE(x)		(((x) & IPC_PARAM_BLOCK_SIZE_MASK) \
					<< IPC_PARAM_BLOCK_SIZE_SHIFT)

#define IPC_PPL_INSTANCE_ID_SHIFT	16
#define IPC_PPL_INSTANCE_ID_MASK	0xFF
#define IPC_PPL_INSTANCE_ID(x)		(((x) & IPC_PPL_INSTANCE_ID_MASK) \
					<< IPC_PPL_INSTANCE_ID_SHIFT)

#define IPC_CORE_ID_SHIFT		24
#define IPC_CORE_ID_MASK		0x1F
#define IPC_CORE_ID(x)			(((x) & IPC_CORE_ID_MASK) \
					<< IPC_CORE_ID_SHIFT)

#define IPC_DOMAIN_SHIFT                28
#define IPC_DOMAIN_MASK                 0x1
#define IPC_DOMAIN(x)                   (((x) & IPC_DOMAIN_MASK) \
					<< IPC_DOMAIN_SHIFT)

/* Bind/Unbind message extension register */
#define IPC_DST_MOD_ID_SHIFT		0
#define IPC_DST_MOD_ID(x)		(((x) & IPC_MOD_ID_MASK) \
					<< IPC_DST_MOD_ID_SHIFT)

#define IPC_DST_MOD_INSTANCE_ID_SHIFT 16
#define IPC_DST_MOD_INSTANCE_ID(x)	(((x) & IPC_MOD_INSTANCE_ID_MASK) \
					<< IPC_DST_MOD_INSTANCE_ID_SHIFT)

#define IPC_DST_QUEUE_SHIFT		24
#define IPC_DST_QUEUE_MASK		0x7
#define IPC_DST_QUEUE(x)		(((x) & IPC_DST_QUEUE_MASK) \
					<< IPC_DST_QUEUE_SHIFT)

#define IPC_SRC_QUEUE_SHIFT		27
#define IPC_SRC_QUEUE_MASK		0x7
#define IPC_SRC_QUEUE(x)		(((x) & IPC_SRC_QUEUE_MASK) \
					<< IPC_SRC_QUEUE_SHIFT)
/* Load Module count */
#define IPC_LOAD_MODULE_SHIFT		0
#define IPC_LOAD_MODULE_MASK		0xFF
#define IPC_LOAD_MODULE_CNT(x)		(((x) & IPC_LOAD_MODULE_MASK) \
					<< IPC_LOAD_MODULE_SHIFT)

/* Save pipeline messgae extension register */
#define IPC_DMA_ID_SHIFT		0
#define IPC_DMA_ID_MASK			0x1F
#define IPC_DMA_ID(x)			(((x) & IPC_DMA_ID_MASK) \
					<< IPC_DMA_ID_SHIFT)
/* Large Config message extension register */
#define IPC_DATA_OFFSET_SZ_SHIFT	0
#define IPC_DATA_OFFSET_SZ_MASK		0xFFFFF
#define IPC_DATA_OFFSET_SZ(x)		(((x) & IPC_DATA_OFFSET_SZ_MASK) \
					<< IPC_DATA_OFFSET_SZ_SHIFT)
#define IPC_DATA_OFFSET_SZ_CLEAR	~(IPC_DATA_OFFSET_SZ_MASK \
					  << IPC_DATA_OFFSET_SZ_SHIFT)

#define IPC_LARGE_PARAM_ID_SHIFT	20
#define IPC_LARGE_PARAM_ID_MASK		0xFF
#define IPC_LARGE_PARAM_ID(x)		(((x) & IPC_LARGE_PARAM_ID_MASK) \
					<< IPC_LARGE_PARAM_ID_SHIFT)

#define IPC_FINAL_BLOCK_SHIFT		28
#define IPC_FINAL_BLOCK_MASK		0x1
#define IPC_FINAL_BLOCK(x)		(((x) & IPC_FINAL_BLOCK_MASK) \
					<< IPC_FINAL_BLOCK_SHIFT)

#define IPC_INITIAL_BLOCK_SHIFT		29
#define IPC_INITIAL_BLOCK_MASK		0x1
#define IPC_INITIAL_BLOCK(x)		(((x) & IPC_INITIAL_BLOCK_MASK) \
					<< IPC_INITIAL_BLOCK_SHIFT)
#define IPC_INITIAL_BLOCK_CLEAR		~(IPC_INITIAL_BLOCK_MASK \
					  << IPC_INITIAL_BLOCK_SHIFT)
/* Set D0ix IPC extension register */
#define IPC_D0IX_WAKE_SHIFT		0
#define IPC_D0IX_WAKE_MASK		0x1
#define IPC_D0IX_WAKE(x)		(((x) & IPC_D0IX_WAKE_MASK) \
					<< IPC_D0IX_WAKE_SHIFT)

#define IPC_D0IX_STREAMING_SHIFT	1
#define IPC_D0IX_STREAMING_MASK		0x1
#define IPC_D0IX_STREAMING(x)		(((x) & IPC_D0IX_STREAMING_MASK) \
					<< IPC_D0IX_STREAMING_SHIFT)


enum skl_ipc_msg_target {
	IPC_FW_GEN_MSG = 0,
	IPC_MOD_MSG = 1
};

enum skl_ipc_msg_direction {
	IPC_MSG_REQUEST = 0,
	IPC_MSG_REPLY = 1
};

/* Global Message Types */
enum skl_ipc_glb_type {
	IPC_GLB_GET_FW_VERSION = 0, /* Retrieves firmware version */
	IPC_GLB_LOAD_MULTIPLE_MODS = 15,
	IPC_GLB_UNLOAD_MULTIPLE_MODS = 16,
	IPC_GLB_CREATE_PPL = 17,
	IPC_GLB_DELETE_PPL = 18,
	IPC_GLB_SET_PPL_STATE = 19,
	IPC_GLB_GET_PPL_STATE = 20,
	IPC_GLB_GET_PPL_CONTEXT_SIZE = 21,
	IPC_GLB_SAVE_PPL = 22,
	IPC_GLB_RESTORE_PPL = 23,
	IPC_GLB_LOAD_LIBRARY = 24,
	IPC_GLB_NOTIFY = 26,
	IPC_GLB_MAX_IPC_MSG_NUMBER = 31 /* Maximum message number */
};

enum skl_ipc_glb_reply {
	IPC_GLB_REPLY_SUCCESS = 0,

	IPC_GLB_REPLY_UNKNOWN_MSG_TYPE = 1,
	IPC_GLB_REPLY_ERROR_INVALID_PARAM = 2,

	IPC_GLB_REPLY_BUSY = 3,
	IPC_GLB_REPLY_PENDING = 4,
	IPC_GLB_REPLY_FAILURE = 5,
	IPC_GLB_REPLY_INVALID_REQUEST = 6,

	IPC_GLB_REPLY_OUT_OF_MEMORY = 7,
	IPC_GLB_REPLY_OUT_OF_MIPS = 8,

	IPC_GLB_REPLY_INVALID_RESOURCE_ID = 9,
	IPC_GLB_REPLY_INVALID_RESOURCE_STATE = 10,

	IPC_GLB_REPLY_MOD_MGMT_ERROR = 100,
	IPC_GLB_REPLY_MOD_LOAD_CL_FAILED = 101,
	IPC_GLB_REPLY_MOD_LOAD_INVALID_HASH = 102,

	IPC_GLB_REPLY_MOD_UNLOAD_INST_EXIST = 103,
	IPC_GLB_REPLY_MOD_NOT_INITIALIZED = 104,

	IPC_GLB_REPLY_INVALID_CONFIG_PARAM_ID = 120,
	IPC_GLB_REPLY_INVALID_CONFIG_DATA_LEN = 121,
	IPC_GLB_REPLY_GATEWAY_NOT_INITIALIZED = 140,
	IPC_GLB_REPLY_GATEWAY_NOT_EXIST = 141,
	IPC_GLB_REPLY_SCLK_ALREADY_RUNNING = 150,
	IPC_GLB_REPLY_MCLK_ALREADY_RUNNING = 151,

	IPC_GLB_REPLY_PPL_NOT_INITIALIZED = 160,
	IPC_GLB_REPLY_PPL_NOT_EXIST = 161,
	IPC_GLB_REPLY_PPL_SAVE_FAILED = 162,
	IPC_GLB_REPLY_PPL_RESTORE_FAILED = 163,

	IPC_MAX_STATUS = ((1<<IPC_IXC_STATUS_BITS)-1)
};

enum skl_ipc_notification_type {
	IPC_GLB_NOTIFY_GLITCH = 0,
	IPC_GLB_NOTIFY_OVERRUN = 1,
	IPC_GLB_NOTIFY_UNDERRUN = 2,
	IPC_GLB_NOTIFY_END_STREAM = 3,
	IPC_GLB_NOTIFY_PHRASE_DETECTED = 4,
	IPC_GLB_NOTIFY_RESOURCE_EVENT = 5,
	IPC_GLB_NOTIFY_LOG_BUFFER_STATUS = 6,
	IPC_GLB_NOTIFY_TIMESTAMP_CAPTURED = 7,
	IPC_GLB_NOTIFY_FW_READY = 8
};

/* Module Message Types */
enum skl_ipc_module_msg {
	IPC_MOD_INIT_INSTANCE = 0,
	IPC_MOD_CONFIG_GET = 1,
	IPC_MOD_CONFIG_SET = 2,
	IPC_MOD_LARGE_CONFIG_GET = 3,
	IPC_MOD_LARGE_CONFIG_SET = 4,
	IPC_MOD_BIND = 5,
	IPC_MOD_UNBIND = 6,
	IPC_MOD_SET_DX = 7,
	IPC_MOD_SET_D0IX = 8
};

void skl_ipc_tx_data_copy(struct ipc_message *msg, char *tx_data,
		size_t tx_size)
{
	if (tx_size)
		memcpy(msg->tx_data, tx_data, tx_size);
}

static bool skl_ipc_is_dsp_busy(struct sst_dsp *dsp)
{
	u32 hipci;

	hipci = sst_dsp_shim_read_unlocked(dsp, SKL_ADSP_REG_HIPCI);
	return (hipci & SKL_ADSP_REG_HIPCI_BUSY);
}

/* Lock to be held by caller */
static void skl_ipc_tx_msg(struct sst_generic_ipc *ipc, struct ipc_message *msg)
{
	struct skl_ipc_header *header = (struct skl_ipc_header *)(&msg->header);

	if (msg->tx_size)
		sst_dsp_outbox_write(ipc->dsp, msg->tx_data, msg->tx_size);
	sst_dsp_shim_write_unlocked(ipc->dsp, SKL_ADSP_REG_HIPCIE,
						header->extension);
	sst_dsp_shim_write_unlocked(ipc->dsp, SKL_ADSP_REG_HIPCI,
		header->primary | SKL_ADSP_REG_HIPCI_BUSY);
}

int skl_ipc_check_D0i0(struct sst_dsp *dsp, bool state)
{
	int ret;

	/* check D0i3 support */
	if (!dsp->fw_ops.set_state_D0i0)
		return 0;

	/* Attempt D0i0 or D0i3 based on state */
	if (state)
		ret = dsp->fw_ops.set_state_D0i0(dsp);
	else
		ret = dsp->fw_ops.set_state_D0i3(dsp);

	return ret;
}

static struct ipc_message *skl_ipc_reply_get_msg(struct sst_generic_ipc *ipc,
				u64 ipc_header)
{
	struct ipc_message *msg =  NULL;
	struct skl_ipc_header *header = (struct skl_ipc_header *)(&ipc_header);

	if (list_empty(&ipc->rx_list)) {
		dev_err(ipc->dev, "ipc: rx list is empty but received 0x%x\n",
			header->primary);
		goto out;
	}

	msg = list_first_entry(&ipc->rx_list, struct ipc_message, list);

	list_del(&msg->list);
out:
	return msg;

}

int skl_ipc_process_notification(struct sst_generic_ipc *ipc,
		struct skl_ipc_header header)
{
	struct skl_dev *skl = container_of(ipc, struct skl_dev, ipc);

	if (IPC_GLB_NOTIFY_MSG_TYPE(header.primary)) {
		switch (IPC_GLB_NOTIFY_TYPE(header.primary)) {

		case IPC_GLB_NOTIFY_UNDERRUN:
			dev_err(ipc->dev, "FW Underrun %x\n", header.primary);
			break;

		case IPC_GLB_NOTIFY_RESOURCE_EVENT:
			dev_err(ipc->dev, "MCPS Budget Violation: %x\n",
						header.primary);
			break;

		case IPC_GLB_NOTIFY_FW_READY:
			skl->boot_complete = true;
			wake_up(&skl->boot_wait);
			break;

		case IPC_GLB_NOTIFY_PHRASE_DETECTED:
			dev_dbg(ipc->dev, "***** Phrase Detected **********\n");

			/*
			 * Per HW recomendation, After phrase detection,
			 * clear the CGCTL.MISCBDCGE.
			 *
			 * This will be set back on stream closure
			 */
			skl->enable_miscbdcge(ipc->dev, false);
			skl->miscbdcg_disabled = true;
			break;

		default:
			dev_err(ipc->dev, "ipc: Unhandled error msg=%x\n",
						header.primary);
			break;
		}
	}

	return 0;
}

struct skl_ipc_err_map {
	const char *msg;
	enum skl_ipc_glb_reply reply;
	int err;
};

static struct skl_ipc_err_map skl_err_map[] = {
	{"DSP out of memory", IPC_GLB_REPLY_OUT_OF_MEMORY, -ENOMEM},
	{"DSP busy", IPC_GLB_REPLY_BUSY, -EBUSY},
	{"SCLK already running", IPC_GLB_REPLY_SCLK_ALREADY_RUNNING,
			IPC_GLB_REPLY_SCLK_ALREADY_RUNNING},
	{"MCLK already running", IPC_GLB_REPLY_MCLK_ALREADY_RUNNING,
			IPC_GLB_REPLY_MCLK_ALREADY_RUNNING},
};

static int skl_ipc_set_reply_error_code(struct sst_generic_ipc *ipc, u32 reply)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(skl_err_map); i++) {
		if (skl_err_map[i].reply == reply)
			break;
	}

	if (i == ARRAY_SIZE(skl_err_map)) {
		dev_err(ipc->dev, "ipc FW reply: %d FW Error Code: %u\n",
				reply,
				ipc->dsp->fw_ops.get_fw_errcode(ipc->dsp));
		return -EINVAL;
	}

	if (skl_err_map[i].err < 0)
		dev_err(ipc->dev, "ipc FW reply: %s FW Error Code: %u\n",
				skl_err_map[i].msg,
				ipc->dsp->fw_ops.get_fw_errcode(ipc->dsp));
	else
		dev_info(ipc->dev, "ipc FW reply: %s FW Error Code: %u\n",
				skl_err_map[i].msg,
				ipc->dsp->fw_ops.get_fw_errcode(ipc->dsp));

	return skl_err_map[i].err;
}

void skl_ipc_process_reply(struct sst_generic_ipc *ipc,
		struct skl_ipc_header header)
{
	struct ipc_message *msg;
	u32 reply = header.primary & IPC_GLB_REPLY_STATUS_MASK;
	u64 *ipc_header = (u64 *)(&header);
	struct skl_dev *skl = container_of(ipc, struct skl_dev, ipc);
	unsigned long flags;

	spin_lock_irqsave(&ipc->dsp->spinlock, flags);
	msg = skl_ipc_reply_get_msg(ipc, *ipc_header);
	spin_unlock_irqrestore(&ipc->dsp->spinlock, flags);
	if (msg == NULL) {
		dev_dbg(ipc->dev, "ipc: rx list is empty\n");
		return;
	}

	/* first process the header */
	if (reply == IPC_GLB_REPLY_SUCCESS) {
		dev_dbg(ipc->dev, "ipc FW reply %x: success\n", header.primary);
		/* copy the rx data from the mailbox */
		sst_dsp_inbox_read(ipc->dsp, msg->rx_data, msg->rx_size);
		switch (IPC_GLB_NOTIFY_MSG_TYPE(header.primary)) {
		case IPC_GLB_LOAD_MULTIPLE_MODS:
		case IPC_GLB_LOAD_LIBRARY:
			skl->mod_load_complete = true;
			skl->mod_load_status = true;
			wake_up(&skl->mod_load_wait);
			break;

		default:
			break;

		}
	} else {
		msg->errno = skl_ipc_set_reply_error_code(ipc, reply);
		switch (IPC_GLB_NOTIFY_MSG_TYPE(header.primary)) {
		case IPC_GLB_LOAD_MULTIPLE_MODS:
		case IPC_GLB_LOAD_LIBRARY:
			skl->mod_load_complete = true;
			skl->mod_load_status = false;
			wake_up(&skl->mod_load_wait);
			break;

		default:
			break;

		}
	}

	spin_lock_irqsave(&ipc->dsp->spinlock, flags);
	sst_ipc_tx_msg_reply_complete(ipc, msg);
	spin_unlock_irqrestore(&ipc->dsp->spinlock, flags);
}

irqreturn_t skl_dsp_irq_thread_handler(int irq, void *context)
{
	struct sst_dsp *dsp = context;
	struct skl_dev *skl = sst_dsp_get_thread_context(dsp);
	struct sst_generic_ipc *ipc = &skl->ipc;
	struct skl_ipc_header header = {0};
	u32 hipcie, hipct, hipcte;
	int ipc_irq = 0;

	if (dsp->intr_status & SKL_ADSPIS_CL_DMA)
		skl_cldma_process_intr(dsp);

	/* Here we handle IPC interrupts only */
	if (!(dsp->intr_status & SKL_ADSPIS_IPC))
		return IRQ_NONE;

	hipcie = sst_dsp_shim_read_unlocked(dsp, SKL_ADSP_REG_HIPCIE);
	hipct = sst_dsp_shim_read_unlocked(dsp, SKL_ADSP_REG_HIPCT);
	hipcte = sst_dsp_shim_read_unlocked(dsp, SKL_ADSP_REG_HIPCTE);

	/* reply message from DSP */
	if (hipcie & SKL_ADSP_REG_HIPCIE_DONE) {
		sst_dsp_shim_update_bits(dsp, SKL_ADSP_REG_HIPCCTL,
			SKL_ADSP_REG_HIPCCTL_DONE, 0);

		/* clear DONE bit - tell DSP we have completed the operation */
		sst_dsp_shim_update_bits_forced(dsp, SKL_ADSP_REG_HIPCIE,
			SKL_ADSP_REG_HIPCIE_DONE, SKL_ADSP_REG_HIPCIE_DONE);

		ipc_irq = 1;

		/* unmask Done interrupt */
		sst_dsp_shim_update_bits(dsp, SKL_ADSP_REG_HIPCCTL,
			SKL_ADSP_REG_HIPCCTL_DONE, SKL_ADSP_REG_HIPCCTL_DONE);
	}

	/* New message from DSP */
	if (hipct & SKL_ADSP_REG_HIPCT_BUSY) {
		header.primary = hipct;
		header.extension = hipcte;
		dev_dbg(dsp->dev, "IPC irq: Firmware respond primary:%x\n",
						header.primary);
		dev_dbg(dsp->dev, "IPC irq: Firmware respond extension:%x\n",
						header.extension);

		if (IPC_GLB_NOTIFY_RSP_TYPE(header.primary)) {
			/* Handle Immediate reply from DSP Core */
			skl_ipc_process_reply(ipc, header);
		} else {
			dev_dbg(dsp->dev, "IPC irq: Notification from firmware\n");
			skl_ipc_process_notification(ipc, header);
		}
		/* clear  busy interrupt */
		sst_dsp_shim_update_bits_forced(dsp, SKL_ADSP_REG_HIPCT,
			SKL_ADSP_REG_HIPCT_BUSY, SKL_ADSP_REG_HIPCT_BUSY);
		ipc_irq = 1;
	}

	if (ipc_irq == 0)
		return IRQ_NONE;

	skl_ipc_int_enable(dsp);

	/* continue to send any remaining messages... */
	schedule_work(&ipc->kwork);

	return IRQ_HANDLED;
}

void skl_ipc_int_enable(struct sst_dsp *ctx)
{
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_ADSPIC,
			SKL_ADSPIC_IPC, SKL_ADSPIC_IPC);
}

void skl_ipc_int_disable(struct sst_dsp *ctx)
{
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_ADSPIC,
			SKL_ADSPIC_IPC, 0);
}

void skl_ipc_op_int_enable(struct sst_dsp *ctx)
{
	/* enable IPC DONE interrupt */
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_HIPCCTL,
		SKL_ADSP_REG_HIPCCTL_DONE, SKL_ADSP_REG_HIPCCTL_DONE);

	/* Enable IPC BUSY interrupt */
	sst_dsp_shim_update_bits(ctx, SKL_ADSP_REG_HIPCCTL,
		SKL_ADSP_REG_HIPCCTL_BUSY, SKL_ADSP_REG_HIPCCTL_BUSY);
}

void skl_ipc_op_int_disable(struct sst_dsp *ctx)
{
	/* disable IPC DONE interrupt */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_HIPCCTL,
					SKL_ADSP_REG_HIPCCTL_DONE, 0);

	/* Disable IPC BUSY interrupt */
	sst_dsp_shim_update_bits_unlocked(ctx, SKL_ADSP_REG_HIPCCTL,
					SKL_ADSP_REG_HIPCCTL_BUSY, 0);

}

bool skl_ipc_int_status(struct sst_dsp *ctx)
{
	return sst_dsp_shim_read_unlocked(ctx,
			SKL_ADSP_REG_ADSPIS) & SKL_ADSPIS_IPC;
}

int skl_ipc_init(struct device *dev, struct skl_dev *skl)
{
	struct sst_generic_ipc *ipc;
	int err;

	ipc = &skl->ipc;
	ipc->dsp = skl->dsp;
	ipc->dev = dev;

	ipc->tx_data_max_size = SKL_ADSP_W1_SZ;
	ipc->rx_data_max_size = SKL_ADSP_W0_UP_SZ;

	err = sst_ipc_init(ipc);
	if (err)
		return err;

	ipc->ops.tx_msg = skl_ipc_tx_msg;
	ipc->ops.tx_data_copy = skl_ipc_tx_data_copy;
	ipc->ops.is_dsp_busy = skl_ipc_is_dsp_busy;

	return 0;
}

void skl_ipc_free(struct sst_generic_ipc *ipc)
{
	/* Disable IPC DONE interrupt */
	sst_dsp_shim_update_bits(ipc->dsp, SKL_ADSP_REG_HIPCCTL,
		SKL_ADSP_REG_HIPCCTL_DONE, 0);

	/* Disable IPC BUSY interrupt */
	sst_dsp_shim_update_bits(ipc->dsp, SKL_ADSP_REG_HIPCCTL,
		SKL_ADSP_REG_HIPCCTL_BUSY, 0);

	sst_ipc_fini(ipc);
}

int skl_ipc_create_pipeline(struct sst_generic_ipc *ipc,
		u16 ppl_mem_size, u8 ppl_type, u8 instance_id, u8 lp_mode)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_CREATE_PPL);
	header.primary |= IPC_INSTANCE_ID(instance_id);
	header.primary |= IPC_PPL_TYPE(ppl_type);
	header.primary |= IPC_PPL_MEM_SIZE(ppl_mem_size);

	header.extension = IPC_PPL_LP_MODE(lp_mode);

	dev_dbg(ipc->dev, "In %s header=%d\n", __func__, header.primary);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: create pipeline fail, err: %d\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_create_pipeline);

int skl_ipc_delete_pipeline(struct sst_generic_ipc *ipc, u8 instance_id)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_DELETE_PPL);
	header.primary |= IPC_INSTANCE_ID(instance_id);

	dev_dbg(ipc->dev, "In %s header=%d\n", __func__, header.primary);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: delete pipeline failed, err %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(skl_ipc_delete_pipeline);

int skl_ipc_set_pipeline_state(struct sst_generic_ipc *ipc,
		u8 instance_id, enum skl_ipc_pipeline_state state)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_SET_PPL_STATE);
	header.primary |= IPC_INSTANCE_ID(instance_id);
	header.primary |= IPC_PPL_STATE(state);

	dev_dbg(ipc->dev, "In %s header=%d\n", __func__, header.primary);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: set pipeline state failed, err: %d\n", ret);
		return ret;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_set_pipeline_state);

int
skl_ipc_save_pipeline(struct sst_generic_ipc *ipc, u8 instance_id, int dma_id)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_SAVE_PPL);
	header.primary |= IPC_INSTANCE_ID(instance_id);

	header.extension = IPC_DMA_ID(dma_id);
	dev_dbg(ipc->dev, "In %s header=%d\n", __func__, header.primary);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: save pipeline failed, err: %d\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_save_pipeline);

int skl_ipc_restore_pipeline(struct sst_generic_ipc *ipc, u8 instance_id)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_RESTORE_PPL);
	header.primary |= IPC_INSTANCE_ID(instance_id);

	dev_dbg(ipc->dev, "In %s header=%d\n", __func__, header.primary);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: restore  pipeline failed, err: %d\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_restore_pipeline);

int skl_ipc_set_dx(struct sst_generic_ipc *ipc, u8 instance_id,
		u16 module_id, struct skl_ipc_dxstate_info *dx)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_MOD_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_MOD_SET_DX);
	header.primary |= IPC_MOD_INSTANCE_ID(instance_id);
	header.primary |= IPC_MOD_ID(module_id);

	dev_dbg(ipc->dev, "In %s primary =%x ext=%x\n", __func__,
			 header.primary, header.extension);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header,
				dx, sizeof(*dx), NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: set dx failed, err %d\n", ret);
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_set_dx);

int skl_ipc_init_instance(struct sst_generic_ipc *ipc,
		struct skl_ipc_init_instance_msg *msg, void *param_data)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;
	u32 *buffer = (u32 *)param_data;
	 /* param_block_size must be in dwords */
	u16 param_block_size = msg->param_data_size / sizeof(u32);

	print_hex_dump_debug("Param data:", DUMP_PREFIX_NONE,
		16, 4, buffer, param_block_size, false);

	header.primary = IPC_MSG_TARGET(IPC_MOD_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_MOD_INIT_INSTANCE);
	header.primary |= IPC_MOD_INSTANCE_ID(msg->instance_id);
	header.primary |= IPC_MOD_ID(msg->module_id);

	header.extension = IPC_CORE_ID(msg->core_id);
	header.extension |= IPC_PPL_INSTANCE_ID(msg->ppl_instance_id);
	header.extension |= IPC_PARAM_BLOCK_SIZE(param_block_size);
	header.extension |= IPC_DOMAIN(msg->domain);

	dev_dbg(ipc->dev, "In %s primary =%x ext=%x\n", __func__,
			 header.primary, header.extension);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, param_data,
			msg->param_data_size, NULL, 0);

	if (ret < 0) {
		dev_err(ipc->dev, "ipc: init instance failed\n");
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_init_instance);

int skl_ipc_bind_unbind(struct sst_generic_ipc *ipc,
		struct skl_ipc_bind_unbind_msg *msg)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	u8 bind_unbind = msg->bind ? IPC_MOD_BIND : IPC_MOD_UNBIND;
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_MOD_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(bind_unbind);
	header.primary |= IPC_MOD_INSTANCE_ID(msg->instance_id);
	header.primary |= IPC_MOD_ID(msg->module_id);

	header.extension = IPC_DST_MOD_ID(msg->dst_module_id);
	header.extension |= IPC_DST_MOD_INSTANCE_ID(msg->dst_instance_id);
	header.extension |= IPC_DST_QUEUE(msg->dst_queue);
	header.extension |= IPC_SRC_QUEUE(msg->src_queue);

	dev_dbg(ipc->dev, "In %s hdr=%x ext=%x\n", __func__, header.primary,
			 header.extension);
	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(ipc->dev, "ipc: bind/unbind failed\n");
		return ret;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_bind_unbind);

/*
 * In order to load a module we need to send IPC to initiate that. DMA will
 * performed to load the module memory. The FW supports multiple module load
 * at single shot, so we can send IPC with N modules represented by
 * module_cnt
 */
int skl_ipc_load_modules(struct sst_generic_ipc *ipc,
				u8 module_cnt, void *data)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_LOAD_MULTIPLE_MODS);
	header.primary |= IPC_LOAD_MODULE_CNT(module_cnt);

	ret = sst_ipc_tx_message_nowait(ipc, *ipc_header, data,
				(sizeof(u16) * module_cnt));
	if (ret < 0)
		dev_err(ipc->dev, "ipc: load modules failed :%d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_load_modules);

int skl_ipc_unload_modules(struct sst_generic_ipc *ipc, u8 module_cnt,
							void *data)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_UNLOAD_MULTIPLE_MODS);
	header.primary |= IPC_LOAD_MODULE_CNT(module_cnt);

	ret = sst_ipc_tx_message_wait(ipc, *ipc_header, data,
				(sizeof(u16) * module_cnt), NULL, 0);
	if (ret < 0)
		dev_err(ipc->dev, "ipc: unload modules failed :%d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_unload_modules);

int skl_ipc_set_large_config(struct sst_generic_ipc *ipc,
		struct skl_ipc_large_config_msg *msg, u32 *param)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret = 0;
	size_t sz_remaining, tx_size, data_offset;

	header.primary = IPC_MSG_TARGET(IPC_MOD_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_MOD_LARGE_CONFIG_SET);
	header.primary |= IPC_MOD_INSTANCE_ID(msg->instance_id);
	header.primary |= IPC_MOD_ID(msg->module_id);

	header.extension = IPC_DATA_OFFSET_SZ(msg->param_data_size);
	header.extension |= IPC_LARGE_PARAM_ID(msg->large_param_id);
	header.extension |= IPC_FINAL_BLOCK(0);
	header.extension |= IPC_INITIAL_BLOCK(1);

	sz_remaining = msg->param_data_size;
	data_offset = 0;
	while (sz_remaining != 0) {
		tx_size = sz_remaining > SKL_ADSP_W1_SZ
				? SKL_ADSP_W1_SZ : sz_remaining;
		if (tx_size == sz_remaining)
			header.extension |= IPC_FINAL_BLOCK(1);

		dev_dbg(ipc->dev, "In %s primary=%#x ext=%#x\n", __func__,
			header.primary, header.extension);
		dev_dbg(ipc->dev, "transmitting offset: %#x, size: %#x\n",
			(unsigned)data_offset, (unsigned)tx_size);
		ret = sst_ipc_tx_message_wait(ipc, *ipc_header,
					  ((char *)param) + data_offset,
					  tx_size, NULL, 0);
		if (ret < 0) {
			dev_err(ipc->dev,
				"ipc: set large config fail, err: %d\n", ret);
			return ret;
		}
		sz_remaining -= tx_size;
		data_offset = msg->param_data_size - sz_remaining;

		/* clear the fields */
		header.extension &= IPC_INITIAL_BLOCK_CLEAR;
		header.extension &= IPC_DATA_OFFSET_SZ_CLEAR;
		/* fill the fields */
		header.extension |= IPC_INITIAL_BLOCK(0);
		header.extension |= IPC_DATA_OFFSET_SZ(data_offset);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_set_large_config);

int skl_ipc_get_large_config(struct sst_generic_ipc *ipc,
		struct skl_ipc_large_config_msg *msg, u32 *param)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret = 0;
	size_t sz_remaining, rx_size, data_offset;

	header.primary = IPC_MSG_TARGET(IPC_MOD_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_MOD_LARGE_CONFIG_GET);
	header.primary |= IPC_MOD_INSTANCE_ID(msg->instance_id);
	header.primary |= IPC_MOD_ID(msg->module_id);

	header.extension = IPC_DATA_OFFSET_SZ(msg->param_data_size);
	header.extension |= IPC_LARGE_PARAM_ID(msg->large_param_id);
	header.extension |= IPC_FINAL_BLOCK(1);
	header.extension |= IPC_INITIAL_BLOCK(1);

	sz_remaining = msg->param_data_size;
	data_offset = 0;

	while (sz_remaining != 0) {
		rx_size = sz_remaining > SKL_ADSP_W1_SZ
				? SKL_ADSP_W1_SZ : sz_remaining;
		if (rx_size == sz_remaining)
			header.extension |= IPC_FINAL_BLOCK(1);

		ret = sst_ipc_tx_message_wait(ipc, *ipc_header, NULL, 0,
					      ((char *)param) + data_offset,
					      msg->param_data_size);
		if (ret < 0) {
			dev_err(ipc->dev,
				"ipc: get large config fail, err: %d\n", ret);
			return ret;
		}
		sz_remaining -= rx_size;
		data_offset = msg->param_data_size - sz_remaining;

		/* clear the fields */
		header.extension &= IPC_INITIAL_BLOCK_CLEAR;
		header.extension &= IPC_DATA_OFFSET_SZ_CLEAR;
		/* fill the fields */
		header.extension |= IPC_INITIAL_BLOCK(1);
		header.extension |= IPC_DATA_OFFSET_SZ(data_offset);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_get_large_config);

int skl_sst_ipc_load_library(struct sst_generic_ipc *ipc,
				u8 dma_id, u8 table_id, bool wait)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret = 0;

	header.primary = IPC_MSG_TARGET(IPC_FW_GEN_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_GLB_LOAD_LIBRARY);
	header.primary |= IPC_MOD_INSTANCE_ID(table_id);
	header.primary |= IPC_MOD_ID(dma_id);

	if (wait)
		ret = sst_ipc_tx_message_wait(ipc, *ipc_header,
					NULL, 0, NULL, 0);
	else
		ret = sst_ipc_tx_message_nowait(ipc, *ipc_header, NULL, 0);

	if (ret < 0)
		dev_err(ipc->dev, "ipc: load lib failed\n");

	return ret;
}
EXPORT_SYMBOL_GPL(skl_sst_ipc_load_library);

int skl_ipc_set_d0ix(struct sst_generic_ipc *ipc, struct skl_ipc_d0ix_msg *msg)
{
	struct skl_ipc_header header = {0};
	u64 *ipc_header = (u64 *)(&header);
	int ret;

	header.primary = IPC_MSG_TARGET(IPC_MOD_MSG);
	header.primary |= IPC_MSG_DIR(IPC_MSG_REQUEST);
	header.primary |= IPC_GLB_TYPE(IPC_MOD_SET_D0IX);
	header.primary |= IPC_MOD_INSTANCE_ID(msg->instance_id);
	header.primary |= IPC_MOD_ID(msg->module_id);

	header.extension = IPC_D0IX_WAKE(msg->wake);
	header.extension |= IPC_D0IX_STREAMING(msg->streaming);

	dev_dbg(ipc->dev, "In %s primary=%x ext=%x\n", __func__,
			header.primary,	header.extension);

	/*
	 * Use the nopm IPC here as we dont want it checking for D0iX
	 */
	ret = sst_ipc_tx_message_nopm(ipc, *ipc_header, NULL, 0, NULL, 0);
	if (ret < 0)
		dev_err(ipc->dev, "ipc: set d0ix failed, err %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(skl_ipc_set_d0ix);
