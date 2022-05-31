/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
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

#ifndef _UAPI_VM_SOCKETS_H
#define _UAPI_VM_SOCKETS_H

#include <linux/socket.h>
#include <linux/types.h>

/* Option name for STREAM socket buffer size.  Use as the option name in
 * setsockopt(3) or getsockopt(3) to set or get an unsigned long long that
 * specifies the size of the buffer underlying a vSockets STREAM socket.
 * Value is clamped to the MIN and MAX.
 */

#define SO_VM_SOCKETS_BUFFER_SIZE 0

/* Option name for STREAM socket minimum buffer size.  Use as the option name
 * in setsockopt(3) or getsockopt(3) to set or get an unsigned long long that
 * specifies the minimum size allowed for the buffer underlying a vSockets
 * STREAM socket.
 */

#define SO_VM_SOCKETS_BUFFER_MIN_SIZE 1

/* Option name for STREAM socket maximum buffer size.  Use as the option name
 * in setsockopt(3) or getsockopt(3) to set or get an unsigned long long
 * that specifies the maximum size allowed for the buffer underlying a
 * vSockets STREAM socket.
 */

#define SO_VM_SOCKETS_BUFFER_MAX_SIZE 2

/* Option name for socket peer's host-specific VM ID.  Use as the option name
 * in getsockopt(3) to get a host-specific identifier for the peer endpoint's
 * VM.  The identifier is a signed integer.
 * Only available for hypervisor endpoints.
 */

#define SO_VM_SOCKETS_PEER_HOST_VM_ID 3

/* Option name for determining if a socket is trusted.  Use as the option name
 * in getsockopt(3) to determine if a socket is trusted.  The value is a
 * signed integer.
 */

#define SO_VM_SOCKETS_TRUSTED 5

/* Option name for STREAM socket connection timeout.  Use as the option name
 * in setsockopt(3) or getsockopt(3) to set or get the connection
 * timeout for a STREAM socket.
 */

#define SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD 6

/* Option name for using non-blocking send/receive.  Use as the option name
 * for setsockopt(3) or getsockopt(3) to set or get the non-blocking
 * transmit/receive flag for a STREAM socket.  This flag determines whether
 * send() and recv() can be called in non-blocking contexts for the given
 * socket.  The value is a signed integer.
 *
 * This option is only relevant to kernel endpoints, where descheduling the
 * thread of execution is not allowed, for example, while holding a spinlock.
 * It is not to be confused with conventional non-blocking socket operations.
 *
 * Only available for hypervisor endpoints.
 */

#define SO_VM_SOCKETS_NONBLOCK_TXRX 7

#define SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW 8

#if !defined(__KERNEL__)
#if __BITS_PER_LONG == 64 || (defined(__x86_64__) && defined(__ILP32__))
#define SO_VM_SOCKETS_CONNECT_TIMEOUT SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD
#else
#define SO_VM_SOCKETS_CONNECT_TIMEOUT \
	(sizeof(time_t) == sizeof(__kernel_long_t) ? SO_VM_SOCKETS_CONNECT_TIMEOUT_OLD : SO_VM_SOCKETS_CONNECT_TIMEOUT_NEW)
#endif
#endif

/* The vSocket equivalent of INADDR_ANY.  This works for the svm_cid field of
 * sockaddr_vm and indicates the context ID of the current endpoint.
 */

#define VMADDR_CID_ANY -1U

/* Bind to any available port.  Works for the svm_port field of
 * sockaddr_vm.
 */

#define VMADDR_PORT_ANY -1U

/* Use this as the destination CID in an address when referring to the
 * hypervisor.  VMCI relies on it being 0, but this would be useful for other
 * transports too.
 */

#define VMADDR_CID_HYPERVISOR 0

/* Use this as the destination CID in an address when referring to the
 * local communication (loopback).
 * (This was VMADDR_CID_RESERVED, but even VMCI doesn't use it anymore,
 * it was a legacy value from an older release).
 */

#define VMADDR_CID_LOCAL 1

/* Use this as the destination CID in an address when referring to the host
 * (any process other than the hypervisor).  VMCI relies on it being 2, but
 * this would be useful for other transports too.
 */

#define VMADDR_CID_HOST 2

/* The current default use case for the vsock channel is the following:
 * local vsock communication between guest and host and nested VMs setup.
 * In addition to this, implicitly, the vsock packets are forwarded to the host
 * if no host->guest vsock transport is set.
 *
 * Set this flag value in the sockaddr_vm corresponding field if the vsock
 * packets need to be always forwarded to the host. Using this behavior,
 * vsock communication between sibling VMs can be setup.
 *
 * This way can explicitly distinguish between vsock channels created for
 * different use cases, such as nested VMs (or local communication between
 * guest and host) and sibling VMs.
 *
 * The flag can be set in the connect logic in the user space application flow.
 * In the listen logic (from kernel space) the flag is set on the remote peer
 * address. This happens for an incoming connection when it is routed from the
 * host and comes from the guest (local CID and remote CID > VMADDR_CID_HOST).
 */
#define VMADDR_FLAG_TO_HOST 0x01

/* Invalid vSockets version. */

#define VM_SOCKETS_INVALID_VERSION -1U

/* The epoch (first) component of the vSockets version.  A single byte
 * representing the epoch component of the vSockets version.
 */

#define VM_SOCKETS_VERSION_EPOCH(_v) (((_v) & 0xFF000000) >> 24)

/* The major (second) component of the vSockets version.   A single byte
 * representing the major component of the vSockets version.  Typically
 * changes for every major release of a product.
 */

#define VM_SOCKETS_VERSION_MAJOR(_v) (((_v) & 0x00FF0000) >> 16)

/* The minor (third) component of the vSockets version.  Two bytes representing
 * the minor component of the vSockets version.
 */

#define VM_SOCKETS_VERSION_MINOR(_v) (((_v) & 0x0000FFFF))

/* Address structure for vSockets.   The address family should be set to
 * AF_VSOCK.  The structure members should all align on their natural
 * boundaries without resorting to compiler packing directives.  The total size
 * of this structure should be exactly the same as that of struct sockaddr.
 */

struct sockaddr_vm {
	__kernel_sa_family_t svm_family;
	unsigned short svm_reserved1;
	unsigned int svm_port;
	unsigned int svm_cid;
	__u8 svm_flags;
	unsigned char svm_zero[sizeof(struct sockaddr) -
			       sizeof(sa_family_t) -
			       sizeof(unsigned short) -
			       sizeof(unsigned int) -
			       sizeof(unsigned int) -
			       sizeof(__u8)];
};

#define IOCTL_VM_SOCKETS_GET_LOCAL_CID		_IO(7, 0xb9)

#endif /* _UAPI_VM_SOCKETS_H */
