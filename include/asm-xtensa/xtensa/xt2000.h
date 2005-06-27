#ifndef _INC_XT2000_H_
#define _INC_XT2000_H_

/*
 * THIS FILE IS GENERATED -- DO NOT MODIFY BY HAND
 *
 * include/asm-xtensa/xtensa/xt2000.h - Definitions specific to the
 * Tensilica XT2000 Emulation Board
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002 Tensilica Inc.
 */


#include <xtensa/config/core.h>
#include <xtensa/config/system.h>


/*
 *  Default assignment of XT2000 devices to external interrupts.
 */

/*  Ethernet interrupt:  */
#ifdef XCHAL_EXTINT3_NUM
#define SONIC83934_INTNUM	XCHAL_EXTINT3_NUM
#define SONIC83934_INTLEVEL	XCHAL_EXTINT3_LEVEL
#define SONIC83934_INTMASK	XCHAL_EXTINT3_MASK
#else
#define SONIC83934_INTMASK	0
#endif

/*  DUART channel 1 interrupt (P1 - console):  */
#ifdef XCHAL_EXTINT4_NUM
#define DUART16552_1_INTNUM	XCHAL_EXTINT4_NUM
#define DUART16552_1_INTLEVEL	XCHAL_EXTINT4_LEVEL
#define DUART16552_1_INTMASK	XCHAL_EXTINT4_MASK
#else
#define DUART16552_1_INTMASK	0
#endif

/*  DUART channel 2 interrupt (P2 - 2nd serial port):  */
#ifdef XCHAL_EXTINT5_NUM
#define DUART16552_2_INTNUM	XCHAL_EXTINT5_NUM
#define DUART16552_2_INTLEVEL	XCHAL_EXTINT5_LEVEL
#define DUART16552_2_INTMASK	XCHAL_EXTINT5_MASK
#else
#define DUART16552_2_INTMASK	0
#endif

/*  FPGA-combined PCI/etc interrupts:  */
#ifdef XCHAL_EXTINT6_NUM
#define XT2000_FPGAPCI_INTNUM	XCHAL_EXTINT6_NUM
#define XT2000_FPGAPCI_INTLEVEL	XCHAL_EXTINT6_LEVEL
#define XT2000_FPGAPCI_INTMASK	XCHAL_EXTINT6_MASK
#else
#define XT2000_FPGAPCI_INTMASK	0
#endif



/*
 *  Device addresses.
 *
 *  Note:  for endianness-independence, use 32-bit loads and stores for all
 *  register accesses to Ethernet, DUART and LED devices.  Undefined bits
 *  may need to be masked out if needed when reading if the actual register
 *  size is smaller than 32 bits.
 *
 *  Note:  XT2000 bus byte lanes are defined in terms of msbyte and lsbyte
 *  relative to the processor.  So 32-bit registers are accessed consistently
 *  from both big and little endian processors.  However, this means byte
 *  sequences are not consistent between big and little endian processors.
 *  This is fine for RAM, and for ROM if ROM is created for a specific
 *  processor (and thus has correct byte sequences).  However this may be
 *  unexpected for Flash, which might contain a file-system that one wants
 *  to use for multiple processor configurations (eg. the Flash might contain
 *  the Ethernet card's address, endianness-independent application data, etc).
 *  That is, byte sequences written in Flash by a core of a given endianness
 *  will be byte-swapped when seen by a core of the other endianness.
 *  Someone implementing an endianness-independent Flash file system will
 *  likely handle this byte-swapping issue in the Flash driver software.
 */

#define DUART16552_XTAL_FREQ	18432000	/* crystal frequency in Hz */
#define XTBOARD_FLASH_MAXSIZE	0x4000000	/* 64 MB (max; depends on what is socketed!) */
#define XTBOARD_EPROM_MAXSIZE	0x0400000	/* 4 MB (max; depends on what is socketed!) */
#define XTBOARD_EEPROM_MAXSIZE	0x0080000	/* 512 kB (max; depends on what is socketed!) */
#define XTBOARD_ASRAM_SIZE	0x0100000	/* 1 MB */
#define XTBOARD_PCI_MEM_SIZE	0x8000000	/* 128 MB (allocated) */
#define XTBOARD_PCI_IO_SIZE	0x1000000	/* 16 MB (allocated) */

