/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_ZYNQMP_IPI_MESSAGE_H_
#define _LINUX_ZYNQMP_IPI_MESSAGE_H_

/**
 * struct zynqmp_ipi_message - ZynqMP IPI message structure
 * @len:  Length of message
 * @data: message payload
 *
 * This is the structure for data used in mbox_send_message
 * the maximum length of data buffer is fixed to 12 bytes.
 * Client is supposed to be aware of this.
 */
struct zynqmp_ipi_message {
	size_t len;
	u8 data[0];
};

#endif /* _LINUX_ZYNQMP_IPI_MESSAGE_H_ */
