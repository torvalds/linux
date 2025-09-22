/*	$OpenBSD: smbiosvar.h,v 1.14 2025/07/15 01:09:32 jsg Exp $	*/
/*
 * Copyright (c) 2006 Gordon Willem Klok <gklok@cogeco.ca>
 * Copyright (c) 2005 Jordan Hargrave
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MACHINE_SMBIOSVAR_
#define _MACHINE_SMBIOSVAR_

#define SMBIOS_START			0xf0000
#define SMBIOS_END			0xfffff

#define SMBIOS_UUID_NPRESENT		0x1
#define SMBIOS_UUID_NSET		0x2

/*
 * Section 3.5 of "UUIDs and GUIDs" found at
 * http://www.opengroup.org/dce/info/draft-leach-uuids-guids-01.txt
 * specifies the string representation of a UUID.
 */
#define SMBIOS_UUID_REP "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define SMBIOS_UUID_REPLEN 37 /* 16 zero padded values, 4 hyphens, 1 null */

struct smbios_entry {
	uint8_t		mjr;
	uint8_t		min;
	uint8_t		*addr;
	uint16_t	len;
	uint16_t	count;
};

struct smbhdr {
	uint32_t	sig;		/* "_SM_" */
	uint8_t		checksum;	/* Entry point checksum */
	uint8_t		len;		/* Entry point structure length */
	uint8_t		majrev;		/* Specification major revision */
	uint8_t		minrev;		/* Specification minor revision */
	uint16_t	mss;		/* Maximum Structure Size */
	uint8_t		epr;		/* Entry Point Revision */
	uint8_t		fa[5];		/* value determined by EPR */
	uint8_t		sasig[5];	/* Secondary Anchor "_DMI_" */
	uint8_t		sachecksum;	/* Secondary Checksum */
	uint16_t	size;		/* Length of structure table in bytes */
	uint32_t	addr;		/* Structure table address */
	uint16_t	count;		/* Number of SMBIOS structures */
	uint8_t		rev;		/* BCD revision */
} __packed;

struct smb3hdr {
	uint8_t		sig[5];		/* "_SM3_" */
	uint8_t		checksum;	/* Entry point structure checksum */
	uint8_t		len;		/* Entry point length */
	uint8_t		majrev;		/* SMBIOS major version */
	uint8_t		minrev;		/* SMBIOS minor version */
	uint8_t		docrev;		/* SMBIOS docrev */
	uint8_t		epr;		/* Entry point revision */
	uint8_t		reserved;	/* Reserved */
	uint32_t	size;		/* Structure table maximum size */
	uint64_t	addr;		/* Structure table address */
} __packed;

struct smbtblhdr {
	uint8_t		type;
	uint8_t		size;
	uint16_t	handle;
} __packed;

struct smbtable {
	struct smbtblhdr *hdr;
	void		 *tblhdr;
	uint32_t	 cookie;
};

#define	SMBIOS_TYPE_BIOS		0
#define	SMBIOS_TYPE_SYSTEM		1
#define	SMBIOS_TYPE_BASEBOARD		2
#define	SMBIOS_TYPE_ENCLOSURE		3
#define	SMBIOS_TYPE_PROCESSOR		4
#define	SMBIOS_TYPE_MEMCTRL		5
#define	SMBIOS_TYPE_MEMMOD		6
#define	SMBIOS_TYPE_CACHE		7
#define	SMBIOS_TYPE_PORT		8
#define	SMBIOS_TYPE_SLOTS		9
#define	SMBIOS_TYPE_OBD			10
#define	SMBIOS_TYPE_OEM			11
#define	SMBIOS_TYPE_SYSCONFOPT		12
#define	SMBIOS_TYPE_BIOSLANG		13
#define	SMBIOS_TYPE_GROUPASSOC		14
#define	SMBIOS_TYPE_SYSEVENTLOG		15
#define	SMBIOS_TYPE_PHYMEM		16
#define	SMBIOS_TYPE_MEMDEV		17
#define	SMBIOS_TYPE_ECCINFO32		18
#define	SMBIOS_TYPE_MEMMAPARRAYADDR	19
#define	SMBIOS_TYPE_MEMMAPDEVADDR	20
#define	SMBIOS_TYPE_INBUILTPOINT	21
#define	SMBIOS_TYPE_PORTBATT		22
#define	SMBIOS_TYPE_SYSRESET		23
#define	SMBIOS_TYPE_HWSECUIRTY		24
#define	SMBIOS_TYPE_PWRCTRL		25
#define	SMBIOS_TYPE_VOLTPROBE		26
#define	SMBIOS_TYPE_COOLING		27
#define	SMBIOS_TYPE_TEMPPROBE		28
#define	SMBIOS_TYPE_CURRENTPROBE	29
#define	SMBIOS_TYPE_OOB_REMOTEACCESS	30
#define	SMBIOS_TYPE_BIS			31
#define	SMBIOS_TYPE_SBI			32
#define	SMBIOS_TYPE_ECCINFO64		33
#define	SMBIOS_TYPE_MGMTDEV		34
#define	SMBIOS_TYPE_MGTDEVCOMP		35
#define	SMBIOS_TYPE_MGTDEVTHRESH	36
#define	SMBIOS_TYPE_MEMCHANNEL		37
#define	SMBIOS_TYPE_IPMIDEV		38
#define	SMBIOS_TYPE_SPS			39
#define	SMBIOS_TYPE_INACTIVE		126
#define	SMBIOS_TYPE_EOT			127

