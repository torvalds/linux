/*
 * Copyright © 2000-2010 David Woodhouse <dwmw2@infradead.org> et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __MTD_CFI_H__
#define __MTD_CFI_H__

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/mtd/flashchip.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi_endian.h>
#include <linux/mtd/xip.h>

#ifdef CONFIG_MTD_CFI_I1
#define cfi_interleave(cfi) 1
#define cfi_interleave_is_1(cfi) (cfi_interleave(cfi) == 1)
#else
#define cfi_interleave_is_1(cfi) (0)
#endif

#ifdef CONFIG_MTD_CFI_I2
# ifdef cfi_interleave
#  undef cfi_interleave
#  define cfi_interleave(cfi) ((cfi)->interleave)
# else
#  define cfi_interleave(cfi) 2
# endif
#define cfi_interleave_is_2(cfi) (cfi_interleave(cfi) == 2)
#else
#define cfi_interleave_is_2(cfi) (0)
#endif

#ifdef CONFIG_MTD_CFI_I4
# ifdef cfi_interleave
#  undef cfi_interleave
#  define cfi_interleave(cfi) ((cfi)->interleave)
# else
#  define cfi_interleave(cfi) 4
# endif
#define cfi_interleave_is_4(cfi) (cfi_interleave(cfi) == 4)
#else
#define cfi_interleave_is_4(cfi) (0)
#endif

#ifdef CONFIG_MTD_CFI_I8
# ifdef cfi_interleave
#  undef cfi_interleave
#  define cfi_interleave(cfi) ((cfi)->interleave)
# else
#  define cfi_interleave(cfi) 8
# endif
#define cfi_interleave_is_8(cfi) (cfi_interleave(cfi) == 8)
#else
#define cfi_interleave_is_8(cfi) (0)
#endif

#ifndef cfi_interleave
#warning No CONFIG_MTD_CFI_Ix selected. No NOR chip support can work.
static inline int cfi_interleave(void *cfi)
{
	BUG();
	return 0;
}
#endif

static inline int cfi_interleave_supported(int i)
{
	switch (i) {
#ifdef CONFIG_MTD_CFI_I1
	case 1:
#endif
#ifdef CONFIG_MTD_CFI_I2
	case 2:
#endif
#ifdef CONFIG_MTD_CFI_I4
	case 4:
#endif
#ifdef CONFIG_MTD_CFI_I8
	case 8:
#endif
		return 1;

	default:
		return 0;
	}
}


/* NB: these values must represents the number of bytes needed to meet the
 *     device type (x8, x16, x32).  Eg. a 32 bit device is 4 x 8 bytes.
 *     These numbers are used in calculations.
 */
#define CFI_DEVICETYPE_X8  (8 / 8)
#define CFI_DEVICETYPE_X16 (16 / 8)
#define CFI_DEVICETYPE_X32 (32 / 8)
#define CFI_DEVICETYPE_X64 (64 / 8)


/* Device Interface Code Assignments from the "Common Flash Memory Interface
 * Publication 100" dated December 1, 2001.
 */
#define CFI_INTERFACE_X8_ASYNC		0x0000
#define CFI_INTERFACE_X16_ASYNC		0x0001
#define CFI_INTERFACE_X8_BY_X16_ASYNC	0x0002
#define CFI_INTERFACE_X32_ASYNC		0x0003
#define CFI_INTERFACE_X16_BY_X32_ASYNC	0x0005
#define CFI_INTERFACE_NOT_ALLOWED	0xffff


/* NB: We keep these structures in memory in HOST byteorder, except
 * where individually noted.
 */

/* Basic Query Structure */
struct cfi_ident {
	uint8_t  qry[3];
	uint16_t P_ID;
	uint16_t P_ADR;
	uint16_t A_ID;
	uint16_t A_ADR;
	uint8_t  VccMin;
	uint8_t  VccMax;
	uint8_t  VppMin;
	uint8_t  VppMax;
	uint8_t  WordWriteTimeoutTyp;
	uint8_t  BufWriteTimeoutTyp;
	uint8_t  BlockEraseTimeoutTyp;
	uint8_t  ChipEraseTimeoutTyp;
	uint8_t  WordWriteTimeoutMax;
	uint8_t  BufWriteTimeoutMax;
	uint8_t  BlockEraseTimeoutMax;
	uint8_t  ChipEraseTimeoutMax;
	uint8_t  DevSize;
	uint16_t InterfaceDesc;
	uint16_t MaxBufWriteSize;
	uint8_t  NumEraseRegions;
	uint32_t EraseRegionInfo[0]; /* Not host ordered */
} __packed;

/* Extended Query Structure for both PRI and ALT */

