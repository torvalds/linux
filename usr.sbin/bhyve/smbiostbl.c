/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <assert.h>
#include <errno.h>
#include <md5.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uuid.h>

#include <machine/vmm.h>
#include <vmmapi.h>

#include "bhyverun.h"
#include "smbiostbl.h"

#define	MB			(1024*1024)
#define	GB			(1024ULL*1024*1024)

#define SMBIOS_BASE		0xF1000

/* BHYVE_ACPI_BASE - SMBIOS_BASE) */
#define	SMBIOS_MAX_LENGTH	(0xF2400 - 0xF1000)

#define	SMBIOS_TYPE_BIOS	0
#define	SMBIOS_TYPE_SYSTEM	1
#define	SMBIOS_TYPE_CHASSIS	3
#define	SMBIOS_TYPE_PROCESSOR	4
#define	SMBIOS_TYPE_MEMARRAY	16
#define	SMBIOS_TYPE_MEMDEVICE	17
#define	SMBIOS_TYPE_MEMARRAYMAP	19
#define	SMBIOS_TYPE_BOOT	32
#define	SMBIOS_TYPE_EOT		127

struct smbios_structure {
	uint8_t		type;
	uint8_t		length;
	uint16_t	handle;
} __packed;

typedef int (*initializer_func_t)(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

struct smbios_template_entry {
	struct smbios_structure	*entry;
	const char		**strings;
	initializer_func_t	initializer;
};

/*
 * SMBIOS Structure Table Entry Point
 */
#define	SMBIOS_ENTRY_EANCHOR	"_SM_"
#define	SMBIOS_ENTRY_EANCHORLEN	4
#define	SMBIOS_ENTRY_IANCHOR	"_DMI_"
#define	SMBIOS_ENTRY_IANCHORLEN	5

struct smbios_entry_point {
	char		eanchor[4];	/* anchor tag */
	uint8_t		echecksum;	/* checksum of entry point structure */
	uint8_t		eplen;		/* length in bytes of entry point */
	uint8_t		major;		/* major version of the SMBIOS spec */
	uint8_t		minor;		/* minor version of the SMBIOS spec */
	uint16_t	maxssize;	/* maximum size in bytes of a struct */
	uint8_t		revision;	/* entry point structure revision */
	uint8_t		format[5];	/* entry point rev-specific data */
	char		ianchor[5];	/* intermediate anchor tag */
	uint8_t		ichecksum;	/* intermediate checksum */
	uint16_t	stlen;		/* len in bytes of structure table */
	uint32_t	staddr;		/* physical addr of structure table */
	uint16_t	stnum;		/* number of structure table entries */
	uint8_t		bcdrev;		/* BCD value representing DMI ver */
} __packed;

/*
 * BIOS Information
 */
#define	SMBIOS_FL_ISA		0x00000010	/* ISA is supported */
#define	SMBIOS_FL_PCI		0x00000080	/* PCI is supported */
#define	SMBIOS_FL_SHADOW	0x00001000	/* BIOS shadowing is allowed */
#define	SMBIOS_FL_CDBOOT	0x00008000	/* Boot from CD is supported */
#define	SMBIOS_FL_SELBOOT	0x00010000	/* Selectable Boot supported */
#define	SMBIOS_FL_EDD		0x00080000	/* EDD Spec is supported */

#define	SMBIOS_XB1_FL_ACPI	0x00000001	/* ACPI is supported */

#define	SMBIOS_XB2_FL_BBS	0x00000001	/* BIOS Boot Specification */
#define	SMBIOS_XB2_FL_VM	0x00000010	/* Virtual Machine */

struct smbios_table_type0 {
	struct smbios_structure	header;
	uint8_t			vendor;		/* vendor string */
	uint8_t			version;	/* version string */
	uint16_t		segment;	/* address segment location */
	uint8_t			rel_date;	/* release date */
	uint8_t			size;		/* rom size */
	uint64_t		cflags;		/* characteristics */
	uint8_t			xc_bytes[2];	/* characteristics ext bytes */
	uint8_t			sb_major_rel;	/* system bios version */
	uint8_t			sb_minor_rele;
	uint8_t			ecfw_major_rel;	/* embedded ctrl fw version */
	uint8_t			ecfw_minor_rel;
} __packed;

/*
 * System Information
 */
#define	SMBIOS_WAKEUP_SWITCH	0x06	/* power switch */

struct smbios_table_type1 {
	struct smbios_structure	header;
	uint8_t			manufacturer;	/* manufacturer string */
	uint8_t			product;	/* product name string */
	uint8_t			version;	/* version string */
	uint8_t			serial;		/* serial number string */
	uint8_t			uuid[16];	/* uuid byte array */
	uint8_t			wakeup;		/* wake-up event */
	uint8_t			sku;		/* sku number string */
	uint8_t			family;		/* family name string */
} __packed;

/*
 * System Enclosure or Chassis
 */
#define	SMBIOS_CHT_UNKNOWN	0x02	/* unknown */

#define	SMBIOS_CHST_SAFE	0x03	/* safe */

#define	SMBIOS_CHSC_NONE	0x03	/* none */

struct smbios_table_type3 {
	struct smbios_structure	header;
	uint8_t			manufacturer;	/* manufacturer string */
	uint8_t			type;		/* type */
	uint8_t			version;	/* version string */
	uint8_t			serial;		/* serial number string */
	uint8_t			asset;		/* asset tag string */
	uint8_t			bustate;	/* boot-up state */
	uint8_t			psstate;	/* power supply state */
	uint8_t			tstate;		/* thermal state */
	uint8_t			security;	/* security status */
	uint8_t			uheight;	/* height in 'u's */
	uint8_t			cords;		/* number of power cords */
	uint8_t			elems;		/* number of element records */
	uint8_t			elemlen;	/* length of records */
	uint8_t			sku;		/* sku number string */
} __packed;

/*
 * Processor Information
 */
#define	SMBIOS_PRT_CENTRAL	0x03	/* central processor */

#define	SMBIOS_PRF_OTHER	0x01	/* other */

#define	SMBIOS_PRS_PRESENT	0x40	/* socket is populated */
#define	SMBIOS_PRS_ENABLED	0x1	/* enabled */

#define	SMBIOS_PRU_NONE		0x06	/* none */

#define	SMBIOS_PFL_64B	0x04	/* 64-bit capable */

struct smbios_table_type4 {
	struct smbios_structure	header;
	uint8_t			socket;		/* socket designation string */
	uint8_t			type;		/* processor type */
	uint8_t			family;		/* processor family */
	uint8_t			manufacturer;	/* manufacturer string */
	uint64_t		cpuid;		/* processor cpuid */
	uint8_t			version;	/* version string */
	uint8_t			voltage;	/* voltage */
	uint16_t		clkspeed;	/* ext clock speed in mhz */
	uint16_t		maxspeed;	/* maximum speed in mhz */
	uint16_t		curspeed;	/* current speed in mhz */
	uint8_t			status;		/* status */
	uint8_t			upgrade;	/* upgrade */
	uint16_t		l1handle;	/* l1 cache handle */
	uint16_t		l2handle;	/* l2 cache handle */
	uint16_t		l3handle;	/* l3 cache handle */
	uint8_t			serial;		/* serial number string */
	uint8_t			asset;		/* asset tag string */
	uint8_t			part;		/* part number string */
	uint8_t			cores;		/* cores per socket */
	uint8_t			ecores;		/* enabled cores */
	uint8_t			threads;	/* threads per socket */
	uint16_t		cflags;		/* processor characteristics */
	uint16_t		family2;	/* processor family 2 */
} __packed;

/*
 * Physical Memory Array
 */
#define	SMBIOS_MAL_SYSMB	0x03	/* system board or motherboard */

#define	SMBIOS_MAU_SYSTEM	0x03	/* system memory */

#define	SMBIOS_MAE_NONE		0x03	/* none */

struct smbios_table_type16 {
	struct smbios_structure	header;
	uint8_t			location;	/* physical device location */
	uint8_t			use;		/* device functional purpose */
	uint8_t			ecc;		/* err detect/correct method */
	uint32_t		size;		/* max mem capacity in kb */
	uint16_t		errhand;	/* handle of error (if any) */
	uint16_t		ndevs;		/* num of slots or sockets */
	uint64_t		xsize;		/* max mem capacity in bytes */
} __packed;

/*
 * Memory Device
 */
#define	SMBIOS_MDFF_UNKNOWN	0x02	/* unknown */

#define	SMBIOS_MDT_UNKNOWN	0x02	/* unknown */

#define	SMBIOS_MDF_UNKNOWN	0x0004	/* unknown */

struct smbios_table_type17 {
	struct smbios_structure	header;
	uint16_t		arrayhand;	/* handle of physl mem array */
	uint16_t		errhand;	/* handle of mem error data */
	uint16_t		twidth;		/* total width in bits */
	uint16_t		dwidth;		/* data width in bits */
	uint16_t		size;		/* size in bytes */
	uint8_t			form;		/* form factor */
	uint8_t			set;		/* set */
	uint8_t			dloc;		/* device locator string */
	uint8_t			bloc;		/* phys bank locator string */
	uint8_t			type;		/* memory type */
	uint16_t		flags;		/* memory characteristics */
	uint16_t		maxspeed;	/* maximum speed in mhz */
	uint8_t			manufacturer;	/* manufacturer string */
	uint8_t			serial;		/* serial number string */
	uint8_t			asset;		/* asset tag string */
	uint8_t			part;		/* part number string */
	uint8_t			attributes;	/* attributes */
	uint32_t		xsize;		/* extended size in mbs */
	uint16_t		curspeed;	/* current speed in mhz */
	uint16_t		minvoltage;	/* minimum voltage */
	uint16_t		maxvoltage;	/* maximum voltage */
	uint16_t		curvoltage;	/* configured voltage */
} __packed;

/*
 * Memory Array Mapped Address
 */
struct smbios_table_type19 {
	struct smbios_structure	header;
	uint32_t		saddr;		/* start phys addr in kb */
	uint32_t		eaddr;		/* end phys addr in kb */
	uint16_t		arrayhand;	/* physical mem array handle */
	uint8_t			width;		/* num of dev in row */
	uint64_t		xsaddr;		/* start phys addr in bytes */
	uint64_t		xeaddr;		/* end phys addr in bytes */
} __packed;

/*
 * System Boot Information
 */
#define	SMBIOS_BOOT_NORMAL	0	/* no errors detected */

struct smbios_table_type32 {
	struct smbios_structure	header;
	uint8_t			reserved[6];
	uint8_t			status;		/* boot status */
} __packed;

/*
 * End-of-Table
 */
struct smbios_table_type127 {
	struct smbios_structure	header;
} __packed;

struct smbios_table_type0 smbios_type0_template = {
	{ SMBIOS_TYPE_BIOS, sizeof (struct smbios_table_type0), 0 },
	1,	/* bios vendor string */
	2,	/* bios version string */
	0xF000,	/* bios address segment location */
	3,	/* bios release date */
	0x0,	/* bios size (64k * (n + 1) is the size in bytes) */
	SMBIOS_FL_ISA | SMBIOS_FL_PCI | SMBIOS_FL_SHADOW |
	    SMBIOS_FL_CDBOOT | SMBIOS_FL_EDD,
	{ SMBIOS_XB1_FL_ACPI, SMBIOS_XB2_FL_BBS | SMBIOS_XB2_FL_VM },
	0x0,	/* bios major release */
	0x0,	/* bios minor release */
	0xff,	/* embedded controller firmware major release */
	0xff	/* embedded controller firmware minor release */
};

const char *smbios_type0_strings[] = {
	"BHYVE",	/* vendor string */
	"1.00",		/* bios version string */
	"03/14/2014",	/* bios release date string */
	NULL
};

struct smbios_table_type1 smbios_type1_template = {
	{ SMBIOS_TYPE_SYSTEM, sizeof (struct smbios_table_type1), 0 },
	1,		/* manufacturer string */
	2,		/* product string */
	3,		/* version string */
	4,		/* serial number string */
	{ 0 },
	SMBIOS_WAKEUP_SWITCH,
	5,		/* sku string */
	6		/* family string */
};

static int smbios_type1_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

const char *smbios_type1_strings[] = {
	" ",		/* manufacturer string */
	"BHYVE",	/* product name string */
	"1.0",		/* version string */
	"None",		/* serial number string */
	"None",		/* sku string */
	" ",		/* family name string */
	NULL
};

struct smbios_table_type3 smbios_type3_template = {
	{ SMBIOS_TYPE_CHASSIS, sizeof (struct smbios_table_type3), 0 },
	1,		/* manufacturer string */
	SMBIOS_CHT_UNKNOWN,
	2,		/* version string */
	3,		/* serial number string */
	4,		/* asset tag string */
	SMBIOS_CHST_SAFE,
	SMBIOS_CHST_SAFE,
	SMBIOS_CHST_SAFE,
	SMBIOS_CHSC_NONE,
	0,		/* height in 'u's (0=enclosure height unspecified) */
	0,		/* number of power cords (0=number unspecified) */
	0,		/* number of contained element records */
	0,		/* length of records */
	5		/* sku number string */
};

const char *smbios_type3_strings[] = {
	" ",		/* manufacturer string */
	"1.0",		/* version string */
	"None",		/* serial number string */
	"None",		/* asset tag string */
	"None",		/* sku number string */
	NULL
};

struct smbios_table_type4 smbios_type4_template = {
	{ SMBIOS_TYPE_PROCESSOR, sizeof (struct smbios_table_type4), 0 },
	1,		/* socket designation string */
	SMBIOS_PRT_CENTRAL,
	SMBIOS_PRF_OTHER,
	2,		/* manufacturer string */
	0,		/* cpuid */
	3,		/* version string */
	0,		/* voltage */
	0,		/* external clock frequency in mhz (0=unknown) */
	0,		/* maximum frequency in mhz (0=unknown) */
	0,		/* current frequency in mhz (0=unknown) */
	SMBIOS_PRS_PRESENT | SMBIOS_PRS_ENABLED,
	SMBIOS_PRU_NONE,
	-1,		/* l1 cache handle */
	-1,		/* l2 cache handle */
	-1,		/* l3 cache handle */
	4,		/* serial number string */
	5,		/* asset tag string */
	6,		/* part number string */
	0,		/* cores per socket (0=unknown) */
	0,		/* enabled cores per socket (0=unknown) */
	0,		/* threads per socket (0=unknown) */
	SMBIOS_PFL_64B,
	SMBIOS_PRF_OTHER
};

const char *smbios_type4_strings[] = {
	" ",		/* socket designation string */
	" ",		/* manufacturer string */
	" ",		/* version string */
	"None",		/* serial number string */
	"None",		/* asset tag string */
	"None",		/* part number string */
	NULL
};

static int smbios_type4_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

struct smbios_table_type16 smbios_type16_template = {
	{ SMBIOS_TYPE_MEMARRAY, sizeof (struct smbios_table_type16),  0 },
	SMBIOS_MAL_SYSMB,
	SMBIOS_MAU_SYSTEM,
	SMBIOS_MAE_NONE,
	0x80000000,	/* max mem capacity in kb (0x80000000=use extended) */
	-1,		/* handle of error (if any) */
	0,		/* number of slots or sockets (TBD) */
	0		/* extended maximum memory capacity in bytes (TBD) */
};

static int smbios_type16_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

struct smbios_table_type17 smbios_type17_template = {
	{ SMBIOS_TYPE_MEMDEVICE, sizeof (struct smbios_table_type17),  0 },
	-1,		/* handle of physical memory array */
	-1,		/* handle of memory error data */
	64,		/* total width in bits including ecc */
	64,		/* data width in bits */
	0x7fff,		/* size in bytes (0x7fff=use extended)*/
	SMBIOS_MDFF_UNKNOWN,
	0,		/* set (0x00=none, 0xff=unknown) */
	1,		/* device locator string */
	2,		/* physical bank locator string */
	SMBIOS_MDT_UNKNOWN,
	SMBIOS_MDF_UNKNOWN,
	0,		/* maximum memory speed in mhz (0=unknown) */
	3,		/* manufacturer string */
	4,		/* serial number string */
	5,		/* asset tag string */
	6,		/* part number string */
	0,		/* attributes (0=unknown rank information) */
	0,		/* extended size in mb (TBD) */
	0,		/* current speed in mhz (0=unknown) */
	0,		/* minimum voltage in mv (0=unknown) */
	0,		/* maximum voltage in mv (0=unknown) */
	0		/* configured voltage in mv (0=unknown) */
};

const char *smbios_type17_strings[] = {
	" ",		/* device locator string */
	" ",		/* physical bank locator string */
	" ",		/* manufacturer string */
	"None",		/* serial number string */
	"None",		/* asset tag string */
	"None",		/* part number string */
	NULL
};

static int smbios_type17_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

struct smbios_table_type19 smbios_type19_template = {
	{ SMBIOS_TYPE_MEMARRAYMAP, sizeof (struct smbios_table_type19),  0 },
	0xffffffff,	/* starting phys addr in kb (0xffffffff=use ext) */
	0xffffffff,	/* ending phys addr in kb (0xffffffff=use ext) */
	-1,		/* physical memory array handle */
	1,		/* number of devices that form a row */
	0,		/* extended starting phys addr in bytes (TDB) */
	0		/* extended ending phys addr in bytes (TDB) */
};

static int smbios_type19_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

struct smbios_table_type32 smbios_type32_template = {
	{ SMBIOS_TYPE_BOOT, sizeof (struct smbios_table_type32),  0 },
	{ 0, 0, 0, 0, 0, 0 },
	SMBIOS_BOOT_NORMAL
};

struct smbios_table_type127 smbios_type127_template = {
	{ SMBIOS_TYPE_EOT, sizeof (struct smbios_table_type127),  0 }
};

static int smbios_generic_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size);

