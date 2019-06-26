/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2018 NXP
 *
 * Header file for the IPC implementation.
 */

#ifndef _SC_IPC_H
#define _SC_IPC_H

#include <linux/device.h>
#include <linux/types.h>

#define IMX_SC_RPC_VERSION	1
#define IMX_SC_RPC_MAX_MSG	8

struct imx_sc_ipc;

enum imx_sc_rpc_svc {
	IMX_SC_RPC_SVC_UNKNOWN = 0,
	IMX_SC_RPC_SVC_RETURN = 1,
	IMX_SC_RPC_SVC_PM = 2,
	IMX_SC_RPC_SVC_RM = 3,
	IMX_SC_RPC_SVC_TIMER = 5,
	IMX_SC_RPC_SVC_PAD = 6,
	IMX_SC_RPC_SVC_MISC = 7,
	IMX_SC_RPC_SVC_IRQ = 8,
	IMX_SC_RPC_SVC_ABORT = 9
};

struct imx_sc_rpc_msg {
	uint8_t ver;
	uint8_t size;
	uint8_t svc;
	uint8_t func;
};

/*
 * This is an function to send an RPC message over an IPC channel.
 * It is called by client-side SCFW API function shims.
 *
 * @param[in]     ipc         IPC handle
 * @param[in,out] msg         handle to a message
 * @param[in]     have_resp   response flag
 *
 * If have_resp is true then this function waits for a response
 * and returns the result in msg.
 */
int imx_scu_call_rpc(struct imx_sc_ipc *ipc, void *msg, bool have_resp);

/*
 * This function gets the default ipc handle used by SCU
 *
 * @param[out]	ipc	sc ipc handle
 *
 * @return Returns an error code (0 = success, failed if < 0)
 */
int imx_scu_get_handle(struct imx_sc_ipc **ipc);
#endif /* _SC_IPC_H */
