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
 *          Reid Linnemann      (Spectra Logic Corporation)
 *          Samuel Klopsch      (Spectra Logic Corporation)
 */
/*
 * SCSI tape drive timestamp support
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>
#include <time.h>
#include <locale.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <camlib.h>
#include "camcontrol.h"

#define TIMESTAMP_REPORT 0
#define TIMESTAMP_SET    1
#define MIL              "milliseconds"
#define UTC              "utc"

static int set_restore_flags(struct cam_device *device, uint8_t *flags,
			     int set_flag, int task_attr, int retry_count,
			     int timeout);
static int report_timestamp(struct cam_device *device, uint64_t *ts,
			    int task_attr, int retry_count, int timeout);
static int set_timestamp(struct cam_device *device, char *format_string,
			 char *timestamp_string, int task_attr, int retry_count,
			 int timeout);

static int
set_restore_flags(struct cam_device *device, uint8_t *flags, int set_flag,
		  int task_attr, int retry_count, int timeout)
{
	unsigned long blk_desc_length, hdr_and_blk_length;
	int error = 0;
	struct scsi_control_ext_page *control_page = NULL;
	struct scsi_mode_header_10 *mode_hdr = NULL;
	struct scsi_mode_sense_10 *cdb = NULL;
	union ccb *ccb = NULL;
	unsigned long mode_buf_size = sizeof(struct scsi_mode_header_10) +
	    sizeof(struct scsi_mode_blk_desc) +
	    sizeof(struct scsi_control_ext_page);
	uint8_t mode_buf[mode_buf_size];

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}
	/*
	 * Get the control extension subpage, we'll send it back modified to
	 * enable SCSI control over the tape drive's timestamp
	 */
	scsi_mode_sense_len(&ccb->csio,
	    /*retries*/ retry_count,
	    /*cbfcnp*/ NULL,
	    /*tag_action*/ task_attr,
	    /*dbd*/ 0,
	    /*page_control*/ SMS_PAGE_CTRL_CURRENT,
	    /*page*/ SCEP_PAGE_CODE,
	    /*param_buf*/ &mode_buf[0],
	    /*param_len*/ mode_buf_size,
	    /*minimum_cmd_size*/ 10,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 5000);
	/*
	 * scsi_mode_sense_len does not have a subpage argument at the moment,
	 * so we have to manually set the subpage code before calling
	 * cam_send_ccb().
	 */
	cdb = (struct scsi_mode_sense_10 *)ccb->csio.cdb_io.cdb_bytes;
	cdb->subpage = SCEP_SUBPAGE_CODE;

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending Mode Sense");
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		error = 1;
		goto bailout;
	}

	mode_hdr = (struct scsi_mode_header_10 *)&mode_buf[0];
	blk_desc_length = scsi_2btoul(mode_hdr->blk_desc_len);
	hdr_and_blk_length = sizeof(struct scsi_mode_header_10)+blk_desc_length;
	/*
	 * Create the control page at the correct point in the mode_buf, it
	 * starts after the header and the blk description.
	 */
	assert(hdr_and_blk_length <=
	    sizeof(mode_buf) - sizeof(struct scsi_control_ext_page));
	control_page = (struct scsi_control_ext_page *)&mode_buf
	    [hdr_and_blk_length];
	if (set_flag != 0) {
		*flags = control_page->flags;
		/*
		 * Set the SCSIP flag to enable SCSI to change the
		 * tape drive's timestamp.
		 */
		control_page->flags |= SCEP_SCSIP;
	} else {
		control_page->flags = *flags;
	}

	scsi_mode_select_len(&ccb->csio,
	    /*retries*/ retry_count,
	    /*cbfcnp*/ NULL,
	    /*tag_action*/ task_attr,
	    /*scsi_page_fmt*/ 1,
	    /*save_pages*/ 0,
	    /*param_buf*/ &mode_buf[0],
	    /*param_len*/ mode_buf_size,
	    /*minimum_cmd_size*/ 10,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 5000);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending Mode Select");
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		error = 1;
		goto bailout;
	}

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	return error;
}

