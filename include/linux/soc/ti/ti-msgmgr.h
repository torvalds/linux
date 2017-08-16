/*
 * Texas Instruments' Message Manager
 *
 * Copyright (C) 2015-2016 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef TI_MSGMGR_H
#define TI_MSGMGR_H

/**
 * struct ti_msgmgr_message - Message Manager structure
 * @len: Length of data in the Buffer
 * @buf: Buffer pointer
 *
 * This is the structure for data used in mbox_send_message
 * the length of data buffer used depends on the SoC integration
 * parameters - each message may be 64, 128 bytes long depending
 * on SoC. Client is supposed to be aware of this.
 */
struct ti_msgmgr_message {
	size_t len;
	u8 *buf;
};

#endif /* TI_MSGMGR_H */
