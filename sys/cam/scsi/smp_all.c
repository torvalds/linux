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
 * $Id: //depot/users/kenm/FreeBSD-test/sys/cam/scsi/smp_all.c#4 $
 */

/*
 * Serial Management Protocol helper functions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/libkern.h>
#include <sys/kernel.h>
#else /* _KERNEL */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#endif /* _KERNEL */

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/scsi/smp_all.h>
#include <sys/sbuf.h>

#ifndef _KERNEL
#include <camlib.h>
#endif

static char *smp_yesno(int val);

static char *
smp_yesno(int val)
{
	char *str;

	if (val)
		str = "Yes";
	else
		str = "No";

	return (str);
}

struct smp_error_table_entry {
	uint8_t	function_result;
	const char *desc;
};

/* List current as of SPL Revision 7 */
static struct smp_error_table_entry smp_error_table[] = {
	{SMP_FR_ACCEPTED, "SMP Function Accepted"},
	{SMP_FR_UNKNOWN_FUNC, "Unknown SMP Function"},
	{SMP_FR_FUNCTION_FAILED, "SMP Function Failed"},
	{SMP_FR_INVALID_REQ_FRAME_LEN, "Invalid Request Frame Length"},
	{SMP_FR_INVALID_EXP_CHG_CNT, "Invalid Expander Change Count"},
	{SMP_FR_BUSY, "Busy"},
	{SMP_FR_INCOMPLETE_DESC_LIST, "Incomplete Descriptor List"},
	{SMP_FR_PHY_DOES_NOT_EXIST, "Phy Does Not Exist"},
	{SMP_FR_INDEX_DOES_NOT_EXIST, "Index Does Not Exist"},
	{SMP_FR_PHY_DOES_NOT_SUP_SATA, "Phy Does Not Support SATA"},
	{SMP_FR_UNKNOWN_PHY_OP, "Unknown Phy Operation"},
	{SMP_FR_UNKNOWN_PHY_TEST_FUNC, "Unknown Phy Test Function"},
	{SMP_FR_PHY_TEST_FUNC_INPROG, "Phy Test Function In Progress"},
	{SMP_FR_PHY_VACANT, "Phy Vacant"},
	{SMP_FR_UNKNOWN_PHY_EVENT_SRC, "Unknown Phy Event Source"},
	{SMP_FR_UNKNOWN_DESC_TYPE, "Unknown Descriptor Type"},
	{SMP_FR_UNKNOWN_PHY_FILTER, "Unknown Phy Filter"},
	{SMP_FR_AFFILIATION_VIOLATION, "Affiliation Violation"},
	{SMP_FR_SMP_ZONE_VIOLATION, "SMP Zone Violation"},
	{SMP_FR_NO_MGMT_ACCESS_RIGHTS, "No Management Access Rights"},
	{SMP_FR_UNKNOWN_ED_ZONING_VAL, "Unknown Enable Disable Zoning Value"},
	{SMP_FR_ZONE_LOCK_VIOLATION, "Zone Lock Violation"},
	{SMP_FR_NOT_ACTIVATED, "Not Activated"},
	{SMP_FR_ZG_OUT_OF_RANGE, "Zone Group Out of Range"},
	{SMP_FR_NO_PHYS_PRESENCE, "No Physical Presence"},
	{SMP_FR_SAVING_NOT_SUP, "Saving Not Supported"},
	{SMP_FR_SRC_ZONE_DNE, "Source Zone Group Does Not Exist"},
	{SMP_FR_DISABLED_PWD_NOT_SUP, "Disabled Password Not Supported"}
};

const char *
smp_error_desc(int function_result)
{
	int i;

	for (i = 0; i < nitems(smp_error_table); i++){
		if (function_result == smp_error_table[i].function_result)
			return (smp_error_table[i].desc);
	}
	return ("Reserved Function Result");
}

