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

#include "i40e_adminq.h"
#include "i40e_prototype.h"
#include "i40e_dcb.h"

/**
 * i40e_get_dcbx_status
 * @hw: pointer to the hw struct
 * @status: Embedded DCBX Engine Status
 *
 * Get the DCBX status from the Firmware
 **/
enum i40e_status_code i40e_get_dcbx_status(struct i40e_hw *hw, u16 *status)
{
	u32 reg;

	if (!status)
		return I40E_ERR_PARAM;

	reg = rd32(hw, I40E_PRTDCB_GENS);
	*status = (u16)((reg & I40E_PRTDCB_GENS_DCBX_STATUS_MASK) >>
			I40E_PRTDCB_GENS_DCBX_STATUS_SHIFT);

	return I40E_SUCCESS;
}

/**
 * i40e_parse_ieee_etscfg_tlv
 * @tlv: IEEE 802.1Qaz ETS CFG TLV
 * @dcbcfg: Local store to update ETS CFG data
 *
 * Parses IEEE 802.1Qaz ETS CFG TLV
 **/
static void i40e_parse_ieee_etscfg_tlv(struct i40e_lldp_org_tlv *tlv,
				       struct i40e_dcbx_config *dcbcfg)
{
	struct i40e_dcb_ets_config *etscfg;
	u8 *buf = tlv->tlvinfo;
	u16 offset = 0;
	u8 priority;
	int i;

	/* First Octet post subtype
	 * --------------------------
	 * |will-|CBS  | Re-  | Max |
	 * |ing  |     |served| TCs |
	 * --------------------------
	 * |1bit | 1bit|3 bits|3bits|
	 */
	etscfg = &dcbcfg->etscfg;
	etscfg->willing = (u8)((buf[offset] & I40E_IEEE_ETS_WILLING_MASK) >>
			       I40E_IEEE_ETS_WILLING_SHIFT);
	etscfg->cbs = (u8)((buf[offset] & I40E_IEEE_ETS_CBS_MASK) >>
			   I40E_IEEE_ETS_CBS_SHIFT);
	etscfg->maxtcs = (u8)((buf[offset] & I40E_IEEE_ETS_MAXTC_MASK) >>
			      I40E_IEEE_ETS_MAXTC_SHIFT);

	/* Move offset to Priority Assignment Table */
	offset++;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		priority = (u8)((buf[offset] & I40E_IEEE_ETS_PRIO_1_MASK) >>
				I40E_IEEE_ETS_PRIO_1_SHIFT);
		etscfg->prioritytable[i * 2] =  priority;
		priority = (u8)((buf[offset] & I40E_IEEE_ETS_PRIO_0_MASK) >>
				I40E_IEEE_ETS_PRIO_0_SHIFT);
		etscfg->prioritytable[i * 2 + 1] = priority;
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		etscfg->tcbwtable[i] = buf[offset++];

