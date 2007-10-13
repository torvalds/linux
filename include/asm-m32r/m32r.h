#ifndef _ASM_M32R_M32R_H_
#define _ASM_M32R_M32R_H_

/*
 * Renesas M32R processor
 *
 * Copyright (C) 2003, 2004  Renesas Technology Corp.
 */


/* Chip type */
#if defined(CONFIG_CHIP_XNUX_MP) || defined(CONFIG_CHIP_XNUX2_MP)
#include <asm/m32r_mp_fpga.h>
#elif defined(CONFIG_CHIP_VDEC2) || defined(CONFIG_CHIP_XNUX2) \
	|| defined(CONFIG_CHIP_M32700) || defined(CONFIG_CHIP_M32102) \
        || defined(CONFIG_CHIP_OPSP) || defined(CONFIG_CHIP_M32104)
#include <asm/m32102.h>
#endif

/* Platform type */
#if defined(CONFIG_PLAT_M32700UT)
#include <asm/m32700ut/m32700ut_pld.h>
#include <asm/m32700ut/m32700ut_lan.h>
#include <asm/m32700ut/m32700ut_lcd.h>
/* for ei_handler:linux/arch/m32r/kernel/entry.S */
#define M32R_INT1ICU_ISTS	PLD_ICUISTS
#define M32R_INT1ICU_IRQ_BASE	M32700UT_PLD_IRQ_BASE
#define M32R_INT0ICU_ISTS	M32700UT_LAN_ICUISTS
#define M32R_INT0ICU_IRQ_BASE	M32700UT_LAN_PLD_IRQ_BASE
#define M32R_INT2ICU_ISTS	M32700UT_LCD_ICUISTS
#define M32R_INT2ICU_IRQ_BASE	M32700UT_LCD_PLD_IRQ_BASE
#endif  /* CONFIG_PLAT_M32700UT */

#if defined(CONFIG_PLAT_OPSPUT)
#include <asm/opsput/opsput_pld.h>
#include <asm/opsput/opsput_lan.h>
#include <asm/opsput/opsput_lcd.h>
/* for ei_handler:linux/arch/m32r/kernel/entry.S */
#define M32R_INT1ICU_ISTS	PLD_ICUISTS
#define M32R_INT1ICU_IRQ_BASE	OPSPUT_PLD_IRQ_BASE
#define M32R_INT0ICU_ISTS	OPSPUT_LAN_ICUISTS
#define M32R_INT0ICU_IRQ_BASE	OPSPUT_LAN_PLD_IRQ_BASE
#define M32R_INT2ICU_ISTS	OPSPUT_LCD_ICUISTS
#define M32R_INT2ICU_IRQ_BASE	OPSPUT_LCD_PLD_IRQ_BASE
#endif  /* CONFIG_PLAT_OPSPUT */

#if defined(CONFIG_PLAT_MAPPI2)
#include <asm/mappi2/mappi2_pld.h>
#endif	/* CONFIG_PLAT_MAPPI2 */

#if defined(CONFIG_PLAT_MAPPI3)
#include <asm/mappi3/mappi3_pld.h>
#endif	/* CONFIG_PLAT_MAPPI3 */

#if defined(CONFIG_PLAT_USRV)
#include <asm/m32700ut/m32700ut_pld.h>
/* for ei_handler:linux/arch/m32r/kernel/entry.S */
#define M32R_INT1ICU_ISTS	PLD_ICUISTS
#define M32R_INT1ICU_IRQ_BASE	M32700UT_PLD_IRQ_BASE
#endif

#if defined(CONFIG_PLAT_M32104UT)
#include <asm/m32104ut/m32104ut_pld.h>
/* for ei_handler:linux/arch/m32r/kernel/entry.S */
#define M32R_INT1ICU_ISTS	PLD_ICUISTS
#define M32R_INT1ICU_IRQ_BASE	M32104UT_PLD_IRQ_BASE
#endif  /* CONFIG_PLAT_M32104 */

/*
 * M32R Register
 */

/*
 * MMU Register
 */

#define MMU_REG_BASE	(0xffff0000)
#define ITLB_BASE	(0xfe000000)
#define DTLB_BASE	(0xfe000800)

#define NR_TLB_ENTRIES	CONFIG_TLB_ENTRIES

#define MATM	MMU_REG_BASE		/* MMU Address Translation Mode
					   Register */
#define MPSZ	(0x04 + MMU_REG_BASE)	/* MMU Page Size Designation Register */
#define MASID	(0x08 + MMU_REG_BASE)	/* MMU Address Space ID Register */
#define MESTS	(0x0c + MMU_REG_BASE)	/* MMU Exception Status Register */
#define MDEVA	(0x10 + MMU_REG_BASE)	/* MMU Operand Exception Virtual
					   Address Register */
