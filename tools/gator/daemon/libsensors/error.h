/*
    error.h - Part of libsensors, a Linux library for reading sensor data.
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

#ifndef LIB_SENSORS_ERROR_H
#define LIB_SENSORS_ERROR_H

#define SENSORS_ERR_WILDCARDS	1 /* Wildcard found in chip name */
#define SENSORS_ERR_NO_ENTRY	2 /* No such subfeature known */
#define SENSORS_ERR_ACCESS_R	3 /* Can't read */
#define SENSORS_ERR_KERNEL	4 /* Kernel interface error */
#define SENSORS_ERR_DIV_ZERO	5 /* Divide by zero */
#define SENSORS_ERR_CHIP_NAME	6 /* Can't parse chip name */
#define SENSORS_ERR_BUS_NAME	7 /* Can't parse bus name */
#define SENSORS_ERR_PARSE	8 /* General parse error */
#define SENSORS_ERR_ACCESS_W	9 /* Can't write */
#define SENSORS_ERR_IO		10 /* I/O error */
#define SENSORS_ERR_RECURSION	11 /* Evaluation recurses too deep */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* This function returns a pointer to a string which describes the error.
   errnum may be negative (the corresponding positive error is returned).
   You may not modify the result! */
const char *sensors_strerror(int errnum);

/* These functions are called when a parse error is detected. Give them new
   values, and your own functions are called instead of the default (which
   print to stderr). These functions may terminate the program, but they
   usually output an error and return. The first function is the original
   one, the second one was added later when support for multiple
   configuration files was added.
   The library code now only calls the second function. However, for
   backwards compatibility, if an application provides a custom handling
   function for the first function but not the second, then all parse
   errors will be reported using the first function (that is, the filename
   is never reported.)
   Note that filename can be NULL (if filename isn't known) and lineno
   can be 0 (if the error occurs before the actual parsing starts.) */
extern void (*sensors_parse_error) (const char *err, int lineno);
extern void (*sensors_parse_error_wfn) (const char *err,
					const char *filename, int lineno);

/* This function is called when an immediately fatal error (like no
   memory left) is detected. Give it a new value, and your own function
   is called instead of the default (which prints to stderr and ends
   the program). Never let it return! */
extern void (*sensors_fatal_error) (const char *proc, const char *err);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* def LIB_SENSORS_ERROR_H */
