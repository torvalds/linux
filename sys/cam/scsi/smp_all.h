/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test/sys/cam/scsi/smp_all.h#4 $
 * $FreeBSD$
 */

/*
 * Serial Management Protocol definitions.
 */

#ifndef	_SCSI_SMP_ALL_H
#define	_SCSI_SMP_ALL_H	1

#define	SMP_FRAME_TYPE_REQUEST	0x40
#define	SMP_FRAME_TYPE_RESPONSE	0x41
#define	SMP_WORD_LEN		4
#define	SMP_CRC_LEN		4

/*
 * SMP Functions (current as of SPL Revision 7)
 */
/* 0x00 to 0x7f: SMP input functions */
/* 0x00 to 0x0f: General SMP input functions */
#define	SMP_FUNC_REPORT_GENERAL		0x00
#define	SMP_FUNC_REPORT_MANUF_INFO	0x01
#define	SMP_FUNC_REPORT_SC_STATUS	0x03
#define	SMP_FUNC_REPORT_ZONE_PERM_TBL	0x04
#define	SMP_FUNC_REPORT_ZONE_MAN_PWD	0x05
#define	SMP_FUNC_REPORT_BROADCAST	0x06

/* 0x10 to 0x1f: Phy-based SMP input functions */
#define	SMP_FUNC_DISCOVER		0x10
#define	SMP_FUNC_REPORT_PHY_ERR_LOG	0x11
#define	SMP_FUNC_REPORT_PHY_SATA	0x12
#define	SMP_FUNC_REPORT_ROUTE_INFO	0x13
#define	SMP_FUNC_REPORT_PHY_EVENT	0x14

/* 0x20 to 0x2f: Descriptor list-based SMP input functions */
#define	SMP_FUNC_DISCOVER_LIST		0x20
#define	SMP_FUNC_REPORT_PHY_EVENT_LIST	0x21
#define	SMP_FUNC_REPORT_EXP_RTL		0x22

/* 0x30 to 0x3f: Reserved for SMP input functions */
/* 0x40 to 0x7f: Vendor specific */

/* 0x80 to 0xff: SMP output functions */
/* 0x80 to 0x8f: General SMP output functions */
#define	SMP_FUNC_CONFIG_GENERAL		0x80
#define	SMP_FUNC_ENABLE_DISABLE_ZONING	0x81
#define	SMP_FUNC_ZONED_BROADCAST	0x85
#define	SMP_FUNC_ZONE_LOCK		0x86
#define	SMP_FUNC_ZONE_ACTIVATE		0x87
#define	SMP_FUNC_ZONE_UNLOCK		0x88
#define	SMP_FUNC_CONFIG_ZM_PWD		0x89
#define	SMP_FUNC_CONFIG_ZONE_PHY_INFO	0x8a
#define	SMP_FUNC_CONFIG_ZONE_PERM_TBL	0x8b

/* 0x90 to 0x9f: Phy-based SMP output functions */
#define	SMP_FUNC_CONFIG_ROUTE_INFO	0x90
#define	SMP_FUNC_PHY_CONTROL		0x91
#define	SMP_FUNC_PHY_TEST_FUNC		0x92
#define	SMP_FUNC_CONFIG_PHY_EVENT	0x93

/* 0xa0 to 0xbf: Reserved for SMP output functions */
/* 0xc0 to 0xff: Vendor specific */

/*
 * Function Results (current as of SPL Revision 7)
 */
