/***********************license start***************
 * Copyright (c) 2003-2012  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/






/**
 * @file
 * Header file for simple executive application initialization.  This defines
 * part of the ABI between the bootloader and the application.
 * <hr>$Revision: 70327 $<hr>
 *
 */

#ifndef __CVMX_APP_INIT_H__
#define __CVMX_APP_INIT_H__

#ifdef	__cplusplus
extern "C" {
#endif


/* Current major and minor versions of the CVMX bootinfo block that is passed
** from the bootloader to the application.  This is versioned so that applications
** can properly handle multiple bootloader versions. */
#define CVMX_BOOTINFO_MAJ_VER 1
#define CVMX_BOOTINFO_MIN_VER 3


#if (CVMX_BOOTINFO_MAJ_VER == 1)
#define CVMX_BOOTINFO_OCTEON_SERIAL_LEN 20
/* This structure is populated by the bootloader.  For binary
** compatibility the only changes that should be made are
** adding members to the end of the structure, and the minor
** version should be incremented at that time.
** If an incompatible change is made, the major version
** must be incremented, and the minor version should be reset
** to 0.
*/
struct cvmx_bootinfo {
#ifdef __BIG_ENDIAN_BITFIELD
    uint32_t major_version;
    uint32_t minor_version;

    uint64_t stack_top;
    uint64_t heap_base;
    uint64_t heap_end;
    uint64_t desc_vaddr;

    uint32_t exception_base_addr;
    uint32_t stack_size;
    uint32_t flags;
    uint32_t core_mask;
    uint32_t dram_size;  /**< DRAM size in megabytes */
    uint32_t phy_mem_desc_addr;  /**< physical address of free memory descriptor block*/
    uint32_t debugger_flags_base_addr;  /**< used to pass flags from app to debugger */
    uint32_t eclock_hz;  /**< CPU clock speed, in hz */
    uint32_t dclock_hz;  /**< DRAM clock speed, in hz */
    uint32_t reserved0;
    uint16_t board_type;
    uint8_t board_rev_major;
    uint8_t board_rev_minor;
    uint16_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    char board_serial_number[CVMX_BOOTINFO_OCTEON_SERIAL_LEN];
    uint8_t mac_addr_base[6];
    uint8_t mac_addr_count;
#if (CVMX_BOOTINFO_MIN_VER >= 1)
    /* Several boards support compact flash on the Octeon boot bus.  The CF
    ** memory spaces may be mapped to different addresses on different boards.
    ** These are the physical addresses, so care must be taken to use the correct
    ** XKPHYS/KSEG0 addressing depending on the application's ABI.
    ** These values will be 0 if CF is not present */
    uint64_t compact_flash_common_base_addr;
    uint64_t compact_flash_attribute_base_addr;
    /* Base address of the LED display (as on EBT3000 board)
    ** This will be 0 if LED display not present. */
    uint64_t led_display_base_addr;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 2)
    uint32_t dfa_ref_clock_hz;  /**< DFA reference clock in hz (if applicable)*/
    uint32_t config_flags;  /**< flags indicating various configuration options.  These flags supercede
                            ** the 'flags' variable and should be used instead if available */
#if defined(OCTEON_VENDOR_GEFES)
    uint32_t dfm_size; /**< DFA Size */
#endif
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 3)
    uint64_t fdt_addr;  /**< Address of the OF Flattened Device Tree structure describing the board. */
