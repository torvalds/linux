/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_IOV_IF_H
#define _QED_IOV_IF_H

#include <linux/qed/qed_if.h>

/* Structs used by PF to control and manipulate child VFs */
struct qed_iov_hv_ops {
	int (*configure)(struct qed_dev *cdev, int num_vfs_param);

};

#endif
