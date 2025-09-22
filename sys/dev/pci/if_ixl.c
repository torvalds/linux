/*	$OpenBSD: if_ixl.c,v 1.109 2025/09/17 12:54:19 jan Exp $ */

/*
 * Copyright (c) 2013-2015, Intel Corporation
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2016,2017 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bpfilter.h"
#include "kstat.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/pool.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/task.h>
#include <sys/syslog.h>
#include <sys/intrmap.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/route.h>
#include <net/toeplitz.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NKSTAT > 0
#include <sys/kstat.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#ifdef __sparc64__
#include <dev/ofw/openfirm.h>
#endif

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

#define IXL_MAX_VECTORS			8 /* XXX this is pretty arbitrary */

#define I40E_MASK(mask, shift)		((mask) << (shift))
#define I40E_PF_RESET_WAIT_COUNT	200
#define I40E_AQ_LARGE_BUF		512

/* bitfields for Tx queue mapping in QTX_CTL */
#define I40E_QTX_CTL_VF_QUEUE		0x0
#define I40E_QTX_CTL_VM_QUEUE		0x1
#define I40E_QTX_CTL_PF_QUEUE		0x2

#define I40E_QUEUE_TYPE_EOL		0x7ff
#define I40E_INTR_NOTX_QUEUE		0

#define I40E_QUEUE_TYPE_RX		0x0
#define I40E_QUEUE_TYPE_TX		0x1
#define I40E_QUEUE_TYPE_PE_CEQ		0x2
#define I40E_QUEUE_TYPE_UNKNOWN		0x3

#define I40E_ITR_INDEX_RX		0x0
#define I40E_ITR_INDEX_TX		0x1
#define I40E_ITR_INDEX_OTHER		0x2
#define I40E_ITR_INDEX_NONE		0x3

#include <dev/pci/if_ixlreg.h>

#define I40E_INTR_NOTX_QUEUE		0
#define I40E_INTR_NOTX_INTR		0
#define I40E_INTR_NOTX_RX_QUEUE		0
#define I40E_INTR_NOTX_TX_QUEUE		1
#define I40E_INTR_NOTX_RX_MASK		I40E_PFINT_ICR0_QUEUE_0_MASK
#define I40E_INTR_NOTX_TX_MASK		I40E_PFINT_ICR0_QUEUE_1_MASK

struct ixl_aq_desc {
	uint16_t	iaq_flags;
#define	IXL_AQ_DD		(1U << 0)
#define	IXL_AQ_CMP		(1U << 1)
#define IXL_AQ_ERR		(1U << 2)
#define IXL_AQ_VFE		(1U << 3)
#define IXL_AQ_LB		(1U << 9)
#define IXL_AQ_RD		(1U << 10)
#define IXL_AQ_VFC		(1U << 11)
#define IXL_AQ_BUF		(1U << 12)
#define IXL_AQ_SI		(1U << 13)
#define IXL_AQ_EI		(1U << 14)
#define IXL_AQ_FE		(1U << 15)

#define IXL_AQ_FLAGS_FMT	"\020" "\020FE" "\017EI" "\016SI" "\015BUF" \
				    "\014VFC" "\013DB" "\012LB" "\004VFE" \
				    "\003ERR" "\002CMP" "\001DD"

	uint16_t	iaq_opcode;

	uint16_t	iaq_datalen;
	uint16_t	iaq_retval;

	uint64_t	iaq_cookie;

	uint32_t	iaq_param[4];
/*	iaq_data_hi	iaq_param[2] */
/*	iaq_data_lo	iaq_param[3] */
} __packed __aligned(8);

/* aq commands */
#define IXL_AQ_OP_GET_VERSION		0x0001
#define IXL_AQ_OP_DRIVER_VERSION	0x0002
#define IXL_AQ_OP_QUEUE_SHUTDOWN	0x0003
#define IXL_AQ_OP_SET_PF_CONTEXT	0x0004
#define IXL_AQ_OP_GET_AQ_ERR_REASON	0x0005
#define IXL_AQ_OP_REQUEST_RESOURCE	0x0008
#define IXL_AQ_OP_RELEASE_RESOURCE	0x0009
#define IXL_AQ_OP_LIST_FUNC_CAP		0x000a
#define IXL_AQ_OP_LIST_DEV_CAP		0x000b
#define IXL_AQ_OP_MAC_ADDRESS_READ	0x0107
#define IXL_AQ_OP_CLEAR_PXE_MODE	0x0110
#define IXL_AQ_OP_SWITCH_GET_CONFIG	0x0200
#define IXL_AQ_OP_RX_CTL_READ		0x0206
#define IXL_AQ_OP_RX_CTL_WRITE		0x0207
#define IXL_AQ_OP_ADD_VSI		0x0210
#define IXL_AQ_OP_UPD_VSI_PARAMS	0x0211
#define IXL_AQ_OP_GET_VSI_PARAMS	0x0212
#define IXL_AQ_OP_ADD_VEB		0x0230
#define IXL_AQ_OP_UPD_VEB_PARAMS	0x0231
#define IXL_AQ_OP_GET_VEB_PARAMS	0x0232
#define IXL_AQ_OP_ADD_MACVLAN		0x0250
#define IXL_AQ_OP_REMOVE_MACVLAN	0x0251
#define IXL_AQ_OP_SET_VSI_PROMISC	0x0254
#define IXL_AQ_OP_PHY_GET_ABILITIES	0x0600
#define IXL_AQ_OP_PHY_SET_CONFIG	0x0601
#define IXL_AQ_OP_PHY_SET_MAC_CONFIG	0x0603
#define IXL_AQ_OP_PHY_RESTART_AN	0x0605
#define IXL_AQ_OP_PHY_LINK_STATUS	0x0607
#define IXL_AQ_OP_PHY_SET_EVENT_MASK	0x0613
#define IXL_AQ_OP_PHY_SET_REGISTER	0x0628
#define IXL_AQ_OP_PHY_GET_REGISTER	0x0629
#define IXL_AQ_OP_LLDP_GET_MIB		0x0a00
#define IXL_AQ_OP_LLDP_MIB_CHG_EV	0x0a01
#define IXL_AQ_OP_LLDP_ADD_TLV		0x0a02
#define IXL_AQ_OP_LLDP_UPD_TLV		0x0a03
#define IXL_AQ_OP_LLDP_DEL_TLV		0x0a04
#define IXL_AQ_OP_LLDP_STOP_AGENT	0x0a05
#define IXL_AQ_OP_LLDP_START_AGENT	0x0a06
#define IXL_AQ_OP_LLDP_GET_CEE_DCBX	0x0a07
#define IXL_AQ_OP_LLDP_SPECIFIC_AGENT	0x0a09
#define IXL_AQ_OP_SET_RSS_KEY		0x0b02 /* 722 only */
#define IXL_AQ_OP_SET_RSS_LUT		0x0b03 /* 722 only */
#define IXL_AQ_OP_GET_RSS_KEY		0x0b04 /* 722 only */
#define IXL_AQ_OP_GET_RSS_LUT		0x0b05 /* 722 only */

struct ixl_aq_mac_addresses {
	uint8_t		pf_lan[ETHER_ADDR_LEN];
	uint8_t		pf_san[ETHER_ADDR_LEN];
	uint8_t		port[ETHER_ADDR_LEN];
	uint8_t		pf_wol[ETHER_ADDR_LEN];
} __packed;

#define IXL_AQ_MAC_PF_LAN_VALID		(1U << 4)
#define IXL_AQ_MAC_PF_SAN_VALID		(1U << 5)
#define IXL_AQ_MAC_PORT_VALID		(1U << 6)
#define IXL_AQ_MAC_PF_WOL_VALID		(1U << 7)

struct ixl_aq_capability {
	uint16_t	cap_id;
#define IXL_AQ_CAP_SWITCH_MODE		0x0001
#define IXL_AQ_CAP_MNG_MODE		0x0002
#define IXL_AQ_CAP_NPAR_ACTIVE		0x0003
#define IXL_AQ_CAP_OS2BMC_CAP		0x0004
#define IXL_AQ_CAP_FUNCTIONS_VALID	0x0005
#define IXL_AQ_CAP_ALTERNATE_RAM	0x0006
#define IXL_AQ_CAP_WOL_AND_PROXY	0x0008
#define IXL_AQ_CAP_SRIOV		0x0012
#define IXL_AQ_CAP_VF			0x0013
#define IXL_AQ_CAP_VMDQ			0x0014
#define IXL_AQ_CAP_8021QBG		0x0015
#define IXL_AQ_CAP_8021QBR		0x0016
#define IXL_AQ_CAP_VSI			0x0017
#define IXL_AQ_CAP_DCB			0x0018
#define IXL_AQ_CAP_FCOE			0x0021
#define IXL_AQ_CAP_ISCSI		0x0022
#define IXL_AQ_CAP_RSS			0x0040
#define IXL_AQ_CAP_RXQ			0x0041
#define IXL_AQ_CAP_TXQ			0x0042
#define IXL_AQ_CAP_MSIX			0x0043
#define IXL_AQ_CAP_VF_MSIX		0x0044
#define IXL_AQ_CAP_FLOW_DIRECTOR	0x0045
#define IXL_AQ_CAP_1588			0x0046
#define IXL_AQ_CAP_IWARP		0x0051
#define IXL_AQ_CAP_LED			0x0061
#define IXL_AQ_CAP_SDP			0x0062
#define IXL_AQ_CAP_MDIO			0x0063
#define IXL_AQ_CAP_WSR_PROT		0x0064
#define IXL_AQ_CAP_NVM_MGMT		0x0080
#define IXL_AQ_CAP_FLEX10		0x00F1
#define IXL_AQ_CAP_CEM			0x00F2
	uint8_t		major_rev;
	uint8_t		minor_rev;
	uint32_t	number;
	uint32_t	logical_id;
	uint32_t	phys_id;
	uint8_t		_reserved[16];
} __packed __aligned(4);

#define IXL_LLDP_SHUTDOWN		0x1

struct ixl_aq_switch_config {
	uint16_t	num_reported;
	uint16_t	num_total;
	uint8_t		_reserved[12];
} __packed __aligned(4);

struct ixl_aq_switch_config_element {
	uint8_t		type;
#define IXL_AQ_SW_ELEM_TYPE_MAC		1
#define IXL_AQ_SW_ELEM_TYPE_PF		2
#define IXL_AQ_SW_ELEM_TYPE_VF		3
#define IXL_AQ_SW_ELEM_TYPE_EMP		4
#define IXL_AQ_SW_ELEM_TYPE_BMC		5
#define IXL_AQ_SW_ELEM_TYPE_PV		16
#define IXL_AQ_SW_ELEM_TYPE_VEB		17
#define IXL_AQ_SW_ELEM_TYPE_PA		18
#define IXL_AQ_SW_ELEM_TYPE_VSI		19
	uint8_t		revision;
#define IXL_AQ_SW_ELEM_REV_1		1
	uint16_t	seid;

	uint16_t	uplink_seid;
	uint16_t	downlink_seid;

	uint8_t		_reserved[3];
	uint8_t		connection_type;
#define IXL_AQ_CONN_TYPE_REGULAR	0x1
#define IXL_AQ_CONN_TYPE_DEFAULT	0x2
#define IXL_AQ_CONN_TYPE_CASCADED	0x3

	uint16_t	scheduler_id;
	uint16_t	element_info;
} __packed __aligned(4);

#define IXL_PHY_TYPE_SGMII		0x00
#define IXL_PHY_TYPE_1000BASE_KX	0x01
#define IXL_PHY_TYPE_10GBASE_KX4	0x02
#define IXL_PHY_TYPE_10GBASE_KR		0x03
#define IXL_PHY_TYPE_40GBASE_KR4	0x04
#define IXL_PHY_TYPE_XAUI		0x05
#define IXL_PHY_TYPE_XFI		0x06
#define IXL_PHY_TYPE_SFI		0x07
#define IXL_PHY_TYPE_XLAUI		0x08
#define IXL_PHY_TYPE_XLPPI		0x09
#define IXL_PHY_TYPE_40GBASE_CR4_CU	0x0a
#define IXL_PHY_TYPE_10GBASE_CR1_CU	0x0b
#define IXL_PHY_TYPE_10GBASE_AOC	0x0c
#define IXL_PHY_TYPE_40GBASE_AOC	0x0d
#define IXL_PHY_TYPE_100BASE_TX		0x11
#define IXL_PHY_TYPE_1000BASE_T		0x12
#define IXL_PHY_TYPE_10GBASE_T		0x13
#define IXL_PHY_TYPE_10GBASE_SR		0x14
#define IXL_PHY_TYPE_10GBASE_LR		0x15
#define IXL_PHY_TYPE_10GBASE_SFPP_CU	0x16
#define IXL_PHY_TYPE_10GBASE_CR1	0x17
#define IXL_PHY_TYPE_40GBASE_CR4	0x18
#define IXL_PHY_TYPE_40GBASE_SR4	0x19
#define IXL_PHY_TYPE_40GBASE_LR4	0x1a
#define IXL_PHY_TYPE_1000BASE_SX	0x1b
#define IXL_PHY_TYPE_1000BASE_LX	0x1c
#define IXL_PHY_TYPE_1000BASE_T_OPTICAL	0x1d
#define IXL_PHY_TYPE_20GBASE_KR2	0x1e

#define IXL_PHY_TYPE_25GBASE_KR		0x1f
#define IXL_PHY_TYPE_25GBASE_CR		0x20
#define IXL_PHY_TYPE_25GBASE_SR		0x21
#define IXL_PHY_TYPE_25GBASE_LR		0x22
#define IXL_PHY_TYPE_25GBASE_AOC	0x23
#define IXL_PHY_TYPE_25GBASE_ACC	0x24

struct ixl_aq_module_desc {
	uint8_t		oui[3];
	uint8_t		_reserved1;
	uint8_t		part_number[16];
	uint8_t		revision[4];
	uint8_t		_reserved2[8];
} __packed __aligned(4);

struct ixl_aq_phy_abilities {
	uint32_t	phy_type;

	uint8_t		link_speed;
#define IXL_AQ_PHY_LINK_SPEED_100MB	(1 << 1)
#define IXL_AQ_PHY_LINK_SPEED_1000MB	(1 << 2)
#define IXL_AQ_PHY_LINK_SPEED_10GB	(1 << 3)
#define IXL_AQ_PHY_LINK_SPEED_40GB	(1 << 4)
#define IXL_AQ_PHY_LINK_SPEED_20GB	(1 << 5)
#define IXL_AQ_PHY_LINK_SPEED_25GB	(1 << 6)
	uint8_t		abilities;
	uint16_t	eee_capability;

	uint32_t	eeer_val;

	uint8_t		d3_lpan;
	uint8_t		phy_type_ext;
#define IXL_AQ_PHY_TYPE_EXT_25G_KR	0x01
#define IXL_AQ_PHY_TYPE_EXT_25G_CR	0x02
#define IXL_AQ_PHY_TYPE_EXT_25G_SR	0x04
#define IXL_AQ_PHY_TYPE_EXT_25G_LR	0x08
	uint8_t		fec_cfg_curr_mod_ext_info;
#define IXL_AQ_ENABLE_FEC_KR		0x01
#define IXL_AQ_ENABLE_FEC_RS		0x02
#define IXL_AQ_REQUEST_FEC_KR		0x04
#define IXL_AQ_REQUEST_FEC_RS		0x08
#define IXL_AQ_ENABLE_FEC_AUTO		0x10
#define IXL_AQ_MODULE_TYPE_EXT_MASK	0xe0
#define IXL_AQ_MODULE_TYPE_EXT_SHIFT	5
	uint8_t		ext_comp_code;

	uint8_t		phy_id[4];

	uint8_t		module_type[3];
#define IXL_SFF8024_ID_SFP		0x03
#define IXL_SFF8024_ID_QSFP		0x0c
#define IXL_SFF8024_ID_QSFP_PLUS	0x0d
#define IXL_SFF8024_ID_QSFP28		0x11
	uint8_t		qualified_module_count;
#define IXL_AQ_PHY_MAX_QMS		16
	struct ixl_aq_module_desc
			qualified_module[IXL_AQ_PHY_MAX_QMS];
} __packed __aligned(4);

struct ixl_aq_link_param {
	uint8_t		notify;
#define IXL_AQ_LINK_NOTIFY	0x03
	uint8_t		_reserved1;
	uint8_t		phy;
	uint8_t		speed;
	uint8_t		status;
	uint8_t		_reserved2[11];
} __packed __aligned(4);

struct ixl_aq_vsi_param {
	uint16_t	uplink_seid;
	uint8_t		connect_type;
#define IXL_AQ_VSI_CONN_TYPE_NORMAL	(0x1)
#define IXL_AQ_VSI_CONN_TYPE_DEFAULT	(0x2)
#define IXL_AQ_VSI_CONN_TYPE_CASCADED	(0x3)
	uint8_t		_reserved1;

	uint8_t		vf_id;
	uint8_t		_reserved2;
	uint16_t	vsi_flags;
#define IXL_AQ_VSI_TYPE_SHIFT		0x0
#define IXL_AQ_VSI_TYPE_MASK		(0x3 << IXL_AQ_VSI_TYPE_SHIFT)
#define IXL_AQ_VSI_TYPE_VF		0x0
#define IXL_AQ_VSI_TYPE_VMDQ2		0x1
#define IXL_AQ_VSI_TYPE_PF		0x2
#define IXL_AQ_VSI_TYPE_EMP_MNG		0x3
#define IXL_AQ_VSI_FLAG_CASCADED_PV	0x4

	uint32_t	addr_hi;
	uint32_t	addr_lo;
} __packed __aligned(16);

struct ixl_aq_add_macvlan {
	uint16_t	num_addrs;
	uint16_t	seid0;
	uint16_t	seid1;
	uint16_t	seid2;
	uint32_t	addr_hi;
	uint32_t	addr_lo;
} __packed __aligned(16);

struct ixl_aq_add_macvlan_elem {
	uint8_t		macaddr[6];
	uint16_t	vlan;
	uint16_t	flags;
#define IXL_AQ_OP_ADD_MACVLAN_PERFECT_MATCH	0x0001
#define IXL_AQ_OP_ADD_MACVLAN_IGNORE_VLAN	0x0004
	uint16_t	queue;
	uint32_t	_reserved;
} __packed __aligned(16);

struct ixl_aq_remove_macvlan {
	uint16_t	num_addrs;
	uint16_t	seid0;
	uint16_t	seid1;
	uint16_t	seid2;
	uint32_t	addr_hi;
	uint32_t	addr_lo;
} __packed __aligned(16);

struct ixl_aq_remove_macvlan_elem {
	uint8_t		macaddr[6];
	uint16_t	vlan;
	uint8_t		flags;
#define IXL_AQ_OP_REMOVE_MACVLAN_PERFECT_MATCH	0x0001
#define IXL_AQ_OP_REMOVE_MACVLAN_IGNORE_VLAN	0x0008
	uint8_t		_reserved[7];
} __packed __aligned(16);

struct ixl_aq_vsi_reply {
	uint16_t	seid;
	uint16_t	vsi_number;

	uint16_t	vsis_used;
	uint16_t	vsis_free;

	uint32_t	addr_hi;
	uint32_t	addr_lo;
} __packed __aligned(16);

struct ixl_aq_vsi_data {
	/* first 96 byte are written by SW */
	uint16_t	valid_sections;
#define IXL_AQ_VSI_VALID_SWITCH		(1 << 0)
#define IXL_AQ_VSI_VALID_SECURITY	(1 << 1)
#define IXL_AQ_VSI_VALID_VLAN		(1 << 2)
#define IXL_AQ_VSI_VALID_CAS_PV		(1 << 3)
#define IXL_AQ_VSI_VALID_INGRESS_UP	(1 << 4)
#define IXL_AQ_VSI_VALID_EGRESS_UP	(1 << 5)
#define IXL_AQ_VSI_VALID_QUEUE_MAP	(1 << 6)
#define IXL_AQ_VSI_VALID_QUEUE_OPT	(1 << 7)
#define IXL_AQ_VSI_VALID_OUTER_UP	(1 << 8)
#define IXL_AQ_VSI_VALID_SCHED		(1 << 9)
	/* switch section */
	uint16_t	switch_id;
#define IXL_AQ_VSI_SWITCH_ID_SHIFT	0
#define IXL_AQ_VSI_SWITCH_ID_MASK	(0xfff << IXL_AQ_VSI_SWITCH_ID_SHIFT)
#define IXL_AQ_VSI_SWITCH_NOT_STAG	(1 << 12)
#define IXL_AQ_VSI_SWITCH_LOCAL_LB	(1 << 14)

	uint8_t		_reserved1[2];
	/* security section */
	uint8_t		sec_flags;
#define IXL_AQ_VSI_SEC_ALLOW_DEST_OVRD	(1 << 0)
#define IXL_AQ_VSI_SEC_ENABLE_VLAN_CHK	(1 << 1)
#define IXL_AQ_VSI_SEC_ENABLE_MAC_CHK	(1 << 2)
	uint8_t		_reserved2;

	/* vlan section */
	uint16_t	pvid;
	uint16_t	fcoe_pvid;

	uint8_t		port_vlan_flags;
#define IXL_AQ_VSI_PVLAN_MODE_SHIFT	0
#define IXL_AQ_VSI_PVLAN_MODE_MASK	(0x3 << IXL_AQ_VSI_PVLAN_MODE_SHIFT)
#define IXL_AQ_VSI_PVLAN_MODE_TAGGED	(0x1 << IXL_AQ_VSI_PVLAN_MODE_SHIFT)
#define IXL_AQ_VSI_PVLAN_MODE_UNTAGGED	(0x2 << IXL_AQ_VSI_PVLAN_MODE_SHIFT)
#define IXL_AQ_VSI_PVLAN_MODE_ALL	(0x3 << IXL_AQ_VSI_PVLAN_MODE_SHIFT)
#define IXL_AQ_VSI_PVLAN_INSERT_PVID	(0x4 << IXL_AQ_VSI_PVLAN_MODE_SHIFT)
#define IXL_AQ_VSI_PVLAN_EMOD_SHIFT	0x3
#define IXL_AQ_VSI_PVLAN_EMOD_MASK	(0x3 << IXL_AQ_VSI_PVLAN_EMOD_SHIFT)
#define IXL_AQ_VSI_PVLAN_EMOD_STR_BOTH	(0x0 << IXL_AQ_VSI_PVLAN_EMOD_SHIFT)
#define IXL_AQ_VSI_PVLAN_EMOD_STR_UP	(0x1 << IXL_AQ_VSI_PVLAN_EMOD_SHIFT)
#define IXL_AQ_VSI_PVLAN_EMOD_STR	(0x2 << IXL_AQ_VSI_PVLAN_EMOD_SHIFT)
#define IXL_AQ_VSI_PVLAN_EMOD_NOTHING	(0x3 << IXL_AQ_VSI_PVLAN_EMOD_SHIFT)
	uint8_t		_reserved3[3];

	/* ingress egress up section */
	uint32_t	ingress_table;
#define IXL_AQ_VSI_UP_SHIFT(_up)	((_up) * 3)
#define IXL_AQ_VSI_UP_MASK(_up)		(0x7 << (IXL_AQ_VSI_UP_SHIFT(_up))
	uint32_t	egress_table;

	/* cascaded pv section */
	uint16_t	cas_pv_tag;
	uint8_t		cas_pv_flags;
#define IXL_AQ_VSI_CAS_PV_TAGX_SHIFT	0
#define IXL_AQ_VSI_CAS_PV_TAGX_MASK	(0x3 << IXL_AQ_VSI_CAS_PV_TAGX_SHIFT)
#define IXL_AQ_VSI_CAS_PV_TAGX_LEAVE	(0x0 << IXL_AQ_VSI_CAS_PV_TAGX_SHIFT)
#define IXL_AQ_VSI_CAS_PV_TAGX_REMOVE	(0x1 << IXL_AQ_VSI_CAS_PV_TAGX_SHIFT)
#define IXL_AQ_VSI_CAS_PV_TAGX_COPY	(0x2 << IXL_AQ_VSI_CAS_PV_TAGX_SHIFT)
#define IXL_AQ_VSI_CAS_PV_INSERT_TAG	(1 << 4)
#define IXL_AQ_VSI_CAS_PV_ETAG_PRUNE	(1 << 5)
#define IXL_AQ_VSI_CAS_PV_ACCEPT_HOST_TAG \
					(1 << 6)
	uint8_t		_reserved4;

	/* queue mapping section */
	uint16_t	mapping_flags;
#define IXL_AQ_VSI_QUE_MAP_MASK		0x1
#define IXL_AQ_VSI_QUE_MAP_CONTIG	0x0
#define IXL_AQ_VSI_QUE_MAP_NONCONTIG	0x1
	uint16_t	queue_mapping[16];
#define IXL_AQ_VSI_QUEUE_SHIFT		0x0
#define IXL_AQ_VSI_QUEUE_MASK		(0x7ff << IXL_AQ_VSI_QUEUE_SHIFT)
	uint16_t	tc_mapping[8];
#define IXL_AQ_VSI_TC_Q_OFFSET_SHIFT	0
#define IXL_AQ_VSI_TC_Q_OFFSET_MASK	(0x1ff << IXL_AQ_VSI_TC_Q_OFFSET_SHIFT)
#define IXL_AQ_VSI_TC_Q_NUMBER_SHIFT	9
#define IXL_AQ_VSI_TC_Q_NUMBER_MASK	(0x7 << IXL_AQ_VSI_TC_Q_NUMBER_SHIFT)

	/* queueing option section */
	uint8_t		queueing_opt_flags;
#define IXL_AQ_VSI_QUE_OPT_MCAST_UDP_EN	(1 << 2)
#define IXL_AQ_VSI_QUE_OPT_UCAST_UDP_EN	(1 << 3)
#define IXL_AQ_VSI_QUE_OPT_TCP_EN	(1 << 4)
#define IXL_AQ_VSI_QUE_OPT_FCOE_EN	(1 << 5)
#define IXL_AQ_VSI_QUE_OPT_RSS_LUT_PF	0
#define IXL_AQ_VSI_QUE_OPT_RSS_LUT_VSI	(1 << 6)
	uint8_t		_reserved5[3];

	/* scheduler section */
	uint8_t		up_enable_bits;
	uint8_t		_reserved6;

	/* outer up section */
	uint32_t	outer_up_table; /* same as ingress/egress tables */
	uint8_t		_reserved7[8];

