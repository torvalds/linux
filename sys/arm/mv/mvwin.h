/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2007-2011 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MVWIN_H_
#define _MVWIN_H_

/*
 * Decode windows addresses.
 *
 * All decoding windows must be aligned to their size, which has to be
 * a power of 2.
 */

/*
 * SoC Integrated devices: 0xF1000000, 16 MB (VA == PA)
 */

/* SoC Regs */
#define MV_PHYS_BASE		0xF1000000
#define MV_SIZE			(1024 * 1024)	/* 1 MB */

/* SRAM */
#define MV_CESA_SRAM_BASE	0xF1100000

/*
 * External devices: 0x80000000, 1 GB (VA == PA)
 * Includes Device Bus, PCI and PCIE.
 */
#if defined(SOC_MV_ORION)
#define MV_PCI_PORTS	2	/* 1x PCI + 1x PCIE */
#elif defined(SOC_MV_KIRKWOOD)
#define MV_PCI_PORTS	1	/* 1x PCIE */
#elif defined(SOC_MV_DISCOVERY)
#define MV_PCI_PORTS	8	/* 8x PCIE */
#else
#define	MV_PCI_PORTS    1       /* 1x PCIE -> worst case */
#endif

/* PCI/PCIE Memory */
#define MV_PCI_MEM_PHYS_BASE	0x80000000
#define MV_PCI_MEM_SIZE		(512 * 1024 * 1024)	/* 512 MB */
#define MV_PCI_MEM_BASE		MV_PCI_MEM_PHYS_BASE
#define MV_PCI_MEM_SLICE_SIZE	(MV_PCI_MEM_SIZE / MV_PCI_PORTS)
/* PCI/PCIE I/O */
#define MV_PCI_IO_PHYS_BASE	0xBF000000
#define MV_PCI_IO_SIZE		(16 * 1024 * 1024)	/* 16 MB */
#define MV_PCI_IO_BASE		MV_PCI_IO_PHYS_BASE
#define MV_PCI_IO_SLICE_SIZE	(MV_PCI_IO_SIZE / MV_PCI_PORTS)
#define MV_PCI_VA_MEM_BASE	0
#define MV_PCI_VA_IO_BASE	0

/*
 * Device Bus (VA == PA)
 */
#define MV_DEV_BOOT_BASE    0xF9300000
#define MV_DEV_BOOT_SIZE    (1024 * 1024)   /* 1 MB */

#define MV_DEV_CS0_BASE     0xF9400000
#define MV_DEV_CS0_SIZE     (1024 * 1024)   /* 1 MB */

#define MV_DEV_CS1_BASE     0xF9500000
#define MV_DEV_CS1_SIZE     (32 * 1024 * 1024)  /* 32 MB */

#define MV_DEV_CS2_BASE     0xFB500000
#define MV_DEV_CS2_SIZE     (1024 * 1024)   /* 1 MB */


/*
 * Integrated SoC peripherals addresses
 */
#define MV_BASE			MV_PHYS_BASE	/* VA == PA mapping */
#define	MV_DDR_CADR_BASE_ARMV7	(MV_BASE + 0x20180)
#define MV_DDR_CADR_BASE	(MV_BASE + 0x1500)
#define MV_MPP_BASE		(MV_BASE + 0x10000)


#define MV_MISC_BASE		(MV_BASE + 0x18200)
#define MV_MBUS_BRIDGE_BASE	(MV_BASE + 0x20000)
#define MV_INTREGS_BASE		(MV_MBUS_BRIDGE_BASE + 0x80)
#define MV_MP_CLOCKS_BASE	(MV_MBUS_BRIDGE_BASE + 0x700)

#define	MV_CPU_CONTROL_BASE_ARMV7	(MV_MBUS_BRIDGE_BASE + 0x1800)
#define MV_CPU_CONTROL_BASE	(MV_MBUS_BRIDGE_BASE + 0x100)

#define MV_PCI_BASE		(MV_BASE + 0x30000)
#define MV_PCI_SIZE		0x2000

#define	MV_PCIE_BASE_ARMADA38X	(MV_BASE + 0x80000)
#define MV_PCIE_BASE		(MV_BASE + 0x40000)
#define MV_PCIE_SIZE		0x2000
#define MV_SDIO_BASE		(MV_BASE + 0x90000)
#define MV_SDIO_SIZE		0x10000

/*
 * Decode windows definitions and macros
 */
#define	MV_WIN_CPU_CTRL_ARMV7(n)		(((n) < 8) ? 0x10 * (n) :  0x90 + (0x8 * ((n) - 8)))
#define	MV_WIN_CPU_BASE_ARMV7(n)		((((n) < 8) ? 0x10 * (n) :  0x90 + (0x8 * ((n) - 8))) + 0x4)
#define	MV_WIN_CPU_REMAP_LO_ARMV7(n)	(0x10 * (n) +  0x008)
#define	MV_WIN_CPU_REMAP_HI_ARMV7(n)	(0x10 * (n) +  0x00C)