static struct smbios_template_entry smbios_template[] = {
	{ (struct smbios_structure *)&smbios_type0_template,
	  smbios_type0_strings,
	  smbios_generic_initializer },
	{ (struct smbios_structure *)&smbios_type1_template,
	  smbios_type1_strings,
	  smbios_type1_initializer },
	{ (struct smbios_structure *)&smbios_type3_template,
	  smbios_type3_strings,
	  smbios_generic_initializer },
	{ (struct smbios_structure *)&smbios_type4_template,
	  smbios_type4_strings,
	  smbios_type4_initializer },
	{ (struct smbios_structure *)&smbios_type16_template,
	  NULL,
	  smbios_type16_initializer },
	{ (struct smbios_structure *)&smbios_type17_template,
	  smbios_type17_strings,
	  smbios_type17_initializer },
	{ (struct smbios_structure *)&smbios_type19_template,
	  NULL,
	  smbios_type19_initializer },
	{ (struct smbios_structure *)&smbios_type32_template,
	  NULL,
	  smbios_generic_initializer },
	{ (struct smbios_structure *)&smbios_type127_template,
	  NULL,
	  smbios_generic_initializer },
	{ NULL,NULL, NULL }
};

static uint64_t guest_lomem, guest_himem;
static uint16_t type16_handle;