#endif
#else /* __BIG_ENDIAN */
	/*
	 * Little-Endian: When the CPU mode is switched to
	 * little-endian, the view of the structure has some of the
	 * fields swapped.
	 */
	uint32_t minor_version;
	uint32_t major_version;

	uint64_t stack_top;
	uint64_t heap_base;
	uint64_t heap_end;
	uint64_t desc_vaddr;

	uint32_t stack_size;
	uint32_t exception_base_addr;

	uint32_t core_mask;
	uint32_t flags;

	uint32_t phy_mem_desc_addr;
	uint32_t dram_size;

	uint32_t eclock_hz;
	uint32_t debugger_flags_base_addr;

	uint32_t reserved0;
	uint32_t dclock_hz;

	uint8_t reserved3;
	uint8_t reserved2;
	uint16_t reserved1;
	uint8_t board_rev_minor;
	uint8_t board_rev_major;
	uint16_t board_type;

	union cvmx_bootinfo_scramble {
		/* Must byteswap these four words so that...*/
		uint64_t s[4];
		/* ... this strucure has the proper data arrangement. */
		struct {
			char board_serial_number[CVMX_BOOTINFO_OCTEON_SERIAL_LEN];
			uint8_t mac_addr_base[6];
			uint8_t mac_addr_count;
			uint8_t pad[5];
		} le;
	} scramble1;

#if (CVMX_BOOTINFO_MIN_VER >= 1)
	uint64_t compact_flash_common_base_addr;
	uint64_t compact_flash_attribute_base_addr;
	uint64_t led_display_base_addr;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 2)
	uint32_t config_flags;
	uint32_t dfa_ref_clock_hz;
#endif
#if (CVMX_BOOTINFO_MIN_VER >= 3)
	uint64_t fdt_addr;
#endif
#endif
};

typedef struct cvmx_bootinfo cvmx_bootinfo_t;

#define CVMX_BOOTINFO_CFG_FLAG_PCI_HOST			(1ull << 0)
#define CVMX_BOOTINFO_CFG_FLAG_PCI_TARGET		(1ull << 1)
#define CVMX_BOOTINFO_CFG_FLAG_DEBUG			(1ull << 2)
#define CVMX_BOOTINFO_CFG_FLAG_NO_MAGIC			(1ull << 3)
/* This flag is set if the TLB mappings are not contained in the
** 0x10000000 - 0x20000000 boot bus region. */
#define CVMX_BOOTINFO_CFG_FLAG_OVERSIZE_TLB_MAPPING     (1ull << 4)
#define CVMX_BOOTINFO_CFG_FLAG_BREAK			(1ull << 5)

#endif /*   (CVMX_BOOTINFO_MAJ_VER == 1) */


