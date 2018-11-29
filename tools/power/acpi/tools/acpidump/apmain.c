// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: apmain - Main module for the acpidump utility
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#define _DECLARE_GLOBALS
#include "acpidump.h"

/*
 * acpidump - A portable utility for obtaining system ACPI tables and dumping
 * them in an ASCII hex format suitable for binary extraction via acpixtract.
 *
 * Obtaining the system ACPI tables is an OS-specific operation.
 *
 * This utility can be ported to any host operating system by providing a
 * module containing system-specific versions of these interfaces:
 *
 *      acpi_os_get_table_by_address
 *      acpi_os_get_table_by_index
 *      acpi_os_get_table_by_name
 *
 * See the ACPICA Reference Guide for the exact definitions of these
 * interfaces. Also, see these ACPICA source code modules for example
 * implementations:
 *
 *      source/os_specific/service_layers/oswintbl.c
 *      source/os_specific/service_layers/oslinuxtbl.c
 */

/* Local prototypes */

static void ap_display_usage(void);

static int ap_do_options(int argc, char **argv);

static int ap_insert_action(char *argument, u32 to_be_done);

/* Table for deferred actions from command line options */

struct ap_dump_action action_table[AP_MAX_ACTIONS];
u32 current_action = 0;

#define AP_UTILITY_NAME             "ACPI Binary Table Dump Utility"
#define AP_SUPPORTED_OPTIONS        "?a:bc:f:hn:o:r:sv^xz"

/******************************************************************************
 *
 * FUNCTION:    ap_display_usage
 *
 * DESCRIPTION: Usage message for the acpi_dump utility
 *
 ******************************************************************************/

static void ap_display_usage(void)
{

	ACPI_USAGE_HEADER("acpidump [options]");

	ACPI_OPTION("-b", "Dump tables to binary files");
	ACPI_OPTION("-h -?", "This help message");
	ACPI_OPTION("-o <File>", "Redirect output to file");
	ACPI_OPTION("-r <Address>", "Dump tables from specified RSDP");
	ACPI_OPTION("-s", "Print table summaries only");
	ACPI_OPTION("-v", "Display version information");
	ACPI_OPTION("-vd", "Display build date and time");
	ACPI_OPTION("-z", "Verbose mode");

	ACPI_USAGE_TEXT("\nTable Options:\n");

	ACPI_OPTION("-a <Address>", "Get table via a physical address");
	ACPI_OPTION("-c <on|off>", "Turning on/off customized table dumping");
	ACPI_OPTION("-f <BinaryFile>", "Get table via a binary file");
	ACPI_OPTION("-n <Signature>", "Get table via a name/signature");
	ACPI_OPTION("-x", "Do not use but dump XSDT");
	ACPI_OPTION("-x -x", "Do not use or dump XSDT");

	ACPI_USAGE_TEXT("\n"
			"Invocation without parameters dumps all available tables\n"
			"Multiple mixed instances of -a, -f, and -n are supported\n\n");
}

/******************************************************************************
 *
 * FUNCTION:    ap_insert_action
 *
 * PARAMETERS:  argument            - Pointer to the argument for this action
 *              to_be_done          - What to do to process this action
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add an action item to the action table
 *
 ******************************************************************************/

static int ap_insert_action(char *argument, u32 to_be_done)
{

	/* Insert action and check for table overflow */

	action_table[current_action].argument = argument;
	action_table[current_action].to_be_done = to_be_done;

	current_action++;
	if (current_action > AP_MAX_ACTIONS) {
		fprintf(stderr, "Too many table options (max %d)\n",
			AP_MAX_ACTIONS);
		return (-1);
	}

	return (0);
}

/******************************************************************************
 *
 * FUNCTION:    ap_do_options
 *
 * PARAMETERS:  argc/argv           - Standard argc/argv
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Command line option processing. The main actions for getting
 *              and dumping tables are deferred via the action table.
 *
 *****************************************************************************/

