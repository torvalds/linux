/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, 2016 Chelsio Communications, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __T4_REGS_VALUES_H__
#define __T4_REGS_VALUES_H__

/*
 * This file contains definitions for various T4 register value hardware
 * constants.  The types of values encoded here are predominantly those for
 * register fields which control "modal" behavior.  For the most part, we do
 * not include definitions for register fields which are simple numeric
 * metrics, etc.
 *
 * These new "modal values" use a naming convention which matches the
 * currently existing macros in t4_reg.h.  For register field FOO which would
 * have S_FOO, M_FOO, V_FOO() and G_FOO() macros, we introduce X_FOO_{MODE}
 * definitions.  These can be used as V_FOO(X_FOO_MODE) or as (G_FOO(x) ==
 * X_FOO_MODE).
 *
 * Note that this should all be part of t4_regs.h but the toolset used to
 * generate that file doesn't [yet] have the capability of collecting these
 * constants.
 */

/*
 * SGE definitions.
 * ================
 */

/*
 * SGE register field values.
 */

/* CONTROL register */
#define X_FLSPLITMODE_FLSPLITMIN	0
#define X_FLSPLITMODE_ETHHDR		1
#define X_FLSPLITMODE_IPHDR		2
#define X_FLSPLITMODE_TCPHDR		3

#define X_DCASYSTYPE_FSB		0
#define X_DCASYSTYPE_CSI		1

#define X_EGSTATPAGESIZE_64B		0
#define X_EGSTATPAGESIZE_128B		1

#define X_RXPKTCPLMODE_DATA		0
#define X_RXPKTCPLMODE_SPLIT		1

#define X_INGPCIEBOUNDARY_SHIFT		5
#define X_INGPCIEBOUNDARY_32B		0
#define X_INGPCIEBOUNDARY_64B		1
#define X_INGPCIEBOUNDARY_128B		2
#define X_INGPCIEBOUNDARY_256B		3
#define X_INGPCIEBOUNDARY_512B		4
#define X_INGPCIEBOUNDARY_1024B		5
#define X_INGPCIEBOUNDARY_2048B		6
#define X_INGPCIEBOUNDARY_4096B		7

#define X_T6_INGPADBOUNDARY_SHIFT	3
#define X_T6_INGPADBOUNDARY_8B		0
#define X_T6_INGPADBOUNDARY_16B		1
#define X_T6_INGPADBOUNDARY_32B		2
#define X_T6_INGPADBOUNDARY_64B		3
#define X_T6_INGPADBOUNDARY_128B	4
#define X_T6_INGPADBOUNDARY_256B	5
#define X_T6_INGPADBOUNDARY_512B	6
#define X_T6_INGPADBOUNDARY_1024B	7

#define X_INGPADBOUNDARY_SHIFT		5
#define X_INGPADBOUNDARY_32B		0
#define X_INGPADBOUNDARY_64B		1
#define X_INGPADBOUNDARY_128B		2
#define X_INGPADBOUNDARY_256B		3
#define X_INGPADBOUNDARY_512B		4
#define X_INGPADBOUNDARY_1024B		5
#define X_INGPADBOUNDARY_2048B		6
#define X_INGPADBOUNDARY_4096B		7

#define X_EGRPCIEBOUNDARY_SHIFT		5
#define X_EGRPCIEBOUNDARY_32B		0
#define X_EGRPCIEBOUNDARY_64B		1
#define X_EGRPCIEBOUNDARY_128B		2
#define X_EGRPCIEBOUNDARY_256B		3
#define X_EGRPCIEBOUNDARY_512B		4
#define X_EGRPCIEBOUNDARY_1024B		5
#define X_EGRPCIEBOUNDARY_2048B		6
#define X_EGRPCIEBOUNDARY_4096B		7

/* CONTROL2 register */
#define X_INGPACKBOUNDARY_SHIFT		5	// *most* of the values ...
#define X_INGPACKBOUNDARY_16B		0	// Note weird value!
#define X_INGPACKBOUNDARY_64B		1
#define X_INGPACKBOUNDARY_128B		2
#define X_INGPACKBOUNDARY_256B		3
#define X_INGPACKBOUNDARY_512B		4
#define X_INGPACKBOUNDARY_1024B		5
#define X_INGPACKBOUNDARY_2048B		6
#define X_INGPACKBOUNDARY_4096B		7