	/* TSA Assignment Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		etscfg->tsatable[i] = buf[offset++];
}

/**
 * i40e_parse_ieee_etsrec_tlv
 * @tlv: IEEE 802.1Qaz ETS REC TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Parses IEEE 802.1Qaz ETS REC TLV
 **/
static void i40e_parse_ieee_etsrec_tlv(struct i40e_lldp_org_tlv *tlv,
				       struct i40e_dcbx_config *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;
	u16 offset = 0;
	u8 priority;
	int i;

	/* Move offset to priority table */
	offset++;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		priority = (u8)((buf[offset] & I40E_IEEE_ETS_PRIO_1_MASK) >>
				I40E_IEEE_ETS_PRIO_1_SHIFT);
		dcbcfg->etsrec.prioritytable[i*2] =  priority;
		priority = (u8)((buf[offset] & I40E_IEEE_ETS_PRIO_0_MASK) >>
				I40E_IEEE_ETS_PRIO_0_SHIFT);
		dcbcfg->etsrec.prioritytable[i*2 + 1] = priority;
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		dcbcfg->etsrec.tcbwtable[i] = buf[offset++];

	/* TSA Assignment Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		dcbcfg->etsrec.tsatable[i] = buf[offset++];
}

/**
 * i40e_parse_ieee_pfccfg_tlv
 * @tlv: IEEE 802.1Qaz PFC CFG TLV
 * @dcbcfg: Local store to update PFC CFG data
 *
 * Parses IEEE 802.1Qaz PFC CFG TLV
 **/
static void i40e_parse_ieee_pfccfg_tlv(struct i40e_lldp_org_tlv *tlv,
				       struct i40e_dcbx_config *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;

	/* ----------------------------------------
	 * |will-|MBC  | Re-  | PFC |  PFC Enable  |
	 * |ing  |     |served| cap |              |
	 * -----------------------------------------
	 * |1bit | 1bit|2 bits|4bits| 1 octet      |
	 */
	dcbcfg->pfc.willing = (u8)((buf[0] & I40E_IEEE_PFC_WILLING_MASK) >>
				   I40E_IEEE_PFC_WILLING_SHIFT);
	dcbcfg->pfc.mbc = (u8)((buf[0] & I40E_IEEE_PFC_MBC_MASK) >>
			       I40E_IEEE_PFC_MBC_SHIFT);
	dcbcfg->pfc.pfccap = (u8)((buf[0] & I40E_IEEE_PFC_CAP_MASK) >>
				  I40E_IEEE_PFC_CAP_SHIFT);
	dcbcfg->pfc.pfcenable = buf[1];
}

/**
 * i40e_parse_ieee_app_tlv
 * @tlv: IEEE 802.1Qaz APP TLV
 * @dcbcfg: Local store to update APP PRIO data
 *
 * Parses IEEE 802.1Qaz APP PRIO TLV
 **/
static void i40e_parse_ieee_app_tlv(struct i40e_lldp_org_tlv *tlv,
				    struct i40e_dcbx_config *dcbcfg)
{
	u16 typelength;
	u16 offset = 0;
	u16 length;
	int i = 0;
	u8 *buf;

	typelength = I40E_NTOHS(tlv->typelength);
	length = (u16)((typelength & I40E_LLDP_TLV_LEN_MASK) >>
		       I40E_LLDP_TLV_LEN_SHIFT);
	buf = tlv->tlvinfo;

	/* The App priority table starts 5 octets after TLV header */
	length -= (sizeof(tlv->ouisubtype) + 1);

	/* Move offset to App Priority Table */
	offset++;

	/* Application Priority Table (3 octets)
	 * Octets:|         1          |    2    |    3    |
	 *        -----------------------------------------
	 *        |Priority|Rsrvd| Sel |    Protocol ID    |
	 *        -----------------------------------------
	 *   Bits:|23    21|20 19|18 16|15                0|
	 *        -----------------------------------------
	 */
	while (offset < length) {
		dcbcfg->app[i].priority = (u8)((buf[offset] &
						I40E_IEEE_APP_PRIO_MASK) >>
					       I40E_IEEE_APP_PRIO_SHIFT);
		dcbcfg->app[i].selector = (u8)((buf[offset] &
						I40E_IEEE_APP_SEL_MASK) >>
					       I40E_IEEE_APP_SEL_SHIFT);
		dcbcfg->app[i].protocolid = (buf[offset + 1] << 0x8) |
					     buf[offset + 2];
		/* Move to next app */
		offset += 3;
		i++;
		if (i >= I40E_DCBX_MAX_APPS)
			break;
	}

	dcbcfg->numapps = i;
}

/**
 * i40e_parse_ieee_etsrec_tlv
 * @tlv: IEEE 802.1Qaz TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Get the TLV subtype and send it to parsing function
 * based on the subtype value
 **/
static void i40e_parse_ieee_tlv(struct i40e_lldp_org_tlv *tlv,
				struct i40e_dcbx_config *dcbcfg)
{
	u32 ouisubtype;
	u8 subtype;

	ouisubtype = I40E_NTOHL(tlv->ouisubtype);
	subtype = (u8)((ouisubtype & I40E_LLDP_TLV_SUBTYPE_MASK) >>
		       I40E_LLDP_TLV_SUBTYPE_SHIFT);
	switch (subtype) {
	case I40E_IEEE_SUBTYPE_ETS_CFG:
		i40e_parse_ieee_etscfg_tlv(tlv, dcbcfg);
		break;
	case I40E_IEEE_SUBTYPE_ETS_REC:
		i40e_parse_ieee_etsrec_tlv(tlv, dcbcfg);
		break;
	case I40E_IEEE_SUBTYPE_PFC_CFG:
		i40e_parse_ieee_pfccfg_tlv(tlv, dcbcfg);
		break;
	case I40E_IEEE_SUBTYPE_APP_PRI:
		i40e_parse_ieee_app_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}

/**
 * i40e_parse_cee_pgcfg_tlv
 * @tlv: CEE DCBX PG CFG TLV
 * @dcbcfg: Local store to update ETS CFG data
 *
 * Parses CEE DCBX PG CFG TLV
 **/
static void i40e_parse_cee_pgcfg_tlv(struct i40e_cee_feat_tlv *tlv,
				     struct i40e_dcbx_config *dcbcfg)
{
	struct i40e_dcb_ets_config *etscfg;
	u8 *buf = tlv->tlvinfo;
	u16 offset = 0;
	u8 priority;
	int i;

	etscfg = &dcbcfg->etscfg;

	if (tlv->en_will_err & I40E_CEE_FEAT_TLV_WILLING_MASK)
		etscfg->willing = 1;

	etscfg->cbs = 0;
	/* Priority Group Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		priority = (u8)((buf[offset] & I40E_CEE_PGID_PRIO_1_MASK) >>
				 I40E_CEE_PGID_PRIO_1_SHIFT);
		etscfg->prioritytable[i * 2] =  priority;
		priority = (u8)((buf[offset] & I40E_CEE_PGID_PRIO_0_MASK) >>
				 I40E_CEE_PGID_PRIO_0_SHIFT);
		etscfg->prioritytable[i * 2 + 1] = priority;
		offset++;
	}

	/* PG Percentage Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |pg0|pg1|pg2|pg3|pg4|pg5|pg6|pg7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		etscfg->tcbwtable[i] = buf[offset++];

	/* Number of TCs supported (1 octet) */
	etscfg->maxtcs = buf[offset];
}

/**
 * i40e_parse_cee_pfccfg_tlv
 * @tlv: CEE DCBX PFC CFG TLV
 * @dcbcfg: Local store to update PFC CFG data
 *
 * Parses CEE DCBX PFC CFG TLV
 **/
static void i40e_parse_cee_pfccfg_tlv(struct i40e_cee_feat_tlv *tlv,
				      struct i40e_dcbx_config *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;

	if (tlv->en_will_err & I40E_CEE_FEAT_TLV_WILLING_MASK)
		dcbcfg->pfc.willing = 1;

	/* ------------------------
	 * | PFC Enable | PFC TCs |
	 * ------------------------
	 * | 1 octet    | 1 octet |
	 */
	dcbcfg->pfc.pfcenable = buf[0];
	dcbcfg->pfc.pfccap = buf[1];
}

/**
 * i40e_parse_cee_app_tlv
 * @tlv: CEE DCBX APP TLV
 * @dcbcfg: Local store to update APP PRIO data
 *
 * Parses CEE DCBX APP PRIO TLV
 **/
static void i40e_parse_cee_app_tlv(struct i40e_cee_feat_tlv *tlv,
				   struct i40e_dcbx_config *dcbcfg)
{
	u16 length, typelength, offset = 0;
	struct i40e_cee_app_prio *app;
	u8 i;

	typelength = I40E_NTOHS(tlv->hdr.typelen);
	length = (u16)((typelength & I40E_LLDP_TLV_LEN_MASK) >>
		       I40E_LLDP_TLV_LEN_SHIFT);

	dcbcfg->numapps = length / sizeof(*app);
	if (!dcbcfg->numapps)
		return;
	if (dcbcfg->numapps > I40E_DCBX_MAX_APPS)
		dcbcfg->numapps = I40E_DCBX_MAX_APPS;

	for (i = 0; i < dcbcfg->numapps; i++) {
		u8 up, selector;

		app = (struct i40e_cee_app_prio *)(tlv->tlvinfo + offset);
		for (up = 0; up < I40E_MAX_USER_PRIORITY; up++) {
			if (app->prio_map & BIT(up))
				break;
		}
		dcbcfg->app[i].priority = up;

		/* Get Selector from lower 2 bits, and convert to IEEE */
		selector = (app->upper_oui_sel & I40E_CEE_APP_SELECTOR_MASK);
		switch (selector) {
		case I40E_CEE_APP_SEL_ETHTYPE:
			dcbcfg->app[i].selector = I40E_APP_SEL_ETHTYPE;
			break;
		case I40E_CEE_APP_SEL_TCPIP:
			dcbcfg->app[i].selector = I40E_APP_SEL_TCPIP;
			break;
		default:
			/* Keep selector as it is for unknown types */
			dcbcfg->app[i].selector = selector;
		}

		dcbcfg->app[i].protocolid = I40E_NTOHS(app->protocol);
		/* Move to next app */
		offset += sizeof(*app);
	}
}

/**
 * i40e_parse_cee_tlv
 * @tlv: CEE DCBX TLV
 * @dcbcfg: Local store to update DCBX config data
 *
 * Get the TLV subtype and send it to parsing function
 * based on the subtype value
 **/
static void i40e_parse_cee_tlv(struct i40e_lldp_org_tlv *tlv,
			       struct i40e_dcbx_config *dcbcfg)
{
	u16 len, tlvlen, sublen, typelength;
	struct i40e_cee_feat_tlv *sub_tlv;
	u8 subtype, feat_tlv_count = 0;
	u32 ouisubtype;

	ouisubtype = I40E_NTOHL(tlv->ouisubtype);
	subtype = (u8)((ouisubtype & I40E_LLDP_TLV_SUBTYPE_MASK) >>
		       I40E_LLDP_TLV_SUBTYPE_SHIFT);
	/* Return if not CEE DCBX */
	if (subtype != I40E_CEE_DCBX_TYPE)
		return;

	typelength = I40E_NTOHS(tlv->typelength);
	tlvlen = (u16)((typelength & I40E_LLDP_TLV_LEN_MASK) >>
			I40E_LLDP_TLV_LEN_SHIFT);
	len = sizeof(tlv->typelength) + sizeof(ouisubtype) +
	      sizeof(struct i40e_cee_ctrl_tlv);
	/* Return if no CEE DCBX Feature TLVs */
	if (tlvlen <= len)
		return;

	sub_tlv = (struct i40e_cee_feat_tlv *)((char *)tlv + len);
	while (feat_tlv_count < I40E_CEE_MAX_FEAT_TYPE) {
		typelength = I40E_NTOHS(sub_tlv->hdr.typelen);
		sublen = (u16)((typelength &
				I40E_LLDP_TLV_LEN_MASK) >>
				I40E_LLDP_TLV_LEN_SHIFT);
		subtype = (u8)((typelength & I40E_LLDP_TLV_TYPE_MASK) >>
				I40E_LLDP_TLV_TYPE_SHIFT);
		switch (subtype) {
		case I40E_CEE_SUBTYPE_PG_CFG:
			i40e_parse_cee_pgcfg_tlv(sub_tlv, dcbcfg);
			break;
		case I40E_CEE_SUBTYPE_PFC_CFG:
			i40e_parse_cee_pfccfg_tlv(sub_tlv, dcbcfg);
			break;
		case I40E_CEE_SUBTYPE_APP_PRI:
			i40e_parse_cee_app_tlv(sub_tlv, dcbcfg);
			break;
		default:
			return; /* Invalid Sub-type return */
		}
		feat_tlv_count++;
		/* Move to next sub TLV */
		sub_tlv = (struct i40e_cee_feat_tlv *)((char *)sub_tlv +
						sizeof(sub_tlv->hdr.typelen) +
						sublen);
	}
}

/**
 * i40e_parse_org_tlv
 * @tlv: Organization specific TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Currently only IEEE 802.1Qaz TLV is supported, all others
 * will be returned
 **/
static void i40e_parse_org_tlv(struct i40e_lldp_org_tlv *tlv,
			       struct i40e_dcbx_config *dcbcfg)
{
	u32 ouisubtype;
	u32 oui;

	ouisubtype = I40E_NTOHL(tlv->ouisubtype);
	oui = (u32)((ouisubtype & I40E_LLDP_TLV_OUI_MASK) >>
		    I40E_LLDP_TLV_OUI_SHIFT);
	switch (oui) {
	case I40E_IEEE_8021QAZ_OUI:
		i40e_parse_ieee_tlv(tlv, dcbcfg);
		break;
	case I40E_CEE_DCBX_OUI:
		i40e_parse_cee_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}

/**
 * i40e_lldp_to_dcb_config
 * @lldpmib: LLDPDU to be parsed
 * @dcbcfg: store for LLDPDU data
 *
 * Parse DCB configuration from the LLDPDU
 **/
enum i40e_status_code i40e_lldp_to_dcb_config(u8 *lldpmib,
				    struct i40e_dcbx_config *dcbcfg)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_lldp_org_tlv *tlv;
	u16 type;
	u16 length;
	u16 typelength;
	u16 offset = 0;

	if (!lldpmib || !dcbcfg)
		return I40E_ERR_PARAM;

	/* set to the start of LLDPDU */
	lldpmib += I40E_LLDP_MIB_HLEN;
	tlv = (struct i40e_lldp_org_tlv *)lldpmib;
	while (1) {
		typelength = I40E_NTOHS(tlv->typelength);
		type = (u16)((typelength & I40E_LLDP_TLV_TYPE_MASK) >>
			     I40E_LLDP_TLV_TYPE_SHIFT);
		length = (u16)((typelength & I40E_LLDP_TLV_LEN_MASK) >>
			       I40E_LLDP_TLV_LEN_SHIFT);
		offset += sizeof(typelength) + length;

		/* END TLV or beyond LLDPDU size */
		if ((type == I40E_TLV_TYPE_END) || (offset > I40E_LLDPDU_SIZE))
			break;

		switch (type) {
		case I40E_TLV_TYPE_ORG:
			i40e_parse_org_tlv(tlv, dcbcfg);
			break;
		default:
			break;
		}

		/* Move to next TLV */
		tlv = (struct i40e_lldp_org_tlv *)((char *)tlv +
						    sizeof(tlv->typelength) +
						    length);
	}

	return ret;
}

/**
 * i40e_aq_get_dcb_config
 * @hw: pointer to the hw struct
 * @mib_type: mib type for the query
 * @bridgetype: bridge type for the query (remote)
 * @dcbcfg: store for LLDPDU data
 *
 * Query DCB configuration from the Firmware
 **/
enum i40e_status_code i40e_aq_get_dcb_config(struct i40e_hw *hw, u8 mib_type,
				   u8 bridgetype,
				   struct i40e_dcbx_config *dcbcfg)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_virt_mem mem;
	u8 *lldpmib;

	/* Allocate the LLDPDU */
	ret = i40e_allocate_virt_mem(hw, &mem, I40E_LLDPDU_SIZE);
	if (ret)
		return ret;

	lldpmib = (u8 *)mem.va;
	ret = i40e_aq_get_lldp_mib(hw, bridgetype, mib_type,
				   (void *)lldpmib, I40E_LLDPDU_SIZE,
				   NULL, NULL, NULL);
	if (ret)
		goto free_mem;

	/* Parse LLDP MIB to get dcb configuration */
	ret = i40e_lldp_to_dcb_config(lldpmib, dcbcfg);

free_mem:
	i40e_free_virt_mem(hw, &mem);
	return ret;
}

/**
 * i40e_cee_to_dcb_v1_config
 * @cee_cfg: pointer to CEE v1 response configuration struct
 * @dcbcfg: DCB configuration struct
 *
 * Convert CEE v1 configuration from firmware to DCB configuration
 **/
static void i40e_cee_to_dcb_v1_config(
			struct i40e_aqc_get_cee_dcb_cfg_v1_resp *cee_cfg,
			struct i40e_dcbx_config *dcbcfg)
{
	u16 status, tlv_status = LE16_TO_CPU(cee_cfg->tlv_status);
	u16 app_prio = LE16_TO_CPU(cee_cfg->oper_app_prio);
	u8 i, tc, err;

	/* CEE PG data to ETS config */
	dcbcfg->etscfg.maxtcs = cee_cfg->oper_num_tc;

	/* Note that the FW creates the oper_prio_tc nibbles reversed
	 * from those in the CEE Priority Group sub-TLV.
	 */
	for (i = 0; i < 4; i++) {
		tc = (u8)((cee_cfg->oper_prio_tc[i] &
			 I40E_CEE_PGID_PRIO_0_MASK) >>
			 I40E_CEE_PGID_PRIO_0_SHIFT);
		dcbcfg->etscfg.prioritytable[i*2] =  tc;
		tc = (u8)((cee_cfg->oper_prio_tc[i] &
			 I40E_CEE_PGID_PRIO_1_MASK) >>
			 I40E_CEE_PGID_PRIO_1_SHIFT);
		dcbcfg->etscfg.prioritytable[i*2 + 1] = tc;
	}

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		dcbcfg->etscfg.tcbwtable[i] = cee_cfg->oper_tc_bw[i];

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (dcbcfg->etscfg.prioritytable[i] == I40E_CEE_PGID_STRICT) {
			/* Map it to next empty TC */
			dcbcfg->etscfg.prioritytable[i] =
						cee_cfg->oper_num_tc - 1;
			dcbcfg->etscfg.tsatable[i] = I40E_IEEE_TSA_STRICT;
		} else {
			dcbcfg->etscfg.tsatable[i] = I40E_IEEE_TSA_ETS;
		}
	}

	/* CEE PFC data to ETS config */
	dcbcfg->pfc.pfcenable = cee_cfg->oper_pfc_en;
	dcbcfg->pfc.pfccap = I40E_MAX_TRAFFIC_CLASS;

	status = (tlv_status & I40E_AQC_CEE_APP_STATUS_MASK) >>
		  I40E_AQC_CEE_APP_STATUS_SHIFT;
	err = (status & I40E_TLV_STATUS_ERR) ? 1 : 0;
	/* Add APPs if Error is False */
	if (!err) {
		/* CEE operating configuration supports FCoE/iSCSI/FIP only */
		dcbcfg->numapps = I40E_CEE_OPER_MAX_APPS;

		/* FCoE APP */
		dcbcfg->app[0].priority =
			(app_prio & I40E_AQC_CEE_APP_FCOE_MASK) >>
			 I40E_AQC_CEE_APP_FCOE_SHIFT;
		dcbcfg->app[0].selector = I40E_APP_SEL_ETHTYPE;
		dcbcfg->app[0].protocolid = I40E_APP_PROTOID_FCOE;

		/* iSCSI APP */
		dcbcfg->app[1].priority =
			(app_prio & I40E_AQC_CEE_APP_ISCSI_MASK) >>
			 I40E_AQC_CEE_APP_ISCSI_SHIFT;
		dcbcfg->app[1].selector = I40E_APP_SEL_TCPIP;
		dcbcfg->app[1].protocolid = I40E_APP_PROTOID_ISCSI;

		/* FIP APP */
		dcbcfg->app[2].priority =
			(app_prio & I40E_AQC_CEE_APP_FIP_MASK) >>
			 I40E_AQC_CEE_APP_FIP_SHIFT;
		dcbcfg->app[2].selector = I40E_APP_SEL_ETHTYPE;
		dcbcfg->app[2].protocolid = I40E_APP_PROTOID_FIP;
	}
}

/**
 * i40e_cee_to_dcb_config
 * @cee_cfg: pointer to CEE configuration struct
 * @dcbcfg: DCB configuration struct
 *
 * Convert CEE configuration from firmware to DCB configuration
 **/
static void i40e_cee_to_dcb_config(
				struct i40e_aqc_get_cee_dcb_cfg_resp *cee_cfg,
				struct i40e_dcbx_config *dcbcfg)
{
	u32 status, tlv_status = LE32_TO_CPU(cee_cfg->tlv_status);
	u16 app_prio = LE16_TO_CPU(cee_cfg->oper_app_prio);
	u8 i, tc, err, sync, oper;