#define	MV_WIN_CPU_CTRL_ARMV5(n)		(0x10 * (n) + (((n) < 8) ? 0x000 : 0x880))
#define	MV_WIN_CPU_BASE_ARMV5(n)		(0x10 * (n) + (((n) < 8) ? 0x004 : 0x884))
#define	MV_WIN_CPU_REMAP_LO_ARMV5(n)		(0x10 * (n) + (((n) < 8) ? 0x008 : 0x888))
#define	MV_WIN_CPU_REMAP_HI_ARMV5(n)		(0x10 * (n) + (((n) < 8) ? 0x00C : 0x88C))

#if defined(SOC_MV_DISCOVERY)
#define MV_WIN_CPU_MAX			14
#else
#define MV_WIN_CPU_MAX			8
#endif
#define	MV_WIN_CPU_MAX_ARMV7		20

#define MV_WIN_CPU_ATTR_SHIFT		8
#define MV_WIN_CPU_TARGET_SHIFT		4
#define MV_WIN_CPU_ENABLE_BIT		1

#define MV_WIN_DDR_BASE(n)		(0x8 * (n) + 0x0)
#define MV_WIN_DDR_SIZE(n)		(0x8 * (n) + 0x4)
#define MV_WIN_DDR_MAX			4

/*
 * These values are valid only for peripherals decoding windows
 * Bit in ATTR is zeroed according to CS bank number
 */
#define MV_WIN_DDR_ATTR(cs)		(0x0F & ~(0x01 << (cs)))
#define MV_WIN_DDR_TARGET		0x0

#if defined(SOC_MV_DISCOVERY)
#define MV_WIN_CESA_TARGET		9
#define MV_WIN_CESA_ATTR(eng_sel)	1
#else
#define	MV_WIN_CESA_TARGET		3
#define	MV_WIN_CESA_ATTR(eng_sel)	0
#endif

#define	MV_WIN_CESA_TARGET_ARMADAXP	9
/*
 * Bits [2:3] of cesa attribute select engine:
 * eng_sel:
 *  1: engine1
 *  2: engine0
 */
#define	MV_WIN_CESA_ATTR_ARMADAXP(eng_sel)	(1 | ((eng_sel) << 2))
#define	MV_WIN_CESA_TARGET_ARMADA38X		9
/*
 * Bits [1:0] = Data swapping
 *  0x0 = Byte swap
 *  0x1 = No swap
 *  0x2 = Byte and word swap
 *  0x3 = Word swap
 * Bits [4:2] = CESA select:
 *  0x6 = CESA0
 *  0x5 = CESA1
 */
#define	MV_WIN_CESA_ATTR_ARMADA38X(eng_sel)	(0x11 | (1 << (3 - (eng_sel))))
/* CESA TDMA address decoding registers */
#define MV_WIN_CESA_CTRL(n)		(0x8 * (n) + 0xA04)
#define MV_WIN_CESA_BASE(n)		(0x8 * (n) + 0xA00)
#define MV_WIN_CESA_MAX			4

#define MV_WIN_USB_CTRL(n)		(0x10 * (n) + 0x320)
#define MV_WIN_USB_BASE(n)		(0x10 * (n) + 0x324)
#define MV_WIN_USB_MAX			4

#define	MV_WIN_USB3_CTRL(n)		(0x8 * (n) + 0x4000)
#define	MV_WIN_USB3_BASE(n)		(0x8 * (n) + 0x4004)
#define	MV_WIN_USB3_MAX			8

#define	MV_WIN_NETA_OFFSET		0x2000
#define	MV_WIN_NETA_BASE(n)		MV_WIN_ETH_BASE(n) + MV_WIN_NETA_OFFSET

#define MV_WIN_CESA_OFFSET		0x2000

#define MV_WIN_ETH_BASE(n)		(0x8 * (n) + 0x200)
#define MV_WIN_ETH_SIZE(n)		(0x8 * (n) + 0x204)
#define MV_WIN_ETH_REMAP(n)		(0x4 * (n) + 0x280)
#define MV_WIN_ETH_MAX			6

#define MV_WIN_IDMA_BASE(n)		(0x8 * (n) + 0xa00)
#define MV_WIN_IDMA_SIZE(n)		(0x8 * (n) + 0xa04)
#define MV_WIN_IDMA_REMAP(n)		(0x4 * (n) + 0xa60)
#define MV_WIN_IDMA_CAP(n)		(0x4 * (n) + 0xa70)
#define MV_WIN_IDMA_MAX			8
#define MV_IDMA_CHAN_MAX		4

