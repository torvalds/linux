/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#ifndef _I40E_DCB_H_
#define _I40E_DCB_H_

#include "i40e_type.h"

#define I40E_DCBX_OFFLOAD_DISABLED	0
#define I40E_DCBX_OFFLOAD_ENABLED	1

#define I40E_DCBX_STATUS_NOT_STARTED	0
#define I40E_DCBX_STATUS_IN_PROGRESS	1
#define I40E_DCBX_STATUS_DONE		2
#define I40E_DCBX_STATUS_MULTIPLE_PEERS	3
#define I40E_DCBX_STATUS_DISABLED	7

#define I40E_TLV_TYPE_END		0
#define I40E_TLV_TYPE_ORG		127

#define I40E_IEEE_8021QAZ_OUI		0x0080C2
#define I40E_IEEE_SUBTYPE_ETS_CFG	9
#define I40E_IEEE_SUBTYPE_ETS_REC	10
#define I40E_IEEE_SUBTYPE_PFC_CFG	11
#define I40E_IEEE_SUBTYPE_APP_PRI	12

#define I40E_CEE_DCBX_OUI		0x001b21
#define I40E_CEE_DCBX_TYPE		2

#define I40E_CEE_SUBTYPE_CTRL		1
#define I40E_CEE_SUBTYPE_PG_CFG		2
#define I40E_CEE_SUBTYPE_PFC_CFG	3
#define I40E_CEE_SUBTYPE_APP_PRI	4

#define I40E_CEE_MAX_FEAT_TYPE		3
#define I40E_LLDP_ADMINSTATUS_DISABLED		0
#define I40E_LLDP_ADMINSTATUS_ENABLED_RX	1
#define I40E_LLDP_ADMINSTATUS_ENABLED_TX	2
#define I40E_LLDP_ADMINSTATUS_ENABLED_RXTX	3

/* Defines for LLDP TLV header */
#define I40E_LLDP_MIB_HLEN		14
#define I40E_LLDP_TLV_LEN_SHIFT		0
#define I40E_LLDP_TLV_LEN_MASK		(0x01FF << I40E_LLDP_TLV_LEN_SHIFT)
#define I40E_LLDP_TLV_TYPE_SHIFT	9
#define I40E_LLDP_TLV_TYPE_MASK		(0x7F << I40E_LLDP_TLV_TYPE_SHIFT)
#define I40E_LLDP_TLV_SUBTYPE_SHIFT	0
#define I40E_LLDP_TLV_SUBTYPE_MASK	(0xFF << I40E_LLDP_TLV_SUBTYPE_SHIFT)
#define I40E_LLDP_TLV_OUI_SHIFT		8
#define I40E_LLDP_TLV_OUI_MASK		(0xFFFFFF << I40E_LLDP_TLV_OUI_SHIFT)

/* Defines for IEEE ETS TLV */
#define I40E_IEEE_ETS_MAXTC_SHIFT	0
#define I40E_IEEE_ETS_MAXTC_MASK	(0x7 << I40E_IEEE_ETS_MAXTC_SHIFT)
#define I40E_IEEE_ETS_CBS_SHIFT		6
#define I40E_IEEE_ETS_CBS_MASK		BIT(I40E_IEEE_ETS_CBS_SHIFT)
#define I40E_IEEE_ETS_WILLING_SHIFT	7
#define I40E_IEEE_ETS_WILLING_MASK	BIT(I40E_IEEE_ETS_WILLING_SHIFT)
#define I40E_IEEE_ETS_PRIO_0_SHIFT	0
#define I40E_IEEE_ETS_PRIO_0_MASK	(0x7 << I40E_IEEE_ETS_PRIO_0_SHIFT)
#define I40E_IEEE_ETS_PRIO_1_SHIFT	4
#define I40E_IEEE_ETS_PRIO_1_MASK	(0x7 << I40E_IEEE_ETS_PRIO_1_SHIFT)
#define I40E_CEE_PGID_PRIO_0_SHIFT	0
#define I40E_CEE_PGID_PRIO_0_MASK	(0xF << I40E_CEE_PGID_PRIO_0_SHIFT)
#define I40E_CEE_PGID_PRIO_1_SHIFT	4
#define I40E_CEE_PGID_PRIO_1_MASK	(0xF << I40E_CEE_PGID_PRIO_1_SHIFT)
#define I40E_CEE_PGID_STRICT		15

/* Defines for IEEE TSA types */
#define I40E_IEEE_TSA_STRICT		0
#define I40E_IEEE_TSA_CBS		1
#define I40E_IEEE_TSA_ETS		2
#define I40E_IEEE_TSA_VENDOR		255

/* Defines for IEEE PFC TLV */
#define I40E_IEEE_PFC_CAP_SHIFT		0
#define I40E_IEEE_PFC_CAP_MASK		(0xF << I40E_IEEE_PFC_CAP_SHIFT)
#define I40E_IEEE_PFC_MBC_SHIFT		6
#define I40E_IEEE_PFC_MBC_MASK		BIT(I40E_IEEE_PFC_MBC_SHIFT)
#define I40E_IEEE_PFC_WILLING_SHIFT	7
#define I40E_IEEE_PFC_WILLING_MASK	BIT(I40E_IEEE_PFC_WILLING_SHIFT)