	/* CEE PG data to ETS config */
	dcbcfg->etscfg.maxtcs = cee_cfg->oper_num_tc;

	/* Note that the FW creates the oper_prio_tc nibbles reversed
	 * from those in the CEE Priority Group sub-TLV.
	 */
	for (i = 0; i < 4; i++) {
		tc = (u8)((cee_cfg->oper_prio_tc[i] &
			 I40E_CEE_PGID_PRIO_0_MASK) >>
			 I40E_CEE_PGID_PRIO_0_SHIFT);
		dcbcfg->etscfg.prioritytable[i*2] =  tc;
		tc = (u8)((cee_cfg->oper_prio_tc[i] &
			 I40E_CEE_PGID_PRIO_1_MASK) >>
			 I40E_CEE_PGID_PRIO_1_SHIFT);
		dcbcfg->etscfg.prioritytable[i*2 + 1] = tc;
	}

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		dcbcfg->etscfg.tcbwtable[i] = cee_cfg->oper_tc_bw[i];

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (dcbcfg->etscfg.prioritytable[i] == I40E_CEE_PGID_STRICT) {
			/* Map it to next empty TC */
			dcbcfg->etscfg.prioritytable[i] =
						cee_cfg->oper_num_tc - 1;
			dcbcfg->etscfg.tsatable[i] = I40E_IEEE_TSA_STRICT;
		} else {
			dcbcfg->etscfg.tsatable[i] = I40E_IEEE_TSA_ETS;
		}
	}