/* Type defines for board and chip types */
enum cvmx_board_types_enum {
    CVMX_BOARD_TYPE_NULL           =  0,
    CVMX_BOARD_TYPE_SIM            =  1,
    CVMX_BOARD_TYPE_EBT3000        =  2,
    CVMX_BOARD_TYPE_KODAMA         =  3,
    CVMX_BOARD_TYPE_NIAGARA        =  4,	/* Obsolete, no longer supported */
    CVMX_BOARD_TYPE_NAC38          =  5,	/* Obsolete, no longer supported */
    CVMX_BOARD_TYPE_THUNDER        =  6,
    CVMX_BOARD_TYPE_TRANTOR        =  7,	/* Obsolete, no longer supported */
    CVMX_BOARD_TYPE_EBH3000        =  8,
    CVMX_BOARD_TYPE_EBH3100        =  9,
    CVMX_BOARD_TYPE_HIKARI         = 10,
    CVMX_BOARD_TYPE_CN3010_EVB_HS5 = 11,
    CVMX_BOARD_TYPE_CN3005_EVB_HS5 = 12,
#if defined(OCTEON_VENDOR_GEFES)
    CVMX_BOARD_TYPE_TNPA3804       = 13,
    CVMX_BOARD_TYPE_AT5810         = 14,
    CVMX_BOARD_TYPE_WNPA3850       = 15,
    CVMX_BOARD_TYPE_W3860          = 16,
#else
    CVMX_BOARD_TYPE_KBP            = 13,
    CVMX_BOARD_TYPE_CN3020_EVB_HS5 = 14,  /* Deprecated, CVMX_BOARD_TYPE_CN3010_EVB_HS5 supports the CN3020 */
    CVMX_BOARD_TYPE_EBT5800        = 15,
    CVMX_BOARD_TYPE_NICPRO2        = 16,
#endif
    CVMX_BOARD_TYPE_EBH5600        = 17,
    CVMX_BOARD_TYPE_EBH5601        = 18,
    CVMX_BOARD_TYPE_EBH5200        = 19,
    CVMX_BOARD_TYPE_BBGW_REF       = 20,
    CVMX_BOARD_TYPE_NIC_XLE_4G     = 21,
    CVMX_BOARD_TYPE_EBT5600        = 22,
    CVMX_BOARD_TYPE_EBH5201        = 23,
    CVMX_BOARD_TYPE_EBT5200        = 24,
    CVMX_BOARD_TYPE_CB5600         = 25,
    CVMX_BOARD_TYPE_CB5601         = 26,
    CVMX_BOARD_TYPE_CB5200         = 27,
    CVMX_BOARD_TYPE_GENERIC        = 28,   /* Special 'generic' board type, supports many boards */
    CVMX_BOARD_TYPE_EBH5610        = 29,
    CVMX_BOARD_TYPE_LANAI2_A       = 30,
    CVMX_BOARD_TYPE_LANAI2_U       = 31,
    CVMX_BOARD_TYPE_EBB5600        = 32,
    CVMX_BOARD_TYPE_EBB6300        = 33,
    CVMX_BOARD_TYPE_NIC_XLE_10G    = 34,
    CVMX_BOARD_TYPE_LANAI2_G       = 35,
    CVMX_BOARD_TYPE_EBT5810        = 36,
    CVMX_BOARD_TYPE_NIC10E         = 37,
    CVMX_BOARD_TYPE_EP6300C        = 38,
    CVMX_BOARD_TYPE_EBB6800        = 39,
    CVMX_BOARD_TYPE_NIC4E          = 40,
    CVMX_BOARD_TYPE_NIC2E          = 41,
    CVMX_BOARD_TYPE_EBB6600        = 42,
    CVMX_BOARD_TYPE_REDWING        = 43,
    CVMX_BOARD_TYPE_NIC68_4        = 44,
    CVMX_BOARD_TYPE_NIC10E_66      = 45,
    CVMX_BOARD_TYPE_EBB6100        = 46,
    CVMX_BOARD_TYPE_EVB7100        = 47,
    CVMX_BOARD_TYPE_MAX,
    /* NOTE:  256-257 are being used by a customer. */

    /* The range from CVMX_BOARD_TYPE_MAX to CVMX_BOARD_TYPE_CUST_DEFINED_MIN is reserved
    ** for future SDK use. */

    /* Set aside a range for customer boards.  These numbers are managed
    ** by Cavium.
    */
    CVMX_BOARD_TYPE_CUST_DEFINED_MIN = 10000,
    CVMX_BOARD_TYPE_CUST_WSX16       = 10001,
    CVMX_BOARD_TYPE_CUST_NS0216      = 10002,
    CVMX_BOARD_TYPE_CUST_NB5         = 10003,
    CVMX_BOARD_TYPE_CUST_WMR500      = 10004,
    CVMX_BOARD_TYPE_CUST_ITB101      = 10005,
    CVMX_BOARD_TYPE_CUST_NTE102      = 10006,
    CVMX_BOARD_TYPE_CUST_AGS103      = 10007,
#if !defined(OCTEON_VENDOR_LANNER)
    CVMX_BOARD_TYPE_CUST_GST104      = 10008,
#else
    CVMX_BOARD_TYPE_CUST_LANNER_MR955= 10008,
#endif
    CVMX_BOARD_TYPE_CUST_GCT105      = 10009,
    CVMX_BOARD_TYPE_CUST_AGS106      = 10010,
    CVMX_BOARD_TYPE_CUST_SGM107      = 10011,
    CVMX_BOARD_TYPE_CUST_GCT108      = 10012,
    CVMX_BOARD_TYPE_CUST_AGS109      = 10013,
    CVMX_BOARD_TYPE_CUST_GCT110      = 10014,
    CVMX_BOARD_TYPE_CUST_L2_AIR_SENDER  = 10015,
    CVMX_BOARD_TYPE_CUST_L2_AIR_RECEIVER= 10016,
    CVMX_BOARD_TYPE_CUST_L2_ACCTON2_TX  = 10017,
    CVMX_BOARD_TYPE_CUST_L2_ACCTON2_RX  = 10018,
    CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_TX= 10019,
    CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_RX= 10020,
#if defined(OCTEON_VENDOR_LANNER)
    CVMX_BOARD_TYPE_CUST_LANNER_MR730	= 10021,
#else
    CVMX_BOARD_TYPE_CUST_L2_ZINWELL     = 10021,
#endif
    CVMX_BOARD_TYPE_CUST_DEFINED_MAX = 20000,

