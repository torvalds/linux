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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/sbuf.h>
#include <sys/queue.h>
#include <sys/disk.h>
#include <sys/disk_zone.h>
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

static struct scsi_nv zone_cmd_map[] = {
	{ "rz", DISK_ZONE_REPORT_ZONES },
	{ "reportzones", DISK_ZONE_REPORT_ZONES },
	{ "close", DISK_ZONE_CLOSE },
	{ "finish", DISK_ZONE_FINISH },
	{ "open", DISK_ZONE_OPEN },
	{ "rwp", DISK_ZONE_RWP },
	{ "params", DISK_ZONE_GET_PARAMS }
};

static struct scsi_nv zone_rep_opts[] = {
	{ "all", DISK_ZONE_REP_ALL },
	{ "empty", DISK_ZONE_REP_EMPTY },
	{ "imp_open", DISK_ZONE_REP_IMP_OPEN },
	{ "exp_open", DISK_ZONE_REP_EXP_OPEN },
	{ "closed", DISK_ZONE_REP_CLOSED },
	{ "full", DISK_ZONE_REP_FULL },
	{ "readonly", DISK_ZONE_REP_READONLY },
	{ "ro", DISK_ZONE_REP_READONLY },
	{ "offline", DISK_ZONE_REP_OFFLINE },
	{ "reset", DISK_ZONE_REP_RWP },
	{ "rwp", DISK_ZONE_REP_RWP },
	{ "nonseq", DISK_ZONE_REP_NON_SEQ },
	{ "nonwp", DISK_ZONE_REP_NON_WP }
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

static struct scsi_nv zone_cmd_desc_table[] = {
	{"Report Zones", DISK_ZONE_RZ_SUP },
	{"Open", DISK_ZONE_OPEN_SUP },
	{"Close", DISK_ZONE_CLOSE_SUP },
	{"Finish", DISK_ZONE_FINISH_SUP },
	{"Reset Write Pointer", DISK_ZONE_RWP_SUP }
};

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


static void usage(int error);
static void zonectl_print_params(struct disk_zone_disk_params *params);
zone_print_status zonectl_print_rz(struct disk_zone_report *report,
				   zone_output_flags out_flags, int first_pass);

static void
usage(int error)
{
	fprintf(error ? stderr : stdout,
"usage: zonectl <-d dev> <-c cmd> [-a][-o rep_opts] [-l lba][-P print_opts]\n"
	);
}

static void
zonectl_print_params(struct disk_zone_disk_params *params)
{
	unsigned int i;
	int first;

	printf("Zone Mode: ");
	switch (params->zone_mode) {
	case DISK_ZONE_MODE_NONE:
		printf("None");
		break;
	case DISK_ZONE_MODE_HOST_AWARE:
		printf("Host Aware");
		break;
	case DISK_ZONE_MODE_DRIVE_MANAGED:
		printf("Drive Managed");
		break;
	case DISK_ZONE_MODE_HOST_MANAGED:
		printf("Host Managed");
		break;
	default:
		printf("Unknown mode %#x", params->zone_mode);
		break;
	}
	printf("\n");

	first = 1;
	printf("Command support: ");
	for (i = 0; i < sizeof(zone_cmd_desc_table) /
	     sizeof(zone_cmd_desc_table[0]); i++) {
		if (params->flags & zone_cmd_desc_table[i].value) {
			if (first == 0)
				printf(", ");
			else
				first = 0;
			printf("%s", zone_cmd_desc_table[i].name);
		}
	}
	if (first == 1)
		printf("None");
	printf("\n");

	printf("Unrestricted Read in Sequential Write Required Zone "
	    "(URSWRZ): %s\n", (params->flags & DISK_ZONE_DISK_URSWRZ) ?
	    "Yes" : "No");

	printf("Optimal Number of Open Sequential Write Preferred Zones: ");
	if (params->flags & DISK_ZONE_OPT_SEQ_SET)
		if (params->optimal_seq_zones == SVPD_ZBDC_OPT_SEQ_NR)
			printf("Not Reported");
		else
			printf("%ju", (uintmax_t)params->optimal_seq_zones);
	else
		printf("Not Set");
	printf("\n");


	printf("Optimal Number of Non-Sequentially Written Sequential Write "
	   "Preferred Zones: ");
	if (params->flags & DISK_ZONE_OPT_NONSEQ_SET)
		if (params->optimal_nonseq_zones == SVPD_ZBDC_OPT_NONSEQ_NR)
			printf("Not Reported");
		else
			printf("%ju",(uintmax_t)params->optimal_nonseq_zones);
	else
		printf("Not Set");
	printf("\n");

	printf("Maximum Number of Open Sequential Write Required Zones: ");
	if (params->flags & DISK_ZONE_MAX_SEQ_SET)
		if (params->max_seq_zones == SVPD_ZBDC_MAX_SEQ_UNLIMITED)
			printf("Unlimited");
		else
			printf("%ju", (uintmax_t)params->max_seq_zones);
	else
		printf("Not Set");
	printf("\n");
}

zone_print_status
zonectl_print_rz(struct disk_zone_report *report, zone_output_flags out_flags,
		 int first_pass)
{
	zone_print_status status = ZONE_PRINT_OK;
	struct disk_zone_rep_header *header = &report->header;
	int field_widths[ZONE_NUM_FIELDS];
	struct disk_zone_rep_entry *entry;
	uint64_t next_lba = 0;
	char tmpstr[80];
	char word_sep;
	int more_data = 0;
	uint32_t i;

	field_widths[ZONE_FW_START] = 11;
	field_widths[ZONE_FW_LEN] = 6;
	field_widths[ZONE_FW_WP] = 11;
	field_widths[ZONE_FW_TYPE] = 13;
	field_widths[ZONE_FW_COND] = 13;
	field_widths[ZONE_FW_SEQ] = 14;
	field_widths[ZONE_FW_RESET] = 16;

	if ((report->entries_available - report->entries_filled) > 0) {
		more_data = 1;
		status = ZONE_PRINT_MORE_DATA;
	}

	if (out_flags == ZONE_OF_SCRIPT)
		word_sep = '_';
	else
		word_sep = ' ';

	if ((out_flags != ZONE_OF_SCRIPT)
	 && (first_pass != 0)) {
		printf("%u zones, Maximum LBA %#jx (%ju)\n",
		    report->entries_available,
		    (uintmax_t)header->maximum_lba,
		    (uintmax_t)header->maximum_lba);

		switch (header->same) {
		case DISK_ZONE_SAME_ALL_DIFFERENT:
			printf("Zone lengths and types may vary\n");
			break;
		case DISK_ZONE_SAME_ALL_SAME:
			printf("Zone lengths and types are all the same\n");
			break;
		case DISK_ZONE_SAME_LAST_DIFFERENT:
			printf("Zone types are the same, last zone length "
			    "differs\n");
			break;
		case DISK_ZONE_SAME_TYPES_DIFFERENT:
			printf("Zone lengths are the same, types vary\n");
			break;
		default:
			printf("Unknown SAME field value %#x\n",header->same);
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

	for (i = 0; i < report->entries_filled; i++) {
		entry = &report->entries[i];

		printf("%#*jx, %*ju, %#*jx, ", field_widths[ZONE_FW_START],
		    (uintmax_t)entry->zone_start_lba,
		    field_widths[ZONE_FW_LEN],
		    (uintmax_t)entry->zone_length, field_widths[ZONE_FW_WP],
		    (uintmax_t)entry->write_pointer_lba);

		switch (entry->zone_type) {
		case DISK_ZONE_TYPE_CONVENTIONAL:
			snprintf(tmpstr, sizeof(tmpstr), "Conventional");
			break;
		case DISK_ZONE_TYPE_SEQ_PREFERRED:
		case DISK_ZONE_TYPE_SEQ_REQUIRED:
			snprintf(tmpstr, sizeof(tmpstr), "Seq%c%s",
			    word_sep, (entry->zone_type ==
			    DISK_ZONE_TYPE_SEQ_PREFERRED) ? "Preferred" :
			    "Required");
			break;
		default:
			snprintf(tmpstr, sizeof(tmpstr), "Zone%ctype%c%#x",
			    word_sep, word_sep, entry->zone_type);
			break;
		}
		printf("%*s, ", field_widths[ZONE_FW_TYPE], tmpstr);

		switch (entry->zone_condition) {
		case DISK_ZONE_COND_NOT_WP:
			snprintf(tmpstr, sizeof(tmpstr), "NWP");
			break;
		case DISK_ZONE_COND_EMPTY:
			snprintf(tmpstr, sizeof(tmpstr), "Empty");
			break;
		case DISK_ZONE_COND_IMPLICIT_OPEN:
			snprintf(tmpstr, sizeof(tmpstr), "Implicit%cOpen",
			    word_sep);
			break;
		case DISK_ZONE_COND_EXPLICIT_OPEN:
			snprintf(tmpstr, sizeof(tmpstr), "Explicit%cOpen",
			    word_sep);
			break;
		case DISK_ZONE_COND_CLOSED:
			snprintf(tmpstr, sizeof(tmpstr), "Closed");
			break;
		case DISK_ZONE_COND_READONLY:
			snprintf(tmpstr, sizeof(tmpstr), "Readonly");
			break;
		case DISK_ZONE_COND_FULL:
			snprintf(tmpstr, sizeof(tmpstr), "Full");
			break;
		case DISK_ZONE_COND_OFFLINE:
			snprintf(tmpstr, sizeof(tmpstr), "Offline");
			break;
		default:
			snprintf(tmpstr, sizeof(tmpstr), "%#x",
			    entry->zone_condition);
			break;
		}

		printf("%*s, ", field_widths[ZONE_FW_COND], tmpstr);

		if (entry->zone_flags & DISK_ZONE_FLAG_NON_SEQ)
			snprintf(tmpstr, sizeof(tmpstr), "Non%cSequential",
			    word_sep);
		else
			snprintf(tmpstr, sizeof(tmpstr), "Sequential");

		printf("%*s, ", field_widths[ZONE_FW_SEQ], tmpstr);

		if (entry->zone_flags & DISK_ZONE_FLAG_RESET)
			snprintf(tmpstr, sizeof(tmpstr), "Reset%cNeeded",
			    word_sep);
		else
			snprintf(tmpstr, sizeof(tmpstr), "No%cReset%cNeeded",
			    word_sep, word_sep);

		printf("%*s\n", field_widths[ZONE_FW_RESET], tmpstr);

		next_lba = entry->zone_start_lba + entry->zone_length;
	}
bailout:
	report->starting_id = next_lba;

	return (status);
}

int
main(int argc, char **argv)
{
	int c;
	int all_zones = 0;
	int error = 0;
	int action = -1, rep_option = -1;
	int fd = -1;
	uint64_t lba = 0;
	zone_output_flags out_flags = ZONE_OF_NORMAL;
	char *filename = NULL;
	struct disk_zone_args zone_args;
	struct disk_zone_rep_entry *entries = NULL;
	uint32_t num_entries = 16384;
	zone_print_status zp_status;
	int first_pass = 1;
	size_t entry_alloc_size;
	int open_flags = O_RDONLY;

	while ((c = getopt(argc, argv, "ac:d:hl:o:P:?")) != -1) {
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
		case 'd':
			filename = strdup(optarg);
			if (filename == NULL)
				err(1, "Unable to allocate memory for "
				    "filename");
			break;
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
		case 'o': {
			scsi_nv_status status;
			int entry_num;

			status = scsi_get_nv(zone_rep_opts,
			    (sizeof(zone_rep_opts) /
			    sizeof(zone_rep_opts[0])),
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
			error = 1;
		case 'h': /*FALLTHROUGH*/
			usage(error);
			goto bailout;
			break; /*NOTREACHED*/
		}
	}

	if (filename == NULL) {
		warnx("You must specify a device with -d");
		error = 1;
	}
	if (action == -1) {
		warnx("You must specify an action with -c");
		error = 1;
	}

	if (error != 0) {
		usage(error);
		goto bailout;
	}

	bzero(&zone_args, sizeof(zone_args));

	zone_args.zone_cmd = action;

	switch (action) {
	case DISK_ZONE_OPEN:
	case DISK_ZONE_CLOSE:
	case DISK_ZONE_FINISH:
	case DISK_ZONE_RWP:
		open_flags = O_RDWR;
		zone_args.zone_params.rwp.id = lba;
		if (all_zones != 0)
			zone_args.zone_params.rwp.flags |=
			    DISK_ZONE_RWP_FLAG_ALL;
		break;
	case DISK_ZONE_REPORT_ZONES: {
		entry_alloc_size = num_entries *
		    sizeof(struct disk_zone_rep_entry);
		entries = malloc(entry_alloc_size);
		if (entries == NULL) {
			warn("Could not allocate %zu bytes",
			    entry_alloc_size);
			error = 1;
			goto bailout;
		}
		zone_args.zone_params.report.entries_allocated = num_entries;
		zone_args.zone_params.report.entries = entries;
		zone_args.zone_params.report.starting_id = lba;
		if (rep_option != -1)
			zone_args.zone_params.report.rep_options = rep_option;
		break;
	}
	case DISK_ZONE_GET_PARAMS:
		break;
	default:
		warnx("Unknown action %d", action);
		error = 1;
		goto bailout;
		break; /*NOTREACHED*/
	}

	fd = open(filename, open_flags);
	if (fd == -1) {
		warn("Unable to open device %s", filename);
		error = 1;
		goto bailout;
	}
next_chunk:
	error = ioctl(fd, DIOCZONECMD, &zone_args);
	if (error == -1) {
		warn("DIOCZONECMD ioctl failed");
		error = 1;
		goto bailout;
	}

	switch (action) {
	case DISK_ZONE_OPEN:
	case DISK_ZONE_CLOSE:
	case DISK_ZONE_FINISH:
	case DISK_ZONE_RWP:
		break;
	case DISK_ZONE_REPORT_ZONES:
		zp_status = zonectl_print_rz(&zone_args.zone_params.report,
		    out_flags, first_pass);
		if (zp_status == ZONE_PRINT_MORE_DATA) {
			first_pass = 0;
			bzero(entries, entry_alloc_size);
			zone_args.zone_params.report.entries_filled = 0;
			goto next_chunk;
		} else if (zp_status == ZONE_PRINT_ERROR)
			error = 1;
		break;
	case DISK_ZONE_GET_PARAMS:
		zonectl_print_params(&zone_args.zone_params.disk_params);
		break;
	default:
		warnx("Unknown action %d", action);
		error = 1;
		goto bailout;
		break; /*NOTREACHED*/
	}
bailout:
	free(entries);

	if (fd != -1)
		close(fd);
	exit (error);
}