	/* CEE PFC data to ETS config */
	dcbcfg->pfc.pfcenable = cee_cfg->oper_pfc_en;
	dcbcfg->pfc.pfccap = I40E_MAX_TRAFFIC_CLASS;

	i = 0;
	status = (tlv_status & I40E_AQC_CEE_FCOE_STATUS_MASK) >>
		  I40E_AQC_CEE_FCOE_STATUS_SHIFT;
	err = (status & I40E_TLV_STATUS_ERR) ? 1 : 0;
	sync = (status & I40E_TLV_STATUS_SYNC) ? 1 : 0;
	oper = (status & I40E_TLV_STATUS_OPER) ? 1 : 0;
	/* Add FCoE APP if Error is False and Oper/Sync is True */
	if (!err && sync && oper) {
		/* FCoE APP */
		dcbcfg->app[i].priority =
			(app_prio & I40E_AQC_CEE_APP_FCOE_MASK) >>
			 I40E_AQC_CEE_APP_FCOE_SHIFT;
		dcbcfg->app[i].selector = I40E_APP_SEL_ETHTYPE;
		dcbcfg->app[i].protocolid = I40E_APP_PROTOID_FCOE;
		i++;
	}

	status = (tlv_status & I40E_AQC_CEE_ISCSI_STATUS_MASK) >>
		  I40E_AQC_CEE_ISCSI_STATUS_SHIFT;
	err = (status & I40E_TLV_STATUS_ERR) ? 1 : 0;
	sync = (status & I40E_TLV_STATUS_SYNC) ? 1 : 0;
	oper = (status & I40E_TLV_STATUS_OPER) ? 1 : 0;
	/* Add iSCSI APP if Error is False and Oper/Sync is True */
	if (!err && sync && oper) {
		/* iSCSI APP */
		dcbcfg->app[i].priority =
			(app_prio & I40E_AQC_CEE_APP_ISCSI_MASK) >>
			 I40E_AQC_CEE_APP_ISCSI_SHIFT;
		dcbcfg->app[i].selector = I40E_APP_SEL_TCPIP;
		dcbcfg->app[i].protocolid = I40E_APP_PROTOID_ISCSI;
		i++;
	}

