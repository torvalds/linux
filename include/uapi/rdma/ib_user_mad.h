/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc. All rights reserved.
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

#ifndef IB_USER_MAD_H
#define IB_USER_MAD_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */
#define IB_USER_MAD_ABI_VERSION	5

/*
 * Make sure that all structs defined in this file remain laid out so
 * that they pack the same way on 32-bit and 64-bit architectures (to
 * avoid incompatibility between 32-bit userspace and 64-bit kernels).
 */

/**
 * ib_user_mad_hdr_old - Old version of MAD packet header without pkey_index
 * @id - ID of agent MAD received with/to be sent with
 * @status - 0 on successful receive, ETIMEDOUT if no response
 *   received (transaction ID in data[] will be set to TID of original
 *   request) (ignored on send)
 * @timeout_ms - Milliseconds to wait for response (unset on receive)
 * @retries - Number of automatic retries to attempt
 * @qpn - Remote QP number received from/to be sent to
 * @qkey - Remote Q_Key to be sent with (unset on receive)
 * @lid - Remote lid received from/to be sent to
 * @sl - Service level received with/to be sent with
 * @path_bits - Local path bits received with/to be sent with
 * @grh_present - If set, GRH was received/should be sent
 * @gid_index - Local GID index to send with (unset on receive)
 * @hop_limit - Hop limit in GRH
 * @traffic_class - Traffic class in GRH
 * @gid - Remote GID in GRH
 * @flow_label - Flow label in GRH
 */
struct ib_user_mad_hdr_old {
	__u32	id;
	__u32	status;
	__u32	timeout_ms;
	__u32	retries;
	__u32	length;
	__be32	qpn;
	__be32  qkey;
	__be16	lid;
	__u8	sl;
	__u8	path_bits;
	__u8	grh_present;
	__u8	gid_index;
	__u8	hop_limit;
	__u8	traffic_class;
	__u8	gid[16];
	__be32	flow_label;
};

/**
 * ib_user_mad_hdr - MAD packet header
 *   This layout allows specifying/receiving the P_Key index.  To use
 *   this capability, an application must call the
 *   IB_USER_MAD_ENABLE_PKEY ioctl on the user MAD file handle before
 *   any other actions with the file handle.
 * @id - ID of agent MAD received with/to be sent with
 * @status - 0 on successful receive, ETIMEDOUT if no response
 *   received (transaction ID in data[] will be set to TID of original
 *   request) (ignored on send)
 * @timeout_ms - Milliseconds to wait for response (unset on receive)
 * @retries - Number of automatic retries to attempt
 * @qpn - Remote QP number received from/to be sent to
 * @qkey - Remote Q_Key to be sent with (unset on receive)
 * @lid - Remote lid received from/to be sent to
 * @sl - Service level received with/to be sent with
 * @path_bits - Local path bits received with/to be sent with
 * @grh_present - If set, GRH was received/should be sent
 * @gid_index - Local GID index to send with (unset on receive)
 * @hop_limit - Hop limit in GRH
 * @traffic_class - Traffic class in GRH
 * @gid - Remote GID in GRH
 * @flow_label - Flow label in GRH
 * @pkey_index - P_Key index
 */
struct ib_user_mad_hdr {
	__u32	id;
	__u32	status;
	__u32	timeout_ms;
	__u32	retries;
	__u32	length;
	__be32	qpn;
	__be32  qkey;
	__be16	lid;
	__u8	sl;
	__u8	path_bits;
	__u8	grh_present;
	__u8	gid_index;
	__u8	hop_limit;
	__u8	traffic_class;
	__u8	gid[16];
	__be32	flow_label;
	__u16	pkey_index;
	__u8	reserved[6];
};

/**
 * ib_user_mad - MAD packet
 * @hdr - MAD packet header
 * @data - Contents of MAD
 *
 */
struct ib_user_mad {
	struct ib_user_mad_hdr hdr;
	__u64	data[0];
};

/*
 * Earlier versions of this interface definition declared the
 * method_mask[] member as an array of __u32 but treated it as a
 * bitmap made up of longs in the kernel.  This ambiguity meant that
 * 32-bit big-endian applications that can run on both 32-bit and
 * 64-bit kernels had no consistent ABI to rely on, and 64-bit
 * big-endian applications that treated method_mask as being made up
 * of 32-bit words would have their bitmap misinterpreted.
 *
 * To clear up this confusion, we change the declaration of
 * method_mask[] to use unsigned long and handle the conversion from
 * 32-bit userspace to 64-bit kernel for big-endian systems in the
 * compat_ioctl method.  Unfortunately, to keep the structure layout
 * the same, we need the method_mask[] array to be aligned only to 4
 * bytes even when long is 64 bits, which forces us into this ugly
 * typedef.
 */
typedef unsigned long __attribute__((aligned(4))) packed_ulong;
#define IB_USER_MAD_LONGS_PER_METHOD_MASK (128 / (8 * sizeof (long)))

/**
 * ib_user_mad_reg_req - MAD registration request
 * @id - Set by the kernel; used to identify agent in future requests.
 * @qpn - Queue pair number; must be 0 or 1.
 * @method_mask - The caller will receive unsolicited MADs for any method
 *   where @method_mask = 1.
 * @mgmt_class - Indicates which management class of MADs should be receive
 *   by the caller.  This field is only required if the user wishes to
 *   receive unsolicited MADs, otherwise it should be 0.
 * @mgmt_class_version - Indicates which version of MADs for the given
 *   management class to receive.
 * @oui: Indicates IEEE OUI when mgmt_class is a vendor class
 *   in the range from 0x30 to 0x4f. Otherwise not used.
 * @rmpp_version: If set, indicates the RMPP version used.
 *
 */
struct ib_user_mad_reg_req {
	__u32	id;
	packed_ulong method_mask[IB_USER_MAD_LONGS_PER_METHOD_MASK];
	__u8	qpn;
	__u8	mgmt_class;
	__u8	mgmt_class_version;
	__u8    oui[3];
	__u8	rmpp_version;
};

#define IB_IOCTL_MAGIC		0x1b

#define IB_USER_MAD_REGISTER_AGENT	_IOWR(IB_IOCTL_MAGIC, 1, \
					      struct ib_user_mad_reg_req)

#define IB_USER_MAD_UNREGISTER_AGENT	_IOW(IB_IOCTL_MAGIC, 2, __u32)

#define IB_USER_MAD_ENABLE_PKEY		_IO(IB_IOCTL_MAGIC, 3)

#endif /* IB_USER_MAD_H */