static int
smbios_generic_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size)
{
	struct smbios_structure *entry;

	memcpy(curaddr, template_entry, template_entry->length);
	entry = (struct smbios_structure *)curaddr;
	entry->handle = *n + 1;
	curaddr += entry->length;
	if (template_strings != NULL) {
		int	i;

		for (i = 0; template_strings[i] != NULL; i++) {
			const char *string;
			int len;

			string = template_strings[i];
			len = strlen(string) + 1;
			memcpy(curaddr, string, len);
			curaddr += len;
		}
		*curaddr = '\0';
		curaddr++;
	} else {
		/* Minimum string section is double nul */
		*curaddr = '\0';
		curaddr++;
		*curaddr = '\0';
		curaddr++;
	}
	(*n)++;
	*endaddr = curaddr;

	return (0);
}

static int
smbios_type1_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size)
{
	struct smbios_table_type1 *type1;

	smbios_generic_initializer(template_entry, template_strings,
	    curaddr, endaddr, n, size);
	type1 = (struct smbios_table_type1 *)curaddr;

	if (guest_uuid_str != NULL) {
		uuid_t		uuid;
		uint32_t	status;

		uuid_from_string(guest_uuid_str, &uuid, &status);
		if (status != uuid_s_ok)
			return (-1);

		uuid_enc_le(&type1->uuid, &uuid);
	} else {
		MD5_CTX		mdctx;
		u_char		digest[16];
		char		hostname[MAXHOSTNAMELEN];

		/*
		 * Universally unique and yet reproducible are an
		 * oxymoron, however reproducible is desirable in
		 * this case.
		 */
		if (gethostname(hostname, sizeof(hostname)))
			return (-1);

		MD5Init(&mdctx);
		MD5Update(&mdctx, vmname, strlen(vmname));
		MD5Update(&mdctx, hostname, sizeof(hostname));
		MD5Final(digest, &mdctx);

		/*
		 * Set the variant and version number.
		 */
		digest[6] &= 0x0F;
		digest[6] |= 0x30;	/* version 3 */
		digest[8] &= 0x3F;
		digest[8] |= 0x80;

		memcpy(&type1->uuid, digest, sizeof (digest));
	}

	return (0);
}

