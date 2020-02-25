// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: getopt
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

/*
 * ACPICA getopt() implementation
 *
 * Option strings:
 *    "f"       - Option has no arguments
 *    "f:"      - Option requires an argument
 *    "f+"      - Option has an optional argument
 *    "f^"      - Option has optional single-char sub-options
 *    "f|"      - Option has required single-char sub-options
 */

#include <acpi/acpi.h>
#include "accommon.h"
#include "acapps.h"

#define ACPI_OPTION_ERROR(msg, badchar) \
	if (acpi_gbl_opterr) {fprintf (stderr, "%s%c\n", msg, badchar);}

int acpi_gbl_opterr = 1;
int acpi_gbl_optind = 1;
int acpi_gbl_sub_opt_char = 0;
char *acpi_gbl_optarg;

static int current_char_ptr = 1;

/*******************************************************************************
 *
 * FUNCTION:    acpi_getopt_argument
 *
 * PARAMETERS:  argc, argv          - from main
 *
 * RETURN:      0 if an argument was found, -1 otherwise. Sets acpi_gbl_Optarg
 *              to point to the next argument.
 *
 * DESCRIPTION: Get the next argument. Used to obtain arguments for the
 *              two-character options after the original call to acpi_getopt.
 *              Note: Either the argument starts at the next character after
 *              the option, or it is pointed to by the next argv entry.
 *              (After call to acpi_getopt, we need to backup to the previous
 *              argv entry).
 *
 ******************************************************************************/

int acpi_getopt_argument(int argc, char **argv)
{

	acpi_gbl_optind--;
	current_char_ptr++;

	if (argv[acpi_gbl_optind][(int)(current_char_ptr + 1)] != '\0') {
		acpi_gbl_optarg =
		    &argv[acpi_gbl_optind++][(int)(current_char_ptr + 1)];
	} else if (++acpi_gbl_optind >= argc) {
		ACPI_OPTION_ERROR("\nOption requires an argument", 0);

		current_char_ptr = 1;
		return (-1);
	} else {
		acpi_gbl_optarg = argv[acpi_gbl_optind++];
	}

	current_char_ptr = 1;
	return (0);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_getopt
 *
 * PARAMETERS:  argc, argv          - from main
 *              opts                - options info list
 *
 * RETURN:      Option character or ACPI_OPT_END
 *
 * DESCRIPTION: Get the next option
 *
 ******************************************************************************/

int acpi_getopt(int argc, char **argv, char *opts)
{
	int current_char;
	char *opts_ptr;

	if (current_char_ptr == 1) {
		if (acpi_gbl_optind >= argc ||
		    argv[acpi_gbl_optind][0] != '-' ||
		    argv[acpi_gbl_optind][1] == '\0') {
			return (ACPI_OPT_END);
		} else if (strcmp(argv[acpi_gbl_optind], "--") == 0) {
			acpi_gbl_optind++;
			return (ACPI_OPT_END);
		}
	}

	/* Get the option */

	current_char = argv[acpi_gbl_optind][current_char_ptr];

	/* Make sure that the option is legal */

	if (current_char == ':' ||
	    (opts_ptr = strchr(opts, current_char)) == NULL) {
		ACPI_OPTION_ERROR("Illegal option: -", current_char);

		if (argv[acpi_gbl_optind][++current_char_ptr] == '\0') {
			acpi_gbl_optind++;
			current_char_ptr = 1;
		}

		return ('?');
	}

	/* Option requires an argument? */

	if (*++opts_ptr == ':') {
		if (argv[acpi_gbl_optind][(int)(current_char_ptr + 1)] != '\0') {
			acpi_gbl_optarg =
			    &argv[acpi_gbl_optind++][(int)
						     (current_char_ptr + 1)];
		} else if (++acpi_gbl_optind >= argc) {
			ACPI_OPTION_ERROR("Option requires an argument: -",
					  current_char);

			current_char_ptr = 1;
			return ('?');
		} else {
			acpi_gbl_optarg = argv[acpi_gbl_optind++];
		}

		current_char_ptr = 1;
	}

	/* Option has an optional argument? */

	else if (*opts_ptr == '+') {
		if (argv[acpi_gbl_optind][(int)(current_char_ptr + 1)] != '\0') {
			acpi_gbl_optarg =
			    &argv[acpi_gbl_optind++][(int)
						     (current_char_ptr + 1)];
		} else if (++acpi_gbl_optind >= argc) {
			acpi_gbl_optarg = NULL;
		} else {
			acpi_gbl_optarg = argv[acpi_gbl_optind++];
		}

		current_char_ptr = 1;
	}

	/* Option has optional single-char arguments? */

	else if (*opts_ptr == '^') {
		if (argv[acpi_gbl_optind][(int)(current_char_ptr + 1)] != '\0') {
			acpi_gbl_optarg =
			    &argv[acpi_gbl_optind][(int)(current_char_ptr + 1)];
		} else {
			acpi_gbl_optarg = "^";
		}

		acpi_gbl_sub_opt_char = acpi_gbl_optarg[0];
		acpi_gbl_optind++;
		current_char_ptr = 1;
	}

	/* Option has a required single-char argument? */

	else if (*opts_ptr == '|') {
		if (argv[acpi_gbl_optind][(int)(current_char_ptr + 1)] != '\0') {
			acpi_gbl_optarg =
			    &argv[acpi_gbl_optind][(int)(current_char_ptr + 1)];
		} else {
			ACPI_OPTION_ERROR
			    ("Option requires a single-character suboption: -",
			     current_char);

			current_char_ptr = 1;
			return ('?');
		}

		acpi_gbl_sub_opt_char = acpi_gbl_optarg[0];
		acpi_gbl_optind++;
		current_char_ptr = 1;
	}

	/* Option with no arguments */

	else {
		if (argv[acpi_gbl_optind][++current_char_ptr] == '\0') {
			current_char_ptr = 1;
			acpi_gbl_optind++;
		}

		acpi_gbl_optarg = NULL;
	}

	return (current_char);
}
