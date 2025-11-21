/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _CDNS_CSI2RX_H
#define _CDNS_CSI2RX_H

#include <media/v4l2-subdev.h>

/**
 * cdns_csi2rx_negotiate_ppc - Negotiate pixel-per-clock on output interface
 *
 * @subdev: point to &struct v4l2_subdev
 * @pad: pad number of the source pad
 * @ppc: pointer to requested pixel-per-clock value
 *
 * Returns 0 on success, negative error code otherwise.
 */
int cdns_csi2rx_negotiate_ppc(struct v4l2_subdev *subdev, unsigned int pad,
			      u8 *ppc);

#endif