	/* last 32 bytes are written by FW */
	uint16_t	qs_handle[8];
#define IXL_AQ_VSI_QS_HANDLE_INVALID	0xffff
	uint16_t	stat_counter_idx;
	uint16_t	sched_id;

	uint8_t		_reserved8[12];
} __packed __aligned(8);

CTASSERT(sizeof(struct ixl_aq_vsi_data) == 128);

struct ixl_aq_vsi_promisc_param {
	uint16_t	flags;
	uint16_t	valid_flags;
#define IXL_AQ_VSI_PROMISC_FLAG_UCAST	(1 << 0)
#define IXL_AQ_VSI_PROMISC_FLAG_MCAST	(1 << 1)
#define IXL_AQ_VSI_PROMISC_FLAG_BCAST	(1 << 2)
#define IXL_AQ_VSI_PROMISC_FLAG_DFLT	(1 << 3)
#define IXL_AQ_VSI_PROMISC_FLAG_VLAN	(1 << 4)
#define IXL_AQ_VSI_PROMISC_FLAG_RXONLY	(1 << 15)

	uint16_t	seid;
#define IXL_AQ_VSI_PROMISC_SEID_VALID	(1 << 15)
	uint16_t	vlan;
#define IXL_AQ_VSI_PROMISC_VLAN_VALID	(1 << 15)
	uint32_t	reserved[2];
} __packed __aligned(8);

struct ixl_aq_veb_param {
	uint16_t	uplink_seid;
	uint16_t	downlink_seid;
	uint16_t	veb_flags;
#define IXL_AQ_ADD_VEB_FLOATING		(1 << 0)
#define IXL_AQ_ADD_VEB_PORT_TYPE_SHIFT	1
#define IXL_AQ_ADD_VEB_PORT_TYPE_MASK	(0x3 << IXL_AQ_ADD_VEB_PORT_TYPE_SHIFT)
#define IXL_AQ_ADD_VEB_PORT_TYPE_DEFAULT \
					(0x2 << IXL_AQ_ADD_VEB_PORT_TYPE_SHIFT)
#define IXL_AQ_ADD_VEB_PORT_TYPE_DATA	(0x4 << IXL_AQ_ADD_VEB_PORT_TYPE_SHIFT)
#define IXL_AQ_ADD_VEB_ENABLE_L2_FILTER	(1 << 3) /* deprecated */
#define IXL_AQ_ADD_VEB_DISABLE_STATS	(1 << 4)
	uint8_t		enable_tcs;
	uint8_t		_reserved[9];
} __packed __aligned(16);

struct ixl_aq_veb_reply {
	uint16_t	_reserved1;
	uint16_t	_reserved2;
	uint16_t	_reserved3;
	uint16_t	switch_seid;
	uint16_t	veb_seid;
#define IXL_AQ_VEB_ERR_FLAG_NO_VEB	(1 << 0)
#define IXL_AQ_VEB_ERR_FLAG_NO_SCHED	(1 << 1)
#define IXL_AQ_VEB_ERR_FLAG_NO_COUNTER	(1 << 2)
#define IXL_AQ_VEB_ERR_FLAG_NO_ENTRY	(1 << 3);
	uint16_t	statistic_index;
	uint16_t	vebs_used;
	uint16_t	vebs_free;
} __packed __aligned(16);

/* GET PHY ABILITIES param[0] */
#define IXL_AQ_PHY_REPORT_QUAL		(1 << 0)
#define IXL_AQ_PHY_REPORT_INIT		(1 << 1)

struct ixl_aq_phy_reg_access {
	uint8_t		phy_iface;
#define IXL_AQ_PHY_IF_INTERNAL		0
#define IXL_AQ_PHY_IF_EXTERNAL		1
#define IXL_AQ_PHY_IF_MODULE		2
	uint8_t		dev_addr;
	uint16_t	recall;
#define IXL_AQ_PHY_QSFP_DEV_ADDR	0
#define IXL_AQ_PHY_QSFP_LAST		1
	uint32_t	reg;
	uint32_t	val;
	uint32_t	_reserved2;
} __packed __aligned(16);

/* RESTART_AN param[0] */
#define IXL_AQ_PHY_RESTART_AN		(1 << 1)
#define IXL_AQ_PHY_LINK_ENABLE		(1 << 2)

struct ixl_aq_link_status { /* this occupies the iaq_param space */
	uint16_t	command_flags; /* only field set on command */
#define IXL_AQ_LSE_MASK			0x3
#define IXL_AQ_LSE_NOP			0x0
#define IXL_AQ_LSE_DISABLE		0x2
#define IXL_AQ_LSE_ENABLE		0x3
#define IXL_AQ_LSE_IS_ENABLED		0x1 /* only set in response */
	uint8_t		phy_type;
	uint8_t		link_speed;
#define IXL_AQ_LINK_SPEED_1GB		(1 << 2)
#define IXL_AQ_LINK_SPEED_10GB		(1 << 3)
#define IXL_AQ_LINK_SPEED_40GB		(1 << 4)
#define IXL_AQ_LINK_SPEED_25GB		(1 << 6)
	uint8_t		link_info;
#define IXL_AQ_LINK_UP_FUNCTION		0x01
#define IXL_AQ_LINK_FAULT		0x02
#define IXL_AQ_LINK_FAULT_TX		0x04
#define IXL_AQ_LINK_FAULT_RX		0x08
#define IXL_AQ_LINK_FAULT_REMOTE	0x10
#define IXL_AQ_LINK_UP_PORT		0x20
#define IXL_AQ_MEDIA_AVAILABLE		0x40
#define IXL_AQ_SIGNAL_DETECT		0x80
	uint8_t		an_info;
#define IXL_AQ_AN_COMPLETED		0x01
#define IXL_AQ_LP_AN_ABILITY		0x02
#define IXL_AQ_PD_FAULT			0x04
#define IXL_AQ_FEC_EN			0x08
#define IXL_AQ_PHY_LOW_POWER		0x10
#define IXL_AQ_LINK_PAUSE_TX		0x20
#define IXL_AQ_LINK_PAUSE_RX		0x40
#define IXL_AQ_QUALIFIED_MODULE		0x80

	uint8_t		ext_info;
#define IXL_AQ_LINK_PHY_TEMP_ALARM	0x01
#define IXL_AQ_LINK_XCESSIVE_ERRORS	0x02
#define IXL_AQ_LINK_TX_SHIFT		0x02
#define IXL_AQ_LINK_TX_MASK		(0x03 << IXL_AQ_LINK_TX_SHIFT)
#define IXL_AQ_LINK_TX_ACTIVE		0x00
#define IXL_AQ_LINK_TX_DRAINED		0x01
#define IXL_AQ_LINK_TX_FLUSHED		0x03
#define IXL_AQ_LINK_FORCED_40G		0x10
/* 25G Error Codes */
#define IXL_AQ_25G_NO_ERR		0X00
#define IXL_AQ_25G_NOT_PRESENT		0X01
#define IXL_AQ_25G_NVM_CRC_ERR		0X02
#define IXL_AQ_25G_SBUS_UCODE_ERR	0X03
#define IXL_AQ_25G_SERDES_UCODE_ERR	0X04
#define IXL_AQ_25G_NIMB_UCODE_ERR	0X05
	uint8_t		loopback;
	uint16_t	max_frame_size;

	uint8_t		config;
#define IXL_AQ_CONFIG_FEC_KR_ENA	0x01
#define IXL_AQ_CONFIG_FEC_RS_ENA	0x02
#define IXL_AQ_CONFIG_CRC_ENA	0x04
#define IXL_AQ_CONFIG_PACING_MASK	0x78
	uint8_t		power_desc;
#define IXL_AQ_LINK_POWER_CLASS_1	0x00
#define IXL_AQ_LINK_POWER_CLASS_2	0x01
#define IXL_AQ_LINK_POWER_CLASS_3	0x02
#define IXL_AQ_LINK_POWER_CLASS_4	0x03
#define IXL_AQ_PWR_CLASS_MASK		0x03

	uint8_t		reserved[4];
} __packed __aligned(4);
/* event mask command flags for param[2] */
#define IXL_AQ_PHY_EV_MASK		0x3ff
#define IXL_AQ_PHY_EV_LINK_UPDOWN	(1 << 1)
#define IXL_AQ_PHY_EV_MEDIA_NA		(1 << 2)
#define IXL_AQ_PHY_EV_LINK_FAULT	(1 << 3)
#define IXL_AQ_PHY_EV_PHY_TEMP_ALARM	(1 << 4)
#define IXL_AQ_PHY_EV_EXCESS_ERRORS	(1 << 5)
#define IXL_AQ_PHY_EV_SIGNAL_DETECT	(1 << 6)
#define IXL_AQ_PHY_EV_AN_COMPLETED	(1 << 7)
#define IXL_AQ_PHY_EV_MODULE_QUAL_FAIL	(1 << 8)
#define IXL_AQ_PHY_EV_PORT_TX_SUSPENDED	(1 << 9)

struct ixl_aq_rss_lut { /* 722 */
#define IXL_AQ_SET_RSS_LUT_VSI_VALID	(1 << 15)
#define IXL_AQ_SET_RSS_LUT_VSI_ID_SHIFT	0
#define IXL_AQ_SET_RSS_LUT_VSI_ID_MASK	\
	(0x3FF << IXL_AQ_SET_RSS_LUT_VSI_ID_SHIFT)

	uint16_t	vsi_number;
#define IXL_AQ_SET_RSS_LUT_TABLE_TYPE_SHIFT 0
#define IXL_AQ_SET_RSS_LUT_TABLE_TYPE_MASK \
	(0x1 << IXL_AQ_SET_RSS_LUT_TABLE_TYPE_SHIFT)
#define IXL_AQ_SET_RSS_LUT_TABLE_TYPE_VSI	0
#define IXL_AQ_SET_RSS_LUT_TABLE_TYPE_PF	1
	uint16_t	flags;
	uint8_t		_reserved[4];
	uint32_t	addr_hi;
	uint32_t	addr_lo;
} __packed __aligned(16);

struct ixl_aq_get_set_rss_key { /* 722 */
#define IXL_AQ_SET_RSS_KEY_VSI_VALID	(1 << 15)
#define IXL_AQ_SET_RSS_KEY_VSI_ID_SHIFT	0
#define IXL_AQ_SET_RSS_KEY_VSI_ID_MASK	\
	(0x3FF << IXL_AQ_SET_RSS_KEY_VSI_ID_SHIFT)
	uint16_t	vsi_number;
	uint8_t		_reserved[6];
	uint32_t	addr_hi;
	uint32_t	addr_lo;
} __packed __aligned(16);

/* aq response codes */
#define IXL_AQ_RC_OK			0  /* success */
#define IXL_AQ_RC_EPERM			1  /* Operation not permitted */
#define IXL_AQ_RC_ENOENT		2  /* No such element */
#define IXL_AQ_RC_ESRCH			3  /* Bad opcode */
#define IXL_AQ_RC_EINTR			4  /* operation interrupted */
#define IXL_AQ_RC_EIO			5  /* I/O error */
#define IXL_AQ_RC_ENXIO			6  /* No such resource */
#define IXL_AQ_RC_E2BIG			7  /* Arg too long */
#define IXL_AQ_RC_EAGAIN		8  /* Try again */
#define IXL_AQ_RC_ENOMEM		9  /* Out of memory */
#define IXL_AQ_RC_EACCES		10 /* Permission denied */
#define IXL_AQ_RC_EFAULT		11 /* Bad address */
#define IXL_AQ_RC_EBUSY			12 /* Device or resource busy */
#define IXL_AQ_RC_EEXIST		13 /* object already exists */
#define IXL_AQ_RC_EINVAL		14 /* invalid argument */
#define IXL_AQ_RC_ENOTTY		15 /* not a typewriter */
#define IXL_AQ_RC_ENOSPC		16 /* No space or alloc failure */
#define IXL_AQ_RC_ENOSYS		17 /* function not implemented */
#define IXL_AQ_RC_ERANGE		18 /* parameter out of range */
#define IXL_AQ_RC_EFLUSHED		19 /* cmd flushed due to prev error */
#define IXL_AQ_RC_BAD_ADDR		20 /* contains a bad pointer */
#define IXL_AQ_RC_EMODE			21 /* not allowed in current mode */
#define IXL_AQ_RC_EFBIG			22 /* file too large */

struct ixl_tx_desc {
	uint64_t		addr;
	uint64_t		cmd;
#define IXL_TX_DESC_DTYPE_SHIFT		0
#define IXL_TX_DESC_DTYPE_MASK		(0xfULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_DATA		(0x0ULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_NOP		(0x1ULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_CONTEXT	(0x1ULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_FCOE_CTX	(0x2ULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_FD		(0x8ULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_DDP_CTX	(0x9ULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_FLEX_DATA	(0xbULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_FLEX_CTX_1	(0xcULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_FLEX_CTX_2	(0xdULL << IXL_TX_DESC_DTYPE_SHIFT)
#define IXL_TX_DESC_DTYPE_DONE		(0xfULL << IXL_TX_DESC_DTYPE_SHIFT)

#define IXL_TX_DESC_CMD_SHIFT		4
#define IXL_TX_DESC_CMD_MASK		(0x3ffULL << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_EOP		(0x001 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_RS		(0x002 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_ICRC		(0x004 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_IL2TAG1		(0x008 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_DUMMY		(0x010 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_IIPT_MASK	(0x060 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_IIPT_NONIP	(0x000 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_IIPT_IPV6	(0x020 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_IIPT_IPV4	(0x040 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_IIPT_IPV4_CSUM	(0x060 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_FCOET		(0x080 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_L4T_EOFT_MASK	(0x300 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_L4T_EOFT_UNK	(0x000 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_L4T_EOFT_TCP	(0x100 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_L4T_EOFT_SCTP	(0x200 << IXL_TX_DESC_CMD_SHIFT)
#define IXL_TX_DESC_CMD_L4T_EOFT_UDP	(0x300 << IXL_TX_DESC_CMD_SHIFT)

#define IXL_TX_DESC_MACLEN_SHIFT	16
#define IXL_TX_DESC_MACLEN_MASK		(0x7fULL << IXL_TX_DESC_MACLEN_SHIFT)
#define IXL_TX_DESC_IPLEN_SHIFT		23
#define IXL_TX_DESC_IPLEN_MASK		(0x7fULL << IXL_TX_DESC_IPLEN_SHIFT)
#define IXL_TX_DESC_L4LEN_SHIFT		30
#define IXL_TX_DESC_L4LEN_MASK		(0xfULL << IXL_TX_DESC_L4LEN_SHIFT)
#define IXL_TX_DESC_FCLEN_SHIFT		30
#define IXL_TX_DESC_FCLEN_MASK		(0xfULL << IXL_TX_DESC_FCLEN_SHIFT)

#define IXL_TX_DESC_BSIZE_SHIFT		34
#define IXL_TX_DESC_BSIZE_MAX		0x3fffULL
#define IXL_TX_DESC_BSIZE_MASK		\
	(IXL_TX_DESC_BSIZE_MAX << IXL_TX_DESC_BSIZE_SHIFT)

#define IXL_TX_CTX_DESC_CMD_TSO		0x10
#define IXL_TX_CTX_DESC_TLEN_SHIFT	30
#define IXL_TX_CTX_DESC_MSS_SHIFT	50

#define IXL_TX_DESC_L2TAG1_SHIFT	48
} __packed __aligned(16);

struct ixl_rx_rd_desc_16 {
	uint64_t		paddr; /* packet addr */
	uint64_t		haddr; /* header addr */
} __packed __aligned(16);

struct ixl_rx_rd_desc_32 {
	uint64_t		paddr; /* packet addr */
	uint64_t		haddr; /* header addr */
	uint64_t		_reserved1;
	uint64_t		_reserved2;
} __packed __aligned(16);

struct ixl_rx_wb_desc_16 {
	uint16_t		_reserved1;
	uint16_t		l2tag1;
	uint32_t		filter_status;
	uint64_t		qword1;
#define IXL_RX_DESC_DD			(1 << 0)
#define IXL_RX_DESC_EOP			(1 << 1)
#define IXL_RX_DESC_L2TAG1P		(1 << 2)
#define IXL_RX_DESC_L3L4P		(1 << 3)
#define IXL_RX_DESC_CRCP		(1 << 4)
#define IXL_RX_DESC_TSYNINDX_SHIFT	5	/* TSYNINDX */
#define IXL_RX_DESC_TSYNINDX_MASK	(7 << IXL_RX_DESC_TSYNINDX_SHIFT)
#define IXL_RX_DESC_UMB_SHIFT		9
#define IXL_RX_DESC_UMB_MASK		(0x3 << IXL_RX_DESC_UMB_SHIFT)
#define IXL_RX_DESC_UMB_UCAST		(0x0 << IXL_RX_DESC_UMB_SHIFT)
#define IXL_RX_DESC_UMB_MCAST		(0x1 << IXL_RX_DESC_UMB_SHIFT)
#define IXL_RX_DESC_UMB_BCAST		(0x2 << IXL_RX_DESC_UMB_SHIFT)
#define IXL_RX_DESC_UMB_MIRROR		(0x3 << IXL_RX_DESC_UMB_SHIFT)
#define IXL_RX_DESC_FLM			(1 << 11)
#define IXL_RX_DESC_FLTSTAT_SHIFT	12
#define IXL_RX_DESC_FLTSTAT_MASK	(0x3 << IXL_RX_DESC_FLTSTAT_SHIFT)
#define IXL_RX_DESC_FLTSTAT_NODATA	(0x0 << IXL_RX_DESC_FLTSTAT_SHIFT)
#define IXL_RX_DESC_FLTSTAT_FDFILTID	(0x1 << IXL_RX_DESC_FLTSTAT_SHIFT)
#define IXL_RX_DESC_FLTSTAT_RSS		(0x3 << IXL_RX_DESC_FLTSTAT_SHIFT)
#define IXL_RX_DESC_LPBK		(1 << 14)
#define IXL_RX_DESC_IPV6EXTADD		(1 << 15)
#define IXL_RX_DESC_INT_UDP_0		(1 << 18)

#define IXL_RX_DESC_RXE			(1 << 19)
#define IXL_RX_DESC_HBO			(1 << 21)
#define IXL_RX_DESC_IPE			(1 << 22)
#define IXL_RX_DESC_L4E			(1 << 23)
#define IXL_RX_DESC_EIPE		(1 << 24)
#define IXL_RX_DESC_OVERSIZE		(1 << 25)

#define IXL_RX_DESC_PTYPE_SHIFT		30
#define IXL_RX_DESC_PTYPE_MASK		(0xffULL << IXL_RX_DESC_PTYPE_SHIFT)
#define IXL_RX_DESC_PTYPE_MAC_IPV4_TCP	26
#define IXL_RX_DESC_PTYPE_MAC_IPV6_TCP	92

#define IXL_RX_DESC_PLEN_SHIFT		38
#define IXL_RX_DESC_PLEN_MASK		(0x3fffULL << IXL_RX_DESC_PLEN_SHIFT)
#define IXL_RX_DESC_HLEN_SHIFT		42
#define IXL_RX_DESC_HLEN_MASK		(0x7ffULL << IXL_RX_DESC_HLEN_SHIFT)
} __packed __aligned(16);

struct ixl_rx_wb_desc_32 {
	uint64_t		qword0;
	uint64_t		qword1;
	uint64_t		qword2;
	uint64_t		qword3;
} __packed __aligned(16);

#define IXL_TX_PKT_DESCS		8
#define IXL_TX_TSO_PKT_DESCS		128
#define IXL_TX_QUEUE_ALIGN		128
#define IXL_RX_QUEUE_ALIGN		128

#define IXL_HARDMTU			9712 /* 9726 - ETHER_HDR_LEN */
#define IXL_TSO_SIZE			((255 * 1024) - 1)
#define IXL_MAX_DMA_SEG_SIZE		((16 * 1024) - 1)

/*
 * Our TCP/IP Stack is unable handle packets greater than MAXMCLBYTES.
 * This interface is unable handle packets greater than IXL_TSO_SIZE.
 */
CTASSERT(MAXMCLBYTES < IXL_TSO_SIZE);

#define IXL_PCIREG			PCI_MAPREG_START

#define IXL_ITR0			0x0
#define IXL_ITR1			0x1
#define IXL_ITR2			0x2
#define IXL_NOITR			0x2

#define IXL_AQ_NUM			256
#define IXL_AQ_MASK			(IXL_AQ_NUM - 1)
#define IXL_AQ_ALIGN			64 /* lol */
#define IXL_AQ_BUFLEN			4096

#define IXL_HMC_ROUNDUP			512
#define IXL_HMC_PGSIZE			4096
#define IXL_HMC_DVASZ			sizeof(uint64_t)
#define IXL_HMC_PGS			(IXL_HMC_PGSIZE / IXL_HMC_DVASZ)
#define IXL_HMC_L2SZ			(IXL_HMC_PGSIZE * IXL_HMC_PGS)
#define IXL_HMC_PDVALID			1ULL

struct ixl_aq_regs {
	bus_size_t		atq_tail;
	bus_size_t		atq_head;
	bus_size_t		atq_len;
	bus_size_t		atq_bal;
	bus_size_t		atq_bah;

	bus_size_t		arq_tail;
	bus_size_t		arq_head;
	bus_size_t		arq_len;
	bus_size_t		arq_bal;
	bus_size_t		arq_bah;

	uint32_t		atq_len_enable;
	uint32_t		atq_tail_mask;
	uint32_t		atq_head_mask;

	uint32_t		arq_len_enable;
	uint32_t		arq_tail_mask;
	uint32_t		arq_head_mask;
};

struct ixl_phy_type {
	uint64_t	phy_type;
	uint64_t	ifm_type;
};

struct ixl_speed_type {
	uint8_t		dev_speed;
	uint64_t	net_speed;
};

struct ixl_aq_buf {
	SIMPLEQ_ENTRY(ixl_aq_buf)
				 aqb_entry;
	void			*aqb_data;
	bus_dmamap_t		 aqb_map;
};
SIMPLEQ_HEAD(ixl_aq_bufs, ixl_aq_buf);

struct ixl_dmamem {
	bus_dmamap_t		ixm_map;
	bus_dma_segment_t	ixm_seg;
	int			ixm_nsegs;
	size_t			ixm_size;
	caddr_t			ixm_kva;
};
#define IXL_DMA_MAP(_ixm)	((_ixm)->ixm_map)
#define IXL_DMA_DVA(_ixm)	((_ixm)->ixm_map->dm_segs[0].ds_addr)
#define IXL_DMA_KVA(_ixm)	((void *)(_ixm)->ixm_kva)
#define IXL_DMA_LEN(_ixm)	((_ixm)->ixm_size)

struct ixl_hmc_entry {
	uint64_t		 hmc_base;
	uint32_t		 hmc_count;
	uint32_t		 hmc_size;
};

#define IXL_HMC_LAN_TX		 0
#define IXL_HMC_LAN_RX		 1
#define IXL_HMC_FCOE_CTX	 2
#define IXL_HMC_FCOE_FILTER	 3
#define IXL_HMC_COUNT		 4

struct ixl_hmc_pack {
	uint16_t		offset;
	uint16_t		width;
	uint16_t		lsb;
};

/*
 * these hmc objects have weird sizes and alignments, so these are abstract
 * representations of them that are nice for c to populate.
 *
 * the packing code relies on little-endian values being stored in the fields,
 * no high bits in the fields being set, and the fields must be packed in the
 * same order as they are in the ctx structure.
 */

struct ixl_hmc_rxq {
	uint16_t		 head;
	uint8_t			 cpuid;
	uint64_t		 base;
#define IXL_HMC_RXQ_BASE_UNIT		128
	uint16_t		 qlen;
	uint16_t		 dbuff;
#define IXL_HMC_RXQ_DBUFF_UNIT		128
	uint8_t			 hbuff;
#define IXL_HMC_RXQ_HBUFF_UNIT		64
	uint8_t			 dtype;
#define IXL_HMC_RXQ_DTYPE_NOSPLIT	0x0
#define IXL_HMC_RXQ_DTYPE_HSPLIT	0x1
#define IXL_HMC_RXQ_DTYPE_SPLIT_ALWAYS	0x2
	uint8_t			 dsize;
#define IXL_HMC_RXQ_DSIZE_16		0
#define IXL_HMC_RXQ_DSIZE_32		1
	uint8_t			 crcstrip;
	uint8_t			 fc_ena;
	uint8_t			 l2tsel;
#define IXL_HMC_RXQ_L2TSEL_2ND_TAG_TO_L2TAG1 \
					0
#define IXL_HMC_RXQ_L2TSEL_1ST_TAG_TO_L2TAG1 \
					1
	uint8_t			 hsplit_0;
	uint8_t			 hsplit_1;
	uint8_t			 showiv;
	uint16_t		 rxmax;
	uint8_t			 tphrdesc_ena;
	uint8_t			 tphwdesc_ena;
	uint8_t			 tphdata_ena;
	uint8_t			 tphhead_ena;
	uint8_t			 lrxqthresh;
	uint8_t			 prefena;
};

static const struct ixl_hmc_pack ixl_hmc_pack_rxq[] = {
	{ offsetof(struct ixl_hmc_rxq, head),		13,	0 },
	{ offsetof(struct ixl_hmc_rxq, cpuid),		8,	13 },
	{ offsetof(struct ixl_hmc_rxq, base),		57,	32 },
	{ offsetof(struct ixl_hmc_rxq, qlen),		13,	89 },
	{ offsetof(struct ixl_hmc_rxq, dbuff),		7,	102 },
	{ offsetof(struct ixl_hmc_rxq, hbuff),		5,	109 },
	{ offsetof(struct ixl_hmc_rxq, dtype),		2,	114 },
	{ offsetof(struct ixl_hmc_rxq, dsize),		1,	116 },
	{ offsetof(struct ixl_hmc_rxq, crcstrip),	1,	117 },
	{ offsetof(struct ixl_hmc_rxq, fc_ena),		1,	118 },
	{ offsetof(struct ixl_hmc_rxq, l2tsel),		1,	119 },
	{ offsetof(struct ixl_hmc_rxq, hsplit_0),	4,	120 },
	{ offsetof(struct ixl_hmc_rxq, hsplit_1),	2,	124 },
	{ offsetof(struct ixl_hmc_rxq, showiv),		1,	127 },
	{ offsetof(struct ixl_hmc_rxq, rxmax),		14,	174 },
	{ offsetof(struct ixl_hmc_rxq, tphrdesc_ena),	1,	193 },
	{ offsetof(struct ixl_hmc_rxq, tphwdesc_ena),	1,	194 },
	{ offsetof(struct ixl_hmc_rxq, tphdata_ena),	1,	195 },
	{ offsetof(struct ixl_hmc_rxq, tphhead_ena),	1,	196 },
	{ offsetof(struct ixl_hmc_rxq, lrxqthresh),	3,	198 },
	{ offsetof(struct ixl_hmc_rxq, prefena),	1,	201 },
};