static int
smbios_type4_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size)
{
	int i;

	for (i = 0; i < guest_ncpus; i++) {
		struct smbios_table_type4 *type4;
		char *p;
		int nstrings, len;

		smbios_generic_initializer(template_entry, template_strings,
		    curaddr, endaddr, n, size);
		type4 = (struct smbios_table_type4 *)curaddr;
		p = curaddr + sizeof (struct smbios_table_type4);
		nstrings = 0;
		while (p < *endaddr - 1) {
			if (*p++ == '\0')
				nstrings++;
		}
		len = sprintf(*endaddr - 1, "CPU #%d", i) + 1;
		*endaddr += len - 1;
		*(*endaddr) = '\0';
		(*endaddr)++;
		type4->socket = nstrings + 1;
		curaddr = *endaddr;
	}

	return (0);
}

static int
smbios_type16_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size)
{
	struct smbios_table_type16 *type16;

	type16_handle = *n;
	smbios_generic_initializer(template_entry, template_strings,
	    curaddr, endaddr, n, size);
	type16 = (struct smbios_table_type16 *)curaddr;
	type16->xsize = guest_lomem + guest_himem;
	type16->ndevs = guest_himem > 0 ? 2 : 1;

	return (0);
}

static int
smbios_type17_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size)
{
	struct smbios_table_type17 *type17;

	smbios_generic_initializer(template_entry, template_strings,
	    curaddr, endaddr, n, size);
	type17 = (struct smbios_table_type17 *)curaddr;
	type17->arrayhand = type16_handle;
	type17->xsize = guest_lomem;

	if (guest_himem > 0) {
		curaddr = *endaddr;
		smbios_generic_initializer(template_entry, template_strings,
		    curaddr, endaddr, n, size);
		type17 = (struct smbios_table_type17 *)curaddr;
		type17->arrayhand = type16_handle;
		type17->xsize = guest_himem;
	}

	return (0);
}

