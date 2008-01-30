/* Written 2000 by Andi Kleen */
#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <linux/threads.h>
#include <asm/ldt.h>

#ifndef __ASSEMBLY__

#include <linux/string.h>

#include <asm/segment.h>

static inline void _set_gate(int gate, unsigned type, unsigned long func,
			     unsigned dpl, unsigned ist)
{
	gate_desc s;

	s.offset_low = PTR_LOW(func);
	s.segment = __KERNEL_CS;
	s.ist = ist;
	s.p = 1;
	s.dpl = dpl;
	s.zero0 = 0;
	s.zero1 = 0;
	s.type = type;
	s.offset_middle = PTR_MIDDLE(func);
	s.offset_high = PTR_HIGH(func);
	/*
	 * does not need to be atomic because it is only done once at
	 * setup time
	 */
	write_idt_entry(idt_table, gate, &s);
}

static inline void set_intr_gate(int nr, void *func)
{
	BUG_ON((unsigned)nr > 0xFF);
	_set_gate(nr, GATE_INTERRUPT, (unsigned long) func, 0, 0);
}

static inline void set_intr_gate_ist(int nr, void *func, unsigned ist)
{
	BUG_ON((unsigned)nr > 0xFF);
	_set_gate(nr, GATE_INTERRUPT, (unsigned long) func, 0, ist);
}

static inline void set_system_gate(int nr, void *func)
{
	BUG_ON((unsigned)nr > 0xFF);
	_set_gate(nr, GATE_INTERRUPT, (unsigned long) func, 3, 0);
}

static inline void set_system_gate_ist(int nr, void *func, unsigned ist)
{
	_set_gate(nr, GATE_INTERRUPT, (unsigned long) func, 3, ist);
}

static inline void set_tss_desc(unsigned cpu, void *addr)
{
	struct desc_struct *d = get_cpu_gdt_table(cpu);
	tss_desc tss;

	/*
	 * sizeof(unsigned long) coming from an extra "long" at the end
	 * of the iobitmap. See tss_struct definition in processor.h
	 *
	 * -1? seg base+limit should be pointing to the address of the
	 * last valid byte
	 */
	set_tssldt_descriptor(&tss,
		(unsigned long)addr, DESC_TSS,
		IO_BITMAP_OFFSET + IO_BITMAP_BYTES + sizeof(unsigned long) - 1);
	write_gdt_entry(d, GDT_ENTRY_TSS, &tss, DESC_TSS);
}

#endif /* !__ASSEMBLY__ */

#endif
