#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <asm/ldt.h>
#include <asm/segment.h>
#include <asm/desc_defs.h>

#ifndef __ASSEMBLY__

#include <linux/preempt.h>
#include <linux/percpu.h>

extern void set_intr_gate(unsigned int irq, void * addr);

static inline void pack_gate(gate_desc *gate,
	unsigned long base, unsigned short seg, unsigned char type, unsigned char flags)
{
	gate->a = (seg << 16) | (base & 0xffff);
	gate->b = (base & 0xffff0000) | ((type & 0xff) << 8) | (flags & 0xff);
}

static inline void _set_gate(int gate, unsigned int type, void *addr, unsigned short seg)
{
	gate_desc g;
	pack_gate(&g, (unsigned long)addr, seg, type, 0);
	write_idt_entry(idt_table, gate, &g);
}

static inline void __set_tss_desc(unsigned int cpu, unsigned int entry, const void *addr)
{
	tss_desc tss;
	pack_descriptor(&tss, (unsigned long)addr,
			offsetof(struct tss_struct, __cacheline_filler) - 1,
			DESC_TSS, 0);
	write_gdt_entry(get_cpu_gdt_table(cpu), entry, &tss, DESC_TSS);
}


#define set_tss_desc(cpu,addr) __set_tss_desc(cpu, GDT_ENTRY_TSS, addr)

#endif /* !__ASSEMBLY__ */

#endif
