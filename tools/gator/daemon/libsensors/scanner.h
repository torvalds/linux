/*
    scanner.h - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 2006 Mark M. Hoffman <mhoffman@lightlink.com>

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

/*** This file modified by ARM on Jan 23, 2013 to fix input defined but not used warning from conf-lex.c. ***/

#ifndef LIB_SENSORS_SCANNER_H
#define LIB_SENSORS_SCANNER_H

int sensors_scanner_init(FILE *input, const char *filename);
void sensors_scanner_exit(void);

#define YY_NO_INPUT

#endif