#define	SMP_FR_ACCEPTED			0x00
#define	SMP_FR_UNKNOWN_FUNC		0x01
#define	SMP_FR_FUNCTION_FAILED		0x02
#define	SMP_FR_INVALID_REQ_FRAME_LEN	0x03
#define	SMP_FR_INVALID_EXP_CHG_CNT	0x04
#define	SMP_FR_BUSY			0x05
#define	SMP_FR_INCOMPLETE_DESC_LIST	0x06
#define	SMP_FR_PHY_DOES_NOT_EXIST	0x10
#define	SMP_FR_INDEX_DOES_NOT_EXIST	0x11
#define	SMP_FR_PHY_DOES_NOT_SUP_SATA	0x12
#define	SMP_FR_UNKNOWN_PHY_OP		0x13
#define	SMP_FR_UNKNOWN_PHY_TEST_FUNC	0x14
#define	SMP_FR_PHY_TEST_FUNC_INPROG	0x15
#define	SMP_FR_PHY_VACANT		0x16
#define	SMP_FR_UNKNOWN_PHY_EVENT_SRC	0x17
#define	SMP_FR_UNKNOWN_DESC_TYPE	0x18
#define	SMP_FR_UNKNOWN_PHY_FILTER	0x19
#define	SMP_FR_AFFILIATION_VIOLATION	0x1a
#define	SMP_FR_SMP_ZONE_VIOLATION	0x20
#define	SMP_FR_NO_MGMT_ACCESS_RIGHTS	0x21
#define	SMP_FR_UNKNOWN_ED_ZONING_VAL	0x22
#define	SMP_FR_ZONE_LOCK_VIOLATION	0x23
#define	SMP_FR_NOT_ACTIVATED		0x24
#define	SMP_FR_ZG_OUT_OF_RANGE		0x25
#define	SMP_FR_NO_PHYS_PRESENCE		0x26
#define	SMP_FR_SAVING_NOT_SUP		0x27
#define	SMP_FR_SRC_ZONE_DNE		0x28
#define	SMP_FR_DISABLED_PWD_NOT_SUP	0x29

/*
 * REPORT GENERAL request and response, current as of SPL Revision 7.
 */
struct smp_report_general_request
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t	response_len;
	uint8_t	request_len;
	uint8_t	crc[4];
};

struct smp_report_general_response
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t	function_result;
	uint8_t	response_len;
#define	SMP_RG_RESPONSE_LEN		0x11
	uint8_t	expander_change_count[2];
	uint8_t	expander_route_indexes[2];
	uint8_t	long_response;
#define	SMP_RG_LONG_RESPONSE		0x80
	uint8_t	num_phys;
	uint8_t	config_bits0;
#define	SMP_RG_TABLE_TO_TABLE_SUP	0x80
#define	SMP_RG_ZONE_CONFIGURING		0x40
#define	SMP_RG_SELF_CONFIGURING		0x20
#define	SMP_RG_STP_CONTINUE_AWT		0x10
#define	SMP_RG_OPEN_REJECT_RETRY_SUP	0x08
#define	SMP_RG_CONFIGURES_OTHERS	0x04
#define	SMP_RG_CONFIGURING		0x02
#define	SMP_RG_EXT_CONFIG_ROUTE_TABLE	0x01
	uint8_t	reserved0;
	uint8_t	encl_logical_id[8];
	uint8_t	reserved1[8];
	uint8_t	reserved2[2];
	uint8_t	stp_bus_inact_time_limit[2];
	uint8_t	stp_max_conn_time_limit[2];
	uint8_t	stp_smp_it_nexus_loss_time[2];
	uint8_t	config_bits1;
#define	SMP_RG_NUM_ZONE_GROUPS_MASK	0xc0
#define	SMP_RG_NUM_ZONE_GROUPS_SHIFT	6
#define	SMP_RG_ZONE_LOCKED		0x10
#define	SMP_RG_PP_SUPPORTED		0x08
#define	SMP_RG_PP_ASSERTED		0x04
#define	SMP_RG_ZONING_SUPPORTED		0x02
#define	SMP_RG_ZONING_ENABLED		0x01
	uint8_t	config_bits2;
#define	SMP_RG_SAVING			0x10
#define	SMP_RG_SAVING_ZM_PWD_SUP	0x08
#define	SMP_RG_SAVING_PHY_INFO_SUP	0x04
#define	SMP_RG_SAVING_ZPERM_TAB_SUP	0x02
#define	SMP_RG_SAVING_ZENABLED_SUP	0x01
	uint8_t	max_num_routed_addrs[2];
	uint8_t	active_zm_address[8];
	uint8_t	zone_lock_inact_time_limit[2];
	uint8_t	reserved3[2];
	uint8_t	reserved4;
	uint8_t	first_encl_conn_el_index;
	uint8_t	num_encl_conn_el_indexes;
	uint8_t	reserved5;
	uint8_t	reduced_functionality;
