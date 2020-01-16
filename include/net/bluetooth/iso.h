/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2022 Intel Corporation
 */

#ifndef __ISO_H
#define __ISO_H

/* ISO defaults */
#define ISO_DEFAULT_MTU		251

/* ISO socket address */
struct sockaddr_iso {
	sa_family_t	iso_family;
	bdaddr_t	iso_bdaddr;
	__u8		iso_bdaddr_type;
};

#endif /* __ISO_H */
