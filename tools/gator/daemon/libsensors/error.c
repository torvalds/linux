/*
    error.c - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
    Copyright (C) 2007-2009   Jean Delvare <khali@linux-fr.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA.
*/

#include <stdlib.h>
#include <stdio.h>
#include "error.h"
#include "general.h"

static void sensors_default_parse_error(const char *err, int lineno);
static void sensors_default_parse_error_wfn(const char *err,
					    const char *filename, int lineno);
static void sensors_default_fatal_error(const char *proc, const char *err);

void (*sensors_parse_error) (const char *err, int lineno) =
						sensors_default_parse_error;
void (*sensors_parse_error_wfn) (const char *err, const char *filename,
				 int lineno) = sensors_default_parse_error_wfn;
void (*sensors_fatal_error) (const char *proc, const char *err) =
						sensors_default_fatal_error;

static const char *errorlist[] = {
	/* Invalid error code    */ "Unknown error",
	/* SENSORS_ERR_WILDCARDS */ "Wildcard found in chip name",
	/* SENSORS_ERR_NO_ENTRY  */ "No such subfeature known",
	/* SENSORS_ERR_ACCESS_R  */ "Can't read",
	/* SENSORS_ERR_KERNEL    */ "Kernel interface error",
	/* SENSORS_ERR_DIV_ZERO  */ "Divide by zero",
	/* SENSORS_ERR_CHIP_NAME */ "Can't parse chip name",
	/* SENSORS_ERR_BUS_NAME  */ "Can't parse bus name",
	/* SENSORS_ERR_PARSE     */ "General parse error",
	/* SENSORS_ERR_ACCESS_W  */ "Can't write",
	/* SENSORS_ERR_IO        */ "I/O error",
	/* SENSORS_ERR_RECURSION */ "Evaluation recurses too deep",
};

const char *sensors_strerror(int errnum)
{
	if (errnum < 0)
		errnum = -errnum;
	if (errnum >= ARRAY_SIZE(errorlist))
		errnum = 0;
	return errorlist[errnum];
}

void sensors_default_parse_error(const char *err, int lineno)
{
	if (lineno)
		fprintf(stderr, "Error: Line %d: %s\n", lineno, err);
	else
		fprintf(stderr, "Error: %s\n", err);
}

void sensors_default_parse_error_wfn(const char *err,
				     const char *filename, int lineno)
{
	/* If application provided a custom parse error reporting function
	   but not the variant with the filename, fall back to the original
	   variant without the filename, for backwards compatibility. */
	if (sensors_parse_error != sensors_default_parse_error ||
	    !filename)
		return sensors_parse_error(err, lineno);

	if (lineno)
		fprintf(stderr, "Error: File %s, line %d: %s\n", filename,
			lineno, err);
	else
		fprintf(stderr, "Error: File %s: %s\n", filename, err);
}

void sensors_default_fatal_error(const char *proc, const char *err)
{
	fprintf(stderr, "Fatal error in `%s': %s\n", proc, err);
	exit(1);
}
