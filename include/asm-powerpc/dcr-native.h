/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ASM_POWERPC_DCR_NATIVE_H
#define _ASM_POWERPC_DCR_NATIVE_H
#ifdef __KERNEL__

#include <asm/reg.h>

typedef struct {} dcr_host_t;

#define DCR_MAP_OK(host)	(1)

#define dcr_map(dev, dcr_n, dcr_c)	{}
#define dcr_unmap(host, dcr_n, dcr_c)	{}
#define dcr_read(host, dcr_n)		mfdcr(dcr_n)
#define dcr_write(host, dcr_n, value)	mtdcr(dcr_n, value)


#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_NATIVE_H */


