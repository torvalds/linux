/*-
 * Copyright (c) 2014 Spectra Logic Corporation
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
 * SCSI Read and Write Attribute support for camcontrol(8).
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
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_ch.h>
#include <cam/scsi/scsi_message.h>
#include <camlib.h>
#include "camcontrol.h"

#if 0
struct scsi_attr_desc {
	int attr_id;
	
	STAILQ_ENTRY(scsi_attr_desc) links;
};
#endif

static struct scsi_nv elem_type_map[] = {
	{ "all", ELEMENT_TYPE_ALL },
	{ "picker", ELEMENT_TYPE_MT },
	{ "slot", ELEMENT_TYPE_ST },
	{ "portal", ELEMENT_TYPE_IE },
	{ "drive", ELEMENT_TYPE_DT },
};

static struct scsi_nv sa_map[] = {
	{ "attr_values", SRA_SA_ATTR_VALUES },
	{ "attr_list", SRA_SA_ATTR_LIST },
	{ "lv_list", SRA_SA_LOG_VOL_LIST },
	{ "part_list", SRA_SA_PART_LIST },
	{ "supp_attr", SRA_SA_SUPPORTED_ATTRS }
};

static struct scsi_nv output_format_map[] = {
	{ "text_esc", SCSI_ATTR_OUTPUT_TEXT_ESC },
	{ "text_raw", SCSI_ATTR_OUTPUT_TEXT_RAW },
	{ "nonascii_esc", SCSI_ATTR_OUTPUT_NONASCII_ESC },
	{ "nonascii_trim", SCSI_ATTR_OUTPUT_NONASCII_TRIM },
	{ "nonascii_raw", SCSI_ATTR_OUTPUT_NONASCII_RAW },
	{ "field_all", SCSI_ATTR_OUTPUT_FIELD_ALL },
	{ "field_none", SCSI_ATTR_OUTPUT_FIELD_NONE },
	{ "field_desc", SCSI_ATTR_OUTPUT_FIELD_DESC },
	{ "field_num", SCSI_ATTR_OUTPUT_FIELD_NUM },
	{ "field_size", SCSI_ATTR_OUTPUT_FIELD_SIZE },
	{ "field_rw", SCSI_ATTR_OUTPUT_FIELD_RW },
};

int
scsiattrib(struct cam_device *device, int argc, char **argv, char *combinedopt,
	   int task_attr, int retry_count, int timeout, int verbosemode,
	   int err_recover)
{
	union ccb *ccb = NULL;
	int attr_num = -1;
#if 0
	int num_attrs = 0;
#endif
	int start_attr = 0;
	int cached_attr = 0;
	int read_service_action = -1;
	int read_attr = 0, write_attr = 0;
	int element_address = 0;
	int element_type = ELEMENT_TYPE_ALL;
	int partition = 0;
	int logical_volume = 0;
	char *endptr;
	uint8_t *data_buf = NULL;
	uint32_t dxfer_len = UINT16_MAX - 1;
	uint32_t valid_len;
	uint32_t output_format;
	STAILQ_HEAD(, scsi_attr_desc) write_attr_list;
	int error = 0;
	int c;

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	STAILQ_INIT(&write_attr_list);

	/*
	 * By default, when displaying attribute values, we trim out
	 * non-ASCII characters in ASCII fields.  We display all fields
	 * (description, attribute number, attribute size, and readonly
	 * status).  We default to displaying raw text.
	 *
	 * XXX KDM need to port this to stable/10 and newer FreeBSD
	 * versions that have iconv built in and can convert codesets.
	 */
	output_format = SCSI_ATTR_OUTPUT_NONASCII_TRIM |
			SCSI_ATTR_OUTPUT_FIELD_ALL | 
			SCSI_ATTR_OUTPUT_TEXT_RAW;

	data_buf = malloc(dxfer_len);
	if (data_buf == NULL) {
		warn("%s: error allocating %u bytes", __func__, dxfer_len);
		error = 1;
		goto bailout;
	}

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			attr_num = strtol(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid attribute number %s",
				    __func__, optarg);
				error = 1;
				goto bailout;
			}
			start_attr = attr_num;
			break;
		case 'c':
			cached_attr = 1;
			break;
		case 'e':
			element_address = strtol(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid element address %s",
				    __func__, optarg);
				error = 1;
				goto bailout;
			}
			break;
		case 'F': {
			scsi_nv_status status;
			scsi_attrib_output_flags new_outflags;
			int entry_num = 0;
			char *tmpstr;

			if (isdigit(optarg[0])) {
				output_format = strtoul(optarg, &endptr, 0); 
				if (*endptr != '\0') {
					warnx("%s: invalid numeric output "
					    "format argument %s", __func__,
					    optarg);
					error = 1;
					goto bailout;
				}
				break;
			}
			new_outflags = SCSI_ATTR_OUTPUT_NONE;

			while ((tmpstr = strsep(&optarg, ",")) != NULL) {
				status = scsi_get_nv(output_format_map,
				    sizeof(output_format_map) /
				    sizeof(output_format_map[0]), tmpstr,
				    &entry_num, SCSI_NV_FLAG_IG_CASE);

				if (status == SCSI_NV_FOUND)
					new_outflags |=
					    output_format_map[entry_num].value;
				else {
					warnx("%s: %s format option %s",
					    __func__,
					    (status == SCSI_NV_AMBIGUOUS) ?
					    "ambiguous" : "invalid", tmpstr);
					error = 1;
					goto bailout;
				}
			}
			output_format = new_outflags;
			break;
		}
		case 'p':
			partition = strtol(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid partition number %s",
				    __func__, optarg);
				error = 1;
				goto bailout;
			}
			break;
		case 'r': {
			scsi_nv_status status;
			int entry_num = 0;

			status = scsi_get_nv(sa_map, sizeof(sa_map) /
			    sizeof(sa_map[0]), optarg, &entry_num,
			    SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				read_service_action = sa_map[entry_num].value;
			else {
				warnx("%s: %s %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "service action",
				    optarg);
				error = 1;
				goto bailout;
			}
			read_attr = 1;
			break;
		}
		case 's':
			start_attr = strtol(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid starting attr argument %s",
				    __func__, optarg);
				error = 1;
				goto bailout;
			}
			break;
		case 'T': {
			scsi_nv_status status;
			int entry_num = 0;

			status = scsi_get_nv(elem_type_map,
			    sizeof(elem_type_map) / sizeof(elem_type_map[0]),
			    optarg, &entry_num, SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				element_type = elem_type_map[entry_num].value;
			else {
				warnx("%s: %s %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", "element type",
				    optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'w':
			warnx("%s: writing attributes is not implemented yet",
			      __func__);
			error = 1;
			goto bailout;
			break;
		case 'V':
			logical_volume = strtol(optarg, &endptr, 0);

			if (*endptr != '\0') {
				warnx("%s: invalid logical volume argument %s",
				    __func__, optarg);
				error = 1;
				goto bailout;
			}
			break;
		default:
			break;
		}
	}

	/*
	 * Default to reading attributes 
	 */
	if (((read_attr == 0) && (write_attr == 0))
	 || ((read_attr != 0) && (write_attr != 0))) {
		warnx("%s: Must specify either -r or -w", __func__);
		error = 1;
		goto bailout;
	}

	if (read_attr != 0) {
		scsi_read_attribute(&ccb->csio,
				    /*retries*/ retry_count,
				    /*cbfcnp*/ NULL,
				    /*tag_action*/ task_attr,
				    /*service_action*/ read_service_action,
				    /*element*/ element_address,
				    /*elem_type*/ element_type,
				    /*logical_volume*/ logical_volume,
				    /*partition*/ partition,
				    /*first_attribute*/ start_attr,
				    /*cache*/ cached_attr,
				    /*data_ptr*/ data_buf,
				    /*length*/ dxfer_len,
			            /*sense_len*/ SSD_FULL_SIZE,
				    /*timeout*/ timeout ? timeout : 60000);
#if 0
	} else {
#endif

	}

	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (err_recover != 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending %s ATTRIBUTE", (read_attr != 0) ?
		    "READ" : "WRITE");

		if (verbosemode != 0) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}

		error = 1;
		goto bailout;
	}

	if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (verbosemode != 0) {
			cam_error_print(device, ccb, CAM_ESF_ALL,
					CAM_EPF_ALL, stderr);
		}
		error = 1;
		goto bailout;
	}

	if (read_attr == 0)
		goto bailout;

	valid_len = dxfer_len - ccb->csio.resid;

	switch (read_service_action) {
	case SRA_SA_ATTR_VALUES: {
		uint32_t len_left, hdr_len, cur_len;
		struct scsi_read_attribute_values *hdr;
		struct scsi_mam_attribute_header *cur_id;
		char error_str[512];
		uint8_t *cur_pos;
		struct sbuf *sb;

		hdr = (struct scsi_read_attribute_values *)data_buf;

		if (valid_len < sizeof(*hdr)) {
			fprintf(stdout, "No attributes returned.\n");
			error = 0;
			goto bailout;
		}

		sb = sbuf_new_auto();
		if (sb == NULL) {
			warn("%s: Unable to allocate sbuf", __func__);
			error = 1;
			goto bailout;
		}
		/*
		 * XXX KDM grab more data if it is available.
		 */
		hdr_len = scsi_4btoul(hdr->length);

		for (len_left = MIN(valid_len, hdr_len),
		     cur_pos = &hdr->attribute_0[0]; len_left > sizeof(*cur_id);
		     len_left -= cur_len, cur_pos += cur_len) {
			int cur_attr_num;
			cur_id = (struct scsi_mam_attribute_header *)cur_pos;
			cur_len = scsi_2btoul(cur_id->length) + sizeof(*cur_id);
			cur_attr_num = scsi_2btoul(cur_id->id);

			if ((attr_num != -1)
			 && (cur_attr_num != attr_num))
				continue;

			error = scsi_attrib_sbuf(sb, cur_id, len_left,
			    /*user_table*/ NULL, /*num_user_entries*/ 0,
			    /*prefer_user_table*/ 0, output_format, error_str,
			    sizeof(error_str));
			if (error != 0) {
				warnx("%s: %s", __func__, error_str);
				sbuf_delete(sb);
				error = 1;
				goto bailout;
			}
			if (attr_num != -1)
				break;
		}

		sbuf_finish(sb);
		fprintf(stdout, "%s", sbuf_data(sb));
		sbuf_delete(sb);
		break;
	}
	case SRA_SA_SUPPORTED_ATTRS:
	case SRA_SA_ATTR_LIST: {
		uint32_t len_left, hdr_len;
		struct scsi_attrib_list_header *hdr;
		struct scsi_attrib_table_entry *entry = NULL;
		const char *sa_name = "Supported Attributes";
		const char *at_name = "Available Attributes";
		int attr_id;
		uint8_t *cur_id;

		hdr = (struct scsi_attrib_list_header *)data_buf;
		if (valid_len < sizeof(*hdr)) {
			fprintf(stdout, "No %s\n",
				(read_service_action == SRA_SA_SUPPORTED_ATTRS)?
				 sa_name : at_name);
			error = 0;
			goto bailout;
		}
		fprintf(stdout, "%s:\n",
			(read_service_action == SRA_SA_SUPPORTED_ATTRS) ?
			 sa_name : at_name);
		hdr_len = scsi_4btoul(hdr->length);
		for (len_left = MIN(valid_len, hdr_len),
		     cur_id = &hdr->first_attr_0[0]; len_left > 1;
		     len_left -= sizeof(uint16_t), cur_id += sizeof(uint16_t)) {
			attr_id = scsi_2btoul(cur_id);

			if ((attr_num != -1)
			 && (attr_id != attr_num))
				continue;

			entry = scsi_get_attrib_entry(attr_id);
			fprintf(stdout, "0x%.4x", attr_id);
			if (entry == NULL)
				fprintf(stdout, "\n");
			else
				fprintf(stdout, ": %s\n", entry->desc);

			if (attr_num != -1)
				break;
		}
		break;
	}
	case SRA_SA_PART_LIST:
	case SRA_SA_LOG_VOL_LIST: {
		struct scsi_attrib_lv_list *lv_list;
		const char *partition_name = "Partition";
		const char *lv_name = "Logical Volume";

		if (valid_len < sizeof(*lv_list)) {
			fprintf(stdout, "No %s list returned\n",
				(read_service_action == SRA_SA_PART_LIST) ?
				partition_name : lv_name);
			error = 0;
			goto bailout;
		}

		lv_list = (struct scsi_attrib_lv_list *)data_buf;

		fprintf(stdout, "First %s: %d\n",
			(read_service_action == SRA_SA_PART_LIST) ?
			partition_name : lv_name,
			lv_list->first_lv_number);
		fprintf(stdout, "Number of %ss: %d\n",
			(read_service_action == SRA_SA_PART_LIST) ?
			partition_name : lv_name,
			lv_list->num_logical_volumes);
		break;
	}
	default:
		break;
	}
bailout:
	if (ccb != NULL)
		cam_freeccb(ccb);

	free(data_buf);

	return (error);
}
