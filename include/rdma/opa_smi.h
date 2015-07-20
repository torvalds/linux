/*
 * Copyright (c) 2014 Intel Corporation.  All rights reserved.
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

#if !defined(OPA_SMI_H)
#define OPA_SMI_H

#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>

#define OPA_SMP_LID_DATA_SIZE			2016
#define OPA_SMP_DR_DATA_SIZE			1872
#define OPA_SMP_MAX_PATH_HOPS			64

#define OPA_SMI_CLASS_VERSION			0x80

#define OPA_LID_PERMISSIVE			cpu_to_be32(0xFFFFFFFF)

struct opa_smp {
	u8	base_version;
	u8	mgmt_class;
	u8	class_version;
	u8	method;
	__be16	status;
	u8	hop_ptr;
	u8	hop_cnt;
	__be64	tid;
	__be16	attr_id;
	__be16	resv;
	__be32	attr_mod;
	__be64	mkey;
	union {
		struct {
			uint8_t data[OPA_SMP_LID_DATA_SIZE];
		} lid;
		struct {
			__be32	dr_slid;
			__be32	dr_dlid;
			u8	initial_path[OPA_SMP_MAX_PATH_HOPS];
			u8	return_path[OPA_SMP_MAX_PATH_HOPS];
			u8	reserved[8];
			u8	data[OPA_SMP_DR_DATA_SIZE];
		} dr;
	} route;
} __packed;


static inline u8
opa_get_smp_direction(struct opa_smp *smp)
{
	return ib_get_smp_direction((struct ib_smp *)smp);
}

static inline u8 *opa_get_smp_data(struct opa_smp *smp)
{
	if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		return smp->route.dr.data;

	return smp->route.lid.data;
}

static inline size_t opa_get_smp_data_size(struct opa_smp *smp)
{
	if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		return sizeof(smp->route.dr.data);

	return sizeof(smp->route.lid.data);
}

static inline size_t opa_get_smp_header_size(struct opa_smp *smp)
{
	if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		return sizeof(*smp) - sizeof(smp->route.dr.data);

	return sizeof(*smp) - sizeof(smp->route.lid.data);
}

#endif /* OPA_SMI_H */
