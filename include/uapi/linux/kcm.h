/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Kernel Connection Multiplexor
 *
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * User API to clone KCM sockets and attach transport socket to a KCM
 * multiplexor.
 */

#ifndef KCM_KERNEL_H
#define KCM_KERNEL_H

struct kcm_attach {
	int fd;
	int bpf_fd;
};

struct kcm_unattach {
	int fd;
};

struct kcm_clone {
	int fd;
};

#define SIOCKCMATTACH	(SIOCPROTOPRIVATE + 0)
#define SIOCKCMUNATTACH	(SIOCPROTOPRIVATE + 1)
#define SIOCKCMCLONE	(SIOCPROTOPRIVATE + 2)

#define KCMPROTO_CONNECTED	0

/* Socket options */
#define KCM_RECV_DISABLE	1

#endif