#define MV_WIN_XOR_BASE(n, m)		(0x4 * (n) + 0xa50 + (m) * 0x100)
#define MV_WIN_XOR_SIZE(n, m)		(0x4 * (n) + 0xa70 + (m) * 0x100)
#define MV_WIN_XOR_REMAP(n, m)		(0x4 * (n) + 0xa90 + (m) * 0x100)
#define MV_WIN_XOR_CTRL(n, m)		(0x4 * (n) + 0xa40 + (m) * 0x100)
#define MV_WIN_XOR_OVERR(n, m)		(0x4 * (n) + 0xaa0 + (m) * 0x100)
#define MV_WIN_XOR_MAX			8
#define MV_XOR_CHAN_MAX			2
#define MV_XOR_NON_REMAP		4

#define	MV_WIN_PCIE_TARGET_ARMADAXP(n)		(4 + (4 * ((n) % 2)))
#define	MV_WIN_PCIE_MEM_ATTR_ARMADAXP(n)	(0xE8 + (0x10 * ((n) / 2)))
#define	MV_WIN_PCIE_IO_ATTR_ARMADAXP(n)		(0xE0 + (0x10 * ((n) / 2)))
#define	MV_WIN_PCIE_TARGET_ARMADA38X(n)		((n) == 0 ? 8 : 4)
#define	MV_WIN_PCIE_MEM_ATTR_ARMADA38X(n)	((n) < 2 ? 0xE8 : (0xD8 - (((n) % 2) * 0x20)))
#define	MV_WIN_PCIE_IO_ATTR_ARMADA38X(n)	((n) < 2 ? 0xE0 : (0xD0 - (((n) % 2) * 0x20)))
#if defined(SOC_MV_DISCOVERY) || defined(SOC_MV_KIRKWOOD)
#define MV_WIN_PCIE_TARGET(n)		4
#define MV_WIN_PCIE_MEM_ATTR(n)		0xE8
#define MV_WIN_PCIE_IO_ATTR(n)		0xE0
#elif defined(SOC_MV_ORION)
#define MV_WIN_PCIE_TARGET(n)		4
#define MV_WIN_PCIE_MEM_ATTR(n)		0x59
#define MV_WIN_PCIE_IO_ATTR(n)		0x51
#else
#define	MV_WIN_PCIE_TARGET(n)           (4 + (4 * ((n) % 2)))
#define	MV_WIN_PCIE_MEM_ATTR(n)         (0xE8 + (0x10 * ((n) / 2)))
#define	MV_WIN_PCIE_IO_ATTR(n)          (0xE0 + (0x10 * ((n) / 2)))
#endif

#define MV_WIN_PCI_TARGET		3
#define MV_WIN_PCI_MEM_ATTR		0x59
#define MV_WIN_PCI_IO_ATTR		0x51

#define MV_WIN_PCIE_CTRL(n)		(0x10 * (((n) < 5) ? (n) : \
					    (n) + 1) + 0x1820)
#define MV_WIN_PCIE_BASE(n)		(0x10 * (((n) < 5) ? (n) : \
					    (n) + 1) + 0x1824)
#define MV_WIN_PCIE_REMAP(n)		(0x10 * (((n) < 5) ? (n) : \
					    (n) + 1) + 0x182C)
#define MV_WIN_PCIE_MAX			6

#define MV_PCIE_BAR_CTRL(n)		(0x04 * (n) + 0x1800)
#define MV_PCIE_BAR_BASE(n)		(0x08 * ((n) < 3 ? (n) : 4) + 0x0010)
#define MV_PCIE_BAR_BASE_H(n)		(0x08 * (n) + 0x0014)
#define MV_PCIE_BAR_MAX			4
#define MV_PCIE_BAR_64BIT		(0x4)
#define MV_PCIE_BAR_PREFETCH_EN		(0x8)

#define MV_PCIE_CONTROL			(0x1a00)
#define MV_PCIE_ROOT_CMPLX		(1 << 1)

#define	MV_WIN_SATA_CTRL_ARMADA38X(n)	(0x10 * (n) + 0x60)
#define	MV_WIN_SATA_BASE_ARMADA38X(n)	(0x10 * (n) + 0x64)
#define	MV_WIN_SATA_SIZE_ARMADA38X(n)	(0x10 * (n) + 0x68)
#define	MV_WIN_SATA_MAX_ARMADA38X	4
#define	MV_WIN_SATA_CTRL(n)		(0x10 * (n) + 0x30)
#define	MV_WIN_SATA_BASE(n)		(0x10 * (n) + 0x34)
#define	MV_WIN_SATA_MAX			4

#define	MV_WIN_SDHCI_CTRL(n)		(0x8 * (n) + 0x4080)
#define	MV_WIN_SDHCI_BASE(n)		(0x8 * (n) + 0x4084)
#define	MV_WIN_SDHCI_MAX		8