#ifdef XSHAL_IOBLOCK_BYPASS_PADDR
/*  PCI memory space:  */
# define XTBOARD_PCI_MEM_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0x0000000)
/*  Socketed Flash (eg. 2 x 16-bit devices):  */
# define XTBOARD_FLASH_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0x8000000)
/*  PCI I/O space:  */
# define XTBOARD_PCI_IO_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xC000000)
/*  V3 PCI interface chip register/config space:  */
# define XTBOARD_V3PCI_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD000000)
/*  Bus Interface registers:  */
# define XTBOARD_BUSINT_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD010000)
/*  FPGA registers:  */
# define XT2000_FPGAREGS_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD020000)
/*  SONIC SN83934 Ethernet controller/transceiver:  */
# define SONIC83934_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD030000)
/*  8-character bitmapped LED display:  */
# define XTBOARD_LED_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD040000)
/*  National-Semi PC16552D DUART:  */
# define DUART16552_1_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD050020)	/* channel 1 (P1 - console) */
# define DUART16552_2_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD050000)	/* channel 2 (P2) */
/*  Asynchronous Static RAM:  */
# define XTBOARD_ASRAM_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD400000)
/*  8-bit EEPROM:  */
# define XTBOARD_EEPROM_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD600000)
/*  2 x 16-bit EPROMs:  */
# define XTBOARD_EPROM_PADDR	(XSHAL_IOBLOCK_BYPASS_PADDR+0xD800000)
#endif /* XSHAL_IOBLOCK_BYPASS_PADDR */

/*  These devices might be accessed cached:  */
#ifdef XSHAL_IOBLOCK_CACHED_PADDR
# define XTBOARD_PCI_MEM_CACHED_PADDR	(XSHAL_IOBLOCK_CACHED_PADDR+0x0000000)
# define XTBOARD_FLASH_CACHED_PADDR	(XSHAL_IOBLOCK_CACHED_PADDR+0x8000000)
# define XTBOARD_ASRAM_CACHED_PADDR	(XSHAL_IOBLOCK_CACHED_PADDR+0xD400000)
# define XTBOARD_EEPROM_CACHED_PADDR	(XSHAL_IOBLOCK_CACHED_PADDR+0xD600000)
# define XTBOARD_EPROM_CACHED_PADDR	(XSHAL_IOBLOCK_CACHED_PADDR+0xD800000)
#endif /* XSHAL_IOBLOCK_CACHED_PADDR */


/***  Same thing over again, this time with virtual addresses:  ***/

#ifdef XSHAL_IOBLOCK_BYPASS_VADDR
/*  PCI memory space:  */
# define XTBOARD_PCI_MEM_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0x0000000)
/*  Socketed Flash (eg. 2 x 16-bit devices):  */
# define XTBOARD_FLASH_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0x8000000)
/*  PCI I/O space:  */
# define XTBOARD_PCI_IO_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xC000000)
/*  V3 PCI interface chip register/config space:  */
# define XTBOARD_V3PCI_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD000000)
/*  Bus Interface registers:  */
# define XTBOARD_BUSINT_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD010000)
/*  FPGA registers:  */
# define XT2000_FPGAREGS_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD020000)
/*  SONIC SN83934 Ethernet controller/transceiver:  */
# define SONIC83934_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD030000)
/*  8-character bitmapped LED display:  */
# define XTBOARD_LED_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD040000)
/*  National-Semi PC16552D DUART:  */
# define DUART16552_1_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD050020)	/* channel 1 (P1 - console) */
# define DUART16552_2_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD050000)	/* channel 2 (P2) */
/*  Asynchronous Static RAM:  */
# define XTBOARD_ASRAM_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD400000)
/*  8-bit EEPROM:  */
# define XTBOARD_EEPROM_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD600000)
/*  2 x 16-bit EPROMs:  */
# define XTBOARD_EPROM_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD800000)
#endif /* XSHAL_IOBLOCK_BYPASS_VADDR */

/*  These devices might be accessed cached:  */
#ifdef XSHAL_IOBLOCK_CACHED_VADDR
# define XTBOARD_PCI_MEM_CACHED_VADDR	(XSHAL_IOBLOCK_CACHED_VADDR+0x0000000)
# define XTBOARD_FLASH_CACHED_VADDR	(XSHAL_IOBLOCK_CACHED_VADDR+0x8000000)
# define XTBOARD_ASRAM_CACHED_VADDR	(XSHAL_IOBLOCK_CACHED_VADDR+0xD400000)
# define XTBOARD_EEPROM_CACHED_VADDR	(XSHAL_IOBLOCK_CACHED_VADDR+0xD600000)
# define XTBOARD_EPROM_CACHED_VADDR	(XSHAL_IOBLOCK_CACHED_VADDR+0xD800000)
#endif /* XSHAL_IOBLOCK_CACHED_VADDR */


