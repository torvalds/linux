/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Michael Smith
 * Copyright (c) 1998 Jonathan Lemon
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_PC_BIOS_H_
#define _MACHINE_PC_BIOS_H_

/* 
 * Signature structure for the BIOS32 Service Directory header 
 */
struct bios32_SDheader 
{
    u_int8_t	sig[4];
    u_int32_t	entry;
    u_int8_t	revision;
    u_int8_t	len;
    u_int8_t	cksum;
    u_int8_t	pad[5];
};

/* 
 * PnP BIOS presence structure
 */
struct PnPBIOS_table 
{
    u_int8_t	sig[4];			/* "$PnP */
    u_int8_t	version;		/* should be 0x10 */
    u_int8_t	len;    		/* total structure length */
    u_int16_t	control;		/* BIOS feature flags */
    u_int8_t	cksum;			/* checksum */
    u_int32_t	evflagaddr;		/* address of event notificaton flag */
    u_int16_t	rmentryoffset;		/* real-mode entry offset */
    u_int16_t	rmentryseg;		/*                 segment */
    u_int16_t	pmentryoffset;		/* protected-mode entry offset */
    u_int32_t	pmentrybase;		/*                segment base */
    u_int32_t	oemdevid;		/* motherboard EISA ID */
    u_int16_t	rmbiosseg;		/* real-mode BIOS segment */
    u_int32_t	pmdataseg;		/* protected-mode data segment */
} __packed;

/*
 * PnP BIOS return codes
 */
#define PNP_SUCCESS				0x00
#define PNP_NOT_SET_STATICALLY			0x7f
#define PNP_UNKNOWN_FUNCTION			0x81
#define PNP_FUNCTION_NOT_SUPPORTED		0x82
#define PNP_INVALID_HANDLE			0x83
#define PNP_BAD_PARAMETER			0x84
#define PNP_SET_FAILED				0x85
#define PNP_EVENTS_NOT_PENDING			0x86
#define PNP_SYSTEM_NOT_DOCKED			0x87
#define PNP_NO_ISA_PNP_CARDS			0x88
#define PNP_UNABLE_TO_DETERMINE_DOCK_CAPABILITIES 0x89
#define PNP_CONFIG_CHANGE_FAILED_NO_BATTERY	0x8a
#define PNP_CONFIG_CHANGE_FAILED_RESOURCE_CONFLICT 0x8b
#define PNP_BUFFER_TOO_SMALL			0x8c
#define PNP_USE_ESCD_SUPPORT			0x8d
#define PNP_MESSAGE_NOT_SUPPORTED		0x8e
#define PNP_HARDWARE_ERROR			0x8f

/*
 * DMI return codes
 */
#define DMI_SUCCESS				0x00
#define DMI_UNKNOWN_FUNCTION			0x81
#define DMI_FUNCTION_NOT_SUPPORTED		0x82
#define DMI_INVALID_HANDLE			0x83
#define DMI_BAD_PARAMETER			0x84
#define DMI_INVALID_SUBFUNCTION			0x85
#define DMI_NO_CHANGE				0x86
#define DMI_ADD_STRUCTURE_FAILED		0x87
#define DMI_READ_ONLY				0x8d
#define DMI_LOCK_NOT_SUPPORTED			0x90
#define DMI_CURRENTLY_LOCKED			0x91
#define DMI_INVALID_LOCK			0x92

/*
 * format specifiers and defines for bios16()
 *     s	= short (16 bits)
 *     i	= int (32 bits)
 *     p	= pointer (converted to seg:offset)
 *     C,D,U 	= selector (corresponding to code/data/utility segment)
 */
#define PNP_COUNT_DEVNODES	"sppD",		0x00
#define PNP_GET_DEVNODE		"sppsD",	0x01
#define PNP_SET_DEVNODE		"sspsD",	0x02
#define PNP_GET_EVENT		"spD",		0x03
#define PNP_SEND_MSG		"ssD",		0x04
#define PNP_GET_DOCK_INFO	"spD",		0x05

#define PNP_SEL_PRIBOOT		"ssiiisspD",	0x07
#define PNP_GET_PRIBOOT		"sspppppD",	0x08
#define PNP_SET_RESINFO		"spD",		0x09
#define PNP_GET_RESINFO		"spD",		0x0A
#define PNP_GET_APM_ID		"sppD",		0x0B

