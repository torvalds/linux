/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Eugen Hristev <eugen.hristev@microchip.com>
 */

#ifndef __LINUX_ATMEL_ISC_MEDIA_H__
#define __LINUX_ATMEL_ISC_MEDIA_H__

/*
 * There are 8 controls available:
 * 4 gain controls, sliders, for each of the BAYER components: R, B, GR, GB.
 * These gains are multipliers for each component, in format unsigned 0:4:9 with
 * a default value of 512 (1.0 multiplier).
 * 4 offset controls, sliders, for each of the BAYER components: R, B, GR, GB.
 * These offsets are added/substracted from each component, in format signed
 * 1:12:0 with a default value of 0 (+/- 0)
 *
 * To expose this to userspace, added 8 custom controls, in an auto cluster.
 *
 * To summarize the functionality:
 * The auto cluster switch is the auto white balance control, and it works
 * like this:
 * AWB == 1: autowhitebalance is on, the do_white_balance button is inactive,
 * the gains/offsets are inactive, but volatile and readable.
 * Thus, the results of the whitebalance algorithm are available to userspace to
 * read at any time.
 * AWB == 0: autowhitebalance is off, cluster is in manual mode, user can
 * configure the gain/offsets directly.
 * More than that, if the do_white_balance button is
 * pressed, the driver will perform one-time-adjustment, (preferably with color
 * checker card) and the userspace can read again the new values.
 *
 * With this feature, the userspace can save the coefficients and reinstall them
 * for example after reboot or reprobing the driver.
 */

enum atmel_isc_ctrl_id {
	/* Red component gain control */
	ISC_CID_R_GAIN = (V4L2_CID_USER_ATMEL_ISC_BASE + 0),
	/* Blue component gain control */
	ISC_CID_B_GAIN,
	/* Green Red component gain control */
	ISC_CID_GR_GAIN,
	/* Green Blue gain control */
	ISC_CID_GB_GAIN,
	/* Red component offset control */
	ISC_CID_R_OFFSET,
	/* Blue component offset control */
	ISC_CID_B_OFFSET,
	/* Green Red component offset control */
	ISC_CID_GR_OFFSET,
	/* Green Blue component offset control */
	ISC_CID_GB_OFFSET,
};

#endif
