
/* Common Flash Interface structures
 * See http://support.intel.com/design/flash/technote/index.htm
 * $Id: cfi.h,v 1.57 2005/11/15 23:28:17 tpoynor Exp $
 */

#ifndef __MTD_CFI_H__
#define __MTD_CFI_H__

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/mtd/flashchip.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi_endian.h>

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
} __attribute__((packed));

/* Extended Query Structure for both PRI and ALT */

struct cfi_extquery {
	uint8_t  pri[3];
	uint8_t  MajorVersion;
	uint8_t  MinorVersion;
} __attribute__((packed));

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
} __attribute__((packed));

struct cfi_intelext_otpinfo {
	uint32_t ProtRegAddr;
	uint16_t FactGroups;
	uint8_t  FactProtRegSize;
	uint16_t UserGroups;
	uint8_t  UserProtRegSize;
} __attribute__((packed));

struct cfi_intelext_blockinfo {
	uint16_t NumIdentBlocks;
	uint16_t BlockSize;
	uint16_t MinBlockEraseCycles;
	uint8_t  BitsPerCell;
	uint8_t  BlockCap;
} __attribute__((packed));

struct cfi_intelext_regioninfo {
	uint16_t NumIdentPartitions;
	uint8_t  NumOpAllowed;
	uint8_t  NumOpAllowedSimProgMode;
	uint8_t  NumOpAllowedSimEraMode;
	uint8_t  NumBlockTypes;
	struct cfi_intelext_blockinfo BlockTypes[1];
} __attribute__((packed));

struct cfi_intelext_programming_regioninfo {
	uint8_t  ProgRegShift;
	uint8_t  Reserved1;
	uint8_t  ControlValid;
	uint8_t  Reserved2;
	uint8_t  ControlInvalid;
	uint8_t  Reserved3;
} __attribute__((packed));

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
} __attribute__((packed));

/* Vendor-Specific PRI for Atmel chips (command set 0x0002) */

struct cfi_pri_atmel {
	uint8_t pri[3];
	uint8_t MajorVersion;
	uint8_t MinorVersion;
	uint8_t Features;
	uint8_t BottomBoot;
	uint8_t BurstMode;
	uint8_t PageMode;
} __attribute__((packed));

struct cfi_pri_query {
	uint8_t  NumFields;
	uint32_t ProtField[1]; /* Not host ordered */
} __attribute__((packed));

struct cfi_bri_query {
	uint8_t  PageModeReadCap;
	uint8_t  NumFields;
	uint32_t ConfField[1]; /* Not host ordered */
} __attribute__((packed));

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
	unsigned long chipshift; /* Because they're of the same type */
	const char *im_name;	 /* inter_module name for cmdset_setup */
	struct flchip chips[0];  /* per-chip data structure for each chip */
};

/*
 * Returns the command address according to the given geometry.
 */
static inline uint32_t cfi_build_cmd_addr(uint32_t cmd_ofs, int interleave, int type)
{
	return (cmd_ofs * type) * interleave;
}

/*
 * Transforms the CFI command for the given geometry (bus width & interleave).
 * It looks too long to be inline, but in the common case it should almost all
 * get optimised away.
 */
static inline map_word cfi_build_cmd(u_long cmd, struct map_info *map, struct cfi_private *cfi)
{
	map_word val = { {0} };
	int wordwidth, words_per_bus, chip_mode, chips_per_word;
	unsigned long onecmd;
	int i;

	/* We do it this way to give the compiler a fighting chance
	   of optimising away all the crap for 'bankwidth' larger than
	   an unsigned long, in the common case where that support is
	   disabled */
	if (map_bankwidth_is_large(map)) {
		wordwidth = sizeof(unsigned long);
		words_per_bus = (map_bankwidth(map)) / wordwidth; // i.e. normally 1
	} else {
		wordwidth = map_bankwidth(map);
		words_per_bus = 1;
	}

	chip_mode = map_bankwidth(map) / cfi_interleave(cfi);
	chips_per_word = wordwidth * cfi_interleave(cfi) / map_bankwidth(map);

	/* First, determine what the bit-pattern should be for a single
	   device, according to chip mode and endianness... */
	switch (chip_mode) {
	default: BUG();
	case 1:
		onecmd = cmd;
		break;
	case 2:
		onecmd = cpu_to_cfi16(cmd);
		break;
	case 4:
		onecmd = cpu_to_cfi32(cmd);
		break;
	}

	/* Now replicate it across the size of an unsigned long, or
	   just to the bus width as appropriate */
	switch (chips_per_word) {
	default: BUG();
#if BITS_PER_LONG >= 64
	case 8:
		onecmd |= (onecmd << (chip_mode * 32));
#endif
	case 4:
		onecmd |= (onecmd << (chip_mode * 16));
	case 2:
		onecmd |= (onecmd << (chip_mode * 8));
	case 1:
		;
	}

	/* And finally, for the multi-word case, replicate it
	   in all words in the structure */
	for (i=0; i < words_per_bus; i++) {
		val.x[i] = onecmd;
	}

	return val;
}
#define CMD(x)  cfi_build_cmd((x), map, cfi)