/* List current as of SPL Revision 7 */
struct smp_cmd_table_entry {
	uint8_t	cmd_num;
	const char *desc;
} smp_cmd_table[] = {
	{SMP_FUNC_REPORT_GENERAL, "REPORT GENERAL"},
	{SMP_FUNC_REPORT_MANUF_INFO, "REPORT MANUFACTURER INFORMATION"},
	{SMP_FUNC_REPORT_SC_STATUS, "REPORT SELF-CONFIGURATION STATUS"},
	{SMP_FUNC_REPORT_ZONE_PERM_TBL, "REPORT ZONE PERMISSION TABLE"},
	{SMP_FUNC_REPORT_BROADCAST, "REPORT BROADCAST"},
	{SMP_FUNC_DISCOVER, "DISCOVER"},
	{SMP_FUNC_REPORT_PHY_ERR_LOG, "REPORT PHY ERROR LOG"},
	{SMP_FUNC_REPORT_PHY_SATA, "REPORT PHY SATA"},
	{SMP_FUNC_REPORT_ROUTE_INFO, "REPORT ROUTE INFORMATION"},
	{SMP_FUNC_REPORT_PHY_EVENT, "REPORT PHY EVENT"},
	{SMP_FUNC_DISCOVER_LIST, "DISCOVER LIST"},
	{SMP_FUNC_REPORT_PHY_EVENT_LIST, "REPORT PHY EVENT LIST"},
	{SMP_FUNC_REPORT_EXP_RTL, "REPORT EXPANDER ROUTE TABLE LIST"},
	{SMP_FUNC_CONFIG_GENERAL, "CONFIGURE GENERAL"},
	{SMP_FUNC_ENABLE_DISABLE_ZONING, "ENABLE DISABLE ZONING"},
	{SMP_FUNC_ZONED_BROADCAST, "ZONED BROADCAST"},
	{SMP_FUNC_ZONE_LOCK, "ZONE LOCK"},
	{SMP_FUNC_ZONE_ACTIVATE, "ZONE ACTIVATE"},
	{SMP_FUNC_ZONE_UNLOCK, "ZONE UNLOCK"},
	{SMP_FUNC_CONFIG_ZM_PWD, "CONFIGURE ZONE MANAGER PASSWORD"},
	{SMP_FUNC_CONFIG_ZONE_PHY_INFO, "CONFIGURE ZONE PHY INFORMATION"},
	{SMP_FUNC_CONFIG_ZONE_PERM_TBL, "CONFIGURE ZONE PERMISSION TABLE"},
	{SMP_FUNC_CONFIG_ROUTE_INFO, "CONFIGURE ROUTE INFORMATION"},
	{SMP_FUNC_PHY_CONTROL, "PHY CONTROL"},
	{SMP_FUNC_PHY_TEST_FUNC, "PHY TEST FUNCTION"},
	{SMP_FUNC_CONFIG_PHY_EVENT, "CONFIGURE PHY EVENT"}
};

const char *
smp_command_desc(uint8_t cmd_num)
{
	int i;

	for (i = 0; i < nitems(smp_cmd_table) &&
	     smp_cmd_table[i].cmd_num <= cmd_num; i++) {
		if (cmd_num == smp_cmd_table[i].cmd_num)
			return (smp_cmd_table[i].desc);
	}

	/*
	 * 0x40 to 0x7f and 0xc0 to 0xff are the vendor specific SMP
	 * command ranges.
	 */
	if (((cmd_num >= 0x40) && (cmd_num <= 0x7f))
	 || (cmd_num >= 0xc0)) {
		return ("Vendor Specific SMP Command");
	} else {
		return ("Unknown SMP Command");
	}
}

/*
 * Decode a SMP request buffer into a string of hexadecimal numbers.
 *
 * smp_request:    SMP request
 * request_len:    length of the SMP request buffer, may be reduced if the
 *                 caller only wants part of the buffer printed
 * sb:             sbuf(9) buffer
 * line_prefix:    prefix for new lines, or an empty string ("")
 * first_line_len: length left on first line
 * line_len:       total length of subsequent lines, 0 for no additional lines
 *                 if there are no additional lines, first line will get ...
 *                 at the end if there is additional data
 */
void
smp_command_decode(uint8_t *smp_request, int request_len, struct sbuf *sb,
		   char *line_prefix, int first_line_len, int line_len)
{
	int i, cur_len;

	for (i = 0, cur_len = first_line_len; i < request_len; i++) {
		/*
		 * Each byte takes 3 characters.  As soon as we go less
		 * than 6 (meaning we have at least 3 and at most 5
		 * characters left), check to see whether the subsequent
		 * line length (line_len) is long enough to bother with.
		 * If the user set it to 0, or some other length that isn't
		 * enough to hold at least the prefix and one byte, put ...
		 * on the first line to indicate that there is more data
		 * and bail out.
		 */
		if ((cur_len < 6)
		 && (line_len < (strlen(line_prefix) + 3))) {
			sbuf_printf(sb, "...");
			return;
		}
		if (cur_len < 3) {
			sbuf_printf(sb, "\n%s", line_prefix);
			cur_len = line_len - strlen(line_prefix);
		}
		sbuf_printf(sb, "%02x ", smp_request[i]);
		cur_len = cur_len - 3;
	}
}