static int
smbios_type19_initializer(struct smbios_structure *template_entry,
    const char **template_strings, char *curaddr, char **endaddr,
    uint16_t *n, uint16_t *size)
{
	struct smbios_table_type19 *type19;

	smbios_generic_initializer(template_entry, template_strings,
	    curaddr, endaddr, n, size);
	type19 = (struct smbios_table_type19 *)curaddr;
	type19->arrayhand = type16_handle;
	type19->xsaddr = 0;
	type19->xeaddr = guest_lomem;

	if (guest_himem > 0) {
		curaddr = *endaddr;
		smbios_generic_initializer(template_entry, template_strings,
		    curaddr, endaddr, n, size);
		type19 = (struct smbios_table_type19 *)curaddr;
		type19->arrayhand = type16_handle;
		type19->xsaddr = 4*GB;
		type19->xeaddr = guest_himem;
	}

	return (0);
}

static void
smbios_ep_initializer(struct smbios_entry_point *smbios_ep, uint32_t staddr)
{
	memset(smbios_ep, 0, sizeof(*smbios_ep));
	memcpy(smbios_ep->eanchor, SMBIOS_ENTRY_EANCHOR,
	    SMBIOS_ENTRY_EANCHORLEN);
	smbios_ep->eplen = 0x1F;
	assert(sizeof (struct smbios_entry_point) == smbios_ep->eplen);
	smbios_ep->major = 2;
	smbios_ep->minor = 6;
	smbios_ep->revision = 0;
	memcpy(smbios_ep->ianchor, SMBIOS_ENTRY_IANCHOR,
	    SMBIOS_ENTRY_IANCHORLEN);
	smbios_ep->staddr = staddr;
	smbios_ep->bcdrev = 0x24;
}