/* GTS register */
#define SGE_TIMERREGS			6
#define X_TIMERREG_COUNTER0		0
#define X_TIMERREG_COUNTER1		1
#define X_TIMERREG_COUNTER2		2
#define X_TIMERREG_COUNTER3		3
#define X_TIMERREG_COUNTER4		4
#define X_TIMERREG_COUNTER5		5
#define X_TIMERREG_RESTART_COUNTER	6
#define X_TIMERREG_UPDATE_CIDX		7

/*
 * Egress Context field values
 */
#define EC_WR_UNITS			16

#define X_FETCHBURSTMIN_SHIFT		4
#define X_FETCHBURSTMIN_16B		0
#define X_FETCHBURSTMIN_32B		1
#define X_FETCHBURSTMIN_64B		2
#define X_FETCHBURSTMIN_128B		3

#define X_FETCHBURSTMAX_SHIFT		6
#define X_FETCHBURSTMAX_64B		0
#define X_FETCHBURSTMAX_128B		1
#define X_FETCHBURSTMAX_256B		2
#define X_FETCHBURSTMAX_512B		3

#define X_HOSTFCMODE_NONE		0
#define X_HOSTFCMODE_INGRESS_QUEUE	1
#define X_HOSTFCMODE_STATUS_PAGE	2
#define X_HOSTFCMODE_BOTH		3

#define X_HOSTFCOWNER_UP		0
#define X_HOSTFCOWNER_SGE		1

#define X_CIDXFLUSHTHRESH_1		0
#define X_CIDXFLUSHTHRESH_2		1
#define X_CIDXFLUSHTHRESH_4		2
#define X_CIDXFLUSHTHRESH_8		3
#define X_CIDXFLUSHTHRESH_16		4
#define X_CIDXFLUSHTHRESH_32		5
#define X_CIDXFLUSHTHRESH_64		6
#define X_CIDXFLUSHTHRESH_128		7

#define X_IDXSIZE_UNIT			64

#define X_BASEADDRESS_ALIGN		512

/*
 * Ingress Context field values
 */
#define X_UPDATESCHEDULING_TIMER	0
#define X_UPDATESCHEDULING_COUNTER_OPTTIMER	1

#define X_UPDATEDELIVERY_NONE		0
#define X_UPDATEDELIVERY_INTERRUPT	1
#define X_UPDATEDELIVERY_STATUS_PAGE	2
#define X_UPDATEDELIVERY_BOTH		3

#define X_INTERRUPTDESTINATION_PCIE	0
#define X_INTERRUPTDESTINATION_IQ	1

#define X_QUEUEENTRYSIZE_16B		0
#define X_QUEUEENTRYSIZE_32B		1
#define X_QUEUEENTRYSIZE_64B		2
#define X_QUEUEENTRYSIZE_128B		3

#define IC_SIZE_UNIT			16
#define IC_BASEADDRESS_ALIGN		512

#define X_RSPD_TYPE_FLBUF		0
#define X_RSPD_TYPE_CPL			1
#define X_RSPD_TYPE_INTR		2

/*
 * Context field definitions.  This is by no means a complete list of SGE
 * Context fields.  In the vast majority of cases the firmware initializes
 * things the way they need to be set up.  But in a few small cases, we need
 * to compute new values and ship them off to the firmware to be applied to
 * the SGE Conexts ...
 */

/*
 * Congestion Manager Definitions.
 */
#define S_CONMCTXT_CNGTPMODE		19
#define M_CONMCTXT_CNGTPMODE		0x3
#define V_CONMCTXT_CNGTPMODE(x)		((x) << S_CONMCTXT_CNGTPMODE)
#define G_CONMCTXT_CNGTPMODE(x)  \
	(((x) >> S_CONMCTXT_CNGTPMODE) & M_CONMCTXT_CNGTPMODE)
#define S_CONMCTXT_CNGCHMAP		0
#define M_CONMCTXT_CNGCHMAP		0xffff
#define V_CONMCTXT_CNGCHMAP(x)		((x) << S_CONMCTXT_CNGCHMAP)
#define G_CONMCTXT_CNGCHMAP(x)   \
	(((x) >> S_CONMCTXT_CNGCHMAP) & M_CONMCTXT_CNGCHMAP)

#define X_CONMCTXT_CNGTPMODE_DISABLE	0
#define X_CONMCTXT_CNGTPMODE_QUEUE	1
#define X_CONMCTXT_CNGTPMODE_CHANNEL	2
#define X_CONMCTXT_CNGTPMODE_BOTH	3

