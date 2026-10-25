/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Controls header for IT6625/IT6626 driver
 */

#ifndef _UAPI_LINUX_IT6625_H
#define _UAPI_LINUX_IT6625_H

#include <linux/v4l2-controls.h>

/*
 * Currently detected HDMI audio sampling rate, in Hz. Read-only.
 * 0 means the rate is unavailable/unknown: no audio is currently
 * present on the input, the hardware reported a sample-rate id this
 * driver doesn't recognize, or the status read itself failed. Never a
 * literal 0 Hz sample rate.
 */
#define V4L2_CID_IT6625_AUDIO_SAMPLING_RATE	(V4L2_CID_USER_IT6625_BASE + 0)

/*
 * Whether HDMI audio is currently present on the input. Read-only.
 */
#define V4L2_CID_IT6625_AUDIO_PRESENT		(V4L2_CID_USER_IT6625_BASE + 1)

#endif /* _UAPI_LINUX_IT6625_H */
