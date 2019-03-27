/*-
 * Copyright (c) 2016 Spectra Logic Corporation
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
 * Authors: Ken Merry           (Spectra Logic Corporation)
 */
/*
 * ATA Extended Power Conditions (EPC) support
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/ata.h>

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <err.h>
#include <locale.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_message.h>
#include <camlib.h>
#include "camcontrol.h"

typedef enum {
	EPC_ACTION_NONE		= 0x00,
	EPC_ACTION_LIST		= 0x01,
	EPC_ACTION_TIMER_SET	= 0x02,
	EPC_ACTION_IMMEDIATE	= 0x03,
	EPC_ACTION_GETMODE	= 0x04
} epc_action;

static struct scsi_nv epc_flags[] = {
	{ "Supported", ATA_PCL_COND_SUPPORTED },
	{ "Saveable", ATA_PCL_COND_SUPPORTED },
	{ "Changeable", ATA_PCL_COND_CHANGEABLE },
	{ "Default Timer Enabled", ATA_PCL_DEFAULT_TIMER_EN },
	{ "Saved Timer Enabled", ATA_PCL_SAVED_TIMER_EN },
	{ "Current Timer Enabled", ATA_PCL_CURRENT_TIMER_EN },
	{ "Hold Power Condition Not Supported", ATA_PCL_HOLD_PC_NOT_SUP }
};

static struct scsi_nv epc_power_cond_map[] = {
	{ "Standby_z", ATA_EPC_STANDBY_Z },
	{ "z", ATA_EPC_STANDBY_Z },
	{ "Standby_y", ATA_EPC_STANDBY_Y },
	{ "y", ATA_EPC_STANDBY_Y },
	{ "Idle_a", ATA_EPC_IDLE_A },
	{ "a", ATA_EPC_IDLE_A },
	{ "Idle_b", ATA_EPC_IDLE_B },
	{ "b", ATA_EPC_IDLE_B },
	{ "Idle_c", ATA_EPC_IDLE_C },
	{ "c", ATA_EPC_IDLE_C }
};

static struct scsi_nv epc_rst_val[] = {
	{ "default", ATA_SF_EPC_RST_DFLT },
	{ "saved", 0}
};

static struct scsi_nv epc_ps_map[] = {
	{ "unknown", ATA_SF_EPC_SRC_UNKNOWN },
	{ "battery", ATA_SF_EPC_SRC_BAT },
	{ "notbattery", ATA_SF_EPC_SRC_NOT_BAT }
};

/*
 * These aren't subcommands of the EPC SET FEATURES subcommand, but rather
 * commands that determine the current capabilities and status of the drive.
 * The EPC subcommands are limited to 4 bits, so we won't collide with any
 * future values.
 */
#define	CCTL_EPC_GET_STATUS	0x8001
#define	CCTL_EPC_LIST		0x8002

static struct scsi_nv epc_cmd_map[] = {
	{ "restore", ATA_SF_EPC_RESTORE },
	{ "goto", ATA_SF_EPC_GOTO },
	{ "timer", ATA_SF_EPC_SET_TIMER },
	{ "state", ATA_SF_EPC_SET_STATE },
	{ "enable", ATA_SF_EPC_ENABLE },
	{ "disable", ATA_SF_EPC_DISABLE },
	{ "source", ATA_SF_EPC_SET_SOURCE },
	{ "status", CCTL_EPC_GET_STATUS },
	{ "list", CCTL_EPC_LIST }
};

static int epc_list(struct cam_device *device, camcontrol_devtype devtype,
		    union ccb *ccb, int retry_count, int timeout);
static void epc_print_pcl_desc(struct ata_power_cond_log_desc *desc,
			       const char *prefix);
static int epc_getmode(struct cam_device *device, camcontrol_devtype devtype,
		       union ccb *ccb, int retry_count, int timeout,
		       int power_only);
static int epc_set_features(struct cam_device *device,
			    camcontrol_devtype devtype, union ccb *ccb,
			    int retry_count, int timeout, int action,
			    int power_cond, int timer, int enable, int save,
			    int delayed_entry, int hold, int power_src,
			    int restore_src);

