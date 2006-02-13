/*
 *  include/asm-s390/setup.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#ifndef _ASM_S390_SETUP_H
#define _ASM_S390_SETUP_H

#ifdef __KERNEL__

#include <asm/types.h>

#define PARMAREA		0x10400
#define COMMAND_LINE_SIZE 	896
#define RAMDISK_ORIGIN		0x800000
#define RAMDISK_SIZE		0x800000
#define MEMORY_CHUNKS		16	/* max 0x7fff */
#define IPL_PARMBLOCK_ORIGIN	0x2000

#ifndef __ASSEMBLY__

#ifndef __s390x__
#define IPL_DEVICE        (*(unsigned long *)  (0x10404))
#define INITRD_START      (*(unsigned long *)  (0x1040C))
#define INITRD_SIZE       (*(unsigned long *)  (0x10414))
#else /* __s390x__ */
#define IPL_DEVICE        (*(unsigned long *)  (0x10400))
#define INITRD_START      (*(unsigned long *)  (0x10408))
#define INITRD_SIZE       (*(unsigned long *)  (0x10410))
#endif /* __s390x__ */
#define COMMAND_LINE      ((char *)            (0x10480))

/*
 * Machine features detected in head.S
 */
extern unsigned long machine_flags;

#define MACHINE_IS_VM		(machine_flags & 1)
#define MACHINE_IS_P390		(machine_flags & 4)
#define MACHINE_HAS_MVPG	(machine_flags & 16)
#define MACHINE_HAS_DIAG44	(machine_flags & 32)
#define MACHINE_HAS_IDTE	(machine_flags & 128)

#ifndef __s390x__
#define MACHINE_HAS_IEEE	(machine_flags & 2)
#define MACHINE_HAS_CSP		(machine_flags & 8)
#else /* __s390x__ */
#define MACHINE_HAS_IEEE	(1)
#define MACHINE_HAS_CSP		(1)
#endif /* __s390x__ */


#define MACHINE_HAS_SCLP	(!MACHINE_IS_P390)

/*
 * Console mode. Override with conmode=
 */
extern unsigned int console_mode;
extern unsigned int console_devno;
extern unsigned int console_irq;

#define CONSOLE_IS_UNDEFINED	(console_mode == 0)
#define CONSOLE_IS_SCLP		(console_mode == 1)
#define CONSOLE_IS_3215		(console_mode == 2)
#define CONSOLE_IS_3270		(console_mode == 3)
#define SET_CONSOLE_SCLP	do { console_mode = 1; } while (0)
#define SET_CONSOLE_3215	do { console_mode = 2; } while (0)
#define SET_CONSOLE_3270	do { console_mode = 3; } while (0)

struct ipl_list_header {
	u32 length;
	u8  reserved[3];
	u8  version;
} __attribute__((packed));

struct ipl_block_fcp {
	u32 length;
	u8  pbt;
	u8  reserved1[322-1];
	u16 devno;
	u8  reserved2[4];
	u64 wwpn;
	u64 lun;
	u32 bootprog;
	u8  reserved3[12];
	u64 br_lba;
	u32 scp_data_len;
	u8  reserved4[260];
	u8  scp_data[];
} __attribute__((packed));

struct ipl_parameter_block {
	union {
		u32 length;
		struct ipl_list_header header;
	} hdr;
	struct ipl_block_fcp fcp;
} __attribute__((packed));

#define IPL_MAX_SUPPORTED_VERSION (0)

#define IPL_TYPE_FCP (0)

/*
 * IPL validity flags and parameters as detected in head.S
 */
extern u32 ipl_parameter_flags;
extern u16 ipl_devno;

#define IPL_DEVNO_VALID		(ipl_parameter_flags & 1)
#define IPL_PARMBLOCK_VALID	(ipl_parameter_flags & 2)

#define IPL_PARMBLOCK_START	((struct ipl_parameter_block *) \
				 IPL_PARMBLOCK_ORIGIN)
#define IPL_PARMBLOCK_SIZE	(IPL_PARMBLOCK_START->hdr.length)

#else /* __ASSEMBLY__ */

#ifndef __s390x__
#define IPL_DEVICE        0x10404
#define INITRD_START      0x1040C
#define INITRD_SIZE       0x10414
#else /* __s390x__ */
#define IPL_DEVICE        0x10400
#define INITRD_START      0x10408
#define INITRD_SIZE       0x10410
#endif /* __s390x__ */
#define COMMAND_LINE      0x10480

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_S390_SETUP_H */
