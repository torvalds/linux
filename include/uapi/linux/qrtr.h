/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_QRTR_H
#define _LINUX_QRTR_H

#include <linux/socket.h>
#include <linux/types.h>

struct sockaddr_qrtr {
	__kernel_sa_family_t sq_family;
	__u32 sq_node;
	__u32 sq_port;
};

#endif /* _LINUX_QRTR_H */
