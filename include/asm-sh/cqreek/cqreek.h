#ifndef __ASM_SH_CQREEK_CQREEK_H
#define __ASM_SH_CQREEK_CQREEK_H

#define BRIDGE_FEATURE		0x0002

#define BRIDGE_IDE_CTRL		0x0018
#define BRIDGE_IDE_INTR_LVL    	0x001A
#define BRIDGE_IDE_INTR_MASK	0x001C
#define BRIDGE_IDE_INTR_STAT	0x001E

#define BRIDGE_ISA_CTRL		0x0028
#define BRIDGE_ISA_INTR_LVL    	0x002A
#define BRIDGE_ISA_INTR_MASK	0x002C
#define BRIDGE_ISA_INTR_STAT	0x002E

/* arch/sh/boards/cqreek/setup.c */
extern void setup_cqreek(void);

/* arch/sh/boards/cqreek/irq.c */
extern int cqreek_has_ide, cqreek_has_isa;
extern void init_cqreek_IRQ(void);

/* arch/sh/boards/cqreek/io.c */
extern unsigned long cqreek_port2addr(unsigned long port);

#endif /* __ASM_SH_CQREEK_CQREEK_H */

