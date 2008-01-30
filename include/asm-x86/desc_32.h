#ifndef __ARCH_DESC_H
#define __ARCH_DESC_H

#include <asm/ldt.h>
#include <asm/segment.h>
#include <asm/desc_defs.h>

#ifndef __ASSEMBLY__

#include <linux/preempt.h>
#include <linux/percpu.h>

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