    /* Set aside a range for customer private use.  The SDK won't
    ** use any numbers in this range. */
    CVMX_BOARD_TYPE_CUST_PRIVATE_MIN = 20001,
#if defined(OCTEON_VENDOR_LANNER)
    CVMX_BOARD_TYPE_CUST_LANNER_MR320= 20002,
    CVMX_BOARD_TYPE_CUST_LANNER_MR321X=20007,
#endif
#if defined(OCTEON_VENDOR_UBIQUITI)
    CVMX_BOARD_TYPE_CUST_UBIQUITI_E100=20002,
    CVMX_BOARD_TYPE_CUST_UBIQUITI_E120= 20004,
#endif
#if defined(OCTEON_VENDOR_RADISYS)
    CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE=20002,
#endif
#if defined(OCTEON_VENDOR_GEFES)
    CVMX_BOARD_TYPE_CUST_TNPA5804       = 20005,
    CVMX_BOARD_TYPE_CUST_W5434          = 20006,
    CVMX_BOARD_TYPE_CUST_W5650          = 20007,
    CVMX_BOARD_TYPE_CUST_W5800          = 20008,
    CVMX_BOARD_TYPE_CUST_W5651X         = 20009,
    CVMX_BOARD_TYPE_CUST_TNPA5651X      = 20010,
    CVMX_BOARD_TYPE_CUST_TNPA56X4       = 20011,
    CVMX_BOARD_TYPE_CUST_W63XX          = 20013,
#endif
    CVMX_BOARD_TYPE_CUST_PRIVATE_MAX = 30000,


    /* Range for IO modules */
    CVMX_BOARD_TYPE_MODULE_MIN          = 30001,
    CVMX_BOARD_TYPE_MODULE_PCIE_RC_4X   = 30002,
    CVMX_BOARD_TYPE_MODULE_PCIE_EP_4X   = 30003,
    CVMX_BOARD_TYPE_MODULE_SGMII_MARVEL = 30004,
    CVMX_BOARD_TYPE_MODULE_SFPPLUS_BCM  = 30005,
    CVMX_BOARD_TYPE_MODULE_SRIO         = 30006,
    CVMX_BOARD_TYPE_MODULE_EBB5600_QLM0 = 30007,
    CVMX_BOARD_TYPE_MODULE_EBB5600_QLM1 = 30008,
    CVMX_BOARD_TYPE_MODULE_EBB5600_QLM2 = 30009,
    CVMX_BOARD_TYPE_MODULE_EBB5600_QLM3 = 30010,
    CVMX_BOARD_TYPE_MODULE_MAX          = 31000

    /* The remaining range is reserved for future use. */
};
enum cvmx_chip_types_enum {
    CVMX_CHIP_TYPE_NULL = 0,
    CVMX_CHIP_SIM_TYPE_DEPRECATED = 1,
    CVMX_CHIP_TYPE_OCTEON_SAMPLE = 2,
    CVMX_CHIP_TYPE_MAX
};

/* Compatability alias for NAC38 name change, planned to be removed from SDK 1.7 */
#define CVMX_BOARD_TYPE_NAO38	CVMX_BOARD_TYPE_NAC38

