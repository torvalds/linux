/*
 * Some of the code in this file has been gleaned from the 64 bit
 * discontigmem support code base.
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to Pat Gaughen <gone@us.ibm.com>
 */

#ifndef _ASM_SRAT_H_
#define _ASM_SRAT_H_

#ifndef CONFIG_ACPI_SRAT
#error CONFIG_ACPI_SRAT not defined, and srat.h header has been included
#endif

extern int get_memcfg_from_srat(void);
extern unsigned long *get_zholes_size(int);

#endif /* _ASM_SRAT_H_ */