void
smp_command_sbuf(struct ccb_smpio *smpio, struct sbuf *sb,
		 char *line_prefix, int first_line_len, int line_len)
{
	sbuf_printf(sb, "%s. ", smp_command_desc(smpio->smp_request[1]));

	/*
	 * Acccount for the command description and the period and space
	 * after the command description.
	 */
	first_line_len -= strlen(smp_command_desc(smpio->smp_request[1])) + 2;

	smp_command_decode(smpio->smp_request, smpio->smp_request_len, sb,
			   line_prefix, first_line_len, line_len);
}

/*
 * Print SMP error output.  For userland commands, we need the cam_device
 * structure so we can get the path information from the CCB.
 */
#ifdef _KERNEL
void
smp_error_sbuf(struct ccb_smpio *smpio, struct sbuf *sb)
#else /* !_KERNEL*/
void
smp_error_sbuf(struct cam_device *device, struct ccb_smpio *smpio,
	       struct sbuf *sb)
#endif /* _KERNEL/!_KERNEL */
{
	char path_str[64];

#ifdef _KERNEL
	xpt_path_string(smpio->ccb_h.path, path_str, sizeof(path_str));
#else
	cam_path_string(device, path_str, sizeof(path_str));
#endif
	smp_command_sbuf(smpio, sb, path_str, 80 - strlen(path_str), 80);
	sbuf_printf(sb, "\n");

	sbuf_cat(sb, path_str);
	sbuf_printf(sb, "SMP Error: %s (0x%x)\n",
		    smp_error_desc(smpio->smp_response[2]),
		    smpio->smp_response[2]);
}

/*
 * Decode the SMP REPORT GENERAL response.  The format is current as of SPL
 * Revision 7, but the parsing should be backward compatible for older
 * versions of the spec.
 */
