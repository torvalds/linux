/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Texas Instruments' Message Manager
 *
 * Copyright (C) 2015-2022 Texas Instruments Incorporated - https://www.ti.com/
 *	Nishanth Menon
 */

#ifndef TI_MSGMGR_H
#define TI_MSGMGR_H

struct mbox_chan;

/**
 * struct ti_msgmgr_message - Message Manager structure
 * @len: Length of data in the Buffer
 * @buf: Buffer pointer
 * @chan_rx: Expected channel for response, must be provided to use polled rx
 * @timeout_rx_ms: Timeout value to use if polling for response
 *
 * This is the structure for data used in mbox_send_message
 * the length of data buffer used depends on the SoC integration
 * parameters - each message may be 64, 128 bytes long depending
 * on SoC. Client is supposed to be aware of this.
 */
struct ti_msgmgr_message {
	size_t len;
	u8 *buf;
	struct mbox_chan *chan_rx;
	int timeout_rx_ms;
};

#endif /* TI_MSGMGR_H */
