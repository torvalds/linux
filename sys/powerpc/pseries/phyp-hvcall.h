/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Andreas Tobler
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_PSERIES_PHYP_HVCALL_H_
#define	_PSERIES_PHYP_HVCALL_H_

/* Information taken from: Power.org PAPR, Version 2.4 (December 7, 2009). */

#include <sys/types.h>

/* Return codes. */

#define H_SUCCESS       0
#define H_BUSY          1  /* Hardware Busy -- Retry Later. */
#define H_CLOSED        2  /* Virtual I/O connection is closed. */
#define H_NOT_AVAILABLE 3
#define H_CONSTRAINED   4  /* The request called for resources in excess of
			      the maximum allowed. The resultant allocation
			      was constrained to maximum allowed. */
#define H_PARTIAL       5  /* The request completed only partially successful.
			      Parameters were valid but some specific hcall
			      function condition prevented fully completing the
			      architected function, see the specific hcall
			      definition for possible reasons. */
#define H_IN_PROGRESS     14
#define H_PAGE_REGISTERED 15
#define H_PARTIAL_STORE   16
#define H_PENDING         17
#define H_CONTINUE        18

#define H_LONG_BUSY_ORDER_1_MS   9900  /* This return code is identical to
					  H_BUSY, but with the added bonus of a
					  hint to the partition OS. If the
					  partition OS can delay for 1
					  millisecond, the hcall will likely
					  succeed on a new hcall with no further
					  busy return codes. If the partition OS
					  cannot handle a delay, they are
					  certainly free to immediately turn
					  around and try again. */
#define H_LONG_BUSY_ORDER_10_MS  9901  /* Similar to H_LONG_BUSY_ORDER_1_MS, but
					  the hint is 10mSec wait this time. */

#define H_LONG_BUSY_ORDER_100_MS 9902  /* Similar to H_LONG_BUSY_ORDER_1_MS, but
					  the hint is 100mSec wait this time. */ 

#define H_LONG_BUSY_ORDER_1_S    9903  /* Similar to H_LONG_BUSY_ORDER_1_MS, but
					  the hint is 1Sec wait this time. */
#define H_LONG_BUSY_ORDER_10_S   9904  /* Similar to H_LONG_BUSY_ORDER_1_MS, but
					  the hint is 10Sec wait this time. */
#define H_LONG_BUSY_ORDER_100_S  9905  /* Similar to H_LONG_BUSY_ORDER_1_MS, but
					  the hint is 100Sec wait this time. */

#define H_HARDWARE   -1  /* Error. */
#define H_FUNCTION   -2  /* Not supported. */
#define H_PRIVILEGE  -3  /* Caller not in privileged mode. */
#define H_PARAMETER  -4  /* Outside valid range for partition or conflicting. */
#define H_BAD_MODE   -5  /* Illegal MSR value. */
#define H_PTEG_FULL  -6  /* The requested pteg was full. */
#define H_NOT_FOUND  -7  /* The requested entitiy was not found. */
#define H_RESERVED_DABR -8  /* The requested address is reserved by the
			       hypervisor on this processor. */
#define H_NOMEM      -9
#define H_AUTHORITY -10  /* The caller did not have authority to perform the
			    function. */
#define H_PERMISSION -11  /* The mapping specified by the request does not
			     allow for the requested transfer. */
#define H_DROPPED   -12  /* One or more packets could not be delivered to
			    their requested destinations. */
#define H_S_PARM   -13  /* The source parameter is illegal. */
#define H_D_PARM   -14  /* The destination parameter is illegal. */
#define H_R_PARM   -15  /* The remote TCE mapping is illegal. */
#define H_RESOURCE  -16  /* One or more required resources are in use. */
#define H_ADAPTER_PARM -17  /* Invalid adapter. */
#define H_RH_PARM  -18  /* Resource not valid or logical partition
			   conflicting. */
#define H_RCQ_PARM -19  /* RCQ not valid or logical partition conflicting. */
#define H_SCQ_PARM -20  /* SCQ not valid or logical partition conflicting. */
#define H_EQ_PARM -21  /* EQ not valid or logical partition conflicting. */
#define H_RT_PARM -22  /* Invalid resource type. */
#define H_ST_PARM -23  /* Invalid service type. */
#define H_SIGT_PARM -24 /* Invalid signalling type. */
#define H_TOKEN_PARM -25  /* Invalid token. */
#define H_MLENGTH_PARM -27  /* Invalid memory length. */
#define H_MEM_PARM -28  /* Invalid memory I/O virtual address. */
#define H_MEM_ACCESS_PARM -29  /* Invalid memory access control. */
#define H_ATTR_PARM -30  /* Invalid attribute value. */
#define H_PORT_PARM -31  /* Invalid port number. */
#define H_MCG_PARM -32  /* Invalid multicast group. */
#define H_VL_PARM -33  /* Invalid virtual lane. */
#define H_TSIZE_PARM -34  /* Invalid trace size. */
#define H_TRACE_PARM -35  /* Invalid trace buffer. */
#define H_MASK_PARM -37  /* Invalid mask value. */
#define H_MCG_FULL -38  /* Multicast attachments exceeded. */
#define H_ALIAS_EXIST -39  /* Alias QP already defined. */
#define H_P_COUNTER -40  /* Invalid counter specification. */
#define H_TABLE_FULL -41  /* Resource page table full. */
#define H_ALT_TABLE -42  /* Alternate table already exists / alternate page
			    table not available. */