	status = (tlv_status & I40E_AQC_CEE_FIP_STATUS_MASK) >>
		  I40E_AQC_CEE_FIP_STATUS_SHIFT;
	err = (status & I40E_TLV_STATUS_ERR) ? 1 : 0;
	sync = (status & I40E_TLV_STATUS_SYNC) ? 1 : 0;
	oper = (status & I40E_TLV_STATUS_OPER) ? 1 : 0;
	/* Add FIP APP if Error is False and Oper/Sync is True */
	if (!err && sync && oper) {
		/* FIP APP */
		dcbcfg->app[i].priority =
			(app_prio & I40E_AQC_CEE_APP_FIP_MASK) >>
			 I40E_AQC_CEE_APP_FIP_SHIFT;
		dcbcfg->app[i].selector = I40E_APP_SEL_ETHTYPE;
		dcbcfg->app[i].protocolid = I40E_APP_PROTOID_FIP;
		i++;
	}
	dcbcfg->numapps = i;
}

/**
 * i40e_get_ieee_dcb_config
 * @hw: pointer to the hw struct
 *
 * Get IEEE mode DCB configuration from the Firmware
 **/
static enum i40e_status_code i40e_get_ieee_dcb_config(struct i40e_hw *hw)
{
	enum i40e_status_code ret = I40E_SUCCESS;

	/* IEEE mode */
	hw->local_dcbx_config.dcbx_mode = I40E_DCBX_MODE_IEEE;
	/* Get Local DCB Config */
	ret = i40e_aq_get_dcb_config(hw, I40E_AQ_LLDP_MIB_LOCAL, 0,
				     &hw->local_dcbx_config);
	if (ret)
		goto out;

	/* Get Remote DCB Config */
	ret = i40e_aq_get_dcb_config(hw, I40E_AQ_LLDP_MIB_REMOTE,
				     I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE,
				     &hw->remote_dcbx_config);
	/* Don't treat ENOENT as an error for Remote MIBs */
	if (hw->aq.asq_last_status == I40E_AQ_RC_ENOENT)
		ret = I40E_SUCCESS;

out:
	return ret;
}

/**
 * i40e_get_dcb_config
 * @hw: pointer to the hw struct
 *
 * Get DCB configuration from the Firmware
 **/
