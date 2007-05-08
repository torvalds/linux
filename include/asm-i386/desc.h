#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <asm/ldt.h>
#include <asm/segment.h>

#ifndef __ASSEMBLY__

#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/percpu.h>

#include <asm/mmu.h>

struct Xgt_desc_struct {
	unsigned short size;
	unsigned long address __attribute__((packed));
	unsigned short pad;
} __attribute__ ((packed));

struct gdt_page
{
	struct desc_struct gdt[GDT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));
DECLARE_PER_CPU(struct gdt_page, gdt_page);

static inline struct desc_struct *get_cpu_gdt_table(unsigned int cpu)
{
	return per_cpu(gdt_page, cpu).gdt;
}

extern struct Xgt_desc_struct idt_descr;
extern struct desc_struct idt_table[];
extern void set_intr_gate(unsigned int irq, void * addr);

static inline void pack_descriptor(__u32 *a, __u32 *b,
	unsigned long base, unsigned long limit, unsigned char type, unsigned char flags)
{
	*a = ((base & 0xffff) << 16) | (limit & 0xffff);
	*b = (base & 0xff000000) | ((base & 0xff0000) >> 16) |
		(limit & 0x000f0000) | ((type & 0xff) << 8) | ((flags & 0xf) << 20);
}

static inline void pack_gate(__u32 *a, __u32 *b,
	unsigned long base, unsigned short seg, unsigned char type, unsigned char flags)
{
	*a = (seg << 16) | (base & 0xffff);
	*b = (base & 0xffff0000) | ((type & 0xff) << 8) | (flags & 0xff);
}

#define DESCTYPE_LDT 	0x82	/* present, system, DPL-0, LDT */
#define DESCTYPE_TSS 	0x89	/* present, system, DPL-0, 32-bit TSS */
#define DESCTYPE_TASK	0x85	/* present, system, DPL-0, task gate */
#define DESCTYPE_INT	0x8e	/* present, system, DPL-0, interrupt gate */
#define DESCTYPE_TRAP	0x8f	/* present, system, DPL-0, trap gate */
#define DESCTYPE_DPL3	0x60	/* DPL-3 */
#define DESCTYPE_S	0x10	/* !system */

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define load_TR_desc() native_load_tr_desc()
#define load_gdt(dtr) native_load_gdt(dtr)
#define load_idt(dtr) native_load_idt(dtr)
#define load_tr(tr) __asm__ __volatile("ltr %0"::"m" (tr))
#define load_ldt(ldt) __asm__ __volatile("lldt %0"::"m" (ldt))

#define store_gdt(dtr) native_store_gdt(dtr)
#define store_idt(dtr) native_store_idt(dtr)
#define store_tr(tr) (tr = native_store_tr())
#define store_ldt(ldt) __asm__ ("sldt %0":"=m" (ldt))

#define load_TLS(t, cpu) native_load_tls(t, cpu)
#define set_ldt native_set_ldt

#define write_ldt_entry(dt, entry, a, b) write_dt_entry(dt, entry, a, b)
#define write_gdt_entry(dt, entry, a, b) write_dt_entry(dt, entry, a, b)
#define write_idt_entry(dt, entry, a, b) write_dt_entry(dt, entry, a, b)
#endif

static inline void write_dt_entry(struct desc_struct *dt,
				  int entry, u32 entry_low, u32 entry_high)
{
	dt[entry].a = entry_low;
	dt[entry].b = entry_high;
}

static inline void native_set_ldt(const void *addr, unsigned int entries)
{
	if (likely(entries == 0))
		__asm__ __volatile__("lldt %w0"::"q" (0));
	else {
		unsigned cpu = smp_processor_id();
		__u32 a, b;

		pack_descriptor(&a, &b, (unsigned long)addr,
				entries * sizeof(struct desc_struct) - 1,
				DESCTYPE_LDT, 0);
		write_gdt_entry(get_cpu_gdt_table(cpu), GDT_ENTRY_LDT, a, b);
		__asm__ __volatile__("lldt %w0"::"q" (GDT_ENTRY_LDT*8));
	}
}


