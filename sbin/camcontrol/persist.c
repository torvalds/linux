/*-
 * Copyright (c) 2013 Spectra Logic Corporation
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
 * SCSI Persistent Reservation support for camcontrol(8).
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/queue.h>

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

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_message.h>
#include <camlib.h>
#include "camcontrol.h"

struct persist_transport_id {
	struct scsi_transportid_header *hdr;
	unsigned int alloc_len;
	STAILQ_ENTRY(persist_transport_id) links;
};

/*
 * Service Actions for PERSISTENT RESERVE IN.
 */
static struct scsi_nv persist_in_actions[] = {
	{ "read_keys", SPRI_RK },
	{ "read_reservation", SPRI_RR },
	{ "report_capabilities", SPRI_RC },
	{ "read_full_status", SPRI_RS }
};

/*
 * Service Actions for PERSISTENT RESERVE OUT.
 */
static struct scsi_nv persist_out_actions[] = {
	{ "register", SPRO_REGISTER },
	{ "reserve", SPRO_RESERVE },
	{ "release" , SPRO_RELEASE },
	{ "clear", SPRO_CLEAR },
	{ "preempt", SPRO_PREEMPT },
	{ "preempt_abort", SPRO_PRE_ABO },
	{ "register_ignore", SPRO_REG_IGNO },
	{ "register_move", SPRO_REG_MOVE },
	{ "replace_lost", SPRO_REPL_LOST_RES }
};

/*
 * Known reservation scopes.  As of SPC-4, only LU_SCOPE is used in the
 * spec.  The others are obsolete.
 */
static struct scsi_nv persist_scope_table[] = {
	{ "lun", SPR_LU_SCOPE },
	{ "extent", SPR_EXTENT_SCOPE },
	{ "element", SPR_ELEMENT_SCOPE }
};

/*
 * Reservation types.  The longer name for a given reservation type is
 * listed first, so that it makes more sense when we print out the
 * reservation type.  We step through the table linearly when looking for
 * the text name for a particular numeric reservation type value.
 */
static struct scsi_nv persist_type_table[] = {
	{ "read_shared", SPR_TYPE_RD_SHARED },
	{ "write_exclusive", SPR_TYPE_WR_EX },
	{ "wr_ex", SPR_TYPE_WR_EX },
	{ "read_exclusive", SPR_TYPE_RD_EX },
	{ "rd_ex", SPR_TYPE_RD_EX },
	{ "exclusive_access", SPR_TYPE_EX_AC },
	{ "ex_ac", SPR_TYPE_EX_AC },
	{ "write_exclusive_reg_only", SPR_TYPE_WR_EX_RO },
	{ "wr_ex_ro", SPR_TYPE_WR_EX_RO },
	{ "exclusive_access_reg_only", SPR_TYPE_EX_AC_RO },
	{ "ex_ac_ro", SPR_TYPE_EX_AC_RO },
	{ "write_exclusive_all_regs", SPR_TYPE_WR_EX_AR },
	{ "wr_ex_ar", SPR_TYPE_WR_EX_AR },
	{ "exclusive_access_all_regs", SPR_TYPE_EX_AC_AR },
	{ "ex_ac_ar", SPR_TYPE_EX_AC_AR }
};

/*
 * Print out the standard scope/type field.
 */
static void
persist_print_scopetype(uint8_t scopetype)
{
	const char *tmpstr;
	int num_entries;

	num_entries = sizeof(persist_scope_table) /
		      sizeof(persist_scope_table[0]);
	tmpstr = scsi_nv_to_str(persist_scope_table, num_entries,
				scopetype & SPR_SCOPE_MASK);
	fprintf(stdout, "Scope: %s (%#x)\n", (tmpstr != NULL) ? tmpstr :
		"Unknown", (scopetype & SPR_SCOPE_MASK) >> SPR_SCOPE_SHIFT);

	num_entries = sizeof(persist_type_table) /
		      sizeof(persist_type_table[0]);
	tmpstr = scsi_nv_to_str(persist_type_table, num_entries,
				scopetype & SPR_TYPE_MASK);
	fprintf(stdout, "Type: %s (%#x)\n", (tmpstr != NULL) ? tmpstr :
		"Unknown", scopetype & SPR_TYPE_MASK);
}

