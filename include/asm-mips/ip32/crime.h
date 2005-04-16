/*
 * Definitions for the SGI CRIME (CPU, Rendering, Interconnect and Memory
 * Engine)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 */

#ifndef __ASM_CRIME_H__
#define __ASM_CRIME_H__

/*
 * Address map
 */
#define CRIME_BASE	0x14000000	/* physical */

#undef BIT
#define BIT(x)	(1UL << (x))

struct sgi_crime {
	volatile unsigned long id;
#define CRIME_ID_MASK			0xff
#define CRIME_ID_IDBITS			0xf0
#define CRIME_ID_IDVALUE		0xa0
#define CRIME_ID_REV			0x0f
#define CRIME_REV_PETTY			0x00
#define CRIME_REV_11			0x11
#define CRIME_REV_13			0x13
#define CRIME_REV_14			0x14

	volatile unsigned long control;
#define CRIME_CONTROL_MASK		0x3fff
#define CRIME_CONTROL_TRITON_SYSADC	0x2000
#define CRIME_CONTROL_CRIME_SYSADC	0x1000
#define CRIME_CONTROL_HARD_RESET	0x0800
#define CRIME_CONTROL_SOFT_RESET	0x0400
#define CRIME_CONTROL_DOG_ENA		0x0200
#define CRIME_CONTROL_ENDIANESS		0x0100
#define CRIME_CONTROL_ENDIAN_BIG	0x0100
#define CRIME_CONTROL_ENDIAN_LITTLE	0x0000
#define CRIME_CONTROL_CQUEUE_HWM	0x000f
#define CRIME_CONTROL_CQUEUE_SHFT	0
#define CRIME_CONTROL_WBUF_HWM		0x00f0
#define CRIME_CONTROL_WBUF_SHFT		8

	volatile unsigned long istat;
	volatile unsigned long imask;
	volatile unsigned long soft_int;
	volatile unsigned long hard_int;
#define MACE_VID_IN1_INT		BIT(0)
#define MACE_VID_IN2_INT		BIT(1)
#define MACE_VID_OUT_INT		BIT(2)
#define MACE_ETHERNET_INT		BIT(3)
#define MACE_SUPERIO_INT		BIT(4)
#define MACE_MISC_INT			BIT(5)
#define MACE_AUDIO_INT			BIT(6)
#define MACE_PCI_BRIDGE_INT		BIT(7)
#define MACEPCI_SCSI0_INT		BIT(8)
#define MACEPCI_SCSI1_INT		BIT(9)
#define MACEPCI_SLOT0_INT		BIT(10)
#define MACEPCI_SLOT1_INT		BIT(11)
#define MACEPCI_SLOT2_INT		BIT(12)
#define MACEPCI_SHARED0_INT		BIT(13)
#define MACEPCI_SHARED1_INT		BIT(14)
#define MACEPCI_SHARED2_INT		BIT(15)
#define CRIME_GBE0_INT			BIT(16)
#define CRIME_GBE1_INT			BIT(17)
#define CRIME_GBE2_INT			BIT(18)
#define CRIME_GBE3_INT			BIT(19)
#define CRIME_CPUERR_INT		BIT(20)
#define CRIME_MEMERR_INT		BIT(21)
#define CRIME_RE_EMPTY_E_INT		BIT(22)
#define CRIME_RE_FULL_E_INT		BIT(23)
#define CRIME_RE_IDLE_E_INT		BIT(24)
#define CRIME_RE_EMPTY_L_INT		BIT(25)
#define CRIME_RE_FULL_L_INT		BIT(26)
#define CRIME_RE_IDLE_L_INT    		BIT(27)
#define CRIME_SOFT0_INT			BIT(28)
#define CRIME_SOFT1_INT			BIT(29)
#define CRIME_SOFT2_INT			BIT(30)
#define CRIME_SYSCORERR_INT		CRIME_SOFT2_INT
#define CRIME_VICE_INT			BIT(31)
/* Masks for deciding who handles the interrupt */
#define CRIME_MACE_INT_MASK		0x8f
#define CRIME_MACEISA_INT_MASK		0x70
#define CRIME_MACEPCI_INT_MASK		0xff00
#define CRIME_CRIME_INT_MASK		0xffff0000

