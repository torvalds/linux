/*
 * Copyright (c) 2010 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if !defined(_RDMA_IB_H)
#define _RDMA_IB_H

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cred.h>

struct ib_addr {
	union {
		__u8		uib_addr8[16];
		__be16		uib_addr16[8];
		__be32		uib_addr32[4];
		__be64		uib_addr64[2];
	} ib_u;
#define sib_addr8		ib_u.uib_addr8
#define sib_addr16		ib_u.uib_addr16
#define sib_addr32		ib_u.uib_addr32
#define sib_addr64		ib_u.uib_addr64
#define sib_raw			ib_u.uib_addr8
#define sib_subnet_prefix	ib_u.uib_addr64[0]
#define sib_interface_id	ib_u.uib_addr64[1]
};

static inline int ib_addr_any(const struct ib_addr *a)
{
	return ((a->sib_addr64[0] | a->sib_addr64[1]) == 0);
}

static inline int ib_addr_loopback(const struct ib_addr *a)
{
	return ((a->sib_addr32[0] | a->sib_addr32[1] |
		 a->sib_addr32[2] | (a->sib_addr32[3] ^ htonl(1))) == 0);
}

static inline void ib_addr_set(struct ib_addr *addr,
			       __be32 w1, __be32 w2, __be32 w3, __be32 w4)
{
	addr->sib_addr32[0] = w1;
	addr->sib_addr32[1] = w2;
	addr->sib_addr32[2] = w3;
	addr->sib_addr32[3] = w4;
}

static inline int ib_addr_cmp(const struct ib_addr *a1, const struct ib_addr *a2)
{
	return memcmp(a1, a2, sizeof(struct ib_addr));
}

struct sockaddr_ib {
	unsigned short int	sib_family;	/* AF_IB */
	__be16			sib_pkey;
	__be32			sib_flowinfo;
	struct ib_addr		sib_addr;
	__be64			sib_sid;
	__be64			sib_sid_mask;
	__u64			sib_scope_id;
};

/*
 * The IB interfaces that use write() as bi-directional ioctl() are
 * fundamentally unsafe, since there are lots of ways to trigger "write()"
 * calls from various contexts with elevated privileges. That includes the
 * traditional suid executable error message writes, but also various kernel
 * interfaces that can write to file descriptors.
 *
 * This function provides protection for the legacy API by restricting the
 * calling context.
 */
static inline bool ib_safe_file_access(struct file *filp)
{
	return filp->f_cred == current_cred() && segment_eq(get_fs(), USER_DS);
}

#endif /* _RDMA_IB_H */
