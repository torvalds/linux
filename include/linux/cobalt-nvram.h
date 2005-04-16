/*
 * $Id: cobalt-nvram.h,v 1.20 2001/10/17 23:16:55 thockin Exp $
 * cobalt-nvram.h : defines for the various fields in the cobalt NVRAM
 *
 * Copyright 2001,2002 Sun Microsystems, Inc.
 */

#ifndef COBALT_NVRAM_H
#define COBALT_NVRAM_H

#include <linux/nvram.h>

#define COBT_CMOS_INFO_MAX		0x7f	/* top address allowed */
#define COBT_CMOS_BIOS_DRIVE_INFO	0x12	/* drive info would go here */

#define COBT_CMOS_CKS_START		NVRAM_OFFSET(0x0e)
#define COBT_CMOS_CKS_END		NVRAM_OFFSET(0x7f)

/* flag bytes - 16 flags for now, leave room for more */
#define COBT_CMOS_FLAG_BYTE_0		NVRAM_OFFSET(0x10)
#define COBT_CMOS_FLAG_BYTE_1		NVRAM_OFFSET(0x11)

/* flags in flag bytes - up to 16 */
#define COBT_CMOS_FLAG_MIN		0x0001
#define COBT_CMOS_CONSOLE_FLAG		0x0001 /* console on/off */
#define COBT_CMOS_DEBUG_FLAG		0x0002 /* ROM debug messages */
#define COBT_CMOS_AUTO_PROMPT_FLAG	0x0004 /* boot to ROM prompt? */
#define COBT_CMOS_CLEAN_BOOT_FLAG	0x0008 /* set by a clean shutdown */
#define COBT_CMOS_HW_NOPROBE_FLAG	0x0010 /* go easy on the probing */
#define COBT_CMOS_SYSFAULT_FLAG		0x0020 /* system fault detected */
#define COBT_CMOS_OOPSPANIC_FLAG	0x0040 /* panic on oops */
#define COBT_CMOS_DELAY_CACHE_FLAG	0x0080 /* delay cache initialization */
#define COBT_CMOS_NOLOGO_FLAG		0x0100 /* hide "C" logo @ boot */
#define COBT_CMOS_VERSION_FLAG		0x0200 /* the version field is valid */
#define COBT_CMOS_FLAG_MAX		0x0200

/* leave byte 0x12 blank - Linux looks for drive info here */

/* CMOS structure version, valid if COBT_CMOS_VERSION_FLAG is true */
#define COBT_CMOS_VERSION		NVRAM_OFFSET(0x13)
#define COBT_CMOS_VER_BTOCODE		1 /* min. version needed for btocode */

/* index of default boot method */
#define COBT_CMOS_BOOT_METHOD		NVRAM_OFFSET(0x20)
#define COBT_CMOS_BOOT_METHOD_DISK	0
#define COBT_CMOS_BOOT_METHOD_ROM	1
#define COBT_CMOS_BOOT_METHOD_NET	2

#define COBT_CMOS_BOOT_DEV_MIN		NVRAM_OFFSET(0x21)
/* major #, minor # of first through fourth boot device */
#define COBT_CMOS_BOOT_DEV0_MAJ		NVRAM_OFFSET(0x21)
#define COBT_CMOS_BOOT_DEV0_MIN		NVRAM_OFFSET(0x22)
#define COBT_CMOS_BOOT_DEV1_MAJ		NVRAM_OFFSET(0x23)
#define COBT_CMOS_BOOT_DEV1_MIN		NVRAM_OFFSET(0x24)
#define COBT_CMOS_BOOT_DEV2_MAJ		NVRAM_OFFSET(0x25)
#define COBT_CMOS_BOOT_DEV2_MIN		NVRAM_OFFSET(0x26)
#define COBT_CMOS_BOOT_DEV3_MAJ		NVRAM_OFFSET(0x27)
#define COBT_CMOS_BOOT_DEV3_MIN		NVRAM_OFFSET(0x28)
#define COBT_CMOS_BOOT_DEV_MAX		NVRAM_OFFSET(0x28)

