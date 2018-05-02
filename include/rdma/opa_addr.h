/*
 * Copyright(c) 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef OPA_ADDR_H
#define OPA_ADDR_H

#include <rdma/opa_smi.h>

#define	OPA_SPECIAL_OUI		(0x00066AULL)
#define OPA_MAKE_ID(x)          (cpu_to_be64(OPA_SPECIAL_OUI << 40 | (x)))
#define OPA_TO_IB_UCAST_LID(x) (((x) >= be16_to_cpu(IB_MULTICAST_LID_BASE)) \
				? 0 : x)
#define OPA_GID_INDEX		0x1
/**
 * 0xF8 - 4 bits of multicast range and 1 bit for collective range
 * Example: For 24 bit LID space,
 * Multicast range: 0xF00000 to 0xF7FFFF
 * Collective range: 0xF80000 to 0xFFFFFE
 */
#define OPA_MCAST_NR 0x4 /* Number of top bits set */
#define OPA_COLLECTIVE_NR 0x1 /* Number of bits after MCAST_NR */

/**
 * ib_is_opa_gid: Returns true if the top 24 bits of the gid
 * contains the OPA_STL_OUI identifier. This identifies that
 * the provided gid is a special purpose GID meant to carry
 * extended LID information.
 *
 * @gid: The Global identifier
 */
static inline bool ib_is_opa_gid(const union ib_gid *gid)
{
	return ((be64_to_cpu(gid->global.interface_id) >> 40) ==
		OPA_SPECIAL_OUI);
}

/**
 * opa_get_lid_from_gid: Returns the last 32 bits of the gid.
 * OPA devices use one of the gids in the gid table to also
 * store the lid.
 *
 * @gid: The Global identifier
 */
static inline u32 opa_get_lid_from_gid(const union ib_gid *gid)
{
	return be64_to_cpu(gid->global.interface_id) & 0xFFFFFFFF;
}

/**
 * opa_is_extended_lid: Returns true if dlid or slid are
 * extended.
 *
 * @dlid: The DLID
 * @slid: The SLID
 */
static inline bool opa_is_extended_lid(__be32 dlid, __be32 slid)
{
	if ((be32_to_cpu(dlid) >=
	     be16_to_cpu(IB_MULTICAST_LID_BASE)) ||
	    (be32_to_cpu(slid) >=
	     be16_to_cpu(IB_MULTICAST_LID_BASE)))
		return true;

	return false;
}

/* Get multicast lid base */
static inline u32 opa_get_mcast_base(u32 nr_top_bits)
{
	return (be32_to_cpu(OPA_LID_PERMISSIVE) << (32 - nr_top_bits));
}

/* Check for a valid unicast LID for non-SM traffic types */
static inline bool rdma_is_valid_unicast_lid(struct rdma_ah_attr *attr)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_IB) {
		if (!rdma_ah_get_dlid(attr) ||
		    rdma_ah_get_dlid(attr) >=
		    be32_to_cpu(IB_MULTICAST_LID_BASE))
			return false;
	} else if (attr->type == RDMA_AH_ATTR_TYPE_OPA) {
		if (!rdma_ah_get_dlid(attr) ||
		    rdma_ah_get_dlid(attr) >=
		    opa_get_mcast_base(OPA_MCAST_NR))
			return false;
	}
	return true;
}
#endif /* OPA_ADDR_H */