static int ap_do_options(int argc, char **argv)
{
	int j;
	acpi_status status;

	/* Command line options */

	while ((j =
		acpi_getopt(argc, argv, AP_SUPPORTED_OPTIONS)) != ACPI_OPT_END)
		switch (j) {
			/*
			 * Global options
			 */
		case 'b':	/* Dump all input tables to binary files */

			gbl_binary_mode = TRUE;
			continue;

		case 'c':	/* Dump customized tables */

			if (!strcmp(acpi_gbl_optarg, "on")) {
				gbl_dump_customized_tables = TRUE;
			} else if (!strcmp(acpi_gbl_optarg, "off")) {
				gbl_dump_customized_tables = FALSE;
			} else {
				fprintf(stderr,
					"%s: Cannot handle this switch, please use on|off\n",
					acpi_gbl_optarg);
				return (-1);
			}
			continue;

		case 'h':
		case '?':

			ap_display_usage();
			return (1);

		case 'o':	/* Redirect output to a single file */

			if (ap_open_output_file(acpi_gbl_optarg)) {
				return (-1);
			}
			continue;

		case 'r':	/* Dump tables from specified RSDP */

			status =
			    acpi_ut_strtoul64(acpi_gbl_optarg, &gbl_rsdp_base);
			if (ACPI_FAILURE(status)) {
				fprintf(stderr,
					"%s: Could not convert to a physical address\n",
					acpi_gbl_optarg);
				return (-1);
			}
			continue;

		case 's':	/* Print table summaries only */

			gbl_summary_mode = TRUE;
			continue;

		case 'x':	/* Do not use XSDT */

			if (!acpi_gbl_do_not_use_xsdt) {
				acpi_gbl_do_not_use_xsdt = TRUE;
			} else {
				gbl_do_not_dump_xsdt = TRUE;
			}
			continue;

		case 'v':	/* -v: (Version): signon already emitted, just exit */

			switch (acpi_gbl_optarg[0]) {
			case '^':	/* -v: (Version) */

				fprintf(stderr,
					ACPI_COMMON_SIGNON(AP_UTILITY_NAME));
				return (1);

			case 'd':

				fprintf(stderr,
					ACPI_COMMON_SIGNON(AP_UTILITY_NAME));
				printf(ACPI_COMMON_BUILD_TIME);
				return (1);

			default:

				printf("Unknown option: -v%s\n",
				       acpi_gbl_optarg);
				return (-1);
			}
			break;

		case 'z':	/* Verbose mode */

			gbl_verbose_mode = TRUE;
			fprintf(stderr, ACPI_COMMON_SIGNON(AP_UTILITY_NAME));
			continue;

			/*
			 * Table options
			 */
		case 'a':	/* Get table by physical address */

			if (ap_insert_action
			    (acpi_gbl_optarg, AP_DUMP_TABLE_BY_ADDRESS)) {
				return (-1);
			}
			break;

		case 'f':	/* Get table from a file */

			if (ap_insert_action
			    (acpi_gbl_optarg, AP_DUMP_TABLE_BY_FILE)) {
				return (-1);
			}
			break;

		case 'n':	/* Get table by input name (signature) */

			if (ap_insert_action
			    (acpi_gbl_optarg, AP_DUMP_TABLE_BY_NAME)) {
				return (-1);
			}
			break;

		default:

			ap_display_usage();
			return (-1);
		}

	/* If there are no actions, this means "get/dump all tables" */

	if (current_action == 0) {
		if (ap_insert_action(NULL, AP_DUMP_ALL_TABLES)) {
			return (-1);
		}
	}

	return (0);
}

/******************************************************************************
 *
 * FUNCTION:    main
 *
 * PARAMETERS:  argc/argv           - Standard argc/argv
 *
 * RETURN:      Status
 *
 * DESCRIPTION: C main function for acpidump utility
 *
 ******************************************************************************/

#if !defined(_GNU_EFI) && !defined(_EDK2_EFI)
int ACPI_SYSTEM_XFACE main(int argc, char *argv[])
#else
int ACPI_SYSTEM_XFACE acpi_main(int argc, char *argv[])
#endif
{
	int status = 0;
	struct ap_dump_action *action;
	u32 file_size;
	u32 i;

	ACPI_DEBUG_INITIALIZE();	/* For debug version only */
	acpi_os_initialize();
	gbl_output_file = ACPI_FILE_OUT;
	acpi_gbl_integer_byte_width = 8;

	/* Process command line options */

	status = ap_do_options(argc, argv);
	if (status > 0) {
		return (0);
	}
	if (status < 0) {
		return (status);
	}

	/* Get/dump ACPI table(s) as requested */

	for (i = 0; i < current_action; i++) {
		action = &action_table[i];
		switch (action->to_be_done) {
		case AP_DUMP_ALL_TABLES:

			status = ap_dump_all_tables();
			break;

		case AP_DUMP_TABLE_BY_ADDRESS:

			status = ap_dump_table_by_address(action->argument);
			break;

		case AP_DUMP_TABLE_BY_NAME:

			status = ap_dump_table_by_name(action->argument);
			break;

		case AP_DUMP_TABLE_BY_FILE:

			status = ap_dump_table_from_file(action->argument);
			break;

		default:

			fprintf(stderr,
				"Internal error, invalid action: 0x%X\n",
				action->to_be_done);
			return (-1);
		}

		if (status) {
			return (status);
		}
	}

	if (gbl_output_filename) {
		if (gbl_verbose_mode) {

			/* Summary for the output file */

			file_size = cm_get_file_size(gbl_output_file);
			fprintf(stderr,
				"Output file %s contains 0x%X (%u) bytes\n\n",
				gbl_output_filename, file_size, file_size);
		}

		fclose(gbl_output_file);
	}

	return (status);
}