#define IXL_HMC_RXQ_MINSIZE (201 + 1)

struct ixl_hmc_txq {
	uint16_t		head;
	uint8_t			new_context;
	uint64_t		base;
#define IXL_HMC_TXQ_BASE_UNIT		128
	uint8_t			fc_ena;
	uint8_t			timesync_ena;
	uint8_t			fd_ena;
	uint8_t			alt_vlan_ena;
	uint16_t		thead_wb;
	uint8_t			cpuid;
	uint8_t			head_wb_ena;
#define IXL_HMC_TXQ_DESC_WB		0
#define IXL_HMC_TXQ_HEAD_WB		1
	uint16_t		qlen;
	uint8_t			tphrdesc_ena;
	uint8_t			tphrpacket_ena;
	uint8_t			tphwdesc_ena;
	uint64_t		head_wb_addr;
	uint32_t		crc;
	uint16_t		rdylist;
	uint8_t			rdylist_act;
};

static const struct ixl_hmc_pack ixl_hmc_pack_txq[] = {
	{ offsetof(struct ixl_hmc_txq, head),		13,	0 },
	{ offsetof(struct ixl_hmc_txq, new_context),	1,	30 },
	{ offsetof(struct ixl_hmc_txq, base),		57,	32 },
	{ offsetof(struct ixl_hmc_txq, fc_ena),		1,	89 },
	{ offsetof(struct ixl_hmc_txq, timesync_ena),	1,	90 },
	{ offsetof(struct ixl_hmc_txq, fd_ena),		1,	91 },
	{ offsetof(struct ixl_hmc_txq, alt_vlan_ena),	1,	92 },
	{ offsetof(struct ixl_hmc_txq, cpuid),		8,	96 },
/* line 1 */
	{ offsetof(struct ixl_hmc_txq, thead_wb),	13,	0 + 128 },
	{ offsetof(struct ixl_hmc_txq, head_wb_ena),	1,	32 + 128 },
	{ offsetof(struct ixl_hmc_txq, qlen),		13,	33 + 128 },
	{ offsetof(struct ixl_hmc_txq, tphrdesc_ena),	1,	46 + 128 },
	{ offsetof(struct ixl_hmc_txq, tphrpacket_ena),	1,	47 + 128 },
	{ offsetof(struct ixl_hmc_txq, tphwdesc_ena),	1,	48 + 128 },
	{ offsetof(struct ixl_hmc_txq, head_wb_addr),	64,	64 + 128 },
/* line 7 */
	{ offsetof(struct ixl_hmc_txq, crc),		32,	0 + (7*128) },
	{ offsetof(struct ixl_hmc_txq, rdylist),	10,	84 + (7*128) },
	{ offsetof(struct ixl_hmc_txq, rdylist_act),	1,	94 + (7*128) },
};

#define IXL_HMC_TXQ_MINSIZE (94 + (7*128) + 1)

struct ixl_rss_key {
	uint32_t		 key[13];
};

struct ixl_rss_lut_128 {
	uint32_t		 entries[128 / sizeof(uint32_t)];
};

struct ixl_rss_lut_512 {
	uint32_t		 entries[512 / sizeof(uint32_t)];
};

/* driver structures */

struct ixl_vector;
struct ixl_chip;

struct ixl_tx_map {
	struct mbuf		*txm_m;
	bus_dmamap_t		 txm_map;
	bus_dmamap_t		 txm_map_tso;
	unsigned int		 txm_eop;
};

struct ixl_tx_ring {
	struct ixl_softc	*txr_sc;
	struct ixl_vector	*txr_vector;
	struct ifqueue		*txr_ifq;

	unsigned int		 txr_prod;
	unsigned int		 txr_cons;

	struct ixl_tx_map	*txr_maps;
	struct ixl_dmamem	 txr_mem;

	bus_size_t		 txr_tail;
	unsigned int		 txr_qid;
} __aligned(CACHE_LINE_SIZE);

struct ixl_rx_map {
	struct mbuf		*rxm_m;
	bus_dmamap_t		 rxm_map;
};

struct ixl_rx_ring {
	struct ixl_softc	*rxr_sc;
	struct ixl_vector	*rxr_vector;
	struct ifiqueue		*rxr_ifiq;

	struct if_rxring	 rxr_acct;
	struct timeout		 rxr_refill;

	unsigned int		 rxr_prod;
	unsigned int		 rxr_cons;

	struct ixl_rx_map	*rxr_maps;
	struct ixl_dmamem	 rxr_mem;

	struct mbuf		*rxr_m_head;
	struct mbuf		**rxr_m_tail;

	bus_size_t		 rxr_tail;
	unsigned int		 rxr_qid;
} __aligned(CACHE_LINE_SIZE);

struct ixl_atq {
	struct ixl_aq_desc	  iatq_desc;
	void			 *iatq_arg;
	void			(*iatq_fn)(struct ixl_softc *, void *);
};
SIMPLEQ_HEAD(ixl_atq_list, ixl_atq);

struct ixl_vector {
	struct ixl_softc	*iv_sc;
	struct ixl_rx_ring	*iv_rxr;
	struct ixl_tx_ring	*iv_txr;
	int			 iv_qid;
	void			*iv_ihc;
	char			 iv_name[16];
} __aligned(CACHE_LINE_SIZE);

struct ixl_softc {
	struct device		 sc_dev;
	const struct ixl_chip	*sc_chip;
	struct arpcom		 sc_ac;
	struct ifmedia		 sc_media;
	uint64_t		 sc_media_status;
	uint64_t		 sc_media_active;

	pci_chipset_tag_t	 sc_pc;
	pci_intr_handle_t	 sc_ih;
	void			*sc_ihc;
	pcitag_t		 sc_tag;

	bus_dma_tag_t		 sc_dmat;
	bus_space_tag_t		 sc_memt;
	bus_space_handle_t	 sc_memh;
	bus_size_t		 sc_mems;

	uint16_t		 sc_api_major;
	uint16_t		 sc_api_minor;
	uint8_t			 sc_pf_id;
	uint16_t		 sc_uplink_seid;	/* le */
	uint16_t		 sc_downlink_seid;	/* le */
	uint16_t		 sc_veb_seid;		/* le */
	uint16_t		 sc_vsi_number;		/* le */
	uint16_t		 sc_seid;
	unsigned int		 sc_base_queue;
	unsigned int		 sc_port;

	struct ixl_dmamem	 sc_scratch;

	const struct ixl_aq_regs *
				 sc_aq_regs;

	struct ixl_dmamem	 sc_atq;
	unsigned int		 sc_atq_prod;
	unsigned int		 sc_atq_cons;

	struct mutex		 sc_atq_mtx;
	struct ixl_dmamem	 sc_arq;
	struct task		 sc_arq_task;
	struct ixl_aq_bufs	 sc_arq_idle;
	struct ixl_aq_bufs	 sc_arq_live;
	struct if_rxring	 sc_arq_ring;
	unsigned int		 sc_arq_prod;
	unsigned int		 sc_arq_cons;

	struct mutex		 sc_link_state_mtx;
	struct task		 sc_link_state_task;
	struct ixl_atq		 sc_link_state_atq;

	struct ixl_dmamem	 sc_hmc_sd;
	struct ixl_dmamem	 sc_hmc_pd;
	struct ixl_hmc_entry	 sc_hmc_entries[IXL_HMC_COUNT];

	unsigned int		 sc_tx_ring_ndescs;
	unsigned int		 sc_rx_ring_ndescs;
	unsigned int		 sc_nqueues;	/* 1 << sc_nqueues */

	struct intrmap		*sc_intrmap;
	struct ixl_vector	*sc_vectors;

	struct rwlock		 sc_cfg_lock;
	unsigned int		 sc_dead;

	uint8_t			 sc_enaddr[ETHER_ADDR_LEN];

#if NKSTAT > 0
	struct mutex		 sc_kstat_mtx;
	struct timeout		 sc_kstat_tmo;
	struct kstat		*sc_port_kstat;
	struct kstat		*sc_vsi_kstat;
#endif
};
#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

#define delaymsec(_ms)	delay(1000 * (_ms))

static void	ixl_clear_hw(struct ixl_softc *);
static int	ixl_pf_reset(struct ixl_softc *);

static int	ixl_dmamem_alloc(struct ixl_softc *, struct ixl_dmamem *,
		    bus_size_t, u_int);
static void	ixl_dmamem_free(struct ixl_softc *, struct ixl_dmamem *);

static int	ixl_arq_fill(struct ixl_softc *);
static void	ixl_arq_unfill(struct ixl_softc *);

static int	ixl_atq_poll(struct ixl_softc *, struct ixl_aq_desc *,
		    unsigned int);
static void	ixl_atq_set(struct ixl_atq *,
		    void (*)(struct ixl_softc *, void *), void *);
static void	ixl_atq_post(struct ixl_softc *, struct ixl_atq *);
static void	ixl_atq_done(struct ixl_softc *);
static void	ixl_atq_exec(struct ixl_softc *, struct ixl_atq *,
		    const char *);
static int	ixl_get_version(struct ixl_softc *);
static int	ixl_pxe_clear(struct ixl_softc *);
static int	ixl_lldp_shut(struct ixl_softc *);
static int	ixl_get_mac(struct ixl_softc *);
static int	ixl_get_switch_config(struct ixl_softc *);
static int	ixl_phy_mask_ints(struct ixl_softc *);
static int	ixl_get_phy_types(struct ixl_softc *, uint64_t *);
static int	ixl_restart_an(struct ixl_softc *);
static int	ixl_hmc(struct ixl_softc *);
static void	ixl_hmc_free(struct ixl_softc *);
static int	ixl_get_vsi(struct ixl_softc *);
static int	ixl_set_vsi(struct ixl_softc *);
static int	ixl_get_link_status(struct ixl_softc *);
static int	ixl_set_link_status(struct ixl_softc *,
		    const struct ixl_aq_desc *);
static int	ixl_add_macvlan(struct ixl_softc *, uint8_t *, uint16_t,
		    uint16_t);
static int	ixl_remove_macvlan(struct ixl_softc *, uint8_t *, uint16_t,
		    uint16_t);
static void	ixl_link_state_update(void *);
static void	ixl_arq(void *);
static void	ixl_hmc_pack(void *, const void *,
		    const struct ixl_hmc_pack *, unsigned int);

static int	ixl_get_sffpage(struct ixl_softc *, struct if_sffpage *);
static int	ixl_sff_get_byte(struct ixl_softc *, uint8_t, uint32_t,
		    uint8_t *);
static int	ixl_sff_set_byte(struct ixl_softc *, uint8_t, uint32_t,
		    uint8_t);

static int	ixl_match(struct device *, void *, void *);
static void	ixl_attach(struct device *, struct device *, void *);

static void	ixl_media_add(struct ixl_softc *, uint64_t);
static int	ixl_media_change(struct ifnet *);
static void	ixl_media_status(struct ifnet *, struct ifmediareq *);
static void	ixl_watchdog(struct ifnet *);
static int	ixl_ioctl(struct ifnet *, u_long, caddr_t);
static void	ixl_start(struct ifqueue *);
static int	ixl_intr0(void *);
static int	ixl_intr_vector(void *);
static int	ixl_up(struct ixl_softc *);
static int	ixl_down(struct ixl_softc *);
static int	ixl_iff(struct ixl_softc *);

static struct ixl_tx_ring *
		ixl_txr_alloc(struct ixl_softc *, unsigned int);
static void	ixl_txr_qdis(struct ixl_softc *, struct ixl_tx_ring *, int);
static void	ixl_txr_config(struct ixl_softc *, struct ixl_tx_ring *);
static int	ixl_txr_enabled(struct ixl_softc *, struct ixl_tx_ring *);
static int	ixl_txr_disabled(struct ixl_softc *, struct ixl_tx_ring *);
static void	ixl_txr_unconfig(struct ixl_softc *, struct ixl_tx_ring *);
static void	ixl_txr_clean(struct ixl_softc *, struct ixl_tx_ring *);
static void	ixl_txr_free(struct ixl_softc *, struct ixl_tx_ring *);
static int	ixl_txeof(struct ixl_softc *, struct ixl_tx_ring *);

static struct ixl_rx_ring *
		ixl_rxr_alloc(struct ixl_softc *, unsigned int);
static void	ixl_rxr_config(struct ixl_softc *, struct ixl_rx_ring *);
static int	ixl_rxr_enabled(struct ixl_softc *, struct ixl_rx_ring *);
static int	ixl_rxr_disabled(struct ixl_softc *, struct ixl_rx_ring *);
static void	ixl_rxr_unconfig(struct ixl_softc *, struct ixl_rx_ring *);
static void	ixl_rxr_clean(struct ixl_softc *, struct ixl_rx_ring *);
static void	ixl_rxr_free(struct ixl_softc *, struct ixl_rx_ring *);
static int	ixl_rxeof(struct ixl_softc *, struct ixl_rx_ring *);
static void	ixl_rxfill(struct ixl_softc *, struct ixl_rx_ring *);
static void	ixl_rxrefill(void *);
static int	ixl_rxrinfo(struct ixl_softc *, struct if_rxrinfo *);
static void	ixl_rx_checksum(struct mbuf *, uint64_t);

#if NKSTAT > 0
static void	ixl_kstat_attach(struct ixl_softc *);
#endif

struct cfdriver ixl_cd = {
	NULL,
	"ixl",
	DV_IFNET,
};

const struct cfattach ixl_ca = {
	sizeof(struct ixl_softc),
	ixl_match,
	ixl_attach,
};

static const struct ixl_phy_type ixl_phy_type_map[] = {
	{ 1ULL << IXL_PHY_TYPE_SGMII,		IFM_1000_SGMII },
	{ 1ULL << IXL_PHY_TYPE_1000BASE_KX,	IFM_1000_KX },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_KX4,	IFM_10G_KX4 },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_KR,	IFM_10G_KR },
	{ 1ULL << IXL_PHY_TYPE_40GBASE_KR4,	IFM_40G_KR4 },
	{ 1ULL << IXL_PHY_TYPE_XAUI |
	  1ULL << IXL_PHY_TYPE_XFI,		IFM_10G_CX4 },
	{ 1ULL << IXL_PHY_TYPE_SFI,		IFM_10G_SFI },
	{ 1ULL << IXL_PHY_TYPE_XLAUI |
	  1ULL << IXL_PHY_TYPE_XLPPI,		IFM_40G_XLPPI },
	{ 1ULL << IXL_PHY_TYPE_40GBASE_CR4_CU |
	  1ULL << IXL_PHY_TYPE_40GBASE_CR4,	IFM_40G_CR4 },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_CR1_CU |
	  1ULL << IXL_PHY_TYPE_10GBASE_CR1,	IFM_10G_CR1 },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_AOC,	IFM_10G_AOC },
	{ 1ULL << IXL_PHY_TYPE_40GBASE_AOC,	IFM_40G_AOC },
	{ 1ULL << IXL_PHY_TYPE_100BASE_TX,	IFM_100_TX },
	{ 1ULL << IXL_PHY_TYPE_1000BASE_T_OPTICAL |
	  1ULL << IXL_PHY_TYPE_1000BASE_T,	IFM_1000_T },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_T,	IFM_10G_T },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_SR,	IFM_10G_SR },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_LR,	IFM_10G_LR },
	{ 1ULL << IXL_PHY_TYPE_10GBASE_SFPP_CU,	IFM_10G_SFP_CU },
	{ 1ULL << IXL_PHY_TYPE_40GBASE_SR4,	IFM_40G_SR4 },
	{ 1ULL << IXL_PHY_TYPE_40GBASE_LR4,	IFM_40G_LR4 },
	{ 1ULL << IXL_PHY_TYPE_1000BASE_SX,	IFM_1000_SX },
	{ 1ULL << IXL_PHY_TYPE_1000BASE_LX,	IFM_1000_LX },
	{ 1ULL << IXL_PHY_TYPE_20GBASE_KR2,	IFM_20G_KR2 },
	{ 1ULL << IXL_PHY_TYPE_25GBASE_KR,	IFM_25G_KR },
	{ 1ULL << IXL_PHY_TYPE_25GBASE_CR,	IFM_25G_CR },
	{ 1ULL << IXL_PHY_TYPE_25GBASE_SR,	IFM_25G_SR },
	{ 1ULL << IXL_PHY_TYPE_25GBASE_LR,	IFM_25G_LR },
	{ 1ULL << IXL_PHY_TYPE_25GBASE_AOC,	IFM_25G_AOC },
	{ 1ULL << IXL_PHY_TYPE_25GBASE_ACC,	IFM_25G_CR },
};

static const struct ixl_speed_type ixl_speed_type_map[] = {
	{ IXL_AQ_LINK_SPEED_40GB,		IF_Gbps(40) },
	{ IXL_AQ_LINK_SPEED_25GB,		IF_Gbps(25) },
	{ IXL_AQ_LINK_SPEED_10GB,		IF_Gbps(10) },
	{ IXL_AQ_LINK_SPEED_1GB,		IF_Gbps(1) },
};

static const struct ixl_aq_regs ixl_pf_aq_regs = {
	.atq_tail	= I40E_PF_ATQT,
	.atq_tail_mask	= I40E_PF_ATQT_ATQT_MASK,
	.atq_head	= I40E_PF_ATQH,
	.atq_head_mask	= I40E_PF_ATQH_ATQH_MASK,
	.atq_len	= I40E_PF_ATQLEN,
	.atq_bal	= I40E_PF_ATQBAL,
	.atq_bah	= I40E_PF_ATQBAH,
	.atq_len_enable	= I40E_PF_ATQLEN_ATQENABLE_MASK,

	.arq_tail	= I40E_PF_ARQT,
	.arq_tail_mask	= I40E_PF_ARQT_ARQT_MASK,
	.arq_head	= I40E_PF_ARQH,
	.arq_head_mask	= I40E_PF_ARQH_ARQH_MASK,
	.arq_len	= I40E_PF_ARQLEN,
	.arq_bal	= I40E_PF_ARQBAL,
	.arq_bah	= I40E_PF_ARQBAH,
	.arq_len_enable	= I40E_PF_ARQLEN_ARQENABLE_MASK,
};

#define ixl_rd(_s, _r) \
	bus_space_read_4((_s)->sc_memt, (_s)->sc_memh, (_r))
#define ixl_wr(_s, _r, _v) \
	bus_space_write_4((_s)->sc_memt, (_s)->sc_memh, (_r), (_v))
#define ixl_barrier(_s, _r, _l, _o) \
	bus_space_barrier((_s)->sc_memt, (_s)->sc_memh, (_r), (_l), (_o))
#define ixl_intr_enable(_s) \
	ixl_wr((_s), I40E_PFINT_DYN_CTL0, I40E_PFINT_DYN_CTL0_INTENA_MASK | \
	    I40E_PFINT_DYN_CTL0_CLEARPBA_MASK | \
	    (IXL_NOITR << I40E_PFINT_DYN_CTL0_ITR_INDX_SHIFT))

#define ixl_nqueues(_sc)	(1 << (_sc)->sc_nqueues)

#ifdef __LP64__
#define ixl_dmamem_hi(_ixm)	(uint32_t)(IXL_DMA_DVA(_ixm) >> 32)
#else
#define ixl_dmamem_hi(_ixm)	0
#endif

#define ixl_dmamem_lo(_ixm)	(uint32_t)IXL_DMA_DVA(_ixm)

static inline void
ixl_aq_dva(struct ixl_aq_desc *iaq, bus_addr_t addr)
{
#ifdef __LP64__
	htolem32(&iaq->iaq_param[2], addr >> 32);
#else
	iaq->iaq_param[2] = htole32(0);
#endif
	htolem32(&iaq->iaq_param[3], addr);
}

#if _BYTE_ORDER == _BIG_ENDIAN
#define HTOLE16(_x)	(uint16_t)(((_x) & 0xff) << 8 | ((_x) & 0xff00) >> 8)
#else
#define HTOLE16(_x)	(_x)
#endif

static struct rwlock ixl_sff_lock = RWLOCK_INITIALIZER("ixlsff");

/* deal with differences between chips */

struct ixl_chip {
	uint64_t		  ic_rss_hena;
	uint32_t		(*ic_rd_ctl)(struct ixl_softc *, uint32_t);
	void			(*ic_wr_ctl)(struct ixl_softc *, uint32_t,
				      uint32_t);

	int			(*ic_set_rss_key)(struct ixl_softc *,
				      const struct ixl_rss_key *);
	int			(*ic_set_rss_lut)(struct ixl_softc *,
				      const struct ixl_rss_lut_128 *);
};

static inline uint64_t
ixl_rss_hena(struct ixl_softc *sc)
{
	return (sc->sc_chip->ic_rss_hena);
}

static inline uint32_t
ixl_rd_ctl(struct ixl_softc *sc, uint32_t r)
{
	return ((*sc->sc_chip->ic_rd_ctl)(sc, r));
}

static inline void
ixl_wr_ctl(struct ixl_softc *sc, uint32_t r, uint32_t v)
{
	(*sc->sc_chip->ic_wr_ctl)(sc, r, v);
}

static inline int
ixl_set_rss_key(struct ixl_softc *sc, const struct ixl_rss_key *rsskey)
{
	return ((*sc->sc_chip->ic_set_rss_key)(sc, rsskey));
}

static inline int
ixl_set_rss_lut(struct ixl_softc *sc, const struct ixl_rss_lut_128 *lut)
{
	return ((*sc->sc_chip->ic_set_rss_lut)(sc, lut));
}

/* 710 chip specifics */

static uint32_t		ixl_710_rd_ctl(struct ixl_softc *, uint32_t);
static void		ixl_710_wr_ctl(struct ixl_softc *, uint32_t, uint32_t);
static int		ixl_710_set_rss_key(struct ixl_softc *,
			    const struct ixl_rss_key *);
static int		ixl_710_set_rss_lut(struct ixl_softc *,
			    const struct ixl_rss_lut_128 *);

static const struct ixl_chip ixl_710 = {
	.ic_rss_hena =		IXL_RSS_HENA_BASE_710,
	.ic_rd_ctl =		ixl_710_rd_ctl,
	.ic_wr_ctl =		ixl_710_wr_ctl,
	.ic_set_rss_key =	ixl_710_set_rss_key,
	.ic_set_rss_lut =	ixl_710_set_rss_lut,
};

/* 722 chip specifics */

static uint32_t		ixl_722_rd_ctl(struct ixl_softc *, uint32_t);
static void		ixl_722_wr_ctl(struct ixl_softc *, uint32_t, uint32_t);
static int		ixl_722_set_rss_key(struct ixl_softc *,
			    const struct ixl_rss_key *);
static int		ixl_722_set_rss_lut(struct ixl_softc *,
			    const struct ixl_rss_lut_128 *);

static const struct ixl_chip ixl_722 = {
	.ic_rss_hena =		IXL_RSS_HENA_BASE_722,
	.ic_rd_ctl =		ixl_722_rd_ctl,
	.ic_wr_ctl =		ixl_722_wr_ctl,
	.ic_set_rss_key =	ixl_722_set_rss_key,
	.ic_set_rss_lut =	ixl_722_set_rss_lut,
};

/*
 * 710 chips using an older firmware/API use the same ctl ops as
 * 722 chips. or 722 chips use the same ctl ops as 710 chips in early
 * firmware/API versions?
*/

static const struct ixl_chip ixl_710_decrepit = {
	.ic_rss_hena =		IXL_RSS_HENA_BASE_710,
	.ic_rd_ctl =		ixl_722_rd_ctl,
	.ic_wr_ctl =		ixl_722_wr_ctl,
	.ic_set_rss_key =	ixl_710_set_rss_key,
	.ic_set_rss_lut =	ixl_710_set_rss_lut,
};

/* driver code */

struct ixl_device {
	const struct ixl_chip	*id_chip;
	pci_vendor_id_t		 id_vid;
	pci_product_id_t	 id_pid;
};

static const struct ixl_device ixl_devices[] = {
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_10G_SFP },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_10G_SFP_2 },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XL710_40G_BP },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_10G_BP, },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XL710_QSFP_1 },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XL710_QSFP_2 },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_10G_QSFP },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_10G_BASET },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XL710_20G_BP_1 },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XL710_20G_BP_2 },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_T4_10G },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XXV710_25G_BP },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_XXV710_25G_SFP28, },
	{ &ixl_710, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X710_10G_T, },
	{ &ixl_722, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X722_10G_KX },
	{ &ixl_722, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X722_10G_QSFP },
	{ &ixl_722, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X722_10G_SFP_1 },
	{ &ixl_722, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X722_1G },
	{ &ixl_722, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X722_10G_T },
	{ &ixl_722, PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_X722_10G_SFP_2 },
};

static const struct ixl_device *
ixl_device_lookup(struct pci_attach_args *pa)
{
	pci_vendor_id_t vid = PCI_VENDOR(pa->pa_id);
	pci_product_id_t pid = PCI_PRODUCT(pa->pa_id);
	const struct ixl_device *id;
	unsigned int i;

	for (i = 0; i < nitems(ixl_devices); i++) {
		id = &ixl_devices[i];
		if (id->id_vid == vid && id->id_pid == pid)
			return (id);
	}

	return (NULL);
}

static int
ixl_match(struct device *parent, void *match, void *aux)
{
	return (ixl_device_lookup(aux) != NULL);
}

