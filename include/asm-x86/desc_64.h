/* Written 2000 by Andi Kleen */
#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <linux/threads.h>
#include <asm/ldt.h>

#ifndef __ASSEMBLY__

#include <linux/string.h>
#include <linux/smp.h>

#include <asm/segment.h>

extern struct desc_struct cpu_gdt_table[GDT_ENTRIES];

#define load_TR_desc() asm volatile("ltr %w0"::"r" (GDT_ENTRY_TSS*8))
#define load_LDT_desc() asm volatile("lldt %w0"::"r" (GDT_ENTRY_LDT*8))

static inline unsigned long __store_tr(void)
{
       unsigned long tr;

       asm volatile ("str %w0":"=r" (tr));
       return tr;
}

#define store_tr(tr) (tr) = __store_tr()

extern struct desc_ptr cpu_gdt_descr[];

static inline void write_ldt_entry(struct desc_struct *ldt,
				   int entry, void *ptr)
{
	memcpy(&ldt[entry], ptr, 8);
}

/* the cpu gdt accessor */
#define get_cpu_gdt_table(x) ((struct desc_struct *)cpu_gdt_descr[x].address)

static inline void load_gdt(const struct desc_ptr *ptr)
{
	asm volatile("lgdt %w0"::"m" (*ptr));
}

static inline void store_gdt(struct desc_ptr *ptr)
{
       asm("sgdt %w0":"=m" (*ptr));
}

static inline void _set_gate(void *adr, unsigned type, unsigned long func,
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
	memcpy(adr, &s, 16);
}

static inline void set_intr_gate(int nr, void *func)
{
	BUG_ON((unsigned)nr > 0xFF);
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 0, 0);
}

static inline void set_intr_gate_ist(int nr, void *func, unsigned ist)
{
	BUG_ON((unsigned)nr > 0xFF);
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 0, ist);
}

static inline void set_system_gate(int nr, void *func)
{
	BUG_ON((unsigned)nr > 0xFF);
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 3, 0);
}

static inline void set_system_gate_ist(int nr, void *func, unsigned ist)
{
	_set_gate(&idt_table[nr], GATE_INTERRUPT, (unsigned long) func, 3, ist);
}

static inline void load_idt(const struct desc_ptr *ptr)
{
	asm volatile("lidt %w0"::"m" (*ptr));
}

static inline void store_idt(struct desc_ptr *dtr)
{
       asm("sidt %w0":"=m" (*dtr));
}

static inline void set_tssldt_descriptor(void *ptr, unsigned long tss,
					 unsigned type, unsigned size)
{
	struct ldttss_desc64 d;

	memset(&d, 0, sizeof(d));
	d.limit0 = size & 0xFFFF;
	d.base0 = PTR_LOW(tss);
	d.base1 = PTR_MIDDLE(tss) & 0xFF;
	d.type = type;
	d.p = 1;
	d.limit1 = (size >> 16) & 0xF;
	d.base2 = (PTR_MIDDLE(tss) >> 8) & 0xFF;
	d.base3 = PTR_HIGH(tss);
	memcpy(ptr, &d, 16);
}

static inline void set_tss_desc(unsigned cpu, void *addr)
{
	/*
	 * sizeof(unsigned long) coming from an extra "long" at the end
	 * of the iobitmap. See tss_struct definition in processor.h
	 *
	 * -1? seg base+limit should be pointing to the address of the
	 * last valid byte
	 */
	set_tssldt_descriptor(&get_cpu_gdt_table(cpu)[GDT_ENTRY_TSS],
		(unsigned long)addr, DESC_TSS,
		IO_BITMAP_OFFSET + IO_BITMAP_BYTES + sizeof(unsigned long) - 1);
}

static inline void set_ldt(void *addr, int entries)
{
	if (likely(entries == 0))
		__asm__ __volatile__("lldt %w0"::"q" (0));
	else {
		unsigned cpu = smp_processor_id();

		set_tssldt_descriptor(&get_cpu_gdt_table(cpu)[GDT_ENTRY_LDT],
			     (unsigned long)addr, DESC_LDT, entries * 8 - 1);
		__asm__ __volatile__("lldt %w0"::"q" (GDT_ENTRY_LDT*8));
	}
}

static inline void load_TLS(struct thread_struct *t, unsigned int cpu)
{
	unsigned int i;
	struct desc_struct *gdt = (get_cpu_gdt_table(cpu) + GDT_ENTRY_TLS_MIN);

	for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++)
		gdt[i] = t->tls_array[i];
}

#endif /* !__ASSEMBLY__ */

#endif
