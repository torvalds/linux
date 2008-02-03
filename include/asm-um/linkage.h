#ifndef __ASM_UM_LINKAGE_H
#define __ASM_UM_LINKAGE_H

#include "asm/arch/linkage.h"


/* <linux/linkage.h> will pick sane defaults */
#ifdef CONFIG_GPROF
#undef fastcall
#endif

#endif
