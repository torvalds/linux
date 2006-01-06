#ifdef __KERNEL__
#ifndef _ASM_M32R_IRQ_H
#define _ASM_M32R_IRQ_H

#include <linux/config.h>

#if defined(CONFIG_PLAT_M32700UT_Alpha) || defined(CONFIG_PLAT_USRV)
/*
 * IRQ definitions for M32700UT
 *  M32700 Chip: 64 interrupts
 *  ICU of M32700UT-on-board PLD: 32 interrupts cascaded to INT1# chip pin
 */
#define	M32700UT_NUM_CPU_IRQ	(64)
#define M32700UT_NUM_PLD_IRQ	(32)
#define M32700UT_IRQ_BASE	0
#define M32700UT_CPU_IRQ_BASE	M32700UT_IRQ_BASE
#define M32700UT_PLD_IRQ_BASE	(M32700UT_CPU_IRQ_BASE + M32700UT_NUM_CPU_IRQ)

#define NR_IRQS	(M32700UT_NUM_CPU_IRQ + M32700UT_NUM_PLD_IRQ)
#elif defined(CONFIG_PLAT_M32700UT)
/*
 * IRQ definitions for M32700UT(Rev.C) + M32R-LAN
 *  M32700 Chip: 64 interrupts
 *  ICU of M32700UT-on-board PLD: 32 interrupts cascaded to INT1# chip pin
 *  ICU of M32R-LCD-on-board PLD: 32 interrupts cascaded to INT2# chip pin
 *  ICU of M32R-LAN-on-board PLD: 32 interrupts cascaded to INT0# chip pin
 */
#define M32700UT_NUM_CPU_IRQ		(64)
#define M32700UT_NUM_PLD_IRQ		(32)
#define M32700UT_NUM_LCD_PLD_IRQ	(32)
#define M32700UT_NUM_LAN_PLD_IRQ	(32)
#define M32700UT_IRQ_BASE		0
#define M32700UT_CPU_IRQ_BASE		(M32700UT_IRQ_BASE)
#define M32700UT_PLD_IRQ_BASE \
	(M32700UT_CPU_IRQ_BASE + M32700UT_NUM_CPU_IRQ)
#define M32700UT_LCD_PLD_IRQ_BASE \
	(M32700UT_PLD_IRQ_BASE + M32700UT_NUM_PLD_IRQ)
#define M32700UT_LAN_PLD_IRQ_BASE \
	(M32700UT_LCD_PLD_IRQ_BASE + M32700UT_NUM_LCD_PLD_IRQ)

#define NR_IRQS \
	(M32700UT_NUM_CPU_IRQ + M32700UT_NUM_PLD_IRQ \
	+ M32700UT_NUM_LCD_PLD_IRQ + M32700UT_NUM_LAN_PLD_IRQ)
#elif defined(CONFIG_PLAT_OPSPUT)
/*
 * IRQ definitions for OPSPUT + M32R-LAN
 *  OPSP Chip: 64 interrupts
 *  ICU of OPSPUT-on-board PLD: 32 interrupts cascaded to INT1# chip pin
 *  ICU of M32R-LCD-on-board PLD: 32 interrupts cascaded to INT2# chip pin
 *  ICU of M32R-LAN-on-board PLD: 32 interrupts cascaded to INT0# chip pin
 */
#define OPSPUT_NUM_CPU_IRQ		(64)
#define OPSPUT_NUM_PLD_IRQ		(32)
#define OPSPUT_NUM_LCD_PLD_IRQ	(32)
#define OPSPUT_NUM_LAN_PLD_IRQ	(32)
#define OPSPUT_IRQ_BASE		0
#define OPSPUT_CPU_IRQ_BASE		(OPSPUT_IRQ_BASE)
#define OPSPUT_PLD_IRQ_BASE \
	(OPSPUT_CPU_IRQ_BASE + OPSPUT_NUM_CPU_IRQ)
#define OPSPUT_LCD_PLD_IRQ_BASE \
	(OPSPUT_PLD_IRQ_BASE + OPSPUT_NUM_PLD_IRQ)
#define OPSPUT_LAN_PLD_IRQ_BASE \
	(OPSPUT_LCD_PLD_IRQ_BASE + OPSPUT_NUM_LCD_PLD_IRQ)

#define NR_IRQS \
	(OPSPUT_NUM_CPU_IRQ + OPSPUT_NUM_PLD_IRQ \
	+ OPSPUT_NUM_LCD_PLD_IRQ + OPSPUT_NUM_LAN_PLD_IRQ)

#elif defined(CONFIG_PLAT_M32104UT)
/*
 * IRQ definitions for M32104UT
 *  M32104 Chip: 64 interrupts
 *  ICU of M32104UT-on-board PLD: 32 interrupts cascaded to INT1# chip pin
 */
#define	M32104UT_NUM_CPU_IRQ	(64)
#define M32104UT_NUM_PLD_IRQ	(32)
#define M32104UT_IRQ_BASE	0
#define M32104UT_CPU_IRQ_BASE	M32104UT_IRQ_BASE
#define M32104UT_PLD_IRQ_BASE	(M32104UT_CPU_IRQ_BASE + M32104UT_NUM_CPU_IRQ)

#define NR_IRQS	\
    (M32104UT_NUM_CPU_IRQ + M32104UT_NUM_PLD_IRQ)

#else
#define NR_IRQS	64
#endif

#define irq_canonicalize(irq)	(irq)

#endif /* _ASM_M32R_IRQ_H */
#endif /* __KERNEL__ */