void
ixl_attach(struct device *parent, struct device *self, void *aux)
{
	struct ixl_softc *sc = (struct ixl_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct pci_attach_args *pa = aux;
	pcireg_t memtype;
	uint32_t port, ari, func;
	uint64_t phy_types = 0;
	unsigned int nqueues, i;
	int tries;

	rw_init(&sc->sc_cfg_lock, "ixlcfg");

	sc->sc_chip = ixl_device_lookup(pa)->id_chip;
	sc->sc_pc = pa->pa_pc;
	sc->sc_tag = pa->pa_tag;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_aq_regs = &ixl_pf_aq_regs;

	sc->sc_nqueues = 0; /* 1 << 0 is 1 queue */
	sc->sc_tx_ring_ndescs = 1024;
	sc->sc_rx_ring_ndescs = 1024;

	memtype = pci_mapreg_type(sc->sc_pc, sc->sc_tag, IXL_PCIREG);
	if (pci_mapreg_map(pa, IXL_PCIREG, memtype, 0,
	    &sc->sc_memt, &sc->sc_memh, NULL, &sc->sc_mems, 0)) {
		printf(": unable to map registers\n");
		return;
	}

	sc->sc_base_queue = (ixl_rd(sc, I40E_PFLAN_QALLOC) &
	    I40E_PFLAN_QALLOC_FIRSTQ_MASK) >>
	    I40E_PFLAN_QALLOC_FIRSTQ_SHIFT;

	ixl_clear_hw(sc);
	if (ixl_pf_reset(sc) == -1) {
		/* error printed by ixl_pf_reset */
		goto unmap;
	}

	port = ixl_rd(sc, I40E_PFGEN_PORTNUM);
	port &= I40E_PFGEN_PORTNUM_PORT_NUM_MASK;
	port >>= I40E_PFGEN_PORTNUM_PORT_NUM_SHIFT;
	sc->sc_port = port;
	printf(": port %u", port);

	ari = ixl_rd(sc, I40E_GLPCI_CAPSUP);
	ari &= I40E_GLPCI_CAPSUP_ARI_EN_MASK;
	ari >>= I40E_GLPCI_CAPSUP_ARI_EN_SHIFT;

	func = ixl_rd(sc, I40E_PF_FUNC_RID);
	sc->sc_pf_id = func & (ari ? 0xff : 0x7);

	/* initialise the adminq */

	mtx_init(&sc->sc_atq_mtx, IPL_NET);

	if (ixl_dmamem_alloc(sc, &sc->sc_atq,
	    sizeof(struct ixl_aq_desc) * IXL_AQ_NUM, IXL_AQ_ALIGN) != 0) {
		printf("\n" "%s: unable to allocate atq\n", DEVNAME(sc));
		goto unmap;
	}

	SIMPLEQ_INIT(&sc->sc_arq_idle);
	SIMPLEQ_INIT(&sc->sc_arq_live);
	if_rxr_init(&sc->sc_arq_ring, 2, IXL_AQ_NUM - 1);
	task_set(&sc->sc_arq_task, ixl_arq, sc);
	sc->sc_arq_cons = 0;
	sc->sc_arq_prod = 0;

	if (ixl_dmamem_alloc(sc, &sc->sc_arq,
	    sizeof(struct ixl_aq_desc) * IXL_AQ_NUM, IXL_AQ_ALIGN) != 0) {
		printf("\n" "%s: unable to allocate arq\n", DEVNAME(sc));
		goto free_atq;
	}

	if (!ixl_arq_fill(sc)) {
		printf("\n" "%s: unable to fill arq descriptors\n",
		    DEVNAME(sc));
		goto free_arq;
	}

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_arq),
	    0, IXL_DMA_LEN(&sc->sc_arq),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	for (tries = 0; tries < 10; tries++) {
		int rv;

		sc->sc_atq_cons = 0;
		sc->sc_atq_prod = 0;

		ixl_wr(sc, sc->sc_aq_regs->atq_head, 0);
		ixl_wr(sc, sc->sc_aq_regs->arq_head, 0);
		ixl_wr(sc, sc->sc_aq_regs->atq_tail, 0);
		ixl_wr(sc, sc->sc_aq_regs->arq_tail, 0);

		ixl_barrier(sc, 0, sc->sc_mems, BUS_SPACE_BARRIER_WRITE);

		ixl_wr(sc, sc->sc_aq_regs->atq_bal,
		    ixl_dmamem_lo(&sc->sc_atq));
		ixl_wr(sc, sc->sc_aq_regs->atq_bah,
		    ixl_dmamem_hi(&sc->sc_atq));
		ixl_wr(sc, sc->sc_aq_regs->atq_len,
		    sc->sc_aq_regs->atq_len_enable | IXL_AQ_NUM);

		ixl_wr(sc, sc->sc_aq_regs->arq_bal,
		    ixl_dmamem_lo(&sc->sc_arq));
		ixl_wr(sc, sc->sc_aq_regs->arq_bah,
		    ixl_dmamem_hi(&sc->sc_arq));
		ixl_wr(sc, sc->sc_aq_regs->arq_len,
		    sc->sc_aq_regs->arq_len_enable | IXL_AQ_NUM);

		rv = ixl_get_version(sc);
		if (rv == 0)
			break;
		if (rv != ETIMEDOUT) {
			printf(", unable to get firmware version\n");
			goto shutdown;
		}

		delaymsec(100);
	}

	ixl_wr(sc, sc->sc_aq_regs->arq_tail, sc->sc_arq_prod);

	if (ixl_pxe_clear(sc) != 0) {
		/* error printed by ixl_pxe_clear */
		goto shutdown;
	}

	if (ixl_get_mac(sc) != 0) {
		/* error printed by ixl_get_mac */
		goto shutdown;
	}

	if (pci_intr_map_msix(pa, 0, &sc->sc_ih) == 0) {
		int nmsix = pci_intr_msix_count(pa);
		if (nmsix > 1) { /* we used 1 (the 0th) for the adminq */
			nmsix--;

			sc->sc_intrmap = intrmap_create(&sc->sc_dev,
			    nmsix, IXL_MAX_VECTORS, INTRMAP_POWEROF2);
			nqueues = intrmap_count(sc->sc_intrmap);
			KASSERT(nqueues > 0);
			KASSERT(powerof2(nqueues));
			sc->sc_nqueues = fls(nqueues) - 1;
		}
	} else {
		if (pci_intr_map_msi(pa, &sc->sc_ih) != 0 &&
		    pci_intr_map(pa, &sc->sc_ih) != 0) {
			printf(", unable to map interrupt\n");
			goto shutdown;
		}
	}

	nqueues = ixl_nqueues(sc);

	printf(", %s, %d queue%s, address %s\n",
	    pci_intr_string(sc->sc_pc, sc->sc_ih), ixl_nqueues(sc),
	    (nqueues > 1 ? "s" : ""),
	    ether_sprintf(sc->sc_ac.ac_enaddr));

	if (ixl_hmc(sc) != 0) {
		/* error printed by ixl_hmc */
		goto shutdown;
	}

	if (ixl_lldp_shut(sc) != 0) {
		/* error printed by ixl_lldp_shut */
		goto free_hmc;
	}

	if (ixl_phy_mask_ints(sc) != 0) {
		/* error printed by ixl_phy_mask_ints */
		goto free_hmc;
	}

	if (ixl_restart_an(sc) != 0) {
		/* error printed by ixl_restart_an */
		goto free_hmc;
	}

	if (ixl_get_switch_config(sc) != 0) {
		/* error printed by ixl_get_switch_config */
		goto free_hmc;
	}

	if (ixl_get_phy_types(sc, &phy_types) != 0) {
		/* error printed by ixl_get_phy_abilities */
		goto free_hmc;
	}

	mtx_init(&sc->sc_link_state_mtx, IPL_NET);
	if (ixl_get_link_status(sc) != 0) {
		/* error printed by ixl_get_link_status */
		goto free_hmc;
	}

	if (ixl_dmamem_alloc(sc, &sc->sc_scratch,
	    sizeof(struct ixl_aq_vsi_data), 8) != 0) {
		printf("%s: unable to allocate scratch buffer\n", DEVNAME(sc));
		goto free_hmc;
	}

	if (ixl_get_vsi(sc) != 0) {
		/* error printed by ixl_get_vsi */
		goto free_hmc;
	}

	if (ixl_set_vsi(sc) != 0) {
		/* error printed by ixl_set_vsi */
		goto free_scratch;
	}

	sc->sc_ihc = pci_intr_establish(sc->sc_pc, sc->sc_ih,
	    IPL_NET | IPL_MPSAFE, ixl_intr0, sc, DEVNAME(sc));
	if (sc->sc_ihc == NULL) {
		printf("%s: unable to establish interrupt handler\n",
		    DEVNAME(sc));
		goto free_scratch;
	}

	sc->sc_vectors = mallocarray(sizeof(*sc->sc_vectors), nqueues,
	    M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (sc->sc_vectors == NULL) {
		printf("%s: unable to allocate vectors\n", DEVNAME(sc));
		goto free_scratch;
	}

	for (i = 0; i < nqueues; i++) {
		struct ixl_vector *iv = &sc->sc_vectors[i];
		iv->iv_sc = sc;
		iv->iv_qid = i;
		snprintf(iv->iv_name, sizeof(iv->iv_name),
		    "%s:%u", DEVNAME(sc), i); /* truncated? */
	}

	if (sc->sc_intrmap) {
		for (i = 0; i < nqueues; i++) {
			struct ixl_vector *iv = &sc->sc_vectors[i];
			pci_intr_handle_t ih;
			int v = i + 1; /* 0 is used for adminq */

			if (pci_intr_map_msix(pa, v, &ih)) {
				printf("%s: unable to map msi-x vector %d\n",
				    DEVNAME(sc), v);
				goto free_vectors;
			}

			iv->iv_ihc = pci_intr_establish_cpu(sc->sc_pc, ih,
			    IPL_NET | IPL_MPSAFE,
			    intrmap_cpu(sc->sc_intrmap, i),
			    ixl_intr_vector, iv, iv->iv_name);
			if (iv->iv_ihc == NULL) {
				printf("%s: unable to establish interrupt %d\n",
				    DEVNAME(sc), v);
				goto free_vectors;
			}

			ixl_wr(sc, I40E_PFINT_DYN_CTLN(i),
			    I40E_PFINT_DYN_CTLN_INTENA_MASK |
			    I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
			    (IXL_NOITR << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT));
		}
	}

	/* fixup the chip ops for older fw releases */
	if (sc->sc_chip == &ixl_710 &&
	    sc->sc_api_major == 1 && sc->sc_api_minor < 5)
		sc->sc_chip = &ixl_710_decrepit;

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = ixl_ioctl;
	ifp->if_qstart = ixl_start;
	ifp->if_watchdog = ixl_watchdog;
	ifp->if_hardmtu = IXL_HARDMTU;
	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifq_init_maxlen(&ifp->if_snd, sc->sc_tx_ring_ndescs);

	ifp->if_capabilities = IFCAP_VLAN_MTU;
#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif
	ifp->if_capabilities |= IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4 |
	    IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
	ifp->if_capabilities |= IFCAP_TSOv4 | IFCAP_TSOv6;

	ifp->if_capabilities |= IFCAP_LRO;
#if notyet
	/* for now tcplro at ixl(4) is default off */
	ifp->if_xflags |= IFXF_LRO;
#endif

	ifmedia_init(&sc->sc_media, 0, ixl_media_change, ixl_media_status);

	ixl_media_add(sc, phy_types);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	if_attach_queues(ifp, nqueues);
	if_attach_iqueues(ifp, nqueues);

	task_set(&sc->sc_link_state_task, ixl_link_state_update, sc);
	ixl_wr(sc, I40E_PFINT_ICR0_ENA,
	    I40E_PFINT_ICR0_ENA_LINK_STAT_CHANGE_MASK |
	    I40E_PFINT_ICR0_ENA_ADMINQ_MASK);
	ixl_wr(sc, I40E_PFINT_STAT_CTL0,
	    IXL_NOITR << I40E_PFINT_STAT_CTL0_OTHER_ITR_INDX_SHIFT);

	/* remove default mac filter and replace it so we can see vlans */
	ixl_remove_macvlan(sc, sc->sc_ac.ac_enaddr, 0, 0);
	ixl_remove_macvlan(sc, sc->sc_ac.ac_enaddr, 0,
	    IXL_AQ_OP_REMOVE_MACVLAN_IGNORE_VLAN);
	ixl_add_macvlan(sc, sc->sc_ac.ac_enaddr, 0,
	    IXL_AQ_OP_ADD_MACVLAN_IGNORE_VLAN);
	ixl_add_macvlan(sc, etherbroadcastaddr, 0,
	    IXL_AQ_OP_ADD_MACVLAN_IGNORE_VLAN);
	memcpy(sc->sc_enaddr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);

	ixl_intr_enable(sc);

#if NKSTAT > 0
	ixl_kstat_attach(sc);
#endif

	return;
free_vectors:
	if (sc->sc_intrmap != NULL) {
		for (i = 0; i < nqueues; i++) {
			struct ixl_vector *iv = &sc->sc_vectors[i];
			if (iv->iv_ihc == NULL)
				continue;
			pci_intr_disestablish(sc->sc_pc, iv->iv_ihc);
		}
	}
	free(sc->sc_vectors, M_DEVBUF, nqueues * sizeof(*sc->sc_vectors));
free_scratch:
	ixl_dmamem_free(sc, &sc->sc_scratch);
free_hmc:
	ixl_hmc_free(sc);
shutdown:
	ixl_wr(sc, sc->sc_aq_regs->atq_head, 0);
	ixl_wr(sc, sc->sc_aq_regs->arq_head, 0);
	ixl_wr(sc, sc->sc_aq_regs->atq_tail, 0);
	ixl_wr(sc, sc->sc_aq_regs->arq_tail, 0);

	ixl_wr(sc, sc->sc_aq_regs->atq_bal, 0);
	ixl_wr(sc, sc->sc_aq_regs->atq_bah, 0);
	ixl_wr(sc, sc->sc_aq_regs->atq_len, 0);

	ixl_wr(sc, sc->sc_aq_regs->arq_bal, 0);
	ixl_wr(sc, sc->sc_aq_regs->arq_bah, 0);
	ixl_wr(sc, sc->sc_aq_regs->arq_len, 0);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_arq),
	    0, IXL_DMA_LEN(&sc->sc_arq),
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	ixl_arq_unfill(sc);

free_arq:
	ixl_dmamem_free(sc, &sc->sc_arq);
free_atq:
	ixl_dmamem_free(sc, &sc->sc_atq);
unmap:
	bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_mems);
	sc->sc_mems = 0;

	if (sc->sc_intrmap != NULL)
		intrmap_destroy(sc->sc_intrmap);
}

static void
ixl_media_add(struct ixl_softc *sc, uint64_t phy_types)
{
	struct ifmedia *ifm = &sc->sc_media;
	const struct ixl_phy_type *itype;
	unsigned int i;

	for (i = 0; i < nitems(ixl_phy_type_map); i++) {
		itype = &ixl_phy_type_map[i];

		if (ISSET(phy_types, itype->phy_type))
			ifmedia_add(ifm, IFM_ETHER | itype->ifm_type, 0, NULL);
	}
}

static int
ixl_media_change(struct ifnet *ifp)
{
	/* ignore? */
	return (EOPNOTSUPP);
}

static void
ixl_media_status(struct ifnet *ifp, struct ifmediareq *ifm)
{
	struct ixl_softc *sc = ifp->if_softc;

	KERNEL_ASSERT_LOCKED();

	mtx_enter(&sc->sc_link_state_mtx);
	ifm->ifm_status = sc->sc_media_status;
	ifm->ifm_active = sc->sc_media_active;
	mtx_leave(&sc->sc_link_state_mtx);
}

static void
ixl_watchdog(struct ifnet *ifp)
{

}

int
ixl_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ixl_softc *sc = (struct ixl_softc *)ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	uint8_t addrhi[ETHER_ADDR_LEN], addrlo[ETHER_ADDR_LEN];
	int aqerror, error = 0;

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				error = ixl_up(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ixl_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = ixl_rxrinfo(sc, (struct if_rxrinfo *)ifr->ifr_data);
		break;

	case SIOCADDMULTI:
		if (ether_addmulti(ifr, &sc->sc_ac) == ENETRESET) {
			error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
			if (error != 0)
				return (error);

			aqerror = ixl_add_macvlan(sc, addrlo, 0,
			    IXL_AQ_OP_ADD_MACVLAN_IGNORE_VLAN);
			if (aqerror == IXL_AQ_RC_ENOSPC) {
				ether_delmulti(ifr, &sc->sc_ac);
				error = ENOSPC;
			}

			if (sc->sc_ac.ac_multirangecnt > 0) {
				SET(ifp->if_flags, IFF_ALLMULTI);
				error = ENETRESET;
			}
		}
		break;

	case SIOCDELMULTI:
		if (ether_delmulti(ifr, &sc->sc_ac) == ENETRESET) {
			error = ether_multiaddr(&ifr->ifr_addr, addrlo, addrhi);
			if (error != 0)
				return (error);

			ixl_remove_macvlan(sc, addrlo, 0,
			    IXL_AQ_OP_REMOVE_MACVLAN_IGNORE_VLAN);

			if (ISSET(ifp->if_flags, IFF_ALLMULTI) &&
			    sc->sc_ac.ac_multirangecnt == 0) {
				CLR(ifp->if_flags, IFF_ALLMULTI);
				error = ENETRESET;
			}
		}
		break;

	case SIOCGIFSFFPAGE:
		error = rw_enter(&ixl_sff_lock, RW_WRITE|RW_INTR);
		if (error != 0)
			break;

		error = ixl_get_sffpage(sc, (struct if_sffpage *)data);
		rw_exit(&ixl_sff_lock);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET)
		error = ixl_iff(sc);

	return (error);
}

static inline void *
ixl_hmc_kva(struct ixl_softc *sc, unsigned int type, unsigned int i)
{
	uint8_t *kva = IXL_DMA_KVA(&sc->sc_hmc_pd);
	struct ixl_hmc_entry *e = &sc->sc_hmc_entries[type];

	if (i >= e->hmc_count)
		return (NULL);

	kva += e->hmc_base;
	kva += i * e->hmc_size;

	return (kva);
}

static inline size_t
ixl_hmc_len(struct ixl_softc *sc, unsigned int type)
{
	struct ixl_hmc_entry *e = &sc->sc_hmc_entries[type];

	return (e->hmc_size);
}

static int
ixl_configure_rss(struct ixl_softc *sc)
{
	struct ixl_rss_key rsskey;
	struct ixl_rss_lut_128 lut;
	uint8_t *lute = (uint8_t *)&lut;
	uint64_t rss_hena;
	unsigned int i, nqueues;
	int error;

#if 0
	/* if we want to do a 512 entry LUT, do this. */
	uint32_t v = ixl_rd_ctl(sc, I40E_PFQF_CTL_0);
	SET(v, I40E_PFQF_CTL_0_HASHLUTSIZE_MASK);
	ixl_wr_ctl(sc, I40E_PFQF_CTL_0, v);
#endif

	stoeplitz_to_key(&rsskey, sizeof(rsskey));

	nqueues = ixl_nqueues(sc);
	for (i = 0; i < sizeof(lut); i++) {
		/*
		 * ixl must have a power of 2 rings, so using mod
		 * to populate the table is fine.
		 */
		lute[i] = i % nqueues;
	}

	error = ixl_set_rss_key(sc, &rsskey);
	if (error != 0)
		return (error);

	rss_hena = (uint64_t)ixl_rd_ctl(sc, I40E_PFQF_HENA(0));
	rss_hena |= (uint64_t)ixl_rd_ctl(sc, I40E_PFQF_HENA(1)) << 32;
	rss_hena |= ixl_rss_hena(sc);
	ixl_wr_ctl(sc, I40E_PFQF_HENA(0), rss_hena);
	ixl_wr_ctl(sc, I40E_PFQF_HENA(1), rss_hena >> 32);

	error = ixl_set_rss_lut(sc, &lut);
	if (error != 0)
		return (error);

	/* nothing to clena up :( */

	return (0);
}