/* Defines for IEEE APP TLV */
#define I40E_IEEE_APP_SEL_SHIFT		0
#define I40E_IEEE_APP_SEL_MASK		(0x7 << I40E_IEEE_APP_SEL_SHIFT)
#define I40E_IEEE_APP_PRIO_SHIFT	5
#define I40E_IEEE_APP_PRIO_MASK		(0x7 << I40E_IEEE_APP_PRIO_SHIFT)

/* TLV definitions for preparing MIB */
#define I40E_TLV_ID_CHASSIS_ID		0
#define I40E_TLV_ID_PORT_ID		1
#define I40E_TLV_ID_TIME_TO_LIVE	2
#define I40E_IEEE_TLV_ID_ETS_CFG	3
#define I40E_IEEE_TLV_ID_ETS_REC	4
#define I40E_IEEE_TLV_ID_PFC_CFG	5
#define I40E_IEEE_TLV_ID_APP_PRI	6
#define I40E_TLV_ID_END_OF_LLDPPDU	7
#define I40E_TLV_ID_START		I40E_IEEE_TLV_ID_ETS_CFG

#define I40E_IEEE_ETS_TLV_LENGTH	25
#define I40E_IEEE_PFC_TLV_LENGTH	6
#define I40E_IEEE_APP_TLV_LENGTH	11

#pragma pack(1)

/* IEEE 802.1AB LLDP TLV structure */
struct i40e_lldp_generic_tlv {
	__be16 typelength;
	u8 tlvinfo[1];
};

/* IEEE 802.1AB LLDP Organization specific TLV */
struct i40e_lldp_org_tlv {
	__be16 typelength;
	__be32 ouisubtype;
	u8 tlvinfo[1];
};

struct i40e_cee_tlv_hdr {
	__be16 typelen;
	u8 operver;
	u8 maxver;
};

struct i40e_cee_ctrl_tlv {
	struct i40e_cee_tlv_hdr hdr;
	__be32 seqno;
	__be32 ackno;
};

struct i40e_cee_feat_tlv {
	struct i40e_cee_tlv_hdr hdr;
	u8 en_will_err; /* Bits: |En|Will|Err|Reserved(5)| */
#define I40E_CEE_FEAT_TLV_ENABLE_MASK	0x80
#define I40E_CEE_FEAT_TLV_WILLING_MASK	0x40
#define I40E_CEE_FEAT_TLV_ERR_MASK	0x20
	u8 subtype;
	u8 tlvinfo[1];
};

struct i40e_cee_app_prio {
	__be16 protocol;
	u8 upper_oui_sel; /* Bits: |Upper OUI(6)|Selector(2)| */
#define I40E_CEE_APP_SELECTOR_MASK	0x03
	__be16 lower_oui;
	u8 prio_map;
};
#pragma pack()

/*
 * TODO: The below structures related LLDP/DCBX variables
 * and statistics are defined but need to find how to get
 * the required information from the Firmware to use them
 */

/* IEEE 802.1AB LLDP Agent Statistics */
struct i40e_lldp_stats {
	u64 remtablelastchangetime;
	u64 remtableinserts;
	u64 remtabledeletes;
	u64 remtabledrops;
	u64 remtableageouts;
	u64 txframestotal;
	u64 rxframesdiscarded;
	u64 rxportframeerrors;
	u64 rxportframestotal;
	u64 rxporttlvsdiscardedtotal;
	u64 rxporttlvsunrecognizedtotal;
	u64 remtoomanyneighbors;
};

/* IEEE 802.1Qaz DCBX variables */
struct i40e_dcbx_variables {
	u32 defmaxtrafficclasses;
	u32 defprioritytcmapping;
	u32 deftcbandwidth;
	u32 deftsaassignment;
};

enum i40e_status_code i40e_get_dcbx_status(struct i40e_hw *hw,
					   u16 *status);
enum i40e_status_code i40e_lldp_to_dcb_config(u8 *lldpmib,
					      struct i40e_dcbx_config *dcbcfg);
enum i40e_status_code i40e_aq_get_dcb_config(struct i40e_hw *hw, u8 mib_type,
					     u8 bridgetype,
					     struct i40e_dcbx_config *dcbcfg);
enum i40e_status_code i40e_get_dcb_config(struct i40e_hw *hw);
enum i40e_status_code i40e_init_dcb(struct i40e_hw *hw);
enum i40e_status_code i40e_set_dcb_config(struct i40e_hw *hw);
enum i40e_status_code i40e_dcb_config_to_lldp(u8 *lldpmib, u16 *miblen,
					      struct i40e_dcbx_config *dcbcfg);

#endif /* _I40E_DCB_H_ */
