/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *	$FreeBSD$
 *
 */

#ifndef _SYS_PCIIO_H_
#define	_SYS_PCIIO_H_

#include <sys/ioccom.h>

#define PCI_MAXNAMELEN	16

typedef enum {
	PCI_GETCONF_LAST_DEVICE,
	PCI_GETCONF_LIST_CHANGED,
	PCI_GETCONF_MORE_DEVS,
	PCI_GETCONF_ERROR
} pci_getconf_status;

typedef enum {
	PCI_GETCONF_NO_MATCH		= 0x0000,
	PCI_GETCONF_MATCH_DOMAIN	= 0x0001,
	PCI_GETCONF_MATCH_BUS		= 0x0002,
	PCI_GETCONF_MATCH_DEV		= 0x0004,
	PCI_GETCONF_MATCH_FUNC		= 0x0008,
	PCI_GETCONF_MATCH_NAME		= 0x0010,
	PCI_GETCONF_MATCH_UNIT		= 0x0020,
	PCI_GETCONF_MATCH_VENDOR	= 0x0040,
	PCI_GETCONF_MATCH_DEVICE	= 0x0080,
	PCI_GETCONF_MATCH_CLASS		= 0x0100
} pci_getconf_flags;

struct pcisel {
	u_int32_t	pc_domain;	/* domain number */
	u_int8_t	pc_bus;		/* bus number */
	u_int8_t	pc_dev;		/* device on this bus */
	u_int8_t	pc_func;	/* function on this device */
};

struct pci_conf {
	struct pcisel	pc_sel;		/* domain+bus+slot+function */
	u_int8_t	pc_hdr;		/* PCI header type */
	u_int16_t	pc_subvendor;	/* card vendor ID */
	u_int16_t	pc_subdevice;	/* card device ID, assigned by 
					   card vendor */
	u_int16_t	pc_vendor;	/* chip vendor ID */
	u_int16_t	pc_device;	/* chip device ID, assigned by 
					   chip vendor */
	u_int8_t	pc_class;	/* chip PCI class */
	u_int8_t	pc_subclass;	/* chip PCI subclass */
	u_int8_t	pc_progif;	/* chip PCI programming interface */
	u_int8_t	pc_revid;	/* chip revision ID */
	char		pd_name[PCI_MAXNAMELEN + 1];  /* device name */
	u_long		pd_unit;	/* device unit number */
};

struct pci_match_conf {
	struct pcisel		pc_sel;		/* domain+bus+slot+function */
	char			pd_name[PCI_MAXNAMELEN + 1];  /* device name */
	u_long			pd_unit;	/* Unit number */
	u_int16_t		pc_vendor;	/* PCI Vendor ID */
	u_int16_t		pc_device;	/* PCI Device ID */
	u_int8_t		pc_class;	/* PCI class */
	pci_getconf_flags	flags;		/* Matching expression */
};

struct pci_conf_io {
	u_int32_t		pat_buf_len;	/* pattern buffer length */
	u_int32_t		num_patterns;	/* number of patterns */
	struct pci_match_conf	*patterns;	/* pattern buffer */
	u_int32_t		match_buf_len;	/* match buffer length */
	u_int32_t		num_matches;	/* number of matches returned */
	struct pci_conf		*matches;	/* match buffer */
	u_int32_t		offset;		/* offset into device list */
	u_int32_t		generation;	/* device list generation */
	pci_getconf_status	status;		/* request status */
};

struct pci_io {
	struct pcisel	pi_sel;		/* device to operate on */
	int		pi_reg;		/* configuration register to examine */
	int		pi_width;	/* width (in bytes) of read or write */
	u_int32_t	pi_data;	/* data to write or result of read */
};

struct pci_bar_io {
	struct pcisel	pbi_sel;	/* device to operate on */
	int		pbi_reg;	/* starting address of BAR */
	int		pbi_enabled;	/* decoding enabled */
	uint64_t	pbi_base;	/* current value of BAR */
	uint64_t	pbi_length;	/* length of BAR */
};

struct pci_vpd_element {
	char		pve_keyword[2];
	uint8_t		pve_flags;
	uint8_t		pve_datalen;
	uint8_t		pve_data[0];
};

#define	PVE_FLAG_IDENT		0x01	/* Element is the string identifier */
#define	PVE_FLAG_RW		0x02	/* Element is read/write */

#define	PVE_NEXT(pve)							\
	((struct pci_vpd_element *)((char *)(pve) +			\
	    sizeof(struct pci_vpd_element) + (pve)->pve_datalen))

struct pci_list_vpd_io {
	struct pcisel	plvi_sel;	/* device to operate on */
	size_t		plvi_len;	/* size of the data area */
	struct pci_vpd_element *plvi_data;
};

struct pci_bar_mmap {
	void		*pbm_map_base;	/* (sometimes IN)/OUT mmaped base */
	size_t		pbm_map_length;	/* mapped length of the BAR, multiple
					   of pages */
	uint64_t	pbm_bar_length;	/* actual length of the BAR */
	int		pbm_bar_off;	/* offset from the mapped base to the
					   start of BAR */
	struct pcisel	pbm_sel;	/* device to operate on */
	int		pbm_reg;	/* starting address of BAR */
	int		pbm_flags;
	int		pbm_memattr;
};

#define	PCIIO_BAR_MMAP_FIXED	0x01
#define	PCIIO_BAR_MMAP_EXCL	0x02
#define	PCIIO_BAR_MMAP_RW	0x04
#define	PCIIO_BAR_MMAP_ACTIVATE	0x08

#define	PCIOCGETCONF	_IOWR('p', 5, struct pci_conf_io)
#define	PCIOCREAD	_IOWR('p', 2, struct pci_io)
#define	PCIOCWRITE	_IOWR('p', 3, struct pci_io)
#define	PCIOCATTACHED	_IOWR('p', 4, struct pci_io)
#define	PCIOCGETBAR	_IOWR('p', 6, struct pci_bar_io)
#define	PCIOCLISTVPD	_IOWR('p', 7, struct pci_list_vpd_io)
#define	PCIOCBARMMAP	_IOWR('p', 8, struct pci_bar_mmap)

#endif /* !_SYS_PCIIO_H_ */