/*  System ROM:  */
#define XTBOARD_ROM_SIZE		XSHAL_ROM_SIZE
#ifdef XSHAL_ROM_VADDR
#define XTBOARD_ROM_VADDR		XSHAL_ROM_VADDR
#endif
#ifdef XSHAL_ROM_PADDR
#define XTBOARD_ROM_PADDR		XSHAL_ROM_PADDR
#endif

/*  System RAM:  */
#define XTBOARD_RAM_SIZE		XSHAL_RAM_SIZE
#ifdef XSHAL_RAM_VADDR
#define XTBOARD_RAM_VADDR		XSHAL_RAM_VADDR
#endif
#ifdef XSHAL_RAM_PADDR
#define XTBOARD_RAM_PADDR		XSHAL_RAM_PADDR
#endif
#define XTBOARD_RAM_BYPASS_VADDR	XSHAL_RAM_BYPASS_VADDR
#define XTBOARD_RAM_BYPASS_PADDR	XSHAL_RAM_BYPASS_PADDR



/*
 *  Things that depend on device addresses.
 */


#define XTBOARD_CACHEATTR_WRITEBACK	XSHAL_XT2000_CACHEATTR_WRITEBACK
#define XTBOARD_CACHEATTR_WRITEALLOC	XSHAL_XT2000_CACHEATTR_WRITEALLOC
#define XTBOARD_CACHEATTR_WRITETHRU	XSHAL_XT2000_CACHEATTR_WRITETHRU
#define XTBOARD_CACHEATTR_BYPASS	XSHAL_XT2000_CACHEATTR_BYPASS
#define XTBOARD_CACHEATTR_DEFAULT	XSHAL_XT2000_CACHEATTR_DEFAULT

#define XTBOARD_BUSINT_PIPE_REGIONS	XSHAL_XT2000_PIPE_REGIONS
#define XTBOARD_BUSINT_SDRAM_REGIONS	XSHAL_XT2000_SDRAM_REGIONS



/*
 *  BusLogic (FPGA) registers.
 *  All these registers are normally accessed using 32-bit loads/stores.
 */

/*  Register offsets:  */
#define XT2000_DATECD_OFS	0x00	/* date code (read-only) */
#define XT2000_STSREG_OFS	0x04	/* status (read-only) */
#define XT2000_SYSLED_OFS	0x08	/* system LED */
#define XT2000_WRPROT_OFS	0x0C	/* write protect */
#define XT2000_SWRST_OFS	0x10	/* software reset */
#define XT2000_SYSRST_OFS	0x14	/* system (peripherals) reset */
#define XT2000_IMASK_OFS	0x18	/* interrupt mask */
#define XT2000_ISTAT_OFS	0x1C	/* interrupt status */
#define XT2000_V3CFG_OFS	0x20	/* V3 config (V320 PCI) */

/*  Physical register addresses:  */
#ifdef XT2000_FPGAREGS_PADDR
#define XT2000_DATECD_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_DATECD_OFS)
#define XT2000_STSREG_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_STSREG_OFS)
#define XT2000_SYSLED_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_SYSLED_OFS)
#define XT2000_WRPROT_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_WRPROT_OFS)
#define XT2000_SWRST_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_SWRST_OFS)
#define XT2000_SYSRST_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_SYSRST_OFS)
#define XT2000_IMASK_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_IMASK_OFS)
#define XT2000_ISTAT_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_ISTAT_OFS)
#define XT2000_V3CFG_PADDR	(XT2000_FPGAREGS_PADDR+XT2000_V3CFG_OFS)
#endif