static int
ixl_up(struct ixl_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ifqueue *ifq;
	struct ifiqueue *ifiq;
	struct ixl_vector *iv;
	struct ixl_rx_ring *rxr;
	struct ixl_tx_ring *txr;
	unsigned int nqueues, i;
	uint32_t reg;
	int rv = ENOMEM;

	nqueues = ixl_nqueues(sc);

	rw_enter_write(&sc->sc_cfg_lock);
	if (sc->sc_dead) {
		rw_exit_write(&sc->sc_cfg_lock);
		return (ENXIO);
	}

	/* allocation is the only thing that can fail, so do it up front */
	for (i = 0; i < nqueues; i++) {
		rxr = ixl_rxr_alloc(sc, i);
		if (rxr == NULL)
			goto free;

		txr = ixl_txr_alloc(sc, i);
		if (txr == NULL) {
			ixl_rxr_free(sc, rxr);
			goto free;
		}

		/* wire everything together */
		iv = &sc->sc_vectors[i];
		iv->iv_rxr = rxr;
		iv->iv_txr = txr;

		ifq = ifp->if_ifqs[i];
		ifq->ifq_softc = txr;
		txr->txr_ifq = ifq;

		ifiq = ifp->if_iqs[i];
		ifiq->ifiq_softc = rxr;
		rxr->rxr_ifiq = ifiq;
	}

	/* XXX wait 50ms from completion of last RX queue disable */

	for (i = 0; i < nqueues; i++) {
		iv = &sc->sc_vectors[i];
		rxr = iv->iv_rxr;
		txr = iv->iv_txr;

		ixl_txr_qdis(sc, txr, 1);

		ixl_rxr_config(sc, rxr);
		ixl_txr_config(sc, txr);

		ixl_wr(sc, I40E_QTX_CTL(i), I40E_QTX_CTL_PF_QUEUE |
		    (sc->sc_pf_id << I40E_QTX_CTL_PF_INDX_SHIFT));

		ixl_wr(sc, rxr->rxr_tail, 0);
		ixl_rxfill(sc, rxr);

		reg = ixl_rd(sc, I40E_QRX_ENA(i));
		SET(reg, I40E_QRX_ENA_QENA_REQ_MASK);
		ixl_wr(sc, I40E_QRX_ENA(i), reg);

		reg = ixl_rd(sc, I40E_QTX_ENA(i));
		SET(reg, I40E_QTX_ENA_QENA_REQ_MASK);
		ixl_wr(sc, I40E_QTX_ENA(i), reg);
	}

	for (i = 0; i < nqueues; i++) {
		iv = &sc->sc_vectors[i];
		rxr = iv->iv_rxr;
		txr = iv->iv_txr;

		if (ixl_rxr_enabled(sc, rxr) != 0)
			goto down;

		if (ixl_txr_enabled(sc, txr) != 0)
			goto down;
	}

	ixl_configure_rss(sc);

	SET(ifp->if_flags, IFF_RUNNING);

	if (sc->sc_intrmap == NULL) {
		ixl_wr(sc, I40E_PFINT_LNKLST0,
		    (I40E_INTR_NOTX_QUEUE <<
		     I40E_PFINT_LNKLST0_FIRSTQ_INDX_SHIFT) |
		    (I40E_QUEUE_TYPE_RX <<
		     I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_SHIFT));

		ixl_wr(sc, I40E_QINT_RQCTL(I40E_INTR_NOTX_QUEUE),
		    (I40E_INTR_NOTX_INTR << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
		    (I40E_ITR_INDEX_RX << I40E_QINT_RQCTL_ITR_INDX_SHIFT) |
		    (I40E_INTR_NOTX_RX_QUEUE <<
		     I40E_QINT_RQCTL_MSIX0_INDX_SHIFT) |
		    (I40E_INTR_NOTX_QUEUE << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT) |
		    (I40E_QUEUE_TYPE_TX << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT) |
		    I40E_QINT_RQCTL_CAUSE_ENA_MASK);

		ixl_wr(sc, I40E_QINT_TQCTL(I40E_INTR_NOTX_QUEUE),
		    (I40E_INTR_NOTX_INTR << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
		    (I40E_ITR_INDEX_TX << I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
		    (I40E_INTR_NOTX_TX_QUEUE <<
		     I40E_QINT_TQCTL_MSIX0_INDX_SHIFT) |
		    (I40E_QUEUE_TYPE_EOL << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT) |
		    (I40E_QUEUE_TYPE_RX << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT) |
		    I40E_QINT_TQCTL_CAUSE_ENA_MASK);
	} else {
		/* vector 0 has no queues */
		ixl_wr(sc, I40E_PFINT_LNKLST0,
		    I40E_QUEUE_TYPE_EOL <<
		    I40E_PFINT_LNKLST0_FIRSTQ_INDX_SHIFT);

		/* queue n is mapped to vector n+1 */
		for (i = 0; i < nqueues; i++) {
			/* LNKLSTN(i) configures vector i+1 */
			ixl_wr(sc, I40E_PFINT_LNKLSTN(i),
			    (i << I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT) |
			    (I40E_QUEUE_TYPE_RX <<
			     I40E_PFINT_LNKLSTN_FIRSTQ_TYPE_SHIFT));
			ixl_wr(sc, I40E_QINT_RQCTL(i),
			    ((i+1) << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
			    (I40E_ITR_INDEX_RX <<
			     I40E_QINT_RQCTL_ITR_INDX_SHIFT) |
			    (i << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT) |
			    (I40E_QUEUE_TYPE_TX <<
			     I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT) |
			    I40E_QINT_RQCTL_CAUSE_ENA_MASK);
			ixl_wr(sc, I40E_QINT_TQCTL(i),
			    ((i+1) << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
			    (I40E_ITR_INDEX_TX <<
			     I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
			    (I40E_QUEUE_TYPE_EOL <<
			     I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT) |
			    (I40E_QUEUE_TYPE_RX <<
			     I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT) |
			    I40E_QINT_TQCTL_CAUSE_ENA_MASK);

			ixl_wr(sc, I40E_PFINT_ITRN(0, i), 0x7a);
			ixl_wr(sc, I40E_PFINT_ITRN(1, i), 0x7a);
			ixl_wr(sc, I40E_PFINT_ITRN(2, i), 0);
		}
	}

	ixl_wr(sc, I40E_PFINT_ITR0(0), 0x7a);
	ixl_wr(sc, I40E_PFINT_ITR0(1), 0x7a);
	ixl_wr(sc, I40E_PFINT_ITR0(2), 0);

	rw_exit_write(&sc->sc_cfg_lock);

	return (ENETRESET);

free:
	for (i = 0; i < nqueues; i++) {
		iv = &sc->sc_vectors[i];
		rxr = iv->iv_rxr;
		txr = iv->iv_txr;

		if (rxr == NULL) {
			/*
			 * tx and rx get set at the same time, so if one
			 * is NULL, the other is too.
			 */
			continue;
		}

		ixl_txr_free(sc, txr);
		ixl_rxr_free(sc, rxr);
	}
	rw_exit_write(&sc->sc_cfg_lock);
	return (rv);
down:
	rw_exit_write(&sc->sc_cfg_lock);
	ixl_down(sc);
	return (ETIMEDOUT);
}

static int
ixl_iff(struct ixl_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ixl_atq iatq;
	struct ixl_aq_desc *iaq;
	struct ixl_aq_vsi_promisc_param *param;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return (0);

	memset(&iatq, 0, sizeof(iatq));

	iaq = &iatq.iatq_desc;
	iaq->iaq_opcode = htole16(IXL_AQ_OP_SET_VSI_PROMISC);

	param = (struct ixl_aq_vsi_promisc_param *)&iaq->iaq_param;
	param->flags = htole16(IXL_AQ_VSI_PROMISC_FLAG_BCAST |
	    IXL_AQ_VSI_PROMISC_FLAG_VLAN);
	if (ISSET(ifp->if_flags, IFF_PROMISC)) {
		param->flags |= htole16(IXL_AQ_VSI_PROMISC_FLAG_UCAST |
		    IXL_AQ_VSI_PROMISC_FLAG_MCAST);
	} else if (ISSET(ifp->if_flags, IFF_ALLMULTI)) {
		param->flags |= htole16(IXL_AQ_VSI_PROMISC_FLAG_MCAST);
	}
	param->valid_flags = htole16(IXL_AQ_VSI_PROMISC_FLAG_UCAST |
	    IXL_AQ_VSI_PROMISC_FLAG_MCAST | IXL_AQ_VSI_PROMISC_FLAG_BCAST |
	    IXL_AQ_VSI_PROMISC_FLAG_VLAN);
	param->seid = sc->sc_seid;

	ixl_atq_exec(sc, &iatq, "ixliff");

	if (iaq->iaq_retval != htole16(IXL_AQ_RC_OK))
		return (EIO);

	if (memcmp(sc->sc_enaddr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN) != 0) {
		ixl_remove_macvlan(sc, sc->sc_enaddr, 0,
		    IXL_AQ_OP_REMOVE_MACVLAN_IGNORE_VLAN);
		ixl_add_macvlan(sc, sc->sc_ac.ac_enaddr, 0,
		    IXL_AQ_OP_ADD_MACVLAN_IGNORE_VLAN);
		memcpy(sc->sc_enaddr, sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN);
	}
	return (0);
}

static int
ixl_down(struct ixl_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ixl_vector *iv;
	struct ixl_rx_ring *rxr;
	struct ixl_tx_ring *txr;
	unsigned int nqueues, i;
	uint32_t reg;
	int error = 0;

	nqueues = ixl_nqueues(sc);

	rw_enter_write(&sc->sc_cfg_lock);

	CLR(ifp->if_flags, IFF_RUNNING);

	NET_UNLOCK();

	/* mask interrupts */
	reg = ixl_rd(sc, I40E_QINT_RQCTL(I40E_INTR_NOTX_QUEUE));
	CLR(reg, I40E_QINT_RQCTL_CAUSE_ENA_MASK);
	ixl_wr(sc, I40E_QINT_RQCTL(I40E_INTR_NOTX_QUEUE), reg);

	reg = ixl_rd(sc, I40E_QINT_TQCTL(I40E_INTR_NOTX_QUEUE));
	CLR(reg, I40E_QINT_TQCTL_CAUSE_ENA_MASK);
	ixl_wr(sc, I40E_QINT_TQCTL(I40E_INTR_NOTX_QUEUE), reg);

	ixl_wr(sc, I40E_PFINT_LNKLST0, I40E_QUEUE_TYPE_EOL);

	/* make sure the no hw generated work is still in flight */
	intr_barrier(sc->sc_ihc);
	if (sc->sc_intrmap != NULL) {
		for (i = 0; i < nqueues; i++) {
			iv = &sc->sc_vectors[i];
			rxr = iv->iv_rxr;
			txr = iv->iv_txr;

			ixl_txr_qdis(sc, txr, 0);

			ifq_barrier(txr->txr_ifq);

			timeout_del_barrier(&rxr->rxr_refill);

			intr_barrier(iv->iv_ihc);
		}
	}

	/* XXX wait at least 400 usec for all tx queues in one go */
	delay(500);

	for (i = 0; i < nqueues; i++) {
		reg = ixl_rd(sc, I40E_QTX_ENA(i));
		CLR(reg, I40E_QTX_ENA_QENA_REQ_MASK);
		ixl_wr(sc, I40E_QTX_ENA(i), reg);

		reg = ixl_rd(sc, I40E_QRX_ENA(i));
		CLR(reg, I40E_QRX_ENA_QENA_REQ_MASK);
		ixl_wr(sc, I40E_QRX_ENA(i), reg);
	}

	for (i = 0; i < nqueues; i++) {
		iv = &sc->sc_vectors[i];
		rxr = iv->iv_rxr;
		txr = iv->iv_txr;

		if (ixl_txr_disabled(sc, txr) != 0)
			goto die;

		if (ixl_rxr_disabled(sc, rxr) != 0)
			goto die;
	}

	for (i = 0; i < nqueues; i++) {
		iv = &sc->sc_vectors[i];
		rxr = iv->iv_rxr;
		txr = iv->iv_txr;

		ixl_txr_unconfig(sc, txr);
		ixl_rxr_unconfig(sc, rxr);

		ixl_txr_clean(sc, txr);
		ixl_rxr_clean(sc, rxr);

		ixl_txr_free(sc, txr);
		ixl_rxr_free(sc, rxr);

		ifp->if_iqs[i]->ifiq_softc = NULL;
		ifp->if_ifqs[i]->ifq_softc =  NULL;
	}

out:
	rw_exit_write(&sc->sc_cfg_lock);
	NET_LOCK();
	return (error);
die:
	sc->sc_dead = 1;
	log(LOG_CRIT, "%s: failed to shut down rings", DEVNAME(sc));
	error = ETIMEDOUT;
	goto out;
}

static struct ixl_tx_ring *
ixl_txr_alloc(struct ixl_softc *sc, unsigned int qid)
{
	struct ixl_tx_ring *txr;
	struct ixl_tx_map *maps, *txm;
	unsigned int i;

	txr = malloc(sizeof(*txr), M_DEVBUF, M_WAITOK|M_CANFAIL);
	if (txr == NULL)
		return (NULL);

	maps = mallocarray(sizeof(*maps),
	    sc->sc_tx_ring_ndescs, M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (maps == NULL)
		goto free;

	if (ixl_dmamem_alloc(sc, &txr->txr_mem,
	    sizeof(struct ixl_tx_desc) * sc->sc_tx_ring_ndescs,
	    IXL_TX_QUEUE_ALIGN) != 0)
		goto freemap;

	for (i = 0; i < sc->sc_tx_ring_ndescs; i++) {
		txm = &maps[i];

		if (bus_dmamap_create(sc->sc_dmat,
		    MAXMCLBYTES, IXL_TX_PKT_DESCS, IXL_MAX_DMA_SEG_SIZE, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &txm->txm_map) != 0)
			goto uncreate;

		if (bus_dmamap_create(sc->sc_dmat,
		    MAXMCLBYTES, IXL_TX_TSO_PKT_DESCS, IXL_MAX_DMA_SEG_SIZE, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &txm->txm_map_tso) != 0)
			goto uncreate;

		txm->txm_eop = -1;
		txm->txm_m = NULL;
	}

	txr->txr_cons = txr->txr_prod = 0;
	txr->txr_maps = maps;

	txr->txr_tail = I40E_QTX_TAIL(qid);
	txr->txr_qid = qid;

	return (txr);

uncreate:
	for (i = 0; i < sc->sc_tx_ring_ndescs; i++) {
		txm = &maps[i];

		if (txm->txm_map != NULL)
			bus_dmamap_destroy(sc->sc_dmat, txm->txm_map);
		if (txm->txm_map_tso != NULL)
			bus_dmamap_destroy(sc->sc_dmat, txm->txm_map_tso);
	}

	ixl_dmamem_free(sc, &txr->txr_mem);
freemap:
	free(maps, M_DEVBUF, sizeof(*maps) * sc->sc_tx_ring_ndescs);
free:
	free(txr, M_DEVBUF, sizeof(*txr));
	return (NULL);
}

static void
ixl_txr_qdis(struct ixl_softc *sc, struct ixl_tx_ring *txr, int enable)
{
	unsigned int qid;
	bus_size_t reg;
	uint32_t r;

	qid = txr->txr_qid + sc->sc_base_queue;
	reg = I40E_GLLAN_TXPRE_QDIS(qid / 128);
	qid %= 128;

	r = ixl_rd(sc, reg);
	CLR(r, I40E_GLLAN_TXPRE_QDIS_QINDX_MASK);
	SET(r, qid << I40E_GLLAN_TXPRE_QDIS_QINDX_SHIFT);
	SET(r, enable ? I40E_GLLAN_TXPRE_QDIS_CLEAR_QDIS_MASK :
	    I40E_GLLAN_TXPRE_QDIS_SET_QDIS_MASK);
	ixl_wr(sc, reg, r);
}

static void
ixl_txr_config(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	struct ixl_hmc_txq txq;
	struct ixl_aq_vsi_data *data = IXL_DMA_KVA(&sc->sc_scratch);
	void *hmc;

	memset(&txq, 0, sizeof(txq));
	txq.head = htole16(0);
	txq.new_context = 1;
	htolem64(&txq.base,
	    IXL_DMA_DVA(&txr->txr_mem) / IXL_HMC_TXQ_BASE_UNIT);
	txq.head_wb_ena = IXL_HMC_TXQ_DESC_WB;
	htolem16(&txq.qlen, sc->sc_tx_ring_ndescs);
	txq.tphrdesc_ena = 0;
	txq.tphrpacket_ena = 0;
	txq.tphwdesc_ena = 0;
	txq.rdylist = data->qs_handle[0];

	hmc = ixl_hmc_kva(sc, IXL_HMC_LAN_TX, txr->txr_qid);
	memset(hmc, 0, ixl_hmc_len(sc, IXL_HMC_LAN_TX));
	ixl_hmc_pack(hmc, &txq, ixl_hmc_pack_txq, nitems(ixl_hmc_pack_txq));
}

static void
ixl_txr_unconfig(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	void *hmc;

	hmc = ixl_hmc_kva(sc, IXL_HMC_LAN_TX, txr->txr_qid);
	memset(hmc, 0, ixl_hmc_len(sc, IXL_HMC_LAN_TX));
}

static void
ixl_txr_clean(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	struct ixl_tx_map *maps, *txm;
	bus_dmamap_t map;
	unsigned int i;

	maps = txr->txr_maps;
	for (i = 0; i < sc->sc_tx_ring_ndescs; i++) {
		txm = &maps[i];

		if (txm->txm_m == NULL)
			continue;

		if (ISSET(txm->txm_m->m_pkthdr.csum_flags, M_TCP_TSO))
			map = txm->txm_map_tso;
		else
			map = txm->txm_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);

		m_freem(txm->txm_m);
		txm->txm_m = NULL;
	}

	ifq_clr_oactive(txr->txr_ifq);
}

static int
ixl_txr_enabled(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	bus_size_t ena = I40E_QTX_ENA(txr->txr_qid);
	uint32_t reg;
	int i;

	for (i = 0; i < 10; i++) {
		reg = ixl_rd(sc, ena);
		if (ISSET(reg, I40E_QTX_ENA_QENA_STAT_MASK))
			return (0);

		delaymsec(10);
	}

	return (ETIMEDOUT);
}

static int
ixl_txr_disabled(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	bus_size_t ena = I40E_QTX_ENA(txr->txr_qid);
	uint32_t reg;
	int i;

	for (i = 0; i < 20; i++) {
		reg = ixl_rd(sc, ena);
		if (ISSET(reg, I40E_QTX_ENA_QENA_STAT_MASK) == 0)
			return (0);

		delaymsec(10);
	}

	return (ETIMEDOUT);
}

static void
ixl_txr_free(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	struct ixl_tx_map *maps, *txm;
	unsigned int i;

	maps = txr->txr_maps;
	for (i = 0; i < sc->sc_tx_ring_ndescs; i++) {
		txm = &maps[i];

		bus_dmamap_destroy(sc->sc_dmat, txm->txm_map);
		bus_dmamap_destroy(sc->sc_dmat, txm->txm_map_tso);
	}

	ixl_dmamem_free(sc, &txr->txr_mem);
	free(maps, M_DEVBUF, sizeof(*maps) * sc->sc_tx_ring_ndescs);
	free(txr, M_DEVBUF, sizeof(*txr));
}

static inline int
ixl_load_mbuf(bus_dma_tag_t dmat, bus_dmamap_t map, struct mbuf *m)
{
	int error;

	error = bus_dmamap_load_mbuf(dmat, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT);
	if (error != EFBIG)
		return (error);

	error = m_defrag(m, M_DONTWAIT);
	if (error != 0)
		return (error);

	return (bus_dmamap_load_mbuf(dmat, map, m,
	    BUS_DMA_STREAMING | BUS_DMA_NOWAIT));
}

static uint64_t
ixl_tx_setup_offload(struct mbuf *m0, struct ixl_tx_ring *txr,
    unsigned int prod)
{
	struct ether_extracted ext;
	uint64_t hlen;
	uint64_t offload = 0;

#if NVLAN > 0
	if (ISSET(m0->m_flags, M_VLANTAG)) {
		uint64_t vtag = m0->m_pkthdr.ether_vtag;
		offload |= IXL_TX_DESC_CMD_IL2TAG1;
		offload |= vtag << IXL_TX_DESC_L2TAG1_SHIFT;
	}
#endif

	if (!ISSET(m0->m_pkthdr.csum_flags,
	    M_IPV4_CSUM_OUT|M_TCP_CSUM_OUT|M_UDP_CSUM_OUT|M_TCP_TSO))
		return (offload);

	ether_extract_headers(m0, &ext);

	if (ext.ip4) {
		offload |= ISSET(m0->m_pkthdr.csum_flags, M_IPV4_CSUM_OUT) ?
		    IXL_TX_DESC_CMD_IIPT_IPV4_CSUM :
		    IXL_TX_DESC_CMD_IIPT_IPV4;
#ifdef INET6
	} else if (ext.ip6) {
		offload |= IXL_TX_DESC_CMD_IIPT_IPV6;
#endif
	} else {
		panic("CSUM_OUT set for non-IP packet");
		/* NOTREACHED */
	}
	hlen = ext.iphlen;

	offload |= (ETHER_HDR_LEN >> 1) << IXL_TX_DESC_MACLEN_SHIFT;
	offload |= (hlen >> 2) << IXL_TX_DESC_IPLEN_SHIFT;

	if (ext.tcp && ISSET(m0->m_pkthdr.csum_flags, M_TCP_CSUM_OUT)) {
		offload |= IXL_TX_DESC_CMD_L4T_EOFT_TCP;
		offload |= (uint64_t)(ext.tcphlen >> 2)
		    << IXL_TX_DESC_L4LEN_SHIFT;
	} else if (ext.udp && ISSET(m0->m_pkthdr.csum_flags, M_UDP_CSUM_OUT)) {
		offload |= IXL_TX_DESC_CMD_L4T_EOFT_UDP;
		offload |= (uint64_t)(sizeof(*ext.udp) >> 2)
		    << IXL_TX_DESC_L4LEN_SHIFT;
	}

	if (ISSET(m0->m_pkthdr.csum_flags, M_TCP_TSO)) {
		if (ext.tcp && m0->m_pkthdr.ph_mss > 0) {
			struct ixl_tx_desc *ring, *txd;
			uint64_t cmd = 0, paylen, outlen;

			hlen += ext.tcphlen;

			/*
			 * The MSS should not be set to a lower value than 64
			 * or larger than 9668 bytes.
			 */
			outlen = MIN(9668, MAX(64, m0->m_pkthdr.ph_mss));
			paylen = m0->m_pkthdr.len - ETHER_HDR_LEN - hlen;

			ring = IXL_DMA_KVA(&txr->txr_mem);
			txd = &ring[prod];

			cmd |= IXL_TX_DESC_DTYPE_CONTEXT;
			cmd |= IXL_TX_CTX_DESC_CMD_TSO;
			cmd |= paylen << IXL_TX_CTX_DESC_TLEN_SHIFT;
			cmd |= outlen << IXL_TX_CTX_DESC_MSS_SHIFT;

			htolem64(&txd->addr, 0);
			htolem64(&txd->cmd, cmd);

			tcpstat_add(tcps_outpkttso,
			    (paylen + outlen - 1) / outlen);
		} else
			tcpstat_inc(tcps_outbadtso);
	}

	return (offload);
}

static void
ixl_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct ixl_softc *sc = ifp->if_softc;
	struct ixl_tx_ring *txr = ifq->ifq_softc;
	struct ixl_tx_desc *ring, *txd;
	struct ixl_tx_map *txm;
	bus_dmamap_t map;
	struct mbuf *m;
	uint64_t cmd;
	unsigned int prod, free, last, i;
	unsigned int mask;
	int post = 0;
	uint64_t offload;
#if NBPFILTER > 0
	caddr_t if_bpf;
#endif

	if (!LINK_STATE_IS_UP(ifp->if_link_state)) {
		ifq_purge(ifq);
		return;
	}

	prod = txr->txr_prod;
	free = txr->txr_cons;
	if (free <= prod)
		free += sc->sc_tx_ring_ndescs;
	free -= prod;

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&txr->txr_mem),
	    0, IXL_DMA_LEN(&txr->txr_mem), BUS_DMASYNC_POSTWRITE);

	ring = IXL_DMA_KVA(&txr->txr_mem);
	mask = sc->sc_tx_ring_ndescs - 1;

	for (;;) {
		/* We need one extra descriptor for TSO packets. */
		if (free <= (IXL_TX_TSO_PKT_DESCS + 1)) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		offload = ixl_tx_setup_offload(m, txr, prod);

		txm = &txr->txr_maps[prod];
		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO))
			map = txm->txm_map_tso;
		else
			map = txm->txm_map;

		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO)) {
			prod++;
			prod &= mask;
			free--;
		}

		if (ixl_load_mbuf(sc->sc_dmat, map, m) != 0) {
			ifq->ifq_errors++;
			m_freem(m);
			continue;
		}

		bus_dmamap_sync(sc->sc_dmat, map, 0,
		    map->dm_mapsize, BUS_DMASYNC_PREWRITE);

		for (i = 0; i < map->dm_nsegs; i++) {
			txd = &ring[prod];

			cmd = (uint64_t)map->dm_segs[i].ds_len <<
			    IXL_TX_DESC_BSIZE_SHIFT;
			cmd |= IXL_TX_DESC_DTYPE_DATA | IXL_TX_DESC_CMD_ICRC;
			cmd |= offload;

			htolem64(&txd->addr, map->dm_segs[i].ds_addr);
			htolem64(&txd->cmd, cmd);

			last = prod;

			prod++;
			prod &= mask;
		}
		cmd |= IXL_TX_DESC_CMD_EOP | IXL_TX_DESC_CMD_RS;
		htolem64(&txd->cmd, cmd);

		txm->txm_m = m;
		txm->txm_eop = last;

#if NBPFILTER > 0
		if_bpf = ifp->if_bpf;
		if (if_bpf)
			bpf_mtap_ether(if_bpf, m, BPF_DIRECTION_OUT);
#endif

		free -= i;
		post = 1;
	}

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&txr->txr_mem),
	    0, IXL_DMA_LEN(&txr->txr_mem), BUS_DMASYNC_PREWRITE);

	if (post) {
		txr->txr_prod = prod;
		ixl_wr(sc, txr->txr_tail, prod);
	}
}

static int
ixl_txeof(struct ixl_softc *sc, struct ixl_tx_ring *txr)
{
	struct ifqueue *ifq = txr->txr_ifq;
	struct ixl_tx_desc *ring, *txd;
	struct ixl_tx_map *txm;
	bus_dmamap_t map;
	unsigned int cons, prod, last;
	unsigned int mask;
	uint64_t dtype;
	int done = 0;

	prod = txr->txr_prod;
	cons = txr->txr_cons;

	if (cons == prod)
		return (0);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&txr->txr_mem),
	    0, IXL_DMA_LEN(&txr->txr_mem), BUS_DMASYNC_POSTREAD);

	ring = IXL_DMA_KVA(&txr->txr_mem);
	mask = sc->sc_tx_ring_ndescs - 1;

	do {
		txm = &txr->txr_maps[cons];
		last = txm->txm_eop;
		txd = &ring[last];

		dtype = txd->cmd & htole64(IXL_TX_DESC_DTYPE_MASK);
		if (dtype != htole64(IXL_TX_DESC_DTYPE_DONE))
			break;

		if (ISSET(txm->txm_m->m_pkthdr.csum_flags, M_TCP_TSO))
			map = txm->txm_map_tso;
		else
			map = txm->txm_map;

		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);
		m_freem(txm->txm_m);

		txm->txm_m = NULL;
		txm->txm_eop = -1;

		cons = last + 1;
		cons &= mask;

		done = 1;
	} while (cons != prod);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&txr->txr_mem),
	    0, IXL_DMA_LEN(&txr->txr_mem), BUS_DMASYNC_PREREAD);

	txr->txr_cons = cons;

	//ixl_enable(sc, txr->txr_msix);

	if (done && ifq_is_oactive(ifq))
		ifq_restart(ifq);

	return (done);
}

static struct ixl_rx_ring *
ixl_rxr_alloc(struct ixl_softc *sc, unsigned int qid)
{
	struct ixl_rx_ring *rxr;
	struct ixl_rx_map *maps, *rxm;
	unsigned int i;

	rxr = malloc(sizeof(*rxr), M_DEVBUF, M_WAITOK|M_CANFAIL);
	if (rxr == NULL)
		return (NULL);

	maps = mallocarray(sizeof(*maps),
	    sc->sc_rx_ring_ndescs, M_DEVBUF, M_WAITOK|M_CANFAIL|M_ZERO);
	if (maps == NULL)
		goto free;

	if (ixl_dmamem_alloc(sc, &rxr->rxr_mem,
	    sizeof(struct ixl_rx_rd_desc_16) * sc->sc_rx_ring_ndescs,
	    IXL_RX_QUEUE_ALIGN) != 0)
		goto freemap;

	for (i = 0; i < sc->sc_rx_ring_ndescs; i++) {
		rxm = &maps[i];

		if (bus_dmamap_create(sc->sc_dmat,
		    IXL_HARDMTU, 1, IXL_HARDMTU, 0,
		    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &rxm->rxm_map) != 0)
			goto uncreate;

		rxm->rxm_m = NULL;
	}

	rxr->rxr_sc = sc;
	if_rxr_init(&rxr->rxr_acct, 17, sc->sc_rx_ring_ndescs - 1);
	timeout_set(&rxr->rxr_refill, ixl_rxrefill, rxr);
	rxr->rxr_cons = rxr->rxr_prod = 0;
	rxr->rxr_m_head = NULL;
	rxr->rxr_m_tail = &rxr->rxr_m_head;
	rxr->rxr_maps = maps;

	rxr->rxr_tail = I40E_QRX_TAIL(qid);
	rxr->rxr_qid = qid;

	return (rxr);

uncreate:
	for (i = 0; i < sc->sc_rx_ring_ndescs; i++) {
		rxm = &maps[i];

		if (rxm->rxm_map == NULL)
			continue;

		bus_dmamap_destroy(sc->sc_dmat, rxm->rxm_map);
	}

	ixl_dmamem_free(sc, &rxr->rxr_mem);
freemap:
	free(maps, M_DEVBUF, sizeof(*maps) * sc->sc_rx_ring_ndescs);
free:
	free(rxr, M_DEVBUF, sizeof(*rxr));
	return (NULL);
}

static void
ixl_rxr_clean(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	struct ixl_rx_map *maps, *rxm;
	bus_dmamap_t map;
	unsigned int i;

	timeout_del_barrier(&rxr->rxr_refill);

	maps = rxr->rxr_maps;
	for (i = 0; i < sc->sc_rx_ring_ndescs; i++) {
		rxm = &maps[i];

		if (rxm->rxm_m == NULL)
			continue;

		map = rxm->rxm_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);

		m_freem(rxm->rxm_m);
		rxm->rxm_m = NULL;
	}

	m_freem(rxr->rxr_m_head);
	rxr->rxr_m_head = NULL;
	rxr->rxr_m_tail = &rxr->rxr_m_head;

	rxr->rxr_prod = rxr->rxr_cons = 0;
}

static int
ixl_rxr_enabled(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	bus_size_t ena = I40E_QRX_ENA(rxr->rxr_qid);
	uint32_t reg;
	int i;

	for (i = 0; i < 10; i++) {
		reg = ixl_rd(sc, ena);
		if (ISSET(reg, I40E_QRX_ENA_QENA_STAT_MASK))
			return (0);

		delaymsec(10);
	}

	return (ETIMEDOUT);
}

static int
ixl_rxr_disabled(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	bus_size_t ena = I40E_QRX_ENA(rxr->rxr_qid);
	uint32_t reg;
	int i;

	for (i = 0; i < 20; i++) {
		reg = ixl_rd(sc, ena);
		if (ISSET(reg, I40E_QRX_ENA_QENA_STAT_MASK) == 0)
			return (0);

		delaymsec(10);
	}

	return (ETIMEDOUT);
}

static void
ixl_rxr_config(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	struct ixl_hmc_rxq rxq;
	void *hmc;

	memset(&rxq, 0, sizeof(rxq));

	rxq.head = htole16(0);
	htolem64(&rxq.base,
	    IXL_DMA_DVA(&rxr->rxr_mem) / IXL_HMC_RXQ_BASE_UNIT);
	htolem16(&rxq.qlen, sc->sc_rx_ring_ndescs);
	rxq.dbuff = htole16(MCLBYTES / IXL_HMC_RXQ_DBUFF_UNIT);
	rxq.hbuff = 0;
	rxq.dtype = IXL_HMC_RXQ_DTYPE_NOSPLIT;
	rxq.dsize = IXL_HMC_RXQ_DSIZE_16;
	rxq.crcstrip = 1;
	rxq.l2tsel = IXL_HMC_RXQ_L2TSEL_1ST_TAG_TO_L2TAG1;
	rxq.showiv = 0;
	rxq.rxmax = htole16(IXL_HARDMTU);
	rxq.tphrdesc_ena = 0;
	rxq.tphwdesc_ena = 0;
	rxq.tphdata_ena = 0;
	rxq.tphhead_ena = 0;
	rxq.lrxqthresh = 0;
	rxq.prefena = 1;

	hmc = ixl_hmc_kva(sc, IXL_HMC_LAN_RX, rxr->rxr_qid);
	memset(hmc, 0, ixl_hmc_len(sc, IXL_HMC_LAN_RX));
	ixl_hmc_pack(hmc, &rxq, ixl_hmc_pack_rxq, nitems(ixl_hmc_pack_rxq));
}

