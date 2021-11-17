/*
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef ISCSI_ISER_H
#define ISCSI_ISER_H

#define ISER_ZBVA_NOT_SUP		0x80
#define ISER_SEND_W_INV_NOT_SUP		0x40
#define ISERT_ZBVA_NOT_USED		0x80
#define ISERT_SEND_W_INV_NOT_USED	0x40

#define ISCSI_CTRL	0x10
#define ISER_HELLO	0x20
#define ISER_HELLORPLY	0x30

#define ISER_VER	0x10
#define ISER_WSV	0x08
#define ISER_RSV	0x04

/**
 * struct iser_cm_hdr - iSER CM header (from iSER Annex A12)
 *
 * @flags:        flags support (zbva, send_w_inv)
 * @rsvd:         reserved
 */
struct iser_cm_hdr {
	u8      flags;
	u8      rsvd[3];
} __packed;

/**
 * struct iser_ctrl - iSER header of iSCSI control PDU
 *
 * @flags:        opcode and read/write valid bits
 * @rsvd:         reserved
 * @write_stag:   write rkey
 * @write_va:     write virtual address
 * @reaf_stag:    read rkey
 * @read_va:      read virtual address
 */
struct iser_ctrl {
	u8      flags;
	u8      rsvd[3];
	__be32  write_stag;
	__be64  write_va;
	__be32  read_stag;
	__be64  read_va;
} __packed;

#endif /* ISCSI_ISER_H */