enum i40e_status_code i40e_get_dcb_config(struct i40e_hw *hw)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_aqc_get_cee_dcb_cfg_resp cee_cfg;
	struct i40e_aqc_get_cee_dcb_cfg_v1_resp cee_v1_cfg;

	/* If Firmware version < v4.33 on X710/XL710, IEEE only */
	if ((hw->mac.type == I40E_MAC_XL710) &&
	    (((hw->aq.fw_maj_ver == 4) && (hw->aq.fw_min_ver < 33)) ||
	      (hw->aq.fw_maj_ver < 4)))
		return i40e_get_ieee_dcb_config(hw);

	/* If Firmware version == v4.33 on X710/XL710, use old CEE struct */
	if ((hw->mac.type == I40E_MAC_XL710) &&
	    ((hw->aq.fw_maj_ver == 4) && (hw->aq.fw_min_ver == 33))) {
		ret = i40e_aq_get_cee_dcb_config(hw, &cee_v1_cfg,
						 sizeof(cee_v1_cfg), NULL);
		if (ret == I40E_SUCCESS) {
			/* CEE mode */
			hw->local_dcbx_config.dcbx_mode = I40E_DCBX_MODE_CEE;
			hw->local_dcbx_config.tlv_status =
					LE16_TO_CPU(cee_v1_cfg.tlv_status);
			i40e_cee_to_dcb_v1_config(&cee_v1_cfg,
						  &hw->local_dcbx_config);
		}
	} else {
		ret = i40e_aq_get_cee_dcb_config(hw, &cee_cfg,
						 sizeof(cee_cfg), NULL);
		if (ret == I40E_SUCCESS) {
			/* CEE mode */
			hw->local_dcbx_config.dcbx_mode = I40E_DCBX_MODE_CEE;
			hw->local_dcbx_config.tlv_status =
					LE32_TO_CPU(cee_cfg.tlv_status);
			i40e_cee_to_dcb_config(&cee_cfg,
					       &hw->local_dcbx_config);
		}
	}

	/* CEE mode not enabled try querying IEEE data */
	if (hw->aq.asq_last_status == I40E_AQ_RC_ENOENT)
		return i40e_get_ieee_dcb_config(hw);

	if (ret != I40E_SUCCESS)
		goto out;

	/* Get CEE DCB Desired Config */
	ret = i40e_aq_get_dcb_config(hw, I40E_AQ_LLDP_MIB_LOCAL, 0,
				     &hw->desired_dcbx_config);
	if (ret)
		goto out;

	/* Get Remote DCB Config */
	ret = i40e_aq_get_dcb_config(hw, I40E_AQ_LLDP_MIB_REMOTE,
			     I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE,
			     &hw->remote_dcbx_config);
	/* Don't treat ENOENT as an error for Remote MIBs */
	if (hw->aq.asq_last_status == I40E_AQ_RC_ENOENT)
		ret = I40E_SUCCESS;

out:
	return ret;
}

/**
 * i40e_init_dcb
 * @hw: pointer to the hw struct
 *
 * Update DCB configuration from the Firmware
 **/
enum i40e_status_code i40e_init_dcb(struct i40e_hw *hw)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_lldp_variables lldp_cfg;
	u8 adminstatus = 0;

	if (!hw->func_caps.dcb)
		return ret;

	/* Read LLDP NVM area */
	ret = i40e_read_lldp_cfg(hw, &lldp_cfg);
	if (ret)
		return ret;

	/* Get the LLDP AdminStatus for the current port */
	adminstatus = lldp_cfg.adminstatus >> (hw->port * 4);
	adminstatus &= 0xF;

	/* LLDP agent disabled */
	if (!adminstatus) {
		hw->dcbx_status = I40E_DCBX_STATUS_DISABLED;
		return ret;
	}

	/* Get DCBX status */
	ret = i40e_get_dcbx_status(hw, &hw->dcbx_status);
	if (ret)
		return ret;

	/* Check the DCBX Status */
	switch (hw->dcbx_status) {
	case I40E_DCBX_STATUS_DONE:
	case I40E_DCBX_STATUS_IN_PROGRESS:
		/* Get current DCBX configuration */
		ret = i40e_get_dcb_config(hw);
		if (ret)
			return ret;
		break;
	case I40E_DCBX_STATUS_DISABLED:
		return ret;
	case I40E_DCBX_STATUS_NOT_STARTED:
	case I40E_DCBX_STATUS_MULTIPLE_PEERS:
	default:
		break;
	}

	/* Configure the LLDP MIB change event */
	ret = i40e_aq_cfg_lldp_mib_change_event(hw, TRUE, NULL);
	if (ret)
		return ret;

	return ret;
}

/**
 * i40e_add_ieee_ets_tlv - Prepare ETS TLV in IEEE format
 * @tlv: Fill the ETS config data in IEEE format
 * @dcbcfg: Local store which holds the DCB Config
 *
 * Prepare IEEE 802.1Qaz ETS CFG TLV
 **/
static void i40e_add_ieee_ets_tlv(struct i40e_lldp_org_tlv *tlv,
				  struct i40e_dcbx_config *dcbcfg)
{
	u8 priority0, priority1, maxtcwilling = 0;
	struct i40e_dcb_ets_config *etscfg;
	u16 offset = 0, typelength, i;
	u8 *buf = tlv->tlvinfo;
	u32 ouisubtype;

	typelength = (u16)((I40E_TLV_TYPE_ORG << I40E_LLDP_TLV_TYPE_SHIFT) |
			I40E_IEEE_ETS_TLV_LENGTH);
	tlv->typelength = I40E_HTONS(typelength);

	ouisubtype = (u32)((I40E_IEEE_8021QAZ_OUI << I40E_LLDP_TLV_OUI_SHIFT) |
			I40E_IEEE_SUBTYPE_ETS_CFG);
	tlv->ouisubtype = I40E_HTONL(ouisubtype);

	/* First Octet post subtype
	 * --------------------------
	 * |will-|CBS  | Re-  | Max |
	 * |ing  |     |served| TCs |
	 * --------------------------
	 * |1bit | 1bit|3 bits|3bits|
	 */
	etscfg = &dcbcfg->etscfg;
	if (etscfg->willing)
		maxtcwilling = BIT(I40E_IEEE_ETS_WILLING_SHIFT);
	maxtcwilling |= etscfg->maxtcs & I40E_IEEE_ETS_MAXTC_MASK;
	buf[offset] = maxtcwilling;

	/* Move offset to Priority Assignment Table */
	offset++;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		priority0 = etscfg->prioritytable[i * 2] & 0xF;
		priority1 = etscfg->prioritytable[i * 2 + 1] & 0xF;
		buf[offset] = (priority0 << I40E_IEEE_ETS_PRIO_1_SHIFT) |
				priority1;
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		buf[offset++] = etscfg->tcbwtable[i];

	/* TSA Assignment Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		buf[offset++] = etscfg->tsatable[i];
}

/**
 * i40e_add_ieee_etsrec_tlv - Prepare ETS Recommended TLV in IEEE format
 * @tlv: Fill ETS Recommended TLV in IEEE format
 * @dcbcfg: Local store which holds the DCB Config
 *
 * Prepare IEEE 802.1Qaz ETS REC TLV
 **/
static void i40e_add_ieee_etsrec_tlv(struct i40e_lldp_org_tlv *tlv,
				     struct i40e_dcbx_config *dcbcfg)
{
	struct i40e_dcb_ets_config *etsrec;
	u16 offset = 0, typelength, i;
	u8 priority0, priority1;
	u8 *buf = tlv->tlvinfo;
	u32 ouisubtype;

