/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2005
 *      Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _RESOURCE_VAR_H_
#define	_RESOURCE_VAR_H_

typedef int cm_resource_type;

struct physaddr {
	uint64_t		np_quad;
#ifdef notdef
	uint32_t		np_low;
	uint32_t		np_high;
#endif
};

typedef struct physaddr physaddr;

enum interface_type {
	InterfaceTypeUndefined = -1,
	Internal,
	Isa,
	Eisa,
	MicroChannel,
	TurboChannel,
	PCIBus,
	VMEBus,
	NuBus,
	PCMCIABus,
	CBus,
	MPIBus,
	MPSABus,
	ProcessorInternal,
	InternalPowerBus,
	PNPISABus,
	PNPBus,
	MaximumInterfaceType
};

typedef enum interface_type interface_type;

#define	CmResourceTypeNull                0   /* ResType_All or ResType_None (0x0000) */
#define	CmResourceTypePort                1   /* ResType_IO (0x0002) */
#define	CmResourceTypeInterrupt           2   /* ResType_IRQ (0x0004) */
#define	CmResourceTypeMemory              3   /* ResType_Mem (0x0001) */
#define	CmResourceTypeDma                 4   /* ResType_DMA (0x0003) */
#define	CmResourceTypeDeviceSpecific      5   /* ResType_ClassSpecific (0xFFFF) */
#define	CmResourceTypeBusNumber           6   /* ResType_BusNumber (0x0006) */
#define	CmResourceTypeMaximum             7
#define	CmResourceTypeNonArbitrated     128   /* Not arbitrated if 0x80 bit set */
#define	CmResourceTypeConfigData        128   /* ResType_Reserved (0x8000) */
#define	CmResourceTypeDevicePrivate     129   /* ResType_DevicePrivate (0x8001) */
#define	CmResourceTypePcCardConfig      130   /* ResType_PcCardConfig (0x8002) */

enum cm_share_disposition {
    CmResourceShareUndetermined = 0,    /* Reserved */
    CmResourceShareDeviceExclusive,
    CmResourceShareDriverExclusive,
    CmResourceShareShared
};

typedef enum cm_share_disposition cm_share_disposition;

/* Define the bit masks for Flags when type is CmResourceTypeInterrupt */

#define	CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE	0
#define	CM_RESOURCE_INTERRUPT_LATCHED		1

/* Define the bit masks for Flags when type is CmResourceTypeMemory */

#define	CM_RESOURCE_MEMORY_READ_WRITE		0x0000
#define	CM_RESOURCE_MEMORY_READ_ONLY		0x0001
#define	CM_RESOURCE_MEMORY_WRITE_ONLY		0x0002
#define	CM_RESOURCE_MEMORY_PREFETCHABLE		0x0004

#define	CM_RESOURCE_MEMORY_COMBINEDWRITE	0x0008
#define	CM_RESOURCE_MEMORY_24			0x0010
#define	CM_RESOURCE_MEMORY_CACHEABLE		0x0020

/* Define the bit masks for Flags when type is CmResourceTypePort */

#define	CM_RESOURCE_PORT_MEMORY			0x0000
#define	CM_RESOURCE_PORT_IO			0x0001
#define	CM_RESOURCE_PORT_10_BIT_DECODE		0x0004
#define	CM_RESOURCE_PORT_12_BIT_DECODE		0x0008
#define	CM_RESOURCE_PORT_16_BIT_DECODE		0x0010
#define	CM_RESOURCE_PORT_POSITIVE_DECODE	0x0020
#define	CM_RESOURCE_PORT_PASSIVE_DECODE		0x0040
#define	CM_RESOURCE_PORT_WINDOW_DECODE		0x0080

/* Define the bit masks for Flags when type is CmResourceTypeDma */

#define	CM_RESOURCE_DMA_8			0x0000
#define	CM_RESOURCE_DMA_16			0x0001
#define	CM_RESOURCE_DMA_32			0x0002
#define	CM_RESOURCE_DMA_8_AND_16		0x0004
#define	CM_RESOURCE_DMA_BUS_MASTER		0x0008
#define	CM_RESOURCE_DMA_TYPE_A			0x0010
#define	CM_RESOURCE_DMA_TYPE_B			0x0020
#define	CM_RESOURCE_DMA_TYPE_F			0x0040

struct cm_partial_resource_desc {
	uint8_t			cprd_type;
	uint8_t			cprd_sharedisp;
	uint16_t		cprd_flags;
	union {
		struct {
			physaddr		cprd_start;
			uint32_t		cprd_len;
		} cprd_generic;
		struct {
			physaddr		cprd_start;
			uint32_t		cprd_len;
		} cprd_port;
		struct {
			uint32_t		cprd_level;
			uint32_t		cprd_vector;
			uint32_t		cprd_affinity;
		} cprd_intr;
		struct {
			physaddr		cprd_start;
			uint32_t		cprd_len;
		} cprd_mem;
		struct {
			uint32_t		cprd_chan;
			uint32_t		cprd_port;
			uint32_t		cprd_rsvd;
		} cprd_dmachan;
		struct {
			uint32_t		cprd_data[3];
		} cprd_devpriv;
		struct {
			uint32_t		cprd_datasize;
			uint32_t		cprd_rsvd1;
			uint32_t		cprd_rsvd2;
		} cprd_devspec;
	} u __attribute__((packed));
};

typedef struct cm_partial_resource_desc cm_partial_resource_desc;

struct cm_partial_resource_list {
	uint16_t		cprl_version;
	uint16_t		cprl_revision;
	uint32_t		cprl_count;
	cm_partial_resource_desc	cprl_partial_descs[1];
};

typedef struct cm_partial_resource_list cm_partial_resource_list;

struct cm_full_resource_list {
	interface_type		cfrl_type;
	uint32_t		cfrl_busnum;
	cm_partial_resource_desc	cfrl_partiallist;
};

typedef struct cm_full_resource_list cm_full_resource_list;

struct cm_resource_list {
	uint32_t		crl_count;
	cm_full_resource_list	crl_rlist;
};

typedef struct cm_resource_list cm_resource_list;

typedef cm_partial_resource_list ndis_resource_list;

#endif /* _RESOURCE_VAR_H_ */