static inline void native_load_tr_desc(void)
{
	asm volatile("ltr %w0"::"q" (GDT_ENTRY_TSS*8));
}

static inline void native_load_gdt(const struct Xgt_desc_struct *dtr)
{
	asm volatile("lgdt %0"::"m" (*dtr));
}

static inline void native_load_idt(const struct Xgt_desc_struct *dtr)
{
	asm volatile("lidt %0"::"m" (*dtr));
}

static inline void native_store_gdt(struct Xgt_desc_struct *dtr)
{
	asm ("sgdt %0":"=m" (*dtr));
}

static inline void native_store_idt(struct Xgt_desc_struct *dtr)
{
	asm ("sidt %0":"=m" (*dtr));
}

static inline unsigned long native_store_tr(void)
{
	unsigned long tr;
	asm ("str %0":"=r" (tr));
	return tr;
}

static inline void native_load_tls(struct thread_struct *t, unsigned int cpu)
{
	unsigned int i;
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);

	for (i = 0; i < GDT_ENTRY_TLS_ENTRIES; i++)
		gdt[GDT_ENTRY_TLS_MIN + i] = t->tls_array[i];
}

static inline void _set_gate(int gate, unsigned int type, void *addr, unsigned short seg)
{
	__u32 a, b;
	pack_gate(&a, &b, (unsigned long)addr, seg, type, 0);
	write_idt_entry(idt_table, gate, a, b);
}

static inline void __set_tss_desc(unsigned int cpu, unsigned int entry, const void *addr)
{
	__u32 a, b;
	pack_descriptor(&a, &b, (unsigned long)addr,
			offsetof(struct tss_struct, __cacheline_filler) - 1,
			DESCTYPE_TSS, 0);
	write_gdt_entry(get_cpu_gdt_table(cpu), entry, a, b);
}


#define set_tss_desc(cpu,addr) __set_tss_desc(cpu, GDT_ENTRY_TSS, addr)

#define LDT_entry_a(info) \
	((((info)->base_addr & 0x0000ffff) << 16) | ((info)->limit & 0x0ffff))

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
	0x7000)

#define LDT_empty(info) (\
	(info)->base_addr	== 0	&& \
	(info)->limit		== 0	&& \
	(info)->contents	== 0	&& \
	(info)->read_exec_only	== 1	&& \
	(info)->seg_32bit	== 0	&& \
	(info)->limit_in_pages	== 0	&& \
	(info)->seg_not_present	== 1	&& \
	(info)->useable		== 0	)

static inline void clear_LDT(void)
{
	set_ldt(NULL, 0);
}

/*
 * load one particular LDT into the current CPU
 */
static inline void load_LDT_nolock(mm_context_t *pc)
{
	set_ldt(pc->ldt, pc->size);
}

static inline void load_LDT(mm_context_t *pc)
{
	preempt_disable();
	load_LDT_nolock(pc);
	preempt_enable();
}

static inline unsigned long get_desc_base(unsigned long *desc)
{
	unsigned long base;
	base = ((desc[0] >> 16)  & 0x0000ffff) |
		((desc[1] << 16) & 0x00ff0000) |
		(desc[1] & 0xff000000);
	return base;
}

#else /* __ASSEMBLY__ */

/*
 * GET_DESC_BASE reads the descriptor base of the specified segment.
 *
 * Args:
 *    idx - descriptor index
 *    gdt - GDT pointer
 *    base - 32bit register to which the base will be written
 *    lo_w - lo word of the "base" register
 *    lo_b - lo byte of the "base" register
 *    hi_b - hi byte of the low word of the "base" register
 *
 * Example:
 *    GET_DESC_BASE(GDT_ENTRY_ESPFIX_SS, %ebx, %eax, %ax, %al, %ah)
 *    Will read the base address of GDT_ENTRY_ESPFIX_SS and put it into %eax.
 */
#define GET_DESC_BASE(idx, gdt, base, lo_w, lo_b, hi_b) \
	movb idx*8+4(gdt), lo_b; \
	movb idx*8+7(gdt), hi_b; \
	shll $16, base; \
	movw idx*8+2(gdt), lo_w;

#endif /* !__ASSEMBLY__ */

#endif