static int
report_timestamp(struct cam_device *device, uint64_t *ts, int task_attr,
		 int retry_count, int timeout)
{
	int error = 0;
	struct scsi_report_timestamp_data *report_buf = malloc(
		sizeof(struct scsi_report_timestamp_data));
	uint8_t temp_timestamp[8];
	uint32_t report_buf_size = sizeof(
	    struct scsi_report_timestamp_data);
	union ccb *ccb = NULL;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}

	scsi_report_timestamp(&ccb->csio,
	    /*retries*/ retry_count,
	    /*cbfcnp*/ NULL,
	    /*tag_action*/ task_attr,
	    /*pdf*/ 0,
	    /*buf*/ report_buf,
	    /*buf_len*/ report_buf_size,
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 5000);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending Report Timestamp");
		goto bailout;
	}
	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		error = 1;
		goto bailout;
	}

	bzero(temp_timestamp, sizeof(temp_timestamp));
	memcpy(&temp_timestamp[2], &report_buf->timestamp, 6);

	*ts = scsi_8btou64(temp_timestamp);

bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);
	free(report_buf);

	return error;
}

static int
set_timestamp(struct cam_device *device, char *format_string,
	      char *timestamp_string, int task_attr, int retry_count,
	      int timeout)
{
	struct scsi_set_timestamp_parameters ts_p;
	time_t time_value;
	struct tm time_struct;
	uint8_t flags = 0;
	int error = 0;
	uint64_t ts = 0;
	union ccb *ccb = NULL;
	int do_restore_flags = 0;

	error = set_restore_flags(device, &flags, /*set_flag*/ 1, task_attr,
				  retry_count, timeout);
	if (error != 0)
		goto bailout;

	do_restore_flags = 1;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}

	if (strcmp(format_string, UTC) == 0) {
		time(&time_value);
		ts = (uint64_t) time_value;
	} else {
		bzero(&time_struct, sizeof(struct tm));
		if (strptime(timestamp_string, format_string,
		    &time_struct) == NULL) {
			warnx("%s: strptime(3) failed", __func__);
			error = 1;
			goto bailout;
		}
		time_value = mktime(&time_struct);
		ts = (uint64_t) time_value;
	}
	/* Convert time from seconds to milliseconds */
	ts *= 1000;
	bzero(&ts_p, sizeof(ts_p));
	scsi_create_timestamp(ts_p.timestamp, ts);

	scsi_set_timestamp(&ccb->csio,
	    /*retries*/ retry_count,
	    /*cbfcnp*/ NULL,
	    /*tag_action*/ task_attr,
	    /*buf*/ &ts_p,
	    /*buf_len*/ sizeof(ts_p),
	    /*sense_len*/ SSD_FULL_SIZE,
	    /*timeout*/ timeout ? timeout : 5000);

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;
	if (retry_count > 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	error = cam_send_ccb(device, ccb);
	if (error != 0) {
		warn("error sending Set Timestamp");
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		cam_error_print(device, ccb, CAM_ESF_ALL,
				CAM_EPF_ALL, stderr);
		error = 1;
		goto bailout;
	}

	printf("Timestamp set to %ju\n", (uintmax_t)ts);

bailout:
	if (do_restore_flags != 0)
		error = set_restore_flags(device, &flags, /*set_flag*/ 0,
					  task_attr, retry_count, timeout);
	if (ccb != NULL)
		cam_freeccb(ccb);

	return error;
}