struct cfi_extquery {
	uint8_t  pri[3];
	uint8_t  MajorVersion;
	uint8_t  MinorVersion;
} __packed;

/* Vendor-Specific PRI for Intel/Sharp Extended Command Set (0x0001) */

struct cfi_pri_intelext {
	uint8_t  pri[3];
	uint8_t  MajorVersion;
	uint8_t  MinorVersion;
	uint32_t FeatureSupport; /* if bit 31 is set then an additional uint32_t feature
				    block follows - FIXME - not currently supported */
	uint8_t  SuspendCmdSupport;
	uint16_t BlkStatusRegMask;
	uint8_t  VccOptimal;
	uint8_t  VppOptimal;
	uint8_t  NumProtectionFields;
	uint16_t ProtRegAddr;
	uint8_t  FactProtRegSize;
	uint8_t  UserProtRegSize;
	uint8_t  extra[0];
} __packed;

struct cfi_intelext_otpinfo {
	uint32_t ProtRegAddr;
	uint16_t FactGroups;
	uint8_t  FactProtRegSize;
	uint16_t UserGroups;
	uint8_t  UserProtRegSize;
} __packed;

struct cfi_intelext_blockinfo {
	uint16_t NumIdentBlocks;
	uint16_t BlockSize;
	uint16_t MinBlockEraseCycles;
	uint8_t  BitsPerCell;
	uint8_t  BlockCap;
} __packed;

struct cfi_intelext_regioninfo {
	uint16_t NumIdentPartitions;
	uint8_t  NumOpAllowed;
	uint8_t  NumOpAllowedSimProgMode;
	uint8_t  NumOpAllowedSimEraMode;
	uint8_t  NumBlockTypes;
	struct cfi_intelext_blockinfo BlockTypes[1];
} __packed;

struct cfi_intelext_programming_regioninfo {
	uint8_t  ProgRegShift;
	uint8_t  Reserved1;
	uint8_t  ControlValid;
	uint8_t  Reserved2;
	uint8_t  ControlInvalid;
	uint8_t  Reserved3;
} __packed;

/* Vendor-Specific PRI for AMD/Fujitsu Extended Command Set (0x0002) */

struct cfi_pri_amdstd {
	uint8_t  pri[3];
	uint8_t  MajorVersion;
	uint8_t  MinorVersion;
	uint8_t  SiliconRevision; /* bits 1-0: Address Sensitive Unlock */
	uint8_t  EraseSuspend;
	uint8_t  BlkProt;
	uint8_t  TmpBlkUnprotect;
	uint8_t  BlkProtUnprot;
	uint8_t  SimultaneousOps;
	uint8_t  BurstMode;
	uint8_t  PageMode;
	uint8_t  VppMin;
	uint8_t  VppMax;
	uint8_t  TopBottom;
	/* Below field are added from version 1.5 */
	uint8_t  ProgramSuspend;
	uint8_t  UnlockBypass;
	uint8_t  SecureSiliconSector;
	uint8_t  SoftwareFeatures;
#define CFI_POLL_STATUS_REG	BIT(0)
#define CFI_POLL_DQ		BIT(1)
} __packed;

/* Vendor-Specific PRI for Atmel chips (command set 0x0002) */

struct cfi_pri_atmel {
	uint8_t pri[3];
	uint8_t MajorVersion;
	uint8_t MinorVersion;
	uint8_t Features;
	uint8_t BottomBoot;
	uint8_t BurstMode;
	uint8_t PageMode;
} __packed;

struct cfi_pri_query {
	uint8_t  NumFields;
	uint32_t ProtField[1]; /* Not host ordered */
} __packed;

struct cfi_bri_query {
	uint8_t  PageModeReadCap;
	uint8_t  NumFields;
	uint32_t ConfField[1]; /* Not host ordered */
} __packed;

#define P_ID_NONE               0x0000
#define P_ID_INTEL_EXT          0x0001
#define P_ID_AMD_STD            0x0002
#define P_ID_INTEL_STD          0x0003
#define P_ID_AMD_EXT            0x0004
#define P_ID_WINBOND            0x0006
#define P_ID_ST_ADV             0x0020
#define P_ID_MITSUBISHI_STD     0x0100
#define P_ID_MITSUBISHI_EXT     0x0101
#define P_ID_SST_PAGE           0x0102
#define P_ID_SST_OLD            0x0701
#define P_ID_INTEL_PERFORMANCE  0x0200
#define P_ID_INTEL_DATA         0x0210
#define P_ID_RESERVED           0xffff


#define CFI_MODE_CFI	1
#define CFI_MODE_JEDEC	0