static void
smbios_ep_finalizer(struct smbios_entry_point *smbios_ep, uint16_t len,
    uint16_t num, uint16_t maxssize)
{
	uint8_t	checksum;
	int	i;

	smbios_ep->maxssize = maxssize;
	smbios_ep->stlen = len;
	smbios_ep->stnum = num;

	checksum = 0;
	for (i = 0x10; i < 0x1f; i++) {
		checksum -= ((uint8_t *)smbios_ep)[i];
	}
	smbios_ep->ichecksum = checksum;

	checksum = 0;
	for (i = 0; i < 0x1f; i++) {
		checksum -= ((uint8_t *)smbios_ep)[i];
	}
	smbios_ep->echecksum = checksum;
}

int
smbios_build(struct vmctx *ctx)
{
	struct smbios_entry_point	*smbios_ep;
	uint16_t			n;
	uint16_t			maxssize;
	char				*curaddr, *startaddr, *ststartaddr;
	int				i;
	int				err;

	guest_lomem = vm_get_lowmem_size(ctx);
	guest_himem = vm_get_highmem_size(ctx);

	startaddr = paddr_guest2host(ctx, SMBIOS_BASE, SMBIOS_MAX_LENGTH);
	if (startaddr == NULL) {
		fprintf(stderr, "smbios table requires mapped mem\n");
		return (ENOMEM);
	}

	curaddr = startaddr;

	smbios_ep = (struct smbios_entry_point *)curaddr;
	smbios_ep_initializer(smbios_ep, SMBIOS_BASE +
	    sizeof(struct smbios_entry_point));
	curaddr += sizeof(struct smbios_entry_point);
	ststartaddr = curaddr;

	n = 0;
	maxssize = 0;
	for (i = 0; smbios_template[i].entry != NULL; i++) {
		struct smbios_structure	*entry;
		const char		**strings;
		initializer_func_t      initializer;
		char			*endaddr;
		uint16_t		size;

		entry = smbios_template[i].entry;
		strings = smbios_template[i].strings;
		initializer = smbios_template[i].initializer;

		err = (*initializer)(entry, strings, curaddr, &endaddr,
		    &n, &size);
		if (err != 0)
			return (err);

		if (size > maxssize)
			maxssize = size;

		curaddr = endaddr;
	}

	assert(curaddr - startaddr < SMBIOS_MAX_LENGTH);
	smbios_ep_finalizer(smbios_ep, curaddr - ststartaddr, n, maxssize);

	return (0);
}
