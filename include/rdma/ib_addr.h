/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 *
 */

#if !defined(IB_ADDR_H)
#define IB_ADDR_H

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <rdma/ib_verbs.h>

struct rdma_dev_addr {
	unsigned char src_dev_addr[MAX_ADDR_LEN];
	unsigned char dst_dev_addr[MAX_ADDR_LEN];
	unsigned char broadcast[MAX_ADDR_LEN];
	enum ib_node_type dev_type;
};

/**
 * rdma_translate_ip - Translate a local IP address to an RDMA hardware
 *   address.
 */
int rdma_translate_ip(struct sockaddr *addr, struct rdma_dev_addr *dev_addr);

/**
 * rdma_resolve_ip - Resolve source and destination IP addresses to
 *   RDMA hardware addresses.
 * @src_addr: An optional source address to use in the resolution.  If a
 *   source address is not provided, a usable address will be returned via
 *   the callback.
 * @dst_addr: The destination address to resolve.
 * @addr: A reference to a data location that will receive the resolved
 *   addresses.  The data location must remain valid until the callback has
 *   been invoked.
 * @timeout_ms: Amount of time to wait for the address resolution to complete.
 * @callback: Call invoked once address resolution has completed, timed out,
 *   or been canceled.  A status of 0 indicates success.
 * @context: User-specified context associated with the call.
 */
int rdma_resolve_ip(struct sockaddr *src_addr, struct sockaddr *dst_addr,
		    struct rdma_dev_addr *addr, int timeout_ms,
		    void (*callback)(int status, struct sockaddr *src_addr,
				     struct rdma_dev_addr *addr, void *context),
		    void *context);

void rdma_addr_cancel(struct rdma_dev_addr *addr);

static inline int ip_addr_size(struct sockaddr *addr)
{
	return addr->sa_family == AF_INET6 ?
	       sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
}

static inline u16 ib_addr_get_pkey(struct rdma_dev_addr *dev_addr)
{
	return ((u16)dev_addr->broadcast[8] << 8) | (u16)dev_addr->broadcast[9];
}

static inline void ib_addr_set_pkey(struct rdma_dev_addr *dev_addr, u16 pkey)
{
	dev_addr->broadcast[8] = pkey >> 8;
	dev_addr->broadcast[9] = (unsigned char) pkey;
}

static inline union ib_gid *ib_addr_get_sgid(struct rdma_dev_addr *dev_addr)
{
	return 	(union ib_gid *) (dev_addr->src_dev_addr + 4);
}

static inline void ib_addr_set_sgid(struct rdma_dev_addr *dev_addr,
				    union ib_gid *gid)
{
	memcpy(dev_addr->src_dev_addr + 4, gid, sizeof *gid);
}

static inline union ib_gid *ib_addr_get_dgid(struct rdma_dev_addr *dev_addr)
{
	return 	(union ib_gid *) (dev_addr->dst_dev_addr + 4);
}

static inline void ib_addr_set_dgid(struct rdma_dev_addr *dev_addr,
				    union ib_gid *gid)
{
	memcpy(dev_addr->dst_dev_addr + 4, gid, sizeof *gid);
}

#endif /* IB_ADDR_H */