#define	SMP_RG_REDUCED_FUNCTIONALITY	0x80
	uint8_t	time_to_reduced_func;
	uint8_t	initial_time_to_reduced_func;
	uint8_t	max_reduced_func_time;
	uint8_t	last_sc_stat_desc_index[2];
	uint8_t	max_sc_stat_descs[2];
	uint8_t	last_phy_evl_desc_index[2];
	uint8_t	max_stored_pel_descs[2];
	uint8_t	stp_reject_to_open_limit[2];
	uint8_t	reserved6[2];
	uint8_t	crc[4];
};

/*
 * REPORT MANUFACTURER INFORMATION request and response, current as of SPL
 * Revision 7.
 */
struct smp_report_manuf_info_request
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t	response_len;
	uint8_t	request_len;
#define	SMP_RMI_REQUEST_LEN		0x00
	uint8_t	crc[4];
};

struct smp_report_manuf_info_response
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t	function_result;
	uint8_t	response_len;
#define	SMP_RMI_RESPONSE_LEN		0x0e
	uint8_t	expander_change_count[2];
	uint8_t	reserved0[2];
	uint8_t	sas_11_format;
#define	SMP_RMI_SAS11_FORMAT		0x01
	uint8_t	reserved1[3];
	uint8_t	vendor[8];
	uint8_t	product[16];
	uint8_t	revision[4];
	uint8_t	comp_vendor[8];
	uint8_t	comp_id[2];
	uint8_t	comp_revision;
	uint8_t	reserved2;
	uint8_t	vendor_specific[8];
	uint8_t	crc[4];
};

/*
 * DISCOVER request and response, current as of SPL Revision 7.
 */
struct smp_discover_request
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t response_len;
	uint8_t request_len;
#define	SMP_DIS_REQUEST_LEN		0x02
	uint8_t reserved0[4];
	uint8_t	ignore_zone_group;
#define	SMP_DIS_IGNORE_ZONE_GROUP	0x01
	uint8_t	phy;
	uint8_t	reserved1[2];
	uint8_t	crc[4];
};

struct smp_discover_response
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t	function_result;
	uint8_t	response_len;
#define	SMP_DIS_RESPONSE_LEN		0x20
	uint8_t	expander_change_count[2];
	uint8_t	reserved0[3];
	uint8_t	phy;
	uint8_t	reserved1[2];
	uint8_t	attached_device;
#define	SMP_DIS_AD_TYPE_MASK		0x70
#define	SMP_DIS_AD_TYPE_NONE		0x00
#define	SMP_DIS_AD_TYPE_SAS_SATA	0x10
#define	SMP_DIS_AD_TYPE_EXP		0x20
#define	SMP_DIS_AD_TYPE_EXP_OLD		0x30
#define	SMP_DIS_ATTACH_REASON_MASK	0x0f
	uint8_t	neg_logical_link_rate;
#define	SMP_DIS_LR_MASK			0x0f
#define	SMP_DIS_LR_DISABLED		0x01
#define	SMP_DIS_LR_PHY_RES_PROB		0x02
#define	SMP_DIS_LR_SPINUP_HOLD		0x03
#define	SMP_DIS_LR_PORT_SEL		0x04
#define	SMP_DIS_LR_RESET_IN_PROG	0x05
#define	SMP_DIS_LR_UNSUP_PHY_ATTACHED	0x06
#define	SMP_DIS_LR_G1_15GBPS		0x08
#define	SMP_DIS_LR_G2_30GBPS		0x09
#define	SMP_DIS_LR_G3_60GBPS		0x0a
	uint8_t	config_bits0;
#define	SMP_DIS_ATTACHED_SSP_INIT	0x08
#define	SMP_DIS_ATTACHED_STP_INIT	0x04
#define	SMP_DIS_ATTACHED_SMP_INIT	0x02
#define	SMP_DIS_ATTACHED_SATA_HOST	0x01
	uint8_t	config_bits1;
