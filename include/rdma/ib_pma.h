/*
 * Copyright (c) 2006, 2007, 2008, 2009, 2010 QLogic Corporation.
 * All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#if !defined(IB_PMA_H)
#define IB_PMA_H

#include <rdma/ib_mad.h>

/*
 * PMA class portinfo capability mask bits
 */
#define IB_PMA_CLASS_CAP_ALLPORTSELECT  cpu_to_be16(1 << 8)
#define IB_PMA_CLASS_CAP_EXT_WIDTH      cpu_to_be16(1 << 9)
#define IB_PMA_CLASS_CAP_XMIT_WAIT      cpu_to_be16(1 << 12)

#define IB_PMA_CLASS_PORT_INFO          cpu_to_be16(0x0001)
#define IB_PMA_PORT_SAMPLES_CONTROL     cpu_to_be16(0x0010)
#define IB_PMA_PORT_SAMPLES_RESULT      cpu_to_be16(0x0011)
#define IB_PMA_PORT_COUNTERS            cpu_to_be16(0x0012)
#define IB_PMA_PORT_COUNTERS_EXT        cpu_to_be16(0x001D)
#define IB_PMA_PORT_SAMPLES_RESULT_EXT  cpu_to_be16(0x001E)

struct ib_pma_mad {
	struct ib_mad_hdr mad_hdr;
	u8 reserved[40];
	u8 data[192];
} __packed;

struct ib_pma_portsamplescontrol {
	u8 opcode;
	u8 port_select;
	u8 tick;
	u8 counter_width;		/* resv: 7:3, counter width: 2:0 */
	__be32 counter_mask0_9;		/* 2, 10 3-bit fields */
	__be16 counter_mask10_14;	/* 1, 5 3-bit fields */
	u8 sample_mechanisms;
	u8 sample_status;		/* only lower 2 bits */
	__be64 option_mask;
	__be64 vendor_mask;
	__be32 sample_start;
	__be32 sample_interval;
	__be16 tag;
	__be16 counter_select[15];
	__be32 reserved1;
	__be64 samples_only_option_mask;
	__be32 reserved2[28];
};

struct ib_pma_portsamplesresult {
	__be16 tag;
	__be16 sample_status;   /* only lower 2 bits */
	__be32 counter[15];
};

struct ib_pma_portsamplesresult_ext {
	__be16 tag;
	__be16 sample_status;   /* only lower 2 bits */
	__be32 extended_width;  /* only upper 2 bits */
	__be64 counter[15];
};

struct ib_pma_portcounters {
	u8 reserved;
	u8 port_select;
	__be16 counter_select;
	__be16 symbol_error_counter;
	u8 link_error_recovery_counter;
	u8 link_downed_counter;
	__be16 port_rcv_errors;
	__be16 port_rcv_remphys_errors;
	__be16 port_rcv_switch_relay_errors;
	__be16 port_xmit_discards;
	u8 port_xmit_constraint_errors;
	u8 port_rcv_constraint_errors;
	u8 reserved1;
	u8 link_overrun_errors; /* LocalLink: 7:4, BufferOverrun: 3:0 */
	__be16 reserved2;
	__be16 vl15_dropped;
	__be32 port_xmit_data;
	__be32 port_rcv_data;
	__be32 port_xmit_packets;
	__be32 port_rcv_packets;
	__be32 port_xmit_wait;
} __packed;


#define IB_PMA_SEL_SYMBOL_ERROR                 cpu_to_be16(0x0001)
#define IB_PMA_SEL_LINK_ERROR_RECOVERY          cpu_to_be16(0x0002)
#define IB_PMA_SEL_LINK_DOWNED                  cpu_to_be16(0x0004)
#define IB_PMA_SEL_PORT_RCV_ERRORS              cpu_to_be16(0x0008)
#define IB_PMA_SEL_PORT_RCV_REMPHYS_ERRORS      cpu_to_be16(0x0010)
#define IB_PMA_SEL_PORT_XMIT_DISCARDS           cpu_to_be16(0x0040)
#define IB_PMA_SEL_LOCAL_LINK_INTEGRITY_ERRORS  cpu_to_be16(0x0200)
#define IB_PMA_SEL_EXCESSIVE_BUFFER_OVERRUNS    cpu_to_be16(0x0400)
#define IB_PMA_SEL_PORT_VL15_DROPPED            cpu_to_be16(0x0800)
#define IB_PMA_SEL_PORT_XMIT_DATA               cpu_to_be16(0x1000)
#define IB_PMA_SEL_PORT_RCV_DATA                cpu_to_be16(0x2000)
#define IB_PMA_SEL_PORT_XMIT_PACKETS            cpu_to_be16(0x4000)
#define IB_PMA_SEL_PORT_RCV_PACKETS             cpu_to_be16(0x8000)

struct ib_pma_portcounters_ext {
	u8 reserved;
	u8 port_select;
	__be16 counter_select;
	__be32 reserved1;
	__be64 port_xmit_data;
	__be64 port_rcv_data;
	__be64 port_xmit_packets;
	__be64 port_rcv_packets;
	__be64 port_unicast_xmit_packets;
	__be64 port_unicast_rcv_packets;
	__be64 port_multicast_xmit_packets;
	__be64 port_multicast_rcv_packets;
} __packed;

#define IB_PMA_SELX_PORT_XMIT_DATA              cpu_to_be16(0x0001)
#define IB_PMA_SELX_PORT_RCV_DATA               cpu_to_be16(0x0002)
#define IB_PMA_SELX_PORT_XMIT_PACKETS           cpu_to_be16(0x0004)
#define IB_PMA_SELX_PORT_RCV_PACKETS            cpu_to_be16(0x0008)
#define IB_PMA_SELX_PORT_UNI_XMIT_PACKETS       cpu_to_be16(0x0010)
#define IB_PMA_SELX_PORT_UNI_RCV_PACKETS        cpu_to_be16(0x0020)
#define IB_PMA_SELX_PORT_MULTI_XMIT_PACKETS     cpu_to_be16(0x0040)
#define IB_PMA_SELX_PORT_MULTI_RCV_PACKETS      cpu_to_be16(0x0080)

#endif /* IB_PMA_H */
