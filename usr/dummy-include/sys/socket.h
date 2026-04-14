/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _DUMMY_SYS_SOCKET_H
#define _DUMMY_SYS_SOCKET_H

#include <linux/socket.h>

struct sockaddr {
	__kernel_sa_family_t	sa_family;	/* address family, AF_xxx	*/
	char			sa_data[14];	/* 14 bytes of protocol address	*/
};

#endif /* _DUMMY_SYS_SOCKET_H */
