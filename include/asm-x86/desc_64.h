/* Written 2000 by Andi Kleen */ 
#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <linux/threads.h>
#include <asm/ldt.h>

#ifndef __ASSEMBLY__

#include <linux/string.h>
#include <linux/smp.h>
#include <asm/desc_defs.h>

#include <asm/segment.h>
#include <asm/mmu.h>

extern struct desc_struct cpu_gdt_table[GDT_ENTRIES];

#define load_TR_desc() asm volatile("ltr %w0"::"r" (GDT_ENTRY_TSS*8))
#define load_LDT_desc() asm volatile("lldt %w0"::"r" (GDT_ENTRY_LDT*8))
#define clear_LDT()  asm volatile("lldt %w0"::"r" (0))

static inline unsigned long __store_tr(void)
{
       unsigned long tr;

       asm volatile ("str %w0":"=r" (tr));
       return tr;
}

#define store_tr(tr) (tr) = __store_tr()

/*
 * This is the ldt that every process will get unless we need
 * something other than this.
 */
extern struct desc_struct default_ldt[];
extern struct gate_struct idt_table[]; 
extern struct desc_ptr cpu_gdt_descr[];

/* the cpu gdt accessor */
#define cpu_gdt(_cpu) ((struct desc_struct *)cpu_gdt_descr[_cpu].address)

static inline void load_gdt(const struct desc_ptr *ptr)
{
	asm volatile("lgdt %w0"::"m" (*ptr));
}

static inline void store_gdt(struct desc_ptr *ptr)
{
       asm("sgdt %w0":"=m" (*ptr));
}

static inline void _set_gate(void *adr, unsigned type, unsigned long func, unsigned dpl, unsigned ist)  
{
	struct gate_struct s; 	
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
	/* does not need to be atomic because it is only done once at setup time */ 
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

static inline void set_tssldt_descriptor(void *ptr, unsigned long tss, unsigned type, 
					 unsigned size) 
{ 
	struct ldttss_desc d;
	memset(&d,0,sizeof(d)); 
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
	set_tssldt_descriptor(&cpu_gdt(cpu)[GDT_ENTRY_TSS],
		(unsigned long)addr, DESC_TSS,
		IO_BITMAP_OFFSET + IO_BITMAP_BYTES + sizeof(unsigned long) - 1);
} 

static inline void set_ldt_desc(unsigned cpu, void *addr, int size)
{ 
	set_tssldt_descriptor(&cpu_gdt(cpu)[GDT_ENTRY_LDT], (unsigned long)addr,
			      DESC_LDT, size * 8 - 1);
}

#define LDT_entry_a(info) \
	((((info)->base_addr & 0x0000ffff) << 16) | ((info)->limit & 0x0ffff))
/* Don't allow setting of the lm bit. It is useless anyways because 
   64bit system calls require __USER_CS. */ 
#define LDT_entry_b(info) \
	(((info)->base_addr & 0xff000000) | \
	(((info)->base_addr & 0x00ff0000) >> 16) | \
	((info)->limit & 0xf0000) | \
	(((info)->read_exec_only ^ 1) << 9) | \
	((info)->contents << 10) | \
	(((info)->seg_not_present ^ 1) << 15) | \
	((info)->seg_32bit << 22) | \
	((info)->limit_in_pages << 23) | \
	((info)->useable << 20) | \
	/* ((info)->lm << 21) | */ \
	0x7000)

#define LDT_empty(info) (\
	(info)->base_addr	== 0	&& \
	(info)->limit		== 0	&& \
	(info)->contents	== 0	&& \
	(info)->read_exec_only	== 1	&& \
	(info)->seg_32bit	== 0	&& \
	(info)->limit_in_pages	== 0	&& \
	(info)->seg_not_present	== 1	&& \
	(info)->useable		== 0	&& \
	(info)->lm		== 0)

static inline void load_TLS(struct thread_struct *t, unsigned int cpu)
{
	unsigned int i;
	u64 *gdt = (u64 *)(cpu_gdt(cpu) + GDT_ENTRY_TLS_MIN);

	for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++)
		gdt[i] = t->tls_array[i];
} 

/*
 * load one particular LDT into the current CPU
 */
static inline void load_LDT_nolock (mm_context_t *pc, int cpu)
{
	int count = pc->size;

	if (likely(!count)) {
		clear_LDT();
		return;
	}
		
	set_ldt_desc(cpu, pc->ldt, count);
	load_LDT_desc();
}

static inline void load_LDT(mm_context_t *pc)
{
	int cpu = get_cpu();
	load_LDT_nolock(pc, cpu);
	put_cpu();
}

extern struct desc_ptr idt_descr;

#endif /* !__ASSEMBLY__ */

#endif