static void
epc_print_pcl_desc(struct ata_power_cond_log_desc *desc, const char *prefix)
{
	int first;
	unsigned int i,	num_printed, max_chars;

	first = 1;
	max_chars = 75;

	num_printed = printf("%sFlags: ", prefix);
	for (i = 0; i < (sizeof(epc_flags) / sizeof(epc_flags[0])); i++) {
		if ((desc->flags & epc_flags[i].value) == 0)
			continue;
		if (first == 0) {
			num_printed += printf(", ");
		}
		if ((num_printed + strlen(epc_flags[i].name)) > max_chars) {
			printf("\n");
			num_printed = printf("%s       ", prefix);
		}
		num_printed += printf("%s", epc_flags[i].name);
		first = 0;
	}
	if (first != 0)
		printf("None");
	printf("\n");

	printf("%sDefault timer setting: %.1f sec\n", prefix,
	    (double)(le32dec(desc->default_timer) / 10));
	printf("%sSaved timer setting: %.1f sec\n", prefix,
	    (double)(le32dec(desc->saved_timer) / 10));
	printf("%sCurrent timer setting: %.1f sec\n", prefix,
	    (double)(le32dec(desc->current_timer) / 10));
	printf("%sNominal time to active: %.1f sec\n", prefix,
	    (double)(le32dec(desc->nom_time_to_active) / 10));
	printf("%sMinimum timer: %.1f sec\n", prefix,
	    (double)(le32dec(desc->min_timer) / 10));
	printf("%sMaximum timer: %.1f sec\n", prefix,
	    (double)(le32dec(desc->max_timer) / 10));
	printf("%sNumber of transitions to power condition: %u\n", prefix,
	    le32dec(desc->num_transitions_to_pc));
	printf("%sHours in power condition: %u\n", prefix,
	    le32dec(desc->hours_in_pc));
}

static int
epc_list(struct cam_device *device, camcontrol_devtype devtype, union ccb *ccb,
	 int retry_count, int timeout)
{
	struct ata_power_cond_log_idle *idle_log;
	struct ata_power_cond_log_standby *standby_log;
	uint8_t log_buf[sizeof(*idle_log) + sizeof(*standby_log)];
	uint16_t log_addr = ATA_POWER_COND_LOG;
	uint16_t page_number = ATA_PCL_IDLE;
	uint64_t lba;
	int error = 0;

	lba = (((uint64_t)page_number & 0xff00) << 32) |
	      ((page_number & 0x00ff) << 8) |
	      (log_addr & 0xff);

	error = build_ata_cmd(ccb,
	    /*retry_count*/ retry_count,
	    /*flags*/ CAM_DIR_IN | CAM_DEV_QFRZDIS,
	    /*tag_action*/ MSG_SIMPLE_Q_TAG,
	    /*protocol*/ AP_PROTO_DMA | AP_EXTEND,
	    /*ata_flags*/ AP_FLAG_BYT_BLOK_BLOCKS |
			  AP_FLAG_TLEN_SECT_CNT |
			  AP_FLAG_TDIR_FROM_DEV,
	    /*features*/ 0,
	    /*sector_count*/ 2,
	    /*lba*/ lba,
	    /*command*/ ATA_READ_LOG_DMA_EXT,
	    /*auxiliary*/ 0,
	    /*data_ptr*/ log_buf,
	    /*dxfer_len*/ sizeof(log_buf),
	    /*cdb_storage*/ NULL,
	    /*cdb_storage_len*/ 0,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 60000,
	    /*is48bit*/ 1,
	    /*devtype*/ devtype);

	if (error != 0) {
		warnx("%s: build_ata_cmd() failed, likely programmer error",
		    __func__);
		goto bailout;
	}

	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending ATA READ LOG EXT CCB");
		error = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL,stderr);
		error = 1;
		goto bailout;
	}

	idle_log = (struct ata_power_cond_log_idle *)log_buf;
	standby_log =
	    (struct ata_power_cond_log_standby *)&log_buf[sizeof(*idle_log)];

	printf("ATA Power Conditions Log:\n");
	printf("  Idle power conditions page:\n");
	printf("    Idle A condition:\n");
	epc_print_pcl_desc(&idle_log->idle_a_desc, "      ");
	printf("    Idle B condition:\n");
	epc_print_pcl_desc(&idle_log->idle_b_desc, "      ");
	printf("    Idle C condition:\n");
	epc_print_pcl_desc(&idle_log->idle_c_desc, "      ");
	printf("  Standby power conditions page:\n");
	printf("    Standby Y condition:\n");
	epc_print_pcl_desc(&standby_log->standby_y_desc, "      ");
	printf("    Standby Z condition:\n");
	epc_print_pcl_desc(&standby_log->standby_z_desc, "      ");