#define PNP_GET_ISA_INFO	"spD",		0x40
#define PNP_GET_ECSD_INFO	"spppD",	0x41
#define PNP_READ_ESCD		"spUD",		0x42
#define PNP_WRITE_ESCD		"spUD",		0x43

#define PNP_GET_DMI_INFO	"spppppD",	0x50
#define PNP_GET_DMI_STRUCTURE	"sppUD",	0x51
#define PNP_SET_DMI_STRUCTURE	"sppsUD"	0x52
#define PNP_GET_DMI_CHANGE	"spUD"		0x53
#define PNP_DMI_CONTROL		"sspsUD"	0x54
#define PNP_GET_GPNV_INFO	"sppppD"	0x55
#define PNP_READ_GPNV_DATA	"ssppUD"	0x56
#define PNP_WRITE_GPNV_DATA	"sspsUD"	0x57

#define PNP_BOOT_CHECK		"sp",		0x60
#define PNP_COUNT_IPL		"sppp",		0x61
#define PNP_GET_BOOTPRI		"spp",		0x62
#define PNP_SET_BOOTPRI		"sp",		0x63
#define PNP_GET_LASTBOOT	"sp",		0x64
#define PNP_GET_BOOTFIRST	"sp",		0x65
#define PNP_SET_BOOTFIRST	"sp",		0x66

/*
 * PCI BIOS functions
 */
#define PCIBIOS_BIOS_PRESENT		0xb101
#define PCIBIOS_READ_CONFIG_BYTE	0xb108
#define PCIBIOS_READ_CONFIG_WORD	0xb109
#define PCIBIOS_READ_CONFIG_DWORD	0xb10a
#define PCIBIOS_WRITE_CONFIG_BYTE	0xb10b
#define PCIBIOS_WRITE_CONFIG_WORD	0xb10c
#define PCIBIOS_WRITE_CONFIG_DWORD	0xb10d
#define PCIBIOS_GET_IRQ_ROUTING		0xb10e
#define PCIBIOS_ROUTE_INTERRUPT		0xb10f

/*
 * PCI interrupt routing table.
 *
 * $PIR in the BIOS segment contains a PIR_table
 * int 1a:b106 returns PIR_table in buffer at es:(e)di 
 * int 1a:b18e returns PIR_table in buffer at es:(e)di 
 * int 1a:b406 returns es:di pointing to the BIOS PIR_table
 */
struct PIR_header 
{
    int8_t	ph_signature[4];
    u_int16_t	ph_version;
    u_int16_t	ph_length;
    u_int8_t	ph_router_bus;
    u_int8_t	ph_router_dev_fn;
    u_int16_t	ph_pci_irqs;
    u_int16_t	ph_router_vendor;
    u_int16_t	ph_router_device;
    u_int32_t	ph_miniport;
    u_int8_t	ph_res[11];
    u_int8_t	ph_checksum;
} __packed;

struct PIR_intpin 
{
    u_int8_t	link;
    u_int16_t	irqs;
} __packed;

struct PIR_entry
{
    u_int8_t		pe_bus;
    u_int8_t		pe_res1:3;
    u_int8_t		pe_device:5;
    struct PIR_intpin	pe_intpin[4];
    u_int8_t	pe_slot;
    u_int8_t	pe_res3;
} __packed;

struct PIR_table 
{
    struct PIR_header	pt_header;
    struct PIR_entry	pt_entry[0];
} __packed;

/*
 * Int 15:E820 'SMAP' structure
 */
#define SMAP_SIG	0x534D4150			/* 'SMAP' */

#define	SMAP_TYPE_MEMORY	1
#define	SMAP_TYPE_RESERVED	2
#define	SMAP_TYPE_ACPI_RECLAIM	3
#define	SMAP_TYPE_ACPI_NVS	4
#define	SMAP_TYPE_ACPI_ERROR	5
#define	SMAP_TYPE_DISABLED	6
#define	SMAP_TYPE_PMEM		7
#define	SMAP_TYPE_PRAM		12

#define	SMAP_XATTR_ENABLED	0x00000001
#define	SMAP_XATTR_NON_VOLATILE	0x00000002
#define	SMAP_XATTR_MASK		(SMAP_XATTR_ENABLED | SMAP_XATTR_NON_VOLATILE)