static void
persist_print_transportid(uint8_t *buf, uint32_t len)
{
	struct sbuf *sb;

	sb = sbuf_new_auto();
	if (sb == NULL)
		fprintf(stderr, "Unable to allocate sbuf\n");

	scsi_transportid_sbuf(sb, (struct scsi_transportid_header *)buf, len);

	sbuf_finish(sb);

	fprintf(stdout, "%s\n", sbuf_data(sb));

	sbuf_delete(sb);
}

/*
 * Print out a persistent reservation.  This is used with the READ
 * RESERVATION (0x01) service action of the PERSISTENT RESERVE IN command.
 */
static void
persist_print_res(struct scsi_per_res_in_header *hdr, uint32_t valid_len)
{
	uint32_t length;
	struct scsi_per_res_in_rsrv *res;

	length = scsi_4btoul(hdr->length);
	length = MIN(length, valid_len);

	res = (struct scsi_per_res_in_rsrv *)hdr;

	if (length < sizeof(res->data) - sizeof(res->data.extent_length)) {
		if (length == 0)
			fprintf(stdout, "No reservations.\n");
		else
			warnx("unable to print reservation, only got %u "
			      "valid bytes", length);
		return;
	}
	fprintf(stdout, "PRgeneration: %#x\n",
		scsi_4btoul(res->header.generation));
	fprintf(stdout, "Reservation Key: %#jx\n",
		(uintmax_t)scsi_8btou64(res->data.reservation));
	fprintf(stdout, "Scope address: %#x\n",
		scsi_4btoul(res->data.scope_addr));

	persist_print_scopetype(res->data.scopetype);

	fprintf(stdout, "Extent length: %u\n",
		scsi_2btoul(res->data.extent_length));
}

/*
 * Print out persistent reservation keys.  This is used with the READ KEYS
 * service action of the PERSISTENT RESERVE IN command.
 */
static void
persist_print_keys(struct scsi_per_res_in_header *hdr, uint32_t valid_len)
{
	uint32_t length, num_keys, i;
	struct scsi_per_res_key *key;

	length = scsi_4btoul(hdr->length);
	length = MIN(length, valid_len);

	num_keys = length / sizeof(*key);

	fprintf(stdout, "PRgeneration: %#x\n", scsi_4btoul(hdr->generation));
	fprintf(stdout, "%u key%s%s\n", num_keys, (num_keys == 1) ? "" : "s",
		(num_keys == 0) ? "." : ":");

	for (i = 0, key = (struct scsi_per_res_key *)&hdr[1]; i < num_keys;
	     i++, key++) {
		fprintf(stdout, "%u: %#jx\n", i,
			(uintmax_t)scsi_8btou64(key->key));
	}
}

/*
 * Print out persistent reservation capabilities.  This is used with the
 * REPORT CAPABILITIES service action of the PERSISTENT RESERVE IN command.
 */