struct cfi_private {
	uint16_t cmdset;
	void *cmdset_priv;
	int interleave;
	int device_type;
	int cfi_mode;		/* Are we a JEDEC device pretending to be CFI? */
	int addr_unlock1;
	int addr_unlock2;
	struct mtd_info *(*cmdset_setup)(struct map_info *);
	struct cfi_ident *cfiq; /* For now only one. We insist that all devs
				  must be of the same type. */
	int mfr, id;
	int numchips;
	map_word sector_erase_cmd;
	unsigned long chipshift; /* Because they're of the same type */
	const char *im_name;	 /* inter_module name for cmdset_setup */
	struct flchip chips[0];  /* per-chip data structure for each chip */
};

uint32_t cfi_build_cmd_addr(uint32_t cmd_ofs,
				struct map_info *map, struct cfi_private *cfi);

map_word cfi_build_cmd(u_long cmd, struct map_info *map, struct cfi_private *cfi);
#define CMD(x)  cfi_build_cmd((x), map, cfi)

unsigned long cfi_merge_status(map_word val, struct map_info *map,
					   struct cfi_private *cfi);
#define MERGESTATUS(x) cfi_merge_status((x), map, cfi)

uint32_t cfi_send_gen_cmd(u_char cmd, uint32_t cmd_addr, uint32_t base,
				struct map_info *map, struct cfi_private *cfi,
				int type, map_word *prev_val);

static inline uint8_t cfi_read_query(struct map_info *map, uint32_t addr)
{
	map_word val = map_read(map, addr);

	if (map_bankwidth_is_1(map)) {
		return val.x[0];
	} else if (map_bankwidth_is_2(map)) {
		return cfi16_to_cpu(map, val.x[0]);
	} else {
		/* No point in a 64-bit byteswap since that would just be
		   swapping the responses from different chips, and we are
		   only interested in one chip (a representative sample) */
		return cfi32_to_cpu(map, val.x[0]);
	}
}

static inline uint16_t cfi_read_query16(struct map_info *map, uint32_t addr)
{
	map_word val = map_read(map, addr);

	if (map_bankwidth_is_1(map)) {
		return val.x[0] & 0xff;
	} else if (map_bankwidth_is_2(map)) {
		return cfi16_to_cpu(map, val.x[0]);
	} else {
		/* No point in a 64-bit byteswap since that would just be
		   swapping the responses from different chips, and we are
		   only interested in one chip (a representative sample) */
		return cfi32_to_cpu(map, val.x[0]);
	}
}

void cfi_udelay(int us);

int __xipram cfi_qry_present(struct map_info *map, __u32 base,
			     struct cfi_private *cfi);
int __xipram cfi_qry_mode_on(uint32_t base, struct map_info *map,
			     struct cfi_private *cfi);
void __xipram cfi_qry_mode_off(uint32_t base, struct map_info *map,
			       struct cfi_private *cfi);

struct cfi_extquery *cfi_read_pri(struct map_info *map, uint16_t adr, uint16_t size,
			     const char* name);
struct cfi_fixup {
	uint16_t mfr;
	uint16_t id;
	void (*fixup)(struct mtd_info *mtd);
};

#define CFI_MFR_ANY		0xFFFF
#define CFI_ID_ANY		0xFFFF
#define CFI_MFR_CONTINUATION	0x007F

#define CFI_MFR_AMD		0x0001
#define CFI_MFR_AMIC		0x0037
#define CFI_MFR_ATMEL		0x001F
#define CFI_MFR_EON		0x001C
#define CFI_MFR_FUJITSU		0x0004
#define CFI_MFR_HYUNDAI		0x00AD
#define CFI_MFR_INTEL		0x0089
#define CFI_MFR_MACRONIX	0x00C2
#define CFI_MFR_NEC		0x0010
#define CFI_MFR_PMC		0x009D
#define CFI_MFR_SAMSUNG		0x00EC
#define CFI_MFR_SHARP		0x00B0
#define CFI_MFR_SST		0x00BF
#define CFI_MFR_ST		0x0020 /* STMicroelectronics */
#define CFI_MFR_MICRON		0x002C /* Micron */
#define CFI_MFR_TOSHIBA		0x0098
#define CFI_MFR_WINBOND		0x00DA

void cfi_fixup(struct mtd_info *mtd, struct cfi_fixup* fixups);

typedef int (*varsize_frob_t)(struct map_info *map, struct flchip *chip,
			      unsigned long adr, int len, void *thunk);

int cfi_varsize_frob(struct mtd_info *mtd, varsize_frob_t frob,
	loff_t ofs, size_t len, void *thunk);


#endif /* __MTD_CFI_H__ */
