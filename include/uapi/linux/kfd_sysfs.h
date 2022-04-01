/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT */
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef KFD_SYSFS_H_INCLUDED
#define KFD_SYSFS_H_INCLUDED

/* Capability bits in node properties */
#define HSA_CAP_HOT_PLUGGABLE			0x00000001
#define HSA_CAP_ATS_PRESENT			0x00000002
#define HSA_CAP_SHARED_WITH_GRAPHICS		0x00000004
#define HSA_CAP_QUEUE_SIZE_POW2			0x00000008
#define HSA_CAP_QUEUE_SIZE_32BIT		0x00000010
#define HSA_CAP_QUEUE_IDLE_EVENT		0x00000020
#define HSA_CAP_VA_LIMIT			0x00000040
#define HSA_CAP_WATCH_POINTS_SUPPORTED		0x00000080
#define HSA_CAP_WATCH_POINTS_TOTALBITS_MASK	0x00000f00
#define HSA_CAP_WATCH_POINTS_TOTALBITS_SHIFT	8
#define HSA_CAP_DOORBELL_TYPE_TOTALBITS_MASK	0x00003000
#define HSA_CAP_DOORBELL_TYPE_TOTALBITS_SHIFT	12

#define HSA_CAP_DOORBELL_TYPE_PRE_1_0		0x0
#define HSA_CAP_DOORBELL_TYPE_1_0		0x1
#define HSA_CAP_DOORBELL_TYPE_2_0		0x2
#define HSA_CAP_AQL_QUEUE_DOUBLE_MAP		0x00004000

/* Old buggy user mode depends on this being 0 */
#define HSA_CAP_RESERVED_WAS_SRAM_EDCSUPPORTED	0x00080000

#define HSA_CAP_MEM_EDCSUPPORTED		0x00100000
#define HSA_CAP_RASEVENTNOTIFY			0x00200000
#define HSA_CAP_ASIC_REVISION_MASK		0x03c00000
#define HSA_CAP_ASIC_REVISION_SHIFT		22
#define HSA_CAP_SRAM_EDCSUPPORTED		0x04000000
#define HSA_CAP_SVMAPI_SUPPORTED		0x08000000
#define HSA_CAP_FLAGS_COHERENTHOSTACCESS	0x10000000
#define HSA_CAP_RESERVED			0xe00f8000

/* Heap types in memory properties */
#define HSA_MEM_HEAP_TYPE_SYSTEM	0
#define HSA_MEM_HEAP_TYPE_FB_PUBLIC	1
#define HSA_MEM_HEAP_TYPE_FB_PRIVATE	2
#define HSA_MEM_HEAP_TYPE_GPU_GDS	3
#define HSA_MEM_HEAP_TYPE_GPU_LDS	4
#define HSA_MEM_HEAP_TYPE_GPU_SCRATCH	5

/* Flag bits in memory properties */
#define HSA_MEM_FLAGS_HOT_PLUGGABLE		0x00000001
#define HSA_MEM_FLAGS_NON_VOLATILE		0x00000002
#define HSA_MEM_FLAGS_RESERVED			0xfffffffc

/* Cache types in cache properties */
#define HSA_CACHE_TYPE_DATA		0x00000001
#define HSA_CACHE_TYPE_INSTRUCTION	0x00000002
#define HSA_CACHE_TYPE_CPU		0x00000004
#define HSA_CACHE_TYPE_HSACU		0x00000008
#define HSA_CACHE_TYPE_RESERVED		0xfffffff0

/* Link types in IO link properties (matches CRAT link types) */
#define HSA_IOLINK_TYPE_UNDEFINED	0
#define HSA_IOLINK_TYPE_HYPERTRANSPORT	1
#define HSA_IOLINK_TYPE_PCIEXPRESS	2
#define HSA_IOLINK_TYPE_AMBA		3
#define HSA_IOLINK_TYPE_MIPI		4
#define HSA_IOLINK_TYPE_QPI_1_1	5
#define HSA_IOLINK_TYPE_RESERVED1	6
#define HSA_IOLINK_TYPE_RESERVED2	7
#define HSA_IOLINK_TYPE_RAPID_IO	8
#define HSA_IOLINK_TYPE_INFINIBAND	9
#define HSA_IOLINK_TYPE_RESERVED3	10
#define HSA_IOLINK_TYPE_XGMI		11
#define HSA_IOLINK_TYPE_XGOP		12
#define HSA_IOLINK_TYPE_GZ		13
#define HSA_IOLINK_TYPE_ETHERNET_RDMA	14
#define HSA_IOLINK_TYPE_RDMA_OTHER	15
#define HSA_IOLINK_TYPE_OTHER		16

/* Flag bits in IO link properties (matches CRAT flags, excluding the
 * bi-directional flag, which is not offially part of the CRAT spec, and
 * only used internally in KFD)
 */
#define HSA_IOLINK_FLAGS_ENABLED		(1 << 0)
#define HSA_IOLINK_FLAGS_NON_COHERENT		(1 << 1)
#define HSA_IOLINK_FLAGS_NO_ATOMICS_32_BIT	(1 << 2)
#define HSA_IOLINK_FLAGS_NO_ATOMICS_64_BIT	(1 << 3)
#define HSA_IOLINK_FLAGS_NO_PEER_TO_PEER_DMA	(1 << 4)
#define HSA_IOLINK_FLAGS_RESERVED		0xffffffe0

#endif
