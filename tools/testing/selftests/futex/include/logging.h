/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      Glibc independent futex library for testing kernel functionality.
 *
 * AUTHOR
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2009-Nov-6: Initial version by Darren Hart <dvhart@linux.intel.com>
 *
 *****************************************************************************/

#ifndef _LOGGING_H
#define _LOGGING_H

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/futex.h>
#include "kselftest.h"

/*
 * Define PASS, ERROR, and FAIL strings with and without color escape
 * sequences, default to no color.
 */
#define ESC 0x1B, '['
#define BRIGHT '1'
#define GREEN '3', '2'
#define YELLOW '3', '3'
#define RED '3', '1'
#define ESCEND 'm'
#define BRIGHT_GREEN ESC, BRIGHT, ';', GREEN, ESCEND
#define BRIGHT_YELLOW ESC, BRIGHT, ';', YELLOW, ESCEND
#define BRIGHT_RED ESC, BRIGHT, ';', RED, ESCEND
#define RESET_COLOR ESC, '0', 'm'
static const char PASS_COLOR[] = {BRIGHT_GREEN, ' ', 'P', 'A', 'S', 'S',
				  RESET_COLOR, 0};
static const char ERROR_COLOR[] = {BRIGHT_YELLOW, 'E', 'R', 'R', 'O', 'R',
				   RESET_COLOR, 0};
static const char FAIL_COLOR[] = {BRIGHT_RED, ' ', 'F', 'A', 'I', 'L',
				  RESET_COLOR, 0};
static const char INFO_NORMAL[] = " INFO";
static const char PASS_NORMAL[] = " PASS";
static const char ERROR_NORMAL[] = "ERROR";
static const char FAIL_NORMAL[] = " FAIL";
const char *INFO = INFO_NORMAL;
const char *PASS = PASS_NORMAL;
const char *ERROR = ERROR_NORMAL;
const char *FAIL = FAIL_NORMAL;

/* Verbosity setting for INFO messages */
#define VQUIET    0
#define VCRITICAL 1
#define VINFO     2
#define VMAX      VINFO
int _verbose = VCRITICAL;

/* Functional test return codes */
#define RET_PASS   0
#define RET_ERROR -1
#define RET_FAIL  -2

/**
 * log_color() - Use colored output for PASS, ERROR, and FAIL strings
 * @use_color:	use color (1) or not (0)
 */
void log_color(int use_color)
{
	if (use_color) {
		PASS = PASS_COLOR;
		ERROR = ERROR_COLOR;
		FAIL = FAIL_COLOR;
	} else {
		PASS = PASS_NORMAL;
		ERROR = ERROR_NORMAL;
		FAIL = FAIL_NORMAL;
	}
}

/**
 * log_verbosity() - Set verbosity of test output
 * @verbose:	Enable (1) verbose output or not (0)
 *
 * Currently setting verbose=1 will enable INFO messages and 0 will disable
 * them. FAIL and ERROR messages are always displayed.
 */
void log_verbosity(int level)
{
	if (level > VMAX)
		level = VMAX;
	else if (level < 0)
		level = 0;
	_verbose = level;
}

/**
 * print_result() - Print standard PASS | ERROR | FAIL results
 * @ret:	the return value to be considered: 0 | RET_ERROR | RET_FAIL
 *
 * print_result() is primarily intended for functional tests.
 */
void print_result(const char *test_name, int ret)
{
	switch (ret) {
	case RET_PASS:
		ksft_test_result_pass("%s\n", test_name);
		ksft_print_cnts();
		return;
	case RET_ERROR:
		ksft_test_result_error("%s\n", test_name);
		ksft_print_cnts();
		return;
	case RET_FAIL:
		ksft_test_result_fail("%s\n", test_name);
		ksft_print_cnts();
		return;
	}
}

/* log level macros */
#define info(message, vargs...) \
do { \
	if (_verbose >= VINFO) \
		fprintf(stderr, "\t%s: "message, INFO, ##vargs); \
} while (0)

#define error(message, err, args...) \
do { \
	if (_verbose >= VCRITICAL) {\
		if (err) \
			fprintf(stderr, "\t%s: %s: "message, \
				ERROR, strerror(err), ##args); \
		else \
			fprintf(stderr, "\t%s: "message, ERROR, ##args); \
	} \
} while (0)

#define fail(message, args...) \
do { \
	if (_verbose >= VCRITICAL) \
		fprintf(stderr, "\t%s: "message, FAIL, ##args); \
} while (0)

#endif