/*  Virtual register addresses:  */
#ifdef XT2000_FPGAREGS_VADDR
#define XT2000_DATECD_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_DATECD_OFS)
#define XT2000_STSREG_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_STSREG_OFS)
#define XT2000_SYSLED_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_SYSLED_OFS)
#define XT2000_WRPROT_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_WRPROT_OFS)
#define XT2000_SWRST_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_SWRST_OFS)
#define XT2000_SYSRST_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_SYSRST_OFS)
#define XT2000_IMASK_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_IMASK_OFS)
#define XT2000_ISTAT_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_ISTAT_OFS)
#define XT2000_V3CFG_VADDR	(XT2000_FPGAREGS_VADDR+XT2000_V3CFG_OFS)
/*  Register access (for C code):  */
#define XT2000_DATECD_REG	(*(volatile unsigned*) XT2000_DATECD_VADDR)
#define XT2000_STSREG_REG	(*(volatile unsigned*) XT2000_STSREG_VADDR)
#define XT2000_SYSLED_REG	(*(volatile unsigned*) XT2000_SYSLED_VADDR)
#define XT2000_WRPROT_REG	(*(volatile unsigned*) XT2000_WRPROT_VADDR)
#define XT2000_SWRST_REG	(*(volatile unsigned*) XT2000_SWRST_VADDR)
#define XT2000_SYSRST_REG	(*(volatile unsigned*) XT2000_SYSRST_VADDR)
#define XT2000_IMASK_REG	(*(volatile unsigned*) XT2000_IMASK_VADDR)
#define XT2000_ISTAT_REG	(*(volatile unsigned*) XT2000_ISTAT_VADDR)
#define XT2000_V3CFG_REG	(*(volatile unsigned*) XT2000_V3CFG_VADDR)
#endif

/*  DATECD (date code) bit fields:  */

/*  BCD-coded month (01..12):  */
#define XT2000_DATECD_MONTH_SHIFT	24
#define XT2000_DATECD_MONTH_BITS	8
#define XT2000_DATECD_MONTH_MASK	0xFF000000
/*  BCD-coded day (01..31):  */
#define XT2000_DATECD_DAY_SHIFT		16
#define XT2000_DATECD_DAY_BITS		8
#define XT2000_DATECD_DAY_MASK		0x00FF0000
/*  BCD-coded year (2001..9999):  */
#define XT2000_DATECD_YEAR_SHIFT	0
#define XT2000_DATECD_YEAR_BITS		16
#define XT2000_DATECD_YEAR_MASK		0x0000FFFF

/*  STSREG (status) bit fields:  */

/*  Switch SW3 setting bit fields (0=off/up, 1=on/down):  */
#define XT2000_STSREG_SW3_SHIFT		0
#define XT2000_STSREG_SW3_BITS		4
#define XT2000_STSREG_SW3_MASK		0x0000000F
/*  Boot-select bits of switch SW3:  */
#define XT2000_STSREG_BOOTSEL_SHIFT	0
#define XT2000_STSREG_BOOTSEL_BITS	2
#define XT2000_STSREG_BOOTSEL_MASK	0x00000003
/*  Boot-select values:  */
#define XT2000_STSREG_BOOTSEL_FLASH	0
#define XT2000_STSREG_BOOTSEL_EPROM16	1
#define XT2000_STSREG_BOOTSEL_PROM8	2
#define XT2000_STSREG_BOOTSEL_ASRAM	3
/*  User-defined bits of switch SW3:  */
#define XT2000_STSREG_SW3_2_SHIFT	2
#define XT2000_STSREG_SW3_2_MASK	0x00000004
#define XT2000_STSREG_SW3_3_SHIFT	3
#define XT2000_STSREG_SW3_3_MASK	0x00000008

/*  SYSLED (system LED) bit fields:  */

/*  LED control bit (0=off, 1=on):  */
#define XT2000_SYSLED_LEDON_SHIFT	0
#define XT2000_SYSLED_LEDON_MASK	0x00000001

/*  WRPROT (write protect) bit fields (0=writable, 1=write-protected [default]):  */

/*  Flash write protect:  */
#define XT2000_WRPROT_FLWP_SHIFT	0
#define XT2000_WRPROT_FLWP_MASK		0x00000001
/*  Reserved but present write protect bits:  */
#define XT2000_WRPROT_WRP_SHIFT		1
#define XT2000_WRPROT_WRP_BITS		7
#define XT2000_WRPROT_WRP_MASK		0x000000FE

/*  SWRST (software reset; allows s/w to generate power-on equivalent reset):  */

/*  Software reset bits:  */
#define XT2000_SWRST_SWR_SHIFT		0
#define XT2000_SWRST_SWR_BITS		16
#define XT2000_SWRST_SWR_MASK		0x0000FFFF
/*  Software reset value -- writing this value resets the board:  */
#define XT2000_SWRST_RESETVALUE		0x0000DEAD

/*  SYSRST (system reset; controls reset of individual peripherals):  */