bailout:
	return (error);
}

static int
epc_getmode(struct cam_device *device, camcontrol_devtype devtype,
	    union ccb *ccb, int retry_count, int timeout, int power_only)
{
	struct ata_params *ident = NULL;
	struct ata_identify_log_sup_cap sup_cap;
	const char *mode_name = NULL;
	uint8_t error = 0, ata_device = 0, status = 0;
	uint16_t count = 0;
	uint64_t lba = 0;
	uint32_t page_number, log_address;
	uint64_t caps = 0;
	int avail_bytes = 0;
	int res_available = 0;
	int retval;

	retval = 0;

	if (power_only != 0)
		goto check_power_mode;

	/*
	 * Get standard ATA Identify data.
	 */
	retval = ata_do_identify(device, retry_count, timeout, ccb, &ident);
	if (retval != 0) {
		warnx("Couldn't get identify data");
		goto bailout;
	}

	/*
	 * Get the ATA Identify Data Log (0x30),
	 * Supported Capabilities Page (0x03).
	 */
	log_address = ATA_IDENTIFY_DATA_LOG;
	page_number = ATA_IDL_SUP_CAP;
	lba = (((uint64_t)page_number & 0xff00) << 32) |
	       ((page_number & 0x00ff) << 8) |
	       (log_address & 0xff);

	bzero(&sup_cap, sizeof(sup_cap));
	/*
	 * XXX KDM check the supported protocol.
	 */
	retval = build_ata_cmd(ccb,
	    /*retry_count*/ retry_count,
	    /*flags*/ CAM_DIR_IN | CAM_DEV_QFRZDIS,
	    /*tag_action*/ MSG_SIMPLE_Q_TAG,
	    /*protocol*/ AP_PROTO_DMA |
			 AP_EXTEND,
	    /*ata_flags*/ AP_FLAG_BYT_BLOK_BLOCKS |
			  AP_FLAG_TLEN_SECT_CNT |
			  AP_FLAG_TDIR_FROM_DEV,
	    /*features*/ 0,
	    /*sector_count*/ 1,
	    /*lba*/ lba,
	    /*command*/ ATA_READ_LOG_DMA_EXT,
	    /*auxiliary*/ 0,
	    /*data_ptr*/ (uint8_t *)&sup_cap,
	    /*dxfer_len*/ sizeof(sup_cap), 
	    /*cdb_storage*/ NULL,
	    /*cdb_storage_len*/ 0,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 60000,
	    /*is48bit*/ 1,
	    /*devtype*/ devtype);

	if (retval != 0) {
		warnx("%s: build_ata_cmd() failed, likely a programmer error",
		    __func__);
		goto bailout;
	}

	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	retval = cam_send_ccb(device, ccb);
	if (retval != 0) {
		warn("error sending ATA READ LOG CCB");
		retval = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL,stderr);
		retval = 1;
		goto bailout;
	}

	if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
		avail_bytes = ccb->csio.dxfer_len - ccb->csio.resid;
	} else {
		avail_bytes = ccb->ataio.dxfer_len - ccb->ataio.resid;
	}
	if (avail_bytes < (int)sizeof(sup_cap)) {
		warnx("Couldn't get enough of the ATA Supported "
		    "Capabilities log, %d bytes returned", avail_bytes);
		retval = 1;
		goto bailout;
	}
	caps = le64dec(sup_cap.sup_cap);
	if ((caps & ATA_SUP_CAP_VALID) == 0) {
		warnx("Supported capabilities bits are not valid");
		retval = 1;
		goto bailout;
	}

	printf("APM: %sSupported, %sEnabled\n",
	    (ident->support.command2 & ATA_SUPPORT_APM) ? "" : "NOT ",
	    (ident->enabled.command2 & ATA_SUPPORT_APM) ? "" : "NOT ");
	printf("EPC: %sSupported, %sEnabled\n",
	    (ident->support2 & ATA_SUPPORT_EPC) ? "" : "NOT ",
	    (ident->enabled2 & ATA_ENABLED_EPC) ? "" : "NOT ");
	printf("Low Power Standby %sSupported\n",
	    (caps & ATA_SC_LP_STANDBY_SUP) ? "" : "NOT ");
	printf("Set EPC Power Source %sSupported\n",
	    (caps & ATA_SC_SET_EPC_PS_SUP) ? "" : "NOT ");
	