void
smp_report_general_sbuf(struct smp_report_general_response *response,
			int response_len, struct sbuf *sb)
{
	sbuf_printf(sb, "Report General\n");
	sbuf_printf(sb, "Response Length: %d words (%d bytes)\n",
		    response->response_len,
		    response->response_len * SMP_WORD_LEN);
	sbuf_printf(sb, "Expander Change Count: %d\n",
		    scsi_2btoul(response->expander_change_count));
	sbuf_printf(sb, "Expander Route Indexes: %d\n", 
		    scsi_2btoul(response->expander_route_indexes));
	sbuf_printf(sb, "Long Response: %s\n",
		    smp_yesno(response->long_response &
			      SMP_RG_LONG_RESPONSE));
	sbuf_printf(sb, "Number of Phys: %d\n", response->num_phys);
	sbuf_printf(sb, "Table to Table Supported: %s\n",
		    smp_yesno(response->config_bits0 &
		    SMP_RG_TABLE_TO_TABLE_SUP));
	sbuf_printf(sb, "Zone Configuring: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_ZONE_CONFIGURING));
	sbuf_printf(sb, "Self Configuring: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_SELF_CONFIGURING));
	sbuf_printf(sb, "STP Continue AWT: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_STP_CONTINUE_AWT));
	sbuf_printf(sb, "Open Reject Retry Supported: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_OPEN_REJECT_RETRY_SUP));
	sbuf_printf(sb, "Configures Others: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_CONFIGURES_OTHERS));
	sbuf_printf(sb, "Configuring: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_CONFIGURING));
	sbuf_printf(sb, "Externally Configurable Route Table: %s\n", 
		    smp_yesno(response->config_bits0 &
		    SMP_RG_CONFIGURING));
	sbuf_printf(sb, "Enclosure Logical Identifier: 0x%016jx\n",
		    (uintmax_t)scsi_8btou64(response->encl_logical_id));

	/*
	 * If the response->response_len is 0, then we don't have the
	 * extended information.  Also, if the user didn't allocate enough
	 * space for the full request, don't try to parse it.
	 */
	if ((response->response_len == 0)
	 || (response_len < (sizeof(struct smp_report_general_response) -
	     sizeof(response->crc))))
		return;

	sbuf_printf(sb, "STP Bus Inactivity Time Limit: %d\n",
		    scsi_2btoul(response->stp_bus_inact_time_limit));
	sbuf_printf(sb, "STP Maximum Connect Time Limit: %d\n",
		    scsi_2btoul(response->stp_max_conn_time_limit));
	sbuf_printf(sb, "STP SMP I_T Nexus Loss Time: %d\n",
		    scsi_2btoul(response->stp_smp_it_nexus_loss_time));

	sbuf_printf(sb, "Number of Zone Groups: %d\n",
		    (response->config_bits1 & SMP_RG_NUM_ZONE_GROUPS_MASK) >>
		    SMP_RG_NUM_ZONE_GROUPS_SHIFT);
	sbuf_printf(sb, "Zone Locked: %s\n",
		    smp_yesno(response->config_bits1 & SMP_RG_ZONE_LOCKED));
	sbuf_printf(sb, "Physical Presence Supported: %s\n",
		    smp_yesno(response->config_bits1 & SMP_RG_PP_SUPPORTED));
	sbuf_printf(sb, "Physical Presence Asserted: %s\n",
		    smp_yesno(response->config_bits1 & SMP_RG_PP_ASSERTED));
	sbuf_printf(sb, "Zoning Supported: %s\n",
		    smp_yesno(response->config_bits1 &
			      SMP_RG_ZONING_SUPPORTED));
	sbuf_printf(sb, "Zoning Enabled: %s\n",
		    smp_yesno(response->config_bits1 & SMP_RG_ZONING_ENABLED));

	sbuf_printf(sb, "Saving: %s\n",
		    smp_yesno(response->config_bits2 & SMP_RG_SAVING));
	sbuf_printf(sb, "Saving Zone Manager Password Supported: %s\n",
		    smp_yesno(response->config_bits2 &
			      SMP_RG_SAVING_ZM_PWD_SUP));
	sbuf_printf(sb, "Saving Zone Phy Information Supported: %s\n",
		    smp_yesno(response->config_bits2 &
			      SMP_RG_SAVING_PHY_INFO_SUP));
	sbuf_printf(sb, "Saving Zone Permission Table Supported: %s\n",
		    smp_yesno(response->config_bits2 &
			      SMP_RG_SAVING_ZPERM_TAB_SUP));
	sbuf_printf(sb, "Saving Zoning Enabled Supported: %s\n",
		    smp_yesno(response->config_bits2 &
			      SMP_RG_SAVING_ZENABLED_SUP));

	sbuf_printf(sb, "Maximum Number of Routed SAS Addresses: %d\n",
		    scsi_2btoul(response->max_num_routed_addrs));

	sbuf_printf(sb, "Active Zone Manager SAS Address: 0x%016jx\n",
		    scsi_8btou64(response->active_zm_address));

	sbuf_printf(sb, "Zone Inactivity Time Limit: %d\n",
		    scsi_2btoul(response->zone_lock_inact_time_limit));

	sbuf_printf(sb, "First Enclosure Connector Element Index: %d\n",
		    response->first_encl_conn_el_index);

	sbuf_printf(sb, "Number of Enclosure Connector Element Indexes: %d\n",
		    response->num_encl_conn_el_indexes);

	sbuf_printf(sb, "Reduced Functionality: %s\n",
		    smp_yesno(response->reduced_functionality &
			      SMP_RG_REDUCED_FUNCTIONALITY));

	sbuf_printf(sb, "Time to Reduced Functionality: %d\n",
		    response->time_to_reduced_func);
	sbuf_printf(sb, "Initial Time to Reduced Functionality: %d\n",
		    response->initial_time_to_reduced_func);
	sbuf_printf(sb, "Maximum Reduced Functionality Time: %d\n",
		    response->max_reduced_func_time);

	sbuf_printf(sb, "Last Self-Configuration Status Descriptor Index: %d\n",
		    scsi_2btoul(response->last_sc_stat_desc_index));

	sbuf_printf(sb, "Maximum Number of Storated Self-Configuration "
		    "Status Descriptors: %d\n",
		    scsi_2btoul(response->max_sc_stat_descs));

	sbuf_printf(sb, "Last Phy Event List Descriptor Index: %d\n",
		    scsi_2btoul(response->last_phy_evl_desc_index));

	sbuf_printf(sb, "Maximum Number of Stored Phy Event List "
		    "Descriptors: %d\n",
		    scsi_2btoul(response->max_stored_pel_descs));

	sbuf_printf(sb, "STP Reject to Open Limit: %d\n",
		    scsi_2btoul(response->stp_reject_to_open_limit));
}

/*
 * Decode the SMP REPORT MANUFACTURER INFORMATION response.  The format is
 * current as of SPL Revision 7, but the parsing should be backward
 * compatible for older versions of the spec.
 */
void
smp_report_manuf_info_sbuf(struct smp_report_manuf_info_response *response,
			   int response_len, struct sbuf *sb)
{
	char vendor[16], product[48], revision[16];
	char comp_vendor[16];

	sbuf_printf(sb, "Report Manufacturer Information\n");
	sbuf_printf(sb, "Expander Change count: %d\n",
		    scsi_2btoul(response->expander_change_count));
	sbuf_printf(sb, "SAS 1.1 Format: %s\n",
		    smp_yesno(response->sas_11_format & SMP_RMI_SAS11_FORMAT));
	cam_strvis(vendor, response->vendor, sizeof(response->vendor),
		   sizeof(vendor));
	cam_strvis(product, response->product, sizeof(response->product),
		   sizeof(product));
	cam_strvis(revision, response->revision, sizeof(response->revision),
		   sizeof(revision));
	sbuf_printf(sb, "<%s %s %s>\n", vendor, product, revision);

	if ((response->sas_11_format & SMP_RMI_SAS11_FORMAT) == 0) {
		uint8_t *curbyte;
		int line_start, line_cursor;

		sbuf_printf(sb, "Vendor Specific Data:\n");

		/*
		 * Print out the bytes roughly in the style of hd(1), but
		 * without the extra ASCII decoding.  Hexadecimal line
		 * numbers on the left, and 16 bytes per line, with an
		 * extra space after the first 8 bytes.
		 *
		 * It would be nice if this sort of thing were available
		 * in a library routine.
		 */
		for (curbyte = (uint8_t *)&response->comp_vendor, line_start= 1,
		     line_cursor = 0; curbyte < (uint8_t *)&response->crc;
		     curbyte++, line_cursor++) {
			if (line_start != 0) {
				sbuf_printf(sb, "%08lx  ",
					    (unsigned long)(curbyte -
					    (uint8_t *)response));
				line_start = 0;
				line_cursor = 0;
			}
			sbuf_printf(sb, "%02x", *curbyte);

			if (line_cursor == 15) {
				sbuf_printf(sb, "\n");
				line_start = 1;
			} else
				sbuf_printf(sb, " %s", (line_cursor == 7) ?
					    " " : "");
		}
		if (line_cursor != 16)
			sbuf_printf(sb, "\n");
		return;
	}

	cam_strvis(comp_vendor, response->comp_vendor,
		   sizeof(response->comp_vendor), sizeof(comp_vendor));
	sbuf_printf(sb, "Component Vendor: %s\n", comp_vendor);
	sbuf_printf(sb, "Component ID: %#x\n", scsi_2btoul(response->comp_id));
	sbuf_printf(sb, "Component Revision: %#x\n", response->comp_revision);
	sbuf_printf(sb, "Vendor Specific: 0x%016jx\n",
		    (uintmax_t)scsi_8btou64(response->vendor_specific));
}

/*
 * Compose a SMP REPORT GENERAL request and put it into a CCB.  This is
 * current as of SPL Revision 7.
 */
void
smp_report_general(struct ccb_smpio *smpio, uint32_t retries,
		   void (*cbfcnp)(struct cam_periph *, union ccb *),
		   struct smp_report_general_request *request, int request_len,
		   uint8_t *response, int response_len, int long_response,
		   uint32_t timeout)
{
	cam_fill_smpio(smpio,
		       retries,
		       cbfcnp,
		       /*flags*/CAM_DIR_BOTH,
		       (uint8_t *)request,
		       request_len - SMP_CRC_LEN,
		       response,
		       response_len,
		       timeout);

	bzero(request, sizeof(*request));

	request->frame_type = SMP_FRAME_TYPE_REQUEST;
	request->function = SMP_FUNC_REPORT_GENERAL;
	request->response_len = long_response ? SMP_RG_RESPONSE_LEN : 0;
	request->request_len = 0;
}

/*
 * Compose a SMP DISCOVER request and put it into a CCB.  This is current
 * as of SPL Revision 7.
 */
void
smp_discover(struct ccb_smpio *smpio, uint32_t retries,
	     void (*cbfcnp)(struct cam_periph *, union ccb *),
	     struct smp_discover_request *request, int request_len,
	     uint8_t *response, int response_len, int long_response,
	     int ignore_zone_group, int phy, uint32_t timeout)
{
	cam_fill_smpio(smpio,
		       retries,
		       cbfcnp,
		       /*flags*/CAM_DIR_BOTH,
		       (uint8_t *)request,
		       request_len - SMP_CRC_LEN,
		       response,
		       response_len,
		       timeout);

	bzero(request, sizeof(*request));
	request->frame_type = SMP_FRAME_TYPE_REQUEST;
	request->function = SMP_FUNC_DISCOVER;
	request->response_len = long_response ? SMP_DIS_RESPONSE_LEN : 0;
	request->request_len = long_response ? SMP_DIS_REQUEST_LEN : 0;
	if (ignore_zone_group != 0)
		request->ignore_zone_group |= SMP_DIS_IGNORE_ZONE_GROUP;
	request->phy = phy;
}

/*
 * Compose a SMP REPORT MANUFACTURER INFORMATION request and put it into a
 * CCB.  This is current as of SPL Revision 7.
 */
void
smp_report_manuf_info(struct ccb_smpio *smpio, uint32_t retries,
		      void (*cbfcnp)(struct cam_periph *, union ccb *),
		      struct smp_report_manuf_info_request *request,
		      int request_len, uint8_t *response, int response_len,
		      int long_response, uint32_t timeout)
{
	cam_fill_smpio(smpio,
		       retries,
		       cbfcnp,
		       /*flags*/CAM_DIR_BOTH,
		       (uint8_t *)request,
		       request_len - SMP_CRC_LEN,
		       response,
		       response_len,
		       timeout);

	bzero(request, sizeof(*request));

	request->frame_type = SMP_FRAME_TYPE_REQUEST;
	request->function = SMP_FUNC_REPORT_MANUF_INFO;
	request->response_len = long_response ? SMP_RMI_RESPONSE_LEN : 0;
	request->request_len = long_response ? SMP_RMI_REQUEST_LEN : 0;
}

/*
 * Compose a SMP PHY CONTROL request and put it into a CCB.  This is
 * current as of SPL Revision 7.
 */
void
smp_phy_control(struct ccb_smpio *smpio, uint32_t retries,
		void (*cbfcnp)(struct cam_periph *, union ccb *),
		struct smp_phy_control_request *request, int request_len,
		uint8_t *response, int response_len, int long_response,
		uint32_t expected_exp_change_count, int phy, int phy_op,
		int update_pp_timeout_val, uint64_t attached_device_name,
		int prog_min_prl, int prog_max_prl, int slumber_partial,
		int pp_timeout_value, uint32_t timeout)
{
	cam_fill_smpio(smpio,
		       retries,
		       cbfcnp,
		       /*flags*/CAM_DIR_BOTH,
		       (uint8_t *)request,
		       request_len - SMP_CRC_LEN,
		       response,
		       response_len,
		       timeout);

	bzero(request, sizeof(*request));

	request->frame_type = SMP_FRAME_TYPE_REQUEST;
	request->function = SMP_FUNC_PHY_CONTROL;
	request->response_len = long_response ? SMP_PC_RESPONSE_LEN : 0;
	request->request_len = long_response ? SMP_PC_REQUEST_LEN : 0;
	scsi_ulto2b(expected_exp_change_count, request->expected_exp_chg_cnt);
	request->phy = phy;
	request->phy_operation = phy_op;

	if (update_pp_timeout_val != 0)
		request->update_pp_timeout |= SMP_PC_UPDATE_PP_TIMEOUT;

	scsi_u64to8b(attached_device_name, request->attached_device_name);
	request->prog_min_phys_link_rate = (prog_min_prl <<
		SMP_PC_PROG_MIN_PL_RATE_SHIFT) & SMP_PC_PROG_MIN_PL_RATE_MASK;
	request->prog_max_phys_link_rate = (prog_max_prl <<
		SMP_PC_PROG_MAX_PL_RATE_SHIFT) & SMP_PC_PROG_MAX_PL_RATE_MASK;
	request->config_bits0 = slumber_partial;
	request->pp_timeout_value = pp_timeout_value;
}