/* Functions to return string based on type */
#define ENUM_BRD_TYPE_CASE(x)   case x: return(#x + 16);   /* Skip CVMX_BOARD_TYPE_ */
static inline const char *cvmx_board_type_to_string(enum cvmx_board_types_enum type)
{
    switch (type)
    {
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NULL)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_SIM)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT3000)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_KODAMA)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIAGARA)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NAC38)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_THUNDER)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_TRANTOR)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH3000)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH3100)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_HIKARI)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CN3010_EVB_HS5)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CN3005_EVB_HS5)
#if defined(OCTEON_VENDOR_GEFES)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_TNPA3804)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_AT5810)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_WNPA3850)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_W3860)
#else
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_KBP)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CN3020_EVB_HS5)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5800)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NICPRO2)
#endif
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5600)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5601)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5200)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_BBGW_REF)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC_XLE_4G)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5600)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5201)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5200)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CB5600)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CB5601)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CB5200)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_GENERIC)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBH5610)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_LANAI2_A)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_LANAI2_U)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB5600)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6300)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC_XLE_10G)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_LANAI2_G)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBT5810)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC10E)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EP6300C)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6800)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC4E)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC2E)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6600)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_REDWING)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC68_4)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_NIC10E_66)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EBB6100)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_EVB7100)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MAX)

        /* Customer boards listed here */
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_DEFINED_MIN)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_WSX16)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_NS0216)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_NB5)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_WMR500)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_ITB101)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_NTE102)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_AGS103)
#if !defined(OCTEON_VENDOR_LANNER)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GST104)
#else
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_LANNER_MR955)
#endif
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GCT105)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_AGS106)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_SGM107)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GCT108)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_AGS109)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_GCT110)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_AIR_SENDER)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_AIR_RECEIVER)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_ACCTON2_TX)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_ACCTON2_RX)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_TX)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_WSTRNSNIC_RX)
#if defined(OCTEON_VENDOR_LANNER)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_LANNER_MR730)
#else
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_L2_ZINWELL)
#endif
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_DEFINED_MAX)

        /* Customer private range */
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_PRIVATE_MIN)
#if defined(OCTEON_VENDOR_LANNER)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_LANNER_MR320)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_LANNER_MR321X)
#endif
#if defined(OCTEON_VENDOR_UBIQUITI)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_UBIQUITI_E100)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_UBIQUITI_E120)
#endif
#if defined(OCTEON_VENDOR_RADISYS)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE)
#endif
#if defined(OCTEON_VENDOR_GEFES)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_TNPA5804)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_W5434)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_W5650)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_W5800)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_W5651X)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_TNPA5651X)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_TNPA56X4)
	ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_W63XX)
#endif
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_CUST_PRIVATE_MAX)

        /* Module range */
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_MIN)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_PCIE_RC_4X)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_PCIE_EP_4X)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_SGMII_MARVEL)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_SFPPLUS_BCM)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_SRIO)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_EBB5600_QLM0)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_EBB5600_QLM1)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_EBB5600_QLM2)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_EBB5600_QLM3)
        ENUM_BRD_TYPE_CASE(CVMX_BOARD_TYPE_MODULE_MAX)
    }
    return "Unsupported Board";
}

#define ENUM_CHIP_TYPE_CASE(x)   case x: return(#x + 15);   /* Skip CVMX_CHIP_TYPE */
static inline const char *cvmx_chip_type_to_string(enum cvmx_chip_types_enum type)
{
    switch (type)
    {
        ENUM_CHIP_TYPE_CASE(CVMX_CHIP_TYPE_NULL)
        ENUM_CHIP_TYPE_CASE(CVMX_CHIP_SIM_TYPE_DEPRECATED)
        ENUM_CHIP_TYPE_CASE(CVMX_CHIP_TYPE_OCTEON_SAMPLE)
        ENUM_CHIP_TYPE_CASE(CVMX_CHIP_TYPE_MAX)
    }
    return "Unsupported Chip";
}


extern int cvmx_debug_uart;



#ifdef	__cplusplus
}
#endif

#endif /* __CVMX_APP_INIT_H__ */