#define	SMP_DIS_ATTACHED_SATA_PORTSEL	0x80
#define	SMP_DIS_STP_BUFFER_TOO_SMALL	0x10
#define	SMP_DIS_ATTACHED_SSP_TARG	0x08
#define	SMP_DIS_ATTACHED_STP_TARG	0x04
#define	SMP_DIS_ATTACHED_SMP_TARG	0x02
#define	SMP_DIS_ATTACHED_SATA_DEV	0x01
	uint8_t	sas_address[8];
	uint8_t	attached_sas_address[8];
	uint8_t	attached_phy_id;
	uint8_t	config_bits2;
#define	SMP_DIS_ATT_SLUMB_CAP		0x10
#define	SMP_DIS_ATT_PAR_CAP		0x08
#define	SMP_DIS_ATT_IN_ZPSDS_PER	0x04
#define	SMP_DIS_ATT_REQ_IN_ZPSDS	0x02
#define	SMP_DIS_ATT_BREAK_RPL_CAP	0x01
	uint8_t	reserved2[6];
	uint8_t	link_rate0;
#define	SMP_DIS_PROG_MIN_LR_MASK	0xf0
#define	SMP_DIS_PROG_MIN_LR_SHIFT	4
#define	SMP_DIS_HARD_MIN_LR_MASK	0x0f
	uint8_t	link_rate1;
#define	SMP_DIS_PROG_MAX_LR_MAX		0xf0
#define	SMP_DIS_PROG_MAX_LR_SHIFT	4
#define	SMP_DIS_HARD_MAX_LR_MASK	0x0f
	uint8_t	phy_change_count;
	uint8_t	pp_timeout;
#define	SMP_DIS_VIRTUAL_PHY		0x80
#define	SMP_DIS_PP_TIMEOUT_MASK		0x0f
	uint8_t	routing_attr;
	uint8_t	conn_type;
	uint8_t	conn_el_index;
	uint8_t	conn_phys_link;
	uint8_t	config_bits3;
#define	SMP_DIS_PHY_POW_COND_MASK	0xc0
#define	SMP_DIS_PHY_POW_COND_SHIFT	6
#define	SMP_DIS_SAS_SLUMB_CAP		0x08
#define	SMP_DIS_SAS_PART_CAP		0x04
#define	SMP_DIS_SATA_SLUMB_CAP		0x02
#define	SMP_DIS_SATA_PART_CAP		0x01
	uint8_t	config_bits4;
#define	SMP_DIS_SAS_SLUMB_ENB		0x08
#define	SMP_DIS_SAS_PART_ENB		0x04
#define	SMP_DIS_SATA_SLUMB_ENB		0x02
#define	SMP_DIS_SATA_PART_ENB		0x01
	uint8_t	vendor_spec[2];
	uint8_t	attached_dev_name[8];
	uint8_t	config_bits5;
#define	SMP_DIS_REQ_IN_ZPSDS_CHG	0x40
#define	SMP_DIS_IN_ZPSDS_PER		0x20
#define	SMP_DIS_REQ_IN_ZPSDS		0x10
#define	SMP_DIS_ZG_PER			0x04
#define	SMP_DIS_IN_ZPSDS		0x02
#define	SMP_DIS_ZONING_ENB		0x01
	uint8_t	reserved3[2];
	uint8_t	zone_group;
	uint8_t	self_config_status;
	uint8_t	self_config_levels_comp;
	uint8_t	reserved4[2];
	uint8_t	self_config_sas_addr[8];
	uint8_t	prog_phy_cap[4];
	uint8_t	current_phy_cap[4];
	uint8_t	attached_phy_cap[4];
	uint8_t	reserved5[6];
	uint8_t	neg_phys_link_rate;
#define	SMP_DIS_REASON_MASK		0xf0
#define	SMP_DIS_REASON_SHIFT		4
#define	SMP_DIS_PHYS_LR_MASK		0x0f
	uint8_t	config_bits6;
