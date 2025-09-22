/*	$OpenBSD: pciio.h,v 1.8 2020/06/22 04:11:37 dlg Exp $	*/

/*-
 * Copyright (c) 1997, Stefan Esser <se@FreeBSD.ORG>
 * Copyright (c) 1997, 1998, 1999, Kenneth D. Merry <ken@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *	$FreeBSD: src/sys/sys/pciio.h,v 1.5 1999/12/08 17:44:04 ken Exp $
 *
 */

#ifndef _SYS_PCIIO_H_
#define	_SYS_PCIIO_H_

#include <sys/ioccom.h>

struct pcisel {
	u_int8_t	pc_bus;		/* bus number */
	u_int8_t	pc_dev;		/* device on this bus */
	u_int8_t	pc_func;	/* function on this device */
};

struct pci_io {
	struct pcisel	pi_sel;		/* device to operate on */
	int		pi_reg;		/* configuration register to examine */
	int		pi_width;	/* width (in bytes) of read or write */
	u_int32_t	pi_data;	/* data to write or result of read */
};

struct pci_rom {
	struct pcisel	pr_sel;
	int		pr_romlen;
	char		*pr_rom;
};

struct pci_vpd_req {
	struct pcisel	pv_sel;
	int		pv_offset;
	int		pv_count;
	uint32_t	*pv_data;
};

struct pci_vga {
	struct pcisel	pv_sel;
	int		pv_lock;
	int		pv_decode;
};

#define	PCI_VGA_UNLOCK		0x00
#define	PCI_VGA_LOCK		0x01
#define	PCI_VGA_TRYLOCK		0x02

#define	PCI_VGA_IO_ENABLE	0x01
#define	PCI_VGA_MEM_ENABLE	0x02

#define	PCIOCREAD	_IOWR('p', 2, struct pci_io)
#define	PCIOCWRITE	_IOWR('p', 3, struct pci_io)
#define	PCIOCGETROMLEN	_IOWR('p', 4, struct pci_rom)
#define	PCIOCGETROM	_IOWR('p', 5, struct pci_rom)
#define	PCIOCGETVGA	_IOWR('p', 6, struct pci_vga)
#define	PCIOCSETVGA	_IOWR('p', 7, struct pci_vga)
#define	PCIOCREADMASK	_IOWR('p', 8, struct pci_io)
#define	PCIOCGETVPD	_IOWR('p', 9, struct pci_vpd_req)

#endif /* !_SYS_PCIIO_H_ */
