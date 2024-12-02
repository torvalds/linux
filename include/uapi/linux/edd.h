/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * linux/include/linux/edd.h
 *  Copyright (C) 2002, 2003, 2004 Dell Inc.
 *  by Matt Domsch <Matt_Domsch@dell.com>
 *
 * structures and definitions for the int 13h, ax={41,48}h
 * BIOS Enhanced Disk Drive Services
 * This is based on the T13 group document D1572 Revision 0 (August 14 2002)
 * available at http://www.t13.org/docs2002/d1572r0.pdf.  It is
 * very similar to D1484 Revision 3 http://www.t13.org/docs2002/d1484r3.pdf
 *
 * In a nutshell, arch/{i386,x86_64}/boot/setup.S populates a scratch
 * table in the boot_params that contains a list of BIOS-enumerated
 * boot devices.
 * In arch/{i386,x86_64}/kernel/setup.c, this information is
 * transferred into the edd structure, and in drivers/firmware/edd.c, that
 * information is used to identify BIOS boot disk.  The code in setup.S
 * is very sensitive to the size of these structures.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef _UAPI_LINUX_EDD_H
#define _UAPI_LINUX_EDD_H

#include <linux/types.h>

#define EDDNR 0x1e9		/* addr of number of edd_info structs at EDDBUF
				   in boot_params - treat this as 1 byte  */
#define EDDBUF	0xd00		/* addr of edd_info structs in boot_params */
#define EDDMAXNR 6		/* number of edd_info structs starting at EDDBUF  */
#define EDDEXTSIZE 8		/* change these if you muck with the structures */
#define EDDPARMSIZE 74
#define CHECKEXTENSIONSPRESENT 0x41
#define GETDEVICEPARAMETERS 0x48
#define LEGACYGETDEVICEPARAMETERS 0x08
#define EDDMAGIC1 0x55AA
#define EDDMAGIC2 0xAA55


#define READ_SECTORS 0x02         /* int13 AH=0x02 is READ_SECTORS command */
#define EDD_MBR_SIG_OFFSET 0x1B8  /* offset of signature in the MBR */
#define EDD_MBR_SIG_BUF    0x290  /* addr in boot params */
#define EDD_MBR_SIG_MAX 16        /* max number of signatures to store */
#define EDD_MBR_SIG_NR_BUF 0x1ea  /* addr of number of MBR signtaures at EDD_MBR_SIG_BUF
				     in boot_params - treat this as 1 byte  */

#ifndef __ASSEMBLY__

#define EDD_EXT_FIXED_DISK_ACCESS           (1 << 0)
#define EDD_EXT_DEVICE_LOCKING_AND_EJECTING (1 << 1)
#define EDD_EXT_ENHANCED_DISK_DRIVE_SUPPORT (1 << 2)
#define EDD_EXT_64BIT_EXTENSIONS            (1 << 3)

#define EDD_INFO_DMA_BOUNDARY_ERROR_TRANSPARENT (1 << 0)
#define EDD_INFO_GEOMETRY_VALID                (1 << 1)
#define EDD_INFO_REMOVABLE                     (1 << 2)
#define EDD_INFO_WRITE_VERIFY                  (1 << 3)
#define EDD_INFO_MEDIA_CHANGE_NOTIFICATION     (1 << 4)
#define EDD_INFO_LOCKABLE                      (1 << 5)
#define EDD_INFO_NO_MEDIA_PRESENT              (1 << 6)
#define EDD_INFO_USE_INT13_FN50                (1 << 7)

struct edd_device_params {
	__u16 length;
	__u16 info_flags;
	__u32 num_default_cylinders;
	__u32 num_default_heads;
	__u32 sectors_per_track;
	__u64 number_of_sectors;
	__u16 bytes_per_sector;
	__u32 dpte_ptr;		/* 0xFFFFFFFF for our purposes */
	__u16 key;		/* = 0xBEDD */
	__u8 device_path_info_length;	/* = 44 */
	__u8 reserved2;
	__u16 reserved3;
	__u8 host_bus_type[4];
	__u8 interface_type[8];
	union {
		struct {
			__u16 base_address;
			__u16 reserved1;
			__u32 reserved2;
		} __attribute__ ((packed)) isa;
		struct {
			__u8 bus;
			__u8 slot;
			__u8 function;
			__u8 channel;
			__u32 reserved;
		} __attribute__ ((packed)) pci;
		/* pcix is same as pci */
		struct {
			__u64 reserved;
		} __attribute__ ((packed)) ibnd;
		struct {
			__u64 reserved;
		} __attribute__ ((packed)) xprs;
		struct {
			__u64 reserved;
		} __attribute__ ((packed)) htpt;
		struct {
			__u64 reserved;
		} __attribute__ ((packed)) unknown;
	} interface_path;
	union {
		struct {
			__u8 device;
			__u8 reserved1;
			__u16 reserved2;
			__u32 reserved3;
			__u64 reserved4;
		} __attribute__ ((packed)) ata;
		struct {
			__u8 device;
			__u8 lun;
			__u8 reserved1;
			__u8 reserved2;
			__u32 reserved3;
			__u64 reserved4;
		} __attribute__ ((packed)) atapi;
		struct {
			__u16 id;
			__u64 lun;
			__u16 reserved1;
			__u32 reserved2;
		} __attribute__ ((packed)) scsi;
		struct {
			__u64 serial_number;
			__u64 reserved;
		} __attribute__ ((packed)) usb;
		struct {
			__u64 eui;
			__u64 reserved;
		} __attribute__ ((packed)) i1394;
		struct {
			__u64 wwid;
			__u64 lun;
		} __attribute__ ((packed)) fibre;
		struct {
			__u64 identity_tag;
			__u64 reserved;
		} __attribute__ ((packed)) i2o;
		struct {
			__u32 array_number;
			__u32 reserved1;
			__u64 reserved2;
		} __attribute__ ((packed)) raid;
		struct {
			__u8 device;
			__u8 reserved1;
			__u16 reserved2;
			__u32 reserved3;
			__u64 reserved4;
		} __attribute__ ((packed)) sata;
		struct {
			__u64 reserved1;
			__u64 reserved2;
		} __attribute__ ((packed)) unknown;
	} device_path;
	__u8 reserved4;
	__u8 checksum;
} __attribute__ ((packed));

struct edd_info {
	__u8 device;
	__u8 version;
	__u16 interface_support;
	__u16 legacy_max_cylinder;
	__u8 legacy_max_head;
	__u8 legacy_sectors_per_track;
	struct edd_device_params params;
} __attribute__ ((packed));

struct edd {
	unsigned int mbr_signature[EDD_MBR_SIG_MAX];
	struct edd_info edd_info[EDDMAXNR];
	unsigned char mbr_signature_nr;
	unsigned char edd_info_nr;
};

#endif				/*!__ASSEMBLY__ */

#endif /* _UAPI_LINUX_EDD_H */