/*
 * SMBIOS Structure Type 0 "BIOS Information"
 * DMTF Specification DSP0134 Section: 3.3.1 p.g. 34
 */
struct smbios_struct_bios {
	uint8_t		vendor;		/* string */
	uint8_t		version;	/* string */
	uint16_t	startaddr;
	uint8_t		release;	/* string */
	uint8_t		romsize;
	uint64_t	characteristics;
	uint32_t	charext;
	uint8_t		major_rel;
	uint8_t		minor_rel;
	uint8_t		ecf_mjr_rel;	/* embedded controller firmware */
	uint8_t		ecf_min_rel;	/* embedded controller firmware */
} __packed;

/*
 * SMBIOS Structure Type 1 "System Information"
 * DMTF Specification DSP0134 Section 3.3.2 p.g. 35
 */

struct smbios_sys {
/* SMBIOS spec 2.0+ */
	uint8_t		vendor;		/* string */
	uint8_t		product;	/* string */
	uint8_t		version;	/* string */
	uint8_t		serial;		/* string */
/* SMBIOS spec 2.1+ */
	uint8_t		uuid[16];
	uint8_t		wakeup;
/* SMBIOS spec 2.4+ */
	uint8_t		sku;		/* string */
	uint8_t		family;		/* string */
} __packed;

/*
 * SMBIOS Structure Type 2 "Base Board (Module) Information"
 * DMTF Specification DSP0134 Section 3.3.3 p.g. 37
 */
struct smbios_board {
	uint8_t		vendor;		/* string */
	uint8_t		product;	/* string */
	uint8_t		version;	/* string */
	uint8_t		serial;		/* string */
	uint8_t		asset;		/* string */
	uint8_t		feature;	/* feature flags */
	uint8_t		location;	/* location in chassis */
	uint16_t	handle;		/* chassis handle */
	uint8_t		type;		/* board type */
	uint8_t		noc;		/* number of contained objects */
} __packed;

/*
 * SMBIOS Structure Type 3 "System Enclosure or Chassis"
 * DMTF Specification DSP0134
 */
struct smbios_enclosure {
	/* SMBIOS spec  2.0+ */
	uint8_t		vendor;		/* string */
	uint8_t		type;
	uint8_t		version;	/* string */
	uint8_t		serial;		/* string */
	uint8_t		asset_tag;	/* string */
	/* SMBIOS spec  2.1+ */
	uint8_t		boot_state;
	uint8_t		psu_state;
	uint8_t		thermal_state;
	uint8_t		security_status;
	/* SMBIOS spec 2.3+ */
	uint16_t	oem_defined;
	uint8_t		height;
	uint8_t		no_power_cords;
	uint8_t		no_contained_element;
	uint8_t		reclen_contained_element;
	uint8_t		contained_elements;
	/* SMBIOS spec 2.7+ */
	uint8_t		sku;		/* string */
} __packed;

/*
 * SMBIOS Structure Type 4 "processor Information"
 * DMTF Specification DSP0134 v2.5 Section 3.3.5 p.g. 24
 */
struct smbios_cpu {
	uint8_t		cpu_socket_designation;	/* string */
	uint8_t		cpu_type;
	uint8_t		cpu_family;
	uint8_t		cpu_mfg;		/* string */
	uint32_t	cpu_id_eax;
	uint32_t	cpu_id_edx;
	uint8_t		cpu_version;		/* string */
	uint8_t		cpu_voltage;
	uint16_t	cpu_clock;
	uint16_t	cpu_max_speed;
	uint16_t	cpu_current_speed;
	uint8_t		cpu_status;
#define SMBIOS_CPUST_POPULATED			(1<<6)
#define SMBIOS_CPUST_STATUSMASK			(0x07)
	uint8_t		cpu_upgrade;
	uint16_t	cpu_l1_handle;
	uint16_t	cpu_l2_handle;
	uint16_t	cpu_l3_handle;
	uint8_t		cpu_serial;		/* string */
	uint8_t		cpu_asset_tag;		/* string */
	uint8_t		cpu_part_nr;		/* string */
	/* following fields were added in smbios 2.5 */
	uint8_t		cpu_core_count;
	uint8_t		cpu_core_enabled;
	uint8_t		cpu_thread_count;
	uint16_t	cpu_characteristics;
} __packed;

/*
 * SMBIOS Structure Type 38 "IPMI Information"
 * DMTF Specification DSP0134 Section 3.3.39 p.g. 91
 */
struct smbios_ipmi {
	uint8_t		smipmi_if_type;		/* IPMI Interface Type */
	uint8_t		smipmi_if_rev;		/* BCD IPMI Revision */
	uint8_t		smipmi_i2c_address;	/* I2C address of BMC */
	uint8_t		smipmi_nvram_address;	/* I2C address of NVRAM
						 * storage */
	uint64_t	smipmi_base_address;	/* Base address of BMC (BAR
						 * format */
	uint8_t		smipmi_base_flags;	/* Flags field:
						 * bit 7:6 : register spacing
						 *   00 = byte
						 *   01 = dword
						 *   02 = word
						 * bit 4 : Lower bit BAR
						 * bit 3 : IRQ valid
						 * bit 2 : N/A
						 * bit 1 : Interrupt polarity
						 * bit 0 : Interrupt trigger */
	uint8_t		smipmi_irq;		/* IRQ if applicable */
} __packed;

int smbios_find_table(uint8_t, struct smbtable *);
char *smbios_get_string(struct smbtable *, uint8_t, char *, size_t);

#endif