check_power_mode:

	retval = build_ata_cmd(ccb,
	    /*retry_count*/ retry_count,
	    /*flags*/ CAM_DIR_NONE | CAM_DEV_QFRZDIS,
	    /*tag_action*/ MSG_SIMPLE_Q_TAG,
	    /*protocol*/ AP_PROTO_NON_DATA |
			 AP_EXTEND,
	    /*ata_flags*/ AP_FLAG_BYT_BLOK_BLOCKS |
			  AP_FLAG_TLEN_NO_DATA |
			  AP_FLAG_CHK_COND,
	    /*features*/ ATA_SF_EPC,
	    /*sector_count*/ 0,
	    /*lba*/ 0,
	    /*command*/ ATA_CHECK_POWER_MODE,
	    /*auxiliary*/ 0,
	    /*data_ptr*/ NULL,
	    /*dxfer_len*/ 0, 
	    /*cdb_storage*/ NULL,
	    /*cdb_storage_len*/ 0,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 60000,
	    /*is48bit*/ 0,
	    /*devtype*/ devtype);

	if (retval != 0) {
		warnx("%s: build_ata_cmd() failed, likely a programmer error",
		    __func__);
		goto bailout;
	}

	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	retval = cam_send_ccb(device, ccb);
	if (retval != 0) {
		warn("error sending ATA CHECK POWER MODE CCB");
		retval = 1;
		goto bailout;
	}

	/*
	 * Check to see whether we got the requested ATA result if this
	 * is an SCSI ATA PASS-THROUGH command.
	 */
	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_SCSI_STATUS_ERROR)
	 && (ccb->csio.scsi_status == SCSI_STATUS_CHECK_COND)) {
		int error_code, sense_key, asc, ascq;

		retval = scsi_extract_sense_ccb(ccb, &error_code,
		    &sense_key, &asc, &ascq);
		if (retval == 0) {
			cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL,
			    stderr);
			retval = 1;
			goto bailout;
		}
		if ((sense_key == SSD_KEY_RECOVERED_ERROR)
		 && (asc == 0x00)
		 && (ascq == 0x1d)) {
			res_available = 1;
		}
		
	}
	if (((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
	 && (res_available == 0)) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL,stderr);
		retval = 1;
		goto bailout;
	}

	retval = get_ata_status(device, ccb, &error, &count, &lba, &ata_device,
	    &status);
	if (retval != 0) {
		warnx("Unable to get ATA CHECK POWER MODE result");
		retval = 1;
		goto bailout;
	}

	mode_name = scsi_nv_to_str(epc_power_cond_map,
	    sizeof(epc_power_cond_map) / sizeof(epc_power_cond_map[0]), count);
	printf("Current power state: ");
	/* Note: ident can be null in power_only mode */
	if ((ident == NULL)
	 || (ident->enabled2 & ATA_ENABLED_EPC)) {
		if (mode_name != NULL)
			printf("%s", mode_name);
		else if (count == 0xff) {
			printf("PM0:Active or PM1:Idle");
		}
	} else {
		switch (count) {
		case 0x00:
			printf("PM2:Standby");
			break;
		case 0x80:
			printf("PM1:Idle");
			break;
		case 0xff:
			printf("PM0:Active or PM1:Idle");
			break;
		}
	}
	printf("(0x%02x)\n", count);

	if (power_only != 0)
		goto bailout;

	if (caps & ATA_SC_LP_STANDBY_SUP) {
		uint32_t wait_mode;

		wait_mode = (lba >> 20) & 0xff;
		if (wait_mode == 0xff) {
			printf("Device not waiting to enter lower power "
			    "condition");
		} else {
			mode_name = scsi_nv_to_str(epc_power_cond_map,
			    sizeof(epc_power_cond_map) /
			    sizeof(epc_power_cond_map[0]), wait_mode);
			printf("Device waiting to enter mode %s (0x%02x)\n",
			    (mode_name != NULL) ? mode_name : "Unknown",
			    wait_mode);
		}
		printf("Device is %sheld in the current power condition\n",
		    (lba & 0x80000) ? "" : "NOT ");
	}
