#ifndef _ASM_DESC_H_
#define _ASM_DESC_H_

#ifndef __ASSEMBLY__
#include <asm/desc_defs.h>
#include <asm/ldt.h>
#include <asm/mmu.h>

static inline void fill_ldt(struct desc_struct *desc, struct user_desc *info)
{
	desc->limit0 = info->limit & 0x0ffff;
	desc->base0 = info->base_addr & 0x0000ffff;

	desc->base1 = (info->base_addr & 0x00ff0000) >> 16;
	desc->type = (info->read_exec_only ^ 1) << 1;
	desc->type |= info->contents << 2;
	desc->s = 1;
	desc->dpl = 0x3;
	desc->p = info->seg_not_present ^ 1;
	desc->limit = (info->limit & 0xf0000) >> 16;
	desc->avl = info->useable;
	desc->d = info->seg_32bit;
	desc->g = info->limit_in_pages;
	desc->base2 = (info->base_addr & 0xff000000) >> 24;
}

extern struct desc_ptr idt_descr;
extern gate_desc idt_table[];

#ifdef CONFIG_X86_32
# include "desc_32.h"
#else
# include "desc_64.h"
#endif

#define _LDT_empty(info) (\
	(info)->base_addr	== 0	&& \
	(info)->limit		== 0	&& \
	(info)->contents	== 0	&& \
	(info)->read_exec_only	== 1	&& \
	(info)->seg_32bit	== 0	&& \
	(info)->limit_in_pages	== 0	&& \
	(info)->seg_not_present	== 1	&& \
	(info)->useable		== 0)

#ifdef CONFIG_X86_64
#define LDT_empty(info) (_LDT_empty(info) && ((info)->lm == 0))
#else
#define LDT_empty(info) (_LDT_empty(info))
#endif

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

static inline unsigned long get_desc_base(struct desc_struct *desc)
{
	return desc->base0 | ((desc->base1) << 16) | ((desc->base2) << 24);
}

#else
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


#endif /* __ASSEMBLY__ */

#endif
