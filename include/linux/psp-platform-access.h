/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PSP_PLATFORM_ACCESS_H
#define __PSP_PLATFORM_ACCESS_H

#include <linux/psp.h>

enum psp_platform_access_msg {
	PSP_CMD_NONE = 0x0,
};

struct psp_req_buffer_hdr {
	u32 payload_size;
	u32 status;
} __packed;

struct psp_request {
	struct psp_req_buffer_hdr header;
	void *buf;
} __packed;

/**
 * psp_send_platform_access_msg() - Send a message to control platform features
 *
 * This function is intended to be used by drivers outside of ccp to communicate
 * with the platform.
 *
 * Returns:
 *  0:           success
 *  -%EBUSY:     mailbox in recovery or in use
 *  -%ENODEV:    driver not bound with PSP device
 *  -%ETIMEDOUT: request timed out
 *  -%EIO:       unknown error (see kernel log)
 */
int psp_send_platform_access_msg(enum psp_platform_access_msg, struct psp_request *req);

/**
 * psp_ring_platform_doorbell() - Ring platform doorbell
 *
 * This function is intended to be used by drivers outside of ccp to ring the
 * platform doorbell with a message.
 *
 * Returns:
 *  0:           success
 *  -%EBUSY:     mailbox in recovery or in use
 *  -%ENODEV:    driver not bound with PSP device
 *  -%ETIMEDOUT: request timed out
 *  -%EIO:       error will be stored in result argument
 */
int psp_ring_platform_doorbell(int msg, u32 *result);

/**
 * psp_check_platform_access_status() - Checks whether platform features is ready
 *
 * This function is intended to be used by drivers outside of ccp to determine
 * if platform features has initialized.
 *
 * Returns:
 * 0          platform features is ready
 * -%ENODEV   platform features is not ready or present
 */
int psp_check_platform_access_status(void);

#endif /* __PSP_PLATFORM_ACCESS_H */
