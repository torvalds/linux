/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000-2001 by Coleman Kane <cokane@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Gardner Buchanan.
 * 4. The name of Gardner Buchanan may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *   $FreeBSD$
 */

/* tdfx_vars.h -- constants and structs used in the tdfx driver
	Copyright (C) 2000-2001 by Coleman Kane <cokane@FreeBSD.org>
*/
#ifndef	TDFX_VARS_H
#define	TDFX_VARS_H

#include <sys/memrange.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cdefs.h>

#define  PCI_DEVICE_ALLIANCE_AT3D	0x643d1142
#define	PCI_DEVICE_3DFX_VOODOO1		0x0001121a
#define	PCI_DEVICE_3DFX_VOODOO2		0x0002121a
#define	PCI_DEVICE_3DFX_BANSHEE		0x0003121a
#define	PCI_DEVICE_3DFX_VOODOO3		0x0005121a

#define PCI_VENDOR_ID_FREEBSD      0x0
#define PCI_DEVICE_ID_FREEBSD      0x2
#define PCI_COMMAND_FREEBSD        0x4
#define PCI_REVISION_ID_FREEBSD    0x8
#define PCI_BASE_ADDRESS_0_FREEBSD 0x10
#define PCI_BASE_ADDRESS_1_FREEBSD 0x14
#define PCI_PRIBUS_FREEBSD         0x18
#define PCI_IOBASE_0_FREEBSD       0x2c
#define PCI_IOLIMIT_0_FREEBSD      0x30
#define SST1_PCI_SPECIAL1_FREEBSD  0x40
#define SST1_PCI_SPECIAL2_FREEBSD  0x44
#define SST1_PCI_SPECIAL3_FREEBSD  0x48
#define SST1_PCI_SPECIAL4_FREEBSD  0x54

#define VGA_INPUT_STATUS_1C 0x3DA
#define VGA_MISC_OUTPUT_READ 0x3cc
#define VGA_MISC_OUTPUT_WRITE 0x3c2
#define SC_INDEX 0x3c4
#define SC_DATA  0x3c5

#define PCI_MAP_REG_START 0x10
#define UNIT(m)	(m & 0xf)

/* IOCTL Calls */
#define	TDFX_IOC_TYPE_PIO		0
#define	TDFX_IOC_TYPE_QUERY	'3'
#define	TDFX_IOC_QRY_BOARDS	2
#define	TDFX_IOC_QRY_FETCH	3
#define	TDFX_IOC_QRY_UPDATE	4

struct tdfx_softc {
	int cardno;
	vm_offset_t addr, addr2;
	struct resource *memrange, *memrange2, *piorange;
	int memrid, memrid2, piorid;
	long range;
	int vendor;
	int type;
	int addr0, addr1;
	short pio0, pio0max;
	unsigned char bus;
	unsigned char dv;
	struct file *curFile;
	device_t dev;
	struct cdev *devt;
	struct mem_range_desc mrdesc;
	int busy;
};

struct tdfx_pio_data {
	short port;
	short size;
	int device;
	void *value;
};

#endif /* TDFX_VARS_H */
