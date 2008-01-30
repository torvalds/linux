#ifndef _ASM_DESC_H_
#define _ASM_DESC_H_

#ifndef __ASSEMBLY__
#include <asm/desc_defs.h>
#include <asm/ldt.h>

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

#endif

#ifdef CONFIG_X86_32
# include "desc_32.h"
#else
# include "desc_64.h"
#endif

#endif