static void
ixl_rxr_unconfig(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	void *hmc;

	hmc = ixl_hmc_kva(sc, IXL_HMC_LAN_RX, rxr->rxr_qid);
	memset(hmc, 0, ixl_hmc_len(sc, IXL_HMC_LAN_RX));
}

static void
ixl_rxr_free(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	struct ixl_rx_map *maps, *rxm;
	unsigned int i;

	maps = rxr->rxr_maps;
	for (i = 0; i < sc->sc_rx_ring_ndescs; i++) {
		rxm = &maps[i];

		bus_dmamap_destroy(sc->sc_dmat, rxm->rxm_map);
	}

	ixl_dmamem_free(sc, &rxr->rxr_mem);
	free(maps, M_DEVBUF, sizeof(*maps) * sc->sc_rx_ring_ndescs);
	free(rxr, M_DEVBUF, sizeof(*rxr));
}

static int
ixl_rxeof(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	struct ifiqueue *ifiq = rxr->rxr_ifiq;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ixl_rx_wb_desc_16 *ring, *rxd;
	struct ixl_rx_map *rxm;
	bus_dmamap_t map;
	unsigned int cons, prod;
	struct mbuf_list mltcp = MBUF_LIST_INITIALIZER();
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint64_t word;
	unsigned int ptype;
	unsigned int len;
	unsigned int mask;
	int done = 0;

	prod = rxr->rxr_prod;
	cons = rxr->rxr_cons;

	if (cons == prod)
		return (0);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&rxr->rxr_mem),
	    0, IXL_DMA_LEN(&rxr->rxr_mem),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	ring = IXL_DMA_KVA(&rxr->rxr_mem);
	mask = sc->sc_rx_ring_ndescs - 1;

	do {
		rxd = &ring[cons];

		word = lemtoh64(&rxd->qword1);
		if (!ISSET(word, IXL_RX_DESC_DD))
			break;

		if_rxr_put(&rxr->rxr_acct, 1);

		rxm = &rxr->rxr_maps[cons];

		map = rxm->rxm_map;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, map);
		
		m = rxm->rxm_m;
		rxm->rxm_m = NULL;

		ptype = (word & IXL_RX_DESC_PTYPE_MASK)
		    >> IXL_RX_DESC_PTYPE_SHIFT;
		len = (word & IXL_RX_DESC_PLEN_MASK) >> IXL_RX_DESC_PLEN_SHIFT;
		m->m_len = len;
		m->m_pkthdr.len = 0;

		m->m_next = NULL;
		*rxr->rxr_m_tail = m;
		rxr->rxr_m_tail = &m->m_next;

		m = rxr->rxr_m_head;
		m->m_pkthdr.len += len;

		if (ISSET(word, IXL_RX_DESC_EOP)) {
			if (!ISSET(word,
			    IXL_RX_DESC_RXE | IXL_RX_DESC_OVERSIZE)) {
				if ((word & IXL_RX_DESC_FLTSTAT_MASK) ==
				    IXL_RX_DESC_FLTSTAT_RSS) {
					m->m_pkthdr.ph_flowid =
					    lemtoh32(&rxd->filter_status);
					m->m_pkthdr.csum_flags |= M_FLOWID;
				}

#if NVLAN > 0
				if (ISSET(word, IXL_RX_DESC_L2TAG1P)) {
					m->m_pkthdr.ether_vtag =
					    lemtoh16(&rxd->l2tag1);
					SET(m->m_flags, M_VLANTAG);
				}
#endif

				ixl_rx_checksum(m, word);

#ifndef SMALL_KERNEL
				if (ISSET(ifp->if_xflags, IFXF_LRO) &&
				    (ptype == IXL_RX_DESC_PTYPE_MAC_IPV4_TCP ||
				     ptype == IXL_RX_DESC_PTYPE_MAC_IPV6_TCP))
					tcp_softlro_glue(&mltcp, m, ifp);
				else
#endif
					ml_enqueue(&ml, m);
			} else {
				ifp->if_ierrors++; /* XXX */
				m_freem(m);
			}

			rxr->rxr_m_head = NULL;
			rxr->rxr_m_tail = &rxr->rxr_m_head;
		}

		cons++;
		cons &= mask;

		done = 1;
	} while (cons != prod);

	if (done) {
		int livelocked = 0;

		rxr->rxr_cons = cons;
		if (ifiq_input(ifiq, &mltcp))
			livelocked = 1;
		if (ifiq_input(ifiq, &ml))
			livelocked = 1;
		if (livelocked)
			if_rxr_livelocked(&rxr->rxr_acct);
		ixl_rxfill(sc, rxr);
	}

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&rxr->rxr_mem),
	    0, IXL_DMA_LEN(&rxr->rxr_mem),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	return (done);
}

static void
ixl_rxfill(struct ixl_softc *sc, struct ixl_rx_ring *rxr)
{
	struct ixl_rx_rd_desc_16 *ring, *rxd;
	struct ixl_rx_map *rxm;
	bus_dmamap_t map;
	struct mbuf *m;
	unsigned int prod;
	unsigned int slots;
	unsigned int mask;
	int post = 0;

	slots = if_rxr_get(&rxr->rxr_acct, sc->sc_rx_ring_ndescs);
	if (slots == 0)
		return;

	prod = rxr->rxr_prod;

	ring = IXL_DMA_KVA(&rxr->rxr_mem);
	mask = sc->sc_rx_ring_ndescs - 1;

	do {
		rxm = &rxr->rxr_maps[prod];

		m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES + ETHER_ALIGN);
		if (m == NULL)
			break;
		m->m_data += (m->m_ext.ext_size - (MCLBYTES + ETHER_ALIGN));
		m->m_len = m->m_pkthdr.len = MCLBYTES + ETHER_ALIGN;

		map = rxm->rxm_map;

		if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m,
		    BUS_DMA_NOWAIT) != 0) {
			m_freem(m);
			break;
		}

		rxm->rxm_m = m;

		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREREAD);

		rxd = &ring[prod];

		htolem64(&rxd->paddr, map->dm_segs[0].ds_addr);
		rxd->haddr = htole64(0);

		prod++;
		prod &= mask;

		post = 1;
	} while (--slots);

	if_rxr_put(&rxr->rxr_acct, slots);

	if (if_rxr_inuse(&rxr->rxr_acct) == 0)
		timeout_add(&rxr->rxr_refill, 1);
	else if (post) {
		rxr->rxr_prod = prod;
		ixl_wr(sc, rxr->rxr_tail, prod);
	}
}

void
ixl_rxrefill(void *arg)
{
	struct ixl_rx_ring *rxr = arg;
	struct ixl_softc *sc = rxr->rxr_sc;

	ixl_rxfill(sc, rxr);
}

static int
ixl_rxrinfo(struct ixl_softc *sc, struct if_rxrinfo *ifri)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct if_rxring_info *ifr;
	struct ixl_rx_ring *ring;
	int i, rv;

	if (!ISSET(ifp->if_flags, IFF_RUNNING))
		return (ENOTTY);

	ifr = mallocarray(sizeof(*ifr), ixl_nqueues(sc), M_TEMP,
	    M_WAITOK|M_CANFAIL|M_ZERO);
	if (ifr == NULL)
		return (ENOMEM);

	for (i = 0; i < ixl_nqueues(sc); i++) {
		ring = ifp->if_iqs[i]->ifiq_softc;
		ifr[i].ifr_size = MCLBYTES;
		snprintf(ifr[i].ifr_name, sizeof(ifr[i].ifr_name), "%d", i);
		ifr[i].ifr_info = ring->rxr_acct;
	}

	rv = if_rxr_info_ioctl(ifri, ixl_nqueues(sc), ifr);
	free(ifr, M_TEMP, ixl_nqueues(sc) * sizeof(*ifr));

	return (rv);
}

static void
ixl_rx_checksum(struct mbuf *m, uint64_t word)
{
	if (!ISSET(word, IXL_RX_DESC_L3L4P))
		return;

	if (ISSET(word, IXL_RX_DESC_IPE))
		return;

	m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;

	if (ISSET(word, IXL_RX_DESC_L4E))
		return;

	m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK | M_UDP_CSUM_IN_OK;
}

static int
ixl_intr0(void *xsc)
{
	struct ixl_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t icr;
	int rv = 0;

	ixl_intr_enable(sc);
	icr = ixl_rd(sc, I40E_PFINT_ICR0);

	if (ISSET(icr, I40E_PFINT_ICR0_ADMINQ_MASK)) {
		ixl_atq_done(sc);
		task_add(systq, &sc->sc_arq_task);
		rv = 1;
	}

	if (ISSET(icr, I40E_PFINT_ICR0_LINK_STAT_CHANGE_MASK)) {
		task_add(systq, &sc->sc_link_state_task);
		rv = 1;
	}

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		struct ixl_vector *iv = sc->sc_vectors;
		if (ISSET(icr, I40E_INTR_NOTX_RX_MASK))
			rv |= ixl_rxeof(sc, iv->iv_rxr);
		if (ISSET(icr, I40E_INTR_NOTX_TX_MASK))
			rv |= ixl_txeof(sc, iv->iv_txr);
	}

	return (rv);
}

static int
ixl_intr_vector(void *v)
{
	struct ixl_vector *iv = v;
	struct ixl_softc *sc = iv->iv_sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int rv = 0;

	if (ISSET(ifp->if_flags, IFF_RUNNING)) {
		rv |= ixl_rxeof(sc, iv->iv_rxr);
		rv |= ixl_txeof(sc, iv->iv_txr);
	}

	ixl_wr(sc, I40E_PFINT_DYN_CTLN(iv->iv_qid),
	    I40E_PFINT_DYN_CTLN_INTENA_MASK |
	    I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
	    (IXL_NOITR << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT));

	return (rv);
}

static void
ixl_link_state_update_iaq(struct ixl_softc *sc, void *arg)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ixl_aq_desc *iaq = arg;
	uint16_t retval;
	int link_state;
	int change = 0;

	retval = lemtoh16(&iaq->iaq_retval);
	if (retval != IXL_AQ_RC_OK) {
		printf("%s: LINK STATUS error %u\n", DEVNAME(sc), retval);
		return;
	}

	link_state = ixl_set_link_status(sc, iaq);
	mtx_enter(&sc->sc_link_state_mtx);
	if (ifp->if_link_state != link_state) {
		ifp->if_link_state = link_state;
		change = 1;
	}
	mtx_leave(&sc->sc_link_state_mtx);

	if (change)
		if_link_state_change(ifp);
}

static void
ixl_link_state_update(void *xsc)
{
	struct ixl_softc *sc = xsc;
	struct ixl_aq_desc *iaq;
	struct ixl_aq_link_param *param;

	memset(&sc->sc_link_state_atq, 0, sizeof(sc->sc_link_state_atq));
	iaq = &sc->sc_link_state_atq.iatq_desc;
	iaq->iaq_opcode = htole16(IXL_AQ_OP_PHY_LINK_STATUS);
	param = (struct ixl_aq_link_param *)iaq->iaq_param;
	param->notify = IXL_AQ_LINK_NOTIFY;

	ixl_atq_set(&sc->sc_link_state_atq, ixl_link_state_update_iaq, iaq);
	ixl_atq_post(sc, &sc->sc_link_state_atq);
}

#if 0
static void
ixl_aq_dump(const struct ixl_softc *sc, const struct ixl_aq_desc *iaq)
{
	printf("%s: flags %b opcode %04x\n", DEVNAME(sc),
	    lemtoh16(&iaq->iaq_flags), IXL_AQ_FLAGS_FMT,
	    lemtoh16(&iaq->iaq_opcode));
	printf("%s: datalen %u retval %u\n", DEVNAME(sc),
	    lemtoh16(&iaq->iaq_datalen), lemtoh16(&iaq->iaq_retval));
	printf("%s: cookie %016llx\n", DEVNAME(sc), iaq->iaq_cookie);
	printf("%s: %08x %08x %08x %08x\n", DEVNAME(sc),
	    lemtoh32(&iaq->iaq_param[0]), lemtoh32(&iaq->iaq_param[1]),
	    lemtoh32(&iaq->iaq_param[2]), lemtoh32(&iaq->iaq_param[3]));
}
#endif

static void
ixl_arq(void *xsc)
{
	struct ixl_softc *sc = xsc;
	struct ixl_aq_desc *arq, *iaq;
	struct ixl_aq_buf *aqb;
	unsigned int cons = sc->sc_arq_cons;
	unsigned int prod;
	int done = 0;

	prod = ixl_rd(sc, sc->sc_aq_regs->arq_head) &
	    sc->sc_aq_regs->arq_head_mask;

	if (cons == prod)
		goto done;

	arq = IXL_DMA_KVA(&sc->sc_arq);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_arq),
	    0, IXL_DMA_LEN(&sc->sc_arq),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	do {
		iaq = &arq[cons];

		aqb = SIMPLEQ_FIRST(&sc->sc_arq_live);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_arq_live, aqb_entry);
		bus_dmamap_sync(sc->sc_dmat, aqb->aqb_map, 0, IXL_AQ_BUFLEN,
		    BUS_DMASYNC_POSTREAD);

		switch (iaq->iaq_opcode) {
		case HTOLE16(IXL_AQ_OP_PHY_LINK_STATUS):
			ixl_link_state_update_iaq(sc, iaq);
			break;
		}

		memset(iaq, 0, sizeof(*iaq));
		SIMPLEQ_INSERT_TAIL(&sc->sc_arq_idle, aqb, aqb_entry);
		if_rxr_put(&sc->sc_arq_ring, 1);

		cons++;
		cons &= IXL_AQ_MASK;

		done = 1;
	} while (cons != prod);

	if (done && ixl_arq_fill(sc))
		ixl_wr(sc, sc->sc_aq_regs->arq_tail, sc->sc_arq_prod);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_arq),
	    0, IXL_DMA_LEN(&sc->sc_arq),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	sc->sc_arq_cons = cons;

done:
	ixl_intr_enable(sc);
}

static void
ixl_atq_set(struct ixl_atq *iatq,
    void (*fn)(struct ixl_softc *, void *), void *arg)
{
	iatq->iatq_fn = fn;
	iatq->iatq_arg = arg;
}

static void
ixl_atq_post(struct ixl_softc *sc, struct ixl_atq *iatq)
{
	struct ixl_aq_desc *atq, *slot;
	unsigned int prod;

	mtx_enter(&sc->sc_atq_mtx);

	atq = IXL_DMA_KVA(&sc->sc_atq);
	prod = sc->sc_atq_prod;
	slot = atq + prod;

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq), BUS_DMASYNC_POSTWRITE);

	*slot = iatq->iatq_desc;
	slot->iaq_cookie = (uint64_t)iatq;

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq), BUS_DMASYNC_PREWRITE);

	prod++;
	prod &= IXL_AQ_MASK;
	sc->sc_atq_prod = prod;
	ixl_wr(sc, sc->sc_aq_regs->atq_tail, prod);

	mtx_leave(&sc->sc_atq_mtx);
}

static void
ixl_atq_done(struct ixl_softc *sc)
{
	struct ixl_aq_desc *atq, *slot;
	struct ixl_atq *iatq;
	unsigned int cons;
	unsigned int prod;

	mtx_enter(&sc->sc_atq_mtx);

	prod = sc->sc_atq_prod;
	cons = sc->sc_atq_cons;

	if (prod == cons) {
		mtx_leave(&sc->sc_atq_mtx);
		return;
	}

	atq = IXL_DMA_KVA(&sc->sc_atq);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	do {
		slot = &atq[cons];
		if (!ISSET(slot->iaq_flags, htole16(IXL_AQ_DD)))
			break;

		KASSERT(slot->iaq_cookie != 0);
		iatq = (struct ixl_atq *)slot->iaq_cookie;
		iatq->iatq_desc = *slot;

		memset(slot, 0, sizeof(*slot));

		(*iatq->iatq_fn)(sc, iatq->iatq_arg);

		cons++;
		cons &= IXL_AQ_MASK;
	} while (cons != prod);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	sc->sc_atq_cons = cons;

	mtx_leave(&sc->sc_atq_mtx);
}

static void
ixl_wakeup(struct ixl_softc *sc, void *arg)
{
	struct cond *c = arg;

	cond_signal(c);
}

static void
ixl_atq_exec(struct ixl_softc *sc, struct ixl_atq *iatq, const char *wmesg)
{
	struct cond c = COND_INITIALIZER();

	KASSERT(iatq->iatq_desc.iaq_cookie == 0);

	ixl_atq_set(iatq, ixl_wakeup, &c);
	ixl_atq_post(sc, iatq);

	cond_wait(&c, wmesg);
}

static int
ixl_atq_poll(struct ixl_softc *sc, struct ixl_aq_desc *iaq, unsigned int tm)
{
	struct ixl_aq_desc *atq, *slot;
	unsigned int prod;
	unsigned int t = 0;

	mtx_enter(&sc->sc_atq_mtx);

	atq = IXL_DMA_KVA(&sc->sc_atq);
	prod = sc->sc_atq_prod;
	slot = atq + prod;

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq), BUS_DMASYNC_POSTWRITE);

	*slot = *iaq;
	slot->iaq_flags |= htole16(IXL_AQ_SI);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq), BUS_DMASYNC_PREWRITE);

	prod++;
	prod &= IXL_AQ_MASK;
	sc->sc_atq_prod = prod;
	ixl_wr(sc, sc->sc_aq_regs->atq_tail, prod);

	while (ixl_rd(sc, sc->sc_aq_regs->atq_head) != prod) {
		delaymsec(1);

		if (t++ > tm) {
			mtx_leave(&sc->sc_atq_mtx);
			return (ETIMEDOUT);
		}
	}

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq), BUS_DMASYNC_POSTREAD);
	*iaq = *slot;
	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_atq),
	    0, IXL_DMA_LEN(&sc->sc_atq), BUS_DMASYNC_PREREAD);

	sc->sc_atq_cons = prod;

	mtx_leave(&sc->sc_atq_mtx);
	return (0);
}

static int
ixl_get_version(struct ixl_softc *sc)
{
	struct ixl_aq_desc iaq;
	uint32_t fwbuild, fwver, apiver;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_GET_VERSION);

	if (ixl_atq_poll(sc, &iaq, 2000) != 0)
		return (ETIMEDOUT);
	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK))
		return (EIO);

	fwbuild = lemtoh32(&iaq.iaq_param[1]);
	fwver = lemtoh32(&iaq.iaq_param[2]);
	apiver = lemtoh32(&iaq.iaq_param[3]);

	sc->sc_api_major = apiver & 0xffff;
	sc->sc_api_minor = (apiver >> 16) & 0xffff;

	printf(", FW %hu.%hu.%05u API %hu.%hu", (uint16_t)fwver,
	    (uint16_t)(fwver >> 16), fwbuild,
	    sc->sc_api_major, sc->sc_api_minor);

	return (0);
}

static int
ixl_pxe_clear(struct ixl_softc *sc)
{
	struct ixl_aq_desc iaq;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_CLEAR_PXE_MODE);
	iaq.iaq_param[0] = htole32(0x2);

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf(", CLEAR PXE MODE timeout\n");
		return (-1);
	}

	switch (iaq.iaq_retval) {
	case HTOLE16(IXL_AQ_RC_OK):
	case HTOLE16(IXL_AQ_RC_EEXIST):
		break;
	default:
		printf(", CLEAR PXE MODE error\n");
		return (-1);
	}

	return (0);
}

static int
ixl_lldp_shut(struct ixl_softc *sc)
{
	struct ixl_aq_desc iaq;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_LLDP_STOP_AGENT);
	iaq.iaq_param[0] = htole32(IXL_LLDP_SHUTDOWN);

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf(", STOP LLDP AGENT timeout\n");
		return (-1);
	}

	switch (iaq.iaq_retval) {
	case HTOLE16(IXL_AQ_RC_EMODE):
	case HTOLE16(IXL_AQ_RC_EPERM):
		/* ignore silently */
	default:
		break;
	}

	return (0);
}

static int
ixl_get_mac(struct ixl_softc *sc)
{
	struct ixl_dmamem idm;
	struct ixl_aq_desc iaq;
	struct ixl_aq_mac_addresses *addrs;
	int rv;

#ifdef __sparc64__
	if (OF_getprop(PCITAG_NODE(sc->sc_tag), "local-mac-address",
	    sc->sc_ac.ac_enaddr, ETHER_ADDR_LEN) == ETHER_ADDR_LEN)
		return (0);
#endif

	if (ixl_dmamem_alloc(sc, &idm, sizeof(*addrs), 0) != 0) {
		printf(", unable to allocate mac addresses\n");
		return (-1);
	}

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_flags = htole16(IXL_AQ_BUF);
	iaq.iaq_opcode = htole16(IXL_AQ_OP_MAC_ADDRESS_READ);
	iaq.iaq_datalen = htole16(sizeof(*addrs));
	ixl_aq_dva(&iaq, IXL_DMA_DVA(&idm));

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&idm), 0, IXL_DMA_LEN(&idm),
	    BUS_DMASYNC_PREREAD);

	rv = ixl_atq_poll(sc, &iaq, 250);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&idm), 0, IXL_DMA_LEN(&idm),
	    BUS_DMASYNC_POSTREAD);

	if (rv != 0) {
		printf(", MAC ADDRESS READ timeout\n");
		rv = -1;
		goto done;
	}
	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf(", MAC ADDRESS READ error\n");
		rv = -1;
		goto done;
	}

	addrs = IXL_DMA_KVA(&idm);
	if (!ISSET(iaq.iaq_param[0], htole32(IXL_AQ_MAC_PORT_VALID))) {
		printf(", port address is not valid\n");
		goto done;
	}

	memcpy(sc->sc_ac.ac_enaddr, addrs->port, ETHER_ADDR_LEN);
	rv = 0;

done:
	ixl_dmamem_free(sc, &idm);
	return (rv);
}

static int
ixl_get_switch_config(struct ixl_softc *sc)
{
	struct ixl_dmamem idm;
	struct ixl_aq_desc iaq;
	struct ixl_aq_switch_config *hdr;
	struct ixl_aq_switch_config_element *elms, *elm;
	unsigned int nelm;
	int rv;

	if (ixl_dmamem_alloc(sc, &idm, IXL_AQ_BUFLEN, 0) != 0) {
		printf("%s: unable to allocate switch config buffer\n",
		    DEVNAME(sc));
		return (-1);
	}

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_flags = htole16(IXL_AQ_BUF |
	    (IXL_AQ_BUFLEN > I40E_AQ_LARGE_BUF ? IXL_AQ_LB : 0));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_SWITCH_GET_CONFIG);
	iaq.iaq_datalen = htole16(IXL_AQ_BUFLEN);
	ixl_aq_dva(&iaq, IXL_DMA_DVA(&idm));

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&idm), 0, IXL_DMA_LEN(&idm),
	    BUS_DMASYNC_PREREAD);

	rv = ixl_atq_poll(sc, &iaq, 250);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&idm), 0, IXL_DMA_LEN(&idm),
	    BUS_DMASYNC_POSTREAD);

	if (rv != 0) {
		printf("%s: GET SWITCH CONFIG timeout\n", DEVNAME(sc));
		rv = -1;
		goto done;
	}
	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf("%s: GET SWITCH CONFIG error\n", DEVNAME(sc));
		rv = -1;
		goto done;
	}

	hdr = IXL_DMA_KVA(&idm);
	elms = (struct ixl_aq_switch_config_element *)(hdr + 1);

	nelm = lemtoh16(&hdr->num_reported);
	if (nelm < 1) {
		printf("%s: no switch config available\n", DEVNAME(sc));
		rv = -1;
		goto done;
	}

#if 0
	for (i = 0; i < nelm; i++) {
		elm = &elms[i];

		printf("%s: type %x revision %u seid %04x\n", DEVNAME(sc),
		    elm->type, elm->revision, lemtoh16(&elm->seid));
		printf("%s: uplink %04x downlink %04x\n", DEVNAME(sc),
		    lemtoh16(&elm->uplink_seid),
		    lemtoh16(&elm->downlink_seid));
		printf("%s: conntype %x scheduler %04x extra %04x\n",
		    DEVNAME(sc), elm->connection_type,
		    lemtoh16(&elm->scheduler_id),
		    lemtoh16(&elm->element_info));
	}
#endif

	elm = &elms[0];

	sc->sc_uplink_seid = elm->uplink_seid;
	sc->sc_downlink_seid = elm->downlink_seid;
	sc->sc_seid = elm->seid;

	if ((sc->sc_uplink_seid == htole16(0)) !=
	    (sc->sc_downlink_seid == htole16(0))) {
		printf("%s: SEIDs are misconfigured\n", DEVNAME(sc));
		rv = -1;
		goto done;
	}

done:
	ixl_dmamem_free(sc, &idm);
	return (rv);
}

static int
ixl_phy_mask_ints(struct ixl_softc *sc)
{
	struct ixl_aq_desc iaq;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_PHY_SET_EVENT_MASK);
	iaq.iaq_param[2] = htole32(IXL_AQ_PHY_EV_MASK &
	    ~(IXL_AQ_PHY_EV_LINK_UPDOWN | IXL_AQ_PHY_EV_MODULE_QUAL_FAIL |
	      IXL_AQ_PHY_EV_MEDIA_NA));

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf("%s: SET PHY EVENT MASK timeout\n", DEVNAME(sc));
		return (-1);
	}
	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf("%s: SET PHY EVENT MASK error\n", DEVNAME(sc));
		return (-1);
	}

	return (0);
}

static int
ixl_get_phy_abilities(struct ixl_softc *sc,struct ixl_dmamem *idm)
{
	struct ixl_aq_desc iaq;
	int rv;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_flags = htole16(IXL_AQ_BUF |
	    (IXL_DMA_LEN(idm) > I40E_AQ_LARGE_BUF ? IXL_AQ_LB : 0));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_PHY_GET_ABILITIES);
	htolem16(&iaq.iaq_datalen, IXL_DMA_LEN(idm));
	iaq.iaq_param[0] = htole32(IXL_AQ_PHY_REPORT_INIT);
	ixl_aq_dva(&iaq, IXL_DMA_DVA(idm));

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(idm), 0, IXL_DMA_LEN(idm),
	    BUS_DMASYNC_PREREAD);

	rv = ixl_atq_poll(sc, &iaq, 250);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(idm), 0, IXL_DMA_LEN(idm),
	    BUS_DMASYNC_POSTREAD);

	if (rv != 0)
		return (-1);

	return (lemtoh16(&iaq.iaq_retval));
}