#define H_MR_CONDITION -43  /* Invalid memory region condition. */
#define H_NOT_ENOUGH_RESOURCES -44  /* Insufficient resources. */
#define H_R_STATE -45  /* Invalid resource state condition or sequencing
			  error. */
#define H_RESCINDED -46
#define H_ABORTED -54
#define H_P2 -55
#define H_P3 -56
#define H_P4 -57
#define H_P5 -58
#define H_P6 -59
#define H_P7 -60
#define H_P8 -61
#define H_P9 -62
#define H_NOOP -63
#define H_TOO_BIG -64

#define H_UNSUPPORTED -67  /* Parameter value outside of the range supported
			      by this implementation. */

/* Flags. */
/* Table 168. Page Frame Table Access flags field definition. */
#define H_EXACT                 (1UL<<(63-24))
#define H_R_XLATE               (1UL<<(63-25))
#define H_READ_4                (1UL<<(63-26))

/* Table 178. CMO Page Usage State flags Definition. */
#define H_PAGE_STATE_CHANGE     (1UL<<(63-28))
#define H_PAGE_UNUSED           ((1UL<<(63-29)) | (1UL<<(63-30)))
#define H_PAGE_SET_UNUSED       (H_PAGE_STATE_CHANGE | H_PAGE_UNUSED)
#define H_PAGE_SET_LOANED       (H_PAGE_SET_UNUSED | (1UL<<(63-31)))
#define H_PAGE_SET_ACTIVE       H_PAGE_STATE_CHANGE

/* Table 168. Page Frame Table Access flags field definition. */
#define H_AVPN                  (1UL<<(63-32))
#define H_ANDCOND               (1UL<<(63-33))

#define H_ICACHE_INVALIDATE     (1UL<<(63-40))
#define H_ICACHE_SYNCHRONIZE    (1UL<<(63-41))

#define H_ZERO_PAGE             (1UL<<(63-48))
#define H_COPY_PAGE             (1UL<<(63-49))

#define H_N (1UL<<(63-61))
#define H_PP1 (1UL<<(63-62))
#define H_PP2 (1UL<<(63-63))