struct bios_smap {
    u_int64_t	base;
    u_int64_t	length;
    u_int32_t	type;
} __packed;

/* Structure extended to include extended attribute field in ACPI 3.0. */
struct bios_smap_xattr {
    u_int64_t	base;
    u_int64_t	length;
    u_int32_t	type;
    u_int32_t	xattr;
} __packed;

/*
 * System Management BIOS
 */
#define	SMBIOS_START	0xf0000
#define	SMBIOS_STEP	0x10
#define	SMBIOS_OFF	0
#define	SMBIOS_LEN	4
#define	SMBIOS_SIG	"_SM_"

struct smbios_eps {
	uint8_t		anchor_string[4];		/* '_SM_' */
	uint8_t		checksum;
	uint8_t		length;
	uint8_t		major_version;
	uint8_t		minor_version;
	uint16_t	maximum_structure_size;
	uint8_t		entry_point_revision;
	uint8_t		formatted_area[5];
	uint8_t		intermediate_anchor_string[5];	/* '_DMI_' */
	uint8_t		intermediate_checksum;
	uint16_t	structure_table_length;
	uint32_t	structure_table_address;
	uint16_t	number_structures;
	uint8_t		BCD_revision;
};

struct smbios_structure_header {
	uint8_t		type;
	uint8_t		length;
	uint16_t	handle;
};

#ifdef _KERNEL
#define BIOS_PADDRTOVADDR(x)	((x) + PMAP_MAP_LOW)
#define BIOS_VADDRTOPADDR(x)	((x) - PMAP_MAP_LOW)

struct bios_oem_signature {
	char * anchor;		/* search anchor string in BIOS memory */
	size_t offset;		/* offset from anchor (may be negative) */
	size_t totlen;		/* total length of BIOS string to copy */
} __packed;

struct bios_oem_range {
	u_int from;		/* shouldn't be below 0xe0000 */
	u_int to;		/* shouldn't be above 0xfffff */
} __packed;

struct bios_oem {
	struct bios_oem_range range;
	struct bios_oem_signature signature[];
} __packed;

struct segment_info {
	u_int	base;
	u_int	limit;
};

#define BIOSCODE_FLAG	0x01
#define BIOSDATA_FLAG	0x02
#define BIOSUTIL_FLAG	0x04
#define BIOSARGS_FLAG	0x08

struct bios_segments {
	struct	segment_info code32;		/* 32-bit code (mandatory) */
	struct	segment_info code16;		/* 16-bit code */
	struct	segment_info data;		/* 16-bit data */
	struct	segment_info util;		/* 16-bit utility */
	struct	segment_info args;		/* 16-bit args */
};

struct bios_regs {
	u_int	eax;
	u_int	ebx;
	u_int	ecx;
	u_int	edx;
	u_int	esi;
	u_int	edi;
};

struct bios_args {
	u_int	entry;				/* entry point of routine */
	struct	bios_regs r;
	struct	bios_segments seg;
};

/* 
 * BIOS32 Service Directory entry.  Caller supplies name, bios32_SDlookup
 * fills in the rest of the details.
 */
struct bios32_SDentry 
{
    union 
    {
	u_int8_t	name[4];	/* service identifier */
	u_int32_t	id;		/* as a 32-bit value */
    } ident;
    u_int32_t	base;			/* base of service */
    u_int32_t	len;			/* service length */
    u_int32_t	entry;			/* entrypoint offset from base */
    vm_offset_t	ventry;			/* entrypoint in kernel virtual segment */
};

/*
 * Exported lookup results 
 */
extern struct bios32_SDentry	PCIbios;

int	bios_oem_strings(struct bios_oem *oem, u_char *buffer, size_t maxlen);
uint32_t bios_sigsearch(uint32_t start, u_char *sig, int siglen, int paralen,
	    int sigofs);
int	bios16(struct bios_args *, char *, ...);
int	bios16_call(struct bios_regs *, char *);
int	bios32(struct bios_regs *, u_int, u_short);
int	bios32_SDlookup(struct bios32_SDentry *ent);
void	set_bios_selectors(struct bios_segments *, int);

#endif

#endif /* _MACHINE_PC_BIOS_H_ */
