/*-
 * Copyright (c) 2015, 2016 Spectra Logic Corporation
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
 * SCSI and ATA Shingled Media Recording (SMR) support for camcontrol(8).
 * This is an implementation of the SCSI ZBC and ATA ZAC specs.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/chio.h>

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
#include <cam/scsi/scsi_ch.h>
#include <cam/scsi/scsi_message.h>
#include <camlib.h>
#include "camcontrol.h"

static struct scsi_nv zone_cmd_map[] = {
	{ "rz", ZBC_IN_SA_REPORT_ZONES },
	{ "reportzones", ZBC_IN_SA_REPORT_ZONES },
	{ "close", ZBC_OUT_SA_CLOSE },
	{ "finish", ZBC_OUT_SA_FINISH },
	{ "open", ZBC_OUT_SA_OPEN },
	{ "rwp", ZBC_OUT_SA_RWP }
};

static struct scsi_nv zone_rep_opts[] = {
	{ "all", ZBC_IN_REP_ALL_ZONES },
	{ "empty", ZBC_IN_REP_EMPTY },
	{ "imp_open", ZBC_IN_REP_IMP_OPEN },
	{ "exp_open", ZBC_IN_REP_EXP_OPEN },
	{ "closed", ZBC_IN_REP_CLOSED },
	{ "full", ZBC_IN_REP_FULL },
	{ "readonly", ZBC_IN_REP_READONLY },
	{ "ro", ZBC_IN_REP_READONLY },
	{ "offline", ZBC_IN_REP_OFFLINE },
	{ "rwp", ZBC_IN_REP_RESET },
	{ "reset", ZBC_IN_REP_RESET },
	{ "nonseq", ZBC_IN_REP_NON_SEQ },
	{ "nonwp", ZBC_IN_REP_NON_WP }
};

typedef enum {
	ZONE_OF_NORMAL	= 0x00,
	ZONE_OF_SUMMARY	= 0x01,
	ZONE_OF_SCRIPT	= 0x02
} zone_output_flags;

static struct scsi_nv zone_print_opts[] = {
	{ "normal", ZONE_OF_NORMAL },
	{ "summary", ZONE_OF_SUMMARY },
	{ "script", ZONE_OF_SCRIPT }
};

#define	ZAC_ATA_SECTOR_COUNT(bcount)	(((bcount) / 512) & 0xffff)

typedef enum {
	ZONE_PRINT_OK,
	ZONE_PRINT_MORE_DATA,
	ZONE_PRINT_ERROR
} zone_print_status;

typedef enum {
	ZONE_FW_START,
	ZONE_FW_LEN,
	ZONE_FW_WP,
	ZONE_FW_TYPE,
	ZONE_FW_COND,
	ZONE_FW_SEQ,
	ZONE_FW_RESET,
	ZONE_NUM_FIELDS
} zone_field_widths;

zone_print_status zone_rz_print(uint8_t *data_ptr, uint32_t valid_len,
				int ata_format, zone_output_flags out_flags,
				int first_pass, uint64_t *next_start_lba);


zone_print_status
zone_rz_print(uint8_t *data_ptr, uint32_t valid_len, int ata_format,
	      zone_output_flags out_flags, int first_pass,
	      uint64_t *next_start_lba)
{
	struct scsi_report_zones_hdr *hdr = NULL;
	struct scsi_report_zones_desc *desc = NULL;
	uint32_t hdr_len, len;
	uint64_t max_lba, next_lba = 0;
	int more_data = 0;
	zone_print_status status = ZONE_PRINT_OK;
	char tmpstr[80];
	int field_widths[ZONE_NUM_FIELDS];
	char word_sep;

	if (valid_len < sizeof(*hdr)) {
		status = ZONE_PRINT_ERROR;
		goto bailout;
	}

	hdr = (struct scsi_report_zones_hdr *)data_ptr;

	field_widths[ZONE_FW_START] = 11;
	field_widths[ZONE_FW_LEN] = 6;
	field_widths[ZONE_FW_WP] = 11;
	field_widths[ZONE_FW_TYPE] = 13;
	field_widths[ZONE_FW_COND] = 13;
	field_widths[ZONE_FW_SEQ] = 14;
	field_widths[ZONE_FW_RESET] = 16;

	if (ata_format == 0) {
		hdr_len = scsi_4btoul(hdr->length);
		max_lba = scsi_8btou64(hdr->maximum_lba);
	} else {
		hdr_len = le32dec(hdr->length);
		max_lba = le64dec(hdr->maximum_lba);
	}

	if (hdr_len > (valid_len + sizeof(*hdr))) {
		more_data = 1;
		status = ZONE_PRINT_MORE_DATA;
	}

	len = MIN(valid_len - sizeof(*hdr), hdr_len);

	if (out_flags == ZONE_OF_SCRIPT)
		word_sep = '_';
	else
		word_sep = ' ';

	if ((out_flags != ZONE_OF_SCRIPT)
	 && (first_pass != 0)) {
		printf("%zu zones, Maximum LBA %#jx (%ju)\n",
		    hdr_len / sizeof(*desc), (uintmax_t)max_lba,
		    (uintmax_t)max_lba);

		switch (hdr->byte4 & SRZ_SAME_MASK) {
		case SRZ_SAME_ALL_DIFFERENT:
			printf("Zone lengths and types may vary\n");
			break;
		case SRZ_SAME_ALL_SAME:
			printf("Zone lengths and types are all the same\n");
			break;
		case SRZ_SAME_LAST_DIFFERENT:
			printf("Zone types are the same, last zone length "
			    "differs\n");
			break;
		case SRZ_SAME_TYPES_DIFFERENT:
			printf("Zone lengths are the same, types vary\n");
			break;
		default:
			printf("Unknown SAME field value %#x\n",
			    hdr->byte4 & SRZ_SAME_MASK);
			break;
		}
	}
	if (out_flags == ZONE_OF_SUMMARY) {
		status = ZONE_PRINT_OK;
		goto bailout;
	}

	if ((out_flags == ZONE_OF_NORMAL)
	 && (first_pass != 0)) {
		printf("%*s  %*s  %*s  %*s  %*s  %*s  %*s\n",
		    field_widths[ZONE_FW_START], "Start LBA",
		    field_widths[ZONE_FW_LEN], "Length",
		    field_widths[ZONE_FW_WP], "WP LBA",
		    field_widths[ZONE_FW_TYPE], "Zone Type",
		    field_widths[ZONE_FW_COND], "Condition",
		    field_widths[ZONE_FW_SEQ], "Sequential",
		    field_widths[ZONE_FW_RESET], "Reset");
	}

	for (desc = &hdr->desc_list[0]; len >= sizeof(*desc);
	     len -= sizeof(*desc), desc++) {
		uint64_t length, start_lba, wp_lba;

		if (ata_format == 0) {
			length = scsi_8btou64(desc->zone_length);
			start_lba = scsi_8btou64(desc->zone_start_lba);
			wp_lba = scsi_8btou64(desc->write_pointer_lba);
		} else {
			length = le64dec(desc->zone_length);
			start_lba = le64dec(desc->zone_start_lba);
			wp_lba = le64dec(desc->write_pointer_lba);
		}

		printf("%#*jx, %*ju, %#*jx, ", field_widths[ZONE_FW_START],
		    (uintmax_t)start_lba, field_widths[ZONE_FW_LEN],
		    (uintmax_t)length, field_widths[ZONE_FW_WP],
		    (uintmax_t)wp_lba);

		switch (desc->zone_type & SRZ_TYPE_MASK) {
		case SRZ_TYPE_CONVENTIONAL:
			snprintf(tmpstr, sizeof(tmpstr), "Conventional");
			break;
		case SRZ_TYPE_SEQ_PREFERRED:
		case SRZ_TYPE_SEQ_REQUIRED:
			snprintf(tmpstr, sizeof(tmpstr), "Seq%c%s",
			    word_sep, ((desc->zone_type & SRZ_TYPE_MASK) ==
			    SRZ_TYPE_SEQ_PREFERRED) ? "Preferred" :
			    "Required");
			break;
		default:
			snprintf(tmpstr, sizeof(tmpstr), "Zone%ctype%c%#x",
			    word_sep, word_sep,desc->zone_type &
			    SRZ_TYPE_MASK);
			break;
		}
		printf("%*s, ", field_widths[ZONE_FW_TYPE], tmpstr);

		switch (desc->zone_flags & SRZ_ZONE_COND_MASK) {
		case SRZ_ZONE_COND_NWP:
			snprintf(tmpstr, sizeof(tmpstr), "NWP");
			break;
		case SRZ_ZONE_COND_EMPTY:
			snprintf(tmpstr, sizeof(tmpstr), "Empty");
			break;
		case SRZ_ZONE_COND_IMP_OPEN:
			snprintf(tmpstr, sizeof(tmpstr), "Implicit%cOpen", 
			    word_sep);
			break;
		case SRZ_ZONE_COND_EXP_OPEN:
			snprintf(tmpstr, sizeof(tmpstr), "Explicit%cOpen",
			    word_sep);
			break;
		case SRZ_ZONE_COND_CLOSED:
			snprintf(tmpstr, sizeof(tmpstr), "Closed");
			break;
		case SRZ_ZONE_COND_READONLY:
			snprintf(tmpstr, sizeof(tmpstr), "Readonly");
			break;
		case SRZ_ZONE_COND_FULL:
			snprintf(tmpstr, sizeof(tmpstr), "Full");
			break;
		case SRZ_ZONE_COND_OFFLINE:
			snprintf(tmpstr, sizeof(tmpstr), "Offline");
			break;
		default:
			snprintf(tmpstr, sizeof(tmpstr), "%#x",
			    desc->zone_flags & SRZ_ZONE_COND_MASK);
			break;
		}

		printf("%*s, ", field_widths[ZONE_FW_COND], tmpstr);

		if (desc->zone_flags & SRZ_ZONE_NON_SEQ)
			snprintf(tmpstr, sizeof(tmpstr), "Non%cSequential",
			    word_sep);
		else
			snprintf(tmpstr, sizeof(tmpstr), "Sequential");

		printf("%*s, ", field_widths[ZONE_FW_SEQ], tmpstr);

		if (desc->zone_flags & SRZ_ZONE_RESET)
			snprintf(tmpstr, sizeof(tmpstr), "Reset%cNeeded",
			    word_sep);
		else
			snprintf(tmpstr, sizeof(tmpstr), "No%cReset%cNeeded",
			    word_sep, word_sep);

		printf("%*s\n", field_widths[ZONE_FW_RESET], tmpstr);
		
		next_lba = start_lba + length;
	}
bailout:
	*next_start_lba = next_lba;

	return (status);
}

int
zone(struct cam_device *device, int argc, char **argv, char *combinedopt,
     int task_attr, int retry_count, int timeout, int verbosemode __unused)
{
	union ccb *ccb = NULL;
	int action = -1, rep_option = -1;
	int all_zones = 0;
	uint64_t lba = 0;
	int error = 0;
	uint8_t *data_ptr = NULL;
	uint32_t alloc_len = 65536, valid_len = 0;
	camcontrol_devtype devtype;
	int ata_format = 0, use_ncq = 0;
	int first_pass = 1;
	zone_print_status zp_status;
	zone_output_flags out_flags = ZONE_OF_NORMAL;
	uint8_t *cdb_storage = NULL;
	int cdb_storage_len = 32;
	int c;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(ccb);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			all_zones = 1;
			break;
		case 'c': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(zone_cmd_map,
			    (sizeof(zone_cmd_map) / sizeof(zone_cmd_map[0])),
			    optarg, &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				action = zone_cmd_map[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "zone command",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'l': {
			char *endptr;

			lba = strtoull(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid lba argument %s", __func__,
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'N':
			use_ncq = 1;
			break;
		case 'o': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(zone_rep_opts,
			    (sizeof(zone_rep_opts) /sizeof(zone_rep_opts[0])),
			    optarg, &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				rep_option = zone_rep_opts[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "report zones",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'P': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(zone_print_opts,
			    (sizeof(zone_print_opts) /
			    sizeof(zone_print_opts[0])), optarg, &entry_num,
			    SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				out_flags = zone_print_opts[entry_num].value;
			else {
				warnx("%s: %s: %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "print",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		default:
			break;
		}
	}
	if (action == -1) {
		warnx("%s: must specify -c <zone_cmd>", __func__);
		error = 1;
		goto bailout;
	}
	error = get_device_type(device, retry_count, timeout,
	    /*printerrors*/ 1, &devtype);
	if (error != 0)
		errx(1, "Unable to determine device type");

	if (action == ZBC_IN_SA_REPORT_ZONES) {

		data_ptr = malloc(alloc_len);
		if (data_ptr == NULL)
			err(1, "unable to allocate %u bytes", alloc_len);

restart_report:
		bzero(data_ptr, alloc_len);
		
		switch (devtype) {
		case CC_DT_SCSI:
			scsi_zbc_in(&ccb->csio,
			    /*retries*/ retry_count,
			    /*cbfcnp*/ NULL,
			    /*tag_action*/ task_attr,
			    /*service_action*/ action,
			    /*zone_start_lba*/ lba,
			    /*zone_options*/ (rep_option != -1) ?
					      rep_option : 0,
			    /*data_ptr*/ data_ptr,
			    /*dxfer_len*/ alloc_len,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ timeout ? timeout : 60000);
			break;
		case CC_DT_ATA:
		case CC_DT_ATA_BEHIND_SCSI: {
			uint8_t command = 0;
			uint8_t protocol = 0;
			uint16_t features = 0, sector_count = 0;
			uint32_t auxiliary = 0;

			/*
			 * XXX KDM support the partial bit?
			 */
			if (use_ncq == 0) {
				command = ATA_ZAC_MANAGEMENT_IN;
				features = action;
				if (rep_option != -1)
					features |= (rep_option << 8);	
				sector_count = ZAC_ATA_SECTOR_COUNT(alloc_len);
				protocol = AP_PROTO_DMA;
			} else {
				if (cdb_storage == NULL)
					cdb_storage = calloc(cdb_storage_len, 1);
				if (cdb_storage == NULL)
					err(1, "couldn't allocate memory");

				command = ATA_RECV_FPDMA_QUEUED;
				features = ZAC_ATA_SECTOR_COUNT(alloc_len);
				sector_count = ATA_RFPDMA_ZAC_MGMT_IN << 8;
				auxiliary = action & 0xf;
				if (rep_option != -1)
					auxiliary |= rep_option << 8;
				protocol = AP_PROTO_FPDMA;
			}

			error = build_ata_cmd(ccb,
			    /*retry_count*/ retry_count,
			    /*flags*/ CAM_DIR_IN | CAM_DEV_QFRZDIS,
			    /*tag_action*/ task_attr,
			    /*protocol*/ protocol,
			    /*ata_flags*/ AP_FLAG_BYT_BLOK_BLOCKS |
					  AP_FLAG_TLEN_SECT_CNT |
					  AP_FLAG_TDIR_FROM_DEV,
			    /*features*/ features,
			    /*sector_count*/ sector_count,
			    /*lba*/ lba,
			    /*command*/ command,
			    /*auxiliary*/ auxiliary,
			    /*data_ptr*/ data_ptr,
			    /*dxfer_len*/ ZAC_ATA_SECTOR_COUNT(alloc_len)*512,
			    /*cdb_storage*/ cdb_storage,
			    /*cdb_storage_len*/ cdb_storage_len,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ timeout ? timeout : 60000,
			    /*is48bit*/ 1,
			    /*devtype*/ devtype);

			if (error != 0) {
				warnx("%s: build_ata_cmd() failed, likely "
				    "programmer error", __func__);
				goto bailout;
			}

			ata_format = 1;

			break;
		}
		default:
			warnx("%s: Unknown device type %d", __func__,devtype);
			error = 1;
			goto bailout;
			break; /*NOTREACHED*/
		}
	} else {
		/*
		 * XXX KDM the current methodology is to always send ATA
		 * commands to ATA devices.  Need to figure out how to
		 * detect whether a SCSI to ATA translation layer will
		 * translate ZBC IN/OUT commands to the appropriate ZAC
		 * command.
		 */
		switch (devtype) {
		case CC_DT_SCSI:
			scsi_zbc_out(&ccb->csio,
			    /*retries*/ retry_count,
			    /*cbfcnp*/ NULL,
			    /*tag_action*/ task_attr,
			    /*service_action*/ action,
			    /*zone_id*/ lba,
			    /*zone_flags*/ (all_zones != 0) ? ZBC_OUT_ALL : 0,
			    /*data_ptr*/ NULL,
			    /*dxfer_len*/ 0,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ timeout ? timeout : 60000);
			break;
		case CC_DT_ATA:
		case CC_DT_ATA_BEHIND_SCSI: {
			uint8_t command = 0;
			uint8_t protocol = 0;
			uint16_t features = 0, sector_count = 0;
			uint32_t auxiliary = 0;

			/*
			 * Note that we're taking advantage of the fact
			 * that the action numbers are the same between the
			 * ZBC and ZAC specs.
			 */

			if (use_ncq == 0) {
				protocol = AP_PROTO_NON_DATA;
				command = ATA_ZAC_MANAGEMENT_OUT;
				features = action & 0xf;
				if (all_zones != 0)
					features |= (ZBC_OUT_ALL << 8);
			} else {
				cdb_storage = calloc(cdb_storage_len, 1);
				if (cdb_storage == NULL)
					err(1, "couldn't allocate memory");

				protocol = AP_PROTO_FPDMA;
				command = ATA_NCQ_NON_DATA;
				features = ATA_NCQ_ZAC_MGMT_OUT;
				auxiliary = action & 0xf;
				if (all_zones != 0)
					auxiliary |= (ZBC_OUT_ALL << 8);
			}


			error = build_ata_cmd(ccb,
			    /*retry_count*/ retry_count,
			    /*flags*/ CAM_DIR_NONE | CAM_DEV_QFRZDIS,
			    /*tag_action*/ task_attr,
			    /*protocol*/ AP_PROTO_NON_DATA,
			    /*ata_flags*/ AP_FLAG_BYT_BLOK_BYTES |
					  AP_FLAG_TLEN_NO_DATA,
			    /*features*/ features,
			    /*sector_count*/ sector_count,
			    /*lba*/ lba,
			    /*command*/ command,
			    /*auxiliary*/ auxiliary,
			    /*data_ptr*/ NULL,
			    /*dxfer_len*/ 0,
			    /*cdb_storage*/ cdb_storage,
			    /*cdb_storage_len*/ cdb_storage_len,
			    /*sense_len*/ SSD_FULL_SIZE,
			    /*timeout*/ timeout ? timeout : 60000,
			    /*is48bit*/ 1,
			    /*devtype*/ devtype);
			if (error != 0) {
				warnx("%s: build_ata_cmd() failed, likely "
				    "programmer error", __func__);
				goto bailout;
			}
			ata_format = 1;
			break;
		}
		default:
			warnx("%s: Unknown device type %d", __func__,devtype);
			error = 1;
			goto bailout;
			break; /*NOTREACHED*/
		}
	}

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending %s %s CCB", (devtype == CC_DT_SCSI) ?
		     "ZBC" : "ZAC Management",
		     (action == ZBC_IN_SA_REPORT_ZONES) ? "In" : "Out");
		error = -1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL, CAM_EPF_ALL,stderr);
		error = 1;
		goto bailout;
	}

	/*
	 * If we aren't reading the list of zones, we're done.
	 */
	if (action != ZBC_IN_SA_REPORT_ZONES)
		goto bailout;

	if (ccb->ccb_h.func_code == XPT_SCSI_IO)
		valid_len = ccb->csio.dxfer_len - ccb->csio.resid;
	else
		valid_len = ccb->ataio.dxfer_len - ccb->ataio.resid;

	zp_status = zone_rz_print(data_ptr, valid_len, ata_format, out_flags,
	    first_pass, &lba);

	if (zp_status == ZONE_PRINT_MORE_DATA) {
		bzero(ccb, sizeof(*ccb));
		first_pass = 0;
		if (cdb_storage != NULL)
			bzero(cdb_storage, cdb_storage_len);
		goto restart_report;
	} else if (zp_status == ZONE_PRINT_ERROR)
		error = 1;
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	free(data_ptr);
	free(cdb_storage);

	return (error);
}