	typelength = (u16)((I40E_TLV_TYPE_ORG << I40E_LLDP_TLV_TYPE_SHIFT) |
			I40E_IEEE_ETS_TLV_LENGTH);
	tlv->typelength = I40E_HTONS(typelength);

	ouisubtype = (u32)((I40E_IEEE_8021QAZ_OUI << I40E_LLDP_TLV_OUI_SHIFT) |
			I40E_IEEE_SUBTYPE_ETS_REC);
	tlv->ouisubtype = I40E_HTONL(ouisubtype);

	etsrec = &dcbcfg->etsrec;
	/* First Octet is reserved */
	/* Move offset to Priority Assignment Table */
	offset++;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		priority0 = etsrec->prioritytable[i * 2] & 0xF;
		priority1 = etsrec->prioritytable[i * 2 + 1] & 0xF;
		buf[offset] = (priority0 << I40E_IEEE_ETS_PRIO_1_SHIFT) |
				priority1;
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		buf[offset++] = etsrec->tcbwtable[i];

	/* TSA Assignment Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		buf[offset++] = etsrec->tsatable[i];
}

 /**
 * i40e_add_ieee_pfc_tlv - Prepare PFC TLV in IEEE format
 * @tlv: Fill PFC TLV in IEEE format
 * @dcbcfg: Local store to get PFC CFG data
 *
 * Prepare IEEE 802.1Qaz PFC CFG TLV
 **/
static void i40e_add_ieee_pfc_tlv(struct i40e_lldp_org_tlv *tlv,
				  struct i40e_dcbx_config *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;
	u32 ouisubtype;
	u16 typelength;

	typelength = (u16)((I40E_TLV_TYPE_ORG << I40E_LLDP_TLV_TYPE_SHIFT) |
			I40E_IEEE_PFC_TLV_LENGTH);
	tlv->typelength = I40E_HTONS(typelength);

	ouisubtype = (u32)((I40E_IEEE_8021QAZ_OUI << I40E_LLDP_TLV_OUI_SHIFT) |
			I40E_IEEE_SUBTYPE_PFC_CFG);
	tlv->ouisubtype = I40E_HTONL(ouisubtype);

	/* ----------------------------------------
	 * |will-|MBC  | Re-  | PFC |  PFC Enable  |
	 * |ing  |     |served| cap |              |
	 * -----------------------------------------
	 * |1bit | 1bit|2 bits|4bits| 1 octet      |
	 */
	if (dcbcfg->pfc.willing)
		buf[0] = BIT(I40E_IEEE_PFC_WILLING_SHIFT);

	if (dcbcfg->pfc.mbc)
		buf[0] |= BIT(I40E_IEEE_PFC_MBC_SHIFT);

	buf[0] |= dcbcfg->pfc.pfccap & 0xF;
	buf[1] = dcbcfg->pfc.pfcenable;
}

/**
 * i40e_add_ieee_app_pri_tlv -  Prepare APP TLV in IEEE format
 * @tlv: Fill APP TLV in IEEE format
 * @dcbcfg: Local store to get APP CFG data
 *
 * Prepare IEEE 802.1Qaz APP CFG TLV
 **/
static void i40e_add_ieee_app_pri_tlv(struct i40e_lldp_org_tlv *tlv,
				      struct i40e_dcbx_config *dcbcfg)
{
	u16 typelength, length, offset = 0;
	u8 priority, selector, i = 0;
	u8 *buf = tlv->tlvinfo;
	u32 ouisubtype;

	/* No APP TLVs then just return */
	if (dcbcfg->numapps == 0)
		return;
	ouisubtype = (u32)((I40E_IEEE_8021QAZ_OUI << I40E_LLDP_TLV_OUI_SHIFT) |
			I40E_IEEE_SUBTYPE_APP_PRI);
	tlv->ouisubtype = I40E_HTONL(ouisubtype);

	/* Move offset to App Priority Table */
	offset++;
	/* Application Priority Table (3 octets)
	 * Octets:|         1          |    2    |    3    |
	 *        -----------------------------------------
	 *        |Priority|Rsrvd| Sel |    Protocol ID    |
	 *        -----------------------------------------
	 *   Bits:|23    21|20 19|18 16|15                0|
	 *        -----------------------------------------
	 */
	while (i < dcbcfg->numapps) {
		priority = dcbcfg->app[i].priority & 0x7;
		selector = dcbcfg->app[i].selector & 0x7;
		buf[offset] = (priority << I40E_IEEE_APP_PRIO_SHIFT) | selector;
		buf[offset + 1] = (dcbcfg->app[i].protocolid >> 0x8) & 0xFF;
		buf[offset + 2] =  dcbcfg->app[i].protocolid & 0xFF;
		/* Move to next app */
		offset += 3;
		i++;
		if (i >= I40E_DCBX_MAX_APPS)
			break;
	}
	/* length includes size of ouisubtype + 1 reserved + 3*numapps */
	length = sizeof(tlv->ouisubtype) + 1 + (i*3);
	typelength = (u16)((I40E_TLV_TYPE_ORG << I40E_LLDP_TLV_TYPE_SHIFT) |
		(length & 0x1FF));
	tlv->typelength = I40E_HTONS(typelength);
}