static void
persist_print_cap(struct scsi_per_res_cap *cap, uint32_t valid_len)
{
	uint32_t length;
	int check_type_mask = 0;
	uint32_t type_mask;

	length = scsi_2btoul(cap->length);
	length = MIN(length, valid_len);
	type_mask = scsi_2btoul(cap->type_mask);

	if (length < __offsetof(struct scsi_per_res_cap, type_mask)) {
		fprintf(stdout, "Insufficient data (%u bytes) to report "
			"full capabilities\n", length);
		return;
	}
	if (length >= __offsetof(struct scsi_per_res_cap, reserved))
		check_type_mask = 1;
	
	fprintf(stdout, "Replace Lost Reservation Capable (RLR_C): %d\n",
		(cap->flags1 & SPRI_RLR_C) ? 1 : 0);
	fprintf(stdout, "Compatible Reservation Handling (CRH): %d\n",
		(cap->flags1 & SPRI_CRH) ? 1 : 0);
	fprintf(stdout, "Specify Initiator Ports Capable (SIP_C): %d\n",
		(cap->flags1 & SPRI_SIP_C) ? 1 : 0);
	fprintf(stdout, "All Target Ports Capable (ATP_C): %d\n",
		(cap->flags1 & SPRI_ATP_C) ? 1 : 0);
	fprintf(stdout, "Persist Through Power Loss Capable (PTPL_C): %d\n",
		(cap->flags1 & SPRI_PTPL_C) ? 1 : 0);
	fprintf(stdout, "ALLOW COMMANDS field: (%#x)\n",
		(cap->flags2 & SPRI_ALLOW_CMD_MASK) >> SPRI_ALLOW_CMD_SHIFT);
	/*
	 * These cases are cut-and-pasted from SPC4r36l.  There is no
	 * succinct way to describe these otherwise, and even with the
	 * verbose description, the user will probably have to refer to
	 * the spec to fully understand what is going on.
	 */
	switch (cap->flags2 & SPRI_ALLOW_CMD_MASK) {
	case SPRI_ALLOW_1:
		fprintf(stdout,
"    The device server allows the TEST UNIT READY command through Write\n"
"    Exclusive type reservations and Exclusive Access type reservations\n"
"    and does not provide information about whether the following commands\n"
"    are allowed through Write Exclusive type reservations:\n"
"        a) the MODE SENSE command, READ ATTRIBUTE command, READ BUFFER\n"
"           command, RECEIVE COPY RESULTS command, RECEIVE DIAGNOSTIC\n"
"           RESULTS command, REPORT SUPPORTED OPERATION CODES command,\n"
"           and REPORT SUPPORTED TASK MANAGEMENT FUNCTION command; and\n"
"        b) the READ DEFECT DATA command (see SBC-3).\n");
		break;
	case SPRI_ALLOW_2:
		fprintf(stdout,
"    The device server allows the TEST UNIT READY command through Write\n"
"    Exclusive type reservations and Exclusive Access type reservations\n"
"    and does not allow the following commands through Write Exclusive type\n"
"    reservations:\n"
"        a) the MODE SENSE command, READ ATTRIBUTE command, READ BUFFER\n"
"           command, RECEIVE DIAGNOSTIC RESULTS command, REPORT SUPPORTED\n"
"           OPERATION CODES command, and REPORT SUPPORTED TASK MANAGEMENT\n"
"           FUNCTION command; and\n"
"        b) the READ DEFECT DATA command.\n"
"    The device server does not allow the RECEIVE COPY RESULTS command\n"
"    through Write Exclusive type reservations or Exclusive Access type\n"
"    reservations.\n");
		break;
	case SPRI_ALLOW_3:
		fprintf(stdout,
"    The device server allows the TEST UNIT READY command through Write\n"
"    Exclusive type reservations and Exclusive Access type reservations\n"
"    and allows the following commands through Write Exclusive type\n"
"    reservations:\n"
"        a) the MODE SENSE command, READ ATTRIBUTE command, READ BUFFER\n"
"           command, RECEIVE DIAGNOSTIC RESULTS command, REPORT SUPPORTED\n"
"           OPERATION CODES command, and REPORT SUPPORTED TASK MANAGEMENT\n"
"           FUNCTION command; and\n"
"        b) the READ DEFECT DATA command.\n"
"    The device server does not allow the RECEIVE COPY RESULTS command\n"
"    through Write Exclusive type reservations or Exclusive Access type\n"
"    reservations.\n");
		break;
	case SPRI_ALLOW_4:
		fprintf(stdout,
"    The device server allows the TEST UNIT READY command and the RECEIVE\n"
"    COPY RESULTS command through Write Exclusive type reservations and\n"
"    Exclusive Access type reservations and allows the following commands\n"
"    through Write Exclusive type reservations:\n"
"        a) the MODE SENSE command, READ ATTRIBUTE command, READ BUFFER\n"
"           command, RECEIVE DIAGNOSTIC RESULTS command, REPORT SUPPORTED\n"
"           OPERATION CODES command, and REPORT SUPPORTED TASK MANAGEMENT\n"
"           FUNCTION command; and\n"
"        b) the READ DEFECT DATA command.\n");
		break;
	case SPRI_ALLOW_NA:
		fprintf(stdout,
"    No information is provided about whether certain commands are allowed\n"
"    through certain types of persistent reservations.\n");
		break;
	default:
		fprintf(stdout,
"    Unknown ALLOW COMMANDS value %#x\n",
			(cap->flags2 & SPRI_ALLOW_CMD_MASK) >>
			SPRI_ALLOW_CMD_SHIFT);
		break;
	}
	fprintf(stdout, "Persist Through Power Loss Activated (PTPL_A): %d\n",
		(cap->flags2 & SPRI_PTPL_A) ? 1 : 0);
	if ((check_type_mask != 0)
	 && (cap->flags2 & SPRI_TMV)) {
		fprintf(stdout, "Supported Persistent Reservation Types:\n");
		fprintf(stdout, "    Write Exclusive - All Registrants "
			"(WR_EX_AR): %d\n",
			(type_mask & SPRI_TM_WR_EX_AR)? 1 : 0);
		fprintf(stdout, "    Exclusive Access - Registrants Only "
			"(EX_AC_RO): %d\n",
			(type_mask & SPRI_TM_EX_AC_RO) ? 1 : 0);
		fprintf(stdout, "    Write Exclusive - Registrants Only "
			"(WR_EX_RO): %d\n",
			(type_mask & SPRI_TM_WR_EX_RO)? 1 : 0);
		fprintf(stdout, "    Exclusive Access (EX_AC): %d\n",
			(type_mask & SPRI_TM_EX_AC) ? 1 : 0);
		fprintf(stdout, "    Write Exclusive (WR_EX): %d\n",
			(type_mask & SPRI_TM_WR_EX) ? 1 : 0);
		fprintf(stdout, "    Exclusive Access - All Registrants "
			"(EX_AC_AR): %d\n",
			(type_mask & SPRI_TM_EX_AC_AR) ? 1 : 0);
	} else {
		fprintf(stdout, "Persistent Reservation Type Mask is NOT "
			"valid\n");
	}

	
}

