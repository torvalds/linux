/*	$OpenBSD: octeonvar.h,v 1.54 2022/08/29 02:01:18 jsg Exp $	*/
/*	$NetBSD: maltavar.h,v 1.3 2002/03/18 10:10:16 simonb Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MIPS_OCTEON_OCTEONVAR_H_
#define _MIPS_OCTEON_OCTEONVAR_H_

#include <machine/bus.h>

/* XXX elsewhere */
#define	_ASM_PROLOGUE \
		"	.set push			\n" \
		"	.set noreorder			\n"
#define	_ASM_PROLOGUE_MIPS64 \
		_ASM_PROLOGUE				\
		"	.set mips64			\n"
#define	_ASM_PROLOGUE_OCTEON \
		_ASM_PROLOGUE				\
		"	.set arch=octeon		\n"
#define	_ASM_EPILOGUE \
		"	.set pop			\n"
/*
 * subbits = __BITS64_GET(XXX, bits);
 * bits = __BITS64_SET(XXX, subbits);
 */
#ifndef	__BITS64_GET
#define	__BITS64_GET(name, bits)	\
	    (((uint64_t)(bits) & name) >> name##_SHIFT)
#endif
#ifndef	__BITS64_SET
#define	__BITS64_SET(name, subbits)	\
	    (((uint64_t)(subbits) << name##_SHIFT) & name)
#endif

struct octeon_config {
	bus_space_tag_t mc_iobus_bust;
	bus_space_tag_t mc_bootbus_bust;

	bus_dma_tag_t mc_iobus_dmat;
	bus_dma_tag_t mc_bootbus_dmat;
};

#define	GPIO_CONFIG_MD_OUTPUT_SEL_MASK	(GPIO_CONFIG_MD0 | GPIO_CONFIG_MD1)
#define	GPIO_CONFIG_MD_USB0_VBUS_CTRL	GPIO_CONFIG_MD0
#define	GPIO_CONFIG_MD_USB1_VBUS_CTRL	GPIO_CONFIG_MD1

/*
 * FPA map
 */

#define	OCTEON_POOL_NO_PKT	0
#define	OCTEON_POOL_NO_WQE	1
#define	OCTEON_POOL_NO_CMD	2
#define	OCTEON_POOL_NO_SG	3
#define	OCTEON_POOL_NO_XXX_4	4
#define	OCTEON_POOL_NO_XXX_5	5
#define	OCTEON_POOL_NO_XXX_6	6
#define	OCTEON_POOL_NO_DUMP	7	/* FPA debug dump */

#define	OCTEON_POOL_SIZE_PKT	1920	/* 128 x 15 */
#define	OCTEON_POOL_SIZE_WQE	128	/* 128 x 1 */
#define	OCTEON_POOL_SIZE_CMD	1024	/* 128 x 8 */
#define	OCTEON_POOL_SIZE_SG	128	/* 128 x 1 */
#define	OCTEON_POOL_SIZE_XXX_4	0
#define	OCTEON_POOL_SIZE_XXX_5	0
#define	OCTEON_POOL_SIZE_XXX_6	0
#define	OCTEON_POOL_SIZE_XXX_7	0

#define	OCTEON_POOL_NELEMS_PKT		4096
#define	OCTEON_POOL_NELEMS_WQE		4096
#define	OCTEON_POOL_NELEMS_CMD		32
#define	OCTEON_POOL_NELEMS_SG		4096
#define	OCTEON_POOL_NELEMS_XXX_4	0
#define	OCTEON_POOL_NELEMS_XXX_5	0
#define	OCTEON_POOL_NELEMS_XXX_6	0
#define	OCTEON_POOL_NELEMS_XXX_7	0

/*
 * CVMSEG (``scratch'') memory map
 */
struct octeon_cvmseg_map {
	uint64_t		csm_pow_intr;

	struct octeon_cvmseg_ether_map {
		uint64_t	csm_ether_fau_done;
	} csm_ether[12/* XXX */];
} __packed;
#define	OCTEON_CVMSEG_OFFSET(entry) \
	offsetof(struct octeon_cvmseg_map, entry)
#define	OCTEON_CVMSEG_ETHER_OFFSET(n, entry) \
	(offsetof(struct octeon_cvmseg_map, csm_ether) + \
	 sizeof(struct octeon_cvmseg_ether_map) * (n) + \
	 offsetof(struct octeon_cvmseg_ether_map, entry))

/*
 * FAU register map
 *
 * => FAU registers exist in FAU unit
 * => devices (PKO) can access these registers
 * => CPU can read those values after loading them into CVMSEG
 */
struct octeon_fau_map {
	struct {
		/* PKO command index */
		uint64_t	_fau_map_port_pkocmdidx;
		/* send requested */
		uint64_t	_fau_map_port_txreq;
		/* send completed */
		uint64_t	_fau_map_port_txdone;
		/* XXX */
		uint64_t	_fau_map_port_pad;
	} __packed _fau_map_port[3];
};

/*
 * POW qos/group map
 */

#define	OCTEON_POW_QOS_PIP		0
#define	OCTEON_POW_QOS_CORE1		1
#define	OCTEON_POW_QOS_XXX_2		2
#define	OCTEON_POW_QOS_XXX_3		3
#define	OCTEON_POW_QOS_XXX_4		4
#define	OCTEON_POW_QOS_XXX_5		5
#define	OCTEON_POW_QOS_XXX_6		6
#define	OCTEON_POW_QOS_XXX_7		7

#define	OCTEON_POW_GROUP_MAX		16

enum cnmac_stat {
	cnmac_stat_rx_toto_gmx,
	cnmac_stat_rx_totp_gmx,
	cnmac_stat_rx_toto_pip,
	cnmac_stat_rx_totp_pip,
	cnmac_stat_rx_h64,
	cnmac_stat_rx_h127,
	cnmac_stat_rx_h255,
	cnmac_stat_rx_h511,
	cnmac_stat_rx_h1023,
	cnmac_stat_rx_h1518,
	cnmac_stat_rx_hmax,
	cnmac_stat_rx_bcast,
	cnmac_stat_rx_mcast,
	cnmac_stat_rx_qdpo,
	cnmac_stat_rx_qdpp,
	cnmac_stat_rx_fcs,
	cnmac_stat_rx_frag,
	cnmac_stat_rx_undersz,
	cnmac_stat_rx_jabber,
	cnmac_stat_rx_oversz,
	cnmac_stat_rx_raw,
	cnmac_stat_rx_bad,
	cnmac_stat_rx_drop,
	cnmac_stat_rx_ctl,
	cnmac_stat_rx_dmac,
	cnmac_stat_tx_toto,
	cnmac_stat_tx_totp,
	cnmac_stat_tx_hmin,
	cnmac_stat_tx_h64,
	cnmac_stat_tx_h127,
	cnmac_stat_tx_h255,
	cnmac_stat_tx_h511,
	cnmac_stat_tx_h1023,
	cnmac_stat_tx_h1518,
	cnmac_stat_tx_hmax,
	cnmac_stat_tx_bcast,
	cnmac_stat_tx_mcast,
	cnmac_stat_tx_coll,
	cnmac_stat_tx_defer,
	cnmac_stat_tx_scol,
	cnmac_stat_tx_mcol,
	cnmac_stat_tx_ctl,
	cnmac_stat_tx_uflow,
	cnmac_stat_count
};

#if defined(_KERNEL) || defined(_STANDALONE)
#define OCTEON_ARGV_MAX 64

/*
 * OCTEON board types recognized by OpenBSD/octeon.
 *
 * It is fine to use BOARD_UNKNOWN when the board does not need
 * special treatment.
 */
enum octeon_board {
	BOARD_UNKNOWN,
	BOARD_CHECKPOINT_N100,
	BOARD_CN3010_EVB_HS5,
	BOARD_DLINK_DSR_500,
	BOARD_NETGEAR_UTM25,
	BOARD_RHINOLABS_UTM8,
	BOARD_UBIQUITI_E100,
	BOARD_UBIQUITI_E120,
	BOARD_UBIQUITI_E200,
	BOARD_UBIQUITI_E220,
	BOARD_UBIQUITI_E300,
	BOARD_UBIQUITI_E1000,
};

struct boot_desc {
	uint32_t	desc_ver;
	uint32_t	desc_size;
	uint64_t	stack_top;
	uint64_t 	heap_start;
	uint64_t	heap_end;
	uint64_t      	__unused17;
	uint64_t     	__unused16;
	uint32_t      	__unused18;
	uint32_t      	__unused15;
	uint32_t      	__unused14;
	uint32_t	argc;
	uint32_t	argv[OCTEON_ARGV_MAX];
	uint32_t	flags;
	uint32_t	core_mask;
	uint32_t	dram_size;
	uint32_t	phy_mem_desc_addr;
	uint32_t	debugger_flag_addr;
	uint32_t	eclock;
	uint32_t      	__unused10;
	uint32_t      	__unused9;
	uint16_t      	__unused8;
	uint8_t 	__unused7;
	uint8_t 	__unused6;
	uint16_t 	__unused5;
	uint8_t 	__unused4;
	uint8_t 	__unused3;
	uint8_t 	__unused2[20];
	uint8_t 	__unused1[6];
	uint8_t 	__unused0;
	uint64_t 	boot_info_addr;
};

struct boot_info {
	uint32_t ver_major;
	uint32_t ver_minor;
	uint64_t stack_top;
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t boot_desc_addr;
	uint32_t exception_base_addr;
	uint32_t stack_size;
	uint32_t flags;
	uint32_t core_mask;
	uint32_t dram_size;
	uint32_t phys_mem_desc_addr;
	uint32_t debugger_flags_addr;
	uint32_t eclock;
	uint32_t dclock;
	uint32_t __unused0;
	uint16_t board_type;
	uint8_t board_rev_major;
	uint8_t board_rev_minor;
	uint16_t __unused1;
	uint8_t __unused2;
	uint8_t __unused3;
	char board_serial[20];
	uint8_t mac_addr_base[6];
	uint8_t mac_addr_count;
	uint64_t cf_common_addr;
	uint64_t cf_attr_addr;
	uint64_t led_display_addr;
	uint32_t dfaclock;
	uint32_t config_flags;
	/* The fields below are available when ver_minor >= 3. */
	uint64_t fdt_addr;
};

struct octeon_bootmem_desc {
	uint32_t	lock;
	uint32_t	flags;
	uint64_t	head_addr;
	uint32_t	major_version;
	uint32_t	minor_version;
	uint64_t	app_data_addr;
	uint64_t	app_data_size;
	uint32_t	named_block_num_blocks;
	uint32_t	named_block_name_len;
	uint64_t	named_block_array_addr;
};

struct octeon_bootmem_block {
	uint64_t	next;
	uint64_t	size;
};

extern enum octeon_board octeon_board;
extern struct boot_desc *octeon_boot_desc;
extern struct boot_info *octeon_boot_info;

#ifdef _KERNEL
/* Device capabilities advertised in boot_info->config_flags */
#define BOOTINFO_CFG_FLAG_PCI_HOST	(1ull << 0)
#define BOOTINFO_CFG_FLAG_PCI_TARGET	(1ull << 1)
#define BOOTINFO_CFG_FLAG_DEBUG		(1ull << 2)
#define BOOTINFO_CFG_FLAG_NO_MAGIC	(1ull << 3)

#define BOOTMEM_BLOCK_ALIGN		16
#define BOOTMEM_BLOCK_MASK		(BOOTMEM_BLOCK_ALIGN - 1)
#define BOOTMEM_BLOCK_MIN_SIZE		16

int	bootmem_alloc_region(paddr_t, size_t);
void	bootmem_free(paddr_t, size_t);

int	octeon_ioclock_speed(void);

#endif /* _KERNEL */
#endif /* _KERNEL || _STANDALONE */

static inline int
ffs64(uint64_t val)
{
	int ret;

	__asm volatile ( \
		_ASM_PROLOGUE_MIPS64
		"	dclz	%0, %1			\n"
		_ASM_EPILOGUE
		: "=r"(ret) : "r"(val));
	return 64 - ret;
}

static inline int
ffs32(uint32_t val)
{
	int ret;

	__asm volatile ( \
		_ASM_PROLOGUE_MIPS64
		"	clz	%0, %1			\n"
		_ASM_EPILOGUE
		: "=r"(ret) : "r"(val));
	return 32 - ret;
}

static inline uint64_t
octeon_xkphys_read_8(paddr_t address)
{
	volatile uint64_t *p =
	    (volatile uint64_t *)(PHYS_TO_XKPHYS(address, CCA_NC));
	return (*p);
}

#define	MIO_BOOT_BIST_STAT			0x00011800000000f8ULL
static inline void
octeon_xkphys_write_8(paddr_t address, uint64_t value)
{
	*(volatile uint64_t *)(PHYS_TO_XKPHYS(address, CCA_NC)) = value;

	/*
	 * It seems an immediate read is necessary when doing a write to an RSL
	 * register in order to complete the write.
	 * We use MIO_BOOT_BIST_STAT because it's apparently the fastest
	 * write.
	 */

	/*
	 * XXX
	 * This if would be better written as:
	 * if ((address & 0xffffff0000000000ULL) == OCTEON_MIO_BOOT_BASE) {
	 * but octeonreg.h can't be included here and we want this inlined
	 *
	 * Note that the SDK masks with 0x7ffff but that doesn't make sense.
	 * This is a physical address.
	 */
	if (((address >> 40) & 0xfffff) == (0x118)) {
		value = *(volatile uint64_t *)
		    (PHYS_TO_XKPHYS(MIO_BOOT_BIST_STAT, CCA_NC));
	}
}

static inline void
octeon_iobdma_write_8(uint64_t value)
{
	uint64_t addr = 0xffffffffffffa200ULL;

	*(volatile uint64_t *)addr = value;
}

static inline void
octeon_lmtdma_write_8(off_t offset, uint64_t value)
{
	*(volatile uint64_t *)(0xffffffffffffa400ULL + offset) = value;
}

static inline uint64_t
octeon_cvmseg_read_8(size_t offset)
{
	return *(volatile uint64_t *)(0xffffffffffff8000ULL + offset);
}

static inline void
octeon_cvmseg_write_8(size_t offset, uint64_t value)
{
	*(volatile uint64_t *)(0xffffffffffff8000ULL + offset) = value;
}

static inline uint32_t
octeon_get_coreid(void)
{
	uint32_t coreid;

	__asm volatile (
		_ASM_PROLOGUE_OCTEON
		"	rdhwr	%0, $0\n"
		_ASM_EPILOGUE
		: "=r" (coreid));
	return coreid;
}

static inline uint64_t
octeon_get_cycles(void)
{
	uint64_t tmp;

	__asm volatile (
		_ASM_PROLOGUE_MIPS64
		"	dmfc0	%[tmp], $9, 6		\n"
		_ASM_EPILOGUE
		: [tmp]"=&r"(tmp));
	return tmp;
}

static inline uint64_t
octeon_get_cvmctl(void)
{
	uint64_t tmp;

	__asm volatile (
		_ASM_PROLOGUE_OCTEON
		"	dmfc0	%[tmp], $9, 7		\n"
		_ASM_EPILOGUE
		: [tmp]"=r"(tmp));
	return tmp;
}

static inline uint64_t
octeon_get_cvmmemctl(void)
{
	uint64_t tmp;

	__asm volatile (
		_ASM_PROLOGUE_OCTEON
		"	dmfc0	%[tmp], $11, 7		\n"
		_ASM_EPILOGUE
		: [tmp]"=r"(tmp));
	return tmp;
}

static inline void
octeon_set_cvmmemctl(uint64_t val)
{
	__asm volatile (
		_ASM_PROLOGUE_OCTEON
		"	dmtc0	%[tmp], $11, 7		\n"
		_ASM_EPILOGUE
		: : [tmp]"r"(val) : "memory");
}

static inline void
octeon_synciobdma(void)
{
	__asm volatile (
		_ASM_PROLOGUE_OCTEON
		"	synciobdma\n"
		_ASM_EPILOGUE
		: : : "memory");
}

#endif	/* _MIPS_OCTEON_OCTEONVAR_H_ */