static inline unsigned long cfi_merge_status(map_word val, struct map_info *map,
					   struct cfi_private *cfi)
{
	int wordwidth, words_per_bus, chip_mode, chips_per_word;
	unsigned long onestat, res = 0;
	int i;

	/* We do it this way to give the compiler a fighting chance
	   of optimising away all the crap for 'bankwidth' larger than
	   an unsigned long, in the common case where that support is
	   disabled */
	if (map_bankwidth_is_large(map)) {
		wordwidth = sizeof(unsigned long);
		words_per_bus = (map_bankwidth(map)) / wordwidth; // i.e. normally 1
	} else {
		wordwidth = map_bankwidth(map);
		words_per_bus = 1;
	}

	chip_mode = map_bankwidth(map) / cfi_interleave(cfi);
	chips_per_word = wordwidth * cfi_interleave(cfi) / map_bankwidth(map);

	onestat = val.x[0];
	/* Or all status words together */
	for (i=1; i < words_per_bus; i++) {
		onestat |= val.x[i];
	}

	res = onestat;
	switch(chips_per_word) {
	default: BUG();
#if BITS_PER_LONG >= 64
	case 8:
		res |= (onestat >> (chip_mode * 32));
#endif
	case 4:
		res |= (onestat >> (chip_mode * 16));
	case 2:
		res |= (onestat >> (chip_mode * 8));
	case 1:
		;
	}

	/* Last, determine what the bit-pattern should be for a single
	   device, according to chip mode and endianness... */
	switch (chip_mode) {
	case 1:
		break;
	case 2:
		res = cfi16_to_cpu(res);
		break;
	case 4:
		res = cfi32_to_cpu(res);
		break;
	default: BUG();
	}
	return res;
}

#define MERGESTATUS(x) cfi_merge_status((x), map, cfi)


/*
 * Sends a CFI command to a bank of flash for the given geometry.
 *
 * Returns the offset in flash where the command was written.
 * If prev_val is non-null, it will be set to the value at the command address,
 * before the command was written.
 */
static inline uint32_t cfi_send_gen_cmd(u_char cmd, uint32_t cmd_addr, uint32_t base,
				struct map_info *map, struct cfi_private *cfi,
				int type, map_word *prev_val)
{
	map_word val;
	uint32_t addr = base + cfi_build_cmd_addr(cmd_addr, cfi_interleave(cfi), type);

	val = cfi_build_cmd(cmd, map, cfi);

	if (prev_val)
		*prev_val = map_read(map, addr);

	map_write(map, val, addr);

	return addr - base;
}

static inline uint8_t cfi_read_query(struct map_info *map, uint32_t addr)
{
	map_word val = map_read(map, addr);

	if (map_bankwidth_is_1(map)) {
		return val.x[0];
	} else if (map_bankwidth_is_2(map)) {
		return cfi16_to_cpu(val.x[0]);
	} else {
		/* No point in a 64-bit byteswap since that would just be
		   swapping the responses from different chips, and we are
		   only interested in one chip (a representative sample) */
		return cfi32_to_cpu(val.x[0]);
	}
}

static inline uint16_t cfi_read_query16(struct map_info *map, uint32_t addr)
{
	map_word val = map_read(map, addr);

	if (map_bankwidth_is_1(map)) {
		return val.x[0] & 0xff;
	} else if (map_bankwidth_is_2(map)) {
		return cfi16_to_cpu(val.x[0]);
	} else {
		/* No point in a 64-bit byteswap since that would just be
		   swapping the responses from different chips, and we are
		   only interested in one chip (a representative sample) */
		return cfi32_to_cpu(val.x[0]);
	}
}

static inline void cfi_udelay(int us)
{
	if (us >= 1000) {
		msleep((us+999)/1000);
	} else {
		udelay(us);
		cond_resched();
	}
}

struct cfi_extquery *cfi_read_pri(struct map_info *map, uint16_t adr, uint16_t size,
			     const char* name);
struct cfi_fixup {
	uint16_t mfr;
	uint16_t id;
	void (*fixup)(struct mtd_info *mtd, void* param);
	void* param;
};

#define CFI_MFR_ANY 0xffff
#define CFI_ID_ANY  0xffff

#define CFI_MFR_AMD 0x0001
#define CFI_MFR_ATMEL 0x001F
#define CFI_MFR_ST  0x0020 	/* STMicroelectronics */

void cfi_fixup(struct mtd_info *mtd, struct cfi_fixup* fixups);

typedef int (*varsize_frob_t)(struct map_info *map, struct flchip *chip,
			      unsigned long adr, int len, void *thunk);

int cfi_varsize_frob(struct mtd_info *mtd, varsize_frob_t frob,
	loff_t ofs, size_t len, void *thunk);


#endif /* __MTD_CFI_H__ */
