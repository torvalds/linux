/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPLAT_H
#define _SPLAT_H

#include "list.h"
#include "../include/splat-ctl.h"

#define DEV_NAME			"/dev/splatctl"
#define COLOR_BLACK			"\033[0;30m"
#define COLOR_DK_GRAY			"\033[1;30m"
#define COLOR_BLUE			"\033[0;34m"
#define COLOR_LT_BLUE			"\033[1;34m"
#define COLOR_GREEN			"\033[0;32m"
#define COLOR_LT_GREEN			"\033[1;32m"
#define COLOR_CYAN			"\033[0;36m"
#define COLOR_LT_CYAN			"\033[1;36m"
#define COLOR_RED			"\033[0;31m"
#define COLOR_LT_RED			"\033[1;31m"
#define COLOR_PURPLE			"\033[0;35m"
#define COLOR_LT_PURPLE			"\033[1;35m"
#define COLOR_BROWN			"\033[0;33m"
#define COLOR_YELLOW			"\033[1;33m"
#define COLOR_LT_GRAY			"\033[0;37m"
#define COLOR_WHITE			"\033[1;37m"
#define COLOR_RESET			"\033[0m"

typedef struct subsystem {
	splat_user_t sub_desc;		/* Subsystem description */
	List sub_tests;			/* Assocated subsystem tests list */
} subsystem_t;

typedef struct test {
	splat_user_t test_desc;		/* Test description */
	subsystem_t *test_sub;		/* Parent subsystem */
} test_t;

typedef struct cmd_args {
	int args_verbose;		/* Verbose flag */
	int args_do_list;		/* Display all tests flag */
	int args_do_all;		/* Run all tests flag */
	int args_do_color;		/* Colorize output */
	int args_exit_on_error;		/* Exit on first error flag */
	List args_tests;		/* Requested subsystems/tests */
} cmd_args_t;

#endif /* _SPLAT_H */