static int
ixl_get_phy_types(struct ixl_softc *sc, uint64_t *phy_types_ptr)
{
	struct ixl_dmamem idm;
	struct ixl_aq_phy_abilities *phy;
	uint64_t phy_types;
	int rv;

	if (ixl_dmamem_alloc(sc, &idm, IXL_AQ_BUFLEN, 0) != 0) {
		printf("%s: unable to allocate phy abilities buffer\n",
		    DEVNAME(sc));
		return (-1);
	}

	rv = ixl_get_phy_abilities(sc, &idm);
	switch (rv) {
	case -1:
		printf("%s: GET PHY ABILITIES timeout\n", DEVNAME(sc));
		goto err;
	case IXL_AQ_RC_OK:
		break;
	case IXL_AQ_RC_EIO:
		/* API is too old to handle this command */
		phy_types = 0;
		goto done;
	default:
		printf("%s: GET PHY ABILITIES error %u\n", DEVNAME(sc), rv);
		goto err;
	}

	phy = IXL_DMA_KVA(&idm);

	phy_types = lemtoh32(&phy->phy_type);
	phy_types |= (uint64_t)phy->phy_type_ext << 32;

done:
	*phy_types_ptr = phy_types;

	rv = 0;

err:
	ixl_dmamem_free(sc, &idm);
	return (rv);
}

/*
 * this returns -2 on software/driver failure, -1 for problems
 * talking to the hardware, or the sff module type.
 */

static int
ixl_get_module_type(struct ixl_softc *sc)
{
	struct ixl_dmamem idm;
	struct ixl_aq_phy_abilities *phy;
	int rv;

	if (ixl_dmamem_alloc(sc, &idm, IXL_AQ_BUFLEN, 0) != 0)
		return (-2);

	rv = ixl_get_phy_abilities(sc, &idm);
	if (rv != IXL_AQ_RC_OK) {
		rv = -1;
		goto done;
	}

	phy = IXL_DMA_KVA(&idm);

	rv = phy->module_type[0];

done:
	ixl_dmamem_free(sc, &idm);
	return (rv);
}

static int
ixl_get_link_status(struct ixl_softc *sc)
{
	struct ixl_aq_desc iaq;
	struct ixl_aq_link_param *param;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_PHY_LINK_STATUS);
	param = (struct ixl_aq_link_param *)iaq.iaq_param;
	param->notify = IXL_AQ_LINK_NOTIFY;

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf("%s: GET LINK STATUS timeout\n", DEVNAME(sc));
		return (-1);
	}
	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf("%s: GET LINK STATUS error\n", DEVNAME(sc));
		return (0);
	}

	sc->sc_ac.ac_if.if_link_state = ixl_set_link_status(sc, &iaq);

	return (0);
}

struct ixl_sff_ops {
	int (*open)(struct ixl_softc *sc, struct if_sffpage *, uint8_t *);
	int (*get)(struct ixl_softc *sc, struct if_sffpage *, size_t);
	int (*close)(struct ixl_softc *sc, struct if_sffpage *, uint8_t);
};

static int
ixl_sfp_open(struct ixl_softc *sc, struct if_sffpage *sff, uint8_t *page)
{
	int error;

	if (sff->sff_addr != IFSFF_ADDR_EEPROM)
		return (0);

	error = ixl_sff_get_byte(sc, IFSFF_ADDR_EEPROM, 127, page);
	if (error != 0)
		return (error);
	if (*page == sff->sff_page)
		return (0);
	error = ixl_sff_set_byte(sc, IFSFF_ADDR_EEPROM, 127, sff->sff_page);
	if (error != 0)
		return (error);

	return (0);
}

static int
ixl_sfp_get(struct ixl_softc *sc, struct if_sffpage *sff, size_t i)
{
	return (ixl_sff_get_byte(sc, sff->sff_addr, i, &sff->sff_data[i]));
}

static int
ixl_sfp_close(struct ixl_softc *sc, struct if_sffpage *sff, uint8_t page)
{
	int error;

	if (sff->sff_addr != IFSFF_ADDR_EEPROM)
		return (0);

	if (page == sff->sff_page)
		return (0);

	error = ixl_sff_set_byte(sc, IFSFF_ADDR_EEPROM, 127, page);
	if (error != 0)
		return (error);

	return (0);
}

static const struct ixl_sff_ops ixl_sfp_ops = {
	ixl_sfp_open,
	ixl_sfp_get,
	ixl_sfp_close,
};

static int
ixl_qsfp_open(struct ixl_softc *sc, struct if_sffpage *sff, uint8_t *page)
{
	if (sff->sff_addr != IFSFF_ADDR_EEPROM)
		return (EIO);

	return (0);
}

static int
ixl_qsfp_get(struct ixl_softc *sc, struct if_sffpage *sff, size_t i)
{
	return (ixl_sff_get_byte(sc, sff->sff_page, i, &sff->sff_data[i]));
}

static int
ixl_qsfp_close(struct ixl_softc *sc, struct if_sffpage *sff, uint8_t page)
{
	return (0);
}

static const struct ixl_sff_ops ixl_qsfp_ops = {
	ixl_qsfp_open,
	ixl_qsfp_get,
	ixl_qsfp_close,
};

static int
ixl_get_sffpage(struct ixl_softc *sc, struct if_sffpage *sff)
{
	const struct ixl_sff_ops *ops;
	uint8_t page;
	size_t i;
	int error;

	switch (ixl_get_module_type(sc)) {
	case -2:
		return (ENOMEM);
	case -1:
		return (ENXIO);
	case IXL_SFF8024_ID_SFP:
		ops = &ixl_sfp_ops;
		break;
	case IXL_SFF8024_ID_QSFP:
	case IXL_SFF8024_ID_QSFP_PLUS:
	case IXL_SFF8024_ID_QSFP28:
		ops = &ixl_qsfp_ops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	error = (*ops->open)(sc, sff, &page);
	if (error != 0)
		return (error);

	for (i = 0; i < sizeof(sff->sff_data); i++) {
		error = (*ops->get)(sc, sff, i);
		if (error != 0)
			return (error);
	}

	error = (*ops->close)(sc, sff, page);

	return (0);
}

static int
ixl_sff_get_byte(struct ixl_softc *sc, uint8_t dev, uint32_t reg, uint8_t *p)
{
	struct ixl_atq iatq;
	struct ixl_aq_desc *iaq;
	struct ixl_aq_phy_reg_access *param;

	memset(&iatq, 0, sizeof(iatq));
	iaq = &iatq.iatq_desc;
	iaq->iaq_opcode = htole16(IXL_AQ_OP_PHY_GET_REGISTER);
	param = (struct ixl_aq_phy_reg_access *)iaq->iaq_param;
	param->phy_iface = IXL_AQ_PHY_IF_MODULE;
	param->dev_addr = dev;
	htolem32(&param->reg, reg);

	ixl_atq_exec(sc, &iatq, "ixlsffget");

	if (ISSET(sc->sc_ac.ac_if.if_flags, IFF_DEBUG)) {
		printf("%s: %s(dev 0x%02x, reg 0x%02x) -> %04x\n",
		    DEVNAME(sc), __func__,
		    dev, reg, lemtoh16(&iaq->iaq_retval));
	}

	switch (iaq->iaq_retval) {
	case htole16(IXL_AQ_RC_OK):
		break;
	case htole16(IXL_AQ_RC_EBUSY):
		return (EBUSY);
	case htole16(IXL_AQ_RC_ESRCH):
		return (ENODEV);
	case htole16(IXL_AQ_RC_EIO):
	case htole16(IXL_AQ_RC_EINVAL):
	default:
		return (EIO);
	}

	*p = lemtoh32(&param->val);

	return (0);
}

static int
ixl_sff_set_byte(struct ixl_softc *sc, uint8_t dev, uint32_t reg, uint8_t v)
{
	struct ixl_atq iatq;
	struct ixl_aq_desc *iaq;
	struct ixl_aq_phy_reg_access *param;

	memset(&iatq, 0, sizeof(iatq));
	iaq = &iatq.iatq_desc;
	iaq->iaq_opcode = htole16(IXL_AQ_OP_PHY_SET_REGISTER);
	param = (struct ixl_aq_phy_reg_access *)iaq->iaq_param;
	param->phy_iface = IXL_AQ_PHY_IF_MODULE;
	param->dev_addr = dev;
	htolem32(&param->reg, reg);
	htolem32(&param->val, v);

	ixl_atq_exec(sc, &iatq, "ixlsffset");

	if (ISSET(sc->sc_ac.ac_if.if_flags, IFF_DEBUG)) {
		printf("%s: %s(dev 0x%02x, reg 0x%02x, val 0x%02x) -> %04x\n",
		    DEVNAME(sc), __func__,
		    dev, reg, v, lemtoh16(&iaq->iaq_retval));
	}

	switch (iaq->iaq_retval) {
	case htole16(IXL_AQ_RC_OK):
		break;
	case htole16(IXL_AQ_RC_EBUSY):
		return (EBUSY);
	case htole16(IXL_AQ_RC_ESRCH):
		return (ENODEV);
	case htole16(IXL_AQ_RC_EIO):
	case htole16(IXL_AQ_RC_EINVAL):
	default:
		return (EIO);
	}

	return (0);
}

static int
ixl_get_vsi(struct ixl_softc *sc)
{
	struct ixl_dmamem *vsi = &sc->sc_scratch;
	struct ixl_aq_desc iaq;
	struct ixl_aq_vsi_param *param;
	struct ixl_aq_vsi_reply *reply;
	int rv;

	/* grumble, vsi info isn't "known" at compile time */

	memset(&iaq, 0, sizeof(iaq));
	htolem16(&iaq.iaq_flags, IXL_AQ_BUF |
	    (IXL_DMA_LEN(vsi) > I40E_AQ_LARGE_BUF ? IXL_AQ_LB : 0));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_GET_VSI_PARAMS);
	htolem16(&iaq.iaq_datalen, IXL_DMA_LEN(vsi));
	ixl_aq_dva(&iaq, IXL_DMA_DVA(vsi));

	param = (struct ixl_aq_vsi_param *)iaq.iaq_param;
	param->uplink_seid = sc->sc_seid;

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(vsi), 0, IXL_DMA_LEN(vsi),
	    BUS_DMASYNC_PREREAD);

	rv = ixl_atq_poll(sc, &iaq, 250);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(vsi), 0, IXL_DMA_LEN(vsi),
	    BUS_DMASYNC_POSTREAD);

	if (rv != 0) {
		printf("%s: GET VSI timeout\n", DEVNAME(sc));
		return (-1);
	}

	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf("%s: GET VSI error %u\n", DEVNAME(sc),
		    lemtoh16(&iaq.iaq_retval));
		return (-1);
	}

	reply = (struct ixl_aq_vsi_reply *)iaq.iaq_param;
	sc->sc_vsi_number = reply->vsi_number;

	return (0);
}

static int
ixl_set_vsi(struct ixl_softc *sc)
{
	struct ixl_dmamem *vsi = &sc->sc_scratch;
	struct ixl_aq_desc iaq;
	struct ixl_aq_vsi_param *param;
	struct ixl_aq_vsi_data *data = IXL_DMA_KVA(vsi);
	int rv;

	data->valid_sections = htole16(IXL_AQ_VSI_VALID_QUEUE_MAP |
	    IXL_AQ_VSI_VALID_VLAN);

	CLR(data->mapping_flags, htole16(IXL_AQ_VSI_QUE_MAP_MASK));
	SET(data->mapping_flags, htole16(IXL_AQ_VSI_QUE_MAP_CONTIG));
	data->queue_mapping[0] = htole16(0);
	data->tc_mapping[0] = htole16((0 << IXL_AQ_VSI_TC_Q_OFFSET_SHIFT) |
	    (sc->sc_nqueues << IXL_AQ_VSI_TC_Q_NUMBER_SHIFT));

	CLR(data->port_vlan_flags,
	    htole16(IXL_AQ_VSI_PVLAN_MODE_MASK | IXL_AQ_VSI_PVLAN_EMOD_MASK));
	SET(data->port_vlan_flags, htole16(IXL_AQ_VSI_PVLAN_MODE_ALL |
	    IXL_AQ_VSI_PVLAN_EMOD_STR_BOTH));

	/* grumble, vsi info isn't "known" at compile time */

	memset(&iaq, 0, sizeof(iaq));
	htolem16(&iaq.iaq_flags, IXL_AQ_BUF | IXL_AQ_RD |
	    (IXL_DMA_LEN(vsi) > I40E_AQ_LARGE_BUF ? IXL_AQ_LB : 0));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_UPD_VSI_PARAMS);
	htolem16(&iaq.iaq_datalen, IXL_DMA_LEN(vsi));
	ixl_aq_dva(&iaq, IXL_DMA_DVA(vsi));

	param = (struct ixl_aq_vsi_param *)iaq.iaq_param;
	param->uplink_seid = sc->sc_seid;

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(vsi), 0, IXL_DMA_LEN(vsi),
	    BUS_DMASYNC_PREWRITE);

	rv = ixl_atq_poll(sc, &iaq, 250);

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(vsi), 0, IXL_DMA_LEN(vsi),
	    BUS_DMASYNC_POSTWRITE);

	if (rv != 0) {
		printf("%s: UPDATE VSI timeout\n", DEVNAME(sc));
		return (-1);
	}

	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf("%s: UPDATE VSI error %u\n", DEVNAME(sc),
		    lemtoh16(&iaq.iaq_retval));
		return (-1);
	}

	return (0);
}

static const struct ixl_phy_type *
ixl_search_phy_type(uint8_t phy_type)
{
	const struct ixl_phy_type *itype;
	uint64_t mask;
	unsigned int i;

	if (phy_type >= 64)
		return (NULL);

	mask = 1ULL << phy_type;

	for (i = 0; i < nitems(ixl_phy_type_map); i++) {
		itype = &ixl_phy_type_map[i];

		if (ISSET(itype->phy_type, mask))
			return (itype);
	}

	return (NULL);
}

static uint64_t
ixl_search_link_speed(uint8_t link_speed)
{
	const struct ixl_speed_type *type;
	unsigned int i;

	for (i = 0; i < nitems(ixl_speed_type_map); i++) {
		type = &ixl_speed_type_map[i];

		if (ISSET(type->dev_speed, link_speed))
			return (type->net_speed);
	}

	return (0);
}

static int
ixl_set_link_status(struct ixl_softc *sc, const struct ixl_aq_desc *iaq)
{
	const struct ixl_aq_link_status *status;
	const struct ixl_phy_type *itype;
	uint64_t ifm_active = IFM_ETHER;
	uint64_t ifm_status = IFM_AVALID;
	int link_state = LINK_STATE_DOWN;
	uint64_t baudrate = 0;

	status = (const struct ixl_aq_link_status *)iaq->iaq_param;
	if (!ISSET(status->link_info, IXL_AQ_LINK_UP_FUNCTION))
		goto done;

	ifm_active |= IFM_FDX;
	ifm_status |= IFM_ACTIVE;
	link_state = LINK_STATE_FULL_DUPLEX;

	itype = ixl_search_phy_type(status->phy_type);
	if (itype != NULL)
		ifm_active |= itype->ifm_type;

	if (ISSET(status->an_info, IXL_AQ_LINK_PAUSE_TX))
		ifm_active |= IFM_ETH_TXPAUSE;
	if (ISSET(status->an_info, IXL_AQ_LINK_PAUSE_RX))
		ifm_active |= IFM_ETH_RXPAUSE;

	baudrate = ixl_search_link_speed(status->link_speed);

done:
	mtx_enter(&sc->sc_link_state_mtx);
	sc->sc_media_active = ifm_active;
	sc->sc_media_status = ifm_status;
	sc->sc_ac.ac_if.if_baudrate = baudrate;
	mtx_leave(&sc->sc_link_state_mtx);

	return (link_state);
}

static int
ixl_restart_an(struct ixl_softc *sc)
{
	struct ixl_aq_desc iaq;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_opcode = htole16(IXL_AQ_OP_PHY_RESTART_AN);
	iaq.iaq_param[0] =
	    htole32(IXL_AQ_PHY_RESTART_AN | IXL_AQ_PHY_LINK_ENABLE);

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf("%s: RESTART AN timeout\n", DEVNAME(sc));
		return (-1);
	}
	if (iaq.iaq_retval != htole16(IXL_AQ_RC_OK)) {
		printf("%s: RESTART AN error\n", DEVNAME(sc));
		return (-1);
	}

	return (0);
}

static int
ixl_add_macvlan(struct ixl_softc *sc, uint8_t *macaddr, uint16_t vlan, uint16_t flags)
{
	struct ixl_aq_desc iaq;
	struct ixl_aq_add_macvlan *param;
	struct ixl_aq_add_macvlan_elem *elem;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_flags = htole16(IXL_AQ_BUF | IXL_AQ_RD);
	iaq.iaq_opcode = htole16(IXL_AQ_OP_ADD_MACVLAN);
	iaq.iaq_datalen = htole16(sizeof(*elem));
	ixl_aq_dva(&iaq, IXL_DMA_DVA(&sc->sc_scratch));

	param = (struct ixl_aq_add_macvlan *)&iaq.iaq_param;
	param->num_addrs = htole16(1);
	param->seid0 = htole16(0x8000) | sc->sc_seid;
	param->seid1 = 0;
	param->seid2 = 0;

	elem = IXL_DMA_KVA(&sc->sc_scratch);
	memset(elem, 0, sizeof(*elem));
	memcpy(elem->macaddr, macaddr, ETHER_ADDR_LEN);
	elem->flags = htole16(IXL_AQ_OP_ADD_MACVLAN_PERFECT_MATCH | flags);
	elem->vlan = htole16(vlan);

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf("%s: ADD_MACVLAN timeout\n", DEVNAME(sc));
		return (IXL_AQ_RC_EINVAL);
	}

	return letoh16(iaq.iaq_retval);
}

static int
ixl_remove_macvlan(struct ixl_softc *sc, uint8_t *macaddr, uint16_t vlan, uint16_t flags)
{
	struct ixl_aq_desc iaq;
	struct ixl_aq_remove_macvlan *param;
	struct ixl_aq_remove_macvlan_elem *elem;

	memset(&iaq, 0, sizeof(iaq));
	iaq.iaq_flags = htole16(IXL_AQ_BUF | IXL_AQ_RD);
	iaq.iaq_opcode = htole16(IXL_AQ_OP_REMOVE_MACVLAN);
	iaq.iaq_datalen = htole16(sizeof(*elem));
	ixl_aq_dva(&iaq, IXL_DMA_DVA(&sc->sc_scratch));

	param = (struct ixl_aq_remove_macvlan *)&iaq.iaq_param;
	param->num_addrs = htole16(1);
	param->seid0 = htole16(0x8000) | sc->sc_seid;
	param->seid1 = 0;
	param->seid2 = 0;

	elem = IXL_DMA_KVA(&sc->sc_scratch);
	memset(elem, 0, sizeof(*elem));
	memcpy(elem->macaddr, macaddr, ETHER_ADDR_LEN);
	elem->flags = htole16(IXL_AQ_OP_REMOVE_MACVLAN_PERFECT_MATCH | flags);
	elem->vlan = htole16(vlan);

	if (ixl_atq_poll(sc, &iaq, 250) != 0) {
		printf("%s: REMOVE_MACVLAN timeout\n", DEVNAME(sc));
		return (IXL_AQ_RC_EINVAL);
	}

	return letoh16(iaq.iaq_retval);
}

static int
ixl_hmc(struct ixl_softc *sc)
{
	struct {
		uint32_t   count;
		uint32_t   minsize;
		bus_size_t maxcnt;
		bus_size_t setoff;
		bus_size_t setcnt;
	} regs[] = {
		{
			0,
			IXL_HMC_TXQ_MINSIZE,
			I40E_GLHMC_LANTXOBJSZ,
			I40E_GLHMC_LANTXBASE(sc->sc_pf_id),
			I40E_GLHMC_LANTXCNT(sc->sc_pf_id),
		},
		{
			0,
			IXL_HMC_RXQ_MINSIZE,
			I40E_GLHMC_LANRXOBJSZ,
			I40E_GLHMC_LANRXBASE(sc->sc_pf_id),
			I40E_GLHMC_LANRXCNT(sc->sc_pf_id),
		},
		{
			0,
			0,
			I40E_GLHMC_FCOEMAX,
			I40E_GLHMC_FCOEDDPBASE(sc->sc_pf_id),
			I40E_GLHMC_FCOEDDPCNT(sc->sc_pf_id),
		},
		{
			0,
			0,
			I40E_GLHMC_FCOEFMAX,
			I40E_GLHMC_FCOEFBASE(sc->sc_pf_id),
			I40E_GLHMC_FCOEFCNT(sc->sc_pf_id),
		},
	};
	struct ixl_hmc_entry *e;
	uint64_t size, dva;
	uint8_t *kva;
	uint64_t *sdpage;
	unsigned int i;
	int npages, tables;

	CTASSERT(nitems(regs) <= nitems(sc->sc_hmc_entries));

	regs[IXL_HMC_LAN_TX].count = regs[IXL_HMC_LAN_RX].count =
	    ixl_rd(sc, I40E_GLHMC_LANQMAX);

	size = 0;
	for (i = 0; i < nitems(regs); i++) {
		e = &sc->sc_hmc_entries[i];

		e->hmc_count = regs[i].count;
		e->hmc_size = 1U << ixl_rd(sc, regs[i].maxcnt);
		e->hmc_base = size;

		if ((e->hmc_size * 8) < regs[i].minsize) {
			printf("%s: kernel hmc entry is too big\n",
			    DEVNAME(sc));
			return (-1);
		}

		size += roundup(e->hmc_size * e->hmc_count, IXL_HMC_ROUNDUP);
	}
	size = roundup(size, IXL_HMC_PGSIZE);
	npages = size / IXL_HMC_PGSIZE;

	tables = roundup(size, IXL_HMC_L2SZ) / IXL_HMC_L2SZ;

	if (ixl_dmamem_alloc(sc, &sc->sc_hmc_pd, size, IXL_HMC_PGSIZE) != 0) {
		printf("%s: unable to allocate hmc pd memory\n", DEVNAME(sc));
		return (-1);
	}

	if (ixl_dmamem_alloc(sc, &sc->sc_hmc_sd, tables * IXL_HMC_PGSIZE,
	    IXL_HMC_PGSIZE) != 0) {
		printf("%s: unable to allocate hmc sd memory\n", DEVNAME(sc));
		ixl_dmamem_free(sc, &sc->sc_hmc_pd);
		return (-1);
	}

	kva = IXL_DMA_KVA(&sc->sc_hmc_pd);
	memset(kva, 0, IXL_DMA_LEN(&sc->sc_hmc_pd));

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_hmc_pd),
	    0, IXL_DMA_LEN(&sc->sc_hmc_pd),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dva = IXL_DMA_DVA(&sc->sc_hmc_pd);
	sdpage = IXL_DMA_KVA(&sc->sc_hmc_sd);
	for (i = 0; i < npages; i++) {
		htolem64(sdpage++, dva | IXL_HMC_PDVALID);

		dva += IXL_HMC_PGSIZE;
	}

	bus_dmamap_sync(sc->sc_dmat, IXL_DMA_MAP(&sc->sc_hmc_sd),
	    0, IXL_DMA_LEN(&sc->sc_hmc_sd),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dva = IXL_DMA_DVA(&sc->sc_hmc_sd);
	for (i = 0; i < tables; i++) {
		uint32_t count;

		KASSERT(npages >= 0);

		count = (npages > IXL_HMC_PGS) ? IXL_HMC_PGS : npages;

		ixl_wr(sc, I40E_PFHMC_SDDATAHIGH, dva >> 32);
		ixl_wr(sc, I40E_PFHMC_SDDATALOW, dva |
		    (count << I40E_PFHMC_SDDATALOW_PMSDBPCOUNT_SHIFT) |
		    (1U << I40E_PFHMC_SDDATALOW_PMSDVALID_SHIFT));
		ixl_barrier(sc, 0, sc->sc_mems, BUS_SPACE_BARRIER_WRITE);
		ixl_wr(sc, I40E_PFHMC_SDCMD,
		    (1U << I40E_PFHMC_SDCMD_PMSDWR_SHIFT) | i);

		npages -= IXL_HMC_PGS;
		dva += IXL_HMC_PGSIZE;
	}

	for (i = 0; i < nitems(regs); i++) {
		e = &sc->sc_hmc_entries[i];

		ixl_wr(sc, regs[i].setoff, e->hmc_base / IXL_HMC_ROUNDUP);
		ixl_wr(sc, regs[i].setcnt, e->hmc_count);
	}

	return (0);
}

static void
ixl_hmc_free(struct ixl_softc *sc)
{
	ixl_dmamem_free(sc, &sc->sc_hmc_sd);
	ixl_dmamem_free(sc, &sc->sc_hmc_pd);
}

static void
ixl_hmc_pack(void *d, const void *s, const struct ixl_hmc_pack *packing,
    unsigned int npacking)
{
	uint8_t *dst = d;
	const uint8_t *src = s;
	unsigned int i;

	for (i = 0; i < npacking; i++) {
		const struct ixl_hmc_pack *pack = &packing[i];
		unsigned int offset = pack->lsb / 8;
		unsigned int align = pack->lsb % 8;
		const uint8_t *in = src + pack->offset;
		uint8_t *out = dst + offset;
		int width = pack->width;
		unsigned int inbits = 0;

		if (align) {
			inbits = (*in++) << align;
			*out++ |= (inbits & 0xff);
			inbits >>= 8;

			width -= 8 - align;
		}

		while (width >= 8) {
			inbits |= (*in++) << align;
			*out++ = (inbits & 0xff);
			inbits >>= 8;

			width -= 8;
		}

		if (width > 0) {
			inbits |= (*in) << align;
			*out |= (inbits & ((1 << width) - 1));
		}
	}
}

