/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_FSI_H
#define _UAPI_LINUX_FSI_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * /dev/scom "raw" ioctl interface
 *
 * The driver supports a high level "read/write" interface which
 * handles retries and converts the status to Linux error codes,
 * however low level tools an debugger need to access the "raw"
 * HW status information and interpret it themselves, so this
 * ioctl interface is also provided for their use case.
 */

/* Structure for SCOM read/write */
struct scom_access {
	__u64	addr;		/* SCOM address, supports indirect */
	__u64	data;		/* SCOM data (in for write, out for read) */
	__u64	mask;		/* Data mask for writes */
	__u32	intf_errors;	/* Interface error flags */
#define SCOM_INTF_ERR_PARITY		0x00000001 /* Parity error */
#define SCOM_INTF_ERR_PROTECTION	0x00000002 /* Blocked by secure boot */
#define SCOM_INTF_ERR_ABORT		0x00000004 /* PIB reset during access */
#define SCOM_INTF_ERR_UNKNOWN		0x80000000 /* Unknown error */
	/*
	 * Note: Any other bit set in intf_errors need to be considered as an
	 * error. Future implementations may define new error conditions. The
	 * pib_status below is only valid if intf_errors is 0.
	 */
	__u8	pib_status;	/* 3-bit PIB status */
#define SCOM_PIB_SUCCESS	0	/* Access successful */
#define SCOM_PIB_BLOCKED	1	/* PIB blocked, pls retry */
#define SCOM_PIB_OFFLINE	2	/* Chiplet offline */
#define SCOM_PIB_PARTIAL	3	/* Partial good */
#define SCOM_PIB_BAD_ADDR	4	/* Invalid address */
#define SCOM_PIB_CLK_ERR	5	/* Clock error */
#define SCOM_PIB_PARITY_ERR	6	/* Parity error on the PIB bus */
#define SCOM_PIB_TIMEOUT	7	/* Bus timeout */
	__u8	pad;
};

/* Flags for SCOM check */
#define SCOM_CHECK_SUPPORTED	0x00000001	/* Interface supported */
#define SCOM_CHECK_PROTECTED	0x00000002	/* Interface blocked by secure boot */

/* Flags for SCOM reset */
#define SCOM_RESET_INTF		0x00000001	/* Reset interface */
#define SCOM_RESET_PIB		0x00000002	/* Reset PIB */

#define FSI_SCOM_CHECK	_IOR('s', 0x00, __u32)
#define FSI_SCOM_READ	_IOWR('s', 0x01, struct scom_access)
#define FSI_SCOM_WRITE	_IOWR('s', 0x02, struct scom_access)
#define FSI_SCOM_RESET	_IOW('s', 0x03, __u32)

#endif /* _UAPI_LINUX_FSI_H */