#define MDEVP	(0x14 + MMU_REG_BASE)	/* MMU Operand Exception Virtual Page
					   Number Register */
#define MPTB	(0x18 + MMU_REG_BASE)	/* MMU Page Table Base Register */
#define MSVA	(0x20 + MMU_REG_BASE)	/* MMU Search Virtual Address
					   Register */
#define MTOP	(0x24 + MMU_REG_BASE)	/* MMU TLB Operation Register */
#define MIDXI	(0x28 + MMU_REG_BASE)	/* MMU Index Register for
					   Instruciton */
#define MIDXD	(0x2c + MMU_REG_BASE)	/* MMU Index Register for Operand */

#define MATM_offset	(MATM - MMU_REG_BASE)
#define MPSZ_offset	(MPSZ - MMU_REG_BASE)
#define MASID_offset	(MASID - MMU_REG_BASE)
#define MESTS_offset	(MESTS - MMU_REG_BASE)
#define MDEVA_offset	(MDEVA - MMU_REG_BASE)
#define MDEVP_offset	(MDEVP - MMU_REG_BASE)
#define MPTB_offset	(MPTB - MMU_REG_BASE)
#define MSVA_offset	(MSVA - MMU_REG_BASE)
#define MTOP_offset	(MTOP - MMU_REG_BASE)
#define MIDXI_offset	(MIDXI - MMU_REG_BASE)
#define MIDXD_offset	(MIDXD - MMU_REG_BASE)

#define MESTS_IT	(1 << 0)	/* Instruction TLB miss */
#define MESTS_IA	(1 << 1)	/* Instruction Access Exception */
#define MESTS_DT	(1 << 4)	/* Operand TLB miss */
#define MESTS_DA	(1 << 5)	/* Operand Access Exception */
#define MESTS_DRW	(1 << 6)	/* Operand Write Exception Flag */

/*
 * PSW (Processor Status Word)
 */

/* PSW bit */
#define M32R_PSW_BIT_SM   (7)    /* Stack Mode */
#define M32R_PSW_BIT_IE   (6)    /* Interrupt Enable */
#define M32R_PSW_BIT_PM   (3)    /* Processor Mode [0:Supervisor,1:User] */
#define M32R_PSW_BIT_C    (0)    /* Condition */
#define M32R_PSW_BIT_BSM  (7+8)  /* Backup Stack Mode */
#define M32R_PSW_BIT_BIE  (6+8)  /* Backup Interrupt Enable */
#define M32R_PSW_BIT_BPM  (3+8)  /* Backup Processor Mode */
#define M32R_PSW_BIT_BC   (0+8)  /* Backup Condition */

/* PSW bit map */
#define M32R_PSW_SM   (1UL<< M32R_PSW_BIT_SM)   /* Stack Mode */
#define M32R_PSW_IE   (1UL<< M32R_PSW_BIT_IE)   /* Interrupt Enable */
#define M32R_PSW_PM   (1UL<< M32R_PSW_BIT_PM)   /* Processor Mode */
#define M32R_PSW_C    (1UL<< M32R_PSW_BIT_C)    /* Condition */
#define M32R_PSW_BSM  (1UL<< M32R_PSW_BIT_BSM)  /* Backup Stack Mode */
#define M32R_PSW_BIE  (1UL<< M32R_PSW_BIT_BIE)  /* Backup Interrupt Enable */
#define M32R_PSW_BPM  (1UL<< M32R_PSW_BIT_BPM)  /* Backup Processor Mode */
#define M32R_PSW_BC   (1UL<< M32R_PSW_BIT_BC)   /* Backup Condition */

/*
 * Direct address to SFR
 */

#include <asm/page.h>
#ifdef CONFIG_MMU
#define NONCACHE_OFFSET  (__PAGE_OFFSET + 0x20000000)
#else
#define NONCACHE_OFFSET  __PAGE_OFFSET
#endif /* CONFIG_MMU */

#define M32R_ICU_ISTS_ADDR  M32R_ICU_ISTS_PORTL+NONCACHE_OFFSET
#define M32R_ICU_IPICR_ADDR  M32R_ICU_IPICR0_PORTL+NONCACHE_OFFSET
#define M32R_ICU_IMASK_ADDR  M32R_ICU_IMASK_PORTL+NONCACHE_OFFSET
#define M32R_FPGA_CPU_NAME_ADDR  M32R_FPGA_CPU_NAME0_PORTL+NONCACHE_OFFSET
#define M32R_FPGA_MODEL_ID_ADDR  M32R_FPGA_MODEL_ID0_PORTL+NONCACHE_OFFSET
#define M32R_FPGA_VERSION_ADDR   M32R_FPGA_VERSION0_PORTL+NONCACHE_OFFSET

#endif /* _ASM_M32R_M32R_H_ */