static struct ixl_aq_buf *
ixl_aqb_alloc(struct ixl_softc *sc)
{
	struct ixl_aq_buf *aqb;

	aqb = malloc(sizeof(*aqb), M_DEVBUF, M_WAITOK);
	if (aqb == NULL)
		return (NULL);

	aqb->aqb_data = dma_alloc(IXL_AQ_BUFLEN, PR_WAITOK);
	if (aqb->aqb_data == NULL)
		goto free;

	if (bus_dmamap_create(sc->sc_dmat, IXL_AQ_BUFLEN, 1,
	    IXL_AQ_BUFLEN, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &aqb->aqb_map) != 0)
		goto dma_free;

	if (bus_dmamap_load(sc->sc_dmat, aqb->aqb_map, aqb->aqb_data,
	    IXL_AQ_BUFLEN, NULL, BUS_DMA_WAITOK) != 0)
		goto destroy;

	return (aqb);

destroy:
	bus_dmamap_destroy(sc->sc_dmat, aqb->aqb_map);
dma_free:
	dma_free(aqb->aqb_data, IXL_AQ_BUFLEN);
free:
	free(aqb, M_DEVBUF, sizeof(*aqb));

	return (NULL);
}

static void
ixl_aqb_free(struct ixl_softc *sc, struct ixl_aq_buf *aqb)
{
	bus_dmamap_unload(sc->sc_dmat, aqb->aqb_map);
	bus_dmamap_destroy(sc->sc_dmat, aqb->aqb_map);
	dma_free(aqb->aqb_data, IXL_AQ_BUFLEN);
	free(aqb, M_DEVBUF, sizeof(*aqb));
}

static int
ixl_arq_fill(struct ixl_softc *sc)
{
	struct ixl_aq_buf *aqb;
	struct ixl_aq_desc *arq, *iaq;
	unsigned int prod = sc->sc_arq_prod;
	unsigned int n;
	int post = 0;

	n = if_rxr_get(&sc->sc_arq_ring, IXL_AQ_NUM);
	arq = IXL_DMA_KVA(&sc->sc_arq);

	while (n > 0) {
		aqb = SIMPLEQ_FIRST(&sc->sc_arq_idle);
		if (aqb != NULL)
			SIMPLEQ_REMOVE_HEAD(&sc->sc_arq_idle, aqb_entry);
		else if ((aqb = ixl_aqb_alloc(sc)) == NULL)
			break;

		memset(aqb->aqb_data, 0, IXL_AQ_BUFLEN);

		bus_dmamap_sync(sc->sc_dmat, aqb->aqb_map, 0, IXL_AQ_BUFLEN,
		    BUS_DMASYNC_PREREAD);

		iaq = &arq[prod];
		iaq->iaq_flags = htole16(IXL_AQ_BUF |
		    (IXL_AQ_BUFLEN > I40E_AQ_LARGE_BUF ? IXL_AQ_LB : 0));
		iaq->iaq_opcode = 0;
		iaq->iaq_datalen = htole16(IXL_AQ_BUFLEN);
		iaq->iaq_retval = 0;
		iaq->iaq_cookie = 0;
		iaq->iaq_param[0] = 0;
		iaq->iaq_param[1] = 0;
		ixl_aq_dva(iaq, aqb->aqb_map->dm_segs[0].ds_addr);

		SIMPLEQ_INSERT_TAIL(&sc->sc_arq_live, aqb, aqb_entry);

		prod++;
		prod &= IXL_AQ_MASK;

		post = 1;

		n--;
	}

	if_rxr_put(&sc->sc_arq_ring, n);
	sc->sc_arq_prod = prod;

	return (post);
}

static void
ixl_arq_unfill(struct ixl_softc *sc)
{
	struct ixl_aq_buf *aqb;

	while ((aqb = SIMPLEQ_FIRST(&sc->sc_arq_live)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&sc->sc_arq_live, aqb_entry);

		bus_dmamap_sync(sc->sc_dmat, aqb->aqb_map, 0, IXL_AQ_BUFLEN,
		    BUS_DMASYNC_POSTREAD);
		ixl_aqb_free(sc, aqb);
	}
}

static void
ixl_clear_hw(struct ixl_softc *sc)
{
	uint32_t num_queues, base_queue;
	uint32_t num_pf_int;
	uint32_t num_vf_int;
	uint32_t num_vfs;
	uint32_t i, j;
	uint32_t val;

	/* get number of interrupts, queues, and vfs */
	val = ixl_rd(sc, I40E_GLPCI_CNF2);
	num_pf_int = (val & I40E_GLPCI_CNF2_MSI_X_PF_N_MASK) >>
	    I40E_GLPCI_CNF2_MSI_X_PF_N_SHIFT;
	num_vf_int = (val & I40E_GLPCI_CNF2_MSI_X_VF_N_MASK) >>
	    I40E_GLPCI_CNF2_MSI_X_VF_N_SHIFT;

	val = ixl_rd(sc, I40E_PFLAN_QALLOC);
	base_queue = (val & I40E_PFLAN_QALLOC_FIRSTQ_MASK) >>
	    I40E_PFLAN_QALLOC_FIRSTQ_SHIFT;
	j = (val & I40E_PFLAN_QALLOC_LASTQ_MASK) >>
	    I40E_PFLAN_QALLOC_LASTQ_SHIFT;
	if (val & I40E_PFLAN_QALLOC_VALID_MASK)
		num_queues = (j - base_queue) + 1;
	else
		num_queues = 0;

	val = ixl_rd(sc, I40E_PF_VT_PFALLOC);
	i = (val & I40E_PF_VT_PFALLOC_FIRSTVF_MASK) >>
	    I40E_PF_VT_PFALLOC_FIRSTVF_SHIFT;
	j = (val & I40E_PF_VT_PFALLOC_LASTVF_MASK) >>
	    I40E_PF_VT_PFALLOC_LASTVF_SHIFT;
	if (val & I40E_PF_VT_PFALLOC_VALID_MASK)
		num_vfs = (j - i) + 1;
	else
		num_vfs = 0;

	/* stop all the interrupts */
	ixl_wr(sc, I40E_PFINT_ICR0_ENA, 0);
	val = 0x3 << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT;
	for (i = 0; i < num_pf_int - 2; i++)
		ixl_wr(sc, I40E_PFINT_DYN_CTLN(i), val);

	/* Set the FIRSTQ_INDX field to 0x7FF in PFINT_LNKLSTx */
	val = I40E_QUEUE_TYPE_EOL << I40E_PFINT_LNKLST0_FIRSTQ_INDX_SHIFT;
	ixl_wr(sc, I40E_PFINT_LNKLST0, val);
	for (i = 0; i < num_pf_int - 2; i++)
		ixl_wr(sc, I40E_PFINT_LNKLSTN(i), val);
	val = I40E_QUEUE_TYPE_EOL << I40E_VPINT_LNKLST0_FIRSTQ_INDX_SHIFT;
	for (i = 0; i < num_vfs; i++)
		ixl_wr(sc, I40E_VPINT_LNKLST0(i), val);
	for (i = 0; i < num_vf_int - 2; i++)
		ixl_wr(sc, I40E_VPINT_LNKLSTN(i), val);

	/* warn the HW of the coming Tx disables */
	for (i = 0; i < num_queues; i++) {
		uint32_t abs_queue_idx = base_queue + i;
		uint32_t reg_block = 0;

		if (abs_queue_idx >= 128) {
			reg_block = abs_queue_idx / 128;
			abs_queue_idx %= 128;
		}

		val = ixl_rd(sc, I40E_GLLAN_TXPRE_QDIS(reg_block));
		val &= ~I40E_GLLAN_TXPRE_QDIS_QINDX_MASK;
		val |= (abs_queue_idx << I40E_GLLAN_TXPRE_QDIS_QINDX_SHIFT);
		val |= I40E_GLLAN_TXPRE_QDIS_SET_QDIS_MASK;

		ixl_wr(sc, I40E_GLLAN_TXPRE_QDIS(reg_block), val);
	}
	delaymsec(400);

	/* stop all the queues */
	for (i = 0; i < num_queues; i++) {
		ixl_wr(sc, I40E_QINT_TQCTL(i), 0);
		ixl_wr(sc, I40E_QTX_ENA(i), 0);
		ixl_wr(sc, I40E_QINT_RQCTL(i), 0);
		ixl_wr(sc, I40E_QRX_ENA(i), 0);
	}

	/* short wait for all queue disables to settle */
	delaymsec(50);
}

static int
ixl_pf_reset(struct ixl_softc *sc)
{
	uint32_t cnt = 0;
	uint32_t cnt1 = 0;
	uint32_t reg = 0;
	uint32_t grst_del;

	/*
	 * Poll for Global Reset steady state in case of recent GRST.
	 * The grst delay value is in 100ms units, and we'll wait a
	 * couple counts longer to be sure we don't just miss the end.
	 */
	grst_del = ixl_rd(sc, I40E_GLGEN_RSTCTL);
	grst_del &= I40E_GLGEN_RSTCTL_GRSTDEL_MASK;
	grst_del >>= I40E_GLGEN_RSTCTL_GRSTDEL_SHIFT;
	grst_del += 10;

	for (cnt = 0; cnt < grst_del; cnt++) {
		reg = ixl_rd(sc, I40E_GLGEN_RSTAT);
		if (!(reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK))
			break;
		delaymsec(100);
	}
	if (reg & I40E_GLGEN_RSTAT_DEVSTATE_MASK) {
		printf(", Global reset polling failed to complete\n");
		return (-1);
	}

	/* Now Wait for the FW to be ready */
	for (cnt1 = 0; cnt1 < I40E_PF_RESET_WAIT_COUNT; cnt1++) {
		reg = ixl_rd(sc, I40E_GLNVM_ULD);
		reg &= (I40E_GLNVM_ULD_CONF_CORE_DONE_MASK |
		    I40E_GLNVM_ULD_CONF_GLOBAL_DONE_MASK);
		if (reg == (I40E_GLNVM_ULD_CONF_CORE_DONE_MASK |
		    I40E_GLNVM_ULD_CONF_GLOBAL_DONE_MASK))
			break;

		delaymsec(10);
	}
	if (!(reg & (I40E_GLNVM_ULD_CONF_CORE_DONE_MASK |
	    I40E_GLNVM_ULD_CONF_GLOBAL_DONE_MASK))) {
		printf(", wait for FW Reset complete timed out "
		    "(I40E_GLNVM_ULD = 0x%x)\n", reg);
		return (-1);
	}

	/*
	 * If there was a Global Reset in progress when we got here,
	 * we don't need to do the PF Reset
	 */
	if (cnt == 0) {
		reg = ixl_rd(sc, I40E_PFGEN_CTRL);
		ixl_wr(sc, I40E_PFGEN_CTRL, reg | I40E_PFGEN_CTRL_PFSWR_MASK);
		for (cnt = 0; cnt < I40E_PF_RESET_WAIT_COUNT; cnt++) {
			reg = ixl_rd(sc, I40E_PFGEN_CTRL);
			if (!(reg & I40E_PFGEN_CTRL_PFSWR_MASK))
				break;
			delaymsec(1);
		}
		if (reg & I40E_PFGEN_CTRL_PFSWR_MASK) {
			printf(", PF reset polling failed to complete"
			    "(I40E_PFGEN_CTRL= 0x%x)\n", reg);
			return (-1);
		}
	}

	return (0);
}

static uint32_t
ixl_710_rd_ctl(struct ixl_softc *sc, uint32_t r)
{
	struct ixl_atq iatq;
	struct ixl_aq_desc *iaq;
	uint16_t retval;

	memset(&iatq, 0, sizeof(iatq));
	iaq = &iatq.iatq_desc;
	iaq->iaq_opcode = htole16(IXL_AQ_OP_RX_CTL_READ);
	htolem32(&iaq->iaq_param[1], r);

	ixl_atq_exec(sc, &iatq, "ixl710rd");

	retval = lemtoh16(&iaq->iaq_retval);
	if (retval != IXL_AQ_RC_OK) {
		printf("%s: %s failed (%u)\n", DEVNAME(sc), __func__, retval);
		return (~0U);
	}

	return (lemtoh32(&iaq->iaq_param[3]));
}

static void
ixl_710_wr_ctl(struct ixl_softc *sc, uint32_t r, uint32_t v)
{
	struct ixl_atq iatq;
	struct ixl_aq_desc *iaq;
	uint16_t retval;

	memset(&iatq, 0, sizeof(iatq));
	iaq = &iatq.iatq_desc;
	iaq->iaq_opcode = htole16(IXL_AQ_OP_RX_CTL_WRITE);
	htolem32(&iaq->iaq_param[1], r);
	htolem32(&iaq->iaq_param[3], v);

	ixl_atq_exec(sc, &iatq, "ixl710wr");

	retval = lemtoh16(&iaq->iaq_retval);
	if (retval != IXL_AQ_RC_OK) {
		printf("%s: %s %08x=%08x failed (%u)\n",
		    DEVNAME(sc), __func__, r, v, retval);
	}
}

static int
ixl_710_set_rss_key(struct ixl_softc *sc, const struct ixl_rss_key *rsskey)
{
	unsigned int i;

	for (i = 0; i < nitems(rsskey->key); i++)
		ixl_wr_ctl(sc, I40E_PFQF_HKEY(i), rsskey->key[i]);

	return (0);
}

static int
ixl_710_set_rss_lut(struct ixl_softc *sc, const struct ixl_rss_lut_128 *lut)
{
	unsigned int i;

	for (i = 0; i < nitems(lut->entries); i++)
		ixl_wr(sc, I40E_PFQF_HLUT(i), lut->entries[i]);

	return (0);
}

static uint32_t
ixl_722_rd_ctl(struct ixl_softc *sc, uint32_t r)
{
	return (ixl_rd(sc, r));
}

static void
ixl_722_wr_ctl(struct ixl_softc *sc, uint32_t r, uint32_t v)
{
	ixl_wr(sc, r, v);
}

static int
ixl_722_set_rss_key(struct ixl_softc *sc, const struct ixl_rss_key *rsskey)
{
	/* XXX */

	return (0);
}

static int
ixl_722_set_rss_lut(struct ixl_softc *sc, const struct ixl_rss_lut_128 *lut)
{
	/* XXX */

	return (0);
}

static int
ixl_dmamem_alloc(struct ixl_softc *sc, struct ixl_dmamem *ixm,
    bus_size_t size, u_int align)
{
	ixm->ixm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, ixm->ixm_size, 1,
	    ixm->ixm_size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
	    &ixm->ixm_map) != 0)
		return (1);
	if (bus_dmamem_alloc(sc->sc_dmat, ixm->ixm_size,
	    align, 0, &ixm->ixm_seg, 1, &ixm->ixm_nsegs,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;
	if (bus_dmamem_map(sc->sc_dmat, &ixm->ixm_seg, ixm->ixm_nsegs,
	    ixm->ixm_size, &ixm->ixm_kva, BUS_DMA_WAITOK) != 0)
		goto free;
	if (bus_dmamap_load(sc->sc_dmat, ixm->ixm_map, ixm->ixm_kva,
	    ixm->ixm_size, NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return (0);
unmap:
	bus_dmamem_unmap(sc->sc_dmat, ixm->ixm_kva, ixm->ixm_size);
free:
	bus_dmamem_free(sc->sc_dmat, &ixm->ixm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, ixm->ixm_map);
	return (1);
}

static void
ixl_dmamem_free(struct ixl_softc *sc, struct ixl_dmamem *ixm)
{
	bus_dmamap_unload(sc->sc_dmat, ixm->ixm_map);
	bus_dmamem_unmap(sc->sc_dmat, ixm->ixm_kva, ixm->ixm_size);
	bus_dmamem_free(sc->sc_dmat, &ixm->ixm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, ixm->ixm_map);
}

#if NKSTAT > 0

CTASSERT(KSTAT_KV_U_NONE <= 0xffU);
CTASSERT(KSTAT_KV_U_PACKETS <= 0xffU);
CTASSERT(KSTAT_KV_U_BYTES <= 0xffU);

struct ixl_counter {
	const char		*c_name;
	uint32_t		 c_base;
	uint8_t			 c_width;
	uint8_t			 c_type;
};

const struct ixl_counter ixl_port_counters[] = {
	/* GORC */
	{ "rx bytes",		0x00300000, 48, KSTAT_KV_U_BYTES },
	/* MLFC */
	{ "mac local errs",	0x00300020, 32, KSTAT_KV_U_NONE },
	/* MRFC */
	{ "mac remote errs",	0x00300040, 32, KSTAT_KV_U_NONE },
	/* MSPDC */
	{ "mac short",		0x00300060, 32, KSTAT_KV_U_PACKETS },
	/* CRCERRS */
	{ "crc errs",		0x00300080, 32, KSTAT_KV_U_PACKETS },
	/* RLEC */
	{ "rx len errs",	0x003000a0, 32, KSTAT_KV_U_PACKETS },
	/* ERRBC */
	{ "byte errs",		0x003000c0, 32, KSTAT_KV_U_PACKETS },
	/* ILLERRC */
	{ "illegal byte",	0x003000d0, 32, KSTAT_KV_U_PACKETS },
	/* RUC */
	{ "rx undersize",	0x00300100, 32, KSTAT_KV_U_PACKETS },
	/* ROC */
	{ "rx oversize",	0x00300120, 32, KSTAT_KV_U_PACKETS },
	/* LXONRXCNT */
	{ "rx link xon",	0x00300140, 32, KSTAT_KV_U_PACKETS },
	/* LXOFFRXCNT */
	{ "rx link xoff",	0x00300160, 32, KSTAT_KV_U_PACKETS },

	/* Priority XON Received Count */
	/* Priority XOFF Received Count */
	/* Priority XON to XOFF Count */

	/* PRC64 */
	{ "rx 64B",		0x00300480, 48, KSTAT_KV_U_PACKETS },
	/* PRC127 */
	{ "rx 65-127B",		0x003004A0, 48, KSTAT_KV_U_PACKETS },
	/* PRC255 */
	{ "rx 128-255B",	0x003004C0, 48, KSTAT_KV_U_PACKETS },
	/* PRC511 */
	{ "rx 256-511B",	0x003004E0, 48, KSTAT_KV_U_PACKETS },
	/* PRC1023 */
	{ "rx 512-1023B",	0x00300500, 48, KSTAT_KV_U_PACKETS },
	/* PRC1522 */
	{ "rx 1024-1522B",	0x00300520, 48, KSTAT_KV_U_PACKETS },
	/* PRC9522 */
	{ "rx 1523-9522B",	0x00300540, 48, KSTAT_KV_U_PACKETS },
	/* ROC */
	{ "rx fragment",	0x00300560, 32, KSTAT_KV_U_PACKETS },
	/* RJC */
	{ "rx jabber",		0x00300580, 32, KSTAT_KV_U_PACKETS },
	/* UPRC */
	{ "rx ucasts",		0x003005a0, 48, KSTAT_KV_U_PACKETS },
	/* MPRC */
	{ "rx mcasts",		0x003005c0, 48, KSTAT_KV_U_PACKETS },
	/* BPRC */
	{ "rx bcasts",		0x003005e0, 48, KSTAT_KV_U_PACKETS },
	/* RDPC */
	{ "rx discards",	0x00300600, 32, KSTAT_KV_U_PACKETS },
	/* LDPC */
	{ "rx lo discards",	0x00300620, 32, KSTAT_KV_U_PACKETS },
	/* RUPP */
	{ "rx no dest",		0x00300660, 32, KSTAT_KV_U_PACKETS },

	/* GOTC */
	{ "tx bytes",		0x00300680, 48, KSTAT_KV_U_BYTES },
	/* PTC64 */
	{ "tx 64B",		0x003006A0, 48, KSTAT_KV_U_PACKETS },
	/* PTC127 */
	{ "tx 65-127B",		0x003006C0, 48, KSTAT_KV_U_PACKETS },
	/* PTC255 */
	{ "tx 128-255B",	0x003006E0, 48, KSTAT_KV_U_PACKETS },
	/* PTC511 */
	{ "tx 256-511B",	0x00300700, 48, KSTAT_KV_U_PACKETS },
	/* PTC1023 */
	{ "tx 512-1023B",	0x00300720, 48, KSTAT_KV_U_PACKETS },
	/* PTC1522 */
	{ "tx 1024-1522B",	0x00300740, 48, KSTAT_KV_U_PACKETS },
	/* PTC9522 */
	{ "tx 1523-9522B",	0x00300760, 48, KSTAT_KV_U_PACKETS },

	/* Priority XON Transmitted Count */
	/* Priority XOFF Transmitted Count */

	/* LXONTXC */
	{ "tx link xon",	0x00300980, 48, KSTAT_KV_U_PACKETS },
	/* LXOFFTXC */
	{ "tx link xoff",	0x003009a0, 48, KSTAT_KV_U_PACKETS },
	/* UPTC */
	{ "tx ucasts",		0x003009c0, 48, KSTAT_KV_U_PACKETS },
	/* MPTC */
	{ "tx mcasts",		0x003009e0, 48, KSTAT_KV_U_PACKETS },
	/* BPTC */
	{ "tx bcasts",		0x00300a00, 48, KSTAT_KV_U_PACKETS },
	/* TDOLD */
	{ "tx link down",	0x00300a20, 48, KSTAT_KV_U_PACKETS },
};

const struct ixl_counter ixl_vsi_counters[] = {
	/* VSI RDPC */
	{ "rx discards",	0x00310000, 32, KSTAT_KV_U_PACKETS },
	/* VSI GOTC */
	{ "tx bytes",		0x00328000, 48, KSTAT_KV_U_BYTES },
	/* VSI UPTC */
	{ "tx ucasts",		0x0033c000, 48, KSTAT_KV_U_PACKETS },
	/* VSI MPTC */
	{ "tx mcasts",		0x0033cc00, 48, KSTAT_KV_U_PACKETS },
	/* VSI BPTC */
	{ "tx bcasts",		0x0033d800, 48, KSTAT_KV_U_PACKETS },
	/* VSI TEPC */
	{ "tx errs",		0x00344000, 48, KSTAT_KV_U_PACKETS },
	/* VSI TDPC */
	{ "tx discards",	0x00348000, 48, KSTAT_KV_U_PACKETS },
	/* VSI GORC */
	{ "rx bytes",		0x00358000, 48, KSTAT_KV_U_BYTES },
	/* VSI UPRC */
	{ "rx ucasts",		0x0036c000, 48, KSTAT_KV_U_PACKETS },
	/* VSI MPRC */
	{ "rx mcasts",		0x0036cc00, 48, KSTAT_KV_U_PACKETS },
	/* VSI BPRC */
	{ "rx bcasts",		0x0036d800, 48, KSTAT_KV_U_PACKETS },
	/* VSI RUPP */
	{ "rx noproto",		0x0036e400, 32, KSTAT_KV_U_PACKETS },
};

struct ixl_counter_state {
	const struct ixl_counter
				*counters;
	uint64_t		*values;
	size_t			 n;
	uint32_t		 index;
	unsigned int		 gen;
};

static void
ixl_rd_counters(struct ixl_softc *sc, const struct ixl_counter_state *state,
    uint64_t *vs)
{
	const struct ixl_counter *c;
	bus_addr_t r;
	uint64_t v;
	size_t i;

	for (i = 0; i < state->n; i++) {
		c = &state->counters[i];

		r = c->c_base + (state->index * 8);

		if (c->c_width == 32)
			v = bus_space_read_4(sc->sc_memt, sc->sc_memh, r);
		else
			v = bus_space_read_8(sc->sc_memt, sc->sc_memh, r);

		vs[i] = v;
	}
}

static int
ixl_kstat_read(struct kstat *ks)
{
	struct ixl_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	struct ixl_counter_state *state = ks->ks_ptr;
	unsigned int gen = (state->gen++) & 1;
	uint64_t *ovs = state->values + (gen * state->n);
	uint64_t *nvs = state->values + (!gen * state->n);
	size_t i;

	ixl_rd_counters(sc, state, nvs);
	getnanouptime(&ks->ks_updated);

	for (i = 0; i < state->n; i++) {
		const struct ixl_counter *c = &state->counters[i];
		uint64_t n = nvs[i], o = ovs[i];

		if (c->c_width < 64) {
			if (n < o)
				n += (1ULL << c->c_width);
		}

		kstat_kv_u64(&kvs[i]) += (n - o);
	}

	return (0);
}

static void
ixl_kstat_tick(void *arg)
{
	struct ixl_softc *sc = arg;

	timeout_add_sec(&sc->sc_kstat_tmo, 4);

	mtx_enter(&sc->sc_kstat_mtx);

	ixl_kstat_read(sc->sc_port_kstat);
	ixl_kstat_read(sc->sc_vsi_kstat);

	mtx_leave(&sc->sc_kstat_mtx);
}

static struct kstat *
ixl_kstat_create(struct ixl_softc *sc, const char *name,
    const struct ixl_counter *counters, size_t n, uint32_t index)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	struct ixl_counter_state *state;
	const struct ixl_counter *c;
	unsigned int i;

	ks = kstat_create(DEVNAME(sc), 0, name, 0, KSTAT_T_KV, 0);
	if (ks == NULL) {
		/* unable to create kstats */
		return (NULL);
	}

	kvs = mallocarray(n, sizeof(*kvs), M_DEVBUF, M_WAITOK|M_ZERO);
	for (i = 0; i < n; i++) {
		c = &counters[i];

		kstat_kv_unit_init(&kvs[i], c->c_name,
		    KSTAT_KV_T_COUNTER64, c->c_type);
	}

	ks->ks_data = kvs;
	ks->ks_datalen = n * sizeof(*kvs);
	ks->ks_read = ixl_kstat_read;

	state = malloc(sizeof(*state), M_DEVBUF, M_WAITOK|M_ZERO);
	state->counters = counters;
	state->n = n;
	state->values = mallocarray(n * 2, sizeof(*state->values),
	    M_DEVBUF, M_WAITOK|M_ZERO);
	state->index = index;
	ks->ks_ptr = state;

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	kstat_install(ks);

	/* fetch a baseline */
	ixl_rd_counters(sc, state, state->values);

	return (ks);
}

static void
ixl_kstat_attach(struct ixl_softc *sc)
{
	mtx_init(&sc->sc_kstat_mtx, IPL_SOFTCLOCK);
	timeout_set(&sc->sc_kstat_tmo, ixl_kstat_tick, sc);

	sc->sc_port_kstat = ixl_kstat_create(sc, "ixl-port",
	    ixl_port_counters, nitems(ixl_port_counters), sc->sc_port);
	sc->sc_vsi_kstat = ixl_kstat_create(sc, "ixl-vsi",
	    ixl_vsi_counters, nitems(ixl_vsi_counters),
	    lemtoh16(&sc->sc_vsi_number));

	/* ixl counters go up even when the interface is down */
	timeout_add_sec(&sc->sc_kstat_tmo, 4);
}

#endif /* NKSTAT > 0 */
