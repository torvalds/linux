/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2015 6WIND S.A.
 * Author: Nicolas Dichtel <nicolas.dichtel@6wind.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */
#ifndef _UAPI_LINUX_NET_NAMESPACE_H_
#define _UAPI_LINUX_NET_NAMESPACE_H_

/* Attributes of RTM_NEWNSID/RTM_GETNSID messages */
enum {
	NETNSA_NONE,
#define NETNSA_NSID_NOT_ASSIGNED -1
	NETNSA_NSID,
	NETNSA_PID,
	NETNSA_FD,
	NETNSA_TARGET_NSID,
	__NETNSA_MAX,
};

#define NETNSA_MAX		(__NETNSA_MAX - 1)

#endif /* _UAPI_LINUX_NET_NAMESPACE_H_ */