/* checksum of bytes 0xe-0x7f */
#define COBT_CMOS_CHECKSUM		NVRAM_OFFSET(0x2e)

/* running uptime counter, units of 5 minutes (32 bits =~ 41000 years) */
#define COBT_CMOS_UPTIME_0		NVRAM_OFFSET(0x30)
#define COBT_CMOS_UPTIME_1		NVRAM_OFFSET(0x31)
#define COBT_CMOS_UPTIME_2		NVRAM_OFFSET(0x32)
#define COBT_CMOS_UPTIME_3		NVRAM_OFFSET(0x33)

/* count of successful boots (32 bits) */
#define COBT_CMOS_BOOTCOUNT_0		NVRAM_OFFSET(0x38)
#define COBT_CMOS_BOOTCOUNT_1		NVRAM_OFFSET(0x39)
#define COBT_CMOS_BOOTCOUNT_2		NVRAM_OFFSET(0x3a)
#define COBT_CMOS_BOOTCOUNT_3		NVRAM_OFFSET(0x3b)

/* 13 bytes: system serial number, same as on the back of the system */
#define COBT_CMOS_SYS_SERNUM_LEN	13
#define COBT_CMOS_SYS_SERNUM_0		NVRAM_OFFSET(0x40)
#define COBT_CMOS_SYS_SERNUM_1		NVRAM_OFFSET(0x41)
#define COBT_CMOS_SYS_SERNUM_2		NVRAM_OFFSET(0x42)
#define COBT_CMOS_SYS_SERNUM_3		NVRAM_OFFSET(0x43)
#define COBT_CMOS_SYS_SERNUM_4		NVRAM_OFFSET(0x44)
#define COBT_CMOS_SYS_SERNUM_5		NVRAM_OFFSET(0x45)
#define COBT_CMOS_SYS_SERNUM_6		NVRAM_OFFSET(0x46)
#define COBT_CMOS_SYS_SERNUM_7		NVRAM_OFFSET(0x47)
#define COBT_CMOS_SYS_SERNUM_8		NVRAM_OFFSET(0x48)
#define COBT_CMOS_SYS_SERNUM_9		NVRAM_OFFSET(0x49)
#define COBT_CMOS_SYS_SERNUM_10		NVRAM_OFFSET(0x4a)
#define COBT_CMOS_SYS_SERNUM_11		NVRAM_OFFSET(0x4b)
#define COBT_CMOS_SYS_SERNUM_12		NVRAM_OFFSET(0x4c)
/* checksum for serial num - 1 byte */
#define COBT_CMOS_SYS_SERNUM_CSUM	NVRAM_OFFSET(0x4f)

#define COBT_CMOS_ROM_REV_MAJ		NVRAM_OFFSET(0x50)
#define COBT_CMOS_ROM_REV_MIN		NVRAM_OFFSET(0x51)
#define COBT_CMOS_ROM_REV_REV		NVRAM_OFFSET(0x52)

#define COBT_CMOS_BTO_CODE_0		NVRAM_OFFSET(0x53)
#define COBT_CMOS_BTO_CODE_1		NVRAM_OFFSET(0x54)
#define COBT_CMOS_BTO_CODE_2		NVRAM_OFFSET(0x55)
#define COBT_CMOS_BTO_CODE_3		NVRAM_OFFSET(0x56)

#define COBT_CMOS_BTO_IP_CSUM		NVRAM_OFFSET(0x57)
#define COBT_CMOS_BTO_IP_0		NVRAM_OFFSET(0x58)
#define COBT_CMOS_BTO_IP_1		NVRAM_OFFSET(0x59)
#define COBT_CMOS_BTO_IP_2		NVRAM_OFFSET(0x5a)
#define COBT_CMOS_BTO_IP_3		NVRAM_OFFSET(0x5b)

#endif /* COBALT_NVRAM_H */
