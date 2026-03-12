/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2025 MediaTek Inc.
 */

#ifndef __MTK_VCP_MAILBOX_H__
#define __MTK_VCP_MAILBOX_H__

#define MTK_VCP_MBOX_SLOT_MAX_SIZE	0x100 /* mbox max slot size */

/**
 * struct mtk_ipi_info - mailbox message info for mtk-vcp-mailbox
 * @msg: The share buffer between IPC and mailbox driver
 * @len: Message length
 * @id: This is for identification purposes and not actually used
 *	by the mailbox hardware.
 * @index: The signal number of the mailbox message.
 * @slot_ofs: Data slot offset.
 * @irq_status: Captures incoming signals for the RX path.
 *
 * It is used between IPC with mailbox driver.
 */
struct mtk_ipi_info {
	void *msg;
	u32 len;
	u32 id;
	u32 index;
	u32 slot_ofs;
	u32 irq_status;
};

#endif