int
timestamp(struct cam_device *device, int argc, char **argv, char *combinedopt,
	  int task_attr, int retry_count, int timeout, int verbosemode __unused)
{
	int c;
	uint64_t ts = 0;
	char *format_string = NULL;
	char *timestamp_string = NULL;
	int action = -1;
	int error = 0;
	int single_arg = 0;
	int do_utc = 0;

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'r': {
			if (action != -1) {
				warnx("Use only one -r or only one -s");
				error =1;
				goto bailout;
			}
			action = TIMESTAMP_REPORT;
			break;
		}
		case 's': {
			if (action != -1) {
				warnx("Use only one -r or only one -s");
				error = 1;
				goto bailout;
			}
			action = TIMESTAMP_SET;
			break;
		}
		case 'f': {
			single_arg++;
			free(format_string);
			format_string = strdup(optarg);
			if (format_string == NULL) {
				warn("Error allocating memory for format "
				   "argument");
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'm': {
			single_arg++;
			free(format_string);
			format_string = strdup(MIL);
			if (format_string == NULL) {
				warn("Error allocating memory");
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'U': {
			do_utc = 1;
			break;
		}
		case 'T':
			free(timestamp_string);
			timestamp_string = strdup(optarg);
			if (timestamp_string == NULL) {
				warn("Error allocating memory for format "
				   "argument");
				error = 1;
				goto bailout;
			}
			break;
		default:
			break;
		}
	}

	if (action == -1) {
		warnx("Must specify an action, either -r or -s");
		error = 1;
		goto bailout;
	}

	if (single_arg > 1) {
		warnx("Select only one: %s",
		    (action == TIMESTAMP_REPORT) ?
		    "-f format or -m for the -r flag" : 
		    "-f format -T time or -U for the -s flag");
		error = 1;
		goto bailout;
	}

	if (action == TIMESTAMP_SET) {
		if ((format_string == NULL)
		 && (do_utc == 0)) {
			warnx("Must specify either -f format or -U for "
			    "setting the timestamp");
			error = 1;
		} else if ((format_string != NULL)
			&& (do_utc != 0)) {
			warnx("Must specify only one of -f or -U to set "
			    "the timestamp");
			error = 1;
		} else if ((format_string != NULL)
			&& (strcmp(format_string, MIL) == 0)) {
			warnx("-m is not allowed for setting the "
			    "timestamp");
			error = 1;
		} else if ((do_utc == 0)
			&& (timestamp_string == NULL)) {
			warnx("Must specify the time (-T) to set as the "
			    "timestamp");
			error = 1;
		}
		if (error != 0)
			goto bailout;
	} else if (action == TIMESTAMP_REPORT) {
		if (format_string == NULL) {
			format_string = strdup("%c %Z");
			if (format_string == NULL) {
				warn("Error allocating memory for format "
				    "string");
				error = 1;
				goto bailout;
			}
		}
	}

	if (action == TIMESTAMP_REPORT) {
		error = report_timestamp(device, &ts, task_attr, retry_count,
		    timeout);
		if (error != 0) {
			goto bailout;
		} else if (strcmp(format_string, MIL) == 0) {
			printf("Timestamp in milliseconds: %ju\n",
			    (uintmax_t)ts);
		} else {
			char temp_timestamp_string[100];
			time_t time_var = ts / 1000;
			const struct tm *restrict cur_time;

			setlocale(LC_TIME, "");
			if (do_utc != 0)
				cur_time = gmtime(&time_var);
			else
				cur_time = localtime(&time_var);

			strftime(temp_timestamp_string,
			    sizeof(temp_timestamp_string), format_string,
			    cur_time);
			printf("Formatted timestamp: %s\n",
			    temp_timestamp_string);
		}
	} else if (action == TIMESTAMP_SET) {
		if (do_utc != 0) {
			format_string = strdup(UTC);
			if (format_string == NULL) {
				warn("Error allocating memory for format "
				    "string");
				error = 1;
				goto bailout;
			}
		}

		error = set_timestamp(device, format_string, timestamp_string,
		    task_attr, retry_count, timeout);
	}

bailout:
	free(format_string);
	free(timestamp_string);

	return (error);
}