bailout:
	return (retval);

}

static int
epc_set_features(struct cam_device *device, camcontrol_devtype devtype,
		 union ccb *ccb, int retry_count, int timeout, int action,
		 int power_cond, int timer, int enable, int save,
		 int delayed_entry, int hold, int power_src, int restore_src)
{
	uint64_t lba;
	uint16_t count = 0;
	int error;

	error = 0;

	lba = action;

	switch (action) {
	case ATA_SF_EPC_SET_TIMER:
		lba |= ((timer << ATA_SF_EPC_TIMER_SHIFT) &
			 ATA_SF_EPC_TIMER_MASK);
		/* FALLTHROUGH */
	case ATA_SF_EPC_SET_STATE:
		lba |= (enable ? ATA_SF_EPC_TIMER_EN : 0) |
		       (save ? ATA_SF_EPC_TIMER_SAVE : 0);
		count = power_cond;
		break;
	case ATA_SF_EPC_GOTO:
		count = power_cond;
		lba |= (delayed_entry ? ATA_SF_EPC_GOTO_DELAY : 0) |
		       (hold ? ATA_SF_EPC_GOTO_HOLD : 0);
		break;
	case ATA_SF_EPC_RESTORE:
		lba |= restore_src |
		       (save ? ATA_SF_EPC_RST_SAVE : 0);
		break;
	case ATA_SF_EPC_ENABLE:
	case ATA_SF_EPC_DISABLE:
		break;
	case ATA_SF_EPC_SET_SOURCE:
		count = power_src;
		break;
	}

	error = build_ata_cmd(ccb,
	    /*retry_count*/ retry_count,
	    /*flags*/ CAM_DIR_NONE | CAM_DEV_QFRZDIS,
	    /*tag_action*/ MSG_SIMPLE_Q_TAG,
	    /*protocol*/ AP_PROTO_NON_DATA | AP_EXTEND,
	    /*ata_flags*/ AP_FLAG_BYT_BLOK_BLOCKS |
			  AP_FLAG_TLEN_NO_DATA |
			  AP_FLAG_TDIR_FROM_DEV,
	    /*features*/ ATA_SF_EPC,
	    /*sector_count*/ count,
	    /*lba*/ lba,
	    /*command*/ ATA_SETFEATURES,
	    /*auxiliary*/ 0,
	    /*data_ptr*/ NULL,
	    /*dxfer_len*/ 0, 
	    /*cdb_storage*/ NULL,
	    /*cdb_storage_len*/ 0,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 60000,
	    /*is48bit*/ 1,
	    /*devtype*/ devtype);

	if (error != 0) {
		warnx("%s: build_ata_cmd() failed, likely a programmer error",
		    __func__);
		goto bailout;
	}

	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending ATA SET FEATURES CCB");
		error = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL,stderr);
		error = 1;
		goto bailout;
	}

bailout:
	return (error);
}