/* pSeries hypervisor opcodes. */
#define H_REMOVE		0x04
#define H_ENTER			0x08
#define H_READ			0x0c
#define H_CLEAR_MOD		0x10
#define H_CLEAR_REF		0x14
#define H_PROTECT		0x18
#define H_GET_TCE		0x1c
#define H_PUT_TCE		0x20
#define H_SET_SPRG0		0x24
#define H_SET_DABR		0x28
#define H_PAGE_INIT		0x2c
#define H_SET_ASR		0x30
#define H_ASR_ON		0x34
#define H_ASR_OFF		0x38
#define H_LOGICAL_CI_LOAD	0x3c
#define H_LOGICAL_CI_STORE	0x40
#define H_LOGICAL_CACHE_LOAD	0x44
#define H_LOGICAL_CACHE_STORE	0x48
#define H_LOGICAL_ICBI		0x4c
#define H_LOGICAL_DCBF		0x50
#define H_GET_TERM_CHAR		0x54
#define H_PUT_TERM_CHAR		0x58
#define H_REAL_TO_LOGICAL	0x5c
#define H_HYPERVISOR_DATA	0x60
#define H_EOI			0x64
#define H_CPPR			0x68
#define H_IPI			0x6c
#define H_IPOLL			0x70
#define H_XIRR			0x74
#define H_MIGRATE_DMA		0x78
#define H_PERFMON		0x7c
#define H_REGISTER_VPA		0xdc
#define H_CEDE			0xe0
#define H_CONFER		0xe4
#define H_PROD			0xe8
#define H_GET_PPP		0xec
#define H_SET_PPP		0xf0
#define H_PURR			0xf4
#define H_PIC			0xf8
#define H_REG_CRQ		0xfc
#define H_FREE_CRQ		0x100
#define H_VIO_SIGNAL		0x104
#define H_SEND_CRQ		0x108
#define H_PUT_RTCE              0x10c
#define H_COPY_RDMA		0x110
#define H_REGISTER_LOGICAL_LAN	0x114
#define H_FREE_LOGICAL_LAN	0x118
#define H_ADD_LOGICAL_LAN_BUFFER 0x11c
#define H_SEND_LOGICAL_LAN	0x120
#define H_BULK_REMOVE		0x124
#define H_WRITE_RDMA            0x128
#define H_READ_RDMA             0x12c
#define H_MULTICAST_CTRL	0x130
#define H_SET_XDABR		0x134
#define H_STUFF_TCE		0x138
#define H_PUT_TCE_INDIRECT	0x13c
#define H_PUT_RTCE_INDIRECT	0x140
#define H_CHANGE_LOGICAL_LAN_MAC 0x14c
#define H_VTERM_PARTNER_INFO	0x150
#define H_REGISTER_VTERM	0x154
#define H_FREE_VTERM		0x158
/* Reserved ....
#define H_RESET_EVENTS          0x15c
#define H_ALLOC_RESOURCE        0x160
#define H_FREE_RESOURCE         0x164
#define H_MODIFY_QP             0x168
#define H_QUERY_QP              0x16c
#define H_REREGISTER_PMR        0x170
#define H_REGISTER_SMR          0x174
#define H_QUERY_MR              0x178
#define H_QUERY_MW              0x17c
#define H_QUERY_HCA             0x180
#define H_QUERY_PORT            0x184
#define H_MODIFY_PORT           0x188
#define H_DEFINE_AQP1           0x18c
#define H_GET_TRACE_BUFFER      0x190
#define H_DEFINE_AQP0           0x194
#define H_RESIZE_MR             0x198
#define H_ATTACH_MCQP           0x19c
#define H_DETACH_MCQP           0x1a0
#define H_CREATE_RPT            0x1a4
#define H_REMOVE_RPT            0x1a8
#define H_REGISTER_RPAGES       0x1ac
#define H_DISABLE_AND_GETC      0x1b0
#define H_ERROR_DATA            0x1b4
#define H_GET_HCA_INFO          0x1b8
#define H_GET_PERF_COUNT        0x1bc
#define H_MANAGE_TRACE          0x1c0
.... */
#define H_FREE_LOGICAL_LAN_BUFFER 0x1d4
#define H_POLL_PENDING		0x1d8
/* Reserved ....
#define H_QUERY_INT_STATE       0x1e4
.... */
#define H_LIOBN_ATTRIBUTES	0x240
#define H_ILLAN_ATTRIBUTES	0x244
#define H_REMOVE_RTCE	        0x24c
/* Reserved ...
#define H_MODIFY_HEA_QP		0x250
#define H_QUERY_HEA_QP		0x254
#define H_QUERY_HEA		0x258
#define H_QUERY_HEA_PORT	0x25c
#define H_MODIFY_HEA_PORT	0x260
#define H_REG_BCMC		0x264
#define H_DEREG_BCMC		0x268
#define H_REGISTER_HEA_RPAGES	0x26c
#define H_DISABLE_AND_GET_HEA	0x270
#define H_GET_HEA_INFO		0x274
#define H_ALLOC_HEA_RESOURCE	0x278
#define H_ADD_CONN		0x284
#define H_DEL_CONN		0x288
... */
#define H_JOIN			0x298
#define H_DONOR_OPERATION	0x29c
#define H_VASI_SIGNAL	       	0x2a0
#define H_VASI_STATE            0x2a4
#define H_VIOCTL	       	0x2a8
#define H_VRMASD	       	0x2ac
#define H_ENABLE_CRQ		0x2b0
/* Reserved ...
#define H_GET_EM_PARMS		0x2b8
... */
#define H_VPM_STAT	       	0x2bc
#define H_SET_MPP		0x2d0
#define H_GET_MPP		0x2d4
#define H_MO_PERF		0x2d8
#define H_REG_SUB_CRQ		0x2dc
#define H_FREE_SUB_CRQ		0x2e0
#define H_SEND_SUB_CRQ		0x2e4
#define H_SEND_SUB_CRQ_IND	0x2e8
#define H_HOME_NODE_ASSOC	0x2ec
/* Reserved ... */
#define H_BEST_ENERGY		0x2f4
#define H_REG_SNS		0x2f8
#define H_X_XIRR		0x2fc
#define H_RANDOM		0x300
/* Reserved ... */
#define H_COP_OP		0x304
#define H_STOP_COP_OP		0x308
#define H_GET_MPP_X		0x314
#define H_SET_MODE		0x31C
/* Reserved ... */
#define H_GET_DMA_XLATES_L	0x324
#define MAX_HCALL_OPCODE	H_GET_DMA_XLATES_L

int64_t phyp_hcall(uint64_t opcode, ...);
int64_t phyp_pft_hcall(uint64_t opcode, uint64_t flags, uint64_t pteidx,
    uint64_t pte_hi, uint64_t pte_lo, uint64_t *pteidx_out, uint64_t *ptelo_out,
    uint64_t *r6);

#endif /* _PSERIES_PHYP_HVCALL_H_ */

