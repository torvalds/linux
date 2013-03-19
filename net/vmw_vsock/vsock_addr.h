/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2007-2013 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _VSOCK_ADDR_H_
#define _VSOCK_ADDR_H_

#include <linux/vm_sockets.h>

void vsock_addr_init(struct sockaddr_vm *addr, u32 cid, u32 port);
int vsock_addr_validate(const struct sockaddr_vm *addr);
bool vsock_addr_bound(const struct sockaddr_vm *addr);
void vsock_addr_unbind(struct sockaddr_vm *addr);
bool vsock_addr_equals_addr(const struct sockaddr_vm *addr,
			    const struct sockaddr_vm *other);
bool vsock_addr_equals_addr_any(const struct sockaddr_vm *addr,
				const struct sockaddr_vm *other);
int vsock_addr_cast(const struct sockaddr *addr, size_t len,
		    struct sockaddr_vm **out_addr);

#endif