static void
persist_print_full(struct scsi_per_res_in_header *hdr, uint32_t valid_len)
{
	uint32_t length, len_to_go = 0;
	struct scsi_per_res_in_full_desc *desc;
	uint8_t *cur_pos;
	int i;

	length = scsi_4btoul(hdr->length);
	length = MIN(length, valid_len);

	if (length < sizeof(*desc)) {
		if (length == 0)
			fprintf(stdout, "No reservations.\n");
		else
			warnx("unable to print reservation, only got %u "
			      "valid bytes", length);
		return;
	}

	fprintf(stdout, "PRgeneration: %#x\n", scsi_4btoul(hdr->generation));
	cur_pos = (uint8_t *)&hdr[1];
	for (len_to_go = length, i = 0,
	     desc = (struct scsi_per_res_in_full_desc *)cur_pos;
	     len_to_go >= sizeof(*desc);
	     desc = (struct scsi_per_res_in_full_desc *)cur_pos, i++) {
		uint32_t additional_length, cur_length;


		fprintf(stdout, "Reservation Key: %#jx\n",
			(uintmax_t)scsi_8btou64(desc->res_key.key));
		fprintf(stdout, "All Target Ports (ALL_TG_PT): %d\n",
			(desc->flags & SPRI_FULL_ALL_TG_PT) ? 1 : 0);
		fprintf(stdout, "Reservation Holder (R_HOLDER): %d\n",
			(desc->flags & SPRI_FULL_R_HOLDER) ? 1 : 0);
		
		if (desc->flags & SPRI_FULL_R_HOLDER)
			persist_print_scopetype(desc->scopetype);

		if ((desc->flags & SPRI_FULL_ALL_TG_PT) == 0)
			fprintf(stdout, "Relative Target Port ID: %#x\n",
				scsi_2btoul(desc->rel_trgt_port_id));

		additional_length = scsi_4btoul(desc->additional_length);

		persist_print_transportid(desc->transport_id,
					  additional_length);

		cur_length = sizeof(*desc) + additional_length;
		len_to_go -= cur_length;
		cur_pos += cur_length;
	}
}