#define	MV_BOOTROM_MEM_ADDR	0xFFF00000
#define	MV_BOOTROM_WIN_SIZE	0xF
#define	MV_CPU_SUBSYS_REGS_LEN	0x100

#define	IO_WIN_9_CTRL_OFFSET	0x98
#define	IO_WIN_9_BASE_OFFSET	0x9C

/* Mbus decoding unit IDs and attributes */
#define	MBUS_BOOTROM_TGT_ID	0x1
#define	MBUS_BOOTROM_ATTR	0x1D

/* Internal Units Sync Barrier Control Register */
#define	MV_SYNC_BARRIER_CTRL		0x84
#define	MV_SYNC_BARRIER_CTRL_ALL	0xFFFF

/* IO Window Control Register fields */
#define	IO_WIN_SIZE_SHIFT	16
#define	IO_WIN_SIZE_MASK	0xFFFF
#define	IO_WIN_COH_ATTR_MASK	(0xF << 12)
#define	IO_WIN_ATTR_SHIFT	8
#define	IO_WIN_ATTR_MASK	0xFF
#define	IO_WIN_TGT_SHIFT	4
#define	IO_WIN_TGT_MASK		0xF
#define	IO_WIN_SYNC_SHIFT	1
#define	IO_WIN_SYNC_MASK	0x1
#define	IO_WIN_ENA_SHIFT	0
#define	IO_WIN_ENA_MASK		0x1

#define WIN_REG_IDX_RD(pre,reg,off,base)					\
	static __inline uint32_t						\
	pre ## _ ## reg ## _read(int i)						\
	{									\
		return (bus_space_read_4(fdtbus_bs_tag, base, off(i)));		\
	}

#define WIN_REG_IDX_RD2(pre,reg,off,base)					\
	static  __inline uint32_t						\
	pre ## _ ## reg ## _read(int i, int j)					\
	{									\
		return (bus_space_read_4(fdtbus_bs_tag, base, off(i, j)));		\
	}									\

#define WIN_REG_BASE_IDX_RD(pre,reg,off)					\
	static __inline uint32_t						\
	pre ## _ ## reg ## _read(uint32_t base, int i)				\
	{									\
		return (bus_space_read_4(fdtbus_bs_tag, base, off(i)));		\
	}

#define WIN_REG_BASE_IDX_RD2(pre,reg,off)					\
	static __inline uint32_t						\
	pre ## _ ## reg ## _read(uint32_t base, int i, int j)				\
	{									\
		return (bus_space_read_4(fdtbus_bs_tag, base, off(i, j)));		\
	}

#define WIN_REG_IDX_WR(pre,reg,off,base)					\
	static __inline void							\
	pre ## _ ## reg ## _write(int i, uint32_t val)				\
	{									\
		bus_space_write_4(fdtbus_bs_tag, base, off(i), val);			\
	}

#define WIN_REG_IDX_WR2(pre,reg,off,base)					\
	static __inline void							\
	pre ## _ ## reg ## _write(int i, int j, uint32_t val)			\
	{									\
		bus_space_write_4(fdtbus_bs_tag, base, off(i, j), val);		\
	}

#define WIN_REG_BASE_IDX_WR(pre,reg,off)					\
	static __inline void							\
	pre ## _ ## reg ## _write(uint32_t base, int i, uint32_t val)		\
	{									\
		bus_space_write_4(fdtbus_bs_tag, base, off(i), val);			\
	}

#define WIN_REG_BASE_IDX_WR2(pre,reg,off)					\
	static __inline void							\
	pre ## _ ## reg ## _write(uint32_t base, int i, int j, uint32_t val)		\
	{									\
		bus_space_write_4(fdtbus_bs_tag, base, off(i, j), val);			\
	}

#define WIN_REG_RD(pre,reg,off,base)						\
	static __inline uint32_t						\
	pre ## _ ## reg ## _read(void)						\
	{									\
		return (bus_space_read_4(fdtbus_bs_tag, base, off));			\
	}

#define WIN_REG_BASE_RD(pre,reg,off)						\
	static __inline uint32_t						\
	pre ## _ ## reg ## _read(uint32_t base)					\
	{									\
		return (bus_space_read_4(fdtbus_bs_tag, base, off));			\
	}

#define WIN_REG_WR(pre,reg,off,base)						\
	static __inline void							\
	pre ## _ ## reg ## _write(uint32_t val)					\
	{									\
		bus_space_write_4(fdtbus_bs_tag, base, off, val);			\
	}

#define WIN_REG_BASE_WR(pre,reg,off)						\
	static __inline void							\
	pre ## _ ## reg ## _write(uint32_t base, uint32_t val)			\
	{									\
		bus_space_write_4(fdtbus_bs_tag, base, off, val);			\
	}

#endif /* _MVWIN_H_ */
