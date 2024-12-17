/* SPDX-License-Identifier: GPL-2.0 */
/*
 *Copyright (c) 2024 Microchip Technology Inc. All rights reserved.
 */

#ifndef _LINUX_MCHP_IPC_H_
#define _LINUX_MCHP_IPC_H_

#include <linux/mailbox_controller.h>
#include <linux/types.h>

struct mchp_ipc_msg {
	u32 *buf;
	u16 size;
};

struct mchp_ipc_sbi_chan {
	void *buf_base_tx;
	void *buf_base_rx;
	void *msg_buf_tx;
	void *msg_buf_rx;
	phys_addr_t buf_base_tx_addr;
	phys_addr_t buf_base_rx_addr;
	phys_addr_t msg_buf_tx_addr;
	phys_addr_t msg_buf_rx_addr;
	int chan_aggregated_irq;
	int mp_irq;
	int mc_irq;
	u32 id;
	u32 max_msg_size;
};

#endif /* _LINUX_MCHP_IPC_H_ */