/*  All-device reset:  */
#define XT2000_SYSRST_ALL_SHIFT		0
#define XT2000_SYSRST_ALL_BITS		4
#define XT2000_SYSRST_ALL_MASK		0x0000000F
/*  HDSP-2534 LED display reset (1=reset, 0=nothing):  */
#define XT2000_SYSRST_LED_SHIFT		0
#define XT2000_SYSRST_LED_MASK		0x00000001
/*  Sonic DP83934 Ethernet controller reset (1=reset, 0=nothing):  */
#define XT2000_SYSRST_SONIC_SHIFT	1
#define XT2000_SYSRST_SONIC_MASK	0x00000002
/*  DP16552 DUART reset (1=reset, 0=nothing):  */
#define XT2000_SYSRST_DUART_SHIFT	2
#define XT2000_SYSRST_DUART_MASK	0x00000004
/*  V3 V320 PCI bridge controller reset (1=reset, 0=nothing):  */
#define XT2000_SYSRST_V3_SHIFT		3
#define XT2000_SYSRST_V3_MASK		0x00000008

/*  IMASK (interrupt mask; 0=disable, 1=enable):  */
/*  ISTAT (interrupt status; 0=inactive, 1=pending):  */

/*  PCI INTP interrupt:  */
#define XT2000_INTMUX_PCI_INTP_SHIFT	2
#define XT2000_INTMUX_PCI_INTP_MASK	0x00000004
/*  PCI INTS interrupt:  */
#define XT2000_INTMUX_PCI_INTS_SHIFT	3
#define XT2000_INTMUX_PCI_INTS_MASK	0x00000008
/*  PCI INTD interrupt:  */
#define XT2000_INTMUX_PCI_INTD_SHIFT	4
#define XT2000_INTMUX_PCI_INTD_MASK	0x00000010
/*  V320 PCI controller interrupt:  */
#define XT2000_INTMUX_V3_SHIFT		5
#define XT2000_INTMUX_V3_MASK		0x00000020
/*  PCI ENUM interrupt:  */
#define XT2000_INTMUX_PCI_ENUM_SHIFT	6
#define XT2000_INTMUX_PCI_ENUM_MASK	0x00000040
/*  PCI DEG interrupt:  */
#define XT2000_INTMUX_PCI_DEG_SHIFT	7
#define XT2000_INTMUX_PCI_DEG_MASK	0x00000080

/*  V3CFG (V3 config, V320 PCI controller):  */

/*  V3 address control (0=pass-thru, 1=V3 address bits 31:28 set to 4'b0001 [default]):  */
#define XT2000_V3CFG_V3ADC_SHIFT	0
#define XT2000_V3CFG_V3ADC_MASK		0x00000001

/* I2C Devices */

#define	XT2000_I2C_RTC_ID		0x68
#define	XT2000_I2C_NVRAM0_ID		0x56	/* 1st 256 byte block */
#define	XT2000_I2C_NVRAM1_ID		0x57	/* 2nd 256 byte block */

/*  NVRAM Board Info structure:  */

#define XT2000_NVRAM_SIZE		512

#define XT2000_NVRAM_BINFO_START	0x100
#define XT2000_NVRAM_BINFO_SIZE		0x20
#define XT2000_NVRAM_BINFO_VERSION	0x10	/* version 1.0 */
#if 0
#define XT2000_NVRAM_BINFO_VERSION_OFFSET	0x00
#define XT2000_NVRAM_BINFO_VERSION_SIZE			0x1
#define XT2000_NVRAM_BINFO_ETH_ADDR_OFFSET	0x02
#define XT2000_NVRAM_BINFO_ETH_ADDR_SIZE		0x6
#define XT2000_NVRAM_BINFO_SN_OFFSET		0x10
#define XT2000_NVRAM_BINFO_SN_SIZE			0xE
#define	XT2000_NVRAM_BINFO_CRC_OFFSET		0x1E
#define	XT2000_NVRAM_BINFO_CRC_SIZE			0x2
#endif /*0*/

#if !defined(__ASSEMBLY__) && !defined(_NOCLANGUAGE)
typedef struct xt2000_nvram_binfo {
    unsigned char	version;
    unsigned char	reserved1;
    unsigned char	eth_addr[6];
    unsigned char	reserved8[8];
    unsigned char	serialno[14];
    unsigned char	crc[2];		/* 16-bit CRC */
} xt2000_nvram_binfo;
#endif /*!__ASSEMBLY__ && !_NOCLANGUAGE*/


#endif /*_INC_XT2000_H_*/