 /**
 * i40e_add_dcb_tlv - Add all IEEE TLVs
 * @tlv: pointer to org tlv
 *
 * add tlv information
 **/
static void i40e_add_dcb_tlv(struct i40e_lldp_org_tlv *tlv,
			     struct i40e_dcbx_config *dcbcfg,
			     u16 tlvid)
{
	switch (tlvid) {
	case I40E_IEEE_TLV_ID_ETS_CFG:
		i40e_add_ieee_ets_tlv(tlv, dcbcfg);
		break;
	case I40E_IEEE_TLV_ID_ETS_REC:
		i40e_add_ieee_etsrec_tlv(tlv, dcbcfg);
		break;
	case I40E_IEEE_TLV_ID_PFC_CFG:
		i40e_add_ieee_pfc_tlv(tlv, dcbcfg);
		break;
	case I40E_IEEE_TLV_ID_APP_PRI:
		i40e_add_ieee_app_pri_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}

 /**
 * i40e_set_dcb_config - Set the local LLDP MIB to FW
 * @hw: pointer to the hw struct
 *
 * Set DCB configuration to the Firmware
 **/
enum i40e_status_code i40e_set_dcb_config(struct i40e_hw *hw)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_dcbx_config *dcbcfg;
	struct i40e_virt_mem mem;
	u8 mib_type, *lldpmib;
	u16 miblen;

	/* update the hw local config */
	dcbcfg = &hw->local_dcbx_config;
	/* Allocate the LLDPDU */
	ret = i40e_allocate_virt_mem(hw, &mem, I40E_LLDPDU_SIZE);
	if (ret)
		return ret;

	mib_type = SET_LOCAL_MIB_AC_TYPE_LOCAL_MIB;
	if (dcbcfg->app_mode == I40E_DCBX_APPS_NON_WILLING) {
		mib_type |= SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS <<
			    SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_SHIFT;
	}
	lldpmib = (u8 *)mem.va;
	ret = i40e_dcb_config_to_lldp(lldpmib, &miblen, dcbcfg);
	ret = i40e_aq_set_lldp_mib(hw, mib_type, (void *)lldpmib, miblen, NULL);

	i40e_free_virt_mem(hw, &mem);
	return ret;
}

/**
 * i40e_dcb_config_to_lldp - Convert Dcbconfig to MIB format
 * @hw: pointer to the hw struct
 * @dcbcfg: store for LLDPDU data
 *
 * send DCB configuration to FW
 **/
enum i40e_status_code i40e_dcb_config_to_lldp(u8 *lldpmib, u16 *miblen,
					      struct i40e_dcbx_config *dcbcfg)
{
	u16 length, offset = 0, tlvid = I40E_TLV_ID_START;
	enum i40e_status_code ret = I40E_SUCCESS;
	struct i40e_lldp_org_tlv *tlv;
	u16 typelength;

	tlv = (struct i40e_lldp_org_tlv *)lldpmib;
	while (1) {
		i40e_add_dcb_tlv(tlv, dcbcfg, tlvid++);
		typelength = I40E_NTOHS(tlv->typelength);
		length = (u16)((typelength & I40E_LLDP_TLV_LEN_MASK) >>
				I40E_LLDP_TLV_LEN_SHIFT);
		if (length)
			offset += length + 2;
		/* END TLV or beyond LLDPDU size */
		if ((tlvid >= I40E_TLV_ID_END_OF_LLDPPDU) ||
		    (offset > I40E_LLDPDU_SIZE))
			break;
		/* Move to next TLV */
		if (length)
			tlv = (struct i40e_lldp_org_tlv *)((char *)tlv +
			      sizeof(tlv->typelength) + length);
	}
	*miblen = offset;
	return ret;
}


/**
 * _i40e_read_lldp_cfg - generic read of LLDP Configuration data from NVM
 * @hw: pointer to the HW structure
 * @lldp_cfg: pointer to hold lldp configuration variables
 * @module: address of the module pointer
 * @word_offset: offset of LLDP configuration
 *
 * Reads the LLDP configuration data from NVM using passed addresses
 **/
static enum i40e_status_code _i40e_read_lldp_cfg(struct i40e_hw *hw,
					  struct i40e_lldp_variables *lldp_cfg,
					  u8 module, u32 word_offset)
{
	u32 address, offset = (2 * word_offset);
	enum i40e_status_code ret;
	__le16 raw_mem;
	u16 mem;

	ret = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret != I40E_SUCCESS)
		return ret;

	ret = i40e_aq_read_nvm(hw, 0x0, module * 2, sizeof(raw_mem), &raw_mem,
			       TRUE, NULL);
	i40e_release_nvm(hw);
	if (ret != I40E_SUCCESS)
		return ret;

	mem = LE16_TO_CPU(raw_mem);
	/* Check if this pointer needs to be read in word size or 4K sector
	 * units.
	 */
	if (mem & I40E_PTR_TYPE)
		address = (0x7FFF & mem) * 4096;
	else
		address = (0x7FFF & mem) * 2;

	ret = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret != I40E_SUCCESS)
		goto err_lldp_cfg;

	ret = i40e_aq_read_nvm(hw, module, offset, sizeof(raw_mem), &raw_mem,
			       TRUE, NULL);
	i40e_release_nvm(hw);
	if (ret != I40E_SUCCESS)
		return ret;

	mem = LE16_TO_CPU(raw_mem);
	offset = mem + word_offset;
	offset *= 2;

	ret = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret != I40E_SUCCESS)
		goto err_lldp_cfg;

	ret = i40e_aq_read_nvm(hw, 0, address + offset,
			       sizeof(struct i40e_lldp_variables), lldp_cfg,
			       TRUE, NULL);
	i40e_release_nvm(hw);

err_lldp_cfg:
	return ret;
}

/**
 * i40e_read_lldp_cfg - read LLDP Configuration data from NVM
 * @hw: pointer to the HW structure
 * @lldp_cfg: pointer to hold lldp configuration variables
 *
 * Reads the LLDP configuration data from NVM
 **/
enum i40e_status_code i40e_read_lldp_cfg(struct i40e_hw *hw,
					 struct i40e_lldp_variables *lldp_cfg)
{
	enum i40e_status_code ret = I40E_SUCCESS;
	u32 mem;

	if (!lldp_cfg)
		return I40E_ERR_PARAM;

	ret = i40e_acquire_nvm(hw, I40E_RESOURCE_READ);
	if (ret != I40E_SUCCESS)
		return ret;

	ret = i40e_aq_read_nvm(hw, I40E_SR_NVM_CONTROL_WORD, 0, sizeof(mem),
			       &mem, TRUE, NULL);
	i40e_release_nvm(hw);
	if (ret != I40E_SUCCESS)
		return ret;

	/* Read a bit that holds information whether we are running flat or
	 * structured NVM image. Flat image has LLDP configuration in shadow
	 * ram, so there is a need to pass different addresses for both cases.
	 */
	if (mem & I40E_SR_NVM_MAP_STRUCTURE_TYPE) {
		/* Flat NVM case */
		ret = _i40e_read_lldp_cfg(hw, lldp_cfg, I40E_SR_EMP_MODULE_PTR,
					  I40E_SR_LLDP_CFG_PTR);
	} else {
		/* Good old structured NVM image */
		ret = _i40e_read_lldp_cfg(hw, lldp_cfg, I40E_EMP_MODULE_PTR,
					  I40E_NVM_LLDP_CFG_PTR);
	}

	return ret;
}