#define	SMP_DIS_OPTICAL_MODE_ENB	0x04
#define	SMP_DIS_NEG_SSC			0x02
#define	SMP_DIS_HW_MUX_SUP		0x01
	uint8_t	config_bits7;
#define	SMP_DIS_DEF_IN_ZPSDS_PER	0x20
#define	SMP_DIS_DEF_REQ_IN_ZPSDS	0x10
#define	SMP_DIS_DEF_ZG_PER		0x04
#define	SMP_DIS_DEF_ZONING_ENB		0x01
	uint8_t	reserved6;
	uint8_t	reserved7;
	uint8_t	default_zone_group;
	uint8_t	config_bits8;
#define	SMP_DIS_SAVED_IN_ZPSDS_PER	0x20
#define	SMP_DIS_SAVED_REQ_IN_SPSDS	0x10
#define	SMP_DIS_SAVED_ZG_PER		0x04
#define	SMP_DIS_SAVED_ZONING_ENB	0x01
	uint8_t	reserved8;
	uint8_t	reserved9;
	uint8_t	saved_zone_group;
	uint8_t	config_bits9;
#define	SMP_DIS_SHADOW_IN_ZPSDS_PER	0x20
#define	SMP_DIS_SHADOW_IN_REQ_IN_ZPSDS	0x10
#define	SMP_DIS_SHADOW_ZG_PER		0x04
	uint8_t reserved10;
	uint8_t reserved11;
	uint8_t	shadow_zone_group;
	uint8_t	device_slot_num;
	uint8_t	device_slot_group_num;
	uint8_t	device_slot_group_out_conn[6];
	uint8_t	stp_buffer_size[2];
	uint8_t	reserved12;
	uint8_t	reserved13;
	uint8_t	crc[4];
};

/*
 * PHY CONTROL request and response.  Current as of SPL Revision 7.
 */
struct smp_phy_control_request
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t response_len;
#define	SMP_PC_RESPONSE_LEN		0x00
	uint8_t request_len;
#define	SMP_PC_REQUEST_LEN		0x09
	uint8_t expected_exp_chg_cnt[2];
	uint8_t reserved0[3];
	uint8_t phy;
	uint8_t phy_operation;
#define	SMP_PC_PHY_OP_NOP		0x00
#define	SMP_PC_PHY_OP_LINK_RESET	0x01
#define	SMP_PC_PHY_OP_HARD_RESET	0x02
#define	SMP_PC_PHY_OP_DISABLE		0x03
#define	SMP_PC_PHY_OP_CLEAR_ERR_LOG	0x05
#define	SMP_PC_PHY_OP_CLEAR_AFFILIATON	0x06
#define	SMP_PC_PHY_OP_TRANS_SATA_PSS	0x07
#define	SMP_PC_PHY_OP_CLEAR_STP_ITN_LS	0x08
#define	SMP_PC_PHY_OP_SET_ATT_DEV_NAME	0x09
	uint8_t update_pp_timeout;
#define	SMP_PC_UPDATE_PP_TIMEOUT	0x01
	uint8_t reserved1[12];
	uint8_t attached_device_name[8];
	uint8_t prog_min_phys_link_rate;
#define	SMP_PC_PROG_MIN_PL_RATE_MASK	0xf0
#define	SMP_PC_PROG_MIN_PL_RATE_SHIFT	4
	uint8_t prog_max_phys_link_rate;
#define	SMP_PC_PROG_MAX_PL_RATE_MASK	0xf0
#define	SMP_PC_PROG_MAX_PL_RATE_SHIFT	4
	uint8_t	config_bits0;