int
scsipersist(struct cam_device *device, int argc, char **argv, char *combinedopt,
	    int task_attr, int retry_count, int timeout, int verbosemode,
	    int err_recover)
{
	union ccb *ccb = NULL;
	int c, in = 0, out = 0;
	int action = -1, num_ids = 0;
	int error = 0;
	uint32_t res_len = 0;
	unsigned long rel_tgt_port = 0;
	uint8_t *res_buf = NULL;
	int scope = SPR_LU_SCOPE, res_type = 0;
	struct persist_transport_id *id, *id2;
	STAILQ_HEAD(, persist_transport_id) transport_id_list;
	uint64_t key = 0, sa_key = 0;
	struct scsi_nv *table = NULL;
	size_t table_size = 0, id_len = 0;
	uint32_t valid_len = 0;
	int all_tg_pt = 0, aptpl = 0, spec_i_pt = 0, unreg = 0,rel_port_set = 0;

	STAILQ_INIT(&transport_id_list);

	ccb = cam_getccb(device);
	if (ccb == NULL) {
		warnx("%s: error allocating CCB", __func__);
		error = 1;
		goto bailout;
	}

	CCB_CLEAR_ALL_EXCEPT_HDR(&ccb->csio);

	while ((c = getopt(argc, argv, combinedopt)) != -1) {
		switch (c) {
		case 'a':
			all_tg_pt = 1;
			break;
		case 'I': {
			int error_str_len = 128;
			char error_str[error_str_len];
			char *id_str;

			id = malloc(sizeof(*id));
			if (id == NULL) {
				warnx("%s: error allocating %zu bytes",
				    __func__, sizeof(*id));
				error = 1;
				goto bailout;
			}
			bzero(id, sizeof(*id));

			id_str = strdup(optarg);
			if (id_str == NULL) {
				warnx("%s: error duplicating string %s",
				    __func__, optarg);
				free(id);
				error = 1;
				goto bailout;
			}
			error = scsi_parse_transportid(id_str, &id->hdr,
			    &id->alloc_len, error_str, error_str_len);
			if (error != 0) {
				warnx("%s", error_str);
				error = 1;
				free(id);
				free(id_str);
				goto bailout;
			}
			free(id_str);

			STAILQ_INSERT_TAIL(&transport_id_list, id, links);
			num_ids++;
			id_len += id->alloc_len;
			break;
		}
		case 'k': 
		case 'K': {
			char *endptr;
			uint64_t tmpval;

			tmpval = strtoumax(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid key argument %s", __func__,
				    optarg);
				error = 1;
				goto bailout;
			}
			if (c == 'k') {
				key = tmpval;
			} else {
				sa_key = tmpval;
			}
			break;
		}
		case 'i':
		case 'o': {
			scsi_nv_status status;
			int table_entry = 0;

			if (c == 'i') {
				in = 1;
				table = persist_in_actions;
				table_size = sizeof(persist_in_actions) /
					sizeof(persist_in_actions[0]);
			} else {
				out = 1;
				table = persist_out_actions;
				table_size = sizeof(persist_out_actions) /
					sizeof(persist_out_actions[0]);
			}

			if ((in + out) > 1) {
				warnx("%s: only one in (-i) or out (-o) "
				    "action is allowed", __func__);
				error = 1;
				goto bailout;
			}

			status = scsi_get_nv(table, table_size, optarg,
					     &table_entry,SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				action = table[table_entry].value;
			else {
				warnx("%s: %s %s option %s", __func__,
				    (status == SCSI_NV_AMBIGUOUS) ?
				    "ambiguous" : "invalid", in ? "in" :
				    "out", optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'p':
			aptpl = 1;
			break;
		case 'R': {
			char *endptr;

			rel_tgt_port = strtoul(optarg, &endptr, 0);
			if (*endptr != '\0') {
				warnx("%s: invalid relative target port %s",
				    __func__, optarg);
				error = 1;
				goto bailout;
			}
			rel_port_set = 1;
			break;
		}
		case 's': {
			size_t scope_size;
			struct scsi_nv *scope_table = NULL;
			scsi_nv_status status;
			int table_entry = 0;
			char *endptr;

			/*
			 * First check to see if the user gave us a numeric
			 * argument.  If so, we'll try using it.
			 */
			if (isdigit(optarg[0])) {
				scope = strtol(optarg, &endptr, 0);
				if (*endptr != '\0') {
					warnx("%s: invalid scope %s",
					       __func__, optarg);
					error = 1;
					goto bailout;
				}
				scope = (scope << SPR_SCOPE_SHIFT) &
				    SPR_SCOPE_MASK;
				break;
			}

			scope_size = sizeof(persist_scope_table) /
				     sizeof(persist_scope_table[0]);
			scope_table = persist_scope_table;
			status = scsi_get_nv(scope_table, scope_size, optarg,
					     &table_entry,SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				scope = scope_table[table_entry].value;
			else {
				warnx("%s: %s scope %s", __func__,
				      (status == SCSI_NV_AMBIGUOUS) ?
				      "ambiguous" : "invalid", optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'S':
			spec_i_pt = 1;
			break;
		case 'T': {
			size_t res_type_size;
			struct scsi_nv *rtype_table = NULL;
			scsi_nv_status status;
			char *endptr;
			int table_entry = 0;

			/*
			 * First check to see if the user gave us a numeric
			 * argument.  If so, we'll try using it.
			 */
			if (isdigit(optarg[0])) {
				res_type = strtol(optarg, &endptr, 0);
				if (*endptr != '\0') {
					warnx("%s: invalid reservation type %s",
					       __func__, optarg);
					error = 1;
					goto bailout;
				}
				break;
			}

			res_type_size = sizeof(persist_type_table) /
					sizeof(persist_type_table[0]);
			rtype_table = persist_type_table;
			status = scsi_get_nv(rtype_table, res_type_size,
					     optarg, &table_entry,
					     SCSI_NV_FLAG_IG_CASE);
			if (status == SCSI_NV_FOUND)
				res_type = rtype_table[table_entry].value;
			else {
				warnx("%s: %s reservation type %s", __func__,
				      (status == SCSI_NV_AMBIGUOUS) ?
				      "ambiguous" : "invalid", optarg);
				error = 1;
				goto bailout;
			}
			break;
		}
		case 'U':
			unreg = 1;
			break;
		default:
			break;
		}
	}

	if ((in + out) != 1) {
		warnx("%s: you must specify one of -i or -o", __func__);
		error = 1;
		goto bailout;
	}

	/*
	 * Note that we don't really try to figure out whether the user
	 * needs to specify one or both keys.  There are a number of
	 * scenarios, and sometimes 0 is a valid and desired value.
	 */
	if (in != 0) {
		switch (action) {
		case SPRI_RK:
		case SPRI_RR:
		case SPRI_RS:
			/*
			 * Allocate the maximum length possible for these
			 * service actions.  According to the spec, the
			 * target is supposed to return the available
			 * length in the header, regardless of the
			 * allocation length.  In practice, though, with
			 * the READ FULL STATUS (SPRI_RS) service action,
			 * some Seagate drives (in particular a
			 * Constellation ES, <SEAGATE ST32000444SS 0006>)
			 * don't return the available length if you only
			 * allocate the length of the header.  So just
			 * allocate the maximum here so we don't miss
			 * anything.
			 */
			res_len = SPRI_MAX_LEN;
			break;
		case SPRI_RC:
			res_len = sizeof(struct scsi_per_res_cap);
			break;
		default:
			/* In theory we should catch this above */
			warnx("%s: invalid action %d", __func__, action);
			error = 1;
			goto bailout;
			break;
		}
	} else {

		/*
		 * XXX KDM need to add length for transport IDs for the
		 * register and move service action and the register
		 * service action with the SPEC_I_PT bit set.
		 */
		if (action == SPRO_REG_MOVE) {
			if (num_ids != 1) {
			    	warnx("%s: register and move requires a "
				    "single transport ID (-I)", __func__);
				error = 1;
				goto bailout;
			}
			if (rel_port_set == 0) {
				warnx("%s: register and move requires a "
				    "relative target port (-R)", __func__);
				error = 1;
				goto bailout;
			}
			res_len = sizeof(struct scsi_per_res_reg_move) + id_len;
		} else {
			res_len = sizeof(struct scsi_per_res_out_parms);
			if ((action == SPRO_REGISTER)
			 && (num_ids != 0)) {
				/*
				 * If the user specifies any IDs with the
				 * register service action, turn on the
				 * spec_i_pt bit.
				 */
				spec_i_pt = 1;
				res_len += id_len;
				res_len +=
				    sizeof(struct scsi_per_res_out_trans_ids);
			}
		}
	}
retry:
	if (res_buf != NULL) {
		free(res_buf);
		res_buf = NULL;
	}
	res_buf = malloc(res_len);
	if (res_buf == NULL) {
		warn("%s: error allocating %d bytes", __func__, res_len);
		error = 1;
		goto bailout;
	}
	bzero(res_buf, res_len);

	if (in != 0) {
		scsi_persistent_reserve_in(&ccb->csio,
					   /*retries*/ retry_count,
					   /*cbfcnp*/ NULL,
					   /*tag_action*/ task_attr,
					   /*service_action*/ action,
					   /*data_ptr*/ res_buf,
					   /*dxfer_len*/ res_len,
					   /*sense_len*/ SSD_FULL_SIZE,
					   /*timeout*/ timeout ? timeout :5000);

	} else {
		switch (action) {
		case SPRO_REGISTER:
			if (spec_i_pt != 0) {
				struct scsi_per_res_out_trans_ids *id_hdr;
				uint8_t *bufptr;

				bufptr = res_buf +
				    sizeof(struct scsi_per_res_out_parms) +
				    sizeof(struct scsi_per_res_out_trans_ids);
				STAILQ_FOREACH(id, &transport_id_list, links) {
					bcopy(id->hdr, bufptr, id->alloc_len);
					bufptr += id->alloc_len;
				}
				id_hdr = (struct scsi_per_res_out_trans_ids *)
				    (res_buf +
				    sizeof(struct scsi_per_res_out_parms));
				scsi_ulto4b(id_len, id_hdr->additional_length);
			}
		case SPRO_REG_IGNO:
		case SPRO_PREEMPT:
		case SPRO_PRE_ABO:
		case SPRO_RESERVE:
		case SPRO_RELEASE:
		case SPRO_CLEAR:
		case SPRO_REPL_LOST_RES: {
			struct scsi_per_res_out_parms *parms;

			parms = (struct scsi_per_res_out_parms *)res_buf;

			scsi_u64to8b(key, parms->res_key.key);
			scsi_u64to8b(sa_key, parms->serv_act_res_key);
			if (spec_i_pt != 0)
				parms->flags |= SPR_SPEC_I_PT;
			if (all_tg_pt != 0)
				parms->flags |= SPR_ALL_TG_PT;
			if (aptpl != 0)
				parms->flags |= SPR_APTPL;
			break;
		}
		case SPRO_REG_MOVE: {
			struct scsi_per_res_reg_move *reg_move;
			uint8_t *bufptr;

			reg_move = (struct scsi_per_res_reg_move *)res_buf;

			scsi_u64to8b(key, reg_move->res_key.key);
			scsi_u64to8b(sa_key, reg_move->serv_act_res_key);
			if (unreg != 0)
				reg_move->flags |= SPR_REG_MOVE_UNREG;
			if (aptpl != 0)
				reg_move->flags |= SPR_REG_MOVE_APTPL;
			scsi_ulto2b(rel_tgt_port, reg_move->rel_trgt_port_id);
			id = STAILQ_FIRST(&transport_id_list);
			/*
			 * This shouldn't happen, since we already checked
			 * the number of IDs above.
			 */
			if (id == NULL) {
				warnx("%s: No transport IDs found!", __func__);
				error = 1;
				goto bailout;
			}
			bufptr = (uint8_t *)&reg_move[1];
			bcopy(id->hdr, bufptr, id->alloc_len);
			scsi_ulto4b(id->alloc_len,
			    reg_move->transport_id_length);
			break;
		}
		default:
			break;
		}
		scsi_persistent_reserve_out(&ccb->csio,
					    /*retries*/ retry_count,
					    /*cbfcnp*/ NULL,
					    /*tag_action*/ task_attr,
					    /*service_action*/ action,
					    /*scope*/ scope,
					    /*res_type*/ res_type,
					    /*data_ptr*/ res_buf,
					    /*dxfer_len*/ res_len,
					    /*sense_len*/ SSD_FULL_SIZE,
					    /*timeout*/ timeout ?timeout :5000);
	}

	/* Disable freezing the device queue */
	ccb->ccb_h.flags |= CAM_DEV_QFRZDIS;

	if (err_recover != 0)
		ccb->ccb_h.flags |= CAM_PASS_ERR_RECOVER;

	if (cam_send_ccb(device, ccb) < 0) {
		warn("error sending PERSISTENT RESERVE %s", (in != 0) ?
		    "IN" : "OUT");

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

	if (in == 0)
		goto bailout;

	valid_len = res_len - ccb->csio.resid;

	switch (action) {
	case SPRI_RK:
	case SPRI_RR:
	case SPRI_RS: {
		struct scsi_per_res_in_header *hdr;
		uint32_t hdr_len;

		if (valid_len < sizeof(*hdr)) {
			warnx("%s: only got %d valid bytes, need %zd",
			      __func__, valid_len, sizeof(*hdr));
			error = 1;
			goto bailout;
		}
		hdr = (struct scsi_per_res_in_header *)res_buf;
		hdr_len = scsi_4btoul(hdr->length);

		if (hdr_len > (res_len - sizeof(*hdr))) {
			res_len = hdr_len + sizeof(*hdr);
			goto retry;
		}

		if (action == SPRI_RK) {
			persist_print_keys(hdr, valid_len);
		} else if (action == SPRI_RR) {
			persist_print_res(hdr, valid_len);
		} else {
			persist_print_full(hdr, valid_len);
		}
		break;
	}
	case SPRI_RC: {
		struct scsi_per_res_cap *cap;
		uint32_t cap_len;

		if (valid_len < sizeof(*cap)) {
			warnx("%s: only got %u valid bytes, need %zd",
			      __func__, valid_len, sizeof(*cap));
			error = 1;
			goto bailout;
		}
		cap = (struct scsi_per_res_cap *)res_buf;
		cap_len = scsi_2btoul(cap->length);
		if (cap_len != sizeof(*cap)) {
			/*
			 * We should be able to deal with this,
			 * it's just more trouble.
			 */
			warnx("%s: reported size %u is different "
			    "than expected size %zd", __func__,
			    cap_len, sizeof(*cap));
		}

		/*
		 * If there is more data available, grab it all,
		 * even though we don't really know what to do with
		 * the extra data since it obviously wasn't in the
		 * spec when this code was written.
		 */
		if (cap_len > res_len) {
			res_len = cap_len;
			goto retry;
		}
		persist_print_cap(cap, valid_len);
		break;
	}
	default:
		break;
	}

bailout:
	free(res_buf);

	if (ccb != NULL)
		cam_freeccb(ccb);

	STAILQ_FOREACH_SAFE(id, &transport_id_list, links, id2) {
		STAILQ_REMOVE(&transport_id_list, id, persist_transport_id,
		    links);
		free(id);
	}
	return (error);
}
