#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#ifdef CONFIG_X86_32
# include "fixmap_32.h"
#else
# include "fixmap_64.h"
#endif

#define clear_fixmap(idx)			\
	__set_fixmap(idx, 0, __pgprot(0))

#endif
