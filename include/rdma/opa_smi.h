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

#define OPA_MAX_VLS				32
#define OPA_MAX_SLS				32
#define OPA_MAX_SCS				32

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


/* Subnet management attributes */
/* ... */
#define OPA_ATTRIB_ID_NODE_DESCRIPTION		cpu_to_be16(0x0010)
#define OPA_ATTRIB_ID_NODE_INFO			cpu_to_be16(0x0011)
#define OPA_ATTRIB_ID_PORT_INFO			cpu_to_be16(0x0015)
#define OPA_ATTRIB_ID_PARTITION_TABLE		cpu_to_be16(0x0016)
#define OPA_ATTRIB_ID_SL_TO_SC_MAP		cpu_to_be16(0x0017)
#define OPA_ATTRIB_ID_VL_ARBITRATION		cpu_to_be16(0x0018)
#define OPA_ATTRIB_ID_SM_INFO			cpu_to_be16(0x0020)
#define OPA_ATTRIB_ID_CABLE_INFO		cpu_to_be16(0x0032)
#define OPA_ATTRIB_ID_AGGREGATE			cpu_to_be16(0x0080)
#define OPA_ATTRIB_ID_SC_TO_SL_MAP		cpu_to_be16(0x0082)
#define OPA_ATTRIB_ID_SC_TO_VLR_MAP		cpu_to_be16(0x0083)
#define OPA_ATTRIB_ID_SC_TO_VLT_MAP		cpu_to_be16(0x0084)
#define OPA_ATTRIB_ID_SC_TO_VLNT_MAP		cpu_to_be16(0x0085)
/* ... */
#define OPA_ATTRIB_ID_PORT_STATE_INFO		cpu_to_be16(0x0087)
/* ... */
#define OPA_ATTRIB_ID_BUFFER_CONTROL_TABLE	cpu_to_be16(0x008A)
/* ... */

struct opa_node_description {
	u8 data[64];
} __packed;

struct opa_node_info {
	u8      base_version;
	u8      class_version;
	u8      node_type;
	u8      num_ports;
	__be32  reserved;
	__be64  system_image_guid;
	__be64  node_guid;
	__be64  port_guid;
	__be16  partition_cap;
	__be16  device_id;
	__be32  revision;
	u8      local_port_num;
	u8      vendor_id[3];   /* network byte order */
} __packed;

#define OPA_PARTITION_TABLE_BLK_SIZE 32

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
