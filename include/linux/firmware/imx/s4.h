/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2021 NXP
 *
 * Header file for the IPC implementation.
 */

#ifndef _S4_IPC_H
#define _S4_IPC_H

struct imx_s4_ipc;

struct imx_s4_rpc_msg {
	uint8_t ver;
	uint8_t size;
	uint8_t cmd;
	uint8_t tag;
} __packed;

#endif /* _S4_IPC_H */