	volatile unsigned long watchdog;
#define CRIME_DOG_POWER_ON_RESET	0x00010000
#define CRIME_DOG_WARM_RESET		0x00080000
#define CRIME_DOG_TIMEOUT		(CRIME_DOG_POWER_ON_RESET|CRIME_DOG_WARM_RESET)
#define CRIME_DOG_VALUE			0x00007fff

	volatile unsigned long timer;
#define CRIME_MASTER_FREQ		66666500	/* Crime upcounter frequency */
#define CRIME_NS_PER_TICK		15		/* for delay_calibrate */

	volatile unsigned long cpu_error_addr;
#define CRIME_CPU_ERROR_ADDR_MASK	0x3ffffffff

	volatile unsigned long cpu_error_stat;
#define CRIME_CPU_ERROR_MASK		0x7		/* cpu error stat is 3 bits */
#define CRIME_CPU_ERROR_CPU_ILL_ADDR	0x4
#define CRIME_CPU_ERROR_VICE_WRT_PRTY	0x2
#define CRIME_CPU_ERROR_CPU_WRT_PRTY	0x1

	unsigned long _pad0[54];

	volatile unsigned long mc_ctrl;
	volatile unsigned long bank_ctrl[8];
#define CRIME_MEM_BANK_CONTROL_MASK		0x11f	/* 9 bits 7:5 reserved */
#define CRIME_MEM_BANK_CONTROL_ADDR		0x01f
#define CRIME_MEM_BANK_CONTROL_SDRAM_SIZE	0x100
#define CRIME_MAXBANKS				8

	volatile unsigned long mem_ref_counter;
#define CRIME_MEM_REF_COUNTER_MASK	0x3ff		/* 10bit */

	volatile unsigned long mem_error_stat;
#define CRIME_MEM_ERROR_STAT_MASK       0x0ff7ffff	/* 28-bit register */
#define CRIME_MEM_ERROR_MACE_ID		0x0000007f
#define CRIME_MEM_ERROR_MACE_ACCESS	0x00000080
#define CRIME_MEM_ERROR_RE_ID		0x00007f00
#define CRIME_MEM_ERROR_RE_ACCESS	0x00008000
#define CRIME_MEM_ERROR_GBE_ACCESS	0x00010000
#define CRIME_MEM_ERROR_VICE_ACCESS	0x00020000
#define CRIME_MEM_ERROR_CPU_ACCESS	0x00040000
#define CRIME_MEM_ERROR_RESERVED	0x00080000
#define CRIME_MEM_ERROR_SOFT_ERR	0x00100000
#define CRIME_MEM_ERROR_HARD_ERR	0x00200000
#define CRIME_MEM_ERROR_MULTIPLE	0x00400000
#define CRIME_MEM_ERROR_ECC		0x01800000
#define CRIME_MEM_ERROR_MEM_ECC_RD	0x00800000
#define CRIME_MEM_ERROR_MEM_ECC_RMW	0x01000000
#define CRIME_MEM_ERROR_INV		0x0e000000
#define CRIME_MEM_ERROR_INV_MEM_ADDR_RD	0x02000000
#define CRIME_MEM_ERROR_INV_MEM_ADDR_WR	0x04000000
#define CRIME_MEM_ERROR_INV_MEM_ADDR_RMW 0x08000000

	volatile unsigned long mem_error_addr;
#define CRIME_MEM_ERROR_ADDR_MASK	0x3fffffff

	volatile unsigned long mem_ecc_syn;
#define CRIME_MEM_ERROR_ECC_SYN_MASK	0xffffffff

	volatile unsigned long mem_ecc_chk;
#define CRIME_MEM_ERROR_ECC_CHK_MASK	0xffffffff

	volatile unsigned long mem_ecc_repl;
#define CRIME_MEM_ERROR_ECC_REPL_MASK	0xffffffff
};

extern struct sgi_crime *crime;

#define CRIME_HI_MEM_BASE	0x40000000	/* this is where whole 1G of RAM is mapped */

#endif /* __ASM_CRIME_H__ */
