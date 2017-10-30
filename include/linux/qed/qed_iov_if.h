/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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

#ifndef _QED_IOV_IF_H
#define _QED_IOV_IF_H

#include <linux/qed/qed_if.h>

/* Structs used by PF to control and manipulate child VFs */
struct qed_iov_hv_ops {
	int (*configure)(struct qed_dev *cdev, int num_vfs_param);

	int (*set_mac) (struct qed_dev *cdev, u8 *mac, int vfid);

	int (*set_vlan) (struct qed_dev *cdev, u16 vid, int vfid);

	int (*get_config) (struct qed_dev *cdev, int vf_id,
			   struct ifla_vf_info *ivi);

	int (*set_link_state) (struct qed_dev *cdev, int vf_id,
			       int link_state);

	int (*set_spoof) (struct qed_dev *cdev, int vfid, bool val);

	int (*set_rate) (struct qed_dev *cdev, int vfid,
			 u32 min_rate, u32 max_rate);

	int (*set_trust) (struct qed_dev *cdev, int vfid, bool trust);
};

#endif