#define	SMP_PC_SP_NC			0x00
#define	SMP_PC_SP_DISABLE		0x02
#define	SMP_PC_SP_ENABLE		0x01
#define	SMP_PC_SAS_SLUMBER_NC		0x00
#define	SMP_PC_SAS_SLUMBER_DISABLE	0x80
#define	SMP_PC_SAS_SLUMBER_ENABLE	0x40
#define	SMP_PC_SAS_SLUMBER_MASK		0xc0
#define	SMP_PC_SAS_SLUMBER_SHIFT	6
#define	SMP_PC_SAS_PARTIAL_NC		0x00
#define	SMP_PC_SAS_PARTIAL_DISABLE	0x20
#define	SMP_PC_SAS_PARTIAL_ENABLE	0x10
#define	SMP_PC_SAS_PARTIAL_MASK		0x30
#define	SMP_PC_SAS_PARTIAL_SHIFT	4
#define	SMP_PC_SATA_SLUMBER_NC		0x00
#define	SMP_PC_SATA_SLUMBER_DISABLE	0x08
#define	SMP_PC_SATA_SLUMBER_ENABLE	0x04
#define	SMP_PC_SATA_SLUMBER_MASK	0x0c
#define	SMP_PC_SATA_SLUMBER_SHIFT	2
#define	SMP_PC_SATA_PARTIAL_NC		0x00
#define	SMP_PC_SATA_PARTIAL_DISABLE	0x02
#define	SMP_PC_SATA_PARTIAL_ENABLE	0x01
#define	SMP_PC_SATA_PARTIAL_MASK	0x03
#define	SMP_PC_SATA_PARTIAL_SHIFT	0
	uint8_t	reserved2;
	uint8_t	pp_timeout_value;
#define	SMP_PC_PP_TIMEOUT_MASK		0x0f
	uint8_t reserved3[3];
	uint8_t	crc[4];
};

struct smp_phy_control_response
{
	uint8_t	frame_type;
	uint8_t	function;
	uint8_t	function_result;
	uint8_t	response_len;
#define	SMP_PC_RESPONSE_LEN		0x00
	uint8_t crc[4];
};

__BEGIN_DECLS

const char *smp_error_desc(int function_result);
const char *smp_command_desc(uint8_t cmd_num);
void smp_command_decode(uint8_t *smp_request, int request_len, struct sbuf *sb,
			char *line_prefix, int first_line_len, int line_len);
void smp_command_sbuf(struct ccb_smpio *smpio, struct sbuf *sb,
		      char *line_prefix, int first_line_len, int line_len);

#ifdef _KERNEL
void smp_error_sbuf(struct ccb_smpio *smpio, struct sbuf *sb);
#else /* !_KERNEL*/
void smp_error_sbuf(struct cam_device *device, struct ccb_smpio *smpio,
		    struct sbuf *sb);
#endif /* _KERNEL/!_KERNEL */

void smp_report_general_sbuf(struct smp_report_general_response *response,
			     int response_len, struct sbuf *sb);

void smp_report_manuf_info_sbuf(struct smp_report_manuf_info_response *response,
				int response_len, struct sbuf *sb);

void smp_report_general(struct ccb_smpio *smpio, uint32_t retries,
			void (*cbfcnp)(struct cam_periph *, union ccb *),
			struct smp_report_general_request *request,
			int request_len, uint8_t *response, int response_len,
			int long_response, uint32_t timeout);

void smp_discover(struct ccb_smpio *smpio, uint32_t retries,
		  void (*cbfcnp)(struct cam_periph *, union ccb *),
		  struct smp_discover_request *request, int request_len,
		  uint8_t *response, int response_len, int long_response,
		  int ignore_zone_group, int phy, uint32_t timeout);

void smp_report_manuf_info(struct ccb_smpio *smpio, uint32_t retries,
			   void (*cbfcnp)(struct cam_periph *, union ccb *),
			   struct smp_report_manuf_info_request *request,
			   int request_len, uint8_t *response, int response_len,
			   int long_response, uint32_t timeout);

void smp_phy_control(struct ccb_smpio *smpio, uint32_t retries,
		     void (*cbfcnp)(struct cam_periph *, union ccb *),
		     struct smp_phy_control_request *request, int request_len,
		     uint8_t *response, int response_len, int long_response,
		     uint32_t expected_exp_change_count, int phy, int phy_op,
		     int update_pp_timeout_val, uint64_t attached_device_name,
		     int prog_min_prl, int prog_max_prl, int slumber_partial,
		     int pp_timeout_value, uint32_t timeout);
__END_DECLS

#endif /*_SCSI_SMP_ALL_H*/
