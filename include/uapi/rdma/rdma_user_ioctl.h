/*
 * Copyright (c) 2016 Mellanox Technologies, LTD. All rights reserved.
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
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef RDMA_USER_IOCTL_H
#define RDMA_USER_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <rdma/ib_user_mad.h>
#include <rdma/hfi/hfi1_ioctl.h>

/* Documentation/ioctl/ioctl-number.txt */
#define RDMA_IOCTL_MAGIC		0x1b
#define IB_IOCTL_MAGIC			RDMA_IOCTL_MAGIC

#define IB_USER_MAD_REGISTER_AGENT	_IOWR(IB_IOCTL_MAGIC, 1, \
					      struct ib_user_mad_reg_req)

#define IB_USER_MAD_UNREGISTER_AGENT	_IOW(IB_IOCTL_MAGIC, 2, __u32)

#define IB_USER_MAD_ENABLE_PKEY		_IO(IB_IOCTL_MAGIC, 3)

#define IB_USER_MAD_REGISTER_AGENT2     _IOWR(IB_IOCTL_MAGIC, 4, \
					      struct ib_user_mad_reg_req2)

/* User commands. */
#define HFI1_CMD_ASSIGN_CTXT     1	/* allocate HFI and context */
#define HFI1_CMD_CTXT_INFO       2	/* find out what resources we got */
#define HFI1_CMD_USER_INFO       3	/* set up userspace */
#define HFI1_CMD_TID_UPDATE      4	/* update expected TID entries */
#define HFI1_CMD_TID_FREE        5	/* free expected TID entries */
#define HFI1_CMD_CREDIT_UPD      6	/* force an update of PIO credit */

#define HFI1_CMD_RECV_CTRL       8	/* control receipt of packets */
#define HFI1_CMD_POLL_TYPE       9	/* set the kind of polling we want */
#define HFI1_CMD_ACK_EVENT       10	/* ack & clear user status bits */
#define HFI1_CMD_SET_PKEY        11     /* set context's pkey */
#define HFI1_CMD_CTXT_RESET      12     /* reset context's HW send context */
#define HFI1_CMD_TID_INVAL_READ  13     /* read TID cache invalidations */
#define HFI1_CMD_GET_VERS	 14	/* get the version of the user cdev */

/*
 * User IOCTLs can not go above 128 if they do then see common.h and change the
 * base for the snoop ioctl
 */

/*
 * Make the ioctls occupy the last 0xf0-0xff portion of the IB range
 */
#define __NUM(cmd) (HFI1_CMD_##cmd + 0xe0)

#define HFI1_IOCTL_ASSIGN_CTXT \
	_IOWR(IB_IOCTL_MAGIC, __NUM(ASSIGN_CTXT), struct hfi1_user_info)
#define HFI1_IOCTL_CTXT_INFO \
	_IOW(IB_IOCTL_MAGIC, __NUM(CTXT_INFO), struct hfi1_ctxt_info)
#define HFI1_IOCTL_USER_INFO \
	_IOW(IB_IOCTL_MAGIC, __NUM(USER_INFO), struct hfi1_base_info)
#define HFI1_IOCTL_TID_UPDATE \
	_IOWR(IB_IOCTL_MAGIC, __NUM(TID_UPDATE), struct hfi1_tid_info)
#define HFI1_IOCTL_TID_FREE \
	_IOWR(IB_IOCTL_MAGIC, __NUM(TID_FREE), struct hfi1_tid_info)
#define HFI1_IOCTL_CREDIT_UPD \
	_IO(IB_IOCTL_MAGIC, __NUM(CREDIT_UPD))
#define HFI1_IOCTL_RECV_CTRL \
	_IOW(IB_IOCTL_MAGIC, __NUM(RECV_CTRL), int)
#define HFI1_IOCTL_POLL_TYPE \
	_IOW(IB_IOCTL_MAGIC, __NUM(POLL_TYPE), int)
#define HFI1_IOCTL_ACK_EVENT \
	_IOW(IB_IOCTL_MAGIC, __NUM(ACK_EVENT), unsigned long)
#define HFI1_IOCTL_SET_PKEY \
	_IOW(IB_IOCTL_MAGIC, __NUM(SET_PKEY), __u16)
#define HFI1_IOCTL_CTXT_RESET \
	_IO(IB_IOCTL_MAGIC, __NUM(CTXT_RESET))
#define HFI1_IOCTL_TID_INVAL_READ \
	_IOWR(IB_IOCTL_MAGIC, __NUM(TID_INVAL_READ), struct hfi1_tid_info)
#define HFI1_IOCTL_GET_VERS \
	_IOR(IB_IOCTL_MAGIC, __NUM(GET_VERS), int)

#endif /* RDMA_USER_IOCTL_H */