/*
 * T5 and later support a new BAR2-based doorbell mechanism for Egress Queues.
 * The User Doorbells are each 128 bytes in length with a Simple Doorbell at
 * offsets 8x and a Write Combining single 64-byte Egress Queue Unit
 * (X_IDXSIZE_UNIT) Gather Buffer interface at offset 64.  For Ingress Queues,
 * we have a Going To Sleep register at offsets 8x+4.
 *
 * As noted above, we have many instances of the Simple Doorbell and Going To
 * Sleep registers at offsets 8x and 8x+4, respectively.  We want to use a
 * non-64-byte aligned offset for the Simple Doorbell in order to attempt to
 * avoid buffering of the writes to the Simple Doorbell and we want to use a
 * non-contiguous offset for the Going To Sleep writes in order to avoid
 * possible combining between them.
 */
#define SGE_UDB_SIZE		128
#define SGE_UDB_KDOORBELL	8
#define SGE_UDB_GTS		20
#define SGE_UDB_WCDOORBELL	64

/*
 * CIM definitions.
 * ================
 */

/*
 * CIM register field values.
 */
#define X_MBOWNER_NONE			0
#define X_MBOWNER_FW			1
#define X_MBOWNER_PL			2
#define X_MBOWNER_FW_DEFERRED		3

/*
 * PCI-E definitions.
 * ==================
 */

#define X_WINDOW_SHIFT			10
#define X_PCIEOFST_SHIFT		10

/*
 * TP definitions.
 * ===============
 */

/*
 * TP_VLAN_PRI_MAP controls which subset of fields will be present in the
 * Compressed Filter Tuple for LE filters.  Each bit set in TP_VLAN_PRI_MAP
 * selects for a particular field being present.  These fields, when present
 * in the Compressed Filter Tuple, have the following widths in bits.
 */
#define S_FT_FIRST			S_FCOE
#define S_FT_LAST			S_FRAGMENTATION

#define W_FT_FCOE			1
#define W_FT_PORT			3
#define W_FT_VNIC_ID			17
#define W_FT_VLAN			17
#define W_FT_TOS			8
#define W_FT_PROTOCOL			8
#define W_FT_ETHERTYPE			16
#define W_FT_MACMATCH			9
#define W_FT_MPSHITTYPE			3
#define W_FT_FRAGMENTATION		1

#define M_FT_FCOE			((1ULL << W_FT_FCOE) - 1)
#define M_FT_PORT			((1ULL << W_FT_PORT) - 1)
#define M_FT_VNIC_ID			((1ULL << W_FT_VNIC_ID) - 1)
#define M_FT_VLAN			((1ULL << W_FT_VLAN) - 1)
#define M_FT_TOS			((1ULL << W_FT_TOS) - 1)
#define M_FT_PROTOCOL			((1ULL << W_FT_PROTOCOL) - 1)
#define M_FT_ETHERTYPE			((1ULL << W_FT_ETHERTYPE) - 1)
#define M_FT_MACMATCH			((1ULL << W_FT_MACMATCH) - 1)
#define M_FT_MPSHITTYPE			((1ULL << W_FT_MPSHITTYPE) - 1)
#define M_FT_FRAGMENTATION		((1ULL << W_FT_FRAGMENTATION) - 1)

/*
 * Some of the Compressed Filter Tuple fields have internal structure.  These
 * bit shifts/masks describe those structures.  All shifts are relative to the
 * base position of the fields within the Compressed Filter Tuple
 */
#define S_FT_VLAN_VLD			16
#define V_FT_VLAN_VLD(x)		((x) << S_FT_VLAN_VLD)
#define F_FT_VLAN_VLD			V_FT_VLAN_VLD(1U)

#define S_FT_VNID_ID_VF			0
#define M_FT_VNID_ID_VF			0x7fU
#define V_FT_VNID_ID_VF(x)		((x) << S_FT_VNID_ID_VF)
#define G_FT_VNID_ID_VF(x)		(((x) >> S_FT_VNID_ID_VF) & M_FT_VNID_ID_VF)

#define S_FT_VNID_ID_PF			7
#define M_FT_VNID_ID_PF			0x7U
#define V_FT_VNID_ID_PF(x)		((x) << S_FT_VNID_ID_PF)
#define G_FT_VNID_ID_PF(x)		(((x) >> S_FT_VNID_ID_PF) & M_FT_VNID_ID_PF)

#define S_FT_VNID_ID_VLD		16
#define V_FT_VNID_ID_VLD(x)		((x) << S_FT_VNID_ID_VLD)
#define F_FT_VNID_ID_VLD(x)		V_FT_VNID_ID_VLD(1U)

#endif /* __T4_REGS_VALUES_H__ */
