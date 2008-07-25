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

#ifndef _ASM_POWERPC_DCR_GENERIC_H
#define _ASM_POWERPC_DCR_GENERIC_H
#ifdef __KERNEL__
#ifndef __ASSEMBLY__

enum host_type_t {DCR_HOST_MMIO, DCR_HOST_NATIVE, DCR_HOST_INVALID};

typedef struct {
	enum host_type_t type;
	union {
		dcr_host_mmio_t mmio;
		dcr_host_native_t native;
	} host;
} dcr_host_t;

extern bool dcr_map_ok_generic(dcr_host_t host);

extern dcr_host_t dcr_map_generic(struct device_node *dev, unsigned int dcr_n,
			  unsigned int dcr_c);
extern void dcr_unmap_generic(dcr_host_t host, unsigned int dcr_c);

extern u32 dcr_read_generic(dcr_host_t host, unsigned int dcr_n);

extern void dcr_write_generic(dcr_host_t host, unsigned int dcr_n, u32 value);

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_GENERIC_H */


