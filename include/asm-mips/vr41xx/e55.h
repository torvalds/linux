/*
 *  e55.h, Include file for CASIO CASSIOPEIA E-10/15/55/65.
 *
 *  Copyright (C) 2002-2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __CASIO_E55_H
#define __CASIO_E55_H

#include <asm/addrspace.h>
#include <asm/vr41xx/vr41xx.h>

/*
 * Board specific address mapping
 */
#define VR41XX_ISA_MEM_BASE		0x10000000
#define VR41XX_ISA_MEM_SIZE		0x04000000

/* VR41XX_ISA_IO_BASE includes offset from real base. */
#define VR41XX_ISA_IO_BASE		0x1400c000
#define VR41XX_ISA_IO_SIZE		0x03ff4000

#define ISA_BUS_IO_BASE			0
#define ISA_BUS_IO_SIZE			VR41XX_ISA_IO_SIZE

#define IO_PORT_BASE			KSEG1ADDR(VR41XX_ISA_IO_BASE)
#define IO_PORT_RESOURCE_START		ISA_BUS_IO_BASE
#define IO_PORT_RESOURCE_END		(ISA_BUS_IO_BASE + ISA_BUS_IO_SIZE - 1)

#endif /* __CASIO_E55_H */