int
epc(struct cam_device *device, int argc, char **argv, char *combinedopt,
    int retry_count, int timeout, int verbosemode __unused)
{
	union ccb *ccb = NULL;
	int error = 0;
	int c;
	int action = -1;
	camcontrol_devtype devtype;
	double timer_val = -1;
	int timer_tenths = 0, power_cond = -1;
	int delayed_entry = 0, hold = 0;
	int enable = -1, save = 0;
	int restore_src = -1;
	int power_src = -1;
	int power_only = 0;


	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'c': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(epc_cmd_map,
			    (sizeof(epc_cmd_map) / sizeof(epc_cmd_map[0])),
			    optarg, &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				action = epc_cmd_map[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "epc command",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'd':
			enable = 0;
			break;
		case 'D':
			delayed_entry = 1;
			break;
		case 'e':
			enable = 1;
			break;
		case 'H':
			hold = 1;
			break;
		case 'p': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(epc_power_cond_map,
			    (sizeof(epc_power_cond_map) /
			     sizeof(epc_power_cond_map[0])), optarg,
			     &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				power_cond =epc_power_cond_map[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "power condition",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'P':
			power_only = 1;
			break;
		case 'r': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(epc_rst_val,
			    (sizeof(epc_rst_val) /
			     sizeof(epc_rst_val[0])), optarg,
			     &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				restore_src = epc_rst_val[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid",
				    "restore value source", optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 's':
			save = 1;
			break;
		case 'S': {
			scsi_nv_status status;
			int entry_num;
			
			status = scsi_get_nv(epc_ps_map,
			    (sizeof(epc_ps_map) / sizeof(epc_ps_map[0])),
			    optarg, &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				power_src = epc_ps_map[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "power source",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'T': {
			char *endptr;

			timer_val = strtod(optarg, &endptr);
			if (timer_val < 0) {
				warnx("Invalid timer value %f", timer_val);
				error = 1;
				goto bailout;
			} else if (*endptr != '\0') {
				warnx("Invalid timer value %s", optarg);
				error = 1;
				goto bailout;
			}
			timer_tenths = timer_val * 10;
			break;
		}
		default:
			break;
		}
	}

	if (action == -1) {
		warnx("Must specify an action");
		error = 1;
		goto bailout;
	}
	
	error = get_device_type(device, retry_count, timeout,
	    /*printerrors*/ 1, &devtype);
	if (error != 0)
		errx(1, "Unable to determine device type");

	switch (devtype) {
	case CC_DT_ATA:
	case CC_DT_ATA_BEHIND_SCSI:
		break;
	default:
		warnx("The epc subcommand only works with ATA protocol "
		    "devices");
		error = 1;
		goto bailout;
		break; /*NOTREACHED*/
	}

	switch (action) {
	case ATA_SF_EPC_SET_TIMER:
		if (timer_val == -1) {
			warnx("Must specify a timer value (-T time)");
			error = 1;
		}
		/* FALLTHROUGH */
	case ATA_SF_EPC_SET_STATE:
		if (enable == -1) {
			warnx("Must specify enable (-e) or disable (-d)");
			error = 1;
		}
		/* FALLTHROUGH */
	case ATA_SF_EPC_GOTO:
		if (power_cond == -1) {
			warnx("Must specify a power condition with -p");
			error = 1;
		}
		if (error != 0)
			goto bailout;
		break;
	case ATA_SF_EPC_SET_SOURCE:
		if (power_src == -1) {
			warnx("Must specify a power source (-S battery or "
			    "-S notbattery) value");
			error = 1;
			goto bailout;
		}
		break;
	case ATA_SF_EPC_RESTORE:
		if (restore_src == -1) {
			warnx("Must specify a source for restored value, "
			    "-r default or -r saved");
			error = 1;
			goto bailout;
		}
		break;
	case ATA_SF_EPC_ENABLE:
	case ATA_SF_EPC_DISABLE:
	case CCTL_EPC_GET_STATUS:
	case CCTL_EPC_LIST:
	default:
		break;
	}

	switch (action) {
	case CCTL_EPC_GET_STATUS:
		error = epc_getmode(device, devtype, ccb, retry_count, timeout,
		    power_only);
		break;
	case CCTL_EPC_LIST:
		error = epc_list(device, devtype, ccb, retry_count, timeout);
		break;
	case ATA_SF_EPC_RESTORE:
	case ATA_SF_EPC_GOTO:
	case ATA_SF_EPC_SET_TIMER:
	case ATA_SF_EPC_SET_STATE:
	case ATA_SF_EPC_ENABLE:
	case ATA_SF_EPC_DISABLE:
	case ATA_SF_EPC_SET_SOURCE:
		error = epc_set_features(device, devtype, ccb, retry_count,
		    timeout, action, power_cond, timer_tenths, enable, save,
		    delayed_entry, hold, power_src, restore_src);
		break;
	default:
		warnx("Not implemented yet");
		error = 1;
		goto bailout;
		break;
	}


bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return (error);
}
