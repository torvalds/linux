#ifndef __ASM_SH_SE7206_H
#define __ASM_SH_SE7206_H

#define PA_SMSC		0x30000000
#define PA_MRSHPC	0x34000000
#define PA_LED		0x31400000

void init_se7206_IRQ(void);

#define __IO_PREFIX	se7206
#include <asm/io_generic.h>

#endif /* __ASM_SH_SE7206_H */
