#ifndef _ASM_X86_SEGMENT_H_
#define _ASM_X86_SEGMENT_H_

#ifdef CONFIG_X86_32
# include "segment_32.h"
#else
# include "segment_64.h"
#endif

#ifndef CONFIG_PARAVIRT
#define get_kernel_rpl()  0
#endif

/* User mode is privilege level 3 */
#define USER_RPL		0x3
/* LDT segment has TI set, GDT has it cleared */
#define SEGMENT_LDT		0x4
#define SEGMENT_GDT		0x0

/* Bottom two bits of selector give the ring privilege level */
#define SEGMENT_RPL_MASK	0x3
/* Bit 2 is table indicator (LDT/GDT) */
#define SEGMENT_TI_MASK		0x4

#define IDT_ENTRIES 256
#define GDT_SIZE (GDT_ENTRIES * 8)
#define GDT_ENTRY_TLS_ENTRIES 3
#define TLS_SIZE (GDT_ENTRY_TLS_ENTRIES * 8)

#endif
